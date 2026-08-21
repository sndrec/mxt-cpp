class_name PracticeInputEditor
extends PanelContainer

signal manual_mode_requested(enabled: bool)
signal capture_live_requested
signal rewind_requested
signal step_requested

const PlayerInputClass = preload("res://player/player_input.gd")
const RAW_MAX := 254
const AXIS_NEUTRAL := 127

@onready var mode_button: Button = $Margin/Content/Mode
@onready var capture_button: Button = $Margin/Content/Actions/Capture
@onready var neutral_button: Button = $Margin/Content/Actions/Neutral
@onready var rewind_button: Button = $Margin/Content/TimelineActions/Rewind
@onready var step_button: Button = $Margin/Content/TimelineActions/Step
@onready var accelerate: CheckBox = $Margin/Content/Buttons/Accelerate
@onready var brake: CheckBox = $Margin/Content/Buttons/Brake
@onready var boost: CheckBox = $Margin/Content/Buttons/Boost
@onready var spin_attack: CheckBox = $Margin/Content/Buttons/SpinAttack
@onready var side_attack: CheckBox = $Margin/Content/Buttons/SideAttack

var analog_sliders: Dictionary = {}
var analog_values: Dictionary = {}
var analog_normalized_labels: Dictionary = {}
var raw_values := {
	"steer_horizontal": AXIS_NEUTRAL,
	"steer_vertical": AXIS_NEUTRAL,
	"strafe_left": 0,
	"strafe_right": 0,
}
var manual_mode := false
var editable := false
var authored_template := false
var synchronizing := false
var last_live_bytes := PackedByteArray([0])
var last_round_trip_valid := true


func _ready() -> void:
	_register_analog_row("steer_horizontal", $Margin/Content/Analog/SteerHorizontal)
	_register_analog_row("steer_vertical", $Margin/Content/Analog/SteerVertical)
	_register_analog_row("strafe_left", $Margin/Content/Analog/StrafeLeft)
	_register_analog_row("strafe_right", $Margin/Content/Analog/StrafeRight)
	mode_button.pressed.connect(func(): manual_mode_requested.emit(!manual_mode))
	capture_button.pressed.connect(func(): capture_live_requested.emit())
	neutral_button.pressed.connect(reset_neutral)
	rewind_button.pressed.connect(func(): rewind_requested.emit())
	step_button.pressed.connect(func(): step_requested.emit())
	for button in [accelerate, brake, boost, spin_attack, side_attack]:
		button.toggled.connect(_on_boolean_toggled)
	_sync_controls()
	set_frozen(false)


func set_manual_mode(enabled: bool, seed_bytes: PackedByteArray = PackedByteArray()) -> void:
	manual_mode = enabled
	if manual_mode and !authored_template:
		seed_from_bytes(seed_bytes if !seed_bytes.is_empty() else last_live_bytes, false)
	mode_button.text = "Input Mode  ·  %s" % ("Manual" if manual_mode else "Live")
	_update_editability()


func set_frozen(enabled: bool) -> void:
	editable = enabled
	_update_editability()


func set_timeline_controls_enabled(can_rewind: bool, can_step: bool) -> void:
	rewind_button.disabled = !can_rewind
	step_button.disabled = !can_step


func set_live_input(input_bytes: PackedByteArray) -> void:
	if input_bytes.is_empty():
		return
	last_live_bytes = input_bytes.duplicate()
	if !manual_mode:
		seed_from_bytes(last_live_bytes, false)


func seed_from_bytes(input_bytes: PackedByteArray, authored: bool = true) -> void:
	var decoded := _decode_raw(input_bytes)
	for field in raw_values.keys():
		raw_values[field] = int(decoded.get(field, raw_values[field]))
	accelerate.set_pressed_no_signal(bool(decoded.get("accelerate", false)))
	brake.set_pressed_no_signal(bool(decoded.get("brake", false)))
	boost.set_pressed_no_signal(bool(decoded.get("boost", false)))
	spin_attack.set_pressed_no_signal(bool(decoded.get("spin_attack", false)))
	side_attack.set_pressed_no_signal(bool(decoded.get("side_attack", false)))
	authored_template = authored
	_sync_controls()


func reset_neutral() -> void:
	raw_values["steer_horizontal"] = AXIS_NEUTRAL
	raw_values["steer_vertical"] = AXIS_NEUTRAL
	raw_values["strafe_left"] = 0
	raw_values["strafe_right"] = 0
	for button in [accelerate, brake, boost, spin_attack, side_attack]:
		button.set_pressed_no_signal(false)
	authored_template = true
	_sync_controls()


func manual_bytes() -> PackedByteArray:
	var input := PlayerInputClass.new()
	input.steer_horizontal = _axis_normalized(int(raw_values["steer_horizontal"]))
	input.steer_vertical = _axis_normalized(int(raw_values["steer_vertical"]))
	input.strafe_left = _trigger_normalized(int(raw_values["strafe_left"]))
	input.strafe_right = _trigger_normalized(int(raw_values["strafe_right"]))
	input.accelerate = 1.0 if accelerate.button_pressed else 0.0
	input.brake = 1.0 if brake.button_pressed else 0.0
	input.boost = boost.button_pressed
	input.spinattack = spin_attack.button_pressed
	input.sideattack = side_attack.button_pressed
	var encoded: PackedByteArray = input.serialize()
	var decoded := _decode_raw(encoded)
	last_round_trip_valid = true
	for field in raw_values.keys():
		if int(decoded.get(field, -1)) != int(raw_values[field]):
			last_round_trip_valid = false
			break
	return encoded


func _register_analog_row(field: String, row: Control) -> void:
	var slider := row.get_node("Slider") as HSlider
	var value := row.get_node("Raw") as SpinBox
	var normalized := row.get_node("Normalized") as Label
	analog_sliders[field] = slider
	analog_values[field] = value
	analog_normalized_labels[field] = normalized
	slider.value_changed.connect(_on_analog_changed.bind(field, false))
	value.value_changed.connect(_on_analog_changed.bind(field, true))
	slider.gui_input.connect(_on_analog_gui_input.bind(field))


func _on_analog_changed(value: float, field: String, _from_numeric: bool) -> void:
	if synchronizing:
		return
	raw_values[field] = clampi(roundi(value), 0, RAW_MAX)
	authored_template = true
	_sync_controls()


func _on_analog_gui_input(event: InputEvent, field: String) -> void:
	if !editable or !manual_mode or !(event is InputEventMouseButton) or !event.pressed:
		return
	if event.button_index == MOUSE_BUTTON_WHEEL_UP:
		raw_values[field] = mini(RAW_MAX, int(raw_values[field]) + 1)
	elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		raw_values[field] = maxi(0, int(raw_values[field]) - 1)
	else:
		return
	authored_template = true
	_sync_controls()
	(analog_sliders[field] as HSlider).accept_event()


func _on_boolean_toggled(_enabled: bool) -> void:
	if !synchronizing:
		authored_template = true


func _sync_controls() -> void:
	synchronizing = true
	for field in raw_values.keys():
		var raw := int(raw_values[field])
		(analog_sliders[field] as HSlider).set_value_no_signal(raw)
		(analog_values[field] as SpinBox).set_value_no_signal(raw)
		var normalized := _trigger_normalized(raw) if field.begins_with("strafe") else _axis_normalized(raw)
		(analog_normalized_labels[field] as Label).text = "%+.6f" % normalized
	synchronizing = false


func _update_editability() -> void:
	var can_edit := editable and manual_mode
	for slider in analog_sliders.values():
		(slider as HSlider).editable = can_edit
	for value in analog_values.values():
		(value as SpinBox).editable = can_edit
	for button in [accelerate, brake, boost, spin_attack, side_attack, neutral_button]:
		button.disabled = !can_edit
	capture_button.disabled = !editable


func _decode_raw(input_bytes: PackedByteArray) -> Dictionary:
	var input := PlayerInputClass.new()
	input.deserialize(input_bytes)
	return {
		"steer_horizontal": clampi(roundi(remap(input.steer_horizontal, -1.0, 1.0, 0.0, RAW_MAX)), 0, RAW_MAX),
		"steer_vertical": clampi(roundi(remap(input.steer_vertical, -1.0, 1.0, 0.0, RAW_MAX)), 0, RAW_MAX),
		"strafe_left": clampi(roundi(input.strafe_left * RAW_MAX), 0, RAW_MAX),
		"strafe_right": clampi(roundi(input.strafe_right * RAW_MAX), 0, RAW_MAX),
		"accelerate": input.accelerate > 0.5,
		"brake": input.brake > 0.5,
		"boost": input.boost,
		"spin_attack": input.spinattack,
		"side_attack": input.sideattack,
	}


func _axis_normalized(raw: int) -> float:
	return remap(float(raw), 0.0, RAW_MAX, -1.0, 1.0)


func _trigger_normalized(raw: int) -> float:
	return float(raw) / RAW_MAX
