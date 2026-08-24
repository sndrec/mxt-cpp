from __future__ import annotations

import hashlib
import json
import os
import re
import threading
import time
import traceback
import uuid
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from authoritative_store import AuthoritativeLeaderboardStore, AuthoritativeStoreError
from replay_verifier import ReplayVerificationError, ReplayVerifier
from steam_web_api import SteamWebApi, SteamWebApiError


TICKET_PATTERN = re.compile(r"^[0-9a-fA-F]{2,5120}$")
BOARD_PATTERN = re.compile(r"^[a-z0-9_-]{1,128}$")


@dataclass(frozen=True)
class ServiceConfig:
    listen_host: str
    listen_port: int
    app_id: int
    auth_app_ids: frozenset[int]
    publisher_key: str
    ticket_identity: str
    maximum_replay_bytes: int
    verifier_concurrency: int
    ticket_reuse_window_seconds: int
    boards: dict[str, dict[str, Any]]
    steam_api: SteamWebApi
    authoritative_store: AuthoritativeLeaderboardStore
    replay_verifier: ReplayVerifier
    log_path: Path


class JsonlEventLog:
    MAX_BYTES = 16 * 1024 * 1024
    BACKUP_COUNT = 3

    def __init__(self, path: Path) -> None:
        self.path = path
        self._lock = threading.Lock()
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def write(self, event: str, **fields: Any) -> None:
        now = time.time()
        record = {
            "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now)),
            "unix_time": now,
            "event": event,
            **fields,
        }
        line = json.dumps(record, separators=(",", ":"), ensure_ascii=True)
        with self._lock:
            self._rotate_if_needed(len(line) + 1)
            with self.path.open("a", encoding="utf-8", newline="\n") as output:
                output.write(line)
                output.write("\n")
        print(f"MXT_LEADERBOARD_SERVICE {line}", flush=True)

    def _rotate_if_needed(self, incoming_bytes: int) -> None:
        try:
            current_bytes = self.path.stat().st_size
        except FileNotFoundError:
            return
        if current_bytes + incoming_bytes <= self.MAX_BYTES:
            return
        oldest = self.path.with_name(f"{self.path.name}.{self.BACKUP_COUNT}")
        oldest.unlink(missing_ok=True)
        for index in range(self.BACKUP_COUNT - 1, 0, -1):
            source = self.path.with_name(f"{self.path.name}.{index}")
            if source.is_file():
                source.replace(self.path.with_name(f"{self.path.name}.{index + 1}"))
        self.path.replace(self.path.with_name(f"{self.path.name}.1"))


class TicketReplayGuard:
    def __init__(self, window_seconds: int) -> None:
        self._window_seconds = window_seconds
        self._lock = threading.Lock()
        self._tickets: dict[bytes, float] = {}

    def consume(self, ticket_hex: str) -> bool:
        digest = hashlib.sha256(ticket_hex.encode("ascii")).digest()
        now = time.monotonic()
        with self._lock:
            expired = [key for key, deadline in self._tickets.items() if deadline <= now]
            for key in expired:
                del self._tickets[key]
            if digest in self._tickets:
                return False
            self._tickets[digest] = now + self._window_seconds
            return True


class LeaderboardHttpServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], config: ServiceConfig) -> None:
        super().__init__(address, LeaderboardRequestHandler)
        self.config = config
        self.verifier_slots = threading.BoundedSemaphore(config.verifier_concurrency)
        self.ticket_guard = TicketReplayGuard(config.ticket_reuse_window_seconds)
        self.event_log = JsonlEventLog(config.log_path)

    def log_event(self, event: str, **fields: Any) -> None:
        try:
            self.event_log.write(event, **fields)
        except OSError as exc:
            print(f"MXT_LEADERBOARD_SERVICE_LOG_ERROR event={event} error={exc}", flush=True)


class LeaderboardRequestHandler(BaseHTTPRequestHandler):
    server: LeaderboardHttpServer
    protocol_version = "HTTP/1.1"

    def log_message(self, format_string: str, *args: Any) -> None:
        print(f"{self.address_string()} - {format_string % args}", flush=True)

    def _json(self, status: HTTPStatus, payload: dict[str, Any], request_id: str = "") -> None:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
        self.send_response(status.value)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if request_id:
            self.send_header("X-MXT-Request-ID", request_id)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def do_GET(self) -> None:
        if self.path == "/healthz":
            self._json(HTTPStatus.OK, {"ok": True})
            return
        self._json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"})

    def do_POST(self) -> None:
        request_id = uuid.uuid4().hex[:16]
        started = time.monotonic()
        try:
            self._handle_post(request_id, started)
        except Exception as exc:  # This boundary must keep one bad request from killing its worker thread.
            self.server.log_event(
                "submission_unhandled_exception",
                request_id=request_id,
                duration_msec=round((time.monotonic() - started) * 1000.0, 3),
                exception_type=type(exc).__name__,
                message=str(exc),
                traceback=traceback.format_exc(),
            )
            try:
                self._json(
                    HTTPStatus.INTERNAL_SERVER_ERROR,
                    {
                        "ok": False,
                        "error": "internal_server_error",
                        "message": f"Leaderboard backend error; reference {request_id}.",
                        "request_id": request_id,
                    },
                    request_id,
                )
            except (BrokenPipeError, ConnectionResetError):
                self.server.log_event("submission_response_disconnected", request_id=request_id)

    def _handle_post(self, request_id: str, started: float) -> None:
        if self.path != "/v1/time-attack/submit":
            self._json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"}, request_id)
            return
        config = self.server.config
        if self.headers.get_content_type() != "application/vnd.mxt.replay+json":
            self.server.log_event("submission_rejected", request_id=request_id, stage="headers", error="invalid_content_type")
            self._json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, {"ok": False, "error": "invalid_content_type"}, request_id)
            return
        try:
            content_length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            content_length = -1
        if content_length <= 0 or content_length > config.maximum_replay_bytes:
            self.server.log_event(
                "submission_rejected", request_id=request_id, stage="headers", error="invalid_replay_size", content_length=content_length
            )
            self._json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"ok": False, "error": "invalid_replay_size"}, request_id)
            return
        authorization = self.headers.get("Authorization", "")
        ticket_hex = authorization[12:].strip() if authorization.startswith("SteamTicket ") else ""
        identity = self.headers.get("X-MXT-Ticket-Identity", "")
        board_name = self.headers.get("X-MXT-Board", "")
        auth_app_id_text = self.headers.get("X-MXT-Steam-App-ID", "")
        claimed_score_text = self.headers.get("X-MXT-Claimed-Score-Milliseconds", "")
        if not TICKET_PATTERN.fullmatch(ticket_hex) or (len(ticket_hex) & 1) != 0 or identity != config.ticket_identity:
            self.server.log_event("submission_rejected", request_id=request_id, stage="authentication", error="invalid_authentication_ticket")
            self._json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "invalid_authentication_ticket"}, request_id)
            return
        if not BOARD_PATTERN.fullmatch(board_name) or board_name not in config.boards:
            self.server.log_event("submission_rejected", request_id=request_id, stage="headers", error="unknown_leaderboard", board_name=board_name)
            self._json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "unknown_leaderboard"}, request_id)
            return
        try:
            auth_app_id = int(auth_app_id_text)
        except ValueError:
            auth_app_id = 0
        if auth_app_id not in config.auth_app_ids:
            self.server.log_event(
                "submission_rejected", request_id=request_id, stage="headers", error="unapproved_steam_app", auth_app_id=auth_app_id
            )
            self._json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unapproved_steam_app"}, request_id)
            return
        try:
            claimed_score = int(claimed_score_text)
        except ValueError:
            self.server.log_event("submission_rejected", request_id=request_id, stage="headers", error="invalid_claimed_score")
            self._json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "invalid_claimed_score"}, request_id)
            return
        if claimed_score <= 0 or claimed_score > 0x7FFFFFFF:
            self.server.log_event(
                "submission_rejected", request_id=request_id, stage="headers", error="invalid_claimed_score", claimed_score=claimed_score
            )
            self._json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "invalid_claimed_score"}, request_id)
            return
        self.server.log_event(
            "submission_received",
            request_id=request_id,
            board_name=board_name,
            auth_app_id=auth_app_id,
            claimed_score=claimed_score,
            content_length=content_length,
        )
        replay_bytes = self.rfile.read(content_length)
        if len(replay_bytes) != content_length:
            self.server.log_event(
                "submission_rejected",
                request_id=request_id,
                stage="body",
                error="incomplete_replay",
                expected_bytes=content_length,
                received_bytes=len(replay_bytes),
            )
            self._json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": "incomplete_replay"}, request_id)
            return
        auth_started = time.monotonic()
        try:
            steam_id = config.steam_api.authenticate_ticket(ticket_hex, identity, auth_app_id)
        except SteamWebApiError as exc:
            self.server.log_event(
                "submission_failed", request_id=request_id, stage="steam_authentication", error="steam_authentication_failed", message=str(exc)
            )
            self._json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "steam_authentication_failed", "message": str(exc)}, request_id)
            return
        self.server.log_event(
            "submission_authenticated",
            request_id=request_id,
            steam_id=str(steam_id),
            duration_msec=round((time.monotonic() - auth_started) * 1000.0, 3),
        )
        try:
            owns_app = config.steam_api.check_app_ownership(steam_id, auth_app_id)
        except SteamWebApiError as exc:
            self.server.log_event(
                "submission_failed", request_id=request_id, stage="steam_ownership", error="steam_ownership_unavailable", steam_id=str(steam_id), message=str(exc)
            )
            self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"ok": False, "error": "steam_ownership_unavailable", "message": str(exc)}, request_id)
            return
        if not owns_app:
            self.server.log_event("submission_rejected", request_id=request_id, stage="steam_ownership", error="app_not_owned", steam_id=str(steam_id))
            self._json(HTTPStatus.FORBIDDEN, {"ok": False, "error": "app_not_owned"}, request_id)
            return
        if not self.server.verifier_slots.acquire(blocking=False):
            self.server.log_event("submission_deferred", request_id=request_id, stage="verification", error="verifier_busy")
            self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"ok": False, "error": "verifier_busy"}, request_id)
            return
        if not self.server.ticket_guard.consume(ticket_hex):
            self.server.verifier_slots.release()
            self.server.log_event("submission_rejected", request_id=request_id, stage="verification", error="authentication_ticket_already_used")
            self._json(HTTPStatus.CONFLICT, {"ok": False, "error": "authentication_ticket_already_used"}, request_id)
            return
        verify_started = time.monotonic()
        self.server.log_event("submission_verification_started", request_id=request_id, board_name=board_name)
        try:
            verified = config.replay_verifier.verify(replay_bytes, board_name, claimed_score)
        except ReplayVerificationError as exc:
            self.server.log_event(
                "submission_rejected", request_id=request_id, stage="verification", error="replay_rejected", message=str(exc), duration_msec=round((time.monotonic() - verify_started) * 1000.0, 3)
            )
            self._json(HTTPStatus.UNPROCESSABLE_ENTITY, {"ok": False, "error": "replay_rejected", "message": str(exc)}, request_id)
            return
        finally:
            self.server.verifier_slots.release()
        self.server.log_event(
            "submission_verified",
            request_id=request_id,
            board_name=board_name,
            replay_sha256=str(verified.get("replay_sha256", "")),
            replay_schema_version=int(verified.get("replay_schema_version", 0)),
            ruleset_revision=int(verified.get("ruleset_revision", 0)),
            game_version=verified.get("game_version", {}),
            duration_msec=round((time.monotonic() - verify_started) * 1000.0, 3),
        )
        archive_started = time.monotonic()
        board = config.boards[board_name]
        replay_digest = str(verified.get("replay_sha256", ""))
        envelope = {
            "schema_version": 1,
            "run_kind": "ranked_time_attack",
            "track_source": "official",
            "vehicle_source": "official",
            "steam_id": str(steam_id),
            "auth_app_id": auth_app_id,
            "board_id": board_name,
            "track_content_id": str(verified.get("track_content_id", "")),
            "track_gameplay_digest": str(verified.get("track_gameplay_digest", "")),
            "track_title": str(board.get("track_title", board.get("track_slug", board_name))),
            "vehicle_content_id": str(verified.get("vehicle_content_id", "")),
            "vehicle_gameplay_digest": str(verified.get("vehicle_gameplay_digest", "")),
            "machine_setting_percent": int(verified.get("machine_setting_percent", -1)),
            "score_milliseconds": claimed_score,
            "ruleset_revision": int(verified.get("ruleset_revision", 0)),
            "replay_schema_version": int(verified.get("replay_schema_version", 0)),
            "game_version": verified.get("game_version", {}),
            "replay_sha256": replay_digest,
            "replay_byte_length": len(replay_bytes),
            "persona_name": "",
            "avatar_url": "",
            "profile_url": f"https://steamcommunity.com/profiles/{steam_id}",
            "source_timestamp_unix": int(time.time()),
            "provenance": "native_submission",
        }
        self.server.log_event(
            "submission_archive_started",
            request_id=request_id,
            board_name=board_name,
            steam_id=str(steam_id),
            claimed_score=claimed_score,
            replay_sha256=replay_digest,
        )
        try:
            archive = config.authoritative_store.archive_verified_run(replay_bytes, envelope)
        except AuthoritativeStoreError as exc:
            self.server.log_event(
                "submission_failed",
                request_id=request_id,
                stage="authoritative_archive",
                error="authoritative_archive_failed",
                exception_type=type(exc).__name__,
                message=str(exc),
                steam_id=str(steam_id),
                replay_sha256=replay_digest,
                duration_msec=round((time.monotonic() - archive_started) * 1000.0, 3),
            )
            self._json(
                HTTPStatus.SERVICE_UNAVAILABLE,
                {"ok": False, "error": "authoritative_archive_failed", "message": str(exc), "request_id": request_id},
                request_id,
            )
            return
        self.server.log_event(
            "submission_archive_completed",
            request_id=request_id,
            board_name=board_name,
            steam_id=str(steam_id),
            run_id=str(archive.get("run_id", "")),
            run_created=bool(archive.get("run_created", False)),
            vehicle_best_changed=bool(archive.get("vehicle_best_changed", False)),
            global_rank=int(archive.get("global_rank", 0) or 0),
            duration_msec=round((time.monotonic() - archive_started) * 1000.0, 3),
        )
        self._json(
            HTTPStatus.OK,
            {
                "ok": True,
                "archived": True,
                "steam_id": str(steam_id),
                "board_name": board_name,
                "score_milliseconds": claimed_score,
                "replay_sha256": replay_digest,
                "run_id": str(archive.get("run_id", "")),
                "run_created": bool(archive.get("run_created", False)),
                "vehicle_best_changed": bool(archive.get("vehicle_best_changed", False)),
                "is_vehicle_best": bool(archive.get("is_vehicle_best", False)),
                "personal_best_milliseconds": int(archive.get("personal_best_milliseconds", 0) or 0),
                "global_rank": int(archive.get("global_rank", 0) or 0),
            },
            request_id,
        )
        self.server.log_event(
            "submission_completed",
            request_id=request_id,
            board_name=board_name,
            duration_msec=round((time.monotonic() - started) * 1000.0, 3),
        )


def _required_environment(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError(f"required environment variable {name} is missing")
    return value


def _positive_integer_environment(name: str, default: int | None = None) -> int:
    raw = os.environ.get(name, "")
    if not raw and default is not None:
        return default
    if not raw:
        raise RuntimeError(f"required environment variable {name} is missing")
    try:
        value = int(raw)
    except ValueError as exc:
        raise RuntimeError(f"{name} must be a positive integer") from exc
    if value <= 0:
        raise RuntimeError(f"{name} must be positive")
    return value


def load_config() -> ServiceConfig:
    service_root = Path(__file__).resolve().parent
    repository_root = service_root.parents[1]
    app_id = _positive_integer_environment("MXT_STEAM_APP_ID")
    auth_app_ids = frozenset(
        int(value.strip())
        for value in os.environ.get("MXT_STEAM_AUTH_APP_IDS", str(app_id)).split(",")
        if value.strip()
    )
    if not auth_app_ids or any(value <= 0 for value in auth_app_ids):
        raise RuntimeError("MXT_STEAM_AUTH_APP_IDS must contain positive comma-separated App IDs")
    publisher_key = _required_environment("MXT_STEAM_PUBLISHER_KEY")
    authoritative_url = _required_environment("MXT_LEADERBOARD_API_URL")
    authoritative_secret = _required_environment("MXT_LEADERBOARD_INGEST_SECRET")
    game_executable_text = os.environ.get("MXT_GAME_EXECUTABLE", "").strip()
    godot_executable_text = os.environ.get("MXT_GODOT_EXE", "").strip()
    godot_project_text = os.environ.get("MXT_GODOT_PROJECT", str(repository_root / "mxto")).strip()
    game_executable = Path(game_executable_text).resolve() if game_executable_text else None
    godot_executable = Path(godot_executable_text).resolve() if godot_executable_text else None
    godot_project = Path(godot_project_text).resolve() if godot_executable is not None else None
    for executable in (game_executable, godot_executable):
        if executable is not None and not executable.is_file():
            raise RuntimeError(f"verifier executable does not exist: {executable}")
    if godot_project is not None and not godot_project.is_dir():
        raise RuntimeError(f"Godot project directory does not exist: {godot_project}")
    if game_executable is None and godot_executable is None:
        raise RuntimeError("MXT_GAME_EXECUTABLE or MXT_GODOT_EXE must configure the replay verifier")
    manifest_path = Path(
        os.environ.get("MXT_LEADERBOARD_MANIFEST", str(repository_root / "mxto" / "steam" / "leaderboards.json"))
    ).resolve()
    steam_base_url = os.environ.get("MXT_STEAM_API_BASE", "https://partner.steam-api.com").strip()
    timeout_seconds = float(os.environ.get("MXT_REPLAY_VERIFY_TIMEOUT_SECONDS", "120"))
    manifest_reader = ReplayVerifier(
        manifest_path=manifest_path,
        game_executable=game_executable,
        godot_executable=godot_executable,
        godot_project=godot_project,
        timeout_seconds=timeout_seconds,
    )
    manifest_boards = manifest_reader.manifest_boards()
    official_boards = {
        name: board
        for name, board in manifest_boards.items()
        if str(board.get("track_source", "official")) == "official"
    }
    if not official_boards:
        raise RuntimeError("leaderboard manifest contains no official ranked boards")
    replay_verifier = ReplayVerifier(
        manifest_path=manifest_path,
        game_executable=game_executable,
        godot_executable=godot_executable,
        godot_project=godot_project,
        timeout_seconds=timeout_seconds,
    )
    log_path = Path(
        os.environ.get("MXT_LEADERBOARD_LOG_PATH", str(service_root.parent / "logs" / "leaderboard-service.jsonl"))
    ).resolve()
    return ServiceConfig(
        listen_host=os.environ.get("MXT_LEADERBOARD_LISTEN_HOST", "127.0.0.1"),
        listen_port=_positive_integer_environment("MXT_LEADERBOARD_LISTEN_PORT", 8787),
        app_id=app_id,
        auth_app_ids=auth_app_ids,
        publisher_key=publisher_key,
        ticket_identity=os.environ.get("MXT_STEAM_TICKET_IDENTITY", "mxt-leaderboard-v1"),
        maximum_replay_bytes=_positive_integer_environment("MXT_MAX_REPLAY_BYTES", 16 * 1024 * 1024),
        verifier_concurrency=_positive_integer_environment("MXT_VERIFIER_CONCURRENCY", 2),
        ticket_reuse_window_seconds=_positive_integer_environment("MXT_TICKET_REUSE_WINDOW_SECONDS", 600),
        boards=official_boards,
        steam_api=SteamWebApi(publisher_key, app_id, steam_base_url),
        authoritative_store=AuthoritativeLeaderboardStore(
            authoritative_url,
            authoritative_secret,
            float(os.environ.get("MXT_LEADERBOARD_API_TIMEOUT_SECONDS", "30")),
        ),
        replay_verifier=replay_verifier,
        log_path=log_path,
    )


def main() -> None:
    config = load_config()
    server = LeaderboardHttpServer((config.listen_host, config.listen_port), config)
    server.log_event(
        "service_started",
        listen_host=config.listen_host,
        listen_port=config.listen_port,
        app_id=config.app_id,
        authenticated_app_ids=sorted(config.auth_app_ids),
        board_count=len(config.boards),
        verifier_concurrency=config.verifier_concurrency,
        log_path=str(config.log_path),
    )
    print(f"MaxX Throttle leaderboard verifier listening on {config.listen_host}:{config.listen_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
