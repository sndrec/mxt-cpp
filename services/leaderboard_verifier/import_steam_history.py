from __future__ import annotations

import argparse
from pathlib import Path

from authoritative_store import AuthoritativeLeaderboardStore
from historical_import import HistoricalScoreStore, SteamHistoryImporter
from replay_verifier import ReplayVerifier


def _secret(path: Path) -> str:
    value = path.read_text(encoding="utf-8").strip()
    if len(value) < 32:
        raise ValueError(f"Secret is missing or too short: {path}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Import a complete Steam leaderboard snapshot.")
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--game-executable", type=Path)
    parser.add_argument("--godot-executable", type=Path)
    parser.add_argument("--godot-project", type=Path)
    parser.add_argument("--temporary-directory", type=Path)
    parser.add_argument("--api-url", required=True)
    parser.add_argument("--ingest-secret-file", type=Path, required=True)
    parser.add_argument("--migration-secret-file", type=Path, required=True)
    parser.add_argument("--verifier-timeout-seconds", type=float, default=180.0)
    args = parser.parse_args()
    verifier = ReplayVerifier(
        manifest_path=args.manifest.resolve(),
        game_executable=args.game_executable.resolve() if args.game_executable else None,
        godot_executable=args.godot_executable.resolve() if args.godot_executable else None,
        godot_project=args.godot_project.resolve() if args.godot_project else None,
        timeout_seconds=args.verifier_timeout_seconds,
        temporary_directory=args.temporary_directory.resolve() if args.temporary_directory else None,
    )
    importer = SteamHistoryImporter(
        snapshot_path=args.snapshot.resolve(),
        report_path=args.report.resolve(),
        replay_verifier=verifier,
        replay_store=AuthoritativeLeaderboardStore(args.api_url, _secret(args.ingest_secret_file)),
        score_store=HistoricalScoreStore(args.api_url, _secret(args.migration_secret_file)),
    )
    report = importer.run()
    counts = report["counts"]
    print(
        "Steam history import complete: "
        f"{counts['scanned']} scanned, "
        f"{counts['replay_backed_imported']} replay-backed, "
        f"{counts['score_only_imported']} score-only, "
        f"{counts['invalid']} invalid, {counts['failed']} failed."
    )
    print(f"Report: {args.report.resolve()}")
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
