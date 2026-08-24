import type { LeaderboardEntryRow } from "./types";

export type CategoryRow = Readonly<{
  vehicle_content_id: string;
  vehicle_gameplay_digest: string;
  entry_count: number;
  record_milliseconds: number;
  record_run_id: string;
  record_steam_id: string;
  record_persona_name: string;
}>;

export async function readCategories(env: Env, boardId: string): Promise<CategoryRow[]> {
  const result = await env.DB.prepare(`
    WITH ranked AS (
      SELECT
        r.vehicle_content_id,
        r.vehicle_gameplay_digest,
        r.score_milliseconds,
        r.run_id,
        r.steam_id,
        p.persona_name,
        COUNT(*) OVER (
          PARTITION BY r.vehicle_content_id, r.vehicle_gameplay_digest
        ) AS entry_count,
        ROW_NUMBER() OVER (
          PARTITION BY r.vehicle_content_id, r.vehicle_gameplay_digest
          ORDER BY r.score_milliseconds, r.received_unix, r.run_id
        ) AS category_rank
      FROM player_vehicle_bests best
      JOIN verified_runs r ON r.run_id = best.run_id
      JOIN board_vehicle_roster roster
        ON roster.board_id = r.board_id
        AND roster.vehicle_content_id = r.vehicle_content_id
        AND roster.vehicle_gameplay_digest = r.vehicle_gameplay_digest
      JOIN players p ON p.steam_id = r.steam_id
      WHERE r.board_id = ?1
        AND r.moderation_state = 'visible'
        AND p.moderation_state = 'visible'
    )
    SELECT
      r.vehicle_content_id,
      r.vehicle_gameplay_digest,
      r.entry_count,
      r.score_milliseconds AS record_milliseconds,
      r.run_id AS record_run_id,
      r.steam_id AS record_steam_id,
      r.persona_name AS record_persona_name
    FROM ranked r
    WHERE r.category_rank = 1
    ORDER BY r.vehicle_content_id, r.vehicle_gameplay_digest
  `).bind(boardId).all<CategoryRow>();
  return result.results;
}
export async function readPlayerBests(
  env: Env,
  boardId: string,
  steamId: string,
): Promise<LeaderboardEntryRow[]> {
  const result = await env.DB.prepare(`
    SELECT
      0 AS global_rank,
      r.run_id, r.steam_id, p.persona_name, p.avatar_url, p.profile_url,
      r.score_milliseconds, r.received_unix,
      r.track_content_id, r.track_gameplay_digest,
      r.vehicle_content_id, r.vehicle_gameplay_digest, r.machine_setting_percent,
      r.ruleset_revision, r.replay_schema_version,
      r.game_version_major, r.game_version_compatibility, r.game_version_patch,
      r.replay_sha256, r.replay_byte_length, r.provenance,
      '' AS historical_unavailable_reason
    FROM player_vehicle_bests best
    JOIN verified_runs r ON r.run_id = best.run_id
    JOIN board_vehicle_roster roster
      ON roster.board_id = r.board_id
      AND roster.vehicle_content_id = r.vehicle_content_id
      AND roster.vehicle_gameplay_digest = r.vehicle_gameplay_digest
    JOIN players p ON p.steam_id = r.steam_id
    WHERE r.board_id = ?1
      AND r.steam_id = ?2
      AND r.moderation_state = 'visible'
      AND p.moderation_state = 'visible'
    ORDER BY r.vehicle_content_id, r.vehicle_gameplay_digest
  `).bind(boardId, steamId).all<LeaderboardEntryRow>();
  return result.results;
}
