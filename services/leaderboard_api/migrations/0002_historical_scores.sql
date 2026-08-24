CREATE TABLE historical_scores (
    historical_id TEXT PRIMARY KEY,
    steam_id TEXT NOT NULL REFERENCES players(steam_id),
    board_id TEXT NOT NULL,
    track_content_id TEXT NOT NULL,
    track_gameplay_digest TEXT NOT NULL,
    score_milliseconds INTEGER NOT NULL CHECK (score_milliseconds > 0),
    ruleset_revision INTEGER NOT NULL CHECK (ruleset_revision > 0),
    captured_persona_name TEXT NOT NULL DEFAULT '',
    received_unix INTEGER NOT NULL,
    source_timestamp_unix INTEGER NOT NULL DEFAULT 0,
    source_global_rank INTEGER NOT NULL DEFAULT 0,
    source_ugc_handle TEXT NOT NULL DEFAULT '',
    source_details_json TEXT NOT NULL DEFAULT '[]',
    unavailable_reason TEXT NOT NULL,
    provenance TEXT NOT NULL CHECK (provenance = 'steam_import_score_only'),
    moderation_state TEXT NOT NULL DEFAULT 'visible' CHECK (moderation_state IN ('visible', 'quarantined', 'hidden')),
    moderation_reason TEXT NOT NULL DEFAULT '',
    UNIQUE (board_id, steam_id),
    FOREIGN KEY (board_id, track_gameplay_digest, ruleset_revision)
        REFERENCES boards(board_id, track_gameplay_digest, ruleset_revision)
) STRICT;

CREATE INDEX historical_scores_board_score
ON historical_scores(board_id, score_milliseconds, received_unix, historical_id)
WHERE moderation_state = 'visible';
