#pragma once

#include "car/car_points.h"
#include "car/car_properties.h"
#include "mxt_core/player_input.h"
#include "track/racetrack.h"
#include "mxt_core/math_utils.h"
#include "mxt_core/enums.h"

#include <cmath>
#include <vector>

#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/variant/vector3.hpp"
#include "godot_cpp/variant/transform3d.hpp"
#include "godot_cpp/variant/quaternion.hpp"
#include "godot_cpp/variant/packed_vector3_array.hpp"

#include "mxt_core/mtxa_stack.hpp"

struct RoadData {
	uint8_t terrain;
	int16_t cp_idx;
	godot::Vector3 spatial_t;
	godot::Vector2 road_t;
	godot::Transform3D closest_surface;
	godot::Transform3D closest_root;
};

struct CollisionData {
	bool collided;
	godot::Vector3 collision_point;
	godot::Vector3 collision_normal;
	RoadData road_data;
};

class PhysicsCar
{
private:
	MtxStack* mtxa;
public:
	RaceTrack* current_track = nullptr;
	PhysicsCarProperties* car_properties = nullptr; // Base properties

	// Stats derived from car_properties and m_accel_setting, matching MXRacer members
	float calced_max_energy = 100.0f; // Based on MXRacer's direct initialization
	uint32_t machine_state = 0;       // Use MACHINESTATE::FLAGS for operations
	float stat_weight = 0.0f;
	float stat_grip_1 = 0.0f;
	float stat_grip_2 = 0.0f;
	float stat_grip_3 = 0.0f;
	float stat_turn_tension = 0.0f;
	float stat_turn_movement = 0.0f;
	float stat_strafe_turn = 0.0f;
	float stat_strafe = 0.0f;
	float stat_turn_reaction = 0.0f;
	float stat_drift_accel = 0.0f;
	float stat_body = 0.0f;
	float stat_acceleration = 0.0f;
	float stat_max_speed = 0.0f;
	float stat_boost_strength = 0.0f;
	float stat_boost_length = 0.0f;
	float stat_turn_decel = 0.0f;
	float stat_drag = 0.0f;
	uint8_t stat_accel_press_grip_frames = 0;
	float camera_reorienting = 0.0f;    // Corresponds to CarDefinition.camera_reorienting
	float camera_repositioning = 0.0f;  // Corresponds to CarDefinition.camera_repositioning

	godot::String machine_name;

	godot::Vector3 position_current = godot::Vector3();
	godot::Vector3 position_old = godot::Vector3();
	godot::Vector3 position_old_2 = godot::Vector3();
	godot::Vector3 position_old_dupe = godot::Vector3();
	godot::Vector3 position_bottom = godot::Vector3();
	godot::Vector3 position_behind = godot::Vector3();

	godot::Vector3 velocity = godot::Vector3();
	godot::Vector3 velocity_angular = godot::Vector3();
	godot::Vector3 velocity_local = godot::Vector3();
	godot::Vector3 velocity_local_flattened_and_rotated = godot::Vector3();

	godot::Transform3D basis_physical = godot::Transform3D();
	godot::Transform3D basis_physical_other = godot::Transform3D();
	godot::Transform3D transform_visual = godot::Transform3D();

	float base_speed = 0.0f;
	float boost_turbo = 0.0f;
	float weight_derived_1 = 0.0f;
	float weight_derived_2 = 0.0f;
	float weight_derived_3 = 0.0f;
	godot::Vector3 visual_rotation = godot::Vector3();
	float race_start_charge = 0.0f;
	float speed_kmh = 0.0f;
	float air_tilt = 0.0f;
	float energy = 0.0f;
	uint32_t boost_frames = 0;
	uint32_t boost_frames_manual = 0;
	float height_adjust_from_boost = 0.0f;
	uint32_t spinattack_direction = 0;
	float spinattack_angle = 0.0f;
	float spinattack_decrement = 0.0f;
	uint32_t brake_timer = 0;

	godot::Vector3 collision_push_track = godot::Vector3();
	godot::Vector3 collision_push_rail = godot::Vector3();
	godot::Vector3 collision_push_total = godot::Vector3();
	godot::Vector3 collision_response = godot::Vector3();
	godot::Vector3 track_surface_normal = godot::Vector3();
	godot::Vector3 track_surface_normal_prev = godot::Vector3();
	godot::Vector3 track_surface_pos = godot::Vector3();
	float height_above_track = 0.0f;

	uint16_t current_checkpoint = 0;
	float checkpoint_fraction = 0.0f;
	uint8_t lap = 1; // Initialized to 1 as in MXRacer reset
	float lap_progress = 0.0f;

	float input_strafe_32 = 0.0f;
	float input_strafe_1_6 = 0.0f;
	float input_steer_pitch = 0.0f;
	float input_strafe = 0.0f;
	float input_steer_yaw = 0.0f;
	float input_accel = 0.0f;
	float input_brake = 0.0f;
	float input_yaw_dupe = 0.0f;

	uint8_t rail_collision_timer = 0;
	uint32_t terrain_state = 0; // Use TERRAIN::FLAGS for operations
	uint8_t grip_frames_from_accel_press = 0;
	float visual_shake_mult = 0.0f;
	uint32_t frames_since_start = 0;
	uint32_t frames_since_start_2 = 0;
	uint8_t side_attack_delay = 0;
	uint32_t air_time = 0;
	float damage_from_last_hit = 0.0f;
	uint32_t strafe_effect = 0;
	bool machine_crashed = false;
	uint8_t machine_collision_frame_counter = 0;
	uint8_t car_hit_invincibility = 0;
	uint8_t boost_delay_frame_counter = 0;

	float turn_reaction_input = 0.0f;
	float turn_reaction_effect = 0.0f;
	float boost_energy_use_mult = 1.0f;
	uint32_t frames_since_death = 0;
	uint32_t terrain_state_2 = 0;
	uint32_t suspension_reset_flag = 0;
	float turning_related = 0.0f;

	float stat_obstacle_collision = 0.0f;
	float stat_track_collision = 0.0f;
	uint32_t state_2 = 0;
	float side_attack_indicator = 0.0f;
	float unk_stat_0x5d4 = 0.0f;
	uint32_t g_anim_timer = 0;
	godot::Transform3D g_pitch_mtx_0x5e0 = godot::Transform3D();
	int strafe_visual_roll = 0;
	godot::Quaternion unk_quat_0x5c4 = godot::Quaternion();

	float m_accel_setting = 0.5f; // Balance setting (0.0 to 1.0), corresponds to g_balance. Default to 0.5 (neutral).
	uint64_t level_start_time = 0;

	PhysicsCarSuspensionPoint tilt_fl;
	PhysicsCarSuspensionPoint tilt_fr;
	PhysicsCarSuspensionPoint tilt_bl;
	PhysicsCarSuspensionPoint tilt_br;

	PhysicsCarCollisionPoint wall_fl;
	PhysicsCarCollisionPoint wall_fr;
	PhysicsCarCollisionPoint wall_bl;
	PhysicsCarCollisionPoint wall_br;

public:
	PhysicsCar() = default;

	godot::Vector3 prepare_machine_frame();
	float get_current_stage_min_y() const;
	void handle_machine_damage_and_visuals();
	bool find_floor_beneath_machine();
	void handle_steering();
	void set_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag);
	void remove_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag);
	void handle_suspension_states();
	void handle_machine_turn_and_strafe(PhysicsCarSuspensionPoint& tilt_corner, float in_angle_vel);
	void handle_linear_velocity();
	float handle_machine_accel_and_boost(float neg_local_fwd_speed, float abs_local_lateral_speed, float drift_accel_factor);
	void handle_angle_velocity();
	void handle_airborne_controls();
	void orient_vehicle_from_gravity_or_road();
	void handle_drag_and_glide_forces();
	void rotate_machine_from_angle_velocity();
	void handle_startup_wobble();
	void initialize_machine();
	void update_machine_stats(); // This will use car_properties (base) and m_accel_setting to derive stats
	void reset_machine(int reset_type);
	void rotate_mtxa_from_diff_btwn_machine_front_and_back();
	void update_suspension_forces(PhysicsCarSuspensionPoint& in_corner);
	godot::Vector3 get_avg_track_normal_from_tilt_corners();
	void set_terrain_state_from_track();
	void handle_attack_states();
	void apply_torque_from_force(const godot::Vector3& p_local_offset, const godot::Vector3& wf_world_force);
	void simulate_machine_motion();
	int update_machine_corners();
    void create_machine_visual_transform();
    void handle_machine_collision_response();
    void align_machine_y_with_track_normal_immediate();
    void handle_checkpoints();
    void post_tick();
    void tick();
};
