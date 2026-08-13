#pragma once

#include "core/sim_math.h"
#include "godot_cpp/variant/vector3.hpp"
#include "godot_cpp/variant/color.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/engine.hpp"

class CollisionCheckpoint
{
public:
	SimVec3 position_start;
	SimVec3 position_end;
	SimBasis orientation_start;
	SimBasis orientation_end;
	float x_radius_start;
	float x_radius_end;
	float y_radius_start;
	float y_radius_end;
	float x_radius_start_inv;
	float x_radius_end_inv;
	float y_radius_start_inv;
	float y_radius_end_inv;
	float t_start;
	float t_end;
	SimPlane start_plane;
	SimPlane end_plane;
	float local_distance;
	float distance;
	int road_segment;
	int num_neighboring_checkpoints;
	int* neighboring_checkpoints;
	bool contains(SimVec3 &in_pos){
		return start_plane.is_point_over(in_pos) && end_plane.is_point_over(in_pos);
	}
	void debug_draw(float draw_time = 0.01666666f)
	{
		godot::Object *dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
		SimVec3 p1 = position_start;
		SimVec3 p2 = position_end;
		SimBasis b1 = orientation_start;
		SimBasis b2 = orientation_end;
		auto gd = [](const SimVec3& v) { return godot::Vector3(v.x, v.y, v.z); };
		float rx1 = x_radius_start;
		float rx2 = x_radius_end;
		float ry1 = y_radius_start;
		float ry2 = y_radius_end;
		dd3d->call("draw_line", gd(p1), gd(p2), godot::Color(1.0f, 1.0f, 1.0f), draw_time);
		dd3d->call("draw_line", gd(p1 + b1[0] * rx1), gd(p2 + b2[0] * rx2), godot::Color(1.0f, 1.0f, 1.0f), draw_time);
		dd3d->call("draw_line", gd(p1 + b1[1] * ry1), gd(p2 + b2[1] * ry2), godot::Color(1.0f, 1.0f, 1.0f), draw_time);
		dd3d->call("draw_line", gd(p1 + b1[0] * -rx1), gd(p2 + b2[0] * -rx2), godot::Color(1.0f, 1.0f, 1.0f), draw_time);
		dd3d->call("draw_line", gd(p1 + b1[1] * -ry1), gd(p2 + b2[1] * -ry2), godot::Color(1.0f, 1.0f, 1.0f), draw_time);

		dd3d->call("draw_arrow", gd(p1), gd(p1 + b1[0] * 6.0f), godot::Color(1.0f, 0.5f, 0.5f), 0.25, true, draw_time);
		dd3d->call("draw_arrow", gd(p1), gd(p1 + b1[1] * 6.0f), godot::Color(0.5f, 1.0f, 0.5f), 0.25, true, draw_time);
		dd3d->call("draw_arrow", gd(p1), gd(p1 + b1[2] * 6.0f), godot::Color(0.5f, 0.5f, 1.0f), 0.25, true, draw_time);

		dd3d->call("draw_arrow", gd(p2), gd(p2 + b2[0] * 12.0f), godot::Color(0.5f, 0.0f, 0.0f), 0.125, true, draw_time);
		dd3d->call("draw_arrow", gd(p2), gd(p2 + b2[1] * 12.0f), godot::Color(0.0f, 0.5f, 0.0f), 0.125, true, draw_time);
		dd3d->call("draw_arrow", gd(p2), gd(p2 + b2[2] * 12.0f), godot::Color(0.0f, 0.0f, 0.5f), 0.125, true, draw_time);

		dd3d->call("draw_arrow", gd(p1), gd(p1 + start_plane.normal * 4.0f), godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, draw_time);
		dd3d->call("draw_arrow", gd(p2), gd(p2 + end_plane.normal * 4.0f), godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, draw_time);
		//dd3d->call("draw_plane", start_plane, godot::Color(0.f, 1.f, 0.f, 0.05f), p1, 0.016666f);
		//dd3d->call("draw_plane", end_plane, godot::Color(1.f, 0.f, 0.f, 0.05f), p2, 0.016666f);
	}
};
