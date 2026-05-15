#include "track/racetrack.h"
#include "car/physics_car.h" // for CollisionData and RoadData
#include <cfloat>
#include <cstdlib>
#include <algorithm>
#include <chrono>
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

static bool mesh_debug_draw_current_car(const TrackQueryScratch *scratch)
{
	return scratch && scratch->debug_mesh_current_global_car_index == scratch->debug_mesh_draw_global_car_index;
}

static void draw_mesh_debug_triangle(const TrackMeshCollisionTriangle &tri, const godot::Color &color, float draw_time)
{
	const SimVec3 face = (tri.p1 - tri.p0).cross(tri.p2 - tri.p0);
	const float face_len2 = face.length_squared();
	const SimVec3 offset = face_len2 > 1.0e-8f ? face * (0.05f / sqrtf(face_len2)) : SimVec3();
	godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	dd3d->call("draw_line", godot_vec3_from_sim(tri.p0 + offset), godot_vec3_from_sim(tri.p1 + offset), color, draw_time);
	dd3d->call("draw_line", godot_vec3_from_sim(tri.p1 + offset), godot_vec3_from_sim(tri.p2 + offset), color, draw_time);
	dd3d->call("draw_line", godot_vec3_from_sim(tri.p2 + offset), godot_vec3_from_sim(tri.p0 + offset), color, draw_time);
}

static inline float safe_inverse_road_scale(float scale)
{
	return (fabsf(scale) > 1.0e-5f) ? (1.0f / scale) : 0.0f;
}

static void draw_nearest_rail_candidate(
	const TrackEdgeRailSide sides[2],
	const SimVec3& reference,
	float draw_time)
{
	if (!DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAIL_CANDIDATES)) {
		return;
	}
	int best_idx = 0;
	float best_dist2 = (reference - sides[0].pos).length_squared();
	const float dist2_1 = (reference - sides[1].pos).length_squared();
	if (dist2_1 < best_dist2) {
		best_idx = 1;
		best_dist2 = dist2_1;
	}
	const TrackEdgeRailSide &side = sides[best_idx];
	godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	dd3d->call("draw_arrow", godot_vec3_from_sim(side.pos), godot_vec3_from_sim(side.pos + side.rail_n * 8.0f), godot::Color(1.0f, 0.0f, 1.0f), 0.35, true, draw_time);
	dd3d->call("draw_arrow", godot_vec3_from_sim(side.pos), godot_vec3_from_sim(side.pos + side.up_n * 6.0f), godot::Color(0.2f, 1.0f, 0.2f), 0.2, true, draw_time);
	dd3d->call("draw_arrow", godot_vec3_from_sim(side.pos), godot_vec3_from_sim(side.pos + side.forward_n * 6.0f), godot::Color(0.2f, 0.5f, 1.0f), 0.2, true, draw_time);
}

static bool intersect_tunnel_roof_rail(
	const TrackEdgeRailSide sides[2],
	const RoadTransform &root,
	const SimVec3 &p0,
	const SimVec3 &ray,
	float *hit_t_out,
	SimVec3 *normal_out)
{
	const float left_height = sides[0].height * root.scale.y;
	const float right_height = sides[1].height * root.scale.y;
	if (left_height <= 0.0f || right_height <= 0.0f) {
		return false;
	}

	const SimVec3 left_top = sides[0].pos + sides[0].up_n * left_height;
	const SimVec3 right_top = sides[1].pos + sides[1].up_n * right_height;
	SimVec3 left_top_local = root.t3d.xform_inv(left_top);
	SimVec3 right_top_local = root.t3d.xform_inv(right_top);
	SimVec3 p0_local = root.t3d.xform_inv(p0);
	SimVec3 ray_local = root.t3d.basis.xform_inv(ray);
	left_top_local = SimVec3(
		left_top_local.x * safe_inverse_road_scale(root.scale.x),
		left_top_local.y * safe_inverse_road_scale(root.scale.y),
		left_top_local.z * safe_inverse_road_scale(root.scale.z));
	right_top_local = SimVec3(
		right_top_local.x * safe_inverse_road_scale(root.scale.x),
		right_top_local.y * safe_inverse_road_scale(root.scale.y),
		right_top_local.z * safe_inverse_road_scale(root.scale.z));
	p0_local = SimVec3(
		p0_local.x * safe_inverse_road_scale(root.scale.x),
		p0_local.y * safe_inverse_road_scale(root.scale.y),
		p0_local.z * safe_inverse_road_scale(root.scale.z));
	ray_local = SimVec3(
		ray_local.x * safe_inverse_road_scale(root.scale.x),
		ray_local.y * safe_inverse_road_scale(root.scale.y),
		ray_local.z * safe_inverse_road_scale(root.scale.z));
	const SimVec3 center = (left_top_local + right_top_local) * 0.5f;
	const SimVec3 side_axis = (left_top_local - right_top_local).normalized();
	const SimVec3 up_axis(0.0f, 1.0f, 0.0f);
	const float radius = left_top_local.distance_to(right_top_local) * 0.5f;
	if (radius <= 0.000001f || side_axis.length_squared() <= 0.0f || up_axis.length_squared() <= 0.0f) {
		return false;
	}

	const SimVec3 rel0 = p0_local - center;
	const float x0 = rel0.dot(side_axis) / radius;
	const float y0 = rel0.dot(up_axis) / radius;
	const float dx = ray_local.dot(side_axis) / radius;
	const float dy = ray_local.dot(up_axis) / radius;
	const float a = dx * dx + dy * dy;
	if (a <= 0.00000001f) {
		return false;
	}
	const float b = 2.0f * (x0 * dx + y0 * dy);
	const float c = x0 * x0 + y0 * y0 - 1.0f;
	const float disc = b * b - 4.0f * a * c;
	if (disc < 0.0f) {
		return false;
	}

	const float sqrt_disc = sqrtf(disc);
	const float inv_denom = 0.5f / a;
	float best_t = FLT_MAX;
	for (int i = 0; i < 2; ++i) {
		const float t = (-b + (i == 0 ? -sqrt_disc : sqrt_disc)) * inv_denom;
		if (t < 0.0f || t > 1.0f || t >= best_t) {
			continue;
		}
		const float y = y0 + dy * t;
		if (y < 0.0f) {
			continue;
		}
		const float x = x0 + dx * t;
		const SimVec3 radial = side_axis * x + up_axis * y;
		const SimVec3 normal_local = (-radial).normalized();
		if (normal_local.length_squared() <= 0.0f) {
			continue;
		}
		const SimVec3 normal = root.t3d.basis.xform(SimVec3(
			normal_local.x * safe_inverse_road_scale(root.scale.x),
			normal_local.y * safe_inverse_road_scale(root.scale.y),
			normal_local.z * safe_inverse_road_scale(root.scale.z))).normalized();
		if (normal.length_squared() <= 0.0f) {
			continue;
		}
		best_t = t;
		*normal_out = normal;
	}

	if (best_t == FLT_MAX) {
		return false;
	}
	*hit_t_out = best_t;
	return true;
}

static inline bool road_shape_opens_up(const RoadShape *shape)
{
	return shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
}

static inline bool road_shape_opens_down(const RoadShape *shape)
{
	return shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
}

static inline float checkpoint_time_for_point(
	const CollisionCheckpoint &cp,
	const SimVec3 &point,
	bool clamp_cp_t,
	float *out_cp_t)
{
	const SimVec3 p1 = cp.start_plane.project(point);
	const SimVec3 p2 = cp.end_plane.project(point);
	float cp_t = get_closest_t_on_segment(point, p1, p2);
	if (clamp_cp_t) {
		cp_t = std::clamp(cp_t, 0.0f, 1.0f);
	}
	if (out_cp_t) {
		*out_cp_t = cp_t;
	}
	return remap_float(cp_t, 0.0f, 1.0f, cp.t_start, cp.t_end);
}

static bool inverse_root_point_to_road(
	const TrackSegment &segment,
	const SimVec3 &point,
	float road_y,
	SimVec2 &road_t,
	SimVec3 &spatial_t,
	RoadTransform *root_out,
	RoadTransform *root_derivative_out)
{
	RoadTransform root;
	RoadTransform root_derivative;
	segment.curve_matrix->sample_with_derivative(root, root_derivative, road_y);

	const SimVec3 local = root.t3d.xform_inv(point);
	spatial_t = SimVec3(
		local.x * safe_inverse_road_scale(root.scale.x),
		local.y * safe_inverse_road_scale(root.scale.y),
		road_y);

	RoadShape *shape = segment.road_shape;
	const bool is_open = road_shape_opens_up(shape) || road_shape_opens_down(shape);
	if (is_open && fabsf(root.scale.y) < 5.0f) {
		if (spatial_t.x > -1.0001f && spatial_t.x < 1.0001f) {
			const float openness = shape->openness->sample(road_y);
			if (openness <= 0.50001f) {
				const float tx_clamped = std::clamp(spatial_t.x, -0.99f, 0.99f);
				float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
				if (road_shape_opens_down(shape)) {
					y_val = -y_val;
				}
				spatial_t.y = y_val;
			}
		} else {
			road_t = SimVec2(-1000.0f, road_y);
			return false;
		}
	}

	shape->find_t_from_relative_pos(road_t, spatial_t);
	if (root_out) {
		*root_out = root;
	}
	if (root_derivative_out) {
		*root_derivative_out = root_derivative;
	}
	return true;
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
	const TrackSegment &segment = segments[cp->road_segment];
	const float road_y = checkpoint_time_for_point(*cp, point, true, nullptr);
	RoadTransform root;
	RoadTransform root_derivative;
	if (!inverse_root_point_to_road(segment, point, road_y, road_t, spatial_t, &root, &root_derivative)) {
		return;
	}
	segment.road_shape->get_oriented_transform_at_time_presampled(out_transform, road_t, root, root_derivative);
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

	const SimFloat4 tz = SimFloat4(cp->t_start) + (SimFloat4(cp->t_end) - SimFloat4(cp->t_start)) * cp_t;

	float tz_s[4];
	sim_store4(tz_s, tz);

	const bool shape_open_top =
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
	const bool shape_open_bottom = shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
	const bool is_open = shape_open_top || shape_open_bottom;

	RoadTransform root[4];
	RoadTransform root_derivative[4];
	segment.curve_matrix->sample4_with_derivative(root, root_derivative, tz_s);

	bool invalid_lane[4] = { false, false, false, false };
	for (int lane = 0; lane < 4; ++lane) {
		const SimVec3 local = root[lane].t3d.xform_inv(point[lane]);
		spatial_t[lane] = SimVec3(
			local.x * safe_inverse_road_scale(root[lane].scale.x),
			local.y * safe_inverse_road_scale(root[lane].scale.y),
			tz_s[lane]);
		if (is_open && fabsf(root[lane].scale.y) < 5.0f) {
			if (spatial_t[lane].x > -1.0001f && spatial_t[lane].x < 1.0001f) {
				const float openness = shape->openness->sample(tz_s[lane]);
				if (openness <= 0.50001f) {
					const float tx_clamped = std::clamp(spatial_t[lane].x, -0.99f, 0.99f);
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
	for (int lane = 0; lane < 4; ++lane) {
		if (invalid_lane[lane]) {
			road_t[lane].x = -1000.0f;
			out_transform[lane] = SimTransform();
			continue;
		}
		shape->get_oriented_transform_at_time_presampled(out_transform[lane], road_t[lane], root[lane], root_derivative[lane]);
	}
}

static void convert_point_to_road(
	RaceTrack *track,
	int cp_idx,
	const SimVec3 &point,
	SimVec2 &road_t,
	SimVec3 &spatial_t,
	float *out_cp_t = nullptr,
	RoadTransform *out_root = nullptr,
	RoadTransform *out_root_derivative = nullptr)
{
	if (cp_idx == -1)
	{
		road_t.x = -1000.0f;
		return;
	}
	const CollisionCheckpoint *cp = &track->checkpoints[cp_idx];
	const TrackSegment &segment = track->segments[cp->road_segment];
	const float road_y = checkpoint_time_for_point(*cp, point, false, out_cp_t);
	inverse_root_point_to_road(segment, point, road_y, road_t, spatial_t, out_root, out_root_derivative);
}


void RaceTrack::convert_point_to_road(
	int cp_idx,
	const SimVec3 &point,
	SimVec2 &road_t,
	SimVec3 &spatial_t,
	float *out_cp_t,
	RoadTransform *out_root,
	RoadTransform *out_root_derivative)
{
	if (cp_idx == -1)
	{
		road_t.x = -1000.0f;
		return;
	}
	const CollisionCheckpoint *cp = &checkpoints[cp_idx];
	const TrackSegment &segment = segments[cp->road_segment];
	const float road_y = checkpoint_time_for_point(*cp, point, false, out_cp_t);
	inverse_root_point_to_road(segment, point, road_y, road_t, spatial_t, out_root, out_root_derivative);
}

struct CastParams {
	RaceTrack *track;
	uint8_t mask;
	bool smooth_mesh_hits = true;
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
	if (!segment.analytic_collision_enabled) {
		return;
	}

	if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
		godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
		dd3d->call("draw_arrow", godot_vec3_from_sim(p0), godot_vec3_from_sim(p1), godot::Color(1.0f, 1.0f, 1.0f), 0.25, true, _TICK_DELTA);
	}

	SimVec2  road_t_sample_raw;  SimVec3 spatial_t_sample;
	RoadTransform sample_root;
	RoadTransform sample_root_derivative;
	convert_point_to_road(track, use_idx, sample_pt, road_t_sample_raw, spatial_t_sample, nullptr, &sample_root, &sample_root_derivative);
	if (road_t_sample_raw.x == -1000.0)
	{
		return;
	}

	SimTransform surf;        // THE ONLY SURFACE FETCH
	//if (oriented)
	segment.road_shape->get_oriented_transform_at_time_presampled(surf, road_t_sample_raw, sample_root, sample_root_derivative);
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
			RoadTransform hit_root;
			convert_point_to_road(track, use_idx, hit_point, road_t_hit_raw, spatial_t_hit, nullptr, &hit_root, nullptr);
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
				out_collision.collision_face_point = out_collision.collision_point;
				out_collision.collision_face_normal = out_collision.collision_normal;

				out_collision.road_data.cp_idx          = use_idx;
				out_collision.road_data.spatial_t       = sim_vec3_from_godot(spatial_t_hit);
				out_collision.road_data.road_t          = sim_vec2_from_godot(road_t_hit_raw);
				out_collision.road_data.closest_surface = sim_transform_from_godot(surf);     // reuse single transform
				out_collision.road_data.closest_root = hit_root;

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

	if (!segment.road_shape->supports_edge_rails()) {
		return;
	}

	const RoadTransform &root_t = sample_root;
	TrackEdgeRailSide sides[2];
	segment.road_shape->get_edge_rail_sides(
		sides,
		road_t_sample_raw.y,
		surf.origin,
		sample_root,
		sample_root_derivative,
		segment.left_rail_height,
		segment.right_rail_height);
	draw_nearest_rail_candidate(sides, sample_pt, _TICK_DELTA);

	for (int side_index = 0; side_index < 2; ++side_index) {
		const TrackEdgeRailSide &side = sides[side_index];
		const float ra = (p0 - side.pos).dot(side.rail_n);
		const float rb = (p1 - side.pos).dot(side.rail_n);
		if ((ra <= 0.0f && rb <= 0.0f) || (ra >= 0.0f && rb >= 0.0f))
			continue;

		const float t = ra / (ra - rb);
		if (t < 0.0f || t > 1.0f)
			continue;

		const SimVec3 hit = p0 + ray * t;

		SimVec2 road_t_hit_raw;  SimVec3 spatial_t_hit;
		RoadTransform hit_root;
		convert_point_to_road(track, use_idx, hit, road_t_hit_raw, spatial_t_hit, nullptr, &hit_root, nullptr);
		if (road_t_hit_raw.x == -1000.0)
		{
			continue;
		}
		if (!track_segment_rail_side_active(segment, side_index, road_t_hit_raw.y))
		{
			continue;
		}

		const float vdist = (hit - surf.origin).dot(surf_n);        // height above track
		if (vdist < 0.0f || (hit - side.pos).dot(side.up_n) > side.height * root_t.scale.y)
			continue;
		if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(side.rail_n) > 0.0f)
			continue;

		const float dist = t * ray.length();
		if (dist < best_t) {
			best_t                          = dist;
			out_collision.collided          = true;
			out_collision.collision_point   = sim_vec3_from_godot(hit);
			out_collision.collision_normal  = sim_vec3_from_godot(side.rail_n);
			out_collision.collision_face_point = out_collision.collision_point;
			out_collision.collision_face_normal = out_collision.collision_normal;

			out_collision.road_data.cp_idx          = use_idx;
			out_collision.road_data.spatial_t       = sim_vec3_from_godot(spatial_t_hit);
			out_collision.road_data.road_t          = sim_vec2_from_godot(road_t_hit_raw);
			out_collision.road_data.closest_surface = sim_transform_from_godot(surf); // reuse same transform
			out_collision.road_data.closest_root = hit_root;
			out_collision.road_data.terrain         = 0x100;
		}
	}

	if (segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_TUNNEL &&
		track_segment_rail_side_active(segment, 0, road_t_sample_raw.y) &&
		track_segment_rail_side_active(segment, 1, road_t_sample_raw.y)) {
		float tunnel_t = 0.0f;
		SimVec3 tunnel_n;
		if (intersect_tunnel_roof_rail(sides, root_t, p0, ray, &tunnel_t, &tunnel_n)) {
			const SimVec3 hit = p0 + ray * tunnel_t;
			SimVec2 road_t_hit_raw;  SimVec3 spatial_t_hit;
			RoadTransform hit_root;
			convert_point_to_road(track, use_idx, hit, road_t_hit_raw, spatial_t_hit, nullptr, &hit_root, nullptr);
			if (road_t_hit_raw.x != -1000.0f &&
				track_segment_rail_side_active(segment, 0, road_t_hit_raw.y) &&
				track_segment_rail_side_active(segment, 1, road_t_hit_raw.y) &&
				((params.mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(tunnel_n) <= 0.0f)) {
				const float dist = tunnel_t * ray.length();
				if (dist < best_t) {
					best_t                          = dist;
					out_collision.collided          = true;
					out_collision.collision_point   = sim_vec3_from_godot(hit);
					out_collision.collision_normal  = sim_vec3_from_godot(tunnel_n);
					out_collision.collision_face_point = out_collision.collision_point;
					out_collision.collision_face_normal = out_collision.collision_normal;

					out_collision.road_data.cp_idx          = use_idx;
					out_collision.road_data.spatial_t       = sim_vec3_from_godot(spatial_t_hit);
					out_collision.road_data.road_t          = sim_vec2_from_godot(road_t_hit_raw);
					out_collision.road_data.closest_surface = sim_transform_from_godot(surf);
					out_collision.road_data.closest_root = hit_root;
					out_collision.road_data.terrain         = 0x100;
				}
			}
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

static bool aabb_overlaps_segment(const SimAABB &bounds, const SimVec3 &p0, const SimVec3 &p1)
{
	const SimVec3 box_max = bounds.position + bounds.size;
	const SimVec3 ray = p1 - p0;
	float t_min = 0.0f;
	float t_max = 1.0f;

	if (fabsf(ray.x) <= 1.0e-7f) {
		if (p0.x < bounds.position.x || p0.x > box_max.x) {
			return false;
		}
	} else {
		float t0 = (bounds.position.x - p0.x) / ray.x;
		float t1 = (box_max.x - p0.x) / ray.x;
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		t_min = std::max(t_min, t0);
		t_max = std::min(t_max, t1);
		if (t_min > t_max) {
			return false;
		}
	}
	if (fabsf(ray.y) <= 1.0e-7f) {
		if (p0.y < bounds.position.y || p0.y > box_max.y) {
			return false;
		}
	} else {
		float t0 = (bounds.position.y - p0.y) / ray.y;
		float t1 = (box_max.y - p0.y) / ray.y;
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		t_min = std::max(t_min, t0);
		t_max = std::min(t_max, t1);
		if (t_min > t_max) {
			return false;
		}
	}
	if (fabsf(ray.z) <= 1.0e-7f) {
		if (p0.z < bounds.position.z || p0.z > box_max.z) {
			return false;
		}
	} else {
		float t0 = (bounds.position.z - p0.z) / ray.z;
		float t1 = (box_max.z - p0.z) / ray.z;
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		t_min = std::max(t_min, t0);
		t_max = std::min(t_max, t1);
		if (t_min > t_max) {
			return false;
		}
	}
	return true;
}

static bool aabb_overlaps_aabb(const SimAABB &a, const SimAABB &b)
{
	const SimVec3 a_max = a.position + a.size;
	const SimVec3 b_max = b.position + b.size;
	return a.position.x <= b_max.x && a_max.x >= b.position.x &&
		a.position.y <= b_max.y && a_max.y >= b.position.y &&
		a.position.z <= b_max.z && a_max.z >= b.position.z;
}

enum MeshCastRejectReason
{
	MESH_CAST_REJECT_NONE,
	MESH_CAST_REJECT_PARALLEL,
	MESH_CAST_REJECT_BACKSIDE,
	MESH_CAST_REJECT_T,
	MESH_CAST_REJECT_BARY
};

static bool triangle_ray_hit(
	const TrackMeshCollisionTriangle &tri,
	const SimVec3 &p0,
	const SimVec3 &ray,
	bool allow_backface,
	float *out_t,
	float *out_u,
	float *out_v,
	float *out_w,
	SimVec3 *out_face_normal,
	bool *out_backside_hit,
	MeshCastRejectReason *out_reject_reason = nullptr)
{
	const SimVec3 edge0 = tri.p1 - tri.p0;
	const SimVec3 edge1 = tri.p2 - tri.p0;
	SimVec3 face_normal = edge0.cross(edge1);
	const float face_len2 = face_normal.length_squared();
	if (face_len2 <= 1.0e-8f) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_PARALLEL;
		}
		return false;
	}
	face_normal *= 1.0f / sqrtf(face_len2);
	const float denom = ray.dot(face_normal);
	if (fabsf(denom) <= 1.0e-7f) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_PARALLEL;
		}
		return false;
	}
	const bool backside_hit = denom > 0.0f;
	if (!allow_backface && backside_hit) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_BACKSIDE;
		}
		return false;
	}

	const float t = (tri.p0 - p0).dot(face_normal) / denom;
	if (t < 0.0f || t > 1.0f) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_T;
		}
		return false;
	}

	const SimVec3 hit = p0 + ray * t;
	const SimVec3 v0 = tri.p1 - tri.p0;
	const SimVec3 v1 = tri.p2 - tri.p0;
	const SimVec3 v2 = hit - tri.p0;
	const float d00 = v0.dot(v0);
	const float d01 = v0.dot(v1);
	const float d11 = v1.dot(v1);
	const float d20 = v2.dot(v0);
	const float d21 = v2.dot(v1);
	const float denom_bary = d00 * d11 - d01 * d01;
	if (fabsf(denom_bary) <= 1.0e-8f) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_BARY;
		}
		return false;
	}
	const float inv_denom = 1.0f / denom_bary;
	const float v = (d11 * d20 - d01 * d21) * inv_denom;
	const float w = (d00 * d21 - d01 * d20) * inv_denom;
	const float u = 1.0f - v - w;
	const float slop = -1.0e-4f;
	if (u < slop || v < slop || w < slop) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_BARY;
		}
		return false;
	}

	if (out_reject_reason) {
		*out_reject_reason = MESH_CAST_REJECT_NONE;
	}
	*out_t = t;
	*out_u = u;
	*out_v = v;
	*out_w = w;
	*out_face_normal = face_normal;
	*out_backside_hit = backside_hit;
	return true;
}

static SimVec3 mesh_collision_smooth_normal(const TrackMeshCollisionTriangle &tri, float u, float v, float w, const SimVec3 &face_normal)
{
	SimVec3 n0 = tri.n0.normalized();
	SimVec3 n1 = tri.n1.normalized();
	SimVec3 n2 = tri.n2.normalized();
	SimVec3 n = n0 * u + n1 * v + n2 * w;
	if (n.length_squared() <= 1.0e-8f) {
		return face_normal;
	}
	return n.normalized();
}

static SimVec3 mesh_collision_phong_point(const TrackMeshCollisionTriangle &tri, float u, float v, float w)
{
	const SimVec3 n0 = tri.n0.normalized();
	const SimVec3 n1 = tri.n1.normalized();
	const SimVec3 n2 = tri.n2.normalized();
	const SimVec3 flat_pos = tri.p0 * u + tri.p1 * v + tri.p2 * w;
	const SimVec3 proj0 = flat_pos - n0 * (flat_pos - tri.p0).dot(n0);
	const SimVec3 proj1 = flat_pos - n1 * (flat_pos - tri.p1).dot(n1);
	const SimVec3 proj2 = flat_pos - n2 * (flat_pos - tri.p2).dot(n2);
	const SimVec3 phong_pos = proj0 * u + proj1 * v + proj2 * w;
	return phong_pos.lerp(flat_pos, 0.5f);
}

static SimVec3 clamp_mesh_collision_phong_point(const SimVec3 &flat_point, const SimVec3 &phong_point)
{
	constexpr float kMaxPhongOffset = 2.0f;
	const SimVec3 offset = phong_point - flat_point;
	const float len2 = offset.length_squared();
	if (len2 <= kMaxPhongOffset * kMaxPhongOffset) {
		return phong_point;
	}
	const float len = sqrtf(len2);
	if (len <= 1.0e-6f) {
		return flat_point;
	}
	return flat_point + offset * (kMaxPhongOffset / len);
}

static bool mesh_collision_uses_smooth_surface(uint32_t terrain)
{
	return (terrain & (TERRAIN::RAIL | TERRAIN::HOLE)) == 0;
}

static SimTransform mesh_collision_surface_transform(const TrackMeshCollisionTriangle &tri, const SimVec3 &hit_point, const SimVec3 &normal)
{
	SimVec3 tangent = (tri.p1 - tri.p0).normalized();
	if (tangent.length_squared() <= 1.0e-8f || fabsf(tangent.dot(normal)) > 0.98f) {
		tangent = (tri.p2 - tri.p0).normalized();
	}
	tangent = (tangent - normal * tangent.dot(normal)).normalized();
	if (tangent.length_squared() <= 1.0e-8f) {
		tangent = SimVec3(1.0f, 0.0f, 0.0f);
	}
	SimVec3 forward = tangent.cross(normal).normalized();
	SimBasis basis;
	basis[0] = tangent;
	basis[1] = normal;
	basis[2] = forward;
	return SimTransform(basis, hit_point);
}

static SimVec3 closest_point_on_mesh_triangle(const TrackMeshCollisionTriangle &tri, const SimVec3 &p, float *out_u, float *out_v, float *out_w)
{
	const SimVec3 ab = tri.p1 - tri.p0;
	const SimVec3 ac = tri.p2 - tri.p0;
	const SimVec3 ap = p - tri.p0;
	const float d1 = ab.dot(ap);
	const float d2 = ac.dot(ap);
	if (d1 <= 0.0f && d2 <= 0.0f) {
		*out_u = 1.0f; *out_v = 0.0f; *out_w = 0.0f;
		return tri.p0;
	}

	const SimVec3 bp = p - tri.p1;
	const float d3 = ab.dot(bp);
	const float d4 = ac.dot(bp);
	if (d3 >= 0.0f && d4 <= d3) {
		*out_u = 0.0f; *out_v = 1.0f; *out_w = 0.0f;
		return tri.p1;
	}

	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		const float v = d1 / (d1 - d3);
		*out_u = 1.0f - v; *out_v = v; *out_w = 0.0f;
		return tri.p0 + ab * v;
	}

	const SimVec3 cp = p - tri.p2;
	const float d5 = ab.dot(cp);
	const float d6 = ac.dot(cp);
	if (d6 >= 0.0f && d5 <= d6) {
		*out_u = 0.0f; *out_v = 0.0f; *out_w = 1.0f;
		return tri.p2;
	}

	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		const float w = d2 / (d2 - d6);
		*out_u = 1.0f - w; *out_v = 0.0f; *out_w = w;
		return tri.p0 + ac * w;
	}

	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		*out_u = 0.0f; *out_v = 1.0f - w; *out_w = w;
		return tri.p1 + (tri.p2 - tri.p1) * w;
	}

	const float denom = 1.0f / (va + vb + vc);
	const float v = vb * denom;
	const float w = vc * denom;
	*out_u = 1.0f - v - w;
	*out_v = v;
	*out_w = w;
	return tri.p0 * (*out_u) + tri.p1 * (*out_v) + tri.p2 * (*out_w);
}

static void mesh_triangle_barycentric_on_face(
	const TrackMeshCollisionTriangle &tri,
	const SimVec3 &point,
	const SimVec3 &edge0,
	const SimVec3 &edge1,
	float inv_denom,
	float d00,
	float d01,
	float d11,
	float *out_u,
	float *out_v,
	float *out_w)
{
	const SimVec3 v2 = point - tri.p0;
	const float d20 = v2.dot(edge0);
	const float d21 = v2.dot(edge1);
	*out_v = (d11 * d20 - d01 * d21) * inv_denom;
	*out_w = (d00 * d21 - d01 * d20) * inv_denom;
	*out_u = 1.0f - *out_v - *out_w;
}

enum MeshFloorProjectionResult
{
	MESH_FLOOR_PROJECT_MISS,
	MESH_FLOOR_PROJECT_FACE,
	MESH_FLOOR_PROJECT_SMOOTH
};

static bool project_point_to_mesh_triangle(
	const TrackMeshCollisionTriangle &tri,
	const SimVec3 &p,
	bool allow_backside,
	bool allow_smooth_retry,
	SimVec3 *out_point,
	float *out_u,
	float *out_v,
	float *out_w,
	bool *out_backside_sample,
	MeshFloorProjectionResult *out_projection_result = nullptr,
	bool *out_smooth_retry_candidate = nullptr)
{
	if (out_smooth_retry_candidate) {
		*out_smooth_retry_candidate = false;
	}
	const SimVec3 &v0 = tri.edge0;
	const SimVec3 &v1 = tri.edge1;
	const SimVec3 &face_n = tri.face_normal;
	const float d00 = tri.projection_d00;
	const float d01 = tri.projection_d01;
	const float d11 = tri.projection_d11;
	const float inv_denom = tri.projection_inv_denom;

	SimVec3 projected = p - face_n * ((p - tri.p0).dot(face_n));
	constexpr float kBacksideSlop = -0.01f;
	const float signed_face_dist = (p - projected).dot(face_n);
	if (signed_face_dist < kBacksideSlop && !allow_backside) {
		if (out_projection_result) {
			*out_projection_result = MESH_FLOOR_PROJECT_MISS;
		}
		return false;
	}
	const bool backside_sample = signed_face_dist < kBacksideSlop;
	float u = 0.0f;
	float v = 0.0f;
	float w = 0.0f;
	mesh_triangle_barycentric_on_face(tri, projected, v0, v1, inv_denom, d00, d01, d11, &u, &v, &w);
	constexpr float kSupportSlop = -0.01f;
	if (u >= kSupportSlop && v >= kSupportSlop && w >= kSupportSlop) {
		*out_point = projected;
		*out_u = u;
		*out_v = v;
		*out_w = w;
		*out_backside_sample = backside_sample;
		if (out_projection_result) {
			*out_projection_result = MESH_FLOOR_PROJECT_FACE;
		}
		return true;
	}
	constexpr float kSmoothRetrySlop = -0.10f;
	if (u < kSmoothRetrySlop || v < kSmoothRetrySlop || w < kSmoothRetrySlop) {
		if (out_projection_result) {
			*out_projection_result = MESH_FLOOR_PROJECT_MISS;
		}
		return false;
	}
	if (!allow_smooth_retry) {
		if (out_smooth_retry_candidate && mesh_collision_uses_smooth_surface(tri.terrain)) {
			*out_smooth_retry_candidate = true;
		}
		if (out_projection_result) {
			*out_projection_result = MESH_FLOOR_PROJECT_MISS;
		}
		return false;
	}

	for (int i = 0; i < 3; ++i) {
		SimVec3 smooth_n = tri.n0 * u + tri.n1 * v + tri.n2 * w;
		if (smooth_n.length_squared() <= 1.0e-8f) {
			smooth_n = face_n;
		} else {
			smooth_n = smooth_n.normalized();
		}
		const float normal_dot_face = smooth_n.dot(face_n);
		if (fabsf(normal_dot_face) <= 1.0e-4f) {
			if (out_projection_result) {
				*out_projection_result = MESH_FLOOR_PROJECT_MISS;
			}
			return false;
		}
		projected = p - smooth_n * ((p - tri.p0).dot(face_n) / normal_dot_face);
		if ((p - projected).dot(face_n) < kBacksideSlop && !allow_backside) {
			if (out_projection_result) {
				*out_projection_result = MESH_FLOOR_PROJECT_MISS;
			}
			return false;
		}
		mesh_triangle_barycentric_on_face(tri, projected, v0, v1, inv_denom, d00, d01, d11, &u, &v, &w);
	}

	if (u < kSupportSlop || v < kSupportSlop || w < kSupportSlop) {
		if (out_projection_result) {
			*out_projection_result = MESH_FLOOR_PROJECT_MISS;
		}
		return false;
	}
	*out_point = projected;
	*out_u = u;
	*out_v = v;
	*out_w = w;
	*out_backside_sample = backside_sample;
	if (out_projection_result) {
		*out_projection_result = MESH_FLOOR_PROJECT_SMOOTH;
	}
	return true;
}

static bool project_point_to_mesh_triangle_face_fast(
	const TrackMeshCollisionTriangle &tri,
	const SimVec3 &p,
	bool allow_backside,
	float *out_signed_face_dist,
	float *out_u,
	float *out_v,
	float *out_w,
	bool *out_backside_sample,
	bool *out_smooth_retry_candidate)
{
	if (out_smooth_retry_candidate) {
		*out_smooth_retry_candidate = false;
	}
	const float px = p.x - tri.p0.x;
	const float py = p.y - tri.p0.y;
	const float pz = p.z - tri.p0.z;
	const SimVec3 &face_n = tri.face_normal;
	const float signed_face_dist = px * face_n.x + py * face_n.y + pz * face_n.z;
	constexpr float kBacksideSlop = -0.01f;
	if (signed_face_dist < kBacksideSlop && !allow_backside) {
		return false;
	}

	const SimVec3 &edge0 = tri.edge0;
	const SimVec3 &edge1 = tri.edge1;
	const float d20 = px * edge0.x + py * edge0.y + pz * edge0.z;
	const float d21 = px * edge1.x + py * edge1.y + pz * edge1.z;
	const float v = (tri.projection_d11 * d20 - tri.projection_d01 * d21) * tri.projection_inv_denom;
	const float w = (tri.projection_d00 * d21 - tri.projection_d01 * d20) * tri.projection_inv_denom;
	const float u = 1.0f - v - w;
	constexpr float kSupportSlop = -0.01f;
	if (u >= kSupportSlop && v >= kSupportSlop && w >= kSupportSlop) {
		*out_signed_face_dist = signed_face_dist;
		*out_u = u;
		*out_v = v;
		*out_w = w;
		*out_backside_sample = signed_face_dist < kBacksideSlop;
		return true;
	}

	constexpr float kSmoothRetrySlop = -0.10f;
	if (u >= kSmoothRetrySlop && v >= kSmoothRetrySlop && w >= kSmoothRetrySlop &&
		out_smooth_retry_candidate && mesh_collision_uses_smooth_surface(tri.terrain)) {
		*out_smooth_retry_candidate = true;
	}
	return false;
}

static float distance2_to_aabb(const SimAABB &bounds, const SimVec3 &p)
{
	const SimVec3 box_max = bounds.position + bounds.size;
	float d2 = 0.0f;
	float delta = 0.0f;
	if (p.x < bounds.position.x) {
		delta = bounds.position.x - p.x;
		d2 += delta * delta;
	} else if (p.x > box_max.x) {
		delta = p.x - box_max.x;
		d2 += delta * delta;
	}
	if (p.y < bounds.position.y) {
		delta = bounds.position.y - p.y;
		d2 += delta * delta;
	} else if (p.y > box_max.y) {
		delta = p.y - box_max.y;
		d2 += delta * delta;
	}
	if (p.z < bounds.position.z) {
		delta = bounds.position.z - p.z;
		d2 += delta * delta;
	} else if (p.z > box_max.z) {
		delta = p.z - box_max.z;
		d2 += delta * delta;
	}
	return d2;
}

static TrackQueryScratch::MeshFloorProfileCounters *mesh_floor_profile_counters(TrackQueryScratch *scratch)
{
	if (!scratch) {
		return nullptr;
	}
	int scope = static_cast<int>(scratch->mesh_floor_profile_scope);
	if (scope < 0 || scope >= TrackQueryScratch::MESH_FLOOR_PROFILE_SCOPE_COUNT) {
		scope = TrackQueryScratch::MESH_FLOOR_SCOPE_UNKNOWN;
	}
	return &scratch->mesh_floor_profile[scope];
}

static bool scan_mesh_cast_triangle(
	const CastParams &params,
	CollisionData &out_collision,
	const SimVec3 &p0,
	const SimVec3 &p1,
	const SimVec3 &ray,
	float ray_len,
	int start_idx,
	int tri_index,
	float &best_dist,
	TrackQueryScratch *scratch)
{
	RaceTrack *track = params.track;
	if (scratch) {
		scratch->mesh_cast_tri_tests += 1;
	}
	const TrackMeshCollisionTriangle &tri = track->mesh_collision_triangles[tri_index];
	const bool is_rail = (tri.terrain & TERRAIN::RAIL) != 0;
	if (is_rail) {
		if ((params.mask & CAST_FLAGS::WANTS_RAIL) == 0) {
			if (scratch) {
				scratch->mesh_cast_surface_rejects += 1;
			}
			return false;
		}
	} else if ((params.mask & CAST_FLAGS::WANTS_TRACK) == 0) {
		if (scratch) {
			scratch->mesh_cast_surface_rejects += 1;
		}
		return false;
	}
	if (!aabb_overlaps_segment(tri.bounds, p0, p1)) {
		if (scratch) {
			scratch->mesh_cast_aabb_rejects += 1;
		}
		return false;
	}
	if (mesh_debug_draw_current_car(scratch) && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS)) {
		const bool rail_query = (params.mask & CAST_FLAGS::WANTS_RAIL) != 0;
		const bool terrain_query = (params.mask & CAST_FLAGS::WANTS_TERRAIN) != 0;
		const godot::Color color = rail_query
			? godot::Color(1.0f, 0.0f, 1.0f, 0.75f)
			: (terrain_query ? godot::Color(1.0f, 0.85f, 0.1f, 0.75f) : godot::Color(1.0f, 0.45f, 0.05f, 0.75f));
		draw_mesh_debug_triangle(tri, color, _TICK_DELTA);
	}

	float hit_t = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
	float w = 0.0f;
	SimVec3 face_normal;
	bool backside_hit = false;
	const bool allow_backside = (tri.terrain & TERRAIN::BACKSIDE) != 0;
	MeshCastRejectReason reject_reason = MESH_CAST_REJECT_NONE;
	if (!triangle_ray_hit(tri, p0, ray, allow_backside, &hit_t, &u, &v, &w, &face_normal, &backside_hit, &reject_reason)) {
		if (scratch) {
			switch (reject_reason) {
				case MESH_CAST_REJECT_PARALLEL:
					scratch->mesh_cast_ray_parallel_rejects += 1;
					break;
				case MESH_CAST_REJECT_BACKSIDE:
					scratch->mesh_cast_backside_rejects += 1;
					break;
				case MESH_CAST_REJECT_T:
					scratch->mesh_cast_t_rejects += 1;
					break;
				case MESH_CAST_REJECT_BARY:
					scratch->mesh_cast_bary_rejects += 1;
					break;
				default:
					break;
			}
		}
		return false;
	}
	const float dist = hit_t * ray_len;
	if (dist >= best_dist) {
		if (scratch) {
			scratch->mesh_cast_best_dist_rejects += 1;
		}
		return false;
	}

	const SimVec3 flat_point = p0 + ray * hit_t;
	SimVec3 hit_normal = face_normal;
	SimVec3 hit_face_normal = face_normal;
	SimVec3 hit_point = flat_point;
	if (params.smooth_mesh_hits && mesh_collision_uses_smooth_surface(tri.terrain)) {
		hit_normal = mesh_collision_smooth_normal(tri, u, v, w, face_normal);
		hit_point = clamp_mesh_collision_phong_point(flat_point, mesh_collision_phong_point(tri, u, v, w));
	}
	if (backside_hit) {
		hit_normal *= -1.0f;
		hit_face_normal *= -1.0f;
	}
	const int cp_idx = tri.checkpoint_index >= 0 ? tri.checkpoint_index : start_idx;
	if (cp_idx < 0 || cp_idx >= track->num_checkpoints) {
		godot::UtilityFunctions::printerr(godot::String("MXT mesh collision triangle has invalid checkpoint index "), cp_idx);
		std::abort();
	}

	best_dist = dist;
	if (scratch) {
		scratch->mesh_cast_hits += 1;
	}
	if (mesh_debug_draw_current_car(scratch) && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS)) {
		draw_mesh_debug_triangle(tri, godot::Color(0.2f, 1.0f, 0.15f, 0.95f), _TICK_DELTA);
	}
	out_collision.collided = true;
	out_collision.collision_point = hit_point;
	out_collision.collision_normal = hit_normal;
	out_collision.collision_face_point = flat_point;
	out_collision.collision_face_normal = hit_face_normal;
	out_collision.road_data.cp_idx = cp_idx;
	out_collision.road_data.spatial_t = SimVec3();
	out_collision.road_data.road_t = SimVec2(0.0f, 0.5f);
	out_collision.road_data.closest_surface = mesh_collision_surface_transform(tri, hit_point, hit_normal);
	out_collision.road_data.closest_root = RoadTransform();
	out_collision.road_data.terrain = static_cast<uint16_t>(tri.terrain);
	out_collision.mesh_triangle_index = tri_index;
	return true;
}

static void cast_mesh_collision_fast(
	const CastParams &params,
	CollisionData &out_collision,
	const SimVec3 &p0,
	const SimVec3 &p1,
	int start_idx,
	TrackQueryScratch *scratch)
{
	RaceTrack *track = params.track;
	if (scratch) {
		scratch->mesh_cast_calls += 1;
	}
	if (track->num_mesh_collision_triangles <= 0) {
		return;
	}
	const int segment_index = track->checkpoints[start_idx].road_segment;
	const TrackSegment &segment = track->segments[segment_index];
	if (segment.mesh_collision_count <= 0 &&
		(!track->mesh_world_bvh_nodes || !track->mesh_world_bvh_triangle_indices || track->num_mesh_world_bvh_nodes <= 0)) {
		return;
	}
	const SimVec3 ray = p1 - p0;
	const float ray_len = ray.length();
	if (ray_len <= 1.0e-6f) {
		return;
	}

	float best_dist = out_collision.collided ? p0.distance_to(out_collision.collision_point) : FLT_MAX;
	bool mesh_hit = false;
	auto scan_triangle = [&](int tri_index) {
		mesh_hit |= scan_mesh_cast_triangle(params, out_collision, p0, p1, ray, ray_len, start_idx, tri_index, best_dist, scratch);
	};

	auto scan_checkpoint = [&](int cp_idx) {
		if (cp_idx < 0 || cp_idx >= track->num_checkpoints || !track->mesh_checkpoint_triangle_head) {
			return;
		}
		if (track->mesh_checkpoint_bvh_nodes && track->mesh_checkpoint_bvh_triangle_indices && track->mesh_checkpoint_bvh_node_start) {
			const int root = track->mesh_checkpoint_bvh_node_start[cp_idx];
			if (root < 0) {
				return;
			}
			int stack[128];
			int stack_count = 0;
			stack[stack_count++] = root;
			while (stack_count > 0) {
				const int node_index = stack[--stack_count];
				const TrackMeshBVHNode &node = track->mesh_checkpoint_bvh_nodes[node_index];
				if (scratch) {
					scratch->mesh_cast_bvh_node_tests += 1;
				}
				if (!aabb_overlaps_segment(node.bounds, p0, p1)) {
					continue;
				}
				if (node.count > 0) {
					const int end = node.left_first + node.count;
					for (int i = node.left_first; i < end; ++i) {
						scan_triangle(track->mesh_checkpoint_bvh_triangle_indices[i]);
					}
				} else {
					if (stack_count + 2 > 128) {
						godot::UtilityFunctions::printerr(godot::String("MXT mesh checkpoint BVH traversal stack overflow"));
						std::abort();
					}
					if (node.left_first < 0 || node.right_first < 0) {
						godot::UtilityFunctions::printerr(godot::String("MXT mesh checkpoint BVH interior node has invalid child"));
						std::abort();
					}
					stack[stack_count++] = node.left_first;
					stack[stack_count++] = node.right_first;
				}
			}
			return;
		}
		for (int tri_index = track->mesh_checkpoint_triangle_head[cp_idx]; tri_index >= 0; tri_index = track->mesh_collision_triangles[tri_index].next_checkpoint_triangle) {
			scan_triangle(tri_index);
		}
	};

	if (track->mesh_world_bvh_nodes && track->mesh_world_bvh_triangle_indices && track->num_mesh_world_bvh_nodes > 0) {
		int stack[256];
		int stack_count = 0;
		stack[stack_count++] = 0;
		while (stack_count > 0) {
			const int node_index = stack[--stack_count];
			const TrackMeshBVHNode &node = track->mesh_world_bvh_nodes[node_index];
			if (scratch) {
				scratch->mesh_cast_bvh_node_tests += 1;
			}
			if (!aabb_overlaps_segment(node.bounds, p0, p1)) {
				continue;
			}
			if (node.count > 0) {
				const int end = node.left_first + node.count;
				for (int i = node.left_first; i < end; ++i) {
					scan_triangle(track->mesh_world_bvh_triangle_indices[i]);
				}
			} else {
				if (stack_count + 2 > 256) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world cast BVH traversal stack overflow"));
					std::abort();
				}
				if (node.left_first < 0 || node.right_first < 0) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world cast BVH interior node has invalid child"));
					std::abort();
				}
				stack[stack_count++] = node.left_first;
				stack[stack_count++] = node.right_first;
			}
		}
		return;
	}

	scan_checkpoint(start_idx);
	for (int delta = 1; delta <= 4; ++delta) {
		scan_checkpoint(start_idx - delta);
		scan_checkpoint(start_idx + delta);
	}
	const CollisionCheckpoint &start_cp = track->checkpoints[start_idx];
	for (int neighbor = 0; neighbor < start_cp.num_neighboring_checkpoints; ++neighbor) {
		scan_checkpoint(start_cp.neighboring_checkpoints[neighbor]);
	}
	if (!mesh_hit && (params.mask & CAST_FLAGS::WANTS_RAIL) == 0) {
		const int tri_end = segment.mesh_collision_start + segment.mesh_collision_count;
		for (int tri_index = segment.mesh_collision_start; tri_index < tri_end; ++tri_index) {
			scan_triangle(tri_index);
		}
	}
}

bool RaceTrack::collect_mesh_cast_candidates(const SimAABB &bounds, uint8_t mask, TrackQueryScratch &scratch)
{
	scratch.mesh_cast_candidate_count = 0;
	scratch.mesh_cast_candidate_builds += 1;
	if (!mesh_world_bvh_nodes || !mesh_world_bvh_triangle_indices || num_mesh_world_bvh_nodes <= 0) {
		return false;
	}

	int stack[256];
	int stack_count = 0;
	stack[stack_count++] = 0;
	while (stack_count > 0) {
		const int node_index = stack[--stack_count];
		const TrackMeshBVHNode &node = mesh_world_bvh_nodes[node_index];
		scratch.mesh_cast_candidate_bvh_node_tests += 1;
		if (!aabb_overlaps_aabb(node.bounds, bounds)) {
			continue;
		}
		if (node.count > 0) {
			const int end = node.left_first + node.count;
			for (int i = node.left_first; i < end; ++i) {
				const int tri_index = mesh_world_bvh_triangle_indices[i];
				const TrackMeshCollisionTriangle &tri = mesh_collision_triangles[tri_index];
				const bool is_rail = (tri.terrain & TERRAIN::RAIL) != 0;
				if (is_rail) {
					if ((mask & CAST_FLAGS::WANTS_RAIL) == 0) {
						continue;
					}
				} else if ((mask & CAST_FLAGS::WANTS_TRACK) == 0) {
					continue;
				}
				if (!aabb_overlaps_aabb(tri.bounds, bounds)) {
					continue;
				}
				if (scratch.mesh_cast_candidate_count >= TrackQueryScratch::MAX_MESH_CAST_CANDIDATES) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh cast candidate list overflow"));
					std::abort();
				}
				scratch.mesh_cast_candidate_indices[scratch.mesh_cast_candidate_count++] = tri_index;
				scratch.mesh_cast_candidate_triangles += 1;
			}
		} else {
			if (stack_count + 2 > 256) {
				godot::UtilityFunctions::printerr(godot::String("MXT mesh world candidate BVH traversal stack overflow"));
				std::abort();
			}
			if (node.left_first < 0 || node.right_first < 0) {
				godot::UtilityFunctions::printerr(godot::String("MXT mesh world candidate BVH interior node has invalid child"));
				std::abort();
			}
			stack[stack_count++] = node.left_first;
			stack[stack_count++] = node.right_first;
		}
	}
	return true;
}

void RaceTrack::cast_vs_mesh_candidates_fast(
	CollisionData &out_collision,
	const SimVec3 &p0,
	const SimVec3 &p1,
	uint8_t mask,
	int start_idx,
	TrackQueryScratch *scratch,
	bool smooth_mesh_hits)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();
	if (!scratch || start_idx < 0 || start_idx >= num_checkpoints || num_mesh_collision_triangles <= 0) {
		return;
	}
	if ((mask & (CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_RAIL)) == 0) {
		return;
	}
	scratch->mesh_cast_calls += 1;
	const SimVec3 ray = p1 - p0;
	const float ray_len = ray.length();
	if (ray_len <= 1.0e-6f) {
		return;
	}

	float best_dist = FLT_MAX;
	CastParams params{ this, mask, smooth_mesh_hits };
	for (int i = 0; i < scratch->mesh_cast_candidate_count; ++i) {
		scan_mesh_cast_triangle(
			params,
			out_collision,
			p0,
			p1,
			ray,
			ray_len,
			start_idx,
			scratch->mesh_cast_candidate_indices[i],
			best_dist,
			scratch);
	}
}

void RaceTrack::sample_mesh_floor_fast(CollisionData &out_collision, const SimVec3 &point, float max_distance, uint8_t mask, int start_idx, bool allow_global_fallback, TrackQueryScratch *scratch, int seed_triangle_index)
{
	TrackQueryScratch::MeshFloorProfileCounters *floor_profile = mesh_floor_profile_counters(scratch);
	const auto floor_profile_start = std::chrono::high_resolution_clock::now();
	auto finish_floor_profile = [&]() {
		if (floor_profile) {
			const auto floor_profile_end = std::chrono::high_resolution_clock::now();
			floor_profile->query_us += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(floor_profile_end - floor_profile_start).count());
		}
	};
	if (scratch) {
		scratch->mesh_floor_calls += 1;
	}
	if (floor_profile) {
		floor_profile->calls += 1;
	}
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();
	if (start_idx < 0 || start_idx >= num_checkpoints || num_mesh_collision_triangles <= 0) {
		finish_floor_profile();
		return;
	}
	if ((mask & CAST_FLAGS::WANTS_TRACK) == 0) {
		finish_floor_profile();
		return;
	}

	const float max_dist2 = max_distance * max_distance;
	float best_dist2 = max_dist2;
	float best_u = 0.0f;
	float best_v = 0.0f;
	float best_w = 0.0f;
	SimVec3 best_flat_point;
	bool best_backside_sample = false;
	const TrackMeshCollisionTriangle *best_tri = nullptr;
	int best_tri_index = -1;
	bool allow_smooth_projection_retry = false;
	bool saw_smooth_projection_candidate = false;

	auto scan_triangle = [&](int tri_index) {
		if (scratch) {
			scratch->mesh_floor_tri_tests += 1;
		}
		if (floor_profile) {
			floor_profile->tri_tests += 1;
		}
		const TrackMeshCollisionTriangle &tri = mesh_collision_triangles[tri_index];
		if ((tri.terrain & TERRAIN::RAIL) != 0) {
			if (scratch) {
				scratch->mesh_floor_rail_rejects += 1;
			}
			if (floor_profile) {
				floor_profile->rail_rejects += 1;
			}
			return;
		}
		if (distance2_to_aabb(tri.bounds, point) > best_dist2) {
			if (scratch) {
				scratch->mesh_floor_aabb_rejects += 1;
			}
			if (floor_profile) {
				floor_profile->aabb_rejects += 1;
			}
			return;
		}
		float u = 0.0f;
		float v = 0.0f;
		float w = 0.0f;
		SimVec3 projected;
		bool backside_sample = false;
		const bool allow_backside = (tri.terrain & TERRAIN::BACKSIDE) != 0;
		MeshFloorProjectionResult projection_result = MESH_FLOOR_PROJECT_MISS;
		bool smooth_retry_candidate = false;
		float signed_face_dist = 0.0f;
		float dist2 = 0.0f;
		if (allow_smooth_projection_retry) {
			if (!project_point_to_mesh_triangle(tri, point, allow_backside, true, &projected, &u, &v, &w, &backside_sample, &projection_result, &smooth_retry_candidate)) {
				saw_smooth_projection_candidate |= smooth_retry_candidate;
				if (scratch) {
					scratch->mesh_floor_projection_misses += 1;
				}
				if (floor_profile) {
					floor_profile->projection_misses += 1;
				}
				return;
			}
			dist2 = (point - projected).length_squared();
		} else if (project_point_to_mesh_triangle_face_fast(tri, point, allow_backside, &signed_face_dist, &u, &v, &w, &backside_sample, &smooth_retry_candidate)) {
			projection_result = MESH_FLOOR_PROJECT_FACE;
			dist2 = signed_face_dist * signed_face_dist;
		} else {
			saw_smooth_projection_candidate |= smooth_retry_candidate;
			if (scratch) {
				scratch->mesh_floor_projection_misses += 1;
			}
			if (floor_profile) {
				floor_profile->projection_misses += 1;
			}
			return;
		}
		if (scratch) {
			if (projection_result == MESH_FLOOR_PROJECT_SMOOTH) {
				scratch->mesh_floor_smooth_projection_hits += 1;
			} else {
				scratch->mesh_floor_face_projection_hits += 1;
			}
		}
		if (floor_profile) {
			if (projection_result == MESH_FLOOR_PROJECT_SMOOTH) {
				floor_profile->smooth_projection_hits += 1;
			} else {
				floor_profile->face_projection_hits += 1;
			}
		}
		if (mesh_debug_draw_current_car(scratch) && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_FLOOR_TESTS)) {
			draw_mesh_debug_triangle(tri, godot::Color(0.1f, 0.8f, 1.0f, 0.7f), _TICK_DELTA);
		}
		if (dist2 >= best_dist2) {
			if (scratch) {
				scratch->mesh_floor_best_dist_rejects += 1;
			}
			if (floor_profile) {
				floor_profile->best_dist_rejects += 1;
			}
			return;
		}
		if (scratch) {
			scratch->mesh_floor_best_updates += 1;
		}
		if (floor_profile) {
			floor_profile->best_updates += 1;
		}
		best_dist2 = dist2;
		best_u = u;
		best_v = v;
		best_w = w;
		if (projection_result == MESH_FLOOR_PROJECT_FACE) {
			best_flat_point = point - tri.face_normal * signed_face_dist;
		} else {
			best_flat_point = projected;
		}
		best_backside_sample = backside_sample;
		best_tri = &tri;
		best_tri_index = tri_index;
		if (mesh_debug_draw_current_car(scratch) && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS)) {
			draw_mesh_debug_triangle(tri, godot::Color(0.1f, 1.0f, 0.45f, 0.95f), _TICK_DELTA);
		}
	};

	auto scan_checkpoint = [&](int cp_idx) {
		if (cp_idx < 0 || cp_idx >= num_checkpoints || !mesh_checkpoint_triangle_head) {
			return;
		}
		if (scratch) {
			scratch->mesh_floor_checkpoint_scans += 1;
		}
		for (int tri_index = mesh_checkpoint_triangle_head[cp_idx]; tri_index >= 0; tri_index = mesh_collision_triangles[tri_index].next_checkpoint_triangle) {
			scan_triangle(tri_index);
		}
	};

	auto scan_segment = [&](int seg) {
		if (seg < 0 || seg >= num_segments) {
			return;
		}
		const TrackSegment &segment = segments[seg];
		if (segment.mesh_collision_count <= 0 || distance2_to_aabb(segment.mesh_bounds, point) > best_dist2) {
			return;
		}
		if (scratch) {
			scratch->mesh_floor_segment_scans += 1;
		}
		const int tri_end = segment.mesh_collision_start + segment.mesh_collision_count;
		for (int tri_index = segment.mesh_collision_start; tri_index < tri_end; ++tri_index) {
			scan_triangle(tri_index);
		}
	};

	auto scan_world_bvh_root = [&](int root_node_index) {
		if (!mesh_world_bvh_nodes || !mesh_world_bvh_triangle_indices || num_mesh_world_bvh_nodes <= 0) {
			return false;
		}
		int stack[256];
		int stack_count = 0;
		stack[stack_count++] = root_node_index;
		while (stack_count > 0) {
			const int node_index = stack[--stack_count];
			const TrackMeshBVHNode &node = mesh_world_bvh_nodes[node_index];
			if (scratch) {
				scratch->mesh_floor_bvh_node_tests += 1;
			}
			if (floor_profile) {
				floor_profile->bvh_node_tests += 1;
			}
			if (distance2_to_aabb(node.bounds, point) > best_dist2) {
				continue;
			}
			if (node.count > 0) {
				const int end = node.left_first + node.count;
				for (int i = node.left_first; i < end; ++i) {
					scan_triangle(mesh_world_bvh_triangle_indices[i]);
				}
			} else {
				if (stack_count + 2 > 256) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world BVH traversal stack overflow"));
					std::abort();
				}
				if (node.left_first < 0 || node.right_first < 0) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world BVH interior node has invalid child"));
					std::abort();
				}
				const TrackMeshBVHNode &left = mesh_world_bvh_nodes[node.left_first];
				const TrackMeshBVHNode &right = mesh_world_bvh_nodes[node.right_first];
				const float left_dist2 = distance2_to_aabb(left.bounds, point);
				const float right_dist2 = distance2_to_aabb(right.bounds, point);
				if (left_dist2 <= right_dist2) {
					if (right_dist2 <= best_dist2) {
						stack[stack_count++] = node.right_first;
					}
					if (left_dist2 <= best_dist2) {
						stack[stack_count++] = node.left_first;
					}
				} else {
					if (left_dist2 <= best_dist2) {
						stack[stack_count++] = node.left_first;
					}
					if (right_dist2 <= best_dist2) {
						stack[stack_count++] = node.right_first;
					}
				}
			}
		}
		return true;
	};

	if (seed_triangle_index >= num_mesh_collision_triangles) {
		godot::UtilityFunctions::printerr(godot::String("MXT mesh floor seed triangle out of range "), seed_triangle_index);
		std::abort();
	}
	auto scan_mesh_floor_candidates = [&]() {
		if (seed_triangle_index >= 0) {
			if (scratch) {
				scratch->mesh_floor_seed_calls += 1;
			}
			if (floor_profile) {
				floor_profile->seed_calls += 1;
			}
			const int prev_best_tri_index = best_tri_index;
			scan_triangle(seed_triangle_index);
			if (scratch && best_tri_index == seed_triangle_index && best_tri_index != prev_best_tri_index) {
				scratch->mesh_floor_seed_hits += 1;
			}
			if (floor_profile && best_tri_index == seed_triangle_index && best_tri_index != prev_best_tri_index) {
				floor_profile->seed_hits += 1;
			}
		}

		if (scan_world_bvh_root(0)) {
			return;
		}
		scan_checkpoint(start_idx);
		for (int delta = 1; delta <= 2; ++delta) {
			scan_checkpoint(start_idx - delta);
			scan_checkpoint(start_idx + delta);
		}
		const CollisionCheckpoint &start_cp = checkpoints[start_idx];
		for (int neighbor = 0; neighbor < start_cp.num_neighboring_checkpoints; ++neighbor) {
			scan_checkpoint(start_cp.neighboring_checkpoints[neighbor]);
		}
		if (!best_tri && allow_global_fallback) {
			scan_segment(checkpoints[start_idx].road_segment);
		}
		if (!best_tri && allow_global_fallback) {
			for (int seg = 0; seg < num_segments; ++seg) {
				scan_segment(seg);
			}
		}
	};
	auto scan_profile_start = std::chrono::high_resolution_clock::now();
	scan_mesh_floor_candidates();
	if (floor_profile) {
		const auto scan_profile_end = std::chrono::high_resolution_clock::now();
		floor_profile->scan_us += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(scan_profile_end - scan_profile_start).count());
	}
	if (!best_tri && saw_smooth_projection_candidate) {
		if (floor_profile) {
			floor_profile->smooth_retry_queries += 1;
		}
		allow_smooth_projection_retry = true;
		scan_profile_start = std::chrono::high_resolution_clock::now();
		scan_mesh_floor_candidates();
		if (floor_profile) {
			const auto scan_profile_end = std::chrono::high_resolution_clock::now();
			floor_profile->scan_us += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(scan_profile_end - scan_profile_start).count());
		}
	}

	if (!best_tri) {
		finish_floor_profile();
		return;
	}

	const SimVec3 edge0 = best_tri->p1 - best_tri->p0;
	const SimVec3 edge1 = best_tri->p2 - best_tri->p0;
	SimVec3 face_normal = edge0.cross(edge1).normalized();
	SimVec3 smooth_normal = face_normal;
	SimVec3 smooth_point = best_flat_point;
	if (mesh_collision_uses_smooth_surface(best_tri->terrain)) {
		const auto smooth_profile_start = std::chrono::high_resolution_clock::now();
		smooth_normal = mesh_collision_smooth_normal(*best_tri, best_u, best_v, best_w, face_normal);
		smooth_point = clamp_mesh_collision_phong_point(best_flat_point, mesh_collision_phong_point(*best_tri, best_u, best_v, best_w));
		if (floor_profile) {
			const auto smooth_profile_end = std::chrono::high_resolution_clock::now();
			floor_profile->final_smooth_us += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(smooth_profile_end - smooth_profile_start).count());
			floor_profile->final_smooth_outputs += 1;
		}
	}
	if (best_backside_sample) {
		smooth_normal *= -1.0f;
		face_normal *= -1.0f;
	}
	const int cp_idx = best_tri->checkpoint_index >= 0 ? best_tri->checkpoint_index : start_idx;
	if (cp_idx < 0 || cp_idx >= num_checkpoints) {
		godot::UtilityFunctions::printerr(godot::String("MXT mesh collision triangle has invalid checkpoint index "), cp_idx);
		std::abort();
	}

	out_collision.collided = true;
	out_collision.collision_point = smooth_point;
	out_collision.collision_normal = smooth_normal;
	out_collision.collision_face_point = best_flat_point;
	out_collision.collision_face_normal = face_normal;
	out_collision.road_data.cp_idx = cp_idx;
	out_collision.road_data.spatial_t = SimVec3();
	out_collision.road_data.road_t = SimVec2(0.0f, 0.5f);
	out_collision.road_data.closest_surface = mesh_collision_surface_transform(*best_tri, smooth_point, smooth_normal);
	out_collision.road_data.closest_root = RoadTransform();
	out_collision.road_data.terrain = static_cast<uint16_t>(best_tri->terrain);
	out_collision.mesh_triangle_index = best_tri_index;
	finish_floor_profile();
}

void RaceTrack::cast_vs_track_fast(CollisionData &out_collision,
	SimVec3 const &p0,
	SimVec3 const &p1,
	uint8_t mask,
	int start_idx, bool oriented, TrackQueryScratch *scratch, bool smooth_mesh_hits)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();

	if ((mask & (CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_RAIL)) == 0)
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
	
	CastParams params{ this, mask, smooth_mesh_hits };
	cast_segment_fast(params, out_collision, p0, p1, start_idx, sample_point, true);
	cast_mesh_collision_fast(params, out_collision, p0, p1, start_idx, scratch);
}
