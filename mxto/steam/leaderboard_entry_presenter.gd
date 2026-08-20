class_name LeaderboardEntryPresenter extends RefCounted

const LeaderboardDetailsClass = preload("res://steam/leaderboard_details.gd")
const GameVersionData = preload("res://core/game_version.gd")


static func decorate(game_manager: GameManager, entry: Dictionary) -> Dictionary:
	var result := entry.duplicate(true)
	var decoded := LeaderboardDetailsClass.decode(entry.get("details", []))
	result["_trusted_details"] = decoded
	result["_display_vehicle"] = _vehicle_display(game_manager, decoded)
	result["_display_version"] = _version_display(decoded)
	result["_replay_available"] = _replay_available(result, decoded)
	result["_compatibility_warning"] = _compatibility_warning(decoded)
	return result


static func _vehicle_display(game_manager: GameManager, decoded: Dictionary) -> String:
	if decoded.is_empty():
		return "Unknown"
	var vehicle_name := _vehicle_name_for_digest(game_manager, String(decoded.get("vehicle_gameplay_digest", "")))
	var machine_setting_percent := int(decoded.get("machine_setting_percent", -1))
	return "%s · %d%%" % [vehicle_name, machine_setting_percent] if machine_setting_percent >= 0 else vehicle_name


static func _version_display(decoded: Dictionary) -> String:
	if decoded.is_empty():
		return "Legacy / unknown"
	var version_value = decoded.get("game_version", {})
	var version: Dictionary = version_value if typeof(version_value) == TYPE_DICTIONARY else {}
	return "v%d.%d.%d · replay r%d" % [
		int(version.get("major", 0)),
		int(version.get("compatibility", 0)),
		int(version.get("patch", 0)),
		int(decoded.get("replay_schema_version", 0)),
	]


static func _vehicle_name_for_digest(game_manager: GameManager, gameplay_digest: String) -> String:
	if game_manager != null and game_manager.vehicle_content_controller != null:
		for definition_value in game_manager.vehicle_content_controller.definitions:
			var definition: CarDefinition = definition_value
			if definition == null:
				continue
			var record: Dictionary = game_manager.vehicle_content_controller.content_catalog.resolve_content(definition.content_id)
			if String(record.get("gameplay_digest", "")) == gameplay_digest:
				return definition.name
	return "Digest %s…" % gameplay_digest.trim_prefix("sha256:").left(8)


static func _replay_available(entry: Dictionary, decoded: Dictionary) -> bool:
	var ugc_handle := int(entry.get("ugc_handle", 0))
	return ugc_handle != 0 and ugc_handle != -1 and !String(decoded.get("replay_sha256", "")).is_empty()


static func _compatibility_warning(decoded: Dictionary) -> bool:
	var version_value = decoded.get("game_version", {})
	if typeof(version_value) != TYPE_DICTIONARY:
		return false
	var version: Dictionary = version_value
	return int(version.get("major", -1)) != GameVersionData.MAJOR \
		or int(version.get("compatibility", -1)) != GameVersionData.COMPATIBILITY
