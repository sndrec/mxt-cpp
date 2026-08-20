extends SceneTree

const ReplayValidatorClass = preload("res://steam/leaderboard_replay_validator.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_path := _argument_value("--ghost-replay-path")
	var output_path := _argument_value("--ghost-capture-output")
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path) or output_path.is_empty():
		_fail("replay and capture output arguments are required")
		return
	var replay_value = JSON.parse_string(FileAccess.get_file_as_string(replay_path))
	if typeof(replay_value) != TYPE_DICTIONARY:
		_fail("cached replay did not parse")
		return
	var replay: Dictionary = replay_value
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	game_manager.set_physics_process(false)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
	var validation: Dictionary = ReplayValidatorClass.validate(game_manager, replay)
	if !bool(validation.get("valid", false)):
		_fail("fixture replay is not canonical: %s" % String(validation.get("reason", "unknown")))
		return
	var track_index := game_manager.track_content_controller.track_index_for_id(String(replay.get("track_content_id", "")))
	if track_index < 0:
		_fail("fixture track is unavailable")
		return
	var racer_ids: Array = replay.get("racer_ids", [])
	var descriptor := {
		"cache_path": replay_path,
		"replay_sha256": "visual_capture",
		"slot_index": 0,
		"persona_name": "Leaderboard Ghost",
		"steam_id": int(racer_ids[0]),
		"global_rank": 1,
		"score_milliseconds": 1,
		"compatibility_warning": false,
		"trusted_details": {
			"track_gameplay_digest": String(validation.get("track_gameplay_digest", "")),
			"vehicle_gameplay_digest": String(validation.get("vehicle_gameplay_digest", "")),
		},
		"validation": validation.duplicate(true),
	}
	game_manager.track_selector.select(track_index)
	game_manager.singleplayer_cpu_count = 0
	game_manager._on_time_attack_setup_start_requested(false, {"ghost_descriptors": [descriptor]})
	var controller := game_manager.time_attack_ghost_controller
	if controller.runtime_slots.size() != 1:
		_fail("visual ghost runtime did not start")
		return
	var frames_to_run := mini(300, int((controller.runtime_slots[0] as Dictionary).get("frame_count", 0)) - 1)
	for tick in range(frames_to_run):
		controller.tick(tick)
	controller.call("_process", 1.0 / 60.0)
	var slot: Dictionary = controller.runtime_slots[0]
	var sim: GameSim = slot.get("sim", null)
	var racer_id := int(slot.get("racer_id", -1))
	if sim == null or racer_id < 0:
		_fail("visual ghost transform is unavailable")
		return
	var car_transform: Transform3D = sim.get_player_render_transform(racer_id)
	var camera := Camera3D.new()
	game_manager.add_child(camera)
	var up := car_transform.basis.y.normalized()
	var back := car_transform.basis.z.normalized()
	camera.global_position = car_transform.origin + up * 12.0 + back * 22.0
	camera.look_at(car_transform.origin + up * 1.5, up)
	camera.current = true
	await process_frame
	await process_frame
	await process_frame
	var output_directory := output_path.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(output_directory) != OK:
		_fail("could not create capture directory")
		return
	var image := root.get_texture().get_image()
	if image == null or image.is_empty() or image.save_png(output_path) != OK:
		_fail("could not save ghost visibility capture")
		return
	print("MXT_TIME_ATTACK_GHOST_VISUAL_CAPTURE_OK track=", String(replay.get("track_name", "")),
		" output=", output_path)
	game_manager._return_to_menu()
	game_manager.queue_free()
	await process_frame
	quit(0)


func _argument_value(flag: String) -> String:
	for values in [OS.get_cmdline_args(), OS.get_cmdline_user_args()]:
		var index := (values as Array).find(flag)
		if index >= 0 and index + 1 < (values as Array).size():
			return String((values as Array)[index + 1])
	return ""


func _fail(message: String) -> void:
	push_error("MXT_TIME_ATTACK_GHOST_VISUAL_CAPTURE_FAIL " + message)
	quit(1)
