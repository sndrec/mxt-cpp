class_name TimeAttackRules extends RefCounted

const MANIFEST_PATH := "res://steam/leaderboards.json"
const RULESET_REVISION := 2
const LAP_COUNT := 3
const TICKS_PER_SECOND := 60

static var _manifest: Dictionary = {}

static func build_options() -> Dictionary:
	return {
		"game_mode": 0,
		"vehicle_restore": true,
		"bumpers": false,
		"s_boost": false,
		"cpu_count": 0,
		"lap_count": LAP_COUNT,
		"session_kind": "time_attack",
		"time_attack_ruleset_revision": RULESET_REVISION,
		"grand_prix_current_track": 0,
		"grand_prix_points": {},
		"grand_prix_ko_energy_bonuses": {},
		"grand_prix_eliminated_ids": [],
	}

static func options_match(options: Dictionary) -> bool:
	return int(options.get("game_mode", -1)) == 0 \
		and bool(options.get("vehicle_restore", false)) \
		and !bool(options.get("bumpers", true)) \
		and !bool(options.get("s_boost", true)) \
		and int(options.get("cpu_count", -1)) == 0 \
		and int(options.get("lap_count", -1)) == LAP_COUNT \
		and String(options.get("session_kind", "")) == "time_attack" \
		and int(options.get("time_attack_ruleset_revision", -1)) == RULESET_REVISION

static func finish_ticks_to_milliseconds(finish_tick: int, start_tick: int) -> int:
	return int(round(float(maxi(0, finish_tick - start_tick)) * 1000.0 / float(TICKS_PER_SECOND)))

static func manifest() -> Dictionary:
	if _manifest.is_empty():
		var value = JSON.parse_string(FileAccess.get_file_as_string(MANIFEST_PATH))
		if typeof(value) == TYPE_DICTIONARY:
			_manifest = value
	return _manifest

static func board_for_track_digest(gameplay_digest: String) -> Dictionary:
	for value in manifest().get("boards", []):
		if typeof(value) == TYPE_DICTIONARY and String(value.get("track_gameplay_digest", "")) == gameplay_digest:
			return (value as Dictionary).duplicate(true)
	return {}

static func track_record_matches_board(record: Dictionary, board: Dictionary) -> bool:
	if String(record.get("gameplay_digest", "")) != String(board.get("track_gameplay_digest", "")):
		return false
	var track_source := String(board.get("track_source", "official"))
	if track_source == "official":
		return String(record.get("source", "")) == "official"
	if track_source == "curated_workshop":
		return String(record.get("source", "")) == "workshop" \
			and String(record.get("published_file_id", "")) == String(board.get("published_file_id", ""))
	return false

static func board_for_name(steam_name: String) -> Dictionary:
	for value in manifest().get("boards", []):
		if typeof(value) == TYPE_DICTIONARY and String(value.get("steam_name", "")) == steam_name:
			return (value as Dictionary).duplicate(true)
	return {}

static func board_title(board: Dictionary) -> String:
	return String(board.get(
		"track_title",
		String(board.get("track_slug", "Track")).replace("-", " ").capitalize()))

static func friendly_reason(reason: String) -> String:
	match reason:
		"modified_time_attack_rules": return "Ranked rules were changed. Restore the standard three-lap Time Attack rules."
		"debug_or_automation_enabled": return "Debug or automation controls are active. Disable them for a ranked run."
		"invalid_track_identity": return "The selected track does not have a complete content identity."
		"track_has_no_ranked_board": return "This track does not currently have a ranked Steam leaderboard."
		"uncurated_or_mismatched_track": return "The selected track is not the exact official or curated version used by this leaderboard."
		"unofficial_or_mismatched_vehicle": return "Ranked Time Attack requires an exact official machine. You can switch directly to the All Rounder or practice unranked."
		"invalid_finish_time": return "The race did not produce a valid finish time."
		"replay_recording_failed": return "The run finished, but its verification replay could not be saved."
		"replay_too_long": return "The replay is longer than the one-hour verification limit."
		"noncanonical_frames": return "The replay has missing, out-of-order, or malformed input frames."
		"draft_vehicle": return "Draft machines are available only in unranked test drives."
		"ineligible": return "This run is not eligible for the ranked leaderboard."
		_: return reason.replace("_", " ").capitalize()

static func rules_description() -> String:
	return "3 laps · Vehicle Restore enabled · No bumpers · No S-BOOST · No CPU racers"
