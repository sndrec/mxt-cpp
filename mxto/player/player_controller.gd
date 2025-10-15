class_name PlayerController
extends Node

var car_definition: Resource
var accel_setting: float = 1.0
var player_settings: Resource
var input_calib: InputCalibration

func _ready() -> void:
	input_calib = InputCalibration.load_from_disk()

func get_input() -> PlayerInput:
	var p := PlayerInput.new()
	p.strafe_left = Input.get_action_strength("StrafeLeft")
	p.strafe_right = Input.get_action_strength("StrafeRight")
	var raw_steer := Vector2(Input.get_axis("SteerLeft", "SteerRight"), Input.get_axis("SteerUp", "SteerDown"))
	if input_calib == null:
		p.steer_horizontal = raw_steer.x
		p.steer_vertical = raw_steer.y
	else:
		var cal := input_calib.apply(raw_steer)
		p.steer_horizontal = cal.x
		p.steer_vertical = cal.y
	p.accelerate = Input.get_action_strength("Accelerate")
	p.brake = Input.get_action_strength("Brake")
	p.spinattack = Input.is_action_just_pressed("SpinAttack")
	p.sideattack = Input.is_action_just_pressed("SideAttack")
	p.boost = Input.is_action_just_pressed("Boost")
	p.apply_quantization()
	return p
