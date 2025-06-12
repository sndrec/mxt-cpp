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
                                  godot::Vector2 &road_t, godot::Vector3 &spatial_t)
{
    const CollisionCheckpoint &cp = track->checkpoints[cp_idx];
    godot::Vector3 p1 = cp.start_plane.project(point);
    godot::Vector3 p2 = cp.end_plane.project(point);
    float cp_t = get_closest_t_on_segment(point, p1, p2);
    godot::Basis basis;
    basis[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t).normalized();
    basis[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t).normalized();
    basis[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t).normalized();
    godot::Vector3 midpoint = cp.position_start.lerp(cp.position_end, cp_t);
    godot::Plane sep_x_plane(basis[0], midpoint);
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

