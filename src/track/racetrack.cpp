#include "track/racetrack.h"
#include "car/physics_car.h" // for CollisionData and RoadData
#include <cfloat>
#include <algorithm>
#include "mxt_core/enums.h"

int RaceTrack::find_checkpoint_recursive(const godot::Vector3 &pos, int cp_index, int iterations) const
{
    if (iterations > 10)
        return -1;
    const CollisionCheckpoint &cp = checkpoints[cp_index];
    if (!cp.end_plane.is_point_over(pos) && cp.start_plane.is_point_over(pos))
        return cp_index;
    for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
        int neighbor = cp.neighboring_checkpoints[i];
        int found = find_checkpoint_recursive(pos, neighbor, iterations + 1);
        if (found != -1)
            return found;
    }
    return -1;
}

static void convert_point_to_road(const RaceTrack *track, int cp_idx, const godot::Vector3 &point,
                                  godot::Vector2 &road_t, godot::Vector3 &spatial_t, float *out_cp_t = nullptr)
{
    const CollisionCheckpoint &cp = track->checkpoints[cp_idx];
    godot::Vector3 p1 = cp.start_plane.project(point);
    godot::Vector3 p2 = cp.end_plane.project(point);
    float cp_t = get_closest_t_on_segment(point, p1, p2);
    if (out_cp_t)
        *out_cp_t = cp_t;
    float cp_t_clamped = fminf(fmaxf(cp_t, 0.0f), 1.0f);
    godot::Basis basis;
    basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t_clamped).normalized();
    basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t_clamped).normalized();
    basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t_clamped).normalized();
    godot::Vector3 midpoint = cp.position_start.lerp(cp.position_end, cp_t_clamped);
    godot::Plane sep_x_plane(basis[0], midpoint);
    godot::Plane sep_y_plane(basis.get_column(1), midpoint);
    float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t_clamped);
    float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t_clamped);
    float tx = sep_x_plane.distance_to(point) * x_r;
    float ty = sep_y_plane.distance_to(point) * y_r;
    float tz = remap_float(cp_t_clamped, 0.0f, 1.0f, cp.t_start, cp.t_end);
    spatial_t = godot::Vector3(tx, ty, tz);
    track->segments[cp.road_segment].road_shape->find_t_from_relative_pos(road_t, spatial_t);
}

struct CastParams {
    RaceTrack *track;
    uint8_t mask;
};

static void cast_segment(const CastParams &params, CollisionData &out_collision,
                         const godot::Vector3 &p0, const godot::Vector3 &p1,
                         int start_idx)
{
    out_collision.collided = false;
    out_collision.road_data.cp_idx = -1;

    if ((params.mask & CAST_FLAGS::WANTS_TRACK) == 0)
        return;

    RaceTrack *track = params.track;
    std::vector<int> checkpoints_to_test;
    auto add_cp = [&checkpoints_to_test](int idx) {
        if (idx < 0)
            return;
        if (std::find(checkpoints_to_test.begin(), checkpoints_to_test.end(), idx) == checkpoints_to_test.end())
            checkpoints_to_test.push_back(idx);
    };

    if (start_idx == -1) {
        checkpoints_to_test = track->get_viable_checkpoints(p0);
    } else {
        int cp0 = track->find_checkpoint_recursive(p0, start_idx);
        add_cp(cp0);
    }

    if (checkpoints_to_test.empty())
        return;

    float best_t = FLT_MAX;
    godot::Vector3 ray = p1 - p0;

    for (int cp_idx : checkpoints_to_test) {
        const TrackSegment &segment = track->segments[track->checkpoints[cp_idx].road_segment];
        godot::Vector2 road_t0_raw, road_t1_raw;
        godot::Vector3 spatial_t0, spatial_t1;
        convert_point_to_road(track, cp_idx, p0, road_t0_raw, spatial_t0);
        convert_point_to_road(track, cp_idx, p1, road_t1_raw, spatial_t1);

        godot::Vector2 road_t0 = road_t0_raw;
        godot::Vector2 road_t1 = road_t1_raw;
        road_t0.x = fminf(fmaxf(road_t0.x, -1.0f), 1.0f);
        road_t1.x = fminf(fmaxf(road_t1.x, -1.0f), 1.0f);

        godot::Transform3D surf0, surf1;
        segment.road_shape->get_oriented_transform_at_time(surf0, road_t0);
        segment.road_shape->get_oriented_transform_at_time(surf1, road_t1);
        float d0 = (p0 - surf0.origin).dot(surf0.basis[1]);
        float d1 = (p1 - surf1.origin).dot(surf1.basis[1]);

        if ((d0 <= 0.0f && d1 <= 0.0f) || (d0 >= 0.0f && d1 >= 0.0f)) {
        } else {
            float t = d0 / (d0 - d1);
            if (t >= 0.0f && t <= 1.0f) {
                godot::Vector3 hit_point = p0 + ray * t;
                godot::Vector2 road_t_hit_raw; godot::Vector3 spatial_t_hit;
                convert_point_to_road(track, cp_idx, hit_point, road_t_hit_raw, spatial_t_hit);
                godot::Vector2 road_t_hit = road_t_hit_raw;
                road_t_hit.x = fminf(fmaxf(road_t_hit.x, -1.0f), 1.0f);
                godot::Transform3D surf_hit;
                segment.road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
                godot::Vector3 normal = surf_hit.basis[1];
                if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(normal) <= 0.0f) {
                    float dist = t * ray.length();
                    if (dist < best_t) {
                        best_t = dist;
                        out_collision.collided = true;
                        out_collision.collision_point = hit_point;
                        out_collision.collision_normal = normal;
                        out_collision.road_data.cp_idx = cp_idx;
                        out_collision.road_data.spatial_t = spatial_t_hit;
                        out_collision.road_data.road_t = road_t_hit_raw;
                        segment.road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
                        segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit.y);
                        out_collision.road_data.terrain = 0;
                        if (params.mask & CAST_FLAGS::WANTS_TERRAIN) {
                            for (int i = 0; i < segment.road_shape->num_embeds; ++i) {
                                RoadEmbed *embed = &segment.road_shape->road_embeds[i];
                                if (road_t_hit_raw.y > embed->start_offset && road_t_hit_raw.y < embed->end_offset) {
                                    float et = (road_t_hit_raw.y - embed->start_offset) / (embed->end_offset - embed->start_offset);
                                    float l = embed->left_border->sample(et);
                                    float r = embed->right_border->sample(et);
                                    if (road_t_hit_raw.x < l && road_t_hit_raw.x > r) {
                                        out_collision.road_data.terrain = embed->embed_type;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!(params.mask & CAST_FLAGS::WANTS_RAIL))
            continue;

        godot::Transform3D root_t;
        segment.curve_matrix->sample(root_t, (road_t0_raw.y + road_t1_raw.y) * 0.5f);
        godot::Basis rbasis = root_t.basis.transposed();
        const godot::Vector3 left_pos = root_t.origin + rbasis[0];
        const godot::Vector3 right_pos = root_t.origin - rbasis[0];
        const godot::Vector3 left_plane_n = -rbasis[0].normalized();
        const godot::Vector3 right_plane_n = rbasis[0].normalized();

        struct RailSide { godot::Vector3 pos; godot::Vector3 plane_n; godot::Vector3 rail_n; float height; } sides[2] = {
            { left_pos, left_plane_n, -surf0.basis[1].cross(surf0.basis[2]), segment.left_rail_height },
            { right_pos, right_plane_n, surf0.basis[1].cross(surf0.basis[2]), segment.right_rail_height }
        };

        for (const auto &side : sides) {
            float ra = (p0 - side.pos).dot(side.plane_n);
            float rb = (p1 - side.pos).dot(side.plane_n);
            if ((ra <= 0.0f && rb <= 0.0f) || (ra >= 0.0f && rb >= 0.0f))
                continue;
            float t = ra / (ra - rb);
            if (t < 0.0f || t > 1.0f)
                continue;
            godot::Vector3 hit = p0 + ray * t;
            godot::Vector2 road_t_hit_raw; godot::Vector3 spatial_t_hit;
            convert_point_to_road(track, cp_idx, hit, road_t_hit_raw, spatial_t_hit);
            godot::Vector2 road_t_hit = road_t_hit_raw;
            road_t_hit.x = fminf(fmaxf(road_t_hit.x, -1.0f), 1.0f);
            godot::Transform3D surf_hit;
            segment.road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
            float vdist = (hit - surf_hit.origin).dot(surf_hit.basis[1]);
            if (vdist < 0.0f || vdist > side.height)
                continue;
            if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(side.rail_n) > 0.0f)
                continue;
            float dist = t * ray.length();
            if (dist < best_t) {
                best_t = dist;
                out_collision.collided = true;
                out_collision.collision_point = hit;
                out_collision.collision_normal = side.rail_n;
                out_collision.road_data.cp_idx = cp_idx;
                out_collision.road_data.spatial_t = spatial_t_hit;
                out_collision.road_data.road_t = road_t_hit_raw;
                segment.road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
                segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit.y);
                out_collision.road_data.terrain = 0;
            }
        }
    }
}

void RaceTrack::cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx)
{
    out_collision.collided = false;
    out_collision.road_data.cp_idx = -1;

    if ((mask & CAST_FLAGS::WANTS_TRACK) == 0)
        return;

    const float step_len = 0.5f;
    const float epsilon = 0.05f;

    godot::Vector3 ray = p1 - p0;
    float total_len = ray.length();
    if (total_len == 0.0f)
        return;
    godot::Vector3 dir = ray / total_len;

    CastParams params{ this, mask };

    godot::Vector3 cur = p0;
    float travelled = 0.0f;
    int cp_idx = start_idx;

    if (cp_idx != -1)
        cp_idx = find_checkpoint_recursive(cur, cp_idx);

    while (travelled < total_len) {
        float seg = fminf(step_len, total_len - travelled);
        godot::Vector3 next = cur + dir * seg;
        CollisionData hit;
        cast_segment(params, hit, cur, next, cp_idx);
        if (hit.collided) {
            godot::Vector3 back = hit.collision_point - dir * epsilon;
            cast_segment(params, hit, back, hit.collision_point + dir * epsilon * 2, hit.road_data.cp_idx);
            if (hit.collided) {
                out_collision = hit;
                return;
            }
        }
        cur = next;
        travelled += seg;
        if (cp_idx != -1)
            cp_idx = find_checkpoint_recursive(cur, cp_idx);
    }
}
