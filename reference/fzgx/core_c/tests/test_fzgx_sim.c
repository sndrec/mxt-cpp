#include "fzgx/sim.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FZGX_REPO_ROOT
#define FZGX_REPO_ROOT "."
#endif

static const fzgx_track_manifest TEST_TRACKS[] = {
    {8u, 116u, 3u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 1u, {0u, 0u, 0u}},
    {38u, 200u, 1u, FZGX_CIRCUIT_TYPE_OPEN, 0u, 0u, {0u, 0u, 0u}},
    {15u, 116u, 3u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 1u, {0u, 0u, 0u}},
    {36u, 116u, 3u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 1u, {0u, 0u, 0u}},
};

static const fzgx_machine_definition TEST_MACHINES[] = {
    {
        .machine_id = 0u,
        .weight = 1620.0f,
        .acceleration = 0.45f,
        .max_speed = 0.1f,
        .grip_1 = 1.0f,
        .grip_3 = 0.2f,
        .turn_tension = 0.12f,
        .drift_accel = 0.4f,
        .turn_movement = 145.0f,
        .strafe_turn = 20.0f,
        .strafe = 35.0f,
        .turn_reaction = 10.0f,
        .grip_2 = 0.7f,
        .boost_strength = 14.0f,
        .boost_length = 1.5f,
        .turn_decel = 0.02f,
        .drag = 0.01f,
        .body = 1.1f,
        .grip_frames_from_accel_press = 1u,
        .state_flags = 2u,
        .reserved_stat_0x4a = 0u,
        .reserved_stat_0x4b = 0u,
        .camera_reorienting = 1.0f,
        .camera_repositioning = 1.0f,
        .suspension_offsets =
            {
                {0.8f, 0.0f, -1.5f},
                {-0.8f, 0.0f, -1.5f},
                {1.1f, 0.0f, 1.7f},
                {-1.1f, 0.0f, 1.7f},
            },
        .wall_offsets =
            {
                {1.0f, -0.1f, -1.7f},
                {-1.0f, -0.1f, -1.7f},
                {1.3f, -0.1f, 1.9f},
                {-1.3f, -0.1f, 1.9f},
            },
        .is_custom_machine = 0u,
        .reserved = {0u, 0u, 0u},
        .name = "Blue Falcon",
    },
};

static fzgx_content_bundle make_test_bundle(void) {
  fzgx_content_bundle bundle;
  memset(&bundle, 0, sizeof(bundle));
  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 4u;
  bundle.machine_count = 1u;
  bundle.tracks = TEST_TRACKS;
  bundle.machines = TEST_MACHINES;
  return bundle;
}

static void test_build_builtin_coli_course_path(
    char *path_out,
    size_t path_capacity,
    unsigned course_id) {
  assert(path_out != 0);
  assert(path_capacity != 0u);
  assert(
      snprintf(
          path_out,
          path_capacity,
          "%s/fzgx-iso/files/stage/COLI_COURSE%02u.lz",
          FZGX_REPO_ROOT,
          course_id) > 0);
}

static uint32_t test_find_builtin_track_index_by_authored_id(
    const fzgx_content_bundle *bundle,
    uint32_t authored_track_id) {
  uint32_t i;

  assert(bundle != 0);
  for (i = 0u; i < bundle->track_count; ++i) {
    if (bundle->tracks[i].authored_track_id == authored_track_id) {
      return i;
    }
  }
  assert(!"builtin track id not found");
  return 0u;
}

static uint32_t test_find_builtin_machine_index_by_name(
    const fzgx_content_bundle *bundle,
    const char *name) {
  uint32_t i;

  assert(bundle != 0);
  assert(name != 0);
  for (i = 0u; i < bundle->machine_count; ++i) {
    if (strcmp(bundle->machines[i].name, name) == 0) {
      return i;
    }
  }
  assert(!"builtin machine name not found");
  return 0u;
}

static fzgx_vec3 test_vec3_sub(fzgx_vec3 a, fzgx_vec3 b) {
  return (fzgx_vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static fzgx_vec3 test_vec3_cross(fzgx_vec3 a, fzgx_vec3 b) {
  return (fzgx_vec3){
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

static void test_init_static_collider_quad(
    fzgx_static_collider_quad_record *quad,
    fzgx_vec3 vertex0,
    fzgx_vec3 vertex1,
    fzgx_vec3 vertex2,
    fzgx_vec3 vertex3,
    fzgx_vec3 normal) {
  fzgx_vec3 center = {
      0.25f * (vertex0.x + vertex1.x + vertex2.x + vertex3.x),
      0.25f * (vertex0.y + vertex1.y + vertex2.y + vertex3.y),
      0.25f * (vertex0.z + vertex1.z + vertex2.z + vertex3.z),
  };
  fzgx_vec3 edges[4];
  fzgx_vec3 deltas[4];

  quad->plane_distance = -(normal.x * vertex0.x + normal.y * vertex0.y + normal.z * vertex0.z);
  quad->normal = normal;
  quad->vertex0 = vertex0;
  quad->vertex1 = vertex1;
  quad->vertex2 = vertex2;
  quad->vertex3 = vertex3;
  edges[0] = test_vec3_cross(test_vec3_sub(vertex1, vertex0), normal);
  edges[1] = test_vec3_cross(test_vec3_sub(vertex2, vertex1), normal);
  edges[2] = test_vec3_cross(test_vec3_sub(vertex3, vertex2), normal);
  edges[3] = test_vec3_cross(test_vec3_sub(vertex0, vertex3), normal);
  deltas[0] = test_vec3_sub(center, vertex0);
  deltas[1] = test_vec3_sub(center, vertex1);
  deltas[2] = test_vec3_sub(center, vertex2);
  deltas[3] = test_vec3_sub(center, vertex3);
  if ((deltas[0].x * edges[0].x + deltas[0].y * edges[0].y + deltas[0].z * edges[0].z) < 0.0f) {
    edges[0].x = -edges[0].x;
    edges[0].y = -edges[0].y;
    edges[0].z = -edges[0].z;
    edges[1].x = -edges[1].x;
    edges[1].y = -edges[1].y;
    edges[1].z = -edges[1].z;
    edges[2].x = -edges[2].x;
    edges[2].y = -edges[2].y;
    edges[2].z = -edges[2].z;
    edges[3].x = -edges[3].x;
    edges[3].y = -edges[3].y;
    edges[3].z = -edges[3].z;
  }
  quad->edge_normal0 = edges[0];
  quad->edge_normal1 = edges[1];
  quad->edge_normal2 = edges[2];
  quad->edge_normal3 = edges[3];
}

typedef struct test_spherecast_callback_state {
  fzgx_status status;
  uint32_t call_count;
  fzgx_world_spherecast_request requests[16];
  uint32_t request_count;
  fzgx_world_spherecast_request first_request;
  fzgx_world_spherecast_request last_request;
  fzgx_world_spherecast_result result;
  fzgx_world_spherecast_result results[8];
  uint32_t result_count;
} test_spherecast_callback_state;

static fzgx_status test_spherecast_callback(
    void *userdata,
    const fzgx_world_spherecast_request *request,
    fzgx_world_spherecast_result *result) {
  test_spherecast_callback_state *state = (test_spherecast_callback_state *)userdata;
  if (state->call_count == 0u) {
    state->first_request = *request;
  }
  if (state->request_count < 16u) {
    state->requests[state->request_count] = *request;
    state->request_count += 1u;
  }
  if (state->call_count < state->result_count) {
    *result = state->results[state->call_count];
  } else {
    *result = state->result;
  }
  state->call_count += 1u;
  state->last_request = *request;
  return state->status;
}

static float test_shared_checkpoint_distance(
    const fzgx_track_course_content *course,
    int32_t checkpoint_index,
    float checkpoint_fraction) {
  const fzgx_track_node_record *track_node = 0;
  const fzgx_checkpoint_record *checkpoint;

  assert(
      fzgx_track_course_get_track_node(course, (uint32_t)checkpoint_index, &track_node) ==
      FZGX_STATUS_OK);
  assert(track_node->checkpoint_count != 0u);
  checkpoint = &course->checkpoints[track_node->checkpoint_offset];
  return checkpoint->start_distance +
         checkpoint_fraction * (checkpoint->end_distance - checkpoint->start_distance);
}

static void test_configure_and_seed_machine(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 4u;

  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(world.machines[0].machine_id == 0u);
  assert(world.machines[0].entrant_runtime_flags == 0u);
  assert(world.machines[0].basis_physical.basis_x_x == 1.0f);
  assert(world.machines[0].basis_physical.basis_y_y == 1.0f);
  assert(world.machines[0].basis_physical.basis_z_z == 1.0f);
  assert(world.machines[0].basis_physical.origin.x == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].velocity.y == 0.0f);
  assert(world.machines[0].velocity.z == 0.0f);
  assert(world.machines[0].angular_velocity.x == 0.0f);
  assert(world.machines[0].angular_velocity.y == 0.0f);
  assert(world.machines[0].angular_velocity.z == 0.0f);
  assert(world.machines[0].surface_normal.x == 0.0f);
  assert(world.machines[0].surface_normal.y == 1.0f);
  assert(world.machines[0].surface_normal.z == 0.0f);
  assert(world.machines[0].current_checkpoint == 0);
  assert(world.machines[0].checkpoint_fraction == 0.0f);
  assert(world.machines[0].score == 0u);
  assert(world.machines[0].energy == 100.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].speed_kmh == 0.0f);
  assert(world.machines[0].max_speed_kmh == 0.0f);
  assert(world.machines[0].collision_push_total.x == 0.0f);
  assert(world.machines[0].collision_response.x == 0.0f);
  assert(world.machines[0].damage_from_last_hit == 0.0f);
  assert(world.machines[0].machine_state == 0x80000400u);
  assert(world.machines[0].state_2 == 0u);
  assert(world.machines[0].wall_hit_count == 0u);
  assert(world.machines[0].boost_count == 0u);
  assert(world.machines[0].dash_plate_hit_count == 0u);
  assert(world.machines[0].boost_frames == 0u);
  assert(world.machines[0].boost_frames_manual == 0u);
  assert(world.machines[0].air_tilt == 0.0f);
  assert(world.machines[0].visual_shake_mult == 0.0f);
  assert(world.machines[0].height_adjust_from_boost == 0.0f);
  assert(world.machines[0].unk_float_0x208 == 200.0f);
  assert(world.machines[0].frames_since_death == 0u);
  assert(world.machines[0].g_unk_breakdown_int == 0u);
  assert(world.machines[0].rail_collision_timer == 0u);
  assert(world.machines[0].suspension_reset_flag == 0u);
  assert(world.machines[0].frames_until_restored == 0u);
  assert(world.machines[0].unk_random_0x514 == 0xffffffffu);
  assert(world.machines[0].restore_progress == 0.0f);
  assert(world.machines[0].unk_restore_0x51c == 0.0f);
  assert(world.machines[0].restore_count == 0u);
  assert(world.machines[0].post_restore_frame_countdown == 0u);
  assert(world.machines[0].strafe_visual_roll_angle == 0);
  assert(world.machines[0].turn_reaction_effect == 0.0f);
  assert(world.machines[0].unk_stat_0x5d4 == 0.0f);
  assert(world.machines[0].machine_crashed == false);
  assert(world.machines[0].terrain_flags == 0u);
  assert(world.machines[0].branch_indicator == 0u);
  assert(world.machines[0].branch_flags == 0u);
  assert(world.machines[0].branch_slot == 4u);
  assert(world.machines[0].control_profile_kind == 0u);
  assert(world.machines[0].frames_since_start == 0u);
  assert(world.machines[0].stat_acceleration == 0.45f);
  assert(world.machines[0].stat_max_speed == 0.1f);
  assert(world.machines[0].stat_turn_movement == 145.0f);
  assert(world.machines[0].stat_strafe_turn == 20.0f);
  assert(world.machines[0].stat_strafe == 35.0f);
  assert(world.machines[0].stat_grip_2 == 0.7f);
  assert(world.machines[0].stat_grip_3 == 0.2f);
  assert(fabsf(world.machines[0].weight_derived_1 - 5265.0f) < 0.0001f);
  assert(fabsf(world.machines[0].weight_derived_2 - 4556.25f) < 0.0001f);
  assert(fabsf(world.machines[0].weight_derived_3 - 5265.0f) < 0.0001f);
  assert(fabsf(world.machines[0].stat_boost_strength - 7.98f) < 0.0001f);
  assert(fabsf(world.machines[0].stat_obstacle_collision - 2.404344f) < 0.0001f);
  assert(fabsf(world.machines[0].stat_track_collision - 1.3f) < 0.0001f);
  assert(world.machines[0].boost_energy_use_mult == 1.0f);
  assert(world.machines[0].const_float_2_0 == 2.0f);
  assert(world.machines[0].velocity_local.x == 0.0f);
  assert(world.machines[0].velocity_local.y == 0.0f);
  assert(world.machines[0].velocity_local.z == 0.0f);
  assert(world.machines[0].suspension_state[0] == 0u);
  assert(world.machines[0].suspension_state[1] == 0u);
  assert(world.machines[0].suspension_state[2] == 0u);
  assert(world.machines[0].suspension_state[3] == 0u);
  assert(world.machines[0].last_machine_approached == 0xffu);
  assert(fabsf(world.machines[0].suspension_corners[0].offset.x - 0.8f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[3].offset.z - 1.7f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[2].offset.x - 1.3f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[1].offset.y - -0.1f) < 0.0001f);
  assert(world.machines[0].track_state.stable_cp_idx[0] == -1);
  assert(world.machines[0].track_state.stable_cp_idx[1] == 0);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == -1);
  assert(world.machines[0].track_state.lap_cross_cp == -1);
  assert(world.machines[0].track_state.lap_split_gate_mask == -1);
  assert(world.machines[0].track_state.lap_time_display == 0u);
  assert(world.machines[0].track_state.total_time_display == 0u);
  assert(world.machines[0].track_state.history_time_display == 0u);
  assert(world.machines[0].track_state.lap_min == 0u);
  assert(world.machines[0].track_state.lap_sec == 0u);
  assert(world.machines[0].track_state.lap_centi == 0u);
  assert(world.machines[0].track_state.total_min == 0u);
  assert(world.machines[0].track_state.total_sec == 0u);
  assert(world.machines[0].track_state.total_centi == 0u);
  assert(world.machines[0].track_state.history_min == 0u);
  assert(world.machines[0].track_state.history_sec == 0u);
  assert(world.machines[0].track_state.history_centi == 0u);
  assert(world.machines[0].track_state.best_splits[0].frames == 0);
  assert(world.machines[0].track_state.best_lap.frames == 0);
  assert(world.machines[0].track_state.best_lap_slot == 0u);
  assert(world.machines[0].track_state.cur_cp_idx == -1);
  assert(world.machines[0].track_state.selected_cached_frame_index == 0);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_ACTIVE) != 0u);
}

static void test_step_advances_frame_and_stages_controls(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  fzgx_race_step_options options = {true, true, false, true};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].energy = 25.0f;
  world.machines[0].frames_since_start_2 = 0xffu;
  control.accel = 1.0f;
  control.control_profile_kind = 2u;
  control.buttons = FZGX_INPUT_BUTTON_SIDE_ATTACK;
  control.steer_yaw = 0.25f;

  assert(fzgx_sim_step(&world, &control, 1u, &options) == FZGX_STATUS_OK);
  assert(world.frame_index == 1u);
  assert(world.controls[0].accel == 1.0f);
  assert(world.controls[0].steer_yaw == 0.25f);
  assert(world.controls[0].control_profile_kind == 2u);
  assert(world.machines[0].input_accel == 1.0f);
  assert(fabsf(world.machines[0].input_steer_yaw - 0.0625f) < 0.0001f);
  assert(fabsf(world.machines[0].input_yaw_dupe - 0.0625f) < 0.0001f);
  assert(world.machines[0].control_profile_kind == 2u);
  assert((world.machines[0].state_2 & 8u) != 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_SIM_MOTION_RAN) != 0u);
  assert(world.machines[0].energy == world.machines[0].max_energy);
  assert(world.machines[0].base_speed > 0.0f);
  assert(world.machines[0].track_state.lap_time_frames == 1);
  assert(world.machines[0].track_state.rank_this_frame == 0u);
}

static void test_step_machine_phase_steering_accounts_for_suspension_scale(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;
  float unclamped_turn_y;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  world.machines[0].frames_since_start_2 = 0xffu;

  control.steer_yaw = 0.5f;
  control.strafe = 1.0f;
  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  unclamped_turn_y = world.machines[0].angular_velocity.y;
  assert(unclamped_turn_y != 0.0f);

  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  world.machines[0].frames_since_start_2 = 0xffu;
  world.machines[0].suspension_corners[0].state = 4u;
  world.machines[0].suspension_state[0] = 4u;
  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert(fabsf(world.machines[0].angular_velocity.y) < fabsf(unclamped_turn_y));
  assert((world.machines[0].suspension_corners[1].state & 4u) != 0u);
  assert((world.machines[0].suspension_corners[2].state & 4u) != 0u);
  assert((world.machines[0].suspension_corners[3].state & 4u) != 0u);
}

static void test_step_machine_phase_applies_drive_force_and_strafing_state(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  world.machines[0].frames_since_start_2 = 1u;

  control.accel = 1.0f;
  control.strafe = 0.5f;
  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_state & 0x00004000u) != 0u);
  assert((world.machines[0].suspension_state[0] & 0x10u) != 0u);
  assert((world.machines[0].suspension_state[1] & 0x10u) != 0u);
  assert((world.machines[0].suspension_state[2] & 0x10u) != 0u);
  assert((world.machines[0].suspension_state[3] & 0x10u) != 0u);
  assert(fabsf(world.machines[0].input_strafe_32 - 4.0f) < 0.0001f);
  assert(world.machines[0].base_speed > 0.0f);
  assert(
      (fabsf(world.machines[0].velocity.x) > 0.0f) ||
      (fabsf(world.machines[0].velocity.y) > 0.0f) ||
      (fabsf(world.machines[0].velocity.z) > 0.0f));
}

static void test_step_machine_phase_refreshes_speed_and_local_velocity_views(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  world.machines[0].frames_since_start_2 = 1u;

  world.machines[0].velocity.z = -1620.0f;
  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].speed_kmh - 216.0f) < 0.0001f);
  assert(fabsf(world.machines[0].velocity_local.x - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].velocity_local.y - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].velocity_local.z - -1620.0f) < 0.0001f);
  assert(fabsf(world.machines[0].velocity_local_flattened_and_rotated.y - 0.0f) < 0.0001f);
  assert(world.machines[0].velocity.z > -1620.0f);
  assert(world.machines[0].position.z < 0.0f);
}

static void test_step_machine_phase_clears_side_attack_below_speed_gate(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  world.machines[0].frames_since_start_2 = 1u;

  world.machines[0].velocity.z = -1000.0f;
  world.machines[0].machine_state |= 0x00020000u;
  world.machines[0].side_attack_delay = 3u;

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_state & 0x00020000u) == 0u);
  assert(world.machines[0].side_attack_delay == 0u);
}

static void test_step_machine_phase_clamps_angular_velocity_from_weight(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].frames_since_start_2 = 0xffu;
  world.machines[0].angular_velocity.y = 500.0f;

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].angular_velocity.y - 227.8125f) < 0.0001f);
  assert(fabsf(world.machines[0].basis_physical.basis_x_x - 1.0f) > 0.0001f);
}

static void test_step_machine_phase_applies_turn_and_strafe_corner_force(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].position_old = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].frames_since_start_2 = 2u;

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(775.0f < world.machines[0].velocity.x);
  assert(world.machines[0].velocity.x < 776.0f);
  assert(fabsf(world.machines[0].turning_related - 777.6f) < 0.01f);
  assert(fabsf(world.machines[0].angular_velocity.y - 77.76f) < 0.01f);
}

static void test_step_machine_phase_prepare_prefix_clears_countdown_inputs(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state |= 0x00008000u | 0x00020000u | 0x00000008u | 0x00000040u;
  world.machines[0].input_steer_yaw = 0.75f;
  world.machines[0].input_steer_pitch = 0.5f;
  world.machines[0].input_brake = 1.0f;
  world.machines[0].input_strafe = 1.0f;
  world.machines[0].frames_since_start_2 = 5u;
  world.machines[0].visual_roll = 10.0f;
  world.machines[0].visual_pitch = 20.0f;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].input_steer_yaw == 0.0f);
  assert(world.machines[0].input_steer_pitch == 0.0f);
  assert(world.machines[0].input_brake == 0.0f);
  assert(world.machines[0].input_strafe == 0.0f);
  assert((world.machines[0].machine_state & 0x00020000u) == 0u);
  assert((world.machines[0].machine_state & 0x00000008u) == 0u);
  assert((world.machines[0].machine_state & 0x00000040u) == 0u);
  assert(world.machines[0].frames_since_start_2 == 4u);
  assert(fabsf(world.machines[0].visual_roll - 8.0f) < 0.0001f);
  assert(world.machines[0].visual_pitch < 20.0f);
}

static void test_step_machine_phase_prepare_prefix_snapshots_old_positions(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position.x = 10.0f;
  world.machines[0].position.y = 20.0f;
  world.machines[0].position.z = 30.0f;
  world.machines[0].position_old.x = 1.0f;
  world.machines[0].position_old.y = 2.0f;
  world.machines[0].position_old.z = 3.0f;
  world.machines[0].velocity.z = -1620.0f;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].position_old.x - 10.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position_old.y - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position_old.z - 30.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position_old_dupe.x - 10.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position_old_2.x - 10.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_a.x - 10.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_a.y - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_a.z - 30.0f) < 0.0001f);
  assert(world.machines[0].frames_since_start == 1u);
}

static void test_step_machine_phase_sets_terrain_state_from_spherecast_callback(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result.result_flags = 0xfau;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){10.0f, 20.0f, 30.0f};
  world.machines[0].position_old = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 3u);
  assert(callback_state.first_request.flags == 0xfau);
  assert(callback_state.first_request.machine_index == 0u);
  assert(fabsf(callback_state.first_request.start.x - 10.0f) < 0.0001f);
  assert(fabsf(callback_state.first_request.end.z - 3.0f) < 0.0001f);
  assert((world.machines[0].machine_state & 0x00400020u) == 0x00400020u);
  assert((world.machines[0].state_2 & 1u) != 0u);
  assert((world.machines[0].terrain_flags & 0x10000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x08000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x20000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x02000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x04000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x02000000u) != 0u);
}

static void test_step_machine_phase_sets_terrain_state_from_native_static_collider_course(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  char path[256];
  uint32_t track_index;

  test_build_builtin_coli_course_path(path, sizeof(path), 8u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &static_course) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  track_index = test_find_builtin_track_index_by_authored_id(bundle, 8u);
  assert(fzgx_sim_world_set_track(&world, track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){-99.72320365905762f, 2.1652615070343018f, -898.5908813476562f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].machine_state &= ~0x00000002u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_state & 0x00400020u) == 0x00400020u);
  assert((world.machines[0].terrain_flags & 0x10000000u) != 0u);

  world.frame_index = 0u;
  world.machines[0].terrain_flags = 0u;
  world.machines[0].machine_state &= ~(0x00400020u);
  world.machines[0].position =
      (fzgx_vec3){-117.50794474283855f, 0.2092533359924952f, -1665.4914143880208f};
  world.machines[0].position_old = world.machines[0].position;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert((world.machines[0].terrain_flags & 0x20000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x10000000u) == 0u);

  fzgx_content_release_static_collider_course(&static_course);
}

static void test_world_binds_native_track_mesh_course_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_track_mesh_course track_mesh_course = {0};
  char path[256];

  test_build_builtin_coli_course_path(path, sizeof(path), 8u);
  assert(fzgx_content_load_track_mesh_course_from_path(path, &track_mesh_course) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track_mesh_course(&world, &track_mesh_course) == FZGX_STATUS_OK);
  assert(world.track_mesh_course == &track_mesh_course);
  assert(world.track_mesh_course->chunk_count != 0u);
  assert(world.track_mesh_course->class_count == FZGX_TRACK_MESH_CLASS_COUNT_GX);
  assert(world.track_mesh_course->chunks[0].tri_count != 0u);
  assert(world.track_mesh_course->chunks[0].quad_count != 0u);

  fzgx_content_release_track_mesh_course(&track_mesh_course);
}

static void test_step_machine_phase_updates_floor_and_suspension_from_native_track_mesh_course_exact(
    void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_track_mesh_course track_mesh_course = {0};
  fzgx_owned_track_mesh_chunk *chunk;
  size_t cached_frame_corner_count = 0u;
  size_t i;
  const uint32_t class_count = FZGX_TRACK_MESH_CLASS_COUNT_AX;

  track_mesh_course.class_count = class_count;
  track_mesh_course.chunk_count = 1u;
  track_mesh_course.chunks =
      (fzgx_owned_track_mesh_chunk *)calloc(track_mesh_course.chunk_count, sizeof(*track_mesh_course.chunks));
  assert(track_mesh_course.chunks != 0);
  chunk = &track_mesh_course.chunks[0];
  chunk->class_count = class_count;
  chunk->cell_count = 1u;
  chunk->grid_origin_x = -50.0f;
  chunk->grid_origin_z = -50.0f;
  chunk->inv_cell_size_x = 100.0f;
  chunk->inv_cell_size_z = 100.0f;
  chunk->grid_subdiv_x = 1;
  chunk->grid_subdiv_z = 1;
  chunk->quad_count = 1u;
  chunk->quad_index_count = 1u;
  chunk->quads = (fzgx_static_collider_quad_record *)calloc(chunk->quad_count, sizeof(*chunk->quads));
  chunk->quad_indices = (uint16_t *)calloc(chunk->quad_index_count, sizeof(*chunk->quad_indices));
  chunk->quad_cells = (fzgx_static_collider_index_span *)calloc(
      class_count * chunk->cell_count, sizeof(*chunk->quad_cells));
  assert(chunk->quads != 0);
  assert(chunk->quad_indices != 0);
  assert(chunk->quad_cells != 0);
  test_init_static_collider_quad(
      &chunk->quads[0],
      (fzgx_vec3){-20.0f, 0.0f, -20.0f},
      (fzgx_vec3){20.0f, 0.0f, -20.0f},
      (fzgx_vec3){20.0f, 0.0f, 20.0f},
      (fzgx_vec3){-20.0f, 0.0f, 20.0f},
      (fzgx_vec3){0.0f, 1.0f, 0.0f});
  chunk->quad_indices[0] = 0u;
  chunk->quad_cells[0].offset = 0u;
  chunk->quad_cells[0].count = 1u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track_mesh_course(&world, &track_mesh_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].basis_physical.basis_x = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_y = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical.basis_z = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  world.machines[0].position = (fzgx_vec3){0.0f, 10.0f, 0.0f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].frames_since_start_2 = 90u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].zero_minus_height_above_track > 0.0f);
  assert(world.machines[0].position_bottom.y <= world.machines[0].position.y);
  assert(world.machines[0].surface_normal.y > 0.5f);
  assert((world.machines[0].terrain_flags & 0x02000000u) == 0u);
  for (i = 0u; i < 4u; ++i) {
    if (world.machines[0].suspension_corners[i].query_cached_frame_count != 0u) {
      cached_frame_corner_count += 1u;
    }
  }
  assert(cached_frame_corner_count == 0u);

  free(chunk->quad_cells);
  free(chunk->quad_indices);
  free(chunk->quads);
  free(track_mesh_course.chunks);
}

static void test_step_machine_phase_updates_floor_and_suspension_from_native_static_collider_course(
    void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  char path[256];
  uint32_t track_index;
  size_t cached_frame_corner_count = 0u;
  size_t i;

  test_build_builtin_coli_course_path(path, sizeof(path), 8u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &static_course) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  track_index = test_find_builtin_track_index_by_authored_id(bundle, 8u);
  assert(fzgx_sim_world_set_track(&world, track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].basis_physical.basis_x = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_y = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical.basis_z = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  world.machines[0].position = (fzgx_vec3){-99.72320365905762f, 2.1652615070343018f, -896.7908935546875f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].frames_since_start_2 = 90u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].zero_minus_height_above_track > 0.0f);
  assert(world.machines[0].position_bottom.y <= world.machines[0].position.y);
  assert(world.machines[0].surface_normal.y > 0.5f);
  assert((world.machines[0].terrain_flags & 0x02000000u) == 0u);
  for (i = 0u; i < 4u; ++i) {
    if (world.machines[0].suspension_corners[i].query_cached_frame_count != 0u) {
      cached_frame_corner_count += 1u;
    }
  }
  assert(cached_frame_corner_count != 0u);

  fzgx_content_release_static_collider_course(&static_course);
}

static void test_step_machine_phase_acquires_floor_from_start_grid_with_native_static_and_track_mesh_courses(
    void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  fzgx_owned_track_mesh_course track_mesh_course = {0};
  fzgx_vec3 start_position;
  size_t i;
  char path[256];
  uint32_t track_index;

  test_build_builtin_coli_course_path(path, sizeof(path), 8u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_content_load_track_mesh_course_from_path(path, &track_mesh_course) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track_mesh_course(&world, &track_mesh_course) == FZGX_STATUS_OK);
  track_index = test_find_builtin_track_index_by_authored_id(bundle, 8u);
  assert(fzgx_sim_world_set_track(&world, track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0f) ==
      FZGX_STATUS_OK);

  start_position = world.machines[0].position;
  world.machines[0].frames_since_start_2 = 0xffu;
  world.machines[0].machine_state |= 0x00000400u;
  world.machines[0].machine_state &= ~0x00008000u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_ACTIVE;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].zero_minus_height_above_track > 0.0f);
  assert(world.machines[0].position_bottom.y <= world.machines[0].position.y);
  assert(world.machines[0].surface_normal.y > 0.5f);
  assert((world.machines[0].terrain_flags & 0x02000000u) == 0u);
  assert(world.machines[0].current_checkpoint >= 0);
  assert(fabsf(world.machines[0].position.x - start_position.x) < 0.1f);
  assert(fabsf(world.machines[0].position.z - start_position.z) < 0.1f);
  for (i = 0u; i < 4u; ++i) {
    assert(world.machines[0].suspension_corners[i].query_cached_frame_count != 0u);
    assert((world.machines[0].suspension_corners[i].query_surface_flags & 0x02000000u) == 0u);
  }

  fzgx_content_release_track_mesh_course(&track_mesh_course);
  fzgx_content_release_static_collider_course(&static_course);
}

static void test_step_defers_inactive_start_grid_machine_until_activation(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  fzgx_owned_track_mesh_course track_mesh_course = {0};
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;
  fzgx_race_step_options options = {true, true, false, true};
  fzgx_vec3 start_position;
  char path[256];
  uint32_t track_index;

  test_build_builtin_coli_course_path(path, sizeof(path), 1u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_content_load_track_mesh_course_from_path(path, &track_mesh_course) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track_mesh_course(&world, &track_mesh_course) == FZGX_STATUS_OK);
  track_index = test_find_builtin_track_index_by_authored_id(bundle, 1u);
  assert(fzgx_sim_world_set_track(&world, track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0f) ==
      FZGX_STATUS_OK);

  start_position = world.machines[0].position;
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_ACTIVE) == 0u);

  assert(fzgx_sim_step(&world, &control, 1u, &options) == FZGX_STATUS_OK);

  assert(world.frame_index == 0u);
  assert((world.machines[0].state_2 & 8u) == 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_SIM_MOTION_RAN) == 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_ACTIVE) == 0u);
  assert(fabsf(world.machines[0].position.x - start_position.x) < 0.0001f);
  assert(fabsf(world.machines[0].position.y - start_position.y) < 0.0001f);
  assert(fabsf(world.machines[0].position.z - start_position.z) < 0.0001f);

  control.accel = 1.0f;
  assert(fzgx_sim_step(&world, &control, 1u, &options) == FZGX_STATUS_OK);

  assert(world.frame_index == 1u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_ACTIVE) != 0u);
  assert((world.machines[0].state_2 & 8u) != 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_SIM_MOTION_RAN) != 0u);
  assert(world.machines[0].zero_minus_height_above_track > 0.0f);
  assert(world.machines[0].position.y > (start_position.y - 0.5f));
  assert((world.machines[0].terrain_flags & 0x02000000u) == 0u);

  fzgx_content_release_track_mesh_course(&track_mesh_course);
  fzgx_content_release_static_collider_course(&static_course);
}

static void test_step_keeps_mute_city_machine_grounded_through_early_flat_road_transition(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  fzgx_owned_track_mesh_course track_mesh_course = {0};
  fzgx_owned_dynamic_scene_collision_course dynamic_course = {0};
  fzgx_race_step_options options = {true, true, false, true};
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;
  char path[256];
  uint32_t track_index;
  uint32_t machine_index;
  uint32_t frame;

  test_build_builtin_coli_course_path(path, sizeof(path), 1u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_content_load_track_mesh_course_from_path(path, &track_mesh_course) == FZGX_STATUS_OK);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &dynamic_course) ==
      FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track_mesh_course(&world, &track_mesh_course) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  track_index = test_find_builtin_track_index_by_authored_id(bundle, 1u);
  machine_index = test_find_builtin_machine_index_by_name(bundle, "Red Gazelle");
  assert(fzgx_sim_world_set_track(&world, track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, machine_index) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0f) ==
      FZGX_STATUS_OK);

  for (frame = 0u; frame < 154u; ++frame) {
    memset(&control, 0, sizeof(control));
    if (frame >= 1u) {
      control.accel = 1.0f;
    }
    assert(fzgx_sim_step(&world, &control, 1u, &options) == FZGX_STATUS_OK);
  }

  assert((world.machines[0].machine_state & 0x00000800u) == 0u);
  assert(world.machines[0].zero_minus_height_above_track > 0.0f);
  assert(world.machines[0].current_checkpoint >= 14);
  assert(world.machines[0].position_bottom.y <= world.machines[0].position.y);

  fzgx_content_release_dynamic_scene_collision_course(&dynamic_course);
  fzgx_content_release_track_mesh_course(&track_mesh_course);
  fzgx_content_release_static_collider_course(&static_course);
}

static void test_step_machine_phase_updates_suspension_force_from_spherecast_callback(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result.has_hit = true;
  callback_state.result.hit_time = 1.0f;
  callback_state.result.hit_point = (fzgx_vec3){-1.1f, 0.0f, 1.7f};
  callback_state.result.aux_hit_point = (fzgx_vec3){-1.1f, 0.0f, 1.7f};
  callback_state.result.hit_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].frames_since_start_2 = 90u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 6u);
  assert(callback_state.first_request.flags == 0x60005u);
  assert(callback_state.first_request.machine_index == 0u);
  assert(fabsf(callback_state.first_request.start.x - 0.8f) < 0.0001f);
  assert(fabsf(callback_state.first_request.start.y - -2000.0f) < 0.0001f);
  assert(fabsf(callback_state.first_request.start.z - -1.5f) < 0.0001f);
  assert(fabsf(callback_state.first_request.end.x - 0.8f) < 0.0001f);
  assert(fabsf(callback_state.first_request.end.y - 0.0f) < 0.0001f);
  assert(fabsf(callback_state.first_request.end.z - -1.5f) < 0.0001f);
  assert((world.machines[0].machine_state & 0x00000002u) == 0u);
  assert((world.machines[0].suspension_corners[0].state & 0x2u) == 0u);
  assert((world.machines[0].suspension_corners[3].state & 0x2u) == 0u);
  assert(fabsf(world.machines[0].suspension_corners[0].force - 1.7f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[3].force_spatial_len - 392.445f) < 0.001f);
  assert(fabsf(world.machines[0].suspension_corners[3].pos.x - -1.1f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[3].pos.y - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[3].up_vector.y - 1.0f) < 0.0001f);
}

static void test_step_machine_phase_builds_avg_track_normal_from_corner_sweeps(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result.has_hit = true;
  callback_state.result.hit_time = 1.0f;
  callback_state.result.aux_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  callback_state.result.hit_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  callback_state.result.surface_flags = 0x03e00000u;
  callback_state.result.selected_cached_frame_index = 2;
  callback_state.result.cached_frame_count = 3u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].frames_since_start_2 = 90u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 6u);
  assert(fabsf(world.machines[0].surface_normal.x - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].surface_normal.y - 1.0f) < 0.0001f);
  assert(fabsf(world.machines[0].surface_normal.z - 0.0f) < 0.0001f);
  assert(world.machines[0].branch_slot == 2u);
}

static void test_step_machine_phase_flattens_course_0x0f_special_case_corners(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  size_t i;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 6u;
  for (i = 0u; i < 4u; ++i) {
    callback_state.results[i].has_hit = true;
    callback_state.results[i].hit_time = 1.0f;
    callback_state.results[i].aux_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
    callback_state.results[i].hit_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
    callback_state.results[i].surface_flags = 0x03e00000u;
    callback_state.results[i].hit_info_flags = 0x200u;
    callback_state.results[i].selected_cached_frame_index = 1;
    callback_state.results[i].cached_frame_count = 3u;
  }

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].frames_since_start_2 = 90u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  for (i = 0u; i < 4u; ++i) {
    assert((world.machines[0].suspension_corners[i].state & 0x0au) == 0x0au);
    assert(fabsf(world.machines[0].suspension_corners[i].force) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].force_spatial_len) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].up_vector.x) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].up_vector.y - 1.0f) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].up_vector.z) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].up_vector_2.x) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].up_vector_2.y - 1.0f) < 0.0001f);
    assert(fabsf(world.machines[0].suspension_corners[i].up_vector_2.z) < 0.0001f);
  }
}

static void test_step_machine_phase_finds_floor_beneath_machine_generic_path(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result.has_hit = true;
  callback_state.result.hit_time = 0.995f;
  callback_state.result.hit_point = (fzgx_vec3){0.0f, -10.0f, 0.0f};
  callback_state.result.aux_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  callback_state.result.hit_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  callback_state.result.surface_flags = 0x00123456u;
  callback_state.result.material_flags = 0x00abcdefu;
  callback_state.result.checkpoint_index = 77;
  callback_state.result.checkpoint_fraction = 0.25f;
  callback_state.result.branch_flags = 0x13579bdfu;
  callback_state.result_count = 2u;
  callback_state.results[0] = callback_state.result;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state &= ~0x00000400u;
  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].track_state.cur_cp_idx = 10;
  world.machines[0].track_state.next_cp_idx = -1;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 2u);
  assert(callback_state.first_request.flags == 0x4260005u);
  assert(fabsf(callback_state.first_request.start.y - -2000.0f) < 0.0001f);
  assert(fabsf(callback_state.first_request.end.y - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].zero_minus_height_above_track - 10.0f) < 0.01f);
  assert(world.machines[0].current_checkpoint == 77);
  assert(fabsf(world.machines[0].checkpoint_fraction - 0.25f) < 0.0001f);
  assert(world.machines[0].branch_flags == 0x13579bdfu);
  assert(world.machines[0].floor_surface_flags == 0x00123456u);
  assert(world.machines[0].floor_material_flags == 0x00abcdefu);
  assert(world.machines[0].position_bottom.y < 0.0f);
}

static void test_step_machine_phase_passes_sonic_oval_floor_bias_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  bool found_bias_request = false;
  size_t i;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 6u;
  callback_state.result.has_hit = true;
  callback_state.result.hit_time = 0.995f;
  callback_state.result.hit_point = (fzgx_vec3){0.0f, -10.0f, 0.0f};
  callback_state.result.aux_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  callback_state.result.hit_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  callback_state.result.surface_flags = 0x00123456u;
  callback_state.result.material_flags = 0x00abcdefu;
  callback_state.result.checkpoint_index = 77;
  callback_state.result.checkpoint_fraction = 0.25f;
  callback_state.result.branch_flags = 0x13579bdfu;
  for (size_t i = 0u; i < 4u; ++i) {
    callback_state.results[i] = callback_state.result;
    callback_state.results[i].slot0_piece_opaque = 0u;
    callback_state.results[i].slot0_checkpoint_index = -1;
    callback_state.results[i].latest_slot0_flags = 0u;
    callback_state.results[i].alternate_slot0_flags = 0u;
  }
  callback_state.results[0].slot0_piece_opaque = 0x12345678u;
  callback_state.results[0].slot0_checkpoint_index = 55;
  callback_state.results[3].latest_slot0_flags = 0x87654321u;
  callback_state.results[3].alternate_slot0_flags = 0xabcdef12u;
  callback_state.results[4] = callback_state.result;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 3u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state |= 0x10000000u;
  world.machines[0].frames_since_start_2 = 90u;
  world.machines[0].track_state.cur_cp_idx = 10;
  world.machines[0].track_state.next_cp_idx = -1;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(callback_state.request_count != 0u);
  for (i = 0u; i < callback_state.request_count; ++i) {
    if (!callback_state.requests[i].has_sonic_oval_floor_bias) {
      continue;
    }
    assert(callback_state.requests[i].flags == 0x4260005u);
    assert(callback_state.requests[i].sonic_oval_floor_bias.candidate_count == 1u);
    assert(callback_state.requests[i].sonic_oval_floor_bias.slot0_piece_opaque == 0x12345678u);
    assert(callback_state.requests[i].sonic_oval_floor_bias.slot0_checkpoint_index == 55);
    assert(callback_state.requests[i].sonic_oval_floor_bias.latest_slot0_flags == 0x87654321u);
    assert(
        callback_state.requests[i].sonic_oval_floor_bias.alternate_slot0_flags == 0xabcdef12u);
    found_bias_request = true;
    break;
  }
  assert(found_bias_request);
  assert(world.machines[0].current_checkpoint == 77);
  assert(fabsf(world.machines[0].checkpoint_fraction - 0.25f) < 0.0001f);
}

static void test_step_machine_phase_updates_air_tilt_when_airborne(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result.has_hit = false;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].frames_since_start_2 = 90u;
  world.machines[0].air_time = 61u;
  world.machines[0].input_steer_yaw = 0.5f;
  world.machines[0].input_steer_pitch = 0.25f;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, -200.0f};

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].air_tilt > 0.0f);
}

static void test_step_machine_phase_orients_basis_toward_floor_normal(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result.has_hit = true;
  callback_state.result.hit_time = 0.995f;
  callback_state.result.hit_point = (fzgx_vec3){0.0f, -10.0f, 0.0f};
  callback_state.result.aux_hit_point = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  callback_state.result.hit_normal = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  callback_state.result.surface_flags = 0x00123456u;
  callback_state.result.branch_flags = 0x01800000u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state &= ~0x00000400u;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, -400.0f};

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].branch_indicator == 0x01800000u);
  assert(world.machines[0].basis_physical.basis_y_x > 0.0f);
  assert(world.machines[0].basis_physical.basis_y_y < 1.0f);
}

static void test_step_machine_phase_applies_drag_and_glide_forces(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;
  float initial_speed;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, -2000.0f};
  initial_speed = fabsf(world.machines[0].velocity.z);

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].velocity.z) < initial_speed);
  assert(world.machines[0].visual_shake_mult > 0.0f);
}

static void test_step_machine_phase_collides_with_landmines_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 3u;
  callback_state.results[2].has_hit = true;
  callback_state.results[2].hit_point = (fzgx_vec3){0.0f, 0.0f, -0.5f};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_state &= ~0x00000400u;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, -1620.0f};

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 3u);
  assert(callback_state.last_request.flags == 0x4000u);
  assert((world.machines[0].terrain_flags & 0x40000000u) != 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 80.0f) < 0.0001f);
  assert(world.machines[0].visual_pitch != 0.0f);
  assert(world.machines[0].velocity.z != 0.0f);
}

static void test_step_machine_phase_collides_with_landmines_from_native_dynamic_scene_course(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_owned_dynamic_scene_collision_course dynamic_course = {0};
  char path[256];
  uint32_t track_index;
  fzgx_vec3 mine_normal;
  fzgx_vec3 old_pos;
  fzgx_vec3 new_pos;
  fzgx_vec3 motion;

  test_build_builtin_coli_course_path(path, sizeof(path), 11u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &dynamic_course) ==
      FZGX_STATUS_OK);
  assert(dynamic_course.object_count > 8u);
  assert(strcmp(dynamic_course.objects[8].primary_lod_name, "MINE01_MINE") == 0);
  assert(dynamic_course.objects[8].collider_mesh.collider_type == 0x4000u);
  assert(dynamic_course.objects[8].has_transform_matrix == 1u);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(bundle != 0);
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(world.scene_object_query_filter_mask == 0x00000001u);
  assert(world.scene_object_query_mode_mask == 0x00000f00u);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  track_index = test_find_builtin_track_index_by_authored_id(bundle, 11u);
  assert(fzgx_sim_world_set_track(&world, track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  mine_normal = dynamic_course.objects[8].transform_matrix.basis_y;
  old_pos.x = dynamic_course.objects[8].transform_matrix.origin.x + mine_normal.x * 20.0f;
  old_pos.y = dynamic_course.objects[8].transform_matrix.origin.y + mine_normal.y * 20.0f;
  old_pos.z = dynamic_course.objects[8].transform_matrix.origin.z + mine_normal.z * 20.0f;
  new_pos.x = dynamic_course.objects[8].transform_matrix.origin.x - mine_normal.x * 20.0f;
  new_pos.y = dynamic_course.objects[8].transform_matrix.origin.y - mine_normal.y * 20.0f;
  new_pos.z = dynamic_course.objects[8].transform_matrix.origin.z - mine_normal.z * 20.0f;
  motion.x = new_pos.x - old_pos.x;
  motion.y = new_pos.y - old_pos.y;
  motion.z = new_pos.z - old_pos.z;

  world.machines[0].position_old = old_pos;
  world.machines[0].position = old_pos;
  world.machines[0].surface_normal = mine_normal;
  world.machines[0].velocity.x = motion.x * world.machines[0].stat_weight;
  world.machines[0].velocity.y = motion.y * world.machines[0].stat_weight;
  world.machines[0].velocity.z = motion.z * world.machines[0].stat_weight;
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert((world.machines[0].terrain_flags & 0x40000000u) != 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 80.0f) < 0.0001f);

  fzgx_content_release_dynamic_scene_collision_course(&dynamic_course);
}

static void test_step_machine_phase_native_dynamic_scene_landmine_uses_collider_type_and_trxs_exact(
    void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_dynamic_scene_object_record objects[2];
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
  fzgx_static_collider_quad_record quads[2];
  fzgx_vec3 old_pos;
  fzgx_vec3 new_pos;
  fzgx_vec3 motion;

  memset(&objects, 0, sizeof(objects));
  memset(&quads, 0, sizeof(quads));
  memset(&dynamic_course, 0, sizeof(dynamic_course));

  quads[0].vertex0 = (fzgx_vec3){-5.0f, 0.0f, -5.0f};
  quads[0].vertex1 = (fzgx_vec3){5.0f, 0.0f, -5.0f};
  quads[0].vertex2 = (fzgx_vec3){5.0f, 0.0f, 5.0f};
  quads[0].vertex3 = (fzgx_vec3){-5.0f, 0.0f, 5.0f};
  quads[1] = quads[0];

  objects[0].render_flags_0 = 0x00004000u;
  objects[0].has_collider_mesh = 1u;
  objects[0].collider_mesh.collider_type = 0x00002000u;
  objects[0].collider_mesh.quad_count = 1u;
  objects[0].collider_mesh.quads = &quads[0];
  objects[0].has_transform_matrix = 1u;
  objects[0].transform_matrix = (fzgx_mat43){
      {1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
      {50.0f, 0.0f, 0.0f},
  };

  objects[1].render_flags_0 = 0x00000001u;
  objects[1].has_collider_mesh = 1u;
  objects[1].collider_mesh.collider_type = 0x00004000u;
  objects[1].collider_mesh.quad_count = 1u;
  objects[1].collider_mesh.quads = &quads[1];
  objects[1].transform.position = (fzgx_vec3){50.0f, 0.0f, 0.0f};
  objects[1].transform.scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};

  dynamic_course.object_count = 2u;
  dynamic_course.objects = objects;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  old_pos = (fzgx_vec3){50.0f, 20.0f, 0.0f};
  new_pos = (fzgx_vec3){50.0f, -20.0f, 0.0f};
  motion = (fzgx_vec3){
      new_pos.x - old_pos.x,
      new_pos.y - old_pos.y,
      new_pos.z - old_pos.z,
  };

  world.machines[0].position_old = old_pos;
  world.machines[0].position = old_pos;
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].velocity.x = motion.x * world.machines[0].stat_weight;
  world.machines[0].velocity.y = motion.y * world.machines[0].stat_weight;
  world.machines[0].velocity.z = motion.z * world.machines[0].stat_weight;
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert((world.machines[0].terrain_flags & 0x40000000u) == 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 100.0f) < 0.0001f);

  world.machines[0].position_old = old_pos;
  world.machines[0].position = old_pos;
  world.machines[0].terrain_flags &= ~0x40000000u;
  world.machines[0].damage_from_last_hit = 0.0f;
  world.machines[0].energy = 100.0f;
  world.machines[0].velocity.x = motion.x * world.machines[0].stat_weight;
  world.machines[0].velocity.y = motion.y * world.machines[0].stat_weight;
  world.machines[0].velocity.z = motion.z * world.machines[0].stat_weight;
  objects[1].transform.unknown_transform_option = 0x0fu;
  objects[1].transform.object_active_override = 0x00u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert((world.machines[0].terrain_flags & 0x40000000u) != 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 80.0f) < 0.0001f);
}

static void test_step_machine_phase_native_dynamic_scene_landmine_uses_generic_clip_bank_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_dynamic_scene_object_record object;
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
  fzgx_static_collider_quad_record quad;
  fzgx_keyable_attribute position_x_keys[2];
  fzgx_vec3 old_pos;
  fzgx_vec3 new_pos;
  fzgx_vec3 motion;

  memset(&world, 0, sizeof(world));
  memset(&object, 0, sizeof(object));
  memset(&dynamic_course, 0, sizeof(dynamic_course));
  memset(&quad, 0, sizeof(quad));
  memset(&position_x_keys, 0, sizeof(position_x_keys));

  quad.vertex0 = (fzgx_vec3){-5.0f, 0.0f, -5.0f};
  quad.vertex1 = (fzgx_vec3){5.0f, 0.0f, -5.0f};
  quad.vertex2 = (fzgx_vec3){5.0f, 0.0f, 5.0f};
  quad.vertex3 = (fzgx_vec3){-5.0f, 0.0f, 5.0f};
  quad.normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  quad.edge_normal0 = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  quad.edge_normal1 = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  quad.edge_normal2 = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  quad.edge_normal3 = (fzgx_vec3){1.0f, 0.0f, 0.0f};

  position_x_keys[0].interpolation_mode = 1u;
  position_x_keys[0].time = 0.0f;
  position_x_keys[0].value = 0.0f;
  position_x_keys[1].interpolation_mode = 1u;
  position_x_keys[1].time = 0.5f;
  position_x_keys[1].value = 50.0f;

  object.render_flags_0 = 0x00000001u;
  object.transform.position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.transform.scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  object.transform.unknown_transform_option = 0x0fu;
  object.transform.object_active_override = 0x01u;
  object.animation_clip_address = 1u;
  object.has_animation_clip = 1u;
  object.animation_clip.time_start_frames = 0.0f;
  object.animation_clip.time_end_frames = 30.0f;
  object.animation_clip.curves[6].curve.keyable_count = 2u;
  object.animation_clip.curves[6].curve.keyables = position_x_keys;
  object.has_collider_mesh = 1u;
  object.collider_mesh.collider_type = 0x00004000u;
  object.collider_mesh.bounding_sphere.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.collider_mesh.bounding_sphere.radius = 8.0f;
  object.collider_mesh.quad_count = 1u;
  object.collider_mesh.quads = &quad;

  dynamic_course.object_count = 1u;
  dynamic_course.objects = &object;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  old_pos = (fzgx_vec3){50.0f, 20.0f, 0.0f};
  new_pos = (fzgx_vec3){50.0f, -20.0f, 0.0f};
  motion = (fzgx_vec3){
      new_pos.x - old_pos.x,
      new_pos.y - old_pos.y,
      new_pos.z - old_pos.z,
  };

  world.stage_scene_frame_banks[0] = 29u;
  world.machines[0].position_old = old_pos;
  world.machines[0].position = old_pos;
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].velocity.x = motion.x * world.machines[0].stat_weight;
  world.machines[0].velocity.y = motion.y * world.machines[0].stat_weight;
  world.machines[0].velocity.z = motion.z * world.machines[0].stat_weight;
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert(world.stage_scene_frame_banks[0] == 30u);
  assert(world.stage_scene_frame_banks[1] == 1u);
  assert(world.stage_scene_frame_banks[2] == 1u);
  assert(world.stage_scene_frame_banks[3] == 1u);
  assert((world.machines[0].terrain_flags & 0x40000000u) != 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 80.0f) < 0.0001f);
}

static void test_step_machine_phase_native_dynamic_scene_landmine_uses_controller_clip_slot_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_dynamic_scene_object_record object;
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
  fzgx_static_collider_quad_record quad;
  fzgx_keyable_attribute position_x_keys[2];
  fzgx_vec3 old_pos;
  fzgx_vec3 new_pos;
  fzgx_vec3 motion;

  memset(&world, 0, sizeof(world));
  memset(&object, 0, sizeof(object));
  memset(&dynamic_course, 0, sizeof(dynamic_course));
  memset(&quad, 0, sizeof(quad));
  memset(&position_x_keys, 0, sizeof(position_x_keys));

  quad.vertex0 = (fzgx_vec3){-5.0f, 0.0f, -5.0f};
  quad.vertex1 = (fzgx_vec3){5.0f, 0.0f, -5.0f};
  quad.vertex2 = (fzgx_vec3){5.0f, 0.0f, 5.0f};
  quad.vertex3 = (fzgx_vec3){-5.0f, 0.0f, 5.0f};
  quad.normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  quad.edge_normal0 = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  quad.edge_normal1 = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  quad.edge_normal2 = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  quad.edge_normal3 = (fzgx_vec3){1.0f, 0.0f, 0.0f};

  position_x_keys[0].interpolation_mode = 1u;
  position_x_keys[0].time = 0.0f;
  position_x_keys[0].value = 0.0f;
  position_x_keys[1].interpolation_mode = 1u;
  position_x_keys[1].time = 1.0f;
  position_x_keys[1].value = 100.0f;

  object.render_flags_0 = 0x000400e1u;
  object.transform.position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.transform.scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  object.transform.unknown_transform_option = 0x0fu;
  object.transform.object_active_override = 0x00u;
  object.animation_clip_address = 1u;
  object.has_animation_clip = 1u;
  object.animation_clip.time_start_frames = 0.0f;
  object.animation_clip.time_end_frames = 60.0f;
  object.animation_clip.bank_time_frames[0] = 0.0f;
  object.animation_clip.bank_time_frames[1] = 0.0f;
  object.animation_clip.bank_time_frames[2] = 0.0f;
  object.animation_clip.bank_time_frames[3] = 0.0f;
  object.animation_clip.layer_flags = 0x00010000u;
  object.animation_clip.curves[6].curve.keyable_count = 2u;
  object.animation_clip.curves[6].curve.keyables = position_x_keys;
  object.has_collider_mesh = 1u;
  object.collider_mesh.collider_type = 0x00004000u;
  object.collider_mesh.bounding_sphere.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.collider_mesh.bounding_sphere.radius = 8.0f;
  object.collider_mesh.quad_count = 1u;
  object.collider_mesh.quads = &quad;

  dynamic_course.object_count = 1u;
  dynamic_course.objects = &object;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  old_pos = (fzgx_vec3){50.0f, 20.0f, 0.0f};
  new_pos = (fzgx_vec3){50.0f, -20.0f, 0.0f};
  motion = (fzgx_vec3){
      new_pos.x - old_pos.x,
      new_pos.y - old_pos.y,
      new_pos.z - old_pos.z,
  };

  world.stage_scene_context_mask = 0x00000080u;
  world.stage_scene_context_view_slot = 0u;
  world.stage_scene_context_active_machine_index = 0;
  world.dynamic_scene_clip_bank_time_frames[0][0] = 30.0f;
  world.machines[0].position_old = old_pos;
  world.machines[0].position = old_pos;
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].velocity.x = motion.x * world.machines[0].stat_weight;
  world.machines[0].velocity.y = motion.y * world.machines[0].stat_weight;
  world.machines[0].velocity.z = motion.z * world.machines[0].stat_weight;
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert((world.machines[0].terrain_flags & 0x40000000u) != 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 80.0f) < 0.0001f);
}

static void test_step_machine_phase_native_dynamic_scene_landmine_uses_story_clip_offset_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_dynamic_scene_object_record object;
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
  fzgx_static_collider_quad_record quad;
  fzgx_keyable_attribute position_x_keys[2];
  fzgx_vec3 old_pos;
  fzgx_vec3 new_pos;
  fzgx_vec3 motion;

  memset(&world, 0, sizeof(world));
  memset(&object, 0, sizeof(object));
  memset(&dynamic_course, 0, sizeof(dynamic_course));
  memset(&quad, 0, sizeof(quad));
  memset(&position_x_keys, 0, sizeof(position_x_keys));

  quad.vertex0 = (fzgx_vec3){-5.0f, 0.0f, -5.0f};
  quad.vertex1 = (fzgx_vec3){5.0f, 0.0f, -5.0f};
  quad.vertex2 = (fzgx_vec3){5.0f, 0.0f, 5.0f};
  quad.vertex3 = (fzgx_vec3){-5.0f, 0.0f, 5.0f};
  quad.normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  quad.edge_normal0 = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  quad.edge_normal1 = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  quad.edge_normal2 = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  quad.edge_normal3 = (fzgx_vec3){1.0f, 0.0f, 0.0f};

  position_x_keys[0].interpolation_mode = 1u;
  position_x_keys[0].time = 0.0f;
  position_x_keys[0].value = 0.0f;
  position_x_keys[1].interpolation_mode = 1u;
  position_x_keys[1].time = 1.0f;
  position_x_keys[1].value = 100.0f;

  object.render_flags_0 = 0x00000001u;
  object.transform.position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.transform.scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  object.transform.unknown_transform_option = 0x0fu;
  object.transform.object_active_override = 0x80u;
  object.animation_clip_address = 1u;
  object.has_animation_clip = 1u;
  object.animation_clip.time_start_frames = 0.0f;
  object.animation_clip.time_end_frames = 60.0f;
  object.animation_clip.curves[6].curve.keyable_count = 2u;
  object.animation_clip.curves[6].curve.keyables = position_x_keys;
  object.has_collider_mesh = 1u;
  object.collider_mesh.collider_type = 0x00004000u;
  object.collider_mesh.bounding_sphere.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.collider_mesh.bounding_sphere.radius = 8.0f;
  object.collider_mesh.quad_count = 1u;
  object.collider_mesh.quads = &quad;

  dynamic_course.object_count = 1u;
  dynamic_course.objects = &object;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  old_pos = (fzgx_vec3){50.0f, 20.0f, 0.0f};
  new_pos = (fzgx_vec3){50.0f, -20.0f, 0.0f};
  motion = (fzgx_vec3){
      new_pos.x - old_pos.x,
      new_pos.y - old_pos.y,
      new_pos.z - old_pos.z,
  };

  world.stage_scene_frame_banks[0] = 59u;
  world.stage_scene_story_clip_offset_frames = 40.0f;
  world.pending_stage_scene_story_delta_frames = 10.0f;
  world.has_pending_stage_scene_story_delta = 1u;
  world.machines[0].position_old = old_pos;
  world.machines[0].position = old_pos;
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].velocity.x = motion.x * world.machines[0].stat_weight;
  world.machines[0].velocity.y = motion.y * world.machines[0].stat_weight;
  world.machines[0].velocity.z = motion.z * world.machines[0].stat_weight;
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert(world.stage_scene_frame_banks[0] == 60u);
  assert(world.has_pending_stage_scene_story_delta == 0u);
  assert(fabsf(world.stage_scene_story_clip_offset_frames - 30.0f) < 0.0001f);
  assert((world.machines[0].terrain_flags & 0x40000000u) != 0u);
  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 80.0f) < 0.0001f);
}

static void test_step_machine_phase_promotes_pending_dynamic_scene_flags_for_alt_terrain_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_dynamic_scene_object_record object;
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
  fzgx_static_collider_quad_record quad;

  memset(&world, 0, sizeof(world));
  memset(&object, 0, sizeof(object));
  memset(&dynamic_course, 0, sizeof(dynamic_course));
  memset(&quad, 0, sizeof(quad));

  quad.vertex0 = (fzgx_vec3){-5.0f, 0.0f, -5.0f};
  quad.vertex1 = (fzgx_vec3){5.0f, 0.0f, -5.0f};
  quad.vertex2 = (fzgx_vec3){5.0f, 0.0f, 5.0f};
  quad.vertex3 = (fzgx_vec3){-5.0f, 0.0f, 5.0f};
  quad.normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  quad.edge_normal0 = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  quad.edge_normal1 = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  quad.edge_normal2 = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  quad.edge_normal3 = (fzgx_vec3){1.0f, 0.0f, 0.0f};

  object.render_flags_0 = 0x00000001u;
  object.transform.position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.transform.scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  object.transform.unknown_transform_option = 0x0fu;
  object.transform.object_active_override = 0x00u;
  object.has_collider_mesh = 1u;
  object.collider_mesh.collider_type = 0x00004000u;
  object.collider_mesh.bounding_sphere.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object.collider_mesh.bounding_sphere.radius = 8.0f;
  object.collider_mesh.quad_count = 1u;
  object.collider_mesh.quads = &quad;

  dynamic_course.object_count = 1u;
  dynamic_course.objects = &object;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_dynamic_scene_collision_course(&world, &dynamic_course) ==
      FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.dynamic_scene_runtime_flags[0] = 0x20000000u;
  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].position_old_dupe = world.machines[0].position;
  world.machines[0].position_old_2 = world.machines[0].position;
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].terrain_flags = 0u;
  world.machines[0].terrain_flags_2 = 0u;
  world.machines[0].machine_state &= ~0x00000400u;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert((world.dynamic_scene_runtime_flags[0] & 0x20000000u) == 0u);
  assert((world.dynamic_scene_runtime_flags[0] & 0x40000000u) != 0u);
  assert((world.machines[0].terrain_flags & 0x02000000u) != 0u);
}

static void test_step_machine_phase_runs_post_rotation_tail_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_control_sample control = {0};
  control.control_profile_kind = 2u;
  fzgx_vec3 initial_bottom;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){10.0f, 0.0f, -5.0f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].position_old_dupe = world.machines[0].position;
  world.machines[0].position_old_2 = world.machines[0].position;
  world.machines[0].position_bottom = (fzgx_vec3){3.0f, -10.0f, 7.0f};
  world.machines[0].velocity = (fzgx_vec3){1620.0f, 0.0f, 0.0f};
  world.machines[0].base_speed = 3.0f;
  world.machines[0].angular_velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].frames_since_start_2 = 13u;
  world.machines[0].rail_collision_timer = 3u;
  world.machines[0].unk_float_0x208 = 0.0f;
  world.machines[0].machine_state |= 0x03803000u;
  initial_bottom = world.machines[0].position_bottom;

  assert(fzgx_sim_step_begin(&world, &control, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_state & 0x03803000u) == 0u);
  assert((world.machines[0].machine_state & 0x00080000u) != 0u);
  assert(world.machines[0].rail_collision_timer == 2u);
  assert(world.machines[0].unk_float_0x208 == 200.0f);
  assert(world.machines[0].angular_velocity.x != 0.0f);
  assert(world.machines[0].position_bottom.x > initial_bottom.x);
}

static void test_frame_phase_refreshes_wall_contact_queries_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_race_step_options options = {false, false, false, false};

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 5u;
  callback_state.results[1].has_hit = true;
  callback_state.results[1].summary_flags = 0xf0000000u;
  callback_state.results[1].contact_slots[0].has_contact = true;
  callback_state.results[1].contact_slots[0].push = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  callback_state.results[1].contact_slots[1].has_contact = true;
  callback_state.results[1].contact_slots[1].push = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  callback_state.results[2].has_hit = true;
  callback_state.results[2].summary_flags = 0xf0000000u;
  callback_state.results[2].contact_slots[0].has_contact = true;
  callback_state.results[2].contact_slots[0].push = (fzgx_vec3){2.0f, 0.0f, 0.0f};
  callback_state.results[2].contact_slots[1].has_contact = true;
  callback_state.results[2].contact_slots[1].push = (fzgx_vec3){0.0f, 0.0f, 2.0f};
  callback_state.results[3].has_hit = true;
  callback_state.results[3].summary_flags = 0xf0000000u;
  callback_state.results[3].contact_slots[0].has_contact = true;
  callback_state.results[3].contact_slots[0].push = (fzgx_vec3){3.0f, 0.0f, 0.0f};
  callback_state.results[3].contact_slots[1].has_contact = true;
  callback_state.results[3].contact_slots[1].push = (fzgx_vec3){0.0f, 0.0f, 3.0f};
  callback_state.results[4].has_hit = true;
  callback_state.results[4].summary_flags = 0xf0000000u;
  callback_state.results[4].contact_slots[0].has_contact = true;
  callback_state.results[4].contact_slots[0].push = (fzgx_vec3){4.0f, 0.0f, 0.0f};
  callback_state.results[4].contact_slots[1].has_contact = true;
  callback_state.results[4].contact_slots[1].push = (fzgx_vec3){0.0f, 0.0f, 4.0f};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){10.0f, 20.0f, 30.0f};
  world.machines[0].wall_corners[0].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[1].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[2].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[3].pos_a = world.machines[0].position;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 5u);
  assert(callback_state.first_request.flags == 0x22800u);
  assert(callback_state.last_request.flags == 0x3020005u);
  assert(fabsf(world.machines[0].wall_corners[0].collision.x - 1.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].collision.z - 1.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[1].collision.x - 2.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[1].collision.z - 2.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[2].collision.x - 3.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[2].collision.z - 3.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[3].collision.x - 4.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[3].collision.z - 4.0f) < 0.0001f);
  assert(fabsf(world.machines[0].collision_push_total.x - 4.0f) < 0.0001f);
  assert(fabsf(world.machines[0].collision_push_total.y - 0.0f) < 0.0001f);
  assert(fabsf(world.machines[0].collision_push_total.z - 4.0f) < 0.0001f);
}

static void test_frame_phase_refreshes_wall_contact_queries_from_native_static_collider_course(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  fzgx_static_collider_surface_grid surface_grids[14];
  fzgx_static_collider_quad_record quads[3];
  uint16_t quad_indices[3] = {0u, 1u, 2u};
  fzgx_race_step_options options = {false, false, false, false};
  size_t i;

  memset(surface_grids, 0, sizeof(surface_grids));
  memset(quads, 0, sizeof(quads));
  static_course.surface_count = 14u;
  static_course.mesh_grid.left = -20.0f;
  static_course.mesh_grid.top = -20.0f;
  static_course.mesh_grid.subdivision_width = 40.0f;
  static_course.mesh_grid.subdivision_length = 40.0f;
  static_course.mesh_grid.num_subdivisions_x = 1;
  static_course.mesh_grid.num_subdivisions_z = 1;
  static_course.quad_count = 3u;
  static_course.quad_index_count = 3u;
  static_course.quads = quads;
  static_course.quad_indices = quad_indices;
  static_course.surface_grids = surface_grids;
  test_init_static_collider_quad(
      &quads[0],
      (fzgx_vec3){-10.0f, -0.05f, -10.0f},
      (fzgx_vec3){10.0f, -0.05f, -10.0f},
      (fzgx_vec3){10.0f, -0.05f, 10.0f},
      (fzgx_vec3){-10.0f, -0.05f, 10.0f},
      (fzgx_vec3){0.0f, 1.0f, 0.0f});
  test_init_static_collider_quad(
      &quads[1],
      (fzgx_vec3){-0.5f, -5.0f, -10.0f},
      (fzgx_vec3){-0.5f, -5.0f, 10.0f},
      (fzgx_vec3){-0.5f, 5.0f, 10.0f},
      (fzgx_vec3){-0.5f, 5.0f, -10.0f},
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  test_init_static_collider_quad(
      &quads[2],
      (fzgx_vec3){0.5f, -5.0f, 10.0f},
      (fzgx_vec3){0.5f, -5.0f, -10.0f},
      (fzgx_vec3){0.5f, 5.0f, -10.0f},
      (fzgx_vec3){0.5f, 5.0f, 10.0f},
      (fzgx_vec3){-1.0f, 0.0f, 0.0f});
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE].quad_cells[0].offset = 0u;
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE].quad_cells[0].count = 1u;
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_WALL].quad_cells[0].offset = 1u;
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_WALL].quad_cells[0].count = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_x = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_y = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical.basis_z = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  world.machines[0].zero_minus_height_above_track = 10.0f;
  for (i = 0u; i < 4u; ++i) {
    world.machines[0].wall_corners[i].pos_a = world.machines[0].position;
  }

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].wall_corners[0].collision.x < -0.1f);
  assert(world.machines[0].wall_corners[0].collision.y > 0.01f);
  assert(world.machines[0].wall_corners[1].collision.x > 0.1f);
  assert(world.machines[0].wall_corners[1].collision.y > 0.01f);
  assert((world.machines[0].suspension_corners[0].state & 0x20u) == 0u);
  assert((world.machines[0].suspension_corners[1].state & 0x20u) == 0u);
}

static void test_frame_phase_native_nohit_wall_queries_still_leave_suspension_enabled(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  fzgx_static_collider_surface_grid surface_grids[14];
  fzgx_static_collider_quad_record quads[1];
  uint16_t quad_indices[1] = {0u};
  fzgx_race_step_options options = {false, false, false, false};
  size_t i;

  memset(surface_grids, 0, sizeof(surface_grids));
  memset(quads, 0, sizeof(quads));
  static_course.surface_count = 14u;
  static_course.mesh_grid.left = -20.0f;
  static_course.mesh_grid.top = -20.0f;
  static_course.mesh_grid.subdivision_width = 40.0f;
  static_course.mesh_grid.subdivision_length = 40.0f;
  static_course.mesh_grid.num_subdivisions_x = 1;
  static_course.mesh_grid.num_subdivisions_z = 1;
  static_course.quad_count = 1u;
  static_course.quad_index_count = 1u;
  static_course.quads = quads;
  static_course.quad_indices = quad_indices;
  static_course.surface_grids = surface_grids;
  test_init_static_collider_quad(
      &quads[0],
      (fzgx_vec3){-10.0f, -0.05f, -10.0f},
      (fzgx_vec3){10.0f, -0.05f, -10.0f},
      (fzgx_vec3){10.0f, -0.05f, 10.0f},
      (fzgx_vec3){-10.0f, -0.05f, 10.0f},
      (fzgx_vec3){0.0f, 1.0f, 0.0f});
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE].quad_cells[0].offset = 0u;
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE].quad_cells[0].count = 1u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_x = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_y = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical.basis_z = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  world.machines[0].zero_minus_height_above_track = 10.0f;
  for (i = 0u; i < 4u; ++i) {
    world.machines[0].wall_corners[i].pos_a = world.machines[0].position;
    world.machines[0].suspension_corners[i].state = 0u;
  }

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  for (i = 0u; i < 4u; ++i) {
    assert((world.machines[0].suspension_corners[i].state & 0x20u) == 0u);
  }
}

static void test_frame_phase_applies_branch_slot_filter_to_course8_wall_sweeps(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_race_step_options options = {false, false, false, false};
  uint32_t expected_flags = 0x21020005u;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 5u;
  callback_state.results[1].has_hit = true;
  callback_state.results[1].summary_flags = 0xf0000000u;
  callback_state.results[2].has_hit = true;
  callback_state.results[2].summary_flags = 0xf0000000u;
  callback_state.results[3].has_hit = true;
  callback_state.results[3].summary_flags = 0xf0000000u;
  callback_state.results[4].has_hit = true;
  callback_state.results[4].summary_flags = 0xf0000000u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].branch_slot = 2u;
  world.machines[0].track_state.cur_cp_idx = 0x22;
  world.machines[0].track_state.next_cp_idx = 0x23;
  world.machines[0].wall_corners[0].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[1].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[2].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[3].pos_a = world.machines[0].position;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(callback_state.call_count == 5u);
  assert(callback_state.requests[0].flags == 0x22800u);
  assert(callback_state.requests[1].flags == expected_flags);
  assert(callback_state.requests[2].flags == expected_flags);
  assert(callback_state.requests[3].flags == expected_flags);
  assert(callback_state.requests[4].flags == expected_flags);
}

static void test_frame_phase_center_wall_sweep_oob_hit_sets_fallout_state_from_native_static_course(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_owned_static_collider_course static_course = {0};
  fzgx_static_collider_surface_grid surface_grids[14];
  fzgx_static_collider_quad_record quads[1];
  uint16_t quad_indices[1] = {0u};
  fzgx_race_step_options options = {false, false, false, false};
  size_t i;

  memset(surface_grids, 0, sizeof(surface_grids));
  memset(quads, 0, sizeof(quads));
  static_course.surface_count = 14u;
  static_course.mesh_grid.left = -20.0f;
  static_course.mesh_grid.top = -20.0f;
  static_course.mesh_grid.subdivision_width = 40.0f;
  static_course.mesh_grid.subdivision_length = 40.0f;
  static_course.mesh_grid.num_subdivisions_x = 1;
  static_course.mesh_grid.num_subdivisions_z = 1;
  static_course.quad_count = 1u;
  static_course.quad_index_count = 1u;
  static_course.quads = quads;
  static_course.quad_indices = quad_indices;
  static_course.surface_grids = surface_grids;
  test_init_static_collider_quad(
      &quads[0],
      (fzgx_vec3){-10.0f, -5.0f, 0.5f},
      (fzgx_vec3){10.0f, -5.0f, 0.5f},
      (fzgx_vec3){10.0f, 5.0f, 0.5f},
      (fzgx_vec3){-10.0f, 5.0f, 0.5f},
      (fzgx_vec3){0.0f, 0.0f, 1.0f});
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_DEATH_2].quad_cells[0].offset = 0u;
  surface_grids[FZGX_STATIC_COLLIDER_SURFACE_DEATH_2].quad_cells[0].count = 1u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_static_collider_course(&world, &static_course) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].energy = 55.0f;
  world.machines[0].base_speed = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};
  for (i = 0u; i < 4u; ++i) {
    world.machines[0].wall_corners[i].pos_a = world.machines[0].position;
  }
  world.machines[0].wall_corners[0].pos_a = (fzgx_vec3){0.0f, 0.0f, 2.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
  assert((world.machines[0].machine_state & 0x40800080u) == 0x40800080u);
  assert((world.machines[0].state_2 & 0x10u) != 0u);
  assert(world.machines[0].energy == 0.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].angular_velocity.x == 0.0f);
}

static void test_frame_phase_center_wall_sweep_oob_hit_sets_fallout_state(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_race_step_options options = {false, false, false, false};

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 1u;
  callback_state.results[0].has_hit = true;
  callback_state.results[0].hit_info_flags = 0x600u;
  callback_state.results[0].hit_normal = (fzgx_vec3){1.0f, 0.0f, 0.0f};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].energy = 55.0f;
  world.machines[0].base_speed = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};
  world.machines[0].wall_corners[0].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[1].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[2].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[3].pos_a = world.machines[0].position;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
  assert((world.machines[0].machine_state & 0x40800080u) == 0x40800080u);
  assert((world.machines[0].state_2 & 0x10u) != 0u);
  assert(world.machines[0].energy == 0.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].angular_velocity.x == 0.0f);
}

static void test_frame_phase_applies_generic_world_contact_response(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  test_spherecast_callback_state callback_state;
  fzgx_race_step_options options = {false, false, false, false};

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 5u;
  callback_state.results[1].has_hit = true;
  callback_state.results[1].summary_flags = 0xf0000000u;
  callback_state.results[1].contact_slots[0].has_contact = true;
  callback_state.results[1].contact_slots[0].push = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  callback_state.results[2].has_hit = true;
  callback_state.results[2].summary_flags = 0xf0000000u;
  callback_state.results[2].contact_slots[0].has_contact = true;
  callback_state.results[2].contact_slots[0].push = (fzgx_vec3){2.0f, 0.0f, 0.0f};
  callback_state.results[3].has_hit = true;
  callback_state.results[3].summary_flags = 0xf0000000u;
  callback_state.results[3].contact_slots[0].has_contact = true;
  callback_state.results[3].contact_slots[0].push = (fzgx_vec3){3.0f, 0.0f, 0.0f};
  callback_state.results[4].has_hit = true;
  callback_state.results[4].summary_flags = 0xf0000000u;
  callback_state.results[4].contact_slots[0].has_contact = true;
  callback_state.results[4].contact_slots[0].push = (fzgx_vec3){4.0f, 0.0f, 0.0f};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].velocity = (fzgx_vec3){-500.0f, 0.0f, 1000.0f};
  world.machines[0].energy = 100.0f;
  world.machines[0].frames_since_start_2 = 90u;
  world.machines[0].wall_corners[0].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[1].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[2].pos_a = world.machines[0].position;
  world.machines[0].wall_corners[3].pos_a = world.machines[0].position;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_state & 0x00800000u) != 0u);
  assert(world.machines[0].wall_hit_count == 1u);
  assert(fabsf(world.machines[0].collision_push_total.x - 4.0f) < 0.0001f);
  assert(fabsf(world.machines[0].collision_response.x - 4.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position.x - 4.0f) < 0.0001f);
  assert(world.machines[0].energy < 100.0f);
  assert(world.machines[0].damage_from_last_hit > 0.0f);
}

static void test_frame_phase_applies_grounded_lava_damage_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].terrain_flags |= 0x02000000u;
  world.machines[0].stat_body = 90.0f;
  world.machines[0].max_energy = 100.0f;
  world.machines[0].energy = 25.0f;
  world.machines[0].machine_state &= ~0x00000080u;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].damage_from_last_hit - 20.0f) < 0.0001f);
  assert(fabsf(world.machines[0].energy - 5.0f) < 0.0001f);
  assert((world.machines[0].machine_state & 0x00000080u) == 0u);
}

static void test_frame_phase_lava_can_trigger_instant_destroy_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 0x208u;
  world.machines[0].terrain_flags |= 0x02000000u;
  world.machines[0].stat_body = 90.0f;
  world.machines[0].max_energy = 10.0f;
  world.machines[0].energy = 5.0f;
  world.machines[0].base_speed = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
  assert((world.machines[0].machine_state & 0x40000880u) == 0x40000880u);
  assert((world.machines[0].machine_state & 0x00800000u) != 0u);
  assert((world.machines[0].state_2 & 0x10u) != 0u);
  assert(world.machines[0].energy == 0.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].angular_velocity.x == 0.0f);
}

static void test_frame_phase_marks_zero_hp_machine_crashed_and_seeds_restore_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x00000081u;
  world.machines[0].frames_until_restored = 0u;
  world.machines[0].machine_crashed = false;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].machine_crashed == true);
  assert(world.machines[0].frames_until_restored == 0x00b4u);
}

static void test_frame_phase_b29_suppresses_crash_restore_seed_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x10000081u;
  world.machines[0].frames_until_restored = 0u;
  world.machines[0].machine_crashed = false;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].machine_crashed == true);
  assert(world.machines[0].frames_until_restored == 0u);
}

static void test_frame_phase_damage_ko_extends_restore_countdown_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x00800081u;
  world.machines[0].frames_until_restored = 0u;
  world.machines[0].machine_crashed = false;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].frames_until_restored == 270u);
}

static void test_frame_phase_oob_crash_keeps_default_restore_countdown_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x40800081u;
  world.machines[0].frames_until_restored = 0u;
  world.machines[0].machine_crashed = false;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].frames_until_restored == 180u);
}

static void test_frame_phase_broken_down_machine_applies_mid_speed_decay_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x00000280u;
  world.machines[0].machine_crashed = true;
  world.machines[0].speed_kmh = 50.0f;
  world.machines[0].velocity = (fzgx_vec3){100.0f, -20.0f, 10.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].velocity.x - 95.0f) < 0.0001f);
  assert(fabsf(world.machines[0].velocity.y + 19.0f) < 0.0001f);
  assert(fabsf(world.machines[0].velocity.z - 9.5f) < 0.0001f);
}

static void test_frame_phase_zero_hp_machine_runs_breakdown_fling_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state &= ~(0x08000201u);
  world.machines[0].machine_state |= 0x00000080u;
  world.machines[0].speed_kmh = 400.0f;
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].frames_since_death = 0u;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].state_2 & 2u) != 0u);
  assert(world.machines[0].frames_since_death == 2u);
  assert(fabsf(world.machines[0].velocity.y) < 0.001f);
  assert((world.machines[0].broken_down_angle_fac2.x != 0.0f) ||
         (world.machines[0].broken_down_angle_fac2.y != 0.0f) ||
         (world.machines[0].broken_down_angle_fac2.z != 0.0f));
}

static void test_frame_phase_breakdown_physics_accumulates_angles_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x00000002u;
  world.machines[0].frames_since_death = 10u;
  world.machines[0].g_unk_breakdown_int = 3u;
  world.machines[0].broken_down_angle_fac1 = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].broken_down_angle_fac2 = (fzgx_vec3){4.0f, 5.0f, 6.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].broken_down_angle_fac1.x - 5.0f) < 0.0001f);
  assert(fabsf(world.machines[0].broken_down_angle_fac1.y - 7.0f) < 0.0001f);
  assert(fabsf(world.machines[0].broken_down_angle_fac1.z - 9.0f) < 0.0001f);
  assert(world.machines[0].g_unk_breakdown_int == 4u);
}

static void test_frame_phase_breakdown_physics_times_out_to_state2_latch_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].frames_since_death = 0x0100u;
  world.machines[0].g_unk_breakdown_int = 0xefu;
  world.machines[0].speed_kmh = 0.0f;
  world.machines[0].position_old = (fzgx_vec3){9.0f, 8.0f, 7.0f};
  world.machines[0].position = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].g_unk_breakdown_int == 0xf0u);
  assert((world.machines[0].state_2 & 0x180u) == 0x180u);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].velocity.y == 0.0f);
  assert(world.machines[0].velocity.z == 0.0f);
  assert(world.machines[0].position.x == 9.0f);
  assert(world.machines[0].position.y == 8.0f);
  assert(world.machines[0].position.z == 7.0f);
}

static void test_frame_phase_updates_transform_visual_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].position = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].position_old_dupe = world.machines[0].position;
  world.machines[0].position_old_2 = world.machines[0].position;
  world.machines[0].height_adjust_from_boost = 0.2f;
  world.machines[0].visual_shake_mult = 0.0f;
  world.machines[0].terrain_flags &= ~0x20000000u;
  world.machines[0].machine_state &= ~0x00000020u;
  world.machines[0].frames_since_start_2 = 90u;
  world.machines[0].base_speed = 2.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].height_adjust_from_boost - 0.19f) < 0.0001f);
  assert(fabsf(world.machines[0].transform_visual.origin.x - 1.0f) < 0.0001f);
  assert(fabsf(world.machines[0].transform_visual.origin.y - 2.19f) < 0.0001f);
  assert(fabsf(world.machines[0].transform_visual.origin.z - 3.0f) < 0.0001f);
  assert(world.machines[0].g_pitch_mtx_0x5e0.basis_y_z != 0.0f);
}

static void test_frame_phase_broken_down_machine_retires_below_speed_floor_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_state |= 0x00000280u;
  world.machines[0].machine_crashed = true;
  world.machines[0].speed_kmh = 5.0f;
  world.machines[0].position_old = (fzgx_vec3){9.0f, 8.0f, 7.0f};
  world.machines[0].position = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_state & 0x08000000u) != 0u);
  assert((world.machines[0].state_2 & 0x180u) == 0x180u);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].position.x == 9.0f);
  assert(world.machines[0].position.y == 8.0f);
  assert(world.machines[0].position.z == 7.0f);
}

static void test_step_machine_phase_respawn_interpolates_and_skips_motion_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].frames_until_restored = 150u;
  world.machines[0].g_restore_matrix_1 = world.machines[0].basis_physical;
  world.machines[0].g_restore_matrix_1.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].g_restore_mtx_2 = world.machines[0].transform_visual;
  world.machines[0].g_restore_mtx_3 = world.machines[0].basis_physical;
  world.machines[0].g_restore_mtx_3.origin = (fzgx_vec3){0.0f, 100.0f, 0.0f};

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].frames_until_restored == 149u);
  assert(world.machines[0].post_restore_frame_countdown == 0x3cu);
  assert(fabsf(world.machines[0].restore_progress - 0.00049382716f) < 0.000001f);
  assert(world.machines[0].position.y > 0.0f);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_SIM_MOTION_RAN) == 0u);
  assert((world.machines[0].state_2 & 8u) == 0u);
}

static void test_step_machine_phase_respawn_handoff_restores_machine_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_mat43 restore_transform;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);

  restore_transform = world.machines[0].transform_visual;
  restore_transform.origin.y += 2.0f;
  world.machines[0].frames_until_restored = 21u;
  world.machines[0].energy = 10.0f;
  world.machines[0].restore_count = 2u;
  world.machines[0].g_restore_matrix_1 = world.machines[0].basis_physical;
  world.machines[0].g_restore_matrix_1.origin = world.machines[0].position;
  world.machines[0].g_restore_mtx_2 = world.machines[0].transform_visual;
  world.machines[0].g_restore_mtx_3 = restore_transform;

  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);

  assert(world.machines[0].frames_until_restored == 20u);
  assert(world.machines[0].restore_count == 3u);
  assert(world.machines[0].frames_since_start_2 == 59u);
  assert((world.machines[0].machine_state & 0x00000400u) != 0u);
  assert((world.machines[0].machine_state & 0x00008000u) != 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_SIM_MOTION_RAN) != 0u);
  assert((world.machines[0].state_2 & 8u) != 0u);
  assert(world.machines[0].energy >= 50.0f);
  assert(world.machines[0].track_state.cur_cp_idx >= 0);
  assert(world.machines[0].track_state.last_cp_idx == -1);
}

static void test_frame_phase_latches_machine_approach_counter_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_approach_frame_counter = 0u;
  world.machines[0].machine_state &= ~(0x00000880u);
  world.machines[0].approach_dir = (fzgx_vec3){100.0f, 0.0f, 0.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(world.machines[0].machine_approach_frame_counter == 60u);
}

static void test_frame_phase_clamps_machine_below_negative_ten_thousand_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].position = (fzgx_vec3){12.0f, -10001.0f, 34.0f};
  world.machines[0].velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].position.x - 12.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position.y - -10000.0f) < 0.0001f);
  assert(fabsf(world.machines[0].position.z - 34.0f) < 0.0001f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].velocity.y == 0.0f);
  assert(world.machines[0].velocity.z == 0.0f);
}

static void test_frame_phase_destroys_machine_below_negative_five_thousand_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].position = (fzgx_vec3){12.0f, -5001.0f, 34.0f};
  world.machines[0].base_speed = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
  assert((world.machines[0].machine_state & 0x40000880u) == 0x40000880u);
  assert((world.machines[0].state_2 & 0x10u) != 0u);
  assert(fabsf(world.machines[0].position.y - -5001.0f) < 0.0001f);
  assert(world.machines[0].energy == 0.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].angular_velocity.x == 0.0f);
}

static void test_frame_phase_destroys_machine_below_track_min_height_margin_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].position = (fzgx_vec3){12.0f, -1000.0f, 34.0f};
  world.machines[0].base_speed = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].angular_velocity = (fzgx_vec3){4.0f, 5.0f, 6.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
  assert((world.machines[0].machine_state & 0x40000880u) == 0x40000880u);
  assert((world.machines[0].state_2 & 0x10u) != 0u);
  assert(fabsf(world.machines[0].position.y - -1000.0f) < 0.0001f);
  assert(world.machines[0].energy == 0.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].angular_velocity.x == 0.0f);
}

static void test_frame_phase_refreshes_corner_history_from_current_transform_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].basis_physical.basis_x_x = 1.0f;
  world.machines[0].basis_physical.basis_y_y = 1.0f;
  world.machines[0].basis_physical.basis_z_z = 1.0f;
  world.machines[0].position = (fzgx_vec3){10.0f, 20.0f, 30.0f};
  world.machines[0].suspension_corners[0].offset = (fzgx_vec3){1.0f, 2.0f, 3.0f};
  world.machines[0].suspension_corners[0].force = 4.0f;
  world.machines[0].suspension_corners[0].rest_length_scale = 5.0f;
  world.machines[0].suspension_corners[0].pos = (fzgx_vec3){7.0f, 8.0f, 9.0f};
  world.machines[0].wall_corners[0].offset = (fzgx_vec3){1.5f, -0.5f, 2.0f};
  world.machines[0].wall_corners[0].pos_b = (fzgx_vec3){11.0f, 12.0f, 13.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].suspension_corners[0].pos_old.x - 7.0f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[0].pos_old.y - 8.0f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[0].pos_old.z - 9.0f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[0].pos.x - 11.0f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[0].pos.y - 21.0f) < 0.0001f);
  assert(fabsf(world.machines[0].suspension_corners[0].pos.z - 33.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_a.x - 11.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_a.y - 12.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_a.z - 13.0f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_b.x - 11.5f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_b.y - 19.5f) < 0.0001f);
  assert(fabsf(world.machines[0].wall_corners[0].pos_b.z - 32.0f) < 0.0001f);
}

static void test_frame_phase_refreshes_speed_and_max_speed_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].stat_weight = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, 100.0f};
  world.machines[0].max_speed_kmh = 150.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].speed_kmh - 216.0f) < 0.0001f);
  assert(fabsf(world.machines[0].max_speed_kmh - 216.0f) < 0.0001f);
}

static void test_frame_phase_skips_max_speed_refresh_when_damage_state_is_set(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].state_2 |= 8u;
  world.machines[0].stat_weight = 100.0f;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, 100.0f};
  world.machines[0].max_speed_kmh = 150.0f;
  world.machines[0].machine_state |= 0x00800000u;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  assert(fabsf(world.machines[0].speed_kmh - 216.0f) < 0.0001f);
  assert(fabsf(world.machines[0].max_speed_kmh - 150.0f) < 0.0001f);
}

static void test_configure_world_with_builtin_ruby_bundle_and_30_machines(void) {
  static const uint32_t ruby_track_indices[] = {0u, 4u, 11u, 16u, 27u};
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  uint32_t track_index;
  uint32_t machine_index;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 30u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 30u) == FZGX_STATUS_OK);

  for (track_index = 0u; track_index < sizeof(ruby_track_indices) / sizeof(ruby_track_indices[0]);
       ++track_index) {
    assert(fzgx_sim_world_set_track(&world, ruby_track_indices[track_index]) == FZGX_STATUS_OK);
    for (machine_index = 0u; machine_index < 30u; ++machine_index) {
      assert(
          fzgx_sim_world_seed_machine_from_content(&world, machine_index, machine_index) ==
          FZGX_STATUS_OK);
      assert(world.machines[machine_index].machine_id == machine_index);
      assert((world.machines[machine_index].machine_flags & FZGX_MACHINE_FLAG_ACTIVE) != 0u);
    }
  }

  assert(world.active_track_index == 27u);
  assert(world.machines[6].stat_weight == 1260.0f);
  assert(world.machines[6].stat_grip_1 == 0.47f);
  assert(world.machines[6].stat_body == 0.85f);
  assert(fabsf(world.machines[6].stat_boost_strength - 7.98f) < 0.0001f);
}

static void test_build_ordinary_start_grid_slot_transform_from_world(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  static const uint32_t ruby_track_indices[] = {0u, 4u, 11u, 16u, 27u};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  uint32_t i;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 30u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);

  for (i = 0u; i < sizeof(ruby_track_indices) / sizeof(ruby_track_indices[0]); ++i) {
    fzgx_mat43 transform = {0};
    float width = 0.0f;

    assert(fzgx_sim_world_set_track(&world, ruby_track_indices[i]) == FZGX_STATUS_OK);
    assert(
        fzgx_sim_world_build_ordinary_start_grid_slot_transform(
            &world, 0u, &width, &transform) == FZGX_STATUS_OK);
    assert(width > 0.0f);
    assert(
        (transform.basis_x_x != 0.0f) ||
        (transform.basis_x_y != 0.0f) ||
        (transform.basis_x_z != 0.0f));
  }
}

static void test_build_ordinary_start_grid_slot_query_result_from_world(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  static const uint32_t ruby_track_indices[] = {0u, 4u, 11u, 16u, 27u};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  uint32_t i;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 30u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);

  for (i = 0u; i < sizeof(ruby_track_indices) / sizeof(ruby_track_indices[0]); ++i) {
    fzgx_current_track_query_result query;

    memset(&query, 0xff, sizeof(query));
    assert(fzgx_sim_world_set_track(&world, ruby_track_indices[i]) == FZGX_STATUS_OK);
    assert(
        fzgx_sim_world_build_ordinary_start_grid_slot_query_result(&world, 0u, &query) ==
        FZGX_STATUS_OK);
    assert(query.checkpoint_index >= 0);
    assert(query.cached_frame_count >= 1u);
    assert(query.last_frac_diff > 0.0f);
    assert(query.selected_cached_frame_index >= 0);
    assert(query.selected_cached_frame_index < (int32_t)query.cached_frame_count);
    assert(query.frame.track_width_or_radius > 0.0f);
  }
}

static void test_snapshot_round_trip(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_snapshot snapshot;
  fzgx_machine_snapshot copy;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.machine_id = 7u;
  snapshot.machine_flags = FZGX_MACHINE_FLAG_ACTIVE | FZGX_MACHINE_FLAG_AIRBORNE;
  snapshot.energy = 83.0f;
  snapshot.max_energy = 100.0f;
  snapshot.position.x = 1.0f;
  snapshot.position.y = 2.0f;
  snapshot.position.z = 3.0f;
  snapshot.track_state.last_cp_idx = 17;
  snapshot.track_state.last_cp_frac = 0.25f;
  snapshot.track_state.cur_cp_idx = 18;
  snapshot.track_state.cur_cp_frac = 0.5f;
  snapshot.track_state.next_cp_idx = 19;
  snapshot.track_state.next_cp_frac = 0.75f;
  snapshot.track_state.respawn_pos.x = 10.0f;
  snapshot.track_state.rank_this_frame = 4u;

  assert(fzgx_sim_world_set_machine_snapshot(&world, 0u, &snapshot) == FZGX_STATUS_OK);
  memset(&copy, 0, sizeof(copy));
  assert(fzgx_sim_world_get_machine_snapshot(&world, 0u, &copy) == FZGX_STATUS_OK);
  assert(memcmp(&snapshot, &copy, sizeof(snapshot)) == 0);
}

static void test_finalize_machine_finish_score(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].score = 10u;
  world.machines[0].energy = 50.0f;
  world.machines[0].wall_hit_count = 3u;
  world.machines[0].boost_count = 0u;
  world.machines[0].dash_plate_hit_count = 0u;
  world.machines[0].clean_race_bonus_eligible = 1u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_ZERO_HP;

  assert(fzgx_sim_finalize_machine_finish_score(&world, 0u) == FZGX_STATUS_OK);
  assert(world.machines[0].score == (uint16_t)(10u + 25u + 25u + 40u + 25u + 25u - 3u));
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_COMPLETED_RACE) != 0u);
}

static void test_apply_machine_track_sample_current_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&sample, 0, sizeof(sample));
  sample.checkpoint_index = 24;
  sample.checkpoint_fraction = 0.75f;
  sample.active_bank_cp_idx[0] = 24;
  sample.active_bank_cp_idx[1] = 25;
  sample.active_bank_cp_idx[2] = 26;
  sample.active_bank_cp_idx[3] = 27;
  sample.active_bank_cp_frac[0] = 0.75f;
  sample.active_bank_cp_frac[1] = 0.25f;
  sample.active_bank_cp_frac[2] = 0.5f;
  sample.active_bank_cp_frac[3] = 0.875f;
  sample.segment_index = 7;
  sample.flags = 0x02200000u;
  sample.cached_frame_count = 3u;
  sample.selected_cached_frame_index = 2;
  sample.checkpoint_world_pos.x = 1.0f;
  sample.checkpoint_world_pos.y = 2.0f;
  sample.checkpoint_world_pos.z = 3.0f;
  sample.track_current_transform.basis_x_x = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_z = 1.0f;
  sample.track_current_transform.origin.x = 11.0f;
  sample.track_current_transform.origin.y = 12.0f;
  sample.track_current_transform.origin.z = 13.0f;
  sample.track_current_scale.x = 1.5f;
  sample.track_current_scale.y = 2.5f;
  sample.track_current_scale.z = 3.5f;
  sample.track_scl_x = 4.0f;
  sample.track_scl_y = 6.0f;
  sample.track_anchor.x = 7.0f;
  sample.track_anchor.y = 8.0f;
  sample.track_anchor.z = 9.0f;
  sample.track_forward.x = 0.0f;
  sample.track_forward.y = 0.0f;
  sample.track_forward.z = 1.0f;
  sample.track_up.x = 10.0f;
  sample.track_up.y = 20.0f;
  sample.track_up.z = 30.0f;
  sample.track_width_or_radius = 5.0f;
  sample.track_hcylin = 2.0f;
  sample.track_follow_offset.x = 40.0f;
  sample.track_follow_offset.y = 50.0f;
  sample.track_follow_offset.z = 60.0f;
  sample.angle_from_track_forward = 999.0f;
  sample.lap_progress_fraction = 123.5f;
  sample.last_frac_diff = 8000.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.lap_cross_cp = 0;
  world.machines[0].position.x = 3.0f;
  world.machines[0].position.y = 4.0f;
  world.machines[0].position.z = 5.0f;

  assert(fzgx_sim_world_apply_machine_track_sample(&world, 0u, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.stable_cp_idx[0] == 24);
  assert(world.machines[0].track_state.stable_cp_frac[0] == 0.75f);
  assert(world.machines[0].track_state.stable_cp_idx[1] == 25);
  assert(world.machines[0].track_state.stable_cp_frac[1] == 0.25f);
  assert(world.machines[0].track_state.stable_cp_idx[2] == 26);
  assert(world.machines[0].track_state.stable_cp_frac[2] == 0.5f);
  assert(world.machines[0].track_state.stable_cp_idx[3] == 27);
  assert(world.machines[0].track_state.stable_cp_frac[3] == 0.875f);
  assert(world.machines[0].track_state.cur_cp_idx == 24);
  assert(world.machines[0].track_state.cur_cp_frac == 0.75f);
  assert(world.machines[0].track_state.next_cp_idx == -1);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == -1);
  assert(world.machines[0].track_state.flags == 0x02200000u);
  assert(world.machines[0].track_state.cached_frame_count == 3u);
  assert(world.machines[0].track_state.selected_cached_frame_index == 2);
  assert(world.machines[0].track_state.track_current_transform.origin.x == 11.0f);
  assert(world.machines[0].track_state.track_current_scale.y == 2.5f);
  assert(world.machines[0].track_state.track_scl_x == 4.0f);
  assert(world.machines[0].track_state.track_anchor.z == 9.0f);
  assert(world.machines[0].track_state.last_track_pos.x == 0.0f);
  assert(world.machines[0].track_state.last_track_pos.y == 0.0f);
  assert(world.machines[0].track_state.last_track_pos.z == 1.0f);
  assert(world.machines[0].track_state.track_up.y == 20.0f);
  assert(world.machines[0].track_state.track_width_or_radius == 5.0f);
  assert(world.machines[0].track_state.track_hcylin == 2.0f);
  assert(world.machines[0].track_state.track_follow_offset.z == 60.0f);
  assert(world.machines[0].track_state.angle_from_track_forward == -8.0f);
  assert(world.machines[0].track_state.desired_dist_from_track_center == 5.0f);
  assert(world.machines[0].track_state.track_relative_yaw_angle16 == 0u);
  assert(world.machines[0].track_state.lap_progress_fraction == 123.5f);
  assert(world.machines[0].track_state.last_frac_diff == 8000.0f);
  assert(world.machines[0].track_state.last_seg_dist == 123.5f);
  assert(world.machines[0].track_state.last_cp_idx == 0);
}

static void test_apply_machine_track_sample_next_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].track_state.cur_cp_idx = 10;
  world.machines[0].track_state.cur_cp_frac = 0.5f;

  memset(&sample, 0, sizeof(sample));
  sample.checkpoint_index = 11;
  sample.checkpoint_fraction = 0.25f;
  sample.active_bank_cp_idx[0] = 11;
  sample.active_bank_cp_idx[1] = 12;
  sample.active_bank_cp_idx[2] = 13;
  sample.active_bank_cp_idx[3] = 14;
  sample.active_bank_cp_frac[0] = 0.25f;
  sample.active_bank_cp_frac[1] = 0.35f;
  sample.active_bank_cp_frac[2] = 0.45f;
  sample.active_bank_cp_frac[3] = 0.55f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;

  assert(fzgx_sim_world_apply_machine_track_sample(&world, 0u, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_NEXT);
  assert(world.machines[0].track_state.stable_cp_idx[0] == 10);
  assert(world.machines[0].track_state.stable_cp_frac[0] == 0.5f);
  assert(world.machines[0].track_state.cur_cp_idx == 10);
  assert(world.machines[0].track_state.next_cp_idx == 11);
  assert(world.machines[0].track_state.next_cp_frac == 0.25f);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == 11);
  assert(world.machines[0].track_state.predictive_cp_frac[0] == 0.25f);
  assert(world.machines[0].track_state.predictive_cp_idx[1] == 12);
  assert(world.machines[0].track_state.predictive_cp_frac[1] == 0.35f);
  assert(world.machines[0].track_state.predictive_cp_idx[2] == 13);
  assert(world.machines[0].track_state.predictive_cp_frac[2] == 0.45f);
  assert(world.machines[0].track_state.predictive_cp_idx[3] == 14);
  assert(world.machines[0].track_state.predictive_cp_frac[3] == 0.55f);
}

static void test_step_machine_phase_recomputes_track_metrics(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&sample, 0, sizeof(sample));
  sample.flags = 0x02200000u;
  sample.track_current_transform.basis_x_x = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_z = 1.0f;
  sample.track_width_or_radius = 5.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;

  world.machines[0].position.x = 3.0f;
  assert(fzgx_sim_world_apply_machine_track_sample(&world, 0u, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.angle_from_track_forward == 3.0f);

  world.machines[0].position.x = 4.0f;
  assert(fzgx_sim_step_machine_phase(&world) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.angle_from_track_forward == 4.0f);
}

static void test_apply_machine_track_sample_uses_exact_cached_frame_ratio_gate(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&sample, 0, sizeof(sample));
  sample.flags = 0x01800000u;
  sample.track_current_transform.basis_x_x = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_z = 1.0f;
  sample.track_current_scale.x = 4.0f;
  sample.track_current_scale.y = 5.0f;
  sample.track_width_or_radius = 7.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;

  world.machines[0].position.x = 2.5f;
  assert(fzgx_sim_world_apply_machine_track_sample(&world, 0u, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.angle_from_track_forward == 2.5f);
  assert(world.machines[0].track_state.desired_dist_from_track_center == 7.0f);
}

static void test_commit_active_checkpoint_bank_current_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.stable_cp_idx[0] = 10;
  world.machines[0].track_state.stable_cp_idx[1] = 11;
  world.machines[0].track_state.stable_cp_idx[2] = 12;
  world.machines[0].track_state.stable_cp_idx[3] = 13;
  world.machines[0].track_state.stable_cp_frac[0] = 0.1f;
  world.machines[0].track_state.stable_cp_frac[1] = 0.2f;
  world.machines[0].track_state.stable_cp_frac[2] = 0.3f;
  world.machines[0].track_state.stable_cp_frac[3] = 0.4f;

  assert(fzgx_sim_world_commit_machine_active_checkpoint_bank(&world, 0u) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 10);
  assert(world.machines[0].track_state.cp_hist_idx[1] == 11);
  assert(world.machines[0].track_state.cp_hist_idx[2] == 12);
  assert(world.machines[0].track_state.cp_hist_idx[3] == 13);
  assert(world.machines[0].track_state.cp_hist_frac[0] == 0.1f);
  assert(world.machines[0].track_state.cp_hist_frac[1] == 0.2f);
  assert(world.machines[0].track_state.cp_hist_frac[2] == 0.3f);
  assert(world.machines[0].track_state.cp_hist_frac[3] == 0.4f);
}

static void test_commit_active_checkpoint_bank_next_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;
  world.machines[0].track_state.predictive_cp_idx[0] = 20;
  world.machines[0].track_state.predictive_cp_idx[1] = 21;
  world.machines[0].track_state.predictive_cp_idx[2] = 22;
  world.machines[0].track_state.predictive_cp_idx[3] = 23;
  world.machines[0].track_state.predictive_cp_frac[0] = 0.6f;
  world.machines[0].track_state.predictive_cp_frac[1] = 0.7f;
  world.machines[0].track_state.predictive_cp_frac[2] = 0.8f;
  world.machines[0].track_state.predictive_cp_frac[3] = 0.9f;

  assert(fzgx_sim_world_commit_machine_active_checkpoint_bank(&world, 0u) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 20);
  assert(world.machines[0].track_state.cp_hist_idx[1] == 21);
  assert(world.machines[0].track_state.cp_hist_idx[2] == 22);
  assert(world.machines[0].track_state.cp_hist_idx[3] == 23);
  assert(world.machines[0].track_state.cp_hist_frac[0] == 0.6f);
  assert(world.machines[0].track_state.cp_hist_frac[1] == 0.7f);
  assert(world.machines[0].track_state.cp_hist_frac[2] == 0.8f);
  assert(world.machines[0].track_state.cp_hist_frac[3] == 0.9f);
}

static void test_commit_machine_checkpoint_snapshot(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].current_checkpoint = 14;
  world.machines[0].checkpoint_fraction = 0.625f;
  world.machines[0].position.x = 1.0f;
  world.machines[0].position.y = 2.0f;
  world.machines[0].position.z = 3.0f;
  world.machines[0].track_state.lap_start_cp = 5;
  world.machines[0].track_state.lap_cross_cp = 4;

  assert(fzgx_sim_world_commit_machine_checkpoint_snapshot(&world, 0u) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.last_cp_idx == 14);
  assert(world.machines[0].track_state.last_cp_frac == 0.625f);
  assert(world.machines[0].track_state.last_cp_pos.x == 1.0f);
  assert(world.machines[0].track_state.last_cp_pos.y == 2.0f);
  assert(world.machines[0].track_state.last_cp_pos.z == 3.0f);
  assert(world.machines[0].track_state.prev_lap_cp == 5);
  assert(world.machines[0].track_state.prev_lap_cross_cp == 4);
}

static fzgx_current_track_query_result make_test_current_track_query_result(void) {
  fzgx_current_track_query_result query_result;
  memset(&query_result, 0, sizeof(query_result));
  query_result.checkpoint_index = 24;
  query_result.checkpoint_fraction = 0.75f;
  query_result.active_bank_cp_idx[0] = 24;
  query_result.active_bank_cp_idx[1] = 25;
  query_result.active_bank_cp_idx[2] = 26;
  query_result.active_bank_cp_idx[3] = 27;
  query_result.active_bank_cp_frac[0] = 0.75f;
  query_result.active_bank_cp_frac[1] = 0.25f;
  query_result.active_bank_cp_frac[2] = 0.5f;
  query_result.active_bank_cp_frac[3] = 0.875f;
  query_result.segment_index = 7;
  query_result.cached_frame_count = 3u;
  query_result.selected_cached_frame_index = 2;
  query_result.lap_progress_fraction = 123.5f;
  query_result.last_frac_diff = 8000.0f;
  query_result.frame.track_current_transform.basis_x_x = 1.0f;
  query_result.frame.track_current_transform.basis_y_y = 1.0f;
  query_result.frame.track_current_transform.basis_z_z = 1.0f;
  query_result.frame.track_current_scale.x = 1.5f;
  query_result.frame.track_current_scale.y = 2.5f;
  query_result.frame.track_current_scale.z = 3.5f;
  query_result.frame.track_scl_x = 4.0f;
  query_result.frame.track_scl_y = 6.0f;
  query_result.frame.track_anchor.x = 7.0f;
  query_result.frame.track_anchor.y = 8.0f;
  query_result.frame.track_anchor.z = 9.0f;
  query_result.frame.track_forward.z = 1.0f;
  query_result.frame.track_up.x = 10.0f;
  query_result.frame.track_up.y = 20.0f;
  query_result.frame.track_up.z = 30.0f;
  query_result.frame.track_width_or_radius = 5.0f;
  query_result.frame.track_hcylin = 2.0f;
  query_result.frame.track_follow_offset.x = 40.0f;
  query_result.frame.track_follow_offset.y = 50.0f;
  query_result.frame.track_follow_offset.z = 60.0f;
  query_result.frame.track_flags = 0x02200000u;
  return query_result;
}

static void test_build_current_track_sample_from_query_result(void) {
  fzgx_current_track_query_result query_result = make_test_current_track_query_result();
  fzgx_machine_track_sample sample;

  assert(
      fzgx_sim_build_current_track_sample_from_query_result(&query_result, &sample) ==
      FZGX_STATUS_OK);
  assert(sample.checkpoint_index == 24);
  assert(sample.checkpoint_fraction == 0.75f);
  assert(sample.active_bank_cp_idx[1] == 25);
  assert(sample.active_bank_cp_frac[3] == 0.875f);
  assert(sample.segment_index == 7);
  assert(sample.flags == 0x02200000u);
  assert(sample.cached_frame_count == 3u);
  assert(sample.selected_cached_frame_index == 2);
  assert(sample.track_current_scale.y == 2.5f);
  assert(sample.track_anchor.z == 9.0f);
  assert(sample.track_forward.z == 1.0f);
  assert(sample.track_up.y == 20.0f);
  assert(sample.track_width_or_radius == 5.0f);
  assert(sample.track_hcylin == 2.0f);
  assert(sample.track_follow_offset.z == 60.0f);
  assert(sample.lap_progress_fraction == 123.5f);
  assert(sample.last_frac_diff == 8000.0f);
  assert(sample.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
}

static void test_apply_current_checkpoint_query_result(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_current_checkpoint_query_result query_result;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&query_result, 0, sizeof(query_result));
  query_result.checkpoint_index = 24;
  query_result.checkpoint_fraction = 0.75f;

  world.machines[0].track_state.next_cp_idx = 25;
  world.machines[0].track_state.next_cp_frac = 0.25f;
  world.machines[0].track_state.predictive_cp_idx[1] = 26;
  world.machines[0].track_state.predictive_cp_frac[1] = 0.5f;

  assert(
      fzgx_sim_world_apply_current_checkpoint_query_result(&world, 0u, &query_result) ==
      FZGX_STATUS_OK);
  assert(world.machines[0].current_checkpoint == 24);
  assert(world.machines[0].checkpoint_fraction == 0.75f);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.cur_cp_idx == 24);
  assert(world.machines[0].track_state.cur_cp_frac == 0.75f);
  assert(world.machines[0].track_state.stable_cp_idx[0] == 24);
  assert(world.machines[0].track_state.stable_cp_frac[0] == 0.75f);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == 25);
  assert(world.machines[0].track_state.predictive_cp_frac[0] == 0.25f);
  assert(world.machines[0].track_state.predictive_cp_idx[1] == 26);
  assert(world.machines[0].track_state.predictive_cp_frac[1] == 0.5f);
}

static void test_apply_active_checkpoint_bank_result(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_active_checkpoint_bank_result bank_result;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&bank_result, 0, sizeof(bank_result));
  bank_result.checkpoint_variant_count = 2u;
  bank_result.preferred_variant_slot = 1u;
  bank_result.checkpoint_index[0] = 24;
  bank_result.checkpoint_fraction[0] = 0.75f;
  bank_result.checkpoint_index[1] = 25;
  bank_result.checkpoint_fraction[1] = 0.25f;
  bank_result.checkpoint_index[2] = 24;
  bank_result.checkpoint_fraction[2] = 0.75f;
  bank_result.checkpoint_index[3] = 24;
  bank_result.checkpoint_fraction[3] = 0.75f;

  world.machines[0].track_state.next_cp_idx = 30;
  world.machines[0].track_state.next_cp_frac = 0.5f;

  assert(
      fzgx_sim_world_apply_active_checkpoint_bank_result(&world, 0u, &bank_result) ==
      FZGX_STATUS_OK);
  assert(world.machines[0].current_checkpoint == 24);
  assert(world.machines[0].checkpoint_fraction == 0.75f);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.cur_cp_idx == 24);
  assert(world.machines[0].track_state.cur_cp_frac == 0.75f);
  assert(world.machines[0].track_state.stable_cp_idx[0] == 24);
  assert(world.machines[0].track_state.stable_cp_frac[0] == 0.75f);
  assert(world.machines[0].track_state.stable_cp_idx[1] == 25);
  assert(world.machines[0].track_state.stable_cp_frac[1] == 0.25f);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == 30);
  assert(world.machines[0].track_state.predictive_cp_frac[0] == 0.5f);
}

static void test_refresh_machine_racetrack_state_from_sample(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].track_state.need_resnap = 1u;
  world.machines[0].position.x = 10.0f;
  world.machines[0].position.y = 20.0f;
  world.machines[0].position.z = 30.0f;
  world.machines[0].current_checkpoint = 42;
  world.machines[0].checkpoint_fraction = 0.625f;
  world.machines[0].track_state.lap_start_cp = 9;
  world.machines[0].track_state.lap_cross_cp = 8;

  memset(&sample, 0, sizeof(sample));
  sample.checkpoint_index = 24;
  sample.checkpoint_fraction = 0.75f;
  sample.active_bank_cp_idx[0] = 24;
  sample.active_bank_cp_idx[1] = 25;
  sample.active_bank_cp_idx[2] = 26;
  sample.active_bank_cp_idx[3] = 27;
  sample.active_bank_cp_frac[0] = 0.75f;
  sample.active_bank_cp_frac[1] = 0.25f;
  sample.active_bank_cp_frac[2] = 0.5f;
  sample.active_bank_cp_frac[3] = 0.875f;
  sample.segment_index = 7;
  sample.flags = 0x02200000u;
  sample.cached_frame_count = 3u;
  sample.selected_cached_frame_index = 2;
  sample.track_current_transform.basis_x_x = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_z = 1.0f;
  sample.track_width_or_radius = 5.0f;
  sample.lap_progress_fraction = 123.5f;
  sample.last_frac_diff = 8000.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;

  assert(fzgx_sim_world_refresh_machine_racetrack_state_from_sample(
             &world, 0u, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.need_resnap == 0u);
  assert(world.machines[0].track_state.respawn_pos.x == 10.0f);
  assert(world.machines[0].track_state.respawn_pos.y == 20.0f);
  assert(world.machines[0].track_state.respawn_pos.z == 30.0f);
  assert(world.machines[0].track_state.last_fit_pos.x == 10.0f);
  assert(world.machines[0].track_state.last_fit_pos.y == 20.0f);
  assert(world.machines[0].track_state.last_fit_pos.z == 30.0f);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 24);
  assert(world.machines[0].track_state.cp_hist_idx[1] == 25);
  assert(world.machines[0].track_state.cp_hist_idx[2] == 26);
  assert(world.machines[0].track_state.cp_hist_idx[3] == 27);
  assert(world.machines[0].track_state.last_cp_idx == -1);
  assert(world.machines[0].track_state.last_cp_frac == 0.625f);
  assert(world.machines[0].track_state.last_cp_pos.x == 10.0f);
  assert(world.machines[0].track_state.prev_lap_cp == 9);
  assert(world.machines[0].track_state.prev_lap_cross_cp == 8);
}

static void test_refresh_machine_racetrack_state_from_query_result(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_current_track_query_result query_result = make_test_current_track_query_result();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].track_state.need_resnap = 1u;
  world.machines[0].position.x = 10.0f;
  world.machines[0].position.y = 20.0f;
  world.machines[0].position.z = 30.0f;
  world.machines[0].current_checkpoint = 42;
  world.machines[0].checkpoint_fraction = 0.625f;
  world.machines[0].track_state.lap_start_cp = 9;
  world.machines[0].track_state.lap_cross_cp = 8;

  assert(fzgx_sim_world_refresh_machine_racetrack_state_from_query_result(
             &world, 0u, &query_result) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.need_resnap == 0u);
  assert(world.machines[0].track_state.respawn_pos.x == 10.0f);
  assert(world.machines[0].track_state.last_fit_pos.y == 20.0f);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 24);
  assert(world.machines[0].track_state.cp_hist_idx[3] == 27);
  assert(world.machines[0].track_state.cur_cp_pointer == 7);
  assert(world.machines[0].track_state.last_cp_idx == -1);
  assert(world.machines[0].track_state.last_cp_frac == 0.625f);
}

static void test_reset_machine_from_current_transform_sample(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position.x = 1.0f;
  world.machines[0].position.y = 2.0f;
  world.machines[0].position.z = 3.0f;
  world.machines[0].basis_physical.basis_x_x = 1.0f;
  world.machines[0].basis_physical.basis_y_y = 1.0f;
  world.machines[0].basis_physical.basis_z_z = 1.0f;
  world.machines[0].basis_physical.origin.x = 99.0f;
  world.machines[0].energy = 12.0f;
  world.machines[0].score = 77u;
  world.machines[0].terrain_flags = 0xdeadbeefu;
  world.machines[0].branch_flags = 0x12345678u;
  world.machines[0].branch_slot = 1u;
  world.machines[0].control_profile_kind = 3u;
  world.machines[0].frames_since_start = 99u;
  world.machines[0].track_state.need_resnap = 1u;
  world.machines[0].current_checkpoint = 42;
  world.machines[0].checkpoint_fraction = 0.125f;
  world.machines[0].track_state.lap_start_cp = 6;
  world.machines[0].track_state.lap_cross_cp = 5;

  memset(&sample, 0, sizeof(sample));
  sample.checkpoint_index = 24;
  sample.checkpoint_fraction = 0.75f;
  sample.active_bank_cp_idx[0] = 24;
  sample.active_bank_cp_idx[1] = 25;
  sample.active_bank_cp_idx[2] = 26;
  sample.active_bank_cp_idx[3] = 27;
  sample.active_bank_cp_frac[0] = 0.75f;
  sample.active_bank_cp_frac[1] = 0.25f;
  sample.active_bank_cp_frac[2] = 0.5f;
  sample.active_bank_cp_frac[3] = 0.875f;
  sample.segment_index = 7;
  sample.flags = 0x02200000u;
  sample.cached_frame_count = 3u;
  sample.selected_cached_frame_index = 2;
  sample.track_current_transform.basis_x_z = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_x = -1.0f;
  sample.track_current_transform.origin.x = 11.0f;
  sample.track_current_transform.origin.y = 12.0f;
  sample.track_current_transform.origin.z = 13.0f;
  sample.track_width_or_radius = 5.0f;
  sample.lap_progress_fraction = 123.5f;
  sample.last_frac_diff = 8000.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;

  assert(fzgx_sim_world_reset_machine_from_current_transform_sample(
             &world, 0u, 216.0, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].energy == 100.0f);
  assert(world.machines[0].score == 0u);
  assert(world.machines[0].terrain_flags == 0u);
  assert(world.machines[0].branch_flags == 0u);
  assert(world.machines[0].branch_slot == 4u);
  assert(world.machines[0].control_profile_kind == 0u);
  assert(world.machines[0].frames_since_start == 0u);
  assert(world.machines[0].current_checkpoint == 0);
  assert(world.machines[0].checkpoint_fraction == 0.0f);
  assert(world.machines[0].position.x == 11.0f);
  assert(world.machines[0].position.y == 12.0f);
  assert(world.machines[0].position.z == 13.0f);
  assert(world.machines[0].surface_normal.x == 0.0f);
  assert(world.machines[0].surface_normal.y == 1.0f);
  assert(world.machines[0].surface_normal.z == 0.0f);
  assert(world.machines[0].basis_physical.origin.x == 0.0f);
  assert(world.machines[0].basis_physical.origin.y == 0.0f);
  assert(world.machines[0].basis_physical.origin.z == 0.0f);
  assert(world.machines[0].position_bottom.x == 11.0f);
  assert(world.machines[0].position_bottom.y == 11.9f);
  assert(world.machines[0].position_bottom.z == 13.0f);
  assert(
      sqrtf(
          world.machines[0].velocity.x * world.machines[0].velocity.x +
          world.machines[0].velocity.y * world.machines[0].velocity.y +
          world.machines[0].velocity.z * world.machines[0].velocity.z) >
      0.0f);
  assert(fabsf(world.machines[0].base_speed - 1.08f) < 0.0001f);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_ACTIVE) != 0u);
  assert((world.machines[0].machine_state & 0x00000400u) != 0u);
  assert(world.machines[0].input_accel == 1.0f);
  assert(world.machines[0].frames_since_start_2 == 90u);
  assert(world.machines[0].track_state.last_fit_pos.x == 11.0f);
  assert(world.machines[0].track_state.last_fit_pos.y == 12.0f);
  assert(world.machines[0].track_state.last_fit_pos.z == 13.0f);
  assert(world.machines[0].track_state.respawn_pos.x == 11.0f);
  assert(world.machines[0].track_state.respawn_pos.y == 12.0f);
  assert(world.machines[0].track_state.respawn_pos.z == 13.0f);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 24);
  assert(world.machines[0].track_state.last_cp_idx == -1);
}

static void test_reset_machine_to_ordinary_start_grid_slot(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_mat43 expected_transform = {0};
  fzgx_current_track_query_result expected_query;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 30u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&expected_query, 0, sizeof(expected_query));
  assert(
      fzgx_sim_world_build_ordinary_start_grid_slot_transform(
          &world, 5u, 0, &expected_transform) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_build_ordinary_start_grid_slot_query_result(
          &world, 5u, &expected_query) == FZGX_STATUS_OK);

  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 5u, 216.0) ==
      FZGX_STATUS_OK);
  assert(fabsf(world.machines[0].position.x - expected_transform.origin.x) < 0.0001f);
  assert(fabsf(world.machines[0].position.y - expected_transform.origin.y) < 0.0001f);
  assert(fabsf(world.machines[0].position.z - expected_transform.origin.z) < 0.0001f);
  assert(fabsf(world.machines[0].basis_physical.basis_x_x - expected_transform.basis_x_x) < 0.0001f);
  assert(fabsf(world.machines[0].basis_physical.basis_x_y - expected_transform.basis_x_y) < 0.0001f);
  assert(fabsf(world.machines[0].basis_physical.basis_x_z - expected_transform.basis_x_z) < 0.0001f);
  assert(fabsf(world.machines[0].base_speed - 1.08f) < 0.0001f);
  assert(world.machines[0].track_state.cur_cp_idx == expected_query.checkpoint_index);
  assert(fabsf(world.machines[0].track_state.cur_cp_frac - expected_query.checkpoint_fraction) < 0.0001f);
  assert(world.machines[0].track_state.cached_frame_count == expected_query.cached_frame_count);
  assert(world.machines[0].track_state.last_frac_diff == expected_query.last_frac_diff);
  assert(
      world.machines[0].track_state.selected_cached_frame_index ==
      expected_query.selected_cached_frame_index);
  assert(fabsf(world.machines[0].track_state.last_fit_pos.x - expected_transform.origin.x) < 0.0001f);
  assert(fabsf(world.machines[0].track_state.respawn_pos.z - expected_transform.origin.z) < 0.0001f);
}

static void test_build_machine_current_track_query_result_uses_cur_checkpoint_when_next_differs(void) {
  static const fzgx_track_manifest manifests[1] = {
      {1u, 2u, 1u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 0u, {0u, 0u, 0u}},
  };
  static const fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x400u, 0x100u},
      {1u, 1u, 0x450u, 0x200u},
  };
  static const fzgx_checkpoint_record checkpoints[2] = {
      {
          0.0f,
          1.0f,
          {0.0f, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
          {10.0f, {-1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
          0.0f,
          10.0f,
          5.0f,
          1u,
          1u,
          0u,
      },
      {
          -100.0f,
          1.0f,
          {-100.0f, {1.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}},
          {110.0f, {-1.0f, 0.0f, 0.0f}, {110.0f, 0.0f, 0.0f}},
          10.0f,
          20.0f,
          7.0f,
          1u,
          1u,
          0u,
      },
  };
  static const fzgx_track_segment_record track_segments[2] = {
      {
          0x100u,
          0x02u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          {5.0f, 1.0f, 1.0f},
          {0.0f, 0.0f, 0.0f},
          {10.0f, 0.0f, 0.0f},
          0u,
          0u,
          0.0f,
          0.0f,
          0u,
          0u,
          0,
      },
      {
          0x200u,
          0x02u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          {7.0f, 1.0f, 1.0f},
          {0.0f, 0.0f, 0.0f},
          {100.0f, 0.0f, 0.0f},
          0u,
          0u,
          0.0f,
          0.0f,
          0u,
          0u,
          0,
      },
  };
  static const fzgx_track_course_content courses[1] = {
      {1u, 2u, 2u, 2u, 0u, 20.0f, -100.0f, 0u, 0, track_nodes, checkpoints, track_segments, 0},
  };
  fzgx_content_bundle bundle = {0};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_current_track_query_result expected_query = {0};
  fzgx_current_track_query_result query = {0};
  fzgx_vec3 point = {5.0f, 0.0f, 0.0f};

  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  bundle.machine_count = 1u;
  bundle.track_course_count = 1u;
  bundle.tracks = manifests;
  bundle.machines = TEST_MACHINES;
  bundle.track_courses = courses;

  assert(fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
             &courses[0],
             0,
             manifests[0].authored_track_id,
             manifests[0].circuit_type,
             &point,
             0,
             0.5f,
             &expected_query) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = point;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.5f;
  world.machines[0].track_state.next_cp_idx = 1;
  world.machines[0].track_state.next_cp_frac = 0.5f;

  assert(
      fzgx_sim_world_build_machine_current_track_query_result(&world, 0u, &query) ==
      FZGX_STATUS_OK);
  assert(query.checkpoint_index == expected_query.checkpoint_index);
  assert(fabsf(query.checkpoint_fraction - expected_query.checkpoint_fraction) < 0.0001f);
  assert(fabsf(query.frame.track_current_transform.origin.x -
               expected_query.frame.track_current_transform.origin.x) < 0.0001f);
  assert(fabsf(query.frame.track_width_or_radius - expected_query.frame.track_width_or_radius) <
         0.0001f);
  assert(query.frame.track_flags == expected_query.frame.track_flags);
}

static void test_reset_machines_to_ordinary_start_grid_ruby_full_field(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  static const uint32_t ruby_track_indices[] = {0u, 4u, 11u, 16u, 27u};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  uint32_t order[FZGX_SIM_MAX_MACHINES];
  uint32_t track_i;
  uint32_t slot_i;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 30u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 30u) == FZGX_STATUS_OK);

  for (slot_i = 0u; slot_i < FZGX_SIM_MAX_MACHINES; ++slot_i) {
    order[slot_i] = (FZGX_SIM_MAX_MACHINES - 1u) - slot_i;
    assert(fzgx_sim_world_seed_machine_from_content(&world, slot_i, slot_i) == FZGX_STATUS_OK);
  }

  for (track_i = 0u; track_i < sizeof(ruby_track_indices) / sizeof(ruby_track_indices[0]); ++track_i) {
    assert(fzgx_sim_world_set_track(&world, ruby_track_indices[track_i]) == FZGX_STATUS_OK);
    assert(
        fzgx_sim_world_reset_machines_to_ordinary_start_grid(&world, order, FZGX_SIM_MAX_MACHINES, 216.0) ==
        FZGX_STATUS_OK);

    for (slot_i = 0u; slot_i < FZGX_SIM_MAX_MACHINES; ++slot_i) {
      uint32_t machine_index = order[slot_i];
      fzgx_mat43 expected_transform = {0};
      fzgx_current_track_query_result expected_query;

      memset(&expected_query, 0, sizeof(expected_query));
      assert(
          fzgx_sim_world_build_ordinary_start_grid_slot_transform(
              &world, slot_i, 0, &expected_transform) == FZGX_STATUS_OK);
      assert(
          fzgx_sim_world_build_ordinary_start_grid_slot_query_result(
              &world, slot_i, &expected_query) == FZGX_STATUS_OK);

      assert(fabsf(world.machines[machine_index].position.x - expected_transform.origin.x) < 0.0001f);
      assert(fabsf(world.machines[machine_index].position.y - expected_transform.origin.y) < 0.0001f);
      assert(fabsf(world.machines[machine_index].position.z - expected_transform.origin.z) < 0.0001f);
      assert(fabsf(world.machines[machine_index].base_speed - 1.08f) < 0.0001f);
      assert(world.machines[machine_index].track_state.rank_this_frame == (uint8_t)slot_i);
      assert(world.machines[machine_index].track_state.cur_cp_idx == expected_query.checkpoint_index);
      assert(
          fabsf(
              world.machines[machine_index].track_state.cur_cp_frac -
              expected_query.checkpoint_fraction) < 0.0001f);
      assert(
          world.machines[machine_index].track_state.cached_frame_count ==
          expected_query.cached_frame_count);
      assert(
          world.machines[machine_index].track_state.last_frac_diff ==
          expected_query.last_frac_diff);
      assert(
          world.machines[machine_index].track_state.selected_cached_frame_index ==
          expected_query.selected_cached_frame_index);
    }
  }
}

static void test_refresh_machine_track_fit_transform_generic_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  test_spherecast_callback_state callback_state;
  fzgx_mat43 fitted_transform;
  uint32_t expected_preferred_mask;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 5u;
  callback_state.results[0].has_hit = true;
  callback_state.results[0].hit_point = (fzgx_vec3){10.0f, 20.0f, 30.0f};
  callback_state.results[0].checkpoint_index = 24;
  callback_state.results[0].checkpoint_fraction = 0.5f;
  callback_state.results[0].selected_cached_frame_index = 0;
  callback_state.results[0].cached_frame_count = 3u;
  callback_state.results[1].has_hit = true;
  callback_state.results[1].hit_point = (fzgx_vec3){1.0f, 0.0f, -1.0f};
  callback_state.results[2].has_hit = true;
  callback_state.results[2].hit_point = (fzgx_vec3){-1.0f, 0.0f, -1.0f};
  callback_state.results[3].has_hit = true;
  callback_state.results[3].hit_point = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  callback_state.results[4].has_hit = true;
  callback_state.results[4].hit_point = (fzgx_vec3){-1.0f, 1.0f, 1.0f};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);

  world.machines[0].track_state.last_cp_idx = world.machines[0].track_state.cur_cp_idx;
  world.machines[0].track_state.last_cp_frac = world.machines[0].track_state.cur_cp_frac;
  world.machines[0].track_state.last_cp_pos = world.machines[0].position;
  expected_preferred_mask =
      0x00880001u |
      (0x80000000u >> (uint32_t)world.machines[0].track_state.selected_cached_frame_index);

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);

  assert(callback_state.call_count == 5u);
  assert(callback_state.first_request.flags == expected_preferred_mask);
  assert(callback_state.first_request.checkpoint_history_count != 0u);
  assert(callback_state.first_request.checkpoint_history_count <= 4u);
  assert(
      callback_state.first_request.checkpoint_history_index[0] ==
      world.machines[0].track_state.cur_cp_idx);
  assert(fabsf(callback_state.first_request.checkpoint_history_fraction[0] -
               world.machines[0].track_state.cur_cp_frac) < 0.0001f);
  assert(callback_state.last_request.flags == 0x00880001u);
  assert(fitted_transform.origin.y > 20.0f);
  assert(fitted_transform.basis_y_y > 0.5f);
  assert(world.machines[0].track_state.last_cp_idx == 24);
  assert(fabsf(world.machines[0].track_state.last_cp_frac - 0.5f) < 0.0001f);
}

static void test_refresh_machine_track_fit_transform_without_spherecast_callback_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_mat43 fitted_transform;
  fzgx_vec3 original_position;
  int32_t expected_checkpoint_index;
  float expected_checkpoint_fraction;
  float delta_length;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);

  expected_checkpoint_index = world.machines[0].track_state.cur_cp_idx;
  expected_checkpoint_fraction = world.machines[0].track_state.cur_cp_frac;
  original_position = world.machines[0].position;
  world.machines[0].track_state.last_cp_idx = expected_checkpoint_index;
  world.machines[0].track_state.last_cp_frac = expected_checkpoint_fraction;
  world.machines[0].track_state.last_cp_pos = original_position;

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);

  delta_length = sqrtf(
      (fitted_transform.origin.x - original_position.x) *
          (fitted_transform.origin.x - original_position.x) +
      (fitted_transform.origin.y - original_position.y) *
          (fitted_transform.origin.y - original_position.y) +
      (fitted_transform.origin.z - original_position.z) *
          (fitted_transform.origin.z - original_position.z));
  assert(delta_length < 20.0f);
  assert(fitted_transform.basis_y_y > 0.5f);
  assert(world.machines[0].track_state.last_cp_idx == expected_checkpoint_index);
  assert(
      fabsf(world.machines[0].track_state.last_cp_frac - expected_checkpoint_fraction) < 0.0001f);
}

static void test_refresh_machine_track_fit_transform_course23_snap_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  test_spherecast_callback_state callback_state;
  fzgx_mat43 fitted_transform;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 24u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);

  world.machines[0].track_state.last_cp_idx = 0x90;
  world.machines[0].track_state.last_cp_frac = 0.0f;
  world.machines[0].track_state.last_cp_pos = world.machines[0].position;

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);
  assert(callback_state.call_count == 2u);
  assert(world.machines[0].track_state.last_cp_idx == 0x91);
  assert(fabsf(world.machines[0].track_state.last_cp_frac - 0.1f) < 0.0001f);
}

static void test_refresh_machine_track_fit_transform_course23_snap_without_callback_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_mat43 fitted_transform;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 24u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);

  world.machines[0].track_state.last_cp_idx = 0x90;
  world.machines[0].track_state.last_cp_frac = 0.0f;
  world.machines[0].track_state.last_cp_pos = world.machines[0].position;

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);
  assert(fitted_transform.basis_y_y > 0.5f);
  assert(world.machines[0].track_state.last_cp_idx == 0x91);
  assert(fabsf(world.machines[0].track_state.last_cp_frac - 0.1f) < 0.0001f);
}

static void test_refresh_machine_track_fit_transform_course9_window_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  test_spherecast_callback_state callback_state;
  fzgx_mat43 fitted_transform;
  int32_t checkpoint_index = -1;
  float checkpoint_fraction = 0.0f;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 5u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 5u, &course) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_track_course_find_checkpoint_for_track_distance(
          course, 16362.0f, 0, &checkpoint_index, &checkpoint_fraction) == FZGX_STATUS_OK);

  world.machines[0].track_state.last_cp_idx = checkpoint_index;
  world.machines[0].track_state.last_cp_frac = checkpoint_fraction;
  world.machines[0].track_state.last_cp_pos = world.machines[0].position;

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);
  assert(callback_state.call_count == 2u);
  assert(fabsf(test_shared_checkpoint_distance(
                   course,
                   world.machines[0].track_state.last_cp_idx,
                   world.machines[0].track_state.last_cp_frac) -
               16371.0f) < 0.01f);
}

static void test_refresh_machine_track_fit_transform_course13_window_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  test_spherecast_callback_state callback_state;
  fzgx_mat43 fitted_transform;
  int32_t checkpoint_index = -1;
  float checkpoint_fraction = 0.0f;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 8u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 8u, &course) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_track_course_find_checkpoint_for_track_distance(
          course, 6460.0f, 0, &checkpoint_index, &checkpoint_fraction) == FZGX_STATUS_OK);

  world.machines[0].track_state.last_cp_idx = checkpoint_index;
  world.machines[0].track_state.last_cp_frac = checkpoint_fraction;
  world.machines[0].track_state.last_cp_pos = world.machines[0].position;

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);
  assert(callback_state.call_count == 2u);
  assert(fabsf(test_shared_checkpoint_distance(
                   course,
                   world.machines[0].track_state.last_cp_idx,
                   world.machines[0].track_state.last_cp_frac) -
               6506.0f) < 0.01f);
}

static void test_refresh_machine_track_fit_transform_course15_window_exact(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  test_spherecast_callback_state callback_state;
  fzgx_mat43 fitted_transform;
  int32_t checkpoint_index = -1;
  float checkpoint_fraction = 0.0f;

  memset(&callback_state, 0, sizeof(callback_state));
  callback_state.status = FZGX_STATUS_OK;
  callback_state.result_count = 2u;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_set_spherecast_callback(
          &world, test_spherecast_callback, &callback_state) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 10u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 10u, &course) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_track_course_find_checkpoint_for_track_distance(
          course, 16960.0f, 0, &checkpoint_index, &checkpoint_fraction) == FZGX_STATUS_OK);

  world.machines[0].track_state.last_cp_idx = checkpoint_index;
  world.machines[0].track_state.last_cp_frac = checkpoint_fraction;
  world.machines[0].track_state.last_cp_pos = world.machines[0].position;

  assert(
      fzgx_sim_world_refresh_machine_track_fit_transform(&world, 0u, &fitted_transform) ==
      FZGX_STATUS_OK);
  assert(callback_state.call_count == 2u);
  assert(fabsf(test_shared_checkpoint_distance(
                   course,
                   world.machines[0].track_state.last_cp_idx,
                   world.machines[0].track_state.last_cp_frac) -
               16980.0f) < 0.01f);
}

static void test_step_frame_phase_reseeds_active_checkpoint_state_from_builtin_course(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_current_checkpoint_query_result expected_checkpoint;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 5u, 0.0) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 0u, &course) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_track_course_build_shared_checkpoint_query_result(
          course,
          bundle->tracks[0].authored_track_id,
          bundle->tracks[0].circuit_type,
          &world.machines[0].position,
          &expected_checkpoint) == FZGX_STATUS_OK);

  world.machines[0].track_state.need_resnap = 1u;
  world.machines[0].track_state.cur_cp_idx = -1;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.last_cp_idx = -1;
  world.machines[0].track_state.last_cp_frac = 0.0f;
  world.machines[0].track_state.last_cp_pos = (fzgx_vec3){0};
  world.machines[0].track_state.cp_hist_idx[0] = -1;
  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;

  assert(fzgx_sim_step_frame_phase(&world, 0) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.need_resnap == 0u);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.cur_cp_idx == expected_checkpoint.checkpoint_index);
  assert(
      fabsf(
          world.machines[0].track_state.cur_cp_frac - expected_checkpoint.checkpoint_fraction) <
      0.0001f);
  assert(world.machines[0].track_state.stable_cp_idx[0] == expected_checkpoint.checkpoint_index);
  assert(world.machines[0].track_state.cp_hist_idx[0] == expected_checkpoint.checkpoint_index);
  assert(world.machines[0].track_state.last_cp_idx == -1);
  assert(
      fabsf(
          world.machines[0].track_state.respawn_pos.x - world.machines[0].position.x) < 0.0001f);
  assert(
      fabsf(
          world.machines[0].track_state.respawn_pos.y - world.machines[0].position.y) < 0.0001f);
  assert(
      fabsf(
          world.machines[0].track_state.respawn_pos.z - world.machines[0].position.z) < 0.0001f);
}

static void test_step_frame_phase_commits_stable_checkpoint_state_from_builtin_course(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_current_track_query_result expected_query;
  fzgx_current_checkpoint_query_result expected_checkpoint;
  fzgx_active_checkpoint_bank_result expected_bank;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_build_ordinary_start_grid_slot_query_result(
          &world, 5u, &expected_query) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 5u, 0.0) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 0u, &course) ==
      FZGX_STATUS_OK);

  world.machines[0].current_checkpoint = expected_query.checkpoint_index;
  world.machines[0].checkpoint_fraction = expected_query.checkpoint_fraction;
  world.machines[0].zero_minus_height_above_track = 1.0f;
  world.machines[0].track_state.cur_cp_idx = expected_query.checkpoint_index - 1;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;
  world.machines[0].track_state.next_cp_idx = expected_query.checkpoint_index - 1;
  world.machines[0].track_state.next_cp_frac = 0.0f;
  world.machines[0].track_state.cp_hist_idx[0] = -1;
  world.machines[0].track_state.last_cp_idx = -1;
  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  assert(
      fzgx_track_course_build_seeded_checkpoint_query_result_with_fallback(
          course,
          bundle->tracks[0].authored_track_id,
          bundle->tracks[0].circuit_type,
          &world.machines[0].position,
          expected_query.checkpoint_index,
          &expected_checkpoint) == FZGX_STATUS_OK);
  assert(
      fzgx_track_course_build_active_checkpoint_bank_for_point(
          course,
          bundle->tracks[0].authored_track_id,
          bundle->tracks[0].circuit_type,
          &world.machines[0].position,
          expected_checkpoint.checkpoint_index,
          expected_checkpoint.checkpoint_fraction,
          &expected_bank) == FZGX_STATUS_OK);

  assert(fzgx_sim_step_frame_phase(&world, 0) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.cur_cp_idx == expected_checkpoint.checkpoint_index);
  assert(
      fabsf(
          world.machines[0].track_state.cur_cp_frac - expected_checkpoint.checkpoint_fraction) <
      0.0001f);
  assert(world.machines[0].track_state.cp_hist_idx[0] == expected_checkpoint.checkpoint_index);
  assert(world.machines[0].track_state.cur_cp_pointer == expected_bank.containment_checkpoint_index[0]);
  assert(world.machines[0].track_state.seg_index_hist[0] == expected_bank.containment_checkpoint_index[1]);
  assert(world.machines[0].track_state.neighbor_cp_idx[0] == expected_bank.checkpoint_index[1]);
  assert(
      fabsf(
          world.machines[0].track_state.neighbor_cp_frac[0] -
          expected_bank.checkpoint_fraction[1]) <
      0.0001f);
  assert(world.machines[0].track_state.cached_frame_count >= 1u);
  assert(
      world.machines[0].track_state.selected_cached_frame_index >= 0 &&
      world.machines[0].track_state.selected_cached_frame_index <
          (int32_t)world.machines[0].track_state.cached_frame_count);
  assert(
      (world.machines[0].track_state.track_current_transform.basis_x_x != 0.0f) ||
      (world.machines[0].track_state.track_current_transform.basis_x_y != 0.0f) ||
      (world.machines[0].track_state.track_current_transform.basis_x_z != 0.0f));
  assert(world.machines[0].track_state.last_cp_idx == expected_query.checkpoint_index);
  assert(
      fabsf(
          world.machines[0].track_state.last_cp_frac - expected_query.checkpoint_fraction) <
      0.0001f);
  assert(
      fabsf(
          world.machines[0].track_state.last_cp_pos.x - world.machines[0].position.x) < 0.0001f);
  assert(
      fabsf(
          world.machines[0].track_state.last_fit_pos.x - world.machines[0].position.x) < 0.0001f);
}

static void test_step_frame_phase_skips_checkpoint_writeback_on_forward_progress_guard(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  int32_t target_checkpoint_index;
  float checkpoint_distance_delta;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 5u, 0.0) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 0u, &course) ==
      FZGX_STATUS_OK);

  target_checkpoint_index = (int32_t)(bundle->tracks[0].checkpoint_count - 1u);
  checkpoint_distance_delta =
      test_shared_checkpoint_distance(course, target_checkpoint_index, 0.0f) -
      test_shared_checkpoint_distance(course, 0, 0.0f);
  assert(checkpoint_distance_delta > 1000.0f);

  world.machines[0].current_checkpoint = target_checkpoint_index;
  world.machines[0].checkpoint_fraction = 0.0f;
  world.machines[0].zero_minus_height_above_track = 1.0f;
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.cp_hist_idx[0] = -1;
  world.machines[0].track_state.last_cp_idx = -1;
  world.machines[0].track_state.last_fit_pos = world.machines[0].position;
  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;

  assert(fzgx_sim_step_frame_phase(&world, 0) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cur_cp_idx >= 0);
  assert(world.machines[0].track_state.cp_hist_idx[0] == -1);
  assert(world.machines[0].track_state.last_cp_idx == -1);
}

static void test_step_frame_phase_checkpoint_neighborhood_gate_keeps_on_track_machine_alive(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 5u, 0.0) ==
      FZGX_STATUS_OK);

  world.machines[0].current_checkpoint = world.machines[0].track_state.cur_cp_idx;
  world.machines[0].checkpoint_fraction = world.machines[0].track_state.cur_cp_frac;
  world.machines[0].zero_minus_height_above_track = 1.0f;
  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;

  assert(fzgx_sim_step_frame_phase(&world, 0) == FZGX_STATUS_OK);
  assert((world.machines[0].machine_state & 0x800u) == 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) == 0u);
}

static void test_step_frame_phase_checkpoint_neighborhood_gate_marks_far_machine_fallout(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 5u, 0.0) ==
      FZGX_STATUS_OK);

  world.machines[0].position.x += 100000.0f;
  world.machines[0].position.y -= 100000.0f;
  world.machines[0].position.z += 100000.0f;
  world.machines[0].current_checkpoint = world.machines[0].track_state.cur_cp_idx;
  world.machines[0].checkpoint_fraction = world.machines[0].track_state.cur_cp_frac;
  world.machines[0].zero_minus_height_above_track = 1.0f;
  world.machines[0].state_2 |= 8u;
  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;

  assert(fzgx_sim_step_frame_phase(&world, 0) == FZGX_STATUS_OK);
  assert((world.machines[0].machine_state & 0x800u) != 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
}

static void test_reset_machine_from_explicit_transform_sample(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;
  fzgx_mat43 placement_transform = {0};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].track_state.need_resnap = 1u;
  placement_transform.basis_x_x = 1.0f;
  placement_transform.basis_y_y = 1.0f;
  placement_transform.basis_z_z = 1.0f;
  placement_transform.origin.x = 101.0f;
  placement_transform.origin.y = 202.0f;
  placement_transform.origin.z = 303.0f;

  memset(&sample, 0, sizeof(sample));
  sample.checkpoint_index = 24;
  sample.checkpoint_fraction = 0.75f;
  sample.active_bank_cp_idx[0] = 24;
  sample.active_bank_cp_idx[1] = 25;
  sample.active_bank_cp_idx[2] = 26;
  sample.active_bank_cp_idx[3] = 27;
  sample.active_bank_cp_frac[0] = 0.75f;
  sample.active_bank_cp_frac[1] = 0.25f;
  sample.active_bank_cp_frac[2] = 0.5f;
  sample.active_bank_cp_frac[3] = 0.875f;
  sample.segment_index = 7;
  sample.flags = 0x02200000u;
  sample.cached_frame_count = 3u;
  sample.selected_cached_frame_index = 2;
  sample.track_current_transform.basis_x_x = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_z = 1.0f;
  sample.track_current_transform.origin.x = 11.0f;
  sample.track_current_transform.origin.y = 12.0f;
  sample.track_current_transform.origin.z = 13.0f;
  sample.track_width_or_radius = 5.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;

  assert(fzgx_sim_world_reset_machine_from_transform_sample(
             &world, 0u, &placement_transform, 216.0, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].position.x == 101.0f);
  assert(world.machines[0].position.y == 202.0f);
  assert(world.machines[0].position.z == 303.0f);
  assert(world.machines[0].basis_physical.origin.x == 0.0f);
  assert(world.machines[0].track_state.track_current_transform.origin.x == 11.0f);
  assert(world.machines[0].track_state.track_current_transform.origin.y == 12.0f);
  assert(world.machines[0].track_state.track_current_transform.origin.z == 13.0f);
  assert(world.machines[0].track_state.last_fit_pos.x == 101.0f);
  assert(world.machines[0].track_state.respawn_pos.z == 303.0f);
  assert(
      sqrtf(
          world.machines[0].velocity.x * world.machines[0].velocity.x +
          world.machines[0].velocity.y * world.machines[0].velocity.y +
          world.machines[0].velocity.z * world.machines[0].velocity.z) >
      0.0f);
}

static void test_reset_machine_from_current_transform_query_result(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_current_track_query_result query_result = make_test_current_track_query_result();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position.x = 1.0f;
  world.machines[0].position.y = 2.0f;
  world.machines[0].position.z = 3.0f;
  world.machines[0].basis_physical.basis_x_x = 1.0f;
  world.machines[0].basis_physical.basis_y_y = 1.0f;
  world.machines[0].basis_physical.basis_z_z = 1.0f;
  world.machines[0].track_state.need_resnap = 1u;

  assert(fzgx_sim_world_reset_machine_from_current_transform_query_result(
             &world, 0u, 216.0, &query_result) == FZGX_STATUS_OK);
  assert(
      sqrtf(
          world.machines[0].velocity.x * world.machines[0].velocity.x +
          world.machines[0].velocity.y * world.machines[0].velocity.y +
          world.machines[0].velocity.z * world.machines[0].velocity.z) >
      0.0f);
  assert(fabsf(world.machines[0].base_speed - 1.08f) < 0.0001f);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 24);
  assert(world.machines[0].track_state.last_cp_idx == -1);
}

static void test_reset_machine_track_state_normal_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].track_state.track_current_transform.origin.x = 5.0f;
  world.machines[0].track_state.flags = 0x02200000u;
  world.machines[0].track_state.need_resnap = 99u;
  world.machines[0].track_state.cached_frame_count = 7u;
  world.machines[0].track_state.selected_cached_frame_index = 3;
  world.machines[0].track_state.cur_cp_pointer = 12;
  world.machines[0].track_state.seg_index_hist[0] = 9;
  world.machines[0].track_state.last_fit_pos.z = 8.0f;
  world.machines[0].track_state.cur_cp_idx = 10;
  world.machines[0].track_state.neighbor_cp_idx[1] = 11;
  world.machines[0].track_state.cur_cp_frac = 0.25f;
  world.machines[0].track_state.next_cp_idx = 12;
  world.machines[0].track_state.next_cp_frac = 0.75f;
  world.machines[0].track_state.last_seg_dist = 14.0f;
  world.machines[0].track_state.last_frac_diff = 15.0f;
  world.machines[0].track_state.lap_progress_fraction = 16.0f;
  world.machines[0].track_state.track_relative_yaw_angle16 = 17u;
  world.machines[0].track_state.angle_from_track_forward = 18.0f;
  world.machines[0].track_state.rank_this_frame = 19u;
  world.machines[0].track_state.lap_start_cp = 20;
  world.machines[0].track_state.lap_cross_cp = 21;
  world.machines[0].track_state.last_cp_idx = 22;
  world.machines[0].track_state.last_cp_frac = 0.875f;
  world.machines[0].track_state.last_cp_pos.x = 23.0f;
  world.machines[0].track_state.prev_lap_cp = 24;
  world.machines[0].track_state.prev_lap_cross_cp = 25;
  world.machines[0].track_state.lap_split_gate_mask = 26;
  world.machines[0].track_state.respawn_pos.y = 27.0f;

  assert(fzgx_sim_world_reset_machine_track_state(
             &world, 0u, FZGX_RACETRACK_REFRESH_NORMAL) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.track_current_transform.origin.x == 0.0f);
  assert(world.machines[0].track_state.flags == 0u);
  assert(world.machines[0].track_state.need_resnap == 0u);
  assert(world.machines[0].track_state.cached_frame_count == 0u);
  assert(world.machines[0].track_state.selected_cached_frame_index == 0);
  assert(world.machines[0].track_state.cur_cp_pointer == -1);
  assert(world.machines[0].track_state.cur_cp_idx == -1);
  assert(world.machines[0].track_state.next_cp_idx == -1);
  assert(world.machines[0].track_state.last_seg_dist == 0.0f);
  assert(world.machines[0].track_state.angle_from_track_forward == 0.0f);
  assert(world.machines[0].track_state.rank_this_frame == 0u);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.stable_cp_idx[0] == -1);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == -1);
  assert(world.machines[0].track_state.lap_start_cp == 20);
  assert(world.machines[0].track_state.lap_cross_cp == 21);
  assert(world.machines[0].track_state.last_cp_idx == 22);
  assert(world.machines[0].track_state.last_cp_frac == 0.875f);
  assert(world.machines[0].track_state.last_cp_pos.x == 23.0f);
  assert(world.machines[0].track_state.prev_lap_cp == 24);
  assert(world.machines[0].track_state.prev_lap_cross_cp == 25);
  assert(world.machines[0].track_state.lap_split_gate_mask == 26);
  assert(world.machines[0].track_state.respawn_pos.y == 27.0f);
}

static void test_reset_machine_track_state_forced_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].track_state.track_current_transform.origin.x = 5.0f;
  world.machines[0].track_state.cur_cp_idx = 10;
  world.machines[0].track_state.next_cp_idx = 11;
  world.machines[0].track_state.lap_start_cp = 20;
  world.machines[0].track_state.lap_cross_cp = 21;
  world.machines[0].track_state.last_cp_idx = 22;
  world.machines[0].track_state.prev_lap_cp = 24;
  world.machines[0].track_state.prev_lap_cross_cp = 25;
  world.machines[0].track_state.lap_split_gate_mask = 26;
  world.machines[0].track_state.respawn_pos.y = 27.0f;

  assert(fzgx_sim_world_reset_machine_track_state(
             &world, 0u, FZGX_RACETRACK_REFRESH_FORCED) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.track_current_transform.origin.x == 0.0f);
  assert(world.machines[0].track_state.need_resnap == 1u);
  assert(world.machines[0].track_state.cur_cp_pointer == -1);
  assert(world.machines[0].track_state.cur_cp_idx == -1);
  assert(world.machines[0].track_state.next_cp_idx == -1);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.stable_cp_idx[0] == -1);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == -1);
  assert(world.machines[0].track_state.lap_start_cp == 0);
  assert(world.machines[0].track_state.lap_cross_cp == -1);
  assert(world.machines[0].track_state.last_cp_idx == 0);
  assert(world.machines[0].track_state.prev_lap_cp == 0);
  assert(world.machines[0].track_state.prev_lap_cross_cp == 0);
  assert(world.machines[0].track_state.lap_split_gate_mask == -1);
  assert(world.machines[0].track_state.respawn_pos.y == 0.0f);
}

static void test_commit_machine_checkpoint_writeback_if_clean_normal_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags = FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.stable_cp_idx[0] = 30;
  world.machines[0].track_state.stable_cp_frac[0] = 0.3f;
  world.machines[0].current_checkpoint = 31;
  world.machines[0].checkpoint_fraction = 0.4f;
  world.machines[0].position.x = 4.0f;
  world.machines[0].position.y = 5.0f;
  world.machines[0].position.z = 6.0f;
  world.machines[0].track_state.lap_start_cp = 7;
  world.machines[0].track_state.lap_cross_cp = 6;

  assert(fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
             &world, 0u, FZGX_RACETRACK_REFRESH_NORMAL, false) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 30);
  assert(world.machines[0].track_state.cp_hist_frac[0] == 0.3f);
  assert(world.machines[0].track_state.last_cp_idx == 31);
  assert(world.machines[0].track_state.last_cp_frac == 0.4f);
  assert(world.machines[0].track_state.last_cp_pos.x == 4.0f);
  assert(world.machines[0].track_state.prev_lap_cp == 7);
  assert(world.machines[0].track_state.prev_lap_cross_cp == 6);
}

static void test_commit_machine_checkpoint_writeback_if_clean_forced_mode(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags = 0u;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;
  world.machines[0].track_state.cur_cp_idx = 39;
  world.machines[0].track_state.cur_cp_frac = 0.55f;
  world.machines[0].track_state.predictive_cp_idx[0] = 40;
  world.machines[0].track_state.predictive_cp_frac[0] = 0.6f;
  world.machines[0].current_checkpoint = 41;
  world.machines[0].checkpoint_fraction = 0.7f;

  assert(fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
             &world, 0u, FZGX_RACETRACK_REFRESH_FORCED, false) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 40);
  assert(world.machines[0].track_state.cp_hist_frac[0] == 0.6f);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.next_cp_idx == -1);
  assert(world.machines[0].track_state.next_cp_frac == 0.0f);
  assert(world.machines[0].track_state.stable_cp_idx[0] == 39);
  assert(world.machines[0].track_state.stable_cp_frac[0] == 0.55f);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == -1);
  assert(world.machines[0].track_state.predictive_cp_frac[0] == 0.0f);
  assert(world.machines[0].track_state.last_cp_idx == -1);
  assert(world.machines[0].track_state.last_cp_frac == 0.7f);
}

static void test_commit_machine_checkpoint_writeback_skips_fallout(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags =
      FZGX_MACHINE_FLAG_SIM_MOTION_RAN | FZGX_MACHINE_FLAG_FALLOUT;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.stable_cp_idx[0] = 50;
  world.machines[0].current_checkpoint = 51;

  assert(fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
             &world, 0u, FZGX_RACETRACK_REFRESH_NORMAL, false) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 0);
  assert(world.machines[0].track_state.last_cp_idx == 0);
}

static void test_commit_machine_checkpoint_writeback_skips_normal_airborne_or_not_ready(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags = FZGX_MACHINE_FLAG_AIRBORNE;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.stable_cp_idx[0] = 60;
  world.machines[0].current_checkpoint = 61;

  assert(fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
             &world, 0u, FZGX_RACETRACK_REFRESH_NORMAL, false) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 0);
  assert(world.machines[0].track_state.last_cp_idx == 0);
}

static void test_commit_machine_checkpoint_writeback_skips_forward_progress_guard(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags = FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;
  world.machines[0].track_state.stable_cp_idx[0] = 70;
  world.machines[0].current_checkpoint = 71;

  assert(fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
             &world, 0u, FZGX_RACETRACK_REFRESH_NORMAL, true) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.cp_hist_idx[0] == 0);
  assert(world.machines[0].track_state.last_cp_idx == 0);
}

static void test_commit_machine_checkpoint_writeback_forced_mode_still_invalidates_last_cp_idx(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags = FZGX_MACHINE_FLAG_FALLOUT;
  world.machines[0].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;
  world.machines[0].track_state.cur_cp_idx = 98;
  world.machines[0].track_state.cur_cp_frac = 0.125f;
  world.machines[0].track_state.next_cp_idx = 99;
  world.machines[0].track_state.next_cp_frac = 0.25f;
  world.machines[0].track_state.last_cp_idx = 99;

  assert(fzgx_sim_world_commit_machine_checkpoint_writeback_if_clean(
             &world, 0u, FZGX_RACETRACK_REFRESH_FORCED, false) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_CURRENT);
  assert(world.machines[0].track_state.next_cp_idx == -1);
  assert(world.machines[0].track_state.next_cp_frac == 0.0f);
  assert(world.machines[0].track_state.stable_cp_idx[0] == 98);
  assert(world.machines[0].track_state.predictive_cp_idx[0] == -1);
  assert(world.machines[0].track_state.last_cp_idx == -1);
}

static void test_open_circuit_progress_ignores_lap_cross_cp(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_machine_track_sample sample;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  memset(&sample, 0, sizeof(sample));
  sample.flags = 0x02200000u;
  sample.track_current_transform.basis_x_x = 1.0f;
  sample.track_current_transform.basis_y_y = 1.0f;
  sample.track_current_transform.basis_z_z = 1.0f;
  sample.track_width_or_radius = 5.0f;
  sample.lap_progress_fraction = 50.0f;
  sample.last_frac_diff = 100.0f;
  sample.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_CURRENT;

  world.machines[0].track_state.lap_cross_cp = 3;

  assert(fzgx_sim_world_apply_machine_track_sample(&world, 0u, &sample) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.last_seg_dist == 50.0f);
}

static void test_step_frame_phase_open_track_does_not_double_increment_lap_timer(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const uint32_t open_track_index = 27u;
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation = 0;
  fzgx_current_track_query_result query = {0};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_race_step_options options = {true, false, false, false};
  fzgx_vec3 selection_point = {0};

  assert(bundle->tracks[open_track_index].authored_track_id == 38u);
  assert(bundle->tracks[open_track_index].circuit_type == FZGX_CIRCUIT_TYPE_OPEN);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, open_track_index, &course) ==
      FZGX_STATUS_OK);
  assert(fzgx_content_bundle_get_track_course_animation_for_track_index(
             bundle, open_track_index, &animation) == FZGX_STATUS_OK);
  assert(fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
             course,
             animation,
             bundle->tracks[open_track_index].authored_track_id,
             bundle->tracks[open_track_index].circuit_type,
             &selection_point,
             0,
             0.0f,
             &query) == FZGX_STATUS_OK);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, open_track_index) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].state_2 |= 8u;
  world.machines[0].position = query.frame.track_current_transform.origin;
  world.machines[0].position_old = world.machines[0].position;
  assert(fzgx_sim_world_refresh_machine_racetrack_state_from_query_result(
             &world, 0u, &query) == FZGX_STATUS_OK);
  world.machines[0].current_checkpoint = 0;
  world.machines[0].checkpoint_fraction = 0.0f;
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.stable_cp_idx[0] = 0;
  world.machines[0].track_state.stable_cp_frac[0] = 0.0f;
  world.machines[0].track_state.last_cp_idx = 0;
  world.machines[0].track_state.lap_start_cp = 0;
  world.machines[0].track_state.lap_cross_cp = 0;
  world.machines[0].track_state.prev_lap_cp = 0;
  world.machines[0].track_state.prev_lap_cross_cp = 0;
  world.machines[0].track_state.lap_time_frames = 10;
  world.machines[0].track_state.lap_time_fraction = 0.0f;
  world.machines[0].position.z = 1.0f;
  world.machines[0].position_old.z = 1.0f;
  world.machines[0].track_state.respawn_pos = world.machines[0].position;
  world.machines[0].track_state.respawn_pos.z = 2.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.lap_cross_cp == 0);
  assert(world.machines[0].track_state.lap_time_frames == 11);
  assert(fabsf(world.machines[0].track_state.lap_time_fraction) < 0.0001f);
  assert(world.machines[0].track_state.history_time_frames == 11);
  assert(fabsf(world.machines[0].track_state.history_time_fraction) < 0.0001f);
  assert(world.machines[0].track_state.last_seg_dist ==
         world.machines[0].track_state.lap_progress_fraction);
}

static void test_step_frame_phase_uses_current_neighbor_fraction_tail_for_lap_progress(void) {
  static const fzgx_track_manifest manifests[1] = {
      {1u, 1u, 1u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 0u, {0u, 0u, 0u}},
  };
  static const fzgx_track_node_record track_nodes[1] = {
      {3u, 0u, 0u, 0u},
  };
  static const fzgx_checkpoint_record checkpoints[3] = {
      {
          0.0f,
          1.0f,
          {0.0f, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
          {10.0f, {-1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
          0.0f,
          10.0f,
          5.0f,
          1u,
          1u,
          0u,
      },
      {
          10.0f,
          1.0f,
          {0.0f, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
          {10.0f, {-1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
          10.0f,
          20.0f,
          5.0f,
          1u,
          1u,
          0u,
      },
      {
          20.0f,
          1.0f,
          {0.0f, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
          {10.0f, {-1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
          20.0f,
          30.0f,
          5.0f,
          1u,
          1u,
          0u,
      },
  };
  static const fzgx_track_course_content courses[1] = {
      {1u, 1u, 3u, 0u, 0u, 30.0f, -100.0f, 0u, 0, track_nodes, checkpoints, 0, 0},
  };
  fzgx_content_bundle bundle = {0};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_current_checkpoint_query_result expected_checkpoint = {0};
  fzgx_active_checkpoint_bank_result expected_bank = {0};
  fzgx_race_step_options options = {false, false, false, false};
  fzgx_vec3 position = {5.0f, 0.0f, 0.0f};
  float stale_fraction;
  float expected_lap_progress;

  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  bundle.machine_count = 1u;
  bundle.track_course_count = 1u;
  bundle.tracks = manifests;
  bundle.machines = TEST_MACHINES;
  bundle.track_courses = courses;

  assert(
      fzgx_track_course_build_seeded_checkpoint_query_result_with_fallback(
          courses,
          manifests[0].authored_track_id,
          manifests[0].circuit_type,
          &position,
          0,
          &expected_checkpoint) == FZGX_STATUS_OK);
  assert(
      fzgx_track_course_build_active_checkpoint_bank_for_point(
          courses,
          manifests[0].authored_track_id,
          manifests[0].circuit_type,
          &position,
          expected_checkpoint.checkpoint_index,
          expected_checkpoint.checkpoint_fraction,
          &expected_bank) == FZGX_STATUS_OK);
  assert(expected_bank.checkpoint_variant_count == 3u);

  stale_fraction = expected_bank.checkpoint_fraction[2] + 0.25f;
  if (1.0f < stale_fraction) {
    stale_fraction = expected_bank.checkpoint_fraction[2] - 0.25f;
  }
  assert(fabsf(stale_fraction - expected_bank.checkpoint_fraction[2]) > 0.01f);

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].position = position;
  world.machines[0].position_old = position;
  world.machines[0].track_state.respawn_pos = position;
  world.machines[0].current_checkpoint = 0;
  world.machines[0].checkpoint_fraction = 0.0f;
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.stable_cp_idx[0] = 0;
  world.machines[0].track_state.stable_cp_frac[0] = 0.0f;
  world.machines[0].track_state.stable_cp_idx[2] = expected_bank.checkpoint_index[2];
  world.machines[0].track_state.stable_cp_frac[2] = stale_fraction;
  world.machines[0].track_state.selected_cached_frame_index = 2;
  world.machines[0].track_state.last_cp_idx = 0;
  world.machines[0].track_state.last_cp_frac = 0.0f;
  world.machines[0].track_state.lap_start_cp = 0;
  world.machines[0].track_state.lap_cross_cp = 0;
  world.machines[0].track_state.prev_lap_cp = 0;
  world.machines[0].track_state.prev_lap_cross_cp = 0;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);

  expected_lap_progress =
      checkpoints[2].start_distance +
      expected_bank.checkpoint_fraction[2] *
          (checkpoints[2].end_distance - checkpoints[2].start_distance);

  assert(world.machines[0].track_state.selected_cached_frame_index == 2);
  assert(
      fabsf(
          world.machines[0].track_state.neighbor_cp_frac[1] -
          expected_bank.checkpoint_fraction[2]) < 0.0001f);
  assert(fabsf(world.machines[0].track_state.lap_progress_fraction - expected_lap_progress) < 0.0001f);
  assert(fabsf(world.machines[0].track_state.last_seg_dist - expected_lap_progress) < 0.0001f);
  assert(
      fabsf(
          world.machines[0].track_state.lap_progress_fraction -
          (checkpoints[2].start_distance +
           stale_fraction * (checkpoints[2].end_distance - checkpoints[2].start_distance))) > 0.1f);
}

static void test_step_frame_phase_updates_facing_latch_from_track_forward(void) {
  static const fzgx_track_manifest manifests[1] = {
      {1u, 2u, 1u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 0u, {0u, 0u, 0u}},
  };
  static const fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x400u, 0x100u},
      {1u, 1u, 0x450u, 0x200u},
  };
  static const fzgx_checkpoint_record checkpoints[2] = {
      {
          0.0f,
          1.0f,
          {0.0f, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
          {10.0f, {-1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
          0.0f,
          10.0f,
          5.0f,
          1u,
          1u,
          0u,
      },
      {
          10.0f,
          1.0f,
          {10.0f, {1.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}},
          {20.0f, {-1.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}},
          10.0f,
          20.0f,
          5.0f,
          1u,
          1u,
          0u,
      },
  };
  static const fzgx_track_segment_record track_segments[2] = {
      {
          0x100u,
          0x02u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          {5.0f, 1.0f, 1.0f},
          {0.0f, 0.0f, 0.0f},
          {10.0f, 0.0f, 0.0f},
          0u,
          0u,
          0.0f,
          0.0f,
          0u,
          0u,
          0,
      },
      {
          0x200u,
          0x02u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          0u,
          {5.0f, 1.0f, 1.0f},
          {0.0f, 0.0f, 0.0f},
          {20.0f, 0.0f, 0.0f},
          0u,
          0u,
          0.0f,
          0.0f,
          0u,
          0u,
          0,
      },
  };
  static const fzgx_track_course_content courses[1] = {
      {1u, 2u, 2u, 2u, 0u, 20.0f, -100.0f, 0u, 0, track_nodes, checkpoints, track_segments, 0},
  };
  static const fzgx_track_course_animation_content animations[1] = {
      {1u, 0u, 0u, 0u, 0u, 0},
  };
  fzgx_content_bundle bundle = {0};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_race_step_options options = {false, false, false, false};

  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  bundle.machine_count = 1u;
  bundle.track_course_count = 1u;
  bundle.track_animation_course_count = 1u;
  bundle.tracks = manifests;
  bundle.machines = TEST_MACHINES;
  bundle.track_courses = courses;
  bundle.track_animations = animations;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){5.0f, 0.0f, 0.0f};
  world.machines[0].position_old = world.machines[0].position;
  world.machines[0].current_checkpoint = 0;
  world.machines[0].checkpoint_fraction = 0.5f;
  world.machines[0].state_2 |= 8u;
  world.machines[0].zero_minus_height_above_track = 1.0f;
  world.machines[0].basis_physical.basis_x = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_y = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical.basis_z = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.5f;
  world.machines[0].track_state.stable_cp_idx[0] = 0;
  world.machines[0].track_state.stable_cp_frac[0] = 0.5f;
  world.machines[0].track_state.last_cp_idx = 0;
  world.machines[0].track_state.last_cp_frac = 0.5f;
  world.machines[0].track_state.facing_counter = 29;
  world.machines[0].track_state.facing_flag = 0;
  world.machines[0].track_state.facing_toggled = 0;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.last_track_pos.x == 0.0f);
  assert(world.machines[0].track_state.last_track_pos.y == 0.0f);
  assert(world.machines[0].track_state.last_track_pos.z == -1.0f);
  assert(world.machines[0].track_state.facing_counter == 30);
  assert(world.machines[0].track_state.facing_flag == 1);
  assert(world.machines[0].track_state.facing_toggled == 1);
}

static void test_step_frame_phase_sorts_finished_and_active_machines(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 3u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 3u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 2u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_COMPLETED_RACE;
  world.machines[0].track_state.total_time_frames = 200;
  world.machines[0].track_state.last_seg_dist = 5.0f;
  world.machines[1].track_state.last_seg_dist = 90.0f;
  world.machines[2].machine_flags |= FZGX_MACHINE_FLAG_RETIRED;
  world.machines[2].track_state.last_seg_dist = 100.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.rank_this_frame == 0u);
  assert(world.machines[1].track_state.rank_this_frame == 1u);
  assert(world.machines[2].track_state.rank_this_frame == 2u);
}

static void test_step_frame_phase_orders_finished_by_total_time(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_COMPLETED_RACE;
  world.machines[0].track_state.total_time_frames = 250;
  world.machines[1].machine_flags |= FZGX_MACHINE_FLAG_COMPLETED_RACE;
  world.machines[1].track_state.total_time_frames = 200;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[1].track_state.rank_this_frame == 0u);
  assert(world.machines[0].track_state.rank_this_frame == 1u);
}

static void test_step_frame_phase_orders_finished_by_total_time_fraction(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_COMPLETED_RACE;
  world.machines[0].track_state.total_time_frames = 250;
  world.machines[0].track_state.total_time_fraction = 0.75f;
  world.machines[1].machine_flags |= FZGX_MACHINE_FLAG_COMPLETED_RACE;
  world.machines[1].track_state.total_time_frames = 250;
  world.machines[1].track_state.total_time_fraction = 0.25f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[1].track_state.rank_this_frame == 0u);
  assert(world.machines[0].track_state.rank_this_frame == 1u);
}

static void test_step_frame_phase_updates_time_extension_trigger_mask_from_motion(void) {
  static const fzgx_track_manifest manifests[1] = {
      {33u, 1u, 1u, FZGX_CIRCUIT_TYPE_CLOSED, 1u, 0u, {0u, 0u, 0u}},
  };
  static const fzgx_track_node_record track_nodes[1] = {
      {1u, 0u, 0x100u, 0u},
  };
  static const fzgx_checkpoint_record checkpoints[1] = {
      {
          0.0f,
          1.0f,
          {0.0f, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
          {0.0f, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
          0.0f,
          10.0f,
          10.0f,
          0u,
          0u,
          0u,
      },
  };
  static const fzgx_time_extension_trigger_record triggers[1] = {
      {
          {0.0f, 0.0f, 0.0f},
          0u,
          0u,
          0u,
          0u,
          0u,
          {20.0f, 20.0f, 1.0f},
          0u,
      },
  };
  static const fzgx_track_course_content courses[1] = {
      {33u, 1u, 1u, 0u, 0u, 10.0f, -100.0f, 1u, triggers, track_nodes, checkpoints, 0, 0},
  };
  fzgx_content_bundle bundle = {0};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_race_step_options options = {false, false, false, false};

  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  bundle.machine_count = 1u;
  bundle.track_course_count = 1u;
  bundle.tracks = manifests;
  bundle.machines = TEST_MACHINES;
  bundle.track_courses = courses;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  assert(world.machines[0].track_state.time_extension_trigger_mask == 0xffffffffu);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].current_checkpoint = 0;
  world.machines[0].checkpoint_fraction = 0.0f;
  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  world.machines[0].position_old = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.stable_cp_idx[0] = 0;
  world.machines[0].track_state.stable_cp_frac[0] = 0.0f;
  world.machines[0].track_state.last_cp_idx = 0;
  world.machines[0].track_state.last_cp_frac = 0.0f;
  world.machines[0].track_state.lap_start_cp = 0;
  world.machines[0].track_state.lap_cross_cp = 0;
  world.machines[0].track_state.prev_lap_cp = 0;
  world.machines[0].track_state.prev_lap_cross_cp = 0;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.time_extension_trigger_mask == 0x7fffffffu);
  assert(fzgx_sim_get_time_extension_trigger_progress_index(&world.machines[0].track_state) == 1);
}

static void test_step_frame_phase_closed_track_lap_rollover_applies_fractional_correction(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_race_step_options options = {true, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(
      fzgx_sim_world_reset_machine_to_ordinary_start_grid_slot(&world, 0u, 0u, 0.0) ==
      FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].state_2 |= 8u;
  world.machines[0].current_checkpoint = 0;
  world.machines[0].checkpoint_fraction = 0.0f;
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.stable_cp_idx[0] = 0;
  world.machines[0].track_state.stable_cp_frac[0] = 0.0f;
  world.machines[0].track_state.last_cp_idx = 0;
  world.machines[0].track_state.lap_start_cp = 0;
  world.machines[0].track_state.lap_cross_cp = 0;
  world.machines[0].track_state.prev_lap_cp = 7;
  world.machines[0].track_state.prev_lap_cross_cp = 6;
  world.machines[0].track_state.lap_time_frames = 10;
  world.machines[0].track_state.lap_time_fraction = 0.0f;
  world.machines[0].position.z = -1.0f;
  world.machines[0].track_state.respawn_pos = world.machines[0].position;
  world.machines[0].track_state.respawn_pos.z = 1.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.lap_cross_cp == 1);
  assert(world.machines[0].track_state.lap_start_cp == 1);
  assert(world.machines[0].track_state.total_time_frames == 10);
  assert(fabsf(world.machines[0].track_state.total_time_fraction - 0.5f) < 0.0001f);
  assert(world.machines[0].track_state.lap_time_frames == 0);
  assert(fabsf(world.machines[0].track_state.lap_time_fraction - 0.5f) < 0.0001f);
  assert(world.machines[0].track_state.history_time_frames == 11);
  assert(fabsf(world.machines[0].track_state.history_time_fraction) < 0.0001f);
  assert(world.machines[0].track_state.best_splits[6].frames == 10);
  assert(fabsf(world.machines[0].track_state.best_splits[6].fraction - 0.5f) < 0.0001f);
  assert(world.machines[0].track_state.best_splits[7].frames == 0);
  assert(world.machines[0].track_state.best_lap.frames == 10);
  assert(fabsf(world.machines[0].track_state.best_lap.fraction - 0.5f) < 0.0001f);
  assert(world.machines[0].track_state.best_lap.display ==
         world.machines[0].track_state.best_splits[6].display);
  assert(world.machines[0].track_state.best_lap_slot == 0u);
  assert(world.machines[0].track_state.lap_min == 0u);
  assert(world.machines[0].track_state.lap_sec == 0u);
  assert(world.machines[0].track_state.lap_centi == 8u);
  assert(world.machines[0].track_state.total_min == 0u);
  assert(world.machines[0].track_state.total_sec == 0u);
  assert(world.machines[0].track_state.total_centi == 175u);
  assert(world.machines[0].track_state.history_min == 0u);
  assert(world.machines[0].track_state.history_sec == 0u);
  assert(world.machines[0].track_state.history_centi == 183u);
  assert(world.machines[0].track_state.total_time_display != 0u);
  assert(world.machines[0].track_state.history_time_display != 0u);
  assert((world.machines[0].machine_state & 0x00040000u) != 0u);
  assert(world.machines[0].track_state.lap_split_gate_mask == -1);
  assert(world.machines[0].track_state.respawn_pos.z == -1.0f);
}

static void test_step_frame_phase_history_display_uses_component_accumulation(void) {
  static const fzgx_track_manifest manifests[1] = {
      {1u, 1u, 1u, FZGX_CIRCUIT_TYPE_CLOSED, 0u, 0u, {0u, 0u, 0u}},
  };
  static const fzgx_track_node_record track_nodes[1] = {
      {1u, 0u, 0x100u, 0u},
  };
  static const fzgx_checkpoint_record checkpoints[1] = {
      {
          0.0f,
          1.0f,
          {0.0f, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
          {0.0f, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
          0.0f,
          10.0f,
          10.0f,
          0u,
          0u,
          0u,
      },
  };
  static const fzgx_track_course_content courses[1] = {
      {1u, 1u, 1u, 0u, 0u, 10.0f, -100.0f, 0u, 0, track_nodes, checkpoints, 0, 0},
  };
  fzgx_content_bundle bundle = {0};
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_race_step_options options = {false, false, false, false};

  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  bundle.machine_count = 1u;
  bundle.track_course_count = 1u;
  bundle.tracks = manifests;
  bundle.machines = TEST_MACHINES;
  bundle.track_courses = courses;

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_SIM_MOTION_RAN;
  world.machines[0].current_checkpoint = 0;
  world.machines[0].checkpoint_fraction = 0.0f;
  world.machines[0].track_state.cur_cp_idx = 0;
  world.machines[0].track_state.cur_cp_frac = 0.0f;
  world.machines[0].track_state.stable_cp_idx[0] = 0;
  world.machines[0].track_state.stable_cp_frac[0] = 0.0f;
  world.machines[0].track_state.last_cp_idx = 0;
  world.machines[0].track_state.last_cp_frac = 0.0f;
  world.machines[0].track_state.lap_start_cp = 0;
  world.machines[0].track_state.lap_cross_cp = 0;
  world.machines[0].track_state.prev_lap_cp = 0;
  world.machines[0].track_state.prev_lap_cross_cp = 0;
  world.machines[0].track_state.total_time_frames = 0;
  world.machines[0].track_state.total_time_fraction = 0.4f;
  world.machines[0].track_state.lap_time_frames = 0;
  world.machines[0].track_state.lap_time_fraction = 0.4f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.lap_centi == 6u);
  assert(world.machines[0].track_state.total_centi == 6u);
  assert(world.machines[0].track_state.history_centi == 12u);
  assert(world.machines[0].track_state.history_time_frames == 0);
  assert(fabsf(world.machines[0].track_state.history_time_fraction - 0.8f) < 0.0001f);
  assert((world.machines[0].track_state.history_time_display & 0xfffu) == 12u);
}

static void test_step_frame_phase_handles_pairwise_machine_collision_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){-40.0f, 0.0f, 0.0f};
  world.machines[0].position_old_dupe = world.machines[0].position;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical = (fzgx_mat43){
      {0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f},
      {-1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };
  world.machines[1].position = (fzgx_vec3){40.0f, 0.0f, 0.0f};
  world.machines[1].position_old_dupe = world.machines[1].position;
  world.machines[1].velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[1].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[1].basis_physical = (fzgx_mat43){
      {0.0f, 0.0f, -1.0f},
      {0.0f, 1.0f, 0.0f},
      {1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert((world.machines[0].unk_random_0x514 & 0x40000000u) == 0u);
  assert((world.machines[1].unk_random_0x514 & 0x80000000u) == 0u);

  world.machines[0].position = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  world.machines[0].position_old_dupe = (fzgx_vec3){-2.5f, 0.0f, 0.0f};
  world.machines[0].velocity = (fzgx_vec3){4.0f, 0.0f, 0.0f};
  world.machines[1].position = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[1].position_old_dupe = (fzgx_vec3){2.5f, 0.0f, 0.0f};
  world.machines[1].velocity = (fzgx_vec3){-4.0f, 0.0f, 0.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert((world.machines[0].machine_state & 0x02000000u) != 0u);
  assert((world.machines[1].machine_state & 0x02000000u) != 0u);
  assert(world.machines[0].position.x < -1.0f);
  assert(1.0f < world.machines[1].position.x);
  assert(world.machines[0].collision_response.x < 0.0f);
  assert(0.0f < world.machines[1].collision_response.x);
  assert(world.machines[0].velocity.x < 0.0f);
  assert(0.0f < world.machines[1].velocity.x);
  assert(world.machines[0].energy < world.machines[0].max_energy);
  assert(world.machines[1].energy < world.machines[1].max_energy);
  assert(world.machines[0].last_machine_approached == 1u);
  assert(world.machines[1].last_machine_approached == 0u);
}

static void test_step_frame_phase_destroys_collision_flagged_machine_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);

  world.machines[0].position = (fzgx_vec3){-40.0f, 0.0f, 0.0f};
  world.machines[0].position_old_dupe = world.machines[0].position;
  world.machines[0].velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical = (fzgx_mat43){
      {0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f},
      {-1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };
  world.machines[1].position = (fzgx_vec3){40.0f, 0.0f, 0.0f};
  world.machines[1].position_old_dupe = world.machines[1].position;
  world.machines[1].velocity = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[1].surface_normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[1].basis_physical = (fzgx_mat43){
      {0.0f, 0.0f, -1.0f},
      {0.0f, 1.0f, 0.0f},
      {1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert((world.machines[0].unk_random_0x514 & 0x40000000u) == 0u);
  assert((world.machines[1].unk_random_0x514 & 0x80000000u) == 0u);

  world.machines[0].entrant_runtime_flags = FZGX_ENTRANT_RUNTIME_FLAG_COLLISION_DESTROY;
  world.machines[0].position = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  world.machines[0].position_old_dupe = (fzgx_vec3){-2.5f, 0.0f, 0.0f};
  world.machines[0].velocity = (fzgx_vec3){4.0f, 0.0f, 0.0f};
  world.machines[1].position = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[1].position_old_dupe = (fzgx_vec3){2.5f, 0.0f, 0.0f};
  world.machines[1].velocity = (fzgx_vec3){-4.0f, 0.0f, 0.0f};

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) != 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_ZERO_HP) != 0u);
  assert((world.machines[0].state_2 & 0x10u) != 0u);
  assert(world.machines[0].energy == 0.0f);
  assert(world.machines[0].base_speed == 0.0f);
  assert(world.machines[0].speed_kmh == 0.0f);
  assert(world.machines[0].velocity.x == 0.0f);
  assert(world.machines[0].velocity.y == 0.0f);
  assert(world.machines[0].velocity.z == 0.0f);
  assert((world.machines[0].entrant_runtime_flags &
          FZGX_ENTRANT_RUNTIME_FLAG_COLLISION_DESTROY) == 0u);
  assert((world.machines[0].entrant_runtime_flags &
          FZGX_ENTRANT_RUNTIME_FLAG_DESTROYED) != 0u);
  assert((world.machines[1].machine_flags & FZGX_MACHINE_FLAG_FALLOUT) == 0u);
}

static void test_step_frame_phase_raw_full_heal_uses_constant_hundred_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false, 1u, 0u, {0u, 0u}, 0u};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].energy = 25.0f;
  world.machines[0].max_energy = 150.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].energy == 100.0f);
}

static void test_step_frame_phase_raw_finish_score_threshold_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false, 1u, 2u, {0u, 0u}, 0x00004000u};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].score = 10u;
  world.machines[0].energy = 50.0f;
  world.machines[0].wall_hit_count = 3u;
  world.machines[0].boost_count = 0u;
  world.machines[0].dash_plate_hit_count = 0u;
  world.machines[0].clean_race_bonus_eligible = 1u;
  world.machines[0].track_state.lap_start_cp = 2;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert((world.machines[0].machine_state & 0x00010000u) != 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_COMPLETED_RACE) != 0u);
  assert(world.machines[0].score == (uint16_t)(10u + 25u + 25u + 40u + 25u - 3u));
}

static void test_step_frame_phase_raw_finish_score_is_suppressed_by_0x40_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false, 1u, 0u, {0u, 0u}, 0x00004040u};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].track_state.lap_start_cp = 0;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert((world.machines[0].machine_state & 0x00010000u) == 0u);
  assert((world.machines[0].machine_flags & FZGX_MACHINE_FLAG_COMPLETED_RACE) == 0u);
}

static void test_step_frame_phase_raw_0x8000_keeps_retired_visible_in_ranking_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false, 1u, 0u, {0u, 0u}, 0x0000c000u};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);

  world.machines[0].machine_flags |= FZGX_MACHINE_FLAG_RETIRED;
  world.machines[0].track_state.last_seg_dist = 100.0f;
  world.machines[1].track_state.last_seg_dist = 90.0f;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.rank_this_frame == 0u);
  assert(world.machines[1].track_state.rank_this_frame == 1u);
}

static void test_step_frame_phase_persistent_full_heal_latch_blocks_finish_score_exact(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {false, false, false, false, 1u, 0u, {0u, 0u}, 0x00004000u};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_race_full_heal_latch(&world, true) == FZGX_STATUS_OK);

  world.machines[0].energy = 25.0f;
  world.machines[0].track_state.lap_start_cp = 0;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].energy == 100.0f);
  assert((world.machines[0].machine_state & 0x00010000u) == 0u);
  assert(world.race_full_heal_latch_active == 1u);
  assert(world.race_full_heal_latch_persistent == 1u);
}

static void test_step_frame_phase_caps_lap_timer(void) {
  fzgx_sim_world world;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_race_step_options options = {true, false, false, false};

  fzgx_sim_world_init(&world);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 1u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 1u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);

  world.machines[0].track_state.lap_time_frames = 360000;

  assert(fzgx_sim_step_frame_phase(&world, &options) == FZGX_STATUS_OK);
  assert(world.machines[0].track_state.lap_time_frames == 360000);
}

static void test_world_snapshot_round_trip(void) {
  fzgx_sim_world world;
  fzgx_sim_world restored;
  fzgx_sim_world_config config;
  fzgx_content_bundle bundle = make_test_bundle();
  fzgx_world_snapshot snapshot;

  fzgx_sim_world_init(&world);
  fzgx_sim_world_init(&restored);
  config.api_version = FZGX_SIM_API_VERSION;
  config.machine_capacity = 2u;
  assert(fzgx_sim_world_configure(&world, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_configure(&restored, &config, &bundle) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_track(&world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_machine_count(&world, 2u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 0u, 0u) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_seed_machine_from_content(&world, 1u, 0u) == FZGX_STATUS_OK);

  world.frame_index = 123u;
  world.stage_scene_frame_banks[0] = 7u;
  world.stage_scene_frame_banks[1] = 8u;
  world.stage_scene_frame_banks[2] = 9u;
  world.stage_scene_frame_banks[3] = 10u;
  world.stage_scene_context_mask = 0x00000020u;
  world.stage_scene_context_active_machine_index = 1;
  world.stage_scene_context_view_slot = 3u;
  world.race_full_heal_latch_active = 1u;
  world.race_full_heal_latch_persistent = 1u;
  world.stage_scene_story_clip_offset_frames = 12.0f;
  world.pending_stage_scene_story_delta_frames = 4.0f;
  world.has_pending_stage_scene_story_delta = 1u;
  world.scene_object_query_filter_mask = 0x00000004u;
  world.scene_object_query_mode_mask = 0x00000200u;
  world.dynamic_scene_runtime_flag_count = 1u;
  world.dynamic_scene_runtime_flags[0] = 0x40000000u;
  world.dynamic_scene_clip_bank_time_frames[0][0] = 1.0f;
  world.dynamic_scene_clip_bank_time_frames[0][1] = 2.0f;
  world.dynamic_scene_clip_bank_time_frames[0][2] = 3.0f;
  world.dynamic_scene_clip_bank_time_frames[0][3] = 4.0f;
  world.machines[0].position.x = 4.0f;
  world.machines[1].track_state.cur_cp_idx = 9;
  world.machines[1].track_state.next_cp_idx = 10;
  world.machines[1].track_state.active_checkpoint_mode = FZGX_ACTIVE_CHECKPOINT_NEXT;

  assert(fzgx_sim_world_get_snapshot(&world, &snapshot) == FZGX_STATUS_OK);
  assert(fzgx_sim_world_set_snapshot(&restored, &snapshot) == FZGX_STATUS_OK);
  assert(restored.frame_index == 123u);
  assert(restored.stage_scene_frame_banks[0] == 7u);
  assert(restored.stage_scene_frame_banks[1] == 8u);
  assert(restored.stage_scene_frame_banks[2] == 9u);
  assert(restored.stage_scene_frame_banks[3] == 10u);
  assert(restored.stage_scene_context_mask == 0x00000020u);
  assert(restored.stage_scene_context_active_machine_index == 1);
  assert(restored.stage_scene_context_view_slot == 3u);
  assert(restored.has_pending_stage_scene_story_delta == 1u);
  assert(restored.race_full_heal_latch_active == 1u);
  assert(restored.race_full_heal_latch_persistent == 1u);
  assert(restored.stage_scene_story_clip_offset_frames == 12.0f);
  assert(restored.pending_stage_scene_story_delta_frames == 4.0f);
  assert(restored.active_track_index == 0u);
  assert(restored.scene_object_query_filter_mask == 0x00000004u);
  assert(restored.scene_object_query_mode_mask == 0x00000200u);
  assert(restored.dynamic_scene_runtime_flag_count == 1u);
  assert(restored.dynamic_scene_runtime_flags[0] == 0x40000000u);
  assert(restored.dynamic_scene_clip_bank_time_frames[0][0] == 1.0f);
  assert(restored.dynamic_scene_clip_bank_time_frames[0][1] == 2.0f);
  assert(restored.dynamic_scene_clip_bank_time_frames[0][2] == 3.0f);
  assert(restored.dynamic_scene_clip_bank_time_frames[0][3] == 4.0f);
  assert(restored.machine_count == 2u);
  assert(restored.machines[0].position.x == 4.0f);
  assert(restored.machines[1].track_state.cur_cp_idx == 9);
  assert(restored.machines[1].track_state.next_cp_idx == 10);
  assert(restored.machines[1].track_state.active_checkpoint_mode == FZGX_ACTIVE_CHECKPOINT_NEXT);
}

int main(void) {
  test_configure_and_seed_machine();
  test_step_advances_frame_and_stages_controls();
  test_step_machine_phase_steering_accounts_for_suspension_scale();
  test_step_machine_phase_applies_drive_force_and_strafing_state();
  test_step_machine_phase_refreshes_speed_and_local_velocity_views();
  test_step_machine_phase_clears_side_attack_below_speed_gate();
  test_step_machine_phase_clamps_angular_velocity_from_weight();
  test_step_machine_phase_applies_turn_and_strafe_corner_force();
  test_step_machine_phase_prepare_prefix_clears_countdown_inputs();
  test_step_machine_phase_prepare_prefix_snapshots_old_positions();
  test_step_machine_phase_sets_terrain_state_from_spherecast_callback();
  test_step_machine_phase_sets_terrain_state_from_native_static_collider_course();
  test_world_binds_native_track_mesh_course_exact();
  test_step_machine_phase_updates_floor_and_suspension_from_native_track_mesh_course_exact();
  test_step_machine_phase_updates_floor_and_suspension_from_native_static_collider_course();
  test_step_machine_phase_acquires_floor_from_start_grid_with_native_static_and_track_mesh_courses();
  test_step_defers_inactive_start_grid_machine_until_activation();
  test_step_keeps_mute_city_machine_grounded_through_early_flat_road_transition();
  test_step_machine_phase_updates_suspension_force_from_spherecast_callback();
  test_step_machine_phase_builds_avg_track_normal_from_corner_sweeps();
  test_step_machine_phase_flattens_course_0x0f_special_case_corners();
  test_step_machine_phase_finds_floor_beneath_machine_generic_path();
  test_step_machine_phase_passes_sonic_oval_floor_bias_exact();
  test_step_machine_phase_updates_air_tilt_when_airborne();
  test_step_machine_phase_orients_basis_toward_floor_normal();
  test_step_machine_phase_applies_drag_and_glide_forces();
  test_step_machine_phase_collides_with_landmines_exact();
  test_step_machine_phase_collides_with_landmines_from_native_dynamic_scene_course();
  test_step_machine_phase_native_dynamic_scene_landmine_uses_collider_type_and_trxs_exact();
  test_step_machine_phase_native_dynamic_scene_landmine_uses_generic_clip_bank_exact();
  test_step_machine_phase_native_dynamic_scene_landmine_uses_controller_clip_slot_exact();
  test_step_machine_phase_native_dynamic_scene_landmine_uses_story_clip_offset_exact();
  test_step_machine_phase_promotes_pending_dynamic_scene_flags_for_alt_terrain_exact();
  test_step_machine_phase_runs_post_rotation_tail_exact();
  test_frame_phase_refreshes_wall_contact_queries_exact();
  test_frame_phase_refreshes_wall_contact_queries_from_native_static_collider_course();
  test_frame_phase_native_nohit_wall_queries_still_leave_suspension_enabled();
  test_frame_phase_applies_branch_slot_filter_to_course8_wall_sweeps();
  test_frame_phase_center_wall_sweep_oob_hit_sets_fallout_state_from_native_static_course();
  test_frame_phase_center_wall_sweep_oob_hit_sets_fallout_state();
  test_frame_phase_applies_generic_world_contact_response();
  test_frame_phase_applies_grounded_lava_damage_exact();
  test_frame_phase_lava_can_trigger_instant_destroy_exact();
  test_frame_phase_marks_zero_hp_machine_crashed_and_seeds_restore_exact();
  test_frame_phase_b29_suppresses_crash_restore_seed_exact();
  test_frame_phase_damage_ko_extends_restore_countdown_exact();
  test_frame_phase_oob_crash_keeps_default_restore_countdown_exact();
  test_frame_phase_broken_down_machine_applies_mid_speed_decay_exact();
  test_frame_phase_zero_hp_machine_runs_breakdown_fling_exact();
  test_frame_phase_breakdown_physics_accumulates_angles_exact();
  test_frame_phase_breakdown_physics_times_out_to_state2_latch_exact();
  test_frame_phase_updates_transform_visual_exact();
  test_frame_phase_broken_down_machine_retires_below_speed_floor_exact();
  test_step_machine_phase_respawn_interpolates_and_skips_motion_exact();
  test_step_machine_phase_respawn_handoff_restores_machine_exact();
  test_frame_phase_latches_machine_approach_counter_exact();
  test_frame_phase_clamps_machine_below_negative_ten_thousand_exact();
  test_frame_phase_destroys_machine_below_negative_five_thousand_exact();
  test_frame_phase_destroys_machine_below_track_min_height_margin_exact();
  test_frame_phase_refreshes_corner_history_from_current_transform_exact();
  test_frame_phase_refreshes_speed_and_max_speed_exact();
  test_frame_phase_skips_max_speed_refresh_when_damage_state_is_set();
  test_configure_world_with_builtin_ruby_bundle_and_30_machines();
  test_build_ordinary_start_grid_slot_transform_from_world();
  test_build_ordinary_start_grid_slot_query_result_from_world();
  test_snapshot_round_trip();
  test_world_snapshot_round_trip();
  test_finalize_machine_finish_score();
  test_apply_machine_track_sample_current_mode();
  test_apply_machine_track_sample_next_mode();
  test_step_machine_phase_recomputes_track_metrics();
  test_apply_machine_track_sample_uses_exact_cached_frame_ratio_gate();
  test_commit_active_checkpoint_bank_current_mode();
  test_commit_active_checkpoint_bank_next_mode();
  test_commit_machine_checkpoint_snapshot();
  test_build_current_track_sample_from_query_result();
  test_apply_current_checkpoint_query_result();
  test_apply_active_checkpoint_bank_result();
  test_refresh_machine_racetrack_state_from_sample();
  test_refresh_machine_racetrack_state_from_query_result();
  test_reset_machine_from_current_transform_sample();
  test_reset_machine_to_ordinary_start_grid_slot();
  test_build_machine_current_track_query_result_uses_cur_checkpoint_when_next_differs();
  test_reset_machines_to_ordinary_start_grid_ruby_full_field();
  test_refresh_machine_track_fit_transform_generic_exact();
  test_refresh_machine_track_fit_transform_without_spherecast_callback_exact();
  test_refresh_machine_track_fit_transform_course23_snap_exact();
  test_refresh_machine_track_fit_transform_course23_snap_without_callback_exact();
  test_refresh_machine_track_fit_transform_course9_window_exact();
  test_refresh_machine_track_fit_transform_course13_window_exact();
  test_refresh_machine_track_fit_transform_course15_window_exact();
  test_step_frame_phase_reseeds_active_checkpoint_state_from_builtin_course();
  test_step_frame_phase_commits_stable_checkpoint_state_from_builtin_course();
  test_step_frame_phase_skips_checkpoint_writeback_on_forward_progress_guard();
  test_step_frame_phase_checkpoint_neighborhood_gate_keeps_on_track_machine_alive();
  test_step_frame_phase_checkpoint_neighborhood_gate_marks_far_machine_fallout();
  test_step_frame_phase_updates_facing_latch_from_track_forward();
  test_reset_machine_from_explicit_transform_sample();
  test_reset_machine_from_current_transform_query_result();
  test_reset_machine_track_state_normal_mode();
  test_reset_machine_track_state_forced_mode();
  test_commit_machine_checkpoint_writeback_if_clean_normal_mode();
  test_commit_machine_checkpoint_writeback_if_clean_forced_mode();
  test_commit_machine_checkpoint_writeback_skips_fallout();
  test_commit_machine_checkpoint_writeback_skips_normal_airborne_or_not_ready();
  test_commit_machine_checkpoint_writeback_skips_forward_progress_guard();
  test_commit_machine_checkpoint_writeback_forced_mode_still_invalidates_last_cp_idx();
  test_open_circuit_progress_ignores_lap_cross_cp();
  test_step_frame_phase_open_track_does_not_double_increment_lap_timer();
  test_step_frame_phase_uses_current_neighbor_fraction_tail_for_lap_progress();
  test_step_frame_phase_sorts_finished_and_active_machines();
  test_step_frame_phase_orders_finished_by_total_time();
  test_step_frame_phase_orders_finished_by_total_time_fraction();
  test_step_frame_phase_updates_time_extension_trigger_mask_from_motion();
  test_step_frame_phase_closed_track_lap_rollover_applies_fractional_correction();
  test_step_frame_phase_history_display_uses_component_accumulation();
  test_step_frame_phase_handles_pairwise_machine_collision_exact();
  test_step_frame_phase_destroys_collision_flagged_machine_exact();
  test_step_frame_phase_raw_full_heal_uses_constant_hundred_exact();
  test_step_frame_phase_raw_finish_score_threshold_exact();
  test_step_frame_phase_raw_finish_score_is_suppressed_by_0x40_exact();
  test_step_frame_phase_raw_0x8000_keeps_retired_visible_in_ranking_exact();
  test_step_frame_phase_persistent_full_heal_latch_blocks_finish_score_exact();
  test_step_frame_phase_caps_lap_timer();
  return 0;
}
