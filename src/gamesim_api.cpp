#include "gamesim_internal.h"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "mxt_core/debug.hpp"
#include "mxt_core/spatial_audio_manager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace godot;
void GameSim::set_sim_started(const bool p_sim_started)
{
	sim_started = p_sim_started;
	spatial_audio_last_assignment_tick = -1;
	spatial_audio_last_update_frame = UINT64_MAX;
	if (!sim_started && spatial_audio_manager) {
		spatial_audio_manager->stop_emitters();
	}
}

bool GameSim::get_sim_started()
{
	return sim_started;
}

String GameSim::get_phase_profile_string() const
{
	if (!phase_profile_enabled || phase_profile_frames == 0) {
		return "MXT_PHASE_PROFILE_DISABLED";
	}
	auto avg = [](uint64_t total, uint64_t frames) -> godot::String {
		if (frames == 0) {
			return "0";
		}
		return godot::String::num_int64(static_cast<int64_t>(total / frames));
	};
	godot::String out = "MXT_PHASE_PROFILE frames=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_frames));
	out += " total_us=" + avg(phase_profile_total_us, phase_profile_frames);
	out += " total_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_total_max_us));
	out += " pre_us=" + avg(phase_profile_pre_us, phase_profile_frames);
	out += " pre_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_pre_max_us));
	out += " input_us=" + avg(phase_profile_input_us, phase_profile_frames);
	out += " input_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_input_max_us));
	out += " vehicle_us=" + avg(phase_profile_vehicle_us, phase_profile_frames);
	out += " vehicle_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_vehicle_max_us));
	out += " v_begin_us=" + avg(phase_profile_vehicle_begin_us, phase_profile_frames);
	out += " v_apply_input_us=" + avg(phase_profile_vehicle_apply_input_us, phase_profile_frames);
	out += " v_floor_us=" + avg(phase_profile_vehicle_floor_us, phase_profile_frames);
	out += " v_prepare_frame_us=" + avg(phase_profile_vehicle_prepare_frame_us, phase_profile_frames);
	out += " v_floor_corner_analytic_surface_us=" + avg(phase_profile_vehicle_floor_corner_analytic_surface_us, phase_profile_frames);
	out += " v_floor_mesh_candidate_collect_us=" + avg(phase_profile_vehicle_floor_mesh_candidate_collect_us, phase_profile_frames);
	out += " v_floor_mesh_cast4_us=" + avg(phase_profile_vehicle_floor_mesh_cast4_us, phase_profile_frames);
	out += " v_floor_mesh_sample_us=" + avg(phase_profile_vehicle_floor_mesh_sample_us, phase_profile_frames);
	out += " v_find_floor_us=" + avg(phase_profile_vehicle_find_floor_us, phase_profile_frames);
	out += " v_find_floor_cast_us=" + avg(phase_profile_vehicle_find_floor_cast_us, phase_profile_frames);
	out += " v_find_floor_mesh_us=" + avg(phase_profile_vehicle_find_floor_mesh_us, phase_profile_frames);
	out += " v_find_floor_analytic_us=" + avg(phase_profile_vehicle_find_floor_analytic_us, phase_profile_frames);
	out += " v_terrain_us=" + avg(phase_profile_vehicle_terrain_us, phase_profile_frames);
	out += " v_trigger_us=" + avg(phase_profile_vehicle_trigger_us, phase_profile_frames);
	out += " v_motion_us=" + avg(phase_profile_vehicle_motion_us, phase_profile_frames);
	out += " v_finish_tick_us=" + avg(phase_profile_vehicle_finish_tick_us, phase_profile_frames);
	out += " v_collision_us=" + avg(phase_profile_vehicle_collision_us, phase_profile_frames);
	out += " v_post_tick_us=" + avg(phase_profile_vehicle_post_tick_us, phase_profile_frames);
	out += " v_corner_update_us=" + avg(phase_profile_vehicle_corner_update_us, phase_profile_frames);
	out += " v_corner_old_analytic_us=" + avg(phase_profile_vehicle_corner_old_analytic_us, phase_profile_frames);
	out += " v_corner_new_checkpoint_us=" + avg(phase_profile_vehicle_corner_new_checkpoint_us, phase_profile_frames);
	out += " v_corner_new_analytic_us=" + avg(phase_profile_vehicle_corner_new_analytic_us, phase_profile_frames);
	out += " v_corner_mesh_us=" + avg(phase_profile_vehicle_corner_mesh_us, phase_profile_frames);
	out += " v_tail_us=" + avg(phase_profile_vehicle_tail_us, phase_profile_frames);
	out += " v_checkpoint_us=" + avg(phase_profile_vehicle_checkpoint_us, phase_profile_frames);
	out += " v_spark_collect_us=" + avg(phase_profile_vehicle_spark_collect_us, phase_profile_frames);
	out += " post_vehicle_us=" + avg(phase_profile_post_vehicle_us, phase_profile_frames);
	out += " post_vehicle_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_post_vehicle_max_us));
	out += " placement_us=" + avg(phase_profile_placement_us, phase_profile_frames);
	out += " placement_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_placement_max_us));
	out += " post_us=" + avg(phase_profile_post_us, phase_profile_frames);
	out += " post_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_post_max_us));
	out += " save_us=" + avg(phase_profile_save_us, phase_profile_frames);
	out += " save_max_us=" + godot::String::num_int64(static_cast<int64_t>(phase_profile_save_max_us));
	out += " save_bumper_us=" + avg(phase_profile_save_bumper_us, phase_profile_frames);
	out += " save_voice_us=" + avg(phase_profile_save_voice_us, phase_profile_frames);
	out += " save_memcpy_us=" + avg(phase_profile_save_memcpy_us, phase_profile_frames);
	return out;
}

godot::PackedInt64Array GameSim::get_phase_profile_last_sample() const
{
	godot::PackedInt64Array sample;
	sample.resize(9);
	sample.set(0, static_cast<int64_t>(phase_profile_last_total_us));
	sample.set(1, static_cast<int64_t>(phase_profile_last_pre_us));
	sample.set(2, static_cast<int64_t>(phase_profile_last_input_us));
	sample.set(3, static_cast<int64_t>(phase_profile_last_vehicle_us));
	sample.set(4, static_cast<int64_t>(phase_profile_last_vehicle_collision_us));
	sample.set(5, static_cast<int64_t>(phase_profile_last_post_vehicle_us));
	sample.set(6, static_cast<int64_t>(phase_profile_last_placement_us));
	sample.set(7, static_cast<int64_t>(phase_profile_last_post_us));
	sample.set(8, static_cast<int64_t>(phase_profile_last_save_us));
	return sample;
}

uint64_t GameSim::render_profile_now_us() const
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

String GameSim::get_render_profile_string() const
{
	if (!render_profile_enabled || render_profile_frames == 0) {
		return "MXT_RENDER_PROFILE_DISABLED";
	}
	auto avg = [](uint64_t total, uint64_t frames) -> godot::String {
		if (frames == 0) {
			return "0";
		}
		return godot::String::num_int64(static_cast<int64_t>(total / frames));
	};
	godot::String out = "MXT_RENDER_PROFILE_CPP frames=" + godot::String::num_int64(static_cast<int64_t>(render_profile_frames));
	out += " total_us=" + avg(render_profile_total_us, render_profile_frames);
	out += " total_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_total_max_us));
	out += " get_children_us=" + avg(render_profile_get_children_us, render_profile_frames);
	out += " cache_us=" + avg(render_profile_cache_us, render_profile_frames);
	out += " snapshots_us=" + avg(render_profile_snapshots_us, render_profile_frames);
	out += " snapshots_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_snapshots_max_us));
	out += " effects_us=" + avg(render_profile_effects_us, render_profile_frames);
	out += " effects_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_effects_max_us));
	out += " multimesh_us=" + avg(render_profile_multimesh_us, render_profile_frames);
	out += " multimesh_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_multimesh_max_us));
	out += " body_instances=" + avg(render_profile_body_instances, render_profile_frames);
	out += " thruster_instances=" + avg(render_profile_thruster_instances, render_profile_frames);
	out += " camera_us=" + avg(render_profile_camera_us, render_profile_frames);
	out += " local_visual_us=" + avg(render_profile_local_visual_us, render_profile_frames);
	out += " cpu_driver_us=" + avg(render_profile_cpu_driver_us, render_profile_frames);
	out += " cpu_driver_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_cpu_driver_max_us));
	out += " spark_us=" + avg(render_profile_spark_us, render_profile_frames);
	out += " visuals_only_frames=" + godot::String::num_int64(static_cast<int64_t>(render_profile_visuals_only_frames));
	out += " visuals_only_total_us=" + avg(render_profile_visuals_only_total_us, render_profile_visuals_only_frames);
	out += " visuals_only_total_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_visuals_only_total_max_us));
	out += " visuals_only_effects_us=" + avg(render_profile_visuals_only_effects_us, render_profile_visuals_only_frames);
	out += " visuals_only_effects_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_visuals_only_effects_max_us));
	out += " visuals_only_multimesh_us=" + avg(render_profile_visuals_only_multimesh_us, render_profile_visuals_only_frames);
	out += " visuals_only_multimesh_max_us=" + godot::String::num_int64(static_cast<int64_t>(render_profile_visuals_only_multimesh_max_us));
	out += " visuals_only_body_instances=" + avg(render_profile_visuals_only_body_instances, render_profile_visuals_only_frames);
	out += " visuals_only_thruster_instances=" + avg(render_profile_visuals_only_thruster_instances, render_profile_visuals_only_frames);
	out += " visuals_only_camera_us=" + avg(render_profile_visuals_only_camera_us, render_profile_visuals_only_frames);
	return out;
}

godot::PackedInt64Array GameSim::get_render_profile_last_sample() const
{
	godot::PackedInt64Array sample;
	sample.resize(2);
	sample.set(0, static_cast<int64_t>(std::max(render_last_body_instances, 0)));
	sample.set(1, static_cast<int64_t>(std::max(render_last_thruster_instances, 0)));
	return sample;
}

void GameSim::set_phase_profile_enabled(bool enabled)
{
	phase_profile_enabled = enabled;
	phase_profile_frames = 0;
	phase_profile_total_us = 0;
	phase_profile_total_max_us = 0;
	phase_profile_pre_us = 0;
	phase_profile_pre_max_us = 0;
	phase_profile_input_us = 0;
	phase_profile_input_max_us = 0;
	phase_profile_vehicle_us = 0;
	phase_profile_vehicle_max_us = 0;
	phase_profile_vehicle_begin_us = 0;
	phase_profile_vehicle_apply_input_us = 0;
	phase_profile_vehicle_floor_us = 0;
	phase_profile_vehicle_prepare_frame_us = 0;
	phase_profile_vehicle_floor_corner_analytic_surface_us = 0;
	phase_profile_vehicle_floor_mesh_candidate_collect_us = 0;
	phase_profile_vehicle_floor_mesh_cast4_us = 0;
	phase_profile_vehicle_floor_mesh_sample_us = 0;
	phase_profile_vehicle_find_floor_us = 0;
	phase_profile_vehicle_find_floor_cast_us = 0;
	phase_profile_vehicle_find_floor_mesh_us = 0;
	phase_profile_vehicle_find_floor_analytic_us = 0;
	phase_profile_vehicle_terrain_us = 0;
	phase_profile_vehicle_trigger_us = 0;
	phase_profile_vehicle_motion_us = 0;
	phase_profile_vehicle_finish_tick_us = 0;
	phase_profile_vehicle_collision_us = 0;
	phase_profile_vehicle_post_tick_us = 0;
	phase_profile_vehicle_corner_update_us = 0;
	phase_profile_vehicle_corner_old_analytic_us = 0;
	phase_profile_vehicle_corner_new_checkpoint_us = 0;
	phase_profile_vehicle_corner_new_analytic_us = 0;
	phase_profile_vehicle_corner_mesh_us = 0;
	phase_profile_vehicle_tail_us = 0;
	phase_profile_vehicle_checkpoint_us = 0;
	phase_profile_vehicle_spark_collect_us = 0;
	phase_profile_post_vehicle_us = 0;
	phase_profile_post_vehicle_max_us = 0;
	phase_profile_placement_us = 0;
	phase_profile_placement_max_us = 0;
	phase_profile_post_us = 0;
	phase_profile_post_max_us = 0;
	phase_profile_save_us = 0;
	phase_profile_save_max_us = 0;
	phase_profile_save_bumper_us = 0;
	phase_profile_save_voice_us = 0;
	phase_profile_save_memcpy_us = 0;
	phase_profile_last_total_us = 0;
	phase_profile_last_pre_us = 0;
	phase_profile_last_input_us = 0;
	phase_profile_last_vehicle_us = 0;
	phase_profile_last_vehicle_collision_us = 0;
	phase_profile_last_post_vehicle_us = 0;
	phase_profile_last_placement_us = 0;
	phase_profile_last_post_us = 0;
	phase_profile_last_save_us = 0;
}

void GameSim::set_render_profile_enabled(bool enabled)
{
	render_profile_enabled = enabled;
	render_profile_frames = 0;
	render_profile_total_us = 0;
	render_profile_total_max_us = 0;
	render_profile_get_children_us = 0;
	render_profile_cache_us = 0;
	render_profile_snapshots_us = 0;
	render_profile_snapshots_max_us = 0;
	render_profile_effects_us = 0;
	render_profile_effects_max_us = 0;
	render_profile_multimesh_us = 0;
	render_profile_multimesh_max_us = 0;
	render_profile_body_instances = 0;
	render_profile_thruster_instances = 0;
	render_profile_camera_us = 0;
	render_profile_local_visual_us = 0;
	render_profile_cpu_driver_us = 0;
	render_profile_cpu_driver_max_us = 0;
	render_profile_spark_us = 0;
	render_profile_visuals_only_frames = 0;
	render_profile_visuals_only_total_us = 0;
	render_profile_visuals_only_total_max_us = 0;
	render_profile_visuals_only_effects_us = 0;
	render_profile_visuals_only_effects_max_us = 0;
	render_profile_visuals_only_multimesh_us = 0;
	render_profile_visuals_only_multimesh_max_us = 0;
	render_profile_visuals_only_body_instances = 0;
	render_profile_visuals_only_thruster_instances = 0;
	render_profile_visuals_only_camera_us = 0;
}

void GameSim::set_render_node_effects_enabled(bool enabled)
{
	render_node_effects_enabled = enabled;
	if (!enabled) {
		for (RenderEffectPoolSlot& slot : render_effect_pool_slots) {
			if (slot.recharge_particles) {
				slot.recharge_particles->set_emitting(false);
			}
			if (slot.attack_particles) {
				slot.attack_particles->set_emitting(false);
			}
			if (slot.landing_particles) {
				slot.landing_particles->set_emitting(false);
			}
			if (slot.damage_electricity) {
				slot.damage_electricity->set_visible(false);
				slot.damage_electricity->set_emitting(false);
				slot.damage_electricity->set_amount_ratio(0.0);
			}
			if (slot.damage_smoke) {
				slot.damage_smoke->set_visible(false);
				slot.damage_smoke->set_emitting(false);
				slot.damage_smoke->set_amount_ratio(0.0);
			}
			if (slot.boost_electricity) {
				slot.boost_electricity->set("boosting", false);
				slot.boost_electricity->set("visible", false);
			}
			slot.car_index = -1;
		}
	}
}

void GameSim::set_render_thruster_lights_enabled(bool enabled)
{
	render_thruster_lights_enabled = enabled;
	if (!enabled) {
		hide_unused_render_thruster_lights(0);
	}
}

void GameSim::set_render_all_car_bodies(bool enabled)
{
	render_all_car_bodies = enabled;
}

void GameSim::set_render_car_body_view_distance(double distance)
{
	render_car_body_view_distance = std::max(0.0f, static_cast<float>(distance));
}

void GameSim::set_start_grid_slots(godot::PackedInt32Array p_slots)
{
	start_grid_slots.clear();
	start_grid_slots.reserve(p_slots.size());
	for (int i = 0; i < p_slots.size(); ++i) {
		start_grid_slots.push_back(p_slots[i]);
	}
}

int GameSim::get_player_race_place(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0 ||
			!vehicle_tick_soa.placement_order_valid ||
			!vehicle_tick_soa.placement_indices) {
		return 0;
	}

	int place = 1;
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index < 0 || car_index >= num_cars) {
			continue;
		}
		if (car_player_ids[car_index] < 0) {
			continue;
		}
		if (car_player_ids[car_index] == player_id) {
			return place;
		}
		place += 1;
	}
	return 0;
}

godot::PackedInt32Array GameSim::get_race_leaderboard_window(int player_id, int max_entries, const godot::Dictionary& finished_players, const godot::Dictionary& eliminated_players) const
{
	godot::PackedInt32Array window;
	if (!cars || !car_player_ids || num_cars <= 0 ||
			!vehicle_tick_soa.placement_order_valid ||
			!vehicle_tick_soa.placement_indices) {
		return window;
	}
	int ranked_count = 0;
	int focus_rank = -1;
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index >= 0 && car_index < num_cars && car_player_ids[car_index] >= 0) {
			if (car_player_ids[car_index] == player_id) {
				focus_rank = ranked_count;
			}
			ranked_count += 1;
		}
	}
	if (ranked_count == 0) {
		return window;
	}
	const int entry_count = std::max(1, std::min(max_entries, ranked_count));
	if (focus_rank < 0) {
		focus_rank = 0;
	}

	const int half = entry_count >> 1;
	int start = focus_rank - half;
	const int max_start = ranked_count - entry_count;
	if (start < 0) {
		start = 0;
	}
	if (start > max_start) {
		start = max_start;
	}

	window.resize(1 + entry_count * 2);
	int out_index = 1;
	int live_place = finished_players.size() + 1;
	int ranked_index = 0;
	int focus_place = focus_rank + 1;
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index < 0 || car_index >= num_cars) {
			continue;
		}
		const int ranked_player_id = car_player_ids[car_index];
		if (ranked_player_id < 0) {
			continue;
		}
		int displayed_place = ranked_index + 1;
		if (!finished_players.has(ranked_player_id) && !eliminated_players.has(ranked_player_id)) {
			displayed_place = live_place;
			live_place += 1;
		}
		if (ranked_player_id == player_id) {
			focus_place = displayed_place;
		}
		if (ranked_index >= start && ranked_index < start + entry_count) {
			window.set(out_index++, ranked_player_id);
			window.set(out_index++, displayed_place);
		}
		ranked_index += 1;
	}
	window.set(0, focus_place);
	return window;
}

int GameSim::get_race_control_start_tick() const
{
	if (!cars || num_cars <= 0) {
		return 300;
	}
	uint64_t start_tick = 0;
	for (int i = 0; i < num_cars; ++i) {
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		start_tick = std::max(start_tick, car_soa.level_start_time[lane]);
	}
	if (start_tick == 0) {
		return 300;
	}
	return static_cast<int>(std::min<uint64_t>(start_tick, static_cast<uint64_t>(INT32_MAX)));
}

godot::PackedInt32Array GameSim::get_finished_player_ids() const
{
	godot::PackedInt32Array result;
	if (!cars || !car_player_ids || num_cars <= 0) {
		return result;
	}
	for (int i = 0; i < num_cars; ++i) {
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if ((car_soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u) {
			result.push_back(car_player_ids[i]);
		}
	}
	return result;
}

godot::PackedInt32Array GameSim::get_eliminated_player_ids() const
{
	godot::PackedInt32Array result;
	if (vehicle_restore_enabled || !cars || !car_player_ids || num_cars <= 0) {
		return result;
	}
	for (int i = 0; i < num_cars; ++i) {
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if (vehicle_restore_off_eliminated(car_soa, lane)) {
			result.push_back(car_player_ids[i]);
		}
	}
	return result;
}

bool GameSim::is_player_race_finished(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return false;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return (car_soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u;
	}
	return false;
}

bool GameSim::is_player_race_eliminated(int player_id) const
{
	if (vehicle_restore_enabled || !cars || !car_player_ids || num_cars <= 0) {
		return false;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return vehicle_restore_off_eliminated(car_soa, lane);
	}
	return false;
}

double GameSim::get_player_ko_energy_bonus(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0.0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<double>(car_soa.ko_energy_bonus[lane]);
	}
	return 0.0;
}

void GameSim::set_player_ko_energy_bonus(int player_id, double bonus)
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return;
	}
	const float clamped_bonus = std::max(0.0f, static_cast<float>(bonus));
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		car_soa.ko_energy_bonus[lane] = clamped_bonus;
		if (car_soa.car_properties[lane]) {
			car_soa.calced_max_energy[lane] =
				car_soa.car_properties[lane]->max_energy + car_soa.ko_energy_bonus[lane];
		}
		car_soa.energy[lane] = car_soa.calced_max_energy[lane];
		return;
	}
}

double GameSim::get_player_lap_distance(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0.0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		return static_cast<double>(compute_car_distance_along_track(cars[i]));
	}
	return 0.0;
}

int GameSim::get_player_lap(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<int>(car_soa.lap[lane]);
	}
	return 0;
}

int GameSim::get_player_level_start_time(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 300;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<int>(std::min<uint64_t>(car_soa.level_start_time[lane], static_cast<uint64_t>(INT32_MAX)));
	}
	return 300;
}

double GameSim::get_player_speed_kmh(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return 0.0;
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		return static_cast<double>(car_soa.speed_kmh[lane]);
	}
	return 0.0;
}

godot::String GameSim::get_player_debug_string(int player_id) const
{
	if (!cars || !car_player_ids || num_cars <= 0) {
		return "missing cars";
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const SimVec3 pos = LOAD_INDEXED_VEC3(car_soa, position_current, lane);
		const SimVec3 vel = LOAD_INDEXED_VEC3(car_soa, velocity, lane);
		const SimVec3 track_normal = LOAD_INDEXED_VEC3(car_soa, track_surface_normal, lane);
		const SimVec3 up = MXT_LOAD_TRANSFORM(car_soa, basis_physical, lane).basis.get_column(1);
		godot::String out = "id=" + godot::String::num_int64(player_id);
		out += " cp=" + godot::String::num_int64(car_soa.current_checkpoint[lane]);
		out += " last_ground_cp=" + godot::String::num_int64(car_soa.last_ground_checkpoint[lane]);
		out += " frac=" + godot::String::num(car_soa.checkpoint_fraction[lane]);
		out += " lap=" + godot::String::num_int64(car_soa.lap[lane]);
		out += " dist=" + godot::String::num(compute_car_distance_along_track(cars[i]));
		out += " track_dist=" + godot::String::num(car_soa.checkpoint_track_distance[lane]);
		out += " last_ground_dist=" + godot::String::num(car_soa.last_ground_distance[lane]);
		out += " restore=" + godot::String::num_int64(car_soa.restore_state[lane]);
		out += " rollback=" + godot::String(car_soa.broken_lap_rollback_pending[lane] ? "1" : "0");
		out += ":" + godot::String::num_int64(car_soa.broken_lap_rollback_lap[lane]);
		out += " rail_timer=" + godot::String::num_int64(car_soa.rail_collision_timer[lane]);
		out += " last_hit=" + godot::String(car_soa.has_last_hit_tick[lane] ? "1" : "0");
		out += ":" + godot::String::num_int64(car_soa.last_hit_tick[lane]);
		out += ":" + godot::String::num(car_soa.last_hit_sfx_strength[lane]);
		out += " pos=(" + godot::String::num(pos.x) + "," + godot::String::num(pos.y) + "," + godot::String::num(pos.z) + ")";
		out += " vel=(" + godot::String::num(vel.x) + "," + godot::String::num(vel.y) + "," + godot::String::num(vel.z) + ")";
		out += " speed=" + godot::String::num(car_soa.speed_kmh[lane]);
		out += " h=" + godot::String::num(car_soa.height_above_track[lane]);
		out += " n=(" + godot::String::num(track_normal.x) + "," + godot::String::num(track_normal.y) + "," + godot::String::num(track_normal.z) + ")";
		out += " up=(" + godot::String::num(up.x) + "," + godot::String::num(up.y) + "," + godot::String::num(up.z) + ")";
		out += " state=0x" + godot::String::num_int64(car_soa.machine_state[lane], 16);
		out += " terrain=0x" + godot::String::num_int64(car_soa.terrain_state[lane], 16);
		return out;
	}
	return "missing player";
}

godot::String GameSim::get_bumper_debug_string() const
{
	godot::String out = "enabled=" + godot::String(bumpers_enabled ? "1" : "0");
	out += " count=" + godot::String::num_int64(bumper_count);
	out += " start=" + godot::String::num_int64(num_cars);
	if (!cars || !bumper_cars || bumper_count <= 0) {
		out += " active=0";
		return out;
	}
	float lead_distance = 0.0f;
	int leader_lap = 0;
	for (int i = 0; i < num_cars; ++i) {
		const PhysicsCarSoA& car_soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const float distance = compute_vehicle_distance_along_track(
			car_soa.current_checkpoint[lane],
			car_soa.checkpoint_fraction[lane],
			car_soa.lap[lane]);
		if (distance > lead_distance) {
			lead_distance = distance;
			leader_lap = static_cast<int>(car_soa.lap[lane]);
		}
	}
	if (num_cars > 0) {
		const PhysicsCarSoA& racer_soa = *cars[0].soa;
		const int racer_lane = cars[0].soa_index;
		out += " racer0_cp=" + godot::String::num_int64(racer_soa.current_checkpoint[racer_lane]);
		out += " racer0_coll=" + godot::String::num_int64(racer_soa.current_collision_checkpoint[racer_lane]);
		out += " racer0_lap=" + godot::String::num_int64(racer_soa.lap[racer_lane]);
		out += " racer0_state=0x" + godot::String::num_int64(racer_soa.machine_state[racer_lane], 16);
	}
	out += " leader_lap=" + godot::String::num_int64(leader_lap);
	out += " lead_dist=" + godot::String::num(lead_distance);
	int active_count = 0;
	int first_active = -1;
	for (int slot = 0; slot < bumper_count && slot < BUMPER_POOL_SIZE; ++slot) {
		if (bumper_states[slot].active) {
			if (first_active < 0) {
				first_active = slot;
			}
			++active_count;
		}
	}
	out += " active=" + godot::String::num_int64(active_count);
	if (first_active < 0) {
		return out;
	}
	if (first_active < 0 || first_active >= bumper_count) {
		out += " first_oob=1";
		return out;
	}
	const PhysicsCarSoA& car_soa = *bumper_cars[first_active].soa;
	const int lane = bumper_cars[first_active].soa_index;
	const SimVec3 pos = LOAD_INDEXED_VEC3(car_soa, position_current, lane);
	const SimVec3 vel = LOAD_INDEXED_VEC3(car_soa, velocity, lane);
	out += " first_slot=" + godot::String::num_int64(first_active);
	out += " target_lane=" + godot::String::num(bumper_states[first_active].target_lane);
	out += " cp=" + godot::String::num_int64(car_soa.current_checkpoint[lane]);
	out += " coll_cp=" + godot::String::num_int64(car_soa.current_collision_checkpoint[lane]);
	out += " lap=" + godot::String::num_int64(car_soa.lap[lane]);
	out += " dist=" + godot::String::num(compute_car_distance_along_track(bumper_cars[first_active]));
	out += " road_x=" + godot::String::num(car_soa.road_sample[lane].road_t.x);
	out += " speed=" + godot::String::num(car_soa.speed_kmh[lane]);
	out += " base=" + godot::String::num(car_soa.base_speed[lane]);
	out += " restore=" + godot::String::num_int64(car_soa.restore_state[lane]);
	out += " state2=0x" + godot::String::num_int64(car_soa.state_2[lane], 16);
	out += " pos=(" + godot::String::num(pos.x) + "," + godot::String::num(pos.y) + "," + godot::String::num(pos.z) + ")";
	out += " vel=(" + godot::String::num(vel.x) + "," + godot::String::num(vel.y) + "," + godot::String::num(vel.z) + ")";
	out += " state=0x" + godot::String::num_int64(car_soa.machine_state[lane], 16);
	return out;
}

godot::Array GameSim::get_race_order()
{
	godot::Array order;
	if (!cars || !car_player_ids || num_cars <= 0 ||
			!vehicle_tick_soa.placement_order_valid ||
			!vehicle_tick_soa.placement_indices) {
		return order;
	}
	for (int i = 0; i < num_cars; ++i) {
		const int car_index = vehicle_tick_soa.placement_indices[i];
		if (car_index >= 0 && car_index < num_cars && car_player_ids[car_index] >= 0) {
			order.append(car_player_ids[car_index]);
		}
	}
	return order;
}

godot::Transform3D GameSim::get_player_render_transform(int player_id) const
{
	if (!car_player_ids || render_final_current_transforms.empty()) {
		return godot::Transform3D();
	}
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		SimTransform render_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		return gd_transform(render_transform);
	}
	return godot::Transform3D();
}

godot::Transform3D GameSim::get_player_physical_render_transform(int player_id) const
{
	if (!car_player_ids || !cars) {
		return godot::Transform3D();
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		SimTransform prev = MXT_LOAD_TRANSFORM(soa, basis_physical_other, lane);
		SimTransform current = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
		prev.origin = LOAD_INDEXED_VEC3(soa, position_old, lane);
		current.origin = LOAD_INDEXED_VEC3(soa, position_current, lane);
		return gd_transform(interpolate_sim_transform(prev, current, alpha));
	}
	return godot::Transform3D();
}

godot::Vector3 GameSim::get_player_physical_render_up(int player_id) const
{
	if (!car_player_ids || !cars) {
		return godot::Vector3(0.0, 1.0, 0.0);
	}
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] != player_id) {
			continue;
		}
		const PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		SimVec3 up = LOAD_INDEXED_VEC3(soa, track_surface_normal_prev, lane).lerp(
			LOAD_INDEXED_VEC3(soa, track_surface_normal, lane),
			alpha);
		if (up.length_squared() <= 0.0001f) {
			up = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(1);
		}
		if (up.length_squared() <= 0.0001f) {
			up = SimVec3(0.0f, 1.0f, 0.0f);
		}
		return gd_vec3(up.normalized());
	}
	return godot::Vector3(0.0, 1.0, 0.0);
}

godot::Transform3D GameSim::get_car_render_transform(int car_index) const
{
	if (car_index < 0 ||
			car_index >= num_cars ||
			car_index >= static_cast<int>(render_final_current_transforms.size()) ||
			car_index >= static_cast<int>(render_final_prev_transforms.size())) {
		return godot::Transform3D();
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	SimTransform render_transform = interpolate_sim_transform(
		render_final_prev_transforms[car_index],
		render_final_current_transforms[car_index],
		alpha);
	return gd_transform(render_transform);
}

godot::Transform3D GameSim::get_saved_player_voice_transform(int player_id, int target_tick) const
{
	if (target_tick < 0) {
		return godot::Transform3D();
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	const SavedState& state = state_buffer[index];
	if (state.tick != target_tick || state.voice_transform_count <= 0) {
		return godot::Transform3D();
	}
	const int count = std::min(state.voice_transform_count, static_cast<int>(state.voice_transforms.size()));
	for (int i = 0; i < count; ++i) {
		if (state.voice_transforms[i].player_id == player_id) {
			return godot::Transform3D(godot::Basis(), gd_vec3(state.voice_transforms[i].origin));
		}
	}
	return godot::Transform3D();
}

godot::Array GameSim::get_saved_player_voice_transforms(int target_tick) const
{
	godot::Array out;
	if (target_tick < 0) {
		return out;
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	const SavedState& state = state_buffer[index];
	if (state.tick != target_tick || state.voice_transform_count <= 0) {
		return out;
	}
	const int count = std::min(state.voice_transform_count, static_cast<int>(state.voice_transforms.size()));
	for (int i = 0; i < count; ++i) {
		godot::Dictionary item;
		item["player_id"] = state.voice_transforms[i].player_id;
		item["transform"] = godot::Transform3D(godot::Basis(), gd_vec3(state.voice_transforms[i].origin));
		out.append(item);
	}
	return out;
}

godot::Dictionary GameSim::select_saved_voice_recipients(int sender_id, int local_id, int target_tick, godot::Array eligible_peer_ids, godot::Array excluded_peer_ids, double voice_range, int max_recipients) const
{
	Dictionary out;
	PackedInt32Array recipients;
	out["recipients"] = recipients;
	out["snapshot_count"] = 0;
	out["source_found"] = false;
	out["local_candidate"] = false;
	out["local_distance"] = -1.0;
	if (target_tick < 0 || max_recipients <= 0 || voice_range <= 0.0 || eligible_peer_ids.is_empty()) {
		return out;
	}
	const SavedState& state = state_buffer[target_tick % STATE_BUFFER_LEN];
	if (state.tick != target_tick || state.voice_transform_count <= 0) {
		return out;
	}
	const int transform_count = std::min(state.voice_transform_count, static_cast<int>(state.voice_transforms.size()));
	out["snapshot_count"] = transform_count;
	SimVec3 source_origin;
	bool source_found = false;
	for (int i = 0; i < transform_count; ++i) {
		if (state.voice_transforms[i].player_id == sender_id) {
			source_origin = state.voice_transforms[i].origin;
			source_found = true;
			break;
		}
	}
	out["source_found"] = source_found;
	if (!source_found) {
		return out;
	}

	struct VoiceCandidate {
		int32_t player_id = -1;
		float distance_sq = 0.0f;
	};
	constexpr int MAX_NATIVE_VOICE_RECIPIENTS = 64;
	VoiceCandidate nearest[MAX_NATIVE_VOICE_RECIPIENTS];
	const int recipient_limit = std::min(max_recipients, MAX_NATIVE_VOICE_RECIPIENTS);
	const float max_distance_sq = static_cast<float>(voice_range * voice_range);
	int nearest_count = 0;
	for (int i = 0; i < transform_count; ++i) {
		const SavedVoiceTransform& voice = state.voice_transforms[i];
		const int32_t player_id = voice.player_id;
		if (player_id < 0 || player_id == sender_id || !eligible_peer_ids.has(player_id) || excluded_peer_ids.has(player_id)) {
			continue;
		}
		const SimVec3 delta = voice.origin - source_origin;
		const float distance_sq = delta.length_squared();
		if (distance_sq > max_distance_sq) {
			continue;
		}
		if (player_id == local_id) {
			out["local_candidate"] = true;
			out["local_distance"] = std::sqrt(distance_sq);
		}
		int insert_at = nearest_count;
		while (insert_at > 0 && nearest[insert_at - 1].distance_sq > distance_sq) {
			--insert_at;
		}
		if (insert_at >= recipient_limit) {
			continue;
		}
		const int shift_end = std::min(nearest_count, recipient_limit - 1);
		for (int shift = shift_end; shift > insert_at; --shift) {
			nearest[shift] = nearest[shift - 1];
		}
		nearest[insert_at].player_id = player_id;
		nearest[insert_at].distance_sq = distance_sq;
		nearest_count = std::min(nearest_count + 1, recipient_limit);
	}
	recipients.resize(nearest_count);
	for (int i = 0; i < nearest_count; ++i) {
		recipients.set(i, nearest[i].player_id);
	}
	out["recipients"] = recipients;
	return out;
}

godot::Array GameSim::get_check_warning_candidates(int player_id) const
{
	godot::Array out;
	if (!car_player_ids || num_cars <= 1 || render_final_current_transforms.empty()) {
		return out;
	}
	int focus_index = -1;
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (car_player_ids[i] == player_id) {
			focus_index = i;
			break;
		}
	}
	if (focus_index < 0) {
		return out;
	}

	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	const SimTransform focus = interpolate_sim_transform(
		render_final_prev_transforms[focus_index],
		render_final_current_transforms[focus_index],
		alpha);
	const SimVec3 focus_pos = focus.origin;
	SimVec3 check_right = focus.basis.get_column(0);
	SimVec3 check_forward = -focus.basis.get_column(2);
	if (gameplay_camera_node) {
		const godot::Transform3D camera_transform = gameplay_camera_node->get_global_transform();
		check_right = sim_vec3(camera_transform.basis.get_column(0));
		check_forward = -sim_vec3(camera_transform.basis.get_column(2));
	}
	if (check_forward.length_squared() <= 0.0001f || check_right.length_squared() <= 0.0001f) {
		return out;
	}
	check_forward = check_forward.normalized();
	check_right = check_right.normalized();

	struct CheckWarningCandidate {
		float distance = FLT_MAX;
		float lateral = 0.0f;
		SimVec3 intersect;
		float alpha = 0.0f;
		int32_t player_id = -1;
	};
	constexpr int CHECK_WARNING_LIMIT = 6;
	CheckWarningCandidate best[CHECK_WARNING_LIMIT];
	for (int i = 0; i < num_cars && i < static_cast<int>(render_final_current_transforms.size()); ++i) {
		if (i == focus_index) {
			continue;
		}
		const SimTransform other = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		const SimVec3 delta = other.origin - focus_pos;
		const float signed_dist = delta.dot(check_forward);
		if (signed_dist >= -1.0f || signed_dist < -80.0f) {
			continue;
		}
		const SimVec3 intersect = other.origin - check_forward * signed_dist;
		const float lateral = (intersect - focus_pos).dot(check_right);
		if (std::abs(lateral) > 70.0f) {
			continue;
		}
		const float alpha_value = std::clamp((signed_dist + 80.0f) / 79.0f, 0.0f, 1.0f);
		const float distance = -signed_dist;
		if (distance >= best[CHECK_WARNING_LIMIT - 1].distance) {
			continue;
		}
		int insert_at = CHECK_WARNING_LIMIT - 1;
		while (insert_at > 0 && distance < best[insert_at - 1].distance) {
			best[insert_at] = best[insert_at - 1];
			--insert_at;
		}
		best[insert_at].distance = distance;
		best[insert_at].lateral = lateral;
		best[insert_at].intersect = intersect;
		best[insert_at].alpha = alpha_value;
		best[insert_at].player_id = car_player_ids[i];
	}
	for (int i = 0; i < CHECK_WARNING_LIMIT; ++i) {
		if (best[i].player_id < 0) {
			continue;
		}
		godot::Dictionary entry;
		entry["player_id"] = best[i].player_id;
		entry["lateral"] = best[i].lateral;
		entry["intersect"] = gd_vec3(best[i].intersect);
		entry["alpha"] = best[i].alpha;
		out.append(entry);
	}
	return out;
}

godot::Array GameSim::consume_race_events()
{
	godot::Array out;
	for (const RaceEvent& event : race_events) {
		godot::Dictionary entry;
		entry["type"] = static_cast<int>(event.type);
		entry["actor_id"] = event.actor_id;
		entry["target_id"] = event.target_id;
		entry["tick"] = event.tick;
		entry["value"] = event.value;
		out.append(entry);
	}
	race_events.clear();
	return out;
}

namespace {
struct DipSwitchDefinition {
		const char* key;
		const char* label;
		int flag;
	};

	const DipSwitchDefinition DIP_SWITCH_DEFINITIONS[] = {
		{"DIP_DRAW_RAYCASTS", "Draw Raycasts", DIP_SWITCH::DIP_DRAW_RAYCASTS},
		{"DIP_DRAW_CHECKPOINTS", "Draw Checkpoints", DIP_SWITCH::DIP_DRAW_CHECKPOINTS},
		{"DIP_DRAW_SEGMENT_SURF", "Draw Segment Surface", DIP_SWITCH::DIP_DRAW_SEGMENT_SURF},
		{"DIP_DRAW_TILT_CORNER_DATA", "Draw Tilt Corner Data", DIP_SWITCH::DIP_DRAW_TILT_CORNER_DATA},
		{"DIP_DRAW_SEG_BOUNDS", "Draw Segment Bounds", DIP_SWITCH::DIP_DRAW_SEG_BOUNDS},
		{"DIP_DRAW_BRANCH_CENTERLINE", "Draw Branch Centerline", DIP_SWITCH::DIP_DRAW_BRANCH_CENTERLINE},
		{"DIP_TRACE_RAIL_SAMPLING", "Trace Rail Sampling", DIP_SWITCH::DIP_TRACE_RAIL_SAMPLING},
		{"DIP_DRAW_RAIL_CANDIDATES", "Draw Rail Candidates", DIP_SWITCH::DIP_DRAW_RAIL_CANDIDATES},
		{"DIP_TRACE_PIPE_FLOOR", "Trace Pipe Floor", DIP_SWITCH::DIP_TRACE_PIPE_FLOOR},
		{"DIP_DRAW_MESH_FLOOR_TESTS", "Draw Mesh Floor Tests", DIP_SWITCH::DIP_DRAW_MESH_FLOOR_TESTS},
		{"DIP_DRAW_MESH_CAST_TESTS", "Draw Mesh Cast Tests", DIP_SWITCH::DIP_DRAW_MESH_CAST_TESTS},
		{"DIP_DRAW_MESH_COLLISION_HITS", "Draw Mesh Collision Hits", DIP_SWITCH::DIP_DRAW_MESH_COLLISION_HITS},
		{"DIP_TRACE_MESH_FLOOR", "Trace Mesh Floor", DIP_SWITCH::DIP_TRACE_MESH_FLOOR},
	};
}

Array GameSim::get_dip_switches() const
{
	Array switches;
	for (const auto& def : DIP_SWITCH_DEFINITIONS) {
		Dictionary entry;
		entry["key"] = String(def.key);
		entry["label"] = String(def.label);
		entry["flag"] = def.flag;
		entry["enabled"] = DEBUG::dip_enabled(def.flag);
		switches.push_back(entry);
	}
	return switches;
}

bool GameSim::is_dip_switch_enabled(int flag) const
{
	return DEBUG::dip_enabled(flag);
}

void GameSim::set_dip_switch_enabled(int flag, bool enabled)
{
	if (enabled) {
		DEBUG::enable_dip(flag);
	} else {
		DEBUG::disable_dip(flag);
	}
}

void GameSim::set_rail_trace_filter(int car_index, int tick_start, int tick_end)
{
	DEBUG::set_rail_trace_filter(car_index, tick_start, tick_end);
}

double GameSim::get_first_lap_distance() const
{
	if (!sim_started || !cars || num_cars <= 0 || !current_track)
	{
		return 0.0;
	}
	return static_cast<double>(compute_car_distance_along_track(cars[0]));
}

double GameSim::get_track_lap_length() const
{
	if (!current_track) {
		return 0.0;
	}
	float lap_length = current_track->lap_length;
	if (lap_length <= 0.0f && current_track->num_checkpoints > 0) {
		lap_length = current_track->checkpoints[current_track->num_checkpoints - 1].distance;
	}
	return static_cast<double>(lap_length);
}
