@tool

class_name ThrusterFire extends Node3D

@onready var thruster_flash: Sprite3D = $Sprite3D
@onready var thrust_sprite: Sprite3D = $ThrustSprite

@export var desired_thrust_power : float = 0.0
@export var boosting := false
@export var thruster_color := Color.WHITE
@export var thruster_color_boost := Color.WHITE
@export var thrust_enabled := false:
	set(in_thrust):
		if in_thrust:
			thrust_on_time = 0.001 * Time.get_ticks_msec()
		else:
			thrust_off_time = 0.001 * Time.get_ticks_msec()
		thrust_enabled = in_thrust

var thrust_power : float = 0.0
var thrust_on_time : float = 0.0
var thrust_off_time : float = 0.0

@onready var thrust_particles: GPUParticles3D = $ThrustParticles

func _ready() -> void:
	thrust_particles.process_material = thrust_particles.process_material.duplicate(true)

var thruster_vel_min_use := 0.0
var thruster_vel_max_use := 0.0

func _process(delta: float) -> void:
	var cur_time := 0.001 * Time.get_ticks_msec()
	var use_desired : float = desired_thrust_power if thrust_enabled else 0
	thrust_power = lerpf(thrust_power, use_desired, delta * 12)
	var use_color := thruster_color_boost if boosting else thruster_color
	thrust_sprite.set_instance_shader_parameter("thrust_power", thrust_power)
	thruster_vel_min_use = lerpf(thruster_vel_min_use, remap(thrust_power, 0, 1, 12, 24), delta * 12) if boosting else lerpf(thruster_vel_min_use, remap(thrust_power, 0, 1, 3, 8), delta * 12)
	thruster_vel_max_use = lerpf(thruster_vel_max_use, remap(thrust_power, 0, 1, 16, 32), delta * 12) if boosting else lerpf(thruster_vel_max_use, remap(thrust_power, 0, 1, 4, 10), delta * 12)
	var exhaust_color := use_color * minf(1.5, thrust_power) * 0.75 if boosting else use_color * minf(1.5, thrust_power) * 0.4
	var pmat : ParticleProcessMaterial = thrust_particles.process_material
	pmat.initial_velocity_min = thruster_vel_min_use
	pmat.initial_velocity_max = thruster_vel_max_use
	pmat.color = exhaust_color
	thrust_sprite.modulate = exhaust_color
	thrust_sprite.pixel_size = 0.03 * scale.x
	if thrust_enabled and cur_time < thrust_on_time + 0.25:
		var ratio := remap(cur_time, thrust_on_time, thrust_on_time + 0.25, 0, 1)
		thruster_flash.visible = true
		thruster_flash.pixel_size = lerpf(0.01, 0.1, ratio) * scale.x
		thruster_flash.modulate = use_color
		thruster_flash.modulate.a = pow((1 - ratio), 4) * thruster_flash.modulate.a * 0.4
	else:
		thruster_flash.visible = false
