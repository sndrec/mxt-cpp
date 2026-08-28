extends SceneTree

const PlayerInputClass = preload("res://player/player_input.gd")

var game_manager: GameManager


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("main scene could not be loaded")
		return
	game_manager = packed.instantiate() as GameManager
	root.add_child(game_manager)
	game_manager.set_physics_process(false)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
	var finite := _options(99)
	game_manager._start_singleplayer_race(false, finite)
	var practice := game_manager.practice_controller
	if !practice.session_active or !practice.timeline_enabled \
			or game_manager.game_sim.get_target_lap_count() != 99 \
			or !game_manager.game_sim.get_boost_unlocked_from_start() \
			or !game_manager.replay_recorder.active \
			or game_manager.replay_recorder.source != "practice":
		_fail("finite Practice did not start with its exact policy")
		return
	practice._set_game_speed_index(10)
	if practice.game_speed() != 0.5 or Engine.time_scale != 0.5 or Engine.physics_ticks_per_second != 30:
		_fail("0.50x Practice clock is incorrect")
		return
	practice._set_game_speed_index(0)
	if Engine.time_scale != 0.0 or Engine.physics_ticks_per_second != 60:
		_fail("0.00x Practice clock is incorrect")
		return
	practice.input_editor.step_button.pressed.emit()
	if !practice.consume_frame_advance() or practice.consume_frame_advance():
		_fail("Exact Input Step Frame did not request exactly one frame")
		return
	var stick := InputEventJoypadMotion.new()
	stick.axis = JOY_AXIS_RIGHT_X
	stick.axis_value = 0.8
	if !practice.handle_runtime_input(stick) or !practice.consume_frame_advance() or practice.consume_frame_advance():
		_fail("frame advance was not edge-latched to one request")
		return
	stick.axis_value = 0.0
	practice.handle_runtime_input(stick)
	stick.axis_value = 0.8
	if !practice.handle_runtime_input(stick) or !practice.consume_frame_advance():
		_fail("frame advance did not re-arm after stick release")
		return
	for _tick in range(50):
		_advance(PackedByteArray([0]))
	if game_manager._singleplayer_tick != 50 or practice.canonical_frame_count() != 50:
		_fail("finite Practice did not commit one canonical frame per tick")
		return
	if practice.input_editor.rewind_button.disabled:
		_fail("Exact Input Rewind Frame did not enable when history became available")
		return
	practice.input_editor.rewind_button.pressed.emit()
	if !practice.consume_frame_rewind() or game_manager._singleplayer_tick != 49:
		_fail("Exact Input Rewind Frame did not restore exactly one authored frame")
		return
	_advance(PackedByteArray([0]))
	if game_manager._singleplayer_tick != 50 or practice.canonical_frame_count() != 50:
		_fail("frame-step continuation after Exact Input rewind did not replace the canonical future")
		return
	var export_dir := ProjectSettings.globalize_path("user://practice_smoke_exports")
	var first_export := game_manager.replay_recorder._write("practice_smoke", export_dir)
	var second_export := game_manager.replay_recorder._write("practice_smoke", export_dir)
	if first_export.is_empty() or second_export.is_empty() or first_export == second_export \
			or !FileAccess.file_exists(first_export) or !FileAccess.file_exists(second_export) \
			or practice.canonical_frame_count() != 50 or !game_manager.replay_recorder.active:
		_fail("repeatable partial export mutated the live canonical timeline")
		return
	DirAccess.remove_absolute(first_export)
	DirAccess.remove_absolute(second_export)
	DirAccess.remove_absolute(export_dir)
	if !practice.save_selected_slot():
		_fail("finite Practice could not retain the first branch")
		return
	var rewind_count := 0
	while practice.rewind_one_frame():
		rewind_count += 1
	if rewind_count != 44 or game_manager._singleplayer_tick != 6 or practice.rewind_one_frame():
		_fail("45-record rewind boundary accepted stale modulo state count=%d tick=%d" % [rewind_count, game_manager._singleplayer_tick])
		return
	if !practice.load_selected_slot() or game_manager._singleplayer_tick != 50 or practice.canonical_frame_count() != 50:
		_fail("explicit slot did not restore the retained canonical branch")
		return
	_advance(PackedByteArray([0]))
	_advance(PackedByteArray([0]))
	if !practice.rewind_one_frame() or game_manager._singleplayer_tick != 51:
		_fail("rewind after slot restore did not load exactly one prior authored frame")
		return
	var accelerated := PlayerInputClass.new()
	accelerated.accelerate = 1.0
	_advance(accelerated.serialize())
	var branch_stream: MxtReplayStream = practice.canonical_stream()
	var branch_frame := branch_stream.read_frame(51) if branch_stream != null else {}
	if practice.canonical_frame_count() != 52 or branch_frame.get("inputs", {}).is_empty():
		_fail("new input did not replace the discarded canonical future")
		return
	if !practice.load_selected_slot() or practice.canonical_frame_count() != 50:
		_fail("retained original branch was mutated by alternate extension")
		return
	var before_samples := practice.telemetry_sample_count
	var before_formats := practice.telemetry_format_count
	practice.update(1.0)
	if practice.telemetry_sample_count != before_samples or practice.telemetry_format_count != before_formats:
		_fail("Telemetry Off performed visible telemetry work")
		return
	game_manager._return_to_menu()
	if Engine.time_scale != 1.0 or Engine.physics_ticks_per_second != 60:
		_fail("finite Practice teardown did not restore the engine clock")
		return
	var infinite := _options(0)
	game_manager._start_singleplayer_race(false, infinite)
	practice = game_manager.practice_controller
	if !practice.session_active or practice.timeline_enabled \
			or game_manager.game_sim.get_target_lap_count() != 0 \
			or game_manager.replay_recorder.active:
		_fail("Infinite Practice retained replay state or lost its lap policy")
		return
	for _tick in range(120):
		_advance(PackedByteArray([0]))
	if practice.canonical_frame_count() != 0 or practice.session_completed:
		_fail("Infinite Practice recorded frames or completed")
		return
	game_manager._return_to_menu()
	print("MXT_PRACTICE_RUNTIME_SMOKE_OK rewind_frames=", rewind_count)
	game_manager.queue_free()
	await process_frame
	quit(0)


func _advance(input_bytes: PackedByteArray) -> void:
	game_manager._simulate_singleplayer_tick(input_bytes)
	game_manager.practice_controller.capture_completed_tick(game_manager._singleplayer_tick - 1)


func _options(laps: int) -> MxtRaceConfiguration:
	var configuration := MxtRaceConfiguration.new()
	configuration.session_kind = MxtRaceConfiguration.SESSION_PRACTICE
	configuration.vehicle_restore = true
	configuration.bumpers = false
	configuration.s_boost = true
	configuration.boost_unlocked_from_start = true
	configuration.cpu_count = 0
	configuration.lap_count = laps
	configuration.leaderboard_eligible = false
	configuration.leaderboard_ineligible_reason = "practice"
	return configuration


func _fail(message: String) -> void:
	push_error("MXT_PRACTICE_RUNTIME_SMOKE_FAIL " + message)
	if game_manager != null:
		if game_manager.game_sim != null and game_manager.game_sim.sim_started:
			game_manager._return_to_menu()
		game_manager.queue_free()
	quit(1)
