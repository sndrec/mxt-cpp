class_name LeaderboardEntryPresenter extends RefCounted

const GameVersionData = preload("res://core/game_version.gd")


static func decorate(game_manager: GameManager, entry: MxtLeaderboardEntry) -> MxtLeaderboardEntry:
	entry.set_presentation(
		entry.display_rank if !entry.display_rank.is_empty() else "#%d" % entry.rank,
		entry.display_player if !entry.display_player.is_empty() else player_name(entry),
		entry.display_vehicle if !entry.display_vehicle.is_empty() else _vehicle_display(game_manager, entry),
		entry.display_version if !entry.display_version.is_empty() else _version_display(entry),
		!entry.run_id.is_empty() and !entry.replay_sha256.is_empty() if entry.source != "local" else entry.replay_available,
		entry.compatibility_warning if entry.source == "local" else _compatibility_warning(entry))
	return entry


static func _vehicle_display(game_manager: GameManager, entry: MxtLeaderboardEntry) -> String:
	var display_name := vehicle_name(
		game_manager,
		entry.vehicle_content_id,
		entry.vehicle_gameplay_digest)
	var machine_setting_percent := entry.machine_setting_percent
	return "%s · %d%%" % [display_name, machine_setting_percent] if machine_setting_percent >= 0 else display_name


static func _version_display(entry: MxtLeaderboardEntry) -> String:
	if entry.provenance == "steam_import_score_only":
		return "Historical Steam entry · no replay"
	if entry.game_version_major < 0:
		return "Unknown version"
	return "v%d.%d.%d · replay r%d" % [
		entry.game_version_major,
		entry.game_version_compatibility,
		entry.game_version_patch,
		entry.replay_schema_version,
	]


static func vehicle_name(game_manager: GameManager, content_id: String, gameplay_digest: String) -> String:
	if content_id.is_empty() and gameplay_digest.is_empty():
		return "Unspecified"
	if game_manager != null and game_manager.vehicle_content_controller != null:
		var definition := game_manager.vehicle_content_controller.get_definition(content_id)
		if definition != null:
			return definition.name
		for definition_value in game_manager.vehicle_content_controller.definitions:
			var candidate: CarDefinition = definition_value
			if candidate == null:
				continue
			var record: MxtContentRecord = game_manager.vehicle_content_controller.content_catalog.resolve_content(candidate.content_id)
			if record != null and record.gameplay_digest == gameplay_digest:
				return candidate.name
	return "Digest %s…" % gameplay_digest.trim_prefix("sha256:").left(8)


static func player_name(entry: MxtLeaderboardEntry) -> String:
	var persona_name := entry.persona_name.strip_edges()
	return persona_name if !persona_name.is_empty() else "Steam %s" % str(entry.steam_id)


static func category_player_name(persona_name: String, steam_id) -> String:
	var clean_name := persona_name.strip_edges()
	return clean_name if !clean_name.is_empty() else "Steam %s" % str(steam_id)


static func _compatibility_warning(entry: MxtLeaderboardEntry) -> bool:
	return entry.game_version_major >= 0 and (entry.game_version_major != GameVersionData.MAJOR \
		or entry.game_version_compatibility != GameVersionData.COMPATIBILITY)
