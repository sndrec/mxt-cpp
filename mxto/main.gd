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

@onready var obj_viewport: SubViewport = get_node_or_null("GameWorld/ObjViewport") as SubViewport
@onready var outline_viewport: SubViewport = get_node_or_null("GameWorld/OutlineViewport") as SubViewport
@onready var obj_viewport_texture: ColorRect = get_node_or_null("GameWorld/ObjViewportTexture") as ColorRect
@onready var outline_viewport_texture: ColorRect = get_node_or_null("GameWorld/OutlineViewportTexture") as ColorRect

const PlayerInputClass = preload("res://player/player_input.gd")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const FinishMedalScene: PackedScene = preload("res://ui/finish_medal.tscn")
const KoMedalScene: PackedScene = preload("res://ui/ko_medal.tscn")

var tracks: Array = []
var car_definitions: Array = []
var players: Array = []
var player_scene := preload("res://player/player_controller.tscn")
var spectator_scene := preload("res://player/spectator.tscn")
var local_player_index: int = 0
var headless_mode: bool = false
var trigger_objects: Array = []
var spectator_node: Node3D
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
var auto_singleplayer_mode: bool = false
var auto_track_editor_mode: bool = false
var auto_accelerate_mode: bool = false
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

const OUTER_PROFILE_WINDOW := 360
const OUTER_PROFILE_FIELDS := [
	"physics_total",
	"local_input",
	"simulate_call",
	"sp_build_inputs",
	"sp_tick_gamesim",
	"render_gamesim",
	"visual_just_rendered",
	"race_finish",
	"process_total",
	"process_labels",
	"process_native_visual",
	"process_effect_tiers",
]
var outer_profile_samples: Array = []
var outer_profile_sums: Dictionary = {}
var outer_profile_cursor := 0
var outer_profile_count := 0
var _last_sp_build_inputs_us := 0
var _last_sp_tick_gamesim_us := 0
var _last_simulate_call_us := 0
var _last_process_total_us := 0
var _last_process_labels_us := 0
var _last_process_native_visual_us := 0
var _last_process_effect_tiers_us := 0
var race_pause_root: Control
var race_pause_title: Label
var race_pause_resume_button: Button
var race_pause_disconnect_button: Button
var race_pause_lobby_button: Button
var race_pause_open := false
var debug_rail_trace_requested := false
var active_stickers := {}
var race_notification_hide_msec := 0
var race_medals: Array[Control] = []

func _record_outer_profile(sample: Dictionary) -> void:
	if outer_profile_sums.is_empty():
		for field in OUTER_PROFILE_FIELDS:
			outer_profile_sums[field] = 0
	if outer_profile_count == OUTER_PROFILE_WINDOW:
		var old: Dictionary = outer_profile_samples[outer_profile_cursor]
		for field in OUTER_PROFILE_FIELDS:
			outer_profile_sums[field] -= int(old.get(field, 0))
	else:
		outer_profile_samples.append({})
		outer_profile_count += 1
	for field in OUTER_PROFILE_FIELDS:
		var value := int(sample.get(field, 0))
		outer_profile_sums[field] += value
	outer_profile_samples[outer_profile_cursor] = sample.duplicate()
	outer_profile_cursor = (outer_profile_cursor + 1) % OUTER_PROFILE_WINDOW

func get_outer_profile_string() -> String:
	var count := maxi(outer_profile_count, 1)
	var out := "MXT_OUTER_AVG_US frames=%d" % outer_profile_count
	for field in OUTER_PROFILE_FIELDS:
		out += " %s=%d" % [field, int(int(outer_profile_sums.get(field, 0)) / count)]
	return out

func _ready() -> void:
	#obj_viewport_texture.texture = obj_viewport.get_texture()
	#outline_viewport_texture.texture = outline_viewport.get_texture()
	car_render_manager = CarRenderManagerClass.new()
	car_render_manager.name = "CarRenderManager"
	$GameWorld.add_child(car_render_manager)
	randomize()
	_load_tracks()
	_load_car_definitions()
	network_manager.race_started.connect(_on_network_race_started)
	network_manager.race_finished.connect(_on_network_race_finished)
	network_manager.race_event.connect(_on_race_event)
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
	if args.has("--host") or user_args.has("--host"):
		call_deferred("_auto_host")
	auto_singleplayer_mode = args.has("--auto-singleplayer") or user_args.has("--auto-singleplayer")
	auto_accelerate_mode = args.has("--auto-accelerate") or user_args.has("--auto-accelerate")
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
	if debug_replay_autoload_path != "":
		call_deferred("_load_and_start_debug_replay", debug_replay_autoload_path)
	elif auto_track_editor_mode:
		call_deferred("_on_track_editor_button_pressed")
	elif auto_singleplayer_mode:
		call_deferred("_on_singleplayer_button_pressed")
	if headless_mode and !auto_track_editor_mode and !auto_singleplayer_mode and debug_replay_autoload_path == "":
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
	return int(clamp(float(args[cpu_idx + 1]), 0.0, 999.0))

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
				break

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
		if dir.current_is_dir() and !folder.begins_with("."):
			var def_path := "res://vehicle/asset/%s/definition.tres" % folder
			if ResourceLoader.exists(def_path):
				var def_res := load(def_path)
				if def_res != null:
					car_definitions.append(def_res)
		folder = dir.get_next()
	dir.list_dir_end()

func _on_start_button_pressed() -> void:
	var err := network_manager.host()
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
	network_manager.multiplayer.connected_to_server.connect(
		func():
			network_manager.send_player_settings(settings_dict),
		Object.CONNECT_ONE_SHOT)
	network_manager.join(ip_field.text)
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

func _build_race_pause_menu() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 100
	add_child(layer)

	race_pause_root = Control.new()
	race_pause_root.visible = false
	race_pause_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	race_pause_root.mouse_filter = Control.MOUSE_FILTER_STOP
	layer.add_child(race_pause_root)

	var shade := ColorRect.new()
	shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	shade.color = Color(0.0, 0.0, 0.0, 0.55)
	race_pause_root.add_child(shade)

	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	race_pause_root.add_child(center)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(320.0, 0.0)
	center.add_child(panel)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	panel.add_child(box)

	race_pause_title = Label.new()
	race_pause_title.text = "Race Paused"
	race_pause_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(race_pause_title)

	race_pause_resume_button = Button.new()
	race_pause_resume_button.text = "Resume"
	race_pause_resume_button.pressed.connect(_close_race_pause_menu)
	box.add_child(race_pause_resume_button)

	race_pause_lobby_button = Button.new()
	race_pause_lobby_button.text = "Back To Lobby"
	race_pause_lobby_button.pressed.connect(_on_pause_lobby_pressed)
	box.add_child(race_pause_lobby_button)

	race_pause_disconnect_button = Button.new()
	race_pause_disconnect_button.text = "Disconnect"
	race_pause_disconnect_button.pressed.connect(_on_pause_disconnect_pressed)
	box.add_child(race_pause_disconnect_button)

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

func _local_player_id() -> int:
	if singleplayer_mode:
		return 0
	return multiplayer.get_unique_id() if multiplayer.has_multiplayer_peer() else 0

func _player_display_name(id: int) -> String:
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

func _format_race_time(tick_value: int) -> String:
	var race_tick := maxi(0, tick_value - 300)
	var total_msec := int(round(float(race_tick) * 1000.0 / 60.0))
	var minutes := int(total_msec / 60000)
	var seconds := int(total_msec / 1000) % 60
	var milliseconds := total_msec % 1000
	return "%d:%02d.%03d" % [minutes, seconds, milliseconds]

func _show_finish_medal(actor_id: int, tick_value: int) -> void:
	var medal := FinishMedalScene.instantiate() as Control
	_add_race_medal(medal)
	medal.call("set_finisher_name", _player_display_name(actor_id), _format_race_time(tick_value))

func _show_ko_medal(actor_id: int, target_id: int) -> void:
	var medal := KoMedalScene.instantiate() as Control
	_add_race_medal(medal)
	medal.call("set_names", _player_display_name(actor_id), _player_display_name(target_id))

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
	if singleplayer_mode or !multiplayer.has_multiplayer_peer():
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
	var cp_count := pb.get_u32()
	var seg_count := pb.get_u32()
	var trig_count := 0
	if version != "v0.1" and version != "v0.2":
		trig_count = pb.get_u32()

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
		if road_type == 5 or road_type == 6:
			_skip_curve.call(); _skip_curve.call(); _skip_curve.call()
		if road_type == 2 or road_type == 4 or road_type == 6:
			_skip_curve.call()
		# v0.4+: Open Rounded Square has an extra seam-rotation curve after openness
		if road_type == 6 and version != "v0.1" and version != "v0.2" and version != "v0.3":
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
		if version != "v0.1":
			pb.get_float(); pb.get_float()
		if version != "v0.1" and version != "v0.2" and version != "v0.3" and version != "v0.4":
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

func _load_and_start_debug_replay(path: String) -> void:
	var replay := _load_debug_replay_file(path)
	if replay.is_empty():
		return
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	if game_sim.sim_started or singleplayer_mode:
		_return_to_menu()
	var track_index := _debug_replay_find_track_index(replay)
	if track_index < 0 or track_index >= tracks.size():
		print("MXT_DEBUG_REPLAY load failed: track not found for ", replay.get("track_name", ""))
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or settings.is_empty():
		print("MXT_DEBUG_REPLAY load failed: replay has no racer settings.")
		return
	var snapshot_tick := int(replay.get("snapshot_tick", -1))
	var snapshot_state := Marshalls.base64_to_raw(String(replay.get("snapshot_state_b64", "")))
	if snapshot_tick < 0 or snapshot_state.is_empty():
		print("MXT_DEBUG_REPLAY load failed: missing native snapshot.")
		return
	debug_replay_playback_inputs.clear()
	var inputs = replay.get("inputs_b64", [])
	if typeof(inputs) != TYPE_ARRAY:
		print("MXT_DEBUG_REPLAY load failed: inputs_b64 is not an array.")
		return
	for input_b64 in inputs:
		debug_replay_playback_inputs.append(Marshalls.base64_to_raw(String(input_b64)))
	if debug_replay_playback_inputs.is_empty():
		print("MXT_DEBUG_REPLAY load failed: replay has no input frames.")
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
	game_sim.set_state_data(snapshot_tick, snapshot_state)
	game_sim.load_state(snapshot_tick)
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
	var info : Dictionary = tracks[track_index]
	# Load track metadata JSON and optional ground texture (ground.png) from the same folder
	current_track_meta = {}
	current_track_ground_image = null
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
	var local_id := _local_player_id()
	local_player_index = racer_ids.find(local_id)
	car_node_container.instantiate_cars(chosen_defs, racer_ids, local_id)
	var idx := 0
	for car:VisualCar in car_node_container.get_children():
		car.game_manager = self
		if idx < racer_settings.size():
			car.player_settings = racer_settings[idx]
			car.name_label.text = " " + racer_settings[idx].username + " "
		idx += 1
	car_render_manager.configure(chosen_defs, car_node_container.get_children())
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
	else:
		for car:VisualCar in car_node_container.get_children():
			if car.owning_id == local_id:
				car.name_label.queue_free()
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
	game_sim.set_car_render_manager(car_render_manager)
	# Ensure the C++ sim sees the shared spawn seed before instantiation
	game_sim.set_spawn_seed(network_manager.spawn_seed)
	game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
	game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
	network_manager.netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	if local_player_index >= 0 and local_player_index < car_node_container.get_child_count():
		var local_car := car_node_container.get_child(local_player_index) as VisualCar
		if local_car != null:
			game_sim.set_gameplay_camera(local_car.car_camera, local_car.owning_id)
	if network_manager.is_server:
		server_game_sim.car_node_container = car_node_container
		server_game_sim.spark_node_container = spark_node_container
		server_game_sim.set_car_render_manager(car_render_manager)
		server_game_sim.set_spawn_seed(network_manager.spawn_seed)
		server_game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
		server_game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
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
		network_manager.send_start_race(lobby_track_selector.selected, settings_array)

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
	if headless_mode:
		return
	race_finish_label.visible = false
	active_stickers.clear()
	_return_to_lobby()

func _update_player_list() -> void:
	player_list.clear()
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.get_cpu_roster()
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

func _update_nametags(active_camera: Camera3D, delta: float) -> void:
	if active_camera == null:
		return
	for car: VisualCar in car_node_container.get_children():
		if car == null or car.local_visual_enabled or !is_instance_valid(car.name_label):
			continue
		var render_transform := game_sim.get_player_render_transform(car.owning_id)
		var world_pos := render_transform.origin
		var to_car := world_pos - active_camera.global_position
		var hidden := active_camera.global_position.distance_squared_to(world_pos) > 12000.0
		hidden = hidden or active_camera.global_basis.z.dot(to_car) > 0.0
		var target_alpha := 0.0 if hidden else 1.0
		car.name_label.visible = true
		car.name_label.size = car.name_label.get_combined_minimum_size()
		car.name_label.modulate.a = lerpf(car.name_label.modulate.a, target_alpha, delta * 20.0)
		car.name_label.position = active_camera.unproject_position(
			world_pos + active_camera.global_basis.x * 1.5 + active_camera.global_basis.y * 1.5
		) + Vector2(72, -90)

func _physics_process(delta: float) -> void:
	if auto_track_editor_mode:
		return
	if headless_mode:
		if singleplayer_mode and game_sim.sim_started:
			_simulate_singleplayer_tick()
			if auto_quit_after_frames >= 0 and _singleplayer_tick >= auto_quit_after_frames:
				get_tree().quit()
			return
		if multiplayer.has_multiplayer_peer():
			var pi := _generate_random_input()
			network_manager.set_local_input(pi.serialize())
			network_manager.collect_client_inputs()
		return
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if lobby_control.visible:
		_update_player_list()
		var can_edit_cpu := network_manager.is_server and !network_manager.race_active
		add_cpu_button.disabled = !can_edit_cpu
		remove_cpu_button.disabled = !can_edit_cpu or network_manager.get_cpu_roster().is_empty()
	if game_sim.sim_started:
		var physics_start := Time.get_ticks_usec()
		var local_input_us := 0
		var render_us := 0
		var visual_us := 0
		var race_finish_us := 0
		_last_sp_build_inputs_us = 0
		_last_sp_tick_gamesim_us = 0
		var local_pi := PlayerInputClass.new()
		var local_input_start := Time.get_ticks_usec()
		if _window_accepts_input() and players.size() > local_player_index:
			var controller = players[local_player_index]
			if controller != null:
				local_pi = controller.get_input()
		var input_bytes := local_pi.serialize()
		local_input_us = Time.get_ticks_usec() - local_input_start
		var simulate_start := Time.get_ticks_usec()
		if singleplayer_mode:
			_simulate_singleplayer_tick(input_bytes, local_input_us)
		else:
			network_manager.set_local_input(input_bytes)
			if network_manager.is_server:
				_simulate_host_frame(input_bytes)
			else:
				_simulate_single_tick()
		_last_simulate_call_us = Time.get_ticks_usec() - simulate_start
		_consume_authoritative_race_events()
		var render_start := Time.get_ticks_usec()
		game_sim.render_gamesim()
		render_us = Time.get_ticks_usec() - render_start
		var visual_start := Time.get_ticks_usec()
		_update_nametags(get_viewport().get_camera_3d(), delta)
		for car:VisualCar in car_node_container.get_children():
			if car.local_visual_enabled:
				car.just_rendered()
		visual_us = Time.get_ticks_usec() - visual_start
		var race_finish_start := Time.get_ticks_usec()
		_check_race_finished()
		race_finish_us = Time.get_ticks_usec() - race_finish_start
		_record_outer_profile({
			"physics_total": Time.get_ticks_usec() - physics_start,
			"local_input": local_input_us,
			"simulate_call": _last_simulate_call_us,
			"sp_build_inputs": _last_sp_build_inputs_us,
			"sp_tick_gamesim": _last_sp_tick_gamesim_us,
			"render_gamesim": render_us,
			"visual_just_rendered": visual_us,
			"race_finish": race_finish_us,
			"process_total": _last_process_total_us,
			"process_labels": _last_process_labels_us,
			"process_native_visual": _last_process_native_visual_us,
			"process_effect_tiers": _last_process_effect_tiers_us,
		})

func _simulate_singleplayer_tick(input_bytes: PackedByteArray = PackedByteArray(), build_inputs_us: int = 0):
	var start_time := Time.get_ticks_usec()
	var build_inputs_start := start_time
	if debug_replay_playback:
		if debug_replay_playback_index >= debug_replay_playback_inputs.size():
			debug_replay_playback = false
			game_sim.set_sim_started(false)
			print("MXT_DEBUG_REPLAY playback complete ", debug_replay_loaded_path, " end_tick=", _singleplayer_tick)
			if headless_mode:
				get_tree().quit()
			return
		input_bytes = (debug_replay_playback_inputs[debug_replay_playback_index] as PackedByteArray).duplicate()
		build_inputs_us = Time.get_ticks_usec() - build_inputs_start
		debug_replay_playback_index += 1
	if input_bytes.is_empty():
		var local_pi := PlayerInputClass.new()
		if auto_accelerate_mode:
			local_pi.accelerate = 1.0
		var accepts_input := _window_accepts_input()
		if !auto_accelerate_mode and accepts_input and players.size() > local_player_index:
			var controller = players[local_player_index]
			if controller != null:
				local_pi = controller.get_input()
		input_bytes = local_pi.serialize()
		build_inputs_us = Time.get_ticks_usec() - build_inputs_start
	if debug_replay_recording:
		debug_replay_inputs.append(input_bytes.duplicate())
	_last_sp_build_inputs_us = build_inputs_us
	_dump_offline_auth_input_sample(input_bytes)
	_dump_offline_state_sample()
	var tick_gamesim_start := Time.get_ticks_usec()
	game_sim.tick_singleplayer(_local_player_id(), input_bytes)
	_last_sp_tick_gamesim_us = Time.get_ticks_usec() - tick_gamesim_start
	_singleplayer_tick += 1
	# Update HUD timing using the same field clients use
	network_manager.clients_server_tick = _singleplayer_tick
	var end_time := Time.get_ticks_usec()
	network_manager.rollback_frametime_us = end_time - start_time

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
		var outer_profile := get_outer_profile_string()
		DisplayServer.clipboard_set(profile + "\n" + render_profile + "\n" + outer_profile)
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
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	lobby_control.visible = true
	network_manager.flush_waiting_peers()
	network_manager.reset_race_state(true)
	singleplayer_mode = false
	_singleplayer_tick = 0

func _check_race_finished() -> void:
	if !game_sim.sim_started:
		return
	if !network_manager.is_server and !singleplayer_mode:
		return
	var all_done := true
	var racer_ids := network_manager.get_simulation_roster()
	var finish_sim := server_game_sim if network_manager.is_server and server_game_sim != null else game_sim
	for racer_id in racer_ids:
		if network_manager._disconnected_during_race.has(racer_id):
			continue
		if network_manager.player_finish_times.has(racer_id):
			continue
		var finished := false
		if finish_sim != null and finish_sim.has_method("is_player_race_finished"):
			finished = finish_sim.is_player_race_finished(racer_id)
		else:
			for car in car_node_container.get_children():
				if car is VisualCar and car.owning_id == racer_id:
					finished = (car.machine_state & VisualCar.FZ_MS.COMPLETEDRACE_1_Q) != 0
					break
		if finished:
			if network_manager.is_server:
				network_manager.send_player_finished(racer_id, network_manager.server_tick)
			else:
				network_manager.record_player_finished(racer_id, network_manager.clients_server_tick)
		else:
			all_done = false
	if network_manager.is_server:
		if all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()
				network_manager.send_race_finish_time(network_manager.net_race_finish_time)
			if Time.get_ticks_msec() > network_manager.net_race_finish_time + 5000:
				network_manager.send_end_race()
				race_finish_label.visible = false
	else:
		if singleplayer_mode and all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()

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
	var process_start := Time.get_ticks_usec()
	var now_msec := Time.get_ticks_msec()
	for id in active_stickers.keys():
		var data: Dictionary = active_stickers[id]
		if now_msec > int(data.get("expires", 0)):
			active_stickers.erase(id)
	if race_finish_label.visible and race_notification_hide_msec > 0 and now_msec > race_notification_hide_msec and network_manager.net_race_finish_time == -1:
		race_finish_label.visible = false
		race_notification_hide_msec = 0
	var label_start := process_start
	frame_time_label.text = str(network_manager.rollback_frametime_us) + "us"
	rtt_label.text = str(roundi(network_manager.rtt_s * 1000.0)) + "ms"
	var label_us := Time.get_ticks_usec() - label_start
	var native_visual_us := 0
	if game_sim.sim_started:
		var native_visual_start := Time.get_ticks_usec()
		game_sim.render_gamesim_visuals_only(delta)
		native_visual_us = Time.get_ticks_usec() - native_visual_start
	var effect_us := 0
	var active_camera := get_viewport().get_camera_3d()
	if game_sim.sim_started and is_instance_valid(active_camera):
		effect_us = 0
	if game_sim.sim_started:
		_last_process_total_us = Time.get_ticks_usec() - process_start
		_last_process_labels_us = label_us
		_last_process_native_visual_us = native_visual_us
		_last_process_effect_tiers_us = effect_us
