class_name GameManager extends Node

@onready var game_sim: GameSim = $GameSim
@onready var server_game_sim: GameSim = $ServerGameSim
@onready var connect_host_box: HBoxContainer = $Control/ConnectHostBox
@onready var start_button: Button = $Control/ConnectHostBox/StartButton
@onready var join_button: Button = $Control/ConnectHostBox/JoinButton
@onready var ip_field: LineEdit = $Control/ConnectHostBox/IPField
@onready var port_field: LineEdit = $Control/ConnectHostBox/Port
@onready var track_selector: OptionButton = $Control/TrackSelector
@onready var lobby_control: Control = $Lobby
@onready var lobby_track_selector: OptionButton = $Lobby/LobbyTrackSelector
@onready var start_race_button: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/StartRaceButton
@onready var player_list: ItemList = $Lobby/PlayerList
@onready var car_node_container: CarNodeContainer = $GameWorld/CarNodeContainer
@onready var spark_node_container: SuperSparkContainer = $GameWorld/SuperSparkContainer
@onready var obj_container: Node3D = $GameWorld/ObjContainer
@onready var debug_track_mesh: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/DebugTrackMesh
@onready var network_manager: NetworkManager = $NetworkManager
@onready var cpu_driver_manager: CpuDriverManager = $CpuDriverManager
@onready var car_settings: Control = $CarSettings
@onready var controller_settings: Control = $ControllerSettings
@onready var car_settings_button: Button = $Control/CarSettingsButton
@onready var singleplayer_button: Button = $Control/SingleplayerButton
@onready var spectator_race_button: Button = $Control/SpectatorRaceButton
@onready var controller_settings_button: Button = $Control/ControllerSettingsButton
@onready var track_editor_button: Button = $Control/TrackEditorButton
@onready var car_settings_button_lobby: Button = $Lobby/CarSettingsButton
@onready var controller_settings_button_lobby: Button = $Lobby/ControllerSettingsButton
@onready var race_finish_label: Label = $RaceFinishLabel
@onready var frame_time_label: Label = $FrameTimeLabel
@onready var rtt_label: Label = $RTTLabel
@onready var cpu_slider: HSlider = $Control/CpuSlider
@onready var cpu_slider_label: Label = $Control/CpuSliderLabel
@onready var add_cpu_button: Button = $Lobby/AddCpuButton
@onready var remove_cpu_button: Button = $Lobby/RemoveCpuButton
@onready var lobby_game_mode_choice: OptionButton = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/GameModeChoice
@onready var lobby_vehicle_restore_toggle: CheckBox = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/VehicleRestoreToggle
@onready var lobby_bumpers_toggle: CheckBox = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/BumpersToggle
@onready var lobby_stage_button_container: VBoxContainer = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/StageBox/StageScroll/StageButtonContainer
@onready var lobby_stage_preview_container: HBoxContainer = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/StageBox/PreviewScroll/StagePreviewContainer
@onready var lobby_player_list_container: VBoxContainer = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/PlayerScroll/PlayerListContainer
@onready var lobby_chibi_viewport: SubViewport = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport
@onready var lobby_chibi_camera: Camera3D = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport/LobbyChibiCamera
@onready var lobby_chibi_root: Node3D = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport/LobbyChibiRoot
@onready var lobby_chibi_nameplates: Control = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport/LobbyChibiNameplates
@onready var lobby_chat_box: RichTextLabel = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ChatPanel/ChatMargin/ChatBox/LobbyChatBox
@onready var lobby_say_text: LineEdit = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ChatPanel/ChatMargin/ChatBox/ChatInputBox/LobbySayText
@onready var lobby_send_text_button: Button = $Lobby/LobbyStatic/LobbyContainer/BottomBox/ChatPanel/ChatMargin/ChatBox/ChatInputBox/LobbySendTextButton
@onready var race_pause_root: Control = $RacePauseLayer/RacePauseRoot
@onready var race_pause_title: Label = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/RacePauseTitle
@onready var race_pause_resume_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/ResumeButton
@onready var race_pause_lobby_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/LobbyButton
@onready var race_pause_disconnect_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/DisconnectButton
var lobby_chibi_cars := {}
var lobby_grand_prix_track_sequence: Array[int] = []
var lobby_applying_race_options := false
var lobby_player_list_signature := ""

@onready var obj_viewport: SubViewport = get_node_or_null("GameWorld/ObjViewport") as SubViewport
@onready var outline_viewport: SubViewport = get_node_or_null("GameWorld/OutlineViewport") as SubViewport
@onready var obj_viewport_texture: ColorRect = get_node_or_null("GameWorld/ObjViewportTexture") as ColorRect
@onready var outline_viewport_texture: ColorRect = get_node_or_null("GameWorld/OutlineViewportTexture") as ColorRect

const PlayerInputClass = preload("res://player/player_input.gd")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const LobbyChibiCarClass = preload("res://ui/lobby_chibi_car.gd")
const FinishMedalScene: PackedScene = preload("res://ui/finish_medal.tscn")
const KoMedalScene: PackedScene = preload("res://ui/ko_medal.tscn")
const BUMPER_DEFINITION_PATH := "res://vehicle/asset/bumper/definition.tres"
const BUMPER_POOL_SIZE := 60

var tracks: Array = []
var car_definitions: Array = []
var players: Array = []
var player_scene := preload("res://player/player_controller.tscn")
var spectator_scene := preload("res://player/spectator.tscn")
var local_player_index: int = 0
var headless_mode: bool = false
var trigger_objects: Array = []
var spectator_node: Node3D
var local_elimination_spectator_active := false
const TRIGGER_SCENES = {
			 0: preload("res://asset/obj_dashplate.tscn"),
			 1: preload("res://asset/obj_jumpplate.tscn"),
			 2: preload("res://asset/obj_mine.tscn"),
}

const ROAD_MATS = {
	0: preload("res://asset/tex/track/tracktex (1).png"),
	1: preload("res://asset/tex/track/tracktex (2).png"),
	2: preload("res://asset/tex/track/tracktex (3).png"),
	3: preload("res://asset/tex/track/tracktex (4).png"),
	4: preload("res://asset/tex/track/tracktex (5).png"),
	5: preload("res://asset/tex/track/tracktex (6).png"),
	6: preload("res://asset/tex/track/tracktex (7).png"),
	7: preload("res://asset/tex/track/tracktex (8).png"),
	8: preload("res://asset/tex/track/tracktex (9).png"),
	9: preload("res://asset/tex/track/tracktex (10).png"),
	10: preload("res://asset/tex/track/tracktex (11).png"),
	11: preload("res://asset/tex/track/tracktex (12).png"),
	12: preload("res://asset/tex/track/tracktex (13).png"),
	13: preload("res://asset/tex/track/tracktex (14).png"),
	14: preload("res://asset/tex/track/tracktex (15).png"),
	15: preload("res://asset/tex/track/tracktex (16).png"),
	16: preload("res://asset/tex/track/tracktex (17).png"),
	17: preload("res://asset/tex/track/tracktex (18).png"),
	18: preload("res://asset/tex/track/tracktex (19).png"),
}

# Singleplayer state
var singleplayer_mode: bool = false
var _singleplayer_tick: int = 0
var singleplayer_cpu_count: int = 0
var launch_cpu_driver_count: int = -1
var auto_host_mode: bool = false
var auto_singleplayer_mode: bool = false
var auto_track_editor_mode: bool = false
var auto_accelerate_mode: bool = false
var auto_bumpers_mode: bool = false
var debug_bumper_smoke_mode: bool = false
var auto_render_profile_mode: bool = false
var auto_disable_car_multimesh_mode: bool = false
var auto_disable_node_effects_mode: bool = false
var auto_disable_thruster_lights_mode: bool = false
var auto_hide_track_visuals_mode: bool = false
var auto_disable_hud_mode: bool = false
var auto_hide_hud_only_mode: bool = false
var auto_disable_hud_process_only_mode: bool = false
var auto_disable_minimap_mode: bool = false
var auto_quit_after_frames: int = -1
var current_track_meta: Dictionary = {}
var current_track_ground_image: Image
var car_render_manager: CarRenderManager
var debug_replay_recording: bool = false
var debug_replay_playback: bool = false
var debug_replay_inputs: Array = []
var debug_replay_snapshot_tick: int = -1
var debug_replay_snapshot_state: PackedByteArray = PackedByteArray()
var debug_replay_playback_inputs: Array = []
var debug_replay_playback_index: int = 0
var debug_replay_autoload_path: String = ""
var debug_replay_loaded_path: String = ""
var _last_race_track_index: int = -1
var _last_race_settings: Array = []

const DEBUG_REPLAY_VERSION := 1
const DIP_TRACE_RAIL_SAMPLING := 0x40
const DIP_TRACE_PIPE_FLOOR := 0x100
const DIP_TRACE_MESH_FLOOR := 0x1000

var race_pause_open := false
var debug_rail_trace_requested := false
var active_stickers := {}
var race_notification_hide_msec := 0
var race_medals: Array[Control] = []
var render_profile_frames := 0
var render_profile_physics_us := 0
var render_profile_tick_us := 0
var render_profile_render_us := 0
var render_profile_nametag_us := 0
var render_profile_local_visual_us := 0
var render_profile_process_us := 0
var render_profile_visuals_only_us := 0
var nametag_pool: Array[Label] = []
var nametag_pool_car_indices: Array[int] = []
var nametag_pool_pending_indices: Array[int] = []
var nametag_names: Array[String] = []
var nametag_best_distances: Array[float] = []
var nametag_best_indices: Array[int] = []
var placement_badge_pool: Array[TextureRect] = []

const TOP_PLACE_BADGE_TEXTURES: Array[Texture2D] = [
	preload("res://ui/placements/mxt-1.png"),
	preload("res://ui/placements/mxt-2.png"),
	preload("res://ui/placements/mxt-3.png"),
]

const NAMETAG_VISIBLE_BUDGET := 30
const NAMETAG_MAX_DISTANCE_SQ := 12000.0

func _ready() -> void:
	#obj_viewport_texture.texture = obj_viewport.get_texture()
	#outline_viewport_texture.texture = outline_viewport.get_texture()
	car_render_manager = CarRenderManagerClass.new()
	car_render_manager.name = "CarRenderManager"
	$GameWorld.add_child(car_render_manager)
	randomize()
	_build_lobby_options_controls()
	_build_multiplayer_connect_box()
	_load_tracks()
	_load_car_definitions()
	network_manager.race_started.connect(_on_network_race_started)
	network_manager.race_finished.connect(_on_network_race_finished)
	network_manager.race_event.connect(_on_race_event)
	network_manager.race_options_changed.connect(_on_network_race_options_changed)
	car_settings.hide()
	if !car_settings_button.pressed.is_connected(_on_car_settings_button_pressed):
		car_settings_button.pressed.connect(_on_car_settings_button_pressed)
	if !car_settings_button_lobby.pressed.is_connected(_on_car_settings_button_pressed):
		car_settings_button_lobby.pressed.connect(_on_car_settings_button_pressed)
	if !controller_settings_button.pressed.is_connected(_on_controller_settings_button_pressed):
		controller_settings_button.pressed.connect(_on_controller_settings_button_pressed)
	if !controller_settings_button_lobby.pressed.is_connected(_on_controller_settings_button_pressed):
		controller_settings_button_lobby.pressed.connect(_on_controller_settings_button_pressed)
	if !track_editor_button.pressed.is_connected(_on_track_editor_button_pressed):
		track_editor_button.pressed.connect(_on_track_editor_button_pressed)
	# Rewire the Singleplayer button to its own handler, not the multiplayer host flow
	if singleplayer_button.pressed.is_connected(_on_start_button_pressed):
		singleplayer_button.pressed.disconnect(_on_start_button_pressed)
	singleplayer_button.pressed.connect(_on_singleplayer_button_pressed)
	spectator_race_button.pressed.connect(_on_spectator_race_button_pressed)
	cpu_slider.value_changed.connect(_on_singleplayer_cpu_slider_changed)
	add_cpu_button.pressed.connect(_on_add_cpu_button_pressed)
	remove_cpu_button.pressed.connect(_on_remove_cpu_button_pressed)
	if !start_race_button.pressed.is_connected(_on_start_race_button_pressed):
		start_race_button.pressed.connect(_on_start_race_button_pressed)
	_build_race_pause_menu()
	singleplayer_cpu_count = int(cpu_slider.value)
	_update_cpu_slider_label()
	network_manager.set_cpu_driver_manager(cpu_driver_manager)
	headless_mode = DisplayServer.get_name() == "headless"
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	launch_cpu_driver_count = _parse_cpu_driver_count_arg(args)
	if launch_cpu_driver_count < 0:
		launch_cpu_driver_count = _parse_cpu_driver_count_arg(user_args)
	if launch_cpu_driver_count >= 0:
		singleplayer_cpu_count = launch_cpu_driver_count
		if cpu_slider != null:
			cpu_slider.value = singleplayer_cpu_count
		_update_cpu_slider_label()
		network_manager.set_cpu_driver_count(launch_cpu_driver_count)
	auto_host_mode = args.has("--host") or user_args.has("--host")
	if auto_host_mode:
		call_deferred("_auto_host")
	auto_singleplayer_mode = args.has("--auto-singleplayer") or user_args.has("--auto-singleplayer")
	auto_accelerate_mode = args.has("--auto-accelerate") or user_args.has("--auto-accelerate")
	auto_bumpers_mode = args.has("--auto-bumpers") or user_args.has("--auto-bumpers")
	debug_bumper_smoke_mode = args.has("--debug-bumper-smoke") or user_args.has("--debug-bumper-smoke")
	auto_render_profile_mode = args.has("--render-profile") or user_args.has("--render-profile")
	auto_disable_car_multimesh_mode = args.has("--profile-disable-car-multimesh") or user_args.has("--profile-disable-car-multimesh")
	auto_disable_node_effects_mode = args.has("--profile-disable-node-effects") or user_args.has("--profile-disable-node-effects")
	auto_disable_thruster_lights_mode = args.has("--profile-disable-thruster-lights") or user_args.has("--profile-disable-thruster-lights")
	auto_hide_track_visuals_mode = args.has("--profile-hide-track-visuals") or user_args.has("--profile-hide-track-visuals")
	auto_disable_hud_mode = args.has("--profile-disable-hud") or user_args.has("--profile-disable-hud")
	auto_hide_hud_only_mode = args.has("--profile-hide-hud-only") or user_args.has("--profile-hide-hud-only")
	auto_disable_hud_process_only_mode = args.has("--profile-disable-hud-process-only") or user_args.has("--profile-disable-hud-process-only")
	auto_disable_minimap_mode = args.has("--profile-disable-minimap") or user_args.has("--profile-disable-minimap")
	game_sim.set_render_profile_enabled(auto_render_profile_mode)
	game_sim.set_render_node_effects_enabled(!auto_disable_node_effects_mode)
	game_sim.set_render_thruster_lights_enabled(!auto_disable_thruster_lights_mode)
	auto_track_editor_mode = args.has("--track-editor") or user_args.has("--track-editor") or args.has("--mxt-track-editor") or user_args.has("--mxt-track-editor")
	var quit_idx := args.find("--quit-after-frames")
	var quit_args := args
	if quit_idx == -1:
		quit_idx = user_args.find("--quit-after-frames")
		quit_args = user_args
	if quit_idx != -1 and quit_idx + 1 < quit_args.size():
		auto_quit_after_frames = max(0, int(quit_args[quit_idx + 1]))
	var replay_idx := args.find("--debug-replay")
	var replay_args := args
	if replay_idx == -1:
		replay_idx = user_args.find("--debug-replay")
		replay_args = user_args
	if replay_idx != -1 and replay_idx + 1 < replay_args.size():
		debug_replay_autoload_path = String(replay_args[replay_idx + 1])
	debug_rail_trace_requested = args.has("--debug-rail-trace") or user_args.has("--debug-rail-trace")
	if debug_rail_trace_requested:
		game_sim.set_dip_switch_enabled(DIP_TRACE_RAIL_SAMPLING, true)
		server_game_sim.set_dip_switch_enabled(DIP_TRACE_RAIL_SAMPLING, true)
	if args.has("--debug-mesh-floor-trace") or user_args.has("--debug-mesh-floor-trace"):
		game_sim.set_dip_switch_enabled(DIP_TRACE_MESH_FLOOR, true)
		server_game_sim.set_dip_switch_enabled(DIP_TRACE_MESH_FLOOR, true)
	if debug_replay_autoload_path != "":
		call_deferred("_load_and_start_debug_replay", debug_replay_autoload_path)
	elif auto_track_editor_mode:
		call_deferred("_on_track_editor_button_pressed")
	elif auto_singleplayer_mode:
		call_deferred("_on_singleplayer_button_pressed")
	if headless_mode and !auto_host_mode and !auto_track_editor_mode and !auto_singleplayer_mode and debug_replay_autoload_path == "":
		var def_path := ""
		if car_definitions.size() > 0:
			def_path = car_definitions[0].resource_path
		var settings_dict = {
			"username": "Headless",
			"car_definition_path": def_path,
			"accel_setting": 1.0,
		}
		network_manager.multiplayer.connected_to_server.connect(
			func():
				network_manager.send_player_settings(settings_dict),
			Object.CONNECT_ONE_SHOT)
		var join_timer := Timer.new()
		join_timer.one_shot = true
		join_timer.wait_time = 3.0
		add_child(join_timer)
		join_timer.timeout.connect(func(): network_manager.join("127.0.0.1"))
		join_timer.start()
		$Control.visible = false
		lobby_control.visible = true

func _parse_cpu_driver_count_arg(args: Array) -> int:
	var cpu_idx := args.find("-cpu-drivers")
	if cpu_idx == -1:
		cpu_idx = args.find("--cpu-drivers")
	if cpu_idx == -1 or cpu_idx + 1 >= args.size():
		return -1
	return int(clamp(float(args[cpu_idx + 1]), 0.0, 5000.0))

func _load_tracks() -> void:
	tracks.clear()
	track_selector.clear()
	lobby_track_selector.clear()
	lobby_grand_prix_track_sequence.clear()
	_scan_dir("res://track")
	for t in tracks:
		track_selector.add_item(t["name"])
		lobby_track_selector.add_item(t["name"])
	if tracks.size() > 0:
		track_selector.selected = 0
		lobby_track_selector.selected = 0
		lobby_grand_prix_track_sequence.append(0)
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	var track_name_idx := args.find("--track-name")
	var track_args := args
	if track_name_idx == -1:
		track_name_idx = user_args.find("--track-name")
		track_args = user_args
	if track_name_idx != -1 and track_name_idx + 1 < track_args.size():
		var desired_track := String(track_args[track_name_idx + 1]).to_lower()
		for i in range(tracks.size()):
			if String(tracks[i].get("name", "")).to_lower() == desired_track:
				track_selector.selected = i
				lobby_track_selector.selected = i
				if !lobby_grand_prix_track_sequence.is_empty():
					lobby_grand_prix_track_sequence[0] = i
				break
	_populate_lobby_stage_buttons()
	_refresh_lobby_race_options()

func get_cpu_driver_manager() -> CpuDriverManager:
	return cpu_driver_manager

func get_car_definition_paths() -> Array:
	var paths: Array = []
	for def in car_definitions:
		if def is Resource:
			var path = def.resource_path
			if path != "":
				paths.append(path)
	return paths

func build_cpu_player_settings(index: int) -> Dictionary:
	var ps := PlayerSettings.new()
	ps.username = "CPU %03d" % (index + 1)
	var defs := get_car_definition_paths()
	if defs.size() > 0:
		ps.car_definition_path = defs[index % defs.size()]
	else:
		ps.car_definition_path = ""
	ps.accel_setting = 1.0
	ps.spectator = false
	return ps.to_dict()

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
	var dir = DirAccess.open("res://vehicle/asset")
	if dir == null:
		return
	dir.list_dir_begin()
	var folder := dir.get_next()
	while folder != "":
		if dir.current_is_dir() and !folder.begins_with(".") and folder != "bumper":
			var def_path := "res://vehicle/asset/%s/definition.tres" % folder
			if ResourceLoader.exists(def_path):
				var def_res := load(def_path)
				if def_res != null:
					car_definitions.append(def_res)
		folder = dir.get_next()
	dir.list_dir_end()

func _on_start_button_pressed() -> void:
	var err := network_manager.host(_multiplayer_lobby_port())
	if err != OK:
		return
	if launch_cpu_driver_count >= 0:
		network_manager.set_cpu_driver_count(launch_cpu_driver_count)
	network_manager.send_player_settings(car_settings.get_player_settings().to_dict())
	start_race_button.disabled = false
	$Control.visible = false
	lobby_control.visible = true

func _on_singleplayer_button_pressed() -> void:
	_start_singleplayer_race(false)

func _on_spectator_race_button_pressed() -> void:
	_start_singleplayer_race(true)

func _on_track_editor_button_pressed() -> void:
	get_tree().change_scene_to_file("res://track_editing_scene.tscn")

func _start_singleplayer_race(as_spectator: bool) -> void:
	# Start a local, singleplayer race that does not touch networking at all.
	# Prepare a minimal settings array using the current local player settings.
	singleplayer_mode = true
	_singleplayer_tick = 0
	network_manager.reset_race_state()
	var my_id := _local_player_id()
	if as_spectator:
		network_manager.player_ids = []
		network_manager.spectator_ids = [my_id]
	else:
		network_manager.player_ids = [my_id]
		network_manager.spectator_ids = []
	network_manager.set_singleplayer_cpu_count(singleplayer_cpu_count)
	if auto_bumpers_mode:
		network_manager.race_options["bumpers"] = true
	var ps = car_settings.get_player_settings()
	# Ensure we have a sensible car selection; fall back if needed
	if ps.car_definition_path == "" and car_definitions.size() > 0:
		ps.car_definition_path = car_definitions[0].resource_path
	ps.spectator = as_spectator
	network_manager.player_settings[my_id] = ps.to_dict()
	# Invoke the normal race startup, but driven entirely by local state
	var settings_array: Array = [ps.to_dict()]
	var cpu_ids := network_manager.get_cpu_roster()
	for i in range(cpu_ids.size()):
		var cpu_id = cpu_ids[i]
		var cpu_settings = network_manager.player_settings.get(cpu_id, build_cpu_player_settings(i))
		settings_array.append(cpu_settings)
	_start_race(track_selector.selected, settings_array)
	# Hide menus
	$Control.visible = false
	lobby_control.visible = false

func _on_join_button_pressed() -> void:
	var settings_dict = car_settings.get_player_settings().to_dict()
	var err := network_manager.join(ip_field.text, _multiplayer_lobby_port())
	if err != OK:
		return
	network_manager.multiplayer.connected_to_server.connect(
		func():
			network_manager.send_player_settings(settings_dict),
		Object.CONNECT_ONE_SHOT)
	start_race_button.disabled = true
	$Control.visible = false
	lobby_control.visible = true

func _auto_host() -> void:
	_on_start_button_pressed()

func _on_singleplayer_cpu_slider_changed(value: float) -> void:
	singleplayer_cpu_count = int(round(value))
	_update_cpu_slider_label()

func _update_cpu_slider_label() -> void:
	if cpu_slider_label:
		cpu_slider_label.text = "CPU Racers: %d" % singleplayer_cpu_count

func _build_multiplayer_connect_box() -> void:
	start_button.text = "Host\n"
	join_button.text = "Connect\n"
	ip_field.custom_minimum_size = Vector2(192.0, 0.0)
	port_field.custom_minimum_size = Vector2(192.0, 0.0)
	port_field.text = "27016"
	if !port_field.text_submitted.is_connected(_on_multiplayer_port_submitted):
		port_field.text_submitted.connect(_on_multiplayer_port_submitted)

func _on_multiplayer_port_submitted(_text: String) -> void:
	_on_join_button_pressed()

func _multiplayer_lobby_port() -> int:
	if port_field == null:
		return 27016
	var parsed_port := port_field.text.to_int()
	if parsed_port <= 0:
		return 27016
	return int(clamp(parsed_port, 1, 65535))

func _build_lobby_options_controls() -> void:
	player_list.visible = false
	player_list.custom_minimum_size = Vector2(220.0, 320.0)
	player_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	player_list.size_flags_vertical = Control.SIZE_EXPAND_FILL

	lobby_track_selector.visible = false
	lobby_track_selector.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if !lobby_track_selector.item_selected.is_connected(_on_lobby_track_selected):
		lobby_track_selector.item_selected.connect(_on_lobby_track_selected)

	add_cpu_button.visible = false
	remove_cpu_button.visible = false
	car_settings_button_lobby.visible = false
	controller_settings_button_lobby.visible = false
	if !lobby_game_mode_choice.item_selected.is_connected(_on_lobby_game_mode_selected):
		lobby_game_mode_choice.item_selected.connect(_on_lobby_game_mode_selected)
	if !lobby_vehicle_restore_toggle.toggled.is_connected(_on_lobby_vehicle_restore_toggled):
		lobby_vehicle_restore_toggle.toggled.connect(_on_lobby_vehicle_restore_toggled)
	if !lobby_bumpers_toggle.toggled.is_connected(_on_lobby_bumpers_toggled):
		lobby_bumpers_toggle.toggled.connect(_on_lobby_bumpers_toggled)
	if !lobby_say_text.text_submitted.is_connected(_on_lobby_chat_text_submitted):
		lobby_say_text.text_submitted.connect(_on_lobby_chat_text_submitted)
	if !lobby_send_text_button.pressed.is_connected(_on_lobby_chat_send_pressed):
		lobby_send_text_button.pressed.connect(_on_lobby_chat_send_pressed)
	var viewport_stack := $Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack as Control
	if viewport_stack != null and !viewport_stack.gui_input.is_connected(_on_lobby_chibi_view_gui_input):
		viewport_stack.gui_input.connect(_on_lobby_chibi_view_gui_input)
	_populate_lobby_stage_buttons()
	_refresh_lobby_stage_preview()

func _build_race_pause_menu() -> void:
	if !race_pause_resume_button.pressed.is_connected(_close_race_pause_menu):
		race_pause_resume_button.pressed.connect(_close_race_pause_menu)
	if !race_pause_lobby_button.pressed.is_connected(_on_pause_lobby_pressed):
		race_pause_lobby_button.pressed.connect(_on_pause_lobby_pressed)
	if !race_pause_disconnect_button.pressed.is_connected(_on_pause_disconnect_pressed):
		race_pause_disconnect_button.pressed.connect(_on_pause_disconnect_pressed)

func _open_race_pause_menu() -> void:
	if race_pause_root == null or !game_sim.sim_started:
		return
	race_pause_open = true
	race_pause_root.visible = true
	var host := network_manager.is_server and !singleplayer_mode
	race_pause_title.text = "Host Race Menu" if host else "Race Menu"
	race_pause_lobby_button.visible = host
	race_pause_disconnect_button.text = "Exit To Main Menu" if singleplayer_mode else "Disconnect"
	race_pause_resume_button.grab_focus()

func _close_race_pause_menu() -> void:
	race_pause_open = false
	if race_pause_root != null:
		race_pause_root.visible = false

func _on_pause_disconnect_pressed() -> void:
	_close_race_pause_menu()
	_return_to_menu()

func _on_pause_lobby_pressed() -> void:
	_close_race_pause_menu()
	if network_manager.is_server:
		network_manager.send_end_race()

func _on_add_cpu_button_pressed() -> void:
	if !network_manager.is_server:
		return
	network_manager.add_cpu_driver()

func _on_remove_cpu_button_pressed() -> void:
	if !network_manager.is_server:
		return
	network_manager.remove_cpu_driver()

func _on_lobby_game_mode_selected(_index: int) -> void:
	_refresh_lobby_race_options()

func _on_lobby_track_selected(_index: int) -> void:
	_refresh_lobby_race_options()

func _populate_lobby_stage_buttons() -> void:
	if lobby_stage_button_container == null:
		return
	for child in lobby_stage_button_container.get_children():
		child.queue_free()
	for i in range(tracks.size()):
		var button := Button.new()
		button.text = str(tracks[i].get("name", "Track"))
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(_on_lobby_stage_button_pressed.bind(i))
		lobby_stage_button_container.add_child(button)

func _on_lobby_stage_button_pressed(track_index: int) -> void:
	if !network_manager.is_server:
		return
	if track_index < 0 or track_index >= tracks.size():
		return
	lobby_grand_prix_track_sequence.append(track_index)
	_refresh_lobby_race_options()

func _on_lobby_stage_preview_pressed(sequence_index: int) -> void:
	if !network_manager.is_server:
		return
	if sequence_index < 0 or sequence_index >= lobby_grand_prix_track_sequence.size():
		return
	lobby_grand_prix_track_sequence.remove_at(sequence_index)
	_refresh_lobby_race_options()

func _on_lobby_vehicle_restore_toggled(_toggled: bool) -> void:
	_refresh_lobby_race_options()

func _on_lobby_bumpers_toggled(_toggled: bool) -> void:
	_refresh_lobby_race_options()

func _build_lobby_race_options() -> Dictionary:
	var selected_track_indices := []
	for selected_index in lobby_grand_prix_track_sequence:
		selected_track_indices.append(int(selected_index))
	if selected_track_indices.is_empty():
		selected_track_indices.append(lobby_track_selector.selected)
	return {
		"game_mode": lobby_game_mode_choice.selected if lobby_game_mode_choice != null else 0,
		"track_indices": selected_track_indices,
		"vehicle_restore": lobby_vehicle_restore_toggle.button_pressed if lobby_vehicle_restore_toggle != null else true,
		"bumpers": lobby_bumpers_toggle.button_pressed if lobby_bumpers_toggle != null else false,
		"grand_prix_current_track": 0,
		"grand_prix_points": {},
		"grand_prix_ko_energy_bonuses": {},
		"grand_prix_eliminated_ids": [],
	}

func _refresh_lobby_race_options() -> void:
	if lobby_applying_race_options:
		_refresh_lobby_stage_preview()
		return
	var options := _build_lobby_race_options()
	if network_manager.is_server:
		network_manager.send_race_options(options)
	else:
		network_manager.race_options = options
	_refresh_lobby_stage_preview()

func _on_network_race_options_changed(options: Dictionary) -> void:
	if lobby_game_mode_choice == null:
		return
	lobby_applying_race_options = true
	var mode := int(options.get("game_mode", 0))
	if mode >= 0 and mode < lobby_game_mode_choice.item_count:
		lobby_game_mode_choice.select(mode)
	if lobby_vehicle_restore_toggle != null:
		lobby_vehicle_restore_toggle.set_pressed_no_signal(bool(options.get("vehicle_restore", true)))
	if lobby_bumpers_toggle != null:
		lobby_bumpers_toggle.set_pressed_no_signal(bool(options.get("bumpers", false)))
	lobby_grand_prix_track_sequence.clear()
	var track_indices: Array = options.get("track_indices", [])
	for track_index in track_indices:
		var idx := int(track_index)
		if idx >= 0 and idx < tracks.size():
			lobby_grand_prix_track_sequence.append(idx)
	if track_indices.size() > 0:
		var first_index := int(track_indices[0])
		if first_index >= 0 and first_index < tracks.size():
			lobby_track_selector.selected = first_index
	lobby_applying_race_options = false
	_refresh_lobby_stage_preview()

func _refresh_lobby_stage_preview() -> void:
	if lobby_stage_preview_container == null:
		return
	for child in lobby_stage_preview_container.get_children():
		child.queue_free()
	var options := _build_lobby_race_options()
	var track_indices: Array = options.get("track_indices", [])
	for i in range(track_indices.size()):
		var track_index := int(track_indices[i])
		var label := Button.new()
		label.disabled = !network_manager.is_server
		if track_index >= 0 and track_index < tracks.size():
			label.text = "%d. %s" % [i + 1, str(tracks[track_index].get("name", "Track"))]
		else:
			label.text = "%d. Missing Track" % (i + 1)
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		label.pressed.connect(_on_lobby_stage_preview_pressed.bind(i))
		lobby_stage_preview_container.add_child(label)

func _on_lobby_chat_send_pressed() -> void:
	if lobby_say_text == null:
		return
	_submit_lobby_chat_message(lobby_say_text.text)
	lobby_say_text.clear()

func _on_lobby_chat_text_submitted(text: String) -> void:
	_submit_lobby_chat_message(text)
	if lobby_say_text != null:
		lobby_say_text.clear()
		lobby_say_text.release_focus()

func _submit_lobby_chat_message(text: String) -> void:
	var clean := text.strip_edges()
	if clean == "":
		return
	if clean.length() > 220:
		clean = clean.substr(0, 220)
	if !network_manager.has_network_peer():
		_append_lobby_chat_message(_local_player_id(), clean)
	elif network_manager.is_server:
		_broadcast_lobby_chat_message.rpc(_local_player_id(), clean)
	else:
		_send_lobby_chat_message_to_server.rpc_id(1, clean)

@rpc("any_peer", "call_local", "reliable")
func _send_lobby_chat_message_to_server(text: String) -> void:
	if !network_manager.is_server:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = _local_player_id()
	var clean := text.strip_edges()
	if clean == "":
		return
	if clean.length() > 220:
		clean = clean.substr(0, 220)
	_broadcast_lobby_chat_message.rpc(sender, clean)

@rpc("any_peer", "call_local", "reliable")
func _broadcast_lobby_chat_message(sender_id: int, text: String) -> void:
	_append_lobby_chat_message(sender_id, text)

func _append_lobby_chat_message(sender_id: int, text: String) -> void:
	if lobby_chat_box == null:
		return
	var color := Color(1.0, 1.0, 0.4, 1.0) if sender_id == _local_player_id() else Color(0.78, 0.84, 1.0, 1.0)
	var name := _player_display_name(sender_id)
	lobby_chat_box.add_text("\n")
	lobby_chat_box.push_color(color)
	lobby_chat_box.add_text(name)
	lobby_chat_box.pop()
	lobby_chat_box.add_text(": " + text)

func _on_lobby_chibi_view_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and lobby_say_text != null:
		lobby_say_text.release_focus()

func _lobby_accepts_chibi_input() -> bool:
	if !_window_accepts_input():
		return false
	return lobby_say_text == null or !lobby_say_text.has_focus()

func _update_lobby_chibi_cars(_delta: float) -> void:
	if lobby_chibi_root == null:
		return
	var roster := _get_lobby_human_roster()
	var live := {}
	for i in range(roster.size()):
		var id := int(roster[i])
		live[id] = true
		var settings = network_manager.player_settings.get(id, {})
		if !lobby_chibi_cars.has(id) or !is_instance_valid(lobby_chibi_cars[id]):
			var new_car = LobbyChibiCarClass.new()
			new_car.name = "ChibiCar%d" % id
			new_car.position = _lobby_chibi_spawn_position(i)
			lobby_chibi_root.add_child(new_car)
			new_car.setup(id, settings, self, lobby_chibi_camera, lobby_chibi_nameplates, id == _local_player_id())
			lobby_chibi_cars[id] = new_car
		else:
			var existing_car = lobby_chibi_cars[id]
			existing_car.update_settings(settings)
			existing_car.set_local_control(id == _local_player_id())
	for id in lobby_chibi_cars.keys():
		if !live.has(id):
			var stale_car = lobby_chibi_cars[id]
			if is_instance_valid(stale_car):
				stale_car.queue_free()
			lobby_chibi_cars.erase(id)

func _get_lobby_human_roster() -> Array:
	var out := []
	for source in [network_manager.player_ids, network_manager.spectator_ids, network_manager.waiting_peers]:
		for id in source:
			var int_id := int(id)
			if network_manager.get_cpu_roster().has(int_id):
				continue
			if out.has(int_id):
				continue
			out.append(int_id)
	return out

func _lobby_chibi_spawn_position(index: int) -> Vector3:
	var x := -6.0 + float(index % 4) * 4.0
	var z := -3.0 + float(index / 4) * 3.0
	return Vector3(x, 0.6, z)

func _send_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !network_manager.has_network_peer():
		_apply_lobby_chibi_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	elif network_manager.is_server:
		_apply_lobby_chibi_state.rpc(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	else:
		_submit_lobby_chibi_state.rpc_id(1, player_id, velocity, knockback_velocity, angle_velocity, position, rotation)

@rpc("any_peer", "call_local", "unreliable_ordered")
func _submit_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !network_manager.is_server:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender != 0:
		player_id = sender
	_apply_lobby_chibi_state.rpc(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)

@rpc("any_peer", "call_local", "unreliable_ordered")
func _apply_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !lobby_chibi_cars.has(player_id):
		return
	var car = lobby_chibi_cars[player_id]
	if car != null and is_instance_valid(car):
		car.apply_remote_state(velocity, knockback_velocity, angle_velocity, position, rotation)

func _initialize_grand_prix_options(options: Dictionary, roster: Array) -> Dictionary:
	var initialized := options.duplicate(true)
	if int(initialized.get("game_mode", 0)) != 1:
		return initialized
	var points := {}
	for id in roster:
		if !network_manager.get_cpu_roster().has(id):
			points[int(id)] = 0
	initialized["grand_prix_current_track"] = 0
	initialized["grand_prix_points"] = points
	initialized["grand_prix_ko_energy_bonuses"] = {}
	initialized["grand_prix_eliminated_ids"] = []
	return initialized

func _local_player_id() -> int:
	if singleplayer_mode:
		return 0
	return multiplayer.get_unique_id() if network_manager.has_network_peer() else 0

func _player_display_name(id: int) -> String:
	if id < 0:
		return "Bumper"
	var name := str(id)
	var settings = network_manager.player_settings.get(id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		name = str(settings["username"])
	if network_manager.get_cpu_roster().has(id):
		name = "[CPU] " + name
	return name

func _show_race_notification(text: String, duration_msec: int = 2200) -> void:
	if race_finish_label == null:
		return
	race_finish_label.text = text
	race_finish_label.visible = true
	race_notification_hide_msec = Time.get_ticks_msec() + duration_msec

func _local_player_is_eliminated() -> bool:
	return !network_manager.is_vehicle_restore_enabled() and network_manager.player_eliminations.has(_local_player_id())

func _should_suppress_local_race_input() -> bool:
	return _local_player_is_eliminated()

func _activate_local_elimination_spectator() -> void:
	if local_elimination_spectator_active:
		return
	if !_local_player_is_eliminated():
		return
	local_elimination_spectator_active = true
	var current_camera := get_viewport().get_camera_3d()
	var start_transform := Transform3D.IDENTITY
	if current_camera != null:
		start_transform = current_camera.global_transform
	elif car_node_container.local_visual_car != null:
		var car := car_node_container.local_visual_car
		var car_transform := car.car_transform.global_transform
		var interest := car_transform.origin + car_transform.basis.y * 2.0
		var position := interest - car_transform.basis.z * 26.0 + car_transform.basis.y * 10.0 + car_transform.basis.x * 10.0
		start_transform.origin = position
		start_transform = start_transform.looking_at(interest, car_transform.basis.y.normalized())
	if spectator_node == null:
		spectator_node = spectator_scene.instantiate()
		add_child(spectator_node)
	spectator_node.global_transform = start_transform
	if spectator_node.has_method("sync_look_from_current_transform"):
		spectator_node.call("sync_look_from_current_transform")
	if car_node_container.local_visual_car != null:
		car_node_container.local_visual_car.race_hud.visible = false
	_show_race_notification("Eliminated - Spectating", 3000)

func _format_race_time(tick_value: int) -> String:
	var race_tick := maxi(0, tick_value - 300)
	var total_msec := int(round(float(race_tick) * 1000.0 / 60.0))
	var minutes := int(total_msec / 60000)
	var seconds := int(total_msec / 1000) % 60
	var milliseconds := total_msec % 1000
	return "%d:%02d.%03d" % [minutes, seconds, milliseconds]

func _format_ordinal(value: int) -> String:
	var mod_100 := value % 100
	if mod_100 >= 11 and mod_100 <= 13:
		return "%dth" % value
	match value % 10:
		1:
			return "%dst" % value
		2:
			return "%dnd" % value
		3:
			return "%drd" % value
		_:
			return "%dth" % value

func _format_race_results_summary() -> String:
	var lines := ["Race Results"]
	for i in range(network_manager.finish_order.size()):
		var id := int(network_manager.finish_order[i])
		lines.append("%s  %s" % [_format_ordinal(i + 1), _player_display_name(id)])
	if !network_manager.player_eliminations.is_empty():
		lines.append("")
		lines.append("Eliminated")
		for id_value in network_manager.player_eliminations.keys():
			lines.append(_player_display_name(int(id_value)))
	if network_manager.is_grand_prix_enabled():
		lines.append("")
		lines.append("Grand Prix Standings")
		var points: Dictionary = network_manager.race_options.get("grand_prix_points", {})
		var standings := []
		for id_value in points.keys():
			standings.append([int(_lookup_id_value(points, int(id_value), 0)), int(id_value)])
		standings.sort_custom(func(a, b): return a[0] > b[0])
		for i in range(standings.size()):
			lines.append("%s  %s  %d" % [
				_format_ordinal(i + 1),
				_player_display_name(int(standings[i][1])),
				int(standings[i][0])
			])
	return "\n".join(lines)

func _show_race_results_summary() -> void:
	if race_finish_label == null:
		return
	race_finish_label.text = _format_race_results_summary()
	race_finish_label.visible = true
	race_notification_hide_msec = 0

func _show_finish_medal(actor_id: int, tick_value: int) -> void:
	var medal := FinishMedalScene.instantiate() as Control
	_add_race_medal(medal)
	medal.call("set_finisher_name", _player_display_name(actor_id), _format_race_time(tick_value))

func _show_ko_medal(actor_id: int, target_id: int) -> void:
	var medal := KoMedalScene.instantiate() as Control
	_add_race_medal(medal)
	var target_name := "Obstacle" if target_id < 0 else _player_display_name(target_id)
	medal.call("set_names", _player_display_name(actor_id), target_name)

func _add_race_medal(medal: Control) -> void:
	add_child(medal)
	medal.tree_exited.connect(_refresh_race_medal_feed)
	race_medals.insert(0, medal)
	while race_medals.size() > 3:
		var oldest := race_medals.pop_back() as Control
		if is_instance_valid(oldest) and oldest.is_inside_tree():
			oldest.call("dismiss")
	_refresh_race_medal_feed()

func _refresh_race_medal_feed() -> void:
	race_medals = race_medals.filter(func(existing): return is_instance_valid(existing) and existing.is_inside_tree())
	for i in range(race_medals.size()):
		race_medals[i].call("set_feed_index", i)

func _on_race_event(event_type: String, actor_id: int, target_id: int, tick_value: int, value: int) -> void:
	if event_type == "sticker":
		_show_sticker(actor_id, value)
		return
	if event_type == "eliminated":
		if actor_id == _local_player_id():
			_activate_local_elimination_spectator()
		return
	if event_type == "ko":
		_show_ko_medal(actor_id, target_id)
		return
	if event_type == "finish":
		_show_finish_medal(actor_id, tick_value)

func _show_sticker(actor_id: int, sticker_index: int) -> void:
	var now := Time.get_ticks_msec()
	active_stickers[actor_id] = {
		"sticker": sticker_index,
		"started": now,
		"expires": now + 2200,
	}

func send_local_sticker(sticker_index: int) -> void:
	if singleplayer_mode or !network_manager.has_network_peer():
		_show_sticker(_local_player_id(), sticker_index)
	else:
		network_manager.send_sticker(sticker_index)

func _consume_authoritative_race_events() -> void:
	if singleplayer_mode:
		for event in game_sim.consume_race_events():
			_on_race_event("ko", int(event["actor_id"]), int(event["target_id"]), int(event["tick"]), int(event["value"]))
		return
	if network_manager.is_server and server_game_sim != null:
		for event in server_game_sim.consume_race_events():
			network_manager.send_race_event("ko", int(event["actor_id"]), int(event["target_id"]), int(event["tick"]), int(event["value"]))
	else:
		game_sim.consume_race_events()

func _parse_level_triggers(bytes: PackedByteArray) -> Array:
	var pb := StreamPeerBuffer.new()
	pb.data_array = bytes
	pb.big_endian = false
	var header_size := pb.get_u32()
	var version := pb.get_string(4)
	if version != "v0.9":
		push_error("MXT track format hard-cutover failure: expected v0.9, got %s" % version)
		return []
	var cp_count := pb.get_u32()
	var seg_count := pb.get_u32()
	var trig_count := pb.get_u32()
	var mesh_collision_triangle_count := pb.get_u32()

	for i in range(cp_count):
		pb.get_float() # pos start x
		pb.get_float(); pb.get_float()
		pb.get_float(); pb.get_float(); pb.get_float() # pos end
		for j in range(9):
			pb.get_float()
		for j in range(9):
			pb.get_float()
		for j in range(7):
			pb.get_float()
		pb.get_u32()
		for j in range(3):
			pb.get_float()
		pb.get_float()
		for j in range(3):
			pb.get_float()
		pb.get_float()
		var conn := pb.get_u32()
		for j in range(conn):
			pb.get_u32()

	var _skip_curve = func():
		var point_count := pb.get_u32()
		pb.seek(pb.get_position() + point_count * 16)

	for i in range(seg_count):
		pb.get_u32()
		var road_type := pb.get_u32()
		pb.get_u32()
		if road_type == 5 or road_type == 6:
			_skip_curve.call(); _skip_curve.call(); _skip_curve.call()
		if road_type == 2 or road_type == 4 or road_type == 6:
			_skip_curve.call()
		if road_type == 6:
			_skip_curve.call()
		var mod_count := pb.get_u32()
		for m in range(mod_count):
			_skip_curve.call(); _skip_curve.call()
		var embed_count := pb.get_u32()
		for e in range(embed_count):
			pb.get_float(); pb.get_float(); pb.get_u32(); _skip_curve.call(); _skip_curve.call()
		for j in range(3):
			_skip_curve.call()
		for j in range(9):
			_skip_curve.call()
		for j in range(3):
			_skip_curve.call()
		pb.get_float(); pb.get_float()
		pb.get_float(); pb.get_float()
		pb.get_float(); pb.get_float()

	var out := []
	for i in range(trig_count):
		var t_type := pb.get_u32()
		pb.get_u32()
		pb.get_u32()
		var b := Basis()
		b.x.x = pb.get_float()
		b.x.y = pb.get_float()
		b.x.z = pb.get_float()
		b.y.x = pb.get_float()
		b.y.y = pb.get_float()
		b.y.z = pb.get_float()
		b.z.x = pb.get_float()
		b.z.y = pb.get_float()
		b.z.z = pb.get_float()
		var origin := Vector3.ZERO
		origin.x = pb.get_float()
		origin.y = pb.get_float()
		origin.z = pb.get_float()
		var inv_t := Transform3D(b, origin)
		var tform := inv_t.affine_inverse()
		var ext := Vector3.ZERO
		ext.x = pb.get_float()
		ext.y = pb.get_float()
		ext.z = pb.get_float()
		out.append({"type": t_type, "transform": tform, "extents": ext})
	if mesh_collision_triangle_count > 0:
		pb.seek(pb.get_position() + mesh_collision_triangle_count * (4 + 18 * 4))
	return out

func _on_car_settings_button_pressed() -> void:
	car_settings.call("open_settings")

func _on_controller_settings_button_pressed() -> void:
	controller_settings.call("open_settings")

func _close_settings_menus_for_race_start() -> void:
	if car_settings != null:
		if car_settings.visible and car_settings.has_method("_on_close_pressed"):
			car_settings.call("_on_close_pressed")
		else:
			car_settings.hide()
	if controller_settings != null:
		if controller_settings.visible and controller_settings.has_method("_on_close_pressed"):
			controller_settings.call("_on_close_pressed")
		else:
			controller_settings.hide()

func _generate_random_input() -> PlayerInput:
	var p := PlayerInputClass.new()
	p.strafe_left = randf()
	p.strafe_right = randf()
	p.steer_horizontal = randf_range(-1.0, 1.0)
	p.steer_vertical = randf_range(-1.0, 1.0)
	p.accelerate = randf()
	p.brake = randf()
	p.spinattack = randi() % 2 == 0
	p.sideattack = randi() % 2 == 0
	p.boost = randi() % 2 == 0
	p.apply_quantization()
	return p

func _debug_replay_dir() -> String:
	return ProjectSettings.globalize_path("user://debug_replays")

func _debug_replay_make_stamp() -> String:
	return Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")

func _debug_replay_track_name() -> String:
	if _last_race_track_index >= 0 and _last_race_track_index < tracks.size():
		return String(tracks[_last_race_track_index].get("name", "track"))
	return "track"

func _debug_replay_track_path() -> String:
	if _last_race_track_index >= 0 and _last_race_track_index < tracks.size():
		return String(tracks[_last_race_track_index].get("mxt", ""))
	return ""

func _debug_replay_find_track_index(data: Dictionary) -> int:
	var replay_track_path := String(data.get("track_mxt", ""))
	if replay_track_path != "":
		for i in range(tracks.size()):
			if String(tracks[i].get("mxt", "")) == replay_track_path:
				return i
	var replay_track_name := String(data.get("track_name", ""))
	if replay_track_name != "":
		for i in range(tracks.size()):
			if String(tracks[i].get("name", "")) == replay_track_name:
				return i
	return int(data.get("track_index", -1))

func _start_debug_replay_recording() -> void:
	if !singleplayer_mode or !game_sim.sim_started:
		print("MXT_DEBUG_REPLAY record ignored: start a singleplayer race first.")
		return
	if _singleplayer_tick <= 0:
		print("MXT_DEBUG_REPLAY record ignored: wait one physics tick, then press F5 again.")
		return
	debug_replay_snapshot_tick = _singleplayer_tick - 1
	debug_replay_snapshot_state = game_sim.get_state_data(debug_replay_snapshot_tick)
	if debug_replay_snapshot_state.is_empty():
		print("MXT_DEBUG_REPLAY record failed: native state snapshot was empty.")
		return
	debug_replay_inputs.clear()
	debug_replay_recording = true
	print("MXT_DEBUG_REPLAY recording from completed_tick=", debug_replay_snapshot_tick)

func _stop_and_save_debug_replay_recording() -> void:
	if !debug_replay_recording:
		return
	debug_replay_recording = false
	var replay_dir := _debug_replay_dir()
	var err := DirAccess.make_dir_recursive_absolute(replay_dir)
	if err != OK:
		print("MXT_DEBUG_REPLAY save failed: could not create ", replay_dir, " err=", err)
		return
	var input_b64: Array = []
	for input_bytes: PackedByteArray in debug_replay_inputs:
		input_b64.append(Marshalls.raw_to_base64(input_bytes))
	var replay := {
		"version": DEBUG_REPLAY_VERSION,
		"created_unix": Time.get_unix_time_from_system(),
		"track_index": _last_race_track_index,
		"track_name": _debug_replay_track_name(),
		"track_mxt": _debug_replay_track_path(),
		"settings": _last_race_settings.duplicate(true),
		"singleplayer_cpu_count": singleplayer_cpu_count,
		"spawn_seed": network_manager.spawn_seed,
		"snapshot_tick": debug_replay_snapshot_tick,
		"snapshot_state_b64": Marshalls.raw_to_base64(debug_replay_snapshot_state),
		"inputs_b64": input_b64,
	}
	var safe_track := _debug_replay_track_name().replace("/", "_").replace("\\", "_").replace(" ", "_")
	var path := replay_dir.path_join("mxt_%s_%s.json" % [safe_track, _debug_replay_make_stamp()])
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		print("MXT_DEBUG_REPLAY save failed: ", FileAccess.get_open_error())
		return
	file.store_string(JSON.stringify(replay, "\t"))
	file.close()
	print("MXT_DEBUG_REPLAY saved ", path, " frames=", debug_replay_inputs.size())

func _load_debug_replay_file(path: String) -> Dictionary:
	var resolved_path := path
	if resolved_path.begins_with("user://") or resolved_path.begins_with("res://"):
		resolved_path = ProjectSettings.globalize_path(resolved_path)
	elif !resolved_path.is_absolute_path():
		var project_dir := ProjectSettings.globalize_path("res://")
		var project_candidate := project_dir.path_join(resolved_path)
		var repo_candidate := project_dir.path_join("..").simplify_path().path_join(resolved_path)
		if FileAccess.file_exists(project_candidate):
			resolved_path = project_candidate
		elif FileAccess.file_exists(repo_candidate):
			resolved_path = repo_candidate
	if !FileAccess.file_exists(resolved_path):
		print("MXT_DEBUG_REPLAY load failed: file not found: ", resolved_path)
		return {}
	var text := FileAccess.get_file_as_string(resolved_path)
	var parsed = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		print("MXT_DEBUG_REPLAY load failed: JSON root is not a dictionary.")
		return {}
	if int(parsed.get("version", 0)) != DEBUG_REPLAY_VERSION:
		print("MXT_DEBUG_REPLAY load failed: unsupported version ", parsed.get("version", null))
		return {}
	return parsed

func _debug_replay_load_failed(message: String) -> void:
	print(message)
	if headless_mode:
		get_tree().quit()

func _load_and_start_debug_replay(path: String) -> void:
	var replay := _load_debug_replay_file(path)
	if replay.is_empty():
		if headless_mode:
			get_tree().quit()
		return
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	if game_sim.sim_started or singleplayer_mode:
		_return_to_menu()
	var track_index := _debug_replay_find_track_index(replay)
	if track_index < 0 or track_index >= tracks.size():
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: track not found for %s" % replay.get("track_name", ""))
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or settings.is_empty():
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: replay has no racer settings.")
		return
	var snapshot_tick := int(replay.get("snapshot_tick", -1))
	var snapshot_state := Marshalls.base64_to_raw(String(replay.get("snapshot_state_b64", "")))
	if snapshot_tick < 0 or snapshot_state.is_empty():
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: missing native snapshot.")
		return
	debug_replay_playback_inputs.clear()
	var inputs = replay.get("inputs_b64", [])
	if typeof(inputs) != TYPE_ARRAY:
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: inputs_b64 is not an array.")
		return
	for input_b64 in inputs:
		debug_replay_playback_inputs.append(Marshalls.base64_to_raw(String(input_b64)))
	if debug_replay_playback_inputs.is_empty():
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: replay has no input frames.")
		return

	singleplayer_mode = true
	_singleplayer_tick = 0
	network_manager.reset_race_state()
	network_manager.set_spawn_seed(int(replay.get("spawn_seed", 0)))
	var local_id := _local_player_id()
	network_manager.player_ids = [local_id]
	network_manager.spectator_ids = []
	singleplayer_cpu_count = maxi(0, settings.size() - 1)
	network_manager.set_singleplayer_cpu_count(singleplayer_cpu_count)
	network_manager.player_settings[local_id] = settings[0]
	var cpu_ids := network_manager.get_cpu_roster()
	for i in range(cpu_ids.size()):
		if i + 1 < settings.size():
			network_manager.player_settings[cpu_ids[i]] = settings[i + 1]

	_start_race(track_index, settings)
	if !game_sim.load_state_data(snapshot_tick, snapshot_state):
		_return_to_menu()
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: native snapshot could not be applied.")
		return
	_singleplayer_tick = snapshot_tick + 1
	network_manager.clients_server_tick = _singleplayer_tick
	debug_replay_playback_index = 0
	debug_replay_playback = true
	debug_replay_loaded_path = path
	$Control.visible = false
	lobby_control.visible = false
	print("MXT_DEBUG_REPLAY playback started ", path, " start_tick=", _singleplayer_tick, " frames=", debug_replay_playback_inputs.size())

@onready var world_environment: WorldEnvironment = $GameWorld/WorldEnvironment
@onready var track_floor: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/TrackFloor
@onready var track_clouds: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/TrackClouds
@onready var directional_light_3d: DirectionalLight3D = $GameWorld/DirectionalLight3D

func _start_race(track_index: int, settings: Array) -> void:
	if track_index < 0 or track_index >= tracks.size():
		return
	_close_settings_menus_for_race_start()
	_last_race_track_index = track_index
	_last_race_settings = settings.duplicate(true)
	active_stickers.clear()
	race_notification_hide_msec = 0
	local_elimination_spectator_active = false
	var info : Dictionary = tracks[track_index]
	# Load track metadata JSON and optional ground texture (ground.png) from the same folder
	current_track_meta = {}
	current_track_ground_image = null
	debug_track_mesh.visible = !auto_hide_track_visuals_mode
	track_floor.visible = !auto_hide_track_visuals_mode
	track_clouds.visible = !auto_hide_track_visuals_mode
	obj_container.visible = !auto_hide_track_visuals_mode
	var json_path = info["mxt"].get_basename() + ".json"
	if FileAccess.file_exists(json_path):
		var json_text := FileAccess.get_file_as_string(json_path)
		var parsed = JSON.parse_string(json_text)
		if typeof(parsed) == TYPE_DICTIONARY:
			current_track_meta = parsed
			RenderingServer.global_shader_parameter_set("fog_dist", current_track_meta.fog_distance)
			var floor_mat : ShaderMaterial = track_floor.get_active_material(0)
			var cloud_mat : ShaderMaterial = track_clouds.get_active_material(0)
			floor_mat.set_shader_parameter("albedo", current_track_meta.ground_color)
			cloud_mat.set_shader_parameter("albedo", current_track_meta.cloud_color)
			track_floor.position.z = -current_track_meta.ground_height
			track_clouds.position.z = -current_track_meta.cloud_height
			var sky_mat : ProceduralSkyMaterial = world_environment.environment.sky.sky_material
			sky_mat.sky_top_color = Color(current_track_meta.sky_top_color[0], current_track_meta.sky_top_color[1], current_track_meta.sky_top_color[2])
			sky_mat.sky_horizon_color = Color(current_track_meta.sky_horizon_color[0], current_track_meta.sky_horizon_color[1], current_track_meta.sky_horizon_color[2])
			sky_mat.ground_horizon_color = Color(current_track_meta.sky_horizon_color[0], current_track_meta.sky_horizon_color[1], current_track_meta.sky_horizon_color[2])
			sky_mat.ground_bottom_color = Color(current_track_meta.sky_ground_color[0], current_track_meta.sky_ground_color[1], current_track_meta.sky_ground_color[2])
			directional_light_3d.light_color = Color(current_track_meta.light_color[0], current_track_meta.light_color[1], current_track_meta.light_color[2])
			directional_light_3d.light_energy = current_track_meta.light_intensity
			world_environment.environment.ambient_light_color = Color(current_track_meta.ambient_color[0], current_track_meta.ambient_color[1], current_track_meta.ambient_color[2])
			world_environment.environment.ambient_light_energy = current_track_meta.ambient_intensity
			var track_dir = json_path.get_base_dir()
			var ground_path = track_dir.path_join("ground.png")
			if FileAccess.file_exists(ground_path):
				var bytes := FileAccess.get_file_as_bytes(ground_path)
				if bytes.size() > 0:
					var img := Image.load_from_file(ground_path)
					current_track_ground_image = img
					var floor_tex := ImageTexture.new()
					floor_tex.set_image(img)
					floor_mat.set_shader_parameter("texture_albedo", floor_tex)
					
	var chosen_defs : Array = []
	var parsed_settings : Array = []
	var racer_settings : Array = []
	var racer_ids : Array = []
	var racer_cpu_flags : Array = []
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.get_cpu_roster()
	var racer_roster_index := 0
	for i in range(settings.size()):
		var d = settings[i]
		if typeof(d) == TYPE_DICTIONARY:
			var ps := PlayerSettings.new()
			ps.from_dict(d)
			parsed_settings.append(ps)
			if !ps.spectator:
				racer_settings.append(ps)
				var def_res := load(ps.car_definition_path)
				if def_res != null:
					chosen_defs.append(def_res)
				if racer_roster_index < roster.size():
					var pid = roster[racer_roster_index]
					racer_ids.append(pid)
					racer_cpu_flags.append(cpu_ids.has(pid))
				racer_roster_index += 1
	var bumpers_enabled := bool(network_manager.race_options.get("bumpers", false))
	var bumper_def: CarDefinition = load(BUMPER_DEFINITION_PATH) if bumpers_enabled else null
	var render_defs := chosen_defs.duplicate()
	if bumper_def != null:
		for _slot in BUMPER_POOL_SIZE:
			render_defs.append(bumper_def)
	var local_id := _local_player_id()
	local_player_index = racer_ids.find(local_id)
	car_node_container.instantiate_cars(chosen_defs, racer_ids, local_id)
	nametag_names.clear()
	nametag_names.resize(racer_settings.size())
	for idx in racer_settings.size():
		var nametag_text: String = " " + racer_settings[idx].username + " "
		nametag_names[idx] = nametag_text
	for car: VisualCar in car_node_container.get_children():
		car.game_manager = self
	if car_node_container.local_visual_car != null and local_player_index >= 0 and local_player_index < racer_settings.size():
		car_node_container.local_visual_car.player_settings = racer_settings[local_player_index]
		if is_instance_valid(car_node_container.local_visual_car.name_label):
			car_node_container.local_visual_car.name_label.text = nametag_names[local_player_index]
	car_render_manager.multimesh_render_enabled = !auto_disable_car_multimesh_mode
	car_render_manager.configure(render_defs, car_node_container.get_children())
	for p in players:
		if p != null:
			p.queue_free()
	players.clear()
	if spectator_node:
		spectator_node.queue_free()
		spectator_node = null
	var car_props : Array = []
	var accel_settings_arr : Array = []
	for i in racer_settings.size():
		var is_cpu_racer = i < racer_cpu_flags.size() and racer_cpu_flags[i]
		if is_cpu_racer:
			players.append(null)
			continue
		var pc := player_scene.instantiate()
		pc.car_definition = chosen_defs[i]
		pc.accel_setting = racer_settings[i].accel_setting
		pc.player_settings = racer_settings[i]
		add_child(pc)
		players.append(pc)
	if local_player_index == -1:
		spectator_node = spectator_scene.instantiate()
		add_child(spectator_node)
	for n in chosen_defs.size():
		var def = chosen_defs[n]
		var bytes := FileAccess.get_file_as_bytes(def.car_definition)
		car_props.append(bytes)
		if n < racer_settings.size():
			accel_settings_arr.append(racer_settings[n].accel_setting)
		else:
			accel_settings_arr.append(1.0)
	var level_buffer := StreamPeerBuffer.new()
	level_buffer.data_array = FileAccess.get_file_as_bytes(info["mxt"])
	game_sim.car_node_container = car_node_container
	game_sim.spark_node_container = spark_node_container
	game_sim.set_render_node_effects_enabled(!auto_disable_node_effects_mode)
	game_sim.set_render_thruster_lights_enabled(!auto_disable_thruster_lights_mode)
	game_sim.set_car_render_manager(car_render_manager)
	# Ensure the C++ sim sees the shared spawn seed before instantiation
	game_sim.set_spawn_seed(network_manager.spawn_seed)
	game_sim.set_vehicle_restore_enabled(network_manager.is_vehicle_restore_enabled())
	if game_sim.has_method("set_bumpers_enabled"):
		game_sim.set_bumpers_enabled(bumpers_enabled and bumper_def != null)
	if game_sim.has_method("set_multiplayer_intro_camera_enabled"):
		game_sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode)
	game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
	game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
	_apply_grand_prix_ko_energy_bonuses(game_sim, racer_ids)
	network_manager.netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	if car_node_container.local_visual_car != null:
		game_sim.set_gameplay_camera(car_node_container.local_visual_car.car_camera, car_node_container.local_visual_car.owning_id)
		var local_hud := car_node_container.local_visual_car.race_hud
		if auto_disable_minimap_mode:
			var minimap_control := local_hud.get_node_or_null("MinimapControl") as Control
			if minimap_control != null:
				minimap_control.visible = false
				minimap_control.process_mode = Node.PROCESS_MODE_DISABLED
			var minimap_viewport := local_hud.get_node_or_null("MinimapControl/SubViewport") as SubViewport
			if minimap_viewport != null:
				minimap_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		if auto_disable_hud_mode or auto_hide_hud_only_mode:
			local_hud.visible = false
		if auto_disable_hud_mode or auto_disable_hud_process_only_mode:
			local_hud.process_mode = Node.PROCESS_MODE_DISABLED
		if auto_disable_hud_mode:
			car_node_container.local_visual_car.race_hud.process_mode = Node.PROCESS_MODE_DISABLED
			frame_time_label.visible = false
			rtt_label.visible = false
	_configure_nametag_pool()
	if network_manager.is_server:
		server_game_sim.car_node_container = car_node_container
		server_game_sim.spark_node_container = spark_node_container
		server_game_sim.set_car_render_manager(car_render_manager)
		server_game_sim.set_spawn_seed(network_manager.spawn_seed)
		server_game_sim.set_vehicle_restore_enabled(network_manager.is_vehicle_restore_enabled())
		if server_game_sim.has_method("set_bumpers_enabled"):
			server_game_sim.set_bumpers_enabled(bumpers_enabled and bumper_def != null)
		if server_game_sim.has_method("set_multiplayer_intro_camera_enabled"):
			server_game_sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode)
		server_game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
		server_game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
		_apply_grand_prix_ko_energy_bonuses(server_game_sim, racer_ids)
		network_manager.server_netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	if singleplayer_mode:
		game_sim.set_cpu_driver_manager(null)
		if network_manager.is_server:
			server_game_sim.set_cpu_driver_manager(null)
	elif network_manager.is_server:
		game_sim.set_cpu_driver_manager(null)
		server_game_sim.set_cpu_driver_manager(cpu_driver_manager)
	else:
		game_sim.set_cpu_driver_manager(null)
		server_game_sim.set_cpu_driver_manager(null)
	network_manager.game_sim = game_sim
	if network_manager.is_server:
		network_manager.server_game_sim = server_game_sim
	if !headless_mode:
		var obj_path = info["mxt"].get_basename() + ".obj"
		if ResourceLoader.exists(obj_path):
			var loaded_mesh: Mesh = load(obj_path)
			if loaded_mesh != null:
				var runtime_mesh: Mesh = loaded_mesh.duplicate(true)
				debug_track_mesh.mesh = runtime_mesh
				lobby_control.visible = false
				for i in runtime_mesh.get_surface_count():
					var mat := runtime_mesh.surface_get_material(i)
					if mat == null:
						continue
					var mat_name := mat.resource_name
					if mat_name == "track_surface":
						var new_road : ShaderMaterial = preload("res://asset/debug_track_mat.tres")
						runtime_mesh.surface_set_material(i, new_road)
					if mat_name == "track_rail":
						var new_road : ShaderMaterial = preload("res://asset/debug_rail_mat.tres")
						runtime_mesh.surface_set_material(i, new_road)
					if mat_name == "embed_dirt":
						var new_road : ShaderMaterial = preload("res://asset/dirt_mat.tres")
						runtime_mesh.surface_set_material(i, new_road)
					if mat_name == "embed_recharge":
						var new_road : ShaderMaterial = preload("res://asset/recharge_mat.tres")
						runtime_mesh.surface_set_material(i, new_road)
					if mat_name == "embed_ice":
						var new_road : ShaderMaterial = preload("res://asset/ice_mat.tres")
						runtime_mesh.surface_set_material(i, new_road)
				#elif mat.resource_name.find("track_surface") != -1:
					#var index := mat.resource_name.substr(14, -1).to_int()
					#var new_road : ShaderMaterial = preload("res://asset/debug_track_mat.tres")
					#new_road = new_road.duplicate()
					#new_road.set_shader_parameter("texture_albedo", ROAD_MATS.get(index - 1))
					#debug_track_mesh.mesh.surface_set_material(i, new_road)
		trigger_objects.clear()
		for trig in _parse_level_triggers(level_buffer.data_array):
			var scene = TRIGGER_SCENES.get(trig["type"], null)
			if scene:
				var inst:Node3D = scene.instantiate()
				inst.transform = trig["transform"]
				obj_container.add_child(inst)
				trigger_objects.append(inst)
	if !singleplayer_mode:
		if network_manager.is_server:
			network_manager.client_ready()
		else:
			network_manager.client_ready.rpc_id(1)

func _on_start_race_button_pressed() -> void:
	if network_manager.is_server:
		_close_settings_menus_for_race_start()
		network_manager.prepare_race_roster("start_button")
		var settings_array : Array = []
		var roster := network_manager.get_simulation_roster()
		for id in roster:
			var ps = network_manager.player_settings.get(id, null)
			if ps == null:
				var def_path = car_definitions[randi() % car_definitions.size()].resource_path
				ps = {"car_definition_path": def_path, "accel_setting": 1.0, "username": str(id)}
			settings_array.append(ps)
		var race_options := _build_lobby_race_options()
		race_options = _initialize_grand_prix_options(race_options, roster)
		var track_indices: Array = race_options.get("track_indices", [lobby_track_selector.selected])
		var first_track_index := lobby_track_selector.selected
		if !track_indices.is_empty():
			first_track_index = int(track_indices[0])
		network_manager.send_start_race(first_track_index, settings_array, race_options)

func _on_network_race_started(track_index: int, settings: Array) -> void:
	if headless_mode:
		_start_race(track_index, settings)
		network_manager.client_ready.rpc_id(1)
		return
	_start_race(track_index, settings)
	game_sim.set_sim_started(false)
	if network_manager.is_server:
		server_game_sim.set_sim_started(false)

func _on_network_race_finished() -> void:
	if headless_mode and network_manager.pending_next_race_track_index < 0:
		return
	race_finish_label.visible = false
	active_stickers.clear()
	if network_manager.pending_next_race_track_index >= 0:
		_transition_to_next_grand_prix_race()
	else:
		_return_to_lobby()

func _update_player_list() -> void:
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.get_cpu_roster()
	if lobby_player_list_container != null:
		var signature_parts := []
		for id in roster:
			signature_parts.append("%d:%s" % [int(id), _player_display_name(int(id))])
		var signature := "|".join(signature_parts)
		if signature == lobby_player_list_signature:
			return
		lobby_player_list_signature = signature
		for child in lobby_player_list_container.get_children():
			child.queue_free()
		for id in roster:
			var row := Button.new()
			row.text = _player_display_name(int(id))
			row.disabled = true
			row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			lobby_player_list_container.add_child(row)
		return
	player_list.clear()
	for id in roster:
		var name := str(id)
		if network_manager.player_settings.has(id):
			var ps = network_manager.player_settings[id]
			if typeof(ps) == TYPE_DICTIONARY and ps.has("username"):
				name = ps["username"]
		if cpu_ids.has(id):
			name = "[CPU] " + name
		player_list.add_item(name)

func _window_accepts_input() -> bool:
	if race_pause_open:
		return false
	var window := get_window()
	return window == null or window.has_focus()

func _reset_nametag_pool() -> void:
	for label in nametag_pool:
		if is_instance_valid(label):
			label.queue_free()
	for badge in placement_badge_pool:
		if is_instance_valid(badge):
			badge.queue_free()
	nametag_pool.clear()
	nametag_pool_car_indices.clear()
	nametag_pool_pending_indices.clear()
	nametag_best_distances.clear()
	nametag_best_indices.clear()
	placement_badge_pool.clear()

func _configure_nametag_pool() -> void:
	_reset_nametag_pool()
	var template_label: Label = null
	for car: VisualCar in car_node_container.get_children():
		if car != null and is_instance_valid(car.name_label):
			template_label = car.name_label
			break
	if template_label == null:
		return
	for slot in NAMETAG_VISIBLE_BUDGET:
		var label := template_label.duplicate() as Label
		label.name = "NametagPool%d" % slot
		label.visible = false
		label.modulate.a = 1.0
		add_child(label)
		nametag_pool.append(label)
		nametag_pool_car_indices.append(-1)
		nametag_pool_pending_indices.append(-1)
		nametag_best_distances.append(INF)
		nametag_best_indices.append(-1)
	for car: VisualCar in car_node_container.get_children():
		if car != null and is_instance_valid(car.name_label):
			car.name_label.queue_free()
	for place in TOP_PLACE_BADGE_TEXTURES.size():
		var badge := TextureRect.new()
		badge.name = "TopPlaceBadge%d" % (place + 1)
		badge.texture = TOP_PLACE_BADGE_TEXTURES[place]
		badge.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		badge.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		badge.custom_minimum_size = Vector2(56.0, 56.0)
		badge.size = Vector2(56.0, 56.0)
		badge.pivot_offset = badge.size * 0.5
		badge.visible = false
		add_child(badge)
		placement_badge_pool.append(badge)

func _nametag_best_contains(car_index: int) -> bool:
	for slot in NAMETAG_VISIBLE_BUDGET:
		if nametag_best_indices[slot] == car_index:
			return true
	return false

func _nametag_pool_has_car(car_index: int) -> bool:
	for slot in NAMETAG_VISIBLE_BUDGET:
		if nametag_pool_car_indices[slot] == car_index or nametag_pool_pending_indices[slot] == car_index:
			return true
	return false

func _nametag_assign(label: Label, slot: int, car_index: int) -> void:
	label.text = nametag_names[car_index] if car_index < nametag_names.size() else ""
	label.size = label.get_combined_minimum_size()
	label.modulate.a = 0.0
	label.visible = true
	nametag_pool_car_indices[slot] = car_index
	nametag_pool_pending_indices[slot] = -1

func _car_index_for_player_id(player_id: int) -> int:
	var car_index := 0
	for car in car_node_container.get_children():
		if car is VisualCar:
			if car.owning_id == player_id:
				return car_index
			car_index += 1
	return -1

func _update_top_place_badges(active_camera: Camera3D, camera_position: Vector3, camera_right: Vector3, camera_up: Vector3) -> void:
	for badge in placement_badge_pool:
		badge.visible = false
	if singleplayer_mode or game_sim == null or placement_badge_pool.is_empty():
		return
	var local_id := _local_player_id()
	var order: Array = game_sim.get_race_order()
	var badge_slot := 0
	for rank in mini(order.size(), TOP_PLACE_BADGE_TEXTURES.size()):
		var player_id := int(order[rank])
		if player_id == local_id:
			continue
		var car_index := _car_index_for_player_id(player_id)
		if car_index < 0:
			continue
		var render_transform: Transform3D = game_sim.get_car_render_transform(car_index)
		var world_pos := render_transform.origin
		if camera_position.distance_squared_to(world_pos) > NAMETAG_MAX_DISTANCE_SQ or !active_camera.is_position_in_frustum(world_pos):
			continue
		var badge := placement_badge_pool[badge_slot]
		badge.texture = TOP_PLACE_BADGE_TEXTURES[rank]
		badge.visible = true
		badge.position = active_camera.unproject_position(
			world_pos + camera_right * 1.5 + camera_up * 2.35
		) + Vector2(42.0, -132.0)
		badge_slot += 1
		if badge_slot >= placement_badge_pool.size():
			return

func _update_nametags(active_camera: Camera3D, delta: float) -> void:
	if auto_disable_hud_mode or active_camera == null or nametag_pool.is_empty():
		return
	var camera_position := active_camera.global_position
	var camera_right := active_camera.global_basis.x
	var camera_up := active_camera.global_basis.y
	var car_count := nametag_names.size()
	for slot in NAMETAG_VISIBLE_BUDGET:
		nametag_best_distances[slot] = INF
		nametag_best_indices[slot] = -1
	for car_index in car_count:
		if car_index == local_player_index:
			continue
		var render_transform: Transform3D = game_sim.get_car_render_transform(car_index)
		var world_pos: Vector3 = render_transform.origin
		var distance_sq := camera_position.distance_squared_to(world_pos)
		if distance_sq > NAMETAG_MAX_DISTANCE_SQ or !active_camera.is_position_in_frustum(world_pos):
			continue
		if distance_sq >= nametag_best_distances[NAMETAG_VISIBLE_BUDGET - 1]:
			continue
		var insert_at := NAMETAG_VISIBLE_BUDGET - 1
		while insert_at > 0 and distance_sq < nametag_best_distances[insert_at - 1]:
			nametag_best_distances[insert_at] = nametag_best_distances[insert_at - 1]
			nametag_best_indices[insert_at] = nametag_best_indices[insert_at - 1]
			insert_at -= 1
		nametag_best_distances[insert_at] = distance_sq
		nametag_best_indices[insert_at] = car_index
	for slot in NAMETAG_VISIBLE_BUDGET:
		var label := nametag_pool[slot]
		var car_index := nametag_pool_car_indices[slot]
		if car_index < 0:
			label.visible = false
			label.modulate.a = 0.0
			continue
		if car_index >= car_count:
			label.visible = false
			label.modulate.a = 0.0
			nametag_pool_car_indices[slot] = -1
			nametag_pool_pending_indices[slot] = -1
			continue
		var render_transform: Transform3D = game_sim.get_car_render_transform(car_index)
		var world_pos: Vector3 = render_transform.origin
		if !active_camera.is_position_in_frustum(world_pos) or camera_position.distance_squared_to(world_pos) > NAMETAG_MAX_DISTANCE_SQ:
			label.visible = false
			label.modulate.a = 0.0
			nametag_pool_car_indices[slot] = -1
			nametag_pool_pending_indices[slot] = -1
			continue
		if !_nametag_best_contains(car_index):
			label.modulate.a = maxf(0.0, label.modulate.a - delta * 12.0)
			if label.modulate.a <= 0.0:
				label.visible = false
				nametag_pool_car_indices[slot] = -1
			continue
		label.visible = true
		label.modulate.a = minf(1.0, label.modulate.a + delta * 20.0)
		label.position = active_camera.unproject_position(
			world_pos + camera_right * 1.5 + camera_up * 1.5
		) + Vector2(72, -90)
	_update_top_place_badges(active_camera, camera_position, camera_right, camera_up)
	for desired_slot in NAMETAG_VISIBLE_BUDGET:
		var desired_car_index := nametag_best_indices[desired_slot]
		if desired_car_index < 0 or _nametag_pool_has_car(desired_car_index):
			continue
		var target_pool_slot := -1
		for slot in NAMETAG_VISIBLE_BUDGET:
			if nametag_pool_car_indices[slot] == -1:
				target_pool_slot = slot
				break
		if target_pool_slot == -1:
			for slot in NAMETAG_VISIBLE_BUDGET:
				if nametag_pool_pending_indices[slot] == -1 and !_nametag_best_contains(nametag_pool_car_indices[slot]):
					nametag_pool_pending_indices[slot] = desired_car_index
					target_pool_slot = slot
					break
		if target_pool_slot == -1:
			continue
		var label := nametag_pool[target_pool_slot]
		if nametag_pool_car_indices[target_pool_slot] == -1 or label.modulate.a <= 0.0:
			_nametag_assign(label, target_pool_slot, desired_car_index)
	for slot in NAMETAG_VISIBLE_BUDGET:
		var pending_index := nametag_pool_pending_indices[slot]
		if pending_index < 0:
			continue
		var label := nametag_pool[slot]
		label.modulate.a = maxf(0.0, label.modulate.a - delta * 12.0)
		if label.modulate.a <= 0.0:
			_nametag_assign(label, slot, pending_index)
	for slot in NAMETAG_VISIBLE_BUDGET:
		var car_index := nametag_pool_car_indices[slot]
		if car_index < 0:
			continue
		var label := nametag_pool[slot]
		var render_transform: Transform3D = game_sim.get_car_render_transform(car_index)
		var world_pos: Vector3 = render_transform.origin
		label.visible = true
		label.position = active_camera.unproject_position(
			world_pos + camera_right * 1.5 + camera_up * 1.5
		) + Vector2(72, -90)

func _print_render_profile_summary() -> void:
	if !auto_render_profile_mode or render_profile_frames <= 0:
		return
	print("MXT_RENDER_PROFILE frames=", render_profile_frames,
		" physics_us=", int(render_profile_physics_us / render_profile_frames),
		" tick_us=", int(render_profile_tick_us / render_profile_frames),
		" render_us=", int(render_profile_render_us / render_profile_frames),
		" nametag_us=", int(render_profile_nametag_us / render_profile_frames),
		" local_visual_us=", int(render_profile_local_visual_us / render_profile_frames),
		" process_us=", int(render_profile_process_us / render_profile_frames),
		" visuals_only_us=", int(render_profile_visuals_only_us / render_profile_frames))
	print(game_sim.get_render_profile_string())

func _physics_process(delta: float) -> void:
	if auto_track_editor_mode:
		return
	if headless_mode:
		if singleplayer_mode and game_sim.sim_started:
			_simulate_singleplayer_tick()
			if auto_quit_after_frames >= 0 and _singleplayer_tick >= auto_quit_after_frames:
				get_tree().quit()
			return
		if network_manager.has_network_peer():
			var pi := _generate_random_input()
			network_manager.set_local_input(pi.serialize())
			network_manager.collect_client_inputs()
		return
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if lobby_control.visible:
		_update_player_list()
		_update_lobby_chibi_cars(delta)
		var can_edit_cpu := network_manager.is_server and !network_manager.race_active
		var missing_selected_tracks := lobby_grand_prix_track_sequence.is_empty()
		add_cpu_button.disabled = !can_edit_cpu
		remove_cpu_button.disabled = !can_edit_cpu or network_manager.get_cpu_roster().is_empty()
		start_race_button.disabled = !can_edit_cpu or tracks.is_empty() or missing_selected_tracks
		lobby_track_selector.disabled = !can_edit_cpu
		if lobby_game_mode_choice != null:
			lobby_game_mode_choice.disabled = !can_edit_cpu
		if lobby_vehicle_restore_toggle != null:
			lobby_vehicle_restore_toggle.disabled = !can_edit_cpu
		if lobby_bumpers_toggle != null:
			lobby_bumpers_toggle.disabled = !can_edit_cpu
		if lobby_stage_button_container != null:
			for child in lobby_stage_button_container.get_children():
				var button := child as Button
				if button != null:
					button.disabled = !can_edit_cpu
		if lobby_stage_preview_container != null:
			for child in lobby_stage_preview_container.get_children():
				var button := child as Button
				if button != null:
					button.disabled = !can_edit_cpu
	if game_sim.sim_started:
		var profile_physics_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		var local_pi := PlayerInputClass.new()
		if !_should_suppress_local_race_input() and _window_accepts_input() and players.size() > local_player_index:
			var controller = players[local_player_index]
			if controller != null:
				local_pi = controller.get_input()
		var input_bytes := local_pi.serialize()
		if singleplayer_mode:
			var profile_tick_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
			_simulate_singleplayer_tick(input_bytes)
			if auto_render_profile_mode:
				render_profile_tick_us += Time.get_ticks_usec() - profile_tick_start
			if auto_quit_after_frames >= 0 and _singleplayer_tick >= auto_quit_after_frames:
				_print_render_profile_summary()
				get_tree().quit()
				return
		else:
			network_manager.set_local_input(input_bytes)
			if network_manager.is_server:
				_simulate_host_frame(input_bytes)
			else:
				_simulate_single_tick()
		_consume_authoritative_race_events()
		var profile_render_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		game_sim.render_gamesim()
		if auto_render_profile_mode:
			render_profile_render_us += Time.get_ticks_usec() - profile_render_start
		var profile_nametag_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		_update_nametags(get_viewport().get_camera_3d(), delta)
		if auto_render_profile_mode:
			render_profile_nametag_us += Time.get_ticks_usec() - profile_nametag_start
		var profile_local_visual_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		if car_node_container.local_visual_car != null:
			car_node_container.local_visual_car.just_rendered()
		if auto_render_profile_mode:
			render_profile_local_visual_us += Time.get_ticks_usec() - profile_local_visual_start
			render_profile_physics_us += Time.get_ticks_usec() - profile_physics_start
			render_profile_frames += 1
		_check_race_finished()

func _simulate_singleplayer_tick(input_bytes: PackedByteArray = PackedByteArray()):
	var start_time := Time.get_ticks_usec()
	if debug_replay_playback:
		if debug_replay_playback_index >= debug_replay_playback_inputs.size():
			debug_replay_playback = false
			game_sim.set_sim_started(false)
			print("MXT_DEBUG_REPLAY playback complete ", debug_replay_loaded_path, " end_tick=", _singleplayer_tick)
			if headless_mode:
				get_tree().quit()
			return
		input_bytes = (debug_replay_playback_inputs[debug_replay_playback_index] as PackedByteArray).duplicate()
		debug_replay_playback_index += 1
	if input_bytes.is_empty():
		var local_pi := PlayerInputClass.new()
		if auto_accelerate_mode:
			local_pi.accelerate = 1.0
		var accepts_input := _window_accepts_input()
		if !auto_accelerate_mode and !_should_suppress_local_race_input() and accepts_input and players.size() > local_player_index:
			var controller = players[local_player_index]
			if controller != null:
				local_pi = controller.get_input()
		input_bytes = local_pi.serialize()
	if debug_replay_recording:
		debug_replay_inputs.append(input_bytes.duplicate())
	_dump_offline_auth_input_sample(input_bytes)
	_dump_offline_state_sample()
	game_sim.tick_singleplayer(_local_player_id(), input_bytes)
	_singleplayer_tick += 1
	if debug_bumper_smoke_mode and _singleplayer_tick % 120 == 0 and game_sim.has_method("get_bumper_debug_string"):
		print("MXT_BUMPER_SMOKE tick=", _singleplayer_tick, " ", game_sim.get_bumper_debug_string())
	# Update HUD timing using the same field clients use
	network_manager.clients_server_tick = _singleplayer_tick
	network_manager.rollback_frametime_us = Time.get_ticks_usec() - start_time

func _dump_offline_auth_input_sample(local_input_bytes: PackedByteArray) -> void:
	if !network_manager.dump_auth_input_samples:
		return
	if game_sim == null:
		return
	var roster := network_manager.get_simulation_roster()
	if roster.is_empty():
		return
	var cpu_ids := network_manager.get_cpu_roster()
	var local_id := _local_player_id()
	for id in roster:
		var input_bytes := network_manager.NEUTRAL_INPUT_BYTES
		if cpu_ids.has(id):
			input_bytes = game_sim.get_native_cpu_input_for_tick(int(id), _singleplayer_tick)
		elif int(id) == local_id:
			input_bytes = local_input_bytes
		network_manager.netcode_session.store_authoritative_input(_singleplayer_tick, int(id), input_bytes)
	network_manager.netcode_session.build_authoritative_input_packet(
		_singleplayer_tick,
		network_manager.AUTH_INPUT_REDUNDANCY_FRAMES
	)

func _dump_offline_state_sample() -> void:
	if !network_manager.dump_state_samples:
		return
	if game_sim == null:
		return
	if _singleplayer_tick % network_manager.STATE_BROADCAST_INTERVAL_TICKS != 0:
		return
	var state := game_sim.get_state_data(_singleplayer_tick)
	network_manager.dump_state_sample(
		state,
		_singleplayer_tick,
		network_manager.get_simulation_roster().size()
	)

func _simulate_host_frame(local_input_bytes: PackedByteArray):
	var loops := 0
	const MAX_TICKS_PER_FRAME := 120
	while loops < MAX_TICKS_PER_FRAME:
		network_manager.set_local_input(local_input_bytes)
		var server_inputs := network_manager.collect_server_inputs()
		if server_inputs.is_empty():
			break
		network_manager.post_tick()
		loops += 1
	network_manager.collect_client_inputs()

func _simulate_single_tick():
	var loops := 0
	const MAX_TICKS_PER_FRAME := 120
	while loops < MAX_TICKS_PER_FRAME:
		var frame_inputs := network_manager.collect_client_inputs()
		if frame_inputs.is_empty():
			return
		if network_manager.is_server:
			var server_inputs := network_manager.collect_server_inputs()
			if !server_inputs.is_empty():
				network_manager.post_tick()
		else:
			network_manager.post_tick()
		loops += 1
		if network_manager.is_server or network_manager.local_tick >= network_manager.clients_target_tick:
			return

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F3:
		var profile := game_sim.get_phase_profile_string()
		var render_profile := game_sim.get_render_profile_string()
		DisplayServer.clipboard_set(profile + "\n" + render_profile)
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F5:
		if debug_replay_recording:
			_stop_and_save_debug_replay_recording()
		else:
			_start_debug_replay_recording()
		get_viewport().set_input_as_handled()
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F8:
		var replay_path := DisplayServer.clipboard_get().strip_edges()
		if replay_path != "":
			_load_and_start_debug_replay(replay_path)
		get_viewport().set_input_as_handled()
	if game_sim.sim_started and event.is_action_pressed("ui_cancel"):
		if race_pause_open:
			_close_race_pause_menu()
		else:
			_open_race_pause_menu()
		get_viewport().set_input_as_handled()

func _return_to_menu() -> void:
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	debug_replay_playback = false
	_close_race_pause_menu()
	race_finish_label.visible = false
	active_stickers.clear()
	_reset_nametag_pool()
	var was_server := network_manager.is_server
	network_manager.disconnect_from_server()
	game_sim.destroy_gamesim()
	if was_server:
		server_game_sim.destroy_gamesim()
	network_manager.game_sim = null
	network_manager.server_game_sim = null
	for child in car_node_container.get_children():
		child.queue_free()
	for obj in trigger_objects:
		obj.queue_free()
	trigger_objects.clear()
	for p in players:
		if p != null:
			p.queue_free()
	players.clear()
	if spectator_node:
		spectator_node.queue_free()
		spectator_node = null
	local_elimination_spectator_active = false
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	singleplayer_mode = false
	_singleplayer_tick = 0
	$Control.visible = true
	lobby_control.visible = false

func _return_to_lobby() -> void:
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	debug_replay_playback = false
	_close_race_pause_menu()
	_reset_nametag_pool()
	game_sim.destroy_gamesim()
	race_finish_label.visible = false
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	for child in car_node_container.get_children():
		if child != null:
			child.queue_free()
	for obj in trigger_objects:
		if obj != null:
			obj.queue_free()
	trigger_objects.clear()
	for p in players:
		if p != null:
			p.queue_free()
	players.clear()
	if spectator_node:
		spectator_node.queue_free()
		spectator_node = null
	local_elimination_spectator_active = false
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	lobby_control.visible = true
	network_manager.flush_waiting_peers()
	network_manager.reset_race_state(true)
	singleplayer_mode = false
	_singleplayer_tick = 0

func _teardown_race_world_for_transition() -> void:
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	debug_replay_playback = false
	_close_race_pause_menu()
	_reset_nametag_pool()
	game_sim.destroy_gamesim()
	race_finish_label.visible = false
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	for child in car_node_container.get_children():
		if child != null:
			child.queue_free()
	for obj in trigger_objects:
		if obj != null:
			obj.queue_free()
	trigger_objects.clear()
	for p in players:
		if p != null:
			p.queue_free()
	players.clear()
	if spectator_node:
		spectator_node.queue_free()
		spectator_node = null
	local_elimination_spectator_active = false
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	lobby_control.visible = false
	singleplayer_mode = false
	_singleplayer_tick = 0

func _transition_to_next_grand_prix_race() -> void:
	var next_track_index := network_manager.pending_next_race_track_index
	var next_settings := network_manager.pending_next_race_settings.duplicate(true)
	var next_options := network_manager.pending_next_race_options.duplicate(true)
	_teardown_race_world_for_transition()
	network_manager.reset_race_state(true)
	network_manager.race_options = next_options
	_apply_grand_prix_eliminations(next_options)
	network_manager.start_race(next_track_index, next_settings, next_options)
	if network_manager.is_server and network_manager.player_ids.size() <= 1:
		network_manager.begin_simulation()

func _apply_grand_prix_eliminations(options: Dictionary) -> void:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	if eliminated_ids.is_empty():
		return
	for eliminated_id in eliminated_ids:
		var id := int(eliminated_id)
		if network_manager.player_ids.has(id):
			network_manager.player_ids.erase(id)
			if !network_manager.spectator_ids.has(id):
				network_manager.spectator_ids.append(id)
		if network_manager.cpu_player_ids.has(id):
			network_manager.cpu_player_ids.erase(id)
			network_manager.cpu_player_settings.erase(id)

func _lookup_id_value(dict: Dictionary, id: int, fallback):
	if dict.has(id):
		return dict[id]
	var id_string := str(id)
	if dict.has(id_string):
		return dict[id_string]
	return fallback

func _apply_grand_prix_ko_energy_bonuses(sim: GameSim, racer_ids: Array) -> void:
	if sim == null or !network_manager.is_grand_prix_enabled():
		return
	if !sim.has_method("set_player_ko_energy_bonus"):
		return
	var bonuses: Dictionary = network_manager.race_options.get("grand_prix_ko_energy_bonuses", {})
	if bonuses.is_empty():
		return
	for id_value in racer_ids:
		var id := int(id_value)
		var bonus := float(_lookup_id_value(bonuses, id, 0.0))
		if bonus > 0.0:
			sim.set_player_ko_energy_bonus(id, bonus)

func _capture_grand_prix_ko_energy_bonuses(sim: GameSim) -> Dictionary:
	var bonuses := {}
	if sim == null or !sim.has_method("get_player_ko_energy_bonus"):
		return bonuses
	for id_value in network_manager.get_simulation_roster():
		var id := int(id_value)
		bonuses[id] = float(sim.get_player_ko_energy_bonus(id))
	return bonuses

func _record_grand_prix_race_results(sim: GameSim) -> void:
	if !network_manager.is_server or !network_manager.is_grand_prix_enabled():
		return
	var options := network_manager.race_options.duplicate(true)
	var current_track_index := int(options.get("grand_prix_current_track", 0))
	if int(options.get("grand_prix_recorded_track", -1)) == current_track_index:
		return
	var points: Dictionary = options.get("grand_prix_points", {})
	var human_racers := network_manager.race_player_ids.duplicate(true)
	var racer_count := human_racers.size()
	for id_value in human_racers:
		var id := int(id_value)
		var total := int(_lookup_id_value(points, id, 0))
		if network_manager.player_finish_placements.has(id):
			var place := int(network_manager.player_finish_placements[id])
			total += maxi(0, racer_count - place + 1)
		points[id] = total
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	if !network_manager.is_vehicle_restore_enabled():
		for id_value in network_manager.player_eliminations.keys():
			var id := int(id_value)
			if !eliminated_ids.has(id):
				eliminated_ids.append(id)
	options["grand_prix_points"] = points
	options["grand_prix_eliminated_ids"] = eliminated_ids
	options["grand_prix_ko_energy_bonuses"] = _capture_grand_prix_ko_energy_bonuses(sim)
	options["grand_prix_recorded_track"] = current_track_index
	network_manager.race_options = options
	network_manager.send_race_options(options)

func _build_next_grand_prix_settings(options: Dictionary) -> Array:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var settings := []
	for id_value in network_manager.get_simulation_roster():
		var id := int(id_value)
		if eliminated_ids.has(id):
			continue
		var ps = network_manager.player_settings.get(id, null)
		if ps != null:
			settings.append(ps)
	return settings

func _has_active_human_grand_prix_racer(options: Dictionary) -> bool:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	for id_value in network_manager.player_ids:
		if !eliminated_ids.has(int(id_value)):
			return true
	return false

func _finish_or_advance_grand_prix(finish_sim: GameSim) -> void:
	_record_grand_prix_race_results(finish_sim)
	if !network_manager.is_grand_prix_enabled():
		network_manager.send_end_race()
		return
	var options := network_manager.race_options.duplicate(true)
	var track_indices: Array = options.get("track_indices", [])
	var current_index := int(options.get("grand_prix_current_track", 0))
	var next_index := current_index + 1
	if next_index >= track_indices.size() or !_has_active_human_grand_prix_racer(options):
		network_manager.send_end_race()
		return
	options["grand_prix_current_track"] = next_index
	var next_track_index := int(track_indices[next_index])
	var next_settings := _build_next_grand_prix_settings(options)
	var seed := randi()
	network_manager.set_spawn_seed.rpc(seed)
	network_manager.set_spawn_seed(seed)
	network_manager.send_end_race(next_track_index, next_settings, options)

func _check_race_finished() -> void:
	if !game_sim.sim_started:
		return
	if !network_manager.is_server and !singleplayer_mode:
		return
	var racer_ids := network_manager.get_simulation_roster()
	var human_racer_ids := network_manager.race_player_ids.duplicate(true)
	if human_racer_ids.is_empty():
		human_racer_ids = network_manager.player_ids.duplicate(true)
	var finish_watch_ids := human_racer_ids if !human_racer_ids.is_empty() else racer_ids
	var all_done := true
	var finish_sim := server_game_sim if network_manager.is_server and server_game_sim != null else game_sim
	for racer_id in racer_ids:
		var watch_racer := finish_watch_ids.has(racer_id)
		if network_manager._disconnected_during_race.has(racer_id):
			continue
		if network_manager.player_finish_times.has(racer_id):
			continue
		if network_manager.player_eliminations.has(racer_id):
			continue
		var finished := false
		var eliminated := false
		if finish_sim != null and finish_sim.has_method("is_player_race_finished"):
			finished = finish_sim.is_player_race_finished(racer_id)
		if !network_manager.is_vehicle_restore_enabled() and finish_sim != null and finish_sim.has_method("is_player_race_eliminated"):
			eliminated = finish_sim.is_player_race_eliminated(racer_id)
		else:
			for car in car_node_container.get_children():
				if car is VisualCar and car.owning_id == racer_id:
					finished = (car.machine_state & VisualCar.FZ_MS.COMPLETEDRACE_1_Q) != 0
					eliminated = !network_manager.is_vehicle_restore_enabled() and (car.machine_state & (VisualCar.FZ_MS.ZEROHP | VisualCar.FZ_MS.FALLOUT)) != 0
					break
		if finished:
			if network_manager.is_server:
				network_manager.send_player_finished(racer_id, network_manager.server_tick)
			else:
				network_manager.record_player_finished(racer_id, network_manager.clients_server_tick)
		elif eliminated:
			if network_manager.is_server:
				network_manager.send_player_eliminated(racer_id, network_manager.server_tick)
			elif singleplayer_mode:
				network_manager.record_player_eliminated(racer_id, network_manager.clients_server_tick)
		elif watch_racer:
			all_done = false
	if network_manager.is_server:
		if all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()
				network_manager.send_race_finish_time(network_manager.net_race_finish_time)
				_record_grand_prix_race_results(finish_sim)
				_show_race_results_summary()
			if Time.get_ticks_msec() > network_manager.net_race_finish_time + 10000:
				_finish_or_advance_grand_prix(finish_sim)
				race_finish_label.visible = false
	else:
		if singleplayer_mode and all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()
				_show_race_results_summary()

@onready var obj_camera: Camera3D = get_node_or_null("GameWorld/ObjViewport/ObjCamera") as Camera3D
@onready var outline_camera: Camera3D = get_node_or_null("GameWorld/OutlineViewport/OutlineCamera") as Camera3D
const FULL_EFFECT_CAR_BUDGET := 10

func _update_car_effect_tiers(active_camera: Camera3D) -> void:
	var ranked_cars: Array = []
	for car: VisualCar in car_node_container.get_children():
		if car == null:
			continue
		var dist_sq := active_camera.global_position.distance_squared_to(car.car_transform.global_position)
		ranked_cars.append([dist_sq, car])

	ranked_cars.sort_custom(func(a, b): return a[0] < b[0])

	var full_budget_remaining := FULL_EFFECT_CAR_BUDGET
	for ranked in ranked_cars:
		var car: VisualCar = ranked[1]
		if car.local_visual_enabled:
			car.set_effect_tier(VisualCar.EffectTier.FULL)
			full_budget_remaining -= 1
			continue
		if full_budget_remaining > 0:
			car.set_effect_tier(VisualCar.EffectTier.FULL)
			full_budget_remaining -= 1
		else:
			car.set_effect_tier(VisualCar.EffectTier.THRUSTER_ONLY)

func _process(delta: float) -> void:
	var profile_process_start := Time.get_ticks_usec() if auto_render_profile_mode and game_sim.sim_started else 0
	var now_msec := Time.get_ticks_msec()
	for id in active_stickers.keys():
		var data: Dictionary = active_stickers[id]
		if now_msec > int(data.get("expires", 0)):
			active_stickers.erase(id)
	if race_finish_label.visible and race_notification_hide_msec > 0 and now_msec > race_notification_hide_msec and network_manager.net_race_finish_time == -1:
		race_finish_label.visible = false
		race_notification_hide_msec = 0
	frame_time_label.text = str(network_manager.rollback_frametime_us) + "us"
	rtt_label.text = str(roundi(network_manager.rtt_s * 1000.0)) + "ms"
	if game_sim.sim_started and network_manager.net_race_finish_time != -1:
		_show_race_results_summary()
	if game_sim.sim_started:
		var profile_visuals_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		game_sim.render_gamesim_visuals_only(delta)
		if auto_render_profile_mode:
			render_profile_visuals_only_us += Time.get_ticks_usec() - profile_visuals_start
			render_profile_process_us += Time.get_ticks_usec() - profile_process_start
