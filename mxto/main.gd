class_name GameManager extends Node

@onready var game_sim: GameSim = $GameSim
@onready var server_game_sim: GameSim = $ServerGameSim
@onready var replay_controller: ReplayController = $ReplayController
@onready var race_audio_controller: RaceAudioController = $RaceAudioController
@onready var track_content_controller: TrackContentController = $TrackContentController
@onready var playtest_lobby_probe = $PlaytestLobbyProbe
@onready var connect_host_box: HBoxContainer = $Control/ConnectHostBox
@onready var start_button: Button = $Control/ConnectHostBox/StartButton
@onready var join_button: Button = $Control/ConnectHostBox/JoinButton
@onready var join_playtest_button: Button = $Control/JoinPlaytestButton
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
@onready var debug_track_mesh_container: Node3D = $GameWorld/DebugTrackMeshContainer
@onready var debug_track_mesh: MeshInstance3D = $GameWorld/DebugTrackMeshContainer/DebugTrackMesh
@onready var network_manager: NetworkManager = $NetworkManager
@onready var car_settings: Control = $CarSettings
@onready var options_menu: Control = $OptionsMenu
@onready var car_settings_button: Button = $Control/CarSettingsButton
@onready var singleplayer_button: Button = $Control/SingleplayerButton
@onready var spectator_race_button: Button = $Control/SpectatorRaceButton
@onready var controller_settings_button: Button = $Control/ControllerSettingsButton
@onready var track_editor_button: Button = $Control/TrackEditorButton
@onready var car_settings_button_lobby: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CarSettingsButton
@onready var controller_settings_button_lobby: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/ControllerSettingsButton
@onready var race_finish_label: Label = $RaceFinishLabel
@onready var frame_time_label: Label = $FrameTimeLabel
@onready var rtt_label: Label = $RTTLabel
@onready var version_label: Label = $VersionLabel
@onready var cpu_slider: HSlider = $Control/CpuSlider
@onready var cpu_slider_label: Label = $Control/CpuSliderLabel
@onready var lobby_cpu_count_label: Label = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CpuControlBox/CpuCountLabel
@onready var add_cpu_button: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CpuControlBox/AddCpuButton
@onready var remove_cpu_button: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CpuControlBox/RemoveCpuButton
@onready var lobby_game_mode_choice: OptionButton = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/GameModeChoice
@onready var lobby_vehicle_restore_toggle: CheckBox = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/VehicleRestoreToggle
@onready var lobby_bumpers_toggle: CheckBox = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/BumpersToggle
@onready var lobby_s_boost_toggle: CheckBox = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/SBoostToggle
@onready var lobby_stage_button_container: VBoxContainer = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/StageBox/StageScroll/StageButtonContainer
@onready var lobby_stage_preview_container: VBoxContainer = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/StageBox/PreviewScroll/StagePreviewContainer
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
@onready var race_pause_options_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/OptionsButton
@onready var race_pause_lobby_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/LobbyButton
@onready var race_pause_disconnect_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/DisconnectButton
var lobby_chibi_cars := {}
var lobby_chibi_pending_states := {}
var lobby_chibi_last_broadcast_msec := 0
var lobby_grand_prix_track_sequence: Array[int] = []
var lobby_applying_race_options := false
var lobby_player_list_signature := ""

@onready var obj_viewport: SubViewport = get_node_or_null("GameWorld/ObjViewport") as SubViewport
@onready var outline_viewport: SubViewport = get_node_or_null("GameWorld/OutlineViewport") as SubViewport
@onready var obj_viewport_texture: ColorRect = get_node_or_null("GameWorld/ObjViewportTexture") as ColorRect
@onready var outline_viewport_texture: ColorRect = get_node_or_null("GameWorld/OutlineViewportTexture") as ColorRect

const PlayerInputClass = preload("res://player/player_input.gd")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CustomStampAtlasBuilder = preload("res://vehicle/customization/custom_stamp_atlas_builder.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")
const LobbyChibiCarClass = preload("res://ui/lobby_chibi_car.gd")
const FinishMedalScene: PackedScene = preload("res://ui/finish_medal.tscn")
const KoMedalScene: PackedScene = preload("res://ui/ko_medal.tscn")
const RaceResultsOverlayScene: PackedScene = preload("res://ui/race_results_overlay.tscn")
const RaceCommunicationOverlayScene: PackedScene = preload("res://ui/race_communication_overlay.tscn")
const BUMPER_DEFINITION_PATH := "res://vehicle/asset/bumper/definition.tres"
const BUMPER_POOL_SIZE := 60
const RACE_RESULTS_SCREEN_MSEC := 15000

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
var auto_render_all_car_bodies_mode: bool = false
var auto_disable_node_effects_mode: bool = false
var auto_disable_thruster_lights_mode: bool = false
var auto_hide_track_visuals_mode: bool = false
var auto_disable_hud_mode: bool = false
var auto_hide_hud_only_mode: bool = false
var auto_disable_hud_process_only_mode: bool = false
var auto_disable_minimap_mode: bool = false
var auto_quit_after_frames: int = -1
var car_render_manager: CarRenderManager
var lobby_chibi_render_manager: CarRenderManager
var lobby_chibi_render_signature := ""
const LOBBY_CHIBI_BROADCAST_INTERVAL_MSEC := 100
const MAX_LOBBY_CHAT_HISTORY := 128
const CHAT_MAX_MESSAGE_CHARACTERS := 220
const CHAT_RATE_WINDOW_MSEC := 5000
const CHAT_RATE_MAX_MESSAGES := 8
const CHAT_RATE_STATE_PRUNE_THRESHOLD := 128
const CHAT_GLOBAL_BURST_MESSAGES := 48.0
const CHAT_GLOBAL_REFILL_PER_SECOND := 8.0
var lobby_chat_history: Array[Dictionary] = []
var text_chat_rate_state := {}
var text_chat_global_tokens := CHAT_GLOBAL_BURST_MESSAGES
var text_chat_global_refill_msec := 0
var lobby_chat_rendered_history_size := 0
var singleplayer_options_root: Control
var singleplayer_options_restore_toggle: CheckBox
var singleplayer_options_bumpers_toggle: CheckBox
var singleplayer_options_s_boost_toggle: CheckBox
var singleplayer_options_as_spectator := false
var _last_race_track_index: int = -1
var _last_race_settings: Array = []
var race_dnf_low_speed_ticks := {}
var start_sync_drop_root: PanelContainer
var start_sync_drop_label: Label
var start_sync_drop_button: Button
var live_spectate_focus_id := -1
var live_spectate_strafe_dir := 0

const DNF_SPEED_THRESHOLD_KMH := 400.0
const DNF_LOW_SPEED_TICKS := 60 * 10
const FORCE_END_WINDOW_TICKS := 60 * 60
const LIVE_SPECTATE_STRAFE_THRESHOLD := 0.65
const DIP_TRACE_RAIL_SAMPLING := 0x40
const DIP_TRACE_PIPE_FLOOR := 0x100
const DIP_TRACE_MESH_FLOOR := 0x1000

var race_pause_open := false
var debug_rail_trace_requested := false
var debug_rail_trace_car_index := -1
var debug_rail_trace_tick_start := -1
var debug_rail_trace_tick_end := -1
var active_stickers := {}
var race_notification_hide_msec := 0
var race_medals: Array[Control] = []
var race_results_overlay: RaceResultsOverlay
var race_communication_overlay: RaceCommunicationOverlay
var race_results_next_accel_setting: float = -1.0
var race_results_hid_race_hud := false
var race_results_saved_race_hud_visible := false
var render_profile_frames := 0
var render_profile_process_frames := 0
var render_profile_physics_us := 0
var render_profile_tick_us := 0
var render_profile_render_us := 0
var render_profile_nametag_us := 0
var render_profile_local_visual_us := 0
var render_profile_input_us := 0
var render_profile_events_us := 0
var render_profile_camera_us := 0
var render_profile_audio_tick_us := 0
var render_profile_finish_check_us := 0
var render_profile_process_us := 0
var render_profile_visuals_only_us := 0
var render_profile_physics_max_us := 0
var render_profile_tick_max_us := 0
var render_profile_render_max_us := 0
var render_profile_nametag_max_us := 0
var render_profile_local_visual_max_us := 0
var render_profile_input_max_us := 0
var render_profile_events_max_us := 0
var render_profile_camera_max_us := 0
var render_profile_audio_tick_max_us := 0
var render_profile_finish_check_max_us := 0
var render_profile_process_max_us := 0
var render_profile_visuals_only_max_us := 0
var render_profile_last_process_start_us := 0
var render_profile_frame_gap_max_us := 0
var render_profile_frame_gap_over_16ms := 0
var render_profile_frame_gap_over_20ms := 0
var render_profile_frame_gap_over_25ms := 0
var render_profile_frame_gap_over_33ms := 0
var render_profile_frame_gap_over_50ms := 0
var render_profile_last_physics_sample_us := 0
var render_profile_last_process_sample_us := 0
var render_profile_last_pipeline_draw := -1
var render_profile_last_pipeline_surface := -1
var render_profile_last_pipeline_mesh := -1
var render_profile_top_gap_us := PackedInt64Array()
var render_profile_top_gap_tick := PackedInt64Array()
var render_profile_top_gap_physics_us := PackedInt64Array()
var render_profile_top_gap_process_us := PackedInt64Array()
var render_profile_top_gap_pipeline_draw := PackedInt64Array()
var render_profile_top_gap_pipeline_surface := PackedInt64Array()
var render_profile_top_gap_pipeline_mesh := PackedInt64Array()
var render_profile_top_gap_native_total_us := PackedInt64Array()
var render_profile_top_gap_native_vehicle_us := PackedInt64Array()
var render_profile_top_gap_native_collision_us := PackedInt64Array()
var render_profile_top_gap_native_save_us := PackedInt64Array()
var render_profile_top_gap_body_instances := PackedInt64Array()
var render_profile_top_gap_draw_calls := PackedInt64Array()
var render_profile_top_gap_primitives := PackedInt64Array()
var render_profile_top_gap_engine_process_us := PackedInt64Array()
var render_profile_top_gap_engine_physics_us := PackedInt64Array()
const RENDER_PROFILE_TOP_GAP_COUNT := 24
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
const CLEAN_SCREENSHOT_SIZE := Vector2i(3840, 2160)
const CLEAN_SCREENSHOT_DIRECTORY := "user://screenshots"

var clean_screenshot_in_progress := false

func _read_int_arg(args: Array, user_args: Array, flag: String, default_value: int) -> int:
	var idx := args.find(flag)
	var source_args := args
	if idx == -1:
		idx = user_args.find(flag)
		source_args = user_args
	if idx == -1 or idx + 1 >= source_args.size():
		return default_value
	return int(source_args[idx + 1])

func _ready() -> void:
	#obj_viewport_texture.texture = obj_viewport.get_texture()
	#outline_viewport_texture.texture = outline_viewport.get_texture()
	car_render_manager = CarRenderManagerClass.new()
	car_render_manager.name = "CarRenderManager"
	$GameWorld.add_child(car_render_manager)
	race_audio_controller.initialize()
	race_results_overlay = RaceResultsOverlayScene.instantiate() as RaceResultsOverlay
	add_child(race_results_overlay)
	race_results_overlay.machine_setting_changed.connect(_on_race_results_machine_setting_changed)
	race_communication_overlay = RaceCommunicationOverlayScene.instantiate() as RaceCommunicationOverlay
	add_child(race_communication_overlay)
	race_communication_overlay.message_submitted.connect(_submit_text_chat_message)
	lobby_chibi_render_manager = CarRenderManagerClass.new()
	lobby_chibi_render_manager.name = "LobbyChibiRenderManager"
	if lobby_chibi_root != null:
		lobby_chibi_root.add_child(lobby_chibi_render_manager)
	randomize()
	_build_lobby_options_controls()
	_build_multiplayer_connect_box()
	_build_singleplayer_race_options_screen()
	replay_controller.initialize()
	_build_start_sync_drop_panel()
	_load_tracks()
	_load_car_definitions()
	if car_settings != null and car_settings.has_method("refresh_after_game_manager_loaded"):
		car_settings.call("refresh_after_game_manager_loaded")
	network_manager.race_started.connect(_on_network_race_started)
	network_manager.race_finished.connect(_on_network_race_finished)
	network_manager.race_event.connect(_on_race_event)
	network_manager.race_options_changed.connect(_on_network_race_options_changed)
	car_settings.hide()
	options_menu.hide()
	if !car_settings_button.pressed.is_connected(_on_car_settings_button_pressed):
		car_settings_button.pressed.connect(_on_car_settings_button_pressed)
	if !car_settings_button_lobby.pressed.is_connected(_on_car_settings_button_pressed):
		car_settings_button_lobby.pressed.connect(_on_car_settings_button_pressed)
	if !controller_settings_button.pressed.is_connected(_on_controller_settings_button_pressed):
		controller_settings_button.pressed.connect(_on_controller_settings_button_pressed)
	if !controller_settings_button_lobby.pressed.is_connected(_on_controller_settings_button_pressed):
		controller_settings_button_lobby.pressed.connect(_on_controller_settings_button_pressed)
	if !options_menu.visibility_changed.is_connected(_on_controller_settings_visibility_changed):
		options_menu.visibility_changed.connect(_on_controller_settings_visibility_changed)
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
	if !join_playtest_button.pressed.is_connected(_on_join_playtest_button_pressed):
		join_playtest_button.pressed.connect(_on_join_playtest_button_pressed)
	if !playtest_lobby_probe.availability_changed.is_connected(_on_playtest_lobby_availability_changed):
		playtest_lobby_probe.availability_changed.connect(_on_playtest_lobby_availability_changed)
	_build_race_pause_menu()
	singleplayer_cpu_count = int(cpu_slider.value)
	_update_cpu_slider_label()
	headless_mode = DisplayServer.get_name() == "headless"
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	launch_cpu_driver_count = _parse_cpu_driver_count_arg(args)
	if launch_cpu_driver_count < 0:
		launch_cpu_driver_count = _parse_cpu_driver_count_arg(user_args)

	if launch_cpu_driver_count >= 0:
		singleplayer_cpu_count = launch_cpu_driver_count
		if cpu_slider != null:
			cpu_slider.set_value_no_signal(mini(singleplayer_cpu_count, int(cpu_slider.max_value)))
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
	auto_render_all_car_bodies_mode = args.has("--render-all-car-bodies") or user_args.has("--render-all-car-bodies")
	auto_disable_node_effects_mode = args.has("--profile-disable-node-effects") or user_args.has("--profile-disable-node-effects")
	auto_disable_thruster_lights_mode = args.has("--profile-disable-thruster-lights") or user_args.has("--profile-disable-thruster-lights")
	auto_hide_track_visuals_mode = args.has("--profile-hide-track-visuals") or user_args.has("--profile-hide-track-visuals")
	auto_disable_hud_mode = args.has("--profile-disable-hud") or user_args.has("--profile-disable-hud")
	auto_hide_hud_only_mode = args.has("--profile-hide-hud-only") or user_args.has("--profile-hide-hud-only")
	auto_disable_hud_process_only_mode = args.has("--profile-disable-hud-process-only") or user_args.has("--profile-disable-hud-process-only")
	auto_disable_minimap_mode = args.has("--profile-disable-minimap") or user_args.has("--profile-disable-minimap")
	game_sim.set_render_profile_enabled(auto_render_profile_mode)
	game_sim.set_phase_profile_enabled(auto_render_profile_mode)
	if auto_render_profile_mode:
		render_profile_top_gap_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_tick.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_physics_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_process_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_pipeline_draw.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_pipeline_surface.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_pipeline_mesh.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_native_total_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_native_vehicle_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_native_collision_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_native_save_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_body_instances.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_draw_calls.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_primitives.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_engine_process_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_engine_physics_us.resize(RENDER_PROFILE_TOP_GAP_COUNT)
		render_profile_top_gap_us.fill(0)
		render_profile_top_gap_tick.fill(0)
		render_profile_top_gap_physics_us.fill(0)
		render_profile_top_gap_process_us.fill(0)
		render_profile_top_gap_pipeline_draw.fill(0)
		render_profile_top_gap_pipeline_surface.fill(0)
		render_profile_top_gap_pipeline_mesh.fill(0)
		render_profile_top_gap_native_total_us.fill(0)
		render_profile_top_gap_native_vehicle_us.fill(0)
		render_profile_top_gap_native_collision_us.fill(0)
		render_profile_top_gap_native_save_us.fill(0)
		render_profile_top_gap_body_instances.fill(0)
		render_profile_top_gap_draw_calls.fill(0)
		render_profile_top_gap_primitives.fill(0)
		render_profile_top_gap_engine_process_us.fill(0)
		render_profile_top_gap_engine_physics_us.fill(0)
	game_sim.set_render_node_effects_enabled(!auto_disable_node_effects_mode)
	game_sim.set_render_thruster_lights_enabled(!auto_disable_thruster_lights_mode)
	game_sim.set_render_all_car_bodies(auto_render_all_car_bodies_mode)
	auto_track_editor_mode = args.has("--track-editor") or user_args.has("--track-editor") or args.has("--mxt-track-editor") or user_args.has("--mxt-track-editor")
	var quit_idx := args.find("--quit-after-frames")
	var quit_args := args
	if quit_idx == -1:
		quit_idx = user_args.find("--quit-after-frames")
		quit_args = user_args
	if quit_idx != -1 and quit_idx + 1 < quit_args.size():
		auto_quit_after_frames = max(0, int(quit_args[quit_idx + 1]))
	debug_rail_trace_requested = args.has("--debug-rail-trace") or user_args.has("--debug-rail-trace")
	if debug_rail_trace_requested:
		debug_rail_trace_car_index = _read_int_arg(args, user_args, "--debug-rail-trace-car-index", -1)
		debug_rail_trace_tick_start = _read_int_arg(args, user_args, "--debug-rail-trace-from", -1)
		debug_rail_trace_tick_end = _read_int_arg(args, user_args, "--debug-rail-trace-to", -1)
		game_sim.set_dip_switch_enabled(DIP_TRACE_RAIL_SAMPLING, true)
		server_game_sim.set_dip_switch_enabled(DIP_TRACE_RAIL_SAMPLING, true)
		game_sim.set_rail_trace_filter(debug_rail_trace_car_index, debug_rail_trace_tick_start, debug_rail_trace_tick_end)
		server_game_sim.set_rail_trace_filter(debug_rail_trace_car_index, debug_rail_trace_tick_start, debug_rail_trace_tick_end)
	if args.has("--debug-mesh-floor-trace") or user_args.has("--debug-mesh-floor-trace"):
		game_sim.set_dip_switch_enabled(DIP_TRACE_MESH_FLOOR, true)
		server_game_sim.set_dip_switch_enabled(DIP_TRACE_MESH_FLOOR, true)
	var replay_launch_requested := replay_controller.configure_command_line(args, user_args)
	if !replay_launch_requested and auto_track_editor_mode:
		call_deferred("_on_track_editor_button_pressed")
	elif !replay_launch_requested and auto_singleplayer_mode:
		call_deferred("_on_singleplayer_button_pressed")
	if headless_mode and !auto_host_mode and !auto_track_editor_mode and !auto_singleplayer_mode and !replay_launch_requested:
		var def_path := ""
		if car_definitions.size() > 0:
			def_path = car_definitions[0].resource_path
		var settings_dict = {
			"username": "Headless",
			"car_definition_path": def_path,
			"accel_setting": 1.0,
		}
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
	lobby_track_selector.clear()
	lobby_grand_prix_track_sequence.clear()
	for t in track_content_controller.tracks:
		track_selector.add_item(t["name"])
		lobby_track_selector.add_item(t["name"])
	if !track_content_controller.tracks.is_empty():
		track_selector.selected = 0
		lobby_track_selector.selected = 0
		lobby_grand_prix_track_sequence.append(0)
	var command_line_track_index := track_content_controller.command_line_track_index()
	if command_line_track_index >= 0:
		track_selector.selected = command_line_track_index
		lobby_track_selector.selected = command_line_track_index
		if !lobby_grand_prix_track_sequence.is_empty():
			lobby_grand_prix_track_sequence[0] = command_line_track_index
	_populate_lobby_stage_buttons()
	_refresh_lobby_race_options()

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
	_reset_text_chat_history()
	if launch_cpu_driver_count >= 0:
		network_manager.set_cpu_driver_count(launch_cpu_driver_count)
	network_manager.send_player_settings(car_settings.get_player_settings().to_dict())
	network_manager.custom_stamp_network.send_active_custom_stamp_manifest()
	start_race_button.disabled = false
	$Control.visible = false
	lobby_control.visible = true

func _on_singleplayer_button_pressed() -> void:
	if headless_mode or auto_singleplayer_mode:
		_start_singleplayer_race(false, _build_default_singleplayer_race_options())
	else:
		_open_singleplayer_race_options(false)

func _on_spectator_race_button_pressed() -> void:
	_open_singleplayer_race_options(true)

func _on_track_editor_button_pressed() -> void:
	get_tree().change_scene_to_file("res://track_editing_scene.tscn")

func _start_singleplayer_race(as_spectator: bool, race_options: Dictionary = {}) -> void:
	# Start a local, singleplayer race that does not touch networking at all.
	# Prepare a minimal settings array using the current local player settings.
	singleplayer_mode = true
	_singleplayer_tick = 0
	network_manager.reset_race_state()
	var options := race_options.duplicate(true) if !race_options.is_empty() else _build_default_singleplayer_race_options()
	options["track_ids"] = [track_content_controller.track_id_for_index(track_selector.selected)]
	if auto_bumpers_mode:
		options["bumpers"] = true
	network_manager.race_options = options
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
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false

func _on_join_button_pressed() -> void:
	_join_multiplayer_lobby(ip_field.text, _multiplayer_lobby_port())

func _join_multiplayer_lobby(address: String, port: int) -> void:
	var settings_dict = car_settings.get_player_settings().to_dict()
	var err := network_manager.join(address, port)
	if err != OK:
		return
	_reset_text_chat_history()
	network_manager.multiplayer.connected_to_server.connect(
		_send_connected_player_settings.bind(settings_dict),
		Object.CONNECT_ONE_SHOT)
	start_race_button.disabled = true
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
	network_manager.send_player_settings(settings_dict)
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

func _build_default_singleplayer_race_options() -> Dictionary:
	return {
		"game_mode": 0,
		"track_ids": [track_content_controller.track_id_for_index(track_selector.selected)],
		"vehicle_restore": bool(network_manager.race_options.get("vehicle_restore", true)),
		"bumpers": bool(network_manager.race_options.get("bumpers", false)) or auto_bumpers_mode,
		"s_boost": bool(network_manager.race_options.get("s_boost", true)),
		"grand_prix_current_track": 0,
		"grand_prix_points": {},
		"grand_prix_ko_energy_bonuses": {},
		"grand_prix_eliminated_ids": [],
	}

func _open_singleplayer_race_options(as_spectator: bool) -> void:
	_build_singleplayer_race_options_screen()
	singleplayer_options_as_spectator = as_spectator
	var options := _build_default_singleplayer_race_options()
	singleplayer_options_restore_toggle.set_pressed_no_signal(bool(options.get("vehicle_restore", true)))
	singleplayer_options_bumpers_toggle.set_pressed_no_signal(bool(options.get("bumpers", false)))
	singleplayer_options_s_boost_toggle.set_pressed_no_signal(bool(options.get("s_boost", true)))
	$Control.visible = false
	lobby_control.visible = false
	singleplayer_options_root.visible = true
	singleplayer_options_restore_toggle.grab_focus()

func _build_singleplayer_race_options_from_controls() -> Dictionary:
	var options := _build_default_singleplayer_race_options()
	if singleplayer_options_restore_toggle != null:
		options["vehicle_restore"] = singleplayer_options_restore_toggle.button_pressed
	if singleplayer_options_bumpers_toggle != null:
		options["bumpers"] = singleplayer_options_bumpers_toggle.button_pressed or auto_bumpers_mode
	if singleplayer_options_s_boost_toggle != null:
		options["s_boost"] = singleplayer_options_s_boost_toggle.button_pressed
	return options

func _on_singleplayer_options_back_pressed() -> void:
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false
	$Control.visible = true

func _on_singleplayer_options_start_pressed() -> void:
	_start_singleplayer_race(singleplayer_options_as_spectator, _build_singleplayer_race_options_from_controls())

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

	car_settings_button_lobby.visible = true
	controller_settings_button_lobby.visible = true
	if !lobby_game_mode_choice.item_selected.is_connected(_on_lobby_game_mode_selected):
		lobby_game_mode_choice.item_selected.connect(_on_lobby_game_mode_selected)
	if !lobby_vehicle_restore_toggle.toggled.is_connected(_on_lobby_vehicle_restore_toggled):
		lobby_vehicle_restore_toggle.toggled.connect(_on_lobby_vehicle_restore_toggled)
	if !lobby_bumpers_toggle.toggled.is_connected(_on_lobby_bumpers_toggled):
		lobby_bumpers_toggle.toggled.connect(_on_lobby_bumpers_toggled)
	if lobby_s_boost_toggle != null and !lobby_s_boost_toggle.toggled.is_connected(_on_lobby_s_boost_toggled):
		lobby_s_boost_toggle.toggled.connect(_on_lobby_s_boost_toggled)
	if !lobby_say_text.text_submitted.is_connected(_on_lobby_chat_text_submitted):
		lobby_say_text.text_submitted.connect(_on_lobby_chat_text_submitted)
	lobby_say_text.keep_editing_on_text_submit = true
	if !lobby_send_text_button.pressed.is_connected(_on_lobby_chat_send_pressed):
		lobby_send_text_button.pressed.connect(_on_lobby_chat_send_pressed)
	lobby_send_text_button.focus_mode = Control.FOCUS_NONE
	if !lobby_control.visibility_changed.is_connected(_on_lobby_visibility_changed):
		lobby_control.visibility_changed.connect(_on_lobby_visibility_changed)
	var viewport_stack := $Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack as Control
	if viewport_stack != null and !viewport_stack.gui_input.is_connected(_on_lobby_chibi_view_gui_input):
		viewport_stack.gui_input.connect(_on_lobby_chibi_view_gui_input)
	_populate_lobby_stage_buttons()
	_refresh_lobby_stage_preview()

func _build_race_pause_menu() -> void:
	if !race_pause_resume_button.pressed.is_connected(_close_race_pause_menu):
		race_pause_resume_button.pressed.connect(_close_race_pause_menu)
	if !race_pause_options_button.pressed.is_connected(_on_pause_options_pressed):
		race_pause_options_button.pressed.connect(_on_pause_options_pressed)
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
	replay_controller.refresh_pause_button()
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

func _on_pause_options_pressed() -> void:
	options_menu.call("open_settings")

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
	for i in range(track_content_controller.tracks.size()):
		var button := Button.new()
		button.text = str(track_content_controller.tracks[i].get("name", "Track"))
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(_on_lobby_stage_button_pressed.bind(i))
		lobby_stage_button_container.add_child(button)

func _on_lobby_stage_button_pressed(track_index: int) -> void:
	if !network_manager.is_server:
		return
	if track_index < 0 or track_index >= track_content_controller.tracks.size():
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

func _on_lobby_s_boost_toggled(_toggled: bool) -> void:
	_refresh_lobby_race_options()

func _build_lobby_race_options() -> Dictionary:
	var selected_track_ids := []
	for selected_index in lobby_grand_prix_track_sequence:
		var track_id := track_content_controller.track_id_for_index(int(selected_index))
		if track_id != "":
			selected_track_ids.append(track_id)
	return {
		"game_mode": lobby_game_mode_choice.selected if lobby_game_mode_choice != null else 0,
		"track_ids": selected_track_ids,
		"vehicle_restore": lobby_vehicle_restore_toggle.button_pressed if lobby_vehicle_restore_toggle != null else true,
		"bumpers": lobby_bumpers_toggle.button_pressed if lobby_bumpers_toggle != null else false,
		"s_boost": lobby_s_boost_toggle.button_pressed if lobby_s_boost_toggle != null else true,
		"grand_prix_current_track": 0,
		"grand_prix_points": {},
		"grand_prix_ko_energy_bonuses": {},
		"grand_prix_eliminated_ids": [],
	}

func _refresh_lobby_race_options() -> void:
	if lobby_applying_race_options:
		_refresh_lobby_stage_preview()
		_update_lobby_start_race_button()
		return
	var options := _build_lobby_race_options()
	if network_manager.is_server:
		network_manager.send_race_options(options)
	else:
		network_manager.race_options = options
	_refresh_lobby_stage_preview()
	_update_lobby_start_race_button()

func _update_lobby_start_race_button() -> void:
	if start_race_button == null:
		return
	var can_edit_cpu := network_manager.is_server and !network_manager.race_active
	start_race_button.disabled = !can_edit_cpu or track_content_controller.tracks.is_empty() or lobby_grand_prix_track_sequence.is_empty()

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
	if lobby_s_boost_toggle != null:
		lobby_s_boost_toggle.set_pressed_no_signal(bool(options.get("s_boost", true)))
	lobby_grand_prix_track_sequence.clear()
	var track_ids: Array = options.get("track_ids", [])
	for track_id_value in track_ids:
		var idx := track_content_controller.track_index_for_id(String(track_id_value))
		if idx >= 0 and idx < track_content_controller.tracks.size():
			lobby_grand_prix_track_sequence.append(idx)
	if track_ids.size() > 0:
		var first_index := track_content_controller.track_index_for_id(String(track_ids[0]))
		if first_index >= 0 and first_index < track_content_controller.tracks.size():
			lobby_track_selector.selected = first_index
	lobby_applying_race_options = false
	_refresh_lobby_stage_preview()

func _refresh_lobby_stage_preview() -> void:
	if lobby_stage_preview_container == null:
		return
	for child in lobby_stage_preview_container.get_children():
		child.queue_free()
	var options := network_manager.race_options
	var track_ids: Array = options.get("track_ids", [])
	for i in range(track_ids.size()):
		var track_id := String(track_ids[i])
		var track_index := track_content_controller.track_index_for_id(track_id)
		var label := Button.new()
		label.disabled = !network_manager.is_server
		if track_index >= 0 and track_index < track_content_controller.tracks.size():
			label.text = "%d. %s" % [i + 1, str(track_content_controller.tracks[track_index].get("name", "Track"))]
		else:
			label.text = "%d. Missing Track" % (i + 1)
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		label.pressed.connect(_on_lobby_stage_preview_pressed.bind(i))
		lobby_stage_preview_container.add_child(label)

func _on_lobby_visibility_changed() -> void:
	if lobby_control.visible:
		_refresh_lobby_chat_box()

func _on_lobby_chat_send_pressed() -> void:
	if lobby_say_text == null:
		return
	_submit_text_chat_message(lobby_say_text.text)
	lobby_say_text.clear()
	_refocus_lobby_chat_deferred()

func _on_lobby_chat_text_submitted(text: String) -> void:
	_submit_text_chat_message(text)
	if lobby_say_text != null:
		lobby_say_text.clear()
		_refocus_lobby_chat_deferred()

func _refocus_lobby_chat_deferred() -> void:
	call_deferred("_refocus_lobby_chat")

func _refocus_lobby_chat() -> void:
	if lobby_say_text == null or !lobby_control.visible:
		return
	lobby_say_text.grab_focus()
	lobby_say_text.caret_column = lobby_say_text.text.length()

func _submit_text_chat_message(text: String) -> void:
	var clean := _sanitize_chat_message(text)
	if clean == "":
		return
	if !network_manager.has_network_peer():
		_append_text_chat_message(_local_player_id(), clean)
	elif network_manager.is_server:
		_server_publish_chat_message(_local_player_id(), clean)
	else:
		_send_text_chat_message_to_server.rpc_id(1, clean)

@rpc("any_peer", "call_remote", "reliable", 8)
func _send_text_chat_message_to_server(text: String) -> void:
	if !network_manager.is_server:
		return
	var sender := multiplayer.get_remote_sender_id()
	var clean := _sanitize_chat_message(text)
	if clean == "":
		return
	_server_publish_chat_message(sender, clean)

@rpc("authority", "call_local", "reliable", 8)
func _broadcast_text_chat_message(sender_id: int, text: String) -> void:
	var clean := _sanitize_chat_message(text)
	if clean == "":
		return
	_append_text_chat_message(sender_id, clean)

func _server_publish_chat_message(sender_id: int, text: String) -> void:
	if !network_manager.is_server or !_server_chat_sender_is_valid(sender_id):
		return
	if !_server_chat_rate_limit_allows(sender_id):
		return
	_broadcast_text_chat_message.rpc(sender_id, text)

func _server_chat_sender_is_valid(sender_id: int) -> bool:
	if sender_id <= 0:
		return false
	if sender_id == _local_player_id():
		return true
	return (
		network_manager.player_ids.has(sender_id)
		or network_manager.spectator_ids.has(sender_id)
		or network_manager.waiting_peers.has(sender_id)
	)

func _server_chat_rate_limit_allows(sender_id: int) -> bool:
	var now_msec := Time.get_ticks_msec()
	if text_chat_rate_state.size() >= CHAT_RATE_STATE_PRUNE_THRESHOLD:
		_prune_stale_chat_rate_state(now_msec)
	var state: Dictionary = text_chat_rate_state.get(sender_id, {})
	var window_start_msec := int(state.get("window_start_msec", now_msec))
	var message_count := int(state.get("message_count", 0))
	if now_msec - window_start_msec >= CHAT_RATE_WINDOW_MSEC:
		window_start_msec = now_msec
		message_count = 0
	if message_count >= CHAT_RATE_MAX_MESSAGES:
		return false
	_refill_global_chat_tokens(now_msec)
	if text_chat_global_tokens < 1.0:
		return false
	text_chat_global_tokens -= 1.0
	text_chat_rate_state[sender_id] = {
		"window_start_msec": window_start_msec,
		"message_count": message_count + 1,
	}
	return true

func _refill_global_chat_tokens(now_msec: int) -> void:
	if text_chat_global_refill_msec <= 0:
		text_chat_global_refill_msec = now_msec
		return
	var elapsed_msec := now_msec - text_chat_global_refill_msec
	if elapsed_msec <= 0:
		return
	text_chat_global_tokens = minf(
		CHAT_GLOBAL_BURST_MESSAGES,
		text_chat_global_tokens + float(elapsed_msec) * 0.001 * CHAT_GLOBAL_REFILL_PER_SECOND)
	text_chat_global_refill_msec = now_msec

func _prune_stale_chat_rate_state(now_msec: int) -> void:
	for sender_id in text_chat_rate_state.keys():
		var state: Dictionary = text_chat_rate_state[sender_id]
		if now_msec - int(state.get("window_start_msec", 0)) >= CHAT_RATE_WINDOW_MSEC * 2:
			text_chat_rate_state.erase(sender_id)

func _sanitize_chat_message(text: String) -> String:
	var clean := text.replace("\r", " ").replace("\n", " ").replace("\t", " ").strip_edges()
	if clean.length() > CHAT_MAX_MESSAGE_CHARACTERS:
		clean = clean.substr(0, CHAT_MAX_MESSAGE_CHARACTERS)
	return clean

func _append_text_chat_message(sender_id: int, text: String) -> void:
	var name := _player_display_name(sender_id)
	var removed_oldest := lobby_chat_history.size() >= MAX_LOBBY_CHAT_HISTORY
	if lobby_chat_history.size() >= MAX_LOBBY_CHAT_HISTORY:
		lobby_chat_history.remove_at(0)
	var message := {
		"id": sender_id,
		"name": name,
		"text": text,
	}
	lobby_chat_history.append(message)
	if race_communication_overlay != null and _race_chat_overlay_accepts_messages():
		race_communication_overlay.append_message(sender_id, name, text)
	if lobby_control.visible:
		_append_lobby_chat_box_message(message, removed_oldest)

func _refresh_lobby_chat_box() -> void:
	if lobby_chat_box == null or !lobby_control.visible:
		return
	lobby_chat_box.clear()
	lobby_chat_box.push_color(Color(0.33, 0.33, 0.33, 1.0))
	lobby_chat_box.add_text("Never tell your password to anyone.")
	lobby_chat_box.pop()
	for message in lobby_chat_history:
		_append_lobby_chat_box_line(message)
	lobby_chat_rendered_history_size = lobby_chat_history.size()

func _append_lobby_chat_box_message(message: Dictionary, removed_oldest: bool) -> void:
	var expected_previous_size := lobby_chat_history.size() if removed_oldest else lobby_chat_history.size() - 1
	if lobby_chat_rendered_history_size != expected_previous_size:
		_refresh_lobby_chat_box()
		return
	if removed_oldest:
		lobby_chat_box.remove_paragraph(1, true)
	_append_lobby_chat_box_line(message)
	lobby_chat_rendered_history_size = lobby_chat_history.size()

func _append_lobby_chat_box_line(message: Dictionary) -> void:
	var sender_id := int(message.get("id", -1))
	var color := Color(1.0, 1.0, 0.4, 1.0) if sender_id == _local_player_id() else Color(0.78, 0.84, 1.0, 1.0)
	lobby_chat_box.add_text("\n")
	lobby_chat_box.push_color(color)
	lobby_chat_box.add_text(str(message.get("name", str(sender_id))))
	lobby_chat_box.pop()
	lobby_chat_box.add_text(": " + str(message.get("text", "")))

func _reset_text_chat_history() -> void:
	lobby_chat_history.clear()
	text_chat_rate_state.clear()
	text_chat_global_tokens = CHAT_GLOBAL_BURST_MESSAGES
	text_chat_global_refill_msec = 0
	lobby_chat_rendered_history_size = 0
	if lobby_chat_box != null and lobby_control.visible:
		_refresh_lobby_chat_box()
	if race_communication_overlay != null:
		race_communication_overlay.clear_messages()

func _race_chat_overlay_accepts_messages() -> bool:
	return game_sim != null and game_sim.sim_started and !lobby_control.visible

func _on_lobby_chibi_view_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and lobby_say_text != null:
		lobby_say_text.release_focus()

func _lobby_accepts_chibi_input() -> bool:
	if !_lobby_chibi_active():
		return false
	if !_window_accepts_input():
		return false
	return lobby_say_text == null or !lobby_say_text.has_focus()

func _lobby_chibi_active() -> bool:
	return lobby_control != null and lobby_control.visible and network_manager != null and !network_manager.race_active

func _lobby_chibi_accepts_network_state() -> bool:
	if network_manager == null or network_manager.race_active:
		return false
	return network_manager.is_server or _lobby_chibi_active()

func _clear_lobby_chibi_cars() -> void:
	var had_state := !lobby_chibi_cars.is_empty() or !lobby_chibi_pending_states.is_empty() or lobby_chibi_render_signature != ""
	if !had_state:
		if lobby_chibi_viewport != null and lobby_chibi_viewport.render_target_update_mode != SubViewport.UPDATE_DISABLED:
			lobby_chibi_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		return
	for id in lobby_chibi_cars.keys():
		var car = lobby_chibi_cars[id]
		if car != null and is_instance_valid(car):
			car.queue_free()
	if lobby_chibi_nameplates != null:
		for child in lobby_chibi_nameplates.get_children():
			child.queue_free()
	lobby_chibi_cars.clear()
	lobby_chibi_pending_states.clear()
	lobby_chibi_last_broadcast_msec = 0
	lobby_chibi_render_signature = ""
	if lobby_chibi_render_manager != null:
		lobby_chibi_render_manager.clear_renderer()
	if lobby_chibi_viewport != null:
		lobby_chibi_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED

func _update_lobby_chibi_cars(_delta: float) -> void:
	if !_lobby_chibi_active():
		_clear_lobby_chibi_cars()
		return
	if lobby_chibi_viewport != null:
		lobby_chibi_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
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
	_submit_lobby_chibi_render(roster)
	if network_manager.is_server:
		_broadcast_lobby_chibi_states_if_needed()

func _submit_lobby_chibi_render(roster: Array) -> void:
	if lobby_chibi_render_manager == null:
		return
	var definitions := []
	var settings := []
	var player_ids := []
	var render_cars := []
	var signature_parts := []
	for id in roster:
		if !lobby_chibi_cars.has(id):
			continue
		var car = lobby_chibi_cars[id]
		if car == null or !is_instance_valid(car):
			continue
		var definition: CarDefinition = car.get_render_definition()
		if definition == null:
			continue
		definitions.append(definition)
		settings.append(car.player_settings)
		player_ids.append(int(id))
		render_cars.append(car)
		signature_parts.append(str(id) + ":" + definition.resource_path + ":" + str(car.get_render_livery_hash()) + ":" + _custom_stamp_manifest_signature(int(id)))
	var signature := ""
	for part in signature_parts:
		signature += part + "|"
	if signature != lobby_chibi_render_signature:
		var stamp_render := _prepare_custom_stamp_render_payload(player_ids, settings, "lobby")
		lobby_chibi_render_manager.set_custom_stamp_atlas(stamp_render.get("texture", null))
		lobby_chibi_render_manager.configure_manual(definitions, stamp_render.get("settings", settings))
		lobby_chibi_render_signature = signature
	lobby_chibi_render_manager.begin_manual_submit()
	var render_root_inv := lobby_chibi_render_manager.global_transform.affine_inverse()
	for i in range(render_cars.size()):
		var car = render_cars[i]
		lobby_chibi_render_manager.submit_manual_car(
			i,
			render_root_inv * car.get_render_transform(),
			car.get_render_overlay(),
			car.get_render_outline_velocity(),
			car.get_render_outline_overlay(),
			car.get_render_thrust(),
			false)

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

func _pack_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> Array:
	return [player_id, velocity, knockback_velocity, angle_velocity, position, rotation]

func _store_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	lobby_chibi_pending_states[player_id] = _pack_lobby_chibi_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)

func _broadcast_lobby_chibi_states_if_needed() -> void:
	if !_lobby_chibi_accepts_network_state() or !network_manager.is_server or lobby_chibi_pending_states.is_empty():
		return
	var now := Time.get_ticks_msec()
	if now < lobby_chibi_last_broadcast_msec + LOBBY_CHIBI_BROADCAST_INTERVAL_MSEC:
		return
	lobby_chibi_last_broadcast_msec = now
	var batch := []
	for id in lobby_chibi_pending_states.keys():
		batch.append(lobby_chibi_pending_states[id])
	lobby_chibi_pending_states.clear()
	_apply_lobby_chibi_state_batch.rpc(batch)

func _send_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !_lobby_chibi_active():
		return
	if !network_manager.has_network_peer():
		_apply_lobby_chibi_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	elif network_manager.is_server:
		_store_lobby_chibi_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	else:
		_submit_lobby_chibi_state.rpc_id(1, player_id, velocity, knockback_velocity, angle_velocity, position, rotation)

@rpc("any_peer", "call_local", "unreliable_ordered")
func _submit_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !_lobby_chibi_accepts_network_state() or !network_manager.is_server:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender != 0:
		player_id = sender
	_store_lobby_chibi_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	_apply_lobby_chibi_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	_broadcast_lobby_chibi_states_if_needed()

@rpc("authority", "call_local", "unreliable_ordered")
func _apply_lobby_chibi_state_batch(states: Array) -> void:
	if !_lobby_chibi_active():
		return
	for state in states:
		if typeof(state) != TYPE_ARRAY:
			continue
		var values: Array = state
		if values.size() < 6:
			continue
		if typeof(values[2]) != TYPE_VECTOR3 or typeof(values[4]) != TYPE_VECTOR3 or typeof(values[5]) != TYPE_VECTOR3:
			continue
		_apply_lobby_chibi_state(int(values[0]), float(values[1]), values[2], float(values[3]), values[4], values[5])

@rpc("any_peer", "call_local", "unreliable_ordered")
func _apply_lobby_chibi_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !_lobby_chibi_active():
		return
	if !lobby_chibi_cars.has(player_id):
		return
	var car = lobby_chibi_cars[player_id]
	if car != null and is_instance_valid(car):
		car.apply_remote_state(velocity, knockback_velocity, angle_velocity, position, rotation)

func _lobby_latency_text_for_player(player_id: int) -> String:
	if player_id == _local_player_id():
		return "0ms"
	var value := -1.0
	if network_manager.lobby_latency_rtt_s.has(player_id):
		value = float(network_manager.lobby_latency_rtt_s[player_id])
	elif network_manager.peer_client_rtt_s.has(player_id):
		value = float(network_manager.peer_client_rtt_s[player_id])
	elif !network_manager.is_server and player_id == 1:
		value = network_manager.rtt_s
	if value < 0.0:
		return "--ms"
	return "%dms" % roundi(value * 1000.0)

func _initialize_grand_prix_options(options: Dictionary, roster: Array) -> Dictionary:
	var initialized := options.duplicate(true)
	if int(initialized.get("game_mode", 0)) != 1:
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
	var settings = network_manager.player_settings.get(id, null)
	if settings == null and network_manager.cpu_player_settings.has(id):
		settings = network_manager.cpu_player_settings[id]
	if settings == null:
		if network_manager.cpu_player_ids.has(id):
			settings = build_cpu_player_settings(fallback_cpu_index)
		else:
			var def_path: String = car_definitions[0].resource_path if car_definitions.size() > 0 else ""
			settings = {"car_definition_path": def_path, "accel_setting": 1.0, "username": str(id)}
	var out := {}
	if typeof(settings) == TYPE_DICTIONARY:
		out = (settings as Dictionary).duplicate(true)
	elif settings is PlayerSettings:
		out = (settings as PlayerSettings).to_dict()
	out["_race_player_id"] = id
	out["_race_is_cpu"] = network_manager.cpu_player_ids.has(id)
	return out

func _apply_race_roster_options(options: Dictionary, human_ids: Array, cpu_ids: Array, spectator_ids: Array = []) -> Dictionary:
	var out := options.duplicate(true)
	out["race_human_ids"] = human_ids.duplicate(true)
	out["race_cpu_ids"] = cpu_ids.duplicate(true)
	out["race_spectator_ids"] = spectator_ids.duplicate(true)
	return out

func _local_player_id() -> int:
	if replay_controller.replay_playback_active:
		return replay_controller.replay_playback_local_player_id
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
	var info := network_manager.start_sync_drop_info()
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
	if network_manager != null and network_manager.request_drop_start_sync_stalled_players():
		start_sync_drop_root.visible = false

func _local_player_is_eliminated() -> bool:
	return !network_manager.is_vehicle_restore_enabled() and network_manager.player_eliminations.has(_local_player_id())

func _local_player_is_dnf() -> bool:
	return network_manager.player_dnfs.has(_local_player_id())

func _should_suppress_local_race_input() -> bool:
	return _local_player_is_eliminated() or _local_player_is_dnf()

func _vehicle_restore_off_state_is_eliminated(machine_state: int, state_2: int, position_y: float, minimum_y: float) -> bool:
	if (machine_state & VisualCar.FZ_MS.COMPLETEDRACE_1_Q) != 0:
		return false
	if (machine_state & VisualCar.FZ_MS.FALLOUT) != 0:
		return true
	if position_y < minimum_y:
		return true
	if (machine_state & VisualCar.FZ_MS.ZEROHP) == 0:
		return false
	if (machine_state & VisualCar.FZ_MS.RETIRED) != 0:
		return true
	return (state_2 & 0x80) != 0 and (machine_state & VisualCar.FZ_MS.AIRBORNE) == 0

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

func _local_player_can_live_spectate() -> bool:
	var local_id := _local_player_id()
	return network_manager.spectator_ids.has(local_id) or network_manager.player_finish_times.has(local_id) or network_manager.player_dnfs.has(local_id)

func _live_spectate_targets() -> Array:
	var targets := []
	for id_value in network_manager.get_simulation_roster():
		var id := int(id_value)
		if network_manager.player_finish_times.has(id):
			continue
		if network_manager.player_dnfs.has(id):
			continue
		if network_manager._disconnected_during_race.has(id):
			continue
		if network_manager.player_eliminations.has(id):
			continue
		targets.append(id)
	return targets

func _apply_live_spectate_focus(focus_id: int) -> void:
	if car_node_container.local_visual_car == null:
		return
	live_spectate_focus_id = focus_id
	var car := car_node_container.local_visual_car
	car.owning_id = focus_id
	car.race_hud.focus_player_id = focus_id
	var settings = network_manager.player_settings.get(focus_id, null)
	if settings != null:
		var ps := _player_settings_for_stamp_render(settings)
		if ps != null:
			car.player_settings = ps
	if is_instance_valid(car.name_label):
		car.name_label.text = _player_display_name(focus_id)
	if game_sim != null:
		game_sim.set_gameplay_camera(car.car_camera, focus_id)
	if spectator_node != null and spectator_node.has_method("set_input_enabled"):
		spectator_node.call("set_input_enabled", false)
	car.car_camera.make_current()
	car.make_vehicle_audio_listener_current()

func _toggle_live_spectate_camera() -> void:
	if !_local_player_can_live_spectate() or spectator_node == null:
		return
	var free_camera := spectator_node.get_node_or_null("Camera3D") as Camera3D
	if free_camera == null:
		return
	var current_camera := get_viewport().get_camera_3d()
	if current_camera == free_camera:
		var targets := _live_spectate_targets()
		if targets.is_empty():
			return
		var focus_id := live_spectate_focus_id
		if !targets.has(focus_id):
			focus_id = int(targets[0])
		_apply_live_spectate_focus(focus_id)
		_show_race_notification("Gameplay Camera: %s" % _player_display_name(focus_id), 1200)
		return
	if current_camera != null:
		spectator_node.global_transform = current_camera.global_transform
	if spectator_node.has_method("sync_look_from_current_transform"):
		spectator_node.call("sync_look_from_current_transform")
	if spectator_node.has_method("set_input_enabled"):
		spectator_node.call("set_input_enabled", true)
	free_camera.make_current()
	_show_race_notification("Free Camera", 1200)

func _change_live_spectate_focus(delta: int) -> void:
	if !_local_player_can_live_spectate():
		return
	var targets := _live_spectate_targets()
	if targets.is_empty():
		return
	var current_id := live_spectate_focus_id
	if current_id < 0 and car_node_container.local_visual_car != null:
		current_id = car_node_container.local_visual_car.owning_id
	var current_index := targets.find(current_id)
	var next_index := 0
	if current_index >= 0:
		next_index = posmod(current_index + delta, targets.size())
	elif delta < 0:
		next_index = targets.size() - 1
	_apply_live_spectate_focus(int(targets[next_index]))
	_show_race_notification("Spectating: %s" % _player_display_name(int(targets[next_index])), 1200)

func _update_live_finished_spectate_input() -> void:
	if !_local_player_can_live_spectate():
		live_spectate_strafe_dir = 0
		live_spectate_focus_id = -1
		return
	var left := Input.get_action_raw_strength("StrafeLeft")
	var right := Input.get_action_raw_strength("StrafeRight")
	var dir := 0
	if right >= LIVE_SPECTATE_STRAFE_THRESHOLD and right >= left:
		dir = 1
	elif left >= LIVE_SPECTATE_STRAFE_THRESHOLD:
		dir = -1
	if dir == 0:
		live_spectate_strafe_dir = 0
		return
	if live_spectate_strafe_dir == dir:
		return
	live_spectate_strafe_dir = dir
	_change_live_spectate_focus(dir)

func _race_results_sim() -> GameSim:
	if network_manager.is_server and server_game_sim != null:
		return server_game_sim
	return game_sim

func _race_results_start_tick() -> int:
	var sim := _race_results_sim()
	if sim != null and sim.has_method("get_player_level_start_time"):
		var local_id := _local_player_id()
		if local_id != 0:
			var local_start := int(sim.get_player_level_start_time(local_id))
			if local_start > 0:
				return local_start
		for id_value in network_manager.get_simulation_roster():
			var start_tick := int(sim.get_player_level_start_time(int(id_value)))
			if start_tick > 0:
				return start_tick
	return 300

func _format_race_time(tick_value: int, official_start_tick: int = -1) -> String:
	if official_start_tick < 0:
		official_start_tick = _race_results_start_tick()
	var race_tick := maxi(0, tick_value - official_start_tick)
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

func _format_race_results_text() -> String:
	var lines := ["Race Results"]
	var finish_rows := []
	for id_value in network_manager.player_finish_placements.keys():
		var id := int(id_value)
		finish_rows.append([int(network_manager.player_finish_placements[id_value]), id])
	finish_rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		return int(a[1]) < int(b[1])
	)
	for row in finish_rows:
		var place := int(row[0])
		var id := int(row[1])
		var time_text := ""
		var tick_value := int(_lookup_id_value(network_manager.player_finish_times, id, -1))
		if tick_value >= 0:
			time_text = "  " + _format_race_time(tick_value)
		lines.append("%s  %s%s" % [_format_ordinal(place), _player_display_name(id), time_text])
	if !network_manager.player_eliminations.is_empty():
		lines.append("")
		lines.append("Eliminated")
		for id_value in network_manager.player_eliminations.keys():
			lines.append(_player_display_name(int(id_value)))
	if !network_manager.player_dnfs.is_empty():
		lines.append("")
		lines.append("DNF")
		for id_value in network_manager.player_dnfs.keys():
			lines.append(_player_display_name(int(id_value)))
	return "\n".join(lines)

func _format_grand_prix_results_text() -> String:
	if network_manager.is_grand_prix_enabled():
		var lines := ["Grand Prix Standings"]
		var points: Dictionary = network_manager.race_options.get("grand_prix_points", {})
		var standings := []
		for id_value in points.keys():
			standings.append([int(_lookup_id_value(points, int(id_value), 0)), int(id_value)])
		standings.sort_custom(func(a, b):
			if int(a[0]) != int(b[0]):
				return int(a[0]) > int(b[0])
			return int(a[1]) < int(b[1])
		)
		for i in range(standings.size()):
			lines.append("%s  %s  %d" % [
				_format_ordinal(i + 1),
				_player_display_name(int(standings[i][1])),
				int(standings[i][0])
			])
		return "\n".join(lines)
	return ""

func _format_race_results_summary() -> String:
	var race_text := _format_race_results_text()
	var grand_prix_text := _format_grand_prix_results_text()
	if grand_prix_text == "":
		return race_text
	return race_text + "\n\n" + grand_prix_text

func _race_results_countdown_seconds() -> int:
	if network_manager.net_race_finish_time < 0:
		return -1
	var remaining_msec := maxi(0, RACE_RESULTS_SCREEN_MSEC - (Time.get_ticks_msec() - network_manager.net_race_finish_time))
	return ceili(float(remaining_msec) / 1000.0)

func _next_grand_prix_track_id_for_results() -> String:
	if !network_manager.is_grand_prix_enabled():
		return ""
	var track_ids: Array = network_manager.race_options.get("track_ids", [])
	var next_index := int(network_manager.race_options.get("grand_prix_current_track", 0)) + 1
	if next_index < 0 or next_index >= track_ids.size():
		return ""
	return String(track_ids[next_index])

func _local_player_accel_setting_for_results() -> float:
	if race_results_next_accel_setting >= 0.0:
		return race_results_next_accel_setting
	var local_id := _local_player_id()
	var settings = network_manager.player_settings.get(local_id, {})
	if typeof(settings) == TYPE_DICTIONARY and (settings as Dictionary).has("accel_setting"):
		return clampf(float((settings as Dictionary)["accel_setting"]), 0.0, 1.0)
	if car_settings != null:
		var player_settings = car_settings.get("player_settings")
		if player_settings is PlayerSettings:
			return clampf((player_settings as PlayerSettings).accel_setting, 0.0, 1.0)
	return 1.0

func _apply_local_next_race_accel_setting(accel_setting: float) -> void:
	accel_setting = clampf(accel_setting, 0.0, 1.0)
	var local_id := _local_player_id()
	var settings = network_manager.player_settings.get(local_id, {})
	if typeof(settings) == TYPE_DICTIONARY:
		settings = (settings as Dictionary).duplicate(true)
	else:
		settings = {}
	if settings.is_empty() and car_settings != null:
		var player_settings = car_settings.get("player_settings")
		if player_settings is PlayerSettings:
			settings = (player_settings as PlayerSettings).to_dict()
	settings["accel_setting"] = accel_setting
	network_manager.player_settings[local_id] = settings
	if car_settings != null:
		var player_settings = car_settings.get("player_settings")
		if player_settings is PlayerSettings:
			(player_settings as PlayerSettings).accel_setting = accel_setting

func _on_race_results_machine_setting_changed(accel_setting: float) -> void:
	if _next_grand_prix_track_id_for_results() == "":
		return
	race_results_next_accel_setting = clampf(accel_setting, 0.0, 1.0)
	_apply_local_next_race_accel_setting(race_results_next_accel_setting)
	if network_manager.has_method("send_next_race_accel_setting"):
		network_manager.call("send_next_race_accel_setting", race_results_next_accel_setting)

func _show_race_results_summary() -> void:
	if replay_controller.replay_playback_active:
		_hide_race_results_summary()
		return
	if race_finish_label != null:
		race_finish_label.visible = false
	_set_race_results_hud_hidden(true)
	if race_results_overlay != null:
		race_results_overlay.set_results(_format_race_results_text(), _format_grand_prix_results_text())
		race_results_overlay.set_countdown_seconds(_race_results_countdown_seconds())
		var next_track_id := _next_grand_prix_track_id_for_results()
		race_results_overlay.set_next_race(
			track_content_controller.track_name_for_id(next_track_id),
			_local_player_accel_setting_for_results(),
			next_track_id != "")
		race_results_overlay.visible = true
	race_notification_hide_msec = 0

func _hide_race_results_summary() -> void:
	if race_results_overlay != null:
		race_results_overlay.visible = false
		race_results_overlay.set_countdown_seconds(-1)
		race_results_overlay.set_next_race("", 1.0, false)
	_set_race_results_hud_hidden(false)

func _set_race_results_hud_hidden(hidden: bool) -> void:
	var race_hud := _local_race_hud()
	if hidden:
		if race_results_hid_race_hud:
			return
		if race_hud == null:
			return
		race_results_saved_race_hud_visible = race_hud.visible
		race_results_hid_race_hud = true
		race_hud.visible = false
		return
	if !race_results_hid_race_hud:
		return
	race_results_hid_race_hud = false
	if race_hud != null:
		race_hud.visible = race_results_saved_race_hud_visible

func _local_race_hud() -> Control:
	if car_node_container == null or car_node_container.local_visual_car == null:
		return null
	return car_node_container.local_visual_car.race_hud as Control

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
	if event_type == "dnf":
		if actor_id == _local_player_id():
			_show_race_notification("DNF - Spectating", 3000)
			_change_live_spectate_focus(1)
		return
	if event_type == "ko":
		_show_ko_medal(actor_id, target_id)
		return
	if event_type == "finish":
		if actor_id == _local_player_id():
			race_audio_controller.begin_local_finish()
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
			if replay_controller.replay_collecting_timeline_markers:
				replay_controller.record_timeline_event(event)
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
	options_menu.call("open_settings")

func _on_controller_settings_visibility_changed() -> void:
	if options_menu != null and !options_menu.visible:
		replay_controller.reload_input_calibration()

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

func _prepare_race_custom_stamp_atlas(racer_ids: Array, racer_settings: Array) -> Texture2D:
	var stamp_render := _prepare_custom_stamp_render_payload(racer_ids, racer_settings, "race")
	return stamp_render.get("texture", null)

func _prepare_custom_stamp_render_payload(racer_ids: Array, racer_settings: Array, warning_context := "race") -> Dictionary:
	var render_settings := []
	for settings in racer_settings:
		var normalized := _player_settings_for_stamp_render(settings)
		if normalized != null:
			render_settings.append(normalized)
	if racer_ids.is_empty() or racer_settings.is_empty():
		return {"texture": null, "settings": render_settings}
	var manifests := {}
	var local_payloads := {}
	for i in range(mini(racer_ids.size(), render_settings.size())):
		var racer_id := int(racer_ids[i])
		var manifest := network_manager.custom_stamp_network.get_custom_stamp_manifest(racer_id)
		if manifest.is_empty():
			var payload := _build_local_custom_stamp_payload_for_render(render_settings[i])
			if bool(payload.get("ok", false)):
				manifest = payload.get("manifest", [])
				if !manifest.is_empty():
					local_payloads[racer_id] = payload
			else:
				push_warning("Failed to prepare local custom stamps for %s: %s" % [warning_context, str(payload.get("error", "unknown error"))])
		if !manifest.is_empty():
			manifests[racer_id] = manifest
	if manifests.is_empty():
		return {"texture": null, "settings": render_settings}
	var region_build := CustomStampAtlasBuilder.allocate_player_regions(racer_ids, manifests)
	if !bool(region_build.get("ok", false)):
		push_warning("Failed to allocate custom stamp player regions for %s: %s" % [warning_context, str(region_build.get("error", "unknown error"))])
		return {"texture": null, "settings": render_settings}
	var regions: Dictionary = region_build.get("regions", {})
	var player_records: Array = []
	for i in range(mini(racer_ids.size(), render_settings.size())):
		var player_id := int(racer_ids[i])
		if !manifests.has(player_id) or !regions.has(player_id):
			continue
		var manifest: Array = manifests[player_id]
		var region: Dictionary = regions[player_id]
		var placements := {}
		var blobs: Array = []
		for raw_entry in manifest:
			if typeof(raw_entry) != TYPE_DICTIONARY:
				continue
			var entry: Dictionary = raw_entry
			var stamp_hash: String = str(entry.get("hash", ""))
			if stamp_hash == "":
				continue
			var blob = _race_custom_stamp_blob_for_hash(player_id, stamp_hash, local_payloads)
			if blob == null:
				push_warning("Missing custom stamp blob for %s atlas: %s" % [warning_context, stamp_hash])
				return {"texture": null, "settings": render_settings}
			var rect := _custom_stamp_manifest_rect(entry)
			var region_size := _custom_stamp_manifest_region_size(entry)
			if rect.size.x <= 0 or rect.size.y <= 0 or region_size == Vector2i.ZERO:
				push_warning("Invalid custom stamp packed rect for %s atlas: %s" % [warning_context, stamp_hash])
				return {"texture": null, "settings": render_settings}
			placements[stamp_hash] = {
				"rect": rect,
				"rotated": bool(entry.get("rect_rotated", false)),
				"region_size": region_size,
			}
			blobs.append(blob)
		if placements.is_empty():
			continue
		var region_origin: Vector2i = region["origin"]
		_apply_custom_stamp_manifest_to_settings(render_settings[i], manifest, region_origin)
		player_records.append({
			"player_id": player_id,
			"region_origin": region_origin,
			"placements": placements,
			"blobs": blobs,
		})
	if player_records.is_empty():
		return {"texture": null, "settings": render_settings}
	var atlas_build := CustomStampAtlasBuilder.build_atlas_image(player_records)
	if !bool(atlas_build.get("ok", false)):
		push_warning("Failed to build custom stamp %s atlas: %s" % [warning_context, str(atlas_build.get("error", "unknown error"))])
		return {"texture": null, "settings": render_settings}
	return {
		"texture": CustomStampAtlasBuilder.texture_from_image(atlas_build.get("image", null) as Image),
		"settings": render_settings,
	}

func _build_local_custom_stamp_payload_for_race(settings) -> Dictionary:
	return _build_local_custom_stamp_payload_for_render(settings)

func _build_local_custom_stamp_payload_for_render(settings) -> Dictionary:
	var ps := _player_settings_for_stamp_render(settings)
	if ps == null or ps.car_livery.is_empty():
		return {"ok": true, "manifest": [], "blobs": []}
	var livery := CarLivery.new()
	livery.from_dict(ps.car_livery)
	livery.car_definition_path = ps.car_definition_path
	return CustomStampStore.build_livery_payload(livery)

func _race_custom_stamp_blob_for_hash(player_id: int, stamp_hash: String, local_payloads: Dictionary):
	if local_payloads.has(player_id):
		var payload: Dictionary = local_payloads[player_id]
		for blob in payload.get("blobs", []):
			if blob != null and blob.stamp_hash == stamp_hash:
				return blob
	return network_manager.custom_stamp_network.get_custom_stamp_blob(stamp_hash)

func _apply_custom_stamp_manifest_to_settings(settings, manifest: Array, region_origin: Vector2i) -> void:
	var ps := settings as PlayerSettings
	if ps == null or ps.car_livery.is_empty():
		return
	var entry_by_hash := {}
	for raw_entry in manifest:
		if typeof(raw_entry) == TYPE_DICTIONARY:
			var entry: Dictionary = raw_entry
			entry_by_hash[str(entry.get("hash", ""))] = entry
	if entry_by_hash.is_empty():
		return
	var livery := CarLivery.new()
	livery.from_dict(ps.car_livery)
	var changed := false
	for stamp in livery.stamps:
		if stamp == null or !stamp.is_custom():
			continue
		var stamp_hash: String = stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		if !entry_by_hash.has(stamp_hash):
			continue
		var entry: Dictionary = entry_by_hash[stamp_hash]
		var rect := _custom_stamp_manifest_rect(entry)
		var global_rect := Rect2(
			float(region_origin.x + rect.position.x) / float(CustomStampAtlasBuilder.ATLAS_SIZE.x),
			float(region_origin.y + rect.position.y) / float(CustomStampAtlasBuilder.ATLAS_SIZE.y),
			float(rect.size.x) / float(CustomStampAtlasBuilder.ATLAS_SIZE.x),
			float(rect.size.y) / float(CustomStampAtlasBuilder.ATLAS_SIZE.y)
		)
		stamp.custom_rect = global_rect
		stamp.custom_rect_rotated = bool(entry.get("rect_rotated", false))
		stamp.palette_id = int(entry.get("palette_id", stamp.palette_id))
		changed = true
	if changed:
		ps.set_car_livery(livery)

func _player_settings_for_stamp_render(settings) -> PlayerSettings:
	if settings is PlayerSettings:
		var copy := PlayerSettings.new()
		copy.from_dict((settings as PlayerSettings).to_dict())
		return copy
	elif typeof(settings) == TYPE_DICTIONARY:
		var copy := PlayerSettings.new()
		copy.from_dict(settings)
		return copy
	return null

func _custom_stamp_manifest_signature(player_id: int) -> String:
	var manifest := network_manager.custom_stamp_network.get_custom_stamp_manifest(player_id)
	if manifest.is_empty():
		return ""
	var parts := []
	for raw_entry in manifest:
		if typeof(raw_entry) != TYPE_DICTIONARY:
			continue
		var entry: Dictionary = raw_entry
		var stamp_hash := str(entry.get("hash", ""))
		var blob = network_manager.custom_stamp_network.get_custom_stamp_blob(stamp_hash)
		var cached := "1" if blob != null else "0"
		parts.append("%s:%s:%s:%s:%s" % [
			stamp_hash,
			str(entry.get("rect_pixels", [])),
			str(entry.get("region_size", [])),
			str(entry.get("rect_rotated", false)),
			cached,
		])
	parts.sort()
	var signature := ""
	for part in parts:
		signature += part + ";"
	return signature

func _custom_stamp_manifest_rect(entry: Dictionary) -> Rect2i:
	if !entry.has("rect_pixels") or typeof(entry["rect_pixels"]) != TYPE_ARRAY:
		return Rect2i()
	var values: Array = entry["rect_pixels"]
	if values.size() < 4:
		return Rect2i()
	return Rect2i(int(values[0]), int(values[1]), int(values[2]), int(values[3]))

func _custom_stamp_manifest_region_size(entry: Dictionary) -> Vector2i:
	if !entry.has("region_size") or typeof(entry["region_size"]) != TYPE_ARRAY:
		return Vector2i.ZERO
	var values: Array = entry["region_size"]
	if values.size() < 2:
		return Vector2i.ZERO
	return Vector2i(int(values[0]), int(values[1]))

func _start_race(track_index: int, settings: Array) -> bool:
	if track_index < 0 or track_index >= track_content_controller.tracks.size():
		return false
	_close_settings_menus_for_race_start()
	$Control.visible = false
	lobby_control.visible = false
	_clear_lobby_chibi_cars()
	_last_race_track_index = track_index
	_last_race_settings = settings.duplicate(true)
	active_stickers.clear()
	race_notification_hide_msec = 0
	local_elimination_spectator_active = false
	live_spectate_focus_id = -1
	live_spectate_strafe_dir = 0
	race_dnf_low_speed_ticks.clear()
	var info: Dictionary = track_content_controller.tracks[track_index]
	_hide_race_results_summary()
	if !track_content_controller.prepare_race(track_index):
		return false
	race_audio_controller.reset_for_race()
	race_audio_controller.configure_track_music(
		track_content_controller.current_track_dir,
		track_content_controller.current_metadata)
					
	var chosen_defs : Array = []
	var racer_settings : Array = []
	var racer_ids : Array = []
	var racer_cpu_flags : Array = []
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.get_cpu_roster()
	var keyed_settings := {}
	var ordered_settings := []
	for raw_settings in settings:
		if typeof(raw_settings) != TYPE_DICTIONARY:
			continue
		var settings_dict: Dictionary = raw_settings
		if settings_dict.has("_race_player_id"):
			keyed_settings[int(settings_dict["_race_player_id"])] = settings_dict
		else:
			ordered_settings.append(settings_dict)
	if !keyed_settings.is_empty():
		for id_value in roster:
			var pid := int(id_value)
			if !keyed_settings.has(pid):
				continue
			var d: Dictionary = keyed_settings[pid]
			var ps := PlayerSettings.new()
			ps.from_dict(d)
			if !ps.spectator:
				racer_settings.append(ps)
				var def_res := load(ps.car_definition_path)
				if def_res != null:
					chosen_defs.append(def_res)
				racer_ids.append(pid)
				racer_cpu_flags.append(cpu_ids.has(pid))
	else:
		var racer_roster_index := 0
		for d in ordered_settings:
			var ps := PlayerSettings.new()
			ps.from_dict(d)
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
	var custom_stamp_render := _prepare_custom_stamp_render_payload(racer_ids, racer_settings, "race")
	var custom_stamp_atlas: Texture2D = custom_stamp_render.get("texture", null)
	var bumper_def: CarDefinition = load(BUMPER_DEFINITION_PATH) if bumpers_enabled else null
	var render_defs := chosen_defs.duplicate()
	var render_settings: Array = custom_stamp_render.get("settings", racer_settings.duplicate())
	if bumper_def != null:
		for _slot in BUMPER_POOL_SIZE:
			render_defs.append(bumper_def)
			render_settings.append({})
	var local_id := _local_player_id()
	local_player_index = racer_ids.find(local_id)
	var start_grid_slots := replay_controller.replay_start_grid_slots if replay_controller.replay_playback_active and replay_controller.replay_start_grid_slots.size() == racer_ids.size() else _build_start_grid_slots(racer_ids)
	var visual_focus_id := local_id
	if local_player_index == -1 and !racer_ids.is_empty():
		visual_focus_id = int(racer_ids[0])
	car_node_container.instantiate_cars(chosen_defs, racer_ids, visual_focus_id)
	nametag_names.clear()
	nametag_names.resize(racer_settings.size())
	for idx in racer_settings.size():
		var nametag_text: String = " " + racer_settings[idx].username + " "
		nametag_names[idx] = nametag_text
	for car: VisualCar in car_node_container.get_children():
		car.game_manager = self
	if car_node_container.local_visual_car != null:
		var visual_player_index := racer_ids.find(car_node_container.local_visual_car.owning_id)
		if visual_player_index >= 0 and visual_player_index < racer_settings.size():
			car_node_container.local_visual_car.player_settings = racer_settings[visual_player_index]
			if is_instance_valid(car_node_container.local_visual_car.name_label):
				car_node_container.local_visual_car.name_label.text = nametag_names[visual_player_index]
	car_render_manager.multimesh_render_enabled = !auto_disable_car_multimesh_mode
	car_render_manager.set_custom_stamp_atlas(custom_stamp_atlas)
	car_render_manager.configure(render_defs, car_node_container.get_children(), render_settings)
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
	game_sim.set_start_grid_slots(start_grid_slots)
	game_sim.set_vehicle_restore_enabled(network_manager.is_vehicle_restore_enabled())
	if game_sim.has_method("set_bumpers_enabled"):
		game_sim.set_bumpers_enabled(bumpers_enabled and bumper_def != null)
	if game_sim.has_method("set_s_boost_enabled"):
		game_sim.set_s_boost_enabled(network_manager.is_s_boost_enabled())
	if game_sim.has_method("set_multiplayer_intro_camera_enabled"):
		game_sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode or replay_controller.replay_playback_use_multiplayer_startup)
	game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
	race_audio_controller.configure_vehicle_properties(chosen_defs)
	game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
	_apply_grand_prix_ko_energy_bonuses(game_sim, racer_ids)
	network_manager.netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	replay_controller.start_recording(track_index, settings, racer_ids, racer_cpu_flags, start_grid_slots)
	if car_node_container.local_visual_car != null:
		game_sim.set_gameplay_camera(car_node_container.local_visual_car.car_camera, car_node_container.local_visual_car.owning_id)
		if local_player_index == -1:
			live_spectate_focus_id = car_node_container.local_visual_car.owning_id
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
		server_game_sim.set_start_grid_slots(start_grid_slots)
		server_game_sim.set_vehicle_restore_enabled(network_manager.is_vehicle_restore_enabled())
		if server_game_sim.has_method("set_bumpers_enabled"):
			server_game_sim.set_bumpers_enabled(bumpers_enabled and bumper_def != null)
		if server_game_sim.has_method("set_s_boost_enabled"):
			server_game_sim.set_s_boost_enabled(network_manager.is_s_boost_enabled())
		if server_game_sim.has_method("set_multiplayer_intro_camera_enabled"):
			server_game_sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode or replay_controller.replay_playback_use_multiplayer_startup)
		server_game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
		server_game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
		_apply_grand_prix_ko_energy_bonuses(server_game_sim, racer_ids)
		network_manager.server_netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	network_manager.game_sim = game_sim
	if network_manager.is_server:
		network_manager.server_game_sim = server_game_sim
	if !headless_mode:
		track_content_controller.load_runtime_visuals()
		trigger_objects.clear()
		for trig in _parse_level_triggers(level_buffer.data_array):
			var scene = TRIGGER_SCENES.get(trig["type"], null)
			if scene:
				var inst:Node3D = scene.instantiate()
				inst.transform = trig["transform"]
				obj_container.add_child(inst)
				trigger_objects.append(inst)
	return true

func _on_start_race_button_pressed() -> void:
	if network_manager.is_server:
		if lobby_grand_prix_track_sequence.is_empty():
			return
		_close_settings_menus_for_race_start()
		network_manager.prepare_race_roster("start_button")
		var settings_array : Array = []
		var human_ids := network_manager.player_ids.duplicate(true)
		var cpu_ids := network_manager.cpu_player_ids.duplicate(true)
		var roster := human_ids.duplicate(true)
		roster.append_array(cpu_ids)
		for id_value in roster:
			var id := int(id_value)
			settings_array.append(_settings_dict_for_race_id(id, cpu_ids.find(id)))
		var race_options := _build_lobby_race_options()
		race_options = _initialize_grand_prix_options(race_options, roster)
		race_options = _apply_race_roster_options(race_options, human_ids, cpu_ids, network_manager.spectator_ids)
		var track_ids: Array = race_options.get("track_ids", [track_content_controller.track_id_for_index(lobby_track_selector.selected)])
		var first_track_id := track_content_controller.track_id_for_index(lobby_track_selector.selected)
		if !track_ids.is_empty():
			first_track_id = String(track_ids[0])
		network_manager.send_start_race(first_track_id, settings_array, race_options)

func _on_network_race_started(track_id: String, settings: Array) -> void:
	var track_index := track_content_controller.track_index_for_id(track_id)
	if track_index < 0:
		var message := "Missing race track %s" % track_id
		network_manager.report_race_admission(network_manager.RACE_ADMISSION_FAILED, message)
		push_error(message)
		_show_race_notification(message, 5000)
		if headless_mode:
			get_tree().quit(1)
		return
	var admission_phase := network_manager.race_netplay_phase
	network_manager.report_race_admission(network_manager.RACE_ADMISSION_LOADING, "loading %s" % track_id)
	# Return to the multiplayer loop once before beginning the synchronous load so
	# the server receives the explicit loading acknowledgement.
	await get_tree().process_frame
	if !network_manager.race_active or network_manager.race_netplay_phase != admission_phase:
		return
	race_results_next_accel_setting = -1.0
	race_dnf_low_speed_ticks.clear()
	live_spectate_focus_id = -1
	live_spectate_strafe_dir = 0
	if start_sync_drop_root != null:
		start_sync_drop_root.visible = false
	if race_communication_overlay != null:
		race_communication_overlay.close_chat()
	_close_settings_menus_for_race_start()
	if !_start_race(track_index, settings):
		network_manager.report_race_admission(network_manager.RACE_ADMISSION_FAILED, "race initialization failed")
		return
	game_sim.set_sim_started(false)
	if network_manager.is_server:
		server_game_sim.set_sim_started(false)
	network_manager.report_race_admission(network_manager.RACE_ADMISSION_READY, "race initialized")

func _on_network_race_finished() -> void:
	if headless_mode and network_manager.pending_next_race_track_id == "":
		return
	race_finish_label.visible = false
	_hide_race_results_summary()
	active_stickers.clear()
	if network_manager.pending_next_race_track_id != "":
		_transition_to_next_grand_prix_race()
	else:
		_return_to_lobby()

func _update_player_list() -> void:
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.get_cpu_roster()
	if lobby_player_list_container != null:
		var signature_parts := []
		for id in roster:
			signature_parts.append("%d:%s:%s:%s" % [
				int(id),
				_player_display_name(int(id)),
				str(cpu_ids.has(id)),
				str(network_manager.is_server)
			])
		var signature := "|".join(signature_parts)
		if signature == lobby_player_list_signature:
			return
		lobby_player_list_signature = signature
		for child in lobby_player_list_container.get_children():
			child.queue_free()
		for id in roster:
			var id_int := int(id)
			var row := HBoxContainer.new()
			row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row.add_theme_constant_override("separation", 6)
			lobby_player_list_container.add_child(row)
			var name_label := Label.new()
			name_label.text = _player_display_name(id_int)
			if cpu_ids.has(id):
				name_label.text = "[CPU] " + name_label.text
			name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row.add_child(name_label)
			var can_kick := network_manager.is_server and !cpu_ids.has(id) and id_int != _local_player_id()
			if can_kick:
				var kick_button := Button.new()
				kick_button.text = "Kick"
				kick_button.pressed.connect(_on_lobby_kick_player_pressed.bind(id_int))
				row.add_child(kick_button)
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

func _on_lobby_kick_player_pressed(player_id: int) -> void:
	network_manager.kick_human_player(player_id)
	lobby_player_list_signature = ""
	_update_player_list()

func _window_accepts_input() -> bool:
	if race_pause_open:
		return false
	if race_communication_overlay != null and race_communication_overlay.is_chat_open():
		return false
	var window := get_window()
	return window == null or window.has_focus()

func _take_clean_4k_screenshot() -> void:
	if clean_screenshot_in_progress or headless_mode:
		return
	clean_screenshot_in_progress = true

	var viewport := get_viewport()
	var window := get_window()
	if window == null:
		clean_screenshot_in_progress = false
		return
	var original_window_mode := window.mode
	var original_window_size := window.size
	var original_window_position := window.position
	var original_window_screen := window.current_screen
	var original_canvas_cull_mask := viewport.get_canvas_cull_mask()

	var race_hud := _local_race_hud()
	var race_hud_process_mode := Node.PROCESS_MODE_INHERIT
	var world_sticker_nodes: Array[Node3D] = []
	var world_sticker_visibility: Array[bool] = []
	if race_hud != null:
		race_hud_process_mode = race_hud.process_mode
		race_hud.process_mode = Node.PROCESS_MODE_DISABLED
		var sticker_pool_value = race_hud.get("sticker_pool")
		if sticker_pool_value is Array:
			for value in sticker_pool_value:
				var sticker_node := value as Node3D
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
	window.size = CLEAN_SCREENSHOT_SIZE
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

	if image.is_empty() or image.get_size() != CLEAN_SCREENSHOT_SIZE:
		push_error("Failed to capture a 4K screenshot; rendered image size was %s" % image.get_size())
		clean_screenshot_in_progress = false
		return
	var directory_path := ProjectSettings.globalize_path(CLEAN_SCREENSHOT_DIRECTORY)
	var directory_error := DirAccess.make_dir_recursive_absolute(directory_path)
	if directory_error != OK:
		push_error("Failed to create screenshot directory: %s" % directory_path)
		clean_screenshot_in_progress = false
		return
	var now := Time.get_datetime_dict_from_system()
	var file_name := "mxt_%04d-%02d-%02d_%02d-%02d-%02d_%03d.png" % [
		int(now["year"]),
		int(now["month"]),
		int(now["day"]),
		int(now["hour"]),
		int(now["minute"]),
		int(now["second"]),
		Time.get_ticks_msec() % 1000,
	]
	var screenshot_path := CLEAN_SCREENSHOT_DIRECTORY.path_join(file_name)
	var save_error := image.save_png(screenshot_path)
	if save_error == OK:
		print("Saved clean 4K screenshot: %s" % ProjectSettings.globalize_path(screenshot_path))
	else:
		push_error("Failed to save clean 4K screenshot: %s" % error_string(save_error))
	clean_screenshot_in_progress = false

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
	var top_players := []
	for car in car_node_container.get_children():
		var visual_car := car as VisualCar
		if visual_car == null:
			continue
		var player_id := visual_car.owning_id
		if player_id == local_id:
			continue
		var place := int(game_sim.get_player_race_place(player_id))
		if place >= 1 and place <= TOP_PLACE_BADGE_TEXTURES.size():
			top_players.append([place, player_id])
	top_players.sort_custom(func(a, b): return int(a[0]) < int(b[0]))
	var badge_slot := 0
	for entry in top_players:
		var place := int(entry[0])
		var player_id := int(entry[1])
		var render_transform: Transform3D = game_sim.get_player_render_transform(player_id)
		var world_pos := render_transform.origin
		if camera_position.distance_squared_to(world_pos) > NAMETAG_MAX_DISTANCE_SQ or !active_camera.is_position_in_frustum(world_pos):
			continue
		var badge := placement_badge_pool[badge_slot]
		badge.texture = TOP_PLACE_BADGE_TEXTURES[place - 1]
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
	var process_frames := maxi(render_profile_process_frames, 1)
	print("MXT_RENDER_PROFILE frames=", render_profile_frames,
		" process_frames=", render_profile_process_frames,
		" physics_us=", int(render_profile_physics_us / render_profile_frames),
		" physics_max_us=", render_profile_physics_max_us,
		" tick_us=", int(render_profile_tick_us / render_profile_frames),
		" tick_max_us=", render_profile_tick_max_us,
		" render_us=", int(render_profile_render_us / render_profile_frames),
		" render_max_us=", render_profile_render_max_us,
		" nametag_us=", int(render_profile_nametag_us / render_profile_frames),
		" nametag_max_us=", render_profile_nametag_max_us,
		" local_visual_us=", int(render_profile_local_visual_us / render_profile_frames),
		" local_visual_max_us=", render_profile_local_visual_max_us,
		" input_us=", int(render_profile_input_us / render_profile_frames),
		" input_max_us=", render_profile_input_max_us,
		" events_us=", int(render_profile_events_us / render_profile_frames),
		" events_max_us=", render_profile_events_max_us,
		" camera_us=", int(render_profile_camera_us / render_profile_frames),
		" camera_max_us=", render_profile_camera_max_us,
		" audio_tick_us=", int(render_profile_audio_tick_us / render_profile_frames),
		" audio_tick_max_us=", render_profile_audio_tick_max_us,
		" finish_check_us=", int(render_profile_finish_check_us / render_profile_frames),
		" finish_check_max_us=", render_profile_finish_check_max_us,
		" process_us=", int(render_profile_process_us / process_frames),
		" process_max_us=", render_profile_process_max_us,
		" visuals_only_us=", int(render_profile_visuals_only_us / process_frames),
		" visuals_only_max_us=", render_profile_visuals_only_max_us,
		" frame_gap_max_us=", render_profile_frame_gap_max_us,
		" frame_gap_over_16ms=", render_profile_frame_gap_over_16ms,
		" frame_gap_over_20ms=", render_profile_frame_gap_over_20ms,
		" frame_gap_over_25ms=", render_profile_frame_gap_over_25ms,
		" frame_gap_over_33ms=", render_profile_frame_gap_over_33ms,
		" frame_gap_over_50ms=", render_profile_frame_gap_over_50ms)
	print("MXT_RACER_COUNTS configured_cpus=", singleplayer_cpu_count,
		" requested_cpus=", launch_cpu_driver_count)
	var top_gap_parts := PackedStringArray()
	for i in RENDER_PROFILE_TOP_GAP_COUNT:
		if render_profile_top_gap_us[i] <= 0:
			break
		top_gap_parts.append("%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d" % [
			render_profile_top_gap_us[i],
			render_profile_top_gap_tick[i],
			render_profile_top_gap_physics_us[i],
			render_profile_top_gap_process_us[i],
			render_profile_top_gap_pipeline_draw[i],
			render_profile_top_gap_pipeline_surface[i],
			render_profile_top_gap_pipeline_mesh[i],
			render_profile_top_gap_native_total_us[i],
			render_profile_top_gap_native_vehicle_us[i],
			render_profile_top_gap_native_collision_us[i],
			render_profile_top_gap_native_save_us[i],
			render_profile_top_gap_body_instances[i],
			render_profile_top_gap_draw_calls[i],
			render_profile_top_gap_primitives[i],
			render_profile_top_gap_engine_process_us[i],
			render_profile_top_gap_engine_physics_us[i],
		])
	print("MXT_RENDER_PROFILE_TOP_GAPS gap_us:tick:physics_us:process_us:pipeline_draw:pipeline_surface:pipeline_mesh:native_total_us:native_vehicle_us:native_collision_us:native_save_us:body_instances:draw_calls:primitives:engine_process_us:engine_physics_us=", "|".join(top_gap_parts))
	var profile_hud := _local_race_hud() as RaceHud
	if profile_hud != null:
		print(profile_hud.get_render_profile_string())
	var profile_visual_car := car_node_container.local_visual_car
	if profile_visual_car != null:
		print(profile_visual_car.get_render_profile_string())
	_print_active_script_profile_counts()
	print(game_sim.get_phase_profile_string())
	print(game_sim.get_render_profile_string())

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

func _record_render_profile_gap(gap_us: int, pipeline_draw: int, pipeline_surface: int, pipeline_mesh: int, draw_calls: int, primitives: int, engine_process_us: int, engine_physics_us: int) -> void:
	if gap_us <= render_profile_top_gap_us[RENDER_PROFILE_TOP_GAP_COUNT - 1]:
		return
	var insert_at := RENDER_PROFILE_TOP_GAP_COUNT - 1
	while insert_at > 0 and gap_us > render_profile_top_gap_us[insert_at - 1]:
		render_profile_top_gap_us[insert_at] = render_profile_top_gap_us[insert_at - 1]
		render_profile_top_gap_tick[insert_at] = render_profile_top_gap_tick[insert_at - 1]
		render_profile_top_gap_physics_us[insert_at] = render_profile_top_gap_physics_us[insert_at - 1]
		render_profile_top_gap_process_us[insert_at] = render_profile_top_gap_process_us[insert_at - 1]
		render_profile_top_gap_pipeline_draw[insert_at] = render_profile_top_gap_pipeline_draw[insert_at - 1]
		render_profile_top_gap_pipeline_surface[insert_at] = render_profile_top_gap_pipeline_surface[insert_at - 1]
		render_profile_top_gap_pipeline_mesh[insert_at] = render_profile_top_gap_pipeline_mesh[insert_at - 1]
		render_profile_top_gap_native_total_us[insert_at] = render_profile_top_gap_native_total_us[insert_at - 1]
		render_profile_top_gap_native_vehicle_us[insert_at] = render_profile_top_gap_native_vehicle_us[insert_at - 1]
		render_profile_top_gap_native_collision_us[insert_at] = render_profile_top_gap_native_collision_us[insert_at - 1]
		render_profile_top_gap_native_save_us[insert_at] = render_profile_top_gap_native_save_us[insert_at - 1]
		render_profile_top_gap_body_instances[insert_at] = render_profile_top_gap_body_instances[insert_at - 1]
		render_profile_top_gap_draw_calls[insert_at] = render_profile_top_gap_draw_calls[insert_at - 1]
		render_profile_top_gap_primitives[insert_at] = render_profile_top_gap_primitives[insert_at - 1]
		render_profile_top_gap_engine_process_us[insert_at] = render_profile_top_gap_engine_process_us[insert_at - 1]
		render_profile_top_gap_engine_physics_us[insert_at] = render_profile_top_gap_engine_physics_us[insert_at - 1]
		insert_at -= 1
	var native_phase_sample := game_sim.get_phase_profile_last_sample()
	var native_render_sample := game_sim.get_render_profile_last_sample()
	render_profile_top_gap_us[insert_at] = gap_us
	render_profile_top_gap_tick[insert_at] = _singleplayer_tick
	render_profile_top_gap_physics_us[insert_at] = render_profile_last_physics_sample_us
	render_profile_top_gap_process_us[insert_at] = render_profile_last_process_sample_us
	render_profile_top_gap_pipeline_draw[insert_at] = pipeline_draw
	render_profile_top_gap_pipeline_surface[insert_at] = pipeline_surface
	render_profile_top_gap_pipeline_mesh[insert_at] = pipeline_mesh
	if native_phase_sample.size() >= 9:
		render_profile_top_gap_native_total_us[insert_at] = native_phase_sample[0]
		render_profile_top_gap_native_vehicle_us[insert_at] = native_phase_sample[3]
		render_profile_top_gap_native_collision_us[insert_at] = native_phase_sample[4]
		render_profile_top_gap_native_save_us[insert_at] = native_phase_sample[8]
	if native_render_sample.size() >= 2:
		render_profile_top_gap_body_instances[insert_at] = native_render_sample[0]
	render_profile_top_gap_draw_calls[insert_at] = draw_calls
	render_profile_top_gap_primitives[insert_at] = primitives
	render_profile_top_gap_engine_process_us[insert_at] = engine_process_us
	render_profile_top_gap_engine_physics_us[insert_at] = engine_physics_us

func _physics_process(delta: float) -> void:
	if auto_track_editor_mode:
		return
	if headless_mode:
		if singleplayer_mode and game_sim.sim_started:
			if replay_controller.replay_playback_active:
				replay_controller.simulate_playback()
			else:
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
		network_manager.process_lobby_latency()
		_update_player_list()
		_update_lobby_chibi_cars(delta)
		var can_edit_cpu := network_manager.is_server and !network_manager.race_active
		if lobby_cpu_count_label != null:
			lobby_cpu_count_label.text = "CPU Drivers: %d" % network_manager.get_cpu_roster().size()
		add_cpu_button.disabled = !can_edit_cpu
		remove_cpu_button.disabled = !can_edit_cpu or network_manager.get_cpu_roster().is_empty()
		_update_lobby_start_race_button()
		lobby_track_selector.disabled = !can_edit_cpu
		if lobby_game_mode_choice != null:
			lobby_game_mode_choice.disabled = !can_edit_cpu
		if lobby_vehicle_restore_toggle != null:
			lobby_vehicle_restore_toggle.disabled = !can_edit_cpu
		if lobby_bumpers_toggle != null:
			lobby_bumpers_toggle.disabled = !can_edit_cpu
		if lobby_s_boost_toggle != null:
			lobby_s_boost_toggle.disabled = !can_edit_cpu
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
	else:
		_clear_lobby_chibi_cars()
	if game_sim.sim_started:
		var profile_physics_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		var profile_input_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		var local_pi := PlayerInputClass.new()
		if !_should_suppress_local_race_input() and _window_accepts_input() and players.size() > local_player_index:
			var controller = players[local_player_index]
			if controller != null:
				local_pi = controller.get_input()
		var input_bytes := local_pi.serialize()
		if auto_render_profile_mode:
			var profile_input_us := Time.get_ticks_usec() - profile_input_start
			render_profile_input_us += profile_input_us
			render_profile_input_max_us = maxi(render_profile_input_max_us, profile_input_us)
		if singleplayer_mode and _local_player_is_dnf() and game_sim.has_method("get_native_cpu_input_for_tick"):
			input_bytes = game_sim.get_native_cpu_input_for_tick(_local_player_id(), _singleplayer_tick)
		if singleplayer_mode:
			var profile_tick_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
			if replay_controller.replay_playback_active:
				replay_controller.simulate_playback()
			else:
				_simulate_singleplayer_tick(input_bytes)
			if auto_render_profile_mode:
				var profile_tick_us := Time.get_ticks_usec() - profile_tick_start
				render_profile_tick_us += profile_tick_us
				render_profile_tick_max_us = maxi(render_profile_tick_max_us, profile_tick_us)
			if auto_quit_after_frames >= 0 and _singleplayer_tick >= auto_quit_after_frames:
				_print_render_profile_summary()
				RenderingServer.force_sync()
				get_tree().quit()
				return
		else:
			network_manager.set_local_input(input_bytes)
			if network_manager.is_server:
				_simulate_host_frame(input_bytes)
			else:
				_simulate_single_tick()
		var profile_events_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		if !replay_controller.replay_playback_active:
			_consume_authoritative_race_events()
		if auto_render_profile_mode:
			var profile_events_us := Time.get_ticks_usec() - profile_events_start
			render_profile_events_us += profile_events_us
			render_profile_events_max_us = maxi(render_profile_events_max_us, profile_events_us)
		var profile_camera_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		_update_native_render_camera()
		if auto_render_profile_mode:
			var profile_camera_us := Time.get_ticks_usec() - profile_camera_start
			render_profile_camera_us += profile_camera_us
			render_profile_camera_max_us = maxi(render_profile_camera_max_us, profile_camera_us)
		var profile_render_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		game_sim.render_gamesim()
		if auto_render_profile_mode:
			var profile_render_us := Time.get_ticks_usec() - profile_render_start
			render_profile_render_us += profile_render_us
			render_profile_render_max_us = maxi(render_profile_render_max_us, profile_render_us)
		var profile_audio_tick_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		race_audio_controller.after_simulation_tick()
		if auto_render_profile_mode:
			var profile_audio_tick_us := Time.get_ticks_usec() - profile_audio_tick_start
			render_profile_audio_tick_us += profile_audio_tick_us
			render_profile_audio_tick_max_us = maxi(render_profile_audio_tick_max_us, profile_audio_tick_us)
		var profile_nametag_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		_update_nametags(get_viewport().get_camera_3d(), delta)
		if auto_render_profile_mode:
			var profile_nametag_us := Time.get_ticks_usec() - profile_nametag_start
			render_profile_nametag_us += profile_nametag_us
			render_profile_nametag_max_us = maxi(render_profile_nametag_max_us, profile_nametag_us)
		var profile_local_visual_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		if car_node_container.local_visual_car != null:
			car_node_container.local_visual_car.just_rendered()
		if auto_render_profile_mode:
			var profile_local_visual_us := Time.get_ticks_usec() - profile_local_visual_start
			render_profile_local_visual_us += profile_local_visual_us
			render_profile_local_visual_max_us = maxi(render_profile_local_visual_max_us, profile_local_visual_us)
		var profile_finish_check_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		_check_race_finished()
		if auto_render_profile_mode:
			var profile_finish_check_us := Time.get_ticks_usec() - profile_finish_check_start
			var profile_physics_us := Time.get_ticks_usec() - profile_physics_start
			render_profile_finish_check_us += profile_finish_check_us
			render_profile_finish_check_max_us = maxi(render_profile_finish_check_max_us, profile_finish_check_us)
			render_profile_physics_us += profile_physics_us
			render_profile_physics_max_us = maxi(render_profile_physics_max_us, profile_physics_us)
			render_profile_last_physics_sample_us = profile_physics_us
			render_profile_frames += 1

func _simulate_singleplayer_tick(input_bytes: PackedByteArray = PackedByteArray()):
	var start_time := Time.get_ticks_usec()
	if replay_controller.replay_playback_active:
		replay_controller.simulate_playback()
		network_manager.rollback_frametime_us = Time.get_ticks_usec() - start_time
		return
	input_bytes = replay_controller.consume_debug_playback_input(input_bytes)
	if !game_sim.sim_started:
		return
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
		if _local_player_is_dnf() and game_sim.has_method("get_native_cpu_input_for_tick"):
			input_bytes = game_sim.get_native_cpu_input_for_tick(_local_player_id(), _singleplayer_tick)
	replay_controller.record_debug_input(input_bytes)
	_dump_offline_auth_input_sample(input_bytes)
	_dump_offline_state_sample()
	var tick_to_record := _singleplayer_tick
	game_sim.tick_singleplayer(_local_player_id(), input_bytes)
	replay_controller.record_singleplayer_frame(tick_to_record)
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
		if cpu_ids.has(id) or network_manager.player_dnfs.has(int(id)) or network_manager._disconnected_during_race.has(int(id)):
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
	if _handle_race_chat_unhandled_input(event):
		return
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F3:
		var profile := game_sim.get_phase_profile_string()
		var render_profile := game_sim.get_render_profile_string()
		DisplayServer.clipboard_set(profile + "\n" + render_profile)
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F4:
		_take_clean_4k_screenshot()
		get_viewport().set_input_as_handled()
		return
	if replay_controller.handle_unhandled_input(event):
		get_viewport().set_input_as_handled()
		return
	if game_sim.sim_started and _local_player_can_live_spectate():
		if event.is_action_pressed("DpadLeft"):
			_change_live_spectate_focus(-1)
			get_viewport().set_input_as_handled()
			return
		if event.is_action_pressed("DpadRight"):
			_change_live_spectate_focus(1)
			get_viewport().set_input_as_handled()
			return
		if event.is_action_pressed("SpinAttack") or (event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_TAB):
			_toggle_live_spectate_camera()
			get_viewport().set_input_as_handled()
			return
	if lobby_control.visible and event.is_action_pressed("ui_cancel"):
		_close_settings_menus_for_race_start()
		_return_to_menu()
		get_viewport().set_input_as_handled()
	if game_sim.sim_started and event.is_action_pressed("ui_cancel"):
		if race_pause_open:
			_close_race_pause_menu()
		else:
			_open_race_pause_menu()
		get_viewport().set_input_as_handled()

func _handle_race_chat_unhandled_input(event: InputEvent) -> bool:
	if race_communication_overlay == null or !(event is InputEventKey):
		return false
	var key := event as InputEventKey
	if !key.pressed or key.echo:
		return false
	var is_enter := key.keycode == KEY_ENTER or key.keycode == KEY_KP_ENTER
	var is_escape := key.keycode == KEY_ESCAPE
	if race_communication_overlay.is_chat_open():
		if is_enter or is_escape:
			get_viewport().set_input_as_handled()
			return true
		return false
	if !is_enter:
		return false
	if !game_sim.sim_started or lobby_control.visible or race_pause_open:
		return false
	if car_settings != null and car_settings.visible:
		return false
	if options_menu != null and options_menu.visible:
		return false
	var window := get_window()
	if window != null and !window.has_focus():
		return false
	race_communication_overlay.open_chat()
	get_viewport().set_input_as_handled()
	return true

func _return_to_menu() -> void:
	if race_communication_overlay != null:
		race_communication_overlay.close_chat()
	race_audio_controller.leave_race(0.5)
	replay_controller.reset_for_transition(network_manager.is_server and !singleplayer_mode)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_close_race_pause_menu()
	race_finish_label.visible = false
	_hide_race_results_summary()
	active_stickers.clear()
	_reset_nametag_pool()
	var was_server := network_manager.is_server
	network_manager.disconnect_from_server()
	game_sim.destroy_gamesim()
	if was_server:
		server_game_sim.destroy_gamesim()
	network_manager.game_sim = null
	network_manager.server_game_sim = null
	track_content_controller.teardown_runtime()
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
	live_spectate_focus_id = -1
	live_spectate_strafe_dir = 0
	race_dnf_low_speed_ticks.clear()
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	singleplayer_mode = false
	_singleplayer_tick = 0
	$Control.visible = true
	lobby_control.visible = false
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false

func _return_to_lobby() -> void:
	if race_communication_overlay != null:
		race_communication_overlay.close_chat()
	race_audio_controller.leave_race(0.5)
	replay_controller.reset_for_transition(network_manager.is_server and !singleplayer_mode)
	_close_race_pause_menu()
	_reset_nametag_pool()
	game_sim.destroy_gamesim()
	race_finish_label.visible = false
	_hide_race_results_summary()
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	track_content_controller.teardown_runtime()
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
	live_spectate_focus_id = -1
	live_spectate_strafe_dir = 0
	race_dnf_low_speed_ticks.clear()
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	lobby_control.visible = true
	network_manager.flush_waiting_peers()
	network_manager.reset_race_state(true)
	network_manager.broadcast_lobby_roster()
	singleplayer_mode = false
	_singleplayer_tick = 0

func _teardown_race_world_for_transition() -> void:
	race_audio_controller.leave_race()
	replay_controller.reset_for_transition(network_manager.is_server and !singleplayer_mode)
	_close_race_pause_menu()
	_reset_nametag_pool()
	game_sim.destroy_gamesim()
	race_finish_label.visible = false
	_hide_race_results_summary()
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	track_content_controller.teardown_runtime()
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
	live_spectate_focus_id = -1
	live_spectate_strafe_dir = 0
	race_dnf_low_speed_ticks.clear()
	Engine.physics_ticks_per_second = 60
	local_player_index = 0
	lobby_control.visible = false
	singleplayer_mode = false
	_singleplayer_tick = 0

func _transition_to_next_grand_prix_race() -> void:
	var next_track_id := network_manager.pending_next_race_track_id
	var next_settings := network_manager.pending_next_race_settings.duplicate(true)
	var next_options := network_manager.pending_next_race_options.duplicate(true)
	_teardown_race_world_for_transition()
	if network_manager.is_server:
		network_manager.flush_waiting_peers(true)
	network_manager.reset_race_state(true)
	network_manager.race_options = next_options
	_apply_grand_prix_eliminations(next_options)
	network_manager.start_race(next_track_id, next_settings, next_options)

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
			network_manager.player_settings.erase(id)

func _lookup_id_value(dict: Dictionary, id: int, fallback):
	if dict.has(id):
		return dict[id]
	var id_string := str(id)
	if dict.has(id_string):
		return dict[id_string]
	return fallback

func _build_start_grid_slots(racer_ids: Array) -> PackedInt32Array:
	var slots := PackedInt32Array()
	slots.resize(racer_ids.size())
	for i in range(racer_ids.size()):
		slots[i] = -1
	if singleplayer_mode and !replay_controller.replay_playback_use_multiplayer_startup and !network_manager.get_cpu_roster().is_empty():
		var local_index := racer_ids.find(_local_player_id())
		if local_index >= 0 and racer_ids.size() > 1:
			var next_slot := 0
			for i in range(racer_ids.size()):
				if i == local_index:
					continue
				slots[i] = next_slot
				next_slot += 1
			slots[local_index] = racer_ids.size() - 1
			return slots
	if !network_manager.is_grand_prix_enabled():
		return slots
	var current_track_index := int(network_manager.race_options.get("grand_prix_current_track", 0))
	if current_track_index <= 0:
		return slots
	var points: Dictionary = network_manager.race_options.get("grand_prix_points", {})
	var standings := []
	for i in range(racer_ids.size()):
		var id := int(racer_ids[i])
		standings.append([int(_lookup_id_value(points, id, 0)), i, id])
	standings.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) > int(b[0])
		return int(a[1]) < int(b[1])
	)
	var racer_count := racer_ids.size()
	for rank in range(standings.size()):
		var racer_index := int(standings[rank][1])
		slots[racer_index] = racer_count - 1 - rank
	return slots

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
	var race_racers := network_manager.get_simulation_roster()
	var racer_count := race_racers.size()
	var place_by_id := _build_final_race_place_map(sim, race_racers)
	var finish_tick_by_id := _build_final_race_finish_tick_map(place_by_id)
	network_manager.send_final_race_results(place_by_id, finish_tick_by_id)
	for id_value in race_racers:
		var id := int(id_value)
		var total := int(_lookup_id_value(points, id, 0))
		var place := int(_lookup_id_value(place_by_id, id, 0))
		if place > 0:
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

func _build_final_race_place_map(sim: GameSim, race_racers: Array) -> Dictionary:
	var place_by_id := {}
	var placement_rows := []
	for id_value in race_racers:
		var id := int(id_value)
		if network_manager.player_dnfs.has(id):
			continue
		var place := int(_lookup_id_value(network_manager.player_finish_placements, id, 0))
		if place > 0:
			var finish_tick := int(_lookup_id_value(network_manager.player_finish_times, id, 0x7fffffff))
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
		if place_by_id.has(id) or network_manager.player_dnfs.has(id):
			continue
		var finish_tick := int(_lookup_id_value(network_manager.player_finish_times, id, -1))
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
		if network_manager._disconnected_during_race.has(id) or network_manager.player_eliminations.has(id) or network_manager.player_dnfs.has(id):
			continue
		place_by_id[id] = place_by_id.size() + 1
	return place_by_id

func _build_final_race_finish_tick_map(place_by_id: Dictionary) -> Dictionary:
	var finish_tick_by_id := {}
	for id_value in place_by_id.keys():
		var id := int(id_value)
		var finish_tick := int(_lookup_id_value(network_manager.player_finish_times, id, -1))
		if finish_tick >= 0:
			finish_tick_by_id[id] = finish_tick
	return finish_tick_by_id

func _build_next_grand_prix_settings(options: Dictionary) -> Array:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var settings := []
	var active_ids := network_manager.player_ids.duplicate(true)
	active_ids.append_array(network_manager.cpu_player_ids)
	for id_value in active_ids:
		var id := int(id_value)
		if eliminated_ids.has(id):
			continue
		settings.append(_settings_dict_for_race_id(id, network_manager.cpu_player_ids.find(id)))
	return settings

func _build_next_grand_prix_rosters(options: Dictionary) -> Dictionary:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var human_ids := []
	for id_value in network_manager.player_ids:
		var id := int(id_value)
		if !eliminated_ids.has(id):
			human_ids.append(id)
	var cpu_ids := []
	for id_value in network_manager.cpu_player_ids:
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
	var options := network_manager.race_options.duplicate(true)
	var track_ids: Array = options.get("track_ids", [])
	var current_index := int(options.get("grand_prix_current_track", 0))
	var next_index := current_index + 1
	if next_index >= track_ids.size() or !_has_active_human_grand_prix_racer(options):
		network_manager.send_end_race()
		return
	options["grand_prix_current_track"] = next_index
	var next_track_id := String(track_ids[next_index])
	var next_settings := _build_next_grand_prix_settings(options)
	var next_rosters := _build_next_grand_prix_rosters(options)
	options = _apply_race_roster_options(
		options,
		next_rosters["human_ids"],
		next_rosters["cpu_ids"],
		next_rosters["spectator_ids"])
	options = network_manager.reserve_next_race_netplay_options(options)
	var seed := randi()
	options["spawn_seed"] = seed
	network_manager.send_end_race(next_track_id, next_settings, options)

func _race_control_has_started(sim: GameSim) -> bool:
	if sim == null:
		return network_manager.get_race_tick() >= 300
	return network_manager.get_race_tick() >= sim.get_race_control_start_tick()

func _mark_racer_dnf(racer_id: int, reason: String) -> void:
	var tick := network_manager.get_race_tick()
	if network_manager.is_server and !singleplayer_mode:
		network_manager.send_player_dnf(racer_id, tick, reason)
	else:
		network_manager.record_player_dnf(racer_id, tick, reason)

func _update_low_speed_dnf(racer_id: int, finish_sim: GameSim, race_control_started: bool) -> bool:
	if singleplayer_mode:
		race_dnf_low_speed_ticks.erase(racer_id)
		return false
	if finish_sim == null:
		return false
	if !race_control_started:
		race_dnf_low_speed_ticks.erase(racer_id)
		return false
	if network_manager.player_finish_times.has(racer_id) or network_manager.player_dnfs.has(racer_id):
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
		if network_manager.player_finish_times.has(int(id_value)):
			count += 1
	return count

func _update_force_end_dnf(human_racer_ids: Array) -> void:
	if singleplayer_mode:
		return
	var human_count := human_racer_ids.size()
	if human_count <= 0:
		return
	var finished_count := _human_finish_count(human_racer_ids)
	if network_manager.race_force_end_deadline_tick < 0 and finished_count * 4 >= human_count:
		var deadline_tick := network_manager.get_race_tick() + FORCE_END_WINDOW_TICKS
		if network_manager.is_server and !singleplayer_mode:
			network_manager.send_race_force_end_deadline(deadline_tick)
		else:
			network_manager.race_force_end_deadline_tick = deadline_tick
	if network_manager.race_force_end_deadline_tick < 0:
		return
	if network_manager.get_race_tick() < network_manager.race_force_end_deadline_tick:
		return
	for id_value in human_racer_ids:
		var id := int(id_value)
		if network_manager.player_finish_times.has(id):
			continue
		if network_manager.player_dnfs.has(id):
			continue
		if network_manager._disconnected_during_race.has(id):
			continue
		if network_manager.player_eliminations.has(id):
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
	var race_control_started := _race_control_has_started(finish_sim)
	_update_force_end_dnf(finish_watch_ids)
	if finish_sim != null:
		for id_value in finish_sim.get_finished_player_ids():
			var racer_id := int(id_value)
			if network_manager._disconnected_during_race.has(racer_id):
				continue
			if network_manager.player_finish_times.has(racer_id) or network_manager.player_eliminations.has(racer_id):
				continue
			if network_manager.is_server and !singleplayer_mode:
				network_manager.send_player_finished(racer_id, network_manager.server_tick)
			else:
				network_manager.record_player_finished(racer_id, network_manager.clients_server_tick)
		if !network_manager.is_vehicle_restore_enabled():
			for id_value in finish_sim.get_eliminated_player_ids():
				var racer_id := int(id_value)
				if network_manager._disconnected_during_race.has(racer_id):
					continue
				if network_manager.player_finish_times.has(racer_id) or network_manager.player_eliminations.has(racer_id):
					continue
				if network_manager.is_server and !singleplayer_mode:
					network_manager.send_player_eliminated(racer_id, network_manager.server_tick)
				elif singleplayer_mode:
					network_manager.record_player_eliminated(racer_id, network_manager.clients_server_tick)
	var all_done := true
	for id_value in finish_watch_ids:
		var racer_id := int(id_value)
		if network_manager._disconnected_during_race.has(racer_id):
			continue
		if network_manager.player_finish_times.has(racer_id) or network_manager.player_eliminations.has(racer_id) or network_manager.player_dnfs.has(racer_id):
			continue
		if _update_low_speed_dnf(racer_id, finish_sim, race_control_started):
			continue
		if !network_manager.player_dnfs.has(racer_id):
			all_done = false
	if replay_controller.replay_playback_active:
		return
	if network_manager.is_server:
		if all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()
				network_manager.send_race_finish_time(network_manager.net_race_finish_time)
				_record_grand_prix_race_results(finish_sim)
				_show_race_results_summary()
			if Time.get_ticks_msec() > network_manager.net_race_finish_time + RACE_RESULTS_SCREEN_MSEC:
				_finish_or_advance_grand_prix(finish_sim)
				race_finish_label.visible = false
				_hide_race_results_summary()
	else:
		if singleplayer_mode and all_done:
			if network_manager.net_race_finish_time == -1:
				network_manager.net_race_finish_time = Time.get_ticks_msec()
				_show_race_results_summary()

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
	_update_playtest_lobby_probe()
	var profile_process_start := Time.get_ticks_usec() if auto_render_profile_mode and game_sim.sim_started else 0
	if profile_process_start > 0:
		var profile_pipeline_draw := int(Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_DRAW))
		var profile_pipeline_surface := int(Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_SURFACE))
		var profile_pipeline_mesh := int(Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_MESH))
		var profile_draw_calls := int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
		var profile_primitives := int(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME))
		var profile_engine_process_us := roundi(Performance.get_monitor(Performance.TIME_PROCESS) * 1000000.0)
		var profile_engine_physics_us := roundi(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000000.0)
		var profile_pipeline_draw_delta := 0 if render_profile_last_pipeline_draw < 0 else maxi(0, profile_pipeline_draw - render_profile_last_pipeline_draw)
		var profile_pipeline_surface_delta := 0 if render_profile_last_pipeline_surface < 0 else maxi(0, profile_pipeline_surface - render_profile_last_pipeline_surface)
		var profile_pipeline_mesh_delta := 0 if render_profile_last_pipeline_mesh < 0 else maxi(0, profile_pipeline_mesh - render_profile_last_pipeline_mesh)
		if render_profile_last_process_start_us > 0 and render_profile_frames > 120:
			var profile_frame_gap_us := profile_process_start - render_profile_last_process_start_us
			render_profile_frame_gap_max_us = maxi(render_profile_frame_gap_max_us, profile_frame_gap_us)
			if profile_frame_gap_us > 16667:
				render_profile_frame_gap_over_16ms += 1
				_record_render_profile_gap(profile_frame_gap_us, profile_pipeline_draw_delta, profile_pipeline_surface_delta, profile_pipeline_mesh_delta, profile_draw_calls, profile_primitives, profile_engine_process_us, profile_engine_physics_us)
			if profile_frame_gap_us > 20000:
				render_profile_frame_gap_over_20ms += 1
			if profile_frame_gap_us > 25000:
				render_profile_frame_gap_over_25ms += 1
			if profile_frame_gap_us > 33333:
				render_profile_frame_gap_over_33ms += 1
			if profile_frame_gap_us > 50000:
				render_profile_frame_gap_over_50ms += 1
		render_profile_last_process_start_us = profile_process_start
		render_profile_last_pipeline_draw = profile_pipeline_draw
		render_profile_last_pipeline_surface = profile_pipeline_surface
		render_profile_last_pipeline_mesh = profile_pipeline_mesh
	race_audio_controller.update(delta)
	_update_race_communication_overlay()
	_update_start_sync_drop_panel()
	var now_msec := Time.get_ticks_msec()
	for id in active_stickers.keys():
		var data: Dictionary = active_stickers[id]
		if now_msec > int(data.get("expires", 0)):
			active_stickers.erase(id)
	if race_finish_label.visible and race_notification_hide_msec > 0 and now_msec > race_notification_hide_msec and network_manager.net_race_finish_time == -1:
		race_finish_label.visible = false
		race_notification_hide_msec = 0
	_update_lobby_debug_label_visibility()
	frame_time_label.text = str(network_manager.rollback_frametime_us) + "us"
	rtt_label.text = str(roundi(network_manager.rtt_s * 1000.0)) + "ms"
	if game_sim.sim_started and network_manager.net_race_finish_time != -1 and !replay_controller.replay_playback_active:
		_show_race_results_summary()
		replay_controller.refresh_pause_button()
	if game_sim.sim_started:
		_update_live_finished_spectate_input()
		var profile_visuals_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		replay_controller.update(delta)
		_update_native_render_camera()
		game_sim.render_gamesim_visuals_only(delta)
		if auto_render_profile_mode:
			var profile_visuals_only_us := Time.get_ticks_usec() - profile_visuals_start
			var profile_process_us := Time.get_ticks_usec() - profile_process_start
			render_profile_visuals_only_us += profile_visuals_only_us
			render_profile_visuals_only_max_us = maxi(render_profile_visuals_only_max_us, profile_visuals_only_us)
			render_profile_process_us += profile_process_us
			render_profile_process_max_us = maxi(render_profile_process_max_us, profile_process_us)
			render_profile_last_process_sample_us = profile_process_us
			render_profile_process_frames += 1

func _update_lobby_debug_label_visibility() -> void:
	var in_lobby := lobby_control.visible
	if frame_time_label != null:
		frame_time_label.visible = !in_lobby and !auto_disable_hud_mode
	if rtt_label != null:
		rtt_label.visible = !in_lobby and !auto_disable_hud_mode
	if version_label != null:
		version_label.visible = !in_lobby

func _update_race_communication_overlay() -> void:
	if race_communication_overlay == null:
		return
	if !game_sim.sim_started or replay_controller.replay_playback_active or !network_manager.has_network_peer():
		race_communication_overlay.set_voice_status({"race_active": false}, {})
		return
	var voice_node := network_manager.get_node_or_null("ProximityVoiceChat")
	if voice_node == null or !voice_node.has_method("get_voice_debug_status"):
		race_communication_overlay.set_voice_status({"race_active": false}, {})
		return
	var status: Dictionary = voice_node.call("get_voice_debug_status")
	var player_names := {}
	var local_id := int(status.get("local_id", _local_player_id()))
	player_names[local_id] = _player_display_name(local_id)
	var remote_peers: Array = status.get("remote_voice_peers", [])
	for peer_data in remote_peers:
		if typeof(peer_data) != TYPE_DICTIONARY:
			continue
		var peer_id := int(peer_data.get("id", -1))
		if peer_id >= 0:
			player_names[peer_id] = _player_display_name(peer_id)
	race_communication_overlay.set_voice_status(status, player_names)
