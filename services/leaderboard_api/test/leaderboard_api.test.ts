import { env, exports } from "cloudflare:workers";
import { beforeEach, describe, expect, it } from "vitest";

import type { VerifiedRunEnvelope } from "../src/types";

const INGEST_SECRET = "test-ingest-secret-with-sufficient-entropy";
const BASE_URL = "https://leaderboards.example.test";

function bytesToHex(bytes: Uint8Array): string {
  let output = "";
  for (const byte of bytes) output += byte.toString(16).padStart(2, "0");
  return output;
}

async function sha256(bytes: Uint8Array): Promise<string> {
  return `sha256:${bytesToHex(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)))}`;
}

function base64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

async function signature(message: string): Promise<string> {
  const key = await crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(INGEST_SECRET),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  return bytesToHex(new Uint8Array(await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(message))));
}

async function envelope(
  replay: Uint8Array,
  overrides: Partial<VerifiedRunEnvelope> = {},
): Promise<VerifiedRunEnvelope> {
  return {
    schema_version: 1,
    run_kind: "ranked_time_attack",
    track_source: "official",
    vehicle_source: "official",
    steam_id: "76561198000000001",
    auth_app_id: 5001340,
    board_id: "mxt_ta_test_11111111_r2",
    track_content_id: "official:test-track",
    track_gameplay_digest: `sha256:${"11".repeat(32)}`,
    track_title: "Test Track",
    vehicle_content_id: "official:all-rounder",
    vehicle_gameplay_digest: `sha256:${"22".repeat(32)}`,
    machine_setting_percent: 50,
    score_milliseconds: 60_000,
    ruleset_revision: 2,
    replay_schema_version: 5,
    game_version: { major: 0, compatibility: 3, patch: 2 },
    replay_sha256: await sha256(replay),
    replay_byte_length: replay.byteLength,
    persona_name: "Test Pilot",
    avatar_url: "https://avatars.example.test/test.jpg",
    profile_url: "https://steamcommunity.com/profiles/76561198000000001",
    source_timestamp_unix: Math.floor(Date.now() / 1000),
    provenance: "native_submission",
    ...overrides,
  };
}

async function ingest(
  replay: Uint8Array,
  metadata: VerifiedRunEnvelope,
  signatureOverride = "",
): Promise<Response> {
  const encoded = base64Url(new TextEncoder().encode(JSON.stringify(metadata)));
  const timestamp = String(Math.floor(Date.now() / 1000));
  const signed = signatureOverride === "" ? await signature(`${timestamp}\n${encoded}`) : signatureOverride;
  return exports.default.fetch(new Request(`${BASE_URL}/v1/ingest/verified-run`, {
    method: "POST",
    headers: {
      "Content-Length": String(replay.byteLength),
      "Content-Type": "application/vnd.mxt.replay",
      "X-MXT-Ingest-Metadata": encoded,
      "X-MXT-Ingest-Signature": signed,
      "X-MXT-Ingest-Timestamp": timestamp,
    },
    body: replay,
  }));
}

async function clearStorage(): Promise<void> {
  await env.DB.batch([
    env.DB.prepare("DELETE FROM player_vehicle_bests"),
    env.DB.prepare("DELETE FROM verified_runs"),
    env.DB.prepare("DELETE FROM replay_objects"),
    env.DB.prepare("DELETE FROM boards"),
    env.DB.prepare("DELETE FROM players"),
  ]);
  let cursor: string | undefined;
  do {
    const listed = await env.REPLAYS.list({ cursor });
    await Promise.all(listed.objects.map((object) => env.REPLAYS.delete(object.key)));
    cursor = listed.truncated ? listed.cursor : undefined;
  } while (cursor !== undefined);
}

beforeEach(clearStorage);

describe("custom leaderboard authority", () => {
  it("archives a verified run and serves its leaderboard replay", async () => {
    const replay = new TextEncoder().encode("valid ranked replay one");
    const metadata = await envelope(replay);
    const response = await ingest(replay, metadata);
    expect(response.status).toBe(200);
    const accepted = await response.json<Record<string, unknown>>();
    expect(accepted.ok).toBe(true);
    expect(accepted.archived).toBe(true);
    expect(accepted.run_created).toBe(true);
    expect(accepted.vehicle_best_changed).toBe(true);
    expect(accepted.global_rank).toBe(1);

    const runCount = await env.DB.prepare("SELECT COUNT(*) AS count FROM verified_runs").first<number>("count");
    expect(runCount).toBe(1);

    const boardResponse = await exports.default.fetch(
      `${BASE_URL}/v1/leaderboards/${metadata.board_id}?scope=global`,
    );
    expect(boardResponse.status).toBe(200);
    const board = await boardResponse.json<{ entries: Array<Record<string, unknown>> }>();
    expect(board.entries).toHaveLength(1);
    expect(board.entries[0]?.score_milliseconds).toBe(metadata.score_milliseconds);
    expect(board.entries[0]?.machine_setting_percent).toBe(50);
    const runId = String(board.entries[0]?.run_id ?? "");

    const urlResponse = await exports.default.fetch(`${BASE_URL}/v1/runs/${runId}/replay-url`);
    expect(urlResponse.status).toBe(200);
    const urlResult = await urlResponse.json<{ replay_url: string }>();
    const replayResponse = await exports.default.fetch(urlResult.replay_url);
    expect(replayResponse.status).toBe(200);
    expect(replayResponse.headers.get("X-MXT-Replay-SHA256")).toBe(metadata.replay_sha256);
    expect(new Uint8Array(await replayResponse.arrayBuffer())).toEqual(replay);
  });

  it("archives slower ranked completions without replacing the best", async () => {
    const fastReplay = new TextEncoder().encode("fast ranked replay");
    const slowReplay = new TextEncoder().encode("slower ranked replay");
    const fast = await envelope(fastReplay, { score_milliseconds: 50_000 });
    const slow = await envelope(slowReplay, { score_milliseconds: 70_000 });
    expect((await ingest(fastReplay, fast)).status).toBe(200);
    const slowResponse = await ingest(slowReplay, slow);
    expect(slowResponse.status).toBe(200);
    const slowResult = await slowResponse.json<Record<string, unknown>>();
    expect(slowResult.archived).toBe(true);
    expect(slowResult.run_created).toBe(true);
    expect(slowResult.vehicle_best_changed).toBe(false);
    expect(slowResult.is_vehicle_best).toBe(false);
    expect(slowResult.personal_best_milliseconds).toBe(50_000);

    const runCount = await env.DB.prepare("SELECT COUNT(*) AS count FROM verified_runs").first<number>("count");
    expect(runCount).toBe(2);
    const objectCount = await env.DB.prepare("SELECT COUNT(*) AS count FROM replay_objects").first<number>("count");
    expect(objectCount).toBe(2);
  });

  it.each([
    ["practice runs", { run_kind: "practice" }, "ineligible_run_kind"],
    ["custom tracks", { track_source: "custom" }, "ineligible_track_source"],
    ["Workshop vehicles", { vehicle_source: "workshop" }, "ineligible_vehicle_source"],
  ])("rejects %s before archival", async (_name, overrides, expectedError) => {
    const replay = new TextEncoder().encode("ineligible replay");
    const metadata = await envelope(replay);
    const encoded = base64Url(new TextEncoder().encode(JSON.stringify({ ...metadata, ...overrides })));
    const timestamp = String(Math.floor(Date.now() / 1000));
    const response = await exports.default.fetch(new Request(`${BASE_URL}/v1/ingest/verified-run`, {
      method: "POST",
      headers: {
        "Content-Length": String(replay.byteLength),
        "Content-Type": "application/vnd.mxt.replay",
        "X-MXT-Ingest-Metadata": encoded,
        "X-MXT-Ingest-Signature": await signature(`${timestamp}\n${encoded}`),
        "X-MXT-Ingest-Timestamp": timestamp,
      },
      body: replay,
    }));
    expect(response.status).toBe(400);
    expect(await response.json<Record<string, unknown>>()).toMatchObject({ error: expectedError });
    expect(await env.DB.prepare("SELECT COUNT(*) AS count FROM verified_runs").first<number>("count")).toBe(0);
    expect((await env.REPLAYS.list()).objects).toHaveLength(0);
  });

  it("makes identical retry submissions idempotent", async () => {
    const replay = new TextEncoder().encode("retry-safe ranked replay");
    const metadata = await envelope(replay);
    expect((await ingest(replay, metadata)).status).toBe(200);
    const retry = await ingest(replay, metadata);
    expect(retry.status).toBe(200);
    const result = await retry.json<Record<string, unknown>>();
    expect(result.run_created).toBe(false);
    expect(result.vehicle_best_changed).toBe(false);
    expect(result.is_vehicle_best).toBe(true);
    const runCount = await env.DB.prepare("SELECT COUNT(*) AS count FROM verified_runs").first<number>("count");
    expect(runCount).toBe(1);
  });

  it("derives one overall entry from independent vehicle bests", async () => {
    const firstReplay = new TextEncoder().encode("all rounder attempt");
    const secondReplay = new TextEncoder().encode("top speeder attempt");
    const rivalReplay = new TextEncoder().encode("rival attempt");
    const first = await envelope(firstReplay, { score_milliseconds: 62_000 });
    const second = await envelope(secondReplay, {
      score_milliseconds: 58_000,
      vehicle_content_id: "official:top-speeder",
      vehicle_gameplay_digest: `sha256:${"33".repeat(32)}`,
    });
    const rival = await envelope(rivalReplay, {
      steam_id: "76561198000000002",
      persona_name: "Rival Pilot",
      score_milliseconds: 59_000,
    });
    expect((await ingest(firstReplay, first)).status).toBe(200);
    expect((await ingest(secondReplay, second)).status).toBe(200);
    expect((await ingest(rivalReplay, rival)).status).toBe(200);

    const overallResponse = await exports.default.fetch(
      `${BASE_URL}/v1/leaderboards/${first.board_id}?scope=global`,
    );
    const overall = await overallResponse.json<{ entries: Array<Record<string, unknown>> }>();
    expect(overall.entries).toHaveLength(2);
    expect(overall.entries[0]?.steam_id).toBe(first.steam_id);
    expect(overall.entries[0]?.score_milliseconds).toBe(58_000);
    expect(overall.entries[1]?.steam_id).toBe(rival.steam_id);

    const vehicleResponse = await exports.default.fetch(
      `${BASE_URL}/v1/leaderboards/${first.board_id}?scope=global&vehicle_digest=${encodeURIComponent(first.vehicle_gameplay_digest)}`,
    );
    const vehicle = await vehicleResponse.json<{ entries: Array<Record<string, unknown>> }>();
    expect(vehicle.entries).toHaveLength(2);
    expect(vehicle.entries[0]?.score_milliseconds).toBe(59_000);
    expect(vehicle.entries[1]?.score_milliseconds).toBe(62_000);

    const categoriesResponse = await exports.default.fetch(
      `${BASE_URL}/v1/boards/${first.board_id}/categories`,
    );
    const categories = await categoriesResponse.json<{ categories: Array<Record<string, unknown>> }>();
    expect(categories.categories).toHaveLength(2);
    expect(categories.categories).toContainEqual(expect.objectContaining({
      vehicle_gameplay_digest: first.vehicle_gameplay_digest,
      entry_count: 2,
      record_milliseconds: 59_000,
      record_steam_id: rival.steam_id,
      record_persona_name: "Rival Pilot",
    }));

    const playerBestsResponse = await exports.default.fetch(
      `${BASE_URL}/v1/boards/${first.board_id}/players/${first.steam_id}/bests`,
    );
    const playerBests = await playerBestsResponse.json<{ entries: Array<Record<string, unknown>> }>();
    expect(playerBests.entries).toHaveLength(2);
    expect(playerBests.entries.map((entry) => entry.score_milliseconds)).toEqual([62_000, 58_000]);
  });

  it("rejects unsigned replay uploads before storing anything", async () => {
    const replay = new TextEncoder().encode("untrusted replay");
    const metadata = await envelope(replay);
    const response = await ingest(replay, metadata, "00".repeat(32));
    expect(response.status).toBe(401);
    const runCount = await env.DB.prepare("SELECT COUNT(*) AS count FROM verified_runs").first<number>("count");
    expect(runCount).toBe(0);
    expect((await env.REPLAYS.list()).objects).toHaveLength(0);
  });
});
