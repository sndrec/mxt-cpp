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
			cars[i].initialize();

			PhysicsCarProperties* new_car_properties = allocate_object<PhysicsCarProperties>();

			cars[i].car_properties = new_car_properties;

			new_car_properties->weight = randf_range(900.0f, 2300.0f);
			new_car_properties->acceleration = randf_range(2.0f, 12.0f);
			new_car_properties->max_speed = randf_range(1.5f, 6.0f);
			new_car_properties->max_health = 100.0f;
			new_car_properties->health_recharge_rate = 24.0f;
			new_car_properties->boost_duration = randf_range(0.5f, 2.0f);
			new_car_properties->boost_energy = randf_range(10.0f, 20.0f);
			new_car_properties->boost_topspeed_mult = randf_range(1.1f, 2.0f);
			new_car_properties->boost_accel_mult = randf_range(1.1f, 1.4f);
			new_car_properties->dash_topspeed_mult = randf_range(1.3f, 2.0f);
			new_car_properties->dash_accel_mult = randf_range(1.15f, 1.6f);
			new_car_properties->boost_and_dash_topspeed_mult = randf_range(2.0f, 3.0f);
			new_car_properties->boost_and_dash_accel_mult = randf_range(1.6f, 2.4f);
			new_car_properties->turbo_add_boost = randf_range(8.0f, 20.0f);
			new_car_properties->turbo_add_dashplate = randf_range(12.0f, 30.0f);
			new_car_properties->turbo_depletion = randf_range(4.0f, 12.0f);
			new_car_properties->turbo_depletion_boost = new_car_properties->turbo_depletion * randf_range(0.5f, 0.9f);
			new_car_properties->turbo_depletion_boost_dash = new_car_properties->turbo_depletion_boost * randf_range(0.5f, 0.9f);
			new_car_properties->turbo_depletion_percentage = randf_range(0.05f, 0.15f);
			new_car_properties->strafe_power = randf_range(20.f, 50.f);
			new_car_properties->strafe_accel = randf_range(20.f, 40.f);
			new_car_properties->strafe_turn_effect = randf_range(0.2f, 3.0f);
			new_car_properties->strafe_qt_laterality = randf_range(0.5f, 1.0f);
			new_car_properties->strafe_mts_laterality = randf_range(0.4f, 0.8f);
			new_car_properties->strafe_qt_mult = randf_range(0.6f, 1.0f);
			new_car_properties->strafe_mts_mult = randf_range(1.0f, 2.0f);
			new_car_properties->turn_strafe_effect = randf_range(5.0f, 20.0f);
			new_car_properties->steer_acceleration = randf_range(8.0f, 18.0f);
			new_car_properties->steer_speed_target = randf_range(50.0f, 100.0f);
			new_car_properties->steer_speed_target_drift = randf_range(140.0f, 220.0f);
			new_car_properties->steer_reaction = randf_range(0.2f, 1.5f);
			new_car_properties->steer_reaction_damp = randf_range(4.0f, 12.0f);
			new_car_properties->angle_drag = randf_range(2.0f, 10.0f);
			new_car_properties->grip = randf_range(1.5f, 8.0f);
			new_car_properties->drift_accel = randf_range(-2.0f, 2.0f);
			new_car_properties->turn_accel = randf_range(-0.3f, 0.3f);
			new_car_properties->drag = randf_range(0.15f, 1.4f);

			Curve* vel_redir_curve = allocate_curve_from_keyframe_count(2);

			vel_redir_curve->keyframes[0].value = randf_range(0.004f, 0.01f);
			vel_redir_curve->keyframes[1].value = randf_range(0.02f, 0.05f);

			Curve* qt_redir_curve = allocate_curve_from_keyframe_count(2);
			qt_redir_curve->keyframes[0].value = randf_range(0.01f, 0.02f);
			qt_redir_curve->keyframes[1].value = randf_range(0.04f, 0.08f);

			Curve* mts_redir_curve = allocate_curve_from_keyframe_count(2);
			mts_redir_curve->keyframes[0].value = randf_range(0.0005f, 0.002f);
			mts_redir_curve->keyframes[1].value = randf_range(0.008f, 0.02f);

			new_car_properties->vel_redir = vel_redir_curve;
			new_car_properties->vel_redir_drift = vel_redir_curve;
			new_car_properties->vel_redir_quickturn = qt_redir_curve;
			new_car_properties->vel_redir_mts = mts_redir_curve;

			Curve* new_car_speed_curve = allocate_curve_from_keyframe_count(2);
			new_car_speed_curve->keyframes[0].value = randf_range(0.9f, 1.1f);
			new_car_speed_curve->keyframes[1].value = randf_range(0.7f, 0.9f);
			new_car_properties->vel_redir_mult_by_speed = new_car_speed_curve;

			cars[i].num_suspension_points = 4;

			PhysicsCarSuspensionPoint* suspension_points = allocate_array<PhysicsCarSuspensionPoint>(4);

			cars[i].suspension_points = suspension_points;

			suspension_points[0].target_length = 4.0f;
			suspension_points[0].max_length = 3.0f;
			suspension_points[0].spring_strength = 12.0f;
			suspension_points[0].origin_point = godot::Vector3(0.45f, 0.0f, 1.2f);
			suspension_points[0].target_dir = godot::Vector3(0.0f, -1.0f, 0.0f);

			suspension_points[1].target_length = 4.0f;
			suspension_points[1].max_length = 3.0f;
			suspension_points[1].spring_strength = 12.0f;
			suspension_points[1].origin_point = godot::Vector3(-0.45f, 0.0f, 1.2f);
			suspension_points[1].target_dir = godot::Vector3(0.0f, -1.0f, 0.0f);

			suspension_points[2].target_length = 4.0f;
			suspension_points[2].max_length = 3.0f;
			suspension_points[2].spring_strength = 12.0f;
			suspension_points[2].origin_point = godot::Vector3(1.0f, 0.0f, -1.5f);
			suspension_points[2].target_dir = godot::Vector3(0.0f, -1.0f, 0.0f);

			suspension_points[3].target_length = 4.0f;
			suspension_points[3].max_length = 3.0f;
			suspension_points[3].spring_strength = 12.0f;
			suspension_points[3].origin_point = godot::Vector3(-1.0f, 0.0f, -1.5f);
			suspension_points[3].target_dir = godot::Vector3(0.0f, -1.0f, 0.0f);

			PhysicsCarCollisionPoint* collision_points = allocate_array<PhysicsCarCollisionPoint>(4);

			cars[i].collision_points = collision_points;
			cars[i].num_collision_points = 4;

			collision_points[0].origin_point = godot::Vector3(0.9f, 0.75f, 1.2f);
			collision_points[1].origin_point = godot::Vector3(-1.0f, 0.75f, 1.2f);
			collision_points[2].origin_point = godot::Vector3(0.9f, 0.75f, -1.2f);
			collision_points[3].origin_point = godot::Vector3(-1.0f, 0.75f, -1.2f);

			cars[i].health = new_car_properties->max_health;

		}
		return cars;
	}
};