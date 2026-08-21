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

var game_manager: GameManager
var session_active := false
var session_serial := 0
var session_options: Dictionary = {}
var session_completed := false
var game_speed_index := DEFAULT_SPEED_INDEX
var pause_freeze_active := false
var pending_frame_advance := false
var frame_stick_direction := 0
var pause_speed_stick_direction := 0
var preserved_retry_speed_index := -1
var game_speed_button: Button


func initialize(in_game_manager: GameManager, in_game_speed_button: Button = null) -> void:
	game_manager = in_game_manager
	game_speed_button = in_game_speed_button
	if game_speed_button != null:
		game_speed_button.pressed.connect(increase_game_speed)
		game_speed_button.gui_input.connect(_on_game_speed_gui_input)
	_update_game_speed_button()


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
	frame_stick_direction = 0
	pause_speed_stick_direction = 0
	_apply_clock()
	_update_game_speed_button()
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
	frame_stick_direction = 0
	pause_speed_stick_direction = 0
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
	_apply_clock()


func blocks_automatic_ticks() -> bool:
	return session_active and (pause_freeze_active or game_speed_index == 0)


func consume_frame_advance() -> bool:
	if !pending_frame_advance or !session_active or pause_freeze_active or game_speed_index != 0:
		return false
	pending_frame_advance = false
	return true


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
	if !session_active or pause_freeze_active or game_speed_index != 0:
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
	_apply_clock()
	_update_game_speed_button()
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
