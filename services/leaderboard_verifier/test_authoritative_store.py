from __future__ import annotations

import base64
import hashlib
import hmac
import json
import unittest
from unittest import mock

from authoritative_store import AuthoritativeLeaderboardStore, AuthoritativeStoreError


class _Response:
    def __init__(self, payload: dict[str, object]) -> None:
        self._payload = json.dumps(payload).encode("utf-8")

    def __enter__(self) -> _Response:
        return self

    def __exit__(self, *_args: object) -> None:
        return None

    def read(self, _maximum: int) -> bytes:
        return self._payload


class AuthoritativeStoreTests(unittest.TestCase):
    def test_archive_signs_exact_metadata_and_replay_bytes(self) -> None:
        replay = b"verified replay bytes"
        digest = "sha256:" + hashlib.sha256(replay).hexdigest()
        metadata = {"schema_version": 1, "replay_sha256": digest}
        response = _Response({"ok": True, "archived": True, "replay_sha256": digest, "run_id": "11" * 32})
        with mock.patch("authoritative_store.time.time", return_value=1_800_000_000), mock.patch(
            "authoritative_store.urllib.request.urlopen", return_value=response
        ) as open_request:
            result = AuthoritativeLeaderboardStore(
                "https://leaderboards.example.test/", "shared-ingest-secret"
            ).archive_verified_run(replay, metadata)

        request = open_request.call_args.args[0]
        encoded = request.headers["X-mxt-ingest-metadata"]
        expected_signature = hmac.new(
            b"shared-ingest-secret", f"1800000000\n{encoded}".encode("ascii"), hashlib.sha256
        ).hexdigest()
        self.assertEqual(request.full_url, "https://leaderboards.example.test/v1/ingest/verified-run")
        self.assertEqual(request.data, replay)
        self.assertEqual(request.headers["Content-type"], "application/vnd.mxt.replay")
        self.assertEqual(request.headers["X-mxt-ingest-signature"], expected_signature)
        decoded = base64.urlsafe_b64decode(encoded + "=" * (-len(encoded) % 4))
        self.assertEqual(json.loads(decoded), metadata)
        self.assertEqual(result["run_id"], "11" * 32)

    def test_archive_rejects_mismatched_confirmation_digest(self) -> None:
        replay = b"verified replay bytes"
        expected = "sha256:" + hashlib.sha256(replay).hexdigest()
        response = _Response({"ok": True, "archived": True, "replay_sha256": "sha256:" + "00" * 32})
        with mock.patch("authoritative_store.urllib.request.urlopen", return_value=response):
            with self.assertRaisesRegex(AuthoritativeStoreError, "different replay digest"):
                AuthoritativeLeaderboardStore(
                    "https://leaderboards.example.test", "shared-ingest-secret"
                ).archive_verified_run(replay, {"replay_sha256": expected})


if __name__ == "__main__":
    unittest.main()
