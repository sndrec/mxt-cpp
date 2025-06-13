#include "track/racetrack.h"
#include "car/physics_car.h" // for CollisionData and RoadData
#include <cfloat>
#include <algorithm>
#include <cmath>
    cp_t = fminf(fmaxf(cp_t, 0.0f), 1.0f);
    tx = fminf(fmaxf(tx, -1.0f), 1.0f);
    ty = fminf(fmaxf(ty, -1.0f), 1.0f);
    auto add_cp = [&checkpoints_to_test](int idx) {
        if (idx < 0)
            return;
        if (std::find(checkpoints_to_test.begin(), checkpoints_to_test.end(), idx) == checkpoints_to_test.end())
            checkpoints_to_test.push_back(idx);
    };

        int cp0 = find_checkpoint_recursive(p0, start_idx);
        int cp1 = find_checkpoint_recursive(p1, start_idx);
        add_cp(cp0);
        add_cp(cp1);
        if (cp0 != -1) {
            const CollisionCheckpoint &cp = checkpoints[cp0];
            for (int i = 0; i < cp.num_neighboring_checkpoints; ++i)
                add_cp(cp.neighboring_checkpoints[i]);
        }
        if (cp1 != -1 && cp1 != cp0) {
            const CollisionCheckpoint &cp = checkpoints[cp1];
            for (int i = 0; i < cp.num_neighboring_checkpoints; ++i)
                add_cp(cp.neighboring_checkpoints[i]);
        }
        const TrackSegment &segment = segments[checkpoints[cp_idx].road_segment];
        segment.road_shape->get_oriented_transform_at_time(surf0, road_t0);
        segment.road_shape->get_oriented_transform_at_time(surf1, road_t1);
        if ((d0 <= 0.0f && d1 <= 0.0f) || (d0 >= 0.0f && d1 >= 0.0f)) {
            // no crossing of road surface
        } else {
            float t = d0 / (d0 - d1);
            if (t >= 0.0f && t <= 1.0f) {
                godot::Vector3 hit_point = p0 + ray * t;
                godot::Vector2 road_t_hit; godot::Vector3 spatial_t_hit;
                convert_point_to_road(this, cp_idx, hit_point, road_t_hit, spatial_t_hit);
                godot::Transform3D surf_hit;
                segment.road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
                godot::Vector3 normal = surf_hit.basis[1];
                if ((mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(normal) <= 0.0f) {
                    float dist = t * ray.length();
                    if (dist < best_t) {
                        best_t = dist;
                        out_collision.collided = true;
                        out_collision.collision_point = hit_point;
                        out_collision.collision_normal = normal;
                        out_collision.road_data.cp_idx = cp_idx;
                        out_collision.road_data.spatial_t = spatial_t_hit;
                        out_collision.road_data.road_t = road_t_hit;
                        segment.road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
                        segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit.y);
                        out_collision.road_data.terrain = 0;
                        if (mask & CAST_FLAGS::WANTS_TERRAIN) {
                            for (int i = 0; i < segment.road_shape->num_embeds; ++i) {
                                RoadEmbed *embed = &segment.road_shape->road_embeds[i];
                                if (road_t_hit.y > embed->start_offset && road_t_hit.y < embed->end_offset) {
                                    float et = (road_t_hit.y - embed->start_offset) / (embed->end_offset - embed->start_offset);
                                    float l = embed->left_border->sample(et);
                                    float r = embed->right_border->sample(et);
                                    if (road_t_hit.x < l && road_t_hit.x > r) {
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
        if (!(mask & CAST_FLAGS::WANTS_RAIL))
        godot::Transform3D root_t;
        segment.curve_matrix->sample(root_t, (road_t0.y + road_t1.y) * 0.5f);
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
            godot::Vector2 road_t_hit; godot::Vector3 spatial_t_hit;
            convert_point_to_road(this, cp_idx, hit, road_t_hit, spatial_t_hit);
            godot::Transform3D surf_hit;
            segment.road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
            float vdist = (hit - surf_hit.origin).dot(surf_hit.basis[1]);
            if (vdist < 0.0f || vdist > side.height)
                continue;
            if ((mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(side.rail_n) > 0.0f)
                continue;
            float dist = t * ray.length();
            if (dist < best_t) {
                best_t = dist;
                out_collision.collided = true;
                out_collision.collision_point = hit;
                out_collision.collision_normal = side.rail_n;
                out_collision.road_data.cp_idx = cp_idx;
                out_collision.road_data.spatial_t = spatial_t_hit;
                out_collision.road_data.road_t = road_t_hit;
                segment.road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
                segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit.y);
                out_collision.road_data.terrain = 0;
            }
    godot::Plane sep_y_plane(basis.get_column(1), midpoint);
    float x_r = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
    float y_r = lerp(cp.y_radius_start_inv, cp.y_radius_end_inv, cp_t);
    float tx = sep_x_plane.distance_to(point) * x_r;
    float ty = sep_y_plane.distance_to(point) * y_r;
    float tz = remap_float(cp_t, 0.0f, 1.0f, cp.t_start, cp.t_end);
    spatial_t = godot::Vector3(tx, ty, tz);
    track->segments[cp.road_segment].road_shape->find_t_from_relative_pos(road_t, spatial_t);
}

void RaceTrack::cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int start_idx)
{
    out_collision.collided = false;
    out_collision.road_data.cp_idx = -1;

    if ((mask & CAST_FLAGS::WANTS_TRACK) == 0)
        return;

    std::vector<int> checkpoints_to_test;
    if (start_idx == -1) {
        checkpoints_to_test = get_viable_checkpoints((p0 + p1) * 0.5f);
    } else {
        int cp = find_checkpoint_recursive(p0, start_idx);
        if (cp == -1)
            cp = find_checkpoint_recursive(p1, start_idx);
        if (cp != -1)
            checkpoints_to_test.push_back(cp);
    }

    if (checkpoints_to_test.empty())
        return;

    float best_t = FLT_MAX;
    godot::Vector3 ray = p1 - p0;

    for (int cp_idx : checkpoints_to_test) {
        godot::Vector2 road_t0, road_t1;
        godot::Vector3 spatial_t0, spatial_t1;
        convert_point_to_road(this, cp_idx, p0, road_t0, spatial_t0);
        convert_point_to_road(this, cp_idx, p1, road_t1, spatial_t1);

        godot::Transform3D surf0, surf1;
        segments[checkpoints[cp_idx].road_segment].road_shape->get_oriented_transform_at_time(surf0, road_t0);
        segments[checkpoints[cp_idx].road_segment].road_shape->get_oriented_transform_at_time(surf1, road_t1);
        float d0 = (p0 - surf0.origin).dot(surf0.basis[1]);
        float d1 = (p1 - surf1.origin).dot(surf1.basis[1]);

        if ((d0 > 0.0f && d1 > 0.0f) || (d0 < 0.0f && d1 < 0.0f))
            continue;

        float t = d0 / (d0 - d1);
        if (t < 0.0f || t > 1.0f)
            continue;

        godot::Vector3 hit_point = p0 + ray * t;
        godot::Vector2 road_t_hit; godot::Vector3 spatial_t_hit;
        convert_point_to_road(this, cp_idx, hit_point, road_t_hit, spatial_t_hit);
        godot::Transform3D surf_hit;
        segments[checkpoints[cp_idx].road_segment].road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
        godot::Vector3 normal = surf_hit.basis[1];
        if ((mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(normal) > 0.0f)
            continue;

        float dist = t * ray.length();
        if (dist < best_t) {
            best_t = dist;
            out_collision.collided = true;
            out_collision.collision_point = hit_point;
            out_collision.collision_normal = normal;
            out_collision.road_data.terrain = 0;
            out_collision.road_data.cp_idx = cp_idx;
            out_collision.road_data.spatial_t = spatial_t_hit;
            out_collision.road_data.road_t = road_t_hit;
            segments[checkpoints[cp_idx].road_segment].road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
            segments[checkpoints[cp_idx].road_segment].curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit.y);
        }
    }
}

