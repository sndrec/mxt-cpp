class_name LocalTimeAttackReplayCatalog extends RefCounted

const GameVersionData = preload("res://core/game_version.gd")
const REPLAY_SCHEMA_VERSION := 5
const REPLAY_SUFFIX := ".mxt_replay"
const PHYSICS_TICKS_PER_SECOND := 60.0

var replay_root := "user://replays"


func scan(game_manager: GameManager, track_index: int) -> Array:
	var entries: Array = []
	if game_manager == null or track_index < 0:
		return entries
	var expected_track_digest := game_manager.track_content_controller.track_gameplay_digest_for_index(track_index)
	var absolute_root := ProjectSettings.globalize_path(replay_root)
	var directory := DirAccess.open(absolute_root)
	if directory == null:
		return entries
	for file_name in directory.get_files():
		if !file_name.ends_with(REPLAY_SUFFIX):
			continue
		var path := absolute_root.path_join(file_name)
		var metadata := _load_metadata(path)
		if !_is_compatible_local_ghost(metadata, expected_track_digest):
			continue
		var entry := _entry_from_metadata(game_manager, path, metadata)
		if !entry.is_empty():
			entries.append(entry)
	entries.sort_custom(func(a: Dictionary, b: Dictionary):
		return float(a.get("created_unix", 0.0)) > float(b.get("created_unix", 0.0)))
	return entries


func prepare_entry(game_manager: GameManager, entry: Dictionary, track_index: int) -> Dictionary:
	var path := String(entry.get("_local_path", ""))
	if path.is_empty() or !FileAccess.file_exists(path):
		return _failure("local_replay_missing", "That local replay file no longer exists.")
	var replay_stream := MxtReplayStream.new()
	if !replay_stream.load_file(path):
		return _failure("local_replay_invalid", "That local replay could not be parsed.")
	var replay: Dictionary = replay_stream.get_metadata()
	var expected_track_digest := game_manager.track_content_controller.track_gameplay_digest_for_index(track_index)
	if !_is_compatible_local_ghost(replay, expected_track_digest):
		return _failure("local_replay_incompatible", "That replay is not a compatible single-racer Time Attack or Practice run for this track.")
	if replay_stream.frame_count() <= 0:
		return _failure("local_replay_invalid", "That local replay contains no race input.")
	var digest := _file_digest(path)
	if digest.is_empty():
		return _failure("local_replay_invalid", "That local replay could not be read.")
	var prepared_entry := entry.duplicate(true)
	var trusted_details := {
		"replay_sha256": digest,
		"track_gameplay_digest": expected_track_digest,
	}
	var validation := {
		"valid": true,
		"track_gameplay_digest": expected_track_digest,
		"replay_schema_version": int(replay.get("schema_version", -1)),
		"game_version": replay.get("game_version", {}).duplicate(true) if typeof(replay.get("game_version", {})) == TYPE_DICTIONARY else {},
	}
	prepared_entry["_trusted_details"] = trusted_details
	prepared_entry["_local_path"] = path
	prepared_entry["_replay_available"] = true
	prepared_entry["_compatibility_warning"] = _has_compatibility_warning(replay)
	prepared_entry["_local_validation"] = validation
	return {"success": true, "entry": prepared_entry}


func _entry_from_metadata(game_manager: GameManager, path: String, metadata: Dictionary) -> Dictionary:
	var digest := _file_digest(path)
	if digest.is_empty():
		return {}
	var settings: Dictionary = (metadata.get("settings", []) as Array)[0]
	var players: Array = metadata.get("players", []) as Array
	var player: Dictionary = players[0] if !players.is_empty() and typeof(players[0]) == TYPE_DICTIONARY else {}
	var racer_id := int((metadata.get("racer_ids", []) as Array)[0])
	var finish_tick := _dictionary_int(metadata.get("finish_times", {}), racer_id)
	if finish_tick <= 0:
		finish_tick = int(metadata.get("duration_ticks", 0))
	var definition: CarDefinition = game_manager.vehicle_content_controller.get_definition(String(settings.get("vehicle_content_id", "")))
	var machine_name := definition.name if definition != null else String(settings.get("vehicle_content_id", "Unknown"))
	var machine_setting := roundi(float(settings.get("accel_setting", 0.5)) * 100.0)
	var created_unix := float(metadata.get("created_unix", 0.0))
	var date_text := Time.get_datetime_string_from_unix_time(int(created_unix), true) if created_unix > 0.0 else "Unknown date"
	var trusted_details := {
		"replay_sha256": digest,
		"track_gameplay_digest": String(metadata.get("track_gameplay_digest", "")),
	}
	return {
		"_source": "local",
		"_local_path": path,
		"_replay_available": true,
		"_compatibility_warning": _has_compatibility_warning(metadata),
		"_trusted_details": trusted_details,
		"_display_rank": "Local",
		"_display_player": String(player.get("username", settings.get("username", "Local replay"))),
		"_display_vehicle": "%s · %d%%" % [machine_name, machine_setting],
		"_display_version": "%s · %s" % [String(metadata.get("build", "Unknown version")), date_text],
		"steam_id": 0,
		"persona_name": String(player.get("username", settings.get("username", "Local replay"))),
		"global_rank": 0,
		"score_milliseconds": roundi(float(finish_tick) * 1000.0 / PHYSICS_TICKS_PER_SECOND),
		"ugc_handle": 0,
		"created_unix": created_unix,
	}


func _is_compatible_local_ghost(metadata: Dictionary, expected_track_digest: String) -> bool:
	if metadata.is_empty() or int(metadata.get("schema_version", -1)) != REPLAY_SCHEMA_VERSION:
		return false
	var source := String(metadata.get("source", ""))
	var mode := String(metadata.get("mode", ""))
	if !((source == "singleplayer" and mode == "Time Attack") \
			or (source == "practice" and mode == "Practice")):
		return false
	if String(metadata.get("track_gameplay_digest", "")) != expected_track_digest:
		return false
	var racer_ids_value = metadata.get("racer_ids", [])
	var cpu_flags_value = metadata.get("cpu_flags", [])
	var settings_value = metadata.get("settings", [])
	if typeof(racer_ids_value) != TYPE_ARRAY or (racer_ids_value as Array).size() != 1:
		return false
	if typeof(cpu_flags_value) != TYPE_ARRAY or (cpu_flags_value as Array).size() != 1 or bool((cpu_flags_value as Array)[0]):
		return false
	if source == "practice":
		var race_options_value = metadata.get("race_options", {})
		if typeof(race_options_value) != TYPE_DICTIONARY \
				or int((race_options_value as Dictionary).get("lap_count", 0)) <= 0:
			return false
	return typeof(settings_value) == TYPE_ARRAY and (settings_value as Array).size() == 1 \
		and typeof((settings_value as Array)[0]) == TYPE_DICTIONARY


func _load_metadata(path: String) -> Dictionary:
	if path.is_empty() or !FileAccess.file_exists(path):
		return {}
	var stream := MxtReplayStream.new()
	return stream.get_metadata() if stream.load_file(path, true) else {}


func _file_digest(path: String) -> String:
	var hex := FileAccess.get_sha256(path)
	return "sha256:" + hex if !hex.is_empty() else ""


func _has_compatibility_warning(replay: Dictionary) -> bool:
	var version_value = replay.get("game_version", {})
	if typeof(version_value) != TYPE_DICTIONARY:
		return true
	var version: Dictionary = version_value
	return int(version.get("major", -1)) != GameVersionData.MAJOR \
		or int(version.get("compatibility", -1)) != GameVersionData.COMPATIBILITY


func _dictionary_int(source_value, key: int) -> int:
	if typeof(source_value) != TYPE_DICTIONARY:
		return -1
	var source: Dictionary = source_value
	return int(source[key]) if source.has(key) else int(source.get(str(key), -1))


func _failure(reason: String, message: String) -> Dictionary:
	return {"success": false, "reason": reason, "message": message}
