#pragma once

#include "main.h"
#include "car/physics_car.h"
#include "track/racetrack.h"

#include <algorithm>
#include <cstdint>

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
