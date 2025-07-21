class_name SpectatorPlayer
extends Node3D

@export var move_speed: float = 20.0
@export var look_speed: float = 2.0

@onready var camera: Camera3D = $Camera3D

func _ready() -> void:
    if camera == null:
        camera = Camera3D.new()
        add_child(camera)
    camera.current = true

func _physics_process(delta: float) -> void:
    var forward := Input.get_action_strength("Accelerate") - Input.get_action_strength("Brake")
    var right := Input.get_action_strength("StrafeRight") - Input.get_action_strength("StrafeLeft")
    var move := (transform.basis * Vector3(right, 0, -forward)) * move_speed * delta
    translate(move)
    var yaw := Input.get_axis("SteerLeft", "SteerRight") * look_speed * delta
    var pitch := Input.get_axis("SteerUp", "SteerDown") * look_speed * delta
    rotate_y(-yaw)
    rotate_x(-pitch)

