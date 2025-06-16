#include "track/racetrack.h"
#include "car/physics_car.h" // for CollisionData and RoadData
#include <cfloat>
#include <algorithm>
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/enums.h"
#include <queue>
#include <vector>
#include <limits>
#include <functional>
#include "mxt_core/debug.hpp"
#include "track/checkpoint_bvh.h"

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

int RaceTrack::find_checkpoint_bfs(const godot::Vector3 &pos, int start_index) const {
    // BFS setup
    std::queue<std::pair<int,int>> q;
    std::vector<bool> visited(num_checkpoints, false);
    std::vector<int> candidates;

    q.push({ start_index, 0 });
    visited[start_index] = true;

    while (!q.empty()) {
        auto [idx, depth] = q.front();
        q.pop();

        const CollisionCheckpoint &cp = checkpoints[idx];
        bool over_end   = cp.end_plane.is_point_over(pos);
        bool over_start = cp.start_plane.is_point_over(pos);

        // prune backwards branch
        if (idx < start_index && over_end)
            continue;
        // prune forwards branch
        if (idx > start_index && !over_start)
            continue;
        // mark as candidate & prune deeper
        if (!over_end && over_start) {
            candidates.push_back(idx);
            continue;
        }

        // enqueue neighbors up to depth 9
        if (depth < 9) {
            for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
                int nei = cp.neighboring_checkpoints[i];
                if (!visited[nei]) {
                    visited[nei] = true;
                    q.push({ nei, depth + 1 });
                }
            }
        }
    }

    // distance comparison over candidates
    int   best_cp     = -1;
    float best_dist2  = std::numeric_limits<float>::infinity();

    for (int idx : candidates) {
        const CollisionCheckpoint &cp = checkpoints[idx];

        // project pos onto segment
        godot::Vector3 p1    = cp.start_plane.project(pos);
        godot::Vector3 p2    = cp.end_plane.project(pos);
        float           cp_t = get_closest_t_on_segment(pos, p1, p2);

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

        float tx = sep_x.distance_to(pos) * x_r;
        float ty = sep_y.distance_to(pos) * y_r;
        float dist2 = tx * tx + ty * ty;

        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best_cp    = idx;
        }
    }

    return best_cp;
}

void RaceTrack::get_road_surface(int cp_idx, const godot::Vector3 &point,
                                  godot::Vector2 &road_t, godot::Vector3 &spatial_t, godot::Transform3D &out_transform, bool oriented)
{
    CollisionCheckpoint *cp;
    int best = get_best_checkpoint(point);
    if (best == -1){
        return;
    }
    cp = &checkpoints[best];
    godot::Vector3 p1 = cp->start_plane.project(point);
    godot::Vector3 p2 = cp->end_plane.project(point);
    float cp_t = get_closest_t_on_segment(point, p1, p2);
    godot::Basis basis;
    basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t).normalized();
    basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t).normalized();
    basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t).normalized();
    godot::Vector3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
    godot::Plane sep_x_plane(basis[0], midpoint);
    godot::Plane sep_y_plane(basis[1], midpoint);
    float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
    float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);
    float tx = sep_x_plane.distance_to(point) * x_r;
    float ty = sep_y_plane.distance_to(point) * y_r;
    float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);
    spatial_t = godot::Vector3(tx, ty, tz);
    bool y_less_than_x = y_r > x_r;
    bool is_open = false;
    bool use_top_half = false;

    // Check for open road shape
    RoadShape *shape = segments[cp->road_segment].road_shape;
    if (RoadShapeCylinderOpen *cyl_open = dynamic_cast<RoadShapeCylinderOpen *>(shape)) {
        is_open = true;
        use_top_half = true;
    } else if (RoadShapePipeOpen *pipe_open = dynamic_cast<RoadShapePipeOpen *>(shape)) {
        is_open = true;
        use_top_half = false;
    }

    if (is_open && y_less_than_x) {
        float openness = shape->openness->sample(tz);
        if (openness <= 0.5f) {
            float tx_clamped = std::clamp(tx, -1.0f, 1.0f);
            float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
            if (!use_top_half)
                y_val = -y_val;
            spatial_t.y = y_val;
        }
    }
    segments[cp->road_segment].road_shape->find_t_from_relative_pos(road_t, spatial_t);
    segments[cp->road_segment].road_shape->get_oriented_transform_at_time(out_transform, road_t);
}

static void convert_point_to_road(RaceTrack *track, int cp_idx, const godot::Vector3 &point,
                                  godot::Vector2 &road_t, godot::Vector3 &spatial_t, float *out_cp_t = nullptr)
{
    const CollisionCheckpoint *cp;
    int best = track->get_best_checkpoint(point);
    if (best == -1){
        return;
    }
    cp = &track->checkpoints[best];

    godot::Vector3 p1 = cp->start_plane.project(point);
    godot::Vector3 p2 = cp->end_plane.project(point);
    float cp_t = get_closest_t_on_segment(point, p1, p2);
    if (out_cp_t)
        *out_cp_t = cp_t;

    godot::Basis basis;
    basis[0] = cp->orientation_start[0].lerp(cp->orientation_end[0], cp_t).normalized();
    basis[2] = cp->orientation_start[2].lerp(cp->orientation_end[2], cp_t).normalized();
    basis[1] = cp->orientation_start[1].lerp(cp->orientation_end[1], cp_t).normalized();

    godot::Vector3 midpoint = cp->position_start.lerp(cp->position_end, cp_t);
    godot::Plane sep_x_plane(basis[0], midpoint);
    godot::Plane sep_y_plane(basis[1], midpoint);

    float x_r = lerp(cp->x_radius_start_inv, cp->x_radius_end_inv, cp_t);
    float y_r = lerp(cp->y_radius_start_inv, cp->y_radius_end_inv, cp_t);

    float tx = sep_x_plane.distance_to(point) * x_r;
    float ty = sep_y_plane.distance_to(point) * y_r;
    float tz = remap_float(cp_t, 0.0f, 1.0f, cp->t_start, cp->t_end);

    spatial_t = godot::Vector3(tx, ty, tz);

    RoadShape *shape = track->segments[cp->road_segment].road_shape;

    bool y_less_than_x = y_r > x_r;
    bool is_open = false;
    bool use_top_half = false;

    // Check for open road shape
    if (RoadShapeCylinderOpen *cyl_open = dynamic_cast<RoadShapeCylinderOpen *>(shape)) {
        is_open = true;
        use_top_half = true;
    } else if (RoadShapePipeOpen *pipe_open = dynamic_cast<RoadShapePipeOpen *>(shape)) {
        is_open = true;
        use_top_half = false;
    }

    if (is_open && y_less_than_x) {
        float openness = shape->openness->sample(tz);
        if (openness <= 0.5f) {
            float tx_clamped = std::clamp(tx, -1.0f, 1.0f);
            float y_val = sqrtf(1.0f - tx_clamped * tx_clamped);
            if (!use_top_half)
                y_val = -y_val;
            spatial_t.y = y_val;
        }
    }

    shape->find_t_from_relative_pos(road_t, spatial_t);
}


struct CastParams {
    RaceTrack *track;
    uint8_t mask;
};

static void cast_segment(const CastParams &params, CollisionData &out_collision,
                         const godot::Vector3 &p0, const godot::Vector3 &p1,
                         int use_idx, bool oriented = true)
{
    out_collision.collided = false;
    out_collision.road_data.cp_idx = -1;

    if ((params.mask & CAST_FLAGS::WANTS_TRACK) == 0)
        return;
    godot::Object* dd3d;
    if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
        dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
        dd3d->call("draw_arrow", p0, p1, godot::Color(1.0f, 1.0f, 1.0f), 0.25, true, _TICK_DELTA);
    }
    RaceTrack *track = params.track;

    float best_t = FLT_MAX;
    godot::Vector3 ray = p1 - p0;

    const TrackSegment &segment = track->segments[track->checkpoints[use_idx].road_segment];
    godot::Vector2 road_t0_raw, road_t1_raw;
    godot::Vector3 spatial_t0, spatial_t1;
    convert_point_to_road(track, use_idx, p0, road_t0_raw, spatial_t0);
    convert_point_to_road(track, use_idx, p1, road_t1_raw, spatial_t1);

    godot::Vector2 road_t0 = road_t0_raw;
    godot::Vector2 road_t1 = road_t1_raw;
    road_t0.x = fminf(fmaxf(road_t0.x, -1.0f), 1.0f);
    road_t1.x = fminf(fmaxf(road_t1.x, -1.0f), 1.0f);

    godot::Transform3D surf0, surf1;
    if (oriented){
        segment.road_shape->get_oriented_transform_at_time(surf0, road_t0);
        segment.road_shape->get_oriented_transform_at_time(surf1, road_t1);
    }else{
        segment.road_shape->get_transform_at_time(surf0, road_t0);
        segment.road_shape->get_transform_at_time(surf1, road_t1);
    }
    float d0 = (p0 - surf0.origin).dot(surf0.basis[1]);
    float d1 = (p1 - surf1.origin).dot(surf1.basis[1]);

    if ((d0 <= 0.0f && d1 <= 0.0f) || (d0 >= 0.0f && d1 >= 0.0f)) {
    } else {
        float t = d0 / (d0 - d1);
        if (t >= 0.0f && t <= 1.0f) {
            godot::Vector3 hit_point = p0 + ray * t;
            godot::Vector2 road_t_hit_raw; godot::Vector3 spatial_t_hit;
            convert_point_to_road(track, use_idx, hit_point, road_t_hit_raw, spatial_t_hit);
            godot::Vector2 road_t_hit = road_t_hit_raw;
            road_t_hit.x = fminf(fmaxf(road_t_hit.x, -1.0f), 1.0f);
            godot::Transform3D surf_hit;
            if (oriented){
                segment.road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
            }else{
                segment.road_shape->get_transform_at_time(surf_hit, road_t_hit);
            }
            godot::Vector3 normal = surf_hit.basis[1];
            if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(normal) <= 0.0f) {
                float dist = t * ray.length();
                if (dist < best_t) {
                    best_t = dist;
                    out_collision.collided = true;
                    out_collision.collision_point = hit_point;
                    out_collision.collision_normal = normal;
                    out_collision.road_data.cp_idx = use_idx;
                    out_collision.road_data.spatial_t = spatial_t_hit;
                    out_collision.road_data.road_t = road_t_hit_raw;
                    if (oriented){
                        segment.road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
                    }else{
                        segment.road_shape->get_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
                    }
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

    if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
        if (out_collision.collided){
            dd3d->call("draw_arrow", out_collision.collision_point, out_collision.collision_point + out_collision.collision_normal * 2, godot::Color(0.0f, 0.0f, 1.0f), 0.25, true, _TICK_DELTA);
        }
    }

    if (!(params.mask & CAST_FLAGS::WANTS_RAIL))
        return;

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
        convert_point_to_road(track, use_idx, hit, road_t_hit_raw, spatial_t_hit);
        godot::Vector2 road_t_hit = road_t_hit_raw;
        road_t_hit.x = fminf(fmaxf(road_t_hit.x, -1.0f), 1.0f);
        godot::Transform3D surf_hit;
        if (oriented){
            segment.road_shape->get_oriented_transform_at_time(surf_hit, road_t_hit);
        }else{
            segment.road_shape->get_transform_at_time(surf_hit, road_t_hit);
        }
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
            out_collision.road_data.cp_idx = use_idx;
            out_collision.road_data.spatial_t = spatial_t_hit;
            out_collision.road_data.road_t = road_t_hit_raw;
            if (oriented){
                segment.road_shape->get_oriented_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
            }else{
                segment.road_shape->get_transform_at_time(out_collision.road_data.closest_surface, road_t_hit);
            }
            segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit.y);
            out_collision.road_data.terrain = 0x100;
        }
    }
    if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
        if (out_collision.collided){
            dd3d->call("draw_arrow", out_collision.collision_point, out_collision.collision_point + out_collision.collision_normal * 2, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Same helpers / includes / forward-decls as before …
// ──────────────────────────────────────────────────────────────────────────────

static void cast_segment_fast(const CastParams  &params,
    CollisionData                               &out_collision,
    godot::Vector3 const                        &p0,
    godot::Vector3 const                        &p1,
    int                                         use_idx,
    godot::Vector3 const                        &sample_pt, // NEW
    bool                                        oriented    = true)
{
    out_collision.collided          = false;
    out_collision.road_data.cp_idx  = -1;
    if ((params.mask & CAST_FLAGS::WANTS_TRACK) == 0)
        return;

    RaceTrack *track                = params.track;
    const TrackSegment &segment     = track->segments[track->checkpoints[use_idx].road_segment];

    godot::Object* dd3d;
    if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
        dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
        dd3d->call("draw_arrow", p0, p1, godot::Color(1.0f, 1.0f, 1.0f), 0.25, true, _TICK_DELTA);
    }

    godot::Vector2  road_t_sample_raw;  godot::Vector3 spatial_t_sample;
    convert_point_to_road(track, use_idx, sample_pt, road_t_sample_raw, spatial_t_sample);

    godot::Transform3D surf;        // THE ONLY SURFACE FETCH
    if (oriented)
        segment.road_shape->get_oriented_transform_at_time(surf, road_t_sample_raw);
    else
        segment.road_shape->get_transform_at_time(surf, road_t_sample_raw);

    const godot::Vector3 surf_n     = surf.basis[1];                    // Up/normal
    const godot::Vector3 surf_fwd   = surf.basis[2];                    // Forward (needed for rails)

    // ── 2) Basic plane hit against the single surface ────────────────────────
    const godot::Vector3 ray        = p1 - p0;
    float best_t                    = FLT_MAX;

    const float d0                  = (p0 - surf.origin).dot(surf_n);
    const float d1                  = (p1 - surf.origin).dot(surf_n);

    if (!((d0 <= 0.0f && d1 <= 0.0f) || (d0 >= 0.0f && d1 >= 0.0f))) {
        const float t = d0 / (d0 - d1);                                     // p0->p1 crossing %
        if (t >= 0.0f && t <= 1.0f) {
            const godot::Vector3 hit_point  = p0 + ray * t;

            godot::Vector2 road_t_hit_raw;  godot::Vector3 spatial_t_hit;
            convert_point_to_road(track, use_idx, hit_point, road_t_hit_raw, spatial_t_hit);

            if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) != 0 || ray.dot(surf_n) <= 0.0f) {
                const float dist = t * ray.length();
                best_t                      = dist;
                out_collision.collided      = true;
                out_collision.collision_point   = hit_point;
                out_collision.collision_normal  = surf_n;

                out_collision.road_data.cp_idx          = use_idx;
                out_collision.road_data.spatial_t       = spatial_t_hit;
                out_collision.road_data.road_t          = road_t_hit_raw;
                out_collision.road_data.closest_surface = surf;     // reuse single transform
                segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit_raw.y);

                out_collision.road_data.terrain = 0;
                if (params.mask & CAST_FLAGS::WANTS_TERRAIN) {
                    for (int i = 0; i < segment.road_shape->num_embeds; ++i) {
                        RoadEmbed *embed = &segment.road_shape->road_embeds[i];
                        if (road_t_hit_raw.y > embed->start_offset && road_t_hit_raw.y < embed->end_offset) {
                            const float et = (road_t_hit_raw.y - embed->start_offset) /
                                             (embed->end_offset - embed->start_offset);
                            const float l   = embed->left_border->sample(et);
                            const float r   = embed->right_border->sample(et);
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

    if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
        if (out_collision.collided){
            dd3d->call("draw_arrow", out_collision.collision_point, out_collision.collision_point + out_collision.collision_normal * 2, godot::Color(0.0f, 0.0f, 1.0f), 0.25, true, _TICK_DELTA);
        }
    }

    // ── 3) Optional rail cast (uses the SAME surface data) ───────────────────
    if ((params.mask & CAST_FLAGS::WANTS_RAIL) == 0)
        return;

    if (dynamic_cast<RoadShapeCylinder*>(segment.road_shape) ||
        dynamic_cast<RoadShapePipe*>(segment.road_shape) ||
        dynamic_cast<RoadShapeCylinderOpen*>(segment.road_shape) ||
        dynamic_cast<RoadShapePipeOpen*>(segment.road_shape))
        return;

    godot::Transform3D root_t;
    segment.curve_matrix->sample(root_t, road_t_sample_raw.y);
    const godot::Basis rbasis       = root_t.basis.transposed();
    const godot::Vector3 left_pos   = root_t.origin + rbasis[0];
    const godot::Vector3 right_pos  = root_t.origin - rbasis[0];
    const godot::Vector3 left_plane_n   = -rbasis[0].normalized();
    const godot::Vector3 right_plane_n  =  rbasis[0].normalized();

    struct RailSide { godot::Vector3 pos, plane_n, rail_n; float height; };
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

        const godot::Vector3 hit = p0 + ray * t;

        godot::Vector2 road_t_hit_raw;  godot::Vector3 spatial_t_hit;
        convert_point_to_road(track, use_idx, hit, road_t_hit_raw, spatial_t_hit);

        const float vdist = (hit - surf.origin).dot(surf_n);        // height above track
        if (vdist < 0.0f || vdist > side.height)
            continue;
        if ((params.mask & CAST_FLAGS::WANTS_BACKFACE) == 0 && ray.dot(side.rail_n) > 0.0f)
            continue;

        const float dist = t * ray.length();
        if (dist < best_t) {
            best_t                          = dist;
            out_collision.collided          = true;
            out_collision.collision_point   = hit;
            out_collision.collision_normal  = side.rail_n;

            out_collision.road_data.cp_idx          = use_idx;
            out_collision.road_data.spatial_t       = spatial_t_hit;
            out_collision.road_data.road_t          = road_t_hit_raw;
            out_collision.road_data.closest_surface = surf; // reuse same transform
            segment.curve_matrix->sample(out_collision.road_data.closest_root, road_t_hit_raw.y);
            out_collision.road_data.terrain         = 0x100;
        }
    }

    if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAYCASTS)){
        if (out_collision.collided && out_collision.road_data.terrain == 0x100){
            dd3d->call("draw_arrow", out_collision.collision_point, out_collision.collision_point + out_collision.collision_normal * 2, godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
        }
    }
}


void RaceTrack::cast_vs_track(CollisionData &out_collision, const godot::Vector3 &p0, const godot::Vector3 &p1, uint8_t mask, int cp_idx, bool oriented)
{
    out_collision.collided = false;
    out_collision.road_data.cp_idx = -1;

    if ((mask & CAST_FLAGS::WANTS_TRACK) == 0)
    {
        return;
    }

    if (cp_idx == -1)
    {
        return;
    }

    const float step_len = 2.0f;
    const float epsilon = 0.05f;

    godot::Vector3 ray = p1 - p0;
    float total_len = ray.length();
    if (total_len == 0.0f)
        return;
    godot::Vector3 dir = ray / total_len;

    CastParams params{ this, mask };

    godot::Vector3 cur = p0;
    float travelled = 0.0f;

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

void RaceTrack::cast_vs_track_fast(CollisionData &out_collision,
    godot::Vector3 const &p0,
    godot::Vector3 const &p1,
    uint8_t mask,
    int start_idx, bool oriented)
{
    out_collision.collided = false;
    out_collision.road_data.cp_idx = -1;

    if ((mask & CAST_FLAGS::WANTS_TRACK) == 0)
        return;

    // choose sample point
    godot::Vector3 sample_point;
    if (mask & CAST_FLAGS::SAMPLE_FROM_P0)
        sample_point = p0;
    else if (mask & CAST_FLAGS::SAMPLE_FROM_MID)
        sample_point = (p0 + p1) * 0.5f;
    else
        sample_point = p1;

    // pick a single checkpoint
    int cp_idx = -1;
    godot::Vector3 other_point = (sample_point == p1) ? p0 : p1;
    cp_idx = get_best_checkpoint(sample_point, other_point);
    //if (start_idx == -1) {
    //    auto cps = get_viable_checkpoints(sample_point);
    //    if (cps.empty()){
    //        return;
    //    }
    //    cp_idx = cps[0];
    //} else {
    //    cp_idx = find_checkpoint_bfs(sample_point, start_idx);
    //}
    if (cp_idx == -1)
    {
        return;
    }

    // do one raycast against that checkpoint
    CastParams params{ this, mask };
    cast_segment_fast(params, out_collision, p0, p1, cp_idx, sample_point, true);
}

void RaceTrack::build_checkpoint_bvh()
{
    checkpoint_aabbs.resize(num_checkpoints);
    for (int i = 0; i < num_checkpoints; ++i) {
        const CollisionCheckpoint &cp = checkpoints[i];
        TrackSegment &seg = segments[cp.road_segment];
        godot::AABB box;
        bool first = true;
        for (int ty = 0; ty < 8; ++ty) {
            float ft = cp.t_start + (cp.t_end - cp.t_start) * ((float)ty / 7.f);
            for (int tx = 0; tx < 8; ++tx) {
                float fx = -1.0f + 2.0f * ((float)tx / 7.f);
                godot::Vector3 sample;
                seg.road_shape->get_position_at_time(sample, godot::Vector2(fx, ft));
                godot::AABB pt(sample, godot::Vector3(0,0,0));
                if (first) {
                    box = pt;
                    first = false;
                } else {
                    box = box.merge(pt);
                }
            }
        }
        checkpoint_aabbs[i] = box;
    }
    checkpoint_bvh.build(checkpoint_aabbs);
}

void RaceTrack::debug_draw_checkpoint_bvh() const
{
    if (checkpoint_bvh.nodes.empty())
        return;
    godot::Object *dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
    std::function<void(int)> draw_node = [&](int idx){
        if (idx < 0) return;
        const CheckpointBVHNode &node = checkpoint_bvh.nodes[idx];
        dd3d->call("draw_aabb", node.bounds, godot::Color(1.0f, 0.0f, 1.0f, 0.1f), _TICK_DELTA);
        if (node.left != -1) draw_node(node.left);
        if (node.right != -1) draw_node(node.right);
    };
    draw_node(0);
}
