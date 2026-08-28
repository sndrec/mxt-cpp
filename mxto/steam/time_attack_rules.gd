class_name TimeAttackRules extends RefCounted

const MANIFEST_PATH := "res://steam/leaderboards.json"
const RULESET_REVISION := 2
const LAP_COUNT := 3
const TICKS_PER_SECOND := 60

static var _manifest: Dictionary = {}

static func build_configuration() -> MxtRaceConfiguration:
	var configuration := MxtRaceConfiguration.new()
	configuration.session_kind = MxtRaceConfiguration.SESSION_TIME_ATTACK
	configuration.game_mode = 0
	configuration.vehicle_restore = true
	configuration.bumpers = false
	configuration.s_boost = false
	configuration.cpu_count = 0
	configuration.lap_count = LAP_COUNT
	configuration.time_attack_ruleset_revision = RULESET_REVISION
	return configuration

static func configuration_matches(configuration: MxtRaceConfiguration) -> bool:
	return configuration != null \
		and configuration.game_mode == 0 \
		and configuration.vehicle_restore \
		and !configuration.bumpers \
		and !configuration.s_boost \
		and configuration.cpu_count == 0 \
		and configuration.lap_count == LAP_COUNT \
		and configuration.is_time_attack() \
		and configuration.time_attack_ruleset_revision == RULESET_REVISION

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
		if typeof(value) == TYPE_DICTIONARY \
				and String(value.get("track_source", "official")) == "official" \
				and String(value.get("track_gameplay_digest", "")) == gameplay_digest:
			return (value as Dictionary).duplicate(true)
	return {}

static func track_record_matches_board(record: Dictionary, board: Dictionary) -> bool:
	if String(record.get("gameplay_digest", "")) != String(board.get("track_gameplay_digest", "")):
		return false
	return String(board.get("track_source", "official")) == "official" \
		and String(record.get("source", "")) == "official"

static func board_for_name(steam_name: String) -> Dictionary:
	for value in manifest().get("boards", []):
		if typeof(value) == TYPE_DICTIONARY \
				and String(value.get("track_source", "official")) == "official" \
				and String(value.get("steam_name", "")) == steam_name:
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
		"track_has_no_ranked_board": return "This track does not currently have a ranked leaderboard."
		"uncurated_or_mismatched_track": return "The selected track is not the exact official version used by this leaderboard."
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
