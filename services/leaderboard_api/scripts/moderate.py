from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import time
import urllib.error
import urllib.request
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Soft-moderate a leaderboard run, historical score, or player.")
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--secret-file", type=Path, required=True)
    parser.add_argument("--target-kind", choices=("run", "historical_score", "player"), required=True)
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--state", choices=("visible", "quarantined", "hidden"), required=True)
    parser.add_argument("--reason", required=True)
    parser.add_argument("--operator", required=True)
    args = parser.parse_args()

    secret = args.secret_file.read_text(encoding="ascii").strip()
    if len(secret) < 64:
        raise SystemExit("Admin secret file is missing or invalid.")
    record = {
        "schema_version": 1,
        "target_kind": args.target_kind,
        "target_id": args.target_id,
        "state": args.state,
        "reason": args.reason,
        "operator": args.operator,
    }
    body = json.dumps(record, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    timestamp = str(int(time.time()))
    signature = hmac.new(
        secret.encode("ascii"), timestamp.encode("ascii") + b"\n" + body, hashlib.sha256
    ).hexdigest()
    request = urllib.request.Request(
        args.base_url.rstrip("/") + "/v1/admin/moderation",
        data=body,
        method="POST",
        headers={
            "Content-Length": str(len(body)),
            "Content-Type": "application/json",
            "X-MXT-Admin-Signature": signature,
            "X-MXT-Admin-Timestamp": timestamp,
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30.0) as response:
            result = json.load(response)
    except urllib.error.HTTPError as error:
        detail = error.read(64 * 1024).decode("utf-8", errors="replace")
        raise SystemExit(f"Moderation failed with HTTP {error.code}: {detail}") from error
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
