#pragma once

#include "godot_cpp/variant/vector3.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/classes/engine.hpp"

class CollisionCheckpoint
{
public:
	godot::Vector3 position_start;
	godot::Vector3 position_end;
	godot::Basis orientation_start;
	godot::Basis orientation_end;
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
	godot::Plane start_plane;
	godot::Plane end_plane;
	float distance;
	int road_segment;
	int num_neighboring_checkpoints;
	int* neighboring_checkpoints;
	bool contains(godot::Vector3 &in_pos){
		return start_plane.is_point_over(in_pos) && end_plane.is_point_over(in_pos);
	}
	void debug_draw()
	{
		godot::Object *dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
		godot::Vector3 p1 = position_start;
		godot::Vector3 p2 = position_end;
		godot::Basis b1 = orientation_start;
		godot::Basis b2 = orientation_end;
		float rx1 = x_radius_start;
		float rx2 = x_radius_end;
		float ry1 = y_radius_start;
		float ry2 = y_radius_end;
		dd3d->call("draw_line", p1, p2, godot::Color(1.0f, 1.0f, 1.0f), 0.01666666);
		dd3d->call("draw_line", p1 + b1[0] * rx1, p2 + b2[0] * rx2, godot::Color(1.0f, 1.0f, 1.0f), 0.01666666);
		dd3d->call("draw_line", p1 + b1[1] * ry1, p2 + b2[1] * ry2, godot::Color(1.0f, 1.0f, 1.0f), 0.01666666);
		dd3d->call("draw_line", p1 + b1[0] * -rx1, p2 + b2[0] * -rx2, godot::Color(1.0f, 1.0f, 1.0f), 0.01666666);
		dd3d->call("draw_line", p1 + b1[1] * -ry1, p2 + b2[1] * -ry2, godot::Color(1.0f, 1.0f, 1.0f), 0.01666666);

		dd3d->call("draw_arrow", p1, p1 + b1[0] * 6, godot::Color(1.0f, 0.5f, 0.5f), 0.25, true, 0.01666666);
		dd3d->call("draw_arrow", p1, p1 + b1[1] * 6, godot::Color(0.5f, 1.0f, 0.5f), 0.25, true, 0.01666666);
		dd3d->call("draw_arrow", p1, p1 + b1[2] * 6, godot::Color(0.5f, 0.5f, 1.0f), 0.25, true, 0.01666666);

		dd3d->call("draw_arrow", p2, p2 + b2[0] * 12, godot::Color(0.5f, 0.0f, 0.0f), 0.125, true, 0.01666666);
		dd3d->call("draw_arrow", p2, p2 + b2[1] * 12, godot::Color(0.0f, 0.5f, 0.0f), 0.125, true, 0.01666666);
		dd3d->call("draw_arrow", p2, p2 + b2[2] * 12, godot::Color(0.0f, 0.0f, 0.5f), 0.125, true, 0.01666666);

		dd3d->call("draw_arrow", p1, p1 + start_plane.normal * 4, godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, 0.01666666);
		dd3d->call("draw_arrow", p2, p2 + end_plane.normal * 4, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, 0.01666666);
		dd3d->call("draw_plane", start_plane, godot::Color(0.f, 1.f, 0.f, 0.05f), p1, 0.016666f);
		dd3d->call("draw_plane", end_plane, godot::Color(1.f, 0.f, 0.f, 0.05f), p2, 0.016666f);
	}
};