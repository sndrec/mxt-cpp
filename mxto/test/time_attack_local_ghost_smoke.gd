extends SceneTree


class SelectionCache extends LeaderboardReplayCache:
	func request_replay(_board_name: String, _entry: Dictionary) -> int:
		var token := next_token
		next_token += 1
		return token


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_path := _argument_value("--ghost-replay-path")
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path):
		_fail("--ghost-replay-path must identify a local Time Attack replay")
		return
	var replay_value = JSON.parse_string(FileAccess.get_file_as_string(replay_path))
	if typeof(replay_value) != TYPE_DICTIONARY:
		_fail("fixture replay did not parse")
		return
	var replay: Dictionary = replay_value
	var track_digest := String(replay.get("track_gameplay_digest", ""))
	var main_scene := load("res://main.tscn") as PackedScene
	var picker_scene := load("res://ui/time_attack_ghost_picker.tscn") as PackedScene
	if main_scene == null or picker_scene == null:
		_fail("required scenes did not load")
		return
	var game_manager := main_scene.instantiate() as GameManager
	root.add_child(game_manager)
	game_manager.set_physics_process(false)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
	await process_frame
	var track_index := _track_index_for_digest(game_manager, track_digest)
	if track_index < 0:
		_fail("fixture track is not installed")
		return
	game_manager.track_selector.select(track_index)

	var catalog := LocalTimeAttackReplayCatalog.new()
	catalog.replay_root = replay_path.get_base_dir()
	var entries := catalog.scan(game_manager, track_index)
	var fixture_entry := _entry_for_path(entries, replay_path)
	if fixture_entry.is_empty():
		var metadata := catalog._load_metadata(replay_path)
		_fail("catalog did not expose the exact-track local Time Attack fixture: root=%s files=%d metadata=%s" % [
			replay_path.get_base_dir(),
			entries.size(),
			JSON.stringify({
				"schema": metadata.get("schema_version", -1),
				"mode": metadata.get("mode", ""),
				"source": metadata.get("source", ""),
				"track": metadata.get("track_gameplay_digest", ""),
				"expected_track": track_digest,
				"digest": catalog._file_digest(replay_path),
			}),
		])
		return
	var prepared := catalog.prepare_entry(game_manager, fixture_entry, track_index)
	if !bool(prepared.get("success", false)):
		_fail("local replay preparation failed: %s" % JSON.stringify(prepared))
		return
	var entry: Dictionary = prepared.get("entry", {})
	var digest := String((entry.get("_trusted_details", {}) as Dictionary).get("replay_sha256", ""))
	if digest.is_empty() or int(entry.get("score", 0)) <= 0:
		_fail("catalog entry is missing digest or finish time")
		return

	var cache := SelectionCache.new()
	root.add_child(cache)
	var selection := TimeAttackGhostSelection.new()
	selection.initialize(cache)
	var scope := "local:" + track_digest
	selection.set_board(scope)
	var select_result := selection.select_local(scope, entry)
	if !bool(select_result.get("success", false)) or !selection.all_ready() or selection.count() != 1:
		_fail("local selection was not immediately ready: %s" % JSON.stringify(select_result))
		return
	var duplicate_entry := entry.duplicate(true)
	duplicate_entry["ugc_handle"] = 9001
	var duplicate_result := selection.select(scope, duplicate_entry)
	if !bool(duplicate_result.get("success", false)) or selection.count() != 1:
		_fail("local and leaderboard copies were not deduplicated by digest")
		return
	var controller_prepare := game_manager.time_attack_ghost_controller.prepare(selection.ready_descriptors(), track_index)
	if !bool(controller_prepare.get("success", false)) or int(controller_prepare.get("ghost_count", 0)) != 1:
		_fail("ghost controller refused the local descriptor: %s" % JSON.stringify(controller_prepare))
		return

	var picker := picker_scene.instantiate() as TimeAttackGhostPicker
	root.add_child(picker)
	picker.local_replay_catalog.replay_root = replay_path.get_base_dir()
	picker.initialize(game_manager, selection)
	picker.active_request_type = "local"
	picker.open_for_track(scope, "")
	await process_frame
	if picker.active_request_type != "local" or picker.visible_entries.is_empty() \
			or !picker.global_button.disabled or !picker.friends_button.disabled:
		_fail("local-only picker did not populate or disable Steam views")
		return
	print("MXT_TIME_ATTACK_LOCAL_GHOST_SMOKE_OK entries=", picker.visible_entries.size(),
		" deduplicated=true ready=true")
	picker.queue_free()
	cache.queue_free()
	game_manager.queue_free()
	await process_frame
	quit(0)


func _track_index_for_digest(game_manager: GameManager, digest: String) -> int:
	for track_index in range(game_manager.track_content_controller.tracks.size()):
		if game_manager.track_content_controller.track_gameplay_digest_for_index(track_index) == digest:
			return track_index
	return -1


func _entry_for_path(entries: Array, replay_path: String) -> Dictionary:
	var simplified := replay_path.replace("\\", "/").simplify_path().to_lower()
	for entry_value in entries:
		if typeof(entry_value) == TYPE_DICTIONARY \
				and String((entry_value as Dictionary).get("_local_path", "")).replace("\\", "/").simplify_path().to_lower() == simplified:
			return entry_value as Dictionary
	return {}


func _argument_value(flag: String) -> String:
	for values in [OS.get_cmdline_args(), OS.get_cmdline_user_args()]:
		var index := (values as Array).find(flag)
		if index >= 0 and index + 1 < (values as Array).size():
			return String((values as Array)[index + 1])
	return ""


func _fail(message: String) -> void:
	push_error("MXT_TIME_ATTACK_LOCAL_GHOST_SMOKE_FAIL " + message)
	quit(1)
