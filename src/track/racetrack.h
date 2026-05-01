#pragma once

#include "track/track_segment.h"
#include "track/collision_checkpoint.h"
#include "track/trigger_collider.h"
#include "mxt_core/math_utils.h"
#include <vector>
#include <cstdint>

struct CollisionData;

struct alignas(64) TrackQueryScratch
{
	static constexpr int MAX_TRIGGER_EVENTS = 4096;
	struct TriggerEvent
	{
		int car_index;
		int trigger_index;
		uint8_t collision_flags;
		uint8_t trigger_type;
	};

	int candidate_checkpoints[8];
	int checkpoint_stack[64];
	int visited_checkpoints[64];
	int trigger_event_count = 0;
	TriggerEvent trigger_events[MAX_TRIGGER_EVENTS];

	void reset_trigger_events()
	{
		trigger_event_count = 0;
	}

	void push_trigger_event(int car_index, int trigger_index, uint8_t collision_flags, uint8_t trigger_type)
	{
		if (trigger_event_count >= MAX_TRIGGER_EVENTS) {
			return;
		}
		trigger_events[trigger_event_count++] = {car_index, trigger_index, collision_flags, trigger_type};
	}
};

class RaceTrack
{
public:
	int num_segments;
	int num_checkpoints;
	float minimum_y;
	TrackSegment* segments;
		CollisionCheckpoint* checkpoints;
		int num_trigger_colliders;
		TriggerCollider** trigger_colliders;
		float lap_length;
		int canonical_start_index;
		std::vector<uint8_t> canonical_flags;
		std::vector<int> canonical_next;
		std::vector<int> canonical_prev;
		struct BranchInfo {
			int entry;
			int exit;
			std::vector<int> checkpoints;
		};
		std::vector<BranchInfo> branch_infos;
		std::vector<int> checkpoint_branch_id;
		void compute_checkpoint_distances();
		void collect_branch_sequence(int cp_idx, std::vector<int> &out_indices) const;
		int find_checkpoint_recursive(const SimVec3 &pos, int cp_index, TrackQueryScratch &scratch, int iterations = 0);
	void cast_vs_track_fast(CollisionData &out_collision, const SimVec3 &p0, const SimVec3 &p1, uint8_t mask, int start_idx = -1, bool oriented = false);
	void get_road_surface(int cp_idx, const SimVec3 &point, SimVec2 &road_t, SimVec3 &spatial_t, SimTransform &out_transform, bool oriented = true);
	void get_road_surface4_same_checkpoint(int cp_idx, const SimVec3 point[4], SimVec2 road_t[4], SimVec3 spatial_t[4], SimTransform out_transform[4]);
	void convert_point_to_road(
		int cp_idx,
		const SimVec3 &point,
		SimVec2 &road_t,
		SimVec3 &spatial_t,
		float *out_cp_t = nullptr,
		RoadTransform *out_root = nullptr,
		RoadTransform *out_root_derivative = nullptr);
	int get_best_checkpoint(SimVec3 in_point, TrackQueryScratch &scratch)
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
				scratch.candidate_checkpoints[num_valid] = i;
				num_valid += 1;
				if (num_valid == 8)
				{
					break;
				}
			}
		}
		if (num_valid == 0)
		{
			return -1;
		}
		int   best_cp     = -1;
		float best_dist2  = std::numeric_limits<float>::infinity();
		for (int i = 0; i < num_valid; i++) {
			int idx = scratch.candidate_checkpoints[i];
			const CollisionCheckpoint &cp = checkpoints[idx];

			// project pos onto segment
			SimVec3 p1    = cp.start_plane.project(in_point);
			SimVec3 p2    = cp.end_plane.project(in_point);
			float           cp_t = get_closest_t_on_segment(in_point, p1, p2);

			// interpolate orientation
			SimBasis basis;
			basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);

			SimVec3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
			SimPlane    sep_x(basis[0], midpoint);
			SimPlane    sep_y(basis[1], midpoint);

			float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);

			float tx = sep_x.distance_to(in_point) * x_r;
			float ty = sep_y.distance_to(in_point) * x_r; // not a bug, we use x_r on purpose and for good reason - trust.
			float dist2 = tx * tx + ty * ty;

			if (dist2 < best_dist2) {
				best_dist2 = dist2;
				best_cp    = idx;
			}
		}
		return best_cp;
	}
	int get_best_checkpoint(SimVec3 in_point, int start_idx, TrackQueryScratch &scratch)
	{
		if (start_idx < 0 || start_idx >= num_checkpoints)
			return get_best_checkpoint(in_point, scratch);

		int visited_count = 0;
		int stack_top = 0;
		int num_valid = 0;

		scratch.checkpoint_stack[stack_top++] = start_idx;

		int check_count = 0;

		while (stack_top > 0) {
			int idx = scratch.checkpoint_stack[--stack_top];
			bool visited = false;
			for (int i = 0; i < visited_count; ++i) {
				if (scratch.visited_checkpoints[i] == idx) {
					visited = true;
					break;
				}
			}
			if (visited)
				continue;
			if (visited_count < 64) {
				scratch.visited_checkpoints[visited_count++] = idx;
			}

			check_count++;
			if (check_count > 16)
				break;

			CollisionCheckpoint &cp = checkpoints[idx];
			bool passed_start = cp.start_plane.is_point_over(in_point);
			bool passed_end   = cp.end_plane.is_point_over(in_point);

			// Valid if over start and under end plane
			if (passed_start && !passed_end) {
				scratch.candidate_checkpoints[num_valid++] = idx;
			}
			if (num_valid == 8)
				break;

			for (int i = 0; i < cp.num_neighboring_checkpoints; i++) {
				int neighbor = cp.neighboring_checkpoints[i];
				if (neighbor < 0 || neighbor >= num_checkpoints)
					continue;
				bool neighbor_visited = false;
				for (int visited_i = 0; visited_i < visited_count; ++visited_i) {
					if (scratch.visited_checkpoints[visited_i] == neighbor) {
						neighbor_visited = true;
						break;
					}
				}
				if (neighbor_visited)
					continue;

				// Prune based on checkpoint ordering and spatial relation
				bool wraps_forward  = (idx == num_checkpoints - 1 && neighbor == 0);
				bool wraps_backward = (idx == 0 && neighbor == num_checkpoints - 1);
				if (idx < neighbor && !passed_end && !wraps_backward && !wraps_forward)
						continue;
				if (idx > neighbor && passed_start && !wraps_backward && !wraps_forward)
					continue;

				if (stack_top < 64) {
					scratch.checkpoint_stack[stack_top++] = neighbor;
				}
			}
		}

		if (num_valid == 0)
			return -1;

		if (num_valid == 1)
			return scratch.candidate_checkpoints[0];

		int best_cp = -1;
		float best_dist2 = std::numeric_limits<float>::infinity();

		for (int i = 0; i < num_valid; i++) {
			int idx = scratch.candidate_checkpoints[i];
			const CollisionCheckpoint &cp = checkpoints[idx];

			SimVec3 p1 = cp.start_plane.project(in_point);
			SimVec3 p2 = cp.end_plane.project(in_point);
			float cp_t = get_closest_t_on_segment(in_point, p1, p2);

			SimBasis basis;
			basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);

			SimVec3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
			SimPlane sep_x(basis[0], midpoint);
			SimPlane sep_y(basis[1], midpoint);

			float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);

			float tx = sep_x.distance_to(in_point) * x_r;
			float ty = sep_y.distance_to(in_point) * x_r; // not a bug, we use x_r on purpose and for good reason - trust.
			float dist2 = tx * tx + ty * ty;

			if (dist2 < best_dist2) {
				best_dist2 = dist2;
				best_cp = idx;
			}
		}

		return best_cp;
	}

};
