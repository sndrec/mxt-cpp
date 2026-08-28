extends SceneTree

const ReplayValidatorClass = preload("res://steam/leaderboard_replay_validator.gd")

var game_manager: GameManager


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_path := _argument_value("--ghost-replay-path")
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path):
		_fail("--ghost-replay-path must identify a cached leaderboard replay")
		return
	var replay_stream := MxtReplayStream.new()
	if !replay_stream.load_file(replay_path):
		_fail("cached replay did not parse")
		return
	var replay: Dictionary = replay_stream.get_metadata()
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	game_manager = packed.instantiate() as GameManager
	root.add_child(game_manager)
	game_manager.set_physics_process(false)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
	var validation: Dictionary = ReplayValidatorClass.validate(game_manager, replay, replay_stream)
	if !bool(validation.get("valid", false)):
		_fail("fixture replay is not canonical: %s metadata_ids=%s stream_ids=%s metadata_cpu=%s stream_cpu=%s" % [
			String(validation.get("reason", "unknown")),
			str(replay.get("racer_ids", [])),
			str(replay_stream.get_roster_ids()),
			str(replay.get("cpu_flags", [])),
			str(replay_stream.get_cpu_flags()),
		])
		return
	var track_index := game_manager.track_content_controller.track_index_for_id(String(replay.get("track_content_id", "")))
	if track_index < 0:
		_fail("fixture track is unavailable")
		return
	game_manager.track_selector.select(track_index)
	var descriptor := _descriptor(replay_path, replay, validation, 0)
	if !_exercise_isolation_and_reconstruction(track_index, descriptor):
		return
	if !(await _exercise_practice_cpu_lifecycle(track_index, descriptor)):
		return
	if !_exercise_ranked_recording_matrix(track_index, descriptor, replay, replay_stream):
		return
	if !_catalog_excludes_cache_files():
		return
	game_manager._return_to_menu()
	if !game_manager.time_attack_ghost_controller.runtime_slots.is_empty() \
			or game_manager.time_attack_ghost_controller.render_manager != null \
			or game_manager.game_sim.sim_started:
		_fail("final race exit left simulation or renderer state alive")
		return
	print("MXT_TIME_ATTACK_GHOST_LIFECYCLE_SMOKE_OK")
	game_manager.queue_free()
	await process_frame
	quit(0)


func _exercise_isolation_and_reconstruction(track_index: int, descriptor: Dictionary) -> bool:
	var controller := game_manager.time_attack_ghost_controller
	var second_descriptor := descriptor.duplicate(true)
	second_descriptor["replay_sha256"] = String(descriptor.get("replay_sha256", "")) + "_second"
	second_descriptor["slot_index"] = 1
	second_descriptor["persona_name"] = "Isolation Ghost"
	var prepare: Dictionary = controller.prepare([descriptor, second_descriptor], track_index)
	if !bool(prepare.get("success", false)):
		_fail("two-ghost isolation preparation failed: %s" % String(prepare.get("message", "unknown")))
		return false
	var start: Dictionary = controller.start_race(track_index)
	if !bool(start.get("success", false)) or controller.runtime_slots.size() != 2:
		_fail("two-ghost isolation runtime did not start")
		return false
	controller.tick(0)
	var sim_a: GameSim = (controller.runtime_slots[0] as Dictionary).get("sim", null)
	var sim_b: GameSim = (controller.runtime_slots[1] as Dictionary).get("sim", null)
	if sim_a == null or sim_b == null or sim_a == sim_b:
		_fail("ghost slots do not own distinct simulations")
		return false
	var state_a_before := sim_a.get_state_data(0)
	var state_b_before := sim_b.get_state_data(0)
	var state_stats: Dictionary = sim_a.get_network_state_size_stats()
	var trigger_count := int(state_stats.get("trigger_count", 0))
	var trigger_bytes := int(state_stats.get("triggers", 0))
	if state_a_before.is_empty() or state_a_before != state_b_before or trigger_count <= 0 or trigger_bytes <= 0:
		_fail("fixture did not provide identical simulations with mutable trigger state")
		return false
	var changed_state := state_a_before.duplicate()
	var trigger_offset := changed_state.size() - trigger_bytes
	var trigger_stride := trigger_bytes / trigger_count
	if trigger_stride < 10:
		_fail("serialized trigger state has an unexpected layout")
		return false
	for trigger_index in range(trigger_count):
		var offset := trigger_offset + trigger_index * trigger_stride
		changed_state[offset] = 1
		changed_state[offset + 1] = 0
		changed_state[offset + 2] = 0
		changed_state[offset + 3] = 128
		changed_state[offset + 4] = 63
		changed_state[offset + 5] = 123
		changed_state[offset + 6] = 0
		changed_state[offset + 7] = 0
		changed_state[offset + 8] = 0
		changed_state[offset + 9] = 1
	if !sim_a.load_state_data(0, changed_state):
		_fail("could not load a changed mutable trigger state into one ghost")
		return false
	var state_a_after := sim_a.get_state_data(0)
	var state_b_after := sim_b.get_state_data(0)
	if state_a_after == state_a_before or state_b_after != state_b_before:
		_fail("mutable trigger state crossed simulation boundaries")
		return false
	var old_sim_ids := [sim_a.get_instance_id(), sim_b.get_instance_id()]
	controller.teardown_runtime()
	var restart: Dictionary = controller.start_race(track_index)
	if !bool(restart.get("success", false)) or controller.runtime_slots.size() != 2:
		_fail("prepared ghosts could not reconstruct for Retry/Race Again")
		return false
	for slot_value in controller.runtime_slots:
		var slot: Dictionary = slot_value
		var restarted_sim: GameSim = slot.get("sim", null)
		if int(slot.get("frame_index", -1)) != 0 or restarted_sim == null or old_sim_ids.has(restarted_sim.get_instance_id()):
			_fail("reconstructed ghost did not restart at tick zero with fresh native state")
			return false
	var result_snapshot := JSON.stringify({
		"finish_times": game_manager.network_manager.race_results.player_finish_times,
		"dnfs": game_manager.network_manager.race_results.player_dnfs,
	})
	game_manager.network_manager.race_results.player_finish_times[999] = 1
	controller.call("_process", 1.0 / 60.0)
	for slot_value in controller.runtime_slots:
		if String((slot_value as Dictionary).get("state", "")) != "active":
			_fail("player completion stopped an unfinished ghost")
			return false
	game_manager.network_manager.race_results.player_finish_times.erase(999)
	for slot_value in controller.runtime_slots:
		(slot_value as Dictionary)["state"] = "finished"
	controller.call("_process", controller.FADE_SECONDS + 0.01)
	for slot_value in controller.runtime_slots:
		if String((slot_value as Dictionary).get("state", "")) != "hidden":
			_fail("finished ghost did not complete its presentation-only fade")
			return false
	var result_after := JSON.stringify({
		"finish_times": game_manager.network_manager.race_results.player_finish_times,
		"dnfs": game_manager.network_manager.race_results.player_dnfs,
	})
	if result_after != result_snapshot or game_manager.time_attack_finalized:
		_fail("ghost finish/fade mutated the player's result state")
		return false
	controller.teardown_runtime()
	return true


func _exercise_practice_cpu_lifecycle(track_index: int, descriptor: Dictionary) -> bool:
	game_manager.track_selector.select(track_index)
	game_manager.singleplayer_cpu_count = 1
	var configuration := MxtRaceConfiguration.new()
	configuration.session_kind = MxtRaceConfiguration.SESSION_PRACTICE
	configuration.vehicle_restore = true
	configuration.s_boost = false
	configuration.cpu_count = 1
	configuration.lap_count = 3
	configuration.leaderboard_ineligible_reason = "practice_unranked"
	game_manager._on_practice_start_requested(configuration, {"ghost_descriptors": [descriptor]})
	var controller := game_manager.time_attack_ghost_controller
	if !game_manager.game_sim.sim_started or controller.runtime_slots.size() != 1:
		_fail("practice race with CPU and ghost did not start")
		return false
	var main_memory: Dictionary = game_manager.game_sim.get_memory_usage_stats()
	var ghost_memory: Dictionary = ((controller.runtime_slots[0] as Dictionary).get("sim") as GameSim).get_memory_usage_stats()
	if int(main_memory.get("car_count", 0)) != 2 or int(ghost_memory.get("car_count", 0)) != 1:
		_fail("practice CPU was not kept in the main simulation and out of the ghost simulation")
		return false
	var recording := game_manager.replay_controller
	if recording.replay_recording_metadata.mode != "Practice" \
			or recording.replay_recording_racer_ids.size() != 2:
		_fail("practice CPU roster was not recorded independently from ghost selection")
		return false
	var first_sim_id := (((controller.runtime_slots[0] as Dictionary).get("sim")) as GameSim).get_instance_id()
	game_manager._on_pause_retry_pressed()
	await process_frame
	await process_frame
	if controller.runtime_slots.size() != 1:
		_fail("Retry did not restore the selected ghost")
		return false
	var retry_slot: Dictionary = controller.runtime_slots[0]
	var retry_sim: GameSim = retry_slot.get("sim", null)
	if retry_sim == null or retry_sim.get_instance_id() == first_sim_id or int(retry_slot.get("frame_index", -1)) != 0:
		_fail("Retry did not reconstruct the ghost from tick zero")
		return false
	var retry_sim_id := retry_sim.get_instance_id()
	game_manager._on_time_attack_race_again_requested()
	await process_frame
	await process_frame
	if controller.runtime_slots.size() != 1:
		_fail("Race Again did not restore the selected ghost")
		return false
	var race_again_slot: Dictionary = controller.runtime_slots[0]
	var race_again_sim: GameSim = race_again_slot.get("sim", null)
	if race_again_sim == null or race_again_sim.get_instance_id() == retry_sim_id or int(race_again_slot.get("frame_index", -1)) != 0:
		_fail("Race Again did not reconstruct the ghost from tick zero")
		return false
	if int(game_manager.game_sim.get_memory_usage_stats().get("car_count", 0)) != 2:
		_fail("practice CPU roster was lost across Race Again")
		return false
	game_manager._return_to_menu()
	return true


func _exercise_ranked_recording_matrix(track_index: int, descriptor: Dictionary, replay: Dictionary, replay_stream: MxtReplayStream) -> bool:
	var racer_ids: Array = replay.get("racer_ids", [])
	var settings: Array = replay.get("settings", [])
	var grid_values: Array = replay.get("start_grid_slots", [])
	var grid_slots := PackedInt32Array()
	for value in grid_values:
		grid_slots.append(int(value))
	for ghost_count in [0, 1, 2, 4]:
		var descriptors: Array = []
		for slot_index in range(ghost_count):
			var slot_descriptor := descriptor.duplicate(true)
			slot_descriptor["replay_sha256"] = "ranked_%d" % slot_index
			slot_descriptor["slot_index"] = slot_index
			descriptors.append(slot_descriptor)
		game_manager.track_selector.select(track_index)
		game_manager.singleplayer_cpu_count = 0
		game_manager._on_time_attack_ranked_start_requested({"ghost_descriptors": descriptors})
		if game_manager.time_attack_ghost_controller.runtime_slots.size() != ghost_count:
			_fail("ranked Time Attack did not start %d selected ghosts" % ghost_count)
			return false
		game_manager.network_manager.load_race_metadata_dictionary((replay.get("race_options", {}) as Dictionary))
		game_manager.singleplayer_mode = true
		var race_roster := MxtRaceRoster.new()
		if !race_roster.append_settings(int(racer_ids[0]), int(racer_ids[0]), false, false, false, settings[0]):
			_fail("failed to build ranked replay smoke roster")
			return false
		game_manager.replay_controller.start_recording(track_index, race_roster, grid_slots)
		var candidate: Dictionary = game_manager.replay_controller.replay_recording_metadata.to_dictionary()
		candidate["saved_reason"] = "time_attack_submission"
		candidate["duration_ticks"] = int(replay.get("duration_ticks", 0))
		candidate["finish_times"] = (replay.get("finish_times", {}) as Dictionary).duplicate(true)
		candidate["finish_placements"] = (replay.get("finish_placements", {}) as Dictionary).duplicate(true)
		candidate["eliminations"] = (replay.get("eliminations", {}) as Dictionary).duplicate(true)
		var candidate_validation: Dictionary = ReplayValidatorClass.validate(game_manager, candidate, replay_stream)
		if !bool(candidate_validation.get("valid", false)):
			_fail("%d-ghost ranked replay is noncanonical: %s" % [ghost_count, String(candidate_validation.get("reason", "unknown"))])
			return false
		if game_manager.replay_controller.replay_recording_racer_ids.size() != 1 \
				or JSON.stringify(candidate).findn("ghost") >= 0:
			_fail("%d-ghost selection leaked into the ranked replay roster or schema" % ghost_count)
			return false
		game_manager._return_to_menu()
	return true


func _catalog_excludes_cache_files() -> bool:
	var replay_controller := game_manager.replay_controller
	replay_controller._build_replay_catalog()
	replay_controller._refresh_replay_catalog()
	for entry_value in replay_controller.replay_catalog_entries:
		var entry: Dictionary = entry_value
		if String(entry.get("_path", "")).replace("\\", "/").contains("/leaderboard_replays/"):
			_fail("leaderboard cache file appeared in the saved replay catalog")
			return false
	return true


func _descriptor(replay_path: String, replay: Dictionary, validation: Dictionary, slot_index: int) -> Dictionary:
	var settings: Array = replay.get("settings", [])
	var setting: Dictionary = settings[0]
	var racer_ids: Array = replay.get("racer_ids", [])
	return {
		"cache_path": replay_path,
		"replay_sha256": "lifecycle_%d" % slot_index,
		"slot_index": slot_index,
		"persona_name": "Lifecycle Ghost",
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


func _argument_value(flag: String) -> String:
	for values in [OS.get_cmdline_args(), OS.get_cmdline_user_args()]:
		var index := (values as Array).find(flag)
		if index >= 0 and index + 1 < (values as Array).size():
			return String((values as Array)[index + 1])
	return ""


func _fail(message: String) -> void:
	push_error("MXT_TIME_ATTACK_GHOST_LIFECYCLE_SMOKE_FAIL " + message)
	quit(1)
