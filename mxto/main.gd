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
@onready var replays_button: Button = $Control/ReplaysButton
@onready var car_settings_button_lobby: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CarSettingsButton
@onready var controller_settings_button_lobby: Button = $Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/ControllerSettingsButton
@onready var race_finish_label: Label = $RaceFinishLabel
@onready var frame_time_label: Label = $FrameTimeLabel
@onready var rtt_label: Label = $RTTLabel
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
@onready var race_pause_save_replay_button: Button = $RacePauseLayer/RacePauseRoot/Center/Panel/Box/SaveReplayButton
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
const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CustomStampAtlasBuilder = preload("res://vehicle/customization/custom_stamp_atlas_builder.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")
const LobbyChibiCarClass = preload("res://ui/lobby_chibi_car.gd")
const FinishMedalScene: PackedScene = preload("res://ui/finish_medal.tscn")
const KoMedalScene: PackedScene = preload("res://ui/ko_medal.tscn")
const RaceResultsOverlayScene: PackedScene = preload("res://ui/race_results_overlay.tscn")
const BUMPER_DEFINITION_PATH := "res://vehicle/asset/bumper/definition.tres"
const BUMPER_POOL_SIZE := 60
const SPATIAL_AUDIO_SFX := {
	&"active_start": "res://sfx/vehicle/active_start.wav",
	&"air_0": "res://sfx/vehicle/air_0.wav",
	&"air_1": "res://sfx/vehicle/air_1.wav",
	&"brake": "res://sfx/vehicle/brake.wav",
	&"collision_light": "res://sfx/vehicle/collision_light.wav",
	&"collision_light_secondary": "res://sfx/vehicle/collision/PACK1-152.wav",
	&"collision_medium": "res://sfx/vehicle/collision_medium.wav",
	&"collision_hard": "res://sfx/vehicle/collision_hard.wav",
	&"collision_heavy": "res://sfx/vehicle/collision_heavy.wav",
	&"dash_plate": "res://sfx/vehicle/dash_plate.wav",
	&"dash_plate_secondary": "res://sfx/vehicle/boost/PACK1-245.wav",
	&"energy_restore": "res://sfx/vehicle/energy_restore.wav",
	&"gx_engine_185": "res://sfx/vehicle/thrust/PACK1-185.wav",
	&"gx_engine_186": "res://sfx/vehicle/thrust/PACK1-186.wav",
	&"gx_engine_187": "res://sfx/vehicle/thrust/PACK1-187.wav",
	&"gx_engine_188": "res://sfx/vehicle/thrust/PACK1-188.wav",
	&"gx_engine_189": "res://sfx/vehicle/thrust/PACK1-189.wav",
	&"gx_engine_190": "res://sfx/vehicle/thrust/PACK1-190.wav",
	&"gx_engine_236": "res://sfx/vehicle/thrust/PACK1-236.wav",
	&"gx_engine_237": "res://sfx/vehicle/thrust/PACK1-237.wav",
	&"gx_engine_240": "res://sfx/vehicle/thrust/PACK1-240.wav",
	&"gx_engine_250": "res://sfx/vehicle/thrust/PACK1-250.wav",
	&"gx_engine_251": "res://sfx/vehicle/thrust/PACK1-251.wav",
	&"jump_plate": "res://sfx/vehicle/boost/PACK1-242.wav",
	&"landing": "res://sfx/vehicle/landing.wav",
	&"landing_b10": "res://sfx/vehicle/landing_b10.wav",
	&"spinattack": "res://sfx/vehicle/spinattack.wav",
	&"mine": "res://sfx/vehicle/mine.wav",
	&"sideattack": "res://sfx/vehicle/sideattack.wav",
	&"strafe": "res://sfx/vehicle/strafe.wav",
	&"suspension_contact": "res://sfx/vehicle/suspension_contact.wav",
	&"drift_loop": "res://sfx/vehicle/thrust/PACK1-191.wav",
	&"terrain_dirt": "res://sfx/vehicle/terrain_dirt.wav",
	&"terrain_lava": "res://sfx/vehicle/thrust/PACK1-197.wav",
	&"terrain_lava_secondary": "res://sfx/vehicle/thrust/PACK1-196.wav",
	&"thrust_on": "res://sfx/vehicle/thrust_on.wav",
	&"zero_hp": "res://sfx/vehicle/zero_hp.wav",
	&"announcer_boost_power": "res://sfx/announcer/boost_power.wav",
	&"announcer_final_lap": "res://sfx/announcer/final_lap.wav",
	&"announcer_go": "res://sfx/announcer/go.wav",
	&"announcer_one": "res://sfx/announcer/one.wav",
	&"announcer_three": "res://sfx/announcer/three.wav",
	&"announcer_two": "res://sfx/announcer/two.wav",
}
const RACE_BOOST_POWER_LAP_INDEX := 2
const RACE_FINAL_LAP_INDEX := 3
const RACE_MUSIC_START_LEAD_TICKS := 360
const RACE_FINISH_MUSIC_FADE_SECONDS := 0.5
const RACE_RESULTS_MUSIC_DELAY_SECONDS := 2.0
const RACE_RESULTS_MUSIC_INTRO := "res://content/base/music/raceresults_intro.ogg"
const RACE_RESULTS_MUSIC_LOOP := "res://content/base/music/raceresults_loop.ogg"
const RACE_FINISH_WHOOSH_STREAM := preload("res://sfx/whoosh.wav")
const RACE_FINISH_SFX_DUCK_BUS := &"SFX"
const RACE_FINISH_SFX_DUCK_DB := -8.0
const RACE_FINISH_SFX_DUCK_FADE_SECONDS := 1.0

var tracks: Array = []
var car_definitions: Array = []
var players: Array = []
var player_scene := preload("res://player/player_controller.tscn")
var spectator_scene := preload("res://player/spectator.tscn")
var local_player_index: int = 0
var headless_mode: bool = false
var trigger_objects: Array = []
var track_visual_scene_instance: Node
var spectator_node: Node3D
var spatial_audio: Node
var ui_sfx_player: AudioStreamPlayer
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
var auto_replay_catalog_profile_mode: bool = false
var auto_quit_after_frames: int = -1
var current_track_meta: Dictionary = {}
var current_track_ground_image: Image
var car_render_manager: CarRenderManager
var lobby_chibi_render_manager: CarRenderManager
var lobby_chibi_render_signature := ""
var debug_replay_recording: bool = false
var debug_replay_playback: bool = false
var debug_replay_inputs: Array = []
var debug_replay_snapshot_tick: int = -1
var debug_replay_snapshot_state: PackedByteArray = PackedByteArray()
var debug_replay_playback_inputs: Array = []
var debug_replay_playback_index: int = 0
var debug_replay_autoload_path: String = ""
var debug_replay_loaded_path: String = ""
var replay_autoload_path: String = ""
var replay_recording_active: bool = false
var replay_recording_saved: bool = false
var replay_recording_source: String = ""
var replay_recording_metadata: Dictionary = {}
var replay_recording_racer_ids: Array = []
var replay_recording_cpu_flags: Array = []
var replay_recording_frames: Array = []
var replay_start_grid_slots: PackedInt32Array = PackedInt32Array()
var replay_playback_active: bool = false
var replay_playback_frames: Array = []
var replay_playback_index: int = 0
var replay_playback_loaded_path: String = ""
var replay_playback_focus_index: int = 0
var replay_playback_racer_ids: Array = []
var replay_playback_cpu_flags: Array = []
var replay_playback_local_player_id: int = 0
var replay_playback_use_multiplayer_startup: bool = false
var replay_strict_verify_requested: bool = false
var replay_skip_seek_bake_requested: bool = false
var replay_load_profile_requested: bool = false
var replay_playback_use_singleplayer_tick: bool = false
var replay_saved_finish_times: Dictionary = {}
var replay_saved_finish_placements: Dictionary = {}
var replay_saved_eliminations: Dictionary = {}
var replay_playback_paused: bool = false
var replay_playback_rate: float = 1.0
var replay_seek_checkpoints: Array = []
var replay_seeking_active: bool = false
var replay_camera_mode: int = 0
var replay_auto_camera: Camera3D
var replay_relative_camera: Camera3D
var replay_relative_gravity_basis := Basis.IDENTITY
var replay_relative_gravity_basis_valid := false
var replay_relative_camera_basis := Basis.IDENTITY
var replay_relative_camera_basis_desired := Basis.IDENTITY
var replay_relative_offset := Vector3.ZERO
var replay_relative_velocity := Vector3.ZERO
var replay_relative_pending_look_delta := Vector2.ZERO
var replay_input_calib: InputCalibration
var replay_catalog_root: Control
var replay_catalog_list: ItemList
var replay_catalog_metadata_label: RichTextLabel
var replay_catalog_name_edit: LineEdit
var replay_catalog_watch_button: Button
var replay_catalog_rename_button: Button
var replay_catalog_delete_button: Button
var replay_catalog_entries: Array = []
var replay_timeline_root: Control
var replay_timeline_panel: PanelContainer
var replay_timeline_track: ColorRect
var replay_timeline_fill: ColorRect
var replay_timeline_playhead: ColorRect
var replay_timeline_marker_layer: Control
var replay_timeline_time_label: Label
var replay_timeline_rate_label: Label
var replay_timeline_play_button: Button
var replay_timeline_focus_prev_button: Button
var replay_timeline_focus_next_button: Button
var replay_timeline_markers: Dictionary = {}
var replay_marker_last_laps: Dictionary = {}
var replay_marker_last_places: Dictionary = {}
var replay_collecting_timeline_markers: bool = false
var replay_timeline_markers_dirty: bool = true
var replay_timeline_marker_last_focus: int = -999999
var replay_timeline_marker_last_size := Vector2(-1.0, -1.0)
var race_audio_last_tick: int = -1
var race_audio_last_local_lap: int = -1
var race_audio_boost_power_announced: bool = false
var race_audio_final_lap_requested: bool = false
var race_audio_waiting_music_start: bool = false
var race_audio_pending_music_wait_for_race_start: bool = false
var race_audio_pending_music: Dictionary = {}
var race_finish_audio_started: bool = false
var race_finish_audio_generation: int = 0
var race_finish_sfx_duck_active: bool = false
var race_finish_sfx_duck_releasing: bool = false
var race_finish_sfx_duck_current_db: float = 0.0
var race_finish_sfx_duck_target_db: float = 0.0
var race_finish_sfx_duck_base_volume_db: float = 0.0
var singleplayer_options_root: Control
var singleplayer_options_restore_toggle: CheckBox
var singleplayer_options_bumpers_toggle: CheckBox
var singleplayer_options_s_boost_toggle: CheckBox
var singleplayer_options_as_spectator := false
var _last_race_track_index: int = -1
var _last_race_settings: Array = []

const DEBUG_REPLAY_VERSION := 1
const REPLAY_SCHEMA_VERSION := 3
const REPLAY_CAMERA_GAME := 0
const REPLAY_CAMERA_AUTO := 1
const REPLAY_CAMERA_SPECTATOR := 2
const REPLAY_CAMERA_RELATIVE := 3
const REPLAY_RELATIVE_DEFAULT_OFFSET := Vector3(0.0, 8.0, 28.0)
const REPLAY_RELATIVE_LOOK_TARGET := Vector3(0.0, 2.0, 0.0)
const REPLAY_RELATIVE_LOOK_SPEED := 0.0025
const REPLAY_RELATIVE_LOOK_ACTION_SPEED := 6.0
const REPLAY_RELATIVE_ROLL_SPEED := 4.0
const REPLAY_RELATIVE_MOVE_SPEED := 300.0
const REPLAY_RELATIVE_FAST_MOVE_SPEED := 900.0
const REPLAY_SEEK_CHECKPOINT_INTERVAL := 1800
const DIP_TRACE_RAIL_SAMPLING := 0x40
const DIP_TRACE_PIPE_FLOOR := 0x100
const DIP_TRACE_MESH_FLOOR := 0x1000

var race_pause_open := false
var debug_rail_trace_requested := false
var active_stickers := {}
var race_notification_hide_msec := 0
var race_medals: Array[Control] = []
var race_results_overlay: RaceResultsOverlay
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
	_setup_spatial_audio()
	_setup_ui_audio()
	race_results_overlay = RaceResultsOverlayScene.instantiate() as RaceResultsOverlay
	add_child(race_results_overlay)
	lobby_chibi_render_manager = CarRenderManagerClass.new()
	lobby_chibi_render_manager.name = "LobbyChibiRenderManager"
	if lobby_chibi_root != null:
		lobby_chibi_root.add_child(lobby_chibi_render_manager)
	randomize()
	_build_lobby_options_controls()
	_build_multiplayer_connect_box()
	_build_singleplayer_race_options_screen()
	_build_replay_timeline_controls()
	_load_tracks()
	_load_car_definitions()
	if car_settings != null and car_settings.has_method("refresh_after_game_manager_loaded"):
		car_settings.call("refresh_after_game_manager_loaded")
	network_manager.race_started.connect(_on_network_race_started)
	network_manager.race_finished.connect(_on_network_race_finished)
	network_manager.race_event.connect(_on_race_event)
	network_manager.race_options_changed.connect(_on_network_race_options_changed)
	network_manager.authoritative_server_frame.connect(_on_authoritative_server_frame)
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
	replay_input_calib = InputCalibration.load_from_disk()
	if !track_editor_button.pressed.is_connected(_on_track_editor_button_pressed):
		track_editor_button.pressed.connect(_on_track_editor_button_pressed)
	if replays_button != null and !replays_button.pressed.is_connected(_open_replay_catalog):
		replays_button.pressed.connect(_open_replay_catalog)
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
	auto_replay_catalog_profile_mode = args.has("--profile-replay-catalog") or user_args.has("--profile-replay-catalog")
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
	var real_replay_idx := args.find("--replay")
	var real_replay_args := args
	if real_replay_idx == -1:
		real_replay_idx = user_args.find("--replay")
		real_replay_args = user_args
	if real_replay_idx != -1 and real_replay_idx + 1 < real_replay_args.size():
		replay_autoload_path = String(real_replay_args[real_replay_idx + 1])
	replay_strict_verify_requested = args.has("--strict-replay-verify") or user_args.has("--strict-replay-verify")
	replay_skip_seek_bake_requested = args.has("--skip-replay-seek-bake") or user_args.has("--skip-replay-seek-bake")
	replay_load_profile_requested = args.has("--profile-replay-load") or user_args.has("--profile-replay-load")
	debug_rail_trace_requested = args.has("--debug-rail-trace") or user_args.has("--debug-rail-trace")
	if debug_rail_trace_requested:
		game_sim.set_dip_switch_enabled(DIP_TRACE_RAIL_SAMPLING, true)
		server_game_sim.set_dip_switch_enabled(DIP_TRACE_RAIL_SAMPLING, true)
	if args.has("--debug-mesh-floor-trace") or user_args.has("--debug-mesh-floor-trace"):
		game_sim.set_dip_switch_enabled(DIP_TRACE_MESH_FLOOR, true)
		server_game_sim.set_dip_switch_enabled(DIP_TRACE_MESH_FLOOR, true)
	if replay_autoload_path != "":
		call_deferred("_start_replay_playback_from_path", replay_autoload_path)
	elif debug_replay_autoload_path != "":
		call_deferred("_load_and_start_debug_replay", debug_replay_autoload_path)
	elif auto_replay_catalog_profile_mode:
		call_deferred("_profile_replay_catalog_and_quit")
	elif auto_track_editor_mode:
		call_deferred("_on_track_editor_button_pressed")
	elif auto_singleplayer_mode:
		call_deferred("_on_singleplayer_button_pressed")
	if headless_mode and !auto_host_mode and !auto_track_editor_mode and !auto_singleplayer_mode and !auto_replay_catalog_profile_mode and debug_replay_autoload_path == "" and replay_autoload_path == "":
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

func _setup_spatial_audio() -> void:
	if DisplayServer.get_name() == "headless":
		return
	if spatial_audio != null or !ClassDB.class_exists("MxtSpatialAudioManager"):
		return
	var audio_node := ClassDB.instantiate("MxtSpatialAudioManager") as Node
	if audio_node == null:
		return
	audio_node.name = "SpatialAudioManager"
	$GameWorld.add_child(audio_node)
	spatial_audio = audio_node
	spatial_audio.call("configure", 30, 16, 16, 8)
	spatial_audio.call("set_audio_bus", &"SFX")
	spatial_audio.call("set_remote_vehicle_audio_bus", &"RemoteVehicleSFX")
	spatial_audio.call("set_music_bus", &"Music")
	spatial_audio.call("set_announcer_bus", &"Announcer")
	spatial_audio.call("set_reassignment_fade_seconds", 0.05)
	spatial_audio.call("set_max_announcer_queue", 16)
	for sfx_id in SPATIAL_AUDIO_SFX.keys():
		spatial_audio.call("register_sfx", sfx_id, SPATIAL_AUDIO_SFX[sfx_id])
	if game_sim != null and game_sim.has_method("set_spatial_audio_manager"):
		game_sim.call("set_spatial_audio_manager", spatial_audio)

func _setup_ui_audio() -> void:
	if DisplayServer.get_name() == "headless" or ui_sfx_player != null:
		return
	ui_sfx_player = AudioStreamPlayer.new()
	ui_sfx_player.name = "UiSfxPlayer"
	ui_sfx_player.bus = &"SFX"
	add_child(ui_sfx_player)

func _configure_vehicle_audio_properties(definitions: Array) -> void:
	if spatial_audio == null or !spatial_audio.has_method("set_vehicle_manual_boost_stream"):
		return
	for i in definitions.size():
		var definition := definitions[i] as CarDefinition
		var boost_sfx: AudioStream = null
		if definition != null:
			boost_sfx = definition.manual_boost_sfx
		spatial_audio.call("set_vehicle_manual_boost_stream", i, boost_sfx)

func _play_vehicle_oneshot_sfx(car_index: int, sfx_id: StringName, volume_db: float = 0.0, pitch_scale: float = 1.0) -> bool:
	if game_sim == null or !game_sim.has_method("play_car_oneshot_sfx"):
		return false
	return bool(game_sim.call("play_car_oneshot_sfx", car_index, sfx_id, volume_db, pitch_scale))

func _play_world_oneshot_sfx(position: Vector3, sfx_id: StringName, volume_db: float = 0.0, pitch_scale: float = 1.0) -> bool:
	if game_sim == null or !game_sim.has_method("play_world_oneshot_sfx"):
		return false
	return bool(game_sim.call("play_world_oneshot_sfx", position, sfx_id, volume_db, pitch_scale))

func _queue_announcer_sfx(sfx_id: StringName, volume_db: float = 0.0, pitch_scale: float = 1.0) -> bool:
	if spatial_audio == null or !spatial_audio.has_method("queue_announcer"):
		return false
	return bool(spatial_audio.call("queue_announcer", sfx_id, volume_db, pitch_scale))

func _play_ui_sfx(stream: AudioStream, volume_db: float = 0.0, pitch_scale: float = 1.0) -> void:
	if stream == null or DisplayServer.get_name() == "headless":
		return
	if ui_sfx_player == null:
		_setup_ui_audio()
	if ui_sfx_player == null:
		return
	ui_sfx_player.stream = stream
	ui_sfx_player.volume_db = volume_db
	ui_sfx_player.pitch_scale = pitch_scale
	ui_sfx_player.play()

func _race_finish_sfx_duck_bus_index() -> int:
	return AudioServer.get_bus_index(RACE_FINISH_SFX_DUCK_BUS)

func _apply_race_finish_sfx_duck() -> void:
	var bus_index := _race_finish_sfx_duck_bus_index()
	if bus_index < 0:
		return
	AudioServer.set_bus_volume_db(bus_index, race_finish_sfx_duck_base_volume_db + race_finish_sfx_duck_current_db)

func _begin_race_finish_sfx_duck() -> void:
	var bus_index := _race_finish_sfx_duck_bus_index()
	if bus_index < 0:
		return
	if !race_finish_sfx_duck_active:
		race_finish_sfx_duck_current_db = 0.0
		race_finish_sfx_duck_base_volume_db = AudioServer.get_bus_volume_db(bus_index)
	else:
		race_finish_sfx_duck_base_volume_db = AudioServer.get_bus_volume_db(bus_index) - race_finish_sfx_duck_current_db
	race_finish_sfx_duck_target_db = RACE_FINISH_SFX_DUCK_DB
	race_finish_sfx_duck_active = true
	race_finish_sfx_duck_releasing = false
	_apply_race_finish_sfx_duck()

func _release_race_finish_sfx_duck() -> void:
	if !race_finish_sfx_duck_active:
		return
	race_finish_sfx_duck_target_db = 0.0
	race_finish_sfx_duck_releasing = true

func _update_race_finish_sfx_duck(delta: float) -> void:
	if !race_finish_sfx_duck_active:
		return
	var step := 0.0
	if RACE_FINISH_SFX_DUCK_FADE_SECONDS > 0.0:
		step = absf(RACE_FINISH_SFX_DUCK_DB) * maxf(delta, 0.0) / RACE_FINISH_SFX_DUCK_FADE_SECONDS
	else:
		step = absf(race_finish_sfx_duck_target_db - race_finish_sfx_duck_current_db)
	race_finish_sfx_duck_current_db = move_toward(race_finish_sfx_duck_current_db, race_finish_sfx_duck_target_db, step)
	_apply_race_finish_sfx_duck()
	if race_finish_sfx_duck_releasing and is_equal_approx(race_finish_sfx_duck_current_db, 0.0):
		var bus_index := _race_finish_sfx_duck_bus_index()
		if bus_index >= 0:
			AudioServer.set_bus_volume_db(bus_index, race_finish_sfx_duck_base_volume_db)
		race_finish_sfx_duck_active = false
		race_finish_sfx_duck_releasing = false
		race_finish_sfx_duck_current_db = 0.0
		race_finish_sfx_duck_target_db = 0.0

func _cancel_race_finish_audio(stop_ui_sfx: bool = false) -> void:
	race_finish_audio_generation += 1
	race_finish_audio_started = false
	_release_race_finish_sfx_duck()
	if stop_ui_sfx and ui_sfx_player != null:
		ui_sfx_player.stop()

func _begin_local_race_finish_audio() -> void:
	if race_finish_audio_started or replay_playback_active:
		return
	race_finish_audio_started = true
	var generation := race_finish_audio_generation
	_play_ui_sfx(RACE_FINISH_WHOOSH_STREAM, 10.0)
	_begin_race_finish_sfx_duck()
	stop_music(RACE_FINISH_MUSIC_FADE_SECONDS)
	_play_race_results_music_after_delay(generation)

func _play_race_results_music_after_delay(generation: int) -> void:
	await get_tree().create_timer(RACE_RESULTS_MUSIC_DELAY_SECONDS).timeout
	if generation != race_finish_audio_generation or !race_finish_audio_started:
		return
	if replay_playback_active or game_sim == null or !game_sim.sim_started:
		return
	_play_music_from_definition({
		"loop": RACE_RESULTS_MUSIC_LOOP,
		"intro": RACE_RESULTS_MUSIC_INTRO,
		"final_loop": "",
		"final_intro": "",
		"final_lap_timestamps": [],
	})

func _reset_race_audio_state() -> void:
	_cancel_race_finish_audio(true)
	race_audio_last_tick = -1
	race_audio_last_local_lap = -1
	race_audio_boost_power_announced = false
	race_audio_final_lap_requested = false
	race_audio_waiting_music_start = false
	race_audio_pending_music_wait_for_race_start = false
	race_audio_pending_music.clear()
	if spatial_audio != null:
		if spatial_audio.has_method("clear_announcer_queue"):
			spatial_audio.call("clear_announcer_queue")
		if spatial_audio.has_method("stop_music"):
			spatial_audio.call("stop_music")

func _resolve_track_audio_path(track_dir: String, path_value) -> String:
	var path := str(path_value)
	if path.is_empty():
		return ""
	if path.begins_with("res://") or path.begins_with("user://"):
		return path
	return track_dir.path_join(path)

func _play_music_from_definition(definition: Dictionary) -> bool:
	if spatial_audio == null or !spatial_audio.has_method("play_music_paths"):
		return false
	var timestamps := PackedFloat32Array()
	var raw_timestamps = definition.get("final_lap_timestamps", [])
	if typeof(raw_timestamps) == TYPE_PACKED_FLOAT32_ARRAY:
		timestamps = raw_timestamps
	elif typeof(raw_timestamps) == TYPE_ARRAY:
		timestamps = PackedFloat32Array(raw_timestamps)
	return bool(spatial_audio.call(
		"play_music_paths",
		str(definition.get("loop", "")),
		str(definition.get("intro", "")),
		str(definition.get("final_loop", "")),
		str(definition.get("final_intro", "")),
		timestamps))

func _audio_stream_resource_path(stream_value) -> String:
	if stream_value is Resource:
		return str(stream_value.resource_path)
	return ""

func _music_definition_from_resource(resource_path: String) -> Dictionary:
	if resource_path.is_empty() or !ResourceLoader.exists(resource_path):
		return {}
	var music_res := load(resource_path)
	if music_res == null:
		return {}
	return {
		"loop": _audio_stream_resource_path(music_res.get("musicLoop")),
		"intro": _audio_stream_resource_path(music_res.get("musicIntro")),
		"final_loop": _audio_stream_resource_path(music_res.get("musicLoopFinalLap")),
		"final_intro": _audio_stream_resource_path(music_res.get("musicIntroFinalLap")),
		"final_lap_timestamps": music_res.get("finalLapTimeStamps"),
		"wait_for_race_start": bool(music_res.get("wait_for_race_start")),
	}

func play_music_resource(resource_path: String) -> bool:
	var definition := _music_definition_from_resource(resource_path)
	if definition.is_empty():
		return false
	return _play_music_from_definition(definition)

func stop_music(fade_seconds: float = 0.0) -> void:
	if spatial_audio != null and spatial_audio.has_method("stop_music"):
		spatial_audio.call("stop_music", fade_seconds)

func _configure_track_music(track_dir: String) -> void:
	race_audio_waiting_music_start = false
	race_audio_pending_music.clear()
	if spatial_audio == null:
		return
	var music_def = current_track_meta.get("music", {})
	if typeof(music_def) == TYPE_STRING:
		music_def = _resolve_track_audio_path(track_dir, music_def)
	if typeof(music_def) == TYPE_STRING_NAME:
		music_def = _resolve_track_audio_path(track_dir, str(music_def))
	if typeof(music_def) == TYPE_STRING and !str(music_def).is_empty():
		music_def = _music_definition_from_resource(str(music_def))
	elif current_track_meta.has("music_resource"):
		music_def = _music_definition_from_resource(_resolve_track_audio_path(track_dir, current_track_meta.get("music_resource", "")))
	if typeof(music_def) != TYPE_DICTIONARY:
		if spatial_audio.has_method("stop_music"):
			spatial_audio.call("stop_music")
		return
	var music: Dictionary = music_def
	if music.has("resource"):
		var resource_def := _music_definition_from_resource(_resolve_track_audio_path(track_dir, music.get("resource", "")))
		if resource_def.is_empty():
			if spatial_audio.has_method("stop_music"):
				spatial_audio.call("stop_music")
			return
		music = resource_def
	var loop_path := _resolve_track_audio_path(track_dir, music.get("loop", ""))
	if loop_path.is_empty():
		if spatial_audio.has_method("stop_music"):
			spatial_audio.call("stop_music")
		return
	var native_def := {
		"loop": loop_path,
		"intro": _resolve_track_audio_path(track_dir, music.get("intro", "")),
		"final_loop": _resolve_track_audio_path(track_dir, music.get("final_loop", "")),
		"final_intro": _resolve_track_audio_path(track_dir, music.get("final_intro", "")),
		"final_lap_timestamps": music.get("final_lap_timestamps", []),
	}
	race_audio_pending_music = native_def
	race_audio_pending_music_wait_for_race_start = bool(music.get("wait_for_race_start", false))
	race_audio_waiting_music_start = true

func _race_audio_focus_player_id() -> int:
	var local_id := _local_player_id()
	if local_id != 0 and network_manager.get_simulation_roster().has(local_id):
		return local_id
	var roster := network_manager.get_simulation_roster()
	if !roster.is_empty():
		return int(roster[0])
	return local_id

func _update_race_audio_events_after_actual_tick() -> void:
	if spatial_audio == null or replay_playback_active or game_sim == null or !game_sim.sim_started:
		return
	var player_id := _race_audio_focus_player_id()
	var current_tick := network_manager.get_race_tick()
	if current_tick < 0:
		return
	var previous_tick := race_audio_last_tick
	race_audio_last_tick = current_tick
	if game_sim.has_method("get_player_level_start_time"):
		var start_tick := int(game_sim.call("get_player_level_start_time", player_id))
		if race_audio_waiting_music_start:
			var music_start_tick := start_tick
			if !race_audio_pending_music_wait_for_race_start:
				music_start_tick = maxi(0, start_tick - RACE_MUSIC_START_LEAD_TICKS)
			if previous_tick < music_start_tick and current_tick >= music_start_tick:
				race_audio_waiting_music_start = false
				race_audio_pending_music_wait_for_race_start = false
				_play_music_from_definition(race_audio_pending_music)
				race_audio_pending_music.clear()
		var countdown_marks := [
			[start_tick - 180, &"announcer_three"],
			[start_tick - 120, &"announcer_two"],
			[start_tick - 60, &"announcer_one"],
			[start_tick, &"announcer_go"],
		]
		for mark in countdown_marks:
			var mark_tick := int(mark[0])
			if previous_tick < mark_tick and current_tick >= mark_tick:
				_queue_announcer_sfx(mark[1])
	if game_sim.has_method("get_player_lap"):
		var lap := int(game_sim.call("get_player_lap", player_id))
		if race_audio_last_local_lap >= 0:
			if !race_audio_boost_power_announced and race_audio_last_local_lap < RACE_BOOST_POWER_LAP_INDEX and lap >= RACE_BOOST_POWER_LAP_INDEX:
				race_audio_boost_power_announced = true
				_queue_announcer_sfx(&"announcer_boost_power")
			if !race_audio_final_lap_requested and race_audio_last_local_lap < RACE_FINAL_LAP_INDEX and lap >= RACE_FINAL_LAP_INDEX:
				race_audio_final_lap_requested = true
				_queue_announcer_sfx(&"announcer_final_lap")
				if spatial_audio.has_method("request_final_lap_music"):
					spatial_audio.call("request_final_lap_music")
		race_audio_last_local_lap = lap

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

func _clear_track_visual_scene() -> void:
	if track_visual_scene_instance != null:
		if is_instance_valid(track_visual_scene_instance):
			track_visual_scene_instance.queue_free()
		track_visual_scene_instance = null

func _resolve_track_visual_scene_path(track_dir: String, meta: Dictionary) -> String:
	var scene_path := String(meta.get("visual_scene", "")).strip_edges()
	if scene_path != "":
		if scene_path.begins_with("res://") or scene_path.begins_with("user://"):
			return scene_path
		return track_dir.path_join(scene_path)
	var default_path := track_dir.path_join("track.tscn")
	if ResourceLoader.exists(default_path):
		return default_path
	return ""

func _load_track_visual_scene(scene_path: String) -> bool:
	if scene_path == "" or !ResourceLoader.exists(scene_path):
		return false
	var packed := load(scene_path) as PackedScene
	if packed == null:
		push_warning("Track visual scene is not a PackedScene: %s" % scene_path)
		return false
	var inst := packed.instantiate()
	if inst == null:
		push_warning("Failed to instantiate track visual scene: %s" % scene_path)
		return false
	track_visual_scene_instance = inst
	obj_container.add_child(inst)
	return true

func _set_builtin_track_visuals_enabled(enabled: bool) -> void:
	if debug_track_mesh_container != null:
		debug_track_mesh_container.visible = enabled and !auto_hide_track_visuals_mode
	if directional_light_3d != null:
		directional_light_3d.visible = enabled
	if world_environment != null:
		world_environment.environment = default_world_environment_resource if enabled else null

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
	network_manager.send_active_custom_stamp_manifest()
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
	options["track_indices"] = [track_selector.selected]
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
	var settings_dict = car_settings.get_player_settings().to_dict()
	var err := network_manager.join(ip_field.text, _multiplayer_lobby_port())
	if err != OK:
		return
	network_manager.multiplayer.connected_to_server.connect(
		_send_connected_player_settings.bind(settings_dict),
		Object.CONNECT_ONE_SHOT)
	start_race_button.disabled = true
	$Control.visible = false
	lobby_control.visible = true

func _send_connected_player_settings(settings_dict: Dictionary) -> void:
	network_manager.send_player_settings(settings_dict)
	network_manager.send_active_custom_stamp_manifest()

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
		"track_indices": [track_selector.selected],
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
	if !race_pause_options_button.pressed.is_connected(_on_pause_options_pressed):
		race_pause_options_button.pressed.connect(_on_pause_options_pressed)
	if race_pause_save_replay_button != null and !race_pause_save_replay_button.pressed.is_connected(_on_pause_save_replay_pressed):
		race_pause_save_replay_button.pressed.connect(_on_pause_save_replay_pressed)
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
	_refresh_race_pause_replay_button()
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

func _on_pause_save_replay_pressed() -> void:
	var saved_path := _save_replay_recording("manual")
	if saved_path != "":
		_show_race_notification("Replay Saved", 2200)
	_refresh_race_pause_replay_button()

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

func _on_lobby_s_boost_toggled(_toggled: bool) -> void:
	_refresh_lobby_race_options()

func _build_lobby_race_options() -> Dictionary:
	var selected_track_indices := []
	for selected_index in lobby_grand_prix_track_sequence:
		selected_track_indices.append(int(selected_index))
	return {
		"game_mode": lobby_game_mode_choice.selected if lobby_game_mode_choice != null else 0,
		"track_indices": selected_track_indices,
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
	start_race_button.disabled = !can_edit_cpu or tracks.is_empty() or lobby_grand_prix_track_sequence.is_empty()

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
	_submit_lobby_chibi_render(roster)

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
	if replay_playback_active:
		return replay_playback_local_player_id
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

func _show_race_results_summary() -> void:
	if replay_playback_active:
		_hide_race_results_summary()
		return
	if race_finish_label != null:
		race_finish_label.visible = false
	if race_results_overlay != null:
		race_results_overlay.set_results(_format_race_results_text(), _format_grand_prix_results_text())
		race_results_overlay.visible = true
	race_notification_hide_msec = 0

func _hide_race_results_summary() -> void:
	if race_results_overlay != null:
		race_results_overlay.visible = false

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
		if actor_id == _local_player_id():
			_begin_local_race_finish_audio()
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
			if replay_collecting_timeline_markers:
				_record_replay_timeline_event_marker(event)
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
		replay_input_calib = InputCalibration.load_from_disk()

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

func _replay_dir() -> String:
	return ProjectSettings.globalize_path("user://replays")

func _replay_make_stamp() -> String:
	return Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")

func _replay_build_signature() -> String:
	var game_version := network_manager.version_string.strip_edges()
	if game_version == "":
		game_version = str(ProjectSettings.get_setting("application/config/name", "Maxx Throttle C++"))
	var engine_version: Dictionary = Engine.get_version_info()
	return "%s|godot:%s|schema:%d" % [game_version, str(engine_version.get("string", "")), REPLAY_SCHEMA_VERSION]

func _replay_track_name() -> String:
	return _debug_replay_track_name()

func _replay_track_path() -> String:
	return _debug_replay_track_path()

func _replay_mode_name() -> String:
	if !singleplayer_mode:
		return "Multiplayer"
	if network_manager.get_cpu_roster().is_empty():
		return "Time Attack"
	return "CPU Race"

func _replay_should_record_current_race() -> bool:
	if replay_playback_active:
		return false
	if singleplayer_mode:
		return true
	return network_manager.is_server

func _start_replay_recording(track_index: int, settings: Array, racer_ids: Array, cpu_flags: Array, start_grid_slots: PackedInt32Array) -> void:
	_stop_replay_recording(false)
	if !_replay_should_record_current_race():
		return
	replay_recording_active = true
	replay_recording_saved = false
	replay_recording_source = "singleplayer" if singleplayer_mode else "server"
	replay_recording_racer_ids = racer_ids.duplicate(true)
	replay_recording_cpu_flags = cpu_flags.duplicate(true)
	replay_recording_frames.clear()
	var start_grid_slot_array := []
	for slot in start_grid_slots:
		start_grid_slot_array.append(int(slot))
	var player_records: Array = []
	for i in range(racer_ids.size()):
		var id := int(racer_ids[i])
		var raw_settings: Dictionary = {}
		if i < settings.size() and typeof(settings[i]) == TYPE_DICTIONARY:
			raw_settings = (settings[i] as Dictionary).duplicate(true)
		elif network_manager.player_settings.has(id) and typeof(network_manager.player_settings[id]) == TYPE_DICTIONARY:
			raw_settings = (network_manager.player_settings[id] as Dictionary).duplicate(true)
		player_records.append({
			"id": id,
			"username": str(raw_settings.get("username", "Player")),
			"cpu": i < cpu_flags.size() and bool(cpu_flags[i]),
			"car_definition_path": str(raw_settings.get("car_definition_path", "")),
			"sticker_1": int(raw_settings.get("sticker_1", 0)),
			"sticker_2": int(raw_settings.get("sticker_2", 1)),
			"sticker_3": int(raw_settings.get("sticker_3", 2)),
			"sticker_4": int(raw_settings.get("sticker_4", 3)),
			"car_livery": raw_settings.get("car_livery", {}).duplicate(true) if typeof(raw_settings.get("car_livery", {})) == TYPE_DICTIONARY else {},
			"settings": raw_settings,
		})
	replay_recording_metadata = {
		"schema_version": REPLAY_SCHEMA_VERSION,
		"build": _replay_build_signature(),
		"created_unix": Time.get_unix_time_from_system(),
		"name": "%s %s" % [_replay_track_name(), _replay_make_stamp()],
		"mode": _replay_mode_name(),
		"source": replay_recording_source,
		"track_index": track_index,
		"track_name": _replay_track_name(),
		"track_mxt": _replay_track_path(),
		"settings": settings.duplicate(true),
		"racer_ids": racer_ids.duplicate(true),
		"cpu_flags": cpu_flags.duplicate(true),
		"start_grid_slots": start_grid_slot_array,
		"players": player_records,
		"spawn_seed": network_manager.spawn_seed,
		"race_options": network_manager.race_options.duplicate(true),
	}
	_refresh_race_pause_replay_button()

func _stop_replay_recording(save_server_replay: bool) -> void:
	if save_server_replay and replay_recording_active and !replay_recording_saved and replay_recording_source == "server":
		_save_replay_recording("auto")
	replay_recording_active = false

func _refresh_race_pause_replay_button() -> void:
	if race_pause_save_replay_button == null:
		return
	var can_save := singleplayer_mode and replay_recording_active and !replay_recording_saved and network_manager.net_race_finish_time != -1
	race_pause_save_replay_button.visible = can_save
	race_pause_save_replay_button.disabled = !can_save

func _encoded_replay_frame(tick: int, frame_inputs: Dictionary) -> Dictionary:
	var encoded := {}
	for id_value in frame_inputs.keys():
		if typeof(frame_inputs[id_value]) != TYPE_PACKED_BYTE_ARRAY:
			continue
		var bytes: PackedByteArray = frame_inputs[id_value]
		encoded[str(int(id_value))] = Marshalls.raw_to_base64(bytes)
	return {"tick": tick, "inputs": encoded}

func _raw_replay_frame(tick: int, frame_inputs: Dictionary) -> Dictionary:
	var copied := {}
	for id_value in frame_inputs.keys():
		if typeof(frame_inputs[id_value]) != TYPE_PACKED_BYTE_ARRAY:
			continue
		var bytes: PackedByteArray = frame_inputs[id_value]
		copied[int(id_value)] = bytes.duplicate()
	return {"tick": tick, "inputs": copied}

func _record_replay_frame(tick: int, frame_inputs: Dictionary) -> void:
	if !replay_recording_active or replay_recording_saved or frame_inputs.is_empty():
		return
	replay_recording_frames.append(_raw_replay_frame(tick, frame_inputs))

func _on_authoritative_server_frame(tick: int, frame_inputs: Dictionary) -> void:
	_record_replay_frame(tick, frame_inputs)

func _build_singleplayer_replay_frame(local_input_bytes: PackedByteArray) -> Dictionary:
	var out := {}
	var roster := network_manager.get_simulation_roster()
	var cpu_ids := network_manager.get_cpu_roster()
	var local_id := _local_player_id()
	for id_value in roster:
		var id := int(id_value)
		var input_bytes := network_manager.NEUTRAL_INPUT_BYTES
		if cpu_ids.has(id) and game_sim != null and game_sim.has_method("get_native_cpu_input_for_tick"):
			input_bytes = game_sim.get_native_cpu_input_for_tick(id, _singleplayer_tick)
		elif id == local_id:
			input_bytes = local_input_bytes
		out[id] = input_bytes
	return out

func _save_replay_recording(reason: String) -> String:
	if !replay_recording_active or replay_recording_saved or replay_recording_frames.is_empty():
		return ""
	var replay_dir := _replay_dir()
	var err := DirAccess.make_dir_recursive_absolute(replay_dir)
	if err != OK:
		push_warning("Replay save failed: could not create %s err=%s" % [replay_dir, str(err)])
		return ""
	var replay := replay_recording_metadata.duplicate(true)
	replay["saved_reason"] = reason
	replay["duration_ticks"] = replay_recording_frames.size()
	replay["finish_times"] = network_manager.player_finish_times.duplicate(true)
	replay["finish_placements"] = network_manager.player_finish_placements.duplicate(true)
	replay["eliminations"] = network_manager.player_eliminations.duplicate(true)
	var encoded_frames: Array = []
	for raw_frame in replay_recording_frames:
		if typeof(raw_frame) != TYPE_DICTIONARY:
			continue
		var frame_dict: Dictionary = raw_frame
		var raw_inputs = frame_dict.get("inputs", {})
		if typeof(raw_inputs) != TYPE_DICTIONARY:
			continue
		encoded_frames.append(_encoded_replay_frame(int(frame_dict.get("tick", encoded_frames.size())), raw_inputs as Dictionary))
	replay["frames"] = encoded_frames
	var safe_track := str(replay.get("track_name", "track")).replace("/", "_").replace("\\", "_").replace(" ", "_")
	var path := replay_dir.path_join("mxt_%s_%s.replay.json" % [safe_track, _replay_make_stamp()])
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_warning("Replay save failed: %s" % str(FileAccess.get_open_error()))
		return ""
	file.store_string(JSON.stringify(replay, "\t"))
	file.close()
	replay_recording_saved = true
	replay_recording_active = false
	print("MXT_REPLAY saved ", path, " frames=", replay_recording_frames.size())
	return path

func _load_replay_file(path: String) -> Dictionary:
	if !FileAccess.file_exists(path):
		push_warning("Replay load failed: file not found: %s" % path)
		return {}
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(parsed) != TYPE_DICTIONARY:
		push_warning("Replay load failed: JSON root is not a dictionary.")
		return {}
	if int(parsed.get("schema_version", -1)) != REPLAY_SCHEMA_VERSION:
		push_warning("Replay load refused: schema mismatch.")
		return {}
	if str(parsed.get("build", "")) != _replay_build_signature():
		push_warning("Replay load refused: build mismatch.")
		return {}
	return parsed

func _replay_metadata_json_without_frames(text: String) -> String:
	var key_pos := text.find("\n\t\"frames\"")
	if key_pos < 0:
		key_pos = text.find("\"frames\"")
	if key_pos < 0:
		return text
	var colon_pos := text.find(":", key_pos)
	if colon_pos < 0:
		return text
	var array_start := text.find("[", colon_pos)
	if array_start < 0:
		return text
	var array_end := text.find("\n\t]", array_start)
	if array_end < 0:
		return text
	var remove_start := key_pos
	var remove_end := array_end + 3
	if remove_start > 0 and text.substr(remove_start - 1, 1) == ",":
		remove_start -= 1
	elif remove_end < text.length() and text.substr(remove_end, 1) == ",":
		remove_end += 1
	return text.substr(0, remove_start) + text.substr(remove_end)

func _load_replay_metadata_file(path: String) -> Dictionary:
	if path == "" or !FileAccess.file_exists(path):
		return {}
	var text := FileAccess.get_file_as_string(path)
	if text == "":
		return {}
	var metadata_text := _replay_metadata_json_without_frames(text)
	var parsed = JSON.parse_string(metadata_text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return {}
	return parsed as Dictionary

func _replay_find_track_index(data: Dictionary) -> int:
	return _debug_replay_find_track_index(data)

func _decode_replay_frame(frame: Dictionary) -> Dictionary:
	var out := {}
	var raw_inputs = frame.get("inputs", {})
	if typeof(raw_inputs) != TYPE_DICTIONARY:
		return out
	for id_value in (raw_inputs as Dictionary).keys():
		out[int(id_value)] = Marshalls.base64_to_raw(str(raw_inputs[id_value]))
	return out

func _replay_int_dictionary(source: Dictionary) -> Dictionary:
	var out := {}
	for key in source.keys():
		out[int(key)] = int(source[key])
	return out

func _replay_compare_int_dictionary(label: String, expected_raw: Dictionary, actual_raw: Dictionary) -> bool:
	var expected := _replay_int_dictionary(expected_raw)
	var actual := _replay_int_dictionary(actual_raw)
	var ok := true
	for key in expected.keys():
		if !actual.has(key):
			push_warning("Replay verify %s missing id=%d expected=%d" % [label, int(key), int(expected[key])])
			ok = false
		elif int(actual[key]) != int(expected[key]):
			push_warning("Replay verify %s mismatch id=%d expected=%d actual=%d" % [label, int(key), int(expected[key]), int(actual[key])])
			ok = false
	for key in actual.keys():
		if !expected.has(key):
			push_warning("Replay verify %s unexpected id=%d actual=%d" % [label, int(key), int(actual[key])])
			ok = false
	return ok

func _verify_replay_playback_results() -> bool:
	var ok := true
	ok = _replay_compare_int_dictionary("finish_times", replay_saved_finish_times, network_manager.player_finish_times) and ok
	ok = _replay_compare_int_dictionary("finish_placements", replay_saved_finish_placements, network_manager.player_finish_placements) and ok
	ok = _replay_compare_int_dictionary("eliminations", replay_saved_eliminations, network_manager.player_eliminations) and ok
	if !ok and game_sim != null and game_sim.has_method("get_player_debug_string"):
		for id_value in replay_playback_racer_ids:
			print("MXT_REPLAY_VERIFY_STATE tick=", _singleplayer_tick, " ", game_sim.get_player_debug_string(int(id_value)))
	return ok

func _build_replay_timeline_controls() -> void:
	if replay_timeline_root != null and is_instance_valid(replay_timeline_root):
		return
	replay_timeline_root = Control.new()
	replay_timeline_root.name = "ReplayTimeline"
	replay_timeline_root.visible = false
	replay_timeline_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(replay_timeline_root)
	replay_timeline_panel = PanelContainer.new()
	replay_timeline_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	replay_timeline_panel.anchor_left = 0.08
	replay_timeline_panel.anchor_right = 0.92
	replay_timeline_panel.anchor_top = 1.0
	replay_timeline_panel.anchor_bottom = 1.0
	replay_timeline_panel.offset_top = -132.0
	replay_timeline_panel.offset_bottom = -18.0
	replay_timeline_root.add_child(replay_timeline_panel)
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 16)
	margin.add_theme_constant_override("margin_right", 16)
	margin.add_theme_constant_override("margin_top", 10)
	margin.add_theme_constant_override("margin_bottom", 10)
	replay_timeline_panel.add_child(margin)
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 8)
	margin.add_child(rows)
	var focus_controls := HBoxContainer.new()
	focus_controls.add_theme_constant_override("separation", 10)
	rows.add_child(focus_controls)
	replay_timeline_focus_prev_button = Button.new()
	replay_timeline_focus_prev_button.text = "<"
	replay_timeline_focus_prev_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_focus_prev_button.custom_minimum_size = Vector2(180.0, 28.0)
	replay_timeline_focus_prev_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_focus_prev_button.pressed.connect(_on_replay_focus_previous_pressed)
	focus_controls.add_child(replay_timeline_focus_prev_button)
	replay_timeline_focus_next_button = Button.new()
	replay_timeline_focus_next_button.text = ">"
	replay_timeline_focus_next_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_focus_next_button.custom_minimum_size = Vector2(180.0, 28.0)
	replay_timeline_focus_next_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_focus_next_button.pressed.connect(_on_replay_focus_next_pressed)
	focus_controls.add_child(replay_timeline_focus_next_button)
	replay_timeline_track = ColorRect.new()
	replay_timeline_track.color = Color(0.08, 0.09, 0.1, 0.92)
	replay_timeline_track.custom_minimum_size = Vector2(0.0, 18.0)
	replay_timeline_track.mouse_filter = Control.MOUSE_FILTER_STOP
	replay_timeline_track.gui_input.connect(_on_replay_timeline_track_input)
	rows.add_child(replay_timeline_track)
	replay_timeline_fill = ColorRect.new()
	replay_timeline_fill.color = Color(0.95, 0.76, 0.26, 1.0)
	replay_timeline_fill.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_track.add_child(replay_timeline_fill)
	replay_timeline_playhead = ColorRect.new()
	replay_timeline_playhead.color = Color(1.0, 1.0, 1.0, 1.0)
	replay_timeline_playhead.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_track.add_child(replay_timeline_playhead)
	replay_timeline_marker_layer = Control.new()
	replay_timeline_marker_layer.name = "MarkerLayer"
	replay_timeline_marker_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_marker_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	replay_timeline_track.add_child(replay_timeline_marker_layer)
	replay_timeline_track.move_child(replay_timeline_playhead, replay_timeline_track.get_child_count() - 1)
	var controls := HBoxContainer.new()
	controls.add_theme_constant_override("separation", 10)
	rows.add_child(controls)
	replay_timeline_play_button = Button.new()
	replay_timeline_play_button.text = "Pause"
	replay_timeline_play_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_play_button.pressed.connect(_on_replay_timeline_play_pressed)
	controls.add_child(replay_timeline_play_button)
	var slower := Button.new()
	slower.text = "-"
	slower.focus_mode = Control.FOCUS_NONE
	slower.pressed.connect(_on_replay_timeline_slower_pressed)
	controls.add_child(slower)
	replay_timeline_rate_label = Label.new()
	replay_timeline_rate_label.custom_minimum_size.x = 70.0
	replay_timeline_rate_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	controls.add_child(replay_timeline_rate_label)
	var faster := Button.new()
	faster.text = "+"
	faster.focus_mode = Control.FOCUS_NONE
	faster.pressed.connect(_on_replay_timeline_faster_pressed)
	controls.add_child(faster)
	replay_timeline_time_label = Label.new()
	replay_timeline_time_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_time_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	controls.add_child(replay_timeline_time_label)

func _format_replay_timeline_time(tick_value: int) -> String:
	var total_msec := int(round(float(maxi(tick_value, 0)) * 1000.0 / 60.0))
	var minutes := int(total_msec / 60000)
	var seconds := int(total_msec / 1000) % 60
	var milliseconds := total_msec % 1000
	return "%d:%02d.%03d" % [minutes, seconds, milliseconds]

func _replay_marker_bucket(player_id: int) -> Dictionary:
	if !replay_timeline_markers.has(player_id):
		replay_timeline_markers[player_id] = {
			"deaths": [],
			"kos": [],
			"laps": [],
			"finishes": [],
			"first_overtakes": [],
			"place_up": [],
			"place_down": [],
		}
	return replay_timeline_markers[player_id]

func _add_replay_timeline_marker(player_id: int, marker_type: String, tick_value: int) -> void:
	tick_value = clampi(tick_value, 0, maxi(replay_playback_frames.size(), 1))
	var bucket := _replay_marker_bucket(player_id)
	var key := marker_type
	if !bucket.has(key):
		bucket[key] = []
	var markers: Array = bucket[key]
	if !markers.has(tick_value):
		markers.append(tick_value)
		replay_timeline_markers_dirty = true

func _lookup_replay_tick_for_id(source: Dictionary, player_id: int, fallback: int = -1) -> int:
	if source.has(player_id):
		return int(source[player_id])
	var key := str(player_id)
	if source.has(key):
		return int(source[key])
	return fallback

func _initialize_replay_timeline_markers() -> void:
	replay_timeline_markers.clear()
	replay_marker_last_laps.clear()
	replay_marker_last_places.clear()
	replay_timeline_markers_dirty = true
	for id_value in replay_playback_racer_ids:
		var id := int(id_value)
		_replay_marker_bucket(id)
		var finish_tick := _lookup_replay_tick_for_id(replay_saved_finish_times, id)
		if finish_tick >= 0:
			_add_replay_timeline_marker(id, "finishes", finish_tick)
		var death_tick := _lookup_replay_tick_for_id(replay_saved_eliminations, id)
		if death_tick >= 0:
			_add_replay_timeline_marker(id, "deaths", death_tick)

func _record_replay_timeline_event_marker(event: Dictionary) -> void:
	if int(event.get("type", 0)) != 1:
		return
	var tick_value := int(event.get("tick", _singleplayer_tick))
	var attacker_id := int(event.get("actor_id", -1))
	var target_id := int(event.get("target_id", -1))
	if attacker_id >= 0:
		_add_replay_timeline_marker(attacker_id, "kos", tick_value)
	if target_id >= 0:
		_add_replay_timeline_marker(target_id, "deaths", tick_value)

func _update_replay_lap_timeline_markers() -> void:
	if game_sim == null or !game_sim.has_method("get_player_lap"):
		return
	for id_value in replay_playback_racer_ids:
		var id := int(id_value)
		var lap := int(game_sim.get_player_lap(id))
		if !replay_marker_last_laps.has(id):
			replay_marker_last_laps[id] = lap
			continue
		var previous_lap := int(replay_marker_last_laps[id])
		if lap > previous_lap:
			for crossed_lap in range(previous_lap + 1, lap + 1):
				if crossed_lap > 0:
					_add_replay_timeline_marker(id, "laps", _singleplayer_tick)
		replay_marker_last_laps[id] = lap

func _update_replay_placement_timeline_markers() -> void:
	if game_sim == null or !game_sim.has_method("get_player_race_place"):
		return
	for id_value in replay_playback_racer_ids:
		var id := int(id_value)
		var place := int(game_sim.get_player_race_place(id))
		if place <= 0:
			continue
		if !replay_marker_last_places.has(id):
			replay_marker_last_places[id] = place
			continue
		var previous_place := int(replay_marker_last_places[id])
		if previous_place <= 0:
			replay_marker_last_places[id] = place
			continue
		if place < previous_place:
			_add_replay_timeline_marker(id, "place_up", _singleplayer_tick)
			if place == 1:
				_add_replay_timeline_marker(id, "first_overtakes", _singleplayer_tick)
		elif place > previous_place:
			_add_replay_timeline_marker(id, "place_down", _singleplayer_tick)
		replay_marker_last_places[id] = place

func _update_replay_race_state_timeline_markers() -> void:
	_update_replay_lap_timeline_markers()
	_update_replay_placement_timeline_markers()

func _clear_replay_timeline_marker_nodes() -> void:
	if replay_timeline_marker_layer == null:
		return
	for child in replay_timeline_marker_layer.get_children():
		replay_timeline_marker_layer.remove_child(child)
		child.queue_free()
	replay_timeline_marker_last_focus = -999999
	replay_timeline_marker_last_size = Vector2(-1.0, -1.0)

func _reset_replay_timeline_markers() -> void:
	replay_timeline_markers.clear()
	replay_marker_last_laps.clear()
	replay_marker_last_places.clear()
	replay_collecting_timeline_markers = false
	replay_timeline_markers_dirty = true
	_clear_replay_timeline_marker_nodes()

func _timeline_marker_x(tick_value: int) -> float:
	var total_ticks := maxf(float(maxi(replay_playback_frames.size(), 1)), 1.0)
	return replay_timeline_track.size.x * clampf(float(tick_value) / total_ticks, 0.0, 1.0)

func _add_timeline_line_marker(x: float, width: float, height: float, bottom: float, color: Color) -> void:
	var rect := ColorRect.new()
	rect.color = color
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	rect.size = Vector2(width, height)
	rect.position = Vector2(x - width * 0.5, bottom - height)
	replay_timeline_marker_layer.add_child(rect)

func _add_timeline_circle_marker(x: float, radius: float, color: Color) -> void:
	var circle := Polygon2D.new()
	circle.color = color
	var points := PackedVector2Array()
	for i in range(20):
		var angle := TAU * float(i) / 20.0
		points.append(Vector2(cos(angle), sin(angle)) * radius)
	circle.polygon = points
	circle.position = Vector2(x, replay_timeline_track.size.y * 0.5)
	replay_timeline_marker_layer.add_child(circle)

func _add_timeline_flag_marker(x: float, color: Color) -> void:
	var bar_h := replay_timeline_track.size.y
	var line_h := bar_h + 16.0
	_add_timeline_line_marker(x, 3.0, line_h, bar_h, color)
	var flag := Polygon2D.new()
	flag.color = color
	flag.polygon = PackedVector2Array([
		Vector2(0.0, 0.0),
		Vector2(14.0, 5.0),
		Vector2(0.0, 10.0),
	])
	flag.position = Vector2(x + 1.5, bar_h - line_h)
	replay_timeline_marker_layer.add_child(flag)

func _redraw_replay_timeline_markers() -> void:
	if replay_timeline_track == null or replay_timeline_marker_layer == null:
		return
	_clear_replay_timeline_marker_nodes()
	var bucket := _replay_marker_bucket(_focused_replay_player_id())
	var bar_h := replay_timeline_track.size.y
	var circle_radius := maxf(2.0, (bar_h + 2.0) * 0.5)
	for tick_value in bucket.get("place_down", []):
		_add_timeline_line_marker(_timeline_marker_x(int(tick_value)), 1.0, bar_h, bar_h, Color(1.0, 0.48, 0.48, 1.0))
	for tick_value in bucket.get("place_up", []):
		_add_timeline_line_marker(_timeline_marker_x(int(tick_value)), 1.0, bar_h, bar_h, Color(0.56, 1.0, 0.62, 1.0))
	for tick_value in bucket.get("laps", []):
		_add_timeline_flag_marker(_timeline_marker_x(int(tick_value)), Color(0.2, 1.0, 0.28, 1.0))
	for tick_value in bucket.get("finishes", []):
		_add_timeline_flag_marker(_timeline_marker_x(int(tick_value)), Color.WHITE)
	for tick_value in bucket.get("deaths", []):
		_add_timeline_line_marker(_timeline_marker_x(int(tick_value)), 3.0, bar_h + 4.0, bar_h + 2.0, Color(1.0, 0.08, 0.05, 1.0))
	for tick_value in bucket.get("first_overtakes", []):
		_add_timeline_circle_marker(_timeline_marker_x(int(tick_value)), circle_radius, Color(1.0, 0.78, 0.12, 1.0))
	for tick_value in bucket.get("kos", []):
		_add_timeline_circle_marker(_timeline_marker_x(int(tick_value)), circle_radius, Color(1.0, 0.08, 0.05, 1.0))
	replay_timeline_markers_dirty = false
	replay_timeline_marker_last_focus = _focused_replay_player_id()
	replay_timeline_marker_last_size = replay_timeline_track.size

func _update_replay_timeline_marker_nodes() -> void:
	if replay_timeline_track == null or replay_timeline_marker_layer == null:
		return
	var focus_id := _focused_replay_player_id()
	if replay_timeline_markers_dirty or focus_id != replay_timeline_marker_last_focus or replay_timeline_track.size != replay_timeline_marker_last_size:
		_redraw_replay_timeline_markers()

func _set_replay_playback_rate(rate: float) -> void:
	var rates := [0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0]
	var best := 1.0
	var best_delta := INF
	for value in rates:
		var delta := absf(float(value) - rate)
		if delta < best_delta:
			best = float(value)
			best_delta = delta
	replay_playback_rate = best
	_apply_replay_playback_clock()

func _apply_replay_playback_clock() -> void:
	if replay_playback_active and !replay_playback_paused:
		Engine.time_scale = replay_playback_rate
		Engine.physics_ticks_per_second = maxi(1, roundi(60.0 * replay_playback_rate))
	else:
		Engine.time_scale = 1.0
		Engine.physics_ticks_per_second = 60

func _format_replay_playback_rate() -> String:
	if replay_playback_rate >= 1.0:
		return "%dx" % roundi(replay_playback_rate)
	return "%.3fx" % replay_playback_rate

func _on_replay_focus_previous_pressed() -> void:
	_change_replay_focus(-1)

func _on_replay_focus_next_pressed() -> void:
	_change_replay_focus(1)

func _on_replay_timeline_play_pressed() -> void:
	replay_playback_paused = !replay_playback_paused
	_apply_replay_playback_clock()
	_update_replay_timeline_controls()

func _on_replay_timeline_slower_pressed() -> void:
	_set_replay_playback_rate(replay_playback_rate * 0.5)
	_update_replay_timeline_controls()

func _on_replay_timeline_faster_pressed() -> void:
	_set_replay_playback_rate(replay_playback_rate * 2.0)
	_update_replay_timeline_controls()

func _on_replay_timeline_track_input(event: InputEvent) -> void:
	if !replay_playback_active:
		return
	var mouse_event := event as InputEventMouse
	if mouse_event == null:
		return
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index != MOUSE_BUTTON_LEFT or !button.pressed:
			return
	elif !(event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT)):
		return
	var width := maxf(replay_timeline_track.size.x, 1.0)
	var ratio := clampf(mouse_event.position.x / width, 0.0, 1.0)
	_seek_replay_to_tick(roundi(ratio * float(maxi(replay_playback_frames.size(), 1))))
	_update_replay_timeline_controls()
	get_viewport().set_input_as_handled()

func _step_replay_by_ticks(delta_ticks: int) -> void:
	if !replay_playback_active:
		return
	if delta_ticks == 0:
		return
	replay_playback_paused = true
	_apply_replay_playback_clock()
	var target_tick := clampi(_singleplayer_tick + delta_ticks, 0, replay_playback_frames.size())
	if target_tick == _singleplayer_tick:
		_update_replay_timeline_controls()
		return
	if target_tick < _singleplayer_tick:
		_seek_replay_to_tick(target_tick, false)
	else:
		while _singleplayer_tick < target_tick and replay_playback_index < replay_playback_frames.size():
			if !_tick_replay_playback(false):
				break
		_apply_replay_focus_to_local_visual()
		if game_sim.sim_started:
			_update_native_render_camera()
			game_sim.render_gamesim()
			if car_node_container.local_visual_car != null:
				car_node_container.local_visual_car.just_rendered()
	_update_replay_timeline_controls()

func _update_replay_timeline_controls() -> void:
	if replay_timeline_root == null:
		return
	var should_show := false
	if replay_playback_active:
		var mouse_y := get_viewport().get_mouse_position().y
		var viewport_h := get_viewport().get_visible_rect().size.y
		should_show = mouse_y >= viewport_h - 158.0
		if replay_timeline_panel != null:
			should_show = should_show or replay_timeline_panel.get_global_rect().has_point(get_viewport().get_mouse_position())
	replay_timeline_root.visible = should_show
	var total_ticks := maxi(replay_playback_frames.size(), 1)
	var current_tick := clampi(_singleplayer_tick, 0, total_ticks)
	var ratio := float(current_tick) / float(total_ticks)
	if replay_timeline_fill != null and replay_timeline_track != null:
		replay_timeline_fill.position = Vector2.ZERO
		replay_timeline_fill.size = Vector2(replay_timeline_track.size.x * ratio, replay_timeline_track.size.y)
	if replay_timeline_playhead != null and replay_timeline_track != null:
		replay_timeline_playhead.size = Vector2(4.0, replay_timeline_track.size.y + 8.0)
		replay_timeline_playhead.position = Vector2(replay_timeline_track.size.x * ratio - 2.0, -4.0)
	_update_replay_timeline_marker_nodes()
	if replay_timeline_time_label != null:
		replay_timeline_time_label.text = "%s / %s    tick %d / %d" % [
			_format_replay_timeline_time(current_tick),
			_format_replay_timeline_time(total_ticks),
			current_tick,
			total_ticks
		]
	if replay_timeline_rate_label != null:
		replay_timeline_rate_label.text = _format_replay_playback_rate()
	if replay_timeline_play_button != null:
		replay_timeline_play_button.text = "Play" if replay_playback_paused else "Pause"

func _start_replay_playback_from_path(path: String) -> void:
	var profile_start_us := Time.get_ticks_usec()
	var replay := _load_replay_file(path)
	var profile_load_us := Time.get_ticks_usec() - profile_start_us
	if replay.is_empty():
		if headless_mode:
			get_tree().quit(1)
		return
	if game_sim.sim_started or singleplayer_mode:
		_return_to_menu()
	var track_index := _replay_find_track_index(replay)
	if track_index < 0 or track_index >= tracks.size():
		push_warning("Replay load failed: track not found for %s" % str(replay.get("track_name", "")))
		if headless_mode:
			get_tree().quit(1)
		return
	var frames = replay.get("frames", [])
	if typeof(frames) != TYPE_ARRAY or (frames as Array).is_empty():
		push_warning("Replay load failed: replay has no frames.")
		if headless_mode:
			get_tree().quit(1)
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or (settings as Array).is_empty():
		push_warning("Replay load failed: replay has no racer settings.")
		if headless_mode:
			get_tree().quit(1)
		return
	var racer_ids: Array = replay.get("racer_ids", [])
	var cpu_flags: Array = replay.get("cpu_flags", [])
	if racer_ids.is_empty():
		for i in range((settings as Array).size()):
			racer_ids.append(i)
			cpu_flags.append(false)
	var profile_validate_us := Time.get_ticks_usec() - profile_start_us - profile_load_us
	replay_playback_active = true
	replay_playback_frames = frames as Array
	var profile_frames_duplicate_us := Time.get_ticks_usec() - profile_start_us - profile_load_us - profile_validate_us
	replay_playback_index = 0
	replay_playback_loaded_path = path
	replay_playback_racer_ids = racer_ids.duplicate(true)
	replay_playback_cpu_flags = cpu_flags.duplicate(true)
	replay_saved_finish_times = (replay.get("finish_times", {}) as Dictionary).duplicate(true) if typeof(replay.get("finish_times", {})) == TYPE_DICTIONARY else {}
	replay_saved_finish_placements = (replay.get("finish_placements", {}) as Dictionary).duplicate(true) if typeof(replay.get("finish_placements", {})) == TYPE_DICTIONARY else {}
	replay_saved_eliminations = (replay.get("eliminations", {}) as Dictionary).duplicate(true) if typeof(replay.get("eliminations", {})) == TYPE_DICTIONARY else {}
	replay_start_grid_slots = PackedInt32Array()
	var saved_grid_slots = replay.get("start_grid_slots", [])
	if typeof(saved_grid_slots) == TYPE_ARRAY:
		replay_start_grid_slots.resize((saved_grid_slots as Array).size())
		for i in range((saved_grid_slots as Array).size()):
			replay_start_grid_slots[i] = int(saved_grid_slots[i])
	replay_playback_focus_index = 0
	replay_playback_local_player_id = int(replay_playback_racer_ids[0])
	replay_playback_use_multiplayer_startup = str(replay.get("source", "")) == "server" or str(replay.get("mode", "")) == "Multiplayer"
	replay_playback_use_singleplayer_tick = str(replay.get("source", "")) == "singleplayer"
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_seek_checkpoints.clear()
	replay_collecting_timeline_markers = false
	replay_seeking_active = false
	replay_camera_mode = REPLAY_CAMERA_GAME
	singleplayer_mode = true
	_singleplayer_tick = 0
	network_manager.reset_race_state()
	network_manager.set_spawn_seed(int(replay.get("spawn_seed", 0)))
	network_manager.race_options = (replay.get("race_options", {}) as Dictionary).duplicate(true) if typeof(replay.get("race_options", {})) == TYPE_DICTIONARY else {}
	network_manager.player_ids.clear()
	network_manager.cpu_player_ids.clear()
	for i in range(replay_playback_racer_ids.size()):
		var id := int(replay_playback_racer_ids[i])
		var is_cpu := i < replay_playback_cpu_flags.size() and bool(replay_playback_cpu_flags[i])
		if is_cpu:
			network_manager.cpu_player_ids.append(id)
		else:
			network_manager.player_ids.append(id)
		if i < (settings as Array).size() and typeof(settings[i]) == TYPE_DICTIONARY:
			network_manager.player_settings[id] = (settings[i] as Dictionary).duplicate(true)
	var profile_setup_us := Time.get_ticks_usec() - profile_start_us - profile_load_us - profile_validate_us - profile_frames_duplicate_us
	var profile_race_start_us := Time.get_ticks_usec()
	_start_race(track_index, settings as Array)
	profile_race_start_us = Time.get_ticks_usec() - profile_race_start_us
	$Control.visible = false
	lobby_control.visible = false
	if replay_catalog_root != null:
		replay_catalog_root.visible = false
	_apply_replay_focus_to_local_visual()
	var profile_timeline_us := Time.get_ticks_usec()
	_initialize_replay_timeline_markers()
	profile_timeline_us = Time.get_ticks_usec() - profile_timeline_us
	var profile_bake_us := 0
	if !replay_skip_seek_bake_requested:
		var profile_bake_start_us := Time.get_ticks_usec()
		_bake_replay_seek_checkpoints()
		profile_bake_us = Time.get_ticks_usec() - profile_bake_start_us
	else:
		_capture_replay_seek_checkpoint(0)
	_apply_replay_playback_clock()
	_apply_replay_camera_mode()
	if replay_load_profile_requested:
		var total_load_us := Time.get_ticks_usec() - profile_start_us
		print("MXT_REPLAY_LOAD_PROFILE path=", path,
			" total_us=", total_load_us,
			" file_parse_us=", profile_load_us,
			" validate_us=", profile_validate_us,
			" frames_duplicate_us=", profile_frames_duplicate_us,
			" setup_us=", profile_setup_us,
			" race_start_us=", profile_race_start_us,
			" timeline_us=", profile_timeline_us,
			" bake_us=", profile_bake_us,
			" frames=", replay_playback_frames.size(),
			" racers=", replay_playback_racer_ids.size(),
			" skip_bake=", replay_skip_seek_bake_requested)
	print("MXT_REPLAY playback started ", path, " frames=", replay_playback_frames.size())
	if headless_mode:
		var replay_fast_forward_start_us := Time.get_ticks_usec()
		while replay_playback_active and replay_playback_index < replay_playback_frames.size():
			if !_tick_replay_playback(false):
				get_tree().quit(1)
				return
			if replay_strict_verify_requested:
				_check_race_finished()
		var replay_fast_forward_elapsed_us := Time.get_ticks_usec() - replay_fast_forward_start_us
		var replay_frame_count := replay_playback_frames.size()
		print("MXT_REPLAY playback complete ", replay_playback_loaded_path,
			" frames=", replay_frame_count,
			" avg_tick_us=", int(float(replay_fast_forward_elapsed_us) / float(maxi(replay_frame_count, 1))))
		if replay_strict_verify_requested:
			var strict_replay_ok := _verify_replay_playback_results()
			if !strict_replay_ok:
				print("MXT_REPLAY_VERIFY_FAIL path=", replay_playback_loaded_path, " frames=", replay_frame_count)
				get_tree().quit(1)
				return
		print("MXT_REPLAY_VERIFY_OK path=", replay_playback_loaded_path, " frames=", replay_frame_count)
		get_tree().quit()

func _capture_replay_seek_checkpoint(next_tick: int) -> void:
	if game_sim == null or !game_sim.has_method("get_full_state_data"):
		return
	for checkpoint in replay_seek_checkpoints:
		if int((checkpoint as Dictionary).get("tick", -1)) == next_tick:
			return
	var state: PackedByteArray = game_sim.get_full_state_data(next_tick)
	if state.is_empty():
		return
	replay_seek_checkpoints.append({
		"tick": next_tick,
		"index": replay_playback_index,
		"state": state,
		"finish_times": network_manager.player_finish_times.duplicate(true),
		"finish_placements": network_manager.player_finish_placements.duplicate(true),
		"eliminations": network_manager.player_eliminations.duplicate(true),
	})

func _find_replay_seek_checkpoint(target_tick: int) -> Dictionary:
	var best: Dictionary = {}
	var best_tick := -1
	for checkpoint_value in replay_seek_checkpoints:
		if typeof(checkpoint_value) != TYPE_DICTIONARY:
			continue
		var checkpoint: Dictionary = checkpoint_value
		var checkpoint_tick := int(checkpoint.get("tick", -1))
		if checkpoint_tick <= target_tick and checkpoint_tick > best_tick:
			best = checkpoint
			best_tick = checkpoint_tick
	return best

func _restore_replay_race_event_state(checkpoint: Dictionary) -> void:
	network_manager.player_finish_times = (checkpoint.get("finish_times", {}) as Dictionary).duplicate(true)
	network_manager.player_finish_placements = (checkpoint.get("finish_placements", {}) as Dictionary).duplicate(true)
	network_manager.player_eliminations = (checkpoint.get("eliminations", {}) as Dictionary).duplicate(true)
	network_manager._rebuild_finish_order_from_placements()
	network_manager.net_race_finish_time = -1

func _reset_replay_netcode_session() -> void:
	if !replay_playback_active:
		return
	network_manager.netcode_session.configure(
		replay_playback_racer_ids,
		replay_playback_cpu_flags,
		_local_player_id()
	)

func _bake_replay_seek_checkpoints() -> void:
	if game_sim == null or !game_sim.has_method("get_full_state_data") or !game_sim.has_method("load_full_state_data"):
		return
	replay_seeking_active = true
	replay_collecting_timeline_markers = true
	replay_marker_last_laps.clear()
	replay_marker_last_places.clear()
	_update_replay_race_state_timeline_markers()
	_capture_replay_seek_checkpoint(0)
	while replay_playback_index < replay_playback_frames.size():
		if !_tick_replay_playback(false):
			break
		if (_singleplayer_tick % REPLAY_SEEK_CHECKPOINT_INTERVAL) == 0:
			_capture_replay_seek_checkpoint(_singleplayer_tick)
	_capture_replay_seek_checkpoint(_singleplayer_tick)
	replay_collecting_timeline_markers = false
	_seek_replay_to_tick(0, false)
	replay_seeking_active = false

func _seek_replay_to_tick(target_tick: int, show_notice: bool = true) -> bool:
	if !replay_playback_active or game_sim == null or !game_sim.has_method("load_full_state_data"):
		return false
	target_tick = clampi(target_tick, 0, replay_playback_frames.size())
	var checkpoint := _find_replay_seek_checkpoint(target_tick)
	if checkpoint.is_empty():
		return false
	var checkpoint_tick := int(checkpoint.get("tick", 0))
	var state: PackedByteArray = checkpoint.get("state", PackedByteArray())
	if state.is_empty() or !game_sim.load_full_state_data(checkpoint_tick, state):
		push_warning("Replay seek failed: could not load full checkpoint at tick %d" % checkpoint_tick)
		return false
	_restore_replay_race_event_state(checkpoint)
	_reset_replay_netcode_session()
	_singleplayer_tick = checkpoint_tick
	replay_playback_index = int(checkpoint.get("index", checkpoint_tick))
	replay_seeking_active = true
	while _singleplayer_tick < target_tick and replay_playback_index < replay_playback_frames.size():
		if !_tick_replay_playback(false):
			break
	replay_seeking_active = false
	network_manager.clients_server_tick = _singleplayer_tick
	_apply_replay_focus_to_local_visual()
	if game_sim.sim_started:
		_update_native_render_camera()
		game_sim.render_gamesim()
		if car_node_container.local_visual_car != null:
			car_node_container.local_visual_car.just_rendered()
	if show_notice:
		_show_race_notification("Replay: %s" % _format_replay_timeline_time(_singleplayer_tick), 900)
	return true

func _tick_replay_playback(return_to_menu_on_complete: bool = true) -> bool:
	if replay_playback_index >= replay_playback_frames.size():
		if return_to_menu_on_complete:
			print("MXT_REPLAY playback complete ", replay_playback_loaded_path)
			if headless_mode:
				get_tree().quit()
			else:
				_return_to_menu()
		return false
	var raw_frame = replay_playback_frames[replay_playback_index]
	if typeof(raw_frame) != TYPE_DICTIONARY:
		if return_to_menu_on_complete:
			if headless_mode:
				get_tree().quit(1)
			else:
				_return_to_menu()
		return false
	var frame: Dictionary = raw_frame
	var frame_tick := int(frame.get("tick", replay_playback_index))
	if frame_tick != _singleplayer_tick:
		push_warning("Replay playback refused: expected tick %d, found saved tick %d" % [_singleplayer_tick, frame_tick])
		if return_to_menu_on_complete:
			if headless_mode:
				get_tree().quit(1)
			else:
				_return_to_menu()
		return false
	var frame_inputs := _decode_replay_frame(frame)
	if replay_playback_use_singleplayer_tick:
		var local_id := _local_player_id()
		var local_input: PackedByteArray = frame_inputs.get(local_id, network_manager.NEUTRAL_INPUT_BYTES)
		game_sim.tick_singleplayer(local_id, local_input)
	else:
		for id_value in frame_inputs.keys():
			network_manager.netcode_session.store_pending_input(_singleplayer_tick, int(id_value), frame_inputs[id_value])
		if !network_manager.netcode_session.tick_server_frame(game_sim, _singleplayer_tick, true):
			push_warning("Replay playback failed at tick %d" % _singleplayer_tick)
			if return_to_menu_on_complete:
				if headless_mode:
					get_tree().quit(1)
				else:
					_return_to_menu()
			return false
	_consume_authoritative_race_events()
	if replay_collecting_timeline_markers:
		_update_replay_race_state_timeline_markers()
	replay_playback_index += 1
	_singleplayer_tick += 1
	network_manager.clients_server_tick = _singleplayer_tick
	if !replay_seeking_active and (_singleplayer_tick % REPLAY_SEEK_CHECKPOINT_INTERVAL) == 0:
		_capture_replay_seek_checkpoint(_singleplayer_tick)
	return true

func _simulate_replay_playback() -> void:
	if replay_playback_paused:
		return
	_tick_replay_playback(true)

func _ensure_replay_auto_camera() -> Camera3D:
	if replay_auto_camera == null or !is_instance_valid(replay_auto_camera):
		replay_auto_camera = Camera3D.new()
		replay_auto_camera.name = "ReplayAutoCamera"
		replay_auto_camera.near = 0.25
		replay_auto_camera.far = 40000.0
		replay_auto_camera.fov = 70.0
		$GameWorld.add_child(replay_auto_camera)
	return replay_auto_camera

func _ensure_replay_relative_camera() -> Camera3D:
	if replay_relative_camera == null or !is_instance_valid(replay_relative_camera):
		replay_relative_camera = Camera3D.new()
		replay_relative_camera.name = "ReplayRelativeCamera"
		replay_relative_camera.near = 0.25
		replay_relative_camera.far = 40000.0
		replay_relative_camera.fov = 72.0
		$GameWorld.add_child(replay_relative_camera)
	return replay_relative_camera

func _focused_replay_player_id() -> int:
	if replay_playback_racer_ids.is_empty():
		return _local_player_id()
	replay_playback_focus_index = clampi(replay_playback_focus_index, 0, replay_playback_racer_ids.size() - 1)
	return int(replay_playback_racer_ids[replay_playback_focus_index])

func _focused_replay_car() -> VisualCar:
	var focus_id := _focused_replay_player_id()
	if car_node_container.local_visual_car != null and car_node_container.local_visual_car.owning_id == focus_id:
		return car_node_container.local_visual_car
	for car in car_node_container.get_children():
		if car is VisualCar and car.owning_id == focus_id:
			return car
	return null

func _apply_replay_focus_to_local_visual() -> void:
	if !replay_playback_active or car_node_container.local_visual_car == null:
		return
	var focus_id := _focused_replay_player_id()
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
	if !auto_disable_hud_mode and !auto_hide_hud_only_mode:
		car.race_hud.visible = true
	if !auto_disable_hud_mode and !auto_disable_hud_process_only_mode:
		car.race_hud.process_mode = Node.PROCESS_MODE_INHERIT

func _focused_replay_transform() -> Transform3D:
	if game_sim != null and game_sim.has_method("get_player_physical_render_transform"):
		return game_sim.get_player_physical_render_transform(_focused_replay_player_id())
	if game_sim != null and game_sim.has_method("get_player_render_transform"):
		return game_sim.get_player_render_transform(_focused_replay_player_id())
	var car := _focused_replay_car()
	if car != null:
		return Transform3D(car.basis_physical.basis, car.position_current)
	return Transform3D.IDENTITY

func _focused_replay_up() -> Vector3:
	if game_sim != null and game_sim.has_method("get_player_physical_render_up"):
		var native_up: Vector3 = game_sim.get_player_physical_render_up(_focused_replay_player_id())
		if native_up.length_squared() > 0.0001:
			return native_up.normalized()
	var car := _focused_replay_car()
	if car != null and car.track_surface_normal.length_squared() > 0.0001:
		return car.track_surface_normal.normalized()
	var transform := _focused_replay_transform()
	if transform.basis.y.length_squared() > 0.0001:
		return transform.basis.y.normalized()
	return Vector3.UP

func _replay_action_strength(action_name: String) -> float:
	if InputMap.has_action(action_name):
		return Input.get_action_strength(action_name)
	return 0.0

func _replay_action_axis(negative_action: String, positive_action: String) -> float:
	return _replay_action_strength(positive_action) - _replay_action_strength(negative_action)

func _replay_calibrated_strafe_axis() -> float:
	var raw_left := Input.get_action_raw_strength("StrafeLeft")
	var raw_right := Input.get_action_raw_strength("StrafeRight")
	if replay_input_calib == null:
		replay_input_calib = InputCalibration.load_from_disk()
	return replay_input_calib.apply_strafe_right(raw_right) - replay_input_calib.apply_strafe_left(raw_left)

func _replay_relative_gravity_basis_from_up(up: Vector3, preserve_basis: Basis, fallback_basis: Basis) -> Basis:
	if up.length_squared() <= 0.0001:
		up = Vector3.UP
	else:
		up = up.normalized()
	var forward := -preserve_basis.z
	forward = (forward - up * forward.dot(up))
	if forward.length_squared() <= 0.0001:
		forward = -fallback_basis.z
		forward = (forward - up * forward.dot(up))
	if forward.length_squared() <= 0.0001:
		forward = up.cross(fallback_basis.x)
	if forward.length_squared() <= 0.0001:
		var seed := Vector3.FORWARD
		if absf(up.dot(seed)) > 0.95:
			seed = Vector3.RIGHT
		forward = seed - up * seed.dot(up)
	forward = forward.normalized()
	var right := forward.cross(up).normalized()
	forward = up.cross(right).normalized()
	return Basis(right, up, -forward).orthonormalized()

func _apply_replay_relative_camera_transform(car_transform: Transform3D) -> void:
	var camera := _ensure_replay_relative_camera()
	var camera_basis := (replay_relative_gravity_basis * replay_relative_camera_basis).orthonormalized()
	var camera_position := car_transform.origin + replay_relative_gravity_basis * replay_relative_offset
	camera.global_transform = Transform3D(camera_basis, camera_position)

func _reset_replay_relative_camera() -> void:
	replay_relative_gravity_basis_valid = false
	replay_relative_pending_look_delta = Vector2.ZERO
	replay_relative_velocity = Vector3.ZERO
	replay_relative_offset = REPLAY_RELATIVE_DEFAULT_OFFSET
	var camera := _ensure_replay_relative_camera()
	var car_transform := _focused_replay_transform()
	replay_relative_gravity_basis = _replay_relative_gravity_basis_from_up(_focused_replay_up(), car_transform.basis, car_transform.basis)
	replay_relative_gravity_basis_valid = true
	var local_look := Transform3D(Basis.IDENTITY, replay_relative_offset).looking_at(REPLAY_RELATIVE_LOOK_TARGET, Vector3.UP)
	replay_relative_camera_basis_desired = local_look.basis.orthonormalized()
	replay_relative_camera_basis = replay_relative_camera_basis_desired
	_apply_replay_relative_camera_transform(car_transform)
	camera.current = true

func _apply_replay_camera_mode() -> void:
	if !replay_playback_active:
		return
	_apply_replay_focus_to_local_visual()
	var car := _focused_replay_car()
	if replay_camera_mode == REPLAY_CAMERA_GAME and car_node_container.local_visual_car != null:
		if spectator_node != null and spectator_node.has_method("set_input_enabled"):
			spectator_node.call("set_input_enabled", false)
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		game_sim.set_gameplay_camera(car_node_container.local_visual_car.car_camera, _focused_replay_player_id())
		car_node_container.local_visual_car.car_camera.make_current()
		car_node_container.local_visual_car.make_vehicle_audio_listener_current()
	elif replay_camera_mode == REPLAY_CAMERA_AUTO:
		if spectator_node != null and spectator_node.has_method("set_input_enabled"):
			spectator_node.call("set_input_enabled", false)
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_ensure_replay_auto_camera().make_current()
	elif replay_camera_mode == REPLAY_CAMERA_RELATIVE:
		if spectator_node != null and spectator_node.has_method("set_input_enabled"):
			spectator_node.call("set_input_enabled", false)
		_reset_replay_relative_camera()
		_ensure_replay_relative_camera().make_current()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	else:
		if spectator_node == null:
			spectator_node = spectator_scene.instantiate()
			add_child(spectator_node)
		var focus_transform := _focused_replay_transform()
		spectator_node.global_position = focus_transform.origin - focus_transform.basis.z * 32.0 + focus_transform.basis.y * 12.0
		spectator_node.look_at(focus_transform.origin + focus_transform.basis.y * 2.0, focus_transform.basis.y.normalized())
		if spectator_node.has_method("sync_look_from_current_transform"):
			spectator_node.call("sync_look_from_current_transform")
		if spectator_node.has_method("set_input_enabled"):
			spectator_node.call("set_input_enabled", true)
		var camera := spectator_node.get_node_or_null("Camera3D") as Camera3D
		if camera != null:
			camera.make_current()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

func _cycle_replay_camera_mode() -> void:
	replay_camera_mode = (replay_camera_mode + 1) % 4
	_apply_replay_camera_mode()
	_show_race_notification("Replay Camera: %s" % _replay_camera_mode_name(), 1200)

func _replay_camera_mode_name() -> String:
	match replay_camera_mode:
		REPLAY_CAMERA_GAME:
			return "Game"
		REPLAY_CAMERA_AUTO:
			return "Auto"
		REPLAY_CAMERA_RELATIVE:
			return "Relative Cam"
		_:
			return "Spectator"

func _replay_camera_mode_uses_mouse_capture() -> bool:
	return replay_camera_mode == REPLAY_CAMERA_RELATIVE or replay_camera_mode == REPLAY_CAMERA_SPECTATOR

func _change_replay_focus(delta: int) -> void:
	if !replay_playback_active or replay_playback_racer_ids.is_empty():
		return
	if replay_camera_mode != REPLAY_CAMERA_GAME and replay_camera_mode != REPLAY_CAMERA_AUTO and replay_camera_mode != REPLAY_CAMERA_RELATIVE:
		return
	replay_playback_focus_index = posmod(replay_playback_focus_index + delta, replay_playback_racer_ids.size())
	_apply_replay_camera_mode()
	replay_timeline_markers_dirty = true
	_show_race_notification("Replay Focus: %s" % _player_display_name(_focused_replay_player_id()), 1200)

func _update_replay_auto_camera(delta: float) -> void:
	if !replay_playback_active or replay_camera_mode != REPLAY_CAMERA_AUTO:
		return
	var camera := _ensure_replay_auto_camera()
	var car_transform := _focused_replay_transform()
	var speed_scale := 0.5
	var car := _focused_replay_car()
	if car != null:
		speed_scale = clampf(car.speed_kmh / 1800.0, 0.0, 1.0)
	var target := car_transform.origin + car_transform.basis.y * 2.0
	var desired := target - car_transform.basis.z * lerpf(24.0, 42.0, speed_scale) + car_transform.basis.y * lerpf(9.0, 15.0, speed_scale)
	camera.global_position = camera.global_position.lerp(desired, clampf(delta * 4.0, 0.0, 1.0))
	camera.look_at(target, car_transform.basis.y.normalized())

func _update_replay_relative_camera(delta: float) -> void:
	if !replay_playback_active or replay_camera_mode != REPLAY_CAMERA_RELATIVE:
		return
	var car_transform := _focused_replay_transform()
	var desired_gravity_basis := replay_relative_gravity_basis
	if replay_relative_gravity_basis_valid:
		desired_gravity_basis = _replay_relative_gravity_basis_from_up(_focused_replay_up(), replay_relative_gravity_basis, car_transform.basis)
	else:
		desired_gravity_basis = _replay_relative_gravity_basis_from_up(_focused_replay_up(), car_transform.basis, car_transform.basis)
		replay_relative_gravity_basis = desired_gravity_basis
		replay_relative_gravity_basis_valid = true
	replay_relative_gravity_basis = replay_relative_gravity_basis.slerp(desired_gravity_basis, clampf(delta * 5.0, 0.0, 1.0)).orthonormalized()

	var look_delta := replay_relative_pending_look_delta
	replay_relative_pending_look_delta = Vector2.ZERO
	var pitch_amount := -look_delta.y * REPLAY_RELATIVE_LOOK_SPEED
	var yaw_amount := -look_delta.x * REPLAY_RELATIVE_LOOK_SPEED
	pitch_amount += _replay_action_axis("CameraUp", "CameraDown") * delta * -REPLAY_RELATIVE_LOOK_ACTION_SPEED
	pitch_amount += _replay_action_axis("CamForward", "CamBack") * delta * -REPLAY_RELATIVE_LOOK_ACTION_SPEED
	yaw_amount += _replay_action_axis("CameraLeft", "CameraRight") * delta * -REPLAY_RELATIVE_LOOK_ACTION_SPEED
	yaw_amount += _replay_action_axis("CamLeft", "CamRight") * delta * -REPLAY_RELATIVE_LOOK_ACTION_SPEED
	var roll_input := _replay_calibrated_strafe_axis()
	if Input.is_physical_key_pressed(KEY_Q):
		roll_input -= 1.0
	if Input.is_physical_key_pressed(KEY_E):
		roll_input += 1.0
	roll_input = clampf(roll_input, -1.0, 1.0)
	var roll_amount := roll_input * delta * -REPLAY_RELATIVE_ROLL_SPEED
	if pitch_amount != 0.0:
		replay_relative_camera_basis_desired = replay_relative_camera_basis_desired.rotated(replay_relative_camera_basis_desired.x, pitch_amount)
	if yaw_amount != 0.0:
		replay_relative_camera_basis_desired = replay_relative_camera_basis_desired.rotated(replay_relative_camera_basis_desired.y, yaw_amount)
	if roll_amount != 0.0:
		replay_relative_camera_basis_desired = replay_relative_camera_basis_desired.rotated(replay_relative_camera_basis_desired.z, roll_amount)
	replay_relative_camera_basis_desired = replay_relative_camera_basis_desired.orthonormalized()
	replay_relative_camera_basis = replay_relative_camera_basis.slerp(replay_relative_camera_basis_desired, clampf(delta * 8.0, 0.0, 1.0)).orthonormalized()

	var move_input := Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		move_input.z -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		move_input.z += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		move_input.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		move_input.x += 1.0
	if Input.is_physical_key_pressed(KEY_CTRL):
		move_input.y -= 1.0
	move_input.x += _replay_action_axis("MoveLeft", "MoveRight")
	move_input.x += _replay_action_axis("SteerLeft", "SteerRight")
	move_input.z += _replay_action_axis("MoveForward", "MoveBack")
	move_input.z += _replay_action_axis("SteerUp", "SteerDown")
	if move_input.length_squared() > 1.0:
		move_input = move_input.normalized()
	var current_speed := REPLAY_RELATIVE_FAST_MOVE_SPEED if Input.is_physical_key_pressed(KEY_SHIFT) else REPLAY_RELATIVE_MOVE_SPEED
	var desired_velocity := replay_relative_camera_basis * move_input * current_speed
	var velocity_lerp := clampf(delta * (12.0 if move_input.length_squared() > 0.0 else 8.0), 0.0, 1.0)
	replay_relative_velocity = replay_relative_velocity.lerp(desired_velocity, velocity_lerp)
	replay_relative_offset += replay_relative_velocity * delta
	_apply_replay_relative_camera_transform(car_transform)

func _build_replay_catalog() -> void:
	if replay_catalog_root != null and is_instance_valid(replay_catalog_root):
		return
	replay_catalog_root = Control.new()
	replay_catalog_root.name = "ReplayCatalog"
	replay_catalog_root.visible = false
	replay_catalog_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(replay_catalog_root)
	var shade := ColorRect.new()
	shade.color = Color(0.0, 0.0, 0.0, 0.72)
	shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	replay_catalog_root.add_child(shade)
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 48)
	margin.add_theme_constant_override("margin_top", 42)
	margin.add_theme_constant_override("margin_right", 48)
	margin.add_theme_constant_override("margin_bottom", 42)
	replay_catalog_root.add_child(margin)
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 18)
	margin.add_child(columns)
	replay_catalog_list = ItemList.new()
	replay_catalog_list.custom_minimum_size = Vector2(430, 0)
	replay_catalog_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	replay_catalog_list.item_selected.connect(_on_replay_catalog_selected)
	columns.add_child(replay_catalog_list)
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 10)
	columns.add_child(right)
	var title := Label.new()
	title.text = "Replays"
	right.add_child(title)
	replay_catalog_metadata_label = RichTextLabel.new()
	replay_catalog_metadata_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	replay_catalog_metadata_label.bbcode_enabled = false
	right.add_child(replay_catalog_metadata_label)
	replay_catalog_name_edit = LineEdit.new()
	replay_catalog_name_edit.placeholder_text = "Replay name"
	right.add_child(replay_catalog_name_edit)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	right.add_child(buttons)
	replay_catalog_watch_button = Button.new()
	replay_catalog_watch_button.text = "Watch"
	replay_catalog_watch_button.pressed.connect(_on_replay_catalog_watch_pressed)
	buttons.add_child(replay_catalog_watch_button)
	replay_catalog_rename_button = Button.new()
	replay_catalog_rename_button.text = "Rename"
	replay_catalog_rename_button.pressed.connect(_on_replay_catalog_rename_pressed)
	buttons.add_child(replay_catalog_rename_button)
	replay_catalog_delete_button = Button.new()
	replay_catalog_delete_button.text = "Delete"
	replay_catalog_delete_button.pressed.connect(_on_replay_catalog_delete_pressed)
	buttons.add_child(replay_catalog_delete_button)
	var close_button := Button.new()
	close_button.text = "Close"
	close_button.pressed.connect(_close_replay_catalog)
	buttons.add_child(close_button)

func _open_replay_catalog() -> void:
	_build_replay_catalog()
	_refresh_replay_catalog()
	$Control.visible = false
	lobby_control.visible = false
	replay_catalog_root.visible = true
	if replay_catalog_list.item_count > 0:
		replay_catalog_list.select(0)
		_on_replay_catalog_selected(0)

func _profile_replay_catalog_and_quit() -> void:
	_build_replay_catalog()
	var metadata_start := Time.get_ticks_usec()
	_refresh_replay_catalog()
	var metadata_us := Time.get_ticks_usec() - metadata_start
	var full_parse_count := 0
	var full_parse_start := Time.get_ticks_usec()
	var replay_dir := _replay_dir()
	var dir := DirAccess.open(replay_dir)
	if dir != null:
		dir.list_dir_begin()
		var file_name := dir.get_next()
		while file_name != "":
			if !dir.current_is_dir() and file_name.ends_with(".replay.json"):
				var path := replay_dir.path_join(file_name)
				var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
				if typeof(parsed) == TYPE_DICTIONARY:
					full_parse_count += 1
			file_name = dir.get_next()
		dir.list_dir_end()
	var full_parse_us := Time.get_ticks_usec() - full_parse_start
	print("MXT_REPLAY_CATALOG_PROFILE entries=", replay_catalog_entries.size(),
		" metadata_us=", metadata_us,
		" full_parse_entries=", full_parse_count,
		" full_parse_us=", full_parse_us)
	get_tree().quit()

func _close_replay_catalog() -> void:
	if replay_catalog_root != null:
		replay_catalog_root.visible = false
	if !game_sim.sim_started:
		$Control.visible = true

func _refresh_replay_catalog() -> void:
	replay_catalog_entries.clear()
	if replay_catalog_list == null:
		return
	replay_catalog_list.clear()
	var replay_dir := _replay_dir()
	var err := DirAccess.make_dir_recursive_absolute(replay_dir)
	if err != OK:
		return
	var dir := DirAccess.open(replay_dir)
	if dir == null:
		return
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if !dir.current_is_dir() and file_name.ends_with(".replay.json"):
			var path := replay_dir.path_join(file_name)
			var data := _load_replay_metadata_file(path)
			if !data.is_empty():
				data["_path"] = path
				replay_catalog_entries.append(data)
		file_name = dir.get_next()
	dir.list_dir_end()
	replay_catalog_entries.sort_custom(func(a, b): return float(a.get("created_unix", 0.0)) > float(b.get("created_unix", 0.0)))
	for entry in replay_catalog_entries:
		var title := str(entry.get("name", entry.get("track_name", "Replay")))
		replay_catalog_list.add_item(title)
	_update_replay_catalog_buttons()

func _selected_replay_catalog_entry() -> Dictionary:
	if replay_catalog_list == null:
		return {}
	var selected := replay_catalog_list.get_selected_items()
	if selected.is_empty():
		return {}
	var idx := int(selected[0])
	if idx < 0 or idx >= replay_catalog_entries.size():
		return {}
	return replay_catalog_entries[idx]

func _on_replay_catalog_selected(_index: int) -> void:
	var entry := _selected_replay_catalog_entry()
	if entry.is_empty():
		replay_catalog_metadata_label.text = ""
		replay_catalog_name_edit.text = ""
		_update_replay_catalog_buttons()
		return
	replay_catalog_name_edit.text = str(entry.get("name", entry.get("track_name", "Replay")))
	var player_lines: Array = []
	for player in entry.get("players", []):
		if typeof(player) != TYPE_DICTIONARY:
			continue
		var p: Dictionary = player
		var cpu := " CPU" if bool(p.get("cpu", false)) else ""
		var livery: Dictionary = p.get("car_livery", {}) if typeof(p.get("car_livery", {})) == TYPE_DICTIONARY else {}
		var stamp_count := 0
		if typeof(livery.get("stamps", [])) == TYPE_ARRAY:
			stamp_count = (livery.get("stamps", []) as Array).size()
		player_lines.append("%s%s - %s - %d stamps" % [
			str(p.get("username", "Player")),
			cpu,
			str(p.get("car_definition_path", "")),
			stamp_count
		])
	var compatible := int(entry.get("schema_version", -1)) == REPLAY_SCHEMA_VERSION and str(entry.get("build", "")) == _replay_build_signature()
	replay_catalog_metadata_label.text = "\n".join([
		"Track: %s" % str(entry.get("track_name", "")),
		"Mode: %s" % str(entry.get("mode", "")),
		"Duration: %s" % _format_race_time(int(entry.get("duration_ticks", 0)), 0),
		"Players:",
		"\n".join(player_lines),
		"",
		"Compatible: %s" % ("yes" if compatible else "no"),
		str(entry.get("_path", "")),
	])
	_update_replay_catalog_buttons()

func _update_replay_catalog_buttons() -> void:
	var entry := _selected_replay_catalog_entry()
	var has_entry := !entry.is_empty()
	var compatible := has_entry and int(entry.get("schema_version", -1)) == REPLAY_SCHEMA_VERSION and str(entry.get("build", "")) == _replay_build_signature()
	if replay_catalog_watch_button != null:
		replay_catalog_watch_button.disabled = !compatible
	if replay_catalog_rename_button != null:
		replay_catalog_rename_button.disabled = !has_entry
	if replay_catalog_delete_button != null:
		replay_catalog_delete_button.disabled = !has_entry

func _on_replay_catalog_watch_pressed() -> void:
	var entry := _selected_replay_catalog_entry()
	if entry.is_empty():
		return
	_start_replay_playback_from_path(str(entry.get("_path", "")))

func _on_replay_catalog_rename_pressed() -> void:
	var entry := _selected_replay_catalog_entry()
	if entry.is_empty():
		return
	var path := str(entry.get("_path", ""))
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(parsed) != TYPE_DICTIONARY:
		return
	var data: Dictionary = parsed
	data["name"] = replay_catalog_name_edit.text.strip_edges()
	if str(data["name"]) == "":
		data["name"] = str(data.get("track_name", "Replay"))
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(JSON.stringify(data, "\t"))
	file.close()
	_refresh_replay_catalog()

func _on_replay_catalog_delete_pressed() -> void:
	var entry := _selected_replay_catalog_entry()
	if entry.is_empty():
		return
	var path := str(entry.get("_path", ""))
	DirAccess.remove_absolute(path)
	_refresh_replay_catalog()

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
		get_tree().quit(1)

func _load_and_start_debug_replay(path: String) -> void:
	var replay := _load_debug_replay_file(path)
	if replay.is_empty():
		if headless_mode:
			get_tree().quit(1)
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
		var manifest := network_manager.get_custom_stamp_manifest(racer_id)
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
	return network_manager.get_custom_stamp_blob(stamp_hash)

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
	var manifest := network_manager.get_custom_stamp_manifest(player_id)
	if manifest.is_empty():
		return ""
	var parts := []
	for raw_entry in manifest:
		if typeof(raw_entry) != TYPE_DICTIONARY:
			continue
		var entry: Dictionary = raw_entry
		var stamp_hash := str(entry.get("hash", ""))
		var blob = network_manager.get_custom_stamp_blob(stamp_hash)
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

func _start_race(track_index: int, settings: Array) -> void:
	if track_index < 0 or track_index >= tracks.size():
		return
	_close_settings_menus_for_race_start()
	$Control.visible = false
	lobby_control.visible = false
	_last_race_track_index = track_index
	_last_race_settings = settings.duplicate(true)
	active_stickers.clear()
	race_notification_hide_msec = 0
	local_elimination_spectator_active = false
	var info : Dictionary = tracks[track_index]
	# Load track metadata JSON and optional ground texture (ground.png) from the same folder
	current_track_meta = {}
	current_track_ground_image = null
	_hide_race_results_summary()
	_clear_track_visual_scene()
	debug_track_mesh.mesh = null
	obj_container.visible = !auto_hide_track_visuals_mode
	var json_path: String = String(info["mxt"]).get_basename() + ".json"
	var track_dir: String = json_path.get_base_dir()
	_reset_race_audio_state()
	if FileAccess.file_exists(json_path):
		var json_text := FileAccess.get_file_as_string(json_path)
		var parsed = JSON.parse_string(json_text)
		if typeof(parsed) == TYPE_DICTIONARY:
			current_track_meta = parsed
	_configure_track_music(track_dir)
	var visual_scene_path_for_race := _resolve_track_visual_scene_path(track_dir, current_track_meta)
	var has_track_visual_scene := visual_scene_path_for_race != "" and ResourceLoader.exists(visual_scene_path_for_race)
	_set_builtin_track_visuals_enabled(!has_track_visual_scene)
	if !has_track_visual_scene:
		debug_track_mesh.visible = !auto_hide_track_visuals_mode
		track_floor.visible = !auto_hide_track_visuals_mode
		track_clouds.visible = !auto_hide_track_visuals_mode
	if !current_track_meta.is_empty() and world_environment.environment != null:
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
		if !has_track_visual_scene:
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
	var start_grid_slots := replay_start_grid_slots if replay_playback_active and replay_start_grid_slots.size() == racer_ids.size() else _build_start_grid_slots(racer_ids)
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
		game_sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode or replay_playback_use_multiplayer_startup)
	game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
	_configure_vehicle_audio_properties(chosen_defs)
	game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
	_apply_grand_prix_ko_energy_bonuses(game_sim, racer_ids)
	network_manager.netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	_start_replay_recording(track_index, settings, racer_ids, racer_cpu_flags, start_grid_slots)
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
		server_game_sim.set_start_grid_slots(start_grid_slots)
		server_game_sim.set_vehicle_restore_enabled(network_manager.is_vehicle_restore_enabled())
		if server_game_sim.has_method("set_bumpers_enabled"):
			server_game_sim.set_bumpers_enabled(bumpers_enabled and bumper_def != null)
		if server_game_sim.has_method("set_s_boost_enabled"):
			server_game_sim.set_s_boost_enabled(network_manager.is_s_boost_enabled())
		if server_game_sim.has_method("set_multiplayer_intro_camera_enabled"):
			server_game_sim.set_multiplayer_intro_camera_enabled(!singleplayer_mode or replay_playback_use_multiplayer_startup)
		server_game_sim.instantiate_gamesim(level_buffer.duplicate(), car_props.duplicate(true), accel_settings_arr)
		server_game_sim.set_player_metadata(racer_ids, racer_cpu_flags)
		_apply_grand_prix_ko_energy_bonuses(server_game_sim, racer_ids)
		network_manager.server_netcode_session.configure(racer_ids, racer_cpu_flags, _local_player_id())
	network_manager.game_sim = game_sim
	if network_manager.is_server:
		network_manager.server_game_sim = server_game_sim
	if !headless_mode:
		var visual_scene_loaded := _load_track_visual_scene(visual_scene_path_for_race)
		var obj_path: String = String(info["mxt"]).get_basename() + ".obj"
		if !visual_scene_loaded and ResourceLoader.exists(obj_path):
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
			network_manager.client_ready(network_manager.race_netplay_phase)
		else:
			network_manager.client_ready.rpc_id(1, network_manager.race_netplay_phase)

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
		var track_indices: Array = race_options.get("track_indices", [lobby_track_selector.selected])
		var first_track_index := lobby_track_selector.selected
		if !track_indices.is_empty():
			first_track_index = int(track_indices[0])
		network_manager.send_start_race(first_track_index, settings_array, race_options)

func _on_network_race_started(track_index: int, settings: Array) -> void:
	_close_settings_menus_for_race_start()
	if headless_mode:
		_start_race(track_index, settings)
		return
	_start_race(track_index, settings)
	game_sim.set_sim_started(false)
	if network_manager.is_server:
		server_game_sim.set_sim_started(false)

func _on_network_race_finished() -> void:
	if headless_mode and network_manager.pending_next_race_track_index < 0:
		return
	race_finish_label.visible = false
	_hide_race_results_summary()
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
			if replay_playback_active:
				_simulate_replay_playback()
			else:
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
		if !replay_playback_active:
			_consume_authoritative_race_events()
		_update_native_render_camera()
		var profile_render_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		game_sim.render_gamesim()
		_update_race_audio_events_after_actual_tick()
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
	if replay_playback_active:
		_tick_replay_playback()
		network_manager.rollback_frametime_us = Time.get_ticks_usec() - start_time
		return
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
	var tick_to_record := _singleplayer_tick
	game_sim.tick_singleplayer(_local_player_id(), input_bytes)
	if replay_recording_active and game_sim.has_method("get_input_frame_as_dictionary"):
		_record_replay_frame(tick_to_record, game_sim.get_input_frame_as_dictionary(tick_to_record))
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

func _input(event: InputEvent) -> void:
	if replay_playback_active and event is InputEventKey:
		var replay_key := event as InputEventKey
		if replay_key.pressed and !replay_key.echo:
			match replay_key.keycode:
				KEY_LEFT:
					_step_replay_by_ticks(-1)
					get_viewport().set_input_as_handled()
					return
				KEY_RIGHT:
					_step_replay_by_ticks(1)
					get_viewport().set_input_as_handled()
					return
	if replay_playback_active and event is InputEventMouseButton:
		var replay_mouse_button := event as InputEventMouseButton
		if !replay_mouse_button.pressed and replay_mouse_button.button_index == MOUSE_BUTTON_RIGHT:
			if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			get_viewport().set_input_as_handled()
			return
	if !replay_playback_active or replay_camera_mode != REPLAY_CAMERA_RELATIVE:
		return
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var motion: InputEventMouseMotion = event
		replay_relative_pending_look_delta += motion.relative
		get_viewport().set_input_as_handled()
	elif event.is_action_pressed("ui_cancel"):
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			get_viewport().set_input_as_handled()

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
	if replay_playback_active and event is InputEventMouseButton:
		var replay_mouse_button := event as InputEventMouseButton
		if replay_mouse_button.button_index == MOUSE_BUTTON_RIGHT:
			if replay_mouse_button.pressed and _replay_camera_mode_uses_mouse_capture():
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
				get_viewport().set_input_as_handled()
				return
	if replay_playback_active and event is InputEventKey:
		var replay_key := event as InputEventKey
		if replay_key.pressed and !replay_key.echo:
			match replay_key.keycode:
				KEY_SPACE:
					_cycle_replay_camera_mode()
					get_viewport().set_input_as_handled()
					return
	if replay_playback_active and event.is_action_pressed("SpinAttack"):
		_cycle_replay_camera_mode()
		get_viewport().set_input_as_handled()
		return
	if replay_playback_active and event.is_action_pressed("DpadLeft"):
		_change_replay_focus(-1)
		get_viewport().set_input_as_handled()
		return
	if replay_playback_active and event.is_action_pressed("DpadRight"):
		_change_replay_focus(1)
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

func _return_to_menu() -> void:
	_cancel_race_finish_audio(true)
	stop_music(0.5)
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	_stop_replay_recording(network_manager.is_server and !singleplayer_mode)
	debug_replay_playback = false
	replay_playback_active = false
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_seek_checkpoints.clear()
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	_reset_replay_timeline_markers()
	replay_start_grid_slots = PackedInt32Array()
	if replay_timeline_root != null:
		replay_timeline_root.visible = false
	_apply_replay_playback_clock()
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
	_clear_track_visual_scene()
	debug_track_mesh.mesh = null
	_set_builtin_track_visuals_enabled(true)
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
	if singleplayer_options_root != null:
		singleplayer_options_root.visible = false

func _return_to_lobby() -> void:
	_cancel_race_finish_audio(true)
	stop_music(0.5)
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	_stop_replay_recording(network_manager.is_server and !singleplayer_mode)
	debug_replay_playback = false
	replay_playback_active = false
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_seek_checkpoints.clear()
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	_reset_replay_timeline_markers()
	replay_start_grid_slots = PackedInt32Array()
	if replay_timeline_root != null:
		replay_timeline_root.visible = false
	_apply_replay_playback_clock()
	_close_race_pause_menu()
	_reset_nametag_pool()
	game_sim.destroy_gamesim()
	race_finish_label.visible = false
	_hide_race_results_summary()
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	_clear_track_visual_scene()
	debug_track_mesh.mesh = null
	_set_builtin_track_visuals_enabled(true)
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
	network_manager.broadcast_lobby_roster()
	singleplayer_mode = false
	_singleplayer_tick = 0

func _teardown_race_world_for_transition() -> void:
	_cancel_race_finish_audio(true)
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	_stop_replay_recording(network_manager.is_server and !singleplayer_mode)
	debug_replay_playback = false
	replay_playback_active = false
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_seek_checkpoints.clear()
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	_reset_replay_timeline_markers()
	replay_start_grid_slots = PackedInt32Array()
	if replay_timeline_root != null:
		replay_timeline_root.visible = false
	_apply_replay_playback_clock()
	_close_race_pause_menu()
	_reset_nametag_pool()
	game_sim.destroy_gamesim()
	race_finish_label.visible = false
	_hide_race_results_summary()
	if network_manager.is_server:
		server_game_sim.destroy_gamesim()
		network_manager.server_game_sim = null
	_clear_track_visual_scene()
	debug_track_mesh.mesh = null
	_set_builtin_track_visuals_enabled(true)
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
	if network_manager.is_server:
		network_manager.flush_waiting_peers(true)
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
	if singleplayer_mode and !replay_playback_use_multiplayer_startup and !network_manager.get_cpu_roster().is_empty():
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
	var finish_rows := []
	for id_value in race_racers:
		var id := int(id_value)
		if network_manager._disconnected_during_race.has(id):
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
		if network_manager._disconnected_during_race.has(id) or network_manager.player_eliminations.has(id):
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
	var track_indices: Array = options.get("track_indices", [])
	var current_index := int(options.get("grand_prix_current_track", 0))
	var next_index := current_index + 1
	if next_index >= track_indices.size() or !_has_active_human_grand_prix_racer(options):
		network_manager.send_end_race()
		return
	options["grand_prix_current_track"] = next_index
	var next_track_index := int(track_indices[next_index])
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
	var finish_sim := server_game_sim if network_manager.is_server and !singleplayer_mode and server_game_sim != null else game_sim
	for racer_id in racer_ids:
		var watch_racer := finish_watch_ids.has(racer_id)
		if network_manager._disconnected_during_race.has(racer_id):
			continue
		if network_manager.player_finish_times.has(racer_id):
			continue
		var finished := false
		var eliminated := false
		if finish_sim != null and finish_sim.has_method("is_player_race_finished"):
			finished = finish_sim.is_player_race_finished(racer_id)
		if !finished and network_manager.player_eliminations.has(racer_id):
			continue
		if finished:
			if network_manager.is_server and !singleplayer_mode:
				network_manager.send_player_finished(racer_id, network_manager.server_tick)
			else:
				network_manager.record_player_finished(racer_id, network_manager.clients_server_tick)
			continue
		if !network_manager.is_vehicle_restore_enabled() and finish_sim != null and finish_sim.has_method("is_player_race_eliminated"):
			eliminated = finish_sim.is_player_race_eliminated(racer_id)
		else:
			for car in car_node_container.get_children():
				if car is VisualCar and car.owning_id == racer_id:
					finished = (car.machine_state & VisualCar.FZ_MS.COMPLETEDRACE_1_Q) != 0
					eliminated = !network_manager.is_vehicle_restore_enabled() and _vehicle_restore_off_state_is_eliminated(
						car.machine_state,
						car.state_2,
						car.position_current.y,
						-100000.0)
					break
		if eliminated:
			if network_manager.is_server and !singleplayer_mode:
				network_manager.send_player_eliminated(racer_id, network_manager.server_tick)
			elif singleplayer_mode:
				network_manager.record_player_eliminated(racer_id, network_manager.clients_server_tick)
		elif watch_racer:
			all_done = false
	if replay_playback_active:
		return
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
	var profile_process_start := Time.get_ticks_usec() if auto_render_profile_mode and game_sim.sim_started else 0
	_update_race_finish_sfx_duck(delta)
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
	if game_sim.sim_started and network_manager.net_race_finish_time != -1 and !replay_playback_active:
		_show_race_results_summary()
		_refresh_race_pause_replay_button()
	if game_sim.sim_started:
		var profile_visuals_start := Time.get_ticks_usec() if auto_render_profile_mode else 0
		_update_replay_auto_camera(delta)
		_update_replay_relative_camera(delta)
		_update_replay_timeline_controls()
		_update_native_render_camera()
		game_sim.render_gamesim_visuals_only(delta)
		if auto_render_profile_mode:
			render_profile_visuals_only_us += Time.get_ticks_usec() - profile_visuals_start
			render_profile_process_us += Time.get_ticks_usec() - profile_process_start
