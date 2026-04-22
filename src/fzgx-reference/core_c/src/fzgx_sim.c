#include "fzgx/sim.h"

#include "../../catalog/sine_lut_14b.inc"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FZGX_PI 3.14159265358979323846f
#define FZGX_HALFPI 1.57079632679489661923f
#define FZGX_ANGLE16_PER_RAD (65536.0f / (2.0f * FZGX_PI))

enum {
  FZGX_TERRAIN_JUMP = 0x04000000u,
  FZGX_TERRAIN_RECHARGE = 0x08000000u,
  FZGX_TERRAIN_DASHPLATE = 0x10000000u,
  FZGX_TERRAIN_LAVA = 0x02000000u,
  FZGX_TERRAIN_LANDMINE = 0x40000000u,
  FZGX_TERRAIN_DIRT = 0x20000000u,
  FZGX_TERRAIN_ICE = 0x80000000u
};

enum {
  FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT = 40000u
};

enum {
  FZGX_CRASH_RESTORE_FRAMES_DEFAULT_EXACT = 0x00b4u,
  FZGX_MS_CRASH_RESTORE_TRIGGER_EXACT = 0x02810000u,
  FZGX_MS_B1 = 0x00000001u,
  FZGX_MS_AIRBORNE = 0x00000002u,
  FZGX_MS_AIRBORNEMORE0_2S_Q = 0x00000004u,
  FZGX_MS_SPINATTACKING = 0x00000008u,
  FZGX_MS_JUSTLANDED = 0x00000010u,
  FZGX_MS_BOOSTING = 0x00000020u,
  FZGX_MS_JUSTPRESSEDBOOST = 0x00000040u,
  FZGX_MS_0HP = 0x00000080u,
  FZGX_MS_B9 = 0x00000100u,
  FZGX_MS_B10 = 0x00000200u,
  FZGX_MS_B13 = 0x00001000u,
  FZGX_MS_B14 = 0x00002000u,
  FZGX_MS_FALLOUT = 0x00000800u,
  FZGX_MS_SIDEATTACKING = 0x00020000u,
  FZGX_MS_STRAFING = 0x00004000u,
  FZGX_MS_STARTINGCOUNTDOWN = 0x00008000u,
  FZGX_MS_COMPLETEDRACE_1_Q = 0x00010000u,
  FZGX_MS_CROSSEDLAPLINE_Q = 0x00040000u,
  FZGX_MS_JUSTTAPPEDACCEL = 0x00080000u,
  FZGX_MS_RACEJUSTBEGAN_Q = 0x00100000u,
  FZGX_MS_BOOSTING_DASHPLATE = 0x00200000u,
  FZGX_MS_B23 = 0x00400000u,
  FZGX_MS_COMPLETEDRACE_2_Q = 0x04000000u,
  FZGX_MS_RETIRED = 0x08000000u,
  FZGX_MS_TOOKDAMAGE = 0x00800000u,
  FZGX_MS_LOWGRIP = 0x01000000u,
  FZGX_MS_JUSTHITVEHICLE_Q = 0x02000000u,
  FZGX_MS_B29 = 0x10000000u,
  FZGX_MS_B30 = 0x20000000u,
  FZGX_MS_ACTIVE = 0x00000400u,
  FZGX_MS_DIEDTHISFRAMEOOB_Q = 0x40000000u,
  FZGX_MS_VEHICLEACTIVE_Q = 0x80000000u
};

enum {
  FZGX_TC_DRIFT = 0x04u,
  FZGX_TC_STRAFING = 0x10u
};

static const uint8_t fzgx_track_normal_corner_index_sets[5][4] = {
    {2u, 1u, 0u, 3u},
    {2u, 3u, 1u, 3u},
    {0u, 2u, 3u, 2u},
    {3u, 1u, 0u, 1u},
    {1u, 0u, 2u, 0u},
};

_Static_assert(sizeof(fzgx_track_side_query_summary) == 0x5c,
               "fzgx_track_side_query_summary must match the raw 0x5c corner record");
_Static_assert(offsetof(fzgx_track_side_query_summary, reserved0) == 0x0c,
               "fzgx_track_side_query_summary.reserved0 must hold the raw +0x0c hit normal");
_Static_assert(offsetof(fzgx_track_side_query_summary, shared_mask) == 0x18,
               "fzgx_track_side_query_summary.shared_mask must match raw +0x18");
_Static_assert(sizeof(fzgx_track_side_query_buffer_exact) == 0xc0,
               "fzgx_track_side_query_buffer_exact must match the raw 0xc0 corner buffer");
_Static_assert(offsetof(fzgx_track_side_query_buffer_exact, records) == 0x08,
               "fzgx_track_side_query_buffer_exact.records must begin at raw +0x08");
_Static_assert(sizeof(fzgx_mat43) == 0x30, "fzgx_mat43 must match raw matrix_4_3");
_Static_assert(offsetof(fzgx_mat43, basis_x_x) == 0x00, "fzgx_mat43.basis_x_x must match raw +0x00");
_Static_assert(offsetof(fzgx_mat43, origin_x) == 0x0c, "fzgx_mat43.origin_x must match raw +0x0c");
_Static_assert(offsetof(fzgx_mat43, origin_y) == 0x1c, "fzgx_mat43.origin_y must match raw +0x1c");
_Static_assert(offsetof(fzgx_mat43, origin_z) == 0x2c, "fzgx_mat43.origin_z must match raw +0x2c");
_Static_assert(sizeof(fzgx_track_frame_record) == 0x80,
               "fzgx_track_frame_record must match the raw 0x80 cached-frame export");
_Static_assert(offsetof(fzgx_track_frame_record, track_current_transform) == 0x00,
               "fzgx_track_frame_record.track_current_transform must match raw +0x00");
_Static_assert(offsetof(fzgx_track_frame_record, track_current_scale) == 0x30,
               "fzgx_track_frame_record.track_current_scale must match raw +0x30");
_Static_assert(offsetof(fzgx_track_frame_record, track_anchor) == 0x44,
               "fzgx_track_frame_record.track_anchor must match raw +0x44");
_Static_assert(offsetof(fzgx_track_frame_record, track_width_or_radius) == 0x68,
               "fzgx_track_frame_record.track_width_or_radius must match raw +0x68");
_Static_assert(offsetof(fzgx_track_frame_record, track_follow_offset) == 0x70,
               "fzgx_track_frame_record.track_follow_offset must match raw +0x70");
_Static_assert(offsetof(fzgx_track_frame_record, track_flags) == 0x7c,
               "fzgx_track_frame_record.track_flags must match raw +0x7c");

enum {
  FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT = 0xc00u,
  FZGX_COLLISION_SCRATCH_MATERIAL_FLAGS_OFFSET_EXACT = 0x1d4u,
  FZGX_COLLISION_SCRATCH_SLOT3_PRESENT_OFFSET_EXACT = 0x1f0u,
  FZGX_COLLISION_SCRATCH_SLOT3_PUSH_OFFSET_EXACT = 0x210u,
  FZGX_COLLISION_SCRATCH_SLOT3_INFO_OFFSET_EXACT = 0x228u,
  FZGX_COLLISION_SCRATCH_SLOT1_PRESENT_OFFSET_EXACT = 0x238u,
  FZGX_COLLISION_SCRATCH_SLOT1_PUSH_OFFSET_EXACT = 0x258u,
  FZGX_COLLISION_SCRATCH_SLOT1_INFO_OFFSET_EXACT = 0x270u,
  FZGX_COLLISION_SCRATCH_SLOT0_PRESENT_OFFSET_EXACT = 0x280u,
  FZGX_COLLISION_SCRATCH_SLOT0_PUSH_OFFSET_EXACT = 0x2a0u,
  FZGX_COLLISION_SCRATCH_SLOT0_INFO_OFFSET_EXACT = 0x2b8u,
  FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT = 0x10cu,
  FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT = 0x110u,
  FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT = 0x114u,
  FZGX_COLLISION_SCRATCH_SURFACE_FLAGS_OFFSET_EXACT = 0x118u,
  FZGX_COLLISION_SCRATCH_MACHINE_OBSTACLE_COLLISION_OFFSET_EXACT = 0x1b0u,
  FZGX_COLLISION_SCRATCH_MACHINE_TRACK_COLLISION_OFFSET_EXACT = 0x1b4u,
  FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT = 0x1b8u,
  FZGX_COLLISION_SCRATCH_TRACK_QUERY_BRANCH_SELECTOR_OFFSET_EXACT = 0x1bcu,
  FZGX_COLLISION_SCRATCH_TRACK_QUERY_CHECKPOINT_INDEX_OFFSET_EXACT = 0x1c0u,
  FZGX_COLLISION_SCRATCH_TRACK_QUERY_CHECKPOINT_FRACTION_OFFSET_EXACT = 0x1c4u,
  FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT = 0x1c8u,
  FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT = 0x1ccu,
  FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT = 0x1d0u,
  FZGX_COLLISION_SCRATCH_OFFSET_0X1D4_EXACT = 0x1d4u,
  FZGX_COLLISION_SCRATCH_TRACK_QUERY_CONTINUITY_GATE_OFFSET_EXACT = 0x1d8u,
  FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT = 0x1dcu,
  FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT = 0x1e0u,
  FZGX_COLLISION_SCRATCH_HIT_INFO_FLAGS_OFFSET_EXACT = 0x160u,
  FZGX_COLLISION_SCRATCH_HIT_NORMAL_OFFSET_EXACT = 0x180u,
  FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT = 0x1e4u,
  FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT = 0x1f0u,
  FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT = 0x22cu,
  FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT = 0x238u,
  FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_SLOT_OFFSET_EXACT = 0x274u,
  FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT = 0x280u,
  FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT = 0x2bcu,
  FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT = 0x2c8u,
  FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT = 0x304u,
  FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT = 0x310u,
  FZGX_COLLISION_SCRATCH_PENDING_SIDE_BANK_OFFSET_EXACT = 0x34cu,
  FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT = 0x5acu,
  FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT = 0x5b0u,
  FZGX_COLLISION_SCRATCH_PENDING_SIDE_SUMMARY_SLOT_MAP_OFFSET_EXACT = 0x58cu,
  FZGX_COLLISION_SCRATCH_OFFSET_0X62C_EXACT = 0x62cu,
  FZGX_COLLISION_SCRATCH_REJECTION_FRAME_BANK_OFFSET_EXACT = 0x7b0u,
  FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT = 0x10u,
  FZGX_COLLISION_SCRATCH_HIT_SLOT_POINT_OFFSET_EXACT = 0x14u,
  FZGX_COLLISION_SCRATCH_HIT_SLOT_NORMAL_OFFSET_EXACT = 0x20u,
  FZGX_COLLISION_SCRATCH_HIT_SLOT_PUSH_OFFSET_EXACT = 0x2cu,
  FZGX_COLLISION_SCRATCH_HIT_SLOT_AUX_POINT_OFFSET_EXACT = 0x38u,
  FZGX_COLLISION_SCRATCH_HIT_SLOT_OPAQUE_OFFSET_EXACT = 0x44u,
  FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT = 0x5b0u
};

static uint8_t *fzgx_collision_scratch_raw_exact(fzgx_sim_world *world) {
  return (world != 0) ? world->exact_collision_scratch_raw : 0;
}

static const uint8_t *fzgx_collision_scratch_raw_const_exact(const fzgx_sim_world *world) {
  return (world != 0) ? world->exact_collision_scratch_raw : 0;
}

static uint8_t *g_fzgx_collision_scratch_raw_current_exact = 0;
static fzgx_track_side_query_buffer_exact *g_fzgx_collision_piece_scratch_current_exact = 0;

static uint32_t fzgx_collision_scratch_read_u32_current_exact(size_t offset) {
  uint32_t value = 0u;

  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return 0u;
  }
  memcpy(&value, g_fzgx_collision_scratch_raw_current_exact + offset, sizeof(value));
  return value;
}

static float fzgx_collision_scratch_read_f32_current_exact(size_t offset) {
  float value = 0.0f;

  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return 0.0f;
  }
  memcpy(&value, g_fzgx_collision_scratch_raw_current_exact + offset, sizeof(value));
  return value;
}

static void fzgx_collision_scratch_write_u32_current_exact(size_t offset, uint32_t value) {
  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(g_fzgx_collision_scratch_raw_current_exact + offset, &value, sizeof(value));
}

static void fzgx_collision_scratch_write_f32_current_exact(size_t offset, float value) {
  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(g_fzgx_collision_scratch_raw_current_exact + offset, &value, sizeof(value));
}

static void fzgx_collision_scratch_or_u32_current_exact(size_t offset, uint32_t value) {
  uint32_t current_value;

  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((offset + sizeof(current_value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(
      &current_value,
      g_fzgx_collision_scratch_raw_current_exact + offset,
      sizeof(current_value));
  current_value |= value;
  memcpy(
      g_fzgx_collision_scratch_raw_current_exact + offset,
      &current_value,
      sizeof(current_value));
}

static void fzgx_collision_scratch_write_vec3_current_exact(size_t offset, fzgx_vec3 value) {
  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(g_fzgx_collision_scratch_raw_current_exact + offset, &value, sizeof(value));
}

static void fzgx_collision_scratch_copy_current_exact(
    size_t dst_offset,
    size_t src_offset,
    size_t size) {
  if ((g_fzgx_collision_scratch_raw_current_exact == 0) ||
      ((dst_offset + size) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT) ||
      ((src_offset + size) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(
      g_fzgx_collision_scratch_raw_current_exact + dst_offset,
      g_fzgx_collision_scratch_raw_current_exact + src_offset,
      size);
}

static void fzgx_collision_scratch_set_piece_scratch_field_exact(
    fzgx_sim_world *world,
    fzgx_track_side_query_buffer_exact *piece_scratch) {
  if (world == 0) {
    return;
  }
  world->exact_collision_piece_scratch_0x700 = piece_scratch;
}

static uint32_t fzgx_track_side_query_summary_slot_flags_ptr_opaque_exact(
    const fzgx_track_side_query_buffer_exact *buffer,
    uintptr_t summary_flags_ptr_exact);
static uintptr_t fzgx_track_side_query_summary_slot_flags_ptr_from_opaque_exact(
    const fzgx_track_side_query_buffer_exact *buffer,
    uint32_t summary_flags_ptr_opaque);

static void fzgx_collision_scratch_write_u32_exact(
    fzgx_sim_world *world,
    size_t offset,
    uint32_t value) {
  uint8_t *raw = fzgx_collision_scratch_raw_exact(world);

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(raw + offset, &value, sizeof(value));
}

static void fzgx_collision_scratch_write_s32_exact(
    fzgx_sim_world *world,
    size_t offset,
    int32_t value) {
  uint8_t *raw = fzgx_collision_scratch_raw_exact(world);

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(raw + offset, &value, sizeof(value));
}

static void fzgx_collision_scratch_write_f32_exact(
    fzgx_sim_world *world,
    size_t offset,
    float value) {
  uint8_t *raw = fzgx_collision_scratch_raw_exact(world);

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(raw + offset, &value, sizeof(value));
}

static void fzgx_collision_scratch_write_vec3_exact(
    fzgx_sim_world *world,
    size_t offset,
    fzgx_vec3 value) {
  uint8_t *raw = fzgx_collision_scratch_raw_exact(world);

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return;
  }
  memcpy(raw + offset, &value, sizeof(value));
}

static uint32_t fzgx_collision_scratch_read_u32_exact(
    const fzgx_sim_world *world,
    size_t offset) {
  const uint8_t *raw = fzgx_collision_scratch_raw_const_exact(world);
  uint32_t value = 0u;

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return 0u;
  }
  memcpy(&value, raw + offset, sizeof(value));
  return value;
}

static int32_t fzgx_collision_scratch_read_s32_exact(
    const fzgx_sim_world *world,
    size_t offset) {
  const uint8_t *raw = fzgx_collision_scratch_raw_const_exact(world);
  int32_t value = 0;

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return 0;
  }
  memcpy(&value, raw + offset, sizeof(value));
  return value;
}

static float fzgx_collision_scratch_read_f32_exact(
    const fzgx_sim_world *world,
    size_t offset) {
  const uint8_t *raw = fzgx_collision_scratch_raw_const_exact(world);
  float value = 0.0f;

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return 0.0f;
  }
  memcpy(&value, raw + offset, sizeof(value));
  return value;
}

static fzgx_vec3 fzgx_collision_scratch_read_vec3_exact(
    const fzgx_sim_world *world,
    size_t offset) {
  const uint8_t *raw = fzgx_collision_scratch_raw_const_exact(world);
  fzgx_vec3 value = {0.0f, 0.0f, 0.0f};

  if ((raw == 0) || ((offset + sizeof(value)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT)) {
    return value;
  }
  memcpy(&value, raw + offset, sizeof(value));
  return value;
}

static void fzgx_collision_scratch_reset_exact(fzgx_sim_world *world) {
  if (world == 0) {
    return;
  }

  memset(
      world->exact_collision_scratch_raw,
      0,
      FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT +
          FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT,
      -1.0f);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
          FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT,
      -1.0f);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
          FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT,
      -1.0f);
}

static uint32_t fzgx_collision_scratch_get_selected_frame_flags_exact(
    const fzgx_sim_world *world) {
  const uint8_t *raw = fzgx_collision_scratch_raw_const_exact(world);
  uint32_t cached_frame_count;
  int32_t selected_cached_frame_index;
  uint32_t track_flags = 0u;
  size_t frame_offset;

  if (raw == 0) {
    return 0u;
  }
  cached_frame_count = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT);
  selected_cached_frame_index = fzgx_collision_scratch_read_s32_exact(
      world, FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT);
  if ((selected_cached_frame_index < 0) ||
      ((uint32_t)selected_cached_frame_index >= cached_frame_count) ||
      ((uint32_t)selected_cached_frame_index >= FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY)) {
    return 0u;
  }

  frame_offset =
      FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT +
      (sizeof(fzgx_track_frame_record) * (size_t)(uint32_t)selected_cached_frame_index) +
      offsetof(fzgx_track_frame_record, track_flags);
  if ((frame_offset + sizeof(track_flags)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT) {
    return 0u;
  }
  memcpy(&track_flags, raw + frame_offset, sizeof(track_flags));
  return track_flags;
}

static fzgx_status fzgx_prepare_track_collision_query_for_checkpoint_exact(
    fzgx_sim_world *world,
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    double checkpoint_fraction,
    int32_t checkpoint_index,
    fzgx_track_segment_trs_curve_cache_exact *curve_cache_inout) {
  const fzgx_track_node_record *track_node;
  const fzgx_checkpoint_record *checkpoint = 0;
  const fzgx_track_segment_record *track_segment = 0;
  uint8_t *scratch_raw;
  fzgx_track_frame_record *cached_frames;
  fzgx_mat43 transform;
  fzgx_vec3 scale;
  float curve_time;
  uint32_t cached_frame_cursor = 0u;
  uint32_t cached_frame_count;
  int32_t seed_limit;
  fzgx_status status;

  if ((world == 0) || (course == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((checkpoint_index < 0) || ((uint32_t)checkpoint_index >= course->track_node_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  scratch_raw = fzgx_collision_scratch_raw_exact(world);
  if (scratch_raw == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  cached_frames = (fzgx_track_frame_record *)(void *)(scratch_raw +
                                                     FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT);
  track_node = &course->track_nodes[(uint32_t)checkpoint_index];
  status = fzgx_track_course_get_checkpoint_variant(
      course, (uint32_t)checkpoint_index, 0u, &checkpoint);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_course_find_track_segment_by_address(
      course, track_node->root_segment_address, &track_segment);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  curve_time =
      checkpoint->curve_time_start +
      ((float)checkpoint_fraction * (checkpoint->curve_time_end - checkpoint->curve_time_start));

  fzgx_collision_scratch_reset_exact(world);
  fzgx_collision_scratch_write_u32_exact(world, FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT, 0u);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT, 0u);
  fzgx_collision_scratch_write_u32_exact(world, FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT, 0u);
  fzgx_collision_scratch_write_u32_exact(world, FZGX_COLLISION_SCRATCH_OFFSET_0X1D4_EXACT, 0u);
  fzgx_collision_scratch_write_u32_exact(world, FZGX_COLLISION_SCRATCH_OFFSET_0X62C_EXACT, 0u);
  fzgx_collision_scratch_write_u32_exact(world, FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT, 0u);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT, 0u);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT + sizeof(uint32_t),
      -1.0f);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT, 0u);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT + sizeof(uint32_t),
      -1.0f);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT, 0u);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT + sizeof(uint32_t),
      -1.0f);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT, 0u);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT + sizeof(uint32_t),
      -1.0f);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT, 0u);
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT + sizeof(uint32_t),
      -1.0f);
  fzgx_collision_scratch_write_f32_exact(
      world, FZGX_COLLISION_SCRATCH_MACHINE_OBSTACLE_COLLISION_OFFSET_EXACT, 2.0f);
  fzgx_collision_scratch_write_f32_exact(
      world, FZGX_COLLISION_SCRATCH_MACHINE_TRACK_COLLISION_OFFSET_EXACT, 0.0f);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_BRANCH_SELECTOR_OFFSET_EXACT, 1u);
  memset(
      scratch_raw + FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT,
      0,
      sizeof(fzgx_track_frame_record) * FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY);
  memset(&transform, 0, sizeof(transform));
  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  status = fzgx_populate_track_cached_frames_from_piece_tree_exact(
      course,
      animation_course,
      track_segment,
      curve_time,
      &transform,
      &scale,
      curve_cache_inout,
      cached_frames,
      FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
      &cached_frame_cursor);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  cached_frame_count = cached_frame_cursor + 1u;
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT, cached_frame_count);
  seed_limit = (int32_t)cached_frame_count;
  if (1 < seed_limit) {
    seed_limit -= 1;
  }
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT, (uint32_t)seed_limit);

  return FZGX_STATUS_OK;
}

typedef struct fzgx_sincos_result {
  float sin_value;
  float cos_value;
} fzgx_sincos_result;

static const fzgx_track_manifest *fzgx_get_active_track_manifest(
    const fzgx_sim_world *world);
static fzgx_status fzgx_get_active_track_course(
    const fzgx_sim_world *world,
    const fzgx_track_course_content **course_out);
static int32_t fzgx_get_machine_active_checkpoint_index(
    const fzgx_machine_track_state *track);
static float fzgx_get_machine_active_checkpoint_fraction(
    const fzgx_machine_track_state *track);
static fzgx_status fzgx_build_current_track_query_from_shared_point_exact(
    const fzgx_sim_world *world,
    const fzgx_vec3 *point,
    fzgx_current_track_query_result *query_out);

static const double fzgx_crash_restore_damage_addend_exact = 90.0;
static const double fzgx_respawn_target_capture_frame_exact = 150.0;
static const double fzgx_track_summary_checkpoint_traverse_gap_exact = 200.0;
static const float fzgx_respawn_progress_delta_exact = 0.0004938271595165133f;
static const float fzgx_checkpoint_neighborhood_max_vertical_delta_exact = 200.0f;
static const float fzgx_generic_surface_push_bias_exact = 0.5f;
static const double fzgx_track_round_cross_section_scale_epsilon_exact = 0.01;
static const float fzgx_track_round_cross_section_start_offset_exact = 2000.0f;
static const float fzgx_track_round_cross_section_radius_squared_exact = 0.25f;
static const float fzgx_track_side_rejection_epsilon_exact = 0.1f;
static const float fzgx_track_modulated_surface_accept_x_exact = 0.5f;
static const float fzgx_track_road_lateral_min_exact = -0.5f;
static const float fzgx_track_road_lateral_max_exact = 0.5f;
static const float fzgx_track_side_surface_summary_height_min_exact = 5.0f;
static const float fzgx_track_side_surface_summary_course3_height_min_exact = 50.0f;
static const float fzgx_track_pipe_family_lateral_limit_exact = 1.75f;
static const float fzgx_track_open_pipe_cutoff_angle_scale_exact = 32768.0f;
static const float fzgx_track_course33_pipe_material_cp19_min_fraction_exact = 0.8f;
static const float fzgx_track_course33_pipe_material_cp22_max_fraction_exact = 0.32f;
static const float fzgx_track_course33_pipe_material_branch2_cp40_max_fraction_exact = 0.877f;
static const uint32_t fzgx_mode_story_exact = 9u;
static const float fzgx_track_fit_course9_window_pairs_exact[4][2] = {
    {16361.0f, 16361.0f},
    {16661.0f, 16661.0f},
    {16961.0f, 16961.0f},
    {17261.0f, 17261.0f},
};
static const float fzgx_track_fit_course13_window_pairs_exact[7][2] = {
    {6459.0f, 6496.0f},
    {7259.0f, 7296.0f},
    {8059.0f, 8096.0f},
    {13407.0f, 13425.0f},
    {13534.0f, 13552.0f},
    {13661.0f, 13679.0f},
    {14137.0f, 14152.0f},
};

static float fzgx_vec3_length(fzgx_vec3 v);
static fzgx_vec3 fzgx_transform_local_vector(
    const fzgx_mat43 *transform,
    fzgx_vec3 local_vector);
static fzgx_vec3 fzgx_transform_local_point(
    const fzgx_mat43 *transform,
    fzgx_vec3 local_point);
static fzgx_vec3 fzgx_world_point_to_local(
    const fzgx_mat43 *track_transform,
    fzgx_vec3 world_point);
static fzgx_vec3 fzgx_world_point_to_local_scaled_orthogonal_exact(
    const fzgx_mat43 *transform,
    fzgx_vec3 world_point);
static fzgx_mat43 fzgx_mat43_identity_exact(void);
static float fzgx_vec3_distance_squared_exact(fzgx_vec3 a, fzgx_vec3 b);
static void fzgx_mat43_rotate_about_x_right(fzgx_mat43 *transform, uint16_t angle);
static void fzgx_mat43_rotate_about_y_right(fzgx_mat43 *transform, uint16_t angle);
static void fzgx_mat43_rotate_about_z_right(fzgx_mat43 *transform, uint16_t angle);
static void fzgx_mat43_translate_local_exact(fzgx_mat43 *transform, fzgx_vec3 local_offset);
static void fzgx_mat43_rigid_invert_exact(fzgx_mat43 *transform);
static fzgx_vec3 fzgx_vec3_normalize_or_exact(fzgx_vec3 vector, fzgx_vec3 fallback);
static fzgx_status fzgx_refresh_machine_track_fit_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_mat43 *transform_out);
static fzgx_status fzgx_build_machine_track_fit_transform_from_racetrack_state_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_mat43 *transform_out);
static fzgx_status fzgx_reset_machine_from_current_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units);
static fzgx_status fzgx_place_machine_from_current_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units);
static bool fzgx_track_id_is_sonic_oval_exact(uint32_t authored_track_id);
static bool fzgx_refresh_machine_wall_contact_queries_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine);
static fzgx_vec3 fzgx_vec3_lerp_exact(fzgx_vec3 a, fzgx_vec3 b, float t);
static void fzgx_apply_entrant_visual_scale_table_exact(
    const fzgx_machine_snapshot *machine,
    fzgx_mat43 *transform_inout);
static fzgx_vec3 fzgx_world_point_to_track_frame_local_exact(
    const fzgx_track_frame_record *frame,
    fzgx_vec3 world_point);

static float fzgx_plane_eval_point_exact(const fzgx_plane *plane, const fzgx_vec3 *point) {
  return plane->distance + point->x * plane->normal.x + point->y * plane->normal.y +
         point->z * plane->normal.z;
}

static uint32_t fzgx_float_bits_exact(float value) {
  uint32_t bits = 0u;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static void fzgx_apply_machine_flags_from_exact_state(fzgx_machine_snapshot *machine) {
  uint32_t flags = machine->machine_flags;

  if ((machine->machine_state & FZGX_MS_ACTIVE) != 0u) {
    flags |= FZGX_MACHINE_FLAG_ACTIVE;
  } else {
    flags &= ~FZGX_MACHINE_FLAG_ACTIVE;
  }
  if ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) {
    flags |= FZGX_MACHINE_FLAG_AIRBORNE;
  } else {
    flags &= ~FZGX_MACHINE_FLAG_AIRBORNE;
  }
  if ((machine->machine_state & FZGX_MS_SIDEATTACKING) != 0u) {
    flags |= FZGX_MACHINE_FLAG_SIDE_ATTACKING;
  } else {
    flags &= ~FZGX_MACHINE_FLAG_SIDE_ATTACKING;
  }
  if ((machine->machine_state & FZGX_MS_SPINATTACKING) != 0u) {
    flags |= FZGX_MACHINE_FLAG_SPIN_ATTACKING;
  } else {
    flags &= ~FZGX_MACHINE_FLAG_SPIN_ATTACKING;
  }
  if ((machine->machine_state & FZGX_MS_0HP) != 0u) {
    flags |= FZGX_MACHINE_FLAG_ZERO_HP;
  } else {
    flags &= ~FZGX_MACHINE_FLAG_ZERO_HP;
  }
  if ((machine->machine_state & FZGX_MS_FALLOUT) != 0u) {
    flags |= FZGX_MACHINE_FLAG_FALLOUT;
  } else {
    flags &= ~FZGX_MACHINE_FLAG_FALLOUT;
  }
  machine->machine_flags = flags;
}

static void fzgx_update_machine_stats_from_definition(
    fzgx_machine_snapshot *machine,
    const fzgx_machine_definition *definition) {
  size_t i;

  machine->stat_weight = definition->weight;
  machine->stat_grip_1 = definition->grip_1;
  machine->stat_grip_3 = definition->grip_3;
  machine->stat_turn_movement = definition->turn_movement;
  machine->stat_strafe = definition->strafe;
  machine->stat_turn_reaction = definition->turn_reaction;
  machine->stat_grip_2 = definition->grip_2;
  machine->stat_body = definition->body;
  machine->stat_turn_tension = definition->turn_tension;
  machine->stat_drift_accel = definition->drift_accel;
  machine->stat_grip_frames_from_accel_press = definition->grip_frames_from_accel_press;
  machine->camera_reorienting = definition->camera_reorienting;
  machine->camera_repositioning = definition->camera_repositioning;
  machine->stat_strafe_turn = definition->strafe_turn;
  machine->stat_acceleration = definition->acceleration;
  machine->stat_max_speed = definition->max_speed;
  machine->stat_boost_strength = 0.57f * definition->boost_strength;
  machine->stat_boost_length = definition->boost_length;
  machine->stat_turn_decel = definition->turn_decel;
  machine->stat_drag = definition->drag;
  machine->weight_derived_1 = 52.0f * machine->stat_weight * 0.0625f;
  machine->weight_derived_2 = 45.0f * machine->stat_weight * 0.0625f;
  machine->weight_derived_3 = 52.0f * machine->stat_weight * 0.0625f;
  machine->stat_obstacle_collision = 0.0f;
  machine->stat_track_collision = 1.0f;
  for (i = 0u; i < 4u; ++i) {
    float wall_offset_len;
    float wall_offset_x_abs;

    machine->suspension_corners[i].offset = definition->suspension_offsets[i];
    machine->suspension_corners[i].rest_length_scale = 1.7f;
    machine->wall_corners[i].offset = definition->wall_offsets[i];
    wall_offset_len = fzgx_vec3_length(machine->wall_corners[i].offset);
    if (machine->stat_obstacle_collision < wall_offset_len) {
      machine->stat_obstacle_collision = wall_offset_len;
    }
    wall_offset_x_abs = fabsf(machine->wall_corners[i].offset.x);
    if (machine->stat_track_collision < wall_offset_x_abs) {
      machine->stat_track_collision = wall_offset_x_abs;
    }
  }
  machine->stat_obstacle_collision += 0.1f;
  if ((definition->state_flags & 1u) == 0u) {
    machine->machine_state &= ~FZGX_MS_B9;
  } else {
    machine->machine_state |= FZGX_MS_B9;
  }
  if ((definition->state_flags & 2u) != 0u) {
    machine->machine_state |= FZGX_MS_VEHICLEACTIVE_Q;
  } else {
    machine->machine_state &= ~FZGX_MS_B1;
  }
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static void fzgx_derive_machine_base_stat_values_exact(
    float balance,
    fzgx_machine_definition *stats) {
  float fVar1;
  float fVar2;
  bool bVar3;
  float fVar4;
  double dVar5;
  double dVar6;
  double dVar7;
  double dVar8;
  double dVar9;

  bVar3 = true;
  dVar8 = (double)0.0f;
  dVar7 = (double)(balance - 0.5f);
  if (dVar7 <= dVar8) {
    dVar5 = (double)stats->drift_accel;
    dVar6 = (double)1.4f;
    if ((double)1.0f <= dVar5) {
      if ((double)1.5f <= dVar5) {
        stats->drift_accel =
            (float)(dVar5 - (double)((1.2f - (float)(dVar5 - (double)1.5f)) *
                                     (float)(dVar5 * dVar7)));
      } else {
        stats->drift_accel = (float)(dVar5 - (double)(1.2f * (float)(dVar5 * dVar7)));
      }
    } else {
      stats->drift_accel =
          (float)(dVar5 - (double)(float)((double)2.0f *
                                          (double)(float)((double)(float)((double)2.0f - dVar5) *
                                                         dVar7)));
    }
    dVar5 = dVar8;
    if (2.3f < stats->drift_accel) {
      stats->drift_accel = 2.3f;
    }
  } else {
    dVar5 = (double)1.0f;
    dVar9 = (double)stats->drift_accel;
    dVar6 = dVar8;
    if (dVar5 < dVar9) {
      stats->drift_accel = (float)(dVar9 - (double)(1.8f * (float)(dVar9 * dVar7)));
    }
  }
  if (((dVar7 < (double)0.0f) && (0.5f <= stats->acceleration)) &&
      (stats->max_speed <= 0.2f)) {
    bVar3 = false;
  }
  dVar8 = (double)stats->max_speed;
  if (dVar7 <= (double)0.0f) {
    fVar1 = (float)(dVar8 - (double)0.12f) / 0.08f;
    if (1.0f < fVar1) {
      fVar1 = 1.0f;
    }
    fVar1 = 0.45f * (0.4f + 0.2f * fVar1);
  } else {
    if (0.4f <= stats->acceleration) {
      dVar9 = (double)1.0f;
      if ((0.5f <= stats->acceleration) &&
          (dVar9 = (double)0.0f, (double)0.15f <= dVar8)) {
        dVar9 = (double)-0.25f;
      }
    } else {
      dVar9 = (double)3.2f;
    }
    fVar1 = (float)((double)0.16f * dVar9);
  }
  fVar1 = (float)(dVar7 * fabs((double)(float)(dVar5 - dVar8))) * fVar1;
  dVar8 = (double)stats->acceleration;
  if ((dVar8 <= (double)0.6f) || ((double)0.0f <= dVar7)) {
    stats->acceleration =
        (float)((double)stats->acceleration +
                0.6 * -dVar7 * fabs((double)(float)(dVar6 - dVar8)));
  } else {
    stats->acceleration =
        (float)(dVar8 + (double)(float)((double)2.0f * dVar7) *
                            fabs((double)(float)((double)0.7f - dVar8)));
  }
  fVar4 = 0.01f;
  if (stats->acceleration < 0.4f) {
    fVar2 = 1.0f;
    if (stats->acceleration < 0.31f) {
      fVar1 = fVar1 * 1.5f;
      fVar2 = 1.5f;
    }
    if ((double)0.03f < (double)stats->turn_decel) {
      fVar2 = fVar2 * 1.5f;
    }
    if (dVar7 < (double)0.0f) {
      fVar2 = fVar2 * 2.0f;
    }
    stats->turn_decel =
        stats->turn_decel - fabsf(0.7f * fVar2 * (float)((double)stats->turn_decel * dVar7));
    if (stats->turn_decel < fVar4) {
      stats->turn_decel = fVar4;
    }
  }
  if ((stats->weight < 700.0f) && (0.7f < stats->acceleration)) {
    stats->acceleration = 0.7f;
  }
  dVar8 = (double)0.0f;
  stats->max_speed = stats->max_speed + fVar1;
  if (dVar7 <= dVar8) {
    stats->turn_movement = stats->turn_movement * (1.0f - (float)((double)0.2f * dVar7));
  } else {
    stats->turn_movement = stats->turn_movement * (1.0f - (float)((double)0.6f * dVar7));
  }
  fVar4 = 1.0f;
  fVar1 = 1.0f + (float)((double)0.25f * dVar7);
  stats->grip_1 = stats->grip_1 * fVar1;
  stats->grip_3 = stats->grip_3 * fVar1;
  if (bVar3) {
    stats->boost_strength = stats->boost_strength * (fVar4 + (float)((double)0.1f * dVar7));
  }
}

static void fzgx_reset_machine_corner_runtime_exact(fzgx_machine_snapshot *machine) {
  size_t i;
  fzgx_mat43 placement_transform = machine->basis_physical;
  fzgx_vec3 up_vector = fzgx_mat43_get_basis_y_exact(&machine->basis_physical);

  fzgx_mat43_set_origin_exact(&placement_transform, machine->position);

  for (i = 0u; i < 4u; ++i) {
    fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[i];
    fzgx_machine_wall_corner_snapshot *wall = &machine->wall_corners[i];

    corner->state = 0u;
    corner->force = 0.0f;
    corner->force_spatial_len = 0.0f;
    corner->pos_old = fzgx_transform_local_point(&placement_transform, corner->offset);
    corner->pos = fzgx_transform_local_point(&placement_transform, corner->offset);
    corner->force_spatial = (fzgx_vec3){0};
    corner->up_vector_2 = up_vector;
    corner->up_vector = up_vector;
    wall->pos_a = fzgx_transform_local_point(
        &placement_transform, (fzgx_vec3){0.0f, 0.1f, 0.0f});
    wall->pos_b = fzgx_transform_local_point(&placement_transform, wall->offset);
    wall->collision = (fzgx_vec3){0};
    memset(&machine->corner_scratch[i], 0, sizeof(machine->corner_scratch[i]));
    machine->corner_scratch[i].write_record_offset = 0x08u;
    machine->corner_scratch[i].latest_record_offset = 0x64u;
  }
}

static fzgx_track_side_query_summary *fzgx_track_side_query_buffer_get_record_exact(
    fzgx_track_side_query_buffer_exact *buffer,
    uint32_t record_offset) {
  if (buffer == 0) {
    return 0;
  }
  switch (record_offset) {
    case 0x08u:
      return &buffer->records[0];
    case 0x64u:
      return &buffer->records[1];
    default:
      return 0;
  }
}

static const fzgx_track_side_query_summary *fzgx_track_side_query_buffer_get_record_const_exact(
    const fzgx_track_side_query_buffer_exact *buffer,
    uint32_t record_offset) {
  if (buffer == 0) {
    return 0;
  }
  switch (record_offset) {
    case 0x08u:
      return &buffer->records[0];
    case 0x64u:
      return &buffer->records[1];
    default:
      return 0;
  }
}

static void fzgx_track_side_query_buffer_reset_write_record_exact(
    fzgx_track_side_query_buffer_exact *buffer) {
  fzgx_track_side_query_summary *write_summary;

  if (buffer == 0) {
    return;
  }
  write_summary =
      fzgx_track_side_query_buffer_get_record_exact(buffer, buffer->write_record_offset);
  if (write_summary == 0) {
    return;
  }
  write_summary->candidate_count = 0u;
  write_summary->summary_flags = 0u;
  write_summary->special_postprocess_flag = 0u;
}

static const fzgx_track_side_query_summary *fzgx_get_machine_corner_write_summary_const_exact(
    const fzgx_machine_snapshot *machine,
    size_t corner_index) {
  if ((machine == 0) || (corner_index >= 4u)) {
    return 0;
  }
  return fzgx_track_side_query_buffer_get_record_const_exact(
      &machine->corner_scratch[corner_index],
      machine->corner_scratch[corner_index].write_record_offset);
}

static fzgx_track_side_query_summary *fzgx_get_machine_corner_latest_summary_exact(
    fzgx_machine_snapshot *machine,
    size_t corner_index) {
  if ((machine == 0) || (corner_index >= 4u)) {
    return 0;
  }
  return fzgx_track_side_query_buffer_get_record_exact(
      &machine->corner_scratch[corner_index],
      machine->corner_scratch[corner_index].latest_record_offset);
}

static const fzgx_track_side_query_summary *fzgx_get_machine_corner_latest_summary_const_exact(
    const fzgx_machine_snapshot *machine,
    size_t corner_index) {
  if ((machine == 0) || (corner_index >= 4u)) {
    return 0;
  }
  return fzgx_track_side_query_buffer_get_record_const_exact(
      &machine->corner_scratch[corner_index],
      machine->corner_scratch[corner_index].latest_record_offset);
}

static void fzgx_swap_machine_corner_contact_scratch_exact(
    fzgx_machine_snapshot *machine,
    size_t corner_index) {
  fzgx_track_side_query_buffer_exact *buffer;
  uint32_t write_record_offset;

  if ((machine == 0) || (corner_index >= 4u)) {
    return;
  }
  buffer = &machine->corner_scratch[corner_index];
  write_record_offset = buffer->write_record_offset;

  buffer->write_record_offset = buffer->latest_record_offset;
  buffer->latest_record_offset = write_record_offset;
}

static void fzgx_set_track_side_query_summary_hit_normal_exact(
    fzgx_track_side_query_summary *summary,
    fzgx_vec3 normal) {
  memcpy(summary->reserved0, &normal, sizeof(normal));
}

static fzgx_vec3 fzgx_get_track_side_query_summary_hit_normal_exact(
    const fzgx_track_side_query_summary *summary) {
  fzgx_vec3 normal = {0.0f, 0.0f, 0.0f};

  memcpy(&normal, summary->reserved0, sizeof(normal));
  return normal;
}

static uintptr_t fzgx_track_side_query_summary_slot_flags_ptr_exact(
    fzgx_track_side_query_summary *summary,
    uint32_t slot_index) {
  if ((summary == 0) || (slot_index >= 4u)) {
    return (uintptr_t)0;
  }
  return (uintptr_t)&summary->flags[slot_index];
}

static uint32_t fzgx_track_side_query_summary_slot_flags_ptr_opaque_exact(
    const fzgx_track_side_query_buffer_exact *buffer,
    uintptr_t summary_flags_ptr_exact) {
  uint32_t record_index;
  uint32_t slot_index;

  if ((buffer == 0) || (summary_flags_ptr_exact == (uintptr_t)0)) {
    return 0u;
  }
  for (record_index = 0u; record_index < 2u; ++record_index) {
    for (slot_index = 0u; slot_index < 4u; ++slot_index) {
      uintptr_t candidate_ptr = (uintptr_t)&buffer->records[record_index].flags[slot_index];

      if (candidate_ptr == summary_flags_ptr_exact) {
        return (uint32_t)(candidate_ptr - (uintptr_t)buffer);
      }
    }
  }
  return 0u;
}

static uintptr_t fzgx_track_side_query_summary_slot_flags_ptr_from_opaque_exact(
    const fzgx_track_side_query_buffer_exact *buffer,
    uint32_t summary_flags_ptr_opaque) {
  uintptr_t candidate_ptr;
  uint32_t record_index;
  uint32_t slot_index;

  if ((buffer == 0) || (summary_flags_ptr_opaque == 0u)) {
    return (uintptr_t)0;
  }
  candidate_ptr = (uintptr_t)buffer + (uintptr_t)summary_flags_ptr_opaque;
  for (record_index = 0u; record_index < 2u; ++record_index) {
    for (slot_index = 0u; slot_index < 4u; ++slot_index) {
      if ((uintptr_t)&buffer->records[record_index].flags[slot_index] == candidate_ptr) {
        return candidate_ptr;
      }
    }
  }
  return (uintptr_t)0;
}

static uint32_t fzgx_clamp_track_side_query_summary_slot_exact(uint32_t slot_index) {
  return (slot_index <= 3u) ? slot_index : 3u;
}

static uintptr_t fzgx_request_previous_side_summary_slot_flags_ptr_exact(
    const fzgx_world_spherecast_request *request,
    uint32_t slot_index) {
  if ((request == 0) || (request->previous_side_summary_live_exact == (uintptr_t)0)) {
    return (uintptr_t)0;
  }
  return fzgx_track_side_query_summary_slot_flags_ptr_exact(
      (fzgx_track_side_query_summary *)(uintptr_t)request->previous_side_summary_live_exact,
      fzgx_clamp_track_side_query_summary_slot_exact(slot_index));
}

static uintptr_t fzgx_request_current_side_summary_slot_flags_ptr_exact(
    const fzgx_world_spherecast_request *request,
    uint32_t slot_index) {
  if ((request == 0) || (request->current_side_summary_live_exact == (uintptr_t)0)) {
    return (uintptr_t)0;
  }
  return fzgx_track_side_query_summary_slot_flags_ptr_exact(
      (fzgx_track_side_query_summary *)(uintptr_t)request->current_side_summary_live_exact,
      fzgx_clamp_track_side_query_summary_slot_exact(slot_index));
}

static const fzgx_track_side_query_summary *fzgx_request_previous_side_summary_exact(
    const fzgx_world_spherecast_request *request) {
  if (request == 0) {
    return 0;
  }
  if (request->previous_side_summary_live_exact != (uintptr_t)0) {
    return (const fzgx_track_side_query_summary *)(uintptr_t)request->previous_side_summary_live_exact;
  }
  if (request->has_previous_side_summary) {
    return &request->previous_side_summary;
  }
  return 0;
}

static fzgx_track_side_query_summary *fzgx_request_current_side_summary_exact(
    const fzgx_world_spherecast_request *request) {
  if ((request != 0) && (request->current_side_summary_live_exact != (uintptr_t)0)) {
    return (fzgx_track_side_query_summary *)(uintptr_t)request->current_side_summary_live_exact;
  }
  return 0;
}

static uint32_t fzgx_read_summary_flags_ptr_exact(uintptr_t summary_flags_ptr_exact) {
  if (summary_flags_ptr_exact == (uintptr_t)0) {
    return 0u;
  }
  return *(const uint32_t *)(uintptr_t)summary_flags_ptr_exact;
}

static void fzgx_mask_summary_flags_ptr_exact(uintptr_t summary_flags_ptr_exact, uint32_t mask) {
  if (summary_flags_ptr_exact == (uintptr_t)0) {
    return;
  }
  *(uint32_t *)(uintptr_t)summary_flags_ptr_exact &=
      mask;
}

static float fzgx_vec3_dot(fzgx_vec3 a, fzgx_vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static fzgx_vec3 fzgx_vec3_cross(const fzgx_vec3 *lhs, const fzgx_vec3 *rhs) {
  fzgx_vec3 out;
  out.x = lhs->y * rhs->z - lhs->z * rhs->y;
  out.y = lhs->z * rhs->x - lhs->x * rhs->z;
  out.z = lhs->x * rhs->y - lhs->y * rhs->x;
  return out;
}

static fzgx_vec3 fzgx_vec3_sub(fzgx_vec3 a, fzgx_vec3 b) {
  fzgx_vec3 out;
  out.x = a.x - b.x;
  out.y = a.y - b.y;
  out.z = a.z - b.z;
  return out;
}

static fzgx_vec3 fzgx_vec3_add(fzgx_vec3 a, fzgx_vec3 b) {
  fzgx_vec3 out;
  out.x = a.x + b.x;
  out.y = a.y + b.y;
  out.z = a.z + b.z;
  return out;
}

static fzgx_vec3 fzgx_vec3_scale(fzgx_vec3 vector, float scale) {
  fzgx_vec3 out;
  out.x = vector.x * scale;
  out.y = vector.y * scale;
  out.z = vector.z * scale;
  return out;
}

static float fzgx_math_sqrt_exact(float value) {
  if (isnan(value)) {
    if (isinf(value) && (value > 0.0f)) {
      return INFINITY;
    }
    return 0.0f;
  }
  if (value > 0.0f) {
    return sqrtf(value);
  }
  return 0.0f;
}

static float fzgx_vec3_normalized_dot(fzgx_vec3 a, fzgx_vec3 b) {
  return fzgx_vec3_dot(a, b) / fzgx_math_sqrt_exact(fzgx_vec3_dot(a, a) * fzgx_vec3_dot(b, b));
}

static float fzgx_vec3_length(fzgx_vec3 v) {
  return fzgx_math_sqrt_exact(fzgx_vec3_dot(v, v));
}

static bool fzgx_vec3_is_nonzero(fzgx_vec3 vector) {
  return (vector.x != 0.0f) || (vector.y != 0.0f) || (vector.z != 0.0f);
}

static float fzgx_vec3_length_squared(fzgx_vec3 vector) {
  return fzgx_vec3_dot(vector, vector);
}

static void fzgx_vec3_set_length_exact(float length, const fzgx_vec3 *source, fzgx_vec3 *out) {
  float source_length = fzgx_vec3_length(*source);

  if (source_length <= 0.0f) {
    *out = (fzgx_vec3){0};
    return;
  }
  *out = fzgx_vec3_scale(*source, length / source_length);
}

static float fzgx_clamp_unit_exact(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

static float fzgx_output_closest_points_between_lines_and_return_square_dist_exact(
    const fzgx_vec3 *p1,
    const fzgx_vec3 *p2,
    const fzgx_vec3 *p3,
    const fzgx_vec3 *p4,
    fzgx_vec3 *closest1_out,
    fzgx_vec3 *closest2_out) {
  float delta_x = p1->x - p3->x;
  float dir_b_x = p4->x - p3->x;
  float dir_a_x = p2->x - p1->x;
  float dir_b_y = p4->y - p3->y;
  float dir_a_y = p2->y - p1->y;
  float delta_y = p1->y - p3->y;
  float dir_b_z = p4->z - p3->z;
  float delta_z = p1->z - p3->z;
  float dir_a_z = p2->z - p1->z;
  float dot_ab = dir_b_z * dir_a_z + dir_b_x * dir_a_x + dir_b_y * dir_a_y;
  float dot_delta_b = delta_z * dir_b_z + delta_x * dir_b_x + delta_y * dir_b_y;
  float len_sq_b = dir_b_z * dir_b_z + dir_b_x * dir_b_x + dir_b_y * dir_b_y;
  float param_a =
      (dot_delta_b * dot_ab - (delta_z * dir_a_z + delta_x * dir_a_x + delta_y * dir_a_y) * len_sq_b) /
      ((dir_a_z * dir_a_z + dir_a_x * dir_a_x + dir_a_y * dir_a_y) * len_sq_b - dot_ab * dot_ab);
  float param_b;
  float clamped_a = 0.0f;
  float clamped_b = 0.0f;
  fzgx_vec3 delta;

  if ((fzgx_float_bits_exact(param_a) & 0x7f800000u) == 0x7f800000u) {
    param_a = 0.0f;
  }
  param_b = (dot_delta_b + param_a * dot_ab) / len_sq_b;
  if ((fzgx_float_bits_exact(param_b) & 0x7f800000u) == 0x7f800000u) {
    param_b = 0.0f;
  }
  if (0.0f <= param_a) {
    clamped_a = param_a;
    if (1.0f < param_a) {
      clamped_a = 1.0f;
    }
  }
  if ((0.0f <= param_b) && (1.0f < param_b)) {
    clamped_b = 1.0f;
  } else if (0.0f <= param_b) {
    clamped_b = param_b;
  }

  closest1_out->x = p1->x + (p2->x - p1->x) * clamped_a;
  closest1_out->y = p1->y + (p2->y - p1->y) * clamped_a;
  closest1_out->z = p1->z + (p2->z - p1->z) * clamped_a;
  closest2_out->x = p3->x + (p4->x - p3->x) * clamped_b;
  closest2_out->y = p3->y + (p4->y - p3->y) * clamped_b;
  closest2_out->z = p3->z + (p4->z - p3->z) * clamped_b;
  delta = fzgx_vec3_sub(*closest2_out, *closest1_out);
  return fzgx_vec3_length_squared(delta);
}

static bool fzgx_capsule_intersects_convex_polygon_exact(
    float radius,
    fzgx_vec3 segment_start,
    fzgx_vec3 segment_end,
    float plane_distance,
    fzgx_vec3 plane_normal,
    const fzgx_vec3 *vertices,
    const fzgx_vec3 *edge_normals,
    uint32_t vertex_count) {
  fzgx_vec3 polygon_edge;
  fzgx_vec3 local_axis_z;
  fzgx_vec3 local_axis_x;
  fzgx_vec3 local_segment_end;
  fzgx_vec3 local_vertices[4];
  const fzgx_vec3 local_segment_start = {0};
  float radius_sq = radius * radius;
  bool slab_hit = false;
  uint32_t i;
  float start_plane_distance;

  if ((vertex_count == 0u) || (vertex_count >= 5u)) {
    return false;
  }
  start_plane_distance = fzgx_vec3_dot(plane_normal, segment_start) + plane_distance;
  if ((start_plane_distance < -5.0f) || (10.0f < start_plane_distance)) {
    return false;
  }

  polygon_edge = fzgx_vec3_sub(vertices[1], vertices[0]);
  local_axis_z = fzgx_vec3_cross(&polygon_edge, &plane_normal);
  if (!fzgx_vec3_is_nonzero(local_axis_z)) {
    return false;
  }
  local_axis_z = fzgx_vec3_scale(local_axis_z, 1.0f / fzgx_vec3_length(local_axis_z));
  local_axis_x = fzgx_vec3_cross(&plane_normal, &local_axis_z);
  if (!fzgx_vec3_is_nonzero(local_axis_x)) {
    return false;
  }
  local_axis_x = fzgx_vec3_scale(local_axis_x, 1.0f / fzgx_vec3_length(local_axis_x));

  local_segment_end = fzgx_vec3_sub(segment_end, segment_start);
  local_segment_end = (fzgx_vec3){
      fzgx_vec3_dot(local_axis_x, local_segment_end),
      fzgx_vec3_dot(plane_normal, local_segment_end),
      fzgx_vec3_dot(local_axis_z, local_segment_end),
  };
  for (i = 0u; i < vertex_count; ++i) {
    fzgx_vec3 relative_vertex = fzgx_vec3_sub(vertices[i], segment_start);

    local_vertices[i] = (fzgx_vec3){
        fzgx_vec3_dot(local_axis_x, relative_vertex),
        fzgx_vec3_dot(plane_normal, relative_vertex),
        fzgx_vec3_dot(local_axis_z, relative_vertex),
    };
    if ((-10.0f <= local_vertices[i].y) && (local_vertices[i].y <= 5.0f)) {
      slab_hit = true;
    }
  }
  if (!slab_hit) {
    return false;
  }

  for (i = 0u; i < vertex_count; ++i) {
    fzgx_vec3 delta = fzgx_vec3_sub(segment_start, vertices[i]);

    if (fzgx_vec3_dot(delta, edge_normals[i]) < 0.0f) {
      break;
    }
  }
  if (i == vertex_count) {
    return true;
  }

  for (i = 0u; i < vertex_count; ++i) {
    uint32_t next_i = (i + 1u) % vertex_count;
    fzgx_vec3 closest_segment;
    fzgx_vec3 closest_edge;

    if (fzgx_output_closest_points_between_lines_and_return_square_dist_exact(
            &local_segment_start,
            &local_segment_end,
            &local_vertices[i],
            &local_vertices[next_i],
            &closest_segment,
            &closest_edge) < radius_sq) {
      return true;
    }
  }
  return false;
}

static bool fzgx_sweep_sphere_against_convex_polygon_exact(
    float radius,
    fzgx_vec3 *segment_start_inout,
    const fzgx_vec3 *segment_end,
    const fzgx_vec3 *segment_diff,
    float *hit_time_out,
    float plane_distance,
    const fzgx_vec3 *plane_normal,
    const fzgx_vec3 *vertices,
    const fzgx_vec3 *edge_normals,
    uint32_t vertex_count,
    uint32_t mask) {
  float diff_dot_normal;
  float start_plane_distance;
  float plane_hit_t;
  float radius_hit_t;
  float radius_sq;
  fzgx_vec3 projected_hit_point;
  uint32_t i;

  if ((segment_start_inout == 0) || (segment_end == 0) || (segment_diff == 0) || (hit_time_out == 0) ||
      (plane_normal == 0) || (vertices == 0) || (edge_normals == 0) || (vertex_count == 0u)) {
    return false;
  }

  diff_dot_normal = plane_normal->x * segment_diff->x + plane_normal->y * segment_diff->y +
                    plane_normal->z * segment_diff->z;
  if ((0.0f < diff_dot_normal) && ((mask & 0x400000u) == 0u)) {
    return false;
  }

  start_plane_distance =
      plane_distance + plane_normal->x * segment_start_inout->x + plane_normal->y * segment_start_inout->y +
      plane_normal->z * segment_start_inout->z;
  plane_hit_t = start_plane_distance / diff_dot_normal;
  if ((1.0f < plane_hit_t) || ((fzgx_float_bits_exact(plane_hit_t) & 0x7f800000u) == 0x7f800000u)) {
    return false;
  }

  radius_hit_t = (start_plane_distance - radius) / diff_dot_normal;
  if ((radius_hit_t < 0.0f) || ((fzgx_float_bits_exact(radius_hit_t) & 0x7f800000u) == 0x7f800000u)) {
    return false;
  }

  if (plane_hit_t < 0.0f) {
    plane_hit_t = 0.0f;
  }
  projected_hit_point.x = segment_start_inout->x + segment_diff->x * -plane_hit_t;
  projected_hit_point.y = segment_start_inout->y + segment_diff->y * -plane_hit_t;
  projected_hit_point.z = segment_start_inout->z + segment_diff->z * -plane_hit_t;
  radius_sq = radius * radius;

  for (i = 0u; i < vertex_count; ++i) {
    uint32_t next_i = (i + 1u) % vertex_count;
    fzgx_vec3 delta = fzgx_vec3_sub(projected_hit_point, vertices[i]);
    float edge_dot = fzgx_vec3_dot(delta, edge_normals[i]);

    if (edge_dot < 0.0f) {
      fzgx_vec3 closest_segment;
      fzgx_vec3 closest_edge;

      if (radius_sq <
          fzgx_output_closest_points_between_lines_and_return_square_dist_exact(
              segment_start_inout,
              segment_end,
              &vertices[i],
              &vertices[next_i],
              &closest_segment,
              &closest_edge)) {
        return false;
      }
    }
  }

  if (1.0f < radius_hit_t) {
    radius_hit_t = 1.0f;
  }
  segment_start_inout->x += segment_diff->x * -radius_hit_t;
  segment_start_inout->y += segment_diff->y * -radius_hit_t;
  segment_start_inout->z += segment_diff->z * -radius_hit_t;
  *hit_time_out = 1.0f - radius_hit_t;
  return true;
}

static float fzgx_angle16_to_degrees_exact(uint16_t angle16) {
  return ((float)(int16_t)angle16 / 32768.0f) * 180.0f;
}

static uint16_t fzgx_degrees_to_angle16_exact(float degrees) {
  return (uint16_t)(int32_t)(degrees * (32768.0f / 180.0f));
}

static fzgx_vec3 fzgx_vec3_normalize_or_exact(fzgx_vec3 vector, fzgx_vec3 fallback) {
  float length = fzgx_vec3_length(vector);

  if (length <= FLT_EPSILON) {
    return fallback;
  }
  return fzgx_vec3_scale(vector, 1.0f / length);
}

static fzgx_sincos_result fzgx_math_sincos_14b(uint16_t angle) {
  uint32_t index = (uint32_t)angle & 0x3fffu;
  uint8_t high_byte = (uint8_t)(angle >> 8);
  float sin_value;
  float cos_value;
  fzgx_sincos_result result;

  if (((high_byte >> 6) & 1u) != 0u) {
    index = 0x4000u - index;
  }
  sin_value = fzgx_sine_lut_14b[index];
  cos_value = fzgx_sine_lut_14b[0x4000u - index];
  if ((high_byte >> 7) != 0u) {
    sin_value = -sin_value;
  }
  if ((((high_byte >> 6) & 1u) ^ (high_byte >> 7)) != 0u) {
    cos_value = -cos_value;
  }
  result.sin_value = sin_value;
  result.cos_value = cos_value;
  return result;
}

static float fzgx_math_rsqrt_chunk_exact(float value, float half_value) {
  float estimate = 1.0f / sqrtf(value);
  float triple_half = half_value + half_value + half_value;

  estimate = estimate * -(estimate * estimate * half_value * value - triple_half);
  return estimate * -(estimate * estimate * half_value * value - triple_half);
}

static float fzgx_math_rsqrt_exact(float value) {
  if (!(value > 0.0f)) {
    return INFINITY;
  }
  return fzgx_math_rsqrt_chunk_exact(value, 0.5f);
}

static uint32_t fzgx_count_leading_zeros_exact(uint32_t value) {
  if (value == 0u) {
    return 32u;
  }
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_clz(value);
#else
  uint32_t count = 0u;
  while ((value & 0x80000000u) == 0u) {
    value <<= 1;
    count += 1u;
  }
  return count;
#endif
}

static void fzgx_mat43_rotate_about_x_right(fzgx_mat43 *transform, uint16_t angle) {
  fzgx_sincos_result sincos = fzgx_math_sincos_14b(angle);
  float basis_y_x = transform->basis_y_x;
  float basis_y_y = transform->basis_y_y;
  float basis_y_z = transform->basis_y_z;
  float basis_z_x = transform->basis_z_x;
  float basis_z_y = transform->basis_z_y;
  float basis_z_z = transform->basis_z_z;

  transform->basis_y_x = basis_y_x * sincos.cos_value + basis_z_x * sincos.sin_value;
  transform->basis_y_y = basis_y_y * sincos.cos_value + basis_z_y * sincos.sin_value;
  transform->basis_y_z = basis_y_z * sincos.cos_value + basis_z_z * sincos.sin_value;
  transform->basis_z_x = basis_y_x * -sincos.sin_value + basis_z_x * sincos.cos_value;
  transform->basis_z_y = basis_y_y * -sincos.sin_value + basis_z_y * sincos.cos_value;
  transform->basis_z_z = basis_y_z * -sincos.sin_value + basis_z_z * sincos.cos_value;
}

static void fzgx_mat43_rotate_about_y_right(fzgx_mat43 *transform, uint16_t angle) {
  fzgx_sincos_result sincos = fzgx_math_sincos_14b(angle);
  float basis_x_x = transform->basis_x_x;
  float basis_x_y = transform->basis_x_y;
  float basis_x_z = transform->basis_x_z;
  float basis_z_x = transform->basis_z_x;
  float basis_z_y = transform->basis_z_y;
  float basis_z_z = transform->basis_z_z;

  transform->basis_z_x = basis_x_x * sincos.sin_value + basis_z_x * sincos.cos_value;
  transform->basis_z_y = basis_x_y * sincos.sin_value + basis_z_y * sincos.cos_value;
  transform->basis_z_z = basis_x_z * sincos.sin_value + basis_z_z * sincos.cos_value;
  transform->basis_x_x = basis_x_x * sincos.cos_value + basis_z_x * -sincos.sin_value;
  transform->basis_x_y = basis_x_y * sincos.cos_value + basis_z_y * -sincos.sin_value;
  transform->basis_x_z = basis_x_z * sincos.cos_value + basis_z_z * -sincos.sin_value;
}

static void fzgx_mat43_rotate_about_z_right(fzgx_mat43 *transform, uint16_t angle) {
  fzgx_sincos_result sincos = fzgx_math_sincos_14b(angle);
  float basis_x_x = transform->basis_x_x;
  float basis_x_y = transform->basis_x_y;
  float basis_x_z = transform->basis_x_z;
  float basis_y_x = transform->basis_y_x;
  float basis_y_y = transform->basis_y_y;
  float basis_y_z = transform->basis_y_z;

  transform->basis_x_x = basis_x_x * sincos.cos_value + basis_y_x * sincos.sin_value;
  transform->basis_x_y = basis_x_y * sincos.cos_value + basis_y_y * sincos.sin_value;
  transform->basis_x_z = basis_x_z * sincos.cos_value + basis_y_z * sincos.sin_value;
  transform->basis_y_x = basis_x_x * -sincos.sin_value + basis_y_x * sincos.cos_value;
  transform->basis_y_y = basis_x_y * -sincos.sin_value + basis_y_y * sincos.cos_value;
  transform->basis_y_z = basis_x_z * -sincos.sin_value + basis_y_z * sincos.cos_value;
}

static void fzgx_make_axis_angle_quat_exact(
    fzgx_quat *out_quat,
    const fzgx_vec3 *axis,
    int16_t angle) {
  float axis_len_sq = axis->x * axis->x + axis->y * axis->y + axis->z * axis->z;

  if ((float)FLT_EPSILON <= axis_len_sq) {
    float sin_half = fzgx_math_sincos_14b((uint16_t)(angle >> 1)).sin_value;
    float axis_scale = fzgx_math_rsqrt_exact(axis_len_sq) * sin_half;

    out_quat->x = axis->x * axis_scale;
    out_quat->y = axis->y * axis_scale;
    out_quat->z = axis->z * axis_scale;
    out_quat->w = fzgx_math_sincos_14b((uint16_t)((angle >> 1) + 0x4000)).sin_value;
  } else {
    out_quat->x = 0.0f;
    out_quat->y = 0.0f;
    out_quat->z = 0.0f;
    out_quat->w = 1.0f;
  }
}

static void fzgx_mat43_from_quat_exact(fzgx_mat43 *transform, const fzgx_quat *quat) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;

  fVar1 = quat->x;
  fVar2 = quat->y;
  fVar7 = 1.0f;
  fVar3 = quat->z;
  fVar5 = fVar7 - fVar7;
  fVar4 = quat->w;
  fVar6 = fVar7 + fVar7;
  transform->origin_x = fVar5;
  transform->origin_y = fVar5;
  transform->origin_z = fVar5;
  transform->basis_y_y = -((fVar3 * fVar3 + fVar1 * fVar1) * fVar6 - fVar7);
  transform->basis_z_z = -((fVar1 * fVar1 + fVar2 * fVar2) * fVar6 - fVar7);
  transform->basis_x_x = -((fVar2 * fVar2 + fVar3 * fVar3) * fVar6 - fVar7);
  fVar6 = fVar2 + fVar2;
  fVar3 = fVar3 + fVar3;
  fVar5 = fVar4 * (fVar1 + fVar1);
  transform->basis_z_y = fVar2 * fVar3 - fVar5;
  transform->basis_y_z = fVar2 * fVar3 + fVar5;
  transform->basis_x_z = fVar1 * fVar3 - fVar4 * fVar6;
  transform->basis_z_x = fVar1 * fVar3 + fVar4 * fVar6;
  transform->basis_y_x = fVar1 * fVar6 - fVar4 * fVar3;
  transform->basis_x_y = fVar1 * fVar6 + fVar4 * fVar3;
}

static void fzgx_quat_to_mat43_exact(fzgx_mat43 *transform, const fzgx_quat *quat) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  float fVar10;
  float fVar11;
  float fVar12;

  fVar12 = 1.0f;
  fVar1 = quat->x;
  fVar3 = quat->y;
  fVar2 = quat->z;
  fVar4 = quat->w;
  fVar5 = fVar12 - fVar12;
  fVar6 = fVar12 + fVar12;
  fVar8 = fVar3 * fVar3;
  fVar9 = fVar2 * fVar2 + fVar1 * fVar1;
  fVar7 = fVar9 + fVar4 * fVar4 + fVar8;
  fVar10 = 1.0f / fVar7;
  fVar10 = fVar10 * -(fVar7 * fVar10 - fVar6) * fVar6;
  transform->origin_x = fVar5;
  transform->origin_z = fVar5;
  transform->basis_z_z = -((fVar1 * fVar1 + fVar8) * fVar10 - fVar12);
  fVar11 = fVar1 * fVar2 + fVar3 * fVar4;
  fVar7 = fVar3 * fVar2 + fVar1 * fVar4;
  transform->basis_x_y = (fVar1 * fVar3 + fVar2 * fVar4) * fVar10;
  transform->basis_y_y = -(fVar9 * fVar10 - fVar12);
  transform->basis_x_x = -((fVar2 * fVar2 + fVar8) * fVar10 - fVar12);
  transform->basis_y_x = (fVar1 * fVar3 - fVar2 * fVar4) * fVar10;
  transform->basis_z_x = fVar11 * fVar10;
  transform->basis_z_y = -(fVar1 * fVar4 * fVar6 - fVar7) * fVar10;
  transform->origin_y = fVar5;
  transform->basis_x_z = -(fVar3 * fVar4 * fVar6 - fVar11) * fVar10;
  transform->basis_y_z = fVar7 * fVar10;
}

static void fzgx_make_quat_between_vectors_exact(
    fzgx_quat *out_quat,
    const fzgx_vec3 *from,
    const fzgx_vec3 *to) {
  double dot =
      (double)(from->x * to->x + from->y * to->y + from->z * to->z);

  if (dot <= 0.9999899) {
    if (-0.9999899 <= dot) {
      fzgx_vec3 axis = {
          from->y * to->z - from->z * to->y,
          from->z * to->x - from->x * to->z,
          from->x * to->y - from->y * to->x,
      };
      float axis_scale = fzgx_math_rsqrt_exact(fzgx_vec3_dot(axis, axis));
      float sin_half = fzgx_math_sqrt_exact(0.5f * (1.0f - (float)dot));

      axis = fzgx_vec3_scale(axis, axis_scale);
      out_quat->x = axis.x * sin_half;
      out_quat->y = axis.y * sin_half;
      out_quat->z = axis.z * sin_half;
      out_quat->w = fzgx_math_sqrt_exact(0.5f * (1.0f + (float)dot));
    } else {
      fzgx_vec3 axis = {0.0f, from->x, -from->y};
      if (fzgx_math_sqrt_exact(from->x * from->x + from->y * from->y) < 0.000001f) {
        axis = (fzgx_vec3){-from->z, 0.0f, from->x};
      }
      axis = fzgx_vec3_scale(axis, fzgx_math_rsqrt_exact(fzgx_vec3_dot(axis, axis)));
      out_quat->x = axis.x;
      out_quat->y = axis.y;
      out_quat->z = axis.z;
      out_quat->w = 0.0f;
    }
  } else {
    out_quat->x = 0.0f;
    out_quat->y = 0.0f;
    out_quat->z = 0.0f;
    out_quat->w = 1.0f;
  }
}

static fzgx_mat43 fzgx_mat43_identity_exact(void) {
  fzgx_mat43 transform;
  memset(&transform, 0, sizeof(transform));
  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  return transform;
}

static void fzgx_mat43_normalize_basis_exact(fzgx_mat43 *transform) {
  float fVar1;
  double dVar3;
  double dVar4;
  double dVar5;
  double dVar6;
  double dVar7;
  double dVar8;
  double dVar9;
  double dVar10;
  double dVar11;
  double dVar12;
  double dVar13;
  double dVar14;
  double dVar15;
  double dVar16;
  float fVar17;

  fVar1 = 0.5f;
  fVar17 = transform->basis_x_x;
  dVar6 = (double)fVar17;
  fVar17 = transform->basis_y_x;
  dVar13 = (double)fVar17;
  fVar17 = transform->basis_z_x;
  dVar7 = (double)fVar17;
  fVar17 = transform->basis_x_y;
  dVar8 = (double)fVar17;
  fVar17 = transform->basis_y_y;
  dVar14 = (double)fVar17;
  fVar17 = transform->basis_z_y;
  dVar9 = (double)fVar17;
  fVar17 = transform->basis_x_z;
  dVar10 = (double)fVar17;
  fVar17 = transform->basis_y_z;
  dVar15 = (double)fVar17;
  fVar17 = transform->basis_z_z;
  dVar11 = (double)fVar17;
  dVar16 = dVar15 * dVar15 + dVar14 * dVar14 + dVar13 * dVar13;
  dVar12 = (double)(float)(dVar11 * dVar11 +
                          (double)(float)(dVar9 * dVar9 + (double)(float)(dVar7 * dVar7)));
  dVar4 = (double)fVar1;
  fVar17 = fzgx_math_rsqrt_chunk_exact((float)(dVar10 * dVar10 + dVar8 * dVar8 + dVar6 * dVar6), fVar1);
  dVar3 = (double)fVar17;
  dVar5 = (double)(float)dVar4;
  fzgx_math_rsqrt_chunk_exact((float)dVar16, (float)dVar4);
  transform->basis_x_x = (float)(dVar6 * dVar3);
  transform->basis_y_x = (float)(dVar13 * dVar3);
  transform->basis_x_y = (float)(dVar8 * dVar3);
  transform->basis_y_y = (float)(dVar14 * dVar3);
  transform->basis_x_z = (float)(dVar10 * dVar3);
  transform->basis_y_z = (float)(dVar15 * dVar3);
  fVar17 = fzgx_math_rsqrt_chunk_exact((float)dVar12, (float)dVar5);
  dVar3 = (double)fVar17;
  transform->basis_z_x = (float)(dVar7 * dVar3);
  transform->basis_z_y = (float)(dVar9 * dVar3);
  transform->basis_z_z = (float)(dVar11 * dVar3);
}

static void fzgx_mat43_to_quat_exact(const fzgx_mat43 *transform, fzgx_quat *quat) {
  float fVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  float fVar5;
  float fVar6;
  int local_48[3];
  float local_3c[15];
  const float *matrix = &transform->basis_x_x;

  fVar6 = matrix[10] + matrix[0] + matrix[5];
  local_48[0] = 1;
  local_48[1] = 2;
  local_48[2] = 0;
  if (fVar6 <= 0.0f) {
    iVar4 = 0;
    if (matrix[0] < matrix[5]) {
      iVar4 = 1;
    }
    if (matrix[iVar4 * 5] < matrix[10]) {
      iVar4 = 2;
    }
    iVar2 = local_48[iVar4];
    iVar3 = local_48[iVar2];
    fVar6 = fzgx_math_sqrt_exact(1.0f + (matrix[iVar4 * 5] - (matrix[iVar2 * 5] + matrix[iVar3 * 5])));
    local_3c[iVar4] = 0.5f * fVar6;
    if (fVar6 != 0.0f) {
      fVar6 = 0.5f / fVar6;
    }
    fVar5 = matrix[iVar4 + iVar3 * 4];
    fVar1 = matrix[iVar3 + iVar4 * 4];
    local_3c[3] = fVar6 * (matrix[iVar2 + iVar3 * 4] - matrix[iVar3 + iVar2 * 4]);
    local_3c[iVar2] = fVar6 * (matrix[iVar4 + iVar2 * 4] + matrix[iVar2 + iVar4 * 4]);
    local_3c[iVar3] = fVar6 * (fVar5 + fVar1);
    quat->x = local_3c[0];
    quat->y = local_3c[1];
    quat->z = local_3c[2];
    quat->w = local_3c[3];
  } else {
    fVar5 = fzgx_math_sqrt_exact(1.0f + fVar6);
    fVar6 = 0.5f / fVar5;
    quat->w = 0.5f * fVar5;
    quat->x = fVar6 * (matrix[9] - matrix[6]);
    quat->y = fVar6 * (matrix[2] - matrix[8]);
    quat->z = fVar6 * (matrix[4] - matrix[1]);
  }
}

static void fzgx_quat_slerp_exact(
    float fraction,
    fzgx_quat *out_quat,
    const fzgx_quat *from,
    const fzgx_quat *to) {
  double d_fraction = (double)fraction;
  double to_x = (double)to->x;
  double to_y = (double)to->y;
  double to_z = (double)to->z;
  double to_w = (double)to->w;
  double dot = (double)((float)((double)from->w * to_w) +
                        (float)((double)from->z * to_z) +
                        (float)((double)from->x * to_x) +
                        (float)((double)from->y * to_y));
  double from_scale;

  if (dot < 0.0) {
    dot = -dot;
    to_x = -to_x;
    to_y = -to_y;
    to_z = -to_z;
    to_w = -to_w;
  }
  if ((double)(float)(1.0f - dot) <= 0.000001) {
    from_scale = (double)(float)(1.0f - d_fraction);
  } else {
    double angle = acos(dot);
    double angle_f = (double)(float)angle;
    double sin_angle = sin(angle_f);
    double sin_angle_f = (double)(float)sin_angle;

    from_scale = sin((double)(float)((double)(float)(1.0f - d_fraction) * angle_f));
    from_scale = (double)(float)(from_scale / sin_angle_f);
    d_fraction = sin((double)(float)(d_fraction * angle_f));
    d_fraction = (double)(float)(d_fraction / sin_angle_f);
  }
  out_quat->x = (float)(from_scale * (double)from->x) + (float)(d_fraction * to_x);
  out_quat->y = (float)(from_scale * (double)from->y) + (float)(d_fraction * to_y);
  out_quat->z = (float)(from_scale * (double)from->z) + (float)(d_fraction * to_z);
  out_quat->w = (float)(from_scale * (double)from->w) + (float)(d_fraction * to_w);
}

static void fzgx_ray_scale_exact(
    float scale,
    const fzgx_vec3 *start,
    const fzgx_vec3 *end,
    fzgx_vec3 *out) {
  if ((start == 0) || (end == 0) || (out == 0)) {
    return;
  }
  out->x = start->x + scale * (end->x - start->x);
  out->y = start->y + scale * (end->y - start->y);
  out->z = start->z + scale * (end->z - start->z);
}

static void fzgx_mtx_slerp_exact(
    float fraction,
    const fzgx_mat43 *from,
    const fzgx_mat43 *to,
    fzgx_mat43 *out) {
  fzgx_quat from_quat;
  fzgx_quat to_quat;
  fzgx_quat out_quat;

  if ((from == 0) || (to == 0) || (out == 0)) {
    return;
  }
  fzgx_mat43_to_quat_exact(from, &from_quat);
  fzgx_mat43_to_quat_exact(to, &to_quat);
  fzgx_quat_slerp_exact(fraction, &out_quat, &from_quat, &to_quat);
  fzgx_quat_to_mat43_exact(out, &out_quat);
}

static void fzgx_mat43_translate_local_exact(fzgx_mat43 *transform, fzgx_vec3 local_offset) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  float fVar10;
  float fVar11;
  float fVar12;
  float fVar13;
  float fVar14;
  float fVar15;

  fVar1 = local_offset.x;
  fVar3 = local_offset.y;
  fVar2 = local_offset.z;
  fVar4 = transform->basis_x_x;
  fVar5 = transform->basis_y_x;
  fVar8 = transform->basis_x_y;
  fVar9 = transform->basis_y_y;
  fVar12 = transform->basis_x_z;
  fVar13 = transform->basis_y_z;
  fVar6 = transform->basis_z_x;
  fVar7 = transform->origin_x;
  fVar10 = transform->basis_z_y;
  fVar11 = transform->origin_y;
  fVar14 = transform->basis_z_z;
  fVar15 = transform->origin_z;
  transform->origin_x = fVar6 * fVar2 + fVar4 * fVar1 + fVar7 * 1.0f + fVar5 * fVar3;
  transform->origin_y = fVar10 * fVar2 + fVar8 * fVar1 + fVar11 * 1.0f + fVar9 * fVar3;
  transform->origin_z = fVar14 * fVar2 + fVar12 * fVar1 + fVar15 * 1.0f + fVar13 * fVar3;
}

static void fzgx_mtxa_multiply_mtx_exact(fzgx_mat43 *mtxa, const fzgx_mat43 *in_mtx) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  float fVar10;
  float fVar11;
  float fVar12;
  float fVar13;
  float fVar14;
  float fVar15;
  float fVar16;
  float fVar17;
  float fVar18;
  float fVar19;
  float fVar20;
  float fVar21;
  float fVar22;
  float fVar23;
  float fVar24;
  float fVar25;
  float fVar26;

  fVar1 = in_mtx->basis_x_x;
  fVar7 = in_mtx->basis_y_x;
  fVar13 = mtxa->basis_x_x;
  fVar14 = mtxa->basis_y_x;
  fVar2 = in_mtx->basis_z_x;
  fVar8 = in_mtx->origin_x;
  fVar17 = mtxa->basis_x_y;
  fVar18 = mtxa->basis_y_y;
  fVar3 = in_mtx->basis_x_y;
  fVar9 = in_mtx->basis_y_y;
  fVar21 = mtxa->basis_x_z;
  fVar22 = mtxa->basis_y_z;
  fVar4 = in_mtx->basis_z_y;
  fVar10 = in_mtx->origin_y;
  fVar5 = in_mtx->basis_x_z;
  fVar11 = in_mtx->basis_y_z;
  fVar6 = in_mtx->basis_z_z;
  fVar12 = in_mtx->origin_z;
  fVar15 = mtxa->basis_z_x;
  fVar16 = mtxa->origin_x;
  fVar19 = mtxa->basis_z_y;
  fVar20 = mtxa->origin_y;
  fVar23 = mtxa->basis_z_z;
  fVar24 = mtxa->origin_z;
  fVar25 = 0.0f;
  fVar26 = 1.0f;
  mtxa->basis_x_x = fVar5 * fVar15 + fVar3 * fVar14 + fVar1 * fVar13;
  mtxa->basis_y_x = fVar11 * fVar15 + fVar9 * fVar14 + fVar7 * fVar13;
  mtxa->basis_x_y = fVar5 * fVar19 + fVar3 * fVar18 + fVar1 * fVar17;
  mtxa->basis_y_y = fVar11 * fVar19 + fVar9 * fVar18 + fVar7 * fVar17;
  mtxa->basis_x_z = fVar5 * fVar23 + fVar3 * fVar22 + fVar1 * fVar21;
  mtxa->basis_y_z = fVar11 * fVar23 + fVar9 * fVar22 + fVar7 * fVar21;
  mtxa->basis_z_x = fVar25 * fVar15 + fVar6 * fVar15 + fVar4 * fVar14 + fVar2 * fVar13;
  mtxa->origin_x = fVar26 * fVar16 + fVar12 * fVar15 + fVar10 * fVar14 + fVar8 * fVar13;
  mtxa->basis_z_y = fVar25 * fVar19 + fVar6 * fVar19 + fVar4 * fVar18 + fVar2 * fVar17;
  mtxa->origin_y = fVar26 * fVar20 + fVar12 * fVar19 + fVar10 * fVar18 + fVar8 * fVar17;
  mtxa->basis_z_z = fVar25 * fVar23 + fVar6 * fVar23 + fVar4 * fVar22 + fVar2 * fVar21;
  mtxa->origin_z = fVar26 * fVar24 + fVar12 * fVar23 + fVar10 * fVar22 + fVar8 * fVar21;
}

static void fzgx_mat43_rigid_invert_exact(fzgx_mat43 *transform) {
  float origin_x;
  float origin_y;
  float origin_z;
  float basis_x_x;
  float basis_y_x;
  float basis_z_x;
  float basis_x_y;
  float basis_y_y;
  float basis_z_y;
  float basis_x_z;
  float basis_y_z;
  float basis_z_z;

  origin_x = transform->origin_x;
  basis_x_x = transform->basis_x_x;
  basis_y_x = transform->basis_y_x;
  basis_z_x = transform->basis_z_x;
  origin_y = transform->origin_y;
  basis_x_y = transform->basis_x_y;
  basis_y_y = transform->basis_y_y;
  basis_z_y = transform->basis_z_y;
  origin_z = transform->origin_z;
  basis_x_z = transform->basis_x_z;
  basis_y_z = transform->basis_y_z;
  basis_z_z = transform->basis_z_z;
  transform->basis_x_y = basis_y_x;
  transform->basis_x_z = basis_z_x;
  transform->basis_y_x = basis_x_y;
  transform->basis_y_z = basis_z_y;
  transform->basis_z_x = basis_x_z;
  transform->basis_z_y = basis_y_z;
  transform->origin_x = -(origin_z * basis_x_z + origin_y * basis_x_y + origin_x * basis_x_x);
  transform->origin_y = -(origin_z * basis_y_z + origin_y * basis_y_y + origin_x * basis_y_x);
  transform->origin_z = -(origin_z * basis_z_z + origin_y * basis_z_y + origin_x * basis_z_x);
}

static fzgx_vec3 fzgx_world_vector_to_local(
    const fzgx_mat43 *track_transform,
    fzgx_vec3 world_vector) {
  fzgx_vec3 local;
  local.x = track_transform->basis_x_y * world_vector.y +
            track_transform->basis_x_x * world_vector.x;
  local.y = track_transform->basis_y_y * world_vector.y +
            track_transform->basis_y_x * world_vector.x;
  local.z = track_transform->basis_z_y * world_vector.y +
            track_transform->basis_z_x * world_vector.x;
  local.x = track_transform->basis_x_z * world_vector.z + local.x;
  local.y = track_transform->basis_y_z * world_vector.z + local.y;
  local.z = track_transform->basis_z_z * world_vector.z + local.z;
  return local;
}

static fzgx_vec3 fzgx_world_point_to_local(
    const fzgx_mat43 *track_transform,
    fzgx_vec3 world_point) {
  world_point.x -= track_transform->origin_x;
  world_point.y -= track_transform->origin_y;
  world_point.z -= track_transform->origin_z;
  return fzgx_world_vector_to_local(track_transform, world_point);
}

static fzgx_vec3 fzgx_world_point_to_local_scaled_orthogonal_exact(
    const fzgx_mat43 *transform,
    fzgx_vec3 world_point) {
  fzgx_vec3 delta;
  float basis_x_length_squared;
  float basis_y_length_squared;
  float basis_z_length_squared;
  fzgx_vec3 local = {0.0f, 0.0f, 0.0f};

  if (transform == 0) {
    return local;
  }
  delta = fzgx_vec3_sub(world_point, fzgx_mat43_get_origin_exact(transform));
  basis_x_length_squared = fzgx_vec3_dot(
      fzgx_mat43_get_basis_x_exact(transform), fzgx_mat43_get_basis_x_exact(transform));
  basis_y_length_squared = fzgx_vec3_dot(
      fzgx_mat43_get_basis_y_exact(transform), fzgx_mat43_get_basis_y_exact(transform));
  basis_z_length_squared = fzgx_vec3_dot(
      fzgx_mat43_get_basis_z_exact(transform), fzgx_mat43_get_basis_z_exact(transform));
  if (basis_x_length_squared > FLT_EPSILON) {
    local.x = fzgx_vec3_dot(delta, fzgx_mat43_get_basis_x_exact(transform)) / basis_x_length_squared;
  }
  if (basis_y_length_squared > FLT_EPSILON) {
    local.y = fzgx_vec3_dot(delta, fzgx_mat43_get_basis_y_exact(transform)) / basis_y_length_squared;
  }
  if (basis_z_length_squared > FLT_EPSILON) {
    local.z = fzgx_vec3_dot(delta, fzgx_mat43_get_basis_z_exact(transform)) / basis_z_length_squared;
  }
  return local;
}

static fzgx_vec3 fzgx_world_point_to_track_frame_local_exact(
    const fzgx_track_frame_record *frame,
    fzgx_vec3 world_point) {
  if (frame == 0) {
    return (fzgx_vec3){0.0f, 0.0f, 0.0f};
  }
  return fzgx_world_point_to_local(&frame->track_current_transform, world_point);
}

static fzgx_vec3 fzgx_transform_local_vector(
    const fzgx_mat43 *transform,
    fzgx_vec3 local_vector) {
  fzgx_vec3 world;
  world.x = transform->basis_z_x * local_vector.z +
            transform->basis_x_x * local_vector.x +
            transform->basis_y_x * local_vector.y;
  world.y = transform->basis_z_y * local_vector.z +
            transform->basis_x_y * local_vector.x +
            transform->basis_y_y * local_vector.y;
  world.z = transform->basis_z_z * local_vector.z +
            transform->basis_x_z * local_vector.x +
            transform->basis_y_z * local_vector.y;
  return world;
}

static fzgx_vec3 fzgx_transform_local_point(
    const fzgx_mat43 *transform,
    fzgx_vec3 local_point) {
  fzgx_vec3 world;
  world.x = transform->basis_z_x * local_point.z +
            transform->basis_x_x * local_point.x +
            transform->origin_x * 1.0f +
            transform->basis_y_x * local_point.y;
  world.y = transform->basis_z_y * local_point.z +
            transform->basis_x_y * local_point.x +
            transform->origin_y * 1.0f +
            transform->basis_y_y * local_point.y;
  world.z = transform->basis_z_z * local_point.z +
            transform->basis_x_z * local_point.x +
            transform->origin_z * 1.0f +
            transform->basis_y_z * local_point.y;
  return world;
}

static fzgx_vec3 fzgx_vec3_lerp_exact(fzgx_vec3 a, fzgx_vec3 b, float t) {
  fzgx_vec3 out;
  out.x = a.x + (b.x - a.x) * t;
  out.y = a.y + (b.y - a.y) * t;
  out.z = a.z + (b.z - a.z) * t;
  return out;
}

static float fzgx_vec3_distance_squared_exact(fzgx_vec3 a, fzgx_vec3 b) {
  fzgx_vec3 delta = fzgx_vec3_sub(a, b);
  return fzgx_vec3_dot(delta, delta);
}

static uint16_t fzgx_math_atan2_angle16(float y, float x) {
  float angle = atan2f(y, x) * FZGX_ANGLE16_PER_RAD;
  int wrapped = (int)lroundf(angle) & 0xffff;
  return (uint16_t)wrapped;
}

static int16_t fzgx_fixed_acos_angle16_from_dot(float dot) {
  float clamped = dot;
  if (clamped < -1.0f) {
    clamped = -1.0f;
  } else if (clamped > 1.0f) {
    clamped = 1.0f;
  }
  return (int16_t)lroundf(acosf(clamped) * FZGX_ANGLE16_PER_RAD);
}

static int16_t fzgx_fixed_arcsin_angle16_from_dot(float dot) {
  float clamped = dot;
  if (clamped < -1.0f) {
    clamped = -1.0f;
  } else if (clamped > 1.0f) {
    clamped = 1.0f;
  }
  return (int16_t)lroundf(asinf(clamped) * FZGX_ANGLE16_PER_RAD);
}

static const fzgx_track_manifest *fzgx_get_active_track_manifest(
    const fzgx_sim_world *world) {
  if ((world == 0) || (world->content == 0) ||
      (world->active_track_index >= world->content->track_count)) {
    return 0;
  }
  return &world->content->tracks[world->active_track_index];
}

static fzgx_status fzgx_get_active_track_course(
    const fzgx_sim_world *world,
    const fzgx_track_course_content **course_out) {
  if (course_out == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *course_out = 0;
  if ((world == 0) || (world->content == 0)) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  return fzgx_content_bundle_get_track_course_for_track_index(
      world->content, world->active_track_index, course_out);
}

static bool fzgx_machine_sort_key_is_completed(const fzgx_machine_snapshot *machine) {
  return ((machine->machine_state & FZGX_MS_COMPLETEDRACE_1_Q) != 0u) ||
         ((machine->machine_flags & FZGX_MACHINE_FLAG_COMPLETED_RACE) != 0u);
}

static bool fzgx_machine_sort_key_is_retired(const fzgx_machine_snapshot *machine) {
  return ((machine->machine_state & FZGX_MS_RETIRED) != 0u) ||
         ((machine->machine_flags & FZGX_MACHINE_FLAG_RETIRED) != 0u);
}

static uint32_t fzgx_machine_rank_bucket_exact(
    const fzgx_machine_snapshot *machine,
    bool retired_visible) {
  uint32_t state = machine->machine_state;

  if ((state & FZGX_MS_B30) != 0u) {
    return 3u;
  }
  if (fzgx_machine_sort_key_is_completed(machine)) {
    return 0u;
  }
  if (!retired_visible && fzgx_machine_sort_key_is_retired(machine)) {
    return 2u;
  }
  return 1u;
}

static bool fzgx_should_machine_rank_before_exact(
    const fzgx_machine_snapshot *lhs,
    uint32_t lhs_index,
    const fzgx_machine_snapshot *rhs,
    uint32_t rhs_index,
    bool retired_visible) {
  uint32_t lhs_bucket = fzgx_machine_rank_bucket_exact(lhs, retired_visible);
  uint32_t rhs_bucket = fzgx_machine_rank_bucket_exact(rhs, retired_visible);

  if (lhs_bucket != rhs_bucket) {
    return lhs_bucket < rhs_bucket;
  }
  if (lhs_bucket == 0u) {
    if (lhs->track_state.total_time_frames != rhs->track_state.total_time_frames) {
      return lhs->track_state.total_time_frames < rhs->track_state.total_time_frames;
    }
    if (lhs->track_state.total_time_fraction != rhs->track_state.total_time_fraction) {
      return lhs->track_state.total_time_fraction < rhs->track_state.total_time_fraction;
    }
    return lhs_index < rhs_index;
  }
  if (lhs->track_state.last_seg_dist != rhs->track_state.last_seg_dist) {
    return lhs->track_state.last_seg_dist > rhs->track_state.last_seg_dist;
  }
  return lhs_index < rhs_index;
}

static void fzgx_update_machine_ranks_exact(
    fzgx_sim_world *world,
    bool retired_visible) {
  uint32_t order[FZGX_SIM_MAX_MACHINES];
  uint32_t i;
  uint32_t j;

  for (i = 0u; i < world->machine_count; ++i) {
    order[i] = i;
  }
  for (i = 1u; i < world->machine_count; ++i) {
    uint32_t candidate = order[i];
    j = i;
    while (j > 0u) {
      uint32_t prior = order[j - 1u];
      if (!fzgx_should_machine_rank_before_exact(
              &world->machines[candidate],
              candidate,
              &world->machines[prior],
              prior,
              retired_visible)) {
        break;
      }
      order[j] = prior;
      --j;
    }
    order[j] = candidate;
  }
  for (i = 0u; i < world->machine_count; ++i) {
    world->machines[order[i]].track_state.rank_this_frame = (uint8_t)i;
  }
}

typedef struct fzgx_frame_phase_race_context {
  uint32_t raw_frame_flags;
  uint8_t finish_score_lap_threshold;
  bool raw_frame_state_live;
  bool temporary_full_heal_override;
  bool retired_visible_for_ranking;
} fzgx_frame_phase_race_context;

static fzgx_frame_phase_race_context fzgx_build_frame_phase_race_context_exact(
    const fzgx_race_step_options *options) {
  fzgx_frame_phase_race_context context;

  memset(&context, 0, sizeof(context));
  if ((options == 0) || !options->has_raw_frame_state) {
    return context;
  }
  context.raw_frame_flags = options->raw_frame_flags;
  context.finish_score_lap_threshold = options->finish_score_lap_threshold;
  if ((context.raw_frame_flags & 0x20u) != 0u) {
    return context;
  }
  context.raw_frame_state_live = true;
  if (((context.raw_frame_flags & 0x4000u) == 0u) ||
      ((context.raw_frame_flags & 0x200000u) != 0u)) {
    context.temporary_full_heal_override = true;
  }
  context.retired_visible_for_ranking = (context.raw_frame_flags & 0x8000u) != 0u;
  return context;
}

static void fzgx_recompute_machine_track_progress_metrics(
    fzgx_machine_snapshot *machine,
    const fzgx_track_manifest *track_manifest) {
  fzgx_machine_track_state *track = &machine->track_state;
  if (track_manifest == 0) {
    return;
  }
  if (track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_CLOSED) {
    track->last_seg_dist =
        track->lap_progress_fraction + track->last_frac_diff * (float)track->lap_cross_cp;
  } else {
    track->last_seg_dist = track->lap_progress_fraction;
  }
}

static void fzgx_normalize_race_time_exact(int32_t *frames, float *fraction) {
  if ((frames == 0) || (fraction == 0)) {
    return;
  }
  while (*fraction >= 1.0f) {
    *fraction -= 1.0f;
    *frames += 1;
  }
  while ((*fraction < 0.0f) && (*frames > 0)) {
    *fraction += 1.0f;
    *frames -= 1;
  }
}

static void fzgx_add_race_time_exact(
    int32_t *dst_frames,
    float *dst_fraction,
    int32_t add_frames,
    float add_fraction) {
  if ((dst_frames == 0) || (dst_fraction == 0)) {
    return;
  }
  *dst_frames += add_frames;
  *dst_fraction += add_fraction;
  fzgx_normalize_race_time_exact(dst_frames, dst_fraction);
}

static void fzgx_clear_race_time_exact(int32_t *frames, float *fraction) {
  if (frames != 0) {
    *frames = 0;
  }
  if (fraction != 0) {
    *fraction = 0.0f;
  }
}

static void fzgx_compute_race_time_fields_exact(
    int32_t frames,
    float fraction,
    uint8_t *minutes_out,
    uint8_t *seconds_out,
    uint16_t *millis_out) {
  uint32_t display_minutes = 99u;
  uint32_t display_seconds = 59u;
  uint32_t display_millis = 999u;

  if ((minutes_out == 0) || (seconds_out == 0) || (millis_out == 0)) {
    return;
  }

  if (frames < 360000) {
    if (1.0f <= fraction) {
      fraction -= 1.0f;
      frames += 1;
    }
    if ((frames / 3600) < 100) {
      uint32_t minutes = (uint32_t)(frames / 3600);
      uint32_t seconds = (uint32_t)((frames % 3600) / 60);
      uint32_t remainder = (uint32_t)((frames % 3600) % 60);
      uint32_t millis = (uint32_t)(1000.0f * (((float)remainder + fraction) / 60.0f));

      if (999u < millis) {
        millis = 999u;
      }
      display_minutes = minutes;
      display_seconds = seconds;
      display_millis = millis;
    }
  }

  *minutes_out = (uint8_t)display_minutes;
  *seconds_out = (uint8_t)display_seconds;
  *millis_out = (uint16_t)display_millis;
}

static uint32_t fzgx_pack_race_time_fields_exact(
    uint8_t minutes,
    uint8_t seconds,
    uint16_t millis) {
  return ((uint32_t)minutes << 20) | ((uint32_t)seconds << 12) | (uint32_t)millis;
}

static uint32_t fzgx_pack_race_time_display_exact(int32_t frames, float fraction) {
  uint8_t minutes;
  uint8_t seconds;
  uint16_t millis;

  fzgx_compute_race_time_fields_exact(frames, fraction, &minutes, &seconds, &millis);
  return fzgx_pack_race_time_fields_exact(minutes, seconds, millis);
}

static void fzgx_copy_race_time_triplet_exact(
    fzgx_race_time_triplet *dst,
    const fzgx_race_time_triplet *src) {
  if ((dst == 0) || (src == 0)) {
    return;
  }
  *dst = *src;
}

static void fzgx_clear_race_time_triplet_exact(fzgx_race_time_triplet *triplet) {
  if (triplet == 0) {
    return;
  }
  triplet->frames = 0;
  triplet->fraction = 0.0f;
  triplet->display = 0u;
}

static void fzgx_update_race_time_triplet_display_exact(
    fzgx_race_time_triplet *triplet) {
  if (triplet == 0) {
    return;
  }
  triplet->display = fzgx_pack_race_time_display_exact(triplet->frames, triplet->fraction);
}

static void fzgx_update_machine_race_time_displays_exact(fzgx_machine_track_state *track) {
  if (track == 0) {
    return;
  }
  track->lap_time_display =
      fzgx_pack_race_time_fields_exact(track->lap_min, track->lap_sec, track->lap_centi);
  track->total_time_display =
      fzgx_pack_race_time_fields_exact(track->total_min, track->total_sec, track->total_centi);
  track->history_time_display = fzgx_pack_race_time_fields_exact(
      track->history_min, track->history_sec, track->history_centi);
}

static void fzgx_destroy_machine_instantly_exact(fzgx_machine_snapshot *machine) {
  if (machine == 0) {
    return;
  }
  if ((machine->state_2 & 0x10u) != 0u) {
    return;
  }
  machine->machine_state |=
      FZGX_MS_FALLOUT | FZGX_MS_0HP | FZGX_MS_TOOKDAMAGE | FZGX_MS_DIEDTHISFRAMEOOB_Q;
  machine->state_2 |= 0x10u;
  machine->energy = 0.0f;
  machine->base_speed = 0.0f;
  machine->velocity = (fzgx_vec3){0};
  machine->angular_velocity = (fzgx_vec3){0};
  machine->speed_kmh = 0.0f;
  machine->state_2 &= ~0x80u;
  machine->entrant_runtime_flags &=
      ~FZGX_ENTRANT_RUNTIME_FLAG_COLLISION_DESTROY;
  machine->entrant_runtime_flags |= FZGX_ENTRANT_RUNTIME_FLAG_DESTROYED;
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static bool fzgx_damage_machine_exact(float damage_scale, fzgx_machine_snapshot *machine) {
  float damage;
  float max_damage;

  if (machine == 0) {
    return false;
  }
  if ((machine->frames_until_restored != 0u) || (machine->breakdown_frame_counter != 0u)) {
    return false;
  }
  damage = damage_scale * machine->stat_body;
  if (((machine->machine_state & FZGX_MS_B10) == 0u) && (20.0f < damage)) {
    damage = 20.0f;
  }
  max_damage = 1.01f * machine->max_energy;
  if (max_damage < damage) {
    damage = max_damage;
  }
  machine->damage_from_last_hit = damage;
  machine->energy -= machine->damage_from_last_hit;
  if (machine->energy < 0.0f) {
    bool seeded_breakdown = false;

    if ((machine->machine_state & (FZGX_MS_COMPLETEDRACE_1_Q | FZGX_MS_0HP)) == 0u) {
      machine->breakdown_frame_counter = 60u;
      seeded_breakdown = true;
    }
    machine->machine_state |= FZGX_MS_0HP;
    machine->energy = 0.0f;
    machine->base_speed = 0.0f;
    return seeded_breakdown;
  }
  return false;
}

static bool fzgx_handle_machine_crash_exact(
    fzgx_machine_snapshot *machine,
    bool allow_zero_hp_restore) {
  uint32_t state;
  bool should_seed_restore = false;

  if (machine == 0) {
    return false;
  }
  state = machine->machine_state;
  if ((state & FZGX_MS_FALLOUT) == 0u) {
    if ((state & FZGX_MS_0HP) != 0u) {
      if (!machine->machine_crashed && ((state & FZGX_MS_COMPLETEDRACE_1_Q) == 0u)) {
        machine->machine_crashed = true;
      }
    }
    if ((machine->machine_state & FZGX_MS_B1) != 0u) {
      if (((state & FZGX_MS_0HP) != 0u) &&
          (((machine->machine_state & FZGX_MS_CRASH_RESTORE_TRIGGER_EXACT) != 0u) ||
           allow_zero_hp_restore)) {
        should_seed_restore = true;
      }
    }
  } else {
    if ((state & FZGX_MS_B1) == 0u) {
      if ((state & FZGX_MS_COMPLETEDRACE_1_Q) != 0u) {
        should_seed_restore = true;
      }
    } else {
      should_seed_restore = true;
    }
    if (((state & FZGX_MS_COMPLETEDRACE_1_Q) == 0u) && !machine->machine_crashed) {
      machine->machine_crashed = true;
    }
  }
  state = machine->machine_state;
  if (((state & FZGX_MS_DIEDTHISFRAMEOOB_Q) != 0u) && ((state & FZGX_MS_B1) != 0u)) {
    should_seed_restore = true;
  }
  if ((state & FZGX_MS_B29) != 0u) {
    should_seed_restore = false;
  }
  return should_seed_restore;
}

static void fzgx_breakdown_fling_physics_exact(fzgx_machine_snapshot *machine) {
  uint32_t position_hash;
  uint32_t rand_x_bits;
  uint32_t rand_y_bits;
  float local_x;
  float local_z;
  double launch_scale;
  fzgx_vec3 launch_world;
  fzgx_vec3 launch_local;
  fzgx_vec3 surface_impulse;

  if (machine == 0) {
    return;
  }
  position_hash = fzgx_float_bits_exact(machine->position.z) ^
                  fzgx_float_bits_exact(machine->position.x) ^
                  fzgx_float_bits_exact(machine->position.y);
  rand_x_bits = (position_hash ^ fzgx_float_bits_exact(machine->angular_velocity.x)) & 0xffffu;
  rand_y_bits = (position_hash ^ fzgx_float_bits_exact(machine->angular_velocity.y)) & 0xffffu;
  local_x = 2.0f * ((float)rand_x_bits / 65536.0f) - 1.0f;
  local_z = 2.0f * ((float)rand_y_bits / 65536.0f) - 1.0f;
  if (local_x <= 0.0f) {
    local_x -= 0.5f;
  } else {
    local_x += 0.5f;
  }
  if (local_z <= 0.0f) {
    local_z -= 0.5f;
  } else {
    local_z += 0.5f;
  }
  launch_scale = (double)(0.0015f * machine->speed_kmh - 1.0f);
  if (launch_scale < 0.0) {
    launch_scale = 0.0;
  } else if (1.0 < launch_scale) {
    launch_scale = 1.0;
  }
  launch_scale *= 450.0 / 216.0;
  launch_world = fzgx_transform_local_vector(
      &machine->basis_physical, (fzgx_vec3){-local_x, 0.5f, -local_z});
  launch_world = fzgx_vec3_scale(launch_world, (float)(launch_scale * machine->stat_weight));
  machine->state_2 |= 2u;
  launch_local = fzgx_world_vector_to_local(&machine->basis_physical, launch_world);
  launch_local = fzgx_vec3_scale(launch_local, 0.5f);
  machine->broken_down_angle_fac2.x =
      machine->angular_velocity.x - local_z * launch_local.y;
  machine->broken_down_angle_fac2.y =
      machine->angular_velocity.y + local_z * launch_local.x - local_x * launch_local.z;
  machine->broken_down_angle_fac2.z =
      machine->angular_velocity.z + local_x * launch_local.y;
  fzgx_vec3_set_length_exact(
      (float)(0.2 * (launch_scale * machine->stat_weight)),
      &machine->surface_normal,
      &surface_impulse);
  machine->velocity = fzgx_vec3_add(machine->velocity, surface_impulse);
  if (30u < machine->frames_since_death) {
    fzgx_vec3 visual_up = fzgx_mat43_get_basis_y_exact(&machine->transform_visual);
    float surface_dot = fzgx_vec3_normalized_dot(visual_up, machine->surface_normal);

    machine->state_2 |= 0x20u;
    if ((double)surface_dot < 0.99) {
      fzgx_vec3 axis = {
          -((visual_up.z * machine->surface_normal.y) - (visual_up.y * machine->surface_normal.z)),
          -((visual_up.x * machine->surface_normal.z) - (visual_up.z * machine->surface_normal.x)),
          -((visual_up.y * machine->surface_normal.x) - (visual_up.x * machine->surface_normal.y)),
      };
      float axis_len = fzgx_vec3_length(axis);

      if (0.1f < axis_len) {
        float normalized_dot = 0.0f;
        int angle16;
        fzgx_quat rotation_quat;
        fzgx_mat43 rotation;

        if (0.0 <= (double)surface_dot) {
          normalized_dot = surface_dot * surface_dot;
        }
        angle16 = (int)(1365.0f * (1.0f - normalized_dot));
        fzgx_make_axis_angle_quat_exact(&rotation_quat, &axis, (int16_t)angle16);
        fzgx_mat43_from_quat_exact(&rotation, &rotation_quat);
        fzgx_mtxa_multiply_mtx_exact(&machine->transform_visual, &rotation);
      }
    }
  }
  if (machine->frames_since_death < 2u) {
    machine->frames_since_death = 2u;
  } else {
    uint16_t frames_since_death = (uint16_t)machine->frames_since_death + 1u;
    if (0x00efu < frames_since_death) {
      frames_since_death = 0x00f0u;
    }
    machine->frames_since_death = frames_since_death;
  }
}

static void fzgx_breakdown_physics_exact(fzgx_machine_snapshot *machine) {
  if (machine == 0) {
    return;
  }
  if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
    fzgx_breakdown_fling_physics_exact(machine);
  }
  if (machine->frames_since_death < 0x003cu) {
    machine->broken_down_angle_fac1 =
        fzgx_vec3_add(machine->broken_down_angle_fac1, machine->broken_down_angle_fac2);
  }
  machine->g_unk_breakdown_int = (uint8_t)(machine->g_unk_breakdown_int + 1u);
  if (0xefu < machine->g_unk_breakdown_int) {
    if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
      machine->velocity = (fzgx_vec3){0};
      machine->position = machine->position_old;
      if ((machine->state_2 & 0x90u) == 0u) {
        machine->state_2 |= 0x80u;
        machine->state_2 |= 0x100u;
      }
    }
    machine->g_unk_breakdown_int = 0xf0u;
  }
}

static bool fzgx_debug_trace_visual_basis_window_exact(uint32_t frame_index);
static void fzgx_debug_log_mat43_basis_exact(
    const char *channel,
    const char *stage,
    uint32_t frame_index,
    uint32_t machine_index,
    const fzgx_mat43 *transform,
    const fzgx_machine_snapshot *machine);

static void fzgx_create_machine_visual_transform_exact(
    fzgx_machine_snapshot *machine,
    uint32_t anim_timer) {
  fzgx_mat43 current_transform;
  fzgx_vec3 visual_origin;
  float startup_wobble = 0.0f;
  float startup_roll_offset;
  uint32_t velocity_hash;
  float shake_scale;

  if (machine == 0) {
    return;
  }

  if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
    fzgx_debug_log_mat43_basis_exact(
        "visualstage",
        "input_physical",
        anim_timer,
        0u,
        &machine->basis_physical,
        machine);
  }

  current_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&current_transform, machine->position);

  if (machine->base_speed <= 2.0f) {
    startup_wobble = (2.0f - machine->base_speed) * 0.5f;
  }
  if (machine->frames_since_start_2 < 90u) {
    startup_wobble *= (float)machine->frames_since_start_2 / 90.0f;
  }
  machine->unk_stat_0x5d4 += 0.05f * (startup_wobble - machine->unk_stat_0x5d4);
  startup_roll_offset =
      (float)(int16_t)(int)(182.04445f * 0.5f *
                            (machine->unk_stat_0x5d4 *
                             fzgx_math_sincos_14b(
                                 (uint16_t)((int16_t)anim_timer * 0x109)).sin_value));

  {
    float vertical_offset =
        0.006f * (machine->unk_stat_0x5d4 *
                  fzgx_math_sincos_14b((uint16_t)((int16_t)anim_timer * 0x1a3)).sin_value);

    if ((machine->frames_since_start_2 < 90u) && (0x28u < machine->machine_id)) {
      vertical_offset += 0.5f * (1.0f - (float)machine->frames_since_start_2 / 90.0f);
    }
    visual_origin = fzgx_vec3_add(
        machine->position,
        fzgx_transform_local_vector(
            &current_transform,
            (fzgx_vec3){0.0f, vertical_offset - 0.2f * machine->unk_stat_0x5d4, 0.0f}));
  }

  fzgx_mat43_normalize_basis_exact(&current_transform);
  fzgx_mat43_set_origin_exact(&current_transform, (fzgx_vec3){0});
  machine->basis_physical = current_transform;
  if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
    fzgx_debug_log_mat43_basis_exact(
        "visualstage",
        "normalized_physical",
        anim_timer,
        0u,
        &current_transform,
        machine);
  }

  {
    float suspension_pitch =
        machine->suspension_corners[2].offset.z / -machine->suspension_corners[0].offset.z - 1.0f;
    fzgx_mat43 pitch_transform = current_transform;

    if (suspension_pitch < -0.2f) {
      suspension_pitch = -0.2f;
    } else if (0.2f < suspension_pitch) {
      suspension_pitch = 0.2f;
    }
    fzgx_mat43_rotate_about_x_right(
        &pitch_transform, (uint16_t)(int)(182.04445f * 30.0f * suspension_pitch));
    machine->g_pitch_mtx_0x5e0 = pitch_transform;
  }

  fzgx_mat43_rotate_about_z_right(
      &current_transform,
      (uint16_t)(int)(10430.378f * (machine->broken_down_angle_fac1.z / machine->weight_derived_3)));
  fzgx_mat43_rotate_about_y_right(
      &current_transform,
      (uint16_t)(int)(10430.378f * (machine->broken_down_angle_fac1.y / machine->weight_derived_2)));
  fzgx_mat43_rotate_about_x_right(
      &current_transform,
      (uint16_t)(int)(10430.378f * (machine->broken_down_angle_fac1.x / machine->weight_derived_1)));
  if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
    fzgx_debug_log_mat43_basis_exact(
        "visualstage",
        "post_breakdown_rot",
        anim_timer,
        0u,
        &current_transform,
        machine);
  }

  if ((machine->state_2 & 0x20u) == 0u) {
    fzgx_mat43 local_visual = fzgx_mat43_identity_exact();

    if ((machine->machine_state & FZGX_MS_ACTIVE) != 0u) {
      machine->turn_reaction_effect +=
          0.05f * (machine->turn_reaction_input - machine->turn_reaction_effect);
      fzgx_mat43_rotate_about_y_right(
          &local_visual, (uint16_t)(int)(182.04445f * machine->turn_reaction_effect));
    }

    {
      float speed_mag = fzgx_vec3_length(machine->velocity);
      float speed_norm = (speed_mag / machine->stat_weight) / 4.629629629f;
      int16_t angular_roll_angle = (int16_t)(int)(
          10430.378f * speed_norm * 4.5f *
          (machine->angular_velocity.y / machine->weight_derived_2));
      int combined_roll;
      float visual_pitch_effect;
      float visual_roll_effect = 2.5f * (machine->visual_roll / machine->weight_derived_3);
      fzgx_quat target_quat;

      machine->strafe_visual_roll_angle = (int16_t)(int)(
          182.04445f * (machine->stat_strafe / 15.0f) * -5.0f *
          machine->input_strafe_1_6 * speed_norm);
      combined_roll = (int)angular_roll_angle + (int)machine->strafe_visual_roll_angle;
      visual_pitch_effect =
          1.0f - (float)((combined_roll < 0) ? -combined_roll : combined_roll) / 3640.0f;
      if (visual_pitch_effect < 0.0f) {
        visual_pitch_effect = 0.0f;
      }
      visual_pitch_effect *= 0.7f * (machine->visual_pitch / machine->weight_derived_1);
      if (visual_pitch_effect < -0.3f) {
        visual_pitch_effect = -0.3f;
      } else if (0.3f < visual_pitch_effect) {
        visual_pitch_effect = 0.3f;
      }
      if (visual_roll_effect < -0.5f) {
        visual_roll_effect = -0.5f;
      } else if (0.5f < visual_roll_effect) {
        visual_roll_effect = 0.5f;
      }
      fzgx_mat43_rotate_about_x_right(
          &local_visual, (uint16_t)(int)(10430.378f * visual_pitch_effect));
      combined_roll += (int16_t)(int)(10430.378f * -visual_roll_effect);
      if (combined_roll < -0x238e) {
        combined_roll = -0x238e;
      } else if (0x238e < combined_roll) {
        combined_roll = 0x238e;
      }
      fzgx_mat43_rotate_about_z_right(
          &local_visual, (uint16_t)(int)((float)(int16_t)combined_roll + startup_roll_offset));
      if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
        fzgx_debug_log_mat43_basis_exact(
            "visualstage",
            "local_visual_pre_quat",
            anim_timer,
            0u,
            &local_visual,
            machine);
      }
      fzgx_mat43_to_quat_exact(&local_visual, &target_quat);
      fzgx_quat_slerp_exact(0.2f, &machine->unk_quat_0x5c4, &machine->unk_quat_0x5c4, &target_quat);
      fzgx_mat43_from_quat_exact(&local_visual, &machine->unk_quat_0x5c4);
      if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
        fzgx_debug_log_mat43_basis_exact(
            "visualstage",
            "local_visual_post_quat",
            anim_timer,
            0u,
            &local_visual,
            machine);
      }
      fzgx_mtxa_multiply_mtx_exact(&current_transform, &local_visual);
      if (machine->spinattack_angle != 0u) {
        if (machine->spinattack_direction == 0u) {
          fzgx_mat43_rotate_about_y_right(&current_transform, (uint16_t)machine->spinattack_angle);
        } else {
          fzgx_mat43_rotate_about_y_right(&current_transform, (uint16_t)(-(int)machine->spinattack_angle));
        }
      }
      if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
        fzgx_debug_log_mat43_basis_exact(
            "visualstage",
            "post_local_visual",
            anim_timer,
            0u,
            &current_transform,
            machine);
      }
    }
  } else {
    current_transform = machine->transform_visual;
  }

  fzgx_mat43_set_origin_exact(&current_transform, visual_origin);

  velocity_hash = fzgx_float_bits_exact(machine->velocity.z) ^
                  fzgx_float_bits_exact(machine->velocity.x) ^
                  fzgx_float_bits_exact(machine->velocity.y);
  shake_scale = 0.00006f * machine->visual_shake_mult;
  fzgx_mat43_rotate_about_z_right(
      &current_transform,
      (uint16_t)(int)(10430.378f * shake_scale *
                       ((float)((velocity_hash ^ fzgx_float_bits_exact(machine->angular_velocity.y)) & 0xffffu) /
                        65536.0f)));
  fzgx_mat43_rotate_about_x_right(
      &current_transform,
      (uint16_t)(int)(10430.378f * shake_scale *
                       ((float)((velocity_hash ^ fzgx_float_bits_exact(machine->angular_velocity.x)) & 0xffffu) /
                        65536.0f)));
  if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
    fzgx_debug_log_mat43_basis_exact(
        "visualstage",
        "post_shake",
        anim_timer,
        0u,
        &current_transform,
        machine);
  }

  if ((machine->machine_state & FZGX_MS_BOOSTING) == 0u) {
    machine->height_adjust_from_boost -= 0.05f * machine->height_adjust_from_boost;
  } else {
    float pitch_adjust = machine->visual_pitch;
    if (pitch_adjust < 0.0f) {
      pitch_adjust = 0.0f;
    }
    machine->height_adjust_from_boost +=
        0.2f * (4.5f * (pitch_adjust / machine->weight_derived_1) - machine->height_adjust_from_boost);
    if (0.3f < machine->height_adjust_from_boost) {
      machine->height_adjust_from_boost = 0.3f;
    }
  }
  fzgx_mat43_set_origin_exact(&current_transform, fzgx_vec3_add(
      fzgx_mat43_get_origin_exact(&current_transform),
      fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&current_transform), machine->height_adjust_from_boost)));

  if ((machine->terrain_flags & FZGX_TERRAIN_DIRT) != 0u) {
    double dirt_scale = (double)(0.1f + machine->speed_kmh / 900.0f);
    fzgx_vec3 dirt_jitter;

    if (1.0 < dirt_scale) {
      dirt_scale = 1.0;
    }
    dirt_jitter.x =
        (float)((velocity_hash ^ fzgx_float_bits_exact(machine->angular_velocity.y)) & 0xffffu) /
            65536.0f -
        0.5f;
    dirt_jitter.y = 0.0f;
    dirt_jitter.z =
        (float)((velocity_hash ^ fzgx_float_bits_exact(machine->angular_velocity.z)) & 0xffffu) /
            65536.0f -
        0.5f;
    dirt_jitter = fzgx_transform_local_vector(&current_transform, dirt_jitter);
    dirt_jitter = fzgx_vec3_scale(dirt_jitter, (float)(0.15 * dirt_scale));
    fzgx_mat43_set_origin_exact(
        &current_transform,
        fzgx_vec3_add(fzgx_mat43_get_origin_exact(&current_transform), dirt_jitter));
  }

  if ((machine->entrant_runtime_flags & FZGX_ENTRANT_RUNTIME_FLAG_VISUAL_SCALE) != 0u) {
    fzgx_apply_entrant_visual_scale_table_exact(machine, &current_transform);
  }
  machine->transform_visual = current_transform;
  if (fzgx_debug_trace_visual_basis_window_exact(anim_timer)) {
    fzgx_debug_log_mat43_basis_exact(
        "visualstage",
        "final_visual",
        anim_timer,
        0u,
        &machine->transform_visual,
        machine);
  }
}

static void fzgx_apply_entrant_visual_scale_table_exact(
    const fzgx_machine_snapshot *machine,
    fzgx_mat43 *transform_inout) {
  float visual_scale = 1.0f;

  if ((machine == 0) || (transform_inout == 0)) {
    return;
  }
  if (machine->machine_id == 0u) {
    visual_scale = 3.0f;
  } else if (machine->machine_id == 1u) {
    /* Raw slice uses (&FLOAT_3)[machine_id * 2]; adjacent collision table follows 2.0 -> 1.5. */
    visual_scale = 2.5f;
  }
  fzgx_mat43_set_basis_x_exact(transform_inout, fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(transform_inout), visual_scale));
  fzgx_mat43_set_basis_y_exact(transform_inout, fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(transform_inout), visual_scale));
  fzgx_mat43_set_basis_z_exact(transform_inout, fzgx_vec3_scale(fzgx_mat43_get_basis_z_exact(transform_inout), visual_scale));
}

int32_t fzgx_sim_get_time_extension_trigger_progress_index(
    const fzgx_machine_track_state *track) {
  if (track == 0) {
    return 0;
  }
  return track->lap_start_cp * 4 +
         (int32_t)fzgx_count_leading_zeros_exact(track->time_extension_trigger_mask);
}

static void fzgx_update_time_extension_trigger_mask_from_motion_exact(
    const fzgx_track_course_content *course,
    const fzgx_vec3 *position_current,
    const fzgx_vec3 *position_old,
    uint32_t *mask) {
  uint32_t trigger_index;
  int32_t cleared_index = -1;

  if ((course == 0) || (position_current == 0) || (position_old == 0) || (mask == 0) ||
      (course->time_extension_trigger_count == 0u) || (course->time_extension_triggers == 0)) {
    return;
  }

  trigger_index = fzgx_count_leading_zeros_exact(*mask);
  for (; trigger_index < course->time_extension_trigger_count; ++trigger_index) {
    const fzgx_time_extension_trigger_record *trigger =
        &course->time_extension_triggers[trigger_index];
    uint32_t rotate = (trigger_index + 1u) & 0x1fu;
    fzgx_mat43 trigger_transform = {0};
    fzgx_vec3 local_current;
    fzgx_vec3 local_old;

    if ((((*mask << rotate) | (*mask >> (32u - rotate))) & 1u) == 0u) {
      continue;
    }

    trigger_transform.basis_x_x = 1.0f;
    trigger_transform.basis_y_y = 1.0f;
    trigger_transform.basis_z_z = 1.0f;
    fzgx_mat43_set_origin_exact(&trigger_transform, trigger->position);
    if (trigger->rotation_z_angle16 != 0u) {
      fzgx_mat43_rotate_about_z_right(&trigger_transform, trigger->rotation_z_angle16);
    }
    if (trigger->rotation_y_angle16 != 0u) {
      fzgx_mat43_rotate_about_y_right(&trigger_transform, trigger->rotation_y_angle16);
    }
    if (trigger->rotation_x_angle16 != 0u) {
      fzgx_mat43_rotate_about_x_right(&trigger_transform, trigger->rotation_x_angle16);
    }
    fzgx_mat43_rigid_invert_exact(&trigger_transform);
    local_current = fzgx_transform_local_point(&trigger_transform, *position_current);
    local_old = fzgx_transform_local_point(&trigger_transform, *position_old);

    if ((local_current.z < 0.0f) && (0.0f <= local_old.z)) {
      float scale_y = 5.0f * trigger->scale.y;
      float scale_x = 5.0f * trigger->scale.x;
      float crossing_t;
      float local_x;
      float local_y;

      if ((scale_x == 0.0f) || (scale_y == 0.0f) || (local_current.z == local_old.z)) {
        continue;
      }

      crossing_t = -(local_current.z / (local_current.z - local_old.z));
      local_current.y /= scale_y;
      local_old.y /= scale_y;
      local_current.x /= scale_x;
      local_old.x /= scale_x;
      local_x = (local_current.x - local_old.x) * crossing_t + local_current.x;
      local_y = (local_current.y - local_old.y) * crossing_t + local_current.y;
      if (trigger->option == 0u) {
        if ((local_x < -1.0f) || (1.0f < local_x) || (local_y < -1.0f)) {
          continue;
        }
      } else {
        local_y = local_x * local_x + local_y * local_y;
      }
      cleared_index = (int32_t)trigger_index;
      if (local_y <= 1.0f) {
        break;
      }
    }
  }

  if (0 <= cleared_index) {
    *mask &= 0xffffffffu >> (uint32_t)(cleared_index + 1);
  }
}

static void fzgx_recompute_machine_track_derived_metrics(
    fzgx_machine_snapshot *machine,
    const fzgx_track_manifest *track_manifest) {
  fzgx_machine_track_state *track;
  fzgx_mat43 local_machine_transform;
  fzgx_track_frame_record frame_record;
  fzgx_vec3 local_origin;
  float ratio;
  float width_or_radius;

  track = &machine->track_state;
  local_machine_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&local_machine_transform, machine->position);
  memset(&frame_record, 0, sizeof(frame_record));
  frame_record.track_current_transform = track->track_current_transform;
  fzgx_mat43_rigid_invert_exact(&frame_record.track_current_transform);
  fzgx_mat43_set_origin_exact(&local_machine_transform, machine->position);
  fzgx_mtxa_multiply_mtx_exact(&local_machine_transform, &frame_record.track_current_transform);
  local_origin = fzgx_mat43_get_origin_exact(&local_machine_transform);
  track->track_relative_yaw_angle16 =
      fzgx_math_atan2_angle16(local_machine_transform.basis_z_x, local_machine_transform.basis_z_z);

  frame_record.track_current_transform = track->track_current_transform;
  frame_record.track_current_scale = track->track_current_scale;
  frame_record.track_scl_x = track->track_scl_x;
  frame_record.track_scl_y = track->track_scl_y;
  frame_record.track_anchor = track->track_anchor;
  frame_record.track_forward = track->track_forward;
  frame_record.track_up = track->track_up;
  frame_record.track_width_or_radius = track->track_width_or_radius;
  frame_record.track_hcylin = track->track_hcylin;
  frame_record.track_follow_offset = track->track_follow_offset;
  frame_record.track_flags = track->flags;
  if (fzgx_track_frame_get_width_and_scale(&frame_record, &width_or_radius, &ratio) !=
      FZGX_STATUS_OK) {
    width_or_radius = track->track_width_or_radius;
    ratio = 0.0f;
  }

  if (((track->flags & 0x02200000u) == 0u) && (ratio != 0.0f)) {
    if ((track->flags & 0x01800000u) == 0u) {
      fzgx_vec3 side_axis = {0.0f, -0.5f * track->track_scl_y, 0.0f};
      float local_x = local_origin.x;
      float half_scl_x = 0.5f * track->track_scl_x;
      float abs_local_x = fabsf(local_x);

      track->desired_dist_from_track_center =
          (1.0f + 2.1415927f * ratio) * (track->track_width_or_radius - track->track_scl_x);

      if (half_scl_x < abs_local_x) {
        float signed_angle;
        float angle_from_track_forward;
        if (local_x >= 0.0f) {
          local_x -= half_scl_x;
        } else {
          local_x += half_scl_x;
        }
        local_origin.x = local_x;
        signed_angle = (local_x < 0.0f) ? -1.0f : 1.0f;
        signed_angle *= (float)fzgx_fixed_arcsin_angle16_from_dot(
            fzgx_vec3_normalized_dot(side_axis, local_origin));
        if ((track->flags & 1u) != 0u) {
          float flip_sign = (signed_angle < 0.0f) ? -1.0f : 1.0f;
          signed_angle = flip_sign * (32768.0f - fabsf(signed_angle));
        }
        angle_from_track_forward =
            0.5f * ((signed_angle / 32768.0f) * track->desired_dist_from_track_center);
        if (angle_from_track_forward >= 0.0f) {
          angle_from_track_forward += half_scl_x;
        } else {
          angle_from_track_forward -= half_scl_x;
        }
        track->angle_from_track_forward = angle_from_track_forward;
        track->desired_dist_from_track_center += 2.0f * track->track_scl_x;
      } else {
        float angle_from_track_forward = abs_local_x;
        track->desired_dist_from_track_center += 2.0f * track->track_scl_x;
        if (((local_origin.y >= 0.0f) ? 1u : 0u) != (track->flags & 1u)) {
          angle_from_track_forward = 0.5f * track->desired_dist_from_track_center - abs_local_x;
        }
        if (local_origin.x < 0.0f) {
          angle_from_track_forward = -angle_from_track_forward;
        }
        track->angle_from_track_forward = angle_from_track_forward;
      }
    } else {
      fzgx_vec3 side_axis = {0.0f, -0.5f * track->track_current_scale.y, 0.0f};
      float signed_angle = (local_origin.x < 0.0f) ? -1.0f : 1.0f;
      track->desired_dist_from_track_center =
          FZGX_HALFPI * (track->track_hcylin *
                         (track->track_current_scale.x + track->track_current_scale.y));
      signed_angle *= (float)fzgx_fixed_arcsin_angle16_from_dot(
          fzgx_vec3_normalized_dot(side_axis, local_origin));
      if ((track->flags & 1u) != 0u) {
        float flip_sign = (signed_angle < 0.0f) ? -1.0f : 1.0f;
        signed_angle = flip_sign * (32768.0f - fabsf(signed_angle));
      }
      if (track->track_hcylin != 0.0f) {
        track->angle_from_track_forward =
            track->desired_dist_from_track_center *
            (signed_angle / (65536.0f * track->track_hcylin));
      } else {
        track->angle_from_track_forward = 0.0f;
      }
    }
  } else {
    track->desired_dist_from_track_center = width_or_radius;
    track->angle_from_track_forward = local_origin.x;
  }

  fzgx_recompute_machine_track_progress_metrics(machine, track_manifest);
}

static void fzgx_sync_machine_active_checkpoint_pointers_exact(
    fzgx_machine_track_state *track) {
  if (track == 0) {
    return;
  }
  if (track->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_NEXT) {
    track->active_cp_idx_ptr_offset = offsetof(fzgx_machine_track_state, next_cp_idx);
    track->active_frac_ptr_offset = offsetof(fzgx_machine_track_state, next_cp_frac);
    return;
  }
  track->active_cp_idx_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_idx);
  track->active_frac_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_frac);
}

static void fzgx_sync_machine_checkpoint_bank_slot0s(fzgx_machine_track_state *track) {
  track->stable_cp_idx[0] = track->cur_cp_idx;
  track->stable_cp_frac[0] = track->cur_cp_frac;
  track->predictive_cp_idx[0] = track->next_cp_idx;
  track->predictive_cp_frac[0] = track->next_cp_frac;
  fzgx_sync_machine_active_checkpoint_pointers_exact(track);
}

static void fzgx_copy_checkpoint_bank(
    int32_t *dst_idx,
    float *dst_frac,
    const int32_t *src_idx,
    const float *src_frac) {
  memcpy(dst_idx, src_idx, 4u * sizeof(*dst_idx));
  memcpy(dst_frac, src_frac, 4u * sizeof(*dst_frac));
}

static int32_t fzgx_get_machine_active_checkpoint_index(
    const fzgx_machine_track_state *track) {
  if (track->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_NEXT) {
    return track->next_cp_idx;
  }
  return track->cur_cp_idx;
}

static float fzgx_get_machine_active_checkpoint_fraction(
    const fzgx_machine_track_state *track) {
  if (track->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_NEXT) {
    return track->next_cp_frac;
  }
  return track->cur_cp_frac;
}

static void fzgx_get_machine_track_frame_refresh_checkpoint_exact(
    const fzgx_machine_track_state *track,
    int32_t *checkpoint_index_out,
    float *checkpoint_fraction_out) {
  if ((track == 0) || (checkpoint_index_out == 0) || (checkpoint_fraction_out == 0)) {
    return;
  }
  if ((track->next_cp_idx < 0) || (track->next_cp_idx == track->cur_cp_idx)) {
    *checkpoint_index_out = fzgx_get_machine_active_checkpoint_index(track);
    *checkpoint_fraction_out = fzgx_get_machine_active_checkpoint_fraction(track);
  } else {
    *checkpoint_index_out = track->cur_cp_idx;
    *checkpoint_fraction_out = track->cur_cp_frac;
  }
}

static void fzgx_seed_world_spherecast_from_machine_track_state_exact(
    fzgx_world_spherecast_request *request,
    const fzgx_machine_track_state *track) {
  uint32_t history_count;
  uint32_t i;

  if ((request == 0) || (track == 0)) {
    return;
  }

  request->checkpoint_seed_index = track->cur_cp_pointer;
  request->checkpoint_seed_aux = track->checkpoint_variant_count;
  if (track->checkpoint_variant_count <= 0) {
    return;
  }

  history_count = (uint32_t)track->checkpoint_variant_count;
  if (history_count > 4u) {
    history_count = 4u;
  }
  request->checkpoint_history_count = history_count;
  request->checkpoint_history_index[0] = track->cur_cp_pointer;
  for (i = 1u; i < history_count; ++i) {
    request->checkpoint_history_index[i] = track->seg_index_hist[i - 1u];
  }
}

static void fzgx_apply_machine_active_checkpoint_bank_exact(
    fzgx_machine_track_state *track,
    const fzgx_active_checkpoint_bank_result *bank_result) {
  size_t i;

  if (track->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT) {
    fzgx_copy_checkpoint_bank(
        track->stable_cp_idx,
        track->stable_cp_frac,
        bank_result->checkpoint_index,
        bank_result->checkpoint_fraction);
  } else {
    fzgx_copy_checkpoint_bank(
        track->predictive_cp_idx,
        track->predictive_cp_frac,
        bank_result->checkpoint_index,
        bank_result->checkpoint_fraction);
  }
  track->cur_cp_pointer = bank_result->containment_checkpoint_index[0];
  track->checkpoint_variant_count = (int32_t)bank_result->checkpoint_variant_count;
  for (i = 0u; i < 3u; ++i) {
    track->seg_index_hist[i] = bank_result->containment_checkpoint_index[i + 1u];
  }
  fzgx_sync_machine_checkpoint_bank_slot0s(track);
}

static void fzgx_apply_machine_current_checkpoint_neighbor_bank_exact(
    fzgx_machine_track_state *track,
    const fzgx_active_checkpoint_bank_result *bank_result) {
  size_t i;

  for (i = 0u; i < 3u; ++i) {
    track->neighbor_cp_idx[i] = bank_result->checkpoint_index[i + 1u];
    track->neighbor_cp_frac[i] = bank_result->checkpoint_fraction[i + 1u];
  }
}

static double fzgx_distance_to_segment_exact(
    const fzgx_vec3 *point,
    const fzgx_vec3 *seg_start,
    const fzgx_vec3 *seg_end,
    fzgx_vec3 *closest_point_out) {
  float seg_x;
  float seg_y;
  float seg_z;
  float start_to_point_x;
  float start_to_point_y;
  float start_to_point_z;
  float t;

  seg_x = seg_end->x - seg_start->x;
  seg_y = seg_end->y - seg_start->y;
  seg_z = seg_end->z - seg_start->z;
  start_to_point_x = seg_start->x - point->x;
  start_to_point_y = seg_start->y - point->y;
  start_to_point_z = seg_start->z - point->z;
  t = -(seg_z * start_to_point_z + seg_y * start_to_point_y + seg_x * start_to_point_x) /
      (seg_z * seg_z + seg_y * seg_y + seg_x * seg_x);
  if (!isfinite(t)) {
    *closest_point_out = *seg_start;
    return (double)fzgx_vec3_length(fzgx_vec3_sub(*seg_start, *point));
  }
  if ((t < 0.0f) || (1.0f < t)) {
    fzgx_vec3 start_delta = fzgx_vec3_sub(*seg_start, *point);
    fzgx_vec3 end_delta = fzgx_vec3_sub(*seg_end, *point);
    float start_dist_sq = fzgx_vec3_dot(start_delta, start_delta);
    float end_dist_sq = fzgx_vec3_dot(end_delta, end_delta);
    if (end_dist_sq <= start_dist_sq) {
      *closest_point_out = *seg_end;
      return (double)sqrtf(end_dist_sq);
    }
    *closest_point_out = *seg_start;
    return (double)sqrtf(start_dist_sq);
  }
  closest_point_out->x = seg_start->x + seg_x * t;
  closest_point_out->y = seg_start->y + seg_y * t;
  closest_point_out->z = seg_start->z + seg_z * t;
  return (double)fzgx_vec3_length(fzgx_vec3_sub(*closest_point_out, *point));
}

static fzgx_status fzgx_track_course_eval_shared_checkpoint_distance_exact(
    const fzgx_track_course_content *course,
    int32_t checkpoint_index,
    float checkpoint_fraction,
    float *distance_out) {
  const fzgx_track_node_record *track_node = 0;
  const fzgx_checkpoint_record *checkpoint;
  fzgx_status status;

  if ((course == 0) || (distance_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (checkpoint_index < 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  status = fzgx_track_course_get_track_node(course, (uint32_t)checkpoint_index, &track_node);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (track_node->checkpoint_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  checkpoint = &course->checkpoints[track_node->checkpoint_offset];
  *distance_out =
      checkpoint->start_distance +
      checkpoint_fraction * (checkpoint->end_distance - checkpoint->start_distance);
  return FZGX_STATUS_OK;
}

static void fzgx_reset_machine_track_state(
    fzgx_machine_track_state *track,
    fzgx_racetrack_refresh_mode refresh_mode) {
  if (track != 0) {
    if (refresh_mode == FZGX_RACETRACK_REFRESH_NORMAL) {
      memset(track, 0, 0x117);
    } else {
      memset(track, 0, 0x1fc);
      track->lap_cross_cp = -1;
      track->time_extension_trigger_mask = -1;
    }
    track->need_resnap = (uint32_t)refresh_mode;
    track->active_cp_idx_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_idx);
    track->active_frac_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_frac);
    track->cur_cp_pointer = -1;
    track->cur_cp_idx = -1;
    track->next_cp_idx = -1;

    track->lap_split_gate_mask = (refresh_mode == FZGX_RACETRACK_REFRESH_NORMAL) ? 0 : -1;
    track->cached_frame_count = 0u;
    track->selected_cached_frame_index = 0;
    track->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
    memset(track->reserved_track_state_tail, 0, sizeof(track->reserved_track_state_tail));
  }
}

static void fzgx_update_machine_pitch_matrix_from_front_and_back_exact(
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *basis_transform) {
  fzgx_mat43 pitch_transform = *basis_transform;
  float pitch_ratio =
      machine->suspension_corners[3].offset.z / -machine->suspension_corners[0].offset.z - 1.0f;

  if (pitch_ratio < -0.2f) {
    pitch_ratio = -0.2f;
  } else if (0.2f < pitch_ratio) {
    pitch_ratio = 0.2f;
  }
  if (pitch_ratio != 0.0f) {
    fzgx_mat43_rotate_about_x_right(
        &pitch_transform, (uint16_t)(int)(182.04445f * 30.0f * pitch_ratio));
  }
  machine->g_pitch_mtx_0x5e0 = pitch_transform;
}

static void fzgx_reset_machine_runtime_snapshot(
    fzgx_machine_snapshot *machine,
    bool full_reset) {
  uint32_t preserved_machine_state = machine->machine_state;
  uint32_t preserved_state_2 = machine->state_2;
  fzgx_mat43 current_transform = machine->basis_physical;
  fzgx_mat43 basis_only_transform = machine->basis_physical;

  fzgx_mat43_set_origin_exact(&current_transform, machine->position);
  fzgx_mat43_set_origin_exact(&basis_only_transform, (fzgx_vec3){0});
  memset(&machine->track_query_filter_cache, 0, sizeof(machine->track_query_filter_cache));

  if (full_reset) {
    machine->machine_state =
        preserved_machine_state &
        (FZGX_MS_B30 | FZGX_MS_COMPLETEDRACE_2_Q | FZGX_MS_B10 | FZGX_MS_B9);
    machine->machine_flags &=
        ~(FZGX_MACHINE_FLAG_COMPLETED_RACE | FZGX_MACHINE_FLAG_RETIRED);
  } else {
    machine->machine_state =
        preserved_machine_state &
        (FZGX_MS_B30 | FZGX_MS_COMPLETEDRACE_2_Q | FZGX_MS_COMPLETEDRACE_1_Q | FZGX_MS_B10 |
         FZGX_MS_B9);
  }
  machine->state_2 = full_reset ? preserved_state_2 : (preserved_state_2 & 1u);
  machine->machine_flags &=
      ~(FZGX_MACHINE_FLAG_ACTIVE | FZGX_MACHINE_FLAG_AIRBORNE |
        FZGX_MACHINE_FLAG_SIDE_ATTACKING | FZGX_MACHINE_FLAG_SPIN_ATTACKING |
        FZGX_MACHINE_FLAG_ZERO_HP | FZGX_MACHINE_FLAG_FALLOUT |
        FZGX_MACHINE_FLAG_SIM_MOTION_RAN);
  machine->basis_physical = basis_only_transform;
  machine->basis_physical_other = basis_only_transform;
  machine->transform_visual = current_transform;
  if (machine->machine_id > 0x28u) {
    fzgx_mat43_set_origin_exact(
        &machine->transform_visual,
        fzgx_vec3_add(
            fzgx_mat43_get_origin_exact(&machine->transform_visual),
            fzgx_transform_local_vector(&current_transform, (fzgx_vec3){0.0f, 0.5f, 0.0f})));
  }
  machine->position_old = machine->position;
  machine->position_old_dupe = machine->position;
  machine->position_old_2 = machine->position;
  machine->velocity = (fzgx_vec3){0};
  machine->angular_velocity = (fzgx_vec3){0};
  machine->velocity_local = (fzgx_vec3){0};
  machine->velocity_local_flattened_and_rotated = (fzgx_vec3){0};
  machine->surface_normal = fzgx_mat43_get_basis_y_exact(&machine->basis_physical);
  machine->collision_push_min = (fzgx_vec3){0};
  machine->collision_push_max = (fzgx_vec3){0};
  machine->collision_push_total = (fzgx_vec3){0};
  machine->collision_response = (fzgx_vec3){0};
  machine->broken_down_angle_fac1 = (fzgx_vec3){0};
  machine->broken_down_angle_fac2 = (fzgx_vec3){0};
  machine->position_bottom =
      fzgx_transform_local_point(&current_transform, (fzgx_vec3){0.0f, -0.1f, 0.0f});
  machine->approach_dir = (fzgx_vec3){10000.0f, 0.0f, 0.0f};
  machine->current_checkpoint = 0;
  machine->checkpoint_fraction = 0.0f;
  machine->score = 0u;
  machine->clean_race_bonus_eligible = 0u;
  machine->energy = machine->max_energy;
  machine->base_speed = 0.0f;
  machine->speed_kmh = 0.0f;
  machine->turning_related = 0.0f;
  machine->dash_plate_hit_count = 0u;
  machine->terrain_flags = 0u;
  machine->terrain_flags_2 = 0u;
  machine->branch_indicator = 0u;
  machine->branch_flags = 0u;
  machine->floor_surface_flags = 0u;
  machine->floor_material_flags = 0u;
  machine->branch_slot = 4u;
  machine->control_profile_kind = 0u;
  machine->frames_since_start = 0u;
  machine->grip_frames_from_accel_press = 0u;
  machine->boost_frames = 0u;
  machine->boost_frames_manual = 0u;
  machine->height_adjust_from_boost = 0.0f;
  machine->race_start_charge = 0.0f;
  machine->boost_turbo = 0.0f;
  machine->air_tilt = 0.0f;
  machine->visual_shake_mult = 0.0f;
  machine->input_strafe_32 = 0.0f;
  machine->input_strafe_1_6 = 0.0f;
  machine->input_steer_pitch = 0.0f;
  machine->input_strafe = 0.0f;
  machine->input_steer_yaw = 0.0f;
  machine->input_accel = 0.0f;
  machine->input_brake = 0.0f;
  machine->unk_float_0x208 = 200.0f;
  machine->input_yaw_dupe = 0.0f;
  machine->visual_roll = 0.0f;
  machine->visual_pitch = 0.0f;
  machine->zero_minus_height_above_track = 0.0f;
  machine->turn_reaction_input = 0.0f;
  machine->turn_reaction_effect = 0.0f;
  machine->boost_energy_use_mult = 1.0f;
  machine->const_float_2_0 = 2.0f;
  machine->damage_from_last_hit = 0.0f;
  machine->spinattack_angle = 0u;
  machine->spinattack_angle_decrement = 0u;
  machine->spinattack_direction = 0u;
  machine->stat_grip_frames_from_accel_press = 0u;
  machine->brake_timer = 0u;
  machine->frames_since_start_2 = 0u;
  machine->side_attack_delay = 0u;
  machine->air_time = 0u;
  machine->machine_collision_frame_counter = 0u;
  machine->car_hit_invincibility = 0u;
  machine->machine_approach_frame_counter = 0u;
  machine->last_machine_approached = 0xffu;
  machine->time_since_ko_frame_counter = 0u;
  machine->g_unk_breakdown_int = 0u;
  machine->unk_byte_0x4c3 = 0u;
  machine->breakdown_frame_counter = 0u;
  machine->frames_since_death = 0u;
  machine->rail_collision_timer = 0u;
  machine->suspension_reset_flag = 0u;
  machine->boost_delay_frame_counter = 0u;
  machine->frames_until_restored = 0u;
  machine->unk_random_0x514 = 0xffffffffu;
  machine->restore_progress = 0.0f;
  machine->unk_restore_0x51c = 0.0f;
  machine->strafe_visual_roll_angle = 0;
  machine->unk_byte_0x240 = 0u;
  machine->unk_byte_0x241 = 0u;
  machine->unk_stat_0x5d4 = 0.0f;
  machine->unk_quat_0x5c4 = (fzgx_quat){0.0f, 0.0f, 0.0f, 1.0f};
  machine->g_restore_matrix_1 = basis_only_transform;
  fzgx_mat43_set_origin_exact(&machine->g_restore_matrix_1, machine->position);
  machine->g_restore_mtx_2 = machine->transform_visual;
  machine->g_restore_mtx_3 = machine->g_restore_matrix_1;
  machine->post_restore_frame_countdown = 0u;
  machine->g_pitch_mtx_0x5e0 = basis_only_transform;
  fzgx_mat43_set_origin_exact(&machine->g_pitch_mtx_0x5e0, (fzgx_vec3){0});
  memset(machine->suspension_state, 0, sizeof(machine->suspension_state));
  machine->side_attack_indicator = 0.0f;
  machine->spin_attack_kill_indicator = false;
  machine->machine_crashed = false;
  if (full_reset) {
    machine->restore_count = 0u;
    machine->max_speed_kmh = 0.0f;
    machine->wall_hit_count = 0u;
    machine->boost_count = 0u;
  }
  machine->state_2 &= 0xfffffc4fu;
  fzgx_reset_machine_corner_runtime_exact(machine);
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static void fzgx_apply_machine_placement_transform(
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *placement_transform) {
  fzgx_mat43 physical_transform = *placement_transform;

  machine->position = fzgx_mat43_get_origin_exact(placement_transform);
  machine->position_old = fzgx_mat43_get_origin_exact(placement_transform);
  machine->position_old_dupe = fzgx_mat43_get_origin_exact(placement_transform);
  machine->position_old_2 = fzgx_mat43_get_origin_exact(placement_transform);
  machine->surface_normal = fzgx_mat43_get_basis_y_exact(placement_transform);
  machine->position_bottom =
      fzgx_transform_local_point(placement_transform, (fzgx_vec3){0.0f, -0.1f, 0.0f});
  fzgx_mat43_set_origin_exact(&physical_transform, (fzgx_vec3){0});
  machine->basis_physical = physical_transform;
  machine->basis_physical_other = machine->basis_physical;
  machine->transform_visual = *placement_transform;
  if (machine->machine_id > 0x28u) {
    fzgx_mat43_set_origin_exact(&machine->transform_visual, fzgx_vec3_add(
            fzgx_mat43_get_origin_exact(&machine->transform_visual),
            fzgx_transform_local_vector(placement_transform, (fzgx_vec3){0.0f, 0.5f, 0.0f})));
  }
  machine->g_restore_matrix_1 = physical_transform;
  fzgx_mat43_set_origin_exact(&machine->g_restore_matrix_1, machine->position);
  machine->g_restore_mtx_2 = machine->transform_visual;
  machine->g_restore_mtx_3 = machine->g_restore_matrix_1;
  fzgx_update_machine_pitch_matrix_from_front_and_back_exact(machine, &physical_transform);
  fzgx_reset_machine_corner_runtime_exact(machine);
}

static fzgx_status fzgx_validate_world(const fzgx_sim_world *world) {
  if (world == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (world->api_version != FZGX_SIM_API_VERSION) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  if ((world->machine_capacity == 0u) || (world->machine_capacity > FZGX_SIM_MAX_MACHINES)) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  if (world->machine_count > world->machine_capacity) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  return FZGX_STATUS_OK;
}

typedef struct fzgx_track_sweep_query_exact {
  fzgx_vec3 seed_point;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  uint32_t branch_selector;
  float curve_time;
  uint32_t source_piece_word;
  uint32_t continuity_gate;
  const fzgx_track_segment_record *root_segment;
  const fzgx_checkpoint_record *checkpoint;
} fzgx_track_sweep_query_exact;
typedef struct fzgx_track_pending_side_contact_exact {
  int32_t branch_selector;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  uint32_t result_flags;
  float hit_time;
  fzgx_vec3 hit_point;
  fzgx_vec3 hit_normal;
  fzgx_vec3 push;
  fzgx_vec3 aux_hit_point;
  uint32_t summary_flags_ptr_opaque;
} fzgx_track_pending_side_contact_exact;
_Static_assert(sizeof(fzgx_track_pending_side_contact_exact) == 0x48,
               "fzgx_track_pending_side_contact_exact must match the raw pending side-contact bank record");
typedef struct fzgx_track_transient_hit_exact {
  bool valid;
  uint8_t reserved0[3];
  float hit_time;
  fzgx_vec3 hit_point;
  fzgx_vec3 aux_hit_point;
  fzgx_vec3 hit_normal;
  fzgx_vec3 push;
  uint32_t result_flags;
} fzgx_track_transient_hit_exact;
enum {
  FZGX_TRACK_PENDING_SIDE_CANDIDATE_CAPACITY_EXACT = 8u,
  FZGX_TRACK_SIDE_REJECTION_FRAME_CAPACITY_EXACT = 3u
};
typedef struct fzgx_track_piece_solver_state_exact {
  const fzgx_track_course_content *course;
  const fzgx_track_course_animation_content *animation_course;
  uint32_t authored_track_id;
  uint32_t debug_world_frame_index;
  fzgx_mat43 transform;
  fzgx_vec3 scale;
  uint32_t summary_seed_slot_count;
  uint8_t reserved0[4];
  fzgx_track_segment_trs_curve_cache_exact trs_curve_cache;
} fzgx_track_piece_solver_state_exact;
typedef struct fzgx_track_piece_solver_local_state_exact {
  fzgx_mat43 transform;
  fzgx_vec3 scale;
  float curved_width;
  float curved_height;
} fzgx_track_piece_solver_local_state_exact;

static fzgx_track_piece_solver_local_state_exact fzgx_capture_track_piece_solver_local_state_exact(
    const fzgx_track_piece_solver_state_exact *state) {
  fzgx_track_piece_solver_local_state_exact local_state;

  memset(&local_state, 0, sizeof(local_state));
  if (state != 0) {
    local_state.transform = state->transform;
    local_state.scale = state->scale;
    local_state.curved_width = fzgx_collision_scratch_read_f32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT);
    local_state.curved_height = fzgx_collision_scratch_read_f32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT);
  }
  return local_state;
}

static void fzgx_restore_track_piece_solver_local_state_exact(
    fzgx_track_piece_solver_state_exact *state,
    const fzgx_track_piece_solver_local_state_exact *local_state) {
  if ((state == 0) || (local_state == 0)) {
    return;
  }

  state->transform = local_state->transform;
  state->scale = local_state->scale;
  fzgx_collision_scratch_write_f32_current_exact(
      FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT, local_state->curved_width);
  fzgx_collision_scratch_write_f32_current_exact(
      FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT, local_state->curved_height);
}

static uint32_t fzgx_collision_scratch_current_track_side_summary_slot_flags_ptr_opaque_exact(
    uintptr_t summary_flags_ptr_exact) {
  return fzgx_track_side_query_summary_slot_flags_ptr_opaque_exact(
      g_fzgx_collision_piece_scratch_current_exact, summary_flags_ptr_exact);
}

static bool fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
    size_t result_flags_offset,
    float hit_time) {
  if (fzgx_collision_scratch_read_u32_current_exact(result_flags_offset) == 0u) {
    return true;
  }
  return hit_time <
         fzgx_collision_scratch_read_f32_current_exact(result_flags_offset + sizeof(uint32_t));
}

static void fzgx_collision_scratch_current_write_track_hit_slot_record_exact(
    size_t slot_offset,
    const fzgx_track_pending_side_contact_exact *record) {
  if ((record == 0) || (g_fzgx_collision_scratch_raw_current_exact == 0)) {
    return;
  }
  if ((slot_offset + sizeof(*record)) > FZGX_COLLISION_SCRATCH_RAW_SIZE_EXACT) {
    return;
  }
  memcpy(g_fzgx_collision_scratch_raw_current_exact + slot_offset, record, sizeof(*record));
}

static void fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
    size_t slot_offset,
    size_t result_flags_offset,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_transient_hit_exact *transient_hit,
    uintptr_t summary_flags_ptr_exact) {
  fzgx_track_pending_side_contact_exact slot_record;

  if ((query == 0) || (transient_hit == 0) || !transient_hit->valid) {
    return;
  }
  if (!fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
          result_flags_offset, transient_hit->hit_time)) {
    return;
  }

  memset(&slot_record, 0, sizeof(slot_record));
  slot_record.branch_selector = (int32_t)query->branch_selector;
  slot_record.checkpoint_index = query->checkpoint_index;
  slot_record.checkpoint_fraction = query->checkpoint_fraction;
  slot_record.result_flags = transient_hit->result_flags;
  slot_record.hit_time = transient_hit->hit_time;
  slot_record.hit_point = transient_hit->hit_point;
  slot_record.hit_normal = transient_hit->hit_normal;
  slot_record.push = transient_hit->push;
  slot_record.aux_hit_point = transient_hit->aux_hit_point;
  slot_record.summary_flags_ptr_opaque =
      fzgx_collision_scratch_current_track_side_summary_slot_flags_ptr_opaque_exact(
          summary_flags_ptr_exact);
  fzgx_collision_scratch_current_write_track_hit_slot_record_exact(slot_offset, &slot_record);
}

static bool fzgx_debug_trace_double_branches_window_exact(
    uint32_t authored_track_id,
    uint32_t machine_index,
    uint32_t frame_index) {
  if ((authored_track_id == 0x15u) && (machine_index == 0u)) {
    return (842u <= frame_index) && (frame_index <= 860u);
  }
  if (machine_index != 0u) {
    return false;
  }
  if (authored_track_id == 0x15u) {
    return (1760u <= frame_index) && (frame_index <= 1905u);
  }
  if (authored_track_id == 0x1bu) {
    return (((250u <= frame_index) && (frame_index <= 380u)) ||
            ((578u <= frame_index) && (frame_index <= 593u)) ||
            ((630u <= frame_index) && (frame_index <= 656u)) ||
            ((720u <= frame_index) && (frame_index <= 735u)));
  }
  if (authored_track_id != 0x1du) {
    return false;
  }
  return ((258u <= frame_index) && (frame_index <= 420u)) ||
         ((390u <= frame_index) && (frame_index <= 440u)) ||
         ((486u <= frame_index) && (frame_index <= 492u)) ||
         ((498u <= frame_index) && (frame_index <= 505u)) ||
         ((680u <= frame_index) && (frame_index <= 730u)) ||
         ((1508u <= frame_index) && (frame_index <= 1512u)) ||
         ((1919u <= frame_index) && (frame_index <= 1925u)) ||
         ((2109u <= frame_index) && (frame_index <= 2119u)) ||
         ((2178u <= frame_index) && (frame_index <= 2188u));
}

static void fzgx_debug_log_world_spherecast_seed_exact(
    const char *channel,
    uint32_t frame_index,
    const fzgx_world_spherecast_request *request,
    const fzgx_machine_track_state *track) {
  int32_t history0 = -1;
  int32_t history1 = -1;
  int32_t history2 = -1;
  int32_t history3 = -1;

  if ((channel == 0) || (request == 0) || (track == 0)) {
    return;
  }
  if (request->checkpoint_history_count > 0u) {
    history0 = request->checkpoint_history_index[0];
  }
  if (request->checkpoint_history_count > 1u) {
    history1 = request->checkpoint_history_index[1];
  }
  if (request->checkpoint_history_count > 2u) {
    history2 = request->checkpoint_history_index[2];
  }
  if (request->checkpoint_history_count > 3u) {
    history3 = request->checkpoint_history_index[3];
  }
  fprintf(
      stderr,
      "%s|frame=%u|stage=seed|flags=0x%08x|seed=%d|seed_aux=%u|hist_count=%u|"
      "hist=(%d,%d,%d,%d)|track_cur_ptr=%d|track_cp_count=%d|seg_hist=(%d,%d,%d)|"
      "cur_cp=%d|cur_cpf=%.6f|next_cp=%d|next_cpf=%.6f\n",
      channel,
      frame_index,
      request->flags,
      request->checkpoint_seed_index,
      request->checkpoint_seed_aux,
      request->checkpoint_history_count,
      history0,
      history1,
      history2,
      history3,
      track->cur_cp_pointer,
      track->checkpoint_variant_count,
      track->seg_index_hist[0],
      track->seg_index_hist[1],
      track->seg_index_hist[2],
      track->cur_cp_idx,
      track->cur_cp_frac,
      track->next_cp_idx,
      track->next_cp_frac);
}

static bool fzgx_debug_trace_mcsg_side_solver_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_piece_solver_state_exact *state) {
  if ((request == 0) || (query == 0) || (state == 0)) {
    return false;
  }

  return fzgx_debug_trace_double_branches_window_exact(
             state->authored_track_id, request->machine_index, state->debug_world_frame_index) &&
         (((request->flags & 0x0fffffffu) == 0x04060005u) ||
          ((request->flags & 0x0fffffffu) == 0x04260005u) ||
          ((request->flags & 0x000fffffu) == 0x00022800u) ||
          ((request->flags & 0x000fffffu) == 0x00023f00u) ||
         ((request->flags & 0x03ffffffu) == 0x03020005u));
}

static bool fzgx_debug_trace_visual_basis_window_exact(uint32_t frame_index) {
  return ((120u <= frame_index) && (frame_index <= 130u));
}

static void fzgx_debug_log_mat43_basis_exact(
    const char *channel,
    const char *stage,
    uint32_t frame_index,
    uint32_t machine_index,
    const fzgx_mat43 *transform,
    const fzgx_machine_snapshot *machine) {
  fzgx_vec3 basis_x;
  fzgx_vec3 basis_y;
  fzgx_vec3 basis_z;
  float len_x;
  float len_y;
  float len_z;
  float dot_xy;
  float dot_yz;
  float dot_zx;

  if ((channel == 0) || (stage == 0) || (transform == 0) || (machine == 0)) {
    return;
  }

  basis_x = fzgx_mat43_get_basis_x_exact(transform);
  basis_y = fzgx_mat43_get_basis_y_exact(transform);
  basis_z = fzgx_mat43_get_basis_z_exact(transform);
  len_x = fzgx_vec3_length(basis_x);
  len_y = fzgx_vec3_length(basis_y);
  len_z = fzgx_vec3_length(basis_z);
  dot_xy = fzgx_vec3_dot(basis_x, basis_y);
  dot_yz = fzgx_vec3_dot(basis_y, basis_z);
  dot_zx = fzgx_vec3_dot(basis_z, basis_x);
  fprintf(
      stderr,
      "%s|frame=%u|stage=%s|machine=%u|origin=(%.3f,%.3f,%.3f)|"
      "basis_len=(%.6f,%.6f,%.6f)|basis_dot=(%.6f,%.6f,%.6f)|"
      "state=0x%08x|state2=0x%08x|terrain=0x%08x\n",
      channel,
      frame_index,
      stage,
      machine_index,
      transform->origin_x,
      transform->origin_y,
      transform->origin_z,
      len_x,
      len_y,
      len_z,
      dot_xy,
      dot_yz,
      dot_zx,
      machine->machine_state,
      machine->state_2,
      machine->terrain_flags);
}

static fzgx_track_piece_solver_local_state_exact fzgx_capture_track_piece_solver_local_state_exact(
    const fzgx_track_piece_solver_state_exact *state);
static void fzgx_restore_track_piece_solver_local_state_exact(
    fzgx_track_piece_solver_state_exact *state,
    const fzgx_track_piece_solver_local_state_exact *local_state);

enum {
  FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X_EXACT = 6u,
  FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT = 1u,
  FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y_EXACT = 7u
};
static fzgx_status fzgx_apply_track_collision_node_transform_exact(
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static void fzgx_commit_pending_rejection_frame_exact(
    fzgx_track_piece_solver_state_exact *state);
static fzgx_status fzgx_apply_track_collision_transform_only_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_accumulate_road_embedded_surface_flags_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_resolve_track_side_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_resolve_track_modulated_road_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_resolve_track_modulated_road_contact_candidates_sonic_oval_variant_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_evaluate_float_animation_curve_derivative_exact(
    const fzgx_animation_curve *curve,
    float time,
    float *value_out);
static fzgx_status fzgx_try_append_track_side_contact_candidate_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_track_side_query_summary *current_summary,
    uint32_t source_piece_word,
    float local_start_x_norm,
    float local_end_x_norm,
    float local_start_y,
    float local_end_y,
    int side_sign,
    uint32_t *summary_activity_mask_inout,
    bool height_gate,
    bool *accepted_candidate_out);
static fzgx_status fzgx_resolve_track_capsule_pipe_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_resolve_track_open_pipe_cylinder_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_resolve_track_pipe_cylinder_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
/* Mirrors raw g_walk_bvh: branch-gated descendant walk plus per-piece dispatch. */
static fzgx_status fzgx_walk_track_collision_piece_tree_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t branch_slot,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static fzgx_status fzgx_dispatch_track_piece_solver_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t source_piece_word,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout);
static uint32_t fzgx_compute_track_side_summary_flags_exact(
    const fzgx_track_side_query_summary *summary);
static bool fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
    fzgx_sim_world *world,
    const fzgx_vec3 *start_point,
    const fzgx_vec3 *end_point,
    fzgx_vec3 *in_out_contact,
    fzgx_vec3 *out_surface_normal,
    float *contact_dist_out);

static uint32_t fzgx_compute_track_side_summary_flags_exact(
    const fzgx_track_side_query_summary *summary) {
  uint32_t flags_or = 0u;
  uint32_t slot_count;
  uint32_t slot_index;

  if (summary == 0) {
    return 0u;
  }

  slot_count = summary->candidate_count;
  if (slot_count > 4u) {
    slot_count = 4u;
  }
  for (slot_index = 0u; slot_index < slot_count; ++slot_index) {
    uint32_t flags = summary->flags[slot_index];

    if ((flags & 0xf0000000u) != 0u) {
      flags_or |= flags;
    }
  }
  return flags_or;
}

static bool fzgx_track_piece_matches_branch_slot_exact(
    const fzgx_track_segment_record *track_segment,
    uint32_t branch_slot) {
  if (track_segment == 0) {
    return false;
  }
  return (branch_slot == 0u) || (track_segment->branch_index == 0) ||
         ((uint32_t)track_segment->branch_index == branch_slot);
}

static void fzgx_apply_track_side_summary_special_postprocess_exact(
    fzgx_track_side_query_summary *summary) {
  uint32_t slot_count;
  uint32_t slot_index;

  if ((summary == 0) || (summary->special_postprocess_flag == 0u)) {
    return;
  }

  slot_count = summary->candidate_count;
  if (slot_count > 4u) {
    slot_count = 4u;
  }
  for (slot_index = 0u; slot_index < slot_count; ++slot_index) {
    if ((summary->flags[slot_index] & 0x02200000u) != 0u) {
      summary->flags[slot_index] |= 0x20000000u;
    }
  }
  summary->summary_flags = fzgx_compute_track_side_summary_flags_exact(summary);
}

static void fzgx_reject_pending_side_candidates_against_current_piece_exact(
    const fzgx_track_segment_record *track_segment,
    const fzgx_mat43 *inverse_transform,
    float scale_x,
    float scale_y,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_track_side_query_summary *current_summary) {
  float lateral_limit;
  float rail_height_a;
  float rail_height_b;
  uint32_t pending_index;
  bool summary_flags_dirty = false;

  if ((track_segment == 0) || (inverse_transform == 0) || (state == 0)) {
    return;
  }
  if (fabsf(scale_x) <= FLT_EPSILON) {
    return;
  }

  (void)scale_y;
  rail_height_a = track_segment->rail_height_right;
  rail_height_b = track_segment->rail_height_left;
  lateral_limit =
      (fzgx_track_road_lateral_max_exact * scale_x) - fzgx_track_side_rejection_epsilon_exact;

  for (pending_index = 0u;
       pending_index <
       fzgx_collision_scratch_read_u32_current_exact(FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT);
       ++pending_index) {
    fzgx_track_pending_side_contact_exact *candidate =
        (fzgx_track_pending_side_contact_exact *)(void
                                                     *)(g_fzgx_collision_scratch_raw_current_exact +
                                                        FZGX_COLLISION_SCRATCH_PENDING_SIDE_BANK_OFFSET_EXACT +
                                                        (pending_index * sizeof(*candidate)));
    fzgx_vec3 local_aux_point;

    if (candidate->result_flags == 0u) {
      continue;
    }
    local_aux_point = fzgx_transform_local_point(inverse_transform, candidate->aux_hit_point);
    if ((fabsf(local_aux_point.x) <= lateral_limit) &&
        ((fabsf(local_aux_point.y) <= rail_height_b) ||
         (fabsf(local_aux_point.y) <= rail_height_a))) {
      if ((candidate->result_flags & 0x00000c00u) != 0u) {
        if (current_summary != 0) {
          current_summary->special_postprocess_flag = 1u;
        }
      }
      if ((current_summary != 0) &&
          (fzgx_collision_scratch_read_u32_current_exact(
               FZGX_COLLISION_SCRATCH_PENDING_SIDE_SUMMARY_SLOT_MAP_OFFSET_EXACT +
               (pending_index * sizeof(uint32_t))) < 4u)) {
        current_summary->flags[fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_PENDING_SIDE_SUMMARY_SLOT_MAP_OFFSET_EXACT +
            (pending_index * sizeof(uint32_t)))] &=
            0x3fff3fffu;
        summary_flags_dirty = true;
      }
      candidate->result_flags = 0u;
    }
  }

  if ((current_summary != 0) && summary_flags_dirty) {
    current_summary->summary_flags =
        fzgx_compute_track_side_summary_flags_exact(current_summary);
  }
}

static void fzgx_stage_pending_side_rejection_frame_exact(
    const fzgx_track_segment_record *track_segment,
    const fzgx_mat43 *inverse_transform,
    float scale_x,
    fzgx_track_piece_solver_state_exact *state) {
  float max_lateral_bound;
  uint32_t rejection_frame_count;
  uint8_t *rejection_frame_raw;

  if ((track_segment == 0) || (inverse_transform == 0) || (state == 0)) {
    return;
  }
  if (fabsf(scale_x) <= FLT_EPSILON) {
    return;
  }

  rejection_frame_count = fzgx_collision_scratch_read_u32_current_exact(
      FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT);
  if (rejection_frame_count >= FZGX_TRACK_SIDE_REJECTION_FRAME_CAPACITY_EXACT) {
    return;
  }
  max_lateral_bound = track_segment->rail_height_right;
  if (max_lateral_bound < track_segment->rail_height_left) {
    max_lateral_bound = track_segment->rail_height_left;
  }
  rejection_frame_raw =
      g_fzgx_collision_scratch_raw_current_exact +
      FZGX_COLLISION_SCRATCH_REJECTION_FRAME_BANK_OFFSET_EXACT +
      (rejection_frame_count * 0x40u);
  memcpy(rejection_frame_raw, inverse_transform, sizeof(*inverse_transform));
  memcpy(rejection_frame_raw + 0x30u, &state->scale.x, sizeof(state->scale.x));
  memcpy(rejection_frame_raw + 0x34u, &state->scale.y, sizeof(state->scale.y));
  memcpy(rejection_frame_raw + 0x38u, &state->scale.z, sizeof(state->scale.z));
  memcpy(rejection_frame_raw + 0x3cu, &max_lateral_bound, sizeof(max_lateral_bound));
}

static void fzgx_publish_contact_slot_exact(
    fzgx_world_spherecast_result *result_inout,
    uint32_t slot_index,
    fzgx_vec3 push,
    uint32_t info_flags,
    uintptr_t summary_flags_ptr_exact,
    float hit_time) {
  if ((result_inout == 0) || (slot_index >= 4u)) {
    return;
  }

  if (!result_inout->contact_slots[slot_index].has_contact ||
      (hit_time < result_inout->contact_slots[slot_index].hit_time)) {
    result_inout->contact_slots[slot_index].has_contact = true;
    result_inout->contact_slots[slot_index].hit_time = hit_time;
    result_inout->contact_slots[slot_index].push = push;
    result_inout->contact_slots[slot_index].info_flags = info_flags;
    result_inout->contact_slots[slot_index].summary_flags_ptr_exact = summary_flags_ptr_exact;
  }
}

static fzgx_status fzgx_track_sweep_prepare_exact(
    fzgx_sim_world *world,
    const fzgx_world_spherecast_request *request,
    fzgx_track_side_query_buffer_exact *piece_scratch,
    fzgx_world_spherecast_result *result_out) {
  const fzgx_track_manifest *track_manifest;
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  const fzgx_world_spherecast_request *sweep_request = request;
  fzgx_world_spherecast_request request_storage;
  fzgx_track_sweep_query_exact prepared_query;
  fzgx_world_spherecast_request retry_request;
  const fzgx_track_side_query_summary *latest_summary = 0;
  fzgx_track_side_query_summary *write_summary = 0;
  const fzgx_track_node_record *track_node = 0;
  fzgx_track_piece_solver_state_exact solver_state;
  fzgx_track_segment_trs_curve_cache_exact *persistent_trs_curve_cache = 0;
  fzgx_world_spherecast_result result_storage;
  fzgx_world_spherecast_result *param_7 = result_out;
  int32_t checkpoint_ptr[4] = {-1, -1, -1, -1};
  fzgx_status status;
  uint32_t checkpoint_variant_count;
  uint32_t branch_selector;
  uint32_t player_idx;
  uint32_t seed_start_index;
  uint32_t seed_index;
  int32_t invalid_seed_count = 0;
  bool branch_filter_active;

  if ((world == 0) || (request == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (request->machine_index < world->machine_count) {
    persistent_trs_curve_cache = &world->machines[request->machine_index].track_query_filter_cache;
  }
  g_fzgx_collision_scratch_raw_current_exact = fzgx_collision_scratch_raw_exact(world);
  g_fzgx_collision_piece_scratch_current_exact = piece_scratch;

  if (param_7 == 0) {
    param_7 = &result_storage;
  }
  memset(param_7, 0, sizeof(*param_7));
  request_storage = *request;
  if (piece_scratch != 0) {
    fzgx_track_side_query_buffer_reset_write_record_exact(piece_scratch);
    request_storage.previous_side_summary_live_exact = (uintptr_t)0;
    request_storage.current_side_summary_live_exact = (uintptr_t)0;
    latest_summary = fzgx_track_side_query_buffer_get_record_const_exact(
        piece_scratch, piece_scratch->latest_record_offset);
    write_summary = fzgx_track_side_query_buffer_get_record_exact(
        piece_scratch, piece_scratch->write_record_offset);
    if (write_summary != 0) {
      int32_t *piVar10 = (int32_t *)write_summary;

      *piVar10 = 0;
      piVar10[1] = 0;
      piVar10[2] = 0;
    }
    if (latest_summary != 0) {
      request_storage.has_previous_side_summary = true;
      request_storage.previous_side_summary = *latest_summary;
      request_storage.previous_side_summary_live_exact = (uintptr_t)latest_summary;
      request_storage.has_previous_side_normal = true;
      request_storage.previous_side_normal =
          fzgx_get_track_side_query_summary_hit_normal_exact(latest_summary);
    }
    request_storage.current_side_summary_live_exact =
        (write_summary != 0)
            ? (uintptr_t)write_summary
            : (uintptr_t)0;
    sweep_request = &request_storage;
  }
  if ((sweep_request->flags & 0x5u) == 0u) {
    return FZGX_STATUS_OK;
  }

  track_manifest = fzgx_get_active_track_manifest(world);
  if (track_manifest == 0) {
    return FZGX_STATUS_UNIMPLEMENTED;
  }
  status = fzgx_get_active_track_course(world, &course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      world->content, world->active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = 0;
  } else if (status != FZGX_STATUS_OK) {
    return status;
  }

  memset(&solver_state, 0, sizeof(solver_state));
  solver_state.course = course;
  solver_state.animation_course = animation_course;
  solver_state.authored_track_id = track_manifest->authored_track_id;
  solver_state.debug_world_frame_index = world->frame_index;
  if (persistent_trs_curve_cache != 0) {
    solver_state.trs_curve_cache = *persistent_trs_curve_cache;
  }
  branch_filter_active =
      ((sweep_request->flags & 0x01800000u) != 0u) &&
      ((sweep_request->flags & 0xf8000000u) != 0u);
  if (branch_filter_active) {
    request_storage.flags =
        sweep_request->flags |
        (0x80000000u >> (((uint32_t)((int32_t)sweep_request->flags >> 31)) >> 31));
    sweep_request = &request_storage;
  }
  if (sweep_request->checkpoint_history_count != 0u) {
    uint32_t checkpoint_count = sweep_request->checkpoint_history_count;

    if (checkpoint_count > 4u) {
      checkpoint_count = 4u;
    }
    memcpy(checkpoint_ptr, sweep_request->checkpoint_history_index, sizeof(checkpoint_ptr[0]) * checkpoint_count);
  } else {
    checkpoint_ptr[0] = sweep_request->checkpoint_seed_index;
  }
  if (sweep_request->checkpoint_seed_aux != 0u) {
    player_idx = sweep_request->checkpoint_seed_aux;
  } else {
    player_idx = sweep_request->checkpoint_history_count;
    if (player_idx < 2u) {
      player_idx = 1u;
    }
  }
  if (player_idx > 4u) {
    player_idx = 4u;
  }
  if (player_idx < 2u) {
    seed_start_index = 0u;
    fzgx_collision_scratch_write_u32_exact(
        world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT, 1u);
  } else {
    seed_start_index = 1u;
    fzgx_collision_scratch_write_u32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT,
        player_idx - 1u);
  }
  for (seed_index = seed_start_index; seed_index < player_idx; ++seed_index) {
    int32_t checkpoint_seed_index = checkpoint_ptr[seed_index];
    fzgx_status solver_status;
    fzgx_status walk_status;

    if (branch_filter_active) {
      uint32_t rotate = (seed_index + 1u) & 31u;

      if ((((sweep_request->flags << rotate) |
            (sweep_request->flags >> (32u - rotate))) & 1u) == 0u) {
        checkpoint_seed_index = -1;
      }
    }

    if (checkpoint_seed_index < 0) {
      if (write_summary != 0) {
        int32_t *piVar10 = (int32_t *)write_summary;
        int32_t iVar15 = *piVar10;

        piVar10[iVar15 + 7] = 0;
        piVar10[iVar15 + 0xb] = 1;
        piVar10[iVar15 + 0xf] = 0;
        piVar10[iVar15 + 0x13] = -1;
        if (iVar15 < 3) {
          *piVar10 = iVar15 + 1;
        }
      }
      invalid_seed_count += 1;
      continue;
    }

    memset(&prepared_query, 0, sizeof(prepared_query));
    prepared_query.branch_selector = seed_index;
    prepared_query.seed_point = sweep_request->start;
    if ((sweep_request->flags & 0x00080000u) != 0u) {
      fzgx_ray_scale_exact(
          fzgx_generic_surface_push_bias_exact,
          &sweep_request->start,
          &sweep_request->end,
          &prepared_query.seed_point);
    } else if ((sweep_request->flags & 0x00040000u) != 0u) {
      prepared_query.seed_point = sweep_request->end;
    }

    if ((sweep_request->flags & 0x00020000u) == 0u) {
      status = fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
          course,
          track_manifest->authored_track_id,
          track_manifest->circuit_type,
          &prepared_query.seed_point,
          checkpoint_seed_index,
          &prepared_query.branch_selector,
          0u,
          &prepared_query.checkpoint_index,
          &prepared_query.checkpoint_fraction);
    } else {
      uint32_t fallback_branch_selector = prepared_query.branch_selector;

      status = fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
          course,
          track_manifest->authored_track_id,
          track_manifest->circuit_type,
          &prepared_query.seed_point,
          checkpoint_seed_index,
          &fallback_branch_selector,
          1u,
          &prepared_query.checkpoint_index,
          &prepared_query.checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        status = fzgx_track_course_compute_checkpoint_t_for_point(
            course,
            track_manifest->authored_track_id,
            &prepared_query.seed_point,
            checkpoint_seed_index,
            prepared_query.branch_selector,
            &prepared_query.checkpoint_fraction);
        if (status == FZGX_STATUS_OK) {
          prepared_query.checkpoint_index = checkpoint_seed_index;
        }
      } else {
        prepared_query.branch_selector = fallback_branch_selector;
      }
    }
    if (status != FZGX_STATUS_OK) {
      if (write_summary != 0) {
        int32_t *piVar10 = (int32_t *)write_summary;
        int32_t iVar15 = *piVar10;

        piVar10[iVar15 + 7] = 0;
        piVar10[iVar15 + 0xb] = 1;
        piVar10[iVar15 + 0xf] = 0;
        piVar10[iVar15 + 0x13] = -1;
        if (iVar15 < 3) {
          *piVar10 = iVar15 + 1;
        }
      }
      invalid_seed_count += 1;
      continue;
    }
    status = fzgx_track_course_get_track_node(
        course, (uint32_t)prepared_query.checkpoint_index, &track_node);
    if (status != FZGX_STATUS_OK) {
      continue;
    }
    checkpoint_variant_count = track_node->checkpoint_count;
    if (checkpoint_variant_count == 0u) {
      continue;
    }

    branch_selector = prepared_query.branch_selector;
    if ((branch_selector != 0u) && (checkpoint_variant_count <= branch_selector)) {
      branch_selector = checkpoint_variant_count - 1u;
    }
    prepared_query.branch_selector = branch_selector;

    status = fzgx_track_course_get_checkpoint_variant(
        course,
        (uint32_t)prepared_query.checkpoint_index,
        branch_selector,
        &prepared_query.checkpoint);
    if (status != FZGX_STATUS_OK) {
      continue;
    }
    status = fzgx_track_course_compute_curve_time_for_checkpoint_fraction(
        course,
        (uint32_t)prepared_query.checkpoint_index,
        prepared_query.checkpoint_fraction,
        &prepared_query.curve_time);
    if (status != FZGX_STATUS_OK) {
      continue;
    }
    status = fzgx_track_course_get_root_segment_for_track_node(
        course,
        (uint32_t)prepared_query.checkpoint_index,
        &prepared_query.root_segment);
    if (status != FZGX_STATUS_OK) {
      continue;
    }
    status = fzgx_track_segment_build_source_piece_word(
        prepared_query.root_segment, &prepared_query.source_piece_word);
    if (status != FZGX_STATUS_OK) {
      continue;
    }
    if (!fzgx_track_piece_matches_branch_slot_exact(prepared_query.root_segment, branch_selector) ||
        ((prepared_query.source_piece_word & 2u) != 0u)) {
      continue;
    }
    prepared_query.continuity_gate = 0u;
    if ((prepared_query.checkpoint->connect_to_track_in != 0u) &&
        (prepared_query.checkpoint->connect_to_track_out != 0u)) {
      prepared_query.continuity_gate = 1u;
      if ((piece_scratch != 0) &&
          sweep_request->has_previous_side_summary &&
          (fzgx_collision_scratch_read_u32_exact(
               world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT) ==
           sweep_request->previous_side_summary.candidate_count)) {
        if (track_manifest->authored_track_id == 0x18u) {
          if ((((1 < prepared_query.checkpoint_index) &&
                (prepared_query.checkpoint_index < 0x1d)) ||
               ((0x1e < prepared_query.checkpoint_index) &&
                (prepared_query.checkpoint_index < 0x34)) ||
               ((0x39 < prepared_query.checkpoint_index) &&
                (prepared_query.checkpoint_index < 0xa4)) ||
               ((0xa6 < prepared_query.checkpoint_index) &&
                (prepared_query.checkpoint_index < 0xb8)))) {
            prepared_query.continuity_gate = 0u;
          }
        } else if (track_manifest->authored_track_id != 0x08u) {
          prepared_query.continuity_gate = 0u;
        }
      }
    }
    fzgx_collision_scratch_write_u32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_QUERY_BRANCH_SELECTOR_OFFSET_EXACT,
        prepared_query.branch_selector);
    fzgx_collision_scratch_write_s32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_QUERY_CHECKPOINT_INDEX_OFFSET_EXACT,
        prepared_query.checkpoint_index);
    fzgx_collision_scratch_write_f32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_QUERY_CHECKPOINT_FRACTION_OFFSET_EXACT,
        prepared_query.checkpoint_fraction);
    fzgx_collision_scratch_write_u32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_QUERY_CONTINUITY_GATE_OFFSET_EXACT,
        prepared_query.continuity_gate);

    solver_state.transform = fzgx_mat43_identity_exact();
    solver_state.scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
    fzgx_collision_scratch_write_f32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT, 2.0f);
    fzgx_collision_scratch_write_f32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT, 0.0f);
    solver_state.summary_seed_slot_count =
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT);
    solver_status = fzgx_dispatch_track_piece_solver_exact(
        sweep_request,
        &prepared_query,
        prepared_query.root_segment,
        prepared_query.source_piece_word,
        sweep_request->flags,
        &solver_state,
        param_7);
    walk_status = fzgx_walk_track_collision_piece_tree_exact(
        sweep_request,
        &prepared_query,
        prepared_query.root_segment,
        prepared_query.branch_selector,
        sweep_request->flags,
        &solver_state,
        param_7);
    fzgx_collision_scratch_write_u32_exact(
        world,
        FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT,
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT) +
            1u);
    if ((solver_status == FZGX_STATUS_OK) || (walk_status == FZGX_STATUS_OK)) {
      param_7->selected_cached_frame_index = (int32_t)prepared_query.branch_selector;
      param_7->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
    }
  }
  fzgx_collision_scratch_write_u32_exact(
      world,
      FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT,
      fzgx_collision_scratch_read_u32_exact(
          world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT) +
          (uint32_t)invalid_seed_count);
  if (write_summary != 0) {
    fzgx_apply_track_side_summary_special_postprocess_exact(write_summary);
  }
  if ((write_summary != 0) && branch_filter_active &&
      ((write_summary->summary_flags & 0xf0000000u) != 0xf0000000u)) {
    retry_request = *sweep_request;
    retry_request.flags = sweep_request->flags & 0x07ffffffu;
    if (persistent_trs_curve_cache != 0) {
      *persistent_trs_curve_cache = solver_state.trs_curve_cache;
    }
    return fzgx_track_sweep_prepare_exact(world, &retry_request, piece_scratch, result_out);
  }
  {
    fzgx_track_side_query_summary *current_summary = write_summary;
    const fzgx_track_pending_side_contact_exact *best_candidate = 0;
    uint32_t pending_index;
    bool summary_flags_dirty = false;

    for (pending_index = 0u;
         pending_index <
         fzgx_collision_scratch_read_u32_current_exact(FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT);
         ++pending_index) {
      const fzgx_track_pending_side_contact_exact *candidate =
          (const fzgx_track_pending_side_contact_exact *)(const void
                                                              *)(g_fzgx_collision_scratch_raw_current_exact +
                                                                 FZGX_COLLISION_SCRATCH_PENDING_SIDE_BANK_OFFSET_EXACT +
                                                                 (pending_index * sizeof(*candidate)));

      if (candidate->result_flags == 0u) {
        continue;
      }
      if ((best_candidate == 0) || (candidate->hit_time < best_candidate->hit_time)) {
        best_candidate = candidate;
      }
    }

    if (best_candidate != 0) {
      for (pending_index = 0u;
           pending_index <
           fzgx_collision_scratch_read_u32_current_exact(
               FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT);
           ++pending_index) {
        const fzgx_track_pending_side_contact_exact *candidate =
            (const fzgx_track_pending_side_contact_exact *)(const void
                                                                *)(g_fzgx_collision_scratch_raw_current_exact +
                                                                   FZGX_COLLISION_SCRATCH_PENDING_SIDE_BANK_OFFSET_EXACT +
                                                                   (pending_index * sizeof(*candidate)));

        if ((candidate->result_flags == 0u) || (candidate == best_candidate)) {
          continue;
        }
        if ((current_summary != 0) &&
            (fzgx_collision_scratch_read_u32_current_exact(
                 FZGX_COLLISION_SCRATCH_PENDING_SIDE_SUMMARY_SLOT_MAP_OFFSET_EXACT +
                 (pending_index * sizeof(uint32_t))) < 4u)) {
          current_summary
              ->flags[fzgx_collision_scratch_read_u32_current_exact(
                  FZGX_COLLISION_SCRATCH_PENDING_SIDE_SUMMARY_SLOT_MAP_OFFSET_EXACT +
                  (pending_index * sizeof(uint32_t)))] &=
              0x0fffffffu;
          summary_flags_dirty = true;
        }
      }
      if ((current_summary != 0) && summary_flags_dirty) {
        current_summary->summary_flags =
            fzgx_compute_track_side_summary_flags_exact(current_summary);
      }
      fzgx_collision_scratch_current_write_track_hit_slot_record_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_SLOT_OFFSET_EXACT, best_candidate);
      param_7->checkpoint_index = best_candidate->checkpoint_index;
      param_7->checkpoint_fraction = best_candidate->checkpoint_fraction;
      param_7->selected_cached_frame_index = best_candidate->branch_selector;
      fzgx_publish_contact_slot_exact(
          param_7,
          0u,
          best_candidate->push,
          best_candidate->result_flags,
          fzgx_track_side_query_summary_slot_flags_ptr_from_opaque_exact(
              piece_scratch, best_candidate->summary_flags_ptr_opaque),
          best_candidate->hit_time);
    }
  }

  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_MATERIAL_FLAGS_OFFSET_EXACT, param_7->material_flags);
  fzgx_collision_scratch_write_u32_exact(
      world, FZGX_COLLISION_SCRATCH_HIT_INFO_FLAGS_OFFSET_EXACT, param_7->hit_info_flags);
  fzgx_collision_scratch_write_vec3_exact(
      world, FZGX_COLLISION_SCRATCH_HIT_NORMAL_OFFSET_EXACT, param_7->hit_normal);
  param_7->cached_frame_count = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT);
  if (param_7->cached_frame_count > FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY) {
    param_7->cached_frame_count = FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY;
  }
  param_7->has_cached_frame_exports = param_7->cached_frame_count != 0u;
  if (param_7->has_cached_frame_exports) {
    const uint8_t *scratch_raw = fzgx_collision_scratch_raw_const_exact(world);

    if (scratch_raw != 0) {
      memcpy(
          param_7->cached_frames,
          scratch_raw + FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT,
          sizeof(fzgx_track_frame_record) * param_7->cached_frame_count);
    }
  }
  if ((sweep_request->flags & 0x5u) == 0x5u) {
    if ((fzgx_collision_scratch_read_u32_current_exact(
             FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT) != 0u) ||
        (fzgx_collision_scratch_read_u32_current_exact(
             FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT) != 0u)) {
      size_t selected_slot_offset = FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT;
      size_t scan_slot_offset = selected_slot_offset;
      uint32_t slot_state = 0u;
      int32_t scan_count;

      if (fzgx_collision_scratch_read_u32_current_exact(
              FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT) == 0u) {
        slot_state = 1u;
        selected_slot_offset = FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_SLOT_OFFSET_EXACT;
        scan_slot_offset = selected_slot_offset;
        if (fzgx_collision_scratch_read_u32_current_exact(
                FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT) == 0u) {
          slot_state = 2u;
        }
      }
      scan_count = 2 - (int32_t)slot_state;
      if (slot_state < 2u) {
        while (scan_count != 0) {
          if ((fzgx_collision_scratch_read_u32_current_exact(scan_slot_offset + 0x0cu) != 0u) &&
              (fzgx_collision_scratch_read_f32_current_exact(
                   scan_slot_offset + FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT) <
               fzgx_collision_scratch_read_f32_current_exact(
                   selected_slot_offset + FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT))) {
            selected_slot_offset = scan_slot_offset;
          }
          scan_slot_offset += sizeof(fzgx_track_pending_side_contact_exact);
          scan_count -= 1;
        }
        fzgx_collision_scratch_copy_current_exact(
            FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT,
            selected_slot_offset,
            sizeof(fzgx_track_pending_side_contact_exact));
      }
    }
  } else if ((((sweep_request->flags & 0x1u) == 0u) ||
              (fzgx_collision_scratch_read_u32_current_exact(
                   FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT) == 0u))) {
    if (((sweep_request->flags & 0x4u) != 0u) &&
        (fzgx_collision_scratch_read_u32_current_exact(
             FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT) != 0u)) {
      fzgx_collision_scratch_copy_current_exact(
          FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT,
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_SLOT_OFFSET_EXACT,
          sizeof(fzgx_track_pending_side_contact_exact));
    }
  } else {
    fzgx_collision_scratch_copy_current_exact(
        FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
        sizeof(fzgx_track_pending_side_contact_exact));
  }
  if (fzgx_collision_scratch_read_u32_current_exact(
          FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT) != 0u) {
    param_7->has_hit = true;
    param_7->selected_cached_frame_index = fzgx_collision_scratch_read_s32_exact(
        world, FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT + 0x0u);
    param_7->checkpoint_index = fzgx_collision_scratch_read_s32_exact(
        world, FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT + 0x4u);
    param_7->checkpoint_fraction = fzgx_collision_scratch_read_f32_exact(
        world, FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT + 0x8u);
    param_7->result_flags = fzgx_collision_scratch_read_u32_current_exact(
        FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT);
    param_7->surface_flags = param_7->result_flags;
    param_7->hit_time = fzgx_collision_scratch_read_f32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
            FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT);
    param_7->hit_point = fzgx_collision_scratch_read_vec3_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
            FZGX_COLLISION_SCRATCH_HIT_SLOT_POINT_OFFSET_EXACT);
    param_7->hit_normal = fzgx_collision_scratch_read_vec3_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
            FZGX_COLLISION_SCRATCH_HIT_SLOT_NORMAL_OFFSET_EXACT);
    param_7->aux_hit_point = fzgx_collision_scratch_read_vec3_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
            FZGX_COLLISION_SCRATCH_HIT_SLOT_AUX_POINT_OFFSET_EXACT);
    param_7->branch_flags = fzgx_collision_scratch_get_selected_frame_flags_exact(world);
    fzgx_collision_scratch_write_vec3_exact(
        world, FZGX_COLLISION_SCRATCH_HIT_NORMAL_OFFSET_EXACT, param_7->hit_normal);
  }
  if (persistent_trs_curve_cache != 0) {
    *persistent_trs_curve_cache = solver_state.trs_curve_cache;
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_apply_track_collision_node_transform_exact(
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  const fzgx_track_segment_animation_record *animation_segment = 0;
  const fzgx_track_segment_record *children = 0;
  const fzgx_track_segment_record *child_segment = 0;
  const fzgx_track_segment_animation_record *child_animation_segment = 0;
  fzgx_track_frame_record *cached_frame = 0;
  uint32_t source_piece_word;
  uint32_t branch_selector = 0u;
  uint32_t cached_frame_cursor = 0u;
  uint32_t child_count = 0u;
  fzgx_status status;
  float curve_position_x = 0.0f;
  float curve_scale_y = 0.0f;
  float curve_width = 0.0f;
  float child_scale_y = 0.0f;

  (void)result_inout;
  if ((query == 0) || (track_segment == 0) || (state == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (g_fzgx_collision_scratch_raw_current_exact == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((dispatch_flags & 0x00800000u) != 0u) {
    cached_frame_cursor = fzgx_collision_scratch_read_u32_current_exact(
        FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT);
    if (cached_frame_cursor < FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY) {
      cached_frame =
          &((fzgx_track_frame_record *)(void *)(g_fzgx_collision_scratch_raw_current_exact +
                                                FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT))
               [cached_frame_cursor];
    }
  }

  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  fzgx_collision_scratch_write_u32_current_exact(
      FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT,
      fzgx_collision_scratch_read_u32_current_exact(FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT) |
          (source_piece_word & 0x07e00c01u));
  branch_selector = fzgx_collision_scratch_read_u32_current_exact(
      FZGX_COLLISION_SCRATCH_TRACK_QUERY_BRANCH_SELECTOR_OFFSET_EXACT);
  if (((source_piece_word & 0x04000000u) != 0u) &&
      (((branch_selector == 1u) ||
        ((1u < branch_selector) && (cached_frame_cursor == 0u))) &&
       (cached_frame_cursor < 3u))) {
    cached_frame_cursor += 1u;
    fzgx_collision_scratch_write_u32_current_exact(
        FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT, cached_frame_cursor);
    if (cached_frame != 0) {
      cached_frame =
          &((fzgx_track_frame_record *)(void *)(g_fzgx_collision_scratch_raw_current_exact +
                                                FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT))
               [cached_frame_cursor];
      cached_frame->track_flags = 0u;
    }
  }
  if (state->animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        state->animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if ((source_piece_word & 0x00600000u) == 0u) {
    status = fzgx_track_segment_apply_trs(
        track_segment,
        animation_segment,
        query->curve_time,
        &state->transform,
        &state->scale,
        &state->trs_curve_cache);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if ((source_piece_word & 0x00400000u) != 0u) {
    curve_position_x = track_segment->fallback_position.x;
    if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
        (animation_segment->animation_curve_trs->curves != 0) &&
        (animation_segment->animation_curve_trs->curve_count >
         FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X_EXACT)) {
      const fzgx_animation_curve *curve =
          &animation_segment->animation_curve_trs
               ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X_EXACT];

      if ((curve->keyable_count != 0u) && (curve->keyables != 0)) {
        status = fzgx_evaluate_float_animation_curve(curve, query->curve_time, &curve_position_x);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }
    curve_position_x = 2.0f * curve_position_x;
    curve_scale_y = track_segment->fallback_scale.y;
    if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
        (animation_segment->animation_curve_trs->curves != 0) &&
        (animation_segment->animation_curve_trs->curve_count >
         FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT)) {
      const fzgx_animation_curve *curve =
          &animation_segment->animation_curve_trs
               ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT];

      if ((curve->keyable_count != 0u) && (curve->keyables != 0)) {
        status = fzgx_evaluate_float_animation_curve(curve, query->curve_time, &curve_scale_y);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }
    curve_width = state->scale.x * (curve_position_x + curve_scale_y);
    fzgx_collision_scratch_write_f32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT, fabsf(curve_position_x * state->scale.x));
    fzgx_collision_scratch_write_f32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT, fabsf(curve_scale_y * state->scale.y));
  }

  if (cached_frame != 0) {
    fzgx_mat43 inverse_transform;

    cached_frame->track_current_transform = state->transform;
    cached_frame->track_current_scale = state->scale;
    cached_frame->track_anchor = fzgx_mat43_get_origin_exact(&state->transform);
    inverse_transform = state->transform;
    fzgx_mat43_rigid_invert_exact(&inverse_transform);
    cached_frame->track_forward = (fzgx_vec3){
        -inverse_transform.basis_x_z,
        -inverse_transform.basis_y_z,
        -inverse_transform.basis_z_z,
    };
    cached_frame->track_up = (fzgx_vec3){
        inverse_transform.basis_x_y,
        inverse_transform.basis_y_y,
        inverse_transform.basis_z_y,
    };
    if ((source_piece_word & 0x00400000u) != 0u) {
      cached_frame->track_scl_x = fzgx_collision_scratch_read_f32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT);
      cached_frame->track_scl_y = fzgx_collision_scratch_read_f32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT);
    }
    cached_frame->track_flags |= source_piece_word & 0x07e00c01u;

    if (((source_piece_word & 0x00800000u) != 0u) && (track_segment->children_count == 1u)) {
      cached_frame->track_width_or_radius = state->scale.x;
      status = fzgx_track_course_get_track_segment_children(
          state->course, track_segment, &children, &child_count);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if (child_count == 1u) {
        child_segment = &children[0];
        child_scale_y = child_segment->fallback_scale.y;
        if (state->animation_course != 0) {
          status = fzgx_track_course_animation_find_track_segment_by_address(
              state->animation_course, child_segment->address, &child_animation_segment);
          if (status == FZGX_STATUS_OUT_OF_RANGE) {
            child_animation_segment = 0;
          } else if (status != FZGX_STATUS_OK) {
            return status;
          }
        }
        if ((child_animation_segment != 0) &&
            (child_animation_segment->animation_curve_trs != 0) &&
            (child_animation_segment->animation_curve_trs->curves != 0) &&
            (child_animation_segment->animation_curve_trs->curve_count >
             FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT)) {
          const fzgx_animation_curve *curve =
              &child_animation_segment->animation_curve_trs
                   ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT];

          if ((curve->keyable_count != 0u) && (curve->keyables != 0)) {
            status = fzgx_evaluate_float_animation_curve(curve, query->curve_time, &child_scale_y);
            if (status != FZGX_STATUS_OK) {
              return status;
            }
          }
        }
        cached_frame->track_hcylin = child_scale_y;
      }
    } else if ((source_piece_word & 0x00200000u) == 0u) {
      cached_frame->track_width_or_radius =
          ((source_piece_word & 0x00400000u) == 0u) ? state->scale.x : curve_width;
      cached_frame->track_hcylin = 1.0f;
    }

    if ((source_piece_word & 0x00400000u) == 0u) {
      if ((source_piece_word & 0x01800000u) == 0u) {
        cached_frame->track_follow_offset = cached_frame->track_anchor;
      } else {
        cached_frame->track_follow_offset = fzgx_vec3_add(
            fzgx_mat43_get_origin_exact(&state->transform),
            fzgx_vec3_scale(cached_frame->track_up, 0.5f * state->scale.y));
      }
    } else {
      cached_frame->track_follow_offset = fzgx_vec3_add(
          fzgx_mat43_get_origin_exact(&state->transform),
          fzgx_vec3_scale(
              cached_frame->track_up,
              0.5f *
                  fzgx_collision_scratch_read_f32_current_exact(
                      FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT)));
    }
  }
  return FZGX_STATUS_OK;
}

static void fzgx_commit_pending_rejection_frame_exact(
    fzgx_track_piece_solver_state_exact *state) {
  (void)state;
  if (fzgx_collision_scratch_read_u32_current_exact(FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT) <
      FZGX_TRACK_SIDE_REJECTION_FRAME_CAPACITY_EXACT) {
    fzgx_collision_scratch_write_u32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT,
        fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT) +
            1u);
  }
}

/* Mirrors the raw PTR_FUN_80316778 piece-family dispatch table. */
static fzgx_status fzgx_dispatch_track_piece_solver_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t source_piece_word,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  uint32_t solver_index = fzgx_count_leading_zeros_exact(source_piece_word & 0x03fe0000u);

  switch (solver_index) {
    case 6u:
      return fzgx_resolve_track_side_contact_candidates_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    case 7u:
      return fzgx_resolve_track_pipe_cylinder_contact_candidates_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    case 8u:
      return fzgx_resolve_track_open_pipe_cylinder_contact_candidates_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    case 9u:
      return fzgx_resolve_track_capsule_pipe_contact_candidates_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    case 10u:
      return fzgx_resolve_track_modulated_road_contact_candidates_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    case 11u:
    case 12u:
    case 13u:
    case 14u:
      return fzgx_accumulate_road_embedded_surface_flags_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    case 32u:
      return fzgx_apply_track_collision_transform_only_exact(
          request, query, track_segment, dispatch_flags, state, result_inout);
    default:
      return FZGX_STATUS_UNIMPLEMENTED;
  }
}

static fzgx_status fzgx_apply_track_collision_transform_only_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  (void)request;
  return fzgx_apply_track_collision_node_transform_exact(
      query, track_segment, dispatch_flags, state, result_inout);
}

static fzgx_status fzgx_accumulate_road_embedded_surface_flags_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  const fzgx_track_segment_animation_record *animation_segment = 0;
  uint32_t source_piece_word = 0u;
  float curve_time_min = 1000000000.0f;
  float curve_time_max = 0.0f;
  bool has_curve_window = false;
  fzgx_status status;
  uint32_t curve_index;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((dispatch_flags & 0x00200000u) == 0u) {
    return FZGX_STATUS_OK;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (state->animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        state->animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
      (animation_segment->animation_curve_trs->curves != 0)) {
    for (curve_index = 0u; curve_index < 9u; ++curve_index) {
      const fzgx_animation_curve *curve;
      const fzgx_keyable_attribute *first_keyable;
      const fzgx_keyable_attribute *last_keyable;

      if (animation_segment->animation_curve_trs->curve_count <= curve_index) {
        continue;
      }
      curve = &animation_segment->animation_curve_trs->curves[curve_index];
      if ((curve->keyable_count == 0u) || (curve->keyables == 0)) {
        continue;
      }
      first_keyable = &curve->keyables[0];
      if (first_keyable->time < curve_time_min) {
        curve_time_min = first_keyable->time;
      }
      last_keyable = &curve->keyables[curve->keyable_count - 1u];
      if (curve_time_max < last_keyable->time) {
        curve_time_max = last_keyable->time;
      }
      has_curve_window = true;
    }
  }
  if (has_curve_window && !((curve_time_min < query->curve_time) && (query->curve_time < curve_time_max))) {
    return FZGX_STATUS_OK;
  }
  status = fzgx_track_segment_apply_trs(
      track_segment,
      animation_segment,
      query->curve_time,
      &state->transform,
      &state->scale,
      &state->trs_curve_cache);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  {
    fzgx_mat43 inverse_transform;
    fzgx_vec3 local_point;
    float lateral_half_extent = 2.5f + (0.5f * fabsf(state->scale.x));

    inverse_transform = state->transform;
    fzgx_mat43_rigid_invert_exact(&inverse_transform);
    local_point = fzgx_transform_local_point(&inverse_transform, request->end);
    if ((-5.0f < local_point.y) && (local_point.y < 10.0f) &&
        (-lateral_half_extent < local_point.x) && (local_point.x < lateral_half_extent)) {
      fzgx_collision_scratch_or_u32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X1D4_EXACT, source_piece_word & 0x001e0000u);
      fzgx_collision_scratch_or_u32_current_exact(
          FZGX_COLLISION_SCRATCH_MATERIAL_FLAGS_OFFSET_EXACT, source_piece_word & 0x001e0000u);
      if (result_inout != 0) {
        result_inout->material_flags |= source_piece_word & 0x001e0000u;
      }
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_resolve_track_side_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  uint32_t source_piece_word = 0u;
  fzgx_track_side_query_summary *current_summary = 0;
  const fzgx_track_side_query_summary *previous_summary = 0;
  const fzgx_track_corner_record *track_corner = 0;
  fzgx_mat43 inverse_transform;
  fzgx_vec3 local_start;
  fzgx_vec3 local_end;
  float local_start_x_norm;
  float local_end_x_norm;
  float lateral_bound = fzgx_track_road_lateral_max_exact;
  float surface_t = 0.0f;
  float local_hit_x_norm;
  float denominator;
  uint32_t summary_activity_mask = 0xe0000000u;
  uint32_t surface_summary_flags = 0u;
  uint32_t surface_candidate_flags = 0u;
  uint32_t current_summary_slot = 0u;
  uint32_t summary_side_candidate_mask = 0u;
  uint32_t previous_slot_flags = 0u;
  uint32_t enable_side_0x400;
  uint32_t enable_side_0x800;
  bool start_below_surface;
  bool previous_surface_match = false;
  bool previous_alternate_match = false;
  bool previous_surface_piece_match = false;
  bool previous_alternate_piece_match = false;
  uint32_t previous_surface_can_traverse = 0u;
  uint32_t previous_alternate_can_traverse = 0u;
  bool side_accept = false;
  bool accepted_alternate_surface_hit = false;
  fzgx_track_transient_hit_exact primary_hit = {0};
  fzgx_track_transient_hit_exact alternate_hit = {0};
  fzgx_status status;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_apply_track_collision_node_transform_exact(
      query, track_segment, dispatch_flags, state, result_inout);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  current_summary = fzgx_request_current_side_summary_exact(request);
  previous_summary = fzgx_request_previous_side_summary_exact(request);
  enable_side_0x400 = dispatch_flags & 0x4u;
  enable_side_0x800 = enable_side_0x400;
  if (enable_side_0x400 != 0u) {
    if (current_summary != 0) {
      current_summary_slot = current_summary->candidate_count;
    }
    if (previous_summary != 0) {
      if ((previous_summary->shared_mask & 0x00008000u) != 0u) {
        enable_side_0x800 = 0u;
      }
      if ((previous_summary->shared_mask & 0x00004000u) != 0u) {
        enable_side_0x400 = 0u;
      }
    }
  }

  inverse_transform = state->transform;
  fzgx_mat43_rigid_invert_exact(&inverse_transform);
  fzgx_stage_pending_side_rejection_frame_exact(
      track_segment, &inverse_transform, state->scale.x, state);
  fzgx_reject_pending_side_candidates_against_current_piece_exact(
      track_segment, &inverse_transform, state->scale.x, state->scale.y, state, current_summary);
  local_start = fzgx_transform_local_point(&inverse_transform, request->start);
  local_end = fzgx_transform_local_point(&inverse_transform, request->end);
  if (fabsf(state->scale.x) <= FLT_EPSILON) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  local_start_x_norm = local_start.x / state->scale.x;
  local_end_x_norm = local_end.x / state->scale.x;
  if (((source_piece_word & 0x00000400u) == 0u) && ((source_piece_word & 0x00000800u) == 0u)) {
    lateral_bound +=
        fzgx_collision_scratch_read_f32_current_exact(
            FZGX_COLLISION_SCRATCH_MACHINE_TRACK_COLLISION_OFFSET_EXACT) /
        fabsf(state->scale.x);
    if (((local_end_x_norm < -lateral_bound) && (local_start_x_norm < -lateral_bound)) ||
        ((lateral_bound < local_end_x_norm) && (lateral_bound < local_start_x_norm))) {
      summary_activity_mask = 0u;
    }
    if ((1u < fzgx_collision_scratch_read_u32_current_exact(
                   FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT)) &&
        (fzgx_track_side_surface_summary_height_min_exact < local_start.y) &&
        (fzgx_track_side_surface_summary_height_min_exact < local_end.y)) {
      summary_activity_mask &= ~0x20000000u;
    }
  }
  if ((state->authored_track_id == 3u) &&
      (123 < query->checkpoint_index) && (query->checkpoint_index < 140) &&
      (fzgx_track_side_surface_summary_course3_height_min_exact < local_start.y) &&
      (fzgx_track_side_surface_summary_course3_height_min_exact < local_end.y)) {
    summary_activity_mask &= ~0x20000000u;
  }
  if (0.0f < local_start_x_norm) {
    enable_side_0x400 = 0u;
  }
  if (local_start_x_norm <= 0.0f) {
    enable_side_0x800 = 0u;
  }

  start_below_surface = local_start.y <= 0.0f;
  if (start_below_surface) {
    summary_activity_mask &= ~0x20000000u;
  }
  if ((previous_summary != 0) && (current_summary_slot < previous_summary->candidate_count)) {
    previous_slot_flags = previous_summary->flags[current_summary_slot];
  }
  if ((previous_summary != 0) && (state->course != 0)) {
    uint32_t flags;
    uint32_t can_traverse = 0u;
    uint32_t slot_index;

    flags = previous_slot_flags;
    if ((current_summary_slot < previous_summary->candidate_count) &&
        ((flags & 0x20000000u) != 0u)) {
      if ((flags & 0x02a00000u) != 0u) {
        flags |= 0x02a00000u;
      }
      previous_surface_piece_match =
          (previous_summary->piece_opaque[current_summary_slot] == (uint32_t)track_segment->address);
      if (!previous_surface_piece_match) {
        if (fzgx_track_course_can_traverse_checkpoint_interval(
                state->course,
                fzgx_track_summary_checkpoint_traverse_gap_exact,
                previous_summary->checkpoint_index[current_summary_slot],
                query->checkpoint_index,
                &can_traverse) == FZGX_STATUS_OK) {
          previous_surface_can_traverse = can_traverse;
        }
      }
      if (((source_piece_word & 0x03e00000u) == (source_piece_word & flags & 0x03e00000u)) &&
          (previous_surface_piece_match || (previous_surface_can_traverse != 0u))) {
        previous_surface_match = true;
      }
    } else if (query->continuity_gate != 0u) {
      for (slot_index = 0u; slot_index < previous_summary->candidate_count; ++slot_index) {
        flags = previous_summary->flags[slot_index];
        if ((flags & 0x20000000u) != 0u) {
          if ((flags & 0x02a00000u) != 0u) {
            flags |= 0x02a00000u;
          }
          if (((source_piece_word & 0x03e00000u) == (source_piece_word & flags & 0x03e00000u)) &&
              ((previous_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) ||
               ((fzgx_track_course_can_traverse_checkpoint_interval(
                     state->course,
                     fzgx_track_summary_checkpoint_traverse_gap_exact,
                     previous_summary->checkpoint_index[slot_index],
                     query->checkpoint_index,
                     &can_traverse) == FZGX_STATUS_OK) &&
                (can_traverse != 0u)))) {
            previous_surface_match = true;
          }
          break;
        }
      }
    }
  }

  if (((-lateral_bound <= local_end_x_norm) || (-lateral_bound <= local_start_x_norm)) &&
      ((local_end_x_norm <= lateral_bound) || (local_start_x_norm <= lateral_bound))) {
    denominator = local_end.y - local_start.y;
    if (fabsf(denominator) > FLT_EPSILON) {
      surface_t = local_end.y / denominator;
    } else {
      surface_t = 0.0f;
    }
    if (((local_end.y < 0.0f) || !start_below_surface) ||
        ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) == 0x7f800000u)) {
      if (local_end.y < local_start.y) {
        if ((previous_summary != 0) && !previous_surface_match &&
            (0.0f <= surface_t) && (surface_t <= 1.0f) &&
            ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) != 0x7f800000u)) {
          surface_candidate_flags = 0x02000000u;
        }
      } else if ((0.0f <= surface_t) && (surface_t <= 1.0f) &&
                 ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) != 0x7f800000u)) {
        surface_candidate_flags = 0x20000000u;
        surface_summary_flags = 0x20000000u;
      }
    } else {
      surface_candidate_flags = 0x20000000u;
      surface_summary_flags = 0x20000000u;
    }
  }

  if ((previous_summary != 0) && start_below_surface &&
      ((surface_candidate_flags & 0x20000000u) == 0u) &&
      previous_surface_match &&
      (fzgx_collision_scratch_read_u32_current_exact(
           FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT) == 0u)) {
    surface_candidate_flags = 0x20000000u;
    surface_summary_flags = 0x20000000u;
    surface_t = 0.0f;
  }

  if ((previous_summary != 0) && (surface_candidate_flags == 0u) && (state->authored_track_id != 0x08u)) {
    if (previous_surface_match || (0.0f < local_end.y)) {
      uint32_t flags = previous_slot_flags;
      uint32_t branch_mask = 0x03e00000u;
      uint32_t can_traverse = 0u;

        if ((current_summary != 0) &&
            (current_summary_slot < previous_summary->candidate_count) &&
            ((flags & 0x4u) != 0u)) {
        if (state->summary_seed_slot_count == previous_summary->candidate_count) {
          branch_mask = 0x03e00c00u;
        }
        previous_alternate_piece_match =
            (previous_summary->piece_opaque[current_summary_slot] == (uint32_t)track_segment->address);
        if (!previous_alternate_piece_match) {
          if (fzgx_track_course_can_traverse_checkpoint_interval(
                  state->course,
                  fzgx_track_summary_checkpoint_traverse_gap_exact,
                  previous_summary->checkpoint_index[current_summary_slot],
                  query->checkpoint_index,
                  &can_traverse) == FZGX_STATUS_OK) {
            previous_alternate_can_traverse = can_traverse;
          }
        }
        if (((source_piece_word & branch_mask) == (source_piece_word & flags & branch_mask)) &&
            (previous_alternate_piece_match || (previous_alternate_can_traverse != 0u))) {
          previous_alternate_match = true;
        }
      }
    } else {
      previous_alternate_match = true;
    }
    if (previous_alternate_match && !start_below_surface &&
        (fzgx_collision_scratch_read_u32_current_exact(
             FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT) == 0u)) {
      surface_candidate_flags = 0x02000000u;
      surface_t = 0.0f;
    }
  }

  if (surface_candidate_flags != 0u) {
    fzgx_track_transient_hit_exact *transient_hit = 0;
    fzgx_vec3 hit_normal = fzgx_mat43_get_basis_y_exact(&state->transform);
    fzgx_vec3 world_hit_point;
    float road_push_length;

    local_hit_x_norm = local_end_x_norm + surface_t * (local_start_x_norm - local_end_x_norm);
    side_accept = false;
    if (((surface_candidate_flags & 0x20000000u) != 0u) &&
        ((dispatch_flags & 0x02000000u) != 0u) &&
        previous_surface_match &&
        (((0.0f < local_hit_x_norm) &&
          ((source_piece_word & 0x00000800u) != 0u) &&
          ((previous_slot_flags & 0x40000000u) != 0u)) ||
         ((local_hit_x_norm <= 0.0f) &&
          ((source_piece_word & 0x00000400u) != 0u) &&
          ((previous_slot_flags & 0x80000000u) != 0u)))) {
      side_accept = true;
    }
    if (((surface_candidate_flags & 0x20000000u) != 0u) &&
        ((fabsf(local_hit_x_norm) <= lateral_bound) || side_accept)) {
      transient_hit = &primary_hit;
    } else if (((surface_candidate_flags & 0x02000000u) != 0u) &&
               (fabsf(local_hit_x_norm) <= lateral_bound)) {
      transient_hit = &alternate_hit;
      hit_normal = fzgx_vec3_scale(hit_normal, -1.0f);
      accepted_alternate_surface_hit = true;
    }
    if (transient_hit != 0) {
      road_push_length = -local_start.y;
      if (transient_hit == &primary_hit) {
        if (road_push_length < 0.0f) {
          road_push_length = 0.0f;
        }
      } else if (0.0f <= road_push_length) {
        road_push_length = 0.0f;
      }
      world_hit_point = fzgx_vec3_add(
          request->end, fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), surface_t));
      transient_hit->valid = true;
      transient_hit->hit_time = surface_t;
      transient_hit->hit_point = world_hit_point;
      transient_hit->aux_hit_point = world_hit_point;
      transient_hit->hit_normal = hit_normal;
      transient_hit->push = fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&state->transform), road_push_length);
      transient_hit->result_flags = 0x02000000u;
    } else if ((surface_candidate_flags & 0x20000000u) != 0u) {
      surface_candidate_flags = 0u;
      surface_summary_flags = 0u;
    }
  }
  if (fzgx_debug_trace_mcsg_side_solver_exact(request, query, state)) {
    fprintf(
        stderr,
        "sidedbg|frame=%u|seg=0x%08x|src=0x%08x|flags=0x%08x|cp=%d|cpf=%.6f|bs=%u|slot=%u|"
        "prev_count=%u|prev_shared=0x%08x|prev_flags=0x%08x|cont=%u|start=(%.3f,%.3f,%.3f)|"
        "end=(%.3f,%.3f,%.3f)|lsx=%.6f|lex=%.6f|lsy=%.6f|ley=%.6f|lat=%.6f|"
        "below=%u|prev_cp=%d|prev_piece=0x%08x|prev_surface=%u|prev_surface_piece=%u|"
        "prev_surface_traverse=%u|prev_alt=%u|prev_alt_piece=%u|prev_alt_traverse=%u|"
        "surf_flags=0x%08x|surf_sum=0x%08x|"
        "side_mask=0x%08x|activity=0x%08x|prim=%u|alt=%u\n",
        state->debug_world_frame_index,
        (unsigned)track_segment->address,
        source_piece_word,
        request->flags,
        query->checkpoint_index,
        query->checkpoint_fraction,
        query->branch_selector,
        current_summary_slot,
        (previous_summary != 0) ? previous_summary->candidate_count : 0u,
        (previous_summary != 0) ? previous_summary->shared_mask : 0u,
        previous_slot_flags,
        query->continuity_gate,
        request->start.x,
        request->start.y,
        request->start.z,
        request->end.x,
        request->end.y,
        request->end.z,
        local_start_x_norm,
        local_end_x_norm,
        local_start.y,
        local_end.y,
        lateral_bound,
        start_below_surface ? 1u : 0u,
        (previous_summary != 0 && current_summary_slot < previous_summary->candidate_count)
            ? previous_summary->checkpoint_index[current_summary_slot]
            : -1,
        (previous_summary != 0 && current_summary_slot < previous_summary->candidate_count)
            ? previous_summary->piece_opaque[current_summary_slot]
            : 0u,
        previous_surface_match ? 1u : 0u,
        previous_surface_piece_match ? 1u : 0u,
        previous_surface_can_traverse,
        previous_alternate_match ? 1u : 0u,
        previous_alternate_piece_match ? 1u : 0u,
        previous_alternate_can_traverse,
        surface_candidate_flags,
        surface_summary_flags,
        summary_side_candidate_mask,
        summary_activity_mask,
        primary_hit.valid ? 1u : 0u,
        alternate_hit.valid ? 1u : 0u);
  }
  if (primary_hit.valid) {
    if (fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
            FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
            primary_hit.hit_time)) {
      fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
          query,
          &primary_hit,
          fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
      if (result_inout != 0) {
        result_inout->checkpoint_index = query->checkpoint_index;
        result_inout->checkpoint_fraction = query->checkpoint_fraction;
        result_inout->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
        result_inout->selected_cached_frame_index = (int32_t)query->branch_selector;
      }
      if (fzgx_vec3_is_nonzero(primary_hit.push)) {
        fzgx_publish_contact_slot_exact(
            result_inout,
            1u,
            primary_hit.push,
            source_piece_word | 0x10000000u | 0x20000000u,
            fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot),
            primary_hit.hit_time);
      }
    }
  }
  if (alternate_hit.valid) {
    fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT,
        query,
        &alternate_hit,
        fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
  }

  if ((source_piece_word & 0x00000400u) != 0u) {
    fzgx_track_piece_solver_local_state_exact saved_state =
        fzgx_capture_track_piece_solver_local_state_exact(state);
    bool side_candidate_accepted = false;
    float side_local_start_x_norm = local_start_x_norm;
    float side_local_end_x_norm = local_end_x_norm;
    float side_local_start_y = local_start.y;
    float side_local_end_y = local_end.y;

    if (((source_piece_word & 0x00001000u) != 0u) && (state->course != 0) &&
        (track_segment->track_corner_address != 0u) &&
        (fzgx_track_course_find_track_corner_by_address(
             state->course, track_segment->track_corner_address, &track_corner) == FZGX_STATUS_OK)) {
      fzgx_mat43 corner_inverse;
      fzgx_vec3 corner_local_start;
      fzgx_vec3 corner_local_end;

      state->transform = track_corner->transform;
      state->scale.x = track_corner->width;
      corner_inverse = state->transform;
      fzgx_mat43_rigid_invert_exact(&corner_inverse);
      if (fabsf(state->scale.x) <= FLT_EPSILON) {
        fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      corner_local_start = fzgx_transform_local_point(&corner_inverse, request->start);
      corner_local_end = fzgx_transform_local_point(&corner_inverse, request->end);
      side_local_start_x_norm = corner_local_start.x / state->scale.x;
      side_local_end_x_norm = corner_local_end.x / state->scale.x;
      side_local_start_y = corner_local_start.y;
      side_local_end_y = corner_local_end.y;
    }
    if (side_local_start_x_norm <= fzgx_track_road_lateral_min_exact) {
      summary_activity_mask &= ~0x80000000u;
    }
    if (enable_side_0x400 != 0u) {
      status = fzgx_try_append_track_side_contact_candidate_exact(
          request,
          query,
          track_segment,
          state,
          current_summary,
          source_piece_word,
          side_local_start_x_norm,
          side_local_end_x_norm,
          side_local_start_y,
          side_local_end_y,
          +1,
          &summary_activity_mask,
          (surface_candidate_flags & 0x20000000u) == 0u,
          &side_candidate_accepted);
    } else {
      status = FZGX_STATUS_OK;
    }
    fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_UNIMPLEMENTED)) {
      return status;
    }
    if (side_candidate_accepted) {
      summary_side_candidate_mask |= 0x80000000u;
    }
  }

  if ((source_piece_word & 0x00000800u) != 0u) {
    fzgx_track_piece_solver_local_state_exact saved_state =
        fzgx_capture_track_piece_solver_local_state_exact(state);
    bool side_candidate_accepted = false;
    float side_local_start_x_norm = local_start_x_norm;
    float side_local_end_x_norm = local_end_x_norm;
    float side_local_start_y = local_start.y;
    float side_local_end_y = local_end.y;

    if (((source_piece_word & 0x00002000u) != 0u) && (state->course != 0) &&
        (track_segment->track_corner_address != 0u) &&
        (fzgx_track_course_find_track_corner_by_address(
             state->course, track_segment->track_corner_address, &track_corner) == FZGX_STATUS_OK)) {
      fzgx_mat43 corner_inverse;
      fzgx_vec3 corner_local_start;
      fzgx_vec3 corner_local_end;

      state->transform = track_corner->transform;
      state->scale.x = track_corner->width;
      corner_inverse = state->transform;
      fzgx_mat43_rigid_invert_exact(&corner_inverse);
      if (fabsf(state->scale.x) <= FLT_EPSILON) {
        fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      corner_local_start = fzgx_transform_local_point(&corner_inverse, request->start);
      corner_local_end = fzgx_transform_local_point(&corner_inverse, request->end);
      side_local_start_x_norm = corner_local_start.x / state->scale.x;
      side_local_end_x_norm = corner_local_end.x / state->scale.x;
      side_local_start_y = corner_local_start.y;
      side_local_end_y = corner_local_end.y;
    }
    if (fzgx_track_road_lateral_max_exact <= side_local_start_x_norm) {
      summary_activity_mask &= ~0x40000000u;
    }
    if (enable_side_0x800 != 0u) {
      status = fzgx_try_append_track_side_contact_candidate_exact(
          request,
          query,
          track_segment,
          state,
          current_summary,
          source_piece_word,
          side_local_start_x_norm,
          side_local_end_x_norm,
          side_local_start_y,
          side_local_end_y,
          -1,
          &summary_activity_mask,
          (surface_candidate_flags & 0x20000000u) == 0u,
          &side_candidate_accepted);
    } else {
      status = FZGX_STATUS_OK;
    }
    fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_UNIMPLEMENTED)) {
      return status;
    }
    if (side_candidate_accepted) {
      summary_side_candidate_mask |= 0x40000000u;
    }
  }

  fzgx_commit_pending_rejection_frame_exact(state);

  if (((dispatch_flags & 0x4u) != 0u) && (current_summary != 0)) {
    uint32_t slot = current_summary->candidate_count;
    uint32_t summary_flags = source_piece_word | (alternate_hit.valid ? 0x4u : 0u);
    uint32_t summary_shared_mask = 0u;
    if (accepted_alternate_surface_hit) {
      summary_activity_mask = 0u;
    }
    if ((summary_activity_mask != 0u) ||
        (surface_summary_flags != 0u) ||
        ((summary_side_candidate_mask & 0xfdffffffu) != 0u)) {
      summary_flags |=
          0x10000000u |
          summary_activity_mask |
          surface_summary_flags |
          (summary_side_candidate_mask & 0xfdffffffu);
    }
    if ((int32_t)summary_side_candidate_mask < 0) {
      summary_shared_mask |= 0x00008000u;
    } else if ((summary_side_candidate_mask & 0x40000000u) != 0u) {
      summary_shared_mask |= 0x00004000u;
    }
    if (summary_shared_mask != 0u) {
      summary_flags &= 0xffff3fffu;
      summary_flags |= summary_shared_mask;
    }
    if (slot > 3u) {
      slot = 3u;
    }
    current_summary->piece_opaque[slot] = track_segment->address;
    current_summary->width_scale[slot] = state->scale.x;
    current_summary->flags[slot] = summary_flags;
    current_summary->checkpoint_index[slot] = query->checkpoint_index;
    current_summary->summary_flags |= summary_flags;
    if (current_summary->candidate_count < 3u) {
      current_summary->candidate_count += 1u;
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_resolve_track_modulated_road_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  uint32_t source_piece_word = 0u;
  fzgx_track_side_query_summary *current_summary = 0;
  const fzgx_track_segment_animation_record *animation_segment = 0;
  const fzgx_track_corner_record *track_corner = 0;
  const fzgx_track_side_query_summary *previous_summary = 0;
  const fzgx_animation_curve *profile_curve = 0;
  const fzgx_keyable_attribute *first_profile_keyable = 0;
  const fzgx_keyable_attribute *last_profile_keyable = 0;
  fzgx_mat43 inverse_transform;
  fzgx_vec3 local_start;
  fzgx_vec3 local_end;
  float local_start_x_norm;
  float local_end_x_norm;
  float profile_sample_x_norm;
  float profile_height;
  float profile_curve_time;
  float scaled_profile_height;
  float surface_t = 0.0f;
  float local_hit_x_norm;
  float denominator;
  uint32_t current_summary_slot = 0u;
  uint32_t summary_side_candidate_mask = 0u;
  uint32_t surface_summary_flags = 0u;
  uint32_t surface_candidate_flags = 0u;
  uint32_t summary_activity_mask = 0xe0000000u;
  uint32_t previous_slot_flags = 0u;
  uint32_t enable_side_0x400 = dispatch_flags & 0x4u;
  uint32_t enable_side_0x800;
  bool start_below_surface;
  bool end_above_surface;
  bool previous_surface_match = false;
  bool previous_alternate_match = false;
  bool side_accept = false;
  bool accepted_alternate_surface_hit = false;
  fzgx_track_transient_hit_exact primary_hit = {0};
  fzgx_track_transient_hit_exact alternate_hit = {0};
  fzgx_status status;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_apply_track_collision_node_transform_exact(
      query, track_segment, dispatch_flags, state, result_inout);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  if (fzgx_track_id_is_sonic_oval_exact(state->authored_track_id)) {
    return fzgx_resolve_track_modulated_road_contact_candidates_sonic_oval_variant_exact(
        request, query, track_segment, dispatch_flags, state, result_inout);
  }

  previous_summary = fzgx_request_previous_side_summary_exact(request);
  current_summary = fzgx_request_current_side_summary_exact(request);
  if (current_summary != 0) {
    current_summary_slot = current_summary->candidate_count;
  }

  inverse_transform = state->transform;
  fzgx_mat43_rigid_invert_exact(&inverse_transform);
  fzgx_stage_pending_side_rejection_frame_exact(
      track_segment, &inverse_transform, state->scale.x, state);
  fzgx_reject_pending_side_candidates_against_current_piece_exact(
      track_segment, &inverse_transform, state->scale.x, state->scale.y, state, current_summary);
  local_start = fzgx_transform_local_point(&inverse_transform, request->start);
  local_end = fzgx_transform_local_point(&inverse_transform, request->end);
  if (fabsf(state->scale.x) <= FLT_EPSILON) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  local_start_x_norm = local_start.x / state->scale.x;
  local_end_x_norm = local_end.x / state->scale.x;
  if ((request->flags & 0x00080000u) != 0u) {
    profile_sample_x_norm = 0.5f * (local_end_x_norm + local_start_x_norm);
  } else {
    profile_sample_x_norm =
        ((request->flags & 0x00040000u) != 0u) ? local_end_x_norm : local_start_x_norm;
  }

  if (state->animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        state->animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
      (animation_segment->animation_curve_trs->curves != 0) &&
      (animation_segment->animation_curve_trs->curve_count >
       FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y_EXACT)) {
    profile_curve = &animation_segment->animation_curve_trs
                         ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y_EXACT];
  }
  if ((profile_curve == 0) || (profile_curve->keyables == 0) ||
      (profile_curve->keyable_count == 0u)) {
    profile_height = track_segment->fallback_position.y;
  } else {
    first_profile_keyable = &profile_curve->keyables[0];
    last_profile_keyable =
        &profile_curve->keyables[profile_curve->keyable_count - 1u];
    profile_curve_time =
        first_profile_keyable->time +
        ((0.5f + profile_sample_x_norm) *
         (last_profile_keyable->time - first_profile_keyable->time));
    if (profile_curve_time <= last_profile_keyable->time) {
      status = fzgx_evaluate_float_animation_curve(
          profile_curve, profile_curve_time, &profile_height);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    } else {
      profile_height =
          last_profile_keyable->value +
          (last_profile_keyable->tangent_out *
           (profile_curve_time - last_profile_keyable->time));
    }
  }
  scaled_profile_height = profile_height * state->scale.y;
  start_below_surface = local_start.y <= scaled_profile_height;
  end_above_surface = local_end.y > scaled_profile_height;
  if (start_below_surface) {
    summary_activity_mask &= ~0x20000000u;
  }
  enable_side_0x800 = enable_side_0x400;
  if (previous_summary != 0) {
    if (current_summary_slot < previous_summary->candidate_count) {
      previous_slot_flags = previous_summary->flags[current_summary_slot];
    }
    if ((previous_summary->shared_mask & 0x00008000u) != 0u) {
      enable_side_0x800 = 0u;
    }
    if ((previous_summary->shared_mask & 0x00004000u) != 0u) {
      enable_side_0x400 = 0u;
    }
  }
  if (0.0f < local_start_x_norm) {
    enable_side_0x400 = 0u;
  }
  if (local_start_x_norm <= 0.0f) {
    enable_side_0x800 = 0u;
  }
  if ((state->course != 0) && (previous_summary != 0)) {
    uint32_t branch_mask = 0x03e00000u;
    uint32_t can_traverse = 0u;
    uint32_t slot_index;

    if ((current_summary_slot < previous_summary->candidate_count) &&
        ((previous_summary->flags[current_summary_slot] & 0x20000000u) != 0u)) {
      uint32_t flags = previous_summary->flags[current_summary_slot];

      if ((flags & 0x02a00000u) != 0u) {
        flags |= 0x02a00000u;
      }
      if (state->summary_seed_slot_count == previous_summary->candidate_count) {
        branch_mask = 0x03e00c00u;
      }
      if (((source_piece_word & branch_mask) == (source_piece_word & branch_mask & flags)) &&
          ((previous_summary->piece_opaque[current_summary_slot] == (uint32_t)track_segment->address) ||
           ((fzgx_track_course_can_traverse_checkpoint_interval(
                 state->course,
                 fzgx_track_summary_checkpoint_traverse_gap_exact,
                 previous_summary->checkpoint_index[current_summary_slot],
                 query->checkpoint_index,
                 &can_traverse) == FZGX_STATUS_OK) &&
            (can_traverse != 0u)))) {
        previous_surface_match = true;
      }
    }

    if (!previous_surface_match &&
        (fzgx_collision_scratch_read_u32_current_exact(
             FZGX_COLLISION_SCRATCH_TRACK_QUERY_CONTINUITY_GATE_OFFSET_EXACT) != 0u)) {
      for (slot_index = 0u; slot_index < previous_summary->candidate_count; ++slot_index) {
        uint32_t flags = previous_summary->flags[slot_index];

        if ((flags & 0x20000000u) == 0u) {
          continue;
        }
        if ((flags & 0x02a00000u) != 0u) {
          flags |= 0x02a00000u;
        }
        if (state->summary_seed_slot_count == previous_summary->candidate_count) {
          branch_mask = 0x03e00c00u;
        } else {
          branch_mask = 0x03e00000u;
        }
        if ((source_piece_word & branch_mask) != (source_piece_word & branch_mask & flags)) {
          continue;
        }
        if (previous_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) {
          previous_surface_match = true;
          break;
        }
        if (fzgx_track_course_can_traverse_checkpoint_interval(
                state->course,
                fzgx_track_summary_checkpoint_traverse_gap_exact,
                previous_summary->checkpoint_index[slot_index],
                query->checkpoint_index,
                &can_traverse) != FZGX_STATUS_OK) {
          continue;
        }
        if (can_traverse != 0u) {
          previous_surface_match = true;
          break;
        }
      }
    }
  }
  if (((fzgx_track_road_lateral_min_exact <= local_end_x_norm) ||
       (fzgx_track_road_lateral_min_exact <= local_start_x_norm)) &&
      ((local_end_x_norm <= fzgx_track_road_lateral_max_exact) ||
       (local_start_x_norm <= fzgx_track_road_lateral_max_exact))) {
    denominator = local_end.y - local_start.y;
    if (fabsf(denominator) > FLT_EPSILON) {
      surface_t = (local_end.y - scaled_profile_height) / denominator;
    } else {
      surface_t = 0.0f;
    }
    if (((local_end.y < scaled_profile_height) || !start_below_surface) ||
        ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) == 0x7f800000u)) {
      if (local_end.y < local_start.y) {
        if ((previous_summary != 0) && !previous_surface_match &&
            (0.0f <= surface_t) && (surface_t <= 1.0f) &&
            ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) != 0x7f800000u)) {
          surface_candidate_flags = 0x02000000u;
        }
      } else if ((0.0f <= surface_t) && (surface_t <= 1.0f) &&
                 ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) != 0x7f800000u)) {
        surface_candidate_flags = 0x20000000u;
        surface_summary_flags = 0x20000000u;
      }
    } else {
      surface_candidate_flags = 0x20000000u;
      surface_summary_flags = 0x20000000u;
    }
  }

  if ((previous_summary != 0) && start_below_surface &&
      ((surface_candidate_flags & 0x20000000u) == 0u) &&
      (fzgx_collision_scratch_read_u32_current_exact(
           FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT) == 0u)) {
    if (previous_surface_match) {
      surface_candidate_flags = 0x20000000u;
      surface_summary_flags = 0x20000000u;
      surface_t = 0.0f;
    }
  }
  if ((previous_summary != 0) && (surface_candidate_flags == 0u)) {
    bool require_alternate_match = previous_surface_match || end_above_surface;

    if (require_alternate_match) {
      uint32_t slot_index = current_summary_slot;

      if ((state->course != 0) && (slot_index < previous_summary->candidate_count)) {
        uint32_t previous_flags = previous_summary->flags[slot_index];
        uint32_t branch_mask = 0x03e00000u;
        uint32_t can_traverse = 0u;

        if ((previous_flags & 0x4u) != 0u) {
          if (state->summary_seed_slot_count == previous_summary->candidate_count) {
            branch_mask = 0x03e00c00u;
          }
          if (((source_piece_word & branch_mask) ==
               (source_piece_word & branch_mask & previous_flags)) &&
              ((previous_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) ||
               ((fzgx_track_course_can_traverse_checkpoint_interval(
                     state->course,
                     fzgx_track_summary_checkpoint_traverse_gap_exact,
                     previous_summary->checkpoint_index[slot_index],
                     query->checkpoint_index,
                     &can_traverse) == FZGX_STATUS_OK) &&
                (can_traverse != 0u)))) {
            previous_alternate_match = true;
          }
        }
      }
      if (!previous_alternate_match) {
        goto finish_generic_modulated_surface_candidates;
      }
    }
    if (!start_below_surface) {
      if (fzgx_collision_scratch_read_u32_current_exact(
              FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT) != 0u) {
        goto finish_generic_modulated_surface_candidates;
      }
      surface_candidate_flags = 0x02000000u;
      surface_t = 0.0f;
    }
  }
finish_generic_modulated_surface_candidates:

  if (surface_candidate_flags != 0u) {
    fzgx_track_transient_hit_exact *transient_hit = 0;
    fzgx_vec3 hit_normal = fzgx_mat43_get_basis_y_exact(&state->transform);
    fzgx_vec3 world_hit_point;
    float road_push_length;

    local_hit_x_norm = local_end_x_norm + surface_t * (local_start_x_norm - local_end_x_norm);
    side_accept = false;
    if (((surface_candidate_flags & 0x20000000u) != 0u) &&
        ((dispatch_flags & 0x02000000u) != 0u) &&
        previous_surface_match &&
        (((0.0f < local_hit_x_norm) &&
          ((source_piece_word & 0x00000800u) != 0u) &&
          ((previous_slot_flags & 0x40000000u) != 0u)) ||
         ((local_hit_x_norm <= 0.0f) &&
          ((source_piece_word & 0x00000400u) != 0u) &&
          ((previous_slot_flags & 0x80000000u) != 0u)))) {
      side_accept = true;
    }
    if (((surface_candidate_flags & 0x20000000u) != 0u) &&
        ((fabsf(local_hit_x_norm) <= fzgx_track_modulated_surface_accept_x_exact) || side_accept)) {
      transient_hit = &primary_hit;
    } else if (((surface_candidate_flags & 0x02000000u) != 0u) &&
               (fabsf(local_hit_x_norm) <= fzgx_track_modulated_surface_accept_x_exact)) {
      transient_hit = &alternate_hit;
      hit_normal = fzgx_vec3_scale(hit_normal, -1.0f);
      accepted_alternate_surface_hit = true;
    }
    if (transient_hit != 0) {
      road_push_length = scaled_profile_height - local_start.y;
      if (transient_hit == &primary_hit) {
        if (road_push_length < 0.0f) {
          road_push_length = 0.0f;
        }
      } else if (0.0f <= road_push_length) {
        road_push_length = 0.0f;
      }
      world_hit_point = fzgx_vec3_add(
          request->end, fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), surface_t));
      transient_hit->valid = true;
      transient_hit->hit_time = surface_t;
      transient_hit->hit_point = world_hit_point;
      transient_hit->aux_hit_point = world_hit_point;
      transient_hit->hit_normal = hit_normal;
      transient_hit->push = fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&state->transform), road_push_length);
      transient_hit->result_flags = 0x00200000u;
    } else if ((surface_candidate_flags & 0x20000000u) != 0u) {
      surface_candidate_flags = 0u;
      surface_summary_flags = 0u;
    }
  }
  if ((state->authored_track_id == 0x15u) &&
      (state->debug_world_frame_index == 849u) &&
      (request->machine_index == 0u) &&
      (query->checkpoint_index == 48)) {
    fprintf(
        stderr,
        "roaddbg|frame=%u|seg=0x%08x|src=0x%08x|slot=%u|prev_count=%u|prev_flags=0x%08x|"
        "cont=%u|start=(%.3f,%.3f,%.3f)|end=(%.3f,%.3f,%.3f)|"
        "lsx=%.6f|lex=%.6f|lsy=%.6f|ley=%.6f|h=%.6f|below=%u|end_above=%u|"
        "prev_match=%u|surf_flags=0x%08x|surf_sum=0x%08x|prim=%u|alt=%u|"
        "candA=0x%08x|altA=0x%08x\n",
        state->debug_world_frame_index,
        (unsigned)track_segment->address,
        source_piece_word,
        current_summary_slot,
        (previous_summary != 0) ? previous_summary->candidate_count : 0u,
        previous_slot_flags,
        query->continuity_gate,
        request->start.x,
        request->start.y,
        request->start.z,
        request->end.x,
        request->end.y,
        request->end.z,
        local_start_x_norm,
        local_end_x_norm,
        local_start.y,
        local_end.y,
        scaled_profile_height,
        start_below_surface ? 1u : 0u,
        end_above_surface ? 1u : 0u,
        previous_surface_match ? 1u : 0u,
        surface_candidate_flags,
        surface_summary_flags,
        primary_hit.valid ? 1u : 0u,
        alternate_hit.valid ? 1u : 0u,
        fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT),
        fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT));
  }

  if (primary_hit.valid) {
    if (fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
            FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
            primary_hit.hit_time)) {
      fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
          query,
          &primary_hit,
          fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
      if (result_inout != 0) {
        result_inout->checkpoint_index = query->checkpoint_index;
        result_inout->checkpoint_fraction = query->checkpoint_fraction;
        result_inout->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
        result_inout->selected_cached_frame_index = (int32_t)query->branch_selector;
      }
      if (fzgx_vec3_is_nonzero(primary_hit.push)) {
        fzgx_publish_contact_slot_exact(
            result_inout,
            1u,
            primary_hit.push,
            source_piece_word | 0x10000000u | 0x20000000u,
            fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot),
            primary_hit.hit_time);
      }
    }
  }
  if (alternate_hit.valid) {
    fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT,
        query,
        &alternate_hit,
        fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
  }

  if (((source_piece_word & 0x00000400u) != 0u) && (enable_side_0x400 != 0u)) {
    fzgx_track_piece_solver_local_state_exact saved_state =
        fzgx_capture_track_piece_solver_local_state_exact(state);
    bool side_candidate_accepted = false;
    float side_profile_height = track_segment->fallback_position.y;
    float side_local_start_x_norm = local_start_x_norm;
    float side_local_end_x_norm = local_end_x_norm;
    float side_local_start_y;
    float side_local_end_y;
    float side_offset_y;

    if (first_profile_keyable != 0) {
      side_profile_height = first_profile_keyable->value;
    }
    side_offset_y = side_profile_height * state->scale.y;
    side_local_start_y = local_start.y - side_offset_y;
    side_local_end_y = local_end.y - side_offset_y;
    if (((source_piece_word & 0x00001000u) != 0u) && (state->course != 0) &&
        (track_segment->track_corner_address != 0u) &&
        (fzgx_track_course_find_track_corner_by_address(
             state->course, track_segment->track_corner_address, &track_corner) ==
         FZGX_STATUS_OK)) {
      fzgx_mat43 corner_inverse;
      fzgx_vec3 corner_local_start;
      fzgx_vec3 corner_local_end;

      state->transform = track_corner->transform;
      state->scale.x = track_corner->width;
      corner_inverse = state->transform;
      fzgx_mat43_rigid_invert_exact(&corner_inverse);
      if (fabsf(state->scale.x) <= FLT_EPSILON) {
        fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      corner_local_start = fzgx_transform_local_point(&corner_inverse, request->start);
      corner_local_end = fzgx_transform_local_point(&corner_inverse, request->end);
      side_local_start_x_norm = corner_local_start.x / state->scale.x;
      side_local_end_x_norm = corner_local_end.x / state->scale.x;
    }
    if (side_local_start_x_norm <= fzgx_track_road_lateral_min_exact) {
      summary_activity_mask &= ~0x80000000u;
    }
    status = fzgx_try_append_track_side_contact_candidate_exact(
        request,
        query,
        track_segment,
        state,
        current_summary,
        source_piece_word,
        side_local_start_x_norm,
        side_local_end_x_norm,
        side_local_start_y,
        side_local_end_y,
        +1,
        &summary_activity_mask,
        (surface_candidate_flags & 0x20000000u) == 0u,
        &side_candidate_accepted);
    fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_UNIMPLEMENTED)) {
      return status;
    }
    if (side_candidate_accepted) {
      summary_side_candidate_mask |= 0x80000000u;
    }
  }
  if (((source_piece_word & 0x00000800u) != 0u) && (enable_side_0x800 != 0u)) {
    fzgx_track_piece_solver_local_state_exact saved_state =
        fzgx_capture_track_piece_solver_local_state_exact(state);
    bool side_candidate_accepted = false;
    float side_profile_height = track_segment->fallback_position.y;
    float side_local_start_x_norm = local_start_x_norm;
    float side_local_end_x_norm = local_end_x_norm;
    float side_local_start_y;
    float side_local_end_y;
    float side_offset_y;

    if (last_profile_keyable != 0) {
      side_profile_height = last_profile_keyable->value;
    }
    side_offset_y = side_profile_height * state->scale.y;
    side_local_start_y = local_start.y - side_offset_y;
    side_local_end_y = local_end.y - side_offset_y;
    if (((source_piece_word & 0x00002000u) != 0u) && (state->course != 0) &&
        (track_segment->track_corner_address != 0u) &&
        (fzgx_track_course_find_track_corner_by_address(
             state->course, track_segment->track_corner_address, &track_corner) ==
         FZGX_STATUS_OK)) {
      fzgx_mat43 corner_inverse;
      fzgx_vec3 corner_local_start;
      fzgx_vec3 corner_local_end;

      state->transform = track_corner->transform;
      state->scale.x = track_corner->width;
      corner_inverse = state->transform;
      fzgx_mat43_rigid_invert_exact(&corner_inverse);
      if (fabsf(state->scale.x) <= FLT_EPSILON) {
        fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      corner_local_start = fzgx_transform_local_point(&corner_inverse, request->start);
      corner_local_end = fzgx_transform_local_point(&corner_inverse, request->end);
      side_local_start_x_norm = corner_local_start.x / state->scale.x;
      side_local_end_x_norm = corner_local_end.x / state->scale.x;
    }
    if (fzgx_track_road_lateral_max_exact <= side_local_start_x_norm) {
      summary_activity_mask &= ~0x40000000u;
    }
    status = fzgx_try_append_track_side_contact_candidate_exact(
        request,
        query,
        track_segment,
        state,
        current_summary,
        source_piece_word,
        side_local_start_x_norm,
        side_local_end_x_norm,
        side_local_start_y,
        side_local_end_y,
        -1,
        &summary_activity_mask,
        (surface_candidate_flags & 0x20000000u) == 0u,
        &side_candidate_accepted);
    fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state);
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_UNIMPLEMENTED)) {
      return status;
    }
    if (side_candidate_accepted) {
      summary_side_candidate_mask |= 0x40000000u;
    }
  }

  if (((source_piece_word & 0x00000400u) != 0u) &&
      ((source_piece_word & 0x00001000u) == 0u) &&
      (local_start_x_norm <= fzgx_track_road_lateral_min_exact)) {
    summary_activity_mask &= ~0x80000000u;
  }
  if (((source_piece_word & 0x00000800u) != 0u) &&
      ((source_piece_word & 0x00002000u) == 0u) &&
      (fzgx_track_modulated_surface_accept_x_exact <= local_start_x_norm)) {
    summary_activity_mask &= ~0x40000000u;
  }
  if (accepted_alternate_surface_hit) {
    summary_activity_mask = 0u;
  }

  fzgx_commit_pending_rejection_frame_exact(state);

  if (current_summary != 0) {
    uint32_t slot = current_summary->candidate_count;
    uint32_t summary_flags = source_piece_word | (alternate_hit.valid ? 0x4u : 0u);
    uint32_t summary_or_mask =
        summary_activity_mask | (summary_side_candidate_mask & 0xfdffffffu);
    uint32_t summary_shared_mask = 0u;

    if (slot > 3u) {
      slot = 3u;
    }
    if ((summary_or_mask != 0u) || (surface_summary_flags != 0u)) {
      summary_flags |= 0x10000000u | summary_or_mask | surface_summary_flags;
    }
    if ((int32_t)summary_side_candidate_mask < 0) {
      summary_shared_mask |= 0x00008000u;
    } else if ((summary_side_candidate_mask & 0x40000000u) != 0u) {
      summary_shared_mask |= 0x00004000u;
    }
    if (summary_shared_mask != 0u) {
      summary_flags &= 0xffff3fffu;
      summary_flags |= summary_shared_mask;
    }
    current_summary->piece_opaque[slot] = track_segment->address;
    current_summary->width_scale[slot] = state->scale.x;
    current_summary->flags[slot] = summary_flags;
    current_summary->checkpoint_index[slot] = query->checkpoint_index;
    current_summary->summary_flags |= summary_flags;
    if (current_summary->candidate_count < 3u) {
      current_summary->candidate_count += 1u;
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_resolve_track_modulated_road_contact_candidates_sonic_oval_variant_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  uint32_t source_piece_word = 0u;
  fzgx_track_side_query_summary *current_summary = 0;
  const fzgx_track_segment_animation_record *animation_segment = 0;
  const fzgx_animation_curve *profile_curve = 0;
  const fzgx_keyable_attribute *first_profile_keyable = 0;
  const fzgx_keyable_attribute *last_profile_keyable = 0;
  const fzgx_track_side_query_summary *previous_summary = 0;
  const fzgx_track_side_query_summary *surface_match_summary = 0;
  fzgx_track_side_query_summary sonic_oval_bias_summary;
  fzgx_mat43 inverse_transform;
  fzgx_vec3 local_start;
  fzgx_vec3 local_end;
  float local_start_x_norm;
  float local_end_x_norm;
  float local_hit_x_norm = 0.0f;
  float local_surface_height;
  float scaled_surface_height;
  float profile_boundary_height_right = 0.0f;
  float profile_boundary_height_left = 0.0f;
  float profile_boundary_slope_right = 0.0f;
  float profile_boundary_slope_left = 0.0f;
  float surface_t = 0.0f;
  float denominator;
  uint32_t summary_activity_mask = 0xe0000000u;
  uint32_t summary_side_candidate_mask = 0u;
  uint32_t surface_summary_flags = 0u;
  uint32_t surface_candidate_flags = 0u;
  uint32_t surface_match_flags = 0u;
  bool start_below_surface;
  bool end_above_surface;
  bool use_floor_bias;
  bool have_surface_match;
  bool previous_alternate_match = false;
  bool enable_right_side;
  bool enable_left_side;
  bool side_accept = false;
  bool accepted_alternate_surface_hit = false;
  fzgx_track_transient_hit_exact primary_hit = {0};
  fzgx_track_transient_hit_exact alternate_hit = {0};
  fzgx_status status;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (state->animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        state->animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
      (animation_segment->animation_curve_trs->curves != 0) &&
      (animation_segment->animation_curve_trs->curve_count >
       FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y_EXACT)) {
    profile_curve = &animation_segment->animation_curve_trs
                         ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y_EXACT];
  }
  if ((profile_curve != 0) && (profile_curve->keyables != 0) &&
      (profile_curve->keyable_count != 0u)) {
    first_profile_keyable = &profile_curve->keyables[0];
    last_profile_keyable = &profile_curve->keyables[profile_curve->keyable_count - 1u];
  } else {
    profile_curve = 0;
  }

  memset(&sonic_oval_bias_summary, 0, sizeof(sonic_oval_bias_summary));
  previous_summary = fzgx_request_previous_side_summary_exact(request);
  current_summary = fzgx_request_current_side_summary_exact(request);
  if (previous_summary != 0) {
    surface_match_summary = previous_summary;
  }
  use_floor_bias =
      request->has_sonic_oval_floor_bias && ((dispatch_flags & 0x01800000u) == 0u);
  if (use_floor_bias && (request->sonic_oval_floor_bias.candidate_count != 0u)) {
    sonic_oval_bias_summary.candidate_count = 1u;
    sonic_oval_bias_summary.piece_opaque[0] = request->sonic_oval_floor_bias.slot0_piece_opaque;
    sonic_oval_bias_summary.flags[0] = request->sonic_oval_floor_bias.latest_slot0_flags;
    sonic_oval_bias_summary.checkpoint_index[0] =
        request->sonic_oval_floor_bias.slot0_checkpoint_index;
    surface_match_summary = &sonic_oval_bias_summary;
  }

  inverse_transform = state->transform;
  fzgx_mat43_rigid_invert_exact(&inverse_transform);
  local_start = fzgx_transform_local_point(&inverse_transform, request->start);
  local_end = fzgx_transform_local_point(&inverse_transform, request->end);
  if (fabsf(state->scale.x) <= FLT_EPSILON) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  local_start_x_norm = local_start.x / state->scale.x;
  local_end_x_norm = local_end.x / state->scale.x;

  enable_right_side = ((dispatch_flags & 0x4u) != 0u) && ((source_piece_word & 0x00000400u) != 0u);
  enable_left_side = ((dispatch_flags & 0x4u) != 0u) && ((source_piece_word & 0x00000800u) != 0u);
  if (0.0f < local_start_x_norm) {
    enable_right_side = false;
  }
  if (local_start_x_norm <= 0.0f) {
    enable_left_side = false;
  }

  {
    float profile_sample_x_norm =
        ((request->flags & 0x00080000u) != 0u)
            ? (0.5f * (local_end_x_norm + local_start_x_norm))
            : (((request->flags & 0x00040000u) != 0u) ? local_end_x_norm : local_start_x_norm);
    float profile_curve_time;

    if (profile_curve == 0) {
      local_surface_height = track_segment->fallback_position.y;
    } else {
      profile_curve_time =
          first_profile_keyable->time +
          ((0.5f + profile_sample_x_norm) *
           (last_profile_keyable->time - first_profile_keyable->time));
      if (profile_curve_time <= last_profile_keyable->time) {
        status = fzgx_evaluate_float_animation_curve(
            profile_curve, profile_curve_time, &local_surface_height);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      } else {
        local_surface_height =
            last_profile_keyable->value +
            (last_profile_keyable->tangent_out *
             (profile_curve_time - last_profile_keyable->time));
      }
    }
  }
  scaled_surface_height = local_surface_height * state->scale.y;
  start_below_surface = local_start.y <= scaled_surface_height;
  end_above_surface = local_end.y > scaled_surface_height;
  if (start_below_surface) {
    summary_activity_mask &= ~0x20000000u;
  }
  have_surface_match = false;
  if ((state->course != 0) && (surface_match_summary != 0)) {
    uint32_t slot_index = (current_summary != 0) ? current_summary->candidate_count : 0u;
    uint32_t branch_mask = 0x03e00000u;
    uint32_t can_traverse = 0u;

    if ((slot_index < surface_match_summary->candidate_count) &&
        ((surface_match_summary->flags[slot_index] & 0x20000000u) != 0u)) {
      uint32_t flags = surface_match_summary->flags[slot_index];

      if ((flags & 0x02a00000u) != 0u) {
        flags |= 0x02a00000u;
      }
      if (fzgx_collision_scratch_read_u32_current_exact(
              FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT) ==
          surface_match_summary->candidate_count) {
        branch_mask = 0x03e00c00u;
      }
      if (((source_piece_word & branch_mask) == (source_piece_word & branch_mask & flags)) &&
          ((surface_match_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) ||
           ((fzgx_track_course_can_traverse_checkpoint_interval(
                 state->course,
                 fzgx_track_summary_checkpoint_traverse_gap_exact,
                 surface_match_summary->checkpoint_index[slot_index],
                 query->checkpoint_index,
                 &can_traverse) == FZGX_STATUS_OK) &&
            (can_traverse != 0u)))) {
        have_surface_match = true;
      }
    } else if (fzgx_collision_scratch_read_u32_current_exact(
                   FZGX_COLLISION_SCRATCH_TRACK_QUERY_CONTINUITY_GATE_OFFSET_EXACT) != 0u) {
      for (slot_index = 0u; slot_index < surface_match_summary->candidate_count; ++slot_index) {
        uint32_t flags = surface_match_summary->flags[slot_index];

        if ((flags & 0x20000000u) == 0u) {
          continue;
        }
        if ((flags & 0x02a00000u) != 0u) {
          flags |= 0x02a00000u;
        }
        if (fzgx_collision_scratch_read_u32_current_exact(
                FZGX_COLLISION_SCRATCH_TRACK_QUERY_SEED_LIMIT_OFFSET_EXACT) ==
            surface_match_summary->candidate_count) {
          branch_mask = 0x03e00c00u;
        } else {
          branch_mask = 0x03e00000u;
        }
        if ((source_piece_word & branch_mask) != (source_piece_word & branch_mask & flags)) {
          continue;
        }
        if (surface_match_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) {
          have_surface_match = true;
          break;
        }
        if (fzgx_track_course_can_traverse_checkpoint_interval(
                state->course,
                fzgx_track_summary_checkpoint_traverse_gap_exact,
                surface_match_summary->checkpoint_index[slot_index],
                query->checkpoint_index,
                &can_traverse) != FZGX_STATUS_OK) {
          continue;
        }
        if (can_traverse != 0u) {
          have_surface_match = true;
          break;
        }
      }
    }
  }
  if (surface_match_summary != 0) {
    surface_match_flags = surface_match_summary->flags[0];
  }

  if (((fzgx_track_road_lateral_min_exact <= local_end_x_norm) ||
       (fzgx_track_road_lateral_min_exact <= local_start_x_norm)) &&
      ((local_end_x_norm <= fzgx_track_road_lateral_max_exact) ||
       (local_start_x_norm <= fzgx_track_road_lateral_max_exact))) {
    denominator = local_end.y - local_start.y;
    if (fabsf(denominator) > FLT_EPSILON) {
      surface_t = (local_end.y - scaled_surface_height) / denominator;
    } else {
      surface_t = 0.0f;
    }
    if (((local_end.y < scaled_surface_height) || !start_below_surface) ||
        ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) == 0x7f800000u)) {
      if (local_end.y < local_start.y) {
        if ((previous_summary != 0) && !have_surface_match &&
            (0.0f <= surface_t) && (surface_t <= 1.0f) &&
            ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) != 0x7f800000u)) {
          surface_candidate_flags = 0x02000000u;
        }
      } else if ((0.0f <= surface_t) && (surface_t <= 1.0f) &&
                 ((fzgx_float_bits_exact(surface_t) & 0x7f800000u) != 0x7f800000u)) {
        surface_candidate_flags = 0x20000000u;
        surface_summary_flags = 0x20000000u;
      }
    } else {
      surface_candidate_flags = 0x20000000u;
      surface_summary_flags = 0x20000000u;
    }
  }

  if (have_surface_match && start_below_surface &&
      ((surface_candidate_flags & 0x20000000u) == 0u) &&
      (fzgx_collision_scratch_read_u32_current_exact(
           FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT) == 0u)) {
    surface_candidate_flags = 0x20000000u;
    surface_summary_flags = 0x20000000u;
    surface_t = 0.0f;
  }
  if ((previous_summary != 0) && (surface_candidate_flags == 0u)) {
    bool require_alternate_match = have_surface_match || end_above_surface;

    if (require_alternate_match) {
      uint32_t slot_index = (current_summary != 0) ? current_summary->candidate_count : 0u;

      if ((state->course != 0) && (slot_index < previous_summary->candidate_count)) {
        uint32_t previous_flags = previous_summary->flags[slot_index];
        uint32_t branch_mask = 0x03e00000u;
        uint32_t can_traverse = 0u;

        if ((previous_flags & 0x4u) != 0u) {
          if (state->summary_seed_slot_count == previous_summary->candidate_count) {
            branch_mask = 0x03e00c00u;
          }
          if (((source_piece_word & branch_mask) ==
               (source_piece_word & branch_mask & previous_flags)) &&
              ((previous_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) ||
               ((fzgx_track_course_can_traverse_checkpoint_interval(
                     state->course,
                     fzgx_track_summary_checkpoint_traverse_gap_exact,
                     previous_summary->checkpoint_index[slot_index],
                     query->checkpoint_index,
                     &can_traverse) == FZGX_STATUS_OK) &&
                (can_traverse != 0u)))) {
            previous_alternate_match = true;
          }
        }
      }
      if (!previous_alternate_match) {
        goto finish_sonic_modulated_surface_candidates;
      }
    }
    if (!start_below_surface) {
      if (fzgx_collision_scratch_read_u32_current_exact(
              FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT) != 0u) {
        goto finish_sonic_modulated_surface_candidates;
      }
      surface_candidate_flags = 0x02000000u;
      surface_t = 0.0f;
    }
  }
finish_sonic_modulated_surface_candidates:

  if (surface_candidate_flags != 0u) {
    fzgx_track_transient_hit_exact *transient_hit = 0;
    fzgx_vec3 hit_normal = fzgx_mat43_get_basis_y_exact(&state->transform);
    fzgx_vec3 world_hit_point;
    float road_push_length;

    local_hit_x_norm = local_end_x_norm + surface_t * (local_start_x_norm - local_end_x_norm);
    if (!use_floor_bias && ((dispatch_flags & 0x02000000u) != 0u) && have_surface_match) {
      if (((0.0f < local_hit_x_norm) && enable_left_side &&
           ((surface_match_flags & 0x40000000u) != 0u)) ||
          ((local_hit_x_norm <= 0.0f) && enable_right_side &&
           ((surface_match_flags & 0x80000000u) != 0u))) {
        side_accept = true;
      }
    } else if (use_floor_bias && have_surface_match) {
      if (((0.0f < local_hit_x_norm) && enable_left_side &&
           ((surface_match_flags & 0x40000000u) != 0u)) ||
          ((local_hit_x_norm <= 0.0f) && enable_right_side &&
           ((surface_match_flags & 0x80000000u) != 0u))) {
        side_accept = true;
      }
    }
    if (((surface_candidate_flags & 0x20000000u) != 0u) &&
        ((fabsf(local_hit_x_norm) <= fzgx_track_modulated_surface_accept_x_exact) || side_accept)) {
      transient_hit = &primary_hit;
    } else if (((surface_candidate_flags & 0x02000000u) != 0u) &&
               (fabsf(local_hit_x_norm) <= fzgx_track_modulated_surface_accept_x_exact)) {
      transient_hit = &alternate_hit;
      hit_normal = fzgx_vec3_scale(hit_normal, -1.0f);
      accepted_alternate_surface_hit = true;
    }
    if (transient_hit != 0) {
      road_push_length = scaled_surface_height - local_start.y;
      if (transient_hit == &primary_hit) {
        if (road_push_length < 0.0f) {
          road_push_length = 0.0f;
        }
      } else if (0.0f <= road_push_length) {
        road_push_length = 0.0f;
      }
      world_hit_point = fzgx_vec3_add(
          request->end, fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), surface_t));
      transient_hit->valid = true;
      transient_hit->hit_time = surface_t;
      transient_hit->hit_point = world_hit_point;
      transient_hit->aux_hit_point = world_hit_point;
      transient_hit->hit_normal = hit_normal;
      transient_hit->push = fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&state->transform), road_push_length);
      transient_hit->result_flags = 0x00200000u;
    } else if ((surface_candidate_flags & 0x20000000u) != 0u) {
      surface_candidate_flags = 0u;
      surface_summary_flags = 0u;
    }
  }

  if (primary_hit.valid) {
    if (fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
            FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
            primary_hit.hit_time)) {
      fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
          query,
          &primary_hit,
          fzgx_request_current_side_summary_slot_flags_ptr_exact(
              request, (current_summary != 0) ? current_summary->candidate_count : 0u));
      if (result_inout != 0) {
        result_inout->checkpoint_index = query->checkpoint_index;
        result_inout->checkpoint_fraction = query->checkpoint_fraction;
        result_inout->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
        result_inout->selected_cached_frame_index = (int32_t)query->branch_selector;
      }
      if (fzgx_vec3_is_nonzero(primary_hit.push)) {
        fzgx_publish_contact_slot_exact(
            result_inout,
            1u,
            primary_hit.push,
            source_piece_word | 0x10000000u | 0x20000000u,
            fzgx_request_current_side_summary_slot_flags_ptr_exact(
                request, (current_summary != 0) ? current_summary->candidate_count : 0u),
            primary_hit.hit_time);
      }
    }
    if (use_floor_bias) {
      enable_right_side = false;
      enable_left_side = false;
    }
  }
  if (alternate_hit.valid) {
    fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT,
        query,
        &alternate_hit,
        fzgx_request_current_side_summary_slot_flags_ptr_exact(
            request, (current_summary != 0) ? current_summary->candidate_count : 0u));
  }

  if (accepted_alternate_surface_hit) {
    summary_activity_mask = 0u;
  }

  if (enable_right_side) {
    fzgx_mat43 saved_transform = state->transform;
    fzgx_mat43 side_transform = state->transform;
    fzgx_mat43 side_inverse;
    fzgx_vec3 side_local_start;
    fzgx_vec3 side_local_end;
    uint16_t right_angle = 0u;
    bool side_candidate_accepted = false;

    if (profile_curve == 0) {
      profile_boundary_height_right = track_segment->fallback_position.y;
      profile_boundary_slope_right = 0.0f;
    } else {
      float profile_curve_time = first_profile_keyable->time;

      status = fzgx_evaluate_float_animation_curve(
          profile_curve, profile_curve_time, &profile_boundary_height_right);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      status = fzgx_evaluate_float_animation_curve_derivative_exact(
          profile_curve, profile_curve_time, &profile_boundary_slope_right);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }
    if (fabsf(state->scale.x) > FLT_EPSILON) {
      right_angle = fzgx_math_atan2_angle16(
          profile_boundary_slope_right * (state->scale.y / state->scale.x), 1.0f);
    }
    fzgx_mat43_set_origin_exact(&side_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&side_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&side_transform), profile_boundary_height_right * state->scale.y)));
    fzgx_mat43_set_origin_exact(&side_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&side_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&side_transform), fzgx_track_road_lateral_min_exact * state->scale.x)));
    if (right_angle != 0u) {
      fzgx_mat43_rotate_about_z_right(&side_transform, right_angle);
    }
    fzgx_mat43_set_origin_exact(&side_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&side_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&side_transform), fzgx_track_road_lateral_max_exact * state->scale.x)));
    side_inverse = side_transform;
    fzgx_mat43_rigid_invert_exact(&side_inverse);
    side_local_start = fzgx_transform_local_point(&side_inverse, request->start);
    side_local_end = fzgx_transform_local_point(&side_inverse, request->end);
    side_local_start.x /= state->scale.x;
    side_local_end.x /= state->scale.x;
    if (side_local_start.x <= fzgx_track_road_lateral_min_exact) {
      summary_activity_mask &= ~0x80000000u;
    }
    state->transform = side_transform;
    status = fzgx_try_append_track_side_contact_candidate_exact(
        request,
        query,
        track_segment,
        state,
        current_summary,
        source_piece_word,
        side_local_start.x,
        side_local_end.x,
        side_local_start.y,
        side_local_end.y,
        +1,
        &summary_activity_mask,
        (surface_candidate_flags & 0x20000000u) == 0u,
        &side_candidate_accepted);
    state->transform = saved_transform;
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_UNIMPLEMENTED)) {
      return status;
    }
    if (side_candidate_accepted) {
      summary_side_candidate_mask |= 0x80000000u;
    }
  }

  if (enable_left_side) {
    fzgx_mat43 saved_transform = state->transform;
    fzgx_mat43 side_transform = state->transform;
    fzgx_mat43 side_inverse;
    fzgx_vec3 side_local_start;
    fzgx_vec3 side_local_end;
    uint16_t left_angle = 0u;
    bool side_candidate_accepted = false;

    if (profile_curve == 0) {
      profile_boundary_height_left = track_segment->fallback_position.y;
      profile_boundary_slope_left = 0.0f;
    } else {
      float profile_curve_time = last_profile_keyable->time;

      status = fzgx_evaluate_float_animation_curve(
          profile_curve, profile_curve_time, &profile_boundary_height_left);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      status = fzgx_evaluate_float_animation_curve_derivative_exact(
          profile_curve, profile_curve_time, &profile_boundary_slope_left);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }
    if (fabsf(state->scale.x) > FLT_EPSILON) {
      left_angle = fzgx_math_atan2_angle16(
          profile_boundary_slope_left * (state->scale.y / state->scale.x), 1.0f);
    }
    fzgx_mat43_set_origin_exact(&side_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&side_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&side_transform), profile_boundary_height_left * state->scale.y)));
    fzgx_mat43_set_origin_exact(&side_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&side_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&side_transform), fzgx_track_road_lateral_max_exact * state->scale.x)));
    if (left_angle != 0u) {
      fzgx_mat43_rotate_about_z_right(&side_transform, left_angle);
    }
    fzgx_mat43_set_origin_exact(&side_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&side_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&side_transform), fzgx_track_road_lateral_min_exact * state->scale.x)));
    side_inverse = side_transform;
    fzgx_mat43_rigid_invert_exact(&side_inverse);
    side_local_start = fzgx_transform_local_point(&side_inverse, request->start);
    side_local_end = fzgx_transform_local_point(&side_inverse, request->end);
    side_local_start.x /= state->scale.x;
    side_local_end.x /= state->scale.x;
    if (fzgx_track_road_lateral_max_exact <= side_local_start.x) {
      summary_activity_mask &= ~0x40000000u;
    }
    state->transform = side_transform;
    status = fzgx_try_append_track_side_contact_candidate_exact(
        request,
        query,
        track_segment,
        state,
        current_summary,
        source_piece_word,
        side_local_start.x,
        side_local_end.x,
        side_local_start.y,
        side_local_end.y,
        -1,
        &summary_activity_mask,
        (surface_candidate_flags & 0x20000000u) == 0u,
        &side_candidate_accepted);
    state->transform = saved_transform;
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_UNIMPLEMENTED)) {
      return status;
    }
    if (side_candidate_accepted) {
      summary_side_candidate_mask |= 0x40000000u;
    }
  }

  if (current_summary != 0) {
    uint32_t slot = current_summary->candidate_count;
    uint32_t summary_flags = source_piece_word | (alternate_hit.valid ? 0x4u : 0u);
    uint32_t summary_or_mask =
        summary_activity_mask |
        surface_summary_flags |
        (summary_side_candidate_mask & 0xfdffffffu);

    if (slot > 3u) {
      slot = 3u;
    }
    if (summary_or_mask != 0u) {
      summary_flags |= 0x10000000u | summary_or_mask;
    }
    current_summary->piece_opaque[slot] = track_segment->address;
    current_summary->width_scale[slot] = state->scale.x;
    current_summary->flags[slot] = summary_flags;
    current_summary->checkpoint_index[slot] = query->checkpoint_index;
    current_summary->summary_flags |= summary_flags;
    if (current_summary->candidate_count < 3u) {
      current_summary->candidate_count += 1u;
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_evaluate_float_animation_curve_derivative_exact(
    const fzgx_animation_curve *curve,
    float time,
    float *value_out) {
  uint32_t segment_index;
  const fzgx_keyable_attribute *key0;
  const fzgx_keyable_attribute *key1;
  float segment_duration;
  float time_fraction;
  float scaled_tangent_out;
  float scaled_tangent_in;
  float delta_value;

  if ((curve == 0) || (value_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((curve->keyable_count != 0u) && (curve->keyables == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (curve->keyable_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((curve->keyable_count < 2u) || (time <= curve->keyables[0].time)) {
    *value_out = curve->keyables[0].tangent_out;
    return FZGX_STATUS_OK;
  }
  if (curve->keyables[curve->keyable_count - 1u].time <= time) {
    *value_out = curve->keyables[curve->keyable_count - 1u].tangent_in;
    return FZGX_STATUS_OK;
  }

  key0 = &curve->keyables[0];
  key1 = &curve->keyables[1];
  for (segment_index = 0u; segment_index < curve->keyable_count - 2u; ++segment_index) {
    if (time < key1->time) {
      break;
    }
    ++key0;
    ++key1;
  }

  if (key0->interpolation_mode == 0u) {
    *value_out = 0.0f;
    return FZGX_STATUS_OK;
  }
  segment_duration = key1->time - key0->time;
  if (fabsf(segment_duration) <= FLT_EPSILON) {
    *value_out = 0.0f;
    return FZGX_STATUS_OK;
  }
  if (key0->interpolation_mode == 1u) {
    *value_out = (key1->value - key0->value) / segment_duration;
    return FZGX_STATUS_OK;
  }

  time_fraction = (time - key0->time) / segment_duration;
  scaled_tangent_out = segment_duration * key0->tangent_out;
  scaled_tangent_in = segment_duration * key1->tangent_in;
  delta_value = key0->value - key1->value;
  *value_out =
      ((6.0f * time_fraction * time_fraction) - (6.0f * time_fraction)) * delta_value +
      (((3.0f * time_fraction * time_fraction) - (4.0f * time_fraction)) + 1.0f) *
          scaled_tangent_out +
      (((3.0f * time_fraction * time_fraction) - (2.0f * time_fraction)) *
       scaled_tangent_in);
  *value_out /= segment_duration;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_try_append_track_side_contact_candidate_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_track_side_query_summary *current_summary,
    uint32_t source_piece_word,
    float local_start_x_norm,
    float local_end_x_norm,
    float local_start_y,
    float local_end_y,
    int side_sign,
    uint32_t *summary_activity_mask_inout,
    bool height_gate,
    bool *accepted_candidate_out) {
  const fzgx_track_side_query_summary *previous_summary = 0;
  fzgx_vec3 previous_hit_normal = {0.0f, 0.0f, 0.0f};
  fzgx_track_pending_side_contact_exact candidate;
  fzgx_vec3 hit_normal;
  float rail_height = 0.0f;
  float hit_local_y = 0.0f;
  float denominator = 0.0f;
  uint32_t hit_mask = 0u;
  uint32_t side_index = 0u;
  uint32_t summary_side_bit = 0u;
  uint32_t slot_index = 0u;
  float hit_time = 0.0f;
  float push_term = 0.0f;
  bool have_hit = false;
  bool start_beyond_side = false;
  bool direct_cross_candidate = false;
  bool direct_cross_height_ok = false;
  bool previous_slot_matches = false;
  bool trace_side_append = false;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) ||
      (summary_activity_mask_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (accepted_candidate_out != 0) {
    *accepted_candidate_out = false;
  }
  trace_side_append =
      (state->authored_track_id == 0x1du) &&
      (486u <= state->debug_world_frame_index) &&
      (state->debug_world_frame_index <= 505u);

  previous_summary = fzgx_request_previous_side_summary_exact(request);
  if (previous_summary != 0) {
    previous_hit_normal = fzgx_get_track_side_query_summary_hit_normal_exact(previous_summary);
  }
  if (current_summary != 0) {
    slot_index = current_summary->candidate_count;
  }

  if (side_sign <= 0) {
    rail_height = track_segment->rail_height_right;
    hit_mask = 0x800u;
    side_index = 1u;
    summary_side_bit = 0x40000000u;
    hit_normal = fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&state->transform), -1.0f);
    start_beyond_side = fzgx_track_road_lateral_max_exact <= local_start_x_norm;
    if (start_beyond_side && (local_end_x_norm <= fzgx_track_road_lateral_max_exact)) {
      direct_cross_candidate = true;
      denominator = local_end_x_norm - local_start_x_norm;
      hit_time = (local_end_x_norm - fzgx_track_road_lateral_max_exact) / denominator;
      hit_local_y = local_end_y + hit_time * (local_start_y - local_end_y);
      *summary_activity_mask_inout |= summary_side_bit;
      direct_cross_height_ok = !height_gate || (fabsf(hit_local_y) <= rail_height);
      have_hit = direct_cross_height_ok &&
                 (0.0f <= hit_time) && (hit_time <= 1.0f) &&
                 ((fzgx_float_bits_exact(hit_time) & 0x7f800000u) != 0x7f800000u);
    }
    if (!have_hit && start_beyond_side && (previous_summary != 0)) {
      if ((state->course != 0) && (slot_index < previous_summary->candidate_count)) {
        uint32_t previous_flags = previous_summary->flags[slot_index];
        uint32_t branch_mask = 0x03e00000u;
        uint32_t can_traverse = 0u;
        int32_t previous_checkpoint_index = previous_summary->checkpoint_index[slot_index];

        if ((((previous_flags << (side_index + 1u)) |
              (previous_flags >> (32u - (side_index + 1u)))) & 1u) != 0u) {
          if (state->summary_seed_slot_count == previous_summary->candidate_count) {
            branch_mask = 0x03e00c00u;
          }
          if ((source_piece_word & branch_mask) ==
              (source_piece_word & branch_mask & previous_flags)) {
            previous_checkpoint_index = previous_summary->checkpoint_index[slot_index];
            if (previous_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) {
              previous_slot_matches = true;
            } else if ((fzgx_track_course_can_traverse_checkpoint_interval(
                            state->course,
                            fzgx_track_summary_checkpoint_traverse_gap_exact,
                            previous_checkpoint_index,
                            query->checkpoint_index,
                            &can_traverse) == FZGX_STATUS_OK) &&
                       (can_traverse != 0u)) {
              previous_slot_matches = true;
            }
            if (previous_slot_matches) {
              if (state->authored_track_id == 0x10u) {
                if (!((((query->checkpoint_index < 0x5f) || (0x79 < query->checkpoint_index)) ||
                       ((0x5e < previous_checkpoint_index) &&
                        (previous_checkpoint_index < 0x7a))) ||
                      (((query->branch_selector != 1u) || (side_index != 0u)) &&
                       ((query->branch_selector != 2u) || (side_index != 1u))))) {
                  previous_slot_matches = false;
                }
              } else if (state->authored_track_id == 0x1cu) {
                if ((query->checkpoint_index < 0x74) || (0x90 < query->checkpoint_index)) {
                  if (!((query->checkpoint_index < 0xa8) || (0xc6 < query->checkpoint_index))) {
                    if ((previous_checkpoint_index < 0xa8) &&
                        (((query->branch_selector == 1u) && (side_index == 0u)) ||
                         ((query->branch_selector == 2u) && (side_index == 1u)))) {
                      previous_slot_matches = false;
                    } else if (!(previous_checkpoint_index < 199) &&
                               (((query->branch_selector == 1u) && (side_index == 1u)) ||
                                ((query->branch_selector == 2u) && (side_index == 0u)))) {
                      previous_slot_matches = false;
                    }
                  }
                } else if ((previous_checkpoint_index < 0x74) &&
                           (((query->branch_selector == 1u) && (side_index == 1u)) ||
                            ((query->branch_selector == 2u) && (side_index == 0u)))) {
                  previous_slot_matches = false;
                } else if (!(previous_checkpoint_index < 0x91) &&
                           (((query->branch_selector == 1u) && (side_index == 0u)) ||
                            ((query->branch_selector == 2u) && (side_index == 1u)))) {
                  previous_slot_matches = false;
                }
              }
            }
          }
        }
      }
      if (previous_slot_matches &&
          (!height_gate || (fabsf(local_start_y) <= rail_height) ||
           (fabsf(local_end_y) <= rail_height))) {
        hit_time = 0.0f;
        have_hit = true;
        *summary_activity_mask_inout |= summary_side_bit;
      }
    }
  } else {
    rail_height = track_segment->rail_height_left;
    hit_mask = 0x400u;
    side_index = 0u;
    summary_side_bit = 0x80000000u;
    hit_normal = fzgx_mat43_get_basis_x_exact(&state->transform);
    start_beyond_side = local_start_x_norm <= -fzgx_track_road_lateral_max_exact;
    if (start_beyond_side && (-fzgx_track_road_lateral_max_exact <= local_end_x_norm)) {
      direct_cross_candidate = true;
      denominator = local_end_x_norm - local_start_x_norm;
      hit_time = (local_end_x_norm + fzgx_track_road_lateral_max_exact) / denominator;
      hit_local_y = local_end_y + hit_time * (local_start_y - local_end_y);
      *summary_activity_mask_inout |= summary_side_bit;
      direct_cross_height_ok = !height_gate || (fabsf(hit_local_y) <= rail_height);
      have_hit = direct_cross_height_ok &&
                 (0.0f <= hit_time) && (hit_time <= 1.0f) &&
                 ((fzgx_float_bits_exact(hit_time) & 0x7f800000u) != 0x7f800000u);
    }
    if (!have_hit && start_beyond_side && (previous_summary != 0)) {
      if ((state->course != 0) && (slot_index < previous_summary->candidate_count)) {
        uint32_t previous_flags = previous_summary->flags[slot_index];
        uint32_t branch_mask = 0x03e00000u;
        uint32_t can_traverse = 0u;
        int32_t previous_checkpoint_index = previous_summary->checkpoint_index[slot_index];

        if ((((previous_flags << (side_index + 1u)) |
              (previous_flags >> (32u - (side_index + 1u)))) & 1u) != 0u) {
          if (state->summary_seed_slot_count == previous_summary->candidate_count) {
            branch_mask = 0x03e00c00u;
          }
          if ((source_piece_word & branch_mask) ==
              (source_piece_word & branch_mask & previous_flags)) {
            previous_checkpoint_index = previous_summary->checkpoint_index[slot_index];
            if (previous_summary->piece_opaque[slot_index] == (uint32_t)track_segment->address) {
              previous_slot_matches = true;
            } else if ((fzgx_track_course_can_traverse_checkpoint_interval(
                            state->course,
                            fzgx_track_summary_checkpoint_traverse_gap_exact,
                            previous_checkpoint_index,
                            query->checkpoint_index,
                            &can_traverse) == FZGX_STATUS_OK) &&
                       (can_traverse != 0u)) {
              previous_slot_matches = true;
            }
            if (previous_slot_matches) {
              if (state->authored_track_id == 0x10u) {
                if (!((((query->checkpoint_index < 0x5f) || (0x79 < query->checkpoint_index)) ||
                       ((0x5e < previous_checkpoint_index) &&
                        (previous_checkpoint_index < 0x7a))) ||
                      (((query->branch_selector != 1u) || (side_index != 0u)) &&
                       ((query->branch_selector != 2u) || (side_index != 1u))))) {
                  previous_slot_matches = false;
                }
              } else if (state->authored_track_id == 0x1cu) {
                if ((query->checkpoint_index < 0x74) || (0x90 < query->checkpoint_index)) {
                  if (!((query->checkpoint_index < 0xa8) || (0xc6 < query->checkpoint_index))) {
                    if ((previous_checkpoint_index < 0xa8) &&
                        (((query->branch_selector == 1u) && (side_index == 0u)) ||
                         ((query->branch_selector == 2u) && (side_index == 1u)))) {
                      previous_slot_matches = false;
                    } else if (!(previous_checkpoint_index < 199) &&
                               (((query->branch_selector == 1u) && (side_index == 1u)) ||
                                ((query->branch_selector == 2u) && (side_index == 0u)))) {
                      previous_slot_matches = false;
                    }
                  }
                } else if ((previous_checkpoint_index < 0x74) &&
                           (((query->branch_selector == 1u) && (side_index == 1u)) ||
                            ((query->branch_selector == 2u) && (side_index == 0u)))) {
                  previous_slot_matches = false;
                } else if (!(previous_checkpoint_index < 0x91) &&
                           (((query->branch_selector == 1u) && (side_index == 0u)) ||
                            ((query->branch_selector == 2u) && (side_index == 1u)))) {
                  previous_slot_matches = false;
                }
              }
            }
          }
        }
      }
      if (previous_slot_matches &&
          (!height_gate || (fabsf(local_start_y) <= rail_height) ||
           (fabsf(local_end_y) <= rail_height))) {
        hit_time = 0.0f;
        have_hit = true;
        *summary_activity_mask_inout |= summary_side_bit;
      }
    }
  }

  if (!have_hit) {
    return FZGX_STATUS_OK;
  }

  memset(&candidate, 0, sizeof(candidate));
  candidate.branch_selector = (int32_t)query->branch_selector;
  candidate.checkpoint_index = query->checkpoint_index;
  candidate.checkpoint_fraction = query->checkpoint_fraction;
  candidate.result_flags = hit_mask;
  candidate.hit_time = hit_time;
  candidate.hit_point = fzgx_vec3_add(
      request->end, fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), hit_time));
  candidate.aux_hit_point = fzgx_transform_local_point(
      &state->transform,
      (fzgx_vec3){
          state->scale.x * (-(fzgx_track_road_lateral_max_exact * (float)side_sign)),
          0.0f,
          0.0f});
  candidate.hit_normal = hit_normal;
  push_term =
      (state->scale.x *
       ((float)side_sign *
        ((fzgx_track_road_lateral_max_exact * (float)side_sign) + local_start_x_norm))) -
      fzgx_track_road_lateral_max_exact;
  if (push_term < 0.0f) {
    candidate.push = fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&state->transform), (float)side_sign * -push_term);
  }

  if (fzgx_collision_scratch_read_u32_current_exact(FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT) !=
      0u) {
    uint32_t rejection_index;

    for (rejection_index = 0u;
         rejection_index <
         fzgx_collision_scratch_read_u32_current_exact(FZGX_COLLISION_SCRATCH_OFFSET_0X1D0_EXACT);
         ++rejection_index) {
      fzgx_vec3 local_aux_point;
      float lateral_limit;
      const uint8_t *rejection_frame_raw =
          g_fzgx_collision_scratch_raw_current_exact +
          FZGX_COLLISION_SCRATCH_REJECTION_FRAME_BANK_OFFSET_EXACT +
          (rejection_index * 0x40u);
      const fzgx_mat43 *rejection_inverse_transform =
          (const fzgx_mat43 *)(const void *)rejection_frame_raw;
      float rejection_scale_x = 0.0f;
      float rejection_max_lateral_bound = 0.0f;

      memcpy(&rejection_scale_x, rejection_frame_raw + 0x30u, sizeof(rejection_scale_x));
      memcpy(
          &rejection_max_lateral_bound,
          rejection_frame_raw + 0x3cu,
          sizeof(rejection_max_lateral_bound));
      if (fabsf(rejection_scale_x) <= FLT_EPSILON) {
        continue;
      }
      local_aux_point = fzgx_transform_local_point(
          rejection_inverse_transform,
          candidate.aux_hit_point);
      lateral_limit =
          (fzgx_track_road_lateral_max_exact * rejection_scale_x) -
          fzgx_track_side_rejection_epsilon_exact;
      if ((fabsf(local_aux_point.x) <= lateral_limit) &&
          (fabsf(local_aux_point.y) <= rejection_max_lateral_bound)) {
        if (trace_side_append) {
          fprintf(
              stderr,
              "sideappend|frame=%u|seg=0x%08x|bs=%u|side=%d|cp=%d|slot=%u|"
              "t=%.6f|lsx=%.6f|lex=%.6f|lsy=%.6f|ley=%.6f|"
              "flags=0x%08x|reject=overlap|aux=(%.3f,%.3f,%.3f)|"
              "rejl=(%.6f,%.6f,%.6f)|limit=%.6f|height=%.6f\n",
              state->debug_world_frame_index,
              (unsigned)track_segment->address,
              query->branch_selector,
              side_sign,
              query->checkpoint_index,
              slot_index,
              hit_time,
              local_start_x_norm,
              local_end_x_norm,
              local_start_y,
              local_end_y,
              candidate.result_flags,
              candidate.aux_hit_point.x,
              candidate.aux_hit_point.y,
              candidate.aux_hit_point.z,
              local_aux_point.x,
              local_aux_point.y,
              local_aux_point.z,
              lateral_limit,
              rejection_max_lateral_bound);
        }
        if (current_summary != 0) {
          current_summary->special_postprocess_flag = 1u;
        }
        *summary_activity_mask_inout &= ~summary_side_bit;
        return FZGX_STATUS_OK;
      }
    }
  }

  if ((previous_summary != 0) &&
      (fzgx_vec3_dot(previous_hit_normal, candidate.hit_normal) < 0.0f)) {
    if (trace_side_append) {
      fprintf(
          stderr,
          "sideappend|frame=%u|seg=0x%08x|bs=%u|side=%d|cp=%d|slot=%u|"
          "t=%.6f|lsx=%.6f|lex=%.6f|lsy=%.6f|ley=%.6f|"
          "flags=0x%08x|reject=normal|prev=(%.3f,%.3f,%.3f)|hitn=(%.3f,%.3f,%.3f)\n",
          state->debug_world_frame_index,
          (unsigned)track_segment->address,
          query->branch_selector,
          side_sign,
          query->checkpoint_index,
          slot_index,
          hit_time,
          local_start_x_norm,
          local_end_x_norm,
          local_start_y,
          local_end_y,
          candidate.result_flags,
          previous_hit_normal.x,
          previous_hit_normal.y,
          previous_hit_normal.z,
          candidate.hit_normal.x,
          candidate.hit_normal.y,
          candidate.hit_normal.z);
    }
    *summary_activity_mask_inout &= ~summary_side_bit;
    return FZGX_STATUS_OK;
  }
  candidate.summary_flags_ptr_opaque =
      fzgx_collision_scratch_current_track_side_summary_slot_flags_ptr_opaque_exact(
          (slot_index <= 3u)
              ? fzgx_request_current_side_summary_slot_flags_ptr_exact(request, slot_index)
              : (uintptr_t)0);

  {
    uint32_t pending_index = fzgx_collision_scratch_read_u32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT);

    if (pending_index < FZGX_TRACK_PENDING_SIDE_CANDIDATE_CAPACITY_EXACT) {
      memcpy(
          g_fzgx_collision_scratch_raw_current_exact +
              FZGX_COLLISION_SCRATCH_PENDING_SIDE_BANK_OFFSET_EXACT +
              (pending_index * sizeof(candidate)),
          &candidate,
          sizeof(candidate));
      fzgx_collision_scratch_write_u32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X5AC_EXACT, pending_index + 1u);
      fzgx_collision_scratch_write_u32_current_exact(
          FZGX_COLLISION_SCRATCH_PENDING_SIDE_SUMMARY_SLOT_MAP_OFFSET_EXACT +
              (pending_index * sizeof(uint32_t)),
          slot_index);
      if (trace_side_append) {
        fprintf(
            stderr,
            "sideappend|frame=%u|seg=0x%08x|bs=%u|side=%d|cp=%d|slot=%u|"
            "t=%.6f|lsx=%.6f|lex=%.6f|lsy=%.6f|ley=%.6f|"
            "flags=0x%08x|accept=1|direct=%u|direct_h=%u|start_beyond=%u|"
            "prevn=(%.3f,%.3f,%.3f)|"
            "hit=(%.3f,%.3f,%.3f)|aux=(%.3f,%.3f,%.3f)|push=(%.3f,%.3f,%.3f)\n",
            state->debug_world_frame_index,
            (unsigned)track_segment->address,
            query->branch_selector,
            side_sign,
            query->checkpoint_index,
            slot_index,
            hit_time,
            local_start_x_norm,
            local_end_x_norm,
            local_start_y,
            local_end_y,
            candidate.result_flags,
            direct_cross_candidate ? 1u : 0u,
            direct_cross_height_ok ? 1u : 0u,
            start_beyond_side ? 1u : 0u,
            previous_hit_normal.x,
            previous_hit_normal.y,
            previous_hit_normal.z,
            candidate.hit_point.x,
            candidate.hit_point.y,
            candidate.hit_point.z,
            candidate.aux_hit_point.x,
            candidate.aux_hit_point.y,
            candidate.aux_hit_point.z,
            candidate.push.x,
            candidate.push.y,
            candidate.push.z);
      }
    }
  }
  if (accepted_candidate_out != 0) {
    *accepted_candidate_out = true;
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_resolve_track_capsule_pipe_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  fzgx_track_side_query_summary *current_summary = 0;
  const fzgx_track_side_query_summary *previous_summary = 0;
  uint32_t source_piece_word = 0u;
  uint32_t current_summary_slot = 0u;
  uint32_t result_family_flags;
  uint32_t candidate_mask = 0u;
  uint32_t summary_mask_hits = 0xf0000000u;
  uint32_t summary_mask_active = 0xf0000000u;
  uint32_t special_material_flags = 0u;
  fzgx_mat43 inverse_transform;
  fzgx_vec3 local_start;
  fzgx_vec3 local_end;
  fzgx_vec3 best_hit_point = {0};
  fzgx_vec3 best_normal = {0};
  fzgx_vec3 best_push = {0};
  float cap_span = 0.0f;
  float cap_radius = 0.0f;
  float candidate_t = 0.0f;
  float best_t = FLT_MAX;
  float start_radius_sq;
  float end_radius_sq;
  float denominator;
  float hit_x;
  float local_hit_y;
  bool is_cylinder;
  bool have_best = false;
  bool allow_zero_t_reuse;
  int side_sign;
  uint32_t candidate_index;
  fzgx_status status;

  typedef struct fzgx_capsule_subshape_candidate_exact {
    bool valid;
    uint8_t reserved0[3];
    float hit_time;
    fzgx_vec3 hit_point;
    fzgx_vec3 hit_normal;
    fzgx_vec3 push;
  } fzgx_capsule_subshape_candidate_exact;

  fzgx_capsule_subshape_candidate_exact candidate = {0};

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_apply_track_collision_node_transform_exact(
      query, track_segment, dispatch_flags, state, result_inout);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  cap_span = fabsf(fzgx_collision_scratch_read_f32_current_exact(
      FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT));
  cap_radius = fabsf(fzgx_collision_scratch_read_f32_current_exact(
      FZGX_COLLISION_SCRATCH_OFFSET_0X1E0_EXACT));

  is_cylinder = (source_piece_word & 0x1u) != 0u;
  side_sign = is_cylinder ? +1 : -1;
  result_family_flags = 0x00400000u | (is_cylinder ? 1u : 0u);
  allow_zero_t_reuse = (dispatch_flags & 0x02000000u) != 0u;
  current_summary = fzgx_request_current_side_summary_exact(request);
  if ((dispatch_flags & 0x4u) != 0u) {
    previous_summary = fzgx_request_previous_side_summary_exact(request);
    if (current_summary != 0) {
      current_summary_slot = current_summary->candidate_count;
    }
  }

  inverse_transform = state->transform;
  fzgx_mat43_rigid_invert_exact(&inverse_transform);
  fzgx_reject_pending_side_candidates_against_current_piece_exact(
      track_segment,
      &inverse_transform,
      state->scale.x,
      state->scale.y,
      state,
      fzgx_request_current_side_summary_exact(request));
  local_start = fzgx_transform_local_point(&inverse_transform, request->start);
  local_end = fzgx_transform_local_point(&inverse_transform, request->end);

  if (fabsf(cap_radius) <= FLT_EPSILON) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  for (candidate_index = 0u; candidate_index < 2u; ++candidate_index) {
    float center_x = (candidate_index == 0u) ? (0.5f * cap_span) : (-0.5f * cap_span);
    float local_start_x = (local_start.x - center_x) / cap_radius;
    float local_end_x = (local_end.x - center_x) / cap_radius;
    float local_start_y_norm = local_start.y / cap_radius;
    float local_end_y_norm = local_end.y / cap_radius;
    float current_best_limit = have_best ? best_t : FLT_MAX;
    bool start_inside;
    bool previous_match = false;

    start_radius_sq = local_start_x * local_start_x + local_start_y_norm * local_start_y_norm;
    end_radius_sq = local_end_x * local_end_x + local_end_y_norm * local_end_y_norm;
    start_inside =
        (side_sign * (start_radius_sq - fzgx_track_round_cross_section_radius_squared_exact)) <= 0.0f;
    if (start_inside) {
      summary_mask_hits &= ~(0x80000000u >> candidate_index);
    }
    if ((0.0f < (double)(start_radius_sq - fzgx_track_round_cross_section_radius_squared_exact)) &&
        (0.0f < (double)(end_radius_sq - fzgx_track_round_cross_section_radius_squared_exact))) {
      summary_mask_active &= ~(0x80000000u >> candidate_index);
    }

    if ((previous_summary != 0) &&
        (current_summary_slot < previous_summary->candidate_count) &&
        (current_summary_slot <= 3u)) {
      uint32_t previous_flags = previous_summary->flags[current_summary_slot];
      uint32_t branch_mask = 0x03e00000u;
      int32_t previous_checkpoint_index = previous_summary->checkpoint_index[current_summary_slot];
      uint32_t can_traverse = 0u;

      if ((previous_flags & (0x80000000u >> candidate_index)) != 0u) {
        if (state->summary_seed_slot_count == previous_summary->candidate_count) {
          branch_mask = 0x03e00c00u;
        }
        if ((source_piece_word & branch_mask) ==
            (source_piece_word & branch_mask & previous_flags)) {
          previous_match = true;
          if (previous_summary->piece_opaque[current_summary_slot] != track_segment->address) {
            status = fzgx_track_course_can_traverse_checkpoint_interval(
                state->course,
                fzgx_track_summary_checkpoint_traverse_gap_exact,
                previous_checkpoint_index,
                query->checkpoint_index,
                &can_traverse);
            if ((status != FZGX_STATUS_OK) || (can_traverse == 0u)) {
              previous_match = false;
            }
          }
        }
      }
    }

    memset(&candidate, 0, sizeof(candidate));
    denominator = (local_start_x - local_end_x) * (local_start_x - local_end_x) +
                  (local_start_y_norm - local_end_y_norm) * (local_start_y_norm - local_end_y_norm);
    if (denominator > FLT_EPSILON) {
      float dot_term = (local_start_x - local_end_x) * local_end_x +
                       (local_start_y_norm - local_end_y_norm) * local_end_y_norm;
      float discriminant =
          dot_term * dot_term -
          denominator * (end_radius_sq - fzgx_track_round_cross_section_radius_squared_exact);

      if (0.0f <= discriminant) {
        float sqrt_discriminant = sqrtf(discriminant);

        if ((fzgx_float_bits_exact(sqrt_discriminant) & 0x7f800000u) != 0x7f800000u) {
          candidate_t = -(dot_term + (float)side_sign * sqrt_discriminant) / denominator;
          if ((0.0f <= candidate_t) && (candidate_t <= 1.0f) &&
              ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) != 0x7f800000u) &&
              (candidate_t < current_best_limit)) {
            candidate.valid = true;
          }
        }
      }
    }
    if (!candidate.valid && previous_match && start_inside && allow_zero_t_reuse && !have_best) {
      candidate.valid = true;
      candidate_t = 0.0f;
    }
    if (!candidate.valid) {
      continue;
    }

    hit_x = local_end_x + candidate_t * (local_start_x - local_end_x);
    if ((candidate_index == 0u) ? (hit_x < 0.0f) : (0.0f < hit_x)) {
      continue;
    }

    local_hit_y = local_end_y_norm + candidate_t * (local_start_y_norm - local_end_y_norm);
    candidate.hit_point = fzgx_vec3_add(
        request->end, fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), candidate_t));
    candidate.hit_normal =
        fzgx_vec3_normalize_or_exact(
            fzgx_transform_local_vector(
                &state->transform,
                (fzgx_vec3){(float)side_sign * hit_x, (float)side_sign * local_hit_y, 0.0f}),
            fzgx_mat43_get_basis_y_exact(&state->transform));
    {
      float penetration = (float)side_sign * (fzgx_generic_surface_push_bias_exact -
                                              sqrtf(fmaxf(0.0f, start_radius_sq)));

      if (0.0f < penetration) {
        fzgx_vec3 push_local =
            {(float)side_sign * hit_x, (float)side_sign * local_hit_y, 0.0f};

        fzgx_vec3_set_length_exact(penetration, &push_local, &push_local);
        push_local.x *= cap_radius;
        push_local.y *= cap_radius;
        candidate.push = fzgx_transform_local_vector(&state->transform, push_local);
      }
    }
    candidate.hit_time = candidate_t;
    candidate_mask |= 0x80000000u >> candidate_index;
    if (!have_best || (candidate.hit_time < best_t)) {
      have_best = true;
      best_t = candidate.hit_time;
      best_hit_point = candidate.hit_point;
      best_normal = candidate.hit_normal;
      best_push = candidate.push;
    }
  }

  for (; candidate_index < 4u; ++candidate_index) {
    fzgx_mat43 candidate_transform = state->transform;
    fzgx_mat43 candidate_inverse_transform;
    bool previous_match = false;
    float local_start_x_norm;
    float local_end_x_norm;
    float local_hit_x_norm;
    float current_best_limit = have_best ? best_t : FLT_MAX;
    float band_y_offset = is_cylinder ? (0.5f * cap_radius) : (-0.5f * cap_radius);
    fzgx_vec3 band_start;
    fzgx_vec3 band_end;
    bool start_on_allowed_side;

    if (candidate_index == 3u) {
      fzgx_mat43_rotate_about_z_right(&candidate_transform, 0x8000u);
    }
    fzgx_mat43_set_origin_exact(&candidate_transform, fzgx_vec3_add(
        fzgx_mat43_get_origin_exact(&candidate_transform),
        fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&candidate_transform), band_y_offset)));
    candidate_inverse_transform = candidate_transform;
    fzgx_mat43_rigid_invert_exact(&candidate_inverse_transform);
    band_start = fzgx_transform_local_point(&candidate_inverse_transform, request->start);
    band_end = fzgx_transform_local_point(&candidate_inverse_transform, request->end);
    local_start_x_norm = band_start.x / cap_span;
    local_end_x_norm = band_end.x / cap_span;
    start_on_allowed_side = band_start.y <= 0.0f;
    if (start_on_allowed_side) {
      summary_mask_hits &= ~(0x80000000u >> candidate_index);
    }
    if ((((float)side_sign * band_start.y) > 0.0f &&
         ((float)side_sign * band_end.y) > 0.0f) ||
        ((local_start_x_norm < -0.5f) && (local_end_x_norm < -0.5f)) ||
        ((0.5f < local_start_x_norm) && (0.5f < local_end_x_norm))) {
      summary_mask_active &= ~(0x80000000u >> candidate_index);
    }

    if ((previous_summary != 0) &&
        (current_summary_slot < previous_summary->candidate_count) &&
        (current_summary_slot <= 3u)) {
      uint32_t previous_flags = previous_summary->flags[current_summary_slot];
      uint32_t branch_mask = 0x03e00000u;
      int32_t previous_checkpoint_index = previous_summary->checkpoint_index[current_summary_slot];
      uint32_t can_traverse = 0u;

      if ((previous_flags & (0x80000000u >> candidate_index)) != 0u) {
        if (state->summary_seed_slot_count == previous_summary->candidate_count) {
          branch_mask = 0x03e00c00u;
        }
        if ((source_piece_word & branch_mask) ==
            (source_piece_word & branch_mask & previous_flags)) {
          previous_match = true;
          if (previous_summary->piece_opaque[current_summary_slot] != track_segment->address) {
            status = fzgx_track_course_can_traverse_checkpoint_interval(
                state->course,
                fzgx_track_summary_checkpoint_traverse_gap_exact,
                previous_checkpoint_index,
                query->checkpoint_index,
                &can_traverse);
            if ((status != FZGX_STATUS_OK) || (can_traverse == 0u)) {
              previous_match = false;
            }
          }
        }
      }
    }

    memset(&candidate, 0, sizeof(candidate));
    if (start_on_allowed_side && (0.0f <= band_end.y) &&
        ((-0.5f <= local_end_x_norm) || (-0.5f <= local_start_x_norm)) &&
        ((local_end_x_norm <= 0.5f) || (local_start_x_norm <= 0.5f))) {
      denominator = band_end.y - band_start.y;
      if (fabsf(denominator) > FLT_EPSILON) {
        candidate_t = band_end.y / denominator;
      } else {
        candidate_t = 0.0f;
      }
      if ((candidate_t < current_best_limit) &&
          ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) != 0x7f800000u)) {
        candidate.valid = true;
      }
    }
    if (!candidate.valid && start_on_allowed_side && !have_best && previous_match && allow_zero_t_reuse) {
      candidate.valid = true;
      candidate_t = 0.0f;
    }
    if (!candidate.valid) {
      continue;
    }

    local_hit_x_norm = local_end_x_norm + candidate_t * (local_start_x_norm - local_end_x_norm);
    if (!(previous_match && !is_cylinder) &&
        !((-0.5f <= local_hit_x_norm) && (local_hit_x_norm <= 0.5f))) {
      continue;
    }

    candidate.hit_point = fzgx_vec3_add(
        request->end, fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), candidate_t));
    candidate.hit_normal =
        fzgx_vec3_normalize_or_exact(
            fzgx_transform_local_vector(&candidate_transform, (fzgx_vec3){0.0f, 1.0f, 0.0f}),
            fzgx_mat43_get_basis_y_exact(&candidate_transform));
    {
      float penetration = -band_start.y;
      if (0.0f < penetration) {
        candidate.push =
            fzgx_transform_local_vector(&candidate_transform, (fzgx_vec3){0.0f, penetration, 0.0f});
      }
    }
    candidate.hit_time = candidate_t;
    candidate_mask |= 0x80000000u >> candidate_index;
    if (!have_best || (candidate.hit_time < best_t)) {
      have_best = true;
      best_t = candidate.hit_time;
      best_hit_point = candidate.hit_point;
      best_normal = candidate.hit_normal;
      best_push = candidate.push;
    }
  }

  if ((candidate_mask != 0u) && ((dispatch_flags & 0x00200000u) != 0u) &&
      (state->authored_track_id == 0x20u)) {
    int32_t checkpoint_index = query->checkpoint_index;

    if (((29 < checkpoint_index) && (checkpoint_index < 70)) ||
        ((checkpoint_index == 92) && (fzgx_generic_surface_push_bias_exact <= query->checkpoint_fraction)) ||
        ((92 < checkpoint_index) && (checkpoint_index < 97)) ||
        ((checkpoint_index == 97) && (query->checkpoint_fraction <= fzgx_generic_surface_push_bias_exact)) ||
        ((119 < checkpoint_index) && (checkpoint_index < 140))) {
      special_material_flags = 0x00080000u;
    }
  }

  if (!is_cylinder) {
    bool enough_activity =
        ((summary_mask_active & 0x80000000u) != 0u) ||
        ((summary_mask_active & 0x40000000u) != 0u) ||
        (((summary_mask_active & 0x20000000u) != 0u) &&
         ((summary_mask_active & 0x10000000u) != 0u));

    summary_mask_hits = enough_activity || (candidate_mask != 0u) ? 0xf0000000u : 0u;
  }

  if ((dispatch_flags & 0x4u) != 0u) {
    uint32_t summary_flags = source_piece_word |
                             ((summary_mask_hits | candidate_mask) == 0u ? 0u : summary_mask_hits | candidate_mask);

    if (current_summary == 0) {
      goto finish_capsule_summary_exact;
    }
    if (current_summary_slot > 3u) {
      current_summary_slot = 3u;
    }
    current_summary->piece_opaque[current_summary_slot] = track_segment->address;
    current_summary->width_scale[current_summary_slot] = state->scale.x;
    current_summary->flags[current_summary_slot] = summary_flags;
    current_summary->checkpoint_index[current_summary_slot] = query->checkpoint_index;
    current_summary->summary_flags |= summary_flags;
    if (current_summary->candidate_count < 3u) {
      current_summary->candidate_count += 1u;
    }
  }
finish_capsule_summary_exact:

  if (have_best &&
      fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT, best_t)) {
    fzgx_track_transient_hit_exact best_hit = {0};

    best_hit.valid = true;
    best_hit.hit_time = best_t;
    best_hit.hit_point = best_hit_point;
    best_hit.aux_hit_point = best_hit_point;
    best_hit.hit_normal = best_normal;
    best_hit.push = best_push;
    best_hit.result_flags = result_family_flags;
    fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
        FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
        query,
        &best_hit,
        fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
    fzgx_collision_scratch_or_u32_current_exact(
        FZGX_COLLISION_SCRATCH_OFFSET_0X1D4_EXACT, special_material_flags);
    fzgx_collision_scratch_or_u32_current_exact(
        FZGX_COLLISION_SCRATCH_MATERIAL_FLAGS_OFFSET_EXACT, special_material_flags);
    if (result_inout != 0) {
      result_inout->material_flags |= special_material_flags;
      result_inout->checkpoint_index = query->checkpoint_index;
      result_inout->checkpoint_fraction = query->checkpoint_fraction;
      result_inout->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
      result_inout->selected_cached_frame_index = (int32_t)query->branch_selector;
    }
    if (fzgx_vec3_is_nonzero(best_push)) {
      fzgx_publish_contact_slot_exact(
          result_inout,
          1u,
          best_push,
          source_piece_word | 0x10000000u | 0x20000000u,
          fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot),
          best_t);
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_resolve_track_open_pipe_cylinder_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  fzgx_track_side_query_summary *current_summary = 0;
  const fzgx_track_side_query_summary *previous_summary = 0;
  uint32_t source_piece_word = 0u;
  uint32_t current_summary_slot = 0u;
  const fzgx_track_segment_record *child_segments = 0;
  const fzgx_track_segment_record *child_segment = 0;
  const fzgx_track_segment_animation_record *child_animation_segment = 0;
  const fzgx_animation_curve_trs *child_animation_curve_trs = 0;
  const fzgx_animation_curve *child_scale_y_curve = 0;
  fzgx_sincos_result open_cutoff_sincos;
  fzgx_mat43 inverse_transform;
  fzgx_vec3 local_end;
  fzgx_vec3 local_start;
  fzgx_vec3 local_end_unscaled;
  fzgx_vec3 local_start_unscaled;
  fzgx_vec3 local_query_point;
  fzgx_vec3 local_delta;
  fzgx_vec3 local_hit;
  fzgx_vec3 local_normal;
  fzgx_vec3 world_hit_start;
  fzgx_vec3 world_hit_point;
  fzgx_vec3 world_normal;
  fzgx_vec3 world_push = {0};
  float radial_sign = -1.0f;
  float radial_bias = fzgx_generic_surface_push_bias_exact;
  float thin_ratio = 0.0f;
  float open_cutoff_y = 0.0f;
  float open_cutoff_param = 0.0f;
  float candidate_t = 0.0f;
  float denominator;
  float start_radius_sq;
  float end_radius_sq;
  float start_radius;
  float push_length;
  uint32_t summary_flags = 0u;
  uint32_t branch_mask = 0x03e00000u;
  uint32_t result_family_flags = 0u;
  uint32_t can_traverse = 0u;
  bool enable_summary;
  bool is_cylinder;
  bool enable_pipe_alternate;
  bool symmetric_scale = false;
  bool thin_cross_section = false;
  bool start_on_allowed_side;
  bool end_on_allowed_side;
  bool previous_or_end_on_allowed_side;
  bool previous_surface_match = false;
  bool previous_alternate_match = false;
  bool summary_active = true;
  bool primary_candidate = false;
  bool alternate_candidate = false;
  bool publish_candidate = false;
  fzgx_track_transient_hit_exact primary_hit = {0};
  fzgx_track_transient_hit_exact alternate_hit = {0};
  fzgx_status status;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_apply_track_collision_node_transform_exact(
      query, track_segment, dispatch_flags, state, result_inout);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  enable_summary = (dispatch_flags & 0x4u) != 0u;
  is_cylinder = (source_piece_word & 0x1u) != 0u;
  enable_pipe_alternate = enable_summary && !is_cylinder;
  if (enable_summary) {
    previous_summary = fzgx_request_previous_side_summary_exact(request);
    current_summary = fzgx_request_current_side_summary_exact(request);
    if (current_summary != 0) {
      current_summary_slot = current_summary->candidate_count;
    }
  }
  if (is_cylinder) {
    radial_sign = 1.0f;
  }
  if ((state->course != 0) && (track_segment->children_count == 1u)) {
    uint32_t child_count = 0u;

    status = fzgx_track_course_get_track_segment_children(
        state->course, track_segment, &child_segments, &child_count);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if ((child_count == 1u) && (child_segments != 0)) {
      child_segment = &child_segments[0];
      if (state->animation_course != 0) {
        status = fzgx_track_course_animation_find_track_segment_by_address(
            state->animation_course, child_segment->address, &child_animation_segment);
        if (status == FZGX_STATUS_OUT_OF_RANGE) {
          child_animation_segment = 0;
        } else if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
      if ((child_animation_segment != 0) &&
          ((child_animation_curve_trs = child_animation_segment->animation_curve_trs) != 0) &&
          (child_animation_curve_trs->curves != 0) &&
          (child_animation_curve_trs->curve_count > FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT)) {
        child_scale_y_curve =
            &child_animation_curve_trs->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y_EXACT];
      }
      if ((child_scale_y_curve == 0) || (child_scale_y_curve->keyables == 0) ||
          (child_scale_y_curve->keyable_count == 0u)) {
        open_cutoff_param = child_segment->fallback_scale.y;
      } else {
        status = fzgx_evaluate_float_animation_curve(
            child_scale_y_curve, query->curve_time, &open_cutoff_param);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
      if (open_cutoff_param < 0.0f) {
        open_cutoff_param = 0.0f;
      } else if (1.0f < open_cutoff_param) {
        open_cutoff_param = 1.0f;
      }
      open_cutoff_sincos = fzgx_math_sincos_14b(
          (uint16_t)(int)(fzgx_track_open_pipe_cutoff_angle_scale_exact * open_cutoff_param));
      open_cutoff_y = fzgx_generic_surface_push_bias_exact * open_cutoff_sincos.cos_value;
    }
  }

  if (fabsf(state->scale.x) <= FLT_EPSILON || fabsf(state->scale.y) <= FLT_EPSILON) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  thin_ratio = state->scale.y / state->scale.x;
  thin_cross_section = thin_ratio < fzgx_track_side_rejection_epsilon_exact;
  if ((dispatch_flags & 0x00200000u) != 0u) {
    symmetric_scale =
        fabs((double)thin_ratio - 1.0) < fzgx_track_round_cross_section_scale_epsilon_exact;
  }

  inverse_transform = state->transform;
  fzgx_mat43_rigid_invert_exact(&inverse_transform);
  local_end = fzgx_transform_local_point(&inverse_transform, request->end);
  if (symmetric_scale) {
    if (!is_cylinder) {
      float radius_sq = local_end.x * local_end.x + local_end.y * local_end.y;

      if (1.0f <= radius_sq) {
        float inv_radius = fzgx_math_rsqrt_exact(radius_sq);

        local_start.x =
            local_end.x + fzgx_track_round_cross_section_start_offset_exact * local_end.x * inv_radius;
        local_start.y =
            local_end.y + fzgx_track_round_cross_section_start_offset_exact * local_end.y * inv_radius;
        local_start.z = 0.0f;
      } else {
        symmetric_scale = false;
      }
    } else {
      local_start = (fzgx_vec3){0};
    }
  }
  if (!symmetric_scale) {
    local_start = fzgx_transform_local_point(&inverse_transform, request->start);
  }

  local_end_unscaled = local_end;
  local_start_unscaled = local_start;
  local_end.x /= state->scale.x;
  local_end.y /= state->scale.y;
  local_start.x /= state->scale.x;
  local_start.y /= state->scale.y;

  start_radius_sq = local_start.x * local_start.x + local_start.y * local_start.y;
  end_radius_sq = local_end.x * local_end.x + local_end.y * local_end.y;
  start_on_allowed_side =
      (radial_sign * (start_radius_sq - fzgx_track_round_cross_section_radius_squared_exact)) >= 0.0f;
  end_on_allowed_side =
      (radial_sign * (end_radius_sq - fzgx_track_round_cross_section_radius_squared_exact)) >= 0.0f;
  summary_active = start_on_allowed_side;

  if ((previous_summary != 0) &&
      (current_summary_slot < previous_summary->candidate_count)) {
    uint32_t previous_flags = previous_summary->flags[current_summary_slot];

    if (state->summary_seed_slot_count == previous_summary->candidate_count) {
      branch_mask = 0x03e00c00u;
    }
    if (((previous_flags & 0x20000000u) != 0u) &&
        ((source_piece_word & branch_mask) == (source_piece_word & branch_mask & previous_flags)) &&
        ((previous_summary->piece_opaque[current_summary_slot] == track_segment->address) ||
         ((fzgx_track_course_can_traverse_checkpoint_interval(
               state->course,
               fzgx_track_summary_checkpoint_traverse_gap_exact,
               previous_summary->checkpoint_index[current_summary_slot],
               query->checkpoint_index,
               &can_traverse) == FZGX_STATUS_OK) &&
          (can_traverse != 0u)))) {
      previous_surface_match = true;
    }
  }

  if (thin_cross_section) {
    float query_x_norm;
    float cutoff_x_norm;
    float cutoff_y_world;

    radial_bias =
        fzgx_generic_surface_push_bias_exact +
        10.0f *
            (fzgx_collision_scratch_read_f32_current_exact(
                 FZGX_COLLISION_SCRATCH_OFFSET_0X1DC_EXACT) /
             state->scale.x) *
            (fzgx_track_side_rejection_epsilon_exact - thin_ratio);
    if ((request->flags & 0x00080000u) != 0u) {
      fzgx_ray_scale_exact(
          fzgx_generic_surface_push_bias_exact,
          &local_start_unscaled,
          &local_end_unscaled,
          &local_query_point);
    } else if ((request->flags & 0x00040000u) != 0u) {
      local_query_point = local_end_unscaled;
    } else {
      local_query_point = local_start_unscaled;
    }
    query_x_norm = fabsf(local_query_point.x) / (fzgx_generic_surface_push_bias_exact * state->scale.x);
    if (1.0f < query_x_norm) {
      query_x_norm = 1.0f;
    }
    cutoff_x_norm = sqrtf(fmaxf(0.0f, 1.0f - query_x_norm * query_x_norm));
    cutoff_y_world =
        radial_sign * (fzgx_generic_surface_push_bias_exact * state->scale.y) * cutoff_x_norm;
    if (fabsf(local_query_point.x) <= (radial_bias * state->scale.x)) {
      denominator = local_end_unscaled.y - local_start_unscaled.y;
      if (fabsf(denominator) > FLT_EPSILON) {
        candidate_t = (local_end_unscaled.y - cutoff_y_world) / denominator;
      } else {
        candidate_t = 0.0f;
      }
      if (local_end_unscaled.y < local_start_unscaled.y) {
        if (enable_pipe_alternate && !previous_surface_match) {
          if ((candidate_t < 0.0f) || (1.0f < candidate_t) ||
              ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) == 0x7f800000u)) {
            if ((cutoff_y_world <= local_start_unscaled.y) &&
                (local_end_unscaled.y <= cutoff_y_world)) {
              candidate_t = 0.0f;
              alternate_candidate = true;
            }
          } else {
            alternate_candidate = true;
          }
        }
      } else if ((candidate_t < 0.0f) || (1.0f < candidate_t) ||
                 ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) == 0x7f800000u)) {
        if ((local_start_unscaled.y <= cutoff_y_world) &&
            (cutoff_y_world <= local_end_unscaled.y)) {
          candidate_t = 0.0f;
          primary_candidate = true;
        }
      } else {
        primary_candidate = true;
      }
    }
  } else {
    float discriminant;

    local_delta.x = local_start.x - local_end.x;
    local_delta.y = local_start.y - local_end.y;
    local_delta.z = local_start.z - local_end.z;
    denominator = local_delta.x * local_delta.x + local_delta.y * local_delta.y;
    if (denominator > FLT_EPSILON) {
      float dot_term = local_delta.x * local_end.x + local_delta.y * local_end.y;

      discriminant =
          dot_term * dot_term -
          denominator *
              ((local_end.x * local_end.x + local_end.y * local_end.y) -
               fzgx_track_round_cross_section_radius_squared_exact);
      if (0.0f <= discriminant) {
        float sqrt_discriminant = sqrtf(discriminant);

        if ((fzgx_float_bits_exact(sqrt_discriminant) & 0x7f800000u) != 0x7f800000u) {
          candidate_t = -(dot_term + radial_sign * sqrt_discriminant) / denominator;
          if ((0.0f <= candidate_t) && (candidate_t <= 1.0f) &&
              ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) != 0x7f800000u)) {
            primary_candidate = true;
          }
          if (enable_pipe_alternate && !primary_candidate && !previous_surface_match) {
            float alternate_t = -(dot_term - radial_sign * sqrt_discriminant) / denominator;

            if ((0.0f <= alternate_t) && (alternate_t <= 1.0f) &&
                ((fzgx_float_bits_exact(alternate_t) & 0x7f800000u) != 0x7f800000u)) {
              alternate_candidate = true;
              candidate_t = alternate_t;
            }
          }
        }
      }
    }
  if (previous_surface_match && !start_on_allowed_side &&
      !primary_candidate && ((dispatch_flags & 0x02000000u) != 0u)) {
    candidate_t = 0.0f;
    primary_candidate = true;
  }
  previous_or_end_on_allowed_side = previous_surface_match || end_on_allowed_side;
  if (enable_pipe_alternate && !primary_candidate && !alternate_candidate) {
      if (previous_or_end_on_allowed_side) {
        if ((previous_summary != 0) &&
            (current_summary_slot < previous_summary->candidate_count)) {
          uint32_t previous_flags = previous_summary->flags[current_summary_slot];

          can_traverse = 0u;
          if (state->summary_seed_slot_count == previous_summary->candidate_count) {
            branch_mask = 0x03e00c00u;
          } else {
            branch_mask = 0x03e00000u;
          }
          if (((previous_flags & 0x4u) != 0u) &&
              ((source_piece_word & branch_mask) == (source_piece_word & branch_mask & previous_flags)) &&
              ((previous_summary->piece_opaque[current_summary_slot] == track_segment->address) ||
               ((fzgx_track_course_can_traverse_checkpoint_interval(
                     state->course,
                     fzgx_track_summary_checkpoint_traverse_gap_exact,
                     previous_summary->checkpoint_index[current_summary_slot],
                     query->checkpoint_index,
                     &can_traverse) == FZGX_STATUS_OK) &&
                (can_traverse != 0u)))) {
            previous_alternate_match = true;
          }
        }
        if (!previous_alternate_match) {
          enable_pipe_alternate = false;
        }
      }
    if (enable_pipe_alternate && start_on_allowed_side &&
        ((dispatch_flags & 0x02000000u) != 0u)) {
      candidate_t = 0.0f;
      alternate_candidate = true;
    }
    }
  }

  if (alternate_candidate) {
    primary_candidate = true;
  }
  local_hit.z = local_end.z + candidate_t * (local_start.z - local_end.z);
  local_hit.x = local_end.x + candidate_t * (local_start.x - local_end.x);
  local_hit.y = local_end.y + candidate_t * (local_start.y - local_end.y);

  if (!thin_cross_section && primary_candidate) {
    if (!is_cylinder) {
      if (!(local_hit.y <= -open_cutoff_y)) {
        primary_candidate = false;
        alternate_candidate = false;
      } else if ((dispatch_flags & 0x04000000u) != 0u) {
        if (!(local_end.y <= -open_cutoff_y) || !previous_or_end_on_allowed_side) {
          primary_candidate = false;
          alternate_candidate = false;
        }
      }
    } else if (!(open_cutoff_y <= local_hit.y)) {
      primary_candidate = false;
      alternate_candidate = false;
    } else if (((dispatch_flags & 0x04000000u) != 0u) && (local_end.y < open_cutoff_y)) {
      primary_candidate = false;
      alternate_candidate = false;
    }
  }

  if (thin_cross_section &&
      (-radial_bias <= local_start.x) && (local_start.x <= radial_bias) &&
      !is_cylinder && (-open_cutoff_y < local_start.y)) {
    summary_active = true;
  }

  if (primary_candidate) {
    fzgx_track_transient_hit_exact *transient_hit = &primary_hit;
    bool cylinder_family = is_cylinder;

    if (alternate_candidate) {
      cylinder_family = !cylinder_family;
      radial_sign = -radial_sign;
      summary_active = false;
      transient_hit = &alternate_hit;
    }

    if (symmetric_scale) {
      world_hit_start = fzgx_transform_local_point(&state->transform, local_start_unscaled);
      world_hit_point = fzgx_vec3_add(
          request->end,
          fzgx_vec3_scale(fzgx_vec3_sub(world_hit_start, request->end), candidate_t));
    } else {
      world_hit_point = fzgx_vec3_add(
          request->end,
          fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), candidate_t));
    }

    if (thin_cross_section) {
      local_normal.x = 0.0f;
      local_normal.y = radial_sign;
      local_normal.z = 0.0f;
    } else {
      local_normal.x = radial_sign * local_hit.x;
      local_normal.y = radial_sign * local_hit.y;
      local_normal.z = 0.0f;
    }
    world_normal =
        fzgx_vec3_normalize_or_exact(
            fzgx_transform_local_vector(&state->transform, local_normal), fzgx_mat43_get_basis_y_exact(&state->transform));

    if (thin_cross_section) {
      float query_x_norm = fabsf(local_query_point.x) / (fzgx_generic_surface_push_bias_exact * state->scale.x);
      float cutoff_x_norm = sqrtf(fmaxf(0.0f, 1.0f - fminf(query_x_norm, 1.0f) * fminf(query_x_norm, 1.0f)));
      float cutoff_y_world =
          radial_sign * (fzgx_generic_surface_push_bias_exact * state->scale.y) * cutoff_x_norm;

      push_length = cutoff_y_world - local_start_unscaled.y;
    } else {
      start_radius = sqrtf(local_start.x * local_start.x + local_start.y * local_start.y);
      push_length = radial_sign * (radial_bias - start_radius);
    }
    if (push_length > 0.0f) {
      fzgx_vec3_set_length_exact(push_length, &local_normal, &local_normal);
      if (!thin_cross_section) {
        local_normal.x *= state->scale.x;
        local_normal.y *= state->scale.y;
      } else {
        local_normal.x *= state->scale.x;
      }
      world_push = fzgx_transform_local_vector(&state->transform, local_normal);
    }

    if (symmetric_scale) {
      float hit_distance = fzgx_vec3_length(fzgx_vec3_sub(request->end, world_hit_point));
      float sweep_distance = fzgx_vec3_length(fzgx_vec3_sub(request->end, request->start));

      if (sweep_distance > FLT_EPSILON) {
        candidate_t = hit_distance / sweep_distance;
      } else {
        candidate_t = 0.0f;
      }
      if ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) == 0x7f800000u) {
        candidate_t = 0.0f;
      }
    }

    transient_hit->valid = true;
    transient_hit->hit_time = candidate_t;
    transient_hit->hit_point = world_hit_point;
    transient_hit->aux_hit_point = world_hit_point;
    transient_hit->hit_normal = world_normal;
    transient_hit->push = world_push;
    transient_hit->result_flags = 0x00800000u | (cylinder_family ? 1u : 0u);
    result_family_flags = transient_hit->result_flags;
  }

  if (primary_hit.valid) {
    publish_candidate = fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
        FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT, primary_hit.hit_time);
    if (publish_candidate) {
      fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
          query,
          &primary_hit,
          fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
      if (result_inout != 0) {
        result_inout->checkpoint_index = query->checkpoint_index;
        result_inout->checkpoint_fraction = query->checkpoint_fraction;
        result_inout->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
        result_inout->selected_cached_frame_index = (int32_t)query->branch_selector;
      }
      if (fzgx_vec3_is_nonzero(primary_hit.push)) {
        fzgx_publish_contact_slot_exact(
            result_inout,
            1u,
            primary_hit.push,
            source_piece_word | 0x10000000u | 0x20000000u,
            fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot),
            primary_hit.hit_time);
      }
    }
  }
  if (alternate_hit.valid) {
    fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT,
        query,
        &alternate_hit,
        fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
  }

  if (enable_summary) {
    if (current_summary == 0) {
      return FZGX_STATUS_OK;
    }
    if (current_summary_slot > 3u) {
      current_summary_slot = 3u;
    }
    current_summary->piece_opaque[current_summary_slot] = track_segment->address;
    current_summary->width_scale[current_summary_slot] = state->scale.x;
    summary_flags = source_piece_word;
    if (alternate_candidate) {
      summary_flags |= 0x4u;
    }
    if (summary_active || primary_candidate) {
      summary_flags |= 0xf0000000u;
    }
    current_summary->flags[current_summary_slot] = summary_flags;
    current_summary->checkpoint_index[current_summary_slot] = query->checkpoint_index;
    current_summary->summary_flags |= summary_flags;
    if (current_summary->candidate_count < 3u) {
      current_summary->candidate_count += 1u;
    }
  }

  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_resolve_track_pipe_cylinder_contact_candidates_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  fzgx_track_side_query_summary *current_summary = 0;
  const fzgx_track_side_query_summary *previous_summary = 0;
  uint32_t source_piece_word = 0u;
  uint32_t current_summary_slot = 0u;
  fzgx_mat43 inverse_transform;
  fzgx_vec3 local_end;
  fzgx_vec3 local_start;
  fzgx_vec3 local_end_unscaled;
  fzgx_vec3 local_start_unscaled;
  fzgx_vec3 local_delta;
  fzgx_vec3 local_hit;
  fzgx_vec3 local_normal;
  fzgx_vec3 world_hit_start;
  fzgx_vec3 world_hit_point;
  fzgx_vec3 world_normal;
  fzgx_vec3 world_push = {0};
  float radial_sign = -1.0f;
  float radial_bias = fzgx_generic_surface_push_bias_exact;
  float discriminant;
  float denominator;
  float candidate_t = 0.0f;
  float start_radius_sq;
  float end_radius_sq;
  float start_radius;
  float push_length;
  uint32_t candidate_state = 0u;
  uint32_t summary_alt_state = 0u;
  uint32_t result_family_flags = 0u;
  uint32_t summary_flags = 0u;
  uint32_t branch_mask = 0x03e00000u;
  uint32_t special_material_flags = 0u;
  uint32_t can_traverse = 0u;
  bool enable_summary;
  bool is_cylinder;
  bool enable_pipe_alternate;
  bool symmetric_scale = false;
  bool start_on_allowed_side;
  bool end_on_allowed_side;
  bool previous_or_end_on_allowed_side;
  bool primary_summary_active = false;
  bool previous_surface_match = false;
  bool previous_alternate_match = false;
  bool reused_surface_match = false;
  bool publish_candidate = false;
  fzgx_track_transient_hit_exact primary_hit = {0};
  fzgx_track_transient_hit_exact alternate_hit = {0};
  fzgx_status status;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_apply_track_collision_node_transform_exact(
      query, track_segment, dispatch_flags, state, result_inout);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  enable_summary = (dispatch_flags & 0x4u) != 0u;
  is_cylinder = (source_piece_word & 0x1u) != 0u;
  enable_pipe_alternate = enable_summary && !is_cylinder;
  if (enable_summary) {
    previous_summary = fzgx_request_previous_side_summary_exact(request);
    current_summary = fzgx_request_current_side_summary_exact(request);
    if (current_summary != 0) {
      current_summary_slot = current_summary->candidate_count;
    }
  }
  if (is_cylinder) {
    radial_sign = 1.0f;
  }

  if ((dispatch_flags & 0x00200000u) != 0u) {
    float scale_ratio;

    if (fabsf(state->scale.x) > FLT_EPSILON) {
      scale_ratio = state->scale.y / state->scale.x;
      symmetric_scale =
          fabs((double)scale_ratio - 1.0) < fzgx_track_round_cross_section_scale_epsilon_exact;
    }
  }

  inverse_transform = state->transform;
  fzgx_mat43_rigid_invert_exact(&inverse_transform);
  local_end = fzgx_transform_local_point(&inverse_transform, request->end);
  if (symmetric_scale) {
    if (!is_cylinder) {
      float radius_sq = local_end.x * local_end.x + local_end.y * local_end.y;

      if (1.0f <= radius_sq) {
        float inv_radius = fzgx_math_rsqrt_exact(radius_sq);

        local_start.x =
            local_end.x + fzgx_track_round_cross_section_start_offset_exact * local_end.x * inv_radius;
        local_start.y =
            local_end.y + fzgx_track_round_cross_section_start_offset_exact * local_end.y * inv_radius;
        local_start.z = 0.0f;
      } else {
        symmetric_scale = false;
      }
    } else {
      local_start = (fzgx_vec3){0};
    }
  }
  if (!symmetric_scale) {
    local_start = fzgx_transform_local_point(&inverse_transform, request->start);
  }

  local_end_unscaled = local_end;
  local_start_unscaled = local_start;
  if (fabsf(state->scale.x) <= FLT_EPSILON || fabsf(state->scale.y) <= FLT_EPSILON) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  local_end.x /= state->scale.x;
  local_end.y /= state->scale.y;
  local_start.x /= state->scale.x;
  local_start.y /= state->scale.y;

  start_radius_sq = local_start.x * local_start.x + local_start.y * local_start.y;
  end_radius_sq = local_end.x * local_end.x + local_end.y * local_end.y;
  start_on_allowed_side =
      (radial_sign * (start_radius_sq - fzgx_track_round_cross_section_radius_squared_exact)) >= 0.0f;
  end_on_allowed_side =
      (radial_sign * (end_radius_sq - fzgx_track_round_cross_section_radius_squared_exact)) >= 0.0f;

  if ((previous_summary != 0) &&
      (current_summary_slot < previous_summary->candidate_count)) {
    uint32_t previous_flags = previous_summary->flags[current_summary_slot];

    if (state->summary_seed_slot_count == previous_summary->candidate_count) {
      branch_mask = 0x03e00c00u;
    }
    if (((previous_flags & 0x20000000u) != 0u) &&
        ((source_piece_word & branch_mask) == (source_piece_word & branch_mask & previous_flags)) &&
        ((previous_summary->piece_opaque[current_summary_slot] == track_segment->address) ||
         ((fzgx_track_course_can_traverse_checkpoint_interval(
               state->course,
               fzgx_track_summary_checkpoint_traverse_gap_exact,
               previous_summary->checkpoint_index[current_summary_slot],
               query->checkpoint_index,
               &can_traverse) == FZGX_STATUS_OK) &&
          (can_traverse != 0u)))) {
      previous_surface_match = true;
    }
  }

  local_delta.x = local_start.x - local_end.x;
  local_delta.y = local_start.y - local_end.y;
  local_delta.z = local_start.z - local_end.z;
  denominator = local_delta.x * local_delta.x + local_delta.y * local_delta.y;
  if (denominator > FLT_EPSILON) {
    float dot_term = local_delta.x * local_end.x + local_delta.y * local_end.y;

    discriminant =
        dot_term * dot_term -
        denominator *
            ((local_end.x * local_end.x + local_end.y * local_end.y) -
             fzgx_track_round_cross_section_radius_squared_exact);
    if (0.0f <= discriminant) {
      float sqrt_discriminant = sqrtf(discriminant);

      if ((fzgx_float_bits_exact(sqrt_discriminant) & 0x7f800000u) != 0x7f800000u) {
        candidate_t = -(dot_term + radial_sign * sqrt_discriminant) / denominator;
        if ((0.0f <= candidate_t) && (candidate_t <= 1.0f) &&
            ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) != 0x7f800000u)) {
          primary_summary_active = true;
          candidate_state = 0x20000000u;
        }

        if (enable_pipe_alternate && !previous_surface_match && (candidate_state == 0u)) {
          float alternate_t = -(dot_term - radial_sign * sqrt_discriminant) / denominator;

          if ((0.0f <= alternate_t) && (alternate_t <= 1.0f) &&
              ((fzgx_float_bits_exact(alternate_t) & 0x7f800000u) != 0x7f800000u)) {
            summary_alt_state = 0x02000000u;
            candidate_state = 0x02000000u;
            candidate_t = alternate_t;
          }
        }
      }
    }
  }

  if (previous_surface_match && !start_on_allowed_side &&
      ((candidate_state & 0x20000000u) == 0u) &&
      ((dispatch_flags & 0x02000000u) != 0u)) {
    candidate_state = 0x20000000u;
    candidate_t = 0.0f;
    primary_summary_active = true;
    reused_surface_match = true;
  }
  previous_or_end_on_allowed_side = previous_surface_match || end_on_allowed_side;

  if (enable_pipe_alternate && (candidate_state == 0u)) {
    if (previous_or_end_on_allowed_side) {
      if ((previous_summary != 0) &&
          (current_summary_slot < previous_summary->candidate_count)) {
        uint32_t previous_flags = previous_summary->flags[current_summary_slot];

        can_traverse = 0u;
        if (state->summary_seed_slot_count == previous_summary->candidate_count) {
          branch_mask = 0x03e00c00u;
        } else {
          branch_mask = 0x03e00000u;
        }
        if (((previous_flags & 0x4u) != 0u) &&
            ((source_piece_word & branch_mask) == (source_piece_word & branch_mask & previous_flags)) &&
            ((previous_summary->piece_opaque[current_summary_slot] == track_segment->address) ||
             ((fzgx_track_course_can_traverse_checkpoint_interval(
                   state->course,
                   fzgx_track_summary_checkpoint_traverse_gap_exact,
                   previous_summary->checkpoint_index[current_summary_slot],
                   query->checkpoint_index,
                   &can_traverse) == FZGX_STATUS_OK) &&
              (can_traverse != 0u)))) {
          previous_alternate_match = true;
        }
      }
      if (!previous_alternate_match) {
        enable_pipe_alternate = false;
      }
    }

    if (enable_pipe_alternate && start_on_allowed_side &&
        ((dispatch_flags & 0x02000000u) != 0u)) {
      summary_alt_state = 0x02000000u;
      candidate_state = 0x02000000u;
      candidate_t = 0.0f;
    }
  }

  if ((candidate_state & 0x20000000u) != 0u && !is_cylinder &&
      ((dispatch_flags & 0x04000000u) != 0u) && !previous_or_end_on_allowed_side) {
    candidate_state &= ~0x20000000u;
  }

  if (candidate_state != 0u) {
    fzgx_track_transient_hit_exact *transient_hit = &primary_hit;
    bool cylinder_family = is_cylinder;
    bool transient_reused_surface_match = reused_surface_match;

    local_hit.z = local_end.z + candidate_t * (local_start.z - local_end.z);
    local_hit.x = local_end.x + candidate_t * (local_start.x - local_end.x);
    local_hit.y = local_end.y + candidate_t * (local_start.y - local_end.y);
    if ((candidate_state & 0x02000000u) != 0u) {
      cylinder_family = true;
      radial_sign = -radial_sign;
      start_on_allowed_side = false;
      transient_reused_surface_match = false;
      transient_hit = &alternate_hit;
    }

    if (symmetric_scale) {
      world_hit_start = fzgx_transform_local_point(&state->transform, local_start_unscaled);
      world_hit_point = fzgx_vec3_add(
          request->end,
          fzgx_vec3_scale(fzgx_vec3_sub(world_hit_start, request->end), candidate_t));
    } else {
      world_hit_point = fzgx_vec3_add(
          request->end,
          fzgx_vec3_scale(fzgx_vec3_sub(request->start, request->end), candidate_t));
    }

    local_normal.x = radial_sign * local_hit.x;
    local_normal.y = radial_sign * local_hit.y;
    local_normal.z = 0.0f;
    world_normal =
        fzgx_vec3_normalize_or_exact(
            fzgx_transform_local_vector(&state->transform, local_normal), fzgx_mat43_get_basis_y_exact(&state->transform));

    start_radius = sqrtf(local_start.x * local_start.x + local_start.y * local_start.y);
    push_length = radial_sign * (radial_bias - start_radius);
    if (push_length > 0.0f) {
      fzgx_vec3_set_length_exact(push_length, &local_normal, &local_normal);
      local_normal.x *= state->scale.x;
      local_normal.y *= state->scale.y;
      world_push = fzgx_transform_local_vector(&state->transform, local_normal);
    }

    if (symmetric_scale) {
      float hit_distance = fzgx_vec3_length(fzgx_vec3_sub(request->end, world_hit_point));
      float sweep_distance = fzgx_vec3_length(fzgx_vec3_sub(request->end, request->start));

      if (sweep_distance > FLT_EPSILON) {
        candidate_t = hit_distance / sweep_distance;
      } else {
        candidate_t = 0.0f;
      }
      if ((fzgx_float_bits_exact(candidate_t) & 0x7f800000u) == 0x7f800000u) {
        candidate_t = 0.0f;
      }
    }

    transient_hit->valid = true;
    transient_hit->hit_time = candidate_t;
    transient_hit->hit_point = world_hit_point;
    transient_hit->aux_hit_point = world_hit_point;
    transient_hit->hit_normal = world_normal;
    transient_hit->push = world_push;
    transient_hit->result_flags = 0x01000000u | (cylinder_family ? 1u : 0u);
    result_family_flags = transient_hit->result_flags;
    reused_surface_match = transient_reused_surface_match;
    if ((transient_hit == &primary_hit) && ((dispatch_flags & 0x00200000u) != 0u)) {
      uint16_t pipe_angle = 0u;

      if (state->authored_track_id == 0x21u) {
        int32_t checkpoint_index = query->checkpoint_index;

        if ((((19 < checkpoint_index) && (checkpoint_index < 22)) ||
             ((checkpoint_index == 19) &&
              (fzgx_track_course33_pipe_material_cp19_min_fraction_exact <=
               query->checkpoint_fraction))) ||
            ((checkpoint_index == 22) &&
             (query->checkpoint_fraction <=
              fzgx_track_course33_pipe_material_cp22_max_fraction_exact))) {
          pipe_angle = fzgx_math_atan2_angle16(local_end.y, local_end.x);
          if ((0x17ff < (int16_t)pipe_angle) && ((int16_t)pipe_angle < 0x6801)) {
            special_material_flags = 0x00100000u;
          }
        } else if ((query->branch_selector == 2u) &&
                   (((37 < checkpoint_index) && (checkpoint_index < 40)) ||
                    ((checkpoint_index == 40) &&
                     (query->checkpoint_fraction <=
                      fzgx_track_course33_pipe_material_branch2_cp40_max_fraction_exact)))) {
          pipe_angle = fzgx_math_atan2_angle16(local_end.y, local_end.x);
          if ((0x17ff < (int16_t)pipe_angle) && ((int16_t)pipe_angle < 0x6801)) {
            special_material_flags = 0x00100000u;
          }
        }
      } else if ((state->authored_track_id == 0x23u) &&
                 (90 < query->checkpoint_index) && (query->checkpoint_index < 103)) {
        pipe_angle = fzgx_math_atan2_angle16(local_end.y, local_end.x);
        if ((0x5fffu < pipe_angle) && (pipe_angle < 0xa001u)) {
          special_material_flags = 0x00100000u;
        }
      }
    }
  }

  if (primary_hit.valid) {
    publish_candidate = fzgx_collision_scratch_current_track_hit_slot_accepts_exact(
        FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT, primary_hit.hit_time);
    if (publish_candidate) {
      fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_SLOT_OFFSET_EXACT,
          FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT,
          query,
          &primary_hit,
          fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
      fzgx_collision_scratch_or_u32_current_exact(
          FZGX_COLLISION_SCRATCH_OFFSET_0X1D4_EXACT, special_material_flags);
      fzgx_collision_scratch_or_u32_current_exact(
          FZGX_COLLISION_SCRATCH_MATERIAL_FLAGS_OFFSET_EXACT, special_material_flags);
      if (result_inout != 0) {
        result_inout->material_flags |= special_material_flags;
        result_inout->checkpoint_index = query->checkpoint_index;
        result_inout->checkpoint_fraction = query->checkpoint_fraction;
        result_inout->branch_flags = fzgx_collision_scratch_read_u32_current_exact(
            FZGX_COLLISION_SCRATCH_OFFSET_0X1C8_EXACT);
        result_inout->selected_cached_frame_index = (int32_t)query->branch_selector;
      }
      if (fzgx_vec3_is_nonzero(primary_hit.push)) {
        fzgx_publish_contact_slot_exact(
            result_inout,
            1u,
            primary_hit.push,
            source_piece_word | 0x10000000u | 0x20000000u,
            fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot),
            primary_hit.hit_time);
      }
    }
  }
  if (alternate_hit.valid) {
    fzgx_collision_scratch_current_write_track_hit_slot_from_transient_exact(
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_SLOT_OFFSET_EXACT,
        FZGX_COLLISION_SCRATCH_ALT_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT,
        query,
        &alternate_hit,
        fzgx_request_current_side_summary_slot_flags_ptr_exact(request, current_summary_slot));
  }

  if (enable_summary) {
    if (current_summary == 0) {
      return FZGX_STATUS_OK;
    }
    if (current_summary_slot > 3u) {
      current_summary_slot = 3u;
    }
    current_summary->piece_opaque[current_summary_slot] = track_segment->address;
    current_summary->width_scale[current_summary_slot] = state->scale.x;
    summary_flags = source_piece_word | ((summary_alt_state != 0u) ? 0x4u : 0u);
    if (start_on_allowed_side || primary_summary_active || (candidate_state != 0u)) {
      summary_flags |= 0xf0000000u;
    }
    current_summary->flags[current_summary_slot] = summary_flags;
    current_summary->checkpoint_index[current_summary_slot] = query->checkpoint_index;
    current_summary->summary_flags |= summary_flags;
    if (current_summary->candidate_count < 3u) {
      current_summary->candidate_count += 1u;
    }
  }

  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_walk_track_collision_piece_tree_exact(
    const fzgx_world_spherecast_request *request,
    const fzgx_track_sweep_query_exact *query,
    const fzgx_track_segment_record *track_segment,
    uint32_t branch_slot,
    uint32_t dispatch_flags,
    fzgx_track_piece_solver_state_exact *state,
    fzgx_world_spherecast_result *result_inout) {
  const fzgx_track_segment_record *children0 = 0;
  uint32_t child_count0 = 0u;
  uint32_t child_index0;
  fzgx_status status;
  fzgx_status best_status = FZGX_STATUS_UNIMPLEMENTED;

  if ((request == 0) || (query == 0) || (track_segment == 0) || (state == 0) || (result_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_course_get_track_segment_children(
      state->course, track_segment, &children0, &child_count0);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (child_count0 != 0u) {
    for (child_index0 = 0u; child_index0 < child_count0; ++child_index0) {
      const fzgx_track_segment_record *child0 = &children0[child_index0];
      uint32_t source_piece_word0 = 0u;

      if (!fzgx_track_piece_matches_branch_slot_exact(child0, branch_slot)) {
        continue;
      }
      status = fzgx_track_segment_build_source_piece_word(child0, &source_piece_word0);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((source_piece_word0 & 2u) == 0u) {
        fzgx_track_piece_solver_local_state_exact saved_state0 =
            fzgx_capture_track_piece_solver_local_state_exact(state);
        fzgx_status child_status0 = fzgx_dispatch_track_piece_solver_exact(
            request, query, child0, source_piece_word0, dispatch_flags, state, result_inout);
        if ((child_status0 != FZGX_STATUS_OK) && (child_status0 != FZGX_STATUS_UNIMPLEMENTED)) {
          return child_status0;
        }
        if (child_status0 == FZGX_STATUS_OK) {
          best_status = FZGX_STATUS_OK;
        }
        if (child0->children_count != 0u) {
          const fzgx_track_segment_record *children1 = 0;
          uint32_t child_count1 = 0u;
          uint32_t child_index1;

          status = fzgx_track_course_get_track_segment_children(
              state->course, child0, &children1, &child_count1);
          if (status != FZGX_STATUS_OK) {
            return status;
          }
          for (child_index1 = 0u; child_index1 < child_count1; ++child_index1) {
            const fzgx_track_segment_record *child1 = &children1[child_index1];
            uint32_t source_piece_word1 = 0u;

            if (!fzgx_track_piece_matches_branch_slot_exact(child1, branch_slot)) {
              continue;
            }
            status = fzgx_track_segment_build_source_piece_word(child1, &source_piece_word1);
            if (status != FZGX_STATUS_OK) {
              return status;
            }
            if ((source_piece_word1 & 2u) == 0u) {
              fzgx_track_piece_solver_local_state_exact saved_state1 =
                  fzgx_capture_track_piece_solver_local_state_exact(state);
              fzgx_status child_status1 = fzgx_dispatch_track_piece_solver_exact(
                  request, query, child1, source_piece_word1, dispatch_flags, state, result_inout);
              if ((child_status1 != FZGX_STATUS_OK) &&
                  (child_status1 != FZGX_STATUS_UNIMPLEMENTED)) {
                return child_status1;
              }
              if (child_status1 == FZGX_STATUS_OK) {
                best_status = FZGX_STATUS_OK;
              }
              if (child1->children_count != 0u) {
                const fzgx_track_segment_record *children2 = 0;
                uint32_t child_count2 = 0u;
                uint32_t child_index2;

                status = fzgx_track_course_get_track_segment_children(
                    state->course, child1, &children2, &child_count2);
                if (status != FZGX_STATUS_OK) {
                  return status;
                }
                for (child_index2 = 0u; child_index2 < child_count2; ++child_index2) {
                  const fzgx_track_segment_record *child2 = &children2[child_index2];
                  uint32_t source_piece_word2 = 0u;

                  if (!fzgx_track_piece_matches_branch_slot_exact(child2, branch_slot)) {
                    continue;
                  }
                  status = fzgx_track_segment_build_source_piece_word(child2, &source_piece_word2);
                  if (status != FZGX_STATUS_OK) {
                    return status;
                  }
                  if ((source_piece_word2 & 2u) == 0u) {
                    fzgx_track_piece_solver_local_state_exact saved_state2 =
                        fzgx_capture_track_piece_solver_local_state_exact(state);
                    fzgx_status child_status2 = fzgx_dispatch_track_piece_solver_exact(
                        request, query, child2, source_piece_word2, dispatch_flags, state, result_inout);
                    if ((child_status2 != FZGX_STATUS_OK) &&
                        (child_status2 != FZGX_STATUS_UNIMPLEMENTED)) {
                      return child_status2;
                    }
                    if (child_status2 == FZGX_STATUS_OK) {
                      best_status = FZGX_STATUS_OK;
                    }
                    if (child2->children_count != 0u) {
                      const fzgx_track_segment_record *children3 = 0;
                      uint32_t child_count3 = 0u;
                      uint32_t child_index3;

                      status = fzgx_track_course_get_track_segment_children(
                          state->course, child2, &children3, &child_count3);
                      if (status != FZGX_STATUS_OK) {
                        return status;
                      }
                      for (child_index3 = 0u; child_index3 < child_count3; ++child_index3) {
                        const fzgx_track_segment_record *child3 = &children3[child_index3];
                        uint32_t source_piece_word3 = 0u;

                        if (!fzgx_track_piece_matches_branch_slot_exact(child3, branch_slot)) {
                          continue;
                        }
                        status =
                            fzgx_track_segment_build_source_piece_word(child3, &source_piece_word3);
                        if (status != FZGX_STATUS_OK) {
                          return status;
                        }
                        if ((source_piece_word3 & 2u) == 0u) {
                          fzgx_track_piece_solver_local_state_exact saved_state3 =
                              fzgx_capture_track_piece_solver_local_state_exact(state);
                          fzgx_status child_status3 = fzgx_dispatch_track_piece_solver_exact(
                              request, query, child3, source_piece_word3, dispatch_flags, state, result_inout);
                          if ((child_status3 != FZGX_STATUS_OK) &&
                              (child_status3 != FZGX_STATUS_UNIMPLEMENTED)) {
                            return child_status3;
                          }
                          if (child_status3 == FZGX_STATUS_OK) {
                            best_status = FZGX_STATUS_OK;
                          }
                          if (child3->children_count != 0u) {
                            child_status3 = fzgx_walk_track_collision_piece_tree_exact(
                                request,
                                query,
                                child3,
                                branch_slot,
                                dispatch_flags,
                                state,
                                result_inout);
                            if ((child_status3 != FZGX_STATUS_OK) &&
                                (child_status3 != FZGX_STATUS_UNIMPLEMENTED)) {
                              return child_status3;
                            }
                            if (child_status3 == FZGX_STATUS_OK) {
                              best_status = FZGX_STATUS_OK;
                            }
                          }
                          fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state3);
                        }
                      }
                    }
                    fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state2);
                  }
                }
              }
              fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state1);
            }
          }
        }
        fzgx_restore_track_piece_solver_local_state_exact(state, &saved_state0);
      }
    }
  }
  return best_status;
}

static fzgx_mat43 fzgx_mat43_from_transform_trxs_exact(
    const fzgx_transform_trxs_record *transform_record) {
  fzgx_mat43 transform = fzgx_mat43_identity_exact();

  if (transform_record == 0) {
    return transform;
  }
  fzgx_mat43_set_origin_exact(&transform, transform_record->position);
  if (transform_record->rotation_z_angle16 != 0u) {
    fzgx_mat43_rotate_about_z_right(&transform, transform_record->rotation_z_angle16);
  }
  if (transform_record->rotation_y_angle16 != 0u) {
    fzgx_mat43_rotate_about_y_right(&transform, transform_record->rotation_y_angle16);
  }
  if (transform_record->rotation_x_angle16 != 0u) {
    fzgx_mat43_rotate_about_x_right(&transform, transform_record->rotation_x_angle16);
  }
  fzgx_mat43_set_basis_x_exact(&transform, fzgx_vec3_scale(fzgx_mat43_get_basis_x_exact(&transform), transform_record->scale.x));
  fzgx_mat43_set_basis_y_exact(&transform, fzgx_vec3_scale(fzgx_mat43_get_basis_y_exact(&transform), transform_record->scale.y));
  fzgx_mat43_set_basis_z_exact(&transform, fzgx_vec3_scale(fzgx_mat43_get_basis_z_exact(&transform), transform_record->scale.z));
  return transform;
}

static float fzgx_mat43_max_basis_scale_exact(const fzgx_mat43 *transform) {
  float scale_x;
  float scale_y;
  float scale_z;

  if (transform == 0) {
    return 0.0f;
  }
  scale_x = fzgx_vec3_length(fzgx_mat43_get_basis_x_exact(transform));
  scale_y = fzgx_vec3_length(fzgx_mat43_get_basis_y_exact(transform));
  scale_z = fzgx_vec3_length(fzgx_mat43_get_basis_z_exact(transform));
  return fmaxf(scale_x, fmaxf(scale_y, scale_z));
}

static float fzgx_mat43_min_basis_scale_exact(const fzgx_mat43 *transform) {
  float scale_x;
  float scale_y;
  float scale_z;

  if (transform == 0) {
    return 0.0f;
  }
  scale_x = fzgx_vec3_length(fzgx_mat43_get_basis_x_exact(transform));
  scale_y = fzgx_vec3_length(fzgx_mat43_get_basis_y_exact(transform));
  scale_z = fzgx_vec3_length(fzgx_mat43_get_basis_z_exact(transform));
  return fminf(scale_x, fminf(scale_y, scale_z));
}

static fzgx_status fzgx_sample_dynamic_scene_object_world_transform_exact(
    const fzgx_sim_world *world,
    uint32_t machine_index,
    uint32_t object_index,
    const fzgx_owned_dynamic_scene_object_record *object,
    fzgx_mat43 *transform_out) {
  fzgx_transform_trxs_record sampled_transform;

  if ((world == 0) || (object == 0) || (transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (object->has_transform_matrix != 0u) {
    *transform_out = object->transform_matrix;
    return FZGX_STATUS_OK;
  }

  sampled_transform = object->transform;
  if (object->has_animation_clip != 0u) {
    uint16_t mode_pair;
    uint32_t source_slot;
    float source_frames;
    float clip_time_seconds;
    float clip_start_frames;
    float clip_end_frames;
    float clip_span_frames;
    float clip_offset_frames;
    uint32_t flags = object->render_flags_0;

    mode_pair = (uint16_t)(((uint16_t)object->transform.unknown_transform_option << 8) |
                           (uint16_t)object->transform.object_active_override);
    if ((flags & 0x00040000u) == 0u) {
      source_slot = (uint32_t)((mode_pair >> 12) & 3u);
      source_frames = (float)world->stage_scene_frame_banks[source_slot];
    } else {
      uint32_t context_mask = world->stage_scene_context_mask;
      int32_t active_machine_index = world->stage_scene_context_active_machine_index;

      if ((flags & 0x000000e0u) != 0u) {
        if ((flags & context_mask) == 0u) {
          return FZGX_STATUS_OUT_OF_RANGE;
        }
      }
      if (active_machine_index < 0) {
        if (machine_index >= world->machine_count) {
          return FZGX_STATUS_OUT_OF_RANGE;
        }
        active_machine_index = (int32_t)machine_index;
      }
      if ((uint32_t)active_machine_index >= world->machine_count) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      source_slot = world->stage_scene_context_view_slot;
      if (source_slot >= 4u) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      if (object_index >= world->dynamic_scene_runtime_flag_count) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      source_frames = world->dynamic_scene_clip_bank_time_frames[object_index][source_slot];
      if (fzgx_float_bits_exact(source_frames) == 0xffffffffu) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      if ((int32_t)source_frames < 0) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
    }
    if ((mode_pair & 0x0080u) != 0u) {
      source_frames -= world->stage_scene_story_clip_offset_frames;
    }
    clip_start_frames = object->animation_clip.time_start_frames;
    clip_end_frames = object->animation_clip.time_end_frames;
    clip_span_frames = clip_end_frames - clip_start_frames;
    clip_offset_frames = source_frames - clip_start_frames;

    if (clip_span_frames > 0.0f) {
      if ((mode_pair & 1u) == 0u) {
        clip_offset_frames -= clip_span_frames * floorf(clip_offset_frames / clip_span_frames);
      } else if (clip_offset_frames > clip_span_frames) {
        clip_offset_frames = clip_span_frames;
      }
    } else {
      clip_offset_frames = 0.0f;
    }
    source_frames = clip_start_frames + clip_offset_frames;
    clip_time_seconds = source_frames / 60.0f;
    if (fzgx_dynamic_scene_object_sample_transform_trxs(
            object, clip_time_seconds, &sampled_transform) != FZGX_STATUS_OK) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
  }

  *transform_out = fzgx_mat43_from_transform_trxs_exact(&sampled_transform);
  return FZGX_STATUS_OK;
}

static fzgx_mat43 fzgx_resolve_dynamic_scene_object_collider_transform_exact(
    const fzgx_owned_dynamic_scene_object_record *object,
    const fzgx_mat43 *object_transform) {
  if ((object != 0) && (object->has_transform_matrix == 0u) &&
      (object->has_collision_transform != 0u)) {
    return fzgx_mat43_from_transform_trxs_exact(&object->collision_transform);
  }
  if (object_transform != 0) {
    return *object_transform;
  }
  return fzgx_mat43_identity_exact();
}

static uint32_t fzgx_find_terrain_and_objects_exact(
    fzgx_sim_world *world,
    fzgx_vec3 *inout_p0,
    const fzgx_vec3 *in_p1,
    uint32_t collision_mask,
    uint32_t machine_index) {
  uint32_t overlap_mask = 0u;
  fzgx_vec3 local_c4;

  if ((world == 0) || (inout_p0 == 0) || (in_p1 == 0)) {
    return 0u;
  }

  local_c4 = *inout_p0;
  if ((collision_mask & 0xffu) != 0u) {
    const fzgx_owned_track_mesh_course *course = world->track_mesh_course;

    if ((course != 0) && (course->chunk_count != 0u) && (course->chunks != 0)) {
      uint32_t chunk_index;

      for (chunk_index = 0u; chunk_index < course->chunk_count; ++chunk_index) {
        const fzgx_owned_track_mesh_chunk *chunk = &course->chunks[chunk_index];
        fzgx_vec3 local_ac = local_c4;
        fzgx_vec3 local_b8 = *in_p1;
        int32_t cell_x;
        int32_t cell_z;

        if ((chunk->cell_count == 0u) || (chunk->grid_subdiv_x <= 0) || (chunk->grid_subdiv_z <= 0) ||
            (chunk->inv_cell_size_x == 0.0f) || (chunk->inv_cell_size_z == 0.0f)) {
          continue;
        }
        if (chunk_index != 0u) {
          fzgx_mat43 current_transform;
          fzgx_mat43 previous_transform;
          fzgx_mat43 current_inverse;
          fzgx_mat43 previous_inverse;
          fzgx_vec3 current_position = chunk->unk_vec3_0x0;
          fzgx_vec3 previous_position = fzgx_vec3_sub(chunk->unk_vec3_0x0, chunk->unk_vec3_0x18);
          uint16_t current_rotation_x = chunk->rotation_x_angle16;
          uint16_t current_rotation_y = chunk->rotation_y_angle16;
          uint16_t current_rotation_z = chunk->rotation_z_angle16;
          uint16_t previous_rotation_x = chunk->rotation_x_angle16;
          uint16_t previous_rotation_y = chunk->rotation_y_angle16;
          uint16_t previous_rotation_z = chunk->rotation_z_angle16;

          if ((chunk->has_animation_record != 0u) && (chunk->animation_record.has_animation != 0u)) {
            float current_frame = (float)world->frame_index;
            float previous_frame = current_frame;
            float current_seconds;
            float previous_seconds;
            uint32_t channel_index;

            if ((chunk->flags_0x12 & 1u) == 0u) {
              float span_frames = chunk->time_end_0x10c - chunk->time_start_0x108;

              if (span_frames > 0.0f) {
                current_frame = (current_frame - span_frames * floorf(current_frame / span_frames)) +
                                chunk->time_start_0x108;
              }
            }
            if (world->frame_index != 0u) {
              previous_frame = (float)(world->frame_index - 1u);
              if ((chunk->flags_0x12 & 1u) == 0u) {
                float span_frames = chunk->time_end_0x10c - chunk->time_start_0x108;

                if (span_frames > 0.0f) {
                  previous_frame =
                      (previous_frame - span_frames * floorf(previous_frame / span_frames)) +
                      chunk->time_start_0x108;
                }
              }
            }
            current_seconds = current_frame / 60.0f;
            previous_seconds = previous_frame / 60.0f;
            for (channel_index = 0u; channel_index < FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT; ++channel_index) {
              const fzgx_owned_track_mesh_animation_channel *channel =
                  &chunk->animation_record.channels[channel_index];
              fzgx_animation_curve curve;
              float current_value;
              float previous_value;

              if ((channel->keyable_count == 0u) || (channel->keyables == 0)) {
                continue;
              }
              curve.keyable_count = channel->keyable_count;
              curve.keyables = channel->keyables;
              current_value = 0.0f;
              previous_value = 0.0f;
              if ((fzgx_evaluate_float_animation_curve(&curve, current_seconds, &current_value) !=
                   FZGX_STATUS_OK) ||
                  (fzgx_evaluate_float_animation_curve(&curve, previous_seconds, &previous_value) !=
                   FZGX_STATUS_OK)) {
                continue;
              }
              switch (channel_index) {
                case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_X:
                  current_rotation_x = fzgx_degrees_to_angle16_exact(current_value);
                  previous_rotation_x = fzgx_degrees_to_angle16_exact(previous_value);
                  break;
                case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Y:
                  current_rotation_y = fzgx_degrees_to_angle16_exact(current_value);
                  previous_rotation_y = fzgx_degrees_to_angle16_exact(previous_value);
                  break;
                case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Z:
                  current_rotation_z = fzgx_degrees_to_angle16_exact(current_value);
                  previous_rotation_z = fzgx_degrees_to_angle16_exact(previous_value);
                  break;
                case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_X:
                  current_position.x = current_value;
                  previous_position.x = previous_value - chunk->unk_vec3_0x18.x;
                  break;
                case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Y:
                  current_position.y = current_value;
                  previous_position.y = previous_value - chunk->unk_vec3_0x18.y;
                  break;
                case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Z:
                  current_position.z = current_value;
                  previous_position.z = previous_value - chunk->unk_vec3_0x18.z;
                  break;
              }
            }
          }

          current_transform = fzgx_mat43_identity_exact();
          fzgx_mat43_set_origin_exact(&current_transform, current_position);
          if (current_rotation_z != 0u) {
            fzgx_mat43_rotate_about_z_right(&current_transform, current_rotation_z);
          }
          if (current_rotation_y != 0u) {
            fzgx_mat43_rotate_about_y_right(&current_transform, current_rotation_y);
          }
          {
            int32_t relative_x =
                (int32_t)(int16_t)current_rotation_x - (int32_t)(int16_t)chunk->rotation_x_angle16;
            if (relative_x != 0) {
              fzgx_mat43_rotate_about_x_right(&current_transform, (uint16_t)relative_x);
            }
          }
          if (chunk->rotation_y_angle16 != 0u) {
            fzgx_mat43_rotate_about_y_right(
                &current_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_y_angle16));
          }
          if (chunk->rotation_z_angle16 != 0u) {
            fzgx_mat43_rotate_about_z_right(
                &current_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_z_angle16));
          }
          fzgx_mat43_translate_local_exact(
              &current_transform, fzgx_vec3_scale(chunk->unk_vec3_0x0, -1.0f));

          previous_transform = fzgx_mat43_identity_exact();
          fzgx_mat43_set_origin_exact(&previous_transform, previous_position);
          if (previous_rotation_z != 0u) {
            fzgx_mat43_rotate_about_z_right(&previous_transform, previous_rotation_z);
          }
          if (previous_rotation_y != 0u) {
            fzgx_mat43_rotate_about_y_right(&previous_transform, previous_rotation_y);
          }
          {
            int32_t relative_x =
                (int32_t)(int16_t)previous_rotation_x - (int32_t)(int16_t)chunk->rotation_x_angle16;
            if (relative_x != 0) {
              fzgx_mat43_rotate_about_x_right(&previous_transform, (uint16_t)relative_x);
            }
          }
          if (chunk->rotation_y_angle16 != 0u) {
            fzgx_mat43_rotate_about_y_right(
                &previous_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_y_angle16));
          }
          if (chunk->rotation_z_angle16 != 0u) {
            fzgx_mat43_rotate_about_z_right(
                &previous_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_z_angle16));
          }
          fzgx_mat43_translate_local_exact(
              &previous_transform, fzgx_vec3_scale(chunk->unk_vec3_0x0, -1.0f));

          current_inverse = current_transform;
          previous_inverse = previous_transform;
          fzgx_mat43_rigid_invert_exact(&current_inverse);
          fzgx_mat43_rigid_invert_exact(&previous_inverse);
          local_ac = fzgx_transform_local_point(&current_inverse, local_c4);
          local_b8 = fzgx_transform_local_point(&previous_inverse, *in_p1);
        }

        cell_x = (int32_t)((local_b8.x - chunk->grid_origin_x) / chunk->inv_cell_size_x);
        cell_z = (int32_t)((local_b8.z - chunk->grid_origin_z) / chunk->inv_cell_size_z);
        if ((0 <= cell_x) && (cell_x < chunk->grid_subdiv_x) &&
            (0 <= cell_z) && (cell_z < chunk->grid_subdiv_z)) {
          uint32_t cell_index = (uint32_t)(cell_x + cell_z * chunk->grid_subdiv_x);
          uint32_t pass_index;

          for (pass_index = 0u; pass_index < 2u; ++pass_index) {
            uint32_t class_index;

            for (class_index = 0u; class_index < 11u; ++class_index) {
              uint32_t hit_mask = 1u << class_index;
              const uint16_t *indices = 0;
              uint32_t count = 0u;
              uint32_t polygon_index;

              if ((collision_mask & hit_mask) == 0u) {
                continue;
              }
              if (pass_index == 0u) {
                if (fzgx_track_mesh_chunk_get_quad_cell(chunk, class_index, cell_index, &indices, &count) !=
                    FZGX_STATUS_OK) {
                  continue;
                }
              } else if (fzgx_track_mesh_chunk_get_tri_cell(chunk, class_index, cell_index, &indices, &count) !=
                         FZGX_STATUS_OK) {
                continue;
              }
              for (polygon_index = 0u; polygon_index < count; ++polygon_index) {
                fzgx_vec3 saved_local_ac = local_ac;

                if (pass_index == 0u) {
                  uint16_t quad_index = indices[polygon_index];

                  if ((quad_index < chunk->quad_count) &&
                      fzgx_capsule_intersects_convex_polygon_exact(
                          2.5f,
                          local_ac,
                          local_b8,
                          chunk->quads[quad_index].plane_distance,
                          chunk->quads[quad_index].normal,
                          (const fzgx_vec3[]){
                              chunk->quads[quad_index].vertex0,
                              chunk->quads[quad_index].vertex1,
                              chunk->quads[quad_index].vertex2,
                              chunk->quads[quad_index].vertex3,
                          },
                          (const fzgx_vec3[]){
                              chunk->quads[quad_index].edge_normal0,
                              chunk->quads[quad_index].edge_normal1,
                              chunk->quads[quad_index].edge_normal2,
                              chunk->quads[quad_index].edge_normal3,
                          },
                          4u)) {
                    overlap_mask |= hit_mask;
                    local_ac = saved_local_ac;
                  }
                } else {
                  uint16_t tri_index = indices[polygon_index];

                  if ((tri_index < chunk->tri_count) &&
                      fzgx_capsule_intersects_convex_polygon_exact(
                          2.5f,
                          local_ac,
                          local_b8,
                          chunk->tris[tri_index].plane_distance,
                          chunk->tris[tri_index].normal,
                          (const fzgx_vec3[]){
                              chunk->tris[tri_index].vertex0,
                              chunk->tris[tri_index].vertex1,
                              chunk->tris[tri_index].vertex2,
                          },
                          (const fzgx_vec3[]){
                              chunk->tris[tri_index].edge_normal0,
                              chunk->tris[tri_index].edge_normal1,
                              chunk->tris[tri_index].edge_normal2,
                          },
                          3u)) {
                    overlap_mask |= hit_mask;
                    local_ac = saved_local_ac;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if ((collision_mask & 0x4080u) != 0u) {
    const fzgx_owned_dynamic_scene_collision_course *course = world->dynamic_scene_collision_course;

    if (course != 0) {
      uint32_t object_index;

      for (object_index = 0u; object_index < course->object_count; ++object_index) {
        const fzgx_owned_dynamic_scene_object_record *object = &course->objects[object_index];
        const fzgx_owned_scene_object_collider_mesh *mesh;
        fzgx_mat43 transform;
        fzgx_mat43 collider_transform;
        uint32_t object_flags;
        uint16_t object_mode_mask;
        uint32_t query_flag;
        fzgx_vec3 local_ac;
        fzgx_vec3 local_b8;
        uint32_t i;

        object_flags = object->render_flags_0;
        if (object_index < world->dynamic_scene_runtime_flag_count) {
          object_flags |= world->dynamic_scene_runtime_flags[object_index];
        }
        if ((object_flags & world->scene_object_query_filter_mask) == 0u) {
          continue;
        }
        object_mode_mask = (uint16_t)(((uint16_t)object->transform.unknown_transform_option << 8) |
                                      (uint16_t)object->transform.object_active_override);
        if ((object_mode_mask & (uint16_t)world->scene_object_query_mode_mask) == 0u) {
          continue;
        }
        if ((object->has_collider_mesh == 0u) ||
            ((object->collider_mesh.collider_type & 0x4000u) == 0u)) {
          continue;
        }
        if ((object_flags & 0x40000000u) == 0u) {
          query_flag = collision_mask & 0x4000u;
        } else {
          query_flag = collision_mask & 0x80u;
        }
        if (query_flag == 0u) {
          continue;
        }
        if (fzgx_sample_dynamic_scene_object_world_transform_exact(
                world, machine_index, object_index, object, &transform) != FZGX_STATUS_OK) {
          continue;
        }
        mesh = &object->collider_mesh;
        if (fzgx_vec3_distance_squared_exact(
                fzgx_transform_local_point(&transform, mesh->bounding_sphere.origin),
                local_c4) >
            mesh->bounding_sphere.radius * fzgx_mat43_max_basis_scale_exact(&transform) *
                mesh->bounding_sphere.radius * fzgx_mat43_max_basis_scale_exact(&transform)) {
          continue;
        }
        collider_transform = fzgx_resolve_dynamic_scene_object_collider_transform_exact(object, &transform);
        local_ac = fzgx_world_point_to_local_scaled_orthogonal_exact(&collider_transform, local_c4);
        local_b8 = fzgx_world_point_to_local_scaled_orthogonal_exact(&collider_transform, *in_p1);
        for (i = 0u; i < mesh->quad_count; ++i) {
          fzgx_vec3 saved_local_ac = local_ac;

          if (fzgx_capsule_intersects_convex_polygon_exact(
                  2.5f,
                  local_ac,
                  local_b8,
                  mesh->quads[i].plane_distance,
                  mesh->quads[i].normal,
                  (const fzgx_vec3[]){
                      mesh->quads[i].vertex0,
                      mesh->quads[i].vertex1,
                      mesh->quads[i].vertex2,
                      mesh->quads[i].vertex3,
                  },
                  (const fzgx_vec3[]){
                      mesh->quads[i].edge_normal0,
                      mesh->quads[i].edge_normal1,
                      mesh->quads[i].edge_normal2,
                      mesh->quads[i].edge_normal3,
                  },
                  4u)) {
            overlap_mask |= query_flag;
            local_ac = saved_local_ac;
            if ((object_flags & 0x40000000u) == 0u) {
              *inout_p0 = fzgx_mat43_get_origin_exact(&transform);
              if (object_index < world->dynamic_scene_runtime_flag_count) {
                world->dynamic_scene_runtime_flags[object_index] |= 0x20000000u;
              }
            }
          }
        }
        for (i = 0u; i < mesh->tri_count; ++i) {
          fzgx_vec3 saved_local_ac = local_ac;

          if (fzgx_capsule_intersects_convex_polygon_exact(
                  2.5f,
                  local_ac,
                  local_b8,
                  mesh->tris[i].plane_distance,
                  mesh->tris[i].normal,
                  (const fzgx_vec3[]){
                      mesh->tris[i].vertex0,
                      mesh->tris[i].vertex1,
                      mesh->tris[i].vertex2,
                  },
                  (const fzgx_vec3[]){
                      mesh->tris[i].edge_normal0,
                      mesh->tris[i].edge_normal1,
                      mesh->tris[i].edge_normal2,
                  },
                  3u)) {
            overlap_mask |= query_flag;
            local_ac = saved_local_ac;
            if ((object_flags & 0x40000000u) == 0u) {
              *inout_p0 = fzgx_mat43_get_origin_exact(&transform);
              if (object_index < world->dynamic_scene_runtime_flag_count) {
                world->dynamic_scene_runtime_flags[object_index] |= 0x20000000u;
              }
            }
          }
        }
      }
    }
  }
  return overlap_mask;
}

static fzgx_status fzgx_spherecast_vs_world_with_piece_scratch_exact(
    fzgx_sim_world *world,
    const fzgx_world_spherecast_request *request,
    fzgx_track_side_query_buffer_exact *piece_scratch,
    fzgx_world_spherecast_result *result_out) {
  const fzgx_track_manifest *track_manifest;
  fzgx_status track_status = FZGX_STATUS_UNIMPLEMENTED;
  fzgx_status generic_status = FZGX_STATUS_UNIMPLEMENTED;
  fzgx_world_spherecast_request generic_request_storage;
  const fzgx_world_spherecast_request *generic_request = &generic_request_storage;
  fzgx_world_spherecast_result track_result;
  fzgx_world_spherecast_result generic_result;
  float obstacle_collision = 2.0f;
  int32_t winning_generic_mesh_chunk_index = -1;
  fzgx_mat43 winning_generic_chunk_transform = fzgx_mat43_identity_exact();

  if ((world == 0) || (request == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  fzgx_collision_scratch_reset_exact(world);
  memset(&track_result, 0, sizeof(track_result));
  memset(&generic_result, 0, sizeof(generic_result));
  if (result_out != 0) {
    memset(result_out, 0, sizeof(*result_out));
  }
  generic_request_storage = *request;
  if (request->machine_index < world->machine_count) {
    obstacle_collision = world->machines[request->machine_index].stat_obstacle_collision;
    fzgx_collision_scratch_write_f32_exact(
        world, 0x1b0u, world->machines[request->machine_index].stat_obstacle_collision);
    fzgx_collision_scratch_write_f32_exact(
        world, 0x1b4u, world->machines[request->machine_index].stat_track_collision);
  } else {
    fzgx_collision_scratch_write_f32_exact(world, 0x1b0u, 2.0f);
    fzgx_collision_scratch_write_f32_exact(world, 0x1b4u, 0.0f);
  }
  fzgx_collision_scratch_write_f32_exact(
      world,
      FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
          FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT,
      10.0f);
  generic_result.hit_time = 10.0f;

  if ((request->flags & 0x5u) != 0u) {
    track_status = fzgx_track_sweep_prepare_exact(
        world,
        request,
        piece_scratch,
        result_out != 0 ? &track_result : 0);
  }

  track_manifest = fzgx_get_active_track_manifest(world);
  if (((generic_request->flags & 0x04000000u) != 0u) &&
      (track_manifest != 0) &&
      (track_manifest->authored_track_id == 0x0fu)) {
    generic_request_storage.flags &= ~0x4u;
  }

  if ((generic_request->flags & 0x705u) != 0u) {
    if (world->track_mesh_course != 0) {
      const fzgx_owned_track_mesh_course *course = world->track_mesh_course;
      uint32_t machine_count = world->machine_count;
      uint32_t class_selector;
      generic_status = FZGX_STATUS_OK;
      if ((machine_count == 0u) || (4u < machine_count)) {
        machine_count = 1u;
      }
      if ((machine_count == 1u) && (track_manifest != 0) && (track_manifest->authored_track_id == 0x2au)) {
        class_selector = 2u;
      } else {
        class_selector = 1u << (machine_count - 1u);
      }

      if ((course->chunk_count != 0u) && (course->chunks != 0)) {
        uint32_t chunk_index;

        for (chunk_index = 0u; chunk_index < course->chunk_count; ++chunk_index) {
          const fzgx_owned_track_mesh_chunk *chunk = &course->chunks[chunk_index];
          fzgx_mat43 current_transform;
          fzgx_mat43 previous_transform;
          fzgx_mat43 current_inverse;
          fzgx_mat43 previous_inverse;
          fzgx_vec3 local_start = generic_request->start;
          fzgx_vec3 local_end = generic_request->end;
          int32_t cell_x;
          int32_t cell_z;
          uint32_t cell_index;
          uint32_t pass_index;

          if ((chunk->cell_count == 0u) || (chunk->grid_subdiv_x <= 0) || (chunk->grid_subdiv_z <= 0) ||
              (chunk->inv_cell_size_x == 0.0f) || (chunk->inv_cell_size_z == 0.0f)) {
            continue;
          }

          {
            fzgx_vec3 current_position = chunk->unk_vec3_0x0;
            fzgx_vec3 previous_position = fzgx_vec3_sub(chunk->unk_vec3_0x0, chunk->unk_vec3_0x18);
            uint16_t current_rotation_x = chunk->rotation_x_angle16;
            uint16_t current_rotation_y = chunk->rotation_y_angle16;
            uint16_t current_rotation_z = chunk->rotation_z_angle16;
            uint16_t previous_rotation_x = chunk->rotation_x_angle16;
            uint16_t previous_rotation_y = chunk->rotation_y_angle16;
            uint16_t previous_rotation_z = chunk->rotation_z_angle16;

            if ((chunk->has_animation_record != 0u) && (chunk->animation_record.has_animation != 0u)) {
              float current_frame = (float)world->frame_index;
              float previous_frame = current_frame;
              float current_seconds;
              float previous_seconds;
              uint32_t channel_index;

              if ((chunk->flags_0x12 & 1u) == 0u) {
                float span_frames = chunk->time_end_0x10c - chunk->time_start_0x108;

                if (span_frames > 0.0f) {
                  current_frame = (current_frame - span_frames * floorf(current_frame / span_frames)) +
                                  chunk->time_start_0x108;
                }
              }
              if (world->frame_index != 0u) {
                previous_frame = (float)(world->frame_index - 1u);
                if ((chunk->flags_0x12 & 1u) == 0u) {
                  float span_frames = chunk->time_end_0x10c - chunk->time_start_0x108;

                  if (span_frames > 0.0f) {
                    previous_frame =
                        (previous_frame - span_frames * floorf(previous_frame / span_frames)) +
                        chunk->time_start_0x108;
                  }
                }
              }
              current_seconds = current_frame / 60.0f;
              previous_seconds = previous_frame / 60.0f;

              for (channel_index = 0u; channel_index < FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT; ++channel_index) {
                const fzgx_owned_track_mesh_animation_channel *channel =
                    &chunk->animation_record.channels[channel_index];
                fzgx_animation_curve curve;
                float current_value;
                float previous_value;

                if ((channel->keyable_count == 0u) || (channel->keyables == 0)) {
                  continue;
                }
                curve.keyable_count = channel->keyable_count;
                curve.keyables = channel->keyables;
                current_value = 0.0f;
                previous_value = 0.0f;
                if ((fzgx_evaluate_float_animation_curve(&curve, current_seconds, &current_value) !=
                     FZGX_STATUS_OK) ||
                    (fzgx_evaluate_float_animation_curve(&curve, previous_seconds, &previous_value) !=
                     FZGX_STATUS_OK)) {
                  continue;
                }

                switch (channel_index) {
                  case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_X:
                    current_rotation_x = fzgx_degrees_to_angle16_exact(current_value);
                    previous_rotation_x = fzgx_degrees_to_angle16_exact(previous_value);
                    break;
                  case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Y:
                    current_rotation_y = fzgx_degrees_to_angle16_exact(current_value);
                    previous_rotation_y = fzgx_degrees_to_angle16_exact(previous_value);
                    break;
                  case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Z:
                    current_rotation_z = fzgx_degrees_to_angle16_exact(current_value);
                    previous_rotation_z = fzgx_degrees_to_angle16_exact(previous_value);
                    break;
                  case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_X:
                    current_position.x = current_value;
                    previous_position.x = previous_value - chunk->unk_vec3_0x18.x;
                    break;
                  case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Y:
                    current_position.y = current_value;
                    previous_position.y = previous_value - chunk->unk_vec3_0x18.y;
                    break;
                  case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Z:
                    current_position.z = current_value;
                    previous_position.z = previous_value - chunk->unk_vec3_0x18.z;
                    break;
                }
              }
            }

            current_transform = fzgx_mat43_identity_exact();
            fzgx_mat43_set_origin_exact(&current_transform, current_position);
            if (current_rotation_z != 0u) {
              fzgx_mat43_rotate_about_z_right(&current_transform, current_rotation_z);
            }
            if (current_rotation_y != 0u) {
              fzgx_mat43_rotate_about_y_right(&current_transform, current_rotation_y);
            }
            {
              int32_t relative_x =
                  (int32_t)(int16_t)current_rotation_x - (int32_t)(int16_t)chunk->rotation_x_angle16;
              if (relative_x != 0) {
                fzgx_mat43_rotate_about_x_right(&current_transform, (uint16_t)relative_x);
              }
            }
            if (chunk->rotation_y_angle16 != 0u) {
              fzgx_mat43_rotate_about_y_right(
                  &current_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_y_angle16));
            }
            if (chunk->rotation_z_angle16 != 0u) {
              fzgx_mat43_rotate_about_z_right(
                  &current_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_z_angle16));
            }
            fzgx_mat43_translate_local_exact(
                &current_transform, fzgx_vec3_scale(chunk->unk_vec3_0x0, -1.0f));

            previous_transform = fzgx_mat43_identity_exact();
            fzgx_mat43_set_origin_exact(&previous_transform, previous_position);
            if (previous_rotation_z != 0u) {
              fzgx_mat43_rotate_about_z_right(&previous_transform, previous_rotation_z);
            }
            if (previous_rotation_y != 0u) {
              fzgx_mat43_rotate_about_y_right(&previous_transform, previous_rotation_y);
            }
            {
              int32_t relative_x =
                  (int32_t)(int16_t)previous_rotation_x - (int32_t)(int16_t)chunk->rotation_x_angle16;
              if (relative_x != 0) {
                fzgx_mat43_rotate_about_x_right(&previous_transform, (uint16_t)relative_x);
              }
            }
            if (chunk->rotation_y_angle16 != 0u) {
              fzgx_mat43_rotate_about_y_right(
                  &previous_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_y_angle16));
            }
            if (chunk->rotation_z_angle16 != 0u) {
              fzgx_mat43_rotate_about_z_right(
                  &previous_transform, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_z_angle16));
            }
            fzgx_mat43_translate_local_exact(
                &previous_transform, fzgx_vec3_scale(chunk->unk_vec3_0x0, -1.0f));
          }
          if (chunk_index != 0u) {
            current_inverse = current_transform;
            previous_inverse = previous_transform;
            fzgx_mat43_rigid_invert_exact(&current_inverse);
            fzgx_mat43_rigid_invert_exact(&previous_inverse);
            local_start = fzgx_transform_local_point(&current_inverse, generic_request->start);
            local_end = fzgx_transform_local_point(&previous_inverse, generic_request->end);
          }

          cell_x = (int32_t)((local_end.x - chunk->grid_origin_x) / chunk->inv_cell_size_x);
          cell_z = (int32_t)((local_end.z - chunk->grid_origin_z) / chunk->inv_cell_size_z);
          if ((cell_x < 0) || (cell_x >= chunk->grid_subdiv_x) || (cell_z < 0) ||
              (cell_z >= chunk->grid_subdiv_z)) {
            continue;
          }
          cell_index = (uint32_t)(cell_x + cell_z * chunk->grid_subdiv_x);

          for (pass_index = 0u; pass_index < 2u; ++pass_index) {
            uint32_t request_class_index;

            if (winning_generic_mesh_chunk_index >= 0) {
              break;
            }
            for (request_class_index = 0u; request_class_index < 11u; ++request_class_index) {
              const uint16_t *indices = 0;
              uint32_t count = 0u;
              uint32_t class_index = request_class_index;
              uint32_t polygon_index;
              uint32_t hit_mask = 1u << request_class_index;
              bool accepted_hit = false;

              if ((generic_request->flags & hit_mask) == 0u) {
                continue;
              }
              if (request_class_index == 10u) {
                if (class_selector == 2u) {
                  class_index = 11u;
                } else if (class_selector == 4u) {
                  class_index = 12u;
                } else if (class_selector == 8u) {
                  class_index = 13u;
                }
              }

              if (pass_index == 0u) {
                if (fzgx_track_mesh_chunk_get_quad_cell(chunk, class_index, cell_index, &indices, &count) !=
                    FZGX_STATUS_OK) {
                  continue;
                }
              } else {
                if (fzgx_track_mesh_chunk_get_tri_cell(chunk, class_index, cell_index, &indices, &count) !=
                    FZGX_STATUS_OK) {
                  continue;
                }
              }

              for (polygon_index = 0u; polygon_index < count; ++polygon_index) {
                float hit_time;
                fzgx_vec3 hit_point;
                fzgx_vec3 hit_normal;
                bool hit = false;
                fzgx_vec3 segment_start_for_polygon = local_start;
                fzgx_vec3 segment_diff = fzgx_vec3_sub(local_start, local_end);

                if (pass_index == 0u) {
                  uint16_t quad_index = indices[polygon_index];
                  if (quad_index >= chunk->quad_count) {
                    continue;
                  }
                  if ((hit_mask & 0x700u) != 0u) {
                    const fzgx_static_collider_quad_record *quad = &chunk->quads[quad_index];
                    const fzgx_vec3 vertices[4] = {
                        quad->vertex0,
                        quad->vertex1,
                        quad->vertex2,
                        quad->vertex3,
                    };
                    const fzgx_vec3 edge_normals[4] = {
                        quad->edge_normal0,
                        quad->edge_normal1,
                        quad->edge_normal2,
                        quad->edge_normal3,
                    };

                    hit = fzgx_sweep_sphere_against_convex_polygon_exact(
                        obstacle_collision,
                        &segment_start_for_polygon,
                        &local_end,
                        &segment_diff,
                        &hit_time,
                        quad->plane_distance,
                        &quad->normal,
                        vertices,
                        edge_normals,
                        4u,
                        generic_request->flags);
                    hit_point = segment_start_for_polygon;
                  } else {
                    const fzgx_static_collider_quad_record *quad = &chunk->quads[quad_index];
                    const fzgx_vec3 vertices[4] = {
                        quad->vertex0,
                        quad->vertex1,
                        quad->vertex2,
                        quad->vertex3,
                    };
                    const fzgx_vec3 edge_normals[4] = {
                        quad->edge_normal0,
                        quad->edge_normal1,
                        quad->edge_normal2,
                        quad->edge_normal3,
                    };
                    float start_plane_distance =
                        fzgx_vec3_dot(quad->normal, local_start) + quad->plane_distance;
                    float end_plane_distance =
                        fzgx_vec3_dot(quad->normal, local_end) + quad->plane_distance;

                    hit = false;
                    if (0.0f < (end_plane_distance - start_plane_distance)) {
                      fzgx_vec3 segment_delta = fzgx_vec3_sub(local_end, local_start);
                      float denominator = end_plane_distance - start_plane_distance;

                      if (fabsf(denominator) > FLT_EPSILON) {
                        float polygon_hit_time = -start_plane_distance / denominator;

                        if ((0.0f <= polygon_hit_time) && (polygon_hit_time <= 1.0f) &&
                            ((fzgx_float_bits_exact(polygon_hit_time) & 0x7f800000u) !=
                             0x7f800000u)) {
                          fzgx_vec3 polygon_hit_point = fzgx_vec3_add(
                              local_start, fzgx_vec3_scale(segment_delta, polygon_hit_time));
                          bool inside_polygon = true;
                          uint32_t polygon_i;

                          for (polygon_i = 0u; polygon_i < 4u; ++polygon_i) {
                            fzgx_vec3 delta = fzgx_vec3_sub(polygon_hit_point, vertices[polygon_i]);
                            if (fzgx_vec3_dot(delta, edge_normals[polygon_i]) < 0.0f) {
                              inside_polygon = false;
                              break;
                            }
                          }
                          if (inside_polygon) {
                            hit_time = 1.0f - polygon_hit_time;
                            hit_point = polygon_hit_point;
                            hit = true;
                          }
                        }
                      }
                    }
                  }
                  hit_normal = chunk->quads[quad_index].normal;
                } else {
                  uint16_t tri_index = indices[polygon_index];
                  if (tri_index >= chunk->tri_count) {
                    continue;
                  }
                  if ((hit_mask & 0x700u) != 0u) {
                    const fzgx_static_collider_triangle_record *triangle = &chunk->tris[tri_index];
                    const fzgx_vec3 vertices[3] = {
                        triangle->vertex0,
                        triangle->vertex1,
                        triangle->vertex2,
                    };
                    const fzgx_vec3 edge_normals[3] = {
                        triangle->edge_normal0,
                        triangle->edge_normal1,
                        triangle->edge_normal2,
                    };

                    hit = fzgx_sweep_sphere_against_convex_polygon_exact(
                        obstacle_collision,
                        &segment_start_for_polygon,
                        &local_end,
                        &segment_diff,
                        &hit_time,
                        triangle->plane_distance,
                        &triangle->normal,
                        vertices,
                        edge_normals,
                        3u,
                        generic_request->flags);
                    hit_point = segment_start_for_polygon;
                  } else {
                    const fzgx_static_collider_triangle_record *triangle = &chunk->tris[tri_index];
                    const fzgx_vec3 vertices[3] = {
                        triangle->vertex0,
                        triangle->vertex1,
                        triangle->vertex2,
                    };
                    const fzgx_vec3 edge_normals[3] = {
                        triangle->edge_normal0,
                        triangle->edge_normal1,
                        triangle->edge_normal2,
                    };
                    float start_plane_distance =
                        fzgx_vec3_dot(triangle->normal, local_start) + triangle->plane_distance;
                    float end_plane_distance =
                        fzgx_vec3_dot(triangle->normal, local_end) + triangle->plane_distance;

                    hit = false;
                    if (0.0f < (end_plane_distance - start_plane_distance)) {
                      fzgx_vec3 segment_delta = fzgx_vec3_sub(local_end, local_start);
                      float denominator = end_plane_distance - start_plane_distance;

                      if (fabsf(denominator) > FLT_EPSILON) {
                        float polygon_hit_time = -start_plane_distance / denominator;

                        if ((0.0f <= polygon_hit_time) && (polygon_hit_time <= 1.0f) &&
                            ((fzgx_float_bits_exact(polygon_hit_time) & 0x7f800000u) !=
                             0x7f800000u)) {
                          fzgx_vec3 polygon_hit_point = fzgx_vec3_add(
                              local_start, fzgx_vec3_scale(segment_delta, polygon_hit_time));
                          bool inside_polygon = true;
                          uint32_t polygon_i;

                          for (polygon_i = 0u; polygon_i < 3u; ++polygon_i) {
                            fzgx_vec3 delta = fzgx_vec3_sub(polygon_hit_point, vertices[polygon_i]);
                            if (fzgx_vec3_dot(delta, edge_normals[polygon_i]) < 0.0f) {
                              inside_polygon = false;
                              break;
                            }
                          }
                          if (inside_polygon) {
                            hit_time = 1.0f - polygon_hit_time;
                            hit_point = polygon_hit_point;
                            hit = true;
                          }
                        }
                      }
                    }
                  }
                  hit_normal = chunk->tris[tri_index].normal;
                }

                if (!hit) {
                  continue;
                }
                if (hit_time <= generic_result.hit_time) {
                  generic_result.has_hit = true;
                  generic_result.hit_time = hit_time;
                  generic_result.hit_point = hit_point;
                  generic_result.aux_hit_point = hit_point;
                  generic_result.hit_normal = hit_normal;
                  generic_result.result_flags = hit_mask;
                  generic_result.surface_flags = hit_mask;
                  winning_generic_mesh_chunk_index = (int32_t)chunk_index;
                  if (chunk_index != 0u) {
                    winning_generic_chunk_transform = current_transform;
                  }
                  accepted_hit = true;
                }
                if (accepted_hit) {
                  break;
                }
              }
              if (winning_generic_mesh_chunk_index >= 0) {
                break;
              }
            }
          }
        }
      }
    }

  }

  if ((generic_request->flags & 0x3800u) != 0u &&
      (world->dynamic_scene_collision_course != 0) &&
      ((track_manifest == 0) || (track_manifest->authored_track_id != 0x0au) ||
       ((0x7e < generic_request->checkpoint_seed_index) &&
        (generic_request->checkpoint_seed_index < 0x8fu)))) {
    const fzgx_owned_dynamic_scene_collision_course *course = world->dynamic_scene_collision_course;
    uint32_t object_index;

    generic_status = FZGX_STATUS_OK;
    for (object_index = 0u; object_index < course->object_count; ++object_index) {
      const fzgx_owned_dynamic_scene_object_record *object = &course->objects[object_index];
      uint32_t object_flags;
      uint16_t object_mode_mask;
      uint32_t collider_mask;
      uint32_t requested_mask;
      fzgx_mat43 transform;
      fzgx_vec3 sphere_center;
      float distance_sq;
      float max_scale;
      float bound_radius;
      uint32_t tri_index;
      uint32_t quad_index;

      object_flags = object->render_flags_0;
      if (object_index < world->dynamic_scene_runtime_flag_count) {
        object_flags |= world->dynamic_scene_runtime_flags[object_index];
      }
      collider_mask = object->collider_mesh.collider_type & 0x3800u;
      requested_mask = collider_mask & generic_request->flags;
      object_mode_mask = (uint16_t)(((uint16_t)object->transform.unknown_transform_option << 8) |
                                    (uint16_t)object->transform.object_active_override);
      if ((object_flags & world->scene_object_query_filter_mask) == 0u) {
        continue;
      }
      if ((object_mode_mask & (uint16_t)world->scene_object_query_mode_mask) == 0u) {
        continue;
      }
      if (object->has_collider_mesh == 0u) {
        continue;
      }
      if (requested_mask == 0u) {
        continue;
      }
      if (fzgx_sample_dynamic_scene_object_world_transform_exact(
              world, generic_request->machine_index, object_index, object, &transform) !=
          FZGX_STATUS_OK) {
        continue;
      }
      sphere_center =
          fzgx_transform_local_point(&transform, object->collider_mesh.bounding_sphere.origin);
      distance_sq = fzgx_vec3_length_squared(fzgx_vec3_sub(sphere_center, generic_request->start));
      max_scale = fzgx_mat43_max_basis_scale_exact(&transform);
      bound_radius = object->collider_mesh.bounding_sphere.radius * max_scale;
      if (distance_sq > (bound_radius * bound_radius)) {
        continue;
      }

      {
        fzgx_mat43 collider_transform =
            fzgx_resolve_dynamic_scene_object_collider_transform_exact(object, &transform);
        float collision_scale = fzgx_mat43_min_basis_scale_exact(&collider_transform);
        float collision_radius = obstacle_collision;
        fzgx_vec3 local_start =
            fzgx_world_point_to_local(&collider_transform, generic_request->start);
        fzgx_vec3 local_end =
            fzgx_world_point_to_local(&collider_transform, generic_request->end);

        if (collision_scale > 0.0f) {
          collision_radius /= collision_scale;
        }

        for (tri_index = 0u; tri_index < object->collider_mesh.tri_count; ++tri_index) {
          fzgx_vec3 hit_point = local_start;
          fzgx_vec3 hit_normal;
          fzgx_vec3 diff = fzgx_vec3_sub(local_start, local_end);
          float hit_time;
          fzgx_vec3 plane_normal = object->collider_mesh.tris[tri_index].normal;
          float plane_distance = object->collider_mesh.tris[tri_index].plane_distance;
          fzgx_vec3 vertices[3] = {
              object->collider_mesh.tris[tri_index].vertex0,
              object->collider_mesh.tris[tri_index].vertex1,
              object->collider_mesh.tris[tri_index].vertex2,
          };
          fzgx_vec3 edge_normals[3] = {
              object->collider_mesh.tris[tri_index].edge_normal0,
              object->collider_mesh.tris[tri_index].edge_normal1,
              object->collider_mesh.tris[tri_index].edge_normal2,
          };

          if (!fzgx_sweep_sphere_against_convex_polygon_exact(
                  collision_radius,
                  &hit_point,
                  &local_end,
                  &diff,
                  &hit_time,
                  plane_distance,
                  &plane_normal,
                  vertices,
                  edge_normals,
                  3u,
                  generic_request->flags)) {
            continue;
          }
          hit_point = fzgx_transform_local_point(&collider_transform, hit_point);
          hit_normal = fzgx_transform_local_vector(&collider_transform, plane_normal);
          hit_normal = fzgx_vec3_normalize_or_exact(hit_normal, (fzgx_vec3){0.0f, 1.0f, 0.0f});
          if (hit_time <= generic_result.hit_time) {
            generic_result.has_hit = true;
            generic_result.hit_time = hit_time;
            generic_result.hit_point = hit_point;
            generic_result.aux_hit_point = hit_point;
            generic_result.hit_normal = hit_normal;
            generic_result.result_flags = collider_mask;
            generic_result.surface_flags = collider_mask;
            winning_generic_mesh_chunk_index = 0;
          }
        }
        for (quad_index = 0u; quad_index < object->collider_mesh.quad_count; ++quad_index) {
          fzgx_vec3 hit_point = local_start;
          fzgx_vec3 hit_normal;
          fzgx_vec3 diff = fzgx_vec3_sub(local_start, local_end);
          float hit_time;
          fzgx_vec3 plane_normal = object->collider_mesh.quads[quad_index].normal;
          float plane_distance = object->collider_mesh.quads[quad_index].plane_distance;
          fzgx_vec3 vertices[4] = {
              object->collider_mesh.quads[quad_index].vertex0,
              object->collider_mesh.quads[quad_index].vertex1,
              object->collider_mesh.quads[quad_index].vertex2,
              object->collider_mesh.quads[quad_index].vertex3,
          };
          fzgx_vec3 edge_normals[4] = {
              object->collider_mesh.quads[quad_index].edge_normal0,
              object->collider_mesh.quads[quad_index].edge_normal1,
              object->collider_mesh.quads[quad_index].edge_normal2,
              object->collider_mesh.quads[quad_index].edge_normal3,
          };

          if (!fzgx_sweep_sphere_against_convex_polygon_exact(
                  collision_radius,
                  &hit_point,
                  &local_end,
                  &diff,
                  &hit_time,
                  plane_distance,
                  &plane_normal,
                  vertices,
                  edge_normals,
                  4u,
                  generic_request->flags)) {
            continue;
          }
          hit_point = fzgx_transform_local_point(&collider_transform, hit_point);
          hit_normal = fzgx_transform_local_vector(&collider_transform, plane_normal);
          hit_normal = fzgx_vec3_normalize_or_exact(hit_normal, (fzgx_vec3){0.0f, 1.0f, 0.0f});
          if (hit_time <= generic_result.hit_time) {
            generic_result.has_hit = true;
            generic_result.hit_time = hit_time;
            generic_result.hit_point = hit_point;
            generic_result.aux_hit_point = hit_point;
            generic_result.hit_normal = hit_normal;
            generic_result.result_flags = collider_mask;
            generic_result.surface_flags = collider_mask;
            winning_generic_mesh_chunk_index = 0;
          }
        }
      }
    }
  }

  if ((winning_generic_mesh_chunk_index > 0) && generic_result.has_hit) {
    generic_result.hit_point =
        fzgx_transform_local_point(&winning_generic_chunk_transform, generic_result.hit_point);
    generic_result.hit_normal =
        fzgx_transform_local_vector(&winning_generic_chunk_transform, generic_result.hit_normal);
    generic_result.hit_normal = fzgx_vec3_normalize_or_exact(
        generic_result.hit_normal, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  }

  if ((track_status == FZGX_STATUS_OK) || (generic_status == FZGX_STATUS_OK)) {
    fzgx_world_spherecast_result effective_track_result;
    bool generic_selected = false;

    memset(&effective_track_result, 0, sizeof(effective_track_result));
    if (track_status == FZGX_STATUS_OK) {
      if (fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT) != 0u) {
        fzgx_collision_scratch_write_s32_exact(
            world,
            FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT,
            fzgx_collision_scratch_read_s32_exact(
                world, FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT + 0x0u));
        fzgx_collision_scratch_write_s32_exact(
            world,
            FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT,
            fzgx_collision_scratch_read_s32_exact(
                world, FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT + 0x4u));
        fzgx_collision_scratch_write_f32_exact(
            world,
            FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT,
            fzgx_collision_scratch_read_f32_exact(
                world, FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT + 0x8u));
      } else {
        fzgx_collision_scratch_write_s32_exact(
            world,
            FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT,
            fzgx_collision_scratch_read_s32_exact(
                world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_BRANCH_SELECTOR_OFFSET_EXACT));
        fzgx_collision_scratch_write_s32_exact(
            world,
            FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT,
            fzgx_collision_scratch_read_s32_exact(
                world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_CHECKPOINT_INDEX_OFFSET_EXACT));
        fzgx_collision_scratch_write_f32_exact(
            world,
            FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT,
            fzgx_collision_scratch_read_f32_exact(
                world, FZGX_COLLISION_SCRATCH_TRACK_QUERY_CHECKPOINT_FRACTION_OFFSET_EXACT));
      }
      effective_track_result = track_result;
    }
    if ((generic_status == FZGX_STATUS_OK) && generic_result.has_hit) {
      float push_length = fabsf(fzgx_vec3_dot(
          fzgx_vec3_sub(generic_request->start, generic_result.hit_point),
          generic_result.hit_normal));

      fzgx_collision_scratch_write_u32_exact(
          world,
          FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT,
          generic_result.result_flags);
      fzgx_collision_scratch_write_f32_exact(
          world,
          FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
              FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT,
          generic_result.hit_time);
      fzgx_collision_scratch_write_vec3_exact(
          world,
          FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
              FZGX_COLLISION_SCRATCH_HIT_SLOT_POINT_OFFSET_EXACT,
          generic_result.hit_point);
      fzgx_collision_scratch_write_vec3_exact(
          world,
          FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
              FZGX_COLLISION_SCRATCH_HIT_SLOT_NORMAL_OFFSET_EXACT,
          generic_result.hit_normal);
      if ((generic_result.result_flags & 0x3804u) != 0u) {
        push_length += fzgx_generic_surface_push_bias_exact;
      }
      fzgx_collision_scratch_write_vec3_exact(
          world,
          FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
              FZGX_COLLISION_SCRATCH_HIT_SLOT_PUSH_OFFSET_EXACT,
          fzgx_vec3_scale(generic_result.hit_normal, push_length));
    }
    if ((track_status == FZGX_STATUS_OK) && generic_result.has_hit &&
        (fzgx_collision_scratch_read_u32_exact(
             world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT) != 0u) &&
        (fzgx_collision_scratch_read_f32_exact(
             world,
             FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
                 FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT) < generic_result.hit_time)) {
      fzgx_collision_scratch_write_u32_exact(
          world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT, 0u);
    }
    if ((generic_status == FZGX_STATUS_OK) &&
        (fzgx_collision_scratch_read_u32_exact(
             world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT) != 0u) &&
        (!effective_track_result.has_hit ||
         (generic_result.hit_time <= effective_track_result.hit_time))) {
      generic_selected = true;
      fzgx_collision_scratch_write_vec3_exact(
          world, FZGX_COLLISION_SCRATCH_HIT_NORMAL_OFFSET_EXACT, generic_result.hit_normal);
    }
    if (generic_selected) {
      fzgx_collision_scratch_write_u32_exact(
          world, FZGX_COLLISION_SCRATCH_HIT_INFO_FLAGS_OFFSET_EXACT, generic_result.result_flags);
      fzgx_collision_scratch_write_u32_exact(
          world, FZGX_COLLISION_SCRATCH_SURFACE_FLAGS_OFFSET_EXACT, generic_result.result_flags);
    } else if (effective_track_result.has_hit) {
      fzgx_collision_scratch_write_u32_exact(
          world, FZGX_COLLISION_SCRATCH_SURFACE_FLAGS_OFFSET_EXACT, effective_track_result.result_flags);
    }
    if (result_out != 0) {
      memset(result_out, 0, sizeof(*result_out));
      if (track_status == FZGX_STATUS_OK) {
        *result_out = effective_track_result;
      }
      if (generic_selected) {
        *result_out = generic_result;
        if (track_status == FZGX_STATUS_OK) {
          result_out->checkpoint_index = effective_track_result.checkpoint_index;
          result_out->checkpoint_fraction = effective_track_result.checkpoint_fraction;
          result_out->branch_flags = effective_track_result.branch_flags;
          if (effective_track_result.cached_frame_count != 0u) {
            result_out->selected_cached_frame_index =
                effective_track_result.selected_cached_frame_index;
            result_out->cached_frame_count = effective_track_result.cached_frame_count;
            result_out->has_cached_frame_exports = effective_track_result.has_cached_frame_exports;
            if (effective_track_result.has_cached_frame_exports) {
              memcpy(
                  result_out->cached_frames,
                  effective_track_result.cached_frames,
                  sizeof(result_out->cached_frames));
            }
          }
        }
      }
    }
    return FZGX_STATUS_OK;
  }
  if ((track_status != FZGX_STATUS_UNIMPLEMENTED) && (track_status != FZGX_STATUS_OK)) {
    return track_status;
  }
  if ((generic_status != FZGX_STATUS_UNIMPLEMENTED) && (generic_status != FZGX_STATUS_OK)) {
    return generic_status;
  }
  return FZGX_STATUS_UNIMPLEMENTED;
}

static fzgx_status fzgx_spherecast_vs_world_exact(
    fzgx_sim_world *world,
    const fzgx_world_spherecast_request *request,
    fzgx_world_spherecast_result *result_out) {
  return fzgx_spherecast_vs_world_with_piece_scratch_exact(world, request, 0, result_out);
}

void fzgx_sim_world_init(fzgx_sim_world *world) {
  if (world == 0) {
    return;
  }
  memset(world, 0, sizeof(*world));
  world->api_version = FZGX_SIM_API_VERSION;
  world->machine_capacity = FZGX_SIM_MAX_MACHINES;
  world->stage_scene_context_mask = 0x00000080u;
  world->stage_scene_context_active_machine_index = -1;
  world->scene_object_query_filter_mask = 0x00000001u;
  world->scene_object_query_mode_mask = 0x00000f00u;
}

fzgx_status fzgx_sim_world_configure(
    fzgx_sim_world *world,
    const fzgx_sim_world_config *config,
    const fzgx_content_bundle *content) {
  if ((world == 0) || (config == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (config->api_version != FZGX_SIM_API_VERSION) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((config->machine_capacity == 0u) || (config->machine_capacity > FZGX_SIM_MAX_MACHINES)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (content != 0) {
    fzgx_status status = fzgx_content_bundle_validate(content);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  memset(world, 0, sizeof(*world));
  world->api_version = FZGX_SIM_API_VERSION;
  world->machine_capacity = config->machine_capacity;
  world->stage_scene_context_mask = 0x00000080u;
  world->stage_scene_context_active_machine_index = -1;
  world->scene_object_query_filter_mask = 0x00000001u;
  world->scene_object_query_mode_mask = 0x00000f00u;
  world->content = content;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_spherecast_callback(
    fzgx_sim_world *world,
    fzgx_world_spherecast_fn spherecast_fn,
    void *userdata) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  world->spherecast_fn = spherecast_fn;
  world->spherecast_userdata = userdata;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_static_collider_course(
    fzgx_sim_world *world,
    const fzgx_owned_static_collider_course *course) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  world->static_collider_course = course;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_track_mesh_course(
    fzgx_sim_world *world,
    const fzgx_owned_track_mesh_course *course) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  world->track_mesh_course = course;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_dynamic_scene_collision_course(
    fzgx_sim_world *world,
    const fzgx_owned_dynamic_scene_collision_course *course) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((course != 0) && (course->object_count > FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  world->dynamic_scene_collision_course = course;
  world->dynamic_scene_runtime_flag_count = (course != 0) ? course->object_count : 0u;
  memset(world->dynamic_scene_runtime_flags, 0, sizeof(world->dynamic_scene_runtime_flags));
  memset(world->dynamic_scene_clip_bank_time_frames, 0, sizeof(world->dynamic_scene_clip_bank_time_frames));
  if (course != 0) {
    uint32_t object_index;

    for (object_index = 0u; object_index < course->object_count; ++object_index) {
      const fzgx_owned_dynamic_scene_object_record *object = &course->objects[object_index];

      if (object->has_animation_clip == 0u) {
        continue;
      }
      memcpy(
          world->dynamic_scene_clip_bank_time_frames[object_index],
          object->animation_clip.bank_time_frames,
          sizeof(world->dynamic_scene_clip_bank_time_frames[object_index]));
    }
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_debug_exact_spherecast(
    fzgx_sim_world *world,
    const fzgx_world_spherecast_request *request,
    fzgx_track_side_query_buffer_exact *piece_scratch,
    fzgx_world_spherecast_result *result_out) {
  fzgx_status status = fzgx_validate_world(world);

  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((request == 0) || (result_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_spherecast_vs_world_with_piece_scratch_exact(world, request, piece_scratch, result_out);
  return status;
}

fzgx_status fzgx_sim_world_set_race_full_heal_latch(
    fzgx_sim_world *world,
    bool enabled) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  world->race_full_heal_latch_active = enabled ? 1u : 0u;
  world->race_full_heal_latch_persistent = enabled ? 1u : 0u;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_race_mode(fzgx_sim_world *world, uint32_t race_mode) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  world->race_mode = race_mode;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_track(fzgx_sim_world *world, uint32_t track_index) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (world->content == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  if (track_index >= world->content->track_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  world->active_track_index = track_index;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_build_ordinary_start_grid_slot_transform_exact(
    const fzgx_sim_world *world,
    uint32_t slot_index,
    float *track_width_or_radius_out,
    fzgx_mat43 *transform_out) {
  static const float fzgx_start_grid_base_z_exact[5] = {
      19.0f,
      15.0f,
      19.0f,
      19.0f,
      19.0f,
  };
  static const float fzgx_start_grid_step_z_exact[5] = {
      12.899999618530273f,
      13.0f,
      13.0f,
      13.0f,
      13.0f,
  };
  static const uint8_t fzgx_start_grid_lane_count_exact[5] = {
      6u,
      1u,
      4u,
      6u,
      2u,
  };
  static const float fzgx_start_grid_lane_scale_exact[5] = {
      0.5f,
      0.5f,
      0.5f,
      0.5f,
      0.2f,
  };
  static const float fzgx_start_grid_lane_sign_exact[5] = {
      1.0f,
      1.0f,
      -1.0f,
      -1.0f,
      1.0f,
  };
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  const fzgx_track_manifest *track_manifest;
  const uint8_t *scratch_raw;
  fzgx_vec3 query_point = {0};
  uint32_t layout_index = 0u;
  uint32_t lane_count;
  int32_t checkpoint_index = 0;
  int32_t lane_divisor;
  int32_t lane_index;
  float checkpoint_fraction = 0.0f;
  float lateral_offset_x = 0.0f;
  float track_width_or_radius;
  fzgx_status status;

  if ((world == 0) || (transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (world->content == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  track_manifest = fzgx_get_active_track_manifest(world);
  if (track_manifest == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  status = fzgx_get_active_track_course(world, &course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      world->content, world->active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = 0;
  } else if (status != FZGX_STATUS_OK) {
    return status;
  }

  if ((world->race_mode == fzgx_mode_story_exact) &&
      (track_manifest->authored_track_id == 37u)) {
    layout_index = 1u;
  } else if ((world->race_mode == fzgx_mode_story_exact) &&
             (track_manifest->authored_track_id == 39u)) {
    layout_index = 2u;
  } else if ((world->race_mode == fzgx_mode_story_exact) &&
             (track_manifest->authored_track_id == 43u)) {
    layout_index = 3u;
  } else if (track_manifest->authored_track_id == 45u) {
    layout_index = 4u;
  }
  query_point.z = fzgx_start_grid_base_z_exact[layout_index] +
                  (float)slot_index * fzgx_start_grid_step_z_exact[layout_index];
  status = fzgx_track_course_find_shared_checkpoint_for_point(
      course,
      track_manifest->authored_track_id,
      track_manifest->circuit_type,
      &query_point,
      &checkpoint_index,
      &checkpoint_fraction);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_prepare_track_collision_query_for_checkpoint_exact(
      (fzgx_sim_world *)world,
      course,
      animation_course,
      (double)checkpoint_fraction,
      checkpoint_index,
      0);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  scratch_raw = fzgx_collision_scratch_raw_const_exact(world);
  if (scratch_raw == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  memcpy(
      transform_out,
      scratch_raw + FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT,
      sizeof(*transform_out));
  memcpy(
      &track_width_or_radius,
      scratch_raw + FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT +
          offsetof(fzgx_track_frame_record, track_width_or_radius),
      sizeof(track_width_or_radius));
  if ((track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_OPEN) &&
      (checkpoint_index == 0) &&
      (checkpoint_fraction < 0.0f)) {
    fzgx_mat43_translate_local_exact(transform_out, (fzgx_vec3){0.0f, 0.0f, query_point.z});
  }

  lane_count = fzgx_start_grid_lane_count_exact[layout_index];
  if (lane_count < 2u) {
    fzgx_mat43_translate_local_exact(
        transform_out, (fzgx_vec3){0.0f, 0.1f, 0.0f});
  } else {
    lane_divisor = (int32_t)(lane_count - 1u);
    lane_index =
        (int32_t)slot_index - ((int32_t)slot_index / (int32_t)lane_count) * (int32_t)lane_count;
    lateral_offset_x =
        fzgx_start_grid_lane_sign_exact[layout_index] *
        fzgx_start_grid_lane_scale_exact[layout_index] *
        (track_width_or_radius *
         (0.5f - (float)lane_index / (float)lane_divisor));
    fzgx_mat43_translate_local_exact(
        transform_out, (fzgx_vec3){lateral_offset_x, 0.1f, 0.0f});
  }
  if (track_width_or_radius_out != 0) {
    *track_width_or_radius_out = track_width_or_radius;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_build_ordinary_start_grid_slot_transform(
    const fzgx_sim_world *world,
    uint32_t slot_index,
    float *track_width_or_radius_out,
    fzgx_mat43 *transform_out) {
  fzgx_status status = fzgx_validate_world(world);

  if (transform_out == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_build_ordinary_start_grid_slot_transform_exact(
      world, slot_index, track_width_or_radius_out, transform_out);
}

fzgx_status fzgx_sim_world_build_ordinary_start_grid_slot_query_result(
    const fzgx_sim_world *world,
    uint32_t slot_index,
    fzgx_current_track_query_result *query_out) {
  fzgx_mat43 transform;
  fzgx_status status = fzgx_validate_world(world);

  if (query_out == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_build_ordinary_start_grid_slot_transform_exact(
      world, slot_index, 0, &transform);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_build_current_track_query_from_shared_point_exact(
      world,
      &(fzgx_vec3){transform.origin_x, transform.origin_y, transform.origin_z},
      query_out);
}

fzgx_status fzgx_sim_world_build_machine_current_track_query_result(
    const fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_current_track_query_result *query_out) {
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  const fzgx_track_manifest *track_manifest;
  const fzgx_machine_snapshot *machine;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  fzgx_status status = fzgx_validate_world(world);

  if ((query_out == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (world->content == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  track_manifest = fzgx_get_active_track_manifest(world);
  if (track_manifest == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  status = fzgx_content_bundle_get_track_course_for_track_index(
      world->content, world->active_track_index, &course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      world->content, world->active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = 0;
  } else if (status != FZGX_STATUS_OK) {
    return status;
  }

  machine = &world->machines[machine_index];
  fzgx_get_machine_track_frame_refresh_checkpoint_exact(
      &machine->track_state, &checkpoint_index, &checkpoint_fraction);
  if (checkpoint_index < 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  return fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
      course,
      animation_course,
      track_manifest->authored_track_id,
      track_manifest->circuit_type,
      &machine->position,
      checkpoint_index,
      checkpoint_fraction,
      query_out);
}

fzgx_status fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(
    fzgx_sim_world *world,
    uint32_t machine_index,
    uint32_t slot_index,
    double launch_speed_units) {
  fzgx_machine_snapshot *machine;
  fzgx_mat43 transform;
  fzgx_status status = fzgx_validate_world(world);

  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  status = fzgx_build_ordinary_start_grid_slot_transform_exact(
      world, slot_index, 0, &transform);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_place_machine_from_current_transform_exact(
      world, machine_index, machine, &transform, launch_speed_units);
}

fzgx_status fzgx_sim_world_reset_machines_to_ordinary_start_grid(
    fzgx_sim_world *world,
    const uint32_t *machine_indices_in_slot_order,
    size_t machine_index_count,
    double launch_speed_units) {
  size_t slot_index;
  fzgx_status status = fzgx_validate_world(world);

  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((machine_indices_in_slot_order == 0) && (machine_index_count != 0u)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (machine_index_count > world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  for (slot_index = 0u; slot_index < machine_index_count; ++slot_index) {
    uint32_t machine_index = machine_indices_in_slot_order[slot_index];
    if (machine_index >= world->machine_count) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    status = fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(
        world, machine_index, (uint32_t)slot_index, launch_speed_units);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    world->machines[machine_index].track_state.rank_this_frame = (uint8_t)slot_index;
  }

  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_reset_machines_to_ordinary_start_grid_stopped(
    fzgx_sim_world *world,
    const uint32_t *machine_indices_in_slot_order,
    size_t machine_index_count) {
  return fzgx_sim_world_reset_machines_to_ordinary_start_grid(
      world, machine_indices_in_slot_order, machine_index_count, 0.0);
}

fzgx_status fzgx_sim_world_set_machine_count(fzgx_sim_world *world, uint32_t machine_count) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_count > world->machine_capacity) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  world->machine_count = machine_count;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_seed_machine_from_content(
    fzgx_sim_world *world,
    uint32_t machine_index,
    uint32_t definition_index,
    uint32_t machine_setting_percent) {
  fzgx_machine_definition definition;
  fzgx_machine_snapshot *snapshot;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (world->content == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  if ((machine_index >= world->machine_count) || (definition_index >= world->content->machine_count) ||
      (machine_setting_percent > 100u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  definition = world->content->machines[definition_index];
  fzgx_derive_machine_base_stat_values_exact((float)((double)machine_setting_percent * 0.01), &definition);
  snapshot = &world->machines[machine_index];
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->machine_id = definition.machine_id;
  snapshot->machine_flags = FZGX_MACHINE_FLAG_ACTIVE;
  snapshot->energy = 100.0f;
  snapshot->max_energy = 100.0f;
  snapshot->basis_physical.basis_x_x = 1.0f;
  snapshot->basis_physical.basis_y_y = 1.0f;
  snapshot->basis_physical.basis_z_z = 1.0f;
  snapshot->max_energy = 100.0f;
  fzgx_reset_machine_runtime_snapshot(snapshot, true);
  snapshot->machine_state |= FZGX_MS_ACTIVE;
  fzgx_update_machine_stats_from_definition(snapshot, &definition);
  fzgx_reset_machine_track_state(&snapshot->track_state, FZGX_RACETRACK_REFRESH_FORCED);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_machine_snapshot(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_machine_snapshot *snapshot) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((snapshot == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  world->machines[machine_index] = *snapshot;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_get_machine_snapshot(
    const fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *snapshot_out) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((snapshot_out == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *snapshot_out = world->machines[machine_index];
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_set_snapshot(
    fzgx_sim_world *world,
    const fzgx_world_snapshot *snapshot) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((snapshot == 0) || (snapshot->api_version != FZGX_SIM_API_VERSION)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (snapshot->machine_count > world->machine_capacity) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  world->frame_index = snapshot->frame_index;
  memcpy(
      world->stage_scene_frame_banks,
      snapshot->stage_scene_frame_banks,
      sizeof(world->stage_scene_frame_banks));
  world->stage_scene_context_mask = snapshot->stage_scene_context_mask;
  world->stage_scene_context_active_machine_index = snapshot->stage_scene_context_active_machine_index;
  world->stage_scene_context_view_slot = snapshot->stage_scene_context_view_slot;
  world->has_pending_stage_scene_story_delta = snapshot->has_pending_stage_scene_story_delta;
  world->race_full_heal_latch_active = snapshot->race_full_heal_latch_active;
  world->race_full_heal_latch_persistent = snapshot->race_full_heal_latch_persistent;
  world->stage_scene_story_clip_offset_frames = snapshot->stage_scene_story_clip_offset_frames;
  world->pending_stage_scene_story_delta_frames = snapshot->pending_stage_scene_story_delta_frames;
  world->active_track_index = snapshot->active_track_index;
  world->race_mode = snapshot->race_mode;
  world->scene_object_query_filter_mask = snapshot->scene_object_query_filter_mask;
  world->scene_object_query_mode_mask = snapshot->scene_object_query_mode_mask;
  if (snapshot->dynamic_scene_runtime_flag_count > FZGX_SIM_MAX_DYNAMIC_SCENE_OBJECTS) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((world->dynamic_scene_collision_course != 0) &&
      (snapshot->dynamic_scene_runtime_flag_count != world->dynamic_scene_collision_course->object_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  world->dynamic_scene_runtime_flag_count = snapshot->dynamic_scene_runtime_flag_count;
  memcpy(
      world->dynamic_scene_runtime_flags,
      snapshot->dynamic_scene_runtime_flags,
      sizeof(world->dynamic_scene_runtime_flags));
  memcpy(
      world->dynamic_scene_clip_bank_time_frames,
      snapshot->dynamic_scene_clip_bank_time_frames,
      sizeof(world->dynamic_scene_clip_bank_time_frames));
  world->machine_count = snapshot->machine_count;
  memcpy(world->machines, snapshot->machines, sizeof(world->machines));
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_get_snapshot(
    const fzgx_sim_world *world,
    fzgx_world_snapshot *snapshot_out) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (snapshot_out == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(snapshot_out, 0, sizeof(*snapshot_out));
  snapshot_out->api_version = FZGX_SIM_API_VERSION;
  snapshot_out->frame_index = world->frame_index;
  memcpy(
      snapshot_out->stage_scene_frame_banks,
      world->stage_scene_frame_banks,
      sizeof(snapshot_out->stage_scene_frame_banks));
  snapshot_out->stage_scene_context_mask = world->stage_scene_context_mask;
  snapshot_out->stage_scene_context_active_machine_index = world->stage_scene_context_active_machine_index;
  snapshot_out->stage_scene_context_view_slot = world->stage_scene_context_view_slot;
  snapshot_out->has_pending_stage_scene_story_delta = world->has_pending_stage_scene_story_delta;
  snapshot_out->race_full_heal_latch_active = world->race_full_heal_latch_active;
  snapshot_out->race_full_heal_latch_persistent = world->race_full_heal_latch_persistent;
  snapshot_out->stage_scene_story_clip_offset_frames = world->stage_scene_story_clip_offset_frames;
  snapshot_out->pending_stage_scene_story_delta_frames = world->pending_stage_scene_story_delta_frames;
  snapshot_out->active_track_index = world->active_track_index;
  snapshot_out->race_mode = world->race_mode;
  snapshot_out->scene_object_query_filter_mask = world->scene_object_query_filter_mask;
  snapshot_out->scene_object_query_mode_mask = world->scene_object_query_mode_mask;
  snapshot_out->dynamic_scene_runtime_flag_count = world->dynamic_scene_runtime_flag_count;
  memcpy(
      snapshot_out->dynamic_scene_runtime_flags,
      world->dynamic_scene_runtime_flags,
      sizeof(world->dynamic_scene_runtime_flags));
  memcpy(
      snapshot_out->dynamic_scene_clip_bank_time_frames,
      world->dynamic_scene_clip_bank_time_frames,
      sizeof(snapshot_out->dynamic_scene_clip_bank_time_frames));
  snapshot_out->machine_count = world->machine_count;
  memcpy(snapshot_out->machines, world->machines, sizeof(world->machines));
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_apply_machine_track_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_machine_track_sample *sample) {
  fzgx_machine_snapshot *machine;
  fzgx_machine_track_state *track;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((sample == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((sample->active_checkpoint_mode != FZGX_ACTIVE_CHECKPOINT_CURRENT) &&
      (sample->active_checkpoint_mode != FZGX_ACTIVE_CHECKPOINT_NEXT)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  track = &machine->track_state;

  track->active_checkpoint_mode = sample->active_checkpoint_mode;
  track->flags = sample->flags;
  track->cached_frame_count = sample->cached_frame_count;
  track->selected_cached_frame_index = sample->selected_cached_frame_index;
  track->checkpoint_variant_count = (int32_t)sample->checkpoint_variant_count;
  track->cur_cp_pointer = sample->segment_index;
  track->track_current_transform = sample->track_current_transform;
  track->track_current_scale = sample->track_current_scale;
  track->track_scl_x = sample->track_scl_x;
  track->track_scl_y = sample->track_scl_y;
  track->track_anchor = sample->track_anchor;
  track->last_track_pos = sample->track_forward;
  track->track_forward = sample->track_forward;
  track->track_up = sample->track_up;
  track->track_width_or_radius = sample->track_width_or_radius;
  track->track_hcylin = sample->track_hcylin;
  track->track_follow_offset = sample->track_follow_offset;
  track->lap_progress_fraction = sample->lap_progress_fraction;
  track->last_frac_diff = sample->last_frac_diff;

  if (sample->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT) {
    fzgx_copy_checkpoint_bank(
        track->stable_cp_idx,
        track->stable_cp_frac,
        sample->active_bank_cp_idx,
        sample->active_bank_cp_frac);
    track->cur_cp_idx = sample->checkpoint_index;
    track->cur_cp_frac = sample->checkpoint_fraction;
    memcpy(track->neighbor_cp_idx, &sample->active_bank_cp_idx[1], sizeof(track->neighbor_cp_idx));
    memcpy(track->neighbor_cp_frac, &sample->active_bank_cp_frac[1], sizeof(track->neighbor_cp_frac));
    track->next_cp_idx = -1;
    track->next_cp_frac = 0.0f;
  } else {
    fzgx_copy_checkpoint_bank(
        track->predictive_cp_idx,
        track->predictive_cp_frac,
        sample->active_bank_cp_idx,
        sample->active_bank_cp_frac);
    track->next_cp_idx = sample->checkpoint_index;
    track->next_cp_frac = sample->checkpoint_fraction;
  }

  fzgx_sync_machine_checkpoint_bank_slot0s(track);

  fzgx_recompute_machine_track_derived_metrics(machine, fzgx_get_active_track_manifest(world));

  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_build_current_track_sample_from_query_result(
    const fzgx_current_track_query_result *query_result,
    fzgx_machine_track_sample *sample_out) {
  if ((query_result == 0) || (sample_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(sample_out, 0, sizeof(*sample_out));
  sample_out->checkpoint_index = query_result->checkpoint_index;
  sample_out->checkpoint_fraction = query_result->checkpoint_fraction;
  fzgx_copy_checkpoint_bank(
      sample_out->active_bank_cp_idx,
      sample_out->active_bank_cp_frac,
      query_result->active_bank_cp_idx,
      query_result->active_bank_cp_frac);
  sample_out->segment_index = query_result->segment_index;
  sample_out->checkpoint_variant_count = query_result->checkpoint_variant_count;
  sample_out->flags = query_result->frame.track_flags;
  sample_out->cached_frame_count = query_result->cached_frame_count;
  sample_out->selected_cached_frame_index = query_result->selected_cached_frame_index;
  sample_out->track_current_transform = query_result->frame.track_current_transform;
  sample_out->track_current_scale = query_result->frame.track_current_scale;
  sample_out->track_scl_x = query_result->frame.track_scl_x;
  sample_out->track_scl_y = query_result->frame.track_scl_y;
  sample_out->track_anchor = query_result->frame.track_anchor;
  sample_out->track_forward = query_result->frame.track_forward;
  sample_out->track_up = query_result->frame.track_up;
  sample_out->track_width_or_radius = query_result->frame.track_width_or_radius;
  sample_out->track_hcylin = query_result->frame.track_hcylin;
  sample_out->track_follow_offset = query_result->frame.track_follow_offset;
  sample_out->lap_progress_fraction = query_result->lap_progress_fraction;
  sample_out->last_frac_diff = query_result->last_frac_diff;
  sample_out->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  return FZGX_STATUS_OK;
}

static int32_t fzgx_select_nearest_cached_branch_frame_exact(
    const fzgx_sim_world *world,
    const fzgx_vec3 *point);

static void fzgx_update_machine_track_facing_exact(fzgx_machine_snapshot *machine) {
  fzgx_machine_track_state *track;
  float dot;

  if (machine == 0) {
    return;
  }
  track = &machine->track_state;
  if ((machine->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_AIRBORNE)) == 0u) {
    dot = track->last_track_pos.x * -machine->basis_physical.basis_z_x +
          track->last_track_pos.y * -machine->basis_physical.basis_z_y +
          track->last_track_pos.z * -machine->basis_physical.basis_z_z;
    if (0.0f <= dot) {
      track->facing_counter -= 1;
    } else {
      track->facing_counter += 1;
    }
  } else {
    track->facing_counter -= 1;
  }

  track->facing_toggled = 0;
  if (track->facing_counter < 30) {
    if (track->facing_counter < 1) {
      if (track->facing_flag != 0) {
        track->facing_flag = 0;
        track->facing_toggled = 1;
      }
      track->facing_counter = 0;
    }
  } else {
    if (track->facing_flag == 0) {
      track->facing_flag = 1;
      track->facing_toggled = 1;
    }
    track->facing_counter = 30;
  }
}

fzgx_status fzgx_sim_world_apply_active_checkpoint_bank_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_active_checkpoint_bank_result *bank_result) {
  fzgx_machine_snapshot *machine;
  fzgx_machine_track_state *track;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((bank_result == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bank_result->checkpoint_variant_count == 0u) ||
      (bank_result->checkpoint_variant_count > 4u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  track = &machine->track_state;
  machine->current_checkpoint = bank_result->checkpoint_index[0];
  machine->checkpoint_fraction = bank_result->checkpoint_fraction[0];
  track->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  fzgx_copy_checkpoint_bank(
      track->stable_cp_idx,
      track->stable_cp_frac,
      bank_result->checkpoint_index,
      bank_result->checkpoint_fraction);
  track->checkpoint_variant_count = (int32_t)bank_result->checkpoint_variant_count;
  track->cur_cp_idx = bank_result->checkpoint_index[0];
  track->cur_cp_frac = bank_result->checkpoint_fraction[0];
  memcpy(track->neighbor_cp_idx, &bank_result->checkpoint_index[1], sizeof(track->neighbor_cp_idx));
  memcpy(track->neighbor_cp_frac, &bank_result->checkpoint_fraction[1], sizeof(track->neighbor_cp_frac));
  fzgx_sync_machine_checkpoint_bank_slot0s(track);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_apply_current_checkpoint_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_current_checkpoint_query_result *query_result) {
  fzgx_machine_snapshot *machine;
  fzgx_machine_track_state *track;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((query_result == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  machine = &world->machines[machine_index];
  track = &machine->track_state;
  machine->current_checkpoint = query_result->checkpoint_index;
  machine->checkpoint_fraction = query_result->checkpoint_fraction;
  track->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  track->cur_cp_idx = query_result->checkpoint_index;
  track->cur_cp_frac = query_result->checkpoint_fraction;
  fzgx_sync_machine_checkpoint_bank_slot0s(track);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_apply_current_track_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_current_track_query_result *query_result) {
  fzgx_machine_track_sample sample;
  fzgx_status status =
      fzgx_sim_build_current_track_sample_from_query_result(query_result, &sample);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_sim_world_apply_machine_track_sample(world, machine_index, &sample);
}

fzgx_status fzgx_sim_world_reset_machine_from_current_transform_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    double launch_speed_units,
    const fzgx_machine_track_sample *sample) {
  if (sample == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  return fzgx_sim_world_reset_machine_from_transform_sample(
      world,
      machine_index,
      &sample->track_current_transform,
      launch_speed_units,
      sample);
}

static fzgx_status fzgx_manage_checkpoints_and_times_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_racetrack_refresh_mode requested_refresh_mode,
    bool lap_timer_gate_active) {
  const fzgx_track_manifest *track_manifest;
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  fzgx_machine_track_state *track;
  int32_t *active_cp_idx_ptr;
  float *active_cp_frac_ptr;
  int32_t *neighbor_cp_idx_ptr;
  float *neighbor_cp_frac_ptr;
  bool use_current_checkpoint_bank;
  uint32_t airborne_gate;
  bool forward_progress_guard = false;
  int lap_cross_direction = 0;
  int32_t current_variant_slot = 0;
  int32_t selected_cached_frame_index;
  fzgx_status status;

  if ((world == 0) || (machine == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (world->content == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  track_manifest = fzgx_get_active_track_manifest(world);
  if (track_manifest == 0) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  status = fzgx_get_active_track_course(world, &course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (course->track_node_count == 0u) {
    return FZGX_STATUS_OK;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      world->content, world->active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = 0;
  } else if (status != FZGX_STATUS_OK) {
    return status;
  }
  track = &machine->track_state;
  if (track->need_resnap != 0u) {
    track->respawn_pos = machine->position;
  }

  if (requested_refresh_mode == FZGX_RACETRACK_REFRESH_NORMAL) {
    if ((machine->state_2 & 8u) == 0u) {
      airborne_gate = 1u;
      use_current_checkpoint_bank = false;
    } else {
      use_current_checkpoint_bank = 0.0f < machine->zero_minus_height_above_track;
      airborne_gate = machine->machine_state & FZGX_MS_AIRBORNE;
    }
  } else {
    airborne_gate = 0u;
    use_current_checkpoint_bank = true;
  }

  if ((requested_refresh_mode == FZGX_RACETRACK_REFRESH_NORMAL) &&
      ((machine->machine_state & FZGX_MS_COMPLETEDRACE_1_Q) == 0u) &&
      lap_timer_gate_active &&
      ((uint32_t)track->lap_time_frames < 360000u)) {
    track->lap_time_frames += 1;
  }

  active_cp_idx_ptr =
      (int32_t *)((uint8_t *)track + (size_t)track->active_cp_idx_ptr_offset);
  active_cp_frac_ptr = (float *)((uint8_t *)track + (size_t)track->active_frac_ptr_offset);
  neighbor_cp_idx_ptr = &track->cur_cp_idx;
  neighbor_cp_frac_ptr = &track->cur_cp_frac;

  if (*active_cp_idx_ptr < 0) {
    uint32_t variant_index = 0u;

    track->last_fit_pos = track->last_cp_pos;
    track->cur_cp_idx = track->last_cp_idx;
    track->cur_cp_frac = track->last_cp_frac;
    if (track->cur_cp_idx < 0) {
      track->cur_cp_idx = 0;
    }
    if (track->need_resnap != 0u) {
      track->last_fit_pos = machine->position;
      if (track_manifest->authored_track_id == 0x2au) {
        status = fzgx_track_course_find_nearest_checkpoint_for_point(
            course,
            track_manifest->authored_track_id,
            track_manifest->circuit_type,
            &track->last_fit_pos,
            &track->cur_cp_idx,
            &track->cur_cp_frac,
            &variant_index);
      } else {
        status = fzgx_track_course_find_shared_checkpoint_for_point(
            course,
            track_manifest->authored_track_id,
            track_manifest->circuit_type,
            &track->last_fit_pos,
            &track->cur_cp_idx,
            &track->cur_cp_frac);
      }
      if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_OUT_OF_RANGE)) {
        return status;
      }
      if (status == FZGX_STATUS_OUT_OF_RANGE) {
        track->cur_cp_idx = -1;
      }
    }
    airborne_gate = 0u;
    track->need_resnap = 0u;
    use_current_checkpoint_bank = true;
    requested_refresh_mode = FZGX_RACETRACK_REFRESH_FORCED;
  } else {
    uint32_t resolved_variant_index = 0u;

    if (fzgx_debug_trace_double_branches_window_exact(
            track_manifest->authored_track_id, machine_index, world->frame_index)) {
      fprintf(
          stderr,
          "cpstep|frame=%u|stage=pre_resolve|use_current=%u|mode=%u|mc_cp=%d|mc_cpf=%.6f|"
          "cur_cp=%d|cur_cpf=%.6f|next_cp=%d|next_cpf=%.6f|active_cp=%d|active_cpf=%.6f|"
          "last_fit=(%.3f,%.3f,%.3f)|pos=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          use_current_checkpoint_bank ? 1u : 0u,
          (unsigned)requested_refresh_mode,
          machine->current_checkpoint,
          machine->checkpoint_fraction,
          track->cur_cp_idx,
          track->cur_cp_frac,
          track->next_cp_idx,
          track->next_cp_frac,
          *active_cp_idx_ptr,
          *active_cp_frac_ptr,
          track->last_fit_pos.x,
          track->last_fit_pos.y,
          track->last_fit_pos.z,
          machine->position.x,
          machine->position.y,
          machine->position.z);
    }

    if (use_current_checkpoint_bank && (machine->current_checkpoint >= 0)) {
      track->next_cp_idx = machine->current_checkpoint;
      track->next_cp_frac = machine->checkpoint_fraction;
      if (((machine->machine_state & FZGX_MS_B30) == 0u) &&
          (track->cur_cp_idx < track->next_cp_idx)) {
        const fzgx_track_node_record *current_track_node =
            &course->track_nodes[(uint32_t)track->cur_cp_idx];
        const fzgx_track_node_record *next_track_node =
            &course->track_nodes[(uint32_t)track->next_cp_idx];
        const fzgx_checkpoint_record *current_checkpoint =
            &course->checkpoints[current_track_node->checkpoint_offset];
        const fzgx_checkpoint_record *next_checkpoint =
            &course->checkpoints[next_track_node->checkpoint_offset];
        float dx = track->last_fit_pos.x - machine->position.x;
        float dy = track->last_fit_pos.y - machine->position.y;
        float dz = track->last_fit_pos.z - machine->position.z;
        double old_checkpoint_distance =
            (double)(current_checkpoint->start_distance +
                     track->cur_cp_frac *
                         (current_checkpoint->end_distance - current_checkpoint->start_distance));
        double next_checkpoint_distance =
            (double)(next_checkpoint->start_distance +
                     track->next_cp_frac *
                         (next_checkpoint->end_distance - next_checkpoint->start_distance));
        double excess_distance =
            (double)((float)(next_checkpoint_distance - old_checkpoint_distance) -
                     sqrtf(dz * dz + dy * dy + dx * dx));

        if (track_manifest->authored_track_id == 3u) {
          if (1500.0 < excess_distance) {
            forward_progress_guard = true;
          }
        } else if (1000.0 < excess_distance) {
          forward_progress_guard = true;
        }
      }
      track->cur_cp_idx = track->next_cp_idx;
    }
    track->last_fit_pos = machine->position;
    status = fzgx_track_course_resolve_branch_checkpoint_from_seed_with_fallback(
        course,
        track_manifest->authored_track_id,
        track_manifest->circuit_type,
        &track->last_fit_pos,
        track->cur_cp_idx,
        &resolved_variant_index,
        0u,
        &track->cur_cp_idx,
        &track->cur_cp_frac);
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_OUT_OF_RANGE)) {
      return status;
    }
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      track->cur_cp_idx = -1;
    }
    if (fzgx_debug_trace_double_branches_window_exact(
            track_manifest->authored_track_id, machine_index, world->frame_index)) {
      fprintf(
          stderr,
          "cpstep|frame=%u|stage=post_resolve|status=%d|use_current=%u|cur_cp=%d|cur_cpf=%.6f|"
          "next_cp=%d|next_cpf=%.6f|resolved_variant=%u\n",
          world->frame_index,
          (int)status,
          use_current_checkpoint_bank ? 1u : 0u,
          track->cur_cp_idx,
          track->cur_cp_frac,
          track->next_cp_idx,
          track->next_cp_frac,
          resolved_variant_index);
    }
  }

  if (use_current_checkpoint_bank) {
    track->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
    track->active_cp_idx_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_idx);
    track->active_frac_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_frac);
    track->next_cp_idx = -1;
  } else {
    if (track_manifest->authored_track_id == 0x1du) {
      uint32_t resolved_variant_index = 0u;

      status = fzgx_track_course_resolve_branch_checkpoint_from_seed_strict(
          course,
          track_manifest->authored_track_id,
          track_manifest->circuit_type,
          &machine->position,
          *active_cp_idx_ptr,
          &resolved_variant_index,
          0u,
          &track->next_cp_idx,
          &track->next_cp_frac);
      if (status == FZGX_STATUS_OUT_OF_RANGE) {
        uint32_t variant_index = 0u;

        status = fzgx_track_course_find_nearest_checkpoint_for_point(
            course,
            track_manifest->authored_track_id,
            track_manifest->circuit_type,
            &machine->position,
            &track->next_cp_idx,
            &track->next_cp_frac,
            &variant_index);
      }
    } else {
      uint32_t variant_index = 0u;

      status = fzgx_track_course_find_nearest_checkpoint_for_point(
          course,
          track_manifest->authored_track_id,
          track_manifest->circuit_type,
          &machine->position,
          &track->next_cp_idx,
          &track->next_cp_frac,
          &variant_index);
    }
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_OUT_OF_RANGE)) {
      return status;
    }
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      track->next_cp_idx = -1;
    }
    if (track->next_cp_idx < 0) {
      track->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
      track->active_cp_idx_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_idx);
      track->active_frac_ptr_offset = offsetof(fzgx_machine_track_state, cur_cp_frac);
      track->next_cp_idx = -1;
    } else {
      track->active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;
      track->active_cp_idx_ptr_offset = offsetof(fzgx_machine_track_state, next_cp_idx);
      track->active_frac_ptr_offset = offsetof(fzgx_machine_track_state, next_cp_frac);
    }
  }

  active_cp_idx_ptr =
      (int32_t *)((uint8_t *)track + (size_t)track->active_cp_idx_ptr_offset);
  active_cp_frac_ptr = (float *)((uint8_t *)track + (size_t)track->active_frac_ptr_offset);

  status = fzgx_track_course_checkpoint_variant_contains_point(
      course,
      track_manifest->authored_track_id,
      track_manifest->circuit_type,
      &machine->position,
      *active_cp_idx_ptr,
      0u,
      &track->cur_cp_pointer);
  if (status != FZGX_STATUS_OK) {
    track->cur_cp_pointer = -1;
  }
  {
    int32_t checkpoint_index = *active_cp_idx_ptr;

    if (checkpoint_index < 0) {
      track->checkpoint_variant_count = 0;
    } else {
      double best_distance_squared = 0.0;
      uint32_t variant_slot = 1u;
      uint32_t checkpoint_count = course->track_nodes[(uint32_t)checkpoint_index].checkpoint_count;

      current_variant_slot = 0;
      while (variant_slot < checkpoint_count) {
        int32_t resolved_checkpoint_index;
        float resolved_checkpoint_fraction;
        fzgx_vec3 point_on_track;

        status = fzgx_track_course_scan_checkpoint_neighbors_for_point(
            course,
            track_manifest->authored_track_id,
            track_manifest->circuit_type,
            &machine->position,
            *active_cp_idx_ptr,
            variant_slot,
            0u,
            &resolved_checkpoint_index,
            &resolved_checkpoint_fraction,
            &point_on_track);
        if (status != FZGX_STATUS_OK) {
          active_cp_idx_ptr[variant_slot] = *active_cp_idx_ptr;
          active_cp_frac_ptr[variant_slot] = *active_cp_frac_ptr;
        } else {
          double distance_squared;

          active_cp_idx_ptr[variant_slot] = resolved_checkpoint_index;
          active_cp_frac_ptr[variant_slot] = resolved_checkpoint_fraction;
          distance_squared =
              (double)fzgx_vec3_distance_squared_exact(machine->position, point_on_track);
          if ((current_variant_slot < 1) || (distance_squared <= best_distance_squared)) {
            current_variant_slot = (int32_t)variant_slot;
            best_distance_squared = distance_squared;
          }
        }
        status = fzgx_track_course_checkpoint_variant_contains_point(
            course,
            track_manifest->authored_track_id,
            track_manifest->circuit_type,
            &machine->position,
            active_cp_idx_ptr[variant_slot],
            variant_slot,
            &track->seg_index_hist[variant_slot - 1u]);
        if (status != FZGX_STATUS_OK) {
          track->seg_index_hist[variant_slot - 1u] = -1;
        }
        if (fzgx_debug_trace_double_branches_window_exact(
                track_manifest->authored_track_id, machine_index, world->frame_index)) {
          fprintf(
              stderr,
              "banktrace|frame=%u|stage=active_variant|slot=%u|status=%d|cp=%d|cpf=%.6f|"
              "contain=%d|best_slot=%d|best_dist2=%.6f\n",
              world->frame_index,
              variant_slot,
              (int)status,
              active_cp_idx_ptr[variant_slot],
              active_cp_frac_ptr[variant_slot],
              track->seg_index_hist[variant_slot - 1u],
              current_variant_slot,
              best_distance_squared);
        }
        variant_slot += 1u;
      }
      track->checkpoint_variant_count = (int32_t)checkpoint_count;
    }
  }

  {
    int32_t checkpoint_index = track->cur_cp_idx;

    if (checkpoint_index < 0) {
      current_variant_slot = 0;
    } else {
      double best_distance_squared = 0.0;
      uint32_t variant_slot;
      uint32_t checkpoint_count = course->track_nodes[(uint32_t)checkpoint_index].checkpoint_count;

      current_variant_slot = 0;
      for (variant_slot = 1u; variant_slot < checkpoint_count; ++variant_slot) {
        int32_t resolved_checkpoint_index;
        float resolved_checkpoint_fraction;
        fzgx_vec3 point_on_track;

        status = fzgx_track_course_scan_checkpoint_neighbors_for_point(
            course,
            track_manifest->authored_track_id,
            track_manifest->circuit_type,
            &machine->position,
            track->cur_cp_idx,
            variant_slot,
            0u,
            &resolved_checkpoint_index,
            &resolved_checkpoint_fraction,
            &point_on_track);
        if (status != FZGX_STATUS_OK) {
          neighbor_cp_idx_ptr[variant_slot] = track->cur_cp_idx;
          neighbor_cp_frac_ptr[variant_slot] = track->cur_cp_frac;
        } else {
          double distance_squared;

          neighbor_cp_idx_ptr[variant_slot] = resolved_checkpoint_index;
          neighbor_cp_frac_ptr[variant_slot] = resolved_checkpoint_fraction;
          distance_squared =
              (double)fzgx_vec3_distance_squared_exact(machine->position, point_on_track);
          if ((current_variant_slot < 1) || (distance_squared <= best_distance_squared)) {
            current_variant_slot = (int32_t)variant_slot;
            best_distance_squared = distance_squared;
          }
        }
        if (fzgx_debug_trace_double_branches_window_exact(
                track_manifest->authored_track_id, machine_index, world->frame_index)) {
          fprintf(
              stderr,
              "banktrace|frame=%u|stage=neighbor_variant|slot=%u|status=%d|cp=%d|cpf=%.6f|"
              "best_slot=%d|best_dist2=%.6f\n",
              world->frame_index,
              variant_slot,
              (int)status,
              neighbor_cp_idx_ptr[variant_slot],
              neighbor_cp_frac_ptr[variant_slot],
              current_variant_slot,
              best_distance_squared);
        }
      }
    }
  }

  if (track->cur_cp_idx < 0) {
    bool interval_has_no_nearby_checkpoint = true;

    if (track->next_cp_idx >= 0) {
      int selector;

      for (selector = -1; selector < 2; ++selector) {
        uint32_t wrapped_index =
            (uint32_t)((int32_t)course->track_node_count + (track->next_cp_idx - selector)) %
            course->track_node_count;
        const fzgx_track_node_record *track_node = &course->track_nodes[wrapped_index];
        uint32_t variant_slot;

        for (variant_slot = 0u; variant_slot < track_node->checkpoint_count; ++variant_slot) {
          const fzgx_checkpoint_record *checkpoint =
              &course->checkpoints[track_node->checkpoint_offset + variant_slot];

          if (selector == -1) {
            float dy;
            float dx;
            float dz;

            if ((checkpoint->plane_end.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                machine->position.y) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
            dy = machine->position.y - checkpoint->plane_end.origin.y;
            dx = machine->position.x - checkpoint->plane_end.origin.x;
            dz = machine->position.z - checkpoint->plane_end.origin.z;
            if ((dx * dx + dy * dy + dz * dz) <=
                (float)FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
          } else if (selector == 1) {
            float dy;
            float dx;
            float dz;

            if ((checkpoint->plane_start.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                machine->position.y) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
            dy = machine->position.y - checkpoint->plane_start.origin.y;
            dx = machine->position.x - checkpoint->plane_start.origin.x;
            dz = machine->position.z - checkpoint->plane_start.origin.z;
            if ((dx * dx + dy * dy + dz * dz) <=
                (float)FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
          } else {
            fzgx_vec3 closest_point;

            if ((checkpoint->plane_end.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                machine->position.y) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
            if ((checkpoint->plane_start.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                machine->position.y) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
            if (fzgx_distance_to_segment_exact(
                    &machine->position,
                    &checkpoint->plane_start.origin,
                    &checkpoint->plane_end.origin,
                    &closest_point) <=
                (double)fzgx_checkpoint_neighborhood_max_vertical_delta_exact) {
              interval_has_no_nearby_checkpoint = false;
              break;
            }
          }
        }
        if (!interval_has_no_nearby_checkpoint) {
          break;
        }
      }
    }
    if (interval_has_no_nearby_checkpoint) {
      machine->machine_state |= FZGX_MS_FALLOUT;
      fzgx_apply_machine_flags_from_exact_state(machine);
    }
  } else {
    bool interval_has_no_nearby_checkpoint = true;

    for (int selector = -1; selector < 2; ++selector) {
      uint32_t wrapped_index =
          (uint32_t)((int32_t)course->track_node_count + (track->cur_cp_idx - selector)) %
          course->track_node_count;
      const fzgx_track_node_record *track_node = &course->track_nodes[wrapped_index];
      uint32_t variant_slot;

      for (variant_slot = 0u; variant_slot < track_node->checkpoint_count; ++variant_slot) {
        const fzgx_checkpoint_record *checkpoint =
            &course->checkpoints[track_node->checkpoint_offset + variant_slot];

        if (selector == -1) {
          float dy;
          float dx;
          float dz;

          if ((checkpoint->plane_end.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
              machine->position.y) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
          dy = machine->position.y - checkpoint->plane_end.origin.y;
          dx = machine->position.x - checkpoint->plane_end.origin.x;
          dz = machine->position.z - checkpoint->plane_end.origin.z;
          if ((dx * dx + dy * dy + dz * dz) <=
              (float)FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
        } else if (selector == 1) {
          float dy;
          float dx;
          float dz;

          if ((checkpoint->plane_start.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
              machine->position.y) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
          dy = machine->position.y - checkpoint->plane_start.origin.y;
          dx = machine->position.x - checkpoint->plane_start.origin.x;
          dz = machine->position.z - checkpoint->plane_start.origin.z;
          if ((dx * dx + dy * dy + dz * dz) <=
              (float)FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
        } else {
          fzgx_vec3 closest_point;

          if ((checkpoint->plane_end.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
              machine->position.y) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
          if ((checkpoint->plane_start.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
              machine->position.y) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
          if (fzgx_distance_to_segment_exact(
                  &machine->position,
                  &checkpoint->plane_start.origin,
                  &checkpoint->plane_end.origin,
                  &closest_point) <=
              (double)fzgx_checkpoint_neighborhood_max_vertical_delta_exact) {
            interval_has_no_nearby_checkpoint = false;
            break;
          }
        }
      }
      if (!interval_has_no_nearby_checkpoint) {
        break;
      }
    }
    if (interval_has_no_nearby_checkpoint) {
      if (track->next_cp_idx >= 0) {
        interval_has_no_nearby_checkpoint = true;
        for (int selector = -1; selector < 2; ++selector) {
          uint32_t wrapped_index =
              (uint32_t)((int32_t)course->track_node_count + (track->next_cp_idx - selector)) %
              course->track_node_count;
          const fzgx_track_node_record *track_node = &course->track_nodes[wrapped_index];
          uint32_t variant_slot;

          for (variant_slot = 0u; variant_slot < track_node->checkpoint_count; ++variant_slot) {
            const fzgx_checkpoint_record *checkpoint =
                &course->checkpoints[track_node->checkpoint_offset + variant_slot];

            if (selector == -1) {
              float dy;
              float dx;
              float dz;

              if ((checkpoint->plane_end.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                  machine->position.y) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
              dy = machine->position.y - checkpoint->plane_end.origin.y;
              dx = machine->position.x - checkpoint->plane_end.origin.x;
              dz = machine->position.z - checkpoint->plane_end.origin.z;
              if ((dx * dx + dy * dy + dz * dz) <=
                  (float)FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
            } else if (selector == 1) {
              float dy;
              float dx;
              float dz;

              if ((checkpoint->plane_start.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                  machine->position.y) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
              dy = machine->position.y - checkpoint->plane_start.origin.y;
              dx = machine->position.x - checkpoint->plane_start.origin.x;
              dz = machine->position.z - checkpoint->plane_start.origin.z;
              if ((dx * dx + dy * dy + dz * dz) <=
                  (float)FZGX_CHECKPOINT_NEIGHBORHOOD_MAX_DISTANCE_SQUARED_EXACT) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
            } else {
              fzgx_vec3 closest_point;

              if ((checkpoint->plane_end.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                  machine->position.y) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
              if ((checkpoint->plane_start.origin.y - fzgx_checkpoint_neighborhood_max_vertical_delta_exact) <
                  machine->position.y) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
              if (fzgx_distance_to_segment_exact(
                      &machine->position,
                      &checkpoint->plane_start.origin,
                      &checkpoint->plane_end.origin,
                      &closest_point) <=
                  (double)fzgx_checkpoint_neighborhood_max_vertical_delta_exact) {
                interval_has_no_nearby_checkpoint = false;
                break;
              }
            }
          }
          if (!interval_has_no_nearby_checkpoint) {
            break;
          }
        }
      }
      if (interval_has_no_nearby_checkpoint) {
        machine->machine_state |= FZGX_MS_FALLOUT;
        fzgx_apply_machine_flags_from_exact_state(machine);
      }
    }
  }

  selected_cached_frame_index = track->plane_id;
  {
    const uint8_t *scratch_raw;
    uint32_t cached_frame_count;
    size_t frame_offset;

    if ((track->next_cp_idx < 0) || (track->next_cp_idx == track->cur_cp_idx)) {
      status = fzgx_prepare_track_collision_query_for_checkpoint_exact(
          world,
          course,
          animation_course,
          (double)active_cp_frac_ptr[current_variant_slot],
          active_cp_idx_ptr[current_variant_slot],
          &machine->track_query_filter_cache);
    } else {
      status = fzgx_prepare_track_collision_query_for_checkpoint_exact(
          world,
          course,
          animation_course,
          (double)track->cur_cp_frac,
          track->cur_cp_idx,
          &machine->track_query_filter_cache);
    }
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    selected_cached_frame_index =
        fzgx_select_nearest_cached_branch_frame_exact(world, &machine->position);
    cached_frame_count = fzgx_collision_scratch_read_u32_exact(
        world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT);
    track->model_id = cached_frame_count;
    track->plane_id = selected_cached_frame_index;
    scratch_raw = fzgx_collision_scratch_raw_const_exact(world);
    frame_offset =
        FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT +
        (size_t)(uint32_t)selected_cached_frame_index * sizeof(fzgx_track_frame_record);
    memcpy(
        track,
        scratch_raw + frame_offset,
        sizeof(fzgx_track_frame_record));
    track->model_id = cached_frame_count;
    track->plane_id = selected_cached_frame_index;
    track->cached_frame_count = cached_frame_count;
    track->selected_cached_frame_index = selected_cached_frame_index;
    if (fzgx_debug_trace_double_branches_window_exact(
            track_manifest->authored_track_id, machine_index, world->frame_index)) {
      fprintf(
          stderr,
          "cptrace|frame=%u|stage=cached_frame_refresh|active_mode=%u|variant_slot=%d|sel=%d|cached=%u|"
          "active_cp=(%d,%d,%d,%d)|active_cpf=(%.6f,%.6f,%.6f,%.6f)|"
          "neighbor_cp=(%d,%d,%d,%d)|neighbor_cpf=(%.6f,%.6f,%.6f,%.6f)|"
          "cur_ptr=%d|cur_cp=%d|cur_cpf=%.6f|next_cp=%d|next_cpf=%.6f|flags=0x%08x\n",
          world->frame_index,
          (unsigned)track->active_checkpoint_mode,
          current_variant_slot,
          selected_cached_frame_index,
          cached_frame_count,
          active_cp_idx_ptr[0],
          active_cp_idx_ptr[1],
          active_cp_idx_ptr[2],
          active_cp_idx_ptr[3],
          active_cp_frac_ptr[0],
          active_cp_frac_ptr[1],
          active_cp_frac_ptr[2],
          active_cp_frac_ptr[3],
          neighbor_cp_idx_ptr[0],
          neighbor_cp_idx_ptr[1],
          neighbor_cp_idx_ptr[2],
          neighbor_cp_idx_ptr[3],
          neighbor_cp_frac_ptr[0],
          neighbor_cp_frac_ptr[1],
          neighbor_cp_frac_ptr[2],
          neighbor_cp_frac_ptr[3],
          track->cur_cp_pointer,
          track->cur_cp_idx,
          track->cur_cp_frac,
          track->next_cp_idx,
          track->next_cp_frac,
          track->flags);
    }

    if ((machine->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_AIRBORNE)) == 0u) {
      float dot =
          track->last_track_pos.x * -machine->basis_physical.basis_z_x +
          track->last_track_pos.y * -machine->basis_physical.basis_z_y +
          track->last_track_pos.z * -machine->basis_physical.basis_z_z;

      if (0.0f <= dot) {
        track->facing_counter -= 1;
      } else {
        track->facing_counter += 1;
      }
    } else {
      track->facing_counter -= 1;
    }
    track->facing_toggled = 0;
    if (track->facing_counter < 30) {
      if (track->facing_counter < 1) {
        if (track->facing_flag != 0) {
          track->facing_flag = 0;
          track->facing_toggled = 1;
        }
        track->facing_counter = 0;
      }
    } else {
      if (track->facing_flag == 0) {
        track->facing_flag = 1;
        track->facing_toggled = 1;
      }
      track->facing_counter = 30;
    }
  }

  if ((machine->machine_state & FZGX_MS_RETIRED) == 0u) {
    float old_lap_progress = track->lap_progress_fraction;
    float new_lap_progress = 0.0f;
    bool allow_unbounded_seam_check = true;
    double current_signed_distance = 0.0;
    double previous_signed_distance = 0.0;

    track->last_frac_diff = course->track_total_distance;
    if (track->cur_cp_idx < 0) {
      track->lap_progress_fraction = 0.0f;
      old_lap_progress = 0.0f;
    } else {
      const fzgx_track_node_record *lap_track_node;
      const fzgx_checkpoint_record *checkpoint;

      if (neighbor_cp_idx_ptr[selected_cached_frame_index] < 0) {
        selected_cached_frame_index = 0;
      }
      lap_track_node = &course->track_nodes[(uint32_t)neighbor_cp_idx_ptr[selected_cached_frame_index]];
      checkpoint = &course->checkpoints[lap_track_node->checkpoint_offset +
                                        (uint32_t)selected_cached_frame_index];
      new_lap_progress =
          checkpoint->start_distance +
          neighbor_cp_frac_ptr[selected_cached_frame_index] *
              (checkpoint->end_distance - checkpoint->start_distance);
      track->lap_progress_fraction = new_lap_progress;
    }

    if ((track->prev_lap_cp == track->lap_start_cp) &&
        (track->prev_lap_cross_cp == track->lap_cross_cp)) {
      allow_unbounded_seam_check = false;
    }

    if (track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_CLOSED) {
      if (allow_unbounded_seam_check ||
          (((double)new_lap_progress < (double)50.0f ||
            (double)(track->last_frac_diff - new_lap_progress) < (double)50.0f) &&
           ((double)old_lap_progress < (double)50.0f ||
            (double)(track->last_frac_diff - old_lap_progress) < (double)50.0f))) {
        current_signed_distance = (double)machine->position.z;
        previous_signed_distance = (double)track->respawn_pos.z;
        if ((0.0 <= current_signed_distance) && (previous_signed_distance < 0.0)) {
          lap_cross_direction = -1;
        } else if ((current_signed_distance < 0.0) && (0.0 <= previous_signed_distance)) {
          const fzgx_checkpoint_record *start_checkpoint = 0;

          status = fzgx_track_course_get_checkpoint_variant(course, 0u, 0u, &start_checkpoint);
          if (status != FZGX_STATUS_OK) {
            return status;
          }
          if (((machine->position.y - start_checkpoint->plane_start.origin.y) > -1.0f) ||
              ((track->respawn_pos.y - start_checkpoint->plane_start.origin.y) > -1.0f) ||
              (track_manifest->authored_track_id == 0x21u)) {
            lap_cross_direction = 1;
          }
        }
      }
    } else {
      if (allow_unbounded_seam_check ||
          ((fabs((double)new_lap_progress) < 50.0) && (fabs((double)old_lap_progress) < 50.0))) {
        current_signed_distance = (double)machine->position.z;
        previous_signed_distance = (double)track->respawn_pos.z;
        if ((0.0 <= current_signed_distance) && (previous_signed_distance < 0.0)) {
          lap_cross_direction = -1;
        } else if ((current_signed_distance < 0.0) && (0.0 <= previous_signed_distance)) {
          const fzgx_checkpoint_record *start_checkpoint = 0;

          status = fzgx_track_course_get_checkpoint_variant(course, 0u, 0u, &start_checkpoint);
          if (status != FZGX_STATUS_OK) {
            return status;
          }
          if (((machine->position.y - start_checkpoint->plane_start.origin.y) > -1.0f) ||
              ((track->respawn_pos.y - start_checkpoint->plane_start.origin.y) > -1.0f) ||
              (track_manifest->authored_track_id == 0x21u) ||
              (track_manifest->authored_track_id == 0x29u)) {
            lap_cross_direction = 1;
          }
        }
      }
      if ((lap_cross_direction == 0) &&
          (allow_unbounded_seam_check ||
           ((fabs((double)(track->last_frac_diff - new_lap_progress)) < 50.0) &&
            (fabs((double)(track->last_frac_diff - old_lap_progress)) < 50.0)))) {
        const fzgx_checkpoint_record *end_checkpoint = 0;

        status = fzgx_track_course_get_checkpoint_variant(
            course, course->track_node_count - 1u, 0u, &end_checkpoint);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        current_signed_distance = (double)fzgx_plane_eval_point_exact(&end_checkpoint->plane_end, &machine->position);
        previous_signed_distance =
            (double)fzgx_plane_eval_point_exact(&end_checkpoint->plane_end, &track->respawn_pos);
        if ((0.0 <= current_signed_distance) && (previous_signed_distance < 0.0)) {
          lap_cross_direction = -1;
        } else if ((current_signed_distance < 0.0) && (0.0 <= previous_signed_distance)) {
          if (((machine->position.y - end_checkpoint->plane_end.origin.y) > -1.0f) ||
              ((track->respawn_pos.y - end_checkpoint->plane_end.origin.y) > -1.0f) ||
              (track_manifest->authored_track_id == 0x21u) ||
              (track_manifest->authored_track_id == 0x29u)) {
            lap_cross_direction = 1;
          }
        }
      }
    }

    if ((lap_cross_direction != 0) &&
        ((machine->machine_state & FZGX_MS_COMPLETEDRACE_1_Q) == 0u) &&
        ((machine->state_2 & 8u) != 0u) &&
        (track->last_cp_idx >= 0)) {
      double seam_fraction = 0.0;

      if (lap_cross_direction < 1) {
        track->lap_cross_cp -= 1;
      } else {
        track->lap_cross_cp += 1;
      }
      if (10000 < track->lap_cross_cp) {
        track->lap_cross_cp = 10000;
      }
      if (track->lap_cross_cp < -1) {
        track->lap_cross_cp = -1;
      }
      if (1 < (track->lap_start_cp - track->lap_cross_cp)) {
        track->lap_cross_cp = track->lap_start_cp - 1;
      }
      if (track->lap_start_cp < track->lap_cross_cp) {
        track->lap_start_cp = track->lap_cross_cp;
        if (track->lap_cross_cp != 0) {
          float fraction =
              (float)fabs(previous_signed_distance) /
              ((float)fabs(current_signed_distance) + (float)fabs(previous_signed_distance));

          if (!isfinite(fraction)) {
            seam_fraction = 0.0;
          } else {
            seam_fraction = (double)fraction;
            if (1.0 < seam_fraction) {
              seam_fraction = 1.0;
            }
          }
          if ((uint32_t)track->lap_time_frames < 360000u) {
            track->lap_time_frames -= 1;
            track->lap_time_fraction = (float)((double)track->lap_time_fraction + seam_fraction);
          } else {
            track->lap_time_frames = 360000;
            track->lap_time_fraction = 0.0f;
          }
          if (1.0f <= track->lap_time_fraction) {
            track->lap_time_fraction -= 1.0f;
            track->lap_time_frames += 1;
          }
          fzgx_compute_race_time_fields_exact(
              track->lap_time_frames,
              track->lap_time_fraction,
              &track->lap_min,
              &track->lap_sec,
              &track->lap_centi);
        }
        track->best_split_7 = track->best_split_0;
        track->best_split_0 = track->best_split_1;
        track->best_split_1 = track->best_split_2;
        track->best_split_2 = track->best_split_3;
        track->best_split_3 = track->best_split_4;
        track->best_split_4 = track->best_split_5;
        track->best_split_5 = track->best_split_6;
        track->best_split_6.frames = track->lap_time_frames;
        track->best_split_6.fraction = track->lap_time_fraction;
        track->best_split_6.display =
            fzgx_pack_race_time_fields_exact(track->lap_min, track->lap_sec, track->lap_centi);
        if (((track->best_lap_frames == 0) && (track->best_lap_fraction == 0.0f)) ||
            ((uint32_t)track->lap_time_frames < (uint32_t)track->best_lap_frames) ||
            ((track->lap_time_frames == track->best_lap_frames) &&
             (track->lap_time_fraction < track->best_lap_fraction))) {
          track->best_lap.frames = track->lap_time_frames;
          track->best_lap.fraction = track->lap_time_fraction;
          track->best_lap.display =
              fzgx_pack_race_time_fields_exact(track->lap_min, track->lap_sec, track->lap_centi);
          if (track->lap_start_cp < 1) {
            track->best_lap_slot = 0u;
          } else {
            track->best_lap_slot = (uint8_t)(track->lap_start_cp - 1);
          }
        }
        track->total_time_frames += track->lap_time_frames;
        track->total_time_fraction += track->lap_time_fraction;
        if (1.0f <= track->total_time_fraction) {
          track->total_time_fraction -= 1.0f;
          track->total_time_frames += 1;
        }
        track->total_min = (uint8_t)((uint32_t)track->total_min + (uint32_t)track->lap_min);
        track->total_sec = (uint8_t)((uint32_t)track->total_sec + (uint32_t)track->lap_sec);
        track->total_centi =
            (uint16_t)((uint32_t)track->total_centi + (uint32_t)track->lap_centi);
        while (999 < track->total_centi) {
          track->total_centi = (uint16_t)(track->total_centi - 1000);
          track->total_sec += 1;
        }
        while (59 < track->total_sec) {
          track->total_sec = (uint8_t)(track->total_sec - 60);
          track->total_min += 1;
        }
        if (99 < track->total_min) {
          track->total_centi = 999;
          track->total_sec = 59;
          track->total_min = 99;
        }
        fzgx_clear_race_time_exact(&track->lap_time_frames, &track->lap_time_fraction);
        track->lap_time_fraction = (float)(1.0 - seam_fraction);
        if (1.0f <= track->lap_time_fraction) {
          track->lap_time_fraction -= 1.0f;
          track->lap_time_frames += 1;
        }
        track->time_extension_trigger_mask = -1;
        machine->machine_state |= FZGX_MS_CROSSEDLAPLINE_Q;
      }
    }

    if (track->lap_start_cp == track->lap_cross_cp) {
      fzgx_update_time_extension_trigger_mask_from_motion_exact(
          course, &machine->position, &machine->position_old, &track->time_extension_trigger_mask);
    }
    fzgx_compute_race_time_fields_exact(
        track->lap_time_frames,
        track->lap_time_fraction,
        &track->lap_min,
        &track->lap_sec,
        &track->lap_centi);
    track->history_time_frames = track->total_time_frames + track->lap_time_frames;
    track->history_time_fraction = track->total_time_fraction + track->lap_time_fraction;
    if (1.0f <= track->history_time_fraction) {
      track->history_time_fraction -= 1.0f;
      track->history_time_frames += 1;
    }
    track->history_min = (uint8_t)((uint32_t)track->total_min + (uint32_t)track->lap_min);
    track->history_sec = (uint8_t)((uint32_t)track->total_sec + (uint32_t)track->lap_sec);
    track->history_centi =
        (uint16_t)((uint32_t)track->total_centi + (uint32_t)track->lap_centi);
    while (999 < track->history_centi) {
      track->history_centi = (uint16_t)(track->history_centi - 1000);
      track->history_sec += 1;
    }
    while (59 < track->history_sec) {
      track->history_sec = (uint8_t)(track->history_sec - 60);
      track->history_min += 1;
    }
    if (99 < track->history_min) {
      track->history_centi = 999;
      track->history_sec = 59;
      track->history_min = 99;
    }
    if (track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_CLOSED) {
      track->last_seg_dist =
          track->lap_progress_fraction + track->last_frac_diff * (float)track->lap_cross_cp;
    } else {
      track->last_seg_dist = track->lap_progress_fraction;
    }
  }

  if (forward_progress_guard && (lap_cross_direction >= 0)) {
    fzgx_destroy_machine_instantly_exact(machine);
  }
  if ((machine->state_2 & 8u) != 0u) {
    track->respawn_pos = machine->position;
  }
  if (((machine->machine_state & FZGX_MS_FALLOUT) == 0u) &&
      !forward_progress_guard &&
      (airborne_gate == 0u)) {
    track->cp_hist_idx[0] = active_cp_idx_ptr[0];
    track->cp_hist_frac[0] = active_cp_frac_ptr[0];
    track->cp_hist_idx[1] = active_cp_idx_ptr[1];
    track->cp_hist_frac[1] = active_cp_frac_ptr[1];
    track->cp_hist_idx[2] = active_cp_idx_ptr[2];
    track->cp_hist_frac[2] = active_cp_frac_ptr[2];
    track->cp_hist_idx[3] = active_cp_idx_ptr[3];
    track->cp_hist_frac[3] = active_cp_frac_ptr[3];
    track->last_cp_idx = machine->current_checkpoint;
    track->last_cp_frac = machine->checkpoint_fraction;
    track->last_cp_pos = machine->position;
    track->prev_lap_cp = track->lap_start_cp;
    track->prev_lap_cross_cp = track->lap_cross_cp;
  }
  if (requested_refresh_mode != FZGX_RACETRACK_REFRESH_NORMAL) {
    track->last_cp_idx = -1;
  }
  (void)machine_index;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_refresh_machine_racetrack_state_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_racetrack_refresh_mode requested_refresh_mode) {
  return fzgx_manage_checkpoints_and_times_exact(
      world, machine_index, machine, requested_refresh_mode, false);
}

static fzgx_status fzgx_reset_machine_from_current_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units) {
  float fVar1;
  double dVar2;

  if ((world == 0) || (machine == 0) || (placement_transform == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  dVar2 = (double)(float)((launch_speed_units / 216.0) * (double)machine->stat_weight);
  fzgx_apply_machine_placement_transform(machine, placement_transform);
  fzgx_reset_machine_runtime_snapshot(machine, true);
  fzgx_reset_machine_track_state(&machine->track_state, FZGX_RACETRACK_REFRESH_FORCED);
  if (launch_speed_units != 0.0) {
    machine->velocity = fzgx_transform_local_vector(
        &machine->basis_physical, (fzgx_vec3){0.0f, 0.0f, (float)-dVar2});
    fVar1 = 1.0f;
    machine->base_speed = (float)(launch_speed_units / 200.0);
    machine->machine_state = machine->machine_state | FZGX_MS_ACTIVE;
    machine->input_accel = fVar1;
    machine->frames_since_start_2 = 90u;
    fzgx_apply_machine_flags_from_exact_state(machine);
  }
  return fzgx_refresh_machine_racetrack_state_exact(
      world, machine_index, machine, FZGX_RACETRACK_REFRESH_FORCED);
}

static fzgx_status fzgx_place_machine_from_current_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units) {
  return fzgx_reset_machine_from_current_transform_exact(
      world, machine_index, machine, placement_transform, launch_speed_units);
}

fzgx_status fzgx_sim_world_reset_machine_from_transform_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units,
    const fzgx_machine_track_sample *sample) {
  fzgx_machine_snapshot *machine;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((placement_transform == 0) || (sample == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (sample->active_checkpoint_mode != FZGX_ACTIVE_CHECKPOINT_CURRENT) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  (void)sample;
  return fzgx_place_machine_from_current_transform_exact(
      world, machine_index, machine, placement_transform, launch_speed_units);
}

fzgx_status fzgx_sim_world_reset_machine_from_transform_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_mat43 *placement_transform,
    double launch_speed_units,
    const fzgx_current_track_query_result *query_result) {
  fzgx_machine_track_sample sample;
  fzgx_status status =
      fzgx_sim_build_current_track_sample_from_query_result(query_result, &sample);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_sim_world_reset_machine_from_transform_sample(
      world, machine_index, placement_transform, launch_speed_units, &sample);
}

fzgx_status fzgx_sim_world_reset_machine_from_current_transform_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    double launch_speed_units,
    const fzgx_current_track_query_result *query_result) {
  fzgx_machine_track_sample sample;
  fzgx_status status =
      fzgx_sim_build_current_track_sample_from_query_result(query_result, &sample);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_sim_world_reset_machine_from_current_transform_sample(
      world, machine_index, launch_speed_units, &sample);
}

fzgx_status fzgx_sim_world_refresh_machine_racetrack_state_from_sample(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_machine_track_sample *sample) {
  fzgx_machine_snapshot *machine;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((sample == 0) || (machine_index >= world->machine_count)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (sample->active_checkpoint_mode != FZGX_ACTIVE_CHECKPOINT_CURRENT) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  (void)sample;
  return fzgx_refresh_machine_racetrack_state_exact(
      world, machine_index, machine, FZGX_RACETRACK_REFRESH_FORCED);
}

fzgx_status fzgx_sim_world_refresh_machine_racetrack_state_from_query_result(
    fzgx_sim_world *world,
    uint32_t machine_index,
    const fzgx_current_track_query_result *query_result) {
  fzgx_machine_track_sample sample;
  fzgx_status status =
      fzgx_sim_build_current_track_sample_from_query_result(query_result, &sample);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_sim_world_refresh_machine_racetrack_state_from_sample(
      world, machine_index, &sample);
}

fzgx_status fzgx_sim_world_commit_machine_active_checkpoint_bank(
    fzgx_sim_world *world,
    uint32_t machine_index) {
  fzgx_machine_track_state *track;
  int i;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  track = &world->machines[machine_index].track_state;
  if (track->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT) {
    for (i = 0; i < 4; ++i) {
      track->cp_hist_idx[i] = track->stable_cp_idx[i];
      track->cp_hist_frac[i] = track->stable_cp_frac[i];
    }
  } else {
    for (i = 0; i < 4; ++i) {
      track->cp_hist_idx[i] = track->predictive_cp_idx[i];
      track->cp_hist_frac[i] = track->predictive_cp_frac[i];
    }
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_commit_machine_checkpoint_snapshot(
    fzgx_sim_world *world,
    uint32_t machine_index) {
  fzgx_machine_snapshot *machine;
  fzgx_machine_track_state *track;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  track = &machine->track_state;
  track->last_cp_idx = machine->current_checkpoint;
  track->last_cp_frac = machine->checkpoint_fraction;
  track->last_cp_pos = machine->position;
  track->prev_lap_cp = track->lap_start_cp;
  track->prev_lap_cross_cp = track->lap_cross_cp;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_reset_machine_track_state(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_racetrack_refresh_mode refresh_mode) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((refresh_mode != FZGX_RACETRACK_REFRESH_NORMAL) &&
      (refresh_mode != FZGX_RACETRACK_REFRESH_FORCED)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  fzgx_reset_machine_track_state(&world->machines[machine_index].track_state, refresh_mode);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_refresh_machine_track_fit_transform(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_mat43 *transform_out) {
  fzgx_status status = fzgx_validate_world(world);

  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (transform_out == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  return fzgx_refresh_machine_track_fit_transform_exact(
      world, machine_index, &world->machines[machine_index], transform_out);
}

fzgx_status fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_racetrack_refresh_mode refresh_mode,
    bool forward_progress_guard) {
  fzgx_machine_snapshot *machine;
  fzgx_machine_track_state *track;
  int32_t *active_cp_idx_ptr;
  float *active_cp_frac_ptr;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((refresh_mode != FZGX_RACETRACK_REFRESH_NORMAL) &&
      (refresh_mode != FZGX_RACETRACK_REFRESH_FORCED)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  track = &machine->track_state;
  if ((((machine->machine_state & FZGX_MS_FALLOUT) == 0u) && !forward_progress_guard) &&
      ((refresh_mode == FZGX_RACETRACK_REFRESH_FORCED) ||
       (((machine->state_2 & 8u) != 0u) && ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u)))) {
    if (track->active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_NEXT) {
      active_cp_idx_ptr = track->predictive_cp_idx;
      active_cp_frac_ptr = track->predictive_cp_frac;
    } else {
      active_cp_idx_ptr = track->stable_cp_idx;
      active_cp_frac_ptr = track->stable_cp_frac;
    }
    track->cp_hist_idx[0] = active_cp_idx_ptr[0];
    track->cp_hist_frac[0] = active_cp_frac_ptr[0];
    track->cp_hist_idx[1] = active_cp_idx_ptr[1];
    track->cp_hist_frac[1] = active_cp_frac_ptr[1];
    track->cp_hist_idx[2] = active_cp_idx_ptr[2];
    track->cp_hist_frac[2] = active_cp_frac_ptr[2];
    track->cp_hist_idx[3] = active_cp_idx_ptr[3];
    track->cp_hist_frac[3] = active_cp_frac_ptr[3];
    track->last_cp_idx = machine->current_checkpoint;
    track->last_cp_frac = machine->checkpoint_fraction;
    track->last_cp_pos = machine->position;
    track->prev_lap_cp = track->lap_start_cp;
    track->prev_lap_cross_cp = track->lap_cross_cp;
  }
  if (refresh_mode == FZGX_RACETRACK_REFRESH_FORCED) {
    track->last_cp_idx = -1;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_world_recompute_machine_track_derived_metrics(
    fzgx_sim_world *world,
    uint32_t machine_index) {
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  fzgx_recompute_machine_track_derived_metrics(
      &world->machines[machine_index], fzgx_get_active_track_manifest(world));
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_finalize_machine_finish_score(
    fzgx_sim_world *world,
    uint32_t machine_index) {
  fzgx_machine_snapshot *machine;
  uint32_t score;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (machine_index >= world->machine_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  machine = &world->machines[machine_index];
  if ((machine->machine_flags & FZGX_MACHINE_FLAG_COMPLETED_RACE) != 0u) {
    return FZGX_STATUS_OK;
  }
  score = machine->score + (uint32_t)(machine->energy * 0.5f);
  if (machine->boost_count == 0u) {
    score += 25u;
  }
  if (machine->dash_plate_hit_count == 0u) {
    score += 25u;
  }
  if (machine->clean_race_bonus_eligible) {
    score += 40u;
  }
  if ((machine->machine_flags & FZGX_MACHINE_FLAG_ZERO_HP) != 0u) {
    score += 25u;
  }
  if (score > machine->wall_hit_count) {
    score -= machine->wall_hit_count;
  } else {
    score = 0u;
  }
  machine->score = (uint16_t)score;
  machine->machine_flags |= FZGX_MACHINE_FLAG_COMPLETED_RACE;
  machine->machine_state |= FZGX_MS_COMPLETEDRACE_1_Q;
  return FZGX_STATUS_OK;
}

static void fzgx_zero_machine_control_inputs_exact(fzgx_machine_snapshot *machine) {
  machine->input_steer_yaw = 0.0f;
  machine->input_steer_pitch = 0.0f;
  machine->input_accel = 0.0f;
  machine->input_brake = 0.0f;
  machine->input_strafe = 0.0f;
}

static void fzgx_handle_steeringwheel_control_inputs_exact(
    fzgx_machine_snapshot *machine,
    const fzgx_control_sample *control) {
  float strafe_input = control->strafe;
  uint8_t strafe_dir = 0u;

  if (((control->buttons & FZGX_INPUT_BUTTON_STRAFE_MOD) != 0u) &&
      (0.5 < fabs((double)machine->input_steer_yaw)) &&
      (0.2 < fabs((double)(machine->input_steer_yaw - machine->input_yaw_dupe)))) {
    machine->machine_state |= FZGX_MS_SPINATTACKING;
  }
  if (strafe_input <= 0.1f) {
    if (-0.1f <= strafe_input) {
      machine->input_strafe *= 0.9f;
    } else {
      machine->input_strafe -= 0.12f;
      if (machine->input_strafe < -1.0f) {
        machine->input_strafe = -1.0f;
      }
    }
  } else {
    machine->input_strafe += 0.12f;
    if (1.0f < machine->input_strafe) {
      machine->input_strafe = 1.0f;
    }
  }
  if (strafe_input < -0.1f) {
    strafe_dir = 0x10u;
  }
  if (0.1f < strafe_input) {
    strafe_dir |= 0x20u;
  }
  if ((strafe_dir == 0x10u) || (strafe_dir == 0x20u)) {
    if ((machine->unk_byte_0x4c3 & 0xf0u) == strafe_dir) {
      if ((machine->unk_byte_0x4c3 & 0x0fu) < 9u) {
        machine->machine_state |= FZGX_MS_SIDEATTACKING;
        machine->side_attack_indicator = 0.49f * strafe_input;
      } else {
        machine->unk_byte_0x4c3 = (uint8_t)(strafe_dir | 10u);
      }
    } else {
      machine->unk_byte_0x4c3 = (uint8_t)(strafe_dir | 10u);
    }
  }
  if ((0.1f < machine->input_brake) && (machine->brake_timer < 30u)) {
    machine->machine_state |= FZGX_MS_B13;
  }
  machine->state_2 |= 4u;
}

static void fzgx_stage_control_sample_to_machine(
    fzgx_machine_snapshot *machine,
    const fzgx_control_sample *previous_control,
    const fzgx_control_sample *control) {
  bool accel_pressed_now;
  bool accel_pressed_prev;

  machine->side_attack_indicator = 0.0f;
  machine->control_profile_kind = control->control_profile_kind;
  if ((machine->machine_state & FZGX_MS_0HP) != 0u) {
    fzgx_zero_machine_control_inputs_exact(machine);
    return;
  }

  machine->input_steer_yaw = control->steer_yaw;
  machine->input_steer_pitch = control->steer_pitch;
  machine->input_accel = control->accel;
  machine->input_brake = control->brake;
  accel_pressed_now = control->accel > 0.0f;
  accel_pressed_prev = (previous_control != 0) && (previous_control->accel > 0.0f);

  if (((control->buttons & FZGX_INPUT_BUTTON_SIDE_ATTACK) != 0u) &&
      (0.8 < fabs((double)machine->input_steer_yaw))) {
    machine->machine_state |= FZGX_MS_SIDEATTACKING;
    machine->side_attack_indicator = 0.4f * machine->input_steer_yaw;
  }
  if (((control->buttons & FZGX_INPUT_BUTTON_SPIN_ATTACK) != 0u) &&
      ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) == 0u) &&
      (60u < machine->frames_since_start_2) &&
      (0.2 < fabs((double)machine->input_steer_yaw))) {
    machine->machine_state |= FZGX_MS_SPINATTACKING;
  }
  if (accel_pressed_now && !accel_pressed_prev) {
    machine->machine_state |= FZGX_MS_B14;
  }
  if (((control->buttons & FZGX_INPUT_BUTTON_BOOST) != 0u) &&
      ((previous_control == 0) || ((previous_control->buttons & FZGX_INPUT_BUTTON_BOOST) == 0u))) {
    machine->machine_state |= FZGX_MS_JUSTPRESSEDBOOST;
  }

  switch (control->control_profile_kind) {
  case 1u:
  case 2u:
    machine->input_strafe = control->strafe;
    machine->input_steer_yaw *= fabsf(machine->input_steer_yaw);
    if ((control->buttons & FZGX_INPUT_BUTTON_STRAFE_MOD) != 0u) {
      machine->machine_state |= FZGX_MS_B13;
    }
    machine->state_2 &= ~4u;
    break;
  case 3u:
  case 4u:
    fzgx_handle_steeringwheel_control_inputs_exact(machine, control);
    break;
  default:
    fzgx_zero_machine_control_inputs_exact(machine);
    break;
  }
}

static bool fzgx_control_requests_machine_activation_exact(const fzgx_control_sample *control) {
  if (control == 0) {
    return false;
  }
  return control->accel >= 0.3f;
}

static bool fzgx_machine_is_in_prestart_hold_state_exact(const fzgx_machine_snapshot *machine) {
  if (machine == 0) {
    return false;
  }
  if ((machine->machine_state &
       (FZGX_MS_ACTIVE | FZGX_MS_STARTINGCOUNTDOWN | FZGX_MS_FALLOUT | FZGX_MS_COMPLETEDRACE_1_Q |
        FZGX_MS_COMPLETEDRACE_2_Q | FZGX_MS_RETIRED | FZGX_MS_B29)) != 0u) {
    return false;
  }
  if ((machine->frames_until_restored != 0u) || (machine->post_restore_frame_countdown != 0u)) {
    return false;
  }
  if ((machine->frames_since_start != 0u) || (machine->frames_since_start_2 != 0u)) {
    return false;
  }
  if ((machine->race_start_charge != 0.0f) || (machine->base_speed != 0.0f)) {
    return false;
  }
  if ((machine->velocity.x != 0.0f) || (machine->velocity.y != 0.0f) || (machine->velocity.z != 0.0f)) {
    return false;
  }
  if ((machine->angular_velocity.x != 0.0f) || (machine->angular_velocity.y != 0.0f) ||
      (machine->angular_velocity.z != 0.0f)) {
    return false;
  }
  return true;
}

static bool fzgx_world_should_defer_prestart_step_exact(const fzgx_sim_world *world) {
  uint32_t i;
  bool found_held_machine = false;

  if (world == 0) {
    return false;
  }
  for (i = 0u; i < world->machine_count; ++i) {
    const fzgx_machine_snapshot *machine = &world->machines[i];

    if (!fzgx_machine_is_in_prestart_hold_state_exact(machine)) {
      return false;
    }
    if (fzgx_control_requests_machine_activation_exact(&world->controls[i])) {
      return false;
    }
    found_held_machine = true;
  }
  return found_held_machine;
}

static void fzgx_handle_steering_exact(fzgx_machine_snapshot *machine) {
  size_t i;
  float steer_scale;
  float angular_delta;

  if ((machine->machine_state & FZGX_MS_ACTIVE) == 0u) {
    return;
  }
  steer_scale = 1.0f;
  for (i = 0u; i < sizeof(machine->suspension_state) / sizeof(machine->suspension_state[0]); ++i) {
    if ((machine->suspension_corners[i].state & 4u) != 0u) {
      steer_scale -= 0.25f;
    }
  }
  angular_delta =
      (machine->stat_turn_movement +
       steer_scale * machine->stat_strafe_turn * machine->input_strafe *
           machine->input_steer_yaw) *
      -machine->input_steer_yaw;
  if ((machine->machine_state & FZGX_MS_SIDEATTACKING) != 0u) {
    angular_delta *= 0.3f;
  }
  machine->angular_velocity.y += 1.5f * angular_delta;
  if (fabsf(machine->angular_velocity.y) < 1.0f) {
    machine->angular_velocity.y = 0.0f;
  }
  machine->input_yaw_dupe = machine->input_steer_yaw;
}

static void fzgx_refresh_machine_speed_kmh_exact(fzgx_machine_snapshot *machine) {
  float speed_units = fzgx_vec3_length(machine->velocity);
  machine->speed_kmh = 216.0f * (speed_units / machine->stat_weight);
}

static void fzgx_set_terrain_state_from_track_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine) {
  const fzgx_track_manifest *track_manifest = fzgx_get_active_track_manifest(world);
  uint32_t query_flags = ((machine->state_2 & 2u) == 0u) ? 0x98u : 0x88u;
  uint32_t terrain_flags_2 = machine->terrain_flags_2;
  uint32_t result_flags = 0u;
  bool trace_fire_field_kill_window =
      (track_manifest != 0) &&
      (track_manifest->authored_track_id == 17u) &&
      (machine_index == 0u) &&
      (2248u <= world->frame_index) &&
      (world->frame_index <= 2256u);

  if ((terrain_flags_2 & 0x00100000u) == 0u) {
    query_flags |= 0x02u;
  }
  if ((terrain_flags_2 & 0x00080000u) == 0u) {
    query_flags |= 0x20u;
  }
  if ((terrain_flags_2 & 0x00040000u) == 0u) {
    query_flags |= 0x40u;
  }
  if ((terrain_flags_2 & 0x00020000u) == 0u) {
    query_flags |= 0x80u;
  }
  if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
    result_flags = fzgx_find_terrain_and_objects_exact(
        world, &machine->position, &machine->position_old, query_flags, machine_index);
  }
  if ((result_flags & 0x08u) != 0u) {
    machine->machine_state |= FZGX_MS_B23 | FZGX_MS_BOOSTING_DASHPLATE;
    machine->terrain_flags |= FZGX_TERRAIN_DASHPLATE;
  }
  if ((((terrain_flags_2 & 0x00100000u) != 0u) || ((result_flags & 0x02u) != 0u)) &&
      ((machine->machine_state & FZGX_MS_0HP) == 0u)) {
    machine->state_2 |= 1u;
    machine->terrain_flags |= FZGX_TERRAIN_RECHARGE;
  }
  if ((machine->machine_state & FZGX_MS_BOOSTING) == 0u) {
    if (((terrain_flags_2 & 0x00040000u) != 0u) || ((result_flags & 0x40u) != 0u)) {
      machine->terrain_flags |= FZGX_TERRAIN_DIRT;
    }
  }
  if (((terrain_flags_2 & 0x00080000u) != 0u) || ((result_flags & 0x20u) != 0u)) {
    machine->terrain_flags |= FZGX_TERRAIN_ICE;
  }
  if ((result_flags & 0x10u) != 0u) {
    machine->terrain_flags |= FZGX_TERRAIN_JUMP;
  }
  if (((terrain_flags_2 & 0x00020000u) != 0u) || ((result_flags & 0x80u) != 0u)) {
    machine->terrain_flags |= FZGX_TERRAIN_LAVA;
  }
  if (trace_fire_field_kill_window) {
    fprintf(
        stderr,
        "terraintrace|frame=%u|query=0x%08x|terrain2=0x%08x|result=0x%08x|terrain=0x%08x|"
        "surface=0x%08x|state=0x%08x|state2=0x%08x|energy=%.6f\n",
        world->frame_index,
        query_flags,
        terrain_flags_2,
        result_flags,
        machine->terrain_flags,
        machine->floor_surface_flags,
        machine->machine_state,
        machine->state_2,
        machine->energy);
  }
}

static void fzgx_update_suspension_force_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_machine_tilt_corner_snapshot *corner) {
  size_t corner_index;
  const fzgx_track_manifest *track_manifest;
  int32_t track_id = -1;
  float startup_scale;
  double max_rest_force;
  double suspension_force;
  fzgx_mat43 current_transform;
  fzgx_vec3 corner_endpoint;
  float force_scale;
  float previous_force;
  float weight_scale;

  if ((world == 0) || (machine == 0) || (corner == 0)) {
    return;
  }
  corner_index = (size_t)(corner - machine->suspension_corners);
  track_manifest = fzgx_get_active_track_manifest(world);
  if (track_manifest != 0) {
    track_id = (int32_t)track_manifest->authored_track_id;
  }

  startup_scale = 0.1f + (float)machine->frames_since_start_2 / 90.0f;
  if (0.5f < startup_scale) {
    startup_scale = 0.5f;
  }
  max_rest_force = (double)(startup_scale * 2.0f * corner->rest_length_scale);

  current_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&current_transform, machine->position);
  corner_endpoint = fzgx_transform_local_point(&current_transform, corner->offset);
  corner->pos = fzgx_transform_local_point(
      &current_transform,
      (fzgx_vec3){corner->offset.x, corner->offset.y - 2000.0f, corner->offset.z});

  if (((corner->state & 0x20u) != 0u) ||
      ((machine->zero_minus_height_above_track <= 0.0f) && ((corner->state & 2u) != 0u))) {
    if ((((machine_index == 0u) &&
          (((27u <= world->frame_index) && (world->frame_index <= 35u)) ||
           ((120u <= world->frame_index) && (world->frame_index <= 130u)))) ||
         ((track_id >= 0) &&
          fzgx_debug_trace_double_branches_window_exact(
              (uint32_t)track_id, machine_index, world->frame_index))) &&
        (corner_index < 4u)) {
      fprintf(
          stderr,
          "suspdbg|frame=%u|corner=%zu|disconnect=1|state=0x%02x|height=%.6f|"
          "start=(%.3f,%.3f,%.3f)|end=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          corner_index,
          corner->state,
          machine->zero_minus_height_above_track,
          corner->pos.x,
          corner->pos.y,
          corner->pos.z,
          corner_endpoint.x,
          corner_endpoint.y,
          corner_endpoint.z);
    }
    suspension_force = 0.0;
    corner->state |= 0x20u;
  } else {
    fzgx_world_spherecast_request request;
    fzgx_status status;
    fzgx_vec3 sweep_contact;
    fzgx_vec3 sweep_normal;
    float sweep_dist;
    bool sweep_hit;

  memset(&request, 0, sizeof(request));
  request.start = corner->pos;
  request.end = corner_endpoint;
  request.flags = 0x60005u;
  fzgx_seed_world_spherecast_from_machine_track_state_exact(&request, &machine->track_state);
  request.machine_index = machine_index;
  if ((track_id >= 0) &&
      fzgx_debug_trace_double_branches_window_exact(
          (uint32_t)track_id, machine_index, world->frame_index)) {
    fzgx_debug_log_world_spherecast_seed_exact(
        "suspdbg", world->frame_index, &request, &machine->track_state);
  }
  status = fzgx_spherecast_vs_world_exact(world, &request, 0);
    sweep_contact = corner->pos;
    sweep_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    sweep_dist = 0.0f;
    sweep_hit = false;
    if (status == FZGX_STATUS_OK) {
      sweep_hit = fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
          world, &request.start, &request.end, &sweep_contact, &sweep_normal, &sweep_dist);
    }
    if ((machine_index == 0u) &&
        (842u <= world->frame_index) &&
        (world->frame_index <= 860u) &&
        (corner_index < 4u)) {
      fprintf(
          stderr,
          "posold|frame=%u|stage=susp_sweep|corner=%zu|hit=%u|state=0x%02x|"
          "pos_before=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)|"
          "contact=(%.3f,%.3f,%.3f)|normal=(%.3f,%.3f,%.3f)|dist=%.3f\n",
          world->frame_index,
          corner_index,
          sweep_hit ? 1u : 0u,
          corner->state,
          corner->pos.x,
          corner->pos.y,
          corner->pos.z,
          corner->pos_old.x,
          corner->pos_old.y,
          corner->pos_old.z,
          sweep_contact.x,
          sweep_contact.y,
          sweep_contact.z,
          sweep_normal.x,
          sweep_normal.y,
          sweep_normal.z,
          sweep_dist);
    }
    if ((((machine_index == 0u) &&
          (((27u <= world->frame_index) && (world->frame_index <= 35u)) ||
           ((120u <= world->frame_index) && (world->frame_index <= 130u)))) ||
         ((track_id >= 0) &&
          fzgx_debug_trace_double_branches_window_exact(
              (uint32_t)track_id, machine_index, world->frame_index))) &&
        (corner_index < 4u)) {
      fprintf(
          stderr,
          "suspdbg|frame=%u|corner=%zu|start=(%.3f,%.3f,%.3f)|end=(%.3f,%.3f,%.3f)|"
          "track_flags=0x%08x|generic_flags=0x%08x|sel=%d|cp=%d|cpf=%.6f|hit=%u|"
          "contact=(%.3f,%.3f,%.3f)|normal=(%.3f,%.3f,%.3f)|dist=%.3f|state=0x%02x\n",
          world->frame_index,
          corner_index,
          request.start.x,
          request.start.y,
          request.start.z,
          request.end.x,
          request.end.y,
          request.end.z,
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT),
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT),
          fzgx_collision_scratch_read_s32_exact(
              world, FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT),
          fzgx_collision_scratch_read_s32_exact(world, FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT),
          fzgx_collision_scratch_read_f32_exact(
              world, FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT),
          sweep_hit ? 1u : 0u,
          sweep_contact.x,
          sweep_contact.y,
          sweep_contact.z,
          sweep_normal.x,
          sweep_normal.y,
          sweep_normal.z,
          sweep_dist,
          corner->state);
    }
    if (sweep_hit) {
      corner->pos = sweep_contact;
      corner->up_vector_2 = sweep_normal;
      force_scale = sweep_dist;
      if ((machine_index == 0u) &&
          (842u <= world->frame_index) &&
          (world->frame_index <= 860u) &&
          (corner_index < 4u)) {
        fprintf(
            stderr,
            "posold|frame=%u|stage=susp_set_pos|corner=%zu|state=0x%02x|"
            "pos=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)\n",
            world->frame_index,
            corner_index,
            corner->state,
            corner->pos.x,
            corner->pos.y,
            corner->pos.z,
            corner->pos_old.x,
            corner->pos_old.y,
            corner->pos_old.z);
      }
    }
    if (sweep_hit) {
      corner->state &= ~8u;
      suspension_force = (double)(float)((double)(float)(force_scale - 2000.0f) + max_rest_force);
    } else {
      suspension_force = 0.0;
      corner->state |= 8u;
    }
  }

  force_scale = 0.0f;
  if (0.0 < suspension_force) {
    corner->state &= ~2u;
    if (max_rest_force < suspension_force) {
      force_scale = 0.5f * (float)(suspension_force - (double)corner->force) * machine->stat_weight;
      suspension_force = max_rest_force;
    }
    previous_force = corner->force;
    weight_scale = machine->stat_weight / 1200.0f;
    corner->force = (float)suspension_force;
    corner->up_vector = corner->up_vector_2;
    force_scale =
        (float)((double)force_scale +
                (double)(0.009f * (9000.0f * (float)suspension_force) * weight_scale) -
                (double)(weight_scale * 10000.0f * 0.009f * (previous_force - (float)suspension_force)));
  } else {
    corner->state |= 2u;
    corner->force = 0.0f;
    corner->up_vector = (fzgx_vec3){0.0f, 1.0f, 0.0f};
    if ((corner->state & 8u) != 0u) {
      corner->up_vector_2 = (fzgx_vec3){0.0f, 1.0f, 0.0f};
    } else {
      force_scale = 0.0f;
    }
  }
  corner->force_spatial_len = force_scale;

  corner->force_spatial.x = corner->up_vector.x * corner->force_spatial_len;
  corner->force_spatial.y = corner->up_vector.y * corner->force_spatial_len;
  corner->force_spatial.z = corner->up_vector.z * corner->force_spatial_len;
  if ((machine_index == 0u) &&
      (((27u <= world->frame_index) && (world->frame_index <= 35u)) ||
       ((120u <= world->frame_index) && (world->frame_index <= 130u))) &&
      (corner_index < 4u)) {
    fprintf(
        stderr,
        "suspforce|frame=%u|corner=%zu|max_rest=%.6f|force=%.6f|force_len=%.6f|"
        "up=(%.6f,%.6f,%.6f)|state=0x%02x|pos=(%.3f,%.3f,%.3f)\n",
        world->frame_index,
        corner_index,
        (float)max_rest_force,
        corner->force,
        corner->force_spatial_len,
        corner->up_vector.x,
        corner->up_vector.y,
        corner->up_vector.z,
        corner->state,
        corner->pos.x,
        corner->pos.y,
        corner->pos.z);
  }
}

static bool fzgx_track_id_is_sonic_oval_exact(uint32_t authored_track_id) {
  return (authored_track_id == 0x24u) || (authored_track_id == 0x25u);
}

static bool fzgx_track_id_uses_flat_normal_special_case_exact(uint32_t authored_track_id) {
  return authored_track_id == 0x0fu;
}

static const uint8_t *fzgx_select_track_normal_corner_indices_exact(uint32_t mask) {
  switch (mask) {
    case 0x0fu:
      return fzgx_track_normal_corner_index_sets[0];
    case 0x0eu:
      return fzgx_track_normal_corner_index_sets[1];
    case 0x0du:
      return fzgx_track_normal_corner_index_sets[2];
    case 0x0bu:
      return fzgx_track_normal_corner_index_sets[3];
    case 0x07u:
      return fzgx_track_normal_corner_index_sets[4];
    default:
      return 0;
  }
}

static bool fzgx_normalize_vec3_exact(fzgx_vec3 *vector) {
  float length = fzgx_vec3_length(*vector);

  if (!isfinite(length) || !(length > 0.0f)) {
    return false;
  }
  vector->x /= length;
  vector->y /= length;
  vector->z /= length;
  return true;
}

typedef struct fzgx_collision_impact_info {
  fzgx_vec3 relative_dir_local;
  fzgx_vec3 relative_dir_world;
  float speed_per_mass;
  float impact_axis_z;
} fzgx_collision_impact_info;

static bool fzgx_swept_sphere_vs_swept_sphere_exact(
    double radius_a,
    double radius_b,
    const fzgx_vec3 *p0_a,
    const fzgx_vec3 *p1_a,
    const fzgx_vec3 *p0_b,
    const fzgx_vec3 *p1_b,
    float *out_t,
    uint32_t *started_intersecting) {
  fzgx_vec3 delta0;
  fzgx_vec3 delta1;
  double rel_len_sq;
  double proj;
  double c;
  float discriminant;

  if ((p0_a == 0) || (p1_a == 0) || (p0_b == 0) || (p1_b == 0) || (out_t == 0) ||
      (started_intersecting == 0)) {
    return false;
  }

  *out_t = 100.0f;
  *started_intersecting = 0u;
  delta0 = fzgx_vec3_sub(*p0_a, *p0_b);
  delta1 = fzgx_vec3_sub(fzgx_vec3_sub(*p1_a, *p1_b), delta0);
  rel_len_sq = (double)fzgx_vec3_dot(delta1, delta1);
  if (rel_len_sq < (double)FLT_EPSILON) {
    *started_intersecting = 1u;
    return false;
  }

  proj = (double)fzgx_vec3_dot(delta0, delta1);
  c = (double)(fzgx_vec3_dot(delta0, delta0) - (float)(radius_a + radius_b) * (float)(radius_a + radius_b));
  discriminant = (float)(proj * proj - rel_len_sq * c);
  if ((double)discriminant < 0.0) {
    return false;
  }
  if (0.0 < c) {
    if (0.0 < (double)(float)(c + (proj + (float)(rel_len_sq + proj)))) {
      if (proj >= 0.0) {
        return false;
      }
      if (rel_len_sq <= -proj) {
        return false;
      }
    }
    *out_t = (float)(-(double)(float)(proj + (double)sqrtf(discriminant)) / rel_len_sq);
    return true;
  }

  *out_t = 0.0f;
  return true;
}

static void fzgx_update_machine_approach_vector_exact(
    double distance,
    fzgx_machine_snapshot *machine,
    uint32_t other_machine_index,
    const fzgx_machine_snapshot *other_machine) {
  fzgx_mat43 transform;
  fzgx_vec3 local_dir;

  if ((machine == 0) || (other_machine == 0)) {
    return;
  }

  if (machine->machine_approach_frame_counter == 0u) {
    if (distance < (double)fzgx_vec3_length(machine->approach_dir)) {
      transform = machine->basis_physical;
      fzgx_mat43_set_origin_exact(&transform, machine->position);
      local_dir = fzgx_world_point_to_local(&transform, other_machine->position);
      if (fzgx_normalize_vec3_exact(&local_dir) && (local_dir.z < -0.1f)) {
        machine->approach_dir = local_dir;
        machine->last_machine_approached = (uint8_t)other_machine_index;
      }
    }
  } else if (machine->last_machine_approached == (uint8_t)other_machine_index) {
    transform = machine->basis_physical;
    fzgx_mat43_set_origin_exact(&transform, machine->position);
    local_dir = fzgx_world_point_to_local(&transform, other_machine->position);
    if (fzgx_normalize_vec3_exact(&local_dir)) {
      if (-0.1f < local_dir.z) {
        local_dir.z = -0.1f;
      }
      machine->approach_dir = local_dir;
    }
  }
}

static float fzgx_prepare_impact_direction_info_exact(
    const fzgx_machine_snapshot *machine,
    const fzgx_vec3 *impact_point,
    fzgx_collision_impact_info *info_out) {
  fzgx_mat43 transform;
  fzgx_vec3 surface_normal_local;
  float axis_measure;
  double axis_y_threshold;

  if ((machine == 0) || (impact_point == 0) || (info_out == 0)) {
    return 0.0f;
  }

  transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&transform, machine->position);
  info_out->relative_dir_local = fzgx_world_point_to_local(&transform, *impact_point);
  surface_normal_local = fzgx_world_vector_to_local(&machine->basis_physical, machine->surface_normal);
  info_out->relative_dir_local =
      fzgx_vec3_sub(info_out->relative_dir_local, surface_normal_local);
  if (!fzgx_normalize_vec3_exact(&info_out->relative_dir_local)) {
    info_out->relative_dir_local = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  }

  info_out->impact_axis_z = 0.0f;
  axis_y_threshold = 0.05 * fabs((double)info_out->relative_dir_local.y);
  axis_measure = (float)axis_y_threshold;
  if ((double)fabsf(info_out->relative_dir_local.x) <= axis_y_threshold) {
    if (axis_y_threshold <= (double)fabsf(info_out->relative_dir_local.z)) {
      info_out->relative_dir_world = (fzgx_vec3){0.0f, 0.0f, info_out->relative_dir_local.z};
      info_out->impact_axis_z = info_out->relative_dir_local.z;
      axis_measure = fabsf(info_out->relative_dir_local.z);
    } else {
      info_out->relative_dir_world = (fzgx_vec3){0.0f, info_out->relative_dir_local.y, 0.0f};
    }
  } else if ((double)fabsf(info_out->relative_dir_local.x) <= (double)fabsf(info_out->relative_dir_local.z)) {
    info_out->relative_dir_world = (fzgx_vec3){0.0f, 0.0f, info_out->relative_dir_local.z};
    info_out->impact_axis_z = info_out->relative_dir_local.z;
    axis_measure = fabsf(info_out->relative_dir_local.z);
  } else {
    info_out->relative_dir_world = (fzgx_vec3){info_out->relative_dir_local.x, 0.0f, 0.0f};
    axis_measure = fabsf(info_out->relative_dir_local.x);
  }

  if (!fzgx_normalize_vec3_exact(&info_out->relative_dir_world)) {
    info_out->relative_dir_world = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  }
  if (isfinite(machine->velocity.x) && isfinite(machine->velocity.y) && isfinite(machine->velocity.z)) {
    info_out->speed_per_mass = fzgx_vec3_length(machine->velocity) / machine->stat_weight;
  } else {
    info_out->speed_per_mass = 0.0f;
  }
  info_out->relative_dir_world =
      fzgx_transform_local_vector(&machine->basis_physical, info_out->relative_dir_world);
  return fabsf(axis_measure);
}

static double fzgx_scale_collision_impulse_and_damage_exact(
    const fzgx_machine_snapshot *machine,
    bool other_machine_b10) {
  double scale = 1.0;
  float spin_scale;

  if ((machine->machine_state & (FZGX_MS_SPINATTACKING | FZGX_MS_SIDEATTACKING)) == 0u) {
    if ((machine->machine_state & FZGX_MS_B10) != 0u) {
      scale *= 0.8;
    }
  } else if ((machine->machine_state & FZGX_MS_B10) == 0u) {
    spin_scale = 0.5f + 0.5f * (float)machine->spinattack_angle_decrement * 0.00024414062f;
    if (!other_machine_b10) {
      if ((machine->machine_state & FZGX_MS_SPINATTACKING) == 0u) {
        scale *= 2.0;
      } else {
        scale *= (double)(3.0f * spin_scale);
      }
    } else if ((machine->machine_state & FZGX_MS_SPINATTACKING) == 0u) {
      scale *= 6.0;
    } else {
      scale *= (double)(5.0f * spin_scale);
    }
  } else if ((machine->machine_state & FZGX_MS_SPINATTACKING) == 0u) {
    scale *= 4.0;
  } else {
    scale *= 3.5;
  }
  return scale;
}

static float fzgx_machine_pair_collision_radius_exact(const fzgx_machine_snapshot *machine) {
  float radius_scale = 1.0f;

  if ((machine->machine_state & FZGX_MS_B30) == 0u) {
    return machine->const_float_2_0;
  }
  if (machine->machine_id == 0u) {
    radius_scale = 2.0f;
  } else if (machine->machine_id == 1u) {
    radius_scale = 1.5f;
  }
  return 0.8f * machine->const_float_2_0 * radius_scale;
}

static bool fzgx_handle_machine_pair_collision_exact(
    uint32_t machine_index_a,
    fzgx_machine_snapshot *machine_a,
    uint32_t machine_index_b,
    fzgx_machine_snapshot *machine_b) {
  static const float kSweepLimit = 13.888888f;
  fzgx_vec3 separation;
  fzgx_vec3 sweep_start_a;
  fzgx_vec3 sweep_start_b;
  fzgx_vec3 velocity_a = machine_a->velocity;
  fzgx_vec3 velocity_b = machine_b->velocity;
  float hit_t;
  uint32_t started_intersecting;
  double weight_a;
  double weight_b;
  double radius_a;
  double radius_b;
  double distance;

  if ((machine_a == 0) || (machine_b == 0)) {
    return false;
  }
  if (((machine_a->state_2 | machine_b->state_2) & 0x10u) != 0u) {
    return false;
  }

  weight_a = (double)machine_a->stat_weight;
  weight_b = (double)machine_b->stat_weight;
  radius_a = (double)fzgx_machine_pair_collision_radius_exact(machine_a);
  radius_b = (double)fzgx_machine_pair_collision_radius_exact(machine_b);
  separation = fzgx_vec3_sub(machine_a->position, machine_b->position);
  distance = (double)fzgx_vec3_length(separation);
  if (distance > radius_a + radius_b + (double)machine_a->speed_kmh / 216.0 +
                     (double)machine_b->speed_kmh / 216.0) {
    if (machine_index_b < 32u) {
      machine_a->unk_random_0x514 &= ~(0x80000000u >> machine_index_b);
    }
    if (machine_index_a < 32u) {
      machine_b->unk_random_0x514 &= ~(0x80000000u >> machine_index_a);
    }
    return false;
  }

  fzgx_update_machine_approach_vector_exact(distance, machine_a, machine_index_b, machine_b);
  fzgx_update_machine_approach_vector_exact(distance, machine_b, machine_index_a, machine_a);

  if (fzgx_vec3_length(fzgx_vec3_sub(machine_a->position_old_dupe, machine_a->position)) <= kSweepLimit) {
    sweep_start_a = machine_a->position_old_dupe;
  } else {
    sweep_start_a = fzgx_vec3_sub(machine_a->position, machine_a->position_old_dupe);
    fzgx_vec3_set_length_exact(kSweepLimit, &sweep_start_a, &sweep_start_a);
    sweep_start_a = fzgx_vec3_add(machine_a->position, sweep_start_a);
    fzgx_vec3_set_length_exact(kSweepLimit * (float)weight_a, &machine_a->velocity, &velocity_a);
  }

  if (fzgx_vec3_length(fzgx_vec3_sub(machine_b->position_old_dupe, machine_b->position)) <= kSweepLimit) {
    sweep_start_b = machine_b->position_old_dupe;
  } else {
    sweep_start_b = fzgx_vec3_sub(machine_b->position, machine_b->position_old_dupe);
    fzgx_vec3_set_length_exact(kSweepLimit, &sweep_start_b, &sweep_start_b);
    sweep_start_b = fzgx_vec3_add(machine_b->position, sweep_start_b);
    fzgx_vec3_set_length_exact(kSweepLimit * (float)weight_b, &machine_b->velocity, &velocity_b);
  }

  if (!fzgx_swept_sphere_vs_swept_sphere_exact(
          radius_a,
          radius_b,
          &sweep_start_a,
          &machine_a->position,
          &sweep_start_b,
          &machine_b->position,
          &hit_t,
          &started_intersecting)) {
    if ((started_intersecting == 0u) && (machine_index_b < 32u) && (machine_index_a < 32u)) {
      machine_a->unk_random_0x514 &= ~(0x80000000u >> machine_index_b);
      machine_b->unk_random_0x514 &= ~(0x80000000u >> machine_index_a);
    }
    return false;
  }

  {
    fzgx_vec3 hit_pos_a;
    fzgx_vec3 hit_pos_b;
    fzgx_vec3 normal;
    fzgx_vec3 midpoint;
    fzgx_collision_impact_info impact_a;
    fzgx_collision_impact_info impact_b;
    float hit_distance;
    float overlap;
    float proj_a = 0.0f;
    float proj_b = 0.0f;
    double response_scale_a;
    double response_scale_b;
    bool both_b10;
    bool allow_damage_a = true;
    bool allow_damage_b = true;

    hit_t = 1.0f - hit_t;
    fzgx_ray_scale_exact(hit_t, &sweep_start_a, &machine_a->position, &hit_pos_a);
    fzgx_ray_scale_exact(hit_t, &sweep_start_b, &machine_b->position, &hit_pos_b);
    normal = fzgx_vec3_sub(hit_pos_b, hit_pos_a);
    hit_distance = fzgx_vec3_length(normal);
    if (!((double)hit_distance < (radius_a + radius_b)) || !((double)FLT_EPSILON < (double)hit_distance)) {
      if ((machine_index_b < 32u) && (machine_index_a < 32u)) {
        machine_a->unk_random_0x514 &= ~(0x80000000u >> machine_index_b);
        machine_b->unk_random_0x514 &= ~(0x80000000u >> machine_index_a);
      }
      return false;
    }
    if (((machine_index_b < 32u) && ((machine_a->unk_random_0x514 & (0x80000000u >> machine_index_b)) != 0u)) ||
        ((machine_index_a < 32u) && ((machine_b->unk_random_0x514 & (0x80000000u >> machine_index_a)) != 0u))) {
      return false;
    }

    fzgx_normalize_vec3_exact(&normal);
    overlap = 0.5f * ((0.01f + (float)(radius_a + radius_b)) - hit_distance);
    machine_a->position = fzgx_vec3_add(hit_pos_a, fzgx_vec3_scale(normal, -overlap));
    machine_b->position = fzgx_vec3_add(hit_pos_b, fzgx_vec3_scale(normal, overlap));
    midpoint = fzgx_vec3_scale(fzgx_vec3_add(machine_a->position, machine_b->position), 0.5f);

    if (fzgx_prepare_impact_direction_info_exact(machine_a, &midpoint, &impact_a) <=
        fzgx_prepare_impact_direction_info_exact(machine_b, &midpoint, &impact_b)) {
      midpoint = fzgx_vec3_scale(impact_b.relative_dir_world, -1.0f);
    } else {
      midpoint = impact_a.relative_dir_world;
    }

    if ((double)FLT_EPSILON < (double)fzgx_vec3_length(velocity_a)) {
      proj_a = (float)((double)fzgx_vec3_length(velocity_a) *
                       (double)fzgx_vec3_normalized_dot(midpoint, velocity_a) / weight_a);
    }
    if ((double)FLT_EPSILON < (double)fzgx_vec3_length(velocity_b)) {
      proj_b = (float)((double)fzgx_vec3_length(velocity_b) *
                       (double)fzgx_vec3_normalized_dot(midpoint, velocity_b) / weight_b);
    }

    {
      double impulse = (double)(2.0f * (proj_a - proj_b) / (float)(weight_a + weight_b));
      double impulse_a = (double)((float)(weight_a * impulse));
      double impulse_b = (double)((float)(-weight_b * impulse));
      fzgx_vec3 response_a;
      fzgx_vec3 response_b;
      double impulse_scale_a;
      double impulse_scale_b;
      double damage_scale_a;
      double damage_scale_b;
      float velocity_response_scale_a = 2.2f;
      float velocity_response_scale_b = 2.2f;

      machine_a->base_speed += 800.0f * (float)((double)impact_a.impact_axis_z * impulse);
      machine_b->base_speed += 800.0f * (float)((double)impact_b.impact_axis_z * impulse);
      if (machine_a->base_speed < 0.0f) {
        machine_a->base_speed = 0.0f;
      }
      if (machine_b->base_speed < 0.0f) {
        machine_b->base_speed = 0.0f;
      }

      impulse_scale_a = fzgx_scale_collision_impulse_and_damage_exact(
          machine_a, (machine_b->machine_state & FZGX_MS_B10) != 0u);
      impulse_scale_b = fzgx_scale_collision_impulse_and_damage_exact(
          machine_b, (machine_a->machine_state & FZGX_MS_B10) != 0u);
      if ((machine_a->machine_state & FZGX_MS_0HP) != 0u) {
        impulse_scale_a *= 1.5;
        impulse_scale_b *= 1.2;
      }
      if ((machine_b->machine_state & FZGX_MS_0HP) != 0u) {
        impulse_scale_b *= 1.5;
        impulse_scale_a *= 1.2;
      }

      response_a.x = normal.x * (float)(impulse_scale_b * (double)((float)(impulse_b * weight_a))) -
                     0.95f * normal.x * (float)(impulse_scale_b * (double)((float)(impulse_b * weight_a))) *
                         machine_a->surface_normal.x;
      response_a.y = normal.y * (float)(impulse_scale_b * (double)((float)(impulse_b * weight_a))) -
                     0.95f * normal.y * (float)(impulse_scale_b * (double)((float)(impulse_b * weight_a))) *
                         machine_a->surface_normal.y;
      response_a.z = normal.z * (float)(impulse_scale_b * (double)((float)(impulse_b * weight_a))) -
                     0.95f * normal.z * (float)(impulse_scale_b * (double)((float)(impulse_b * weight_a))) *
                         machine_a->surface_normal.z;
      response_b.x = normal.x * (float)(impulse_scale_a * (double)((float)(impulse_a * weight_b))) -
                     0.95f * normal.x * (float)(impulse_scale_a * (double)((float)(impulse_a * weight_b))) *
                         machine_b->surface_normal.x;
      response_b.y = normal.y * (float)(impulse_scale_a * (double)((float)(impulse_a * weight_b))) -
                     0.95f * normal.y * (float)(impulse_scale_a * (double)((float)(impulse_a * weight_b))) *
                         machine_b->surface_normal.y;
      response_b.z = normal.z * (float)(impulse_scale_a * (double)((float)(impulse_a * weight_b))) -
                     0.95f * normal.z * (float)(impulse_scale_a * (double)((float)(impulse_a * weight_b))) *
                         machine_b->surface_normal.z;
      machine_a->collision_response = response_a;
      machine_b->collision_response = response_b;

      if (((machine_a->machine_state | machine_b->machine_state) &
           (FZGX_MS_SIDEATTACKING | FZGX_MS_SPINATTACKING)) != 0u) {
        velocity_response_scale_a = 1.5f;
        velocity_response_scale_b = 1.5f;
      }
      if ((machine_a->machine_state & FZGX_MS_B30) != 0u) {
        velocity_response_scale_a = 1.0f;
      }
      if ((machine_b->machine_state & FZGX_MS_B30) != 0u) {
        velocity_response_scale_b = 1.0f;
      }

      {
        fzgx_vec3 scaled_response = fzgx_vec3_scale(response_a, velocity_response_scale_a);
        float velocity_len = fzgx_vec3_length(machine_a->velocity);
        float response_len = fzgx_vec3_length(response_a);

        if ((0.1f < velocity_len) && (0.1f < response_len)) {
          float dot = fzgx_vec3_normalized_dot(machine_a->velocity, response_a);
          if (0.0f < dot) {
            dot = 0.0f;
          }
          scaled_response = fzgx_vec3_scale(scaled_response, 1.0f + 0.7f * dot);
        }
        machine_a->velocity = fzgx_vec3_add(scaled_response, velocity_a);
      }
      {
        fzgx_vec3 scaled_response = fzgx_vec3_scale(response_b, velocity_response_scale_b);
        float velocity_len = fzgx_vec3_length(machine_b->velocity);
        float response_len = fzgx_vec3_length(response_b);

        if ((0.1f < velocity_len) && (0.1f < response_len)) {
          float dot = fzgx_vec3_normalized_dot(machine_b->velocity, response_b);
          if (0.0f < dot) {
            dot = 0.0f;
          }
          scaled_response = fzgx_vec3_scale(scaled_response, 1.0f + 0.7f * dot);
        }
        machine_b->velocity = fzgx_vec3_add(scaled_response, velocity_b);
      }

      both_b10 = ((machine_a->machine_state & FZGX_MS_B10) != 0u) &&
                 ((machine_b->machine_state & FZGX_MS_B10) != 0u);
      if ((machine_a->machine_state & FZGX_MS_B10) == 0u) {
        if (((machine_b->machine_state & FZGX_MS_B10) != 0u) &&
            ((machine_a->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_SPINATTACKING)) != 0u) &&
            ((machine_b->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_SPINATTACKING)) == 0u)) {
          allow_damage_a = false;
        }
      } else if (((machine_b->machine_state & FZGX_MS_B10) == 0u) &&
                 ((machine_b->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_SPINATTACKING)) != 0u) &&
                 ((machine_a->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_SPINATTACKING)) == 0u)) {
        allow_damage_b = false;
      }

      {
        fzgx_vec3 local_response = fzgx_world_vector_to_local(&machine_a->basis_physical, response_a);
        machine_a->visual_roll += local_response.x;
        machine_a->visual_pitch += local_response.z;
      }
      {
        fzgx_vec3 local_response = fzgx_world_vector_to_local(&machine_b->basis_physical, response_b);
        machine_b->visual_roll += local_response.x;
        machine_b->visual_pitch += local_response.z;
      }

      damage_scale_a =
          (impulse_scale_b * (double)(0.002f * fzgx_vec3_length(response_a))) / impulse_scale_a;
      if (both_b10) {
        damage_scale_a *= 0.001;
      }
      if ((machine_b->machine_state & FZGX_MS_0HP) != 0u) {
        damage_scale_a *= 0.3;
      }
      if (allow_damage_a && (machine_a->car_hit_invincibility == 0u)) {
        (void)fzgx_damage_machine_exact((float)damage_scale_a, machine_a);
      }

      damage_scale_b =
          (impulse_scale_a * (double)(0.002f * fzgx_vec3_length(response_b))) / impulse_scale_b;
      if (both_b10) {
        damage_scale_b *= 0.001;
      }
      if ((machine_a->machine_state & FZGX_MS_0HP) != 0u) {
        damage_scale_b *= 0.3;
      }
      if (allow_damage_b && (machine_b->car_hit_invincibility == 0u)) {
        (void)fzgx_damage_machine_exact((float)damage_scale_b, machine_b);
      }

      if ((machine_a->machine_state & FZGX_MS_0HP) != 0u) {
        machine_a->energy = 0.0f;
      }
      if ((machine_b->machine_state & FZGX_MS_0HP) != 0u) {
        machine_b->energy = 0.0f;
      }
    }

    machine_a->machine_state |= FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_ACTIVE;
    machine_b->machine_state |= FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_ACTIVE;
    fzgx_apply_machine_flags_from_exact_state(machine_a);
    fzgx_apply_machine_flags_from_exact_state(machine_b);
    return true;
  }
}

static bool fzgx_get_avg_track_normal_from_tilt_corners_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    uint32_t authored_track_id,
    fzgx_vec3 *out_normal) {
  const uint8_t *corner_indices = 0;
  uint8_t branch_slots[4];
  uint32_t valid_mask = 0u;
  uint32_t grounded_mask = 0u;
  int zero_generic_hit_corner_count = 0;
  int invalid_corner_count = 0;
  bool got_normal = false;
  bool sonic_oval = (authored_track_id == 0x24u) || (authored_track_id == 0x25u);
  size_t i;

  branch_slots[0] = 4u;
  branch_slots[1] = 4u;
  branch_slots[2] = 4u;
  branch_slots[3] = 4u;
  for (i = 0u; i < 4u; ++i) {
    fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[i];
    uint32_t scratch_generic_hit_present;
    uint32_t scratch_surface_flags;
    int32_t scratch_selected_cached_frame_index;

    fzgx_collision_scratch_set_piece_scratch_field_exact(world, &machine->corner_scratch[i]);
    fzgx_update_suspension_force_exact(world, machine_index, machine, corner);
    if (((corner->state & 2u) == 0u) ||
        ((sonic_oval && ((corner->state & 8u) == 0u)) &&
         (0.0f < machine->zero_minus_height_above_track))) {
      scratch_generic_hit_present = fzgx_collision_scratch_read_u32_exact(
          world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT);
      if (scratch_generic_hit_present == 0u) {
        zero_generic_hit_corner_count += 1;
      }
      scratch_surface_flags = fzgx_collision_scratch_read_u32_exact(
          world, FZGX_COLLISION_SCRATCH_SURFACE_FLAGS_OFFSET_EXACT);
      if ((scratch_surface_flags & 0x0c00u) == 0u) {
        if ((scratch_surface_flags & 0x03e00000u) != 0u) {
          scratch_selected_cached_frame_index = fzgx_collision_scratch_read_s32_exact(
              world, FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT);
          branch_slots[i] = (uint8_t)scratch_selected_cached_frame_index;
        }
      } else {
        invalid_corner_count += 1;
      }
      valid_mask |= 1u << i;
      if ((corner->state & 2u) == 0u) {
        grounded_mask |= 1u << i;
      }
    }
    machine->suspension_state[i] = (uint8_t)corner->state;
  }

  if ((grounded_mask != 0u) && (zero_generic_hit_corner_count == 0) && (authored_track_id == 0x0fu)) {
    grounded_mask = 0u;
    for (i = 0u; i < 4u; ++i) {
      fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[i];

      corner->state |= 0x0au;
      corner->force_spatial_len = 0.0f;
      corner->force = 0.0f;
      corner->up_vector.x = 0.0f;
      corner->up_vector.y = 1.0f;
      corner->up_vector.z = 0.0f;
      corner->up_vector_2.x = 0.0f;
      corner->up_vector_2.y = 1.0f;
      corner->up_vector_2.z = 0.0f;
      corner->force_spatial.x = 0.0f;
      corner->force_spatial.y = 0.0f;
      corner->force_spatial.z = 0.0f;
      machine->suspension_state[i] = (uint8_t)corner->state;
    }
  }

  if ((branch_slots[0] == branch_slots[1]) && (branch_slots[1] == branch_slots[2]) &&
      (branch_slots[2] == branch_slots[3])) {
    machine->branch_slot = branch_slots[0];
  }
  if (fzgx_debug_trace_double_branches_window_exact(
          authored_track_id, machine_index, world->frame_index)) {
    fprintf(
        stderr,
        "cornertrace|frame=%u|stage=avg_track_normal|valid=0x%02x|grounded=0x%02x|"
        "zero_generic=%d|invalid=%d|branch_slots=(%u,%u,%u,%u)|branch_slot=%u|"
        "corner_state=(0x%02x,0x%02x,0x%02x,0x%02x)\n",
        world->frame_index,
        valid_mask,
        grounded_mask,
        zero_generic_hit_corner_count,
        invalid_corner_count,
        branch_slots[0],
        branch_slots[1],
        branch_slots[2],
        branch_slots[3],
        machine->branch_slot,
        machine->suspension_state[0],
        machine->suspension_state[1],
        machine->suspension_state[2],
        machine->suspension_state[3]);
  }

  switch (grounded_mask) {
    case 0x0fu:
      corner_indices = fzgx_track_normal_corner_index_sets[0];
      break;
    case 0x0eu:
      corner_indices = fzgx_track_normal_corner_index_sets[1];
      break;
    case 0x0du:
      corner_indices = fzgx_track_normal_corner_index_sets[2];
      break;
    case 0x0bu:
      corner_indices = fzgx_track_normal_corner_index_sets[3];
      break;
    case 0x07u:
      corner_indices = fzgx_track_normal_corner_index_sets[4];
      break;
    default:
      break;
  }
  if ((corner_indices == 0) && sonic_oval) {
    switch (valid_mask) {
      case 0x0fu:
        corner_indices = fzgx_track_normal_corner_index_sets[0];
        break;
      case 0x0eu:
        corner_indices = fzgx_track_normal_corner_index_sets[1];
        break;
      case 0x0du:
        corner_indices = fzgx_track_normal_corner_index_sets[2];
        break;
      case 0x0bu:
        corner_indices = fzgx_track_normal_corner_index_sets[3];
        break;
      case 0x07u:
        corner_indices = fzgx_track_normal_corner_index_sets[4];
        break;
      default:
        break;
    }
  }

  if (corner_indices != 0) {
    fzgx_vec3 *pos0 = &machine->suspension_corners[corner_indices[0]].pos;
    fzgx_vec3 *pos1 = &machine->suspension_corners[corner_indices[1]].pos;
    fzgx_vec3 *pos2 = &machine->suspension_corners[corner_indices[2]].pos;
    fzgx_vec3 *pos3 = &machine->suspension_corners[corner_indices[3]].pos;
    float edge0_x = pos0->x - pos1->x;
    float edge0_y = pos0->y - pos1->y;
    float edge0_z = pos0->z - pos1->z;
    float edge1_x = pos2->x - pos3->x;
    float edge1_y = pos2->y - pos3->y;
    float edge1_z = pos2->z - pos3->z;
    fzgx_vec3 normal;

    normal.x = edge0_y * edge1_z - edge0_z * edge1_y;
    normal.z = edge0_x * edge1_y - edge0_y * edge1_x;
    normal.y = edge0_z * edge1_x - edge0_x * edge1_z;
    if (fzgx_normalize_vec3_exact(&normal)) {
      for (i = 0u; i < 4u; ++i) {
        fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[i];

        corner->up_vector_2 = normal;
        out_normal->x = corner->up_vector_2.x;
        out_normal->y = corner->up_vector_2.y;
        out_normal->z = corner->up_vector_2.z;
        if ((corner->state & 8u) == 0u) {
          corner->up_vector = normal;
          corner->force_spatial.x = corner->up_vector.x * corner->force_spatial_len;
          corner->force_spatial.y = corner->up_vector.y * corner->force_spatial_len;
          corner->force_spatial.z = corner->up_vector.z * corner->force_spatial_len;
        }
        got_normal = true;
      }
    }
    if (2 < invalid_corner_count) {
      machine->terrain_flags |= 0x02000000u;
      machine->state_2 |= 0x200u;
    }
  }

  return got_normal;
}

static void fzgx_build_default_track_fit_transform_exact(fzgx_mat43 *transform_out) {
  if (transform_out == 0) {
    return;
  }
  *transform_out = fzgx_mat43_identity_exact();
  fzgx_mat43_set_origin_exact(transform_out, (fzgx_vec3){0.0f, 1.0f, -0.1f});
}

static int32_t fzgx_select_nearest_cached_branch_frame_exact(
    const fzgx_sim_world *world,
    const fzgx_vec3 *point) {
  const uint8_t *scratch_raw;
  uint32_t cached_frame_count;
  int32_t selected_frame_index;
  float best_distance_squared;
  const uint8_t *frame_ptr;
  uint32_t frame_index;

  if ((world == 0) || (point == 0)) {
    return 0;
  }
  scratch_raw = fzgx_collision_scratch_raw_const_exact(world);
  if (scratch_raw == 0) {
    return 0;
  }
  cached_frame_count = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT);
  if (cached_frame_count < 2u) {
    return 0;
  }

  selected_frame_index = 1;
  frame_ptr =
      scratch_raw +
      FZGX_COLLISION_SCRATCH_INTERNAL_CACHED_FRAME_BANK_OFFSET_EXACT +
      sizeof(fzgx_track_frame_record);
  best_distance_squared =
      (point->x - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x0u)) *
          (point->x - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x0u)) +
      (point->y - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x4u)) *
          (point->y - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x4u)) +
      (point->z - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x8u)) *
          (point->z - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x8u));
  frame_ptr += sizeof(fzgx_track_frame_record);
  for (frame_index = 2u; frame_index < cached_frame_count; ++frame_index) {
    float distance_squared =
        (point->x - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x0u)) *
            (point->x - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x0u)) +
        (point->y - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x4u)) *
            (point->y - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x4u)) +
        (point->z - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x8u)) *
            (point->z - *(const float *)(frame_ptr + offsetof(fzgx_track_frame_record, track_anchor) + 0x8u));

    if (distance_squared < best_distance_squared) {
      selected_frame_index = (int32_t)frame_index;
      best_distance_squared = distance_squared;
    }
    frame_ptr += sizeof(fzgx_track_frame_record);
  }
  return selected_frame_index;
}

static fzgx_status fzgx_build_machine_track_fit_transform_from_racetrack_state_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_mat43 *transform_out) {
  const fzgx_track_manifest *track_manifest;
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  fzgx_machine_track_state *track;
  fzgx_status status;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  float checkpoint_distance;
  const uint8_t *scratch_raw;
  size_t frame_offset;
  fzgx_mat43 transform;
  fzgx_world_spherecast_request request;
  uint32_t probe_flags;
  uint32_t history_count;
  uint32_t authored_track_id;
  int32_t preferred_frame_index;
  uint32_t i;

  if ((world == 0) || (machine == 0) || (transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  track_manifest = fzgx_get_active_track_manifest(world);
  if ((track_manifest == 0) || (world->content == 0)) {
    fzgx_build_default_track_fit_transform_exact(transform_out);
    return FZGX_STATUS_OK;
  }
  if ((world == 0) ||
      (((world->static_collider_course == 0) && (world->track_mesh_course == 0)) &&
       ((world->content == 0) || (fzgx_get_active_track_course(world, &course) != FZGX_STATUS_OK)))) {
    return FZGX_STATUS_UNIMPLEMENTED;
  }
  authored_track_id = track_manifest->authored_track_id;
  status = fzgx_get_active_track_course(world, &course);
  if (status != FZGX_STATUS_OK) {
    fzgx_build_default_track_fit_transform_exact(transform_out);
    return status;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      world->content, world->active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = 0;
  } else if (status != FZGX_STATUS_OK) {
    return status;
  }

  track = &machine->track_state;
  checkpoint_index = track->last_cp_idx;
  checkpoint_fraction = track->last_cp_frac;
  if (checkpoint_index < 0) {
    fzgx_build_default_track_fit_transform_exact(transform_out);
    return FZGX_STATUS_OK;
  }

  status = fzgx_track_course_eval_shared_checkpoint_distance_exact(
      course, checkpoint_index, checkpoint_fraction, &checkpoint_distance);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  if ((track->prev_lap_cp < track->lap_start_cp) || (track->prev_lap_cross_cp < track->lap_cross_cp)) {
    checkpoint_distance = 5.0f;
    status = fzgx_track_course_find_checkpoint_for_track_distance(
        course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    track->lap_start_cp = track->prev_lap_cp;
    track->lap_cross_cp = track->prev_lap_cross_cp;
  }

  if (track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_CLOSED) {
    if ((checkpoint_distance < 5.0f) ||
        ((course->track_total_distance - checkpoint_distance) < 5.0f)) {
      if (0.0f <= track->last_cp_pos.z) {
        checkpoint_distance = course->track_total_distance - 5.0f;
      } else {
        checkpoint_distance = 5.0f;
      }
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }
  } else {
    if (checkpoint_distance < 5.0f) {
      checkpoint_distance = 5.0f;
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    } else if ((course->track_total_distance - checkpoint_distance) < 5.0f) {
      checkpoint_distance = course->track_total_distance - 5.0f;
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }
  }

  {
    int32_t probe_index;
    float probe_fraction;
    uint32_t can_traverse = 0u;

    status = fzgx_track_course_find_checkpoint_for_track_distance(
        course,
        checkpoint_distance + 20.0f,
        checkpoint_index,
        &probe_index,
        &probe_fraction);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
        course, probe_index, checkpoint_index, &can_traverse);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if ((probe_index != checkpoint_index) && (can_traverse == 0u)) {
      checkpoint_distance -= 20.0f;
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }

    status = fzgx_track_course_find_checkpoint_for_track_distance(
        course,
        checkpoint_distance - 20.0f,
        checkpoint_index,
        &probe_index,
        &probe_fraction);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
        course, probe_index, checkpoint_index, &can_traverse);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if ((probe_index != checkpoint_index) && (can_traverse == 0u)) {
      checkpoint_distance += 20.0f;
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }

    if ((authored_track_id == 0x10u) || (authored_track_id == 0x1cu)) {
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course,
          checkpoint_distance + 40.0f,
          checkpoint_index,
          &probe_index,
          &probe_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      status = fzgx_track_course_can_traverse_nonincreasing_checkpoint_variant_count(
          course, probe_index, checkpoint_index, &can_traverse);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((probe_index != checkpoint_index) && (can_traverse == 0u)) {
        checkpoint_distance -= 40.0f;
        status = fzgx_track_course_find_checkpoint_for_track_distance(
            course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }

      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course,
          checkpoint_distance - 20.0f,
          checkpoint_index,
          &probe_index,
          &probe_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      status = fzgx_track_course_can_traverse_nondecreasing_checkpoint_variant_count(
          course, probe_index, checkpoint_index, &can_traverse);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((probe_index != checkpoint_index) && (can_traverse == 0u)) {
        checkpoint_distance += 20.0f;
        status = fzgx_track_course_find_checkpoint_for_track_distance(
            course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }

    status = fzgx_track_course_find_checkpoint_for_track_distance(
        course,
        checkpoint_distance + 100.0f,
        checkpoint_index,
        &probe_index,
        &probe_fraction);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_can_traverse_checkpoint_interval_exact(
        course, probe_index, checkpoint_index, &can_traverse);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if ((probe_index != checkpoint_index) && (can_traverse == 0u)) {
      for (i = 0u; i < track_manifest->checkpoint_count; ++i) {
        uint32_t current_index =
            (uint32_t)((checkpoint_index + (int32_t)i + (int32_t)track_manifest->checkpoint_count) %
                       (int32_t)track_manifest->checkpoint_count);
        uint32_t next_index = (current_index + 1u) % track_manifest->checkpoint_count;
        const fzgx_track_node_record *track_node = 0;
        const fzgx_checkpoint_record *checkpoint = 0;

        status = fzgx_track_course_get_track_node(course, current_index, &track_node);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        if (track_node->checkpoint_count == 0u) {
          continue;
        }
        checkpoint = &course->checkpoints[track_node->checkpoint_offset];
        if (checkpoint->connect_to_track_out == 0u) {
          checkpoint_index = (int32_t)next_index;
          checkpoint_fraction = 0.0f;
          status = fzgx_track_course_eval_shared_checkpoint_distance_exact(
              course, checkpoint_index, checkpoint_fraction, &checkpoint_distance);
          if (status != FZGX_STATUS_OK) {
            return status;
          }
          break;
        }
      }
    }

    status = fzgx_track_course_find_checkpoint_for_track_distance(
        course,
        checkpoint_distance - 5.0f,
        checkpoint_index,
        &probe_index,
        &probe_fraction);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_can_traverse_checkpoint_interval_exact(
        course, probe_index, checkpoint_index, &can_traverse);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if ((probe_index != checkpoint_index) && (can_traverse == 0u)) {
      checkpoint_distance += 5.0f;
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }
  }

  if ((authored_track_id == 3u) &&
      ((checkpoint_index == 0x3e) || (checkpoint_index == 0x3f))) {
    checkpoint_index = 0x40;
    checkpoint_fraction = 0.0f;
    status = fzgx_track_course_eval_shared_checkpoint_distance_exact(
        course, checkpoint_index, checkpoint_fraction, &checkpoint_distance);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if ((authored_track_id == 0x23u) &&
      ((checkpoint_index == 0x90) ||
       ((checkpoint_index == 0x91) && (checkpoint_fraction < 0.1f)))) {
    checkpoint_index = 0x91;
    checkpoint_fraction = 0.1f;
    status = fzgx_track_course_eval_shared_checkpoint_distance_exact(
        course, checkpoint_index, checkpoint_fraction, &checkpoint_distance);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if (authored_track_id == 9u) {
    for (i = 0u; i < 4u; ++i) {
      if (((fzgx_track_fit_course9_window_pairs_exact[i][0] - 10.0f) < checkpoint_distance) &&
          (checkpoint_distance < (10.0f + fzgx_track_fit_course9_window_pairs_exact[i][1]))) {
        checkpoint_distance = 10.0f + fzgx_track_fit_course9_window_pairs_exact[i][1];
        status = fzgx_track_course_find_checkpoint_for_track_distance(
            course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        status = fzgx_track_course_eval_shared_checkpoint_distance_exact(
            course, checkpoint_index, checkpoint_fraction, &checkpoint_distance);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }
  }

  if (authored_track_id == 0x0du) {
    for (i = 0u; i < 7u; ++i) {
      if (((fzgx_track_fit_course13_window_pairs_exact[i][0] - 10.0f) < checkpoint_distance) &&
          (checkpoint_distance < (10.0f + fzgx_track_fit_course13_window_pairs_exact[i][1]))) {
        checkpoint_distance = 10.0f + fzgx_track_fit_course13_window_pairs_exact[i][1];
        status = fzgx_track_course_find_checkpoint_for_track_distance(
            course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        status = fzgx_track_course_eval_shared_checkpoint_distance_exact(
            course, checkpoint_index, checkpoint_fraction, &checkpoint_distance);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }
  }

  if ((authored_track_id == 0x0fu) &&
      ((16954.0f - 10.0f) < checkpoint_distance) &&
      (checkpoint_distance < (10.0f + 16970.0f))) {
    checkpoint_distance = 10.0f + 16970.0f;
    status = fzgx_track_course_find_checkpoint_for_track_distance(
        course, checkpoint_distance, checkpoint_index, &checkpoint_index, &checkpoint_fraction);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  status = fzgx_prepare_track_collision_query_for_checkpoint_exact(
      world,
      course,
      animation_course,
      (double)checkpoint_fraction,
      checkpoint_index,
      &machine->track_query_filter_cache);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  history_count = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT);
  if (history_count == 0u) {
    fzgx_build_default_track_fit_transform_exact(transform_out);
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  preferred_frame_index =
      fzgx_select_nearest_cached_branch_frame_exact(world, &track->last_cp_pos);
  if ((authored_track_id == 0x1bu) && (preferred_frame_index == 2)) {
    preferred_frame_index = 1;
  }
  scratch_raw = fzgx_collision_scratch_raw_const_exact(world);
  frame_offset =
      FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT +
      (size_t)(uint32_t)preferred_frame_index * sizeof(fzgx_track_frame_record);
  memcpy(&transform, scratch_raw + frame_offset, sizeof(transform));
  if (history_count > 4u) {
    history_count = 4u;
  }
  for (i = 0u; i < history_count; ++i) {
    track->cp_hist_idx[i] = checkpoint_index;
    track->cp_hist_frac[i] = checkpoint_fraction;
  }

  memset(&request, 0, sizeof(request));
  request.start = fzgx_transform_local_point(&transform, (fzgx_vec3){0.0f, -1000.0f, 0.0f});
  request.end = fzgx_transform_local_point(&transform, (fzgx_vec3){0.0f, 1000.0f, 0.0f});
  probe_flags = 0x00880001u;
  if ((preferred_frame_index >= 0) && (preferred_frame_index < 4)) {
    probe_flags |= 0x80000000u >> (uint32_t)preferred_frame_index;
  }
  request.flags = probe_flags;
  request.checkpoint_seed_index = track->cp_hist_idx[0];
  request.checkpoint_seed_aux = history_count;
  request.checkpoint_history_count = history_count;
  for (i = 0u; i < history_count; ++i) {
    request.checkpoint_history_index[i] = track->cp_hist_idx[i];
  }
  memset(request.checkpoint_history_fraction, 0, sizeof(request.checkpoint_history_fraction));
  request.machine_index = machine_index;
  status = fzgx_spherecast_vs_world_exact(world, &request, 0);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  {
    bool sweep_hit;
    fzgx_vec3 sweep_contact = request.start;
    fzgx_vec3 sweep_normal = {0.0f, 0.0f, 0.0f};
    float sweep_dist = 0.0f;

    sweep_hit = fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
        world, &request.start, &request.end, &sweep_contact, &sweep_normal, &sweep_dist);
    if (!sweep_hit) {
      request.flags &= 0x07ffffffu;
      status = fzgx_spherecast_vs_world_exact(world, &request, 0);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      sweep_contact = request.start;
      sweep_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
      sweep_dist = 0.0f;
      sweep_hit = fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
          world, &request.start, &request.end, &sweep_contact, &sweep_normal, &sweep_dist);
    }

    if (sweep_hit) {
      uint32_t fitted_mask = 0u;
      fzgx_vec3 fitted_points[4];
      int32_t selected_frame_index = fzgx_collision_scratch_read_s32_exact(
          world, FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT);
      fzgx_vec3 frame_forward;
      const uint8_t *corner_indices;

      if ((selected_frame_index < 0) || ((uint32_t)selected_frame_index >= history_count)) {
        selected_frame_index = preferred_frame_index;
      }
      if ((selected_frame_index >= 0) && ((uint32_t)selected_frame_index < history_count)) {
        scratch_raw = fzgx_collision_scratch_raw_const_exact(world);
        frame_offset =
            FZGX_COLLISION_SCRATCH_EXPORTED_CACHED_FRAMES_OFFSET_EXACT +
            (size_t)(uint32_t)selected_frame_index * sizeof(fzgx_track_frame_record);
        memcpy(&transform, scratch_raw + frame_offset, sizeof(transform));
      }
      fzgx_mat43_set_origin_exact(&transform, sweep_contact);
      track->last_cp_idx = fzgx_collision_scratch_read_s32_exact(
          world, FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT);
      track->last_cp_frac = fzgx_collision_scratch_read_f32_exact(
          world, FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT);

      for (i = 0u; i < 4u; ++i) {
        fzgx_world_spherecast_request corner_request;
        fzgx_vec3 offset = machine->suspension_corners[i].offset;
        fzgx_vec3 corner_contact;
        fzgx_vec3 corner_normal;
        float corner_dist = 0.0f;

        memset(&corner_request, 0, sizeof(corner_request));
        corner_request.start = fzgx_transform_local_point(
            &transform, (fzgx_vec3){offset.x, offset.y - 100.0f, offset.z});
        corner_request.end = fzgx_transform_local_point(
            &transform, (fzgx_vec3){offset.x, offset.y + 100.0f, offset.z});
        corner_request.flags = 0x00880001u;
        corner_request.checkpoint_seed_index = track->cp_hist_idx[0];
        corner_request.checkpoint_seed_aux = history_count;
        corner_request.checkpoint_history_count = history_count;
        for (uint32_t hist_i = 0u; hist_i < history_count; ++hist_i) {
          corner_request.checkpoint_history_index[hist_i] = track->cp_hist_idx[hist_i];
        }
        memset(
            corner_request.checkpoint_history_fraction,
            0,
            sizeof(corner_request.checkpoint_history_fraction));
        corner_request.machine_index = machine_index;
        status = fzgx_spherecast_vs_world_exact(world, &corner_request, 0);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        corner_contact = corner_request.start;
        corner_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
        if (fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
                world,
                &corner_request.start,
                &corner_request.end,
                &corner_contact,
                &corner_normal,
                &corner_dist)) {
          fitted_points[i] = corner_contact;
          fitted_mask |= (1u << i);
        }
      }

      corner_indices = fzgx_select_track_normal_corner_indices_exact(fitted_mask);
      if (corner_indices != 0) {
        fzgx_vec3 edge_a = fzgx_vec3_sub(
            fitted_points[corner_indices[0]], fitted_points[corner_indices[1]]);
        fzgx_vec3 edge_b = fzgx_vec3_sub(
            fitted_points[corner_indices[2]], fitted_points[corner_indices[3]]);
        fzgx_vec3 normal = {
            -(edge_a.z * edge_b.y - edge_a.y * edge_b.z),
            -(edge_a.x * edge_b.z - edge_a.z * edge_b.x),
            -(edge_a.y * edge_b.x - edge_a.x * edge_b.y),
        };

        if (fzgx_normalize_vec3_exact(&normal)) {
          fzgx_vec3 right;
          fzgx_vec3 forward;

          frame_forward = fzgx_mat43_get_basis_z_exact(&transform);
          right = (fzgx_vec3){
              normal.y * frame_forward.z - normal.z * frame_forward.y,
              normal.z * frame_forward.x - normal.x * frame_forward.z,
              normal.x * frame_forward.y - normal.y * frame_forward.x,
          };
          forward = (fzgx_vec3){
              right.y * normal.z - right.z * normal.y,
              right.z * normal.x - right.x * normal.z,
              right.x * normal.y - right.y * normal.x,
          };
          if (fzgx_normalize_vec3_exact(&right) && fzgx_normalize_vec3_exact(&forward)) {
            fzgx_mat43_set_basis_x_exact(&transform, right);
            fzgx_mat43_set_basis_y_exact(&transform, normal);
            fzgx_mat43_set_basis_z_exact(&transform, forward);
          }
        }
      }
    } else {
      track->last_cp_idx = checkpoint_index;
      track->last_cp_frac = checkpoint_fraction;
    }
  }

  if (authored_track_id == 10u) {
    float local_offset_x = 0.0f;
    uint32_t range_start = 0u;
    uint32_t range_end = 0u;

    if (((uint32_t)checkpoint_index - 0x7fu < 2u) || (checkpoint_index == 0x81)) {
      local_offset_x = -35.0f;
      range_start = 0x7fu;
      range_end = 0x81u;
    } else if (((uint32_t)checkpoint_index - 0x85u < 2u) || (checkpoint_index == 0x87)) {
      local_offset_x = -35.0f;
      range_start = 0x85u;
      range_end = 0x87u;
    } else if (((uint32_t)checkpoint_index - 0x8bu < 2u) || (checkpoint_index == 0x8d)) {
      local_offset_x = -35.0f;
      range_start = 0x8bu;
      range_end = 0x8du;
    } else if (((uint32_t)checkpoint_index - 0x82u < 2u) || (checkpoint_index == 0x84)) {
      local_offset_x = 35.0f;
      range_start = 0x82u;
      range_end = 0x84u;
    } else if (((uint32_t)checkpoint_index - 0x88u < 2u) || (checkpoint_index == 0x8a)) {
      local_offset_x = 35.0f;
      range_start = 0x88u;
      range_end = 0x8au;
    }

    if (local_offset_x != 0.0f) {
      const fzgx_track_node_record *start_node = 0;
      const fzgx_track_node_record *end_node = 0;

      status = fzgx_track_course_get_track_node(course, range_start, &start_node);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      status = fzgx_track_course_get_track_node(course, range_end, &end_node);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((start_node->checkpoint_count != 0u) && (end_node->checkpoint_count != 0u)) {
        const fzgx_checkpoint_record *start_checkpoint =
            &course->checkpoints[start_node->checkpoint_offset];
        const fzgx_checkpoint_record *end_checkpoint =
            &course->checkpoints[end_node->checkpoint_offset];
        float start_plane_distance =
            fzgx_plane_eval_point_exact(
                &start_checkpoint->plane_start,
                &(fzgx_vec3){transform.origin_x, transform.origin_y, transform.origin_z});
        float end_plane_distance =
            fzgx_plane_eval_point_exact(
                &end_checkpoint->plane_end,
                &(fzgx_vec3){transform.origin_x, transform.origin_y, transform.origin_z});
        double t = (double)start_plane_distance / (double)(start_plane_distance + end_plane_distance);

        if (t < 0.0) {
          t = 0.0;
        } else if (1.0 < t) {
          t = 1.0;
        }
        local_offset_x =
            (float)((double)local_offset_x -
                    (double)local_offset_x * fabs(((double)(2.0f * (float)t)) - 1.0));
      }
    }

    fzgx_mat43_translate_local_exact(
        &transform,
        (fzgx_vec3){
            local_offset_x,
            ((world->race_mode == fzgx_mode_story_exact) &&
             ((machine->machine_state & FZGX_MS_ACTIVE) == 0u))
                ? 0.1f
                : 1.0f,
            0.0f,
        });
  } else {
    fzgx_mat43_translate_local_exact(&transform, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  }
  *transform_out = transform;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_refresh_machine_track_fit_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_mat43 *transform_out) {
  return fzgx_build_machine_track_fit_transform_from_racetrack_state_exact(
      world, machine_index, machine, transform_out);
}

static fzgx_status fzgx_build_current_track_query_from_shared_point_exact(
    const fzgx_sim_world *world,
    const fzgx_vec3 *point,
    fzgx_current_track_query_result *query_out) {
  const fzgx_track_manifest *track_manifest;
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  fzgx_current_checkpoint_query_result checkpoint_query;
  fzgx_status status;

  if ((world == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  track_manifest = fzgx_get_active_track_manifest(world);
  if ((track_manifest == 0) || (world->content == 0)) {
    return FZGX_STATUS_NOT_CONFIGURED;
  }
  status = fzgx_get_active_track_course(world, &course);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      world->content, world->active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = 0;
  } else if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_track_course_build_shared_checkpoint_query_result(
      course,
      track_manifest->authored_track_id,
      track_manifest->circuit_type,
      point,
      &checkpoint_query);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
      course,
      animation_course,
      track_manifest->authored_track_id,
      track_manifest->circuit_type,
      point,
      checkpoint_query.checkpoint_index,
      checkpoint_query.checkpoint_fraction,
      query_out);
}

static fzgx_status fzgx_reset_machine_to_restore_transform_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    const fzgx_mat43 *restore_transform) {
  fzgx_current_track_query_result query_result;
  fzgx_status status;

  if ((world == 0) || (machine == 0) || (restore_transform == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(&query_result, 0, sizeof(query_result));
  status = fzgx_build_current_track_query_from_shared_point_exact(
      world,
      &(fzgx_vec3){restore_transform->origin_x, restore_transform->origin_y, restore_transform->origin_z},
      &query_result);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  fzgx_apply_machine_placement_transform(machine, restore_transform);
  fzgx_reset_machine_runtime_snapshot(machine, false);
  return fzgx_sim_world_refresh_machine_racetrack_state_from_query_result(
      world, machine_index, &query_result);
}

static bool fzgx_handle_machine_respawn_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine) {
  uint32_t restore_frames;

  if (machine == 0) {
    return false;
  }

  if (machine->post_restore_frame_countdown != 0u) {
    machine->unk_random_0x514 = 0xffffffffu;
    machine->post_restore_frame_countdown -= 1u;
  }

  if (machine->frames_until_restored == 0u) {
    return false;
  }

  machine->unk_random_0x514 = 0xffffffffu;
  machine->post_restore_frame_countdown = 0x3cu;
  machine->frames_until_restored = (uint16_t)(machine->frames_until_restored - 1u);

  if ((machine->machine_state & FZGX_MS_B29) != 0u) {
    machine->frames_until_restored = 0u;
    machine->position = fzgx_mat43_get_origin_exact(&machine->transform_visual);
    machine->position_old = machine->position;
    machine->basis_physical = machine->transform_visual;
    fzgx_mat43_set_origin_exact(&machine->basis_physical, (fzgx_vec3){0});
    machine->basis_physical_other = machine->basis_physical;
    machine->machine_state &= ~FZGX_MS_STARTINGCOUNTDOWN;
    fzgx_apply_machine_flags_from_exact_state(machine);
    return false;
  }

  restore_frames = (uint32_t)machine->frames_until_restored;
  if (restore_frames == 0u) {
    machine->machine_state &= ~FZGX_MS_STARTINGCOUNTDOWN;
    fzgx_apply_machine_flags_from_exact_state(machine);
    return false;
  }
  if (restore_frames < 0x14u) {
    machine->machine_state |= FZGX_MS_STARTINGCOUNTDOWN;
    fzgx_apply_machine_flags_from_exact_state(machine);
    return false;
  }

  if ((double)restore_frames < fzgx_respawn_target_capture_frame_exact) {
    if ((float)(restore_frames - 0x3cu) >= 45.0f) {
      machine->unk_restore_0x51c += fzgx_respawn_progress_delta_exact;
    } else {
      machine->unk_restore_0x51c -= fzgx_respawn_progress_delta_exact;
    }
    machine->restore_progress += machine->unk_restore_0x51c;
    if (machine->restore_progress < 0.0f) {
      machine->restore_progress = 0.0f;
    } else if (1.0f < machine->restore_progress) {
      machine->restore_progress = 1.0f;
    }

    fzgx_ray_scale_exact(
        machine->restore_progress,
        &(fzgx_vec3){machine->g_restore_matrix_1.origin_x,
                     machine->g_restore_matrix_1.origin_y,
                     machine->g_restore_matrix_1.origin_z},
        &(fzgx_vec3){machine->g_restore_mtx_3.origin_x,
                     machine->g_restore_mtx_3.origin_y,
                     machine->g_restore_mtx_3.origin_z},
        &machine->position);
    fzgx_mtx_slerp_exact(
        machine->restore_progress,
        &machine->g_restore_matrix_1,
        &machine->g_restore_mtx_3,
        &machine->basis_physical);
    fzgx_mat43_set_origin_exact(&machine->basis_physical, (fzgx_vec3){0});
    fzgx_mtx_slerp_exact(
        machine->restore_progress,
        &machine->g_restore_mtx_2,
        &machine->g_restore_mtx_3,
        &machine->transform_visual);
    fzgx_mat43_set_origin_exact(&machine->transform_visual, machine->position);

    if (restore_frames == 20u) {
      uint32_t saved_state = machine->machine_state;
      float saved_energy = machine->energy;
      uint16_t saved_restore_frames = machine->frames_until_restored;
      uint8_t saved_restore_count = machine->restore_count;
      fzgx_status status =
          fzgx_reset_machine_to_restore_transform_exact(world, machine_index, machine, &machine->g_restore_mtx_3);

      if (status == FZGX_STATUS_OK) {
        size_t i;

        machine->frames_until_restored = saved_restore_frames;
        machine->machine_state |= FZGX_MS_ACTIVE;
        machine->frames_since_start_2 = 60u;
        machine->restore_count = (uint8_t)(saved_restore_count + 1u);
        machine->machine_state |= saved_state & FZGX_MS_B1;
        if (machine->energy < (0.5f * machine->max_energy)) {
          machine->energy = 0.5f * machine->max_energy;
        }
        if (saved_energy > machine->energy) {
          machine->energy = saved_energy;
        }
        machine->machine_state |= FZGX_MS_STARTINGCOUNTDOWN;
        for (i = 0u; i < 4u; ++i) {
          machine->wall_corners[i].pos_a = fzgx_transform_local_point(
              &machine->transform_visual, (fzgx_vec3){0.0f, 1.0f, 0.0f});
        }
        (void)fzgx_refresh_machine_wall_contact_queries_exact(world, machine_index, machine);
      }
      fzgx_apply_machine_flags_from_exact_state(machine);
      return false;
    }

    fzgx_apply_machine_flags_from_exact_state(machine);
    return true;
  }

  if (restore_frames < FZGX_CRASH_RESTORE_FRAMES_DEFAULT_EXACT) {
    if ((double)restore_frames == fzgx_respawn_target_capture_frame_exact) {
      machine->restore_progress = 0.0f;
      machine->unk_restore_0x51c = 0.0f;
      machine->g_restore_matrix_1 = machine->basis_physical;
      fzgx_mat43_set_origin_exact(&machine->g_restore_matrix_1, machine->position);
      machine->g_restore_mtx_2 = machine->transform_visual;
      if (fzgx_refresh_machine_track_fit_transform_exact(
              world, machine_index, machine, &machine->g_restore_mtx_3) != FZGX_STATUS_OK) {
        machine->g_restore_mtx_3 = machine->g_restore_matrix_1;
      }
    }
    fzgx_apply_machine_flags_from_exact_state(machine);
    return false;
  }

  fzgx_mat43_set_origin_exact(&machine->transform_visual, machine->position);
  fzgx_apply_machine_flags_from_exact_state(machine);
  return false;
}

static void fzgx_update_tilt_corner_previous_world_pos_exact(
    const fzgx_mat43 *transform,
    fzgx_machine_tilt_corner_snapshot *corner) {
  if ((transform == 0) || (corner == 0)) {
    return;
  }
  corner->pos_old = fzgx_transform_local_point(
      transform,
      (fzgx_vec3){
          corner->offset.x,
          (corner->offset.y + corner->force) - corner->rest_length_scale,
          corner->offset.z,
      });
}

static void fzgx_set_wall_corner_sweep_origin_to_machine_pos_exact(
    const fzgx_machine_snapshot *machine,
    fzgx_machine_wall_corner_snapshot *wall_corner) {
  if ((machine == 0) || (wall_corner == 0)) {
    return;
  }
  wall_corner->pos_a = machine->position;
}

static void fzgx_apply_retired_velocity_damp_exact(fzgx_machine_snapshot *machine);
static void fzgx_handle_attack_states_exact(fzgx_machine_snapshot *machine);

static void fzgx_prepare_machine_frame_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    bool *found_ground_out,
    fzgx_vec3 *ground_normal_out) {
  size_t i;
  uint32_t old_terrain_flags;
  bool airborne = true;
  fzgx_mat43 old_transform = machine->basis_physical_other;
  fzgx_mat43 current_transform = machine->basis_physical;
  const fzgx_track_manifest *track_manifest = fzgx_get_active_track_manifest(world);

  if ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) != 0u) {
    machine->input_steer_yaw = 0.0f;
    machine->input_steer_pitch = 0.0f;
    machine->input_brake = 0.0f;
    machine->input_strafe = 0.0f;
    machine->machine_state &=
        ~(FZGX_MS_SIDEATTACKING | FZGX_MS_JUSTPRESSEDBOOST | FZGX_MS_SPINATTACKING);
  }
  old_terrain_flags = machine->terrain_flags;
  machine->machine_state &=
      ~(FZGX_MS_DIEDTHISFRAMEOOB_Q | FZGX_MS_B23 | FZGX_MS_RACEJUSTBEGAN_Q | FZGX_MS_JUSTTAPPEDACCEL |
        FZGX_MS_CROSSEDLAPLINE_Q | FZGX_MS_JUSTLANDED | FZGX_MS_AIRBORNEMORE0_2S_Q |
        FZGX_MS_AIRBORNE);
  machine->state_2 &= 0xfffffcffu;
  machine->terrain_flags = 0u;
  if ((machine->machine_state & FZGX_MS_B29) == 0u) {
    fzgx_set_terrain_state_from_track_exact(world, machine_index, machine);
  }
  if (((old_terrain_flags >> 28) & 1u) != 0u) {
    machine->machine_state &= ~FZGX_MS_B23;
  }

  fzgx_mat43_set_origin_exact(&old_transform, machine->position_old);
  machine->basis_physical_other = machine->basis_physical;
  machine->position_old_dupe = machine->position;
  machine->position_old = machine->position_old_dupe;
  for (i = 0u; i < 4u; ++i) {
    fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[i];

    fzgx_update_tilt_corner_previous_world_pos_exact(&old_transform, corner);
    if ((machine_index == 0u) &&
        (842u <= world->frame_index) &&
        (world->frame_index <= 860u)) {
      fprintf(
          stderr,
          "posold|frame=%u|stage=prepare_set_old|corner=%zu|force=%.6f|state=0x%02x|"
          "pos=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          i,
          corner->force,
          corner->state,
          corner->pos.x,
          corner->pos.y,
          corner->pos.z,
          corner->pos_old.x,
          corner->pos_old.y,
          corner->pos_old.z);
    }
    if ((corner->state & 4u) != 0u) {
      size_t j;
      for (j = 0u; j < 4u; ++j) {
        machine->suspension_corners[j].state |= 4u;
        machine->suspension_state[j] = (uint8_t)machine->suspension_corners[j].state;
      }
    }
  }
  fzgx_mat43_set_origin_exact(&current_transform, machine->position);
  if (found_ground_out != 0) {
    *found_ground_out = false;
  }
  if (ground_normal_out != 0) {
    *ground_normal_out = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  }
  if (((machine->machine_state & FZGX_MS_ACTIVE) != 0u) && (track_manifest != 0)) {
    fzgx_vec3 ground_normal;

    if (fzgx_get_avg_track_normal_from_tilt_corners_exact(
            world, machine_index, machine, track_manifest->authored_track_id, &ground_normal)) {
      if (found_ground_out != 0) {
        *found_ground_out = true;
      }
      if (ground_normal_out != 0) {
        *ground_normal_out = ground_normal;
      }
    }
  }
  for (i = 0u; i < 4u; ++i) {
    fzgx_set_wall_corner_sweep_origin_to_machine_pos_exact(machine, &machine->wall_corners[i]);
  }
  for (i = 0u; i < 4u; ++i) {
    if ((machine->suspension_corners[i].state & 2u) == 0u) {
      airborne = false;
    }
  }
  if (airborne) {
    machine->machine_state |= FZGX_MS_AIRBORNE;
    if (machine->air_time < 180u) {
      machine->air_time += 1u;
    }
    if (10u < machine->air_time) {
      machine->machine_state |= FZGX_MS_AIRBORNEMORE0_2S_Q;
    }
  } else {
    if (machine->air_time != 0u) {
      machine->machine_state |= FZGX_MS_JUSTLANDED;
    }
    machine->air_time = 0u;
    machine->machine_state &= ~FZGX_MS_AIRBORNEMORE0_2S_Q;
    machine->state_2 &= ~2u;
  }

  machine->turning_related = 0.0f;
  machine->visual_roll *= 0.8f;
  machine->visual_pitch *= 0.9f;
  if (((machine->machine_state & FZGX_MS_ACTIVE) != 0u) && (machine->frames_since_start_2 != 0xffu)) {
    machine->frames_since_start_2 += 1u;
  }
  if (((machine->machine_state & FZGX_MS_COMPLETEDRACE_1_Q) != 0u) ||
      (((machine->terrain_flags >> 27) & 1u) != 0u)) {
    machine->energy += 1.111111f;
    if (machine->max_energy < machine->energy) {
      machine->energy = machine->max_energy;
    }
  }

  fzgx_refresh_machine_speed_kmh_exact(machine);
  fzgx_apply_retired_velocity_damp_exact(machine);
  fzgx_handle_attack_states_exact(machine);
  if (machine->car_hit_invincibility == 0u) {
    if ((machine->machine_state & FZGX_MS_JUSTHITVEHICLE_Q) != 0u) {
      machine->car_hit_invincibility = 6u;
    }
  } else {
    machine->car_hit_invincibility -= 1u;
  }
  if (machine->breakdown_frame_counter != 0u) {
    machine->breakdown_frame_counter -= 1u;
  }
  machine->velocity_local = fzgx_world_vector_to_local(&current_transform, machine->velocity);
  {
    float turn_degrees =
        -(machine->input_steer_yaw * machine->stat_turn_reaction +
          machine->input_strafe * machine->stat_strafe);
    fzgx_mat43 rotated_basis = current_transform;

    if (turn_degrees < -45.0f) {
      turn_degrees = -45.0f;
    } else if (45.0f < turn_degrees) {
      turn_degrees = 45.0f;
    }
    fzgx_mat43_rotate_about_y_right(&rotated_basis, (uint16_t)(int)(182.04445f * turn_degrees));
    machine->velocity_local_flattened_and_rotated =
        fzgx_world_vector_to_local(&rotated_basis, machine->velocity);
    machine->velocity_local_flattened_and_rotated.y = 0.0f;
  }
  machine->position_old_2 = machine->position;
  machine->frames_since_start += 1u;
  if (machine->unk_byte_0x4c3 != 0u) {
    uint8_t low_nibble = (machine->unk_byte_0x4c3 & 0x0fu);

    if (low_nibble <= 1u) {
      machine->unk_byte_0x4c3 = 0u;
    } else {
      machine->unk_byte_0x4c3 =
          (uint8_t)((machine->unk_byte_0x4c3 & 0xf0u) | ((low_nibble - 1u) & 0x0fu));
    }
  }
  machine->approach_dir = (fzgx_vec3){10000.0f, 0.0f, 0.0f};
  if (machine->machine_approach_frame_counter != 0u) {
    machine->machine_approach_frame_counter -= 1u;
  }
  if (machine->time_since_ko_frame_counter != 0u) {
    if (machine->time_since_ko_frame_counter < 120u) {
      machine->time_since_ko_frame_counter += 1u;
    } else {
      machine->time_since_ko_frame_counter = 0u;
    }
  }
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static bool fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
    fzgx_sim_world *world,
    const fzgx_vec3 *start_point,
    const fzgx_vec3 *end_point,
    fzgx_vec3 *in_out_contact,
    fzgx_vec3 *out_surface_normal,
    float *contact_dist_out) {
  size_t selected_slot_offset;
  uint32_t track_result_flags;
  uint32_t generic_result_flags;
  bool have_selected_slot = false;
  float selected_hit_time = 0.0f;
  float track_hit_time;
  float generic_hit_time;
  float sweep_length;

  if ((world == 0) || (start_point == 0) || (end_point == 0) || (in_out_contact == 0) ||
      (out_surface_normal == 0) || (contact_dist_out == 0)) {
    return false;
  }

  track_result_flags = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT);
  generic_result_flags = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT);
  if ((track_result_flags == 0u) && (generic_result_flags == 0u)) {
    *contact_dist_out = 0.0f;
    return false;
  }

  if (track_result_flags != 0u) {
    selected_slot_offset = FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT;
    track_hit_time = fzgx_collision_scratch_read_f32_exact(
        world,
        FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
            FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT);
    selected_hit_time = track_hit_time;
    have_selected_slot = true;
  }
  if (generic_result_flags != 0u) {
    generic_hit_time = fzgx_collision_scratch_read_f32_exact(
        world,
        FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
            FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT);
    if (!have_selected_slot || (generic_hit_time <= selected_hit_time)) {
      selected_slot_offset = FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT;
      selected_hit_time = generic_hit_time;
      have_selected_slot = true;
    }
  }

  if (!have_selected_slot) {
    *contact_dist_out = 0.0f;
    return false;
  }

  if ((selected_slot_offset != FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT) &&
      (selected_slot_offset != FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT)) {
    *contact_dist_out = 0.0f;
    return false;
  }

  if (selected_slot_offset == FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT) {
    track_hit_time = selected_hit_time;
  } else {
    generic_hit_time = selected_hit_time;
  }

  *in_out_contact = fzgx_collision_scratch_read_vec3_exact(
      world, selected_slot_offset + FZGX_COLLISION_SCRATCH_HIT_SLOT_POINT_OFFSET_EXACT);
  *out_surface_normal = fzgx_collision_scratch_read_vec3_exact(
      world, selected_slot_offset + FZGX_COLLISION_SCRATCH_HIT_SLOT_NORMAL_OFFSET_EXACT);
  sweep_length = fzgx_vec3_length(fzgx_vec3_sub(*start_point, *end_point));
  *contact_dist_out = (1.0f - selected_hit_time) * sweep_length;
  return true;
}

static bool fzgx_find_floor_beneath_machine_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine) {
  uint32_t mask = 0x4060005u;
  int32_t next_cp_idx;
  int32_t cur_cp_idx;
  int32_t track_id = -1;
  fzgx_mat43 transform = machine->basis_physical;
  fzgx_vec3 machine_position;
  fzgx_vec3 sweep_contact;
  fzgx_vec3 sweep_normal;
  fzgx_vec3 bottom_probe;
  fzgx_world_spherecast_request request;
  fzgx_status status;
  bool sweep_hit;
  float contact_dist;
  bool trace_floating_window = false;

  next_cp_idx = machine->track_state.next_cp_idx;
  cur_cp_idx = machine->track_state.cur_cp_idx;
  if ((next_cp_idx < 0) || (next_cp_idx == cur_cp_idx)) {
    mask = 0x4260005u;
  }
  machine->terrain_flags_2 = 0u;
  fzgx_mat43_set_origin_exact(&transform, machine->position);
  bottom_probe = fzgx_transform_local_point(&transform, (fzgx_vec3){0.0f, -2000.0f, 0.0f});
  machine_position = machine->position;
  machine->position_bottom = bottom_probe;
  sweep_contact = bottom_probe;
  sweep_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  {
    const fzgx_track_manifest *track_manifest = fzgx_get_active_track_manifest(world);

    if (track_manifest != 0) {
      track_id = (int32_t)track_manifest->authored_track_id;
    }
  }
  trace_floating_window =
      (machine_index == 0u) &&
      (1760u <= world->frame_index) &&
      (world->frame_index <= 1905u);

  memset(&request, 0, sizeof(request));
  request.start = machine->position_bottom;
  request.end = machine_position;
  request.flags = mask;
  fzgx_seed_world_spherecast_from_machine_track_state_exact(&request, &machine->track_state);
  request.machine_index = machine_index;
  if (trace_floating_window ||
      ((track_id >= 0) &&
      fzgx_debug_trace_double_branches_window_exact(
          (uint32_t)track_id, machine_index, world->frame_index))) {
    fzgx_debug_log_world_spherecast_seed_exact(
        "floortrace", world->frame_index, &request, &machine->track_state);
  }
  if (((int16_t)track_id == 0x24) || ((int16_t)track_id == 0x25)) {
    const fzgx_track_side_query_summary *corner0_latest =
        fzgx_track_side_query_buffer_get_record_const_exact(
            &machine->corner_scratch[0], machine->corner_scratch[0].latest_record_offset);
    const fzgx_track_side_query_summary *corner3_latest =
        fzgx_track_side_query_buffer_get_record_const_exact(
            &machine->corner_scratch[3], machine->corner_scratch[3].latest_record_offset);
    const fzgx_track_side_query_summary *corner3_write =
        fzgx_track_side_query_buffer_get_record_const_exact(
            &machine->corner_scratch[3], machine->corner_scratch[3].write_record_offset);

    request.has_sonic_oval_floor_bias = true;
    request.sonic_oval_floor_bias.candidate_count = 1u;
    request.sonic_oval_floor_bias.slot0_piece_opaque = corner0_latest->piece_opaque[0];
    request.sonic_oval_floor_bias.slot0_checkpoint_index = corner0_latest->checkpoint_index[0];
    request.sonic_oval_floor_bias.latest_slot0_flags = corner3_latest->flags[0];
    request.sonic_oval_floor_bias.alternate_slot0_flags = corner3_write->flags[0];
  }
  status = fzgx_spherecast_vs_world_exact(world, &request, 0);
  sweep_hit = false;
  if (status == FZGX_STATUS_OK) {
    sweep_hit = fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
        world,
        &machine->position_bottom,
        &machine_position,
        &sweep_contact,
        &sweep_normal,
        &contact_dist);
  }
  if (!sweep_hit) {
    contact_dist = 0.0f;
  } else {
    uint32_t scratch_surface_flags;

    contact_dist = 20.0f + (contact_dist - 2000.0f);
    scratch_surface_flags = fzgx_collision_scratch_read_u32_exact(
        world, FZGX_COLLISION_SCRATCH_SURFACE_FLAGS_OFFSET_EXACT);
    if ((contact_dist <= 0.0f) &&
        ((machine->machine_state & FZGX_MS_FALLOUT) == 0u) &&
        ((scratch_surface_flags & 0x01c00000u) != 0u) &&
        ((mask & 0x00200000u) != 0u) &&
        (((scratch_surface_flags & 0x00800000u) == 0u) || ((int16_t)track_id != 0x0f))) {
      contact_dist = FLT_EPSILON;
    }
    machine->terrain_flags_2 = fzgx_collision_scratch_read_u32_exact(
        world, FZGX_COLLISION_SCRATCH_MATERIAL_FLAGS_OFFSET_EXACT);
  }
  if (0.0f < contact_dist) {
    machine->surface_normal = sweep_normal;
    machine->zero_minus_height_above_track = contact_dist;
    machine->current_checkpoint = fzgx_collision_scratch_read_s32_exact(
        world, FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT);
    machine->checkpoint_fraction = fzgx_collision_scratch_read_f32_exact(
        world, FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT);
    machine->branch_indicator = fzgx_collision_scratch_get_selected_frame_flags_exact(world);
    machine->floor_surface_flags = fzgx_collision_scratch_read_u32_exact(
        world, FZGX_COLLISION_SCRATCH_SURFACE_FLAGS_OFFSET_EXACT);
  if (((machine_index == 0u) && (120u <= world->frame_index) && (world->frame_index <= 130u)) ||
      trace_floating_window ||
      ((track_id >= 0) &&
       fzgx_debug_trace_double_branches_window_exact(
           (uint32_t)track_id, machine_index, world->frame_index))) {
      fprintf(
          stderr,
          "floortrace|frame=%u|stage=hit|cp=%d|cpf=%.6f|sel=%d|cached=%u|surface=0x%08x|"
          "track_flags=0x%08x|generic_flags=0x%08x|candA=0x%08x|candB=0x%08x|"
          "track_t=%.6f|generic_t=%.6f|"
          "branch_indicator=0x%08x|height=%.6f|pos=(%.3f,%.3f,%.3f)|normal=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->current_checkpoint,
          machine->checkpoint_fraction,
          fzgx_collision_scratch_read_s32_exact(
              world, FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT),
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT),
          machine->floor_surface_flags,
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT),
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT),
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT),
          fzgx_collision_scratch_read_u32_exact(
              world, FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT),
          fzgx_collision_scratch_read_f32_exact(
              world,
              FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
                  FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT),
          fzgx_collision_scratch_read_f32_exact(
              world,
              FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
                  FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT),
          machine->branch_indicator,
          machine->zero_minus_height_above_track,
          machine->position_bottom.x,
          machine->position_bottom.y,
          machine->position_bottom.z,
          machine->surface_normal.x,
          machine->surface_normal.y,
          machine->surface_normal.z);
    }
    return true;
  }

  if (((machine_index == 0u) &&
       (((120u <= world->frame_index) && (world->frame_index <= 130u)) ||
        (world->frame_index < 5u))) ||
      trace_floating_window ||
      ((track_id >= 0) &&
       fzgx_debug_trace_double_branches_window_exact(
           (uint32_t)track_id, machine_index, world->frame_index))) {
    fprintf(
        stderr,
        "floortrace|frame=%u|stage=miss|track_flags=0x%08x|generic_flags=0x%08x|"
        "candA=0x%08x|candB=0x%08x|track_t=%.6f|generic_t=%.6f|sel=%d|cached=%u|cp_idx=%d|cpf=%.6f|"
        "start=(%.3f,%.3f,%.3f)|end=(%.3f,%.3f,%.3f)\n",
        world->frame_index,
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_TRACK_HIT_RESULT_FLAGS_OFFSET_EXACT),
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_GENERIC_HIT_RESULT_FLAGS_OFFSET_EXACT),
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_A_RESULT_FLAGS_OFFSET_EXACT),
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_TRACK_CANDIDATE_B_RESULT_FLAGS_OFFSET_EXACT),
        fzgx_collision_scratch_read_f32_exact(
            world,
            FZGX_COLLISION_SCRATCH_TRACK_HIT_SLOT_OFFSET_EXACT +
                FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT),
        fzgx_collision_scratch_read_f32_exact(
            world,
            FZGX_COLLISION_SCRATCH_GENERIC_HIT_SLOT_OFFSET_EXACT +
                FZGX_COLLISION_SCRATCH_HIT_SLOT_TIME_OFFSET_EXACT),
        fzgx_collision_scratch_read_s32_exact(
            world, FZGX_COLLISION_SCRATCH_SELECTED_CACHED_FRAME_OFFSET_EXACT),
        fzgx_collision_scratch_read_u32_exact(
            world, FZGX_COLLISION_SCRATCH_CACHED_FRAME_COUNT_OFFSET_EXACT),
        fzgx_collision_scratch_read_s32_exact(
            world, FZGX_COLLISION_SCRATCH_CHECKPOINT_INDEX_OFFSET_EXACT),
        fzgx_collision_scratch_read_f32_exact(
            world, FZGX_COLLISION_SCRATCH_CHECKPOINT_FRACTION_OFFSET_EXACT),
        machine->position_bottom.x,
        machine->position_bottom.y,
        machine->position_bottom.z,
        machine_position.x,
        machine_position.y,
        machine_position.z);
  }

  machine->surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  machine->position_bottom = machine_position;
  machine->zero_minus_height_above_track = 0.0f;
  machine->branch_indicator = 0u;
  machine->floor_surface_flags = 0u;
  return false;
}

static bool fzgx_sweep_machine_center_to_wall_corner_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    const fzgx_vec3 *end_point,
    uint32_t query_flags,
    fzgx_vec3 *out_normal,
    fzgx_vec3 *out_hit_point) {
  fzgx_world_spherecast_request request;
  fzgx_status status;
  bool sweep_hit;
  fzgx_vec3 sweep_contact;
  fzgx_vec3 sweep_normal;
  uint32_t hit_info_flags;

  if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
    query_flags &= 0xfffff8ffu;
  }

  memset(&request, 0, sizeof(request));
  request.start = machine->position;
  request.end = *end_point;
  request.flags = query_flags;
  fzgx_seed_world_spherecast_from_machine_track_state_exact(&request, &machine->track_state);
  request.machine_index = machine_index;
  status = fzgx_spherecast_vs_world_exact(world, &request, 0);
  sweep_contact = machine->position;
  sweep_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  sweep_hit = false;
  hit_info_flags = 0u;
  if (status == FZGX_STATUS_OK) {
    sweep_hit = fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
        world,
        &machine->position,
        end_point,
        &sweep_contact,
        &sweep_normal,
        &(float){0.0f});
    if (sweep_hit) {
      hit_info_flags = fzgx_collision_scratch_read_u32_exact(
          world, FZGX_COLLISION_SCRATCH_HIT_INFO_FLAGS_OFFSET_EXACT);
    }
  }
  if (!sweep_hit) {
    *out_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    *out_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    return false;
  }
  if (hit_info_flags == 0x100u) {
    machine->machine_state |= FZGX_MS_FALLOUT;
    *out_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    *out_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    fzgx_apply_machine_flags_from_exact_state(machine);
    return false;
  }
  *out_normal =
      fzgx_collision_scratch_read_vec3_exact(world, FZGX_COLLISION_SCRATCH_HIT_NORMAL_OFFSET_EXACT);
  *out_hit_point = sweep_contact;
  if (((hit_info_flags & 0x600u) != 0u) && ((machine->state_2 & 0x10u) == 0u)) {
    machine->machine_state |= FZGX_MS_FALLOUT;
    machine->machine_state |= FZGX_MS_0HP;
    machine->machine_state |= FZGX_MS_TOOKDAMAGE;
    machine->machine_state |= FZGX_MS_DIEDTHISFRAMEOOB_Q;
    machine->state_2 |= 0x10u;
    machine->energy = 0.0f;
    machine->base_speed = 0.0f;
    machine->velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    machine->angular_velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    machine->speed_kmh = 0.0f;
    machine->state_2 &= 0xffffff7fu;
  }
  if ((hit_info_flags & 0x100u) != 0u) {
    machine->machine_state |= FZGX_MS_FALLOUT;
  }
  fzgx_apply_machine_flags_from_exact_state(machine);
  return true;
}

static void fzgx_sweep_machine_wall_corner_contacts_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine,
    fzgx_machine_wall_corner_snapshot *corner,
    size_t corner_index,
    uint32_t query_flags,
    fzgx_vec3 out_pushes[4],
    uintptr_t out_summary_flags_ptr_exact[4],
    uint32_t out_presence_masks[4],
    uint32_t corner_mask_bit) {
  const fzgx_track_manifest *track_manifest = fzgx_get_active_track_manifest(world);
  fzgx_mat43 transform = machine->basis_physical;
  fzgx_track_side_query_buffer_exact *shared_scratch =
      ((machine != 0) && (corner_index < 4u)) ? &machine->corner_scratch[corner_index] : 0;
  fzgx_world_spherecast_request request;
  fzgx_status status;
  fzgx_vec3 sweep_contact;
  fzgx_vec3 sweep_normal;
  float sweep_dist;
  bool sweep_hit;
  uint32_t hit_info_flags;
  size_t i;

  fzgx_mat43_set_origin_exact(&transform, machine->position);
  corner->pos_b = fzgx_transform_local_point(&transform, corner->offset);
  memset(&request, 0, sizeof(request));
  request.start = corner->pos_b;
  request.end = corner->pos_a;
  request.flags = query_flags;
  fzgx_seed_world_spherecast_from_machine_track_state_exact(&request, &machine->track_state);
  request.machine_index = machine_index;
  status = fzgx_spherecast_vs_world_with_piece_scratch_exact(world, &request, shared_scratch, 0);
  if ((shared_scratch != 0) && (machine != 0)) {
    fzgx_swap_machine_corner_contact_scratch_exact(machine, corner_index);
  }
  for (i = 0u; i < 4u; ++i) {
    out_pushes[i] = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    out_summary_flags_ptr_exact[i] = (uintptr_t)0;
  }
  if (status != FZGX_STATUS_OK) {
    return;
  }
  sweep_contact = corner->pos_b;
  sweep_normal = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  sweep_dist = 0.0f;
  sweep_hit = fzgx_finish_sphere_sweep_world_from_collision_scratch_exact(
      world,
      &corner->pos_b,
      &corner->pos_a,
      &sweep_contact,
      &sweep_normal,
      &sweep_dist);
  if (!sweep_hit) {
    return;
  }
  if (fzgx_collision_scratch_read_u32_exact(world, FZGX_COLLISION_SCRATCH_SLOT0_PRESENT_OFFSET_EXACT) != 0u) {
    out_pushes[0] =
        fzgx_collision_scratch_read_vec3_exact(world, FZGX_COLLISION_SCRATCH_SLOT0_PUSH_OFFSET_EXACT);
    out_summary_flags_ptr_exact[0] = fzgx_track_side_query_summary_slot_flags_ptr_from_opaque_exact(
        shared_scratch,
        fzgx_collision_scratch_read_u32_exact(world, FZGX_COLLISION_SCRATCH_SLOT0_INFO_OFFSET_EXACT));
    out_presence_masks[0] |= corner_mask_bit;
  }
  if (fzgx_collision_scratch_read_u32_exact(world, FZGX_COLLISION_SCRATCH_SLOT1_PRESENT_OFFSET_EXACT) != 0u) {
    out_pushes[1] =
        fzgx_collision_scratch_read_vec3_exact(world, FZGX_COLLISION_SCRATCH_SLOT1_PUSH_OFFSET_EXACT);
    out_summary_flags_ptr_exact[1] = fzgx_track_side_query_summary_slot_flags_ptr_from_opaque_exact(
        shared_scratch,
        fzgx_collision_scratch_read_u32_exact(world, FZGX_COLLISION_SCRATCH_SLOT1_INFO_OFFSET_EXACT));
    out_presence_masks[1] |= corner_mask_bit;
  }
  hit_info_flags = fzgx_collision_scratch_read_u32_exact(
      world, FZGX_COLLISION_SCRATCH_HIT_INFO_FLAGS_OFFSET_EXACT);
  if (hit_info_flags != 0u) {
    if ((hit_info_flags & 0x100u) == 0u) {
      out_pushes[2] =
          fzgx_collision_scratch_read_vec3_exact(world, FZGX_COLLISION_SCRATCH_HIT_NORMAL_OFFSET_EXACT);
      if (((hit_info_flags & 0x600u) != 0u) && ((machine->state_2 & 0x10u) == 0u)) {
        machine->machine_state |=
            FZGX_MS_FALLOUT | FZGX_MS_0HP | FZGX_MS_TOOKDAMAGE | FZGX_MS_DIEDTHISFRAMEOOB_Q;
        machine->state_2 |= 0x10u;
        machine->energy = 0.0f;
        machine->base_speed = 0.0f;
        machine->velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
        machine->angular_velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
        machine->speed_kmh = 0.0f;
        machine->state_2 &= 0xffffff7fu;
      }
    } else {
      machine->machine_state |= FZGX_MS_FALLOUT;
    }
    out_presence_masks[2] |= corner_mask_bit;
  }
  if (fzgx_collision_scratch_read_u32_exact(world, FZGX_COLLISION_SCRATCH_SLOT3_PRESENT_OFFSET_EXACT) != 0u) {
    out_pushes[3] =
        fzgx_collision_scratch_read_vec3_exact(world, FZGX_COLLISION_SCRATCH_SLOT3_PUSH_OFFSET_EXACT);
    out_summary_flags_ptr_exact[3] = fzgx_track_side_query_summary_slot_flags_ptr_from_opaque_exact(
        shared_scratch,
        fzgx_collision_scratch_read_u32_exact(world, FZGX_COLLISION_SCRATCH_SLOT3_INFO_OFFSET_EXACT));
    out_presence_masks[3] |= corner_mask_bit;
  }
}

static bool fzgx_refresh_machine_wall_contact_queries_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine) {
  const fzgx_track_manifest *track_manifest = fzgx_get_active_track_manifest(world);
  uint32_t center_query_flags = 0x22800u;
  uint32_t wall_query_flags;
  int32_t next_cp_idx;
  int32_t cur_cp_idx;
  int32_t track_id = (track_manifest != 0) ? (int32_t)track_manifest->authored_track_id : -1;
  bool trace_fire_field_kill_window =
      (track_manifest != 0) &&
      (track_manifest->authored_track_id == 17u) &&
      (machine_index == 0u) &&
      (2248u <= world->frame_index) &&
      (world->frame_index <= 2256u);
  int32_t *active_checkpoint_index_ptr =
      (int32_t *)((uint8_t *)&machine->track_state + machine->track_state.active_cp_idx_ptr_offset);
  int32_t active_checkpoint_index = *active_checkpoint_index_ptr;
  fzgx_vec3 hit_normal;
  fzgx_vec3 hit_point;
  fzgx_vec3 push_vectors[4][4];
  uintptr_t summary_flags_ptr_exact[4][4];
  uint32_t presence_masks[4] = {0u, 0u, 0u, 0u};
  fzgx_vec3 local_mins[3];
  fzgx_vec3 local_maxs[3];
  bool center_hit;
  bool severe_contact;
  uint32_t shared_mask;
  size_t i;
  size_t group_index;

  if ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) {
    center_query_flags = 0x23f00u;
  }
  center_hit = fzgx_sweep_machine_center_to_wall_corner_exact(
      world,
      machine_index,
      machine,
      &machine->wall_corners[0].pos_a,
      center_query_flags,
      &hit_normal,
      &hit_point);
  next_cp_idx = machine->track_state.next_cp_idx;
  cur_cp_idx = machine->track_state.cur_cp_idx;
  wall_query_flags = 0x1020005u;
  if ((next_cp_idx < 0) || (next_cp_idx == cur_cp_idx)) {
    wall_query_flags = 0x3020005u;
  }
  switch ((int16_t)track_id) {
    case 8:
      if (((0x21 < active_checkpoint_index) && (active_checkpoint_index < 0x25)) ||
          ((0x2c < active_checkpoint_index) && (active_checkpoint_index < 0x30))) {
        wall_query_flags |= 0x80000000u >> (uint32_t)machine->branch_slot;
      }
      break;
    case 0x10:
      if ((0x61 < active_checkpoint_index) && (active_checkpoint_index < 0x78)) {
        wall_query_flags |= 0x80000000u >> (uint32_t)machine->branch_slot;
      }
      break;
    case 0x18:
      if (machine->branch_slot < 4u) {
        wall_query_flags |= 0x80000000u >> (uint32_t)machine->branch_slot;
      }
      break;
    case 0x1c:
      if (((0x76 < active_checkpoint_index) && (active_checkpoint_index < 0x8f)) ||
          ((0xa9 < active_checkpoint_index) && (active_checkpoint_index < 0xc5))) {
        wall_query_flags |= 0x80000000u >> (uint32_t)machine->branch_slot;
      }
      break;
    default:
      break;
  }
  for (i = 0u; i < 4u; ++i) {
    fzgx_sweep_machine_wall_corner_contacts_exact(
        world,
        machine_index,
        machine,
        &machine->wall_corners[i],
        i,
        wall_query_flags,
        push_vectors[i],
        summary_flags_ptr_exact[i],
        presence_masks,
        0x80000000u >> i);
  }
  for (i = 0u; i < 4u; ++i) {
    fzgx_track_side_query_summary *latest_summary = fzgx_track_side_query_buffer_get_record_exact(
        &machine->corner_scratch[i], machine->corner_scratch[i].latest_record_offset);

    if (latest_summary != 0) {
      fzgx_vec3 latest_hit_normal = center_hit ? hit_normal : (fzgx_vec3){0.0f, 0.0f, 0.0f};

      memcpy(latest_summary->reserved0, &latest_hit_normal, sizeof(latest_hit_normal));
    }
  }

  if (FLT_EPSILON < machine->zero_minus_height_above_track) {
    if (((machine->floor_surface_flags & 0x01800000u) == 0u) &&
        ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u)) {
      machine->suspension_reset_flag = 0u;
    }
  } else {
    machine->suspension_reset_flag = 0u;
  }

  severe_contact = false;
  if ((((int16_t)track_id != 0x1d) && ((int16_t)track_id != 0x27)) || !center_hit) {
    for (i = 0u; i < 4u; ++i) {
      const fzgx_track_side_query_summary *latest_summary =
          fzgx_track_side_query_buffer_get_record_const_exact(
              &machine->corner_scratch[i], machine->corner_scratch[i].latest_record_offset);

      if ((machine->suspension_reset_flag != 0u) ||
          (((latest_summary != 0) ? latest_summary->summary_flags : 0u) & 0xf0000000u) !=
              0xf0000000u) {
        if (((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) &&
            (FLT_EPSILON < machine->zero_minus_height_above_track)) {
          machine->suspension_reset_flag = 1u;
        }
        severe_contact = true;
        break;
      }
    }
  }
  if (trace_fire_field_kill_window ||
      ((track_id >= 0) &&
       fzgx_debug_trace_double_branches_window_exact(
           (uint32_t)track_id, machine_index, world->frame_index))) {
    uint32_t summary0 = 0u;
    uint32_t summary1 = 0u;
    uint32_t summary2 = 0u;
    uint32_t summary3 = 0u;
    const fzgx_track_side_query_summary *summary_ptr;

    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[0], machine->corner_scratch[0].latest_record_offset);
    if (summary_ptr != 0) {
      summary0 = summary_ptr->summary_flags;
    }
    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[1], machine->corner_scratch[1].latest_record_offset);
    if (summary_ptr != 0) {
      summary1 = summary_ptr->summary_flags;
    }
    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[2], machine->corner_scratch[2].latest_record_offset);
    if (summary_ptr != 0) {
      summary2 = summary_ptr->summary_flags;
    }
    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[3], machine->corner_scratch[3].latest_record_offset);
    if (summary_ptr != 0) {
      summary3 = summary_ptr->summary_flags;
    }
    fprintf(
        stderr,
        "walllatch|frame=%u|center_hit=%u|airborne=%u|height=%.6f|floor=0x%08x|reset=%u|"
        "presence=(0x%08x,0x%08x,0x%08x,0x%08x)|summary=(0x%08x,0x%08x,0x%08x,0x%08x)|"
        "corner_state=(0x%02x,0x%02x,0x%02x,0x%02x)|severe=%u\n",
        world->frame_index,
        center_hit ? 1u : 0u,
        ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) ? 1u : 0u,
        machine->zero_minus_height_above_track,
        machine->floor_surface_flags,
        machine->suspension_reset_flag,
        presence_masks[0],
        presence_masks[1],
        presence_masks[2],
        presence_masks[3],
        summary0,
        summary1,
        summary2,
        summary3,
        machine->suspension_corners[0].state,
        machine->suspension_corners[1].state,
        machine->suspension_corners[2].state,
        machine->suspension_corners[3].state,
        severe_contact ? 1u : 0u);
  }

  for (i = 0u; i < 4u; ++i) {
    uint32_t *summary_flags_ptr;

    if (severe_contact) {
      machine->suspension_corners[i].state |= 0x20u;
      push_vectors[i][0] = (fzgx_vec3){0.0f, 0.0f, 0.0f};
      summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[i][0];
      presence_masks[0] = 0u;
      push_vectors[i][1] = push_vectors[i][3];
      presence_masks[1] = presence_masks[3];
      if (summary_flags_ptr != 0) {
        *summary_flags_ptr &= 0x0fffffffu;
      }
      summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[i][1];
      if (summary_flags_ptr != 0) {
        *summary_flags_ptr &= 0x0fffffffu;
      }
    } else {
      machine->suspension_corners[i].state &= ~0x20u;
      summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[i][3];
      presence_masks[3] = 0u;
      if (summary_flags_ptr != 0) {
        *summary_flags_ptr &= 0xfffffffbu;
      }
    }
    machine->suspension_state[i] = (uint8_t)machine->suspension_corners[i].state;
  }

  if (center_hit && (presence_masks[0] != 0u)) {
    for (i = 0u; i < 4u; ++i) {
      if (((presence_masks[0] & (0x80000000u >> i)) != 0u) &&
          (fzgx_vec3_dot(hit_normal, push_vectors[i][0]) < 0.0f)) {
        uint32_t *summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[i][0];

        push_vectors[i][0] = (fzgx_vec3){0.0f, 0.0f, 0.0f};
        presence_masks[0] &= ~(0x80000000u >> i);
        if (summary_flags_ptr != 0) {
          *summary_flags_ptr &= 0x0ffffffbu;
        }
      }
    }
  }

  shared_mask = 0u;
  if ((int32_t)presence_masks[0] < 0) {
    uint32_t *summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[0][0];

    if (summary_flags_ptr != 0) {
      shared_mask = *summary_flags_ptr;
    }
  }
  if (((presence_masks[0] >> 30) & 1u) != 0u) {
    uint32_t *summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[1][0];

    if (summary_flags_ptr != 0) {
      shared_mask |= *summary_flags_ptr;
    }
  }
  if (((presence_masks[0] >> 29) & 1u) != 0u) {
    uint32_t *summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[2][0];

    if (summary_flags_ptr != 0) {
      shared_mask |= *summary_flags_ptr;
    }
  }
  if (((presence_masks[0] >> 28) & 1u) != 0u) {
    uint32_t *summary_flags_ptr = (uint32_t *)(uintptr_t)summary_flags_ptr_exact[3][0];

    if (summary_flags_ptr != 0) {
      shared_mask |= *summary_flags_ptr;
    }
  }
  shared_mask &= 0x0000c000u;
  if (shared_mask == 0x0000c000u) {
    shared_mask = 0u;
  }
  for (i = 0u; i < 4u; ++i) {
    fzgx_track_side_query_summary *latest_summary = fzgx_track_side_query_buffer_get_record_exact(
        &machine->corner_scratch[i], machine->corner_scratch[i].latest_record_offset);

    if (latest_summary != 0) {
      latest_summary->shared_mask = shared_mask;
    }
  }

  for (i = 0u; i < 4u; ++i) {
    machine->wall_corners[i].collision = fzgx_vec3_add(push_vectors[i][0], push_vectors[i][1]);
  }

  machine->collision_push_min = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  machine->collision_push_max = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  for (i = 0u; i < 3u; ++i) {
    local_mins[i] = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    local_maxs[i] = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  }

  if (center_hit || (presence_masks[0] != 0u) || (presence_masks[1] != 0u) || (presence_masks[2] != 0u)) {
    for (group_index = 0u; group_index < 3u; ++group_index) {
      if ((group_index == 0u) && center_hit) {
        fzgx_vec3 local_vec = fzgx_world_vector_to_local(&machine->basis_physical, hit_normal);

        if (local_vec.x <= local_maxs[group_index].x) {
          if (local_vec.x < local_mins[group_index].x) {
            local_mins[group_index].x = local_vec.x;
          }
        } else {
          local_maxs[group_index].x = local_vec.x;
        }
        if (local_vec.y <= local_maxs[group_index].y) {
          if (local_vec.y < local_mins[group_index].y) {
            local_mins[group_index].y = local_vec.y;
          }
        } else {
          local_maxs[group_index].y = local_vec.y;
        }
        if (local_vec.z <= local_maxs[group_index].z) {
          if (local_vec.z < local_mins[group_index].z) {
            local_mins[group_index].z = local_vec.z;
          }
        } else {
          local_maxs[group_index].z = local_vec.z;
        }
      }
      for (i = 0u; i < 4u; ++i) {
        if ((presence_masks[group_index] & (0x80000000u >> i)) != 0u) {
          fzgx_vec3 local_vec = fzgx_world_vector_to_local(
              &machine->basis_physical, push_vectors[i][group_index]);

          if (local_vec.x <= local_maxs[group_index].x) {
            if (local_vec.x < local_mins[group_index].x) {
              local_mins[group_index].x = local_vec.x;
            }
          } else {
            local_maxs[group_index].x = local_vec.x;
          }
          if (local_vec.y <= local_maxs[group_index].y) {
            if (local_vec.y < local_mins[group_index].y) {
              local_mins[group_index].y = local_vec.y;
            }
          } else {
            local_maxs[group_index].y = local_vec.y;
          }
          if (local_vec.z <= local_maxs[group_index].z) {
            if (local_vec.z < local_mins[group_index].z) {
              local_mins[group_index].z = local_vec.z;
            }
          } else {
            local_maxs[group_index].z = local_vec.z;
          }
        }
      }
    }
    machine->collision_push_min =
        fzgx_transform_local_vector(&machine->basis_physical, fzgx_vec3_add(
                                                                fzgx_vec3_add(local_mins[0], local_mins[1]),
                                                                local_mins[2]));
    machine->collision_push_max =
        fzgx_transform_local_vector(&machine->basis_physical, fzgx_vec3_add(
                                                                fzgx_vec3_add(local_maxs[0], local_maxs[1]),
                                                                local_maxs[2]));
  }

  machine->collision_push_total = fzgx_vec3_add(machine->collision_push_min, machine->collision_push_max);
  if (center_hit) {
    fzgx_vec3 adjusted_hit = fzgx_vec3_sub(hit_point, machine->collision_push_total);
    size_t nearest_corner = 0u;
    float nearest_dist_sq = 0.0f;

    for (i = 0u; i < 4u; ++i) {
      fzgx_vec3 diff = fzgx_vec3_sub(adjusted_hit, machine->wall_corners[i].pos_b);
      float dist_sq = fzgx_vec3_dot(diff, diff);

      if ((i == 0u) || (dist_sq < nearest_dist_sq)) {
        nearest_corner = i;
        nearest_dist_sq = dist_sq;
      }
    }
    if ((machine->wall_corners[nearest_corner].collision.x == 0.0f) &&
        (machine->wall_corners[nearest_corner].collision.y == 0.0f) &&
        (machine->wall_corners[nearest_corner].collision.z == 0.0f)) {
      machine->wall_corners[nearest_corner].collision = hit_normal;
    }
  }

  if (presence_masks[3] != 0u) {
    machine->terrain_flags |= FZGX_TERRAIN_LAVA;
    machine->state_2 |= 0x200u;
  }

  if (trace_fire_field_kill_window ||
      ((track_manifest != 0) &&
       fzgx_debug_trace_double_branches_window_exact(
           track_manifest->authored_track_id, machine_index, world->frame_index))) {
    uint32_t summary0 = 0u;
    uint32_t summary1 = 0u;
    uint32_t summary2 = 0u;
    uint32_t summary3 = 0u;
    const fzgx_track_side_query_summary *summary_ptr;

    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[0], machine->corner_scratch[0].latest_record_offset);
    if (summary_ptr != 0) {
      summary0 = summary_ptr->summary_flags;
    }
    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[1], machine->corner_scratch[1].latest_record_offset);
    if (summary_ptr != 0) {
      summary1 = summary_ptr->summary_flags;
    }
    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[2], machine->corner_scratch[2].latest_record_offset);
    if (summary_ptr != 0) {
      summary2 = summary_ptr->summary_flags;
    }
    summary_ptr = fzgx_track_side_query_buffer_get_record_const_exact(
        &machine->corner_scratch[3], machine->corner_scratch[3].latest_record_offset);
    if (summary_ptr != 0) {
      summary3 = summary_ptr->summary_flags;
    }
    fprintf(
        stderr,
        "wallagg|frame=%u|center_hit=%u|presence=(0x%08x,0x%08x,0x%08x,0x%08x)|"
        "summary=(0x%08x,0x%08x,0x%08x,0x%08x)|"
        "push_min=(%.3f,%.3f,%.3f)|push_max=(%.3f,%.3f,%.3f)|push_total=(%.3f,%.3f,%.3f)\n",
        world->frame_index,
        center_hit ? 1u : 0u,
        presence_masks[0],
        presence_masks[1],
        presence_masks[2],
        presence_masks[3],
        summary0,
        summary1,
        summary2,
        summary3,
        machine->collision_push_min.x,
        machine->collision_push_min.y,
        machine->collision_push_min.z,
        machine->collision_push_max.x,
        machine->collision_push_max.y,
        machine->collision_push_max.z,
        machine->collision_push_total.x,
        machine->collision_push_total.y,
        machine->collision_push_total.z);
    for (i = 0u; i < 4u; ++i) {
      fprintf(
          stderr,
          "wallagg_corner|frame=%u|corner=%zu|collision=(%.3f,%.3f,%.3f)|"
          "pos_a=(%.3f,%.3f,%.3f)|pos_b=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          i,
          machine->wall_corners[i].collision.x,
          machine->wall_corners[i].collision.y,
          machine->wall_corners[i].collision.z,
          machine->wall_corners[i].pos_a.x,
          machine->wall_corners[i].pos_a.y,
          machine->wall_corners[i].pos_a.z,
          machine->wall_corners[i].pos_b.x,
          machine->wall_corners[i].pos_b.y,
          machine->wall_corners[i].pos_b.z);
    }
  }

  severe_contact = false;
  if (center_hit ||
      ((presence_masks[0] != 0u) &&
       ((local_mins[0].x != 0.0f) || (local_mins[0].y != 0.0f) || (local_mins[0].z != 0.0f) ||
        (local_maxs[0].x != 0.0f) || (local_maxs[0].y != 0.0f) || (local_maxs[0].z != 0.0f)))) {
    severe_contact = true;
  }
  if (severe_contact) {
    severe_contact = false;
    if ((presence_masks[1] != 0u) &&
        ((local_mins[1].x != 0.0f) || (local_mins[1].y != 0.0f) || (local_mins[1].z != 0.0f) ||
         (local_maxs[1].x != 0.0f) || (local_maxs[1].y != 0.0f) || (local_maxs[1].z != 0.0f))) {
      float secondary_mag_sq =
          (local_mins[1].x + local_maxs[1].x) * (local_mins[1].x + local_maxs[1].x) +
          (local_mins[1].y + local_maxs[1].y) * (local_mins[1].y + local_maxs[1].y) +
          (local_mins[1].z + local_maxs[1].z) * (local_mins[1].z + local_maxs[1].z);
      float primary_mag_sq =
          (local_mins[0].x + local_maxs[0].x) * (local_mins[0].x + local_maxs[0].x) +
          (local_mins[0].y + local_maxs[0].y) * (local_mins[0].y + local_maxs[0].y) +
          (local_mins[0].z + local_maxs[0].z) * (local_mins[0].z + local_maxs[0].z);

      severe_contact = true;
      if ((secondary_mag_sq < primary_mag_sq) ||
          (center_hit && (secondary_mag_sq < fzgx_vec3_dot(hit_normal, hit_normal)))) {
        center_hit = true;
      } else {
        center_hit = false;
      }
    } else {
      center_hit = true;
    }
  } else {
    center_hit = false;
  }
  return center_hit;
}

static void fzgx_apply_retired_velocity_damp_exact(fzgx_machine_snapshot *machine) {
  if (((machine->machine_state & FZGX_MS_RETIRED) != 0u) &&
      ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u)) {
    if (10.0f <= machine->speed_kmh) {
      machine->velocity.x *= 0.9f;
      machine->velocity.y *= 0.9f;
      machine->velocity.z *= 0.9f;
    } else {
      machine->velocity = (fzgx_vec3){0};
    }
  }
}

static void fzgx_prepare_machine_frame_post_attack_exact(fzgx_machine_snapshot *machine) {
  if (machine->car_hit_invincibility == 0u) {
    if ((machine->machine_state & FZGX_MS_JUSTHITVEHICLE_Q) != 0u) {
      machine->car_hit_invincibility = 6u;
    }
  } else {
    machine->car_hit_invincibility -= 1u;
  }
  if (machine->breakdown_frame_counter != 0u) {
    machine->breakdown_frame_counter -= 1u;
  }
}

static void fzgx_prepare_machine_frame_tail_exact(fzgx_machine_snapshot *machine) {
  machine->position_old_2 = machine->position;
  machine->frames_since_start += 1u;
  if (machine->unk_byte_0x4c3 != 0u) {
    uint8_t low_nibble = (machine->unk_byte_0x4c3 & 0x0fu);
    if (low_nibble <= 1u) {
      machine->unk_byte_0x4c3 = 0u;
    } else {
      machine->unk_byte_0x4c3 =
          (uint8_t)((machine->unk_byte_0x4c3 & 0xf0u) | ((low_nibble - 1u) & 0x0fu));
    }
  }
  machine->approach_dir = (fzgx_vec3){10000.0f, 0.0f, 0.0f};
  if (machine->machine_approach_frame_counter != 0u) {
    machine->machine_approach_frame_counter -= 1u;
  }
  if (machine->time_since_ko_frame_counter != 0u) {
    if (machine->time_since_ko_frame_counter < 120u) {
      machine->time_since_ko_frame_counter += 1u;
    } else {
      machine->time_since_ko_frame_counter = 0u;
    }
  }
}

static void fzgx_refresh_machine_local_velocity_views_exact(fzgx_machine_snapshot *machine) {
  float turn_degrees =
      -(machine->input_steer_yaw * machine->stat_turn_reaction +
        machine->input_strafe * machine->stat_strafe);
  fzgx_mat43 rotated_basis = machine->basis_physical;

  machine->velocity_local = fzgx_world_vector_to_local(&machine->basis_physical, machine->velocity);
  if (turn_degrees < -45.0f) {
    turn_degrees = -45.0f;
  } else if (45.0f < turn_degrees) {
    turn_degrees = 45.0f;
  }
  fzgx_mat43_rotate_about_y_right(&rotated_basis, (uint16_t)(int)(182.04445f * turn_degrees));
  machine->velocity_local_flattened_and_rotated =
      fzgx_world_vector_to_local(&rotated_basis, machine->velocity);
  machine->velocity_local_flattened_and_rotated.y = 0.0f;
}

static void fzgx_handle_attack_states_exact(fzgx_machine_snapshot *machine) {
  if (machine->speed_kmh < 300.0f) {
    if (machine->spinattack_angle == 0u) {
      machine->machine_state &= ~FZGX_MS_SPINATTACKING;
    }
    machine->machine_state &= ~FZGX_MS_SIDEATTACKING;
  }
  if (machine->side_attack_delay != 0u) {
    machine->machine_state &= ~FZGX_MS_SPINATTACKING;
  }
  if ((machine->machine_state & FZGX_MS_SPINATTACKING) == 0u) {
    machine->spinattack_angle = 0u;
  } else {
    uint32_t angle = machine->spinattack_angle;
    if (angle == 0u) {
      machine->spinattack_angle = 262144u;
      machine->spinattack_angle_decrement = 4096u;
      if (machine->input_steer_yaw <= 0.0f) {
        machine->spinattack_direction = 1u;
      } else {
        machine->spinattack_direction = 0u;
      }
    } else if ((uint32_t)machine->spinattack_angle_decrement < angle) {
      machine->spinattack_angle = angle - (uint32_t)machine->spinattack_angle_decrement;
      if (machine->spinattack_angle < 131072u) {
        machine->spinattack_angle_decrement =
            (uint16_t)(machine->spinattack_angle_decrement - 65u);
        if (machine->spinattack_angle_decrement < 80u) {
          machine->spinattack_angle_decrement = 80u;
        }
      }
    } else {
      machine->spinattack_angle = 0u;
      machine->spinattack_angle_decrement = 0u;
      machine->machine_state &= ~FZGX_MS_SPINATTACKING;
    }
    machine->machine_state &= ~FZGX_MS_SIDEATTACKING;
  }
  if ((machine->machine_state & FZGX_MS_SIDEATTACKING) == 0u) {
    machine->side_attack_delay = 0u;
  } else {
    uint8_t delay = machine->side_attack_delay;
    if (delay == 0u) {
      machine->side_attack_delay = 6u;
    } else if (delay == 1u) {
      machine->machine_state &= ~FZGX_MS_SIDEATTACKING;
    } else {
      machine->side_attack_delay = delay - 1u;
    }
    if (((machine->machine_state & (FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_TOOKDAMAGE)) != 0u) ||
        (machine->input_accel < 0.5f)) {
      machine->machine_state &= ~FZGX_MS_SIDEATTACKING;
      machine->side_attack_delay = 1u;
    }
  }
  if (machine->machine_collision_frame_counter != 0u) {
    machine->machine_collision_frame_counter -= 1u;
  }
  if ((machine->machine_state & (FZGX_MS_SIDEATTACKING | FZGX_MS_SPINATTACKING)) == 0u) {
    machine->spin_attack_kill_indicator = false;
  }
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static void fzgx_handle_suspension_states_exact(fzgx_machine_snapshot *machine) {
  size_t i;
  uint32_t machine_state = machine->machine_state;

  if (machine->grip_frames_from_accel_press != 0u) {
    machine->grip_frames_from_accel_press -= 1u;
  }
  if ((machine_state & FZGX_MS_AIRBORNE) == 0u) {
    if (0.1f < machine->base_speed) {
      if ((machine_state & FZGX_MS_B14) == 0u) {
        bool drift_active = false;
        if ((machine_state & FZGX_MS_B13) != 0u) {
          if (0.1 < fabs((double)machine->input_steer_yaw)) {
            drift_active = true;
          }
        }
        if ((machine->machine_state & FZGX_MS_SPINATTACKING) != 0u) {
          drift_active = true;
        }
        if (drift_active) {
          for (i = 0u; i < sizeof(machine->suspension_state) / sizeof(machine->suspension_state[0]);
               ++i) {
            machine->suspension_corners[i].state |= FZGX_TC_DRIFT;
            machine->suspension_state[i] = (uint8_t)machine->suspension_corners[i].state;
          }
        }
      } else {
        for (i = 0u; i < sizeof(machine->suspension_state) / sizeof(machine->suspension_state[0]);
             ++i) {
          machine->suspension_corners[i].state &= ~FZGX_TC_DRIFT;
          machine->suspension_state[i] = (uint8_t)machine->suspension_corners[i].state;
        }
        machine->grip_frames_from_accel_press = machine->stat_grip_frames_from_accel_press;
      }
    }
  } else {
    for (i = 0u; i < sizeof(machine->suspension_state) / sizeof(machine->suspension_state[0]);
         ++i) {
      machine->suspension_corners[i].state &= ~FZGX_TC_DRIFT;
      machine->suspension_state[i] = (uint8_t)machine->suspension_corners[i].state;
    }
  }
  if (((machine->machine_state & FZGX_MS_STRAFING) != 0u) &&
      (fabs((double)machine->input_steer_yaw) < 0.1)) {
    machine->machine_state &= ~FZGX_MS_STRAFING;
  }
  if (0.3 < fabs((double)machine->input_strafe)) {
    machine->machine_state |= FZGX_MS_STRAFING;
  }
  if ((machine->machine_state & FZGX_MS_STRAFING) == 0u) {
    return;
  }
  for (i = 0u; i < sizeof(machine->suspension_state) / sizeof(machine->suspension_state[0]); ++i) {
    machine->suspension_corners[i].state |= FZGX_TC_STRAFING;
    machine->suspension_state[i] = (uint8_t)machine->suspension_corners[i].state;
  }
}

static void fzgx_apply_machine_angular_impulse_from_world_force_exact(
    fzgx_machine_snapshot *machine,
    const fzgx_vec3 *local_position,
    const fzgx_vec3 *world_force) {
  float fx = local_position->x;
  float fy = local_position->y;
  float fz = local_position->z;
  fzgx_vec3 local_force = fzgx_world_vector_to_local(&machine->basis_physical, *world_force);

  machine->angular_velocity.x =
      machine->angular_velocity.x + -(fz * local_force.y - fy * local_force.z);
  machine->angular_velocity.y =
      machine->angular_velocity.y + -(fx * local_force.z - fz * local_force.x);
  machine->angular_velocity.z =
      machine->angular_velocity.z + -(fy * local_force.x - fx * local_force.y);
}

static void fzgx_handle_machine_turn_and_strafe_exact(
    double angle_velocity_y,
    fzgx_machine_snapshot *machine,
    fzgx_machine_tilt_corner_snapshot *corner) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  bool bVar5;
  uint32_t uVar6;
  float fVar7;
  uint32_t uVar8;
  double dVar9;
  double dVar10;
  double dVar11;
  float fVar12;
  fzgx_vec3 local_68;
  fzgx_vec3 local_5c;
  fzgx_vec3 fStack_50;
  fzgx_vec3 corner_delta;
  fzgx_mat43 basis_physical;
  fzgx_mat43 rotated_basis;

  corner_delta.x = corner->pos_old.x - corner->pos.x;
  corner_delta.y = corner->pos_old.y - corner->pos.y;
  corner_delta.z = corner->pos_old.z - corner->pos.z;
  uVar6 = corner->state & 4u;
  basis_physical = machine->basis_physical;
  rotated_basis = basis_physical;
  fVar3 = -(machine->input_steer_yaw * machine->stat_turn_reaction +
            machine->input_strafe * machine->stat_strafe);
  fVar12 = -45.0f;
  if ((-45.0f <= fVar3) && (fVar12 = fVar3, 45.0f < fVar3)) {
    fVar12 = 45.0f;
  }
  {
    int16_t turn_angle = (int16_t)(int32_t)(182.04445f * fVar12);
    fzgx_mat43_rotate_about_y_right(&rotated_basis, (uint16_t)(turn_angle / 2));
  }
  corner_delta = fzgx_world_vector_to_local(&rotated_basis, corner_delta);
  fVar12 = (float)(((double)fzgx_vec3_length(corner_delta) * 216.0) / 1000.0);
  fVar7 = 1.0f;
  fVar3 = 0.0f;
  bVar5 = false;
  local_5c.y = 0.0f;
  dVar11 = (double)corner_delta.x;
  local_5c.x = 1.0f;
  local_5c.z = 0.0f;
  if ((uVar6 == 0u) && ((corner->state & FZGX_TC_STRAFING) != 0u)) {
    bVar5 = true;
  }
  if (bVar5 || (machine->grip_frames_from_accel_press != 0u)) {
    dVar10 = (double)20.0f;
  } else {
    dVar9 = (double)machine->stat_grip_1;
    dVar10 = dVar9;
    if ((machine->state_2 & 4u) == 0u) {
      if ((uVar6 != 0u) && (machine->brake_timer == 0u)) {
        dVar10 = (double)machine->stat_grip_3;
      }
    } else if (((uVar6 != 0u) && (machine->brake_timer < 30u)) &&
               (dVar10 = (double)machine->stat_grip_3,
                dVar9 < (double)machine->stat_grip_3)) {
      dVar10 = dVar9;
    }
  }
  if (fabs(dVar11) < (double)machine->stat_grip_3) {
    corner->state = corner->state & 0xffffffefu;
  }
  bVar5 = true;
  if ((uVar6 == 0u) && (fabs((double)machine->input_steer_yaw) <= 0.7)) {
    bVar5 = false;
  }
  if ((fabs(dVar11) <= dVar10) || (!bVar5)) {
    dVar10 = dVar11;
    if (fabs(dVar11) < DBL_EPSILON) {
      dVar10 = (double)0.0f;
    }
    corner->state = corner->state & 0xfffffffbu;
  } else {
    dVar9 = (double)0.0f;
    corner->state = corner->state | FZGX_TC_DRIFT;
    if (dVar11 < dVar9) {
      dVar10 = -dVar10;
    }
  }
  uVar8 = machine->machine_state;
  if ((uVar8 &
       (FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_LOWGRIP | FZGX_MS_TOOKDAMAGE |
        FZGX_MS_SIDEATTACKING)) != 0u) {
    dVar10 = (double)0.0f;
  }
  if ((uVar8 & FZGX_MS_RETIRED) == 0u) {
    if ((uVar8 & FZGX_MS_0HP) != 0u) {
      dVar11 = (double)(0.01f * ((float)machine->frames_since_death - 4.0f));
      dVar9 = (double)0.0f;
      if ((dVar9 <= dVar11) && (dVar9 = dVar11, (double)0.05f < dVar11)) {
        dVar9 = (double)0.05f;
      }
      dVar10 = (double)(float)(dVar10 * dVar9);
    }
  } else {
    dVar10 = (double)(float)(dVar10 * (double)0.2f);
  }
  if ((double)0.0f != dVar10) {
    fVar2 = machine->stat_turn_tension;
    fVar1 = (float)(dVar10 * (double)machine->stat_weight);
    if ((0.1f <= fVar2) || (machine->grip_frames_from_accel_press != 0u)) {
      dVar11 = (double)(fVar1 * fVar2);
    } else if (((corner->state & 2u) == 0u) &&
               ((uVar8 & FZGX_MS_JUSTPRESSEDBOOST) == 0u)) {
      fVar4 = 0.2f;
      if ((0.2f <= fVar12) && (fVar4 = fVar12, 0.8f < fVar12)) {
        fVar4 = 0.8f;
      }
      fVar12 = 0.0f;
      if ((corner->state & 4u) == 0u) {
        fVar12 =
            (float)((double)(((fVar4 - 0.2f) / 0.6f) * (fVar2 - 0.1f)) *
                    (0.3 + 0.7 * fabs((double)machine->input_steer_yaw)));
      }
      dVar11 = (double)(fVar1 *
                         (0.1f + fVar12 * (1.0f - (float)machine->rail_collision_timer / 20.0f)));
    } else {
      dVar11 = (double)(fVar1 * 0.1f);
    }
    if ((int32_t)machine->terrain_flags < 0) {
      dVar11 = (double)(float)(dVar11 * (double)0.003f);
    } else if (((machine->terrain_flags >> 29) & 1u) != 0u) {
      dVar11 = (double)(float)(dVar11 * (double)2.0f);
    }
    local_5c.z = (float)((double)fVar3 * dVar11);
    local_5c.x = (float)((double)fVar7 * dVar11);
    local_5c.y = (float)((double)fVar3 * dVar11);
    fStack_50 = fzgx_transform_local_vector(&rotated_basis, local_5c);
    corner->force_spatial.x = fStack_50.x + corner->force_spatial.x;
    corner->force_spatial.y = fStack_50.y + corner->force_spatial.y;
    corner->force_spatial.z = fStack_50.z + corner->force_spatial.z;
    if ((corner->state & 4u) != 0u) {
      dVar11 = (double)(float)(dVar11 * (double)0.6f);
    }
    machine->turning_related = (float)((double)machine->turning_related + dVar11);
  }
  local_68.x = corner->force_spatial.x;
  local_68.y = corner->force_spatial.y;
  local_68.z = corner->force_spatial.z;
  machine->velocity.x = local_68.x + machine->velocity.x;
  machine->velocity.y = local_68.y + machine->velocity.y;
  machine->velocity.z = local_68.z + machine->velocity.z;
  if (machine->rail_collision_timer < 6u) {
    fzgx_apply_machine_angular_impulse_from_world_force_exact(
        machine, &corner->offset, &corner->force_spatial);
  }
  local_68 = fzgx_world_vector_to_local(&basis_physical, local_68);
  if ((uVar6 != 0u) && ((machine->machine_state & FZGX_MS_JUSTHITVEHICLE_Q) == 0u)) {
    angle_velocity_y = (double)(float)(angle_velocity_y * (double)machine->stat_grip_2);
  }
  machine->angular_velocity.y =
      machine->angular_velocity.y - (float)((double)0.125f * angle_velocity_y);
}

static float fzgx_update_machine_base_speed_and_boost_exact(
    float neg_local_fwd_speed,
    float abs_local_lateral_speed,
    float drift_accel_factor,
    fzgx_machine_snapshot *machine) {
  float brake_scale = 0.0001f;
  float zero = 0.0f;
  uint32_t machine_state = machine->machine_state;
  float accel_input;
  float local_fwd_speed_ratio;
  float base_speed_target;
  float base_speed_delta;
  float decel_scale;
  float brake_value;
  float forward_force_scale;

  if (((machine_state & FZGX_MS_0HP) != 0u) && (machine->frames_since_death <= 0x77u)) {
    if (machine->brake_timer < 0x3du) {
      machine->brake_timer += 1u;
    } else {
      machine->input_accel = 0.0f;
      machine->input_brake = brake_scale;
    }
    fzgx_apply_machine_flags_from_exact_state(machine);
    return 0.0f;
  }

  accel_input = machine->input_accel;
  if ((machine->state_2 & 4u) == 0u) {
    if ((accel_input < 0.0f) || (0.0f < machine->input_brake)) {
      accel_input = 0.0f;
    }
  } else if ((accel_input < 0.0f) || (machine->brake_timer > 0x1du)) {
    accel_input = 0.0f;
  }
  if (((machine_state & FZGX_MS_ACTIVE) == 0u) && (accel_input < 0.3f)) {
    accel_input = 0.0f;
  }

  if (accel_input <= FLT_EPSILON) {
    if (machine->race_start_charge <= 0.0f) {
      if ((machine_state & FZGX_MS_STARTINGCOUNTDOWN) != 0u) {
        machine->base_speed = 0.0f;
      }
    } else {
      machine->race_start_charge -= 2.0f;
      if (machine->race_start_charge < 0.0f) {
        machine->race_start_charge = 0.0f;
      }
      if ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) == 0u) {
        machine->base_speed = 0.0f;
      }
    }
  } else {
    if ((machine_state & FZGX_MS_ACTIVE) == 0u) {
      machine->machine_state |= FZGX_MS_ACTIVE;
      machine->frames_since_start_2 = 1u;
    }
    if ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) == 0u) {
      if (machine->race_start_charge > 0.0f) {
        machine->base_speed = 1.0f;
        machine->machine_state |= FZGX_MS_RACEJUSTBEGAN_Q | FZGX_MS_STARTINGCOUNTDOWN;
        machine->race_start_charge = 0.0f;
      }
    } else {
      machine->race_start_charge += accel_input;
    }
  }

  machine_state = machine->machine_state;
  if ((machine_state & FZGX_MS_STARTINGCOUNTDOWN) == 0u) {
    if (machine->control_profile_kind != 0xffu) {
      if (machine->boost_delay_frame_counter != 0u) {
        machine->machine_state &= ~FZGX_MS_JUSTPRESSEDBOOST;
        machine->boost_delay_frame_counter -= 1u;
      }
      if ((machine_state & FZGX_MS_JUSTPRESSEDBOOST) != 0u) {
        if (machine->boost_delay_frame_counter == 0u) {
          machine->boost_delay_frame_counter = 6u;
        } else {
          machine->boost_delay_frame_counter += 1u;
        }
      }
    }
    if ((machine->machine_state & FZGX_MS_B23) == 0u) {
      if (machine->boost_frames == 0u) {
        if (((machine->machine_state & FZGX_MS_JUSTPRESSEDBOOST) == 0u) ||
            (machine->energy <= 1.0f) || (accel_input <= 0.0f)) {
          machine->machine_state &=
              ~(FZGX_MS_BOOSTING_DASHPLATE | FZGX_MS_JUSTPRESSEDBOOST | FZGX_MS_BOOSTING);
          machine->boost_turbo -= (4.0f + 0.5f * machine->boost_turbo) / 60.0f;
        } else {
          float boost_ratio =
              1.0f - machine->boost_turbo / (9.0f * machine->stat_boost_strength);
          uint8_t boost_frames = (uint8_t)(60.0f * machine->stat_boost_length);
          if (boost_ratio < 0.2f) {
            boost_ratio = 0.2f;
          }
          machine->boost_frames = boost_frames;
          machine->boost_frames_manual = boost_frames;
          machine->machine_state |= FZGX_MS_BOOSTING;
          machine->machine_state &= ~FZGX_MS_BOOSTING_DASHPLATE;
          if (machine->boost_count < 100u) {
            machine->boost_count += 1u;
          }
          machine->boost_turbo += machine->stat_boost_strength * boost_ratio;
        }
      } else {
        machine->machine_state &= ~FZGX_MS_JUSTPRESSEDBOOST;
        machine->machine_state |= FZGX_MS_BOOSTING;
      }
    } else {
      float boost_ratio =
          1.0f - machine->boost_turbo / (9.0f * machine->stat_boost_strength);
      uint8_t dash_frames = (uint8_t)(0.5f * 60.0f * machine->stat_boost_length);
      if ((uint32_t)machine->boost_frames < (uint32_t)dash_frames) {
        machine->boost_frames = dash_frames;
      }
      machine->machine_state &= ~FZGX_MS_JUSTPRESSEDBOOST;
      machine->machine_state |= FZGX_MS_BOOSTING;
      if (boost_ratio < 0.2f) {
        boost_ratio = 0.2f;
      }
      machine->boost_turbo += (2.0f * machine->stat_boost_strength) * boost_ratio;
      if (machine->dash_plate_hit_count < 100u) {
        machine->dash_plate_hit_count += 1u;
      }
    }
    if ((machine->machine_state & FZGX_MS_SPINATTACKING) == 0u) {
      machine->boost_turbo -= (2.0f + 0.5f * machine->boost_turbo) / 60.0f;
    } else {
      accel_input *= 0.8f;
      machine->boost_turbo -= (3.0f + 0.5f * machine->boost_turbo) / 60.0f;
    }
    if (machine->boost_turbo < 0.0f) {
      machine->boost_turbo = 0.0f;
    }
    if ((machine->machine_state & FZGX_MS_BOOSTING) != 0u) {
      if (machine->boost_frames_manual != 0u) {
        machine->energy -= 0.16666f * machine->boost_energy_use_mult;
        machine->boost_frames_manual -= 1u;
      }
      machine->boost_frames -= 1u;
      if ((machine->boost_frames == 0u) && (1200.0f < machine->speed_kmh)) {
        float speed_delay = (machine->speed_kmh - 1200.0f) / 60.0f;
        if (10.0f < speed_delay) {
          speed_delay = 10.0f;
        }
        if ((float)machine->boost_delay_frame_counter < speed_delay) {
          machine->boost_delay_frame_counter = (uint8_t)speed_delay;
        }
      }
      if (machine->energy < 0.01f) {
        uint8_t dash_frames = (uint8_t)(0.5f * 60.0f * machine->stat_boost_length);
        machine->energy = 0.01f;
        machine->boost_frames_manual = 0u;
        if ((machine->machine_state & FZGX_MS_BOOSTING_DASHPLATE) == 0u) {
          machine->boost_frames = 0u;
        } else if ((uint32_t)dash_frames < (uint32_t)machine->boost_frames) {
          machine->boost_frames = dash_frames;
        }
      }
    }

    base_speed_target = (accel_input * (40.0f * machine->stat_acceleration)) / 348.0f +
                        machine->base_speed;
    base_speed_delta = base_speed_target - (neg_local_fwd_speed / machine->stat_weight);
    local_fwd_speed_ratio =
        base_speed_target / (36.0f + 40.0f * machine->stat_max_speed + machine->boost_turbo * 2.0f);
    if (local_fwd_speed_ratio < 0.0f) {
      local_fwd_speed_ratio = 0.0f;
    }
    decel_scale = local_fwd_speed_ratio * 4.0f *
                  (machine->stat_acceleration * (0.6f + machine->stat_acceleration));
    if (((machine->machine_state & (FZGX_MS_B23 | FZGX_MS_JUSTPRESSEDBOOST)) == 0u) &&
        ((machine->machine_state & FZGX_MS_BOOSTING) != 0u)) {
      if (machine->stat_weight <= 1000.0f) {
        decel_scale *= 0.3f;
      } else {
        decel_scale *= 0.5f;
      }
    } else if ((machine->machine_state & (FZGX_MS_B23 | FZGX_MS_JUSTPRESSEDBOOST)) != 0u) {
      decel_scale = 0.0f;
    }
    if ((0.0f < base_speed_delta) &&
        (((neg_local_fwd_speed / machine->stat_weight) < 0.0f) ||
         ((machine->terrain_flags >> 29) & 1u) != 0u)) {
      decel_scale *= 5.0f;
    }
    {
      float base_speed_decel =
          (1.0f - drift_accel_factor) *
          (base_speed_delta * decel_scale +
           ((abs_local_lateral_speed * machine->stat_acceleration) / machine->stat_weight) *
               machine->stat_turn_decel);

      if (machine->input_accel < 1.0f) {
        base_speed_decel *= 0.05f + 0.95f * machine->input_accel;
      }
      machine->base_speed = base_speed_target - base_speed_decel;
    }
    if (machine->input_brake <= brake_scale) {
      machine->brake_timer = 0u;
    } else if (machine->brake_timer < 0x1eu) {
      machine->brake_timer += 1u;
    }
    if ((machine->state_2 & 4u) == 0u) {
      brake_value = machine->input_brake * (0.5f * decel_scale);
    } else {
      brake_value = 0.0f;
      if (machine->brake_timer > 0x0eu) {
        brake_value = machine->input_brake * (0.5f * decel_scale);
      }
    }
    if (brake_value > 0.12f) {
      brake_value = 0.12f;
    }
    if (brake_value <= machine->base_speed) {
      machine->base_speed -= brake_value;
    } else {
      machine->base_speed = 0.0f;
    }
    if (machine->base_speed <= machine->stat_drag) {
      machine->base_speed = 0.0f;
    } else {
      machine->base_speed -= machine->stat_drag;
    }
    if (brake_value <= 0.0f) {
      float grip_scale = 1.0f;
      if ((machine->machine_state & FZGX_MS_B14) == 0u) {
        grip_scale = 0.3f;
      }
      if ((neg_local_fwd_speed / machine->stat_weight) < 0.0f) {
        base_speed_delta *= 0.5f * grip_scale;
      } else if (base_speed_delta < 0.0f) {
        base_speed_delta *= 0.5f * grip_scale;
      }
    }
    if (machine->unk_byte_0x241 != 0u) {
      float decay_t =
          (float)machine->unk_byte_0x240 / (3.0f * (float)machine->unk_byte_0x241);
      machine->base_speed *= 1.0f - 0.6f * decay_t * decay_t;
      if ((machine->unk_byte_0x241 < machine->unk_byte_0x240) || (machine->base_speed <= 0.0f)) {
        machine->unk_byte_0x240 = 0u;
        machine->unk_byte_0x241 = 0u;
      } else {
        machine->unk_byte_0x240 += 1u;
      }
    }
    if ((machine->machine_state & FZGX_MS_0HP) != 0u) {
      float hp_scale = machine->speed_kmh / 100.0f;
      if (hp_scale > 1.0f) {
        hp_scale = 1.0f;
      }
      base_speed_delta *= 0.2f - 0.15f * hp_scale;
    }
    if ((machine->machine_state & (FZGX_MS_BOOSTING_DASHPLATE | FZGX_MS_BOOSTING)) == 0u) {
      forward_force_scale = 1000.0f * base_speed_delta;
    } else if (machine->stat_weight <= 1000.0f) {
      forward_force_scale = 1200.0f * base_speed_delta;
    } else {
      forward_force_scale = 1600.0f * base_speed_delta;
    }
  } else {
    forward_force_scale = -neg_local_fwd_speed;
    machine->base_speed = 0.014f * machine->race_start_charge;
    machine->unk_byte_0x241 = 0u;
    machine->unk_byte_0x240 = 0u;
  }

  fzgx_apply_machine_flags_from_exact_state(machine);
  return forward_force_scale;
}

static float fzgx_update_broken_machine_velocity_exact(
    const fzgx_machine_snapshot *machine) {
  if (((machine->track_state.flags & 0x01800000u) != 0u) &&
      (0.0f < machine->zero_minus_height_above_track)) {
    fzgx_vec3 reverse_forward = {
        -machine->basis_physical.basis_z_x,
        -machine->basis_physical.basis_z_y,
        -machine->basis_physical.basis_z_z,
    };
    return fzgx_vec3_normalized_dot(reverse_forward, machine->track_state.last_track_pos);
  }
  return 1.0f;
}

static void fzgx_apply_machine_drive_forces_exact(fzgx_machine_snapshot *machine) {
  float flattened_speed_mag = fzgx_vec3_length(machine->velocity_local_flattened_and_rotated);
  double neg_local_fwd_speed = -(double)machine->velocity_local.z;
  double abs_local_lateral_speed = (double)fabsf(machine->velocity_local.x);
  double drift_accel_factor;
  float forward_force_scale;
  float broken_velocity_scale;
  float airborne_force_scale = 1.0f;
  float turn_degrees;
  fzgx_mat43 rotated_basis = machine->basis_physical;
  fzgx_vec3 force_world;
  float speed_mag;

  if (((machine->machine_state &
        (FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_LOWGRIP | FZGX_MS_TOOKDAMAGE)) != 0u) ||
      ((double)flattened_speed_mag <= (double)(10.0f * machine->stat_weight) / 216.0)) {
    drift_accel_factor = 0.0;
  } else {
    float forward_ratio = machine->velocity_local_flattened_and_rotated.z / flattened_speed_mag;
    drift_accel_factor =
        (double)((1.0f - forward_ratio * forward_ratio) * machine->stat_drift_accel);
    if (1.0 < drift_accel_factor) {
      drift_accel_factor = 1.0;
    }
  }

  forward_force_scale = fzgx_update_machine_base_speed_and_boost_exact(
      (float)neg_local_fwd_speed,
      (float)abs_local_lateral_speed,
      (float)drift_accel_factor,
      machine);
  broken_velocity_scale =
      0.6f + 0.55f * fabsf(fzgx_update_broken_machine_velocity_exact(machine));
  if (1.0f < broken_velocity_scale) {
    broken_velocity_scale = 1.0f;
  }
  forward_force_scale *= broken_velocity_scale;
  machine->velocity.x *= broken_velocity_scale;
  machine->velocity.y *= broken_velocity_scale;
  machine->velocity.z *= broken_velocity_scale;
  if ((machine->machine_state & FZGX_MS_BOOSTING) == 0u) {
    machine->visual_pitch += 0.25f * forward_force_scale;
  } else {
    machine->visual_pitch += 0.05f * forward_force_scale;
  }
  if ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) {
    float airborne_alignment = 3.4f *
                               (0.3f + machine->basis_physical.basis_z_x * machine->surface_normal.x +
                                machine->basis_physical.basis_z_y * machine->surface_normal.y +
                                machine->basis_physical.basis_z_z * machine->surface_normal.z);
    if (airborne_alignment <= 1.0f) {
      if (airborne_alignment < 0.0f) {
        airborne_alignment = 0.0f;
      }
      airborne_force_scale = airborne_alignment * airborne_alignment;
    }
  }

  turn_degrees =
      -(machine->input_steer_yaw * machine->stat_turn_reaction +
        machine->input_strafe * machine->stat_strafe);
  if ((machine->machine_state & FZGX_MS_SIDEATTACKING) != 0u) {
    turn_degrees = 0.0f;
  }
  machine->turn_reaction_input = 0.75f * -(machine->input_steer_yaw * machine->stat_turn_reaction);
  if (turn_degrees < -45.0f) {
    turn_degrees = -45.0f;
  } else if (45.0f < turn_degrees) {
    turn_degrees = 45.0f;
  }
  fzgx_mat43_rotate_about_y_right(
      &rotated_basis, (uint16_t)(int)(182.04445f * turn_degrees));
  force_world = fzgx_transform_local_vector(
      &rotated_basis, (fzgx_vec3){0.0f, 0.0f, -(forward_force_scale * airborne_force_scale)});
  machine->velocity.x += force_world.x;
  machine->velocity.y += force_world.y;
  machine->velocity.z += force_world.z;

  speed_mag = fzgx_vec3_length(machine->velocity);
  if ((double)(speed_mag / machine->stat_weight) > 0.925925925) {
    if (machine->side_attack_delay == 6u) {
      float side_force = speed_mag;
      float side_force_cap = 5.555555f * machine->stat_weight;
      if (side_force_cap < side_force) {
        side_force = side_force_cap;
      }
      force_world = fzgx_transform_local_vector(
          &machine->basis_physical,
          (fzgx_vec3){machine->side_attack_indicator * side_force, 0.0f, 0.0f});
      machine->velocity.x += force_world.x;
      machine->velocity.y += force_world.y;
      machine->velocity.z += force_world.z;
    }
    if (((machine->terrain_flags >> 26) & 1u) != 0u) {
      force_world = fzgx_transform_local_vector(
          &machine->basis_physical, (fzgx_vec3){0.0f, 1.13f * speed_mag, 0.0f});
      machine->velocity.x += force_world.x;
      machine->velocity.y += force_world.y;
      machine->velocity.z += force_world.z;
      machine->state_2 |= 2u;
      machine->angular_velocity.x = 0.0f;
      machine->angular_velocity.z = 0.0f;
    }
  }

  machine->input_strafe_1_6 = machine->input_strafe_32 / 20.0f;
  machine->input_strafe_32 +=
      8.0f * machine->input_strafe - 5.0f * machine->input_strafe_1_6;
}

static void fzgx_clamp_machine_angular_velocity_exact(fzgx_machine_snapshot *machine) {
  float weight_val = 0.99f;
  float angvel_y;

  if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
    if ((machine->machine_state & FZGX_MS_JUSTLANDED) == 0u) {
      weight_val = 0.05f * machine->weight_derived_2;
    } else {
      weight_val = 0.2f * machine->weight_derived_2;
    }
  } else {
    machine->angular_velocity.x *= 0.9f;
    machine->angular_velocity.z *= weight_val;
    weight_val = machine->weight_derived_2;
  }
  angvel_y = machine->angular_velocity.y;
  if (fabsf(angvel_y) <= weight_val) {
    return;
  }
  if (angvel_y < 0.0f) {
    machine->angular_velocity.y = -weight_val;
  } else {
    machine->angular_velocity.y = weight_val;
  }
}

static void fzgx_integrate_machine_rotation_from_angular_velocity_exact(
    fzgx_machine_snapshot *machine) {
  float fVar1;
  float fVar2;
  fzgx_quat local_58;
  fzgx_mat43 mtxa;
  fzgx_mat43 saved_mtxa;
  fzgx_mat43 mStack_48;
  float angle_mag;

  fVar2 = machine->angular_velocity.x;
  fVar1 = machine->angular_velocity.z;
  if (fVar2 <= 3.0f) {
    if (-3.0f <= fVar2) {
      local_58.x = 0.0f;
    } else {
      local_58.x = fVar2 + 3.0f;
    }
  } else {
    local_58.x = fVar2 - 3.0f;
  }
  if (fVar1 <= 3.0f) {
    if (-3.0f <= fVar1) {
      local_58.z = 0.0f;
    } else {
      local_58.z = fVar1 + 3.0f;
    }
  } else {
    local_58.z = fVar1 - 3.0f;
  }
  local_58.x = local_58.x / machine->weight_derived_1;
  local_58.y = machine->angular_velocity.y / machine->weight_derived_2;
  local_58.z = local_58.z / machine->weight_derived_3;
  angle_mag = fzgx_math_sqrt_exact(
      local_58.z * local_58.z + local_58.y * local_58.y + local_58.x * local_58.x);
  fzgx_make_axis_angle_quat_exact(
      &local_58, (fzgx_vec3 *)&local_58, (int16_t)(int32_t)(10430.378f * angle_mag));
  mtxa = machine->basis_physical;
  saved_mtxa = mtxa;
  fzgx_mat43_from_quat_exact(&mtxa, &local_58);
  mStack_48 = mtxa;
  mtxa = saved_mtxa;
  fzgx_mtxa_multiply_mtx_exact(&mtxa, &mStack_48);
  machine->basis_physical = mtxa;
}

static void fzgx_integrate_machine_position_exact(fzgx_machine_snapshot *machine) {
  float inv_weight = 1.0f / machine->stat_weight;

  machine->position.x += machine->velocity.x * inv_weight;
  machine->position.y += machine->velocity.y * inv_weight;
  machine->position.z += machine->velocity.z * inv_weight;
}

static void fzgx_collide_with_landmines_exact(
    fzgx_sim_world *world,
    uint32_t machine_index,
    fzgx_machine_snapshot *machine) {
  fzgx_vec3 mine_pos = machine->position;
  fzgx_vec3 motion;
  fzgx_vec3 from_old_to_mine;
  fzgx_vec3 projected_pos;
  fzgx_vec3 projected_rel;
  fzgx_vec3 track_normal_component;
  fzgx_vec3 tangent_offset;
  fzgx_vec3 mine_push;
  fzgx_vec3 local_impulse;
  float motion_length;
  float motion_scale;
  float push_sign = -1.0f;
  float projected_rel_len;
  float track_normal_projection;
  float tangent_length;
  float tangent_scale;
  float signed_push_scale;
  float replay_scale;

  if (fzgx_find_terrain_and_objects_exact(world, &mine_pos, &machine->position_old, 0x4000u, machine_index) ==
      0u) {
    return;
  }

  motion = fzgx_vec3_sub(machine->position, machine->position_old);
  from_old_to_mine = fzgx_vec3_sub(mine_pos, machine->position_old);
  motion_length = fzgx_vec3_length(motion);
  motion_scale = fzgx_vec3_dot(motion, from_old_to_mine) / (motion_length * motion_length);
  projected_pos.x = motion.x * motion_scale + machine->position_old.x;
  projected_pos.y = motion.y * motion_scale + machine->position_old.y;
  projected_pos.z = motion.z * motion_scale + machine->position_old.z;
  if (600.0f < machine->speed_kmh) {
    push_sign = 1.0f;
  }
  projected_rel = fzgx_vec3_sub(projected_pos, mine_pos);
  projected_rel_len = fzgx_vec3_length(projected_rel);
  track_normal_projection =
      projected_rel_len * fzgx_vec3_normalized_dot(machine->surface_normal, projected_rel);
  track_normal_component =
      fzgx_vec3_add(mine_pos, fzgx_vec3_scale(machine->surface_normal, track_normal_projection));
  tangent_offset = fzgx_vec3_sub(projected_pos, track_normal_component);
  tangent_length = fzgx_vec3_length(tangent_offset);
  signed_push_scale =
      push_sign * fzgx_math_sqrt_exact(1.0f - tangent_length * 0.25f * tangent_length * 0.25f);
  tangent_scale = fzgx_math_sqrt_exact(1.0f - signed_push_scale * signed_push_scale);
  if (tangent_scale <= FLT_EPSILON) {
    tangent_offset = (fzgx_vec3){0};
  } else {
    fzgx_vec3_set_length_exact(tangent_scale, &tangent_offset, &tangent_offset);
  }
  if (signed_push_scale <= FLT_EPSILON) {
    mine_push = (fzgx_vec3){0};
  } else {
    fzgx_vec3_set_length_exact(signed_push_scale, &motion, &mine_push);
  }
  mine_push = fzgx_vec3_add(tangent_offset, mine_push);
  fzgx_vec3_set_length_exact(4.0f, &mine_push, &mine_push);
  machine->position = fzgx_vec3_add(track_normal_component, mine_push);

  local_impulse = fzgx_world_vector_to_local(
      &machine->basis_physical, fzgx_vec3_sub(machine->position, mine_pos));
  replay_scale = fzgx_vec3_length(local_impulse) / 20.0f;
  if (replay_scale < 0.01f) {
    replay_scale = 0.01f;
  } else if (0.6f < replay_scale) {
    replay_scale = 0.6f;
  }
  (void)replay_scale;
  fzgx_vec3_set_length_exact(5.555555f * machine->stat_weight, &local_impulse, &local_impulse);
  machine->visual_roll += 6.0f * local_impulse.x;
  machine->visual_pitch += 2.0f * local_impulse.z;
  local_impulse = fzgx_transform_local_vector(&machine->basis_physical, local_impulse);
  machine->velocity = fzgx_vec3_add(machine->velocity, local_impulse);
  machine->terrain_flags |= FZGX_TERRAIN_LANDMINE;
  fzgx_damage_machine_exact(20.0f, machine);
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static void fzgx_apply_random_startup_wobble_exact(fzgx_machine_snapshot *machine) {
  uint32_t wobble_hash =
      ((uint32_t)machine->position.z ^ (uint32_t)machine->position.x ^
       (uint32_t)machine->position.y ^ (uint32_t)machine->base_speed);
  float wobble_x =
      2.0f * (float)((wobble_hash ^ (uint32_t)machine->angular_velocity.x) & 0xffffu) / 65536.0f -
      1.0f;
  float wobble_y =
      0.5f +
      1.5f * (float)((wobble_hash ^ (uint32_t)machine->angular_velocity.y) & 0xffffu) / 65536.0f;
  fzgx_vec3 world_up = {0.0f, 0.0162037037037f * machine->stat_weight, 0.0f};
  fzgx_vec3 local_up;

  if (wobble_x <= 0.0f) {
    wobble_x -= 0.5f;
  } else {
    wobble_x += 0.5f;
  }
  local_up = fzgx_world_vector_to_local(&machine->basis_physical, world_up);
  machine->angular_velocity.x += -(wobble_y * local_up.y);
  machine->angular_velocity.y += -(wobble_x * local_up.z - wobble_y * local_up.x);
  machine->angular_velocity.z += -(-wobble_x * local_up.y);
}

static void fzgx_sync_bottom_pos_exact(fzgx_machine_snapshot *machine) {
  if ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) == 0u) {
    machine->position_bottom.x += machine->position.x - machine->position_old.x;
    machine->position_bottom.y += machine->position.y - machine->position_old.y;
    machine->position_bottom.z += machine->position.z - machine->position_old.z;
  }
}

static void fzgx_finalize_machine_post_rotation_tail_exact(fzgx_machine_snapshot *machine) {
  if ((machine->machine_state & FZGX_MS_B14) != 0u) {
    machine->machine_state |= FZGX_MS_JUSTTAPPEDACCEL;
  }
  if ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) != 0u) {
    machine->machine_state &= ~(FZGX_MS_RACEJUSTBEGAN_Q | FZGX_MS_JUSTTAPPEDACCEL);
  }
  if ((machine->machine_state & FZGX_MS_ACTIVE) != 0u) {
    uint32_t startup_frames = machine->frames_since_start_2;

    if (startup_frames < 30u) {
      if ((startup_frames % 6u) == 0u) {
        fzgx_apply_random_startup_wobble_exact(machine);
      }
    } else if (startup_frames < 90u) {
      machine->angular_velocity = (fzgx_vec3){0};
    }
  }
  if (machine->rail_collision_timer != 0u) {
    machine->rail_collision_timer -= 1u;
  }
  machine->machine_state &=
      ~(FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_LOWGRIP | FZGX_MS_TOOKDAMAGE | FZGX_MS_B14 |
        FZGX_MS_B13);
  machine->unk_float_0x208 = 200.0f;
  fzgx_sync_bottom_pos_exact(machine);
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static void fzgx_align_machine_y_with_track_normal_immediate_exact(
    fzgx_machine_snapshot *machine) {
  fzgx_mat43 mtxa;
  fzgx_quat rotation_quat;
  fzgx_vec3 current_up;

  current_up = fzgx_transform_local_vector(
      &machine->basis_physical, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  fzgx_make_quat_between_vectors_exact(
      &rotation_quat, &current_up, &machine->surface_normal);
  fzgx_mat43_from_quat_exact(&mtxa, &rotation_quat);
  fzgx_mtxa_multiply_mtx_exact(&mtxa, &machine->basis_physical);
  machine->basis_physical = mtxa;
}

static void fzgx_apply_machine_world_contact_response_exact(
    fzgx_machine_snapshot *machine,
    bool severe_contact) {
  float push_magnitude = fzgx_vec3_length(machine->collision_push_total);
  float speed_magnitude = fzgx_vec3_length(machine->velocity);
  double speed_per_weight = (double)(speed_magnitude / machine->stat_weight);
  bool apply_response = false;

  if (0.0023148148148148147 < (double)push_magnitude) {
    if (!severe_contact) {
      machine->machine_state |= FZGX_MS_LOWGRIP;
    } else {
      machine->machine_state |= FZGX_MS_TOOKDAMAGE;
      if (machine->wall_hit_count < 0xb4u) {
        machine->wall_hit_count += 1u;
      }
    }
  }

  if ((0.00462962962962 < (double)push_magnitude) &&
      (0.00462962962962 < speed_per_weight)) {
    apply_response = true;
  }
  if ((60u < machine->frames_since_start_2) && apply_response) {
    apply_response = ((machine->machine_state & FZGX_MS_TOOKDAMAGE) != 0u);
  }

  if (apply_response) {
    float push_dot_velocity;
    double capped_push_dot;

    machine->collision_response = machine->collision_push_total;
    push_dot_velocity =
        fzgx_vec3_normalized_dot(machine->collision_push_total, machine->velocity);
    capped_push_dot = (double)push_dot_velocity;
    if (0.0 < (double)push_dot_velocity) {
      capped_push_dot = 0.0;
    }
    if (0.02314814814 < speed_per_weight) {
      float push_dot_surface =
          fzgx_vec3_normalized_dot(machine->collision_push_total, machine->surface_normal);
      double damage_scale = 0.0;

      if (fabsf(push_dot_surface) < 0.7f) {
        damage_scale = (double)((0.15f + (float)(capped_push_dot * capped_push_dot)) / 1.5f);
        if ((machine->machine_state & FZGX_MS_B10) == 0u) {
          damage_scale = (double)((float)(damage_scale * (double)speed_magnitude) / 500.0f);
          if (machine->rail_collision_timer != 0u) {
            damage_scale = (double)(float)(damage_scale * 0.15f);
          }
        } else {
          damage_scale = (double)((float)(damage_scale * (double)speed_magnitude) / 2000.0f);
        }
      }
      if (capped_push_dot < -0.5) {
        machine->machine_state &=
            ~(FZGX_MS_B23 | FZGX_MS_BOOSTING_DASHPLATE | FZGX_MS_JUSTPRESSEDBOOST |
              FZGX_MS_BOOSTING);
        machine->boost_frames = 0u;
        machine->boost_frames_manual = 0u;
      }
      if ((machine->machine_state & FZGX_MS_TOOKDAMAGE) != 0u) {
        fzgx_damage_machine_exact((float)damage_scale, machine);
      }
    }

    {
      fzgx_vec3 collision_impulse;

      fzgx_vec3_set_length_exact(
          (float)(capped_push_dot * (double)speed_magnitude),
          &machine->collision_push_total,
          &collision_impulse);
      if (capped_push_dot < 0.0) {
        float speed_scale;
        float boost_scale;
        float root_term;

        root_term = fzgx_math_sqrt_exact(
            1.0f - (float)(capped_push_dot / 0.7) * (float)(capped_push_dot / 0.7));
        if (machine->rail_collision_timer == 0u) {
          speed_scale = 0.2f + 0.6f * root_term;
          boost_scale = 0.3f + 0.4f * speed_scale;
        } else {
          speed_scale = 0.64f + 0.35f * root_term;
          boost_scale = 0.3f + 0.6f * speed_scale;
        }
        machine->base_speed *= speed_scale;
        machine->boost_turbo *= boost_scale;
      }
      if (speed_per_weight <= 1.851851851) {
        collision_impulse = fzgx_vec3_scale(collision_impulse, -1.0f);
        machine->velocity = fzgx_vec3_add(machine->velocity, collision_impulse);
      } else {
        float impulse_scale;

        if ((machine->machine_state & FZGX_MS_0HP) == 0u) {
          if (machine->rail_collision_timer == 0u) {
            impulse_scale = (float)(3.0 - 1.5 * fabs(capped_push_dot));
          } else {
            impulse_scale = (float)(2.0 - fabs(capped_push_dot));
          }
        } else {
          impulse_scale = (float)(3.4 - 1.7 * fabs(capped_push_dot));
        }
        collision_impulse = fzgx_vec3_scale(collision_impulse, -impulse_scale);
        machine->velocity = fzgx_vec3_add(machine->velocity, collision_impulse);
        if (machine->rail_collision_timer == 0u) {
          size_t i;
          for (i = 0u; i < 4u; ++i) {
            machine->suspension_corners[i].state |= 4u;
            machine->suspension_state[i] = (uint8_t)machine->suspension_corners[i].state;
          }
        }
        machine->rail_collision_timer = 20u;
      }
      {
        fzgx_vec3 local_impulse =
            fzgx_world_vector_to_local(&machine->basis_physical, collision_impulse);
        size_t i;

        machine->visual_roll += local_impulse.x;
        machine->visual_pitch += local_impulse.z;
        if ((machine->machine_state & FZGX_MS_ACTIVE) != 0u) {
          for (i = 0u; i < 4u; ++i) {
            fzgx_vec3 local_position = machine->wall_corners[i].offset;

            local_position.z += 0.2f;
            fzgx_apply_machine_angular_impulse_from_world_force_exact(
                machine, &local_position, &machine->wall_corners[i].collision);
          }
        }
        if (60u < machine->frames_since_start_2) {
          fzgx_align_machine_y_with_track_normal_immediate_exact(machine);
        }
      }
    }
  } else if (((machine->machine_state & FZGX_MS_JUSTLANDED) != 0u) &&
             (0.0462962962962 <= speed_per_weight)) {
    float up_dot_surface =
        fzgx_vec3_normalized_dot(fzgx_mat43_get_basis_y_exact(&machine->basis_physical), machine->surface_normal);
    float vel_dot_surface =
        fzgx_vec3_normalized_dot(machine->velocity, machine->surface_normal);
    float surface_vel_len;
    float normal_component;
    fzgx_vec3 tangent_velocity;

    if (up_dot_surface < 0.0f) {
      up_dot_surface = 0.0f;
    }
    surface_vel_len = speed_magnitude;
    normal_component = surface_vel_len * vel_dot_surface;
    machine->base_speed = (float)((double)machine->base_speed * (double)up_dot_surface);
    tangent_velocity = fzgx_vec3_sub(
        machine->velocity,
        fzgx_vec3_scale(machine->surface_normal, normal_component));
    if ((float)(2.0 * fabs((double)(0.5f + vel_dot_surface))) < 0.9f) {
      fzgx_vec3_set_length_exact(
          0.9f * (float)((double)(1.0f - 1.11f * (float)(2.0 * fabs((double)(0.5f + vel_dot_surface)))) *
                         (double)up_dot_surface),
          &tangent_velocity,
          &tangent_velocity);
    }
    machine->velocity = fzgx_vec3_add(
        fzgx_vec3_sub(
            machine->velocity,
            fzgx_vec3_scale(machine->surface_normal, normal_component * up_dot_surface)),
        tangent_velocity);
  }
  machine->position = fzgx_vec3_add(machine->position, machine->collision_push_total);
  fzgx_apply_machine_flags_from_exact_state(machine);
}

static void fzgx_update_machine_air_tilt_exact(fzgx_machine_snapshot *machine) {
  bool active_air_tilt = false;

  if ((60u < machine->frames_since_start_2) &&
      ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u)) {
    active_air_tilt = true;
  }
  if (active_air_tilt) {
    float air_tilt_delta = 2.0f * fabsf(machine->input_steer_yaw);

    if ((machine->state_2 & 2u) != 0u) {
      air_tilt_delta = 0.0f;
    }
    if (0.1f <= air_tilt_delta) {
      air_tilt_delta += 2.0f * machine->input_steer_pitch * fabsf(2.0f - air_tilt_delta);
      if (((machine->machine_state & FZGX_MS_BOOSTING) != 0u) &&
          ((machine->machine_state & FZGX_MS_BOOSTING_DASHPLATE) == 0u)) {
        air_tilt_delta *= 2.0f;
      }
    } else {
      air_tilt_delta += 4.0f * machine->input_steer_pitch;
    }
    if (60u < machine->air_time) {
      float air_time_scale = ((float)machine->air_time - 60.0f) / 120.0f;

      if (1.0f < air_time_scale) {
        air_time_scale = 1.0f;
      }
      air_tilt_delta = air_tilt_delta * (1.0f + 0.3f * air_time_scale) + 0.3f * air_time_scale;
    }
    if ((machine->branch_indicator & 0x01800000u) != 0u) {
      air_tilt_delta *= 0.3f;
    }
    machine->air_tilt += air_tilt_delta;
    if (machine->air_tilt < -50.0f) {
      machine->air_tilt = -50.0f;
    } else if (60.0f < machine->air_tilt) {
      machine->air_tilt = 60.0f;
    }
  } else {
    machine->air_tilt = 0.0f;
  }
}

static void fzgx_orient_vehicle_from_gravity_or_road_exact(fzgx_machine_snapshot *machine) {
  int32_t iVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  double dVar5;
  double dVar6;
  float fVar7;
  fzgx_sincos_result local_118;
  fzgx_vec3 vStack_110;
  fzgx_vec3 vStack_104;
  fzgx_vec3 local_f8;
  fzgx_vec3 local_ec;
  fzgx_quat qStack_e0;
  fzgx_vec3 local_d0;
  fzgx_vec3 local_c4;
  fzgx_quat qStack_b8;
  float fStack_a8;
  float fStack_a4;
  float local_a0;
  fzgx_mat43 mStack_9c;
  fzgx_mat43 mStack_6c;
  fzgx_mat43 mtxa;

  mtxa = machine->basis_physical;
  dVar5 = (double)(1.5f + machine->stat_weight / 4000.0f);
  if ((double)1.8f <= dVar5) {
    if ((double)2.0f < dVar5) {
      dVar5 = (double)2.0f;
    }
  } else {
    dVar5 = (double)(float)(3.6 - dVar5);
  }
  if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
    local_a0 = (float)(dVar5 * (double)1.3f);
  } else if (machine->zero_minus_height_above_track <= 0.0f) {
    local_a0 = (float)(dVar5 * (double)0.6f);
  } else if ((machine->machine_state & FZGX_MS_B10) == 0u) {
    local_a0 = (float)(dVar5 * (double)1.3f);
  } else {
    local_a0 = (float)(dVar5 * (double)1.8f);
  }
  local_a0 = 10.0f * -(0.009f * machine->stat_weight) * local_a0;
  fStack_a8 = machine->surface_normal.x * local_a0;
  fStack_a4 = machine->surface_normal.y * local_a0;
  local_a0 = machine->surface_normal.z * local_a0;
  machine->velocity.x = machine->velocity.x + fStack_a8;
  machine->velocity.y = machine->velocity.y + fStack_a4;
  machine->velocity.z = machine->velocity.z + local_a0;
  if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
    local_f8 = fzgx_transform_local_vector(&mtxa, (fzgx_vec3){0.0f, 1.0f, 0.0f});
    fVar7 = fzgx_vec3_normalized_dot(local_f8, machine->surface_normal);
    dVar5 = (double)fVar7;
    if (dVar5 < (double)0.7f) {
      fVar7 = 0.0f;
      if (0.0 <= dVar5) {
        fVar7 = (float)(dVar5 / (double)0.7f);
      }
      fVar2 = machine->surface_normal.z;
      fVar3 = machine->surface_normal.x;
      fVar4 = machine->surface_normal.y;
      local_ec.x = -(local_f8.z * fVar4 - local_f8.y * fVar2);
      iVar1 = (int32_t)(182.04445f * 40.0f * (1.0f - fVar7));
      local_ec.y = -(local_f8.x * fVar2 - local_f8.z * fVar3);
      local_ec.z = -(local_f8.y * fVar3 - local_f8.x * fVar4);
      fzgx_make_axis_angle_quat_exact(&qStack_e0, &local_ec, (int16_t)iVar1);
      mStack_9c = mtxa;
      fzgx_mat43_from_quat_exact(&mtxa, &qStack_e0);
      fzgx_mtxa_multiply_mtx_exact(&mtxa, &mStack_9c);
    }
    machine->basis_physical = mtxa;
  } else {
    iVar1 = (int32_t)(182.04445f * machine->air_tilt);
    local_118 = fzgx_math_sincos_14b((uint16_t)iVar1);
    local_c4 =
        fzgx_transform_local_vector(&mtxa, (fzgx_vec3){0.0f, local_118.cos_value, local_118.sin_value});
    fVar7 = fzgx_vec3_normalized_dot(local_c4, machine->surface_normal);
    if (fVar7 < 0.992f) {
      dVar5 = (double)(fVar7 + 0.008f);
      fVar7 = 15.0f;
      if ((machine->branch_indicator & 0x01800000u) != 0u) {
        fVar7 = 50.0f;
      }
      dVar6 = (double)fVar7;
      fVar7 = machine->surface_normal.z;
      fVar2 = machine->surface_normal.x;
      fVar3 = machine->surface_normal.y;
      local_d0.x = -(local_c4.z * fVar3 - local_c4.y * fVar7);
      local_d0.y = -(local_c4.x * fVar7 - local_c4.z * fVar2);
      local_d0.z = -(local_c4.y * fVar2 - local_c4.x * fVar3);
      fVar7 = fzgx_vec3_length(local_d0);
      if ((fVar7 < 0.1f) || (dVar5 < (double)0.008f)) {
        vStack_104 = fzgx_transform_local_vector(&mtxa, (fzgx_vec3){0.0f, 1.0f, 0.0f});
        fVar7 = fzgx_vec3_normalized_dot(vStack_104, machine->surface_normal);
        if (fVar7 <= 0.0f) {
          vStack_110 = fzgx_mat43_get_basis_x_exact(&mtxa);
          local_d0 = fzgx_mat43_get_basis_z_exact(&mtxa);
          fVar7 = fzgx_vec3_normalized_dot(machine->surface_normal, vStack_110);
          if (0.0 < (double)fVar7) {
            local_d0.x = -local_d0.x;
            local_d0.y = -local_d0.y;
            local_d0.z = -local_d0.z;
          }
        }
      }
      fVar7 = 0.0f;
      if (0.0 <= dVar5) {
        fVar7 = (float)(dVar5 * dVar5);
      }
      iVar1 = (int32_t)(182.04445f * (float)(dVar6 * (double)(1.0f - fVar7)));
      fzgx_make_axis_angle_quat_exact(&qStack_b8, &local_d0, (int16_t)iVar1);
      mStack_6c = mtxa;
      fzgx_mat43_from_quat_exact(&mtxa, &qStack_b8);
      fzgx_mtxa_multiply_mtx_exact(&mtxa, &mStack_6c);
    }
    machine->basis_physical = mtxa;
  }
}

static void fzgx_handle_drag_and_glide_forces_exact(fzgx_machine_snapshot *machine) {
  float speed_length = fzgx_vec3_length(machine->velocity);
  double weight_speed = (double)(speed_length / machine->stat_weight);

  if (2.0 <= 216.0 * weight_speed) {
    if (216.0 * weight_speed <= 9990.0) {
      float velocity_dot_normal =
          fzgx_vec3_normalized_dot(machine->surface_normal, machine->velocity);
      double forward_dot_normal =
          (double)fzgx_vec3_normalized_dot(
              machine->surface_normal,
              fzgx_transform_local_vector(&machine->basis_physical, (fzgx_vec3){0.0f, 0.0f, -1.0f}));
      float normal_velocity_scale = machine->stat_weight * (float)((double)velocity_dot_normal * weight_speed);
      fzgx_vec3 normal_velocity = fzgx_vec3_scale(machine->surface_normal, normal_velocity_scale);
      fzgx_vec3 drag_vector = fzgx_vec3_sub(machine->velocity, normal_velocity);
      double glide_scale = (double)(float)(weight_speed * (double)(float)(8.0 * weight_speed));
      double lift_scale;

      if ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) {
        if (0.0 <= forward_dot_normal) {
          forward_dot_normal = (double)(float)(forward_dot_normal + 1.0f);
        } else {
          double clamped = (double)(float)(1.0f + forward_dot_normal);
          if ((double)(float)clamped < 0.0) {
            clamped = 0.0;
          }
          forward_dot_normal = clamped;
          glide_scale = (double)(float)(glide_scale * forward_dot_normal);
        }
      }
      fzgx_vec3_set_length_exact((float)glide_scale, &drag_vector, &drag_vector);
      machine->visual_shake_mult = (float)glide_scale;
      lift_scale = (double)velocity_dot_normal;
      if (machine->stat_weight < 1100.0f) {
        float weight_norm = machine->stat_weight / 1100.0f;
        lift_scale = (double)(float)(lift_scale * (double)(weight_norm * weight_norm));
      }
      if ((machine->machine_state & FZGX_MS_BOOSTING) == 0u) {
        if ((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) {
          if ((0.0 <= lift_scale) || (forward_dot_normal <= 0.8)) {
            forward_dot_normal = (double)(float)(lift_scale * 0.6f);
          } else {
            forward_dot_normal = (double)(float)(lift_scale * (0.6f + 4.0f * (float)(forward_dot_normal - 0.8)));
          }
        } else {
          forward_dot_normal = (double)(float)(lift_scale * 0.6f);
        }
      } else {
        forward_dot_normal = (double)(float)(lift_scale * 0.5f);
      }
      drag_vector = fzgx_vec3_add(
          drag_vector,
          fzgx_vec3_scale(machine->surface_normal, (float)(glide_scale * forward_dot_normal)));
      if (machine->frames_since_death != 0u) {
        float death_scale = 0.01f * ((float)machine->frames_since_death - 4.0f);

        if (death_scale < 0.0f) {
          death_scale = 0.0f;
        } else if (1.0f < death_scale) {
          death_scale = 1.0f;
        }
        drag_vector = fzgx_vec3_scale(drag_vector, death_scale);
      }
      machine->velocity = fzgx_vec3_sub(machine->velocity, drag_vector);
    } else {
      fzgx_vec3_set_length_exact(46.25f, &machine->velocity, &machine->velocity);
    }
  } else {
    machine->velocity = (fzgx_vec3){0};
    machine->visual_shake_mult = 0.0f;
  }
}

fzgx_status fzgx_sim_step_begin(
    fzgx_sim_world *world,
    const fzgx_control_sample *controls,
    size_t control_count) {
  size_t i;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (control_count < world->machine_count) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((world->machine_count != 0u) && (controls == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (i = 0; i < world->machine_count; ++i) {
    fzgx_control_sample previous_control = world->controls[i];

    world->machines[i].machine_flags &= ~FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
    world->machines[i].state_2 &= ~8u;
    world->controls[i] = controls[i];
    fzgx_stage_control_sample_to_machine(&world->machines[i], &previous_control, &controls[i]);
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_step_machine_phase(fzgx_sim_world *world) {
  uint32_t bank_index;
  uint32_t i;
  const fzgx_track_manifest *track_manifest;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  track_manifest = fzgx_get_active_track_manifest(world);
  for (bank_index = 0u; bank_index < 4u; ++bank_index) {
    uint32_t bank_increment = 1u;

    if ((bank_index == 1u) && (track_manifest != 0) && (track_manifest->authored_track_id == 0x27u)) {
      bank_increment = 0u;
    }
    world->stage_scene_frame_banks[bank_index] += bank_increment;
    if (world->stage_scene_frame_banks[bank_index] > 0x00690e87u) {
      world->stage_scene_frame_banks[bank_index] = 0u;
    }
  }
  if (world->has_pending_stage_scene_story_delta != 0u) {
    world->stage_scene_story_clip_offset_frames -= world->pending_stage_scene_story_delta_frames;
    world->pending_stage_scene_story_delta_frames = 0.0f;
    world->has_pending_stage_scene_story_delta = 0u;
  }
  for (i = 0u; i < world->dynamic_scene_runtime_flag_count; ++i) {
    uint32_t flags = world->dynamic_scene_runtime_flags[i];

    if ((flags & 0x20000000u) != 0u) {
      flags &= ~0x20000000u;
      flags |= 0x40000000u;
      world->dynamic_scene_runtime_flags[i] = flags;
    }
  }
  for (i = 0; i < world->machine_count; ++i) {
    fzgx_machine_snapshot *machine = &world->machines[i];
    bool found_ground = false;
    bool found_floor = false;
    fzgx_vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    bool trace_mcsg_motion =
        (track_manifest != 0) &&
        fzgx_debug_trace_double_branches_window_exact(
            track_manifest->authored_track_id, i, world->frame_index);
    bool trace_stage_motion = trace_mcsg_motion ||
                              ((i == 0u) &&
                               ((world->frame_index < 40u) ||
                                ((120u <= world->frame_index) && (world->frame_index <= 130u))));

    if (fzgx_handle_machine_respawn_exact(world, i, machine)) {
      continue;
    }
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=machine_begin|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|"
          "ang=(%.3f,%.3f,%.3f)|basis_y=(%.3f,%.3f,%.3f)|basis_z=(%.3f,%.3f,%.3f)|"
          "surf=(%.3f,%.3f,%.3f)|"
          "cp=%d|cpf=%.6f|cur_cp=%d|cur_cpf=%.6f|next_cp=%d|next_cpf=%.6f\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->basis_physical.basis_y_x,
          machine->basis_physical.basis_y_y,
          machine->basis_physical.basis_y_z,
          machine->basis_physical.basis_z_x,
          machine->basis_physical.basis_z_y,
          machine->basis_physical.basis_z_z,
          machine->surface_normal.x,
          machine->surface_normal.y,
          machine->surface_normal.z,
          machine->current_checkpoint,
          machine->checkpoint_fraction,
          machine->track_state.cur_cp_idx,
          machine->track_state.cur_cp_frac,
          machine->track_state.next_cp_idx,
          machine->track_state.next_cp_frac);
      fzgx_debug_log_mat43_basis_exact(
          "physstage",
          "machine_begin",
          world->frame_index,
          i,
          &machine->basis_physical,
          machine);
    }
    machine->state_2 |= 8u;
    machine->machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
    fzgx_prepare_machine_frame_exact(world, i, machine, &found_ground, &ground_normal);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_prepare|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|"
          "found_ground=%u|ground=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          found_ground ? 1u : 0u,
          ground_normal.x,
          ground_normal.y,
          ground_normal.z);
    }
    if (track_manifest != 0) {
      found_floor = fzgx_find_floor_beneath_machine_exact(world, i, machine);
    }
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_floor|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|"
          "found_floor=%u|surf=(%.3f,%.3f,%.3f)|height=%.6f|floor=0x%08x\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          found_floor ? 1u : 0u,
          machine->surface_normal.x,
          machine->surface_normal.y,
          machine->surface_normal.z,
          machine->zero_minus_height_above_track,
          machine->floor_surface_flags);
    }
    if (found_floor && found_ground) {
      machine->surface_normal = ground_normal;
    }
    fzgx_handle_steering_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_steer|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z);
    }
    fzgx_handle_suspension_states_exact(machine);
    if (machine->frames_since_start_2 != 0u) {
      double angle_velocity_y = (double)machine->angular_velocity.y;
      size_t corner_index;

      for (corner_index = 0u; corner_index < 4u; ++corner_index) {
        fzgx_handle_machine_turn_and_strafe_exact(
            angle_velocity_y, machine, &machine->suspension_corners[corner_index]);
        machine->suspension_state[corner_index] =
            (uint8_t)machine->suspension_corners[corner_index].state;
        if (trace_stage_motion) {
          const fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[corner_index];

          fprintf(
              stderr,
              "motiontrace|frame=%u|stage=turn_corner|i=%zu|force=(%.3f,%.3f,%.3f)|"
              "pos=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)\n",
              world->frame_index,
              corner_index,
              corner->force_spatial.x,
              corner->force_spatial.y,
              corner->force_spatial.z,
              corner->pos.x,
              corner->pos.y,
              corner->pos.z,
              corner->pos_old.x,
              corner->pos_old.y,
              corner->pos_old.z,
              machine->velocity.x,
              machine->velocity.y,
              machine->velocity.z);
        }
      }
    }
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_turn|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|turn=%.3f\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->turning_related);
    }
    if ((machine->machine_state & FZGX_MS_AIRBORNEMORE0_2S_Q) != 0u) {
      machine->turning_related *= 0.02f;
    }
    if (0.01f < fabsf(machine->input_strafe)) {
      machine->turning_related *= 0.04f;
    }
    fzgx_apply_machine_drive_forces_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_drive|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|turn=%.3f\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->turning_related);
    }
    fzgx_clamp_machine_angular_velocity_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_clamp_ang|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z);
    }
    fzgx_update_machine_air_tilt_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_air_tilt|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|basis_z=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->basis_physical.basis_z_x,
          machine->basis_physical.basis_z_y,
          machine->basis_physical.basis_z_z);
    }
    fzgx_orient_vehicle_from_gravity_or_road_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_orient|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|basis_z=(%.3f,%.3f,%.3f)|surf=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->basis_physical.basis_z_x,
          machine->basis_physical.basis_z_y,
          machine->basis_physical.basis_z_z,
          machine->surface_normal.x,
          machine->surface_normal.y,
          machine->surface_normal.z);
      fzgx_debug_log_mat43_basis_exact(
          "physstage",
          "post_orient",
          world->frame_index,
          i,
          &machine->basis_physical,
          machine);
    }
    fzgx_handle_drag_and_glide_forces_exact(machine);
    fzgx_integrate_machine_position_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_integrate_pos|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z);
    }
    fzgx_collide_with_landmines_exact(world, i, machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_landmine|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z);
    }
    machine->basis_physical.origin_x = machine->position.x;
    machine->basis_physical.origin_y = machine->position.y;
    machine->basis_physical.origin_z = machine->position.z;
    fzgx_integrate_machine_rotation_from_angular_velocity_exact(machine);
    machine->basis_physical.origin_x = 0.0f;
    machine->basis_physical.origin_y = 0.0f;
    machine->basis_physical.origin_z = 0.0f;
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_rotate|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|basis_y=(%.3f,%.3f,%.3f)|basis_z=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->basis_physical.basis_y_x,
          machine->basis_physical.basis_y_y,
          machine->basis_physical.basis_y_z,
          machine->basis_physical.basis_z_x,
          machine->basis_physical.basis_z_y,
          machine->basis_physical.basis_z_z);
      fzgx_debug_log_mat43_basis_exact(
          "physstage",
          "post_rotate",
          world->frame_index,
          i,
          &machine->basis_physical,
          machine);
    }
    fzgx_finalize_machine_post_rotation_tail_exact(machine);
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=post_finalize|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z);
    }
    if (trace_stage_motion) {
      fprintf(
          stderr,
          "motiontrace|frame=%u|stage=machine_end|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|"
          "ang=(%.3f,%.3f,%.3f)|height=%.6f|floor=0x%08x\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->angular_velocity.x,
          machine->angular_velocity.y,
          machine->angular_velocity.z,
          machine->zero_minus_height_above_track,
          machine->floor_surface_flags);
      fzgx_debug_log_mat43_basis_exact(
          "physstage",
          "machine_end",
          world->frame_index,
          i,
          &machine->basis_physical,
          machine);
    }
  }
  return FZGX_STATUS_OK;
}

static void fzgx_handle_machine_damage_and_state_generic_exact(
    const fzgx_track_course_content *course,
    fzgx_machine_snapshot *machine,
    uint32_t anim_timer) {
  bool lava_damage_applied = false;
  bool should_seed_restore;

  if ((machine == 0) || ((machine->state_2 & 8u) == 0u)) {
    return;
  }

  if (machine->frames_since_death != 0u) {
    fzgx_breakdown_physics_exact(machine);
  }

  if ((machine->terrain_flags & FZGX_TERRAIN_LAVA) != 0u) {
    fzgx_damage_machine_exact(0.33333f, machine);
    lava_damage_applied = true;
    if (((machine->state_2 & 0x200u) != 0u) && ((machine->machine_state & FZGX_MS_0HP) != 0u)) {
      fzgx_destroy_machine_instantly_exact(machine);
    }
  }

  {
    fzgx_mat43 transform = machine->basis_physical;
    size_t i;

    fzgx_mat43_set_origin_exact(&transform, machine->position);
    for (i = 0u; i < 4u; ++i) {
      fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[i];
      fzgx_machine_wall_corner_snapshot *wall_corner = &machine->wall_corners[i];

      corner->pos_old = corner->pos;
      corner->pos = fzgx_transform_local_point(
          &transform,
          (fzgx_vec3){
              corner->offset.x,
              (corner->offset.y + corner->force) - corner->rest_length_scale,
              corner->offset.z,
          });
      wall_corner->pos_a = wall_corner->pos_b;
      wall_corner->pos_b = fzgx_transform_local_point(&transform, wall_corner->offset);
    }
  }

  if (((machine->state_2 & 0x10u) == 0u) &&
      ((machine->position.y < -5000.0f) ||
       ((course != 0) && (machine->position.y < (course->track_min_height - 900.0f))))) {
    fzgx_destroy_machine_instantly_exact(machine);
  }

  if (machine->position.y < -10000.0f) {
    machine->position.y = -10000.0f;
    machine->velocity = (fzgx_vec3){0};
  }

  fzgx_create_machine_visual_transform_exact(machine, anim_timer);

  if ((machine->machine_state & FZGX_MS_STARTINGCOUNTDOWN) != 0u) {
    machine->speed_kmh = 0.0f;
  } else {
    fzgx_refresh_machine_speed_kmh_exact(machine);
    if ((machine->max_speed_kmh < machine->speed_kmh) &&
        ((machine->machine_state &
          (FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_LOWGRIP | FZGX_MS_TOOKDAMAGE)) == 0u) &&
        ((machine->speed_kmh < (80.0f + machine->max_speed_kmh)) || (machine->speed_kmh < 1700.0f))) {
      machine->max_speed_kmh = machine->speed_kmh;
    }
  }
  should_seed_restore = fzgx_handle_machine_crash_exact(machine, true);
  if (!should_seed_restore) {
    uint32_t state = machine->machine_state;

    if ((state & (FZGX_MS_RETIRED | FZGX_MS_B10 | FZGX_MS_B1)) == 0u) {
      if (((state & FZGX_MS_0HP) != 0u) && (machine->frames_since_death == 0u)) {
        fzgx_breakdown_fling_physics_exact(machine);
      }
    } else {
      if ((state & (FZGX_MS_B10 | FZGX_MS_0HP)) == (FZGX_MS_B10 | FZGX_MS_0HP)) {
        if (10.0f <= machine->speed_kmh) {
          if (100.0f <= machine->speed_kmh) {
            if (machine->speed_kmh < 400.0f) {
              machine->velocity.x *= 0.99f;
              machine->velocity.y *= 0.99f;
              machine->velocity.z *= 0.99f;
            }
          } else {
            machine->velocity.x *= 0.95f;
            machine->velocity.y *= 0.95f;
            machine->velocity.z *= 0.95f;
          }
        } else {
          machine->velocity = (fzgx_vec3){0};
          machine->position = machine->position_old;
          machine->machine_state |= FZGX_MS_RETIRED;
          if ((machine->state_2 & 0x80u) == 0u) {
            machine->state_2 |= 0x100u;
          }
          machine->state_2 |= 0x80u;
        }
      }
    }
  } else if (machine->frames_until_restored == 0u) {
    machine->frames_until_restored = FZGX_CRASH_RESTORE_FRAMES_DEFAULT_EXACT;
    if (((machine->machine_state & FZGX_MS_0HP) != 0u) &&
        (((machine->machine_state & (FZGX_MS_JUSTHITVEHICLE_Q | FZGX_MS_TOOKDAMAGE)) != 0u) ||
         lava_damage_applied)) {
      machine->frames_until_restored = (uint16_t)(int)(
          (double)machine->frames_until_restored + fzgx_crash_restore_damage_addend_exact);
    }
    if ((machine->machine_state & FZGX_MS_DIEDTHISFRAMEOOB_Q) != 0u) {
      machine->frames_until_restored = FZGX_CRASH_RESTORE_FRAMES_DEFAULT_EXACT;
    }
  }

  if ((machine->machine_approach_frame_counter == 0u) &&
      ((machine->machine_state & (FZGX_MS_FALLOUT | FZGX_MS_0HP)) == 0u) &&
      (fzgx_vec3_length(machine->approach_dir) < 200.0f)) {
    machine->machine_approach_frame_counter = 0x3cu;
  }

  fzgx_apply_machine_flags_from_exact_state(machine);
}

fzgx_status fzgx_sim_step_frame_phase(
    fzgx_sim_world *world,
    const fzgx_race_step_options *options) {
  uint32_t i;
  const fzgx_track_manifest *track_manifest;
  const fzgx_track_course_content *track_course = 0;
  const fzgx_track_course_animation_content *track_animation_course = 0;
  fzgx_frame_phase_race_context race_context;
  bool have_track_course = false;
  bool have_track_animation_course = false;
  bool full_heal_override_active;
  bool apply_full_heal_exact;
  bool lap_timer_gate_active;
  fzgx_status status = fzgx_validate_world(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  race_context = fzgx_build_frame_phase_race_context_exact(options);
  full_heal_override_active =
      race_context.temporary_full_heal_override || (world->race_full_heal_latch_active != 0u);
  apply_full_heal_exact =
      full_heal_override_active ||
      ((options != 0) && !options->has_raw_frame_state && options->force_full_heal_this_frame);
  lap_timer_gate_active =
      (options != 0) &&
      options->advance_lap_timers &&
      ((!options->has_raw_frame_state) || race_context.raw_frame_state_live);
  track_manifest = fzgx_get_active_track_manifest(world);
  if ((track_manifest != 0) && (world->content != 0)) {
    status = fzgx_get_active_track_course(world, &track_course);
    if (status == FZGX_STATUS_OK) {
      have_track_course = true;
      status = fzgx_content_bundle_get_track_course_animation_for_track_index(
          world->content, world->active_track_index, &track_animation_course);
      if (status == FZGX_STATUS_OK) {
        have_track_animation_course = true;
      } else if (status != FZGX_STATUS_OUT_OF_RANGE) {
        return status;
      }
    } else if (status != FZGX_STATUS_OUT_OF_RANGE) {
      return status;
    }
  }
  for (i = 0u; i < world->machine_count; ++i) {
    uint32_t j;

    for (j = i + 1u; j < world->machine_count; ++j) {
      if (fzgx_handle_machine_pair_collision_exact(
              i, &world->machines[i], j, &world->machines[j])) {
        if ((world->machines[i].entrant_runtime_flags &
             FZGX_ENTRANT_RUNTIME_FLAG_COLLISION_DESTROY) != 0u) {
          fzgx_destroy_machine_instantly_exact(&world->machines[i]);
        }
        if ((world->machines[j].entrant_runtime_flags &
             FZGX_ENTRANT_RUNTIME_FLAG_COLLISION_DESTROY) != 0u) {
          fzgx_destroy_machine_instantly_exact(&world->machines[j]);
        }
        /* Deferred exact branch: FUN_8020f2f8 collision-reactive effect path. */
      }
    }
  }
  for (i = 0u; i < world->machine_count; ++i) {
    fzgx_machine_snapshot *machine = &world->machines[i];
    if ((machine->state_2 & 8u) != 0u) {
      bool trace_fire_field_kill_window =
          (track_manifest != 0) &&
          (track_manifest->authored_track_id == 17u) &&
          (i == 0u) &&
          (2248u <= world->frame_index) &&
          (world->frame_index <= 2256u);
      bool trace_mcsg_contact =
          ((track_manifest != 0) &&
           fzgx_debug_trace_double_branches_window_exact(
               track_manifest->authored_track_id, i, world->frame_index)) ||
          ((i == 0u) && (world->frame_index < 5u)) ||
          trace_fire_field_kill_window;
      bool severe_contact = fzgx_refresh_machine_wall_contact_queries_exact(world, i, machine);
      if (trace_mcsg_contact) {
        size_t corner_index;

        fprintf(
            stderr,
            "frametrace|frame=%u|stage=pre_contact_response|pos=(%.3f,%.3f,%.3f)|"
            "vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|"
            "push=(%.3f,%.3f,%.3f)|surface=0x%08x|rail=%u|severe=%u\n",
            world->frame_index,
            machine->position.x,
            machine->position.y,
            machine->position.z,
            machine->velocity.x,
            machine->velocity.y,
            machine->velocity.z,
            machine->angular_velocity.x,
            machine->angular_velocity.y,
            machine->angular_velocity.z,
            machine->collision_push_total.x,
            machine->collision_push_total.y,
            machine->collision_push_total.z,
            machine->floor_surface_flags,
            (unsigned)machine->rail_collision_timer,
            severe_contact ? 1u : 0u);
        for (corner_index = 0u; corner_index < 4u; ++corner_index) {
          fprintf(
              stderr,
              "frametrace|frame=%u|stage=pre_contact_corner|corner=%zu|"
              "collision=(%.3f,%.3f,%.3f)|offset=(%.3f,%.3f,%.3f)\n",
              world->frame_index,
              corner_index,
              machine->wall_corners[corner_index].collision.x,
              machine->wall_corners[corner_index].collision.y,
              machine->wall_corners[corner_index].collision.z,
              machine->wall_corners[corner_index].offset.x,
              machine->wall_corners[corner_index].offset.y,
              machine->wall_corners[corner_index].offset.z);
        }
      }
      fzgx_apply_machine_world_contact_response_exact(machine, severe_contact);
      if (trace_mcsg_contact) {
        fprintf(
            stderr,
            "frametrace|frame=%u|stage=post_contact_response|pos=(%.3f,%.3f,%.3f)|"
            "vel=(%.3f,%.3f,%.3f)|ang=(%.3f,%.3f,%.3f)|"
            "push=(%.3f,%.3f,%.3f)|surface=0x%08x|rail=%u\n",
            world->frame_index,
            machine->position.x,
            machine->position.y,
            machine->position.z,
            machine->velocity.x,
            machine->velocity.y,
            machine->velocity.z,
            machine->angular_velocity.x,
            machine->angular_velocity.y,
            machine->angular_velocity.z,
            machine->collision_push_total.x,
            machine->collision_push_total.y,
            machine->collision_push_total.z,
            machine->floor_surface_flags,
            (unsigned)machine->rail_collision_timer);
      }
    }
  }
  for (i = 0u; i < world->machine_count; ++i) {
    fzgx_machine_snapshot *machine = &world->machines[i];
    bool trace_fire_field_kill_window =
        (track_manifest != 0) &&
        (track_manifest->authored_track_id == 17u) &&
        (i == 0u) &&
        (2248u <= world->frame_index) &&
        (world->frame_index <= 2256u);
    bool trace_stage_frame =
        ((i == 0u) &&
         (world->frame_index < 5u)) ||
        trace_fire_field_kill_window;
    bool trace_posold_window =
        (i == 0u) &&
        (842u <= world->frame_index) &&
        (world->frame_index <= 860u);
    bool trace_visual_frame =
        (i == 0u) &&
        fzgx_debug_trace_visual_basis_window_exact(world->frame_index);

    if (trace_stage_frame) {
      fprintf(
          stderr,
          "frametrace|frame=%u|stage=pre_damage_state|pos=(%.3f,%.3f,%.3f)|"
          "vel=(%.3f,%.3f,%.3f)|state=0x%08x|state2=0x%08x|terrain=0x%08x|"
          "energy=%.6f|frames_until_restored=%u|post_restore=%u\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->machine_state,
          machine->state_2,
          machine->terrain_flags,
          machine->energy,
          (unsigned)machine->frames_until_restored,
          (unsigned)machine->post_restore_frame_countdown);
    }
    if (trace_posold_window) {
      size_t corner_index;

      for (corner_index = 0u; corner_index < 4u; ++corner_index) {
        const fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[corner_index];

        fprintf(
            stderr,
            "posold|frame=%u|stage=pre_damage|corner=%zu|state=0x%02x|force=%.6f|"
            "pos=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)\n",
            world->frame_index,
            corner_index,
            corner->state,
            corner->force,
            corner->pos.x,
            corner->pos.y,
            corner->pos.z,
            corner->pos_old.x,
            corner->pos_old.y,
            corner->pos_old.z);
      }
    }
    fzgx_handle_machine_damage_and_state_generic_exact(track_course, machine, world->frame_index);
    if (trace_stage_frame) {
      fprintf(
          stderr,
          "frametrace|frame=%u|stage=post_damage_state|pos=(%.3f,%.3f,%.3f)|"
          "vel=(%.3f,%.3f,%.3f)|surface=0x%08x|terrain=0x%08x|state=0x%08x|state2=0x%08x|"
          "energy=%.6f\n",
          world->frame_index,
          machine->position.x,
          machine->position.y,
          machine->position.z,
          machine->velocity.x,
          machine->velocity.y,
          machine->velocity.z,
          machine->floor_surface_flags,
          machine->terrain_flags,
          machine->machine_state,
          machine->state_2,
          machine->energy);
    }
    if (trace_posold_window) {
      size_t corner_index;

      for (corner_index = 0u; corner_index < 4u; ++corner_index) {
        const fzgx_machine_tilt_corner_snapshot *corner = &machine->suspension_corners[corner_index];

        fprintf(
            stderr,
            "posold|frame=%u|stage=post_damage|corner=%zu|state=0x%02x|force=%.6f|"
            "pos=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)\n",
            world->frame_index,
            corner_index,
            corner->state,
            corner->force,
            corner->pos.x,
            corner->pos.y,
            corner->pos.z,
            corner->pos_old.x,
            corner->pos_old.y,
            corner->pos_old.z);
      }
    }
    if (trace_visual_frame) {
      fzgx_debug_log_mat43_basis_exact(
          "visualtrace",
          "frame_phase_post_visual",
          world->frame_index,
          i,
          &machine->transform_visual,
          machine);
    }
  }
  for (i = 0u; i < world->machine_count; ++i) {
    fzgx_machine_snapshot *machine = &world->machines[i];

    if (have_track_course) {
      status = fzgx_manage_checkpoints_and_times_exact(
          world, i, machine, FZGX_RACETRACK_REFRESH_NORMAL, lap_timer_gate_active);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((track_manifest != 0) &&
          fzgx_debug_trace_double_branches_window_exact(
              track_manifest->authored_track_id, i, world->frame_index)) {
        fprintf(
            stderr,
            "frametrace|frame=%u|stage=post_track_refresh|pos=(%.3f,%.3f,%.3f)|"
            "cp=%d|cpf=%.6f|cur_cp=%d|cur_cpf=%.6f|next_cp=%d|next_cpf=%.6f|"
            "sel=%d|cached=%u|cp_count=%d|track_flags=0x%08x|"
            "track_up=(%.3f,%.3f,%.3f)|track_fwd=(%.3f,%.3f,%.3f)|"
            "basis_y=(%.3f,%.3f,%.3f)|basis_z=(%.3f,%.3f,%.3f)\n",
            world->frame_index,
            machine->position.x,
            machine->position.y,
            machine->position.z,
              machine->current_checkpoint,
              machine->checkpoint_fraction,
              machine->track_state.cur_cp_idx,
              machine->track_state.cur_cp_frac,
              machine->track_state.next_cp_idx,
            machine->track_state.next_cp_frac,
            machine->track_state.selected_cached_frame_index,
            machine->track_state.cached_frame_count,
            machine->track_state.checkpoint_variant_count,
            machine->track_state.flags,
            machine->track_state.track_up.x,
            machine->track_state.track_up.y,
            machine->track_state.track_up.z,
            machine->track_state.track_forward.x,
            machine->track_state.track_forward.y,
            machine->track_state.track_forward.z,
            machine->basis_physical.basis_y_x,
            machine->basis_physical.basis_y_y,
            machine->basis_physical.basis_y_z,
            machine->basis_physical.basis_z_x,
            machine->basis_physical.basis_z_y,
            machine->basis_physical.basis_z_z);
      }
    } else if (lap_timer_gate_active &&
               ((machine->machine_state & FZGX_MS_COMPLETEDRACE_1_Q) == 0u) &&
               (machine->track_state.lap_time_frames < 360000)) {
      machine->track_state.lap_time_frames += 1;
      fzgx_update_machine_race_time_displays_exact(&machine->track_state);
    }
  }
  if (apply_full_heal_exact) {
    for (i = 0u; i < world->machine_count; ++i) {
      world->machines[i].energy = 100.0f;
    }
  }
  if (!apply_full_heal_exact && race_context.raw_frame_state_live &&
      ((race_context.raw_frame_flags & 0x40u) == 0u)) {
    for (i = 0u; i < world->machine_count; ++i) {
      fzgx_machine_snapshot *machine = &world->machines[i];
      int32_t lap_start_cp = machine->track_state.lap_start_cp;

      if ((machine->machine_state & (FZGX_MS_RETIRED | FZGX_MS_COMPLETEDRACE_1_Q)) != 0u) {
        continue;
      }
      if ((lap_start_cp >= 0) &&
          ((uint32_t)lap_start_cp >= (uint32_t)race_context.finish_score_lap_threshold)) {
        status = fzgx_sim_finalize_machine_finish_score(world, i);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }
  }
  if (track_manifest != 0) {
    for (i = 0u; i < world->machine_count; ++i) {
      fzgx_recompute_machine_track_derived_metrics(&world->machines[i], track_manifest);
    }
  }
  fzgx_update_machine_ranks_exact(world, race_context.retired_visible_for_ranking);
  world->race_full_heal_latch_active = world->race_full_heal_latch_persistent;
  world->frame_index += 1u;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_sim_step(
    fzgx_sim_world *world,
    const fzgx_control_sample *controls,
    size_t control_count,
    const fzgx_race_step_options *options) {
  fzgx_status status = fzgx_sim_step_begin(world, controls, control_count);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (fzgx_world_should_defer_prestart_step_exact(world)) {
    return FZGX_STATUS_OK;
  }
  status = fzgx_sim_step_machine_phase(world);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_sim_step_frame_phase(world, options);
}
