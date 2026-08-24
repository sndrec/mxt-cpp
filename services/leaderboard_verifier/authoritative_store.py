from __future__ import annotations

import base64
import hashlib
import hmac
import json
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any


class AuthoritativeStoreError(RuntimeError):
    pass


@dataclass(frozen=True)
class AuthoritativeLeaderboardStore:
    base_url: str
    ingest_secret: str
    timeout_seconds: float = 30.0

    def archive_verified_run(self, replay_bytes: bytes, metadata: dict[str, Any]) -> dict[str, Any]:
        encoded_metadata = base64.urlsafe_b64encode(
            json.dumps(metadata, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
        ).rstrip(b"=").decode("ascii")
        timestamp = str(int(time.time()))
        signature = hmac.new(
            self.ingest_secret.encode("utf-8"),
            f"{timestamp}\n{encoded_metadata}".encode("ascii"),
            hashlib.sha256,
        ).hexdigest()
        request = urllib.request.Request(
            self.base_url.rstrip("/") + "/v1/ingest/verified-run",
            data=replay_bytes,
            method="POST",
            headers={
                "Accept": "application/json",
                "Content-Length": str(len(replay_bytes)),
                "Content-Type": "application/vnd.mxt.replay",
                "User-Agent": "MaxX-Throttle-Leaderboard-Verifier/2",
                "X-MXT-Ingest-Metadata": encoded_metadata,
                "X-MXT-Ingest-Signature": signature,
                "X-MXT-Ingest-Timestamp": timestamp,
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:
                payload = response.read(2 * 1024 * 1024)
        except urllib.error.HTTPError as exc:
            detail = exc.read(64 * 1024).decode("utf-8", errors="replace")
            raise AuthoritativeStoreError(
                f"leaderboard authority rejected the verified run with HTTP {exc.code}: {detail[:1024]}"
            ) from exc
        except (urllib.error.URLError, TimeoutError) as exc:
            raise AuthoritativeStoreError(f"leaderboard authority request failed: {exc}") from exc
        try:
            parsed = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise AuthoritativeStoreError("leaderboard authority returned invalid JSON") from exc
        if not isinstance(parsed, dict) or parsed.get("ok") is not True or parsed.get("archived") is not True:
            raise AuthoritativeStoreError("leaderboard authority did not confirm replay archival")
        if str(parsed.get("replay_sha256", "")) != str(metadata.get("replay_sha256", "")):
            raise AuthoritativeStoreError("leaderboard authority confirmed a different replay digest")
        return parsed
