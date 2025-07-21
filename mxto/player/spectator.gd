class_name SpectatorPlayer
extends Node3D

var move_speed: float = 300.0
var look_speed: float = 3.0
var velocity := Vector3.ZERO
var look_velocity := Vector3.ZERO

@onready var camera: Camera3D = $Camera3D

func _ready() -> void:
	if camera == null:
		camera = Camera3D.new()
		add_child(camera)
	camera.current = true

func _physics_process(delta: float) -> void:
	var forward := Input.get_action_strength("SteerUp") - Input.get_action_strength("SteerDown")
	var right := Input.get_action_strength("SteerLeft") - Input.get_action_strength("SteerRight")
	forward = forward * absf(forward)
	right = right * absf(right)
	var move := global_transform.basis.x * -right + global_transform.basis.z * -forward
	move *= move_speed * 0.1
	velocity += move * delta
	velocity *= pow(0.01, delta)
	global_transform.origin += velocity
	var yaw := Input.get_axis("CameraLeft", "CameraRight")
	var pitch := Input.get_axis("CameraUp", "CameraDown")
	var roll := Input.get_axis("StrafeLeft", "StrafeRight")
	yaw = yaw * absf(yaw)
	pitch = pitch * absf(pitch)
	roll = roll * absf(roll)
	look_velocity += global_transform.basis.x * -pitch * look_speed * delta * 8.0
	look_velocity += global_transform.basis.y * -yaw * look_speed * delta * 8.0
	look_velocity += global_transform.basis.z * -roll * look_speed * delta * 8.0
	look_velocity *= pow(0.00001, delta)
	if !(look_velocity.is_zero_approx()):
		global_transform.basis = global_transform.basis.rotated(look_velocity.normalized(), look_velocity.length() * delta)
