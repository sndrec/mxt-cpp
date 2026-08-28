#pragma once

#include <cstdint>

namespace godot {

struct GameSimProfileState {
	bool render_enabled = false;
	bool phase_enabled = false;
	uint64_t phase_frames = 0;
	uint64_t phase_total_us = 0;
	uint64_t phase_total_max_us = 0;
	uint64_t phase_pre_us = 0;
	uint64_t phase_pre_max_us = 0;
	uint64_t phase_input_us = 0;
	uint64_t phase_input_max_us = 0;
	uint64_t phase_vehicle_us = 0;
	uint64_t phase_vehicle_max_us = 0;
	uint64_t phase_vehicle_begin_us = 0;
	uint64_t phase_vehicle_apply_input_us = 0;
	uint64_t phase_vehicle_floor_us = 0;
	uint64_t phase_vehicle_prepare_frame_us = 0;
	uint64_t phase_vehicle_floor_corner_analytic_surface_us = 0;
	uint64_t phase_vehicle_floor_mesh_candidate_collect_us = 0;
	uint64_t phase_vehicle_floor_mesh_cast4_us = 0;
	uint64_t phase_vehicle_floor_mesh_sample_us = 0;
	uint64_t phase_vehicle_find_floor_us = 0;
	uint64_t phase_vehicle_find_floor_cast_us = 0;
	uint64_t phase_vehicle_find_floor_mesh_us = 0;
	uint64_t phase_vehicle_find_floor_analytic_us = 0;
	uint64_t phase_vehicle_terrain_us = 0;
	uint64_t phase_vehicle_trigger_us = 0;
	uint64_t phase_vehicle_motion_us = 0;
	uint64_t phase_vehicle_finish_tick_us = 0;
	uint64_t phase_vehicle_collision_us = 0;
	uint64_t phase_vehicle_post_tick_us = 0;
	uint64_t phase_vehicle_corner_update_us = 0;
	uint64_t phase_vehicle_corner_old_analytic_us = 0;
	uint64_t phase_vehicle_corner_new_checkpoint_us = 0;
	uint64_t phase_vehicle_corner_new_analytic_us = 0;
	uint64_t phase_vehicle_corner_mesh_us = 0;
	uint64_t phase_vehicle_tail_us = 0;
	uint64_t phase_vehicle_checkpoint_us = 0;
	uint64_t phase_vehicle_spark_collect_us = 0;
	uint64_t phase_post_vehicle_us = 0;
	uint64_t phase_post_vehicle_max_us = 0;
	uint64_t phase_placement_us = 0;
	uint64_t phase_placement_max_us = 0;
	uint64_t phase_post_us = 0;
	uint64_t phase_post_max_us = 0;
	uint64_t phase_save_us = 0;
	uint64_t phase_save_max_us = 0;
	uint64_t phase_save_bumper_us = 0;
	uint64_t phase_save_voice_us = 0;
	uint64_t phase_save_memcpy_us = 0;
	uint64_t phase_last_total_us = 0;
	uint64_t phase_last_pre_us = 0;
	uint64_t phase_last_input_us = 0;
	uint64_t phase_last_vehicle_us = 0;
	uint64_t phase_last_vehicle_collision_us = 0;
	uint64_t phase_last_post_vehicle_us = 0;
	uint64_t phase_last_placement_us = 0;
	uint64_t phase_last_post_us = 0;
	uint64_t phase_last_save_us = 0;
	uint64_t render_frames = 0;
	uint64_t render_total_us = 0;
	uint64_t render_total_max_us = 0;
	uint64_t render_get_children_us = 0;
	uint64_t render_cache_us = 0;
	uint64_t render_snapshots_us = 0;
	uint64_t render_snapshots_max_us = 0;
	uint64_t render_effects_us = 0;
	uint64_t render_effects_max_us = 0;
	uint64_t render_multimesh_us = 0;
	uint64_t render_multimesh_max_us = 0;
	uint64_t render_body_instances = 0;
	uint64_t render_thruster_instances = 0;
	uint64_t render_camera_us = 0;
	uint64_t render_local_visual_us = 0;
	uint64_t render_cpu_driver_us = 0;
	uint64_t render_cpu_driver_max_us = 0;
	uint64_t render_spark_us = 0;
	uint64_t render_visuals_only_frames = 0;
	uint64_t render_visuals_only_total_us = 0;
	uint64_t render_visuals_only_total_max_us = 0;
	uint64_t render_visuals_only_effects_us = 0;
	uint64_t render_visuals_only_effects_max_us = 0;
	uint64_t render_visuals_only_multimesh_us = 0;
	uint64_t render_visuals_only_multimesh_max_us = 0;
	uint64_t render_visuals_only_body_instances = 0;
	uint64_t render_visuals_only_thruster_instances = 0;
	uint64_t render_visuals_only_camera_us = 0;
};

} // namespace godot
