class_name RaceSessionController
extends Node

const BUMPER_DEFINITION_PATH := "res://vehicle/asset/bumper/definition.tres"
const BUMPER_POOL_SIZE := 60
const SpectatorControllerClass = preload("res://ui/spectator_controller.gd")
const RacePresentationControllerClass = preload("res://ui/race_presentation_controller.gd")
const DebugRuntimeControllerClass = preload("res://core/debug_runtime_controller.gd")
const TRIGGER_SCENES = {
	0: preload("res://asset/obj_dashplate.tscn"),
	1: preload("res://asset/obj_jumpplate.tscn"),
	2: preload("res://asset/obj_mine.tscn"),
}

var game_root: Node
var game_sim: GameSim
var server_game_sim: GameSim
var network_manager: NetworkManager
var replay_controller
var race_audio_controller: RaceAudioController
var track_content_controller: TrackContentController
var lobby_chibi_controller: LobbyChibiController
var spectator_controller: SpectatorControllerClass
var race_presentation_controller: RacePresentationControllerClass
var vehicle_content_controller: VehicleContentController
var debug_runtime_controller: DebugRuntimeControllerClass
var car_node_container: CarNodeContainer
var spark_node_container: SuperSparkContainer
var object_container: Node3D
var car_render_manager: CarRenderManager
var main_control: Control
var lobby_control: Control
var player_scene: PackedScene

var players: Array = []
var trigger_objects: Array[Node3D] = []
var local_player_index := 0
var current_singleplayer_mode := false
var last_race_track_index := -1
var last_race_settings: Array = []

func initialize(
	in_game_root: Node,
	in_game_sim: GameSim,
	in_server_game_sim: GameSim,
	in_network_manager: NetworkManager,
	in_replay_controller,
	in_race_audio_controller: RaceAudioController,
	in_track_content_controller: TrackContentController,
	in_lobby_chibi_controller: LobbyChibiController,
	in_spectator_controller: SpectatorControllerClass,
	in_race_presentation_controller: RacePresentationControllerClass,
	in_vehicle_content_controller: VehicleContentController,
	in_debug_runtime_controller: DebugRuntimeControllerClass,
	in_car_node_container: CarNodeContainer,
	in_spark_node_container: SuperSparkContainer,
	in_object_container: Node3D,
	in_car_render_manager: CarRenderManager,
	in_main_control: Control,
	in_lobby_control: Control,
	in_player_scene: PackedScene
) -> void:
	game_root = in_game_root
	game_sim = in_game_sim
	server_game_sim = in_server_game_sim
	network_manager = in_network_manager
	replay_controller = in_replay_controller
	race_audio_controller = in_race_audio_controller
	track_content_controller = in_track_content_controller
	lobby_chibi_controller = in_lobby_chibi_controller
	spectator_controller = in_spectator_controller
	race_presentation_controller = in_race_presentation_controller
	vehicle_content_controller = in_vehicle_content_controller
	debug_runtime_controller = in_debug_runtime_controller
	car_node_container = in_car_node_container
	spark_node_container = in_spark_node_container
	object_container = in_object_container
	car_render_manager = in_car_render_manager
	main_control = in_main_control
	lobby_control = in_lobby_control
	player_scene = in_player_scene

func start_race(track_index: int, settings: Array, singleplayer_mode: bool, headless_mode: bool) -> bool:
	if track_index < 0 or track_index >= track_content_controller.tracks.size():
		return false
	current_singleplayer_mode = singleplayer_mode
	main_control.visible = false
	lobby_control.visible = false
	lobby_chibi_controller.clear()
	last_race_track_index = track_index
	last_race_settings = settings.duplicate(true)
	race_presentation_controller.reset()
	spectator_controller.reset()
	var track_info: Dictionary = track_content_controller.tracks[track_index]
	if !track_content_controller.prepare_race(track_index):
		return false
	race_audio_controller.reset_for_race()
	race_audio_controller.configure_track_music(
		track_content_controller.current_track_dir,
		track_content_controller.current_metadata)

	var chosen_definitions: Array = []
	var racer_settings: Array = []
	var racer_ids: Array = []
	var racer_cpu_flags: Array = []
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.lobby_settings.get_cpu_roster()
	var keyed_settings := {}
	var ordered_settings := []
	for raw_settings in settings:
		if typeof(raw_settings) != TYPE_DICTIONARY:
			continue
		var settings_dictionary: Dictionary = raw_settings
		if settings_dictionary.has("_race_player_id"):
			keyed_settings[int(settings_dictionary["_race_player_id"])] = settings_dictionary
		else:
			ordered_settings.append(settings_dictionary)
	if !keyed_settings.is_empty():
		for id_value in roster:
			var player_id := int(id_value)
			if !keyed_settings.has(player_id):
				continue
			if _append_racer(keyed_settings[player_id], player_id, true, cpu_ids.has(player_id), chosen_definitions, racer_settings, racer_ids, racer_cpu_flags) < 0:
				return false
	else:
		var racer_roster_index := 0
		for settings_dictionary in ordered_settings:
			var has_roster_id := racer_roster_index < roster.size()
			var player_id := int(roster[racer_roster_index]) if has_roster_id else 0
			var append_result := _append_racer(settings_dictionary, player_id, has_roster_id, cpu_ids.has(player_id), chosen_definitions, racer_settings, racer_ids, racer_cpu_flags)
			if append_result < 0:
				return false
			if append_result > 0:
				racer_roster_index += 1

	var bumpers_enabled := bool(network_manager.race_options.get("bumpers", false))
	var custom_stamp_render := vehicle_content_controller.prepare_custom_stamp_render_payload(racer_ids, racer_settings, "race")
	var custom_stamp_atlas: Texture2D = custom_stamp_render.get("texture", null)
	var bumper_definition: CarDefinition = load(BUMPER_DEFINITION_PATH) if bumpers_enabled else null
	var render_definitions := chosen_definitions.duplicate()
	var render_settings: Array = custom_stamp_render.get("settings", racer_settings.duplicate())
	if bumper_definition != null:
		for _slot in BUMPER_POOL_SIZE:
			render_definitions.append(bumper_definition)
			render_settings.append({})
	var local_player_id := _local_player_id()
	local_player_index = racer_ids.find(local_player_id)
	var start_grid_slots: PackedInt32Array = replay_controller.replay_start_grid_slots if replay_controller.replay_playback_active and replay_controller.replay_start_grid_slots.size() == racer_ids.size() else _build_start_grid_slots(racer_ids, singleplayer_mode)
	var visual_focus_id := local_player_id
	if local_player_index == -1 and !racer_ids.is_empty():
		visual_focus_id = int(racer_ids[0])
	car_node_container.instantiate_cars(chosen_definitions, racer_ids, visual_focus_id)
	var nametag_names: Array[String] = []
	nametag_names.resize(racer_settings.size())
	for index in racer_settings.size():
		nametag_names[index] = " " + racer_settings[index].username + " "
	for car: VisualCar in car_node_container.get_children():
		car.game_manager = game_root
		car.render_profile_enabled = debug_runtime_controller.render_profile_enabled
	if car_node_container.local_visual_car != null:
		var visual_player_index := racer_ids.find(car_node_container.local_visual_car.owning_id)
		if visual_player_index >= 0 and visual_player_index < racer_settings.size():
			car_node_container.local_visual_car.player_settings = racer_settings[visual_player_index]
			if is_instance_valid(car_node_container.local_visual_car.name_label):
				car_node_container.local_visual_car.name_label.text = nametag_names[visual_player_index]
	debug_runtime_controller.apply_race_render_options(car_render_manager, car_node_container.local_visual_car)
	car_render_manager.set_custom_stamp_atlas(custom_stamp_atlas)
	car_render_manager.configure(render_definitions, car_node_container.get_children(), render_settings)
	_clear_players()
	spectator_controller.configure_race(local_player_id, local_player_index != -1)
	var car_properties: Array = []
	var acceleration_settings: Array = []
	for index in racer_settings.size():
		if index < racer_cpu_flags.size() and racer_cpu_flags[index]:
			players.append(null)
			continue
		var player_controller := player_scene.instantiate()
		player_controller.car_definition = chosen_definitions[index]
		player_controller.accel_setting = racer_settings[index].accel_setting
		player_controller.player_settings = racer_settings[index]
		game_root.add_child(player_controller)
		players.append(player_controller)
	for index in chosen_definitions.size():
		var definition: CarDefinition = chosen_definitions[index]
		car_properties.append(FileAccess.get_file_as_bytes(definition.properties_path))
		acceleration_settings.append(racer_settings[index].accel_setting if index < racer_settings.size() else 1.0)
	var level_buffer := StreamPeerBuffer.new()
	level_buffer.data_array = FileAccess.get_file_as_bytes(track_info["mxt"])
	_configure_game_sim(game_sim, level_buffer, car_properties, acceleration_settings, racer_ids, racer_cpu_flags, start_grid_slots, bumpers_enabled, bumper_definition, singleplayer_mode)
	race_audio_controller.configure_vehicle_properties(chosen_definitions)
	network_manager.netcode_session.configure(racer_ids, racer_cpu_flags, local_player_id)
	replay_controller.start_recording(track_index, settings, racer_ids, racer_cpu_flags, start_grid_slots)
	if car_node_container.local_visual_car != null:
		game_sim.set_gameplay_camera(car_node_container.local_visual_car.car_camera, car_node_container.local_visual_car.owning_id)
	race_presentation_controller.configure_race(local_player_id, local_player_index, singleplayer_mode, nametag_names)
	if network_manager.is_server:
		_configure_game_sim(server_game_sim, level_buffer, car_properties, acceleration_settings, racer_ids, racer_cpu_flags, start_grid_slots, bumpers_enabled, bumper_definition, singleplayer_mode)
		network_manager.server_netcode_session.configure(racer_ids, racer_cpu_flags, local_player_id)
	network_manager.game_sim = game_sim
	if network_manager.is_server:
		network_manager.server_game_sim = server_game_sim
	network_manager.refresh_race_admission_context()
	if !headless_mode:
		track_content_controller.load_runtime_visuals()
		_clear_triggers()
		for trigger in _parse_level_triggers(level_buffer.data_array):
			var trigger_scene: PackedScene = TRIGGER_SCENES.get(trigger["type"], null)
			if trigger_scene != null:
				var instance := trigger_scene.instantiate() as Node3D
				instance.transform = trigger["transform"]
				object_container.add_child(instance)
				trigger_objects.append(instance)
	return true

func begin_transition(singleplayer_mode: bool, audio_fade_seconds := 0.0) -> void:
	race_audio_controller.leave_race(audio_fade_seconds)
	replay_controller.reset_for_transition(network_manager.is_server and !singleplayer_mode)

func destroy_world(disconnect_network: bool, clear_client_sim_reference: bool) -> void:
	race_presentation_controller.reset()
	var was_server := network_manager.is_server
	if disconnect_network:
		network_manager.disconnect_from_server()
	game_sim.destroy_gamesim()
	if was_server:
		server_game_sim.destroy_gamesim()
	if clear_client_sim_reference:
		network_manager.game_sim = null
	network_manager.server_game_sim = null
	network_manager.refresh_race_admission_context()
	track_content_controller.teardown_runtime()
	for child in car_node_container.get_children():
		if child != null:
			child.queue_free()
	_clear_triggers()
	_clear_players()
	spectator_controller.reset()
	Engine.physics_ticks_per_second = 60
	local_player_index = 0

func _append_racer(settings_dictionary: Dictionary, player_id: int, has_player_id: bool, is_cpu: bool, chosen_definitions: Array, racer_settings: Array, racer_ids: Array, racer_cpu_flags: Array) -> int:
	var player_settings := PlayerSettings.new()
	player_settings.from_dict(settings_dictionary)
	if player_settings.spectator:
		return 0
	if !vehicle_content_controller.evidence_matches(player_settings):
		push_error("Race vehicle content evidence mismatch: %s" % player_settings.vehicle_content_id)
		return -1
	var definition := vehicle_content_controller.get_definition(player_settings.vehicle_content_id)
	if definition == null:
		push_error("Race references unavailable vehicle content: %s" % player_settings.vehicle_content_id)
		return -1
	chosen_definitions.append(definition)
	racer_settings.append(player_settings)
	if has_player_id:
		racer_ids.append(player_id)
		racer_cpu_flags.append(is_cpu)
	return 1

func _configure_game_sim(sim: GameSim, level_buffer: StreamPeerBuffer, car_properties: Array, acceleration_settings: Array, racer_ids: Array, racer_cpu_flags: Array, start_grid_slots: PackedInt32Array, bumpers_enabled: bool, bumper_definition: CarDefinition, singleplayer_mode: bool) -> void:
	sim.car_node_container = car_node_container
	sim.spark_node_container = spark_node_container
	sim.set_car_render_manager(car_render_manager)
	sim.set_spawn_seed(network_manager.spawn_seed)
	sim.set_start_grid_slots(start_grid_slots)
	sim.set_vehicle_restore_enabled(network_manager.is_vehicle_restore_enabled())
	if sim.has_method("set_bumpers_enabled"):
		sim.set_bumpers_enabled(bumpers_enabled and bumper_definition != null)
	if sim.has_method("set_s_boost_enabled"):
		sim.set_s_boost_enabled(network_manager.is_s_boost_enabled())
	if sim.has_method("set_multiplayer_intro_camera_enabled"):
		sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode or replay_controller.replay_playback_use_multiplayer_startup)
	sim.instantiate_gamesim(level_buffer.duplicate(), car_properties.duplicate(true), acceleration_settings)
	sim.set_player_metadata(racer_ids, racer_cpu_flags)
	_apply_grand_prix_ko_energy_bonuses(sim, racer_ids)

func _build_start_grid_slots(racer_ids: Array, singleplayer_mode: bool) -> PackedInt32Array:
	var slots := PackedInt32Array()
	slots.resize(racer_ids.size())
	slots.fill(-1)
	if singleplayer_mode and !replay_controller.replay_playback_use_multiplayer_startup and !network_manager.lobby_settings.get_cpu_roster().is_empty():
		var local_index := racer_ids.find(_local_player_id())
		if local_index >= 0 and racer_ids.size() > 1:
			var next_slot := 0
			for index in racer_ids.size():
				if index == local_index:
					continue
				slots[index] = next_slot
				next_slot += 1
			slots[local_index] = racer_ids.size() - 1
			return slots
	if !network_manager.is_grand_prix_enabled() or int(network_manager.race_options.get("grand_prix_current_track", 0)) <= 0:
		return slots
	var points: Dictionary = network_manager.race_options.get("grand_prix_points", {})
	var standings := []
	for index in racer_ids.size():
		var player_id := int(racer_ids[index])
		standings.append([int(_lookup_id_value(points, player_id, 0)), index])
	standings.sort_custom(func(a, b): return int(a[0]) > int(b[0]) if int(a[0]) != int(b[0]) else int(a[1]) < int(b[1]))
	for rank in standings.size():
		slots[int(standings[rank][1])] = racer_ids.size() - 1 - rank
	return slots

func _apply_grand_prix_ko_energy_bonuses(sim: GameSim, racer_ids: Array) -> void:
	if sim == null or !network_manager.is_grand_prix_enabled() or !sim.has_method("set_player_ko_energy_bonus"):
		return
	var bonuses: Dictionary = network_manager.race_options.get("grand_prix_ko_energy_bonuses", {})
	for id_value in racer_ids:
		var player_id := int(id_value)
		var bonus := float(_lookup_id_value(bonuses, player_id, 0.0))
		if bonus > 0.0:
			sim.set_player_ko_energy_bonus(player_id, bonus)

func _parse_level_triggers(bytes: PackedByteArray) -> Array:
	var buffer := StreamPeerBuffer.new()
	buffer.data_array = bytes
	buffer.big_endian = false
	buffer.get_u32()
	var version := buffer.get_string(4)
	if version != "v0.9":
		push_error("MXT track format hard-cutover failure: expected v0.9, got %s" % version)
		return []
	var checkpoint_count := buffer.get_u32()
	var segment_count := buffer.get_u32()
	var trigger_count := buffer.get_u32()
	var mesh_collision_triangle_count := buffer.get_u32()
	for _checkpoint in checkpoint_count:
		for _value in 6: buffer.get_float()
		for _value in 18: buffer.get_float()
		for _value in 7: buffer.get_float()
		buffer.get_u32()
		for _value in 3: buffer.get_float()
		buffer.get_float()
		for _value in 3: buffer.get_float()
		buffer.get_float()
		var connection_count := buffer.get_u32()
		for _connection in connection_count: buffer.get_u32()
	var skip_curve := func():
		var point_count := buffer.get_u32()
		buffer.seek(buffer.get_position() + point_count * 16)
	for _segment in segment_count:
		buffer.get_u32()
		var road_type := buffer.get_u32()
		buffer.get_u32()
		if road_type == 5 or road_type == 6:
			skip_curve.call(); skip_curve.call(); skip_curve.call()
		if road_type == 2 or road_type == 4 or road_type == 6: skip_curve.call()
		if road_type == 6: skip_curve.call()
		for _modulation in buffer.get_u32():
			skip_curve.call(); skip_curve.call()
		for _embed in buffer.get_u32():
			buffer.get_float(); buffer.get_float(); buffer.get_u32(); skip_curve.call(); skip_curve.call()
		for _curve in 3: skip_curve.call()
		for _curve in 9: skip_curve.call()
		for _curve in 3: skip_curve.call()
		for _value in 6: buffer.get_float()
	var triggers := []
	for _trigger in trigger_count:
		var trigger_type := buffer.get_u32()
		buffer.get_u32(); buffer.get_u32()
		var basis := Basis()
		basis.x = Vector3(buffer.get_float(), buffer.get_float(), buffer.get_float())
		basis.y = Vector3(buffer.get_float(), buffer.get_float(), buffer.get_float())
		basis.z = Vector3(buffer.get_float(), buffer.get_float(), buffer.get_float())
		var origin := Vector3(buffer.get_float(), buffer.get_float(), buffer.get_float())
		var transform := Transform3D(basis, origin).affine_inverse()
		var extents := Vector3(buffer.get_float(), buffer.get_float(), buffer.get_float())
		triggers.append({"type": trigger_type, "transform": transform, "extents": extents})
	if mesh_collision_triangle_count > 0:
		buffer.seek(buffer.get_position() + mesh_collision_triangle_count * (4 + 18 * 4))
	return triggers

func _clear_players() -> void:
	for player in players:
		if player != null:
			player.queue_free()
	players.clear()

func _clear_triggers() -> void:
	for trigger in trigger_objects:
		if trigger != null:
			trigger.queue_free()
	trigger_objects.clear()

func _local_player_id() -> int:
	if replay_controller.replay_playback_active:
		return replay_controller.replay_playback_local_player_id
	if current_singleplayer_mode:
		return 0
	if network_manager.has_network_peer():
		return multiplayer.get_unique_id()
	return 0

func _lookup_id_value(source: Dictionary, player_id: int, fallback):
	if source.has(player_id):
		return source[player_id]
	var string_id := str(player_id)
	return source[string_id] if source.has(string_id) else fallback
