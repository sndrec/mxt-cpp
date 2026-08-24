import type { LeaderboardCursor, LeaderboardEntryRow } from "./types";

const ENTRY_COLUMNS = `
  run_id, steam_id, persona_name, avatar_url, profile_url,
  score_milliseconds, received_unix,
  track_content_id, track_gameplay_digest,
  vehicle_content_id, vehicle_gameplay_digest, machine_setting_percent,
  ruleset_revision, replay_schema_version,
  game_version_major, game_version_compatibility, game_version_patch,
  replay_sha256, replay_byte_length, provenance`;

const JOINED_ENTRY_COLUMNS = `
  r.run_id, r.steam_id, p.persona_name, p.avatar_url, p.profile_url,
  r.score_milliseconds, r.received_unix,
  r.track_content_id, r.track_gameplay_digest,
  r.vehicle_content_id, r.vehicle_gameplay_digest, r.machine_setting_percent,
  r.ruleset_revision, r.replay_schema_version,
  r.game_version_major, r.game_version_compatibility, r.game_version_patch,
  r.replay_sha256, r.replay_byte_length, r.provenance`;

const OVERALL_BOARD_CTE = `
WITH candidates AS (
  SELECT
    r.run_id, r.steam_id, p.persona_name, p.avatar_url, p.profile_url,
    r.score_milliseconds, r.received_unix,
    r.track_content_id, r.track_gameplay_digest,
    r.vehicle_content_id, r.vehicle_gameplay_digest, r.machine_setting_percent,
    r.ruleset_revision, r.replay_schema_version,
    r.game_version_major, r.game_version_compatibility, r.game_version_patch,
    r.replay_sha256, r.replay_byte_length, r.provenance,
    ROW_NUMBER() OVER (
      PARTITION BY r.steam_id
      ORDER BY r.score_milliseconds, r.received_unix, r.run_id
    ) AS player_choice
  FROM player_vehicle_bests best
  JOIN verified_runs r ON r.run_id = best.run_id
  JOIN players p ON p.steam_id = r.steam_id
  WHERE r.board_id = ?1
    AND r.moderation_state = 'visible'
    AND p.moderation_state = 'visible'
), board AS (
  SELECT ${ENTRY_COLUMNS},
    ROW_NUMBER() OVER (ORDER BY score_milliseconds, received_unix, run_id) AS global_rank
  FROM candidates
  WHERE player_choice = 1
)`;

const VEHICLE_BOARD_CTE = `
WITH board AS (
  SELECT ${JOINED_ENTRY_COLUMNS},
    ROW_NUMBER() OVER (ORDER BY r.score_milliseconds, r.received_unix, r.run_id) AS global_rank
  FROM player_vehicle_bests best
  JOIN verified_runs r ON r.run_id = best.run_id
  JOIN players p ON p.steam_id = r.steam_id
  WHERE r.board_id = ?1
    AND r.vehicle_gameplay_digest = ?2
    AND r.moderation_state = 'visible'
    AND p.moderation_state = 'visible'
)`;

function cursorWhere(parameterOffset: number): string {
  return `
WHERE (?${parameterOffset} IS NULL)
   OR score_milliseconds > ?${parameterOffset}
   OR (score_milliseconds = ?${parameterOffset} AND received_unix > ?${parameterOffset + 1})
   OR (score_milliseconds = ?${parameterOffset} AND received_unix = ?${parameterOffset + 1} AND run_id > ?${parameterOffset + 2})`;
}

export async function readLeaderboard(
  env: Env,
  boardId: string,
  vehicleDigest: string,
  cursor: LeaderboardCursor | null,
  fetchCount: number,
): Promise<LeaderboardEntryRow[]> {
  const score = cursor?.score_milliseconds ?? null;
  const received = cursor?.received_unix ?? null;
  const runId = cursor?.run_id ?? null;
  if (vehicleDigest === "") {
    const query = `${OVERALL_BOARD_CTE}
SELECT global_rank, ${ENTRY_COLUMNS} FROM board
${cursorWhere(2)}
ORDER BY score_milliseconds, received_unix, run_id
LIMIT ?5`;
    const result = await env.DB.prepare(query).bind(boardId, score, received, runId, fetchCount).all<LeaderboardEntryRow>();
    return result.results;
  }
  const query = `${VEHICLE_BOARD_CTE}
SELECT global_rank, ${ENTRY_COLUMNS} FROM board
${cursorWhere(3)}
ORDER BY score_milliseconds, received_unix, run_id
LIMIT ?6`;
  const result = await env.DB.prepare(query).bind(boardId, vehicleDigest, score, received, runId, fetchCount).all<LeaderboardEntryRow>();
  return result.results;
}

export async function readAroundPlayer(
  env: Env,
  boardId: string,
  vehicleDigest: string,
  steamId: string,
  radius: number,
): Promise<LeaderboardEntryRow[]> {
  if (vehicleDigest === "") {
    const query = `${OVERALL_BOARD_CTE}, target AS (
  SELECT global_rank FROM board WHERE steam_id = ?2
)
SELECT board.global_rank, ${ENTRY_COLUMNS}
FROM board, target
WHERE board.global_rank BETWEEN MAX(1, target.global_rank - ?3) AND target.global_rank + ?3
ORDER BY board.global_rank`;
    const result = await env.DB.prepare(query).bind(boardId, steamId, radius).all<LeaderboardEntryRow>();
    return result.results;
  }
  const query = `${VEHICLE_BOARD_CTE}, target AS (
  SELECT global_rank FROM board WHERE steam_id = ?3
)
SELECT board.global_rank, ${ENTRY_COLUMNS}
FROM board, target
WHERE board.global_rank BETWEEN MAX(1, target.global_rank - ?4) AND target.global_rank + ?4
ORDER BY board.global_rank`;
  const result = await env.DB.prepare(query).bind(boardId, vehicleDigest, steamId, radius).all<LeaderboardEntryRow>();
  return result.results;
}

export async function overallRankForPlayer(env: Env, boardId: string, steamId: string): Promise<number> {
  const query = `${OVERALL_BOARD_CTE}
SELECT global_rank FROM board WHERE steam_id = ?2`;
  const row = await env.DB.prepare(query).bind(boardId, steamId).first<{ global_rank: number }>();
  return row?.global_rank ?? 0;
}

export function publicEntry(row: LeaderboardEntryRow): Record<string, string | number | boolean | object> {
  return {
    rank: row.global_rank,
    run_id: row.run_id,
    steam_id: row.steam_id,
    persona_name: row.persona_name,
    avatar_url: row.avatar_url,
    profile_url: row.profile_url,
    score_milliseconds: row.score_milliseconds,
    received_unix: row.received_unix,
    track_content_id: row.track_content_id,
    track_gameplay_digest: row.track_gameplay_digest,
    vehicle_content_id: row.vehicle_content_id,
    vehicle_gameplay_digest: row.vehicle_gameplay_digest,
    machine_setting_percent: row.machine_setting_percent,
    ruleset_revision: row.ruleset_revision,
    replay_schema_version: row.replay_schema_version,
    game_version: {
      major: row.game_version_major,
      compatibility: row.game_version_compatibility,
      patch: row.game_version_patch,
    },
    replay_sha256: row.replay_sha256,
    replay_byte_length: row.replay_byte_length,
    replay_available: true,
    provenance: row.provenance,
  };
}
