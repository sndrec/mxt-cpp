#include "gamesim/gamesim_internal.h"

#include "mxt_core/math_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace godot;
void GameSim::reset_super_sparks()
{
	if (!super_spark_state || !super_sparks) {
		return;
	}
	super_spark_state->cursor = 0;
	super_spark_state->placement_timer = 0;
	super_spark_state->rng_state = 1;
	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		super_sparks[i].active = 0;
		super_sparks[i].collectable = 0;
		super_sparks[i].animation_frame = 0;
		super_sparks[i].checkpoint = 0;
		super_sparks[i].position = SimVec3();
		super_sparks[i].prev_position = SimVec3();
		super_sparks[i].start_position = SimVec3();
		super_sparks[i].final_position = SimVec3();
		super_sparks[i].plane_normal = SimVec3(0.0f, 1.0f, 0.0f);
	}
}

uint16_t GameSim::compute_s_boost_duration_frames(float gap_distance) const
{
	float seconds = 3.0f;
	if (gap_distance <= 1000.0f) {
		seconds = 3.0f;
	} else if (gap_distance >= 10000.0f) {
		seconds = 8.0f;
	} else {
		float t = (gap_distance - 1000.0f) / 9000.0f;
		seconds = 3.0f + t * 5.0f;
	}
	uint16_t frames = static_cast<uint16_t>(seconds * 60.0f + 0.5f);
	if (frames < 180u)
		frames = 180u;
	return frames;
}

float GameSim::compute_car_distance_along_track(const PhysicsCar& car) const
{
	return compute_vehicle_distance_along_track(car.soa->current_checkpoint[car.soa_index], car.soa->checkpoint_fraction[car.soa_index], car.soa->lap[car.soa_index]);
}

float GameSim::compute_vehicle_distance_along_track(uint16_t current_checkpoint, float checkpoint_fraction, uint8_t lap) const
{
	if (!current_track)
		return 0.0f;
	return current_track->compute_lap_distance(current_checkpoint, checkpoint_fraction, lap);
}

void GameSim::emit_super_sparks_from_car(const PhysicsCar& car, int count)
{
	if (count <= 0)
		return;
	if (!s_boost_enabled)
		return;
	if (!sim_started || !super_spark_state || !super_sparks || !current_track)
		return;
	if ((car.soa->machine_state[car.soa_index] & MACHINESTATE::STARTINGCOUNTDOWN) != 0)
		return;
	if ((car.soa->machine_state[car.soa_index] & (MACHINESTATE::AIRBORNE | MACHINESTATE::ZEROHP)) != 0)
		return;
	if (car.soa->restore_state[car.soa_index] == 2)
		return;

	constexpr uint64_t kPostCountdownBlockFrames = 180;
	const uint64_t current_frame = static_cast<uint64_t>(car.soa->simulation_tick[car.soa_index]);
	const uint64_t safe_frame = static_cast<uint64_t>(car.soa->level_start_time[car.soa_index]) + kPostCountdownBlockFrames;
	if (current_frame < safe_frame)
		return;

	const uint16_t checkpoint = car.soa->current_checkpoint[car.soa_index];
	if (checkpoint >= static_cast<uint16_t>(current_track->num_checkpoints))
		return;
	const SimVec3 car_position = LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index);
	SimVec3 normal_in = LOAD_INDEXED_VEC3(*car.soa, track_surface_normal, car.soa_index);
	if (normal_in.length_squared() <= 0.0001f) {
		normal_in = SimVec3(0.0f, 1.0f, 0.0f);
	} else {
		normal_in = normal_in.normalized();
	}
	SimVec3 tangent_a = car.soa->road_sample[car.soa_index].closest_surface.basis.get_column(0);
	if (tangent_a.length_squared() < 0.0001f) {
		tangent_a = normal_in.cross(SimVec3(0.0f, 0.0f, 1.0f));
	}
	if (tangent_a.length_squared() < 0.0001f) {
		tangent_a = normal_in.cross(SimVec3(1.0f, 0.0f, 0.0f));
	}
	tangent_a = tangent_a.slide(normal_in).normalized();
	SimVec3 tangent_b = normal_in.cross(tangent_a).normalized();

	auto next_rand = [&]() -> float {
		super_spark_state->rng_state = super_spark_state->rng_state * 1664525u + 1013904223u;
		return static_cast<float>(super_spark_state->rng_state & 0x00FFFFFFu) / 16777215.0f;
	};
	auto rand_range = [&](float min_v, float max_v) -> float {
		return min_v + (max_v - min_v) * next_rand();
	};

	for (int n = 0; n < count; ++n) {
		const uint16_t cursor = super_spark_state->cursor;
		SuperSpark& spark = super_sparks[cursor];
		super_spark_state->cursor = static_cast<uint16_t>((cursor + 1) % SUPER_SPARK_CAPACITY);

		const float lateral_a = rand_range(-18.0f, 18.0f);
		const float lateral_b = rand_range(-10.0f, 10.0f);
		const SimVec3 sample_point = car_position + tangent_a * lateral_a + tangent_b * lateral_b;
		SimVec2 road_t;
		SimVec3 spatial_t;
		SimTransform surface;
		current_track->get_road_surface(checkpoint, sample_point, road_t, spatial_t, surface, true);
		SimVec3 surface_normal = surface.basis.get_column(1);
		if (surface_normal.length_squared() <= 0.0001f) {
			surface_normal = normal_in;
		} else {
			surface_normal = surface_normal.normalized();
		}
		const SimVec3 final_position = surface.origin + surface_normal * 1.0f;

		spark.active = 1;
		spark.collectable = 0;
		spark.animation_frame = 0;
		spark.checkpoint = checkpoint;
		spark.plane_normal = surface_normal;
		spark.start_position = car_position;
		spark.final_position = final_position;
		spark.position = car_position;
		spark.prev_position = car_position;
	}
}

void GameSim::update_super_sparks()
{
	if (!s_boost_enabled) {
		return;
	}
	if (!sim_started || !cars || !super_spark_state || !super_sparks)
		return;

	const float collect_radius_sq = SUPER_SPARK_COLLECT_RADIUS * SUPER_SPARK_COLLECT_RADIUS;
	const bool use_checkpoint_masks =
		current_track &&
		current_track->num_checkpoints > 0 &&
		num_cars > 0 &&
		num_cars <= 128;
	if (use_checkpoint_masks) {
		const int checkpoint_count = current_track->num_checkpoints;
		if (static_cast<int>(super_spark_candidate_mask_lo.size()) < checkpoint_count) {
			super_spark_candidate_mask_lo.resize(checkpoint_count);
			super_spark_candidate_mask_hi.resize(checkpoint_count);
		}
		std::fill(super_spark_candidate_mask_lo.begin(), super_spark_candidate_mask_lo.begin() + checkpoint_count, 0ull);
		std::fill(super_spark_candidate_mask_hi.begin(), super_spark_candidate_mask_hi.begin() + checkpoint_count, 0ull);
		auto add_car_candidate = [&](int checkpoint, int car_index) {
			if (checkpoint < 0 || checkpoint >= checkpoint_count) {
				return;
			}
			if (car_index < 64) {
				super_spark_candidate_mask_lo[checkpoint] |= 1ull << car_index;
			} else {
				super_spark_candidate_mask_hi[checkpoint] |= 1ull << (car_index - 64);
			}
		};
		for (int car_idx = 0; car_idx < num_cars; ++car_idx) {
			PhysicsCarSoA& car_soa = *cars[car_idx].soa;
			const int lane = cars[car_idx].soa_index;
			if (car_soa.s_boost_active[lane] || (car_soa.machine_state[lane] & MACHINESTATE::ZEROHP) != 0) {
				continue;
			}
			const int car_checkpoint = car_soa.current_checkpoint[lane];
			if (car_checkpoint < 0 || car_checkpoint >= checkpoint_count) {
				continue;
			}
			add_car_candidate(car_checkpoint, car_idx);
			const CollisionCheckpoint& cp = current_track->checkpoints[car_checkpoint];
			for (int n = 0; n < cp.num_neighboring_checkpoints; ++n) {
				if (cp.neighboring_checkpoints) {
					add_car_candidate(cp.neighboring_checkpoints[n], car_idx);
				}
			}
		}
	}
	auto checkpoint_matches = [&](uint16_t spark_checkpoint, uint16_t car_checkpoint) -> bool {
		if (!current_track || car_checkpoint >= static_cast<uint16_t>(current_track->num_checkpoints))
			return false;
		if (spark_checkpoint == car_checkpoint)
			return true;
		const CollisionCheckpoint& cp = current_track->checkpoints[car_checkpoint];
		for (int n = 0; n < cp.num_neighboring_checkpoints; ++n) {
			if (cp.neighboring_checkpoints && cp.neighboring_checkpoints[n] == spark_checkpoint) {
				return true;
			}
		}
		return false;
	};

	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		SuperSpark& spark = super_sparks[i];
		if (!spark.active)
			continue;
		spark.prev_position = spark.position;

		if (!spark.collectable) {
			spark.position = mxt_super_spark_position_at_frame(
				spark.start_position, spark.final_position, spark.plane_normal, spark.animation_frame, spark.collectable);
			if (spark.animation_frame >= MXT_SUPER_SPARK_ANIMATION_FRAMES) {
				spark.position = spark.final_position;
				spark.collectable = 1;
			} else {
				spark.animation_frame += 1;
				continue;
			}
		}

		auto try_collect_car = [&](int car_idx) -> bool {
			PhysicsCarSoA& car_soa = *cars[car_idx].soa;
			const int lane = cars[car_idx].soa_index;
			if (car_soa.s_boost_active[lane] || (car_soa.machine_state[lane] & MACHINESTATE::ZEROHP) != 0)
				return false;
			if (!use_checkpoint_masks && !checkpoint_matches(spark.checkpoint, car_soa.current_checkpoint[lane]))
				return false;
			SimVec3 closest = get_closest_point_to_segment(
				spark.position, LOAD_INDEXED_VEC3(car_soa, position_old, lane), LOAD_INDEXED_VEC3(car_soa, position_current, lane));
			float dist_sq = spark.position.distance_squared_to(closest);
			if (dist_sq <= collect_radius_sq) {
				if (car_soa.s_boost_charge[lane] < car_soa.s_boost_charge_max[lane]) {
					car_soa.s_boost_charge[lane] += 1;
				}
				car_soa.base_speed[lane] += 0.05f;
				spark.active = 0;
				spark.collectable = 0;
				return true;
			}
			return false;
		};
		if (use_checkpoint_masks && spark.checkpoint < static_cast<uint16_t>(current_track->num_checkpoints)) {
			auto collect_candidate_mask = [&](uint64_t bits, int base_car_idx) {
				while (bits != 0 && spark.active) {
					uint64_t scan = bits;
					int bit_index = 0;
					while ((scan & 1ull) == 0ull) {
						scan >>= 1;
						++bit_index;
					}
					const int car_idx = base_car_idx + bit_index;
					bits &= bits - 1ull;
					if (car_idx < num_cars) {
						try_collect_car(car_idx);
					}
				}
			};
			collect_candidate_mask(super_spark_candidate_mask_lo[spark.checkpoint], 0);
			collect_candidate_mask(super_spark_candidate_mask_hi[spark.checkpoint], 64);
		} else {
			for (int car_idx = 0; car_idx < num_cars; ++car_idx) {
				if (try_collect_car(car_idx)) {
					break;
				}
			}
		}
	}
}
