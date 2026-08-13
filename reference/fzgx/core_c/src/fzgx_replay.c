#include "fzgx/replay.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fzgx_replay_file_exists(const char *path) {
  FILE *file;

  if ((path == NULL) || (path[0] == '\0')) {
    return 0;
  }
  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  fclose(file);
  return 1;
}

static fzgx_status fzgx_replay_reinitialize_world(
    fzgx_sim_world *world,
    const fzgx_content_bundle *bundle) {
  fzgx_sim_world_config config;

  if ((world == NULL) || (bundle == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(&config, 0, sizeof(config));
  fzgx_sim_world_init(world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  return fzgx_sim_world_configure(world, &config, bundle);
}

static fzgx_status fzgx_replay_load_collision_courses_from_stage_path(
    const char *stage_path,
    fzgx_replay_loaded_courses *courses) {
  fzgx_status status;

  if ((stage_path == NULL) || (courses == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_content_load_static_collider_course_from_path(
      stage_path, &courses->static_course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  courses->has_static_course = 1u;

  status = fzgx_content_load_track_mesh_course_from_path(
      stage_path, &courses->track_mesh_course);
  if (status == FZGX_STATUS_OK) {
    courses->has_track_mesh_course = 1u;
  } else if (status != FZGX_STATUS_UNIMPLEMENTED) {
    return status;
  }

  status = fzgx_content_load_dynamic_scene_collision_course_from_path(
      stage_path, &courses->dynamic_course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  courses->has_dynamic_course = 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_replay_load_collision_courses_from_stage_bytes(
    const uint8_t *stage_data,
    uint32_t stage_size,
    fzgx_replay_loaded_courses *courses) {
  fzgx_status status;

  if ((stage_data == NULL) || (courses == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_content_load_static_collider_course_from_bytes(
      stage_data, stage_size, &courses->static_course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  courses->has_static_course = 1u;

  status = fzgx_content_load_track_mesh_course_from_bytes(
      stage_data, stage_size, &courses->track_mesh_course);
  if (status == FZGX_STATUS_OK) {
    courses->has_track_mesh_course = 1u;
  } else if (status != FZGX_STATUS_UNIMPLEMENTED) {
    return status;
  }

  status = fzgx_content_load_dynamic_scene_collision_course_from_bytes(
      stage_data, stage_size, &courses->dynamic_course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  courses->has_dynamic_course = 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_replay_start_session_from_loaded_courses_exact(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses) {
  fzgx_status status;

  status = fzgx_sim_world_set_static_collider_course(
      world, &courses->static_course);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }
  if (courses->has_track_mesh_course != 0u) {
    status = fzgx_sim_world_set_track_mesh_course(
        world, &courses->track_mesh_course);
    if (status != FZGX_STATUS_OK) {
      fzgx_replay_release_loaded_courses(courses);
      return status;
    }
  }
  status = fzgx_sim_world_set_dynamic_scene_collision_course(
      world, &courses->dynamic_course);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }
  status = fzgx_sim_world_set_track(world, track_index);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }
  status = fzgx_sim_world_set_machine_count(world, 1u);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }
  status = fzgx_sim_world_seed_machine_from_content(
      world, 0u, machine_index, machine_setting_percent);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }
  status = fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(
      world, 0u, 0u, 0.0);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }

  world->stage_scene_context_active_machine_index = 0u;
  world->stage_scene_context_view_slot = 0u;

  if (camera != NULL) {
    fzgx_game_camera_init(camera);
    if (camera_render_state != NULL) {
      fzgx_replay_apply_camera_render_state(camera, camera_render_state);
    }
    status = fzgx_game_camera_reset(camera, world, 0u);
    if (status != FZGX_STATUS_OK) {
      fzgx_replay_release_loaded_courses(courses);
      return status;
    }
  }

  return FZGX_STATUS_OK;
}

void fzgx_replay_init_loaded_courses(fzgx_replay_loaded_courses *courses) {
  if (courses == NULL) {
    return;
  }
  memset(courses, 0, sizeof(*courses));
}

void fzgx_replay_release_loaded_courses(fzgx_replay_loaded_courses *courses) {
  if (courses == NULL) {
    return;
  }
  if (courses->has_static_course != 0u) {
    fzgx_content_release_static_collider_course(&courses->static_course);
  }
  if (courses->has_track_mesh_course != 0u) {
    fzgx_content_release_track_mesh_course(&courses->track_mesh_course);
  }
  if (courses->has_dynamic_course != 0u) {
    fzgx_content_release_dynamic_scene_collision_course(&courses->dynamic_course);
  }
  memset(courses, 0, sizeof(*courses));
}

void fzgx_replay_init_camera_render_state(fzgx_replay_camera_render_state *state) {
  if (state == NULL) {
    return;
  }
  state->aspect_ratio = 4.0f / 3.0f;
  state->display_mode_kind = -1;
  state->camera_parameter = -1.0f;
  state->camera_manager_mode = 0;
}

void fzgx_replay_apply_camera_render_state(
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *state) {
  if ((camera == NULL) || (state == NULL)) {
    return;
  }
  if (isfinite(state->aspect_ratio) && (state->aspect_ratio > 0.0f)) {
    camera->aspect_ratio = state->aspect_ratio;
  }
  if ((0 <= state->display_mode_kind) && (state->display_mode_kind <= 0xff)) {
    camera->display_mode_kind = (uint8_t)state->display_mode_kind;
  } else {
    camera->display_mode_kind = 0xffu;
  }
  if (state->camera_manager_mode < 0) {
    camera->camera_manager_mode = 0u;
  } else if (state->camera_manager_mode > 0xff) {
    camera->camera_manager_mode = 0xffu;
  } else {
    camera->camera_manager_mode = (uint8_t)state->camera_manager_mode;
  }
  camera->camera_parameter = state->camera_parameter;
}

fzgx_status fzgx_replay_build_stage_path(
    const char *repo_root,
    uint32_t authored_track_id,
    char *path_out,
    size_t path_capacity) {
  if ((repo_root == NULL) || (path_out == NULL) || (path_capacity == 0u)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  snprintf(
      path_out,
      path_capacity,
      "%s/fzgx-port/stage/COLI_COURSE%02u.lz",
      repo_root,
      authored_track_id);
  if (fzgx_replay_file_exists(path_out)) {
    return FZGX_STATUS_OK;
  }
  snprintf(
      path_out,
      path_capacity,
      "%s/fzgx-iso/files/stage/COLI_COURSE%02u.lz",
      repo_root,
      authored_track_id);
  if (fzgx_replay_file_exists(path_out)) {
    return FZGX_STATUS_OK;
  }
  path_out[0] = '\0';
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_replay_start_session_from_stage_path(
    const char *stage_path,
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses) {
  fzgx_status status;

  if ((stage_path == NULL) || (bundle == NULL) || (world == NULL) || (courses == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((track_index >= bundle->track_count) || (machine_index >= bundle->machine_count) ||
      (machine_setting_percent > 100u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  fzgx_replay_release_loaded_courses(courses);
  status = fzgx_replay_reinitialize_world(world, bundle);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_replay_load_collision_courses_from_stage_path(stage_path, courses);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }

  return fzgx_replay_start_session_from_loaded_courses_exact(
      bundle,
      track_index,
      machine_index,
      machine_setting_percent,
      world,
      camera,
      camera_render_state,
      courses);
}

fzgx_status fzgx_replay_start_session_from_stage_bytes(
    const uint8_t *stage_data,
    uint32_t stage_size,
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses) {
  fzgx_status status;

  if ((stage_data == NULL) || (bundle == NULL) || (world == NULL) || (courses == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((track_index >= bundle->track_count) || (machine_index >= bundle->machine_count) ||
      (machine_setting_percent > 100u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  fzgx_replay_release_loaded_courses(courses);
  status = fzgx_replay_reinitialize_world(world, bundle);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_replay_load_collision_courses_from_stage_bytes(stage_data, stage_size, courses);
  if (status != FZGX_STATUS_OK) {
    fzgx_replay_release_loaded_courses(courses);
    return status;
  }

  return fzgx_replay_start_session_from_loaded_courses_exact(
      bundle,
      track_index,
      machine_index,
      machine_setting_percent,
      world,
      camera,
      camera_render_state,
      courses);
}

fzgx_status fzgx_replay_start_session_from_loaded_courses(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses) {
  fzgx_status status;

  if ((bundle == NULL) || (world == NULL) || (courses == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((track_index >= bundle->track_count) || (machine_index >= bundle->machine_count) ||
      (machine_setting_percent > 100u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  status = fzgx_replay_reinitialize_world(world, bundle);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  return fzgx_replay_start_session_from_loaded_courses_exact(
      bundle,
      track_index,
      machine_index,
      machine_setting_percent,
      world,
      camera,
      camera_render_state,
      courses);
}

fzgx_status fzgx_replay_start_session(
    const char *repo_root,
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses) {
  char stage_path[512];
  fzgx_status status;

  if ((repo_root == NULL) || (bundle == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (track_index >= bundle->track_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  status = fzgx_replay_build_stage_path(
      repo_root,
      bundle->tracks[track_index].authored_track_id,
      stage_path,
      sizeof(stage_path));
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_replay_start_session_from_stage_path(
      stage_path,
      bundle,
      track_index,
      machine_index,
      machine_setting_percent,
      world,
      camera,
      camera_render_state,
      courses);
}

fzgx_status fzgx_replay_step_frame(
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_control_sample *control,
    const fzgx_game_camera_input *camera_input,
    const fzgx_race_step_options *options) {
  fzgx_game_camera_input neutral_camera_input;
  fzgx_status status;

  if ((world == NULL) || (control == NULL) || (options == NULL)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  world->stage_scene_context_active_machine_index = 0u;
  status = fzgx_sim_step(world, control, 1u, options);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (camera == NULL) {
    return FZGX_STATUS_OK;
  }

  memset(&neutral_camera_input, 0, sizeof(neutral_camera_input));
  if (camera_input == NULL) {
    camera_input = &neutral_camera_input;
  }
  return fzgx_game_camera_step(camera, world, camera_input);
}
