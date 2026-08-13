#pragma once

#include "gamesim/gamesim_internal.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace godot;

static inline float fzgx_angle_units_to_rad(float units)
	{
		return units * (TAU / 65536.0f);
	}

	static inline float fzgx_sin_u16(uint32_t units)
	{
		return sinf(fzgx_angle_units_to_rad(static_cast<float>(static_cast<uint16_t>(units))));
	}

	static inline uint32_t float_bits_exact(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	static inline float safe_visual_div(float numerator, float denominator)
	{
		return std::abs(denominator) > 0.0001f ? numerator / denominator : 0.0f;
	}

	static inline void rotate_about_x_right(SimTransform& transform, float angle_units)
	{
		transform.basis = transform.basis * SimBasis(SimQuat(SimVec3(1.0f, 0.0f, 0.0f), fzgx_angle_units_to_rad(angle_units)));
	}

	static inline void rotate_about_y_right(SimTransform& transform, float angle_units)
	{
		transform.basis = transform.basis * SimBasis(SimQuat(SimVec3(0.0f, 1.0f, 0.0f), fzgx_angle_units_to_rad(angle_units)));
	}

	static inline void rotate_about_z_right(SimTransform& transform, float angle_units)
	{
		transform.basis = transform.basis * SimBasis(SimQuat(SimVec3(0.0f, 0.0f, 1.0f), fzgx_angle_units_to_rad(angle_units)));
	}

	static SimTransform compose_machine_visual_transform_for_render(PhysicsCarSoA& c, int i, GameSim::RenderVehicleVisualState& render_state, bool advance_state, bool store_side_effects)
	{
		SimTransform current_transform = MXT_LOAD_TRANSFORM(c, basis_physical, i);
		const SimVec3 position = LOAD_INDEXED_VEC3(c, position_current, i);
		current_transform.origin = position;

		float startup_wobble = 0.0f;
		if (c.base_speed[i] <= 2.0f) {
			startup_wobble = (2.0f - c.base_speed[i]) * 0.5f;
		}
		if (c.frames_since_start_2[i] < 90u) {
			startup_wobble *= static_cast<float>(c.frames_since_start_2[i]) / 90.0f;
		}
		float use_startup_wobble = render_state.startup_wobble;
		if (advance_state) {
			render_state.startup_wobble += 0.05f * (startup_wobble - render_state.startup_wobble);
			use_startup_wobble = render_state.startup_wobble;
		}
		const float startup_roll_offset = static_cast<float>(static_cast<int16_t>(static_cast<int>(
			182.04445f * 0.5f * (use_startup_wobble * fzgx_sin_u16(c.g_anim_timer[i] * 0x109u)))));

		float vertical_offset = 0.006f * (use_startup_wobble * fzgx_sin_u16(c.g_anim_timer[i] * 0x1a3u));
		const SimVec3 visual_origin = position + current_transform.basis.xform(
			SimVec3(0.0f, vertical_offset - 0.2f * use_startup_wobble, 0.0f));

		current_transform.orthonormalize();
		current_transform.origin = SimVec3();

		{
			const int point_base = i * 4;
			const float front_z = c.tilt_offset_z[point_base + 0];
			const float back_z = c.tilt_offset_z[point_base + 2];
			float suspension_pitch = 0.0f;
			if (std::abs(front_z) > 0.0001f) {
				suspension_pitch = back_z / -front_z - 1.0f;
			}
			suspension_pitch = std::max(-0.2f, std::min(0.2f, suspension_pitch));
			SimTransform pitch_transform = current_transform;
			rotate_about_x_right(pitch_transform, static_cast<float>(static_cast<int>(182.04445f * 30.0f * suspension_pitch)));
			if (store_side_effects) {
				MXT_STORE_TRANSFORM(c, g_pitch_mtx_0x5e0, i, pitch_transform);
			}
		}

		const SimVec3 broken_down_angle = LOAD_INDEXED_VEC3(c, unk_vec3_0x4e4, i);
		rotate_about_z_right(current_transform, static_cast<float>(static_cast<int>(10430.378f * safe_visual_div(broken_down_angle.z, c.weight_derived_3[i]))));
		rotate_about_y_right(current_transform, static_cast<float>(static_cast<int>(10430.378f * safe_visual_div(broken_down_angle.y, c.weight_derived_2[i]))));
		rotate_about_x_right(current_transform, static_cast<float>(static_cast<int>(10430.378f * safe_visual_div(broken_down_angle.x, c.weight_derived_1[i]))));

		if ((c.state_2[i] & 0x20u) == 0u) {
			SimTransform local_visual;
			if ((c.machine_state[i] & MACHINESTATE::ACTIVE) != 0u) {
				float use_turn_reaction = render_state.turn_reaction_effect;
				if (advance_state) {
					render_state.turn_reaction_effect += 0.05f * (c.turn_reaction_input[i] - render_state.turn_reaction_effect);
					use_turn_reaction = render_state.turn_reaction_effect;
				}
				rotate_about_y_right(local_visual, static_cast<float>(static_cast<int>(182.04445f * use_turn_reaction)));
			}

			const SimVec3 velocity = LOAD_INDEXED_VEC3(c, velocity, i);
			const float speed_mag = velocity.length();
			const float speed_norm = safe_visual_div(speed_mag, c.stat_weight[i]) / 4.629629629f;
			const int16_t angular_roll_angle = static_cast<int16_t>(static_cast<int>(
				10430.378f * speed_norm * 4.5f * safe_visual_div(c.velocity_angular_y[i], c.weight_derived_2[i])));
			const int strafe_visual_roll = static_cast<int>(static_cast<int16_t>(static_cast<int>(
				182.04445f * (c.stat_strafe[i] / 15.0f) * -5.0f * c.input_strafe_1_6[i] * speed_norm)));
			if (advance_state) {
				render_state.strafe_visual_roll = strafe_visual_roll;
			}
			int combined_roll = static_cast<int>(angular_roll_angle) + strafe_visual_roll;

			float visual_pitch_effect = 1.0f - static_cast<float>(std::abs(combined_roll)) / 3640.0f;
			visual_pitch_effect = std::max(visual_pitch_effect, 0.0f);
			visual_pitch_effect *= 0.7f * safe_visual_div(c.visual_rotation_x[i], c.weight_derived_1[i]);
			visual_pitch_effect = std::max(-0.3f, std::min(0.3f, visual_pitch_effect));
			float visual_roll_effect = 2.5f * safe_visual_div(c.visual_rotation_z[i], c.weight_derived_3[i]);
			visual_roll_effect = std::max(-0.5f, std::min(0.5f, visual_roll_effect));

			rotate_about_x_right(local_visual, static_cast<float>(static_cast<int>(10430.378f * visual_pitch_effect)));
			combined_roll += static_cast<int>(static_cast<int16_t>(static_cast<int>(10430.378f * -visual_roll_effect)));
			combined_roll = std::max(-0x238e, std::min(0x238e, combined_roll));
			rotate_about_z_right(local_visual, static_cast<float>(static_cast<int>(static_cast<float>(static_cast<int16_t>(combined_roll)) + startup_roll_offset)));

			SimQuat target_quat = local_visual.basis.get_rotation_quaternion();
			SimQuat use_visual_quat = render_state.visual_quat;
			if (advance_state) {
				render_state.visual_quat = render_state.visual_quat.slerp(target_quat, 0.2f);
				use_visual_quat = render_state.visual_quat;
			}
			local_visual.basis = SimBasis(use_visual_quat);
			current_transform = current_transform * local_visual;

			if (c.spinattack_angle[i] != 0.0f) {
				const float spin_units = c.spinattack_angle[i] * (65536.0f / TAU);
				rotate_about_y_right(current_transform, c.spinattack_direction[i] == 0 ? spin_units : -spin_units);
			}
		} else {
			current_transform = MXT_LOAD_TRANSFORM(c, transform_visual, i);
		}

		current_transform.origin = visual_origin;

		const SimVec3 velocity = LOAD_INDEXED_VEC3(c, velocity, i);
		const SimVec3 angular_velocity = LOAD_INDEXED_VEC3(c, velocity_angular, i);
		const uint32_t velocity_hash = float_bits_exact(velocity.z) ^ float_bits_exact(velocity.x) ^ float_bits_exact(velocity.y);
		const float shake_scale = 0.00006f * c.visual_shake_mult[i];
		rotate_about_z_right(current_transform, static_cast<float>(static_cast<int>(
			10430.378f * shake_scale * (static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.y)) & 0xffffu) / 65536.0f))));
		rotate_about_x_right(current_transform, static_cast<float>(static_cast<int>(
			10430.378f * shake_scale * (static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.x)) & 0xffffu) / 65536.0f))));

		float use_height_adjust = render_state.height_adjust_from_boost;
		if (advance_state) {
			if ((c.machine_state[i] & MACHINESTATE::BOOSTING) == 0u) {
				render_state.height_adjust_from_boost -= 0.05f * render_state.height_adjust_from_boost;
			} else {
				const float pitch_adjust = std::max(0.0f, c.visual_rotation_x[i]);
				render_state.height_adjust_from_boost += 0.2f * (4.5f * safe_visual_div(pitch_adjust, c.weight_derived_1[i]) - render_state.height_adjust_from_boost);
				render_state.height_adjust_from_boost = std::min(render_state.height_adjust_from_boost, 0.3f);
			}
			use_height_adjust = render_state.height_adjust_from_boost;
		}
		current_transform.origin += current_transform.basis.get_column(1) * use_height_adjust;

		if ((c.terrain_state[i] & TERRAIN::DIRT) != 0u) {
			float dirt_scale = 0.1f + c.speed_kmh[i] / 900.0f;
			dirt_scale = std::min(dirt_scale, 1.0f);
			SimVec3 dirt_jitter(
				static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.y)) & 0xffffu) / 65536.0f - 0.5f,
				0.0f,
				static_cast<float>((velocity_hash ^ float_bits_exact(angular_velocity.z)) & 0xffffu) / 65536.0f - 0.5f);
			dirt_jitter = current_transform.basis.xform(dirt_jitter) * (0.15f * dirt_scale);
			current_transform.origin += dirt_jitter;
		}

		if (store_side_effects) {
			MXT_STORE_TRANSFORM(c, transform_visual, i, current_transform);
		}
		return current_transform;
	}

	static void update_machine_visual_transform_for_render(PhysicsCarSoA& c, int i, GameSim::RenderVehicleVisualState& render_state)
	{
		compose_machine_visual_transform_for_render(c, i, render_state, true, true);
	}

	static bool render_correction_is_small(const SimTransform& correction)
	{
		const SimTransform identity;
		const float pos_error = correction.origin.length_squared();
		const float basis_error =
			(correction.basis.c0 - identity.basis.c0).length_squared() +
			(correction.basis.c1 - identity.basis.c1).length_squared() +
			(correction.basis.c2 - identity.basis.c2).length_squared();
		return pos_error < 0.000025f && basis_error < 0.000025f;
	}

	static SimTransform corrected_render_transform(const std::vector<SimTransform>& corrections,
			const std::vector<uint8_t>& active,
			int index,
			const SimTransform& transform)
	{
		if (index >= 0 &&
				index < static_cast<int>(active.size()) &&
				active[index] &&
				index < static_cast<int>(corrections.size())) {
			SimTransform out = transform;
			out.basis = out.basis * corrections[index].basis;
			out.origin += corrections[index].origin;
			return out;
		}
		return transform;
	}

	static SimTransform apply_render_correction(const SimTransform& transform, const SimTransform& correction)
	{
		SimTransform out = transform;
		out.basis = out.basis * correction.basis;
		out.origin += correction.origin;
		return out;
	}
