extends SceneTree

const TICKS_TO_RUN := 180


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_path := _argument_value("--ghost-replay-path")
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path):
		_fail("--ghost-replay-path must identify a cached leaderboard replay")
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
	var track_index := game_manager.track_content_controller.track_index_for_id(String(replay.get("track_content_id", "")))
	if track_index < 0:
		_fail("replay track is unavailable")
		return
	var racer_ids: Array = replay.get("racer_ids", [])
	var settings: Array = replay.get("settings", [])
	if racer_ids.size() != 1 or settings.size() != 1:
		_fail("replay is not a one-racer stream")
		return
	var setting: Dictionary = settings[0]
	var descriptor_template: Dictionary = {
		"cache_path": replay_path,
		"replay_sha256": "smoke_0",
		"slot_index": 0,
		"persona_name": "Ghost Smoke",
		"steam_id": int(racer_ids[0]),
		"global_rank": 1,
		"score_milliseconds": 1,
		"compatibility_warning": false,
		"trusted_details": {
			"track_gameplay_digest": String(replay.get("track_gameplay_digest", "")),
			"vehicle_gameplay_digest": String(setting.get("vehicle_gameplay_digest", "")),
		},
		"validation": {
			"valid": true,
			"vehicle_content_id": String(setting.get("vehicle_content_id", "")),
		},
	}
	var controller: TimeAttackGhostController = game_manager.time_attack_ghost_controller
	var previous_native_bytes := 0
	for ghost_count in [0, 1, 2, 4]:
		var descriptors: Array = []
		for slot_index in range(ghost_count):
			var descriptor: Dictionary = descriptor_template.duplicate(true)
			descriptor["replay_sha256"] = "smoke_%d" % slot_index
			descriptor["slot_index"] = slot_index
			descriptor["persona_name"] = "Ghost Smoke %d" % (slot_index + 1)
			descriptors.append(descriptor)
		var prepare: Dictionary = controller.prepare(descriptors, track_index)
		if !bool(prepare.get("success", false)):
			_fail("%d-ghost prepare failed: %s" % [ghost_count, String(prepare.get("message", "unknown"))])
			return
		var start: Dictionary = controller.start_race(track_index)
		if !bool(start.get("success", false)) or controller.runtime_slots.size() != ghost_count:
			_fail("%d isolated ghost simulations did not start" % ghost_count)
			return
		for tick in range(TICKS_TO_RUN):
			controller.tick(tick)
		controller.call("_process", 1.0 / 60.0)
		var sim_ids: Dictionary = {}
		for slot_value in controller.runtime_slots:
			var slot: Dictionary = slot_value
			if int(slot.get("frame_index", 0)) != TICKS_TO_RUN:
				_fail("%d-ghost scenario did not advance each ghost exactly once per live tick" % ghost_count)
				return
			var sim: GameSim = slot.get("sim", null)
			if sim == null or sim_ids.has(sim.get_instance_id()):
				_fail("%d-ghost scenario did not allocate independent simulations" % ghost_count)
				return
			sim_ids[sim.get_instance_id()] = true
		var memory: Dictionary = controller.memory_usage_stats()
		var native_bytes := int(memory.get("aggregate_tracked_native_bytes", 0))
		if int(memory.get("ghost_count", 0)) != ghost_count:
			_fail("%d-ghost scenario reported the wrong memory slot count" % ghost_count)
			return
		if ghost_count == 0:
			if native_bytes != 0 or controller.render_manager != null:
				_fail("zero-ghost baseline allocated ghost runtime resources")
				return
		else:
			if native_bytes <= previous_native_bytes:
				_fail("%d-ghost native memory did not scale above the prior scenario" % ghost_count)
				return
			previous_native_bytes = native_bytes
			if controller.render_manager == null or controller.render_manager.archetypes.size() != ghost_count:
				_fail("%d-ghost shared renderer was not configured" % ghost_count)
				return
			for archetype_value in controller.render_manager.archetypes:
				var archetype: Dictionary = archetype_value
				var main_multimesh: MultiMesh = (archetype["main"] as Dictionary)["multimesh"]
				var shadow_multimesh: MultiMesh = (archetype["shadow"] as Dictionary)["multimesh"]
				if main_multimesh.visible_instance_count != 1 or shadow_multimesh.visible_instance_count != 0:
					_fail("%d-ghost render submission did not preserve the no-shadow treatment" % ghost_count)
					return
		controller.teardown_runtime()
		if !controller.runtime_slots.is_empty() or controller.render_manager != null:
			_fail("%d-ghost runtime did not tear down deterministically" % ghost_count)
			return
		print("MXT_TIME_ATTACK_GHOST_SCENARIO_OK ghosts=", ghost_count,
			" ticks=", TICKS_TO_RUN, " native_bytes=", native_bytes)
	print("MXT_TIME_ATTACK_GHOST_SMOKE_OK scenarios=0,1,2,4 ticks=", TICKS_TO_RUN)
	game_manager.queue_free()
	await process_frame
	quit(0)


func _argument_value(flag: String) -> String:
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	for values in [args, user_args]:
		var index := (values as Array).find(flag)
		if index >= 0 and index + 1 < (values as Array).size():
			return String((values as Array)[index + 1])
	return ""


func _fail(message: String) -> void:
	push_error("MXT_TIME_ATTACK_GHOST_SMOKE_FAIL " + message)
	quit(1)
