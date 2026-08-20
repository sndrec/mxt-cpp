#include "physics_car_internal.h"

SimVec3 PhysicsCar::prepare_machine_frame(TrackQueryScratch &scratch, PhysicsCarFloorProfile* profile)
{
	// Reset input if we're in the starting countdown
	if (soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) {
		soa->input_steer_yaw[soa_index] = 0.0f;
		soa->input_steer_pitch[soa_index] = 0.0f;
		soa->input_brake[soa_index] = 0.0f;
		soa->input_strafe[soa_index] = 0.0f;
		soa->machine_state[soa_index] &= ~(MACHINESTATE::SIDEATTACKING |
			MACHINESTATE::JUST_PRESSED_BOOST |
			MACHINESTATE::SPINATTACKING);
	}

	soa->machine_state[soa_index] &= ~(MACHINESTATE::DIEDTHISFRAMEOOB_Q |
		MACHINESTATE::JUST_HIT_DASHPLATE |
		MACHINESTATE::RACEJUSTBEGAN_Q |
		MACHINESTATE::JUSTTAPPEDACCEL |
		MACHINESTATE::CROSSEDLAPLINE_Q |
		MACHINESTATE::JUSTLANDED |
		MACHINESTATE::AIRBORNEMORE0_2S_Q |
		MACHINESTATE::AIRBORNE);

	soa->state_2[soa_index] &= 0xfffffcff;
	soa->terrain_state[soa_index] = 0;
	soa->pending_dashplate_heat[soa_index] = 0.0f;
	soa->pending_dashplate_heat_reward_scale[soa_index] = 1.0f;

	const SimTransform previous_physical = LOAD_TRANSFORM(basis_physical_other);
	const SimVec3 previous_position = LOAD_VEC3(position_old);
	const SimVec3 current_position = LOAD_VEC3(position_current);
	STORE_TRANSFORM(basis_physical_other, LOAD_TRANSFORM(basis_physical));
	STORE_VEC3(position_old_dupe, current_position);
	STORE_VEC3(position_old, current_position);

	const int point_base = soa_index * 4;
	bool any_drift = false;
	for (int i = 0; i < 4; ++i) {
		if (soa->tilt_state[point_base + i] & TILTSTATE::DRIFT) {
			any_drift = true;
		}
	}
	const SimVec3x4 tilt_pos_old = mxt_transform_points4(
		previous_physical,
		previous_position,
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base) + sim_load4(soa->tilt_force + point_base) - sim_load4(soa->tilt_rest_length + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	sim_store4(soa->tilt_pos_old_x + point_base, tilt_pos_old.x);
	sim_store4(soa->tilt_pos_old_y + point_base, tilt_pos_old.y);
	sim_store4(soa->tilt_pos_old_z + point_base, tilt_pos_old.z);
	for (int i = 0; i < 4; ++i) {
		const int p = point_base + i;
		if (any_drift) {
			soa->tilt_state[p] |= TILTSTATE::DRIFT;
		}
	}


	SimVec3 ground_normal = SimVec3(0, 1, 0);
	if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) {
		ground_normal = get_avg_track_normal_from_tilt_corners(scratch, profile);
	}

	bool all_airborne = true;
	for (int i = 0; i < 4; ++i) {
		const int p = POINT_INDEX(i);
		if ((soa->tilt_state[p] & TILTSTATE::AIRBORNE) == 0) {
			all_airborne = false;
		}
	}

	if (all_airborne) {
		soa->machine_state[soa_index] |= MACHINESTATE::AIRBORNE;
		if (soa->air_time[soa_index] < 180)
			soa->air_time[soa_index] += 1;
		if (soa->air_time[soa_index] > 10)
			soa->machine_state[soa_index] |= MACHINESTATE::AIRBORNEMORE0_2S_Q;
	} else {
		if (soa->air_time[soa_index] != 0)
			soa->machine_state[soa_index] |= MACHINESTATE::JUSTLANDED;
		soa->machine_state[soa_index] &= ~MACHINESTATE::AIRBORNEMORE0_2S_Q;
		soa->state_2[soa_index] &= ~2u;
	}
	if (trace_mesh_floor_for_car(soa, soa_index)) {
		const SimVec3 machine_up =
			mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0.0f, 1.0f, 0.0f));
		const SimVec3 velocity = LOAD_VEC3(velocity);
		godot::UtilityFunctions::print(
			godot::String("MXT_SUSPENSION_SUMMARY tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" all_airborne="), all_airborne,
			godot::String(" just_landed="), (soa->machine_state[soa_index] & MACHINESTATE::JUSTLANDED) != 0u,
			godot::String(" air_time="), static_cast<int64_t>(soa->air_time[soa_index]),
			godot::String(" machine_state=0x"), godot::String::num_int64(static_cast<int64_t>(soa->machine_state[soa_index]), 16),
			godot::String(" tilt=(0x"), godot::String::num_int64(static_cast<int64_t>(soa->tilt_state[POINT_INDEX(0)]), 16),
			godot::String(",0x"), godot::String::num_int64(static_cast<int64_t>(soa->tilt_state[POINT_INDEX(1)]), 16),
			godot::String(",0x"), godot::String::num_int64(static_cast<int64_t>(soa->tilt_state[POINT_INDEX(2)]), 16),
			godot::String(",0x"), godot::String::num_int64(static_cast<int64_t>(soa->tilt_state[POINT_INDEX(3)]), 16), godot::String(")"),
			godot::String(" prev_pos=("), previous_position.x, godot::String(","), previous_position.y, godot::String(","), previous_position.z, godot::String(")"),
			godot::String(" pos=("), current_position.x, godot::String(","), current_position.y, godot::String(","), current_position.z, godot::String(")"),
			godot::String(" vel=("), velocity.x, godot::String(","), velocity.y, godot::String(","), velocity.z, godot::String(")"),
			godot::String(" machine_up=("), machine_up.x, godot::String(","), machine_up.y, godot::String(","), machine_up.z, godot::String(")"),
			godot::String(" ground_n=("), ground_normal.x, godot::String(","), ground_normal.y, godot::String(","), ground_normal.z, godot::String(")"));
	}

	soa->turning_related[soa_index] = 0.0f;
	soa->visual_rotation_z[soa_index] *= 0.8f;
	soa->visual_rotation_x[soa_index] *= 0.9f;

	if (soa->machine_state[soa_index] & MACHINESTATE::ACTIVE)
	{
		if (soa->frames_since_start_2[soa_index] != 0)
			soa->frames_since_start_2[soa_index] = std::min(255u, soa->frames_since_start_2[soa_index] + 1);
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0)
	{
		soa->energy[soa_index] += 1.111111f * soa->energy_recharge_mult[soa_index];
		if (soa->energy[soa_index] > soa->calced_max_energy[soa_index])
		{
			soa->energy[soa_index] = soa->calced_max_energy[soa_index];
		}
	}

	float vel_mag = LOAD_VEC3(velocity).length();
	soa->speed_kmh[soa_index] = 216.0f * (vel_mag / std::max(soa->stat_weight[soa_index], 0.001f));

	if ((soa->machine_state[soa_index] & MACHINESTATE::RETIRED) != 0 &&
		(soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		if (soa->speed_kmh[soa_index] >= 10.0f)
			STORE_VEC3(velocity, LOAD_VEC3(velocity) * 0.9f);
		else
			STORE_VEC3(velocity, SimVec3());
	}

	handle_attack_states();

	if (soa->car_hit_invincibility[soa_index] == 0) {
		if (soa->machine_state[soa_index] & MACHINESTATE::JUSTHITVEHICLE_Q)
			soa->car_hit_invincibility[soa_index] = 6;
	} else {
		soa->car_hit_invincibility[soa_index] -= 1;
	}
	if (soa->breakdown_frame_counter[soa_index] > 0){
		soa->breakdown_frame_counter[soa_index] -= 1;
	}

	soa->frames_since_start[soa_index] += 1;

	return ground_normal;
};

float PhysicsCar::get_current_stage_min_y() const
{
	return -100000.0f;
};


void PhysicsCar::broken_down_fling_physics()
{
	// semi-random numbers by hashing positions and angular LOAD_VEC3(velocity)
	uint32_t hash = ((uint32_t)soa->position_current_x[soa_index] ^
		(uint32_t)soa->position_current_y[soa_index] ^
		(uint32_t)soa->position_current_z[soa_index] ^
		(uint32_t)soa->velocity_angular_x[soa_index]) & 0xffff;
	uint32_t hash2 = ((uint32_t)soa->position_current_x[soa_index] ^
		(uint32_t)soa->position_current_y[soa_index] ^
		(uint32_t)soa->position_current_z[soa_index] ^
		(uint32_t)soa->velocity_angular_y[soa_index]) & 0xffff;

	float rand_x = 2.0f * ((float)hash / 65536.0f) - 1.0f;
	float rand_z = 2.0f * ((float)hash2 / 65536.0f) - 1.0f;

	rand_x += (rand_x <= 0.0f) ? -0.5f : 0.5f;
	rand_z += (rand_z <= 0.0f) ? -0.5f : 0.5f;

	float damping_factor = std::clamp((soa->speed_kmh[soa_index] * 0.0015 - 1.0), 0.0, 1.0);
	float force = damping_factor * (450.0 / 216.0) * soa->stat_weight[soa_index];

	// put impulse in world space
	SimVec3 torque_impulse = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(-rand_x, 0.5f, -rand_z));
	torque_impulse *= force;

	soa->state_2[soa_index] |= 2;

	// back into local space
	SimVec3 rotated_torque_impulse = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), torque_impulse);
	rotated_torque_impulse *= 0.5f;

	SimVec3 torque = {
		-(rand_z * rotated_torque_impulse.y),
		-(rand_x * rotated_torque_impulse.z - rand_z * rotated_torque_impulse.x),
		-(-rand_x * rotated_torque_impulse.y)
	};

	// used for random visual rotation of the vehicle when bouncing
	soa->unk_vec3_0x4f0_x[soa_index] = soa->velocity_angular_x[soa_index] + torque.x;
	soa->unk_vec3_0x4f0_y[soa_index] = soa->velocity_angular_y[soa_index] + torque.y;
	soa->unk_vec3_0x4f0_z[soa_index] = soa->velocity_angular_z[soa_index] + torque.z;

	// use previously calculated force for actual bounce
	SimVec3 boost_vec;
	boost_vec = set_vec3_length(LOAD_VEC3(track_surface_normal), force * 0.2);
	ADD_VEC3(velocity, boost_vec);

	if (soa->frames_since_death[soa_index] > 30) {
		soa->state_2[soa_index] |= 0x20;

	// visually orient vehicle back to track once it settles on the road and stops bouncing
	// disabled here because we do this in GDScript instead

	//	SimVec3 up = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0.0f, 1.0f, 0.0f));
	//	if (up.dot(LOAD_VEC3(track_surface_normal)) < 0.99f) {
	//		SimVec3 axis = vec3_cross(-up, LOAD_VEC3(track_surface_normal));
	//		float axis_len = axis.length();
	//		if (axis_len > 0.1f) {
	//			float dot = up.dot(LOAD_VEC3(track_surface_normal));
	//			int angle = (int)(1365.0f * (1.0f - dot * dot));
	//
	//			//make_axis_angle_quat(&q, &axis, (short)angle);
	//		}
	//	}
	}

	if (soa->frames_since_death[soa_index] < 2) {
		soa->frames_since_death[soa_index] = 2;
	} else if (++soa->frames_since_death[soa_index] > 239) {
		soa->frames_since_death[soa_index] = 240;
	}
};

void PhysicsCar::breakdown_physics()
{
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
		broken_down_fling_physics();
	}

	if (soa->frames_since_death[soa_index] < 0x3c) {
		soa->unk_vec3_0x4e4_x[soa_index] += soa->unk_vec3_0x4f0_x[soa_index];
		soa->unk_vec3_0x4e4_y[soa_index] += soa->unk_vec3_0x4f0_y[soa_index];
		soa->unk_vec3_0x4e4_z[soa_index] += soa->unk_vec3_0x4f0_z[soa_index];
	}

	soa->some_breakdown_int[soa_index]++;

	if (soa->some_breakdown_int[soa_index] > 0xef) {
		if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0) {
			soa->velocity_x[soa_index] = 0.0f;
			soa->velocity_y[soa_index] = 0.0f;
			soa->velocity_z[soa_index] = 0.0f;
			STORE_VEC3(position_current, LOAD_VEC3(position_old));
			if ((soa->state_2[soa_index] & 0x90) == 0) {
				soa->state_2[soa_index] = soa->state_2[soa_index] | 0x80;
				soa->state_2[soa_index] = soa->state_2[soa_index] | 0x100;
			}
		}
		soa->some_breakdown_int[soa_index] = 0xf0;
	}
}

bool PhysicsCar::handle_machine_crash(int unk_int) {
	uint32_t state = soa->machine_state[soa_index];
	bool result = false;

	if (!(state & MACHINESTATE::FALLOUT)) {
		if (state & MACHINESTATE::ZEROHP) {
			if (!soa->machine_crashed[soa_index] && !(state & MACHINESTATE::COMPLETEDRACE_1_Q)) {
				soa->machine_crashed[soa_index] = true;
			}
		}

		if ((state & MACHINESTATE::B1) &&
			(state & MACHINESTATE::ZEROHP) &&
			((state & 0x2810000) || unk_int)) {
			result = true;
	}

} else {
	if ((state & MACHINESTATE::B1) || (state & MACHINESTATE::COMPLETEDRACE_1_Q)) {
		result = true;
	}

	if (!(state & MACHINESTATE::COMPLETEDRACE_1_Q) && !soa->machine_crashed[soa_index]) {
		soa->machine_crashed[soa_index] = true;
	}
}

if ((state & MACHINESTATE::DIEDTHISFRAMEOOB_Q) && (state & MACHINESTATE::B1)) {
	result = true;
}

if (state & MACHINESTATE::B29) {
	result = false;
}
return result;
}

void PhysicsCar::handle_machine_damage_and_visuals()
{
	if ((soa->state_2[soa_index] & 0x8u) == 0)
		return;


	if (soa->frames_since_death[soa_index] != 0)
	{
		breakdown_physics();
	}

	if (soa->terrain_state[soa_index] & TERRAIN::LAVA) {
		// Lava damage handling is not yet implemented
		if ((soa->state_2[soa_index] & 0x200u) && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP)) {
			return;
		}
	}

	const int point_base = soa_index * 4;
	sim_store4(soa->tilt_pos_old_x + point_base, sim_load4(soa->tilt_pos_x + point_base));
	sim_store4(soa->tilt_pos_old_y + point_base, sim_load4(soa->tilt_pos_y + point_base));
	sim_store4(soa->tilt_pos_old_z + point_base, sim_load4(soa->tilt_pos_z + point_base));
	const SimVec3x4 tilt_pos = mxt_transform_points4(
		LOAD_TRANSFORM(basis_physical),
		LOAD_VEC3(position_current),
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base) + sim_load4(soa->tilt_force + point_base) - sim_load4(soa->tilt_rest_length + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	sim_store4(soa->tilt_pos_x + point_base, tilt_pos.x);
	sim_store4(soa->tilt_pos_y + point_base, tilt_pos.y);
	sim_store4(soa->tilt_pos_z + point_base, tilt_pos.z);
	if ((soa->state_2[soa_index] & 0x10u) == 0) {
		float y_pos = soa->position_current_y[soa_index];
		float track_min_y = -1000000.0f; // Placeholder until track data is available
		if (y_pos < -5000.0f || y_pos < (track_min_y - 900.0f)) {
			return;
		}
	}

	if (soa->position_current_y[soa_index] < -10000.0f) {
		soa->position_current_y[soa_index] = -10000.0f;
		STORE_VEC3(velocity, SimVec3());
	}

	if ((soa->machine_state[soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) == 0) {
		float world_speed = LOAD_VEC3(velocity).length();
		if (std::abs(soa->stat_weight[soa_index]) > 0.0001f)
			soa->speed_kmh[soa_index] = 216.0f * (world_speed / soa->stat_weight[soa_index]);
		else
			soa->speed_kmh[soa_index] = 0.0f;

		float current_speed_for_max_check = soa->speed_kmh[soa_index];
		bool no_bad_state_flags =
		(soa->machine_state[soa_index] & (MACHINESTATE::JUSTHITVEHICLE_Q | MACHINESTATE::LOWGRIP |
			MACHINESTATE::TOOKDAMAGE)) == 0;

		(void)current_speed_for_max_check;
		(void)no_bad_state_flags;
	}
	bool crashed = handle_machine_crash(1);
	if (crashed == false) {
		if ((soa->machine_state[soa_index] & (MACHINESTATE::RETIRED|MACHINESTATE::B10|MACHINESTATE::B1)) == 0) {
			if (((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) && (soa->frames_since_death[soa_index] == 0)) {
				broken_down_fling_physics();
			}
		}
		else {
			if ((soa->machine_state[soa_index] & (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) == (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) {
				if (10.0f <= soa->speed_kmh[soa_index])
				{
					STORE_VEC3(velocity, LOAD_VEC3(velocity) * 0.95f);
				}else{
					soa->velocity_x[soa_index] = 0.0f;
					soa->velocity_y[soa_index] = 0.0f;
					soa->velocity_z[soa_index] = 0.0f;
					STORE_VEC3(position_current, LOAD_VEC3(position_old));
					soa->machine_state[soa_index] = soa->machine_state[soa_index] | MACHINESTATE::RETIRED;
					if ((soa->state_2[soa_index] & 0x80) == 0) {
						soa->state_2[soa_index] = soa->state_2[soa_index] | 0x100;
					}
					soa->state_2[soa_index] = soa->state_2[soa_index] | 0x80;
				}
			}
		}
	}
};

void PhysicsCar::handle_machine_damage_and_visuals_tail()
{
	if ((soa->state_2[soa_index] & 0x8u) == 0)
		return;

	if (soa->frames_since_death[soa_index] != 0)
	{
		breakdown_physics();
	}

	if (soa->terrain_state[soa_index] & TERRAIN::LAVA) {
		if ((soa->state_2[soa_index] & 0x200u) && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP)) {
			return;
		}
	}

	if ((soa->state_2[soa_index] & 0x10u) == 0) {
		float y_pos = soa->position_current_y[soa_index];
		float track_min_y = -1000000.0f;
		if (y_pos < -5000.0f || y_pos < (track_min_y - 900.0f)) {
			return;
		}
	}

	if (soa->position_current_y[soa_index] < -10000.0f) {
		soa->position_current_y[soa_index] = -10000.0f;
		STORE_VEC3(velocity, SimVec3());
	}

	bool crashed = handle_machine_crash(1);
	if (crashed == false) {
		if ((soa->machine_state[soa_index] & (MACHINESTATE::RETIRED|MACHINESTATE::B10|MACHINESTATE::B1)) == 0) {
			if (((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) && (soa->frames_since_death[soa_index] == 0)) {
				broken_down_fling_physics();
			}
		}
		else {
			if ((soa->machine_state[soa_index] & (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) == (MACHINESTATE::B10|MACHINESTATE::ZEROHP)) {
				if (10.0f <= soa->speed_kmh[soa_index])
				{
					STORE_VEC3(velocity, LOAD_VEC3(velocity) * 0.95f);
				}else{
					soa->velocity_x[soa_index] = 0.0f;
					soa->velocity_y[soa_index] = 0.0f;
					soa->velocity_z[soa_index] = 0.0f;
					STORE_VEC3(position_current, LOAD_VEC3(position_old));
					soa->machine_state[soa_index] = soa->machine_state[soa_index] | MACHINESTATE::RETIRED;
					if ((soa->state_2[soa_index] & 0x80) == 0) {
						soa->state_2[soa_index] = soa->state_2[soa_index] | 0x100;
					}
					soa->state_2[soa_index] = soa->state_2[soa_index] | 0x80;
				}
			}
		}
	}
}

static inline float safe_inverse_floor_scale(float scale)
{
	return fabsf(scale) > 0.00001f ? 1.0f / scale : 0.0f;
}

static bool ray_unit_circle_xy(const SimVec2 &p, const SimVec2 &dir, SimVec2 &out_hit)
{
	const float a = dir.length_squared();
	if (a <= 0.0000001f) {
		return false;
	}
	const float b = 2.0f * (p.x * dir.x + p.y * dir.y);
	const float c = p.length_squared() - 1.0f;
	const float disc = b * b - 4.0f * a * c;
	if (disc < 0.0f) {
		return false;
	}
	const float sqrt_disc = sqrtf(disc);
	const float inv_2a = 0.5f / a;
	const float t0 = (-b - sqrt_disc) * inv_2a;
	const float t1 = (-b + sqrt_disc) * inv_2a;
	float t = FLT_MAX;
	if (t0 >= 0.0f) {
		t = t0;
	}
	if (t1 >= 0.0f && t1 < t) {
		t = t1;
	}
	if (t == FLT_MAX) {
		return false;
	}
	out_hit = p + dir * t;
	return true;
}

static inline void floor_consider_ray_hit(float t, const SimVec2 &hit, float &best_t, SimVec2 &best_hit)
{
	if (t >= 0.0f && t < best_t) {
		best_t = t;
		best_hit = hit;
	}
}

static inline float cross2(const SimVec2 &a, const SimVec2 &b)
{
	return a.x * b.y - a.y * b.x;
}

static bool road_shape_is_open_pipe_or_rect(const RoadShape *shape)
{
	return shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN ||
		shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN;
}

struct PipeFloorTrace
{
	const char *reason = "ok";
	SimVec2 center;
	SimVec2 ray_dir;
	SimVec2 hit;
	float ray_dir_len2 = 0.0f;
	bool open_shape = false;
	bool lip_valid = false;
	bool center_on_road_side = false;
	bool center_in_road_arc = false;
	bool center_on_open_road_side = false;
};

static SimVec2 sample_road_local_xy(const RoadShape *shape, float tx, float ty)
{
	SimVec3 pos;
	SimVec3 dx;
	SimVec3 dy;
	shape->get_local_surface_at_time(pos, dx, dy, SimVec2(tx, ty));
	return SimVec2(pos.x, pos.y);
}

static bool open_road_lip_relation(
	const TrackSegment &segment,
	const SimVec2 &p,
	float ty,
	bool &out_center_on_road_side,
	bool &out_center_in_road_arc)
{
	const RoadShape *shape = segment.road_shape;
	const SimVec2 left = sample_road_local_xy(shape, -1.0f, ty);
	const SimVec2 middle = sample_road_local_xy(shape, 0.0f, ty);
	const SimVec2 right = sample_road_local_xy(shape, 1.0f, ty);
	const SimVec2 lip = right - left;
	if (lip.length_squared() <= 0.000001f) {
		return false;
	}

	const float road_side = cross2(lip, middle - left);
	if (fabsf(road_side) <= 0.000001f) {
		return false;
	}

	SimVec2 center_t;
	SimVec3 center_spatial(p.x, p.y, ty);
	segment.road_shape->find_t_from_relative_pos(center_t, center_spatial);
	const float center_side = cross2(lip, p - left);
	out_center_on_road_side = (center_side * road_side) >= -0.0001f;
	out_center_in_road_arc = center_t.x >= -1.0001f && center_t.x <= 1.0001f;
	return true;
}

static bool ray_rounded_rect_xy(const SimVec2 &p, const SimVec2 &dir, float width, float height, float radius, SimVec2 &out_hit)
{
	const float w2 = fabsf(width) * 0.5f;
	const float h2 = fabsf(height) * 0.5f;
	const float r = fminf(fmaxf(radius, 0.0f), fminf(w2, h2));
	const float inner_w = fmaxf(w2 - r, 0.0f);
	const float inner_h = fmaxf(h2 - r, 0.0f);
	float best_t = FLT_MAX;
	SimVec2 best_hit;

	if (fabsf(dir.x) > 0.0000001f) {
		float t = (w2 - p.x) / dir.x;
		SimVec2 hit = p + dir * t;
		if (hit.y >= -inner_h - 0.0001f && hit.y <= inner_h + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
		t = (-w2 - p.x) / dir.x;
		hit = p + dir * t;
		if (hit.y >= -inner_h - 0.0001f && hit.y <= inner_h + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
	}
	if (fabsf(dir.y) > 0.0000001f) {
		float t = (h2 - p.y) / dir.y;
		SimVec2 hit = p + dir * t;
		if (hit.x >= -inner_w - 0.0001f && hit.x <= inner_w + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
		t = (-h2 - p.y) / dir.y;
		hit = p + dir * t;
		if (hit.x >= -inner_w - 0.0001f && hit.x <= inner_w + 0.0001f) {
			floor_consider_ray_hit(t, hit, best_t, best_hit);
		}
	}
	if (r > 0.0000001f) {
		for (int sx = -1; sx <= 1; sx += 2) {
			for (int sy = -1; sy <= 1; sy += 2) {
				const SimVec2 center(inner_w * static_cast<float>(sx), inner_h * static_cast<float>(sy));
				const SimVec2 rel = p - center;
				const float a = dir.length_squared();
				const float b = 2.0f * (rel.x * dir.x + rel.y * dir.y);
				const float c = rel.length_squared() - r * r;
				const float disc = b * b - 4.0f * a * c;
				if (disc < 0.0f || a <= 0.0000001f) {
					continue;
				}
				const float sqrt_disc = sqrtf(disc);
				const float inv_2a = 0.5f / a;
				const float roots[2] = {
					(-b - sqrt_disc) * inv_2a,
					(-b + sqrt_disc) * inv_2a,
				};
				for (int i = 0; i < 2; ++i) {
					const float t = roots[i];
					const SimVec2 hit = p + dir * t;
					if (static_cast<float>(sx) * (hit.x - center.x) >= -0.0001f &&
						static_cast<float>(sy) * (hit.y - center.y) >= -0.0001f) {
						floor_consider_ray_hit(t, hit, best_t, best_hit);
					}
				}
			}
		}
	}
	if (best_t == FLT_MAX) {
		return false;
	}
	out_hit = best_hit;
	return true;
}

static bool project_machine_down_to_road_cross_section(
	const TrackSegment &segment,
	const RoadTransform &root,
	const SimTransform &machine_transform,
	const SimVec3 &machine_pos,
	SimVec2 &road_t,
	SimVec3 &spatial_t,
	bool &center_on_open_road_side,
	PipeFloorTrace *trace)
{
	center_on_open_road_side = false;
	const SimVec3 root_forward = root.t3d.basis.get_column(2);
	const SimVec3 machine_up = machine_transform.basis.get_column(1);
	const SimVec3 world_down = -machine_up.slide(root_forward);
	if (world_down.length_squared() <= 0.000001f) {
		if (trace) {
			trace->reason = "cross_section_down_degenerate";
		}
		return false;
	}

	const SimVec3 local_pos = root.t3d.xform_inv(machine_pos);
	const SimVec3 local_down_unscaled = root.t3d.basis.xform_inv(world_down);
	const SimVec2 p(
		local_pos.x * safe_inverse_floor_scale(root.scale.x),
		local_pos.y * safe_inverse_floor_scale(root.scale.y));
	const SimVec2 dir(
		local_down_unscaled.x * safe_inverse_floor_scale(root.scale.x),
		local_down_unscaled.y * safe_inverse_floor_scale(root.scale.y));
	if (trace) {
		trace->center = p;
		trace->ray_dir = dir;
		trace->ray_dir_len2 = dir.length_squared();
	}
	if (dir.length_squared() <= 0.000001f) {
		if (trace) {
			trace->reason = "local_ray_degenerate";
		}
		return false;
	}

	if (road_shape_is_open_pipe_or_rect(segment.road_shape)) {
		bool center_on_road_side = false;
		bool center_in_road_arc = false;
		if (trace) {
			trace->open_shape = true;
		}
		if (open_road_lip_relation(segment, p, road_t.y, center_on_road_side, center_in_road_arc)) {
			if (trace) {
				trace->lip_valid = true;
				trace->center_on_road_side = center_on_road_side;
				trace->center_in_road_arc = center_in_road_arc;
			}
			if (!center_on_road_side && !center_in_road_arc) {
				if (trace) {
					trace->reason = "open_hole_side_outside_arc";
				}
				return false;
			}
			center_on_open_road_side = center_on_road_side && center_in_road_arc;
			if (trace) {
				trace->center_on_open_road_side = center_on_open_road_side;
			}
		}
	}

	SimVec2 hit;
	switch (segment.road_shape->shape_type) {
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN:
			if (!ray_unit_circle_xy(p, dir, hit)) {
				if (trace) {
					trace->reason = "pipe_ray_miss";
				}
				return false;
			}
			break;
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT:
		case ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN: {
			RoadShapeRoundedRect *rect = static_cast<RoadShapeRoundedRect *>(segment.road_shape);
			if (!ray_rounded_rect_xy(
					p,
					dir,
					rect->width->sample(road_t.y),
					rect->height->sample(road_t.y),
					rect->radius->sample(road_t.y),
					hit)) {
				if (trace) {
					trace->reason = "rounded_rect_ray_miss";
				}
				return false;
			}
			break;
		}
		default:
			if (trace) {
				trace->reason = "unsupported_shape";
			}
			return false;
	}

	if (trace) {
		trace->hit = hit;
	}
	spatial_t.x = hit.x;
	spatial_t.y = hit.y;
	segment.road_shape->find_t_from_relative_pos(road_t, spatial_t);
	return true;
}

void PhysicsCar::sample_mesh_floor_with_seed(CollisionData &out_collision, const SimVec3 &point, float max_distance, uint8_t mask, int start_idx, bool allow_global_fallback, TrackQueryScratch &scratch, bool build_surface, bool build_surface_basis, bool prime_from_scratch_candidates)
{
	RaceTrack *track = soa->current_track[soa_index];
	if (!track) {
		out_collision.collided = false;
		out_collision.road_data.cp_idx = -1;
		out_collision.mesh_triangle_index = -1;
		return;
	}
	track->sample_mesh_floor_fast(
		out_collision,
		point,
		max_distance,
		mask,
		start_idx,
		allow_global_fallback,
		&scratch,
		soa->last_mesh_floor_triangle[soa_index],
		build_surface,
		build_surface_basis,
		prime_from_scratch_candidates);
	if (out_collision.collided) {
		if (out_collision.mesh_triangle_index < 0) {
			godot::UtilityFunctions::printerr(godot::String("MXT mesh floor sample produced no triangle index"));
			std::abort();
		}
		soa->last_mesh_floor_triangle[soa_index] = out_collision.mesh_triangle_index;
	}
}

bool PhysicsCar::find_floor_beneath_machine(TrackQueryScratch &scratch, PhysicsCarFloorProfile* profile)
{
	uint64_t profile_step = profile ? physics_profile_now_us() : 0;
	soa->road_sample[soa_index].terrain = 0;
	soa->road_sample[soa_index].cp_idx = -1;
	soa->road_sample[soa_index].road_t = SimVec2();
	soa->road_sample[soa_index].spatial_t = SimVec3();
	soa->road_sample[soa_index].closest_surface = SimTransform();
	soa->road_sample[soa_index].closest_root = RoadTransform();
	RaceTrack* track = soa->current_track[soa_index];
	const int current_cp = soa->current_checkpoint[soa_index];
	const SimTransform machine_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 current_position = LOAD_VEC3(position_current);
	bool stay_on = false;
	bool cylinder = false;
	bool pipe = false;
	TrackSegment *floor_seg = &track->segments[track->checkpoints[current_cp].road_segment];
        cylinder = floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER ||
                floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN;
        bool rect = (floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT || floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_ROUNDED_RECT_OPEN);
        pipe = floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE || floor_seg->road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_PIPE_OPEN;
	stay_on = floor_seg->analytic_collision_enabled && (pipe || cylinder || rect);
	const bool trace_mesh_floor = trace_mesh_floor_for_car(soa, soa_index);
	const SimVec3 machine_up_ws = mxt_basis_rotate(machine_transform, SimVec3(0.0f, 1.0f, 0.0f));
	auto reject_backfacing_mesh_floor_hit = [&](CollisionData &mesh_hit) {
		if (mesh_hit.collided && mesh_hit.collision_normal.dot(machine_up_ws) <= 0.0f) {
			mesh_hit.collided = false;
		}
	};
	if ((soa->machine_state[soa_index] & MACHINESTATE::FALLOUT) == 0 &&
		floor_seg->analytic_collision_enabled &&
		road_shape_uses_below_centerline_fallout(floor_seg->road_shape) &&
		analytic_road_world_below_centerline_fallout(*track, current_cp, *floor_seg, current_position)) {
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		trigger_mesh_fallout();
		return false;
	}

	if (!stay_on)
	{
		bool sweep_hit_occurred = false;
		bool nearest_mesh_sample = false;
		bool local_mesh_sample = false;
		CollisionData hit{};
		hit.road_data.cp_idx = -1;
		hit.mesh_triangle_index = -1;
		SimVec3 p0_sweep_start_ws = mxt_transform_point(machine_transform, current_position, SimVec3(0.0f, 1.0f, 0.0f));
		SimVec3 p1_sweep_end_ws = mxt_transform_point(machine_transform, current_position, SimVec3(0.0f, -20.0f, 0.0f));
		STORE_VEC3(position_bottom, p1_sweep_end_ws);
		if (floor_seg->analytic_collision_enabled && (floor_seg->road_shape->embed_terrain_mask & TERRAIN::HOLE) != 0u) {
			SimVec2 center_road_t;
			SimVec3 center_spatial_t;
			RoadTransform center_root;
			track->convert_point_to_road(
				current_cp,
				current_position,
				center_road_t,
				center_spatial_t,
				nullptr,
				&center_root,
				nullptr);
			if (center_road_t.x != -1000.0f &&
				center_road_t.x >= -1.01f && center_road_t.x <= 1.01f &&
				track_segment_longitudinal_t_in_domain(center_road_t.y) &&
				track->analytic_road_sample_has_hole(current_cp, center_road_t)) {
				soa->road_sample[soa_index].terrain = TERRAIN::HOLE;
				soa->road_sample[soa_index].cp_idx = static_cast<int16_t>(current_cp);
				soa->road_sample[soa_index].road_t = center_road_t;
				soa->road_sample[soa_index].spatial_t = center_spatial_t;
				soa->road_sample[soa_index].closest_root = center_root;
				STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
				soa->height_above_track[soa_index] = 0.0f;
				mark_floor_disconnected(soa, soa_index);
				return false;
			}
			physics_profile_mark(profile ? profile->find_floor_analytic_us : nullptr, profile_step);
		}
		if (floor_seg->analytic_collision_enabled) {
			track->cast_vs_track_fast(hit, p0_sweep_start_ws,
				LOAD_VEC3(position_bottom),
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::WANTS_TERRAIN | CAST_FLAGS::SAMPLE_FROM_P0,
				current_cp,
				&scratch);
			if (hit.collided && hit.collision_normal.dot(machine_up_ws) <= 0.0f) {
				hit.collided = false;
			}
			sweep_hit_occurred = hit.collided && hit.road_data.road_t.x >= -1.0f && hit.road_data.road_t.x <= 1.0f && track_segment_longitudinal_t_in_domain(hit.road_data.road_t.y);
			physics_profile_mark(profile ? profile->find_floor_cast_us : nullptr, profile_step);
		} else {
			CollisionData nearest_hit{};
			sample_mesh_floor_with_seed(
				nearest_hit,
				current_position,
				8.0f,
				CAST_FLAGS::WANTS_TRACK,
				current_cp,
				false,
				scratch,
				true,
				false,
				true);
			physics_profile_mark(profile ? profile->find_floor_mesh_us : nullptr, profile_step);
			reject_backfacing_mesh_floor_hit(nearest_hit);
			if (nearest_hit.collided) {
				hit = nearest_hit;
				sweep_hit_occurred = true;
				nearest_mesh_sample = true;
				local_mesh_sample = true;
			}
		}
		if (!sweep_hit_occurred && floor_seg->analytic_collision_enabled) {
			CollisionData mesh_hit{};
			sample_mesh_floor_with_seed(
				mesh_hit,
				current_position,
				8.0f,
				CAST_FLAGS::WANTS_TRACK,
				current_cp,
				true,
				scratch,
				true,
				false,
				true);
			physics_profile_mark(profile ? profile->find_floor_mesh_us : nullptr, profile_step);
			reject_backfacing_mesh_floor_hit(mesh_hit);
			if (mesh_hit.collided) {
				hit = mesh_hit;
				sweep_hit_occurred = true;
				nearest_mesh_sample = true;
				local_mesh_sample = false;
			}
		}
		if (!sweep_hit_occurred && !floor_seg->analytic_collision_enabled) {
			track->cast_vs_track_fast(hit, p0_sweep_start_ws,
				LOAD_VEC3(position_bottom),
				CAST_FLAGS::WANTS_TRACK | CAST_FLAGS::WANTS_BACKFACE | CAST_FLAGS::SAMPLE_FROM_P0,
				current_cp,
				&scratch);
			if (hit.collided && hit.collision_normal.dot(machine_up_ws) <= 0.0f) {
				hit.collided = false;
			}
			sweep_hit_occurred = hit.collided && hit.road_data.road_t.x >= -1.0f && hit.road_data.road_t.x <= 1.0f && track_segment_longitudinal_t_in_domain(hit.road_data.road_t.y);
			physics_profile_mark(profile ? profile->find_floor_cast_us : nullptr, profile_step);
		}
		if (!sweep_hit_occurred && !floor_seg->analytic_collision_enabled) {
			sample_mesh_floor_with_seed(
				hit,
				current_position,
				8.0f,
				CAST_FLAGS::WANTS_TRACK,
				current_cp,
				true,
				scratch,
				true,
				false,
				true);
			sweep_hit_occurred = hit.collided;
			nearest_mesh_sample = hit.collided;
			physics_profile_mark(profile ? profile->find_floor_mesh_us : nullptr, profile_step);
		}
		if (!floor_seg->analytic_collision_enabled) {
			reject_backfacing_mesh_floor_hit(hit);
			sweep_hit_occurred = sweep_hit_occurred && hit.collided;
		}
		soa->road_sample[soa_index].terrain = hit.road_data.terrain;
		soa->road_sample[soa_index].cp_idx = hit.road_data.cp_idx;
		soa->road_sample[soa_index].road_t = hit.road_data.road_t;
		soa->road_sample[soa_index].spatial_t = hit.road_data.spatial_t;
		soa->road_sample[soa_index].closest_surface = hit.road_data.closest_surface;
		float contact_dist_metric = 0.0f;
		if (sweep_hit_occurred) {
			STORE_VEC3(track_surface_pos, hit.collision_point);
			if ((hit.road_data.terrain & TERRAIN::HOLE) != 0u ||
				(hit.road_data.cp_idx >= 0 && track->analytic_road_sample_has_hole(hit.road_data.cp_idx, hit.road_data.road_t))) {
				soa->road_sample[soa_index].terrain |= TERRAIN::HOLE;
				STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
				STORE_VEC3(position_bottom, p1_sweep_end_ws);
				soa->height_above_track[soa_index] = 0.0f;
				mark_floor_disconnected(soa, soa_index);
				return false;
			}
			if (nearest_mesh_sample) {
				const float signed_surface_distance = (current_position - hit.collision_point).dot(hit.collision_normal);
				contact_dist_metric = local_mesh_sample ? 20.0f - fabsf(signed_surface_distance) : 20.0f - signed_surface_distance;
			} else {
				float dist_p0_to_surface =
				current_position.distance_to(hit.collision_point);
				contact_dist_metric = 20.0f - dist_p0_to_surface;
			}
		}
		if (nearest_mesh_sample && !local_mesh_sample && contact_dist_metric > 20.1f) {
			sweep_hit_occurred = false;
		}
		if (trace_mesh_floor) {
			const SimVec3 pos = current_position;
			godot::UtilityFunctions::print(
				godot::String("MXT_MESH_FLOOR_CENTER tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
				godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
				godot::String(" analytic="), floor_seg->analytic_collision_enabled,
				godot::String(" stay_on="), stay_on,
				godot::String(" sweep="), sweep_hit_occurred,
				godot::String(" nearest="), nearest_mesh_sample,
				godot::String(" local="), local_mesh_sample,
				godot::String(" hit="), hit.collided,
				godot::String(" mesh_tri="), static_cast<int64_t>(hit.mesh_triangle_index),
				godot::String(" contact="), contact_dist_metric,
				godot::String(" terrain=0x"), godot::String::num_int64(hit.road_data.terrain, 16),
				godot::String(" pos=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"),
				godot::String(" n=("), hit.collision_normal.x, godot::String(","), hit.collision_normal.y, godot::String(","), hit.collision_normal.z, godot::String(")"),
				godot::String(" road_t=("), hit.road_data.road_t.x, godot::String(","), hit.road_data.road_t.y, godot::String(")"));
		}
		if (sweep_hit_occurred && contact_dist_metric > 0.0f) {
			STORE_VEC3(track_surface_normal, hit.collision_normal);
			soa->height_above_track[soa_index] = contact_dist_metric;
			return true;
		} else {
			STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
			STORE_VEC3(position_bottom, p1_sweep_end_ws);
			soa->height_above_track[soa_index] = 0.0f;
			mark_floor_disconnected(soa, soa_index);
			return false;
		}
	}

	SimVec2 road_t_sample_raw;
	SimVec3 spatial_t_sample;

	RoadTransform root;
	RoadTransform root_derivative;
	const TrackSegment &segment     = track->segments[track->checkpoints[current_cp].road_segment];
	const bool trace_pipe_floor =
		DEBUG::dip_enabled(DIP_SWITCH::DIP_TRACE_PIPE_FLOOR) &&
		(soa->global_start + soa_index) == 0 &&
		(pipe || rect);
	auto trace_floor = [&](const char *reason, const PipeFloorTrace *trace, const SimVec2 &road_t, const SimVec3 &spatial_t, float surface_dist) {
		if (!trace_pipe_floor) {
			return;
		}
		godot::UtilityFunctions::print(
			godot::String("MXT_PIPE_FLOOR reason="), godot::String(reason),
			godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
			godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
			godot::String(" shape="), static_cast<int64_t>(segment.road_shape->shape_type),
			godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(soa->machine_state[soa_index]), 16),
			godot::String(" road_t=("), road_t.x, godot::String(","), road_t.y, godot::String(")"),
			godot::String(" spatial=("), spatial_t.x, godot::String(","), spatial_t.y, godot::String(","), spatial_t.z, godot::String(")"),
			godot::String(" pos=("), soa->position_current_x[soa_index], godot::String(","), soa->position_current_y[soa_index], godot::String(","), soa->position_current_z[soa_index], godot::String(")"),
			godot::String(" vel=("), soa->velocity_x[soa_index], godot::String(","), soa->velocity_y[soa_index], godot::String(","), soa->velocity_z[soa_index], godot::String(")"),
			godot::String(" surf_dist="), surface_dist,
			godot::String(" h="), soa->height_above_track[soa_index],
			godot::String(" push_track_len="), LOAD_VEC3(collision_push_track).length(),
			godot::String(" push_rail_len="), LOAD_VEC3(collision_push_rail).length(),
			godot::String(" trace_reason="), godot::String(trace ? trace->reason : "none"),
			godot::String(" ray_len2="), trace ? trace->ray_dir_len2 : 0.0f,
			godot::String(" center=("), trace ? trace->center.x : 0.0f, godot::String(","), trace ? trace->center.y : 0.0f, godot::String(")"),
			godot::String(" ray=("), trace ? trace->ray_dir.x : 0.0f, godot::String(","), trace ? trace->ray_dir.y : 0.0f, godot::String(")"),
			godot::String(" hit=("), trace ? trace->hit.x : 0.0f, godot::String(","), trace ? trace->hit.y : 0.0f, godot::String(")"),
			godot::String(" lip_valid="), trace ? trace->lip_valid : false,
			godot::String(" road_side="), trace ? trace->center_on_road_side : false,
			godot::String(" in_arc="), trace ? trace->center_in_road_arc : false,
			godot::String(" on_open_road="), trace ? trace->center_on_open_road_side : false);
	};
	auto try_stay_on_mesh_floor = [&](const char *reason) {
		CollisionData mesh_hit{};
		mesh_hit.road_data.cp_idx = -1;
		mesh_hit.mesh_triangle_index = -1;
		sample_mesh_floor_with_seed(
			mesh_hit,
			current_position,
			8.0f,
			CAST_FLAGS::WANTS_TRACK,
			current_cp,
			true,
			scratch,
			true,
			false,
			true);
		physics_profile_mark(profile ? profile->find_floor_mesh_us : nullptr, profile_step);
		reject_backfacing_mesh_floor_hit(mesh_hit);
		float contact_dist_metric = 0.0f;
		if (mesh_hit.collided) {
			const float signed_surface_distance = (current_position - mesh_hit.collision_point).dot(mesh_hit.collision_normal);
			contact_dist_metric = 20.0f - fabsf(signed_surface_distance);
		}
		if (trace_mesh_floor) {
			const SimVec3 pos = current_position;
			godot::UtilityFunctions::print(
				godot::String("MXT_MESH_FLOOR_CENTER tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
				godot::String(" coll_cp="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
				godot::String(" analytic="), floor_seg->analytic_collision_enabled,
				godot::String(" stay_on="), stay_on,
				godot::String(" reason="), godot::String(reason),
				godot::String(" sweep="), mesh_hit.collided,
				godot::String(" nearest="), mesh_hit.collided,
				godot::String(" local="), mesh_hit.collided,
				godot::String(" hit="), mesh_hit.collided,
				godot::String(" mesh_tri="), static_cast<int64_t>(mesh_hit.mesh_triangle_index),
				godot::String(" contact="), contact_dist_metric,
				godot::String(" terrain=0x"), godot::String::num_int64(mesh_hit.road_data.terrain, 16),
				godot::String(" pos=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"),
				godot::String(" n=("), mesh_hit.collision_normal.x, godot::String(","), mesh_hit.collision_normal.y, godot::String(","), mesh_hit.collision_normal.z, godot::String(")"),
				godot::String(" road_t=("), mesh_hit.road_data.road_t.x, godot::String(","), mesh_hit.road_data.road_t.y, godot::String(")"));
		}
		if (!mesh_hit.collided || contact_dist_metric <= 0.0f) {
			return false;
		}
		if ((mesh_hit.road_data.terrain & TERRAIN::HOLE) != 0u ||
			(mesh_hit.road_data.cp_idx >= 0 && track->analytic_road_sample_has_hole(mesh_hit.road_data.cp_idx, mesh_hit.road_data.road_t))) {
			return false;
		}
		soa->road_sample[soa_index].terrain = mesh_hit.road_data.terrain;
		soa->road_sample[soa_index].cp_idx = mesh_hit.road_data.cp_idx;
		soa->road_sample[soa_index].road_t = mesh_hit.road_data.road_t;
		soa->road_sample[soa_index].spatial_t = mesh_hit.road_data.spatial_t;
		soa->road_sample[soa_index].closest_surface = mesh_hit.road_data.closest_surface;
		STORE_VEC3(track_surface_pos, mesh_hit.collision_point);
		STORE_VEC3(track_surface_normal, mesh_hit.collision_normal);
		soa->height_above_track[soa_index] = contact_dist_metric;
		return true;
	};
	track->convert_point_to_road(
		current_cp,
		current_position,
		road_t_sample_raw,
		spatial_t_sample,
		nullptr,
		&root,
		&root_derivative);
	physics_profile_mark(profile ? profile->find_floor_analytic_us : nullptr, profile_step);
	soa->road_sample[soa_index].cp_idx = static_cast<int16_t>(current_cp);
	soa->road_sample[soa_index].road_t = road_t_sample_raw;
	soa->road_sample[soa_index].spatial_t = spatial_t_sample;
	SimTransform surf;
	bool root_sampled = road_t_sample_raw.x != -1000.0f;
	bool center_on_open_road_side = false;
	auto sample_root = [&]() {
		if (!root_sampled) {
			segment.curve_matrix->sample_with_derivative(root, root_derivative, road_t_sample_raw.y);
			root_sampled = true;
		}
	};

	//if (cylinder || pipe)
	//{
	//}
	PipeFloorTrace pipe_trace;
	PipeFloorTrace *pipe_trace_ptr = trace_pipe_floor ? &pipe_trace : nullptr;
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0)
	{
		if(rect || pipe)
		{
			sample_root();
			if (!project_machine_down_to_road_cross_section(
				segment,
				root,
				machine_transform,
				current_position,
				road_t_sample_raw,
				spatial_t_sample,
				center_on_open_road_side,
				pipe_trace_ptr)) {
				trace_floor("orientation_ray_reject", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, 0.0f);
				if (try_stay_on_mesh_floor("orientation_ray_reject")) {
					return true;
				}
				soa->height_above_track[soa_index] = 0.0f;
				STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
				mark_floor_disconnected(soa, soa_index);
				return false;
			}
		}
	}
	if (root_sampled) {
		soa->road_sample[soa_index].closest_root = root;
	}
	if (road_t_sample_raw.x == -1000.0)
	{
		trace_floor("invalid_road_t", nullptr, road_t_sample_raw, spatial_t_sample, 0.0f);
		if (try_stay_on_mesh_floor("invalid_road_t")) {
			return true;
		}
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		return false;
	}

	if (road_t_sample_raw.x > 1.01f || road_t_sample_raw.x < -1.01f || !track_segment_longitudinal_t_in_domain(road_t_sample_raw.y))
	{
		trace_floor("road_t_bounds", nullptr, road_t_sample_raw, spatial_t_sample, 0.0f);
		if (try_stay_on_mesh_floor("road_t_bounds")) {
			return true;
		}
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		return false;
	}
	if (track->analytic_road_sample_has_hole(current_cp, road_t_sample_raw)) {
		trace_floor("hole_embed", nullptr, road_t_sample_raw, spatial_t_sample, 0.0f);
		soa->road_sample[soa_index].terrain |= TERRAIN::HOLE;
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		return false;
	}
	segment.road_shape->get_oriented_transform_at_time_presampled(surf, road_t_sample_raw, root, root_derivative);
	physics_profile_mark(profile ? profile->find_floor_analytic_us : nullptr, profile_step);
	const float surface_dist = (current_position - surf.origin).dot(surf.basis[1]);
	if (surf.basis[1].dot(machine_up_ws) <= 0.0f) {
		trace_floor("surface_faces_away", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, surface_dist);
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		return false;
	}
	if (segment.road_shape->shape_type == ROAD_SHAPE_TYPE::ROAD_SHAPE_CYLINDER_OPEN &&
		surface_dist >= 20.0f) {
		trace_floor("cylinder_distance_cap", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, surface_dist);
		if (try_stay_on_mesh_floor("cylinder_distance_cap")) {
			return true;
		}
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		return false;
	}
	if (center_on_open_road_side && surface_dist < -0.001f) {
		trace_floor("open_center_below_surface", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, surface_dist);
		if (try_stay_on_mesh_floor("open_center_below_surface")) {
			return true;
		}
		soa->height_above_track[soa_index] = 0.0f;
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		mark_floor_disconnected(soa, soa_index);
		return false;
	}
	STORE_VEC3(track_surface_normal, surf.basis[1]);
	soa->height_above_track[soa_index] = fmaxf(1.0f, 20.0f - surface_dist);
	soa->road_sample[soa_index].road_t = road_t_sample_raw;
	soa->road_sample[soa_index].spatial_t = spatial_t_sample;
	soa->road_sample[soa_index].closest_surface = surf;

	//godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
	//dd3d->call("draw_arrow", surf.origin, surf.origin + LOAD_VEC3(track_surface_normal) * 40.0f, godot::Color(1.0f, 1.0f, 1.0f), 0.125, true, _TICK_DELTA);
	//DEBUG::disp_text("soa->height_above_track[soa_index]", soa->height_above_track[soa_index]);

	if (soa->height_above_track[soa_index] > 20.1f)
	{
		trace_floor("height_above_track_too_large", pipe_trace_ptr, road_t_sample_raw, spatial_t_sample, surface_dist);
		if (try_stay_on_mesh_floor("height_above_track_too_large")) {
			return true;
		}
		STORE_VEC3(track_surface_normal, SimVec3(0, 1, 0));
		soa->height_above_track[soa_index] = 0.0f;
		mark_floor_disconnected(soa, soa_index);
		return false;
	}
	return true;
};
