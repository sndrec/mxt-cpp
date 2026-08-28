extends SceneTree

const LegacyReaderClass = preload("res://leaderboards/legacy_leaderboard_replay_reader.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var source_path := _argument_value("--legacy-replay")
	var output_path := _argument_value("--binary-output")
	if source_path.is_empty() or output_path.is_empty() or !FileAccess.file_exists(source_path):
		_fail("legacy source and binary output paths are required")
		return
	var packed := load("res://main.tscn") as PackedScene
	var game_manager := packed.instantiate() as GameManager if packed != null else null
	if game_manager == null:
		_fail("main scene could not be loaded")
		return
	root.add_child(game_manager)
	var result := LegacyReaderClass.convert(game_manager, FileAccess.get_file_as_bytes(source_path))
	if !bool(result.get("valid", false)):
		_fail("legacy replay was rejected: %s" % String(result.get("reason", "unknown")))
		return
	var stream: MxtReplayStream = result.get("_native_stream")
	var metadata_value = result.get("_native_metadata", {})
	if stream == null or typeof(metadata_value) != TYPE_DICTIONARY \
			or !stream.write_file(output_path, metadata_value as Dictionary):
		_fail("legacy replay conversion could not be written")
		return
	var loaded := MxtReplayStream.new()
	if !loaded.load_file(output_path) or int(loaded.get_metadata().get("schema_version", -1)) != 5 \
			or int(loaded.get_metadata().get("legacy_leaderboard_schema_version", -1)) != int(result.get("replay_schema_version", -2)):
		_fail("converted replay did not preserve its legacy trust metadata")
		return
	print("MXT_LEGACY_LEADERBOARD_REPLAY_READER_SMOKE_OK frames=%d output=%s" % [loaded.frame_count(), output_path])
	game_manager.queue_free()
	await process_frame
	quit()


func _argument_value(flag: String) -> String:
	for values in [OS.get_cmdline_args(), OS.get_cmdline_user_args()]:
		var index := (values as Array).find(flag)
		if index >= 0 and index + 1 < (values as Array).size():
			return String((values as Array)[index + 1])
	return ""


func _fail(message: String) -> void:
	push_error("MXT_LEGACY_LEADERBOARD_REPLAY_READER_SMOKE_FAIL " + message)
	quit(1)
