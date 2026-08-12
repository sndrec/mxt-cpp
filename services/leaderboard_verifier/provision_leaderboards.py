from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

from steam_web_api import SteamWebApi


def _leaderboard_pairs(response: dict[str, Any]) -> dict[str, int]:
    output: dict[str, int] = {}

    def visit(value: Any) -> None:
        if isinstance(value, dict):
            name = next((value[key] for key in value if key.lower() in {"name", "leaderboard_name"}), None)
            identifier = next(
                (value[key] for key in value if key.lower() in {"id", "leaderboard_id", "leaderboardid"}),
                None,
            )
            if isinstance(name, str):
                try:
                    numeric_id = int(identifier)
                except (TypeError, ValueError):
                    numeric_id = 0
                if numeric_id > 0:
                    output[name] = numeric_id
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(response)
    return output


def main() -> None:
    service_root = Path(__file__).resolve().parent
    repository_root = service_root.parents[1]
    parser = argparse.ArgumentParser(description="Create MaxX Throttle's trusted Steam leaderboards.")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=repository_root / "mxto" / "steam" / "leaderboards.json",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--steam-api-base", default="https://partner.steam-api.com")
    args = parser.parse_args()
    app_id = int(os.environ["MXT_STEAM_APP_ID"])
    publisher_key = os.environ["MXT_STEAM_PUBLISHER_KEY"]
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    boards = manifest.get("boards", [])
    if not isinstance(boards, list) or not boards:
        raise RuntimeError("leaderboard manifest has no boards")
    api = SteamWebApi(publisher_key, app_id, args.steam_api_base)
    expected_names: list[str] = []
    for board in boards:
        if not isinstance(board, dict) or not isinstance(board.get("steam_name"), str):
            raise RuntimeError("leaderboard manifest contains an invalid board")
        name = board["steam_name"]
        expected_names.append(name)
        print(f"Provisioning {name}", flush=True)
        api.find_or_create_leaderboard(name)
    existing = _leaderboard_pairs(api.get_leaderboards())
    missing = [name for name in expected_names if name not in existing]
    if missing:
        raise RuntimeError(f"Steam did not return numeric IDs for: {', '.join(missing)}")
    output = {
        "app_id": app_id,
        "boards": {name: existing[name] for name in expected_names},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(expected_names)} leaderboard IDs to {args.output}", flush=True)


if __name__ == "__main__":
    main()
