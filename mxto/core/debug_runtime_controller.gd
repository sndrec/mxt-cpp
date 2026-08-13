class_name DebugRuntimeController
extends Node

const GameVersionData = preload("res://core/game_version.gd")
const TOP_GAP_COUNT := 24
const SCREENSHOT_SIZE := Vector2i(3840, 2160)
const SCREENSHOT_DIRECTORY := "user://screenshots"

enum ProfilePhase {
	INPUT,
	TICK,
	EVENTS,
	CAMERA,
	RENDER,
	AUDIO_TICK,
	NAMETAG,
	LOCAL_VISUAL,
	FINISH_CHECK,
}

var game_sim: GameSim
var server_game_sim: GameSim
var network_manager: NetworkManager
var race_presentation_controller
var car_node_container: CarNodeContainer
var frame_time_label: Label
var rtt_label: Label
var version_label: Label

var auto_accelerate := false
var bumper_smoke_enabled := false
var rail_trace_enabled := false
var render_profile_enabled := false
var disable_car_multimesh := false
var render_all_car_bodies := false
var disable_node_effects := false
var disable_thruster_lights := false
var hide_track_visuals := false
var disable_hud := false
var hide_hud_only := false
var disable_hud_process_only := false
var disable_minimap := false
var quit_after_frames := -1

var profile_frames := 0
var profile_process_frames := 0
var profile_physics_us := 0
var profile_physics_max_us := 0
var profile_process_us := 0
var profile_process_max_us := 0
var profile_visuals_only_us := 0
var profile_visuals_only_max_us := 0
var profile_phase_us := PackedInt64Array()
var profile_phase_max_us := PackedInt64Array()
var last_process_start_us := 0
var frame_gap_max_us := 0
var frame_gap_over_16ms := 0
var frame_gap_over_20ms := 0
var frame_gap_over_25ms := 0
var frame_gap_over_33ms := 0
var frame_gap_over_50ms := 0
var last_physics_sample_us := 0
var last_process_sample_us := 0
var last_pipeline_draw := -1
var last_pipeline_surface := -1
var last_pipeline_mesh := -1
var top_gap_us := PackedInt64Array()
var top_gap_tick := PackedInt64Array()
var top_gap_physics_us := PackedInt64Array()
var top_gap_process_us := PackedInt64Array()
var top_gap_pipeline_draw := PackedInt64Array()
var top_gap_pipeline_surface := PackedInt64Array()
var top_gap_pipeline_mesh := PackedInt64Array()
var top_gap_native_total_us := PackedInt64Array()
var top_gap_native_vehicle_us := PackedInt64Array()
var top_gap_native_collision_us := PackedInt64Array()
var top_gap_native_save_us := PackedInt64Array()
var top_gap_body_instances := PackedInt64Array()
var top_gap_draw_calls := PackedInt64Array()
var top_gap_primitives := PackedInt64Array()
var top_gap_engine_process_us := PackedInt64Array()
var top_gap_engine_physics_us := PackedInt64Array()
var screenshot_in_progress := false

func initialize(
	in_game_sim: GameSim,
	in_server_game_sim: GameSim,
	in_network_manager: NetworkManager,
	in_race_presentation_controller,
	in_car_node_container: CarNodeContainer,
	in_frame_time_label: Label,
	in_rtt_label: Label,
	in_version_label: Label
) -> void:
	game_sim = in_game_sim
	server_game_sim = in_server_game_sim
	network_manager = in_network_manager
	race_presentation_controller = in_race_presentation_controller
	car_node_container = in_car_node_container
	frame_time_label = in_frame_time_label
	rtt_label = in_rtt_label
	version_label = in_version_label
	version_label.text = GameVersionData.display_string()

func configure_command_line(args: Array, user_args: Array) -> void:
	auto_accelerate = _has_arg(args, user_args, "--auto-accelerate")
	bumper_smoke_enabled = _has_arg(args, user_args, "--debug-bumper-smoke")
	render_profile_enabled = _has_arg(args, user_args, "--render-profile")
	disable_car_multimesh = _has_arg(args, user_args, "--profile-disable-car-multimesh")
	render_all_car_bodies = _has_arg(args, user_args, "--render-all-car-bodies")
	disable_node_effects = _has_arg(args, user_args, "--profile-disable-node-effects")
	disable_thruster_lights = _has_arg(args, user_args, "--profile-disable-thruster-lights")
	hide_track_visuals = _has_arg(args, user_args, "--profile-hide-track-visuals")
	disable_hud = _has_arg(args, user_args, "--profile-disable-hud")
	hide_hud_only = _has_arg(args, user_args, "--profile-hide-hud-only")
	disable_hud_process_only = _has_arg(args, user_args, "--profile-disable-hud-process-only")
	disable_minimap = _has_arg(args, user_args, "--profile-disable-minimap")
	quit_after_frames = maxi(0, _read_int_arg(args, user_args, "--quit-after-frames", -1)) if _has_arg(args, user_args, "--quit-after-frames") else -1
	game_sim.set_render_profile_enabled(render_profile_enabled)
	game_sim.set_phase_profile_enabled(render_profile_enabled)
	game_sim.set_render_node_effects_enabled(!disable_node_effects)
	game_sim.set_render_thruster_lights_enabled(!disable_thruster_lights)
	game_sim.set_render_all_car_bodies(render_all_car_bodies)
	if render_profile_enabled:
		_initialize_profile_storage()
	rail_trace_enabled = _has_arg(args, user_args, "--debug-rail-trace")
	if rail_trace_enabled:
		var car_index := _read_int_arg(args, user_args, "--debug-rail-trace-car-index", -1)
		var tick_start := _read_int_arg(args, user_args, "--debug-rail-trace-from", -1)
		var tick_end := _read_int_arg(args, user_args, "--debug-rail-trace-to", -1)
		game_sim.set_dip_switch_enabled(0x40, true)
		server_game_sim.set_dip_switch_enabled(0x40, true)
		game_sim.set_rail_trace_filter(car_index, tick_start, tick_end)
		server_game_sim.set_rail_trace_filter(car_index, tick_start, tick_end)
	if _has_arg(args, user_args, "--debug-mesh-floor-trace"):
		game_sim.set_dip_switch_enabled(0x1000, true)
		server_game_sim.set_dip_switch_enabled(0x1000, true)

func apply_race_render_options(car_render_manager: CarRenderManager, local_visual_car: VisualCar) -> void:
	car_render_manager.multimesh_render_enabled = !disable_car_multimesh
	game_sim.set_render_node_effects_enabled(!disable_node_effects)
	game_sim.set_render_thruster_lights_enabled(!disable_thruster_lights)
	if local_visual_car == null:
		return
	var local_hud := local_visual_car.race_hud
	if disable_minimap:
		var minimap_control := local_hud.get_node_or_null("MinimapControl") as Control
		if minimap_control != null:
			minimap_control.visible = false
			minimap_control.process_mode = Node.PROCESS_MODE_DISABLED
		var minimap_viewport := local_hud.get_node_or_null("MinimapControl/SubViewport") as SubViewport
		if minimap_viewport != null:
			minimap_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	if disable_hud or hide_hud_only:
		local_hud.visible = false
	if disable_hud or disable_hud_process_only:
		local_hud.process_mode = Node.PROCESS_MODE_DISABLED
	if disable_hud:
		frame_time_label.visible = false
		rtt_label.visible = false

func update_labels(in_lobby: bool) -> void:
	frame_time_label.text = str(network_manager.rollback_frametime_us) + "us"
	rtt_label.text = str(roundi(network_manager.rtt_s * 1000.0)) + "ms"
	frame_time_label.visible = !in_lobby and !disable_hud
	rtt_label.visible = !in_lobby and !disable_hud
	version_label.visible = !in_lobby

func record_phase(phase: int, start_usec: int) -> void:
	var elapsed := Time.get_ticks_usec() - start_usec
	profile_phase_us[phase] += elapsed
	profile_phase_max_us[phase] = maxi(profile_phase_max_us[phase], elapsed)

func record_physics_frame(start_usec: int) -> void:
	var elapsed := Time.get_ticks_usec() - start_usec
	profile_physics_us += elapsed
	profile_physics_max_us = maxi(profile_physics_max_us, elapsed)
	last_physics_sample_us = elapsed
	profile_frames += 1

func begin_process_frame(singleplayer_tick: int) -> int:
	var start_usec := Time.get_ticks_usec()
	var pipeline_draw := int(Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_DRAW))
	var pipeline_surface := int(Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_SURFACE))
	var pipeline_mesh := int(Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_MESH))
	var draw_calls := int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
	var primitives := int(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME))
	var engine_process_us := roundi(Performance.get_monitor(Performance.TIME_PROCESS) * 1000000.0)
	var engine_physics_us := roundi(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000000.0)
	var pipeline_draw_delta := 0 if last_pipeline_draw < 0 else maxi(0, pipeline_draw - last_pipeline_draw)
	var pipeline_surface_delta := 0 if last_pipeline_surface < 0 else maxi(0, pipeline_surface - last_pipeline_surface)
	var pipeline_mesh_delta := 0 if last_pipeline_mesh < 0 else maxi(0, pipeline_mesh - last_pipeline_mesh)
	if last_process_start_us > 0 and profile_frames > 120:
		var frame_gap_us := start_usec - last_process_start_us
		frame_gap_max_us = maxi(frame_gap_max_us, frame_gap_us)
		if frame_gap_us > 16667:
			frame_gap_over_16ms += 1
			_record_profile_gap(frame_gap_us, singleplayer_tick, pipeline_draw_delta, pipeline_surface_delta, pipeline_mesh_delta, draw_calls, primitives, engine_process_us, engine_physics_us)
		if frame_gap_us > 20000: frame_gap_over_20ms += 1
		if frame_gap_us > 25000: frame_gap_over_25ms += 1
		if frame_gap_us > 33333: frame_gap_over_33ms += 1
		if frame_gap_us > 50000: frame_gap_over_50ms += 1
	last_process_start_us = start_usec
	last_pipeline_draw = pipeline_draw
	last_pipeline_surface = pipeline_surface
	last_pipeline_mesh = pipeline_mesh
	return start_usec

func record_process_frame(process_start_usec: int, visuals_start_usec: int) -> void:
	var now_usec := Time.get_ticks_usec()
	var visuals_elapsed := now_usec - visuals_start_usec
	var process_elapsed := now_usec - process_start_usec
	profile_visuals_only_us += visuals_elapsed
	profile_visuals_only_max_us = maxi(profile_visuals_only_max_us, visuals_elapsed)
	profile_process_us += process_elapsed
	profile_process_max_us = maxi(profile_process_max_us, process_elapsed)
	last_process_sample_us = process_elapsed
	profile_process_frames += 1

func print_profile_summary(singleplayer_cpu_count: int, launch_cpu_driver_count: int) -> void:
	if !render_profile_enabled or profile_frames <= 0:
		return
	var process_frames := maxi(profile_process_frames, 1)
	print("MXT_RENDER_PROFILE frames=", profile_frames,
		" process_frames=", profile_process_frames,
		" physics_us=", int(profile_physics_us / profile_frames),
		" physics_max_us=", profile_physics_max_us,
		" tick_us=", int(profile_phase_us[ProfilePhase.TICK] / profile_frames),
		" tick_max_us=", profile_phase_max_us[ProfilePhase.TICK],
		" render_us=", int(profile_phase_us[ProfilePhase.RENDER] / profile_frames),
		" render_max_us=", profile_phase_max_us[ProfilePhase.RENDER],
		" nametag_us=", int(profile_phase_us[ProfilePhase.NAMETAG] / profile_frames),
		" nametag_max_us=", profile_phase_max_us[ProfilePhase.NAMETAG],
		" local_visual_us=", int(profile_phase_us[ProfilePhase.LOCAL_VISUAL] / profile_frames),
		" local_visual_max_us=", profile_phase_max_us[ProfilePhase.LOCAL_VISUAL],
		" input_us=", int(profile_phase_us[ProfilePhase.INPUT] / profile_frames),
		" input_max_us=", profile_phase_max_us[ProfilePhase.INPUT],
		" events_us=", int(profile_phase_us[ProfilePhase.EVENTS] / profile_frames),
		" events_max_us=", profile_phase_max_us[ProfilePhase.EVENTS],
		" camera_us=", int(profile_phase_us[ProfilePhase.CAMERA] / profile_frames),
		" camera_max_us=", profile_phase_max_us[ProfilePhase.CAMERA],
		" audio_tick_us=", int(profile_phase_us[ProfilePhase.AUDIO_TICK] / profile_frames),
		" audio_tick_max_us=", profile_phase_max_us[ProfilePhase.AUDIO_TICK],
		" finish_check_us=", int(profile_phase_us[ProfilePhase.FINISH_CHECK] / profile_frames),
		" finish_check_max_us=", profile_phase_max_us[ProfilePhase.FINISH_CHECK],
		" process_us=", int(profile_process_us / process_frames),
		" process_max_us=", profile_process_max_us,
		" visuals_only_us=", int(profile_visuals_only_us / process_frames),
		" visuals_only_max_us=", profile_visuals_only_max_us,
		" frame_gap_max_us=", frame_gap_max_us,
		" frame_gap_over_16ms=", frame_gap_over_16ms,
		" frame_gap_over_20ms=", frame_gap_over_20ms,
		" frame_gap_over_25ms=", frame_gap_over_25ms,
		" frame_gap_over_33ms=", frame_gap_over_33ms,
		" frame_gap_over_50ms=", frame_gap_over_50ms)
	print("MXT_RACER_COUNTS configured_cpus=", singleplayer_cpu_count, " requested_cpus=", launch_cpu_driver_count)
	var top_gap_parts := PackedStringArray()
	for i in TOP_GAP_COUNT:
		if top_gap_us[i] <= 0:
			break
		top_gap_parts.append("%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d" % [
			top_gap_us[i], top_gap_tick[i], top_gap_physics_us[i], top_gap_process_us[i],
			top_gap_pipeline_draw[i], top_gap_pipeline_surface[i], top_gap_pipeline_mesh[i],
			top_gap_native_total_us[i], top_gap_native_vehicle_us[i], top_gap_native_collision_us[i],
			top_gap_native_save_us[i], top_gap_body_instances[i], top_gap_draw_calls[i], top_gap_primitives[i],
			top_gap_engine_process_us[i], top_gap_engine_physics_us[i]])
	print("MXT_RENDER_PROFILE_TOP_GAPS gap_us:tick:physics_us:process_us:pipeline_draw:pipeline_surface:pipeline_mesh:native_total_us:native_vehicle_us:native_collision_us:native_save_us:body_instances:draw_calls:primitives:engine_process_us:engine_physics_us=", "|".join(top_gap_parts))
	var profile_hud := race_presentation_controller.local_race_hud() as RaceHud
	if profile_hud != null:
		print(profile_hud.get_render_profile_string())
	var profile_visual_car := car_node_container.local_visual_car
	if profile_visual_car != null:
		print(profile_visual_car.get_render_profile_string())
	_print_active_script_profile_counts()
	print(game_sim.get_phase_profile_string())
	print(game_sim.get_render_profile_string())

func copy_native_profile_to_clipboard() -> void:
	DisplayServer.clipboard_set(game_sim.get_phase_profile_string() + "\n" + game_sim.get_render_profile_string())

func take_clean_4k_screenshot() -> void:
	if screenshot_in_progress or DisplayServer.get_name() == "headless":
		return
	screenshot_in_progress = true
	var viewport := get_viewport()
	var window := get_window()
	if window == null:
		screenshot_in_progress = false
		return
	var original_window_mode := window.mode
	var original_window_size := window.size
	var original_window_position := window.position
	var original_window_screen := window.current_screen
	var original_canvas_cull_mask := viewport.get_canvas_cull_mask()
	var race_hud := race_presentation_controller.local_race_hud() as RaceHud
	var race_hud_process_mode := Node.PROCESS_MODE_INHERIT
	var world_sticker_nodes: Array[Node3D] = []
	var world_sticker_visibility: Array[bool] = []
	if race_hud != null:
		race_hud_process_mode = race_hud.process_mode
		race_hud.process_mode = Node.PROCESS_MODE_DISABLED
		for sticker_node in race_hud.sticker_pool:
			if sticker_node == null or !is_instance_valid(sticker_node):
				continue
			world_sticker_nodes.append(sticker_node)
			world_sticker_visibility.append(sticker_node.visible)
			sticker_node.visible = false
	viewport.set_canvas_cull_mask(0)
	if window.mode != Window.MODE_WINDOWED:
		window.mode = Window.MODE_WINDOWED
		await get_tree().process_frame
	window.current_screen = original_window_screen
	window.size = SCREENSHOT_SIZE
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	window.current_screen = original_window_screen
	window.size = original_window_size
	window.position = original_window_position
	window.mode = original_window_mode
	await get_tree().process_frame
	await RenderingServer.frame_post_draw
	viewport.set_canvas_cull_mask(original_canvas_cull_mask)
	if is_instance_valid(race_hud):
		race_hud.process_mode = race_hud_process_mode
	for i in range(world_sticker_nodes.size()):
		if is_instance_valid(world_sticker_nodes[i]):
			world_sticker_nodes[i].visible = world_sticker_visibility[i]
	if image.is_empty() or image.get_size() != SCREENSHOT_SIZE:
		push_error("Failed to capture a 4K screenshot; rendered image size was %s" % image.get_size())
		screenshot_in_progress = false
		return
	var directory_path := ProjectSettings.globalize_path(SCREENSHOT_DIRECTORY)
	var directory_error := DirAccess.make_dir_recursive_absolute(directory_path)
	if directory_error != OK:
		push_error("Failed to create screenshot directory: %s" % directory_path)
		screenshot_in_progress = false
		return
	var now := Time.get_datetime_dict_from_system()
	var file_name := "mxt_%04d-%02d-%02d_%02d-%02d-%02d_%03d.png" % [
		int(now["year"]), int(now["month"]), int(now["day"]), int(now["hour"]),
		int(now["minute"]), int(now["second"]), Time.get_ticks_msec() % 1000]
	var screenshot_path := SCREENSHOT_DIRECTORY.path_join(file_name)
	var save_error := image.save_png(screenshot_path)
	if save_error == OK:
		print("Saved clean 4K screenshot: %s" % ProjectSettings.globalize_path(screenshot_path))
	else:
		push_error("Failed to save clean 4K screenshot: %s" % error_string(save_error))
	screenshot_in_progress = false

func _initialize_profile_storage() -> void:
	profile_phase_us = _zeroed_array(ProfilePhase.size())
	profile_phase_max_us = _zeroed_array(ProfilePhase.size())
	top_gap_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_tick = _zeroed_array(TOP_GAP_COUNT)
	top_gap_physics_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_process_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_pipeline_draw = _zeroed_array(TOP_GAP_COUNT)
	top_gap_pipeline_surface = _zeroed_array(TOP_GAP_COUNT)
	top_gap_pipeline_mesh = _zeroed_array(TOP_GAP_COUNT)
	top_gap_native_total_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_native_vehicle_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_native_collision_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_native_save_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_body_instances = _zeroed_array(TOP_GAP_COUNT)
	top_gap_draw_calls = _zeroed_array(TOP_GAP_COUNT)
	top_gap_primitives = _zeroed_array(TOP_GAP_COUNT)
	top_gap_engine_process_us = _zeroed_array(TOP_GAP_COUNT)
	top_gap_engine_physics_us = _zeroed_array(TOP_GAP_COUNT)

func _record_profile_gap(gap_us: int, singleplayer_tick: int, pipeline_draw: int, pipeline_surface: int, pipeline_mesh: int, draw_calls: int, primitives: int, engine_process_us: int, engine_physics_us: int) -> void:
	if gap_us <= top_gap_us[TOP_GAP_COUNT - 1]:
		return
	var insert_at := TOP_GAP_COUNT - 1
	while insert_at > 0 and gap_us > top_gap_us[insert_at - 1]:
		top_gap_us[insert_at] = top_gap_us[insert_at - 1]
		top_gap_tick[insert_at] = top_gap_tick[insert_at - 1]
		top_gap_physics_us[insert_at] = top_gap_physics_us[insert_at - 1]
		top_gap_process_us[insert_at] = top_gap_process_us[insert_at - 1]
		top_gap_pipeline_draw[insert_at] = top_gap_pipeline_draw[insert_at - 1]
		top_gap_pipeline_surface[insert_at] = top_gap_pipeline_surface[insert_at - 1]
		top_gap_pipeline_mesh[insert_at] = top_gap_pipeline_mesh[insert_at - 1]
		top_gap_native_total_us[insert_at] = top_gap_native_total_us[insert_at - 1]
		top_gap_native_vehicle_us[insert_at] = top_gap_native_vehicle_us[insert_at - 1]
		top_gap_native_collision_us[insert_at] = top_gap_native_collision_us[insert_at - 1]
		top_gap_native_save_us[insert_at] = top_gap_native_save_us[insert_at - 1]
		top_gap_body_instances[insert_at] = top_gap_body_instances[insert_at - 1]
		top_gap_draw_calls[insert_at] = top_gap_draw_calls[insert_at - 1]
		top_gap_primitives[insert_at] = top_gap_primitives[insert_at - 1]
		top_gap_engine_process_us[insert_at] = top_gap_engine_process_us[insert_at - 1]
		top_gap_engine_physics_us[insert_at] = top_gap_engine_physics_us[insert_at - 1]
		insert_at -= 1
	var native_phase_sample := game_sim.get_phase_profile_last_sample()
	var native_render_sample := game_sim.get_render_profile_last_sample()
	top_gap_us[insert_at] = gap_us
	top_gap_tick[insert_at] = singleplayer_tick
	top_gap_physics_us[insert_at] = last_physics_sample_us
	top_gap_process_us[insert_at] = last_process_sample_us
	top_gap_pipeline_draw[insert_at] = pipeline_draw
	top_gap_pipeline_surface[insert_at] = pipeline_surface
	top_gap_pipeline_mesh[insert_at] = pipeline_mesh
	if native_phase_sample.size() >= 9:
		top_gap_native_total_us[insert_at] = native_phase_sample[0]
		top_gap_native_vehicle_us[insert_at] = native_phase_sample[3]
		top_gap_native_collision_us[insert_at] = native_phase_sample[4]
		top_gap_native_save_us[insert_at] = native_phase_sample[8]
	if native_render_sample.size() >= 2:
		top_gap_body_instances[insert_at] = native_render_sample[0]
	top_gap_draw_calls[insert_at] = draw_calls
	top_gap_primitives[insert_at] = primitives
	top_gap_engine_process_us[insert_at] = engine_process_us
	top_gap_engine_physics_us[insert_at] = engine_physics_us

func _print_active_script_profile_counts() -> void:
	var process_counts := {}
	var physics_counts := {}
	var nodes := get_tree().root.find_children("*", "", true, false)
	nodes.append(get_tree().root)
	for node_value in nodes:
		var node := node_value as Node
		if node == null:
			continue
		var script = node.get_script()
		if script == null:
			continue
		var script_path := str(script.resource_path)
		if script_path.is_empty():
			script_path = str(script)
		if node.is_processing():
			process_counts[script_path] = int(process_counts.get(script_path, 0)) + 1
		if node.is_physics_processing():
			physics_counts[script_path] = int(physics_counts.get(script_path, 0)) + 1
	var process_parts := PackedStringArray()
	for script_path in process_counts:
		process_parts.append("%s=%d" % [script_path, process_counts[script_path]])
	process_parts.sort()
	var physics_parts := PackedStringArray()
	for script_path in physics_counts:
		physics_parts.append("%s=%d" % [script_path, physics_counts[script_path]])
	physics_parts.sort()
	print("MXT_ACTIVE_PROCESS_SCRIPTS ", "|".join(process_parts))
	print("MXT_ACTIVE_PHYSICS_SCRIPTS ", "|".join(physics_parts))

func _has_arg(args: Array, user_args: Array, flag: String) -> bool:
	return args.has(flag) or user_args.has(flag)

func _zeroed_array(size: int) -> PackedInt64Array:
	var values := PackedInt64Array()
	values.resize(size)
	values.fill(0)
	return values

func _read_int_arg(args: Array, user_args: Array, flag: String, default_value: int) -> int:
	var index := args.find(flag)
	var source_args := args
	if index == -1:
		index = user_args.find(flag)
		source_args = user_args
	return int(source_args[index + 1]) if index >= 0 and index + 1 < source_args.size() else default_value
