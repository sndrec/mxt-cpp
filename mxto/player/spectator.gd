class_name SpectatorPlayer
extends Node3D

var move_speed: float = 300.0
var fast_move_speed: float = 900.0
var look_speed: float = 0.0025
var look_action_speed: float = 6.0
var roll_speed: float = 4.0
var velocity := Vector3.ZERO
var camera_basis := Basis.IDENTITY
var camera_basis_desired := Basis.IDENTITY
var pending_look_delta := Vector2.ZERO
var input_enabled := true
var input_calib: InputCalibration

@onready var camera: Camera3D = $Camera3D

func _ready() -> void:
	if camera == null:
		camera = Camera3D.new()
		add_child(camera)
	camera.cull_mask = 7
	camera.current = true
	camera.far = 30000
	input_calib = InputCalibration.load_from_disk()
	sync_look_from_current_transform()

func set_input_enabled(enabled: bool) -> void:
	input_enabled = enabled
	pending_look_delta = Vector2.ZERO
	velocity = Vector3.ZERO
	if !input_enabled and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

func sync_look_from_current_transform() -> void:
	camera_basis = global_transform.basis.orthonormalized()
	camera_basis_desired = camera_basis
	pending_look_delta = Vector2.ZERO
	velocity = Vector3.ZERO

func _exit_tree() -> void:
	if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

func _input(event: InputEvent) -> void:
	if !input_enabled:
		return
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var motion: InputEventMouseMotion = event
		pending_look_delta += motion.relative
	elif event is InputEventMouseButton:
		var mouse_button: InputEventMouseButton = event
		if mouse_button.button_index == MOUSE_BUTTON_RIGHT:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if mouse_button.pressed else Input.MOUSE_MODE_VISIBLE
			get_viewport().set_input_as_handled()
	elif event.is_action_pressed("ui_cancel") and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		get_viewport().set_input_as_handled()

func _process(delta: float) -> void:
	if !input_enabled:
		return
	var look_delta := pending_look_delta
	pending_look_delta = Vector2.ZERO
	var pitch_amount := -look_delta.y * look_speed
	var yaw_amount := -look_delta.x * look_speed
	pitch_amount += _action_axis("CameraUp", "CameraDown") * delta * -look_action_speed
	pitch_amount += _action_axis("CamForward", "CamBack") * delta * -look_action_speed
	yaw_amount += _action_axis("CameraLeft", "CameraRight") * delta * -look_action_speed
	yaw_amount += _action_axis("CamLeft", "CamRight") * delta * -look_action_speed
	var roll_input := _calibrated_strafe_axis()
	if Input.is_physical_key_pressed(KEY_Q):
		roll_input -= 1.0
	if Input.is_physical_key_pressed(KEY_E):
		roll_input += 1.0
	var roll_amount := clampf(roll_input, -1.0, 1.0) * delta * -roll_speed
	if pitch_amount != 0.0:
		camera_basis_desired = camera_basis_desired.rotated(camera_basis_desired.x, pitch_amount)
	if yaw_amount != 0.0:
		camera_basis_desired = camera_basis_desired.rotated(camera_basis_desired.y, yaw_amount)
	if roll_amount != 0.0:
		camera_basis_desired = camera_basis_desired.rotated(camera_basis_desired.z, roll_amount)
	camera_basis_desired = camera_basis_desired.orthonormalized()
	camera_basis = camera_basis.slerp(camera_basis_desired, clampf(delta * 8.0, 0.0, 1.0)).orthonormalized()
	global_transform = Transform3D(camera_basis, global_position)

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
	move_input.x += _action_axis("MoveLeft", "MoveRight")
	move_input.x += _action_axis("SteerLeft", "SteerRight")
	move_input.z += _action_axis("MoveForward", "MoveBack")
	move_input.z += _action_axis("SteerUp", "SteerDown")
	if move_input.length_squared() > 1.0:
		move_input = move_input.normalized()
	var current_speed := fast_move_speed if Input.is_physical_key_pressed(KEY_SHIFT) else move_speed
	var desired_velocity := camera_basis * move_input * current_speed
	var velocity_lerp := clampf(delta * (12.0 if move_input.length_squared() > 0.0 else 8.0), 0.0, 1.0)
	velocity = velocity.lerp(desired_velocity, velocity_lerp)
	global_position += velocity * delta

func _action_strength(action_name: String) -> float:
	return Input.get_action_strength(action_name) if InputMap.has_action(action_name) else 0.0

func _action_axis(negative_action: String, positive_action: String) -> float:
	return _action_strength(positive_action) - _action_strength(negative_action)

func _calibrated_strafe_axis() -> float:
	var raw_left := Input.get_action_raw_strength("StrafeLeft")
	var raw_right := Input.get_action_raw_strength("StrafeRight")
	if input_calib == null:
		input_calib = InputCalibration.load_from_disk()
	return input_calib.apply_strafe_right(raw_right) - input_calib.apply_strafe_left(raw_left)
