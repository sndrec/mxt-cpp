#include "track/racetrack.h"
#include "car/physics_car.h" // for CollisionData and RoadData
#include <cfloat>
#include <algorithm>
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include <queue>
#include <vector>
#include <limits>
#include "mxt_core/debug.hpp"

static inline SimVec3 sim_vec3_from_godot(const SimVec3& v)
{
	return SimVec3(v.x, v.y, v.z);
}

static inline SimVec2 sim_vec2_from_godot(const SimVec2& v)
{
	return SimVec2(v.x, v.y);
}

static inline SimBasis sim_basis_from_godot(const SimBasis& b)
{
	return SimBasis(b[0].x, b[0].y, b[0].z, b[1].x, b[1].y, b[1].z, b[2].x, b[2].y, b[2].z);
}

static inline SimTransform sim_transform_from_godot(const SimTransform& t)
{
	return SimTransform(sim_basis_from_godot(t.basis), sim_vec3_from_godot(t.origin));
}

static inline godot::Vector3 godot_vec3_from_sim(const SimVec3& v)
{
	return godot::Vector3(v.x, v.y, v.z);
}

void RaceTrack::compute_checkpoint_distances()
{
	lap_length = 0.0f;
	if (num_checkpoints <= 0)
	{
		canonical_flags.clear();
		canonical_next.clear();
		canonical_prev.clear();
		branch_infos.clear();
		checkpoint_branch_id.clear();
		canonical_start_index = -1;
		return;
	}

	canonical_start_index = 0;
	canonical_flags.assign(num_checkpoints, 0);
	canonical_next.assign(num_checkpoints, -1);
	canonical_prev.assign(num_checkpoints, -1);
	branch_infos.clear();
	checkpoint_branch_id.assign(num_checkpoints, -1);

	struct Node
	{
		float distance;
		int index;
	};

	auto cmp = [](const Node &a, const Node &b) {
		return a.distance > b.distance;
	};

	std::priority_queue<Node, std::vector<Node>, decltype(cmp)> queue(cmp);
	const int start_index = 0;
	const int final_index = num_checkpoints - 1;
	std::vector<float> best_distance(num_checkpoints, std::numeric_limits<float>::infinity());
	std::vector<int> predecessor(num_checkpoints, -1);
	for (int i = 0; i < num_checkpoints; ++i)
	{
		checkpoints[i].distance = 0.0f;
	}
	best_distance[start_index] = 0.0f;
	queue.push({0.0f, start_index});

	while (!queue.empty())
	{
		Node node = queue.top();
		queue.pop();
		if (node.distance > best_distance[node.index])
		{
			continue;
		}
		if (node.index == final_index)
		{
			break;
		}
		const CollisionCheckpoint &cp = checkpoints[node.index];
		float base_distance = node.distance + cp.local_distance;
		for (int i = 0; i < cp.num_neighboring_checkpoints; ++i)
		{
			int neighbor = cp.neighboring_checkpoints[i];
			if (neighbor < 0 || neighbor >= num_checkpoints)
			{
				continue;
			}
			if (neighbor <= node.index)
			{
				continue;
			}
			if (base_distance + 1e-5f < best_distance[neighbor])
			{
				best_distance[neighbor] = base_distance;
				predecessor[neighbor] = node.index;
				queue.push({base_distance, neighbor});
			}
		}
	}

	auto accumulate_linear = [&]() {
		float accum = 0.0f;
		for (int i = 0; i < num_checkpoints; ++i)
		{
			accum += checkpoints[i].local_distance;
			checkpoints[i].distance = accum;
		}
		lap_length = accum;
	};

	if (best_distance[final_index] == std::numeric_limits<float>::infinity())
	{
		canonical_flags.clear();
		canonical_next.clear();
		canonical_prev.clear();
		branch_infos.clear();
		checkpoint_branch_id.clear();
		canonical_start_index = -1;
		accumulate_linear();
		return;
	}

	std::vector<int> canonical_path;
	canonical_path.push_back(final_index);
	int cursor = final_index;
	while (cursor != start_index)
	{
		cursor = predecessor[cursor];
		if (cursor == -1)
		{
			accumulate_linear();
			return;
		}
		canonical_path.push_back(cursor);
	}
	std::reverse(canonical_path.begin(), canonical_path.end());

	std::vector<char> is_canonical(num_checkpoints, 0);
	std::vector<char> assigned(num_checkpoints, 0);

	float cumulative = 0.0f;
	for (size_t i = 0; i < canonical_path.size(); ++i)
	{
		int idx = canonical_path[i];
		is_canonical[idx] = 1;
		assigned[idx] = 1;
		cumulative += checkpoints[idx].local_distance;
		checkpoints[idx].distance = cumulative;
		int next = (i + 1 < canonical_path.size()) ? canonical_path[i + 1] : start_index;
		canonical_next[idx] = next;
		canonical_prev[next] = idx;
	}
	lap_length = cumulative;

	auto canonical_interval = [&](int entry, int exit) {
		float acc = 0.0f;
		int walker = entry;
		while (true)
		{
			walker = canonical_next[walker];
			if (walker == -1 || walker == entry)
			{
				return -1.0f;
			}
			if (walker == exit)
			{
				return acc;
			}
			acc += checkpoints[walker].local_distance;
		}
	};

	for (int idx : canonical_path)
	{
		int prev = canonical_prev[idx];
		const CollisionCheckpoint &cp = checkpoints[idx];
		for (int i = 0; i < cp.num_neighboring_checkpoints; ++i)
		{
			int neighbor = cp.neighboring_checkpoints[i];
			if (neighbor < 0 || neighbor >= num_checkpoints)
			{
				continue;
			}
			if (neighbor == canonical_next[idx] || neighbor == prev || neighbor <= idx)
			{
				continue;
			}
			if (assigned[neighbor])
			{
				continue;
			}

		std::vector<int> branch_nodes;
		float branch_total = 0.0f;
		int exit_node = -1;
		int current = neighbor;
		int previous = idx;
		std::vector<char> branch_seen(num_checkpoints, 0);

			while (true)
			{
				if (current < 0 || current >= num_checkpoints)
				{
					exit_node = -1;
					break;
				}
				if (branch_seen[current])
				{
					exit_node = -1;
					break;
				}
				branch_seen[current] = 1;
				if (is_canonical[current])
				{
					exit_node = current;
					break;
				}
				branch_nodes.push_back(current);
				branch_total += checkpoints[current].local_distance;

				const CollisionCheckpoint &branch_cp = checkpoints[current];
				int next = -1;
				for (int j = 0; j < branch_cp.num_neighboring_checkpoints; ++j)
				{
					int nb = branch_cp.neighboring_checkpoints[j];
					if (nb == previous)
					{
						continue;
					}
					if (nb > current)
					{
						if (next == -1)
						{
							next = nb;
						}
						else
						{
							next = -1;
							break;
						}
					}
				}
				if (next == -1)
				{
					exit_node = -1;
					break;
				}
				previous = current;
				current = next;
			}

		float entry_progress = checkpoints[idx].distance;
		float exit_progress = entry_progress + branch_total;
		if (exit_node >= 0)
		{
			float interval = canonical_interval(idx, exit_node);
			if (interval >= 0.0f)
			{
				exit_progress = entry_progress + interval;
			}
		}

			float cumulative_branch = 0.0f;
			for (int branch_idx : branch_nodes)
			{
				cumulative_branch += checkpoints[branch_idx].local_distance;
				float mapped;
				if (branch_total > 0.0f)
				{
					float t = cumulative_branch / branch_total;
					mapped = entry_progress + (exit_progress - entry_progress) * t;
				}
				else
				{
					mapped = exit_progress;
				}
				checkpoints[branch_idx].distance = mapped;
				assigned[branch_idx] = 1;
			}
			if (!branch_nodes.empty())
			{
				checkpoints[branch_nodes.back()].distance = exit_progress;
			}
		if (!branch_nodes.empty())
		{
			BranchInfo info;
			info.entry = idx;
			info.exit = exit_node;
			info.checkpoints = branch_nodes;
			int branch_index = static_cast<int>(branch_infos.size());
			branch_infos.push_back(info);
			for (int branch_idx : branch_nodes)
			{
				checkpoint_branch_id[branch_idx] = branch_index;
			}
		}
		}
	}

	for (int i = 0; i < num_checkpoints; ++i)
	{
		if (!assigned[i])
		{
			float bd = best_distance[i];
			checkpoints[i].distance = (bd == std::numeric_limits<float>::infinity()) ? 0.0f : bd;
		}
	}
}

void RaceTrack::collect_branch_sequence(int cp_idx, std::vector<int> &out_indices) const
{
    out_indices.clear();
    if (cp_idx < 0 || cp_idx >= num_checkpoints)
        return;

    if (num_checkpoints == 0)
        return;

    const float eps = 1e-4f;
    const float full = lap_length > 0.0f ? lap_length : 0.0f;

    auto delta_forward = [&](int a, int b) -> float {
        float d = checkpoints[b].distance - checkpoints[a].distance;
        if (full > 0.0f) {
            if (d > 0.5f * full) d -= full;
            else if (d < -0.5f * full) d += full;
        }
        return d;
    };

    auto count_dir = [&](int idx, bool forward) -> int {
        const CollisionCheckpoint &cp = checkpoints[idx];
        int c = 0;
        for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
            int nb = cp.neighboring_checkpoints[i];
            if (nb < 0 || nb >= num_checkpoints) continue;
            float d = delta_forward(idx, nb);
            if (forward) {
                if (d > eps) c++;
            } else {
                if (d < -eps) c++;
            }
        }
        return c;
    };

    auto step_dir = [&](int idx, bool forward) -> int {
        const CollisionCheckpoint &cp = checkpoints[idx];
        int next = -1;
        for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
            int nb = cp.neighboring_checkpoints[i];
            if (nb < 0 || nb >= num_checkpoints) continue;
            float d = delta_forward(idx, nb);
            if (forward) {
                if (d > eps) {
                    if (next == -1) next = nb; else return -2; // junction (>=2)
                }
            } else {
                if (d < -eps) {
                    if (next == -1) next = nb; else return -2; // junction (>=2)
                }
            }
        }
        return next;
    };

    std::vector<int> back_seq;
    {
        int cur = cp_idx;
        int guard = 0;
        while (guard++ < num_checkpoints) {
            int nxt = step_dir(cur, false);
            if (nxt == -2) break;
            if (nxt < 0) break;
            back_seq.push_back(nxt);
            cur = nxt;
        }
    }

    std::vector<int> fwd_seq;
    {
        int cur = cp_idx;
        int guard = 0;
        while (guard++ < num_checkpoints) {
            int nxt = step_dir(cur, true);
            if (nxt == -2) break;
            if (nxt < 0) break;
            fwd_seq.push_back(nxt);
            cur = nxt;
        }
    }

    for (int i = static_cast<int>(back_seq.size()) - 1; i >= 0; --i)
        out_indices.push_back(back_seq[i]);
    out_indices.push_back(cp_idx);
    for (int idx : fwd_seq)
        out_indices.push_back(idx);

    if (out_indices.empty()) {
        for (int i = 0; i < num_checkpoints; ++i)
            out_indices.push_back(i);
    }
}

int RaceTrack::find_checkpoint_recursive(const SimVec3 &pos, int cp_index, TrackQueryScratch &scratch, int iterations)
{
	if (cp_index == -1)
	{
		return get_best_checkpoint(pos, scratch);
	}
	if (iterations > 10)
		return -1;
	const CollisionCheckpoint &cp = checkpoints[cp_index];
	if (!cp.end_plane.is_point_over(pos) && cp.start_plane.is_point_over(pos))
		return cp_index;
	for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
		int neighbor = cp.neighboring_checkpoints[i];
		int found = find_checkpoint_recursive(pos, neighbor, scratch, iterations + 1);
		if (found != -1)
			return found;
	}
	return -1;
}

void RaceTrack::get_road_surface(int cp_idx, const SimVec3 &point,
								  SimVec2 &road_t, SimVec3 &spatial_t, SimTransform &out_transform, bool oriented)
{
	if (cp_idx == -1)
	{
		road_t.x = -1000.0f;
		return;
	}
	CollisionCheckpoint *cp = &checkpoints[cp_idx];
	//if (cp->end_plane.is_point_over(point) || !cp->start_plane.is_point_over(point))
	//{
	//	int new_idx = get_best_checkpoint(point, cp_idx);
	//	if (new_idx != -1)
	//	{
	//		cp_idx = new_idx;
	//		cp = &checkpoints[cp_idx];
	//	}
	//}
	SimVec3 p1 = cp->start_plane.project(point);
	SimVec3 p2 = cp->end_plane.project(point);
	float cp_t = get_closest_t_on_segment(point, p1, p2);
	cp_t = std::clamp(cp_t, 0.0f, 1.0f);
	SimBasis basis;
	basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t);
	basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t);
	basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t);
	SimVec3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
	SimPlane sep_x_plane(basis[0], midpoint);
	SimPlane sep_y_plane(basis[1], midpoint);
	float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
	float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);
	float tx = sep_x_plane.distance_to(point) * x_r;
	float ty = sep_y_plane.distance_to(point) * y_r;
	float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);
	spatial_t = SimVec3(tx, ty, tz);

	bool y_less_than_x = y_r > 0.2f;
	bool is_open = false;
	bool use_top_half = false;

	// Check for open road shape
        RoadShape *shape = segments[cp->road_segment].road_shape;
        if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
                shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN) {
                is_open = true;
                use_top_half = true;
        } else if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN) {
                is_open = true;
                use_top_half = false;
        }
	if (is_open && y_less_than_x) {
		if (tx > -1.0001 && tx < 1.0001)
		{
			float openness = shape->openness->sample(tz);
			if (openness <= 0.50001f) {
				float tx_clamped = std::clamp(tx, -0.99f, 0.99f);
				float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
				if (!use_top_half)
					y_val = -y_val;
				spatial_t.y = y_val;
			}
		}
		else
		{
			road_t.x = -1000.0;
			return;
		}
	}
	segments[cp->road_segment].road_shape->find_t_from_relative_pos(road_t, spatial_t);
	segments[cp->road_segment].road_shape->get_oriented_transform_at_time(out_transform, road_t);
}

void RaceTrack::get_road_surface4_same_checkpoint(
	int cp_idx,
	const SimVec3 point[4],
	SimVec2 road_t[4],
	SimVec3 spatial_t[4],
	SimTransform out_transform[4])
{
	if (cp_idx == -1) {
		for (int lane = 0; lane < 4; ++lane) {
			road_t[lane].x = -1000.0f;
			spatial_t[lane] = SimVec3();
			out_transform[lane] = SimTransform();
		}
		return;
	}

	const CollisionCheckpoint *cp = &checkpoints[cp_idx];
	const TrackSegment &segment = segments[cp->road_segment];
	RoadShape *shape = segment.road_shape;

	const SimFloat4 px(point[0].x, point[1].x, point[2].x, point[3].x);
	const SimFloat4 py(point[0].y, point[1].y, point[2].y, point[3].y);
	const SimFloat4 pz(point[0].z, point[1].z, point[2].z, point[3].z);

	auto plane_project4 = [&](const SimPlane& plane, SimFloat4& ox, SimFloat4& oy, SimFloat4& oz) {
		const SimFloat4 dist =
			SimFloat4(plane.normal.x) * px +
			SimFloat4(plane.normal.y) * py +
			SimFloat4(plane.normal.z) * pz -
			SimFloat4(plane.d);
		ox = px - SimFloat4(plane.normal.x) * dist;
		oy = py - SimFloat4(plane.normal.y) * dist;
		oz = pz - SimFloat4(plane.normal.z) * dist;
	};

	SimFloat4 p1x, p1y, p1z;
	SimFloat4 p2x, p2y, p2z;
	plane_project4(cp->start_plane, p1x, p1y, p1z);
	plane_project4(cp->end_plane, p2x, p2y, p2z);

	const SimFloat4 sx = p2x - p1x;
	const SimFloat4 sy = p2y - p1y;
	const SimFloat4 sz = p2z - p1z;
	const SimFloat4 qx = px - p1x;
	const SimFloat4 qy = py - p1y;
	const SimFloat4 qz = pz - p1z;
	const SimFloat4 l2 = sim_max4(sx * sx + sy * sy + sz * sz, SimFloat4(1e-20f));
	SimFloat4 cp_t = (sx * qx + sy * qy + sz * qz) / l2;
	cp_t = sim_max4(SimFloat4(0.0f), sim_min4(cp_t, SimFloat4(1.0f)));

	auto lerp4 = [&](float a, float b) {
		return SimFloat4(a) + (SimFloat4(b) - SimFloat4(a)) * cp_t;
	};
	const SimFloat4 b0x = lerp4(cp->orientation_start[0].x, cp->orientation_end[0].x);
	const SimFloat4 b0y = lerp4(cp->orientation_start[0].y, cp->orientation_end[0].y);
	const SimFloat4 b0z = lerp4(cp->orientation_start[0].z, cp->orientation_end[0].z);
	const SimFloat4 b1x = lerp4(cp->orientation_start[1].x, cp->orientation_end[1].x);
	const SimFloat4 b1y = lerp4(cp->orientation_start[1].y, cp->orientation_end[1].y);
	const SimFloat4 b1z = lerp4(cp->orientation_start[1].z, cp->orientation_end[1].z);
	const SimFloat4 mx = lerp4(cp->position_start.x, cp->position_end.x);
	const SimFloat4 my = lerp4(cp->position_start.y, cp->position_end.y);
	const SimFloat4 mz = lerp4(cp->position_start.z, cp->position_end.z);
	const SimFloat4 x_r = lerp4(cp->x_radius_start_inv, cp->x_radius_end_inv);
	const SimFloat4 y_r = lerp4(cp->y_radius_start_inv, cp->y_radius_end_inv);
	const SimFloat4 tx = ((px - mx) * b0x + (py - my) * b0y + (pz - mz) * b0z) * x_r;
	SimFloat4 ty = ((px - mx) * b1x + (py - my) * b1y + (pz - mz) * b1z) * y_r;
	const SimFloat4 tz = SimFloat4(cp->t_start) + (SimFloat4(cp->t_end) - SimFloat4(cp->t_start)) * cp_t;

	float tx_s[4], ty_s[4], tz_s[4], yr_s[4];
	sim_store4(tx_s, tx);
	sim_store4(ty_s, ty);
	sim_store4(tz_s, tz);
	sim_store4(yr_s, y_r);

	const bool shape_open_top =
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
	const bool shape_open_bottom = shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
	const bool is_open = shape_open_top || shape_open_bottom;

	bool invalid_lane[4] = { false, false, false, false };
	for (int lane = 0; lane < 4; ++lane) {
		spatial_t[lane] = SimVec3(tx_s[lane], ty_s[lane], tz_s[lane]);
		const bool y_less_than_x = yr_s[lane] > 0.2f;
		if (is_open && y_less_than_x) {
			if (tx_s[lane] > -1.0001f && tx_s[lane] < 1.0001f) {
				const float openness = shape->openness->sample(tz_s[lane]);
				if (openness <= 0.50001f) {
					const float tx_clamped = std::clamp(tx_s[lane], -0.99f, 0.99f);
					float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
					if (shape_open_bottom) {
						y_val = -y_val;
					}
					spatial_t[lane].y = y_val;
				}
			} else {
				invalid_lane[lane] = true;
				road_t[lane] = SimVec2();
				continue;
			}
		}
		shape->find_t_from_relative_pos(road_t[lane], spatial_t[lane]);
	}
	shape->get_oriented_transform_at_time4(out_transform, road_t);
	for (int lane = 0; lane < 4; ++lane) {
		if (invalid_lane[lane]) {
			road_t[lane].x = -1000.0f;
			out_transform[lane] = SimTransform();
		}
	}
}

static void convert_point_to_road(RaceTrack *track, int cp_idx, const SimVec3 &point,
								  SimVec2 &road_t, SimVec3 &spatial_t, float *out_cp_t = nullptr)
{
	if (cp_idx == -1)
	{
		return;
	}
	const CollisionCheckpoint *cp = &track->checkpoints[cp_idx];

	SimVec3 p1 = cp->start_plane.project(point);
	SimVec3 p2 = cp->end_plane.project(point);
	float cp_t = get_closest_t_on_segment(point, p1, p2);
	if (out_cp_t)
		*out_cp_t = cp_t;

       SimBasis basis;
       basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t);
       basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t);
       basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t);

	SimVec3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
	SimPlane sep_x_plane(basis[0], midpoint);
	SimPlane sep_y_plane(basis[1], midpoint);

	float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
	float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);

	float tx = sep_x_plane.distance_to(point) * x_r;
	float ty = sep_y_plane.distance_to(point) * y_r;
	float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);

	spatial_t = SimVec3(tx, ty, tz);

        RoadShape *shape = track->segments[cp->road_segment].road_shape;

        bool y_less_than_x = y_r > 0.2f;
        bool is_open = false;
        bool use_top_half = false;

        if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
                shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN) {
                is_open = true;
                use_top_half = true;
        } else if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN) {
                is_open = true;
                use_top_half = false;
        }
	if (is_open && y_less_than_x) {
		if (tx > -1.0001 && tx < 1.0001)
		{
			float openness = shape->openness->sample(tz);
			if (openness <= 0.50001f) {
				float tx_clamped = std::clamp(tx, -0.99f, 0.99f);
				float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
				if (!use_top_half)
					y_val = -y_val;
				spatial_t.y = y_val;
			}
		}
		else
		{
			road_t.x = -1000.0;
			return;
		}
	}

	shape->find_t_from_relative_pos(road_t, spatial_t);
}


void RaceTrack::convert_point_to_road(int cp_idx, const SimVec3 &point, SimVec2 &road_t, SimVec3 &spatial_t, float *out_cp_t)
{
	if (cp_idx == -1)
	{
		return;
	}
	const CollisionCheckpoint *cp = &checkpoints[cp_idx];

	SimVec3 p1 = cp->start_plane.project(point);
	SimVec3 p2 = cp->end_plane.project(point);
	float cp_t = get_closest_t_on_segment(point, p1, p2);
	if (out_cp_t)
		*out_cp_t = cp_t;

       SimBasis basis;
       basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t);
       basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t);
       basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t);

	SimVec3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
	SimPlane sep_x_plane(basis[0], midpoint);
	SimPlane sep_y_plane(basis[1], midpoint);

	float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
	float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);

	float tx = sep_x_plane.distance_to(point) * x_r;
	float ty = sep_y_plane.distance_to(point) * y_r;
	float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);

	spatial_t = SimVec3(tx, ty, tz);

	RoadShape *shape = segments[cp->road_segment].road_shape;

	bool y_less_than_x = y_r > 0.2f;
	bool is_open = false;
	bool use_top_half = false;

        if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
                shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN) {
                is_open = true;
                use_top_half = true;
        } else if (shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN) {
                is_open = true;
                use_top_half = false;
        }
	if (is_open && y_less_than_x) {
		if (tx > -1.0001 && tx < 1.0001)
		{
			float openness = shape->openness->sample(tz);
			if (openness <= 0.50001f) {
				float tx_clamped = std::clamp(tx, -0.99f, 0.99f);
				float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
				if (!use_top_half)
					y_val = -y_val;
				spatial_t.y = y_val;
			}
		}
		else
		{
			road_t.x = -1000.0;
			return;
		}
	}

	shape->find_t_from_relative_pos(road_t, spatial_t);
}

struct CastParams {
	RaceTrack *track;
	uint8_t mask;
};

// ──────────────────────────────────────────────────────────────────────────────
// Same helpers / includes / forward-decls as before …
// ──────────────────────────────────────────────────────────────────────────────

static void cast_segment_fast(const CastParams  &params,
	CollisionData                               &out_collision,
	SimVec3 const                        &p0,
	SimVec3 const                        &p1,
	int                                         use_idx,
	SimVec3 const                        &sample_pt, // NEW
	bool                                        oriented    = true)
{
	out_collision.collided          = false;
	out_collision.road_data.cp_idx  = -1;
	if ((params.mask & CAST_FLAGS::WANTS_TRACK) == 0)
		return;

	if (use_idx == -1)
	{
		return;
	}

	RaceTrack *track                = params.track;
	const TrackSegment &segment     = track->segments[track->checkpoints[use_idx].road_segment];

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
		dd3d->call("draw_arrow", godot_vec3_from_sim(p0), godot_vec3_from_sim(p1), godot::Color(1.0f, 1.0f, 1.0f), 0.25, true, _TICK_DELTA);
	}

	SimVec2  road_t_sample_raw;  SimVec3 spatial_t_sample;
	convert_point_to_road(track, use_idx, sample_pt, road_t_sample_raw, spatial_t_sample);
	if (road_t_sample_raw.x == -1000.0)
	{
		return;
	}

	SimTransform surf;        // THE ONLY SURFACE FETCH
	//if (oriented)
	segment.road_shape->get_oriented_transform_at_time(surf, road_t_sample_raw);
	//else
		//segment.road_shape->get_transform_at_time(surf, road_t_sample_raw);

	const SimVec3 surf_n     = surf.basis[1];                    // Up/normal
	const SimVec3 surf_fwd   = surf.basis[2];                    // Forward (needed for rails)

	// ── 2) Basic plane hit against the single surface ────────────────────────
	const SimVec3 ray        = p1 - p0;
	float best_t                    = FLT_MAX;

	const float d0                  = (p0 - surf.origin).dot(surf_n);
	const float d1                  = (p1 - surf.origin).dot(surf_n);

	if (!((d0 <= 0.0f && d1 <= 0.0f) || (d0 >= 0.0f && d1 >= 0.0f))) {
		const float t = d0 / (d0 - d1);                                     // p0->p1 crossing %
		if (t >= 0.0f && t <= 1.0f) {
			const SimVec3 hit_point  = p0 + ray * t;

			SimVec2 road_t_hit_raw;  SimVec3 spatial_t_hit;
			convert_point_to_road(track, use_idx, hit_point, road_t_hit_raw, spatial_t_hit);
			if (road_t_hit_raw.x == -1000.0)
			{
				return;
			}

			if ((road_t_hit_raw.x <= 1.0f && road_t_hit_raw.x > -1.0f) && ((params.mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(surf_n) <= 0.0f)) {
				const float dist = t * ray.length();
				best_t                      = dist;
				out_collision.collided      = true;
				out_collision.collision_point   = sim_vec3_from_godot(hit_point);
				out_collision.collision_normal  = sim_vec3_from_godot(surf_n);

				out_collision.road_data.cp_idx          = use_idx;
				out_collision.road_data.spatial_t       = sim_vec3_from_godot(spatial_t_hit);
				out_collision.road_data.road_t          = sim_vec2_from_godot(road_t_hit_raw);
				out_collision.road_data.closest_surface = sim_transform_from_godot(surf);     // reuse single transform
				segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit_raw.y);

				out_collision.road_data.terrain = 0;
				if (params.mask & CAST_FLAGS::WANTS_TERRAIN) {
					for (int i = 0; i < segment.road_shape->num_embeds; ++i) {
						RoadEmbed *embed = &segment.road_shape->road_embeds[i];
						if (road_t_hit_raw.y > embed->start_offset && road_t_hit_raw.y < embed->end_offset) {
							const float l   = embed->left_border->sample(road_t_hit_raw.y);
							const float r   = embed->right_border->sample(road_t_hit_raw.y);
							//DEBUG::disp_text("embed_l", l);
							//DEBUG::disp_text("embed_r", r);
							//DEBUG::disp_text("road_t_x", road_t_hit_raw.x);
							if (road_t_hit_raw.x > l && road_t_hit_raw.x < r) {
								//DEBUG::disp_text("terrain", out_collision.road_data.terrain);
								out_collision.road_data.terrain = embed->embed_type;
								break;
							}
						}
					}
				}
			}
		}
	}

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		if (out_collision.collided){
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			dd3d->call("draw_arrow",
				godot_vec3_from_sim(out_collision.collision_point),
				godot_vec3_from_sim(out_collision.collision_point + out_collision.collision_normal * 2.0f),
				godot::Color(0.0f, 0.0f, 1.0f), 0.25, true, _TICK_DELTA);
		}
	}

	// ── 3) Optional rail cast (uses the SAME surface data) ───────────────────
	if ((params.mask & CAST_FLAGS::WANTS_RAIL) == 0)
		return;

        if (segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE ||
                segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER ||
                segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN ||
                segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
                segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT ||
                segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN)
                return;

	RoadTransform root_t;
	segment.curve_matrix->sample(root_t, road_t_sample_raw.y);
	const SimBasis rbasis       = root_t.t3d.basis;
	const SimVec3 up_normal = rbasis.get_column(1);
	const SimVec3 side_dir = rbasis.get_column(0);
	const SimVec3 side_scaled = side_dir * root_t.scale.x;
	const SimVec3 left_pos   = root_t.t3d.origin + side_scaled;
	const SimVec3 right_pos  = root_t.t3d.origin - side_scaled;
	const SimVec3 left_plane_n   = -side_dir;
	const SimVec3 right_plane_n  =  side_dir;

	struct RailSide { SimVec3 pos, plane_n, rail_n; float height; };
	const RailSide sides[2] = {
		{ left_pos,  left_plane_n,  -surf_n.cross(surf_fwd),    segment.left_rail_height    },
		{ right_pos, right_plane_n,  surf_n.cross(surf_fwd),    segment.right_rail_height   }
	};

	for (const RailSide &side : sides) {
		const float ra = (p0 - side.pos).dot(side.plane_n);
		const float rb = (p1 - side.pos).dot(side.plane_n);
		if ((ra <= 0.0f && rb <= 0.0f) || (ra >= 0.0f && rb >= 0.0f))
			continue;

		const float t = ra / (ra - rb);
		if (t < 0.0f || t > 1.0f)
			continue;

		const SimVec3 hit = p0 + ray * t;

		SimVec2 road_t_hit_raw;  SimVec3 spatial_t_hit;
		convert_point_to_road(track, use_idx, hit, road_t_hit_raw, spatial_t_hit);
		if (road_t_hit_raw.x == -1000.0)
		{
			continue;
		}

		const float vdist = (hit - surf.origin).dot(surf_n);        // height above track
		if (vdist < 0.0f || vdist > side.height * root_t.scale.y)
			continue;
		if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(side.rail_n) > 0.0f)
			continue;

		const float dist = t * ray.length();
		if (dist < best_t) {
			best_t                          = dist;
			out_collision.collided          = true;
			out_collision.collision_point   = sim_vec3_from_godot(hit);
			out_collision.collision_normal  = sim_vec3_from_godot(side.rail_n);

			out_collision.road_data.cp_idx          = use_idx;
			out_collision.road_data.spatial_t       = sim_vec3_from_godot(spatial_t_hit);
			out_collision.road_data.road_t          = sim_vec2_from_godot(road_t_hit_raw);
			out_collision.road_data.closest_surface = sim_transform_from_godot(surf); // reuse same transform
			segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit_raw.y);
			out_collision.road_data.terrain         = 0x100;
		}
	}

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		if (out_collision.collided && out_collision.road_data.terrain == 0x100){
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			dd3d->call("draw_arrow",
				godot_vec3_from_sim(out_collision.collision_point),
				godot_vec3_from_sim(out_collision.collision_point + out_collision.collision_normal * 2.0f),
				godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
		}
	}
}

void RaceTrack::cast_vs_track_fast(CollisionData &out_collision,
	SimVec3 const &p0,
	SimVec3 const &p1,
	uint8_t mask,
	int start_idx, bool oriented)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;

	if ((mask & CAST_FLAGS::WANTS_TRACK) == 0)
		return;

	if (start_idx == -1)
	{
		return;
	}

	// choose sample point
	SimVec3 sample_point;
	if (mask & CAST_FLAGS::SAMPLE_FROM_P0)
		sample_point = p0;
	else if (mask & CAST_FLAGS::SAMPLE_FROM_MID)
		sample_point = (p0 + p1) * 0.5f;
	else
		sample_point = p1;
	
	CastParams params{ this, mask };
	cast_segment_fast(params, out_collision, p0, p1, start_idx, sample_point, true);
}
