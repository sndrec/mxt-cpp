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
        TrackSegment* segments;
        CollisionCheckpoint* checkpoints;
        int find_checkpoint_recursive(const godot::Vector3 &pos, int cp_index, int iterations = 0) const;
        void cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = true);
        void cast_vs_track_fast(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = false);
        void get_road_surface(int cp_idx, const godot::Vector3 &point, godot::Vector2 &road_t, godot::Vector3 &spatial_t, godot::Transform3D &out_transform, bool oriented = true);
        std::vector<int> get_viable_checkpoints(godot::Vector3 in_point)
        {
                std::vector<int> return_checkpoints;
		return_checkpoints.reserve(16);

		// todo: implement a broad phase that can quickly cut out large amounts of checkpoints
		// which do not need testing; BVH, perhaps? at very minimum we can generate an AABB
		// for each track segment and compare against those AABBs first, and ignore
		// checkpoints for segments we can't possibly be interacting with

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
				return_checkpoints.push_back(i);
			}
		}
		return return_checkpoints;
	}
	int get_best_checkpoint(godot::Vector3 in_point) const
        {
                std::vector<int> candidates;
		candidates.reserve(16);

		// todo: implement a broad phase that can quickly cut out large amounts of checkpoints
		// which do not need testing; BVH, perhaps? at very minimum we can generate an AABB
		// for each track segment and compare against those AABBs first, and ignore
		// checkpoints for segments we can't possibly be interacting with

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
			candidates.push_back(i);
		}
		int   best_cp     = -1;
		float best_dist2  = std::numeric_limits<float>::infinity();
		for (int idx : candidates) {
			const CollisionCheckpoint &cp = checkpoints[idx];

			// project pos onto segment
			godot::Vector3 p1    = cp.start_plane.project(in_point);
			godot::Vector3 p2    = cp.end_plane.project(in_point);
			float           cp_t = get_closest_t_on_segment(in_point, p1, p2);

			// interpolate orientation
			godot::Basis basis;
			basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t).normalized();
			basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t).normalized();
			basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t).normalized();

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
};