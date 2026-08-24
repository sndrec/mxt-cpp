import type { HistoricalScoreImport } from "./types";

async function historicalId(boardId: string, steamId: string): Promise<string> {
  const bytes = new Uint8Array(await crypto.subtle.digest(
    "SHA-256",
    new TextEncoder().encode(`steam-history\n${boardId}\n${steamId}`),
  ));
  let output = "";
  for (const byte of bytes) output += byte.toString(16).padStart(2, "0");
  return output;
}

export async function importHistoricalScore(
  env: Env,
  record: HistoricalScoreImport,
  receivedUnix: number,
): Promise<{ historical_id: string; changed: boolean; score_milliseconds: number }> {
  const id = await historicalId(record.board_id, record.steam_id);
  const results = await env.DB.batch([
    env.DB.prepare(`
      INSERT INTO players (
        steam_id, persona_name, avatar_url, profile_url, persona_updated_unix,
        first_seen_unix, last_seen_unix
      ) VALUES (?1, ?2, ?3, ?4, ?5, ?5, ?5)
      ON CONFLICT (steam_id) DO UPDATE SET
        persona_name = CASE WHEN excluded.persona_name <> '' THEN excluded.persona_name ELSE players.persona_name END,
        avatar_url = CASE WHEN excluded.avatar_url <> '' THEN excluded.avatar_url ELSE players.avatar_url END,
        profile_url = CASE WHEN excluded.profile_url <> '' THEN excluded.profile_url ELSE players.profile_url END,
        persona_updated_unix = CASE WHEN excluded.persona_name <> '' THEN excluded.persona_updated_unix ELSE players.persona_updated_unix END,
        last_seen_unix = MAX(players.last_seen_unix, excluded.last_seen_unix)
    `).bind(record.steam_id, record.persona_name, record.avatar_url, record.profile_url, receivedUnix),
    env.DB.prepare(`
      INSERT INTO boards (
        board_id, track_content_id, track_gameplay_digest, track_title,
        ruleset_revision, first_seen_unix, updated_unix
      ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6)
      ON CONFLICT (board_id) DO UPDATE SET
        track_title = excluded.track_title,
        updated_unix = MAX(boards.updated_unix, excluded.updated_unix)
      WHERE boards.track_content_id = excluded.track_content_id
        AND boards.track_gameplay_digest = excluded.track_gameplay_digest
        AND boards.ruleset_revision = excluded.ruleset_revision
    `).bind(
      record.board_id,
      record.track_content_id,
      record.track_gameplay_digest,
      record.track_title,
      record.ruleset_revision,
      receivedUnix,
    ),
    env.DB.prepare(`
      INSERT INTO historical_scores (
        historical_id, steam_id, board_id, track_content_id, track_gameplay_digest,
        score_milliseconds, ruleset_revision, captured_persona_name,
        received_unix, source_timestamp_unix, source_global_rank,
        source_ugc_handle, source_details_json, unavailable_reason, provenance
      ) VALUES (
        ?1, ?2, ?3, ?4, ?5,
        ?6, ?7, ?8,
        ?9, ?10, ?11,
        ?12, ?13, ?14, 'steam_import_score_only'
      )
      ON CONFLICT (board_id, steam_id) DO UPDATE SET
        score_milliseconds = excluded.score_milliseconds,
        captured_persona_name = excluded.captured_persona_name,
        received_unix = excluded.received_unix,
        source_timestamp_unix = excluded.source_timestamp_unix,
        source_global_rank = excluded.source_global_rank,
        source_ugc_handle = excluded.source_ugc_handle,
        source_details_json = excluded.source_details_json,
        unavailable_reason = excluded.unavailable_reason
      WHERE excluded.score_milliseconds <= historical_scores.score_milliseconds
    `).bind(
      id,
      record.steam_id,
      record.board_id,
      record.track_content_id,
      record.track_gameplay_digest,
      record.score_milliseconds,
      record.ruleset_revision,
      record.persona_name,
      receivedUnix,
      record.source_timestamp_unix,
      record.source_global_rank,
      record.source_ugc_handle,
      JSON.stringify(record.source_details),
      record.unavailable_reason,
    ),
  ]);
  const row = await env.DB.prepare(`
    SELECT score_milliseconds FROM historical_scores WHERE historical_id = ?1
  `).bind(id).first<{ score_milliseconds: number }>();
  if (row === null) throw new Error("historical_score_missing_after_import");
  return {
    historical_id: id,
    changed: (results[2]?.meta.changes ?? 0) > 0,
    score_milliseconds: row.score_milliseconds,
  };
}
