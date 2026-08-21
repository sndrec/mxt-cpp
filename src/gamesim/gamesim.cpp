#include "gamesim/gamesim.h"
#include "gamesim/gamesim_cpu_internal.h"
#include "gamesim/gamesim_internal.h"
#include "gamesim/gamesim_render_internal.h"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/input.hpp"
#include "godot_cpp/classes/viewport.hpp"
#include "godot_cpp/classes/world3d.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/string_name.hpp"
#include "godot_cpp/core/math.hpp"
#include "core/curve.h"
#include "core/enums.h"
#include "audio/spatial_audio_manager.h"
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
#include "core/math_utils.h"
#include <chrono>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <new>
#include <type_traits>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#include "core/debug.hpp"

using namespace godot;

static inline float gamesim_dashplate_heat_reward_scale(float leader_gap)
{
	const float clamped_gap = std::clamp(leader_gap, 0.0f, 1000.0f);
	return 0.1f + clamped_gap * 0.0009f;
}

namespace {
	static inline uint64_t profile_now_us()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	static inline void profile_mark(uint64_t* bucket, uint64_t& step)
	{
		if (!bucket) {
			return;
		}
		const uint64_t now = profile_now_us();
		*bucket += now - step;
		step = now;
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

static void begin_vehicle_tick_soa(PhysicsCarSoA& c, PhysicsCar* car_views, PlayerInput* inputs, uint32_t tick_count, int count, bool vehicle_restore_enabled, bool s_boost_enabled)
	{
		for (int i = 0; i < count; ++i) {
			PlayerInput& input = inputs[i];
			const float accel_raw = input.accelerate;
			c.simulation_tick[i] = tick_count;

			if (!s_boost_enabled) {
				c.s_boost_charge[i] = 0;
				c.s_boost_active[i] = false;
				c.s_boost_frames_remaining[i] = 0;
				c.s_boost_emit_frame_accumulator[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
				c.pending_super_sparks[i] = 0;
			} else if (!c.s_boost_active[i]) {
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

			const bool completed_race = (c.machine_state[i] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0;
			const bool fell_out = c.current_track[i] &&
				c.position_current_y[i] < c.current_track[i]->minimum_y;
			const bool zero_hp = c.energy[i] <= 0.0f;
			const bool restore_allowed = vehicle_restore_enabled || completed_race;
			if (fell_out) {
				c.machine_state[i] |= MACHINESTATE::FALLOUT;
			}
			if (!restore_allowed && (fell_out || zero_hp)) {
				if (zero_hp) {
					c.machine_state[i] |= MACHINESTATE::ZEROHP;
					c.energy[i] = 0.0f;
				}
				c.s_boost_active[i] = false;
				c.s_boost_frames_remaining[i] = 0;
				c.s_boost_emit_frame_accumulator[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
			}
			const bool needs_restore =
				restore_allowed &&
				c.current_track[i] &&
				(c.restore_state[i] != 0 || fell_out || zero_hp);
			if (needs_restore) {
				car_views[i].update_restore(accel_raw);
			}

			c.calced_max_energy[i] =
				c.car_properties[i]->base_stats[CAR_STAT_MAX_ENERGY] + c.ko_energy_bonus[i];
			const bool in_startup_countdown = tick_count < c.level_start_time[i];
			if (!in_startup_countdown) {
				STORE_INDEXED_VEC3(c, initial_pos, i, LOAD_INDEXED_VEC3(c, position_current, i));
			}
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

			if ((c.machine_state[i] & MACHINESTATE::ZEROHP) ||
				(!vehicle_restore_enabled && (c.machine_state[i] & MACHINESTATE::FALLOUT))) {
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
			if (input.boost && c.lap[i] > 1 && !c.s_boost_active[i])
				c.machine_state[i] |= MACHINESTATE::JUST_PRESSED_BOOST;

			c.g_anim_timer[i] += 1;
			STORE_INDEXED_VEC3(c, track_surface_normal_prev, i, LOAD_INDEXED_VEC3(c, track_surface_normal, i));
		}
	}

	static void update_damage_visual_geometry_soa(PhysicsCarSoA& c, int count);
	static void project_startup_velocity_and_speed_soa(PhysicsCarSoA& c, int count);

	static inline SimVec3 normalized_or_zero(const SimVec3& v)
	{
		return v.length_squared() > 0.000001f ? v.normalized() : SimVec3();
	}

	static inline SimVec3 remove_axis_component(const SimVec3& v, const SimVec3& axis)
	{
		return v - axis * v.dot(axis);
	}

	static inline SimVec3 keep_axis_component(const SimVec3& v, const SimVec3& axis)
	{
		return axis * v.dot(axis);
	}

	static void translate_contact_points_soa(PhysicsCarSoA& c, int i, const SimVec3& delta)
	{
		if (delta.length_squared() <= 0.0000001f) {
			return;
		}
		const int p = i * 4;
		const SimFloat4 dx(delta.x);
		const SimFloat4 dy(delta.y);
		const SimFloat4 dz(delta.z);
		sim_store4(c.tilt_pos_old_x + p, sim_load4(c.tilt_pos_old_x + p) + dx);
		sim_store4(c.tilt_pos_old_y + p, sim_load4(c.tilt_pos_old_y + p) + dy);
		sim_store4(c.tilt_pos_old_z + p, sim_load4(c.tilt_pos_old_z + p) + dz);
		sim_store4(c.tilt_pos_x + p, sim_load4(c.tilt_pos_x + p) + dx);
		sim_store4(c.tilt_pos_y + p, sim_load4(c.tilt_pos_y + p) + dy);
		sim_store4(c.tilt_pos_z + p, sim_load4(c.tilt_pos_z + p) + dz);
	}

	static void update_machine_corners_soa(PhysicsCarSoA& c, PhysicsCar* car_views, int count,
		TrackQueryScratch &scratch, PhysicsCarCornerProfile* profile = nullptr)
	{
		for (int i = 0; i < count; ++i) {
			if ((c.state_2[i] & 0x8u) && c.restore_state[i] != 2) {
				scratch.debug_mesh_current_global_car_index = c.global_start + i;
				const SimVec3 position_before_corner_collision = LOAD_INDEXED_VEC3(c, position_current, i);
				float max_rail_contact_push = 0.0f;
				const int corner_collision_type_flag = car_views[i].update_machine_corners(scratch, profile, &max_rail_contact_push);
				const SimVec3 rail_push = LOAD_INDEXED_VEC3(c, collision_push_rail, i);
				const SimVec3 track_push = LOAD_INDEXED_VEC3(c, collision_push_track, i);
				const bool just_landed = (c.machine_state[i] & MACHINESTATE::JUSTLANDED) != 0;
				const bool trace_corner_collision =
					DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_RAIL_SAMPLING) &&
					DEBUG::rail_trace_filter_matches(c.global_start + i, c.simulation_tick[i]);
				if (trace_corner_collision) {
					const SimVec3 position_after_corner_collision = LOAD_INDEXED_VEC3(c, position_current, i);
					const SimVec3 machine_up(
						c.basis_physical_c1x[i], c.basis_physical_c1y[i], c.basis_physical_c1z[i]);
					const SimVec3 track_normal = LOAD_INDEXED_VEC3(c, track_surface_normal, i);
					godot::UtilityFunctions::print(
						godot::String("MXT_CORNER_COLLISION_SUMMARY tick="), static_cast<int64_t>(c.simulation_tick[i]),
						godot::String(" car="), static_cast<int64_t>(c.global_start + i),
						godot::String(" flags="), static_cast<int64_t>(corner_collision_type_flag),
						godot::String(" just_landed="), just_landed,
						godot::String(" pos_before=("), position_before_corner_collision.x, godot::String(","), position_before_corner_collision.y, godot::String(","), position_before_corner_collision.z, godot::String(")"),
						godot::String(" pos_after=("), position_after_corner_collision.x, godot::String(","), position_after_corner_collision.y, godot::String(","), position_after_corner_collision.z, godot::String(")"),
						godot::String(" delta=("), position_after_corner_collision.x - position_before_corner_collision.x, godot::String(","), position_after_corner_collision.y - position_before_corner_collision.y, godot::String(","), position_after_corner_collision.z - position_before_corner_collision.z, godot::String(")"),
						godot::String(" track_push=("), track_push.x, godot::String(","), track_push.y, godot::String(","), track_push.z, godot::String(")"),
						godot::String(" rail_push=("), rail_push.x, godot::String(","), rail_push.y, godot::String(","), rail_push.z, godot::String(")"),
						godot::String(" up_dot_track_n="), machine_up.dot(track_normal));
				}
				if (rail_push.length_squared() > 0.0f || track_push.length_squared() > 0.0f || just_landed) {
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
					const bool landing_response = just_landed && speed_over_weight >= 0.0462962962962f;
					if (push_magnitude_track > 0.0023148148f || push_magnitude_rail > 0.0023148148f ||
						full_response || landing_response) {
						constexpr float gx_rail_collision_sfx_strength_scale = 4.0f;
						const float rail_hit_sfx_strength = gx_rail_collision_sfx_strength_scale * max_rail_contact_push;
						const SimVec3 velocity_before_response = LOAD_INDEXED_VEC3(c, velocity, i);
						car_views[i].apply_machine_collision_response_from_corners(corner_collision_type_flag,
							push_magnitude_rail, push_magnitude_track, rail_hit_sfx_strength,
							current_world_speed, speed_over_weight, false);
						if (trace_corner_collision) {
							const SimVec3 velocity_after_response = LOAD_INDEXED_VEC3(c, velocity, i);
							const SimVec3 total_push = LOAD_INDEXED_VEC3(c, collision_push_total, i);
							godot::UtilityFunctions::print(
								godot::String("MXT_COLLISION_RESPONSE_SUMMARY tick="), static_cast<int64_t>(c.simulation_tick[i]),
								godot::String(" car="), static_cast<int64_t>(c.global_start + i),
								godot::String(" flags="), static_cast<int64_t>(corner_collision_type_flag),
								godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(c.machine_state[i]), 16),
								godot::String(" rail_timer="), static_cast<int64_t>(c.rail_collision_timer[i]),
								godot::String(" speed="), current_world_speed,
								godot::String(" speed_over_weight="), speed_over_weight,
								godot::String(" total_push=("), total_push.x, godot::String(","), total_push.y, godot::String(","), total_push.z, godot::String(")"),
								godot::String(" vel_before=("), velocity_before_response.x, godot::String(","), velocity_before_response.y, godot::String(","), velocity_before_response.z, godot::String(")"),
								godot::String(" vel_after=("), velocity_after_response.x, godot::String(","), velocity_after_response.y, godot::String(","), velocity_after_response.z, godot::String(")"));
						}
					}
				}
				if (just_landed) {
					c.air_time[i] = 0;
				}
			}
		}
		scratch.debug_mesh_current_global_car_index = -1;
	}

	static void finish_vehicle_tail_soa(PhysicsCarSoA& c, PhysicsCar* car_views, int count)
	{
		project_startup_velocity_and_speed_soa(c, count);

		update_damage_visual_geometry_soa(c, count);

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
	}

	static void handle_vehicle_checkpoints_soa(PhysicsCarSoA& c, PhysicsCar* car_views, int count,
		TrackQueryScratch &scratch)
	{
		for (int i = 0; i < count; ++i) {
			if (c.restore_state[i] == 2) {
				continue;
			}
			car_views[i].handle_checkpoints(scratch);
			if ((c.machine_state[i] & MACHINESTATE::AIRBORNE) == 0 && (c.machine_state[i] & MACHINESTATE::ZEROHP) == 0) {
				if (DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_RAIL_SAMPLING) &&
					DEBUG::rail_trace_filter_matches(c.global_start + i, c.simulation_tick[i])) {
					SimVec3 pos = LOAD_INDEXED_VEC3(c, position_current, i);
					godot::UtilityFunctions::print(
						godot::String("MXT_LAST_GROUND_TRACE tick="), static_cast<int64_t>(c.simulation_tick[i]),
						godot::String(" car="), static_cast<int64_t>(c.global_start + i),
						godot::String(" old_cp="), static_cast<int64_t>(c.last_ground_checkpoint[i]),
						godot::String(" old_dist="), c.last_ground_distance[i],
						godot::String(" new_cp="), static_cast<int64_t>(c.current_checkpoint[i]),
						godot::String(" new_dist="), c.checkpoint_track_distance[i],
						godot::String(" frac="), c.checkpoint_fraction[i],
						godot::String(" lap="), static_cast<int64_t>(c.lap[i]),
						godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(c.machine_state[i]), 16),
						godot::String(" restore="), static_cast<int64_t>(c.restore_state[i]),
						godot::String(" pos=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"));
				}
				c.last_ground_distance[i] = c.checkpoint_track_distance[i];
				c.last_ground_checkpoint[i] = c.current_checkpoint[i];
			}
		}
	}

	static void collect_pending_s_boost_sparks_soa(PhysicsCarSoA& c, uint8_t* pending_s_boost_sparks, int count,
		bool s_boost_enabled)
	{
		for (int i = 0; i < count; ++i) {
			if (!s_boost_enabled) {
				pending_s_boost_sparks[i] = 0;
				c.s_boost_pending_spark_spawns[i] = 0;
				c.pending_super_sparks[i] = 0;
				continue;
			}
			uint16_t pending = static_cast<uint16_t>(c.s_boost_pending_spark_spawns[i]) + c.pending_super_sparks[i];
			pending_s_boost_sparks[i] = static_cast<uint8_t>(pending > 255 ? 255 : pending);
			c.s_boost_pending_spark_spawns[i] = 0;
			c.pending_super_sparks[i] = 0;
		}
	}

	static bool track_checkpoint_has_trigger_candidates(const RaceTrack* track, int checkpoint)
	{
		if (!track || track->num_trigger_colliders <= 0) {
			return false;
		}
		if (checkpoint < 0 || checkpoint >= track->num_checkpoints ||
				static_cast<int>(track->trigger_checkpoint_offsets.size()) != track->num_checkpoints + 1) {
			return true;
		}
		return track->trigger_checkpoint_offsets[checkpoint] < track->trigger_checkpoint_offsets[checkpoint + 1];
	}

	static bool vehicles_may_emit_trigger_events(PhysicsCar* cars, int count)
	{
		for (int i = 0; i < count; ++i) {
			PhysicsCarSoA& soa = *cars[i].soa;
			const int lane = cars[i].soa_index;
			if ((soa.machine_state[lane] & MACHINESTATE::B29) != 0u) {
				continue;
			}
			if (track_checkpoint_has_trigger_candidates(soa.current_track[lane], soa.current_checkpoint[lane])) {
				return true;
			}
		}
		return false;
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

	static void prepare_vehicle_floor_phase(PhysicsCarSoA& c, PhysicsCar* car_views, int count, TrackQueryScratch &scratch,
		uint64_t* profile_prepare_frame_us = nullptr,
		PhysicsCarFloorProfile* floor_profile = nullptr,
		uint64_t* profile_find_floor_us = nullptr,
		uint64_t* profile_terrain_us = nullptr)
	{
		scratch.reset_trigger_events();
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			scratch.debug_mesh_current_global_car_index = c.global_start + i;
			uint64_t profile_step = profile_prepare_frame_us ? profile_now_us() : 0;
			const uint32_t old_terrain_state = c.terrain_state[i];
			const SimVec3 trigger_p0 = LOAD_INDEXED_VEC3(c, position_old, i);
			const SimVec3 trigger_p1 = LOAD_INDEXED_VEC3(c, position_current, i);
			const SimVec3 ground_normal = car_views[i].prepare_machine_frame(scratch, floor_profile);
			profile_mark(profile_prepare_frame_us, profile_step);
			const bool has_floor = car_views[i].find_floor_beneath_machine(scratch, floor_profile);
			profile_mark(profile_find_floor_us, profile_step);
			if (has_floor) {
				if ((c.machine_state[i] & MACHINESTATE::AIRBORNE) == 0) {
					bool use_analytic_floor_normal = true;
					const bool use_corner_floor_normal = (c.machine_state[i] & MACHINESTATE::ACTIVE) != 0;
					RaceTrack *track = c.current_track[i];
					if (track && c.current_checkpoint[i] < track->num_checkpoints) {
						const TrackSegment &segment = track->segments[track->checkpoints[c.current_checkpoint[i]].road_segment];
						use_analytic_floor_normal = segment.analytic_collision_enabled;
					}
					if (use_analytic_floor_normal && use_corner_floor_normal) {
						STORE_INDEXED_VEC3(c, track_surface_normal, i, ground_normal);
					}
				}
			} else {
				const int base = i * 4;
				for (int lane = 0; lane < 4; ++lane) {
					const int p = base + lane;
					c.tilt_force[p] = 0.0f;
					c.tilt_force_spatial_x[p] = 0.0f;
					c.tilt_force_spatial_y[p] = 0.0f;
					c.tilt_force_spatial_z[p] = 0.0f;
					c.tilt_state[p] |= TILTSTATE::DISCONNECTED | TILTSTATE::AIRBORNE;
				}
			}
			if ((c.machine_state[i] & MACHINESTATE::B29) == 0) {
				car_views[i].set_terrain_state_from_track(scratch, trigger_p0, trigger_p1);
			}
			if ((old_terrain_state & TERRAIN::DASH) != 0u) {
				c.machine_state[i] &= ~MACHINESTATE::JUST_HIT_DASHPLATE;
			}
			profile_mark(profile_terrain_us, profile_step);
		}
		scratch.debug_mesh_current_global_car_index = -1;
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
				if ((event.collision_flags & 0x2) != 0) {
					PhysicsCar* car = cars + c.global_start + event.car_index;
					switch (trigger->type) {
					case TRIGGER_TYPE::DASHPLATE:
						static_cast<Dashplate*>(trigger)->start_touch(car);
						break;
					case TRIGGER_TYPE::MINE:
						static_cast<Mine*>(trigger)->start_touch(car);
						break;
					default:
						break;
					}
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

	static void steering_and_suspension_phase(PhysicsCarSoA& c, PhysicsCar* car_views,
		int count, bool force_first_car_gripped)
	{
		for (int i = 0; i < count; ++i) {
			if (!vehicle_motion_active(c, i)) {
				continue;
			}

			car_views[i].update_effective_machine_stats(false);
			car_views[i].handle_steering();
			car_views[i].handle_suspension_states();

			const float initial_angle_vel_y = c.velocity_angular_y[i];
			if (c.frames_since_start_2[i] != 0) {
				// Benchmark car 0 measures gripped steering; car 1 is the natural
				// both-trigger drift comparison. Ordinary races always allow drift.
				const bool allow_drift =
					!force_first_car_gripped || c.global_start + i != 0;
				car_views[i].handle_machine_turn_and_strafe_points4(
					initial_angle_vel_y, allow_drift);
			} else {
				car_views[i].update_effective_machine_stats(true);
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
			if (!vehicle_motion_active(c, i)) {
				continue;
			}
			car_views[i].handle_linear_velocity();
			car_views[i].handle_angle_velocity();
			car_views[i].handle_airborne_controls();
			car_views[i].orient_vehicle_from_gravity_or_road();
			car_views[i].handle_drag_and_glide_forces();
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

			if (c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) {
				c.machine_state[i] &= ~(MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL);
				SimVec3 road_normal = normalized_or_zero(LOAD_INDEXED_VEC3(c, track_surface_normal, i));
				if (road_normal.length_squared() <= 0.000001f) {
					road_normal = normalized_or_zero(c.road_sample[i].closest_surface.basis.get_column(1));
				}
				if (road_normal.length_squared() <= 0.000001f) {
					road_normal = normalized_or_zero(basis.basis.get_column(1));
				}
				if (road_normal.length_squared() > 0.000001f) {
					const SimVec3 anchor = LOAD_INDEXED_VEC3(c, initial_pos, i);
					const SimVec3 current = LOAD_INDEXED_VEC3(c, position_current, i);
					const SimVec3 tangent_correction = remove_axis_component(current - anchor, road_normal);
					if (tangent_correction.length_squared() > 0.0000001f) {
						STORE_INDEXED_VEC3(c, position_current, i, current - tangent_correction);
						STORE_INDEXED_VEC3(c, position_old, i, LOAD_INDEXED_VEC3(c, position_old, i) - tangent_correction);
						STORE_INDEXED_VEC3(c, position_old_dupe, i, LOAD_INDEXED_VEC3(c, position_old_dupe, i) - tangent_correction);
						STORE_INDEXED_VEC3(c, position_bottom, i, LOAD_INDEXED_VEC3(c, position_bottom, i) - tangent_correction);
						translate_contact_points_soa(c, i, -tangent_correction);
					}
					STORE_INDEXED_VEC3(c, velocity, i, keep_axis_component(LOAD_INDEXED_VEC3(c, velocity, i), road_normal));
					STORE_INDEXED_VEC3(c, knockback_velocity, i, keep_axis_component(LOAD_INDEXED_VEC3(c, knockback_velocity, i), road_normal));
				}
			}

			if ((c.machine_state[i] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
				c.position_bottom_x[i] += c.position_current_x[i] - c.position_old_x[i];
				c.position_bottom_y[i] += c.position_current_y[i] - c.position_old_y[i];
				c.position_bottom_z[i] += c.position_current_z[i] - c.position_old_z[i];
			}
		}
	}

	static void finish_vehicle_motion_phased_soa(PhysicsCarSoA& c, PhysicsCar* car_views,
		int count, bool force_first_car_gripped)
	{
		project_vehicle_velocity_phase(c, count);
		steering_and_suspension_phase(c, car_views, count, force_first_car_gripped);
		linear_orientation_drag_phase(c, car_views, count);
		integrate_vehicle_positions_phase(c, count);
		rotate_and_finish_motion_phase(c, car_views, count);
	}

	static inline bool intervals_overlap(float min_a, float max_a, float min_b, float max_b)
	{
		return min_a <= max_b && min_b <= max_a;
	}

	static inline bool vehicle_is_restoring(const PhysicsCar& car)
	{
		return car.soa->restore_state[car.soa_index] != 0;
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
		constexpr uint32_t kCollisionSfxCooldownFrames = 15;
		auto is_recent_machine_hit = [&](PhysicsCarSoA& c, int lane, uint32_t cooldown_frames) -> bool {
			if (!c.has_last_machine_hit_tick[lane]) {
				return false;
			}
			const uint32_t delta = current_tick - c.last_machine_hit_tick[lane];
			return delta < cooldown_frames;
		};

		const bool recently_hit =
			is_recent_machine_hit(car_a, lane_a, kCollisionSparkCooldownFrames) ||
			is_recent_machine_hit(car_b, lane_b, kCollisionSparkCooldownFrames);
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
		const SimVec3 delta_a = LOAD_INDEXED_VEC3(car_a, position_current, lane_a) -
			LOAD_INDEXED_VEC3(car_a, position_old_dupe, lane_a);
		const SimVec3 delta_b = LOAD_INDEXED_VEC3(car_b, position_current, lane_b) -
			LOAD_INDEXED_VEC3(car_b, position_old_dupe, lane_b);
		const float relative_motion_sq = (delta_b - delta_a).length_squared();
		constexpr float gx_machine_collision_strength_scale = 0.01f;
		constexpr float gx_machine_collision_strength_min = 0.1f;
		constexpr float gx_machine_collision_strength_max = 1.0f;
		const float sfx_strength = std::min(gx_machine_collision_strength_max,
			std::max(gx_machine_collision_strength_min,
				relative_motion_sq * gx_machine_collision_strength_scale));
		if (!is_recent_machine_hit(car_a, lane_a, kCollisionSfxCooldownFrames)) {
			car_a.last_machine_hit_tick[lane_a] = current_tick;
			car_a.last_machine_hit_sfx_strength[lane_a] = sfx_strength;
			car_a.has_last_machine_hit_tick[lane_a] = true;
		} else if (!is_recent_machine_hit(car_b, lane_b, kCollisionSfxCooldownFrames)) {
			car_b.last_machine_hit_tick[lane_b] = current_tick;
			car_b.last_machine_hit_sfx_strength[lane_b] = sfx_strength;
			car_b.has_last_machine_hit_tick[lane_b] = true;
		}
	}

	static void collide_vehicles_broadphase(GameSim& sim, PhysicsCar* car_views, int count,
		int* indices, int& sorted_count, float* min_x, float* max_x, float* min_y, float* max_y, float* min_z, float* max_z)
	{
		constexpr float kMachineCollisionRadius = 2.0f;
		constexpr float kMutationSlop = 8.0f;

		if (sorted_count != count) {
			for (int i = 0; i < count; ++i) {
				indices[i] = i;
			}
			sorted_count = count;
		}

		for (int i = 0; i < count; ++i) {
			PhysicsCarSoA& c = *car_views[i].soa;
			const int lane = car_views[i].soa_index;
			c.position_collision_snapshot_x[lane] = c.position_current_x[lane];
			c.position_collision_snapshot_y[lane] = c.position_current_y[lane];
			c.position_collision_snapshot_z[lane] = c.position_current_z[lane];
			if (c.restore_state[lane] != 0 || c.s_boost_active[lane] || (c.state_2[lane] & 0x10u) != 0u) {
				min_x[i] = FLT_MAX;
				max_x[i] = -FLT_MAX;
				min_y[i] = FLT_MAX;
				max_y[i] = -FLT_MAX;
				min_z[i] = FLT_MAX;
				max_z[i] = -FLT_MAX;
				continue;
			}
			const float radius = kMachineCollisionRadius;
			const float extent = radius + c.speed_kmh[lane] / 216.0f + kMutationSlop;
			min_x[i] = c.position_current_x[lane] - extent;
			max_x[i] = c.position_current_x[lane] + extent;
			min_y[i] = c.position_current_y[lane] - extent;
			max_y[i] = c.position_current_y[lane] + extent;
			min_z[i] = c.position_current_z[lane] - extent;
			max_z[i] = c.position_current_z[lane] + extent;
		}

		auto less_for_collision_sweep = [&](int a, int b) {
			if (min_x[a] != min_x[b]) {
				return min_x[a] < min_x[b];
			}
			return a < b;
		};
		for (int i = 1; i < count; ++i) {
			const int key = indices[i];
			int j = i;
			while (j > 0 && less_for_collision_sweep(key, indices[j - 1])) {
				indices[j] = indices[j - 1];
				--j;
			}
			indices[j] = key;
		}

		for (int sorted_i = 0; sorted_i < count; ++sorted_i) {
			const int i = indices[sorted_i];
			if (min_x[i] == FLT_MAX) {
				break;
			}
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

void GameSim::update_bumper_vehicles()
{
	if (!bumpers_enabled || bumper_count <= 0 || !bumper_cars) {
		return;
	}
	VehicleTickSoA& soa = vehicle_tick_soa;
	PhysicsCarSoA& first_shard = *bumper_cars[0].soa;
	PhysicsCarSoA* bumper_shards = first_shard.shards ? first_shard.shards : &first_shard;
	const int bumper_shard_count = first_shard.shards ? first_shard.shard_count : 1;
	const int sim_lane_count = first_shard.total_lane_count > 0 ? first_shard.total_lane_count : first_shard.lane_count;
	const bool parallel_vehicle_shards = bumper_count >= 16 && bumper_shard_count == VEHICLE_WORKER_COUNT;
	const bool track_has_triggers = vehicles_may_emit_trigger_events(bumper_cars, bumper_count);
	ensure_vehicle_tick_soa_capacity(sim_lane_count);
	for (int i = 0; i < bumper_count; ++i) {
		soa.inputs[i] = generate_bumper_input_for_slot(i);
	}
	for (int i = bumper_count; i < sim_lane_count; ++i) {
		soa.inputs[i] = PlayerInput::from_neutral();
		soa.pending_s_boost_sparks[i] = 0;
	}
	struct BumperVehicleLaneContext {
		GameSim* sim;
		VehicleTickSoA* tick_soa;
		PhysicsCarSoA* bumper_shards;
		int bumper_shard_count;
		bool track_has_triggers;
	};
	BumperVehicleLaneContext lane_context{this, &soa, bumper_shards, bumper_shard_count, track_has_triggers};
	run_vehicle_lanes(bumper_shard_count, parallel_vehicle_shards, &lane_context, [](void* raw_context, int lane, VehicleLaneGroup& group) {
		BumperVehicleLaneContext& context = *static_cast<BumperVehicleLaneContext*>(raw_context);
		GameSim& sim = *context.sim;
		VehicleTickSoA& soa = *context.tick_soa;
		PhysicsCarSoA& car_soa = context.bumper_shards[lane];
		const int global_start = car_soa.global_start;
		TrackQueryScratch &track_scratch = sim.vehicle_lane_track_scratch[lane];
		track_scratch.reset_mesh_query();

		begin_vehicle_tick_soa(car_soa, sim.bumper_cars + global_start,
			soa.inputs + global_start, static_cast<uint32_t>(sim.tick), car_soa.count,
			false, false);

		apply_vehicle_motion_inputs_soa(car_soa, soa.inputs + global_start, car_soa.count);
		prepare_vehicle_floor_phase(car_soa, sim.bumper_cars + global_start, car_soa.count, track_scratch);

		if (context.track_has_triggers) {
			group.sync();
			if (lane == 0) {
				commit_vehicle_trigger_events(context.bumper_shards, sim.bumper_cars, context.bumper_shard_count, sim.vehicle_lane_track_scratch);
			}
			group.sync();
		}

		finish_vehicle_motion_phased_soa(
			car_soa, sim.bumper_cars + global_start, car_soa.count, false);

		group.sync();

		update_machine_corners_soa(car_soa, sim.bumper_cars + global_start, car_soa.count, track_scratch);
		finish_vehicle_tail_soa(car_soa, sim.bumper_cars + global_start, car_soa.count);
		handle_vehicle_checkpoints_soa(car_soa, sim.bumper_cars + global_start, car_soa.count, track_scratch);
		collect_pending_s_boost_sparks_soa(car_soa,
			soa.pending_s_boost_sparks + global_start, car_soa.count, false);
	});
}

void GameSim::collide_racers_with_bumpers()
{
	if (!bumpers_enabled || bumper_count <= 0 || !bumper_cars || !cars) {
		return;
	}
	for (int slot = 0; slot < bumper_count; ++slot) {
		if (!bumper_states[slot].active) {
			continue;
		}
		PhysicsCar& bumper = bumper_cars[slot];
		if (vehicle_is_restoring(bumper)) {
			continue;
		}
		PhysicsCarSoA& bumper_soa = *bumper.soa;
		const int bumper_lane = bumper.soa_index;
		bumper_soa.position_collision_snapshot_x[bumper_lane] = bumper_soa.position_current_x[bumper_lane];
		bumper_soa.position_collision_snapshot_y[bumper_lane] = bumper_soa.position_current_y[bumper_lane];
		bumper_soa.position_collision_snapshot_z[bumper_lane] = bumper_soa.position_current_z[bumper_lane];
		for (int racer_index = 0; racer_index < num_cars; ++racer_index) {
			if (vehicle_is_restoring(cars[racer_index])) {
				continue;
			}
			cars[racer_index].handle_machine_v_bumper_collision(bumper);
		}
	}
}

PlayerInput GameSim::generate_bumper_input_for_slot(int bumper_slot) const
{
	PlayerInput input = PlayerInput::from_neutral();
	if (!bumper_cars || bumper_slot < 0 || bumper_slot >= bumper_count || !bumper_states[bumper_slot].active) {
		return input;
	}
	const float target_lane = bumper_states[bumper_slot].target_lane;
	const PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	input.accelerate = 1.0f;
	input.boost = false;
	const SimBasis physical_basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis;
	SimBasis surface = soa.road_sample[lane].closest_surface.basis;
	float road_tx = soa.road_sample[lane].road_t.x;
	if (soa.current_track[lane]) {
		int sample_cp = soa.current_collision_checkpoint[lane];
		if (sample_cp < 0 || sample_cp >= soa.current_track[lane]->num_checkpoints) {
			sample_cp = soa.current_checkpoint[lane];
		}
		if (sample_cp >= 0 && sample_cp < soa.current_track[lane]->num_checkpoints) {
			const CollisionCheckpoint& cp = soa.current_track[lane]->checkpoints[sample_cp];
			const SimVec3 pos = LOAD_INDEXED_VEC3(soa, position_current, lane);
			const float cp_t = checkpoint_fraction_for_cpu_guidance(cp, pos);
			surface[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			surface[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);
			surface[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			const SimVec3 center = cp.position_start.lerp(cp.position_end, cp_t);
			const float x_radius_inv = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			road_tx = (pos - center).dot(surface[0]) * x_radius_inv;
		}
	}
	if (road_tx == -1000.0f || !std::isfinite(road_tx)) {
		road_tx = 0.0f;
	}
	const float bumper_weight = std::max(soa.stat_weight[lane], 0.001f);
	const SimVec3 velocity_world = LOAD_INDEXED_VEC3(soa, velocity, lane) / bumper_weight;
	const float lateral_velocity = velocity_world.dot(surface.c0);
	const float lane_error = std::clamp(road_tx - target_lane, -1.0f, 1.0f);
	float desired_strafe = 0.0f;
	if (std::abs(lane_error) > 0.025f) {
		desired_strafe = lane_error * 1.35f + lateral_velocity * 0.04f;
		desired_strafe = std::clamp(desired_strafe, -0.55f, 0.55f);
	}
	input.strafe_left = std::clamp(-desired_strafe, 0.0f, 1.0f);
	input.strafe_right = std::clamp(desired_strafe, 0.0f, 1.0f);
	const float desired_steer = (physical_basis.c0 + surface.c0).dot(surface.c2);
	input.steer_horizontal = std::clamp(desired_steer * 18.0f, -1.0f, 1.0f);
	if (soa.speed_kmh[lane] > 850.0f) {
		input.brake = std::clamp((soa.speed_kmh[lane] - 850.0f) / 160.0f, 0.0f, 1.0f);
		input.accelerate = 0.0f;
	}
	return input;
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
	vehicle_tick_soa.placement_order_valid = false;
	if (vehicle_tick_soa.pending_s_boost_sparks) {
		free_cache_aligned(vehicle_tick_soa.pending_s_boost_sparks);
		vehicle_tick_soa.pending_s_boost_sparks = nullptr;
	}
	if (vehicle_tick_soa.collision_indices) {
		free_cache_aligned(vehicle_tick_soa.collision_indices);
		vehicle_tick_soa.collision_indices = nullptr;
	}
	vehicle_tick_soa.collision_order_count = 0;
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

void GameSim::process_pending_ko_events()
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return;
	}
	for (int victim_index = 0; victim_index < num_cars; ++victim_index) {
		PhysicsCarSoA& victim_soa = *cars[victim_index].soa;
		const int victim_lane = cars[victim_index].soa_index;
		const int attacker_index = victim_soa.pending_ko_attacker_car_index[victim_lane];
		if (attacker_index < 0 || attacker_index >= num_cars || attacker_index == victim_index) {
			victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
			continue;
		}

		PhysicsCarSoA& attacker_soa = *cars[attacker_index].soa;
		const int attacker_lane = cars[attacker_index].soa_index;
		const float boost_cost = std::max(1.0f,
			10.0f * attacker_soa.stat_manual_boost_duration_seconds[attacker_lane] * attacker_soa.boost_energy_use_mult[attacker_lane]);
		const float energy_gain = boost_cost * 0.6666666667f;
		if (car_player_ids[attacker_index] >= 0) {
			attacker_soa.ko_energy_bonus[attacker_lane] += energy_gain;
			if (attacker_soa.car_properties[attacker_lane]) {
				attacker_soa.calced_max_energy[attacker_lane] =
					attacker_soa.car_properties[attacker_lane]->base_stats[CAR_STAT_MAX_ENERGY] + attacker_soa.ko_energy_bonus[attacker_lane];
			}
			attacker_soa.energy[attacker_lane] = attacker_soa.calced_max_energy[attacker_lane];
			attacker_soa.machine_state[attacker_lane] &= ~(MACHINESTATE::ZEROHP |
				MACHINESTATE::FALLOUT |
				MACHINESTATE::TOOKDAMAGE |
				MACHINESTATE::LOWGRIP);
			attacker_soa.breakdown_frame_counter[attacker_lane] = 0;
			attacker_soa.some_breakdown_int[attacker_lane] = 0;
			attacker_soa.frames_since_death[attacker_lane] = 0;
			attacker_soa.machine_crashed[attacker_lane] = false;
			attacker_soa.state_2[attacker_lane] &= ~(0x2u | 0x20u | 0x80u | 0x100u);
			STORE_INDEXED_VEC3(attacker_soa, visual_rotation, attacker_lane, SimVec3());
			STORE_INDEXED_VEC3(attacker_soa, unk_vec3_0x4e4, attacker_lane, SimVec3());
			STORE_INDEXED_VEC3(attacker_soa, unk_vec3_0x4f0, attacker_lane, SimVec3());
		}

		RaceEvent event;
		event.type = 1;
		event.actor_id = car_player_ids[attacker_index];
		event.target_id = car_player_ids[victim_index];
		event.tick = tick;
		event.value = static_cast<int32_t>(std::lround(energy_gain));
		race_events.push_back(event);
		victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
	}
	if (!bumpers_enabled || !bumper_cars || bumper_count <= 0) {
		return;
	}
	for (int slot = 0; slot < bumper_count; ++slot) {
		if (!bumper_states[slot].active) {
			continue;
		}
		PhysicsCarSoA& victim_soa = *bumper_cars[slot].soa;
		const int victim_lane = bumper_cars[slot].soa_index;
		const int attacker_index = victim_soa.pending_ko_attacker_car_index[victim_lane];
		if (attacker_index < 0 || attacker_index >= num_cars) {
			victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
			continue;
		}
		PhysicsCarSoA& attacker_soa = *cars[attacker_index].soa;
		const int attacker_lane = cars[attacker_index].soa_index;
		const float boost_cost = std::max(1.0f,
			10.0f * attacker_soa.stat_manual_boost_duration_seconds[attacker_lane] * attacker_soa.boost_energy_use_mult[attacker_lane]);
		const float energy_gain = boost_cost * 0.75f;
		if (car_player_ids[attacker_index] >= 0) {
			attacker_soa.ko_energy_bonus[attacker_lane] += energy_gain;
			if (attacker_soa.car_properties[attacker_lane]) {
				attacker_soa.calced_max_energy[attacker_lane] =
					attacker_soa.car_properties[attacker_lane]->base_stats[CAR_STAT_MAX_ENERGY] + attacker_soa.ko_energy_bonus[attacker_lane];
			}
			attacker_soa.energy[attacker_lane] = std::min(
				attacker_soa.energy[attacker_lane] + energy_gain,
				attacker_soa.calced_max_energy[attacker_lane]);
		}
		RaceEvent event;
		event.type = 1;
		event.actor_id = car_player_ids[attacker_index];
		event.target_id = -1;
		event.tick = tick;
		event.value = static_cast<int32_t>(std::lround(energy_gain));
		race_events.push_back(event);
		victim_soa.pending_ko_attacker_car_index[victim_lane] = -1;
		deactivate_bumper_car(slot);
	}
}

void GameSim::tick_singleplayer(int local_player_id, godot::PackedByteArray local_input)
{
	const PlayerInput decoded_local_input = PlayerInput::from_bytes(local_input);
	tick_gamesim_internal(InputFrameMode::SingleLocal, local_player_id, &decoded_local_input, nullptr, nullptr, 0);
}

bool GameSim::tick_singleplayer_indexed_input(int local_player_id,
	const godot::PackedByteArray& input_bytes,
	const godot::PackedInt32Array& frame_offsets,
	int frame_index)
{
	if (frame_index < 0 || frame_index + 1 >= frame_offsets.size()) {
		return false;
	}
	const int begin = frame_offsets[frame_index];
	const int end = frame_offsets[frame_index + 1];
	if (begin < 0 || end <= begin || end > input_bytes.size()) {
		return false;
	}
	const uint8_t* data = input_bytes.ptr() + begin;
	const int size = end - begin;
	if (PlayerInput::encoded_raw_size_from_mask(data[0]) != size) {
		return false;
	}
	const PlayerInput decoded_local_input = PlayerInput::from_raw(data, size);
	tick_gamesim_internal(InputFrameMode::SingleLocal, local_player_id, &decoded_local_input, nullptr, nullptr, 0);
	return true;
}

void GameSim::tick_gamesim_internal(InputFrameMode mode,
	int local_player_id,
	const PlayerInput* local_input,
	const PlayerInput* decoded_car_inputs,
	const uint8_t* decoded_car_input_present,
	int decoded_car_input_count,
	PlayerInput* out_authoritative_inputs,
	uint8_t* out_authoritative_present,
	bool store_input_history)
{
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	std::fesetround(FE_TONEAREST);
	const bool profile_phase = phase_profile_enabled;
	const uint64_t phase_start = profile_phase ? render_profile_now_us() : 0;
	uint64_t phase_step = phase_start;
	auto phase_mark = [&](uint64_t& bucket, uint64_t& max_bucket) -> uint64_t {
		if (!profile_phase) {
			return 0;
		}
		const uint64_t now = render_profile_now_us();
		const uint64_t elapsed = now - phase_step;
		bucket += elapsed;
		max_bucket = std::max(max_bucket, elapsed);
		phase_step = now;
		return elapsed;
	};
	phase_profile_last_vehicle_collision_us = 0;

	if (num_cars <= 0 || !cars)
	{
		if (!performance_benchmark_mode) {
			save_state();
		}
		tick += 1;
		return;
	}

	VehicleTickSoA& soa = vehicle_tick_soa;
	PhysicsCarSoA& first_shard = *cars[0].soa;
	PhysicsCarSoA* car_shards = first_shard.shards ? first_shard.shards : &first_shard;
	const int car_shard_count = first_shard.shards ? first_shard.shard_count : 1;
	const int sim_lane_count = first_shard.total_lane_count > 0 ? first_shard.total_lane_count : first_shard.lane_count;
	const bool parallel_vehicle_shards = num_cars >= 16 && car_shard_count == VEHICLE_WORKER_COUNT;
	const bool track_has_triggers = vehicles_may_emit_trigger_events(cars, num_cars);
	ensure_vehicle_tick_soa_capacity(sim_lane_count);
	int buf_index = tick % INPUT_BUFFER_LEN;
	PlayerInput* slot = store_input_history ? input_buffer + buf_index * num_cars : nullptr;
	const float lap_length_for_distance = gamesim_track_lap_length(current_track);

	float lead_distance = 0.0f;
	int leader_lap = 0;
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const float distance = gamesim_vehicle_stored_distance(car_soa, lane, lap_length_for_distance);
		soa.pre_distances[i] = distance;
		if (distance > lead_distance) {
			lead_distance = distance;
			leader_lap = static_cast<int>(car_soa.lap[lane]);
		}
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		car_soa.dashplate_heat_reward_scale[lane] =
			gamesim_dashplate_heat_reward_scale(lead_distance - soa.pre_distances[i]);
	}
	update_bumpers(lead_distance, leader_lap);
	phase_profile_last_pre_us = phase_mark(phase_profile_pre_us, phase_profile_pre_max_us);

	const NativeCpuTickContext cpu_tick_context = native_cpu_make_tick_context(tick);
	for (int i = 0; i < num_cars; i++) {
		PlayerInput inp = PlayerInput::from_neutral();
		bool input_already_quantized = false;
		const int32_t player_id = car_player_ids ? car_player_ids[i] : -1;
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const bool completed_race = (car_soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u;
		if ((mode == InputFrameMode::DecodedCarArray || mode == InputFrameMode::DecodedQuantizedCarArray) &&
				i < decoded_car_input_count && decoded_car_inputs &&
				(!decoded_car_input_present || decoded_car_input_present[i])) {
			inp = decoded_car_inputs[i];
			input_already_quantized = mode == InputFrameMode::DecodedQuantizedCarArray;
		} else if (completed_race && player_id != -1) {
			inp = native_cpu_generate_quantized_input_for_car(cars[i], player_id, cpu_tick_context, spawn_seed);
			input_already_quantized = true;
		} else if (mode == InputFrameMode::SingleLocal && player_id == local_player_id && local_input) {
			inp = *local_input;
			input_already_quantized = true;
		} else if (car_is_cpu && car_is_cpu[i]) {
			inp = native_cpu_generate_quantized_input_for_car(cars[i], player_id, cpu_tick_context, spawn_seed);
			input_already_quantized = true;
		}
		if (!input_already_quantized) {
			inp = PlayerInput::quantized(inp);
		}
		if (out_authoritative_inputs && i < decoded_car_input_count) {
			out_authoritative_inputs[i] = inp;
			if (out_authoritative_present) {
				out_authoritative_present[i] = 1;
			}
		}
		if (s_boost_enabled && !car_soa.s_boost_active[lane] && car_soa.s_boost_charge[lane] >= car_soa.s_boost_charge_max[lane] && inp.boost) {
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
			car_soa.boost_frames_manual[lane] = 0;
			car_soa.boost_frames_dash[lane] = 0;
			car_soa.boost_duration_manual_frames[lane] = 0;
			car_soa.boost_duration_dash_frames[lane] = 0;
			car_soa.boost_turbo[lane] = 0.0f;
			car_soa.pending_dashplate_heat[lane] = 0.0f;
			car_soa.pending_dashplate_heat_reward_scale[lane] = 1.0f;
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
		if (slot) {
			slot[i] = inp;
		}
	}
	for (int i = num_cars; i < sim_lane_count; ++i) {
		soa.inputs[i] = PlayerInput::from_neutral();
		soa.pending_s_boost_sparks[i] = 0;
	}
	phase_profile_last_input_us = phase_mark(phase_profile_input_us, phase_profile_input_max_us);

	struct RacerVehicleLaneContext {
		GameSim* sim;
		VehicleTickSoA* tick_soa;
		PhysicsCarSoA* car_shards;
		int car_shard_count;
		bool track_has_triggers;
		bool profile_phase;
	};
	RacerVehicleLaneContext lane_context{this, &soa, car_shards, car_shard_count, track_has_triggers, profile_phase};
	run_vehicle_lanes(car_shard_count, parallel_vehicle_shards, &lane_context, [](void* raw_context, int lane, VehicleLaneGroup& group) {
		RacerVehicleLaneContext& context = *static_cast<RacerVehicleLaneContext*>(raw_context);
		GameSim& sim = *context.sim;
		VehicleTickSoA& soa = *context.tick_soa;
		PhysicsCarSoA& car_soa = context.car_shards[lane];
		const int global_start = car_soa.global_start;
		TrackQueryScratch &track_scratch = sim.vehicle_lane_track_scratch[lane];
		track_scratch.reset_mesh_query();
		const bool profile_phase = context.profile_phase;
		uint64_t vehicle_subphase_step = profile_phase && lane == 0 ? sim.render_profile_now_us() : 0;
		auto vehicle_subphase_mark = [&](uint64_t& bucket) -> uint64_t {
			if (!profile_phase || lane != 0) {
				return 0;
			}
			const uint64_t now = sim.render_profile_now_us();
			const uint64_t elapsed = now - vehicle_subphase_step;
			bucket += elapsed;
			vehicle_subphase_step = now;
			return elapsed;
		};

		begin_vehicle_tick_soa(car_soa, sim.cars + global_start,
			soa.inputs + global_start, static_cast<uint32_t>(sim.tick), car_soa.count,
			sim.vehicle_restore_enabled, sim.s_boost_enabled);
		vehicle_subphase_mark(sim.phase_profile_vehicle_begin_us);

		apply_vehicle_motion_inputs_soa(car_soa, soa.inputs + global_start, car_soa.count);
		vehicle_subphase_mark(sim.phase_profile_vehicle_apply_input_us);

		PhysicsCarFloorProfile floor_profile;
		if (profile_phase && lane == 0) {
			floor_profile.corner_analytic_surface_us = &sim.phase_profile_vehicle_floor_corner_analytic_surface_us;
			floor_profile.mesh_candidate_collect_us = &sim.phase_profile_vehicle_floor_mesh_candidate_collect_us;
			floor_profile.mesh_cast4_us = &sim.phase_profile_vehicle_floor_mesh_cast4_us;
			floor_profile.mesh_floor_sample_us = &sim.phase_profile_vehicle_floor_mesh_sample_us;
			floor_profile.find_floor_cast_us = &sim.phase_profile_vehicle_find_floor_cast_us;
			floor_profile.find_floor_mesh_us = &sim.phase_profile_vehicle_find_floor_mesh_us;
			floor_profile.find_floor_analytic_us = &sim.phase_profile_vehicle_find_floor_analytic_us;
		}
		prepare_vehicle_floor_phase(car_soa, sim.cars + global_start, car_soa.count, track_scratch,
			profile_phase && lane == 0 ? &sim.phase_profile_vehicle_prepare_frame_us : nullptr,
			profile_phase && lane == 0 ? &floor_profile : nullptr,
			profile_phase && lane == 0 ? &sim.phase_profile_vehicle_find_floor_us : nullptr,
			profile_phase && lane == 0 ? &sim.phase_profile_vehicle_terrain_us : nullptr);
		if (context.track_has_triggers) {
			group.sync();
		}
		vehicle_subphase_mark(sim.phase_profile_vehicle_floor_us);

		if (context.track_has_triggers) {
			if (lane == 0) {
				commit_vehicle_trigger_events(context.car_shards, sim.cars, context.car_shard_count, sim.vehicle_lane_track_scratch);
			}
			group.sync();
		}
		vehicle_subphase_mark(sim.phase_profile_vehicle_trigger_us);

		finish_vehicle_motion_phased_soa(car_soa, sim.cars + global_start,
			car_soa.count, sim.performance_benchmark_mode);
		vehicle_subphase_mark(sim.phase_profile_vehicle_motion_us);

		group.sync();
		vehicle_subphase_mark(sim.phase_profile_vehicle_finish_tick_us);

		if (lane == 0) {
			collide_vehicles_broadphase(sim, sim.cars, sim.num_cars,
				soa.collision_indices, soa.collision_order_count,
				soa.collision_min_x, soa.collision_max_x,
				soa.collision_min_y, soa.collision_max_y,
				soa.collision_min_z, soa.collision_max_z);
		}
		group.sync();
		const uint64_t collision_elapsed = vehicle_subphase_mark(sim.phase_profile_vehicle_collision_us);
		if (profile_phase && lane == 0) {
			sim.phase_profile_last_vehicle_collision_us = collision_elapsed;
		}

		const uint64_t post_tick_start = vehicle_subphase_step;
		PhysicsCarCornerProfile corner_profile;
		if (profile_phase && lane == 0) {
			corner_profile.old_analytic_us = &sim.phase_profile_vehicle_corner_old_analytic_us;
			corner_profile.new_checkpoint_us = &sim.phase_profile_vehicle_corner_new_checkpoint_us;
			corner_profile.new_analytic_us = &sim.phase_profile_vehicle_corner_new_analytic_us;
			corner_profile.mesh_us = &sim.phase_profile_vehicle_corner_mesh_us;
		}
		update_machine_corners_soa(car_soa, sim.cars + global_start, car_soa.count, track_scratch,
			profile_phase && lane == 0 ? &corner_profile : nullptr);
		vehicle_subphase_mark(sim.phase_profile_vehicle_corner_update_us);
		finish_vehicle_tail_soa(car_soa, sim.cars + global_start, car_soa.count);
		vehicle_subphase_mark(sim.phase_profile_vehicle_tail_us);
		handle_vehicle_checkpoints_soa(car_soa, sim.cars + global_start, car_soa.count, track_scratch);
		vehicle_subphase_mark(sim.phase_profile_vehicle_checkpoint_us);
		collect_pending_s_boost_sparks_soa(car_soa,
			soa.pending_s_boost_sparks + global_start, car_soa.count, sim.s_boost_enabled);
		vehicle_subphase_mark(sim.phase_profile_vehicle_spark_collect_us);
		if (profile_phase && lane == 0) {
			sim.phase_profile_vehicle_post_tick_us += vehicle_subphase_step - post_tick_start;
		}
	});
	phase_profile_last_vehicle_us = phase_mark(phase_profile_vehicle_us, phase_profile_vehicle_max_us);
	for (int i = 0; i < num_cars; i++) {
		if (s_boost_enabled && soa.pending_s_boost_sparks[i] > 0) {
			emit_super_sparks_from_car(cars[i], soa.pending_s_boost_sparks[i]);
		}
	}
	update_bumper_vehicles();
	collide_racers_with_bumpers();
	phase_profile_last_post_vehicle_us = phase_mark(phase_profile_post_vehicle_us, phase_profile_post_vehicle_max_us);

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		soa.placement_distances[i] = gamesim_vehicle_stored_distance(car_soa, lane, lap_length_for_distance);
		soa.placement_indices[i] = i;
	}
	std::sort(soa.placement_indices, soa.placement_indices + num_cars, [&](int a, int b) {
		return soa.placement_distances[a] > soa.placement_distances[b];
	});
	soa.placement_order_valid = true;
	phase_profile_last_placement_us = phase_mark(phase_profile_placement_us, phase_profile_placement_max_us);

	if (s_boost_enabled && super_spark_state) {
		super_spark_state->placement_timer += 1;
		while (super_spark_state->placement_timer >= 120) {
			int top_racer_indices[3] = { -1, -1, -1 };
			int top_racer_count = 0;
			for (int i = 0; i < num_cars && top_racer_count < 3; ++i) {
				const int car_index = soa.placement_indices[i];
				if (car_index >= 0 && car_index < num_cars && car_player_ids && car_player_ids[car_index] >= 0) {
					top_racer_indices[top_racer_count++] = car_index;
				}
			}
			if (top_racer_count > 0) {
				emit_super_sparks_from_car(cars[top_racer_indices[0]], 4);
			}
			if (top_racer_count > 1) {
				emit_super_sparks_from_car(cars[top_racer_indices[1]], 3);
			}
			if (top_racer_count > 2) {
				emit_super_sparks_from_car(cars[top_racer_indices[2]], 2);
			}
			super_spark_state->placement_timer -= 120;
		}
	}

	process_pending_ko_events();
	if (s_boost_enabled) {
		update_super_sparks();
	}
	phase_profile_last_post_us = phase_mark(phase_profile_post_us, phase_profile_post_max_us);

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
	if (!performance_benchmark_mode) {
		save_state();
	}
	phase_profile_last_save_us = phase_mark(phase_profile_save_us, phase_profile_save_max_us);
	if (profile_phase) {
		const uint64_t now = render_profile_now_us();
		const uint64_t elapsed = now - phase_start;
		phase_profile_total_us += elapsed;
		phase_profile_total_max_us = std::max(phase_profile_total_max_us, elapsed);
		phase_profile_last_total_us = elapsed;
		phase_profile_frames += 1;
	}
	
	
	tick += 1;
	//dd2d->call("set_text", "pos 1", car_positions[0]);
	
	//dd3d->call("draw_points", car_positions, 0, 1.0f, godot::Color(1.f, 0.f, 0.f), 0.0166666);
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

void GameSim::save_state()
{
	int index = tick % STATE_BUFFER_LEN;
	int size = gamestate_data.get_size();
	state_buffer[index].size = size;
	state_buffer[index].tick = tick;
	uint64_t profile_step = phase_profile_enabled ? render_profile_now_us() : 0;
	save_bumper_states_to_saved_state(state_buffer[index]);
	profile_mark(phase_profile_enabled ? &phase_profile_save_bumper_us : nullptr, profile_step);
	update_saved_voice_transforms(state_buffer[index]);
	profile_mark(phase_profile_enabled ? &phase_profile_save_voice_us : nullptr, profile_step);
	if (state_buffer[index].data)
	{
		memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
	}
	profile_mark(phase_profile_enabled ? &phase_profile_save_memcpy_us : nullptr, profile_step);
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
	restore_bumper_states_from_saved_state(state_buffer[index]);
	if (state_buffer[index].vehicle_local_state.empty() ||
			!restore_vehicle_local_state_from_saved_state(state_buffer[index])) {
		rebuild_road_samples_after_state_load();
	}
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
