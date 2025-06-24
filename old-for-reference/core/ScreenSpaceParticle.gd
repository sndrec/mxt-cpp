@tool
class_name ScreenSpaceParticle extends Node3D

var oldPosition : Vector3 = Vector3.ZERO
var olderPosition : Vector3 = Vector3.ZERO
var velocity : Vector3 = Vector3.ZERO
var lastUpdate : int = 0
var spawnTime : int = 0

@export var particleTexture : Texture2D
@export var particleColor : Vector3 = Vector3(1, 1, 1)
@export var particleSize : float = 1
@export var persistence : float = 0.016
@export var change_particle_color_with_length : bool = true
@onready var particle_mesh:MeshInstance3D = %ParticleMesh

var editorCamera : Camera3D

func pos_to_screen_space(inPos : Vector3) -> Vector3:
	if !Engine.is_editor_hint():
		if is_instance_valid(get_viewport()) and is_instance_valid(get_viewport().get_camera_3d()):
			return get_viewport().get_camera_3d().global_transform.inverse() * inPos
		return Vector3.ZERO
	else:
		return editorCamera.global_transform.inverse() * inPos

func _ready() -> void:
	oldPosition = pos_to_screen_space(position + Vector3(0, 0.01, 0))
	olderPosition = oldPosition
	particle_mesh.set_instance_shader_parameter("oldScreenPos", oldPosition)
	particle_mesh.set_instance_shader_parameter("currentScreenPos", pos_to_screen_space(position))
	lastUpdate = Time.get_ticks_msec()
	spawnTime = Time.get_ticks_msec()
	particle_mesh.get_active_material(0).set_shader_parameter("spriteTexture", particleTexture)
	particle_mesh.set_instance_shader_parameter("spriteSize", particleSize)
	var ratio : float = ((float(Time.get_ticks_msec()) * 0.001) - (float(lastUpdate) * 0.001)) / persistence
	var particleStart := olderPosition.lerp(oldPosition, ratio)
	var particleEnd := pos_to_screen_space(position)
	var magnitude := minf((Vector2(particleStart.x, particleStart.y) / absf(particleStart.z) - Vector2(particleEnd.x, particleEnd.y) / absf(particleEnd.z)).length() * absf(maxf(particleStart.z, particleEnd.z)), 1)
	particle_mesh.set_instance_shader_parameter("particleModulate", particleColor / (magnitude * 10))
	
	if Engine.is_editor_hint():
		editorCamera = Camera3D.new()
		editorCamera.position = Vector3(0, 0, -1)
		add_child(editorCamera)

func _physics_process(delta : float) -> void:
	if not particle_mesh:
		return
	var time : float = float(Time.get_ticks_msec() * 0.001)
	if Engine.is_editor_hint():
		position = Vector3(sin(sin(time * 1.2589443) * 10), cos(sin(time * 1.14983449) * 10), sin(sin(time * 1.495832) * 20.382954) * -1)
		editorCamera.global_position = Vector3(0, 0, 5)
	else:
		var changePosition : bool = _particle_process(delta)
		if changePosition:
			position = position + velocity * delta
	if Time.get_ticks_msec() > lastUpdate + persistence * 1000:
		lastUpdate = Time.get_ticks_msec()
		olderPosition = oldPosition
		oldPosition = pos_to_screen_space(position)
	var ratio : float = ((float(Time.get_ticks_msec()) * 0.001) - (float(lastUpdate) * 0.001)) / persistence
	var particleStart := olderPosition.lerp(oldPosition, ratio)
	var particleEnd := pos_to_screen_space(position)
	particle_mesh.set_instance_shader_parameter("spriteSize", particleSize)
	particle_mesh.set_instance_shader_parameter("oldScreenPos", particleStart)
	particle_mesh.set_instance_shader_parameter("currentScreenPos", particleEnd)
	var magnitude := minf((Vector2(particleStart.x, particleStart.y) / absf(particleStart.z) - Vector2(particleEnd.x, particleEnd.y) / absf(particleEnd.z)).length() * absf(maxf(particleStart.z, particleEnd.z)), 1)
	if change_particle_color_with_length:
		particle_mesh.set_instance_shader_parameter("particleModulate", particleColor / (magnitude * 10))
	else:
		particle_mesh.set_instance_shader_parameter("particleModulate", particleColor)

func _particle_process(_delta : float) -> bool:
	return true
