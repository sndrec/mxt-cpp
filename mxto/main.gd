class_name GameManager extends Node

const LobbyChibiControllerClass = preload("res://ui/lobby_chibi_controller.gd")
const LobbyControllerClass = preload("res://ui/lobby_controller.gd")
const SpectatorControllerClass = preload("res://ui/spectator_controller.gd")
const SpectatorRosterClass = preload("res://ui/spectator_roster.gd")
const RacePresentationControllerClass = preload("res://ui/race_presentation_controller.gd")
const DebugRuntimeControllerClass = preload("res://core/debug_runtime_controller.gd")
const RaceSessionControllerClass = preload("res://core/race_session_controller.gd")
const CarSettingsClass = preload("res://ui/car_settings.gd")
const CommunicationControllerClass = preload("res://ui/communication_controller.gd")
const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const LobbyVehicleContentTrackerClass = preload("res://vehicle/lobby_vehicle_content_tracker.gd")
const TimeAttackSetupClass = preload("res://ui/time_attack_setup.gd")
const PracticeSetupClass = preload("res://practice/practice_setup.gd")
const PracticeControllerClass = preload("res://practice/practice_controller.gd")
const PracticeInputEditorClass = preload("res://practice/practice_input_editor.gd")
const CpuVehiclePoolButtonClass = preload("res://ui/cpu_vehicle_pool_button.gd")
const RacePauseControllerClass = preload("res://ui/race_pause_controller.gd")

@onready var game_sim: GameSim = $GameSim
@onready var server_game_sim: GameSim = $ServerGameSim
@onready var replay_controller: ReplayController = $ReplayController
@onready var race_audio_controller: RaceAudioController = $RaceAudioController
@onready var track_content_controller: TrackContentController = $TrackContentController
@onready var track_presentation_controller: TrackPresentationController = $TrackPresentationController
@onready var lobby_chibi_controller: LobbyChibiControllerClass = $LobbyChibiController
@onready var lobby_controller: LobbyControllerClass = $LobbyController
@onready var spectator_controller: SpectatorControllerClass = $SpectatorController
@onready var spectator_roster: SpectatorRosterClass = $SpectatorRoster
@onready var race_presentation_controller: RacePresentationControllerClass = $RacePresentationController
@onready var debug_runtime_controller: DebugRuntimeControllerClass = $DebugRuntimeController
@onready var race_session_controller: RaceSessionControllerClass = $RaceSessionController
@onready var communication_controller: CommunicationControllerClass = $CommunicationController
@onready var vehicle_content_controller: VehicleContentControllerClass = $VehicleContentController
@onready var lobby_vehicle_content_tracker: LobbyVehicleContentTrackerClass = $LobbyVehicleContentTracker
@onready var playtest_lobby_probe = $PlaytestLobbyProbe
@onready var connect_host_box: HBoxContainer = $Control/ConnectHostBox
@onready var start_button: Button = $Control/ConnectHostBox/StartButton
@onready var join_button: Button = $Control/ConnectHostBox/JoinButton
@onready var join_playtest_button: Button = $Control/JoinPlaytestButton
@onready var ip_field: LineEdit = $Control/ConnectHostBox/IPField
@onready var port_field: LineEdit = $Control/ConnectHostBox/Port
@onready var track_selector: OptionButton = $Control/TrackSelector
@onready var lobby_control: Control = $Lobby
@onready var car_node_container: CarNodeContainer = $GameWorld/CarNodeContainer
@onready var spark_node_container: SuperSparkContainer = $GameWorld/SuperSparkContainer
@onready var obj_container: Node3D = $GameWorld/ObjContainer
@onready var debug_track_mesh_container: Node3D = $GameWorld/DebugTrackMeshContainer
@onready var debug_track_mesh: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/DebugTrackMesh
@onready var network_manager: NetworkManager = $NetworkManager
@onready var car_settings: CarSettingsClass = $CarSettings
@onready var time_attack_setup: TimeAttackSetupClass = $TimeAttackSetup
@onready var practice_setup: PracticeSetupClass = $PracticeSetup
@onready var practice_controller: PracticeControllerClass = $PracticeController
@onready var practice_hud: CanvasLayer = $PracticeHud
@onready var practice_hud_label: Label = $PracticeHud/Root/Margin/Panel/Inner/Status
@onready var practice_input_editor_layer: CanvasLayer = $PracticeInputEditor
@onready var practice_input_editor: PracticeInputEditorClass = $PracticeInputEditor/Panel
@onready var options_menu: Control = $OptionsLayer/OptionsMenu
@onready var car_settings_button: Button = $Control/CarSettingsButton
@onready var singleplayer_button: Button = $Control/SingleplayerButton
@onready var spectator_race_button: Button = $Control/SpectatorRaceButton
@onready var controller_settings_button: Button = $Control/ControllerSettingsButton
@onready var track_editor_button: Button = $Control/TrackEditorButton
@onready var frame_time_label: Label = $FrameTimeLabel
@onready var rtt_label: Label = $RTTLabel
@onready var version_label: Label = $VersionLabel
@onready var cpu_slider: HSlider = $Control/CpuSlider
@onready var cpu_slider_label: Label = $Control/CpuSliderLabel
@onready var obj_viewport: SubViewport = get_node_or_null("GameWorld/ObjViewport") as SubViewport
@onready var outline_viewport: SubViewport = get_node_or_null("GameWorld/OutlineViewport") as SubViewport
@onready var obj_viewport_texture: ColorRect = get_node_or_null("GameWorld/ObjViewportTexture") as ColorRect
@onready var outline_viewport_texture: ColorRect = get_node_or_null("GameWorld/OutlineViewportTexture") as ColorRect

const PlayerInputClass = preload("res://player/player_input.gd")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const TimeAttackRulesClass = preload("res://leaderboards/time_attack_rules.gd")
const LeaderboardEligibilityClass = preload("res://leaderboards/leaderboard_eligibility.gd")
const LeaderboardClientClass = preload("res://leaderboards/leaderboard_client.gd")
const LeaderboardReplayCacheClass = preload("res://leaderboards/leaderboard_replay_cache.gd")
const TimeAttackGhostControllerClass = preload("res://time_attack/time_attack_ghost_controller.gd")
const SteamLeaderboardSnapshotClass = preload("res://backend/leaderboard_migration/steam_leaderboard_snapshot.gd")
const BUMPER_DEFINITION_PATH := "res://vehicle/asset/bumper/definition.tres"
const BUMPER_POOL_SIZE := 60
const RACE_CONTENT_DOWNLOAD_TIMEOUT_MSEC := 25000
const CAMERA_SETTINGS_PATH := "user://camera_settings.json"

var player_scene := preload("res://player/player_controller.tscn")
var headless_mode: bool = false

# Singleplayer state
var singleplayer_mode: bool = false
var _singleplayer_tick: int = 0
var singleplayer_cpu_count: int = 0
var vehicle_test_drive_active := false
var vehicle_test_drive_content_id := ""
var vehicle_test_drive_saved_settings: Dictionary = {}
var vehicle_test_drive_saved_cpu_count := 0
var vehicle_test_drive_last_track := 0
var vehicle_test_drive_picker: ConfirmationDialog
var vehicle_test_drive_track_option: OptionButton
var time_attack_eligibility: Dictionary = {}
var time_attack_finalized := false
var time_attack_previous_best_milliseconds := 0
var time_attack_last_replay_path := ""
var time_attack_rank_refresh_board := ""
var time_attack_rank_refresh_global := ""
var gameplay_camera_zoom_mode := 1
var base_vehicle_render_view_distance := 360.0
var launch_cpu_driver_count: int = -1
var auto_host_mode: bool = false
var auto_singleplayer_mode: bool = false
var auto_track_editor_mode: bool = false
var auto_bumpers_mode: bool = false
var car_render_manager: CarRenderManager
var singleplayer_options_root: Control
var singleplayer_options_restore_toggle: CheckBox
var singleplayer_options_bumpers_toggle: CheckBox
var singleplayer_options_s_boost_toggle: CheckBox
var singleplayer_options_cpu_vehicles: CpuVehiclePoolButton
var singleplayer_options_as_spectator := false
var race_dnf_low_speed_ticks := {}
var start_sync_drop_root: PanelContainer
var start_sync_drop_label: Label
var start_sync_drop_button: Button

const DNF_SPEED_THRESHOLD_KMH := 400.0
const DNF_LOW_SPEED_TICKS := 60 * 10
const FORCE_END_WINDOW_TICKS := 60 * 60
const NATIVE_UI_FOCUS_ACTIONS: Array[StringName] = [
	&"ui_up",
	&"ui_down",
	&"ui_left",
	&"ui_right",
	&"ui_focus_next",
	&"ui_focus_prev",
	&"ui_page_up",
	&"ui_page_down",
	&"ui_home",
	&"ui_end",
]

var race_pause_controller: RacePauseController
var last_process_ticks_usec := 0
var steam_service: MxtSteamService
var leaderboard_client: LeaderboardClient
var leaderboard_replay_cache: LeaderboardReplayCache
var leaderboard_watch_request_token := 0
var time_attack_ghost_controller: TimeAttackGhostController
var time_attack_ghost_descriptors: Array = []
var steam_leaderboard_snapshot

func _enter_tree() -> void:
	_disable_native_joypad_focus_navigation()

func _disable_native_joypad_focus_navigation() -> void:
	for action in NATIVE_UI_FOCUS_ACTIONS:
		if !InputMap.has_action(action):
			continue
		for event in InputMap.action_get_events(action):
			if event is InputEventJoypadButton or event is InputEventJoypadMotion:
				InputMap.action_erase_event(action, event)

func _ready() -> void:
	base_vehicle_render_view_distance = float(game_sim.get_render_car_body_view_distance())
	_load_gameplay_camera_settings()
	steam_service = MxtSteamService.new()
	steam_service.name = "SteamService"
	add_child(steam_service)
	leaderboard_client = LeaderboardClientClass.new()
	leaderboard_client.name = "LeaderboardClient"
	add_child(leaderboard_client)
	leaderboard_client.initialize(steam_service)
	leaderboard_client.submission_status_changed.connect(_on_leaderboard_status_changed)
	leaderboard_client.submission_completed.connect(_on_leaderboard_submission_completed)
	leaderboard_client.entries_received.connect(_on_time_attack_rank_entries_received)
	vehicle_content_controller.initialize(steam_service, network_manager.custom_stamp_network)
	lobby_vehicle_content_tracker.initialize(
		steam_service,
		vehicle_content_controller.content_catalog,
		vehicle_content_controller,
		network_manager)
	vehicle_content_controller.catalog_changed.connect(_on_vehicle_content_catalog_changed)
	vehicle_content_controller.catalog_delta.connect(_on_vehicle_content_catalog_delta)
	#obj_viewport_texture.texture = obj_viewport.get_texture()
	#outline_viewport_texture.texture = outline_viewport.get_texture()
	car_render_manager = CarRenderManagerClass.new()
	car_render_manager.name = "CarRenderManager"
	$GameWorld.add_child(car_render_manager)
	race_audio_controller.initialize()
	communication_controller.initialize(self, network_manager, game_sim, replay_controller)
	lobby_chibi_controller.initialize(
		self,
		network_manager,
		game_sim,
		communication_controller.lobby_input,
		vehicle_content_controller,
		lobby_vehicle_content_tracker)
	lobby_controller.initialize(network_manager, track_content_controller, lobby_chibi_controller)
	lobby_controller.start_race_requested.connect(_on_lobby_start_race_requested)
	lobby_controller.car_settings_requested.connect(_on_car_settings_button_pressed)
	lobby_controller.controller_settings_requested.connect(_on_controller_settings_button_pressed)
	lobby_controller.spectator_toggled.connect(_on_lobby_spectator_toggled)
	spectator_controller.initialize(network_manager, game_sim, car_node_container, vehicle_content_controller)
	spectator_roster.initialize(network_manager, game_sim, spectator_controller)
	race_presentation_controller.initialize(
		network_manager,
		game_sim,
		server_game_sim,
		replay_controller,
		track_content_controller,
		car_node_container,
		car_settings)
	race_presentation_controller.results_overlay.time_attack_race_again_requested.connect(_on_time_attack_race_again_requested)
	race_presentation_controller.results_overlay.time_attack_save_replay_requested.connect(_on_time_attack_save_replay_requested)
	race_presentation_controller.results_overlay.time_attack_watch_replay_requested.connect(_on_time_attack_watch_replay_requested)
	race_presentation_controller.results_overlay.time_attack_leaderboard_requested.connect(_on_time_attack_results_leaderboard_requested)
	race_presentation_controller.results_overlay.time_attack_main_menu_requested.connect(_on_time_attack_main_menu_requested)
	debug_runtime_controller.initialize(
		game_sim,
		server_game_sim,
		network_manager,
		race_presentation_controller,
		car_node_container,
		frame_time_label,
		rtt_label,
		version_label)
	race_session_controller.initialize(
		self,
		game_sim,
		server_game_sim,
		network_manager,
		replay_controller,
		race_audio_controller,
		track_content_controller,
		track_presentation_controller,
		lobby_chibi_controller,
		spectator_controller,
		race_presentation_controller,
		vehicle_content_controller,
		debug_runtime_controller,
		car_node_container,
		spark_node_container,
		obj_container,
		car_render_manager,
		$Control,
		lobby_control,
		player_scene)
	spectator_controller.notification_requested.connect(race_presentation_controller.show_notification)
	randomize()
	_build_multiplayer_connect_box()
	_build_singleplayer_race_options_screen()
	leaderboard_replay_cache = LeaderboardReplayCacheClass.new()
	leaderboard_replay_cache.name = "LeaderboardReplayCache"
	add_child(leaderboard_replay_cache)
	leaderboard_replay_cache.initialize(self, leaderboard_client)
	leaderboard_replay_cache.request_completed.connect(_on_leaderboard_replay_cache_request_completed)
	time_attack_ghost_controller = TimeAttackGhostControllerClass.new()
	time_attack_ghost_controller.name = "TimeAttackGhostController"
	add_child(time_attack_ghost_controller)
	time_attack_ghost_controller.initialize(self)
	time_attack_setup.initialize(self)
	time_attack_setup.ranked_start_requested.connect(_on_time_attack_ranked_start_requested)
	time_attack_setup.practice_requested.connect(_on_practice_setup_requested)
	time_attack_setup.back_requested.connect(_on_time_attack_setup_back_requested)
	time_attack_setup.official_vehicle_requested.connect(_on_time_attack_official_vehicle_requested)
	time_attack_setup.leaderboard_requested.connect(_on_time_attack_leaderboard_requested)
	time_attack_setup.watch_replay_requested.connect(_on_leaderboard_replay_watch_requested)
	race_pause_controller = RacePauseControllerClass.new()
	race_pause_controller.name = "RacePauseController"
	add_child(race_pause_controller)
	race_pause_controller.initialize(
		$RacePauseLayer/RacePauseRoot, practice_controller, replay_controller, options_menu)
	race_pause_controller.retry_requested.connect(_on_pause_retry_pressed)
	race_pause_controller.disconnect_requested.connect(_on_pause_disconnect_pressed)
	race_pause_controller.lobby_requested.connect(_on_pause_lobby_pressed)
	race_pause_controller.options_requested.connect(_on_pause_options_requested)
	practice_controller.initialize(
		self,
		race_pause_controller.game_speed_button,
		race_pause_controller.input_mode_button,
		race_pause_controller.input_editor_button,
		race_pause_controller.telemetry_button,
		practice_hud,
		practice_hud_label,
		practice_input_editor_layer,
		practice_input_editor)
	practice_setup.initialize(self, time_attack_setup.ghost_selection)
	practice_setup.start_requested.connect(_on_practice_start_requested)
	practice_setup.back_requested.connect(_on_practice_setup_back_requested)
	replay_controller.initialize()
	car_settings.leaderboard_browser.watch_replay_requested.connect(_on_leaderboard_replay_watch_requested)
	_build_start_sync_drop_panel()
	_load_tracks()
	if car_settings != null and car_settings.has_method("refresh_after_game_manager_loaded"):
		car_settings.call("refresh_after_game_manager_loaded")
	network_manager.race_started.connect(_on_network_race_started)
	network_manager.race_finished.connect(_on_network_race_finished)
	network_manager.race_results.race_event.connect(_on_race_event)
	car_settings.hide()
	options_menu.hide()
	if !car_settings_button.pressed.is_connected(_on_car_settings_button_pressed):
		car_settings_button.pressed.connect(_on_car_settings_button_pressed)
	if !controller_settings_button.pressed.is_connected(_on_controller_settings_button_pressed):
		controller_settings_button.pressed.connect(_on_controller_settings_button_pressed)
	if !options_menu.visibility_changed.is_connected(_on_controller_settings_visibility_changed):
		options_menu.visibility_changed.connect(_on_controller_settings_visibility_changed)
	var vehicle_view_distance_callable := Callable(self, "_on_vehicle_view_distance_changed")
	if options_menu.has_signal("vehicle_view_distance_changed") \
			and !options_menu.is_connected("vehicle_view_distance_changed", vehicle_view_distance_callable):
		options_menu.connect("vehicle_view_distance_changed", vehicle_view_distance_callable)
	if !track_editor_button.pressed.is_connected(_on_track_editor_button_pressed):
		track_editor_button.pressed.connect(_on_track_editor_button_pressed)
	# Rewire the Singleplayer button to its own handler, not the multiplayer host flow
	if singleplayer_button.pressed.is_connected(_on_start_button_pressed):
		singleplayer_button.pressed.disconnect(_on_start_button_pressed)
	singleplayer_button.pressed.connect(_on_singleplayer_button_pressed)
	spectator_race_button.pressed.connect(_on_spectator_race_button_pressed)
	cpu_slider.value_changed.connect(_on_singleplayer_cpu_slider_changed)
	if !join_playtest_button.pressed.is_connected(_on_join_playtest_button_pressed):
		join_playtest_button.pressed.connect(_on_join_playtest_button_pressed)
	if !playtest_lobby_probe.availability_changed.is_connected(_on_playtest_lobby_availability_changed):
		playtest_lobby_probe.availability_changed.connect(_on_playtest_lobby_availability_changed)
	singleplayer_cpu_count = int(cpu_slider.value)
	_update_cpu_slider_label()
	headless_mode = DisplayServer.get_name() == "headless"
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	var steam_snapshot_requested := SteamLeaderboardSnapshotClass.requested(args, user_args)
	if steam_snapshot_requested:
		steam_leaderboard_snapshot = SteamLeaderboardSnapshotClass.new()
		steam_leaderboard_snapshot.name = "SteamLeaderboardSnapshot"
		add_child(steam_leaderboard_snapshot)
		steam_leaderboard_snapshot.initialize(steam_service, args, user_args)
		$Control.visible = false
		lobby_control.visible = false
	var lobby_load_peer_mode := args.has("--lobby-load-peer") or user_args.has("--lobby-load-peer")
	launch_cpu_driver_count = _parse_cpu_driver_count_arg(args)
	if launch_cpu_driver_count < 0:
		launch_cpu_driver_count = _parse_cpu_driver_count_arg(user_args)

	if launch_cpu_driver_count >= 0:
		singleplayer_cpu_count = launch_cpu_driver_count
		if cpu_slider != null:
			cpu_slider.set_value_no_signal(mini(singleplayer_cpu_count, int(cpu_slider.max_value)))
		_update_cpu_slider_label()
		network_manager.lobby_settings.set_cpu_driver_count(launch_cpu_driver_count)
	auto_host_mode = args.has("--host") or user_args.has("--host")
	if auto_host_mode:
		call_deferred("_auto_host")
	auto_singleplayer_mode = args.has("--auto-singleplayer") or user_args.has("--auto-singleplayer")
	auto_bumpers_mode = args.has("--auto-bumpers") or user_args.has("--auto-bumpers")
	debug_runtime_controller.configure_command_line(args, user_args)
	_on_vehicle_view_distance_changed(
		float(options_menu.call("get_vehicle_view_distance_multiplier")),
		bool(options_menu.call("get_render_all_vehicles")))
	auto_track_editor_mode = args.has("--track-editor") or user_args.has("--track-editor") or args.has("--mxt-track-editor") or user_args.has("--mxt-track-editor")
	var replay_launch_requested := replay_controller.configure_command_line(args, user_args)
	if !steam_snapshot_requested and !replay_launch_requested and auto_track_editor_mode:
		call_deferred("_on_track_editor_button_pressed")
	elif !steam_snapshot_requested and !replay_launch_requested and auto_singleplayer_mode:
		call_deferred("_start_singleplayer_race", false, TimeAttackRulesClass.build_configuration())
	if headless_mode and !steam_snapshot_requested and !lobby_load_peer_mode and !auto_host_mode and !auto_track_editor_mode and !auto_singleplayer_mode and !replay_launch_requested:
		var vehicle_content_id := ""
		if vehicle_content_controller.definitions.size() > 0:
			vehicle_content_id = vehicle_content_controller.definitions[0].content_id
		var settings_dict = {
			"username": "Headless",
			"vehicle_content_id": vehicle_content_id,
			"accel_setting": 1.0,
		}
		settings_dict.merge(vehicle_content_controller.get_evidence(vehicle_content_id), true)
		network_manager.multiplayer.connected_to_server.connect(
			_send_connected_player_settings.bind(settings_dict),
			Object.CONNECT_ONE_SHOT)
		var join_timer := Timer.new()
		join_timer.one_shot = true
		join_timer.wait_time = 3.0
		add_child(join_timer)
		join_timer.timeout.connect(func(): network_manager.join("127.0.0.1"))
		join_timer.start()
		$Control.visible = false
		lobby_control.visible = true

func _exit_tree() -> void:
	if practice_controller != null:
		practice_controller.end_session()
	RenderingServer.force_sync()

func _parse_cpu_driver_count_arg(args: Array) -> int:
	var cpu_idx := args.find("-cpu-drivers")
	if cpu_idx == -1:
		cpu_idx = args.find("--cpu-drivers")
	if cpu_idx == -1 or cpu_idx + 1 >= args.size():
		return -1
	return int(clamp(float(args[cpu_idx + 1]), 0.0, 5000.0))

func _load_tracks() -> void:
	track_content_controller.scan_catalog()
	track_selector.clear()
	for t in track_content_controller.tracks:
		track_selector.add_item(t["name"])
	if !track_content_controller.tracks.is_empty():
		track_selector.selected = 0
	var command_line_track_index := track_content_controller.command_line_track_index()
	if command_line_track_index >= 0:
		track_selector.selected = command_line_track_index
	lobby_controller.reload_tracks(command_line_track_index)

func _on_vehicle_content_catalog_changed() -> void:
	_load_tracks()
	if lobby_chibi_controller != null:
		lobby_chibi_controller.refresh_vehicle_content()
	if car_settings != null:
		car_settings.call("refresh_after_game_manager_loaded")

func _on_vehicle_content_catalog_delta(delta: MxtContentCatalogDelta) -> void:
	var affected_content_ids: Array = []
	affected_content_ids.append_array(Array(delta.added_content_ids))
	affected_content_ids.append_array(Array(delta.changed_content_ids))
	affected_content_ids.append_array(Array(delta.removed_content_ids))
	if lobby_chibi_controller != null and !affected_content_ids.is_empty():
		lobby_chibi_controller.refresh_vehicle_content(affected_content_ids)
	if car_settings != null:
		car_settings.call("refresh_after_game_manager_loaded")

func build_cpu_player_settings(index: int, allowed_content_ids: Array = []) -> Dictionary:
	var ps := PlayerSettings.new()
	ps.username = "CPU %03d" % (index + 1)
	var content_ids := vehicle_content_controller.get_vehicle_content_ids()
	var configured_ids_value = Array(network_manager.race_configuration.cpu_vehicle_content_ids) \
		if network_manager != null else []
	var requested_ids: Array = allowed_content_ids
	if requested_ids.is_empty() and typeof(configured_ids_value) == TYPE_ARRAY:
		requested_ids = configured_ids_value
	if !requested_ids.is_empty():
		content_ids.clear()
		for content_id_value in requested_ids:
			var content_id := String(content_id_value)
			if vehicle_content_controller.get_definition(content_id) != null and !content_ids.has(content_id):
				content_ids.append(content_id)
	if network_manager != null and network_manager.has_network_peer():
		content_ids = vehicle_content_controller.get_multiplayer_vehicle_content_ids(
			network_manager.race_configuration.allow_workshop_vehicles)
	if content_ids.size() > 0:
		ps.vehicle_content_id = content_ids[index % content_ids.size()]
	else:
		ps.vehicle_content_id = ""
	vehicle_content_controller.apply_evidence(ps)
	ps.accel_setting = 1.0
	ps.spectator = false
	return ps.to_dict()

func _on_start_button_pressed() -> void:
	var err := network_manager.host(_multiplayer_lobby_port())
	if err != OK:
		return
	communication_controller.reset()
	if launch_cpu_driver_count >= 0:
		network_manager.lobby_settings.set_cpu_driver_count(launch_cpu_driver_count)
	network_manager.lobby_settings.send_player_settings(car_settings.get_player_settings().to_dict())
	network_manager.custom_stamp_network.send_active_custom_stamp_manifest()
	lobby_controller.refresh_controls()
	$Control.visible = false
	lobby_control.visible = true

func _on_lobby_spectator_toggled(enabled: bool) -> void:
	if network_manager.race_active:
		return
	car_settings.set_spectator_enabled(enabled)

func _on_singleplayer_button_pressed() -> void:
	time_attack_setup.open_for_current_selection()


func _on_time_attack_ranked_start_requested(context: Dictionary) -> void:
	time_attack_previous_best_milliseconds = int(context.get("personal_best_milliseconds", 0))
	var descriptor_value = context.get("ghost_descriptors", [])
	time_attack_ghost_descriptors = (descriptor_value as Array).duplicate(true) if typeof(descriptor_value) == TYPE_ARRAY else []
	var ghost_prepare := time_attack_ghost_controller.prepare(time_attack_ghost_descriptors, track_selector.selected)
	if !bool(ghost_prepare.get("success", false)):
		race_presentation_controller.show_notification(String(ghost_prepare.get("message", "Selected ghosts could not be prepared.")), 5000)
		time_attack_setup.open_for_current_selection()
		return
	_start_singleplayer_race(false, TimeAttackRulesClass.build_configuration())


func _on_practice_setup_requested() -> void:
	time_attack_setup.hide()
	practice_setup.open_for_current_selection(singleplayer_cpu_count)


func _on_practice_setup_back_requested() -> void:
	practice_setup.hide()
	if vehicle_test_drive_active:
		_finish_vehicle_test_drive_return()
		return
	time_attack_setup.open_for_current_selection()


func _on_practice_start_requested(configuration: MxtRaceConfiguration, context: Dictionary) -> void:
	if vehicle_test_drive_active:
		configuration.leaderboard_ineligible_reason = "draft_vehicle"
		configuration.custom_content = true
	time_attack_previous_best_milliseconds = 0
	var descriptor_value = context.get("ghost_descriptors", [])
	time_attack_ghost_descriptors = (descriptor_value as Array).duplicate(true) if typeof(descriptor_value) == TYPE_ARRAY else []
	var ghost_prepare := time_attack_ghost_controller.prepare(time_attack_ghost_descriptors, track_selector.selected)
	if !bool(ghost_prepare.get("success", false)):
		race_presentation_controller.show_notification(String(ghost_prepare.get("message", "Selected ghosts could not be prepared.")), 5000)
		practice_setup.open_for_current_selection(configuration.cpu_count)
		return
	_start_singleplayer_race(false, configuration)


func _on_time_attack_setup_back_requested() -> void:
	time_attack_setup.hide()
	$Control.visible = true


func _on_time_attack_official_vehicle_requested() -> void:
	if car_settings.select_ranked_default_vehicle():
		time_attack_setup.refresh_after_vehicle_change()


func _on_time_attack_leaderboard_requested(board_name: String) -> void:
	time_attack_setup.hide()
	$Control.visible = true
	car_settings.open_leaderboards(board_name)

func begin_vehicle_test_drive(snapshot: MxtContentLoadResult) -> void:
	if vehicle_test_drive_active:
		return
	var record := snapshot.record
	var content_id := record.content_id if record != null else ""
	if content_id.is_empty():
		push_error("Vehicle test-drive snapshot has no registered content record")
		return
	var definition: CarDefinition = vehicle_content_controller.get_definition(content_id)
	if content_id.is_empty() or definition == null:
		push_error("Could not create the vehicle test-drive definition")
		return
	vehicle_test_drive_active = true
	vehicle_test_drive_content_id = content_id
	vehicle_test_drive_saved_settings = car_settings.get_player_settings().to_dict()
	vehicle_test_drive_saved_cpu_count = singleplayer_cpu_count
	vehicle_content_controller.definitions_by_content_id[content_id] = definition
	car_settings.call("set_test_drive_vehicle", content_id)
	_open_vehicle_test_drive_track_picker()

func _open_vehicle_test_drive_track_picker() -> void:
	if vehicle_test_drive_picker == null:
		vehicle_test_drive_picker = ConfirmationDialog.new()
		vehicle_test_drive_picker.title = "Test Drive Track"
		vehicle_test_drive_picker.ok_button_text = "Configure Practice"
		vehicle_test_drive_picker.cancel_button_text = "Back to Car Creator"
		vehicle_test_drive_picker.exclusive = true
		vehicle_test_drive_track_option = OptionButton.new()
		vehicle_test_drive_track_option.custom_minimum_size = Vector2(420.0, 0.0)
		vehicle_test_drive_picker.add_child(vehicle_test_drive_track_option)
		vehicle_test_drive_picker.confirmed.connect(_start_vehicle_test_drive)
		vehicle_test_drive_picker.canceled.connect(_cancel_vehicle_test_drive)
		add_child(vehicle_test_drive_picker)
	vehicle_test_drive_track_option.clear()
	for track_value in track_content_controller.tracks:
		var track: Dictionary = track_value
		vehicle_test_drive_track_option.add_item(String(track.get("name", "Track")))
	if vehicle_test_drive_track_option.item_count == 0:
		_cancel_vehicle_test_drive()
		return
	vehicle_test_drive_last_track = clampi(vehicle_test_drive_last_track, 0, vehicle_test_drive_track_option.item_count - 1)
	vehicle_test_drive_track_option.select(vehicle_test_drive_last_track)
	vehicle_test_drive_picker.popup_centered()

func _start_vehicle_test_drive() -> void:
	if !vehicle_test_drive_active or vehicle_test_drive_track_option == null:
		return
	vehicle_test_drive_last_track = vehicle_test_drive_track_option.selected
	track_selector.select(vehicle_test_drive_last_track)
	practice_setup.open_for_current_selection(0)

func _cancel_vehicle_test_drive() -> void:
	if !vehicle_test_drive_active:
		return
	_finish_vehicle_test_drive_return()

func _finish_vehicle_test_drive_return() -> void:
	var saved_settings := vehicle_test_drive_saved_settings.duplicate(true)
	var old_content_id := vehicle_test_drive_content_id
	vehicle_test_drive_active = false
	vehicle_test_drive_content_id = ""
	vehicle_test_drive_saved_settings.clear()
	singleplayer_cpu_count = vehicle_test_drive_saved_cpu_count
	if cpu_slider != null:
		cpu_slider.set_value_no_signal(singleplayer_cpu_count)
	_update_cpu_slider_label()
	if !old_content_id.is_empty():
		vehicle_content_controller.definitions_by_content_id.erase(old_content_id)
	car_settings.call("restore_after_test_drive", saved_settings)

func _on_spectator_race_button_pressed() -> void:
	_open_singleplayer_race_options(true)

func _on_track_editor_button_pressed() -> void:
	get_tree().change_scene_to_file("res://track_editing_scene.tscn")

func _start_singleplayer_race(as_spectator: bool, requested_configuration: MxtRaceConfiguration = null, requested_race_state: Dictionary = {}, requested_track_evidence: MxtTrackContentEvidence = null) -> void:
	# Start a local, singleplayer race that does not touch networking at all.
	# Prepare a minimal settings array using the current local player settings.
	var configuration := requested_configuration.copy() if requested_configuration != null else _build_default_singleplayer_race_configuration()
	var race_state := requested_race_state.duplicate(true) if !requested_race_state.is_empty() else _build_default_singleplayer_race_state()
	var track_evidence := requested_track_evidence.copy() if requested_track_evidence != null else track_content_controller.build_track_content_evidence([track_selector.selected])
	var is_practice := configuration.is_practice()
	var uses_time_attack_ghosts := configuration.is_time_attack() or is_practice
	if uses_time_attack_ghosts:
		if time_attack_ghost_controller.prepared_track_index != track_selector.selected:
			var ghost_prepare := time_attack_ghost_controller.prepare(time_attack_ghost_descriptors, track_selector.selected)
			if !bool(ghost_prepare.get("success", false)):
				race_presentation_controller.show_notification(String(ghost_prepare.get("message", "Selected ghosts could not be prepared.")), 5000)
				if is_practice:
					practice_setup.open_for_current_selection(configuration.cpu_count)
				else:
					time_attack_setup.open_for_current_selection()
				return
	else:
		time_attack_ghost_descriptors.clear()
		time_attack_ghost_controller.clear()
	singleplayer_mode = true
	_singleplayer_tick = 0
	network_manager.reset_race_state()
	if auto_bumpers_mode:
		configuration.bumpers = true
	var my_id := _local_player_id()
	if as_spectator:
		network_manager.player_ids = []
		network_manager.spectator_ids = [my_id]
	else:
		network_manager.player_ids = [my_id]
		network_manager.spectator_ids = []
	var race_cpu_count := configuration.cpu_count
	var ps = car_settings.get_player_settings()
	# Ensure we have a sensible car selection; fall back if needed
	if ps.vehicle_content_id == "" and vehicle_content_controller.definitions.size() > 0:
		ps.vehicle_content_id = vehicle_content_controller.definitions[0].content_id
	vehicle_content_controller.apply_evidence(ps)
	time_attack_last_replay_path = ""
	if configuration.is_time_attack():
		time_attack_eligibility = LeaderboardEligibilityClass.evaluate_start(self, configuration, track_evidence, ps)
		time_attack_finalized = false
		configuration.leaderboard_eligible = bool(time_attack_eligibility.get("eligible", false))
		configuration.leaderboard_ineligible_reason = String(time_attack_eligibility.get("reason", ""))
	else:
		time_attack_eligibility.clear()
		time_attack_finalized = false
	network_manager.race_configuration = configuration
	network_manager.race_track_evidence = track_evidence
	network_manager.race_state = race_state
	network_manager.lobby_settings.set_cpu_driver_count(race_cpu_count)
	network_manager.lobby_settings.set_cpu_driver_vehicle_pool(Array(configuration.cpu_vehicle_content_ids))
	ps.spectator = as_spectator
	network_manager.lobby_settings.set_player_settings(my_id, ps.to_dict())
	var cpu_ids := network_manager.lobby_settings.get_cpu_roster()
	var racer_ids: Array = [my_id]
	racer_ids.append_array(cpu_ids)
	var local_roster: MxtRaceRoster = network_manager.lobby_settings.build_race_roster(racer_ids)
	if local_roster == null:
		return
	_close_settings_menus_for_race_start()
	race_dnf_low_speed_ticks.clear()
	race_session_controller.start_race(track_selector.selected, local_roster, singleplayer_mode, headless_mode)
	if uses_time_attack_ghosts:
		var ghost_start := time_attack_ghost_controller.start_race(track_selector.selected)
		if !bool(ghost_start.get("success", false)):
			var ghost_error := String(ghost_start.get("message", "Selected ghost simulations could not start."))
			push_error(ghost_error)
			race_presentation_controller.show_notification(ghost_error, 5000)
			_return_to_menu()
			if is_practice:
				practice_setup.call_deferred("open_for_current_selection", configuration.cpu_count)
			else:
				time_attack_setup.call_deferred("open_for_current_selection")
			return
	if is_practice and !practice_controller.begin_session(configuration):
		push_error("Practice controller rejected the new Practice session.")
		_return_to_menu()
		practice_setup.call_deferred("open_for_current_selection", configuration.cpu_count)
		return
	# Hide menus
	$Control.visible = false
	lobby_control.visible = false
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false


func resume_replay_in_practice(payload: Dictionary) -> void:
	var transition_start_usec := int(payload.get("transition_start_usec", Time.get_ticks_usec()))
	var metadata_value = payload.get("metadata", {})
	if typeof(metadata_value) != TYPE_DICTIONARY:
		race_presentation_controller.show_notification("Replay resume failed: metadata is unavailable.", 4500)
		return
	var metadata: Dictionary = metadata_value
	var settings_value = metadata.get("settings", [])
	var racer_ids_value = metadata.get("racer_ids", [])
	if typeof(settings_value) != TYPE_ARRAY or typeof(racer_ids_value) != TYPE_ARRAY:
		race_presentation_controller.show_notification("Replay resume failed: recorded roster is unavailable.", 4500)
		return
	var settings: Array = (settings_value as Array).duplicate(true)
	var racer_ids: Array = []
	for id_value in racer_ids_value as Array:
		racer_ids.append(int(id_value))
	var focused_player_id := int(payload.get("focused_player_id", -1))
	var focus_index := racer_ids.find(focused_player_id)
	var track_index := int(payload.get("track_index", -1))
	var cursor := int(payload.get("cursor", -1))
	var full_state: PackedByteArray = payload.get("full_state", PackedByteArray())
	var replay_stream_value = payload.get("replay_stream")
	var canonical_prefix_count := int(payload.get("canonical_prefix_count", -1))
	if focus_index < 0 or settings.size() != racer_ids.size() \
			or track_index < 0 or track_index >= track_content_controller.tracks.size() \
			or cursor < 0 or cursor != _singleplayer_tick or full_state.is_empty() \
			or !replay_controller.replay_playback_active or !game_sim.sim_started \
			or race_session_controller.current_racer_ids != racer_ids \
			or !(replay_stream_value is MxtReplayStream) \
			or canonical_prefix_count != cursor:
		race_presentation_controller.show_notification("Replay resume failed: the captured frame is incomplete.", 4500)
		return
	var practice_cpu_flags: Array = []
	var cpu_ids: Array = []
	for index in racer_ids.size():
		var player_id := int(racer_ids[index])
		var is_cpu := index != focus_index
		practice_cpu_flags.append(is_cpu)
		if is_cpu:
			cpu_ids.append(player_id)
	var metadata_options: Dictionary = (metadata.get("race_options", {}) as Dictionary).duplicate(true) if typeof(metadata.get("race_options", {})) == TYPE_DICTIONARY else {}
	var configuration := MxtRaceConfiguration.new()
	configuration.load_metadata_dictionary(metadata_options)
	var track_evidence := MxtTrackContentEvidence.new()
	track_evidence.load_metadata_dictionary(metadata_options)
	configuration.session_kind = MxtRaceConfiguration.SESSION_PRACTICE
	configuration.leaderboard_eligible = false
	configuration.leaderboard_ineligible_reason = "practice"
	configuration.cpu_count = cpu_ids.size()
	configuration.practice_local_player_id = focused_player_id
	configuration.resumed_from_replay = true
	var race_state := metadata_options.duplicate(true)
	for key in network_manager.RACE_CONFIGURATION_METADATA_KEYS:
		race_state.erase(key)
	for key in network_manager.TRACK_EVIDENCE_METADATA_KEYS:
		race_state.erase(key)
	race_state["race_human_ids"] = [focused_player_id]
	race_state["race_cpu_ids"] = cpu_ids.duplicate(true)
	race_state["race_spectator_ids"] = []
	var exact_grid := PackedInt32Array()
	var grid_value = metadata.get("start_grid_slots", [])
	if typeof(grid_value) == TYPE_ARRAY and (grid_value as Array).size() == racer_ids.size():
		exact_grid.resize(racer_ids.size())
		for index in racer_ids.size():
			exact_grid[index] = int((grid_value as Array)[index])
	elif race_session_controller.current_start_grid_slots.size() == racer_ids.size():
		exact_grid = race_session_controller.current_start_grid_slots.duplicate()
	time_attack_ghost_descriptors.clear()
	var keep_original := bool(payload.get("keep_original_as_ghost", false))
	if keep_original:
		var ghost_prepare := time_attack_ghost_controller.prepare_original_replay(
			metadata,
			replay_stream_value as MxtReplayStream,
			focused_player_id,
			track_index)
		if !bool(ghost_prepare.get("success", false)):
			race_presentation_controller.show_notification(String(ghost_prepare.get("message", "Original ghost could not be prepared.")), 5000)
			return
	else:
		time_attack_ghost_controller.clear()
	replay_controller._apply_replay_focus_to_local_visual()
	if !replay_controller.detach_playback_for_practice():
		race_presentation_controller.show_notification("Replay resume failed while leaving playback.", 5000)
		return
	singleplayer_mode = true
	network_manager.set_spawn_seed(int(metadata.get("spawn_seed", 0)))
	network_manager.race_configuration = configuration
	network_manager.race_track_evidence = track_evidence
	network_manager.race_state = race_state
	network_manager.player_ids = [focused_player_id]
	network_manager.spectator_ids = []
	network_manager.lobby_settings.cpu_player_ids = cpu_ids.duplicate(true)
	network_manager.lobby_settings.set_race_cpu_roster(cpu_ids)
	network_manager.lobby_settings.clear_player_settings()
	for index in racer_ids.size():
		var player_id := int(racer_ids[index])
		var player_settings: Dictionary = (settings[index] as Dictionary).duplicate(true)
		network_manager.lobby_settings.set_player_settings(player_id, player_settings, index != focus_index)
	singleplayer_cpu_count = cpu_ids.size()
	time_attack_eligibility.clear()
	time_attack_finalized = false
	time_attack_last_replay_path = ""
	practice_controller.arm_local_player_override(focused_player_id)
	if !race_session_controller.reconfigure_practice_control(focused_player_id, practice_cpu_flags):
		practice_controller.end_session()
		race_presentation_controller.show_notification("Replay resume failed while transferring racer control.", 5000)
		_return_to_menu()
		return
	var resumed_roster: MxtRaceRoster = network_manager.lobby_settings.build_race_roster(racer_ids)
	if resumed_roster == null or resumed_roster.count() != racer_ids.size():
		practice_controller.end_session()
		race_presentation_controller.show_notification("Replay resume failed while rebuilding its racer roster.", 5000)
		_return_to_menu()
		return
	replay_controller.start_recording(track_index, resumed_roster, exact_grid)
	if !practice_controller.begin_resumed_session(configuration, focused_player_id, replay_stream_value as MxtReplayStream, canonical_prefix_count, transition_start_usec):
		race_presentation_controller.show_notification("Replay resume failed while restoring its canonical timeline.", 5000)
		_return_to_menu()
		return
	network_manager.race_results.restore_practice_state(payload.get("race_results", {}))
	race_dnf_low_speed_ticks = (payload.get("race_dnf_low_speed_ticks", {}) as Dictionary).duplicate(true) if typeof(payload.get("race_dnf_low_speed_ticks", {})) == TYPE_DICTIONARY else {}
	_singleplayer_tick = cursor
	network_manager.input_transport.clients_server_tick = cursor
	game_sim.discard_race_events()
	game_sim.snap_render_after_state_load()
	if keep_original:
		var ghost_start := time_attack_ghost_controller.start_race(track_index)
		if !bool(ghost_start.get("success", false)) or !time_attack_ghost_controller.fast_forward_to_tick(cursor):
			race_presentation_controller.show_notification("The original replay ghost could not reach the resume frame.", 5000)
			_return_to_menu()
			return
	else:
		time_attack_ghost_controller.start_race(track_index)
	$Control.visible = false
	lobby_control.visible = false
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false
	reconcile_practice_state_restore()
	practice_controller.finish_replay_resume_transition(transition_start_usec)
	race_presentation_controller.show_notification("Practice resumed at tick %d · 0.00x" % cursor, 3000)

func _on_join_button_pressed() -> void:
	_join_multiplayer_lobby(ip_field.text, _multiplayer_lobby_port())

func _join_multiplayer_lobby(address: String, port: int) -> void:
	var settings_dict = car_settings.get_player_settings().to_dict()
	var err := network_manager.join(address, port)
	if err != OK:
		return
	communication_controller.reset()
	network_manager.multiplayer.connected_to_server.connect(
		_send_connected_player_settings.bind(settings_dict),
		Object.CONNECT_ONE_SHOT)
	lobby_controller.refresh_controls()
	$Control.visible = false
	lobby_control.visible = true

func _on_join_playtest_button_pressed() -> void:
	if !_playtest_probe_should_run():
		return
	playtest_lobby_probe.set_enabled(false)
	join_playtest_button.visible = false
	_join_multiplayer_lobby(playtest_lobby_probe.server_address, playtest_lobby_probe.server_port)

func _on_playtest_lobby_availability_changed(available: bool) -> void:
	join_playtest_button.visible = available and _playtest_probe_should_run()

func _playtest_probe_should_run() -> bool:
	if headless_mode or network_manager.network_active or !$Control.visible or lobby_control.visible:
		return false
	if car_settings != null and car_settings.visible:
		return false
	if options_menu != null and options_menu.visible:
		return false
	if singleplayer_options_root != null and singleplayer_options_root.visible:
		return false
	if replay_controller != null and replay_controller.replay_catalog_root != null and replay_controller.replay_catalog_root.visible:
		return false
	return true

func _update_playtest_lobby_probe() -> void:
	var should_run := _playtest_probe_should_run()
	playtest_lobby_probe.set_enabled(should_run)
	if !should_run:
		join_playtest_button.visible = false

func _send_connected_player_settings(settings_dict: Dictionary) -> void:
	network_manager.lobby_settings.send_player_settings(settings_dict)
	network_manager.custom_stamp_network.send_active_custom_stamp_manifest()

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

func _build_singleplayer_race_options_screen() -> void:
	if singleplayer_options_root != null:
		return
	singleplayer_options_root = Control.new()
	singleplayer_options_root.name = "SingleplayerRaceOptions"
	singleplayer_options_root.visible = false
	singleplayer_options_root.layout_mode = 1
	singleplayer_options_root.anchor_right = 1.0
	singleplayer_options_root.anchor_bottom = 1.0
	add_child(singleplayer_options_root)

	var shade := ColorRect.new()
	shade.color = Color(0.0, 0.0, 0.0, 0.62)
	shade.layout_mode = 1
	shade.anchor_right = 1.0
	shade.anchor_bottom = 1.0
	singleplayer_options_root.add_child(shade)

	var center := CenterContainer.new()
	center.layout_mode = 1
	center.anchor_right = 1.0
	center.anchor_bottom = 1.0
	singleplayer_options_root.add_child(center)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(380.0, 0.0)
	center.add_child(panel)

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 18)
	margin.add_theme_constant_override("margin_top", 18)
	margin.add_theme_constant_override("margin_right", 18)
	margin.add_theme_constant_override("margin_bottom", 18)
	panel.add_child(margin)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	margin.add_child(box)

	var title := Label.new()
	title.text = "Race Options"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 28)
	box.add_child(title)

	singleplayer_options_restore_toggle = CheckBox.new()
	singleplayer_options_restore_toggle.text = " Vehicle Restore"
	singleplayer_options_restore_toggle.button_pressed = true
	box.add_child(singleplayer_options_restore_toggle)

	singleplayer_options_bumpers_toggle = CheckBox.new()
	singleplayer_options_bumpers_toggle.text = " Bumpers"
	box.add_child(singleplayer_options_bumpers_toggle)

	singleplayer_options_s_boost_toggle = CheckBox.new()
	singleplayer_options_s_boost_toggle.text = " S-BOOST"
	singleplayer_options_s_boost_toggle.button_pressed = true
	box.add_child(singleplayer_options_s_boost_toggle)

	var cpu_vehicle_row := HBoxContainer.new()
	cpu_vehicle_row.add_theme_constant_override("separation", 8)
	box.add_child(cpu_vehicle_row)
	var cpu_vehicle_label := Label.new()
	cpu_vehicle_label.text = "CPU Vehicles"
	cpu_vehicle_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cpu_vehicle_row.add_child(cpu_vehicle_label)
	singleplayer_options_cpu_vehicles = CpuVehiclePoolButtonClass.new()
	singleplayer_options_cpu_vehicles.custom_minimum_size.x = 190.0
	singleplayer_options_cpu_vehicles.initialize(vehicle_content_controller)
	cpu_vehicle_row.add_child(singleplayer_options_cpu_vehicles)

	var button_row := HBoxContainer.new()
	button_row.add_theme_constant_override("separation", 8)
	box.add_child(button_row)

	var back_button := Button.new()
	back_button.text = "Back"
	back_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	back_button.pressed.connect(_on_singleplayer_options_back_pressed)
	button_row.add_child(back_button)

	var start_button_local := Button.new()
	start_button_local.text = "Start Race"
	start_button_local.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	start_button_local.pressed.connect(_on_singleplayer_options_start_pressed)
	button_row.add_child(start_button_local)

func _build_default_singleplayer_race_configuration() -> MxtRaceConfiguration:
	var configuration := MxtRaceConfiguration.new()
	configuration.game_mode = 0
	configuration.vehicle_restore = network_manager.race_configuration.vehicle_restore
	configuration.bumpers = network_manager.race_configuration.bumpers or auto_bumpers_mode
	configuration.s_boost = network_manager.race_configuration.s_boost
	configuration.cpu_count = singleplayer_cpu_count
	return configuration

func _build_default_singleplayer_race_state() -> Dictionary:
	return {
		"grand_prix_current_track": 0,
		"grand_prix_points": {},
		"grand_prix_ko_energy_bonuses": {},
		"grand_prix_eliminated_ids": [],
	}

func _race_content_readiness(track_id: String, roster: MxtRaceRoster, track_evidence: MxtTrackContentEvidence) -> Dictionary:
	var missing_workshop_ids: Array = []
	var problems := PackedStringArray()
	var irrecoverable_problems := PackedStringArray()
	if track_evidence == null or track_evidence.count() == 0 or track_evidence.find_content_id(track_id) < 0:
		var problem := "race track content records are malformed"
		problems.append(problem)
		irrecoverable_problems.append(problem)
	else:
		for i in range(track_evidence.count()):
			if track_content_controller.track_content_evidence_matches(
					track_evidence.get_content_id(i), track_evidence.get_gameplay_digest(i),
					track_evidence.get_package_digest(i), track_evidence.get_workshop_id(i)):
				continue
			var workshop_id := track_evidence.get_workshop_id(i)
			var problem := "missing exact track %s" % track_evidence.get_content_id(i)
			if !_valid_workshop_id(workshop_id):
				problem = "non-Workshop track %s does not match the host build" % track_evidence.get_content_id(i)
				irrecoverable_problems.append(problem)
			problems.append(problem)
			_append_workshop_id(missing_workshop_ids, workshop_id)
	for roster_index in roster.count():
		var settings_value: Dictionary = roster.get_settings_dictionary(roster_index)
		var player_settings := PlayerSettings.new()
		player_settings.from_dict(settings_value)
		if player_settings.spectator or vehicle_content_controller.evidence_matches(player_settings):
			continue
		var problem := "missing exact vehicle %s" % player_settings.vehicle_content_id
		if !_valid_workshop_id(player_settings.vehicle_workshop_id):
			problem = "non-Workshop vehicle %s does not match the host build" % player_settings.vehicle_content_id
			irrecoverable_problems.append(problem)
		problems.append(problem)
		_append_workshop_id(missing_workshop_ids, player_settings.vehicle_workshop_id)
	return {
		"ready": problems.is_empty(),
		"workshop_ids": missing_workshop_ids,
		"detail": "; ".join(problems),
		"downloadable": irrecoverable_problems.is_empty() and !missing_workshop_ids.is_empty(),
	}

func _valid_workshop_id(value: String) -> bool:
	return value.is_valid_int() and value.to_int() > 0

func _append_workshop_id(ids: Array, value: String) -> void:
	if _valid_workshop_id(value):
		var published_file_id := value.to_int()
		if !ids.has(published_file_id):
			ids.append(published_file_id)

func _acquire_race_workshop_content(track_id: String, roster: MxtRaceRoster, track_evidence: MxtTrackContentEvidence) -> Dictionary:
	var readiness := _race_content_readiness(track_id, roster, track_evidence)
	if bool(readiness.get("ready", false)):
		return readiness
	var workshop_ids: Array = readiness.get("workshop_ids", [])
	if !bool(readiness.get("downloadable", false)) or steam_service == null or !steam_service.is_initialized():
		return readiness
	var tracked_any := false
	for published_file_id_value in workshop_ids:
		tracked_any = steam_service.track_workshop_item(int(published_file_id_value)) or tracked_any
	if tracked_any:
		steam_service.refresh_workshop_items()
		readiness = _race_content_readiness(track_id, roster, track_evidence)
		if bool(readiness.get("ready", false)):
			return readiness
	var workshop_id_strings := PackedStringArray()
	for published_file_id_value in workshop_ids:
		workshop_id_strings.append(str(int(published_file_id_value)))
	network_manager.race_admission.report(
		network_manager.race_admission.LOADING,
		"acquiring Workshop package%s %s" % [
			"" if workshop_ids.size() == 1 else "s",
			", ".join(workshop_id_strings),
		])
	var request_started := false
	for published_file_id_value in workshop_ids:
		var published_file_id := int(published_file_id_value)
		var accepted := steam_service.download_workshop_item(published_file_id, true)
		request_started = accepted or request_started
	if !request_started:
		return readiness
	steam_service.refresh_workshop_items()
	var deadline := Time.get_ticks_msec() + RACE_CONTENT_DOWNLOAD_TIMEOUT_MSEC
	var next_refresh_msec := Time.get_ticks_msec() + 1000
	while network_manager.race_active and Time.get_ticks_msec() < deadline:
		await get_tree().create_timer(0.1).timeout
		readiness = _race_content_readiness(track_id, roster, track_evidence)
		if bool(readiness.get("ready", false)):
			return readiness
		var now := Time.get_ticks_msec()
		if now >= next_refresh_msec:
			steam_service.refresh_workshop_items()
			next_refresh_msec = now + 1000
	readiness["detail"] = "%s after Workshop download timeout" % String(readiness.get("detail", "content unavailable"))
	return readiness

func _open_singleplayer_race_options(as_spectator: bool) -> void:
	_build_singleplayer_race_options_screen()
	singleplayer_options_as_spectator = as_spectator
	var configuration := _build_default_singleplayer_race_configuration()
	singleplayer_options_restore_toggle.set_pressed_no_signal(configuration.vehicle_restore)
	singleplayer_options_bumpers_toggle.set_pressed_no_signal(configuration.bumpers)
	singleplayer_options_s_boost_toggle.set_pressed_no_signal(configuration.s_boost)
	$Control.visible = false
	lobby_control.visible = false
	singleplayer_options_root.visible = true
	singleplayer_options_restore_toggle.grab_focus()

func _build_singleplayer_race_configuration_from_controls() -> MxtRaceConfiguration:
	var configuration := _build_default_singleplayer_race_configuration()
	if singleplayer_options_restore_toggle != null:
		configuration.vehicle_restore = singleplayer_options_restore_toggle.button_pressed
	if singleplayer_options_bumpers_toggle != null:
		configuration.bumpers = singleplayer_options_bumpers_toggle.button_pressed or auto_bumpers_mode
	if singleplayer_options_s_boost_toggle != null:
		configuration.s_boost = singleplayer_options_s_boost_toggle.button_pressed
	if singleplayer_options_cpu_vehicles != null:
		configuration.cpu_vehicle_content_ids = PackedStringArray(singleplayer_options_cpu_vehicles.selected_content_ids())
	return configuration

func _on_singleplayer_options_back_pressed() -> void:
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false
	$Control.visible = true

func _on_singleplayer_options_start_pressed() -> void:
	_start_singleplayer_race(singleplayer_options_as_spectator, _build_singleplayer_race_configuration_from_controls())

func _on_multiplayer_port_submitted(_text: String) -> void:
	_on_join_button_pressed()

func _multiplayer_lobby_port() -> int:
	if port_field == null:
		return 27016
	var parsed_port := port_field.text.to_int()
	if parsed_port <= 0:
		return 27016
	return int(clamp(parsed_port, 1, 65535))

func _on_pause_retry_pressed() -> void:
	var configuration := network_manager.race_configuration.copy()
	if !singleplayer_mode or (!configuration.is_time_attack() and !configuration.is_practice()):
		return
	var race_state := network_manager.race_state.duplicate(true)
	var track_evidence := network_manager.race_track_evidence.copy()
	race_pause_controller.close()
	_return_to_menu(configuration.is_practice())
	call_deferred("_start_singleplayer_race", false, configuration, race_state, track_evidence)

func _on_pause_disconnect_pressed() -> void:
	race_pause_controller.close()
	_return_to_menu()

func _on_pause_lobby_pressed() -> void:
	race_pause_controller.close()
	if network_manager.is_server:
		network_manager.send_end_race()

func _on_pause_options_requested() -> void:
	options_menu.call("open_settings")

func _initialize_grand_prix_options(configuration: MxtRaceConfiguration, options: Dictionary, roster: Array) -> Dictionary:
	var initialized := options.duplicate(true)
	if configuration.game_mode != 1:
		return initialized
	var points := {}
	for id in roster:
		points[int(id)] = 0
	initialized["grand_prix_current_track"] = 0
	initialized["grand_prix_points"] = points
	initialized["grand_prix_ko_energy_bonuses"] = {}
	initialized["grand_prix_eliminated_ids"] = []
	return initialized

func _settings_dict_for_race_id(id: int, fallback_cpu_index: int = 0) -> Dictionary:
	var settings: Dictionary = network_manager.lobby_settings.get_player_settings(id)
	if settings.is_empty():
		if network_manager.lobby_settings.cpu_player_ids.has(id):
			settings = build_cpu_player_settings(fallback_cpu_index)
		else:
			var vehicle_content_id: String = vehicle_content_controller.definitions[0].content_id if vehicle_content_controller.definitions.size() > 0 else ""
			settings = {"vehicle_content_id": vehicle_content_id, "accel_setting": 1.0, "username": str(id)}
			settings.merge(vehicle_content_controller.get_evidence(vehicle_content_id), true)
	return settings.duplicate(true)

func _apply_race_roster_options(options: Dictionary, human_ids: Array, cpu_ids: Array, spectator_ids: Array = []) -> Dictionary:
	var out := options.duplicate(true)
	out["race_human_ids"] = human_ids.duplicate(true)
	out["race_cpu_ids"] = cpu_ids.duplicate(true)
	out["race_spectator_ids"] = spectator_ids.duplicate(true)
	return out

func _local_player_id() -> int:
	if practice_controller != null and practice_controller.local_player_id_override >= 0:
		return practice_controller.local_player_id_override
	if replay_controller.replay_playback_active:
		return replay_controller.replay_playback_local_player_id
	if singleplayer_mode:
		return 0
	return multiplayer.get_unique_id() if network_manager.has_network_peer() else 0

func _build_start_sync_drop_panel() -> void:
	start_sync_drop_root = PanelContainer.new()
	start_sync_drop_root.name = "StartSyncDropPanel"
	start_sync_drop_root.visible = false
	start_sync_drop_root.mouse_filter = Control.MOUSE_FILTER_STOP
	start_sync_drop_root.custom_minimum_size = Vector2(420.0, 0.0)
	start_sync_drop_root.anchor_left = 0.5
	start_sync_drop_root.anchor_right = 0.5
	start_sync_drop_root.anchor_top = 0.18
	start_sync_drop_root.anchor_bottom = 0.18
	start_sync_drop_root.offset_left = -210.0
	start_sync_drop_root.offset_right = 210.0
	add_child(start_sync_drop_root)

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 18)
	margin.add_theme_constant_override("margin_top", 14)
	margin.add_theme_constant_override("margin_right", 18)
	margin.add_theme_constant_override("margin_bottom", 14)
	start_sync_drop_root.add_child(margin)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	margin.add_child(box)

	start_sync_drop_label = Label.new()
	start_sync_drop_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	start_sync_drop_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	start_sync_drop_label.add_theme_font_size_override("font_size", 18)
	box.add_child(start_sync_drop_label)

	start_sync_drop_button = Button.new()
	start_sync_drop_button.text = "Drop Stalled Players"
	start_sync_drop_button.focus_mode = Control.FOCUS_NONE
	start_sync_drop_button.pressed.connect(_on_start_sync_drop_pressed)
	box.add_child(start_sync_drop_button)

func _update_start_sync_drop_panel() -> void:
	if start_sync_drop_root == null:
		return
	if network_manager == null or !network_manager.race_active or game_sim.sim_started:
		start_sync_drop_root.visible = false
		return
	var info := network_manager.race_admission.drop_info()
	if !bool(info.get("visible", false)):
		start_sync_drop_root.visible = false
		return
	var names: PackedStringArray = info.get("stalled_names", PackedStringArray())
	var stages: PackedStringArray = info.get("stalled_stages", PackedStringArray())
	var details: PackedStringArray = info.get("stalled_details", PackedStringArray())
	var remaining := float(info.get("drop_remaining_sec", 0.0))
	var entries := PackedStringArray()
	for i in range(names.size()):
		var entry := names[i]
		if i < stages.size() and !stages[i].is_empty():
			entry += " (%s)" % stages[i]
		if i < details.size() and !details[i].is_empty():
			entry += ": %s" % details[i]
		entries.append(entry)
	var title := "Waiting for " + ", ".join(entries)
	if bool(info.get("can_drop", false)):
		start_sync_drop_label.text = title + "."
		start_sync_drop_button.disabled = false
	else:
		start_sync_drop_label.text = title + ". Drop available in %.1fs." % remaining
		start_sync_drop_button.disabled = true
	start_sync_drop_root.visible = true

func _on_start_sync_drop_pressed() -> void:
	if network_manager != null and network_manager.race_admission.request_drop_stalled_players():
		start_sync_drop_root.visible = false

func _on_race_event(event_type: String, actor_id: int, target_id: int, tick_value: int, value: int) -> void:
	if event_type == "sticker":
		race_presentation_controller.show_sticker(actor_id, value)
		return
	if event_type == "eliminated":
		if actor_id == _local_player_id() and replay_controller.should_enqueue_replay_race_notification():
			spectator_controller.activate_local_elimination()
		return
	if event_type == "dnf":
		if actor_id == _local_player_id():
			race_presentation_controller.show_notification("DNF - Spectating", 3000)
			spectator_controller.change_focus(1)
		return
	if event_type == "ko":
		if replay_controller.should_enqueue_replay_race_notification():
			race_presentation_controller.show_ko_medal(actor_id, target_id)
		return
	if event_type == "finish":
		if actor_id == _local_player_id():
			race_audio_controller.begin_local_finish()
		if replay_controller.should_enqueue_replay_race_notification():
			race_presentation_controller.show_finish_medal(actor_id, tick_value)

func _consume_authoritative_race_events() -> void:
	if singleplayer_mode:
		for event in game_sim.consume_race_events():
			if replay_controller.replay_collecting_timeline_markers:
				replay_controller.record_timeline_event(event)
			_on_race_event("ko", int(event["actor_id"]), int(event["target_id"]), int(event["tick"]), int(event["value"]))
		return
	if network_manager.is_server and server_game_sim != null:
		for event in server_game_sim.consume_race_events():
			network_manager.race_results.send_race_event("ko", int(event["actor_id"]), int(event["target_id"]), int(event["tick"]), int(event["value"]))
	else:
		game_sim.consume_race_events()

func _on_car_settings_button_pressed() -> void:
	car_settings.call("open_settings")

func _on_controller_settings_button_pressed() -> void:
	options_menu.call("open_settings")

func _on_vehicle_view_distance_changed(multiplier: float, render_all: bool) -> void:
	game_sim.set_render_car_body_view_distance(
		base_vehicle_render_view_distance * clampf(multiplier, 1.0, 3.0))
	game_sim.set_render_all_car_bodies(render_all or debug_runtime_controller.render_all_car_bodies)

func _on_controller_settings_visibility_changed() -> void:
	if options_menu != null and !options_menu.visible:
		replay_controller.reload_input_calibration()
		race_pause_controller.on_options_visibility_changed(game_sim.sim_started)

func _close_settings_menus_for_race_start() -> void:
	if car_settings != null:
		if car_settings.visible and car_settings.has_method("_on_close_pressed"):
			car_settings.call("_on_close_pressed")
		else:
			car_settings.hide()
	if options_menu != null:
		if options_menu.visible and options_menu.has_method("close_settings"):
			options_menu.call("close_settings")
		else:
			options_menu.hide()

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

@onready var world_environment: WorldEnvironment = $GameWorld/WorldEnvironment
@onready var track_floor: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/TrackFloor
@onready var track_clouds: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/TrackClouds
@onready var directional_light_3d: DirectionalLight3D = $GameWorld/DirectionalLight3D
@onready var default_world_environment_resource: Environment = world_environment.environment

func _on_lobby_start_race_requested(configuration: MxtRaceConfiguration, track_evidence: MxtTrackContentEvidence, requested_options: Dictionary) -> void:
	if !network_manager.is_server:
		return
	_close_settings_menus_for_race_start()
	network_manager.prepare_race_roster("start_button")
	var human_ids := network_manager.player_ids.duplicate(true)
	var cpu_ids := network_manager.lobby_settings.cpu_player_ids.duplicate(true)
	configuration.cpu_count = cpu_ids.size()
	var roster := human_ids.duplicate(true)
	roster.append_array(cpu_ids)
	for id_value in roster:
		var id := int(id_value)
		if !network_manager.lobby_settings.has_player_settings(id):
			network_manager.lobby_settings.set_player_settings(id, _settings_dict_for_race_id(id, cpu_ids.find(id)), cpu_ids.has(id))
	var race_roster: MxtRaceRoster = network_manager.lobby_settings.build_race_roster(roster)
	if race_roster == null:
		return
	var race_state := _initialize_grand_prix_options(configuration, requested_options, roster)
	race_state = _apply_race_roster_options(race_state, human_ids, cpu_ids, network_manager.spectator_ids)
	if track_evidence.count() == 0:
		return
	network_manager.send_start_race(track_evidence.get_content_id(0), race_roster, configuration, track_evidence, race_state)

func _on_network_race_started(track_id: String, roster: MxtRaceRoster) -> void:
	var admission_phase := network_manager.race_netplay_phase
	var readiness: Dictionary = await _acquire_race_workshop_content(track_id, roster, network_manager.race_track_evidence)
	if !network_manager.race_active or network_manager.race_netplay_phase != admission_phase:
		return
	if !bool(readiness.get("ready", false)):
		var evidence_message := String(readiness.get("detail", "Race content evidence mismatch for %s" % track_id))
		network_manager.race_admission.report(network_manager.race_admission.FAILED, evidence_message)
		push_error(evidence_message)
		race_presentation_controller.show_notification(evidence_message, 5000)
		if headless_mode:
			get_tree().quit(1)
		return
	var track_index := track_content_controller.track_index_for_id(track_id)
	if track_index < 0:
		var message := "Missing race track %s" % track_id
		network_manager.race_admission.report(network_manager.race_admission.FAILED, message)
		push_error(message)
		race_presentation_controller.show_notification(message, 5000)
		if headless_mode:
			get_tree().quit(1)
		return
	network_manager.race_admission.report(network_manager.race_admission.LOADING, "loading %s" % track_id)
	# Return to the multiplayer loop once before beginning the synchronous load so
	# the server receives the explicit loading acknowledgement.
	await get_tree().process_frame
	if !network_manager.race_active or network_manager.race_netplay_phase != admission_phase:
		return
	race_dnf_low_speed_ticks.clear()
	if start_sync_drop_root != null:
		start_sync_drop_root.visible = false
	communication_controller.close_race_chat()
	_close_settings_menus_for_race_start()
	if !race_session_controller.start_race(track_index, roster, singleplayer_mode, headless_mode):
		network_manager.race_admission.report(network_manager.race_admission.FAILED, "race initialization failed")
		return
	game_sim.set_sim_started(false)
	if network_manager.is_server:
		server_game_sim.set_sim_started(false)
	network_manager.race_admission.report(network_manager.race_admission.READY, "race initialized")

func _on_network_race_finished() -> void:
	if headless_mode and network_manager.pending_next_race_track_id == "":
		return
	race_presentation_controller.hide_results()
	race_presentation_controller.clear_stickers()
	if network_manager.pending_next_race_track_id != "":
		_transition_to_next_grand_prix_race()
	else:
		_return_to_lobby()

func _window_accepts_input() -> bool:
	if race_pause_controller.open:
		return false
	if communication_controller.is_race_chat_open():
		return false
	var window := get_window()
	return window == null or window.has_focus()

func _physics_process(delta: float) -> void:
	if auto_track_editor_mode:
		return
	if headless_mode:
		if singleplayer_mode and game_sim.sim_started:
			if replay_controller.replay_playback_active:
				replay_controller.simulate_playback()
			else:
				_simulate_singleplayer_tick()
			if debug_runtime_controller.quit_after_frames >= 0 and _singleplayer_tick >= debug_runtime_controller.quit_after_frames:
				get_tree().quit()
			return
		if network_manager.has_network_peer():
			var pi := _generate_random_input()
			network_manager.input_transport.set_local_input(pi.serialize())
			network_manager.input_transport.collect_client_inputs()
		return
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if lobby_control.visible:
		lobby_controller.process_lobby(delta)
	else:
		lobby_controller.clear()
	if game_sim.sim_started and !practice_controller.blocks_automatic_ticks():
		_run_active_race_physics_frame(delta)


func _run_active_race_physics_frame(delta: float) -> void:
	var profile_enabled: bool = debug_runtime_controller.render_profile_enabled
	var profile_physics_start := Time.get_ticks_usec() if profile_enabled else 0
	var profile_input_start := Time.get_ticks_usec() if profile_enabled else 0
	var local_pi := PlayerInputClass.new()
	if !spectator_controller.should_suppress_local_race_input() and _window_accepts_input() and race_session_controller.players.size() > race_session_controller.local_player_index:
		var controller = race_session_controller.players[race_session_controller.local_player_index]
		if controller != null:
			local_pi = controller.get_input()
	var input_bytes := local_pi.serialize()
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.INPUT, profile_input_start)
	if singleplayer_mode and spectator_controller.is_local_dnf() and game_sim.has_method("get_native_cpu_input_for_tick"):
		input_bytes = game_sim.get_native_cpu_input_for_tick(_local_player_id(), _singleplayer_tick)
	if practice_controller.session_active:
		input_bytes = practice_controller.resolve_local_input(input_bytes)
	if singleplayer_mode:
		var profile_tick_start := Time.get_ticks_usec() if profile_enabled else 0
		if replay_controller.replay_playback_active:
			replay_controller.simulate_playback()
		else:
			_simulate_singleplayer_tick(input_bytes)
		if profile_enabled:
			debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.TICK, profile_tick_start)
		if debug_runtime_controller.quit_after_frames >= 0 and _singleplayer_tick >= debug_runtime_controller.quit_after_frames:
			debug_runtime_controller.print_profile_summary(singleplayer_cpu_count, launch_cpu_driver_count)
			RenderingServer.force_sync()
			get_tree().quit()
			return
	else:
		network_manager.input_transport.set_local_input(input_bytes)
		if network_manager.is_server:
			_simulate_host_frame(input_bytes)
		else:
			_simulate_single_tick()
	var profile_events_start := Time.get_ticks_usec() if profile_enabled else 0
	if !replay_controller.replay_playback_active:
		_consume_authoritative_race_events()
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.EVENTS, profile_events_start)
	var profile_camera_start := Time.get_ticks_usec() if profile_enabled else 0
	_update_native_render_camera()
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.CAMERA, profile_camera_start)
	var profile_render_start := Time.get_ticks_usec() if profile_enabled else 0
	game_sim.render_gamesim()
	_sync_gameplay_camera_settings()
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.RENDER, profile_render_start)
	var profile_audio_tick_start := Time.get_ticks_usec() if profile_enabled else 0
	race_audio_controller.after_simulation_tick()
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.AUDIO_TICK, profile_audio_tick_start)
	var profile_nametag_start := Time.get_ticks_usec() if profile_enabled else 0
	race_presentation_controller.update_nametags(get_viewport().get_camera_3d(), delta, debug_runtime_controller.is_hud_hidden())
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.NAMETAG, profile_nametag_start)
	var profile_finish_check_start := Time.get_ticks_usec() if profile_enabled else 0
	if !replay_controller.replay_playback_active:
		_check_race_finished()
	if practice_controller.session_active:
		practice_controller.capture_completed_tick(_singleplayer_tick - 1)
	if profile_enabled:
		debug_runtime_controller.record_phase(DebugRuntimeControllerClass.ProfilePhase.FINISH_CHECK, profile_finish_check_start)
		debug_runtime_controller.record_physics_frame(profile_physics_start)

func _simulate_singleplayer_tick(input_bytes: PackedByteArray = PackedByteArray()):
	var start_time := Time.get_ticks_usec()
	if replay_controller.replay_playback_active:
		replay_controller.simulate_playback()
		network_manager.input_transport.rollback_frametime_us = Time.get_ticks_usec() - start_time
		return
	input_bytes = replay_controller.consume_debug_playback_input(input_bytes)
	if !game_sim.sim_started:
		return
	if input_bytes.is_empty():
		var local_pi := PlayerInputClass.new()
		if debug_runtime_controller.auto_accelerate:
			local_pi.accelerate = 1.0
		var accepts_input := _window_accepts_input()
		if !debug_runtime_controller.auto_accelerate and !spectator_controller.should_suppress_local_race_input() and accepts_input and race_session_controller.players.size() > race_session_controller.local_player_index:
			var controller = race_session_controller.players[race_session_controller.local_player_index]
			if controller != null:
				local_pi = controller.get_input()
		input_bytes = local_pi.serialize()
		if spectator_controller.is_local_dnf() and game_sim.has_method("get_native_cpu_input_for_tick"):
			input_bytes = game_sim.get_native_cpu_input_for_tick(_local_player_id(), _singleplayer_tick)
	replay_controller.record_debug_input(input_bytes)
	_dump_offline_state_sample()
	var tick_to_record := _singleplayer_tick
	game_sim.tick_singleplayer(_local_player_id(), input_bytes)
	if time_attack_ghost_controller != null:
		time_attack_ghost_controller.tick(tick_to_record)
	replay_controller.record_singleplayer_frame(tick_to_record)
	_singleplayer_tick += 1
	if debug_runtime_controller.bumper_smoke_enabled and _singleplayer_tick % 120 == 0 and game_sim.has_method("get_bumper_debug_string"):
		print("MXT_BUMPER_SMOKE tick=", _singleplayer_tick, " ", game_sim.get_bumper_debug_string())
	# Update HUD timing using the same field clients use
	network_manager.input_transport.clients_server_tick = _singleplayer_tick
	network_manager.input_transport.rollback_frametime_us = Time.get_ticks_usec() - start_time


func reconcile_practice_state_restore() -> void:
	if !practice_controller.session_active or !game_sim.sim_started:
		return
	race_presentation_controller.clear_practice_restore_transients()
	spectator_controller.reconcile_after_practice_state_restore()
	var local_id := _local_player_id()
	race_audio_controller.reconcile_practice_state_restore(
		_singleplayer_tick,
		game_sim.get_player_lap(local_id),
		network_manager.race_results.player_finish_times.has(local_id))
	_update_native_render_camera()
	game_sim.render_gamesim()
	_sync_gameplay_camera_settings()

func _dump_offline_state_sample() -> void:
	if !network_manager.telemetry.dump_state_samples:
		return
	if game_sim == null:
		return
	if _singleplayer_tick % network_manager.state_transfer.BROADCAST_INTERVAL_TICKS != 0:
		return
	var state := game_sim.get_state_data(_singleplayer_tick)
	network_manager.telemetry.dump_state_sample(
		state,
		_singleplayer_tick,
		network_manager.get_simulation_roster().size()
	)

func _simulate_host_frame(local_input_bytes: PackedByteArray):
	var loops := 0
	const MAX_TICKS_PER_FRAME := 120
	while loops < MAX_TICKS_PER_FRAME:
		network_manager.input_transport.set_local_input(local_input_bytes)
		var collected := network_manager.input_transport.collect_server_inputs()
		if !collected:
			break
		network_manager.input_transport.post_tick()
		loops += 1
	network_manager.input_transport.collect_client_inputs()

func _simulate_single_tick():
	var loops := 0
	const MAX_TICKS_PER_FRAME := 120
	while loops < MAX_TICKS_PER_FRAME:
		var collected_client := network_manager.input_transport.collect_client_inputs()
		if !collected_client:
			return
		if network_manager.is_server:
			var collected_server := network_manager.input_transport.collect_server_inputs()
			if collected_server:
				network_manager.input_transport.post_tick()
		else:
			network_manager.input_transport.post_tick()
		loops += 1
		if network_manager.is_server or network_manager.input_transport.local_tick >= network_manager.input_transport.clients_target_tick:
			return

func _unhandled_input(event: InputEvent) -> void:
	if communication_controller.handle_unhandled_input(event):
		return
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F3:
		debug_runtime_controller.copy_native_profile_to_clipboard()
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F4:
		debug_runtime_controller.take_clean_4k_screenshot()
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F6:
		var export_result: Dictionary = network_manager.telemetry.export_telemetry_history()
		if bool(export_result.get("success", false)):
			print("Netplay telemetry exported: %s (%d samples)" % [
				str(export_result.get("path", "")),
				int(export_result.get("sample_count", 0)),
			])
		else:
			push_warning(str(export_result.get("error", "Netplay telemetry export failed.")))
		get_viewport().set_input_as_handled()
		return
	if race_pause_controller.options_open and options_menu != null and options_menu.visible \
			and event.is_action_pressed("ui_cancel"):
		options_menu.call("close_settings")
		get_viewport().set_input_as_handled()
		return
	if race_pause_controller.handle_input(event):
		get_viewport().set_input_as_handled()
		return
	if practice_controller.handle_runtime_input(event):
		get_viewport().set_input_as_handled()
		return
	if game_sim.sim_started and !race_pause_controller.options_open and event.is_action_pressed("Pause"):
		if race_pause_controller.open:
			race_pause_controller.close()
		else:
			race_pause_controller.open_for_race(network_manager.race_configuration,
				singleplayer_mode, network_manager.is_server and !singleplayer_mode)
		get_viewport().set_input_as_handled()
		return
	if replay_controller.handle_unhandled_input(event):
		get_viewport().set_input_as_handled()
		return
	if game_sim.sim_started and spectator_controller.handle_unhandled_input(event):
		get_viewport().set_input_as_handled()
		return
	if lobby_control.visible and event.is_action_pressed("ui_cancel"):
		_close_settings_menus_for_race_start()
		_return_to_menu()
		get_viewport().set_input_as_handled()
	if game_sim.sim_started and event.is_action_pressed("ui_cancel"):
		if race_pause_controller.open:
			race_pause_controller.close()
		else:
			race_pause_controller.open_for_race(network_manager.race_configuration,
				singleplayer_mode, network_manager.is_server and !singleplayer_mode)
		get_viewport().set_input_as_handled()

func _return_to_menu(preserve_practice_speed_for_retry: bool = false) -> void:
	communication_controller.close_race_chat()
	practice_controller.end_session(preserve_practice_speed_for_retry)
	race_session_controller.begin_transition(singleplayer_mode, 0.5)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	race_pause_controller.close()
	if time_attack_ghost_controller != null:
		time_attack_ghost_controller.teardown_runtime()
	race_session_controller.destroy_world(true, true)
	race_dnf_low_speed_ticks.clear()
	singleplayer_mode = false
	_singleplayer_tick = 0
	time_attack_eligibility.clear()
	time_attack_finalized = false
	$Control.visible = true
	lobby_control.visible = false
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false
	if vehicle_test_drive_active:
		_finish_vehicle_test_drive_return()

func _return_to_lobby() -> void:
	communication_controller.close_race_chat()
	practice_controller.end_session()
	race_session_controller.begin_transition(singleplayer_mode, 0.5)
	race_pause_controller.close()
	if time_attack_ghost_controller != null:
		time_attack_ghost_controller.teardown_runtime()
	race_session_controller.destroy_world(false, false)
	race_dnf_low_speed_ticks.clear()
	lobby_control.visible = true
	network_manager.flush_waiting_peers()
	network_manager.reset_race_state(true)
	network_manager.broadcast_lobby_roster()
	singleplayer_mode = false
	_singleplayer_tick = 0

func _teardown_race_world_for_transition() -> void:
	practice_controller.end_session()
	race_session_controller.begin_transition(singleplayer_mode)
	race_pause_controller.close()
	if time_attack_ghost_controller != null:
		time_attack_ghost_controller.teardown_runtime()
	race_session_controller.destroy_world(false, false)
	race_dnf_low_speed_ticks.clear()
	lobby_control.visible = false
	singleplayer_mode = false
	_singleplayer_tick = 0

func _transition_to_next_grand_prix_race() -> void:
	var next_track_id := network_manager.pending_next_race_track_id
	var next_roster: MxtRaceRoster = network_manager.pending_next_race_roster.copy() if network_manager.pending_next_race_roster != null else null
	var next_configuration := network_manager.pending_next_race_configuration.copy() if network_manager.pending_next_race_configuration != null else network_manager.race_configuration.copy()
	var next_track_evidence := network_manager.pending_next_race_track_evidence.copy() if network_manager.pending_next_race_track_evidence != null else network_manager.race_track_evidence.copy()
	var next_options := network_manager.pending_next_race_state.duplicate(true)
	_teardown_race_world_for_transition()
	if network_manager.is_server:
		network_manager.flush_waiting_peers(true)
	network_manager.reset_race_state(true)
	network_manager.race_configuration = next_configuration
	network_manager.race_track_evidence = next_track_evidence
	network_manager.race_state = next_options
	_apply_grand_prix_eliminations(next_options)
	if next_roster != null:
		network_manager.start_race(next_track_id, next_roster.encode_wire(), next_configuration.encode_wire(), next_track_evidence.encode_wire(), next_options)

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
		if network_manager.lobby_settings.cpu_player_ids.has(id):
			network_manager.lobby_settings.cpu_player_ids.erase(id)
			network_manager.lobby_settings.remove_player(id)

func _lookup_id_value(dict: Dictionary, id: int, fallback):
	if dict.has(id):
		return dict[id]
	var id_string := str(id)
	if dict.has(id_string):
		return dict[id_string]
	return fallback

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
	var options := network_manager.race_state.duplicate(true)
	var current_track_index := int(options.get("grand_prix_current_track", 0))
	if int(options.get("grand_prix_recorded_track", -1)) == current_track_index:
		return
	var points: Dictionary = options.get("grand_prix_points", {})
	var race_racers := network_manager.get_simulation_roster()
	var racer_count := race_racers.size()
	var place_by_id := _build_final_race_place_map(sim, race_racers)
	var finish_tick_by_id := _build_final_race_finish_tick_map(place_by_id)
	network_manager.race_results.send_final_race_results(place_by_id, finish_tick_by_id)
	for id_value in race_racers:
		var id := int(id_value)
		var total := int(_lookup_id_value(points, id, 0))
		var place := int(_lookup_id_value(place_by_id, id, 0))
		if place > 0:
			total += maxi(0, racer_count - place + 1)
		points[id] = total
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	if !network_manager.is_vehicle_restore_enabled():
		for id_value in network_manager.race_results.player_eliminations.keys():
			var id := int(id_value)
			if !eliminated_ids.has(id):
				eliminated_ids.append(id)
	options["grand_prix_points"] = points
	options["grand_prix_eliminated_ids"] = eliminated_ids
	options["grand_prix_ko_energy_bonuses"] = _capture_grand_prix_ko_energy_bonuses(sim)
	options["grand_prix_recorded_track"] = current_track_index
	network_manager.race_state = options
	network_manager.send_race_state(
		network_manager.race_configuration, network_manager.race_track_evidence, options)

func _build_final_race_place_map(sim: GameSim, race_racers: Array) -> Dictionary:
	var place_by_id := {}
	var placement_rows := []
	for id_value in race_racers:
		var id := int(id_value)
		if network_manager.race_results.player_dnfs.has(id):
			continue
		var place := int(_lookup_id_value(network_manager.race_results.player_finish_placements, id, 0))
		if place > 0:
			var finish_tick := int(_lookup_id_value(network_manager.race_results.player_finish_times, id, 0x7fffffff))
			placement_rows.append([place, finish_tick, id])
	placement_rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		if int(a[1]) != int(b[1]):
			return int(a[1]) < int(b[1])
		return int(a[2]) < int(b[2])
	)
	for row in placement_rows:
		var id := int(row[2])
		place_by_id[id] = place_by_id.size() + 1
	var finish_rows := []
	for id_value in race_racers:
		var id := int(id_value)
		if place_by_id.has(id) or network_manager.race_results.player_dnfs.has(id):
			continue
		var finish_tick := int(_lookup_id_value(network_manager.race_results.player_finish_times, id, -1))
		if finish_tick >= 0:
			finish_rows.append([finish_tick, id])
	finish_rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		return int(a[1]) < int(b[1])
	)
	for row in finish_rows:
		var id := int(row[1])
		place_by_id[id] = place_by_id.size() + 1
	if sim == null or !sim.has_method("get_race_order"):
		return place_by_id
	var order: Array = sim.get_race_order()
	for id_value in order:
		var id := int(id_value)
		if !race_racers.has(id) or place_by_id.has(id):
			continue
		if network_manager._disconnected_during_race.has(id) or network_manager.race_results.player_eliminations.has(id) or network_manager.race_results.player_dnfs.has(id):
			continue
		place_by_id[id] = place_by_id.size() + 1
	return place_by_id

func _build_final_race_finish_tick_map(place_by_id: Dictionary) -> Dictionary:
	var finish_tick_by_id := {}
	for id_value in place_by_id.keys():
		var id := int(id_value)
		var finish_tick := int(_lookup_id_value(network_manager.race_results.player_finish_times, id, -1))
		if finish_tick >= 0:
			finish_tick_by_id[id] = finish_tick
	return finish_tick_by_id

func _build_next_grand_prix_roster(options: Dictionary) -> MxtRaceRoster:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var active_ids := network_manager.player_ids.duplicate(true)
	active_ids.append_array(network_manager.lobby_settings.cpu_player_ids)
	for index in range(active_ids.size() - 1, -1, -1):
		if eliminated_ids.has(int(active_ids[index])):
			active_ids.remove_at(index)
	return network_manager.lobby_settings.build_race_roster(active_ids)

func _build_next_grand_prix_rosters(options: Dictionary) -> Dictionary:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var human_ids := []
	for id_value in network_manager.player_ids:
		var id := int(id_value)
		if !eliminated_ids.has(id):
			human_ids.append(id)
	var cpu_ids := []
	for id_value in network_manager.lobby_settings.cpu_player_ids:
		var id := int(id_value)
		if !eliminated_ids.has(id):
			cpu_ids.append(id)
	var spectator_ids := network_manager.spectator_ids.duplicate(true)
	for id_value in network_manager.waiting_peers:
		var id := int(id_value)
		if !spectator_ids.has(id):
			spectator_ids.append(id)
	return {
		"human_ids": human_ids,
		"cpu_ids": cpu_ids,
		"spectator_ids": spectator_ids,
	}

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
	var options := network_manager.race_state.duplicate(true)
	var current_index := int(options.get("grand_prix_current_track", 0))
	var next_index := current_index + 1
	if next_index >= network_manager.race_track_evidence.count() or !_has_active_human_grand_prix_racer(options):
		network_manager.send_end_race()
		return
	options["grand_prix_current_track"] = next_index
	var next_track_id := network_manager.race_track_evidence.get_content_id(next_index)
	var next_roster: MxtRaceRoster = _build_next_grand_prix_roster(options)
	if next_roster == null:
		network_manager.send_end_race()
		return
	var next_rosters := _build_next_grand_prix_rosters(options)
	options = _apply_race_roster_options(
		options,
		next_rosters["human_ids"],
		next_rosters["cpu_ids"],
		next_rosters["spectator_ids"])
	options = network_manager.reserve_next_race_netplay_state(options)
	var seed := randi()
	options["spawn_seed"] = seed
	network_manager.send_end_race(next_track_id, next_roster, network_manager.race_configuration, network_manager.race_track_evidence, options)

func _race_control_has_started(sim: GameSim) -> bool:
	if sim == null:
		return network_manager.input_transport.get_race_tick() >= 300
	return network_manager.input_transport.get_race_tick() >= sim.get_race_control_start_tick()

func _mark_racer_dnf(racer_id: int, reason: String) -> void:
	var tick := network_manager.input_transport.get_race_tick()
	if network_manager.is_server and !singleplayer_mode:
		network_manager.race_results.send_player_dnf(racer_id, tick, reason)
	else:
		network_manager.race_results.record_player_dnf(racer_id, tick, reason)

func _update_low_speed_dnf(racer_id: int, finish_sim: GameSim, race_control_started: bool) -> bool:
	if singleplayer_mode:
		race_dnf_low_speed_ticks.erase(racer_id)
		return false
	if finish_sim == null:
		return false
	if !race_control_started:
		race_dnf_low_speed_ticks.erase(racer_id)
		return false
	if network_manager.race_results.player_finish_times.has(racer_id) or network_manager.race_results.player_dnfs.has(racer_id):
		race_dnf_low_speed_ticks.erase(racer_id)
		return false
	var speed_kmh := float(finish_sim.get_player_speed_kmh(racer_id))
	if speed_kmh > DNF_SPEED_THRESHOLD_KMH:
		race_dnf_low_speed_ticks.erase(racer_id)
		return false
	var ticks := int(race_dnf_low_speed_ticks.get(racer_id, 0)) + 1
	race_dnf_low_speed_ticks[racer_id] = ticks
	if ticks < DNF_LOW_SPEED_TICKS:
		return false
	_mark_racer_dnf(racer_id, "low_speed")
	race_dnf_low_speed_ticks.erase(racer_id)
	return true

func _human_finish_count(human_racer_ids: Array) -> int:
	var count := 0
	for id_value in human_racer_ids:
		if network_manager.race_results.player_finish_times.has(int(id_value)):
			count += 1
	return count

func _update_force_end_dnf(human_racer_ids: Array) -> void:
	if singleplayer_mode:
		return
	var human_count := human_racer_ids.size()
	if human_count <= 0:
		return
	var finished_count := _human_finish_count(human_racer_ids)
	if network_manager.race_results.race_force_end_deadline_tick < 0 and finished_count * 4 >= human_count:
		var deadline_tick := network_manager.input_transport.get_race_tick() + FORCE_END_WINDOW_TICKS
		if network_manager.is_server and !singleplayer_mode:
			network_manager.race_results.send_race_force_end_deadline(deadline_tick)
		else:
			network_manager.race_results.race_force_end_deadline_tick = deadline_tick
	if network_manager.race_results.race_force_end_deadline_tick < 0:
		return
	if network_manager.input_transport.get_race_tick() < network_manager.race_results.race_force_end_deadline_tick:
		return
	for id_value in human_racer_ids:
		var id := int(id_value)
		if network_manager.race_results.player_finish_times.has(id):
			continue
		if network_manager.race_results.player_dnfs.has(id):
			continue
		if network_manager._disconnected_during_race.has(id):
			continue
		if network_manager.race_results.player_eliminations.has(id):
			continue
		_mark_racer_dnf(id, "force_end")

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
	var finish_sim := server_game_sim if network_manager.is_server and !singleplayer_mode and server_game_sim != null else game_sim
	var configuration := network_manager.race_configuration
	var infinite_practice := configuration.is_practice() and configuration.lap_count == 0
	var race_control_started := _race_control_has_started(finish_sim)
	_update_force_end_dnf(finish_watch_ids)
	if finish_sim != null:
		for id_value in finish_sim.get_finished_player_ids():
			var racer_id := int(id_value)
			if network_manager._disconnected_during_race.has(racer_id):
				continue
			if network_manager.race_results.player_finish_times.has(racer_id) or network_manager.race_results.player_eliminations.has(racer_id):
				continue
			if network_manager.is_server and !singleplayer_mode:
				network_manager.race_results.send_player_finished(racer_id, network_manager.input_transport.server_tick)
			else:
				network_manager.race_results.record_player_finished(racer_id, network_manager.input_transport.clients_server_tick)
		if !network_manager.is_vehicle_restore_enabled():
			for id_value in finish_sim.get_eliminated_player_ids():
				var racer_id := int(id_value)
				if network_manager._disconnected_during_race.has(racer_id):
					continue
				if network_manager.race_results.player_finish_times.has(racer_id) or network_manager.race_results.player_eliminations.has(racer_id):
					continue
				if network_manager.is_server and !singleplayer_mode:
					network_manager.race_results.send_player_eliminated(racer_id, network_manager.input_transport.server_tick)
				elif singleplayer_mode:
					network_manager.race_results.record_player_eliminated(racer_id, network_manager.input_transport.clients_server_tick)
	var all_done := true
	for id_value in finish_watch_ids:
		var racer_id := int(id_value)
		if network_manager._disconnected_during_race.has(racer_id):
			continue
		if network_manager.race_results.player_finish_times.has(racer_id) or network_manager.race_results.player_eliminations.has(racer_id) or network_manager.race_results.player_dnfs.has(racer_id):
			continue
		if _update_low_speed_dnf(racer_id, finish_sim, race_control_started):
			continue
		if !network_manager.race_results.player_dnfs.has(racer_id):
			all_done = false
	if replay_controller.replay_playback_active:
		return
	if network_manager.is_server:
		if all_done:
			if network_manager.race_results.net_race_finish_time == -1:
				network_manager.race_results.net_race_finish_time = Time.get_ticks_msec()
				network_manager.race_results.send_race_finish_time(network_manager.race_results.net_race_finish_time)
				_record_grand_prix_race_results(finish_sim)
				race_presentation_controller.show_results()
			if Time.get_ticks_msec() > network_manager.race_results.net_race_finish_time + RacePresentationControllerClass.RESULTS_SCREEN_MSEC:
				_finish_or_advance_grand_prix(finish_sim)
				race_presentation_controller.hide_results()
	else:
		if singleplayer_mode and all_done and !infinite_practice:
			if network_manager.race_results.net_race_finish_time == -1:
				network_manager.race_results.net_race_finish_time = Time.get_ticks_msec()
				race_presentation_controller.show_results()
				if configuration.is_time_attack():
					_finalize_time_attack()
				elif configuration.is_practice():
					_finalize_practice()

func _finalize_time_attack() -> void:
	if time_attack_finalized or !network_manager.race_configuration.is_time_attack():
		return
	time_attack_finalized = true
	var local_id := _local_player_id()
	var finish_tick := int(network_manager.race_results.player_finish_times.get(local_id, -1))
	var start_tick := race_presentation_controller.race_results_start_tick()
	race_presentation_controller.update_time_attack_submission_status("Preparing verification replay…")
	var replay_path := replay_controller.stage_completed_time_attack_replay(true)
	time_attack_last_replay_path = replay_path
	time_attack_eligibility = LeaderboardEligibilityClass.finalize(
		time_attack_eligibility,
		finish_tick,
		start_tick,
		replay_path)
	var board: Dictionary = time_attack_eligibility.get("board", {})
	time_attack_eligibility["board_name"] = String(board.get("steam_name", ""))
	time_attack_eligibility["friendly_reason"] = TimeAttackRulesClass.friendly_reason(String(time_attack_eligibility.get("reason", "")))
	time_attack_eligibility["replay_can_save"] = replay_controller.can_save_staged_replay_locally(replay_path)
	race_presentation_controller.show_time_attack_result(
		time_attack_eligibility,
		time_attack_previous_best_milliseconds,
		"Preparing trusted submission…")
	if bool(time_attack_eligibility.get("eligible", false)):
		if leaderboard_client.enqueue_submission(time_attack_eligibility):
			race_presentation_controller.show_notification("Time Attack queued for trusted verification", 5000)
			race_presentation_controller.update_time_attack_submission_status(
				"%s The queue is persisted; it is safe to close the game." % String(leaderboard_client.status().get("message", "Queued for verification.")))
		else:
			race_presentation_controller.show_notification("Time Attack replay could not be queued", 5000)
			race_presentation_controller.update_time_attack_submission_status("The verification replay could not be added to the submission queue.")
	else:
		var reason := TimeAttackRulesClass.friendly_reason(String(time_attack_eligibility.get("reason", "ineligible")))
		race_presentation_controller.show_notification("Unranked: %s" % reason, 5000)
		race_presentation_controller.update_time_attack_submission_status("Unranked — %s" % reason)


func _finalize_practice() -> void:
	if !practice_controller.session_active or practice_controller.session_completed:
		return
	practice_controller.mark_completed()
	var local_id := _local_player_id()
	var finish_tick := int(network_manager.race_results.player_finish_times.get(local_id, -1))
	var start_tick := race_presentation_controller.race_results_start_tick()
	var practice_replay_path := replay_controller.stage_completed_time_attack_replay(false)
	time_attack_last_replay_path = practice_replay_path
	var practice_score := TimeAttackRulesClass.finish_ticks_to_milliseconds(finish_tick, start_tick)
	var practice_board := {}
	if network_manager.race_track_evidence.count() == 1:
		practice_board = TimeAttackRulesClass.board_for_track_digest(
			network_manager.race_track_evidence.get_gameplay_digest(0))
	var practice_result := {
		"eligible": false,
		"reason": "practice_unranked",
		"friendly_reason": "Practice Unranked",
		"score_milliseconds": practice_score,
		"board_name": String(practice_board.get("steam_name", "")),
		"replay_path": practice_replay_path,
		"replay_can_save": replay_controller.can_save_staged_replay_locally(practice_replay_path),
	}
	race_presentation_controller.show_time_attack_result(
		practice_result, 0,
		"Practice result only — use Save Replay to keep it locally.")


func _on_leaderboard_status_changed(status: Dictionary) -> void:
	if race_presentation_controller != null:
		var message := String(status.get("message", ""))
		if int(status.get("pending_count", 0)) > 0:
			message += " The queue is persisted; it is safe to close the game."
		race_presentation_controller.update_time_attack_submission_status(message)


func _on_leaderboard_submission_completed(result: Dictionary) -> void:
	if String(result.get("replay_path", "")) != time_attack_last_replay_path:
		return
	var message := "Verified replay archived as your vehicle best." if bool(result.get("is_vehicle_best", false)) else "Verified replay archived; your existing best is faster."
	var rank := int(result.get("global_rank", 0))
	if rank > 0:
		message += " Global rank #%d." % rank
	race_presentation_controller.update_time_attack_submission_status(message)
	var board_name := String(result.get("board_name", ""))
	if !board_name.is_empty():
		time_attack_rank_refresh_board = board_name
		time_attack_rank_refresh_global = ""
		leaderboard_client.request_entries(board_name, "around_user")


func _on_time_attack_rank_entries_received(board_name: String, request_type: String, response: MxtLeaderboardQueryResult) -> void:
	if board_name != time_attack_rank_refresh_board or !response.is_ok():
		return
	var local_steam_id := steam_service.get_steam_id()
	if request_type == "around_user":
		for index in response.get_entry_count():
			var entry := response.get_entry(index)
			if entry != null and entry.steam_id == local_steam_id:
				time_attack_rank_refresh_global = "Global rank #%d" % entry.rank
				break
	var parts: Array[String] = []
	if !time_attack_rank_refresh_global.is_empty():
		parts.append(time_attack_rank_refresh_global)
	if !parts.is_empty():
		race_presentation_controller.update_time_attack_submission_status(
			"Verified replay archived. %s." % ", ".join(parts))


func _on_time_attack_race_again_requested() -> void:
	var practice := network_manager.race_configuration.is_practice()
	var configuration := network_manager.race_configuration.copy() if practice else TimeAttackRulesClass.build_configuration()
	var race_state := network_manager.race_state.duplicate(true) if practice else {}
	var track_evidence := network_manager.race_track_evidence.copy() if practice else null
	_return_to_menu()
	call_deferred("_start_singleplayer_race", false, configuration, race_state, track_evidence)


func _on_time_attack_save_replay_requested() -> void:
	var saved_path := replay_controller.save_staged_replay_locally(time_attack_last_replay_path)
	if saved_path.is_empty():
		race_presentation_controller.show_notification("Replay could not be saved", 3000)
		return
	race_presentation_controller.results_overlay.set_time_attack_replay_saved(saved_path)
	race_presentation_controller.show_notification("Replay Saved", 2200)


func _on_time_attack_watch_replay_requested() -> void:
	if !time_attack_last_replay_path.is_empty():
		replay_controller.call_deferred("play_replay_file", time_attack_last_replay_path)


func _on_leaderboard_replay_watch_requested(board_name: String, entry: MxtLeaderboardEntry) -> void:
	if leaderboard_replay_cache == null:
		return
	if leaderboard_watch_request_token != 0:
		leaderboard_replay_cache.cancel_request(leaderboard_watch_request_token)
	leaderboard_watch_request_token = leaderboard_replay_cache.request_replay(board_name, entry)
	if race_presentation_controller != null:
		race_presentation_controller.show_notification("Preparing leaderboard replay…", 5000)


func _on_leaderboard_replay_cache_request_completed(token: int, result: Dictionary) -> void:
	if token != leaderboard_watch_request_token:
		return
	leaderboard_watch_request_token = 0
	if !bool(result.get("success", false)):
		if race_presentation_controller != null:
			race_presentation_controller.show_notification(String(result.get("message", "Leaderboard replay unavailable.")), 5000)
		return
	if race_presentation_controller != null:
		race_presentation_controller.show_notification(String(result.get("message", "Leaderboard replay ready.")), 3000)
	var cache_path := String(result.get("cache_path", ""))
	if !cache_path.is_empty():
		replay_controller.call_deferred("play_replay_file", cache_path)


func _on_time_attack_results_leaderboard_requested(board_name: String) -> void:
	_return_to_menu()
	car_settings.open_leaderboards(board_name)


func _on_time_attack_main_menu_requested() -> void:
	_return_to_menu()

func _load_gameplay_camera_settings() -> void:
	if FileAccess.file_exists(CAMERA_SETTINGS_PATH):
		var value = JSON.parse_string(FileAccess.get_file_as_string(CAMERA_SETTINGS_PATH))
		if typeof(value) == TYPE_DICTIONARY:
			gameplay_camera_zoom_mode = clampi(int((value as Dictionary).get("zoom_mode", 1)), 0, 3)
	game_sim.set_gameplay_camera_zoom_mode(gameplay_camera_zoom_mode)

func _sync_gameplay_camera_settings() -> void:
	var current_zoom_mode := clampi(game_sim.get_gameplay_camera_zoom_mode(), 0, 3)
	if current_zoom_mode == gameplay_camera_zoom_mode:
		return
	gameplay_camera_zoom_mode = current_zoom_mode
	var file := FileAccess.open(CAMERA_SETTINGS_PATH, FileAccess.WRITE)
	if file == null:
		push_warning("Could not save gameplay camera settings: %s" % error_string(FileAccess.get_open_error()))
		return
	file.store_string(JSON.stringify({"zoom_mode": gameplay_camera_zoom_mode}))
	file.close()

func _update_native_render_camera() -> void:
	if game_sim == null or !game_sim.has_method("set_render_camera"):
		return
	game_sim.set_render_camera(get_viewport().get_camera_3d())

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
	var now_ticks_usec := Time.get_ticks_usec()
	var unscaled_delta := delta if last_process_ticks_usec == 0 else clampf(
		float(now_ticks_usec - last_process_ticks_usec) / 1000000.0, 0.0, 0.1)
	last_process_ticks_usec = now_ticks_usec
	_update_playtest_lobby_probe()
	race_pause_controller.update_navigation(unscaled_delta)
	practice_controller.update(unscaled_delta)
	practice_controller.consume_frame_rewind()
	if practice_controller.consume_frame_advance() and game_sim.sim_started and singleplayer_mode:
		_run_active_race_physics_frame(1.0 / 60.0)
	var profile_enabled: bool = debug_runtime_controller.render_profile_enabled and game_sim.sim_started
	var profile_process_start := debug_runtime_controller.begin_process_frame(_singleplayer_tick) if profile_enabled else 0
	race_audio_controller.update(delta)
	communication_controller.update_race_overlay()
	_update_start_sync_drop_panel()
	race_presentation_controller.update()
	debug_runtime_controller.update_labels(lobby_control.visible)
	if game_sim.sim_started and network_manager.race_results.net_race_finish_time != -1 and !replay_controller.replay_playback_active:
		replay_controller.refresh_pause_button()
	if game_sim.sim_started:
		spectator_controller.update_finished_input()
		var profile_visuals_start := Time.get_ticks_usec() if profile_enabled else 0
		replay_controller.update(delta)
		_update_native_render_camera()
		game_sim.render_gamesim_visuals_only(delta)
		if profile_enabled:
			debug_runtime_controller.record_process_frame(profile_process_start, profile_visuals_start)
