#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/input.hpp"
#include "godot_cpp/classes/viewport.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/string_name.hpp"
#include "godot_cpp/core/math.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include "track/racetrack.h"
#include "track/trigger_collider.h"
#include "track/road_modulation.h"
#include "track/road_embed.h"
#include "car/physics_car.h"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "mxt_core/math_utils.h"
#include <chrono>
#include <cfenv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <new>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#include "mxt_core/debug.hpp"

using namespace godot;

#define LOAD_INDEXED_VEC3(storage, name, index) SimVec3((storage).name##_x[(index)], (storage).name##_y[(index)], (storage).name##_z[(index)])
#define STORE_INDEXED_VEC3(storage, name, index, value) do { const SimVec3 mxt_v3_tmp = (value); (storage).name##_x[(index)] = mxt_v3_tmp.x; (storage).name##_y[(index)] = mxt_v3_tmp.y; (storage).name##_z[(index)] = mxt_v3_tmp.z; } while (0)

namespace {
	static inline godot::Vector3 gd_vec3(const SimVec3& v)
	{
		return godot::Vector3(v.x, v.y, v.z);
	}

	static inline godot::Basis gd_basis(const SimBasis& b)
	{
		godot::Basis out;
		out.set_column(0, gd_vec3(b.c0));
		out.set_column(1, gd_vec3(b.c1));
		out.set_column(2, gd_vec3(b.c2));
		return out;
	}

	static inline godot::Transform3D gd_transform(const SimTransform& t)
	{
		return godot::Transform3D(gd_basis(t.basis), gd_vec3(t.origin));
	}

	static void populate_visual_car_args(godot::Array& visual_args, const PhysicsCar& car)
	{
		visual_args[0] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index));
		visual_args[1] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, position_old, car.soa_index));
		visual_args[2] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, track_surface_normal, car.soa_index));
		visual_args[3] = car.soa->height_above_track[car.soa_index];
		visual_args[4] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity, car.soa_index));
		visual_args[5] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity_angular, car.soa_index));
		visual_args[6] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index));
		visual_args[7] = car.soa->base_speed[car.soa_index];
		visual_args[8] = car.soa->boost_turbo[car.soa_index];
		visual_args[9] = car.soa->speed_kmh[car.soa_index];
		visual_args[10] = car.soa->energy[car.soa_index];
		visual_args[11] = car.soa->lap_progress[car.soa_index];
		visual_args[12] = car.soa->boost_frames[car.soa_index];
		visual_args[13] = car.soa->boost_frames_manual[car.soa_index];
		visual_args[14] = car.soa->lap[car.soa_index];
		visual_args[15] = car.soa->machine_state[car.soa_index];
		visual_args[16] = car.soa->terrain_state[car.soa_index];
		visual_args[17] = car.soa->frames_since_start_2[car.soa_index];
		visual_args[18] = car.soa->tilt_state[car.soa_index * 4];
		visual_args[19] = car.soa->input_strafe[car.soa_index];
		visual_args[20] = car.soa->turn_reaction_input[car.soa_index];
		visual_args[21] = car.soa->g_anim_timer[car.soa_index];
		visual_args[22] = car.soa->state_2[car.soa_index];
		visual_args[23] = gd_vec3(SimVec3(car.soa->tilt_offset_x[car.soa_index * 4], car.soa->tilt_offset_y[car.soa_index * 4], car.soa->tilt_offset_z[car.soa_index * 4]));
		visual_args[24] = gd_vec3(SimVec3(car.soa->tilt_offset_x[car.soa_index * 4 + 2], car.soa->tilt_offset_y[car.soa_index * 4 + 2], car.soa->tilt_offset_z[car.soa_index * 4 + 2]));
		visual_args[25] = car.soa->stat_weight[car.soa_index];
		visual_args[26] = car.soa->stat_strafe[car.soa_index];
		visual_args[27] = car.soa->input_strafe_1_6[car.soa_index];
		visual_args[28] = car.soa->weight_derived_1[car.soa_index];
		visual_args[29] = car.soa->weight_derived_2[car.soa_index];
		visual_args[30] = car.soa->weight_derived_3[car.soa_index];
		visual_args[31] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, visual_rotation, car.soa_index));
		visual_args[32] = car.soa->spinattack_angle[car.soa_index];
		visual_args[33] = car.soa->spinattack_direction[car.soa_index];
		visual_args[34] = car.soa->visual_shake_mult[car.soa_index];
		visual_args[35] = car.soa->input_accel[car.soa_index];
		visual_args[36] = car.soa->restore_state[car.soa_index];
		visual_args[37] = car.soa->restore_move_frames[car.soa_index];
		visual_args[38] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, restore_start_transform, car.soa_index));
		visual_args[39] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, restore_target_transform, car.soa_index));
		visual_args[40] = static_cast<int>(car.get_s_boost_charge());
		visual_args[41] = static_cast<int>(car.get_s_boost_max_charge());
		visual_args[42] = car.is_s_boost_active();
		visual_args[43] = car.is_s_boost_ready();
		visual_args[44] = car.soa->tilt_state[car.soa_index * 4 + 1];
		visual_args[45] = car.soa->tilt_state[car.soa_index * 4 + 2];
		visual_args[46] = car.soa->tilt_state[car.soa_index * 4 + 3];
		visual_args[47] = car.soa->camera_reorienting[car.soa_index];
		visual_args[48] = car.soa->camera_repositioning[car.soa_index];
		visual_args[49] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, track_surface_pos, car.soa_index));
	}

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

	static SimTransform interpolate_sim_transform(const SimTransform& a, const SimTransform& b, float alpha)
	{
		alpha = std::max(0.0f, std::min(1.0f, alpha));
		SimTransform out;
		out.origin = a.origin.lerp(b.origin, alpha);
		const SimQuat qa = a.basis.get_rotation_quaternion();
		const SimQuat qb = b.basis.get_rotation_quaternion();
		out.basis = SimBasis(qa.slerp(qb, alpha));
		return out;
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

	static inline godot::AABB gd_aabb(const SimAABB& b)
	{
		return godot::AABB(gd_vec3(b.position), gd_vec3(b.size));
	}

	static void* alloc_cache_aligned(size_t size)
	{
#if defined(_MSC_VER)
		return _aligned_malloc(size, 64);
#else
		void* ptr = nullptr;
		if (posix_memalign(&ptr, 64, size) != 0) {
			return nullptr;
		}
		return ptr;
#endif
	}

	static void free_cache_aligned(void* ptr)
	{
#if defined(_MSC_VER)
		_aligned_free(ptr);
#else
		::free(ptr);
#endif
	}

	static inline SimVec3 sim_vec3(const godot::Vector3& v)
	{
		return SimVec3(v.x, v.y, v.z);
	}

	static inline SimBasis sim_basis(const godot::Basis& b)
	{
		const godot::Vector3 c0 = b.get_column(0);
		const godot::Vector3 c1 = b.get_column(1);
		const godot::Vector3 c2 = b.get_column(2);
		return SimBasis(c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z);
	}

	static inline SimTransform sim_transform(const godot::Transform3D& t)
	{
		return SimTransform(sim_basis(t.basis), sim_vec3(t.origin));
	}

	struct DipSwitchDefinition {
		const char* key;
		const char* label;
		int flag;
	};

	const DipSwitchDefinition DIP_SWITCH_DEFINITIONS[] = {
		{"DIP_DRAW_RAYCASTS", "Draw Raycasts", DIP_SWITCH::DIP_DRAW_RAYCASTS},
		{"DIP_DRAW_CHECKPOINTS", "Draw Checkpoints", DIP_SWITCH::DIP_DRAW_CHECKPOINTS},
		{"DIP_DRAW_SEGMENT_SURF", "Draw Segment Surface", DIP_SWITCH::DIP_DRAW_SEGMENT_SURF},
		{"DIP_DRAW_TILT_CORNER_DATA", "Draw Tilt Corner Data", DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA},
		{"DIP_DRAW_SEG_BOUNDS", "Draw Segment Bounds", DIP_SWITCH::DIP_DRAW_SEG_BOUNDS},
		{"DIP_DRAW_BRANCH_CENTERLINE", "Draw Branch Centerline", DIP_SWITCH::DIP_DRAW_BRANCH_CENTERLINE},
	};

	static void begin_vehicle_tick_soa(PhysicsCarSoA& c, PhysicsCar* car_views, PlayerInput* inputs, uint32_t tick_count, int count)
	{
		for (int i = 0; i < count; ++i) {
			PlayerInput& input = inputs[i];
			const float accel_raw = input.accelerate;
			c.simulation_tick[i] = tick_count;

			if (!c.s_boost_active[i]) {
				c.s_boost_frames_remaining[i] = 0;
				c.s_boost_emit_frame_accumulator[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
			} else {
				if (c.s_boost_frames_remaining[i] > 0)
					c.s_boost_frames_remaining[i] -= 1;

				c.machine_state[i] &= ~(MACHINESTATE::TOOKDAMAGE | MACHINESTATE::LOWGRIP);

				c.s_boost_emit_frame_accumulator[i] += 1;
				while (c.s_boost_emit_frame_accumulator[i] >= 30) {
					c.s_boost_emit_frame_accumulator[i] -= 30;
					if (c.s_boost_pending_spark_spawns[i] < 255)
						c.s_boost_pending_spark_spawns[i] += 1;
				}

				if (c.s_boost_frames_remaining[i] == 0) {
					c.s_boost_active[i] = false;
					c.s_boost_emit_frame_accumulator[i] = 0;
					c.s_boost_pending_spark_spawns[i] = 0;
				}
			}

			const bool needs_restore =
				c.current_track[i] &&
				!c.s_boost_active[i] &&
				(c.restore_state[i] != 0 ||
				 c.position_current_y[i] < c.current_track[i]->minimum_y ||
				 c.energy[i] <= 0.0f);
			if (needs_restore) {
				car_views[i].update_restore(accel_raw);
			}

			c.calced_max_energy[i] = c.car_properties[i]->max_energy;
			STORE_INDEXED_VEC3(c, initial_pos, i, LOAD_INDEXED_VEC3(c, position_current, i));
			c.side_attack_indicator[i] = 0.0f;

			if (tick_count < c.level_start_time[i] - 180) {
				c.machine_state[i] |= MACHINESTATE::STARTINGCOUNTDOWN;
				c.machine_state[i] &= ~MACHINESTATE::ACTIVE;
			} else if (tick_count < c.level_start_time[i]) {
				c.machine_state[i] |= MACHINESTATE::STARTINGCOUNTDOWN;
				if (c.input_accel[i] > 0.01f)
					c.machine_state[i] |= MACHINESTATE::ACTIVE;
			} else {
				c.machine_state[i] &= ~MACHINESTATE::STARTINGCOUNTDOWN;
			}

			if (c.machine_state[i] & MACHINESTATE::ZEROHP) {
				input.steer_horizontal = 0.0f;
				input.steer_vertical = 0.0f;
				input.boost = false;
				input.brake = 0.0f;
				input.strafe_left = 0.0f;
				input.strafe_right = 0.0f;
			}

			if (input.sideattack)
				c.machine_state[i] |= MACHINESTATE::SIDEATTACKING;
			if (input.spinattack)
				c.machine_state[i] |= MACHINESTATE::SPINATTACKING;
			if (input.boost && c.lap[i] > 1)
				c.machine_state[i] |= MACHINESTATE::JUST_PRESSED_BOOST;

			c.g_anim_timer[i] += 1;
			STORE_INDEXED_VEC3(c, track_surface_normal_prev, i, LOAD_INDEXED_VEC3(c, track_surface_normal, i));
		}
	}

	static void finish_vehicle_tick_soa(PhysicsCarSoA& c, int count)
	{
		for (int i = 0; i < count; ++i) {
			SimTransform basis = MXT_LOAD_TRANSFORM(c, basis_physical, i);
			const SimVec3 pos(c.position_current_x[i], c.position_current_y[i], c.position_current_z[i]);
			const SimVec3 behind = basis.basis.xform(SimVec3(0.0f, 0.5f, 0.5f)) + pos;
			c.position_behind_x[i] = behind.x;
			c.position_behind_y[i] = behind.y;
			c.position_behind_z[i] = behind.z;
		}
	}

	static inline uint32_t elapsed_us(std::chrono::high_resolution_clock::time_point start,
		std::chrono::high_resolution_clock::time_point end)
	{
		return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
	}

	static void update_damage_visual_geometry_soa(PhysicsCarSoA& c, int count);
	static void project_startup_velocity_and_speed_soa(PhysicsCarSoA& c, int count);

	static void post_vehicle_tick_soa(PhysicsCarSoA& c, PhysicsCar* car_views, uint8_t* pending_s_boost_sparks, int count,
		TrackQueryScratch &scratch,
		uint32_t& response_us, uint32_t& checkpoints_us, uint32_t& sparks_us,
		uint32_t& sample_old_us, uint32_t& corners_us, uint32_t& apply_response_us,
		uint32_t& project_speed_us, uint32_t& visual_geom_us, uint32_t& damage_tail_us)
	{
		auto phase_start = std::chrono::high_resolution_clock::now();
		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) && c.restore_state[i] != 2) {
				car_views[i].sample_old_corner_collision_surface(scratch);
			}
		}
		auto now = std::chrono::high_resolution_clock::now();
		sample_old_us = elapsed_us(phase_start, now);

		phase_start = now;
		uint32_t apply_response_accum_us = 0;
		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) && c.restore_state[i] != 2) {
				const int corner_collision_type_flag = car_views[i].update_machine_corners(scratch);
				const SimVec3 rail_push = LOAD_INDEXED_VEC3(c, collision_push_rail, i);
				const SimVec3 track_push = LOAD_INDEXED_VEC3(c, collision_push_track, i);
				const SimVec3 velocity = LOAD_INDEXED_VEC3(c, velocity, i);
				const float push_magnitude_rail = rail_push.length();
				const float push_magnitude_track = track_push.length();
				const float current_world_speed = velocity.length();
				float speed_over_weight = 0.0f;
				if (std::abs(c.stat_weight[i]) > 0.0001f) {
					speed_over_weight = current_world_speed / c.stat_weight[i];
				}
				const bool significant_collision =
					push_magnitude_rail > 0.0046296296f && speed_over_weight > 0.0046296296f;
				const bool full_response =
					c.frames_since_start_2[i] > 0x3c && significant_collision &&
					(corner_collision_type_flag & 2) &&
					(c.machine_state[i] & MACHINESTATE::LOWGRIP) == 0;
				const bool landing_response =
					(c.machine_state[i] & MACHINESTATE::JUSTLANDED) &&
					speed_over_weight >= 0.0462962962962f;
				if (push_magnitude_track > 0.0023148148f || push_magnitude_rail > 0.0023148148f ||
					full_response || landing_response) {
					auto apply_start = std::chrono::high_resolution_clock::now();
					car_views[i].apply_machine_collision_response_from_corners(corner_collision_type_flag,
						push_magnitude_rail, push_magnitude_track, current_world_speed, speed_over_weight, false);
					auto apply_end = std::chrono::high_resolution_clock::now();
					apply_response_accum_us += elapsed_us(apply_start, apply_end);
				}
				if (c.machine_state[i] & MACHINESTATE::JUSTLANDED) {
					c.air_time[i] = 0;
				}
			}
		}
		now = std::chrono::high_resolution_clock::now();
		apply_response_us = apply_response_accum_us;
		const uint32_t corner_and_apply_us = elapsed_us(phase_start, now);
		corners_us = corner_and_apply_us > apply_response_us ? corner_and_apply_us - apply_response_us : 0;

		phase_start = now;
		project_startup_velocity_and_speed_soa(c, count);
		now = std::chrono::high_resolution_clock::now();
		project_speed_us = elapsed_us(phase_start, now);

		phase_start = now;
		update_damage_visual_geometry_soa(c, count);
		now = std::chrono::high_resolution_clock::now();
		visual_geom_us = elapsed_us(phase_start, now);

		phase_start = now;
		for (int i = 0; i < count; ++i) {
			if (c.restore_state[i] == 2) {
				continue;
			}
			car_views[i].handle_machine_damage_and_visuals_tail();
			if (c.frames_since_start_2[i] == 0) {
				STORE_INDEXED_VEC3(c, velocity, i, SimVec3());
				STORE_INDEXED_VEC3(c, position_current, i, LOAD_INDEXED_VEC3(c, initial_pos, i));
			}
		}
		now = std::chrono::high_resolution_clock::now();
		damage_tail_us = elapsed_us(phase_start, now);
		response_us = sample_old_us + corners_us + apply_response_us + project_speed_us + visual_geom_us + damage_tail_us;

		phase_start = now;
		for (int i = 0; i < count; ++i) {
			if (c.restore_state[i] == 2) {
				continue;
			}
			car_views[i].handle_checkpoints(scratch);
			if ((c.machine_state[i] & MACHINESTATE::AIRBORNE) == 0 && (c.machine_state[i] & MACHINESTATE::ZEROHP) == 0) {
				c.last_ground_distance[i] = c.checkpoint_track_distance[i];
				c.last_ground_checkpoint[i] = c.current_checkpoint[i];
			}
		}
		now = std::chrono::high_resolution_clock::now();
		checkpoints_us = elapsed_us(phase_start, now);

		phase_start = now;
		for (int i = 0; i < count; ++i) {
			uint16_t pending = static_cast<uint16_t>(c.s_boost_pending_spark_spawns[i]) + c.pending_super_sparks[i];
			pending_s_boost_sparks[i] = static_cast<uint8_t>(pending > 255 ? 255 : pending);
			c.s_boost_pending_spark_spawns[i] = 0;
			c.pending_super_sparks[i] = 0;
		}
		now = std::chrono::high_resolution_clock::now();
		sparks_us = elapsed_us(phase_start, now);
	}

	static inline bool vehicle_motion_active(const PhysicsCarSoA& c, int i)
	{
		return c.restore_state[i] != 2;
	}

	static void apply_vehicle_motion_inputs_soa(PhysicsCarSoA& c, PlayerInput* inputs, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			const PlayerInput& in = inputs[i];
			c.input_steer_yaw[i] = in.steer_horizontal * std::abs(in.steer_horizontal);
			c.input_steer_pitch[i] = -in.steer_vertical;

			const float strafe_left = std::min(1.0f, in.strafe_left * 1.25f);
			const float strafe_right = std::min(1.0f, in.strafe_right * 1.25f);
			c.input_strafe[i] = -strafe_left + strafe_right;

			const float old_accel = c.input_accel[i];
			c.input_accel[i] = in.accelerate;
			const bool accel_just_pressed = c.input_accel[i] > 0.5f && old_accel <= 0.5f;
			c.input_brake[i] = in.brake;

			if (strafe_left > 0.05f && strafe_right > 0.05f) {
				c.machine_state[i] |= MACHINESTATE::MANUAL_DRIFT;
			}
			if (accel_just_pressed) {
				c.machine_state[i] |= MACHINESTATE::JUSTTAPPEDACCEL | MACHINESTATE::B14;
			}
			c.state_2[i] |= 8u;
		}
	}

	static void prepare_vehicle_floor_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count, TrackQueryScratch &scratch)
	{
		scratch.reset_trigger_events();
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			const SimVec3 ground_normal = car_views[i].prepare_machine_frame(scratch);
			const bool has_floor = car_views[i].find_floor_beneath_machine();
			if (has_floor && (c.machine_state[i] & MACHINESTATE::AIRBORNE) == 0) {
				STORE_INDEXED_VEC3(c, track_surface_normal, i, ground_normal);
			} else {
				const int base = i * 4;
				for (int lane = 0; lane < 4; ++lane) {
					const int p = base + lane;
					c.tilt_force[p] = 0.0f;
					c.tilt_force_spatial_x[p] = 0.0f;
					c.tilt_force_spatial_y[p] = 0.0f;
					c.tilt_force_spatial_z[p] = 0.0f;
					c.tilt_force_spatial_len[p] = 0.0f;
					c.tilt_state[p] |= TILTSTATE::DISCONNECTED | TILTSTATE::AIRBORNE;
				}
			}
		}
	}

	static void commit_vehicle_trigger_events(PhysicsCarSoA* car_shards, PhysicsCar* cars, int shard_count, TrackQueryScratch* lane_scratch)
	{
		for (int shard = 0; shard < shard_count; ++shard) {
			PhysicsCarSoA& c = car_shards[shard];
			TrackQueryScratch& scratch = lane_scratch[shard];
			for (int e = 0; e < scratch.trigger_event_count; ++e) {
				const TrackQueryScratch::TriggerEvent& event = scratch.trigger_events[e];
				if (event.car_index < 0 || event.car_index >= c.count) {
					continue;
				}
				RaceTrack* track = c.current_track[event.car_index];
				if (!track || event.trigger_index < 0 || event.trigger_index >= track->num_trigger_colliders) {
					continue;
				}
				TriggerCollider* trigger = track->trigger_colliders[event.trigger_index];
				if (!trigger) {
					continue;
				}
				PhysicsCar* car = cars + c.global_start + event.car_index;
				if ((event.collision_flags & 0x2) != 0) {
					trigger->start_touch(car);
				}
				trigger->touch(car);
				if ((event.collision_flags & 0x4) != 0) {
					trigger->end_touch(car);
				}
			}
			scratch.reset_trigger_events();
		}
	}

	static void project_vehicle_velocity_phase(PhysicsCarSoA& c, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			const float vx = c.velocity_x[i];
			const float vy = c.velocity_y[i];
			const float vz = c.velocity_z[i];
			const float c0x = c.basis_physical_c0x[i];
			const float c0y = c.basis_physical_c0y[i];
			const float c0z = c.basis_physical_c0z[i];
			const float c1x = c.basis_physical_c1x[i];
			const float c1y = c.basis_physical_c1y[i];
			const float c1z = c.basis_physical_c1z[i];
			const float c2x = c.basis_physical_c2x[i];
			const float c2y = c.basis_physical_c2y[i];
			const float c2z = c.basis_physical_c2z[i];

			c.velocity_local_x[i] = c0x * vx + c0y * vy + c0z * vz;
			c.velocity_local_y[i] = c1x * vx + c1y * vy + c1z * vz;
			c.velocity_local_z[i] = c2x * vx + c2y * vy + c2z * vz;

			float steer = -(c.input_steer_yaw[i] * c.stat_turn_reaction[i] + c.input_strafe[i] * c.stat_strafe[i]);
			steer = std::clamp(steer, -45.0f, 45.0f);
			const float angle = DEG_TO_RAD * steer;
			const float cs = deterministic_fp::cosf(angle);
			const float sn = deterministic_fp::sinf(angle);

			const float sx = c0x * cs - c2x * sn;
			const float sy = c0y * cs - c2y * sn;
			const float sz = c0z * cs - c2z * sn;
			const float fz_x = c0x * sn + c2x * cs;
			const float fz_y = c0y * sn + c2y * cs;
			const float fz_z = c0z * sn + c2z * cs;

			c.velocity_local_flattened_and_rotated_x[i] = sx * vx + sy * vy + sz * vz;
			c.velocity_local_flattened_and_rotated_y[i] = 0.0f;
			c.velocity_local_flattened_and_rotated_z[i] = fz_x * vx + fz_y * vy + fz_z * vz;
		}
	}

	static void steering_and_suspension_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i) || (c.machine_state[i] & MACHINESTATE::ACTIVE) == 0) {
				continue;
			}

			float strafe_turn_mod = 1.0f;
			const int base = i * 4;
			if (c.tilt_state[base + 0] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;
			if (c.tilt_state[base + 1] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;
			if (c.tilt_state[base + 2] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;
			if (c.tilt_state[base + 3] & TILTSTATE::DRIFT) strafe_turn_mod -= 0.25f;

			float steer_strength =
				(c.stat_turn_movement[i] + strafe_turn_mod * c.stat_strafe_turn[i] * c.input_strafe[i] *
					c.input_steer_yaw[i]) *
				-c.input_steer_yaw[i];
			if (c.machine_state[i] & MACHINESTATE::SIDEATTACKING) {
				steer_strength *= 0.3f;
			}
			c.velocity_angular_y[i] += 1.5f * steer_strength;
			if (std::abs(c.velocity_angular_y[i]) < 1.0f) {
				c.velocity_angular_y[i] = 0.0f;
			}
			c.input_yaw_dupe[i] = c.input_steer_yaw[i];
		}

		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_suspension_states();
			}
		}

		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i) || c.frames_since_start_2[i] == 0) {
				continue;
			}
			const float initial_angle_vel_y = c.velocity_angular_y[i];
			car_views[i].handle_machine_turn_and_strafe_points4(initial_angle_vel_y);
		}

		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}
			if (c.machine_state[i] & MACHINESTATE::AIRBORNEMORE0_2S_Q) {
				c.turning_related[i] *= 0.02f;
			}
			if (std::abs(c.input_strafe[i]) > 0.01f) {
				c.turning_related[i] *= 0.04f;
			}
		}
	}

	static void linear_orientation_drag_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_linear_velocity();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_angle_velocity();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_airborne_controls();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].orient_vehicle_from_gravity_or_road();
			}
		}
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].handle_drag_and_glide_forces();
			}
		}
	}

	static inline bool four_vehicle_motion_active(const PhysicsCarSoA& c, int i)
	{
		return vehicle_motion_active(c, i) && vehicle_motion_active(c, i + 1) &&
			vehicle_motion_active(c, i + 2) && vehicle_motion_active(c, i + 3);
	}

	static void integrate_vehicle_positions_phase(PhysicsCarSoA& c, int count)
	{
		int i = 0;
		for (; i + 3 < count; i += 4) {
			if (!four_vehicle_motion_active(c, i)) {
				for (int lane = i; lane < i + 4; ++lane) {
					if (!vehicle_motion_active(c, lane)) {
						continue;
					}
					const float inv_weight = 1.0f / std::max(c.stat_weight[lane], 0.001f);
					c.position_current_x[lane] += c.velocity_x[lane] * inv_weight + c.knockback_velocity_x[lane];
					c.position_current_y[lane] += c.velocity_y[lane] * inv_weight + c.knockback_velocity_y[lane];
					c.position_current_z[lane] += c.velocity_z[lane] * inv_weight + c.knockback_velocity_z[lane];
					c.knockback_velocity_x[lane] *= 0.93333334f;
					c.knockback_velocity_y[lane] *= 0.93333334f;
					c.knockback_velocity_z[lane] *= 0.93333334f;
				}
				continue;
			}

			const SimFloat4 inv_weight = SimFloat4(1.0f) / sim_max4(sim_load4(c.stat_weight + i), SimFloat4(0.001f));
			const SimFloat4 knockback_x = sim_load4(c.knockback_velocity_x + i);
			const SimFloat4 knockback_y = sim_load4(c.knockback_velocity_y + i);
			const SimFloat4 knockback_z = sim_load4(c.knockback_velocity_z + i);
			sim_store4(c.position_current_x + i, sim_load4(c.position_current_x + i) + sim_load4(c.velocity_x + i) * inv_weight + knockback_x);
			sim_store4(c.position_current_y + i, sim_load4(c.position_current_y + i) + sim_load4(c.velocity_y + i) * inv_weight + knockback_y);
			sim_store4(c.position_current_z + i, sim_load4(c.position_current_z + i) + sim_load4(c.velocity_z + i) * inv_weight + knockback_z);
			const SimFloat4 knockback_decay(0.93333334f);
			sim_store4(c.knockback_velocity_x + i, knockback_x * knockback_decay);
			sim_store4(c.knockback_velocity_y + i, knockback_y * knockback_decay);
			sim_store4(c.knockback_velocity_z + i, knockback_z * knockback_decay);
		}
		for (; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}
			const float inv_weight = 1.0f / std::max(c.stat_weight[i], 0.001f);
			c.position_current_x[i] += c.velocity_x[i] * inv_weight + c.knockback_velocity_x[i];
			c.position_current_y[i] += c.velocity_y[i] * inv_weight + c.knockback_velocity_y[i];
			c.position_current_z[i] += c.velocity_z[i] * inv_weight + c.knockback_velocity_z[i];
			c.knockback_velocity_x[i] *= 0.93333334f;
			c.knockback_velocity_y[i] *= 0.93333334f;
			c.knockback_velocity_z[i] *= 0.93333334f;
		}
	}

	static void rotate_and_finish_motion_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		for (int i = 0; i < count; ++i) {
			if (vehicle_motion_active(c, i)) {
				car_views[i].rotate_machine_from_angle_velocity();
			}
		}

		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			if (c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) {
				c.machine_state[i] &= ~(MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL);
			}
			if (c.machine_state[i] & MACHINESTATE::ACTIVE) {
				const uint32_t cd = c.frames_since_start_2[i];
				if (cd < 30) {
					if (cd % 6 == 0) {
						car_views[i].handle_startup_wobble();
					}
				} else if (cd < 90) {
					STORE_INDEXED_VEC3(c, velocity_angular, i, SimVec3());
				}
			}
			if (c.rail_collision_timer[i] > 0) {
				c.rail_collision_timer[i] -= 1;
			}
			c.machine_state[i] &= ~(MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
				MACHINESTATE::TOOKDAMAGE | MACHINESTATE::B14 |
				MACHINESTATE::MANUAL_DRIFT);

			SimTransform basis = MXT_LOAD_TRANSFORM(c, basis_physical, i);
			basis.orthonormalize();
			MXT_STORE_TRANSFORM(c, basis_physical, i, basis);

			if ((c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
				c.position_bottom_x[i] += c.position_current_x[i] - c.position_old_x[i];
				c.position_bottom_y[i] += c.position_current_y[i] - c.position_old_y[i];
				c.position_bottom_z[i] += c.position_current_z[i] - c.position_old_z[i];
			}
		}
	}

	static void begin_vehicle_motion_phased_soa(PhysicsCarSoA& c, PhysicsCar* car_views, PlayerInput* inputs,
		int count, TrackQueryScratch &scratch, uint32_t& prepare_floor_us)
	{
		apply_vehicle_motion_inputs_soa(c, inputs, count);

		auto phase_start = std::chrono::high_resolution_clock::now();
		prepare_vehicle_floor_phase(c, car_views, count, scratch);
		auto now = std::chrono::high_resolution_clock::now();
		prepare_floor_us = elapsed_us(phase_start, now);
	}

	static void finish_vehicle_motion_phased_soa(PhysicsCarSoA& c, PhysicsCar* car_views, int count,
		uint32_t& project_us, uint32_t& steer_susp_us, uint32_t& linear_us, uint32_t& integrate_us)
	{
		auto phase_start = std::chrono::high_resolution_clock::now();
		project_vehicle_velocity_phase(c, count);
		auto now = std::chrono::high_resolution_clock::now();
		project_us = elapsed_us(phase_start, now);

		phase_start = now;
		steering_and_suspension_phase(c, car_views, count);
		now = std::chrono::high_resolution_clock::now();
		steer_susp_us = elapsed_us(phase_start, now);

		phase_start = now;
		linear_orientation_drag_phase(c, car_views, count);
		now = std::chrono::high_resolution_clock::now();
		linear_us = elapsed_us(phase_start, now);

		phase_start = now;
		integrate_vehicle_positions_phase(c, count);
		rotate_and_finish_motion_phase(c, car_views, count);
		now = std::chrono::high_resolution_clock::now();
		integrate_us = elapsed_us(phase_start, now);
	}

	static inline bool intervals_overlap(float min_a, float max_a, float min_b, float max_b)
	{
		return min_a <= max_b && min_b <= max_a;
	}

	static inline SimVec3 transform_point_components(
		float c0x, float c0y, float c0z,
		float c1x, float c1y, float c1z,
		float c2x, float c2y, float c2z,
		float ox, float oy, float oz,
		const SimVec3& p)
	{
		return SimVec3(
			c0x * p.x + c1x * p.y + c2x * p.z + ox,
			c0y * p.x + c1y * p.y + c2y * p.z + oy,
			c0z * p.x + c1z * p.y + c2z * p.z + oz);
	}

	static inline SimVec3x4 transform_points_components4(
		float c0x, float c0y, float c0z,
		float c1x, float c1y, float c1z,
		float c2x, float c2y, float c2z,
		float ox, float oy, float oz,
		SimFloat4 px, SimFloat4 py, SimFloat4 pz)
	{
		return SimVec3x4(
			SimFloat4(c0x) * px + SimFloat4(c1x) * py + SimFloat4(c2x) * pz + SimFloat4(ox),
			SimFloat4(c0y) * px + SimFloat4(c1y) * py + SimFloat4(c2y) * pz + SimFloat4(oy),
			SimFloat4(c0z) * px + SimFloat4(c1z) * py + SimFloat4(c2z) * pz + SimFloat4(oz));
	}

	static void update_damage_visual_geometry_soa(PhysicsCarSoA& c, int count)
	{
		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) == 0) {
				continue;
			}

			const float c0x = c.basis_physical_c0x[i];
			const float c0y = c.basis_physical_c0y[i];
			const float c0z = c.basis_physical_c0z[i];
			const float c1x = c.basis_physical_c1x[i];
			const float c1y = c.basis_physical_c1y[i];
			const float c1z = c.basis_physical_c1z[i];
			const float c2x = c.basis_physical_c2x[i];
			const float c2y = c.basis_physical_c2y[i];
			const float c2z = c.basis_physical_c2z[i];
			const float ox = c.position_current_x[i];
			const float oy = c.position_current_y[i];
			const float oz = c.position_current_z[i];

			const int p = i * 4;
			sim_store4(c.tilt_pos_old_x + p, sim_load4(c.tilt_pos_x + p));
			sim_store4(c.tilt_pos_old_y + p, sim_load4(c.tilt_pos_y + p));
			sim_store4(c.tilt_pos_old_z + p, sim_load4(c.tilt_pos_z + p));
			const SimVec3x4 tilt_pos = transform_points_components4(
				c0x, c0y, c0z, c1x, c1y, c1z, c2x, c2y, c2z, ox, oy, oz,
				sim_load4(c.tilt_offset_x + p),
				sim_load4(c.tilt_offset_y + p) + sim_load4(c.tilt_force + p) - sim_load4(c.tilt_rest_length + p),
				sim_load4(c.tilt_offset_z + p));
			sim_store4(c.tilt_pos_x + p, tilt_pos.x);
			sim_store4(c.tilt_pos_y + p, tilt_pos.y);
			sim_store4(c.tilt_pos_z + p, tilt_pos.z);

			sim_store4(c.wall_pos_a_x + p, sim_load4(c.wall_pos_b_x + p));
			sim_store4(c.wall_pos_a_y + p, sim_load4(c.wall_pos_b_y + p));
			sim_store4(c.wall_pos_a_z + p, sim_load4(c.wall_pos_b_z + p));
			const SimVec3x4 wall_pos = transform_points_components4(
				c0x, c0y, c0z, c1x, c1y, c1z, c2x, c2y, c2z, ox, oy, oz,
				sim_load4(c.wall_offset_x + p),
				sim_load4(c.wall_offset_y + p),
				sim_load4(c.wall_offset_z + p));
			sim_store4(c.wall_pos_b_x + p, wall_pos.x);
			sim_store4(c.wall_pos_b_y + p, wall_pos.y);
			sim_store4(c.wall_pos_b_z + p, wall_pos.z);
		}
	}

	static void project_startup_velocity_and_speed_soa(PhysicsCarSoA& c, int count)
	{
		int i = 0;
		for (; i + 3 < count; i += 4) {
			const SimFloat4 vx = sim_load4(c.velocity_x + i);
			const SimFloat4 vy = sim_load4(c.velocity_y + i);
			const SimFloat4 vz = sim_load4(c.velocity_z + i);
			const SimFloat4 nx = sim_load4(c.track_surface_normal_x + i);
			const SimFloat4 ny = sim_load4(c.track_surface_normal_y + i);
			const SimFloat4 nz = sim_load4(c.track_surface_normal_z + i);
			const SimFloat4 startup_mask(
				c.frames_since_start_2[i + 0] <= 90 ? 1.0f : 0.0f,
				c.frames_since_start_2[i + 1] <= 90 ? 1.0f : 0.0f,
				c.frames_since_start_2[i + 2] <= 90 ? 1.0f : 0.0f,
				c.frames_since_start_2[i + 3] <= 90 ? 1.0f : 0.0f);
			const SimFloat4 dot = (vx * nx + vy * ny + vz * nz) * startup_mask;
			const SimFloat4 out_x = vx - nx * dot;
			const SimFloat4 out_y = vy - ny * dot;
			const SimFloat4 out_z = vz - nz * dot;
			sim_store4(c.velocity_x + i, out_x);
			sim_store4(c.velocity_y + i, out_y);
			sim_store4(c.velocity_z + i, out_z);

			const SimFloat4 speed = sim_sqrt4(out_x * out_x + out_y * out_y + out_z * out_z);
			const SimFloat4 inv_weight(
				std::abs(c.stat_weight[i + 0]) > 0.0001f ? 1.0f / c.stat_weight[i + 0] : 0.0f,
				std::abs(c.stat_weight[i + 1]) > 0.0001f ? 1.0f / c.stat_weight[i + 1] : 0.0f,
				std::abs(c.stat_weight[i + 2]) > 0.0001f ? 1.0f / c.stat_weight[i + 2] : 0.0f,
				std::abs(c.stat_weight[i + 3]) > 0.0001f ? 1.0f / c.stat_weight[i + 3] : 0.0f);
			const SimFloat4 countdown_mask(
				(c.machine_state[i + 0] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f,
				(c.machine_state[i + 1] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f,
				(c.machine_state[i + 2] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f,
				(c.machine_state[i + 3] & MACHINESTATE::STARTINGCOUNTDOWN) == 0 ? 1.0f : 0.0f);
			const SimFloat4 old_speed = sim_load4(c.speed_kmh + i);
			const SimFloat4 new_speed = speed * inv_weight * SimFloat4(216.0f);
			sim_store4(c.speed_kmh + i, old_speed + (new_speed - old_speed) * countdown_mask);
		}

		for (; i < count; ++i) {
			const float startup_mask = c.frames_since_start_2[i] <= 90 ? 1.0f : 0.0f;
			const float dot =
				(c.velocity_x[i] * c.track_surface_normal_x[i] +
				 c.velocity_y[i] * c.track_surface_normal_y[i] +
				 c.velocity_z[i] * c.track_surface_normal_z[i]) * startup_mask;
			c.velocity_x[i] -= c.track_surface_normal_x[i] * dot;
			c.velocity_y[i] -= c.track_surface_normal_y[i] * dot;
			c.velocity_z[i] -= c.track_surface_normal_z[i] * dot;
			if ((c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
				const float speed_sq =
					c.velocity_x[i] * c.velocity_x[i] +
					c.velocity_y[i] * c.velocity_y[i] +
					c.velocity_z[i] * c.velocity_z[i];
				const float inv_weight = std::abs(c.stat_weight[i]) > 0.0001f ? 1.0f / c.stat_weight[i] : 0.0f;
				c.speed_kmh[i] = 216.0f * std::sqrt(speed_sq) * inv_weight;
			}
		}
	}

	static void handle_vehicle_collision_result(GameSim& sim, PhysicsCar* car_views, int i, int j)
	{
		PhysicsCarSoA& car_a = *car_views[i].soa;
		PhysicsCarSoA& car_b = *car_views[j].soa;
		const int lane_a = car_views[i].soa_index;
		const int lane_b = car_views[j].soa_index;
		const uint32_t current_tick = static_cast<uint32_t>(car_a.simulation_tick[lane_a]);
		constexpr uint32_t kCollisionSparkCooldownFrames = 30;
		auto is_recent_hit = [&](PhysicsCarSoA& c, int lane) -> bool {
			if (!c.has_last_hit_tick[lane]) {
				return false;
			}
			const uint32_t delta = current_tick - c.last_hit_tick[lane];
			return delta < kCollisionSparkCooldownFrames;
		};

		const bool recently_hit = is_recent_hit(car_a, lane_a) || is_recent_hit(car_b, lane_b);
		const bool a_attacking = (car_a.machine_state[lane_a] & (MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING)) != 0;
		const bool b_attacking = (car_b.machine_state[lane_b] & (MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING)) != 0;
		int sparks_a = 3;
		int sparks_b = 3;
		if (a_attacking && b_attacking) {
			sparks_a = 6;
			sparks_b = 6;
		} else if (a_attacking && !b_attacking) {
			sparks_b = 8;
		} else if (!a_attacking && b_attacking) {
			sparks_a = 8;
		}
		if (!recently_hit) {
			if (sparks_a > 0) {
				sim.emit_super_sparks_from_car(car_views[i], sparks_a);
			}
			if (sparks_b > 0) {
				sim.emit_super_sparks_from_car(car_views[j], sparks_b);
			}
		}
		car_a.last_hit_tick[lane_a] = current_tick;
		car_b.last_hit_tick[lane_b] = current_tick;
		car_a.has_last_hit_tick[lane_a] = true;
		car_b.has_last_hit_tick[lane_b] = true;
	}

	static void collide_vehicles_broadphase(GameSim& sim, PhysicsCar* car_views, int count,
		int* indices, float* min_x, float* max_x, float* min_y, float* max_y, float* min_z, float* max_z)
	{
		constexpr float kMachineCollisionRadius = 2.0f;
		constexpr float kMutationSlop = 8.0f;

		for (int i = 0; i < count; ++i) {
			PhysicsCarSoA& c = *car_views[i].soa;
			const int lane = car_views[i].soa_index;
			c.position_collision_snapshot_x[lane] = c.position_current_x[lane];
			c.position_collision_snapshot_y[lane] = c.position_current_y[lane];
			c.position_collision_snapshot_z[lane] = c.position_current_z[lane];
			const float extent = kMachineCollisionRadius + c.speed_kmh[lane] / 216.0f + kMutationSlop;
			indices[i] = i;
			min_x[i] = c.position_current_x[lane] - extent;
			max_x[i] = c.position_current_x[lane] + extent;
			min_y[i] = c.position_current_y[lane] - extent;
			max_y[i] = c.position_current_y[lane] + extent;
			min_z[i] = c.position_current_z[lane] - extent;
			max_z[i] = c.position_current_z[lane] + extent;
		}

		std::sort(indices, indices + count, [&](int a, int b) {
			return min_x[a] < min_x[b];
		});

		for (int sorted_i = 0; sorted_i < count; ++sorted_i) {
			const int i = indices[sorted_i];
			const float max_i_x = max_x[i];
			for (int sorted_j = sorted_i + 1; sorted_j < count; ++sorted_j) {
				const int j = indices[sorted_j];
				if (min_x[j] > max_i_x) {
					break;
				}
				if (!intervals_overlap(min_y[i], max_y[i], min_y[j], max_y[j]) ||
					!intervals_overlap(min_z[i], max_z[i], min_z[j], max_z[j])) {
					continue;
				}
				if (car_views[i].handle_machine_v_machine_collision(car_views[j])) {
					handle_vehicle_collision_result(sim, car_views, i, j);
				}
			}
		}
	}
}

void GameSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("instantiate_gamesim", "lvldat_buf", "car_prop_buffers", "accel_settings"), &GameSim::instantiate_gamesim);
	ClassDB::bind_method(D_METHOD("destroy_gamesim"), &GameSim::destroy_gamesim);
	ClassDB::bind_method(D_METHOD("render_gamesim"), &GameSim::render_gamesim);
	ClassDB::bind_method(D_METHOD("get_sim_started"), &GameSim::get_sim_started);
	ClassDB::bind_method(D_METHOD("set_sim_started", "p_sim_started"), &GameSim::set_sim_started);
	ClassDB::bind_method(D_METHOD("set_spawn_seed", "seed"), &GameSim::set_spawn_seed);
	ClassDB::bind_method(D_METHOD("save_state"), &GameSim::save_state);
	ClassDB::bind_method(D_METHOD("load_state", "target_tick"), &GameSim::load_state);
	ClassDB::bind_method(D_METHOD("get_state_data", "target_tick"), &GameSim::get_state_data);
	ClassDB::bind_method(D_METHOD("set_state_data", "target_tick", "data"), &GameSim::set_state_data);
	ClassDB::bind_method(D_METHOD("render_gamesim_visuals_only"), &GameSim::render_gamesim_visuals_only);
	ClassDB::bind_method(D_METHOD("get_dip_switches"), &GameSim::get_dip_switches);
	ClassDB::bind_method(D_METHOD("is_dip_switch_enabled", "flag"), &GameSim::is_dip_switch_enabled);
	ClassDB::bind_method(D_METHOD("set_dip_switch_enabled", "flag", "enabled"), &GameSim::set_dip_switch_enabled);
	ClassDB::bind_method(D_METHOD("get_first_lap_distance"), &GameSim::get_first_lap_distance);
	ClassDB::bind_method(D_METHOD("set_cpu_driver_manager", "manager"), &GameSim::set_cpu_driver_manager);
	ClassDB::bind_method(D_METHOD("get_cpu_driver_manager"), &GameSim::get_cpu_driver_manager);
	ClassDB::bind_method(D_METHOD("get_native_cpu_input_for_tick", "player_id", "expected_tick"), &GameSim::get_native_cpu_input_for_tick);
	ClassDB::bind_method(D_METHOD("set_player_metadata", "player_ids", "cpu_flags"), &GameSim::set_player_metadata);
	ClassDB::bind_method(D_METHOD("get_phase_profile_string"), &GameSim::get_phase_profile_string);
	ClassDB::bind_method(D_METHOD("get_render_profile_string"), &GameSim::get_render_profile_string);
	ClassDB::bind_method(D_METHOD("get_player_race_place", "player_id"), &GameSim::get_player_race_place);
	ClassDB::bind_method(D_METHOD("get_player_render_transform", "player_id"), &GameSim::get_player_render_transform);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sim_started"), "set_sim_started", "get_sim_started");
	ClassDB::bind_method(D_METHOD("get_car_node_container"), &GameSim::get_car_node_container);
	ClassDB::bind_method(D_METHOD("set_car_node_container", "p_car_node_container"), &GameSim::set_car_node_container);
	ClassDB::bind_method(D_METHOD("get_spark_node_container"), &GameSim::get_spark_node_container);
	ClassDB::bind_method(D_METHOD("set_spark_node_container", "p_spark_node_container"), &GameSim::set_spark_node_container);
	ClassDB::bind_method(D_METHOD("set_car_render_manager", "p_car_render_manager"), &GameSim::set_car_render_manager);
	ClassDB::bind_method(D_METHOD("set_gameplay_camera", "p_camera", "player_id"), &GameSim::set_gameplay_camera);
	ClassDB::bind_method(D_METHOD("tick_singleplayer", "local_player_id", "local_input"), &GameSim::tick_singleplayer);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "car_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_car_node_container", "get_car_node_container");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spark_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_spark_node_container", "get_spark_node_container");
};

GameSim::GameSim()
{
	tick = 0;
	tick_delta = 1.0f / 60.0f;
	sim_started = false;
	car_node_container = nullptr;
	spark_node_container = nullptr;
	super_spark_state = nullptr;
	super_sparks = nullptr;
	spark_multimesh_instance = nullptr;
	num_cars = 0;
	cars = nullptr;
	car_properties_array = nullptr;
	reset_super_sparks();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = nullptr;
		state_buffer[i].size = 0;
	}
	input_buffer = nullptr;
};

GameSim::~GameSim()
{
	stop_vehicle_lane_workers();
	destroy_gamesim();
	free_vehicle_tick_soa();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		if (state_buffer[i].data)
		{
			::free(state_buffer[i].data);
			state_buffer[i].data = nullptr;
		}
	}
	if (input_buffer) {
		::free(input_buffer);
		input_buffer = nullptr;
	}
};

void GameSim::VehicleLaneGroup::reset(int p_count)
{
	std::lock_guard<std::mutex> lock(mutex);
	count = p_count;
	waiting = 0;
	generation = 0;
}

void GameSim::VehicleLaneGroup::sync()
{
	if (count <= 1) {
		return;
	}
	std::unique_lock<std::mutex> lock(mutex);
	const uint32_t local_generation = generation;
	waiting += 1;
	if (waiting == count) {
		waiting = 0;
		generation += 1;
		cv.notify_all();
		return;
	}
	cv.wait(lock, [&]() {
		return generation != local_generation;
	});
}

void GameSim::ensure_vehicle_lane_workers()
{
	if (vehicle_lane_workers_started) {
		return;
	}
	vehicle_lane_stop = false;
	for (int i = 0; i < VEHICLE_WORKER_COUNT - 1; ++i) {
		const int lane = i + 1;
		vehicle_lane_workers[i] = std::thread([this, lane]() {
			uint32_t seen_generation = 0;
			for (;;) {
				std::unique_lock<std::mutex> lock(vehicle_lane_mutex);
				vehicle_lane_cv.wait(lock, [&]() {
					return vehicle_lane_stop || vehicle_lane_generation != seen_generation;
				});
				if (vehicle_lane_stop) {
					return;
				}
				seen_generation = vehicle_lane_generation;
				const bool should_run = lane < vehicle_lane_active_count;
				auto fn = vehicle_lane_fn;
				lock.unlock();

				if (should_run) {
					fn(lane, vehicle_lane_group);
				}

				if (should_run) {
					lock.lock();
					vehicle_lane_pending -= 1;
					if (vehicle_lane_pending == 0) {
						vehicle_lane_done_cv.notify_one();
					}
				}
			}
		});
	}
	vehicle_lane_workers_started = true;
}

void GameSim::stop_vehicle_lane_workers()
{
	if (!vehicle_lane_workers_started) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_stop = true;
		vehicle_lane_generation += 1;
	}
	vehicle_lane_cv.notify_all();
	for (int i = 0; i < VEHICLE_WORKER_COUNT - 1; ++i) {
		if (vehicle_lane_workers[i].joinable()) {
			vehicle_lane_workers[i].join();
		}
	}
	vehicle_lane_fn = nullptr;
	vehicle_lane_active_count = 0;
	vehicle_lane_pending = 0;
	vehicle_lane_workers_started = false;
	vehicle_lane_stop = false;
}

void GameSim::run_vehicle_lanes(int lane_count, bool parallel, const std::function<void(int, VehicleLaneGroup&)>& fn)
{
	if (!parallel || lane_count <= 1) {
		VehicleLaneGroup group;
		group.reset(1);
		for (int lane = 0; lane < lane_count; ++lane) {
			fn(lane, group);
		}
		return;
	}

	const int active_lanes = std::min(lane_count, VEHICLE_WORKER_COUNT);
	ensure_vehicle_lane_workers();
	vehicle_lane_group.reset(active_lanes);
	{
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_fn = fn;
		vehicle_lane_active_count = active_lanes;
		vehicle_lane_pending = active_lanes - 1;
		vehicle_lane_generation += 1;
	}
	vehicle_lane_cv.notify_all();

	fn(0, vehicle_lane_group);

	if (active_lanes > 1) {
		std::unique_lock<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_done_cv.wait(lock, [&]() {
			return vehicle_lane_pending == 0;
		});
		vehicle_lane_fn = nullptr;
		vehicle_lane_active_count = 0;
	} else {
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_fn = nullptr;
		vehicle_lane_active_count = 0;
	}
}

void GameSim::free_vehicle_tick_soa()
{
	if (vehicle_tick_soa.inputs) {
		free_cache_aligned(vehicle_tick_soa.inputs);
		vehicle_tick_soa.inputs = nullptr;
	}
	if (vehicle_tick_soa.pre_distances) {
		free_cache_aligned(vehicle_tick_soa.pre_distances);
		vehicle_tick_soa.pre_distances = nullptr;
	}
	if (vehicle_tick_soa.placement_distances) {
		free_cache_aligned(vehicle_tick_soa.placement_distances);
		vehicle_tick_soa.placement_distances = nullptr;
	}
	if (vehicle_tick_soa.placement_indices) {
		free_cache_aligned(vehicle_tick_soa.placement_indices);
		vehicle_tick_soa.placement_indices = nullptr;
	}
	if (vehicle_tick_soa.pending_s_boost_sparks) {
		free_cache_aligned(vehicle_tick_soa.pending_s_boost_sparks);
		vehicle_tick_soa.pending_s_boost_sparks = nullptr;
	}
	if (vehicle_tick_soa.collision_indices) {
		free_cache_aligned(vehicle_tick_soa.collision_indices);
		vehicle_tick_soa.collision_indices = nullptr;
	}
	if (vehicle_tick_soa.collision_min_x) {
		free_cache_aligned(vehicle_tick_soa.collision_min_x);
		vehicle_tick_soa.collision_min_x = nullptr;
	}
	if (vehicle_tick_soa.collision_max_x) {
		free_cache_aligned(vehicle_tick_soa.collision_max_x);
		vehicle_tick_soa.collision_max_x = nullptr;
	}
	if (vehicle_tick_soa.collision_min_y) {
		free_cache_aligned(vehicle_tick_soa.collision_min_y);
		vehicle_tick_soa.collision_min_y = nullptr;
	}
	if (vehicle_tick_soa.collision_max_y) {
		free_cache_aligned(vehicle_tick_soa.collision_max_y);
		vehicle_tick_soa.collision_max_y = nullptr;
	}
	if (vehicle_tick_soa.collision_min_z) {
		free_cache_aligned(vehicle_tick_soa.collision_min_z);
		vehicle_tick_soa.collision_min_z = nullptr;
	}
	if (vehicle_tick_soa.collision_max_z) {
		free_cache_aligned(vehicle_tick_soa.collision_max_z);
		vehicle_tick_soa.collision_max_z = nullptr;
	}
	vehicle_tick_soa.capacity = 0;
}

void GameSim::ensure_vehicle_tick_soa_capacity(int capacity)
{
	if (capacity <= vehicle_tick_soa.capacity) {
		return;
	}

	free_vehicle_tick_soa();
	vehicle_tick_soa.inputs = static_cast<PlayerInput*>(alloc_cache_aligned(sizeof(PlayerInput) * capacity));
	vehicle_tick_soa.pre_distances = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.placement_distances = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.placement_indices = static_cast<int*>(alloc_cache_aligned(sizeof(int) * capacity));
	vehicle_tick_soa.pending_s_boost_sparks = static_cast<uint8_t*>(alloc_cache_aligned(sizeof(uint8_t) * capacity));
	vehicle_tick_soa.collision_indices = static_cast<int*>(alloc_cache_aligned(sizeof(int) * capacity));
	vehicle_tick_soa.collision_min_x = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_max_x = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_min_y = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_max_y = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_min_z = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	vehicle_tick_soa.collision_max_z = static_cast<float*>(alloc_cache_aligned(sizeof(float) * capacity));
	if (!vehicle_tick_soa.inputs || !vehicle_tick_soa.pre_distances ||
		!vehicle_tick_soa.placement_distances || !vehicle_tick_soa.placement_indices ||
		!vehicle_tick_soa.pending_s_boost_sparks || !vehicle_tick_soa.collision_indices ||
		!vehicle_tick_soa.collision_min_x || !vehicle_tick_soa.collision_max_x ||
		!vehicle_tick_soa.collision_min_y || !vehicle_tick_soa.collision_max_y ||
		!vehicle_tick_soa.collision_min_z || !vehicle_tick_soa.collision_max_z) {
		free_vehicle_tick_soa();
		std::abort();
	}
	vehicle_tick_soa.capacity = capacity;
}

void GameSim::set_sim_started(const bool p_sim_started)
{
	sim_started = p_sim_started;
}

bool GameSim::get_sim_started()
{
	return sim_started;
}

void GameSim::record_phase_profile_sample()
{
	VehicleTickSoA& soa = vehicle_tick_soa;
	uint32_t sample[PROFILE_FIELD_COUNT] = {
		soa.prof_total_us,
		soa.prof_input_us,
		soa.prof_begin_us,
		soa.prof_prepare_floor_us,
		soa.prof_project_us,
		soa.prof_steer_susp_us,
		soa.prof_linear_us,
		soa.prof_integrate_us,
		soa.prof_collision_us,
		soa.prof_post_us,
		soa.prof_post_response_us,
		soa.prof_post_sample_old_us,
		soa.prof_post_corners_us,
		soa.prof_post_apply_response_us,
		soa.prof_post_project_speed_us,
		soa.prof_post_visual_geom_us,
		soa.prof_post_damage_tail_us,
		soa.prof_misc_us,
		soa.prof_lane_group_us,
		soa.prof_lanes,
	};

	if (profile_count == PROFILE_WINDOW_TICKS) {
		for (int i = 0; i < PROFILE_FIELD_COUNT; ++i) {
			profile_sums[i] -= profile_samples[profile_cursor][i];
		}
	} else {
		profile_count += 1;
	}

	for (int i = 0; i < PROFILE_FIELD_COUNT; ++i) {
		profile_samples[profile_cursor][i] = sample[i];
		profile_sums[i] += sample[i];
	}
	profile_cursor = (profile_cursor + 1) % PROFILE_WINDOW_TICKS;
}

String GameSim::get_phase_profile_string() const
{
	const int count = profile_count > 0 ? profile_count : 1;
	auto avg = [&](int field) -> int64_t {
		return static_cast<int64_t>(profile_sums[field] / static_cast<uint64_t>(count));
	};

	String out = "MXT_PHASE_AVG_US frames=" + String::num_int64(profile_count);
	out += " total=" + String::num_int64(avg(PROFILE_TOTAL));
	out += " input=" + String::num_int64(avg(PROFILE_INPUT));
	out += " begin=" + String::num_int64(avg(PROFILE_BEGIN));
	out += " prepare_floor=" + String::num_int64(avg(PROFILE_PREPARE_FLOOR));
	out += " project=" + String::num_int64(avg(PROFILE_PROJECT));
	out += " steer_susp=" + String::num_int64(avg(PROFILE_STEER_SUSP));
	out += " linear=" + String::num_int64(avg(PROFILE_LINEAR));
	out += " integrate=" + String::num_int64(avg(PROFILE_INTEGRATE));
	out += " collision=" + String::num_int64(avg(PROFILE_COLLISION));
	out += " post=" + String::num_int64(avg(PROFILE_POST));
	out += " post_response=" + String::num_int64(avg(PROFILE_POST_RESPONSE));
	out += " post_sample_old=" + String::num_int64(avg(PROFILE_POST_SAMPLE_OLD));
	out += " post_corners=" + String::num_int64(avg(PROFILE_POST_CORNERS));
	out += " post_apply_response=" + String::num_int64(avg(PROFILE_POST_APPLY_RESPONSE));
	out += " post_project_speed=" + String::num_int64(avg(PROFILE_POST_PROJECT_SPEED));
	out += " post_visual_geom=" + String::num_int64(avg(PROFILE_POST_VISUAL_GEOM));
	out += " post_damage_tail=" + String::num_int64(avg(PROFILE_POST_DAMAGE_TAIL));
	out += " misc=" + String::num_int64(avg(PROFILE_MISC));
	out += " lane_group=" + String::num_int64(avg(PROFILE_LANE_GROUP));
	out += " lanes=" + String::num_int64(avg(PROFILE_LANES));
	return out;
}

void GameSim::record_render_profile_sample(const uint32_t sample[RENDER_PROFILE_FIELD_COUNT])
{
	if (render_profile_count == PROFILE_WINDOW_TICKS) {
		for (int i = 0; i < RENDER_PROFILE_FIELD_COUNT; ++i) {
			render_profile_sums[i] -= render_profile_samples[render_profile_cursor][i];
		}
	} else {
		render_profile_count += 1;
	}

	for (int i = 0; i < RENDER_PROFILE_FIELD_COUNT; ++i) {
		render_profile_samples[render_profile_cursor][i] = sample[i];
		render_profile_sums[i] += sample[i];
	}
	render_profile_cursor = (render_profile_cursor + 1) % PROFILE_WINDOW_TICKS;
}

String GameSim::get_render_profile_string() const
{
	const int count = render_profile_count > 0 ? render_profile_count : 1;
	auto avg = [&](int field) -> int64_t {
		return static_cast<int64_t>(render_profile_sums[field] / static_cast<uint64_t>(count));
	};

	String out = "MXT_RENDER_AVG_US frames=" + String::num_int64(render_profile_count);
	out += " total=" + String::num_int64(avg(RENDER_PROFILE_TOTAL));
	out += " get_children=" + String::num_int64(avg(RENDER_PROFILE_GET_CHILDREN));
	out += " visual_apply=" + String::num_int64(avg(RENDER_PROFILE_VISUAL_APPLY));
	out += " cpu_total=" + String::num_int64(avg(RENDER_PROFILE_CPU_TOTAL));
	out += " cpu_build_obs=" + String::num_int64(avg(RENDER_PROFILE_CPU_BUILD_OBS));
	out += " cpu_submit=" + String::num_int64(avg(RENDER_PROFILE_CPU_SUBMIT));
	out += " sparks=" + String::num_int64(avg(RENDER_PROFILE_SPARKS));
	out += " debug_draw=" + String::num_int64(avg(RENDER_PROFILE_DEBUG_DRAW));
	out += " vis_cars=" + String::num_int64(avg(RENDER_PROFILE_VIS_CARS));
	return out;
}

int GameSim::get_player_race_place(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 1;
	}

	int target_index = -1;
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] == player_id) {
			target_index = i;
			break;
		}
	}
	if (target_index < 0) {
		return 1;
	}

	PhysicsCarSoA& target_soa = *cars[target_index].soa;
	const int target_lane = cars[target_index].soa_index;
	const int target_lap = target_soa.lap[target_lane];
	const float target_progress = target_soa.lap_progress[target_lane];

	int place = 1;
	for (int i = 0; i < num_cars; ++i) {
		if (i == target_index) {
			continue;
		}
		PhysicsCarSoA& other_soa = *cars[i].soa;
		const int other_lane = cars[i].soa_index;
		const int other_lap = other_soa.lap[other_lane];
		const float other_progress = other_soa.lap_progress[other_lane];
		if (other_lap > target_lap || (other_lap == target_lap && other_progress > target_progress)) {
			place += 1;
		}
	}
	return place;
}

godot::Transform3D GameSim::get_player_render_transform(int player_id) const
{
	if (!car_player_ids || render_final_current_transforms.empty()) {
		return godot::Transform3D();
	}
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		SimTransform render_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		return gd_transform(render_transform);
	}
	return godot::Transform3D();
}

void GameSim::tick_singleplayer(int local_player_id, godot::PackedByteArray local_input)
{
	const PlayerInput decoded_local_input = PlayerInput::from_bytes(local_input);
	tick_gamesim_internal(InputFrameMode::SingleLocal, local_player_id, &decoded_local_input, nullptr, nullptr, 0);
}

void GameSim::tick_gamesim_internal(InputFrameMode mode,
	int local_player_id,
	const PlayerInput* local_input,
	const PlayerInput* decoded_car_inputs,
	const uint8_t* decoded_car_input_present,
	int decoded_car_input_count)
{
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	std::fesetround(FE_TONEAREST);
	std::feclearexcept(FE_ALL_EXCEPT);

	if (num_cars <= 0 || !cars)
	{
		save_state();
		tick += 1;
		return;
	}

	auto start = std::chrono::high_resolution_clock::now();
	VehicleTickSoA& soa = vehicle_tick_soa;
	PhysicsCarSoA& first_shard = *cars[0].soa;
	PhysicsCarSoA* car_shards = first_shard.shards ? first_shard.shards : &first_shard;
	const int car_shard_count = first_shard.shards ? first_shard.shard_count : 1;
	const int sim_lane_count = first_shard.total_lane_count > 0 ? first_shard.total_lane_count : first_shard.lane_count;
	const bool parallel_vehicle_shards = num_cars >= 16 && car_shard_count == VEHICLE_WORKER_COUNT;
	const int active_vehicle_lanes = parallel_vehicle_shards ? std::min(car_shard_count, VEHICLE_WORKER_COUNT) : 1;
	ensure_vehicle_tick_soa_capacity(sim_lane_count);
	int buf_index = tick % INPUT_BUFFER_LEN;
	PlayerInput* slot = input_buffer + buf_index * num_cars;
	auto phase_start = std::chrono::high_resolution_clock::now();

	float lead_distance = 0.0f;
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const float distance = compute_vehicle_distance_along_track(
			car_soa.current_checkpoint[lane], car_soa.checkpoint_fraction[lane], car_soa.lap[lane]);
		soa.pre_distances[i] = distance;
		if (distance > lead_distance) {
			lead_distance = distance;
		}
	}

	for (int i = 0; i < num_cars; i++) {
		PlayerInput inp = PlayerInput::from_neutral();
		const int32_t player_id = car_player_ids ? car_player_ids[i] : -1;
		if (mode == InputFrameMode::SingleLocal && player_id == local_player_id && local_input) {
			inp = *local_input;
		} else if (mode == InputFrameMode::DecodedCarArray && i < decoded_car_input_count && decoded_car_inputs &&
				(!decoded_car_input_present || decoded_car_input_present[i])) {
			inp = decoded_car_inputs[i];
		} else if (car_is_cpu && car_is_cpu[i]) {
			inp = PlayerInput::from_bytes(generate_native_cpu_input_for_tick(player_id, tick));
		}
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if (!car_soa.s_boost_active[lane] && car_soa.s_boost_charge[lane] >= car_soa.s_boost_charge_max[lane] && inp.boost) {
			float gap = lead_distance - soa.pre_distances[i];
			if (gap < 0.0f) {
				gap = 0.0f;
			}
			uint16_t duration_frames = compute_s_boost_duration_frames(gap);
			if (duration_frames == 0)
				duration_frames = 1;
			car_soa.s_boost_active[lane] = true;
			car_soa.s_boost_frames_remaining[lane] = duration_frames;
			car_soa.s_boost_charge[lane] = 0;
			car_soa.s_boost_emit_frame_accumulator[lane] = 0;
			car_soa.s_boost_pending_spark_spawns[lane] = 0;
			car_soa.boost_frames[lane] = 0;
			car_soa.boost_frames_manual[lane] = 0;
			car_soa.boost_turbo[lane] = 0.0f;
			car_soa.dashplate_heat_multiplier[lane] = 1.0f;
			car_soa.boost_delay_frame_counter[lane] = 0;
			car_soa.car_hit_invincibility[lane] = 0;
			car_soa.machine_state[lane] &= ~(MACHINESTATE::JUST_PRESSED_BOOST |
				MACHINESTATE::BOOSTING |
				MACHINESTATE::BOOSTING_DASHPLATE |
				MACHINESTATE::SIDEATTACKING |
				MACHINESTATE::SPINATTACKING |
				MACHINESTATE::TOOKDAMAGE |
				MACHINESTATE::LOWGRIP);
			inp.boost = false;
		}
		soa.inputs[i] = inp;
		slot[i] = inp;
	}
	for (int i = num_cars; i < sim_lane_count; ++i) {
		soa.inputs[i] = PlayerInput::from_neutral();
		soa.pending_s_boost_sparks[i] = 0;
	}
	auto now = std::chrono::high_resolution_clock::now();
	soa.prof_input_us = elapsed_us(phase_start, now);

	soa.prof_prepare_floor_us = 0;
	soa.prof_project_us = 0;
	soa.prof_steer_susp_us = 0;
	soa.prof_linear_us = 0;
	soa.prof_integrate_us = 0;
	uint32_t lane_prepare_floor_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t lane_project_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t lane_steer_susp_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t lane_linear_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t lane_integrate_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t lane_collision_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_response_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_checkpoints_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_sparks_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_sample_old_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_corners_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_apply_response_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_project_speed_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_visual_geom_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_damage_tail_us[VEHICLE_WORKER_COUNT] = {};
	uint32_t post_total_us[VEHICLE_WORKER_COUNT] = {};

	auto lane_group_start = std::chrono::high_resolution_clock::now();
	run_vehicle_lanes(car_shard_count, parallel_vehicle_shards, [&](int lane, VehicleLaneGroup& group) {
		PhysicsCarSoA& car_soa = car_shards[lane];
		const int global_start = car_soa.global_start;
		TrackQueryScratch &track_scratch = vehicle_lane_track_scratch[lane];

		begin_vehicle_tick_soa(car_soa, cars + global_start,
			soa.inputs + global_start, static_cast<uint32_t>(tick), car_soa.count);
		group.sync();

		uint32_t prepare_floor_us = 0;
		uint32_t project_us = 0;
		uint32_t steer_susp_us = 0;
		uint32_t linear_us = 0;
		uint32_t integrate_us = 0;
		begin_vehicle_motion_phased_soa(car_soa, cars + global_start,
			soa.inputs + global_start, car_soa.count, track_scratch, prepare_floor_us);
		lane_prepare_floor_us[lane] = prepare_floor_us;
		group.sync();

		if (lane == 0) {
			commit_vehicle_trigger_events(car_shards, cars, car_shard_count, vehicle_lane_track_scratch);
		}
		group.sync();

		finish_vehicle_motion_phased_soa(car_soa, cars + global_start, car_soa.count,
			project_us, steer_susp_us, linear_us, integrate_us);
		lane_project_us[lane] = project_us;
		lane_steer_susp_us[lane] = steer_susp_us;
		lane_linear_us[lane] = linear_us;
		lane_integrate_us[lane] = integrate_us;
		group.sync();

		finish_vehicle_tick_soa(car_soa, car_soa.count);
		group.sync();

		if (lane == 0) {
			auto collision_start = std::chrono::high_resolution_clock::now();
			collide_vehicles_broadphase(*this, cars, num_cars,
				soa.collision_indices,
				soa.collision_min_x, soa.collision_max_x,
				soa.collision_min_y, soa.collision_max_y,
				soa.collision_min_z, soa.collision_max_z);
			auto collision_end = std::chrono::high_resolution_clock::now();
			lane_collision_us[lane] = elapsed_us(collision_start, collision_end);
		}
		group.sync();

		uint32_t response_us = 0;
		uint32_t checkpoints_us = 0;
		uint32_t sparks_us = 0;
		uint32_t sample_old_us = 0;
		uint32_t corners_us = 0;
		uint32_t apply_response_us = 0;
		uint32_t project_speed_us = 0;
		uint32_t visual_geom_us = 0;
		uint32_t damage_tail_us = 0;
		post_vehicle_tick_soa(car_soa, cars + global_start,
			soa.pending_s_boost_sparks + global_start, car_soa.count, track_scratch,
			response_us, checkpoints_us, sparks_us,
			sample_old_us, corners_us, apply_response_us,
			project_speed_us, visual_geom_us, damage_tail_us);
		post_response_us[lane] = response_us;
		post_checkpoints_us[lane] = checkpoints_us;
		post_sparks_us[lane] = sparks_us;
		post_sample_old_us[lane] = sample_old_us;
		post_corners_us[lane] = corners_us;
		post_apply_response_us[lane] = apply_response_us;
		post_project_speed_us[lane] = project_speed_us;
		post_visual_geom_us[lane] = visual_geom_us;
		post_damage_tail_us[lane] = damage_tail_us;
		post_total_us[lane] = response_us + checkpoints_us + sparks_us;
	});
	now = std::chrono::high_resolution_clock::now();
	const uint32_t lane_group_us = elapsed_us(lane_group_start, now);
	soa.prof_lane_group_us = lane_group_us;
	soa.prof_lanes = static_cast<uint32_t>(active_vehicle_lanes);

	soa.prof_begin_us = 0;
	for (int lane = 0; lane < car_shard_count; ++lane) {
		soa.prof_prepare_floor_us = std::max(soa.prof_prepare_floor_us, lane_prepare_floor_us[lane]);
		soa.prof_project_us = std::max(soa.prof_project_us, lane_project_us[lane]);
		soa.prof_steer_susp_us = std::max(soa.prof_steer_susp_us, lane_steer_susp_us[lane]);
		soa.prof_linear_us = std::max(soa.prof_linear_us, lane_linear_us[lane]);
		soa.prof_integrate_us = std::max(soa.prof_integrate_us, lane_integrate_us[lane]);
	}

	soa.prof_collision_us = lane_collision_us[0];
	soa.prof_post_us = 0;
	soa.prof_post_response_us = 0;
	soa.prof_post_checkpoints_us = 0;
	soa.prof_post_sparks_us = 0;
	soa.prof_post_sample_old_us = 0;
	soa.prof_post_corners_us = 0;
	soa.prof_post_apply_response_us = 0;
	soa.prof_post_project_speed_us = 0;
	soa.prof_post_visual_geom_us = 0;
	soa.prof_post_damage_tail_us = 0;
	for (int shard = 0; shard < car_shard_count; ++shard) {
		soa.prof_post_us = std::max(soa.prof_post_us, post_total_us[shard]);
		soa.prof_post_response_us = std::max(soa.prof_post_response_us, post_response_us[shard]);
		soa.prof_post_checkpoints_us = std::max(soa.prof_post_checkpoints_us, post_checkpoints_us[shard]);
		soa.prof_post_sparks_us = std::max(soa.prof_post_sparks_us, post_sparks_us[shard]);
		soa.prof_post_sample_old_us = std::max(soa.prof_post_sample_old_us, post_sample_old_us[shard]);
		soa.prof_post_corners_us = std::max(soa.prof_post_corners_us, post_corners_us[shard]);
		soa.prof_post_apply_response_us = std::max(soa.prof_post_apply_response_us, post_apply_response_us[shard]);
		soa.prof_post_project_speed_us = std::max(soa.prof_post_project_speed_us, post_project_speed_us[shard]);
		soa.prof_post_visual_geom_us = std::max(soa.prof_post_visual_geom_us, post_visual_geom_us[shard]);
		soa.prof_post_damage_tail_us = std::max(soa.prof_post_damage_tail_us, post_damage_tail_us[shard]);
	}
	for (int i = 0; i < num_cars; i++) {
		if (soa.pending_s_boost_sparks[i] > 0) {
			emit_super_sparks_from_car(cars[i], soa.pending_s_boost_sparks[i]);
		}
	}

	phase_start = now;
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		soa.placement_distances[i] = compute_vehicle_distance_along_track(
			car_soa.current_checkpoint[lane], car_soa.checkpoint_fraction[lane], car_soa.lap[lane]);
		soa.placement_indices[i] = i;
	}
	std::sort(soa.placement_indices, soa.placement_indices + num_cars, [&](int a, int b) {
		return soa.placement_distances[a] > soa.placement_distances[b];
	});

	if (super_spark_state) {
		super_spark_state->placement_timer += 1;
		while (super_spark_state->placement_timer >= 120) {
			if (num_cars > 0) {
				emit_super_sparks_from_car(cars[soa.placement_indices[0]], 4);
			}
			if (num_cars > 1) {
				emit_super_sparks_from_car(cars[soa.placement_indices[1]], 3);
			}
			if (num_cars > 2) {
				emit_super_sparks_from_car(cars[soa.placement_indices[2]], 2);
			}
			super_spark_state->placement_timer -= 120;
		}
	}

	update_super_sparks();
	now = std::chrono::high_resolution_clock::now();
	soa.prof_misc_us = elapsed_us(phase_start, now);

	//for (int i = 0; i < num_cars; i++)
	//{
	//	if (i == 0){
	//		CollisionData collision;
	//		godot::Vector3 p0 = cars[i].position + godot::Vector3(0, 5, 3);
	//		godot::Vector3 p1 = cars[i].position + godot::Vector3(0, -100, 3);
	//		current_track->cast_vs_track(collision, p0, p1, CAST_FLAGS::WANTS_TRACK, cars[i].soa->current_collision_checkpoint[cars[i].soa_index]);
	//		if (collision.collided){
	//			dd3d->call("draw_arrow", collision.collision_point, collision.collision_point + collision.collision_normal * 2, godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//		}
	//		dd3d->call("draw_arrow", p0, p1, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
	//	}
	//}
	save_state();
	auto end = std::chrono::high_resolution_clock::now();
	soa.prof_total_us = elapsed_us(start, end);
	record_phase_profile_sample();
#if 0
	if ((tick % 120) == 0) {
		UtilityFunctions::print("MXT_PHASE_US total=", static_cast<int64_t>(soa.prof_total_us),
			" input=", static_cast<int64_t>(soa.prof_input_us),
			" begin=", static_cast<int64_t>(soa.prof_begin_us),
			" prepare_floor=", static_cast<int64_t>(soa.prof_prepare_floor_us),
			" project=", static_cast<int64_t>(soa.prof_project_us),
			" steer_susp=", static_cast<int64_t>(soa.prof_steer_susp_us),
			" linear=", static_cast<int64_t>(soa.prof_linear_us),
			" integrate=", static_cast<int64_t>(soa.prof_integrate_us),
			" collision=", static_cast<int64_t>(soa.prof_collision_us),
			" post=", static_cast<int64_t>(soa.prof_post_us),
			" post_response=", static_cast<int64_t>(soa.prof_post_response_us),
			" post_checkpoints=", static_cast<int64_t>(soa.prof_post_checkpoints_us),
			" post_sparks=", static_cast<int64_t>(soa.prof_post_sparks_us),
			" post_sample_old=", static_cast<int64_t>(soa.prof_post_sample_old_us),
			" post_corners=", static_cast<int64_t>(soa.prof_post_corners_us),
			" post_apply_response=", static_cast<int64_t>(soa.prof_post_apply_response_us),
			" post_project_speed=", static_cast<int64_t>(soa.prof_post_project_speed_us),
			" post_visual_geom=", static_cast<int64_t>(soa.prof_post_visual_geom_us),
			" post_damage_tail=", static_cast<int64_t>(soa.prof_post_damage_tail_us),
			" misc=", static_cast<int64_t>(soa.prof_misc_us),
			" lane_group=", static_cast<int64_t>(soa.prof_lane_group_us),
			" lanes=", static_cast<int64_t>(soa.prof_lanes));
	}
#endif

	//auto elapsed = std::chrono::high_resolution_clock::now() - start;
	//long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	//godot::Object* dd2d = godot::Engine::get_singleton()->get_singleton("DebugDraw2D");
	//dd2d->call("set_text", "frame time us", microseconds);
	
	
	tick += 1;
	//dd2d->call("set_text", "pos 1", car_positions[0]);
	
	//dd3d->call("draw_points", car_positions, 0, 1.0f, godot::Color(1.f, 0.f, 0.f), 0.0166666);
}

Array GameSim::get_dip_switches() const
{
	Array switches;
	for (const auto& def : DIP_SWITCH_DEFINITIONS) {
		Dictionary entry;
		entry["key"] = String(def.key);
		entry["label"] = String(def.label);
		entry["flag"] = def.flag;
		entry["enabled"] = DEBUG::dip_enabled(def.flag);
		switches.push_back(entry);
	}
	return switches;
}

bool GameSim::is_dip_switch_enabled(int flag) const
{
	return DEBUG::dip_enabled(flag);
}

void GameSim::set_dip_switch_enabled(int flag, bool enabled)
{
	if (enabled) {
		DEBUG::enable_dip(flag);
	} else {
		DEBUG::disable_dip(flag);
	}
}

double GameSim::get_first_lap_distance() const
{
	if (!sim_started || !cars || num_cars <= 0 || !current_track)
	{
		return 0.0;
	}
	return static_cast<double>(compute_car_distance_along_track(cars[0]));
}

void GameSim::instantiate_gamesim(StreamPeerBuffer* lvldat_buf, godot::Array car_prop_buffers, godot::Array accel_settings)
{
	if (Engine::get_singleton()->is_editor_hint()) return;

	tick = 0;
	std::memset(profile_samples, 0, sizeof(profile_samples));
	std::memset(profile_sums, 0, sizeof(profile_sums));
	profile_cursor = 0;
	profile_count = 0;

	int32_t buffer_size = lvldat_buf->get_size();
	const int requested_cars_hint = car_prop_buffers.size() > 0 ? car_prop_buffers.size() : 1;

	level_data.instantiate(1024 * 1024 * 16);

	gamestate_data.instantiate(1024 * 1024 + static_cast<size_t>(requested_cars_hint) * 8192u);
	spark_multimesh_instance = nullptr;
	super_spark_state = gamestate_data.allocate_object<SuperSparkState>();
	if (super_spark_state) {
		super_spark_state->cursor = 0;
		super_spark_state->placement_timer = 0;
		super_spark_state->rng_state = 1;
		super_sparks = super_spark_state->sparks;
		reset_super_sparks();
		super_spark_state->rng_state = static_cast<uint32_t>(spawn_seed) ^ 0xA511E9B1u;
		if (super_spark_state->rng_state == 0) {
			super_spark_state->rng_state = 1;
		}
	} else {
		super_sparks = nullptr;
		spark_multimesh_instance = nullptr;
	}
	int state_capacity = gamestate_data.get_capacity();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = (char*)malloc(state_capacity);
		state_buffer[i].size = 0;
	}

	current_track = level_data.allocate_class<RaceTrack>();
	current_track->num_trigger_colliders = 0;
	current_track->trigger_colliders = nullptr;
	current_track->lap_length = 0.0f;

	uint32_t header_size = lvldat_buf->get_u32();
	String version_string = lvldat_buf->get_string(4);
	uint32_t checkpoint_count = lvldat_buf->get_u32();
	uint32_t segment_count = lvldat_buf->get_u32();
	uint32_t trigger_count = 0;
	if (version_string != "v0.1" && version_string != "v0.2") {
		trigger_count = lvldat_buf->get_u32();
	}

	std::vector<uint32_t> neighboring_checkpoint_indices;


	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_CHECKPOINTS);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA);
	//DEBUG::enable_dip(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS);
	// load in collision checkpoints //

	current_track->num_checkpoints = checkpoint_count;
	current_track->checkpoints = level_data.allocate_array<CollisionCheckpoint>(checkpoint_count);

	for (int i = 0; i < checkpoint_count; i++)
	{
		current_track->checkpoints[i].position_start[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_start[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_start[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].position_end[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[0][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[1][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start[2][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[0][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[1][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][0] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][1] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_end[2][2] = lvldat_buf->get_float();
		current_track->checkpoints[i].orientation_start.orthonormalize();
		current_track->checkpoints[i].orientation_end.orthonormalize();
		current_track->checkpoints[i].x_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_start = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].y_radius_end = lvldat_buf->get_float();
		current_track->checkpoints[i].t_start = lvldat_buf->get_float();
		current_track->checkpoints[i].t_end = lvldat_buf->get_float();
		current_track->checkpoints[i].local_distance = lvldat_buf->get_float();
		current_track->checkpoints[i].distance = 0.0f;
		current_track->checkpoints[i].road_segment = (int)lvldat_buf->get_u32();
		current_track->checkpoints[i].start_plane.normal[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.normal[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.normal[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].start_plane.d = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[0] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[1] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.normal[2] = lvldat_buf->get_float();
		current_track->checkpoints[i].end_plane.d = lvldat_buf->get_float();
		current_track->checkpoints[i].x_radius_start_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].x_radius_start);
		current_track->checkpoints[i].y_radius_start_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].y_radius_start);
		current_track->checkpoints[i].x_radius_end_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].x_radius_end);
		current_track->checkpoints[i].y_radius_end_inv = 1.0f / fmaxf(0.001f, current_track->checkpoints[i].y_radius_end);
		int num_n_cp = (int)lvldat_buf->get_u32();

		current_track->checkpoints[i].num_neighboring_checkpoints = num_n_cp;

		current_track->checkpoints[i].neighboring_checkpoints = level_data.allocate_array<int>(num_n_cp);
		for (int n = 0; n < num_n_cp; n++)
		{
			current_track->checkpoints[i].neighboring_checkpoints[n] = (int)lvldat_buf->get_u32();
		}
	}

	current_track->compute_checkpoint_distances();

	// load in track segments //
	current_track->minimum_y = 0.0f;

	current_track->num_segments = segment_count;
	current_track->segments = level_data.allocate_array<TrackSegment>(segment_count);

	for (int seg = 0; seg < segment_count; seg++)
	{
		int segment_index = (int)lvldat_buf->get_u32();
		int road_type = (int)lvldat_buf->get_u32();

		// what road shape? //

		if (road_type == 0)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShape>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_FLAT;
		}
		else if (road_type == 1)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinder>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER;
		}
		else if (road_type == 2)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapeCylinderOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN;
		}
		else if (road_type == 3)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipe>();
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE;
		}
		else if (road_type == 4)
		{
			current_track->segments[seg].road_shape = level_data.allocate_class<RoadShapePipeOpen>();
			current_track->segments[seg].road_shape->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
		}
		else if (road_type == 5)
		{
			auto* rs = level_data.allocate_class<RoadShapeRoundedRect>();
			rs->width = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->height = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->radius = level_data.allocate_curve_from_buffer(lvldat_buf);
			current_track->segments[seg].road_shape = rs;
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT;
		}
		else if (road_type == 6)
		{
			auto* rs = level_data.allocate_class<RoadShapeRoundedRectOpen>();
			rs->width = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->height = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->radius = level_data.allocate_curve_from_buffer(lvldat_buf);
			rs->openness = level_data.allocate_curve_from_buffer(lvldat_buf);
                // Seam rotation curve added starting in v0.4
			if (version_string != String("v0.1") && version_string != String("v0.2") && version_string != String("v0.3")) {
				rs->open_rotation = level_data.allocate_curve_from_buffer(lvldat_buf);
			} else {
				rs->open_rotation = nullptr;
			}
			current_track->segments[seg].road_shape = rs;
			current_track->segments[seg].road_shape->shape_type = ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
		}

		// road modulations //

		int modulation_count = (int)lvldat_buf->get_u32();
		current_track->segments[seg].road_shape->num_modulations = modulation_count;

		if (modulation_count > 0)
		{
			current_track->segments[seg].road_shape->road_modulations = level_data.allocate_array<RoadModulation>(modulation_count);
			for (int mod = 0; mod < modulation_count; mod++)
			{
				current_track->segments[seg].road_shape->road_modulations[mod].modulation_effect = level_data.allocate_curve_from_buffer(lvldat_buf);
				current_track->segments[seg].road_shape->road_modulations[mod].modulation_height = level_data.allocate_curve_from_buffer(lvldat_buf);
			}
		}

		// road embeds //

		int embed_count = (int)lvldat_buf->get_u32();
		current_track->segments[seg].road_shape->num_embeds = embed_count;
		if (embed_count > 0)
		{
			current_track->segments[seg].road_shape->road_embeds = level_data.allocate_array<RoadEmbed>(embed_count);
			for (int embed = 0; embed < embed_count; embed++)
			{
				current_track->segments[seg].road_shape->road_embeds[embed].start_offset = lvldat_buf->get_float();
				current_track->segments[seg].road_shape->road_embeds[embed].end_offset = lvldat_buf->get_float();
				int desired_embed = (int)lvldat_buf->get_u32();
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::RECHARGE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::RECHARGE;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::DIRT){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::DIRT;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::ICE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::ICE;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::LAVA){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::LAVA;
				}
				if (desired_embed == EMBED_TYPE_TO_TERRAIN::HOLE){
					current_track->segments[seg].road_shape->road_embeds[embed].embed_type = TERRAIN::HOLE;
				}
				
				current_track->segments[seg].road_shape->road_embeds[embed].left_border = level_data.allocate_curve_from_buffer(lvldat_buf);
				current_track->segments[seg].road_shape->road_embeds[embed].right_border = level_data.allocate_curve_from_buffer(lvldat_buf);
			}
		}

		current_track->segments[seg].road_shape->owning_segment = &current_track->segments[seg];

		int pos = lvldat_buf->get_position();
		int num_keyframes = static_cast<int>(lvldat_buf->get_u32());
		lvldat_buf->seek(pos);
      // 1) allocate the SoA object itself on your heap
		{
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 31;
			if (mis) {
				level_data.allocate_bytes(32 - mis);
			}
		}
		void *raw = level_data.allocate_bytes(sizeof(RoadTransformCurve));
		RoadTransformCurve *soa = new (raw) RoadTransformCurve(num_keyframes);
		current_track->segments[seg].curve_matrix = soa;

		auto align32 = [&]() {
			uintptr_t addr = reinterpret_cast<uintptr_t>(level_data.heap_allocation);
			uintptr_t mis = addr & 31;
			if (mis) {
				level_data.allocate_bytes(32 - mis);
			}
		};

		// 2) allocate each float array, after aligning
		align32();
		soa->times       = level_data.allocate_array<float>(num_keyframes);

		align32();
		soa->values      = level_data.allocate_array<float>(num_keyframes * 16);

		align32();
		soa->tangent_in  = level_data.allocate_array<float>(num_keyframes * 16);

		align32();
		soa->tangent_out = level_data.allocate_array<float>(num_keyframes * 16);

		int seg_count = num_keyframes > 0 ? num_keyframes - 1 : 0;

		align32();
		soa->inv_dt  = level_data.allocate_array<float>(seg_count);
		align32();
		soa->coef_a  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_b  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_c  = level_data.allocate_array<float>(seg_count * 16);
		align32();
		soa->coef_d  = level_data.allocate_array<float>(seg_count * 16);

		// 3) fill your keyframes
		for (int n = 0; n < 15; ++n) {
			int cnt = static_cast<int>(lvldat_buf->get_u32());

			for (int i = 0; i < num_keyframes; ++i) {
				float t = lvldat_buf->get_float();
				if (n == 0) soa->times[i] = t;	// write time once

				int idx = i*16 + n;
				soa->values[idx]      = lvldat_buf->get_float();
				soa->tangent_in[idx]  = lvldat_buf->get_float();
				soa->tangent_out[idx] = lvldat_buf->get_float();
			}
		}

		for (int i = 0; i < num_keyframes; ++i) {
			int idx = i*16 + 15;
			soa->values[idx]      = 0.0f;
			soa->tangent_in[idx]  = 0.0f;
			soa->tangent_out[idx] = 0.0f;
		}

		soa->last_k = 0;
		soa->precompute();

		// 4) version�dependent rail heights
		if (version_string != "v0.1") {
			current_track->segments[seg].left_rail_height  = lvldat_buf->get_float();
			current_track->segments[seg].right_rail_height = lvldat_buf->get_float();
		} else {
			current_track->segments[seg].left_rail_height  = 5.0f;
			current_track->segments[seg].right_rail_height = 5.0f;
		}

		// calc segment lengths //

		int sample_per_kf = 32;
		float total_distance = 0.0f;
		RoadTransform latest_sample_pos;
		current_track->segments[seg].curve_matrix->sample(latest_sample_pos, 0.0f);
		for (int i = 0; i < num_keyframes - 1; i++)
		{
			for (int n = 0; n < sample_per_kf; n++)
			{
				float use_t = (float)(n + 1) / sample_per_kf;
				use_t = remap_float(
					use_t,
					0.0f,
					1.0f,
					soa->times[i],
					soa->times[i + 1]
					);
				RoadTransform new_sample_pos;
				current_track->segments[seg].curve_matrix->sample(new_sample_pos, use_t);
				total_distance += latest_sample_pos.t3d.origin.distance_to(new_sample_pos.t3d.origin);
				latest_sample_pos = new_sample_pos;
			}
		}
		current_track->segments[seg].segment_length = total_distance;
		const int bx = 16;
		const int by = 32;
		for (int x = 0; x < bx; x++)
		{
			for (int y = 0; y < by; y += 4)
			{
				SimVec2 use_t[4];
				SimTransform use_pos[4];
				const float tx = (float(x) / (bx - 1)) * 2.0f - 1.0f;
				for (int lane = 0; lane < 4; ++lane) {
					use_t[lane] = SimVec2(tx, float(y + lane) / (by - 1));
				}
				current_track->segments[seg].road_shape->get_oriented_transform_at_time4(use_pos, use_t);
				for (int lane = 0; lane < 4; ++lane) {
					if (use_pos[lane].origin.y < current_track->minimum_y)
					{
						current_track->minimum_y = use_pos[lane].origin.y;
					}
					if (x == 0 && y == 0 && lane == 0)
					{
						current_track->segments[seg].bounds.position = use_pos[lane].origin;
						current_track->segments[seg].bounds.size = SimVec3();
					}
					current_track->segments[seg].bounds.expand_to(use_pos[lane].origin);
					current_track->segments[seg].bounds.expand_to(use_pos[lane].origin + use_pos[lane].basis[1] * 25.f);
				}
			}
		}
		current_track->segments[seg].bounds.grow_by(5.f);
		current_track->segments[seg].checkpoint_start = -1;
		current_track->segments[seg].checkpoint_run_length = 0;
		for (int i = 0; i < current_track->num_checkpoints; i++)
		{
			if (current_track->checkpoints[i].road_segment == seg)
			{
				if (current_track->segments[seg].checkpoint_start == -1)
				{
					current_track->segments[seg].checkpoint_start = i;
				}
				current_track->segments[seg].checkpoint_run_length++;
			}
		}
	}

	current_track->minimum_y -= 250.0f;

	if (trigger_count > 0) {
		current_track->num_trigger_colliders = trigger_count;
		current_track->trigger_colliders = level_data.allocate_array<TriggerCollider*>(trigger_count);
		for (uint32_t t = 0; t < trigger_count; ++t) {
			uint32_t type_val = lvldat_buf->get_u32();
			uint32_t seg_idx  = lvldat_buf->get_u32();
			uint32_t cp_idx   = lvldat_buf->get_u32();

			SimBasis b;
			b[0][0] = lvldat_buf->get_float();
			b[0][1] = lvldat_buf->get_float();
			b[0][2] = lvldat_buf->get_float();
			b[1][0] = lvldat_buf->get_float();
			b[1][1] = lvldat_buf->get_float();
			b[1][2] = lvldat_buf->get_float();
			b[2][0] = lvldat_buf->get_float();
			b[2][1] = lvldat_buf->get_float();
			b[2][2] = lvldat_buf->get_float();
			SimVec3 origin;
			origin.x = lvldat_buf->get_float();
			origin.y = lvldat_buf->get_float();
			origin.z = lvldat_buf->get_float();
			SimTransform inv_t(b, origin);

			SimVec3 ext;
			ext.x = lvldat_buf->get_float();
			ext.y = lvldat_buf->get_float();
			ext.z = lvldat_buf->get_float();

			TriggerCollider* trig = nullptr;
			switch (type_val) {
			case TRIGGER_TYPE::DASHPLATE:
				trig = gamestate_data.allocate_class<Dashplate>();
				break;
			case TRIGGER_TYPE::JUMPPLATE:
				trig = gamestate_data.allocate_class<Jumpplate>();
				break;
			case TRIGGER_TYPE::MINE:
				trig = gamestate_data.allocate_class<Mine>();
				break;
			default:
				// TODO: assert that we never reach here!
				break;
			}
			trig->segment_index = seg_idx;
			trig->checkpoint_index = cp_idx;
			trig->inv_transform = inv_t;
			trig->transform = inv_t.affine_inverse();
			trig->half_extents = ext;
			current_track->trigger_colliders[t] = trig;
		}
	}


	int requested_cars = requested_cars_hint;
	PhysicsCarProperties* props_array = nullptr;
	cars = gamestate_data.create_and_allocate_cars(requested_cars, &props_array);
	car_properties_array = props_array;
	num_cars = requested_cars;
	// Build randomized spawn order using shared seed from server, if provided.
	// This affects only grid slots, not which car index belongs to which player.
	std::vector<int> spawn_order;
	spawn_order.resize(num_cars);
	for (int i = 0; i < num_cars; ++i) spawn_order[i] = i;
		if (spawn_seed != 0 && num_cars > 1) {
			uint32_t seed = static_cast<uint32_t>(spawn_seed);
			auto next_rand = [&seed]() {
				seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
			};
			for (int i = num_cars - 1; i > 0; --i) {
				uint32_t r = next_rand();
				int j = static_cast<int>(r % (i + 1));
				std::swap(spawn_order[i], spawn_order[j]);
			}
		}

		TrackQueryScratch spawn_scratch;
		for (int i = 0; i < num_cars; i++)
		{
			cars[i].soa->current_track[cars[i].soa_index] = current_track;
			if (i < car_prop_buffers.size()) {
				godot::PackedByteArray arr = car_prop_buffers[i];
               // StreamPeerBuffer inherits Reference; using Ref ensures
               // the object is freed when 'pb' goes out of scope.
				godot::Ref<godot::StreamPeerBuffer> pb = godot::Ref<godot::StreamPeerBuffer>(memnew(godot::StreamPeerBuffer));
				pb->set_data_array(arr);
				*(cars[i].soa->car_properties[cars[i].soa_index]) = PhysicsCarProperties::deserialize(*pb);
			}
			if (i < accel_settings.size() && accel_settings[i].get_type() == godot::Variant::FLOAT) {
				cars[i].soa->m_accel_setting[cars[i].soa_index] = accel_settings[i];
			}
			cars[i].initialize_machine();

                // Determine spawn transform at the end of the last track segment
			int seg_idx = current_track->num_segments - 1;
			const int columns = 6;
			const float column_width_start = -0.6f;
			const float column_width_end = 0.6f;
			const float row_spacing = 20.0f;
			const float start_offset = 40.0f;

			int slot = spawn_order[i];
			float distance_back = start_offset + slot * 10;
			while (seg_idx > 0 && distance_back > current_track->segments[seg_idx].segment_length) {
				distance_back -= current_track->segments[seg_idx].segment_length;
				seg_idx -= 1;
			}
			if (seg_idx < 0) {
				seg_idx = 0;
				distance_back = 0.0f;
			}

			const TrackSegment &spawn_seg = current_track->segments[seg_idx];
			float t_y = remap_float(distance_back, 0.0f, spawn_seg.segment_length, 1.0f, 0.0f);
			float t_x = remap_float(static_cast<float>(slot % columns), 0.0f, static_cast<float>(columns - 1), column_width_start, column_width_end);

			SimTransform spawn_transform;
			spawn_seg.road_shape->get_oriented_transform_at_time(spawn_transform, SimVec2(t_x, t_y));
			spawn_transform.basis.orthonormalize();
			spawn_transform.basis = spawn_transform.basis.rotated(spawn_transform.basis.get_column(1), Math_PI);
			const SimVec3 spawn_up = spawn_transform.basis.get_column(1);
			const SimVec3 track_surface_pos = spawn_transform.origin;
			SimVec3 up_offset = spawn_up * 0.5f;
			spawn_transform.origin += up_offset;

			STORE_INDEXED_VEC3(*cars[i].soa, position_current, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_old, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_old_2, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_old_dupe, cars[i].soa_index, spawn_transform.origin);
			STORE_INDEXED_VEC3(*cars[i].soa, position_bottom, cars[i].soa_index, spawn_transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));

			{ SimTransform mxt_tmp = MXT_LOAD_TRANSFORM(*cars[i].soa, basis_physical, cars[i].soa_index); mxt_tmp.basis = spawn_transform.basis; MXT_STORE_TRANSFORM(*cars[i].soa, basis_physical, cars[i].soa_index, mxt_tmp); }
			{ SimTransform mxt_tmp = MXT_LOAD_TRANSFORM(*cars[i].soa, basis_physical_other, cars[i].soa_index); mxt_tmp.basis = spawn_transform.basis; MXT_STORE_TRANSFORM(*cars[i].soa, basis_physical_other, cars[i].soa_index, mxt_tmp); }
			cars[i].update_pitch_transform_from_machine_front_back();

			MXT_STORE_TRANSFORM(*cars[i].soa, transform_visual, cars[i].soa_index, spawn_transform);
			STORE_INDEXED_VEC3(*cars[i].soa, track_surface_normal, cars[i].soa_index, spawn_up);
			STORE_INDEXED_VEC3(*cars[i].soa, track_surface_pos, cars[i].soa_index, track_surface_pos);
			cars[i].soa->height_above_track[cars[i].soa_index] = 19.5f;

			PhysicsCarSoA *car_soa = cars[i].soa;
			const int car_idx = cars[i].soa_index;
			int spawn_checkpoint = current_track->get_best_checkpoint(spawn_transform.origin, spawn_scratch);
			if (spawn_checkpoint < 0) {
				spawn_checkpoint = current_track->get_best_checkpoint(track_surface_pos, spawn_scratch);
			}
			if (spawn_checkpoint >= 0 && spawn_checkpoint < current_track->num_checkpoints) {
				const CollisionCheckpoint &cur_cp = current_track->checkpoints[spawn_checkpoint];
				const SimVec3 p1 = cur_cp.start_plane.project(spawn_transform.origin);
				const SimVec3 p2 = cur_cp.end_plane.project(spawn_transform.origin);
				const float checkpoint_fraction = get_closest_t_on_segment(spawn_transform.origin, p1, p2);
				const float cp_length = cur_cp.local_distance;
				const float cp_start_distance = cur_cp.distance - cur_cp.local_distance;
				float ground_distance = cp_start_distance + cp_length * std::clamp(checkpoint_fraction, 0.0f, 1.0f);
				float lap_length = current_track->lap_length;
				if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
					lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
				}
				if (lap_length > 0.0f) {
					ground_distance = std::fmod(ground_distance, lap_length);
					if (ground_distance < 0.0f) {
						ground_distance += lap_length;
					}
				}
				car_soa->current_checkpoint[car_idx] = static_cast<uint16_t>(spawn_checkpoint);
				car_soa->current_collision_checkpoint[car_idx] = static_cast<uint16_t>(spawn_checkpoint);
				car_soa->last_ground_checkpoint[car_idx] = static_cast<uint16_t>(spawn_checkpoint);
				car_soa->checkpoint_fraction[car_idx] = checkpoint_fraction;
				car_soa->lap_progress[car_idx] = (static_cast<float>(spawn_checkpoint) + checkpoint_fraction) / static_cast<float>(current_track->num_checkpoints);
				car_soa->checkpoint_track_distance[car_idx] = ground_distance;
				car_soa->last_ground_distance[car_idx] = ground_distance;
			}
			const int point_base = car_idx * 4;
			const SimVec3 reset_position = spawn_transform.origin;
			const SimBasis& reset_basis = spawn_transform.basis;
			const SimVec3x4 tilt_pos = transform_points_components4(
				reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
				reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
				reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
				reset_position.x, reset_position.y, reset_position.z,
				sim_load4(car_soa->tilt_offset_x + point_base),
				sim_load4(car_soa->tilt_offset_y + point_base),
				sim_load4(car_soa->tilt_offset_z + point_base));
			const SimVec3x4 wall_pos = transform_points_components4(
				reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
				reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
				reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
				reset_position.x, reset_position.y, reset_position.z,
				sim_load4(car_soa->wall_offset_x + point_base),
				sim_load4(car_soa->wall_offset_y + point_base),
				sim_load4(car_soa->wall_offset_z + point_base));
			const SimVec3 wall_sweep_origin = spawn_transform.xform(SimVec3(0.0f, 0.1f, 0.0f));
			for (int point = 0; point < 4; ++point) {
				const int p = point_base + point;
				car_soa->tilt_state[p] = 0;
				car_soa->tilt_force[p] = 0.0f;
				car_soa->tilt_force_spatial_len[p] = 0.0f;
				STORE_INDEXED_VEC3(*car_soa, tilt_force_spatial, p, SimVec3());
				STORE_INDEXED_VEC3(*car_soa, tilt_up_vector_2, p, spawn_up);
				STORE_INDEXED_VEC3(*car_soa, tilt_up_vector, p, spawn_up);
				STORE_INDEXED_VEC3(*car_soa, wall_pos_a, p, wall_sweep_origin);
				STORE_INDEXED_VEC3(*car_soa, wall_collision, p, SimVec3());
			}
			sim_store4(car_soa->tilt_pos_old_x + point_base, tilt_pos.x);
			sim_store4(car_soa->tilt_pos_old_y + point_base, tilt_pos.y);
			sim_store4(car_soa->tilt_pos_old_z + point_base, tilt_pos.z);
			sim_store4(car_soa->tilt_pos_x + point_base, tilt_pos.x);
			sim_store4(car_soa->tilt_pos_y + point_base, tilt_pos.y);
			sim_store4(car_soa->tilt_pos_z + point_base, tilt_pos.z);
			sim_store4(car_soa->wall_pos_b_x + point_base, wall_pos.x);
			sim_store4(car_soa->wall_pos_b_y + point_base, wall_pos.y);
			sim_store4(car_soa->wall_pos_b_z + point_base, wall_pos.z);
			car_soa->machine_state[car_idx] &= ~(MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q | MACHINESTATE::JUSTLANDED);
			car_soa->air_time[car_idx] = 0;
		}

		input_buffer = static_cast<PlayerInput*>(malloc(sizeof(PlayerInput) * INPUT_BUFFER_LEN * num_cars));
		for (int i = 0; i < INPUT_BUFFER_LEN * num_cars; i++) {
			input_buffer[i] = PlayerInput::from_neutral();
		}
		ensure_vehicle_tick_soa_capacity(num_cars);
		if (car_player_ids) {
			::free(car_player_ids);
		}
		if (car_is_cpu) {
			::free(car_is_cpu);
		}
		car_player_ids = static_cast<int32_t*>(malloc(sizeof(int32_t) * num_cars));
		car_is_cpu = static_cast<uint8_t*>(malloc(sizeof(uint8_t) * num_cars));
		for (int i = 0; i < num_cars; ++i) {
			car_player_ids[i] = -1;
			car_is_cpu[i] = 0;
		}

		sim_started = true;
#if 0
		UtilityFunctions::print("finished constructing level!");
		UtilityFunctions::print("level data size:");
		UtilityFunctions::print(level_data.get_size());
		UtilityFunctions::print("gamestate size:");
		UtilityFunctions::print(gamestate_data.get_size());
		UtilityFunctions::print("trigger objects:");
		UtilityFunctions::print(trigger_count);
#endif

		if (!car_node_container) {
			UtilityFunctions::print("car_node_container is null");
			return;
		}
		if (car_node_container == nullptr) {
			UtilityFunctions::print("container is null");
			return;
		}
	};

	void GameSim::destroy_gamesim()
	{
		if (sim_started)
		{
			if (current_track) {
				current_track->num_trigger_colliders = 0;
				current_track->trigger_colliders = nullptr;
			}
			if (cars) {
				::free(cars);
				cars = nullptr;
			}
			level_data.free_heap();
			gamestate_data.free_heap();
			car_properties_array = nullptr;
			super_spark_state = nullptr;
			super_sparks = nullptr;
			spark_multimesh_instance = nullptr;
			for (int i = 0; i < STATE_BUFFER_LEN; i++)
			{
				if (state_buffer[i].data)
				{
					::free(state_buffer[i].data);
					state_buffer[i].data = nullptr;
				}
			}
			if (input_buffer) {
				::free(input_buffer);
				input_buffer = nullptr;
			}
			free_vehicle_tick_soa();
			sim_started = false;
			tick = 0;
			current_track = nullptr;
		}
		if (cars) {
			::free(cars);
			cars = nullptr;
		}
	if (car_player_ids) {
		::free(car_player_ids);
		car_player_ids = nullptr;
	}
		if (car_is_cpu) {
			::free(car_is_cpu);
			car_is_cpu = nullptr;
		}
		car_render_manager = nullptr;
		gameplay_camera_node = nullptr;
		gameplay_camera.unref();
		gameplay_camera_player_id = -1;
		render_car_transform_nodes.clear();
		render_car_multimeshes.clear();
		render_shadow_multimeshes.clear();
		render_car_local_transforms.clear();
		render_shadow_local_transforms.clear();
		render_car_archetype_indices.clear();
		render_car_slots.clear();
		render_visual_prev_transforms.clear();
		render_visual_current_transforms.clear();
		render_final_prev_transforms.clear();
		render_final_current_transforms.clear();
		render_visual_prev_ground_distances.clear();
		render_visual_current_ground_distances.clear();
		render_visual_initialized.clear();
		render_rollback_corrections.clear();
		render_rollback_correction_active.clear();
		render_rollback_capture_transforms.clear();
		render_rollback_capture_pending = false;
		render_vehicle_visual_state.clear();
		native_cpu_drivers.clear();
		cpu_driver_manager = nullptr;
	};

void GameSim::set_car_render_manager(godot::Object* p_car_render_manager)
{
	car_render_manager = p_car_render_manager;
	render_car_multimeshes.clear();
	render_shadow_multimeshes.clear();
	render_car_local_transforms.clear();
	render_shadow_local_transforms.clear();
	render_car_archetype_indices.clear();
	render_car_slots.clear();
	render_visual_prev_transforms.clear();
	render_visual_current_transforms.clear();
	render_final_prev_transforms.clear();
	render_final_current_transforms.clear();
	render_visual_prev_ground_distances.clear();
	render_visual_current_ground_distances.clear();
	render_visual_initialized.clear();
	render_rollback_corrections.clear();
	render_rollback_correction_active.clear();
	render_rollback_capture_transforms.clear();
	render_rollback_capture_pending = false;
	render_vehicle_visual_state.clear();
	if (!car_render_manager) {
		return;
	}

	godot::Variant bindings_var = car_render_manager->call("get_native_render_bindings");
	if (bindings_var.get_type() != godot::Variant::DICTIONARY) {
		return;
	}
	godot::Dictionary bindings = bindings_var;
	godot::Array multimeshes = bindings.get("multimeshes", godot::Array());
	godot::Array shadow_multimeshes = bindings.get("shadow_multimeshes", godot::Array());
	godot::Array local_transforms = bindings.get("local_transforms", godot::Array());
	godot::Array shadow_local_transforms = bindings.get("shadow_local_transforms", godot::Array());
	godot::PackedInt32Array archetype_indices = bindings.get("archetype_indices", godot::PackedInt32Array());
	godot::PackedInt32Array slots = bindings.get("slots", godot::PackedInt32Array());

	render_car_multimeshes.reserve(multimeshes.size());
	render_shadow_multimeshes.reserve(shadow_multimeshes.size());
	render_car_local_transforms.reserve(local_transforms.size());
	render_shadow_local_transforms.reserve(shadow_local_transforms.size());
	for (int i = 0; i < multimeshes.size(); ++i) {
		godot::Ref<godot::MultiMesh> multimesh = multimeshes[i];
		render_car_multimeshes.push_back(multimesh);
		godot::Ref<godot::MultiMesh> shadow_multimesh;
		if (i < shadow_multimeshes.size()) {
			shadow_multimesh = shadow_multimeshes[i];
		}
		render_shadow_multimeshes.push_back(shadow_multimesh);
		if (i < local_transforms.size() && local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_car_local_transforms.push_back(sim_transform(local_transforms[i]));
		} else {
			render_car_local_transforms.push_back(SimTransform());
		}
		if (i < shadow_local_transforms.size() && shadow_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_shadow_local_transforms.push_back(sim_transform(shadow_local_transforms[i]));
		} else {
			render_shadow_local_transforms.push_back(SimTransform());
		}
	}

	render_car_archetype_indices.resize(archetype_indices.size());
	for (int i = 0; i < archetype_indices.size(); ++i) {
		render_car_archetype_indices[i] = archetype_indices[i];
	}
	render_car_slots.resize(slots.size());
	for (int i = 0; i < slots.size(); ++i) {
		render_car_slots[i] = slots[i];
	}
}

void GameSim::set_gameplay_camera(godot::Camera3D* p_camera, int player_id)
{
	gameplay_camera_node = p_camera;
	gameplay_camera_player_id = player_id;
	if (gameplay_camera.is_null()) {
		gameplay_camera.instantiate();
	}
	if (gameplay_camera.is_valid()) {
		gameplay_camera->reset();
	}
	if (gameplay_camera_node) {
		gameplay_camera_node->make_current();
		gameplay_camera_node->set_near(0.25f);
		gameplay_camera_node->set_far(40000.0f);
	}
}

void GameSim::set_cpu_driver_manager(godot::Object* manager)
{
	cpu_driver_manager = manager;
}

GameSim::NativeCpuDriverState* GameSim::find_native_cpu_driver(int32_t player_id)
{
	for (NativeCpuDriverState& driver : native_cpu_drivers) {
		if (driver.active && driver.player_id == player_id) {
			return &driver;
		}
	}
	return nullptr;
}

void GameSim::configure_native_cpu_drivers()
{
	native_cpu_drivers.clear();
	native_cpu_drivers.resize(std::max(0, num_cars));
	const godot::PackedByteArray neutral = PlayerInput::to_bytes(PlayerInput::from_neutral());
	for (int i = 0; i < num_cars; ++i) {
		NativeCpuDriverState& driver = native_cpu_drivers[i];
		driver.player_id = car_player_ids ? car_player_ids[i] : -1;
		driver.active = (car_is_cpu && car_is_cpu[i] && driver.player_id != -1) ? 1 : 0;
		driver.last_generated_tick = -1;
		driver.pending_input = neutral;
	}
}

static inline uint32_t native_cpu_hash_u32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static inline float native_cpu_rand01_from_seed(uint32_t seed)
{
	return static_cast<float>(native_cpu_hash_u32(seed) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

static inline float native_cpu_smooth_noise_signed(uint32_t seed_base, int expected_tick, int period_ticks)
{
	const int t0 = expected_tick / period_ticks;
	const int t1 = t0 + 1;
	const float frac = static_cast<float>(expected_tick - t0 * period_ticks) / static_cast<float>(period_ticks);
	const float smooth = frac * frac * (3.0f - 2.0f * frac);
	const float a = native_cpu_rand01_from_seed(seed_base ^ (static_cast<uint32_t>(t0) * 0x27D4EB2Du)) * 2.0f - 1.0f;
	const float b = native_cpu_rand01_from_seed(seed_base ^ (static_cast<uint32_t>(t1) * 0x27D4EB2Du)) * 2.0f - 1.0f;
	return a + (b - a) * smooth;
}

static inline PlayerInput native_cpu_generate_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed)
{
	PhysicsCarSoA& soa = *car.soa;
	const int i = car.soa_index;
	const SimBasis physical_basis = MXT_LOAD_TRANSFORM(soa, basis_physical, i).basis;
	SimBasis surface = soa.road_sample[i].closest_surface.basis;
	float road_tx = soa.road_sample[i].road_t.x;
	if (soa.current_track[i]) {
		int sample_cp = soa.current_collision_checkpoint[i];
		if (sample_cp < 0 || sample_cp >= soa.current_track[i]->num_checkpoints) {
			sample_cp = soa.current_checkpoint[i];
		}
		if (sample_cp >= 0 && sample_cp < soa.current_track[i]->num_checkpoints) {
			SimVec2 road_t;
			SimVec3 spatial_t;
			SimTransform road_surface;
			soa.current_track[i]->get_road_surface(sample_cp, LOAD_INDEXED_VEC3(soa, position_current, i), road_t, spatial_t, road_surface);
			surface = road_surface.basis;
			road_tx = road_t.x;
		}
	}
	const float energy = soa.energy[i];
	const uint32_t tilt_state = soa.tilt_state[i * 4 + 1];
	const uint32_t seed_base =
		static_cast<uint32_t>(player_id) * 0x9E3779B9u ^
		static_cast<uint32_t>(expected_tick) * 0x85EBCA6Bu ^
		static_cast<uint32_t>(spawn_seed) * 0xC2B2AE35u;
	const uint32_t lane_seed =
		static_cast<uint32_t>(player_id) * 0x9E3779B9u ^
		static_cast<uint32_t>(spawn_seed) * 0xC2B2AE35u ^
		0xA341316Cu;

	PlayerInput input = PlayerInput::from_neutral();
	input.accelerate = 1.0f;

	float desired_steer = (physical_basis.c0 + surface.c0).dot(surface.c2);
	const float desired_lane = native_cpu_smooth_noise_signed(lane_seed, expected_tick, 480) * 0.8f;

	const float lane_offset = road_tx + desired_lane;
	input.strafe_left = std::max(0.0f, std::min(1.0f, std::abs(std::min(lane_offset, 0.0f)) * 4.0f));
	input.strafe_right = std::max(0.0f, std::min(1.0f, std::max(lane_offset, 0.0f) * 4.0f));

	const bool drifting = (tilt_state & 0x4u) != 0;
	const bool wants_drift = std::abs(desired_steer) >= 0.4f && !drifting;
	if (drifting) {
		desired_steer *= 5.0f;
	}
	input.steer_horizontal = std::max(-1.0f, std::min(1.0f, desired_steer * 30.0f));
	if (wants_drift) {
		input.strafe_left = 1.0f;
		input.strafe_right = 1.0f;
	}

	const uint32_t boost_phase = native_cpu_hash_u32(seed_base ^ 0xB5297A4Du) % 720u;
	const bool wants_boost = energy > 10.0f && boost_phase == 0u;
	input.boost = wants_boost;

	return input;
}

void GameSim::update_native_cpu_driver(int car_index)
{
	if (car_index < 0 || car_index >= num_cars || car_index >= static_cast<int>(native_cpu_drivers.size())) {
		return;
	}
	NativeCpuDriverState& driver = native_cpu_drivers[car_index];
	if (!driver.active) {
		return;
	}

	PlayerInput input = native_cpu_generate_input_for_car(cars[car_index], driver.player_id, tick, spawn_seed);
	driver.pending_input = PlayerInput::to_bytes(input);
	driver.last_generated_tick = tick;
}

void GameSim::update_native_cpu_drivers()
{
	if (!cars || native_cpu_drivers.empty()) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		update_native_cpu_driver(i);
	}
}

godot::PackedByteArray GameSim::get_native_cpu_input_for_tick(int player_id, int expected_tick)
{
	return generate_native_cpu_input_for_tick(player_id, expected_tick);
}

godot::PackedByteArray GameSim::generate_native_cpu_input_for_tick(int player_id, int expected_tick)
{
	NativeCpuDriverState* driver = find_native_cpu_driver(static_cast<int32_t>(player_id));
	if (!driver) {
		return PlayerInput::to_bytes(PlayerInput::from_neutral());
	}
	for (int car_index = 0; car_index < num_cars; ++car_index) {
		if (car_player_ids && car_player_ids[car_index] == player_id) {
			PlayerInput input = native_cpu_generate_input_for_car(cars[car_index], static_cast<int32_t>(player_id), expected_tick, spawn_seed);
			driver->pending_input = PlayerInput::to_bytes(input);
			driver->last_generated_tick = expected_tick;
			return driver->pending_input;
		}
	}
	return PlayerInput::to_bytes(PlayerInput::from_neutral());
}

void GameSim::update_render_visual_snapshots(int visual_count)
{
	if (visual_count <= 0 || !cars) {
		return;
	}
	if (static_cast<int>(render_visual_prev_transforms.size()) != visual_count) {
		render_visual_prev_transforms.resize(visual_count);
		render_visual_current_transforms.resize(visual_count);
		render_final_prev_transforms.resize(visual_count);
		render_final_current_transforms.resize(visual_count);
		render_visual_prev_ground_distances.resize(visual_count);
		render_visual_current_ground_distances.resize(visual_count);
		render_visual_initialized.assign(visual_count, 0);
		render_rollback_corrections.assign(visual_count, SimTransform());
		render_rollback_correction_active.assign(visual_count, 0);
		render_vehicle_visual_state.assign(visual_count, RenderVehicleVisualState());
		render_rollback_capture_transforms.clear();
		render_rollback_capture_pending = false;
	}
	for (int i = 0; i < visual_count; ++i) {
		update_machine_visual_transform_for_render(*cars[i].soa, cars[i].soa_index, render_vehicle_visual_state[i]);
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const SimTransform current = MXT_LOAD_TRANSFORM(soa, transform_visual, lane);
		SimVec3 track_normal = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		if (track_normal.length_squared() <= 0.0001f) {
			track_normal = current.basis.get_column(1);
		}
		track_normal = track_normal.normalized();
		const SimVec3 track_surface_pos = LOAD_INDEXED_VEC3(soa, track_surface_pos, lane);
		float current_ground_distance = (LOAD_INDEXED_VEC3(soa, position_current, lane) - track_surface_pos).dot(track_normal);
		if (current_ground_distance < 0.0f) {
			current_ground_distance = 0.0f;
		}
		const bool was_initialized = render_visual_initialized[i] != 0;
		if (was_initialized) {
			render_visual_prev_transforms[i] = render_visual_current_transforms[i];
			if (i < static_cast<int>(render_final_prev_transforms.size()) &&
					i < static_cast<int>(render_final_current_transforms.size())) {
				render_final_prev_transforms[i] = render_final_current_transforms[i];
			}
			render_visual_prev_ground_distances[i] = render_visual_current_ground_distances[i];
		} else {
			render_visual_prev_transforms[i] = current;
			if (i < static_cast<int>(render_final_prev_transforms.size())) {
				render_final_prev_transforms[i] = current;
			}
			render_visual_prev_ground_distances[i] = current_ground_distance;
			render_visual_initialized[i] = 1;
		}
		render_visual_current_transforms[i] = current;
		render_visual_current_ground_distances[i] = current_ground_distance;
		if (i < static_cast<int>(render_rollback_correction_active.size()) && render_rollback_correction_active[i]) {
			render_rollback_corrections[i] = interpolate_sim_transform(render_rollback_corrections[i], SimTransform(), 0.3f);
			if (render_correction_is_small(render_rollback_corrections[i])) {
				render_rollback_corrections[i] = SimTransform();
				render_rollback_correction_active[i] = 0;
			}
		}
		SimTransform final_transform = current;
		if (i < static_cast<int>(render_rollback_correction_active.size()) &&
				render_rollback_correction_active[i] &&
				i < static_cast<int>(render_rollback_corrections.size())) {
			final_transform = apply_render_correction(current, render_rollback_corrections[i]);
		}
		if (i < static_cast<int>(render_final_current_transforms.size())) {
			render_final_current_transforms[i] = final_transform;
			if (!was_initialized && i < static_cast<int>(render_final_prev_transforms.size())) {
				render_final_prev_transforms[i] = final_transform;
			}
		}
	}
}

void GameSim::apply_render_multimeshes(float alpha)
{
	const int visual_count = std::min(num_cars, static_cast<int>(render_final_current_transforms.size()));
	for (int i = 0; i < visual_count; ++i) {
		if (i >= static_cast<int>(render_car_archetype_indices.size()) || i >= static_cast<int>(render_car_slots.size())) {
			continue;
		}
		const int archetype = render_car_archetype_indices[i];
		const int slot = render_car_slots[i];
		if (archetype < 0 || archetype >= static_cast<int>(render_car_multimeshes.size()) ||
				archetype >= static_cast<int>(render_car_local_transforms.size()) ||
				render_car_multimeshes[archetype].is_null() || slot < 0) {
			continue;
		}
		SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		if (render_car_multimeshes[archetype].is_valid()) {
			const SimTransform instance_transform = visual_transform * render_car_local_transforms[archetype];
			render_car_multimeshes[archetype]->set_instance_transform(slot, gd_transform(instance_transform));
		}
		if (archetype < static_cast<int>(render_shadow_multimeshes.size()) &&
				archetype < static_cast<int>(render_shadow_local_transforms.size()) &&
				render_shadow_multimeshes[archetype].is_valid()) {
			const float prev_ground_distance = i < static_cast<int>(render_visual_prev_ground_distances.size()) ? render_visual_prev_ground_distances[i] : 20.0f;
			const float current_ground_distance = i < static_cast<int>(render_visual_current_ground_distances.size()) ? render_visual_current_ground_distances[i] : prev_ground_distance;
			const float ground_distance = prev_ground_distance + (current_ground_distance - prev_ground_distance) * alpha;
			PhysicsCarSoA& soa = *cars[i].soa;
			const int lane = cars[i].soa_index;
			SimVec3 shadow_normal = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (shadow_normal.length_squared() <= 0.0001f) {
				shadow_normal = visual_transform.basis.get_column(1);
			}
			shadow_normal = shadow_normal.normalized();
			SimTransform shadow_transform = visual_transform * render_shadow_local_transforms[archetype];
			if (ground_distance >= 20.0f) {
				shadow_transform.basis.c0 = SimVec3();
				shadow_transform.basis.c1 = SimVec3();
				shadow_transform.basis.c2 = SimVec3();
			} else {
				shadow_transform.origin += -shadow_normal * ground_distance;
				shadow_transform.basis.c0 = shadow_transform.basis.c0.slide(shadow_normal);
				shadow_transform.basis.c1 = shadow_transform.basis.c1.slide(shadow_normal);
				shadow_transform.basis.c2 = shadow_transform.basis.c2.slide(shadow_normal);
			}
			render_shadow_multimeshes[archetype]->set_instance_transform(slot, gd_transform(shadow_transform));
		}
	}
}

void GameSim::update_native_gameplay_camera(bool step_camera)
{
	if (!gameplay_camera_node || gameplay_camera.is_null() || !cars || !car_player_ids) {
		return;
	}
	int car_index = -1;
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] == gameplay_camera_player_id) {
			car_index = i;
			break;
		}
	}
	if (car_index < 0) {
		return;
	}
	PhysicsCarSoA& soa = *cars[car_index].soa;
	const int lane = cars[car_index].soa_index;
	SimVec3 camera_position_correction;
	const bool has_camera_render_correction =
		car_index < static_cast<int>(render_rollback_correction_active.size()) &&
		render_rollback_correction_active[car_index] &&
		car_index < static_cast<int>(render_rollback_corrections.size());
	if (has_camera_render_correction) {
		camera_position_correction = render_rollback_corrections[car_index].origin;
	}
	if (step_camera) {
		float aspect_ratio = 4.0f / 3.0f;
		if (godot::Viewport* viewport = gameplay_camera_node->get_viewport()) {
			const godot::Vector2 size = viewport->get_visible_rect().size;
			if (size.y > 0.0f) {
				aspect_ratio = static_cast<float>(size.x / size.y);
			}
		}
		SimVec3 track_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		if (track_up.length_squared() <= 0.0001f) {
			track_up = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(1);
		}
		track_up = track_up.normalized();
		godot::Input* input = godot::Input::get_singleton();
		const bool view_up_pressed = input && (input->is_action_just_pressed(godot::StringName("CameraUp")) || input->is_action_just_pressed(godot::StringName("DPadUp")));
		const bool view_down_pressed = input && (input->is_action_just_pressed(godot::StringName("CameraDown")) || input->is_action_just_pressed(godot::StringName("DPadDown")));
			gameplay_camera->step(
			gd_vec3(LOAD_INDEXED_VEC3(soa, position_current, lane) + camera_position_correction),
			gd_vec3(LOAD_INDEXED_VEC3(soa, position_old, lane) + camera_position_correction),
			gd_transform(MXT_LOAD_TRANSFORM(soa, basis_physical, lane)),
			gd_vec3(track_up),
			gd_vec3(LOAD_INDEXED_VEC3(soa, track_surface_pos, lane)),
			soa.height_above_track[lane],
			soa.speed_kmh[lane],
			soa.camera_reorienting[lane],
			soa.camera_repositioning[lane],
				car_index < static_cast<int>(render_vehicle_visual_state.size()) ? render_vehicle_visual_state[car_index].turn_reaction_effect : 0.0f,
			static_cast<int>(soa.machine_state[lane]),
			static_cast<int>(soa.state_2[lane]),
			static_cast<int>(soa.tilt_state[lane * 4 + 0]),
			static_cast<int>(soa.tilt_state[lane * 4 + 1]),
			static_cast<int>(soa.tilt_state[lane * 4 + 2]),
			static_cast<int>(soa.tilt_state[lane * 4 + 3]),
			static_cast<int>(soa.restore_state[lane]),
			static_cast<int>(soa.restore_move_frames[lane]),
			aspect_ratio,
			view_up_pressed,
			view_down_pressed);
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	gameplay_camera_node->set_global_transform(gameplay_camera->get_render_transform(alpha));
	gameplay_camera_node->set_fov(gameplay_camera->get_render_fov(alpha));
	gameplay_camera_node->set_near(0.25);
	gameplay_camera_node->set_far(40000.0);
}

void GameSim::render_gamesim_visuals_only()
{
	if (!sim_started || !cars) {
		return;
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	apply_render_multimeshes(alpha);
	update_native_gameplay_camera(false);
}

void GameSim::set_player_metadata(godot::Array player_ids, godot::Array cpu_flags)
{
	if (!car_player_ids || !car_is_cpu) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		car_player_ids[i] = -1;
		car_is_cpu[i] = 0;
	}
	const int limit = std::min(num_cars, static_cast<int>(player_ids.size()));
	for (int i = 0; i < limit; ++i) {
		godot::Variant id_var = player_ids[i];
		int32_t pid = -1;
		if (id_var.get_type() == godot::Variant::INT) {
			pid = static_cast<int32_t>(id_var.operator int64_t());
		} else if (id_var.get_type() == godot::Variant::FLOAT) {
			pid = static_cast<int32_t>(id_var.operator double());
		}
		car_player_ids[i] = pid;
		bool is_cpu = false;
		if (i < cpu_flags.size()) {
			godot::Variant flag_var = cpu_flags[i];
			if (flag_var.get_type() == godot::Variant::BOOL) {
				is_cpu = static_cast<bool>(flag_var);
			} else if (flag_var.get_type() == godot::Variant::INT) {
				is_cpu = flag_var.operator int64_t() != 0;
			}
		}
		car_is_cpu[i] = is_cpu ? 1 : 0;
	}
	configure_native_cpu_drivers();
}

godot::PackedByteArray GameSim::build_cpu_observation(const PhysicsCar& car) const
{
	godot::Ref<godot::StreamPeerBuffer> buffer;
	buffer.instantiate();
	buffer->seek(0);
	auto write_vec3 = [&](const SimVec3& v) {
		buffer->put_float(v.x);
		buffer->put_float(v.y);
		buffer->put_float(v.z);
	};
	write_vec3(car.soa->road_sample[car.soa_index].spatial_t);
	buffer->put_float(car.soa->road_sample[car.soa_index].road_t.x);
	buffer->put_float(car.soa->road_sample[car.soa_index].road_t.y);
	write_vec3(LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index));
	write_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity, car.soa_index));
	write_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity_angular, car.soa_index));
	const SimBasis basis = MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index).basis;
	for (int col = 0; col < 3; ++col) {
		write_vec3(basis.get_column(col));
	}
	write_vec3(MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index).origin);
	for (int col = 0; col < 3; ++col) {
		write_vec3(car.soa->road_sample[car.soa_index].closest_surface.basis.get_column(col));
	}
	buffer->put_float(car.soa->base_speed[car.soa_index]);
	buffer->put_float(car.soa->energy[car.soa_index]);
	buffer->put_float(car.soa->checkpoint_fraction[car.soa_index]);
	buffer->put_u16(car.soa->current_checkpoint[car.soa_index]);
	buffer->put_u32(car.soa->terrain_state[car.soa_index]);
	buffer->put_u32(car.soa->machine_state[car.soa_index]);
	buffer->put_u8(car.soa->restore_state[car.soa_index]);
	buffer->put_u32(car.soa->tilt_state[car.soa_index * 4 + 1]);
	return buffer->get_data_array();
}

void GameSim::reset_super_sparks()
{
	if (!super_spark_state || !super_sparks) {
		return;
	}
	super_spark_state->cursor = 0;
	super_spark_state->placement_timer = 0;
	super_spark_state->rng_state = 1;
	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		super_sparks[i].active = 0;
		super_sparks[i].collectable = 0;
		super_sparks[i].animation_frame = 0;
		super_sparks[i].checkpoint = 0;
		super_sparks[i].position = SimVec3();
		super_sparks[i].prev_position = SimVec3();
		super_sparks[i].start_position = SimVec3();
		super_sparks[i].final_position = SimVec3();
		super_sparks[i].plane_normal = SimVec3(0.0f, 1.0f, 0.0f);
	}
}

uint16_t GameSim::compute_s_boost_duration_frames(float gap_distance) const
{
	float seconds = 3.0f;
	if (gap_distance <= 1000.0f) {
		seconds = 3.0f;
	} else if (gap_distance >= 10000.0f) {
		seconds = 8.0f;
	} else {
		float t = (gap_distance - 1000.0f) / 9000.0f;
		seconds = 3.0f + t * 5.0f;
	}
	uint16_t frames = static_cast<uint16_t>(seconds * 60.0f + 0.5f);
	if (frames < 180u)
		frames = 180u;
	return frames;
}

float GameSim::compute_car_distance_along_track(const PhysicsCar& car) const
{
	return compute_vehicle_distance_along_track(car.soa->current_checkpoint[car.soa_index], car.soa->checkpoint_fraction[car.soa_index], car.soa->lap[car.soa_index]);
}

float GameSim::compute_vehicle_distance_along_track(uint16_t current_checkpoint, float checkpoint_fraction, uint8_t lap) const
{
	if (!current_track)
		return 0.0f;

	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}

	float lap_progress = 0.0f;
	int cp_idx = current_checkpoint;
	if (cp_idx >= 0 && cp_idx < current_track->num_checkpoints) {
		const CollisionCheckpoint& cp = current_track->checkpoints[cp_idx];
		float entry_distance = cp.distance - cp.local_distance;
		if (entry_distance < 0.0f) {
			entry_distance = 0.0f;
		}
		float fraction = std::clamp(checkpoint_fraction, 0.0f, 1.0f);
		lap_progress = entry_distance + cp.local_distance * fraction;
	}

	float lap_total = lap_progress + lap_length * std::max(static_cast<float>(lap), 0.0f);
	return lap_total;
}

void GameSim::emit_super_sparks_from_car(const PhysicsCar& car, int count)
{
	if (count <= 0)
		return;
	if (!sim_started || !super_spark_state || !super_sparks || !current_track)
		return;
	if ((car.soa->machine_state[car.soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) != 0)
		return;
	if ((car.soa->machine_state[car.soa_index] & (MACHINESTATE::AIRBORNE | MACHINESTATE::ZEROHP)) != 0)
		return;
	if (car.soa->restore_state[car.soa_index] == 2)
		return;

	constexpr uint64_t kPostCountdownBlockFrames = 180;
	const uint64_t current_frame = static_cast<uint64_t>(car.soa->simulation_tick[car.soa_index]);
	const uint64_t safe_frame = static_cast<uint64_t>(car.soa->level_start_time[car.soa_index]) + kPostCountdownBlockFrames;
	if (current_frame < safe_frame)
		return;

	const uint16_t checkpoint = car.soa->current_checkpoint[car.soa_index];
	if (checkpoint >= static_cast<uint16_t>(current_track->num_checkpoints))
		return;
	const SimVec3 car_position = LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index);
	SimVec3 normal_in = LOAD_INDEXED_VEC3(*car.soa, track_surface_normal, car.soa_index);
	if (normal_in.length_squared() <= 0.0001f) {
		normal_in = SimVec3(0.0f, 1.0f, 0.0f);
	} else {
		normal_in = normal_in.normalized();
	}
	SimVec3 tangent_a = car.soa->road_sample[car.soa_index].closest_surface.basis.get_column(0);
	if (tangent_a.length_squared() < 0.0001f) {
		tangent_a = normal_in.cross(SimVec3(0.0f, 0.0f, 1.0f));
	}
	if (tangent_a.length_squared() < 0.0001f) {
		tangent_a = normal_in.cross(SimVec3(1.0f, 0.0f, 0.0f));
	}
	tangent_a = tangent_a.slide(normal_in).normalized();
	SimVec3 tangent_b = normal_in.cross(tangent_a).normalized();

	auto next_rand = [&]() -> float {
		super_spark_state->rng_state = super_spark_state->rng_state * 1664525u + 1013904223u;
		return static_cast<float>(super_spark_state->rng_state & 0x00FFFFFFu) / 16777215.0f;
	};
	auto rand_range = [&](float min_v, float max_v) -> float {
		return min_v + (max_v - min_v) * next_rand();
	};

	for (int n = 0; n < count; ++n) {
		const uint16_t cursor = super_spark_state->cursor;
		SuperSpark& spark = super_sparks[cursor];
		super_spark_state->cursor = static_cast<uint16_t>((cursor + 1) % SUPER_SPARK_CAPACITY);

		const float lateral_a = rand_range(-18.0f, 18.0f);
		const float lateral_b = rand_range(-10.0f, 10.0f);
		const SimVec3 sample_point = car_position + tangent_a * lateral_a + tangent_b * lateral_b;
		SimVec2 road_t;
		SimVec3 spatial_t;
		SimTransform surface;
		current_track->get_road_surface(checkpoint, sample_point, road_t, spatial_t, surface, true);
		SimVec3 surface_normal = surface.basis.get_column(1);
		if (surface_normal.length_squared() <= 0.0001f) {
			surface_normal = normal_in;
		} else {
			surface_normal = surface_normal.normalized();
		}
		const SimVec3 final_position = surface.origin + surface_normal * 1.0f;

		spark.active = 1;
		spark.collectable = 0;
		spark.animation_frame = 0;
		spark.checkpoint = checkpoint;
		spark.plane_normal = surface_normal;
		spark.start_position = car_position;
		spark.final_position = final_position;
		spark.position = car_position;
		spark.prev_position = car_position;
	}
}

void GameSim::update_super_sparks()
{
	if (!sim_started || !cars || !super_spark_state || !super_sparks)
		return;

	constexpr uint16_t kSparkAnimationFrames = 30;
	constexpr float kSparkArcHeight = 8.4f;
	const float collect_radius_sq = SUPER_SPARK_COLLECT_RADIUS * SUPER_SPARK_COLLECT_RADIUS;
	auto checkpoint_matches = [&](uint16_t spark_checkpoint, uint16_t car_checkpoint) -> bool {
		if (!current_track || car_checkpoint >= static_cast<uint16_t>(current_track->num_checkpoints))
			return false;
		if (spark_checkpoint == car_checkpoint)
			return true;
		const CollisionCheckpoint& cp = current_track->checkpoints[car_checkpoint];
		for (int n = 0; n < cp.num_neighboring_checkpoints; ++n) {
			if (cp.neighboring_checkpoints && cp.neighboring_checkpoints[n] == spark_checkpoint) {
				return true;
			}
		}
		return false;
	};

	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		SuperSpark& spark = super_sparks[i];
		if (!spark.active)
			continue;
		spark.prev_position = spark.position;

		if (!spark.collectable) {
			const float t = std::min(static_cast<float>(spark.animation_frame) / static_cast<float>(kSparkAnimationFrames), 1.0f);
			const float arc = 4.0f * t * (1.0f - t);
			spark.position = spark.start_position.lerp(spark.final_position, t) + spark.plane_normal * (kSparkArcHeight * arc);
			if (spark.animation_frame >= kSparkAnimationFrames) {
				spark.position = spark.final_position;
				spark.collectable = 1;
			} else {
				spark.animation_frame += 1;
				continue;
			}
		}

		for (int car_idx = 0; car_idx < num_cars; ++car_idx) {
			PhysicsCarSoA& car_soa = *cars[car_idx].soa;
			const int lane = cars[car_idx].soa_index;
			if (car_soa.s_boost_active[lane] || (car_soa.machine_state[lane] & MACHINESTATE::ZEROHP) != 0)
				continue;
			if (!checkpoint_matches(spark.checkpoint, car_soa.current_checkpoint[lane]))
				continue;
			SimVec3 closest = get_closest_point_to_segment(
				spark.position, LOAD_INDEXED_VEC3(car_soa, position_old, lane), LOAD_INDEXED_VEC3(car_soa, position_current, lane));
			float dist_sq = spark.position.distance_squared_to(closest);
			if (dist_sq <= collect_radius_sq) {
				if (car_soa.s_boost_charge[lane] < car_soa.s_boost_charge_max[lane]) {
					car_soa.s_boost_charge[lane] += 1;
				}
				car_soa.base_speed[lane] += 0.05f;
				spark.active = 0;
				spark.collectable = 0;
				break;
			}
		}
	}
}

void GameSim::update_super_spark_visuals()
{
	if (!spark_node_container || !super_sparks)
		return;
	if (!spark_multimesh_instance) {
		Node *spark_node = spark_node_container->get_node_or_null(NodePath("SparkMultiMesh"));
		spark_multimesh_instance = Object::cast_to<godot::MultiMeshInstance3D>(spark_node);
		if (!spark_multimesh_instance) {
			return;
		}
	}
	Ref<godot::MultiMesh> spark_multimesh = spark_multimesh_instance->get_multimesh();
	if (spark_multimesh.is_null()) {
		return;
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	int active_count = 0;
	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		if (super_sparks[i].active == 0) {
			continue;
		}
		godot::Transform3D spark_transform;
		const SimVec3 render_position = super_sparks[i].prev_position.lerp(super_sparks[i].position, alpha);
		spark_transform.origin = gd_vec3(render_position);
		spark_multimesh->set_instance_transform(active_count, spark_transform);
		active_count += 1;
	}
	spark_multimesh->set_visible_instance_count(active_count);
}

	void GameSim::render_gamesim() {
		auto render_start = std::chrono::high_resolution_clock::now();
		uint32_t render_get_children_us = 0;
		uint32_t render_visual_apply_us = 0;
		uint32_t render_cpu_total_us = 0;
		uint32_t render_cpu_build_obs_us = 0;
		uint32_t render_cpu_submit_us = 0;
		uint32_t render_sparks_us = 0;
		uint32_t render_debug_draw_us = 0;
		int render_vis_car_count = 0;

		if (!sim_started || !car_node_container || !cars) {
			return;
		}

		if (car_node_container == nullptr) {
			return;
		}

		auto phase_start = std::chrono::high_resolution_clock::now();
		TypedArray<godot::Node> vis_cars = car_node_container->get_children();
		auto phase_end = std::chrono::high_resolution_clock::now();
		render_get_children_us = elapsed_us(phase_start, phase_end);
		const int vis_car_count = std::min(num_cars, static_cast<int>(vis_cars.size()));
		render_vis_car_count = vis_car_count;
		phase_start = std::chrono::high_resolution_clock::now();
		update_render_visual_snapshots(vis_car_count);
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		apply_render_multimeshes(alpha);
		update_native_gameplay_camera(true);
		godot::Array local_visual_args;
		local_visual_args.resize(50);
		for (int i = 0; i < vis_car_count; i++) {
			godot::Object *vis_car = Object::cast_to<godot::Object>(vis_cars[i]);
			if (vis_car && static_cast<bool>(vis_car->get("local_visual_enabled"))) {
				populate_visual_car_args(local_visual_args, cars[i]);
				vis_car->callv("apply_sim_state", local_visual_args);
			}
		}
		phase_end = std::chrono::high_resolution_clock::now();
		render_visual_apply_us = elapsed_us(phase_start, phase_end);
		if (car_player_ids && car_is_cpu) {
			auto cpu_start = std::chrono::high_resolution_clock::now();
			auto cpu_build_start = std::chrono::high_resolution_clock::now();
			update_native_cpu_drivers();
			auto cpu_build_end = std::chrono::high_resolution_clock::now();
			render_cpu_build_obs_us = elapsed_us(cpu_build_start, cpu_build_end);
			auto cpu_end = std::chrono::high_resolution_clock::now();
			render_cpu_total_us = elapsed_us(cpu_start, cpu_end);
		}
		phase_start = std::chrono::high_resolution_clock::now();
		update_super_spark_visuals();
		phase_end = std::chrono::high_resolution_clock::now();
		render_sparks_us = elapsed_us(phase_start, phase_end);
		phase_start = std::chrono::high_resolution_clock::now();
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_CHECKPOINTS))
		{
			for (int i = 0; i < current_track->num_checkpoints; i++)
			{
				current_track->checkpoints[i].debug_draw();
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_BRANCH_CENTERLINE))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			if (dd3d && num_cars > 0)
			{
				int cp_idx = cars[0].soa->current_checkpoint[cars[0].soa_index];
				if (cp_idx >= 0 && cp_idx < current_track->num_checkpoints)
				{
					std::vector<int> branch_indices;
					current_track->collect_branch_sequence(cp_idx, branch_indices);
					if (!branch_indices.empty())
					{
						for (size_t b = 0; b < branch_indices.size(); ++b)
						{
							int idx = branch_indices[b];
							if (idx < 0 || idx >= current_track->num_checkpoints)
							{
								continue;
							}
							const CollisionCheckpoint &cp = current_track->checkpoints[idx];
							dd3d->call("draw_line", gd_vec3(cp.position_start), gd_vec3(cp.position_end), godot::Color(1.0f, 0.9f, 0.1f), _TICK_DELTA);
							if (b + 1 < branch_indices.size())
							{
								int next_idx = branch_indices[b + 1];
								if (next_idx >= 0 && next_idx < current_track->num_checkpoints)
								{
									const CollisionCheckpoint &next_cp = current_track->checkpoints[next_idx];
									dd3d->call("draw_line", gd_vec3(cp.position_end), gd_vec3(next_cp.position_start), godot::Color(0.6f, 0.8f, 0.2f), _TICK_DELTA);
								}
							}
						}
					}
				}
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			for (int i = 0; i < current_track->num_segments; i++)
			{
				dd3d->call("draw_aabb", gd_aabb(current_track->segments[i].bounds), godot::Color(1.0f, 0.0f, 1.0f, 0.1f), _TICK_DELTA);
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF))
		{
		//DEBUG::disp_text("current checkpoint", cars[0].soa->current_checkpoint[cars[0].soa_index]);
			int use_seg_ind = current_track->checkpoints[cars[0].soa->current_checkpoint[cars[0].soa_index]].road_segment;
			for (int i = 0; i < current_track->num_segments; i++)
			{
				if (i > use_seg_ind + 1 || i < use_seg_ind - 1){
					continue;
				}
				godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

			const int x_subdiv = 16; // Adjust as needed
			const int y_subdiv = 32;  // Adjust as needed

			for (int yi = 0; yi <= y_subdiv; yi++)
			{
				float y_frac = static_cast<float>(yi) / y_subdiv;
				float y_val = y_frac; // Y: 0.0 to 1.0

				for (int xi = 0; xi <= x_subdiv; xi++)
				{
					float x_frac = static_cast<float>(xi) / x_subdiv;
					float x_val = -1.0f + 2.0f * x_frac; // X: -1.0 to +1.0

					// Interpolated color: red to blue across X, green from 0 to 1 across Y
					float r = 1.0f - x_frac;
					float g = y_frac;
					float b = x_frac;

					SimVec2 shape_pos(x_val, y_val);
					SimTransform road_transform;
					current_track->segments[i].road_shape->get_oriented_transform_at_time(road_transform, shape_pos);

					SimVec3 start = road_transform.origin;
					SimVec3 end = start + road_transform.basis.transposed().get_column(1) * 2.0f; // arrow in local Y/up

					dd3d->call("draw_arrow", gd_vec3(start), gd_vec3(end), godot::Color(r, g, b), 0.5, true, _TICK_DELTA);
				}
			}
		}
	}
		phase_end = std::chrono::high_resolution_clock::now();
		render_debug_draw_us = elapsed_us(phase_start, phase_end);
		auto render_end = std::chrono::high_resolution_clock::now();
		uint32_t sample[RENDER_PROFILE_FIELD_COUNT] = {
			elapsed_us(render_start, render_end),
			render_get_children_us,
			render_visual_apply_us,
			render_cpu_total_us,
			render_cpu_build_obs_us,
			render_cpu_submit_us,
			render_sparks_us,
			render_debug_draw_us,
			static_cast<uint32_t>(render_vis_car_count),
		};
		record_render_profile_sample(sample);
}

void GameSim::save_state()
{
	int index = tick % STATE_BUFFER_LEN;
	int size = gamestate_data.get_size();
	state_buffer[index].size = size;
	if (state_buffer[index].data)
	{
		memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
	}
}

void GameSim::load_state(int target_tick)
{
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return;
	const int correction_count = std::min(num_cars, static_cast<int>(render_rollback_corrections.size()));
	render_rollback_capture_transforms.clear();
	render_rollback_capture_pending = false;
	if (correction_count > 0 && cars) {
		render_rollback_capture_transforms.resize(correction_count);
		for (int i = 0; i < correction_count; ++i) {
			SimTransform current_visual = MXT_LOAD_TRANSFORM(*cars[i].soa, transform_visual, cars[i].soa_index);
			if (i < static_cast<int>(render_vehicle_visual_state.size())) {
				GameSim::RenderVehicleVisualState capture_visual_state = render_vehicle_visual_state[i];
				current_visual = compose_machine_visual_transform_for_render(
					*cars[i].soa,
					cars[i].soa_index,
					capture_visual_state,
					true,
					false);
			}
			const SimTransform predicted_transform = corrected_render_transform(
				render_rollback_corrections,
				render_rollback_correction_active,
				i,
				current_visual);
			render_rollback_capture_transforms[i] = predicted_transform;
		}
		render_rollback_capture_pending = true;
	}
	int size = state_buffer[index].size;
	memcpy(gamestate_data.heap_start, state_buffer[index].data, size);
	gamestate_data.set_size(size);
	tick = target_tick + 1;
	fix_pointers();
}

void GameSim::finish_render_rollback_correction_capture()
{
	if (!render_rollback_capture_pending) {
		return;
	}
	render_rollback_capture_pending = false;
	const int correction_count = std::min(num_cars, static_cast<int>(render_rollback_capture_transforms.size()));
	if (correction_count > 0 && cars) {
		if (static_cast<int>(render_rollback_corrections.size()) < correction_count) {
			render_rollback_corrections.resize(correction_count);
			render_rollback_correction_active.resize(correction_count);
		}
		for (int i = 0; i < correction_count; ++i) {
			SimTransform current_visual = MXT_LOAD_TRANSFORM(*cars[i].soa, transform_visual, cars[i].soa_index);
			if (i < static_cast<int>(render_vehicle_visual_state.size())) {
				GameSim::RenderVehicleVisualState capture_visual_state = render_vehicle_visual_state[i];
				current_visual = compose_machine_visual_transform_for_render(
					*cars[i].soa,
					cars[i].soa_index,
					capture_visual_state,
					true,
					false);
			}
			SimTransform correction;
			correction.origin = render_rollback_capture_transforms[i].origin - current_visual.origin;
			correction.basis = current_visual.basis.transposed() * render_rollback_capture_transforms[i].basis;
			render_rollback_corrections[i] = correction;
			render_rollback_correction_active[i] = render_correction_is_small(correction) ? 0 : 1;
		}
	}
	render_rollback_capture_transforms.clear();
}

namespace {
constexpr uint32_t MXT_NET_STATE_MAGIC = 0x5354584du; // "MXTS", little-endian.
constexpr uint16_t MXT_NET_STATE_VERSION = 1;

struct NetStateWriter {
	std::vector<uint8_t> data;

	template <typename T>
	void write_pod(const T& value) {
		const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
		data.insert(data.end(), src, src + sizeof(T));
	}

	void write_bytes(const void* src, size_t size) {
		if (!src || size == 0) {
			return;
		}
		const uint8_t* bytes = reinterpret_cast<const uint8_t*>(src);
		data.insert(data.end(), bytes, bytes + size);
	}

	void write_vec3(const SimVec3& v) {
		write_pod(v.x);
		write_pod(v.y);
		write_pod(v.z);
	}

	void write_vec2(const SimVec2& v) {
		write_pod(v.x);
		write_pod(v.y);
	}

	void write_quat(const SimQuat& q) {
		write_pod(q.x);
		write_pod(q.y);
		write_pod(q.z);
		write_pod(q.w);
	}

	void write_basis(const SimBasis& b) {
		for (int col = 0; col < 3; ++col) {
			write_vec3(b.get_column(col));
		}
	}

	void write_transform(const SimTransform& t) {
		for (int col = 0; col < 3; ++col) {
			write_vec3(t.basis.get_column(col));
		}
		write_vec3(t.origin);
	}

	godot::PackedByteArray to_packed_byte_array() const {
		godot::PackedByteArray out;
		out.resize(static_cast<int>(data.size()));
		if (!data.empty()) {
			std::memcpy(out.ptrw(), data.data(), data.size());
		}
		return out;
	}
};

struct NetStateReader {
	const uint8_t* data = nullptr;
	int size = 0;
	int pos = 0;

	explicit NetStateReader(const godot::PackedByteArray& bytes) {
		data = bytes.ptr();
		size = bytes.size();
	}

	template <typename T>
	bool read_pod(T& out) {
		if (pos < 0 || pos + static_cast<int>(sizeof(T)) > size) {
			return false;
		}
		std::memcpy(&out, data + pos, sizeof(T));
		pos += static_cast<int>(sizeof(T));
		return true;
	}

	bool read_bytes(void* dst, size_t byte_count) {
		if (byte_count == 0) {
			return true;
		}
		if (!dst || pos < 0 || pos + static_cast<int>(byte_count) > size) {
			return false;
		}
		std::memcpy(dst, data + pos, byte_count);
		pos += static_cast<int>(byte_count);
		return true;
	}

	bool read_vec3(SimVec3& out) {
		return read_pod(out.x) && read_pod(out.y) && read_pod(out.z);
	}

	bool read_vec2(SimVec2& out) {
		return read_pod(out.x) && read_pod(out.y);
	}

	bool read_quat(SimQuat& out) {
		return read_pod(out.x) && read_pod(out.y) && read_pod(out.z) && read_pod(out.w);
	}

	bool read_basis(SimBasis& out) {
		SimVec3 c0, c1, c2;
		if (!read_vec3(c0) || !read_vec3(c1) || !read_vec3(c2)) {
			return false;
		}
		out.set_column(0, c0);
		out.set_column(1, c1);
		out.set_column(2, c2);
		return true;
	}

	bool read_transform(SimTransform& out) {
		SimVec3 c0, c1, c2;
		if (!read_vec3(c0) || !read_vec3(c1) || !read_vec3(c2) || !read_vec3(out.origin)) {
			return false;
		}
		out.basis.set_column(0, c0);
		out.basis.set_column(1, c1);
		out.basis.set_column(2, c2);
		return true;
	}
};
}

#define MXT_NET_CAR_SCALAR_FIELDS(X) \
	X(uint32_t, machine_state) \
	X(uint32_t, boost_frames) \
	X(uint32_t, boost_frames_manual) \
	X(uint32_t, simulation_tick) \
	X(uint32_t, last_hit_tick) \
	X(uint32_t, spinattack_direction) \
	X(uint32_t, brake_timer) \
	X(uint32_t, terrain_state) \
	X(uint32_t, frames_since_start) \
	X(uint32_t, frames_since_start_2) \
	X(uint32_t, air_time) \
	X(uint32_t, strafe_effect) \
	X(uint32_t, frames_since_death) \
	X(uint32_t, terrain_state_2) \
	X(uint32_t, suspension_reset_flag) \
	X(uint32_t, state_2) \
	X(uint32_t, g_anim_timer) \
	X(uint64_t, level_start_time) \
	X(int, some_breakdown_int) \
	X(int, breakdown_frame_counter) \
	X(uint32_t, restore_wait_frames) \
	X(uint32_t, restore_move_frames) \
	X(int, collision_old_cp) \
	X(uint16_t, current_checkpoint) \
	X(uint16_t, current_collision_checkpoint) \
	X(uint16_t, last_ground_checkpoint) \
	X(uint8_t, lap) \
	X(uint8_t, rail_collision_timer) \
	X(uint8_t, grip_frames_from_accel_press) \
	X(uint8_t, side_attack_delay) \
	X(uint8_t, machine_collision_frame_counter) \
	X(uint8_t, car_hit_invincibility) \
	X(uint8_t, boost_delay_frame_counter) \
	X(int8_t, drift_sign) \
	X(uint8_t, restore_state) \
	X(uint16_t, s_boost_charge) \
	X(uint16_t, s_boost_charge_max) \
	X(uint16_t, s_boost_frames_remaining) \
	X(uint16_t, s_boost_emit_frame_accumulator) \
	X(uint8_t, s_boost_pending_spark_spawns) \
	X(uint8_t, pending_super_sparks) \
	X(bool, has_last_hit_tick) \
	X(bool, machine_crashed) \
	X(bool, s_boost_active) \
	X(bool, collision_old_valid) \
	X(bool, collision_old_was_above) \
	X(bool, collision_old_was_inside) \
	X(float, base_speed) \
	X(float, boost_turbo) \
	X(float, dashplate_heat_multiplier) \
	X(float, race_start_charge) \
	X(float, air_tilt) \
	X(float, energy) \
	X(float, spinattack_angle) \
	X(float, spinattack_decrement) \
	X(float, height_above_track) \
	X(float, last_ground_distance) \
	X(float, checkpoint_fraction) \
	X(float, input_strafe_32) \
	X(float, input_strafe_1_6) \
	X(float, input_accel) \
	X(float, damage_from_last_hit) \
	X(float, turn_reaction_input) \
	X(float, turning_related) \
	X(float, drift_ramp) \
	X(float, side_attack_indicator)

#define MXT_NET_CAR_VEC3_FIELDS(X) \
	X(position_current) \
	X(position_old) \
	X(position_old_2) \
	X(position_old_dupe) \
	X(velocity) \
	X(knockback_velocity) \
	X(velocity_angular) \
	X(track_surface_normal) \
	X(track_surface_pos) \
	X(unk_vec3_0x4e4) \
	X(unk_vec3_0x4f0)

#define MXT_NET_CAR_TRANSFORM_FIELDS(X)

#define MXT_NET_TILT_SCALAR_FIELDS(X) \
	X(float, force) \
	X(uint32_t, state)

#define MXT_NET_TILT_VEC3_FIELDS(X)

#define MXT_NET_WALL_VEC3_FIELDS(X)

godot::PackedByteArray GameSim::serialize_network_state(int target_tick) const {
	NetStateWriter writer;
	writer.write_pod(MXT_NET_STATE_MAGIC);
	writer.write_pod(MXT_NET_STATE_VERSION);
	writer.write_pod(static_cast<uint16_t>(0));
	writer.write_pod(static_cast<int32_t>(target_tick));
	writer.write_pod(static_cast<int32_t>(num_cars));

	const int trigger_count = current_track ? current_track->num_trigger_colliders : 0;
	writer.write_pod(static_cast<int32_t>(trigger_count));
	uint16_t active_spark_count = 0;
	if (super_spark_state) {
		for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
			if (super_spark_state->sparks[i].active) {
				++active_spark_count;
			}
		}
		writer.write_pod(super_spark_state->cursor);
		writer.write_pod(super_spark_state->rng_state);
		writer.write_pod(super_spark_state->placement_timer);
		writer.write_pod(active_spark_count);
		for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
			const SuperSpark& spark = super_spark_state->sparks[i];
			if (!spark.active) {
				continue;
			}
			writer.write_pod(i);
			writer.write_pod(spark.active);
			writer.write_pod(spark.collectable);
			writer.write_pod(spark.animation_frame);
			writer.write_pod(spark.checkpoint);
			writer.write_vec3(spark.position);
			writer.write_vec3(spark.prev_position);
			writer.write_vec3(spark.start_position);
			writer.write_vec3(spark.final_position);
			writer.write_vec3(spark.plane_normal);
		}
	} else {
		uint16_t cursor = 0;
		uint32_t rng_state = 0;
		uint32_t placement_timer = 0;
		writer.write_pod(cursor);
		writer.write_pod(rng_state);
		writer.write_pod(placement_timer);
		writer.write_pod(active_spark_count);
	}

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
#define WRITE_NET_SCALAR(type, name) writer.write_pod(soa.name[lane]);
		MXT_NET_CAR_SCALAR_FIELDS(WRITE_NET_SCALAR)
#undef WRITE_NET_SCALAR
#define WRITE_NET_VEC3(name) writer.write_vec3(LOAD_INDEXED_VEC3(soa, name, lane));
		MXT_NET_CAR_VEC3_FIELDS(WRITE_NET_VEC3)
#undef WRITE_NET_VEC3
#define WRITE_NET_TRANSFORM(name) writer.write_transform(MXT_LOAD_TRANSFORM(soa, name, lane));
		MXT_NET_CAR_TRANSFORM_FIELDS(WRITE_NET_TRANSFORM)
#undef WRITE_NET_TRANSFORM
		writer.write_basis(MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis);
		writer.write_basis(MXT_LOAD_TRANSFORM(soa, basis_physical_other, lane).basis);
		if (soa.collision_old_valid[lane]) {
			writer.write_vec2(soa.collision_old_road_t[lane]);
			writer.write_vec3(soa.collision_old_spatial_t[lane]);
			writer.write_transform(soa.collision_old_surface[lane]);
		}
		if (soa.restore_state[lane] != 0) {
			writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_start_transform, lane));
			writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_target_transform, lane));
		}

		const int point_base = lane * 4;
		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
#define WRITE_NET_TILT_SCALAR(type, name) writer.write_pod(soa.tilt_##name[p]);
			MXT_NET_TILT_SCALAR_FIELDS(WRITE_NET_TILT_SCALAR)
#undef WRITE_NET_TILT_SCALAR
#define WRITE_NET_TILT_VEC3(name) writer.write_vec3(SimVec3(soa.tilt_##name##_x[p], soa.tilt_##name##_y[p], soa.tilt_##name##_z[p]));
			MXT_NET_TILT_VEC3_FIELDS(WRITE_NET_TILT_VEC3)
#undef WRITE_NET_TILT_VEC3
#define WRITE_NET_WALL_VEC3(name) writer.write_vec3(SimVec3(soa.wall_##name##_x[p], soa.wall_##name##_y[p], soa.wall_##name##_z[p]));
			MXT_NET_WALL_VEC3_FIELDS(WRITE_NET_WALL_VEC3)
#undef WRITE_NET_WALL_VEC3
		}
	}

	for (int i = 0; i < trigger_count; ++i) {
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		uint8_t exploded = 0;
		float heat = 0.0f;
		uint32_t last_activation_tick = 0;
		uint8_t has_last_activation = 0;
		if (trigger && trigger->type == TRIGGER_TYPE::MINE) {
			exploded = static_cast<Mine*>(trigger)->exploded ? 1 : 0;
		} else if (trigger && trigger->type == TRIGGER_TYPE::DASHPLATE) {
			Dashplate* dash = static_cast<Dashplate*>(trigger);
			heat = dash->heat;
			last_activation_tick = dash->last_activation_tick;
			has_last_activation = dash->has_last_activation ? 1 : 0;
		}
		writer.write_pod(exploded);
		writer.write_pod(heat);
		writer.write_pod(last_activation_tick);
		writer.write_pod(has_last_activation);
	}

	return writer.to_packed_byte_array();
}

bool GameSim::deserialize_network_state(int target_tick, const godot::PackedByteArray& data) {
	NetStateReader reader(data);
	uint32_t magic = 0;
	uint16_t version = 0;
	uint16_t flags = 0;
	int32_t snapshot_tick = 0;
	int32_t snapshot_cars = 0;
	int32_t trigger_count = 0;
	if (!reader.read_pod(magic) || magic != MXT_NET_STATE_MAGIC ||
		!reader.read_pod(version) || version != MXT_NET_STATE_VERSION ||
		!reader.read_pod(flags) ||
		!reader.read_pod(snapshot_tick) ||
		!reader.read_pod(snapshot_cars) ||
		!reader.read_pod(trigger_count)) {
		return false;
	}
	(void)flags;
	(void)snapshot_tick;
	if (snapshot_cars != num_cars || trigger_count < 0) {
		return false;
	}
	if (!super_spark_state) {
		return false;
	}
	uint16_t spark_cursor = 0;
	uint32_t spark_rng_state = 0;
	uint32_t spark_placement_timer = 0;
	uint16_t active_spark_count = 0;
	if (!reader.read_pod(spark_cursor) ||
		!reader.read_pod(spark_rng_state) ||
		!reader.read_pod(spark_placement_timer) ||
		!reader.read_pod(active_spark_count)) {
		return false;
	}
	super_spark_state->cursor = spark_cursor;
	super_spark_state->rng_state = spark_rng_state;
	super_spark_state->placement_timer = spark_placement_timer;
	for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		super_spark_state->sparks[i] = SuperSpark();
	}
	for (uint16_t n = 0; n < active_spark_count; ++n) {
		uint16_t spark_index = 0;
		if (!reader.read_pod(spark_index) || spark_index >= SUPER_SPARK_CAPACITY) {
			return false;
		}
		SuperSpark& spark = super_spark_state->sparks[spark_index];
		if (!reader.read_pod(spark.active) ||
			!reader.read_pod(spark.collectable) ||
			!reader.read_pod(spark.animation_frame) ||
			!reader.read_pod(spark.checkpoint) ||
			!reader.read_vec3(spark.position) ||
			!reader.read_vec3(spark.prev_position) ||
			!reader.read_vec3(spark.start_position) ||
			!reader.read_vec3(spark.final_position) ||
			!reader.read_vec3(spark.plane_normal)) {
			return false;
		}
	}
	super_sparks = super_spark_state->sparks;

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
#define READ_NET_SCALAR(type, name) if (!reader.read_pod(soa.name[lane])) return false;
		MXT_NET_CAR_SCALAR_FIELDS(READ_NET_SCALAR)
#undef READ_NET_SCALAR
#define READ_NET_VEC3(name) do { SimVec3 v; if (!reader.read_vec3(v)) return false; STORE_INDEXED_VEC3(soa, name, lane, v); } while (0);
		MXT_NET_CAR_VEC3_FIELDS(READ_NET_VEC3)
#undef READ_NET_VEC3
#define READ_NET_TRANSFORM(name) do { SimTransform t; if (!reader.read_transform(t)) return false; MXT_STORE_TRANSFORM(soa, name, lane, t); } while (0);
		MXT_NET_CAR_TRANSFORM_FIELDS(READ_NET_TRANSFORM)
#undef READ_NET_TRANSFORM
		SimBasis basis_physical;
		if (!reader.read_basis(basis_physical)) {
			return false;
		}
		MXT_STORE_TRANSFORM(soa, basis_physical, lane, SimTransform(basis_physical, SimVec3()));
		SimBasis basis_physical_other;
		if (!reader.read_basis(basis_physical_other)) {
			return false;
		}
		MXT_STORE_TRANSFORM(soa, basis_physical_other, lane, SimTransform(basis_physical_other, SimVec3()));
		if (soa.collision_old_valid[lane]) {
			if (!reader.read_vec2(soa.collision_old_road_t[lane]) ||
				!reader.read_vec3(soa.collision_old_spatial_t[lane]) ||
				!reader.read_transform(soa.collision_old_surface[lane])) {
				return false;
			}
		} else {
			soa.collision_old_road_t[lane] = SimVec2();
			soa.collision_old_spatial_t[lane] = SimVec3();
			soa.collision_old_surface[lane] = SimTransform();
		}
		if (soa.restore_state[lane] != 0) {
			SimTransform restore_start;
			SimTransform restore_target;
			if (!reader.read_transform(restore_start) ||
				!reader.read_transform(restore_target)) {
				return false;
			}
			MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, restore_start);
			MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, restore_target);
		} else {
			const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
			MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, basis);
			MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, basis);
		}

		const int point_base = lane * 4;
		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
#define READ_NET_TILT_SCALAR(type, name) if (!reader.read_pod(soa.tilt_##name[p])) return false;
			MXT_NET_TILT_SCALAR_FIELDS(READ_NET_TILT_SCALAR)
#undef READ_NET_TILT_SCALAR
#define READ_NET_TILT_VEC3(name) do { SimVec3 v; if (!reader.read_vec3(v)) return false; soa.tilt_##name##_x[p] = v.x; soa.tilt_##name##_y[p] = v.y; soa.tilt_##name##_z[p] = v.z; } while (0);
			MXT_NET_TILT_VEC3_FIELDS(READ_NET_TILT_VEC3)
#undef READ_NET_TILT_VEC3
#define READ_NET_WALL_VEC3(name) do { SimVec3 v; if (!reader.read_vec3(v)) return false; soa.wall_##name##_x[p] = v.x; soa.wall_##name##_y[p] = v.y; soa.wall_##name##_z[p] = v.z; } while (0);
			MXT_NET_WALL_VEC3_FIELDS(READ_NET_WALL_VEC3)
#undef READ_NET_WALL_VEC3
		}
	}

	const int local_trigger_count = current_track ? current_track->num_trigger_colliders : 0;
	if (trigger_count != local_trigger_count) {
		return false;
	}
	for (int i = 0; i < trigger_count; ++i) {
		uint8_t exploded = 0;
		float heat = 0.0f;
		uint32_t last_activation_tick = 0;
		uint8_t has_last_activation = 0;
		if (!reader.read_pod(exploded) ||
			!reader.read_pod(heat) ||
			!reader.read_pod(last_activation_tick) ||
			!reader.read_pod(has_last_activation)) {
			return false;
		}
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		if (!trigger) {
			continue;
		}
		if (trigger->type == TRIGGER_TYPE::MINE) {
			static_cast<Mine*>(trigger)->exploded = exploded != 0;
		} else if (trigger->type == TRIGGER_TYPE::DASHPLATE) {
			Dashplate* dash = static_cast<Dashplate*>(trigger);
			dash->heat = heat;
			dash->last_activation_tick = last_activation_tick;
			dash->has_last_activation = has_last_activation != 0;
		}
	}

	rebuild_static_state_after_network_load();
	const int index = target_tick % STATE_BUFFER_LEN;
	const int size = gamestate_data.get_size();
	if (state_buffer[index].data && size > 0) {
		std::memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
		state_buffer[index].size = size;
	}
	return true;
}

void GameSim::rebuild_static_state_after_network_load() {
	fix_pointers();
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCar& car = cars[i];
		PhysicsCarSoA& soa = *car.soa;
		const int lane = car.soa_index;
		car.update_machine_stats();
		if (soa.car_properties[lane]) {
			soa.calced_max_energy[lane] = soa.car_properties[lane]->max_energy;
		}
		soa.weight_derived_1[lane] = 52.0f * soa.stat_weight[lane] * 0.0625f;
		soa.weight_derived_2[lane] = 45.0f * soa.stat_weight[lane] * 0.0625f;
		soa.weight_derived_3[lane] = 52.0f * soa.stat_weight[lane] * 0.0625f;

		const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
		const SimVec3 position = LOAD_INDEXED_VEC3(soa, position_current, lane);
		STORE_INDEXED_VEC3(soa, position_collision_snapshot, lane, position);
		STORE_INDEXED_VEC3(soa, position_bottom, lane, basis.basis.xform(SimVec3(0.0f, -0.1f, 0.0f)) + position);
		STORE_INDEXED_VEC3(soa, position_behind, lane, basis.basis.xform(SimVec3(0.0f, 0.5f, 0.5f)) + position);
		STORE_INDEXED_VEC3(soa, track_surface_normal_prev, lane, LOAD_INDEXED_VEC3(soa, track_surface_normal, lane));
		STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_track, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_rail, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_total, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_response, lane, SimVec3());
		soa.input_steer_pitch[lane] = 0.0f;
		soa.input_strafe[lane] = 0.0f;
		soa.input_steer_yaw[lane] = 0.0f;
		soa.input_brake[lane] = 0.0f;
		soa.input_yaw_dupe[lane] = 0.0f;

		const SimVec3 velocity = LOAD_INDEXED_VEC3(soa, velocity, lane);
		if (std::abs(soa.stat_weight[lane]) > 0.0001f) {
			soa.speed_kmh[lane] = 216.0f * (velocity.length() / soa.stat_weight[lane]);
		} else {
			soa.speed_kmh[lane] = 0.0f;
		}

		soa.lap_progress[lane] = 0.0f;
		soa.checkpoint_track_distance[lane] = 0.0f;
		RaceTrack* track = soa.current_track[lane] ? soa.current_track[lane] : current_track;
		if (track &&
			soa.current_checkpoint[lane] >= 0 &&
			soa.current_checkpoint[lane] < track->num_checkpoints) {
			const CollisionCheckpoint& cp = track->checkpoints[soa.current_checkpoint[lane]];
			const float fraction = std::clamp(soa.checkpoint_fraction[lane], 0.0f, 1.0f);
			soa.lap_progress[lane] =
				(static_cast<float>(soa.current_checkpoint[lane]) + fraction) / static_cast<float>(track->num_checkpoints);
			float ground_distance = cp.distance - cp.local_distance + cp.local_distance * fraction;
			float lap_length = track->lap_length;
			if (lap_length <= 0.0f && track->num_checkpoints > 0) {
				lap_length = track->checkpoints[track->num_checkpoints - 1].distance;
			}
			if (lap_length > 0.0f) {
				ground_distance = std::fmod(ground_distance, lap_length);
				if (ground_distance < 0.0f) {
					ground_distance += lap_length;
				}
			}
			soa.checkpoint_track_distance[lane] = ground_distance;
		}

		RoadData& road = soa.road_sample[lane];
		road = RoadData();
		road.cp_idx = static_cast<int16_t>(soa.current_checkpoint[lane]);
		if (track &&
			soa.current_checkpoint[lane] >= 0 &&
			soa.current_checkpoint[lane] < track->num_checkpoints) {
			track->get_road_surface(soa.current_checkpoint[lane], position, road.road_t, road.spatial_t, road.closest_surface);
		} else {
			road.closest_surface = basis;
			road.closest_surface.origin = LOAD_INDEXED_VEC3(soa, track_surface_pos, lane);
		}

		const int point_base = lane * 4;
		const SimVec3x4 tilt_pos = transform_points_components4(
			basis.basis.c0.x, basis.basis.c0.y, basis.basis.c0.z,
			basis.basis.c1.x, basis.basis.c1.y, basis.basis.c1.z,
			basis.basis.c2.x, basis.basis.c2.y, basis.basis.c2.z,
			position.x, position.y, position.z,
			sim_load4(soa.tilt_offset_x + point_base),
			sim_load4(soa.tilt_offset_y + point_base) + sim_load4(soa.tilt_force + point_base) - sim_load4(soa.tilt_rest_length + point_base),
			sim_load4(soa.tilt_offset_z + point_base));
		sim_store4(soa.tilt_pos_old_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_old_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_old_z + point_base, tilt_pos.z);
		sim_store4(soa.tilt_pos_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_z + point_base, tilt_pos.z);

		const SimVec3x4 wall_pos = transform_points_components4(
			basis.basis.c0.x, basis.basis.c0.y, basis.basis.c0.z,
			basis.basis.c1.x, basis.basis.c1.y, basis.basis.c1.z,
			basis.basis.c2.x, basis.basis.c2.y, basis.basis.c2.z,
			position.x, position.y, position.z,
			sim_load4(soa.wall_offset_x + point_base),
			sim_load4(soa.wall_offset_y + point_base),
			sim_load4(soa.wall_offset_z + point_base));
		sim_store4(soa.wall_pos_a_x + point_base, wall_pos.x);
		sim_store4(soa.wall_pos_a_y + point_base, wall_pos.y);
		sim_store4(soa.wall_pos_a_z + point_base, wall_pos.z);
		sim_store4(soa.wall_pos_b_x + point_base, wall_pos.x);
		sim_store4(soa.wall_pos_b_y + point_base, wall_pos.y);
		sim_store4(soa.wall_pos_b_z + point_base, wall_pos.z);

		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
			soa.tilt_force_at_point[p] = 0.0f;
			soa.tilt_force_spatial_len[p] = 0.0f;
			STORE_INDEXED_VEC3(soa, tilt_target_dir, p, SimVec3());
			STORE_INDEXED_VEC3(soa, tilt_force_spatial, p, SimVec3());
			SimVec3 tilt_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (tilt_up.length_squared() <= 0.0001f) {
				tilt_up = basis.basis.get_column(1);
			}
			if (tilt_up.length_squared() <= 0.0001f) {
				tilt_up = SimVec3(0.0f, 1.0f, 0.0f);
			}
			tilt_up.normalize();
			STORE_INDEXED_VEC3(soa, tilt_up_vector, p, tilt_up);
			STORE_INDEXED_VEC3(soa, tilt_up_vector_2, p, tilt_up);
			STORE_INDEXED_VEC3(soa, wall_collision, p, SimVec3());
		}
	}
}

godot::PackedByteArray GameSim::get_state_data(int target_tick) const {
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return godot::PackedByteArray();
	return serialize_network_state(target_tick);
}

void GameSim::set_state_data(int target_tick, godot::PackedByteArray data) {
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return;
	if (data.size() >= static_cast<int>(sizeof(uint32_t))) {
		uint32_t magic = 0;
		std::memcpy(&magic, data.ptr(), sizeof(uint32_t));
		if (magic == MXT_NET_STATE_MAGIC) {
			const int live_size = gamestate_data.get_size();
			if (live_size > 0) {
				if (static_cast<int>(network_state_live_backup.size()) < live_size) {
					network_state_live_backup.resize(static_cast<size_t>(live_size));
				}
				std::memcpy(network_state_live_backup.data(), gamestate_data.heap_start, static_cast<size_t>(live_size));
			}
			deserialize_network_state(target_tick, data);
			if (live_size > 0 && static_cast<int>(network_state_live_backup.size()) >= live_size) {
				std::memcpy(gamestate_data.heap_start, network_state_live_backup.data(), static_cast<size_t>(live_size));
				gamestate_data.set_size(live_size);
				fix_pointers();
			}
			return;
		}
	}
	// game state never changes in size after instantiation
	// and should always be the same size between the server and all clients
	int size = static_cast<int>(data.size());
	if (size > 0) {
		memcpy(state_buffer[index].data, data.ptr(), size);
		state_buffer[index].size = size;
	}
}

#undef MXT_NET_CAR_SCALAR_FIELDS
#undef MXT_NET_CAR_VEC3_FIELDS
#undef MXT_NET_CAR_TRANSFORM_FIELDS
#undef MXT_NET_TILT_SCALAR_FIELDS
#undef MXT_NET_TILT_VEC3_FIELDS
#undef MXT_NET_WALL_VEC3_FIELDS

void GameSim::fix_pointers() {
	if (super_spark_state) {
		super_sparks = super_spark_state->sparks;
	} else {
		super_sparks = nullptr;
	}
	if (!sim_started || !cars) {
		return;
	}

	gamestate_data.repair_allocated_cars(cars, num_cars, &car_properties_array);

	const int total_lane_count = (num_cars + 3) & ~3;
	for (int i = 0; i < total_lane_count; ++i) {
		cars[i].soa->current_track[cars[i].soa_index] = current_track;
		// TODO: machine_name is static metadata, not gamestate. Move it out of serialized SoA state.
		cars[i].soa->machine_name[cars[i].soa_index] = "Blue Falcon";
		if (car_properties_array) {
			cars[i].soa->car_properties[cars[i].soa_index] = &car_properties_array[i];
		}
	}

	if (current_track && current_track->trigger_colliders) {
		for (int i = 0; i < current_track->num_trigger_colliders; ++i) {
			TriggerCollider* trig = current_track->trigger_colliders[i];

			TRIGGER_TYPE::TYPE type = trig->type;
			SimTransform transform = trig->transform;
			SimVec3 half_extents = trig->half_extents;
			SimTransform inv_transform = trig->inv_transform;
			int seg_idx = trig->segment_index;
			int cp_idx = trig->checkpoint_index;
			bool exploded = false;
			float dashplate_heat = 0.0f;
			uint32_t dashplate_last_tick = 0;
			bool dashplate_has_last_activation = false;
			if (type == TRIGGER_TYPE::MINE) {
				exploded = static_cast<Mine*>(trig)->exploded;
			}
			if (type == TRIGGER_TYPE::DASHPLATE) {
				Dashplate* dash = static_cast<Dashplate*>(trig);
				dashplate_heat = dash->heat;
				dashplate_last_tick = dash->last_activation_tick;
				dashplate_has_last_activation = dash->has_last_activation;
			}

			switch (type) {
			case TRIGGER_TYPE::DASHPLATE:
				new (trig) Dashplate();
				break;
			case TRIGGER_TYPE::JUMPPLATE:
				new (trig) Jumpplate();
				break;
			case TRIGGER_TYPE::MINE:
				new (trig) Mine();
				break;
			default:
				new (trig) TriggerCollider();
				break;
			}

			trig->transform = transform;
			trig->half_extents = half_extents;
			trig->inv_transform = inv_transform;
			trig->segment_index = seg_idx;
			trig->checkpoint_index = cp_idx;
			if (type == TRIGGER_TYPE::MINE) {
				static_cast<Mine*>(trig)->exploded = exploded;
			}
			if (type == TRIGGER_TYPE::DASHPLATE) {
				Dashplate* dash = static_cast<Dashplate*>(trig);
				dash->heat = dashplate_heat;
				dash->last_activation_tick = dashplate_last_tick;
				dash->has_last_activation = dashplate_has_last_activation;
			}

			current_track->trigger_colliders[i] = trig;
		}
	}
}
