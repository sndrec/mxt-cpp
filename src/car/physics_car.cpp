
#include "physics_car.h"
#include "physics_car_internal.h"
#include "main.h"
#include "godot_cpp/variant/plane.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/curve.h"
#include "mxt_core/enums.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include "mxt_core/debug.hpp"
#include "mxt_core/math_utils.h"
#include "mxt_core/player_input.h"
#include "track/track_segment.h"
#include "track/trigger_collider.h"

PhysicsCar::PhysicsCar(PhysicsCarSoA* p_soa, int p_index)
	: soa(p_soa),
	  soa_index(p_index)
{
#define RESET_PHYSICS_CAR_SOA_VALUE(type, name, default_value) soa->name[soa_index] = default_value;
	PHYSICS_CAR_SCALAR_FIELDS(RESET_PHYSICS_CAR_SOA_VALUE)
#undef RESET_PHYSICS_CAR_SOA_VALUE
#define RESET_PHYSICS_CAR_SOA_VEC3(name, default_value) { SimVec3 v = default_value; soa->name##_x[soa_index] = v.x; soa->name##_y[soa_index] = v.y; soa->name##_z[soa_index] = v.z; }
	PHYSICS_CAR_VEC3_FIELDS(RESET_PHYSICS_CAR_SOA_VEC3)
#undef RESET_PHYSICS_CAR_SOA_VEC3
#define RESET_PHYSICS_CAR_SOA_TRANSFORM(name, default_value) STORE_TRANSFORM(name, default_value);
	PHYSICS_CAR_TRANSFORM_FIELDS(RESET_PHYSICS_CAR_SOA_TRANSFORM)
#undef RESET_PHYSICS_CAR_SOA_TRANSFORM
	for (int lane = 0; lane < 4; ++lane) {
		const int p = POINT_INDEX(lane);
#define RESET_PHYSICS_CAR_TILT_SOA_VALUE(type, name, default_value) soa->tilt_##name[p] = default_value;
		PHYSICS_CAR_TILT_SCALAR_FIELDS(RESET_PHYSICS_CAR_TILT_SOA_VALUE)
#undef RESET_PHYSICS_CAR_TILT_SOA_VALUE
#define RESET_PHYSICS_CAR_TILT_SOA_VEC3(name, default_value) { SimVec3 v = default_value; soa->tilt_##name##_x[p] = v.x; soa->tilt_##name##_y[p] = v.y; soa->tilt_##name##_z[p] = v.z; }
		PHYSICS_CAR_TILT_VEC3_FIELDS(RESET_PHYSICS_CAR_TILT_SOA_VEC3)
#undef RESET_PHYSICS_CAR_TILT_SOA_VEC3
#define RESET_PHYSICS_CAR_WALL_SOA_VEC3(name, default_value) { SimVec3 v = default_value; soa->wall_##name##_x[p] = v.x; soa->wall_##name##_y[p] = v.y; soa->wall_##name##_z[p] = v.z; }
		PHYSICS_CAR_WALL_VEC3_FIELDS(RESET_PHYSICS_CAR_WALL_SOA_VEC3)
#undef RESET_PHYSICS_CAR_WALL_SOA_VEC3
	}
}

void PhysicsCar::handle_steering()
{
	if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) == 0) {
		return;
	}

	float strafe_turn_mod = 1.0f;
	for (int i = 0; i < 4; ++i) {
		if (soa->tilt_state[POINT_INDEX(i)] & TILTSTATE::DRIFT) {
			strafe_turn_mod -= 0.25f;
		}
	}

	float steer_strength =
	(soa->stat_turn_movement[soa_index] + strafe_turn_mod * soa->stat_strafe_turn[soa_index] * soa->input_strafe[soa_index] *
		soa->input_steer_yaw[soa_index]) *
	-soa->input_steer_yaw[soa_index];
	if (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) {
		steer_strength *= 0.3f;
	}

	soa->velocity_angular_y[soa_index] += 1.5f * steer_strength;

	if (std::abs(soa->velocity_angular_y[soa_index]) < 1.0f) {
		soa->velocity_angular_y[soa_index] = 0.0f;
	}

};
void PhysicsCar::set_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag)
{
	for (int i = 0; i < 4; ++i) {
		soa->tilt_state[POINT_INDEX(i)] |= in_flag;
	}
};

void PhysicsCar::remove_flag_on_all_tilt_corners(TILTSTATE::FLAGS in_flag)
{
	for (int i = 0; i < 4; ++i) {
		soa->tilt_state[POINT_INDEX(i)] &= ~static_cast<uint32_t>(in_flag);
	}
};

void PhysicsCar::handle_suspension_states()
{
	if (soa->grip_frames_from_accel_press[soa_index] != 0) {
		soa->grip_frames_from_accel_press[soa_index] -= 1;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		if (soa->base_speed[soa_index] > 0.1f) {
			if ((soa->machine_state[soa_index] & MACHINESTATE::B14) == 0) {
				bool should_drift = false;
				if (soa->machine_state[soa_index] & MACHINESTATE::MANUAL_DRIFT) {
					if (std::abs(soa->input_steer_yaw[soa_index]) > 0.1f) {
						should_drift = true;
					}
				}
				if (should_drift) {
					set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
				}
			} else {
				remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
				soa->grip_frames_from_accel_press[soa_index] = soa->stat_accel_press_grip_frames[soa_index];
				soa->drift_ramp[soa_index] = 0.0f;
			}
		}
	} else {
		remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
		soa->drift_ramp[soa_index] = 0.0f;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STRAFING) != 0 && std::abs(soa->input_steer_yaw[soa_index]) < 0.1f) {
		soa->machine_state[soa_index] &= ~MACHINESTATE::STRAFING;
	}

	if (std::abs(soa->input_strafe[soa_index]) > 0.3f) {
		soa->machine_state[soa_index] |= MACHINESTATE::STRAFING;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STRAFING) == 0) {
		return;
	}

	set_flag_on_all_tilt_corners(TILTSTATE::STRAFING);
};

void PhysicsCar::handle_machine_turn_and_strafe_points4(float in_angle_vel)
{
	const int point_base = soa_index * 4;
	float steer_deg =
		-(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index] + soa->input_strafe[soa_index] * soa->stat_strafe[soa_index]);
	steer_deg = std::clamp(steer_deg, -45.0f, 45.0f);
	SimTransform steer_basis = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_y(steer_basis, DEG_TO_RAD * steer_deg * 0.5f);

	const SimFloat4 dx = sim_load4(soa->tilt_pos_old_x + point_base) - sim_load4(soa->tilt_pos_x + point_base);
	const SimFloat4 dy = sim_load4(soa->tilt_pos_old_y + point_base) - sim_load4(soa->tilt_pos_y + point_base);
	const SimFloat4 dz = sim_load4(soa->tilt_pos_old_z + point_base) - sim_load4(soa->tilt_pos_z + point_base);
	const SimBasis& b = steer_basis.basis;
	const SimFloat4 lx = SimFloat4(b[0].x) * dx + SimFloat4(b[0].y) * dy + SimFloat4(b[0].z) * dz;
	const SimFloat4 ly = SimFloat4(b[1].x) * dx + SimFloat4(b[1].y) * dy + SimFloat4(b[1].z) * dz;
	const SimFloat4 lz = SimFloat4(b[2].x) * dx + SimFloat4(b[2].y) * dy + SimFloat4(b[2].z) * dz;
	const SimFloat4 speed = sim_sqrt4(lx * lx + ly * ly + lz * lz) * SimFloat4(216.0f / 1000.0f);

	float local_x[4];
	float local_y[4];
	float local_z[4];
	float speed_factor[4];
	sim_store4(local_x, lx);
	sim_store4(local_y, ly);
	sim_store4(local_z, lz);
	sim_store4(speed_factor, speed);
	for (int lane = 0; lane < 4; ++lane) {
		handle_machine_turn_and_strafe(lane, in_angle_vel, SimVec3(local_x[lane], local_y[lane], local_z[lane]), speed_factor[lane], steer_basis);
	}
}

void PhysicsCar::handle_machine_turn_and_strafe(
    int point_lane, float in_angle_vel, const SimVec3& corner_delta_local, float speed_factor, const SimTransform& steer_basis) {
	const int p = POINT_INDEX(point_lane);
    // ───────────── Corner movement & steering matrix ─────────────
    SimVec3 corner_delta = corner_delta_local;

    bool is_drifting = (soa->tilt_state[p] & TILTSTATE::DRIFT) != 0;
    bool is_strafing = (soa->tilt_state[p] & TILTSTATE::STRAFING) != 0;

    // ───────────── Grip / drift threshold ─────────────
    float grip_threshold = 0.0f;
    if ((!is_drifting && is_strafing) || soa->grip_frames_from_accel_press[soa_index] != 0) {
        grip_threshold = 20.0f;
    } else {
        float base_grip = soa->stat_grip_1[soa_index];
        grip_threshold = base_grip;
        if ((soa->state_2[soa_index] & 4u) == 0) {
            if (is_drifting && soa->brake_timer[soa_index] == 0) {
                grip_threshold = soa->stat_grip_3[soa_index];
            }
        } else {
            if (is_drifting && soa->brake_timer[soa_index] < 30) {
                grip_threshold =
                    (base_grip >= soa->stat_grip_3[soa_index]) ? soa->stat_grip_3[soa_index] : base_grip;
            }
        }
    }

    if (std::abs(corner_delta.x) < soa->stat_grip_3[soa_index]) {
        soa->drift_ramp[soa_index] = 0.0f;
        soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::DRIFT);
    }

    bool drift_allowed = true;
    if (!is_drifting && std::abs(soa->input_steer_yaw[soa_index]) <= 0.7f) {
        drift_allowed = false;
    }

    float lateral_delta = corner_delta.x;
    float drift_delta = lateral_delta;

    if (std::abs(lateral_delta) <= grip_threshold || !drift_allowed) {
        if (std::abs(lateral_delta) < 1.1920929e-7f) {
            drift_delta = 0.0f;
        }
        soa->drift_ramp[soa_index] = 0.0f;
        soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::DRIFT);
    } else {
        soa->tilt_state[p] |= TILTSTATE::DRIFT;
        drift_delta = (lateral_delta < 0.0f) ? -grip_threshold : grip_threshold;
    }

    // ───────────── Global state modifiers ─────────────
    if (soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP | MACHINESTATE::TOOKDAMAGE | MACHINESTATE::SIDEATTACKING))
    {
        drift_delta = 0.0f;
    }

    if (soa->machine_state[soa_index] & MACHINESTATE::LOWGRIP)
    {
        soa->velocity_angular_x[soa_index] *= 0.975f;
        soa->velocity_angular_z[soa_index] *= 0.975f;
        //align_machine_y_with_track_normal_immediate();
    }
    else if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0)
    {
        soa->velocity_angular_z[soa_index] *= 0.99f;
    }

    if (soa->machine_state[soa_index] & MACHINESTATE::RETIRED)
    {
        drift_delta *= 0.2f;
    } else if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
        float fade =
            std::clamp(0.01f * (static_cast<float>(soa->frames_since_death[soa_index]) - 4.0f),
                       0.0f, 0.05f);
        drift_delta *= fade;
    }

    // ───────────── Force computation ─────────────
    if (drift_delta != 0.0f) {
        if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
            drift_delta *= 0.1;
        }
        float turn_tension = soa->stat_turn_tension[soa_index];
        float weighted_delta = drift_delta * soa->stat_weight[soa_index];
        float applied_force = 0.0f;

        if (turn_tension >= 0.1f || soa->grip_frames_from_accel_press[soa_index] != 0) {
            applied_force = weighted_delta * turn_tension;
        } else if ((soa->tilt_state[p] & TILTSTATE::AIRBORNE) == 0 &&
                   (soa->machine_state[soa_index] & MACHINESTATE::JUST_PRESSED_BOOST) == 0) {
            float rail_timer = static_cast<float>(soa->rail_collision_timer[soa_index]);
            float speed_lerp = std::clamp(speed_factor, 0.2f, 0.8f);
            float steer_scale = 0.0f;
            if ((soa->tilt_state[p] & TILTSTATE::STRAFING) == 0) {
                steer_scale = ((speed_lerp - 0.2f) / 0.6f) *
                              (turn_tension - 0.1f) *
                              (0.3f + 0.7f * std::abs(soa->input_steer_yaw[soa_index]));
            }
            applied_force = weighted_delta *
                            (0.1f + steer_scale * (1.0f - rail_timer / 20.0f));
        } else {
            applied_force = weighted_delta * 0.1f;
        }

        if (soa->terrain_state[soa_index] & TERRAIN::ICE) {
            applied_force *= 0.003f;
        } else if (soa->terrain_state[soa_index] & TERRAIN::DIRT) {
            applied_force *= 2.0f;
        }

        SimVec3 local_force(applied_force, 0.0f, 0.0f);
        SimVec3 world_force = mxt_basis_rotate(steer_basis, local_force);
        STORE_TILT_VEC3(force_spatial, p, LOAD_TILT_VEC3(force_spatial, p) + world_force);

        if (soa->tilt_state[p] & TILTSTATE::STRAFING) {
            applied_force *= 0.6f;
        }
        soa->turning_related[soa_index] += applied_force;
    }

    // ───────────── Apply forces & torque ─────────────

    ADD_VEC3(velocity, LOAD_TILT_VEC3(force_spatial, p));

    if (soa->rail_collision_timer[soa_index] < 6) {
        apply_torque_from_force(LOAD_TILT_VEC3(offset, p), LOAD_TILT_VEC3(force_spatial, p));
    }

    if (is_drifting && (soa->machine_state[soa_index] & MACHINESTATE::JUSTHITVEHICLE_Q) == 0) {
        in_angle_vel *= soa->stat_grip_2[soa_index];
    }

	soa->velocity_angular_y[soa_index] -= 0.125f * in_angle_vel;
};

void PhysicsCar::project_velocity_to_local_frame()
{
	STORE_VEC3(velocity_local, mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(velocity)));
	float steer = -(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index] + soa->input_strafe[soa_index] * soa->stat_strafe[soa_index]);
	steer = std::clamp(steer, -45.0f, 45.0f);
	SimTransform steer_basis = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_y(steer_basis, DEG_TO_RAD * steer);
	STORE_VEC3(velocity_local_flattened_and_rotated, mxt_basis_inverse_rotate(steer_basis, LOAD_VEC3(velocity)));
	soa->velocity_local_flattened_and_rotated_y[soa_index] = 0.0f;
}

void PhysicsCar::handle_linear_velocity()
{
	float vel_flat_rot_x = soa->velocity_local_flattened_and_rotated_x[soa_index];
	float vel_flat_rot_y = soa->velocity_local_flattened_and_rotated_y[soa_index];
	float vel_flat_rot_z = soa->velocity_local_flattened_and_rotated_z[soa_index];

	float neg_local_fwd_speed = -soa->velocity_local_z[soa_index];
	float abs_local_lat_speed = std::abs(soa->velocity_local_x[soa_index]);

	float mag_vel_flat_rot = LOAD_VEC3(velocity_local_flattened_and_rotated).length();

	float drift_accel_component = 0.0f;
	if ((soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP | MACHINESTATE::TOOKDAMAGE)) == 0 && mag_vel_flat_rot > (10.0f * soa->stat_weight[soa_index]) / 216.0f)
	{
		float norm_z_vel_flat_rot = 0.0f;
		if (mag_vel_flat_rot > 0.0001f)
			norm_z_vel_flat_rot = vel_flat_rot_z / mag_vel_flat_rot;

		float drift_factor = 1.0f - (norm_z_vel_flat_rot * norm_z_vel_flat_rot);
		drift_accel_component = drift_factor * soa->stat_drift_accel[soa_index];

		float strafe_factor = (1.0f - std::abs(soa->input_strafe[soa_index]));
		if (soa->velocity_local_x[soa_index] > 0.0f && soa->drift_sign[soa_index] == -1)
		{
			soa->drift_sign[soa_index] = 1;
			soa->drift_ramp[soa_index] = 0.0f;
		}else if (soa->velocity_local_x[soa_index] < 0.0f && soa->drift_sign[soa_index] == 1)
		{
			soa->drift_sign[soa_index] = -1;
			soa->drift_ramp[soa_index] = 0.0f;
		}
		if ((soa->velocity_local_x[soa_index] > 0.0f && soa->drift_sign[soa_index] == 1) || (soa->velocity_local_x[soa_index] < 0.0f && soa->drift_sign[soa_index] == -1))
		{
			soa->drift_ramp[soa_index] = std::min(1.0f, soa->drift_ramp[soa_index] + (0.025f - soa->drift_ramp[soa_index] * 0.025f) * (1.0f + soa->boost_turbo[soa_index] * 0.03f));
		}
		drift_accel_component = drift_accel_component * soa->drift_ramp[soa_index] * strafe_factor;
	}

	float net_fwd_accel = handle_machine_accel_and_boost(
		neg_local_fwd_speed, abs_local_lat_speed, drift_accel_component);

	if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) == 0) {
		soa->visual_rotation_x[soa_index] += 0.25f * net_fwd_accel;
	} else {
		soa->visual_rotation_x[soa_index] += 0.05f * net_fwd_accel;
	}

	float effective_steer_degrees =
	-(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index] + soa->input_strafe[soa_index] * soa->stat_strafe[soa_index]);
	if (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING)
		effective_steer_degrees = 0.0f;
	effective_steer_degrees = std::clamp(effective_steer_degrees, -45.0f, 45.0f);

	soa->turn_reaction_input[soa_index] = 0.75f * -(soa->input_steer_yaw[soa_index] * soa->stat_turn_reaction[soa_index]);
	SimVec3 local_thrust_vector(0.0f, 0.0f, -net_fwd_accel);
	SimTransform thrust_basis = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_y(thrust_basis, DEG_TO_RAD * effective_steer_degrees);
	SimVec3 world_thrust_vector = mxt_basis_rotate(thrust_basis, local_thrust_vector);
	ADD_VEC3(velocity, world_thrust_vector);

	float current_world_speed = LOAD_VEC3(velocity).length();

	if (std::abs(soa->stat_weight[soa_index]) > 0.0001f &&
		current_world_speed / soa->stat_weight[soa_index] > (1.0f / 1.08f)) {
		if (soa->side_attack_delay[soa_index] == 6) {
			float speed_cap_for_dash = (50.0f / 9.0f) * soa->stat_weight[soa_index];
			float clamped_speed_for_dash = std::min(current_world_speed, speed_cap_for_dash);

			SimVec3 local_dash_vector(soa->side_attack_indicator[soa_index] * clamped_speed_for_dash,
				0.0f, 0.0f);
			SimVec3 world_dash_vector = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_dash_vector);
			ADD_VEC3(velocity, world_dash_vector);
		}

		if ((soa->terrain_state[soa_index] & TERRAIN::JUMP) != 0 &&
			(soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
			SimVec3 local_jump_boost(0.0f, 1.13f * current_world_speed, 0.0f);
		SimVec3 world_jump_boost = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_jump_boost);

		ADD_VEC3(velocity, world_jump_boost);
		soa->state_2[soa_index] |= 2u;
		soa->velocity_angular_x[soa_index] = 0.0f;
		soa->velocity_angular_z[soa_index] = 0.0f;
	}
}

soa->input_strafe_1_6[soa_index] = soa->input_strafe_32[soa_index] / 20.0f;
soa->input_strafe_32[soa_index] += (8.0f * soa->input_strafe[soa_index] - 5.0f * soa->input_strafe_1_6[soa_index]);
};

void PhysicsCar::apply_initial_accel_activation(float effective_accel_input)
{
	if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) == 0) {
		soa->machine_state[soa_index] |= MACHINESTATE::ACTIVE;
	}

	if (soa->frames_since_start_2[soa_index] == 0) {
		soa->frames_since_start_2[soa_index] = 1;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		if (soa->race_start_charge[soa_index] > 0.0f) {
			soa->base_speed[soa_index] = 1.0f;
			soa->machine_state[soa_index] |= MACHINESTATE::RACEJUSTBEGAN_Q | MACHINESTATE::JUSTTAPPEDACCEL;
			soa->race_start_charge[soa_index] = 0.0f;
		}
	} else {
		soa->race_start_charge[soa_index] += effective_accel_input;
	}
}

float PhysicsCar::handle_machine_accel_and_boost(float neg_local_fwd_speed, float abs_local_lateral_speed, float drift_accel_factor)
{
	float effective_accel_input = 0.0f;
	float final_thrust_output = 0.0f;

	if (!((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) && soa->frames_since_death[soa_index] <= 0x77)) {
		effective_accel_input = soa->input_accel[soa_index];

		if ((soa->state_2[soa_index] & 4u) == 0) {
			if (effective_accel_input < 0.0f || soa->input_brake[soa_index] > 0.0f)
				effective_accel_input = 0.0f;
		} else if (effective_accel_input < 0.0f || soa->brake_timer[soa_index] > 0x1d) {
			effective_accel_input = 0.0f;
		}

		if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) == 0 && effective_accel_input < 0.3f)
			effective_accel_input = 0.0f;
	}

	if (effective_accel_input <= 0.0001f) {
		if (soa->race_start_charge[soa_index] <= 0.0f) {
			if (soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN)
				soa->base_speed[soa_index] = 0.0f;
		} else {
			soa->race_start_charge[soa_index] -= 2.0f;
			if (soa->race_start_charge[soa_index] < 0.0f)
				soa->race_start_charge[soa_index] = 0.0f;
			if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0)
				soa->base_speed[soa_index] = 0.0f;
		}
	} else {
		apply_initial_accel_activation(effective_accel_input);
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		const bool sboostActive = soa->s_boost_active[soa_index];
		uint32_t current_machine_state = soa->machine_state[soa_index];
		float normalized_fwd_speed = neg_local_fwd_speed / soa->stat_weight[soa_index];

		if (soa->boost_delay_frame_counter[soa_index] != 0) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
			soa->boost_delay_frame_counter[soa_index] -= 1;
		}

		if (current_machine_state & MACHINESTATE::JUST_PRESSED_BOOST) {
			if (soa->boost_delay_frame_counter[soa_index] == 0)
				soa->boost_delay_frame_counter[soa_index] = 6;
			else
				soa->boost_delay_frame_counter[soa_index] += 1;
		}

		current_machine_state = soa->machine_state[soa_index];
		if (sboostActive) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
			soa->boost_frames_manual[soa_index] = 0;
			if ((current_machine_state & MACHINESTATE::JUST_HIT_DASHPLATE) == 0) {
				if (soa->boost_frames[soa_index] > 0) {
					soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE;
				} else {
					soa->machine_state[soa_index] &= ~(MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE);
					soa->dashplate_heat_multiplier[soa_index] = 1.0f;
				}
			} else {
				float boost_strength_factor = 1.0f - soa->boost_turbo[soa_index] / (9.0f * soa->stat_boost_strength[soa_index]);
				int target_dash_boost_frames = static_cast<int>(0.5f * 60.0f * soa->stat_boost_length[soa_index]);

				if (soa->boost_frames[soa_index] < static_cast<uint32_t>(target_dash_boost_frames))
					soa->boost_frames[soa_index] = target_dash_boost_frames;

				float min_boost_strength_factor = 0.2f;
				soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE;

				boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
				float dashplate_multiplier = soa->dashplate_heat_multiplier[soa_index];
				if (dashplate_multiplier < 1.0f)
					dashplate_multiplier = 1.0f;
				soa->boost_turbo[soa_index] += dashplate_multiplier * (2.0f * soa->stat_boost_strength[soa_index]) * boost_strength_factor;
				soa->dashplate_heat_multiplier[soa_index] = 1.0f;
			}
		} else if ((current_machine_state & MACHINESTATE::JUST_HIT_DASHPLATE) == 0) {
			if (soa->boost_frames[soa_index] == 0) {
				bool do_manual_boost = (current_machine_state & MACHINESTATE::JUST_PRESSED_BOOST) &&
				soa->energy[soa_index] > 1.0f && effective_accel_input > 0.0f;
				if (!do_manual_boost) {
					soa->machine_state[soa_index] &= ~(MACHINESTATE::BOOSTING_DASHPLATE |
						MACHINESTATE::JUST_PRESSED_BOOST |
						MACHINESTATE::BOOSTING);
					// soa->boost_turbo[soa_index] -= (2.0f + 0.01f * soa->boost_turbo[soa_index]) / 60.0f * soa->stat_acceleration[soa_index];
				} else {
					float boost_strength_factor = 1.0f - soa->boost_turbo[soa_index] / (9.0f * soa->stat_boost_strength[soa_index]);
					float min_boost_strength_factor = 0.2f;
					int boost_duration_frames = static_cast<int>(60.0f * soa->stat_boost_length[soa_index]);
					soa->boost_frames[soa_index] = boost_duration_frames;
					soa->boost_frames_manual[soa_index] = boost_duration_frames;
					soa->last_manual_boost_tick[soa_index] = soa->frames_since_start[soa_index];
					soa->has_last_manual_boost_tick[soa_index] = true;
					soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING;
					soa->machine_state[soa_index] &= ~MACHINESTATE::BOOSTING_DASHPLATE;

					boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
					soa->boost_turbo[soa_index] += soa->stat_boost_strength[soa_index] * boost_strength_factor;
				}
			} else {
				soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
				soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING;
			}
		} else {
			float boost_strength_factor = 1.0f - soa->boost_turbo[soa_index] / (9.0f * soa->stat_boost_strength[soa_index]);
			int target_dash_boost_frames = static_cast<int>(0.5f * 60.0f * soa->stat_boost_length[soa_index]);

			if (soa->boost_frames[soa_index] < static_cast<uint32_t>(target_dash_boost_frames))
				soa->boost_frames[soa_index] = target_dash_boost_frames;

			float min_boost_strength_factor = 0.2f;
			soa->machine_state[soa_index] &= ~MACHINESTATE::JUST_PRESSED_BOOST;
			soa->machine_state[soa_index] |= MACHINESTATE::BOOSTING;

			boost_strength_factor = std::max(boost_strength_factor, min_boost_strength_factor);
			float dashplate_multiplier = soa->dashplate_heat_multiplier[soa_index];
			if (dashplate_multiplier < 1.0f)
				dashplate_multiplier = 1.0f;
			soa->boost_turbo[soa_index] += dashplate_multiplier * (2.0f * soa->stat_boost_strength[soa_index]) * boost_strength_factor;
			soa->dashplate_heat_multiplier[soa_index] = 1.0f;
		}

		if (soa->boost_frames[soa_index] > 0 || soa->boost_frames_manual[soa_index] > 0)
		{
			soa->boost_turbo[soa_index] -= ((3.0f + 0.03f * soa->boost_turbo[soa_index]) * soa->stat_acceleration[soa_index] * soa->stat_boost_strength[soa_index] * 0.5f) / 60.0f;
		}else
		{
			soa->boost_turbo[soa_index] -= ((6.0f + 0.05f * soa->boost_turbo[soa_index]) * soa->stat_acceleration[soa_index] * soa->stat_boost_strength[soa_index] * 0.5f) / 60.0f;
		}
		soa->boost_turbo[soa_index] = std::max(soa->boost_turbo[soa_index], 0.0f);

		if (soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) {
			if (!sboostActive && soa->boost_frames_manual[soa_index] > 0) {
				soa->energy[soa_index] -= 0.1666666667f * soa->boost_energy_use_mult[soa_index];
				soa->boost_frames_manual[soa_index] -= 1;
			}

			if (soa->boost_frames[soa_index] > 0)
				soa->boost_frames[soa_index] -= 1;

			if (!sboostActive && soa->boost_frames[soa_index] == 0 && soa->speed_kmh[soa_index] > 1200.0f) {
				float cooldown_duration = (soa->speed_kmh[soa_index] - 1200.0f) / 60.0f;
				cooldown_duration = std::min(cooldown_duration, 10.0f);
				if (static_cast<float>(soa->boost_delay_frame_counter[soa_index]) < cooldown_duration)
					soa->boost_delay_frame_counter[soa_index] = static_cast<uint8_t>(cooldown_duration);
			}

			if (!sboostActive && soa->energy[soa_index] < 0.01f) {
				soa->energy[soa_index] = 0.01f;
				soa->boost_frames_manual[soa_index] = 0;
				if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING_DASHPLATE) == 0) {
					soa->boost_frames[soa_index] = 0;
				} else {
					int half_dash_boost_frames = static_cast<int>(0.5f * 60.0f * soa->stat_boost_length[soa_index]);
					if (half_dash_boost_frames < static_cast<int>(soa->boost_frames[soa_index]))
						soa->boost_frames[soa_index] = half_dash_boost_frames;
				}
			}

			if (soa->boost_frames[soa_index] <= 0) {
				soa->boost_frames[soa_index] = 0;
				soa->machine_state[soa_index] &= ~(MACHINESTATE::BOOSTING | MACHINESTATE::BOOSTING_DASHPLATE);
			}
		}

		float accel_stat_scaled = 40.0f * soa->stat_acceleration[soa_index];
		float target_speed_component = (effective_accel_input * accel_stat_scaled) / 348.0f;
		if (soa->boost_frames[soa_index] > 0 || soa->boost_frames_manual[soa_index] > 0 || sboostActive)
		{
			target_speed_component *= 1.0f + soa->stat_boost_strength[soa_index] * soa->stat_acceleration[soa_index] * 0.038f;
		}
		target_speed_component += soa->base_speed[soa_index];
		float speed_difference = target_speed_component - normalized_fwd_speed;

		float speed_factor_denom = 36.0f + 40.0f * soa->stat_max_speed[soa_index] + soa->boost_turbo[soa_index] * 3.0f;
		float speed_factor = 0.0f;
		if (std::abs(speed_factor_denom) > 0.0001f)
			speed_factor = target_speed_component / speed_factor_denom;
		speed_factor = std::max(speed_factor, 0.0f);

		float current_accel_magnitude = speed_factor * 4.0f * (soa->stat_acceleration[soa_index] * (0.6f + soa->stat_acceleration[soa_index]));

		if ((soa->machine_state[soa_index] & (MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::JUST_PRESSED_BOOST)) == 0) {
			if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) || sboostActive) {
				current_accel_magnitude *= (soa->stat_weight[soa_index] <= 1000.0f) ? 0.3f : 0.5f;
			}
		} else {
			current_accel_magnitude = 0.0f;
		}

		if (speed_difference > 0.0f &&
			(normalized_fwd_speed < 0.0f || (soa->terrain_state[soa_index] & TERRAIN::DIRT))) {
			current_accel_magnitude *= 5.0f;
	}

	float final_accel_term = (1.0f - drift_accel_factor) *
	((speed_difference * current_accel_magnitude) +
		((abs_local_lateral_speed * soa->stat_acceleration[soa_index]) / soa->stat_weight[soa_index]) * soa->stat_turn_decel[soa_index]);

	float new_base_speed = target_speed_component - final_accel_term;
	float base_speed_diff = new_base_speed - soa->base_speed[soa_index];

	if (base_speed_diff < 0.0f)
	{
		new_base_speed = soa->base_speed[soa_index] - final_accel_term * 0.1f;
		//base_speed_diff = new_base_speed - soa->base_speed[soa_index];
	}

	soa->base_speed[soa_index] = new_base_speed;

	if (soa->input_brake[soa_index] <= 0.0001f)
		soa->brake_timer[soa_index] = 0;
	else if (soa->brake_timer[soa_index] < 0x1e)
		soa->brake_timer[soa_index] += 1;

	float brake_effect = 0.0f;
	if ((soa->state_2[soa_index] & 4u) == 0)
		brake_effect = soa->input_brake[soa_index] * (0.5f * current_accel_magnitude);
	else if (soa->brake_timer[soa_index] > 0xe)
		brake_effect = soa->input_brake[soa_index] * (0.5f * current_accel_magnitude);

	brake_effect = std::min(brake_effect, 0.12f);
	soa->base_speed[soa_index] = std::max(soa->base_speed[soa_index] - brake_effect, 0.0f);

	soa->base_speed[soa_index] = std::max(soa->base_speed[soa_index] - soa->stat_drag[soa_index], 0.0f);

	if (sboostActive)
	{
		soa->base_speed[soa_index] += 0.025f;
	}

	float final_output_thrust_factor = speed_difference;
	if (brake_effect <= 0.0f) {
		float modifier = 0.3f;
		if (soa->machine_state[soa_index] & MACHINESTATE::B14)
			modifier = 1.0f;

		if (normalized_fwd_speed < 0.0f || final_output_thrust_factor < 0.0f)
			final_output_thrust_factor *= (0.5f * modifier);
	}

	if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
		float speed_ratio_for_0hp = std::min(soa->speed_kmh[soa_index] / 100.0f, 1.0f);
		final_output_thrust_factor *= (0.2f - 0.15f * speed_ratio_for_0hp);
	}

	if ((soa->machine_state[soa_index] & (MACHINESTATE::BOOSTING_DASHPLATE | MACHINESTATE::BOOSTING)) == 0) {
		final_thrust_output = 1000.0f * final_output_thrust_factor;
	} else if (soa->stat_weight[soa_index] <= 1000.0f) {
		final_thrust_output = 1200.0f * final_output_thrust_factor;
	} else {
		final_thrust_output = 1600.0f * final_output_thrust_factor;
	}
} else {
	final_thrust_output = -neg_local_fwd_speed;
	soa->base_speed[soa_index] = 0.014f * soa->race_start_charge[soa_index];
}

if ((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) && soa->frames_since_death[soa_index] <= 0x77) {
	if (soa->brake_timer[soa_index] < 0x3d) {
		soa->brake_timer[soa_index] += 1;
	} else {
		soa->input_accel[soa_index] = 0.0f;
		soa->input_brake[soa_index] = 0.0001f;
	}
	final_thrust_output = 0.0f;
}

return final_thrust_output;
};

void PhysicsCar::handle_angle_velocity()
{
	float weight_val = 0.99f;

	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		if ((soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) == 0) {
			weight_val = 0.05f * soa->weight_derived_2[soa_index];
		} else {
			weight_val = 0.2f * soa->weight_derived_2[soa_index];
		}
	} else {
		soa->velocity_angular_x[soa_index] *= 0.9f;
		soa->velocity_angular_z[soa_index] *= weight_val;
		weight_val = soa->weight_derived_2[soa_index];
	}

	soa->velocity_angular_y[soa_index] = std::clamp(soa->velocity_angular_y[soa_index], -weight_val, weight_val);
};

void PhysicsCar::handle_airborne_controls()
{
	float min_air_tilt = -50.0f;
	float max_air_tilt = 60.0f;
	bool airborne_controls_active = false;

	if (soa->frames_since_start_2[soa_index] > 60 && (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE))
		airborne_controls_active = true;

	if (airborne_controls_active) {
		float current_tilt_increment = 4.0f * soa->input_steer_pitch[soa_index];

		if (soa->air_time[soa_index] > 60) {
			float air_time_factor = static_cast<float>(soa->air_time[soa_index] - 60) / 120.0f;
			air_time_factor = std::min(air_time_factor, 1.0f);

			current_tilt_increment =
			current_tilt_increment * (1.0f + 0.3f * air_time_factor) +
			(0.3f * air_time_factor);
		}

		soa->air_tilt[soa_index] += current_tilt_increment;
		soa->air_tilt[soa_index] = std::clamp(soa->air_tilt[soa_index], min_air_tilt, max_air_tilt);
	} else {
		soa->air_tilt[soa_index] = 0.0f;
	}
};

void PhysicsCar::orient_vehicle_from_gravity_or_road()
{
	float factor = 1.5f + soa->stat_weight[soa_index] / 4000.0f;
	if (factor >= 1.8f) {
		factor = std::min(factor, 2.0f);
	} else {
		factor = 3.6f - factor;
	}

	const bool accel_off = soa->input_accel[soa_index] <= 0.01f;
	float base_factor = 0.0f;
	if (accel_off) {
		base_factor = factor * 0.6f;
	} else if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		base_factor = factor * 1.3f;
	} else if (soa->height_above_track[soa_index] <= 0.0f) {
		base_factor = factor * 0.6f;
	} else {
		base_factor = (soa->machine_state[soa_index] & MACHINESTATE::B10) ? factor * 1.8f
		: factor * 1.3f;
	}

	float force_mag = 10.0f * -(0.009f * soa->stat_weight[soa_index]) * base_factor;

	const bool accel_off_airborne = accel_off && (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0;
	const bool accel_off_road_gravity =
		!accel_off_airborne || soa->height_above_track[soa_index] >= 20.0f - 1.7f;
	const SimVec3 gravity_normal = accel_off_road_gravity ? LOAD_VEC3(track_surface_normal) : SimVec3(0.0f, 1.0f, 0.0f);
	SimVec3 gravity_align_force = gravity_normal * force_mag;
	ADD_VEC3(velocity, gravity_align_force);

	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		SimVec3 machine_world_up = LOAD_TRANSFORM(basis_physical).basis.get_column(1);
		SimVec3 safe_track_normal =
		normalized_safe(gravity_normal, SimVec3(0, 1, 0));
		float dot = 0.0f;
		if (machine_world_up.length_squared() > 0.0001f)
			dot = machine_world_up.dot(safe_track_normal);

		if (dot < 0.7f) {
			float align_factor = 0.0f;
			if (dot >= 0.0f)
				align_factor = dot / 0.7f;
			float rot_deg = 40.0f * (1.0f - align_factor);
			float rot_rad = DEG_TO_RAD * rot_deg;
			SimVec3 axis = machine_world_up.cross(safe_track_normal);
			if (axis.length_squared() > 0.0001f) {
				SimQuat q(axis.normalized(), rot_rad);
					SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
					mxt_tmp.basis = SimBasis(q) * mxt_tmp.basis;
					STORE_TRANSFORM(basis_physical, mxt_tmp);
				}
			}
		} else {
		float tilt_rad = DEG_TO_RAD * soa->air_tilt[soa_index];
		float c = deterministic_fp::cosf(tilt_rad);
		float s = deterministic_fp::sinf(tilt_rad);
		SimVec3 local_tilted_up(0.0f, c, s);
		SimVec3 world_tilted_up = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), local_tilted_up);
		SimVec3 safe_world_up = normalized_safe(world_tilted_up, SimVec3(0, 1, 0));
		SimVec3 safe_track_normal =
		normalized_safe(gravity_normal, SimVec3(0, 1, 0));
		float dot = safe_world_up.dot(safe_track_normal);

		if (dot < 0.992f) {
			float adjusted_dot = dot + 0.008f;
			float base_rot_deg = 15.0f;
			SimVec3 axis = safe_world_up.cross(safe_track_normal);
			float axis_thresh = 0.1f * 0.1f;
			if (axis.length_squared() < axis_thresh || adjusted_dot < 0.008f) {
				SimVec3 cur_up =
				normalized_safe(LOAD_TRANSFORM(basis_physical).basis.get_column(1),
					SimVec3(0, 1, 0));
				float dot_up = cur_up.dot(safe_track_normal);

				if (dot_up <= 0.0f) {
					SimVec3 machine_x =
					normalized_safe(LOAD_TRANSFORM(basis_physical).basis.get_column(0),
						SimVec3(1, 0, 0));
					axis = LOAD_TRANSFORM(basis_physical).basis.get_column(2);
					float dot_track_vs_x = safe_track_normal.dot(machine_x);
					if (dot_track_vs_x > 0.0f)
						axis = -axis;
				}
			}

			if (axis.length_squared() > 0.0001f) {
				SimVec3 norm_axis = axis.normalized();
				float sq_dot = std::max(0.0f, adjusted_dot * adjusted_dot);
				float rot_deg = base_rot_deg * (1.0f - sq_dot);
				float rot_rad = DEG_TO_RAD * rot_deg;
				SimQuat q(norm_axis, rot_rad);
					SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
					mxt_tmp.basis = SimBasis(q) * mxt_tmp.basis;
					STORE_TRANSFORM(basis_physical, mxt_tmp);
				}
			}
		}
};

void PhysicsCar::handle_drag_and_glide_forces()
{
	float speed = LOAD_VEC3(velocity).length();
	float speed_weight_ratio = 0.0f;
	if (std::abs(soa->stat_weight[soa_index]) > 0.0001f)
		speed_weight_ratio = speed / soa->stat_weight[soa_index];

	float scaled_speed = 216.0f * speed_weight_ratio;

	if (scaled_speed < 2.0f) {
		STORE_VEC3(velocity, SimVec3());
		soa->visual_shake_mult[soa_index] = 0.0f;
		return;
	}

	SimVec3 vel_norm = normalized_safe(LOAD_VEC3(velocity), SimVec3());
	float alignment_with_normal = LOAD_VEC3(track_surface_normal).dot(vel_norm);

	SimVec3 forward_world =
	normalized_safe(mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 0, -1)),
		SimVec3(0, 0, -1));
	float forward_normal_alignment =
	LOAD_VEC3(track_surface_normal).dot(forward_world);

	SimVec3 normal_force =
	LOAD_VEC3(track_surface_normal) *
	(soa->stat_weight[soa_index] * alignment_with_normal * speed_weight_ratio);
	float base_drag_mag = speed_weight_ratio * speed_weight_ratio * 8.0f;
	SimVec3 drag_vector = LOAD_VEC3(velocity) - normal_force;

	if (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) {
		if (forward_normal_alignment < 0.0f)
			base_drag_mag *= std::max(0.0f, 1.0f + forward_normal_alignment);
	}

	float drag_len = drag_vector.length();
	if (drag_len > 0.0001f)
		drag_vector *= base_drag_mag / drag_len;
	else
		drag_vector = SimVec3();

	soa->visual_shake_mult[soa_index] = base_drag_mag;

	if (soa->stat_weight[soa_index] < 1100.0f) {
		float weight_scale = soa->stat_weight[soa_index] / 1100.0f;
		alignment_with_normal *= weight_scale * weight_scale;
	}

	bool boosting = (soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) != 0;
	bool airborne = (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0;
	float drag_coeff = 0.0f;

	if (airborne && soa->base_speed[soa_index] > 0.0f) {
		const float pitch_drag_factor =
			airborne_pitch_drag_factor(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(track_surface_normal));
		const float steer = std::sqrt(std::abs(soa->input_steer_yaw[soa_index]));
		const float airtime_factor = std::clamp(static_cast<float>(soa->air_time[soa_index]) / 60.0f, 0.0f, 1.0f);
		const float tilt_multiplier = air_steering_drag_tilt_multiplier(soa->air_tilt[soa_index]);
		const float drag =
			soa->base_speed[soa_index] *
			(pitch_drag_factor * 0.005f + steer * airtime_factor * tilt_multiplier * 0.012f);
		soa->base_speed[soa_index] = std::max(soa->base_speed[soa_index] - drag, 0.0f);
	}

	if (boosting) {
		drag_coeff = alignment_with_normal * 0.5f;
	} else if (airborne) {
		drag_coeff = alignment_with_normal * 0.6f;
	} else {
		drag_coeff = alignment_with_normal * 0.6f;
	}

	drag_vector += LOAD_VEC3(track_surface_normal) * (base_drag_mag * drag_coeff);

	if (soa->frames_since_death[soa_index] != 0) {
		float death_fade =
		std::clamp(0.01f * static_cast<float>(soa->frames_since_death[soa_index]) - 4.0f, 0.0f, 1.0f);
		drag_vector *= death_fade;
	}

	SUB_VEC3(velocity, drag_vector);
};

void PhysicsCar::rotate_machine_from_angle_velocity()
{
	SimVec3 processed_ang_vel;

	const float deadzone_threshold = 3.0f;

	float val_x = soa->velocity_angular_x[soa_index];
	if (std::abs(val_x) <= deadzone_threshold)
		processed_ang_vel.x = 0.0f;
	else
		processed_ang_vel.x = val_x - ((val_x > 0.0f) - (val_x < 0.0f)) * deadzone_threshold;

	float val_z = soa->velocity_angular_z[soa_index];
	if (std::abs(val_z) <= deadzone_threshold)
		processed_ang_vel.z = 0.0f;
	else
		processed_ang_vel.z = val_z - ((val_z > 0.0f) - (val_z < 0.0f)) * deadzone_threshold;

	processed_ang_vel.y = soa->velocity_angular_y[soa_index];

	if (std::abs(soa->weight_derived_1[soa_index]) > 0.0001f)
		processed_ang_vel.x /= soa->weight_derived_1[soa_index];
	else
		processed_ang_vel.x = 0.0f;

	if (std::abs(soa->weight_derived_2[soa_index]) > 0.0001f)
		processed_ang_vel.y /= soa->weight_derived_2[soa_index];
	else
		processed_ang_vel.y = 0.0f;

	if (std::abs(soa->weight_derived_3[soa_index]) > 0.0001f)
		processed_ang_vel.z /= soa->weight_derived_3[soa_index];
	else
		processed_ang_vel.z = 0.0f;

	float rotation_angle_rad = processed_ang_vel.length();
	if (rotation_angle_rad > 0.0001f) {
		SimVec3 rotation_axis = processed_ang_vel.normalized();
		SimQuat delta_q(rotation_axis, rotation_angle_rad);

		SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
		mxt_tmp.basis = mxt_tmp.basis * SimBasis(delta_q);
		STORE_TRANSFORM(basis_physical, mxt_tmp);
	}
};

void PhysicsCar::handle_startup_wobble()
{
	float f_val3_for_cross_prod_y = 0.0f;

	int seed_uVar4 = static_cast<int>(soa->position_current_z[soa_index]) ^
	static_cast<int>(soa->position_current_x[soa_index]) ^
	static_cast<int>(soa->position_current_y[soa_index]) ^
	static_cast<int>(soa->base_speed[soa_index]);

	int intermediate_uint_f1 =
	(seed_uVar4 ^ static_cast<int>(soa->velocity_angular_x[soa_index] * 4000000.0f)) &
	0xffff;
	float normalized_f1 = static_cast<float>(intermediate_uint_f1) / 65535.0f;
	float fVar1_wobble_x = 2.0f * normalized_f1 - 1.0f;

	int intermediate_uint_f2 =
	(seed_uVar4 ^ static_cast<int>(soa->velocity_angular_y[soa_index] * 4000000.0f)) &
	0xffff;
	float normalized_f2 = static_cast<float>(intermediate_uint_f2) / 65535.0f;
	float fVar2_wobble_y_comp = 0.5f + 1.5f * normalized_f2;

	if (fVar1_wobble_x <= 0.0f)
		fVar1_wobble_x -= 0.5f;
	else
		fVar1_wobble_x += 0.5f;

	SimVec3 local_vec_y_scaled(0.0f, 0.0162037037037f * soa->stat_weight[soa_index],
		0.0f);

	SimVec3 local_48_rotated_vec =
	mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), local_vec_y_scaled);

	SimVec3 wobble_pseudo_force_local(fVar1_wobble_x,
		f_val3_for_cross_prod_y,
		fVar2_wobble_y_comp);

	SimVec3 torque_to_add =
	local_48_rotated_vec.cross(wobble_pseudo_force_local);
	ADD_VEC3(velocity_angular, torque_to_add);
};

void PhysicsCar::initialize_machine()
{
	soa->machine_state[soa_index] = 0;

	update_machine_stats();

	soa->weight_derived_1[soa_index] = 52.0f * soa->stat_weight[soa_index] * 0.0625f;
	soa->weight_derived_2[soa_index] = 45.0f * soa->stat_weight[soa_index] * 0.0625f;
	soa->weight_derived_3[soa_index] = 52.0f * soa->stat_weight[soa_index] * 0.0625f;

	soa->boost_turbo[soa_index] = 0.0f;

	soa->s_boost_charge_max[soa_index] = 60;

	if (soa->car_properties[soa_index] != nullptr) {
		for (int i = 0; i < 4; ++i) {
			const int p = POINT_INDEX(i);
			soa->tilt_force[p] = 0.0f;
			STORE_TILT_VEC3(offset, p, soa->car_properties[soa_index]->tilt_corners[i]);
			STORE_TILT_VEC3(pos_old, p, SimVec3());
			soa->tilt_state[p] = 0;
			soa->tilt_rest_length[p] = 1.7f;
		}

		for (int i = 0; i < 4; ++i) {
			const int p = POINT_INDEX(i);
			const SimVec3 wall_offset = soa->car_properties[soa_index]->wall_corners[i];
			STORE_WALL_VEC3(offset, p, wall_offset);
		}
	}

	soa->calced_max_energy[soa_index] = soa->car_properties[soa_index]->max_energy + soa->ko_energy_bonus[soa_index];

	reset_machine(1);
};

void PhysicsCar::update_machine_stats()
{
	if (soa->car_properties[soa_index] == nullptr)
		return;

	PhysicsCarProperties def_stats =
	soa->car_properties[soa_index]->derive_machine_base_stat_values(soa->m_accel_setting[soa_index]);

	soa->stat_weight[soa_index] = def_stats.weight_kg;
	soa->stat_grip_1[soa_index] = def_stats.grip_1;
	soa->stat_grip_3[soa_index] = def_stats.grip_3;
	soa->stat_turn_movement[soa_index] = def_stats.turn_movement;
	soa->stat_strafe[soa_index] = def_stats.strafe;
	soa->stat_turn_reaction[soa_index] = def_stats.turn_reaction;
	soa->stat_grip_2[soa_index] = def_stats.grip_2;
	soa->stat_body[soa_index] = def_stats.body;
	soa->stat_turn_tension[soa_index] = def_stats.turn_tension;
	soa->stat_drift_accel[soa_index] = def_stats.drift_accel;
	soa->stat_accel_press_grip_frames[soa_index] = def_stats.unk_byte_0x48;
	soa->camera_reorienting[soa_index] = def_stats.camera_reorienting;
	soa->camera_repositioning[soa_index] = def_stats.camera_repositioning;
	soa->stat_strafe_turn[soa_index] = def_stats.strafe_turn;
	soa->stat_acceleration[soa_index] = def_stats.acceleration;
	soa->stat_max_speed[soa_index] = def_stats.max_speed;
	soa->stat_boost_strength[soa_index] = 0.57f * def_stats.boost_strength;
	soa->stat_boost_length[soa_index] = def_stats.boost_length;
	soa->stat_turn_decel[soa_index] = def_stats.turn_decel;
	soa->stat_drag[soa_index] = def_stats.drag;
	soa->boost_energy_use_mult[soa_index] = def_stats.boost_energy_use_rate;
	soa->energy_recharge_mult[soa_index] = def_stats.energy_recharge_rate;
	if ((def_stats.state_flags & 1u) == 0u) {
		soa->machine_state[soa_index] &= ~MACHINESTATE::B9;
	} else {
		soa->machine_state[soa_index] |= MACHINESTATE::B9;
	}
	if ((def_stats.state_flags & 2u) != 0u) {
		soa->machine_state[soa_index] |= MACHINESTATE::VEHICLEACTIVE_Q;
	} else {
		soa->machine_state[soa_index] &= ~MACHINESTATE::B1;
	}
};

void PhysicsCar::reset_machine(int reset_type)
{
	soa->level_start_time[soa_index] = soa->frames_since_start[soa_index] + 60 * 5;
	// Clear all LOAD_VEC3(velocity) and collision vectors
	STORE_VEC3(velocity, SimVec3());
	STORE_VEC3(knockback_velocity, SimVec3());
	STORE_VEC3(velocity_local_flattened_and_rotated, SimVec3());
	STORE_VEC3(velocity_local, SimVec3());
	STORE_VEC3(velocity_angular, SimVec3());
	STORE_VEC3(collision_push_total, SimVec3());
	STORE_VEC3(collision_push_rail, SimVec3());
	STORE_VEC3(collision_push_track, SimVec3());

	STORE_VEC3(track_surface_normal, mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 1, 0)));

	// Placeholder spawn values until StageOverseer is ported
	SimVec3 spawn_pos = SimVec3(0.f, 200.f, 0.f);
	float spawn_rot = 0.0f;

	STORE_VEC3(position_current, spawn_pos);
	STORE_VEC3(position_old, spawn_pos);
	STORE_VEC3(position_old_dupe, spawn_pos);

	STORE_VEC3(position_bottom, mxt_transform_point(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(position_current), SimVec3(0.0f, -0.1f, 0.0f)));

	soa->input_steer_yaw[soa_index] = 0.0f;
	soa->visual_shake_mult[soa_index] = 0.0f;
	soa->input_accel[soa_index] = 0.0f;
	soa->input_brake[soa_index] = 0.0f;
	soa->input_strafe[soa_index] = 0.0f;
	soa->input_steer_pitch[soa_index] = 0.0f;
	soa->height_above_track[soa_index] = 0.0f;
	soa->current_checkpoint[soa_index] = 0;
	soa->checkpoint_fraction[soa_index] = 0.0f;
	soa->lap[soa_index] = 0;
	soa->previous_lap_distance[soa_index] = 0.0f;
	soa->broken_lap_rollback_pending[soa_index] = false;
	soa->broken_lap_rollback_lap[soa_index] = 0;
	STORE_VEC3(visual_rotation, SimVec3());
	STORE_VEC3(unk_vec3_0x4e4, SimVec3());
	STORE_VEC3(unk_vec3_0x4f0, SimVec3());

	soa->energy[soa_index] = soa->calced_max_energy[soa_index];
	soa->boost_frames_manual[soa_index] = 0;
	soa->air_tilt[soa_index] = 0.0f;
	soa->boost_frames[soa_index] = 0;
	soa->input_strafe_32[soa_index] = 0.0f;
	soa->input_strafe_1_6[soa_index] = 0.0f;
	soa->frames_since_start_2[soa_index] = 0;
	soa->speed_kmh[soa_index] = 0.0f;
	soa->race_start_charge[soa_index] = 0.0f;
	soa->last_manual_boost_tick[soa_index] = 0;
	soa->last_hit_tick[soa_index] = 0;
	soa->last_hit_sfx_strength[soa_index] = 0.0f;
	soa->last_machine_hit_tick[soa_index] = 0;
	soa->last_machine_hit_sfx_strength[soa_index] = 0.0f;
	soa->has_last_manual_boost_tick[soa_index] = false;
	soa->has_last_hit_tick[soa_index] = false;
	soa->has_last_machine_hit_tick[soa_index] = false;

	soa->grip_frames_from_accel_press[soa_index] = 0;
	soa->air_time[soa_index] = 0;
	soa->spinattack_angle[soa_index] = 0.0f;
	soa->spinattack_decrement[soa_index] = 0.0f;
	soa->spinattack_direction[soa_index] = 0;
	soa->frames_since_start[soa_index] = 0;
	soa->side_attack_delay[soa_index] = 0;
	soa->attack_cooldown_frames[soa_index] = kAttackCooldownFrames;
	soa->brake_timer[soa_index] = 0;
	soa->rail_collision_timer[soa_index] = 0;
	soa->terrain_state[soa_index] = 0;
	soa->frames_since_death[soa_index] = 0;
	soa->turning_related[soa_index] = 0.0f;
	soa->machine_crashed[soa_index] = false;
	soa->boost_delay_frame_counter[soa_index] = 0;
	soa->car_hit_invincibility[soa_index] = 0;
	soa->turn_reaction_input[soa_index] = 0.0f;
	soa->boost_energy_use_mult[soa_index] = soa->car_properties[soa_index] ? soa->car_properties[soa_index]->boost_energy_use_rate : 1.0f;
	soa->energy_recharge_mult[soa_index] = soa->car_properties[soa_index] ? soa->car_properties[soa_index]->energy_recharge_rate : 1.0f;
	soa->breakdown_frame_counter[soa_index] = 0;
	soa->some_breakdown_int[soa_index] = 0;
	soa->drift_sign[soa_index] = 1;
	soa->drift_ramp[soa_index] = 0.0;

	// Orient the machine at the spawn position
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = SimBasis().rotated(SimVec3(0, 1, 0), spawn_rot + PI); STORE_TRANSFORM(basis_physical, mxt_tmp); }
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical_other); mxt_tmp.basis = SimBasis().rotated(SimVec3(0, 1, 0), spawn_rot + PI); STORE_TRANSFORM(basis_physical_other, mxt_tmp); }

	update_pitch_transform_from_machine_front_back();

	// Visual transform matches physical orientation at reset
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.origin = LOAD_VEC3(position_current); STORE_TRANSFORM(transform_visual, mxt_tmp); }

	soa->base_speed[soa_index] = 0.0f;
	soa->boost_turbo[soa_index] = 0.0f;
	uint32_t state_mask_common = MACHINESTATE::COMPLETEDRACE_2_Q |
		MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::B10 |
		MACHINESTATE::B9;
	if (reset_type == 0) {
		soa->machine_state[soa_index] &= state_mask_common;
		soa->state_2[soa_index] &= 1u;
	} else {
		soa->machine_state[soa_index] &= state_mask_common;
	}

	soa->state_2[soa_index] &= 0xfffffc4fu;

	SimTransform initial_placement_transform(LOAD_TRANSFORM(basis_physical).basis, LOAD_VEC3(position_current));

	const int point_base = soa_index * 4;
	const SimTransform reset_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 reset_position = LOAD_VEC3(position_current);
	const SimVec3x4 reset_tilt_pos = mxt_transform_points4(
		reset_transform,
		reset_position,
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	const SimVec3 reset_up = mxt_basis_rotate(reset_transform, SimVec3(0, 1, 0));
	for (int i = 0; i < 4; ++i) {
		const int p = point_base + i;
		soa->tilt_state[p] = 0;
		soa->tilt_force[p] = 0.0f;

		STORE_TILT_VEC3(force_spatial, p, SimVec3());
		STORE_TILT_VEC3(up_vector_2, p, reset_up);
		STORE_TILT_VEC3(up_vector, p, reset_up);
	}
	sim_store4(soa->tilt_pos_old_x + point_base, reset_tilt_pos.x);
	sim_store4(soa->tilt_pos_old_y + point_base, reset_tilt_pos.y);
	sim_store4(soa->tilt_pos_old_z + point_base, reset_tilt_pos.z);
	sim_store4(soa->tilt_pos_x + point_base, reset_tilt_pos.x);
	sim_store4(soa->tilt_pos_y + point_base, reset_tilt_pos.y);
	sim_store4(soa->tilt_pos_z + point_base, reset_tilt_pos.z);
};

void PhysicsCar::update_pitch_transform_from_machine_front_back()
{

	float fr_offset_z = soa->tilt_offset_z[POINT_INDEX(1)];
	float br_offset_z = soa->tilt_offset_z[POINT_INDEX(2)];

	float rotation_factor = 0.0f;
	if (std::abs(fr_offset_z) > 0.0001f)
		rotation_factor = (br_offset_z / -fr_offset_z) - 1.0f;

	float clamped_rotation = std::clamp(rotation_factor, -0.2f, 0.2f);
	float angle_rad = DEG_TO_RAD * (30.0f * clamped_rotation);

	SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical);
	mxt_rotate_basis_x(mxt_tmp, angle_rad);
	STORE_TRANSFORM(g_pitch_mtx_0x5e0, mxt_tmp);

};

void PhysicsCar::update_suspension_forces(
	int point_lane,
	const SimVec3& p0_ray_start_ws,
	const SimVec3& p0,
	const SimVec3& p1_ray_end_ws,
	const SimVec2& road_t_sample_raw,
	const SimTransform& surf,
	float stat_weight,
	float mass_fraction,
	float time_based_factor,
	bool accel_off,
	float ray_start_from_attachment_len,
	float ray_len,
	bool draw_tilt_debug)
{
	const int p = POINT_INDEX(point_lane);

	const float effective_rest_length = accel_off ? 0.75f : soa->tilt_rest_length[p];
	float dynamic_rest_offset = time_based_factor * 2.0f * effective_rest_length;
	const float grounded_dynamic_rest_offset = time_based_factor * 2.0f * soa->tilt_rest_length[p];

	float compression_metric = 0.0f;
	bool grounded_contact = false;
	bool hit_found = false;

	if ((soa->tilt_state[p] & TILTSTATE::B6) != 0 || (soa->height_above_track[soa_index] <= 0.0f && (soa->tilt_state[p] & TILTSTATE::AIRBORNE))) {
		soa->tilt_state[p] |= TILTSTATE::DISCONNECTED;
	} else {
		if (draw_tilt_debug)
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			dd3d->call("draw_arrow", debug_gd_vec3(p0_ray_start_ws), debug_gd_vec3(p1_ray_end_ws), godot::Color(1.0f, 1.0f, 1.0f), 0.125, true, _TICK_DELTA);
			dd3d->call("draw_arrow", debug_gd_vec3(surf.origin), debug_gd_vec3(surf.origin + surf.basis[1].normalized() * 2.0f), godot::Color(1.0f, 0.0f, 0.0f), 0.25, true, _TICK_DELTA);
			DEBUG::disp_text("road t x", road_t_sample_raw.x);
			DEBUG::disp_text("road t y", road_t_sample_raw.y);
		}
		if (road_t_sample_raw.x == -1000.0f)
		{
			soa->tilt_state[p] |= TILTSTATE::DISCONNECTED;
			compression_metric = 0.0f;
		}
		else {
			const SimVec3 plane_n = surf.basis[1];
			const SimVec3 plane_p = surf.origin;
			const SimVec3 ray_dir = p1_ray_end_ws - p0_ray_start_ws;
			const float denom = ray_dir.dot(plane_n);
			float t = 0.0f;
			if (std::abs(denom) > 0.000001f) {
				t = (plane_p - p0_ray_start_ws).dot(plane_n) / denom;
			}
			hit_found = t >= 0.0f && t <= 1.0f;
			SimVec3 intersect = p0_ray_start_ws + ray_dir * t;
			if (hit_found){
				STORE_TILT_VEC3(pos, p, intersect);
				STORE_TILT_VEC3(up_vector_2, p, plane_n);

				float actual_len = fabsf(ray_start_from_attachment_len - ray_len * t);
				float displacement_from_attachment_plane = -actual_len;
				compression_metric = displacement_from_attachment_plane + dynamic_rest_offset;
				grounded_contact = displacement_from_attachment_plane + grounded_dynamic_rest_offset > 0.0f;
				if (draw_tilt_debug)
				{
					godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
					const SimVec3 corner_pos = LOAD_TILT_VEC3(pos, p);
					const SimVec3 corner_up = LOAD_TILT_VEC3(up_vector_2, p);
					dd3d->call("draw_arrow", debug_gd_vec3(corner_pos), debug_gd_vec3(corner_pos + corner_up * 2.0f), godot::Color(0.0f, 1.0f, 0.0f), 0.25, true, _TICK_DELTA);
					float total_sweep_length = p0.distance_to(p1_ray_end_ws);
					float hit_fraction = 0.0f;
					if (total_sweep_length > 0.0001f) {
						hit_fraction = std::min(actual_len / total_sweep_length, 1.0f);
					}
					DEBUG::disp_text("hit_fraction", hit_fraction);
					DEBUG::disp_text("displacement_from_attachment_plane", displacement_from_attachment_plane);
				}
			}
		}

		if (hit_found) {
			soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::DISCONNECTED);
		} else {
			soa->tilt_state[p] |= TILTSTATE::DISCONNECTED;
			compression_metric = 0.0f;
		}
	}

	float calculated_force_magnitude = 0.0f;

	if (compression_metric > 0.0f) {
		soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::AIRBORNE);

		float current_compression = compression_metric;
		float damping1_force_component = 0.0f;

		if (dynamic_rest_offset < compression_metric) {
			damping1_force_component =
			0.5f * (compression_metric - soa->tilt_force[p]) * stat_weight;
			current_compression = dynamic_rest_offset;
		}

		float prev_frame_compression_metric = soa->tilt_force[p];
		soa->tilt_force[p] = current_compression;

		float stiffness_k1 = 9000.0f;
		float damping_coeff_shared = 0.009f;
		float stiffness_k2_for_damping = 10000.0f;

		STORE_TILT_VEC3(up_vector, p, LOAD_TILT_VEC3(up_vector_2, p));

		float spring_force_comp =
		damping_coeff_shared * (stiffness_k1 * current_compression) *
		mass_fraction;

		float delta_compression = prev_frame_compression_metric - current_compression;
		float damping2_force_comp =
		mass_fraction * stiffness_k2_for_damping * damping_coeff_shared *
		delta_compression;

		calculated_force_magnitude =
		damping1_force_component + spring_force_comp - damping2_force_comp;
	} else if (grounded_contact) {
		soa->tilt_state[p] &= ~static_cast<uint32_t>(TILTSTATE::AIRBORNE);
		soa->tilt_force[p] = 0.0f;
		STORE_TILT_VEC3(up_vector, p, LOAD_TILT_VEC3(up_vector_2, p));
		calculated_force_magnitude = 0.0f;
	} else {
		soa->tilt_state[p] |= TILTSTATE::AIRBORNE;
		soa->tilt_force[p] = 0.0f;
		STORE_TILT_VEC3(up_vector, p, SimVec3(0, 1, 0));
		if (soa->tilt_state[p] & TILTSTATE::DISCONNECTED)
			STORE_TILT_VEC3(up_vector_2, p, SimVec3(0, 1, 0));

		calculated_force_magnitude = 0.0f;
	}

	STORE_TILT_VEC3(force_spatial, p, LOAD_TILT_VEC3(up_vector, p) * calculated_force_magnitude);
};

SimVec3 PhysicsCar::get_avg_track_normal_from_tilt_corners(TrackQueryScratch &scratch, PhysicsCarFloorProfile* profile)
{
	uint64_t profile_step = profile ? physics_profile_now_us() : 0;
	scratch.mesh_cast_candidate_count = 0;
	const int point_base = soa_index * 4;
	const SimTransform machine_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 machine_position = LOAD_VEC3(position_current);
	const SimVec3 machine_up_ws = machine_transform.basis[1];
	const SimVec3 track_normal = LOAD_VEC3(track_surface_normal);
	const SimVec3 velocity_ws = LOAD_VEC3(velocity);
	const float stat_weight = soa->stat_weight[soa_index];
	const float inv_weight = 1.0f / std::max(stat_weight, 0.0001f);
	const float mass_fraction = stat_weight / 1200.0f;
	float time_based_factor = 0.1f + static_cast<float>(soa->frames_since_start_2[soa_index]) / 90.0f;
	if (time_based_factor > 0.5f) {
		time_based_factor = 0.5f;
	}
	const bool accel_off = soa->input_accel[soa_index] <= 0.01f;
	const float offset_add = std::max(0.0f, -((velocity_ws * inv_weight).dot(track_normal)));
	RaceTrack* track = soa->current_track[soa_index];
	const bool machine_grounded = (soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0;
	const int collision_cp = soa->current_collision_checkpoint[soa_index];
	const int current_cp = soa->current_checkpoint[soa_index];
	const int analytic_cp =
		machine_grounded &&
		track &&
		collision_cp >= 0 &&
		collision_cp < track->num_checkpoints
			? collision_cp
			: -1;
	const int mesh_query_cp =
		track &&
		current_cp >= 0 &&
		current_cp < track->num_checkpoints
			? current_cp
			: analytic_cp;
	const bool trace_mesh_floor = trace_mesh_floor_for_car(soa, soa_index);
	const bool draw_tilt_debug = DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA);
	const float machine_up_len = machine_transform.basis[1].length();
	const float ray_start_from_attachment_len = (0.01f + offset_add) * machine_up_len;
	const float suspension_ray_len = (4.01f + offset_add) * machine_up_len;

	SimVec3 p0_ray_start_ws[4];
	SimVec3 p0_ws[4];
	SimVec3 p1_ray_end_ws[4];
	SimVec2 road_t[4];
	SimVec3 spatial_t[4];
	SimTransform surf[4];
	int mesh_corner_tri[4] = {-1, -1, -1, -1};
	bool corner_collided[4] = {false, false, false, false};
	const RoadData &center_floor_sample = soa->road_sample[soa_index];
	const bool center_floor_sample_valid =
		soa->height_above_track[soa_index] > 0.0f &&
		center_floor_sample.road_t.x != -1000.0f &&
		center_floor_sample.closest_surface.basis[0].length_squared() >= 0.1f;
	mxt_store_points4(
		p0_ws,
		mxt_transform_points4(
			machine_transform,
			machine_position,
			sim_load4(soa->tilt_offset_x + point_base),
			sim_load4(soa->tilt_offset_y + point_base),
			sim_load4(soa->tilt_offset_z + point_base)));
	const SimVec3 ray_start_offset_ws = machine_up_ws * (0.01f + offset_add);
	const SimVec3 ray_end_offset_ws = machine_up_ws * -4.0f;
	for (int lane = 0; lane < 4; ++lane) {
		p0_ray_start_ws[lane] = p0_ws[lane] + ray_start_offset_ws;
		p1_ray_end_ws[lane] = p0_ws[lane] + ray_end_offset_ws;
	}
	auto plane_ray_t = [](const SimVec3 &ray_start, const SimVec3 &ray_end, const SimTransform &surface) {
		const SimVec3 ray_dir = ray_end - ray_start;
		const SimVec3 plane_n = surface.basis[1];
		const float denom = ray_dir.dot(plane_n);
		if (std::abs(denom) <= 0.000001f) {
			return FLT_MAX;
		}
		const float t = (surface.origin - ray_start).dot(plane_n) / denom;
		return (t >= 0.0f && t <= 1.0f) ? t : FLT_MAX;
	};
	auto plane_ray_t_from_point_normal = [](const SimVec3 &ray_start, const SimVec3 &ray_end, const SimVec3 &point, const SimVec3 &normal) {
		const SimVec3 ray_dir = ray_end - ray_start;
		const float denom = ray_dir.dot(normal);
		if (std::abs(denom) <= 0.000001f) {
			return FLT_MAX;
		}
		const float t = (point - ray_start).dot(normal) / denom;
		return (t >= 0.0f && t <= 1.0f) ? t : FLT_MAX;
	};
	auto analytic_corner_in_domain = [](const SimVec2 &sample) {
		return sample.x >= -1.0f && sample.x <= 1.0f && sample.y >= -0.001f && sample.y <= 1.001f;
	};
	const bool use_analytic_corner_sample =
		track &&
		analytic_cp >= 0 &&
		track->segments[track->checkpoints[analytic_cp].road_segment].analytic_collision_enabled;
	if (use_analytic_corner_sample) {
		track->get_road_surface4_same_checkpoint(analytic_cp, p0_ws, road_t, spatial_t, surf, false);
		physics_profile_mark(profile ? profile->corner_analytic_surface_us : nullptr, profile_step);
		bool use_mesh_suspension_cast_candidates = false;
		bool use_mesh_suspension_hits = false;
		SimAABB mesh_suspension_cast_bounds;
		CollisionData mesh_suspension_hits[4];
		if (mesh_query_cp >= 0 && track->num_mesh_floor_bvh_nodes > 0) {
			mesh_suspension_cast_bounds.position = p0_ray_start_ws[0];
			mesh_suspension_cast_bounds.size = SimVec3();
			mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[0]);
			for (int lane = 1; lane < 4; ++lane) {
				mesh_suspension_cast_bounds.expand_to(p0_ray_start_ws[lane]);
				mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[lane]);
			}
			if (profile) {
				profile_step = physics_profile_now_us();
			}
			use_mesh_suspension_cast_candidates = track->collect_mesh_cast_candidates(
				mesh_suspension_cast_bounds,
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
				scratch);
			physics_profile_mark(profile ? profile->mesh_candidate_collect_us : nullptr, profile_step);
			use_mesh_suspension_hits =
				use_mesh_suspension_cast_candidates && scratch.mesh_cast_candidate_count > 0;
			if (use_mesh_suspension_hits) {
				if (profile) {
					profile_step = physics_profile_now_us();
				}
				track->cast_vs_mesh_candidates4_same_ray_fast(
					mesh_suspension_hits,
					p0_ray_start_ws,
					p1_ray_end_ws,
					CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
					mesh_query_cp,
					&scratch,
					true,
					false,
					false);
				physics_profile_mark(profile ? profile->mesh_cast4_us : nullptr, profile_step);
			}
		}
		for (int lane = 0; lane < 4; ++lane) {
			CollisionData hit{};
			hit.road_data.cp_idx = -1;
			hit.mesh_triangle_index = -1;
			if (use_mesh_suspension_hits) {
				hit = mesh_suspension_hits[lane];
			}
			if (!hit.collided) {
				continue;
			}
			const float analytic_t = analytic_corner_in_domain(road_t[lane])
				? plane_ray_t(p0_ray_start_ws[lane], p1_ray_end_ws[lane], surf[lane])
				: FLT_MAX;
			const float mesh_t = plane_ray_t_from_point_normal(
				p0_ray_start_ws[lane],
				p1_ray_end_ws[lane],
				hit.collision_point,
				hit.collision_normal);
			if (mesh_t <= analytic_t) {
				if (hit.collision_normal.dot(machine_up_ws) < 0.0f) {
					hit.collision_normal *= -1.0f;
				}
				if (trace_mesh_floor) {
					corner_collided[lane] = true;
					mesh_corner_tri[lane] = hit.mesh_triangle_index;
				}
				road_t[lane] = hit.road_data.road_t;
				spatial_t[lane] = hit.road_data.spatial_t;
				surf[lane] = mesh_hit_plane_transform(hit.collision_point, hit.collision_normal);
			}
		}
	} else {
		bool use_mesh_suspension_cast_candidates = false;
		bool use_mesh_suspension_hits = false;
		SimAABB mesh_suspension_cast_bounds;
		CollisionData mesh_suspension_hits[4];
		const bool has_mesh_floor_bvh =
			track &&
			mesh_query_cp >= 0 &&
			track->num_mesh_floor_bvh_nodes > 0;
		if (has_mesh_floor_bvh) {
			mesh_suspension_cast_bounds.position = p0_ray_start_ws[0];
			mesh_suspension_cast_bounds.size = SimVec3();
			mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[0]);
			for (int lane = 1; lane < 4; ++lane) {
				mesh_suspension_cast_bounds.expand_to(p0_ray_start_ws[lane]);
				mesh_suspension_cast_bounds.expand_to(p1_ray_end_ws[lane]);
			}
			if (profile) {
				profile_step = physics_profile_now_us();
			}
			use_mesh_suspension_cast_candidates = track->collect_mesh_cast_candidates(
				mesh_suspension_cast_bounds,
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
				scratch);
			physics_profile_mark(profile ? profile->mesh_candidate_collect_us : nullptr, profile_step);
			use_mesh_suspension_hits =
				use_mesh_suspension_cast_candidates && scratch.mesh_cast_candidate_count > 0;
			if (use_mesh_suspension_hits) {
				if (profile) {
					profile_step = physics_profile_now_us();
				}
				track->cast_vs_mesh_candidates4_same_ray_fast(
					mesh_suspension_hits,
					p0_ray_start_ws,
					p1_ray_end_ws,
					CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
					mesh_query_cp,
					&scratch,
					true,
					false);
				physics_profile_mark(profile ? profile->mesh_cast4_us : nullptr, profile_step);
			}
		}
		for (int lane = 0; lane < 4; ++lane) {
			CollisionData hit{};
			hit.road_data.cp_idx = -1;
			hit.mesh_triangle_index = -1;
			if (use_mesh_suspension_hits) {
				hit = mesh_suspension_hits[lane];
			}
			if (has_mesh_floor_bvh) {
				if (!use_mesh_suspension_cast_candidates) {
					track->cast_vs_mesh_fast(
						hit,
						p0_ray_start_ws[lane],
						p1_ray_end_ws[lane],
						CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
						mesh_query_cp,
						&scratch,
						true,
						nullptr,
						false);
				}
			}
			if (!hit.collided && has_mesh_floor_bvh) {
				if (profile) {
					profile_step = physics_profile_now_us();
				}
				track->sample_mesh_floor_fast(
					hit,
					p0_ws[lane],
					8.0f,
					CAST_FLAGS::WANTS_TRACK,
					mesh_query_cp,
					false,
					&scratch,
					-1,
					true,
					false);
				physics_profile_mark(profile ? profile->mesh_floor_sample_us : nullptr, profile_step);
			}
			if (hit.collided) {
				if (hit.collision_normal.dot(machine_up_ws) < 0.0f) {
					hit.collision_normal *= -1.0f;
					hit.road_data.closest_surface.basis[1] *= -1.0f;
					hit.road_data.closest_surface.basis[2] *= -1.0f;
				}
				if (trace_mesh_floor) {
					corner_collided[lane] = true;
					mesh_corner_tri[lane] = hit.mesh_triangle_index;
				}
				road_t[lane] = hit.road_data.road_t;
				spatial_t[lane] = hit.road_data.spatial_t;
				surf[lane] = hit.road_data.closest_surface;
			} else {
				road_t[lane].x = -1000.0f;
				spatial_t[lane] = SimVec3();
				surf[lane] = SimTransform();
			}
		}
	}

	SimVec3 normal_sum(0, 0, 0);
	int valid_count = 0;
	for (int i = 0; i < 4; ++i) {
		if (road_t[i].x == -1000.0f && center_floor_sample_valid) {
			road_t[i] = center_floor_sample.road_t;
			spatial_t[i] = center_floor_sample.spatial_t;
			surf[i] = center_floor_sample.closest_surface;
			if (trace_mesh_floor) {
				mesh_corner_tri[i] = -2;
			}
		}
		const int p = point_base + i;
		update_suspension_forces(i, p0_ray_start_ws[i], p0_ws[i], p1_ray_end_ws[i], road_t[i], surf[i],
			stat_weight, mass_fraction, time_based_factor, accel_off, ray_start_from_attachment_len, suspension_ray_len,
			draw_tilt_debug);
		if ((soa->tilt_state[p] & TILTSTATE::AIRBORNE) == 0) {
			normal_sum += LOAD_TILT_VEC3(up_vector, p);
			++valid_count;
		}
	}

	if (valid_count > 0) {
		const SimVec3 avg_normal = normal_sum.normalized();
		if (trace_mesh_floor) {
			godot::UtilityFunctions::print(
				godot::String("MXT_MESH_FLOOR_CORNERS tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" analytic_cp="), static_cast<int64_t>(analytic_cp),
				godot::String(" mesh_cp="), static_cast<int64_t>(mesh_query_cp),
				godot::String(" analytic_corner="), use_analytic_corner_sample,
				godot::String(" valid="), static_cast<int64_t>(valid_count),
				godot::String(" avg_n=("), avg_normal.x, godot::String(","), avg_normal.y, godot::String(","), avg_normal.z, godot::String(")"),
				godot::String(" c0=("), corner_collided[0], godot::String(","), static_cast<int64_t>(mesh_corner_tri[0]), godot::String(","), road_t[0].x, godot::String(","), road_t[0].y, godot::String(")"),
				godot::String(" c1=("), corner_collided[1], godot::String(","), static_cast<int64_t>(mesh_corner_tri[1]), godot::String(","), road_t[1].x, godot::String(","), road_t[1].y, godot::String(")"),
				godot::String(" c2=("), corner_collided[2], godot::String(","), static_cast<int64_t>(mesh_corner_tri[2]), godot::String(","), road_t[2].x, godot::String(","), road_t[2].y, godot::String(")"),
				godot::String(" c3=("), corner_collided[3], godot::String(","), static_cast<int64_t>(mesh_corner_tri[3]), godot::String(","), road_t[3].x, godot::String(","), road_t[3].y, godot::String(")"));
		}
		return avg_normal;
	}

	if (trace_mesh_floor) {
		godot::UtilityFunctions::print(
			godot::String("MXT_MESH_FLOOR_CORNERS tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" analytic_cp="), static_cast<int64_t>(analytic_cp),
			godot::String(" mesh_cp="), static_cast<int64_t>(mesh_query_cp),
			godot::String(" analytic_corner="), use_analytic_corner_sample,
			godot::String(" valid=0"),
			godot::String(" c0=("), corner_collided[0], godot::String(","), static_cast<int64_t>(mesh_corner_tri[0]), godot::String(","), road_t[0].x, godot::String(","), road_t[0].y, godot::String(")"),
			godot::String(" c1=("), corner_collided[1], godot::String(","), static_cast<int64_t>(mesh_corner_tri[1]), godot::String(","), road_t[1].x, godot::String(","), road_t[1].y, godot::String(")"),
			godot::String(" c2=("), corner_collided[2], godot::String(","), static_cast<int64_t>(mesh_corner_tri[2]), godot::String(","), road_t[2].x, godot::String(","), road_t[2].y, godot::String(")"),
			godot::String(" c3=("), corner_collided[3], godot::String(","), static_cast<int64_t>(mesh_corner_tri[3]), godot::String(","), road_t[3].x, godot::String(","), road_t[3].y, godot::String(")"));
	}
	return SimVec3();
};

void PhysicsCar::set_terrain_state_from_track(TrackQueryScratch &scratch, const SimVec3 &trigger_p0, const SimVec3 &trigger_p1)
{
	uint32_t terrain_bits = soa->road_sample[soa_index].terrain;
	RaceTrack* track = soa->current_track[soa_index];
	if (soa->height_above_track[soa_index] > 0.0f && track != nullptr) {
		if (track->num_mesh_overlay_triangles > 0) {
			const uint32_t overlay_body = track->sample_mesh_terrain_overlay_fast(LOAD_VEC3(position_current), 3.0f);
			const uint32_t overlay_surface = track->sample_mesh_terrain_overlay_fast(LOAD_VEC3(track_surface_pos), 3.0f);
			terrain_bits |= overlay_body | overlay_surface;
		}
		const TrackSegment &segment = track->segments[track->checkpoints[soa->current_checkpoint[soa_index]].road_segment];
		if (segment.analytic_collision_enabled && segment.road_shape->embed_terrain_mask != 0) {
			const SimVec2& road_t = soa->road_sample[soa_index].road_t;
			if (road_t.x != -1000.0f) {
				terrain_bits |= track->sample_analytic_road_embed_terrain(
					soa->current_checkpoint[soa_index], road_t);
			}
		}
	}

	if (track != nullptr) {
		const int current_cp = soa->current_checkpoint[soa_index];
		const bool use_checkpoint_trigger_index =
			current_cp >= 0 &&
			current_cp < track->num_checkpoints &&
			static_cast<int>(track->trigger_checkpoint_offsets.size()) == track->num_checkpoints + 1;
		const int trigger_begin = use_checkpoint_trigger_index ? track->trigger_checkpoint_offsets[current_cp] : 0;
		const int trigger_end = use_checkpoint_trigger_index ? track->trigger_checkpoint_offsets[current_cp + 1] : track->num_trigger_colliders;
		for (int trigger_iter = trigger_begin; trigger_iter < trigger_end; ++trigger_iter)
		{
			const int i = use_checkpoint_trigger_index ? track->trigger_checkpoint_indices[trigger_iter] : trigger_iter;
			TriggerCollider* trigger = track->trigger_colliders[i];
			uint8_t collision = trigger->intersect_segment(current_cp, track, trigger_p0, trigger_p1, use_checkpoint_trigger_index);
			if ((collision & 0x1) != 0)
			{
				switch (trigger->type)
				{
				case TRIGGER_TYPE::DASHPLATE:
					soa->machine_state[soa_index] |= MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::BOOSTING_DASHPLATE;
					soa->terrain_state[soa_index] |= TERRAIN::DASH;
					scratch.push_trigger_event(soa_index, i, collision, static_cast<uint8_t>(trigger->type));
					break;
				case TRIGGER_TYPE::JUMPPLATE:
					soa->terrain_state[soa_index] |= TERRAIN::JUMP;
					soa->attack_cooldown_frames[soa_index] = 0;
					break;
				case TRIGGER_TYPE::MINE:
					collide_with_landmine(static_cast<Mine*>(trigger), trigger_p0, trigger_p1);
					scratch.push_trigger_event(soa_index, i, collision, static_cast<uint8_t>(trigger->type));
					break;
				default:
					break;
				}
			}
		}
	}

	if (terrain_bits & TERRAIN::DASH) {
		soa->machine_state[soa_index] |= MACHINESTATE::JUST_HIT_DASHPLATE | MACHINESTATE::BOOSTING_DASHPLATE;
		soa->terrain_state[soa_index] |= TERRAIN::DASH;
	}

	if ((terrain_bits & TERRAIN::RECHARGE) && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0) {
		soa->state_2[soa_index] |= 1;
		soa->terrain_state[soa_index] |= TERRAIN::RECHARGE;
		soa->energy[soa_index] += 1.111111f * soa->energy_recharge_mult[soa_index];
		if (soa->energy[soa_index] > soa->calced_max_energy[soa_index]) {
			soa->energy[soa_index] = soa->calced_max_energy[soa_index];
		}
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::BOOSTING) == 0 && (terrain_bits & TERRAIN::DIRT)) {
		soa->terrain_state[soa_index] |= TERRAIN::DIRT;
	}

	if (terrain_bits & TERRAIN::ICE) {
		soa->terrain_state[soa_index] |= TERRAIN::ICE;
	}

	if (terrain_bits & TERRAIN::JUMP) {
		soa->terrain_state[soa_index] |= TERRAIN::JUMP;
		soa->attack_cooldown_frames[soa_index] = 0;
	}

	if (terrain_bits & TERRAIN::LAVA) {
		soa->terrain_state[soa_index] |= TERRAIN::LAVA;
	}

	if (terrain_bits & TERRAIN::HOLE) {
		soa->terrain_state[soa_index] |= TERRAIN::HOLE;
	}

	if (terrain_bits & TERRAIN::FALL) {
		trigger_mesh_fallout();
	}
};

void PhysicsCar::handle_attack_states()
{
	if (soa->attack_cooldown_frames[soa_index] > 0 &&
			(soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		soa->attack_cooldown_frames[soa_index] -= 1;
	}

	if (soa->s_boost_active[soa_index]) {
		soa->machine_state[soa_index] &= ~(MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING);
		soa->side_attack_delay[soa_index] = 0;
		soa->spinattack_angle[soa_index] = 0.0f;
		soa->spinattack_decrement[soa_index] = 0.0f;
		return;
	}
	if (soa->speed_kmh[soa_index] < 300.0f) {
		if (soa->spinattack_angle[soa_index] == 0.0f)
			soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;
		soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
	}

	if (soa->side_attack_delay[soa_index] != 0)
		soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;

	if ((soa->machine_state[soa_index] & MACHINESTATE::SPINATTACKING) == 0) {
		soa->spinattack_angle[soa_index] = 0.0f;
	} else {
		float cur_angle = soa->spinattack_angle[soa_index];
		if (cur_angle == 0.0f) {
			if (soa->attack_cooldown_frames[soa_index] != 0) {
				soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;
			} else {
				soa->attack_cooldown_frames[soa_index] = kAttackCooldownFrames;
				soa->spinattack_angle[soa_index] = Math_PI * 8.0f;
				soa->spinattack_decrement[soa_index] = Math_PI * 0.125f * kSpinAttackShortenMultiplier;
				if (std::abs(soa->input_steer_yaw[soa_index]) > 0.1f) {
					soa->spinattack_direction[soa_index] = (soa->input_steer_yaw[soa_index] < 0.0f) ? 1 : 0;
				}
			}
		} else if (soa->spinattack_decrement[soa_index] < cur_angle) {
			soa->spinattack_angle[soa_index] = cur_angle - soa->spinattack_decrement[soa_index];
			if (soa->spinattack_angle[soa_index] < Math_PI * 4.0f) {
				soa->spinattack_decrement[soa_index] -= Math_PI * 130.0f / 65536.0f * kSpinAttackShortenMultiplier;
				if (soa->spinattack_decrement[soa_index] < Math_PI * 160.0f / 65536.0f * kSpinAttackShortenMultiplier)
					soa->spinattack_decrement[soa_index] = Math_PI * 160.0f / 65536.0f * kSpinAttackShortenMultiplier;
			}
		} else {
			soa->spinattack_angle[soa_index] = 0.0f;
			soa->spinattack_decrement[soa_index] = 0.0f;
			soa->machine_state[soa_index] &= ~MACHINESTATE::SPINATTACKING;
		}
		soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) == 0) {
		soa->side_attack_delay[soa_index] = 0;
	} else {
		uint8_t cur_delay = soa->side_attack_delay[soa_index];
		if (cur_delay == 0) {
			if (soa->attack_cooldown_frames[soa_index] != 0) {
				soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
			} else {
				soa->attack_cooldown_frames[soa_index] = kAttackCooldownFrames;
				soa->side_attack_delay[soa_index] = 6;
				soa->side_attack_indicator[soa_index] = 0.4f * soa->input_steer_yaw[soa_index];
			}
		} else if (cur_delay == 1) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
		} else {
			soa->side_attack_delay[soa_index] = cur_delay - 1;
		}

		if ((soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::TOOKDAMAGE)) != 0 ||
			soa->input_accel[soa_index] < 0.5f) {
			soa->machine_state[soa_index] &= ~MACHINESTATE::SIDEATTACKING;
		soa->side_attack_delay[soa_index] = 1;
	}
}

};

void PhysicsCar::apply_torque_from_force(const SimVec3& p_local_offset, const SimVec3& wf_world_force)
{
	SimVec3 lf = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), wf_world_force);
	soa->velocity_angular_x[soa_index] += -(p_local_offset.z * lf.y - p_local_offset.y * lf.z);
	soa->velocity_angular_y[soa_index] += -(p_local_offset.x * lf.z - p_local_offset.z * lf.x);
	soa->velocity_angular_z[soa_index] += -(p_local_offset.y * lf.x - p_local_offset.x * lf.y);
};

struct OldCornerCollisionSurface {
	int cp = -1;
	bool valid = false;
	bool from_center_floor = false;
	bool was_above = false;
	bool was_inside = false;
	SimVec2 road_t;
	SimTransform surface;
};

static inline bool analytic_depenetration_t_in_surface_domain(const SimVec2 &road_t)
{
	return road_t.x > -1.0f &&
		road_t.x < 1.0f &&
		track_segment_longitudinal_t_in_domain(road_t.y);
}

static OldCornerCollisionSurface sample_old_corner_collision_surface(
	PhysicsCarSoA *soa,
	int soa_index,
	TrackQueryScratch &scratch)
{
	OldCornerCollisionSurface out;
	if (!soa->current_track[soa_index]) {
		return out;
	}

	const int current_cp = soa->current_checkpoint[soa_index];
	const RoadData &floor_sample = soa->road_sample[soa_index];
	if (current_cp >= 0 &&
		current_cp < soa->current_track[soa_index]->num_checkpoints &&
		soa->current_collision_checkpoint[soa_index] == current_cp &&
		floor_sample.cp_idx == current_cp) {
		const TrackSegment &segment = soa->current_track[soa_index]->segments[
			soa->current_track[soa_index]->checkpoints[current_cp].road_segment];
		if (!segment.analytic_collision_enabled) {
			return out;
		}
	}
	if (current_cp >= 0 &&
		current_cp < soa->current_track[soa_index]->num_checkpoints &&
		soa->current_collision_checkpoint[soa_index] == current_cp &&
		soa->height_above_track[soa_index] > 0.0f &&
		floor_sample.road_t.x != -1000.0f &&
		floor_sample.closest_surface.basis[0].length_squared() >= 0.1f) {
		const TrackSegment &segment = soa->current_track[soa_index]->segments[
			soa->current_track[soa_index]->checkpoints[current_cp].road_segment];
		if (segment.analytic_collision_enabled &&
			(floor_sample.terrain & TERRAIN::HOLE) == 0u) {
			out.cp = current_cp;
			out.valid = true;
			out.from_center_floor = true;
			out.road_t = floor_sample.road_t;
			out.surface = floor_sample.closest_surface;
			out.was_above = (LOAD_VEC3(position_old) - floor_sample.closest_surface.origin).dot(floor_sample.closest_surface.basis[1]) >= -5.0f;
			out.was_inside = floor_sample.road_t.x > -1.0f && floor_sample.road_t.x < 1.0f;
			return out;
		}
	}

	int use_cp_old = soa->current_track[soa_index]->get_best_checkpoint(LOAD_VEC3(position_old), soa->current_collision_checkpoint[soa_index], scratch);
	out.cp = use_cp_old;
	if (use_cp_old == -1) {
		return out;
	}
	if (!soa->current_track[soa_index]->segments[soa->current_track[soa_index]->checkpoints[use_cp_old].road_segment].analytic_collision_enabled) {
		return out;
	}

	SimVec2 use_t;
	SimVec3 unused_spatial_t;
	SimTransform use_transform;
	soa->current_track[soa_index]->get_road_surface(use_cp_old, LOAD_VEC3(position_old), use_t, unused_spatial_t, use_transform, false);
	if (soa->current_track[soa_index]->analytic_road_sample_has_hole(use_cp_old, use_t)) {
		return out;
	}
	out.valid = true;
	out.road_t = use_t;
	out.surface = use_transform;
	out.was_above = (LOAD_VEC3(position_old) - use_transform.origin).dot(use_transform.basis[1]) >= -5.0f;
	out.was_inside = use_t.x > -1.0f && use_t.x < 1.0f;
	return out;
}

int PhysicsCar::update_machine_corners(TrackQueryScratch &scratch, PhysicsCarCornerProfile* profile,
	float* out_max_rail_contact_push) {
	uint64_t profile_step = profile ? physics_profile_now_us() : 0;
	STORE_VEC3(collision_push_track, SimVec3());
	STORE_VEC3(collision_push_rail, SimVec3());
	STORE_VEC3(collision_push_total, SimVec3());
	if (out_max_rail_contact_push) {
		*out_max_rail_contact_push = 0.0f;
	}
	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

	int overall_hit_detected_flag = 0;
	float max_rail_contact_push = 0.0f;

	SimVec3 depenetration = SimVec3();
	const int point_base = soa_index * 4;
	const SimTransform machine_transform = LOAD_TRANSFORM(basis_physical);
	const SimTransform old_machine_transform = LOAD_TRANSFORM(basis_physical_other);
	const SimVec3 machine_position = LOAD_VEC3(position_current);
	const SimVec3 old_machine_position = LOAD_VEC3(position_old);
	const RoadData &center_floor_sample = soa->road_sample[soa_index];
	const bool center_floor_sample_is_hole = (center_floor_sample.terrain & TERRAIN::HOLE) != 0u;
	const bool center_floor_sample_valid =
		soa->height_above_track[soa_index] > 0.0f &&
		center_floor_sample.closest_surface.basis[0].length_squared() >= 0.1f;
	RaceTrack* track = soa->current_track[soa_index];
	const bool mesh_floor_depenetration_enabled =
		!center_floor_sample_is_hole;
	SimVec3 wall_corner_world[4];
	mxt_store_points4(
		wall_corner_world,
		mxt_transform_points4(
			machine_transform,
			machine_position,
			sim_load4(soa->wall_offset_x + point_base),
			sim_load4(soa->wall_offset_y + point_base),
			sim_load4(soa->wall_offset_z + point_base)));
	SimVec3 wall_corner_old_world[4];
	bool wall_corner_old_world_valid = false;
	auto ensure_wall_corner_old_world = [&]() {
		if (wall_corner_old_world_valid) {
			return;
		}
		mxt_store_points4(
			wall_corner_old_world,
			mxt_transform_points4(
				old_machine_transform,
				old_machine_position,
				sim_load4(soa->wall_offset_x + point_base),
				sim_load4(soa->wall_offset_y + point_base),
				sim_load4(soa->wall_offset_z + point_base)));
		wall_corner_old_world_valid = true;
	};
	const bool draw_rail_candidates =
		DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_RAIL_CANDIDATES) &&
		(soa->global_start + soa_index) == 0;
	const bool trace_rail = trace_rail_for_car(soa, soa_index);
	auto trace_analytic_rail_context = [&](const char *pass_name, int cp_idx, const SimVec2 &sample_t,
		bool was_inside_check, bool was_above_check, const bool side_possible[2]) {
		if (!trace_rail) {
			return;
		}
		godot::UtilityFunctions::print(
			godot::String("MXT_RAIL_CONTEXT tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" pass="), godot::String(pass_name),
			godot::String(" cp="), static_cast<int64_t>(cp_idx),
			godot::String(" cur_cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
			godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
			godot::String(" t=("), sample_t.x, godot::String(","), sample_t.y, godot::String(")"),
			godot::String(" was_inside="), was_inside_check,
			godot::String(" was_above="), was_above_check,
			godot::String(" side_possible=("), side_possible[0], godot::String(","), side_possible[1], godot::String(")"),
			godot::String(" pos=("), machine_position.x, godot::String(","), machine_position.y, godot::String(","), machine_position.z, godot::String(")"),
			godot::String(" old_pos=("), old_machine_position.x, godot::String(","), old_machine_position.y, godot::String(","), old_machine_position.z, godot::String(")"));
	};
	auto analytic_rail_corner_hit_valid = [&](const char *pass_name, int cp_idx, int wc_idx, int side_index,
		const TrackEdgeRailSide &side, const SimVec3 &new_corner, float rail_height, float *out_new_depth) {
		const float new_depth = (new_corner - side.pos).dot(side.rail_n);
		*out_new_depth = new_depth;
		const SimVec3 old_center_probe = old_machine_position + depenetration;
		const float old_depth = (old_center_probe - side.pos).dot(side.rail_n);
		const bool old_center_to_new_corner_crosses = old_depth >= 0.0f && new_depth < 0.0f;
		const SimVec3 final_hit = project_to_plane(side.rail_n, side.rail_n.dot(side.pos), new_corner);
		const float final_t = checkpoint_longitudinal_t(*track, cp_idx, final_hit);
		const float final_height = (final_hit - side.pos).dot(side.up_n);
		const float denom = old_depth - new_depth;
		float alpha = -1.0f;
		SimVec3 sweep_hit;
		float sweep_t = -1000.0f;
		float sweep_height = -1000.0f;
		const char *reason = "sweep_hit";
		bool valid = false;
		if (new_depth >= 0.0f) {
			reason = "new_corner_not_behind";
		} else if (old_depth < 0.0f) {
			reason = "old_center_already_behind";
		} else if (track_segment_longitudinal_t_in_domain(final_t) && final_height >= 0.0f && final_height <= rail_height) {
			reason = "final_project_hit";
			valid = true;
		} else if (denom <= 0.000001f) {
			reason = "sweep_degenerate";
		} else {
			alpha = old_depth / denom;
			if (alpha < 0.0f || alpha > 1.0f) {
				reason = "alpha_outside";
			} else {
				sweep_hit = old_center_probe + (new_corner - old_center_probe) * alpha;
				sweep_t = checkpoint_longitudinal_t(*track, cp_idx, sweep_hit);
				if (!track_segment_longitudinal_t_in_domain(sweep_t)) {
					reason = "sweep_t_outside";
				} else {
					sweep_height = (sweep_hit - side.pos).dot(side.up_n);
					if (sweep_height < 0.0f || sweep_height > rail_height) {
						reason = "sweep_height_outside";
					} else {
						valid = true;
					}
				}
			}
		}

		if (trace_rail) {
			godot::UtilityFunctions::print(
				godot::String("MXT_RAIL_ANALYTIC tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
				godot::String(" pass="), godot::String(pass_name),
				godot::String(" cp="), static_cast<int64_t>(cp_idx),
				godot::String(" wc="), static_cast<int64_t>(wc_idx),
				godot::String(" side="), static_cast<int64_t>(side_index),
				godot::String(" valid="), valid,
				godot::String(" reason="), godot::String(reason),
				godot::String(" old_center_depth="), old_depth,
				godot::String(" new_corner_depth="), new_depth,
				godot::String(" center_to_new_corner_cross="), old_center_to_new_corner_crosses,
				godot::String(" final_t="), final_t,
				godot::String(" final_height="), final_height,
				godot::String(" sweep_alpha="), alpha,
				godot::String(" sweep_t="), sweep_t,
				godot::String(" sweep_height="), sweep_height,
				godot::String(" rail_height="), rail_height,
				godot::String(" old_start=old_center"),
				godot::String(" new_corner=("), new_corner.x, godot::String(","), new_corner.y, godot::String(","), new_corner.z, godot::String(")"),
				godot::String(" old_center=("), old_center_probe.x, godot::String(","), old_center_probe.y, godot::String(","), old_center_probe.z, godot::String(")"),
				godot::String(" rail_pos=("), side.pos.x, godot::String(","), side.pos.y, godot::String(","), side.pos.z, godot::String(")"),
				godot::String(" rail_n=("), side.rail_n.x, godot::String(","), side.rail_n.y, godot::String(","), side.rail_n.z, godot::String(")"));
		}
		return valid;
	};

	const OldCornerCollisionSurface old_collision =
		sample_old_corner_collision_surface(soa, soa_index, scratch);
	const int use_cp_old = old_collision.cp;
	const bool old_valid = old_collision.valid;
	{
		SimVec2 use_t;
		SimTransform use_transform;
		bool was_above = false;
		bool was_inside = false;
		if (track) {
			if (old_valid)
			{
				use_t = old_collision.road_t;
				use_transform = old_collision.surface;
				was_above = old_collision.was_above;
				was_inside = old_collision.was_inside;
				//DEBUG::disp_text("soa->current_collision_checkpoint[soa_index]", soa->current_collision_checkpoint[soa_index]);
				//DEBUG::disp_text("vehicle was_above", was_above);
				//DEBUG::disp_text("vehicle use_cp_old", use_cp_old);
				//DEBUG::disp_text("vehicle use_t", use_t);
				if (!center_floor_sample_is_hole) {
				if (analytic_depenetration_t_in_surface_domain(use_t) && was_above) {
					auto normal = use_transform.basis[1];
					auto plane_pos = use_transform.origin;
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;
						float depth = (p0 - plane_pos).dot(normal);
						if (depth >= 0.0f) continue;
						SimVec3 d = normal * (-depth);
						ADD_VEC3(collision_push_total, d);
						overall_hit_detected_flag |= 1;
						depenetration += d;
						ADD_VEC3(collision_push_track, d);
						soa->current_checkpoint[soa_index] = use_cp_old;
					}
				}
				const TrackSegment &old_segment = track->segments[track->checkpoints[use_cp_old].road_segment];
				if (old_segment.road_shape->supports_edge_rails() && was_inside && track_segment_longitudinal_t_in_domain(use_t.y) && was_above) {
					const TrackSegment &segment = old_segment;
					const bool side_possible[2] = {
						segment.left_rail_height > 0.0f && track_segment_rail_side_active(segment, 0, use_t.y),
						segment.right_rail_height > 0.0f && track_segment_rail_side_active(segment, 1, use_t.y),
					};
					trace_analytic_rail_context("old", use_cp_old, use_t, was_inside, was_above, side_possible);
					if (side_possible[0] || side_possible[1]) {
					RoadTransform root_t;
					RoadTransform root_derivative;
					segment.curve_matrix->sample_with_derivative(root_t, root_derivative, use_t.y);
					TrackEdgeRailSide sides[2];
					segment.road_shape->get_edge_rail_sides(
						sides,
						use_t.y,
						use_transform.origin,
						root_t,
						root_derivative,
						segment.left_rail_height,
						segment.right_rail_height);
					if (draw_rail_candidates) {
						draw_nearest_rail_candidate(sides, machine_position + depenetration, soa, soa_index, _TICK_DELTA);
					}
					const bool side_active[2] = {
						side_possible[0] && sides[0].height > 0.0f,
						side_possible[1] && sides[1].height > 0.0f,
					};
					const float side_height[2] = {
						sides[0].height * root_t.scale.y,
						sides[1].height * root_t.scale.y,
					};
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;
						for (int i = 0; i < 2; i++) {
							//if (i == 1 && use_t.x < -1.0f)
							//{
							//	continue;
							//}
							//if (i == 0 && use_t.x > 1.0f)
							//{
							//	continue;
							//}
							const TrackEdgeRailSide &side = sides[i];
							if (!side_active[i])
							{
								continue;
							}
							float depth = 0.0f;
							if (!analytic_rail_corner_hit_valid("old", use_cp_old, wc_idx, i, side, p0, side_height[i], &depth)) {
								continue;
							}
							//DEBUG::disp_text("use_hit_t old", use_hit_t);
							SimVec3 d = side.rail_n * (-depth + rail_depenetration_epsilon);
							if (trace_rail) {
								godot::UtilityFunctions::print(
									godot::String("MXT_RAIL_APPLY tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
									godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
									godot::String(" pass=old cp="), static_cast<int64_t>(use_cp_old),
									godot::String(" wc="), static_cast<int64_t>(wc_idx),
									godot::String(" side="), static_cast<int64_t>(i),
									godot::String(" depth="), depth,
									godot::String(" bias="), rail_depenetration_epsilon,
									godot::String(" push=("), d.x, godot::String(","), d.y, godot::String(","), d.z, godot::String(")"));
							}
							ADD_VEC3(collision_push_total, d);
							depenetration += d;
							overall_hit_detected_flag |= 2;
							ADD_VEC3(collision_push_rail, d);
							max_rail_contact_push = std::max(max_rail_contact_push, d.length());
						}
					}
					}
				}
				}
			}
				physics_profile_mark(profile ? profile->old_analytic_us : nullptr, profile_step);
				int use_cp_new = -1;
				bool new_valid = false;
				const bool old_center_floor_pass_was_duplicate =
					old_collision.from_center_floor &&
					analytic_depenetration_t_in_surface_domain(use_t) &&
					was_above &&
					depenetration.x == 0.0f &&
					depenetration.y == 0.0f &&
					depenetration.z == 0.0f;
				if (!old_center_floor_pass_was_duplicate && was_above && !center_floor_sample_is_hole) {
					use_cp_new = track->get_best_checkpoint(machine_position + depenetration, soa->current_collision_checkpoint[soa_index], scratch);
					new_valid = use_cp_new != -1;
					if (new_valid)
					{
						const TrackSegment &new_segment = track->segments[track->checkpoints[use_cp_new].road_segment];
						if (!new_segment.analytic_collision_enabled) {
							new_valid = false;
						}
					}
				}
				physics_profile_mark(profile ? profile->new_checkpoint_us : nullptr, profile_step);
				if (new_valid)
				{
					const bool no_prior_depenetration =
						depenetration.x == 0.0f &&
						depenetration.y == 0.0f &&
						depenetration.z == 0.0f;
					const bool reuse_center_floor_sample =
						no_prior_depenetration &&
						center_floor_sample_valid &&
						center_floor_sample.cp_idx == use_cp_new &&
						center_floor_sample.road_t.x != -1000.0f &&
						(center_floor_sample.terrain & TERRAIN::HOLE) == 0u;
					if (reuse_center_floor_sample) {
						use_t = center_floor_sample.road_t;
						use_transform = center_floor_sample.closest_surface;
					} else {
						SimVec3 unused_spatial_t;
						track->get_road_surface(use_cp_new, machine_position + depenetration, use_t, unused_spatial_t, use_transform, false);
					}
					if (analytic_depenetration_t_in_surface_domain(use_t)) {
					auto normal = use_transform.basis[1];
					auto plane_pos = use_transform.origin;
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;
						float depth = (p0 - plane_pos).dot(normal);
						if (depth >= 0.0f) continue;
						SimVec3 d = normal * (-depth);
						ADD_VEC3(collision_push_total, d);
						overall_hit_detected_flag |= 1;
						depenetration += d;
						ADD_VEC3(collision_push_track, d);
						soa->current_checkpoint[soa_index] = use_cp_new;
					}
				}
				TrackSegment *new_seg = &track->segments[track->checkpoints[use_cp_new].road_segment];
					if (new_seg->road_shape->supports_edge_rails() && was_inside && track_segment_longitudinal_t_in_domain(use_t.y)) {
					const TrackSegment &segment     = track->segments[track->checkpoints[use_cp_new].road_segment];
					const bool side_possible[2] = {
						segment.left_rail_height > 0.0f && track_segment_rail_side_active(segment, 0, use_t.y),
						segment.right_rail_height > 0.0f && track_segment_rail_side_active(segment, 1, use_t.y),
					};
					trace_analytic_rail_context("new", use_cp_new, use_t, was_inside, true, side_possible);
					if (side_possible[0] || side_possible[1]) {
					RoadTransform root_t;
					RoadTransform root_derivative;
					segment.curve_matrix->sample_with_derivative(root_t, root_derivative, use_t.y);
					TrackEdgeRailSide sides[2];
					segment.road_shape->get_edge_rail_sides(
						sides,
						use_t.y,
						use_transform.origin,
						root_t,
						root_derivative,
						segment.left_rail_height,
						segment.right_rail_height);
					if (draw_rail_candidates) {
						draw_nearest_rail_candidate(sides, machine_position + depenetration, soa, soa_index, _TICK_DELTA);
					}
					const bool side_active[2] = {
						side_possible[0] && sides[0].height > 0.0f,
						side_possible[1] && sides[1].height > 0.0f,
					};
					const float side_height[2] = {
						sides[0].height * root_t.scale.y,
						sides[1].height * root_t.scale.y,
					};
					for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
						SimVec3 p0 = wall_corner_world[wc_idx] + depenetration;

						for (int i = 0; i < 2; i++) {
							//if (i == 1 && use_t.x < -1.0f)
							//{
							//	continue;
							//}
							//if (i == 0 && use_t.x > 1.0f)
							//{
							//	continue;
							//}
							const TrackEdgeRailSide &side = sides[i];
							if (!side_active[i])
							{
								continue;
							}
							float depth = 0.0f;
							if (!analytic_rail_corner_hit_valid("new", use_cp_new, wc_idx, i, side, p0, side_height[i], &depth)) {
								continue;
							}
							//DEBUG::disp_text("use_hit_t new", use_hit_t);
							SimVec3 d = side.rail_n * (-depth + rail_depenetration_epsilon);
							if (trace_rail) {
								godot::UtilityFunctions::print(
									godot::String("MXT_RAIL_APPLY tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
									godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
									godot::String(" pass=new cp="), static_cast<int64_t>(use_cp_new),
									godot::String(" wc="), static_cast<int64_t>(wc_idx),
									godot::String(" side="), static_cast<int64_t>(i),
									godot::String(" depth="), depth,
									godot::String(" bias="), rail_depenetration_epsilon,
									godot::String(" push=("), d.x, godot::String(","), d.y, godot::String(","), d.z, godot::String(")"));
							}
							ADD_VEC3(collision_push_total, d);
							depenetration += d;
							overall_hit_detected_flag |= 2;
							ADD_VEC3(collision_push_rail, d);
							max_rail_contact_push = std::max(max_rail_contact_push, d.length());
						}
						}
					}
					}
				}
				physics_profile_mark(profile ? profile->new_analytic_us : nullptr, profile_step);
				if (track) {
					const int mesh_wall_cp = soa->current_checkpoint[soa_index];
					if (mesh_wall_cp >= 0 && mesh_wall_cp < track->num_checkpoints) {
						if (track->num_mesh_collision_triangles > 0) {
							ensure_wall_corner_old_world();
							SimAABB mesh_cast_bounds;
							mesh_cast_bounds.position = old_machine_position;
							mesh_cast_bounds.size = SimVec3();
							mesh_cast_bounds.expand_to(machine_position);
							for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
								mesh_cast_bounds.expand_to(wall_corner_world[wc_idx]);
								mesh_cast_bounds.expand_to(wall_corner_old_world[wc_idx]);
							}
							const uint8_t mesh_cast_mask =
								(mesh_floor_depenetration_enabled ? CAST_FLAGS::WANTS_TRACK : 0) |
								CAST_FLAGS::WANTS_RAIL |
								CAST_FLAGS::WANTS_BACKFACE |
								CAST_FLAGS::SAMPLE_FROM_P1;
							const bool use_mesh_cast_candidates = track->collect_mesh_cast_candidates(mesh_cast_bounds, mesh_cast_mask, scratch);
							const bool skip_empty_candidate_casts =
								use_mesh_cast_candidates && scratch.mesh_cast_candidate_count == 0;
							const SimVec3 mesh_side_reference_point = old_machine_position;
							if (trace_rail) {
								godot::UtilityFunctions::print(
									godot::String("MXT_RAIL_MESH_CONTEXT tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
									godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
									godot::String(" cp="), static_cast<int64_t>(mesh_wall_cp),
									godot::String(" candidates_used="), use_mesh_cast_candidates,
									godot::String(" candidate_count="), static_cast<int64_t>(scratch.mesh_cast_candidate_count),
									godot::String(" skip_empty="), skip_empty_candidate_casts,
									godot::String(" mask="), static_cast<int64_t>(mesh_cast_mask),
									godot::String(" bounds_pos=("), mesh_cast_bounds.position.x, godot::String(","), mesh_cast_bounds.position.y, godot::String(","), mesh_cast_bounds.position.z, godot::String(")"),
									godot::String(" bounds_size=("), mesh_cast_bounds.size.x, godot::String(","), mesh_cast_bounds.size.y, godot::String(","), mesh_cast_bounds.size.z, godot::String(")"));
							}
							auto sweep_mesh_plane_and_depenetrate = [&](const SimVec3 &p0, const SimVec3 &p1, const char *sweep_name, int sweep_corner) {
								CollisionData hit;
								const bool original_bounds_ray =
									depenetration.x == 0.0f &&
									depenetration.y == 0.0f &&
									depenetration.z == 0.0f;
								const bool use_collected_candidates =
									use_mesh_cast_candidates &&
									(original_bounds_ray || (mesh_cast_bounds.has_point(p0) && mesh_cast_bounds.has_point(p1)));
								if (use_collected_candidates) {
									track->cast_vs_mesh_candidates_fast(
										hit,
										p0,
										p1,
										mesh_cast_mask,
										mesh_wall_cp,
										&scratch,
										false,
										&mesh_side_reference_point);
								} else {
									track->cast_vs_mesh_fast(
										hit,
										p0,
										p1,
										mesh_cast_mask,
										mesh_wall_cp,
										&scratch,
										false,
										&mesh_side_reference_point);
								}
								if (!hit.collided) {
									if (trace_rail) {
										godot::UtilityFunctions::print(
											godot::String("MXT_RAIL_MESH_CAST tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
											godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
											godot::String(" sweep="), godot::String(sweep_name),
											godot::String(" wc="), static_cast<int64_t>(sweep_corner),
											godot::String(" used_candidates="), use_collected_candidates,
											godot::String(" hit=false p0=("), p0.x, godot::String(","), p0.y, godot::String(","), p0.z, godot::String(")"),
											godot::String(" p1=("), p1.x, godot::String(","), p1.y, godot::String(","), p1.z, godot::String(")"));
									}
									return;
								}
								const bool kill_hit = (hit.road_data.terrain & TERRAIN::KILL) != 0;
								if (kill_hit) {
									trigger_mesh_kill_collision();
								}
								const bool rail_hit = terrain_mesh_blocks_like_rail(hit.road_data.terrain);
								if (trace_rail) {
									godot::UtilityFunctions::print(
										godot::String("MXT_RAIL_MESH_CAST tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
										godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
										godot::String(" sweep="), godot::String(sweep_name),
										godot::String(" wc="), static_cast<int64_t>(sweep_corner),
										godot::String(" used_candidates="), use_collected_candidates,
										godot::String(" hit=true rail_hit="), rail_hit,
										godot::String(" terrain="), static_cast<int64_t>(hit.road_data.terrain),
										godot::String(" hit_cp="), static_cast<int64_t>(hit.road_data.cp_idx),
										godot::String(" hit_t=("), hit.road_data.road_t.x, godot::String(","), hit.road_data.road_t.y, godot::String(")"),
										godot::String(" point=("), hit.collision_point.x, godot::String(","), hit.collision_point.y, godot::String(","), hit.collision_point.z, godot::String(")"),
										godot::String(" normal=("), hit.collision_face_normal.x, godot::String(","), hit.collision_face_normal.y, godot::String(","), hit.collision_face_normal.z, godot::String(")"),
										godot::String(" p0=("), p0.x, godot::String(","), p0.y, godot::String(","), p0.z, godot::String(")"),
										godot::String(" p1=("), p1.x, godot::String(","), p1.y, godot::String(","), p1.z, godot::String(")"));
								}
								for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
									const SimVec3 p = wall_corner_world[wc_idx] + depenetration;
									const float depth = (p - hit.collision_face_point).dot(hit.collision_face_normal);
									if (depth > 0.0f) {
										continue;
									}
									const float push_bias = rail_hit ? rail_depenetration_epsilon : 0.0f;
									const SimVec3 d = hit.collision_face_normal * (-depth + push_bias);
									if (trace_rail) {
										godot::UtilityFunctions::print(
											godot::String("MXT_RAIL_MESH_APPLY tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
											godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
											godot::String(" sweep="), godot::String(sweep_name),
											godot::String(" cast_wc="), static_cast<int64_t>(sweep_corner),
											godot::String(" apply_wc="), static_cast<int64_t>(wc_idx),
											godot::String(" rail_hit="), rail_hit,
											godot::String(" depth="), depth,
											godot::String(" bias="), push_bias,
											godot::String(" push=("), d.x, godot::String(","), d.y, godot::String(","), d.z, godot::String(")"));
									}
									ADD_VEC3(collision_push_total, d);
									if (rail_hit) {
										ADD_VEC3(collision_push_rail, d);
										max_rail_contact_push = std::max(max_rail_contact_push, d.length());
										overall_hit_detected_flag |= 2;
									} else {
										ADD_VEC3(collision_push_track, d);
										overall_hit_detected_flag |= 1;
									}
									depenetration += d;
								}
							};
							if (!skip_empty_candidate_casts) {
								for (int wc_idx = 0; wc_idx < 4; ++wc_idx) {
									const SimVec3 p0 = old_machine_position + depenetration;
									const SimVec3 p1 = wall_corner_world[wc_idx] + depenetration;
									sweep_mesh_plane_and_depenetrate(p0, p1, "old_center_to_current_corner", wc_idx);
								}
							}
						}
					}
				}
				physics_profile_mark(profile ? profile->mesh_us : nullptr, profile_step);
				ADD_VEC3(position_current, depenetration);
				depenetration = SimVec3();
			}
			if (out_max_rail_contact_push) {
				*out_max_rail_contact_push = max_rail_contact_push;
			}
			return overall_hit_detected_flag;
	}
}
void PhysicsCar::apply_machine_collision_response_from_corners(int corner_collision_type_flag,
	float push_magnitude_rail, float push_magnitude_track, float rail_hit_sfx_strength,
	float current_world_speed,
	float speed_over_weight, bool include_start_projection)
{
	if (push_magnitude_track > 0.0023148148f) {
		if (corner_collision_type_flag & 1)
		{
			soa->machine_state[soa_index] |= MACHINESTATE::LOWGRIP;
		}
	}

	if (push_magnitude_rail > 0.0023148148f) {
		if ((corner_collision_type_flag & 2) && (soa->machine_state[soa_index] & MACHINESTATE::LOWGRIP) == 0)
			soa->machine_state[soa_index] |= MACHINESTATE::TOOKDAMAGE;
	}

	bool is_significant_collision_event =
	(push_magnitude_rail > 0.0046296296f) && (speed_over_weight > 0.0046296296f);

	bool apply_full_response = false;
	if (soa->frames_since_start_2[soa_index] > 0x3c && is_significant_collision_event &&
		(soa->machine_state[soa_index] & MACHINESTATE::TOOKDAMAGE)) {
		apply_full_response = true;
}

if (apply_full_response) {
	STORE_VEC3(collision_response, LOAD_VEC3(collision_push_total));

	if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP)
	{
		STORE_VEC3(velocity, LOAD_VEC3(velocity) + LOAD_VEC3(velocity).dot(LOAD_VEC3(collision_response).normalized()) * -LOAD_VEC3(collision_response).normalized() * 1.05f);
	}else{
		float dot_push_vel_norm = 0.0f;
		if (push_magnitude_rail > 0.0001f && current_world_speed > 0.0001f)
			dot_push_vel_norm = LOAD_VEC3(collision_push_total).normalized().dot(LOAD_VEC3(velocity).normalized());

		float clamped_opposing_dot_prod = std::min(dot_push_vel_norm, 0.0f);

		float response_intensity_factor = 0.0f;
		if (speed_over_weight > 0.02314814814f) {
			float dot_push_track_normal = 0.0f;
			if (push_magnitude_rail > 0.0001f && LOAD_VEC3(track_surface_normal).length_squared() > 0.0001f)
				dot_push_track_normal =
			LOAD_VEC3(collision_push_total).normalized().dot(LOAD_VEC3(track_surface_normal).normalized());

			if (std::abs(dot_push_track_normal) < 0.7f) {
				response_intensity_factor =
				(0.15f + (clamped_opposing_dot_prod * clamped_opposing_dot_prod)) / 1.5f;

				if ((soa->machine_state[soa_index] & MACHINESTATE::B10) == 0) {
					response_intensity_factor =
					(response_intensity_factor * current_world_speed) / 500.0f;
					if (soa->rail_collision_timer[soa_index] != 0)
						response_intensity_factor *= 0.15f;
				} else {
					response_intensity_factor =
					(response_intensity_factor * current_world_speed) / 2000.0f;
				}
			}
		}

		if (clamped_opposing_dot_prod < -0.5f) {
			soa->machine_state[soa_index] &= ~(MACHINESTATE::JUST_HIT_DASHPLATE |
				MACHINESTATE::BOOSTING_DASHPLATE |
				MACHINESTATE::JUST_PRESSED_BOOST |
				MACHINESTATE::BOOSTING);
			soa->machine_state[soa_index] &= ~(MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING);
			soa->boost_frames[soa_index] = 0;
			soa->boost_frames_manual[soa_index] = 0;
		}

		if ((soa->machine_state[soa_index] & MACHINESTATE::TOOKDAMAGE) && soa->breakdown_frame_counter[soa_index] == 0) {
			soa->last_hit_tick[soa_index] = soa->frames_since_start[soa_index];
			soa->last_hit_sfx_strength[soa_index] = rail_hit_sfx_strength;
			soa->has_last_hit_tick[soa_index] = true;

			float damage_base = response_intensity_factor * soa->stat_body[soa_index];
			if ((soa->machine_state[soa_index] & MACHINESTATE::B10) == 0 && damage_base > 20.0f)
				damage_base = 20.0f;

			float max_damage_this_hit = 1.01f * soa->calced_max_energy[soa_index];
			float actual_damage_taken = std::min(damage_base, max_damage_this_hit);
			soa->energy[soa_index] -= actual_damage_taken;

			if (soa->energy[soa_index] < 0.0f) {
				if ((soa->machine_state[soa_index] & (MACHINESTATE::COMPLETEDRACE_1_Q|MACHINESTATE::ZEROHP)) == 0) {
					soa->breakdown_frame_counter[soa_index] = 0x3c;
				}
				soa->energy[soa_index] = 0.0f;
				soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
				soa->base_speed[soa_index] = 0.0f;
			}
		}

		SimVec3 response_impulse_base;
		if (push_magnitude_rail > 0.0001f)
			response_impulse_base = LOAD_VEC3(collision_push_total).normalized() *
		(clamped_opposing_dot_prod * current_world_speed);
		else
			response_impulse_base = SimVec3();

		if (clamped_opposing_dot_prod < 0.0f) {
			float ratio_clamped_dot = clamped_opposing_dot_prod / 0.7f;
			float val_inside_sqrt = std::max(0.0f, 1.0f - (ratio_clamped_dot * ratio_clamped_dot));
			float sqrt_factor = std::sqrt(val_inside_sqrt);

			float base_speed_mult;
			float boost_turbo_additional_mult;

			if (soa->rail_collision_timer[soa_index] == 0) {
				base_speed_mult = 0.2f + 0.6f * sqrt_factor;
				boost_turbo_additional_mult = 0.4f * base_speed_mult;
			} else {
				base_speed_mult = 0.64f + 0.35f * sqrt_factor;
				boost_turbo_additional_mult = 0.6f * base_speed_mult;
			}
			if (!soa->s_boost_active[soa_index]){
				soa->base_speed[soa_index] *= base_speed_mult;
			}
			soa->boost_turbo[soa_index] *= (0.3f + boost_turbo_additional_mult);
		}

		if (speed_over_weight <= 1.851851851f) {
			ADD_VEC3(velocity, response_impulse_base * -1.0f);
		} else {
			float final_impulse_scale_factor;
			if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
				final_impulse_scale_factor = 3.4f - 1.7f * std::abs(clamped_opposing_dot_prod);
			} else if (soa->rail_collision_timer[soa_index] == 0) {
				final_impulse_scale_factor = 3.0f - 1.5f * std::abs(clamped_opposing_dot_prod);
			} else {
				final_impulse_scale_factor = 2.0f - std::abs(clamped_opposing_dot_prod);
			}

			ADD_VEC3(velocity, response_impulse_base * (-final_impulse_scale_factor));

			if (soa->rail_collision_timer[soa_index] == 0) {
				set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			}
			soa->rail_collision_timer[soa_index] = 20;
		}

		if (response_impulse_base.length_squared() > 0.000001f) {
			SimVec3 impulse_local_for_visuals =
			mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), response_impulse_base);
			soa->visual_rotation_z[soa_index] += impulse_local_for_visuals.x;
			soa->visual_rotation_x[soa_index] += impulse_local_for_visuals.z;
		}

		if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) {
			for (int i = 0; i < 4; ++i) {
				apply_torque_from_force(LOAD_VEC3(track_surface_normal), response_impulse_base * -0.002f);
			}
		}

		if (soa->frames_since_start_2[soa_index] > 60)
			align_machine_y_with_track_normal_immediate();
	}

} else if ((soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) && speed_over_weight >= 0.0462962962962f) {
		SimVec3 up_dir = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 1, 0)); // get the vehicle's local up direction normal vector
		float up_dot_track = normalized_safe(up_dir).dot(normalized_safe(LOAD_VEC3(track_surface_normal)));
		float vel_dot_track = normalized_safe(LOAD_VEC3(velocity)).dot(normalized_safe(LOAD_VEC3(track_surface_normal)));
		if (up_dot_track < 0.0f)
			up_dot_track = 0.0f;
		const float landing_penalty_factor = landing_alignment_penalty_factor(soa->air_time[soa_index]);
		const float effective_up_dot_track = 1.0f - ((1.0f - up_dot_track) * landing_penalty_factor);
		const SimVec3 velocity_before_landing_penalty = LOAD_VEC3(velocity);
		float vel_along_track = LOAD_VEC3(velocity).length() * vel_dot_track;
		soa->base_speed[soa_index] = soa->base_speed[soa_index] * effective_up_dot_track * effective_up_dot_track;
		SimVec3 normal_vel = LOAD_VEC3(track_surface_normal) * vel_along_track;
		float vel_align_factor = 2.0f * std::abs(0.5f + vel_dot_track);
		SimVec3 vel_add = LOAD_VEC3(velocity) - normal_vel;
		if (vel_align_factor >= 0.8f && soa->energy[soa_index] > 0.001f && (soa->machine_state[soa_index] & MACHINESTATE::HAS_DISCONNECTED) == 0)
		{
			STORE_VEC3(velocity, LOAD_VEC3(velocity) * 1.4f);
			soa->base_speed[soa_index] += 2.0f;
		}else
		{
			vel_add = set_vec3_length(vel_add, 0.9f * (1.0f - 1.11f * vel_align_factor) * up_dot_track);
			const SimVec3 full_penalty_velocity =
				velocity_before_landing_penalty - normal_vel * up_dot_track + vel_add;
			STORE_VEC3(velocity,
				velocity_before_landing_penalty +
				(full_penalty_velocity - velocity_before_landing_penalty) * landing_penalty_factor);
		}
		soa->air_time[soa_index] = 0;
	}

	if (include_start_projection && soa->frames_since_start_2[soa_index] <= 90)
	{
		ADD_VEC3(velocity, LOAD_VEC3(track_surface_normal) * -(LOAD_VEC3(velocity).dot(LOAD_VEC3(track_surface_normal))));
	}
	if (soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) {
		soa->air_time[soa_index] = 0;
		clear_floor_disconnected(soa, soa_index);
	}
};
