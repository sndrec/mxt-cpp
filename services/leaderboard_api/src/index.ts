import { signHmac, verifyHmac } from "./crypto";
import { importHistoricalScore } from "./historical";
import { publicEntry, readAroundPlayer, readLeaderboard } from "./leaderboards";
import { applyModeration } from "./moderation";
import { readCategories, readPlayerBests, readRun } from "./reads";
import { archiveVerifiedRun, replayRow, storeReplay } from "./storage";
import { API_VERSION } from "./types";
import {
  encodeCursor,
  parseBoardId,
  parseCursor,
  parseHistoricalScoreImport,
  parseModerationRequest,
  parsePositiveInteger,
  parseRunId,
  parseSteamId,
  parseVehicleDigest,
  parseVerifiedEnvelope,
} from "./validation";

const SERVICE_NAME = "mxt-leaderboard-api";
const SIGNATURE_WINDOW_SECONDS = 300;

class ApiError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message = code) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

function jsonResponse(payload: object, status = 200, requestId = ""): Response {
  const headers = new Headers({
    "Cache-Control": "no-store",
    "Content-Type": "application/json; charset=utf-8",
    "X-Content-Type-Options": "nosniff",
  });
  if (requestId !== "") headers.set("X-MXT-Request-ID", requestId);
  return Response.json(payload, { status, headers });
}

function requiredHeader(request: Request, name: string, maximumLength: number): string {
  const value = request.headers.get(name) ?? "";
  if (value.length === 0 || value.length > maximumLength) {
    throw new ApiError(400, `missing_${name.toLowerCase().replace(/-/g, "_")}`);
  }
  return value;
}

function integerEnvironment(value: string, name: string, minimum: number, maximum: number): number {
  if (!/^[0-9]+$/.test(value)) throw new Error(`invalid_${name}`);
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum) {
    throw new Error(`invalid_${name}`);
  }
  return parsed;
}

async function handleIngest(request: Request, env: Env, requestId: string): Promise<Response> {
  if (request.headers.get("Content-Type") !== "application/vnd.mxt.replay") {
    throw new ApiError(415, "invalid_content_type");
  }
  const timestampText = requiredHeader(request, "X-MXT-Ingest-Timestamp", 32);
  const metadataEncoded = requiredHeader(request, "X-MXT-Ingest-Metadata", 16_384);
  const signature = requiredHeader(request, "X-MXT-Ingest-Signature", 128).toLowerCase();
  if (!/^[0-9]+$/.test(timestampText)) throw new ApiError(401, "invalid_ingest_timestamp");
  const timestamp = Number(timestampText);
  const now = Math.floor(Date.now() / 1000);
  if (!Number.isSafeInteger(timestamp) || Math.abs(now - timestamp) > SIGNATURE_WINDOW_SECONDS) {
    throw new ApiError(401, "expired_ingest_signature");
  }
  if (!await verifyHmac(env.INGEST_SECRET, `${timestampText}\n${metadataEncoded}`, signature)) {
    throw new ApiError(401, "invalid_ingest_signature");
  }
  let envelope;
  try {
    envelope = parseVerifiedEnvelope(metadataEncoded);
  } catch (error) {
    throw new ApiError(400, error instanceof Error ? error.message : "invalid_metadata");
  }
  const lengthText = request.headers.get("Content-Length") ?? "";
  if (!/^[0-9]+$/.test(lengthText)) throw new ApiError(411, "missing_content_length");
  const contentLength = Number(lengthText);
  const maximumBytes = integerEnvironment(env.MAX_REPLAY_BYTES, "maximum_replay_bytes", 1, 100 * 1024 * 1024);
  if (contentLength !== envelope.replay_byte_length || contentLength <= 0 || contentLength > maximumBytes) {
    throw new ApiError(413, "invalid_replay_size");
  }
  if (request.body === null) throw new ApiError(400, "missing_replay_body");

  const objectKey = await storeReplay(env, envelope, request.body);
  let archive;
  try {
    archive = await archiveVerifiedRun(env, envelope, objectKey, now);
  } catch (error) {
    if (error instanceof Error && error.message === "competitive_vehicle_digest_conflict") {
      throw new ApiError(409, error.message,
        "This vehicle digest does not belong to the board's competitive revision.");
    }
    throw error;
  }
  console.log(JSON.stringify({
    event: "verified_run_archived",
    request_id: requestId,
    run_id: archive.run_id,
    board_id: envelope.board_id,
    steam_id: envelope.steam_id,
    score_milliseconds: envelope.score_milliseconds,
    run_created: archive.run_created,
    vehicle_best_changed: archive.vehicle_best_changed,
  }));
  return jsonResponse({
    ok: true,
    archived: true,
    board_id: envelope.board_id,
    steam_id: envelope.steam_id,
    score_milliseconds: envelope.score_milliseconds,
    replay_sha256: envelope.replay_sha256,
    ...archive,
  }, 200, requestId);
}

async function handleHistoricalScoreImport(request: Request, env: Env, requestId: string): Promise<Response> {
  if (request.headers.get("Content-Type") !== "application/json") {
    throw new ApiError(415, "invalid_content_type");
  }
  const timestampText = requiredHeader(request, "X-MXT-Migration-Timestamp", 32);
  const signature = requiredHeader(request, "X-MXT-Migration-Signature", 128).toLowerCase();
  if (!/^[0-9]+$/.test(timestampText)) throw new ApiError(401, "invalid_migration_timestamp");
  const timestamp = Number(timestampText);
  const now = Math.floor(Date.now() / 1000);
  if (!Number.isSafeInteger(timestamp) || Math.abs(now - timestamp) > SIGNATURE_WINDOW_SECONDS) {
    throw new ApiError(401, "expired_migration_signature");
  }
  const lengthText = request.headers.get("Content-Length") ?? "";
  if (!/^[0-9]+$/.test(lengthText) || Number(lengthText) <= 0 || Number(lengthText) > 65_536) {
    throw new ApiError(413, "invalid_migration_record_size");
  }
  const body = await request.text();
  if (!await verifyHmac(env.MIGRATION_SECRET, `${timestampText}\n${body}`, signature)) {
    throw new ApiError(401, "invalid_migration_signature");
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(body) as unknown;
  } catch {
    throw new ApiError(400, "invalid_metadata");
  }
  let record;
  try {
    record = parseHistoricalScoreImport(parsed);
  } catch (error) {
    throw new ApiError(400, error instanceof Error ? error.message : "invalid_metadata");
  }
  const imported = await importHistoricalScore(env, record, now);
  console.log(JSON.stringify({
    event: "historical_score_imported",
    request_id: requestId,
    historical_id: imported.historical_id,
    board_id: record.board_id,
    steam_id: record.steam_id,
    score_milliseconds: imported.score_milliseconds,
    changed: imported.changed,
  }));
  return jsonResponse({ ok: true, replay_available: false, ...imported }, 200, requestId);
}

async function handleModeration(request: Request, env: Env, requestId: string): Promise<Response> {
  if (request.headers.get("Content-Type") !== "application/json") {
    throw new ApiError(415, "invalid_content_type");
  }
  const timestampText = requiredHeader(request, "X-MXT-Admin-Timestamp", 32);
  const signature = requiredHeader(request, "X-MXT-Admin-Signature", 128).toLowerCase();
  if (!/^[0-9]+$/.test(timestampText)) throw new ApiError(401, "invalid_admin_timestamp");
  const timestamp = Number(timestampText);
  const now = Math.floor(Date.now() / 1000);
  if (!Number.isSafeInteger(timestamp) || Math.abs(now - timestamp) > SIGNATURE_WINDOW_SECONDS) {
    throw new ApiError(401, "expired_admin_signature");
  }
  const lengthText = request.headers.get("Content-Length") ?? "";
  if (!/^[0-9]+$/.test(lengthText) || Number(lengthText) <= 0 || Number(lengthText) > 65_536) {
    throw new ApiError(413, "invalid_admin_record_size");
  }
  const body = await request.text();
  if (!await verifyHmac(env.ADMIN_SECRET, `${timestampText}\n${body}`, signature)) {
    throw new ApiError(401, "invalid_admin_signature");
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(body) as unknown;
  } catch {
    throw new ApiError(400, "invalid_metadata");
  }
  let moderation;
  try {
    moderation = parseModerationRequest(parsed);
  } catch (error) {
    throw new ApiError(400, error instanceof Error ? error.message : "invalid_metadata");
  }
  try {
    const result = await applyModeration(env, moderation, now);
    console.log(JSON.stringify({ event: "moderation_applied", request_id: requestId, ...result }));
    return jsonResponse({ ok: true, ...result }, 200, requestId);
  } catch (error) {
    if (error instanceof Error && error.message === "moderation_target_not_found") {
      throw new ApiError(404, error.message);
    }
    throw error;
  }
}

async function handleLeaderboard(url: URL, env: Env, boardId: string, requestId: string): Promise<Response> {
  const scope = url.searchParams.get("scope") ?? "global";
  const vehicleDigest = parseVehicleDigest(url.searchParams.get("vehicle_digest"));
  if (scope === "friends") {
    throw new ApiError(501, "friends_scope_not_enabled", "Friends filtering is not enabled on this deployment.");
  }
  let rows;
  let nextCursor = "";
  if (scope === "around_user") {
    const steamId = parseSteamId(url.searchParams.get("steam_id"));
    rows = await readAroundPlayer(env, boardId, vehicleDigest, steamId, 5);
  } else if (scope === "global") {
    const limit = parsePositiveInteger(url.searchParams.get("limit"), 100, 100);
    const cursor = parseCursor(url.searchParams.get("cursor"));
    const fetched = await readLeaderboard(env, boardId, vehicleDigest, cursor, limit + 1);
    const hasMore = fetched.length > limit;
    rows = hasMore ? fetched.slice(0, limit) : fetched;
    const tail = rows.at(-1);
    if (hasMore && tail !== undefined) {
      nextCursor = encodeCursor({
        score_milliseconds: tail.score_milliseconds,
        received_unix: tail.received_unix,
        run_id: tail.run_id,
      });
    }
  } else {
    throw new ApiError(400, "invalid_scope");
  }
  return jsonResponse({
    ok: true,
    board_id: boardId,
    scope,
    vehicle_gameplay_digest: vehicleDigest,
    entries: rows.map(publicEntry),
    next_cursor: nextCursor,
  }, 200, requestId);
}

async function handleCategories(env: Env, boardId: string, requestId: string): Promise<Response> {
  return jsonResponse({
    ok: true,
    board_id: boardId,
    categories: await readCategories(env, boardId),
  }, 200, requestId);
}

async function handlePlayerBests(
  env: Env,
  boardId: string,
  steamId: string,
  requestId: string,
): Promise<Response> {
  const rows = await readPlayerBests(env, boardId, steamId);
  return jsonResponse({
    ok: true,
    board_id: boardId,
    steam_id: steamId,
    entries: rows.map(publicEntry),
  }, 200, requestId);
}

async function handleRun(env: Env, runId: string, requestId: string): Promise<Response> {
  const row = await readRun(env, runId);
  if (row === null) throw new ApiError(404, "run_not_found");
  return jsonResponse({ ok: true, entry: publicEntry(row) }, 200, requestId);
}

async function handleReplayUrl(request: Request, env: Env, runId: string, requestId: string): Promise<Response> {
  const row = await replayRow(env, runId);
  if (row === null || row.moderation_state !== "visible" || row.player_moderation_state !== "visible") {
    throw new ApiError(404, "replay_not_found");
  }
  const ttl = integerEnvironment(env.REPLAY_URL_TTL_SECONDS, "replay_url_ttl_seconds", 30, 3600);
  const expires = Math.floor(Date.now() / 1000) + ttl;
  const signature = await signHmac(env.REPLAY_URL_SECRET, `${runId}\n${expires}`);
  const origin = new URL(request.url).origin;
  const replayUrl = `${origin}/v1/replays/${runId}?expires=${expires}&signature=${signature}`;
  return jsonResponse({
    ok: true,
    run_id: runId,
    replay_sha256: row.replay_sha256,
    replay_byte_length: row.replay_byte_length,
    expires_unix: expires,
    replay_url: replayUrl,
  }, 200, requestId);
}

async function handleReplayDownload(url: URL, env: Env, runId: string, requestId: string): Promise<Response> {
  const expiresText = url.searchParams.get("expires") ?? "";
  const signature = (url.searchParams.get("signature") ?? "").toLowerCase();
  if (!/^[0-9]+$/.test(expiresText)) throw new ApiError(401, "invalid_replay_url");
  const expires = Number(expiresText);
  const now = Math.floor(Date.now() / 1000);
  const ttl = integerEnvironment(env.REPLAY_URL_TTL_SECONDS, "replay_url_ttl_seconds", 30, 3600);
  if (!Number.isSafeInteger(expires) || expires < now || expires > now + ttl + 30) {
    throw new ApiError(401, "expired_replay_url");
  }
  if (!await verifyHmac(env.REPLAY_URL_SECRET, `${runId}\n${expires}`, signature)) {
    throw new ApiError(401, "invalid_replay_url");
  }
  const row = await replayRow(env, runId);
  if (row === null || row.moderation_state !== "visible" || row.player_moderation_state !== "visible") {
    throw new ApiError(404, "replay_not_found");
  }
  const object = await env.REPLAYS.get(row.object_key);
  if (object === null) throw new ApiError(503, "replay_storage_unavailable");
  const headers = new Headers({
    "Cache-Control": "private, max-age=300, immutable",
    "Content-Length": String(object.size),
    "Content-Type": "application/vnd.mxt.replay",
    "ETag": object.httpEtag,
    "X-Content-Type-Options": "nosniff",
    "X-MXT-Replay-SHA256": row.replay_sha256,
    "X-MXT-Request-ID": requestId,
  });
  return new Response(object.body, { status: 200, headers });
}

async function route(request: Request, env: Env, requestId: string): Promise<Response> {
  const url = new URL(request.url);
  if (request.method === "GET" && url.pathname === "/healthz") {
    await env.DB.prepare("SELECT 1").first();
    return jsonResponse({ ok: true, service: SERVICE_NAME, api_version: API_VERSION }, 200, requestId);
  }
  if (request.method === "POST" && url.pathname === "/v1/ingest/verified-run") {
    return handleIngest(request, env, requestId);
  }
  if (request.method === "POST" && url.pathname === "/v1/admin/import-historical-score") {
    return handleHistoricalScoreImport(request, env, requestId);
  }
  if (request.method === "POST" && url.pathname === "/v1/admin/moderation") {
    return handleModeration(request, env, requestId);
  }
  let match = /^\/v1\/leaderboards\/([^/]+)$/.exec(url.pathname);
  if (request.method === "GET" && match?.[1] !== undefined) {
    return handleLeaderboard(url, env, parseBoardId(decodeURIComponent(match[1])), requestId);
  }
  match = /^\/v1\/boards\/([^/]+)\/categories$/.exec(url.pathname);
  if (request.method === "GET" && match?.[1] !== undefined) {
    return handleCategories(env, parseBoardId(decodeURIComponent(match[1])), requestId);
  }
  match = /^\/v1\/boards\/([^/]+)\/players\/([^/]+)\/bests$/.exec(url.pathname);
  if (request.method === "GET" && match?.[1] !== undefined && match[2] !== undefined) {
    return handlePlayerBests(
      env,
      parseBoardId(decodeURIComponent(match[1])),
      parseSteamId(decodeURIComponent(match[2])),
      requestId,
    );
  }
  match = /^\/v1\/runs\/([^/]+)$/.exec(url.pathname);
  if (request.method === "GET" && match?.[1] !== undefined) {
    return handleRun(env, parseRunId(match[1]), requestId);
  }
  match = /^\/v1\/runs\/([^/]+)\/replay-url$/.exec(url.pathname);
  if (request.method === "GET" && match?.[1] !== undefined) {
    return handleReplayUrl(request, env, parseRunId(match[1]), requestId);
  }
  match = /^\/v1\/replays\/([^/]+)$/.exec(url.pathname);
  if (request.method === "GET" && match?.[1] !== undefined) {
    return handleReplayDownload(url, env, parseRunId(match[1]), requestId);
  }
  throw new ApiError(404, "not_found");
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const requestId = crypto.randomUUID();
    try {
      return await route(request, env, requestId);
    } catch (error) {
      const apiError = error instanceof ApiError ? error : new ApiError(500, "internal_server_error");
      console.error(JSON.stringify({
        event: "request_failed",
        request_id: requestId,
        method: request.method,
        path: new URL(request.url).pathname,
        status: apiError.status,
        error: apiError.code,
        internal_message: error instanceof Error ? error.message : String(error),
      }));
      return jsonResponse({
        ok: false,
        error: apiError.code,
        message: apiError.message,
        request_id: requestId,
      }, apiError.status, requestId);
    }
  },
} satisfies ExportedHandler<Env>;
