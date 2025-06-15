#pragma once

#include "godot_cpp/variant/vector3.hpp"
#include "godot_cpp/classes/object.hpp"

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
};