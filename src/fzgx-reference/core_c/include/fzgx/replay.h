#ifndef FZGX_REPLAY_H
#define FZGX_REPLAY_H

#include "fzgx/camera.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fzgx_replay_loaded_courses {
  fzgx_owned_static_collider_course static_course;
  fzgx_owned_track_mesh_course track_mesh_course;
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
  uint8_t has_static_course;
  uint8_t has_track_mesh_course;
  uint8_t has_dynamic_course;
  uint8_t reserved0;
} fzgx_replay_loaded_courses;

typedef struct fzgx_replay_camera_render_state {
  float aspect_ratio;
  int32_t display_mode_kind;
  float camera_parameter;
  int32_t camera_manager_mode;
} fzgx_replay_camera_render_state;

void fzgx_replay_init_loaded_courses(fzgx_replay_loaded_courses *courses);
void fzgx_replay_release_loaded_courses(fzgx_replay_loaded_courses *courses);
void fzgx_replay_init_camera_render_state(fzgx_replay_camera_render_state *state);
void fzgx_replay_apply_camera_render_state(
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *state);
fzgx_status fzgx_replay_build_stage_path(
    const char *repo_root,
    uint32_t authored_track_id,
    char *path_out,
    size_t path_capacity);
fzgx_status fzgx_replay_start_session_from_stage_path(
    const char *stage_path,
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses);
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
    fzgx_replay_loaded_courses *courses);
fzgx_status fzgx_replay_start_session_from_loaded_courses(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses);
fzgx_status fzgx_replay_start_session(
    const char *repo_root,
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    uint32_t machine_index,
    uint32_t machine_setting_percent,
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_replay_camera_render_state *camera_render_state,
    fzgx_replay_loaded_courses *courses);
fzgx_status fzgx_replay_step_frame(
    fzgx_sim_world *world,
    fzgx_game_camera_runtime *camera,
    const fzgx_control_sample *control,
    const fzgx_game_camera_input *camera_input,
    const fzgx_race_step_options *options);

#ifdef __cplusplus
}
#endif

#endif
