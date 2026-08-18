from __future__ import annotations

import json
import hashlib
import os
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


RESULT_PREFIX = "MXT_LEADERBOARD_VERIFY_RESULT "
FAIL_PREFIX = "MXT_LEADERBOARD_VERIFY_FAIL "


class ReplayVerificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class ReplayVerifier:
    manifest_path: Path
    game_executable: Path | None
    godot_executable: Path | None
    godot_project: Path | None
    timeout_seconds: float
    temporary_directory: Path | None = None
    trusted_workshop_packages: tuple[tuple[str, int, Path], ...] = ()

    def _command(self, replay_path: Path, requested_board: str) -> list[str]:
        replay_arguments = [
            "--headless",
            "--replay",
            str(replay_path),
            "--leaderboard-replay-verify",
            "--skip-replay-seek-bake",
        ]
        trusted_package_arguments: list[str] = []
        for board_name, published_file_id, package_path in self.trusted_workshop_packages:
            if board_name == requested_board:
                trusted_package_arguments.extend(
                    ["--trusted-workshop-package", str(published_file_id), str(package_path)]
                )
                break
        if self.game_executable is not None:
            return [str(self.game_executable), *replay_arguments, *trusted_package_arguments]
        if self.godot_executable is not None and self.godot_project is not None:
            return [
                str(self.godot_executable),
                "--headless",
                "--path",
                str(self.godot_project),
                "--replay",
                str(replay_path),
                "--leaderboard-replay-verify",
                "--skip-replay-seek-bake",
                *trusted_package_arguments,
            ]
        raise ReplayVerificationError("no game verifier executable is configured")

    def manifest_boards(self) -> dict[str, dict[str, Any]]:
        try:
            parsed = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise ReplayVerificationError("leaderboard manifest could not be loaded") from exc
        boards = parsed.get("boards", []) if isinstance(parsed, dict) else []
        if not isinstance(boards, list):
            raise ReplayVerificationError("leaderboard manifest has no board list")
        output: dict[str, dict[str, Any]] = {}
        for board in boards:
            if isinstance(board, dict) and isinstance(board.get("steam_name"), str):
                output[board["steam_name"]] = board
        return output

    def verify(self, replay_bytes: bytes, requested_board: str, claimed_score: int) -> dict[str, Any]:
        boards = self.manifest_boards()
        if requested_board not in boards:
            raise ReplayVerificationError("requested leaderboard is not in the official manifest")
        replay_sha256 = "sha256:" + hashlib.sha256(replay_bytes).hexdigest()
        temp_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="wb",
                suffix=".replay.json",
                prefix="mxt_leaderboard_",
                dir=self.temporary_directory,
                delete=False,
            ) as replay_file:
                replay_file.write(replay_bytes)
                temp_path = Path(replay_file.name)
            child_environment = os.environ.copy()
            for secret_name in ("MXT_STEAM_PUBLISHER_KEY",):
                child_environment.pop(secret_name, None)
            creation_flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            try:
                completed = subprocess.run(
                    self._command(temp_path, requested_board),
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    timeout=self.timeout_seconds,
                    check=False,
                    env=child_environment,
                    creationflags=creation_flags,
                )
            except subprocess.TimeoutExpired as exc:
                raise ReplayVerificationError("replay verification timed out") from exc
            output = completed.stdout.decode("utf-8", errors="replace")
            verified: dict[str, Any] | None = None
            for line in output.splitlines():
                if line.startswith(RESULT_PREFIX):
                    try:
                        value = json.loads(line[len(RESULT_PREFIX) :])
                    except json.JSONDecodeError:
                        continue
                    if isinstance(value, dict):
                        verified = value
            if completed.returncode != 0 or verified is None or verified.get("valid") is not True:
                reason = "deterministic replay verification failed"
                for line in output.splitlines():
                    if line.startswith(FAIL_PREFIX):
                        reason = line[len(FAIL_PREFIX) :][:512]
                raise ReplayVerificationError(reason)
        finally:
            if temp_path is not None:
                temp_path.unlink(missing_ok=True)

        board = boards[requested_board]
        if str(verified.get("board_name", "")) != requested_board:
            raise ReplayVerificationError("verified replay belongs to a different leaderboard")
        if str(verified.get("track_gameplay_digest", "")) != str(board.get("track_gameplay_digest", "")):
            raise ReplayVerificationError("verified track digest does not match the leaderboard manifest")
        if int(verified.get("score_milliseconds", -1)) != claimed_score:
            raise ReplayVerificationError("claimed score does not match the re-simulated score")
        if claimed_score <= 0 or claimed_score > 0x7FFFFFFF:
            raise ReplayVerificationError("verified score is outside Steam's supported score range")
        verified["replay_sha256"] = replay_sha256
        return verified
