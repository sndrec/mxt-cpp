#include "gamesim/gamesim_internal.h"

#include "godot_cpp/core/class_db.hpp"
#include "audio/spatial_audio_manager.h"
#include "track/finish_line_display.h"

using namespace godot;
void GameSim::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("instantiate_gamesim", "lvldat_buf", "car_prop_buffers", "accel_settings"), &GameSim::instantiate_gamesim);
	ClassDB::bind_method(D_METHOD("sample_car_properties", "bytes", "machine_setting"), &GameSim::sample_car_properties);
	ClassDB::bind_method(D_METHOD("evaluate_car_properties", "bytes", "machine_setting",
		"genuinely_drifting", "strafe_input", "signed_slip", "manual_boost_active",
		"dashplate_boost_active", "s_boost_active"), &GameSim::evaluate_car_properties);
	ClassDB::bind_method(D_METHOD("destroy_gamesim"), &GameSim::destroy_gamesim);
	ClassDB::bind_method(D_METHOD("render_gamesim"), &GameSim::render_gamesim);
	ClassDB::bind_method(D_METHOD("set_trigger_visuals", "visual_nodes"), &GameSim::set_trigger_visuals);
	ClassDB::bind_method(D_METHOD("get_sim_started"), &GameSim::get_sim_started);
	ClassDB::bind_method(D_METHOD("set_sim_started", "p_sim_started"), &GameSim::set_sim_started);
	ClassDB::bind_method(D_METHOD("set_spawn_seed", "seed"), &GameSim::set_spawn_seed);
	ClassDB::bind_method(D_METHOD("set_start_grid_slots", "slots"), &GameSim::set_start_grid_slots);
	ClassDB::bind_method(D_METHOD("set_vehicle_restore_enabled", "enabled"), &GameSim::set_vehicle_restore_enabled);
	ClassDB::bind_method(D_METHOD("get_vehicle_restore_enabled"), &GameSim::get_vehicle_restore_enabled);
	ClassDB::bind_method(D_METHOD("set_multiplayer_intro_camera_enabled", "enabled"), &GameSim::set_multiplayer_intro_camera_enabled);
	ClassDB::bind_method(D_METHOD("get_multiplayer_intro_camera_enabled"), &GameSim::get_multiplayer_intro_camera_enabled);
	ClassDB::bind_method(D_METHOD("set_bumpers_enabled", "enabled"), &GameSim::set_bumpers_enabled);
	ClassDB::bind_method(D_METHOD("get_bumpers_enabled"), &GameSim::get_bumpers_enabled);
	ClassDB::bind_method(D_METHOD("set_s_boost_enabled", "enabled"), &GameSim::set_s_boost_enabled);
	ClassDB::bind_method(D_METHOD("get_s_boost_enabled"), &GameSim::get_s_boost_enabled);
	ClassDB::bind_method(D_METHOD("set_boost_unlocked_from_start", "enabled"), &GameSim::set_boost_unlocked_from_start);
	ClassDB::bind_method(D_METHOD("get_boost_unlocked_from_start"), &GameSim::get_boost_unlocked_from_start);
	ClassDB::bind_method(D_METHOD("set_target_lap_count", "lap_count"), &GameSim::set_target_lap_count);
	ClassDB::bind_method(D_METHOD("get_target_lap_count"), &GameSim::get_target_lap_count);
	ClassDB::bind_method(D_METHOD("save_state"), &GameSim::save_state);
	ClassDB::bind_method(D_METHOD("has_saved_state", "target_tick"), &GameSim::has_saved_state);
	ClassDB::bind_method(D_METHOD("load_state", "target_tick"), &GameSim::load_state);
	ClassDB::bind_method(D_METHOD("load_state_data", "target_tick", "data"), &GameSim::load_state_data);
	ClassDB::bind_method(D_METHOD("get_state_data", "target_tick"), &GameSim::get_state_data);
	ClassDB::bind_method(D_METHOD("get_full_state_data", "target_tick"), &GameSim::get_full_state_data);
	ClassDB::bind_method(D_METHOD("load_full_state_data", "target_tick", "data"), &GameSim::load_full_state_data);
	ClassDB::bind_method(D_METHOD("get_network_state_size_stats"), &GameSim::get_network_state_size_stats);
	ClassDB::bind_method(D_METHOD("get_memory_usage_stats"), &GameSim::get_memory_usage_stats);
	ClassDB::bind_method(D_METHOD("set_state_data", "target_tick", "data"), &GameSim::set_state_data);
	ClassDB::bind_method(D_METHOD("render_gamesim_visuals_only", "process_delta"), &GameSim::render_gamesim_visuals_only);
	ClassDB::bind_method(D_METHOD("get_dip_switches"), &GameSim::get_dip_switches);
	ClassDB::bind_method(D_METHOD("is_dip_switch_enabled", "flag"), &GameSim::is_dip_switch_enabled);
	ClassDB::bind_method(D_METHOD("set_dip_switch_enabled", "flag", "enabled"), &GameSim::set_dip_switch_enabled);
	ClassDB::bind_method(D_METHOD("set_rail_trace_filter", "car_index", "tick_start", "tick_end"), &GameSim::set_rail_trace_filter);
	ClassDB::bind_method(D_METHOD("set_yaw_trace_filter", "car_index", "tick_start", "tick_end"), &GameSim::set_yaw_trace_filter);
	ClassDB::bind_method(D_METHOD("get_first_lap_distance"), &GameSim::get_first_lap_distance);
	ClassDB::bind_method(D_METHOD("get_track_lap_length"), &GameSim::get_track_lap_length);
	ClassDB::bind_method(D_METHOD("get_native_cpu_input_for_tick", "player_id", "expected_tick"), &GameSim::get_native_cpu_input_for_tick);
	ClassDB::bind_method(D_METHOD("get_input_frame_as_dictionary", "target_tick"), &GameSim::get_input_frame_as_dictionary);
	ClassDB::bind_method(D_METHOD("set_player_metadata", "player_ids", "cpu_flags"), &GameSim::set_player_metadata);
	ClassDB::bind_method(D_METHOD("get_phase_profile_string"), &GameSim::get_phase_profile_string);
	ClassDB::bind_method(D_METHOD("get_phase_profile_last_sample"), &GameSim::get_phase_profile_last_sample);
	ClassDB::bind_method(D_METHOD("set_phase_profile_enabled", "enabled"), &GameSim::set_phase_profile_enabled);
	ClassDB::bind_method(D_METHOD("get_render_profile_string"), &GameSim::get_render_profile_string);
	ClassDB::bind_method(D_METHOD("get_render_profile_last_sample"), &GameSim::get_render_profile_last_sample);
	ClassDB::bind_method(D_METHOD("set_render_profile_enabled", "enabled"), &GameSim::set_render_profile_enabled);
	ClassDB::bind_method(D_METHOD("set_render_node_effects_enabled", "enabled"), &GameSim::set_render_node_effects_enabled);
	ClassDB::bind_method(D_METHOD("set_render_thruster_lights_enabled", "enabled"), &GameSim::set_render_thruster_lights_enabled);
	ClassDB::bind_method(D_METHOD("set_render_all_car_bodies", "enabled"), &GameSim::set_render_all_car_bodies);
	ClassDB::bind_method(D_METHOD("set_render_car_body_view_distance", "distance"), &GameSim::set_render_car_body_view_distance);
	ClassDB::bind_method(D_METHOD("get_render_car_body_view_distance"), &GameSim::get_render_car_body_view_distance);
	ClassDB::bind_method(D_METHOD("get_player_race_place", "player_id"), &GameSim::get_player_race_place);
	ClassDB::bind_method(D_METHOD("get_vehicle_death_states"), &GameSim::get_vehicle_death_states);
	ClassDB::bind_method(D_METHOD("get_race_leaderboard_window", "player_id", "max_entries", "finished_players", "eliminated_players"), &GameSim::get_race_leaderboard_window);
	ClassDB::bind_method(D_METHOD("get_race_control_start_tick"), &GameSim::get_race_control_start_tick);
	ClassDB::bind_method(D_METHOD("get_finished_player_ids"), &GameSim::get_finished_player_ids);
	ClassDB::bind_method(D_METHOD("get_eliminated_player_ids"), &GameSim::get_eliminated_player_ids);
	ClassDB::bind_method(D_METHOD("is_player_race_finished", "player_id"), &GameSim::is_player_race_finished);
	ClassDB::bind_method(D_METHOD("is_player_race_eliminated", "player_id"), &GameSim::is_player_race_eliminated);
	ClassDB::bind_method(D_METHOD("get_player_ko_energy_bonus", "player_id"), &GameSim::get_player_ko_energy_bonus);
	ClassDB::bind_method(D_METHOD("set_player_ko_energy_bonus", "player_id", "bonus"), &GameSim::set_player_ko_energy_bonus);
	ClassDB::bind_method(D_METHOD("get_player_lap_distance", "player_id"), &GameSim::get_player_lap_distance);
	ClassDB::bind_method(D_METHOD("get_player_lap", "player_id"), &GameSim::get_player_lap);
	ClassDB::bind_method(D_METHOD("get_player_level_start_time", "player_id"), &GameSim::get_player_level_start_time);
	ClassDB::bind_method(D_METHOD("get_player_speed_kmh", "player_id"), &GameSim::get_player_speed_kmh);
	ClassDB::bind_method(D_METHOD("get_player_energy_fraction", "player_id"), &GameSim::get_player_energy_fraction);
	ClassDB::bind_method(D_METHOD("get_player_telemetry_sample", "player_id"), &GameSim::get_player_telemetry_sample);
	ClassDB::bind_method(D_METHOD("get_player_debug_string", "player_id"), &GameSim::get_player_debug_string);
	ClassDB::bind_method(D_METHOD("get_bumper_debug_string"), &GameSim::get_bumper_debug_string);
	ClassDB::bind_method(D_METHOD("get_race_order"), &GameSim::get_race_order);
	ClassDB::bind_method(D_METHOD("get_player_render_transform", "player_id"), &GameSim::get_player_render_transform);
	ClassDB::bind_method(D_METHOD("get_player_physical_render_transform", "player_id"), &GameSim::get_player_physical_render_transform);
	ClassDB::bind_method(D_METHOD("get_player_physical_render_up", "player_id"), &GameSim::get_player_physical_render_up);
	ClassDB::bind_method(D_METHOD("get_car_render_transform", "car_index"), &GameSim::get_car_render_transform);
	ClassDB::bind_method(D_METHOD("get_saved_player_voice_transform", "player_id", "target_tick"), &GameSim::get_saved_player_voice_transform);
	ClassDB::bind_method(D_METHOD("get_saved_player_voice_transforms", "target_tick"), &GameSim::get_saved_player_voice_transforms);
	ClassDB::bind_method(D_METHOD("select_saved_voice_recipients", "sender_id", "local_id", "target_tick", "eligible_peer_ids", "excluded_peer_ids", "voice_range", "max_recipients"), &GameSim::select_saved_voice_recipients);
	ClassDB::bind_method(D_METHOD("get_check_warning_candidates", "player_id"), &GameSim::get_check_warning_candidates);
	ClassDB::bind_method(D_METHOD("consume_race_events"), &GameSim::consume_race_events);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sim_started"), "set_sim_started", "get_sim_started");
	ClassDB::bind_method(D_METHOD("get_car_node_container"), &GameSim::get_car_node_container);
	ClassDB::bind_method(D_METHOD("set_car_node_container", "p_car_node_container"), &GameSim::set_car_node_container);
	ClassDB::bind_method(D_METHOD("get_spark_node_container"), &GameSim::get_spark_node_container);
	ClassDB::bind_method(D_METHOD("set_spark_node_container", "p_spark_node_container"), &GameSim::set_spark_node_container);
	ClassDB::bind_method(D_METHOD("set_car_render_manager", "p_car_render_manager"), &GameSim::set_car_render_manager);
	ClassDB::bind_method(D_METHOD("set_gameplay_camera", "p_camera", "player_id"), &GameSim::set_gameplay_camera);
	ClassDB::bind_method(D_METHOD("set_gameplay_camera_zoom_mode", "zoom_mode"), &GameSim::set_gameplay_camera_zoom_mode);
	ClassDB::bind_method(D_METHOD("get_gameplay_camera_zoom_mode"), &GameSim::get_gameplay_camera_zoom_mode);
	ClassDB::bind_method(D_METHOD("set_render_camera", "p_camera"), &GameSim::set_render_camera);
	ClassDB::bind_method(D_METHOD("set_spatial_audio_manager", "p_manager"), &GameSim::set_spatial_audio_manager);
	ClassDB::bind_method(D_METHOD("get_spatial_audio_manager"), &GameSim::get_spatial_audio_manager);
	ClassDB::bind_method(D_METHOD("play_car_oneshot_sfx", "car_index", "sfx_id", "volume_db", "pitch_scale"), &GameSim::play_car_oneshot_sfx, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("play_player_oneshot_sfx", "player_id", "sfx_id", "volume_db", "pitch_scale"), &GameSim::play_player_oneshot_sfx, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("play_world_oneshot_sfx", "position", "sfx_id", "volume_db", "pitch_scale"), &GameSim::play_world_oneshot_sfx, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("tick_singleplayer", "local_player_id", "local_input"), &GameSim::tick_singleplayer);
	ClassDB::bind_method(D_METHOD("tick_singleplayer_indexed_input", "local_player_id", "input_bytes", "frame_offsets", "frame_index"), &GameSim::tick_singleplayer_indexed_input);
	ClassDB::bind_method(D_METHOD("discard_race_events"), &GameSim::discard_race_events);
	ClassDB::bind_method(D_METHOD("update_render_snapshots"), &GameSim::update_render_snapshots);
	ClassDB::bind_method(D_METHOD("snap_render_after_state_load"), &GameSim::snap_render_after_state_load);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "car_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_car_node_container", "get_car_node_container");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spark_node_container", PROPERTY_HINT_RESOURCE_TYPE, "Node3D"), "set_spark_node_container", "get_spark_node_container");
};

GameSim::GameSim()
{
	tick = 0;
	tick_delta = 1.0f / 60.0f;
	sim_started = false;
	current_track = nullptr;
	car_node_container = nullptr;
	spark_node_container = nullptr;
	super_spark_state = nullptr;
	super_sparks = nullptr;
	spark_multimesh_instance = nullptr;
	num_cars = 0;
	cars = nullptr;
	car_properties_array = nullptr;
	bumper_cars = nullptr;
	bumper_properties_array = nullptr;
	clear_player_index_lookup();
	reset_super_sparks();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		state_buffer[i].data = nullptr;
		state_buffer[i].size = 0;
		state_buffer[i].bumper_state_count = 0;
		state_buffer[i].bumper_scheduler_lap = 0;
		state_buffer[i].bumper_next_sequence = 0;
		state_buffer[i].tick = -1;
		state_buffer[i].voice_transform_count = 0;
		state_buffer[i].car_local_state_size = 0;
		state_buffer[i].bumper_local_state_size = 0;
		state_buffer[i].vehicle_local_state.clear();
	}
	input_buffer = nullptr;
};

GameSim::~GameSim()
{
	stop_vehicle_lane_workers();
	destroy_gamesim();
	destroy_collision_spark_runtime();
	destroy_drift_plasma_runtime();
	delete finish_line_display;
	finish_line_display = nullptr;
	free_vehicle_tick_soa();
	for (int i = 0; i < STATE_BUFFER_LEN; i++)
	{
		if (state_buffer[i].data)
		{
			::free(state_buffer[i].data);
			state_buffer[i].data = nullptr;
		}
		state_buffer[i].size = 0;
		state_buffer[i].tick = -1;
		state_buffer[i].voice_transform_count = 0;
		state_buffer[i].voice_transforms.clear();
		state_buffer[i].car_local_state_size = 0;
		state_buffer[i].bumper_local_state_size = 0;
		state_buffer[i].vehicle_local_state.clear();
	}
	if (input_buffer) {
		::free(input_buffer);
		input_buffer = nullptr;
	}
};

void GameSim::VehicleLaneGroup::reset(int p_count)
{
	count = p_count;
	waiting.store(0, std::memory_order_relaxed);
	generation.store(0, std::memory_order_release);
}

void GameSim::VehicleLaneGroup::sync()
{
	if (count <= 1) {
		return;
	}
	const uint32_t local_generation = generation.load(std::memory_order_acquire);
	if (waiting.fetch_add(1, std::memory_order_acq_rel) + 1 == count) {
		waiting.store(0, std::memory_order_release);
		generation.fetch_add(1, std::memory_order_acq_rel);
		return;
	}
	int spins = 0;
	while (generation.load(std::memory_order_acquire) == local_generation) {
		if (++spins > 512) {
			std::this_thread::yield();
		}
	}
}

void GameSim::ensure_vehicle_lane_workers()
{
	if (vehicle_lane_workers_started) {
		return;
	}
	vehicle_lane_stop = false;
	for (int i = 0; i < VEHICLE_WORKER_COUNT - 1; ++i) {
		const int lane = i + 1;
		vehicle_lane_workers[i] = std::thread([this, lane]() {
			uint32_t seen_generation = 0;
			for (;;) {
				std::unique_lock<std::mutex> lock(vehicle_lane_mutex);
				vehicle_lane_cv.wait(lock, [&]() {
					return vehicle_lane_stop || vehicle_lane_generation != seen_generation;
				});
				if (vehicle_lane_stop) {
					return;
				}
				seen_generation = vehicle_lane_generation;
				const bool should_run = lane < vehicle_lane_active_count;
				auto fn = vehicle_lane_fn;
				void* context = vehicle_lane_context;
				lock.unlock();

				if (should_run) {
					fn(context, lane, vehicle_lane_group);
				}

				if (should_run) {
					lock.lock();
					vehicle_lane_pending -= 1;
					if (vehicle_lane_pending == 0) {
						vehicle_lane_done_cv.notify_one();
					}
				}
			}
		});
	}
	vehicle_lane_workers_started = true;
}

void GameSim::stop_vehicle_lane_workers()
{
	if (!vehicle_lane_workers_started) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_stop = true;
		vehicle_lane_generation += 1;
	}
	vehicle_lane_cv.notify_all();
	for (int i = 0; i < VEHICLE_WORKER_COUNT - 1; ++i) {
		if (vehicle_lane_workers[i].joinable()) {
			vehicle_lane_workers[i].join();
		}
	}
	vehicle_lane_fn = nullptr;
	vehicle_lane_context = nullptr;
	vehicle_lane_active_count = 0;
	vehicle_lane_pending = 0;
	vehicle_lane_workers_started = false;
	vehicle_lane_stop = false;
}

void GameSim::run_vehicle_lanes(int lane_count, bool parallel, void* context, VehicleLaneFn fn)
{
	if (!parallel || lane_count <= 1) {
		VehicleLaneGroup group;
		group.reset(1);
		for (int lane = 0; lane < lane_count; ++lane) {
			fn(context, lane, group);
		}
		return;
	}

	const int active_lanes = std::min(lane_count, VEHICLE_WORKER_COUNT);
	ensure_vehicle_lane_workers();
	vehicle_lane_group.reset(active_lanes);
	{
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_fn = fn;
		vehicle_lane_context = context;
		vehicle_lane_active_count = active_lanes;
		vehicle_lane_pending = active_lanes - 1;
		vehicle_lane_generation += 1;
	}
	vehicle_lane_cv.notify_all();

	fn(context, 0, vehicle_lane_group);

	if (active_lanes > 1) {
		std::unique_lock<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_done_cv.wait(lock, [&]() {
			return vehicle_lane_pending == 0;
		});
		vehicle_lane_fn = nullptr;
		vehicle_lane_context = nullptr;
		vehicle_lane_active_count = 0;
	} else {
		std::lock_guard<std::mutex> lock(vehicle_lane_mutex);
		vehicle_lane_fn = nullptr;
		vehicle_lane_context = nullptr;
		vehicle_lane_active_count = 0;
	}
}

void GameSim::set_multiplayer_intro_camera_enabled(bool enabled)
{
	multiplayer_intro_camera_enabled = enabled;
	start_countdown_extra_frames = enabled ? 600u : 0u;
}
