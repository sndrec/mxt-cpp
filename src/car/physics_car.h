#pragma once

#include "car/car_points.h"
#include "car/car_properties.h"
#include "mxt_core/player_input.h"
#include "track/racetrack.h"
#include "mxt_core/math_utils.h"
#include <cmath>

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
	int find_current_checkpoint_recursive(const godot::Vector3 &use_pos, int cp_index, int iterations);
	void get_closest_road_data_at_point(RoadData &out_data, godot::Vector3 &in_point, bool strict, uint16_t start_cp);
public:
	enum STEER_STATE {
		NORMAL,
		DRIFT
	};
	RaceTrack* current_track;
	PhysicsCarProperties* car_properties;
	PlayerInput current_input;
	godot::Vector3 position;
	godot::Vector3 prev_position;
	godot::Vector3 desired_position;
	godot::Vector3 apparent_velocity;
	godot::Basis orientation;
	godot::Basis prev_orientation;
	godot::Transform3D car_transform;
	godot::Vector3 travel_direction;
	godot::Vector3 angle_velocity;
	godot::Vector3 gravity_orientation;
	godot::Transform3D closest_roadroot;
	godot::Transform3D closest_roadt3d;
	godot::Transform3D prev_closest_roadt3d;
	godot::Vector3 current_checkpoint_t;
	godot::Vector2 current_road_t;
	godot::Vector3 air_velocity;
	godot::Vector3 knockback_velocity;
	STEER_STATE current_steer_state;
	float base_speed;
	float apparent_speed;
	float apparent_speed_kmh;
	float health;
	float drift_dot;
	float boost_time;
	float dashplate_time;
	float turbo;
	float air_tilt;
	float ground_air_time;
	float turn_reaction_effect;
	float current_steering_input;
	float previous_steering_input;
	float current_steering;
	float previous_steering;
	float current_strafe;
	bool grounded;
	bool has_road_data;
	int num_collision_points;
	PhysicsCarCollisionPoint* collision_points;
	int num_suspension_points;
	PhysicsCarSuspensionPoint* suspension_points;
	int current_collision_checkpoint;
	int current_race_checkpoint;
	int current_track_segment;
	int current_road_embed;

	// the strategy here is to do each part of the vehicle simulation in steps, to improve memory access performance
	// for example, since all of the road collision for all vehicles is handled contiguously,
	// we can expect the road data to more frequently be hanging around in CPU cache
	// which should hopefully reduce cache misses more than if we just handled one vehicle all at once
	// since there's much less data for a single vehicle than for the road collision
	void initialize();
	void preprocess_car(); // gather input, precalculate anything we'll need more than once
	void display_checkpoint(CollisionCheckpoint* in_checkpoint);
	float calculate_top_speed();
	float calculate_acceleration(float in_speed);
	void process_car_steering(); // modify angle velocities for steering, handle drifting and strafing
	void process_car_acceleration(); // modify linear velocities, travel dir and base speed, drag, accel, boosting, braking
	void process_car_road_collision(); // modify actual vehicle position and orientation, handle going airborne/landing, get road data
	void process_car_car_collision(); // handle intersection and depenetration from other vehicles
	void postprocess_car(); // anything else that needs to be done before we finish ticking
};