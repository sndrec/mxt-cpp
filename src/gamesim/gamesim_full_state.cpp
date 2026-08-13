#include "gamesim/gamesim_internal.h"

#include "godot_cpp/variant/utility_functions.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

using namespace godot;

static constexpr uint32_t MXT_NET_STATE_MAGIC = 0x5354584du;
template <typename T>
static void mxt_write_pod_array(uint8_t*& ptr, const T* src, int count)
{
	const size_t bytes = sizeof(T) * static_cast<size_t>(std::max(0, count));
	if (bytes == 0) {
		return;
	}
	if (src) {
		std::memcpy(ptr, src, bytes);
	} else {
		std::memset(ptr, 0, bytes);
	}
	ptr += bytes;
}

template <typename T>
static bool mxt_read_pod_array(const uint8_t*& ptr, const uint8_t* end, T* dst, int count)
{
	const size_t bytes = sizeof(T) * static_cast<size_t>(std::max(0, count));
	if (bytes == 0) {
		return true;
	}
	if (ptr + bytes > end) {
		return false;
	}
	if (dst) {
		std::memcpy(dst, ptr, bytes);
	}
	ptr += bytes;
	return true;
}

static size_t mxt_vehicle_local_state_size(const PhysicsCar* vehicles, int count)
{
	if (!vehicles || count <= 0 || !vehicles[0].soa || !vehicles[0].soa->shards) {
		return 0;
	}
	size_t total = 0;
	const PhysicsCarSoA* shards = vehicles[0].soa->shards;
	const int shard_count = vehicles[0].soa->shard_count;
	for (int shard = 0; shard < shard_count; ++shard) {
		const PhysicsCarSoA& soa = shards[shard];
		const int lanes = std::max(0, soa.lane_count);
		const int points = std::max(0, soa.point_count);
#define MXT_ACCUM_LANE_ARRAY(type, name, default_value) total += sizeof(type) * static_cast<size_t>(lanes);
		PHYSICS_CAR_STATIC_SCALAR_FIELDS(MXT_ACCUM_LANE_ARRAY)
		PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(MXT_ACCUM_LANE_ARRAY)
#undef MXT_ACCUM_LANE_ARRAY
#define MXT_ACCUM_LANE_VEC3(name, default_value) total += sizeof(float) * 3u * static_cast<size_t>(lanes);
		PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(MXT_ACCUM_LANE_VEC3)
#undef MXT_ACCUM_LANE_VEC3
#define MXT_ACCUM_LANE_TRANSFORM(name, default_value) total += sizeof(float) * 12u * static_cast<size_t>(lanes);
		PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(MXT_ACCUM_LANE_TRANSFORM)
#undef MXT_ACCUM_LANE_TRANSFORM
#define MXT_ACCUM_POINT_ARRAY(type, name, default_value) total += sizeof(type) * static_cast<size_t>(points);
		PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(MXT_ACCUM_POINT_ARRAY)
#undef MXT_ACCUM_POINT_ARRAY
#define MXT_ACCUM_POINT_VEC3(name, default_value) total += sizeof(float) * 3u * static_cast<size_t>(points);
		PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(MXT_ACCUM_POINT_VEC3)
		PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(MXT_ACCUM_POINT_VEC3)
		PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(MXT_ACCUM_POINT_VEC3)
#undef MXT_ACCUM_POINT_VEC3
	}
	return total;
}

static void mxt_write_vehicle_local_state(uint8_t*& ptr, const PhysicsCar* vehicles, int count)
{
	if (!vehicles || count <= 0 || !vehicles[0].soa || !vehicles[0].soa->shards) {
		return;
	}
	const PhysicsCarSoA* shards = vehicles[0].soa->shards;
	const int shard_count = vehicles[0].soa->shard_count;
	for (int shard = 0; shard < shard_count; ++shard) {
		const PhysicsCarSoA& soa = shards[shard];
		const int lanes = std::max(0, soa.lane_count);
		const int points = std::max(0, soa.point_count);
#define MXT_WRITE_LANE_ARRAY(type, name, default_value) mxt_write_pod_array(ptr, soa.name, lanes);
		PHYSICS_CAR_STATIC_SCALAR_FIELDS(MXT_WRITE_LANE_ARRAY)
		PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(MXT_WRITE_LANE_ARRAY)
#undef MXT_WRITE_LANE_ARRAY
#define MXT_WRITE_LANE_VEC3(name, default_value) \
		mxt_write_pod_array(ptr, soa.name##_x, lanes); \
		mxt_write_pod_array(ptr, soa.name##_y, lanes); \
		mxt_write_pod_array(ptr, soa.name##_z, lanes);
		PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(MXT_WRITE_LANE_VEC3)
#undef MXT_WRITE_LANE_VEC3
#define MXT_WRITE_LANE_TRANSFORM(name, default_value) \
		mxt_write_pod_array(ptr, soa.name##_c0x, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c0y, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c0z, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c1x, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c1y, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c1z, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c2x, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c2y, lanes); \
		mxt_write_pod_array(ptr, soa.name##_c2z, lanes); \
		mxt_write_pod_array(ptr, soa.name##_ox, lanes); \
		mxt_write_pod_array(ptr, soa.name##_oy, lanes); \
		mxt_write_pod_array(ptr, soa.name##_oz, lanes);
		PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(MXT_WRITE_LANE_TRANSFORM)
#undef MXT_WRITE_LANE_TRANSFORM
#define MXT_WRITE_POINT_ARRAY(type, name, default_value) mxt_write_pod_array(ptr, soa.tilt_##name, points);
		PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(MXT_WRITE_POINT_ARRAY)
#undef MXT_WRITE_POINT_ARRAY
#define MXT_WRITE_TILT_VEC3(name, default_value) \
		mxt_write_pod_array(ptr, soa.tilt_##name##_x, points); \
		mxt_write_pod_array(ptr, soa.tilt_##name##_y, points); \
		mxt_write_pod_array(ptr, soa.tilt_##name##_z, points);
		PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(MXT_WRITE_TILT_VEC3)
		PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(MXT_WRITE_TILT_VEC3)
#undef MXT_WRITE_TILT_VEC3
#define MXT_WRITE_WALL_VEC3(name, default_value) \
		mxt_write_pod_array(ptr, soa.wall_##name##_x, points); \
		mxt_write_pod_array(ptr, soa.wall_##name##_y, points); \
		mxt_write_pod_array(ptr, soa.wall_##name##_z, points);
		PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(MXT_WRITE_WALL_VEC3)
#undef MXT_WRITE_WALL_VEC3
	}
}

static bool mxt_read_vehicle_local_state(const uint8_t*& ptr, const uint8_t* end, PhysicsCar* vehicles, int count)
{
	if (!vehicles || count <= 0 || !vehicles[0].soa || !vehicles[0].soa->shards) {
		return ptr == end;
	}
	PhysicsCarSoA* shards = vehicles[0].soa->shards;
	const int shard_count = vehicles[0].soa->shard_count;
	for (int shard = 0; shard < shard_count; ++shard) {
		PhysicsCarSoA& soa = shards[shard];
		const int lanes = std::max(0, soa.lane_count);
		const int points = std::max(0, soa.point_count);
#define MXT_READ_LANE_ARRAY(type, name, default_value) if (!mxt_read_pod_array(ptr, end, soa.name, lanes)) return false;
		PHYSICS_CAR_STATIC_SCALAR_FIELDS(MXT_READ_LANE_ARRAY)
		PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(MXT_READ_LANE_ARRAY)
#undef MXT_READ_LANE_ARRAY
#define MXT_READ_LANE_VEC3(name, default_value) \
		if (!mxt_read_pod_array(ptr, end, soa.name##_x, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_y, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_z, lanes)) return false;
		PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(MXT_READ_LANE_VEC3)
#undef MXT_READ_LANE_VEC3
#define MXT_READ_LANE_TRANSFORM(name, default_value) \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c0x, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c0y, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c0z, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c1x, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c1y, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c1z, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c2x, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c2y, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_c2z, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_ox, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_oy, lanes)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.name##_oz, lanes)) return false;
		PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(MXT_READ_LANE_TRANSFORM)
#undef MXT_READ_LANE_TRANSFORM
#define MXT_READ_POINT_ARRAY(type, name, default_value) if (!mxt_read_pod_array(ptr, end, soa.tilt_##name, points)) return false;
		PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(MXT_READ_POINT_ARRAY)
#undef MXT_READ_POINT_ARRAY
#define MXT_READ_TILT_VEC3(name, default_value) \
		if (!mxt_read_pod_array(ptr, end, soa.tilt_##name##_x, points)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.tilt_##name##_y, points)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.tilt_##name##_z, points)) return false;
		PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(MXT_READ_TILT_VEC3)
		PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(MXT_READ_TILT_VEC3)
#undef MXT_READ_TILT_VEC3
#define MXT_READ_WALL_VEC3(name, default_value) \
		if (!mxt_read_pod_array(ptr, end, soa.wall_##name##_x, points)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.wall_##name##_y, points)) return false; \
		if (!mxt_read_pod_array(ptr, end, soa.wall_##name##_z, points)) return false;
		PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(MXT_READ_WALL_VEC3)
#undef MXT_READ_WALL_VEC3
	}
	return ptr == end;
}

void GameSim::save_vehicle_local_state_to_saved_state(SavedState& state) const
{
	const size_t car_size = mxt_vehicle_local_state_size(cars, num_cars);
	const size_t bumper_size = mxt_vehicle_local_state_size(bumper_cars, bumper_count);
	state.car_local_state_size = static_cast<uint32_t>(car_size);
	state.bumper_local_state_size = static_cast<uint32_t>(bumper_size);
	state.vehicle_local_state.resize(car_size + bumper_size);
	if (state.vehicle_local_state.empty()) {
		return;
	}
	uint8_t* ptr = state.vehicle_local_state.data();
	mxt_write_vehicle_local_state(ptr, cars, num_cars);
	mxt_write_vehicle_local_state(ptr, bumper_cars, bumper_count);
}

bool GameSim::restore_vehicle_local_state_from_saved_state(const SavedState& state)
{
	const size_t expected_car_size = mxt_vehicle_local_state_size(cars, num_cars);
	const size_t expected_bumper_size = mxt_vehicle_local_state_size(bumper_cars, bumper_count);
	const size_t expected_total = expected_car_size + expected_bumper_size;
	if (expected_total == 0) {
		return true;
	}
	if (state.car_local_state_size != expected_car_size ||
			state.bumper_local_state_size != expected_bumper_size ||
			state.vehicle_local_state.size() != expected_total) {
		return false;
	}
	const uint8_t* ptr = state.vehicle_local_state.data();
	const uint8_t* car_end = ptr + expected_car_size;
	if (!mxt_read_vehicle_local_state(ptr, car_end, cars, num_cars)) {
		return false;
	}
	const uint8_t* bumper_end = car_end + expected_bumper_size;
	if (!mxt_read_vehicle_local_state(car_end, bumper_end, bumper_cars, bumper_count)) {
		return false;
	}
	return true;
}

void GameSim::rebuild_road_samples_after_state_load() {
	const int rebuild_count = num_cars + (bumper_cars ? bumper_count : 0);
	for (int i = 0; i < rebuild_count; ++i) {
		PhysicsCar& car = i < num_cars ? cars[i] : bumper_cars[i - num_cars];
		PhysicsCarSoA& soa = *car.soa;
		const int lane = car.soa_index;
		const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
		const SimVec3 position = LOAD_INDEXED_VEC3(soa, position_current, lane);
		{
			SimTransform visual = basis;
			visual.origin = position;
			MXT_STORE_TRANSFORM(soa, transform_visual, lane, visual);
		}
		const SimVec3 velocity = LOAD_INDEXED_VEC3(soa, velocity, lane);
		if (std::abs(soa.stat_weight[lane]) > 0.0001f) {
			soa.speed_kmh[lane] = 216.0f * (velocity.length() / soa.stat_weight[lane]);
		} else {
			soa.speed_kmh[lane] = 0.0f;
		}
		STORE_INDEXED_VEC3(soa, initial_pos, lane, position);
		STORE_INDEXED_VEC3(soa, position_collision_snapshot, lane, position);
		STORE_INDEXED_VEC3(soa, position_bottom, lane, basis.basis.xform(SimVec3(0.0f, -0.1f, 0.0f)) + position);
		STORE_INDEXED_VEC3(soa, track_surface_normal_prev, lane, LOAD_INDEXED_VEC3(soa, track_surface_normal, lane));
		STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_track, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_rail, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_total, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_response, lane, SimVec3());
		RaceTrack* track = soa.current_track[lane] ? soa.current_track[lane] : current_track;
		RoadData& road = soa.road_sample[lane];
		road = RoadData();
		road.cp_idx = static_cast<int16_t>(soa.current_checkpoint[lane]);
		if (track &&
			soa.current_checkpoint[lane] >= 0 &&
			soa.current_checkpoint[lane] < track->num_checkpoints) {
			track->get_road_surface(soa.current_checkpoint[lane], position, road.road_t, road.spatial_t, road.closest_surface);
			STORE_INDEXED_VEC3(soa, track_surface_pos, lane, road.closest_surface.origin);
			road.closest_surface.basis[1] = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		} else {
			road.closest_surface = basis;
			road.closest_surface.origin = position;
			STORE_INDEXED_VEC3(soa, track_surface_pos, lane, position);
			road.closest_surface.basis[1] = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		}
	}
}

void GameSim::rebuild_static_state_after_network_load() {
	const int rebuild_count = num_cars + (bumper_cars ? bumper_count : 0);
	for (int i = 0; i < rebuild_count; ++i) {
		PhysicsCar& car = i < num_cars ? cars[i] : bumper_cars[i - num_cars];
		PhysicsCarSoA& soa = *car.soa;
		const int lane = car.soa_index;
		car.update_machine_stats();
		if (soa.car_properties[lane]) {
			soa.calced_max_energy[lane] =
				soa.car_properties[lane]->base_stats[CAR_STAT_MAX_ENERGY] + soa.ko_energy_bonus[lane];
		}
		soa.weight_derived_1[lane] = 52.0f * soa.stat_weight[lane] * 0.0625f;
		soa.weight_derived_2[lane] = 45.0f * soa.stat_weight[lane] * 0.0625f;
		soa.weight_derived_3[lane] = 52.0f * soa.stat_weight[lane] * 0.0625f;

		const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
		const SimVec3 position = LOAD_INDEXED_VEC3(soa, position_current, lane);
		{
			SimTransform visual = basis;
			visual.origin = position;
			MXT_STORE_TRANSFORM(soa, transform_visual, lane, visual);
		}
		STORE_INDEXED_VEC3(soa, position_collision_snapshot, lane, position);
		STORE_INDEXED_VEC3(soa, position_bottom, lane, basis.basis.xform(SimVec3(0.0f, -0.1f, 0.0f)) + position);
		STORE_INDEXED_VEC3(soa, track_surface_normal_prev, lane, LOAD_INDEXED_VEC3(soa, track_surface_normal, lane));
		STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_track, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_rail, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_push_total, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, collision_response, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, unk_vec3_0x4e4, lane, SimVec3());
		STORE_INDEXED_VEC3(soa, unk_vec3_0x4f0, lane, SimVec3());
		soa.input_steer_pitch[lane] = 0.0f;
		soa.input_strafe[lane] = 0.0f;
		soa.input_steer_yaw[lane] = 0.0f;
		soa.input_brake[lane] = 0.0f;

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
			STORE_INDEXED_VEC3(soa, track_surface_pos, lane, road.closest_surface.origin);
			road.closest_surface.basis[1] = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		} else {
			road.closest_surface = basis;
			road.closest_surface.origin = position;
			STORE_INDEXED_VEC3(soa, track_surface_pos, lane, position);
			road.closest_surface.basis[1] = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		}
		STORE_INDEXED_VEC3(soa, track_surface_normal_prev, lane, LOAD_INDEXED_VEC3(soa, track_surface_normal, lane));

		const SimTransform previous_basis = MXT_LOAD_TRANSFORM(soa, basis_physical_other, lane);
		const SimVec3 previous_position = LOAD_INDEXED_VEC3(soa, position_old, lane);
		const int point_base = lane * 4;
		const SimFloat4 tilt_x = sim_load4(soa.tilt_offset_x + point_base);
		const SimFloat4 tilt_y =
			sim_load4(soa.tilt_offset_y + point_base) +
			sim_load4(soa.tilt_force + point_base) -
			sim_load4(soa.tilt_rest_length + point_base);
		const SimFloat4 tilt_z = sim_load4(soa.tilt_offset_z + point_base);
		const SimVec3x4 tilt_pos_old = transform_points_components4(
			previous_basis.basis.c0.x, previous_basis.basis.c0.y, previous_basis.basis.c0.z,
			previous_basis.basis.c1.x, previous_basis.basis.c1.y, previous_basis.basis.c1.z,
			previous_basis.basis.c2.x, previous_basis.basis.c2.y, previous_basis.basis.c2.z,
			previous_position.x, previous_position.y, previous_position.z,
			tilt_x, tilt_y, tilt_z);
		const SimVec3x4 tilt_pos = transform_points_components4(
			basis.basis.c0.x, basis.basis.c0.y, basis.basis.c0.z,
			basis.basis.c1.x, basis.basis.c1.y, basis.basis.c1.z,
			basis.basis.c2.x, basis.basis.c2.y, basis.basis.c2.z,
			position.x, position.y, position.z,
			tilt_x, tilt_y, tilt_z);
		sim_store4(soa.tilt_pos_old_x + point_base, tilt_pos_old.x);
		sim_store4(soa.tilt_pos_old_y + point_base, tilt_pos_old.y);
		sim_store4(soa.tilt_pos_old_z + point_base, tilt_pos_old.z);
		sim_store4(soa.tilt_pos_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_z + point_base, tilt_pos.z);

		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
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
		}
	}
}

godot::PackedByteArray GameSim::get_state_data(int target_tick) const {
	int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data)
		return godot::PackedByteArray();
	return serialize_network_state(target_tick);
}

static constexpr uint32_t MXT_FULL_STATE_MAGIC = 0x4653544du;
static constexpr uint32_t MXT_FULL_STATE_VERSION = 5u;

godot::PackedByteArray GameSim::get_full_state_data(int target_tick) {
	if (!gamestate_data.heap_start || gamestate_data.get_size() <= 0) {
		return godot::PackedByteArray();
	}
	SavedState state = {};
	save_bumper_states_to_saved_state(state);
	update_saved_voice_transforms(state);
	save_vehicle_local_state_to_saved_state(state);
	const uint32_t voice_count = static_cast<uint32_t>(std::min(
		std::max(0, state.voice_transform_count),
		static_cast<int>(state.voice_transforms.size())));
	const uint32_t bumper_count_u32 = static_cast<uint32_t>(BUMPER_POOL_SIZE);
	const uint32_t heap_size = static_cast<uint32_t>(gamestate_data.get_size());
	const uint32_t car_local_state_size = state.car_local_state_size;
	const uint32_t bumper_local_state_size = state.bumper_local_state_size;
	const size_t header_size =
		sizeof(uint32_t) * 8u +
		sizeof(uint8_t) +
		sizeof(uint32_t);
	const size_t bumper_size = static_cast<size_t>(BUMPER_POOL_SIZE) *
		(sizeof(uint8_t) * 2u + sizeof(uint32_t) + sizeof(float));
	const size_t voice_size = static_cast<size_t>(voice_count) *
		(sizeof(int32_t) + sizeof(float) * 3u);
	const size_t vehicle_local_state_size =
		static_cast<size_t>(car_local_state_size) + static_cast<size_t>(bumper_local_state_size);
	const size_t total_size = header_size + bumper_size + voice_size + vehicle_local_state_size + heap_size;
	godot::PackedByteArray out;
	out.resize(static_cast<int>(total_size));
	uint8_t* ptr = out.ptrw();
	auto write_bytes = [&ptr](const void* src, size_t size) {
		std::memcpy(ptr, src, size);
		ptr += size;
	};
	const uint32_t magic = MXT_FULL_STATE_MAGIC;
	const uint32_t version = MXT_FULL_STATE_VERSION;
	const int32_t stored_tick = static_cast<int32_t>(target_tick);
	write_bytes(&magic, sizeof(magic));
	write_bytes(&version, sizeof(version));
	write_bytes(&stored_tick, sizeof(stored_tick));
	write_bytes(&heap_size, sizeof(heap_size));
	write_bytes(&bumper_count_u32, sizeof(bumper_count_u32));
	write_bytes(&voice_count, sizeof(voice_count));
	write_bytes(&car_local_state_size, sizeof(car_local_state_size));
	write_bytes(&bumper_local_state_size, sizeof(bumper_local_state_size));
	write_bytes(&state.bumper_scheduler_lap, sizeof(state.bumper_scheduler_lap));
	write_bytes(&state.bumper_next_sequence, sizeof(state.bumper_next_sequence));
	for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
		const BumperState& bumper = state.bumper_states[i];
		write_bytes(&bumper.active, sizeof(bumper.active));
		write_bytes(&bumper.spawn_lap, sizeof(bumper.spawn_lap));
		write_bytes(&bumper.next_sequence, sizeof(bumper.next_sequence));
		write_bytes(&bumper.target_lane, sizeof(bumper.target_lane));
	}
	for (uint32_t i = 0; i < voice_count; ++i) {
		const SavedVoiceTransform& voice = state.voice_transforms[i];
		write_bytes(&voice.player_id, sizeof(voice.player_id));
		const float values[3] = {
			voice.origin.x, voice.origin.y, voice.origin.z,
		};
		write_bytes(values, sizeof(values));
	}
	if (vehicle_local_state_size > 0) {
		write_bytes(state.vehicle_local_state.data(), vehicle_local_state_size);
	}
	write_bytes(gamestate_data.heap_start, heap_size);
	return out;
}

bool GameSim::load_full_state_data(int target_tick, godot::PackedByteArray data) {
	const uint8_t* ptr = data.ptr();
	const uint8_t* end = ptr + data.size();
	auto read_bytes = [&ptr, end](void* dst, size_t size) -> bool {
		if (ptr + size > end) {
			return false;
		}
		std::memcpy(dst, ptr, size);
		ptr += size;
		return true;
	};
	uint32_t magic = 0;
	uint32_t version = 0;
	int32_t stored_tick = -1;
	uint32_t heap_size = 0;
	uint32_t bumper_count_u32 = 0;
	uint32_t voice_count = 0;
	uint32_t car_local_state_size = 0;
	uint32_t bumper_local_state_size = 0;
	if (!read_bytes(&magic, sizeof(magic)) ||
			!read_bytes(&version, sizeof(version)) ||
			!read_bytes(&stored_tick, sizeof(stored_tick)) ||
			!read_bytes(&heap_size, sizeof(heap_size)) ||
			!read_bytes(&bumper_count_u32, sizeof(bumper_count_u32)) ||
			!read_bytes(&voice_count, sizeof(voice_count)) ||
			!read_bytes(&car_local_state_size, sizeof(car_local_state_size)) ||
			!read_bytes(&bumper_local_state_size, sizeof(bumper_local_state_size)) ||
			magic != MXT_FULL_STATE_MAGIC ||
			version != MXT_FULL_STATE_VERSION ||
			stored_tick != target_tick ||
			bumper_count_u32 != static_cast<uint32_t>(BUMPER_POOL_SIZE)) {
		return false;
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	if (!state_buffer[index].data || heap_size > static_cast<uint32_t>(gamestate_data.get_capacity())) {
		return false;
	}
	SavedState& state = state_buffer[index];
	state.bumper_state_count = static_cast<int>(bumper_count_u32);
	if (!read_bytes(&state.bumper_scheduler_lap, sizeof(state.bumper_scheduler_lap)) ||
			!read_bytes(&state.bumper_next_sequence, sizeof(state.bumper_next_sequence))) {
		return false;
	}
	for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
		BumperState& bumper = state.bumper_states[i];
		if (!read_bytes(&bumper.active, sizeof(bumper.active)) ||
				!read_bytes(&bumper.spawn_lap, sizeof(bumper.spawn_lap)) ||
				!read_bytes(&bumper.next_sequence, sizeof(bumper.next_sequence)) ||
				!read_bytes(&bumper.target_lane, sizeof(bumper.target_lane))) {
			return false;
		}
	}
	state.voice_transforms.resize(voice_count);
	state.voice_transform_count = static_cast<int>(voice_count);
	for (uint32_t i = 0; i < voice_count; ++i) {
		SavedVoiceTransform& voice = state.voice_transforms[i];
		float values[3] = {};
		if (!read_bytes(&voice.player_id, sizeof(voice.player_id)) ||
				!read_bytes(values, sizeof(values))) {
			return false;
		}
		voice.origin = SimVec3(values[0], values[1], values[2]);
	}
	const size_t vehicle_local_state_size =
		static_cast<size_t>(car_local_state_size) + static_cast<size_t>(bumper_local_state_size);
	if (ptr + vehicle_local_state_size > end) {
		return false;
	}
	state.car_local_state_size = car_local_state_size;
	state.bumper_local_state_size = bumper_local_state_size;
	state.vehicle_local_state.resize(vehicle_local_state_size);
	if (vehicle_local_state_size > 0) {
		std::memcpy(state.vehicle_local_state.data(), ptr, vehicle_local_state_size);
		ptr += vehicle_local_state_size;
	}
	if (ptr + heap_size > end) {
		return false;
	}
	std::memcpy(state.data, ptr, heap_size);
	state.size = static_cast<int>(heap_size);
	state.tick = target_tick;
	std::memcpy(gamestate_data.heap_start, state.data, heap_size);
	gamestate_data.set_size(state.size);
	tick = target_tick;
	fix_pointers();
	restore_bumper_states_from_saved_state(state);
	if (!restore_vehicle_local_state_from_saved_state(state)) {
		rebuild_road_samples_after_state_load();
	}
	fix_pointers();
	return true;
}

godot::Dictionary GameSim::get_network_state_size_stats() const {
	const NetworkStateSizeStats& stats = last_network_state_size_stats;
	godot::Dictionary out;
	out["total"] = stats.total;
	out["header"] = stats.header;
	out["bumper_meta"] = stats.bumper_meta;
	out["sparks"] = stats.sparks;
	out["car_scalars"] = stats.car_scalars;
	out["bumper_scalars"] = stats.bumper_scalars;
	out["car_vec3"] = stats.car_vec3;
	out["bumper_vec3"] = stats.bumper_vec3;
	out["car_transform"] = stats.car_transform;
	out["bumper_transform"] = stats.bumper_transform;
	out["car_basis"] = stats.car_basis;
	out["bumper_basis"] = stats.bumper_basis;
	out["car_conditionals"] = stats.car_conditionals;
	out["bumper_conditionals"] = stats.bumper_conditionals;
	out["car_tilt"] = stats.car_tilt;
	out["bumper_tilt"] = stats.bumper_tilt;
	out["car_wall"] = stats.car_wall;
	out["bumper_wall"] = stats.bumper_wall;
	out["triggers"] = stats.triggers;
	out["car_restore_count"] = stats.car_restore_count;
	out["bumper_restore_count"] = stats.bumper_restore_count;
	out["active_bumper_count"] = stats.active_bumper_count;
	out["active_spark_count"] = stats.active_spark_count;
	out["trigger_count"] = stats.trigger_count;
	out["car_count"] = stats.car_count;
	out["bumper_count"] = stats.bumper_count;
	return out;
}

bool GameSim::load_state_data(int target_tick, godot::PackedByteArray data) {
	if (data.size() >= static_cast<int>(sizeof(uint32_t))) {
		uint32_t magic = 0;
		std::memcpy(&magic, data.ptr(), sizeof(uint32_t));
		if (magic == MXT_NET_STATE_MAGIC) {
			if (!deserialize_network_state(target_tick, data)) {
				godot::UtilityFunctions::printerr(godot::String("MXT load_state_data failed to deserialize network state"));
				return false;
			}
			tick = target_tick + 1;
			return true;
		}
	}
	set_state_data(target_tick, data);
	load_state(target_tick);
	return true;
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
			BumperState live_bumper_states[BUMPER_POOL_SIZE];
			const uint8_t live_bumper_scheduler_lap = bumper_scheduler_lap;
			const uint32_t live_bumper_next_sequence = bumper_next_sequence;
			for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
				live_bumper_states[i] = bumper_states[i];
			}
			if (live_size > 0) {
				if (static_cast<int>(network_state_live_backup.size()) < live_size) {
					network_state_live_backup.resize(static_cast<size_t>(live_size));
				}
				std::memcpy(network_state_live_backup.data(), gamestate_data.heap_start, static_cast<size_t>(live_size));
			}
			if (!deserialize_network_state(target_tick, data)) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT set_state_data failed to deserialize network state tick="),
					static_cast<int64_t>(target_tick));
			}
			if (live_size > 0 && static_cast<int>(network_state_live_backup.size()) >= live_size) {
				std::memcpy(gamestate_data.heap_start, network_state_live_backup.data(), static_cast<size_t>(live_size));
				gamestate_data.set_size(live_size);
				fix_pointers();
			}
			for (int i = 0; i < BUMPER_POOL_SIZE; ++i) {
				bumper_states[i] = live_bumper_states[i];
			}
			bumper_scheduler_lap = live_bumper_scheduler_lap;
			bumper_next_sequence = live_bumper_next_sequence;
			return;
		}
	}
	// game state never changes in size after instantiation
	// and should always be the same size between the server and all clients
	int size = static_cast<int>(data.size());
	if (size > 0) {
		memcpy(state_buffer[index].data, data.ptr(), size);
		state_buffer[index].size = size;
		state_buffer[index].tick = target_tick;
		state_buffer[index].voice_transform_count = 0;
	}
}

#undef MXT_NET_CAR_SCALAR_FIELDS
#undef MXT_NET_CAR_VEC3_FIELDS
#undef MXT_NET_CAR_VEC3_HALF_FIELDS
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

	const int total_lane_count = (num_cars + 3) & ~3;
	for (int i = 0; i < total_lane_count; ++i) {
		cars[i].soa->current_track[cars[i].soa_index] = current_track;
		if (car_properties_array) {
			cars[i].soa->car_properties[cars[i].soa_index] = &car_properties_array[i];
		}
	}
	if (bumper_cars && bumper_count > 0) {
		const int total_bumper_lane_count = (bumper_count + 3) & ~3;
		for (int i = 0; i < total_bumper_lane_count; ++i) {
			const int slot = i < bumper_count ? i : bumper_count - 1;
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			soa.current_track[lane] = (i < bumper_count && bumper_states[slot].active) ? current_track : nullptr;
			if (bumper_properties_array) {
				soa.car_properties[lane] = &bumper_properties_array[i];
			}
		}
	}

}
