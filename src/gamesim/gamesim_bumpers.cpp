#include "gamesim/gamesim_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace godot;

static constexpr float BUMPER_POST_LEADER_DAMAGE_PER_SECOND = 1.5f;

void GameSim::set_bumpers_enabled(bool enabled)
{
	bumpers_enabled = enabled;
}

void GameSim::set_s_boost_enabled(bool enabled)
{
	s_boost_enabled = enabled;
	if (enabled) {
		return;
	}
	if (super_spark_state && super_sparks) {
		reset_super_sparks();
	}
	if (!cars) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		soa.s_boost_charge[lane] = 0;
		soa.s_boost_active[lane] = false;
		soa.s_boost_frames_remaining[lane] = 0;
		soa.s_boost_emit_frame_accumulator[lane] = 0;
		soa.s_boost_pending_spark_spawns[lane] = 0;
		soa.pending_super_sparks[lane] = 0;
	}
}

void GameSim::configure_bumper_car(int bumper_slot)
{
	if (!bumper_cars || bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	PhysicsCarProperties* props = soa.car_properties[lane];
	if (!props) {
		return;
	}
	props->base_stats[CAR_STAT_WEIGHT_KG] = 1800.0f;
	props->base_stats[CAR_STAT_ACCELERATION] = 0.55f;
	props->base_stats[CAR_STAT_MAX_SPEED] = 0.14f;
	props->base_stats[CAR_STAT_GRIP_1] = 1.2f;
	props->base_stats[CAR_STAT_GRIP_2] = 1.0f;
	props->base_stats[CAR_STAT_GRIP_3] = 0.35f;
	props->base_stats[CAR_STAT_TURN_TENSION] = 0.02f;
	props->base_stats[CAR_STAT_DRIFT_ACCEL] = 1.8f;
	props->base_stats[CAR_STAT_TURN_MOVEMENT] = 240.0f;
	props->base_stats[CAR_STAT_DRIFT_TURN_MOVEMENT] = 240.0f;
	props->base_stats[CAR_STAT_STRAFE_TURN] = 160.0f;
	props->base_stats[CAR_STAT_STRAFE] = 120.0f;
	props->base_stats[CAR_STAT_TURN_REACTION] = 45.0f;
	props->base_stats[CAR_STAT_MANUAL_TURBO_GAIN] = 0.0f;
	props->base_stats[CAR_STAT_DASHPLATE_TURBO_GAIN] = 0.0f;
	props->base_stats[CAR_STAT_MANUAL_BOOST_DURATION_SECONDS] = 0.1f;
	props->base_stats[CAR_STAT_DASHPLATE_BOOST_DURATION_SECONDS] = 0.05f;
	props->base_stats[CAR_STAT_TURN_DECEL] = 0.0f;
	props->base_stats[CAR_STAT_DRAG] = 0.0065f;
	props->base_stats[CAR_STAT_BODY] = 1.0f;
	props->base_stats[CAR_STAT_CAMERA_REORIENTING] = 1.0f;
	props->base_stats[CAR_STAT_CAMERA_REPOSITIONING] = 1.0f;
	props->base_stats[CAR_STAT_TRACK_COLLISION] = 2.0f;
	props->base_stats[CAR_STAT_OBSTACLE_COLLISION] = 3.5f;
	props->base_stats[CAR_STAT_MAX_ENERGY] = 80.0f;
	props->base_stats[CAR_STAT_BOOST_ENERGY_USE_RATE] = 999.0f;
	props->base_stats[CAR_STAT_ENERGY_RECHARGE_RATE] = 0.0f;
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) {
			props->s_boost_stats[stat] = props->base_stats[stat];
		}
	}
	for (int p = 0; p < 4; ++p) {
		props->tilt_corners[p].x *= 1.4f;
		props->tilt_corners[p].z *= 1.4f;
		props->wall_corners[p].x *= 1.4f;
		props->wall_corners[p].z *= 1.4f;
	}
	soa.m_accel_setting[lane] = 1.0f;
	if (bumper_slot >= 0 && bumper_slot < bumper_count) {
		bumper_states[bumper_slot].active = 0;
		bumper_states[bumper_slot].spawn_lap = 0;
		bumper_states[bumper_slot].next_sequence = static_cast<uint32_t>(bumper_slot);
		bumper_states[bumper_slot].target_lane = 0.0f;
	}
}

bool GameSim::sample_track_transform_at_distance(float absolute_distance, float lane_offset, SimTransform& out_transform, uint16_t& out_checkpoint, float& out_fraction) const
{
	if (!current_track || current_track->num_checkpoints <= 0) {
		return false;
	}
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	if (lap_length <= 0.0f) {
		return false;
	}
	float distance = std::fmod(absolute_distance, lap_length);
	if (distance < 0.0f) {
		distance += lap_length;
	}
	int cp_index = current_track->num_checkpoints - 1;
	for (int i = 0; i < current_track->num_checkpoints; ++i) {
		if (distance <= current_track->checkpoints[i].distance) {
			cp_index = i;
			break;
		}
	}
	const CollisionCheckpoint& cp = current_track->checkpoints[cp_index];
	const float cp_start = std::max(0.0f, cp.distance - cp.local_distance);
	const float cp_len = std::max(cp.local_distance, 0.001f);
	const float cp_fraction = std::clamp((distance - cp_start) / cp_len, 0.0f, 1.0f);
	const float t_y = cp.t_start + (cp.t_end - cp.t_start) * cp_fraction;
	if (cp.road_segment < 0 || cp.road_segment >= current_track->num_segments) {
		return false;
	}
	current_track->segments[cp.road_segment].road_shape->get_oriented_transform_at_time(
		out_transform,
		SimVec2(std::clamp(lane_offset, -0.85f, 0.85f), t_y));
	out_transform.basis.orthonormalize();
	out_checkpoint = static_cast<uint16_t>(cp_index);
	out_fraction = cp_fraction;
	return true;
}

void GameSim::deactivate_bumper_car(int bumper_slot)
{
	if (!bumper_cars || bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	const int was_active = bumper_states[bumper_slot].active;
	bumper_states[bumper_slot].active = 0;
	if (was_active) {
		bumper_states[bumper_slot].next_sequence = 0u;
	}
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	const SimVec3 hidden(0.0f, current_track ? current_track->minimum_y - 10000.0f : -10000.0f, 0.0f);
	STORE_INDEXED_VEC3(soa, position_current, lane, hidden);
	STORE_INDEXED_VEC3(soa, position_old, lane, hidden);
	STORE_INDEXED_VEC3(soa, position_old_dupe, lane, hidden);
	SimTransform hidden_transform = MXT_LOAD_TRANSFORM(soa, transform_visual, lane);
	hidden_transform.origin = hidden;
	MXT_STORE_TRANSFORM(soa, transform_visual, lane, hidden_transform);
	MXT_STORE_TRANSFORM(soa, basis_physical, lane, hidden_transform);
	MXT_STORE_TRANSFORM(soa, basis_physical_other, lane, hidden_transform);
	STORE_INDEXED_VEC3(soa, position_bottom, lane, hidden);
	STORE_INDEXED_VEC3(soa, velocity, lane, SimVec3());
	STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3());
	STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3());
	soa.energy[lane] = 0.0f;
	soa.speed_kmh[lane] = 0.0f;
	soa.base_speed[lane] = 0.0f;
	soa.current_track[lane] = nullptr;
	soa.restore_state[lane] = 2;
	soa.restore_wait_frames[lane] = 0;
	soa.restore_move_frames[lane] = 0;
	soa.machine_state[lane] |= MACHINESTATE::ZEROHP;
	soa.machine_state[lane] &= ~(MACHINESTATE::ACTIVE | MACHINESTATE::STARTINGCOUNTDOWN | MACHINESTATE::FALLOUT | MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q);
}

void GameSim::spawn_bumper_explosion(int bumper_slot)
{
	if (!render_gamesim_called || !spark_node_container || !bumper_cars ||
			bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	SimTransform transform = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
	transform.origin = LOAD_INDEXED_VEC3(soa, position_current, lane);
	spark_node_container->call("spawn_bumper_explosion", gd_transform(transform));
}

void GameSim::set_bumper_track_state(int bumper_slot, float absolute_distance, float lane_offset, bool reset_history)
{
	if (!bumper_cars || !current_track || bumper_slot < 0 || bumper_slot >= bumper_count) {
		return;
	}
	SimTransform transform;
	uint16_t checkpoint = 0;
	float checkpoint_fraction = 0.0f;
	if (!sample_track_transform_at_distance(absolute_distance, lane_offset, transform, checkpoint, checkpoint_fraction)) {
		return;
	}
	const SimVec3 surface_origin = transform.origin;
	transform.basis = transform.basis.rotated(transform.basis.get_column(1), Math_PI);
	transform.basis.orthonormalize();
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	const float bumper_surface_offset = 0.0f;
	const float bumper_spawn_height = 19.5f;
	transform.origin += transform.basis.get_column(1) * bumper_surface_offset;
	PhysicsCarSoA& soa = *bumper_cars[bumper_slot].soa;
	const int lane = bumper_cars[bumper_slot].soa_index;
	soa.current_track[lane] = current_track;
	soa.restore_state[lane] = 0;
	soa.restore_wait_frames[lane] = 0;
	soa.restore_move_frames[lane] = 0;
	STORE_INDEXED_VEC3(soa, position_current, lane, transform.origin);
	if (reset_history) {
		STORE_INDEXED_VEC3(soa, position_old, lane, transform.origin);
		STORE_INDEXED_VEC3(soa, position_old_dupe, lane, transform.origin);
	}
	STORE_INDEXED_VEC3(soa, position_bottom, lane, transform.xform(SimVec3(0.0f, -0.1f, 0.0f)));
	STORE_INDEXED_VEC3(soa, track_surface_normal, lane, transform.basis.get_column(1));
	STORE_INDEXED_VEC3(soa, track_surface_pos, lane, surface_origin);
	MXT_STORE_TRANSFORM(soa, basis_physical, lane, transform);
	if (reset_history) {
		MXT_STORE_TRANSFORM(soa, basis_physical_other, lane, transform);
	}
	MXT_STORE_TRANSFORM(soa, transform_visual, lane, transform);
	if (reset_history) {
		const int point_base = lane * 4;
		const SimVec3 reset_position = transform.origin;
		const SimBasis& reset_basis = transform.basis;
		const SimVec3x4 tilt_pos = transform_points_components4(
			reset_basis.c0.x, reset_basis.c0.y, reset_basis.c0.z,
			reset_basis.c1.x, reset_basis.c1.y, reset_basis.c1.z,
			reset_basis.c2.x, reset_basis.c2.y, reset_basis.c2.z,
			reset_position.x, reset_position.y, reset_position.z,
			sim_load4(soa.tilt_offset_x + point_base),
			sim_load4(soa.tilt_offset_y + point_base),
			sim_load4(soa.tilt_offset_z + point_base));
		for (int point = 0; point < 4; ++point) {
			const int p = point_base + point;
			soa.tilt_state[p] = 0;
			soa.tilt_force[p] = 0.0f;
			STORE_INDEXED_VEC3(soa, tilt_force_spatial, p, SimVec3());
			STORE_INDEXED_VEC3(soa, tilt_up_vector_2, p, transform.basis.get_column(1));
			STORE_INDEXED_VEC3(soa, tilt_up_vector, p, transform.basis.get_column(1));
		}
		sim_store4(soa.tilt_pos_old_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_old_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_old_z + point_base, tilt_pos.z);
		sim_store4(soa.tilt_pos_x + point_base, tilt_pos.x);
		sim_store4(soa.tilt_pos_y + point_base, tilt_pos.y);
		sim_store4(soa.tilt_pos_z + point_base, tilt_pos.z);
	}
	soa.current_checkpoint[lane] = checkpoint;
	soa.current_collision_checkpoint[lane] = checkpoint;
	soa.last_ground_checkpoint[lane] = checkpoint;
	soa.checkpoint_fraction[lane] = checkpoint_fraction;
	if (lap_length > 0.0f) {
		soa.checkpoint_track_distance[lane] = std::fmod(absolute_distance, lap_length);
		const double bumper_lap = std::clamp(
			static_cast<double>(absolute_distance) / static_cast<double>(lap_length),
			0.0,
			static_cast<double>(std::numeric_limits<uint32_t>::max()));
		soa.lap[lane] = static_cast<uint32_t>(bumper_lap);
	}
	soa.previous_lap_distance[lane] = current_track->compute_lap_distance(
		soa.current_checkpoint[lane],
		soa.checkpoint_fraction[lane],
		soa.lap[lane]);
	soa.last_ground_distance[lane] = soa.checkpoint_track_distance[lane];
	soa.last_ground_checkpoint[lane] = checkpoint;
	soa.height_above_track[lane] = bumper_spawn_height;
	soa.machine_state[lane] &= ~(MACHINESTATE::FALLOUT | MACHINESTATE::AIRBORNE | MACHINESTATE::AIRBORNEMORE0_2S_Q);
}

void GameSim::update_bumpers(float lead_distance, uint32_t leader_lap)
{
	if (!bumpers_enabled || bumper_count <= 0 || !bumper_cars || !current_track) {
		return;
	}
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	if (lap_length <= 0.0f) {
		return;
	}
	if (leader_lap < 2) {
		return;
	}
	float lap_distance = std::fmod(lead_distance, lap_length);
	if (lap_distance < 0.0f) {
		lap_distance += lap_length;
	}
	const float interval = leader_lap == 2 ? 520.0f : 300.0f;
	const uint32_t scheduler_lap = leader_lap;
	if (bumper_scheduler_lap != scheduler_lap) {
		bumper_scheduler_lap = scheduler_lap;
		bumper_next_sequence = 0;
	}
	for (int slot = 0; slot < bumper_count && slot < BUMPER_POOL_SIZE; ++slot) {
		BumperState& state = bumper_states[slot];
		PhysicsCarSoA& soa = *bumper_cars[slot].soa;
		const int lane = bumper_cars[slot].soa_index;
		if (!state.active) {
			continue;
		}
		if ((soa.machine_state[lane] & MACHINESTATE::ZEROHP) != 0u) {
			spawn_bumper_explosion(slot);
			deactivate_bumper_car(slot);
			continue;
		}
		const float bumper_distance = current_track->compute_lap_distance(
			soa.current_checkpoint[lane],
			soa.checkpoint_fraction[lane],
			soa.lap[lane]);
		if (lead_distance > bumper_distance && soa.energy[lane] > 1.0f) {
			soa.energy[lane] = std::max(
				1.0f,
				soa.energy[lane] - BUMPER_POST_LEADER_DAMAGE_PER_SECOND * _TICK_DELTA);
		}
	}
	const uint32_t sequence_seed = bumper_track_seed ? bumper_track_seed : bumper_track_seed_from_track(current_track);
	const float trigger_distance = bumper_sequence_trigger_distance(
		sequence_seed,
		leader_lap,
		bumper_next_sequence,
		interval,
		lap_length);
	if (lap_distance < trigger_distance || trigger_distance >= lap_length - 80.0f) {
		return;
	}
	int spawn_slot = -1;
	uint32_t oldest_spawn_lap = std::numeric_limits<uint32_t>::max();
	uint32_t oldest_spawn_sequence = std::numeric_limits<uint32_t>::max();
	for (int slot = 0; slot < bumper_count && slot < BUMPER_POOL_SIZE; ++slot) {
		const BumperState& state = bumper_states[slot];
		if (!state.active) {
			spawn_slot = slot;
			break;
		}
		if (state.spawn_lap < oldest_spawn_lap ||
				(state.spawn_lap == oldest_spawn_lap && state.next_sequence < oldest_spawn_sequence)) {
			spawn_slot = slot;
			oldest_spawn_lap = state.spawn_lap;
			oldest_spawn_sequence = state.next_sequence;
		}
	}
	if (spawn_slot < 0) {
		return;
	}
	const uint32_t spawn_sequence = bumper_next_sequence;
	const uint32_t lane_hash = bumper_hash_u32(sequence_seed ^ (spawn_sequence * 0x9E3779B9u) ^ (static_cast<uint32_t>(spawn_slot) * 0x85EBCA6Bu));
	const float spawn_lane = (static_cast<float>(lane_hash & 0xffffu) / 65535.0f) * 1.2f - 0.6f;
	const float spawn_distance = lead_distance + 1000.0f + static_cast<float>(spawn_slot % 3) * 38.0f;
	SimTransform spawn_transform;
	uint16_t cp = 0;
	float cp_fraction = 0.0f;
	if (!sample_track_transform_at_distance(spawn_distance, spawn_lane, spawn_transform, cp, cp_fraction)) {
		return;
	}
	BumperState& state = bumper_states[spawn_slot];
	if (state.active) {
		deactivate_bumper_car(spawn_slot);
	}
	state.target_lane = spawn_lane;
	// A pooled bumper may still carry death, breakdown, collision, and handling
	// state from its previous life. Reset the machine completely before placing
	// it at its new spawn point; its bumper properties remain configured.
	bumper_cars[spawn_slot].initialize_machine();
	PhysicsCarSoA& soa = *bumper_cars[spawn_slot].soa;
	const int lane = bumper_cars[spawn_slot].soa_index;
	set_bumper_track_state(spawn_slot, spawn_distance, spawn_lane, true);
	const SimTransform bumper_transform = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
	const float bumper_weight = std::max(soa.stat_weight[lane], 0.001f);
	SimTransform forward_sample;
	uint16_t forward_cp = 0;
	float forward_fraction = 0.0f;
	SimVec3 cruise_direction;
	if (sample_track_transform_at_distance(spawn_distance + 5.0f, spawn_lane, forward_sample, forward_cp, forward_fraction)) {
		cruise_direction = forward_sample.origin - spawn_transform.origin;
	}
	if (cruise_direction.length_squared() <= 0.000001f) {
		cruise_direction = -bumper_transform.basis.get_column(2);
	}
	if (cruise_direction.length_squared() <= 0.000001f) {
		cruise_direction = SimVec3(0.0f, 0.0f, -1.0f);
	}
	cruise_direction.normalize();
	const SimVec3 cruise_velocity = cruise_direction * (850.0f / 216.0f) * bumper_weight;
	const SimVec3 cruise_delta = cruise_velocity / bumper_weight;
	const SimVec3 spawn_position = LOAD_INDEXED_VEC3(soa, position_current, lane);
	STORE_INDEXED_VEC3(soa, velocity, lane, cruise_velocity);
	STORE_INDEXED_VEC3(soa, velocity_local, lane, SimVec3(0.0f, 0.0f, -(850.0f / 216.0f) * bumper_weight));
	STORE_INDEXED_VEC3(soa, velocity_local_flattened_and_rotated, lane, SimVec3(0.0f, 0.0f, -(850.0f / 216.0f) * bumper_weight));
	STORE_INDEXED_VEC3(soa, position_old, lane, spawn_position - cruise_delta);
	STORE_INDEXED_VEC3(soa, position_old_dupe, lane, spawn_position - cruise_delta);
	soa.energy[lane] = soa.calced_max_energy[lane];
	soa.level_start_time[lane] = static_cast<uint64_t>(tick);
	soa.machine_state[lane] &= ~(MACHINESTATE::ZEROHP | MACHINESTATE::FALLOUT | MACHINESTATE::TOOKDAMAGE | MACHINESTATE::LOWGRIP);
	soa.machine_state[lane] |= MACHINESTATE::ACTIVE;
	soa.frames_since_start_2[lane] = 91;
	soa.speed_kmh[lane] = 850.0f;
	soa.base_speed[lane] = 850.0f / 216.0f;
	soa.boost_frames_manual[lane] = 0;
	soa.boost_frames_dash[lane] = 0;
	soa.boost_duration_manual_frames[lane] = 0;
	soa.boost_duration_dash_frames[lane] = 0;
	soa.s_boost_active[lane] = false;
	soa.height_above_track[lane] = 19.5f;
	state.active = 1;
	state.spawn_lap = scheduler_lap;
	state.next_sequence = spawn_sequence;
	bumper_next_sequence = spawn_sequence + 1u;
}

void GameSim::save_bumper_states_to_saved_state(SavedState& state) const
{
	const int capacity = BUMPER_POOL_SIZE;
	const int count = std::min(bumper_count, capacity);
	state.bumper_state_count = count;
	state.bumper_scheduler_lap = bumper_scheduler_lap;
	state.bumper_next_sequence = bumper_next_sequence;
	for (int i = 0; i < count; ++i) {
		state.bumper_states[i] = bumper_states[i];
	}
}

void GameSim::update_saved_voice_transforms(SavedState& state) const
{
	state.voice_transform_count = 0;
	if (!cars || !car_player_ids || num_cars <= 0) {
		return;
	}
	if (static_cast<int>(state.voice_transforms.size()) < num_cars) {
		state.voice_transforms.resize(num_cars);
	}
	for (int i = 0; i < num_cars; ++i) {
		const int32_t player_id = car_player_ids[i];
		if (player_id < 0) {
			continue;
		}
		const PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		SavedVoiceTransform& dst = state.voice_transforms[state.voice_transform_count++];
		dst.player_id = player_id;
		dst.origin = LOAD_INDEXED_VEC3(soa, position_current, lane);
	}
}

void GameSim::restore_bumper_states_from_saved_state(const SavedState& state)
{
	const int capacity = BUMPER_POOL_SIZE;
	const int count = std::min(std::max(state.bumper_state_count, 0), std::min(bumper_count, capacity));
	bumper_scheduler_lap = state.bumper_scheduler_lap;
	bumper_next_sequence = state.bumper_next_sequence;
	for (int i = 0; i < count; ++i) {
		bumper_states[i] = state.bumper_states[i];
	}
	for (int i = count; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		bumper_states[i].active = 0;
		bumper_states[i].spawn_lap = 0;
		bumper_states[i].next_sequence = static_cast<uint32_t>(i);
		bumper_states[i].target_lane = 0.0f;
	}
	for (int i = 0; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		if (!bumper_cars) {
			continue;
		}
		PhysicsCarSoA& soa = *bumper_cars[i].soa;
		const int lane = bumper_cars[i].soa_index;
		if (bumper_states[i].active) {
			soa.current_track[lane] = current_track;
			soa.restore_state[lane] = 0;
			soa.restore_wait_frames[lane] = 0;
			soa.restore_move_frames[lane] = 0;
		} else {
			deactivate_bumper_car(i);
		}
	}
}
