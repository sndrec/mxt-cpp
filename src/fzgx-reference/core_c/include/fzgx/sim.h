#ifndef FZGX_SIM_H
#define FZGX_SIM_H

#include "fzgx/content.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FZGX_SIM_API_VERSION
#define FZGX_SIM_API_VERSION 1u
#endif

#define FZGX_SIM_MAX_MACHINES 30u
#define FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS 1877u

enum {
  FZGX_INPUT_BUTTON_SIDE_ATTACK = 1u << 0,
  FZGX_INPUT_BUTTON_SPIN_ATTACK = 1u << 1,
  FZGX_INPUT_BUTTON_BOOST = 1u << 2,
  FZGX_INPUT_BUTTON_STRAFE_MOD = 1u << 3
};

enum {
  FZGX_MACHINE_FLAG_ACTIVE = 1u << 0,
  FZGX_MACHINE_FLAG_COMPLETED_RACE = 1u << 1,
  FZGX_MACHINE_FLAG_RETIRED = 1u << 2,
  FZGX_MACHINE_FLAG_AIRBORNE = 1u << 3,
  FZGX_MACHINE_FLAG_SIDE_ATTACKING = 1u << 4,
  FZGX_MACHINE_FLAG_SPIN_ATTACKING = 1u << 5,
  FZGX_MACHINE_FLAG_ZERO_HP = 1u << 6,
  FZGX_MACHINE_FLAG_FALLOUT = 1u << 7,
  FZGX_MACHINE_FLAG_SIM_MOTION_RAN = 1u << 8
};

enum {
  FZGX_ENTRANT_RUNTIME_FLAG_VISUAL_SCALE = 0x00100000u,
  FZGX_ENTRANT_RUNTIME_FLAG_COLLISION_DESTROY = 0x00200000u,
  FZGX_ENTRANT_RUNTIME_FLAG_DESTROYED = 0x01000000u
};

typedef enum fzgx_active_checkpoint_mode {
  FZGX_ACTIVE_CHECKPOINT_CURRENT = 0,
  FZGX_ACTIVE_CHECKPOINT_NEXT = 1
} fzgx_active_checkpoint_mode;

typedef enum fzgx_racetrack_refresh_mode {
  FZGX_RACETRACK_REFRESH_NORMAL = 0,
  FZGX_RACETRACK_REFRESH_FORCED = 1
} fzgx_racetrack_refresh_mode;

typedef struct fzgx_track_side_query_summary {
  uint32_t candidate_count;
  uint32_t summary_flags;
  uint32_t special_postprocess_flag;
  uint32_t reserved0[3];
  uint32_t shared_mask;
  uint32_t piece_opaque[4];
  float width_scale[4];
  uint32_t flags[4];
  int32_t checkpoint_index[4];
} fzgx_track_side_query_summary;

typedef struct fzgx_track_side_query_buffer_exact {
  uint32_t write_record_offset;
  uint32_t latest_record_offset;
  fzgx_track_side_query_summary records[2];
} fzgx_track_side_query_buffer_exact;

typedef struct fzgx_machine_tilt_corner_snapshot {
  uint32_t state;
  uint32_t reserved0;
  fzgx_vec3 offset;
  fzgx_vec3 pos_old;
  fzgx_vec3 pos;
  fzgx_vec3 up_vector;
  fzgx_vec3 up_vector_2;
  float force;
  float rest_length_scale;
  fzgx_vec3 force_spatial;
  float force_spatial_len;
} fzgx_machine_tilt_corner_snapshot;

typedef struct fzgx_machine_wall_corner_snapshot {
  fzgx_vec3 offset;
  fzgx_vec3 pos_a;
  fzgx_vec3 pos_b;
  fzgx_vec3 collision;
} fzgx_machine_wall_corner_snapshot;

typedef struct fzgx_world_contact_slot_result {
  bool has_contact;
  uint8_t reserved0[3];
  float hit_time;
  fzgx_vec3 push;
  uint32_t info_flags;
  uintptr_t summary_flags_ptr_exact;
} fzgx_world_contact_slot_result;

typedef struct fzgx_sonic_oval_floor_bias {
  uint32_t candidate_count;
  uint32_t slot0_piece_opaque;
  int32_t slot0_checkpoint_index;
  uint32_t latest_slot0_flags;
  uint32_t alternate_slot0_flags;
} fzgx_sonic_oval_floor_bias;

typedef struct fzgx_world_spherecast_request {
  fzgx_vec3 start;
  fzgx_vec3 end;
  uint32_t flags;
  int32_t checkpoint_seed_index;
  int32_t checkpoint_seed_aux;
  uint32_t checkpoint_history_count;
  int32_t checkpoint_history_index[4];
  float checkpoint_history_fraction[4];
  uint32_t machine_index;
  bool has_sonic_oval_floor_bias;
  uint8_t reserved1[3];
  fzgx_sonic_oval_floor_bias sonic_oval_floor_bias;
  bool has_previous_side_summary;
  bool has_previous_side_normal;
  uint8_t reserved2[2];
  fzgx_vec3 previous_side_normal;
  fzgx_track_side_query_summary previous_side_summary;
  uintptr_t previous_side_summary_live_exact;
  uintptr_t current_side_summary_live_exact;
} fzgx_world_spherecast_request;

typedef struct fzgx_world_spherecast_result {
  bool has_hit;
  uint8_t reserved[3];
  float hit_time;
  fzgx_vec3 hit_point;
  fzgx_vec3 aux_hit_point;
  fzgx_vec3 hit_normal;
  uint32_t result_flags;
  uint32_t hit_info_flags;
  uint32_t surface_flags;
  uint32_t material_flags;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  uint32_t branch_flags;
  int32_t selected_cached_frame_index;
  uint32_t cached_frame_count;
  fzgx_world_contact_slot_result contact_slots[4];
  bool has_cached_frame_exports;
  uint8_t reserved_cached_frame_exports[3];
  fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY];
} fzgx_world_spherecast_result;

typedef fzgx_status (*fzgx_world_spherecast_fn)(
    void *userdata,
    const fzgx_world_spherecast_request *request,
    fzgx_world_spherecast_result *result);

typedef struct fzgx_race_time_triplet {
  int32_t frames;
  float fraction;
  uint32_t display;
} fzgx_race_time_triplet;

typedef struct fzgx_quat {
  float x;
  float y;
  float z;
  float w;
} fzgx_quat;

typedef struct fzgx_machine_track_state {
  fzgx_mat43 track_current_transform;
  fzgx_vec3 track_current_scale;
  float track_scl_x;
  float track_scl_y;
  fzgx_vec3 track_anchor;
  union {
    fzgx_vec3 last_track_pos;
    fzgx_vec3 track_forward;
  };
  union {
    fzgx_vec3 follow_pos;
    fzgx_vec3 track_up;
  };
  union {
    float dist_from_track_center;
    float track_width_or_radius;
  };
  union {
    struct {
      float track_hcylin;
      fzgx_vec3 track_follow_offset;
    };
    uint8_t field8_0x6c[16];
  };
  uint32_t flags;
  uint32_t need_resnap;
  float desired_dist_from_track_center;
  uint32_t model_id;
  int32_t plane_id;
  uint32_t active_cp_idx_ptr_offset;
  uint32_t active_frac_ptr_offset;
  union {
    int32_t unk_int_0x98;
    int32_t checkpoint_variant_count;
  };
  int32_t cur_cp_pointer;
  int32_t seg_index_hist[3];
  fzgx_vec3 last_fit_pos;
  union {
    struct {
      int32_t cur_cp_idx;
      int32_t neighbor_cp_idx[3];
    };
    int32_t stable_cp_idx[4];
  };
  union {
    struct {
      float cur_cp_frac;
      float neighbor_cp_frac[3];
    };
    float stable_cp_frac[4];
  };
  union {
    struct {
      int32_t next_cp_idx;
      int32_t predictive_cp_idx_tail[3];
    };
    int32_t predictive_cp_idx[4];
  };
  union {
    struct {
      float next_cp_frac;
      float predictive_cp_frac_tail[3];
    };
    float predictive_cp_frac[4];
  };
  float last_seg_dist;
  float last_frac_diff;
  float lap_progress_fraction;
  union {
    uint16_t track_relative_yaw_angle16;
    uint16_t unk_undefined2_0x104;
  };
  uint8_t field33_0x106[2];
  float angle_from_track_forward;
  int32_t facing_flag;
  int32_t facing_toggled;
  uint8_t facing_counter;
  uint8_t rank_this_frame;
  uint8_t unk_byte_0x116;
  uint8_t unk_byte_0x117;
  int32_t lap_start_cp;
  int32_t lap_cross_cp;
  int32_t cp_hist_idx[4];
  float cp_hist_frac[4];
  int32_t last_cp_idx;
  float last_cp_frac;
  fzgx_vec3 last_cp_pos;
  int32_t prev_lap_cp;
  union {
    int32_t prev_lap_cross_cp;
    int32_t unk_undefined4_0x158;
  };
  union {
    int32_t time_extension_trigger_mask;
    int32_t unk_undefined4_0x15c;
  };
  int32_t lap_time_frames;
  float lap_time_fraction;
  union {
    struct {
      uint8_t lap_min;
      uint8_t lap_sec;
      uint16_t lap_centi;
    };
    uint32_t lap_time_display;
  };
  union {
    struct {
      fzgx_race_time_triplet best_split_6;
      fzgx_race_time_triplet best_split_5;
      fzgx_race_time_triplet best_split_4;
      fzgx_race_time_triplet best_split_3;
      fzgx_race_time_triplet best_split_2;
      fzgx_race_time_triplet best_split_1;
      fzgx_race_time_triplet best_split_0;
      fzgx_race_time_triplet best_split_7;
    };
    fzgx_race_time_triplet best_splits[8];
  };
  union {
    struct {
      int32_t best_lap_frames;
      float best_lap_fraction;
      union {
        struct {
          uint8_t best_lap_slot;
          uint8_t unk_byte_0x1d5;
          uint16_t unk_undefined2_0x1d6;
        };
        uint32_t best_lap_display;
      };
    };
    fzgx_race_time_triplet best_lap;
  };
  int32_t total_time_frames;
  float total_time_fraction;
  union {
    struct {
      uint8_t total_min;
      uint8_t total_sec;
      uint16_t total_centi;
    };
    uint32_t total_time_display;
  };
  int32_t history_time_frames;
  float history_time_fraction;
  union {
    struct {
      uint8_t history_min;
      uint8_t history_sec;
      uint16_t history_centi;
    };
    uint32_t history_time_display;
  };
  fzgx_vec3 respawn_pos;
  int32_t lap_split_gate_mask;
  uint32_t cached_frame_count;
  int32_t selected_cached_frame_index;
  uint8_t active_checkpoint_mode;
  uint8_t reserved_track_state_tail[3];
} fzgx_machine_track_state;

#ifdef __cplusplus
#define FZGX_SIM_STATIC_ASSERT static_assert
#else
#define FZGX_SIM_STATIC_ASSERT _Static_assert
#endif

FZGX_SIM_STATIC_ASSERT(offsetof(fzgx_machine_track_state, flags) == 0x7c, "track_state.flags");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, need_resnap) == 0x80,
    "track_state.need_resnap");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, active_cp_idx_ptr_offset) == 0x90,
    "track_state.active_cp_idx_ptr_offset");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, checkpoint_variant_count) == 0x98,
    "track_state.checkpoint_variant_count");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, cur_cp_pointer) == 0x9c,
    "track_state.cur_cp_pointer");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, cur_cp_idx) == 0xb8,
    "track_state.cur_cp_idx");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, next_cp_idx) == 0xd8,
    "track_state.next_cp_idx");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, lap_cross_cp) == 0x11c,
    "track_state.lap_cross_cp");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, last_cp_pos) == 0x148,
    "track_state.last_cp_pos");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, time_extension_trigger_mask) == 0x15c,
    "track_state.time_extension_trigger_mask");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, total_time_frames) == 0x1d8,
    "track_state.total_time_frames");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, history_time_frames) == 0x1e4,
    "track_state.history_time_frames");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, respawn_pos) == 0x1f0,
    "track_state.respawn_pos");
FZGX_SIM_STATIC_ASSERT(
    offsetof(fzgx_machine_track_state, lap_split_gate_mask) == 0x1fc,
    "track_state.lap_split_gate_mask");
FZGX_SIM_STATIC_ASSERT(sizeof(fzgx_machine_track_state) >= 0x200, "track_state.size");

typedef struct fzgx_machine_snapshot {
  uint32_t machine_id;
  uint32_t entrant_runtime_flags;
  uint32_t machine_flags;
  uint32_t machine_state;
  uint32_t state_2;
  uint16_t score;
  uint8_t wall_hit_count;
  uint8_t boost_count;
  uint8_t dash_plate_hit_count;
  uint8_t clean_race_bonus_eligible;
  uint8_t boost_frames;
  uint8_t boost_frames_manual;
  float height_adjust_from_boost;
  float energy;
  float max_energy;
  float base_speed;
  float speed_kmh;
  float max_speed_kmh;
  float stat_weight;
  float stat_grip_1;
  float stat_grip_2;
  float stat_grip_3;
  float stat_turn_tension;
  float stat_turn_movement;
  float stat_strafe_turn;
  float stat_strafe;
  float stat_turn_reaction;
  float stat_drift_accel;
  float stat_body;
  float stat_acceleration;
  float stat_max_speed;
  float stat_boost_strength;
  float stat_boost_length;
  float stat_turn_decel;
  float stat_drag;
  float camera_reorienting;
  float camera_repositioning;
  float weight_derived_1;
  float weight_derived_2;
  float weight_derived_3;
  float race_start_charge;
  float boost_turbo;
  float air_tilt;
  float visual_shake_mult;
  float input_strafe_32;
  float input_strafe_1_6;
  float input_steer_pitch;
  float input_strafe;
  float input_steer_yaw;
  float input_accel;
  float input_brake;
  float unk_float_0x208;
  float input_yaw_dupe;
  float visual_roll;
  float visual_pitch;
  float zero_minus_height_above_track;
  float turn_reaction_input;
  float turn_reaction_effect;
  float boost_energy_use_mult;
  float const_float_2_0;
  float damage_from_last_hit;
  int32_t current_checkpoint;
  float checkpoint_fraction;
  fzgx_mat43 basis_physical;
  fzgx_mat43 basis_physical_other;
  fzgx_mat43 transform_visual;
  fzgx_vec3 position;
  fzgx_vec3 position_old;
  fzgx_vec3 position_old_dupe;
  fzgx_vec3 position_old_2;
  fzgx_vec3 velocity;
  fzgx_vec3 angular_velocity;
  fzgx_vec3 velocity_local;
  fzgx_vec3 velocity_local_flattened_and_rotated;
  fzgx_vec3 surface_normal;
  fzgx_vec3 collision_push_min;
  fzgx_vec3 collision_push_max;
  fzgx_vec3 collision_push_total;
  fzgx_vec3 collision_response;
  fzgx_vec3 broken_down_angle_fac1;
  fzgx_vec3 broken_down_angle_fac2;
  fzgx_vec3 position_bottom;
  fzgx_vec3 approach_dir;
  float stat_obstacle_collision;
  float stat_track_collision;
  float turning_related;
  uint32_t terrain_flags;
  uint32_t terrain_flags_2;
  uint32_t branch_indicator;
  uint32_t branch_flags;
  uint32_t floor_surface_flags;
  uint32_t floor_material_flags;
  uint8_t branch_slot;
  uint8_t control_profile_kind;
  uint16_t frames_since_start;
  uint32_t spinattack_angle;
  uint16_t spinattack_angle_decrement;
  uint8_t spinattack_direction;
  uint8_t grip_frames_from_accel_press;
  uint8_t stat_grip_frames_from_accel_press;
  uint8_t brake_timer;
  uint8_t frames_since_start_2;
  uint8_t side_attack_delay;
  uint8_t air_time;
  uint8_t machine_collision_frame_counter;
  uint8_t car_hit_invincibility;
  uint8_t machine_approach_frame_counter;
  uint8_t last_machine_approached;
  uint8_t time_since_ko_frame_counter;
  uint8_t g_unk_breakdown_int;
  uint8_t unk_byte_0x4c3;
  uint8_t breakdown_frame_counter;
  uint16_t frames_since_death;
  uint8_t rail_collision_timer;
  uint8_t suspension_reset_flag;
  uint16_t boost_delay_frame_counter;
  uint16_t frames_until_restored;
  uint32_t unk_random_0x514;
  float restore_progress;
  float unk_restore_0x51c;
  fzgx_mat43 g_restore_matrix_1;
  fzgx_mat43 g_restore_mtx_2;
  fzgx_mat43 g_restore_mtx_3;
  uint8_t restore_count;
  uint8_t post_restore_frame_countdown;
  int16_t strafe_visual_roll_angle;
  uint8_t unk_byte_0x240;
  uint8_t unk_byte_0x241;
  float unk_stat_0x5d4;
  fzgx_quat unk_quat_0x5c4;
  fzgx_mat43 g_pitch_mtx_0x5e0;
  uint8_t suspension_state[4];
  float side_attack_indicator;
  bool spin_attack_kill_indicator;
  bool machine_crashed;
  uint8_t reserved2[2];
  fzgx_machine_tilt_corner_snapshot suspension_corners[4];
  fzgx_machine_wall_corner_snapshot wall_corners[4];
  fzgx_track_side_query_buffer_exact corner_scratch[4];
  fzgx_track_segment_trs_curve_cache_exact track_query_filter_cache;
  fzgx_machine_track_state track_state;
} fzgx_machine_snapshot;

typedef struct fzgx_world_snapshot {
  uint32_t api_version;
  uint32_t frame_index;
  uint32_t stage_scene_frame_banks[4];
  uint32_t stage_scene_context_mask;
  int32_t stage_scene_context_active_machine_index;
  uint32_t stage_scene_context_view_slot;
  uint8_t has_pending_stage_scene_story_delta;
  uint8_t race_full_heal_latch_active;
  uint8_t race_full_heal_latch_persistent;
  uint8_t reserved0;
  float stage_scene_story_clip_offset_frames;
  float pending_stage_scene_story_delta_frames;
  uint32_t active_track_index;
  uint32_t race_mode;
  uint32_t scene_object_query_filter_mask;
  uint32_t scene_object_query_mode_mask;
  uint32_t dynamic_scene_runtime_flag_count;
  uint32_t machine_count;
  uint32_t dynamic_scene_runtime_flags[FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS];
  float dynamic_scene_clip_bank_time_frames[FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS][4];
  fzgx_machine_snapshot machines[FZGX_SIM_MAX_MACHINES];
} fzgx_world_snapshot;

typedef struct fzgx_control_sample {
  float steer_yaw;
  float steer_pitch;
  float strafe;
  float accel;
  float brake;
  uint32_t buttons;
  uint8_t control_profile_kind;
  uint8_t reserved[3];
} fzgx_control_sample;

typedef struct fzgx_race_step_options {
  bool advance_lap_timers;
  bool allow_finish_score_transition;
  bool classify_retired_entrants_as_wrecked;
  bool force_full_heal_this_frame;
  uint8_t has_raw_frame_state;
  uint8_t finish_score_lap_threshold;
  uint8_t reserved0[2];
  uint32_t raw_frame_flags;
} fzgx_race_step_options;

typedef struct fzgx_machine_track_sample {
  int32_t checkpoint_index;
  float checkpoint_fraction;
  int32_t active_bank_cp_idx[4];
  float active_bank_cp_frac[4];
  int32_t segment_index;
  uint32_t checkpoint_variant_count;
  uint32_t flags;
  uint32_t cached_frame_count;
  int32_t selected_cached_frame_index;
  fzgx_vec3 checkpoint_world_pos;
  fzgx_mat43 track_current_transform;
  fzgx_vec3 track_current_scale;
  float track_scl_x;
  float track_scl_y;
  fzgx_vec3 track_anchor;
  fzgx_vec3 track_forward;
  fzgx_vec3 track_up;
  float track_width_or_radius;
  float track_hcylin;
  fzgx_vec3 track_follow_offset;
  float angle_from_track_forward;
  float lap_progress_fraction;
  float last_frac_diff;
  uint8_t active_checkpoint_mode;
  uint8_t reserved[3];
} fzgx_machine_track_sample;

typedef struct fzgx_sim_world_config {
  uint32_t api_version;
  uint32_t machine_capacity;
} fzgx_sim_world_config;

typedef struct fzgx_sim_world {
  uint32_t api_version;
  uint32_t frame_index;
  uint32_t stage_scene_frame_banks[4];
  uint32_t stage_scene_context_mask;
  int32_t stage_scene_context_active_machine_index;
  uint32_t stage_scene_context_view_slot;
  uint8_t has_pending_stage_scene_story_delta;
  uint8_t race_full_heal_latch_active;
  uint8_t race_full_heal_latch_persistent;
  uint8_t reserved0;
  float stage_scene_story_clip_offset_frames;
  float pending_stage_scene_story_delta_frames;
  uint32_t machine_capacity;
  uint32_t machine_count;
  uint32_t active_track_index;
  uint32_t race_mode;
  uint32_t scene_object_query_filter_mask;
  uint32_t scene_object_query_mode_mask;
  uint32_t dynamic_scene_runtime_flag_count;
  const fzgx_content_bundle *content;
  const fzgx_owned_static_collider_course *static_collider_course;
  const fzgx_owned_track_mesh_course *track_mesh_course;
  const fzgx_owned_dynamic_scene_collision_course *dynamic_scene_collision_course;
  fzgx_world_spherecast_fn spherecast_fn;
  void *spherecast_userdata;
  uint8_t exact_collision_scratch_raw[0xc00];
  fzgx_track_side_query_buffer_exact *exact_collision_piece_scratch_0x700;
  uint32_t dynamic_scene_runtime_flags[FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS];
  float dynamic_scene_clip_bank_time_frames[FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS][4];
  fzgx_control_sample controls[FZGX_SIM_MAX_MACHINES];
  fzgx_machine_snapshot machines[FZGX_SIM_MAX_MACHINES];
} fzgx_sim_world;

void fzgx_sim_world_init(fzgx_sim_world *world);
fzgx_status fzgx_sim_world_configure(
    fzgx_sim_world *world,
    const fzgx_sim_world_config *config,
    const fzgx_content_bundle *content);
fzgx_status fzgx_sim_world_set_spherecast_callback(
    fzgx_sim_world *world,
    fzgx_world_spherecast_fn spherecast_fn,
    void *userdata);
fzgx_status fzgx_sim_world_set_static_collider_course(
    fzgx_sim_world *world,
    const fzgx_owned_static_collider_course *course);
fzgx_status fzgx_sim_world_set_track_mesh_course(
    fzgx_sim_world *world,
    const fzgx_owned_track_mesh_course *course);
fzgx_status fzgx_sim_world_set_dynamic_scene_collision_course(
    fzgx_sim_world *world,
    const fzgx_owned_dynamic_scene_collision_course *course);
fzgx_status fzgx_sim_world_debug_exact_spherecast(
    fzgx_sim_world *world,
    const fzgx_world_spherecast_request *request,
    fzgx_track_side_query_buffer_exact *piece_scratch,
    fzgx_world_spherecast_result *result_out);
fzgx_status fzgx_sim_world_set_race_full_heal_latch(
    fzgx_sim_world *world,
    bool enabled);
fzgx_status fzgx_sim_world_set_race_mode(fzgx_sim_world *world, uint32_t race_mode);
fzgx_status fzgx_sim_world_set_track(fzgx_sim_world *world, uint32_t track_index);
fzgx_status fzgx_sim_world_build_ordinary_start_grid_slot_transform(
    const fzgx_sim_world *world,
    uint32_t slot_index,
    float *track_width_or_radius_out,
    fzgx_mat43 *transform_out);
fzgx_status fzgx_sim_world_build_ordinary_start_grid_slot_query_result(
    const fzgx_sim_world *world,
    uint32_t slot_index,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_sim_world_build_machine_current_track_query_result(
    const fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(
    fzgx_sim_world *world,
    uint32_t machine_index,
    uint32_t slot_index,
    double launch_speed_units);
fzgx_status fzgx_sim_world_reset_machines_to_ordinary_start_grid(
    fzgx_sim_world *world,
    const uint32_t *machine_indices_in_slot_order,
    size_t machine_index_count,
    double launch_speed_units);
fzgx_status fzgx_sim_world_reset_machines_to_ordinary_start_grid_stopped(
    fzgx_sim_world *world,
    const uint32_t *machine_indices_in_slot_order,
    size_t machine_index_count);
fzgx_status fzgx_sim_world_set_machine_count(fzgx_sim_world *world, uint32_t machine_count);
fzgx_status fzgx_sim_world_seed_machine_from_content(
    fzgx_sim_world *world,
    uint32_t machine_index,
    uint32_t definition_index,
    uint32_t machine_setting_percent);
fzgx_status fzgx_sim_world_set_machine_snapshot(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_machine_snapshot *snapshot);
fzgx_status fzgx_sim_world_get_machine_snapshot(
    const fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *snapshot_out);
fzgx_status fzgx_sim_world_set_snapshot(
    fzgx_sim_world *world,
    const fzgx_world_snapshot *snapshot);
fzgx_status fzgx_sim_world_get_snapshot(
    const fzgx_sim_world *world,
    fzgx_world_snapshot *snapshot_out);
int32_t fzgx_sim_get_time_extension_trigger_progress_index(
    const fzgx_machine_track_state *track);
fzgx_status fzgx_sim_world_apply_machine_track_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_machine_track_sample *sample);
fzgx_status fzgx_sim_build_current_track_sample_from_query_result(
    const fzgx_current_track_query_result *query_result,
    fzgx_machine_track_sample *sample_out);
fzgx_status fzgx_sim_world_apply_active_checkpoint_bank_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_active_checkpoint_bank_result *bank_result);
fzgx_status fzgx_sim_world_apply_current_checkpoint_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_current_checkpoint_query_result *query_result);
fzgx_status fzgx_sim_world_apply_current_track_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_current_track_query_result *query_result);
fzgx_status fzgx_sim_world_reset_machine_from_transform_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units,
    const fzgx_machine_track_sample *sample);
fzgx_status fzgx_sim_world_reset_machine_from_transform_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units,
    const fzgx_current_track_query_result *query_result);
fzgx_status fzgx_sim_world_reset_machine_from_current_transform_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    double launch_speed_units,
    const fzgx_machine_track_sample *sample);
fzgx_status fzgx_sim_world_reset_machine_from_current_transform_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    double launch_speed_units,
    const fzgx_current_track_query_result *query_result);
fzgx_status fzgx_sim_world_refresh_machine_racetrack_state_from_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_machine_track_sample *sample);
fzgx_status fzgx_sim_world_refresh_machine_racetrack_state_from_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_current_track_query_result *query_result);
fzgx_status fzgx_sim_world_commit_machine_active_checkpoint_bank(
    fzgx_sim_world *world,
    uint32_t machine_index);
fzgx_status fzgx_sim_world_commit_machine_checkpoint_snapshot(
    fzgx_sim_world *world,
    uint32_t machine_index);
fzgx_status fzgx_sim_world_reset_machine_track_state(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_racetrack_refresh_mode refresh_mode);
fzgx_status fzgx_sim_world_refresh_machine_track_fit_transform(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_mat43 *transform_out);
fzgx_status fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_racetrack_refresh_mode refresh_mode,
    bool forward_progress_guard);
fzgx_status fzgx_sim_world_recompute_machine_track_derived_metrics(
    fzgx_sim_world *world,
    uint32_t machine_index);
fzgx_status fzgx_sim_finalize_machine_finish_score(
    fzgx_sim_world *world,
    uint32_t machine_index);
fzgx_status fzgx_sim_step_begin(
    fzgx_sim_world *world,
    const fzgx_control_sample *controls,
    size_t control_count);
fzgx_status fzgx_sim_step_machine_phase(fzgx_sim_world *world);
fzgx_status fzgx_sim_step_frame_phase(
    fzgx_sim_world *world,
    const fzgx_race_step_options *options);
fzgx_status fzgx_sim_step(
    fzgx_sim_world *world,
    const fzgx_control_sample *controls,
    size_t control_count,
    const fzgx_race_step_options *options);

#ifdef __cplusplus
}
#endif

#endif
