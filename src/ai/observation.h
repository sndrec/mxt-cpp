#pragma once
#include "car/physics_car.h"
#include "track/racetrack.h"
#include <vector>

struct AgentObservation {
    static constexpr int kSize = 25;
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

    // 7-9: orientation alignment: forward dot/side dot/up dot
    godot::Basis cb = car.basis_physical.basis;
    godot::Vector3 cf = cb.get_column(2);
    godot::Vector3 cu = cb.get_column(1);
    godot::Vector3 tf = tb[2];
    godot::Vector3 tu = tb[1];
    godot::Vector3 tx = tb[0];
    obs.data[w++] = cf.dot(tf); // forward alignment
    obs.data[w++] = cf.dot(tx); // how much pointing to the right
    obs.data[w++] = cu.dot(tu); // up alignment

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

    // 16-18: upcoming curvature estimate: compare forward at small delta along track
    float dy = 0.02f;
    godot::Transform3D surf_next;
    if (have_track) {
        godot::Vector2 t2(road_t.x, std::min(1.0f, road_t.y + dy));
        track.segments[ track.checkpoints[cp_idx].road_segment ].road_shape->get_oriented_transform_at_time(surf_next, t2);
    } else {
        surf_next = surf;
    }
    godot::Vector3 tf2 = surf_next.basis[2];
    float yaw_curv = tf.cross(tf2).dot(tu); // signed yaw change
    float pitch_curv = tf.cross(tf2).dot(tx);
    float roll_curv = tu.cross(surf_next.basis[1]).dot(tf);
    obs.data[w++] = yaw_curv;
    obs.data[w++] = pitch_curv;
    obs.data[w++] = roll_curv;

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

    // Ensure w equals kSize in dev; in release this will be optimized away.
    (void)w;
}
