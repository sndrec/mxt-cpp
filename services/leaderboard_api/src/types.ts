export const API_VERSION = 1;
export const DIGEST_PATTERN = /^sha256:[0-9a-f]{64}$/;
export const STEAM_ID_PATTERN = /^[0-9]{17,20}$/;
export const BOARD_ID_PATTERN = /^[a-z0-9_-]{1,128}$/;
export const RUN_ID_PATTERN = /^[0-9a-f]{64}$/;

export type GameVersion = Readonly<{
  major: number;
  compatibility: number;
  patch: number;
}>;

export type VerifiedRunEnvelope = Readonly<{
  schema_version: 1;
  run_kind: "ranked_time_attack";
  track_source: "official";
  vehicle_source: "official";
  steam_id: string;
  auth_app_id: number;
  board_id: string;
  track_content_id: string;
  track_gameplay_digest: string;
  track_title: string;
  vehicle_content_id: string;
  vehicle_gameplay_digest: string;
  machine_setting_percent: number;
  score_milliseconds: number;
  ruleset_revision: number;
  replay_schema_version: number;
  game_version: GameVersion;
  replay_sha256: string;
  replay_byte_length: number;
  persona_name: string;
  avatar_url: string;
  profile_url: string;
  source_timestamp_unix: number;
  provenance: "native_submission" | "steam_import";
}>;

export type HistoricalScoreImport = Readonly<{
  schema_version: 1;
  steam_id: string;
  auth_app_id: number;
  board_id: string;
  track_content_id: string;
  track_gameplay_digest: string;
  track_title: string;
  score_milliseconds: number;
  ruleset_revision: number;
  persona_name: string;
  avatar_url: string;
  profile_url: string;
  source_timestamp_unix: number;
  source_global_rank: number;
  source_ugc_handle: string;
  source_details: readonly number[];
  unavailable_reason: string;
  provenance: "steam_import_score_only";
}>;

export type ModerationRequest = Readonly<{
  schema_version: 1;
  target_kind: "run" | "historical_score" | "player";
  target_id: string;
  state: "visible" | "quarantined" | "hidden";
  reason: string;
  operator: string;
}>;

export type LeaderboardCursor = Readonly<{
  score_milliseconds: number;
  received_unix: number;
  run_id: string;
}>;

export type LeaderboardEntryRow = Readonly<{
  global_rank: number;
  run_id: string;
  steam_id: string;
  persona_name: string;
  avatar_url: string;
  profile_url: string;
  score_milliseconds: number;
  received_unix: number;
  track_content_id: string;
  track_gameplay_digest: string;
  vehicle_content_id: string;
  vehicle_gameplay_digest: string;
  machine_setting_percent: number;
  ruleset_revision: number;
  replay_schema_version: number;
  game_version_major: number;
  game_version_compatibility: number;
  game_version_patch: number;
  replay_sha256: string;
  replay_byte_length: number;
  provenance: string;
  historical_unavailable_reason: string;
}>;

export type RunReplayRow = Readonly<{
  run_id: string;
  replay_sha256: string;
  object_key: string;
  replay_byte_length: number;
  moderation_state: string;
  player_moderation_state: string;
}>;
