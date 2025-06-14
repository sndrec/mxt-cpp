#pragma once

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/math_utils.h"
#include "track/curve_matrix.h"
#include "track/road_shape_base.h"
#include "track/road_modulation.h"
#include "car/physics_car.h"

class HeapHandler
{
private:
	void* heap;
	char* heap_allocation;
	char* heap_end;
	bool live;
public:

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

			cars[i].car_properties = new_car_properties;

			new_car_properties->weight_kg = randf_range(800.0f, 3000.0f);
			new_car_properties->acceleration = randf_range(0.1f, 0.8f);
			new_car_properties->max_speed = randf_range(-0.5f, 0.5f);
			new_car_properties->grip_1 = randf_range(0.15f, 0.8f);
			new_car_properties->grip_2 = randf_range(0.3f, 1.2f);
			new_car_properties->grip_3 = randf_range(0.05f, 0.4f);
			new_car_properties->turn_tension = randf_range(0.0f, 0.3f);
			new_car_properties->drift_accel = randf_range(-2.0f, 2.0f);
			new_car_properties->turn_movement = randf_range(90.0f, 200.0f);
			new_car_properties->strafe_turn = randf_range(0.0f, 60.0f);
			new_car_properties->strafe = randf_range(20.0f, 50.0f);
			new_car_properties->turn_reaction = randf_range(0.0f, 20.0f);
			new_car_properties->boost_strength = randf_range(0.0f, 20.0f);
			new_car_properties->boost_length = randf_range(0.75f, 2.0f);
			new_car_properties->turn_decel = randf_range(-0.05f, 0.05f);
			new_car_properties->drag = randf_range(0.006f, 0.01f);
			new_car_properties->body = randf_range(0.5f, 1.5f);
			new_car_properties->tilt_corners[0] = godot::Vector3(0.8, 0, -1.5);
			new_car_properties->tilt_corners[1] = godot::Vector3(-0.8, 0, -1.5);
			new_car_properties->tilt_corners[2] = godot::Vector3(1.1, 0, 1.7);
			new_car_properties->tilt_corners[3] = godot::Vector3(-1.1, 0, 1.7);
			new_car_properties->wall_corners[0] = godot::Vector3(1.0, -0.1, -1.7);
			new_car_properties->wall_corners[1] = godot::Vector3(-1.0, -0.1, -1.7);
			new_car_properties->wall_corners[2] = godot::Vector3(1.3, -0.1, 1.9);
			new_car_properties->wall_corners[3] = godot::Vector3(-1.3, -0.1, 1.9);
			cars[i].m_accel_setting = randf_range(0.0f, 1.0f);

		}
		return cars;
	}
};