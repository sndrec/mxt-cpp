class_name CpuDriver
extends Node

const PlayerInputClass := preload("res://player/player_input.gd")

var last_input: PlayerInput = PlayerInputClass.new()

var desired_accel := 0.0
var desired_steer := 0.0
var desired_strafe_left := 0.0
var desired_strafe_right := 0.0
var wants_drift := false
var wants_boost := false
var time_since_last_boost = 0.0
var desired_lane := 0.0

func reset_state() -> void:
	last_input = PlayerInputClass.new()

func process_observation(_tick: int, _observation: PackedByteArray) -> void:
	var buffer : StreamPeerBuffer = StreamPeerBuffer.new()
	buffer.data_array = _observation
	buffer.seek(0)
	var spatial_t : Vector3
	spatial_t.x = buffer.get_float()
	spatial_t.y = buffer.get_float()
	spatial_t.z = buffer.get_float()
	var road_tx := buffer.get_float()
	var road_ty := buffer.get_float()
	var position : Vector3
	position.x = buffer.get_float()
	position.y = buffer.get_float()
	position.z = buffer.get_float()
	var velocity : Vector3
	velocity.x = buffer.get_float()
	velocity.y = buffer.get_float()
	velocity.z = buffer.get_float()
	var velocity_angular : Vector3
	velocity_angular.x = buffer.get_float()
	velocity_angular.y = buffer.get_float()
	velocity_angular.z = buffer.get_float()
	var physical_basis := Basis.IDENTITY
	physical_basis.x.x = buffer.get_float()
	physical_basis.x.y = buffer.get_float()
	physical_basis.x.z = buffer.get_float()
	physical_basis.y.x = buffer.get_float()
	physical_basis.y.y = buffer.get_float()
	physical_basis.y.z = buffer.get_float()
	physical_basis.z.x = buffer.get_float()
	physical_basis.z.y = buffer.get_float()
	physical_basis.z.z = buffer.get_float()
	var nothing : Vector3
	nothing.x = buffer.get_float()
	nothing.y = buffer.get_float()
	nothing.z = buffer.get_float()
	var surface := Basis.IDENTITY
	surface.x.x = buffer.get_float()
	surface.x.y = buffer.get_float()
	surface.x.z = buffer.get_float()
	surface.y.x = buffer.get_float()
	surface.y.y = buffer.get_float()
	surface.y.z = buffer.get_float()
	surface.z.x = buffer.get_float()
	surface.z.y = buffer.get_float()
	surface.z.z = buffer.get_float()
	surface = surface.transposed()
	var base_speed := buffer.get_float()
	var energy := buffer.get_float()
	var checkpoint_fraction := buffer.get_float()
	var current_checkpoint := buffer.get_u16()
	var terrain_state := buffer.get_u32()
	var machine_state := buffer.get_u32()
	var restore_state := buffer.get_u8()
	var tilt_state := buffer.get_u32()
	
	desired_accel = 1
	
	desired_steer = (physical_basis.x + surface.x).dot(surface.z)
	
	desired_lane += randf_range(-0.01, 0.01)
	desired_lane = clampf(desired_lane, -0.75, 0.75)
	#desired_lane = 0.5
	desired_strafe_left = absf(minf(road_tx + desired_lane, 0.0)) * 4
	desired_strafe_right = maxf(road_tx + desired_lane, 0.0) * 4
	desired_strafe_left = clampf(desired_strafe_left, 0, 1)
	desired_strafe_right = clampf(desired_strafe_right, 0, 1)
	DebugDraw3D.draw_arrow(position, position + physical_basis.x * desired_steer * 16, Color.RED, 0.25, true, 0.01666)
	#DebugDraw3D.draw_arrow(position, position + physical_basis.x * -desired_strafe_left * 16, Color.BLUE, 0.25, true, 0.01666)
	
	var drifting := (tilt_state & 0x4) != 0
	if absf(desired_steer) >= 0.4 and !drifting:
		wants_drift = true
	else:
		wants_drift = false
		
	if drifting:
		desired_steer *= 5.0
		
	desired_steer = clampf(desired_steer * 30.0, -1, 1)
	#print(drifting)
	if wants_drift:
		desired_strafe_left = 1.0
		desired_strafe_right = 1.0
		DebugDraw3D.draw_sphere(position, 1.5, Color.YELLOW, 5)
	var wants_to_want_boost = true if energy > 10.0 else false
	wants_boost = true if (wants_to_want_boost and time_since_last_boost > 10) else false
	if wants_boost:
		time_since_last_boost = 0
	else:
		time_since_last_boost += 1

func generate_input() -> PackedByteArray:
	# Default implementation: neutral input.
	last_input.accelerate = desired_accel
	last_input.steer_horizontal = desired_steer
	last_input.strafe_left = desired_strafe_left
	last_input.strafe_right = desired_strafe_right
	last_input.boost = wants_boost
	last_input.apply_quantization()
	return last_input.serialize()
