#pragma once

#include "godot_cpp/classes/stream_peer_buffer.hpp"
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

	godot::StreamPeerBuffer serialize()
	{
		godot::StreamPeerBuffer out_buffer;
		out_buffer.resize(1024);
		out_buffer.put_float(weight_kg);
		out_buffer.put_float(acceleration);
		out_buffer.put_float(max_speed);
		out_buffer.put_float(grip_1);
		out_buffer.put_float(grip_2);
		out_buffer.put_float(grip_3);
		out_buffer.put_float(turn_tension);
		out_buffer.put_float(drift_accel);
		out_buffer.put_float(turn_movement);
		out_buffer.put_float(strafe_turn);
		out_buffer.put_float(strafe);
		out_buffer.put_float(turn_reaction);
		out_buffer.put_float(boost_strength);
		out_buffer.put_float(boost_length);
		out_buffer.put_float(turn_decel);
		out_buffer.put_float(drag);
		out_buffer.put_float(body);
		out_buffer.put_float(camera_reorienting);
		out_buffer.put_float(camera_repositioning);
		out_buffer.put_float(track_collision);
		out_buffer.put_float(obstacle_collision);
		out_buffer.put_float(max_energy);
		out_buffer.put_u32(unk_byte_0x48);
		return out_buffer;
	}

	static PhysicsCarProperties deserialize(godot::StreamPeerBuffer in_buffer)
	{
		PhysicsCarProperties new_properties;
		new_properties.weight_kg = in_buffer.get_float();
		new_properties.acceleration = in_buffer.get_float();
		new_properties.max_speed = in_buffer.get_float();
		new_properties.grip_1 = in_buffer.get_float();
		new_properties.grip_2 = in_buffer.get_float();
		new_properties.grip_3 = in_buffer.get_float();
		new_properties.turn_tension = in_buffer.get_float();
		new_properties.drift_accel = in_buffer.get_float();
		new_properties.turn_movement = in_buffer.get_float();
		new_properties.strafe_turn = in_buffer.get_float();
		new_properties.strafe = in_buffer.get_float();
		new_properties.turn_reaction = in_buffer.get_float();
		new_properties.boost_strength = in_buffer.get_float();
		new_properties.boost_length = in_buffer.get_float();
		new_properties.turn_decel = in_buffer.get_float();
		new_properties.drag = in_buffer.get_float();
		new_properties.body = in_buffer.get_float();
		new_properties.camera_reorienting = in_buffer.get_float();
		new_properties.camera_repositioning = in_buffer.get_float();
		new_properties.track_collision = in_buffer.get_float();
		new_properties.obstacle_collision = in_buffer.get_float();
		new_properties.max_energy = in_buffer.get_float();
		new_properties.unk_byte_0x48 = in_buffer.get_u32();
		return new_properties;
	}

	PhysicsCarProperties derive_machine_base_stat_values(float g_balance) const {
		PhysicsCarProperties result = *this;

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