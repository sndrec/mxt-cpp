class_name ReplayController extends Node

const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const SpectatorControllerClass = preload("res://ui/spectator_controller.gd")
const RacePresentationControllerClass = preload("res://ui/race_presentation_controller.gd")
const DebugRuntimeControllerClass = preload("res://core/debug_runtime_controller.gd")
const RaceSessionControllerClass = preload("res://core/race_session_controller.gd")

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var vehicle_content_controller: VehicleContentControllerClass = get_node("../VehicleContentController") as VehicleContentControllerClass
@onready var spectator_controller: SpectatorControllerClass = get_node("../SpectatorController") as SpectatorControllerClass
@onready var race_presentation_controller: RacePresentationControllerClass = get_node("../RacePresentationController") as RacePresentationControllerClass
@onready var debug_runtime_controller: DebugRuntimeControllerClass = get_node("../DebugRuntimeController") as DebugRuntimeControllerClass
@onready var race_session_controller: RaceSessionControllerClass = get_node("../RaceSessionController") as RaceSessionControllerClass
@onready var replays_button: Button = get_node("../Control/ReplaysButton") as Button
@onready var race_pause_save_replay_button: Button = get_node("../RacePauseLayer/RacePauseRoot/Center/Panel/Box/SaveReplayButton") as Button

const DEBUG_REPLAY_VERSION := 2
const REPLAY_SCHEMA_VERSION := 5
const REPLAY_FILE_SUFFIX := ".mxt_replay"
const REPLAY_COMPATIBILITY_WARNING := "This replay was recorded on an older version of the game, and may desync."
const SUBMISSION_REPLAY_ROOT := "user://leaderboard_submission_replays"
const GameVersionData = preload("res://core/game_version.gd")
const REPLAY_CAMERA_GAME := 0
const REPLAY_CAMERA_AUTO := 1
const REPLAY_CAMERA_SPECTATOR := 2
const REPLAY_CAMERA_RELATIVE := 3
const REPLAY_RELATIVE_DEFAULT_OFFSET := Vector3(0.0, 8.0, 28.0)
const REPLAY_RELATIVE_LOOK_TARGET := Vector3(0.0, 2.0, 0.0)
const REPLAY_RELATIVE_LOOK_SPEED := 0.0025
const REPLAY_RELATIVE_LOOK_ACTION_SPEED := 6.0
const REPLAY_RELATIVE_ROLL_SPEED := 4.0
const REPLAY_MANUAL_CAMERA_FAST_MULTIPLIER := 3.0
const REPLAY_MANUAL_CAMERA_FOV_MIN := 10.0
const REPLAY_MANUAL_CAMERA_FOV_MAX := 150.0
const REPLAY_MANUAL_CAMERA_SPEED_MIN := 1.0
const REPLAY_MANUAL_CAMERA_SPEED_MAX := 5000.0
const REPLAY_SEEK_CHECKPOINT_INTERVAL := 1800
const REPLAY_INTERFACE_CANVAS_LAYER := 90
const REPLAY_INPUT_DISPLAY_SCRIPT := preload("res://replay/replay_input_display.gd")
const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")
const LeaderboardReplayValidatorClass = preload("res://steam/leaderboard_replay_validator.gd")
const LegacyLeaderboardReplayReaderClass = preload("res://steam/legacy_leaderboard_replay_reader.gd")
const REPLAY_DEATH_KO_TEXTURE: Texture2D = preload("res://asset/tex/ui/replay_death_ko.png")
const REPLAY_DEATH_FALL_TEXTURE: Texture2D = preload("res://asset/tex/ui/replay_death_fall.png")
const REPLAY_DEATH_EXPLOSION_TEXTURE: Texture2D = preload("res://asset/tex/ui/replay_death_explosion.png")
const REPLAY_DEATH_ICON_SIZE := Vector2(24.0, 24.0)
const REPLAY_DEATH_FALLOUT := 2

var auto_replay_catalog_profile_mode: bool = false
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
var replay_recording_metadata: MxtReplayRunMetadata = MxtReplayRunMetadata.new()
var replay_recording_racer_ids: Array = []
var replay_recording_cpu_flags: Array = []
var replay_recording_stream: MxtReplayStream = MxtReplayStream.new()
var replay_recording_staged_path: String = ""
var replay_start_grid_slots: PackedInt32Array = PackedInt32Array()
var replay_playback_active: bool = false
var replay_playback_stream: MxtReplayStream
var replay_playback_source_metadata: MxtReplayRunMetadata = MxtReplayRunMetadata.new()
var replay_playback_compatible := false
var replay_playback_track_index := -1
var replay_playback_index: int = 0
var replay_playback_loaded_path: String = ""
var replay_playback_focus_index: int = 0
var replay_playback_racer_ids: Array = []
var replay_playback_cpu_flags: Array = []
var replay_playback_local_player_id: int = 0
var replay_playback_use_multiplayer_startup: bool = false
var replay_strict_verify_requested: bool = false
var replay_leaderboard_verify_requested: bool = false
var replay_leaderboard_validation: Dictionary = {}
var replay_skip_seek_bake_requested: bool = false
var replay_load_profile_requested: bool = false
var replay_playback_use_singleplayer_tick: bool = false
var replay_saved_finish_times: Dictionary = {}
var replay_saved_finish_placements: Dictionary = {}
var replay_saved_eliminations: Dictionary = {}
var replay_playback_paused: bool = false
var replay_playback_rate: float = 1.0
var replay_seek_checkpoints: Array = []
var replay_seek_checkpoint_bytes: int = 0
var replay_playback_source_bytes: int = 0
var replay_seeking_active: bool = false
var replay_normal_playback_tick_active: bool = false
var replay_camera_mode: int = REPLAY_CAMERA_GAME
var replay_auto_camera: Camera3D
var replay_relative_camera: Camera3D
var replay_relative_gravity_basis := Basis.IDENTITY
var replay_relative_gravity_basis_valid := false
var replay_relative_camera_basis := Basis.IDENTITY
var replay_relative_camera_basis_desired := Basis.IDENTITY
var replay_relative_offset := Vector3.ZERO
var replay_relative_velocity := Vector3.ZERO
var replay_relative_pending_look_delta := Vector2.ZERO
var replay_manual_camera_fov := 72.0
var replay_manual_camera_speed := 300.0
var replay_input_calib: InputCalibration
var replay_interface_layer: CanvasLayer
var replay_catalog_root: Control
var replay_catalog_list: ItemList
var replay_catalog_metadata_label: RichTextLabel
var replay_catalog_name_edit: LineEdit
var replay_catalog_watch_button: Button
var replay_catalog_rename_button: Button
var replay_catalog_delete_button: Button
var replay_catalog_entries: Array = []
var replay_compatibility_dialog: ConfirmationDialog
var pending_incompatible_replay_path := ""
var replay_timeline_root: Control
var replay_timeline_panel: PanelContainer
var replay_timeline_track: ColorRect
var replay_timeline_fill: ColorRect
var replay_timeline_playhead: ColorRect
var replay_timeline_marker_layer: Control
var replay_timeline_time_label: Label
var replay_timeline_rate_label: Label
var replay_timeline_play_button: Button
var replay_timeline_camera_mode_button: Button
var replay_timeline_save_local_button: Button
var replay_timeline_focus_prev_button: Button
var replay_timeline_focus_next_button: Button
var replay_timeline_resume_practice_button: Button
var replay_timeline_resume_reason_label: Label
var replay_resume_dialog: ConfirmationDialog
var replay_resume_keep_original_checkbox: CheckBox
var replay_input_display_panel: PanelContainer
var replay_input_display: Control
var replay_input_display_checkbox: CheckBox
var replay_input_display_enabled := false
var replay_hide_hud_checkbox: CheckBox
var replay_hide_hud_enabled := false
var replay_camera_controls: HBoxContainer
var replay_camera_fov_slider: HSlider
var replay_camera_fov_value: SpinBox
var replay_camera_speed_slider: HSlider
var replay_camera_speed_value: SpinBox
var replay_input_display_frame_inputs: Dictionary = {}
var replay_timeline_markers: Dictionary = {}
var replay_marker_last_laps: Dictionary = {}
var replay_marker_last_places: Dictionary = {}
var replay_marker_last_death_states: Dictionary = {}
var replay_collecting_timeline_markers: bool = false
var replay_timeline_markers_dirty: bool = true
var replay_timeline_marker_last_focus: int = -999999
var replay_timeline_marker_last_size := Vector2(-1.0, -1.0)

func initialize() -> void:
	_build_replay_timeline_controls()
	reload_input_calibration()
	if replays_button != null and !replays_button.pressed.is_connected(_open_replay_catalog):
		replays_button.pressed.connect(_open_replay_catalog)
	if race_pause_save_replay_button != null and !race_pause_save_replay_button.pressed.is_connected(_on_pause_save_replay_pressed):
		race_pause_save_replay_button.pressed.connect(_on_pause_save_replay_pressed)
	if !game_manager.network_manager.input_transport.authoritative_server_frame.is_connected(record_frame):
		game_manager.network_manager.input_transport.authoritative_server_frame.connect(record_frame)
	refresh_pause_button()

func _ensure_replay_interface_layer() -> CanvasLayer:
	if replay_interface_layer != null and is_instance_valid(replay_interface_layer):
		return replay_interface_layer
	replay_interface_layer = CanvasLayer.new()
	replay_interface_layer.name = "ReplayInterfaceLayer"
	replay_interface_layer.layer = REPLAY_INTERFACE_CANVAS_LAYER
	add_child(replay_interface_layer)
	return replay_interface_layer

func configure_command_line(args: Array, user_args: Array) -> bool:
	auto_replay_catalog_profile_mode = args.has("--profile-replay-catalog") or user_args.has("--profile-replay-catalog")
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
	replay_leaderboard_verify_requested = args.has("--leaderboard-replay-verify") or user_args.has("--leaderboard-replay-verify")
	replay_strict_verify_requested = replay_leaderboard_verify_requested or args.has("--strict-replay-verify") or user_args.has("--strict-replay-verify")
	replay_skip_seek_bake_requested = args.has("--skip-replay-seek-bake") or user_args.has("--skip-replay-seek-bake")
	replay_load_profile_requested = args.has("--profile-replay-load") or user_args.has("--profile-replay-load")
	if replay_autoload_path != "":
		call_deferred("_request_replay_playback_from_path", replay_autoload_path)
		return true
	if debug_replay_autoload_path != "":
		call_deferred("_load_and_start_debug_replay", debug_replay_autoload_path)
		return true
	if auto_replay_catalog_profile_mode:
		call_deferred("_profile_replay_catalog_and_quit")
		return true
	return false

func reload_input_calibration() -> void:
	replay_input_calib = InputCalibration.load_from_disk()

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
		var motion := event as InputEventMouseMotion
		replay_relative_pending_look_delta += motion.relative
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("ui_cancel") and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		get_viewport().set_input_as_handled()

func handle_unhandled_input(event: InputEvent) -> bool:
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F5:
		if debug_replay_recording:
			_stop_and_save_debug_replay_recording()
		else:
			_start_debug_replay_recording()
		return true
	if event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_F8:
		var replay_path := DisplayServer.clipboard_get().strip_edges()
		if replay_path != "":
			_load_and_start_debug_replay(replay_path)
		return true
	if replay_playback_active and event is InputEventMouseButton:
		var replay_mouse_button := event as InputEventMouseButton
		if replay_mouse_button.button_index == MOUSE_BUTTON_RIGHT and replay_mouse_button.pressed and _replay_camera_mode_uses_mouse_capture():
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			return true
	if replay_playback_active and event.is_action_pressed("SpinAttack"):
		_cycle_replay_camera_mode()
		return true
	if replay_playback_active and event.is_action_pressed("DpadLeft"):
		_change_replay_focus(-1)
		return true
	if replay_playback_active and event.is_action_pressed("DpadRight"):
		_change_replay_focus(1)
		return true
	return false

func consume_debug_playback_input(input_bytes: PackedByteArray) -> PackedByteArray:
	if !debug_replay_playback:
		return input_bytes
	if debug_replay_playback_index >= debug_replay_playback_inputs.size():
		debug_replay_playback = false
		game_manager.game_sim.set_sim_started(false)
		print("MXT_DEBUG_REPLAY playback complete ", debug_replay_loaded_path, " end_tick=", game_manager._singleplayer_tick)
		if game_manager.headless_mode:
			get_tree().quit()
		return PackedByteArray()
	var replay_input := (debug_replay_playback_inputs[debug_replay_playback_index] as PackedByteArray).duplicate()
	debug_replay_playback_index += 1
	return replay_input

func record_debug_input(input_bytes: PackedByteArray) -> void:
	if debug_replay_recording:
		debug_replay_inputs.append(input_bytes.duplicate())

func record_singleplayer_frame(tick: int) -> void:
	if !replay_recording_active:
		return
	if game_manager.practice_controller.session_active:
		game_manager.practice_controller.append_canonical_game_sim_frame(game_manager.game_sim, tick)
		return
	if !replay_recording_stream.append_game_sim_frame(game_manager.game_sim, tick):
		push_error("Replay recording rejected native frame %d: %s" % [tick, replay_recording_stream.get_last_error()])

func reset_for_transition(save_multiplayer_host_replay: bool) -> void:
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	stop_recording(save_multiplayer_host_replay)
	debug_replay_playback = false
	replay_playback_active = false
	_apply_replay_hud_visibility()
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_normal_playback_tick_active = false
	replay_seek_checkpoints.clear()
	replay_seek_checkpoint_bytes = 0
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	_reset_replay_timeline_markers()
	replay_input_display_frame_inputs = {}
	_refresh_replay_input_display()
	replay_start_grid_slots = PackedInt32Array()
	_clear_recording_payload()
	replay_recording_staged_path = ""
	_clear_playback_payload()
	debug_replay_inputs.clear()
	debug_replay_playback_inputs.clear()
	debug_replay_snapshot_state = PackedByteArray()
	if replay_timeline_root != null:
		replay_timeline_root.visible = false
	_apply_replay_playback_clock()


func detach_playback_for_practice() -> bool:
	if !replay_playback_active:
		return false
	replay_playback_active = false
	_apply_replay_hud_visibility()
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_normal_playback_tick_active = false
	replay_seeking_active = false
	replay_collecting_timeline_markers = false
	replay_seek_checkpoints.clear()
	replay_seek_checkpoint_bytes = 0
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	_reset_replay_timeline_markers()
	replay_input_display_frame_inputs = {}
	_refresh_replay_input_display()
	replay_start_grid_slots = PackedInt32Array()
	_clear_playback_payload()
	if replay_timeline_root != null:
		replay_timeline_root.visible = false
	_apply_replay_playback_clock()
	return true


func _clear_recording_payload() -> void:
	replay_recording_metadata = MxtReplayRunMetadata.new()
	replay_recording_racer_ids.clear()
	replay_recording_cpu_flags.clear()
	replay_recording_stream = MxtReplayStream.new()

func _clear_playback_payload() -> void:
	replay_playback_stream = null
	replay_playback_source_metadata = MxtReplayRunMetadata.new()
	replay_playback_compatible = false
	replay_playback_track_index = -1
	replay_playback_index = 0
	replay_playback_loaded_path = ""
	_refresh_replay_timeline_save_local_button()
	replay_playback_focus_index = 0
	replay_playback_racer_ids.clear()
	replay_playback_cpu_flags.clear()
	replay_playback_local_player_id = 0
	replay_playback_source_bytes = 0

func get_memory_usage_stats() -> Dictionary:
	var practice_recording := game_manager.practice_controller.session_active \
		and game_manager.practice_controller.timeline_enabled
	return {
		"recording_frames": game_manager.practice_controller.canonical_frame_count() if practice_recording else replay_recording_stream.frame_count(),
		"recording_input_bytes": game_manager.practice_controller.canonical_input_byte_count() if practice_recording else replay_recording_stream.input_byte_count(),
		"playback_frames": _playback_frame_count(),
		"playback_source_bytes": replay_playback_source_bytes,
		"seek_checkpoint_count": replay_seek_checkpoints.size(),
		"seek_checkpoint_bytes": replay_seek_checkpoint_bytes,
		"debug_recording_frames": debug_replay_inputs.size(),
		"debug_playback_frames": debug_replay_playback_inputs.size(),
	}

func update(delta: float) -> void:
	_update_replay_auto_camera(delta)
	_update_replay_relative_camera(delta)
	_update_replay_timeline_controls()

func _on_pause_save_replay_pressed() -> void:
	var saved_path := save_practice_snapshot_locally() \
		if game_manager.practice_controller.session_active else save_replay_locally()
	if saved_path != "":
		race_presentation_controller.show_notification("Replay Saved", 2200)
	refresh_pause_button()

func finish_recording() -> void:
	replay_recording_active = false
	refresh_pause_button()

func can_save_replay_locally() -> bool:
	if !game_manager.singleplayer_mode:
		return false
	if game_manager.practice_controller.session_active:
		return game_manager.practice_controller.timeline_enabled \
			and game_manager.practice_controller.canonical_frame_count() > 0
	return !replay_recording_saved and replay_recording_stream.frame_count() > 0

func save_replay_locally() -> String:
	if !can_save_replay_locally():
		return ""
	if game_manager.practice_controller.session_active:
		return save_practice_snapshot_locally()
	var path := _write_replay_recording("manual", _replay_dir())
	if path.is_empty():
		return ""
	replay_recording_saved = true
	replay_recording_active = false
	_clear_recording_payload()
	refresh_pause_button()
	return path


func save_practice_snapshot_locally() -> String:
	if !game_manager.singleplayer_mode \
			or !game_manager.practice_controller.session_active \
			or !game_manager.practice_controller.timeline_enabled \
			or game_manager.practice_controller.canonical_frame_count() <= 0:
		return ""
	return _write_replay_recording("practice_snapshot", _replay_dir())

func stage_completed_time_attack_replay(for_submission: bool) -> String:
	var configuration := game_manager.network_manager.race_configuration
	if (!configuration.is_time_attack() and !configuration.is_practice()) \
		or for_submission != configuration.is_time_attack():
		return ""
	finish_recording()
	replay_recording_staged_path = _write_replay_recording(
		"time_attack_submission" if for_submission else "time_attack_preview",
		ProjectSettings.globalize_path(SUBMISSION_REPLAY_ROOT))
	if !replay_recording_staged_path.is_empty():
		_clear_recording_payload()
	return replay_recording_staged_path

func _staged_replay_local_path(path: String) -> String:
	if path.is_empty():
		return ""
	var staging_root := ProjectSettings.globalize_path(SUBMISSION_REPLAY_ROOT).simplify_path().to_lower()
	var absolute_path := ProjectSettings.globalize_path(path).simplify_path()
	var lower_path := absolute_path.to_lower()
	if !lower_path.begins_with(staging_root + "/") and !lower_path.begins_with(staging_root + "\\"):
		return ""
	return _replay_dir().path_join(absolute_path.get_file())

func can_save_staged_replay_locally(path: String) -> bool:
	var local_path := _staged_replay_local_path(path)
	return !local_path.is_empty() and FileAccess.file_exists(path) and !FileAccess.file_exists(local_path)

func save_staged_replay_locally(path: String) -> String:
	var local_path := _staged_replay_local_path(path)
	if local_path.is_empty() or !FileAccess.file_exists(path):
		return ""
	if FileAccess.file_exists(local_path):
		return local_path
	if DirAccess.make_dir_recursive_absolute(_replay_dir()) != OK:
		return ""
	if DirAccess.copy_absolute(ProjectSettings.globalize_path(path), local_path) != OK:
		return ""
	if replay_catalog_root != null and replay_catalog_root.visible:
		_refresh_replay_catalog()
	_refresh_replay_timeline_save_local_button()
	return local_path

func _replay_dir() -> String:
	return ProjectSettings.globalize_path("user://replays")

func _replay_make_stamp() -> String:
	return Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")

func _replay_engine_version() -> String:
	var engine_version: Dictionary = Engine.get_version_info()
	return str(engine_version.get("string", ""))

func _replay_is_compatible(data: Dictionary) -> bool:
	if !_replay_schema_is_supported(data):
		return false
	var stored_version = data.get("game_version", {})
	if typeof(stored_version) != TYPE_DICTIONARY:
		return false
	var version: Dictionary = stored_version
	return int(version.get("major", -1)) == GameVersionData.MAJOR and int(version.get("compatibility", -1)) == GameVersionData.COMPATIBILITY

func _replay_schema_is_supported(data: Dictionary) -> bool:
	return int(data.get("schema_version", -1)) == REPLAY_SCHEMA_VERSION

func _replay_mode_name() -> String:
	if !game_manager.singleplayer_mode:
		return "Multiplayer"
	if game_manager.network_manager.race_configuration.is_practice():
		return "Practice"
	if game_manager.network_manager.lobby_settings.get_cpu_roster().is_empty():
		return "Time Attack"
	return "CPU Race"

func _replay_should_record_current_race() -> bool:
	if replay_playback_active:
		return false
	if game_manager.network_manager.race_configuration.is_practice() \
		and game_manager.network_manager.race_configuration.lap_count == 0:
		return false
	if game_manager.singleplayer_mode:
		return true
	return game_manager.network_manager.is_server

func start_recording(track_index: int, race_roster: MxtRaceRoster, start_grid_slots: PackedInt32Array) -> void:
	stop_recording()
	_clear_recording_payload()
	replay_recording_staged_path = ""
	if !_replay_should_record_current_race() or race_roster == null:
		return
	var practice_session := game_manager.network_manager.race_configuration.is_practice()
	replay_recording_source = "practice" if practice_session else ("singleplayer" if game_manager.singleplayer_mode else "server")
	var normalized_roster := MxtRaceRoster.new()
	for index in race_roster.count():
		var settings: Dictionary = _settings_with_vehicle_content_evidence(race_roster.get_settings_dictionary(index))
		var player_id: int = race_roster.get_player_id(index)
		if !normalized_roster.append_settings(player_id, player_id, race_roster.is_cpu(index), false, false, settings):
			push_error("Replay recording roster rejected: %s" % normalized_roster.get_last_error())
			return
		replay_recording_racer_ids.append(player_id)
		replay_recording_cpu_flags.append(race_roster.is_cpu(index))
	replay_recording_stream = MxtReplayStream.new()
	replay_recording_stream.begin_recording(replay_recording_racer_ids, replay_recording_cpu_flags)
	if !replay_recording_stream.get_last_error().is_empty():
		push_error("Replay recording roster rejected: %s" % replay_recording_stream.get_last_error())
		return
	replay_recording_active = true
	replay_recording_saved = false
	if practice_session:
		game_manager.practice_controller.configure_timeline_roster(replay_recording_racer_ids, replay_recording_cpu_flags)
	var track_content_id := game_manager.track_content_controller.track_id_for_index(track_index)
	var track_record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(track_content_id)
	replay_recording_metadata.schema_version = REPLAY_SCHEMA_VERSION
	replay_recording_metadata.build = GameVersionData.display_string()
	replay_recording_metadata.game_version_major = GameVersionData.MAJOR
	replay_recording_metadata.game_version_compatibility = GameVersionData.COMPATIBILITY
	replay_recording_metadata.game_version_patch = GameVersionData.PATCH
	replay_recording_metadata.engine_version = _replay_engine_version()
	replay_recording_metadata.created_unix = int(Time.get_unix_time_from_system())
	replay_recording_metadata.name = "%s %s" % [_current_track_name(), _replay_make_stamp()]
	replay_recording_metadata.mode = _replay_mode_name()
	replay_recording_metadata.source = replay_recording_source
	replay_recording_metadata.track_content_id = track_content_id
	replay_recording_metadata.track_gameplay_digest = game_manager.track_content_controller.track_gameplay_digest_for_index(track_index)
	replay_recording_metadata.track_package_digest = track_record.package_digest if track_record != null else ""
	replay_recording_metadata.track_workshop_id = str(track_record.published_file_id) if track_record != null and track_record.published_file_id > 0 else ""
	replay_recording_metadata.track_name = _current_track_name()
	replay_recording_metadata.roster = normalized_roster
	replay_recording_metadata.start_grid_slots = start_grid_slots
	replay_recording_metadata.spawn_seed = game_manager.network_manager.spawn_seed
	replay_recording_metadata.set_race_metadata(game_manager.network_manager.race_metadata_dictionary())
	replay_recording_metadata.runtime_auto_accelerate = debug_runtime_controller.auto_accelerate
	replay_recording_metadata.runtime_auto_bumpers = game_manager.auto_bumpers_mode
	replay_recording_metadata.runtime_debug_bumper_smoke = debug_runtime_controller.bumper_smoke_enabled
	replay_recording_metadata.runtime_debug_rail_trace = debug_runtime_controller.rail_trace_enabled
	refresh_pause_button()

func _settings_with_vehicle_content_evidence(settings: Dictionary) -> Dictionary:
	var output := settings.duplicate(true)
	var content_id := String(output.get("vehicle_content_id", ""))
	var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(content_id)
	output["vehicle_gameplay_digest"] = record.gameplay_digest if record != null else ""
	var package_digest := record.package_digest if record != null else ""
	if !package_digest.is_empty():
		output["vehicle_package_digest"] = package_digest
	var published_file_id := str(record.published_file_id) if record != null and record.published_file_id > 0 else ""
	if !published_file_id.is_empty():
		output["vehicle_workshop_id"] = published_file_id
	return output

func _settings_array_with_vehicle_content_evidence(settings: Array) -> Array:
	var output: Array = []
	for settings_value in settings:
		if typeof(settings_value) == TYPE_DICTIONARY:
			output.append(_settings_with_vehicle_content_evidence(settings_value as Dictionary))
	return output

func _race_roster_from_settings(settings: Array, racer_ids: Array, cpu_flags: Array) -> MxtRaceRoster:
	if settings.size() != racer_ids.size():
		push_error("Replay roster settings do not match the recorded racer IDs.")
		return null
	var roster := MxtRaceRoster.new()
	for index in settings.size():
		if typeof(settings[index]) != TYPE_DICTIONARY:
			push_error("Replay roster contains malformed player settings.")
			return null
		var player_id := int(racer_ids[index])
		var is_cpu := index < cpu_flags.size() and bool(cpu_flags[index])
		if !roster.append_settings(player_id, player_id, is_cpu, false, false, settings[index]):
			push_error("Replay roster conversion failed: %s" % roster.get_last_error())
			return null
	return roster

func stop_recording(save_multiplayer_host_replay := false) -> void:
	if save_multiplayer_host_replay and replay_recording_active and !replay_recording_saved and replay_recording_source == "server":
		var saved_path := _write_replay_recording("auto", _replay_dir())
		if !saved_path.is_empty():
			replay_recording_saved = true
	replay_recording_active = false

func refresh_pause_button() -> void:
	if race_pause_save_replay_button == null:
		return
	var active_practice := game_manager.practice_controller.session_active \
		and game_manager.practice_controller.timeline_enabled
	var can_save := can_save_replay_locally() and (active_practice \
		or game_manager.network_manager.race_results.net_race_finish_time != -1)
	race_pause_save_replay_button.text = "Save Current Replay" if active_practice else "Save Replay"
	race_pause_save_replay_button.visible = can_save
	race_pause_save_replay_button.disabled = !can_save

func record_frame(tick: int) -> void:
	if !replay_recording_active or replay_recording_saved:
		return
	if !replay_recording_stream.append_game_sim_frame(game_manager.network_manager.server_game_sim, tick):
		push_error("Replay recording rejected authoritative frame %d." % tick)

func _write_replay_recording(reason: String, replay_dir: String) -> String:
	var export_stream := _recording_stream_for_export()
	if export_stream == null or export_stream.frame_count() <= 0:
		return ""
	var err := DirAccess.make_dir_recursive_absolute(replay_dir)
	if err != OK:
		push_warning("Replay save failed: could not create %s err=%s" % [replay_dir, str(err)])
		return ""
	var run_metadata := replay_recording_metadata.copy()
	run_metadata.saved_reason = reason
	run_metadata.duration_ticks = export_stream.frame_count()
	run_metadata.set_results(
		game_manager.network_manager.race_results.player_finish_times,
		game_manager.network_manager.race_results.player_finish_placements,
		game_manager.network_manager.race_results.player_eliminations)
	var metadata: Dictionary = run_metadata.to_dictionary()
	var safe_track := run_metadata.track_name.replace("/", "_").replace("\\", "_").replace(" ", "_")
	var path := _unique_replay_path(replay_dir, safe_track)
	var temporary_path := path + ".tmp"
	if !export_stream.write_file(temporary_path, metadata):
		push_warning("Replay save failed: %s" % export_stream.get_last_error())
		return ""
	if DirAccess.rename_absolute(temporary_path, path) != OK:
		DirAccess.remove_absolute(temporary_path)
		push_warning("Replay save failed while committing %s" % path)
		return ""
	var saved_frame_count := export_stream.frame_count()
	print("MXT_REPLAY saved ", path, " frames=", saved_frame_count)
	return path


func _recording_stream_for_export() -> MxtReplayStream:
	if game_manager.practice_controller.session_active and game_manager.practice_controller.timeline_enabled:
		return game_manager.practice_controller.canonical_stream()
	return replay_recording_stream


func _unique_replay_path(replay_dir: String, safe_track: String) -> String:
	var basename := "mxt_%s_%s" % [safe_track, _replay_make_stamp()]
	var path := replay_dir.path_join(basename + REPLAY_FILE_SUFFIX)
	var suffix := 2
	while FileAccess.file_exists(path):
		path = replay_dir.path_join("%s_%d%s" % [basename, suffix, REPLAY_FILE_SUFFIX])
		suffix += 1
	return path

func _load_replay_file(path: String) -> Dictionary:
	if !FileAccess.file_exists(path):
		push_warning("Replay load failed: file not found: %s" % path)
		return {}
	var stream := MxtReplayStream.new()
	if !stream.load_file(path):
		if !replay_leaderboard_verify_requested:
			push_warning("Replay load failed: %s" % stream.get_last_error())
			return {}
		var legacy_bytes := FileAccess.get_file_as_bytes(path)
		var legacy_result := LegacyLeaderboardReplayReaderClass.convert(game_manager, legacy_bytes)
		if !bool(legacy_result.get("valid", false)):
			push_warning("Legacy leaderboard replay load failed: %s" % String(legacy_result.get("reason", "invalid_legacy_leaderboard_replay")))
			return {}
		stream = legacy_result.get("_native_stream") as MxtReplayStream
		var legacy_metadata_value = legacy_result.get("_native_metadata", {})
		if stream == null or typeof(legacy_metadata_value) != TYPE_DICTIONARY:
			push_warning("Legacy leaderboard replay conversion produced no replay stream.")
			return {}
		var legacy_metadata: Dictionary = legacy_metadata_value
		legacy_metadata["_replay_stream"] = stream
		replay_playback_source_bytes = legacy_bytes.size()
		return legacy_metadata
	var parsed: Dictionary = stream.get_metadata()
	replay_playback_source_bytes = int(stream.get_stats().get("source_bytes", 0))
	if !_replay_schema_is_supported(parsed):
		push_warning("Replay load refused: unsupported schema %s; expected schema %d." % [
			str(parsed.get("schema_version", -1)),
			REPLAY_SCHEMA_VERSION,
		])
		return {}
	if replay_strict_verify_requested and !_replay_is_compatible(parsed):
		push_warning("Replay load refused: compatibility mismatch. Stored=%s expected=%d.%d schema=%s expected_schema=%d" % [
			str(parsed.get("game_version", {})),
			GameVersionData.MAJOR,
			GameVersionData.COMPATIBILITY,
			str(parsed.get("schema_version", -1)),
			REPLAY_SCHEMA_VERSION,
		])
		return {}
	parsed["_replay_stream"] = stream
	return parsed

func _request_replay_playback_from_path(path: String) -> void:
	var metadata := _load_replay_metadata_file(path)
	if metadata.is_empty() or !_replay_schema_is_supported(metadata) or _replay_is_compatible(metadata) or replay_strict_verify_requested:
		_start_replay_playback_from_path(path, true)
		return
	if game_manager.headless_mode:
		push_warning(REPLAY_COMPATIBILITY_WARNING)
		_start_replay_playback_from_path(path, true)
		return
	_show_replay_compatibility_warning(path)

func play_replay_file(path: String) -> void:
	_request_replay_playback_from_path(path)

func _show_replay_compatibility_warning(path: String) -> void:
	pending_incompatible_replay_path = path
	if replay_compatibility_dialog == null or !is_instance_valid(replay_compatibility_dialog):
		replay_compatibility_dialog = ConfirmationDialog.new()
		replay_compatibility_dialog.name = "ReplayCompatibilityWarning"
		replay_compatibility_dialog.title = "Replay Compatibility Warning"
		replay_compatibility_dialog.dialog_text = REPLAY_COMPATIBILITY_WARNING
		replay_compatibility_dialog.ok_button_text = "Play Anyway"
		replay_compatibility_dialog.cancel_button_text = "Cancel"
		replay_compatibility_dialog.exclusive = true
		replay_compatibility_dialog.confirmed.connect(_on_replay_compatibility_confirmed)
		replay_compatibility_dialog.canceled.connect(_on_replay_compatibility_canceled)
		add_child(replay_compatibility_dialog)
	replay_compatibility_dialog.popup_centered(Vector2i(620, 170))

func _on_replay_compatibility_confirmed() -> void:
	var path := pending_incompatible_replay_path
	pending_incompatible_replay_path = ""
	if !path.is_empty():
		call_deferred("_start_replay_playback_from_path", path, true)

func _on_replay_compatibility_canceled() -> void:
	pending_incompatible_replay_path = ""

func _load_replay_metadata_file(path: String, profile: Dictionary = {}) -> Dictionary:
	if path == "" or !FileAccess.file_exists(path):
		return {}
	var read_start_usec := Time.get_ticks_usec()
	var stream := MxtReplayStream.new()
	var loaded := stream.load_file(path, true)
	profile["read_usec"] = Time.get_ticks_usec() - read_start_usec
	var source_file := FileAccess.open(path, FileAccess.READ)
	profile["bytes"] = source_file.get_length() if source_file != null else 0
	profile["strip_frames_usec"] = 0
	profile["parse_usec"] = 0
	if !loaded:
		return {}
	return stream.get_metadata()


func _decoded_replay_frame_at(frame_index: int) -> Dictionary:
	if replay_playback_stream == null or frame_index < 0 or frame_index >= replay_playback_stream.frame_count():
		return {}
	return replay_playback_stream.read_frame(frame_index)


func _decoded_replay_frame_range(begin_index: int, end_index: int) -> Array:
	var output: Array = []
	if replay_playback_stream == null:
		return output
	return replay_playback_stream.read_frame_range(begin_index, end_index)

func _playback_frame_count() -> int:
	return replay_playback_stream.frame_count() if replay_playback_stream != null else 0

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
	ok = _replay_compare_int_dictionary("finish_times", replay_saved_finish_times, game_manager.network_manager.race_results.player_finish_times) and ok
	ok = _replay_compare_int_dictionary("finish_placements", replay_saved_finish_placements, game_manager.network_manager.race_results.player_finish_placements) and ok
	ok = _replay_compare_int_dictionary("eliminations", replay_saved_eliminations, game_manager.network_manager.race_results.player_eliminations) and ok
	if !ok and game_manager.game_sim != null and game_manager.game_sim.has_method("get_player_debug_string"):
		for id_value in replay_playback_racer_ids:
			print("MXT_REPLAY_VERIFY_STATE tick=", game_manager._singleplayer_tick, " ", game_manager.game_sim.get_player_debug_string(int(id_value)))
	return ok

func _leaderboard_verified_result() -> Dictionary:
	if !bool(replay_leaderboard_validation.get("valid", false)) or replay_playback_racer_ids.size() != 1:
		return {"valid": false, "reason": "missing_leaderboard_validation"}
	var racer_id := int(replay_playback_racer_ids[0])
	var finish_tick := _lookup_replay_tick_for_id(game_manager.network_manager.race_results.player_finish_times, racer_id)
	var start_tick := race_presentation_controller.race_results_start_tick()
	if finish_tick <= start_tick:
		return {"valid": false, "reason": "invalid_resimulated_finish_time"}
	var result := replay_leaderboard_validation.duplicate(true)
	result["finish_tick"] = finish_tick
	result["start_tick"] = start_tick
	result["score_milliseconds"] = TimeAttackRulesClass.finish_ticks_to_milliseconds(finish_tick, start_tick)
	return result

func _build_replay_timeline_controls() -> void:
	if replay_timeline_root != null and is_instance_valid(replay_timeline_root):
		return
	replay_timeline_root = Control.new()
	replay_timeline_root.name = "ReplayTimeline"
	replay_timeline_root.visible = false
	replay_timeline_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_ensure_replay_interface_layer().add_child(replay_timeline_root)
	replay_timeline_panel = PanelContainer.new()
	replay_timeline_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	replay_timeline_panel.anchor_left = 0.08
	replay_timeline_panel.anchor_right = 0.92
	replay_timeline_panel.anchor_top = 1.0
	replay_timeline_panel.anchor_bottom = 1.0
	replay_timeline_panel.offset_top = -210.0
	replay_timeline_panel.offset_bottom = -18.0
	replay_timeline_root.add_child(replay_timeline_panel)
	replay_input_display_panel = PanelContainer.new()
	replay_input_display_panel.name = "ReplayInputDisplayPanel"
	replay_input_display_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_input_display_panel.anchor_left = 0.08
	replay_input_display_panel.anchor_right = 0.08
	replay_input_display_panel.anchor_top = 1.0
	replay_input_display_panel.anchor_bottom = 1.0
	replay_input_display_panel.offset_left = 0.0
	replay_input_display_panel.offset_right = 176.0
	replay_input_display_panel.offset_top = -260.0
	replay_input_display_panel.offset_bottom = -140.0
	replay_input_display_panel.visible = false
	_ensure_replay_interface_layer().add_child(replay_input_display_panel)
	var input_display_margin := MarginContainer.new()
	input_display_margin.add_theme_constant_override("margin_left", 6)
	input_display_margin.add_theme_constant_override("margin_right", 6)
	input_display_margin.add_theme_constant_override("margin_top", 4)
	input_display_margin.add_theme_constant_override("margin_bottom", 4)
	replay_input_display_panel.add_child(input_display_margin)
	replay_input_display = REPLAY_INPUT_DISPLAY_SCRIPT.new() as Control
	input_display_margin.add_child(replay_input_display)
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
	replay_timeline_focus_prev_button.pressed.connect(_change_replay_focus.bind(-1))
	focus_controls.add_child(replay_timeline_focus_prev_button)
	replay_timeline_focus_next_button = Button.new()
	replay_timeline_focus_next_button.text = ">"
	replay_timeline_focus_next_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_focus_next_button.custom_minimum_size = Vector2(180.0, 28.0)
	replay_timeline_focus_next_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_focus_next_button.pressed.connect(_change_replay_focus.bind(1))
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
	replay_timeline_camera_mode_button = Button.new()
	replay_timeline_camera_mode_button.text = "Camera: Game"
	replay_timeline_camera_mode_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_camera_mode_button.pressed.connect(_cycle_replay_camera_mode)
	controls.add_child(replay_timeline_camera_mode_button)
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
	replay_input_display_checkbox = CheckBox.new()
	replay_input_display_checkbox.text = "Inputs"
	replay_input_display_checkbox.focus_mode = Control.FOCUS_NONE
	replay_input_display_checkbox.button_pressed = replay_input_display_enabled
	replay_input_display_checkbox.toggled.connect(_on_replay_input_display_toggled)
	controls.add_child(replay_input_display_checkbox)
	replay_hide_hud_checkbox = CheckBox.new()
	replay_hide_hud_checkbox.text = "Hide HUD"
	replay_hide_hud_checkbox.focus_mode = Control.FOCUS_NONE
	replay_hide_hud_checkbox.button_pressed = replay_hide_hud_enabled
	replay_hide_hud_checkbox.toggled.connect(_on_replay_hide_hud_toggled)
	controls.add_child(replay_hide_hud_checkbox)
	replay_timeline_save_local_button = Button.new()
	replay_timeline_save_local_button.text = "Save Replay Locally"
	replay_timeline_save_local_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_save_local_button.visible = false
	replay_timeline_save_local_button.pressed.connect(_on_replay_timeline_save_local_pressed)
	controls.add_child(replay_timeline_save_local_button)
	replay_timeline_resume_practice_button = Button.new()
	replay_timeline_resume_practice_button.text = "Resume in Practice"
	replay_timeline_resume_practice_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_resume_practice_button.pressed.connect(_on_replay_resume_practice_pressed)
	controls.add_child(replay_timeline_resume_practice_button)
	replay_timeline_time_label = Label.new()
	replay_timeline_time_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_time_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	controls.add_child(replay_timeline_time_label)
	replay_camera_controls = HBoxContainer.new()
	replay_camera_controls.name = "ManualCameraControls"
	replay_camera_controls.add_theme_constant_override("separation", 8)
	replay_camera_controls.visible = false
	rows.add_child(replay_camera_controls)
	var fov_label := Label.new()
	fov_label.text = "FoV"
	replay_camera_controls.add_child(fov_label)
	replay_camera_fov_slider = HSlider.new()
	replay_camera_fov_slider.min_value = REPLAY_MANUAL_CAMERA_FOV_MIN
	replay_camera_fov_slider.max_value = REPLAY_MANUAL_CAMERA_FOV_MAX
	replay_camera_fov_slider.step = 1.0
	replay_camera_fov_slider.value = replay_manual_camera_fov
	replay_camera_fov_slider.custom_minimum_size.x = 180.0
	replay_camera_fov_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_camera_fov_slider.value_changed.connect(_on_replay_camera_fov_changed)
	replay_camera_controls.add_child(replay_camera_fov_slider)
	replay_camera_fov_value = SpinBox.new()
	replay_camera_fov_value.min_value = REPLAY_MANUAL_CAMERA_FOV_MIN
	replay_camera_fov_value.max_value = REPLAY_MANUAL_CAMERA_FOV_MAX
	replay_camera_fov_value.step = 1.0
	replay_camera_fov_value.value = replay_manual_camera_fov
	replay_camera_fov_value.suffix = " deg"
	replay_camera_fov_value.custom_minimum_size.x = 105.0
	replay_camera_fov_value.value_changed.connect(_on_replay_camera_fov_changed)
	replay_camera_controls.add_child(replay_camera_fov_value)
	var speed_label := Label.new()
	speed_label.text = "Speed"
	replay_camera_controls.add_child(speed_label)
	replay_camera_speed_slider = HSlider.new()
	replay_camera_speed_slider.min_value = REPLAY_MANUAL_CAMERA_SPEED_MIN
	replay_camera_speed_slider.max_value = REPLAY_MANUAL_CAMERA_SPEED_MAX
	replay_camera_speed_slider.step = 1.0
	replay_camera_speed_slider.exp_edit = true
	replay_camera_speed_slider.value = replay_manual_camera_speed
	replay_camera_speed_slider.custom_minimum_size.x = 180.0
	replay_camera_speed_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_camera_speed_slider.tooltip_text = "Base movement speed. Hold Shift for 3x speed."
	replay_camera_speed_slider.value_changed.connect(_on_replay_camera_speed_changed)
	replay_camera_controls.add_child(replay_camera_speed_slider)
	replay_camera_speed_value = SpinBox.new()
	replay_camera_speed_value.min_value = REPLAY_MANUAL_CAMERA_SPEED_MIN
	replay_camera_speed_value.max_value = REPLAY_MANUAL_CAMERA_SPEED_MAX
	replay_camera_speed_value.step = 1.0
	replay_camera_speed_value.value = replay_manual_camera_speed
	replay_camera_speed_value.suffix = " u/s"
	replay_camera_speed_value.custom_minimum_size.x = 115.0
	replay_camera_speed_value.tooltip_text = "Base movement speed. Hold Shift for 3x speed."
	replay_camera_speed_value.value_changed.connect(_on_replay_camera_speed_changed)
	replay_camera_controls.add_child(replay_camera_speed_value)
	replay_timeline_resume_reason_label = Label.new()
	replay_timeline_resume_reason_label.modulate = Color(0.78, 0.78, 0.78, 1.0)
	replay_timeline_resume_reason_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows.add_child(replay_timeline_resume_reason_label)


func _replay_resume_eligibility() -> Dictionary:
	if !replay_playback_active:
		return {"eligible": false, "reason": "No replay is currently playing."}
	if !replay_playback_compatible:
		return {"eligible": false, "reason": "Older-version replays cannot resume into Practice."}
	if replay_playback_track_index < 0:
		return {"eligible": false, "reason": "The replay's exact track is unavailable."}
	if replay_playback_racer_ids.is_empty() or replay_playback_focus_index >= replay_playback_racer_ids.size():
		return {"eligible": false, "reason": "The focused replay racer is unavailable."}
	if game_manager.game_sim == null or !game_manager.game_sim.sim_started \
			or !game_manager.game_sim.has_method("get_full_state_data") \
			or !game_manager.game_sim.has_method("load_full_state_data"):
		return {"eligible": false, "reason": "The current replay position has no resumable simulation state."}
	var focus_id := _focused_replay_player_id()
	if game_manager.game_sim.get_finished_player_ids().has(focus_id):
		return {"eligible": false, "reason": "A racer cannot resume after completing the race."}
	if replay_playback_source_metadata.roster == null \
			or replay_playback_source_metadata.roster.count() != replay_playback_racer_ids.size():
		return {"eligible": false, "reason": "The replay does not contain a complete recorded roster."}
	if replay_playback_source_metadata.race_configuration == null:
		return {"eligible": false, "reason": "The replay does not contain resumable race settings."}
	if game_manager._singleplayer_tick < 0 or game_manager._singleplayer_tick > _playback_frame_count():
		return {"eligible": false, "reason": "The current replay cursor is unavailable."}
	return {"eligible": true, "reason": "Continue from this exact frame as an unranked Practice session."}


func _on_replay_resume_practice_pressed() -> void:
	var eligibility := _replay_resume_eligibility()
	if !bool(eligibility.get("eligible", false)):
		race_presentation_controller.show_notification(String(eligibility.get("reason", "Replay cannot resume.")), 4000)
		return
	if replay_resume_dialog == null or !is_instance_valid(replay_resume_dialog):
		replay_resume_dialog = ConfirmationDialog.new()
		replay_resume_dialog.name = "ReplayResumePracticeDialog"
		replay_resume_dialog.title = "Resume in Practice"
		replay_resume_dialog.dialog_text = "Continue from the current replay frame?\nThe focused racer becomes yours; every other racer continues as a CPU."
		replay_resume_dialog.ok_button_text = "Resume"
		replay_resume_dialog.cancel_button_text = "Cancel"
		replay_resume_dialog.exclusive = true
		replay_resume_dialog.confirmed.connect(_on_replay_resume_practice_confirmed)
		replay_resume_keep_original_checkbox = CheckBox.new()
		replay_resume_keep_original_checkbox.text = "Keep Original as Ghost"
		replay_resume_keep_original_checkbox.custom_minimum_size = Vector2(0.0, 30.0)
		replay_resume_dialog.add_child(replay_resume_keep_original_checkbox)
		add_child(replay_resume_dialog)
	var has_future := game_manager._singleplayer_tick < _playback_frame_count()
	replay_resume_keep_original_checkbox.button_pressed = has_future
	replay_resume_keep_original_checkbox.disabled = !has_future
	replay_resume_dialog.popup_centered(Vector2i(650, 210))


func _on_replay_resume_practice_confirmed() -> void:
	var keep_original := replay_resume_keep_original_checkbox != null \
		and !replay_resume_keep_original_checkbox.disabled \
		and replay_resume_keep_original_checkbox.button_pressed
	var payload := _capture_replay_resume_payload(keep_original)
	if payload.is_empty():
		race_presentation_controller.show_notification("The current replay frame could not be captured.", 4000)
		return
	game_manager.call_deferred("resume_replay_in_practice", payload)


func _capture_replay_resume_payload(keep_original: bool) -> Dictionary:
	var eligibility := _replay_resume_eligibility()
	if !bool(eligibility.get("eligible", false)):
		return {}
	var transition_start_usec := Time.get_ticks_usec()
	replay_playback_paused = true
	_apply_replay_playback_clock()
	var cursor := game_manager._singleplayer_tick
	var full_state: PackedByteArray = game_manager.game_sim.get_full_state_data(cursor)
	if full_state.is_empty():
		return {}
	return {
		"transition_start_usec": transition_start_usec,
		"cursor": cursor,
		"full_state": full_state,
		"race_results": game_manager.network_manager.race_results.capture_practice_state(),
		"race_dnf_low_speed_ticks": game_manager.race_dnf_low_speed_ticks.duplicate(true),
		"focused_player_id": _focused_replay_player_id(),
		"track_index": replay_playback_track_index,
		"metadata": replay_playback_source_metadata.to_dictionary(),
		"replay_stream": replay_playback_stream,
		"canonical_prefix_count": cursor,
		"keep_original_as_ghost": keep_original,
	}

func _format_replay_timeline_time(tick_value: int) -> String:
	var total_msec := int(round(float(maxi(tick_value, 0)) * 1000.0 / 60.0))
	var minutes := int(total_msec / 60000)
	var seconds := int(total_msec / 1000) % 60
	var milliseconds := total_msec % 1000
	return "%d:%02d.%03d" % [minutes, seconds, milliseconds]

func _refresh_replay_timeline_save_local_button() -> void:
	if replay_timeline_save_local_button == null:
		return
	var local_path := _staged_replay_local_path(replay_playback_loaded_path)
	replay_timeline_save_local_button.visible = !local_path.is_empty()
	var already_saved := !local_path.is_empty() and FileAccess.file_exists(local_path)
	replay_timeline_save_local_button.disabled = already_saved
	replay_timeline_save_local_button.text = "Saved Locally" if already_saved else "Save Replay Locally"

func _on_replay_timeline_save_local_pressed() -> void:
	var saved_path := save_staged_replay_locally(replay_playback_loaded_path)
	if saved_path.is_empty():
		race_presentation_controller.show_notification("Replay could not be saved", 3000)
		return
	race_presentation_controller.show_notification("Replay Saved", 2200)
	_refresh_replay_timeline_save_local_button()

func _replay_marker_bucket(player_id: int) -> Dictionary:
	if !replay_timeline_markers.has(player_id):
		replay_timeline_markers[player_id] = {
			"death_ko": [],
			"death_fall": [],
			"death_explosion": [],
			"kos": [],
			"laps": [],
			"finishes": [],
			"first_overtakes": [],
			"place_up": [],
			"place_down": [],
		}
	return replay_timeline_markers[player_id]

func _add_replay_timeline_marker(player_id: int, marker_type: String, tick_value: int) -> void:
	tick_value = clampi(tick_value, 0, maxi(_playback_frame_count(), 1))
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
	replay_marker_last_death_states.clear()
	replay_timeline_markers_dirty = true
	for id_value in replay_playback_racer_ids:
		var id := int(id_value)
		_replay_marker_bucket(id)
		var finish_tick := _lookup_replay_tick_for_id(replay_saved_finish_times, id)
		if finish_tick >= 0:
			_add_replay_timeline_marker(id, "finishes", finish_tick)
		if replay_skip_seek_bake_requested:
			var death_tick := _lookup_replay_tick_for_id(replay_saved_eliminations, id)
			if death_tick >= 0:
				_add_replay_timeline_marker(id, "death_explosion", death_tick)

func record_timeline_event(event: Dictionary) -> void:
	if int(event.get("type", 0)) != 1:
		return
	var tick_value := int(event.get("tick", game_manager._singleplayer_tick))
	var attacker_id := int(event.get("actor_id", -1))
	var target_id := int(event.get("target_id", -1))
	if attacker_id >= 0:
		_add_replay_timeline_marker(attacker_id, "kos", tick_value)
	if target_id >= 0:
		_add_replay_timeline_marker(target_id, "death_ko", tick_value)

func _replay_bucket_has_death_at_tick(bucket: Dictionary, tick_value: int) -> bool:
	return (bucket.get("death_ko", []) as Array).has(tick_value) \
		or (bucket.get("death_fall", []) as Array).has(tick_value) \
		or (bucket.get("death_explosion", []) as Array).has(tick_value)

func _update_replay_death_timeline_markers() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_vehicle_death_states"):
		return
	var states: PackedInt32Array = game_manager.game_sim.get_vehicle_death_states()
	for index in range(0, states.size() - 1, 2):
		var id := int(states[index])
		var death_state := int(states[index + 1])
		var previous_state := int(replay_marker_last_death_states.get(id, 0))
		if death_state != 0 and previous_state == 0:
			var tick_value := game_manager._singleplayer_tick
			var bucket := _replay_marker_bucket(id)
			if !_replay_bucket_has_death_at_tick(bucket, tick_value):
				var marker_type := "death_fall" if (death_state & REPLAY_DEATH_FALLOUT) != 0 else "death_explosion"
				_add_replay_timeline_marker(id, marker_type, tick_value)
		replay_marker_last_death_states[id] = death_state

func _update_replay_lap_timeline_markers() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_player_lap"):
		return
	for id_value in replay_playback_racer_ids:
		var id := int(id_value)
		var lap := int(game_manager.game_sim.get_player_lap(id))
		if !replay_marker_last_laps.has(id):
			replay_marker_last_laps[id] = lap
			continue
		var previous_lap := int(replay_marker_last_laps[id])
		if lap > previous_lap:
			for crossed_lap in range(previous_lap + 1, lap + 1):
				if crossed_lap > 0:
					_add_replay_timeline_marker(id, "laps", game_manager._singleplayer_tick)
		replay_marker_last_laps[id] = lap

func _update_replay_placement_timeline_markers() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_player_race_place"):
		return
	for id_value in replay_playback_racer_ids:
		var id := int(id_value)
		var place := int(game_manager.game_sim.get_player_race_place(id))
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
			_add_replay_timeline_marker(id, "place_up", game_manager._singleplayer_tick)
			if place == 1:
				_add_replay_timeline_marker(id, "first_overtakes", game_manager._singleplayer_tick)
		elif place > previous_place:
			_add_replay_timeline_marker(id, "place_down", game_manager._singleplayer_tick)
		replay_marker_last_places[id] = place

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
	replay_marker_last_death_states.clear()
	replay_collecting_timeline_markers = false
	replay_timeline_markers_dirty = true
	_clear_replay_timeline_marker_nodes()

func _timeline_marker_x(tick_value: int) -> float:
	var total_ticks := maxf(float(maxi(_playback_frame_count(), 1)), 1.0)
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

func _add_timeline_death_marker(x: float, texture: Texture2D) -> void:
	var icon := TextureRect.new()
	icon.texture = texture
	icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	icon.size = REPLAY_DEATH_ICON_SIZE
	var half_size := REPLAY_DEATH_ICON_SIZE * 0.5
	var center_x := clampf(x, half_size.x, replay_timeline_track.size.x - half_size.x)
	icon.position = Vector2(center_x - half_size.x, replay_timeline_track.size.y * 0.5 - half_size.y)
	replay_timeline_marker_layer.add_child(icon)

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
	for tick_value in bucket.get("first_overtakes", []):
		_add_timeline_circle_marker(_timeline_marker_x(int(tick_value)), circle_radius, Color(1.0, 0.78, 0.12, 1.0))
	for tick_value in bucket.get("kos", []):
		_add_timeline_circle_marker(_timeline_marker_x(int(tick_value)), circle_radius, Color(1.0, 0.08, 0.05, 1.0))
	for tick_value in bucket.get("death_explosion", []):
		_add_timeline_death_marker(_timeline_marker_x(int(tick_value)), REPLAY_DEATH_EXPLOSION_TEXTURE)
	for tick_value in bucket.get("death_fall", []):
		_add_timeline_death_marker(_timeline_marker_x(int(tick_value)), REPLAY_DEATH_FALL_TEXTURE)
	for tick_value in bucket.get("death_ko", []):
		_add_timeline_death_marker(_timeline_marker_x(int(tick_value)), REPLAY_DEATH_KO_TEXTURE)
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

func _on_replay_input_display_toggled(enabled: bool) -> void:
	replay_input_display_enabled = enabled
	_refresh_replay_input_display()

func _on_replay_hide_hud_toggled(enabled: bool) -> void:
	replay_hide_hud_enabled = enabled
	_apply_replay_hud_visibility()

func _on_replay_camera_fov_changed(value: float) -> void:
	replay_manual_camera_fov = clampf(value, REPLAY_MANUAL_CAMERA_FOV_MIN, REPLAY_MANUAL_CAMERA_FOV_MAX)
	if replay_camera_fov_slider != null:
		replay_camera_fov_slider.set_value_no_signal(replay_manual_camera_fov)
	if replay_camera_fov_value != null:
		replay_camera_fov_value.set_value_no_signal(replay_manual_camera_fov)
	_apply_replay_manual_camera_settings()

func _on_replay_camera_speed_changed(value: float) -> void:
	replay_manual_camera_speed = clampf(value, REPLAY_MANUAL_CAMERA_SPEED_MIN, REPLAY_MANUAL_CAMERA_SPEED_MAX)
	if replay_camera_speed_slider != null:
		replay_camera_speed_slider.set_value_no_signal(replay_manual_camera_speed)
	if replay_camera_speed_value != null:
		replay_camera_speed_value.set_value_no_signal(replay_manual_camera_speed)
	_apply_replay_manual_camera_settings()

func _apply_replay_manual_camera_settings() -> void:
	if replay_relative_camera != null and is_instance_valid(replay_relative_camera):
		replay_relative_camera.fov = replay_manual_camera_fov
	if spectator_controller.spectator != null and is_instance_valid(spectator_controller.spectator):
		var free_camera := spectator_controller.spectator
		free_camera.move_speed = replay_manual_camera_speed
		free_camera.fast_move_speed = replay_manual_camera_speed * REPLAY_MANUAL_CAMERA_FAST_MULTIPLIER
		if free_camera.camera != null:
			free_camera.camera.fov = replay_manual_camera_fov

func _apply_replay_hud_visibility() -> void:
	var hidden := replay_playback_active and replay_hide_hud_enabled
	debug_runtime_controller.set_replay_hud_hidden(hidden)
	var race_hud := race_presentation_controller.local_race_hud() as RaceHud
	if race_hud == null:
		return
	if hidden or debug_runtime_controller.disable_hud or debug_runtime_controller.hide_hud_only:
		race_hud.visible = false
	else:
		race_hud.visible = true

func _refresh_replay_input_display() -> void:
	if replay_input_display_panel == null or replay_input_display == null:
		return
	var should_show := replay_playback_active and replay_input_display_enabled and !game_manager.headless_mode
	replay_input_display_panel.visible = should_show
	if !should_show:
		return
	var focus_id := _focused_replay_player_id()
	if replay_input_display_frame_inputs.has(focus_id):
		replay_input_display.call("set_input_bytes", replay_input_display_frame_inputs[focus_id] as PackedByteArray)
	else:
		replay_input_display.call("clear_input")

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
	_seek_replay_to_tick(roundi(ratio * float(maxi(_playback_frame_count(), 1))))
	_update_replay_timeline_controls()
	get_viewport().set_input_as_handled()

func _step_replay_by_ticks(delta_ticks: int) -> void:
	if !replay_playback_active:
		return
	if delta_ticks == 0:
		return
	replay_playback_paused = true
	_apply_replay_playback_clock()
	var target_tick := clampi(game_manager._singleplayer_tick + delta_ticks, 0, _playback_frame_count())
	if target_tick == game_manager._singleplayer_tick:
		_update_replay_timeline_controls()
		return
	if target_tick < game_manager._singleplayer_tick:
		_seek_replay_to_tick(target_tick, false)
	else:
		while game_manager._singleplayer_tick < target_tick and replay_playback_index < _playback_frame_count():
			if !simulate_playback(false, false, false):
				break
		_apply_replay_focus_to_local_visual()
		if game_manager.game_sim.sim_started:
			game_manager._update_native_render_camera()
			game_manager.game_sim.render_gamesim()
	_update_replay_timeline_controls()

func _update_replay_timeline_controls() -> void:
	if replay_timeline_root == null:
		return
	var should_show := false
	if replay_playback_active:
		var mouse_y := get_viewport().get_mouse_position().y
		var viewport_h := get_viewport().get_visible_rect().size.y
		should_show = mouse_y >= viewport_h - 240.0
		if replay_timeline_panel != null:
			should_show = should_show or replay_timeline_panel.get_global_rect().has_point(get_viewport().get_mouse_position())
	replay_timeline_root.visible = should_show
	var total_ticks := maxi(_playback_frame_count(), 1)
	var current_tick := clampi(game_manager._singleplayer_tick, 0, total_ticks)
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
	if replay_timeline_camera_mode_button != null:
		replay_timeline_camera_mode_button.text = "Camera: %s" % _replay_camera_mode_name()
	if replay_camera_controls != null:
		replay_camera_controls.visible = replay_playback_active and (
			replay_camera_mode == REPLAY_CAMERA_RELATIVE or replay_camera_mode == REPLAY_CAMERA_SPECTATOR)
	if replay_timeline_resume_practice_button != null:
		var eligibility := _replay_resume_eligibility()
		var eligible := bool(eligibility.get("eligible", false))
		var reason := String(eligibility.get("reason", ""))
		replay_timeline_resume_practice_button.disabled = !eligible
		replay_timeline_resume_practice_button.tooltip_text = reason
		if replay_timeline_resume_reason_label != null:
			replay_timeline_resume_reason_label.text = reason

func _start_replay_playback_from_path(path: String, compatibility_warning_accepted := false) -> void:
	var profile_start_us := Time.get_ticks_usec()
	var replay := _load_replay_file(path)
	var loaded_source_bytes := replay_playback_source_bytes
	var profile_load_us := Time.get_ticks_usec() - profile_start_us
	if replay.is_empty():
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	if !_replay_is_compatible(replay) and !compatibility_warning_accepted:
		if game_manager.headless_mode:
			push_warning(REPLAY_COMPATIBILITY_WARNING)
		else:
			_show_replay_compatibility_warning(path)
			return
	if replay_leaderboard_verify_requested:
		replay_leaderboard_validation = LeaderboardReplayValidatorClass.validate(game_manager, replay, replay.get("_replay_stream"))
		if !bool(replay_leaderboard_validation.get("valid", false)):
			print("MXT_LEADERBOARD_VERIFY_FAIL ", JSON.stringify(replay_leaderboard_validation))
			if game_manager.headless_mode:
				get_tree().quit(1)
			return
		if replay.has("legacy_leaderboard_schema_version"):
			replay_leaderboard_validation["replay_schema_version"] = int(replay.get("legacy_leaderboard_schema_version", -1))
	if game_manager.game_sim.sim_started or game_manager.singleplayer_mode:
		game_manager._return_to_menu()
	var track_index := _find_track_index(replay)
	if track_index < 0 or track_index >= game_manager.track_content_controller.tracks.size():
		push_warning("Replay load failed: track not found for %s" % str(replay.get("track_name", "")))
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	var loaded_stream = replay.get("_replay_stream")
	if !(loaded_stream is MxtReplayStream) or (loaded_stream as MxtReplayStream).frame_count() <= 0:
		push_warning("Replay load failed: replay has no frames.")
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or (settings as Array).is_empty():
		push_warning("Replay load failed: replay has no racer settings.")
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	if !_replay_vehicle_content_available(settings as Array):
		push_warning("Replay load failed: exact vehicle gameplay content is unavailable.")
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	var racer_ids: Array = replay.get("racer_ids", [])
	var cpu_flags: Array = replay.get("cpu_flags", [])
	if racer_ids.is_empty():
		for i in range((settings as Array).size()):
			racer_ids.append(i)
			cpu_flags.append(false)
	var metadata_boundary: Dictionary = replay.duplicate(true)
	metadata_boundary["racer_ids"] = racer_ids
	metadata_boundary["cpu_flags"] = cpu_flags
	var profile_validate_us := Time.get_ticks_usec() - profile_start_us - profile_load_us
	replay_playback_active = true
	replay_playback_stream = loaded_stream as MxtReplayStream
	replay_playback_source_metadata = MxtReplayRunMetadata.new()
	if !replay_playback_source_metadata.load_dictionary(metadata_boundary):
		push_warning("Replay load failed: %s" % replay_playback_source_metadata.get_last_error())
		replay_playback_active = false
		return
	replay_playback_compatible = _replay_is_compatible(replay)
	replay_playback_track_index = track_index
	replay_playback_source_bytes = loaded_source_bytes
	var profile_frames_duplicate_us := Time.get_ticks_usec() - profile_start_us - profile_load_us - profile_validate_us
	replay_playback_index = 0
	replay_playback_loaded_path = path
	_refresh_replay_timeline_save_local_button()
	replay_playback_racer_ids = racer_ids.duplicate(true)
	replay_playback_cpu_flags = cpu_flags.duplicate(true)
	replay_saved_finish_times = (replay.get("finish_times", {}) as Dictionary).duplicate(true) if typeof(replay.get("finish_times", {})) == TYPE_DICTIONARY else {}
	replay_saved_finish_placements = (replay.get("finish_placements", {}) as Dictionary).duplicate(true) if typeof(replay.get("finish_placements", {})) == TYPE_DICTIONARY else {}
	replay_saved_eliminations = (replay.get("eliminations", {}) as Dictionary).duplicate(true) if typeof(replay.get("eliminations", {})) == TYPE_DICTIONARY else {}
	replay_start_grid_slots = PackedInt32Array()
	replay_start_grid_slots = replay_playback_source_metadata.start_grid_slots
	replay_playback_focus_index = 0
	replay_input_display_frame_inputs = {}
	replay_playback_local_player_id = int(replay_playback_racer_ids[0])
	replay_playback_use_multiplayer_startup = replay_playback_source_metadata.source == "server" or replay_playback_source_metadata.mode == "Multiplayer"
	replay_playback_use_singleplayer_tick = replay_playback_source_metadata.source == "singleplayer"
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_seek_checkpoints.clear()
	replay_collecting_timeline_markers = false
	replay_seeking_active = false
	replay_normal_playback_tick_active = false
	replay_camera_mode = REPLAY_CAMERA_GAME
	game_manager.singleplayer_mode = true
	game_manager._singleplayer_tick = 0
	game_manager.network_manager.reset_race_state()
	game_manager.network_manager.set_spawn_seed(replay_playback_source_metadata.spawn_seed)
	game_manager.network_manager.load_race_metadata_dictionary(replay_playback_source_metadata.to_dictionary().get("race_options", {}))
	game_manager.network_manager.player_ids.clear()
	game_manager.network_manager.lobby_settings.cpu_player_ids.clear()
	for i in range(replay_playback_racer_ids.size()):
		var id := int(replay_playback_racer_ids[i])
		var is_cpu := i < replay_playback_cpu_flags.size() and bool(replay_playback_cpu_flags[i])
		if is_cpu:
			game_manager.network_manager.lobby_settings.cpu_player_ids.append(id)
		else:
			game_manager.network_manager.player_ids.append(id)
		if i < (settings as Array).size() and typeof(settings[i]) == TYPE_DICTIONARY:
			game_manager.network_manager.lobby_settings.set_player_settings(id, settings[i], is_cpu)
	var profile_setup_us := Time.get_ticks_usec() - profile_start_us - profile_load_us - profile_validate_us - profile_frames_duplicate_us
	var profile_race_start_us := Time.get_ticks_usec()
	game_manager._close_settings_menus_for_race_start()
	game_manager.race_dnf_low_speed_ticks.clear()
	var race_roster: MxtRaceRoster = _race_roster_from_settings(settings as Array, replay_playback_racer_ids, replay_playback_cpu_flags)
	if race_roster == null:
		return
	race_session_controller.start_race(track_index, race_roster, game_manager.singleplayer_mode, game_manager.headless_mode)
	game_manager.game_sim.set_sim_started(true)
	profile_race_start_us = Time.get_ticks_usec() - profile_race_start_us
	game_manager.get_node("Control").visible = false
	game_manager.lobby_control.visible = false
	if replay_catalog_root != null:
		replay_catalog_root.visible = false
	_apply_replay_focus_to_local_visual()
	_refresh_replay_input_display()
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
			" frames=", _playback_frame_count(),
			" racers=", replay_playback_racer_ids.size(),
			" skip_bake=", replay_skip_seek_bake_requested)
	print("MXT_REPLAY playback started ", path, " frames=", _playback_frame_count())
	if game_manager.headless_mode:
		var replay_fast_forward_start_us := Time.get_ticks_usec()
		while replay_playback_active and replay_playback_index < _playback_frame_count():
			if !simulate_playback(false, false, false):
				get_tree().quit(1)
				return
			if replay_strict_verify_requested:
				game_manager._check_race_finished()
		var replay_fast_forward_elapsed_us := Time.get_ticks_usec() - replay_fast_forward_start_us
		var replay_frame_count := _playback_frame_count()
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
		if replay_leaderboard_verify_requested:
			var leaderboard_result := _leaderboard_verified_result()
			if !bool(leaderboard_result.get("valid", false)):
				print("MXT_LEADERBOARD_VERIFY_FAIL ", JSON.stringify(leaderboard_result))
				get_tree().quit(1)
				return
			print("MXT_LEADERBOARD_VERIFY_RESULT ", JSON.stringify(leaderboard_result))
		get_tree().quit()

func _capture_replay_seek_checkpoint(next_tick: int) -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_full_state_data"):
		return
	for checkpoint in replay_seek_checkpoints:
		if int((checkpoint as Dictionary).get("tick", -1)) == next_tick:
			return
	var state: PackedByteArray = game_manager.game_sim.get_full_state_data(next_tick)
	if state.is_empty():
		return
	replay_seek_checkpoints.append({
		"tick": next_tick,
		"index": replay_playback_index,
		"state": state,
		"finish_times": game_manager.network_manager.race_results.player_finish_times.duplicate(true),
		"finish_placements": game_manager.network_manager.race_results.player_finish_placements.duplicate(true),
		"eliminations": game_manager.network_manager.race_results.player_eliminations.duplicate(true),
	})
	replay_seek_checkpoint_bytes += state.size()

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
	game_manager.network_manager.race_results.player_finish_times = (checkpoint.get("finish_times", {}) as Dictionary).duplicate(true)
	game_manager.network_manager.race_results.player_finish_placements = (checkpoint.get("finish_placements", {}) as Dictionary).duplicate(true)
	game_manager.network_manager.race_results.player_eliminations = (checkpoint.get("eliminations", {}) as Dictionary).duplicate(true)
	game_manager.network_manager.race_results.rebuild_finish_order()
	game_manager.network_manager.race_results.net_race_finish_time = -1

func _reset_replay_netcode_session() -> void:
	if !replay_playback_active:
		return
	game_manager.network_manager.input_transport.netcode_session.configure(
		replay_playback_racer_ids,
		replay_playback_cpu_flags,
		game_manager._local_player_id()
	)

func _bake_replay_seek_checkpoints() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_full_state_data") or !game_manager.game_sim.has_method("load_full_state_data"):
		return
	replay_seeking_active = true
	replay_collecting_timeline_markers = true
	replay_marker_last_laps.clear()
	replay_marker_last_places.clear()
	replay_marker_last_death_states.clear()
	_update_replay_lap_timeline_markers()
	_update_replay_placement_timeline_markers()
	_update_replay_death_timeline_markers()
	_capture_replay_seek_checkpoint(0)
	while replay_playback_index < _playback_frame_count():
		if !simulate_playback(false, false, false):
			break
		if (game_manager._singleplayer_tick % REPLAY_SEEK_CHECKPOINT_INTERVAL) == 0:
			_capture_replay_seek_checkpoint(game_manager._singleplayer_tick)
	_capture_replay_seek_checkpoint(game_manager._singleplayer_tick)
	replay_collecting_timeline_markers = false
	_seek_replay_to_tick(0, false)
	replay_seeking_active = false

func _seek_replay_to_tick(target_tick: int, show_notice: bool = true) -> bool:
	if !replay_playback_active or game_manager.game_sim == null or !game_manager.game_sim.has_method("load_full_state_data"):
		return false
	target_tick = clampi(target_tick, 0, _playback_frame_count())
	var checkpoint := _find_replay_seek_checkpoint(target_tick)
	if checkpoint.is_empty():
		return false
	var checkpoint_tick := int(checkpoint.get("tick", 0))
	var state: PackedByteArray = checkpoint.get("state", PackedByteArray())
	if state.is_empty() or !game_manager.game_sim.load_full_state_data(checkpoint_tick, state):
		push_warning("Replay seek failed: could not load full checkpoint at tick %d" % checkpoint_tick)
		return false
	_restore_replay_race_event_state(checkpoint)
	_reset_replay_netcode_session()
	game_manager._singleplayer_tick = checkpoint_tick
	replay_playback_index = int(checkpoint.get("index", checkpoint_tick))
	replay_input_display_frame_inputs = {}
	replay_seeking_active = true
	while game_manager._singleplayer_tick < target_tick and replay_playback_index < _playback_frame_count():
		if !simulate_playback(false, false, false):
			break
	replay_seeking_active = false
	_refresh_replay_input_display()
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	_apply_replay_focus_to_local_visual()
	if game_manager.game_sim.sim_started:
		game_manager._update_native_render_camera()
		game_manager.game_sim.render_gamesim()
	if show_notice:
		race_presentation_controller.show_notification("Replay: %s" % _format_replay_timeline_time(game_manager._singleplayer_tick), 900)
	return true

func should_enqueue_replay_race_notification() -> bool:
	return !replay_playback_active or replay_normal_playback_tick_active

func simulate_playback(handle_terminal_state: bool = true, respect_pause: bool = true, enqueue_event_notifications: bool = true) -> bool:
	if respect_pause and replay_playback_paused:
		return true
	if replay_playback_index >= _playback_frame_count():
		if handle_terminal_state:
			print("MXT_REPLAY playback complete ", replay_playback_loaded_path)
			if game_manager.headless_mode:
				get_tree().quit()
			else:
				replay_playback_paused = true
				_apply_replay_playback_clock()
				_update_replay_timeline_controls()
		return false
	var frame := _decoded_replay_frame_at(replay_playback_index)
	if frame.is_empty():
		if handle_terminal_state:
			if game_manager.headless_mode:
				get_tree().quit(1)
			else:
				game_manager._return_to_menu()
		return false
	var frame_tick := int(frame.get("tick", replay_playback_index))
	if frame_tick != game_manager._singleplayer_tick:
		push_warning("Replay playback refused: expected tick %d, found saved tick %d" % [game_manager._singleplayer_tick, frame_tick])
		if handle_terminal_state:
			if game_manager.headless_mode:
				get_tree().quit(1)
			else:
				game_manager._return_to_menu()
		return false
	var frame_inputs: Dictionary = frame.get("inputs", {})
	replay_input_display_frame_inputs = frame_inputs
	if !replay_seeking_active and !replay_collecting_timeline_markers:
		_refresh_replay_input_display()
	replay_normal_playback_tick_active = enqueue_event_notifications
	if replay_playback_use_singleplayer_tick:
		var local_id := game_manager._local_player_id()
		var local_input: PackedByteArray = frame_inputs.get(local_id, game_manager.network_manager.input_transport.NEUTRAL_INPUT_BYTES)
		game_manager.game_sim.tick_singleplayer(local_id, local_input)
	else:
		for id_value in frame_inputs.keys():
			game_manager.network_manager.input_transport.netcode_session.store_pending_input(game_manager._singleplayer_tick, int(id_value), frame_inputs[id_value])
		if !game_manager.network_manager.input_transport.netcode_session.tick_server_frame(game_manager.game_sim, game_manager._singleplayer_tick, true):
			replay_normal_playback_tick_active = false
			push_warning("Replay playback failed at tick %d" % game_manager._singleplayer_tick)
			if handle_terminal_state:
				if game_manager.headless_mode:
					get_tree().quit(1)
				else:
					game_manager._return_to_menu()
			return false
	game_manager._consume_authoritative_race_events()
	if replay_collecting_timeline_markers:
		_update_replay_lap_timeline_markers()
		_update_replay_placement_timeline_markers()
		_update_replay_death_timeline_markers()
	replay_playback_index += 1
	game_manager._singleplayer_tick += 1
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	game_manager._check_race_finished()
	replay_normal_playback_tick_active = false
	if !replay_seeking_active and (game_manager._singleplayer_tick % REPLAY_SEEK_CHECKPOINT_INTERVAL) == 0:
		_capture_replay_seek_checkpoint(game_manager._singleplayer_tick)
	return true

func _ensure_replay_auto_camera() -> Camera3D:
	if replay_auto_camera == null or !is_instance_valid(replay_auto_camera):
		replay_auto_camera = Camera3D.new()
		replay_auto_camera.name = "ReplayAutoCamera"
		replay_auto_camera.near = 0.25
		replay_auto_camera.far = 40000.0
		replay_auto_camera.fov = 70.0
		game_manager.get_node("GameWorld").add_child(replay_auto_camera)
	return replay_auto_camera

func _ensure_replay_relative_camera() -> Camera3D:
	if replay_relative_camera == null or !is_instance_valid(replay_relative_camera):
		replay_relative_camera = Camera3D.new()
		replay_relative_camera.name = "ReplayRelativeCamera"
		replay_relative_camera.near = 0.25
		replay_relative_camera.far = 40000.0
		replay_relative_camera.fov = replay_manual_camera_fov
		game_manager.get_node("GameWorld").add_child(replay_relative_camera)
	return replay_relative_camera

func _focused_replay_player_id() -> int:
	if replay_playback_racer_ids.is_empty():
		return game_manager._local_player_id()
	replay_playback_focus_index = clampi(replay_playback_focus_index, 0, replay_playback_racer_ids.size() - 1)
	return int(replay_playback_racer_ids[replay_playback_focus_index])

func _focused_replay_car() -> VisualCar:
	var focus_id := _focused_replay_player_id()
	if game_manager.car_node_container.local_visual_car != null and game_manager.car_node_container.local_visual_car.owning_id == focus_id:
		return game_manager.car_node_container.local_visual_car
	for car in game_manager.car_node_container.get_children():
		if car is VisualCar and car.owning_id == focus_id:
			return car
	return null

func _apply_replay_focus_to_local_visual() -> void:
	if !replay_playback_active or game_manager.car_node_container.local_visual_car == null:
		return
	var focus_id := _focused_replay_player_id()
	var car := game_manager.car_node_container.local_visual_car
	car.owning_id = focus_id
	car.race_hud.focus_player_id = focus_id
	var settings: Dictionary = game_manager.network_manager.lobby_settings.get_player_settings(focus_id)
	if !settings.is_empty():
		var ps := vehicle_content_controller.player_settings_for_stamp_render(settings)
		if ps != null:
			car.player_settings = ps
	if is_instance_valid(car.name_label):
		car.name_label.text = race_presentation_controller.player_display_name(focus_id)
	_apply_replay_hud_visibility()
	if !debug_runtime_controller.disable_hud and !debug_runtime_controller.disable_hud_process_only:
		car.race_hud.process_mode = Node.PROCESS_MODE_INHERIT

func _focused_replay_transform() -> Transform3D:
	if game_manager.game_sim != null and game_manager.game_sim.has_method("get_player_physical_render_transform"):
		return game_manager.game_sim.get_player_physical_render_transform(_focused_replay_player_id())
	if game_manager.game_sim != null and game_manager.game_sim.has_method("get_player_render_transform"):
		return game_manager.game_sim.get_player_render_transform(_focused_replay_player_id())
	var car := _focused_replay_car()
	if car != null:
		return Transform3D(car.basis_physical.basis, car.position_current)
	return Transform3D.IDENTITY

func _focused_replay_up() -> Vector3:
	if game_manager.game_sim != null and game_manager.game_sim.has_method("get_player_physical_render_up"):
		var native_up: Vector3 = game_manager.game_sim.get_player_physical_render_up(_focused_replay_player_id())
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
	if replay_camera_mode == REPLAY_CAMERA_GAME and game_manager.car_node_container.local_visual_car != null:
		spectator_controller.disable_free_camera()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		game_manager.game_sim.set_gameplay_camera(game_manager.car_node_container.local_visual_car.car_camera, _focused_replay_player_id())
		game_manager.car_node_container.local_visual_car.car_camera.make_current()
		game_manager.car_node_container.local_visual_car.make_vehicle_audio_listener_current()
	elif replay_camera_mode == REPLAY_CAMERA_AUTO:
		spectator_controller.disable_free_camera()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_ensure_replay_auto_camera().make_current()
	elif replay_camera_mode == REPLAY_CAMERA_RELATIVE:
		spectator_controller.disable_free_camera()
		_reset_replay_relative_camera()
		_ensure_replay_relative_camera().make_current()
		_apply_replay_manual_camera_settings()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	else:
		var focus_transform := _focused_replay_transform()
		spectator_controller.show_free_camera_at(focus_transform)
		_apply_replay_manual_camera_settings()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_update_replay_timeline_controls()

func _cycle_replay_camera_mode() -> void:
	replay_camera_mode = (replay_camera_mode + 1) % 4
	_apply_replay_camera_mode()
	race_presentation_controller.show_notification("Replay Camera: %s" % _replay_camera_mode_name(), 1200)

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
	_refresh_replay_input_display()
	replay_timeline_markers_dirty = true
	race_presentation_controller.show_notification("Replay Focus: %s" % race_presentation_controller.player_display_name(_focused_replay_player_id()), 1200)

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
	if Input.is_physical_key_pressed(KEY_SPACE):
		move_input.y += 1.0
	if Input.is_physical_key_pressed(KEY_CTRL):
		move_input.y -= 1.0
	move_input.x += _replay_action_axis("MoveLeft", "MoveRight")
	move_input.x += _replay_action_axis("SteerLeft", "SteerRight")
	move_input.z += _replay_action_axis("MoveForward", "MoveBack")
	move_input.z += _replay_action_axis("SteerUp", "SteerDown")
	if move_input.length_squared() > 1.0:
		move_input = move_input.normalized()
	var current_speed := replay_manual_camera_speed * REPLAY_MANUAL_CAMERA_FAST_MULTIPLIER \
		if Input.is_physical_key_pressed(KEY_SHIFT) else replay_manual_camera_speed
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
	_ensure_replay_interface_layer().add_child(replay_catalog_root)
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
	game_manager.get_node("Control").visible = false
	game_manager.lobby_control.visible = false
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
			if !dir.current_is_dir() and file_name.ends_with(REPLAY_FILE_SUFFIX):
				var path := replay_dir.path_join(file_name)
				var parsed := _load_replay_metadata_file(path)
				if !parsed.is_empty():
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
	if !game_manager.game_sim.sim_started:
		game_manager.get_node("Control").visible = true

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
	var replay_profiles: Array = []
	var invalid_count := 0
	var total_bytes := 0
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if !dir.current_is_dir() and file_name.ends_with(REPLAY_FILE_SUFFIX):
			var path := replay_dir.path_join(file_name)
			var replay_profile := {"file": file_name}
			var replay_start_usec := Time.get_ticks_usec()
			var data := _load_replay_metadata_file(path, replay_profile)
			replay_profile["duration_usec"] = Time.get_ticks_usec() - replay_start_usec
			replay_profile["valid"] = !data.is_empty()
			total_bytes += int(replay_profile.get("bytes", 0))
			replay_profiles.append(replay_profile)
			if !data.is_empty():
				data["_path"] = path
				replay_catalog_entries.append(data)
			else:
				invalid_count += 1
		file_name = dir.get_next()
	dir.list_dir_end()
	replay_profiles.sort_custom(
		func(a: Dictionary, b: Dictionary): return int(a.get("duration_usec", 0)) > int(b.get("duration_usec", 0)))
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
			str(p.get("vehicle_content_id", "")),
			stamp_count
		])
	var schema_supported := _replay_schema_is_supported(entry)
	var compatibility_status := "current"
	if !schema_supported:
		compatibility_status = "unsupported replay format"
	elif !_replay_is_compatible(entry):
		compatibility_status = "older version (may desync)"
	replay_catalog_metadata_label.text = "\n".join([
		"Track: %s" % str(entry.get("track_name", "")),
		"Mode: %s" % str(entry.get("mode", "")),
		"Game version: %s" % str(entry.get("build", "")),
		"Godot version: %s" % str(entry.get("engine_version", "")),
		"Duration: %s" % race_presentation_controller.format_race_time(int(entry.get("duration_ticks", 0)), 0),
		"Players:",
		"\n".join(player_lines),
		"",
		"Compatibility: %s" % compatibility_status,
		str(entry.get("_path", "")),
	])
	_update_replay_catalog_buttons()

func _update_replay_catalog_buttons() -> void:
	var entry := _selected_replay_catalog_entry()
	var has_entry := !entry.is_empty()
	var schema_supported := has_entry and _replay_schema_is_supported(entry)
	if replay_catalog_watch_button != null:
		replay_catalog_watch_button.disabled = !schema_supported
	if replay_catalog_rename_button != null:
		replay_catalog_rename_button.disabled = !has_entry
	if replay_catalog_delete_button != null:
		replay_catalog_delete_button.disabled = !has_entry

func _on_replay_catalog_watch_pressed() -> void:
	var entry := _selected_replay_catalog_entry()
	if entry.is_empty():
		return
	_request_replay_playback_from_path(str(entry.get("_path", "")))

func _on_replay_catalog_rename_pressed() -> void:
	var entry := _selected_replay_catalog_entry()
	if entry.is_empty():
		return
	var path := str(entry.get("_path", ""))
	var stream := MxtReplayStream.new()
	if !stream.load_file(path, true):
		return
	var data: Dictionary = stream.get_metadata()
	data["name"] = replay_catalog_name_edit.text.strip_edges()
	if str(data["name"]) == "":
		data["name"] = str(data.get("track_name", "Replay"))
	if !stream.rewrite_metadata(path, data):
		push_warning("Replay rename failed: %s" % stream.get_last_error())
		return
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

func _current_track_name() -> String:
	if race_session_controller.last_race_track_index >= 0 and race_session_controller.last_race_track_index < game_manager.track_content_controller.tracks.size():
		return String(game_manager.track_content_controller.tracks[race_session_controller.last_race_track_index].get("name", "track"))
	return "track"

func _current_track_id() -> String:
	if race_session_controller.last_race_track_index >= 0 and race_session_controller.last_race_track_index < game_manager.track_content_controller.tracks.size():
		return game_manager.track_content_controller.track_id_for_index(race_session_controller.last_race_track_index)
	return ""

func _current_track_gameplay_digest() -> String:
	if race_session_controller.last_race_track_index >= 0 and race_session_controller.last_race_track_index < game_manager.track_content_controller.tracks.size():
		return game_manager.track_content_controller.track_gameplay_digest_for_index(race_session_controller.last_race_track_index)
	return ""

func _find_track_index(data: Dictionary) -> int:
	var replay_track_id := String(data.get("track_content_id", ""))
	var replay_gameplay_digest := String(data.get("track_gameplay_digest", ""))
	if replay_track_id.is_empty() or replay_gameplay_digest.is_empty():
		return -1
	var track_index := game_manager.track_content_controller.track_index_for_id(replay_track_id)
	if track_index < 0:
		return -1
	if game_manager.track_content_controller.track_gameplay_digest_for_index(track_index) != replay_gameplay_digest:
		return -1
	var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(replay_track_id)
	if record == null:
		return -1
	var record_package_digest := record.package_digest
	if !record_package_digest.is_empty() and String(data.get("track_package_digest", "")) != record_package_digest:
		return -1
	var record_workshop_id := str(record.published_file_id) if record.published_file_id > 0 else ""
	if !record_workshop_id.is_empty() and String(data.get("track_workshop_id", "")) != record_workshop_id:
		return -1
	return track_index

func _replay_vehicle_content_available(settings: Array) -> bool:
	for settings_value in settings:
		if typeof(settings_value) != TYPE_DICTIONARY:
			return false
		var player_settings: Dictionary = settings_value
		var content_id := String(player_settings.get("vehicle_content_id", ""))
		var gameplay_digest := String(player_settings.get("vehicle_gameplay_digest", ""))
		if content_id.is_empty() or gameplay_digest.is_empty():
			return false
		var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(content_id)
		if record == null or record.gameplay_digest != gameplay_digest:
			return false
		var record_package_digest := record.package_digest
		if !record_package_digest.is_empty() and String(player_settings.get("vehicle_package_digest", "")) != record_package_digest:
			return false
		var record_workshop_id := str(record.published_file_id) if record.published_file_id > 0 else ""
		if !record_workshop_id.is_empty() and String(player_settings.get("vehicle_workshop_id", "")) != record_workshop_id:
			return false
	return true

func _start_debug_replay_recording() -> void:
	if !game_manager.singleplayer_mode or !game_manager.game_sim.sim_started:
		print("MXT_DEBUG_REPLAY record ignored: start a singleplayer race first.")
		return
	if game_manager._singleplayer_tick <= 0:
		print("MXT_DEBUG_REPLAY record ignored: wait one physics tick, then press F5 again.")
		return
	debug_replay_snapshot_tick = game_manager._singleplayer_tick - 1
	debug_replay_snapshot_state = game_manager.game_sim.get_state_data(debug_replay_snapshot_tick)
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
	var track_record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(_current_track_id())
	var replay := {
		"version": DEBUG_REPLAY_VERSION,
		"created_unix": Time.get_unix_time_from_system(),
		"track_content_id": _current_track_id(),
		"track_gameplay_digest": _current_track_gameplay_digest(),
		"track_package_digest": track_record.package_digest if track_record != null else "",
		"track_workshop_id": str(track_record.published_file_id) if track_record != null and track_record.published_file_id > 0 else "",
		"track_name": _current_track_name(),
		"settings": _settings_array_with_vehicle_content_evidence(race_session_controller.last_race_settings),
		"singleplayer_cpu_count": game_manager.singleplayer_cpu_count,
		"spawn_seed": game_manager.network_manager.spawn_seed,
		"snapshot_tick": debug_replay_snapshot_tick,
		"snapshot_state_b64": Marshalls.raw_to_base64(debug_replay_snapshot_state),
		"inputs_b64": input_b64,
	}
	var safe_track := _current_track_name().replace("/", "_").replace("\\", "_").replace(" ", "_")
	var path := replay_dir.path_join("mxt_%s_%s.json" % [safe_track, _replay_make_stamp()])
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
	if game_manager.headless_mode:
		get_tree().quit(1)

func _load_and_start_debug_replay(path: String) -> void:
	var replay := _load_debug_replay_file(path)
	if replay.is_empty():
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	if debug_replay_recording:
		_stop_and_save_debug_replay_recording()
	if game_manager.game_sim.sim_started or game_manager.singleplayer_mode:
		game_manager._return_to_menu()
	var track_index := _find_track_index(replay)
	if track_index < 0 or track_index >= game_manager.track_content_controller.tracks.size():
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: track not found for %s" % replay.get("track_name", ""))
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or settings.is_empty():
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: replay has no racer settings.")
		return
	if !_replay_vehicle_content_available(settings as Array):
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: exact vehicle gameplay content is unavailable.")
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

	game_manager.singleplayer_mode = true
	game_manager._singleplayer_tick = 0
	game_manager.network_manager.reset_race_state()
	game_manager.network_manager.set_spawn_seed(int(replay.get("spawn_seed", 0)))
	var local_id := game_manager._local_player_id()
	game_manager.network_manager.player_ids = [local_id]
	game_manager.network_manager.spectator_ids = []
	game_manager.singleplayer_cpu_count = maxi(0, settings.size() - 1)
	game_manager.network_manager.lobby_settings.set_cpu_driver_count(game_manager.singleplayer_cpu_count)
	game_manager.network_manager.lobby_settings.set_player_settings(local_id, settings[0])
	var cpu_ids := game_manager.network_manager.lobby_settings.get_cpu_roster()
	for i in range(cpu_ids.size()):
		if i + 1 < settings.size():
			game_manager.network_manager.lobby_settings.set_player_settings(cpu_ids[i], settings[i + 1], true)

	game_manager._close_settings_menus_for_race_start()
	game_manager.race_dnf_low_speed_ticks.clear()
	var debug_racer_ids: Array = [local_id]
	debug_racer_ids.append_array(cpu_ids)
	var debug_cpu_flags: Array = [false]
	for _cpu_id in cpu_ids:
		debug_cpu_flags.append(true)
	var race_roster: MxtRaceRoster = _race_roster_from_settings(settings, debug_racer_ids, debug_cpu_flags)
	if race_roster == null:
		game_manager._return_to_menu()
		return
	race_session_controller.start_race(track_index, race_roster, game_manager.singleplayer_mode, game_manager.headless_mode)
	if !game_manager.game_sim.load_state_data(snapshot_tick, snapshot_state):
		game_manager._return_to_menu()
		_debug_replay_load_failed("MXT_DEBUG_REPLAY load failed: native snapshot could not be applied.")
		return
	game_manager._singleplayer_tick = snapshot_tick + 1
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	debug_replay_playback_index = 0
	debug_replay_playback = true
	debug_replay_loaded_path = path
	game_manager.get_node("Control").visible = false
	game_manager.lobby_control.visible = false
	print("MXT_DEBUG_REPLAY playback started ", path, " start_tick=", game_manager._singleplayer_tick, " frames=", debug_replay_playback_inputs.size())
