#pragma once

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/math_utils.h"
#include "track/road_modulation.h"
#include "car/physics_car.h"
#include <algorithm>
#include <cstdlib>
#include <new>

class HeapHandler
{
private:
	void* heap;
	char* heap_end;
	bool live;
public:

	char* heap_allocation;
	char* heap_start;

	HeapHandler()
	{
		live = false;
	};

	HeapHandler(size_t size)
	{
		heap = malloc(size);
		if (!heap) {
			std::abort();
		}
		heap_allocation = reinterpret_cast<char*>(heap);
		heap_start = heap_allocation;
		heap_end = heap_allocation + size;
		live = true;
	};

	void instantiate(size_t size)
	{
		heap = malloc(size);
		if (!heap) {
			std::abort();
		}
		heap_allocation = reinterpret_cast<char*>(heap);
		heap_start = heap_allocation;
		heap_end = heap_allocation + size;
		live = true;
	};

	void free_heap()
	{
		free(heap);
		heap = nullptr;
		heap_allocation = nullptr;
		heap_start = nullptr;
		heap_end = nullptr;
		live = false;
	};

		int get_size()
	{
		return (int)(heap_allocation - reinterpret_cast<char*>(heap));
	}

	int get_capacity()
	{
		return (int)(heap_end - reinterpret_cast<char*>(heap));
	}

	void set_size(int size)
	{
		heap_allocation = reinterpret_cast<char*>(heap) + size;
	}

	void* allocate_bytes(size_t size)
	{
		if (heap_allocation + size > heap_end)
		{
			std::abort();
		}
		char* out = heap_allocation;
		heap_allocation += size;
		return out;
	}

	void* allocate_aligned_bytes(size_t size, size_t alignment)
	{
		uintptr_t cur = reinterpret_cast<uintptr_t>(heap_allocation);
		uintptr_t aligned = (cur + alignment - 1u) & ~(alignment - 1u);
		char* out = reinterpret_cast<char*>(aligned);
		if (out + size > heap_end)
		{
			std::abort();
		}
		heap_allocation = out + size;
		return out;
	}

	template <typename T>
	T* allocate_class()
	{
		T* new_obj = new (allocate_bytes(sizeof(T)))T;
		return new_obj;
	}

	template <typename T>
	T* allocate_object()
	{
		T* new_obj = reinterpret_cast<T*>(allocate_bytes(sizeof(T)));
		return new_obj;
	}

	template <typename T>
	T* allocate_array(size_t size)
	{
		size_t alignment = alignof(T);
		if (alignment < 64)
		{
			alignment = 64;
		}
		T* new_array = reinterpret_cast<T*>(allocate_aligned_bytes(sizeof(T) * size, alignment));
		return new_array;
	}

	static char* align_existing_allocation(char* cursor, size_t alignment)
	{
		uintptr_t cur = reinterpret_cast<uintptr_t>(cursor);
		uintptr_t aligned = (cur + alignment - 1u) & ~(alignment - 1u);
		return reinterpret_cast<char*>(aligned);
	}

	template <typename T>
	static T* bind_existing_array(char*& cursor, size_t size)
	{
		size_t alignment = alignof(T);
		if (alignment < 64)
		{
			alignment = 64;
		}
		cursor = align_existing_allocation(cursor, alignment);
		T* array = reinterpret_cast<T*>(cursor);
		cursor += sizeof(T) * size;
		return array;
	}

	Curve* allocate_curve_from_buffer(godot::StreamPeerBuffer* in_buffer)
	{
		Curve* out_curve = allocate_object<Curve>();
		out_curve->num_keyframes = (int)in_buffer->get_u32();
		out_curve->keyframes = allocate_array<CurveKeyframe>(out_curve->num_keyframes);
		for (int i = 0; i < out_curve->num_keyframes; i++)
		{
			out_curve->keyframes[i].time = in_buffer->get_float();
			out_curve->keyframes[i].value = in_buffer->get_float();
			out_curve->keyframes[i].tangent_in = in_buffer->get_float();
			out_curve->keyframes[i].tangent_out = in_buffer->get_float();
		}
		return out_curve;
	};

	Curve* allocate_curve_from_keyframe_count(int num_keys)
	{
		Curve* out_curve = allocate_object<Curve>();
		out_curve->num_keyframes = num_keys;
		out_curve->keyframes = allocate_array<CurveKeyframe>(out_curve->num_keyframes);
		for (int i = 0; i < out_curve->num_keyframes; i++)
		{
			out_curve->keyframes[i].time = float(i) / float(num_keys - 1.0f);
			out_curve->keyframes[i].value = 0.0f;
			out_curve->keyframes[i].tangent_in = 0.0f;
			out_curve->keyframes[i].tangent_out = 0.0f;
		}
		return out_curve;
	}

       void allocate_physics_car_soa_arrays(PhysicsCarSoA* soa, int lane_count)
       {
               soa->lane_count = lane_count;
               soa->point_count = lane_count * 4;
#define ALLOCATE_PHYSICS_CAR_SOA_ARRAY(type, name, default_value) soa->name = allocate_array<type>(lane_count);
               PHYSICS_CAR_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_SOA_VEC3(name, default_value) soa->name##_x = allocate_array<float>(lane_count); soa->name##_y = allocate_array<float>(lane_count); soa->name##_z = allocate_array<float>(lane_count);
               PHYSICS_CAR_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_SOA_TRANSFORM(name, default_value) soa->name##_c0x = allocate_array<float>(lane_count); soa->name##_c0y = allocate_array<float>(lane_count); soa->name##_c0z = allocate_array<float>(lane_count); soa->name##_c1x = allocate_array<float>(lane_count); soa->name##_c1y = allocate_array<float>(lane_count); soa->name##_c1z = allocate_array<float>(lane_count); soa->name##_c2x = allocate_array<float>(lane_count); soa->name##_c2y = allocate_array<float>(lane_count); soa->name##_c2z = allocate_array<float>(lane_count); soa->name##_ox = allocate_array<float>(lane_count); soa->name##_oy = allocate_array<float>(lane_count); soa->name##_oz = allocate_array<float>(lane_count);
               PHYSICS_CAR_TRANSFORM_FIELDS(ALLOCATE_PHYSICS_CAR_SOA_TRANSFORM)
#undef ALLOCATE_PHYSICS_CAR_SOA_TRANSFORM
#define ALLOCATE_PHYSICS_CAR_TILT_SOA_ARRAY(type, name, default_value) soa->tilt_##name = allocate_array<type>(soa->point_count);
               PHYSICS_CAR_TILT_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_TILT_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_TILT_SOA_VEC3(name, default_value) soa->tilt_##name##_x = allocate_array<float>(soa->point_count); soa->tilt_##name##_y = allocate_array<float>(soa->point_count); soa->tilt_##name##_z = allocate_array<float>(soa->point_count);
               PHYSICS_CAR_TILT_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_TILT_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_WALL_SOA_VEC3(name, default_value) soa->wall_##name##_x = allocate_array<float>(soa->point_count); soa->wall_##name##_y = allocate_array<float>(soa->point_count); soa->wall_##name##_z = allocate_array<float>(soa->point_count);
               PHYSICS_CAR_WALL_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_WALL_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_WALL_SOA_VEC3

               for (int i = 0; i < lane_count; ++i) {
#define CONSTRUCT_PHYSICS_CAR_SOA_ELEMENT(type, name, default_value) new (&soa->name[i]) type();
                       PHYSICS_CAR_SCALAR_FIELDS(CONSTRUCT_PHYSICS_CAR_SOA_ELEMENT)
#undef CONSTRUCT_PHYSICS_CAR_SOA_ELEMENT
               }
               for (int i = 0; i < soa->point_count; ++i) {
#define CONSTRUCT_PHYSICS_CAR_TILT_SOA_ELEMENT(type, name, default_value) new (&soa->tilt_##name[i]) type();
                       PHYSICS_CAR_TILT_SCALAR_FIELDS(CONSTRUCT_PHYSICS_CAR_TILT_SOA_ELEMENT)
#undef CONSTRUCT_PHYSICS_CAR_TILT_SOA_ELEMENT
               }
       }

       static void bind_physics_car_soa_arrays(PhysicsCarSoA* soa, int lane_count, char*& cursor)
       {
               soa->lane_count = lane_count;
               soa->point_count = lane_count * 4;
#define BIND_PHYSICS_CAR_SOA_ARRAY(type, name, default_value) soa->name = bind_existing_array<type>(cursor, lane_count);
               PHYSICS_CAR_SCALAR_FIELDS(BIND_PHYSICS_CAR_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_SOA_ARRAY
#define BIND_PHYSICS_CAR_SOA_VEC3(name, default_value) soa->name##_x = bind_existing_array<float>(cursor, lane_count); soa->name##_y = bind_existing_array<float>(cursor, lane_count); soa->name##_z = bind_existing_array<float>(cursor, lane_count);
               PHYSICS_CAR_VEC3_FIELDS(BIND_PHYSICS_CAR_SOA_VEC3)
#undef BIND_PHYSICS_CAR_SOA_VEC3
#define BIND_PHYSICS_CAR_SOA_TRANSFORM(name, default_value) soa->name##_c0x = bind_existing_array<float>(cursor, lane_count); soa->name##_c0y = bind_existing_array<float>(cursor, lane_count); soa->name##_c0z = bind_existing_array<float>(cursor, lane_count); soa->name##_c1x = bind_existing_array<float>(cursor, lane_count); soa->name##_c1y = bind_existing_array<float>(cursor, lane_count); soa->name##_c1z = bind_existing_array<float>(cursor, lane_count); soa->name##_c2x = bind_existing_array<float>(cursor, lane_count); soa->name##_c2y = bind_existing_array<float>(cursor, lane_count); soa->name##_c2z = bind_existing_array<float>(cursor, lane_count); soa->name##_ox = bind_existing_array<float>(cursor, lane_count); soa->name##_oy = bind_existing_array<float>(cursor, lane_count); soa->name##_oz = bind_existing_array<float>(cursor, lane_count);
               PHYSICS_CAR_TRANSFORM_FIELDS(BIND_PHYSICS_CAR_SOA_TRANSFORM)
#undef BIND_PHYSICS_CAR_SOA_TRANSFORM
#define BIND_PHYSICS_CAR_TILT_SOA_ARRAY(type, name, default_value) soa->tilt_##name = bind_existing_array<type>(cursor, soa->point_count);
               PHYSICS_CAR_TILT_SCALAR_FIELDS(BIND_PHYSICS_CAR_TILT_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_TILT_SOA_ARRAY
#define BIND_PHYSICS_CAR_TILT_SOA_VEC3(name, default_value) soa->tilt_##name##_x = bind_existing_array<float>(cursor, soa->point_count); soa->tilt_##name##_y = bind_existing_array<float>(cursor, soa->point_count); soa->tilt_##name##_z = bind_existing_array<float>(cursor, soa->point_count);
               PHYSICS_CAR_TILT_VEC3_FIELDS(BIND_PHYSICS_CAR_TILT_SOA_VEC3)
#undef BIND_PHYSICS_CAR_TILT_SOA_VEC3
#define BIND_PHYSICS_CAR_WALL_SOA_VEC3(name, default_value) soa->wall_##name##_x = bind_existing_array<float>(cursor, soa->point_count); soa->wall_##name##_y = bind_existing_array<float>(cursor, soa->point_count); soa->wall_##name##_z = bind_existing_array<float>(cursor, soa->point_count);
               PHYSICS_CAR_WALL_VEC3_FIELDS(BIND_PHYSICS_CAR_WALL_SOA_VEC3)
#undef BIND_PHYSICS_CAR_WALL_SOA_VEC3
       }

       void repair_allocated_cars(PhysicsCar* cars, int num_cars, PhysicsCarProperties** out_properties)
       {
               if (!cars || num_cars <= 0) {
                       return;
               }
               constexpr int kVehicleShardCount = 5;
               const int total_lane_count = (num_cars + 3) & ~3;
               PhysicsCarSoA* shards = cars[0].soa;
               if (!shards) {
                       return;
               }

               char* cursor = reinterpret_cast<char*>(shards + kVehicleShardCount);
               const int shard_lane_base = ((total_lane_count / kVehicleShardCount) + 3) & ~3;
               int remaining_lanes = total_lane_count;
               int global_start = 0;
               for (int shard = 0; shard < kVehicleShardCount; ++shard) {
                       int lane_count = shard_lane_base;
                       const int shards_left = kVehicleShardCount - shard;
                       if (lane_count * shards_left > remaining_lanes) {
                               lane_count = (remaining_lanes + shards_left - 1) / shards_left;
                               lane_count = (lane_count + 3) & ~3;
                       }
                       if (shard == kVehicleShardCount - 1) {
                               lane_count = remaining_lanes;
                       }

                       PhysicsCarSoA* soa = &shards[shard];
                       soa->global_start = global_start;
                       soa->shard_index = shard;
                       soa->shard_count = kVehicleShardCount;
                       soa->total_count = num_cars;
                       soa->total_lane_count = total_lane_count;
                       soa->shards = shards;
                       soa->count = std::max(0, std::min(num_cars - global_start, lane_count));
                       bind_physics_car_soa_arrays(soa, lane_count, cursor);
                       for (int lane = 0; lane < lane_count; ++lane) {
                               cars[global_start + lane].soa = soa;
                               cars[global_start + lane].soa_index = lane;
                       }
                       global_start += lane_count;
                       remaining_lanes -= lane_count;
               }

               PhysicsCarProperties* properties = bind_existing_array<PhysicsCarProperties>(cursor, total_lane_count);
               if (out_properties) {
                       *out_properties = properties;
               }
       }

       PhysicsCar* create_and_allocate_cars(int num_cars, PhysicsCarProperties** out_properties)
       {
               constexpr int kVehicleShardCount = 5;
               const int total_lane_count = (num_cars + 3) & ~3;
               PhysicsCarSoA* shards = allocate_array<PhysicsCarSoA>(kVehicleShardCount);
               const int shard_lane_base = ((total_lane_count / kVehicleShardCount) + 3) & ~3;
               int remaining_lanes = total_lane_count;
               int global_start = 0;
               for (int shard = 0; shard < kVehicleShardCount; ++shard) {
                       new (&shards[shard]) PhysicsCarSoA();
                       int lane_count = shard_lane_base;
                       const int shards_left = kVehicleShardCount - shard;
                       if (lane_count * shards_left > remaining_lanes) {
                               lane_count = (remaining_lanes + shards_left - 1) / shards_left;
                               lane_count = (lane_count + 3) & ~3;
                       }
                       if (shard == kVehicleShardCount - 1) {
                               lane_count = remaining_lanes;
                       }
                       shards[shard].global_start = global_start;
                       shards[shard].shard_index = shard;
                       shards[shard].shard_count = kVehicleShardCount;
                       shards[shard].total_count = num_cars;
                       shards[shard].total_lane_count = total_lane_count;
                       shards[shard].shards = shards;
                       shards[shard].count = std::max(0, std::min(num_cars - global_start, lane_count));
                       allocate_physics_car_soa_arrays(&shards[shard], lane_count);
                       global_start += lane_count;
                       remaining_lanes -= lane_count;
               }

               PhysicsCar* cars = static_cast<PhysicsCar*>(::malloc(sizeof(PhysicsCar) * total_lane_count));
               if (!cars) {
                       std::abort();
               }
               for (int shard = 0; shard < kVehicleShardCount; ++shard) {
                       PhysicsCarSoA* soa = &shards[shard];
                       for (int lane = 0; lane < soa->lane_count; ++lane) {
                               new (&cars[soa->global_start + lane]) PhysicsCar(soa, lane);
                       }
               }
               PhysicsCarProperties* properties = allocate_array<PhysicsCarProperties>(total_lane_count);
               if (out_properties) {
                       *out_properties = properties;
               }
               for (int i = 0; i < num_cars; i++)
               {
                       PhysicsCarProperties* new_car_properties = &properties[i];

			new_car_properties->weight_kg = randf_range(1100.0f, 3000.0f);
			new_car_properties->acceleration = randf_range(0.3f, 0.8f);
			new_car_properties->max_speed = randf_range(-0.1f, 0.5f);
			new_car_properties->grip_1 = randf_range(0.3f, 1.1f);
			new_car_properties->grip_2 = randf_range(0.3f, 0.6f);
			new_car_properties->grip_3 = randf_range(0.05f, 0.25f);
			new_car_properties->turn_tension = randf_range(0.0f, 0.3f);
			new_car_properties->drift_accel = randf_range(-0.5f, 2.0f);
			new_car_properties->turn_movement = randf_range(110.0f, 200.0f);
			new_car_properties->strafe_turn = randf_range(0.0f, 100.0f);
			new_car_properties->strafe = randf_range(20.0f, 60.0f);
			new_car_properties->turn_reaction = randf_range(0.0f, 30.0f);
			new_car_properties->boost_strength = randf_range(10.0f, 30.0f);
			new_car_properties->boost_length = randf_range(0.75f, 2.0f);
			new_car_properties->turn_decel = randf_range(-0.05f, 0.05f);
			new_car_properties->drag = randf_range(0.006f, 0.01f);
			new_car_properties->body = randf_range(0.5f, 1.5f);
			//new_car_properties->weight_kg = 1260.f;
			//new_car_properties->acceleration = 0.45f;
			//new_car_properties->max_speed = 0.1f;
			//new_car_properties->grip_1 = 0.47f;
			//new_car_properties->grip_2 = 0.7f;
			//new_car_properties->grip_3 = 0.2f;
			//new_car_properties->turn_tension = 0.12f;
			//new_car_properties->drift_accel = 0.4f;
			//new_car_properties->turn_movement = 145.f;
			//new_car_properties->strafe_turn = 20.f;
			//new_car_properties->strafe = 35.f;
			//new_car_properties->turn_reaction = 10.f;
			//new_car_properties->boost_strength = 14.f;
			//new_car_properties->boost_length = 1.5f;
			//new_car_properties->turn_decel = 0.02f;
			//new_car_properties->drag = 0.01f;
			//new_car_properties->body = 0.85f;
			new_car_properties->tilt_corners[0] = SimVec3(0.8f, 0.f, -1.5f);
			new_car_properties->tilt_corners[1] = SimVec3(-0.8f, 0.f, -1.5f);
			new_car_properties->tilt_corners[2] = SimVec3(1.1f, 0.f, 1.7f);
			new_car_properties->tilt_corners[3] = SimVec3(-1.1f, 0.f, 1.7f);
			new_car_properties->wall_corners[0] = SimVec3(1.0f, -0.1f, -1.7f);
			new_car_properties->wall_corners[1] = SimVec3(-1.0f, -0.1f, -1.7f);
			new_car_properties->wall_corners[2] = SimVec3(1.3f, -0.1f, 1.9f);
			new_car_properties->wall_corners[3] = SimVec3(-1.3f, -0.1f, 1.9f);
			cars[i].soa->m_accel_setting[cars[i].soa_index] = 1.0f;
			cars[i].soa->car_properties[cars[i].soa_index] = new_car_properties;

		}
               for (int i = num_cars; i < total_lane_count; ++i)
               {
                       PhysicsCarProperties* inert_car_properties = &properties[i];
                       cars[i].soa->m_accel_setting[cars[i].soa_index] = 1.0f;
                       cars[i].soa->car_properties[cars[i].soa_index] = inert_car_properties;
                       cars[i].soa->machine_state[cars[i].soa_index] = 0;
                       cars[i].soa->stat_weight[cars[i].soa_index] = 1.0f;
               }
		return cars;
	}
};
