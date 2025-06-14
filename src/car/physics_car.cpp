
#include "physics_car.h"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/math.hpp"
#include <cmath>
#include <algorithm>

static inline godot::Vector3 normalized_safe(const godot::Vector3 &v,
                                             const godot::Vector3 &def = godot::Vector3()) {
    return v.length_squared() > 0.000001f ? v.normalized() : def;
}

godot::Vector3 PhysicsCar::prepare_machine_frame()
{
        // Reset input if we're in the starting countdown
        if (machine_state & MACHINESTATE::STARTINGCOUNTDOWN) {
                input_steer_yaw = 0.0f;
                input_steer_pitch = 0.0f;
                input_brake = 0.0f;
                input_strafe = 0.0f;
                machine_state &= ~(MACHINESTATE::SIDEATTACKING |
                                   MACHINESTATE::JUST_PRESSED_BOOST |
                                   MACHINESTATE::SPINATTACKING);
        }

        uint32_t old_terrain_state = terrain_state;

        machine_state &= ~(MACHINESTATE::DIEDTHISFRAMEOOB_Q |
                            MACHINESTATE::JUST_HIT_DASHPLATE |
                            MACHINESTATE::RACEJUSTBEGAN_Q |
                            MACHINESTATE::JUSTTAPPEDACCEL |
                            MACHINESTATE::CROSSEDLAPLINE_Q |
                            MACHINESTATE::JUSTLANDED |
                            MACHINESTATE::AIRBORNEMORE0_2S_Q |
                            MACHINESTATE::AIRBORNE);

        state_2 &= 0xfffffcff;
        terrain_state = 0;

        if ((machine_state & MACHINESTATE::B29) == 0) {
                set_terrain_state_from_track();
        }

        if (old_terrain_state & TERRAIN::DASH) {
                machine_state &= ~MACHINESTATE::JUST_HIT_DASHPLATE;
        }

        mtxa->assign(basis_physical_other);
        mtxa->cur->origin = position_old;

        basis_physical_other = basis_physical;
        position_old_dupe = position_current;
        position_old = position_old_dupe;

        PhysicsCarSuspensionPoint* tilt_corners[4] = { &tilt_fl, &tilt_fr, &tilt_bl, &tilt_br };
        PhysicsCarCollisionPoint* wall_corners[4] = { &wall_fl, &wall_fr, &wall_bl, &wall_br };

        for (int i = 0; i < 4; ++i) {
                auto* tc = tilt_corners[i];
                godot::Vector3 new_pos = tc->offset;
                new_pos.y += tc->force;
                new_pos.y -= tc->rest_length;
                tc->pos_old = mtxa->transform_point(new_pos);
                if (tc->state & TILTSTATE::DRIFT) {
                        for (auto* c : tilt_corners) {
                                c->state |= TILTSTATE::DRIFT;
                        }
                }
        }

        mtxa->assign(basis_physical);
        mtxa->cur->origin = position_current;

        godot::Vector3 ground_normal = godot::Vector3(0, 1, 0);
        if (machine_state & MACHINESTATE::ACTIVE) {
                ground_normal = get_avg_track_normal_from_tilt_corners();
        }

        bool all_airborne = true;
        for (int i = 0; i < 4; ++i) {
                if ((tilt_corners[i]->state & TILTSTATE::AIRBORNE) == 0) {
                        all_airborne = false;
                }
                wall_corners[i]->pos_a = position_current;
        }

        if (all_airborne) {
                machine_state |= MACHINESTATE::AIRBORNE;
                air_time += 1;
                if (air_time > 10)
                        machine_state |= MACHINESTATE::AIRBORNEMORE0_2S_Q;
        } else {
                if (air_time != 0)
                        machine_state |= MACHINESTATE::JUSTLANDED;
                air_time = 0;
                machine_state &= ~MACHINESTATE::AIRBORNEMORE0_2S_Q;
                state_2 &= ~2u;
        }

        turning_related = 0.0f;
        visual_rotation.z *= 0.8f;
        visual_rotation.x *= 0.9f;

        if (machine_state & MACHINESTATE::ACTIVE) {
                if (frames_since_start_2 != 0)
                        frames_since_start_2 = std::min(255u, frames_since_start_2 + 1);
        }

        if ((machine_state & MACHINESTATE::COMPLETEDRACE_1_Q) != 0 ||
            (terrain_state & TERRAIN::RECHARGE) != 0) {
                energy += 1.111111f;
                if (energy > calced_max_energy)
                        energy = calced_max_energy;
        }

        float vel_mag = velocity.length();
        speed_kmh = 216.0f * (vel_mag / std::max(stat_weight, 0.001f));

        if ((machine_state & MACHINESTATE::RETIRED) != 0 &&
            (machine_state & MACHINESTATE::AIRBORNE) == 0) {
                if (speed_kmh >= 10.0f)
                        velocity *= 0.9f;
                else
                        velocity = godot::Vector3();
        }

        handle_attack_states();

        if (car_hit_invincibility == 0) {
                if (machine_state & MACHINESTATE::JUSTHITVEHICLE_Q)
                        car_hit_invincibility = 6;
        } else {
                car_hit_invincibility -= 1;
        }

        velocity_local = mtxa->inverse_rotate_point(velocity);
        mtxa->push();
        float steer = -(input_steer_yaw * stat_turn_reaction + input_strafe * stat_strafe);
        steer = std::clamp(steer, -45.0f, 45.0f);
        mtxa->rotate_y(DEG_TO_RAD * steer);
        velocity_local_flattened_and_rotated = mtxa->inverse_rotate_point(velocity);
        velocity_local_flattened_and_rotated.y = 0.0f;
        mtxa->pop();

        position_old_2 = position_current;

        frames_since_start += 1;

        return ground_normal;
};

float PhysicsCar::get_current_stage_min_y() const
{
	return 0.0f;
};

void PhysicsCar::handle_machine_damage_and_visuals()
{
        if ((state_2 & 0x8u) == 0)
                return;

        mtxa->assign(basis_physical);
        mtxa->cur->origin = position_current;

        if (terrain_state & TERRAIN::LAVA) {
                // Lava damage handling is not yet implemented
                if ((state_2 & 0x200u) && (machine_state & MACHINESTATE::ZEROHP)) {
                        return;
                }
        }

        mtxa->assign(basis_physical);
        mtxa->cur->origin = position_current;

        PhysicsCarSuspensionPoint* tilt_corners[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
        PhysicsCarCollisionPoint* wall_corners[4] = {&wall_fl, &wall_fr, &wall_bl, &wall_br};

        for (int i = 0; i < 4; ++i) {
                auto* tc = tilt_corners[i];
                auto* wc = wall_corners[i];

                tc->pos_old = tc->pos;

                float local_y_offset_suspended = tc->offset.y + tc->force - tc->rest_length;
                godot::Vector3 local_pos(tc->offset.x, local_y_offset_suspended, tc->offset.z);
                tc->pos = mtxa->transform_point(local_pos);

                wc->pos_a = wc->pos_b;
                wc->pos_b = mtxa->transform_point(wc->offset);
        }

        if ((state_2 & 0x10u) == 0) {
                float y_pos = position_current.y;
                float track_min_y = -1000000.0f; // Placeholder until track data is available
                if (y_pos < -5000.0f || y_pos < (track_min_y - 900.0f)) {
                        return;
                }
        }

        if (position_current.y < -10000.0f) {
                position_current.y = -10000.0f;
                velocity = godot::Vector3();
        }

        create_machine_visual_transform();

        if ((machine_state & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
                float world_speed = velocity.length();
                if (std::abs(stat_weight) > 0.0001f)
                        speed_kmh = 216.0f * (world_speed / stat_weight);
                else
                        speed_kmh = 0.0f;

                float current_speed_for_max_check = speed_kmh;
                bool no_bad_state_flags =
                        (machine_state & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
                                          MACHINESTATE::TOOKDAMAGE)) == 0;

                (void)current_speed_for_max_check;
                (void)no_bad_state_flags;
        }
};

bool PhysicsCar::find_floor_beneath_machine()
{
	return false;
        godot::Vector3 p0_sweep_start_ws =
                mtxa->transform_point(godot::Vector3(0.0f, 1.0f, 0.0f));
        godot::Vector3 p1_sweep_end_ws =
                mtxa->transform_point(godot::Vector3(0.0f, -200.0f, 0.0f));

        position_bottom = p1_sweep_end_ws;

        bool sweep_hit_occurred = false;
        CollisionData hit;
        if (current_track != nullptr) {
                current_track->cast_vs_track(hit, p0_sweep_start_ws,
                                             position_bottom,
                                             CAST_FLAGS::WANTS_TRACK,
                                             current_checkpoint);
                sweep_hit_occurred = hit.collided;
        }

        float contact_dist_metric = 0.0f;
        if (sweep_hit_occurred) {
                track_surface_pos = hit.collision_point;
                float dist_p0_to_surface =
                        mtxa->cur->origin.distance_to(hit.collision_point);
                contact_dist_metric = 20.0f - dist_p0_to_surface;
        }

        if (sweep_hit_occurred && contact_dist_metric > 0.0f) {
                track_surface_normal = hit.collision_normal;
                height_above_track = contact_dist_metric;
                return true;
        } else {
                track_surface_normal = godot::Vector3(0, 1, 0);
                position_bottom = p1_sweep_end_ws;
                height_above_track = 0.0f;
                return false;
        }
};

void PhysicsCar::handle_steering()
{
        if ((machine_state & MACHINESTATE::ACTIVE) == 0) {
                return;
        }

        float strafe_turn_mod = 1.0f;
        PhysicsCarSuspensionPoint* corners[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
        for (auto* corner : corners) {
                if (corner->state & TILTSTATE::DRIFT) {
                        strafe_turn_mod -= 0.25f;
                }
        }

        float steer_strength =
                (stat_turn_movement + strafe_turn_mod * stat_strafe_turn * input_strafe *
                                             input_steer_yaw) *
                -input_steer_yaw;
        if (machine_state & MACHINESTATE::SIDEATTACKING) {
                steer_strength *= 0.3f;
        }

        velocity_angular.y += 1.5f * steer_strength;

        if (std::abs(velocity_angular.y) < 1.0f) {
                velocity_angular.y = 0.0f;
        }

        input_yaw_dupe = input_steer_yaw;
};

void PhysicsCar::set_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag)
{

        PhysicsCarSuspensionPoint* corners[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
        for (auto* corner : corners) {
                corner->state |= in_flag;
        }
};

void PhysicsCar::remove_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag)
{

        PhysicsCarSuspensionPoint* corners[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
        for (auto* corner : corners) {
                corner->state &= ~static_cast<uint32_t>(in_flag);
        }
};

void PhysicsCar::handle_suspension_states()
{
        if (grip_frames_from_accel_press != 0) {
                grip_frames_from_accel_press -= 1;
        }

        if ((machine_state & MACHINESTATE::AIRBORNE) == 0) {
                if (base_speed > 0.1f) {
                        if ((machine_state & MACHINESTATE::B14) == 0) {
                                bool should_drift = false;
                                if (machine_state & MACHINESTATE::MANUAL_DRIFT) {
                                        if (std::abs(input_steer_yaw) > 0.1f) {
                                                should_drift = true;
                                        }
                                }
                                if (machine_state & MACHINESTATE::SPINATTACKING) {
                                        should_drift = true;
                                }
                                if (should_drift) {
                                        set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
                                }
                        } else {
                                remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
                                grip_frames_from_accel_press = stat_accel_press_grip_frames;
                        }
                }
        } else {
                remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
        }

        if ((machine_state & MACHINESTATE::STRAFING) != 0 && std::abs(input_steer_yaw) < 0.1f) {
                machine_state &= ~MACHINESTATE::STRAFING;
        }

        if (std::abs(input_strafe) > 0.3f) {
                machine_state |= MACHINESTATE::STRAFING;
        }

        if ((machine_state & MACHINESTATE::STRAFING) == 0) {
                return;
        }

        set_flag_on_all_tilt_corners(TILTSTATE::STRAFING);
};

void PhysicsCar::handle_machine_turn_and_strafe(PhysicsCarSuspensionPoint& tilt_corner, float in_angle_vel)
{
        // ───────────── Corner movement & steering matrix ─────────────
        godot::Vector3 corner_delta = tilt_corner.pos_old - tilt_corner.pos;

        bool is_drifting = (tilt_corner.state & TILTSTATE::DRIFT) != 0;
        bool is_strafing = (tilt_corner.state & TILTSTATE::STRAFING) != 0;

        mtxa->push();

        float steer_deg = -(input_steer_yaw * stat_turn_reaction + input_strafe * stat_strafe);
        steer_deg = std::clamp(steer_deg, -45.0f, 45.0f);
        mtxa->rotate_y(DEG_TO_RAD * steer_deg * 0.5f);

        corner_delta = mtxa->inverse_rotate_point(corner_delta);
        float corner_dist = corner_delta.length();
        float speed_factor = (216.0f * corner_dist) / 1000.0f;

        // ───────────── Grip / drift threshold ─────────────
        float grip_threshold = 0.0f;
        if ((!is_drifting && is_strafing) || grip_frames_from_accel_press != 0) {
                grip_threshold = 20.0f;
        } else {
                float base_grip = stat_grip_1;
                grip_threshold = base_grip;
                if ((state_2 & 4u) == 0) {
                        if (is_drifting && brake_timer == 0) {
                                grip_threshold = stat_grip_3;
                        }
                } else {
                        if (is_drifting && brake_timer < 30) {
                                grip_threshold = (base_grip >= stat_grip_3) ? stat_grip_3 : base_grip;
                        }
                }
        }

        if (std::abs(corner_delta.x) < stat_grip_3) {
                tilt_corner.state &= ~static_cast<uint32_t>(TILTSTATE::DRIFT);
        }

        bool drift_allowed = true;
        if (!is_drifting && std::abs(input_steer_yaw) <= 0.7f) {
                drift_allowed = false;
        }

        float lateral_delta = corner_delta.x;
        float drift_delta = lateral_delta;

        if (std::abs(lateral_delta) <= grip_threshold || !drift_allowed) {
                if (std::abs(lateral_delta) < 1.1920929e-7f) {
                        drift_delta = 0.0f;
                }
                tilt_corner.state &= ~static_cast<uint32_t>(TILTSTATE::DRIFT);
        } else {
                tilt_corner.state |= TILTSTATE::DRIFT;
                drift_delta = (lateral_delta < 0.0f) ? -grip_threshold : grip_threshold;
        }

        // ───────────── Global state modifiers ─────────────
        if (machine_state & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
                             MACHINESTATE::TOOKDAMAGE | MACHINESTATE::SIDEATTACKING)) {
                drift_delta = 0.0f;
        }

        if (machine_state & MACHINESTATE::RETIRED) {
                drift_delta *= 0.2f;
        } else if (machine_state & MACHINESTATE::ZEROHP) {
                float fade = std::clamp(0.01f * (static_cast<float>(frames_since_death) - 4.0f), 0.0f, 0.05f);
                drift_delta *= fade;
        }

        // ───────────── Force computation ─────────────
        if (drift_delta != 0.0f) {
                float turn_tension = stat_turn_tension;
                float weighted_delta = drift_delta * stat_weight;
                float applied_force = 0.0f;

                if (turn_tension >= 0.1f || grip_frames_from_accel_press != 0) {
                        applied_force = weighted_delta * turn_tension;
                } else if ((tilt_corner.state & TILTSTATE::AIRBORNE) == 0 &&
                           (machine_state & MACHINESTATE::JUST_PRESSED_BOOST) == 0) {
                        float rail_timer = static_cast<float>(rail_collision_timer);
                        float speed_lerp = std::clamp(speed_factor, 0.2f, 0.8f);
                        float steer_scale = 0.0f;
                        if ((tilt_corner.state & TILTSTATE::STRAFING) == 0) {
                                steer_scale = ((speed_lerp - 0.2f) / 0.6f) * (turn_tension - 0.1f) *
                                               (0.3f + 0.7f * std::abs(input_steer_yaw));
                        }
                        applied_force = weighted_delta * (0.1f + steer_scale * (1.0f - rail_timer / 20.0f));
                } else {
                        applied_force = weighted_delta * 0.1f;
                }

                if (terrain_state & TERRAIN::ICE) {
                        applied_force *= 0.003f;
                } else if (terrain_state & TERRAIN::DIRT) {
                        applied_force *= 2.0f;
                }

                godot::Vector3 local_force(applied_force, 0.0f, 0.0f);
                godot::Vector3 world_force = mtxa->rotate_point(local_force);
                tilt_corner.force_spatial += world_force;

                if (tilt_corner.state & TILTSTATE::STRAFING) {
                        applied_force *= 0.6f;
                }
                turning_related += applied_force;
        }

        // ───────────── Apply forces & torque ─────────────
        mtxa->pop();

        velocity += tilt_corner.force_spatial;

        if (rail_collision_timer < 6) {
                apply_torque_from_force(tilt_corner.offset, tilt_corner.force_spatial);
        }

        if (is_drifting && (machine_state & MACHINESTATE::JUSTHITVEHICLE_Q) == 0) {
                in_angle_vel *= stat_grip_2;
        }

        velocity_angular.y -= 0.125f * in_angle_vel;
};

void PhysicsCar::handle_linear_velocity()
{
        float vel_flat_rot_x = velocity_local_flattened_and_rotated.x;
        float vel_flat_rot_y = velocity_local_flattened_and_rotated.y;
        float vel_flat_rot_z = velocity_local_flattened_and_rotated.z;

        float neg_local_fwd_speed = -velocity_local.z;
        float abs_local_lat_speed = std::abs(velocity_local.x);

        float mag_vel_flat_rot = velocity_local_flattened_and_rotated.length();

        float drift_accel_component = 0.0f;
        if ((machine_state & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
                              MACHINESTATE::TOOKDAMAGE)) == 0 &&
            mag_vel_flat_rot > (10.0f * stat_weight) / 216.0f) {
                float norm_z_vel_flat_rot = 0.0f;
                if (mag_vel_flat_rot > 0.0001f)
                        norm_z_vel_flat_rot = vel_flat_rot_z / mag_vel_flat_rot;

                float drift_factor = 1.0f - (norm_z_vel_flat_rot * norm_z_vel_flat_rot);
                drift_accel_component = drift_factor * stat_drift_accel;
                drift_accel_component = std::min(drift_accel_component, 1.0f);
        }

        float net_fwd_accel = handle_machine_accel_and_boost(
                neg_local_fwd_speed, abs_local_lat_speed, drift_accel_component);

        float broken_factor = 1.0f; // Unused placeholder for future behavior
        float overall_damping = 0.6f + 0.55f;
        overall_damping = std::min(overall_damping, 1.0f);

        net_fwd_accel *= overall_damping;
        velocity *= overall_damping;

        if ((machine_state & MACHINESTATE::BOOSTING) == 0) {
                visual_rotation.x += 0.25f * net_fwd_accel;
        } else {
                visual_rotation.x += 0.05f * net_fwd_accel;
        }

        float airborne_factor = 1.0f;
        if (machine_state & MACHINESTATE::AIRBORNE) {
                godot::Vector3 machine_up_vector_ws = mtxa->cur->basis.get_column(2);
                float dot_prod_up_with_track_normal =
                        machine_up_vector_ws.dot(track_surface_normal);

                float alignment_factor = 3.4f * (0.3f + dot_prod_up_with_track_normal);
                alignment_factor = std::clamp(alignment_factor, 0.0f, 1.0f);
                airborne_factor = alignment_factor * alignment_factor;
        }

        mtxa->push();

        float effective_steer_degrees =
                -(input_steer_yaw * stat_turn_reaction + input_strafe * stat_strafe);
        if (machine_state & MACHINESTATE::SIDEATTACKING)
                effective_steer_degrees = 0.0f;
        effective_steer_degrees = std::clamp(effective_steer_degrees, -45.0f, 45.0f);

        turn_reaction_input = 0.75f * -(input_steer_yaw * stat_turn_reaction);
        mtxa->rotate_y(DEG_TO_RAD * effective_steer_degrees);

        godot::Vector3 local_thrust_vector(0.0f, 0.0f, -(net_fwd_accel * airborne_factor));
        godot::Vector3 world_thrust_vector = mtxa->rotate_point(local_thrust_vector);
        velocity += world_thrust_vector;

        mtxa->pop();

        float current_world_speed = velocity.length();

        if (std::abs(stat_weight) > 0.0001f &&
            current_world_speed / stat_weight > (1.0f / 1.08f)) {
                if (side_attack_delay == 6) {
                        float speed_cap_for_dash = (50.0f / 9.0f) * stat_weight;
                        float clamped_speed_for_dash = std::min(current_world_speed, speed_cap_for_dash);

                        godot::Vector3 local_dash_vector(side_attack_indicator * clamped_speed_for_dash,
                                                        0.0f, 0.0f);
                        godot::Vector3 world_dash_vector = mtxa->rotate_point(local_dash_vector);
                        velocity += world_dash_vector;
                }

                if ((terrain_state & TERRAIN::JUMP) != 0 &&
                    (machine_state & MACHINESTATE::AIRBORNE) == 0) {
                        godot::Vector3 local_jump_boost(0.0f, 1.13f * current_world_speed, 0.0f);
                        godot::Vector3 world_jump_boost = mtxa->rotate_point(local_jump_boost);

                        velocity += world_jump_boost;
                        state_2 |= 2u;
                        velocity_angular.x = 0.0f;
                        velocity_angular.z = 0.0f;
                }
        }

        input_strafe_1_6 = input_strafe_32 / 20.0f;
        input_strafe_32 += (8.0f * input_strafe - 5.0f * input_strafe_1_6);
};

float PhysicsCar::handle_machine_accel_and_boost(float neg_local_fwd_speed, float abs_local_lateral_speed, float drift_accel_factor)
{
	return 0.0f;
        float effective_accel_input = 0.0f;
        float final_thrust_output = 0.0f;

        if (!((machine_state & MACHINESTATE::ZEROHP) && frames_since_death <= 0x77)) {
                effective_accel_input = input_accel;

                if ((state_2 & 4u) == 0) {
                        if (effective_accel_input < 0.0f || input_brake > 0.0f)
                                effective_accel_input = 0.0f;
                } else if (effective_accel_input < 0.0f || brake_timer > 0x1d) {
                        effective_accel_input = 0.0f;
                }

                if ((machine_state & MACHINESTATE::ACTIVE) == 0 && effective_accel_input < 0.3f)
                        effective_accel_input = 0.0f;
        }

        if (effective_accel_input <= 0.0001f) {
                if (race_start_charge <= 0.0f) {
                        if (machine_state & MACHINESTATE::STARTINGCOUNTDOWN)
                                base_speed = 0.0f;
                } else {
                        race_start_charge -= 2.0f;
                        if (race_start_charge < 0.0f)
                                race_start_charge = 0.0f;
                        if ((machine_state & MACHINESTATE::STARTINGCOUNTDOWN) == 0)
                                base_speed = 0.0f;
                }
        } else {
                if ((machine_state & MACHINESTATE::ACTIVE) == 0) {
                        machine_state |= MACHINESTATE::ACTIVE;
                        frames_since_start_2 = 1;
                }

                if ((machine_state & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
                        if (race_start_charge > 0.0f) {
                                base_speed = 1.0f;
                                machine_state |= MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL;
                                race_start_charge = 0.0f;
                        }
                } else {
                        race_start_charge += effective_accel_input;
                }
        }

        if ((machine_state & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
                uint32_t current_machine_state = machine_state;
                float normalized_fwd_speed = neg_local_fwd_speed / stat_weight;

                if (boost_delay_frame_counter != 0) {
                        machine_state &= ~MACHINESTATE::JUST_PRESSED_BOOST;
                        boost_delay_frame_counter -= 1;
                }

                if (current_machine_state & MACHINESTATE::JUST_PRESSED_BOOST) {
                        if (boost_delay_frame_counter == 0)
                                boost_delay_frame_counter = 6;
                        else
                                boost_delay_frame_counter += 1;
                }

                current_machine_state = machine_state;
                if ((current_machine_state & MACHINESTATE::JUST_HIT_DASHPLATE) == 0) {
                        if (boost_frames == 0) {
                                bool can_manual_boost = (current_machine_state & MACHINESTATE::JUST_PRESSED_BOOST) &&
                                                        energy > 1.0f && effective_accel_input > 0.0f;
                                if (!can_manual_boost) {
                                        machine_state &= ~(MACHINESTATE::BOOSTING_DASHPLATE |
                                                           MACHINESTATE::JUST_PRESSED_BOOST |
                                                           MACHINESTATE::BOOSTING);
                                        boost_turbo -= (4.0f + 0.5f * boost_turbo) / 60.0f;
                                } else {
                                        float boost_strength_factor = 1.0f - boost_turbo / (9.0f * stat_boost_strength);
                                        float min_boost_strength_factor = 0.2f;
                                        int boost_duration_frames = static_cast<int>(60.0f * stat_boost_length);
                                        boost_frames = boost_duration_frames;
                                        boost_frames_manual = boost_duration_frames;
                                        machine_state |= MACHINESTATE::BOOSTING;
                                        machine_state &= ~MACHINESTATE::BOOSTING_DASHPLATE;

                                        boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
                                        boost_turbo += stat_boost_strength * boost_strength_factor;
                                }
                        } else {
                                machine_state &= ~MACHINESTATE::JUST_PRESSED_BOOST;
                                machine_state |= MACHINESTATE::BOOSTING;
                        }
                } else {
                        float boost_strength_factor = 1.0f - boost_turbo / (9.0f * stat_boost_strength);
                        int target_dash_boost_frames = static_cast<int>(0.5f * 60.0f * stat_boost_length);

                        if (boost_frames < static_cast<uint32_t>(target_dash_boost_frames))
                                boost_frames = target_dash_boost_frames;

                        float min_boost_strength_factor = 0.2f;
                        machine_state &= ~MACHINESTATE::JUST_PRESSED_BOOST;
                        machine_state |= MACHINESTATE::BOOSTING;

                        boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
                        boost_turbo += (2.0f * stat_boost_strength) * boost_strength_factor;
                }

                if ((machine_state & MACHINESTATE::SPINATTACKING) == 0) {
                        boost_turbo -= (2.0f + 0.5f * boost_turbo) / 60.0f;
                } else {
                        effective_accel_input *= 0.8f;
                        boost_turbo -= (3.0f + 0.5f * boost_turbo) / 60.0f;
                }
                boost_turbo = std::max(boost_turbo, 0.0f);

                if (machine_state & MACHINESTATE::BOOSTING) {
                        if (boost_frames_manual > 0) {
                                energy -= 0.1666666667f * boost_energy_use_mult;
                                boost_frames_manual -= 1;
                        }

                        if (boost_frames > 0)
                                boost_frames -= 1;

                        if (boost_frames == 0 && speed_kmh > 1200.0f) {
                                float cooldown_duration = (speed_kmh - 1200.0f) / 60.0f;
                                cooldown_duration = std::min(cooldown_duration, 10.0f);
                                if (static_cast<float>(boost_delay_frame_counter) < cooldown_duration)
                                        boost_delay_frame_counter = static_cast<uint8_t>(cooldown_duration);
                        }

                        if (energy < 0.01f) {
                                energy = 0.01f;
                                boost_frames_manual = 0;
                                if ((machine_state & MACHINESTATE::BOOSTING_DASHPLATE) == 0) {
                                        boost_frames = 0;
                                } else {
                                        int half_dash_boost_frames = static_cast<int>(0.5f * 60.0f * stat_boost_length);
                                        if (half_dash_boost_frames < static_cast<int>(boost_frames))
                                                boost_frames = half_dash_boost_frames;
                                }
                        }

                        if (boost_frames <= 0) {
                                boost_frames = 0;
                                machine_state &= ~MACHINESTATE::BOOSTING;
                        }
                }

                float accel_stat_scaled = 40.0f * stat_acceleration;
                float target_speed_component = (effective_accel_input * accel_stat_scaled) / 348.0f + base_speed;
                float speed_difference = target_speed_component - normalized_fwd_speed;

                float speed_factor_denom = 36.0f + 40.0f * stat_max_speed + boost_turbo * 2.0f;
                float speed_factor = 0.0f;
                if (std::abs(speed_factor_denom) > 0.0001f)
                        speed_factor = target_speed_component / speed_factor_denom;
                speed_factor = std::max(speed_factor, 0.0f);

                float current_accel_magnitude = speed_factor * 4.0f * (stat_acceleration * (0.6f + stat_acceleration));

                if ((machine_state & (MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::JUST_PRESSED_BOOST)) == 0) {
                        if (machine_state & MACHINESTATE::BOOSTING) {
                                current_accel_magnitude *= (stat_weight <= 1000.0f) ? 0.3f : 0.5f;
                        }
                } else {
                        current_accel_magnitude = 0.0f;
                }

                if (speed_difference > 0.0f &&
                    (normalized_fwd_speed < 0.0f || (terrain_state & TERRAIN::DIRT))) {
                        current_accel_magnitude *= 5.0f;
                }

                float final_accel_term = (1.0f - drift_accel_factor) *
                        ((speed_difference * current_accel_magnitude) +
                         ((abs_local_lateral_speed * stat_acceleration) / stat_weight) * stat_turn_decel);

                if (input_accel < 1.0f)
                        final_accel_term *= (0.05f + 0.95f * input_accel);

                base_speed = target_speed_component - final_accel_term;

                if (input_brake <= 0.0001f)
                        brake_timer = 0;
                else if (brake_timer < 0x1e)
                        brake_timer += 1;

                float brake_effect = 0.0f;
                if ((state_2 & 4u) == 0)
                        brake_effect = input_brake * (0.5f * current_accel_magnitude);
                else if (brake_timer > 0xe)
                        brake_effect = input_brake * (0.5f * current_accel_magnitude);

                brake_effect = std::min(brake_effect, 0.12f);
                base_speed = std::max(base_speed - brake_effect, 0.0f);

                base_speed = std::max(base_speed - stat_drag, 0.0f);

                float final_output_thrust_factor = speed_difference;
                if (brake_effect <= 0.0f) {
                        float modifier = 0.3f;
                        if (machine_state & MACHINESTATE::B14)
                                modifier = 1.0f;

                        if (normalized_fwd_speed < 0.0f || final_output_thrust_factor < 0.0f)
                                final_output_thrust_factor *= (0.5f * modifier);
                }

                if (machine_state & MACHINESTATE::ZEROHP) {
                        float speed_ratio_for_0hp = std::min(speed_kmh / 100.0f, 1.0f);
                        final_output_thrust_factor *= (0.2f - 0.15f * speed_ratio_for_0hp);
                }

                if ((machine_state & (MACHINESTATE::BOOSTING_DASHPLATE | MACHINESTATE::BOOSTING)) == 0) {
                        final_thrust_output = 1000.0f * final_output_thrust_factor;
                } else if (stat_weight <= 1000.0f) {
                        final_thrust_output = 1200.0f * final_output_thrust_factor;
                } else {
                        final_thrust_output = 1600.0f * final_output_thrust_factor;
                }
        } else {
                final_thrust_output = -neg_local_fwd_speed;
                base_speed = 0.014f * race_start_charge;
        }

        if ((machine_state & MACHINESTATE::ZEROHP) && frames_since_death <= 0x77) {
                if (brake_timer < 0x3d) {
                        brake_timer += 1;
                } else {
                        input_accel = 0.0f;
                        input_brake = 0.0001f;
                }
                final_thrust_output = 0.0f;
        }

        return final_thrust_output;
};

void PhysicsCar::handle_angle_velocity()
{
        float weight_val = 0.99f;

        if ((machine_state & MACHINESTATE::AIRBORNE) == 0) {
                if ((machine_state & MACHINESTATE::JUSTLANDED) == 0) {
                        weight_val = 0.05f * weight_derived_2;
                } else {
                        weight_val = 0.2f * weight_derived_2;
                }
        } else {
                velocity_angular.x *= 0.9f;
                velocity_angular.z *= weight_val;
                weight_val = weight_derived_2;
        }

        velocity_angular.y = std::clamp(velocity_angular.y, -weight_val, weight_val);
};

void PhysicsCar::handle_airborne_controls()
{
        float min_air_tilt = -50.0f;
        float max_air_tilt = 60.0f;
        bool airborne_controls_active = false;

        if (frames_since_start_2 > 60 && (machine_state & MACHINESTATE::AIRBORNE))
                airborne_controls_active = true;

        if (airborne_controls_active) {
                float tilt_effect_base = 2.0f * std::abs(input_steer_yaw);

                if (state_2 & 0x2u)
                        tilt_effect_base = 0.0f;

                float current_tilt_increment = 0.0f;
                if (tilt_effect_base >= 0.1f) {
                        current_tilt_increment = tilt_effect_base +
                                2.0f * input_steer_pitch * std::abs(2.0f - tilt_effect_base);
                        if ((machine_state & MACHINESTATE::BOOSTING) &&
                            !(machine_state & MACHINESTATE::BOOSTING_DASHPLATE))
                                current_tilt_increment *= 2.0f;
                } else {
                        current_tilt_increment = tilt_effect_base + 4.0f * input_steer_pitch;
                }

                if (air_time > 60) {
                        float air_time_factor = static_cast<float>(air_time - 60) / 120.0f;
                        air_time_factor = std::min(air_time_factor, 1.0f);

                        current_tilt_increment =
                                current_tilt_increment * (1.0f + 0.3f * air_time_factor) +
                                (0.3f * air_time_factor);
                }

                air_tilt += current_tilt_increment;
                air_tilt = std::clamp(air_tilt, min_air_tilt, max_air_tilt);
        } else {
                air_tilt = 0.0f;
        }
};

void PhysicsCar::orient_vehicle_from_gravity_or_road()
{
        float factor = 1.5f + stat_weight / 4000.0f;
        if (factor >= 1.8f) {
                factor = std::min(factor, 2.0f);
        } else {
                factor = 3.6f - factor;
        }

        float base_factor = 0.0f;
        if ((machine_state & MACHINESTATE::AIRBORNE) == 0) {
                base_factor = factor * 1.3f;
        } else if (height_above_track <= 0.0f) {
                base_factor = factor * 0.6f;
        } else {
                base_factor = (machine_state & MACHINESTATE::B10) ? factor * 1.8f
                                                               : factor * 1.3f;
        }

        float force_mag = 10.0f * -(0.009f * stat_weight) * base_factor;
        godot::Vector3 gravity_align_force = track_surface_normal * force_mag;
        velocity += gravity_align_force;

        basis_physical.basis = basis_physical.basis.orthonormalized();
        mtxa->assign(basis_physical);

        if ((machine_state & MACHINESTATE::AIRBORNE) == 0) {
                godot::Vector3 machine_world_up = mtxa->cur->basis.get_column(1);
                godot::Vector3 safe_track_normal =
                        normalized_safe(track_surface_normal, godot::Vector3(0, 1, 0));
                float dot = 0.0f;
                if (machine_world_up.length_squared() > 0.0001f)
                        dot = machine_world_up.dot(safe_track_normal);

                if (dot < 0.7f) {
                        float align_factor = 0.0f;
                        if (dot >= 0.0f)
                                align_factor = dot / 0.7f;
                        float rot_deg = 40.0f * (1.0f - align_factor);
                        float rot_rad = DEG_TO_RAD * rot_deg;
                        godot::Vector3 axis = machine_world_up.cross(safe_track_normal);
                        if (axis.length_squared() > 0.0001f) {
                                godot::Quaternion q(axis.normalized(), rot_rad);
                                godot::Transform3D old_basis = *mtxa->cur;
                                mtxa->from_quat(q);
                                mtxa->multiply(godot::Transform3D(old_basis.basis,
                                                                   godot::Vector3()));
                        }
                }
                basis_physical.basis = mtxa->cur->basis;
        } else {
                float tilt_rad = DEG_TO_RAD * air_tilt;
                float c = std::cos(tilt_rad);
                float s = std::sin(tilt_rad);
                godot::Vector3 local_tilted_up(0.0f, c, s);
                godot::Vector3 world_tilted_up = mtxa->rotate_point(local_tilted_up);
                godot::Vector3 safe_world_up = normalized_safe(world_tilted_up, godot::Vector3(0, 1, 0));
                godot::Vector3 safe_track_normal =
                        normalized_safe(track_surface_normal, godot::Vector3(0, 1, 0));
                float dot = safe_world_up.dot(safe_track_normal);

                if (dot < 0.992f) {
                        float adjusted_dot = dot + 0.008f;
                        float base_rot_deg = 15.0f;
                        godot::Vector3 axis = safe_world_up.cross(safe_track_normal);
                        float axis_thresh = 0.1f * 0.1f;
                        if (axis.length_squared() < axis_thresh || adjusted_dot < 0.008f) {
                                godot::Vector3 cur_up =
                                        normalized_safe(mtxa->cur->basis.get_column(1),
                                                        godot::Vector3(0, 1, 0));
                                float dot_up = cur_up.dot(safe_track_normal);

                                if (dot_up <= 0.0f) {
                                        godot::Vector3 machine_x =
                                                normalized_safe(mtxa->cur->basis.get_column(0),
                                                                godot::Vector3(1, 0, 0));
                                        axis = mtxa->cur->basis.get_column(2);
                                        float dot_track_vs_x = safe_track_normal.dot(machine_x);
                                        if (dot_track_vs_x > 0.0f)
                                                axis = -axis;
                                }
                        }

                        if (axis.length_squared() > 0.0001f) {
                                godot::Vector3 norm_axis = axis.normalized();
                                float sq_dot = std::max(0.0f, adjusted_dot * adjusted_dot);
                                float rot_deg = base_rot_deg * (1.0f - sq_dot);
                                float rot_rad = DEG_TO_RAD * rot_deg;
                                godot::Quaternion q(norm_axis, rot_rad);
                                godot::Transform3D old_basis = *mtxa->cur;
                                mtxa->from_quat(q);
                                mtxa->multiply(godot::Transform3D(old_basis.basis,
                                                                   godot::Vector3()));
                        }
                }
                basis_physical.basis = mtxa->cur->basis;
        }
};

void PhysicsCar::handle_drag_and_glide_forces()
{
        float speed = velocity.length();
        float speed_weight_ratio = 0.0f;
        if (std::abs(stat_weight) > 0.0001f)
                speed_weight_ratio = speed / stat_weight;

        float scaled_speed = 216.0f * speed_weight_ratio;

        if (scaled_speed < 2.0f) {
                velocity = godot::Vector3();
                visual_shake_mult = 0.0f;
                return;
        }

        if (scaled_speed > 9990.0f) {
                float len = velocity.length();
                if (len > 0.0001f)
                        velocity *= 46.25f / len;
                return;
        }

        godot::Vector3 vel_norm = normalized_safe(velocity, godot::Vector3());
        float alignment_with_normal = track_surface_normal.dot(vel_norm);

        godot::Vector3 forward_world =
                normalized_safe(mtxa->rotate_point(godot::Vector3(0, 0, -1)),
                                godot::Vector3(0, 0, -1));
        float forward_normal_alignment =
                track_surface_normal.dot(forward_world);

        godot::Vector3 normal_force =
                track_surface_normal *
                (stat_weight * alignment_with_normal * speed_weight_ratio);
        float base_drag_mag = speed_weight_ratio * speed_weight_ratio * 8.0f;
        godot::Vector3 drag_vector = velocity - normal_force;

        if (machine_state & MACHINESTATE::AIRBORNE) {
                if (forward_normal_alignment < 0.0f)
                        base_drag_mag *= std::max(0.0f, 1.0f + forward_normal_alignment);
                forward_normal_alignment += 1.0f; // shift to 0 -> 2 range
        }

        float drag_len = drag_vector.length();
        if (drag_len > 0.0001f)
                drag_vector *= base_drag_mag / drag_len;
        else
                drag_vector = godot::Vector3();

        visual_shake_mult = base_drag_mag;

        if (stat_weight < 1100.0f) {
                float weight_scale = stat_weight / 1100.0f;
                alignment_with_normal *= weight_scale * weight_scale;
        }

        bool boosting = (machine_state & MACHINESTATE::BOOSTING) != 0;
        bool airborne = (machine_state & MACHINESTATE::AIRBORNE) != 0;
        float drag_coeff = 0.0f;

        if (boosting) {
                drag_coeff = alignment_with_normal * 0.5f;
        } else if (airborne) {
                if (alignment_with_normal >= 0.0f || forward_normal_alignment <= 0.8f) {
                        drag_coeff = alignment_with_normal * 0.6f;
                } else {
                        drag_coeff =
                                alignment_with_normal *
                                (0.6f + 4.0f * (forward_normal_alignment - 0.8f));
                }
        } else {
                drag_coeff = alignment_with_normal * 0.6f;
        }

        drag_vector += track_surface_normal * (base_drag_mag * drag_coeff);

        if (frames_since_death != 0) {
                float death_fade =
                        std::clamp(0.01f * static_cast<float>(frames_since_death - 4),
                                   0.0f, 1.0f);
                drag_vector *= std::max(1.0f, death_fade);
        }

        velocity -= drag_vector;
};

void PhysicsCar::rotate_machine_from_angle_velocity()
{
        godot::Vector3 processed_ang_vel;

        const float deadzone_threshold = 3.0f;

        float val_x = velocity_angular.x;
        if (std::abs(val_x) <= deadzone_threshold)
                processed_ang_vel.x = 0.0f;
        else
                processed_ang_vel.x = val_x - ((val_x > 0.0f) - (val_x < 0.0f)) * deadzone_threshold;

        float val_z = velocity_angular.z;
        if (std::abs(val_z) <= deadzone_threshold)
                processed_ang_vel.z = 0.0f;
        else
                processed_ang_vel.z = val_z - ((val_z > 0.0f) - (val_z < 0.0f)) * deadzone_threshold;

        processed_ang_vel.y = velocity_angular.y;

        if (std::abs(weight_derived_1) > 0.0001f)
                processed_ang_vel.x /= weight_derived_1;
        else
                processed_ang_vel.x = 0.0f;

        if (std::abs(weight_derived_2) > 0.0001f)
                processed_ang_vel.y /= weight_derived_2;
        else
                processed_ang_vel.y = 0.0f;

        if (std::abs(weight_derived_3) > 0.0001f)
                processed_ang_vel.z /= weight_derived_3;
        else
                processed_ang_vel.z = 0.0f;

        float rotation_angle_rad = processed_ang_vel.length();
        if (rotation_angle_rad > 0.0001f) {
                godot::Vector3 rotation_axis = processed_ang_vel.normalized();
                godot::Quaternion delta_q(rotation_axis, rotation_angle_rad);

                mtxa->push();
                mtxa->from_quat(delta_q);
                godot::Transform3D delta_transform = *mtxa->cur;
                mtxa->pop();

                mtxa->multiply(delta_transform);
        }
};

void PhysicsCar::handle_startup_wobble()
{

};

void PhysicsCar::initialize_machine()
{

};

void PhysicsCar::update_machine_stats()
{

};

void PhysicsCar::reset_machine(int reset_type)
{
        // Clear all velocity and collision vectors
        velocity = godot::Vector3();
        velocity_local_flattened_and_rotated = godot::Vector3();
        velocity_local = godot::Vector3();
        velocity_angular = godot::Vector3();
        collision_push_total = godot::Vector3();
        collision_push_rail = godot::Vector3();
        collision_push_track = godot::Vector3();

        track_surface_normal = mtxa->rotate_point(godot::Vector3(0, 1, 0));

        // Placeholder spawn values until StageOverseer is ported
        godot::Vector3 spawn_pos = godot::Vector3();
        float spawn_rot = 0.0f;

        position_current = spawn_pos;
        position_old = spawn_pos;
        position_old_2 = spawn_pos;
        position_old_dupe = spawn_pos;

        position_bottom = mtxa->transform_point(godot::Vector3(0.0f, -0.1f, 0.0f));

        position_old_2 = godot::Vector3(0, 5, 0);
        input_steer_yaw = 0.0f;
        input_yaw_dupe = 0.0f;
        visual_shake_mult = 0.0f;
        input_accel = 0.0f;
        input_brake = 0.0f;
        input_strafe = 0.0f;
        input_steer_pitch = 0.0f;
        height_above_track = 0.0f;
        current_checkpoint = 0;
        checkpoint_fraction = 0.0f;
        lap = 1;
        visual_rotation = godot::Vector3();

        energy = calced_max_energy;
        boost_frames_manual = 0;
        air_tilt = 0.0f;
        boost_frames = 0;
        input_strafe_32 = 0.0f;
        input_strafe_1_6 = 0.0f;
        frames_since_start_2 = 0;
        speed_kmh = 0.0f;
        race_start_charge = 0.0f;

        height_adjust_from_boost = 0.0f;
        grip_frames_from_accel_press = 0;
        air_time = 0;
        spinattack_angle = 0.0f;
        spinattack_decrement = 0.0f;
        spinattack_direction = 0;
        damage_from_last_hit = 0.0f;
        frames_since_start = 0;
        side_attack_delay = 0;
        brake_timer = 0;
        rail_collision_timer = 0;
        terrain_state = 0;
        machine_collision_frame_counter = 0;
        frames_since_death = 0;
        turning_related = 0.0f;
        machine_crashed = false;
        boost_delay_frame_counter = 0;
        car_hit_invincibility = 0;
        turn_reaction_input = 0.0f;
        turn_reaction_effect = 0.0f;
        boost_energy_use_mult = 1.0f;

        // Orient the machine at the spawn position
        mtxa->push();
        mtxa->cur->origin = spawn_pos;
        basis_physical.basis = godot::Basis().rotated(godot::Vector3(0, 1, 0), spawn_rot + PI);
        basis_physical_other.basis = godot::Basis().rotated(godot::Vector3(0, 1, 0), spawn_rot + PI);

        rotate_mtxa_from_diff_btwn_machine_front_and_back();
        mtxa->pop();

        // Visual transform matches physical orientation at reset
        mtxa->push();
        transform_visual = *mtxa->cur;
        mtxa->pop();

        base_speed = 0.0f;
        boost_turbo = 0.0f;
        position_behind = godot::Vector3();

        uint32_t state_mask_common = MACHINESTATE::B30 | MACHINESTATE::COMPLETEDRACE_2_Q |
                                    MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::B10 |
                                    MACHINESTATE::B9;
        if (reset_type == 0) {
                machine_state &= state_mask_common;
                state_2 &= 1u;
        } else {
                machine_state &= state_mask_common;
        }

        state_2 &= 0xfffffc4fu;
        level_start_time = 0;

        godot::Transform3D initial_placement_transform(basis_physical.basis, position_current);

        PhysicsCarSuspensionPoint* tilt_corners[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
        PhysicsCarCollisionPoint* wall_corners[4] = {&wall_fl, &wall_fr, &wall_bl, &wall_br};

        for (int i = 0; i < 4; ++i) {
                auto* tc = tilt_corners[i];
                auto* wc = wall_corners[i];

                tc->state = 0;
                tc->force = 0.0f;
                tc->force_spatial_len = 0.0f;

                tc->pos_old = mtxa->transform_point(tc->offset);
                tc->pos = tc->pos_old;

                tc->force_spatial = godot::Vector3();
                tc->up_vector_2 = mtxa->rotate_point(godot::Vector3(0, 1, 0));
                tc->up_vector = tc->up_vector_2;

                wc->pos_a = mtxa->transform_point(godot::Vector3(0.0f, 0.1f, 0.0f));
                wc->pos_b = mtxa->transform_point(wc->offset);
                wc->collision = godot::Vector3();
        }
};

void PhysicsCar::rotate_mtxa_from_diff_btwn_machine_front_and_back()
{

};

void PhysicsCar::update_suspension_forces(PhysicsCarSuspensionPoint& in_corner)
{

};

godot::Vector3 get_avg_track_normal_from_tilt_corners()
{
	return godot::Vector3();
};

void PhysicsCar::set_terrain_state_from_track()
{
        uint32_t terrain_bits = 0;
        if ((machine_state & MACHINESTATE::AIRBORNE) == 0 && current_track != nullptr) {
                CollisionData hit;
                current_track->cast_vs_track(hit, position_old, position_current,
                                             CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_TERRAIN,
                                             current_checkpoint);
                if (hit.collided) {
                        terrain_bits |= hit.road_data.terrain;
                }
        } else {
                terrain_bits = 0;
        }

        if (terrain_bits & TERRAIN::DASH) {
                machine_state |= MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::BOOSTING_DASHPLATE;
                terrain_state |= TERRAIN::DASH;
        }

        if ((terrain_bits & TERRAIN::RECHARGE) && (machine_state & MACHINESTATE::ZEROHP) == 0) {
                state_2 |= 1;
                terrain_state |= TERRAIN::RECHARGE;
        }

        if ((machine_state & MACHINESTATE::BOOSTING) == 0 && (terrain_bits & TERRAIN::DIRT)) {
                terrain_state |= TERRAIN::DIRT;
        }

        if (terrain_bits & TERRAIN::ICE) {
                terrain_state |= TERRAIN::ICE;
        }

        if (terrain_bits & TERRAIN::JUMP) {
                terrain_state |= TERRAIN::JUMP;
        }

        if (terrain_bits & TERRAIN::LAVA) {
                terrain_state |= TERRAIN::LAVA;
        }
};

void PhysicsCar::handle_attack_states()
{

};

void PhysicsCar::apply_torque_from_force(const godot::Vector3& p_local_offset, const godot::Vector3& wf_world_force)
{

};

void PhysicsCar::simulate_machine_motion()
{
	PlayerInput current_input = PlayerInput::from_player_input();
        PlayerInput inputs = PlayerInput::from_player_input();

        input_steer_yaw = -inputs.steer_horizontal * std::abs(inputs.steer_horizontal);
        input_steer_pitch = inputs.steer_vertical;

        float in_strafe_left = std::min(1.0f, inputs.strafe_left * 1.25f);
        float in_strafe_right = std::min(1.0f, inputs.strafe_right * 1.25f);
        input_strafe = (-in_strafe_left + in_strafe_right);

        input_accel = inputs.accelerate;
        bool accel_just_pressed = godot::Input::get_singleton()->is_action_just_pressed("Accelerate");
        input_brake = inputs.brake;
        bool brake_just_pressed = godot::Input::get_singleton()->is_action_just_pressed("Brake");

        float in_spinattack = inputs.spinattack ? 1.0f : 0.0f;
        float in_sideattack = 0.0f; // Placeholder: side attack not mapped

        if (in_strafe_left > 0.05f && in_strafe_right > 0.05f) {
                machine_state |= MACHINESTATE::MANUAL_DRIFT;
        }

        if (accel_just_pressed) {
                machine_state |= MACHINESTATE::JUSTTAPPEDACCEL | MACHINESTATE::B14;
        }

        state_2 |= 8u;

        if (godot::Input::get_singleton()->is_action_just_pressed("DPadUp")) {
                reset_machine(1);
                level_start_time -= 270;
        }

        godot::Vector3 ground_normal = prepare_machine_frame();
        bool has_floor = find_floor_beneath_machine();
        if (has_floor) {
                track_surface_normal = ground_normal;
        } else {
                PhysicsCarSuspensionPoint* tcs[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
                for (auto* tc : tcs) {
                        tc->force = 0.0f;
                        tc->force_spatial = godot::Vector3();
                        tc->force_spatial_len = 0.0f;
                        tc->state |= TILTSTATE::DISCONNECTED | TILTSTATE::AIRBORNE;
                }
        }

        handle_steering();
        handle_suspension_states();

        float initial_angle_vel_y = velocity_angular.y;
        if (frames_since_start_2 > 0) {
                PhysicsCarSuspensionPoint* tcs[4] = {&tilt_fl, &tilt_fr, &tilt_bl, &tilt_br};
                for (auto* tc : tcs) {
                        handle_machine_turn_and_strafe(*tc, initial_angle_vel_y);
                }
        }

        if (machine_state & MACHINESTATE::AIRBORNEMORE0_2S_Q) {
                turning_related *= 0.02f;
        }
        if (std::abs(input_strafe) > 0.01f) {
                turning_related *= 0.04f;
        }

        handle_linear_velocity();
        handle_angle_velocity();

        mtxa->assign(basis_physical);
        handle_airborne_controls();
        orient_vehicle_from_gravity_or_road();
        handle_drag_and_glide_forces();

        float inv_weight = 1.0f / std::max(stat_weight, 0.001f);
        position_current += velocity * inv_weight;

        mtxa->cur->origin = position_current;
        rotate_machine_from_angle_velocity();
        mtxa->clear_translation();
        basis_physical = *mtxa->cur;

        if (machine_state & MACHINESTATE::STARTINGCOUNTDOWN) {
                machine_state &= ~(MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL);
        }

        if (machine_state & MACHINESTATE::ACTIVE) {
                uint32_t cd = frames_since_start_2;
                if (cd < 30) {
                        if (cd % 6 == 0) {
                                handle_startup_wobble();
                        }
                } else if (cd < 90) {
                        velocity_angular = godot::Vector3();
                }
        }

        if (rail_collision_timer > 0) {
                rail_collision_timer -= 1;
        }

        machine_state &= ~(MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
                           MACHINESTATE::TOOKDAMAGE | MACHINESTATE::B14 |
                           MACHINESTATE::MANUAL_DRIFT);

        basis_physical = basis_physical.orthonormalized();
        if ((machine_state & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
                position_bottom += position_current - position_old;
        }
};

int PhysicsCar::update_machine_corners()
{
	return 0;
};

void PhysicsCar::create_machine_visual_transform()
{
        float fVar12_initial_factor = 0.0f;
        if (base_speed <= 2.0f)
                fVar12_initial_factor = (2.0f - base_speed) * 0.5f;

        if (frames_since_start_2 < 90)
                fVar12_initial_factor *= static_cast<float>(frames_since_start_2) / 90.0f;

        unk_stat_0x5d4 += 0.05f * (fVar12_initial_factor - unk_stat_0x5d4);

        float dVar11_current_unk_stat = unk_stat_0x5d4;

        float sin_val2_scaled_angle = static_cast<float>(g_anim_timer * 0x1a3);
        float sin_val2 = std::sin(sin_val2_scaled_angle);

        float y_offset_base = 0.006f * (dVar11_current_unk_stat * sin_val2);

        godot::Vector3 visual_y_offset_world =
                mtxa->rotate_point(godot::Vector3(0.0f,
                                                  y_offset_base - (0.2f * dVar11_current_unk_stat),
                                                  0.0f));
        godot::Vector3 target_visual_world_position = position_current + visual_y_offset_world;

        mtxa->cur->basis = mtxa->cur->basis.orthonormalized();
        mtxa->cur->origin = godot::Vector3();
        basis_physical = basis_physical.orthonormalized();
        mtxa->assign(basis_physical);

        mtxa->push();
        float fr_offset_z = tilt_fl.offset.z;
        float br_offset_z = tilt_bl.offset.z;
        float stagger_factor = 0.0f;
        if (std::abs(fr_offset_z) > 0.0001f)
                stagger_factor = (br_offset_z / -fr_offset_z) - 1.0f;
        float clamped_stagger = std::clamp(stagger_factor, -0.2f, 0.2f);
        float pitch_angle_deg = 30.0f * clamped_stagger;
        mtxa->rotate_x(DEG_TO_RAD * pitch_angle_deg);
        g_pitch_mtx_0x5e0 = *mtxa->cur;
        mtxa->pop();

        if ((state_2 & 0x20u) == 0) {
                mtxa->push();
                mtxa->identity();
                if (machine_state & MACHINESTATE::ACTIVE) {
                        turn_reaction_effect += 0.05f * (turn_reaction_input - turn_reaction_effect);
                        float yaw_reaction_rad = DEG_TO_RAD * turn_reaction_effect;
                        mtxa->rotate_y(yaw_reaction_rad);
                }

                float world_vel_mag = velocity.length();
                float speed_factor_for_roll_pitch = 0.0f;
                if (std::abs(stat_weight) > 0.0001f)
                        speed_factor_for_roll_pitch = (world_vel_mag / stat_weight) / 4.629629629f;

                strafe_visual_roll = static_cast<int>(182.04445f * (stat_strafe / 15.0f) * -5.0f *
                                                     input_strafe_1_6 * speed_factor_for_roll_pitch);

                float banking_roll_angle_val_rad = 0.0f;
                if (std::abs(weight_derived_2) > 0.0001f)
                        banking_roll_angle_val_rad =
                                speed_factor_for_roll_pitch * 4.5f * (velocity_angular.y / weight_derived_2);
                int banking_roll_angle_fz_units = static_cast<int>(10430.378f * banking_roll_angle_val_rad);

                int total_roll_fz_units = banking_roll_angle_fz_units + strafe_visual_roll;

                float abs_total_roll_float = std::abs(static_cast<float>(total_roll_fz_units));

                float roll_damping_factor = 1.0f - abs_total_roll_float / 3640.0f;
                roll_damping_factor = std::max(roll_damping_factor, 0.0f);

                float current_visual_pitch_rad = 0.0f;
                if (std::abs(weight_derived_1) > 0.0001f)
                        current_visual_pitch_rad = visual_rotation.x / weight_derived_1;
                float pitch_visual_factor = roll_damping_factor * 0.7f * current_visual_pitch_rad;
                pitch_visual_factor = std::clamp(pitch_visual_factor, -0.3f, 0.3f);

                float current_visual_roll_rad = 0.0f;
                if (std::abs(weight_derived_3) > 0.0001f)
                        current_visual_roll_rad = visual_rotation.z / weight_derived_3;
                float roll_visual_factor = 2.5f * current_visual_roll_rad;
                roll_visual_factor = std::clamp(roll_visual_factor, -0.5f, 0.5f);

                mtxa->rotate_x(pitch_visual_factor);

                float iVar1_from_block2_approx_deg =
                        0.5f * (dVar11_current_unk_stat *
                                std::sin(static_cast<float>(g_anim_timer * 0x109) *
                                         (TAU / 65536.0f)));
                int additional_roll_from_sin_fz_units =
                        static_cast<int>(182.04445f * iVar1_from_block2_approx_deg);

                total_roll_fz_units += static_cast<int>(10430.378f * -roll_visual_factor);
                total_roll_fz_units = std::clamp(total_roll_fz_units, -0x238e, 0x238e);

                int final_roll_fz_units_for_z_rot = total_roll_fz_units + additional_roll_from_sin_fz_units;
                float final_roll_rad_for_z_rot =
                        static_cast<float>(final_roll_fz_units_for_z_rot) * (TAU / 65536.0f);
                mtxa->rotate_z(final_roll_rad_for_z_rot);

                godot::Quaternion visual_delta_q = mtxa->cur->basis.get_rotation_quaternion();

                unk_quat_0x5c4 = unk_quat_0x5c4.slerp(visual_delta_q, 0.2f);
                mtxa->from_quat(unk_quat_0x5c4);

                godot::Transform3D slerped_visual_rotation_transform = *mtxa->cur;
                mtxa->pop();

                mtxa->multiply(slerped_visual_rotation_transform);

                if (spinattack_angle != 0.0f) {
                        if (spinattack_direction == 0)
                                mtxa->rotate_y(spinattack_angle);
                        else
                                mtxa->rotate_y(-spinattack_angle);
                }
        } else {
                mtxa->assign(transform_visual);
        }

        mtxa->cur->origin = target_visual_world_position;

        uint32_t uVar8_shake_seed = static_cast<uint32_t>(velocity.z * 4000000.0f) ^
                                   static_cast<uint32_t>(velocity.x * 4000000.0f) ^
                                   static_cast<uint32_t>(velocity.y * 4000000.0f);

        float shake_rand_norm1 =
                static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(velocity_angular.x * 4000000.0f)) &
                                  0xffff) /
                65535.0f;
        float shake_rand_norm2 =
                static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(velocity_angular.y * 4000000.0f)) &
                                  0xffff) /
                65535.0f;

        float shake_magnitude = 0.00006f * visual_shake_mult;
        float x_shake_rad = shake_magnitude * shake_rand_norm1;
        float z_shake_rad = shake_magnitude * shake_rand_norm2;
        mtxa->rotate_z(z_shake_rad);
        mtxa->rotate_x(x_shake_rad);

        if ((machine_state & MACHINESTATE::BOOSTING) == 0) {
                height_adjust_from_boost -= 0.05f * height_adjust_from_boost;
        } else {
                float effective_pitch_for_boost_lift = std::max(0.0f, visual_rotation.x);
                float target_height_adj = 0.0f;
                if (std::abs(weight_derived_1) > 0.0001f)
                        target_height_adj = 4.5f * (effective_pitch_for_boost_lift / weight_derived_1);

                height_adjust_from_boost += 0.2f * (target_height_adj - height_adjust_from_boost);
                height_adjust_from_boost = std::min(height_adjust_from_boost, 0.3f);
        }

        mtxa->cur->origin += mtxa->cur->basis.y * height_adjust_from_boost;

        if (terrain_state & TERRAIN::DIRT) {
                float jitter_scale_factor = 0.1f + speed_kmh / 900.0f;
                jitter_scale_factor = std::min(jitter_scale_factor, 1.0f);

                float rand_x_norm =
                        static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(velocity_angular.y * 4000000.0f)) &
                                          0xffff) /
                                65535.0f -
                        0.5f;
                float rand_z_norm =
                        static_cast<float>((uVar8_shake_seed ^ static_cast<uint32_t>(velocity_angular.z * 4000000.0f)) &
                                          0xffff) /
                                65535.0f -
                        0.5f;

                godot::Vector3 local_jitter_offset(rand_x_norm, 0.0f, rand_z_norm);
                godot::Vector3 world_jitter_offset = mtxa->rotate_point(local_jitter_offset);

                godot::Vector3 scaled_world_jitter = world_jitter_offset * (0.15f * jitter_scale_factor);
                mtxa->cur->origin += scaled_world_jitter;
        }

        transform_visual = *mtxa->cur;
};

void PhysicsCar::handle_machine_collision_response()
{

};

void PhysicsCar::align_machine_y_with_track_normal_immediate()
{

};

void PhysicsCar::post_tick()
{

};

void PhysicsCar::tick(const PlayerInput& frame_input /* , uint64_t current_game_tick, other game state if needed */)
{

};