import { bytesToHex, deterministicRunId } from "./crypto";
import { overallRankForPlayer } from "./leaderboards";
import type { RunReplayRow, VerifiedRunEnvelope } from "./types";

export type ArchiveResult = Readonly<{
  run_id: string;
  run_created: boolean;
  vehicle_best_changed: boolean;
  is_vehicle_best: boolean;
  personal_best_milliseconds: number;
  global_rank: number;
}>;

function replayObjectKey(replaySha256: string): string {
  const hex = replaySha256.slice("sha256:".length);
  return `replays/sha256/${hex.slice(0, 2)}/${hex}.mxt_replay`;
}

export async function storeReplay(
  env: Env,
  envelope: VerifiedRunEnvelope,
  body: ReadableStream<Uint8Array>,
): Promise<string> {
  const objectKey = replayObjectKey(envelope.replay_sha256);
  const expectedHex = envelope.replay_sha256.slice("sha256:".length);
  let object = await env.REPLAYS.head(objectKey);
  if (object !== null) {
    const storedSha = object.checksums.sha256 === undefined ? "" : bytesToHex(object.checksums.sha256);
    if (object.size !== envelope.replay_byte_length || storedSha !== expectedHex) {
      throw new Error("replay_object_conflict");
    }
    return objectKey;
  }
  object = await env.REPLAYS.put(objectKey, body, {
    sha256: expectedHex,
    httpMetadata: {
      contentType: "application/vnd.mxt.replay",
      cacheControl: "private, max-age=0, no-store",
    },
    customMetadata: {
      replay_sha256: envelope.replay_sha256,
      schema_version: String(envelope.replay_schema_version),
    },
  });
  if (object === null) throw new Error("replay_object_write_failed");
  const storedSha = object.checksums.sha256 === undefined ? "" : bytesToHex(object.checksums.sha256);
  if (object.size !== envelope.replay_byte_length || storedSha !== expectedHex) {
    throw new Error("replay_object_integrity_failed");
  }
  return objectKey;
}

export async function archiveVerifiedRun(
  env: Env,
  envelope: VerifiedRunEnvelope,
  objectKey: string,
  receivedUnix: number,
): Promise<ArchiveResult> {
  const runId = await deterministicRunId(envelope.steam_id, envelope.replay_sha256);
  const statements = [
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
    `).bind(
      envelope.steam_id,
      envelope.persona_name,
      envelope.avatar_url,
      envelope.profile_url,
      receivedUnix,
    ),
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
      envelope.board_id,
      envelope.track_content_id,
      envelope.track_gameplay_digest,
      envelope.track_title,
      envelope.ruleset_revision,
      receivedUnix,
    ),
    env.DB.prepare(`
      INSERT OR IGNORE INTO replay_objects (replay_sha256, object_key, byte_length, created_unix)
      VALUES (?1, ?2, ?3, ?4)
    `).bind(envelope.replay_sha256, objectKey, envelope.replay_byte_length, receivedUnix),
    env.DB.prepare(`
      INSERT OR IGNORE INTO verified_runs (
        run_id, steam_id, board_id, track_content_id, track_gameplay_digest,
        vehicle_content_id, vehicle_gameplay_digest, machine_setting_percent,
        score_milliseconds, ruleset_revision, replay_schema_version,
        game_version_major, game_version_compatibility, game_version_patch,
        replay_sha256, replay_byte_length, captured_persona_name,
        received_unix, source_timestamp_unix, provenance
      ) VALUES (
        ?1, ?2, ?3, ?4, ?5,
        ?6, ?7, ?8,
        ?9, ?10, ?11,
        ?12, ?13, ?14,
        ?15, ?16, ?17,
        ?18, ?19, ?20
      )
    `).bind(
      runId,
      envelope.steam_id,
      envelope.board_id,
      envelope.track_content_id,
      envelope.track_gameplay_digest,
      envelope.vehicle_content_id,
      envelope.vehicle_gameplay_digest,
      envelope.machine_setting_percent,
      envelope.score_milliseconds,
      envelope.ruleset_revision,
      envelope.replay_schema_version,
      envelope.game_version.major,
      envelope.game_version.compatibility,
      envelope.game_version.patch,
      envelope.replay_sha256,
      envelope.replay_byte_length,
      envelope.persona_name,
      receivedUnix,
      envelope.source_timestamp_unix,
      envelope.provenance,
    ),
    env.DB.prepare(`
      INSERT INTO player_vehicle_bests (
        track_gameplay_digest, ruleset_revision, vehicle_gameplay_digest,
        steam_id, run_id, score_milliseconds, received_unix
      ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
      ON CONFLICT (track_gameplay_digest, ruleset_revision, vehicle_gameplay_digest, steam_id)
      DO UPDATE SET
        run_id = excluded.run_id,
        score_milliseconds = excluded.score_milliseconds,
        received_unix = excluded.received_unix
      WHERE excluded.score_milliseconds < player_vehicle_bests.score_milliseconds
    `).bind(
      envelope.track_gameplay_digest,
      envelope.ruleset_revision,
      envelope.vehicle_gameplay_digest,
      envelope.steam_id,
      runId,
      envelope.score_milliseconds,
      receivedUnix,
    ),
  ];
  const results = await env.DB.batch(statements);
  const runCreated = (results[3]?.meta.changes ?? 0) > 0;
  const vehicleBestChanged = (results[4]?.meta.changes ?? 0) > 0;
  const best = await env.DB.prepare(`
    SELECT run_id, score_milliseconds
    FROM player_vehicle_bests
    WHERE track_gameplay_digest = ?1
      AND ruleset_revision = ?2
      AND vehicle_gameplay_digest = ?3
      AND steam_id = ?4
  `).bind(
    envelope.track_gameplay_digest,
    envelope.ruleset_revision,
    envelope.vehicle_gameplay_digest,
    envelope.steam_id,
  ).first<{ run_id: string; score_milliseconds: number }>();
  if (best === null) throw new Error("best_row_missing_after_archive");
  return {
    run_id: runId,
    run_created: runCreated,
    vehicle_best_changed: vehicleBestChanged,
    is_vehicle_best: best.run_id === runId,
    personal_best_milliseconds: best.score_milliseconds,
    global_rank: await overallRankForPlayer(env, envelope.board_id, envelope.steam_id),
  };
}

export async function replayRow(env: Env, runId: string): Promise<RunReplayRow | null> {
  return env.DB.prepare(`
    SELECT
      r.run_id, r.replay_sha256, objects.object_key,
      r.replay_byte_length, r.moderation_state,
      players.moderation_state AS player_moderation_state
    FROM verified_runs r
    JOIN replay_objects objects ON objects.replay_sha256 = r.replay_sha256
    JOIN players ON players.steam_id = r.steam_id
    WHERE r.run_id = ?1
  `).bind(runId).first<RunReplayRow>();
}
