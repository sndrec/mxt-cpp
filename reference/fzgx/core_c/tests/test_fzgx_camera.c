#include "fzgx/camera.h"

#include <assert.h>
#include <math.h>
#include <string.h>

enum {
  TEST_MACHINE_STATE_B1 = 0x00000001u,
  TEST_MACHINE_STATE_ACTIVE = 0x00000400u,
};

static fzgx_sim_world make_test_world(void) {
  fzgx_sim_world world;

  memset(&world, 0, sizeof(world));
  world.api_version = FZGX_SIM_API_VERSION;
  world.machine_count = 1u;
  world.machines[0].machine_flags = FZGX_MACHINE_FLAG_ACTIVE;
  world.machines[0].machine_state = TEST_MACHINE_STATE_B1 | TEST_MACHINE_STATE_ACTIVE;
  world.machines[0].control_profile_kind = 2u;
  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].position_old = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_x = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  world.machines[0].basis_physical.basis_y = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  world.machines[0].basis_physical.basis_z = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  world.machines[0].camera_reorienting = 1.0f;
  world.machines[0].camera_repositioning = 1.0f;
  world.machines[0].track_state.track_up = world.machines[0].basis_physical.basis_y;
  world.machines[0].track_state.last_track_pos = world.machines[0].basis_physical.basis_z;
  return world;
}

static void test_reset_uses_default_zoom(void) {
  fzgx_sim_world world = make_test_world();
  fzgx_game_camera_runtime camera;
  fzgx_game_camera_view view;

  memset(&camera, 0, sizeof(camera));
  assert(fzgx_game_camera_reset(&camera, &world, 0u) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_get_view(&camera, &view) == FZGX_STATUS_OK);
  assert(view.active == 1u);
  assert(view.zoom_mode == FZGX_GAME_CAMERA_ZOOM_CLOSE);
  assert(view.saved_zoom_mode == FZGX_GAME_CAMERA_ZOOM_CLOSE);
  assert(view.behavior_state == FZGX_GAME_CAMERA_BEHAVIOR_NORMAL);
  assert(fabsf(view.position.y - 2.94000006f) < 0.0001f);
  assert(fabsf(view.position.z - 7.19000006f) < 0.0001f);
  assert(fabsf(view.perspective - 55.0f) < 0.001f);
  assert(view.interest.z < view.position.z);
}

static void test_zoom_mode_steps_and_clamps(void) {
  fzgx_sim_world world = make_test_world();
  fzgx_game_camera_runtime camera;
  fzgx_game_camera_input input = {0};
  fzgx_game_camera_view view;

  memset(&camera, 0, sizeof(camera));
  assert(fzgx_game_camera_reset(&camera, &world, 0u) == FZGX_STATUS_OK);

  input.view_down_pressed = 1u;
  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_get_view(&camera, &view) == FZGX_STATUS_OK);
  assert(view.zoom_mode == FZGX_GAME_CAMERA_ZOOM_MEDIUM);

  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_get_view(&camera, &view) == FZGX_STATUS_OK);
  assert(view.zoom_mode == FZGX_GAME_CAMERA_ZOOM_FAR);

  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_get_view(&camera, &view) == FZGX_STATUS_OK);
  assert(view.zoom_mode == FZGX_GAME_CAMERA_ZOOM_FAR);

  memset(&input, 0, sizeof(input));
  input.view_up_pressed = 1u;
  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_step(&camera, &world, &input) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_get_view(&camera, &view) == FZGX_STATUS_OK);
  assert(view.zoom_mode == FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON);
}

static void test_camera_reorients_from_frame_displacement(void) {
  fzgx_sim_world world = make_test_world();
  fzgx_game_camera_runtime camera;
  fzgx_game_camera_view view;

  memset(&camera, 0, sizeof(camera));
  assert(fzgx_game_camera_reset(&camera, &world, 0u) == FZGX_STATUS_OK);

  world.machines[0].position_old = (fzgx_vec3){-4.0f, 0.0f, 0.0f};
  world.machines[0].position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  world.machines[0].suspension_state[0] = 4u;
  world.machines[0].speed_kmh = 300.0f;

  assert(fzgx_game_camera_step(&camera, &world, 0) == FZGX_STATUS_OK);
  assert(fzgx_game_camera_get_view(&camera, &view) == FZGX_STATUS_OK);
  assert(camera.follow_basis.basis_z_x > 0.05f);
  assert(view.position.x > 0.1f);
}

int main(void) {
  test_reset_uses_default_zoom();
  test_zoom_mode_steps_and_clamps();
  test_camera_reorients_from_frame_displacement();
  return 0;
}
