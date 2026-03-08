@tool
class_name VehicleThruster extends Node3D

@onready var particles: GPUParticles3D = $GPUParticles3D
@onready var sprite: Sprite3D = $Sprite3D
@onready var light: OmniLight3D = $OmniLight3D

var current_thrust := 0.0
var current_velocity := Vector3.ZERO
var full_visuals_enabled := true
var smooth_updates_enabled := true

@export var target_thrust := 0.0
@export var target_velocity := Vector3.ZERO

var process_mat : ParticleProcessMaterial

func _ready() -> void:
	particles.process_material = particles.process_material.duplicate()
	process_mat = particles.process_material

func adjust_thruster(in_thrust : float, in_velocity : Vector3):
	target_thrust = in_thrust
	target_velocity = in_velocity
	if !smooth_updates_enabled:
		current_thrust = maxf(target_thrust, 0.0)
		current_velocity = target_velocity
		sprite.pixel_size = current_thrust * 0.0012

func set_visual_mode(enable_full_visuals: bool, enable_smooth_updates: bool = true) -> void:
	full_visuals_enabled = enable_full_visuals
	smooth_updates_enabled = enable_smooth_updates
	set_process(enable_smooth_updates)
	if !full_visuals_enabled:
		particles.emitting = false
		particles.amount_ratio = 0.0
		light.visible = false
	else:
		light.visible = true
	if !smooth_updates_enabled:
		current_thrust = maxf(target_thrust, 0.0)
		current_velocity = target_velocity
		sprite.pixel_size = current_thrust * 0.0012

func _process(delta: float) -> void:
	target_thrust = maxf(target_thrust, 0.0)
	delta = minf(delta, 1.0)
	current_thrust = lerpf(current_thrust, target_thrust, delta * 24.0)
	current_velocity = current_velocity.lerp(target_velocity, delta * 24.0)
	sprite.pixel_size = current_thrust * 0.0012
	if !full_visuals_enabled:
		return

	process_mat.initial_velocity_max = pow(current_thrust, 0.5) * 30
	process_mat.initial_velocity_min = pow(current_thrust, 0.5) * 10
	particles.amount_ratio = current_thrust
	var light_factor = remap(sin(Time.get_ticks_msec() * 0.001 * 120), -PI, PI, 0.0, 1.0)
	light.light_energy = remap(light_factor, 0, 1, 4.0, 6.0) * current_thrust
	light.omni_attenuation = remap(light_factor, 0, 1, 1.0, 3.0) * current_thrust
	var thrust_extra = maxf(0.0, current_thrust - 1.0)
	process_mat.scale_max = 0.8 + thrust_extra * 2.5
	process_mat.scale_min = 0.6 + thrust_extra
	process_mat.spread = minf(10.0 + thrust_extra * 60, 50)
	process_mat.initial_velocity_min = minf(10.0 + thrust_extra * 5.0, 40)
	process_mat.initial_velocity_max = minf(20.0 + thrust_extra * 10.0, 80)
	process_mat.damping_min = maxf(500 - thrust_extra * 1000, 200)
	process_mat.damping_max = maxf(500 - thrust_extra * 1000, 200)
	process_mat.radial_accel_min = maxf(300 - thrust_extra * 500, 50)
	process_mat.radial_accel_max = maxf(300 - thrust_extra * 500, 50)
	process_mat.gravity = Vector3(0, 0, maxf(0.0, 200 - thrust_extra * 100))
	#if thrust_extra > 0.0:
		#print(thrust_extra)
