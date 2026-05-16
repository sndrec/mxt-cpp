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
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
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
	bool track_only_query = false;
	bool draw_cast_tests = false;
	bool draw_collision_hits = false;
	bool build_surface_basis = true;
	const SimVec3 *mesh_side_reference_point = nullptr;
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

static bool mesh_bvh_child_empty(const TrackMeshBVHNode &node, int slot)
{
	return node.count[slot] < 0;
}

static bool mesh_bvh_child_is_leaf(const TrackMeshBVHNode &node, int slot)
{
	return node.count[slot] > 0;
}

static SimAABB mesh_bvh_child_bounds(const TrackMeshBVHNode &node, int slot)
{
	SimAABB bounds;
	bounds.position = SimVec3(node.min_x[slot], node.min_y[slot], node.min_z[slot]);
	bounds.size = SimVec3(
		node.max_x[slot] - node.min_x[slot],
		node.max_y[slot] - node.min_y[slot],
		node.max_z[slot] - node.min_z[slot]);
	return bounds;
}

static bool mesh_bvh_child_overlaps_aabb(const TrackMeshBVHNode &node, int slot, const SimAABB &bounds)
{
	const SimVec3 b_max = bounds.position + bounds.size;
	return node.min_x[slot] <= b_max.x && node.max_x[slot] >= bounds.position.x &&
		node.min_y[slot] <= b_max.y && node.max_y[slot] >= bounds.position.y &&
		node.min_z[slot] <= b_max.z && node.max_z[slot] >= bounds.position.z;
}

static bool mesh_bvh_child_overlaps_segment(const TrackMeshBVHNode &node, int slot, const SimVec3 &p0, const SimVec3 &p1)
{
	const SimAABB bounds = mesh_bvh_child_bounds(node, slot);
	return aabb_overlaps_segment(bounds, p0, p1);
}

static uint32_t mesh_bvh_live_child_mask(const TrackMeshBVHNode &node)
{
	uint32_t mask = 0;
	for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
		if (!mesh_bvh_child_empty(node, slot)) {
			mask |= 1u << slot;
		}
	}
	return mask;
}

static uint32_t mesh_bvh_child_aabb_mask(const TrackMeshBVHNode &node, const SimAABB &bounds)
{
#if defined(__SSE__)
	const SimVec3 b_max = bounds.position + bounds.size;
	const __m128 q_min_x = _mm_load_ps(node.min_x);
	const __m128 q_min_y = _mm_load_ps(node.min_y);
	const __m128 q_min_z = _mm_load_ps(node.min_z);
	const __m128 q_max_x = _mm_load_ps(node.max_x);
	const __m128 q_max_y = _mm_load_ps(node.max_y);
	const __m128 q_max_z = _mm_load_ps(node.max_z);
	__m128 valid = _mm_cmple_ps(q_min_x, _mm_set1_ps(b_max.x));
	valid = _mm_and_ps(valid, _mm_cmpge_ps(q_max_x, _mm_set1_ps(bounds.position.x)));
	valid = _mm_and_ps(valid, _mm_cmple_ps(q_min_y, _mm_set1_ps(b_max.y)));
	valid = _mm_and_ps(valid, _mm_cmpge_ps(q_max_y, _mm_set1_ps(bounds.position.y)));
	valid = _mm_and_ps(valid, _mm_cmple_ps(q_min_z, _mm_set1_ps(b_max.z)));
	valid = _mm_and_ps(valid, _mm_cmpge_ps(q_max_z, _mm_set1_ps(bounds.position.z)));
	return static_cast<uint32_t>(_mm_movemask_ps(valid));
#else
	uint32_t mask = 0;
	for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
		if (!mesh_bvh_child_empty(node, slot) && mesh_bvh_child_overlaps_aabb(node, slot, bounds)) {
			mask |= 1u << slot;
		}
	}
	return mask;
#endif
}

static uint32_t mesh_bvh_child_segment_mask(const TrackMeshBVHNode &node, const SimVec3 &p0, const SimVec3 &p1)
{
#if defined(__SSE__)
	const SimVec3 ray = p1 - p0;
	__m128 t_min = _mm_setzero_ps();
	__m128 t_max = _mm_set1_ps(1.0f);
	__m128 valid = _mm_cmpeq_ps(_mm_setzero_ps(), _mm_setzero_ps());

	auto apply_axis = [&](__m128 min_v, __m128 max_v, float p, float ray_axis) {
		if (fabsf(ray_axis) <= 1.0e-7f) {
			__m128 axis_valid = _mm_and_ps(_mm_cmple_ps(min_v, _mm_set1_ps(p)), _mm_cmpge_ps(max_v, _mm_set1_ps(p)));
			valid = _mm_and_ps(valid, axis_valid);
		} else {
			const __m128 inv_ray = _mm_set1_ps(1.0f / ray_axis);
			const __m128 p_lane = _mm_set1_ps(p);
			const __m128 t0 = _mm_mul_ps(_mm_sub_ps(min_v, p_lane), inv_ray);
			const __m128 t1 = _mm_mul_ps(_mm_sub_ps(max_v, p_lane), inv_ray);
			const __m128 lo = _mm_min_ps(t0, t1);
			const __m128 hi = _mm_max_ps(t0, t1);
			t_min = _mm_max_ps(t_min, lo);
			t_max = _mm_min_ps(t_max, hi);
			valid = _mm_and_ps(valid, _mm_cmple_ps(t_min, t_max));
		}
	};

	apply_axis(_mm_load_ps(node.min_x), _mm_load_ps(node.max_x), p0.x, ray.x);
	apply_axis(_mm_load_ps(node.min_y), _mm_load_ps(node.max_y), p0.y, ray.y);
	apply_axis(_mm_load_ps(node.min_z), _mm_load_ps(node.max_z), p0.z, ray.z);
	return static_cast<uint32_t>(_mm_movemask_ps(valid));
#else
	uint32_t mask = 0;
	for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
		if (!mesh_bvh_child_empty(node, slot) && mesh_bvh_child_overlaps_segment(node, slot, p0, p1)) {
			mask |= 1u << slot;
		}
	}
	return mask;
#endif
}

static float mesh_bvh_child_distance2_to_point(const TrackMeshBVHNode &node, int slot, const SimVec3 &p)
{
	float d2 = 0.0f;
	float delta = 0.0f;
	if (p.x < node.min_x[slot]) {
		delta = node.min_x[slot] - p.x;
		d2 += delta * delta;
	} else if (p.x > node.max_x[slot]) {
		delta = p.x - node.max_x[slot];
		d2 += delta * delta;
	}
	if (p.y < node.min_y[slot]) {
		delta = node.min_y[slot] - p.y;
		d2 += delta * delta;
	} else if (p.y > node.max_y[slot]) {
		delta = p.y - node.max_y[slot];
		d2 += delta * delta;
	}
	if (p.z < node.min_z[slot]) {
		delta = node.min_z[slot] - p.z;
		d2 += delta * delta;
	} else if (p.z > node.max_z[slot]) {
		delta = p.z - node.max_z[slot];
		d2 += delta * delta;
	}
	return d2;
}

static void mesh_bvh_child_distance2_quad(const TrackMeshBVHNode &node, const SimVec3 &p, float out_dist2[MXT_MESH_BVH_WIDTH])
{
#if defined(__SSE__)
	const __m128 px = _mm_set1_ps(p.x);
	const __m128 py = _mm_set1_ps(p.y);
	const __m128 pz = _mm_set1_ps(p.z);
	const __m128 min_x = _mm_load_ps(node.min_x);
	const __m128 min_y = _mm_load_ps(node.min_y);
	const __m128 min_z = _mm_load_ps(node.min_z);
	const __m128 max_x = _mm_load_ps(node.max_x);
	const __m128 max_y = _mm_load_ps(node.max_y);
	const __m128 max_z = _mm_load_ps(node.max_z);
	const __m128 zero = _mm_setzero_ps();
	const __m128 dx = _mm_max_ps(_mm_max_ps(_mm_sub_ps(min_x, px), _mm_sub_ps(px, max_x)), zero);
	const __m128 dy = _mm_max_ps(_mm_max_ps(_mm_sub_ps(min_y, py), _mm_sub_ps(py, max_y)), zero);
	const __m128 dz = _mm_max_ps(_mm_max_ps(_mm_sub_ps(min_z, pz), _mm_sub_ps(pz, max_z)), zero);
	const __m128 d2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dy, dy)), _mm_mul_ps(dz, dz));
	_mm_storeu_ps(out_dist2, d2);
#else
	for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
		out_dist2[slot] = mesh_bvh_child_empty(node, slot) ? FLT_MAX : mesh_bvh_child_distance2_to_point(node, slot, p);
	}
#endif
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
	const SimVec3 &side_reference_point,
	bool allow_backface,
	float *out_t,
	float *out_u,
	float *out_v,
	float *out_w,
	SimVec3 *out_face_normal,
	bool *out_backside_hit,
	MeshCastRejectReason *out_reject_reason = nullptr)
{
	const SimVec3 &face_normal = tri.face_normal;
	const float denom = ray.dot(face_normal);
	if (fabsf(denom) <= 1.0e-7f) {
		if (out_reject_reason) {
			*out_reject_reason = MESH_CAST_REJECT_PARALLEL;
		}
		return false;
	}

	const float signed_start = (side_reference_point - tri.p0).dot(face_normal);
	const bool backside_hit = signed_start < -1.0e-4f || (fabsf(signed_start) <= 1.0e-4f && denom > 0.0f);
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
	const SimVec3 v2 = hit - tri.p0;
	const float d20 = v2.dot(tri.edge0);
	const float d21 = v2.dot(tri.edge1);
	const float v = (tri.projection_d11 * d20 - tri.projection_d01 * d21) * tri.projection_inv_denom;
	const float w = (tri.projection_d00 * d21 - tri.projection_d01 * d20) * tri.projection_inv_denom;
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
	SimVec3 n = tri.n0 * u + tri.n1 * v + tri.n2 * w;
	if (n.length_squared() <= 1.0e-8f) {
		return face_normal;
	}
	return n.normalized();
}

static SimVec3 mesh_collision_phong_point(const TrackMeshCollisionTriangle &tri, float u, float v, float w)
{
	const SimVec3 flat_pos = tri.p0 * u + tri.p1 * v + tri.p2 * w;
	const SimVec3 proj0 = flat_pos - tri.n0 * (flat_pos - tri.p0).dot(tri.n0);
	const SimVec3 proj1 = flat_pos - tri.n1 * (flat_pos - tri.p1).dot(tri.n1);
	const SimVec3 proj2 = flat_pos - tri.n2 * (flat_pos - tri.p2).dot(tri.n2);
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
	return (terrain & (TERRAIN::RAIL | TERRAIN::HOLE | TERRAIN::FALL | TERRAIN::KILL)) == 0;
}

static bool mesh_collision_mask_accepts_surface(uint32_t terrain, uint8_t mask)
{
	if (terrain_mesh_blocks_like_rail(terrain)) {
		return (mask & CAST_FLAGS::WANTS_RAIL) != 0;
	}
	if ((terrain & TERRAIN::FALL) != 0) {
		return (mask & CAST_FLAGS::WANTS_TERRAIN) != 0;
	}
	return (mask & CAST_FLAGS::WANTS_TRACK) != 0;
}

static SimTransform mesh_collision_surface_transform(const TrackMeshCollisionTriangle &tri, const SimVec3 &hit_point, const SimVec3 &normal)
{
	SimVec3 tangent = tri.edge0 - normal * tri.edge0.dot(normal);
	float tangent_len2 = tangent.length_squared();
	if (tangent_len2 <= 1.0e-8f) {
		tangent = tri.edge1 - normal * tri.edge1.dot(normal);
		tangent_len2 = tangent.length_squared();
	}
	if (tangent_len2 > 1.0e-8f) {
		tangent *= 1.0f / sqrtf(tangent_len2);
	} else {
		tangent = SimVec3(1.0f, 0.0f, 0.0f);
	}
	SimVec3 forward = tangent.cross(normal);
	SimBasis basis;
	basis[0] = tangent;
	basis[1] = normal;
	basis[2] = forward;
	return SimTransform(basis, hit_point);
}

static SimTransform mesh_collision_plane_transform(const SimVec3 &hit_point, const SimVec3 &normal)
{
	SimBasis basis;
	basis[0] = SimVec3(1.0f, 0.0f, 0.0f);
	basis[1] = normal;
	basis[2] = SimVec3();
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
	constexpr float kBacksideRecoverySlop = -5.0f;
	constexpr float kBacksideNormalFlipSlop = -0.01f;
	const float signed_face_dist = (p - projected).dot(face_n);
	if (signed_face_dist < kBacksideRecoverySlop && !allow_backside) {
		if (out_projection_result) {
			*out_projection_result = MESH_FLOOR_PROJECT_MISS;
		}
		return false;
	}
	const bool backside_sample = allow_backside && signed_face_dist < kBacksideNormalFlipSlop;
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
		if ((p - projected).dot(face_n) < kBacksideRecoverySlop && !allow_backside) {
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
	constexpr float kBacksideRecoverySlop = -5.0f;
	constexpr float kBacksideNormalFlipSlop = -0.01f;
	if (signed_face_dist < kBacksideRecoverySlop && !allow_backside) {
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
		*out_backside_sample = allow_backside && signed_face_dist < kBacksideNormalFlipSlop;
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
	const TrackMeshCollisionTriangle &tri = track->mesh_collision_triangles[tri_index];
	if (!params.track_only_query) {
		if (!mesh_collision_mask_accepts_surface(tri.terrain, params.mask)) {
			return false;
		}
	}
	if (params.draw_cast_tests) {
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
	const SimVec3 &side_reference_point = params.mesh_side_reference_point ? *params.mesh_side_reference_point : p0;
	if (!triangle_ray_hit(tri, p0, ray, side_reference_point, allow_backside, &hit_t, &u, &v, &w, &face_normal, &backside_hit, &reject_reason)) {
		return false;
	}
	const float dist = hit_t * ray_len;
	if (dist >= best_dist) {
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
	if (start_idx < 0 || start_idx >= track->num_checkpoints) {
		godot::UtilityFunctions::printerr(godot::String("MXT mesh collision query has invalid checkpoint index "), start_idx);
		std::abort();
	}

	best_dist = dist;
	if (params.draw_collision_hits) {
		draw_mesh_debug_triangle(tri, godot::Color(0.2f, 1.0f, 0.15f, 0.95f), _TICK_DELTA);
	}
	out_collision.collided = true;
	out_collision.collision_point = hit_point;
	out_collision.collision_normal = hit_normal;
	out_collision.collision_face_point = flat_point;
	out_collision.collision_face_normal = hit_face_normal;
	out_collision.road_data.cp_idx = start_idx;
	out_collision.road_data.spatial_t = SimVec3();
	out_collision.road_data.road_t = SimVec2(0.0f, 0.5f);
	out_collision.road_data.closest_surface = params.smooth_mesh_hits
		? (params.build_surface_basis ? mesh_collision_surface_transform(tri, hit_point, hit_normal) : mesh_collision_plane_transform(hit_point, hit_normal))
		: SimTransform();
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
	if (track->num_mesh_collision_triangles <= 0) {
		return;
	}
	if (start_idx < 0 || start_idx >= track->num_checkpoints) {
		godot::UtilityFunctions::printerr(godot::String("MXT mesh cast query has invalid checkpoint index "), start_idx);
		std::abort();
	}
	const bool track_only_query =
		(params.mask & CAST_FLAGS::WANTS_TRACK) != 0 &&
		(params.mask & (CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0;
	const TrackMeshBVHNode *bvh_nodes = track_only_query ? track->mesh_floor_bvh_nodes : track->mesh_world_bvh_nodes;
	const int32_t *bvh_triangle_indices = track_only_query ? track->mesh_floor_bvh_triangle_indices : track->mesh_world_bvh_triangle_indices;
	const int num_bvh_nodes = track_only_query ? track->num_mesh_floor_bvh_nodes : track->num_mesh_world_bvh_nodes;
	if (!bvh_nodes || !bvh_triangle_indices || num_bvh_nodes <= 0) {
		return;
	}
	const SimVec3 ray = p1 - p0;
	const float ray_len = ray.length();
	if (ray_len <= 1.0e-6f) {
		return;
	}

	float best_dist = out_collision.collided ? p0.distance_to(out_collision.collision_point) : FLT_MAX;
	auto scan_triangle = [&](int tri_index) {
		scan_mesh_cast_triangle(params, out_collision, p0, p1, ray, ray_len, start_idx, tri_index, best_dist, scratch);
	};

	int stack[256];
	int stack_count = 0;
	stack[stack_count++] = 0;
	while (stack_count > 0) {
		const int node_index = stack[--stack_count];
		const TrackMeshBVHNode &node = bvh_nodes[node_index];
		uint32_t child_mask = mesh_bvh_child_segment_mask(node, p0, p1);
		for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
			if ((child_mask & (1u << slot)) == 0) {
				continue;
			}
			if (mesh_bvh_child_is_leaf(node, slot)) {
				const int end = node.child[slot] + node.count[slot];
				for (int i = node.child[slot]; i < end; ++i) {
					scan_triangle(bvh_triangle_indices[i]);
				}
			} else {
				if (stack_count + 1 > 256) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world cast BVH traversal stack overflow"));
					std::abort();
				}
				if (node.child[slot] < 0) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world cast BVH interior node has invalid child"));
					std::abort();
				}
				stack[stack_count++] = node.child[slot];
			}
		}
	}
}

bool RaceTrack::collect_mesh_cast_candidates(const SimAABB &bounds, uint8_t mask, TrackQueryScratch &scratch)
{
	scratch.mesh_cast_candidate_count = 0;
	const bool track_only_query =
		(mask & CAST_FLAGS::WANTS_TRACK) != 0 &&
		(mask & (CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0;
	const TrackMeshBVHNode *bvh_nodes = track_only_query ? mesh_floor_bvh_nodes : mesh_world_bvh_nodes;
	const int32_t *bvh_triangle_indices = track_only_query ? mesh_floor_bvh_triangle_indices : mesh_world_bvh_triangle_indices;
	const int num_bvh_nodes = track_only_query ? num_mesh_floor_bvh_nodes : num_mesh_world_bvh_nodes;
	if (!bvh_nodes || !bvh_triangle_indices || num_bvh_nodes <= 0) {
		return false;
	}

	int stack[256];
	int stack_count = 0;
	stack[stack_count++] = 0;
	while (stack_count > 0) {
		const int node_index = stack[--stack_count];
		const TrackMeshBVHNode &node = bvh_nodes[node_index];
		uint32_t child_mask = mesh_bvh_child_aabb_mask(node, bounds);
		for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
			if ((child_mask & (1u << slot)) == 0) {
				continue;
			}
			if (mesh_bvh_child_is_leaf(node, slot)) {
				const int end = node.child[slot] + node.count[slot];
				for (int i = node.child[slot]; i < end; ++i) {
					const int tri_index = bvh_triangle_indices[i];
					const TrackMeshCollisionTriangle &tri = mesh_collision_triangles[tri_index];
					if (!track_only_query) {
						if (!mesh_collision_mask_accepts_surface(tri.terrain, mask)) {
							continue;
						}
					}
					if (!aabb_overlaps_aabb(tri.bounds, bounds)) {
						continue;
					}
					if (scratch.mesh_cast_candidate_count >= TrackQueryScratch::MAX_MESH_CAST_CANDIDATES) {
						godot::UtilityFunctions::printerr(godot::String("MXT mesh cast candidate list overflow"));
						std::abort();
					}
					scratch.mesh_cast_candidate_indices[scratch.mesh_cast_candidate_count++] = tri_index;
				}
			} else {
				if (stack_count + 1 > 256) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world candidate BVH traversal stack overflow"));
					std::abort();
				}
				if (node.child[slot] < 0) {
					godot::UtilityFunctions::printerr(godot::String("MXT mesh world candidate BVH interior node has invalid child"));
					std::abort();
				}
				stack[stack_count++] = node.child[slot];
			}
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
	bool smooth_mesh_hits,
	const SimVec3 *mesh_side_reference_point,
	bool build_surface_basis)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();
	if (!scratch || start_idx < 0 || start_idx >= num_checkpoints || num_mesh_collision_triangles <= 0) {
		return;
	}
	if ((mask & (CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0) {
		return;
	}
	const SimVec3 ray = p1 - p0;
	const float ray_len = ray.length();
	if (ray_len <= 1.0e-6f) {
		return;
	}

	float best_dist = FLT_MAX;
	const bool track_only_query = (mask & CAST_FLAGS::WANTS_TRACK) != 0 &&
		(mask & (CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0;
	const bool debug_current_car = mesh_debug_draw_current_car(scratch);
	CastParams params{
		this,
		mask,
		smooth_mesh_hits,
		track_only_query,
		debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS),
		debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS),
		build_surface_basis,
		mesh_side_reference_point
	};
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

void RaceTrack::cast_vs_mesh_candidates4_same_ray_fast(
	CollisionData out_collision[4],
	const SimVec3 p0[4],
	const SimVec3 p1[4],
	uint8_t mask,
	int start_idx,
	TrackQueryScratch *scratch,
	bool smooth_mesh_hits,
	bool build_surface_basis)
{
	for (int lane = 0; lane < 4; ++lane) {
		out_collision[lane].collided = false;
		out_collision[lane].road_data.cp_idx = -1;
		out_collision[lane].mesh_triangle_index = -1;
		out_collision[lane].collision_face_point = SimVec3();
		out_collision[lane].collision_face_normal = SimVec3();
	}
	if (!scratch || start_idx < 0 || start_idx >= num_checkpoints || num_mesh_collision_triangles <= 0) {
		return;
	}
	if ((mask & (CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0) {
		return;
	}
	const SimVec3 ray = p1[0] - p0[0];
	const float ray_len = ray.length();
	if (ray_len <= 1.0e-6f) {
		return;
	}

	float best_dist[4] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
	const bool track_only_query = (mask & CAST_FLAGS::WANTS_TRACK) != 0 &&
		(mask & (CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0;
	const bool debug_current_car = mesh_debug_draw_current_car(scratch);
	const bool draw_cast_tests = debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS);
	const bool draw_collision_hits = debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS);

	for (int candidate = 0; candidate < scratch->mesh_cast_candidate_count; ++candidate) {
		const int tri_index = scratch->mesh_cast_candidate_indices[candidate];
		const TrackMeshCollisionTriangle &tri = mesh_collision_triangles[tri_index];
		if (!track_only_query) {
			if (!mesh_collision_mask_accepts_surface(tri.terrain, mask)) {
				continue;
			}
		}
		if (draw_cast_tests) {
			const bool rail_query = (mask & CAST_FLAGS::WANTS_RAIL) != 0;
			const bool terrain_query = (mask & CAST_FLAGS::WANTS_TERRAIN) != 0;
			const godot::Color color = rail_query
				? godot::Color(1.0f, 0.0f, 1.0f, 0.75f)
				: (terrain_query ? godot::Color(1.0f, 0.85f, 0.1f, 0.75f) : godot::Color(1.0f, 0.45f, 0.05f, 0.75f));
			draw_mesh_debug_triangle(tri, color, _TICK_DELTA);
		}
		const bool allow_backside = (tri.terrain & TERRAIN::BACKSIDE) != 0;
		for (int lane = 0; lane < 4; ++lane) {
			float hit_t = 0.0f;
			float u = 0.0f;
			float v = 0.0f;
			float w = 0.0f;
			SimVec3 face_normal;
			bool backside_hit = false;
			if (!triangle_ray_hit(tri, p0[lane], ray, p0[lane], allow_backside, &hit_t, &u, &v, &w, &face_normal, &backside_hit, nullptr)) {
				continue;
			}
			const float dist = hit_t * ray_len;
			if (dist >= best_dist[lane]) {
				continue;
			}

			const SimVec3 flat_point = p0[lane] + ray * hit_t;
			SimVec3 hit_normal = face_normal;
			SimVec3 hit_face_normal = face_normal;
			SimVec3 hit_point = flat_point;
			if (smooth_mesh_hits && mesh_collision_uses_smooth_surface(tri.terrain)) {
				hit_normal = mesh_collision_smooth_normal(tri, u, v, w, face_normal);
				hit_point = clamp_mesh_collision_phong_point(flat_point, mesh_collision_phong_point(tri, u, v, w));
			}
			if (backside_hit) {
				hit_normal *= -1.0f;
				hit_face_normal *= -1.0f;
			}

			best_dist[lane] = dist;
			out_collision[lane].collided = true;
			out_collision[lane].collision_point = hit_point;
			out_collision[lane].collision_normal = hit_normal;
			out_collision[lane].collision_face_point = flat_point;
			out_collision[lane].collision_face_normal = hit_face_normal;
			out_collision[lane].road_data.cp_idx = start_idx;
			out_collision[lane].road_data.spatial_t = SimVec3();
			out_collision[lane].road_data.road_t = SimVec2(0.0f, 0.5f);
			out_collision[lane].road_data.closest_surface = smooth_mesh_hits
				? (build_surface_basis ? mesh_collision_surface_transform(tri, hit_point, hit_normal) : mesh_collision_plane_transform(hit_point, hit_normal))
				: SimTransform();
			out_collision[lane].road_data.closest_root = RoadTransform();
			out_collision[lane].road_data.terrain = static_cast<uint16_t>(tri.terrain);
			out_collision[lane].mesh_triangle_index = tri_index;
			if (draw_collision_hits) {
				draw_mesh_debug_triangle(tri, godot::Color(0.2f, 1.0f, 0.15f, 0.95f), _TICK_DELTA);
			}
		}
	}
}

void RaceTrack::cast_vs_mesh_fast(
	CollisionData &out_collision,
	const SimVec3 &p0,
	const SimVec3 &p1,
	uint8_t mask,
	int start_idx,
	TrackQueryScratch *scratch,
	bool smooth_mesh_hits,
	const SimVec3 *mesh_side_reference_point,
	bool build_surface_basis)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();
	if ((mask & (CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0) {
		return;
	}
	if (start_idx < 0 || start_idx >= num_checkpoints || num_mesh_collision_triangles <= 0) {
		return;
	}

	const bool track_only_query = (mask & CAST_FLAGS::WANTS_TRACK) != 0 &&
		(mask & (CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0;
	const bool debug_current_car = mesh_debug_draw_current_car(scratch);
	CastParams params{
		this,
		mask,
		smooth_mesh_hits,
		track_only_query,
		debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS),
		debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS),
		build_surface_basis,
		mesh_side_reference_point
	};
	cast_mesh_collision_fast(params, out_collision, p0, p1, start_idx, scratch);
}

void RaceTrack::sample_mesh_floor_fast(CollisionData &out_collision, const SimVec3 &point, float max_distance, uint8_t mask, int start_idx, bool allow_global_fallback, TrackQueryScratch *scratch, int seed_triangle_index, bool build_surface, bool build_surface_basis)
{
	(void)allow_global_fallback;
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();
	if (start_idx < 0 || start_idx >= num_checkpoints || num_mesh_collision_triangles <= 0) {
		return;
	}
	if ((mask & CAST_FLAGS::WANTS_TRACK) == 0) {
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
	const bool debug_current_car = mesh_debug_draw_current_car(scratch);
	const bool draw_floor_tests = debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_FLOOR_TESTS);
	const bool draw_collision_hits = debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS);

	auto scan_triangle = [&](int tri_index) {
		const TrackMeshCollisionTriangle &tri = mesh_collision_triangles[tri_index];
		if (!terrain_mesh_has_floor_response(tri.terrain)) {
			return;
		}
		if (distance2_to_aabb(tri.bounds, point) > best_dist2) {
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
				return;
			}
			dist2 = (point - projected).length_squared();
		} else if (project_point_to_mesh_triangle_face_fast(tri, point, allow_backside, &signed_face_dist, &u, &v, &w, &backside_sample, &smooth_retry_candidate)) {
			projection_result = MESH_FLOOR_PROJECT_FACE;
			dist2 = signed_face_dist * signed_face_dist;
		} else {
			saw_smooth_projection_candidate |= smooth_retry_candidate;
			return;
		}
		if (draw_floor_tests) {
			draw_mesh_debug_triangle(tri, godot::Color(0.1f, 0.8f, 1.0f, 0.7f), _TICK_DELTA);
		}
		if (dist2 >= best_dist2) {
			return;
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
		if (draw_collision_hits) {
			draw_mesh_debug_triangle(tri, godot::Color(0.1f, 1.0f, 0.45f, 0.95f), _TICK_DELTA);
		}
	};

	auto scan_floor_bvh_root = [&](int root_node_index) {
		if (!mesh_floor_bvh_nodes || !mesh_floor_bvh_triangle_indices || num_mesh_floor_bvh_nodes <= 0) {
			return false;
		}
		int stack[256];
		int stack_count = 0;
		stack[stack_count++] = root_node_index;
		while (stack_count > 0) {
			const int node_index = stack[--stack_count];
			const TrackMeshBVHNode &node = mesh_floor_bvh_nodes[node_index];
			float child_dist2[MXT_MESH_BVH_WIDTH];
			mesh_bvh_child_distance2_quad(node, point, child_dist2);
			int ordered_slots[MXT_MESH_BVH_WIDTH];
			int ordered_count = 0;
			for (int slot = 0; slot < MXT_MESH_BVH_WIDTH; ++slot) {
				if (child_dist2[slot] > best_dist2) {
					continue;
				}
				int insert_at = ordered_count;
				while (insert_at > 0 && child_dist2[slot] < child_dist2[ordered_slots[insert_at - 1]]) {
					ordered_slots[insert_at] = ordered_slots[insert_at - 1];
					--insert_at;
				}
				ordered_slots[insert_at] = slot;
				++ordered_count;
			}
			for (int order = ordered_count - 1; order >= 0; --order) {
				const int slot = ordered_slots[order];
				if (mesh_bvh_child_is_leaf(node, slot)) {
					const int end = node.child[slot] + node.count[slot];
					for (int i = node.child[slot]; i < end; ++i) {
						scan_triangle(mesh_floor_bvh_triangle_indices[i]);
					}
				} else {
					if (stack_count + 1 > 256) {
						godot::UtilityFunctions::printerr(godot::String("MXT mesh floor BVH traversal stack overflow"));
						std::abort();
					}
					if (node.child[slot] < 0) {
						godot::UtilityFunctions::printerr(godot::String("MXT mesh floor BVH interior node has invalid child"));
						std::abort();
					}
					stack[stack_count++] = node.child[slot];
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
			scan_triangle(seed_triangle_index);
			if (best_tri_index == seed_triangle_index && best_dist2 <= max_dist2) {
				return;
			}
		}

		if (scan_floor_bvh_root(0)) {
			return;
		}
	};
	scan_mesh_floor_candidates();
	if (!best_tri && saw_smooth_projection_candidate) {
		allow_smooth_projection_retry = true;
		scan_mesh_floor_candidates();
	}

	if (!best_tri) {
		return;
	}

	if (!build_surface) {
		out_collision.collided = true;
		out_collision.collision_point = best_flat_point;
		out_collision.collision_normal = best_backside_sample ? best_tri->face_normal * -1.0f : best_tri->face_normal;
		out_collision.collision_face_point = best_flat_point;
		out_collision.collision_face_normal = out_collision.collision_normal;
		out_collision.road_data.cp_idx = start_idx;
		out_collision.road_data.spatial_t = SimVec3();
		out_collision.road_data.road_t = SimVec2(0.0f, 0.5f);
		out_collision.road_data.closest_surface = SimTransform();
		out_collision.road_data.closest_root = RoadTransform();
		out_collision.road_data.terrain = static_cast<uint16_t>(best_tri->terrain);
		out_collision.mesh_triangle_index = best_tri_index;
		return;
	}

	SimVec3 face_normal = best_tri->face_normal;
	SimVec3 smooth_normal = face_normal;
	SimVec3 smooth_point = best_flat_point;
	if (mesh_collision_uses_smooth_surface(best_tri->terrain)) {
		smooth_normal = mesh_collision_smooth_normal(*best_tri, best_u, best_v, best_w, face_normal);
		smooth_point = clamp_mesh_collision_phong_point(best_flat_point, mesh_collision_phong_point(*best_tri, best_u, best_v, best_w));
	}
	if (best_backside_sample) {
		smooth_normal *= -1.0f;
		face_normal *= -1.0f;
	}
	if (start_idx < 0 || start_idx >= num_checkpoints) {
		godot::UtilityFunctions::printerr(godot::String("MXT mesh floor query has invalid checkpoint index "), start_idx);
		std::abort();
	}

	out_collision.collided = true;
	out_collision.collision_point = smooth_point;
	out_collision.collision_normal = smooth_normal;
	out_collision.collision_face_point = best_flat_point;
	out_collision.collision_face_normal = face_normal;
	out_collision.road_data.cp_idx = start_idx;
	out_collision.road_data.spatial_t = SimVec3();
	out_collision.road_data.road_t = SimVec2(0.0f, 0.5f);
	out_collision.road_data.closest_surface = build_surface_basis
		? mesh_collision_surface_transform(*best_tri, smooth_point, smooth_normal)
		: mesh_collision_plane_transform(smooth_point, smooth_normal);
	out_collision.road_data.closest_root = RoadTransform();
	out_collision.road_data.terrain = static_cast<uint16_t>(best_tri->terrain);
	out_collision.mesh_triangle_index = best_tri_index;
}

void RaceTrack::cast_vs_track_fast(CollisionData &out_collision,
	SimVec3 const &p0,
	SimVec3 const &p1,
	uint8_t mask,
	int start_idx, bool oriented, TrackQueryScratch *scratch, bool smooth_mesh_hits, const SimVec3 *mesh_side_reference_point, bool build_surface_basis)
{
	out_collision.collided = false;
	out_collision.road_data.cp_idx = -1;
	out_collision.mesh_triangle_index = -1;
	out_collision.collision_face_point = SimVec3();
	out_collision.collision_face_normal = SimVec3();

	if ((mask & (CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0)
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
	
	const bool track_only_query = (mask & CAST_FLAGS::WANTS_TRACK) != 0 &&
		(mask & (CAST_FLAGS::WANTS_RAIL | CAST_FLAGS::WANTS_TERRAIN)) == 0;
	const bool debug_current_car = mesh_debug_draw_current_car(scratch);
	CastParams params{
		this,
		mask,
		smooth_mesh_hits,
		track_only_query,
		debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS),
		debug_current_car && DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS),
		build_surface_basis,
		mesh_side_reference_point
	};
	cast_segment_fast(params, out_collision, p0, p1, start_idx, sample_point, true);
	cast_mesh_collision_fast(params, out_collision, p0, p1, start_idx, scratch);
}
