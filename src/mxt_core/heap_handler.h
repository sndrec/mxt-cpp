#pragma once

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/math_utils.h"
#include "track/road_modulation.h"
#include "car/physics_car.h"

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
		heap_allocation = reinterpret_cast<char*>(heap);
		heap_start = heap_allocation;
		heap_end = heap_allocation + size;
		live = true;
	};

	void instantiate(size_t size)
	{
		heap = malloc(size);
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
		if (heap_allocation >= heap_end)
		{
			throw std::bad_alloc();
		}
		char* out = heap_allocation;
		heap_allocation += size;
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
		T* new_array = reinterpret_cast<T*>(allocate_bytes(sizeof(T) * size));
		return new_array;
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

	PhysicsCar* create_and_allocate_cars(int num_cars)
	{
		PhysicsCar* cars = allocate_array<PhysicsCar>(num_cars);
		for (int i = 0; i < num_cars; i++)
		{
			PhysicsCarProperties* new_car_properties = allocate_object<PhysicsCarProperties>();

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
			new_car_properties->tilt_corners[0] = godot::Vector3(0.8f, 0.f, -1.5f);
			new_car_properties->tilt_corners[1] = godot::Vector3(-0.8f, 0.f, -1.5f);
			new_car_properties->tilt_corners[2] = godot::Vector3(1.1f, 0.f, 1.7f);
			new_car_properties->tilt_corners[3] = godot::Vector3(-1.1f, 0.f, 1.7f);
			new_car_properties->wall_corners[0] = godot::Vector3(1.0f, -0.1f, -1.7f);
			new_car_properties->wall_corners[1] = godot::Vector3(-1.0f, -0.1f, -1.7f);
			new_car_properties->wall_corners[2] = godot::Vector3(1.3f, -0.1f, 1.9f);
			new_car_properties->wall_corners[3] = godot::Vector3(-1.3f, -0.1f, 1.9f);
			cars[i].m_accel_setting = 1.0f;
			cars[i].car_properties = new_car_properties;

		}
		return cars;
	}
};