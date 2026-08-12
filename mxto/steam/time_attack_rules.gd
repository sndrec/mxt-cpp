class_name TimeAttackRules extends RefCounted

const MANIFEST_PATH := "res://steam/leaderboards.json"
const RULESET_REVISION := 1
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
