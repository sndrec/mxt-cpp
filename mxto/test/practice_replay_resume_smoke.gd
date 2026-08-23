extends SceneTree

var game_manager: GameManager


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_path := _argument_value("--practice-resume-replay")
	var keep_original := _argument_value("--keep-original") != "false"
	var partial_end := _argument_value("--partial-end") == "true"
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path):
		_fail("--practice-resume-replay must identify a replay")
		return
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("main scene could not be loaded")
		return
	game_manager = packed.instantiate() as GameManager
	root.add_child(game_manager)
	game_manager.headless_mode = false
	game_manager.set_physics_process(false)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
	game_manager.replay_controller.replay_skip_seek_bake_requested = true
	game_manager.replay_controller._start_replay_playback_from_path(replay_path)
	var replay := game_manager.replay_controller
	if !replay.replay_playback_active or replay._playback_frame_count() < 4:
		_fail("replay playback did not start")
		return
	var requested_focus := int(_argument_value("--focus-index")) if !_argument_value("--focus-index").is_empty() else 0
	replay.replay_playback_focus_index = clampi(requested_focus, 0, replay.replay_playback_racer_ids.size() - 1)
	replay._apply_replay_focus_to_local_visual()
	var source_frame_count := replay._playback_frame_count()
	if _argument_value("--expect-completed-disabled") == "true":
		if !replay._seek_replay_to_tick(source_frame_count, false):
			_fail("completed replay could not seek to its terminal cursor")
			return
		var terminal_eligibility: Dictionary = replay._replay_resume_eligibility()
		if bool(terminal_eligibility.get("eligible", true)) or !String(terminal_eligibility.get("reason", "")).contains("complet"):
			_fail("completed racer was not rejected with a specific reason")
			return
		print("MXT_PRACTICE_REPLAY_RESUME_COMPLETED_DISABLED_OK frames=", source_frame_count)
		game_manager._return_to_menu()
		game_manager.queue_free()
		await process_frame
		quit(0)
		return
	var cursor := clampi(source_frame_count / 3, 1, source_frame_count - 1)
	if !replay._seek_replay_to_tick(cursor, false):
		_fail("replay could not seek to the resume cursor")
		return
	if partial_end:
		var partial_stream := MxtReplayStream.new()
		if !partial_stream.copy_prefix_from(replay.replay_playback_stream, cursor):
			_fail("partial replay stream could not be constructed")
			return
		replay.replay_playback_stream = partial_stream
		replay.replay_playback_index = cursor
	var eligibility: Dictionary = replay._replay_resume_eligibility()
	if !bool(eligibility.get("eligible", false)):
		_fail("mid-replay cursor was not resumable: %s" % String(eligibility.get("reason", "unknown")))
		return
	var focus_id := replay._focused_replay_player_id()
	var expected_transform: Transform3D = game_manager.game_sim.get_player_physical_render_transform(focus_id)
	var source_ids: Array = []
	for id_value in replay.replay_playback_racer_ids:
		source_ids.append(int(id_value))
	var payload := replay._capture_replay_resume_payload(keep_original and !partial_end)
	if payload.is_empty():
		_fail("resumable replay frame could not be captured")
		return
	game_manager.resume_replay_in_practice(payload)
	var practice := game_manager.practice_controller
	if !practice.session_active or game_manager.replay_controller.replay_playback_active:
		_fail("replay did not transition into a live Practice session active=%s playback=%s singleplayer=%s sim=%s ghost_error=%s prepared_track=%d" % [
			str(practice.session_active),
			str(game_manager.replay_controller.replay_playback_active),
			str(game_manager.singleplayer_mode),
			str(game_manager.game_sim.sim_started),
			game_manager.time_attack_ghost_controller.last_error,
			game_manager.time_attack_ghost_controller.prepared_track_index,
		])
		return
	if game_manager._singleplayer_tick != cursor or practice.canonical_frame_count() != cursor:
		_fail("resumed Practice cursor/prefix mismatch")
		return
	if game_manager._local_player_id() != focus_id or game_manager.race_session_controller.local_player_index != source_ids.find(focus_id):
		_fail("focused racer did not transfer to local control")
		return
	if String(game_manager.network_manager.race_options.get("session_kind", "")) != "practice" \
			or bool(game_manager.network_manager.race_options.get("leaderboard_eligible", true)) \
			or game_manager.replay_controller.replay_recording_source != "practice" \
			or practice.game_speed() != 0.0:
		_fail("resumed Practice policy was not applied")
		return
	for index in source_ids.size():
		var controller = game_manager.race_session_controller.players[index]
		if (index == source_ids.find(focus_id)) != (controller != null):
			_fail("recorded human/CPU role conversion is incorrect at roster index %d" % index)
			return
	var resumed_transform: Transform3D = game_manager.game_sim.get_player_physical_render_transform(focus_id)
	if resumed_transform.origin.distance_to(expected_transform.origin) > 0.001:
		_fail("focused racer state changed across the replay transition")
		return
	var expected_ghosts := 1 if keep_original and !partial_end else 0
	if game_manager.time_attack_ghost_controller.runtime_slots.size() != expected_ghosts:
		_fail("original-future ghost count is incorrect")
		return
	if expected_ghosts == 1 and int((game_manager.time_attack_ghost_controller.runtime_slots[0] as Dictionary).get("frame_index", -1)) != cursor:
		_fail("original-future ghost did not synchronize to the resume cursor")
		return
	if !practice.save_selected_slot():
		_fail("resumed Practice state could not be saved")
		return
	game_manager._simulate_singleplayer_tick(PackedByteArray([0]))
	practice.capture_completed_tick(game_manager._singleplayer_tick - 1)
	if game_manager._singleplayer_tick != cursor + 1 or practice.canonical_frame_count() != cursor + 1:
		_fail("resumed Practice did not author exactly one new canonical frame")
		return
	if !practice.load_selected_slot() or game_manager._singleplayer_tick != cursor or practice.canonical_frame_count() != cursor:
		_fail("resumed Practice slot did not restore the canonical branch cursor")
		return
	if expected_ghosts == 1 and int((game_manager.time_attack_ghost_controller.runtime_slots[0] as Dictionary).get("frame_index", -1)) != cursor:
		_fail("original-future ghost did not restore with the Practice slot")
		return
	var before_samples := practice.telemetry_sample_count
	var before_formats := practice.telemetry_format_count
	practice.update(1.0)
	if practice.telemetry_sample_count != before_samples or practice.telemetry_format_count != before_formats:
		_fail("Telemetry Off sampled or formatted data")
		return
	game_manager._return_to_menu()
	if Engine.time_scale != 1.0 or Engine.physics_ticks_per_second != 60:
		_fail("Practice exit did not restore the default engine clock")
		return
	print("MXT_PRACTICE_REPLAY_RESUME_SMOKE_OK cursor=", cursor,
		" racers=", source_ids.size(),
		" original_ghost=", expected_ghosts,
		" partial_end=", partial_end)
	game_manager.queue_free()
	await process_frame
	quit(0)


func _argument_value(name: String) -> String:
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	var index := args.find(name)
	var source := args
	if index == -1:
		index = user_args.find(name)
		source = user_args
	if index == -1 or index + 1 >= source.size():
		return ""
	return String(source[index + 1])


func _fail(message: String) -> void:
	push_error("MXT_PRACTICE_REPLAY_RESUME_SMOKE_FAIL " + message)
	if game_manager != null:
		if game_manager.game_sim != null and game_manager.game_sim.sim_started:
			game_manager._return_to_menu()
		game_manager.queue_free()
	quit(1)
