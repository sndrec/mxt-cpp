class_name LeaderboardEntryPresenter extends RefCounted

const GameVersionData = preload("res://core/game_version.gd")


static func decorate(game_manager: GameManager, entry: Dictionary) -> Dictionary:
	var result := entry.duplicate(true)
	result["_trusted_details"] = _trusted_details(entry)
	result["_display_vehicle"] = _vehicle_display(game_manager, entry)
	result["_display_version"] = _version_display(entry)
	result["_replay_available"] = !String(entry.get("run_id", "")).is_empty() \
		and !String(entry.get("replay_sha256", "")).is_empty()
	result["_compatibility_warning"] = _compatibility_warning(entry)
	return result


static func _trusted_details(entry: Dictionary) -> Dictionary:
	return {
		"replay_sha256": String(entry.get("replay_sha256", "")),
		"track_content_id": String(entry.get("track_content_id", "")),
		"track_gameplay_digest": String(entry.get("track_gameplay_digest", "")),
		"vehicle_content_id": String(entry.get("vehicle_content_id", "")),
		"vehicle_gameplay_digest": String(entry.get("vehicle_gameplay_digest", "")),
		"machine_setting_percent": int(entry.get("machine_setting_percent", -1)),
		"ruleset_revision": int(entry.get("ruleset_revision", -1)),
		"replay_schema_version": int(entry.get("replay_schema_version", -1)),
		"game_version": (entry.get("game_version", {}) as Dictionary).duplicate(true) \
			if typeof(entry.get("game_version", {})) == TYPE_DICTIONARY else {},
	}


static func _vehicle_display(game_manager: GameManager, entry: Dictionary) -> String:
	var vehicle_name := _vehicle_name(
		game_manager,
		String(entry.get("vehicle_content_id", "")),
		String(entry.get("vehicle_gameplay_digest", "")))
	var machine_setting_percent := int(entry.get("machine_setting_percent", -1))
	return "%s · %d%%" % [vehicle_name, machine_setting_percent] if machine_setting_percent >= 0 else vehicle_name


static func _version_display(entry: Dictionary) -> String:
	var version_value = entry.get("game_version", {})
	if typeof(version_value) != TYPE_DICTIONARY:
		return "Unknown version"
	var version: Dictionary = version_value
	return "v%d.%d.%d · replay r%d" % [
		int(version.get("major", 0)),
		int(version.get("compatibility", 0)),
		int(version.get("patch", 0)),
		int(entry.get("replay_schema_version", 0)),
	]


static func _vehicle_name(game_manager: GameManager, content_id: String, gameplay_digest: String) -> String:
	if game_manager != null and game_manager.vehicle_content_controller != null:
		var definition := game_manager.vehicle_content_controller.get_definition(content_id)
		if definition != null:
			return definition.name
		for definition_value in game_manager.vehicle_content_controller.definitions:
			var candidate: CarDefinition = definition_value
			if candidate == null:
				continue
			var record: Dictionary = game_manager.vehicle_content_controller.content_catalog.resolve_content(candidate.content_id)
			if String(record.get("gameplay_digest", "")) == gameplay_digest:
				return candidate.name
	return "Digest %s…" % gameplay_digest.trim_prefix("sha256:").left(8)


static func _compatibility_warning(entry: Dictionary) -> bool:
	var version_value = entry.get("game_version", {})
	if typeof(version_value) != TYPE_DICTIONARY:
		return false
	var version: Dictionary = version_value
	return int(version.get("major", -1)) != GameVersionData.MAJOR \
		or int(version.get("compatibility", -1)) != GameVersionData.COMPATIBILITY
