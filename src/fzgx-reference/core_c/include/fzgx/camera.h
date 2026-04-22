#ifndef FZGX_CAMERA_H
#define FZGX_CAMERA_H

#include "fzgx/sim.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON = 0u,
  FZGX_GAME_CAMERA_ZOOM_CLOSE = 1u,
  FZGX_GAME_CAMERA_ZOOM_MEDIUM = 2u,
  FZGX_GAME_CAMERA_ZOOM_FAR = 3u,
  FZGX_GAME_CAMERA_ZOOM_CYCLE = 4u,
  FZGX_GAME_CAMERA_ZOOM_RESTORE = 5u
};

enum {
  FZGX_GAME_CAMERA_BEHAVIOR_NORMAL = 0u,
  FZGX_GAME_CAMERA_BEHAVIOR_PRESET_LOCAL = 1u,
  FZGX_GAME_CAMERA_BEHAVIOR_CYCLE = 2u,
  FZGX_GAME_CAMERA_BEHAVIOR_RESTORE = 3u,
  FZGX_GAME_CAMERA_BEHAVIOR_FALLOUT = 5u,
  FZGX_GAME_CAMERA_BEHAVIOR_TRACK_RESTORE = 6u
};

typedef struct fzgx_game_camera_input {
  uint8_t view_up_pressed;
  uint8_t view_down_pressed;
  uint8_t reserved[2];
} fzgx_game_camera_input;

typedef struct fzgx_game_camera_view {
  uint8_t active;
  uint8_t zoom_mode;
  uint8_t saved_zoom_mode;
  uint8_t behavior_state;
  float perspective;
  float aspect_ratio;
  fzgx_vec3 position;
  fzgx_vec3 previous_position;
  fzgx_vec3 interest;
  fzgx_vec3 up;
  fzgx_mat43 view_matrix;
  fzgx_mat43 previous_view_matrix;
} fzgx_game_camera_view;

typedef struct fzgx_game_camera_runtime {
  uint32_t machine_index;
  uint8_t active;
  uint8_t persistent_saved_zoom_mode;
  uint8_t display_mode_kind;
  uint8_t camera_manager_mode;
  int16_t behavior_state;
  int16_t zoom_mode;
  int16_t saved_zoom_mode;
  int16_t pitch_angle16;
  int16_t restore_countdown;
  int16_t sequence_angle16;
  int16_t airborne_transition_counter;
  int32_t perspective_transition_counter;
  uint32_t cycle_initialized;
  uint32_t shake_active;
  int32_t shake_frames_remaining;
  float aspect_ratio;
  float camera_parameter;
  float perspective;
  float clearance_ratio;
  float boost_perspective_target;
  float vertical_offset_state;
  float interest_vertical_offset_state;
  float previous_speed_kmh;
  fzgx_vec3 local_follow_offset;
  fzgx_vec3 local_interest_offset;
  fzgx_vec3 restore_anchor_position;
  float restore_blend;
  float restore_blend_velocity;
  fzgx_mat43 follow_basis;
  fzgx_vec3 position;
  fzgx_vec3 previous_position;
  fzgx_vec3 interest;
  fzgx_vec3 up;
  fzgx_mat43 view_matrix;
  fzgx_mat43 previous_view_matrix;
  fzgx_vec3 shake_position_base;
  fzgx_vec3 shake_interest_base;
  fzgx_vec3 shake_position_offset;
  fzgx_vec3 shake_position_velocity;
  fzgx_vec3 shake_interest_offset;
  fzgx_vec3 shake_interest_velocity;
  fzgx_track_side_query_buffer_exact vertical_probe_piece_scratch;
} fzgx_game_camera_runtime;

void fzgx_game_camera_init(fzgx_game_camera_runtime *camera);
fzgx_status fzgx_game_camera_reset(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    uint32_t machine_index);
fzgx_status fzgx_game_camera_step(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_game_camera_input *input);
fzgx_status fzgx_game_camera_reset_follow_preview(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    uint32_t machine_index);
fzgx_status fzgx_game_camera_step_follow_preview(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_game_camera_input *input);
fzgx_status fzgx_game_camera_get_view(
    const fzgx_game_camera_runtime *camera,
    fzgx_game_camera_view *view_out);

#ifdef __cplusplus
}
#endif

#endif
