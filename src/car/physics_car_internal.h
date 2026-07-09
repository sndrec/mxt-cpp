#pragma once

#include "physics_car.h"
#include "main.h"
#include "godot_cpp/variant/plane.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include "mxt_core/debug.hpp"
#include "mxt_core/math_utils.h"
#include "mxt_core/player_input.h"
#include "track/track_segment.h"
#include "track/trigger_collider.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
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

static inline float air_steering_drag_tilt_multiplier(float air_tilt)
{
	if (air_tilt > 0.0f) {
		const float down_factor = std::clamp(1.0f - air_tilt / 60.0f, 0.0f, 1.0f);
		return down_factor * down_factor;
	}
	return 1.0f + 2.0f * std::clamp(-air_tilt / 50.0f, 0.0f, 1.0f);
}

static inline uint64_t physics_profile_now_us()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

static inline void physics_profile_mark(uint64_t* bucket, uint64_t& step)
{
	if (!bucket) {
		return;
	}
	const uint64_t now = physics_profile_now_us();
	*bucket += now - step;
	step = now;
}

static inline float airborne_pitch_drag_factor(const SimTransform& basis, const SimVec3& track_normal)
{
	const SimVec3 machine_up_vector_ws = basis.basis.get_column(2);
	const float dot_prod_up_with_track_normal = machine_up_vector_ws.dot(track_normal);
	float alignment_factor = 3.4f * (0.3f + dot_prod_up_with_track_normal);
	alignment_factor = std::clamp(alignment_factor, 0.0f, 1.0f);
	return 1.0f - alignment_factor * alignment_factor;
}

static inline void mark_floor_disconnected(PhysicsCarSoA *soa, int soa_index)
{
	soa->machine_state[soa_index] |= MACHINESTATE::HAS_DISCONNECTED;
}

static inline void clear_floor_disconnected(PhysicsCarSoA *soa, int soa_index)
{
	soa->machine_state[soa_index] &= ~MACHINESTATE::HAS_DISCONNECTED;
}

static inline bool trace_mesh_floor_for_car(const PhysicsCarSoA *soa, int soa_index)
{
	return soa &&
		DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_MESH_FLOOR) &&
		(soa->global_start + soa_index) == 0;
}

static inline bool trace_rail_for_car(const PhysicsCarSoA *soa, int soa_index)
{
	return soa &&
		DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_RAIL_SAMPLING) &&
		DEBUG::rail_trace_filter_matches(soa->global_start + soa_index, soa->simulation_tick[soa_index]);
}

static constexpr float rail_depenetration_epsilon = 0.01f;

static inline SimTransform mesh_hit_plane_transform(const SimVec3 &point, const SimVec3 &normal)
{
	SimBasis basis;
	basis[0] = SimVec3(1.0f, 0.0f, 0.0f);
	basis[1] = normal;
	basis[2] = SimVec3();
	return SimTransform(basis, point);
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
constexpr float kAnalyticRoadBelowCenterlineFalloutWorldY = 500.0f;
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
	const float cp_t = checkpoint_plane_fraction_unclamped(cp, point);
	return remap_float(cp_t, 0.0f, 1.0f, cp.t_start, cp.t_end);
}

static inline bool road_shape_uses_below_centerline_fallout(const RoadShape *shape)
{
	if (!shape) {
		return false;
	}
	return shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_FLAT ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
}

static bool analytic_road_world_below_centerline_fallout(
	const RaceTrack &track,
	int cp_idx,
	const TrackSegment &segment,
	const SimVec3 &point)
{
	if (cp_idx < 0 || cp_idx >= track.num_checkpoints) {
		return false;
	}

	const float road_t = checkpoint_longitudinal_t(track, cp_idx, point);
	if (!track_segment_longitudinal_t_in_domain(road_t)) {
		return false;
	}

	RoadTransform centerline_root;
	segment.curve_matrix->sample(centerline_root, road_t);
	return point.y <= centerline_root.t3d.origin.y - kAnalyticRoadBelowCenterlineFalloutWorldY;
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
