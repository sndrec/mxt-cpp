#pragma once
#include "car/physics_car.h"
#include "track/racetrack.h"
#include <vector>

struct AgentObservation {
    // Layout (22 floats):
    // 0-1: road_t.x, road_t.y
    // 2: checkpoint_fraction
    // 3: yaw angular velocity (scaled)
    // 4-6: velocity in track frame (fwd/right/up), scaled
    // 7-10: lookahead forward alignment (norm vel vs target dirs)
    // 11-14: lookahead sideways alignment (car right vs target dirs)
    // 15: airborne flag
    // 16-18: energy_frac, boost_norm, base_speed
    // 19-20: reserved (was spatial_t.x,y)
    // 21: damage_norm
    static constexpr int kSize = 22;
    float data[kSize];
};

// Builds a compact observation vector for a given car.
// All values are finite; many are in [-1,1] for learning convenience.
inline void build_observation(const PhysicsCar &car, const RaceTrack &track, AgentObservation &obs) {
    //godot::Object *dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
    for (int i = 0; i < AgentObservation::kSize; ++i) obs.data[i] = 0.0f;
    int w = 0;

    const int cp_idx = car.current_checkpoint;
    godot::Vector2 road_t; godot::Vector3 spatial_t; godot::Transform3D surf;
    const bool have_track = (cp_idx >= 0 && cp_idx < track.num_checkpoints);
    if (have_track) {
        const_cast<RaceTrack&>(track).get_road_surface(cp_idx, car.position_current, road_t, spatial_t, surf, true);
    } else {
        road_t = godot::Vector2(0.0f, 0.0f);
        spatial_t = godot::Vector3();
        surf = godot::Transform3D();
    }

    // 0-1: road_t position (x in [-1,1], y in [0,1])
    obs.data[w++] = std::max(-2.0f, std::min(2.0f, road_t.x));
    obs.data[w++] = std::max(0.0f, std::min(1.0f, road_t.y));

    // 2: checkpoint fraction
    obs.data[w++] = std::max(0.0f, std::min(1.0f, car.checkpoint_fraction));

    // 3: yaw angular velocity (around Y), clamped and scaled to ~[-1,1]
    float yaw_rate = car.velocity_angular.y;
    if (!(yaw_rate == yaw_rate)) yaw_rate = 0.0f;
    obs.data[w++] = std::max(-1.0f, std::min(1.0f, yaw_rate * 0.0001f));

    // 4-6: velocity in local track frame (fwd/right/up), scaled
    godot::Basis tb = surf.basis;
    godot::Vector3 v = car.velocity;
    const float vel_scale = 1.0f / 100.0f; // ~100 m/s scale
    obs.data[w++] = v.dot(tb[2]) * vel_scale; // forward
    obs.data[w++] = v.dot(tb[0]) * vel_scale; // right
    obs.data[w++] = v.dot(tb[1]) * vel_scale; // up

    // 7-14: lookahead alignments using normalized velocity and car right vector
    godot::Basis cb = car.basis_physical.basis;
    godot::Vector3 c_right = cb.get_column(0);
    godot::Vector3 c_fwd = cb.get_column(2);

    // Estimate current cumulative distance along the track
    float cp_prev_dist = 0.0f;
    if (car.current_checkpoint > 0 && car.current_checkpoint < track.num_checkpoints)
        cp_prev_dist = track.checkpoints[car.current_checkpoint - 1].distance;
    float cp_dist = 0.0f;
    if (car.current_checkpoint >= 0 && car.current_checkpoint < track.num_checkpoints)
        cp_dist = track.checkpoints[car.current_checkpoint].distance;
    float cur_total_dist = cp_prev_dist + (cp_dist - cp_prev_dist) * std::max(0.0f, std::min(1.0f, car.checkpoint_fraction));

    const float lookahead_step = 150.0f; // meters between lookahead points
    const float center_lerp = 0.2f;      // bias tx towards center
    float t_x_target = road_t.x;

    // Precompute normalized velocity; fallback to car forward if near zero
    godot::Vector3 vel = car.velocity;
    float vlen = vel.length();
    godot::Vector3 vel_n = (vlen > 1e-4f) ? (vel / vlen) : -cb.get_column(2);

    float fwd_aligns[4] = {0,0,0,0};
    float side_aligns[4] = {0,0,0,0};
    // Track length from last checkpoint's cumulative distance
    float track_length = (track.num_checkpoints > 0) ? track.checkpoints[track.num_checkpoints - 1].distance : 0.0f;
    for (int k = 1; k <= 4; ++k) {
        t_x_target = std::max(-1.0f, std::min(1.0f, t_x_target * (1.0f - center_lerp)));
        float target_dist = cur_total_dist + lookahead_step * float(k);
        int it = car.current_checkpoint;
        if (it < 0 || it >= track.num_checkpoints) break;
        float prev_d = cp_prev_dist;
        float this_d = cp_dist;
        float offset = 0.0f;
        int last_it = it;
        int guard = 0;
        while (this_d + offset < target_dist && guard < track.num_checkpoints) {
            // Choose neighbor: lowest index among neighbors with index > it; fallback to overall lowest.
            int best = -1;
            int lowest = -1;
            int ncount = track.checkpoints[it].num_neighboring_checkpoints;
            for (int ni = 0; ni < ncount; ++ni) {
                int nb = track.checkpoints[it].neighboring_checkpoints[ni];
                if (lowest == -1 || nb < lowest) lowest = nb;
                if (nb > it) {
                    if (best == -1 || nb < best) best = nb;
                }
            }
            if (best == -1) best = lowest;
            if (best == -1 || best == it) break;
            it = best;
            // Detect wrap-around: indices decreased; add track length offset
            if (track_length > 0.0f && it <= last_it) {
                offset += track_length;
            }
            last_it = it;
            prev_d = ((it > 0) ? track.checkpoints[it - 1].distance : 0.0f);
            this_d = track.checkpoints[it].distance;
            ++guard;
        }
        float alpha = 0.0f;
        if (this_d > prev_d) {
            alpha = (target_dist - (prev_d + offset)) / (this_d - prev_d);
            if (alpha < 0.0f) alpha = 0.0f; else if (alpha > 1.0f) alpha = 1.0f;
        }
        int seg_idx = track.checkpoints[it].road_segment;
        godot::Vector3 tgt;
        float t_start = track.checkpoints[it].t_start;
        float t_end = track.checkpoints[it].t_end;
        float t_y = t_start + (t_end - t_start) * alpha;
        track.segments[seg_idx].road_shape->get_position_at_time(tgt, godot::Vector2(t_x_target, t_y));
        godot::Vector3 dir = tgt - car.position_current;
        float dlen = dir.length();
        if (dlen > 1e-6f) {
            dir /= dlen;
            fwd_aligns[k-1] = (vel_n - c_fwd).normalized().dot(dir);
            side_aligns[k-1] = c_right.dot(dir);
        }
        //godot::Color arrow_color = godot::Color(1.0f, 0.1f, 0.1f).lerp(godot::Color(0.1f, 1.0f, 0.1f), fwd_aligns[k-1] * 0.5f + 0.5f);
        //dd3d->call("draw_arrow", tgt, tgt + car.track_surface_normal, arrow_color, 0.25f, true, 0.0166666f);
        //dd3d->call("draw_arrow", car.position_current, car.position_current + dir * 4.0f, arrow_color, 0.25f, true, 0.0166666f);
        //dd3d->call("draw_arrow", car.position_current, car.position_current + (vel_n - c_fwd) * 4.0f, godot::Color(1.0f, 1.0f, 1.0f), 0.25f, true, 0.0166666f);
    }
    for (int i = 0; i < 4; ++i) obs.data[w++] = fwd_aligns[i];
    for (int i = 0; i < 4; ++i) obs.data[w++] = side_aligns[i];

    // 15: is_airborne flag
    obs.data[w++] = (car.machine_state & MACHINESTATE::AIRBORNE) ? 1.0f : 0.0f;

    // 16-18: energy fraction, boost normalized, base_speed
    float energy_frac = car.calced_max_energy > 0.0f ? (car.energy / car.calced_max_energy) : 0.0f;
    energy_frac = std::max(0.0f, std::min(1.0f, energy_frac));
    obs.data[w++] = energy_frac;
    obs.data[w++] = std::max(0.0f, std::min(1.0f, car.boost_turbo * 0.01f));
    obs.data[w++] = car.base_speed; // already small

    // 19-20: reserved (previously spatial_t.x,y); set to zero
    obs.data[w++] = car.air_tilt;
    obs.data[w++] = 0.0f;

    // 21: damage taken this step, normalized by max energy and clamped to [0,1]
    float dmg = car.damage_from_last_hit;
    float maxe = car.calced_max_energy > 0.0f ? car.calced_max_energy : 1.0f;
    float dmg_norm = std::max(0.0f, std::min(1.0f, dmg / maxe));
    obs.data[w++] = dmg_norm;

    (void)w;
}
