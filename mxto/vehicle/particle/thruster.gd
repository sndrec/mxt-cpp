@tool
class_name VehicleThruster extends Node3D

@onready var particles: GPUParticles3D = $GPUParticles3D
@onready var sprite: Sprite3D = $Sprite3D
@onready var light: OmniLight3D = $OmniLight3D

var current_thrust := 0.0
var current_velocity := Vector3.ZERO

@export var target_thrust := 0.0
@export var target_velocity := Vector3.ZERO

var process_mat : ParticleProcessMaterial

func _ready() -> void:
	particles.process_material = particles.process_material.duplicate()
	process_mat = particles.process_material

func adjust_thruster(in_thrust : float, in_velocity : Vector3):
	target_thrust = in_thrust
	target_velocity = in_velocity


func _process(delta: float) -> void:
	target_thrust = maxf(target_thrust, 0.0)
	delta = minf(delta, 1.0)
	current_thrust = lerpf(current_thrust, target_thrust, delta * 24.0)
	current_velocity = current_velocity.lerp(target_velocity, delta * 24.0)
	process_mat.initial_velocity_max = pow(current_thrust, 0.5) * 30
	process_mat.initial_velocity_min = pow(current_thrust, 0.5) * 10
	particles.amount_ratio = current_thrust
	sprite.pixel_size = current_thrust * 0.0012
	var light_factor = remap(sin(Time.get_ticks_msec() * 0.001 * 120), -PI, PI, 0.0, 1.0)
	light.light_energy = remap(light_factor, 0, 1, 4.0, 6.0) * current_thrust
	light.omni_attenuation = remap(light_factor, 0, 1, 1.0, 3.0) * current_thrust
	#print(current_thrust)
