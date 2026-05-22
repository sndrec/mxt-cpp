
#include "physics_car.h"
#include "main.h"
#include "godot_cpp/variant/plane.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include "mxt_core/debug.hpp"
#include "mxt_core/math_utils.h"
#include "mxt_core/player_input.h"
#include "track/track_segment.h"
#include "track/trigger_collider.h"

static inline SimVec3 normalized_safe(const SimVec3 &v,
	const SimVec3 &def = SimVec3()) {
	return v.length_squared() > 0.000001f ? v.normalized() : def;
}

static inline SimVec3 set_vec3_length(const SimVec3 &v, float len) {
	float l = v.length();
	if (l > 0.000001f)
		return v * (len / l);
	return SimVec3();
}

static inline float landing_alignment_penalty_factor(uint32_t air_time)
{
	constexpr float full_penalty_frames = 0.5f * _TICKS_PER_SECOND;
	return std::clamp(static_cast<float>(air_time) / full_penalty_frames, 0.0f, 1.0f);
}

static inline bool trace_mesh_floor_for_car(const PhysicsCarSoA *soa, int soa_index)
{
	return soa &&
		DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_MESH_FLOOR) &&
		(soa->global_start + soa_index) == 0;
}

static inline godot::Vector3 debug_gd_vec3(const SimVec3& v)
{
	return godot::Vector3(v.x, v.y, v.z);
}

static void draw_nearest_rail_candidate(
	const TrackEdgeRailSide sides[2],
	const SimVec3& reference,
	const PhysicsCarSoA* soa,
	int lane,
	float draw_time)
{
	if (!DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAIL_CANDIDATES) ||
		!soa ||
		(soa->global_start + lane) != 0) {
		return;
	}
	int best_idx = 0;
	float best_dist2 = (reference - sides[0].pos).length_squared();
	const float dist2_1 = (reference - sides[1].pos).length_squared();
	if (dist2_1 < best_dist2) {
		best_idx = 1;
		best_dist2 = dist2_1;
	}
	const TrackEdgeRailSide &side = sides[best_idx];
	godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	dd3d->call("draw_arrow", debug_gd_vec3(side.pos), debug_gd_vec3(side.pos + side.rail_n * 8.0f), godot::Color(1.0f, 0.0f, 1.0f), 0.35, true, draw_time);
	dd3d->call("draw_arrow", debug_gd_vec3(side.pos), debug_gd_vec3(side.pos + side.up_n * 6.0f), godot::Color(0.2f, 1.0f, 0.2f), 0.2, true, draw_time);
	dd3d->call("draw_arrow", debug_gd_vec3(side.pos), debug_gd_vec3(side.pos + side.forward_n * 6.0f), godot::Color(0.2f, 0.5f, 1.0f), 0.2, true, draw_time);
}

namespace {
constexpr float kRespawnForwardDistance = 100.0f;
constexpr float kMinCheckpointDistance = 0.01f;
constexpr float kMaxPositiveCheckpointAdvance = 1500.0f;
constexpr uint16_t kAttackCooldownFrames = static_cast<uint16_t>(4.0f * _TICKS_PER_SECOND);
constexpr float kSpinAttackShortenMultiplier = 1.3f;

static inline float track_lap_length(const RaceTrack *track)
{
	if (!track) {
		return 0.0f;
	}
	float lap_length = track->lap_length;
	if (lap_length <= 0.0f && track->num_checkpoints > 0) {
		lap_length = track->checkpoints[track->num_checkpoints - 1].distance;
	}
	return lap_length;
}

static inline float normalize_track_distance(float distance, float lap_length)
{
	if (lap_length <= 0.0f) {
		return distance;
	}
	float normalized = std::fmod(distance, lap_length);
	if (normalized < 0.0f) {
		normalized += lap_length;
	}
	return normalized;
}

static inline bool track_distance_is_before_lap_line(float distance, float lap_length)
{
	if (lap_length <= 0.0f) {
		return false;
	}
	return normalize_track_distance(distance, lap_length) > lap_length * 0.875f;
}

static inline void clear_motion_for_restore(PhysicsCarSoA *soa, int lane)
{
	soa->velocity_x[lane] = 0.0f;
	soa->velocity_y[lane] = 0.0f;
	soa->velocity_z[lane] = 0.0f;
	soa->knockback_velocity_x[lane] = 0.0f;
	soa->knockback_velocity_y[lane] = 0.0f;
	soa->knockback_velocity_z[lane] = 0.0f;
	soa->velocity_local_x[lane] = 0.0f;
	soa->velocity_local_y[lane] = 0.0f;
	soa->velocity_local_z[lane] = 0.0f;
	soa->velocity_local_flattened_and_rotated_x[lane] = 0.0f;
	soa->velocity_local_flattened_and_rotated_y[lane] = 0.0f;
	soa->velocity_local_flattened_and_rotated_z[lane] = 0.0f;
	soa->velocity_angular_x[lane] = 0.0f;
	soa->velocity_angular_y[lane] = 0.0f;
	soa->velocity_angular_z[lane] = 0.0f;
	soa->base_speed[lane] = 0.0f;
	soa->boost_turbo[lane] = 0.0f;
	soa->state_2[lane] &= ~0x20u;
}

}

#define LOAD_CAR_VEC3(car, name) SimVec3((car).soa->name##_x[(car).soa_index], (car).soa->name##_y[(car).soa_index], (car).soa->name##_z[(car).soa_index])
#define STORE_CAR_VEC3(car, name, value) do { const SimVec3 mxt_v3_tmp = (value); (car).soa->name##_x[(car).soa_index] = mxt_v3_tmp.x; (car).soa->name##_y[(car).soa_index] = mxt_v3_tmp.y; (car).soa->name##_z[(car).soa_index] = mxt_v3_tmp.z; } while (0)
#define LOAD_VEC3(name) SimVec3(soa->name##_x[soa_index], soa->name##_y[soa_index], soa->name##_z[soa_index])
#define STORE_VEC3(name, value) do { const SimVec3 mxt_v3_tmp = (value); soa->name##_x[soa_index] = mxt_v3_tmp.x; soa->name##_y[soa_index] = mxt_v3_tmp.y; soa->name##_z[soa_index] = mxt_v3_tmp.z; } while (0)
#define ADD_VEC3(name, value) STORE_VEC3(name, LOAD_VEC3(name) + (value))
#define SUB_VEC3(name, value) STORE_VEC3(name, LOAD_VEC3(name) - (value))
#define LOAD_TRANSFORM(name) MXT_LOAD_TRANSFORM(*soa, name, soa_index)
#define STORE_TRANSFORM(name, value) MXT_STORE_TRANSFORM(*soa, name, soa_index, value)
#define LOAD_CAR_TRANSFORM(car, name) MXT_LOAD_TRANSFORM(*(car).soa, name, (car).soa_index)
#define POINT_INDEX(lane) (soa_index * 4 + (lane))
#define LOAD_TILT_VEC3(name, point_index) SimVec3(soa->tilt_##name##_x[(point_index)], soa->tilt_##name##_y[(point_index)], soa->tilt_##name##_z[(point_index)])
#define STORE_TILT_VEC3(name, point_index, value) do { const SimVec3 mxt_v3_tmp = (value); soa->tilt_##name##_x[(point_index)] = mxt_v3_tmp.x; soa->tilt_##name##_y[(point_index)] = mxt_v3_tmp.y; soa->tilt_##name##_z[(point_index)] = mxt_v3_tmp.z; } while (0)
#define LOAD_WALL_VEC3(name, point_index) SimVec3(soa->wall_##name##_x[(point_index)], soa->wall_##name##_y[(point_index)], soa->wall_##name##_z[(point_index)])
#define STORE_WALL_VEC3(name, point_index, value) do { const SimVec3 mxt_v3_tmp = (value); soa->wall_##name##_x[(point_index)] = mxt_v3_tmp.x; soa->wall_##name##_y[(point_index)] = mxt_v3_tmp.y; soa->wall_##name##_z[(point_index)] = mxt_v3_tmp.z; } while (0)

static inline SimVec3 mxt_basis_rotate(const SimTransform& basis_transform, const SimVec3& p)
{
	return basis_transform.basis.xform(p);
}

static inline SimVec3 mxt_basis_inverse_rotate(const SimTransform& basis_transform, const SimVec3& p)
{
	return basis_transform.basis.xform_inv(p);
}

static inline SimVec3 mxt_transform_point(const SimTransform& basis_transform, const SimVec3& origin, const SimVec3& p)
{
	return basis_transform.basis.xform(p) + origin;
}

static inline SimVec3x4 mxt_transform_points4(
	const SimTransform& basis_transform,
	const SimVec3& origin,
	SimFloat4 lx,
	SimFloat4 ly,
	SimFloat4 lz)
{
	const SimBasis& b = basis_transform.basis;
	return SimVec3x4(
		SimFloat4(origin.x) + SimFloat4(b[0].x) * lx + SimFloat4(b[1].x) * ly + SimFloat4(b[2].x) * lz,
		SimFloat4(origin.y) + SimFloat4(b[0].y) * lx + SimFloat4(b[1].y) * ly + SimFloat4(b[2].y) * lz,
		SimFloat4(origin.z) + SimFloat4(b[0].z) * lx + SimFloat4(b[1].z) * ly + SimFloat4(b[2].z) * lz);
}

static inline SimVec3x4 mxt_transform_points4(
	const SimTransform& basis_transform,
	const SimVec3& origin,
	const SimVec3& p0,
	const SimVec3& p1,
	const SimVec3& p2,
	const SimVec3& p3)
{
	return mxt_transform_points4(
		basis_transform,
		origin,
		SimFloat4(p0.x, p1.x, p2.x, p3.x),
		SimFloat4(p0.y, p1.y, p2.y, p3.y),
		SimFloat4(p0.z, p1.z, p2.z, p3.z));
}

static inline void mxt_store_points4(SimVec3 out[4], const SimVec3x4& points)
{
	float x[4];
	float y[4];
	float z[4];
	sim_store4(x, points.x);
	sim_store4(y, points.y);
	sim_store4(z, points.z);
	for (int i = 0; i < 4; ++i) {
		out[i] = SimVec3(x[i], y[i], z[i]);
	}
}

static inline SimVec3 mxt_inverse_transform_point(const SimTransform& basis_transform, const SimVec3& origin, const SimVec3& p)
{
	return basis_transform.basis.xform_inv(p - origin);
}

static inline float checkpoint_longitudinal_t(const RaceTrack& track, int cp_idx, const SimVec3& point)
{
	const CollisionCheckpoint& cp = track.checkpoints[cp_idx];
	const SimVec3 p1 = cp.start_plane.project(point);
	const SimVec3 p2 = cp.end_plane.project(point);
	const float cp_t = get_closest_t_on_segment(point, p1, p2);
	return remap_float(cp_t, 0.0f, 1.0f, cp.t_start, cp.t_end);
}

static inline void mxt_rotate_basis_x(SimTransform& t, float angle_rad)
{
	const float c = deterministic_fp::cosf(angle_rad);
	const float s = deterministic_fp::sinf(angle_rad);
	const SimVec3 y = t.basis.get_column(1);
	const SimVec3 z = t.basis.get_column(2);
	t.basis.set_column(1, y * c + z * s);
	t.basis.set_column(2, z * c - y * s);
}

static inline void mxt_rotate_basis_y(SimTransform& t, float angle_rad)
{
	const float c = deterministic_fp::cosf(angle_rad);
	const float s = deterministic_fp::sinf(angle_rad);
	const SimVec3 x = t.basis.get_column(0);
	const SimVec3 z = t.basis.get_column(2);
	t.basis.set_column(0, x * c - z * s);
	t.basis.set_column(2, x * s + z * c);
}

PhysicsCar::PhysicsCar(PhysicsCarSoA* p_soa, int p_index)
	: soa(p_soa),
	  soa_index(p_index)
{
#define RESET_PHYSICS_CAR_SOA_VALUE(type, name, default_value) soa->name[soa_index] = default_value;
	PHYSICS_CAR_SCALAR_FIELDS(RESET_PHYSICS_CAR_SOA_VALUE)
#undef RESET_PHYSICS_CAR_SOA_VALUE
#define RESET_PHYSICS_CAR_SOA_VEC3(name, default_value) { SimVec3 v = default_value; soa->name##_x[soa_index] = v.x; soa->name##_y[soa_index] = v.y; soa->name##_z[soa_index] = v.z; }
	PHYSICS_CAR_VEC3_FIELDS(RESET_PHYSICS_CAR_SOA_VEC3)
#undef RESET_PHYSICS_CAR_SOA_VEC3
#define RESET_PHYSICS_CAR_SOA_TRANSFORM(name, default_value) STORE_TRANSFORM(name, default_value);
	PHYSICS_CAR_TRANSFORM_FIELDS(RESET_PHYSICS_CAR_SOA_TRANSFORM)
#undef RESET_PHYSICS_CAR_SOA_TRANSFORM
	for (int lane = 0; lane < 4; ++lane) {
		const int p = POINT_INDEX(lane);
#define RESET_PHYSICS_CAR_TILT_SOA_VALUE(type, name, default_value) soa->tilt_##name[p] = default_value;
		PHYSICS_CAR_TILT_SCALAR_FIELDS(RESET_PHYSICS_CAR_TILT_SOA_VALUE)
#undef RESET_PHYSICS_CAR_TILT_SOA_VALUE
#define RESET_PHYSICS_CAR_TILT_SOA_VEC3(name, default_value) { SimVec3 v = default_value; soa->tilt_##name##_x[p] = v.x; soa->tilt_##name##_y[p] = v.y; soa->tilt_##name##_z[p] = v.z; }
		PHYSICS_CAR_TILT_VEC3_FIELDS(RESET_PHYSICS_CAR_TILT_SOA_VEC3)
#undef RESET_PHYSICS_CAR_TILT_SOA_VEC3
#define RESET_PHYSICS_CAR_WALL_SOA_VEC3(name, default_value) { SimVec3 v = default_value; soa->wall_##name##_x[p] = v.x; soa->wall_##name##_y[p] = v.y; soa->wall_##name##_z[p] = v.z; }
		PHYSICS_CAR_WALL_VEC3_FIELDS(RESET_PHYSICS_CAR_WALL_SOA_VEC3)
#undef RESET_PHYSICS_CAR_WALL_SOA_VEC3
	}
}


SimVec3 PhysicsCar::prepare_machine_frame(TrackQueryScratch &scratch)
{
	// Reset input if we're in the starting countdown
	if (soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) {
		soa->input_steer_yaw[soa_index] = 0.0f;
		soa->input_steer_pitch[soa_index] = 0.0f;
		soa->input_brake[soa_index] = 0.0f;
		soa->input_strafe[soa_index] = 0.0f;
		soa->machine_state[soa_index] &= ~(MACHINESTATE::SIDEATTACKING |
			MACHINESTATE::JUST_PRESSED_BOOST |
			MACHINESTATE::SPINATTACKING);
	}

	soa->machine_state[soa_index] &= ~(MACHINESTATE::DIEDTHISFRAMEOOB_Q |
		MACHINESTATE::JUST_HIT_DASHPLATE |
		MACHINESTATE::RACEJUSTBEGAN_Q |
		MACHINESTATE::JUSTTAPPEDACCEL |
		MACHINESTATE::CROSSEDLAPLINE_Q |
		MACHINESTATE::JUSTLANDED |
		MACHINESTATE::AIRBORNEMORE0_2S_Q |
		MACHINESTATE::AIRBORNE);

	soa->state_2[soa_index] &= 0xfffffcff;
	soa->terrain_state[soa_index] = 0;
	soa->dashplate_heat_multiplier[soa_index] = 1.0f;

	const SimTransform previous_physical = LOAD_TRANSFORM(basis_physical_other);
	const SimVec3 previous_position = LOAD_VEC3(position_old);
	STORE_TRANSFORM(basis_physical_other, LOAD_TRANSFORM(basis_physical));
	STORE_VEC3(position_old_dupe, LOAD_VEC3(position_current));
	STORE_VEC3(position_old, LOAD_VEC3(position_old_dupe));

	const int point_base = soa_index * 4;
	bool any_drift = false;
	for (int i = 0; i < 4; ++i) {
		if (soa->tilt_state[point_base + i] & TILTSTATE::DRIFT) {
			any_drift = true;
		}
	}
	const SimVec3x4 tilt_pos_old = mxt_transform_points4(
		previous_physical,
		previous_position,
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base) + sim_load4(soa->tilt_force + point_base) - sim_load4(soa->tilt_rest_length + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	sim_store4(soa->tilt_pos_old_x + point_base, tilt_pos_old.x);
	sim_store4(soa->tilt_pos_old_y + point_base, tilt_pos_old.y);
	sim_store4(soa->tilt_pos_old_z + point_base, tilt_pos_old.z);
	for (int i = 0; i < 4; ++i) {
		const int p = point_base + i;
		if (any_drift) {
			soa->tilt_state[p] |= TILTSTATE::DRIFT;
		}
	}


	SimVec3 ground_normal = SimVec3(0, 1, 0);
	if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) {
		ground_normal = get_avg_track_normal_from_tilt_corners(scratch);
	}

	bool all_airborne = true;
	const SimVec3 current_position = LOAD_VEC3(position_current);
	for (int i = 0; i < 4; ++i) {
		const int p = POINT_INDEX(i);
		if ((soa->tilt_state[p] & TILTSTATE::AIRBORNE) == 0) {
			all_airborne = false;
		}
		STORE_WALL_VEC3(pos_a, p, current_position);
	}

	if (all_airborne) {
		soa->machine_state[soa_index] |= MACHINESTATE::AIRBORNE;
		if (soa->air_time[soa_index] < 180)
			soa->air_time[soa_index] += 1;
		if (soa->air_time[soa_index] > 10)
			soa->machine_state[soa_index] |= MACHINESTATE::AIRBORNEMORE0_2S_Q;
	} else {
		if (soa->air_time[soa_index] != 0)
			soa->machine_state[soa_index] |= MACHINESTATE::JUSTLANDED;
		soa->machine_state[soa_index] &= ~MACHINESTATE::AIRBORNEMORE0_2S_Q;
		soa->state_2[soa_index] &= ~2u;
	}

	soa->turning_related[soa_index] = 0.0f;
	soa->visual_rotation_z[soa_index] *= 0.8f;
	soa->visual_rotation_x[soa_index] *= 0.9f;

	if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE)
	{
		if (soa->frames_since_start_2[soa_index] != 0)
			soa->frames_since_start_2[soa_index] = std::min(255u, soa->frames_since_start_2[soa_index] + 1);
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0)
	{
		soa->energy[soa_index] += 1.111111f * soa->energy_recharge_mult[soa_index];
		if (soa->energy[soa_index] > soa->calced_max_energy[soa_index])
		{
			soa->energy[soa_index] = soa->calced_max_energy[soa_index];
		}
	}

	float vel_mag = LOAD_VEC3(velocity).length();
	soa->speed_kmh[soa_index] = 216.0f * (vel_mag / std::max(soa->stat_weight[soa_index], 0.001f));

	if ((soa->machine_state[soa_index] & MACHINESTATE::RETIRED) != 0 &&
		(soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		if (soa->speed_kmh[soa_index] >= 10.0f)
			STORE_VEC3(velocity, LOAD_VEC3(velocity) * 0.9f);
		else
			STORE_VEC3(velocity, SimVec3());
	}

	handle_attack_states();

	if (soa->car_hit_invincibility[soa_index] == 0) {
		if (soa->machine_state[soa_index] & MACHINESTATE::JUSTHITVEHICLE_Q)
			soa->car_hit_invincibility[soa_index] = 6;
	} else {
		soa->car_hit_invincibility[soa_index] -= 1;
	}
	if (soa->breakdown_frame_counter[soa_index] > 0){
		soa->breakdown_frame_counter[soa_index] -= 1;
	}

	STORE_VEC3(position_old_2, LOAD_VEC3(position_current));

	soa->frames_since_start[soa_index] += 1;

	return ground_normal;
};

float PhysicsCar::get_current_stage_min_y() const
{
	return -100000.0f;
};


void PhysicsCar::broken_down_fling_physics()
{
	// semi-random numbers by hashing positions and angular LOAD_VEC3(velocity)
	uint32_t hash = ((uint32_t)soa->position_current_x[soa_index] ^
		(uint32_t)soa->position_current_y[soa_index] ^
		(uint32_t)soa->position_current_z[soa_index] ^
		(uint32_t)soa->velocity_angular_x[soa_index]) & 0xffff;
	uint32_t hash2 = ((uint32_t)soa->position_current_x[soa_index] ^
		(uint32_t)soa->position_current_y[soa_index] ^
		(uint32_t)soa->position_current_z[soa_index] ^
		(uint32_t)soa->velocity_angular_y[soa_index]) & 0xffff;

	float rand_x = 2.0f * ((float)hash / 65536.0f) - 1.0f;
	float rand_z = 2.0f * ((float)hash2 / 65536.0f) - 1.0f;

	rand_x += (rand_x <= 0.0f) ? -0.5f : 0.5f;
	rand_z += (rand_z <= 0.0f) ? -0.5f : 0.5f;

	float damping_factor = std::clamp((soa->speed_kmh[soa_index] * 0.0015 - 1.0), 0.0, 1.0);
	float force = damping_factor * (450.0 / 216.0) * soa->stat_weight[soa_index];

	// put impulse in world space
	SimVec3 torque_impulse = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(-rand_x, 0.5f, -rand_z));
	torque_impulse *= force;

	soa->state_2[soa_index] |= 2;

	// back into local space
	SimVec3 rotated_torque_impulse = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), torque_impulse);
	rotated_torque_impulse *= 0.5f;

	SimVec3 torque = {
		-(rand_z * rotated_torque_impulse.y),
		-(rand_x * rotated_torque_impulse.z - rand_z * rotated_torque_impulse.x),
		-(-rand_x * rotated_torque_impulse.y)
	};

	// used for random visual rotation of the vehicle when bouncing
	soa->unk_vec3_0x4f0_x[soa_index] = soa->velocity_angular_x[soa_index] + torque.x;
	soa->unk_vec3_0x4f0_y[soa_index] = soa->velocity_angular_y[soa_index] + torque.y;
	soa->unk_vec3_0x4f0_z[soa_index] = soa->velocity_angular_z[soa_index] + torque.z;

	// use previously calculated force for actual bounce
	SimVec3 boost_vec;
	boost_vec = set_vec3_length(LOAD_VEC3(track_surface_normal), force * 0.2);
	ADD_VEC3(velocity, boost_vec);

	if (soa->frames_since_death[soa_index] > 30) {
		soa->state_2[soa_index] |= 0x20;

	// visually orient vehicle back to track once it settles on the road and stops bouncing
	// disabled here because we do this in GDScript instead

	//	SimVec3 up = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0.0f, 1.0f, 0.0f));
	//	if (up.dot(LOAD_VEC3(track_surface_normal)) < 0.99f) {
	//		SimVec3 axis = vec3_cross(-up, LOAD_VEC3(track_surface_normal));
	//		float axis_len = axis.length();
	//		if (axis_len > 0.1f) {
	//			float dot = up.dot(LOAD_VEC3(track_surface_normal));
	//			int angle = (int)(1365.0f * (1.0f - dot * dot));
	//			
	//			//make_axis_angle_quat(&q, &axis, (short)angle);
	//		}
	//	}
	}

	if (soa->frames_since_death[soa_index] < 2) {
		soa->frames_since_death[soa_index] = 2;
	} else if (++soa->frames_since_death[soa_index] > 239) {
		soa->frames_since_death[soa_index] = 240;
	}
};

void PhysicsCar::breakdown_physics()
{
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		broken_down_fling_physics();
	}

	if (soa->frames_since_death[soa_index] < 0x3c) {
		soa->unk_vec3_0x4e4_x[soa_index] += soa->unk_vec3_0x4f0_x[soa_index];
		soa->unk_vec3_0x4e4_y[soa_index] += soa->unk_vec3_0x4f0_y[soa_index];
		soa->unk_vec3_0x4e4_z[soa_index] += soa->unk_vec3_0x4f0_z[soa_index];
	}

	soa->some_breakdown_int[soa_index]++;

	if (soa->some_breakdown_int[soa_index] > 0xef) {
		if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
			soa->velocity_x[soa_index] = 0.0f;
			soa->velocity_y[soa_index] = 0.0f;
			soa->velocity_z[soa_index] = 0.0f;
			STORE_VEC3(position_current, LOAD_VEC3(position_old));
			if ((soa->state_2[soa_index] & 0x90) == 0) {
				soa->state_2[soa_index] = soa->state_2[soa_index] | 0x80;
				soa->state_2[soa_index] = soa->state_2[soa_index] | 0x100;
			}
		}
		soa->some_breakdown_int[soa_index] = 0xf0;
	}
}

bool PhysicsCar::handle_machine_crash(int unk_int) {
	uint32_t state = soa->machine_state[soa_index];
	bool result = false;

	if (!(state & MACHINESTATE::FALLOUT)) {
		if (state & MACHINESTATE::ZEROHP) {
			if (!soa->machine_crashed[soa_index] && !(state & MACHINESTATE::COMPLETEDRACE_1_Q)) {
				soa->machine_crashed[soa_index] = true;
			}
		}

		if ((state & MACHINESTATE::B1) &&
			(state & MACHINESTATE::ZEROHP) &&
			((state & 0x2810000) || unk_int)) {
			result = true;
	}

} else {
	if ((state & MACHINESTATE::B1) || (state & MACHINESTATE::COMPLETEDRACE_1_Q)) {
		result = true;
	}

	if (!(state & MACHINESTATE::COMPLETEDRACE_1_Q) && !soa->machine_crashed[soa_index]) {
		soa->machine_crashed[soa_index] = true;
	}
}

if ((state & MACHINESTATE::DIEDTHISFRAMEOOB_Q) && (state & MACHINESTATE::B1)) {
	result = true;
}

if (state & MACHINESTATE::B29) {
	result = false;
}
return result;
}

void PhysicsCar::handle_machine_damage_and_visuals()
{
	if ((soa->state_2[soa_index] & 0x8u) == 0)
		return;


	if (soa->frames_since_death[soa_index] != 0)
	{
		breakdown_physics();
	}

	if (soa->terrain_state[soa_index] & TERRAIN::LAVA) {
		// Lava damage handling is not yet implemented
		if ((soa->state_2[soa_index] & 0x200u) && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP)) {
			return;
		}
	}

	const int point_base = soa_index * 4;
	sim_store4(soa->tilt_pos_old_x + point_base, sim_load4(soa->tilt_pos_x + point_base));
	sim_store4(soa->tilt_pos_old_y + point_base, sim_load4(soa->tilt_pos_y + point_base));
	sim_store4(soa->tilt_pos_old_z + point_base, sim_load4(soa->tilt_pos_z + point_base));
	const SimVec3x4 tilt_pos = mxt_transform_points4(
		LOAD_TRANSFORM(basis_physical),
		LOAD_VEC3(position_current),
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base) + sim_load4(soa->tilt_force + point_base) - sim_load4(soa->tilt_rest_length + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	sim_store4(soa->tilt_pos_x + point_base, tilt_pos.x);
	sim_store4(soa->tilt_pos_y + point_base, tilt_pos.y);
	sim_store4(soa->tilt_pos_z + point_base, tilt_pos.z);
	sim_store4(soa->wall_pos_a_x + point_base, sim_load4(soa->wall_pos_b_x + point_base));
	sim_store4(soa->wall_pos_a_y + point_base, sim_load4(soa->wall_pos_b_y + point_base));
	sim_store4(soa->wall_pos_a_z + point_base, sim_load4(soa->wall_pos_b_z + point_base));
	const SimVec3x4 wall_pos = mxt_transform_points4(
		LOAD_TRANSFORM(basis_physical),
		LOAD_VEC3(position_current),
		sim_load4(soa->wall_offset_x + point_base),
		sim_load4(soa->wall_offset_y + point_base),
		sim_load4(soa->wall_offset_z + point_base));
	sim_store4(soa->wall_pos_b_x + point_base, wall_pos.x);
	sim_store4(soa->wall_pos_b_y + point_base, wall_pos.y);
	sim_store4(soa->wall_pos_b_z + point_base, wall_pos.z);

	if ((soa->state_2[soa_index] & 0x10u) == 0) {
		float y_pos = soa->position_current_y[soa_index];
		float track_min_y = -1000000.0f; // Placeholder until track data is available
		if (y_pos < -5000.0f || y_pos < (track_min_y - 900.0f)) {
			return;
		}
	}

	if (soa->position_current_y[soa_index] < -10000.0f) {
		soa->position_current_y[soa_index] = -10000.0f;
		STORE_VEC3(velocity, SimVec3());
	}

	//create_machine_visual_transform();

	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		float world_speed = LOAD_VEC3(velocity).length();
		if (std::abs(soa->stat_weight[soa_index]) > 0.0001f)
			soa->speed_kmh[soa_index] = 216.0f * (world_speed / soa->stat_weight[soa_index]);
		else
			soa->speed_kmh[soa_index] = 0.0f;

		float current_speed_for_max_check = soa->speed_kmh[soa_index];
		bool no_bad_state_flags =
		(soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
			MACHINESTATE::TOOKDAMAGE)) == 0;

		(void)current_speed_for_max_check;
		(void)no_bad_state_flags;
	}
	bool crashed = handle_machine_crash(1);
	if (crashed == false) {
		if ((soa->machine_state[soa_index] & (MACHINESTATE::RETIRED|MACHINESTATE::B10|MACHINESTATE::B1)) == 0) {
			if (((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) && (soa->frames_since_death[soa_index] == 0)) {
				broken_down_fling_physics();
			}
		}
		else {
			if ((soa->machine_state[soa_index] & (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) == (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) {
				if (10.0f <= soa->speed_kmh[soa_index])
				{
					STORE_VEC3(velocity, LOAD_VEC3(velocity) * 0.95f);
				}else{
					soa->velocity_x[soa_index] = 0.0f;
					soa->velocity_y[soa_index] = 0.0f;
					soa->velocity_z[soa_index] = 0.0f;
					STORE_VEC3(position_current, LOAD_VEC3(position_old));
					soa->machine_state[soa_index] = soa->machine_state[soa_index] | MACHINESTATE::RETIRED;
					if ((soa->state_2[soa_index] & 0x80) == 0) {
						soa->state_2[soa_index] = soa->state_2[soa_index] | 0x100;
					}
					soa->state_2[soa_index] = soa->state_2[soa_index] | 0x80;
				}
			}
		}
	}
};

void PhysicsCar::handle_machine_damage_and_visuals_tail()
{
	if ((soa->state_2[soa_index] & 0x8u) == 0)
		return;

	if (soa->frames_since_death[soa_index] != 0)
	{
		breakdown_physics();
	}

	if (soa->terrain_state[soa_index] & TERRAIN::LAVA) {
		if ((soa->state_2[soa_index] & 0x200u) && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP)) {
			return;
		}
	}

	if ((soa->state_2[soa_index] & 0x10u) == 0) {
		float y_pos = soa->position_current_y[soa_index];
		float track_min_y = -1000000.0f;
		if (y_pos < -5000.0f || y_pos < (track_min_y - 900.0f)) {
			return;
		}
	}

	if (soa->position_current_y[soa_index] < -10000.0f) {
		soa->position_current_y[soa_index] = -10000.0f;
		STORE_VEC3(velocity, SimVec3());
	}

	bool crashed = handle_machine_crash(1);
	if (crashed == false) {
		if ((soa->machine_state[soa_index] & (MACHINESTATE::RETIRED|MACHINESTATE::B10|MACHINESTATE::B1)) == 0) {
			if (((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) && (soa->frames_since_death[soa_index] == 0)) {
				broken_down_fling_physics();
			}
		}
		else {
			if ((soa->machine_state[soa_index] & (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) == (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) {
				if (10.0f <= soa->speed_kmh[soa_index])
				{
					STORE_VEC3(velocity, LOAD_VEC3(velocity) * 0.95f);
				}else{
					soa->velocity_x[soa_index] = 0.0f;
					soa->velocity_y[soa_index] = 0.0f;
					soa->velocity_z[soa_index] = 0.0f;
					STORE_VEC3(position_current, LOAD_VEC3(position_old));
					soa->machine_state[soa_index] = soa->machine_state[soa_index] | MACHINESTATE::RETIRED;
					if ((soa->state_2[soa_index] & 0x80) == 0) {
						soa->state_2[soa_index] = soa->state_2[soa_index] | 0x100;
					}
					soa->state_2[soa_index] = soa->state_2[soa_index] | 0x80;
				}
			}
		}
	}
}

static inline float safe_inverse_floor_scale(float scale)
{
	return fabsf(scale) > 0.00001f ? 1.0f / scale : 0.0f;
}

static bool ray_unit_circle_xy(const SimVec2 &p, const SimVec2 &dir, SimVec2 &out_hit)
{
	const float a = dir.length_squared();
	if (a <= 0.0000001f) {
		return false;
	}
	const float b = 2.0f * (p.x * dir.x + p.y * dir.y);
	const float c = p.length_squared() - 1.0f;
	const float disc = b * b - 4.0f * a * c;
	if (disc < 0.0f) {
		return false;
	}
	const float sqrt_disc = sqrtf(disc);
	const float inv_2a = 0.5f / a;
	const float t0 = (-b - sqrt_disc) * inv_2a;
	const float t1 = (-b + sqrt_disc) * inv_2a;
	float t = FLT_MAX;
	if (t0 >= 0.0f) {
		t = t0;
	}
	if (t1 >= 0.0f && t1 < t) {
		t = t1;
	}
	if (t == FLT_MAX) {
		return false;
	}
	out_hit = p + dir * t;
	return true;
}

static inline void floor_consider_ray_hit(float t, const SimVec2 &hit, float &best_t, SimVec2 &best_hit)
{
	if (t >= 0.0f && t < best_t) {
		best_t = t;
		best_hit = hit;
	}
}

static inline float cross2(const SimVec2 &a, const SimVec2 &b)
{
	return a.x * b.y - a.y * b.x;
}

static bool road_shape_is_open_pipe_or_rect(const RoadShape *shape)
{
	return shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
}

struct PipeFloorTrace
{
	const char *reason = "ok";
	SimVec2 center;
	SimVec2 ray_dir;
	SimVec2 hit;
	float ray_dir_len2 = 0.0f;
	bool open_shape = false;
	bool lip_valid = false;
	bool center_on_road_side = false;
	bool center_in_road_arc = false;
	bool center_on_open_road_side = false;
};

static SimVec2 sample_road_local_xy(const RoadShape *shape, float tx, float ty)
{
	SimVec3 pos;
	SimVec3 dx;
	SimVec3 dy;
	shape->get_local_surface_at_time(pos, dx, dy, SimVec2(tx, ty));
	return SimVec2(pos.x, pos.y);
}

static bool open_road_lip_relation(
	const TrackSegment &segment,
	const SimVec2 &p,
	float ty,
	bool &out_center_on_road_side,
	bool &out_center_in_road_arc)
{
	const RoadShape *shape = segment.road_shape;
	const SimVec2 left = sample_road_local_xy(shape, -1.0f, ty);
	const SimVec2 middle = sample_road_local_xy(shape, 0.0f, ty);
	const SimVec2 right = sample_road_local_xy(shape, 1.0f, ty);
	const SimVec2 lip = right - left;
	if (lip.length_squared() <= 0.000001f) {
		return false;
	}

	const float road_side = cross2(lip, middle - left);
	if (fabsf(road_side) <= 0.000001f) {
		return false;
	}

	SimVec2 center_t;
	SimVec3 center_spatial(p.x, p.y, ty);
	segment.road_shape->find_t_from_relative_pos(center_t, center_spatial);
	const float center_side = cross2(lip, p - left);
	out_center_on_road_side = (center_side * road_side) >= -0.0001f;
	out_center_in_road_arc = center_t.x >= -1.0001f && center_t.x <= 1.0001f;
	return true;
}

static bool ray_rounded_rect_xy(const SimVec2 &p, const SimVec2 &dir, float width, float height, float radius, SimVec2 &out_hit)
{
	const float w2 = fabsf(width) * 0.5f;
	const float h2 = fabsf(height) * 0.5f;
	const float r = fminf(fmaxf(radius, 0.0f), fminf(w2, h2));
	const float inner_w = fmaxf(w2 - r, 0.0f);
	const float inner_h = fmaxf(h2 - r, 0.0f);
	float best_t = FLT_MAX;
	SimVec2 best_hit;

	if (fabsf(dir.x) > 0.0000001f) {
		float t = (w2 - p.x) / dir.x;
		SimVec2 hit = p + dir * t;
		if (hit.y >= -inner_h - 0.0001f && hit.y <= inner_h + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
		t = (-w2 - p.x) / dir.x;
		hit = p + dir * t;
		if (hit.y >= -inner_h - 0.0001f && hit.y <= inner_h + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
	}
	if (fabsf(dir.y) > 0.0000001f) {
		float t = (h2 - p.y) / dir.y;
		SimVec2 hit = p + dir * t;
		if (hit.x >= -inner_w - 0.0001f && hit.x <= inner_w + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
		t = (-h2 - p.y) / dir.y;
		hit = p + dir * t;
		if (hit.x >= -inner_w - 0.0001f && hit.x <= inner_w + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
	}
	if (r > 0.0000001f) {
		for (int sx = -1; sx <= 1; sx += 2) {
			for (int sy = -1; sy <= 1; sy += 2) {
				const SimVec2 center(inner_w * static_cast<float>(sx), inner_h * static_cast<float>(sy));
				const SimVec2 rel = p - center;
				const float a = dir.length_squared();
				const float b = 2.0f * (rel.x * dir.x + rel.y * dir.y);
				const float c = rel.length_squared() - r * r;
				const float disc = b * b - 4.0f * a * c;
				if (disc < 0.0f || a <= 0.0000001f) {
					continue;
				}
				const float sqrt_disc = sqrtf(disc);
				const float inv_2a = 0.5f / a;
				const float roots[2] = {
					(-b - sqrt_disc) * inv_2a,
					(-b + sqrt_disc) * inv_2a,
				};
				for (int i = 0; i < 2; ++i) {
					const float t = roots[i];
					const SimVec2 hit = p + dir * t;
					if (static_cast<float>(sx) * (hit.x - center.x) >= -0.0001f &&
						static_cast<float>(sy) * (hit.y - center.y) >= -0.0001f) {
						floor_consider_ray_hit(t, hit, best_t, best_hit);
					}
				}
			}
		}
	}
	if (best_t == FLT_MAX) {
		return false;
	}
	out_hit = best_hit;
	return true;
}

static bool project_machine_down_to_road_cross_section(
	const TrackSegment &segment,
	const RoadTransform &root,
	const SimTransform &machine_transform,
	const SimVec3 &machine_pos,
	SimVec2 &road_t,
	SimVec3 &spatial_t,
	bool &center_on_open_road_side,
	PipeFloorTrace *trace)
{
	center_on_open_road_side = false;
	const SimVec3 root_forward = root.t3d.basis.get_column(2);
	const SimVec3 machine_up = machine_transform.basis.get_column(1);
	const SimVec3 world_down = -machine_up.slide(root_forward);
	if (world_down.length_squared() <= 0.000001f) {
		if (trace) {
			trace->reason = "cross_section_down_degenerate";
		}
		return false;
	}

	const SimVec3 local_pos = root.t3d.xform_inv(machine_pos);
	const SimVec3 local_down_unscaled = root.t3d.basis.xform_inv(world_down);
	const SimVec2 p(
		local_pos.x * safe_inverse_floor_scale(root.scale.x),
		local_pos.y * safe_inverse_floor_scale(root.scale.y));
	const SimVec2 dir(
		local_down_unscaled.x * safe_inverse_floor_scale(root.scale.x),
		local_down_unscaled.y * safe_inverse_floor_scale(root.scale.y));
	if (trace) {
		trace->center = p;
		trace->ray_dir = dir;
		trace->ray_dir_len2 = dir.length_squared();
	}
	if (dir.length_squared() <= 0.000001f) {
		if (trace) {
			trace->reason = "local_ray_degenerate";
		}
		return false;
	}

	if (road_shape_is_open_pipe_or_rect(segment.road_shape)) {
		bool center_on_road_side = false;
		bool center_in_road_arc = false;
		if (trace) {
			trace->open_shape = true;
		}
		if (open_road_lip_relation(segment, p, road_t.y, center_on_road_side, center_in_road_arc)) {
			if (trace) {
				trace->lip_valid = true;
				trace->center_on_road_side = center_on_road_side;
				trace->center_in_road_arc = center_in_road_arc;
			}
			if (!center_on_road_side && !center_in_road_arc) {
				if (trace) {
					trace->reason = "open_hole_side_outside_arc";
				}
				return false;
			}
			center_on_open_road_side = center_on_road_side && center_in_road_arc;
			if (trace) {
				trace->center_on_open_road_side = center_on_open_road_side;
			}
		}
	}

	SimVec2 hit;
	switch (segment.road_shape->shape_type) {
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN:
			if (!ray_unit_circle_xy(p, dir, hit)) {
				if (trace) {
					trace->reason = "pipe_ray_miss";
				}
				return false;
			}
			break;
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN: {
			RoadShapeRoundedRect *rect = static_cast<RoadShapeRoundedRect *>(segment.road_shape);
			if (!ray_rounded_rect_xy(
					p,
					dir,
					rect->width->sample(road_t.y),
					rect->height->sample(road_t.y),
					rect->radius->sample(road_t.y),
					hit)) {
				if (trace) {
					trace->reason = "rounded_rect_ray_miss";
				}
				return false;
			}
			break;
		}
		default:
			if (trace) {
				trace->reason = "unsupported_shape";
			}
			return false;
	}

	if (trace) {
		trace->hit = hit;
	}
	spatial_t.x = hit.x;
	spatial_t.y = hit.y;
	segment.road_shape->find_t_from_relative_pos(road_t, spatial_t);
	return true;
}

void PhysicsCar::sample_mesh_floor_with_seed(CollisionData &out_collision, const SimVec3 &point, float max_distance, uint8_t mask, int start_idx, bool allow_global_fallback, TrackQueryScratch &scratch, bool build_surface)
{
	RaceTrack *track = soa->current_track[soa_index];
	if (!track) {
		out_collision.collided = false;
		out_collision.road_data.cp_idx = -1;
		out_collision.mesh_triangle_index = -1;
		return;
	}
	track->sample_mesh_floor_fast(
		out_collision,
		point,
		max_distance,
		mask,
		start_idx,
		allow_global_fallback,
		&scratch,
		soa->last_mesh_floor_triangle[soa_index],
		build_surface,
		true);
	if (out_collision.collided) {
		if (out_collision.mesh_triangle_index < 0) {
			godot::UtilityFunctions::printerr(godot::String("MXT mesh floor sample produced no triangle index"));
			std::abort();
		}
		soa->last_mesh_floor_triangle[soa_index] = out_collision.mesh_triangle_index;
	}
}

bool PhysicsCar::find_floor_beneath_machine(TrackQueryScratch &scratch)
{
	soa->road_sample[soa_index].terrain = 0;
	soa->road_sample[soa_index].road_t = SimVec2();
	soa->road_sample[soa_index].spatial_t = SimVec3();
	soa->road_sample[soa_index].closest_surface = SimTransform();
	soa->road_sample[soa_index].closest_root = RoadTransform();
	bool stay_on = false;
	bool cylinder = false;
	bool pipe = false;
	TrackSegment *floor_seg = &soa->current_track[soa_index]->segments[soa->current_track[soa_index]->checkpoints[soa->current_checkpoint[soa_index]].road_segment];
        cylinder = floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER ||
                floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN;
        bool rect = (floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT || floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN);
        pipe = floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE || floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
	stay_on = floor_seg->analytic_collision_enabled && (pipe || cylinder || rect);
	const bool trace_mesh_floor = trace_mesh_floor_for_car(soa, soa_index);

	if (!stay_on)
	{
		bool sweep_hit_occurred = false;
		bool nearest_mesh_sample = false;
		bool local_mesh_sample = false;
		CollisionData hit{};
		hit.road_data.cp_idx = -1;
		hit.mesh_triangle_index = -1;
		const SimVec3 machine_up_ws = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0.0f, 1.0f, 0.0f));
		auto orient_mesh_floor_hit = [&](CollisionData &mesh_hit) {
			if (mesh_hit.collided && mesh_hit.collision_normal.dot(machine_up_ws) < 0.0f) {
				mesh_hit.collision_normal *= -1.0f;
				mesh_hit.collision_face_normal *= -1.0f;
				mesh_hit.road_data.closest_surface.basis[1] *= -1.0f;
				mesh_hit.road_data.closest_surface.basis[2] *= -1.0f;
			}
		};
		SimVec3 p0_sweep_start_ws = mxt_transform_point(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(position_current), SimVec3(0.0f, 1.0f, 0.0f));
		SimVec3 p1_sweep_end_ws = mxt_transform_point(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(position_current), SimVec3(0.0f, -20.0f, 0.0f));
		STORE_VEC3(position_bottom, p1_sweep_end_ws);
		if (floor_seg->analytic_collision_enabled) {
			SimVec2 center_road_t;
			SimVec3 center_spatial_t;
			RoadTransform center_root;
			soa->current_track[soa_index]->convert_point_to_road(
				soa->current_checkpoint[soa_index],
				LOAD_VEC3(position_current),
				center_road_t,
				center_spatial_t,
				nullptr,
				&center_root,
				nullptr);
			if (center_road_t.x != -1000.0f &&
				center_road_t.x >= -1.01f && center_road_t.x <= 1.01f &&
				center_road_t.y >= -0.001f && center_road_t.y <= 1.001f &&
				soa->current_track[soa_index]->analytic_road_sample_has_hole(soa->current_checkpoint[soa_index], center_road_t)) {
				soa->road_sample[soa_index].terrain = TERRAIN::HOLE;
				soa->road_sample[soa_index].road_t = center_road_t;
				soa->road_sample[soa_index].spatial_t = center_spatial_t;
				soa->road_sample[soa_index].closest_root = center_root;
				STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
				soa->height_above_track[soa_index] = 0.0f;
				return false;
			}
		}
		if (floor_seg->analytic_collision_enabled) {
			soa->current_track[soa_index]->cast_vs_track_fast(hit, p0_sweep_start_ws,
				LOAD_VEC3(position_bottom),
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::WANTS_TERRAIN | CAST_FLAGS::SAMPLE_FROM_P0,
				soa->current_checkpoint[soa_index],
				false,
				&scratch);
			sweep_hit_occurred = hit.collided && hit.road_data.road_t.x >= -1.0f && hit.road_data.road_t.x <= 1.0f && hit.road_data.road_t.y > -0.001f && hit.road_data.road_t.y < 1.001f;
		} else {
			CollisionData nearest_hit{};
			sample_mesh_floor_with_seed(
				nearest_hit,
				LOAD_VEC3(position_current),
				8.0f,
				CAST_FLAGS::WANTS_TRACK,
				soa->current_checkpoint[soa_index],
				false,
				scratch);
			if (nearest_hit.collided) {
				orient_mesh_floor_hit(nearest_hit);
				hit = nearest_hit;
				sweep_hit_occurred = true;
				nearest_mesh_sample = true;
				local_mesh_sample = true;
			}
		}
		if (!sweep_hit_occurred && floor_seg->analytic_collision_enabled) {
			CollisionData mesh_hit{};
			sample_mesh_floor_with_seed(
				mesh_hit,
				LOAD_VEC3(position_current),
				8.0f,
				CAST_FLAGS::WANTS_TRACK,
				soa->current_checkpoint[soa_index],
				true,
				scratch);
			if (mesh_hit.collided) {
				orient_mesh_floor_hit(mesh_hit);
				hit = mesh_hit;
				sweep_hit_occurred = true;
				nearest_mesh_sample = true;
				local_mesh_sample = false;
			}
		}
		if (!sweep_hit_occurred && !floor_seg->analytic_collision_enabled) {
			soa->current_track[soa_index]->cast_vs_track_fast(hit, p0_sweep_start_ws,
				LOAD_VEC3(position_bottom),
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
				soa->current_checkpoint[soa_index],
				false,
				&scratch);
			sweep_hit_occurred = hit.collided && hit.road_data.road_t.x >= -1.0f && hit.road_data.road_t.x <= 1.0f && hit.road_data.road_t.y > -0.001f && hit.road_data.road_t.y < 1.001f;
		}
		if (!sweep_hit_occurred && !floor_seg->analytic_collision_enabled) {
			sample_mesh_floor_with_seed(
				hit,
				LOAD_VEC3(position_current),
				8.0f,
				CAST_FLAGS::WANTS_TRACK,
				soa->current_checkpoint[soa_index],
				true,
				scratch);
			sweep_hit_occurred = hit.collided;
			nearest_mesh_sample = hit.collided;
		}
		if (!floor_seg->analytic_collision_enabled) {
			orient_mesh_floor_hit(hit);
		}
		soa->road_sample[soa_index].terrain = hit.road_data.terrain;
		soa->road_sample[soa_index].road_t = hit.road_data.road_t;
		soa->road_sample[soa_index].spatial_t = hit.road_data.spatial_t;
		soa->road_sample[soa_index].closest_surface = hit.road_data.closest_surface;
		float contact_dist_metric = 0.0f;
		if (sweep_hit_occurred) {
			STORE_VEC3(track_surface_pos, hit.collision_point);
			if ((hit.road_data.terrain & TERRAIN::HOLE) != 0u ||
				(hit.road_data.cp_idx >= 0 && soa->current_track[soa_index]->analytic_road_sample_has_hole(hit.road_data.cp_idx, hit.road_data.road_t))) {
				soa->road_sample[soa_index].terrain |= TERRAIN::HOLE;
				STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
				STORE_VEC3(position_bottom, p1_sweep_end_ws);
				soa->height_above_track[soa_index] = 0.0f;
				return false;
			}
			if (nearest_mesh_sample) {
				const float signed_surface_distance = (LOAD_VEC3(position_current) - hit.collision_point).dot(hit.collision_normal);
				contact_dist_metric = local_mesh_sample ? 20.0f - fabsf(signed_surface_distance) : 20.0f - signed_surface_distance;
			} else {
				float dist_p0_to_surface =
				LOAD_VEC3(position_current).distance_to(hit.collision_point);
				contact_dist_metric = 20.0f - dist_p0_to_surface;
			}
		}
		if (nearest_mesh_sample && !local_mesh_sample && contact_dist_metric > 20.1f) {
			sweep_hit_occurred = false;
		}
		if (trace_mesh_floor) {
			const SimVec3 pos = LOAD_VEC3(position_current);
			godot::UtilityFunctions::print(
				godot::String("MXT_MESH_FLOOR_CENTER tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
				godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
				godot::String(" analytic="), floor_seg->analytic_collision_enabled,
				godot::String(" stay_on="), stay_on,
				godot::String(" sweep="), sweep_hit_occurred,
				godot::String(" nearest="), nearest_mesh_sample,
				godot::String(" local="), local_mesh_sample,
				godot::String(" hit="), hit.collided,
				godot::String(" mesh_tri="), static_cast<int64_t>(hit.mesh_triangle_index),
				godot::String(" contact="), contact_dist_metric,
				godot::String(" terrain=0x"), godot::String::num_int64(hit.road_data.terrain, 16),
				godot::String(" pos=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"),
				godot::String(" n=("), hit.collision_normal.x, godot::String(","), hit.collision_normal.y, godot::String(","), hit.collision_normal.z, godot::String(")"),
				godot::String(" road_t=("), hit.road_data.road_t.x, godot::String(","), hit.road_data.road_t.y, godot::String(")"));
		}
		if (sweep_hit_occurred && contact_dist_metric > 0.0f) {
			STORE_VEC3(track_surface_normal, hit.collision_normal);
			soa->height_above_track[soa_index] = contact_dist_metric;
			return true;
		} else {
			STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
			STORE_VEC3(position_bottom, p1_sweep_end_ws);
			soa->height_above_track[soa_index] = 0.0f;
			return false;
		}
	}

	SimVec2 road_t_sample_raw;
	SimVec3 spatial_t_sample;

	RoadTransform root;
	RoadTransform root_derivative;
	const TrackSegment &segment     = soa->current_track[soa_index]->segments[soa->current_track[soa_index]->checkpoints[soa->current_checkpoint[soa_index]].road_segment];
	const bool trace_pipe_floor =
		DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_PIPE_FLOOR) &&
		(soa->global_start + soa_index) == 0 &&
		(pipe || rect);
	auto trace_floor = [&](const char *reason, const PipeFloorTrace *trace, const SimVec2 &road_t, const SimVec3 &spatial_t, float surface_dist) {
		if (!trace_pipe_floor) {
			return;
		}
		godot::UtilityFunctions::print(
			godot::String("MXT_PIPE_FLOOR reason="), godot::String(reason),
			godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
			godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
			godot::String(" shape="), static_cast<int64_t>(segment.road_shape->shape_type),
			godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(soa->machine_state[soa_index]), 16),
			godot::String(" road_t=("), road_t.x, godot::String(","), road_t.y, godot::String(")"),
			godot::String(" spatial=("), spatial_t.x, godot::String(","), spatial_t.y, godot::String(","), spatial_t.z, godot::String(")"),
			godot::String(" pos=("), soa->position_current_x[soa_index], godot::String(","), soa->position_current_y[soa_index], godot::String(","), soa->position_current_z[soa_index], godot::String(")"),
			godot::String(" vel=("), soa->velocity_x[soa_index], godot::String(","), soa->velocity_y[soa_index], godot::String(","), soa->velocity_z[soa_index], godot::String(")"),
			godot::String(" surf_dist="), surface_dist,
			godot::String(" h="), soa->height_above_track[soa_index],
			godot::String(" push_track_len="), LOAD_VEC3(collision_push_track).length(),
			godot::String(" push_rail_len="), LOAD_VEC3(collision_push_rail).length(),
			godot::String(" trace_reason="), godot::String(trace ? trace->reason : "none"),
			godot::String(" ray_len2="), trace ? trace->ray_dir_len2 : 0.0f,
			godot::String(" center=("), trace ? trace->center.x : 0.0f, godot::String(","), trace ? trace->center.y : 0.0f, godot::String(")"),
			godot::String(" ray=("), trace ? trace->ray_dir.x : 0.0f, godot::String(","), trace ? trace->ray_dir.y : 0.0f, godot::String(")"),
			godot::String(" hit=("), trace ? trace->hit.x : 0.0f, godot::String(","), trace ? trace->hit.y : 0.0f, godot::String(")"),
			godot::String(" lip_valid="), trace ? trace->lip_valid : false,
			godot::String(" road_side="), trace ? trace->center_on_road_side : false,
			godot::String(" in_arc="), trace ? trace->center_in_road_arc : false,
			godot::String(" on_open_road="), trace ? trace->center_on_open_road_side : false);
	};
	soa->current_track[soa_index]->convert_point_to_road(
		soa->current_checkpoint[soa_index],
		LOAD_VEC3(position_current),
		road_t_sample_raw,
		spatial_t_sample,
		nullptr,
		&root,
		&root_derivative);
	soa->road_sample[soa_index].road_t = road_t_sample_raw;
	soa->road_sample[soa_index].spatial_t = spatial_t_sample;
	SimTransform surf;
	bool root_sampled = road_t_sample_raw.x != -1000.0f;
	bool center_on_open_road_side = false;
	auto sample_root = [&]() {
		if (!root_sampled) {
			segment.curve_matrix->sample_with_derivative(root, root_derivative, road_t_sample_raw.y);
			root_sampled = true;
		}
	};

	//if (cylinder || pipe)
	//{
	//}
	PipeFloorTrace pipe_trace;
	PipeFloorTrace *pipe_trace_ptr = trace_pipe_floor ? &pipe_trace : nullptr;
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0)
	{	
		if(rect || pipe)
		{
			sample_root();
			if (!project_machine_down_to_road_cross_section(
				segment,
				root,
				LOAD_TRANSFORM(basis_physical),
				LOAD_VEC3(position_current),
				road_t_sample_raw,
				spatial_t_sample,
				center_on_open_road_side,
				pipe_trace_ptr)) {
				trace_floor("orientation_ray_reject", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, 0.0f);
				soa->height_above_track[soa_index] = 0.0f;
				STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
				return false;
			}
		}
	}
	if (root_sampled) {
		soa->road_sample[soa_index].closest_root = root;
	}
	if (road_t_sample_raw.x == -1000.0)
	{
		trace_floor("invalid_road_t", nullptr, road_t_sample_raw, spatial_t_sample, 0.0f);
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		return false;
	}

	if (road_t_sample_raw.x > 1.01f || road_t_sample_raw.x < -1.01f || road_t_sample_raw.y > 1.001f || road_t_sample_raw.y < -0.001f)
	{
		trace_floor("road_t_bounds", nullptr, road_t_sample_raw, spatial_t_sample, 0.0f);
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		return false;
	}
	if (soa->current_track[soa_index]->analytic_road_sample_has_hole(soa->current_checkpoint[soa_index], road_t_sample_raw)) {
		trace_floor("hole_embed", nullptr, road_t_sample_raw, spatial_t_sample, 0.0f);
		soa->road_sample[soa_index].terrain |= TERRAIN::HOLE;
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		return false;
	}
	segment.road_shape->get_oriented_transform_at_time_presampled(surf, road_t_sample_raw, root, root_derivative);
	const float surface_dist = (LOAD_VEC3(position_current) - surf.origin).dot(surf.basis[1]);
	if (center_on_open_road_side && surface_dist < -0.001f) {
		trace_floor("open_center_below_surface", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, surface_dist);
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		return false;
	}
	STORE_VEC3(track_surface_normal, surf.basis[1]);
	soa->height_above_track[soa_index] = fmaxf(1.0f, 20.0f - surface_dist);
	soa->road_sample[soa_index].road_t = road_t_sample_raw;
	soa->road_sample[soa_index].spatial_t = spatial_t_sample;
	soa->road_sample[soa_index].closest_surface = surf;

	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	//dd3d->call("draw_arrow", surf.origin, surf.origin + LOAD_VEC3(track_surface_normal) * 40.0f, godot::Color(1.0f, 1.0f, 1.0f), 0.125, true, _TICK_DELTA);
	//DEBUG::disp_text("soa->height_above_track[soa_index]", soa->height_above_track[soa_index]);

	if (soa->height_above_track[soa_index] > 20.1f)
	{
		trace_floor("height_above_track_too_large", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, surface_dist);
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		soa->height_above_track[soa_index] = 0.0f;
		return false;
	}
	return true;
};

void PhysicsCar::handle_steering()
{
	if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) == 0) {
		return;
	}

	float strafe_turn_mod = 1.0f;
	for (int i = 0; i < 4; ++i) {
		if (soa->tilt_state[POINT_INDEX(i)] & TILTSTATE::DRIFT) {
			strafe_turn_mod -= 0.25f;
		}
	}

	float steer_strength =
	(soa->stat_turn_movement[soa_index] + strafe_turn_mod * soa->stat_strafe_turn[soa_index] * soa->input_strafe[soa_index] *
		soa->input_steer_yaw[soa_index]) *
	-soa->input_steer_yaw[soa_index];
	if (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) {
		steer_strength *= 0.3f;
	}

	soa->velocity_angular_y[soa_index] += 1.5f * steer_strength;

	if (std::abs(soa->velocity_angular_y[soa_index]) < 1.0f) {
		soa->velocity_angular_y[soa_index] = 0.0f;
	}

	soa->input_yaw_dupe[soa_index] = soa->input_steer_yaw[soa_index];
};

void PhysicsCar::set_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag)
{
	for (int i = 0; i < 4; ++i) {
		soa->tilt_state[POINT_INDEX(i)] |= in_flag;
	}
};

void PhysicsCar::remove_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag)
{
	for (int i = 0; i < 4; ++i) {
		soa->tilt_state[POINT_INDEX(i)] &= ~static_cast<uint32_t>(in_flag);
	}
};

void PhysicsCar::handle_suspension_states()
{
	if (soa->grip_frames_from_accel_press[soa_index] != 0) {
		soa->grip_frames_from_accel_press[soa_index] -= 1;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		if (soa->base_speed[soa_index] > 0.1f) {
			if ((soa->machine_state[soa_index] & MACHINESTATE::B14) == 0) {
				bool should_drift = false;
				if (soa->machine_state[soa_index] & MACHINESTATE::MANUAL_DRIFT) {
					if (std::abs(soa->input_steer_yaw[soa_index]) > 0.1f) {
						should_drift = true;
					}
				}
				if (should_drift) {
					set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
				}
			} else {
				remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
				soa->grip_frames_from_accel_press[soa_index] = soa->stat_accel_press_grip_frames[soa_index];
				soa->drift_ramp[soa_index] = 0.0f;
			}
		}
	} else {
		remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
		soa->drift_ramp[soa_index] = 0.0f;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STRAFING) != 0 && std::abs(soa->input_steer_yaw[soa_index]) < 0.1f) {
		soa->machine_state[soa_index] &= ~MACHINESTATE::STRAFING;
	}

	if (std::abs(soa->input_strafe[soa_index]) > 0.3f) {
		soa->machine_state[soa_index] |= MACHINESTATE::STRAFING;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STRAFING) == 0) {
		return;
	}

	set_flag_on_all_tilt_corners(TILTSTATE::STRAFING);
};

void PhysicsCar::handle_machine_turn_and_strafe_points4(float in_angle_vel)
{
	const int point_base = soa_index * 4;
	float steer_deg =
		-(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index] + soa->input_strafe[soa_index] * soa->stat_strafe[soa_index]);
	steer_deg = std::clamp(steer_deg, -45.0f, 45.0f);
	SimTransform steer_basis = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_y(steer_basis, DEG_TO_RAD * steer_deg * 0.5f);

	const SimFloat4 dx = sim_load4(soa->tilt_pos_old_x + point_base) - sim_load4(soa->tilt_pos_x + point_base);
	const SimFloat4 dy = sim_load4(soa->tilt_pos_old_y + point_base) - sim_load4(soa->tilt_pos_y + point_base);
	const SimFloat4 dz = sim_load4(soa->tilt_pos_old_z + point_base) - sim_load4(soa->tilt_pos_z + point_base);
	const SimBasis& b = steer_basis.basis;
	const SimFloat4 lx = SimFloat4(b[0].x) * dx + SimFloat4(b[0].y) * dy + SimFloat4(b[0].z) * dz;
	const SimFloat4 ly = SimFloat4(b[1].x) * dx + SimFloat4(b[1].y) * dy + SimFloat4(b[1].z) * dz;
	const SimFloat4 lz = SimFloat4(b[2].x) * dx + SimFloat4(b[2].y) * dy + SimFloat4(b[2].z) * dz;
	const SimFloat4 speed = sim_sqrt4(lx * lx + ly * ly + lz * lz) * SimFloat4(216.0f / 1000.0f);

	float local_x[4];
	float local_y[4];
	float local_z[4];
	float speed_factor[4];
	sim_store4(local_x, lx);
	sim_store4(local_y, ly);
	sim_store4(local_z, lz);
	sim_store4(speed_factor, speed);
	for (int lane = 0; lane < 4; ++lane) {
		handle_machine_turn_and_strafe(lane, in_angle_vel, SimVec3(local_x[lane], local_y[lane], local_z[lane]), speed_factor[lane], steer_basis);
	}
}

void PhysicsCar::handle_machine_turn_and_strafe(
    int point_lane, float in_angle_vel, const SimVec3& corner_delta_local, float speed_factor, const SimTransform& steer_basis) {
	const int p = POINT_INDEX(point_lane);
    // ───────────── Corner movement & steering matrix ─────────────
    SimVec3 corner_delta = corner_delta_local;

    bool is_drifting = (soa->tilt_state[p] & TILTSTATE::DRIFT) != 0;
    bool is_strafing = (soa->tilt_state[p] & TILTSTATE::STRAFING) != 0;

    // ───────────── Grip / drift threshold ─────────────
    float grip_threshold = 0.0f;
    if ((!is_drifting && is_strafing) || soa->grip_frames_from_accel_press[soa_index] != 0) {
        grip_threshold = 20.0f;
    } else {
        float base_grip = soa->stat_grip_1[soa_index];
        grip_threshold = base_grip;
        if ((soa->state_2[soa_index] & 4u) == 0) {
            if (is_drifting && soa->brake_timer[soa_index] == 0) {
                grip_threshold = soa->stat_grip_3[soa_index];
            }
        } else {
            if (is_drifting && soa->brake_timer[soa_index] < 30) {
                grip_threshold =
                    (base_grip >= soa->stat_grip_3[soa_index]) ? soa->stat_grip_3[soa_index] : base_grip;
            }
        }
    }

    if (std::abs(corner_delta.x) < soa->stat_grip_3[soa_index]) {
        soa->drift_ramp[soa_index] = 0.0f;
        soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::DRIFT);
    }

    bool drift_allowed = true;
    if (!is_drifting && std::abs(soa->input_steer_yaw[soa_index]) <= 0.7f) {
        drift_allowed = false;
    }

    float lateral_delta = corner_delta.x;
    float drift_delta = lateral_delta;

    if (std::abs(lateral_delta) <= grip_threshold || !drift_allowed) {
        if (std::abs(lateral_delta) < 1.1920929e-7f) {
            drift_delta = 0.0f;
        }
        soa->drift_ramp[soa_index] = 0.0f;
        soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::DRIFT);
    } else {
        soa->tilt_state[p] |= TILTSTATE::DRIFT;
        drift_delta = (lateral_delta < 0.0f) ? -grip_threshold : grip_threshold;
    }

    // ───────────── Global state modifiers ─────────────
    if (soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP | MACHINESTATE::TOOKDAMAGE | MACHINESTATE::SIDEATTACKING))
    {
        drift_delta = 0.0f;
    }

    if (soa->machine_state[soa_index] & MACHINESTATE::LOWGRIP)
    {
        soa->velocity_angular_x[soa_index] *= 0.975f;
        soa->velocity_angular_z[soa_index] *= 0.975f;
        //align_machine_y_with_track_normal_immediate();
    }
    else if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0)
    {
        soa->velocity_angular_z[soa_index] *= 0.99f;
    }

    if (soa->machine_state[soa_index] & MACHINESTATE::RETIRED)
    {
        drift_delta *= 0.2f;
    } else if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
        float fade =
            std::clamp(0.01f * (static_cast<float>(soa->frames_since_death[soa_index]) - 4.0f),
                       0.0f, 0.05f);
        drift_delta *= fade;
    }

    // ───────────── Force computation ─────────────
    if (drift_delta != 0.0f) {
        if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
            drift_delta *= 0.1;
        }
        float turn_tension = soa->stat_turn_tension[soa_index];
        float weighted_delta = drift_delta * soa->stat_weight[soa_index];
        float applied_force = 0.0f;

        if (turn_tension >= 0.1f || soa->grip_frames_from_accel_press[soa_index] != 0) {
            applied_force = weighted_delta * turn_tension;
        } else if ((soa->tilt_state[p] & TILTSTATE::AIRBORNE) == 0 &&
                   (soa->machine_state[soa_index] & MACHINESTATE::JUST_PRESSED_BOOST) == 0) {
            float rail_timer = static_cast<float>(soa->rail_collision_timer[soa_index]);
            float speed_lerp = std::clamp(speed_factor, 0.2f, 0.8f);
            float steer_scale = 0.0f;
            if ((soa->tilt_state[p] & TILTSTATE::STRAFING) == 0) {
                steer_scale = ((speed_lerp - 0.2f) / 0.6f) *
                              (turn_tension - 0.1f) *
                              (0.3f + 0.7f * std::abs(soa->input_steer_yaw[soa_index]));
            }
            applied_force = weighted_delta *
                            (0.1f + steer_scale * (1.0f - rail_timer / 20.0f));
        } else {
            applied_force = weighted_delta * 0.1f;
        }

        if (soa->terrain_state[soa_index] & TERRAIN::ICE) {
            applied_force *= 0.003f;
        } else if (soa->terrain_state[soa_index] & TERRAIN::DIRT) {
            applied_force *= 2.0f;
        }

        SimVec3 local_force(applied_force, 0.0f, 0.0f);
        SimVec3 world_force = mxt_basis_rotate(steer_basis, local_force);
        STORE_TILT_VEC3(force_spatial, p, LOAD_TILT_VEC3(force_spatial, p) + world_force);

        if (soa->tilt_state[p] & TILTSTATE::STRAFING) {
            applied_force *= 0.6f;
        }
        soa->turning_related[soa_index] += applied_force;
    }

    // ───────────── Apply forces & torque ─────────────

    ADD_VEC3(velocity, LOAD_TILT_VEC3(force_spatial, p));

    if (soa->rail_collision_timer[soa_index] < 6) {
        apply_torque_from_force(LOAD_TILT_VEC3(offset, p), LOAD_TILT_VEC3(force_spatial, p));
    }

    if (is_drifting && (soa->machine_state[soa_index] & MACHINESTATE::JUSTHITVEHICLE_Q) == 0) {
        in_angle_vel *= soa->stat_grip_2[soa_index];
    }

	soa->velocity_angular_y[soa_index] -= 0.125f * in_angle_vel;
};

void PhysicsCar::project_velocity_to_local_frame()
{
	STORE_VEC3(velocity_local, mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(velocity)));
	float steer = -(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index] + soa->input_strafe[soa_index] * soa->stat_strafe[soa_index]);
	steer = std::clamp(steer, -45.0f, 45.0f);
	SimTransform steer_basis = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_y(steer_basis, DEG_TO_RAD * steer);
	STORE_VEC3(velocity_local_flattened_and_rotated, mxt_basis_inverse_rotate(steer_basis, LOAD_VEC3(velocity)));
	soa->velocity_local_flattened_and_rotated_y[soa_index] = 0.0f;
}

void PhysicsCar::handle_linear_velocity()
{
	float vel_flat_rot_x = soa->velocity_local_flattened_and_rotated_x[soa_index];
	float vel_flat_rot_y = soa->velocity_local_flattened_and_rotated_y[soa_index];
	float vel_flat_rot_z = soa->velocity_local_flattened_and_rotated_z[soa_index];

	float neg_local_fwd_speed = -soa->velocity_local_z[soa_index];
	float abs_local_lat_speed = std::abs(soa->velocity_local_x[soa_index]);

	float mag_vel_flat_rot = LOAD_VEC3(velocity_local_flattened_and_rotated).length();

	float drift_accel_component = 0.0f;
	if ((soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP | MACHINESTATE::TOOKDAMAGE)) == 0 && mag_vel_flat_rot > (10.0f * soa->stat_weight[soa_index]) / 216.0f)
	{
		float norm_z_vel_flat_rot = 0.0f;
		if (mag_vel_flat_rot > 0.0001f)
			norm_z_vel_flat_rot = vel_flat_rot_z / mag_vel_flat_rot;

		float drift_factor = 1.0f - (norm_z_vel_flat_rot * norm_z_vel_flat_rot);
		drift_accel_component = drift_factor * soa->stat_drift_accel[soa_index];

		float strafe_factor = (1.0f - std::abs(soa->input_strafe[soa_index]));
		if (soa->velocity_local_x[soa_index] > 0.0f && soa->drift_sign[soa_index] == -1)
		{
			soa->drift_sign[soa_index] = 1;
			soa->drift_ramp[soa_index] = 0.0f;
		}else if (soa->velocity_local_x[soa_index] < 0.0f && soa->drift_sign[soa_index] == 1)
		{
			soa->drift_sign[soa_index] = -1;
			soa->drift_ramp[soa_index] = 0.0f;
		}
		if ((soa->velocity_local_x[soa_index] > 0.0f && soa->drift_sign[soa_index] == 1) || (soa->velocity_local_x[soa_index] < 0.0f && soa->drift_sign[soa_index] == -1))
		{
			soa->drift_ramp[soa_index] = std::min(1.0f, soa->drift_ramp[soa_index] + (0.025f - soa->drift_ramp[soa_index] * 0.025f) * (1.0f + soa->boost_turbo[soa_index] * 0.03f));
		}
		drift_accel_component = drift_accel_component * soa->drift_ramp[soa_index] * strafe_factor;
	}

	float net_fwd_accel = handle_machine_accel_and_boost(
		neg_local_fwd_speed, abs_local_lat_speed, drift_accel_component);

	float broken_factor = 1.0f; // Unused placeholder for future behavior
	float overall_damping = 0.6f + 0.55f;
	overall_damping = std::min(overall_damping, 1.0f);

	net_fwd_accel *= overall_damping;
	STORE_VEC3(velocity, LOAD_VEC3(velocity) * overall_damping);

	if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) == 0) {
		soa->visual_rotation_x[soa_index] += 0.25f * net_fwd_accel;
	} else {
		soa->visual_rotation_x[soa_index] += 0.05f * net_fwd_accel;
	}

	float airborne_factor = 1.0f;
	if (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) {
		SimVec3 machine_up_vector_ws = LOAD_TRANSFORM(basis_physical).basis.get_column(2);
		float dot_prod_up_with_track_normal =
		machine_up_vector_ws.dot(LOAD_VEC3(track_surface_normal));

		float alignment_factor = 3.4f * (0.3f + dot_prod_up_with_track_normal);
		alignment_factor = std::clamp(alignment_factor, 0.0f, 1.0f);
		airborne_factor = alignment_factor * alignment_factor;
	}


	float effective_steer_degrees =
	-(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index] + soa->input_strafe[soa_index] * soa->stat_strafe[soa_index]);
	if (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING)
		effective_steer_degrees = 0.0f;
	effective_steer_degrees = std::clamp(effective_steer_degrees, -45.0f, 45.0f);

	soa->turn_reaction_input[soa_index] = 0.75f * -(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index]);
	SimVec3 local_thrust_vector(0.0f, 0.0f, -(net_fwd_accel * airborne_factor));
	SimTransform thrust_basis = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_y(thrust_basis, DEG_TO_RAD * effective_steer_degrees);
	SimVec3 world_thrust_vector = mxt_basis_rotate(thrust_basis, local_thrust_vector);
	ADD_VEC3(velocity, world_thrust_vector);


	float current_world_speed = LOAD_VEC3(velocity).length();

	if (std::abs(soa->stat_weight[soa_index]) > 0.0001f &&
		current_world_speed / soa->stat_weight[soa_index] > (1.0f / 1.08f)) {
		if (soa->side_attack_delay[soa_index] == 6) {
			float speed_cap_for_dash = (50.0f / 9.0f) * soa->stat_weight[soa_index];
			float clamped_speed_for_dash = std::min(current_world_speed, speed_cap_for_dash);

			SimVec3 local_dash_vector(soa->side_attack_indicator[soa_index] * clamped_speed_for_dash,
				0.0f, 0.0f);
			SimVec3 world_dash_vector = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_dash_vector);
			ADD_VEC3(velocity, world_dash_vector);
		}

		if ((soa->terrain_state[soa_index] & TERRAIN::JUMP) != 0 &&
			(soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
			SimVec3 local_jump_boost(0.0f, 1.13f * current_world_speed, 0.0f);
		SimVec3 world_jump_boost = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_jump_boost);

		ADD_VEC3(velocity, world_jump_boost);
		soa->state_2[soa_index] |= 2u;
		soa->velocity_angular_x[soa_index] = 0.0f;
		soa->velocity_angular_z[soa_index] = 0.0f;
	}
}

soa->input_strafe_1_6[soa_index] = soa->input_strafe_32[soa_index] / 20.0f;
soa->input_strafe_32[soa_index] += (8.0f * soa->input_strafe[soa_index] - 5.0f * soa->input_strafe_1_6[soa_index]);
};

void PhysicsCar::apply_initial_accel_activation(float effective_accel_input)
{
	if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) == 0) {
		soa->machine_state[soa_index] |= MACHINESTATE::ACTIVE;
	}

	if (soa->frames_since_start_2[soa_index] == 0) {
		soa->frames_since_start_2[soa_index] = 1;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		if (soa->race_start_charge[soa_index] > 0.0f) {
			soa->base_speed[soa_index] = 1.0f;
			soa->machine_state[soa_index] |= MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL;
			soa->race_start_charge[soa_index] = 0.0f;
		}
	} else {
		soa->race_start_charge[soa_index] += effective_accel_input;
	}
}

float PhysicsCar::handle_machine_accel_and_boost(float neg_local_fwd_speed, float abs_local_lateral_speed, float drift_accel_factor)
{
	float effective_accel_input = 0.0f;
	float final_thrust_output = 0.0f;

	if (!((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) && soa->frames_since_death[soa_index] <= 0x77)) {
		effective_accel_input = soa->input_accel[soa_index];

		if ((soa->state_2[soa_index] & 4u) == 0) {
			if (effective_accel_input < 0.0f || soa->input_brake[soa_index] > 0.0f)
				effective_accel_input = 0.0f;
		} else if (effective_accel_input < 0.0f || soa->brake_timer[soa_index] > 0x1d) {
			effective_accel_input = 0.0f;
		}

		if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) == 0 && effective_accel_input < 0.3f)
			effective_accel_input = 0.0f;
	}

	if (effective_accel_input <= 0.0001f) {
		if (soa->race_start_charge[soa_index] <= 0.0f) {
			if (soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN)
				soa->base_speed[soa_index] = 0.0f;
		} else {
			soa->race_start_charge[soa_index] -= 2.0f;
			if (soa->race_start_charge[soa_index] < 0.0f)
				soa->race_start_charge[soa_index] = 0.0f;
			if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0)
				soa->base_speed[soa_index] = 0.0f;
		}
	} else {
		apply_initial_accel_activation(effective_accel_input);
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		const bool sboostActive = soa->s_boost_active[soa_index];
		uint32_t current_machine_state = soa->machine_state[soa_index];
		float normalized_fwd_speed = neg_local_fwd_speed / soa->stat_weight[soa_index];

		if (soa->boost_delay_frame_counter[soa_index] != 0) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
			soa->boost_delay_frame_counter[soa_index] -= 1;
		}

		if (current_machine_state & MACHINESTATE::JUST_PRESSED_BOOST) {
			if (soa->boost_delay_frame_counter[soa_index] == 0)
				soa->boost_delay_frame_counter[soa_index] = 6;
			else
				soa->boost_delay_frame_counter[soa_index] += 1;
		}

		current_machine_state = soa->machine_state[soa_index];
		if (sboostActive) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
			soa->boost_frames_manual[soa_index] = 0;
			if ((current_machine_state & MACHINESTATE::JUST_HIT_DASHPLATE) == 0) {
				if (soa->boost_frames[soa_index] > 0) {
					soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE;
				} else {
					soa->machine_state[soa_index] &= ~(MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE);
					soa->dashplate_heat_multiplier[soa_index] = 1.0f;
				}
			} else {
				float boost_strength_factor = 1.0f - soa->boost_turbo[soa_index] / (9.0f * soa->stat_boost_strength[soa_index]);
				int target_dash_boost_frames = static_cast<int>(0.5f * 60.0f * soa->stat_boost_length[soa_index]);

				if (soa->boost_frames[soa_index] < static_cast<uint32_t>(target_dash_boost_frames))
					soa->boost_frames[soa_index] = target_dash_boost_frames;

				float min_boost_strength_factor = 0.2f;
				soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE;

				boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
				float dashplate_multiplier = soa->dashplate_heat_multiplier[soa_index];
				if (dashplate_multiplier < 1.0f)
					dashplate_multiplier = 1.0f;
				soa->boost_turbo[soa_index] += dashplate_multiplier * (2.0f * soa->stat_boost_strength[soa_index]) * boost_strength_factor;
				soa->dashplate_heat_multiplier[soa_index] = 1.0f;
			}
		} else if ((current_machine_state & MACHINESTATE::JUST_HIT_DASHPLATE) == 0) {
			if (soa->boost_frames[soa_index] == 0) {
				bool do_manual_boost = (current_machine_state & MACHINESTATE::JUST_PRESSED_BOOST) &&
				soa->energy[soa_index] > 1.0f && effective_accel_input > 0.0f;
				if (!do_manual_boost) {
					soa->machine_state[soa_index] &= ~(MACHINESTATE::BOOSTING_DASHPLATE |
						MACHINESTATE::JUST_PRESSED_BOOST |
						MACHINESTATE::BOOSTING);
					// soa->boost_turbo[soa_index] -= (2.0f + 0.01f * soa->boost_turbo[soa_index]) / 60.0f * soa->stat_acceleration[soa_index];
				} else {
					float boost_strength_factor = 1.0f - soa->boost_turbo[soa_index] / (9.0f * soa->stat_boost_strength[soa_index]);
					float min_boost_strength_factor = 0.2f;
					int boost_duration_frames = static_cast<int>(60.0f * soa->stat_boost_length[soa_index]);
					soa->boost_frames[soa_index] = boost_duration_frames;
					soa->boost_frames_manual[soa_index] = boost_duration_frames;
					soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING;
					soa->machine_state[soa_index] &= ~MACHINESTATE::BOOSTING_DASHPLATE;

					boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
					soa->boost_turbo[soa_index] += soa->stat_boost_strength[soa_index] * boost_strength_factor;
				}
			} else {
				soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
				soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING;
			}
		} else {
			float boost_strength_factor = 1.0f - soa->boost_turbo[soa_index] / (9.0f * soa->stat_boost_strength[soa_index]);
			int target_dash_boost_frames = static_cast<int>(0.5f * 60.0f * soa->stat_boost_length[soa_index]);

			if (soa->boost_frames[soa_index] < static_cast<uint32_t>(target_dash_boost_frames))
				soa->boost_frames[soa_index] = target_dash_boost_frames;

			float min_boost_strength_factor = 0.2f;
			soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
			soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING;

			boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
			float dashplate_multiplier = soa->dashplate_heat_multiplier[soa_index];
			if (dashplate_multiplier < 1.0f)
				dashplate_multiplier = 1.0f;
			soa->boost_turbo[soa_index] += dashplate_multiplier * (2.0f * soa->stat_boost_strength[soa_index]) * boost_strength_factor;
			soa->dashplate_heat_multiplier[soa_index] = 1.0f;
		}

		if (soa->boost_frames[soa_index] > 0 || soa->boost_frames_manual[soa_index] > 0)
		{
			soa->boost_turbo[soa_index] -= ((3.0f + 0.03f * soa->boost_turbo[soa_index]) * soa->stat_acceleration[soa_index] * soa->stat_boost_strength[soa_index] * 0.5f) / 60.0f;
		}else
		{
			soa->boost_turbo[soa_index] -= ((6.0f + 0.05f * soa->boost_turbo[soa_index]) * soa->stat_acceleration[soa_index] * soa->stat_boost_strength[soa_index] * 0.5f) / 60.0f;
		}
		soa->boost_turbo[soa_index] = std::max(soa->boost_turbo[soa_index], 0.0f);

		if (soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) {
			if (!sboostActive && soa->boost_frames_manual[soa_index] > 0) {
				soa->energy[soa_index] -= 0.1666666667f * soa->boost_energy_use_mult[soa_index];
				soa->boost_frames_manual[soa_index] -= 1;
			}

			if (soa->boost_frames[soa_index] > 0)
				soa->boost_frames[soa_index] -= 1;

			if (!sboostActive && soa->boost_frames[soa_index] == 0 && soa->speed_kmh[soa_index] > 1200.0f) {
				float cooldown_duration = (soa->speed_kmh[soa_index] - 1200.0f) / 60.0f;
				cooldown_duration = std::min(cooldown_duration, 10.0f);
				if (static_cast<float>(soa->boost_delay_frame_counter[soa_index]) < cooldown_duration)
					soa->boost_delay_frame_counter[soa_index] = static_cast<uint8_t>(cooldown_duration);
			}

			if (!sboostActive && soa->energy[soa_index] < 0.01f) {
				soa->energy[soa_index] = 0.01f;
				soa->boost_frames_manual[soa_index] = 0;
				if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING_DASHPLATE) == 0) {
					soa->boost_frames[soa_index] = 0;
				} else {
					int half_dash_boost_frames = static_cast<int>(0.5f * 60.0f * soa->stat_boost_length[soa_index]);
					if (half_dash_boost_frames < static_cast<int>(soa->boost_frames[soa_index]))
						soa->boost_frames[soa_index] = half_dash_boost_frames;
				}
			}

			if (soa->boost_frames[soa_index] <= 0) {
				soa->boost_frames[soa_index] = 0;
				soa->machine_state[soa_index] &= ~(MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE);
			}
		}

		float accel_stat_scaled = 40.0f * soa->stat_acceleration[soa_index];
		float target_speed_component = (effective_accel_input * accel_stat_scaled) / 348.0f;
		if (soa->boost_frames[soa_index] > 0 || soa->boost_frames_manual[soa_index] > 0 || sboostActive)
		{
			target_speed_component *= 1.0f + soa->stat_boost_strength[soa_index] * soa->stat_acceleration[soa_index] * 0.038f;
		}
		target_speed_component += soa->base_speed[soa_index];
		float speed_difference = target_speed_component - normalized_fwd_speed;

		float speed_factor_denom = 36.0f + 40.0f * soa->stat_max_speed[soa_index] + soa->boost_turbo[soa_index] * 3.0f;
		float speed_factor = 0.0f;
		if (std::abs(speed_factor_denom) > 0.0001f)
			speed_factor = target_speed_component / speed_factor_denom;
		speed_factor = std::max(speed_factor, 0.0f);

		float current_accel_magnitude = speed_factor * 4.0f * (soa->stat_acceleration[soa_index] * (0.6f + soa->stat_acceleration[soa_index]));

		if ((soa->machine_state[soa_index] & (MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::JUST_PRESSED_BOOST)) == 0) {
			if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) || sboostActive) {
				current_accel_magnitude *= (soa->stat_weight[soa_index] <= 1000.0f) ? 0.3f : 0.5f;
			}
		} else {
			current_accel_magnitude = 0.0f;
		}

		if (speed_difference > 0.0f &&
			(normalized_fwd_speed < 0.0f || (soa->terrain_state[soa_index] & TERRAIN::DIRT))) {
			current_accel_magnitude *= 5.0f;
	}

	float final_accel_term = (1.0f - drift_accel_factor) *
	((speed_difference * current_accel_magnitude) +
		((abs_local_lateral_speed * soa->stat_acceleration[soa_index]) / soa->stat_weight[soa_index]) * soa->stat_turn_decel[soa_index]);

	float new_base_speed = target_speed_component - final_accel_term;
	float base_speed_diff = new_base_speed - soa->base_speed[soa_index];

	if (base_speed_diff < 0.0f)
	{
		new_base_speed = soa->base_speed[soa_index] - final_accel_term * 0.1f;
		//base_speed_diff = new_base_speed - soa->base_speed[soa_index];
	}

	soa->base_speed[soa_index] = new_base_speed;

	if (soa->input_brake[soa_index] <= 0.0001f)
		soa->brake_timer[soa_index] = 0;
	else if (soa->brake_timer[soa_index] < 0x1e)
		soa->brake_timer[soa_index] += 1;

	float brake_effect = 0.0f;
	if ((soa->state_2[soa_index] & 4u) == 0)
		brake_effect = soa->input_brake[soa_index] * (0.5f * current_accel_magnitude);
	else if (soa->brake_timer[soa_index] > 0xe)
		brake_effect = soa->input_brake[soa_index] * (0.5f * current_accel_magnitude);

	brake_effect = std::min(brake_effect, 0.12f);
	soa->base_speed[soa_index] = std::max(soa->base_speed[soa_index] - brake_effect, 0.0f);

	soa->base_speed[soa_index] = std::max(soa->base_speed[soa_index] - soa->stat_drag[soa_index], 0.0f);

	if (sboostActive)
	{
		soa->base_speed[soa_index] += 0.025f;
	}

	float final_output_thrust_factor = speed_difference;
	if (brake_effect <= 0.0f) {
		float modifier = 0.3f;
		if (soa->machine_state[soa_index] & MACHINESTATE::B14)
			modifier = 1.0f;

		if (normalized_fwd_speed < 0.0f || final_output_thrust_factor < 0.0f)
			final_output_thrust_factor *= (0.5f * modifier);
	}

	if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
		float speed_ratio_for_0hp = std::min(soa->speed_kmh[soa_index] / 100.0f, 1.0f);
		final_output_thrust_factor *= (0.2f - 0.15f * speed_ratio_for_0hp);
	}

	if ((soa->machine_state[soa_index] & (MACHINESTATE::BOOSTING_DASHPLATE | MACHINESTATE::BOOSTING)) == 0) {
		final_thrust_output = 1000.0f * final_output_thrust_factor;
	} else if (soa->stat_weight[soa_index] <= 1000.0f) {
		final_thrust_output = 1200.0f * final_output_thrust_factor;
	} else {
		final_thrust_output = 1600.0f * final_output_thrust_factor;
	}
} else {
	final_thrust_output = -neg_local_fwd_speed;
	soa->base_speed[soa_index] = 0.014f * soa->race_start_charge[soa_index];
}

if ((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) && soa->frames_since_death[soa_index] <= 0x77) {
	if (soa->brake_timer[soa_index] < 0x3d) {
		soa->brake_timer[soa_index] += 1;
	} else {
		soa->input_accel[soa_index] = 0.0f;
		soa->input_brake[soa_index] = 0.0001f;
	}
	final_thrust_output = 0.0f;
}

return final_thrust_output;
};

void PhysicsCar::handle_angle_velocity()
{
	float weight_val = 0.99f;

	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		if ((soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) == 0) {
			weight_val = 0.05f * soa->weight_derived_2[soa_index];
		} else {
			weight_val = 0.2f * soa->weight_derived_2[soa_index];
		}
	} else {
		soa->velocity_angular_x[soa_index] *= 0.9f;
		soa->velocity_angular_z[soa_index] *= weight_val;
		weight_val = soa->weight_derived_2[soa_index];
	}

	soa->velocity_angular_y[soa_index] = std::clamp(soa->velocity_angular_y[soa_index], -weight_val, weight_val);
};

void PhysicsCar::handle_airborne_controls()
{
	float min_air_tilt = -50.0f;
	float max_air_tilt = 60.0f;
	bool airborne_controls_active = false;

	if (soa->frames_since_start_2[soa_index] > 60 && (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE))
		airborne_controls_active = true;

	if (airborne_controls_active) {
		float tilt_effect_base = 2.0f * std::abs(soa->input_steer_yaw[soa_index]);

		if (soa->state_2[soa_index] & 0x2u)
			tilt_effect_base = 0.0f;

		float current_tilt_increment = 0.0f;
		if (tilt_effect_base >= 0.1f) {
			current_tilt_increment = tilt_effect_base +
			2.0f * soa->input_steer_pitch[soa_index] * std::abs(2.0f - tilt_effect_base);
			if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) &&
				!(soa->machine_state[soa_index] & MACHINESTATE::BOOSTING_DASHPLATE))
				current_tilt_increment *= 2.0f;
		} else {
			current_tilt_increment = tilt_effect_base + 4.0f * soa->input_steer_pitch[soa_index];
		}

		if (soa->air_time[soa_index] > 60) {
			float air_time_factor = static_cast<float>(soa->air_time[soa_index] - 60) / 120.0f;
			air_time_factor = std::min(air_time_factor, 1.0f);

			current_tilt_increment =
			current_tilt_increment * (1.0f + 0.3f * air_time_factor) +
			(0.3f * air_time_factor);
		}

		soa->air_tilt[soa_index] += current_tilt_increment;
		soa->air_tilt[soa_index] = std::clamp(soa->air_tilt[soa_index], min_air_tilt, max_air_tilt);
	} else {
		soa->air_tilt[soa_index] = 0.0f;
	}
};

void PhysicsCar::orient_vehicle_from_gravity_or_road()
{
	float factor = 1.5f + soa->stat_weight[soa_index] / 4000.0f;
	if (factor >= 1.8f) {
		factor = std::min(factor, 2.0f);
	} else {
		factor = 3.6f - factor;
	}

	float base_factor = 0.0f;
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		base_factor = factor * 1.3f;
	} else if (soa->height_above_track[soa_index] <= 0.0f) {
		base_factor = factor * 0.6f;
	} else {
		base_factor = (soa->machine_state[soa_index] & MACHINESTATE::B10) ? factor * 1.8f
		: factor * 1.3f;
	}

	float force_mag = 10.0f * -(0.009f * soa->stat_weight[soa_index]) * base_factor;

	SimVec3 gravity_align_force = LOAD_VEC3(track_surface_normal) * force_mag;
	ADD_VEC3(velocity, gravity_align_force);


	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		SimVec3 machine_world_up = LOAD_TRANSFORM(basis_physical).basis.get_column(1);
		SimVec3 safe_track_normal =
		normalized_safe(LOAD_VEC3(track_surface_normal), SimVec3(0, 1, 0));
		float dot = 0.0f;
		if (machine_world_up.length_squared() > 0.0001f)
			dot = machine_world_up.dot(safe_track_normal);

		if (dot < 0.7f) {
			float align_factor = 0.0f;
			if (dot >= 0.0f)
				align_factor = dot / 0.7f;
			float rot_deg = 40.0f * (1.0f - align_factor);
			float rot_rad = DEG_TO_RAD * rot_deg;
			SimVec3 axis = machine_world_up.cross(safe_track_normal);
			if (axis.length_squared() > 0.0001f) {
				SimQuat q(axis.normalized(), rot_rad);
					SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
					mxt_tmp.basis = SimBasis(q) * mxt_tmp.basis;
					STORE_TRANSFORM(basis_physical, mxt_tmp);
				}
			}
		} else {
		float tilt_rad = DEG_TO_RAD * soa->air_tilt[soa_index];
		float c = deterministic_fp::cosf(tilt_rad);
		float s = deterministic_fp::sinf(tilt_rad);
		SimVec3 local_tilted_up(0.0f, c, s);
		SimVec3 world_tilted_up = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_tilted_up);
		SimVec3 safe_world_up = normalized_safe(world_tilted_up, SimVec3(0, 1, 0));
		SimVec3 safe_track_normal =
		normalized_safe(LOAD_VEC3(track_surface_normal), SimVec3(0, 1, 0));
		float dot = safe_world_up.dot(safe_track_normal);

		if (dot < 0.992f) {
			float adjusted_dot = dot + 0.008f;
			float base_rot_deg = 15.0f;
			SimVec3 axis = safe_world_up.cross(safe_track_normal);
			float axis_thresh = 0.1f * 0.1f;
			if (axis.length_squared() < axis_thresh || adjusted_dot < 0.008f) {
				SimVec3 cur_up =
				normalized_safe(LOAD_TRANSFORM(basis_physical).basis.get_column(1),
					SimVec3(0, 1, 0));
				float dot_up = cur_up.dot(safe_track_normal);

				if (dot_up <= 0.0f) {
					SimVec3 machine_x =
					normalized_safe(LOAD_TRANSFORM(basis_physical).basis.get_column(0),
						SimVec3(1, 0, 0));
					axis = LOAD_TRANSFORM(basis_physical).basis.get_column(2);
					float dot_track_vs_x = safe_track_normal.dot(machine_x);
					if (dot_track_vs_x > 0.0f)
						axis = -axis;
				}
			}

			if (axis.length_squared() > 0.0001f) {
				SimVec3 norm_axis = axis.normalized();
				float sq_dot = std::max(0.0f, adjusted_dot * adjusted_dot);
				float rot_deg = base_rot_deg * (1.0f - sq_dot);
				float rot_rad = DEG_TO_RAD * rot_deg;
				SimQuat q(norm_axis, rot_rad);
					SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
					mxt_tmp.basis = SimBasis(q) * mxt_tmp.basis;
					STORE_TRANSFORM(basis_physical, mxt_tmp);
				}
			}
		}
};

void PhysicsCar::handle_drag_and_glide_forces()
{
	float speed = LOAD_VEC3(velocity).length();
	float speed_weight_ratio = 0.0f;
	if (std::abs(soa->stat_weight[soa_index]) > 0.0001f)
		speed_weight_ratio = speed / soa->stat_weight[soa_index];

	float scaled_speed = 216.0f * speed_weight_ratio;

	if (scaled_speed < 2.0f) {
		STORE_VEC3(velocity, SimVec3());
		soa->visual_shake_mult[soa_index] = 0.0f;
		return;
	}

	SimVec3 vel_norm = normalized_safe(LOAD_VEC3(velocity), SimVec3());
	float alignment_with_normal = LOAD_VEC3(track_surface_normal).dot(vel_norm);

	SimVec3 forward_world =
	normalized_safe(mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 0, -1)),
		SimVec3(0, 0, -1));
	float forward_normal_alignment =
	LOAD_VEC3(track_surface_normal).dot(forward_world);

	SimVec3 normal_force =
	LOAD_VEC3(track_surface_normal) *
	(soa->stat_weight[soa_index] * alignment_with_normal * speed_weight_ratio);
	float base_drag_mag = speed_weight_ratio * speed_weight_ratio * 8.0f;
	SimVec3 drag_vector = LOAD_VEC3(velocity) - normal_force;

	if (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) {
		if (forward_normal_alignment < 0.0f)
			base_drag_mag *= std::max(0.0f, 1.0f + forward_normal_alignment);
		forward_normal_alignment += 1.0f; // shift to 0 -> 2 range
	}

	float drag_len = drag_vector.length();
	if (drag_len > 0.0001f)
		drag_vector *= base_drag_mag / drag_len;
	else
		drag_vector = SimVec3();

	soa->visual_shake_mult[soa_index] = base_drag_mag;

	if (soa->stat_weight[soa_index] < 1100.0f) {
		float weight_scale = soa->stat_weight[soa_index] / 1100.0f;
		alignment_with_normal *= weight_scale * weight_scale;
	}

	bool boosting = (soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) != 0;
	bool airborne = (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0;
	float drag_coeff = 0.0f;

	if (boosting) {
		drag_coeff = alignment_with_normal * 0.5f;
	} else if (airborne) {
		if (alignment_with_normal >= 0.0f || forward_normal_alignment <= 0.8f) {
			drag_coeff = alignment_with_normal * 0.6f;
		} else {
			drag_coeff =
			alignment_with_normal *
			(0.6f + 4.0f * (forward_normal_alignment - 0.8f));
		}
	} else {
		drag_coeff = alignment_with_normal * 0.6f;
	}

	drag_vector += LOAD_VEC3(track_surface_normal) * (base_drag_mag * drag_coeff);

	if (soa->frames_since_death[soa_index] != 0) {
		float death_fade =
		std::clamp(0.01f * static_cast<float>(soa->frames_since_death[soa_index]) - 4.0f, 0.0f, 1.0f);
		drag_vector *= death_fade;
	}

	SUB_VEC3(velocity, drag_vector);
};

void PhysicsCar::rotate_machine_from_angle_velocity()
{
	SimVec3 processed_ang_vel;

	const float deadzone_threshold = 3.0f;

	float val_x = soa->velocity_angular_x[soa_index];
	if (std::abs(val_x) <= deadzone_threshold)
		processed_ang_vel.x = 0.0f;
	else
		processed_ang_vel.x = val_x - ((val_x > 0.0f) - (val_x < 0.0f)) * deadzone_threshold;

	float val_z = soa->velocity_angular_z[soa_index];
	if (std::abs(val_z) <= deadzone_threshold)
		processed_ang_vel.z = 0.0f;
	else
		processed_ang_vel.z = val_z - ((val_z > 0.0f) - (val_z < 0.0f)) * deadzone_threshold;

	processed_ang_vel.y = soa->velocity_angular_y[soa_index];

	if (std::abs(soa->weight_derived_1[soa_index]) > 0.0001f)
		processed_ang_vel.x /= soa->weight_derived_1[soa_index];
	else
		processed_ang_vel.x = 0.0f;

	if (std::abs(soa->weight_derived_2[soa_index]) > 0.0001f)
		processed_ang_vel.y /= soa->weight_derived_2[soa_index];
	else
		processed_ang_vel.y = 0.0f;

	if (std::abs(soa->weight_derived_3[soa_index]) > 0.0001f)
		processed_ang_vel.z /= soa->weight_derived_3[soa_index];
	else
		processed_ang_vel.z = 0.0f;

	float rotation_angle_rad = processed_ang_vel.length();
	if (rotation_angle_rad > 0.0001f) {
		SimVec3 rotation_axis = processed_ang_vel.normalized();
		SimQuat delta_q(rotation_axis, rotation_angle_rad);

		SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
		mxt_tmp.basis = mxt_tmp.basis * SimBasis(delta_q);
		STORE_TRANSFORM(basis_physical, mxt_tmp);
	}
};

void PhysicsCar::handle_startup_wobble()
{
	float f_val3_for_cross_prod_y = 0.0f;

	int seed_uVar4 = static_cast<int>(soa->position_current_z[soa_index]) ^
	static_cast<int>(soa->position_current_x[soa_index]) ^
	static_cast<int>(soa->position_current_y[soa_index]) ^
	static_cast<int>(soa->base_speed[soa_index]);

	int intermediate_uint_f1 =
	(seed_uVar4 ^ static_cast<int>(soa->velocity_angular_x[soa_index] * 4000000.0f)) &
	0xffff;
	float normalized_f1 = static_cast<float>(intermediate_uint_f1) / 65535.0f;
	float fVar1_wobble_x = 2.0f * normalized_f1 - 1.0f;

	int intermediate_uint_f2 =
	(seed_uVar4 ^ static_cast<int>(soa->velocity_angular_y[soa_index] * 4000000.0f)) &
	0xffff;
	float normalized_f2 = static_cast<float>(intermediate_uint_f2) / 65535.0f;
	float fVar2_wobble_y_comp = 0.5f + 1.5f * normalized_f2;

	if (fVar1_wobble_x <= 0.0f)
		fVar1_wobble_x -= 0.5f;
	else
		fVar1_wobble_x += 0.5f;

	SimVec3 local_vec_y_scaled(0.0f, 0.0162037037037f * soa->stat_weight[soa_index],
		0.0f);

	SimVec3 local_48_rotated_vec =
	mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), local_vec_y_scaled);

	SimVec3 wobble_pseudo_force_local(fVar1_wobble_x,
		f_val3_for_cross_prod_y,
		fVar2_wobble_y_comp);

	SimVec3 torque_to_add =
	local_48_rotated_vec.cross(wobble_pseudo_force_local);
	ADD_VEC3(velocity_angular, torque_to_add);
};

void PhysicsCar::initialize_machine()
{
	soa->machine_state[soa_index] = 0;
	soa->machine_name[soa_index] = "Blue Falcon";

	update_machine_stats();

	soa->weight_derived_1[soa_index] = 52.0f * soa->stat_weight[soa_index] * 0.0625f;
	soa->weight_derived_2[soa_index] = 45.0f * soa->stat_weight[soa_index] * 0.0625f;
	soa->weight_derived_3[soa_index] = 52.0f * soa->stat_weight[soa_index] * 0.0625f;

	soa->boost_turbo[soa_index] = 0.0f;

	soa->s_boost_charge_max[soa_index] = 30;

	if (soa->car_properties[soa_index] != nullptr) {
		for (int i = 0; i < 4; ++i) {
			const int p = POINT_INDEX(i);
			soa->tilt_force[p] = 0.0f;
			STORE_TILT_VEC3(offset, p, soa->car_properties[soa_index]->tilt_corners[i]);
			STORE_TILT_VEC3(pos_old, p, SimVec3());
			soa->tilt_state[p] = 0;
			soa->tilt_rest_length[p] = 1.7f;
		}

		soa->stat_obstacle_collision[soa_index] = 0.0f;
		soa->stat_track_collision[soa_index] = 1.0f;

		for (int i = 0; i < 4; ++i) {
			const int p = POINT_INDEX(i);
			const SimVec3 wall_offset = soa->car_properties[soa_index]->wall_corners[i];
			STORE_WALL_VEC3(offset, p, wall_offset);
			STORE_WALL_VEC3(collision, p, SimVec3());

			float offset_len = wall_offset.length();
			if (soa->stat_obstacle_collision[soa_index] < offset_len)
				soa->stat_obstacle_collision[soa_index] = offset_len;

			float abs_offset_x = std::abs(wall_offset.x);
			if (soa->stat_track_collision[soa_index] < abs_offset_x)
				soa->stat_track_collision[soa_index] = abs_offset_x;
		}
	}

	soa->stat_obstacle_collision[soa_index] += 0.1f;
	soa->calced_max_energy[soa_index] = soa->car_properties[soa_index]->max_energy + soa->ko_energy_bonus[soa_index];

	reset_machine(1);
};

void PhysicsCar::update_machine_stats()
{
	if (soa->car_properties[soa_index] == nullptr)
		return;

	PhysicsCarProperties def_stats =
	soa->car_properties[soa_index]->derive_machine_base_stat_values(soa->m_accel_setting[soa_index]);

	soa->stat_weight[soa_index] = def_stats.weight_kg;
	soa->stat_grip_1[soa_index] = def_stats.grip_1;
	soa->stat_grip_3[soa_index] = def_stats.grip_3;
	soa->stat_turn_movement[soa_index] = def_stats.turn_movement;
	soa->stat_strafe[soa_index] = def_stats.strafe;
	soa->stat_turn_reaction[soa_index] = def_stats.turn_reaction;
	soa->stat_grip_2[soa_index] = def_stats.grip_2;
	soa->stat_body[soa_index] = def_stats.body;
	soa->stat_turn_tension[soa_index] = def_stats.turn_tension;
	soa->stat_drift_accel[soa_index] = def_stats.drift_accel;
	soa->stat_accel_press_grip_frames[soa_index] = def_stats.unk_byte_0x48;
	soa->camera_reorienting[soa_index] = def_stats.camera_reorienting;
	soa->camera_repositioning[soa_index] = def_stats.camera_repositioning;
	soa->stat_strafe_turn[soa_index] = def_stats.strafe_turn;
	soa->stat_acceleration[soa_index] = def_stats.acceleration;
	soa->stat_max_speed[soa_index] = def_stats.max_speed;
	soa->stat_boost_strength[soa_index] = 0.57f * def_stats.boost_strength;
	soa->stat_boost_length[soa_index] = def_stats.boost_length;
	soa->stat_turn_decel[soa_index] = def_stats.turn_decel;
	soa->stat_drag[soa_index] = def_stats.drag;
	soa->boost_energy_use_mult[soa_index] = def_stats.boost_energy_use_rate;
	soa->energy_recharge_mult[soa_index] = def_stats.energy_recharge_rate;
	if ((def_stats.state_flags & 1u) == 0u) {
		soa->machine_state[soa_index] &= ~MACHINESTATE::B9;
	} else {
		soa->machine_state[soa_index] |= MACHINESTATE::B9;
	}
	if ((def_stats.state_flags & 2u) != 0u) {
		soa->machine_state[soa_index] |= MACHINESTATE::VEHICLEACTIVE_Q;
	} else {
		soa->machine_state[soa_index] &= ~MACHINESTATE::B1;
	}
};

void PhysicsCar::reset_machine(int reset_type)
{
	soa->level_start_time[soa_index] = soa->frames_since_start[soa_index] + 60 * 5;
	// Clear all LOAD_VEC3(velocity) and collision vectors
	STORE_VEC3(velocity, SimVec3());
	STORE_VEC3(knockback_velocity, SimVec3());
	STORE_VEC3(velocity_local_flattened_and_rotated, SimVec3());
	STORE_VEC3(velocity_local, SimVec3());
	STORE_VEC3(velocity_angular, SimVec3());
	STORE_VEC3(collision_push_total, SimVec3());
	STORE_VEC3(collision_push_rail, SimVec3());
	STORE_VEC3(collision_push_track, SimVec3());

	STORE_VEC3(track_surface_normal, mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 1, 0)));

	// Placeholder spawn values until StageOverseer is ported
	SimVec3 spawn_pos = SimVec3(0.f, 200.f, 0.f);
	float spawn_rot = 0.0f;

	STORE_VEC3(position_current, spawn_pos);
	STORE_VEC3(position_old, spawn_pos);
	STORE_VEC3(position_old_2, spawn_pos);
	STORE_VEC3(position_old_dupe, spawn_pos);

	STORE_VEC3(position_bottom, mxt_transform_point(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(position_current), SimVec3(0.0f, -0.1f, 0.0f)));

	STORE_VEC3(position_old_2, SimVec3(0, 5, 0));
	soa->input_steer_yaw[soa_index] = 0.0f;
	soa->input_yaw_dupe[soa_index] = 0.0f;
	soa->visual_shake_mult[soa_index] = 0.0f;
	soa->input_accel[soa_index] = 0.0f;
	soa->input_brake[soa_index] = 0.0f;
	soa->input_strafe[soa_index] = 0.0f;
	soa->input_steer_pitch[soa_index] = 0.0f;
	soa->height_above_track[soa_index] = 0.0f;
	soa->current_checkpoint[soa_index] = 0;
	soa->checkpoint_fraction[soa_index] = 0.0f;
	soa->lap[soa_index] = 0;
	soa->previous_lap_distance[soa_index] = 0.0f;
	soa->broken_lap_rollback_pending[soa_index] = false;
	soa->broken_lap_rollback_lap[soa_index] = 0;
	STORE_VEC3(visual_rotation, SimVec3());
	STORE_VEC3(unk_vec3_0x4e4, SimVec3());
	STORE_VEC3(unk_vec3_0x4f0, SimVec3());

	soa->energy[soa_index] = soa->calced_max_energy[soa_index];
	soa->boost_frames_manual[soa_index] = 0;
	soa->air_tilt[soa_index] = 0.0f;
	soa->boost_frames[soa_index] = 0;
	soa->input_strafe_32[soa_index] = 0.0f;
	soa->input_strafe_1_6[soa_index] = 0.0f;
	soa->frames_since_start_2[soa_index] = 0;
	soa->speed_kmh[soa_index] = 0.0f;
	soa->race_start_charge[soa_index] = 0.0f;
	soa->last_hit_tick[soa_index] = 0;
	soa->has_last_hit_tick[soa_index] = false;

	soa->grip_frames_from_accel_press[soa_index] = 0;
	soa->air_time[soa_index] = 0;
	soa->spinattack_angle[soa_index] = 0.0f;
	soa->spinattack_decrement[soa_index] = 0.0f;
	soa->spinattack_direction[soa_index] = 0;
	soa->damage_from_last_hit[soa_index] = 0.0f;
	soa->frames_since_start[soa_index] = 0;
	soa->side_attack_delay[soa_index] = 0;
	soa->attack_cooldown_frames[soa_index] = 0;
	soa->brake_timer[soa_index] = 0;
	soa->rail_collision_timer[soa_index] = 0;
	soa->terrain_state[soa_index] = 0;
	soa->machine_collision_frame_counter[soa_index] = 0;
	soa->frames_since_death[soa_index] = 0;
	soa->turning_related[soa_index] = 0.0f;
	soa->machine_crashed[soa_index] = false;
	soa->boost_delay_frame_counter[soa_index] = 0;
	soa->car_hit_invincibility[soa_index] = 0;
	soa->turn_reaction_input[soa_index] = 0.0f;
	soa->boost_energy_use_mult[soa_index] = soa->car_properties[soa_index] ? soa->car_properties[soa_index]->boost_energy_use_rate : 1.0f;
	soa->energy_recharge_mult[soa_index] = soa->car_properties[soa_index] ? soa->car_properties[soa_index]->energy_recharge_rate : 1.0f;
	soa->breakdown_frame_counter[soa_index] = 0;
	soa->some_breakdown_int[soa_index] = 0;
	soa->drift_sign[soa_index] = 1;
	soa->drift_ramp[soa_index] = 0.0;

	// Orient the machine at the spawn position
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = SimBasis().rotated(SimVec3(0, 1, 0), spawn_rot + PI); STORE_TRANSFORM(basis_physical, mxt_tmp); }
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical_other); mxt_tmp.basis = SimBasis().rotated(SimVec3(0, 1, 0), spawn_rot + PI); STORE_TRANSFORM(basis_physical_other, mxt_tmp); }

	update_pitch_transform_from_machine_front_back();

	// Visual transform matches physical orientation at reset
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.origin = LOAD_VEC3(position_current); STORE_TRANSFORM(transform_visual, mxt_tmp); }

	soa->base_speed[soa_index] = 0.0f;
	soa->boost_turbo[soa_index] = 0.0f;
	STORE_VEC3(position_behind, SimVec3());

	uint32_t state_mask_common = MACHINESTATE::B30 | MACHINESTATE::COMPLETEDRACE_2_Q |
	MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::B10 |
	MACHINESTATE::B9;
	if (reset_type == 0) {
		soa->machine_state[soa_index] &= state_mask_common;
		soa->state_2[soa_index] &= 1u;
	} else {
		soa->machine_state[soa_index] &= state_mask_common;
	}

	soa->state_2[soa_index] &= 0xfffffc4fu;

	SimTransform initial_placement_transform(LOAD_TRANSFORM(basis_physical).basis, LOAD_VEC3(position_current));

	const int point_base = soa_index * 4;
	const SimTransform reset_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 reset_position = LOAD_VEC3(position_current);
	const SimVec3x4 reset_tilt_pos = mxt_transform_points4(
		reset_transform,
		reset_position,
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	const SimVec3x4 reset_wall_pos = mxt_transform_points4(
		reset_transform,
		reset_position,
		sim_load4(soa->wall_offset_x + point_base),
		sim_load4(soa->wall_offset_y + point_base),
		sim_load4(soa->wall_offset_z + point_base));
	const SimVec3 reset_up = mxt_basis_rotate(reset_transform, SimVec3(0, 1, 0));
	const SimVec3 wall_pos_a = mxt_transform_point(reset_transform, reset_position, SimVec3(0.0f, 0.1f, 0.0f));
	for (int i = 0; i < 4; ++i) {
		const int p = point_base + i;
		soa->tilt_state[p] = 0;
		soa->tilt_force[p] = 0.0f;
		soa->tilt_force_spatial_len[p] = 0.0f;

		STORE_TILT_VEC3(force_spatial, p, SimVec3());
		STORE_TILT_VEC3(up_vector_2, p, reset_up);
		STORE_TILT_VEC3(up_vector, p, reset_up);

		STORE_WALL_VEC3(pos_a, p, wall_pos_a);
		STORE_WALL_VEC3(collision, p, SimVec3());
	}
	sim_store4(soa->tilt_pos_old_x + point_base, reset_tilt_pos.x);
	sim_store4(soa->tilt_pos_old_y + point_base, reset_tilt_pos.y);
	sim_store4(soa->tilt_pos_old_z + point_base, reset_tilt_pos.z);
	sim_store4(soa->tilt_pos_x + point_base, reset_tilt_pos.x);
	sim_store4(soa->tilt_pos_y + point_base, reset_tilt_pos.y);
	sim_store4(soa->tilt_pos_z + point_base, reset_tilt_pos.z);
	sim_store4(soa->wall_pos_b_x + point_base, reset_wall_pos.x);
	sim_store4(soa->wall_pos_b_y + point_base, reset_wall_pos.y);
	sim_store4(soa->wall_pos_b_z + point_base, reset_wall_pos.z);
};

void PhysicsCar::update_pitch_transform_from_machine_front_back()
{

	float fr_offset_z = soa->tilt_offset_z[POINT_INDEX(1)];
	float br_offset_z = soa->tilt_offset_z[POINT_INDEX(2)];

	float rotation_factor = 0.0f;
	if (std::abs(fr_offset_z) > 0.0001f)
		rotation_factor = (br_offset_z / -fr_offset_z) - 1.0f;

	float clamped_rotation = std::clamp(rotation_factor, -0.2f, 0.2f);
	float angle_rad = DEG_TO_RAD * (30.0f * clamped_rotation);

	SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_x(mxt_tmp, angle_rad);
	STORE_TRANSFORM(g_pitch_mtx_0x5e0, mxt_tmp);

};

void PhysicsCar::update_suspension_forces(
	int point_lane,
	const SimVec3& p0_ray_start_ws,
	const SimVec3& p0,
	const SimVec3& p1_ray_end_ws,
	const SimVec2& road_t_sample_raw,
	const SimTransform& surf,
	float stat_weight)
{
	const int p = POINT_INDEX(point_lane);

	float time_based_factor = 0.1f + static_cast<float>(soa->frames_since_start_2[soa_index]) / 90.0f;
	if (time_based_factor > 0.5f)
		time_based_factor = 0.5f;

	float dynamic_rest_offset = time_based_factor * 2.0f * soa->tilt_rest_length[p];

	float compression_metric = 0.0f;
	bool hit_found = false;

	if ((soa->tilt_state[p] & TILTSTATE::B6) != 0 || (soa->height_above_track[soa_index] <= 0.0f && (soa->tilt_state[p] & TILTSTATE::AIRBORNE))) {
		soa->tilt_state[p] |= TILTSTATE::DISCONNECTED;
	} else {
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			dd3d->call("draw_arrow", debug_gd_vec3(p0_ray_start_ws), debug_gd_vec3(p1_ray_end_ws), godot::Color(1.0f, 1.0f, 1.0f), 0.125, true, _TICK_DELTA);
			dd3d->call("draw_arrow", debug_gd_vec3(surf.origin), debug_gd_vec3(surf.origin + surf.basis[1].normalized() * 2.0f), godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
			DEBUG::disp_text("road t x", road_t_sample_raw.x);
			DEBUG::disp_text("road t y", road_t_sample_raw.y);
		}
		if (road_t_sample_raw.x == -1000.0f)
		{
			soa->tilt_state[p] |= TILTSTATE::DISCONNECTED;
			compression_metric = 0.0f;
		}
		else if (surf.basis[0].length_squared() >= 0.1) {
			const SimVec3 plane_n = surf.basis[1];
			const SimVec3 plane_p = surf.origin;
			const SimVec3 ray_dir = p1_ray_end_ws - p0_ray_start_ws;
			const float denom = ray_dir.dot(plane_n);
			float t = 0.0f;
			if (std::abs(denom) > 0.000001f) {
				t = (plane_p - p0_ray_start_ws).dot(plane_n) / denom;
			}
			hit_found = t >= 0.0f && t <= 1.0f;
			SimVec3 intersect = p0_ray_start_ws + ray_dir * t;
			if (hit_found){
				STORE_TILT_VEC3(pos, p, intersect);
				STORE_TILT_VEC3(up_vector_2, p, plane_n.normalized());

				float total_sweep_length = p0.distance_to(p1_ray_end_ws);
				float hit_fraction = 0.0f;
				if (total_sweep_length > 0.0001f) {
					hit_fraction =
					p0.distance_to(intersect) /
					total_sweep_length;
					hit_fraction = std::min(hit_fraction, 1.0f);
				}
				float actual_len = hit_fraction * total_sweep_length;
				float displacement_from_attachment_plane = -actual_len;
				compression_metric = displacement_from_attachment_plane + dynamic_rest_offset;
				if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA))
				{
					godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
					const SimVec3 corner_pos = LOAD_TILT_VEC3(pos, p);
					const SimVec3 corner_up = LOAD_TILT_VEC3(up_vector_2, p);
					dd3d->call("draw_arrow", debug_gd_vec3(corner_pos), debug_gd_vec3(corner_pos + corner_up * 2.0f), godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
					DEBUG::disp_text("hit_fraction", hit_fraction);
					DEBUG::disp_text("displacement_from_attachment_plane", displacement_from_attachment_plane);
				}
			}
		}

		if (hit_found) {
			soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::DISCONNECTED);
		} else {
			soa->tilt_state[p] |= TILTSTATE::DISCONNECTED;
			compression_metric = 0.0f;
		}
	}

	float calculated_force_magnitude = 0.0f;

	if (compression_metric > 0.0f) {
		soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::AIRBORNE);

		float current_compression = compression_metric;
		float damping1_force_component = 0.0f;

		if (dynamic_rest_offset < compression_metric) {
			damping1_force_component =
			0.5f * (compression_metric - soa->tilt_force[p]) * stat_weight;
			current_compression = dynamic_rest_offset;
		}

		float prev_frame_compression_metric = soa->tilt_force[p];
		soa->tilt_force[p] = current_compression;

		float mass_fraction = stat_weight / 1200.0f;
		float stiffness_k1 = 9000.0f;
		float damping_coeff_shared = 0.009f;
		float stiffness_k2_for_damping = 10000.0f;

		STORE_TILT_VEC3(up_vector, p, LOAD_TILT_VEC3(up_vector_2, p));

		float spring_force_comp =
		damping_coeff_shared * (stiffness_k1 * current_compression) *
		mass_fraction;

		float delta_compression = prev_frame_compression_metric - current_compression;
		float damping2_force_comp =
		mass_fraction * stiffness_k2_for_damping * damping_coeff_shared *
		delta_compression;

		calculated_force_magnitude =
		damping1_force_component + spring_force_comp - damping2_force_comp;
	} else {
		soa->tilt_state[p] |= TILTSTATE::AIRBORNE;
		soa->tilt_force[p] = 0.0f;
		STORE_TILT_VEC3(up_vector, p, SimVec3(0, 1, 0));
		if (soa->tilt_state[p] & TILTSTATE::DISCONNECTED)
			STORE_TILT_VEC3(up_vector_2, p, SimVec3(0, 1, 0));

		calculated_force_magnitude = 0.0f;
	}

	soa->tilt_force_spatial_len[p] = calculated_force_magnitude;
	STORE_TILT_VEC3(force_spatial, p, LOAD_TILT_VEC3(up_vector, p) * calculated_force_magnitude);
};

SimVec3 PhysicsCar::get_avg_track_normal_from_tilt_corners(TrackQueryScratch &scratch)
{
	const int point_base = soa_index * 4;
	const SimTransform machine_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 machine_position = LOAD_VEC3(position_current);
	const SimVec3 track_normal = LOAD_VEC3(track_surface_normal);
	const SimVec3 velocity_ws = LOAD_VEC3(velocity);
	const float stat_weight = soa->stat_weight[soa_index];
	const float inv_weight = 1.0f / std::max(stat_weight, 0.0001f);
	const float offset_add = std::max(0.0f, -((velocity_ws * inv_weight).dot(track_normal)));
	RaceTrack* track = soa->current_track[soa_index];
	const bool machine_grounded = (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0;
	const int collision_cp = soa->current_collision_checkpoint[soa_index];
	const int current_cp = soa->current_checkpoint[soa_index];
	const int analytic_cp =
		machine_grounded &&
		track &&
		collision_cp >= 0 &&
		collision_cp < track->num_checkpoints
			? collision_cp
			: -1;
	const int mesh_query_cp =
		track &&
		current_cp >= 0 &&
		current_cp < track->num_checkpoints
			? current_cp
			: analytic_cp;

	SimVec3 p0_ray_start_ws[4];
	SimVec3 p0_ws[4];
	SimVec3 p1_ray_end_ws[4];
	SimVec2 road_t[4];
	SimVec3 spatial_t[4];
	SimTransform surf[4];
	int mesh_corner_tri[4] = {-1, -1, -1, -1};
	bool corner_collided[4] = {false, false, false, false};
	const RoadData &center_floor_sample = soa->road_sample[soa_index];
	const bool center_floor_sample_valid =
		soa->height_above_track[soa_index] > 0.0f &&
		center_floor_sample.road_t.x != -1000.0f &&
		center_floor_sample.closest_surface.basis[0].length_squared() >= 0.1f;
	mxt_store_points4(
		p0_ray_start_ws,
		mxt_transform_points4(
			machine_transform,
			machine_position,
			sim_load4(soa->tilt_offset_x + point_base),
			sim_load4(soa->tilt_offset_y + point_base) + SimFloat4(0.01f + offset_add),
			sim_load4(soa->tilt_offset_z + point_base)));
	mxt_store_points4(
		p0_ws,
		mxt_transform_points4(
			machine_transform,
			machine_position,
			sim_load4(soa->tilt_offset_x + point_base),
			sim_load4(soa->tilt_offset_y + point_base),
			sim_load4(soa->tilt_offset_z + point_base)));
	mxt_store_points4(
		p1_ray_end_ws,
		mxt_transform_points4(
			machine_transform,
			machine_position,
			sim_load4(soa->tilt_offset_x + point_base),
			sim_load4(soa->tilt_offset_y + point_base) - SimFloat4(4.0f),
			sim_load4(soa->tilt_offset_z + point_base)));
	auto plane_ray_t = [](const SimVec3 &ray_start, const SimVec3 &ray_end, const SimTransform &surface) {
		const SimVec3 ray_dir = ray_end - ray_start;
		const SimVec3 plane_n = surface.basis[1];
		const float denom = ray_dir.dot(plane_n);
		if (std::abs(denom) <= 0.000001f) {
			return FLT_MAX;
		}
		const float t = (surface.origin - ray_start).dot(plane_n) / denom;
		return (t >= 0.0f && t <= 1.0f) ? t : FLT_MAX;
	};
	auto analytic_corner_in_domain = [](const SimVec2 &sample) {
		return sample.x >= -1.0f && sample.x <= 1.0f && sample.y >= -0.001f && sample.y <= 1.001f;
	};
	const bool use_analytic_corner_sample =
		track &&
		analytic_cp >= 0 &&
		track->segments[track->checkpoints[analytic_cp].road_segment].analytic_collision_enabled;
	if (use_analytic_corner_sample) {
		track->get_road_surface4_same_checkpoint(analytic_cp, p0_ws, road_t, spatial_t, surf);
		bool use_mesh_suspension_cast_candidates = false;
		SimAABB mesh_suspension_cast_bounds;
		CollisionData mesh_suspension_hits[4];
		if (mesh_query_cp >= 0 && track->num_mesh_collision_triangles > 0) {
			mesh_suspension_cast_bounds.position = p0_ray_start_ws[0];
			mesh_suspension_cast_bounds.size = SimVec3();
			mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[0]);
			for (int lane = 1; lane < 4; ++lane) {
				mesh_suspension_cast_bounds.expand_to(p0_ray_start_ws[lane]);
				mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[lane]);
			}
			use_mesh_suspension_cast_candidates = track->collect_mesh_cast_candidates(
				mesh_suspension_cast_bounds,
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
				scratch);
			if (use_mesh_suspension_cast_candidates) {
				track->cast_vs_mesh_candidates4_same_ray_fast(
					mesh_suspension_hits,
					p0_ray_start_ws,
					p1_ray_end_ws,
					CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
					mesh_query_cp,
					&scratch,
					true,
					false);
			}
		}
		for (int lane = 0; lane < 4; ++lane) {
			CollisionData hit{};
			hit.road_data.cp_idx = -1;
			hit.mesh_triangle_index = -1;
			if (use_mesh_suspension_cast_candidates) {
				hit = mesh_suspension_hits[lane];
			}
			if (!hit.collided) {
				continue;
			}
			const float analytic_t = analytic_corner_in_domain(road_t[lane])
				? plane_ray_t(p0_ray_start_ws[lane], p1_ray_end_ws[lane], surf[lane])
				: FLT_MAX;
			const float mesh_t = plane_ray_t(p0_ray_start_ws[lane], p1_ray_end_ws[lane], hit.road_data.closest_surface);
			if (mesh_t <= analytic_t) {
				if (hit.collision_normal.dot(mxt_basis_rotate(machine_transform, SimVec3(0.0f, 1.0f, 0.0f))) < 0.0f) {
					hit.collision_normal *= -1.0f;
					hit.road_data.closest_surface.basis[1] *= -1.0f;
					hit.road_data.closest_surface.basis[2] *= -1.0f;
				}
				corner_collided[lane] = true;
				mesh_corner_tri[lane] = hit.mesh_triangle_index;
				road_t[lane] = hit.road_data.road_t;
				spatial_t[lane] = hit.road_data.spatial_t;
				surf[lane] = hit.road_data.closest_surface;
			}
		}
	} else {
		bool use_mesh_suspension_cast_candidates = false;
		SimAABB mesh_suspension_cast_bounds;
		CollisionData mesh_suspension_hits[4];
		if (track && mesh_query_cp >= 0) {
			mesh_suspension_cast_bounds.position = p0_ray_start_ws[0];
			mesh_suspension_cast_bounds.size = SimVec3();
			mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[0]);
			for (int lane = 1; lane < 4; ++lane) {
				mesh_suspension_cast_bounds.expand_to(p0_ray_start_ws[lane]);
				mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[lane]);
			}
			use_mesh_suspension_cast_candidates = track->collect_mesh_cast_candidates(
				mesh_suspension_cast_bounds,
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
				scratch);
			if (use_mesh_suspension_cast_candidates) {
				track->cast_vs_mesh_candidates4_same_ray_fast(
					mesh_suspension_hits,
					p0_ray_start_ws,
					p1_ray_end_ws,
					CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
					mesh_query_cp,
					&scratch,
					true,
					false);
			}
		}
		for (int lane = 0; lane < 4; ++lane) {
			CollisionData hit{};
			hit.road_data.cp_idx = -1;
			hit.mesh_triangle_index = -1;
			if (use_mesh_suspension_cast_candidates) {
				hit = mesh_suspension_hits[lane];
			}
			if (track && mesh_query_cp >= 0) {
				if (!use_mesh_suspension_cast_candidates) {
					track->cast_vs_mesh_fast(
						hit,
						p0_ray_start_ws[lane],
						p1_ray_end_ws[lane],
						CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
						mesh_query_cp,
						&scratch,
						true,
						nullptr,
						false);
				}
			}
			if (!hit.collided && track && mesh_query_cp >= 0) {
				track->sample_mesh_floor_fast(
					hit,
					p0_ws[lane],
					8.0f,
					CAST_FLAGS::WANTS_TRACK,
					mesh_query_cp,
					false,
					&scratch,
					-1,
					true,
					false);
			}
			if (hit.collided) {
				if (hit.collision_normal.dot(mxt_basis_rotate(machine_transform, SimVec3(0.0f, 1.0f, 0.0f))) < 0.0f) {
					hit.collision_normal *= -1.0f;
					hit.road_data.closest_surface.basis[1] *= -1.0f;
					hit.road_data.closest_surface.basis[2] *= -1.0f;
				}
				corner_collided[lane] = true;
				mesh_corner_tri[lane] = hit.mesh_triangle_index;
				road_t[lane] = hit.road_data.road_t;
				spatial_t[lane] = hit.road_data.spatial_t;
				surf[lane] = hit.road_data.closest_surface;
			} else {
				road_t[lane].x = -1000.0f;
				spatial_t[lane] = SimVec3();
				surf[lane] = SimTransform();
			}
		}
	}

	SimVec3 normal_sum(0, 0, 0);
	int valid_count = 0;
	for (int i = 0; i < 4; ++i) {
		if (road_t[i].x == -1000.0f && center_floor_sample_valid) {
			road_t[i] = center_floor_sample.road_t;
			spatial_t[i] = center_floor_sample.spatial_t;
			surf[i] = center_floor_sample.closest_surface;
			mesh_corner_tri[i] = -2;
		}
		const int p = point_base + i;
		update_suspension_forces(i, p0_ray_start_ws[i], p0_ws[i], p1_ray_end_ws[i], road_t[i], surf[i], stat_weight);
		if ((soa->tilt_state[p] & TILTSTATE::AIRBORNE) == 0) {
			normal_sum += LOAD_TILT_VEC3(up_vector, p);
			++valid_count;
		}
	}

	if (valid_count > 0) {
		const SimVec3 avg_normal = normal_sum.normalized();
		if (trace_mesh_floor_for_car(soa, soa_index)) {
			godot::UtilityFunctions::print(
				godot::String("MXT_MESH_FLOOR_CORNERS tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" analytic_cp="), static_cast<int64_t>(analytic_cp),
				godot::String(" mesh_cp="), static_cast<int64_t>(mesh_query_cp),
				godot::String(" analytic_corner="), use_analytic_corner_sample,
				godot::String(" valid="), static_cast<int64_t>(valid_count),
				godot::String(" avg_n=("), avg_normal.x, godot::String(","), avg_normal.y, godot::String(","), avg_normal.z, godot::String(")"),
				godot::String(" c0=("), corner_collided[0], godot::String(","), static_cast<int64_t>(mesh_corner_tri[0]), godot::String(","), road_t[0].x, godot::String(","), road_t[0].y, godot::String(")"),
				godot::String(" c1=("), corner_collided[1], godot::String(","), static_cast<int64_t>(mesh_corner_tri[1]), godot::String(","), road_t[1].x, godot::String(","), road_t[1].y, godot::String(")"),
				godot::String(" c2=("), corner_collided[2], godot::String(","), static_cast<int64_t>(mesh_corner_tri[2]), godot::String(","), road_t[2].x, godot::String(","), road_t[2].y, godot::String(")"),
				godot::String(" c3=("), corner_collided[3], godot::String(","), static_cast<int64_t>(mesh_corner_tri[3]), godot::String(","), road_t[3].x, godot::String(","), road_t[3].y, godot::String(")"));
		}
		return avg_normal;
	}

	if (trace_mesh_floor_for_car(soa, soa_index)) {
		godot::UtilityFunctions::print(
			godot::String("MXT_MESH_FLOOR_CORNERS tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" analytic_cp="), static_cast<int64_t>(analytic_cp),
			godot::String(" mesh_cp="), static_cast<int64_t>(mesh_query_cp),
			godot::String(" analytic_corner="), use_analytic_corner_sample,
			godot::String(" valid=0"),
			godot::String(" c0=("), corner_collided[0], godot::String(","), static_cast<int64_t>(mesh_corner_tri[0]), godot::String(","), road_t[0].x, godot::String(","), road_t[0].y, godot::String(")"),
			godot::String(" c1=("), corner_collided[1], godot::String(","), static_cast<int64_t>(mesh_corner_tri[1]), godot::String(","), road_t[1].x, godot::String(","), road_t[1].y, godot::String(")"),
			godot::String(" c2=("), corner_collided[2], godot::String(","), static_cast<int64_t>(mesh_corner_tri[2]), godot::String(","), road_t[2].x, godot::String(","), road_t[2].y, godot::String(")"),
			godot::String(" c3=("), corner_collided[3], godot::String(","), static_cast<int64_t>(mesh_corner_tri[3]), godot::String(","), road_t[3].x, godot::String(","), road_t[3].y, godot::String(")"));
	}
	return SimVec3();
};

void PhysicsCar::set_terrain_state_from_track(TrackQueryScratch &scratch, const SimVec3 &trigger_p0, const SimVec3 &trigger_p1)
{
	uint32_t terrain_bits = soa->road_sample[soa_index].terrain;
	RaceTrack* track = soa->current_track[soa_index];
	if (soa->height_above_track[soa_index] > 0.0f && track != nullptr) {
		const uint32_t overlay_body = track->sample_mesh_terrain_overlay_fast(LOAD_VEC3(position_current), 3.0f);
		const uint32_t overlay_surface = track->sample_mesh_terrain_overlay_fast(LOAD_VEC3(track_surface_pos), 3.0f);
		terrain_bits |= overlay_body | overlay_surface;
		const TrackSegment &segment = track->segments[track->checkpoints[soa->current_checkpoint[soa_index]].road_segment];
		if (segment.analytic_collision_enabled) {
			CollisionData hit;
			track->cast_vs_track_fast(hit, LOAD_VEC3(position_current), LOAD_VEC3(position_current) + LOAD_VEC3(track_surface_normal) * -3,
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_TERRAIN | CAST_FLAGS::SAMPLE_FROM_P1,
				soa->current_checkpoint[soa_index],
				false,
				&scratch);
			if (hit.collided) {
				terrain_bits |= hit.road_data.terrain;
			}
		}
	}

	if (track != nullptr) for (int i = 0; i < track->num_trigger_colliders; i++)
	{
		TriggerCollider* trigger = track->trigger_colliders[i];
		uint8_t collision = trigger->intersect_segment(soa->current_checkpoint[soa_index], track, trigger_p0, trigger_p1);
		if ((collision & 0x1) != 0)
		{
			switch (trigger->type)
			{
			case TRIGGER_TYPE::DASHPLATE:
				soa->machine_state[soa_index] |= MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::BOOSTING_DASHPLATE;
				soa->terrain_state[soa_index] |= TERRAIN::DASH;
				scratch.push_trigger_event(soa_index, i, collision, static_cast<uint8_t>(trigger->type));
				break;
			case TRIGGER_TYPE::JUMPPLATE:
				soa->terrain_state[soa_index] |= TERRAIN::JUMP;
				soa->attack_cooldown_frames[soa_index] = 0;
				break;
			case TRIGGER_TYPE::MINE:
				collide_with_landmine(static_cast<Mine*>(trigger), trigger_p0, trigger_p1);
				scratch.push_trigger_event(soa_index, i, collision, static_cast<uint8_t>(trigger->type));
				break;
			default:
				break;
			}
		}
	}

	if (terrain_bits & TERRAIN::DASH) {
		soa->machine_state[soa_index] |= MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::BOOSTING_DASHPLATE;
		soa->terrain_state[soa_index] |= TERRAIN::DASH;
	}

	if ((terrain_bits & TERRAIN::RECHARGE) && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0) {
		soa->state_2[soa_index] |= 1;
		soa->terrain_state[soa_index] |= TERRAIN::RECHARGE;
		soa->energy[soa_index] += 1.111111f * soa->energy_recharge_mult[soa_index];
		if (soa->energy[soa_index] > soa->calced_max_energy[soa_index]) {
			soa->energy[soa_index] = soa->calced_max_energy[soa_index];
		}
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) == 0 && (terrain_bits & TERRAIN::DIRT)) {
		soa->terrain_state[soa_index] |= TERRAIN::DIRT;
	}

	if (terrain_bits & TERRAIN::ICE) {
		soa->terrain_state[soa_index] |= TERRAIN::ICE;
	}

	if (terrain_bits & TERRAIN::JUMP) {
		soa->terrain_state[soa_index] |= TERRAIN::JUMP;
		soa->attack_cooldown_frames[soa_index] = 0;
	}

	if (terrain_bits & TERRAIN::LAVA) {
		soa->terrain_state[soa_index] |= TERRAIN::LAVA;
	}

	if (terrain_bits & TERRAIN::HOLE) {
		soa->terrain_state[soa_index] |= TERRAIN::HOLE;
	}

	if (terrain_bits & TERRAIN::FALL) {
		trigger_mesh_fallout();
	}
};

void PhysicsCar::handle_attack_states()
{
	if (soa->attack_cooldown_frames[soa_index] > 0) {
		soa->attack_cooldown_frames[soa_index] -= 1;
	}

	if (soa->s_boost_active[soa_index]) {
		soa->machine_state[soa_index] &= ~(MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING);
		soa->side_attack_delay[soa_index] = 0;
		soa->spinattack_angle[soa_index] = 0.0f;
		soa->spinattack_decrement[soa_index] = 0.0f;
		return;
	}
	if (soa->speed_kmh[soa_index] < 300.0f) {
		if (soa->spinattack_angle[soa_index] == 0.0f)
			soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;
		soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
	}

	if (soa->side_attack_delay[soa_index] != 0)
		soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;

	if ((soa->machine_state[soa_index] & MACHINESTATE::SPINATTACKING) == 0) {
		soa->spinattack_angle[soa_index] = 0.0f;
	} else {
		float cur_angle = soa->spinattack_angle[soa_index];
		if (cur_angle == 0.0f) {
			if (soa->attack_cooldown_frames[soa_index] != 0) {
				soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;
			} else {
				soa->attack_cooldown_frames[soa_index] = kAttackCooldownFrames;
				soa->spinattack_angle[soa_index] = Math_PI * 8.0f;
				soa->spinattack_decrement[soa_index] = Math_PI * 0.125f * kSpinAttackShortenMultiplier;
				if (std::abs(soa->input_steer_yaw[soa_index]) > 0.1f) {
					soa->spinattack_direction[soa_index] = (soa->input_steer_yaw[soa_index] < 0.0f) ? 1 : 0;
				}
			}
		} else if (soa->spinattack_decrement[soa_index] < cur_angle) {
			soa->spinattack_angle[soa_index] = cur_angle - soa->spinattack_decrement[soa_index];
			if (soa->spinattack_angle[soa_index] < Math_PI * 4.0f) {
				soa->spinattack_decrement[soa_index] -= Math_PI * 130.0f / 65536.0f * kSpinAttackShortenMultiplier;
				if (soa->spinattack_decrement[soa_index] < Math_PI * 160.0f / 65536.0f * kSpinAttackShortenMultiplier)
					soa->spinattack_decrement[soa_index] = Math_PI * 160.0f / 65536.0f * kSpinAttackShortenMultiplier;
			}
		} else {
			soa->spinattack_angle[soa_index] = 0.0f;
			soa->spinattack_decrement[soa_index] = 0.0f;
			soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;
		}
		soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) == 0) {
		soa->side_attack_delay[soa_index] = 0;
	} else {
		uint8_t cur_delay = soa->side_attack_delay[soa_index];
		if (cur_delay == 0) {
			if (soa->attack_cooldown_frames[soa_index] != 0) {
				soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
			} else {
				soa->attack_cooldown_frames[soa_index] = kAttackCooldownFrames;
				soa->side_attack_delay[soa_index] = 6;
				soa->side_attack_indicator[soa_index] = 0.4f * soa->input_steer_yaw[soa_index];
			}
		} else if (cur_delay == 1) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
		} else {
			soa->side_attack_delay[soa_index] = cur_delay - 1;
		}

		if ((soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::TOOKDAMAGE)) != 0 ||
			soa->input_accel[soa_index] < 0.5f) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
		soa->side_attack_delay[soa_index] = 1;
	}
}

if (soa->machine_collision_frame_counter[soa_index] > 0)
	soa->machine_collision_frame_counter[soa_index] -= 1;
};

void PhysicsCar::apply_torque_from_force(const SimVec3& p_local_offset, const SimVec3& wf_world_force)
{
	SimVec3 lf = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), wf_world_force);
	soa->velocity_angular_x[soa_index] += -(p_local_offset.z * lf.y - p_local_offset.y * lf.z);
	soa->velocity_angular_y[soa_index] += -(p_local_offset.x * lf.z - p_local_offset.z * lf.x);
	soa->velocity_angular_z[soa_index] += -(p_local_offset.y * lf.x - p_local_offset.x * lf.y);
};

void PhysicsCar::simulate_machine_motion(PlayerInput in_input)
{

	soa->input_steer_yaw[soa_index] = in_input.steer_horizontal * std::abs(in_input.steer_horizontal);
	soa->input_steer_pitch[soa_index] = -in_input.steer_vertical;

	float in_strafe_left = std::min(1.0f, in_input.strafe_left * 1.25f);
	float in_strafe_right = std::min(1.0f, in_input.strafe_right * 1.25f);
	soa->input_strafe[soa_index] = (-in_strafe_left + in_strafe_right);

	float old_accel = soa->input_accel[soa_index];
	soa->input_accel[soa_index] = in_input.accelerate;
	bool accel_just_pressed = soa->input_accel[soa_index] > 0.5f && old_accel <= 0.5f;
	float old_brake = soa->input_brake[soa_index];
	soa->input_brake[soa_index] = in_input.brake;
	bool brake_just_pressed = soa->input_brake[soa_index] > 0.5f && old_brake <= 0.5f;

	float in_spinattack = in_input.spinattack ? 1.0f : 0.0f;
	float in_sideattack = 0.0f; // Placeholder: side attack not mapped

	if (in_strafe_left > 0.05f && in_strafe_right > 0.05f) {
		soa->machine_state[soa_index] |= MACHINESTATE::MANUAL_DRIFT;
	}

	if (accel_just_pressed) {
		soa->machine_state[soa_index] |= MACHINESTATE::JUSTTAPPEDACCEL | MACHINESTATE::B14;
	}

	soa->state_2[soa_index] |= 8u;

	TrackQueryScratch scratch;
	const uint32_t old_terrain_state = soa->terrain_state[soa_index];
	const SimVec3 trigger_p0 = LOAD_VEC3(position_old);
	const SimVec3 trigger_p1 = LOAD_VEC3(position_current);
	SimVec3 ground_normal = prepare_machine_frame(scratch);
	bool has_floor = find_floor_beneath_machine(scratch);
	if ((soa->machine_state[soa_index] & MACHINESTATE::B29) == 0) {
		set_terrain_state_from_track(scratch, trigger_p0, trigger_p1);
	}
	if (old_terrain_state & TERRAIN::DASH) {
		soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_HIT_DASHPLATE;
	}
	if (has_floor && (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		STORE_VEC3(track_surface_normal, ground_normal);
	} else if (has_floor && soa->height_above_track[soa_index] >= 16.0f) {
		soa->machine_state[soa_index] &= ~(MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q);
	} else {
		const int point_base = soa_index * 4;
		for (int lane = 0; lane < 4; ++lane) {
			const int p = point_base + lane;
			soa->tilt_force[p] = 0.0f;
			STORE_TILT_VEC3(force_spatial, p, SimVec3());
			soa->tilt_force_spatial_len[p] = 0.0f;
			soa->tilt_state[p] |= TILTSTATE::DISCONNECTED | TILTSTATE::AIRBORNE;
		}
	}

	project_velocity_to_local_frame();
	handle_steering();
	handle_suspension_states();

	float initial_angle_vel_y = soa->velocity_angular_y[soa_index];
	if (soa->frames_since_start_2[soa_index] > 0) {
		handle_machine_turn_and_strafe_points4(initial_angle_vel_y);
	}

	if (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNEMORE0_2S_Q) {
		soa->turning_related[soa_index] *= 0.02f;
	}
	if (std::abs(soa->input_strafe[soa_index]) > 0.01f) {
		soa->turning_related[soa_index] *= 0.04f;
	}

	handle_linear_velocity();
	handle_angle_velocity();

	handle_airborne_controls();
	orient_vehicle_from_gravity_or_road();
	handle_drag_and_glide_forces();

	float inv_weight = 1.0f / std::max(soa->stat_weight[soa_index], 0.001f);
	ADD_VEC3(position_current, LOAD_VEC3(velocity) * inv_weight + LOAD_VEC3(knockback_velocity));
	STORE_VEC3(knockback_velocity, LOAD_VEC3(knockback_velocity) * 0.93333334f);

	rotate_machine_from_angle_velocity();

	if (soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) {
		soa->machine_state[soa_index] &= ~(MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL);
	}

	if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) {
		uint32_t cd = soa->frames_since_start_2[soa_index];
		if (cd < 30) {
			if (cd % 6 == 0) {
				handle_startup_wobble();
			}
		} else if (cd < 90) {
			STORE_VEC3(velocity_angular, SimVec3());
		}
	}

	if (soa->rail_collision_timer[soa_index] > 0) {
		soa->rail_collision_timer[soa_index] -= 1;
	}

	soa->machine_state[soa_index] &= ~(MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
		MACHINESTATE::TOOKDAMAGE | MACHINESTATE::B14 |
		MACHINESTATE::MANUAL_DRIFT);

	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.orthonormalize(); STORE_TRANSFORM(basis_physical, mxt_tmp); }
	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		ADD_VEC3(position_bottom, LOAD_VEC3(position_current) - LOAD_VEC3(position_old));
	}
};

void PhysicsCar::sample_old_corner_collision_surface(TrackQueryScratch &scratch)
{
	soa->collision_old_cp[soa_index] = -1;
	soa->collision_old_valid[soa_index] = false;
	soa->collision_old_was_above[soa_index] = false;
	soa->collision_old_was_inside[soa_index] = false;
	soa->collision_old_road_t[soa_index] = SimVec2();
	soa->collision_old_spatial_t[soa_index] = SimVec3();
	soa->collision_old_surface[soa_index] = SimTransform();

	if (!soa->current_track[soa_index]) {
		return;
	}

	int use_cp_old = soa->current_track[soa_index]->get_best_checkpoint(LOAD_VEC3(position_old), soa->current_collision_checkpoint[soa_index], scratch);
	soa->collision_old_cp[soa_index] = use_cp_old;
	if (use_cp_old == -1) {
		return;
	}
	if (!soa->current_track[soa_index]->segments[soa->current_track[soa_index]->checkpoints[use_cp_old].road_segment].analytic_collision_enabled) {
		return;
	}

	SimVec2 use_t;
	SimVec3 use_spatial_t;
	SimTransform use_transform;
	soa->current_track[soa_index]->get_road_surface(use_cp_old, LOAD_VEC3(position_old), use_t, use_spatial_t, use_transform);
	if (soa->current_track[soa_index]->analytic_road_sample_has_hole(use_cp_old, use_t)) {
		return;
	}
	soa->collision_old_valid[soa_index] = true;
	soa->collision_old_road_t[soa_index] = use_t;
	soa->collision_old_spatial_t[soa_index] = use_spatial_t;
	soa->collision_old_surface[soa_index] = use_transform;
	soa->collision_old_was_above[soa_index] = (LOAD_VEC3(position_old) - use_transform.origin).dot(use_transform.basis[1]) >= -5.0f;
	soa->collision_old_was_inside[soa_index] = use_t.x > -1.0f && use_t.x < 1.0f;
}

int PhysicsCar::update_machine_corners(TrackQueryScratch &scratch) {
	STORE_VEC3(collision_push_track, SimVec3());
	STORE_VEC3(collision_push_rail, SimVec3());
	STORE_VEC3(collision_push_total, SimVec3());
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	int overall_hit_detected_flag = 0;
	float inv_weight   = 1.0f / soa->stat_weight[soa_index];
	SimVec3 inv_vel = LOAD_VEC3(velocity) * inv_weight;
	bool any_corner_hit = false;

	SimVec3 depenetration = SimVec3();
	SimVec3 total_depenetration = SimVec3();
	const int point_base = soa_index * 4;
	const SimTransform machine_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 machine_position = LOAD_VEC3(position_current);
	const RoadData &center_floor_sample = soa->road_sample[soa_index];
	const bool center_floor_sample_is_hole = (center_floor_sample.terrain & TERRAIN::HOLE) != 0u;
	const bool center_floor_sample_valid =
		soa->height_above_track[soa_index] > 0.0f &&
		center_floor_sample.closest_surface.basis[0].length_squared() >= 0.1f;
	const bool mesh_floor_depenetration_enabled =
		!center_floor_sample_is_hole &&
		(((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) ||
		center_floor_sample_valid);
	SimVec3 wall_corner_world[4];
	mxt_store_points4(
		wall_corner_world,
		mxt_transform_points4(
			machine_transform,
			machine_position,
			sim_load4(soa->wall_offset_x + point_base),
			sim_load4(soa->wall_offset_y + point_base),
			sim_load4(soa->wall_offset_z + point_base)));
	SimVec3 wall_corner_old_world[4];
	mxt_store_points4(
		wall_corner_old_world,
		mxt_transform_points4(
			LOAD_TRANSFORM(basis_physical_other),
			LOAD_VEC3(position_old),
			sim_load4(soa->wall_offset_x + point_base),
			sim_load4(soa->wall_offset_y + point_base),
			sim_load4(soa->wall_offset_z + point_base)));
	RaceTrack* track = soa->current_track[soa_index];
	auto analytic_rail_corner_hit_valid = [&](int cp_idx, const TrackEdgeRailSide &side,
		const SimVec3 &old_corner, const SimVec3 &new_corner, float rail_height) {
		const float new_depth = (new_corner - side.pos).dot(side.rail_n);
		if (new_depth >= 0.0f) {
			return false;
		}

		const SimVec3 final_hit = project_to_plane(side.rail_n, side.rail_n.dot(side.pos), new_corner);
		const float final_t = checkpoint_longitudinal_t(*track, cp_idx, final_hit);
		if (final_t >= 0.0f && final_t <= 1.0f && (final_hit - side.pos).dot(side.up_n) <= rail_height) {
			return true;
		}

		const float old_depth = (old_corner - side.pos).dot(side.rail_n);
		if (old_depth < 0.0f) {
			return false;
		}
		const float denom = old_depth - new_depth;
		if (denom <= 0.000001f) {
			return false;
		}
		const float alpha = old_depth / denom;
		if (alpha < 0.0f || alpha > 1.0f) {
			return false;
		}
		const SimVec3 sweep_hit = old_corner + (new_corner - old_corner) * alpha;
		const float sweep_t = checkpoint_longitudinal_t(*track, cp_idx, sweep_hit);
		if (sweep_t < 0.0f || sweep_t > 1.0f) {
			return false;
		}
		if ((sweep_hit - side.pos).dot(side.up_n) > rail_height) {
			return false;
		}
		return true;
	};

	const int use_cp_old = soa->collision_old_cp[soa_index];
	const bool old_valid = soa->collision_old_valid[soa_index];
	{
		SimVec2 use_t;
		SimVec3 use_spatial_t;
		SimTransform use_transform;
		bool was_above = false;
		bool was_inside = false;
		if (track) {
			if (old_valid)
			{
				use_t = soa->collision_old_road_t[soa_index];
				use_spatial_t = soa->collision_old_spatial_t[soa_index];
				use_transform = soa->collision_old_surface[soa_index];
				was_above = soa->collision_old_was_above[soa_index];
				was_inside = soa->collision_old_was_inside[soa_index];
				//DEBUG::disp_text("soa->current_collision_checkpoint[soa_index]", soa->current_collision_checkpoint[soa_index]);
				//DEBUG::disp_text("vehicle was_above", was_above);
				//DEBUG::disp_text("vehicle use_cp_old", use_cp_old);
				//DEBUG::disp_text("vehicle use_t", use_t);
				TrackSegment *old_seg = &track->segments[track->checkpoints[use_cp_old].road_segment];
				if (old_seg->analytic_collision_enabled && !center_floor_sample_is_hole) {
				if (use_t.x > -1.0f && use_t.x < 1.0f && use_t.y > 0.0f && use_t.y < 1.0f && was_above) {
					auto normal = use_transform.basis[1];
					auto plane_pos = use_transform.origin;
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;
						float depth = (p0 - plane_pos).dot(normal);
						if (depth >= 0.0f) continue;
						SimVec3 d = normal * (-depth);
						ADD_VEC3(collision_push_total, d);
						overall_hit_detected_flag |= 1;
						any_corner_hit = true;
						depenetration += d;
						ADD_VEC3(collision_push_track, d);
						soa->current_checkpoint[soa_index] = use_cp_old;
					}
				}
				if (old_seg->road_shape->supports_edge_rails() && was_inside && use_t.y > 0.0f && use_t.y < 1.0f && was_above) {
					RoadTransform root_t;
					RoadTransform root_derivative;
					const TrackSegment &segment     = track->segments[track->checkpoints[use_cp_old].road_segment];
					segment.curve_matrix->sample_with_derivative(root_t, root_derivative, use_t.y);
					TrackEdgeRailSide sides[2];
					segment.road_shape->get_edge_rail_sides(
						sides,
						use_t.y,
						use_transform.origin,
						root_t,
						root_derivative,
						segment.left_rail_height,
						segment.right_rail_height);
					draw_nearest_rail_candidate(sides, LOAD_VEC3(position_current) + depenetration, soa, soa_index, _TICK_DELTA);
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;
						for (int i = 0; i < 2; i++) {
							//if (i == 1 && use_t.x < -1.0f)
							//{
							//	continue;
							//}
							//if (i == 0 && use_t.x > 1.0f)
							//{
							//	continue;
							//}
							const TrackEdgeRailSide &side = sides[i];
							if (!track_segment_rail_side_active(segment, i, use_t.y))
							{
								continue;
							}
							if (side.height <= 0.f)
							{
								continue;
							}
							if (!analytic_rail_corner_hit_valid(use_cp_old, side, wall_corner_old_world[wc_idx] + depenetration, p0, side.height * root_t.scale.y)) {
								continue;
							}
							float depth = (p0 - side.pos).dot(side.rail_n);
							//DEBUG::disp_text("use_hit_t old", use_hit_t);
							SimVec3 d = side.rail_n * (-depth);
							ADD_VEC3(collision_push_total, d);
							any_corner_hit = true;
							depenetration += d;
							overall_hit_detected_flag |= 2;
							ADD_VEC3(collision_push_rail, d);
						}
					}
				}
				}
			}
				int use_cp_new = track->get_best_checkpoint(LOAD_VEC3(position_current) + depenetration, soa->current_collision_checkpoint[soa_index], scratch);
				bool new_valid = use_cp_new != -1;
				if (new_valid && center_floor_sample_is_hole) {
					new_valid = false;
				}
				if (new_valid)
				{
					const TrackSegment &new_segment = track->segments[track->checkpoints[use_cp_new].road_segment];
					if (!new_segment.analytic_collision_enabled) {
						new_valid = false;
					}
				}
				if (new_valid)
				{
					track->get_road_surface(use_cp_new, LOAD_VEC3(position_current) + depenetration, use_t, use_spatial_t, use_transform);
					if (use_t.x > -1.0f && use_t.x < 1.0f && use_t.y > 0.0f && use_t.y < 1.0f && was_above) {
					auto normal = use_transform.basis[1];
					auto plane_pos = use_transform.origin;
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;
						float depth = (p0 - plane_pos).dot(normal);
						if (depth >= 0.0f) continue;
						SimVec3 d = normal * (-depth);
						ADD_VEC3(collision_push_total, d);
						overall_hit_detected_flag |= 1;
						any_corner_hit = true;
						depenetration += d;
						ADD_VEC3(collision_push_track, d);
						soa->current_checkpoint[soa_index] = use_cp_new;
					}
				}
				TrackSegment *new_seg = &track->segments[track->checkpoints[use_cp_new].road_segment];
					if (new_seg->road_shape->supports_edge_rails() && was_inside && use_t.y > 0.0f && use_t.y < 1.0f && was_above) {
					RoadTransform root_t;
					RoadTransform root_derivative;
					const TrackSegment &segment     = track->segments[track->checkpoints[use_cp_new].road_segment];
					segment.curve_matrix->sample_with_derivative(root_t, root_derivative, use_t.y);
					TrackEdgeRailSide sides[2];
					segment.road_shape->get_edge_rail_sides(
						sides,
						use_t.y,
						use_transform.origin,
						root_t,
						root_derivative,
						segment.left_rail_height,
						segment.right_rail_height);
					draw_nearest_rail_candidate(sides, LOAD_VEC3(position_current) + depenetration, soa, soa_index, _TICK_DELTA);
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;

						for (int i = 0; i < 2; i++) {
							//if (i == 1 && use_t.x < -1.0f)
							//{
							//	continue;
							//}
							//if (i == 0 && use_t.x > 1.0f)
							//{
							//	continue;
							//}
							const TrackEdgeRailSide &side = sides[i];
							if (!track_segment_rail_side_active(segment, i, use_t.y))
							{
								continue;
							}
							if (side.height <= 0.f)
							{
								continue;
							}
							if (!analytic_rail_corner_hit_valid(use_cp_new, side, wall_corner_old_world[wc_idx] + depenetration, p0, side.height * root_t.scale.y)) {
								continue;
							}
							float depth = (p0 - side.pos).dot(side.rail_n);
							//DEBUG::disp_text("use_hit_t new", use_hit_t);
							SimVec3 d = side.rail_n * (-depth);
							ADD_VEC3(collision_push_total, d);
							any_corner_hit = true;
							depenetration += d;
							overall_hit_detected_flag |= 2;
							ADD_VEC3(collision_push_rail, d);
						}
						}
					}
				}
				if (track) {
					const int mesh_wall_cp = soa->current_checkpoint[soa_index];
					if (mesh_wall_cp >= 0 && mesh_wall_cp < track->num_checkpoints) {
						if (track->num_mesh_collision_triangles > 0) {
							SimAABB mesh_cast_bounds;
							mesh_cast_bounds.position = LOAD_VEC3(position_old);
							mesh_cast_bounds.size = SimVec3();
							mesh_cast_bounds.expand_to(machine_position);
							for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
								mesh_cast_bounds.expand_to(wall_corner_world[wc_idx]);
								mesh_cast_bounds.expand_to(wall_corner_old_world[wc_idx]);
							}
							const uint8_t mesh_cast_mask =
								(mesh_floor_depenetration_enabled ? CAST_FLAGS::WANTS_TRACK : 0) |
								CAST_FLAGS::WANTS_RAIL |
								CAST_FLAGS::WANTS_BACKFACE |
								CAST_FLAGS::SAMPLE_FROM_P1;
							const bool use_mesh_cast_candidates = track->collect_mesh_cast_candidates(mesh_cast_bounds, mesh_cast_mask, scratch);
							const SimVec3 mesh_side_reference_point = LOAD_VEC3(position_old);
							auto sweep_mesh_plane_and_depenetrate = [&](const SimVec3 &p0, const SimVec3 &p1) {
								CollisionData hit;
								if (use_mesh_cast_candidates && mesh_cast_bounds.has_point(p0) && mesh_cast_bounds.has_point(p1)) {
									track->cast_vs_mesh_candidates_fast(
										hit,
										p0,
										p1,
										mesh_cast_mask,
										mesh_wall_cp,
										&scratch,
										false,
										&mesh_side_reference_point);
								} else {
									track->cast_vs_mesh_fast(
										hit,
										p0,
										p1,
										mesh_cast_mask,
										mesh_wall_cp,
										&scratch,
										false,
										&mesh_side_reference_point);
								}
								if (!hit.collided) {
									return;
								}
								const bool kill_hit = (hit.road_data.terrain & TERRAIN::KILL) != 0;
								if (kill_hit) {
									trigger_mesh_kill_collision();
								}
								const bool rail_hit = terrain_mesh_blocks_like_rail(hit.road_data.terrain);
								for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
									const SimVec3 p = wall_corner_world[wc_idx] + depenetration;
									const float depth = (p - hit.collision_face_point).dot(hit.collision_face_normal);
									if (depth > 0.0f) {
										continue;
									}
									const SimVec3 d = hit.collision_face_normal * (-depth);
									ADD_VEC3(collision_push_total, d);
									if (rail_hit) {
										ADD_VEC3(collision_push_rail, d);
										overall_hit_detected_flag |= 2;
									} else {
										ADD_VEC3(collision_push_track, d);
										overall_hit_detected_flag |= 1;
									}
									any_corner_hit = true;
									depenetration += d;
								}
							};
							for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
								const SimVec3 p0 = LOAD_VEC3(position_old) + depenetration;
								const SimVec3 p1 = wall_corner_world[wc_idx] + depenetration;
								sweep_mesh_plane_and_depenetrate(p0, p1);
							}
							for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
								const SimVec3 p0 = wall_corner_old_world[wc_idx] + depenetration;
								const SimVec3 p1 = wall_corner_world[wc_idx] + depenetration;
								sweep_mesh_plane_and_depenetrate(p0, p1);
							}
						}
					}
				}
				ADD_VEC3(position_current, depenetration);
				total_depenetration += depenetration;
				depenetration = SimVec3();
			}
			return overall_hit_detected_flag;
	}
}


//void PhysicsCar::create_machine_visual_transform()
//{
//	float fVar12_initial_factor = 0.0f;
//	if (soa->base_speed[soa_index] <= 2.0f)
//		fVar12_initial_factor = (2.0f - soa->base_speed[soa_index]) * 0.5f;
//
//	if (soa->frames_since_start_2[soa_index] < 90)
//		fVar12_initial_factor *= static_cast<float>(soa->frames_since_start_2[soa_index]) / 90.0f;
//
//	soa->unk_stat_0x5d4[soa_index] += 0.05f * (fVar12_initial_factor - soa->unk_stat_0x5d4[soa_index]);
//
//	float dVar11_current_unk_stat = soa->unk_stat_0x5d4[soa_index];
//
//	float sin_val2_scaled_angle = static_cast<float>(soa->g_anim_timer[soa_index] * 0x1a3);
//	float sin_val2 = deterministic_fp::sinf(sin_val2_scaled_angle);
//
//	float y_offset_base = 0.006f * (dVar11_current_unk_stat * sin_val2);
//
//	SimVec3 visual_y_offset_world =
//	mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0.0f,
//		y_offset_base - (0.2f * dVar11_current_unk_stat),
//		0.0f));
//	SimVec3 target_visual_world_position = LOAD_VEC3(position_current) + visual_y_offset_world;
//
//
//	float fr_offset_z = soa->tilt_fl[soa_index].offset.z;
//	float br_offset_z = soa->tilt_bl[soa_index].offset.z;
//	float stagger_factor = 0.0f;
//	if (std::abs(fr_offset_z) > 0.0001f)
//		stagger_factor = (br_offset_z / -fr_offset_z) - 1.0f;
//	float clamped_stagger = std::clamp(stagger_factor, -0.2f, 0.2f);
//	float pitch_angle_deg = 30.0f * clamped_stagger;
//
//	if ((soa->state_2[soa_index] & 0x20u) == 0) {
//		if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) {
//			soa->turn_reaction_effect[soa_index] += 0.05f * (soa->turn_reaction_input[soa_index] - soa->turn_reaction_effect[soa_index]);
//			float yaw_reaction_rad = DEG_TO_RAD * soa->turn_reaction_effect[soa_index];
//		}
//
//		float world_vel_mag = LOAD_VEC3(velocity).length();
//		float speed_factor_for_roll_pitch = 0.0f;
//		if (std::abs(soa->stat_weight[soa_index]) > 0.0001f)
//			speed_factor_for_roll_pitch = (world_vel_mag / soa->stat_weight[soa_index]) / 4.629629629f;
//
//		soa->strafe_visual_roll[soa_index] = static_cast<int>(182.04445f * (soa->stat_strafe[soa_index] / 15.0f) * -5.0f *
//			soa->input_strafe_1_6[soa_index] * speed_factor_for_roll_pitch);
//
//		float banking_roll_angle_val_rad = 0.0f;
//		if (std::abs(soa->weight_derived_2[soa_index]) > 0.0001f)
//			banking_roll_angle_val_rad =
//		speed_factor_for_roll_pitch * 4.5f * (soa->velocity_angular_y[soa_index] / soa->weight_derived_2[soa_index]);
//		int banking_roll_angle_fz_units = static_cast<int>(10430.378f * banking_roll_angle_val_rad);
//
//		int total_roll_fz_units = banking_roll_angle_fz_units + soa->strafe_visual_roll[soa_index];
//
//		float abs_total_roll_float = std::abs(static_cast<float>(total_roll_fz_units));
//
//		float roll_damping_factor = 1.0f - abs_total_roll_float / 3640.0f;
//		roll_damping_factor = std::max(roll_damping_factor, 0.0f);
//
//		float current_visual_pitch_rad = 0.0f;
//		if (std::abs(soa->weight_derived_1[soa_index]) > 0.0001f)
//			current_visual_pitch_rad = soa->visual_rotation_x[soa_index] / soa->weight_derived_1[soa_index];
//		float pitch_visual_factor = roll_damping_factor * 0.7f * current_visual_pitch_rad;
//		pitch_visual_factor = std::clamp(pitch_visual_factor, -0.3f, 0.3f);
//
//		float current_visual_roll_rad = 0.0f;
//		if (std::abs(soa->weight_derived_3[soa_index]) > 0.0001f)
//			current_visual_roll_rad = soa->visual_rotation_z[soa_index] / soa->weight_derived_3[soa_index];
//		float roll_visual_factor = 2.5f * current_visual_roll_rad;
//		roll_visual_factor = std::clamp(roll_visual_factor, -0.5f, 0.5f);
//
//
//		float iVar1_from_block2_approx_deg =
//		0.5f * (dVar11_current_unk_stat *
//			deterministic_fp::sinf(static_cast<float>(soa->g_anim_timer[soa_index] * 0x109) *
//				(TAU / 65536.0f)));
//		int additional_roll_from_sin_fz_units =
//		static_cast<int>(182.04445f * iVar1_from_block2_approx_deg);
//
//		total_roll_fz_units += static_cast<int>(10430.378f * -roll_visual_factor);
//		total_roll_fz_units = std::clamp(total_roll_fz_units, -0x238e, 0x238e);
//
//		int final_roll_fz_units_for_z_rot = total_roll_fz_units + additional_roll_from_sin_fz_units;
//		float final_roll_rad_for_z_rot =
//		static_cast<float>(final_roll_fz_units_for_z_rot) * (TAU / 65536.0f);
//
//
//		soa->unk_quat_0x5c4[soa_index] = soa->unk_quat_0x5c4[soa_index].slerp(visual_delta_q, 0.2f);
//
//
//
//		if (soa->spinattack_angle[soa_index] != 0.0f) {
//			float use_angle = soa->spinattack_angle[soa_index];
//			while (use_angle > PI)
//			{
//				use_angle -= PI;
//			}
//			while (use_angle < -PI)
//			{
//				use_angle += PI;
//			}
//			if (soa->spinattack_direction[soa_index] == 0)
//			else
//		}
//	} else {
//	}
//
//
//	uint32_t uVar8_shake_seed = static_cast<uint32_t>(soa->velocity_z[soa_index] * 4000000.0f) ^
//	static_cast<uint32_t>(soa->velocity_x[soa_index] * 4000000.0f) ^
//	static_cast<uint32_t>(soa->velocity_y[soa_index] * 4000000.0f);
//
//	float shake_rand_norm1 =
//	static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(soa->velocity_angular_x[soa_index] * 4000000.0f)) &
//		0xffff) /
//	65535.0f;
//	float shake_rand_norm2 =
//	static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(soa->velocity_angular_y[soa_index] * 4000000.0f)) &
//		0xffff) /
//	65535.0f;
//
//	float shake_magnitude = 0.00006f * soa->visual_shake_mult[soa_index];
//	float x_shake_rad = shake_magnitude * shake_rand_norm1;
//	float z_shake_rad = shake_magnitude * shake_rand_norm2;
//
//	if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) == 0) {
//		soa->height_adjust_from_boost[soa_index] -= 0.05f * soa->height_adjust_from_boost[soa_index];
//	} else {
//		float effective_pitch_for_boost_lift = std::max(0.0f, soa->visual_rotation_x[soa_index]);
//		float target_height_adj = 0.0f;
//		if (std::abs(soa->weight_derived_1[soa_index]) > 0.0001f)
//			target_height_adj = 4.5f * (effective_pitch_for_boost_lift / soa->weight_derived_1[soa_index]);
//
//		soa->height_adjust_from_boost[soa_index] += 0.2f * (target_height_adj - soa->height_adjust_from_boost[soa_index]);
//		soa->height_adjust_from_boost[soa_index] = std::min(soa->height_adjust_from_boost[soa_index], 0.3f);
//	}
//
//
//	if (soa->terrain_state[soa_index] & TERRAIN::DIRT) {
//		float jitter_scale_factor = 0.1f + soa->speed_kmh[soa_index] / 900.0f;
//		jitter_scale_factor = std::min(jitter_scale_factor, 1.0f);
//
//		float rand_x_norm =
//		static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(soa->velocity_angular_y[soa_index] * 4000000.0f)) &
//			0xffff) /
//		65535.0f -
//		0.5f;
//		float rand_z_norm =
//		static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(soa->velocity_angular_z[soa_index] * 4000000.0f)) &
//			0xffff) /
//		65535.0f -
//		0.5f;
//
//		SimVec3 local_jitter_offset(rand_x_norm, 0.0f, rand_z_norm);
//		SimVec3 world_jitter_offset = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_jitter_offset);
//
//		SimVec3 scaled_world_jitter = world_jitter_offset * (0.15f * jitter_scale_factor);
//	}
//
//};

void PhysicsCar::handle_machine_collision_response()
{
	TrackQueryScratch scratch;
	int corner_collision_type_flag = update_machine_corners(scratch);

	float push_magnitude_rail = LOAD_VEC3(collision_push_rail).length();
	float push_magnitude_track = LOAD_VEC3(collision_push_track).length();
	float current_world_speed = LOAD_VEC3(velocity).length();

	float speed_over_weight = 0.0f;
	if (std::abs(soa->stat_weight[soa_index]) > 0.0001f)
		speed_over_weight = current_world_speed / soa->stat_weight[soa_index];

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_PIPE_FLOOR) &&
		(soa->global_start + soa_index) == 0 &&
		soa->height_above_track[soa_index] <= 0.0f &&
		(push_magnitude_track > 0.0001f || push_magnitude_rail > 0.0001f)) {
		int shape_type = -1;
		if (soa->current_track[soa_index] != nullptr &&
			soa->current_checkpoint[soa_index] < soa->current_track[soa_index]->num_checkpoints) {
			const CollisionCheckpoint &cp = soa->current_track[soa_index]->checkpoints[soa->current_checkpoint[soa_index]];
			if (cp.road_segment >= 0 && cp.road_segment < soa->current_track[soa_index]->num_segments) {
				shape_type = soa->current_track[soa_index]->segments[cp.road_segment].road_shape->shape_type;
			}
		}
		if (shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE ||
			shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN ||
			shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT ||
			shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN) {
			const RoadData &sample = soa->road_sample[soa_index];
			godot::UtilityFunctions::print(
				godot::String("MXT_PIPE_DEPEN_NO_FLOOR cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
				godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
				godot::String(" shape="), static_cast<int64_t>(shape_type),
				godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(soa->machine_state[soa_index]), 16),
				godot::String(" corner_flags="), static_cast<int64_t>(corner_collision_type_flag),
				godot::String(" road_t=("), sample.road_t.x, godot::String(","), sample.road_t.y, godot::String(")"),
				godot::String(" spatial=("), sample.spatial_t.x, godot::String(","), sample.spatial_t.y, godot::String(","), sample.spatial_t.z, godot::String(")"),
				godot::String(" pos=("), soa->position_current_x[soa_index], godot::String(","), soa->position_current_y[soa_index], godot::String(","), soa->position_current_z[soa_index], godot::String(")"),
				godot::String(" vel=("), soa->velocity_x[soa_index], godot::String(","), soa->velocity_y[soa_index], godot::String(","), soa->velocity_z[soa_index], godot::String(")"),
				godot::String(" push_track_len="), push_magnitude_track,
				godot::String(" push_rail_len="), push_magnitude_rail,
				godot::String(" push_total=("), soa->collision_push_total_x[soa_index], godot::String(","), soa->collision_push_total_y[soa_index], godot::String(","), soa->collision_push_total_z[soa_index], godot::String(")"));
		}
	}

	apply_machine_collision_response_from_corners(corner_collision_type_flag,
		push_magnitude_rail, push_magnitude_track, current_world_speed, speed_over_weight, true);
};

void PhysicsCar::apply_machine_collision_response_from_corners(int corner_collision_type_flag,
	float push_magnitude_rail, float push_magnitude_track, float current_world_speed,
	float speed_over_weight, bool include_start_projection)
{
	if (push_magnitude_track > 0.0023148148f) {
		if (corner_collision_type_flag & 1)
		{
			soa->machine_state[soa_index] |= MACHINESTATE::LOWGRIP;
		}
	}

	if (push_magnitude_rail > 0.0023148148f) {
		if ((corner_collision_type_flag & 2) && (soa->machine_state[soa_index] & MACHINESTATE::LOWGRIP) == 0)
			soa->machine_state[soa_index] |= MACHINESTATE::TOOKDAMAGE;
	}

	bool is_significant_collision_event =
	(push_magnitude_rail > 0.0046296296f) && (speed_over_weight > 0.0046296296f);

	bool apply_full_response = false;
	if (soa->frames_since_start_2[soa_index] > 0x3c && is_significant_collision_event &&
		(soa->machine_state[soa_index] & MACHINESTATE::TOOKDAMAGE)) {
		apply_full_response = true;
}

if (apply_full_response) {
	STORE_VEC3(collision_response, LOAD_VEC3(collision_push_total));

	if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP)
	{
		STORE_VEC3(velocity, LOAD_VEC3(velocity) + LOAD_VEC3(velocity).dot(LOAD_VEC3(collision_response).normalized()) * -LOAD_VEC3(collision_response).normalized() * 1.05f);
	}else{
		float dot_push_vel_norm = 0.0f;
		if (push_magnitude_rail > 0.0001f && current_world_speed > 0.0001f)
			dot_push_vel_norm = LOAD_VEC3(collision_push_total).normalized().dot(LOAD_VEC3(velocity).normalized());

		float clamped_opposing_dot_prod = std::min(dot_push_vel_norm, 0.0f);

		float response_intensity_factor = 0.0f;
		if (speed_over_weight > 0.02314814814f) {
			float dot_push_track_normal = 0.0f;
			if (push_magnitude_rail > 0.0001f && LOAD_VEC3(track_surface_normal).length_squared() > 0.0001f)
				dot_push_track_normal =
			LOAD_VEC3(collision_push_total).normalized().dot(LOAD_VEC3(track_surface_normal).normalized());

			if (std::abs(dot_push_track_normal) < 0.7f) {
				response_intensity_factor =
				(0.15f + (clamped_opposing_dot_prod * clamped_opposing_dot_prod)) / 1.5f;

				if ((soa->machine_state[soa_index] & MACHINESTATE::B10) == 0) {
					response_intensity_factor =
					(response_intensity_factor * current_world_speed) / 500.0f;
					if (soa->rail_collision_timer[soa_index] != 0)
						response_intensity_factor *= 0.15f;
				} else {
					response_intensity_factor =
					(response_intensity_factor * current_world_speed) / 2000.0f;
				}
			}
		}

		if (clamped_opposing_dot_prod < -0.5f) {
			soa->machine_state[soa_index] &= ~(MACHINESTATE::JUST_HIT_DASHPLATE |
				MACHINESTATE::BOOSTING_DASHPLATE |
				MACHINESTATE::JUST_PRESSED_BOOST |
				MACHINESTATE::BOOSTING);
			soa->machine_state[soa_index] &= ~(MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING);
			soa->boost_frames[soa_index] = 0;
			soa->boost_frames_manual[soa_index] = 0;
		}

		if ((soa->machine_state[soa_index] & MACHINESTATE::TOOKDAMAGE) && soa->breakdown_frame_counter[soa_index] == 0) {
			float damage_base = response_intensity_factor * soa->stat_body[soa_index];
			if ((soa->machine_state[soa_index] & MACHINESTATE::B10) == 0 && damage_base > 20.0f)
				damage_base = 20.0f;

			float max_damage_this_hit = 1.01f * soa->calced_max_energy[soa_index];
			float actual_damage_taken = std::min(damage_base, max_damage_this_hit);
			soa->damage_from_last_hit[soa_index] = actual_damage_taken;
			soa->energy[soa_index] -= actual_damage_taken;

			if (soa->energy[soa_index] < 0.0f) {
				if ((soa->machine_state[soa_index] & (MACHINESTATE::COMPLETEDRACE_1_Q|MACHINESTATE::ZEROHP)) == 0) {
					soa->breakdown_frame_counter[soa_index] = 0x3c;
				}
				soa->energy[soa_index] = 0.0f;
				soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
				soa->base_speed[soa_index] = 0.0f;
			}
		}

		SimVec3 response_impulse_base;
		if (push_magnitude_rail > 0.0001f)
			response_impulse_base = LOAD_VEC3(collision_push_total).normalized() *
		(clamped_opposing_dot_prod * current_world_speed);
		else
			response_impulse_base = SimVec3();

		if (clamped_opposing_dot_prod < 0.0f) {
			float ratio_clamped_dot = clamped_opposing_dot_prod / 0.7f;
			float val_inside_sqrt = std::max(0.0f, 1.0f - (ratio_clamped_dot * ratio_clamped_dot));
			float sqrt_factor = std::sqrt(val_inside_sqrt);

			float base_speed_mult;
			float boost_turbo_additional_mult;

			if (soa->rail_collision_timer[soa_index] == 0) {
				base_speed_mult = 0.2f + 0.6f * sqrt_factor;
				boost_turbo_additional_mult = 0.4f * base_speed_mult;
			} else {
				base_speed_mult = 0.64f + 0.35f * sqrt_factor;
				boost_turbo_additional_mult = 0.6f * base_speed_mult;
			}
			if (!soa->s_boost_active[soa_index]){
				soa->base_speed[soa_index] *= base_speed_mult;
			}
			soa->boost_turbo[soa_index] *= (0.3f + boost_turbo_additional_mult);
		}

		if (speed_over_weight <= 1.851851851f) {
			ADD_VEC3(velocity, response_impulse_base * -1.0f);
		} else {
			float final_impulse_scale_factor;
			if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
				final_impulse_scale_factor = 3.4f - 1.7f * std::abs(clamped_opposing_dot_prod);
			} else if (soa->rail_collision_timer[soa_index] == 0) {
				final_impulse_scale_factor = 3.0f - 1.5f * std::abs(clamped_opposing_dot_prod);
			} else {
				final_impulse_scale_factor = 2.0f - std::abs(clamped_opposing_dot_prod);
			}

			ADD_VEC3(velocity, response_impulse_base * (-final_impulse_scale_factor));

			if (soa->rail_collision_timer[soa_index] == 0) {
				set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			}
			soa->rail_collision_timer[soa_index] = 20;
		}

		if (response_impulse_base.length_squared() > 0.000001f) {
			SimVec3 impulse_local_for_visuals =
			mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), response_impulse_base);
			soa->visual_rotation_z[soa_index] += impulse_local_for_visuals.x;
			soa->visual_rotation_x[soa_index] += impulse_local_for_visuals.z;
		}

		if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) {
			for (int i = 0; i < 4; ++i) {
				apply_torque_from_force(LOAD_VEC3(track_surface_normal), response_impulse_base * -0.002f);
			}
		}

		if (soa->frames_since_start_2[soa_index] > 60)
			align_machine_y_with_track_normal_immediate();
	}

} else if ((soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) &&
	speed_over_weight >= 0.0462962962962f) {
	SimVec3 up_dir = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 1, 0)); // get the vehicle's local up direction normal vector
	float up_dot_track = normalized_safe(up_dir).dot(normalized_safe(LOAD_VEC3(track_surface_normal)));
	float vel_dot_track = normalized_safe(LOAD_VEC3(velocity)).dot(normalized_safe(LOAD_VEC3(track_surface_normal)));
	if (up_dot_track < 0.0f)
		up_dot_track = 0.0f;
	const float landing_penalty_factor = landing_alignment_penalty_factor(soa->air_time[soa_index]);
	const float effective_up_dot_track = 1.0f - ((1.0f - up_dot_track) * landing_penalty_factor);
	const SimVec3 velocity_before_landing_penalty = LOAD_VEC3(velocity);
	float vel_along_track = LOAD_VEC3(velocity).length() * vel_dot_track;
	soa->base_speed[soa_index] = soa->base_speed[soa_index] * effective_up_dot_track;
	SimVec3 normal_vel = LOAD_VEC3(track_surface_normal) * vel_along_track;
	float vel_align_factor = 2.0f * std::abs(0.5f + vel_dot_track);
	SimVec3 vel_add = LOAD_VEC3(velocity) - normal_vel;
	if (soa->air_time[soa_index] < 10 && soa->energy[soa_index] > 0.001f)
	{
		STORE_VEC3(velocity, LOAD_VEC3(velocity) * 1.4f);
		soa->base_speed[soa_index] += 1.6f;
	}else
	{
		vel_add = set_vec3_length(vel_add, 0.9f * (1.0f - 1.11f * vel_align_factor) * up_dot_track);
		const SimVec3 full_penalty_velocity =
			velocity_before_landing_penalty - normal_vel * up_dot_track + vel_add;
		STORE_VEC3(velocity,
			velocity_before_landing_penalty +
			(full_penalty_velocity - velocity_before_landing_penalty) * landing_penalty_factor);
	}
	soa->air_time[soa_index] = 0;
}

	if (include_start_projection && soa->frames_since_start_2[soa_index] <= 90)
	{
		ADD_VEC3(velocity, LOAD_VEC3(track_surface_normal) * -(LOAD_VEC3(velocity).dot(LOAD_VEC3(track_surface_normal))));
	}
	if (soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) {
		soa->air_time[soa_index] = 0;
	}
};

void PhysicsCar::align_machine_y_with_track_normal_immediate()
{
	if (LOAD_VEC3(track_surface_normal).length_squared() < 0.0001f)
		return;

	SimVec3 safe_track_normal = LOAD_VEC3(track_surface_normal).normalized();
	SimVec3 machine_current_world_up = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 1, 0));

	if (machine_current_world_up.length_squared() < 0.0001f)
		return;

	SimVec3 safe_machine_world_up = machine_current_world_up.normalized();

	SimQuat delta_rotation_q = SimQuat(safe_machine_world_up, safe_track_normal);
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = SimBasis(delta_rotation_q) * mxt_tmp.basis; STORE_TRANSFORM(basis_physical, mxt_tmp); }
};

void PhysicsCar::handle_checkpoints(TrackQueryScratch &scratch)
{
	if (!soa->current_track[soa_index] || soa->current_track[soa_index]->num_checkpoints == 0)
		return;

	RaceTrack *track = soa->current_track[soa_index];
	uint8_t prev_lap = soa->lap[soa_index];

	int found = track->get_best_checkpoint(LOAD_VEC3(position_current), soa->current_checkpoint[soa_index], scratch);
	int collision = found;
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0 || found == -1)
	{
		collision = track->get_best_checkpoint(LOAD_VEC3(position_current), scratch);
	}
	//if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0 && found == -1)
	//{
	//	found = collision;
	//}
	soa->current_collision_checkpoint[soa_index] = static_cast<int16_t>(collision);
	if (found >= 0 && found < track->num_checkpoints && found != soa->current_checkpoint[soa_index]) {
		uint8_t proposed_lap = soa->lap[soa_index];
		int lap_delta = 0;
		const int lap_line_window = std::max(1, track->num_checkpoints / 8);
		const bool found_after_lap_line = found < lap_line_window;
		const bool found_before_lap_line = found >= track->num_checkpoints - lap_line_window;
		const bool current_after_lap_line = soa->current_checkpoint[soa_index] < lap_line_window;
		const bool current_before_lap_line = soa->current_checkpoint[soa_index] >= track->num_checkpoints - lap_line_window;
		if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) != 0 & (soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0)
		{
			if (found_after_lap_line && current_before_lap_line) {
				proposed_lap += 1;
				lap_delta = 1;
			} else if (current_after_lap_line && found_before_lap_line) {
				if (proposed_lap > 0) {
					proposed_lap -= 1;
					lap_delta = -1;
				}
			}
		}

		soa->lap[soa_index] = proposed_lap;
		soa->current_checkpoint[soa_index] = static_cast<uint16_t>(found);
		if (lap_delta < 0) {
			soa->broken_lap_rollback_pending[soa_index] = false;
			soa->broken_lap_rollback_lap[soa_index] = 0;
		}
		if (lap_delta > 0 && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) {
			soa->broken_lap_rollback_pending[soa_index] = true;
			soa->broken_lap_rollback_lap[soa_index] = proposed_lap;
		}
	}

	if (soa->lap[soa_index] > 3){
		soa->machine_state[soa_index] |= MACHINESTATE::COMPLETEDRACE_1_Q;
		soa->broken_lap_rollback_pending[soa_index] = false;
		soa->broken_lap_rollback_lap[soa_index] = 0;
	}

	const CollisionCheckpoint &cur_cp = track->checkpoints[soa->current_checkpoint[soa_index]];
	SimVec3 p1 = cur_cp.start_plane.project(LOAD_VEC3(position_current));
	SimVec3 p2 = cur_cp.end_plane.project(LOAD_VEC3(position_current));
	float t = get_closest_t_on_segment(LOAD_VEC3(position_current), p1, p2);
	soa->checkpoint_fraction[soa_index] = t;
	soa->lap_progress[soa_index] = (static_cast<float>(soa->current_checkpoint[soa_index]) + t) / static_cast<float>(track->num_checkpoints);

	float cp_length = cur_cp.local_distance;
	float cp_start_distance = cur_cp.distance - cur_cp.local_distance;
	float ground_distance = cp_start_distance + cp_length * std::clamp(soa->checkpoint_fraction[soa_index], 0.0f, 1.0f);
	float lap_length = track_lap_length(track);
	if (lap_length > 0.0f) {
		ground_distance = std::fmod(ground_distance, lap_length);
		if (ground_distance < 0.0f)
			ground_distance += lap_length;
	}
	soa->checkpoint_track_distance[soa_index] = ground_distance;

	const float current_lap_distance = track->compute_lap_distance(
		soa->current_checkpoint[soa_index],
		soa->checkpoint_fraction[soa_index],
		soa->lap[soa_index]);
	if (soa->restore_state[soa_index] == 0 &&
		(soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0) {
		if (current_lap_distance - soa->previous_lap_distance[soa_index] > kMaxPositiveCheckpointAdvance) {
			start_restore_to_last_ground();
		}
	}
	soa->previous_lap_distance[soa_index] = current_lap_distance;

	if (soa->lap[soa_index] != prev_lap) {
		soa->machine_state[soa_index] |= MACHINESTATE::CROSSEDLAPLINE_Q;
	}
};

void PhysicsCar::collide_with_landmine(Mine* in_mine, const SimVec3 &travel_start, const SimVec3 &travel_end)
{	
	if (in_mine->exploded)
	{
		return;
	}

	SimVec3 mine_pos = in_mine->transform.origin;// + in_mine->transform.basis.get_column(1);

	SimVec3 travel_vec = travel_end - travel_start;

	SimVec3 prev_to_mine = mine_pos - travel_start;

	float travel_len = travel_vec.length();

	float t = travel_vec.dot(prev_to_mine) / (travel_len * travel_len);

	SimVec3 closest_on_path = travel_vec * t + travel_start;

	float speed_dir_sign = (soa->speed_kmh[soa_index] > 600.0f) ? 1.0f : -1.0f;

	SimVec3 mine_to_path = closest_on_path - mine_pos;

	float mine_to_path_len = mine_to_path.length();

	float normal_dot = LOAD_VEC3(track_surface_normal).dot(mine_to_path.normalized());

	float normal_proj_len = mine_to_path_len * normal_dot;

	SimVec3 point_on_track_plane = LOAD_VEC3(track_surface_normal) * normal_proj_len + mine_pos;

	SimVec3 planar_component = closest_on_path - point_on_track_plane;

	float planar_length = planar_component.length();
	
	// TODO: replace sqrt with a deterministic_fp variant
	float cosTheta = sqrt(1.0 - planar_length * 0.25 * planar_length * 0.25);
	float signedCosThetaD = speed_dir_sign * cosTheta;
	float sinTheta = sqrt(1.0 - (signedCosThetaD * signedCosThetaD));

	SimVec3 normalDir = {0,0,0};
	if(sinTheta > 0.0000001)
		normalDir = set_vec3_length(planar_component, sinTheta);

	SimVec3 travelDirScaled = {0,0,0};
	if(signedCosThetaD > (0.0000001))
		travelDirScaled = set_vec3_length(travel_vec, signedCosThetaD);

	SimVec3 kickDir = normalDir + travelDirScaled;

	kickDir = set_vec3_length(kickDir, 4.0);

	STORE_VEC3(position_current, kickDir + point_on_track_plane);

	SimVec3 displacementWorld = LOAD_VEC3(position_current) - mine_pos;

	displacementWorld = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), displacementWorld);	// → local

	float displacementLen = displacementWorld.length();

	displacementWorld = set_vec3_length(displacementWorld, 5.555555 * soa->stat_weight[soa_index]);

	soa->visual_rotation_z[soa_index]  += 6.0f * displacementWorld.x;
	soa->visual_rotation_x[soa_index] += 2.0f * displacementWorld.z;

	displacementWorld = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), displacementWorld);

	soa->velocity_x[soa_index] += displacementWorld.x;
	soa->velocity_y[soa_index] += displacementWorld.y;
	soa->velocity_z[soa_index] += displacementWorld.z;

	//------------------------------------------------------------------
	// 8) Damage & state flags (unchanged logic)
	//------------------------------------------------------------------

	soa->terrain_state[soa_index] |= 0x40000000;			// “hit mine” flag

	if(!soa->s_boost_active[soa_index] && soa->breakdown_frame_counter[soa_index] == 0)
	{
		float damage = 20.0f * soa->stat_body[soa_index];

		if((soa->machine_state[soa_index] & MACHINESTATE::B10) == 0 && damage > 20.0f)
			damage = 20.0f;

		float maxFrameDamage = 1.01f * soa->calced_max_energy[soa_index];

		if(damage > maxFrameDamage) damage = maxFrameDamage;

		soa->damage_from_last_hit[soa_index] = damage;
		soa->energy[soa_index] -= damage;

		if(soa->energy[soa_index] < 0.0f)
		{
			if((soa->machine_state[soa_index] & (MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::ZEROHP)) == 0)
				soa->breakdown_frame_counter[soa_index] = 60;

			soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
			soa->energy[soa_index]	  = 0.0f;
			soa->base_speed[soa_index] = 0.0f;
		}
	}

}


bool PhysicsCar::compute_respawn_target(uint16_t cp_idx, SimTransform &out_transform, float &out_distance, uint16_t *out_checkpoint, float *out_fraction) const
{
	out_transform = SimTransform();
	out_distance = soa->last_ground_distance[soa_index];
	if (out_checkpoint) {
		*out_checkpoint = cp_idx;
	}
	if (out_fraction) {
		*out_fraction = 0.0f;
	}

	if (!soa->current_track[soa_index] || soa->current_track[soa_index]->num_checkpoints == 0 || cp_idx >= soa->current_track[soa_index]->num_checkpoints)
		return false;

	const int num_checkpoints = soa->current_track[soa_index]->num_checkpoints;
	const CollisionCheckpoint &start_cp = soa->current_track[soa_index]->checkpoints[cp_idx];

	float lap_length = soa->current_track[soa_index]->lap_length;
	if (lap_length <= 0.0f && num_checkpoints > 0) {
		lap_length = soa->current_track[soa_index]->checkpoints[num_checkpoints - 1].distance;
	}
	const bool has_lap_length = lap_length > 0.0f;

	auto normalize_distance = [&](float dist) -> float {
		if (!has_lap_length)
			return dist;
		float normalized = std::fmod(dist, lap_length);
		if (normalized < 0.0f)
			normalized += lap_length;
		return normalized;
	};

	const float start_cp_length = std::max(start_cp.local_distance, kMinCheckpointDistance);
	const float cp_start_distance = start_cp.distance - start_cp.local_distance;
	const float normalized_ground = normalize_distance(soa->last_ground_distance[soa_index]);
	const float normalized_cp_start = normalize_distance(cp_start_distance);

	float distance_into_cp = normalized_ground - normalized_cp_start;
	if (distance_into_cp < 0.0f && has_lap_length)
		distance_into_cp += lap_length;
	distance_into_cp = std::clamp(distance_into_cp, 0.0f, start_cp_length);

	float remaining = kRespawnForwardDistance;
	if (has_lap_length) {
		remaining = std::fmod(kRespawnForwardDistance, lap_length);
		if (remaining < 0.0f)
			remaining += lap_length;
		if (remaining == 0.0f)
			remaining = lap_length;
	}

	int target_cp_idx = cp_idx;
	float target_fraction = 0.0f;

	const float distance_to_cp_end = std::max(start_cp_length - distance_into_cp, 0.0f);
	if (remaining <= distance_to_cp_end || num_checkpoints == 1) {
		float along = distance_into_cp + remaining;
		float denom = std::max(start_cp_length, kMinCheckpointDistance);
		target_fraction = std::clamp(along / denom, 0.0f, 1.0f);
	} else {
		remaining -= distance_to_cp_end;
		int idx = (cp_idx + 1) % num_checkpoints;
		int last_idx = idx;
		for (int step = 0; step < num_checkpoints; ++step) {
			last_idx = idx;
			const CollisionCheckpoint &candidate = soa->current_track[soa_index]->checkpoints[idx];
			float candidate_length = std::max(candidate.local_distance, kMinCheckpointDistance);
			if (remaining <= candidate_length) {
				target_cp_idx = idx;
				target_fraction = std::clamp(remaining / candidate_length, 0.0f, 1.0f);
				remaining = 0.0f;
				break;
			}
			remaining -= candidate_length;
			idx = (idx + 1) % num_checkpoints;
		}
		if (remaining > 0.0f) {
			target_cp_idx = last_idx;
			target_fraction = 1.0f;
		}
	}

	const CollisionCheckpoint &target_cp = soa->current_track[soa_index]->checkpoints[target_cp_idx];
	float t_y = target_cp.t_start + (target_cp.t_end - target_cp.t_start) * target_fraction;
	t_y = std::clamp(t_y, std::min(target_cp.t_start, target_cp.t_end), std::max(target_cp.t_start, target_cp.t_end));

	soa->current_track[soa_index]->segments[target_cp.road_segment]
	.road_shape->get_oriented_transform_at_time(out_transform, SimVec2(0.0f, t_y));
	out_transform.basis.orthonormalize();
	out_transform.basis = out_transform.basis.rotated(out_transform.basis.get_column(1), Math_PI);
	out_transform.origin += out_transform.basis.get_column(1) * 0.1f;

	float new_distance = soa->last_ground_distance[soa_index] + kRespawnForwardDistance;
	if (has_lap_length) {
		new_distance = std::fmod(new_distance, lap_length);
		if (new_distance < 0.0f)
			new_distance += lap_length;
	}
	out_distance = new_distance;
	if (out_checkpoint) {
		*out_checkpoint = static_cast<uint16_t>(target_cp_idx);
	}
	if (out_fraction) {
		*out_fraction = target_fraction;
	}

	return true;
}

void PhysicsCar::respawn_at_checkpoint(uint16_t cp_idx)
{
	if (!soa->current_track[soa_index] || cp_idx >= soa->current_track[soa_index]->num_checkpoints)
		return;

	SimTransform spawn_transform;
	float respawn_distance = soa->last_ground_distance[soa_index];
	uint16_t respawn_checkpoint = cp_idx;
	float respawn_fraction = 0.0f;
	if (!compute_respawn_target(cp_idx, spawn_transform, respawn_distance, &respawn_checkpoint, &respawn_fraction))
		return;

	if (soa->broken_lap_rollback_pending[soa_index] &&
		(soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0) {
		const float lap_length = track_lap_length(soa->current_track[soa_index]);
		if (soa->lap[soa_index] == soa->broken_lap_rollback_lap[soa_index] &&
			soa->lap[soa_index] > 0 &&
			track_distance_is_before_lap_line(respawn_distance, lap_length)) {
			soa->lap[soa_index] -= 1;
			soa->machine_state[soa_index] |= MACHINESTATE::CROSSEDLAPLINE_Q;
		}
	}
	soa->broken_lap_rollback_pending[soa_index] = false;
	soa->broken_lap_rollback_lap[soa_index] = 0;

	soa->last_ground_distance[soa_index] = respawn_distance;
	soa->last_ground_checkpoint[soa_index] = respawn_checkpoint;
	soa->current_checkpoint[soa_index] = respawn_checkpoint;
	soa->current_collision_checkpoint[soa_index] = static_cast<int16_t>(respawn_checkpoint);
	soa->checkpoint_fraction[soa_index] = respawn_fraction;
	soa->lap_progress[soa_index] = (static_cast<float>(respawn_checkpoint) + respawn_fraction) / static_cast<float>(soa->current_track[soa_index]->num_checkpoints);
	soa->checkpoint_track_distance[soa_index] = respawn_distance;
	soa->previous_lap_distance[soa_index] = soa->current_track[soa_index]->compute_lap_distance(respawn_checkpoint, respawn_fraction, soa->lap[soa_index]);
	STORE_VEC3(position_current, spawn_transform.origin);
	STORE_VEC3(position_old, spawn_transform.origin);
	STORE_VEC3(position_old_2, spawn_transform.origin);
	STORE_VEC3(position_old_dupe, spawn_transform.origin);
	STORE_VEC3(position_bottom, spawn_transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));

	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = spawn_transform.basis; STORE_TRANSFORM(basis_physical, mxt_tmp); }
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical_other); mxt_tmp.basis = spawn_transform.basis; STORE_TRANSFORM(basis_physical_other, mxt_tmp); }
	update_pitch_transform_from_machine_front_back();

	STORE_TRANSFORM(transform_visual, spawn_transform);
	const SimVec3 spawn_up = spawn_transform.basis.get_column(1);
	STORE_VEC3(track_surface_normal, spawn_up);
	STORE_VEC3(track_surface_pos, spawn_transform.origin - spawn_up * 0.1f);
	soa->height_above_track[soa_index] = 19.9f;

	STORE_VEC3(velocity, SimVec3());
	STORE_VEC3(knockback_velocity, SimVec3());
	STORE_VEC3(velocity_local, SimVec3());
	STORE_VEC3(velocity_local_flattened_and_rotated, SimVec3());
	STORE_VEC3(velocity_angular, SimVec3());
	STORE_VEC3(visual_rotation, SimVec3());
	STORE_VEC3(unk_vec3_0x4e4, SimVec3());
	STORE_VEC3(unk_vec3_0x4f0, SimVec3());
	soa->base_speed[soa_index] = 0.0f;
	soa->boost_turbo[soa_index] = 0.0f;
	soa->some_breakdown_int[soa_index] = 0;
	soa->breakdown_frame_counter[soa_index] = 0;
	soa->machine_crashed[soa_index] = false;
	soa->state_2[soa_index] &= ~(0x2u | 0x20u | 0x80u | 0x100u);
	soa->air_time[soa_index] = 0;
	soa->grip_frames_from_accel_press[soa_index] = 0;
	soa->boost_frames[soa_index] = 0;
	soa->boost_frames_manual[soa_index] = 0;

	soa->machine_state[soa_index] &= ~(MACHINESTATE::ZEROHP |
		MACHINESTATE::AIRBORNE |
		MACHINESTATE::AIRBORNEMORE0_2S_Q |
		MACHINESTATE::FALLOUT |
		MACHINESTATE::TOOKDAMAGE |
		MACHINESTATE::LOWGRIP |
		MACHINESTATE::SIDEATTACKING |
		MACHINESTATE::SPINATTACKING |
		MACHINESTATE::JUSTHITVEHICLE_Q);
	soa->frames_since_death[soa_index] = 0;
	const int point_base = soa_index * 4;
	const SimTransform restore_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 restore_position = LOAD_VEC3(position_current);
	const SimVec3x4 tilt_pos = mxt_transform_points4(
		restore_transform,
		restore_position,
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	const SimVec3x4 wall_pos = mxt_transform_points4(
		restore_transform,
		restore_position,
		sim_load4(soa->wall_offset_x + point_base),
		sim_load4(soa->wall_offset_y + point_base),
		sim_load4(soa->wall_offset_z + point_base));
	const SimVec3 wall_pos_a = mxt_transform_point(restore_transform, restore_position, SimVec3(0.0f, 0.1f, 0.0f));
	const SimVec3 up = mxt_basis_rotate(restore_transform, SimVec3(0, 1, 0));
	for (int lane = 0; lane < 4; ++lane) {
		const int p = point_base + lane;
		soa->tilt_state[p] = 0;
		soa->tilt_force[p] = 0.0f;
		soa->tilt_force_spatial_len[p] = 0.0f;
		STORE_TILT_VEC3(force_spatial, p, SimVec3());
		STORE_TILT_VEC3(up_vector_2, p, up);
		STORE_TILT_VEC3(up_vector, p, up);
		STORE_WALL_VEC3(pos_a, p, wall_pos_a);
		STORE_WALL_VEC3(collision, p, SimVec3());
	}
	sim_store4(soa->tilt_pos_old_x + point_base, tilt_pos.x);
	sim_store4(soa->tilt_pos_old_y + point_base, tilt_pos.y);
	sim_store4(soa->tilt_pos_old_z + point_base, tilt_pos.z);
	sim_store4(soa->tilt_pos_x + point_base, tilt_pos.x);
	sim_store4(soa->tilt_pos_y + point_base, tilt_pos.y);
	sim_store4(soa->tilt_pos_z + point_base, tilt_pos.z);
	sim_store4(soa->wall_pos_b_x + point_base, wall_pos.x);
	sim_store4(soa->wall_pos_b_y + point_base, wall_pos.y);
	sim_store4(soa->wall_pos_b_z + point_base, wall_pos.z);
}

SimTransform PhysicsCar::calculate_respawn_transform(uint16_t cp_idx) const
{
	SimTransform spawn_transform;
	float dummy_distance = soa->last_ground_distance[soa_index];
	if (!compute_respawn_target(cp_idx, spawn_transform, dummy_distance, nullptr, nullptr))
		return SimTransform();
	return spawn_transform;
}

void PhysicsCar::start_restore_to_last_ground()
{
	if (!soa->current_track[soa_index]) {
		return;
	}
	soa->restore_state[soa_index] = 2;
	soa->restore_wait_frames[soa_index] = 0;
	soa->restore_move_frames[soa_index] = 0;
	soa->machine_state[soa_index] |= MACHINESTATE::FALLOUT | MACHINESTATE::AIRBORNE;
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(restore_start_transform); mxt_tmp.origin = LOAD_VEC3(position_current); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(restore_start_transform, mxt_tmp); }
	STORE_TRANSFORM(restore_target_transform, calculate_respawn_transform(soa->last_ground_checkpoint[soa_index]));
	clear_motion_for_restore(soa, soa_index);
}

void PhysicsCar::trigger_mesh_fallout()
{
	soa->machine_state[soa_index] |= MACHINESTATE::FALLOUT |
		MACHINESTATE::AIRBORNE |
		MACHINESTATE::DIEDTHISFRAMEOOB_Q;
	soa->terrain_state[soa_index] |= TERRAIN::FALL;
	start_restore_to_last_ground();
}

void PhysicsCar::trigger_mesh_kill_collision()
{
	soa->terrain_state[soa_index] |= TERRAIN::KILL;
	soa->machine_state[soa_index] |= MACHINESTATE::TOOKDAMAGE;
	if ((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) {
		soa->energy[soa_index] = 0.0f;
		return;
	}

	soa->damage_from_last_hit[soa_index] = 1.01f * soa->calced_max_energy[soa_index];
	soa->energy[soa_index] = 0.0f;
	soa->base_speed[soa_index] = 0.0f;
	if ((soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0) {
		soa->breakdown_frame_counter[soa_index] = 60;
	}
	soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
}

void PhysicsCar::update_restore(float accel_input)
{
	if (!soa->current_track[soa_index])
		return;

	bool crashed = soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y || soa->energy[soa_index] <= 0.0f;

	if (soa->restore_state[soa_index] == 0 && crashed) {
		soa->restore_state[soa_index] = 1;
		soa->restore_wait_frames[soa_index] = 0;
		if (soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y)
			soa->machine_state[soa_index] |= MACHINESTATE::FALLOUT;
		if (soa->energy[soa_index] <= 0.0f)
			soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
	}

	if (soa->restore_state[soa_index] == 1) {
		soa->restore_wait_frames[soa_index]++;
		if ((soa->restore_wait_frames[soa_index] >= 60 && accel_input > 0.1f) || (soa->machine_state[soa_index] & MACHINESTATE::FALLOUT) != 0) {
			soa->restore_state[soa_index] = 2;
			soa->restore_move_frames[soa_index] = 0;
			{ SimTransform mxt_tmp = LOAD_TRANSFORM(restore_start_transform); mxt_tmp.origin = LOAD_VEC3(position_current); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(restore_start_transform, mxt_tmp); }
			STORE_TRANSFORM(restore_target_transform, calculate_respawn_transform(soa->last_ground_checkpoint[soa_index]));
			clear_motion_for_restore(soa, soa_index);
		}
	} else if (soa->restore_state[soa_index] == 2) {
		soa->restore_move_frames[soa_index]++;
		const uint32_t restore_total_frames = soa->s_boost_active[soa_index] ? 18u : 180u;
		const uint32_t restore_countdown_frames = soa->s_boost_active[soa_index] ? 16u : 160u;
		float t = std::min(1.0f, static_cast<float>(soa->restore_move_frames[soa_index]) / static_cast<float>(restore_total_frames));
		t = (t < 0.5f) ? (2.0f * t * t) : (-1.0f + (4.0f - 2.0f * t) * t);
		soa->state_2[soa_index] &= ~0x20;
		if (soa->restore_move_frames[soa_index] >= restore_countdown_frames) {
			soa->machine_state[soa_index] |= MACHINESTATE::STARTINGCOUNTDOWN;
		}
		SimVec3 pos = LOAD_TRANSFORM(restore_start_transform).origin.lerp(LOAD_TRANSFORM(restore_target_transform).origin, t);
		SimQuat qs = LOAD_TRANSFORM(restore_start_transform).basis.get_rotation_quaternion();
		SimQuat qe = LOAD_TRANSFORM(restore_target_transform).basis.get_rotation_quaternion();
		SimQuat qi = qs.slerp(qe, t);

		{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = SimBasis(qi); STORE_TRANSFORM(basis_physical, mxt_tmp); }
		{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical_other); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(basis_physical_other, mxt_tmp); }
		{ SimTransform mxt_tmp = LOAD_TRANSFORM(transform_visual); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(transform_visual, mxt_tmp); }

		STORE_VEC3(position_current, pos);
		STORE_VEC3(position_old, pos);
		STORE_VEC3(position_old_2, pos);
		STORE_VEC3(position_old_dupe, pos);
		STORE_VEC3(position_bottom, mxt_transform_point(LOAD_TRANSFORM(basis_physical), pos, SimVec3(0.0f, -0.1f, 0.0f)));

		if (soa->restore_move_frames[soa_index] >= restore_total_frames) {
			soa->state_2[soa_index] &= ~0x20;
			respawn_at_checkpoint(soa->last_ground_checkpoint[soa_index]);
			soa->energy[soa_index] = std::max(soa->energy[soa_index], soa->calced_max_energy[soa_index] * 0.5f);
			soa->machine_state[soa_index] |= MACHINESTATE::ACTIVE;
			soa->machine_state[soa_index] &= ~MACHINESTATE::STARTINGCOUNTDOWN;
			soa->frames_since_start_2[soa_index] = 60;
			soa->restore_state[soa_index] = 0;
			soa->restore_wait_frames[soa_index] = 0;
			soa->restore_move_frames[soa_index] = 0;
		}
	}
}

void PhysicsCar::check_respawn()
{
	if (!soa->current_track[soa_index])
		return;

	if (soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y || soa->energy[soa_index] <= 0.0f) {
		respawn_at_checkpoint(soa->last_ground_checkpoint[soa_index]);
		if (soa->energy[soa_index] < soa->calced_max_energy[soa_index] * 0.5f)
			soa->energy[soa_index] = soa->calced_max_energy[soa_index] * 0.5f;
	}
}

void PhysicsCar::post_tick()
{
	TrackQueryScratch scratch;
	if (soa->state_2[soa_index] & 0x8u) {
		handle_machine_collision_response();
	}
	handle_machine_damage_and_visuals();
	if (soa->frames_since_start_2[soa_index] == 0)
	{
		STORE_VEC3(velocity, SimVec3());
		STORE_VEC3(knockback_velocity, SimVec3());
		STORE_VEC3(position_current, LOAD_VEC3(initial_pos));
	}

	handle_checkpoints(scratch);
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0 && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0) {
		soa->last_ground_distance[soa_index] = soa->checkpoint_track_distance[soa_index];
		soa->last_ground_checkpoint[soa_index] = soa->current_checkpoint[soa_index];
	}
};

bool PhysicsCar::can_collect_super_spark() const
{
	return !soa->s_boost_active[soa_index] && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0;
}

void PhysicsCar::add_super_spark_charge(uint16_t amount)
{
	if (soa->s_boost_active[soa_index] || amount == 0)
		return;

	uint16_t new_charge = soa->s_boost_charge[soa_index] + amount;
	if (new_charge > soa->s_boost_charge_max[soa_index])
		new_charge = soa->s_boost_charge_max[soa_index];
	soa->s_boost_charge[soa_index] = new_charge;
}

bool PhysicsCar::can_start_s_boost() const
{
	return !soa->s_boost_active[soa_index] && soa->s_boost_charge[soa_index] >= soa->s_boost_charge_max[soa_index];
}

void PhysicsCar::start_s_boost(uint16_t duration_frames)
{
	if (duration_frames == 0)
		duration_frames = 1;

	soa->s_boost_active[soa_index] = true;
	soa->s_boost_frames_remaining[soa_index] = duration_frames;
	soa->s_boost_charge[soa_index] = 0;
	soa->s_boost_emit_frame_accumulator[soa_index] = 0;
	soa->s_boost_pending_spark_spawns[soa_index] = 0;
	soa->boost_frames[soa_index] = 0;
	soa->boost_frames_manual[soa_index] = 0;
	soa->boost_turbo[soa_index] = 0.0f;
	soa->dashplate_heat_multiplier[soa_index] = 1.0f;
	soa->boost_delay_frame_counter[soa_index] = 0;
	soa->car_hit_invincibility[soa_index] = 0;
	soa->machine_state[soa_index] &= ~(MACHINESTATE::JUST_PRESSED_BOOST |
		MACHINESTATE::BOOSTING |
		MACHINESTATE::BOOSTING_DASHPLATE |
		MACHINESTATE::SIDEATTACKING |
		MACHINESTATE::SPINATTACKING |
		MACHINESTATE::TOOKDAMAGE |
		MACHINESTATE::LOWGRIP);
}

void PhysicsCar::stop_s_boost()
{
	soa->s_boost_active[soa_index] = false;
	soa->s_boost_frames_remaining[soa_index] = 0;
	soa->s_boost_emit_frame_accumulator[soa_index] = 0;
	soa->s_boost_pending_spark_spawns[soa_index] = 0;
}

void PhysicsCar::update_s_boost_state()
{
	if (!soa->s_boost_active[soa_index]) {
		soa->s_boost_frames_remaining[soa_index] = 0;
		soa->s_boost_emit_frame_accumulator[soa_index] = 0;
		soa->s_boost_pending_spark_spawns[soa_index] = 0;
		return;
	}

	if (soa->s_boost_frames_remaining[soa_index] > 0)
		soa->s_boost_frames_remaining[soa_index] -= 1;

	soa->machine_state[soa_index] &= ~(MACHINESTATE::TOOKDAMAGE | MACHINESTATE::LOWGRIP);

	soa->s_boost_emit_frame_accumulator[soa_index] += 1;
	while (soa->s_boost_emit_frame_accumulator[soa_index] >= 30) {
		soa->s_boost_emit_frame_accumulator[soa_index] -= 30;
		if (soa->s_boost_pending_spark_spawns[soa_index] < 255)
			soa->s_boost_pending_spark_spawns[soa_index] += 1;
	}

	if (soa->s_boost_frames_remaining[soa_index] == 0) {
		stop_s_boost();
	}
}

uint8_t PhysicsCar::consume_pending_s_boost_sparks()
{
	uint8_t pending = soa->s_boost_pending_spark_spawns[soa_index];
	soa->s_boost_pending_spark_spawns[soa_index] = 0;
	return pending;
}

void PhysicsCar::queue_super_sparks(int count)
{
	if (count <= 0)
		return;
	const int pending = static_cast<int>(soa->pending_super_sparks[soa_index]) + count;
	soa->pending_super_sparks[soa_index] = static_cast<uint8_t>(pending > 255 ? 255 : pending);
}

bool PhysicsCar::apply_damage(float impactStrength)
{
    // Already invulnerable or in breakdown? No damage is processed.
	if (soa->s_boost_active[soa_index])
		return false;

	if (soa->breakdown_frame_counter[soa_index] != 0)
		return false;

	float rawDamage = impactStrength * soa->stat_body[soa_index];

    // Never exceed 101 % of maxEnergy
	const float maxAllowedDamage = 1.01f * soa->calced_max_energy[soa_index];
	rawDamage = std::min(rawDamage, maxAllowedDamage);

	soa->damage_from_last_hit[soa_index] = rawDamage;
	soa->energy[soa_index] -= rawDamage;

	if (soa->energy[soa_index] >= 0.0f)
        return false;  // Machine survives the hit

    // Energy fell below zero → breakdown/KO handling
    soa->energy[soa_index]      = 0.0f;
    soa->base_speed[soa_index]   = 0.0f;
    soa->machine_state[soa_index]      |= MACHINESTATE::ZEROHP;

    // Start countdown only if race not finished and KO flag wasn’t already set
    const bool canStartBreakdown =
    (soa->machine_state[soa_index] & (MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::ZEROHP)) == 0;

    if (canStartBreakdown)
    	soa->breakdown_frame_counter[soa_index] = 60;

    return canStartBreakdown;
}

float PhysicsCar::prepare_impact_direction_info(ImpactData &impact, const SimVec3 &impactDirWorld)
{
    // 1)  Transform impact direction into the machine’s local space


	impact.relative_dir_local = mxt_inverse_transform_point(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(position_current), impactDirWorld);

    /* Subtract the (locally expressed) track-surface normal so that the
       direction truly represents the *relative* approach vector. */
	SimVec3 localTrackNormal = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(track_surface_normal));


	impact.relative_dir_local.x -= localTrackNormal.x;
	impact.relative_dir_local.y -= localTrackNormal.y;
	impact.relative_dir_local.z -= localTrackNormal.z;

    /* ---------------------------------------------------------------------
     * 2)  Normalise or default to forward if the vector is degenerate
     * ------------------------------------------------------------------ */
	float len = sqrtf( impact.relative_dir_local.x * impact.relative_dir_local.x +
		impact.relative_dir_local.y * impact.relative_dir_local.y +
		impact.relative_dir_local.z * impact.relative_dir_local.z );

	float kEpsilon = 0.0000001;

	if (len <= kEpsilon) {
		impact.relative_dir_local.x = 0.0f;
		impact.relative_dir_local.y = 0.0f;
        impact.relative_dir_local.z = -1.0f;     // fall-back: forwards
    } else {
        impact.relative_dir_local.normalize();   // keeps len == 1
    }

    /* ---------------------------------------------------------------------
     * 3)  Pick the canonical collision axis (X, Y, or Z)
     *
     *     A small “dead zone” (5 % of |Y|) biases hits away from the Y-axis
     *     unless it really is dominant.
     * ------------------------------------------------------------------ */
    const float absX = fabsf(impact.relative_dir_local.x);
    const float absY = fabsf(impact.relative_dir_local.y);
    const float absZ = fabsf(impact.relative_dir_local.z);
    const float yThreshold = 0.05f * absY;

    impact.impact_axis_z = 0.0f;  // cleared unless a Z hit is selected
    float dominant = 0.0f;         // magnitude of dominant component (return value)

    if (absX <= yThreshold) {
        /* X is negligible compared with 5 % of Y */
        if (yThreshold <= absZ) {          /* Z dominates */
    	impact.relative_dir_world = { 0.0f, 0.0f, impact.relative_dir_local.z };
    	impact.impact_axis_z      =  impact.relative_dir_local.z;
    	dominant                   =  absZ;
        } else {                           /* Y dominates */
    	impact.relative_dir_world = { 0.0f, impact.relative_dir_local.y, 0.0f };
            dominant                   =  yThreshold;   // matches original behaviour
        }
    } else if (absX <= absZ) {             /* Z dominates (X was bigger than 5 %Y but <= Z) */
        impact.relative_dir_world = { 0.0f, 0.0f, impact.relative_dir_local.z };
        impact.impact_axis_z      =  impact.relative_dir_local.z;
        dominant                   =  absZ;
    } else {                               /* X dominates */
        impact.relative_dir_world = { impact.relative_dir_local.x, 0.0f, 0.0f };
        dominant                   =  absX;
    }

    /* Ensure the chosen axis vector is unit length before we rotate it out. */
    impact.relative_dir_world.normalize();

    /* ---------------------------------------------------------------------
     * 4)  Compute scalar speed per unit mass, sanitising NaN/Inf values
     * ------------------------------------------------------------------ */
    if (!std::isfinite(soa->velocity_x[soa_index]) ||
    	!std::isfinite(soa->velocity_y[soa_index]) ||
    	!std::isfinite(soa->velocity_z[soa_index]))
    {
    	impact.speed_per_mass = 0.0f;
    } else {
    	const float speed = sqrtf(soa->velocity_x[soa_index] * soa->velocity_x[soa_index] +
    		soa->velocity_y[soa_index] * soa->velocity_y[soa_index] +
    		soa->velocity_z[soa_index] * soa->velocity_z[soa_index]);
    	impact.speed_per_mass = speed / soa->stat_weight[soa_index];
    }

    /* ---------------------------------------------------------------------
     * 5)  Rotate the canonical direction back to world space and clean up
     * ------------------------------------------------------------------ */
    impact.relative_dir_world = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), impact.relative_dir_world);

    return fabsf(dominant);
}

float PhysicsCar::scale_collision_impulse_and_damage(bool other_machine_b10_flag)
{
	    // Interpret flags once for readability
	const bool isSpinAttacking = (soa->machine_state[soa_index] & MACHINESTATE::SPINATTACKING) != 0;
	const bool isSideAttacking = (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) != 0;
	const bool isB10           = (soa->machine_state[soa_index] & MACHINESTATE::B10)           != 0;
	const bool otherIsB10      = other_machine_b10_flag != 0;

	float scale = 1.0f;

    /* ---------------------------------------------------------------------
       Case 1: neither spin-attack nor side-attack
       ------------------------------------------------------------------ */
	if (!isSpinAttacking && !isSideAttacking)
	{
		if (isB10)
		{
            scale *= 0.8f;            // Slightly reduced impulse for B10 state
        }
        return scale;                 // Nothing else affects this path
    }

    /* ---------------------------------------------------------------------
       Case 2: currently in a spin- or side-attack
       ------------------------------------------------------------------ */

    // Spin intensity factor ∈ [0.5 , 1.0]; safe for side-attack (unused then)
    const float spinIntensity =
    0.5f + 0.5f * soa->spinattack_decrement[soa_index];

    if (!isB10)   // Machine is *not* in B10 state while attacking
    {
    	if (!otherIsB10)
    	{
            // Attacker !B10 vs victim !B10
    		scale *= isSpinAttacking ? (3.0f * spinIntensity) : 2.0f;
    	}
    	else
    	{
            // Attacker !B10 vs victim  B10
    		scale *= isSpinAttacking ? (5.0f * spinIntensity) : 6.0f;
    	}
    }
    else          // Machine *is* in B10 state while attacking
    {
        // Side-attack <→ Spin-attack multipliers differ
    	scale *= isSpinAttacking ? 3.5f : 4.0f;
    }

    return scale;
}

void PhysicsCar::buildSweepForMachine(float cappedSpeedMps, SimVec3 &sweepStartOut, SimVec3 &cappedVelocityOut)
{
    // Distance travelled during last frame
	SimVec3 delta = LOAD_VEC3(position_old_dupe) - LOAD_VEC3(position_current);

	float travelled = delta.length();

	if (travelled <= 13.88888f)
	{
        sweepStartOut     = LOAD_VEC3(position_old_dupe);    // use previous position as start
        cappedVelocityOut = LOAD_VEC3(velocity);
    }
    else
    {
    	delta = set_vec3_length(-delta, 13.88888f);

    	sweepStartOut = LOAD_VEC3(position_current) + delta;

    	cappedVelocityOut = set_vec3_length(LOAD_VEC3(velocity), cappedSpeedMps);
	}
}

static float closest_points_between_segments(
	const SimVec3& p1, const SimVec3& q1,
	const SimVec3& p2, const SimVec3& q2,
	SimVec3& c1, SimVec3& c2)
{
	constexpr float kEpsilon = 0.000001f;
	const SimVec3 d1 = q1 - p1;
	const SimVec3 d2 = q2 - p2;
	const SimVec3 r = p1 - p2;
	const float a = d1.dot(d1);
	const float e = d2.dot(d2);
	const float f = d2.dot(r);
	float s = 0.0f;
	float t = 0.0f;

	if (a <= kEpsilon && e <= kEpsilon) {
		c1 = p1;
		c2 = p2;
		return c1.distance_squared_to(c2);
	}
	if (a <= kEpsilon) {
		t = std::clamp(f / e, 0.0f, 1.0f);
	} else {
		const float c = d1.dot(r);
		if (e <= kEpsilon) {
			s = std::clamp(-c / a, 0.0f, 1.0f);
		} else {
			const float b = d1.dot(d2);
			const float denom = a * e - b * b;
			if (denom != 0.0f) {
				s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
			}
			t = (b * s + f) / e;
			if (t < 0.0f) {
				t = 0.0f;
				s = std::clamp(-c / a, 0.0f, 1.0f);
			} else if (t > 1.0f) {
				t = 1.0f;
				s = std::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}

	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
	return c1.distance_squared_to(c2);
}

static void move_to_plane_side(PhysicsCar& car, const SimVec3& plane_point, const SimVec3& normal, float desired_signed_distance)
{
	const SimVec3 pos = LOAD_CAR_VEC3(car, position_current);
	const float signed_distance = (pos - plane_point).dot(normal);
	if (desired_signed_distance < 0.0f) {
		if (signed_distance <= desired_signed_distance) {
			return;
		}
	} else if (signed_distance >= desired_signed_distance) {
		return;
	}
	const float correction = desired_signed_distance - signed_distance;
	if (std::abs(correction) > 0.000001f) {
		STORE_CAR_VEC3(car, position_current, pos + normal * correction);
	}
}

static void apply_car_collision_knockback(PhysicsCar& car, const SimVec3& impulse)
{
	STORE_CAR_VEC3(car, collision_response, impulse);
	const float weight = car.soa->stat_weight[car.soa_index];
	STORE_CAR_VEC3(car, velocity, LOAD_CAR_VEC3(car, velocity) + impulse * weight);
}

static bool handle_machine_v_machine_collision_impl(PhysicsCar& self, PhysicsCar &other_machine, bool this_bumper, bool other_bumper)
{
	PhysicsCarSoA* soa = self.soa;
	const int soa_index = self.soa_index;
	if (soa->s_boost_active[soa_index] || other_machine.soa->s_boost_active[other_machine.soa_index]) {
		return false;
	}
	if (((soa->state_2[soa_index] | other_machine.soa->state_2[other_machine.soa_index]) & 0x10u) != 0) {
		return false;
	}

	const float radius1 = this_bumper ? 3.0f : 2.0f;
	const float radius2 = other_bumper ? 3.0f : 2.0f;
	const float combined_radius = radius1 + radius2;
	const SimVec3 p1_old = LOAD_CAR_VEC3(self, position_old_dupe);
	const SimVec3 p1 = LOAD_CAR_VEC3(self, position_collision_snapshot);
	const SimVec3 p2_old = LOAD_CAR_VEC3(other_machine, position_old_dupe);
	const SimVec3 p2 = LOAD_CAR_VEC3(other_machine, position_collision_snapshot);

	SimVec3 closest1;
	SimVec3 closest2;
	const float dist_sq = closest_points_between_segments(p1_old, p1, p2_old, p2, closest1, closest2);
	if (dist_sq >= combined_radius * combined_radius) {
		return false;
	}

	SimVec3 collision_normal = p2_old - p1_old;
	if (collision_normal.length_squared() <= 0.000001f) {
		collision_normal = p2 - p1;
	}
	if (collision_normal.length_squared() <= 0.000001f) {
		collision_normal = (p1 - p1_old) - (p2 - p2_old);
	}
	if (collision_normal.length_squared() <= 0.000001f) {
		collision_normal = LOAD_CAR_TRANSFORM(self, basis_physical).basis.get_column(0);
	}
	collision_normal.normalize();

	const SimVec3 relative_motion = (p1 - p1_old) - (p2 - p2_old);
	const float closing_speed = relative_motion.dot(collision_normal);
	if (closing_speed <= 0.0f) {
		return false;
	}

	const bool this_spin_attacking = (soa->machine_state[soa_index] & MACHINESTATE::SPINATTACKING) != 0;
	const bool this_side_attacking = (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) != 0;
	const bool other_spin_attacking = (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::SPINATTACKING) != 0;
	const bool other_side_attacking = (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::SIDEATTACKING) != 0;
	const bool this_attacking = this_spin_attacking || this_side_attacking;
	const bool other_attacking = other_spin_attacking || other_side_attacking;
	const bool this_alive_before = (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0;
	const bool other_alive_before = (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::ZEROHP) == 0;

	const SimVec3 plane_point = (p1 + p2) * 0.5f;
	constexpr float depenetration_overcorrection = 1.1f;
	move_to_plane_side(self, plane_point, collision_normal, -radius1 * depenetration_overcorrection);
	move_to_plane_side(other_machine, plane_point, collision_normal, radius2 * depenetration_overcorrection);

	SimVec3 impulse = collision_normal * (-0.8f * closing_speed);
	float impulse_strength = impulse.length();
	const float attack_impulse_strength = impulse_strength + ((this_attacking || other_attacking) ? 1.0f : 0.0f);

	float damage1 = impulse_strength;
	float damage2 = impulse_strength;
	SimVec3 impulse1 = impulse;
	SimVec3 impulse2 = -impulse;
	bool this_bumper_slide = false;
	bool other_bumper_slide = false;
	if (this_attacking || other_attacking) {
		damage1 = 0.0f;
		damage2 = 0.0f;
		if (other_attacking && !this_spin_attacking) {
			damage1 = attack_impulse_strength * (other_side_attacking ? 20.0f : 10.0f);
			if (this_side_attacking) {
				damage1 *= 2.0f;
			}
		}
		if (this_attacking && !other_spin_attacking) {
			damage2 = attack_impulse_strength * (this_side_attacking ? 20.0f : 10.0f);
			if (other_side_attacking) {
				damage2 *= 2.0f;
			}
		}
	}
	if (this_attacking && !other_attacking) {
		impulse1 = impulse * 2.0f;
		impulse2 = collision_normal * (1.5f * attack_impulse_strength);
	} else if (!this_attacking && other_attacking) {
		impulse1 = collision_normal * (-1.5f * attack_impulse_strength);
		impulse2 = -impulse * 2.0f;
	} else if (this_attacking && other_attacking) {
		impulse1 = impulse * 0.2f;
		impulse2 = impulse * -0.2f;
		if (!this_spin_attacking) {
			impulse1 = collision_normal * (-1.5f * attack_impulse_strength * (this_side_attacking ? 2.0f : 1.0f));
		}
		if (!other_spin_attacking) {
			impulse2 = collision_normal * (1.5f * attack_impulse_strength * (other_side_attacking ? 2.0f : 1.0f));
		}
	}
	if (this_bumper != other_bumper) {
		impulse1 = impulse1 * 1.5f;
		impulse2 = impulse2 * 1.5f;
		if (!this_bumper && !this_attacking) {
			impulse1 += collision_normal * -2.0f;
			damage1 += 12.0f;
			self.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			soa->rail_collision_timer[soa_index] = 24;
			this_bumper_slide = true;
		}
		if (!other_bumper && !other_attacking) {
			impulse2 += collision_normal * 2.0f;
			damage2 += 12.0f;
			other_machine.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			other_machine.soa->rail_collision_timer[other_machine.soa_index] = 24;
			other_bumper_slide = true;
		}
	}

	apply_car_collision_knockback(self, impulse1);
	apply_car_collision_knockback(other_machine, impulse2);
	soa->visual_rotation_z[soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(self, basis_physical), impulse1).x;
	soa->visual_rotation_x[soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(self, basis_physical), impulse1).z;
	other_machine.soa->visual_rotation_z[other_machine.soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(other_machine, basis_physical), impulse2).x;
	other_machine.soa->visual_rotation_x[other_machine.soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(other_machine, basis_physical), impulse2).z;
	if (impulse_strength > 0.5f) {
		if (!this_bumper_slide) {
			self.remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			soa->drift_ramp[soa_index] = 0.0f;
		}
		if (!other_bumper_slide) {
			other_machine.remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			other_machine.soa->drift_ramp[other_machine.soa_index] = 0.0f;
		}
	}
	if (other_attacking && damage1 > 0.0f) {
		self.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
		soa->rail_collision_timer[soa_index] = 20;
	}
	if (this_attacking && damage2 > 0.0f) {
		other_machine.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
		other_machine.soa->rail_collision_timer[other_machine.soa_index] = 20;
	}
	if (damage1 > 0.0f) {
		self.apply_damage(damage1);
	}
	if (damage2 > 0.0f) {
		other_machine.apply_damage(damage2);
	}
	if (this_alive_before && damage1 > 0.0f && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) {
		soa->pending_ko_attacker_car_index[soa_index] = other_machine.soa->global_start + other_machine.soa_index;
	}
	if (other_alive_before && damage2 > 0.0f && (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::ZEROHP) != 0) {
		other_machine.soa->pending_ko_attacker_car_index[other_machine.soa_index] = soa->global_start + soa_index;
	}
	if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
		soa->energy[soa_index] = 0.0f;
	}
	if (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::ZEROHP) {
		other_machine.soa->energy[other_machine.soa_index] = 0.0f;
	}

	soa->machine_state[soa_index] |= (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::ACTIVE);
	other_machine.soa->machine_state[other_machine.soa_index] |= (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::ACTIVE);

	if (soa->frames_since_start_2[soa_index] == 0) {
		self.apply_initial_accel_activation(0.0f);
	}
	if (other_machine.soa->frames_since_start_2[other_machine.soa_index] == 0) {
		other_machine.apply_initial_accel_activation(0.0f);
	}

    return true;     // collision handled
}

bool PhysicsCar::handle_machine_v_machine_collision(PhysicsCar &other_machine)
{
	return handle_machine_v_machine_collision_impl(*this, other_machine, false, false);
}

bool PhysicsCar::handle_machine_v_bumper_collision(PhysicsCar &bumper_machine)
{
	return handle_machine_v_machine_collision_impl(*this, bumper_machine, false, true);
}

void PhysicsCar::test_collision_with_other_car(PhysicsCar &other_car)
{
	constexpr float fixed_radius = 2.0f;
	constexpr float r_sum_sq = (fixed_radius * 2.0f) * (fixed_radius * 2.0f);
	if (LOAD_VEC3(position_current).distance_squared_to(LOAD_CAR_VEC3(other_car, position_current)) > r_sum_sq)
		return;
	SimVec3 relative_velocity = LOAD_VEC3(velocity) - LOAD_CAR_VEC3(other_car, velocity);
	SimVec3 dir = LOAD_CAR_VEC3(other_car, position_current) - LOAD_VEC3(position_current);
	//if (dir.dot(relative_velocity) <= 0.0f)
	//{
	//	return;
	//}
	SimVec3 normal = dir.normalized();
	float proj = (LOAD_CAR_VEC3(other_car, position_current) - LOAD_VEC3(position_current)).dot(normal);
	float overlap = fixed_radius - proj;
	if (overlap <= 0.0f)	return;			// already separated
	// move each centre to opposite sides of the midpoint plane
	float shift = 0.5f * overlap;			// half for each car
	SUB_VEC3(position_current, normal * shift);
	STORE_CAR_VEC3(other_car, position_current, LOAD_CAR_VEC3(other_car, position_current) + (normal * shift));
	float impulse_strength = relative_velocity.dot(normal);
	SimVec3 impulse = normal * impulse_strength;
	SUB_VEC3(velocity, impulse * other_car.soa->stat_weight[other_car.soa_index] * 0.001);
	STORE_CAR_VEC3(other_car, velocity, LOAD_CAR_VEC3(other_car, velocity) + (impulse * soa->stat_weight[soa_index] * 0.001));
};

void PhysicsCar::motion_tick(PlayerInput input)
{
	if (soa->restore_state[soa_index] != 2)
		simulate_machine_motion(input);
}
