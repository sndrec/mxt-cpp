PRAGMA foreign_keys = ON;

CREATE TABLE players (
    steam_id TEXT PRIMARY KEY,
    persona_name TEXT NOT NULL DEFAULT '',
    avatar_url TEXT NOT NULL DEFAULT '',
    profile_url TEXT NOT NULL DEFAULT '',
    persona_updated_unix INTEGER NOT NULL DEFAULT 0,
    moderation_state TEXT NOT NULL DEFAULT 'visible' CHECK (moderation_state IN ('visible', 'quarantined', 'hidden')),
    moderation_reason TEXT NOT NULL DEFAULT '',
    first_seen_unix INTEGER NOT NULL,
    last_seen_unix INTEGER NOT NULL
) STRICT;

CREATE TABLE boards (
    board_id TEXT PRIMARY KEY,
    track_content_id TEXT NOT NULL,
    track_gameplay_digest TEXT NOT NULL,
    track_title TEXT NOT NULL,
    ruleset_revision INTEGER NOT NULL,
    active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),
    first_seen_unix INTEGER NOT NULL,
    updated_unix INTEGER NOT NULL,
    UNIQUE (track_gameplay_digest, ruleset_revision),
    UNIQUE (board_id, track_gameplay_digest, ruleset_revision)
) STRICT;

CREATE TABLE replay_objects (
    replay_sha256 TEXT PRIMARY KEY,
    object_key TEXT NOT NULL UNIQUE,
    byte_length INTEGER NOT NULL CHECK (byte_length > 0),
    created_unix INTEGER NOT NULL
) STRICT;

CREATE TABLE verified_runs (
    run_id TEXT PRIMARY KEY,
    steam_id TEXT NOT NULL REFERENCES players(steam_id),
    board_id TEXT NOT NULL,
    track_content_id TEXT NOT NULL,
    track_gameplay_digest TEXT NOT NULL,
    vehicle_content_id TEXT NOT NULL,
    vehicle_gameplay_digest TEXT NOT NULL,
    machine_setting_percent INTEGER NOT NULL CHECK (machine_setting_percent BETWEEN 0 AND 100),
    score_milliseconds INTEGER NOT NULL CHECK (score_milliseconds > 0),
    ruleset_revision INTEGER NOT NULL CHECK (ruleset_revision > 0),
    replay_schema_version INTEGER NOT NULL CHECK (replay_schema_version > 0),
    game_version_major INTEGER NOT NULL,
    game_version_compatibility INTEGER NOT NULL,
    game_version_patch INTEGER NOT NULL,
    replay_sha256 TEXT NOT NULL REFERENCES replay_objects(replay_sha256),
    replay_byte_length INTEGER NOT NULL CHECK (replay_byte_length > 0),
    captured_persona_name TEXT NOT NULL DEFAULT '',
    received_unix INTEGER NOT NULL,
    source_timestamp_unix INTEGER NOT NULL DEFAULT 0,
    provenance TEXT NOT NULL CHECK (provenance IN ('native_submission', 'steam_import')),
    moderation_state TEXT NOT NULL DEFAULT 'visible' CHECK (moderation_state IN ('visible', 'quarantined', 'hidden')),
    moderation_reason TEXT NOT NULL DEFAULT '',
    UNIQUE (steam_id, replay_sha256),
    FOREIGN KEY (board_id, track_gameplay_digest, ruleset_revision)
        REFERENCES boards(board_id, track_gameplay_digest, ruleset_revision)
) STRICT;

CREATE TABLE player_vehicle_bests (
    track_gameplay_digest TEXT NOT NULL,
    ruleset_revision INTEGER NOT NULL,
    vehicle_gameplay_digest TEXT NOT NULL,
    steam_id TEXT NOT NULL REFERENCES players(steam_id),
    run_id TEXT NOT NULL REFERENCES verified_runs(run_id),
    score_milliseconds INTEGER NOT NULL,
    received_unix INTEGER NOT NULL,
    PRIMARY KEY (track_gameplay_digest, ruleset_revision, vehicle_gameplay_digest, steam_id)
) WITHOUT ROWID, STRICT;

CREATE INDEX verified_runs_board_score
ON verified_runs(board_id, score_milliseconds, received_unix, run_id)
WHERE moderation_state = 'visible';

CREATE INDEX verified_runs_player_board
ON verified_runs(steam_id, board_id, received_unix DESC);

CREATE INDEX player_vehicle_bests_board_score
ON player_vehicle_bests(track_gameplay_digest, ruleset_revision, score_milliseconds, received_unix, run_id);
