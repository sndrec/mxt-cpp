#include "physics_car_internal.h"

void PhysicsCar::align_machine_y_with_track_normal_immediate()
{
	if (LOAD_VEC3(track_surface_normal).length_squared() < 0.0001f)
		return;

	SimVec3 safe_track_normal = LOAD_VEC3(track_surface_normal).normalized();
	SimVec3 machine_current_world_up = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), SimVec3(0, 1, 0));

	if (machine_current_world_up.length_squared() < 0.0001f)
		return;

	SimVec3 safe_machine_world_up = machine_current_world_up.normalized();

	SimQuat delta_rotation_q = SimQuat(safe_machine_world_up, safe_track_normal);
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = SimBasis(delta_rotation_q) * mxt_tmp.basis; STORE_TRANSFORM(basis_physical, mxt_tmp); }
};

void PhysicsCar::handle_checkpoints(TrackQueryScratch &scratch)
{
	if (!soa->current_track[soa_index] || soa->current_track[soa_index]->num_checkpoints == 0)
		return;

	RaceTrack *track = soa->current_track[soa_index];
	uint32_t prev_lap = soa->lap[soa_index];
	const bool trace_restore = trace_rail_for_car(soa, soa_index);
	// Floor depenetration may resynchronize current_checkpoint before lap processing, so retain its pre-contact value for transition direction.
	const int checkpoint_before_floor_contact = soa->checkpoint_before_floor_contact[soa_index];
	soa->checkpoint_before_floor_contact[soa_index] = -1;
	const int old_cp = checkpoint_before_floor_contact >= 0
		? checkpoint_before_floor_contact
		: soa->current_checkpoint[soa_index];
	const int old_coll_cp = soa->current_collision_checkpoint[soa_index];
	const float old_fraction = soa->checkpoint_fraction[soa_index];
	const float old_track_distance = soa->checkpoint_track_distance[soa_index];
	const float old_previous_lap_distance = soa->previous_lap_distance[soa_index];
	const float old_lap_progress = soa->lap_progress[soa_index];
	const uint32_t old_machine_state = soa->machine_state[soa_index];
	const bool old_broken_lap_rollback_pending = soa->broken_lap_rollback_pending[soa_index];
	const uint32_t old_broken_lap_rollback_lap = soa->broken_lap_rollback_lap[soa_index];

	int found = track->get_best_checkpoint(LOAD_VEC3(position_current), soa->current_checkpoint[soa_index], scratch);
	int collision = found;
	if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) != 0 || found == -1)
	{
		collision = track->get_best_checkpoint(LOAD_VEC3(position_current), scratch);
	}
	//if ((soa->machine_state[soa_index] & MACHINESTATE::AIRBORNE) == 0 && found == -1)
	//{
	//	found = collision;
	//}
	soa->current_collision_checkpoint[soa_index] = static_cast<int16_t>(collision);
	if (found >= 0 && found < track->num_checkpoints && found != old_cp) {
		uint32_t proposed_lap = soa->lap[soa_index];
		int lap_delta = 0;
		const int lap_line_window = std::max(1, track->num_checkpoints / 8);
		const bool found_after_lap_line = found < lap_line_window;
		const bool found_before_lap_line = found >= track->num_checkpoints - lap_line_window;
		const bool current_after_lap_line = old_cp < lap_line_window;
		const bool current_before_lap_line = old_cp >= track->num_checkpoints - lap_line_window;
		if ((soa->machine_state[soa_index] & MACHINESTATE::ACTIVE) != 0 && (soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0)
		{
			if (found_after_lap_line && current_before_lap_line) {
				proposed_lap += 1;
				lap_delta = 1;
			} else if (current_after_lap_line && found_before_lap_line) {
				if (proposed_lap > 0) {
					proposed_lap -= 1;
					lap_delta = -1;
				}
			}
		}

		soa->lap[soa_index] = proposed_lap;
		soa->current_checkpoint[soa_index] = static_cast<uint16_t>(found);
		if (lap_delta < 0) {
			soa->broken_lap_rollback_pending[soa_index] = false;
			soa->broken_lap_rollback_lap[soa_index] = 0;
		}
		const uint32_t target_lap = soa->race_lap_target[soa_index];
		if (lap_delta > 0 && (target_lap == 0 || proposed_lap <= target_lap)) {
			const uint32_t unsafe_lap_cross_state = MACHINESTATE::AIRBORNE |
				MACHINESTATE::FALLOUT |
				MACHINESTATE::ZEROHP;
			if ((soa->machine_state[soa_index] & unsafe_lap_cross_state) != 0) {
				soa->broken_lap_rollback_pending[soa_index] = true;
				soa->broken_lap_rollback_lap[soa_index] = proposed_lap;
			} else {
				soa->broken_lap_rollback_pending[soa_index] = false;
				soa->broken_lap_rollback_lap[soa_index] = 0;
			}
		} else if (lap_delta > 0) {
			soa->broken_lap_rollback_pending[soa_index] = false;
			soa->broken_lap_rollback_lap[soa_index] = 0;
		}
	}

	const CollisionCheckpoint &cur_cp = track->checkpoints[soa->current_checkpoint[soa_index]];
	float t = checkpoint_plane_fraction_unclamped(cur_cp, LOAD_VEC3(position_current));
	soa->checkpoint_fraction[soa_index] = t;
	soa->lap_progress[soa_index] = (static_cast<float>(soa->current_checkpoint[soa_index]) + t) / static_cast<float>(track->num_checkpoints);

	float cp_length = cur_cp.local_distance;
	float cp_start_distance = cur_cp.distance - cur_cp.local_distance;
	float ground_distance = cp_start_distance + cp_length * std::clamp(soa->checkpoint_fraction[soa_index], 0.0f, 1.0f);
	float lap_length = track_lap_length(track);
	if (lap_length > 0.0f) {
		ground_distance = std::fmod(ground_distance, lap_length);
		if (ground_distance < 0.0f)
			ground_distance += lap_length;
	}
	soa->checkpoint_track_distance[soa_index] = ground_distance;
	const bool safe_grounded_after_lap_line =
		soa->broken_lap_rollback_pending[soa_index] &&
		soa->lap[soa_index] == soa->broken_lap_rollback_lap[soa_index] &&
		(soa->machine_state[soa_index] & (MACHINESTATE::AIRBORNE | MACHINESTATE::FALLOUT | MACHINESTATE::ZEROHP)) == 0 &&
		!track_distance_is_before_lap_line(ground_distance, lap_length);
	if (safe_grounded_after_lap_line) {
		soa->broken_lap_rollback_pending[soa_index] = false;
		soa->broken_lap_rollback_lap[soa_index] = 0;
	}

	const float current_lap_distance = track->compute_lap_distance(
		soa->current_checkpoint[soa_index],
		soa->checkpoint_fraction[soa_index],
		soa->lap[soa_index]);
	const float checkpoint_advance = current_lap_distance - soa->previous_lap_distance[soa_index];
	auto trace_checkpoint_graph = [&](const char *label, int cp_idx) {
		if (cp_idx < 0 || cp_idx >= track->num_checkpoints) {
			return;
		}
		const CollisionCheckpoint &cp = track->checkpoints[cp_idx];
		const int branch_id = (cp_idx < static_cast<int>(track->checkpoint_branch_id.size())) ? track->checkpoint_branch_id[cp_idx] : -1;
		const int canonical_next = (cp_idx < static_cast<int>(track->canonical_next.size())) ? track->canonical_next[cp_idx] : -1;
		const int canonical_prev = (cp_idx < static_cast<int>(track->canonical_prev.size())) ? track->canonical_prev[cp_idx] : -1;
		godot::String neighbors;
		for (int i = 0; i < cp.num_neighboring_checkpoints; ++i) {
			if (i > 0) {
				neighbors += godot::String(",");
			}
			neighbors += godot::String::num_int64(cp.neighboring_checkpoints[i]);
		}
		godot::UtilityFunctions::print(
			godot::String("MXT_CHECKPOINT_GRAPH_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" label="), godot::String(label),
			godot::String(" cp="), static_cast<int64_t>(cp_idx),
			godot::String(" distance="), cp.distance,
			godot::String(" local="), cp.local_distance,
			godot::String(" start_distance="), cp.distance - cp.local_distance,
			godot::String(" segment="), static_cast<int64_t>(cp.road_segment),
			godot::String(" branch_id="), static_cast<int64_t>(branch_id),
			godot::String(" canonical_prev="), static_cast<int64_t>(canonical_prev),
			godot::String(" canonical_next="), static_cast<int64_t>(canonical_next),
			godot::String(" neighbors="), neighbors);
	};
	if (trace_restore) {
		const SimVec3 pos = LOAD_VEC3(position_current);
		godot::UtilityFunctions::print(
			godot::String("MXT_CHECKPOINT_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" old_cp="), static_cast<int64_t>(old_cp),
			godot::String(" old_coll="), static_cast<int64_t>(old_coll_cp),
			godot::String(" found="), static_cast<int64_t>(found),
			godot::String(" collision="), static_cast<int64_t>(collision),
			godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
			godot::String(" coll="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
			godot::String(" old_frac="), old_fraction,
			godot::String(" frac="), soa->checkpoint_fraction[soa_index],
			godot::String(" old_track_dist="), old_track_distance,
			godot::String(" track_dist="), soa->checkpoint_track_distance[soa_index],
			godot::String(" prev_lap_dist_old="), old_previous_lap_distance,
			godot::String(" lap_dist="), current_lap_distance,
			godot::String(" advance="), checkpoint_advance,
			godot::String(" threshold="), kMaxPositiveCheckpointAdvance,
			godot::String(" lap="), static_cast<int64_t>(soa->lap[soa_index]),
			godot::String(" state_old=0x"), godot::String::num_int64(static_cast<int64_t>(old_machine_state), 16),
			godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(soa->machine_state[soa_index]), 16),
			godot::String(" restore="), static_cast<int64_t>(soa->restore_state[soa_index]),
			godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
			godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index],
			godot::String(" pos=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"));
	}
	if (soa->restore_state[soa_index] == 0 &&
		(soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0) {
		if (checkpoint_advance > kMaxPositiveCheckpointAdvance) {
			if (trace_restore) {
				godot::UtilityFunctions::print(
					godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
					godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
					godot::String(" event=shortcut_advance"),
					godot::String(" advance="), checkpoint_advance,
					godot::String(" prev_lap_dist="), soa->previous_lap_distance[soa_index],
					godot::String(" lap_dist="), current_lap_distance,
					godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
					godot::String(" old_cp="), static_cast<int64_t>(old_cp),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
					godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index],
					godot::String(" lap_length="), track_lap_length(track),
					godot::String(" num_checkpoints="), static_cast<int64_t>(track->num_checkpoints),
					godot::String(" canonical_start="), static_cast<int64_t>(track->canonical_start_index),
					godot::String(" branch_count="), static_cast<int64_t>(track->branch_infos.size()));
				trace_checkpoint_graph("old", old_cp);
				trace_checkpoint_graph("old_prev_linear", old_cp - 1);
				trace_checkpoint_graph("old_next_linear", old_cp + 1);
				trace_checkpoint_graph("current", soa->current_checkpoint[soa_index]);
				trace_checkpoint_graph("collision", soa->current_collision_checkpoint[soa_index]);
				if (soa->current_checkpoint[soa_index] >= 0 &&
					soa->current_checkpoint[soa_index] < static_cast<int>(track->canonical_prev.size())) {
					trace_checkpoint_graph("current_canonical_prev", track->canonical_prev[soa->current_checkpoint[soa_index]]);
				}
				if (soa->current_checkpoint[soa_index] >= 0 &&
					soa->current_checkpoint[soa_index] < static_cast<int>(track->canonical_next.size())) {
					trace_checkpoint_graph("current_canonical_next", track->canonical_next[soa->current_checkpoint[soa_index]]);
				}
				trace_checkpoint_graph("last_ground", soa->last_ground_checkpoint[soa_index]);
				trace_checkpoint_graph("last_ground_next_linear", soa->last_ground_checkpoint[soa_index] + 1);
			}
			// Treat the checkpoint/lap update as a transaction. In particular, a jump across the
			// final lap line must not become a completed race before the shortcut restore begins.
			soa->lap[soa_index] = prev_lap;
			soa->current_checkpoint[soa_index] = static_cast<uint16_t>(old_cp);
			soa->checkpoint_fraction[soa_index] = old_fraction;
			soa->checkpoint_track_distance[soa_index] = old_track_distance;
			soa->lap_progress[soa_index] = old_lap_progress;
			soa->broken_lap_rollback_pending[soa_index] = old_broken_lap_rollback_pending;
			soa->broken_lap_rollback_lap[soa_index] = old_broken_lap_rollback_lap;
			start_restore_to_last_ground();
			return;
		}
	}

	const uint32_t target_lap = soa->race_lap_target[soa_index];
	if (target_lap > 0 && soa->lap[soa_index] > target_lap){
		soa->machine_state[soa_index] |= MACHINESTATE::COMPLETEDRACE_1_Q;
		soa->broken_lap_rollback_pending[soa_index] = false;
		soa->broken_lap_rollback_lap[soa_index] = 0;
	}
	soa->previous_lap_distance[soa_index] = current_lap_distance;

	if (soa->lap[soa_index] != prev_lap) {
		soa->machine_state[soa_index] |= MACHINESTATE::CROSSEDLAPLINE_Q;
	}
};

void PhysicsCar::collide_with_landmine(Mine* in_mine, const SimVec3 &travel_start, const SimVec3 &travel_end)
{
	if (in_mine->exploded)
	{
		return;
	}

	SimVec3 mine_pos = in_mine->transform.origin;// + in_mine->transform.basis.get_column(1);

	SimVec3 travel_vec = travel_end - travel_start;

	SimVec3 prev_to_mine = mine_pos - travel_start;

	float travel_len = travel_vec.length();

	float t = travel_vec.dot(prev_to_mine) / (travel_len * travel_len);

	SimVec3 closest_on_path = travel_vec * t + travel_start;

	float speed_dir_sign = (soa->speed_kmh[soa_index] > 600.0f) ? 1.0f : -1.0f;

	SimVec3 mine_to_path = closest_on_path - mine_pos;

	float mine_to_path_len = mine_to_path.length();

	float normal_dot = LOAD_VEC3(track_surface_normal).dot(mine_to_path.normalized());

	float normal_proj_len = mine_to_path_len * normal_dot;

	SimVec3 point_on_track_plane = LOAD_VEC3(track_surface_normal) * normal_proj_len + mine_pos;

	SimVec3 planar_component = closest_on_path - point_on_track_plane;

	float planar_length = planar_component.length();

	// TODO: replace sqrt with a deterministic_fp variant
	float cosTheta = sqrt(1.0 - planar_length * 0.25 * planar_length * 0.25);
	float signedCosThetaD = speed_dir_sign * cosTheta;
	float sinTheta = sqrt(1.0 - (signedCosThetaD * signedCosThetaD));

	SimVec3 normalDir = {0,0,0};
	if(sinTheta > 0.0000001)
		normalDir = set_vec3_length(planar_component, sinTheta);

	SimVec3 travelDirScaled = {0,0,0};
	if(signedCosThetaD > (0.0000001))
		travelDirScaled = set_vec3_length(travel_vec, signedCosThetaD);

	SimVec3 kickDir = normalDir + travelDirScaled;

	kickDir = set_vec3_length(kickDir, 4.0);

	STORE_VEC3(position_current, kickDir + point_on_track_plane);

	SimVec3 displacementWorld = LOAD_VEC3(position_current) - mine_pos;

	displacementWorld = mxt_basis_inverse_rotate(LOAD_TRANSFORM(basis_physical), displacementWorld);	// → local

	float displacementLen = displacementWorld.length();

	displacementWorld = set_vec3_length(displacementWorld, 5.555555 * soa->stat_weight[soa_index]);

	soa->visual_rotation_z[soa_index]  += 6.0f * displacementWorld.x;
	soa->visual_rotation_x[soa_index] += 2.0f * displacementWorld.z;

	displacementWorld = mxt_basis_rotate(LOAD_TRANSFORM(basis_physical), displacementWorld);

	soa->velocity_x[soa_index] += displacementWorld.x;
	soa->velocity_y[soa_index] += displacementWorld.y;
	soa->velocity_z[soa_index] += displacementWorld.z;

	//------------------------------------------------------------------
	// 8) Damage & state flags (unchanged logic)
	//------------------------------------------------------------------

	soa->terrain_state[soa_index] |= 0x40000000;			// “hit mine” flag

	if(!soa->s_boost_active[soa_index] && soa->breakdown_frame_counter[soa_index] == 0)
	{
		float damage = 20.0f * soa->stat_body[soa_index];

		if((soa->machine_state[soa_index] & MACHINESTATE::B10) == 0 && damage > 20.0f)
			damage = 20.0f;

		float maxFrameDamage = 1.01f * soa->calced_max_energy[soa_index];

		if(damage > maxFrameDamage) damage = maxFrameDamage;

		soa->energy[soa_index] -= damage;

		if(soa->energy[soa_index] < 0.0f)
		{
			if((soa->machine_state[soa_index] & (MACHINESTATE::COMPLETEDRACE_1_Q | MACHINESTATE::ZEROHP)) == 0)
				soa->breakdown_frame_counter[soa_index] = 60;

			soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
			soa->energy[soa_index]	  = 0.0f;
			soa->base_speed[soa_index] = 0.0f;
		}
	}

}


bool PhysicsCar::compute_respawn_target(uint16_t cp_idx, SimTransform &out_transform, float &out_distance, uint16_t *out_checkpoint, float *out_fraction) const
{
	out_transform = SimTransform();
	out_distance = soa->last_ground_distance[soa_index];
	if (out_checkpoint) {
		*out_checkpoint = cp_idx;
	}
	if (out_fraction) {
		*out_fraction = 0.0f;
	}

	if (!soa->current_track[soa_index] || soa->current_track[soa_index]->num_checkpoints == 0 || cp_idx >= soa->current_track[soa_index]->num_checkpoints)
		return false;

	const int num_checkpoints = soa->current_track[soa_index]->num_checkpoints;
	const CollisionCheckpoint &start_cp = soa->current_track[soa_index]->checkpoints[cp_idx];

	float lap_length = soa->current_track[soa_index]->lap_length;
	if (lap_length <= 0.0f && num_checkpoints > 0) {
		lap_length = soa->current_track[soa_index]->checkpoints[num_checkpoints - 1].distance;
	}
	const bool has_lap_length = lap_length > 0.0f;
	const bool trace_restore = trace_rail_for_car(soa, soa_index);

	auto normalize_distance = [&](float dist) -> float {
		if (!has_lap_length)
			return dist;
		float normalized = std::fmod(dist, lap_length);
		if (normalized < 0.0f)
			normalized += lap_length;
		return normalized;
	};

	const float start_cp_length = std::max(start_cp.local_distance, kMinCheckpointDistance);
	const float cp_start_distance = start_cp.distance - start_cp.local_distance;
	const float normalized_ground = normalize_distance(soa->last_ground_distance[soa_index]);
	const float normalized_cp_start = normalize_distance(cp_start_distance);

	float distance_into_cp = normalized_ground - normalized_cp_start;
	if (distance_into_cp < 0.0f && has_lap_length)
		distance_into_cp += lap_length;
	distance_into_cp = std::clamp(distance_into_cp, 0.0f, start_cp_length);

	float remaining = kRespawnForwardDistance;
	if (has_lap_length) {
		remaining = std::fmod(kRespawnForwardDistance, lap_length);
		if (remaining < 0.0f)
			remaining += lap_length;
		if (remaining == 0.0f)
			remaining = lap_length;
	}

	int target_cp_idx = cp_idx;
	float target_fraction = 0.0f;

	const float distance_to_cp_end = std::max(start_cp_length - distance_into_cp, 0.0f);
	if (remaining <= distance_to_cp_end || num_checkpoints == 1) {
		float along = distance_into_cp + remaining;
		float denom = std::max(start_cp_length, kMinCheckpointDistance);
		target_fraction = std::clamp(along / denom, 0.0f, 1.0f);
	} else {
		remaining -= distance_to_cp_end;
		int idx = (cp_idx + 1) % num_checkpoints;
		int last_idx = idx;
		for (int step = 0; step < num_checkpoints; ++step) {
			last_idx = idx;
			const CollisionCheckpoint &candidate = soa->current_track[soa_index]->checkpoints[idx];
			float candidate_length = std::max(candidate.local_distance, kMinCheckpointDistance);
			if (remaining <= candidate_length) {
				target_cp_idx = idx;
				target_fraction = std::clamp(remaining / candidate_length, 0.0f, 1.0f);
				remaining = 0.0f;
				break;
			}
			remaining -= candidate_length;
			idx = (idx + 1) % num_checkpoints;
		}
		if (remaining > 0.0f) {
			target_cp_idx = last_idx;
			target_fraction = 1.0f;
		}
	}

	const CollisionCheckpoint &target_cp = soa->current_track[soa_index]->checkpoints[target_cp_idx];
	float t_y = target_cp.t_start + (target_cp.t_end - target_cp.t_start) * target_fraction;
	t_y = std::clamp(t_y, std::min(target_cp.t_start, target_cp.t_end), std::max(target_cp.t_start, target_cp.t_end));

	soa->current_track[soa_index]->segments[target_cp.road_segment]
	.road_shape->get_oriented_transform_at_time(out_transform, SimVec2(0.0f, t_y));
	out_transform.basis.orthonormalize();
	out_transform.basis = out_transform.basis.rotated(out_transform.basis.get_column(1), Math_PI);
	out_transform.origin += out_transform.basis.get_column(1) * 0.1f;

	float new_distance = soa->last_ground_distance[soa_index] + kRespawnForwardDistance;
	if (has_lap_length) {
		new_distance = std::fmod(new_distance, lap_length);
		if (new_distance < 0.0f)
			new_distance += lap_length;
	}
	out_distance = new_distance;
	if (out_checkpoint) {
		*out_checkpoint = static_cast<uint16_t>(target_cp_idx);
	}
	if (out_fraction) {
		*out_fraction = target_fraction;
	}
	if (trace_restore) {
		godot::UtilityFunctions::print(
			godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" event=compute_respawn_target"),
			godot::String(" cp_arg="), static_cast<int64_t>(cp_idx),
			godot::String(" target_cp="), static_cast<int64_t>(target_cp_idx),
			godot::String(" target_fraction="), target_fraction,
			godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index],
			godot::String(" normalized_ground="), normalized_ground,
			godot::String(" cp_start_dist="), cp_start_distance,
			godot::String(" normalized_cp_start="), normalized_cp_start,
			godot::String(" distance_into_cp="), distance_into_cp,
			godot::String(" respawn_distance="), out_distance,
			godot::String(" lap_length="), lap_length,
			godot::String(" target_pos=("), out_transform.origin.x, godot::String(","), out_transform.origin.y, godot::String(","), out_transform.origin.z, godot::String(")"));
	}

	return true;
}

void PhysicsCar::respawn_at_checkpoint(uint16_t cp_idx)
{
	if (!soa->current_track[soa_index] || cp_idx >= soa->current_track[soa_index]->num_checkpoints)
		return;

	SimTransform spawn_transform;
	float respawn_distance = soa->last_ground_distance[soa_index];
	uint16_t respawn_checkpoint = cp_idx;
	float respawn_fraction = 0.0f;
	if (!compute_respawn_target(cp_idx, spawn_transform, respawn_distance, &respawn_checkpoint, &respawn_fraction))
		return;
	const bool trace_restore = trace_rail_for_car(soa, soa_index);
	if (trace_restore) {
		const SimVec3 pos = LOAD_VEC3(position_current);
		godot::UtilityFunctions::print(
			godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" event=respawn_at_checkpoint"),
			godot::String(" cp_arg="), static_cast<int64_t>(cp_idx),
			godot::String(" respawn_cp="), static_cast<int64_t>(respawn_checkpoint),
			godot::String(" respawn_fraction="), respawn_fraction,
			godot::String(" respawn_distance="), respawn_distance,
			godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
			godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index],
			godot::String(" pos_before=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"),
			godot::String(" spawn=("), spawn_transform.origin.x, godot::String(","), spawn_transform.origin.y, godot::String(","), spawn_transform.origin.z, godot::String(")"));
	}

	if (soa->broken_lap_rollback_pending[soa_index] &&
		(soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0) {
		const float lap_length = track_lap_length(soa->current_track[soa_index]);
		if (soa->lap[soa_index] == soa->broken_lap_rollback_lap[soa_index] &&
			soa->lap[soa_index] > 0 &&
			track_distance_is_before_lap_line(respawn_distance, lap_length)) {
			soa->lap[soa_index] -= 1;
			soa->machine_state[soa_index] |= MACHINESTATE::CROSSEDLAPLINE_Q;
		}
	}
	soa->broken_lap_rollback_pending[soa_index] = false;
	soa->broken_lap_rollback_lap[soa_index] = 0;

	soa->last_ground_distance[soa_index] = respawn_distance;
	soa->last_ground_checkpoint[soa_index] = respawn_checkpoint;
	soa->current_checkpoint[soa_index] = respawn_checkpoint;
	soa->current_collision_checkpoint[soa_index] = static_cast<int16_t>(respawn_checkpoint);
	soa->checkpoint_fraction[soa_index] = respawn_fraction;
	soa->lap_progress[soa_index] = (static_cast<float>(respawn_checkpoint) + respawn_fraction) / static_cast<float>(soa->current_track[soa_index]->num_checkpoints);
	soa->checkpoint_track_distance[soa_index] = respawn_distance;
	soa->previous_lap_distance[soa_index] = soa->current_track[soa_index]->compute_lap_distance(respawn_checkpoint, respawn_fraction, soa->lap[soa_index]);
	STORE_VEC3(position_current, spawn_transform.origin);
	STORE_VEC3(position_old, spawn_transform.origin);
	STORE_VEC3(position_old_dupe, spawn_transform.origin);
	STORE_VEC3(position_bottom, spawn_transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));

	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = spawn_transform.basis; STORE_TRANSFORM(basis_physical, mxt_tmp); }
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical_other); mxt_tmp.basis = spawn_transform.basis; STORE_TRANSFORM(basis_physical_other, mxt_tmp); }
	update_pitch_transform_from_machine_front_back();

	STORE_TRANSFORM(transform_visual, spawn_transform);
	const SimVec3 spawn_up = spawn_transform.basis.get_column(1);
	STORE_VEC3(track_surface_normal, spawn_up);
	STORE_VEC3(track_surface_pos, spawn_transform.origin - spawn_up * 0.1f);
	soa->height_above_track[soa_index] = 19.9f;

	STORE_VEC3(velocity, SimVec3());
	STORE_VEC3(knockback_velocity, SimVec3());
	STORE_VEC3(velocity_local, SimVec3());
	STORE_VEC3(velocity_local_flattened_and_rotated, SimVec3());
	STORE_VEC3(velocity_angular, SimVec3());
	STORE_VEC3(visual_rotation, SimVec3());
	STORE_VEC3(unk_vec3_0x4e4, SimVec3());
	STORE_VEC3(unk_vec3_0x4f0, SimVec3());
	soa->base_speed[soa_index] = 0.0f;
	soa->boost_turbo[soa_index] = 0.0f;
	soa->some_breakdown_int[soa_index] = 0;
	soa->breakdown_frame_counter[soa_index] = 0;
	soa->machine_crashed[soa_index] = false;
	soa->state_2[soa_index] &= ~(0x2u | 0x20u | 0x80u | 0x100u);
	soa->air_time[soa_index] = 0;
	soa->grip_frames_from_accel_press[soa_index] = 0;
	soa->boost_frames_manual[soa_index] = 0;
	soa->boost_frames_dash[soa_index] = 0;
	soa->boost_duration_manual_frames[soa_index] = 0;
	soa->boost_duration_dash_frames[soa_index] = 0;

	soa->machine_state[soa_index] &= ~(MACHINESTATE::ZEROHP |
		MACHINESTATE::AIRBORNE |
		MACHINESTATE::AIRBORNEMORE0_2S_Q |
		MACHINESTATE::FALLOUT |
		MACHINESTATE::TOOKDAMAGE |
		MACHINESTATE::LOWGRIP |
		MACHINESTATE::SIDEATTACKING |
		MACHINESTATE::SPINATTACKING |
		MACHINESTATE::JUSTHITVEHICLE_Q);
	soa->frames_since_death[soa_index] = 0;
	const int point_base = soa_index * 4;
	const SimTransform restore_transform = LOAD_TRANSFORM(basis_physical);
	const SimVec3 restore_position = LOAD_VEC3(position_current);
	const SimVec3x4 tilt_pos = mxt_transform_points4(
		restore_transform,
		restore_position,
		sim_load4(soa->tilt_offset_x + point_base),
		sim_load4(soa->tilt_offset_y + point_base),
		sim_load4(soa->tilt_offset_z + point_base));
	const SimVec3 up = mxt_basis_rotate(restore_transform, SimVec3(0, 1, 0));
	for (int lane = 0; lane < 4; ++lane) {
		const int p = point_base + lane;
		soa->tilt_state[p] = 0;
		soa->tilt_force[p] = 0.0f;
		STORE_TILT_VEC3(force_spatial, p, SimVec3());
		STORE_TILT_VEC3(up_vector_2, p, up);
		STORE_TILT_VEC3(up_vector, p, up);
	}
	sim_store4(soa->tilt_pos_old_x + point_base, tilt_pos.x);
	sim_store4(soa->tilt_pos_old_y + point_base, tilt_pos.y);
	sim_store4(soa->tilt_pos_old_z + point_base, tilt_pos.z);
	sim_store4(soa->tilt_pos_x + point_base, tilt_pos.x);
	sim_store4(soa->tilt_pos_y + point_base, tilt_pos.y);
	sim_store4(soa->tilt_pos_z + point_base, tilt_pos.z);
}

SimTransform PhysicsCar::calculate_respawn_transform(uint16_t cp_idx) const
{
	SimTransform spawn_transform;
	float dummy_distance = soa->last_ground_distance[soa_index];
	if (!compute_respawn_target(cp_idx, spawn_transform, dummy_distance, nullptr, nullptr))
		return SimTransform();
	return spawn_transform;
}

void PhysicsCar::start_restore_to_last_ground()
{
	if (!soa->current_track[soa_index]) {
		return;
	}
	const bool trace_restore = trace_rail_for_car(soa, soa_index);
	if (trace_restore) {
		const SimVec3 pos = LOAD_VEC3(position_current);
		godot::UtilityFunctions::print(
			godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" event=start_restore"),
			godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
			godot::String(" coll="), static_cast<int64_t>(soa->current_collision_checkpoint[soa_index]),
			godot::String(" frac="), soa->checkpoint_fraction[soa_index],
			godot::String(" track_dist="), soa->checkpoint_track_distance[soa_index],
			godot::String(" previous_lap_dist="), soa->previous_lap_distance[soa_index],
			godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
			godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index],
			godot::String(" state=0x"), godot::String::num_int64(static_cast<int64_t>(soa->machine_state[soa_index]), 16),
			godot::String(" restore="), static_cast<int64_t>(soa->restore_state[soa_index]),
			godot::String(" pos=("), pos.x, godot::String(","), pos.y, godot::String(","), pos.z, godot::String(")"));
	}
	soa->restore_state[soa_index] = 2;
	soa->restore_wait_frames[soa_index] = 0;
	soa->restore_move_frames[soa_index] = 0;
	soa->machine_state[soa_index] |= MACHINESTATE::FALLOUT | MACHINESTATE::AIRBORNE;
	{ SimTransform mxt_tmp = LOAD_TRANSFORM(restore_start_transform); mxt_tmp.origin = LOAD_VEC3(position_current); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(restore_start_transform, mxt_tmp); }
	STORE_TRANSFORM(restore_target_transform, calculate_respawn_transform(soa->last_ground_checkpoint[soa_index]));
	clear_motion_for_restore(soa, soa_index);
}

void PhysicsCar::trigger_mesh_fallout()
{
	if (trace_rail_for_car(soa, soa_index)) {
		godot::UtilityFunctions::print(
			godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
			godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
			godot::String(" event=trigger_mesh_fallout"),
			godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
			godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
			godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index]);
	}
	soa->machine_state[soa_index] |= MACHINESTATE::FALLOUT |
		MACHINESTATE::AIRBORNE |
		MACHINESTATE::DIEDTHISFRAMEOOB_Q;
	soa->terrain_state[soa_index] |= TERRAIN::FALL;
	start_restore_to_last_ground();
}

void PhysicsCar::trigger_mesh_kill_collision()
{
	soa->terrain_state[soa_index] |= TERRAIN::KILL;
	soa->machine_state[soa_index] |= MACHINESTATE::TOOKDAMAGE;
	if ((soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) != 0) {
		soa->energy[soa_index] = 0.0f;
		return;
	}

	soa->energy[soa_index] = 0.0f;
	soa->base_speed[soa_index] = 0.0f;
	if ((soa->machine_state[soa_index] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0) {
		soa->breakdown_frame_counter[soa_index] = 60;
	}
	soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
}

void PhysicsCar::update_restore(float accel_input)
{
	if (!soa->current_track[soa_index])
		return;

	bool crashed =
		soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y ||
		soa->energy[soa_index] <= 0.0f ||
		(soa->machine_state[soa_index] & MACHINESTATE::FALLOUT) != 0;

	if (soa->restore_state[soa_index] == 0 && crashed) {
		if (trace_rail_for_car(soa, soa_index)) {
			godot::UtilityFunctions::print(
				godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
				godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
				godot::String(" event=restore_crashed"),
				godot::String(" fell_y="), soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y,
				godot::String(" zero_hp="), soa->energy[soa_index] <= 0.0f,
				godot::String(" fallout="), (soa->machine_state[soa_index] & MACHINESTATE::FALLOUT) != 0,
				godot::String(" cp="), static_cast<int64_t>(soa->current_checkpoint[soa_index]),
				godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
				godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index]);
		}
		soa->restore_state[soa_index] = 1;
		soa->restore_wait_frames[soa_index] = 0;
		if (soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y)
			soa->machine_state[soa_index] |= MACHINESTATE::FALLOUT;
		if (soa->energy[soa_index] <= 0.0f)
			soa->machine_state[soa_index] |= MACHINESTATE::ZEROHP;
	}

	if (soa->restore_state[soa_index] == 1) {
		soa->restore_wait_frames[soa_index]++;
		if ((soa->restore_wait_frames[soa_index] >= 60 && accel_input > 0.1f) || (soa->machine_state[soa_index] & MACHINESTATE::FALLOUT) != 0) {
			if (trace_rail_for_car(soa, soa_index)) {
				godot::UtilityFunctions::print(
					godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
					godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
					godot::String(" event=restore_wait_to_move"),
					godot::String(" wait_frames="), static_cast<int64_t>(soa->restore_wait_frames[soa_index]),
					godot::String(" accel="), accel_input,
					godot::String(" fallout="), (soa->machine_state[soa_index] & MACHINESTATE::FALLOUT) != 0,
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
					godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index]);
			}
			soa->restore_state[soa_index] = 2;
			soa->restore_move_frames[soa_index] = 0;
			{ SimTransform mxt_tmp = LOAD_TRANSFORM(restore_start_transform); mxt_tmp.origin = LOAD_VEC3(position_current); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(restore_start_transform, mxt_tmp); }
			STORE_TRANSFORM(restore_target_transform, calculate_respawn_transform(soa->last_ground_checkpoint[soa_index]));
			clear_motion_for_restore(soa, soa_index);
		}
	} else if (soa->restore_state[soa_index] == 2) {
		soa->restore_move_frames[soa_index]++;
		const uint32_t restore_total_frames = soa->s_boost_active[soa_index] ? 18u : 180u;
		const uint32_t restore_countdown_frames = soa->s_boost_active[soa_index] ? 16u : 160u;
		float t = std::min(1.0f, static_cast<float>(soa->restore_move_frames[soa_index]) / static_cast<float>(restore_total_frames));
		t = (t < 0.5f) ? (2.0f * t * t) : (-1.0f + (4.0f - 2.0f * t) * t);
		soa->state_2[soa_index] &= ~0x20;
		if (soa->restore_move_frames[soa_index] >= restore_countdown_frames) {
			soa->machine_state[soa_index] |= MACHINESTATE::STARTINGCOUNTDOWN;
		}
		SimVec3 pos = LOAD_TRANSFORM(restore_start_transform).origin.lerp(LOAD_TRANSFORM(restore_target_transform).origin, t);
		SimQuat qs = LOAD_TRANSFORM(restore_start_transform).basis.get_rotation_quaternion();
		SimQuat qe = LOAD_TRANSFORM(restore_target_transform).basis.get_rotation_quaternion();
		SimQuat qi = qs.slerp(qe, t);

		{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical); mxt_tmp.basis = SimBasis(qi); STORE_TRANSFORM(basis_physical, mxt_tmp); }
		{ SimTransform mxt_tmp = LOAD_TRANSFORM(basis_physical_other); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(basis_physical_other, mxt_tmp); }
		{ SimTransform mxt_tmp = LOAD_TRANSFORM(transform_visual); mxt_tmp.basis = LOAD_TRANSFORM(basis_physical).basis; STORE_TRANSFORM(transform_visual, mxt_tmp); }

		STORE_VEC3(position_current, pos);
		STORE_VEC3(position_old, pos);
		STORE_VEC3(position_old_dupe, pos);
		STORE_VEC3(position_bottom, mxt_transform_point(LOAD_TRANSFORM(basis_physical), pos, SimVec3(0.0f, -0.1f, 0.0f)));

		if (soa->restore_move_frames[soa_index] >= restore_total_frames) {
			if (trace_rail_for_car(soa, soa_index)) {
				godot::UtilityFunctions::print(
					godot::String("MXT_RESTORE_TRACE tick="), static_cast<int64_t>(soa->simulation_tick[soa_index]),
					godot::String(" car="), static_cast<int64_t>(soa->global_start + soa_index),
					godot::String(" event=restore_finish"),
					godot::String(" move_frames="), static_cast<int64_t>(soa->restore_move_frames[soa_index]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa->last_ground_checkpoint[soa_index]),
					godot::String(" last_ground_dist="), soa->last_ground_distance[soa_index]);
			}
			soa->state_2[soa_index] &= ~0x20;
			respawn_at_checkpoint(soa->last_ground_checkpoint[soa_index]);
			soa->energy[soa_index] = std::max(soa->energy[soa_index], soa->calced_max_energy[soa_index] * 0.5f);
			soa->machine_state[soa_index] |= MACHINESTATE::ACTIVE;
			soa->machine_state[soa_index] &= ~MACHINESTATE::STARTINGCOUNTDOWN;
			soa->frames_since_start_2[soa_index] = 60;
			soa->restore_state[soa_index] = 0;
			soa->restore_wait_frames[soa_index] = 0;
			soa->restore_move_frames[soa_index] = 0;
		}
	}
}

void PhysicsCar::check_respawn()
{
	if (!soa->current_track[soa_index])
		return;

	if (soa->position_current_y[soa_index] < soa->current_track[soa_index]->minimum_y || soa->energy[soa_index] <= 0.0f) {
		respawn_at_checkpoint(soa->last_ground_checkpoint[soa_index]);
		if (soa->energy[soa_index] < soa->calced_max_energy[soa_index] * 0.5f)
			soa->energy[soa_index] = soa->calced_max_energy[soa_index] * 0.5f;
	}
}

bool PhysicsCar::can_collect_super_spark() const
{
	return !soa->s_boost_active[soa_index] && (soa->machine_state[soa_index] & MACHINESTATE::ZEROHP) == 0;
}

void PhysicsCar::add_super_spark_charge(uint16_t amount)
{
	if (soa->s_boost_active[soa_index] || amount == 0)
		return;

	uint16_t new_charge = soa->s_boost_charge[soa_index] + amount;
	if (new_charge > soa->s_boost_charge_max[soa_index])
		new_charge = soa->s_boost_charge_max[soa_index];
	soa->s_boost_charge[soa_index] = new_charge;
}

bool PhysicsCar::can_start_s_boost() const
{
	return !soa->s_boost_active[soa_index] && soa->s_boost_charge[soa_index] >= soa->s_boost_charge_max[soa_index];
}

void PhysicsCar::start_s_boost(uint16_t duration_frames)
{
	if (duration_frames == 0)
		duration_frames = 1;

	soa->s_boost_active[soa_index] = true;
	soa->s_boost_frames_remaining[soa_index] = duration_frames;
	soa->s_boost_charge[soa_index] = 0;
	soa->s_boost_emit_frame_accumulator[soa_index] = 0;
	soa->s_boost_pending_spark_spawns[soa_index] = 0;
	soa->boost_frames_manual[soa_index] = 0;
	soa->boost_frames_dash[soa_index] = 0;
	soa->boost_duration_manual_frames[soa_index] = 0;
	soa->boost_duration_dash_frames[soa_index] = 0;
	soa->boost_turbo[soa_index] = 0.0f;
	soa->pending_dashplate_heat[soa_index] = 0.0f;
	soa->pending_dashplate_heat_reward_scale[soa_index] = 1.0f;
	soa->car_hit_invincibility[soa_index] = 0;
	soa->machine_state[soa_index] &= ~(MACHINESTATE::JUST_PRESSED_BOOST |
		MACHINESTATE::BOOSTING |
		MACHINESTATE::BOOSTING_DASHPLATE |
		MACHINESTATE::SIDEATTACKING |
		MACHINESTATE::SPINATTACKING |
		MACHINESTATE::TOOKDAMAGE |
		MACHINESTATE::LOWGRIP);
}

void PhysicsCar::stop_s_boost()
{
	soa->s_boost_active[soa_index] = false;
	soa->s_boost_frames_remaining[soa_index] = 0;
	soa->s_boost_emit_frame_accumulator[soa_index] = 0;
	soa->s_boost_pending_spark_spawns[soa_index] = 0;
}

void PhysicsCar::update_s_boost_state()
{
	if (!soa->s_boost_active[soa_index]) {
		soa->s_boost_frames_remaining[soa_index] = 0;
		soa->s_boost_emit_frame_accumulator[soa_index] = 0;
		soa->s_boost_pending_spark_spawns[soa_index] = 0;
		return;
	}

	if (soa->s_boost_frames_remaining[soa_index] > 0)
		soa->s_boost_frames_remaining[soa_index] -= 1;

	soa->machine_state[soa_index] &= ~(MACHINESTATE::TOOKDAMAGE | MACHINESTATE::LOWGRIP);

	soa->s_boost_emit_frame_accumulator[soa_index] += 1;
	while (soa->s_boost_emit_frame_accumulator[soa_index] >= 30) {
		soa->s_boost_emit_frame_accumulator[soa_index] -= 30;
		if (soa->s_boost_pending_spark_spawns[soa_index] < 255)
			soa->s_boost_pending_spark_spawns[soa_index] += 1;
	}

	if (soa->s_boost_frames_remaining[soa_index] == 0) {
		stop_s_boost();
	}
}

uint8_t PhysicsCar::consume_pending_s_boost_sparks()
{
	uint8_t pending = soa->s_boost_pending_spark_spawns[soa_index];
	soa->s_boost_pending_spark_spawns[soa_index] = 0;
	return pending;
}

void PhysicsCar::queue_super_sparks(int count)
{
	if (count <= 0)
		return;
	const int pending = static_cast<int>(soa->pending_super_sparks[soa_index]) + count;
	soa->pending_super_sparks[soa_index] = static_cast<uint8_t>(pending > 255 ? 255 : pending);
}
