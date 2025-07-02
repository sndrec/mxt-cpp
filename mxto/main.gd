class_name GameManager extends Node

@onready var game_sim: GameSim = $GameSim
@onready var server_game_sim: GameSim = $ServerGameSim
@onready var start_button: Button = $Control/StartButton
@onready var join_button: Button = $Control/JoinButton
@onready var ip_field: LineEdit = $Control/IPField
@onready var track_selector: OptionButton = $Control/TrackSelector
@onready var lobby_control: Control = $Lobby
@onready var lobby_track_selector: OptionButton = $Lobby/LobbyTrackSelector
@onready var start_race_button: Button = $Lobby/StartRaceButton
@onready var player_list: ItemList = $Lobby/PlayerList
@onready var car_node_container: CarNodeContainer = $GameWorld/CarNodeContainer
@onready var debug_track_mesh: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/DebugTrackMesh
@onready var network_manager: NetworkManager = $NetworkManager
@onready var car_settings: Control = $CarSettings
@onready var car_settings_button: Button = $Control/CarSettingsButton
@onready var car_settings_button_lobby: Button = $Lobby/CarSettingsButton
@onready var race_finish_label: Label = $RaceFinishLabel
@onready var frame_time_label: Label = $FrameTimeLabel

const PlayerInputClass = preload("res://player/player_input.gd")

var tracks: Array = []
var car_definitions: Array = []
var players: Array = []
var player_scene := preload("res://player/player_controller.tscn")
var local_player_index: int = 0

func _ready() -> void:
	_load_tracks()
	_load_car_definitions()
	network_manager.race_started.connect(_on_network_race_started)
	network_manager.race_finished.connect(_on_network_race_finished)
	car_settings.hide()
	car_settings_button.pressed.connect(_on_car_settings_button_pressed)
	car_settings_button_lobby.pressed.connect(_on_car_settings_button_pressed)

func _load_tracks() -> void:
	tracks.clear()
	track_selector.clear()
	lobby_track_selector.clear()
	_scan_dir("res://track")
	for t in tracks:
		track_selector.add_item(t["name"])
		lobby_track_selector.add_item(t["name"])
	if tracks.size() > 0:
		track_selector.selected = 0
		lobby_track_selector.selected = 0

func _scan_dir(path: String) -> void:
	var dir := DirAccess.open(path)
	if dir == null:
		return
	dir.list_dir_begin()
	var file := dir.get_next()
	while file != "":
		if dir.current_is_dir() and !file.begins_with("."):
			_scan_dir(path + "/" + file)
		elif file.get_extension() == "json":
			var json_path := path + "/" + file
			var mxt_path := json_path.get_basename() + ".mxt_track"
			if FileAccess.file_exists(mxt_path):
				var json_data := FileAccess.get_file_as_string(json_path)
				var parsed = JSON.parse_string(json_data)
				if typeof(parsed) == TYPE_DICTIONARY and parsed.has("name"):
					tracks.append({"name": parsed["name"], "mxt": mxt_path})
		file = dir.get_next()
	dir.list_dir_end()

func _load_car_definitions() -> void:
	car_definitions.clear()
	var dir := DirAccess.open("res://vehicle/asset")
	if dir == null:
		return
	dir.list_dir_begin()
	var folder := dir.get_next()
	while folder != "":
		if dir.current_is_dir() and !folder.begins_with("."):
			var def_path := "res://vehicle/asset/%s/definition.tres" % folder
			if ResourceLoader.exists(def_path):
				var def_res := load(def_path)
				if def_res != null:
					car_definitions.append(def_res)
		folder = dir.get_next()
	dir.list_dir_end()

func _on_start_button_pressed() -> void:
	network_manager.host()
	network_manager.send_player_settings(car_settings.get_player_settings().to_dict())
	start_race_button.disabled = false
	$Control.visible = false
	lobby_control.visible = true

func _on_join_button_pressed() -> void:
	var settings_dict = car_settings.get_player_settings().to_dict()
	network_manager.multiplayer.connected_to_server.connect(
		func():
			network_manager.send_player_settings(settings_dict),
		Object.CONNECT_ONE_SHOT)
	network_manager.join(ip_field.text)
	start_race_button.disabled = true
	$Control.visible = false
	lobby_control.visible = true

func _on_car_settings_button_pressed() -> void:
				car_settings.call("open_settings")

func _start_race(track_index: int, settings: Array) -> void:
	if track_index < 0 or track_index >= tracks.size():
		return
	var info : Dictionary = tracks[track_index]
	var chosen_defs : Array = []
	var parsed_settings : Array = []
	for d in settings:
		if typeof(d) == TYPE_DICTIONARY:
			var ps := PlayerSettings.new()
			ps.from_dict(d)
			parsed_settings.append(ps)
			var def_res := load(ps.car_definition_path)
			if def_res != null:
				chosen_defs.append(def_res)
	local_player_index = network_manager.player_ids.find(multiplayer.get_unique_id())
	if local_player_index == -1:
		local_player_index = 0
	car_node_container.instantiate_cars(chosen_defs, network_manager.player_ids, local_player_index)
	var idx := 0
	for car:VisualCar in car_node_container.get_children():
		car.game_manager = self
		if idx < parsed_settings.size():
			car.player_settings = parsed_settings[idx]
		idx += 1
	for p in players:
		p.queue_free()
	players.clear()
	var car_props : Array = []
	var accel_settings_arr : Array = []
	for i in parsed_settings.size():
		var pc := player_scene.instantiate()
		pc.car_definition = chosen_defs[i]
		pc.accel_setting = parsed_settings[i].accel_setting
		pc.player_settings = parsed_settings[i]
		add_child(pc)
		players.append(pc)
	for n in chosen_defs.size():
		var def = chosen_defs[n]
		var bytes := FileAccess.get_file_as_bytes(def.car_definition)
		car_props.append(bytes)
		if n < parsed_settings.size():
			accel_settings_arr.append(parsed_settings[n].accel_setting)
		else:
			accel_settings_arr.append(1.0)
	var level_buffer := StreamPeerBuffer.new()
	level_buffer.data_array = FileAccess.get_file_as_bytes(info["mxt"])
	game_sim.car_node_container = car_node_container
	game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
	if network_manager.is_server:
		server_game_sim.car_node_container = car_node_container
		server_game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
	network_manager.game_sim = game_sim
	if network_manager.is_server:
		network_manager.server_game_sim = server_game_sim
	var obj_path = info["mxt"].get_basename() + ".obj"
	if ResourceLoader.exists(obj_path):
		debug_track_mesh.mesh = load(obj_path)
		lobby_control.visible = false
		for i in debug_track_mesh.mesh.get_surface_count():
			var mat := debug_track_mesh.mesh.surface_get_material(i)
			if mat.resource_name == "track_surface":
				debug_track_mesh.mesh.surface_set_material(i, preload("res://asset/debug_track_mat.tres"))

func _on_start_race_button_pressed() -> void:
	if network_manager.is_server:
		var settings_array : Array = []
		for id in network_manager.player_ids:
			var ps = network_manager.player_settings.get(id, null)
			if ps == null:
				var def_path = car_definitions[randi() % car_definitions.size()].resource_path
				ps = {"car_definition_path": def_path, "accel_setting": 1.0, "username": str(id)}
			settings_array.append(ps)
		network_manager.send_start_race(lobby_track_selector.selected, settings_array)

func _on_network_race_started(track_index: int, settings: Array) -> void:
	_start_race(track_index, settings)

func _on_network_race_finished() -> void:
	race_finish_label.visible = false
	_return_to_lobby()

func _update_player_list() -> void:
	player_list.clear()
	for id in network_manager.player_ids:
		var name := str(id)
		if network_manager.player_settings.has(id):
			var ps = network_manager.player_settings[id]
			if typeof(ps) == TYPE_DICTIONARY and ps.has("username"):
				name = ps["username"]
		player_list.add_item(name)

func _physics_process(delta: float) -> void:
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if lobby_control.visible:
		_update_player_list()
	if game_sim.sim_started:
		var local_pi := PlayerInputClass.new()
		if players.size() > local_player_index:
			local_pi = players[local_player_index].get_input()
		var input_bytes := local_pi.serialize()
		network_manager.set_local_input(input_bytes)
		if network_manager.is_server:
			_simulate_host_frame()
		else:
			_simulate_single_tick()
		game_sim.render_gamesim()
		_check_race_finished()

func _simulate_host_frame():
	var loops := 0
	const MAX_TICKS_PER_FRAME := 120
	var local_pi := PlayerInputClass.new()
	while loops < MAX_TICKS_PER_FRAME:
		if players.size() > local_player_index:
			local_pi = players[local_player_index].get_input()
		var input_bytes := local_pi.serialize()
		network_manager.set_local_input(input_bytes)
		var server_inputs := network_manager.collect_server_inputs()
		if server_inputs.is_empty():
			break
		server_game_sim.tick_gamesim(server_inputs)
		network_manager.post_tick()
		loops += 1
	var client_inputs := network_manager.collect_client_inputs()
	if !client_inputs.is_empty():
		game_sim.tick_gamesim(client_inputs)

func _simulate_single_tick():
	var frame_inputs := network_manager.collect_client_inputs()
	if frame_inputs.is_empty():
		return
	game_sim.tick_gamesim(frame_inputs)
	if network_manager.is_server:
		var server_inputs := network_manager.collect_server_inputs()
		if !server_inputs.is_empty():
			server_game_sim.tick_gamesim(server_inputs)
			network_manager.post_tick()
	else:
		network_manager.post_tick()

func _unhandled_input(event: InputEvent) -> void:
	if game_sim.sim_started and event.is_action_pressed("ui_cancel"):
		_return_to_menu()

func _return_to_menu() -> void:
	network_manager.disconnect_from_server()
	game_sim.destroy_gamesim()
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	for child in car_node_container.get_children():
		child.queue_free()
	for p in players:
		p.queue_free()
	players.clear()
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	$Control.visible = true
	lobby_control.visible = false

func _return_to_lobby() -> void:
	game_sim.destroy_gamesim()
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	for child in car_node_container.get_children():
		child.queue_free()
	for p in players:
		p.queue_free()
	players.clear()
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	lobby_control.visible = true
	network_manager.flush_waiting_peers()
	network_manager.reset_race_state()

func _check_race_finished() -> void:
	if !game_sim.sim_started:
		return
	var all_done := true
	for car in car_node_container.get_children():
		if car is VisualCar:
			if network_manager.player_ids.has(car.owning_id):
				if (car.machine_state & VisualCar.FZ_MS.COMPLETEDRACE_1_Q) == 0:
					all_done = false
					break
	if network_manager.is_server:
		if all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()
				race_finish_label.visible = true
				network_manager.send_race_finish_time(network_manager.net_race_finish_time)
			if Time.get_ticks_msec() > network_manager.net_race_finish_time + 5000:
				network_manager.send_end_race()
				race_finish_label.visible = false
	else:
		if network_manager.net_race_finish_time != -1:
			race_finish_label.visible = true

func _process(delta: float) -> void:
	frame_time_label.text = str(network_manager.rollback_frametime_us)
