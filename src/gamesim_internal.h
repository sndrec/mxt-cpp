#pragma once

#include "main.h"
#include "car/physics_car.h"
#include "mxt_core/enums.h"
#include "track/racetrack.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

static inline godot::Vector3 gd_vec3(const SimVec3& value)
{
	return godot::Vector3(value.x, value.y, value.z);
}

static inline godot::Basis gd_basis(const SimBasis& value)
{
	godot::Basis out;
	out.set_column(0, gd_vec3(value.c0));
	out.set_column(1, gd_vec3(value.c1));
	out.set_column(2, gd_vec3(value.c2));
	return out;
}

static inline godot::Transform3D gd_transform(const SimTransform& value)
{
	return godot::Transform3D(gd_basis(value.basis), gd_vec3(value.origin));
}

static inline SimVec3 sim_vec3(const godot::Vector3& value)
{
	return SimVec3(value.x, value.y, value.z);
}

static inline SimBasis sim_basis(const godot::Basis& value)
{
	const godot::Vector3 c0 = value.get_column(0);
	const godot::Vector3 c1 = value.get_column(1);
	const godot::Vector3 c2 = value.get_column(2);
	return SimBasis(c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z);
}

static inline SimTransform sim_transform(const godot::Transform3D& value)
{
	return SimTransform(sim_basis(value.basis), sim_vec3(value.origin));
}

static inline SimTransform interpolate_sim_transform(const SimTransform& a, const SimTransform& b, float alpha)
{
	alpha = std::clamp(alpha, 0.0f, 1.0f);
	SimTransform out;
	out.origin = a.origin.lerp(b.origin, alpha);
	const SimQuat qa = a.basis.get_rotation_quaternion();
	const SimQuat qb = b.basis.get_rotation_quaternion();
	out.basis = SimBasis(qa.slerp(qb, alpha));
	return out;
}

static inline float gamesim_vehicle_stored_distance(const PhysicsCarSoA& soa, int lane, float lap_length)
{
	return soa.checkpoint_track_distance[lane] + lap_length * static_cast<float>(soa.lap[lane]);
}

static inline bool vehicle_restore_off_eliminated(const PhysicsCarSoA& soa, int lane)
{
	const uint32_t state = soa.machine_state[lane];
	if ((state & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u) {
		return false;
	}
	if ((state & MACHINESTATE::FALLOUT) != 0u) {
		return true;
	}
	if (soa.current_track[lane] && soa.position_current_y[lane] < soa.current_track[lane]->minimum_y) {
		return true;
	}
	if ((state & MACHINESTATE::ZEROHP) == 0u) {
		return false;
	}
	if ((state & MACHINESTATE::RETIRED) != 0u) {
		return true;
	}
	return (soa.state_2[lane] & 0x80u) != 0u && (state & MACHINESTATE::AIRBORNE) == 0u;
}

static inline uint32_t bumper_hash_u32(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

static inline uint32_t bumper_mix_u32(uint32_t state, uint32_t value)
{
	return bumper_hash_u32(state ^ (value + 0x9E3779B9u + (state << 6) + (state >> 2)));
}

static inline uint32_t bumper_float_bits(float value)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static inline uint32_t bumper_track_seed_from_track(const RaceTrack* track)
{
	if (!track) {
		return 0xB62A1C3Du;
	}
	uint32_t seed = 0xB62A1C3Du;
	seed = bumper_mix_u32(seed, static_cast<uint32_t>(track->num_checkpoints));
	seed = bumper_mix_u32(seed, static_cast<uint32_t>(track->num_segments));
	seed = bumper_mix_u32(seed, bumper_float_bits(track->lap_length));
	for (int i = 0; i < track->num_checkpoints; ++i) {
		const CollisionCheckpoint& checkpoint = track->checkpoints[i];
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.position_start.x));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.position_start.y));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.position_start.z));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.position_end.x));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.position_end.y));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.position_end.z));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.distance));
		seed = bumper_mix_u32(seed, bumper_float_bits(checkpoint.local_distance));
	}
	return seed ? seed : 0xB62A1C3Du;
}

static inline float bumper_sequence_trigger_distance(
	uint32_t spawn_seed,
	int leader_lap,
	uint32_t sequence,
	float interval,
	float lap_length)
{
	const uint32_t hash = bumper_hash_u32(
		spawn_seed ^
		(static_cast<uint32_t>(leader_lap) * 0x27D4EB2Du) ^
		(sequence * 0x9E3779B9u) ^
		0xA341316Cu);
	const float jitter =
		(static_cast<float>(hash & 0xffffu) * (1.0f / 65535.0f) - 0.5f) * interval * 0.35f;
	const float first_trigger = leader_lap == 2 ? 680.0f : 360.0f;
	(void)lap_length;
	return first_trigger + static_cast<float>(sequence) * interval + jitter;
}

#define LOAD_INDEXED_VEC3(storage, name, index) SimVec3((storage).name##_x[(index)], (storage).name##_y[(index)], (storage).name##_z[(index)])
#define STORE_INDEXED_VEC3(storage, name, index, value) do { const SimVec3 mxt_v3_tmp = (value); (storage).name##_x[(index)] = mxt_v3_tmp.x; (storage).name##_y[(index)] = mxt_v3_tmp.y; (storage).name##_z[(index)] = mxt_v3_tmp.z; } while (0)

static inline float gamesim_track_lap_length(const RaceTrack* track)
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

static constexpr uint16_t MXT_SUPER_SPARK_ANIMATION_FRAMES = 30;
static constexpr float MXT_SUPER_SPARK_ARC_HEIGHT = 8.4f;

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

static inline SimVec3 mxt_super_spark_position_at_frame(
	const SimVec3& start_position,
	const SimVec3& final_position,
	const SimVec3& plane_normal,
	uint16_t animation_frame,
	uint8_t collectable)
{
	if (collectable) {
		return final_position;
	}
	const float t = std::min(
		static_cast<float>(animation_frame) / static_cast<float>(MXT_SUPER_SPARK_ANIMATION_FRAMES),
		1.0f);
	const float arc = 4.0f * t * (1.0f - t);
	return start_position.lerp(final_position, t) + plane_normal * (MXT_SUPER_SPARK_ARC_HEIGHT * arc);
}
