from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

from historical_import import SteamHistoryImporter


class FakeVerifier:
    def __init__(self) -> None:
        self.calls: list[tuple[bytes, str, int]] = []

    def verify(self, replay: bytes, board_id: str, score: int) -> dict[str, Any]:
        self.calls.append((replay, board_id, score))
        return {
            "track_content_id": "mxt:track:official:test",
            "track_gameplay_digest": "sha256:" + "11" * 32,
            "vehicle_content_id": "mxt:vehicle:official:allrounder",
            "vehicle_gameplay_digest": "sha256:" + "22" * 32,
            "machine_setting_percent": 50,
            "ruleset_revision": 2,
            "replay_schema_version": 4,
            "game_version": {"major": 0, "compatibility": 3, "patch": 1},
        }


class FakeReplayStore:
    def __init__(self) -> None:
        self.calls: list[tuple[bytes, dict[str, Any]]] = []

    def archive_verified_run(self, replay: bytes, metadata: dict[str, Any]) -> dict[str, Any]:
        self.calls.append((replay, metadata))
        return {"ok": True, "archived": True, "run_id": "a" * 64}


class FakeScoreStore:
    def __init__(self) -> None:
        self.calls: list[dict[str, Any]] = []

    def import_score(self, record: dict[str, Any]) -> dict[str, Any]:
        self.calls.append(record)
        return {"ok": True, "historical_id": "b" * 64}


class SteamHistoryImporterTest(unittest.TestCase):
    def test_reverifies_attachments_and_explicitly_imports_unavailable_scores(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            replay = b"legacy json replay bytes"
            digest = "sha256:" + hashlib.sha256(replay).hexdigest()
            replay_relative = f"replays/{digest.removeprefix('sha256:')}.replay"
            replay_path = root / replay_relative
            replay_path.parent.mkdir()
            replay_path.write_bytes(replay)
            entries = [
                {
                    "board_name": "mxt_ta_test_11111111_r2",
                    "track_title": "Test Track",
                    "global_rank": 1,
                    "steam_id": "76561198000000001",
                    "persona_name": "Replay Pilot",
                    "score_milliseconds": 50_000,
                    "replay_status": "downloaded",
                    "replay_path": replay_relative,
                    "replay_sha256": digest,
                    "replay_byte_length": len(replay),
                },
                {
                    "board_name": "mxt_ta_test_11111111_r2",
                    "track_content_id": "mxt:track:official:test",
                    "track_gameplay_digest": "sha256:" + "11" * 32,
                    "track_title": "Test Track",
                    "ruleset_revision": 2,
                    "global_rank": 2,
                    "steam_id": "76561198000000002",
                    "persona_name": "Score Pilot",
                    "score_milliseconds": 55_000,
                    "ugc_handle": "",
                    "source_details": [],
                    "replay_status": "unavailable",
                    "replay_unavailable_reason": "missing_replay_attachment",
                    "replay_sha256": "",
                },
            ]
            snapshot_path = root / "snapshot.json"
            snapshot_path.write_text(json.dumps({
                "format_revision": 1,
                "complete": True,
                "snapshot_unix": 123456,
                "steam_app_id": 5001340,
                "entries": entries,
            }), encoding="utf-8")
            verifier = FakeVerifier()
            replay_store = FakeReplayStore()
            score_store = FakeScoreStore()
            importer = SteamHistoryImporter(
                snapshot_path=snapshot_path,
                report_path=root / "report.json",
                replay_verifier=verifier,  # type: ignore[arg-type]
                replay_store=replay_store,  # type: ignore[arg-type]
                score_store=score_store,
                log=lambda _message: None,
            )
            report = importer.run()
            self.assertTrue(report["complete"])
            self.assertEqual(report["counts"]["replay_backed_imported"], 1)
            self.assertEqual(report["counts"]["score_only_imported"], 1)
            self.assertEqual(verifier.calls, [(replay, entries[0]["board_name"], 50_000)])
            self.assertEqual(replay_store.calls[0][1]["provenance"], "steam_import")
            self.assertEqual(replay_store.calls[0][1]["steam_id"], entries[0]["steam_id"])
            self.assertEqual(score_store.calls[0]["provenance"], "steam_import_score_only")
            self.assertEqual(score_store.calls[0]["unavailable_reason"], "missing_replay_attachment")


if __name__ == "__main__":
    unittest.main()
