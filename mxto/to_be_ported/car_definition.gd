@tool

class_name CarDefinition extends Resource

@export var name : String = "Blue Falcon"
@export var ref_name : String = "CAR_BFALCON"
@export var model : PackedScene
@export var weight_kg := 1260.0
@export var acceleration := 0.45
@export var max_speed := 0.1
@export var grip_1 := 0.47
@export var grip_2 := 0.7
@export var grip_3 := 0.2
@export var turn_tension := 0.12
@export var drift_accel := 0.4
@export var turn_movement := 145.0
@export var strafe_turn := 20.0
@export var strafe := 35.0
@export var turn_reaction := 10.0
@export var boost_strength := 7.98
@export var boost_length := 1.5
@export var turn_decel := 0.02
@export var drag := 0.01
@export var body := 0.85
@export var camera_reorienting := 1.0
@export var camera_repositioning := 1.0
@export var track_collision := 1.3
@export var obstacle_collision := 2.4
@export var unk_byte_0x48 := 1
@export var max_energy := 100.0
@export var tilt_corners : Array[Vector3] = [Vector3(0.8, 0, -1.5), Vector3(-0.8, 0, -1.5), Vector3(1.1, 0, 1.7), Vector3(-1.1, 0, 1.7)]
@export var wall_corners : Array[Vector3] = [Vector3(1.0, -0.1, -1.7), Vector3(-1.0, -0.1, -1.7), Vector3(1.3, -0.1, 1.9), Vector3(-1.3, -0.1, 1.9)]
@export var car_colliders : Array[Capsule] = []
@export var boost_sources : Array[Vector3] = []

func derive_machine_base_stat_values(g_balance: float) -> CarDefinition:
	var result : CarDefinition = duplicate()
	
	var balance_offset := g_balance - 0.5
	
	if balance_offset <= 0.0:
		var drift_accel := result.drift_accel
		if drift_accel >= 1.0:
			if drift_accel >= 1.5:
				result.drift_accel = drift_accel - (1.2 - (drift_accel - 1.5)) * (drift_accel * balance_offset)
			else:
				result.drift_accel = drift_accel - 1.2 * (drift_accel * balance_offset)
		else:
			result.drift_accel = drift_accel - 2.0 * ((2.0 - drift_accel) * balance_offset)
		if result.drift_accel > 2.3:
			result.drift_accel = 2.3
	else:
		if result.drift_accel > 1.0:
			result.drift_accel = result.drift_accel - 1.8 * (result.drift_accel * balance_offset)
	
	var should_modify_boost := true
	if balance_offset < 0.0 and result.acceleration >= 0.5 and result.max_speed <= 0.2:
		should_modify_boost = false
	
	var max_speed_delta := 0.0
	if balance_offset <= 0.0:
		var normalized_speed := (result.max_speed - 0.12) / 0.08
		if normalized_speed > 1.0:
			normalized_speed = 1.0
		max_speed_delta = 0.45 * (0.4 + 0.2 * normalized_speed)
	else:
		var speed_factor := 1.0
		if result.acceleration >= 0.4:
			if result.acceleration >= 0.5 and result.max_speed >= 0.15:
				speed_factor = -0.25
		else:
			speed_factor = 3.2
		max_speed_delta = 0.16 * speed_factor
	max_speed_delta = balance_offset * absf(1.0 - result.max_speed) * max_speed_delta
	
	if result.acceleration <= 0.6 or balance_offset >= 0.0:
		result.acceleration += 0.6 * -balance_offset * absf(result.acceleration - 0.0)
	else:
		result.acceleration += 2.0 * balance_offset * absf(0.7 - result.acceleration)
	
	var min_turn_decel := 0.01
	if result.acceleration < 0.4:
		var decel_factor = 1.0
		if result.acceleration < 0.31:
			max_speed_delta *= 1.5
			decel_factor = 1.5
		if result.turn_decel > 0.03:
			decel_factor *= 1.5
		if balance_offset < 0.0:
			decel_factor *= 2.0
		result.turn_decel -= absf(0.7 * decel_factor * (result.turn_decel * balance_offset))
		if result.turn_decel < min_turn_decel:
			result.turn_decel = min_turn_decel
	
	if result.weight_kg < 700.0 and result.acceleration > 0.7:
		result.acceleration = 0.7
	
	result.max_speed += max_speed_delta
	
	if balance_offset <= 0.0:
		result.turn_movement *= (1.0 - 0.2 * balance_offset)
	else:
		result.turn_movement *= (1.0 - 0.6 * balance_offset)
	
	var grip_scaling := 1.0 + 0.25 * balance_offset
	result.grip_1 *= grip_scaling
	result.grip_3 *= grip_scaling
	
	if should_modify_boost:
		result.boost_strength *= 1.0 + 0.1 * balance_offset
	
	return result


# only defined so the car setting menu doesn't crash

func calculate_accel_with_speed(in_speed : float) -> float:
	return 0.1

func calculate_speed_after_drag(in_speed : float) -> float:
	return 0.1

func calculate_friction(in_speed : float) -> float:
	return 0.1
