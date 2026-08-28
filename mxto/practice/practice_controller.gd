class_name PracticeController
extends Node

const InputEditorClass = preload("res://practice/practice_input_editor.gd")

signal session_started(configuration: MxtRaceConfiguration)
signal session_ended
signal game_speed_changed(speed: float)

const SPEED_STEP := 0.05
const SPEED_STEP_COUNT := 40
const DEFAULT_SPEED_INDEX := 20
const REWIND_CAPACITY := 45
const SLOT_COUNT := 16
const TELEMETRY_UPDATE_SECONDS := 0.1

enum TelemetryMode {
	OFF,
	COMPACT,
	EXPANDED,
}

enum TelemetryField {
	TICK,
	LAP,
	TARGET_LAPS,
	SPEED_KMH,
	FORWARD_KMH,
	LATERAL_KMH,
	VERTICAL_KMH,
	PITCH_DEGREES_PER_SECOND,
	YAW_DEGREES_PER_SECOND,
	ROLL_DEGREES_PER_SECOND,
	ENERGY,
	MAX_ENERGY,
	TURBO,
	MANUAL_BOOST_ACTIVE,
	DASH_BOOST_ACTIVE,
	S_BOOST_ACTIVE,
	DRIFT_CORNER_MASK,
	TECHNIQUE_LAYER,
	TECHNIQUE_INTENSITY,
	HEIGHT_ABOVE_TRACK,
	CHECKPOINT,
	CHECKPOINT_FRACTION,
	MACHINE_STATE_LOW,
	MACHINE_STATE_HIGH,
	TERRAIN_STATE_LOW,
	TERRAIN_STATE_HIGH,
	INPUT_STRAFE_LEFT_RAW,
	INPUT_STRAFE_RIGHT_RAW,
	INPUT_STEER_HORIZONTAL_RAW,
	INPUT_STEER_VERTICAL_RAW,
	INPUT_ACCELERATE,
	INPUT_BRAKE,
	INPUT_SPIN_ATTACK,
	INPUT_BOOST,
	INPUT_SIDE_ATTACK,
	BASE_SPEED_KMH,
	LAP_PROGRESS,
	COLLISION_CHECKPOINT,
	LAST_GROUND_CHECKPOINT,
	SAMPLE_SIZE,
}

var game_manager: GameManager
var session_active := false
var session_serial := 0
var session_options: MxtRaceConfiguration
var session_completed := false
var game_speed_index := DEFAULT_SPEED_INDEX
var pause_freeze_active := false
var pending_frame_advance := false
var pending_frame_rewind := false
var frame_stick_direction := 0
var pause_speed_stick_direction := 0
var preserved_retry_speed_index := -1
var game_speed_button: Button
var input_mode_button: Button
var input_editor_button: Button
var telemetry_button: Button
var hud_root: CanvasLayer
var hud_label: Label
var input_editor_root: CanvasLayer
var input_editor: InputEditorClass
var timeline: MxtReplayStream = MxtReplayStream.new()
var timeline_racer_ids: Array = []
var timeline_cpu_flags: Array = []
var timeline_enabled := false
var manual_input_mode := false
var input_editor_visible := false
var telemetry_mode := TelemetryMode.OFF
var telemetry_update_accumulator := 0.0
var telemetry_sample_count := 0
var telemetry_format_count := 0
var telemetry_text := ""
var last_live_input_bytes := PackedByteArray([0])
var local_player_id_override := -1
var replay_resume_transition_usec := 0
var companion_ring: Array = []
var slots: Array = []
var selected_slot := 0
var slot_creation_sequence := 0
var latest_companion_tick := -1
var last_slot_capture_usec := 0
var last_slot_restore_usec := 0
var aggregate_slot_bytes := 0


func initialize(
	in_game_manager: GameManager,
	in_game_speed_button: Button = null,
	in_input_mode_button: Button = null,
	in_input_editor_button: Button = null,
	in_telemetry_button: Button = null,
	in_hud_root: CanvasLayer = null,
	in_hud_label: Label = null,
	in_input_editor_root: CanvasLayer = null,
	in_input_editor: InputEditorClass = null
) -> void:
	game_manager = in_game_manager
	game_speed_button = in_game_speed_button
	input_mode_button = in_input_mode_button
	input_editor_button = in_input_editor_button
	telemetry_button = in_telemetry_button
	hud_root = in_hud_root
	hud_label = in_hud_label
	input_editor_root = in_input_editor_root
	input_editor = in_input_editor
	companion_ring.resize(REWIND_CAPACITY)
	slots.resize(SLOT_COUNT)
	if game_speed_button != null:
		game_speed_button.pressed.connect(increase_game_speed)
		game_speed_button.gui_input.connect(_on_game_speed_gui_input)
	if input_mode_button != null:
		input_mode_button.pressed.connect(toggle_manual_input_mode)
	if input_editor_button != null:
		input_editor_button.pressed.connect(toggle_input_editor)
	if telemetry_button != null:
		telemetry_button.pressed.connect(cycle_telemetry_mode)
	if input_editor != null:
		input_editor.manual_mode_requested.connect(set_manual_input_mode)
		input_editor.capture_live_requested.connect(capture_current_live_input)
		input_editor.rewind_requested.connect(request_frame_rewind)
		input_editor.step_requested.connect(request_frame_advance)
	_update_game_speed_button()
	_update_input_controls()
	_reset_session_storage()


func begin_session(configuration: MxtRaceConfiguration) -> bool:
	if configuration == null or !configuration.is_practice():
		return false
	if session_active:
		end_session()
	if preserved_retry_speed_index >= 0:
		game_speed_index = preserved_retry_speed_index
	else:
		game_speed_index = DEFAULT_SPEED_INDEX
	preserved_retry_speed_index = -1
	session_serial += 1
	session_options = configuration.copy()
	local_player_id_override = configuration.practice_local_player_id
	timeline_enabled = configuration.lap_count > 0
	timeline = MxtReplayStream.new()
	if timeline_enabled:
		timeline.begin_recording(timeline_racer_ids, timeline_cpu_flags)
		if !timeline.get_last_error().is_empty():
			push_error("Practice timeline rejected its replay roster: %s" % timeline.get_last_error())
			return false
	session_active = true
	session_completed = false
	pause_freeze_active = false
	pending_frame_advance = false
	pending_frame_rewind = false
	frame_stick_direction = 0
	pause_speed_stick_direction = 0
	manual_input_mode = false
	input_editor_visible = false
	telemetry_mode = TelemetryMode.OFF
	telemetry_update_accumulator = 0.0
	telemetry_sample_count = 0
	telemetry_format_count = 0
	telemetry_text = ""
	replay_resume_transition_usec = 0
	last_live_input_bytes = PackedByteArray([0])
	_reset_session_storage()
	if hud_root != null:
		hud_root.visible = true
	if input_editor_root != null:
		input_editor_root.visible = false
	if input_editor != null:
		input_editor.set_manual_mode(false, last_live_input_bytes)
	_apply_clock()
	_update_game_speed_button()
	_update_input_controls()
	_update_hud()
	session_started.emit(session_options.copy())
	return true


func end_session(preserve_speed_for_retry: bool = false) -> void:
	if preserve_speed_for_retry and session_active:
		preserved_retry_speed_index = game_speed_index
	elif !preserve_speed_for_retry:
		preserved_retry_speed_index = -1
	if !session_active and session_options == null:
		local_player_id_override = -1
		replay_resume_transition_usec = 0
		_restore_default_clock()
		_update_game_speed_button()
		return
	session_active = false
	session_completed = false
	session_options = null
	timeline_enabled = false
	game_speed_index = DEFAULT_SPEED_INDEX
	pause_freeze_active = false
	pending_frame_advance = false
	pending_frame_rewind = false
	frame_stick_direction = 0
	pause_speed_stick_direction = 0
	_reset_session_storage()
	timeline = MxtReplayStream.new()
	manual_input_mode = false
	input_editor_visible = false
	telemetry_mode = TelemetryMode.OFF
	telemetry_update_accumulator = 0.0
	telemetry_text = ""
	if hud_root != null:
		hud_root.visible = false
	if input_editor_root != null:
		input_editor_root.visible = false
	_restore_default_clock()
	_update_game_speed_button()
	_update_input_controls()
	session_ended.emit()
	local_player_id_override = -1
	replay_resume_transition_usec = 0


func mark_completed() -> void:
	if session_active:
		session_completed = true


func is_infinite() -> bool:
	return session_active and session_options != null and session_options.lap_count == 0


func arm_local_player_override(player_id: int) -> void:
	local_player_id_override = player_id


func finish_replay_resume_transition(transition_start_usec: int) -> void:
	replay_resume_transition_usec = maxi(Time.get_ticks_usec() - transition_start_usec, 0)


func configure_timeline_roster(racer_ids: Array, cpu_flags: Array) -> void:
	timeline_racer_ids = racer_ids.duplicate(true)
	timeline_cpu_flags = cpu_flags.duplicate(true)


func begin_resumed_session(configuration: MxtRaceConfiguration, focused_player_id: int, source_stream: MxtReplayStream, canonical_prefix_count: int, transition_start_usec: int) -> bool:
	var resumed_configuration := configuration.copy()
	resumed_configuration.practice_local_player_id = focused_player_id
	resumed_configuration.resumed_from_replay = true
	if !begin_session(resumed_configuration):
		return false
	if timeline_enabled and (source_stream == null or !timeline.copy_prefix_from(source_stream, canonical_prefix_count)):
		end_session()
		return false
	game_speed_index = 0
	manual_input_mode = false
	finish_replay_resume_transition(transition_start_usec)
	_apply_clock()
	_update_game_speed_button()
	_update_input_controls()
	_update_hud()
	return true


func append_canonical_game_sim_frame(game_sim: GameSim, tick: int) -> bool:
	return session_active and timeline_enabled and timeline.append_game_sim_frame(game_sim, tick)


func canonical_frame_count() -> int:
	return timeline.frame_count() if session_active and timeline_enabled else 0


func canonical_input_byte_count() -> int:
	return timeline.input_byte_count() if session_active and timeline_enabled else 0


func canonical_stream() -> MxtReplayStream:
	return timeline if session_active and timeline_enabled else null


func resolve_local_input(live_input_bytes: PackedByteArray) -> PackedByteArray:
	if !session_active:
		return live_input_bytes
	last_live_input_bytes = live_input_bytes.duplicate()
	if input_editor != null:
		input_editor.set_live_input(last_live_input_bytes)
	if manual_input_mode and game_speed_index == 0 and input_editor != null:
		var manual_bytes := input_editor.manual_bytes()
		if !input_editor.last_round_trip_valid:
			push_error("Practice manual input failed exact encoded-byte round trip.")
			return PackedByteArray([0])
		return manual_bytes
	return live_input_bytes


func set_manual_input_mode(enabled: bool) -> void:
	if !session_active:
		return
	manual_input_mode = enabled
	if input_editor != null:
		input_editor.set_manual_mode(manual_input_mode, last_live_input_bytes)
	_update_input_controls()


func toggle_manual_input_mode() -> void:
	set_manual_input_mode(!manual_input_mode)


func toggle_input_editor() -> void:
	if !session_active:
		return
	input_editor_visible = !input_editor_visible
	if input_editor_root != null:
		input_editor_root.visible = input_editor_visible
	_update_input_controls()


func capture_current_live_input() -> void:
	if input_editor != null:
		input_editor.seed_from_bytes(last_live_input_bytes)


func cycle_telemetry_mode() -> void:
	set_telemetry_mode((telemetry_mode + 1) % TelemetryMode.size())


func set_telemetry_mode(mode: int) -> void:
	if !session_active:
		return
	telemetry_mode = clampi(mode, TelemetryMode.OFF, TelemetryMode.EXPANDED)
	telemetry_update_accumulator = TELEMETRY_UPDATE_SECONDS
	if telemetry_mode == TelemetryMode.OFF:
		telemetry_text = ""
	_update_input_controls()
	_update_hud()


func update(unscaled_delta: float) -> void:
	if !session_active or telemetry_mode == TelemetryMode.OFF or game_manager == null \
			or !game_manager.game_sim.sim_started:
		return
	telemetry_update_accumulator += unscaled_delta
	if telemetry_update_accumulator < TELEMETRY_UPDATE_SECONDS:
		return
	telemetry_update_accumulator = fmod(telemetry_update_accumulator, TELEMETRY_UPDATE_SECONDS)
	var sample: PackedFloat32Array = game_manager.game_sim.get_player_telemetry_sample(game_manager._local_player_id())
	telemetry_sample_count += 1
	if sample.size() < TelemetryField.SAMPLE_SIZE:
		telemetry_text = "Telemetry unavailable"
	else:
		telemetry_text = _format_telemetry(sample)
		telemetry_format_count += 1
	_update_hud()


func game_speed() -> float:
	return float(game_speed_index) * SPEED_STEP


func set_pause_freeze(enabled: bool) -> void:
	if !session_active:
		return
	pause_freeze_active = enabled
	frame_stick_direction = 0
	pending_frame_advance = false
	pending_frame_rewind = false
	_apply_clock()
	_update_input_controls()


func blocks_automatic_ticks() -> bool:
	return session_active and (pause_freeze_active or game_speed_index == 0)


func consume_frame_advance() -> bool:
	if !pending_frame_advance or !session_active or pause_freeze_active or game_speed_index != 0:
		return false
	pending_frame_advance = false
	return true


func consume_frame_rewind() -> bool:
	if !pending_frame_rewind or !session_active or pause_freeze_active or game_speed_index != 0:
		return false
	pending_frame_rewind = false
	return rewind_one_frame()


func request_frame_advance() -> bool:
	if !session_active or pause_freeze_active or game_speed_index != 0:
		return false
	pending_frame_advance = true
	return true


func request_frame_rewind() -> bool:
	if !session_active or pause_freeze_active or game_speed_index != 0:
		return false
	pending_frame_rewind = true
	return true


func capture_completed_tick(saved_tick: int) -> void:
	if !session_active or saved_tick < 0:
		return
	var ghost_state: Dictionary = game_manager.time_attack_ghost_controller.capture_practice_rolling_state(saved_tick)
	if !bool(ghost_state.get("success", false)):
		return
	var record := _capture_companion_record(saved_tick + 1)
	record["saved_tick"] = saved_tick
	record["ghost_state"] = ghost_state
	companion_ring[saved_tick % REWIND_CAPACITY] = record
	latest_companion_tick = saved_tick
	_update_hud()


func rewind_one_frame() -> bool:
	if !session_active or game_speed_index != 0 or pause_freeze_active:
		return false
	var target_tick := game_manager._singleplayer_tick - 2
	if target_tick < 0:
		_notify("Rewind limit reached")
		return false
	var record_value = companion_ring[target_tick % REWIND_CAPACITY]
	if typeof(record_value) != TYPE_DICTIONARY:
		_notify("Rewind limit reached")
		return false
	var record: Dictionary = record_value
	if int(record.get("session_serial", -1)) != session_serial \
			or int(record.get("saved_tick", -1)) != target_tick \
			or !game_manager.game_sim.has_saved_state(target_tick) \
			or !game_manager.time_attack_ghost_controller.can_restore_practice_rolling_state(record.get("ghost_state", {})):
		_notify("Rewind limit reached")
		return false
	if !game_manager.game_sim.load_state(target_tick):
		_notify("Rewind failed")
		return false
	if !game_manager.time_attack_ghost_controller.restore_practice_rolling_state(record.get("ghost_state", {})):
		_notify("Rewind failed")
		return false
	_restore_companion_record(record)
	game_manager.game_sim.discard_race_events()
	game_manager.game_sim.snap_render_after_state_load()
	game_manager.reconcile_practice_state_restore()
	_notify("Rewind  ·  Tick %d" % game_manager._singleplayer_tick)
	_update_hud()
	return true


func save_selected_slot() -> bool:
	if !_actively_driving():
		return false
	var capture_start := Time.get_ticks_usec()
	var next_tick := game_manager._singleplayer_tick
	var main_state := game_manager.game_sim.get_full_state_data(next_tick)
	var ghost_state: Dictionary = game_manager.time_attack_ghost_controller.capture_practice_full_state()
	if main_state.is_empty() or !bool(ghost_state.get("success", false)):
		_notify("Slot %d — Save failed" % (selected_slot + 1))
		return false
	var companion := _capture_companion_record(next_tick)
	var local_id := game_manager._local_player_id()
	var lap := game_manager.game_sim.get_player_lap(local_id)
	slot_creation_sequence += 1
	var total_bytes := main_state.size() + int(ghost_state.get("total_bytes", 0))
	var timeline_head := timeline.retain_head() if timeline_enabled else -1
	_release_slot_timeline_head(selected_slot)
	slots[selected_slot] = {
		"occupied": true,
		"session_serial": session_serial,
		"roster": game_manager.network_manager.get_simulation_roster().duplicate(),
		"next_tick": next_tick,
		"lap": lap,
		"elapsed_ticks": maxi(0, next_tick - game_manager.race_presentation_controller.race_results_start_tick()),
		"creation_sequence": slot_creation_sequence,
		"main_state": main_state,
		"ghost_state": ghost_state,
		"companion": companion,
		"timeline_head": timeline_head,
		"total_bytes": total_bytes,
	}
	last_slot_capture_usec = Time.get_ticks_usec() - capture_start
	_recalculate_slot_bytes()
	_notify("Slot %d — Saved · Lap %d · %s" % [
		selected_slot + 1,
		lap,
		_format_ticks(int((slots[selected_slot] as Dictionary).get("elapsed_ticks", 0))),
	])
	_update_hud()
	return true


func load_selected_slot() -> bool:
	if !_actively_driving():
		return false
	var slot_value = slots[selected_slot]
	if typeof(slot_value) != TYPE_DICTIONARY or !bool((slot_value as Dictionary).get("occupied", false)):
		_notify("Slot %d — Empty" % (selected_slot + 1))
		return false
	var slot: Dictionary = slot_value
	if int(slot.get("session_serial", -1)) != session_serial \
			or slot.get("roster", []) != game_manager.network_manager.get_simulation_roster() \
			or (timeline_enabled and !timeline.has_head(int(slot.get("timeline_head", -1)))) \
			or !game_manager.time_attack_ghost_controller.can_restore_practice_full_state(slot.get("ghost_state", {})):
		_notify("Slot %d — No longer matches this session" % (selected_slot + 1))
		return false
	var restore_start := Time.get_ticks_usec()
	var next_tick := int(slot.get("next_tick", -1))
	if next_tick < 0 or !game_manager.game_sim.load_full_state_data(next_tick, slot.get("main_state", PackedByteArray())):
		_notify("Slot %d — Load failed" % (selected_slot + 1))
		return false
	if !game_manager.time_attack_ghost_controller.restore_practice_full_state(slot.get("ghost_state", {})):
		_notify("Slot %d — Load failed" % (selected_slot + 1))
		return false
	if timeline_enabled and !timeline.restore_head(int(slot.get("timeline_head", -1))):
		_notify("Slot %d — Timeline restore failed" % (selected_slot + 1))
		return false
	_restore_companion_record(slot.get("companion", {}))
	_clear_companion_ring()
	game_manager.game_sim.discard_race_events()
	game_manager.game_sim.snap_render_after_state_load()
	game_manager.reconcile_practice_state_restore()
	last_slot_restore_usec = Time.get_ticks_usec() - restore_start
	_notify("Slot %d — Loaded · Lap %d · %s" % [
		selected_slot + 1,
		int(slot.get("lap", 1)),
		_format_ticks(int(slot.get("elapsed_ticks", 0))),
	])
	_update_hud()
	return true


func diagnostic_snapshot() -> Dictionary:
	var output := {
		"session_active": session_active,
		"session_serial": session_serial,
		"game_speed": game_speed(),
		"selected_slot": selected_slot + 1,
		"occupied_slots": _occupied_slot_count(),
		"slot_bytes": aggregate_slot_bytes,
		"last_slot_capture_usec": last_slot_capture_usec,
		"last_slot_restore_usec": last_slot_restore_usec,
		"rewind_frames": _available_rewind_frames(),
		"engine_time_scale": Engine.time_scale,
		"engine_physics_ticks_per_second": Engine.physics_ticks_per_second,
	}
	output["timeline"] = timeline.get_stats()
	output["manual_input"] = manual_input_mode
	output["manual_round_trip_valid"] = input_editor.last_round_trip_valid if input_editor != null else true
	output["telemetry_mode"] = telemetry_mode
	output["telemetry_sample_count"] = telemetry_sample_count
	output["telemetry_format_count"] = telemetry_format_count
	output["local_player_id_override"] = local_player_id_override
	output["replay_resume_transition_usec"] = replay_resume_transition_usec
	return output


func increase_game_speed() -> void:
	_set_game_speed_index(game_speed_index + 1)


func decrease_game_speed() -> void:
	_set_game_speed_index(game_speed_index - 1)


func handle_pause_input(event: InputEvent, focus_owner: Control) -> bool:
	if !session_active:
		pause_speed_stick_direction = 0
		return false
	var editing_speed := game_speed_button != null and focus_owner == game_speed_button
	var editing_input_mode := input_mode_button != null and focus_owner == input_mode_button
	var editing_telemetry := telemetry_button != null and focus_owner == telemetry_button
	if !editing_speed and !editing_input_mode and !editing_telemetry:
		pause_speed_stick_direction = 0
		return false
	var left_pressed := event.is_action_pressed("ui_left") or event.is_action_pressed("DpadLeft") \
			or event.is_action_pressed("SteerLeft")
	var right_pressed := event.is_action_pressed("ui_right") or event.is_action_pressed("DpadRight") \
			or event.is_action_pressed("SteerRight")
	if (event.is_action_released("SteerLeft") and pause_speed_stick_direction < 0) \
			or (event.is_action_released("SteerRight") and pause_speed_stick_direction > 0):
		pause_speed_stick_direction = 0
	if left_pressed and pause_speed_stick_direction == 0:
		pause_speed_stick_direction = -1 if event.is_action_pressed("SteerLeft") else 0
		if editing_speed:
			decrease_game_speed()
		elif editing_input_mode:
			set_manual_input_mode(false)
		else:
			set_telemetry_mode(posmod(telemetry_mode - 1, TelemetryMode.size()))
		return true
	if right_pressed and pause_speed_stick_direction == 0:
		pause_speed_stick_direction = 1 if event.is_action_pressed("SteerRight") else 0
		if editing_speed:
			increase_game_speed()
		elif editing_input_mode:
			set_manual_input_mode(true)
		else:
			set_telemetry_mode((telemetry_mode + 1) % TelemetryMode.size())
		return true
	return false


func handle_runtime_input(event: InputEvent) -> bool:
	if !session_active or pause_freeze_active or game_manager == null or !game_manager.game_sim.sim_started:
		frame_stick_direction = 0
		return false
	var driving := _actively_driving()
	if driving and event.is_action_pressed("PracticeSlotPrevious"):
		_select_slot(selected_slot - 1)
		return true
	if driving and event.is_action_pressed("PracticeSlotNext"):
		_select_slot(selected_slot + 1)
		return true
	if driving and event.is_action_pressed("PracticeSlotSave"):
		save_selected_slot()
		return true
	if driving and event.is_action_pressed("PracticeSlotLoad"):
		load_selected_slot()
		return true
	if game_speed_index != 0:
		frame_stick_direction = 0
		return false
	if (event.is_action_released("PracticeRewind") and frame_stick_direction < 0) \
			or (event.is_action_released("PracticeStep") and frame_stick_direction > 0):
		frame_stick_direction = 0
		return true
	if frame_stick_direction == 0 and event.is_action_pressed("PracticeRewind"):
		frame_stick_direction = -1
		return request_frame_rewind()
	if frame_stick_direction == 0 and event.is_action_pressed("PracticeStep"):
		frame_stick_direction = 1
		return request_frame_advance()
	return false


func _set_game_speed_index(value: int) -> void:
	if !session_active:
		return
	var clamped := clampi(value, 0, SPEED_STEP_COUNT)
	if clamped == game_speed_index:
		return
	game_speed_index = clamped
	frame_stick_direction = 0
	pending_frame_advance = false
	pending_frame_rewind = false
	_apply_clock()
	_update_game_speed_button()
	_update_input_controls()
	_update_hud()
	game_speed_changed.emit(game_speed())


func _apply_clock() -> void:
	if !session_active or pause_freeze_active:
		_restore_default_clock(true)
		return
	var speed := game_speed()
	Engine.time_scale = speed
	Engine.physics_ticks_per_second = 60 if speed == 0.0 else maxi(1, roundi(60.0 * speed))


func _restore_default_clock(frozen: bool = false) -> void:
	Engine.time_scale = 0.0 if frozen else 1.0
	Engine.physics_ticks_per_second = 60


func _update_game_speed_button() -> void:
	if game_speed_button == null:
		return
	game_speed_button.text = "Game Speed  ·  %.2fx" % game_speed()


func _update_input_controls() -> void:
	if input_mode_button != null:
		input_mode_button.text = "Input Mode  ·  %s" % ("Manual" if manual_input_mode else "Live")
	if input_editor_button != null:
		input_editor_button.text = "%s Input Editor" % ("Hide" if input_editor_visible else "Show")
		input_editor_button.disabled = !session_active
	if input_editor_root != null:
		input_editor_root.visible = session_active and input_editor_visible
	if input_editor != null:
		input_editor.set_frozen(session_active and game_speed_index == 0 and !pause_freeze_active)
		_update_input_editor_timeline_controls()
	if telemetry_button != null:
		telemetry_button.text = "Telemetry  ·  %s" % ["Off", "Compact", "Expanded"][telemetry_mode]


func _capture_companion_record(next_tick: int) -> Dictionary:
	return {
		"session_serial": session_serial,
		"next_tick": next_tick,
		"race_results": game_manager.network_manager.race_results.capture_practice_state(),
		"race_dnf_low_speed_ticks": game_manager.race_dnf_low_speed_ticks.duplicate(true),
		"time_attack_finalized": game_manager.time_attack_finalized,
		"practice_completed": session_completed,
		"timeline_cursor": timeline.cursor() if timeline_enabled else 0,
	}


func _restore_companion_record(record: Dictionary) -> void:
	game_manager._singleplayer_tick = int(record.get("next_tick", 0))
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	game_manager.network_manager.race_results.restore_practice_state(record.get("race_results", {}))
	game_manager.race_dnf_low_speed_ticks = (record.get("race_dnf_low_speed_ticks", {}) as Dictionary).duplicate(true)
	game_manager.time_attack_finalized = bool(record.get("time_attack_finalized", false))
	session_completed = bool(record.get("practice_completed", false))
	if timeline_enabled:
		var timeline_cursor := int(record.get("timeline_cursor", timeline.cursor()))
		if !timeline.truncate_to(timeline_cursor):
			push_error("Practice canonical timeline could not restore cursor %d." % timeline_cursor)


func _select_slot(value: int) -> void:
	selected_slot = posmod(value, SLOT_COUNT)
	var slot_value = slots[selected_slot]
	if typeof(slot_value) != TYPE_DICTIONARY or !bool((slot_value as Dictionary).get("occupied", false)):
		_notify("Slot %d — Empty" % (selected_slot + 1))
	else:
		var slot: Dictionary = slot_value
		_notify("Slot %d — Lap %d · %s" % [
			selected_slot + 1,
			int(slot.get("lap", 1)),
			_format_ticks(int(slot.get("elapsed_ticks", 0))),
		])
	_update_hud()


func _actively_driving() -> bool:
	return session_active and !pause_freeze_active \
		and game_manager != null and game_manager.game_sim.sim_started \
		and game_manager.network_manager.race_results.net_race_finish_time == -1


func _available_rewind_frames() -> int:
	if !session_active or game_manager == null:
		return 0
	var count := 0
	var target_tick := game_manager._singleplayer_tick - 2
	while count < REWIND_CAPACITY and target_tick >= 0:
		var record_value = companion_ring[target_tick % REWIND_CAPACITY]
		if typeof(record_value) != TYPE_DICTIONARY:
			break
		var record: Dictionary = record_value
		if int(record.get("session_serial", -1)) != session_serial \
				or int(record.get("saved_tick", -1)) != target_tick \
				or !game_manager.game_sim.has_saved_state(target_tick):
			break
		count += 1
		target_tick -= 1
	return count


func _reset_session_storage() -> void:
	_clear_companion_ring()
	for index in range(slots.size()):
		_release_slot_timeline_head(index)
		slots[index] = null
	selected_slot = 0
	slot_creation_sequence = 0
	latest_companion_tick = -1
	last_slot_capture_usec = 0
	last_slot_restore_usec = 0
	aggregate_slot_bytes = 0
	_update_hud()


func _release_slot_timeline_head(index: int) -> void:
	if index < 0 or index >= slots.size() or typeof(slots[index]) != TYPE_DICTIONARY:
		return
	var head_id := int((slots[index] as Dictionary).get("timeline_head", -1))
	if head_id >= 0:
		timeline.release_head(head_id)


func _clear_companion_ring() -> void:
	for index in range(companion_ring.size()):
		companion_ring[index] = null
	latest_companion_tick = -1


func _recalculate_slot_bytes() -> void:
	aggregate_slot_bytes = 0
	for slot_value in slots:
		if typeof(slot_value) == TYPE_DICTIONARY:
			aggregate_slot_bytes += int((slot_value as Dictionary).get("total_bytes", 0))


func _occupied_slot_count() -> int:
	var count := 0
	for slot_value in slots:
		if typeof(slot_value) == TYPE_DICTIONARY and bool((slot_value as Dictionary).get("occupied", false)):
			count += 1
	return count


func _update_hud() -> void:
	if hud_label == null:
		return
	var occupied := false
	if selected_slot >= 0 and selected_slot < slots.size() and typeof(slots[selected_slot]) == TYPE_DICTIONARY:
		occupied = bool((slots[selected_slot] as Dictionary).get("occupied", false))
	var base_text := "PRACTICE  ·  %.2fx  ·  %s input\nSlot %d — %s  ·  Rewind %d/%d  ·  Timeline %d" % [
		game_speed(),
		"Manual" if manual_input_mode else "Live",
		selected_slot + 1,
		"Saved" if occupied else "Empty",
		_available_rewind_frames(),
		REWIND_CAPACITY,
		canonical_frame_count(),
	]
	hud_label.text = base_text if telemetry_text.is_empty() else base_text + "\n" + telemetry_text
	_update_input_editor_timeline_controls()


func _update_input_editor_timeline_controls() -> void:
	if input_editor == null:
		return
	var can_step := session_active and game_speed_index == 0 and !pause_freeze_active
	input_editor.set_timeline_controls_enabled(can_step and _available_rewind_frames() > 0, can_step)


func _format_telemetry(sample: PackedFloat32Array) -> String:
	var target_laps := int(sample[TelemetryField.TARGET_LAPS])
	var technique := _technique_name(int(sample[TelemetryField.TECHNIQUE_LAYER]), int(sample[TelemetryField.DRIFT_CORNER_MASK]))
	var boost_states: Array[String] = []
	if sample[TelemetryField.MANUAL_BOOST_ACTIVE] > 0.5:
		boost_states.append("Manual")
	if sample[TelemetryField.DASH_BOOST_ACTIVE] > 0.5:
		boost_states.append("Dash")
	if sample[TelemetryField.S_BOOST_ACTIVE] > 0.5:
		boost_states.append("S-BOOST")
	var boost_text := "Off" if boost_states.is_empty() else "+".join(boost_states)
	var compact := "Tick %d  ·  Lap %d/%s  ·  %.1f km/h  ·  Yaw %+.1f°/s\n%s  ·  Energy %.0f/%.0f  ·  Turbo %.1f  ·  Boost %s" % [
		int(sample[TelemetryField.TICK]),
		int(sample[TelemetryField.LAP]),
		"∞" if target_laps <= 0 else str(target_laps),
		sample[TelemetryField.SPEED_KMH],
		sample[TelemetryField.YAW_DEGREES_PER_SECOND],
		technique,
		sample[TelemetryField.ENERGY],
		sample[TelemetryField.MAX_ENERGY],
		sample[TelemetryField.TURBO],
		boost_text,
	]
	if telemetry_mode == TelemetryMode.COMPACT:
		return compact
	var machine_state := int(sample[TelemetryField.MACHINE_STATE_LOW]) \
		| (int(sample[TelemetryField.MACHINE_STATE_HIGH]) << 16)
	var terrain_state := int(sample[TelemetryField.TERRAIN_STATE_LOW]) \
		| (int(sample[TelemetryField.TERRAIN_STATE_HIGH]) << 16)
	var airborne := (machine_state & 0x2) != 0
	var low_gravity := airborne and sample[TelemetryField.HEIGHT_ABOVE_TRACK] <= 0.0
	var motion_state := "Low gravity" if low_gravity else ("Airborne" if airborne else "Grounded")
	return compact + "\nVelocity F/L/V  %+.1f  %+.1f  %+.1f km/h  ·  Angular P/Y/R  %+.1f  %+.1f  %+.1f°/s\nCorners %s  ·  %s  ·  Surface %s  ·  Height %.2f\nCheckpoint %d + %.4f  ·  Progress %.4f  ·  Ground CP %d  ·  Collision CP %d\nInput SX/SY/L/R  %d/%d/%d/%d  ·  A%d B%d Boost%d Spin%d Side%d" % [
		sample[TelemetryField.FORWARD_KMH],
		sample[TelemetryField.LATERAL_KMH],
		sample[TelemetryField.VERTICAL_KMH],
		sample[TelemetryField.PITCH_DEGREES_PER_SECOND],
		sample[TelemetryField.YAW_DEGREES_PER_SECOND],
		sample[TelemetryField.ROLL_DEGREES_PER_SECOND],
		_corner_mask_text(int(sample[TelemetryField.DRIFT_CORNER_MASK])),
		motion_state,
		_surface_name(terrain_state),
		sample[TelemetryField.HEIGHT_ABOVE_TRACK],
		int(sample[TelemetryField.CHECKPOINT]),
		sample[TelemetryField.CHECKPOINT_FRACTION],
		sample[TelemetryField.LAP_PROGRESS],
		int(sample[TelemetryField.LAST_GROUND_CHECKPOINT]),
		int(sample[TelemetryField.COLLISION_CHECKPOINT]),
		int(sample[TelemetryField.INPUT_STEER_HORIZONTAL_RAW]),
		int(sample[TelemetryField.INPUT_STEER_VERTICAL_RAW]),
		int(sample[TelemetryField.INPUT_STRAFE_LEFT_RAW]),
		int(sample[TelemetryField.INPUT_STRAFE_RIGHT_RAW]),
		int(sample[TelemetryField.INPUT_ACCELERATE]),
		int(sample[TelemetryField.INPUT_BRAKE]),
		int(sample[TelemetryField.INPUT_BOOST]),
		int(sample[TelemetryField.INPUT_SPIN_ATTACK]),
		int(sample[TelemetryField.INPUT_SIDE_ATTACK]),
	]


func _technique_name(layer: int, drift_mask: int) -> String:
	if layer == 0:
		return "Manual Turbo Slide"
	if layer == 1:
		return "Quick Turn"
	return "Drifting" if drift_mask != 0 else "Gripped"


func _corner_mask_text(mask: int) -> String:
	var names: Array[String] = []
	for corner in range(4):
		if (mask & (1 << corner)) != 0:
			names.append(["FL", "FR", "BL", "BR"][corner])
	return "gripped" if names.is_empty() else "drift " + "/".join(names)


func _surface_name(terrain: int) -> String:
	for entry in [[0x800, "Kill"], [0x400, "Fall"], [0x200, "Hole"], [0x100, "Rail"], [0x40, "Ice"], [0x20, "Lava"], [0x10, "Jump"], [0x8, "Dirt"], [0x4, "Recharge"], [0x2, "Dash"]]:
		if (terrain & int(entry[0])) != 0:
			return String(entry[1])
	return "Road"


func _notify(text: String) -> void:
	if game_manager != null:
		game_manager.race_presentation_controller.show_notification(text, 1200)


func _format_ticks(ticks: int) -> String:
	var total_msec := maxi(0, roundi(float(ticks) * 1000.0 / 60.0))
	return "%d:%02d.%03d" % [int(total_msec / 60000), int(total_msec / 1000) % 60, total_msec % 1000]


func _on_game_speed_gui_input(event: InputEvent) -> void:
	if !session_active:
		return
	if event.is_action_pressed("ui_left"):
		decrease_game_speed()
		game_speed_button.accept_event()
		return
	if event.is_action_pressed("ui_right"):
		increase_game_speed()
		game_speed_button.accept_event()
		return
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			increase_game_speed()
			game_speed_button.accept_event()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			decrease_game_speed()
			game_speed_button.accept_event()
