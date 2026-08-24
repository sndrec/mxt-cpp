CREATE TABLE moderation_actions (
    action_id TEXT PRIMARY KEY,
    target_kind TEXT NOT NULL CHECK (target_kind IN ('run', 'historical_score', 'player')),
    target_id TEXT NOT NULL,
    new_state TEXT NOT NULL CHECK (new_state IN ('visible', 'quarantined', 'hidden')),
    reason TEXT NOT NULL,
    operator TEXT NOT NULL,
    created_unix INTEGER NOT NULL
) STRICT;

CREATE INDEX moderation_actions_target
ON moderation_actions(target_kind, target_id, created_unix DESC);
