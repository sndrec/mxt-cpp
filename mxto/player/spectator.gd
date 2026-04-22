class_name SpectatorPlayer
extends Node3D

var move_speed: float = 300.0
var fast_move_speed: float = 900.0
var look_speed: float = 0.0025
var velocity := Vector3.ZERO
var pitch_rad := 0.0
var yaw_rad := 0.0
var pending_look_delta := Vector2.ZERO

@onready var camera: Camera3D = $Camera3D

func _ready() -> void:
	if camera == null:
		camera = Camera3D.new()
		add_child(camera)
	camera.current = true
	yaw_rad = rotation.y
	pitch_rad = rotation.x
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _exit_tree() -> void:
	if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var motion: InputEventMouseMotion = event
		pending_look_delta += motion.relative
	elif event is InputEventMouseButton:
		var mouse_button: InputEventMouseButton = event
		if mouse_button.pressed and mouse_button.button_index == MOUSE_BUTTON_LEFT:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	elif event.is_action_pressed("ui_cancel"):
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

func _process(_delta: float) -> void:
	var current_speed : float = fast_move_speed if Input.is_physical_key_pressed(KEY_SHIFT) else move_speed
	var move_input : Vector3 = Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		move_input.z -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		move_input.z += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		move_input.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		move_input.x += 1.0
	if Input.is_physical_key_pressed(KEY_E) or Input.is_physical_key_pressed(KEY_SPACE):
		move_input.y += 1.0
	if Input.is_physical_key_pressed(KEY_Q) or Input.is_physical_key_pressed(KEY_CTRL):
		move_input.y -= 1.0
	if move_input.length_squared() > 0.0:
		move_input = move_input.normalized()
	velocity = global_transform.basis * move_input * current_speed
	global_position += velocity * _delta
	if pending_look_delta.is_zero_approx():
		return
	yaw_rad -= pending_look_delta.x * look_speed
	pitch_rad -= pending_look_delta.y * look_speed
	pitch_rad = clampf(pitch_rad, deg_to_rad(-89.0), deg_to_rad(89.0))
	rotation = Vector3(pitch_rad, yaw_rad, 0.0)
	pending_look_delta = Vector2.ZERO
