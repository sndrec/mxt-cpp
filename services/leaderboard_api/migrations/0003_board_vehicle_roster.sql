CREATE TABLE board_vehicle_roster (
    board_id TEXT NOT NULL REFERENCES boards(board_id),
    vehicle_content_id TEXT NOT NULL,
    vehicle_gameplay_digest TEXT NOT NULL,
    first_seen_unix INTEGER NOT NULL,
    PRIMARY KEY (board_id, vehicle_content_id),
    UNIQUE (board_id, vehicle_gameplay_digest)
) WITHOUT ROWID, STRICT;

-- Existing data is admitted only when one vehicle lineage has exactly one digest
-- inside the immutable board revision. Any pre-existing conflict stays excluded
-- until it is inspected instead of being resolved by an arbitrary winner.
INSERT INTO board_vehicle_roster (
    board_id, vehicle_content_id, vehicle_gameplay_digest, first_seen_unix
)
SELECT
    board_id,
    vehicle_content_id,
    MIN(vehicle_gameplay_digest),
    MIN(received_unix)
FROM verified_runs
GROUP BY board_id, vehicle_content_id
HAVING COUNT(DISTINCT vehicle_gameplay_digest) = 1;
