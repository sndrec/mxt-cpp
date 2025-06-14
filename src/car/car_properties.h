#pragma once

#include "mxt_core/curve.h"

class PhysicsCarProperties {
public:
	float weight_kg = 1260.0;
	float acceleration = 0.45;
	float max_speed = 0.1;
	float grip_1 = 0.47;
	float grip_2 = 0.7;
	float grip_3 = 0.2;
	float turn_tension = 0.12;
	float drift_accel = 0.4;
	float turn_movement = 145.0;
	float strafe_turn = 20.0;
	float strafe = 35.0;
	float turn_reaction = 10.0;
	float boost_strength = 7.98;
	float boost_length = 1.5;
	float turn_decel = 0.02;
	float drag = 0.01;
	float body = 0.85;
	float camera_reorienting = 1.0;
	float camera_repositioning = 1.0;
	float track_collision = 1.3;
	float obstacle_collision = 2.4;
	int32_t unk_byte_0x48 = 1;
	float max_energy = 100.0;

	godot::Vector3 tilt_corners[4] = {
		godot::Vector3(0.8, 0, -1.5),
		godot::Vector3(-0.8, 0, -1.5),
		godot::Vector3(1.1, 0, 1.7),
		godot::Vector3(-1.1, 0, 1.7)
	};

	godot::Vector3 wall_corners[4] = {
		godot::Vector3(1.0, -0.1, -1.7),
		godot::Vector3(-1.0, -0.1, -1.7),
		godot::Vector3(1.3, -0.1, 1.9),
		godot::Vector3(-1.3, -0.1, 1.9)
	};

	PhysicsCarProperties derive_machine_base_stat_values(float g_balance) const {
		PhysicsCarProperties result;

		float balance_offset = g_balance - 0.5;

		if (balance_offset <= 0.0) {
			if (result.drift_accel >= 1.0) {
				if (result.drift_accel >= 1.5) {
					result.drift_accel -= (1.2 - (result.drift_accel - 1.5)) * (result.drift_accel * balance_offset);
				} else {
					result.drift_accel -= 1.2 * (result.drift_accel * balance_offset);
				}
			} else {
				result.drift_accel -= 2.0 * ((2.0 - result.drift_accel) * balance_offset);
			}
			if (result.drift_accel > 2.3) {
				result.drift_accel = 2.3;
			}
		} else if (result.drift_accel > 1.0) {
			result.drift_accel -= 1.8 * (result.drift_accel * balance_offset);
		}

		bool should_modify_boost = true;
		if (balance_offset < 0.0 && result.acceleration >= 0.5 && result.max_speed <= 0.2) {
			should_modify_boost = false;
		}

		float max_speed_delta = 0.0;
		if (balance_offset <= 0.0) {
			float normalized_speed = (result.max_speed - 0.12) / 0.08;
			if (normalized_speed > 1.0) normalized_speed = 1.0;
			max_speed_delta = 0.45 * (0.4 + 0.2 * normalized_speed);
		} else {
			float speed_factor = 1.0;
			if (result.acceleration >= 0.4) {
				if (result.acceleration >= 0.5 && result.max_speed >= 0.15) {
					speed_factor = -0.25;
				}
			} else {
				speed_factor = 3.2;
			}
			max_speed_delta = 0.16 * speed_factor;
		}
		max_speed_delta = balance_offset * std::abs(1.0 - result.max_speed) * max_speed_delta;

		if (result.acceleration <= 0.6 || balance_offset >= 0.0) {
			result.acceleration += 0.6 * -balance_offset * std::abs(result.acceleration - 0.0);
		} else {
			result.acceleration += 2.0 * balance_offset * std::abs(0.7 - result.acceleration);
		}

		float min_turn_decel = 0.01;
		if (result.acceleration < 0.4) {
			float decel_factor = 1.0;
			if (result.acceleration < 0.31) {
				max_speed_delta *= 1.5;
				decel_factor = 1.5;
			}
			if (result.turn_decel > 0.03) {
				decel_factor *= 1.5;
			}
			if (balance_offset < 0.0) {
				decel_factor *= 2.0;
			}
			result.turn_decel -= std::abs(0.7 * decel_factor * (result.turn_decel * balance_offset));
			if (result.turn_decel < min_turn_decel) {
				result.turn_decel = min_turn_decel;
			}
		}

		if (result.weight_kg < 700.0 && result.acceleration > 0.7) {
			result.acceleration = 0.7;
		}

		result.max_speed += max_speed_delta;

		if (balance_offset <= 0.0) {
			result.turn_movement *= (1.0 - 0.2 * balance_offset);
		} else {
			result.turn_movement *= (1.0 - 0.6 * balance_offset);
		}

		float grip_scaling = 1.0 + 0.25 * balance_offset;
		result.grip_1 *= grip_scaling;
		result.grip_3 *= grip_scaling;

		if (should_modify_boost) {
			result.boost_strength *= 1.0 + 0.1 * balance_offset;
		}

		return result;
	}
};