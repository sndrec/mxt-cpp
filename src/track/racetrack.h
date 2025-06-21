#pragma once

#include "track/track_segment.h"
#include "track/collision_checkpoint.h"
#include "mxt_core/math_utils.h"
#include "godot_cpp/variant/aabb.hpp"
#include "mxt_core/heap_handler.h"
#include <vector>

struct CollisionData;

struct CheckpointVoxelCell {
	int count;
	int *indices;
};

struct CheckpointGrid {
	godot::AABB bounds;
	float voxel_size;
	int dim_x;
	int dim_y;
	int dim_z;
	CheckpointVoxelCell *cells;
};

class RaceTrack
{
public:
	int num_segments;
	int num_checkpoints;
	TrackSegment* segments;
	CollisionCheckpoint* checkpoints;
	godot::AABB bounds;
	CheckpointGrid checkpoint_grid;
	void build_checkpoint_grid(HeapHandler &alloc, float voxel_size = 100.0f);
	int find_checkpoint_recursive(const godot::Vector3 &pos, int cp_index, int iterations = 0) const;
	void cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = true);
	void cast_vs_track_fast(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx = -1, bool oriented = false);
	void get_road_surface(int cp_idx, const godot::Vector3 &point, godot::Vector2 &road_t, godot::Vector3 &spatial_t, godot::Transform3D &out_transform, bool oriented = true);
	std::vector<int> get_viable_checkpoints(godot::Vector3 in_point) const
	{
		std::vector<int> return_checkpoints;
		return_checkpoints.reserve(16);

		if (checkpoint_grid.cells) {
			int xi = int((in_point.x - checkpoint_grid.bounds.position.x) / checkpoint_grid.voxel_size);
			int yi = int((in_point.y - checkpoint_grid.bounds.position.y) / checkpoint_grid.voxel_size);
			int zi = int((in_point.z - checkpoint_grid.bounds.position.z) / checkpoint_grid.voxel_size);
			if (xi >= 0 && yi >= 0 && zi >= 0 && xi < checkpoint_grid.dim_x && yi < checkpoint_grid.dim_y && zi < checkpoint_grid.dim_z) {
				int cell_idx = xi + checkpoint_grid.dim_x * (yi + checkpoint_grid.dim_y * zi);
				const CheckpointVoxelCell &cell = checkpoint_grid.cells[cell_idx];
				for (int n = 0; n < cell.count; ++n) {
					int i = cell.indices[n];
					if (!checkpoints[i].start_plane.is_point_over(in_point))
						continue;
					if (checkpoints[i].end_plane.is_point_over(in_point))
						continue;
					return_checkpoints.push_back(i);
				}
				return return_checkpoints;
			}
		}

		for (int i = 0; i < num_checkpoints; i++) {
			if (!checkpoints[i].start_plane.is_point_over(in_point))
				continue;
			if (checkpoints[i].end_plane.is_point_over(in_point))
				continue;
			return_checkpoints.push_back(i);
		}

		return return_checkpoints;
	}
	int get_best_checkpoint(godot::Vector3 in_point) const
	{
		std::vector<int> candidates = get_viable_checkpoints(in_point);
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