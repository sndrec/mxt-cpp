from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from replay_verifier import RESULT_PREFIX, ReplayVerifier


class VerifierContractTests(unittest.TestCase):
    def test_exact_received_bytes_are_bound_to_verification_result(self) -> None:
        replay_bytes = b'{"schema_version":4,"frames":[]}\n'
        track_digest = "sha256:" + "22" * 32
        vehicle_digest = "sha256:" + "33" * 32
        verified = {
            "valid": True,
            "board_name": "test-board",
            "track_gameplay_digest": track_digest,
            "vehicle_gameplay_digest": vehicle_digest,
            "machine_setting_percent": 50,
            "score_milliseconds": 12345,
            "ruleset_revision": 7,
            "replay_schema_version": 4,
            "game_version": {"major": 3, "compatibility": 2, "patch": 19},
        }
        completed = subprocess.CompletedProcess(
            args=[], returncode=0, stdout=(RESULT_PREFIX + json.dumps(verified) + "\n").encode()
        )
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "leaderboards.json"
            manifest.write_text(
                json.dumps(
                    {
                        "boards": [
                            {
                                "steam_name": "test-board",
                                "track_gameplay_digest": track_digest,
                            },
                            {
                                "steam_name": "custom-board",
                                "track_source": "curated_workshop",
                                "track_gameplay_digest": "sha256:" + "44" * 32,
                            },
                        ]
                    }
                ),
                encoding="utf-8",
            )
            verifier = ReplayVerifier(
                manifest_path=manifest,
                game_executable=Path("fake-verifier.exe"),
                godot_executable=None,
                godot_project=None,
                timeout_seconds=1.0,
                temporary_directory=Path(directory),
            )
            with mock.patch("replay_verifier.subprocess.run", return_value=completed):
                self.assertEqual(set(verifier.manifest_boards()), {"test-board"})
                result = verifier.verify(replay_bytes, "test-board", 12345)
        self.assertEqual(
            result["replay_sha256"], "sha256:" + hashlib.sha256(replay_bytes).hexdigest()
        )

if __name__ == "__main__":
    unittest.main()
