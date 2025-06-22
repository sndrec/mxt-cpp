class_name PlayerInput extends Resource

var strafe_left: float = 0.0
var strafe_right: float = 0.0
var steer_horizontal: float = 0.0
var steer_vertical: float = 0.0
var accelerate: float = 0.0
var brake: float = 0.0
var spinattack: bool = false
var boost: bool = false

func to_dict() -> Dictionary:
	return {
	 "strafe_left": strafe_left,
	 "strafe_right": strafe_right,
	 "steer_horizontal": steer_horizontal,
	 "steer_vertical": steer_vertical,
	 "accelerate": accelerate,
	 "brake": brake,
	 "spinattack": spinattack,
	 "boost": boost,
	}
