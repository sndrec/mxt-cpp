class_name PlayerController
extends Node

var car_definition: Resource
var accel_setting: float = 1.0
var player_settings: Resource

func get_input() -> PlayerInput:
        var p := PlayerInput.new()
        p.strafe_left = Input.get_action_strength("StrafeLeft")
        p.strafe_right = Input.get_action_strength("StrafeRight")
        p.steer_horizontal = Input.get_axis("SteerLeft", "SteerRight")
        p.steer_vertical = Input.get_axis("SteerUp", "SteerDown")
        p.accelerate = Input.get_action_strength("Accelerate")
        p.brake = Input.get_action_strength("Brake")
        p.spinattack = Input.is_action_just_pressed("SpinAttack")
        p.boost = Input.is_action_just_pressed("Boost")
        p.apply_quantization()
        return p
