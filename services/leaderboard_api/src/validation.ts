import {
  API_VERSION,
  BOARD_ID_PATTERN,
  DIGEST_PATTERN,
  RUN_ID_PATTERN,
  STEAM_ID_PATTERN,
  type LeaderboardCursor,
  type HistoricalScoreImport,
  type VerifiedRunEnvelope,
} from "./types";

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function requiredString(record: Record<string, unknown>, key: string, maximumLength: number): string {
  const value = record[key];
  if (typeof value !== "string" || value.length === 0 || value.length > maximumLength) {
    throw new Error(`invalid_${key}`);
  }
  return value;
}

function optionalString(record: Record<string, unknown>, key: string, maximumLength: number): string {
  const value = record[key];
  if (value === undefined || value === null || value === "") {
    return "";
  }
  if (typeof value !== "string" || value.length > maximumLength) {
    throw new Error(`invalid_${key}`);
  }
  return value;
}

function boundedInteger(
  record: Record<string, unknown>,
  key: string,
  minimum: number,
  maximum: number,
): number {
  const value = record[key];
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < minimum || value > maximum) {
    throw new Error(`invalid_${key}`);
  }
  return value;
}

function validWebUrl(value: string, key: string): string {
  if (value === "") {
    return value;
  }
  let parsed: URL;
  try {
    parsed = new URL(value);
  } catch {
    throw new Error(`invalid_${key}`);
  }
  if (parsed.protocol !== "https:") {
    throw new Error(`invalid_${key}`);
  }
  return value;
}

export function parseVerifiedEnvelope(encoded: string): VerifiedRunEnvelope {
  if (encoded.length === 0 || encoded.length > 16_384 || !/^[A-Za-z0-9_-]+$/.test(encoded)) {
    throw new Error("invalid_metadata_encoding");
  }
  let parsed: unknown;
  try {
    const base64 = encoded.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(encoded.length / 4) * 4, "=");
    parsed = JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(base64), (character) => character.charCodeAt(0)))) as unknown;
  } catch {
    throw new Error("invalid_metadata_encoding");
  }
  if (!isRecord(parsed)) {
    throw new Error("invalid_metadata");
  }
  const gameVersionValue = parsed.game_version;
  if (!isRecord(gameVersionValue)) {
    throw new Error("invalid_game_version");
  }
  boundedInteger(parsed, "schema_version", API_VERSION, API_VERSION);
  if (requiredString(parsed, "run_kind", 32) !== "ranked_time_attack") {
    throw new Error("ineligible_run_kind");
  }
  if (requiredString(parsed, "track_source", 32) !== "official") {
    throw new Error("ineligible_track_source");
  }
  if (requiredString(parsed, "vehicle_source", 32) !== "official") {
    throw new Error("ineligible_vehicle_source");
  }
  const steamId = requiredString(parsed, "steam_id", 20);
  const boardId = requiredString(parsed, "board_id", 128);
  const trackDigest = requiredString(parsed, "track_gameplay_digest", 71);
  const vehicleDigest = requiredString(parsed, "vehicle_gameplay_digest", 71);
  const replayDigest = requiredString(parsed, "replay_sha256", 71);
  if (!STEAM_ID_PATTERN.test(steamId)) throw new Error("invalid_steam_id");
  if (!BOARD_ID_PATTERN.test(boardId)) throw new Error("invalid_board_id");
  if (!DIGEST_PATTERN.test(trackDigest)) throw new Error("invalid_track_gameplay_digest");
  if (!DIGEST_PATTERN.test(vehicleDigest)) throw new Error("invalid_vehicle_gameplay_digest");
  if (!DIGEST_PATTERN.test(replayDigest)) throw new Error("invalid_replay_sha256");
  const provenance = requiredString(parsed, "provenance", 32);
  if (provenance !== "native_submission" && provenance !== "steam_import") {
    throw new Error("invalid_provenance");
  }
  return {
    schema_version: API_VERSION,
    run_kind: "ranked_time_attack",
    track_source: "official",
    vehicle_source: "official",
    steam_id: steamId,
    auth_app_id: boundedInteger(parsed, "auth_app_id", 1, 0x7fffffff),
    board_id: boardId,
    track_content_id: requiredString(parsed, "track_content_id", 256),
    track_gameplay_digest: trackDigest,
    track_title: requiredString(parsed, "track_title", 256),
    vehicle_content_id: requiredString(parsed, "vehicle_content_id", 256),
    vehicle_gameplay_digest: vehicleDigest,
    machine_setting_percent: boundedInteger(parsed, "machine_setting_percent", 0, 100),
    score_milliseconds: boundedInteger(parsed, "score_milliseconds", 1, 0x7fffffff),
    ruleset_revision: boundedInteger(parsed, "ruleset_revision", 1, 0x7fffffff),
    replay_schema_version: boundedInteger(parsed, "replay_schema_version", 1, 0x7fffffff),
    game_version: {
      major: boundedInteger(gameVersionValue, "major", 0, 0xffff),
      compatibility: boundedInteger(gameVersionValue, "compatibility", 0, 0xffff),
      patch: boundedInteger(gameVersionValue, "patch", 0, 0x7fffffff),
    },
    replay_sha256: replayDigest,
    replay_byte_length: boundedInteger(parsed, "replay_byte_length", 1, 0x7fffffff),
    persona_name: optionalString(parsed, "persona_name", 128),
    avatar_url: validWebUrl(optionalString(parsed, "avatar_url", 512), "avatar_url"),
    profile_url: validWebUrl(optionalString(parsed, "profile_url", 512), "profile_url"),
    source_timestamp_unix: boundedInteger(parsed, "source_timestamp_unix", 0, 0x7fffffff),
    provenance,
  };
}

export function parseHistoricalScoreImport(parsed: unknown): HistoricalScoreImport {
  if (!isRecord(parsed)) throw new Error("invalid_metadata");
  boundedInteger(parsed, "schema_version", API_VERSION, API_VERSION);
  const steamId = requiredString(parsed, "steam_id", 20);
  const boardId = requiredString(parsed, "board_id", 128);
  const trackDigest = requiredString(parsed, "track_gameplay_digest", 71);
  if (!STEAM_ID_PATTERN.test(steamId)) throw new Error("invalid_steam_id");
  if (!BOARD_ID_PATTERN.test(boardId)) throw new Error("invalid_board_id");
  if (!DIGEST_PATTERN.test(trackDigest)) throw new Error("invalid_track_gameplay_digest");
  const details = parsed.source_details;
  if (!Array.isArray(details) || details.length > 64 || details.some((value) =>
    typeof value !== "number" || !Number.isSafeInteger(value) || value < -0x80000000 || value > 0x7fffffff)) {
    throw new Error("invalid_source_details");
  }
  const provenance = requiredString(parsed, "provenance", 32);
  if (provenance !== "steam_import_score_only") throw new Error("invalid_provenance");
  return {
    schema_version: API_VERSION,
    steam_id: steamId,
    auth_app_id: boundedInteger(parsed, "auth_app_id", 1, 0x7fffffff),
    board_id: boardId,
    track_content_id: requiredString(parsed, "track_content_id", 256),
    track_gameplay_digest: trackDigest,
    track_title: requiredString(parsed, "track_title", 256),
    score_milliseconds: boundedInteger(parsed, "score_milliseconds", 1, 0x7fffffff),
    ruleset_revision: boundedInteger(parsed, "ruleset_revision", 1, 0x7fffffff),
    persona_name: optionalString(parsed, "persona_name", 128),
    avatar_url: validWebUrl(optionalString(parsed, "avatar_url", 512), "avatar_url"),
    profile_url: validWebUrl(optionalString(parsed, "profile_url", 512), "profile_url"),
    source_timestamp_unix: boundedInteger(parsed, "source_timestamp_unix", 0, 0x7fffffff),
    source_global_rank: boundedInteger(parsed, "source_global_rank", 0, 10_000_000),
    source_ugc_handle: optionalString(parsed, "source_ugc_handle", 32),
    source_details: details as number[],
    unavailable_reason: requiredString(parsed, "unavailable_reason", 128),
    provenance,
  };
}

export function parsePositiveInteger(value: string | null, fallback: number, maximum: number): number {
  if (value === null || value === "") return fallback;
  if (!/^[0-9]+$/.test(value)) throw new Error("invalid_integer");
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1 || parsed > maximum) throw new Error("invalid_integer");
  return parsed;
}

export function parseSteamId(value: string | null): string {
  if (value === null || !STEAM_ID_PATTERN.test(value)) throw new Error("invalid_steam_id");
  return value;
}

export function parseBoardId(value: string): string {
  if (!BOARD_ID_PATTERN.test(value)) throw new Error("invalid_board_id");
  return value;
}

export function parseVehicleDigest(value: string | null): string {
  if (value === null || value === "") return "";
  if (!DIGEST_PATTERN.test(value)) throw new Error("invalid_vehicle_digest");
  return value;
}

export function parseRunId(value: string): string {
  if (!RUN_ID_PATTERN.test(value)) throw new Error("invalid_run_id");
  return value;
}

export function encodeCursor(cursor: LeaderboardCursor): string {
  const bytes = new TextEncoder().encode(JSON.stringify(cursor));
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function parseCursor(value: string | null): LeaderboardCursor | null {
  if (value === null || value === "") return null;
  if (value.length > 512 || !/^[A-Za-z0-9_-]+$/.test(value)) throw new Error("invalid_cursor");
  let parsed: unknown;
  try {
    const base64 = value.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
    parsed = JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(base64), (character) => character.charCodeAt(0)))) as unknown;
  } catch {
    throw new Error("invalid_cursor");
  }
  if (!isRecord(parsed)) throw new Error("invalid_cursor");
  const score = boundedInteger(parsed, "score_milliseconds", 1, 0x7fffffff);
  const received = boundedInteger(parsed, "received_unix", 0, Number.MAX_SAFE_INTEGER);
  const runId = requiredString(parsed, "run_id", 64);
  if (!RUN_ID_PATTERN.test(runId)) throw new Error("invalid_cursor");
  return { score_milliseconds: score, received_unix: received, run_id: runId };
}
