#pragma once

#include "track/track_segment.h"
#include "track/collision_checkpoint.h"
#include "mxt_core/math_utils.h"
#include <vector>

struct CollisionData;

class RaceTrack
{
public:
	int num_segments;
	int num_checkpoints;
	float minimum_y;
	TrackSegment* segments;
	int candidate_scratch[8];
	int candidate_use;
	bool* visited_checkpoints;
	int* checkpoint_stack;
	CollisionCheckpoint* checkpoints;
	int find_checkpoint_recursive(const godot::Vector3 &pos, int cp_index, int iterations = 0);
	void cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = true);
	void cast_vs_track_fast(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = false);
	void get_road_surface(int cp_idx, const godot::Vector3 &point, godot::Vector2 &road_t, godot::Vector3 &spatial_t, godot::Transform3D &out_transform, bool oriented = true);
	std::vector<int> get_viable_checkpoints(godot::Vector3 in_point)
	{
		int num_candidates = 0;
		for (int i = 0; i < num_checkpoints; i++)
		{
			if (!checkpoints[i].start_plane.is_point_over(in_point))
			{
				continue;
			}
			if (checkpoints[i].end_plane.is_point_over(in_point))
			{
				continue;
			}
			godot::Vector3 avg_pos = (checkpoints[i].position_start + checkpoints[i].position_end) * 0.5f;
			float max_x_radius = fmaxf(checkpoints[i].x_radius_start, checkpoints[i].x_radius_end);
			float max_y_radius = fmaxf(checkpoints[i].y_radius_start, checkpoints[i].y_radius_end);
			float max_total_radius = fmaxf(max_x_radius, max_y_radius);
			float radius = (checkpoints[i].position_end - checkpoints[i].position_start).length_squared() + max_total_radius * max_total_radius;
			if (in_point.distance_squared_to(avg_pos) < radius)
			{
				candidate_scratch[num_candidates] = i;
				num_candidates++;
			}
		}
	}
	int get_best_checkpoint(godot::Vector3 in_point)
	{
		int num_valid = 0;
		for (int seg = 0; seg < num_segments; seg++)
		{
			if (!segments[seg].bounds.has_point(in_point))
			{
				continue;
			}
			int start = segments[seg].checkpoint_start;
			int end   = start + segments[seg].checkpoint_run_length;
			for (int i = start; i < end; i++)
			{
				if (!checkpoints[i].start_plane.is_point_over(in_point))
				{
					continue;
				}
				if (checkpoints[i].end_plane.is_point_over(in_point))
				{
					continue;
				}
				candidate_scratch[num_valid] = i;
				num_valid += 1;
			}
		}
		if (num_valid == 0)
		{
			return -1;
		}
		int   best_cp     = -1;
		float best_dist2  = std::numeric_limits<float>::infinity();
		for (int i = 0; i < num_valid; i++) {
			int idx = candidate_scratch[i];
			const CollisionCheckpoint &cp = checkpoints[idx];

			// project pos onto segment
			godot::Vector3 p1    = cp.start_plane.project(in_point);
			godot::Vector3 p2    = cp.end_plane.project(in_point);
			float           cp_t = get_closest_t_on_segment(in_point, p1, p2);

			// interpolate orientation
			godot::Basis basis;
			basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);

			godot::Vector3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
			godot::Plane    sep_x(basis[0], midpoint);
			godot::Plane    sep_y(basis[1], midpoint);

			float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);

			float tx = sep_x.distance_to(in_point) * x_r;
			float ty = sep_y.distance_to(in_point) * x_r;
			float dist2 = tx * tx + ty * ty;

			if (dist2 < best_dist2) {
				best_dist2 = dist2;
				best_cp    = idx;
			}
		}
		return best_cp;
	}
	int get_best_checkpoint(godot::Vector3 in_point, int start_idx)
	{
		if (start_idx < 0 || start_idx >= num_checkpoints)
			return get_best_checkpoint(in_point);

		for (int i = 0; i < num_checkpoints; i++)
			visited_checkpoints[i] = false;

		int stack_top = 0;
		int num_valid = 0;

		checkpoint_stack[stack_top++] = start_idx;

		while (stack_top > 0) {
			int idx = checkpoint_stack[--stack_top];
			if (visited_checkpoints[idx])
				continue;
			visited_checkpoints[idx] = true;

			CollisionCheckpoint &cp = checkpoints[idx];
			bool passed_start = cp.start_plane.is_point_over(in_point);
			bool passed_end   = cp.end_plane.is_point_over(in_point);

			// Valid if over start and under end plane
			if (passed_start && !passed_end) {
				candidate_scratch[num_valid++] = idx;
			}

			for (int i = 0; i < cp.num_neighboring_checkpoints; i++) {
				int neighbor = cp.neighboring_checkpoints[i];
				if (neighbor < 0 || neighbor >= num_checkpoints)
					continue;
				if (visited_checkpoints[neighbor])
					continue;

				// Prune based on checkpoint ordering and spatial relation
				if (idx < neighbor && !passed_start)
					continue;
				if (idx > neighbor && passed_end)
					continue;

				checkpoint_stack[stack_top++] = neighbor;
			}
		}

		if (num_valid == 0)
			return -1;

		if (num_valid == 1)
			return candidate_scratch[0];

		int best_cp = -1;
		float best_dist2 = std::numeric_limits<float>::infinity();

		for (int i = 0; i < num_valid; i++) {
			int idx = candidate_scratch[i];
			const CollisionCheckpoint &cp = checkpoints[idx];

			godot::Vector3 p1 = cp.start_plane.project(in_point);
			godot::Vector3 p2 = cp.end_plane.project(in_point);
			float cp_t = get_closest_t_on_segment(in_point, p1, p2);

			godot::Basis basis;
			basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);

			godot::Vector3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
			godot::Plane sep_x(basis[0], midpoint);
			godot::Plane sep_y(basis[1], midpoint);

			float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);

			float tx = sep_x.distance_to(in_point) * x_r;
			float ty = sep_y.distance_to(in_point) * y_r;
			float dist2 = tx * tx + ty * ty;

			if (dist2 < best_dist2) {
				best_dist2 = dist2;
				best_cp = idx;
			}
		}

		return best_cp;
	}

};