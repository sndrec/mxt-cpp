class_name PracticeController
extends Node

signal session_started(options: Dictionary)
signal session_ended
signal game_speed_changed(speed: float)

const SESSION_KIND := "practice"
const SPEED_STEP := 0.05
const SPEED_STEP_COUNT := 40
const DEFAULT_SPEED_INDEX := 20
const STICK_PRESS_THRESHOLD := 0.65
const STICK_RELEASE_THRESHOLD := 0.25
const REWIND_CAPACITY := 45
const SLOT_COUNT := 16

var game_manager: GameManager
var session_active := false
var session_serial := 0
var session_options: Dictionary = {}
var session_completed := false
var game_speed_index := DEFAULT_SPEED_INDEX
var pause_freeze_active := false
var pending_frame_advance := false
var pending_frame_rewind := false
var frame_stick_direction := 0
var pause_speed_stick_direction := 0
var preserved_retry_speed_index := -1
var game_speed_button: Button
var hud_root: CanvasLayer
var hud_label: Label
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
	in_hud_root: CanvasLayer = null,
	in_hud_label: Label = null
) -> void:
	game_manager = in_game_manager
	game_speed_button = in_game_speed_button
	hud_root = in_hud_root
	hud_label = in_hud_label
	companion_ring.resize(REWIND_CAPACITY)
	slots.resize(SLOT_COUNT)
	if game_speed_button != null:
		game_speed_button.pressed.connect(increase_game_speed)
		game_speed_button.gui_input.connect(_on_game_speed_gui_input)
	_update_game_speed_button()
	_reset_session_storage()


func begin_session(options: Dictionary) -> bool:
	if String(options.get("session_kind", "")) != SESSION_KIND:
		return false
	if session_active:
		end_session()
	if preserved_retry_speed_index >= 0:
		game_speed_index = preserved_retry_speed_index
	else:
		game_speed_index = DEFAULT_SPEED_INDEX
	preserved_retry_speed_index = -1
	session_serial += 1
	session_options = options.duplicate(true)
	session_active = true
	session_completed = false
	pause_freeze_active = false
	pending_frame_advance = false
	pending_frame_rewind = false
	frame_stick_direction = 0
	pause_speed_stick_direction = 0
	_reset_session_storage()
	if hud_root != null:
		hud_root.visible = true
	_apply_clock()
	_update_game_speed_button()
	_update_hud()
	session_started.emit(session_options.duplicate(true))
	return true


func end_session(preserve_speed_for_retry: bool = false) -> void:
	if preserve_speed_for_retry and session_active:
		preserved_retry_speed_index = game_speed_index
	elif !preserve_speed_for_retry:
		preserved_retry_speed_index = -1
	if !session_active and session_options.is_empty():
		_restore_default_clock()
		_update_game_speed_button()
		return
	session_active = false
	session_completed = false
	session_options.clear()
	game_speed_index = DEFAULT_SPEED_INDEX
	pause_freeze_active = false
	pending_frame_advance = false
	pending_frame_rewind = false
	frame_stick_direction = 0
	pause_speed_stick_direction = 0
	_reset_session_storage()
	if hud_root != null:
		hud_root.visible = false
	_restore_default_clock()
	_update_game_speed_button()
	session_ended.emit()


func mark_completed() -> void:
	if session_active:
		session_completed = true


func is_infinite() -> bool:
	return session_active and int(session_options.get("lap_count", 3)) == 0


func retry_options() -> Dictionary:
	return session_options.duplicate(true) if session_active else {}


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
	return {
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


func increase_game_speed() -> void:
	_set_game_speed_index(game_speed_index + 1)


func decrease_game_speed() -> void:
	_set_game_speed_index(game_speed_index - 1)


func handle_pause_input(event: InputEvent, focus_owner: Control) -> bool:
	if !session_active or game_speed_button == null or focus_owner != game_speed_button:
		pause_speed_stick_direction = 0
		return false
	if event.is_action_pressed("ui_left") or event.is_action_pressed("DpadLeft"):
		decrease_game_speed()
		return true
	if event.is_action_pressed("ui_right") or event.is_action_pressed("DpadRight"):
		increase_game_speed()
		return true
	if event is InputEventJoypadMotion and event.axis == JOY_AXIS_LEFT_X:
		var value: float = event.axis_value
		if absf(value) <= STICK_RELEASE_THRESHOLD:
			pause_speed_stick_direction = 0
		elif pause_speed_stick_direction == 0 and absf(value) >= STICK_PRESS_THRESHOLD:
			pause_speed_stick_direction = -1 if value < 0.0 else 1
			_set_game_speed_index(game_speed_index + pause_speed_stick_direction)
		return true
	return false


func handle_runtime_input(event: InputEvent) -> bool:
	if !session_active or pause_freeze_active or game_manager == null or !game_manager.game_sim.sim_started:
		frame_stick_direction = 0
		return false
	var driving := _actively_driving()
	if driving and event.is_action_pressed("DpadLeft"):
		_select_slot(selected_slot - 1)
		return true
	if driving and event.is_action_pressed("DpadRight"):
		_select_slot(selected_slot + 1)
		return true
	if driving and event.is_action_pressed("DpadDown"):
		save_selected_slot()
		return true
	if driving and (event.is_action_pressed("DPadUp") or event.is_action_pressed("DpadUp")):
		load_selected_slot()
		return true
	if game_speed_index != 0:
		frame_stick_direction = 0
		return false
	if !(event is InputEventJoypadMotion) or event.axis != JOY_AXIS_RIGHT_X:
		return false
	var value: float = event.axis_value
	if absf(value) <= STICK_RELEASE_THRESHOLD:
		frame_stick_direction = 0
	elif frame_stick_direction == 0 and absf(value) >= STICK_PRESS_THRESHOLD:
		frame_stick_direction = -1 if value < 0.0 else 1
		if frame_stick_direction > 0:
			pending_frame_advance = true
		else:
			pending_frame_rewind = true
	return true


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


func _capture_companion_record(next_tick: int) -> Dictionary:
	return {
		"session_serial": session_serial,
		"next_tick": next_tick,
		"race_results": game_manager.network_manager.race_results.capture_practice_state(),
		"race_dnf_low_speed_ticks": game_manager.race_dnf_low_speed_ticks.duplicate(true),
		"time_attack_finalized": game_manager.time_attack_finalized,
		"practice_completed": session_completed,
	}


func _restore_companion_record(record: Dictionary) -> void:
	game_manager._singleplayer_tick = int(record.get("next_tick", 0))
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	game_manager.network_manager.race_results.restore_practice_state(record.get("race_results", {}))
	game_manager.race_dnf_low_speed_ticks = (record.get("race_dnf_low_speed_ticks", {}) as Dictionary).duplicate(true)
	game_manager.time_attack_finalized = bool(record.get("time_attack_finalized", false))
	session_completed = bool(record.get("practice_completed", false))


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
		slots[index] = null
	selected_slot = 0
	slot_creation_sequence = 0
	latest_companion_tick = -1
	last_slot_capture_usec = 0
	last_slot_restore_usec = 0
	aggregate_slot_bytes = 0
	_update_hud()


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
	hud_label.text = "PRACTICE  ·  %.2fx\nSlot %d — %s  ·  Rewind %d/%d" % [
		game_speed(),
		selected_slot + 1,
		"Saved" if occupied else "Empty",
		_available_rewind_frames(),
		REWIND_CAPACITY,
	]


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
