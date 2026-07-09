#include "physics_car_internal.h"

bool PhysicsCar::apply_damage(float impactStrength)
{
    // Already invulnerable or in breakdown? No damage is processed.
	if (soa->s_boost_active[soa_index])
		return false;

	if (soa->breakdown_frame_counter[soa_index] != 0)
		return false;

	float rawDamage = impactStrength * soa->stat_body[soa_index];

    // Never exceed 101 % of maxEnergy
	const float maxAllowedDamage = 1.01f * soa->calced_max_energy[soa_index];
	rawDamage = std::min(rawDamage, maxAllowedDamage);

	soa->energy[soa_index] -= rawDamage;

	if (soa->energy[soa_index] >= 0.0f)
        return false;  // Machine survives the hit

    // Energy fell below zero → breakdown/KO handling
    soa->energy[soa_index]      = 0.0f;
    soa->base_speed[soa_index]   = 0.0f;
    soa->machine_state[soa_index]      |= MACHINESTATE::ZEROHP;

    // Start countdown only if race not finished and KO flag wasn’t already set
    const bool canStartBreakdown =
    (soa->machine_state[soa_index] & (MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::ZEROHP)) == 0;

    if (canStartBreakdown)
	soa->breakdown_frame_counter[soa_index] = 60;

    return canStartBreakdown;
}

float PhysicsCar::prepare_impact_direction_info(ImpactData &impact, const SimVec3 &impactDirWorld)
{
    // 1)  Transform impact direction into the machine’s local space


	impact.relative_dir_local = mxt_inverse_transform_point(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(position_current), impactDirWorld);

    /* Subtract the (locally expressed) track-surface normal so that the
       direction truly represents the *relative* approach vector. */
	SimVec3 localTrackNormal = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), LOAD_VEC3(track_surface_normal));


	impact.relative_dir_local.x -= localTrackNormal.x;
	impact.relative_dir_local.y -= localTrackNormal.y;
	impact.relative_dir_local.z -= localTrackNormal.z;

    /* ---------------------------------------------------------------------
     * 2)  Normalise or default to forward if the vector is degenerate
     * ------------------------------------------------------------------ */
	float len = sqrtf( impact.relative_dir_local.x * impact.relative_dir_local.x +
		impact.relative_dir_local.y * impact.relative_dir_local.y +
		impact.relative_dir_local.z * impact.relative_dir_local.z );

	float kEpsilon = 0.0000001;

	if (len <= kEpsilon) {
		impact.relative_dir_local.x = 0.0f;
		impact.relative_dir_local.y = 0.0f;
        impact.relative_dir_local.z = -1.0f;     // fall-back: forwards
    } else {
        impact.relative_dir_local.normalize();   // keeps len == 1
    }

    /* ---------------------------------------------------------------------
     * 3)  Pick the canonical collision axis (X, Y, or Z)
     *
     *     A small “dead zone” (5 % of |Y|) biases hits away from the Y-axis
     *     unless it really is dominant.
     * ------------------------------------------------------------------ */
    const float absX = fabsf(impact.relative_dir_local.x);
    const float absY = fabsf(impact.relative_dir_local.y);
    const float absZ = fabsf(impact.relative_dir_local.z);
    const float yThreshold = 0.05f * absY;

    impact.impact_axis_z = 0.0f;  // cleared unless a Z hit is selected
    float dominant = 0.0f;         // magnitude of dominant component (return value)

    if (absX <= yThreshold) {
        /* X is negligible compared with 5 % of Y */
        if (yThreshold <= absZ) {          /* Z dominates */
	impact.relative_dir_world = { 0.0f, 0.0f, impact.relative_dir_local.z };
	impact.impact_axis_z      =  impact.relative_dir_local.z;
	dominant                   =  absZ;
        } else {                           /* Y dominates */
	impact.relative_dir_world = { 0.0f, impact.relative_dir_local.y, 0.0f };
            dominant                   =  yThreshold;   // matches original behaviour
        }
    } else if (absX <= absZ) {             /* Z dominates (X was bigger than 5 %Y but <= Z) */
        impact.relative_dir_world = { 0.0f, 0.0f, impact.relative_dir_local.z };
        impact.impact_axis_z      =  impact.relative_dir_local.z;
        dominant                   =  absZ;
    } else {                               /* X dominates */
        impact.relative_dir_world = { impact.relative_dir_local.x, 0.0f, 0.0f };
        dominant                   =  absX;
    }

    /* Ensure the chosen axis vector is unit length before we rotate it out. */
    impact.relative_dir_world.normalize();

    /* ---------------------------------------------------------------------
     * 4)  Compute scalar speed per unit mass, sanitising NaN/Inf values
     * ------------------------------------------------------------------ */
    if (!std::isfinite(soa->velocity_x[soa_index]) ||
	!std::isfinite(soa->velocity_y[soa_index]) ||
	!std::isfinite(soa->velocity_z[soa_index]))
    {
	impact.speed_per_mass = 0.0f;
    } else {
	const float speed = sqrtf(soa->velocity_x[soa_index] * soa->velocity_x[soa_index] +
		soa->velocity_y[soa_index] * soa->velocity_y[soa_index] +
		soa->velocity_z[soa_index] * soa->velocity_z[soa_index]);
	impact.speed_per_mass = speed / soa->stat_weight[soa_index];
    }

    /* ---------------------------------------------------------------------
     * 5)  Rotate the canonical direction back to world space and clean up
     * ------------------------------------------------------------------ */
    impact.relative_dir_world = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), impact.relative_dir_world);

    return fabsf(dominant);
}

float PhysicsCar::scale_collision_impulse_and_damage(bool other_machine_b10_flag)
{
	    // Interpret flags once for readability
	const bool isSpinAttacking = (soa->machine_state[soa_index] & MACHINESTATE::SPINATTACKING) != 0;
	const bool isSideAttacking = (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) != 0;
	const bool isB10           = (soa->machine_state[soa_index] & MACHINESTATE::B10)           != 0;
	const bool otherIsB10      = other_machine_b10_flag != 0;

	float scale = 1.0f;

    /* ---------------------------------------------------------------------
       Case 1: neither spin-attack nor side-attack
       ------------------------------------------------------------------ */
	if (!isSpinAttacking && !isSideAttacking)
	{
		if (isB10)
		{
            scale *= 0.8f;            // Slightly reduced impulse for B10 state
        }
        return scale;                 // Nothing else affects this path
    }

    /* ---------------------------------------------------------------------
       Case 2: currently in a spin- or side-attack
       ------------------------------------------------------------------ */

    // Spin intensity factor ∈ [0.5 , 1.0]; safe for side-attack (unused then)
    const float spinIntensity =
    0.5f + 0.5f * soa->spinattack_decrement[soa_index];

    if (!isB10)   // Machine is *not* in B10 state while attacking
    {
	if (!otherIsB10)
	{
            // Attacker !B10 vs victim !B10
		scale *= isSpinAttacking ? (3.0f * spinIntensity) : 2.0f;
	}
	else
	{
            // Attacker !B10 vs victim  B10
		scale *= isSpinAttacking ? (5.0f * spinIntensity) : 6.0f;
	}
    }
    else          // Machine *is* in B10 state while attacking
    {
        // Side-attack <→ Spin-attack multipliers differ
	scale *= isSpinAttacking ? 3.5f : 4.0f;
    }

    return scale;
}

void PhysicsCar::buildSweepForMachine(float cappedSpeedMps, SimVec3 &sweepStartOut, SimVec3 &cappedVelocityOut)
{
    // Distance travelled during last frame
	SimVec3 delta = LOAD_VEC3(position_old_dupe) - LOAD_VEC3(position_current);

	float travelled = delta.length();

	if (travelled <= 13.88888f)
	{
        sweepStartOut     = LOAD_VEC3(position_old_dupe);    // use previous position as start
        cappedVelocityOut = LOAD_VEC3(velocity);
    }
    else
    {
	delta = set_vec3_length(-delta, 13.88888f);

	sweepStartOut = LOAD_VEC3(position_current) + delta;

	cappedVelocityOut = set_vec3_length(LOAD_VEC3(velocity), cappedSpeedMps);
	}
}

static float closest_points_between_segments(
	const SimVec3& p1, const SimVec3& q1,
	const SimVec3& p2, const SimVec3& q2,
	SimVec3& c1, SimVec3& c2)
{
	constexpr float kEpsilon = 0.000001f;
	const SimVec3 d1 = q1 - p1;
	const SimVec3 d2 = q2 - p2;
	const SimVec3 r = p1 - p2;
	const float a = d1.dot(d1);
	const float e = d2.dot(d2);
	const float f = d2.dot(r);
	float s = 0.0f;
	float t = 0.0f;

	if (a <= kEpsilon && e <= kEpsilon) {
		c1 = p1;
		c2 = p2;
		return c1.distance_squared_to(c2);
	}
	if (a <= kEpsilon) {
		t = std::clamp(f / e, 0.0f, 1.0f);
	} else {
		const float c = d1.dot(r);
		if (e <= kEpsilon) {
			s = std::clamp(-c / a, 0.0f, 1.0f);
		} else {
			const float b = d1.dot(d2);
			const float denom = a * e - b * b;
			if (denom != 0.0f) {
				s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
			}
			t = (b * s + f) / e;
			if (t < 0.0f) {
				t = 0.0f;
				s = std::clamp(-c / a, 0.0f, 1.0f);
			} else if (t > 1.0f) {
				t = 1.0f;
				s = std::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}

	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
	return c1.distance_squared_to(c2);
}

static void move_to_plane_side(PhysicsCar& car, const SimVec3& plane_point, const SimVec3& normal, float desired_signed_distance)
{
	const SimVec3 pos = LOAD_CAR_VEC3(car, position_current);
	const float signed_distance = (pos - plane_point).dot(normal);
	if (desired_signed_distance < 0.0f) {
		if (signed_distance <= desired_signed_distance) {
			return;
		}
	} else if (signed_distance >= desired_signed_distance) {
		return;
	}
	const float correction = desired_signed_distance - signed_distance;
	if (std::abs(correction) > 0.000001f) {
		STORE_CAR_VEC3(car, position_current, pos + normal * correction);
	}
}

static void apply_car_collision_knockback(PhysicsCar& car, const SimVec3& impulse)
{
	STORE_CAR_VEC3(car, collision_response, impulse);
	const float weight = car.soa->stat_weight[car.soa_index];
	STORE_CAR_VEC3(car, velocity, LOAD_CAR_VEC3(car, velocity) + impulse * weight);
}

static inline bool machine_is_restoring(const PhysicsCar& car)
{
	return car.soa->restore_state[car.soa_index] != 0;
}

static bool handle_machine_v_machine_collision_impl(PhysicsCar& self, PhysicsCar &other_machine, bool this_bumper, bool other_bumper)
{
	PhysicsCarSoA* soa = self.soa;
	const int soa_index = self.soa_index;
	if (machine_is_restoring(self) || machine_is_restoring(other_machine)) {
		return false;
	}
	if (soa->s_boost_active[soa_index] || other_machine.soa->s_boost_active[other_machine.soa_index]) {
		return false;
	}
	if (((soa->state_2[soa_index] | other_machine.soa->state_2[other_machine.soa_index]) & 0x10u) != 0) {
		return false;
	}

	const float radius1 = this_bumper ? 3.0f : 2.0f;
	const float radius2 = other_bumper ? 3.0f : 2.0f;
	const float combined_radius = radius1 + radius2;
	const SimVec3 p1_old = LOAD_CAR_VEC3(self, position_old_dupe);
	const SimVec3 p1 = LOAD_CAR_VEC3(self, position_collision_snapshot);
	const SimVec3 p2_old = LOAD_CAR_VEC3(other_machine, position_old_dupe);
	const SimVec3 p2 = LOAD_CAR_VEC3(other_machine, position_collision_snapshot);
	const SimVec3 seg1 = p1 - p1_old;
	const SimVec3 seg2 = p2 - p2_old;
	const SimVec3 mid_delta = ((p1_old + p1) - (p2_old + p2)) * 0.5f;
	const float segment_bound =
		combined_radius +
		0.5f * seg1.length() +
		0.5f * seg2.length();
	if (mid_delta.length_squared() >= segment_bound * segment_bound) {
		return false;
	}

	SimVec3 closest1;
	SimVec3 closest2;
	const float dist_sq = closest_points_between_segments(p1_old, p1, p2_old, p2, closest1, closest2);
	if (dist_sq >= combined_radius * combined_radius) {
		return false;
	}

	SimVec3 collision_normal = p2_old - p1_old;
	if (collision_normal.length_squared() <= 0.000001f) {
		collision_normal = p2 - p1;
	}
	if (collision_normal.length_squared() <= 0.000001f) {
		collision_normal = (p1 - p1_old) - (p2 - p2_old);
	}
	if (collision_normal.length_squared() <= 0.000001f) {
		collision_normal = LOAD_CAR_TRANSFORM(self, basis_physical).basis.get_column(0);
	}
	collision_normal.normalize();

	const SimVec3 relative_motion = (p1 - p1_old) - (p2 - p2_old);
	const float closing_speed = relative_motion.dot(collision_normal);
	if (closing_speed <= 0.0f) {
		return false;
	}

	const bool this_spin_attacking = (soa->machine_state[soa_index] & MACHINESTATE::SPINATTACKING) != 0;
	const bool this_side_attacking = (soa->machine_state[soa_index] & MACHINESTATE::SIDEATTACKING) != 0;
	const bool other_spin_attacking = (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::SPINATTACKING) != 0;
	const bool other_side_attacking = (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::SIDEATTACKING) != 0;
	const bool this_attacking = this_spin_attacking || this_side_attacking;
	const bool other_attacking = other_spin_attacking || other_side_attacking;
	const bool this_alive_before = (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0;
	const bool other_alive_before = (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::ZEROHP) == 0;

	const SimVec3 plane_point = (p1 + p2) * 0.5f;
	constexpr float depenetration_overcorrection = 1.1f;
	move_to_plane_side(self, plane_point, collision_normal, -radius1 * depenetration_overcorrection);
	move_to_plane_side(other_machine, plane_point, collision_normal, radius2 * depenetration_overcorrection);

	SimVec3 impulse = collision_normal * (-0.8f * closing_speed);
	float impulse_strength = impulse.length();
	const float attack_impulse_strength = impulse_strength + ((this_attacking || other_attacking) ? 1.0f : 0.0f);

	float damage1 = impulse_strength;
	float damage2 = impulse_strength;
	SimVec3 impulse1 = impulse;
	SimVec3 impulse2 = -impulse;
	bool this_bumper_slide = false;
	bool other_bumper_slide = false;
	if (this_attacking || other_attacking) {
		damage1 = 0.0f;
		damage2 = 0.0f;
		if (other_attacking && !this_spin_attacking) {
			damage1 = attack_impulse_strength * (other_side_attacking ? 20.0f : 10.0f);
			if (this_side_attacking) {
				damage1 *= 2.0f;
			}
		}
		if (this_attacking && !other_spin_attacking) {
			damage2 = attack_impulse_strength * (this_side_attacking ? 20.0f : 10.0f);
			if (other_side_attacking) {
				damage2 *= 2.0f;
			}
		}
	}
	if (this_attacking && !other_attacking) {
		impulse1 = impulse * 2.0f;
		impulse2 = collision_normal * (1.5f * attack_impulse_strength);
	} else if (!this_attacking && other_attacking) {
		impulse1 = collision_normal * (-1.5f * attack_impulse_strength);
		impulse2 = -impulse * 2.0f;
	} else if (this_attacking && other_attacking) {
		impulse1 = impulse * 0.2f;
		impulse2 = impulse * -0.2f;
		if (!this_spin_attacking) {
			impulse1 = collision_normal * (-1.5f * attack_impulse_strength * (this_side_attacking ? 2.0f : 1.0f));
		}
		if (!other_spin_attacking) {
			impulse2 = collision_normal * (1.5f * attack_impulse_strength * (other_side_attacking ? 2.0f : 1.0f));
		}
	}
	if (this_bumper != other_bumper) {
		impulse1 = impulse1 * 1.5f;
		impulse2 = impulse2 * 1.5f;
		if (!this_bumper && !this_attacking) {
			impulse1 += collision_normal * -2.0f;
			damage1 += 12.0f;
			self.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			soa->rail_collision_timer[soa_index] = 24;
			this_bumper_slide = true;
		}
		if (!other_bumper && !other_attacking) {
			impulse2 += collision_normal * 2.0f;
			damage2 += 12.0f;
			other_machine.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			other_machine.soa->rail_collision_timer[other_machine.soa_index] = 24;
			other_bumper_slide = true;
		}
	}

	apply_car_collision_knockback(self, impulse1);
	apply_car_collision_knockback(other_machine, impulse2);
	soa->visual_rotation_z[soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(self, basis_physical), impulse1).x;
	soa->visual_rotation_x[soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(self, basis_physical), impulse1).z;
	other_machine.soa->visual_rotation_z[other_machine.soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(other_machine, basis_physical), impulse2).x;
	other_machine.soa->visual_rotation_x[other_machine.soa_index] += mxt_basis_inverse_rotate(LOAD_CAR_TRANSFORM(other_machine, basis_physical), impulse2).z;
	if (impulse_strength > 0.5f) {
		if (!this_bumper_slide) {
			self.remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			soa->drift_ramp[soa_index] = 0.0f;
		}
		if (!other_bumper_slide) {
			other_machine.remove_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
			other_machine.soa->drift_ramp[other_machine.soa_index] = 0.0f;
		}
	}
	if (other_attacking && damage1 > 0.0f) {
		self.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
		soa->rail_collision_timer[soa_index] = 20;
	}
	if (this_attacking && damage2 > 0.0f) {
		other_machine.set_flag_on_all_tilt_corners(TILTSTATE::DRIFT);
		other_machine.soa->rail_collision_timer[other_machine.soa_index] = 20;
	}
	if (damage1 > 0.0f) {
		self.apply_damage(damage1);
	}
	if (damage2 > 0.0f) {
		other_machine.apply_damage(damage2);
	}
	if (this_alive_before && damage1 > 0.0f && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) {
		soa->pending_ko_attacker_car_index[soa_index] = other_machine.soa->global_start + other_machine.soa_index;
	}
	if (other_alive_before && damage2 > 0.0f && (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::ZEROHP) != 0) {
		other_machine.soa->pending_ko_attacker_car_index[other_machine.soa_index] = soa->global_start + soa_index;
	}
	if (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) {
		soa->energy[soa_index] = 0.0f;
	}
	if (other_machine.soa->machine_state[other_machine.soa_index] & MACHINESTATE::ZEROHP) {
		other_machine.soa->energy[other_machine.soa_index] = 0.0f;
	}

	soa->machine_state[soa_index] |= (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::ACTIVE);
	other_machine.soa->machine_state[other_machine.soa_index] |= (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::ACTIVE);

	if (soa->frames_since_start_2[soa_index] == 0) {
		self.apply_initial_accel_activation(0.0f);
	}
	if (other_machine.soa->frames_since_start_2[other_machine.soa_index] == 0) {
		other_machine.apply_initial_accel_activation(0.0f);
	}

    return true;     // collision handled
}

bool PhysicsCar::handle_machine_v_machine_collision(PhysicsCar &other_machine)
{
	return handle_machine_v_machine_collision_impl(*this, other_machine, false, false);
}

bool PhysicsCar::handle_machine_v_bumper_collision(PhysicsCar &bumper_machine)
{
	return handle_machine_v_machine_collision_impl(*this, bumper_machine, false, true);
}
