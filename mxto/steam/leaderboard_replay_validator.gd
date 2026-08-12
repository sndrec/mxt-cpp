class_name LeaderboardReplayValidator extends RefCounted

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")
const MAX_REPLAY_TICKS := 60 * 60 * 60

static func reject(reason: String) -> Dictionary:
	return {"valid": false, "reason": reason}

static func _dictionary_int(source: Dictionary, id: int, fallback: int = -1) -> int:
	if source.has(id):
		return int(source[id])
	var string_id := str(id)
	if source.has(string_id):
		return int(source[string_id])
	return fallback

static func _single_id_dictionary_matches(source_value, id: int, expected: int) -> bool:
	if typeof(source_value) != TYPE_DICTIONARY:
		return false
	var source: Dictionary = source_value
	return source.size() == 1 and _dictionary_int(source, id) == expected

static func _is_empty_dictionary(value) -> bool:
	return typeof(value) == TYPE_DICTIONARY and (value as Dictionary).is_empty()

static func _is_empty_array(value) -> bool:
	return typeof(value) == TYPE_ARRAY and (value as Array).is_empty()

static func _runtime_flags_are_clean(replay: Dictionary) -> bool:
	var flags_value = replay.get("runtime_flags", {})
	if typeof(flags_value) != TYPE_DICTIONARY:
		return false
	var flags: Dictionary = flags_value
	return flags.size() == 4 \
		and !bool(flags.get("auto_accelerate", true)) \
		and !bool(flags.get("auto_bumpers", true)) \
		and !bool(flags.get("debug_bumper_smoke", true)) \
		and !bool(flags.get("debug_rail_trace", true))

static func _frames_are_canonical(frames: Array, racer_id: int) -> bool:
	if frames.is_empty() or frames.size() > MAX_REPLAY_TICKS:
		return false
	var string_id := str(racer_id)
	for tick in range(frames.size()):
		if typeof(frames[tick]) != TYPE_DICTIONARY:
			return false
		var frame: Dictionary = frames[tick]
		if int(frame.get("tick", -1)) != tick:
			return false
		var inputs_value = frame.get("inputs", {})
		if typeof(inputs_value) != TYPE_DICTIONARY:
			return false
		var inputs: Dictionary = inputs_value
		if inputs.size() != 1 or !inputs.has(string_id) or typeof(inputs[string_id]) != TYPE_STRING:
			return false
	return true

static func validate(game_manager: GameManager, replay: Dictionary) -> Dictionary:
	if String(replay.get("source", "")) != "singleplayer" or String(replay.get("mode", "")) != "Time Attack":
		return reject("not_singleplayer_time_attack")
	if String(replay.get("saved_reason", "")) != "time_attack_submission":
		return reject("not_submission_capture")
	if !_runtime_flags_are_clean(replay):
		return reject("debug_or_automation_enabled")

	var options_value = replay.get("race_options", {})
	if typeof(options_value) != TYPE_DICTIONARY:
		return reject("missing_time_attack_rules")
	var options: Dictionary = options_value
	if !TimeAttackRulesClass.options_match(options):
		return reject("modified_time_attack_rules")
	if !bool(options.get("leaderboard_eligible", false)) or !String(options.get("leaderboard_ineligible_reason", "")).is_empty():
		return reject("capture_marked_ineligible")
	if int(options.get("grand_prix_current_track", -1)) != 0 \
		or !_is_empty_dictionary(options.get("grand_prix_points", {})) \
		or !_is_empty_dictionary(options.get("grand_prix_ko_energy_bonuses", {})) \
		or !_is_empty_array(options.get("grand_prix_eliminated_ids", [])):
		return reject("noncanonical_race_state")

	var racer_ids_value = replay.get("racer_ids", [])
	var cpu_flags_value = replay.get("cpu_flags", [])
	var settings_value = replay.get("settings", [])
	var players_value = replay.get("players", [])
	if typeof(racer_ids_value) != TYPE_ARRAY or typeof(cpu_flags_value) != TYPE_ARRAY \
		or typeof(settings_value) != TYPE_ARRAY or typeof(players_value) != TYPE_ARRAY:
		return reject("invalid_roster")
	var racer_ids: Array = racer_ids_value
	var cpu_flags: Array = cpu_flags_value
	var settings: Array = settings_value
	var players: Array = players_value
	if racer_ids.size() != 1 or cpu_flags.size() != 1 or settings.size() != 1 or players.size() != 1:
		return reject("time_attack_requires_one_racer")
	var racer_id := int(racer_ids[0])
	if racer_id <= 0 or bool(cpu_flags[0]):
		return reject("time_attack_requires_one_human")
	if typeof(settings[0]) != TYPE_DICTIONARY or typeof(players[0]) != TYPE_DICTIONARY:
		return reject("invalid_player_settings")
	var player: Dictionary = players[0]
	if int(player.get("id", -1)) != racer_id or bool(player.get("cpu", true)):
		return reject("player_record_mismatch")

	var track_id := String(replay.get("track_content_id", ""))
	var track_digest := String(replay.get("track_gameplay_digest", ""))
	var track_ids_value = options.get("track_ids", [])
	var track_digests_value = options.get("track_gameplay_digests", [])
	if typeof(track_ids_value) != TYPE_ARRAY or typeof(track_digests_value) != TYPE_ARRAY:
		return reject("track_identity_mismatch")
	var track_ids: Array = track_ids_value
	var track_digests: Array = track_digests_value
	if track_ids.size() != 1 or track_digests.size() != 1 \
		or String(track_ids[0]) != track_id or String(track_digests[0]) != track_digest:
		return reject("track_identity_mismatch")
	var track_record: Dictionary = game_manager.content_catalog.resolve_content(track_id)
	var board := TimeAttackRulesClass.board_for_track_digest(track_digest)
	if board.is_empty():
		return reject("track_has_no_ranked_board")
	if !TimeAttackRulesClass.track_record_matches_board(track_record, board):
		return reject("uncurated_or_mismatched_track")
	var expected_workshop_id := String(board.get("published_file_id", ""))
	if String(replay.get("track_workshop_id", "")) != expected_workshop_id:
		return reject("track_workshop_identity_mismatch")
	var option_workshop_ids_value = options.get("track_workshop_ids", [])
	if typeof(option_workshop_ids_value) != TYPE_ARRAY or (option_workshop_ids_value as Array).size() != 1 \
		or String((option_workshop_ids_value as Array)[0]) != expected_workshop_id:
		return reject("track_workshop_identity_mismatch")

	var player_settings: Dictionary = settings[0]
	var vehicle_id := String(player_settings.get("vehicle_content_id", ""))
	var vehicle_digest := String(player_settings.get("vehicle_gameplay_digest", ""))
	var vehicle_record: Dictionary = game_manager.content_catalog.resolve_content(vehicle_id)
	if String(vehicle_record.get("source", "")) != "official" \
		or String(vehicle_record.get("gameplay_digest", "")) != vehicle_digest:
		return reject("unofficial_or_mismatched_vehicle")
	if String(player.get("vehicle_content_id", "")) != vehicle_id \
		or String(player.get("vehicle_gameplay_digest", "")) != vehicle_digest:
		return reject("vehicle_identity_mismatch")

	var frames_value = replay.get("frames", [])
	if typeof(frames_value) != TYPE_ARRAY or !_frames_are_canonical(frames_value as Array, racer_id):
		return reject("noncanonical_frames")
	var frames: Array = frames_value
	if int(replay.get("duration_ticks", -1)) != frames.size():
		return reject("duration_mismatch")
	var finish_times_value = replay.get("finish_times", {})
	if typeof(finish_times_value) != TYPE_DICTIONARY:
		return reject("invalid_finish_result")
	var finish_tick := _dictionary_int(finish_times_value as Dictionary, racer_id)
	if finish_tick <= 0 or finish_tick > frames.size():
		return reject("invalid_finish_tick")
	if !_single_id_dictionary_matches(replay.get("finish_times", {}), racer_id, finish_tick) \
		or !_single_id_dictionary_matches(replay.get("finish_placements", {}), racer_id, 1):
		return reject("invalid_finish_result")
	var eliminations_value = replay.get("eliminations", {})
	if typeof(eliminations_value) != TYPE_DICTIONARY or !(eliminations_value as Dictionary).is_empty():
		return reject("racer_was_eliminated")

	return {
		"valid": true,
		"reason": "",
		"racer_id": racer_id,
		"board_name": String(board.get("steam_name", "")),
		"track_content_id": track_id,
		"track_gameplay_digest": track_digest,
		"vehicle_content_id": vehicle_id,
		"vehicle_gameplay_digest": vehicle_digest,
		"ruleset_revision": TimeAttackRulesClass.RULESET_REVISION,
		"game_version": replay.get("game_version", {}).duplicate(true) if typeof(replay.get("game_version", {})) == TYPE_DICTIONARY else {},
	}
