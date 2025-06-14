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
        int find_checkpoint_bfs(const godot::Vector3 &pos, int start_index) const;
        void cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = 0);
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
};