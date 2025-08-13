#pragma once
#include "car/physics_car.h"
#include "track/racetrack.h"
#include <vector>

struct AgentObservation {
    static constexpr int kSize = 27;
    float data[kSize];
};

// Builds a compact observation vector for a given car.
// All values are finite; many are in [-1,1] for learning convenience.
inline void build_observation(const PhysicsCar &car, const RaceTrack &track, AgentObservation &obs) {
    for (int i = 0; i < AgentObservation::kSize; ++i) obs.data[i] = 0.0f;
    int w = 0;

    const int cp_idx = car.current_checkpoint;
    godot::Vector2 road_t; godot::Vector3 spatial_t; godot::Transform3D surf;
    const bool have_track = (cp_idx >= 0 && cp_idx < track.num_checkpoints);
    if (have_track) {
        // Oriented surface at the car's projected road position
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

    // 3: height above track (clamped and scaled)
    float h = car.height_above_track;
    if (!(h == h)) h = 0.0f;
    obs.data[w++] = std::max(-5.0f, std::min(5.0f, h)) * 0.2f;

    // 4-6: velocity in local track frame (fwd/right/up), scaled
    godot::Basis tb = surf.basis;
    godot::Vector3 v = car.velocity;
    float v_fwd = v.dot(tb[2]);
    float v_right = v.dot(tb[0]);
    float v_up = v.dot(tb[1]);
    const float vel_scale = 1.0f / 100.0f; // ~100 m/s scale
    obs.data[w++] = v_fwd * vel_scale;
    obs.data[w++] = v_right * vel_scale;
    obs.data[w++] = v_up * vel_scale;

    // 7-10: lookahead alignment towards future points along the track
    godot::Basis cb = car.basis_physical.basis;
    godot::Vector3 cf = cb.get_column(2);
    godot::Vector3 tf = tb[2];
    godot::Vector3 tu = tb[1];
    godot::Vector3 tx = tb[0];

    // Compute four approximately equidistant future targets using checkpoint distances.
    // Use current checkpoint cumulative distance plus fraction to estimate current track distance.
    float cur_cp_prev_dist = 0.0f;
    if (car.current_checkpoint > 0 && car.current_checkpoint < track.num_checkpoints) {
        cur_cp_prev_dist = track.checkpoints[car.current_checkpoint - 1].distance;
    }
    float cur_cp_dist = 0.0f;
    if (car.current_checkpoint >= 0 && car.current_checkpoint < track.num_checkpoints) {
        cur_cp_dist = track.checkpoints[car.current_checkpoint].distance;
    }
    float cur_total_dist = cur_cp_prev_dist + (cur_cp_dist - cur_cp_prev_dist) * std::max(0.0f, std::min(1.0f, car.checkpoint_fraction));

    const float lookahead_step = 150.0f; // meters between lookahead points
    const float center_lerp = 0.5f;      // how much to bias tx towards center (0..1)
    float t_x_target = std::max(-1.0f, std::min(1.0f, road_t.x * (1.0f - center_lerp)));

    for (int k = 1; k <= 4; ++k) {
        float target_dist = cur_total_dist + lookahead_step * float(k);
        int it = car.current_checkpoint;
        int guard = 0;
        float prev_d = cur_cp_prev_dist;
        float this_d = cur_cp_dist;
        if (it < 0 || it >= track.num_checkpoints) {
            obs.data[w++] = 0.0f;
            continue;
        }
        while (this_d < target_dist && guard < track.num_checkpoints) {
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
            prev_d = (it > 0) ? track.checkpoints[it - 1].distance : 0.0f;
            this_d = track.checkpoints[it].distance;
            ++guard;
        }

        float alpha = 0.0f;
        if (this_d > prev_d) {
            alpha = (target_dist - prev_d) / (this_d - prev_d);
            if (alpha < 0.0f) alpha = 0.0f; else if (alpha > 1.0f) alpha = 1.0f;
        }

        int seg_idx = track.checkpoints[it].road_segment;
        godot::Transform3D tgt;
        float t_start = track.checkpoints[it].t_start;
        float t_end = track.checkpoints[it].t_end;
        float t_y = t_start + (t_end - t_start) * alpha;
        track.segments[seg_idx].road_shape->get_oriented_transform_at_time(tgt, godot::Vector2(t_x_target, t_y));
        godot::Vector3 to_tgt = tgt.origin - car.position_current;
        float len = to_tgt.length();
        float align = 0.0f;
        if (len > 1e-6f) {
            align = cf.dot(to_tgt / len);
        }
        obs.data[w++] = align; // [-1,1], higher is better
    }


    // 10-11: input history (smoothed) that car stores
    obs.data[w++] = std::max(-1.0f, std::min(1.0f, car.input_steer_yaw));
    obs.data[w++] = std::max(-1.0f, std::min(1.0f, car.input_steer_pitch));

    // 12-14: energy fraction, boost normalized, base_speed
    float energy_frac = car.calced_max_energy > 0.0f ? (car.energy / car.calced_max_energy) : 0.0f;
    energy_frac = std::max(0.0f, std::min(1.0f, energy_frac));
    obs.data[w++] = energy_frac;
    obs.data[w++] = std::max(0.0f, std::min(1.0f, car.boost_turbo * 0.01f));
    obs.data[w++] = car.base_speed; // already small

    // 15: is_airborne flag
    obs.data[w++] = (car.machine_state & MACHINESTATE::AIRBORNE) ? 1.0f : 0.0f;

    // 16-18 (previously curvature): zeros to keep size stable
    obs.data[w++] = 0.0f;
    obs.data[w++] = 0.0f;
    obs.data[w++] = 0.0f;


    // 19-21: local velocities used in car
    obs.data[w++] = car.velocity_local.x * vel_scale; // right
    obs.data[w++] = car.velocity_local.y * vel_scale; // up
    obs.data[w++] = -car.velocity_local.z * vel_scale; // forward positive

    // 22-23: normalized lateral position from spatial_t.x,y (already normalized)
    obs.data[w++] = std::max(-1.0f, std::min(1.0f, spatial_t.x));
    obs.data[w++] = std::max(-1.0f, std::min(1.0f, spatial_t.y));

    // 24: damage taken this step, normalized by max energy and clamped to [0,1]
    float dmg = car.damage_from_last_hit;
    float maxe = car.calced_max_energy > 0.0f ? car.calced_max_energy : 1.0f;
    float dmg_norm = std::max(0.0f, std::min(1.0f, dmg / maxe));
    obs.data[w++] = dmg_norm;

        // 25-26 (previously direction to next segment): not used; keep zeros
    obs.data[w++] = 0.0f;
    obs.data[w++] = 0.0f;


    // Ensure w equals kSize in dev; in release this will be optimized away.
    (void)w;
}
