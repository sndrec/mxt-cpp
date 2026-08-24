from __future__ import annotations

import hashlib
import hmac
import json
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from authoritative_store import AuthoritativeLeaderboardStore, AuthoritativeStoreError
from replay_verifier import ReplayVerificationError, ReplayVerifier


class HistoricalImportError(RuntimeError):
    pass


@dataclass(frozen=True)
class HistoricalScoreStore:
    base_url: str
    migration_secret: str
    timeout_seconds: float = 30.0

    def import_score(self, record: dict[str, Any]) -> dict[str, Any]:
        body = json.dumps(record, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
        timestamp = str(int(time.time()))
        signature = hmac.new(
            self.migration_secret.encode("utf-8"),
            timestamp.encode("ascii") + b"\n" + body,
            hashlib.sha256,
        ).hexdigest()
        request = urllib.request.Request(
            self.base_url.rstrip("/") + "/v1/admin/import-historical-score",
            data=body,
            method="POST",
            headers={
                "Accept": "application/json",
                "Content-Length": str(len(body)),
                "Content-Type": "application/json",
                "User-Agent": "MaxX-Throttle-Steam-History-Importer/1",
                "X-MXT-Migration-Signature": signature,
                "X-MXT-Migration-Timestamp": timestamp,
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:
                payload = response.read(2 * 1024 * 1024)
        except urllib.error.HTTPError as exc:
            detail = exc.read(64 * 1024).decode("utf-8", errors="replace")
            raise HistoricalImportError(
                f"authority rejected historical score with HTTP {exc.code}: {detail[:1024]}"
            ) from exc
        except (urllib.error.URLError, TimeoutError) as exc:
            raise HistoricalImportError(f"historical score request failed: {exc}") from exc
        try:
            parsed = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise HistoricalImportError("authority returned invalid historical import JSON") from exc
        if not isinstance(parsed, dict) or parsed.get("ok") is not True:
            raise HistoricalImportError("authority did not confirm historical score import")
        return parsed


@dataclass
class SteamHistoryImporter:
    snapshot_path: Path
    report_path: Path
    replay_verifier: ReplayVerifier
    replay_store: AuthoritativeLeaderboardStore
    score_store: HistoricalScoreStore
    log: Callable[[str], None] = print

    def run(self) -> dict[str, Any]:
        snapshot = self._read_snapshot()
        entries = snapshot.get("entries", [])
        if not isinstance(entries, list):
            raise HistoricalImportError("snapshot entries are invalid")
        report: dict[str, Any] = {
            "format_revision": 1,
            "complete": False,
            "snapshot_path": str(self.snapshot_path),
            "started_unix": int(time.time()),
            "completed_unix": 0,
            "counts": {
                "scanned": 0,
                "replay_backed_imported": 0,
                "score_only_imported": 0,
                "invalid": 0,
                "failed": 0,
            },
            "results": [],
        }
        self._write_report(report)
        for index, value in enumerate(entries):
            report["counts"]["scanned"] += 1
            if not isinstance(value, dict):
                self._record(report, {}, "invalid", "invalid_snapshot_entry")
                continue
            entry = value
            self.log(
                f"Steam import {index + 1}/{len(entries)} board={entry.get('board_name', '')} "
                f"rank={entry.get('global_rank', 0)} steam_id={entry.get('steam_id', '')}"
            )
            try:
                if str(entry.get("replay_status", "")) == "downloaded":
                    outcome = self._import_replay_backed(snapshot, entry)
                    report["counts"]["replay_backed_imported"] += 1
                    self._record(report, entry, "replay_backed_imported", "", outcome)
                else:
                    outcome = self._import_score_only(snapshot, entry)
                    report["counts"]["score_only_imported"] += 1
                    self._record(report, entry, "score_only_imported", "", outcome)
            except (HistoricalImportError, ReplayVerificationError, AuthoritativeStoreError, OSError) as exc:
                status = "invalid" if isinstance(exc, ReplayVerificationError) else "failed"
                report["counts"][status] += 1
                self._record(report, entry, status, str(exc))
            self._write_report(report)
        report["complete"] = report["counts"]["failed"] == 0
        report["completed_unix"] = int(time.time())
        self._write_report(report)
        return report

    def _read_snapshot(self) -> dict[str, Any]:
        try:
            snapshot = json.loads(self.snapshot_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise HistoricalImportError("Steam leaderboard snapshot could not be loaded") from exc
        if not isinstance(snapshot, dict) or int(snapshot.get("format_revision", -1)) != 1:
            raise HistoricalImportError("Steam leaderboard snapshot format is unsupported")
        if snapshot.get("complete") is not True:
            raise HistoricalImportError("Steam leaderboard snapshot is incomplete")
        return snapshot

    def _import_replay_backed(self, snapshot: dict[str, Any], entry: dict[str, Any]) -> dict[str, Any]:
        relative_path = str(entry.get("replay_path", ""))
        replay_path = (self.snapshot_path.parent / relative_path).resolve()
        root = self.snapshot_path.parent.resolve()
        if root not in replay_path.parents or not replay_path.is_file():
            raise HistoricalImportError("snapshot replay path is missing or escapes its root")
        replay_bytes = replay_path.read_bytes()
        replay_sha256 = "sha256:" + hashlib.sha256(replay_bytes).hexdigest()
        if replay_sha256 != str(entry.get("replay_sha256", "")):
            raise HistoricalImportError("snapshot replay digest mismatch")
        if len(replay_bytes) != int(entry.get("replay_byte_length", 0)):
            raise HistoricalImportError("snapshot replay length mismatch")
        board_id = str(entry.get("board_name", ""))
        score = int(entry.get("score_milliseconds", 0))
        verified = self.replay_verifier.verify(replay_bytes, board_id, score)
        steam_id = self._steam_id(entry)
        metadata = {
            "schema_version": 1,
            "run_kind": "ranked_time_attack",
            "track_source": "official",
            "vehicle_source": "official",
            "steam_id": steam_id,
            "auth_app_id": int(snapshot.get("steam_app_id", 0)),
            "board_id": board_id,
            "track_content_id": str(verified.get("track_content_id", "")),
            "track_gameplay_digest": str(verified.get("track_gameplay_digest", "")),
            "track_title": str(entry.get("track_title", "")),
            "vehicle_content_id": str(verified.get("vehicle_content_id", "")),
            "vehicle_gameplay_digest": str(verified.get("vehicle_gameplay_digest", "")),
            "machine_setting_percent": int(verified.get("machine_setting_percent", -1)),
            "score_milliseconds": score,
            "ruleset_revision": int(verified.get("ruleset_revision", 0)),
            "replay_schema_version": int(verified.get("replay_schema_version", 0)),
            "game_version": verified.get("game_version", {}),
            "replay_sha256": replay_sha256,
            "replay_byte_length": len(replay_bytes),
            "persona_name": str(entry.get("persona_name", ""))[:128],
            "avatar_url": "",
            "profile_url": f"https://steamcommunity.com/profiles/{steam_id}",
            "source_timestamp_unix": int(snapshot.get("snapshot_unix", 0)),
            "provenance": "steam_import",
        }
        return self.replay_store.archive_verified_run(replay_bytes, metadata)

    def _import_score_only(self, snapshot: dict[str, Any], entry: dict[str, Any]) -> dict[str, Any]:
        steam_id = self._steam_id(entry)
        source_details = entry.get("source_details", [])
        if not isinstance(source_details, list):
            raise HistoricalImportError("historical Steam details are invalid")
        record = {
            "schema_version": 1,
            "steam_id": steam_id,
            "auth_app_id": int(snapshot.get("steam_app_id", 0)),
            "board_id": str(entry.get("board_name", "")),
            "track_content_id": str(entry.get("track_content_id", "")),
            "track_gameplay_digest": str(entry.get("track_gameplay_digest", "")),
            "track_title": str(entry.get("track_title", "")),
            "score_milliseconds": int(entry.get("score_milliseconds", 0)),
            "ruleset_revision": int(entry.get("ruleset_revision", 0)),
            "persona_name": str(entry.get("persona_name", ""))[:128],
            "avatar_url": "",
            "profile_url": f"https://steamcommunity.com/profiles/{steam_id}",
            "source_timestamp_unix": int(snapshot.get("snapshot_unix", 0)),
            "source_global_rank": int(entry.get("global_rank", 0)),
            "source_ugc_handle": str(entry.get("ugc_handle", "")),
            "source_details": source_details,
            "unavailable_reason": str(entry.get("replay_unavailable_reason", "replay_unavailable")),
            "provenance": "steam_import_score_only",
        }
        return self.score_store.import_score(record)

    @staticmethod
    def _steam_id(entry: dict[str, Any]) -> str:
        steam_id = str(entry.get("steam_id", ""))
        if not steam_id.isdecimal() or len(steam_id) < 17 or len(steam_id) > 20:
            raise HistoricalImportError("snapshot entry has an invalid Steam ID")
        return steam_id

    @staticmethod
    def _record(
        report: dict[str, Any],
        entry: dict[str, Any],
        status: str,
        message: str,
        authority_result: dict[str, Any] | None = None,
    ) -> None:
        result = {
            "board_name": str(entry.get("board_name", "")),
            "global_rank": int(entry.get("global_rank", 0)),
            "steam_id": str(entry.get("steam_id", "")),
            "score_milliseconds": int(entry.get("score_milliseconds", 0)),
            "replay_sha256": str(entry.get("replay_sha256", "")),
            "status": status,
        }
        if message:
            result["message"] = message
        if authority_result is not None:
            result["authority_result"] = authority_result
        report["results"].append(result)

    def _write_report(self, report: dict[str, Any]) -> None:
        self.report_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.report_path.with_suffix(self.report_path.suffix + ".tmp")
        temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        temporary.replace(self.report_path)
