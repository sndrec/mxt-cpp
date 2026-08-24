import type { ModerationRequest } from "./types";

export type ModerationResult = Readonly<{
  action_id: string;
  target_kind: ModerationRequest["target_kind"];
  target_id: string;
  moderation_state: ModerationRequest["state"];
}>;

type RunCategory = Readonly<{
  steam_id: string;
  track_gameplay_digest: string;
  ruleset_revision: number;
  vehicle_gameplay_digest: string;
}>;

export async function applyModeration(
  env: Env,
  request: ModerationRequest,
  now: number,
): Promise<ModerationResult> {
  const actionId = crypto.randomUUID();
  const audit = env.DB.prepare(`
    INSERT INTO moderation_actions (
      action_id, target_kind, target_id, new_state, reason, operator, created_unix
    ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
  `).bind(actionId, request.target_kind, request.target_id, request.state,
    request.reason, request.operator, now);

  if (request.target_kind === "run") {
    const run = await env.DB.prepare(`
      SELECT steam_id, track_gameplay_digest, ruleset_revision, vehicle_gameplay_digest
      FROM verified_runs WHERE run_id = ?1
    `).bind(request.target_id).first<RunCategory>();
    if (run === null) throw new Error("moderation_target_not_found");
    await env.DB.batch([
      env.DB.prepare(`
        UPDATE verified_runs SET moderation_state = ?2, moderation_reason = ?3
        WHERE run_id = ?1
      `).bind(request.target_id, request.state, request.reason),
      env.DB.prepare(`
        DELETE FROM player_vehicle_bests
        WHERE track_gameplay_digest = ?1 AND ruleset_revision = ?2
          AND vehicle_gameplay_digest = ?3 AND steam_id = ?4
      `).bind(run.track_gameplay_digest, run.ruleset_revision,
        run.vehicle_gameplay_digest, run.steam_id),
      env.DB.prepare(`
        INSERT INTO player_vehicle_bests (
          track_gameplay_digest, ruleset_revision, vehicle_gameplay_digest,
          steam_id, run_id, score_milliseconds, received_unix
        )
        SELECT
          track_gameplay_digest, ruleset_revision, vehicle_gameplay_digest,
          steam_id, run_id, score_milliseconds, received_unix
        FROM verified_runs
        WHERE steam_id = ?1 AND track_gameplay_digest = ?2 AND ruleset_revision = ?3
          AND vehicle_gameplay_digest = ?4 AND moderation_state = 'visible'
        ORDER BY score_milliseconds, received_unix, run_id
        LIMIT 1
      `).bind(run.steam_id, run.track_gameplay_digest,
        run.ruleset_revision, run.vehicle_gameplay_digest),
      audit,
    ]);
  } else if (request.target_kind === "historical_score") {
    const target = await env.DB.prepare(
      "SELECT historical_id FROM historical_scores WHERE historical_id = ?1",
    ).bind(request.target_id).first();
    if (target === null) throw new Error("moderation_target_not_found");
    await env.DB.batch([
      env.DB.prepare(`
        UPDATE historical_scores SET moderation_state = ?2, moderation_reason = ?3
        WHERE historical_id = ?1
      `).bind(request.target_id, request.state, request.reason),
      audit,
    ]);
  } else {
    const target = await env.DB.prepare(
      "SELECT steam_id FROM players WHERE steam_id = ?1",
    ).bind(request.target_id).first();
    if (target === null) throw new Error("moderation_target_not_found");
    await env.DB.batch([
      env.DB.prepare(`
        UPDATE players SET moderation_state = ?2, moderation_reason = ?3
        WHERE steam_id = ?1
      `).bind(request.target_id, request.state, request.reason),
      audit,
    ]);
  }
  return {
    action_id: actionId,
    target_kind: request.target_kind,
    target_id: request.target_id,
    moderation_state: request.state,
  };
}
