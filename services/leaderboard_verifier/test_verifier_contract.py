from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from replay_verifier import RESULT_PREFIX, ReplayVerifier
from steam_web_api import leaderboard_details


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
                            }
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
                result = verifier.verify(replay_bytes, "test-board", 12345)
        self.assertEqual(
            result["replay_sha256"], "sha256:" + hashlib.sha256(replay_bytes).hexdigest()
        )

    def test_score_details_fit_and_preserve_all_full_digests(self) -> None:
        verified = {
            "replay_sha256": "sha256:" + "11" * 32,
            "track_gameplay_digest": "sha256:" + "22" * 32,
            "vehicle_gameplay_digest": "sha256:" + "33" * 32,
            "ruleset_revision": 7,
            "replay_schema_version": 4,
            "game_version": {"major": 3, "compatibility": 2, "patch": 19},
        }
        packed = leaderboard_details(verified)
        self.assertEqual(len(packed), 116)
        self.assertLessEqual(len(packed), 256)
        words = struct.unpack("<29I", packed)
        self.assertEqual(words[:5], (0x3154584D, 2, 7, 4, 0x03020013))
        self.assertEqual(words[5:13], (0x11111111,) * 8)
        self.assertEqual(words[13:21], (0x22222222,) * 8)
        self.assertEqual(words[21:29], (0x33333333,) * 8)


if __name__ == "__main__":
    unittest.main()
