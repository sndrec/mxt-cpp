#pragma once

#include "car/car_points.h"
#include "car/car_properties.h"
#include "core/curve.h"
#include "core/player_input.h"
#include "track/racetrack.h"
#include "core/math_utils.h"
#include "core/enums.h"
#include "core/sim_math.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/variant/packed_vector3_array.hpp"

static constexpr int MXT_VEHICLE_SHARD_COUNT = 4;

namespace godot {
	class GameSim;
}

struct RoadData {
	uint16_t terrain; // terrain of whatever we're sampling
	int16_t cp_idx; // checkpoint our collision belongs to
	SimVec3 spatial_t; // local coordinate space of the point that is being tested at the road's sampled point
	SimVec2 road_t; // span along the width and length of the road at the sampled point (goes from (-1, 0) to (1, 1))
	SimTransform closest_surface; // oriented transform representing the road's surface position and orientation at the sampled point
	RoadTransform closest_root; // oriented transform representing the road's overall center and orientation at the sampled length along the road
};

struct CollisionData {
	bool collided; // did we collide?
	SimVec3 collision_point; // position of collision
	SimVec3 collision_normal; // surface normal at collision
	SimVec3 collision_face_point; // unsmoothed mesh face point for hard depenetration
	SimVec3 collision_face_normal; // unsmoothed mesh face normal for hard depenetration
	RoadData road_data;
	int32_t mesh_triangle_index = -1;
};

struct ImpactData {
	SimVec3 relative_dir_local;
	SimVec3 relative_dir_world;
	float impact_axis_z;
	float speed_per_mass;
};

struct PhysicsCarCornerProfile {
	uint64_t* old_analytic_us = nullptr;
	uint64_t* new_checkpoint_us = nullptr;
	uint64_t* new_analytic_us = nullptr;
	uint64_t* mesh_us = nullptr;
};

struct PhysicsCarFloorProfile {
	uint64_t* corner_analytic_surface_us = nullptr;
	uint64_t* mesh_candidate_collect_us = nullptr;
	uint64_t* mesh_cast4_us = nullptr;
	uint64_t* mesh_floor_sample_us = nullptr;
	uint64_t* find_floor_cast_us = nullptr;
	uint64_t* find_floor_mesh_us = nullptr;
	uint64_t* find_floor_analytic_us = nullptr;
};

#define PHYSICS_CAR_STATIC_SCALAR_FIELDS(X) \
	X(RaceTrack*, current_track, nullptr) \
	X(PhysicsCarProperties*, car_properties, nullptr) \
	X(float, stat_weight, 0.0f) \
	X(float, stat_grip_1, 0.0f) \
	X(float, stat_grip_2, 0.0f) \
	X(float, stat_grip_3, 0.0f) \
	X(float, stat_turn_tension, 0.0f) \
	X(float, stat_turn_movement, 0.0f) \
	X(float, stat_drift_turn_movement, 0.0f) \
	X(float, stat_max_turn_rate, 200.0f) \
	X(float, stat_strafe_turn, 0.0f) \
	X(float, stat_strafe, 0.0f) \
	X(float, stat_turn_reaction, 0.0f) \
	X(float, stat_drift_accel, 0.0f) \
	X(float, stat_body, 0.0f) \
	X(float, stat_acceleration, 0.0f) \
	X(float, stat_max_speed, 0.0f) \
	X(float, stat_turn_decel, 0.0f) \
	X(float, stat_drag, 0.0f) \
	X(float, stat_track_collision, 0.0f) \
	X(float, stat_obstacle_collision, 0.0f) \
	X(float, stat_manual_turbo_gain, 0.0f) \
	X(float, stat_dashplate_turbo_gain, 0.0f) \
	X(float, stat_jumpplate_turbo_gain, 0.0f) \
	X(float, stat_dashplate_turbo_heat_multiplier, 0.0f) \
	X(float, stat_turbo_flat_loss_per_second, 0.0f) \
	X(float, stat_turbo_percent_loss_per_second, 0.0f) \
	X(float, stat_turbo_top_speed_effect, 0.0f) \
	X(float, stat_manual_boost_duration_seconds, 0.0f) \
	X(float, stat_dashplate_boost_duration_seconds, 0.0f) \
	X(float, stat_s_boost_base_speed_add_per_second, 0.0f) \
	X(float, stat_shift_boost_base_speed_add, 0.0f) \
	X(float, stat_shift_boost_velocity_multiplier, 1.0f) \
	X(float, stat_air_pitch_up_speed_loss_factor, 0.0f) \
	X(float, stat_air_glide_steering_speed_loss_factor, 0.0f) \
	X(float, stat_drive_target_speed_multiplier, 1.0f) \
	X(float, stat_acceleration_response_multiplier, 1.0f) \
	X(float, stat_forward_thrust_multiplier, 1.0f) \
	X(uint8_t, stat_accel_press_grip_frames, 0) \
	X(float, camera_reorienting, 0.0f) \
	X(float, camera_repositioning, 0.0f) \
	X(float, weight_derived_1, 0.0f) \
	X(float, weight_derived_2, 0.0f) \
	X(float, weight_derived_3, 0.0f) \
	X(float, boost_energy_use_mult, 1.0f) \
	X(float, energy_recharge_mult, 1.0f) \
	X(float, m_accel_setting, 0.5f) \
	X(uint16_t, s_boost_charge_max, 60)

#define PHYSICS_CAR_STATE_SCALAR_FIELDS(X) \
	X(float, calced_max_energy, 100.0f) \
	X(uint32_t, machine_state, 0) \
	X(float, base_speed, 0.0f) \
	X(float, boost_turbo, 0.0f) \
	X(float, pending_dashplate_heat, 0.0f) \
	X(float, pending_dashplate_heat_reward_scale, 1.0f) \
	X(float, race_start_charge, 0.0f) \
	X(float, air_tilt, 0.0f) \
	X(float, energy, 0.0f) \
	X(float, ko_energy_bonus, 0.0f) \
	X(uint32_t, boost_frames_manual, 0) \
	X(uint32_t, boost_frames_dash, 0) \
	X(uint32_t, boost_duration_manual_frames, 0) \
	X(uint32_t, boost_duration_dash_frames, 0) \
	X(uint32_t, simulation_tick, 0) \
	X(uint32_t, last_manual_boost_tick, 0) \
	X(uint32_t, last_hit_tick, 0) \
	X(float, last_hit_sfx_strength, 0.0f) \
	X(uint32_t, last_machine_hit_tick, 0) \
	X(float, last_machine_hit_sfx_strength, 0.0f) \
	X(int, pending_ko_attacker_car_index, -1) \
	X(bool, has_last_manual_boost_tick, false) \
	X(bool, has_last_hit_tick, false) \
	X(bool, has_last_machine_hit_tick, false) \
	X(uint32_t, spinattack_direction, 0) \
	X(float, spinattack_angle, 0.0f) \
	X(float, spinattack_decrement, 0.0f) \
	X(uint32_t, brake_timer, 0) \
	X(float, height_above_track, 0.0f) \
	X(uint16_t, current_checkpoint, 0) \
	X(int16_t, current_collision_checkpoint, -1) \
	X(uint16_t, last_ground_checkpoint, 0) \
	X(int32_t, last_mesh_floor_triangle, -1) \
	X(float, last_ground_distance, 0.0f) \
	X(float, previous_lap_distance, 0.0f) \
	X(float, checkpoint_fraction, 0.0f) \
	X(float, checkpoint_track_distance, 0.0f) \
	X(uint8_t, lap, 1) \
	X(bool, broken_lap_rollback_pending, false) \
	X(uint8_t, broken_lap_rollback_lap, 0) \
	X(float, lap_progress, 0.0f) \
	X(float, input_accel, 0.0f) \
	X(uint8_t, rail_collision_timer, 0) \
	X(uint32_t, terrain_state, 0) \
	X(uint8_t, grip_frames_from_accel_press, 0) \
	X(uint32_t, frames_since_start, 0) \
	X(uint32_t, frames_since_start_2, 0) \
	X(uint8_t, side_attack_delay, 0) \
	X(uint16_t, attack_cooldown_frames, 0) \
	X(uint32_t, air_time, 0) \
	X(bool, machine_crashed, false) \
	X(uint8_t, car_hit_invincibility, 0) \
	X(uint32_t, frames_since_death, 0) \
	X(int8_t, drift_sign, 0) \
	X(float, drift_ramp, 0.0f) \
	X(uint32_t, state_2, 0) \
	X(uint64_t, level_start_time, 0) \
	X(int, some_breakdown_int, 0) \
	X(int, breakdown_frame_counter, 0) \
	X(uint8_t, restore_state, 0) \
	X(uint32_t, restore_wait_frames, 0) \
	X(uint32_t, restore_move_frames, 0) \
	X(uint16_t, s_boost_charge, 0) \
	X(uint16_t, s_boost_frames_remaining, 0) \
	X(uint16_t, s_boost_emit_frame_accumulator, 0) \
	X(uint8_t, s_boost_pending_spark_spawns, 0) \
	X(uint8_t, pending_super_sparks, 0) \
	X(bool, s_boost_active, false)

#define PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(X) \
	X(int16_t, checkpoint_before_floor_contact, -1) \
	X(float, visual_shake_mult, 0.0f) \
	X(float, speed_kmh, 0.0f) \
	X(float, dashplate_heat_reward_scale, 1.0f) \
	X(float, turning_related, 0.0f) \
	X(float, side_attack_indicator, 0.0f) \
	X(uint32_t, g_anim_timer, 0) \
	X(float, input_strafe_32, 0.0f) \
	X(float, input_strafe_1_6, 0.0f) \
	X(float, turn_reaction_input, 0.0f) \
	X(float, input_steer_pitch, 0.0f) \
	X(float, input_strafe, 0.0f) \
	X(float, input_steer_yaw, 0.0f) \
	X(float, input_brake, 0.0f) \
	X(RoadData, road_sample, RoadData())

#define PHYSICS_CAR_SCALAR_FIELDS(X) \
	PHYSICS_CAR_STATIC_SCALAR_FIELDS(X) \
	PHYSICS_CAR_STATE_SCALAR_FIELDS(X) \
	PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(X)

#define PHYSICS_CAR_STATE_VEC3_FIELDS(X) \
	X(position_current, SimVec3()) \
	X(position_old, SimVec3()) \
	X(position_old_dupe, SimVec3()) \
	X(velocity, SimVec3()) \
	X(knockback_velocity, SimVec3()) \
	X(velocity_angular, SimVec3()) \
	X(track_surface_normal, SimVec3())

#define PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(X) \
	X(initial_pos, SimVec3()) \
	X(visual_rotation, SimVec3()) \
	X(track_surface_pos, SimVec3()) \
	X(track_surface_normal_prev, SimVec3()) \
	X(position_collision_snapshot, SimVec3()) \
	X(position_bottom, SimVec3()) \
	X(velocity_local, SimVec3()) \
	X(velocity_local_flattened_and_rotated, SimVec3()) \
	X(collision_push_track, SimVec3()) \
	X(collision_push_rail, SimVec3()) \
	X(collision_push_total, SimVec3()) \
	X(collision_response, SimVec3()) \
	X(unk_vec3_0x4e4, SimVec3()) \
	X(unk_vec3_0x4f0, SimVec3())

#define PHYSICS_CAR_VEC3_FIELDS(X) \
	PHYSICS_CAR_STATE_VEC3_FIELDS(X) \
	PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(X)

#define PHYSICS_CAR_STATE_TRANSFORM_FIELDS(X) \
	X(basis_physical, SimTransform()) \
	X(basis_physical_other, SimTransform()) \
	X(restore_start_transform, SimTransform()) \
	X(restore_target_transform, SimTransform())

#define PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(X) \
	X(transform_visual, SimTransform()) \
	X(g_pitch_mtx_0x5e0, SimTransform())

#define PHYSICS_CAR_TRANSFORM_FIELDS(X) \
	PHYSICS_CAR_STATE_TRANSFORM_FIELDS(X) \
	PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(X)

#define PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(X) \
	X(offset, SimVec3())

#define PHYSICS_CAR_TILT_STATE_VEC3_FIELDS(X) \
	X(pos_old, SimVec3()) \
	X(pos, SimVec3()) \
	X(up_vector, SimVec3()) \
	X(up_vector_2, SimVec3())

#define PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(X) \
	X(force_spatial, SimVec3())

#define PHYSICS_CAR_TILT_VEC3_FIELDS(X) \
	PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(X) \
	PHYSICS_CAR_TILT_STATE_VEC3_FIELDS(X) \
	PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(X)

#define PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(X)

#define PHYSICS_CAR_TILT_STATE_SCALAR_FIELDS(X) \
	X(float, force, 0.0f) \
	X(float, rest_length, 0.0f) \
	X(uint32_t, state, 0)

#define PHYSICS_CAR_TILT_SCALAR_FIELDS(X) \
	PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(X) \
	PHYSICS_CAR_TILT_STATE_SCALAR_FIELDS(X)

#define PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(X) \
	X(offset, SimVec3())

#define PHYSICS_CAR_WALL_STATE_VEC3_FIELDS(X)

#define PHYSICS_CAR_WALL_VEC3_FIELDS(X) \
	PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(X) \
	PHYSICS_CAR_WALL_STATE_VEC3_FIELDS(X)

#define PHYSICS_CAR_SOA_FIELDS(X) PHYSICS_CAR_SCALAR_FIELDS(X)

static inline SimTransform mxt_load_transform_soa(
	const float* c0x, const float* c0y, const float* c0z,
	const float* c1x, const float* c1y, const float* c1z,
	const float* c2x, const float* c2y, const float* c2z,
	const float* ox, const float* oy, const float* oz,
	int index)
{
	SimTransform out;
	out.basis.set_column(0, SimVec3(c0x[index], c0y[index], c0z[index]));
	out.basis.set_column(1, SimVec3(c1x[index], c1y[index], c1z[index]));
	out.basis.set_column(2, SimVec3(c2x[index], c2y[index], c2z[index]));
	out.origin = SimVec3(ox[index], oy[index], oz[index]);
	return out;
}

static inline void mxt_store_transform_soa(
	float* c0x, float* c0y, float* c0z,
	float* c1x, float* c1y, float* c1z,
	float* c2x, float* c2y, float* c2z,
	float* ox, float* oy, float* oz,
	int index, const SimTransform& value)
{
	const SimVec3 c0 = value.basis.get_column(0);
	const SimVec3 c1 = value.basis.get_column(1);
	const SimVec3 c2 = value.basis.get_column(2);
	c0x[index] = c0.x; c0y[index] = c0.y; c0z[index] = c0.z;
	c1x[index] = c1.x; c1y[index] = c1.y; c1z[index] = c1.z;
	c2x[index] = c2.x; c2y[index] = c2.y; c2z[index] = c2.z;
	ox[index] = value.origin.x; oy[index] = value.origin.y; oz[index] = value.origin.z;
}

#define MXT_LOAD_TRANSFORM(storage, name, index) \
	mxt_load_transform_soa((storage).name##_c0x, (storage).name##_c0y, (storage).name##_c0z, \
		(storage).name##_c1x, (storage).name##_c1y, (storage).name##_c1z, \
		(storage).name##_c2x, (storage).name##_c2y, (storage).name##_c2z, \
		(storage).name##_ox, (storage).name##_oy, (storage).name##_oz, (index))

#define MXT_STORE_TRANSFORM(storage, name, index, value) \
	do { const SimTransform mxt_transform_tmp = (value); \
		mxt_store_transform_soa((storage).name##_c0x, (storage).name##_c0y, (storage).name##_c0z, \
			(storage).name##_c1x, (storage).name##_c1y, (storage).name##_c1z, \
			(storage).name##_c2x, (storage).name##_c2y, (storage).name##_c2z, \
			(storage).name##_ox, (storage).name##_oy, (storage).name##_oz, (index), mxt_transform_tmp); } while (0)

struct PhysicsCarSoA {
	int count = 0;
	int lane_count = 0;
	int point_count = 0;
	int global_start = 0;
	int shard_index = 0;
	int shard_count = 1;
	int total_count = 0;
	int total_lane_count = 0;
	PhysicsCarSoA* shards = nullptr;
#define DECLARE_PHYSICS_CAR_SOA_ARRAY(type, name, default_value) type* name = nullptr;
	PHYSICS_CAR_SCALAR_FIELDS(DECLARE_PHYSICS_CAR_SOA_ARRAY)
#undef DECLARE_PHYSICS_CAR_SOA_ARRAY
#define DECLARE_PHYSICS_CAR_SOA_VEC3(name, default_value) float* name##_x = nullptr; float* name##_y = nullptr; float* name##_z = nullptr;
	PHYSICS_CAR_VEC3_FIELDS(DECLARE_PHYSICS_CAR_SOA_VEC3)
#undef DECLARE_PHYSICS_CAR_SOA_VEC3
#define DECLARE_PHYSICS_CAR_SOA_TRANSFORM(name, default_value) float* name##_c0x = nullptr; float* name##_c0y = nullptr; float* name##_c0z = nullptr; float* name##_c1x = nullptr; float* name##_c1y = nullptr; float* name##_c1z = nullptr; float* name##_c2x = nullptr; float* name##_c2y = nullptr; float* name##_c2z = nullptr; float* name##_ox = nullptr; float* name##_oy = nullptr; float* name##_oz = nullptr;
	PHYSICS_CAR_TRANSFORM_FIELDS(DECLARE_PHYSICS_CAR_SOA_TRANSFORM)
#undef DECLARE_PHYSICS_CAR_SOA_TRANSFORM
#define DECLARE_PHYSICS_CAR_TILT_SOA_ARRAY(type, name, default_value) type* tilt_##name = nullptr;
	PHYSICS_CAR_TILT_SCALAR_FIELDS(DECLARE_PHYSICS_CAR_TILT_SOA_ARRAY)
#undef DECLARE_PHYSICS_CAR_TILT_SOA_ARRAY
#define DECLARE_PHYSICS_CAR_TILT_SOA_VEC3(name, default_value) float* tilt_##name##_x = nullptr; float* tilt_##name##_y = nullptr; float* tilt_##name##_z = nullptr;
	PHYSICS_CAR_TILT_VEC3_FIELDS(DECLARE_PHYSICS_CAR_TILT_SOA_VEC3)
#undef DECLARE_PHYSICS_CAR_TILT_SOA_VEC3
#define DECLARE_PHYSICS_CAR_WALL_SOA_VEC3(name, default_value) float* wall_##name##_x = nullptr; float* wall_##name##_y = nullptr; float* wall_##name##_z = nullptr;
	PHYSICS_CAR_WALL_VEC3_FIELDS(DECLARE_PHYSICS_CAR_WALL_SOA_VEC3)
#undef DECLARE_PHYSICS_CAR_WALL_SOA_VEC3
};

class PhysicsCar
{
private:
	float scratch_float[16];
	bool compute_respawn_target(uint16_t cp_idx, SimTransform &out_transform, float &out_distance, uint16_t *out_checkpoint, float *out_fraction) const;
	void start_restore_to_last_ground();
	void sample_mesh_floor_with_seed(CollisionData &out_collision, const SimVec3 &point, float max_distance, uint8_t mask, int start_idx, bool allow_global_fallback, TrackQueryScratch &scratch, bool build_surface = true, bool build_surface_basis = true, bool prime_from_scratch_candidates = false);
	void trigger_mesh_fallout();
	void trigger_mesh_kill_collision();
public:
	PhysicsCarSoA* soa = nullptr;
	int soa_index = 0;

public:
	PhysicsCar(PhysicsCarSoA* p_soa, int p_index);
	SimVec3 prepare_machine_frame(TrackQueryScratch &scratch, PhysicsCarFloorProfile* profile = nullptr);
	float get_current_stage_min_y() const;
    void handle_machine_damage_and_visuals();
    void handle_machine_damage_and_visuals_tail();
	bool find_floor_beneath_machine(TrackQueryScratch &scratch, PhysicsCarFloorProfile* profile = nullptr);
	void handle_steering();
	void set_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag);
	void remove_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag);
	void handle_suspension_states();
	float classify_machine_drift(int point_lane, const SimVec3& corner_delta_local,
		float grip_1, float grip_3, bool& out_was_drifting);
	void apply_machine_turn_and_strafe(int point_lane, float in_angle_vel, float drift_delta,
		bool was_drifting, float speed_factor, const SimTransform& steer_basis);
	void handle_machine_turn_and_strafe_points4(float in_angle_vel);
	void project_velocity_to_local_frame();
	void handle_linear_velocity();
	void apply_initial_accel_activation(float effective_accel_input);
	float handle_machine_accel_and_boost(float neg_local_fwd_speed,
		float abs_local_lateral_speed, float drift_accel_factor);
	void handle_angle_velocity();
	void handle_airborne_controls();
	void orient_vehicle_from_gravity_or_road();
	void handle_drag_and_glide_forces();
	void rotate_machine_from_angle_velocity();
	void handle_startup_wobble();
	void initialize_machine();
	void update_machine_stats();
	void update_effective_machine_stats(bool include_technique = true);
	void compute_technique_modifier(bool include_technique,
		CarStatModifierLayer &out_layer, float &out_intensity) const;
	float evaluate_effective_stat_with_context(CarStatId stat,
		CarStatModifierLayer technique_layer, float technique_intensity,
		CarStatModifierLayer boost_layer, bool s_boost_active) const;
	float evaluate_effective_stat(CarStatId stat, bool include_technique,
		bool manual_boost_active, bool dashplate_boost_active, bool s_boost_active) const;
	void reset_machine(int reset_type);
	void update_pitch_transform_from_machine_front_back();
	void update_suspension_forces(int point_lane, const SimVec3& p0_ray_start_ws, const SimVec3& p0, const SimVec3& p1_ray_end_ws, const SimVec2& road_t, const SimTransform& surf, const SimVec3& vehicle_up_ws, float stat_weight, float mass_fraction, float time_based_factor, bool accel_off, float ray_start_from_attachment_len, float ray_len, bool draw_tilt_debug);
	SimVec3 get_avg_track_normal_from_tilt_corners(TrackQueryScratch &scratch, PhysicsCarFloorProfile* profile = nullptr);
	void set_terrain_state_from_track(TrackQueryScratch &scratch, const SimVec3 &trigger_p0, const SimVec3 &trigger_p1);
	void handle_attack_states();
	void apply_torque_from_force(const SimVec3& p_local_offset, const SimVec3& wf_world_force);
	int update_machine_corners(TrackQueryScratch &scratch, PhysicsCarCornerProfile* profile = nullptr,
		float* out_max_rail_contact_push = nullptr);
    void apply_machine_collision_response_from_corners(int corner_collision_type_flag,
	float push_magnitude_rail, float push_magnitude_track, float rail_hit_sfx_strength,
	float current_world_speed,
	float speed_over_weight, bool include_start_projection);
    void align_machine_y_with_track_normal_immediate();
    void handle_checkpoints(TrackQueryScratch &scratch);
    void collide_with_landmine(Mine* in_mine, const SimVec3 &travel_start, const SimVec3 &travel_end);
    void respawn_at_checkpoint(uint16_t cp_idx);
    SimTransform calculate_respawn_transform(uint16_t cp_idx) const;
    void update_restore(float accel_input);
    void check_respawn();
    void breakdown_physics();
    void broken_down_fling_physics();
    bool apply_damage(float impact_strength);
    bool handle_machine_crash(int unk_int);
    float scale_collision_impulse_and_damage(bool other_machine_b10_flag);
    float prepare_impact_direction_info(ImpactData &impact, const SimVec3 &impactDirWorld);
    void buildSweepForMachine(float cappedSpeedMps, SimVec3 &sweepStartOut, SimVec3 &cappedVelocityOut);
    bool handle_machine_v_machine_collision(PhysicsCar &other_machine);
    bool handle_machine_v_bumper_collision(PhysicsCar &bumper_machine);
	bool can_collect_super_spark() const;
	void add_super_spark_charge(uint16_t amount);
	bool can_start_s_boost() const;
	void start_s_boost(uint16_t duration_frames);
	void stop_s_boost();
	void update_s_boost_state();
	uint8_t consume_pending_s_boost_sparks();
	bool is_s_boost_active() const { return soa->s_boost_active[soa_index]; }
	bool is_s_boost_ready() const { return !soa->s_boost_active[soa_index] && soa->s_boost_charge[soa_index] >= soa->s_boost_charge_max[soa_index]; }
	uint16_t get_s_boost_charge() const { return soa->s_boost_charge[soa_index]; }
	uint16_t get_s_boost_max_charge() const { return soa->s_boost_charge_max[soa_index]; }
	void queue_super_sparks(int count);
};
