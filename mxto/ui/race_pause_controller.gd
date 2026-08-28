class_name RacePauseController extends Node

signal retry_requested
signal disconnect_requested
signal lobby_requested
signal options_requested

const NAV_PRESS_THRESHOLD := 0.60
const NAV_RELEASE_THRESHOLD := 0.35
const NAV_DAS_SECONDS := 0.33
const NAV_ARR_SECONDS := 0.09

var root: Control
var title: Label
var resume_button: Button
var game_speed_button: Button
var input_mode_button: Button
var input_editor_button: Button
var telemetry_button: Button
var retry_button: Button
var options_button: Button
var save_replay_button: Button
var lobby_button: Button
var disconnect_button: Button
var practice_controller: PracticeController
var replay_controller: ReplayController
var replay_recorder: ReplayRecorder
var options_menu: Control
var open := false
var options_open := false
var nav_direction := 0
var nav_repeat_seconds := 0.0


func initialize(
		in_root: Control,
		in_practice_controller: PracticeController,
		in_replay_controller: ReplayController,
		in_replay_recorder: ReplayRecorder,
		in_options_menu: Control) -> void:
	root = in_root
	practice_controller = in_practice_controller
	replay_controller = in_replay_controller
	replay_recorder = in_replay_recorder
	options_menu = in_options_menu
	title = root.get_node("Center/Panel/Box/RacePauseTitle")
	resume_button = root.get_node("Center/Panel/Box/ResumeButton")
	game_speed_button = root.get_node("Center/Panel/Box/GameSpeedButton")
	input_mode_button = root.get_node("Center/Panel/Box/InputModeButton")
	input_editor_button = root.get_node("Center/Panel/Box/InputEditorButton")
	telemetry_button = root.get_node("Center/Panel/Box/TelemetryButton")
	retry_button = root.get_node("Center/Panel/Box/RetryButton")
	options_button = root.get_node("Center/Panel/Box/OptionsButton")
	save_replay_button = root.get_node("Center/Panel/Box/SaveReplayButton")
	lobby_button = root.get_node("Center/Panel/Box/LobbyButton")
	disconnect_button = root.get_node("Center/Panel/Box/DisconnectButton")
	resume_button.pressed.connect(close)
	retry_button.pressed.connect(func(): retry_requested.emit())
	options_button.pressed.connect(_open_options)
	lobby_button.pressed.connect(func(): lobby_requested.emit())
	disconnect_button.pressed.connect(func(): disconnect_requested.emit())
	root.hide()


func open_for_race(configuration: MxtRaceConfiguration, singleplayer: bool, host: bool) -> void:
	if root == null:
		return
	open = true
	root.show()
	var practice := configuration.is_practice()
	title.text = "Host Race Menu" if host else "Race Menu"
	game_speed_button.visible = singleplayer and practice
	input_mode_button.visible = singleplayer and practice
	input_editor_button.visible = singleplayer and practice
	telemetry_button.visible = singleplayer and practice
	retry_button.visible = singleplayer and (configuration.is_time_attack() or practice)
	lobby_button.visible = host
	disconnect_button.text = "Exit To Main Menu" if singleplayer else "Disconnect"
	if practice:
		practice_controller.set_pause_freeze(true)
	replay_recorder.refresh_pause_button()
	_reset_navigation()
	resume_button.grab_focus()


func close() -> void:
	open = false
	_reset_navigation()
	if options_open:
		options_open = false
		if options_menu != null and options_menu.visible:
			options_menu.call("close_settings")
	if root != null:
		root.hide()
	if practice_controller != null and practice_controller.session_active:
		practice_controller.set_pause_freeze(false)


func _open_options() -> void:
	options_open = true
	root.hide()
	options_requested.emit()


func on_options_visibility_changed(game_running: bool) -> void:
	if options_menu == null or options_menu.visible or !options_open:
		return
	options_open = false
	if open and game_running:
		root.show()
		options_button.grab_focus()


func update_navigation(delta: float) -> void:
	if !open or root == null or !root.visible:
		_reset_navigation()
		return
	var value := _navigation_value()
	var next_direction := nav_direction
	if nav_direction == 0:
		if value <= -NAV_PRESS_THRESHOLD:
			next_direction = -1
		elif value >= NAV_PRESS_THRESHOLD:
			next_direction = 1
	elif nav_direction < 0 and value >= -NAV_RELEASE_THRESHOLD:
		next_direction = 1 if value >= NAV_PRESS_THRESHOLD else 0
	elif nav_direction > 0 and value <= NAV_RELEASE_THRESHOLD:
		next_direction = -1 if value <= -NAV_PRESS_THRESHOLD else 0
	if next_direction != nav_direction:
		nav_direction = next_direction
		if next_direction == 0:
			nav_repeat_seconds = 0.0
		else:
			_move_focus(next_direction)
			nav_repeat_seconds = NAV_DAS_SECONDS
		return
	if nav_direction == 0:
		return
	nav_repeat_seconds -= delta
	if nav_repeat_seconds <= 0.0:
		_move_focus(nav_direction)
		nav_repeat_seconds = NAV_ARR_SECONDS


func handle_input(event: InputEvent) -> bool:
	if !open or root == null or !root.visible:
		return false
	if practice_controller.handle_pause_input(event, get_viewport().gui_get_focus_owner()):
		return true
	if event.is_action_pressed("Accelerate"):
		var focused := get_viewport().gui_get_focus_owner()
		if focused is BaseButton and focused.visible and !focused.disabled:
			(focused as BaseButton).pressed.emit()
		return true
	return event is InputEventJoypadMotion \
		or event.is_action_pressed("DPadUp") or event.is_action_released("DPadUp") \
		or event.is_action_pressed("DpadUp") or event.is_action_released("DpadUp") \
		or event.is_action_pressed("DpadDown") or event.is_action_released("DpadDown")


func _buttons() -> Array[Button]:
	var result: Array[Button] = []
	for button in [resume_button, game_speed_button, input_mode_button, input_editor_button,
			telemetry_button, retry_button, options_button, save_replay_button, lobby_button,
			disconnect_button]:
		if button != null and button.visible and !button.disabled:
			result.append(button)
	return result


func _move_focus(direction: int) -> void:
	var buttons := _buttons()
	if buttons.is_empty():
		return
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	index = 0 if index < 0 else posmod(index + direction, buttons.size())
	buttons[index].grab_focus()


func _reset_navigation() -> void:
	nav_direction = 0
	nav_repeat_seconds = 0.0


func _navigation_value() -> float:
	var up_strength := maxf(Input.get_action_strength("SteerUp"),
		maxf(Input.get_action_strength("DPadUp"), Input.get_action_strength("DpadUp")))
	var down_strength := maxf(Input.get_action_strength("SteerDown"),
		Input.get_action_strength("DpadDown"))
	return down_strength - up_strength
