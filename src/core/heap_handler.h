#pragma once

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "core/curve.h"
#include "core/math_utils.h"
#include "track/road_modulation.h"
#include "car/physics_car.h"
#include <algorithm>
#include <cstdlib>
#include <new>

class HeapHandler
{
private:
	void* heap = nullptr;
	char* heap_end = nullptr;
	bool live = false;
public:

	char* heap_allocation = nullptr;
	char* heap_start = nullptr;

	HeapHandler()
	{};

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
		if (heap) {
			free(heap);
		}
		heap = nullptr;
		heap_allocation = nullptr;
		heap_start = nullptr;
		heap_end = nullptr;
		live = false;
	};

	bool is_live() const
	{
		return live;
	}

	int get_size() const
	{
		if (!live) {
			return 0;
		}
		return (int)(heap_allocation - reinterpret_cast<char*>(heap));
	}

	int get_capacity() const
	{
		if (!live) {
			return 0;
		}
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

	template <typename T>
	static T* allocate_static_array(size_t size)
	{
		T* new_array = static_cast<T*>(::malloc(sizeof(T) * size));
		if (!new_array) {
			std::abort();
		}
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
#define ALLOCATE_PHYSICS_CAR_STATIC_SOA_ARRAY(type, name, default_value) soa->name = allocate_static_array<type>(lane_count);
               PHYSICS_CAR_STATIC_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_STATIC_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_STATIC_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_STATE_SOA_ARRAY(type, name, default_value) soa->name = allocate_array<type>(lane_count);
               PHYSICS_CAR_STATE_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_STATE_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_STATE_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_ARRAY(type, name, default_value) soa->name = allocate_static_array<type>(lane_count);
               PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_STATE_SOA_VEC3(name, default_value) soa->name##_x = allocate_array<float>(lane_count); soa->name##_y = allocate_array<float>(lane_count); soa->name##_z = allocate_array<float>(lane_count);
               PHYSICS_CAR_STATE_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_STATE_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_STATE_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_VEC3(name, default_value) soa->name##_x = allocate_static_array<float>(lane_count); soa->name##_y = allocate_static_array<float>(lane_count); soa->name##_z = allocate_static_array<float>(lane_count);
               PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_STATE_SOA_TRANSFORM(name, default_value) soa->name##_c0x = allocate_array<float>(lane_count); soa->name##_c0y = allocate_array<float>(lane_count); soa->name##_c0z = allocate_array<float>(lane_count); soa->name##_c1x = allocate_array<float>(lane_count); soa->name##_c1y = allocate_array<float>(lane_count); soa->name##_c1z = allocate_array<float>(lane_count); soa->name##_c2x = allocate_array<float>(lane_count); soa->name##_c2y = allocate_array<float>(lane_count); soa->name##_c2z = allocate_array<float>(lane_count); soa->name##_ox = allocate_array<float>(lane_count); soa->name##_oy = allocate_array<float>(lane_count); soa->name##_oz = allocate_array<float>(lane_count);
               PHYSICS_CAR_STATE_TRANSFORM_FIELDS(ALLOCATE_PHYSICS_CAR_STATE_SOA_TRANSFORM)
#undef ALLOCATE_PHYSICS_CAR_STATE_SOA_TRANSFORM
#define ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM(name, default_value) soa->name##_c0x = allocate_static_array<float>(lane_count); soa->name##_c0y = allocate_static_array<float>(lane_count); soa->name##_c0z = allocate_static_array<float>(lane_count); soa->name##_c1x = allocate_static_array<float>(lane_count); soa->name##_c1y = allocate_static_array<float>(lane_count); soa->name##_c1z = allocate_static_array<float>(lane_count); soa->name##_c2x = allocate_static_array<float>(lane_count); soa->name##_c2y = allocate_static_array<float>(lane_count); soa->name##_c2z = allocate_static_array<float>(lane_count); soa->name##_ox = allocate_static_array<float>(lane_count); soa->name##_oy = allocate_static_array<float>(lane_count); soa->name##_oz = allocate_static_array<float>(lane_count);
               PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM)
#undef ALLOCATE_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM
#define ALLOCATE_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY(type, name, default_value) soa->tilt_##name = allocate_static_array<type>(soa->point_count);
               PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_TILT_STATE_SOA_ARRAY(type, name, default_value) soa->tilt_##name = allocate_array<type>(soa->point_count);
               PHYSICS_CAR_TILT_STATE_SCALAR_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_STATE_SOA_ARRAY)
#undef ALLOCATE_PHYSICS_CAR_TILT_STATE_SOA_ARRAY
#define ALLOCATE_PHYSICS_CAR_TILT_STATIC_SOA_VEC3(name, default_value) soa->tilt_##name##_x = allocate_static_array<float>(soa->point_count); soa->tilt_##name##_y = allocate_static_array<float>(soa->point_count); soa->tilt_##name##_z = allocate_static_array<float>(soa->point_count);
               PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_STATIC_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_TILT_STATIC_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_TILT_STATE_SOA_VEC3(name, default_value) soa->tilt_##name##_x = allocate_array<float>(soa->point_count); soa->tilt_##name##_y = allocate_array<float>(soa->point_count); soa->tilt_##name##_z = allocate_array<float>(soa->point_count);
               PHYSICS_CAR_TILT_STATE_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_STATE_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_TILT_STATE_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3(name, default_value) soa->tilt_##name##_x = allocate_static_array<float>(soa->point_count); soa->tilt_##name##_y = allocate_static_array<float>(soa->point_count); soa->tilt_##name##_z = allocate_static_array<float>(soa->point_count);
               PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_WALL_STATIC_SOA_VEC3(name, default_value) soa->wall_##name##_x = allocate_static_array<float>(soa->point_count); soa->wall_##name##_y = allocate_static_array<float>(soa->point_count); soa->wall_##name##_z = allocate_static_array<float>(soa->point_count);
               PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_WALL_STATIC_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_WALL_STATIC_SOA_VEC3
#define ALLOCATE_PHYSICS_CAR_WALL_STATE_SOA_VEC3(name, default_value) soa->wall_##name##_x = allocate_array<float>(soa->point_count); soa->wall_##name##_y = allocate_array<float>(soa->point_count); soa->wall_##name##_z = allocate_array<float>(soa->point_count);
               PHYSICS_CAR_WALL_STATE_VEC3_FIELDS(ALLOCATE_PHYSICS_CAR_WALL_STATE_SOA_VEC3)
#undef ALLOCATE_PHYSICS_CAR_WALL_STATE_SOA_VEC3

               for (int i = 0; i < lane_count; ++i) {
#define CONSTRUCT_PHYSICS_CAR_STATIC_SOA_ELEMENT(type, name, default_value) new (&soa->name[i]) type();
                       PHYSICS_CAR_STATIC_SCALAR_FIELDS(CONSTRUCT_PHYSICS_CAR_STATIC_SOA_ELEMENT)
#undef CONSTRUCT_PHYSICS_CAR_STATIC_SOA_ELEMENT
#define CONSTRUCT_PHYSICS_CAR_STATE_SOA_ELEMENT(type, name, default_value) new (&soa->name[i]) type();
                       PHYSICS_CAR_STATE_SCALAR_FIELDS(CONSTRUCT_PHYSICS_CAR_STATE_SOA_ELEMENT)
#undef CONSTRUCT_PHYSICS_CAR_STATE_SOA_ELEMENT
#define CONSTRUCT_PHYSICS_CAR_TRANSIENT_SOA_ELEMENT(type, name, default_value) new (&soa->name[i]) type();
                       PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(CONSTRUCT_PHYSICS_CAR_TRANSIENT_SOA_ELEMENT)
#undef CONSTRUCT_PHYSICS_CAR_TRANSIENT_SOA_ELEMENT
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
#define BIND_PHYSICS_CAR_STATIC_SOA_ARRAY(type, name, default_value) if (!soa->name) soa->name = allocate_static_array<type>(lane_count);
               PHYSICS_CAR_STATIC_SCALAR_FIELDS(BIND_PHYSICS_CAR_STATIC_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_STATIC_SOA_ARRAY
#define BIND_PHYSICS_CAR_STATE_SOA_ARRAY(type, name, default_value) soa->name = bind_existing_array<type>(cursor, lane_count);
               PHYSICS_CAR_STATE_SCALAR_FIELDS(BIND_PHYSICS_CAR_STATE_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_STATE_SOA_ARRAY
#define BIND_PHYSICS_CAR_TRANSIENT_SOA_ARRAY(type, name, default_value) if (!soa->name) soa->name = allocate_static_array<type>(lane_count);
               PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(BIND_PHYSICS_CAR_TRANSIENT_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_TRANSIENT_SOA_ARRAY
#define BIND_PHYSICS_CAR_STATE_SOA_VEC3(name, default_value) soa->name##_x = bind_existing_array<float>(cursor, lane_count); soa->name##_y = bind_existing_array<float>(cursor, lane_count); soa->name##_z = bind_existing_array<float>(cursor, lane_count);
               PHYSICS_CAR_STATE_VEC3_FIELDS(BIND_PHYSICS_CAR_STATE_SOA_VEC3)
#undef BIND_PHYSICS_CAR_STATE_SOA_VEC3
#define BIND_PHYSICS_CAR_TRANSIENT_SOA_VEC3(name, default_value) if (!soa->name##_x) soa->name##_x = allocate_static_array<float>(lane_count); if (!soa->name##_y) soa->name##_y = allocate_static_array<float>(lane_count); if (!soa->name##_z) soa->name##_z = allocate_static_array<float>(lane_count);
               PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(BIND_PHYSICS_CAR_TRANSIENT_SOA_VEC3)
#undef BIND_PHYSICS_CAR_TRANSIENT_SOA_VEC3
#define BIND_PHYSICS_CAR_STATE_SOA_TRANSFORM(name, default_value) soa->name##_c0x = bind_existing_array<float>(cursor, lane_count); soa->name##_c0y = bind_existing_array<float>(cursor, lane_count); soa->name##_c0z = bind_existing_array<float>(cursor, lane_count); soa->name##_c1x = bind_existing_array<float>(cursor, lane_count); soa->name##_c1y = bind_existing_array<float>(cursor, lane_count); soa->name##_c1z = bind_existing_array<float>(cursor, lane_count); soa->name##_c2x = bind_existing_array<float>(cursor, lane_count); soa->name##_c2y = bind_existing_array<float>(cursor, lane_count); soa->name##_c2z = bind_existing_array<float>(cursor, lane_count); soa->name##_ox = bind_existing_array<float>(cursor, lane_count); soa->name##_oy = bind_existing_array<float>(cursor, lane_count); soa->name##_oz = bind_existing_array<float>(cursor, lane_count);
               PHYSICS_CAR_STATE_TRANSFORM_FIELDS(BIND_PHYSICS_CAR_STATE_SOA_TRANSFORM)
#undef BIND_PHYSICS_CAR_STATE_SOA_TRANSFORM
#define BIND_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM(name, default_value) if (!soa->name##_c0x) soa->name##_c0x = allocate_static_array<float>(lane_count); if (!soa->name##_c0y) soa->name##_c0y = allocate_static_array<float>(lane_count); if (!soa->name##_c0z) soa->name##_c0z = allocate_static_array<float>(lane_count); if (!soa->name##_c1x) soa->name##_c1x = allocate_static_array<float>(lane_count); if (!soa->name##_c1y) soa->name##_c1y = allocate_static_array<float>(lane_count); if (!soa->name##_c1z) soa->name##_c1z = allocate_static_array<float>(lane_count); if (!soa->name##_c2x) soa->name##_c2x = allocate_static_array<float>(lane_count); if (!soa->name##_c2y) soa->name##_c2y = allocate_static_array<float>(lane_count); if (!soa->name##_c2z) soa->name##_c2z = allocate_static_array<float>(lane_count); if (!soa->name##_ox) soa->name##_ox = allocate_static_array<float>(lane_count); if (!soa->name##_oy) soa->name##_oy = allocate_static_array<float>(lane_count); if (!soa->name##_oz) soa->name##_oz = allocate_static_array<float>(lane_count);
               PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(BIND_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM)
#undef BIND_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM
#define BIND_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY(type, name, default_value) if (!soa->tilt_##name) soa->tilt_##name = allocate_static_array<type>(soa->point_count);
               PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(BIND_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY
#define BIND_PHYSICS_CAR_TILT_STATE_SOA_ARRAY(type, name, default_value) soa->tilt_##name = bind_existing_array<type>(cursor, soa->point_count);
               PHYSICS_CAR_TILT_STATE_SCALAR_FIELDS(BIND_PHYSICS_CAR_TILT_STATE_SOA_ARRAY)
#undef BIND_PHYSICS_CAR_TILT_STATE_SOA_ARRAY
#define BIND_PHYSICS_CAR_TILT_STATIC_SOA_VEC3(name, default_value) if (!soa->tilt_##name##_x) soa->tilt_##name##_x = allocate_static_array<float>(soa->point_count); if (!soa->tilt_##name##_y) soa->tilt_##name##_y = allocate_static_array<float>(soa->point_count); if (!soa->tilt_##name##_z) soa->tilt_##name##_z = allocate_static_array<float>(soa->point_count);
               PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(BIND_PHYSICS_CAR_TILT_STATIC_SOA_VEC3)
#undef BIND_PHYSICS_CAR_TILT_STATIC_SOA_VEC3
#define BIND_PHYSICS_CAR_TILT_STATE_SOA_VEC3(name, default_value) soa->tilt_##name##_x = bind_existing_array<float>(cursor, soa->point_count); soa->tilt_##name##_y = bind_existing_array<float>(cursor, soa->point_count); soa->tilt_##name##_z = bind_existing_array<float>(cursor, soa->point_count);
               PHYSICS_CAR_TILT_STATE_VEC3_FIELDS(BIND_PHYSICS_CAR_TILT_STATE_SOA_VEC3)
#undef BIND_PHYSICS_CAR_TILT_STATE_SOA_VEC3
#define BIND_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3(name, default_value) if (!soa->tilt_##name##_x) soa->tilt_##name##_x = allocate_static_array<float>(soa->point_count); if (!soa->tilt_##name##_y) soa->tilt_##name##_y = allocate_static_array<float>(soa->point_count); if (!soa->tilt_##name##_z) soa->tilt_##name##_z = allocate_static_array<float>(soa->point_count);
               PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(BIND_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3)
#undef BIND_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3
#define BIND_PHYSICS_CAR_WALL_STATIC_SOA_VEC3(name, default_value) if (!soa->wall_##name##_x) soa->wall_##name##_x = allocate_static_array<float>(soa->point_count); if (!soa->wall_##name##_y) soa->wall_##name##_y = allocate_static_array<float>(soa->point_count); if (!soa->wall_##name##_z) soa->wall_##name##_z = allocate_static_array<float>(soa->point_count);
               PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(BIND_PHYSICS_CAR_WALL_STATIC_SOA_VEC3)
#undef BIND_PHYSICS_CAR_WALL_STATIC_SOA_VEC3
#define BIND_PHYSICS_CAR_WALL_STATE_SOA_VEC3(name, default_value) soa->wall_##name##_x = bind_existing_array<float>(cursor, soa->point_count); soa->wall_##name##_y = bind_existing_array<float>(cursor, soa->point_count); soa->wall_##name##_z = bind_existing_array<float>(cursor, soa->point_count);
               PHYSICS_CAR_WALL_STATE_VEC3_FIELDS(BIND_PHYSICS_CAR_WALL_STATE_SOA_VEC3)
#undef BIND_PHYSICS_CAR_WALL_STATE_SOA_VEC3
       }

       static void free_physics_car_static_soa_arrays(PhysicsCar* cars, int num_cars)
       {
               if (!cars || num_cars <= 0 || !cars[0].soa || !cars[0].soa->shards) {
                       return;
               }
               PhysicsCarSoA* shards = cars[0].soa->shards;
               const int shard_count = cars[0].soa->shard_count;
               for (int shard = 0; shard < shard_count; ++shard) {
                       PhysicsCarSoA* soa = &shards[shard];
#define FREE_PHYSICS_CAR_STATIC_SOA_ARRAY(type, name, default_value) ::free(soa->name); soa->name = nullptr;
                       PHYSICS_CAR_STATIC_SCALAR_FIELDS(FREE_PHYSICS_CAR_STATIC_SOA_ARRAY)
#undef FREE_PHYSICS_CAR_STATIC_SOA_ARRAY
#define FREE_PHYSICS_CAR_TRANSIENT_SOA_ARRAY(type, name, default_value) ::free(soa->name); soa->name = nullptr;
                       PHYSICS_CAR_TRANSIENT_SCALAR_FIELDS(FREE_PHYSICS_CAR_TRANSIENT_SOA_ARRAY)
#undef FREE_PHYSICS_CAR_TRANSIENT_SOA_ARRAY
#define FREE_PHYSICS_CAR_TRANSIENT_SOA_VEC3(name, default_value) ::free(soa->name##_x); ::free(soa->name##_y); ::free(soa->name##_z); soa->name##_x = nullptr; soa->name##_y = nullptr; soa->name##_z = nullptr;
                       PHYSICS_CAR_TRANSIENT_VEC3_FIELDS(FREE_PHYSICS_CAR_TRANSIENT_SOA_VEC3)
#undef FREE_PHYSICS_CAR_TRANSIENT_SOA_VEC3
#define FREE_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM(name, default_value) ::free(soa->name##_c0x); ::free(soa->name##_c0y); ::free(soa->name##_c0z); ::free(soa->name##_c1x); ::free(soa->name##_c1y); ::free(soa->name##_c1z); ::free(soa->name##_c2x); ::free(soa->name##_c2y); ::free(soa->name##_c2z); ::free(soa->name##_ox); ::free(soa->name##_oy); ::free(soa->name##_oz); soa->name##_c0x = nullptr; soa->name##_c0y = nullptr; soa->name##_c0z = nullptr; soa->name##_c1x = nullptr; soa->name##_c1y = nullptr; soa->name##_c1z = nullptr; soa->name##_c2x = nullptr; soa->name##_c2y = nullptr; soa->name##_c2z = nullptr; soa->name##_ox = nullptr; soa->name##_oy = nullptr; soa->name##_oz = nullptr;
                       PHYSICS_CAR_TRANSIENT_TRANSFORM_FIELDS(FREE_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM)
#undef FREE_PHYSICS_CAR_TRANSIENT_SOA_TRANSFORM
#define FREE_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY(type, name, default_value) ::free(soa->tilt_##name); soa->tilt_##name = nullptr;
                       PHYSICS_CAR_TILT_STATIC_SCALAR_FIELDS(FREE_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY)
#undef FREE_PHYSICS_CAR_TILT_STATIC_SOA_ARRAY
#define FREE_PHYSICS_CAR_TILT_STATIC_SOA_VEC3(name, default_value) ::free(soa->tilt_##name##_x); ::free(soa->tilt_##name##_y); ::free(soa->tilt_##name##_z); soa->tilt_##name##_x = nullptr; soa->tilt_##name##_y = nullptr; soa->tilt_##name##_z = nullptr;
                       PHYSICS_CAR_TILT_STATIC_VEC3_FIELDS(FREE_PHYSICS_CAR_TILT_STATIC_SOA_VEC3)
#undef FREE_PHYSICS_CAR_TILT_STATIC_SOA_VEC3
#define FREE_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3(name, default_value) ::free(soa->tilt_##name##_x); ::free(soa->tilt_##name##_y); ::free(soa->tilt_##name##_z); soa->tilt_##name##_x = nullptr; soa->tilt_##name##_y = nullptr; soa->tilt_##name##_z = nullptr;
                       PHYSICS_CAR_TILT_TRANSIENT_VEC3_FIELDS(FREE_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3)
#undef FREE_PHYSICS_CAR_TILT_TRANSIENT_SOA_VEC3
#define FREE_PHYSICS_CAR_WALL_STATIC_SOA_VEC3(name, default_value) ::free(soa->wall_##name##_x); ::free(soa->wall_##name##_y); ::free(soa->wall_##name##_z); soa->wall_##name##_x = nullptr; soa->wall_##name##_y = nullptr; soa->wall_##name##_z = nullptr;
                       PHYSICS_CAR_WALL_STATIC_VEC3_FIELDS(FREE_PHYSICS_CAR_WALL_STATIC_SOA_VEC3)
#undef FREE_PHYSICS_CAR_WALL_STATIC_SOA_VEC3
               }
       }

       void repair_allocated_cars(PhysicsCar* cars, int num_cars, PhysicsCarProperties** out_properties)
       {
               if (!cars || num_cars <= 0) {
                       return;
               }
               constexpr int kVehicleShardCount = MXT_VEHICLE_SHARD_COUNT;
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

               (void)cursor;
       }

       PhysicsCar* create_and_allocate_cars(int num_cars, PhysicsCarProperties** out_properties)
       {
               constexpr int kVehicleShardCount = MXT_VEHICLE_SHARD_COUNT;
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
               PhysicsCarProperties* properties = static_cast<PhysicsCarProperties*>(
                       ::malloc(sizeof(PhysicsCarProperties) * total_lane_count));
               if (!properties) {
                       std::abort();
               }
               for (int i = 0; i < total_lane_count; ++i) {
                       new (&properties[i]) PhysicsCarProperties();
               }
               if (out_properties) {
                       *out_properties = properties;
               }
               for (int i = 0; i < num_cars; i++)
               {
                       PhysicsCarProperties* new_car_properties = &properties[i];

			new_car_properties->base_stats[CAR_STAT_WEIGHT_KG] = randf_range(1100.0f, 3000.0f);
			new_car_properties->base_stats[CAR_STAT_ACCELERATION] = randf_range(0.3f, 0.8f);
			new_car_properties->base_stats[CAR_STAT_MAX_SPEED] = randf_range(-0.1f, 0.5f);
			new_car_properties->base_stats[CAR_STAT_GRIP_1] = randf_range(0.3f, 1.1f);
			new_car_properties->base_stats[CAR_STAT_GRIP_2] = randf_range(0.3f, 0.6f);
			new_car_properties->base_stats[CAR_STAT_GRIP_3] = randf_range(0.05f, 0.25f);
			new_car_properties->base_stats[CAR_STAT_TURN_TENSION] = randf_range(0.0f, 0.3f);
			new_car_properties->base_stats[CAR_STAT_DRIFT_ACCEL] = randf_range(-0.5f, 2.0f);
			new_car_properties->base_stats[CAR_STAT_TURN_MOVEMENT] = randf_range(110.0f, 200.0f);
			new_car_properties->base_stats[CAR_STAT_STRAFE_TURN] = randf_range(0.0f, 100.0f);
			new_car_properties->base_stats[CAR_STAT_STRAFE] = randf_range(20.0f, 60.0f);
			new_car_properties->base_stats[CAR_STAT_TURN_REACTION] = randf_range(0.0f, 30.0f);
			new_car_properties->base_stats[CAR_STAT_MANUAL_TURBO_GAIN] = randf_range(5.7f, 17.1f);
			new_car_properties->base_stats[CAR_STAT_DASHPLATE_TURBO_GAIN] =
				2.0f * new_car_properties->base_stats[CAR_STAT_MANUAL_TURBO_GAIN];
			new_car_properties->base_stats[CAR_STAT_MANUAL_BOOST_DURATION_SECONDS] = randf_range(0.75f, 2.0f);
			new_car_properties->base_stats[CAR_STAT_DASHPLATE_BOOST_DURATION_SECONDS] =
				0.5f * new_car_properties->base_stats[CAR_STAT_MANUAL_BOOST_DURATION_SECONDS];
			new_car_properties->base_stats[CAR_STAT_TURN_DECEL] = randf_range(-0.05f, 0.05f);
			new_car_properties->base_stats[CAR_STAT_DRAG] = randf_range(0.006f, 0.01f);
			new_car_properties->base_stats[CAR_STAT_BODY] = randf_range(0.5f, 1.5f);
			for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
				if (PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) {
					new_car_properties->s_boost_stats[stat] = new_car_properties->base_stats[stat];
				}
			}
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
