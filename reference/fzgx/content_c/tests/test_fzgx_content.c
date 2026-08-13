#include "fzgx/content.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef FZGX_REPO_ROOT
#define FZGX_REPO_ROOT "."
#endif

static void build_builtin_coli_course_path(char *path_out, size_t path_capacity, unsigned course_id) {
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

static void build_builtin_stage_gma_path(char *path_out, size_t path_capacity, unsigned stage_id) {
  assert(path_out != 0);
  assert(path_capacity != 0u);
  assert(
      snprintf(
          path_out,
          path_capacity,
          "%s/fzgx-iso/files/stage/st%02u.gma.lz",
          FZGX_REPO_ROOT,
          stage_id) > 0);
}

static void test_validate_null_bundle(void) {
  assert(fzgx_content_bundle_validate(0) == FZGX_STATUS_BAD_ARGUMENT);
}

static void test_validate_minimal_bundle(void) {
  fzgx_content_bundle bundle = {0};
  bundle.api_version = FZGX_CONTENT_API_VERSION;
  assert(fzgx_content_bundle_validate(&bundle) == FZGX_STATUS_OK);
}

static void test_validate_missing_tables(void) {
  fzgx_content_bundle bundle = {0};
  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  assert(fzgx_content_bundle_validate(&bundle) == FZGX_STATUS_BAD_ARGUMENT);

  bundle.track_count = 0u;
  bundle.machine_count = 1u;
  assert(fzgx_content_bundle_validate(&bundle) == FZGX_STATUS_BAD_ARGUMENT);

  bundle.machine_count = 0u;
  bundle.track_course_count = 1u;
  assert(fzgx_content_bundle_validate(&bundle) == FZGX_STATUS_BAD_ARGUMENT);
}

static void test_builtin_iso_bundle(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const fzgx_track_course_content *course = 0;
  const fzgx_track_course_animation_content *animation_course = 0;
  const fzgx_track_segment_animation_record *animation_segment = 0;

  assert(bundle != 0);
  assert(fzgx_content_bundle_validate(bundle) == FZGX_STATUS_OK);
  assert(bundle->track_count == 37u);
  assert(bundle->machine_count == 41u);
  assert(bundle->track_course_count == 37u);
  assert(bundle->track_animation_course_count == 37u);
  assert(bundle->machines[0].machine_id == 0u);
  assert(strcmp(bundle->machines[0].name, "Red Gazelle") == 0);
  assert(bundle->machines[6].machine_id == 6u);
  assert(strcmp(bundle->machines[6].name, "Blue Falcon") == 0);
  assert(bundle->machines[6].weight == 1260.0f);
  assert(bundle->machines[6].grip_1 == 0.47f);
  assert(bundle->machines[6].body == 0.85f);
  assert(bundle->machines[6].boost_strength == 14.0f);
  assert(bundle->tracks[0].authored_track_id == 1u);
  assert(bundle->tracks[4].authored_track_id == 8u);
  assert(bundle->tracks[11].authored_track_id == 16u);
  assert(bundle->tracks[16].authored_track_id == 26u);
  assert(bundle->tracks[27].authored_track_id == 38u);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(bundle, 4u, &course) ==
      FZGX_STATUS_OK);
  assert(course->authored_track_id == 8u);
  assert(course->track_node_count == 116u);
  assert(
      fzgx_content_bundle_find_track_course(bundle, 38u, &course) ==
      FZGX_STATUS_OK);
  assert(course->track_node_count == 40u);
  assert(
      fzgx_content_bundle_find_track_course_animation(bundle, 1u, &animation_course) ==
      FZGX_STATUS_OK);
  assert(animation_course->animation_curve_trs_count == 12u);
  assert(animation_course->animation_curve_count == 108u);
  assert(animation_course->keyable_attribute_count == 390u);
  assert(animation_course->track_segment_count == 12u);
  assert(
      fzgx_content_bundle_get_track_course_animation_for_track_index(
          bundle, 4u, &animation_course) == FZGX_STATUS_OK);
  assert(animation_course->authored_track_id == 8u);
  assert(
      fzgx_content_bundle_find_track_course_animation(bundle, 1u, &animation_course) ==
      FZGX_STATUS_OK);
  assert(
      fzgx_track_course_animation_find_track_segment_by_address(
          animation_course, 0x0001676cu, &animation_segment) == FZGX_STATUS_OK);
  assert(animation_segment->animation_curves_trs_address == 0x00016724u);
  assert(animation_segment->animation_curve_trs != 0);
  assert(animation_segment->animation_curve_trs->curve_count == 9u);
  assert(animation_segment->animation_curve_trs->curves[0].keyable_count == 4u);
  assert(animation_segment->animation_curve_trs->curves[1].keyable_count == 0u);
  assert(animation_segment->animation_curve_trs->curves[8].keyable_count == 26u);
}

static void test_load_track_mesh_course_exact(void) {
  char path[256];
  fzgx_owned_track_mesh_course course = {0};
  const fzgx_owned_track_mesh_chunk *chunk;
  const uint16_t *indices = 0;
  uint32_t count = 0u;

  build_builtin_coli_course_path(path, sizeof(path), 1u);
  assert(fzgx_content_load_track_mesh_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.class_count == FZGX_TRACK_MESH_CLASS_COUNT_GX);
  assert(course.chunk_count == 1u);

  chunk = &course.chunks[0];
  assert(chunk->address == 0x000143e4u);
  assert(chunk->class_count == FZGX_TRACK_MESH_CLASS_COUNT_GX);
  assert(chunk->cell_count == 256u);
  assert(chunk->tri_vertex_base_address == 0x0001c61cu);
  assert(chunk->quad_vertex_base_address == 0x000235b8u);
  assert(chunk->grid_subdiv_x == 16);
  assert(chunk->grid_subdiv_z == 16);
  assert(fabsf(chunk->grid_origin_x - (-1055.0531f)) < 0.001f);
  assert(fabsf(chunk->grid_origin_z - (-331.49136f)) < 0.001f);
  assert(fabsf(chunk->inv_cell_size_x - 120.06337f) < 0.001f);
  assert(fabsf(chunk->inv_cell_size_z - 140.2328f) < 0.001f);
  assert(chunk->tri_count == 148u);
  assert(chunk->quad_count == 65u);
  assert(chunk->tri_index_count == 565u);
  assert(chunk->quad_index_count == 244u);
  assert(chunk->render_instance_count_0xdc == 7u);
  assert(chunk->render_instance_table_0xe0_address == 0x000038d8u);
  assert(chunk->attachment_record_address == 0x00014074u);
  assert(chunk->has_attachment_record == 1u);
  assert(fabsf(chunk->attachment_record.point.x - (-158.65451f)) < 0.001f);
  assert(fabsf(chunk->attachment_record.point.y - 12.782166f) < 0.001f);
  assert(fabsf(chunk->attachment_record.point.z - 795.4710f) < 0.001f);
  assert(fabsf(chunk->attachment_record.unknown_0x0c - 1515.5f) < 0.001f);
  assert(chunk->has_animation_record == 0u);
  assert(chunk->flags_0x104 == 0u);

  assert(
      fzgx_track_mesh_chunk_get_tri_cell(chunk, 3u, 15u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 1u);
  assert(indices != 0);
  assert(indices[0] == 144u);

  assert(
      fzgx_track_mesh_chunk_get_quad_cell(chunk, 3u, 15u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 1u);
  assert(indices != 0);
  assert(indices[0] == 61u);

  fzgx_content_release_track_mesh_course(&course);
}

static void test_select_track_frame_export_record(void) {
  fzgx_track_frame_record frames[2];
  fzgx_track_frame_export_buffer buffer;
  fzgx_track_frame_record selected;

  frames[0].track_anchor.x = 1.0f;
  frames[1].track_anchor.x = 2.0f;
  frames[1].track_flags = 0x02200000u;

  buffer.cached_frames = frames;
  buffer.cached_frame_count = 2u;
  buffer.selected_frame_index = 1;

  assert(fzgx_track_frame_export_buffer_select(&buffer, &selected) == FZGX_STATUS_OK);
  assert(selected.track_anchor.x == 2.0f);
  assert(selected.track_flags == 0x02200000u);
}

static void test_reject_invalid_track_frame_export_buffer(void) {
  fzgx_track_frame_export_buffer buffer = {0};
  fzgx_track_frame_record selected;

  assert(fzgx_track_frame_export_buffer_select(0, &selected) == FZGX_STATUS_BAD_ARGUMENT);
  assert(fzgx_track_frame_export_buffer_select(&buffer, 0) == FZGX_STATUS_BAD_ARGUMENT);

  buffer.cached_frame_count = 1u;
  buffer.cached_frames = 0;
  buffer.selected_frame_index = 0;
  assert(fzgx_track_frame_export_buffer_select(&buffer, &selected) == FZGX_STATUS_BAD_ARGUMENT);

  buffer.cached_frames = &selected;
  buffer.selected_frame_index = -1;
  assert(fzgx_track_frame_export_buffer_select(&buffer, &selected) == FZGX_STATUS_OUT_OF_RANGE);

  buffer.selected_frame_index = 1;
  assert(fzgx_track_frame_export_buffer_select(&buffer, &selected) == FZGX_STATUS_OUT_OF_RANGE);
}

static void test_find_nearest_track_frame_anchor_index(void) {
  fzgx_track_frame_record frames[3] = {0};
  fzgx_track_frame_export_buffer buffer = {0};
  fzgx_vec3 point = {16.0f, 0.0f, 0.0f};
  int32_t selected_index = -1;

  frames[0].track_anchor.x = 0.0f;
  frames[1].track_anchor.x = 10.0f;
  frames[2].track_anchor.x = 20.0f;

  buffer.cached_frames = frames;
  buffer.cached_frame_count = 3u;

  assert(
      fzgx_track_frame_export_buffer_find_nearest_anchor_index(
          &buffer, &point, &selected_index) == FZGX_STATUS_OK);
  assert(selected_index == 2);

  buffer.cached_frame_count = 1u;
  selected_index = -1;
  assert(
      fzgx_track_frame_export_buffer_find_nearest_anchor_index(
          &buffer, &point, &selected_index) == FZGX_STATUS_OK);
  assert(selected_index == 0);
}

static void test_build_nearest_track_frame_export_buffer(void) {
  fzgx_track_frame_record frames[3] = {0};
  fzgx_track_frame_export_buffer buffer = {0};
  fzgx_vec3 point = {16.0f, 0.0f, 0.0f};

  frames[0].track_anchor.x = 0.0f;
  frames[1].track_anchor.x = 10.0f;
  frames[2].track_anchor.x = 20.0f;

  assert(
      fzgx_track_frame_export_buffer_build_nearest_anchor_selection(
          frames, 3u, &point, &buffer) == FZGX_STATUS_OK);
  assert(buffer.cached_frames == frames);
  assert(buffer.cached_frame_count == 3u);
  assert(buffer.selected_frame_index == 2);

  assert(
      fzgx_track_frame_export_buffer_build_nearest_anchor_selection(
          frames, 1u, &point, &buffer) == FZGX_STATUS_OK);
  assert(buffer.selected_frame_index == 0);
}

static void test_can_traverse_checkpoint_interval_helpers_exact(void) {
  fzgx_checkpoint_record checkpoints[4] = {0};
  fzgx_track_node_record track_nodes[4] = {0};
  fzgx_track_course_content course = {0};
  uint32_t can_traverse = 0u;

  checkpoints[0].start_distance = 0.0f;
  checkpoints[0].end_distance = 10.0f;
  checkpoints[0].connect_to_track_out = 1u;
  checkpoints[1].start_distance = 16.0f;
  checkpoints[1].end_distance = 26.0f;
  checkpoints[1].connect_to_track_out = 0u;
  checkpoints[2].start_distance = 26.0f;
  checkpoints[2].end_distance = 36.0f;
  checkpoints[2].connect_to_track_out = 1u;
  checkpoints[3].start_distance = 36.0f;
  checkpoints[3].end_distance = 46.0f;
  checkpoints[3].connect_to_track_out = 1u;

  track_nodes[0].checkpoint_count = 1u;
  track_nodes[0].checkpoint_offset = 0u;
  track_nodes[1].checkpoint_count = 1u;
  track_nodes[1].checkpoint_offset = 1u;
  track_nodes[2].checkpoint_count = 1u;
  track_nodes[2].checkpoint_offset = 2u;
  track_nodes[3].checkpoint_count = 1u;
  track_nodes[3].checkpoint_offset = 3u;

  course.track_node_count = 4u;
  course.checkpoint_record_count = 4u;
  course.track_total_distance = 46.0f;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_exact(&course, 0, 2, &can_traverse) ==
      FZGX_STATUS_OK);
  assert(can_traverse == 0u);

  checkpoints[1].connect_to_track_out = 1u;
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_exact(&course, 0, 2, &can_traverse) ==
      FZGX_STATUS_OK);
  assert(can_traverse == 1u);
}

static void test_find_checkpoint_for_track_distance_exact(void) {
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_node_record track_nodes[3] = {0};
  fzgx_track_course_content course = {0};
  int32_t checkpoint_index = -1;
  float checkpoint_fraction = -1.0f;

  checkpoints[0].start_distance = 0.0f;
  checkpoints[0].end_distance = 10.0f;
  checkpoints[1].start_distance = 10.0f;
  checkpoints[1].end_distance = 20.0f;
  checkpoints[2].start_distance = 20.0f;
  checkpoints[2].end_distance = 30.0f;

  track_nodes[0].checkpoint_count = 1u;
  track_nodes[0].checkpoint_offset = 0u;
  track_nodes[1].checkpoint_count = 1u;
  track_nodes[1].checkpoint_offset = 1u;
  track_nodes[2].checkpoint_count = 1u;
  track_nodes[2].checkpoint_offset = 2u;

  course.track_node_count = 3u;
  course.checkpoint_record_count = 3u;
  course.track_total_distance = 30.0f;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_find_checkpoint_for_track_distance(
          &course, -5.0, 0, &checkpoint_index, &checkpoint_fraction) == FZGX_STATUS_OK);
  assert(checkpoint_index == 2);
  assert(fabsf(checkpoint_fraction - 0.5f) < 0.0001f);

  assert(
      fzgx_track_course_find_checkpoint_for_track_distance(
          &course, 15.0, 2, &checkpoint_index, &checkpoint_fraction) == FZGX_STATUS_OK);
  assert(checkpoint_index == 1);
  assert(fabsf(checkpoint_fraction - 0.5f) < 0.0001f);

  assert(
      fzgx_track_course_find_checkpoint_for_track_distance(
          &course, 35.0, -1, &checkpoint_index, &checkpoint_fraction) == FZGX_STATUS_OK);
  assert(checkpoint_index == 0);
  assert(fabsf(checkpoint_fraction - 0.5f) < 0.0001f);
}

static void test_find_track_course_and_checkpoint_variant(void) {
  fzgx_track_manifest manifests[1] = {0};
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x250u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_segment_record track_segments[2] = {0};
  fzgx_track_course_content courses[1] = {0};
  fzgx_content_bundle bundle = {0};
  const fzgx_track_course_content *course = 0;
  const fzgx_track_node_record *track_node = 0;
  const fzgx_checkpoint_record *checkpoint = 0;
  const fzgx_track_segment_record *track_segment = 0;
  const fzgx_track_segment_record *children = 0;
  uint32_t children_count = 99u;
  uint32_t flags = 0u;

  checkpoints[2].track_width = 42.0f;
  checkpoints[2].connect_to_track_out = 1u;
  track_segments[0].address = 0x200u;
  track_segments[0].children_count = 1u;
  track_segments[0].children_address = 0x250u;
  track_segments[1].address = 0x250u;
  track_segments[1].branch_index = 2;

  manifests[0].authored_track_id = 8u;
  courses[0].authored_track_id = 8u;
  courses[0].track_node_count = 2u;
  courses[0].checkpoint_record_count = 3u;
  courses[0].track_segment_count = 2u;
  courses[0].track_nodes = track_nodes;
  courses[0].checkpoints = checkpoints;
  courses[0].track_segments = track_segments;

  bundle.api_version = FZGX_CONTENT_API_VERSION;
  bundle.track_count = 1u;
  bundle.track_course_count = 1u;
  bundle.tracks = manifests;
  bundle.track_courses = courses;

  assert(fzgx_content_bundle_find_track_course(&bundle, 8u, &course) == FZGX_STATUS_OK);
  assert(course == &courses[0]);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(&bundle, 0u, &course) ==
      FZGX_STATUS_OK);
  assert(course == &courses[0]);

  assert(fzgx_track_course_get_track_node(course, 1u, &track_node) == FZGX_STATUS_OK);
  assert(track_node == &track_nodes[1]);
  assert(track_node->checkpoint_offset == 1u);

  assert(
      fzgx_track_course_get_checkpoint_variant(course, 1u, 1u, &checkpoint) ==
      FZGX_STATUS_OK);
  assert(checkpoint == &checkpoints[2]);
  assert(checkpoint->track_width == 42.0f);
  assert(checkpoint->connect_to_track_out == 1u);

  assert(
      fzgx_track_course_find_track_segment_by_address(course, 0x250u, &track_segment) ==
      FZGX_STATUS_OK);
  assert(track_segment == &track_segments[1]);
  assert(track_segment->branch_index == 2);

  assert(
      fzgx_track_course_get_root_segment_for_track_node(course, 0u, &track_segment) ==
      FZGX_STATUS_OK);
  assert(track_segment == &track_segments[0]);
  assert(track_segment->children_count == 1u);
  assert(
      fzgx_track_course_get_track_segment_children(course, track_segment, &children, &children_count) ==
      FZGX_STATUS_OK);
  assert(children == &track_segments[1]);
  assert(children_count == 1u);
  assert(
      fzgx_track_segment_build_source_piece_word(&track_segments[1], &flags) ==
      FZGX_STATUS_OK);
  assert(flags == 0x00000000u);
  flags = 0u;
  track_segments[0].segment_type = 0x02u;
  track_segments[0].embedded_property_type = 0x10u;
  track_segments[1].segment_type = 0x01u;
  track_segments[1].embedded_property_type = 0x80u;
  track_segments[1].pipe_cylinder_flags = 0x02u;
  assert(
      fzgx_track_course_accumulate_track_segment_flags_recursive(course, &track_segments[0], &flags) ==
      FZGX_STATUS_OK);
  assert(flags == (0x02100000u | 0x01800002u));
}

static void test_can_traverse_checkpoint_interval_without_pipe_open_transition(void) {
  fzgx_track_node_record track_nodes[3] = {
      {1u, 0u, 0x100u, 0x200u},
      {1u, 1u, 0x150u, 0x250u},
      {1u, 2u, 0x1a0u, 0x2a0u},
  };
  fzgx_track_segment_record track_segments[3] = {0};
  fzgx_track_course_content course = {0};
  uint32_t can_traverse = 99u;

  track_segments[0].address = 0x200u;
  track_segments[0].segment_type = 0x02u;
  track_segments[1].address = 0x250u;
  track_segments[1].segment_type = 0x01u;
  track_segments[2].address = 0x2a0u;
  track_segments[2].segment_type = 0x01u;
  track_segments[2].embedded_property_type = 0x80u;

  course.track_node_count = 3u;
  course.track_segment_count = 3u;
  course.track_nodes = track_nodes;
  course.track_segments = track_segments;

  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, 0, 1, &can_traverse) == FZGX_STATUS_OK);
  assert(can_traverse == 1u);
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, 1, 2, &can_traverse) == FZGX_STATUS_OK);
  assert(can_traverse == 0u);
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, 0, 0, &can_traverse) == FZGX_STATUS_OK);
  assert(can_traverse == 1u);
}

static void test_reject_invalid_track_course_queries(void) {
  fzgx_track_course_content course = {0};
  fzgx_track_course_animation_content animation_course = {0};
  fzgx_track_node_record track_node = {0};
  fzgx_checkpoint_record checkpoint = {0};
  fzgx_track_segment_record track_segment = {0};
  fzgx_track_segment_animation_record animation_segment = {0};
  fzgx_content_bundle bundle = {0};
  const fzgx_track_course_content *course_out = 0;
  const fzgx_track_course_animation_content *animation_course_out = 0;
  const fzgx_track_node_record *track_node_out = 0;
  const fzgx_checkpoint_record *checkpoint_out = 0;
  const fzgx_track_segment_record *track_segment_out = 0;
  const fzgx_track_segment_animation_record *animation_segment_out = 0;
  uint32_t track_segment_count_out = 0u;

  bundle.api_version = FZGX_CONTENT_API_VERSION;

  assert(fzgx_content_bundle_find_track_course(0, 0u, &course_out) == FZGX_STATUS_BAD_ARGUMENT);
  assert(fzgx_content_bundle_find_track_course(&bundle, 0u, 0) == FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_find_track_course(&bundle, 0u, &course_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(0, 0u, &course_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(&bundle, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_get_track_course_for_track_index(&bundle, 0u, &course_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_content_bundle_find_track_course_animation(0, 0u, &animation_course_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_find_track_course_animation(&bundle, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_find_track_course_animation(&bundle, 0u, &animation_course_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_content_bundle_get_track_course_animation_for_track_index(0, 0u, &animation_course_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_get_track_course_animation_for_track_index(&bundle, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_bundle_get_track_course_animation_for_track_index(
          &bundle, 0u, &animation_course_out) == FZGX_STATUS_OUT_OF_RANGE);
  bundle.track_animation_course_count = 1u;
  assert(fzgx_content_bundle_validate(&bundle) == FZGX_STATUS_BAD_ARGUMENT);
  bundle.track_animation_course_count = 0u;

  assert(fzgx_track_course_get_track_node(0, 0u, &track_node_out) == FZGX_STATUS_BAD_ARGUMENT);
  assert(fzgx_track_course_get_track_node(&course, 0u, 0) == FZGX_STATUS_BAD_ARGUMENT);

  course.track_node_count = 1u;
  course.track_nodes = 0;
  assert(
      fzgx_track_course_get_track_node(&course, 0u, &track_node_out) ==
      FZGX_STATUS_BAD_ARGUMENT);

  course.track_nodes = &track_node;
  assert(
      fzgx_track_course_get_track_node(&course, 1u, &track_node_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_course_find_track_segment_by_address(0, 0u, &track_segment_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_find_track_segment_by_address(&course, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  course.track_segment_count = 1u;
  course.track_segments = 0;
  assert(
      fzgx_track_course_find_track_segment_by_address(&course, 0u, &track_segment_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  course.track_segments = &track_segment;
  assert(
      fzgx_track_course_find_track_segment_by_address(&course, 0x1234u, &track_segment_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_course_animation_find_track_segment_by_address(
          0, 0u, &animation_segment_out) == FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_animation_find_track_segment_by_address(
          &animation_course, 0u, 0) == FZGX_STATUS_BAD_ARGUMENT);
  animation_course.track_segment_count = 1u;
  assert(
      fzgx_track_course_animation_find_track_segment_by_address(
          &animation_course, 0u, &animation_segment_out) == FZGX_STATUS_BAD_ARGUMENT);
  animation_course.track_segments = &animation_segment;
  assert(
      fzgx_track_course_animation_find_track_segment_by_address(
          &animation_course, 0x1234u, &animation_segment_out) == FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_course_get_root_segment_for_track_node(0, 0u, &track_segment_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_root_segment_for_track_node(&course, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_track_segment_children(0, &track_segment, &track_segment_out,
                                                   &track_segment_count_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_track_segment_children(&course, 0, &track_segment_out,
                                                   &track_segment_count_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_track_segment_children(&course, &track_segment, 0,
                                                   &track_segment_count_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_track_segment_children(&course, &track_segment, &track_segment_out, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_track_segment_children(&course, &track_segment, &track_segment_out,
                                                   &track_segment_count_out) ==
      FZGX_STATUS_OK);
  assert(track_segment_out == 0);
  assert(track_segment_count_out == 0u);
  track_segment.children_count = 1u;
  track_segment.children_address = 0x1234u;
  assert(
      fzgx_track_course_get_track_segment_children(&course, &track_segment, &track_segment_out,
                                                   &track_segment_count_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_segment_build_source_piece_word(0, &track_segment_count_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_segment_build_source_piece_word(&track_segment, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_accumulate_track_segment_flags_recursive(0, &track_segment,
                                                                 &track_segment_count_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_accumulate_track_segment_flags_recursive(&course, 0,
                                                                 &track_segment_count_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_accumulate_track_segment_flags_recursive(&course, &track_segment, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          0, 0, 0, &track_segment_count_out) == FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, 0, 0, 0) == FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, 0, 0, &track_segment_count_out) == FZGX_STATUS_OK);
  course.track_node_count = 1u;
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, -1, 0, &track_segment_count_out) == FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
          &course, 0, 1, &track_segment_count_out) == FZGX_STATUS_OUT_OF_RANGE);

  assert(
      fzgx_track_course_get_checkpoint_variant(0, 0u, 0u, &checkpoint_out) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_get_checkpoint_variant(&course, 0u, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);

  track_node.checkpoint_count = 1u;
  track_node.checkpoint_offset = 0u;
  course.checkpoint_record_count = 1u;
  course.checkpoints = 0;
  assert(
      fzgx_track_course_get_checkpoint_variant(&course, 0u, 0u, &checkpoint_out) ==
      FZGX_STATUS_BAD_ARGUMENT);

  course.checkpoints = &checkpoint;
  assert(
      fzgx_track_course_get_checkpoint_variant(&course, 0u, 1u, &checkpoint_out) ==
      FZGX_STATUS_OUT_OF_RANGE);
}

static void test_evaluate_float_animation_curve(void) {
  fzgx_keyable_attribute step_keys[2] = {
      {0u, 0.0f, 4.0f, 0.0f, 0.0f},
      {0u, 1.0f, 9.0f, 0.0f, 0.0f},
  };
  fzgx_keyable_attribute linear_keys[2] = {
      {1u, 0.0f, 10.0f, 0.0f, 0.0f},
      {1u, 2.0f, 18.0f, 0.0f, 0.0f},
  };
  fzgx_keyable_attribute cubic_keys[2] = {
      {2u, 0.0f, 0.0f, 0.0f, 0.0f},
      {2u, 1.0f, 10.0f, 0.0f, 0.0f},
  };
  fzgx_keyable_attribute cached_keys[3] = {
      {1u, 0.0f, 0.0f, 0.0f, 0.0f},
      {1u, 1.0f, 10.0f, 0.0f, 0.0f},
      {1u, 2.0f, 30.0f, 0.0f, 0.0f},
  };
  fzgx_keyable_attribute cached_backtrack_keys[5] = {
      {1u, 0.0f, 0.0f, 0.0f, 0.0f},
      {1u, 1.0f, 10.0f, 0.0f, 0.0f},
      {1u, 2.0f, 20.0f, 0.0f, 0.0f},
      {1u, 3.0f, 30.0f, 0.0f, 0.0f},
      {1u, 4.0f, 40.0f, 0.0f, 0.0f},
  };
  fzgx_animation_curve step_curve = {2u, step_keys};
  fzgx_animation_curve linear_curve = {2u, linear_keys};
  fzgx_animation_curve cubic_curve = {2u, cubic_keys};
  fzgx_animation_curve cached_curve = {3u, cached_keys};
  fzgx_animation_curve cached_backtrack_curve = {5u, cached_backtrack_keys};
  float value = -1.0f;
  int32_t last_segment_index = 1;

  assert(fzgx_evaluate_float_animation_curve(0, 0.0f, &value) == FZGX_STATUS_BAD_ARGUMENT);
  assert(fzgx_evaluate_float_animation_curve(&step_curve, 0.0f, 0) == FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_evaluate_float_animation_curve(&step_curve, 0.5f, &value) ==
      FZGX_STATUS_OK);
  assert(value == 4.0f);
  assert(
      fzgx_evaluate_float_animation_curve(&linear_curve, 0.5f, &value) ==
      FZGX_STATUS_OK);
  assert(value == 12.0f);
  assert(
      fzgx_evaluate_float_animation_curve(&cubic_curve, 0.25f, &value) ==
      FZGX_STATUS_OK);
  assert(value == 1.5625f);

  assert(
      fzgx_evaluate_float_animation_curve_cached(0, 0.0f, &last_segment_index, &value) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_evaluate_float_animation_curve_cached(&cached_curve, 0.0f, 0, &value) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_evaluate_float_animation_curve_cached(&cached_curve, 0.0f, &last_segment_index, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  last_segment_index = 1;
  assert(
      fzgx_evaluate_float_animation_curve_cached(
          &cached_curve, 0.25f, &last_segment_index, &value) == FZGX_STATUS_OK);
  assert(value == 2.5f);
  assert(last_segment_index == 0);
  assert(
      fzgx_evaluate_float_animation_curve_cached(
          &cached_curve, 1.5f, &last_segment_index, &value) == FZGX_STATUS_OK);
  assert(value == 20.0f);
  assert(last_segment_index == 1);
  assert(
      fzgx_evaluate_float_animation_curve_cached(
          &cached_curve, 2.5f, &last_segment_index, &value) == FZGX_STATUS_OK);
  assert(value == 30.0f);
  assert(last_segment_index == 1);

  last_segment_index = 3;
  assert(
      fzgx_evaluate_float_animation_curve_cached(
          &cached_backtrack_curve, 1.5f, &last_segment_index, &value) == FZGX_STATUS_OK);
  assert(value == 15.0f);
  assert(last_segment_index == 1);
}

static void test_track_segment_sample_trs(void) {
  fzgx_keyable_attribute scale_x_keys[1] = {
      {0u, 0.0f, 11.0f, 0.0f, 0.0f},
  };
  fzgx_keyable_attribute rot_y_keys[1] = {
      {0u, 0.0f, 22.0f, 0.0f, 0.0f},
  };
  fzgx_keyable_attribute pos_z_keys[2] = {
      {1u, 0.0f, 100.0f, 0.0f, 0.0f},
      {1u, 2.0f, 140.0f, 0.0f, 0.0f},
  };
  fzgx_animation_curve curves[9] = {
      {1u, scale_x_keys},
      {0u, 0},
      {0u, 0},
      {0u, 0},
      {1u, rot_y_keys},
      {0u, 0},
      {0u, 0},
      {0u, 0},
      {2u, pos_z_keys},
  };
  fzgx_animation_curve_trs trs = {9u, curves};
  fzgx_track_segment_record segment = {0};
  fzgx_track_segment_animation_record animation_segment = {0};
  fzgx_track_segment_trs_sample sample = {0};

  segment.address = 0x1234u;
  segment.fallback_scale = (fzgx_vec3){2.0f, 3.0f, 4.0f};
  segment.fallback_rotation = (fzgx_vec3){5.0f, 6.0f, 7.0f};
  segment.fallback_position = (fzgx_vec3){8.0f, 9.0f, 10.0f};

  animation_segment.address = 0x1234u;
  animation_segment.animation_curves_trs_address = 0x1200u;
  animation_segment.animation_curve_trs = &trs;

  assert(
      fzgx_track_segment_sample_trs(0, &animation_segment, 1.0f, &sample) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_segment_sample_trs(&segment, &animation_segment, 1.0f, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_segment_sample_trs(&segment, &animation_segment, 1.0f, &sample) ==
      FZGX_STATUS_OK);
  assert(sample.scale.x == 11.0f);
  assert(sample.scale.y == 3.0f);
  assert(sample.scale.z == 4.0f);
  assert(sample.rotation.x == 5.0f);
  assert(sample.rotation.y == 22.0f);
  assert(sample.rotation.z == 7.0f);
  assert(sample.position.x == 8.0f);
  assert(sample.position.y == 9.0f);
  assert(sample.position.z == 120.0f);

  animation_segment.address = 0x5678u;
  assert(
      fzgx_track_segment_sample_trs(&segment, &animation_segment, 1.0f, &sample) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_segment_sample_trs(&segment, 0, 1.0f, &sample) ==
      FZGX_STATUS_OK);
  assert(sample.scale.x == 2.0f);
  assert(sample.rotation.y == 6.0f);
  assert(sample.position.z == 10.0f);
}

static void test_track_segment_apply_trs(void) {
  fzgx_keyable_attribute rot_z_keys[1] = {
      {0u, 0.0f, 90.0f, 0.0f, 0.0f},
  };
  fzgx_animation_curve curves[9] = {
      {0u, 0},
      {0u, 0},
      {0u, 0},
      {0u, 0},
      {0u, 0},
      {1u, rot_z_keys},
      {0u, 0},
      {0u, 0},
      {0u, 0},
  };
  fzgx_animation_curve_trs trs = {9u, curves};
  fzgx_track_segment_record segment = {0};
  fzgx_track_segment_animation_record animation_segment = {0};
  fzgx_mat43 transform = {0};
  fzgx_vec3 scale = {2.0f, 3.0f, 4.0f};

  segment.address = 0x1234u;
  segment.fallback_scale = (fzgx_vec3){5.0f, 6.0f, 7.0f};
  segment.fallback_position = (fzgx_vec3){1.0f, 2.0f, 3.0f};

  animation_segment.address = 0x1234u;
  animation_segment.animation_curves_trs_address = 0x1200u;
  animation_segment.animation_curve_trs = &trs;

  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;

  assert(
      fzgx_track_segment_apply_trs(0, &animation_segment, 0.0f, &transform, &scale, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_segment_apply_trs(&segment, &animation_segment, 0.0f, 0, &scale, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_segment_apply_trs(&segment, &animation_segment, 0.0f, &transform, 0, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);

  assert(
      fzgx_track_segment_apply_trs(&segment, &animation_segment, 0.0f, &transform, &scale, 0) ==
      FZGX_STATUS_OK);
  assert(transform.origin.x == 2.0f);
  assert(transform.origin.y == 6.0f);
  assert(transform.origin.z == 12.0f);
  assert(transform.basis_x_x == 0.0f);
  assert(transform.basis_x_y == 1.0f);
  assert(transform.basis_y_x == -1.0f);
  assert(transform.basis_y_y == 0.0f);
  assert(transform.basis_z_z == 1.0f);
  assert(scale.x == 10.0f);
  assert(scale.y == 18.0f);
  assert(scale.z == 28.0f);
}

static void test_compute_checkpoint_t_for_point(void) {
  fzgx_track_node_record track_nodes[3] = {
      {1u, 0u, 0u, 0u},
      {1u, 1u, 0u, 0u},
      {1u, 2u, 0u, 0u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {0.0f, 0.0f, 0.0f};
  float t = -1.0f;

  checkpoints[0].plane_start.distance = 1.0f;
  checkpoints[0].plane_start.normal.z = 1.0f;
  checkpoints[0].plane_end.distance = 1.0f;
  checkpoints[0].plane_end.normal.z = -1.0f;

  checkpoints[1].plane_start.distance = 2.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 2.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;

  checkpoints[2].plane_start.distance = 1.0f;
  checkpoints[2].plane_start.normal.z = 1.0f;
  checkpoints[2].plane_end.distance = 1.0f;
  checkpoints[2].plane_end.normal.z = -1.0f;

  course.track_node_count = 3u;
  course.checkpoint_record_count = 3u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 1u, &point, 1, 0u, &t) ==
      FZGX_STATUS_OK);
  assert(t == 0.5f);

  point.x = -3.0f;
  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 1u, &point, 1, 0u, &t) ==
      FZGX_STATUS_OK);
  assert(t == 0.0f);

  point.x = 0.0f;
  point.z = 1.0f;
  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 1u, &point, 0, 0u, &t) ==
      FZGX_STATUS_OK);
  assert(t == 0.0f);

  point.z = -1.0f;
  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 1u, &point, 2, 0u, &t) ==
      FZGX_STATUS_OK);
  assert(t == 1.0f);
}

static void test_compute_curve_time_for_checkpoint_fraction(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0u, 0u},
      {1u, 1u, 0u, 0u},
  };
  fzgx_checkpoint_record checkpoints[2] = {0};
  fzgx_track_course_content course = {0};
  float curve_time = -1.0f;

  checkpoints[1].curve_time_start = 10.0f;
  checkpoints[1].curve_time_end = 18.0f;

  course.track_node_count = 2u;
  course.checkpoint_record_count = 2u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_compute_curve_time_for_checkpoint_fraction(&course, 1u, 0.25f, &curve_time) ==
      FZGX_STATUS_OK);
  assert(curve_time == 12.0f);
}

static void test_reject_invalid_checkpoint_t_query(void) {
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {0.0f, 0.0f, 0.0f};
  float t = 0.0f;
  float curve_time = 0.0f;

  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(0, 0u, &point, 0, 0u, &t) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 0u, 0, 0, 0u, &t) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 0u, &point, 0, 0u, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_compute_checkpoint_t_for_point(&course, 0u, &point, 0, 0u, &t) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(
      fzgx_track_course_compute_curve_time_for_checkpoint_fraction(0, 0u, 0.0f, &curve_time) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_compute_curve_time_for_checkpoint_fraction(&course, 0u, 0.0f, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_track_course_compute_curve_time_for_checkpoint_fraction(&course, 0u, 0.0f, &curve_time) ==
      FZGX_STATUS_OUT_OF_RANGE);
}

static void test_checkpoint_variant_contains_point(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {1u, 1u, 0x150u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[2] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  int32_t checkpoint_index = -1;

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;

  checkpoints[1].plane_start.distance = -10.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;

  course.track_node_count = 2u;
  course.checkpoint_record_count = 2u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_checkpoint_variant_contains_point(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, 1, 0u, &checkpoint_index) ==
      FZGX_STATUS_OK);
  assert(checkpoint_index == 1);

  point.x = -50.0f;
  checkpoint_index = -1;
  assert(
      fzgx_track_course_checkpoint_variant_contains_point(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, 1, 0u, &checkpoint_index) ==
      FZGX_STATUS_OUT_OF_RANGE);
  assert(checkpoint_index == -1);

  {
    fzgx_track_node_record course7_nodes[0x21] = {0};
    fzgx_checkpoint_record course7_checkpoints[0x21] = {0};
    fzgx_track_course_content course7 = {0};
    fzgx_vec3 course7_point = {0.0f, 0.0f, 63.0f};

    course7_nodes[0x20].checkpoint_count = 1u;
    course7_nodes[0x20].checkpoint_offset = 0x20u;
    course7_checkpoints[0x20].plane_start.distance = -100.0f;
    course7_checkpoints[0x20].plane_start.normal.z = 1.0f;
    course7_checkpoints[0x20].plane_start.origin.z = 100.0f;
    course7_checkpoints[0x20].plane_end.distance = -50.0f;
    course7_checkpoints[0x20].plane_end.normal.z = 1.0f;
    course7_checkpoints[0x20].plane_end.origin.z = 50.0f;

    course7.track_node_count = 0x21u;
    course7.checkpoint_record_count = 0x21u;
    course7.track_nodes = course7_nodes;
    course7.checkpoints = course7_checkpoints;

    checkpoint_index = -1;
    assert(
        fzgx_track_course_checkpoint_variant_contains_point(
            &course7, 7u, FZGX_CIRCUIT_TYPE_CLOSED, &course7_point, 0x20, 0u,
            &checkpoint_index) == FZGX_STATUS_OK);
    assert(checkpoint_index == 0x20);
  }
}

static void test_scan_checkpoint_neighbors_for_point(void) {
  fzgx_track_node_record track_nodes[3] = {
      {1u, 0u, 0x100u, 0x200u},
      {1u, 1u, 0x150u, 0x200u},
      {1u, 2u, 0x1a0u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  fzgx_vec3 point_on_track = {0};
  int32_t checkpoint_index = -1;
  float t = -1.0f;

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;
  checkpoints[0].plane_start.origin.x = 0.0f;
  checkpoints[0].plane_end.origin.x = 10.0f;

  checkpoints[1].plane_start.distance = -10.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;
  checkpoints[1].plane_start.origin.x = 10.0f;
  checkpoints[1].plane_end.origin.x = 20.0f;

  checkpoints[2].plane_start.distance = -20.0f;
  checkpoints[2].plane_start.normal.x = 1.0f;
  checkpoints[2].plane_end.distance = 30.0f;
  checkpoints[2].plane_end.normal.x = -1.0f;
  checkpoints[2].plane_start.origin.x = 20.0f;
  checkpoints[2].plane_end.origin.x = 30.0f;

  course.track_node_count = 3u;
  course.checkpoint_record_count = 3u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_scan_checkpoint_neighbors_for_point(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          0u,
          0u,
          &checkpoint_index,
          &t,
          &point_on_track) == FZGX_STATUS_OK);
  assert(checkpoint_index == 1);
  assert(t == 0.5f);
  assert(point_on_track.x == 15.0f);

  track_nodes[2].root_segment_address = 0x300u;
  assert(
      fzgx_track_course_scan_checkpoint_neighbors_for_point(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          2,
          0u,
          1u,
          &checkpoint_index,
          &t,
          0) == FZGX_STATUS_OUT_OF_RANGE);
}

static void test_find_shared_checkpoint_for_point(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {1u, 1u, 0x150u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[2] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  int32_t checkpoint_index = -1;
  float t = -1.0f;

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;

  checkpoints[1].plane_start.distance = -10.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;

  course.track_node_count = 2u;
  course.checkpoint_record_count = 2u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_find_shared_checkpoint_for_point(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, &checkpoint_index, &t) ==
      FZGX_STATUS_OK);
  assert(checkpoint_index == 1);
  assert(t == 0.5f);
}

static void test_find_nearest_checkpoint_for_point(void) {
  fzgx_track_node_record track_nodes[3] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x200u},
      {1u, 3u, 0x1a0u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[4] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  int32_t checkpoint_index = -1;
  float t = -1.0f;
  uint32_t variant_index = 99u;

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;
  checkpoints[0].plane_start.origin.x = 0.0f;
  checkpoints[0].plane_end.origin.x = 10.0f;

  checkpoints[1].plane_start.distance = -10.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;
  checkpoints[1].plane_start.origin.x = 10.0f;
  checkpoints[1].plane_end.origin.x = 20.0f;

  checkpoints[2].plane_start.distance = -10.0f;
  checkpoints[2].plane_start.normal.x = 1.0f;
  checkpoints[2].plane_end.distance = 20.0f;
  checkpoints[2].plane_end.normal.x = -1.0f;
  checkpoints[2].plane_start.origin.x = 10.0f;
  checkpoints[2].plane_end.origin.x = 30.0f;

  checkpoints[3].plane_start.distance = -20.0f;
  checkpoints[3].plane_start.normal.x = 1.0f;
  checkpoints[3].plane_end.distance = 30.0f;
  checkpoints[3].plane_end.normal.x = -1.0f;
  checkpoints[3].plane_start.origin.x = 20.0f;
  checkpoints[3].plane_end.origin.x = 30.0f;

  course.track_node_count = 3u;
  course.checkpoint_record_count = 4u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_find_nearest_checkpoint_for_point(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          &checkpoint_index,
          &t,
          &variant_index) == FZGX_STATUS_OK);
  assert(checkpoint_index == 1);
  assert(t == 0.5f);
  assert(variant_index == 1u);
}

static void test_find_nearest_checkpoint_for_point_handles_track7_lower_z_special_case(void) {
  fzgx_track_node_record track_nodes[44] = {0};
  fzgx_checkpoint_record checkpoints[44] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {0.0f, 0.0f, 63.0f};
  int32_t checkpoint_index = -1;
  float t = -1.0f;
  uint32_t variant_index = 99u;

  for (int i = 0; i < 44; ++i) {
    track_nodes[i].checkpoint_count = 1u;
    track_nodes[i].checkpoint_offset = (uint32_t)i;
    checkpoints[i].plane_start.distance = -100.0f;
    checkpoints[i].plane_start.normal.z = 1.0f;
    checkpoints[i].plane_end.distance = -50.0f;
    checkpoints[i].plane_end.normal.z = 1.0f;
    checkpoints[i].plane_start.origin.z = 100.0f;
    checkpoints[i].plane_end.origin.z = 50.0f;
  }
  course.track_node_count = 44u;
  course.checkpoint_record_count = 44u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_find_nearest_checkpoint_for_point(
          &course,
          7u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          &checkpoint_index,
          &t,
          &variant_index) == FZGX_STATUS_OK);
  assert(checkpoint_index == 0x20);
  assert(t == 0.0f);
  assert(variant_index == 0u);
}

static void test_build_checkpoint_query_results(void) {
  fzgx_track_node_record track_nodes[3] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x200u},
      {1u, 3u, 0x1a0u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[4] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  fzgx_current_checkpoint_query_result query = {0};

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;
  checkpoints[0].plane_start.origin.x = 0.0f;
  checkpoints[0].plane_end.origin.x = 10.0f;

  checkpoints[1].plane_start.distance = -10.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;
  checkpoints[1].plane_start.origin.x = 10.0f;
  checkpoints[1].plane_end.origin.x = 20.0f;

  checkpoints[2].plane_start.distance = -10.0f;
  checkpoints[2].plane_start.normal.x = 1.0f;
  checkpoints[2].plane_end.distance = 20.0f;
  checkpoints[2].plane_end.normal.x = -1.0f;
  checkpoints[2].plane_start.origin.x = 10.0f;
  checkpoints[2].plane_end.origin.x = 30.0f;

  checkpoints[3].plane_start.distance = -20.0f;
  checkpoints[3].plane_start.normal.x = 1.0f;
  checkpoints[3].plane_end.distance = 30.0f;
  checkpoints[3].plane_end.normal.x = -1.0f;
  checkpoints[3].plane_start.origin.x = 20.0f;
  checkpoints[3].plane_end.origin.x = 30.0f;

  course.track_node_count = 3u;
  course.checkpoint_record_count = 4u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_build_shared_checkpoint_query_result(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, &query) == FZGX_STATUS_OK);
  assert(query.checkpoint_index == 1);
  assert(query.checkpoint_fraction == 0.5f);
  assert(query.variant_index == 0u);
  assert(query.point_on_track.x == 15.0f);

  memset(&query, 0, sizeof(query));
  assert(
      fzgx_track_course_build_neighbor_checkpoint_query_result(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, 1, 0u, 0u, &query) ==
      FZGX_STATUS_OK);
  assert(query.checkpoint_index == 1);
  assert(query.checkpoint_fraction == 0.5f);
  assert(query.variant_index == 0u);
  assert(query.point_on_track.x == 15.0f);

  memset(&query, 0, sizeof(query));
  assert(
      fzgx_track_course_build_nearest_checkpoint_query_result(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, &query) == FZGX_STATUS_OK);
  assert(query.checkpoint_index == 1);
  assert(query.checkpoint_fraction == 0.5f);
  assert(query.variant_index == 1u);
  assert(query.point_on_track.x == 20.0f);
}

static void test_resolve_branch_checkpoint_from_seed_helpers(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  int32_t checkpoint_index = -1;
  float t = -1.0f;
  uint32_t variant_index = 0u;

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;

  checkpoints[1].plane_start.distance = 0.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;

  checkpoints[2].plane_start.distance = -10.0f;
  checkpoints[2].plane_start.normal.x = 1.0f;
  checkpoints[2].plane_end.distance = 20.0f;
  checkpoints[2].plane_end.normal.x = -1.0f;

  course.track_node_count = 2u;
  course.checkpoint_record_count = 3u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          &variant_index,
          0u,
          &checkpoint_index,
          &t) == FZGX_STATUS_OK);
  assert(checkpoint_index == 1);
  assert(t == 0.5f);
  assert(variant_index == 1u);

  point.x = 5.0f;
  variant_index = 0u;
  checkpoint_index = -1;
  t = -1.0f;
  assert(
      fzgx_track_course_resolve_branch_checkpoint_from_seed_strict(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          &variant_index,
          1u,
          &checkpoint_index,
          &t) == FZGX_STATUS_OUT_OF_RANGE);
  assert(checkpoint_index == -1);

  variant_index = 0u;
  checkpoint_index = -1;
  t = -1.0f;
  assert(
      fzgx_track_course_resolve_branch_checkpoint_from_seed_with_fallback(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          &variant_index,
          1u,
          &checkpoint_index,
          &t) == FZGX_STATUS_OK);
  assert(checkpoint_index == 1);
  assert(t == 0.25f);
  assert(variant_index == 0u);
}

static void test_build_seeded_checkpoint_query_results(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  fzgx_current_checkpoint_query_result query = {0};

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;
  checkpoints[0].plane_start.origin.x = 0.0f;
  checkpoints[0].plane_end.origin.x = 10.0f;

  checkpoints[1].plane_start.distance = 0.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;
  checkpoints[1].plane_start.origin.x = 0.0f;
  checkpoints[1].plane_end.origin.x = 20.0f;

  checkpoints[2].plane_start.distance = -10.0f;
  checkpoints[2].plane_start.normal.x = 1.0f;
  checkpoints[2].plane_end.distance = 20.0f;
  checkpoints[2].plane_end.normal.x = -1.0f;
  checkpoints[2].plane_start.origin.x = 10.0f;
  checkpoints[2].plane_end.origin.x = 20.0f;

  course.track_node_count = 2u;
  course.checkpoint_record_count = 3u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_build_seeded_checkpoint_query_result_with_fallback(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, 1, &query) ==
      FZGX_STATUS_OK);
  assert(query.checkpoint_index == 1);
  assert(query.checkpoint_fraction == 0.5f);
  assert(query.variant_index == 1u);
  assert(query.point_on_track.x == 15.0f);

  point.x = 5.0f;
  memset(&query, 0, sizeof(query));
  assert(
      fzgx_track_course_build_seeded_checkpoint_query_result_strict(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, 1, &query) ==
      FZGX_STATUS_OUT_OF_RANGE);

  memset(&query, 0, sizeof(query));
  assert(
      fzgx_track_course_build_seeded_checkpoint_query_result_with_fallback(
          &course, 1u, FZGX_CIRCUIT_TYPE_CLOSED, &point, 1, &query) ==
      FZGX_STATUS_OK);
  assert(query.checkpoint_index == 1);
  assert(query.checkpoint_fraction == 0.25f);
  assert(query.variant_index == 0u);
  assert(query.point_on_track.x == 5.0f);
}

static void test_build_active_checkpoint_bank_for_point(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x200u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_vec3 point = {15.0f, 0.0f, 0.0f};
  fzgx_active_checkpoint_bank_result bank = {0};

  checkpoints[0].plane_start.distance = 0.0f;
  checkpoints[0].plane_start.normal.x = 1.0f;
  checkpoints[0].plane_end.distance = 10.0f;
  checkpoints[0].plane_end.normal.x = -1.0f;

  checkpoints[1].plane_start.distance = 0.0f;
  checkpoints[1].plane_start.normal.x = 1.0f;
  checkpoints[1].plane_end.distance = 20.0f;
  checkpoints[1].plane_end.normal.x = -1.0f;
  checkpoints[1].plane_start.origin.x = 0.0f;
  checkpoints[1].plane_end.origin.x = 20.0f;

  checkpoints[2].plane_start.distance = -10.0f;
  checkpoints[2].plane_start.normal.x = 1.0f;
  checkpoints[2].plane_end.distance = 20.0f;
  checkpoints[2].plane_end.normal.x = -1.0f;
  checkpoints[2].plane_start.origin.x = 10.0f;
  checkpoints[2].plane_end.origin.x = 20.0f;

  course.track_node_count = 2u;
  course.checkpoint_record_count = 3u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  assert(
      fzgx_track_course_build_active_checkpoint_bank_for_point(
          &course,
          1u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          0.5f,
          &bank) == FZGX_STATUS_OK);
  assert(bank.checkpoint_variant_count == 2u);
  assert(bank.preferred_variant_slot == 1u);
  assert(bank.checkpoint_index[0] == 1);
  assert(bank.checkpoint_fraction[0] == 0.5f);
  assert(bank.containment_checkpoint_index[0] == 1);
  assert(bank.checkpoint_index[1] == 1);
  assert(bank.checkpoint_fraction[1] == 0.5f);
  assert(bank.containment_checkpoint_index[1] == 1);
}

static void test_build_current_track_query_result_from_bank_and_frame_buffer(void) {
  fzgx_current_checkpoint_query_result checkpoint_result = {0};
  fzgx_active_checkpoint_bank_result bank_result = {0};
  fzgx_track_frame_record frames[2] = {0};
  fzgx_track_frame_export_buffer buffer = {0};
  fzgx_current_track_query_result query = {0};

  checkpoint_result.checkpoint_index = 24;
  checkpoint_result.checkpoint_fraction = 0.75f;

  bank_result.checkpoint_variant_count = 2u;
  bank_result.checkpoint_index[0] = 24;
  bank_result.checkpoint_fraction[0] = 0.75f;
  bank_result.checkpoint_index[1] = 25;
  bank_result.checkpoint_fraction[1] = 0.25f;
  bank_result.containment_checkpoint_index[0] = 77;

  frames[1].track_anchor.x = 42.0f;
  frames[1].track_flags = 0x02200000u;

  buffer.cached_frames = frames;
  buffer.cached_frame_count = 2u;
  buffer.selected_frame_index = 1;

  query.lap_progress_fraction = 88.0f;
  query.last_frac_diff = 99.0f;

  assert(
      fzgx_build_current_track_query_result_from_bank_and_frame_buffer(
          &checkpoint_result, &bank_result, &buffer, &query) == FZGX_STATUS_OK);
  assert(query.checkpoint_index == 24);
  assert(query.checkpoint_fraction == 0.75f);
  assert(query.active_bank_cp_idx[0] == 24);
  assert(query.active_bank_cp_frac[0] == 0.75f);
  assert(query.active_bank_cp_idx[1] == 25);
  assert(query.active_bank_cp_frac[1] == 0.25f);
  assert(query.cached_frame_count == 2u);
  assert(query.selected_cached_frame_index == 1);
  assert(query.frame.track_anchor.x == 42.0f);
  assert(query.frame.track_flags == 0x02200000u);
  assert(query.segment_index == 77);
  assert(query.lap_progress_fraction == 88.0f);
  assert(query.last_frac_diff == 99.0f);
}

static void test_build_current_track_query_result_from_bank_and_nearest_frame(void) {
  fzgx_current_checkpoint_query_result checkpoint_result = {0};
  fzgx_active_checkpoint_bank_result bank_result = {0};
  fzgx_track_frame_record frames[3] = {0};
  fzgx_current_track_query_result query = {0};
  fzgx_vec3 selection_point = {16.0f, 0.0f, 0.0f};

  checkpoint_result.checkpoint_index = 24;
  checkpoint_result.checkpoint_fraction = 0.75f;

  bank_result.checkpoint_variant_count = 3u;
  bank_result.checkpoint_index[0] = 24;
  bank_result.checkpoint_fraction[0] = 0.75f;
  bank_result.checkpoint_index[1] = 25;
  bank_result.checkpoint_fraction[1] = 0.25f;
  bank_result.containment_checkpoint_index[0] = 77;

  frames[0].track_anchor.x = 0.0f;
  frames[1].track_anchor.x = 10.0f;
  frames[1].track_flags = 0x01800000u;
  frames[2].track_anchor.x = 20.0f;
  frames[2].track_flags = 0x02200000u;

  query.lap_progress_fraction = 88.0f;
  query.last_frac_diff = 99.0f;

  assert(
      fzgx_build_current_track_query_result_from_bank_and_nearest_frame(
          &checkpoint_result, &bank_result, frames, 3u, &selection_point, &query) ==
      FZGX_STATUS_OK);
  assert(query.checkpoint_index == 24);
  assert(query.checkpoint_fraction == 0.75f);
  assert(query.cached_frame_count == 3u);
  assert(query.selected_cached_frame_index == 2);
  assert(query.frame.track_anchor.x == 20.0f);
  assert(query.frame.track_flags == 0x02200000u);
  assert(query.segment_index == 77);
  assert(query.lap_progress_fraction == 88.0f);
  assert(query.last_frac_diff == 99.0f);
}

static void test_build_current_track_query_result_for_point(void) {
  fzgx_track_node_record track_nodes[2] = {
      {1u, 0u, 0x100u, 0x200u},
      {2u, 1u, 0x150u, 0x250u},
  };
  fzgx_checkpoint_record checkpoints[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_track_frame_record frames[3] = {0};
  fzgx_current_track_query_result query = {0};
  fzgx_active_checkpoint_bank_result expected_bank = {0};
  fzgx_vec3 point = {16.0f, 0.0f, 15.0f};

  checkpoints[1].plane_start.distance = 10.0f;
  checkpoints[1].plane_start.normal.z = -1.0f;
  checkpoints[1].plane_start.origin.z = 10.0f;
  checkpoints[1].plane_end.distance = -20.0f;
  checkpoints[1].plane_end.normal.z = 1.0f;
  checkpoints[1].plane_end.origin.z = 20.0f;
  checkpoints[2] = checkpoints[1];
  checkpoints[2].plane_start.origin.x = 5.0f;
  checkpoints[2].plane_end.origin.x = 15.0f;

  course.authored_track_id = 29u;
  course.track_node_count = 2u;
  course.checkpoint_record_count = 3u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;

  frames[0].track_anchor.x = 0.0f;
  frames[1].track_anchor.x = 10.0f;
  frames[1].track_flags = 0x01800000u;
  frames[2].track_anchor.x = 20.0f;
  frames[2].track_flags = 0x02200000u;

  query.lap_progress_fraction = 88.0f;
  query.last_frac_diff = 99.0f;

  assert(
      fzgx_track_course_build_active_checkpoint_bank_for_point(
          &course,
          29u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          0.5f,
          &expected_bank) == FZGX_STATUS_OK);

  assert(
      fzgx_track_course_build_current_track_query_result_for_point(
          &course,
          29u,
          FZGX_CIRCUIT_TYPE_CLOSED,
          &point,
          1,
          0.5f,
          frames,
          3u,
          &query) == FZGX_STATUS_OK);
  assert(query.checkpoint_index == 1);
  assert(query.checkpoint_fraction == 0.5f);
  assert(query.active_bank_cp_idx[0] == 1);
  assert(query.active_bank_cp_frac[0] == 0.5f);
  assert(query.active_bank_cp_idx[1] == 1);
  assert(query.active_bank_cp_frac[1] == 0.5f);
  assert(query.cached_frame_count == 3u);
  assert(query.selected_cached_frame_index == 2);
  assert(query.frame.track_anchor.x == 20.0f);
  assert(query.frame.track_flags == 0x02200000u);
  assert(query.segment_index == expected_bank.containment_checkpoint_index[0]);
  assert(query.lap_progress_fraction == 88.0f);
  assert(query.last_frac_diff == 99.0f);
}

static void test_build_cached_frames_for_checkpoint_populates_family_specific_tail(void) {
  fzgx_track_node_record track_nodes[1] = {
      {1u, 0u, 0x400u, 0x100u},
  };
  fzgx_checkpoint_record checkpoints[1] = {0};
  fzgx_track_segment_record track_segments[3] = {0};
  fzgx_track_course_content course = {0};
  fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY] = {0};
  uint32_t cached_frame_count = 0u;

  checkpoints[0].curve_time_start = 0.0f;
  checkpoints[0].curve_time_end = 1.0f;
  checkpoints[0].plane_start.normal.z = 1.0f;
  checkpoints[0].plane_end.distance = 100.0f;
  checkpoints[0].plane_end.normal.z = -1.0f;

  track_segments[0].address = 0x100u;
  track_segments[0].segment_type = 0x02u;
  track_segments[0].embedded_property_type = 0x40u;
  track_segments[0].fallback_scale = (fzgx_vec3){10.0f, 1.5f, 1.0f};
  track_segments[0].fallback_position = (fzgx_vec3){2.0f, 0.0f, 0.0f};

  course.authored_track_id = 1u;
  course.track_node_count = 1u;
  course.checkpoint_record_count = 1u;
  course.track_segment_count = 1u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;
  course.track_segments = track_segments;

  assert(
      fzgx_track_course_build_cached_frames_for_checkpoint(
          &course,
          0,
          0u,
          0.0f,
          cached_frames,
          FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
          &cached_frame_count) == FZGX_STATUS_OK);
  assert(cached_frame_count == 1u);
  assert(fabsf(cached_frames[0].track_up.x - 0.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_up.y - 1.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_up.z - 0.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_forward.x - 0.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_forward.y - 0.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_forward.z - (-1.0f)) < 0.0001f);
  assert(fabsf(cached_frames[0].track_scl_x - 4.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_scl_y - 1.5f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_width_or_radius - 5.5f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_follow_offset.x - 0.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_follow_offset.y - 0.75f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_follow_offset.z - 0.0f) < 0.0001f);

  memset(cached_frames, 0, sizeof(cached_frames));
  cached_frame_count = 0u;
  track_segments[0].embedded_property_type = 0x80u;
  track_segments[0].children_count = 1u;
  track_segments[0].children_address = 0x150u;
  track_segments[0].fallback_scale = (fzgx_vec3){10.0f, 4.0f, 1.0f};
  track_segments[0].fallback_position = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  track_segments[1].address = 0x150u;
  track_segments[1].segment_type = 0x02u;
  track_segments[1].pipe_cylinder_flags = 0x02u;
  track_segments[1].fallback_scale = (fzgx_vec3){1.0f, 3.0f, 1.0f};
  course.track_segment_count = 2u;

  assert(
      fzgx_track_course_build_cached_frames_for_checkpoint(
          &course,
          0,
          0u,
          0.0f,
          cached_frames,
          FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
          &cached_frame_count) == FZGX_STATUS_OK);
  assert(cached_frame_count == 1u);
  assert(fabsf(cached_frames[0].track_up.y - 1.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_forward.z - (-1.0f)) < 0.0001f);
  assert(fabsf(cached_frames[0].track_width_or_radius - 10.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_hcylin - 3.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_follow_offset.x - 0.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_follow_offset.y - 2.0f) < 0.0001f);
  assert(fabsf(cached_frames[0].track_follow_offset.z - 0.0f) < 0.0001f);
}

static void test_build_cached_frames_for_checkpoint_counts_branch_root_plus_selected_path(void) {
  fzgx_track_node_record track_nodes[1] = {
      {1u, 0u, 0x400u, 0x100u},
  };
  fzgx_checkpoint_record checkpoints[1] = {0};
  fzgx_track_segment_record track_segments[2] = {0};
  fzgx_track_course_content course = {0};
  fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY] = {0};
  uint32_t cached_frame_count = 0u;

  checkpoints[0].curve_time_start = 0.0f;
  checkpoints[0].curve_time_end = 1.0f;
  checkpoints[0].plane_start.normal.z = 1.0f;
  checkpoints[0].plane_end.distance = 100.0f;
  checkpoints[0].plane_end.normal.z = -1.0f;

  track_segments[0].address = 0x100u;
  track_segments[0].segment_type = 0x04u;
  track_segments[0].children_count = 1u;
  track_segments[0].children_address = 0x120u;
  track_segments[0].fallback_position = (fzgx_vec3){1.0f, 0.0f, 0.0f};

  track_segments[1].address = 0x120u;
  track_segments[1].segment_type = 0x02u;
  track_segments[1].fallback_scale = (fzgx_vec3){5.0f, 1.0f, 1.0f};
  track_segments[1].fallback_position = (fzgx_vec3){3.0f, 0.0f, 0.0f};

  course.authored_track_id = 1u;
  course.track_node_count = 1u;
  course.checkpoint_record_count = 1u;
  course.track_segment_count = 2u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;
  course.track_segments = track_segments;

  assert(
      fzgx_track_course_build_cached_frames_for_checkpoint(
          &course,
          0,
          0u,
          0.0f,
          cached_frames,
          FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
          &cached_frame_count) == FZGX_STATUS_OK);
  assert(cached_frame_count == 2u);
  assert(cached_frames[0].track_flags == 0x00000000u);
  assert(cached_frames[1].track_flags == 0x02000000u);
}

static void test_build_cached_frames_for_checkpoint_keeps_0x200000_width_unset(void) {
  fzgx_track_node_record track_nodes[1] = {
      {1u, 0u, 0x400u, 0x100u},
  };
  fzgx_checkpoint_record checkpoints[1] = {0};
  fzgx_track_segment_record track_segments[1] = {0};
  fzgx_track_course_content course = {0};
  fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY] = {0};
  uint32_t cached_frame_count = 0u;

  checkpoints[0].curve_time_start = 0.0f;
  checkpoints[0].curve_time_end = 1.0f;
  checkpoints[0].plane_start.normal.z = 1.0f;
  checkpoints[0].plane_end.distance = 100.0f;
  checkpoints[0].plane_end.normal.z = -1.0f;

  track_segments[0].address = 0x100u;
  track_segments[0].embedded_property_type = 0x20u;
  track_segments[0].fallback_scale = (fzgx_vec3){1.0f, 90.0f, 45.0f};

  course.authored_track_id = 8u;
  course.track_node_count = 1u;
  course.checkpoint_record_count = 1u;
  course.track_segment_count = 1u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;
  course.track_segments = track_segments;

  assert(
      fzgx_track_course_build_cached_frames_for_checkpoint(
          &course,
          0,
          0u,
          0.0f,
          cached_frames,
          FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
          &cached_frame_count) == FZGX_STATUS_OK);
  assert(cached_frame_count == 1u);
  assert(cached_frames[0].track_flags == 0x00200000u);
  assert(fabsf(cached_frames[0].track_width_or_radius) < 0.0001f);
  assert(fabsf(cached_frames[0].track_hcylin) < 0.0001f);
}

static void test_builtin_0x200000_cached_frame_family_shape_invariants(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  uint32_t track_index;

  assert(bundle != 0);
  for (track_index = 0u; track_index < bundle->track_course_count; ++track_index) {
    const fzgx_track_course_content *course = 0;
    uint32_t segment_index;

    assert(
        fzgx_content_bundle_get_track_course_for_track_index(bundle, track_index, &course) ==
        FZGX_STATUS_OK);
    for (segment_index = 0u; segment_index < course->track_segment_count; ++segment_index) {
      const fzgx_track_segment_record *segment = &course->track_segments[segment_index];

      if (segment->embedded_property_type != 0x20u) {
        continue;
      }
      assert(segment->children_count == 0u);
      assert(segment->segment_type == 0u);
    }
  }
}

static void test_build_cached_frames_for_checkpoint_on_builtin_ruby_courses(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const uint32_t ruby_track_indices[5] = {0u, 4u, 11u, 16u, 27u};
  uint32_t i;

  assert(bundle != 0);
  for (i = 0u; i < 5u; ++i) {
    const fzgx_track_course_content *course = 0;
    const fzgx_track_course_animation_content *animation_course = 0;
    fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY];
    uint32_t cached_frame_count = 0u;

    memset(cached_frames, 0, sizeof(cached_frames));
    assert(
        fzgx_content_bundle_get_track_course_for_track_index(
            bundle, ruby_track_indices[i], &course) == FZGX_STATUS_OK);
    assert(
        fzgx_content_bundle_get_track_course_animation_for_track_index(
            bundle, ruby_track_indices[i], &animation_course) == FZGX_STATUS_OK);
    assert(
        fzgx_track_course_build_cached_frames_for_checkpoint(
            course,
            animation_course,
            0u,
            0.0f,
            cached_frames,
            FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
            &cached_frame_count) == FZGX_STATUS_OK);
    assert(cached_frame_count >= 1u);
    assert(cached_frame_count <= FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY);
    assert(
        (cached_frames[0].track_current_transform.basis_x_x != 0.0f) ||
        (cached_frames[0].track_current_transform.basis_x_y != 0.0f) ||
        (cached_frames[0].track_current_transform.basis_x_z != 0.0f));
  }
}

static void test_builtin_family_specific_cached_frame_tail_shape_invariants(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  uint32_t course_index;

  assert(bundle != 0);
  for (course_index = 0u; course_index < bundle->track_course_count; ++course_index) {
    const fzgx_track_course_content *course = &bundle->track_courses[course_index];
    uint32_t segment_index;

    for (segment_index = 0u; segment_index < course->track_segment_count; ++segment_index) {
      const fzgx_track_segment_record *segment = &course->track_segments[segment_index];

      if (segment->embedded_property_type == 0x80u) {
        assert(segment->children_count == 1u);
      }
      if (segment->embedded_property_type == 0x40u) {
        assert(segment->children_count == 0u);
      }
    }
  }
}

static void test_build_current_track_query_result_for_active_checkpoint(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const uint32_t ruby_track_indices[5] = {0u, 4u, 11u, 16u, 27u};
  uint32_t i;

  assert(bundle != 0);
  for (i = 0u; i < 5u; ++i) {
    const fzgx_track_course_content *course = 0;
    const fzgx_track_course_animation_content *animation_course = 0;
    fzgx_current_track_query_result start_grid_query;
    fzgx_current_track_query_result query;

    memset(&start_grid_query, 0, sizeof(start_grid_query));
    memset(&query, 0, sizeof(query));
    assert(
        fzgx_content_bundle_get_track_course_for_track_index(
            bundle, ruby_track_indices[i], &course) == FZGX_STATUS_OK);
    assert(
        fzgx_content_bundle_get_track_course_animation_for_track_index(
            bundle, ruby_track_indices[i], &animation_course) == FZGX_STATUS_OK);
    assert(
        fzgx_track_course_build_ordinary_start_grid_slot_query_result(
            course,
            animation_course,
            bundle->tracks[ruby_track_indices[i]].authored_track_id,
            bundle->tracks[ruby_track_indices[i]].circuit_type,
            0u,
            &start_grid_query) == FZGX_STATUS_OK);
    assert(
        fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
            course,
            animation_course,
            bundle->tracks[ruby_track_indices[i]].authored_track_id,
            bundle->tracks[ruby_track_indices[i]].circuit_type,
            &start_grid_query.frame.track_current_transform.origin,
            start_grid_query.checkpoint_index,
            start_grid_query.checkpoint_fraction,
            &query) == FZGX_STATUS_OK);
    assert(query.checkpoint_index == start_grid_query.checkpoint_index);
    assert(query.cached_frame_count >= 1u);
    assert(query.last_frac_diff == course->track_total_distance);
    assert(query.selected_cached_frame_index >= 0);
    assert(query.selected_cached_frame_index < (int32_t)query.cached_frame_count);
    assert(
        (query.frame.track_current_transform.basis_x_x != 0.0f) ||
        (query.frame.track_current_transform.basis_x_y != 0.0f) ||
        (query.frame.track_current_transform.basis_x_z != 0.0f));
  }
}

static void test_build_ordinary_start_grid_slot_transform(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const uint32_t ruby_track_indices[5] = {0u, 4u, 11u, 16u, 27u};
  uint32_t i;

  assert(bundle != 0);
  for (i = 0u; i < 5u; ++i) {
    const fzgx_track_course_content *course = 0;
    const fzgx_track_course_animation_content *animation_course = 0;
    fzgx_mat43 transform = {0};
    float width = 0.0f;

    assert(
        fzgx_content_bundle_get_track_course_for_track_index(
            bundle, ruby_track_indices[i], &course) == FZGX_STATUS_OK);
    assert(
        fzgx_content_bundle_get_track_course_animation_for_track_index(
            bundle, ruby_track_indices[i], &animation_course) == FZGX_STATUS_OK);
    assert(
        fzgx_track_course_build_ordinary_start_grid_slot_transform(
            course,
            animation_course,
            bundle->tracks[ruby_track_indices[i]].authored_track_id,
            bundle->tracks[ruby_track_indices[i]].circuit_type,
            0u,
            &width,
            &transform) == FZGX_STATUS_OK);
    assert(width > 0.0f);
    assert(
        (transform.basis_x_x != 0.0f) ||
        (transform.basis_x_y != 0.0f) ||
        (transform.basis_x_z != 0.0f));
    assert(
        (transform.basis_y_x != 0.0f) ||
        (transform.basis_y_y != 0.0f) ||
        (transform.basis_y_z != 0.0f));
    assert(
        (transform.basis_z_x != 0.0f) ||
        (transform.basis_z_y != 0.0f) ||
        (transform.basis_z_z != 0.0f));
  }
}

static void test_build_ordinary_start_grid_slot_query_result(void) {
  const fzgx_content_bundle *bundle = fzgx_content_get_builtin_iso_bundle();
  const uint32_t ruby_track_indices[5] = {0u, 4u, 11u, 16u, 27u};
  uint32_t i;

  assert(bundle != 0);
  for (i = 0u; i < 5u; ++i) {
    const fzgx_track_course_content *course = 0;
    const fzgx_track_course_animation_content *animation_course = 0;
    fzgx_current_track_query_result query;

    memset(&query, 0xff, sizeof(query));
    assert(
        fzgx_content_bundle_get_track_course_for_track_index(
            bundle, ruby_track_indices[i], &course) == FZGX_STATUS_OK);
    assert(
        fzgx_content_bundle_get_track_course_animation_for_track_index(
            bundle, ruby_track_indices[i], &animation_course) == FZGX_STATUS_OK);
    assert(
        fzgx_track_course_build_ordinary_start_grid_slot_query_result(
            course,
            animation_course,
            bundle->tracks[ruby_track_indices[i]].authored_track_id,
            bundle->tracks[ruby_track_indices[i]].circuit_type,
            0u,
            &query) == FZGX_STATUS_OK);
    assert(query.checkpoint_index >= 0);
    assert(query.cached_frame_count >= 1u);
    assert(query.last_frac_diff == course->track_total_distance);
    assert(query.selected_cached_frame_index >= 0);
    assert(query.selected_cached_frame_index < (int32_t)query.cached_frame_count);
    assert(query.frame.track_width_or_radius > 0.0f);
    assert(
        (query.frame.track_current_transform.basis_x_x != 0.0f) ||
        (query.frame.track_current_transform.basis_x_y != 0.0f) ||
        (query.frame.track_current_transform.basis_x_z != 0.0f));
  }
}

static void test_build_ordinary_start_grid_slot_transform_prefers_branch_slot1(void) {
  fzgx_track_node_record track_nodes[1] = {
      {1u, 0u, 0x400u, 0x100u},
  };
  fzgx_checkpoint_record checkpoints[1] = {0};
  fzgx_track_segment_record track_segments[5] = {0};
  fzgx_track_course_content course = {0};
  fzgx_mat43 transform = {0};
  float width = 0.0f;

  checkpoints[0].curve_time_start = 0.0f;
  checkpoints[0].curve_time_end = 1.0f;
  checkpoints[0].plane_start.normal.z = 1.0f;
  checkpoints[0].plane_end.distance = 100.0f;
  checkpoints[0].plane_end.normal.z = -1.0f;

  track_segments[0].address = 0x100u;
  track_segments[0].segment_type = 0x08u;
  track_segments[0].children_count = 2u;
  track_segments[0].children_address = 0x200u;
  track_segments[0].fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};

  track_segments[1].address = 0x200u;
  track_segments[1].segment_type = 0x04u;
  track_segments[1].children_count = 1u;
  track_segments[1].children_address = 0x350u;
  track_segments[1].fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  track_segments[1].fallback_position.x = 20.0f;
  track_segments[1].branch_index = 2;

  track_segments[2].address = 0x250u;
  track_segments[2].segment_type = 0x04u;
  track_segments[2].children_count = 1u;
  track_segments[2].children_address = 0x300u;
  track_segments[2].fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  track_segments[2].fallback_position.x = 10.0f;
  track_segments[2].branch_index = 1;

  track_segments[3].address = 0x300u;
  track_segments[3].segment_type = 0x02u;
  track_segments[3].fallback_scale = (fzgx_vec3){10.0f, 1.0f, 1.0f};

  track_segments[4].address = 0x350u;
  track_segments[4].segment_type = 0x02u;
  track_segments[4].fallback_scale = (fzgx_vec3){6.0f, 1.0f, 1.0f};

  course.authored_track_id = 1u;
  course.track_node_count = 1u;
  course.checkpoint_record_count = 1u;
  course.track_segment_count = 5u;
  course.track_nodes = track_nodes;
  course.checkpoints = checkpoints;
  course.track_segments = track_segments;

  assert(
      fzgx_track_course_build_ordinary_start_grid_slot_transform(
          &course,
          0,
          1u,
          FZGX_CIRCUIT_TYPE_OPEN,
          0u,
          &width,
          &transform) == FZGX_STATUS_OK);
  assert(width == 10.0f);
  assert(transform.origin.x == 12.5f);
  assert(transform.origin.y == 1.0f);
  assert(transform.origin.z == 0.0f);
}

static void test_track_frame_get_width_and_scale(void) {
  fzgx_track_frame_record frame = {0};
  float width = 0.0f;
  float scale = 0.0f;

  frame.track_flags = 0x02200000u;
  frame.track_width_or_radius = 10.0f;
  assert(fzgx_track_frame_get_width_and_scale(&frame, &width, &scale) == FZGX_STATUS_OK);
  assert(fabsf(width - 10.0f) < 0.0001f);
  assert(fabsf(scale - 1.0f) < 0.0001f);

  frame.track_flags = 0x01800000u;
  frame.track_current_scale.x = 2.0f;
  frame.track_current_scale.y = 3.0f;
  frame.track_hcylin = 0.5f;
  assert(fzgx_track_frame_get_width_and_scale(&frame, &width, &scale) == FZGX_STATUS_OK);
  assert(fabsf(scale - 1.5f) < 0.0001f);
  assert(fabsf(width - ((1.0f + 2.1415927410125732f * 1.5f) * 0.5f * 10.0f)) < 0.0001f);

  frame.track_flags = 0x00400000u;
  frame.track_scl_x = 2.0f;
  assert(fzgx_track_frame_get_width_and_scale(&frame, &width, &scale) == FZGX_STATUS_OK);
  assert(fabsf(scale - 1.5f) < 0.0001f);
  assert(fabsf(width - (2.0f * 2.0f + (1.0f + 2.1415927410125732f * 1.5f) * 8.0f)) < 0.0001f);

  frame.track_flags = 0x01800000u;
  frame.track_current_scale.x = 4.0f;
  frame.track_current_scale.y = 0.1f;
  assert(fzgx_track_frame_get_width_and_scale(&frame, &width, &scale) == FZGX_STATUS_OK);
  assert(fabsf(width - 10.0f) < 0.0001f);
  assert(fabsf(scale - 0.0f) < 0.0001f);
}

static void test_track_frame_sample_world_pos(void) {
  fzgx_track_frame_record frame = {0};
  fzgx_vec3 world_pos = {0};

  frame.track_current_transform.basis_x_x = 1.0f;
  frame.track_current_transform.basis_y_y = 1.0f;
  frame.track_current_transform.basis_z_z = 1.0f;
  frame.track_flags = 0x02200000u;
  frame.track_width_or_radius = 10.0f;
  assert(fzgx_track_frame_sample_world_pos(0.5f, &frame, &world_pos) == FZGX_STATUS_OK);
  assert(fabsf(world_pos.x - 5.0f) < 0.0001f);
  assert(fabsf(world_pos.y - 0.0f) < 0.0001f);
  assert(fabsf(world_pos.z - 0.0f) < 0.0001f);

  frame.track_flags = 0x01800000u;
  frame.track_current_scale.x = 4.0f;
  frame.track_current_scale.y = 5.0f;
  frame.track_follow_offset = (fzgx_vec3){100.0f, 2.0f, 3.0f};
  assert(fzgx_track_frame_sample_world_pos(0.5f, &frame, &world_pos) == FZGX_STATUS_OK);
  assert(fabsf(world_pos.x - 105.0f) < 0.0001f);
  assert(fabsf(world_pos.y - 2.0f) < 0.0001f);
  assert(fabsf(world_pos.z - 3.0f) < 0.0001f);

  frame.track_flags = 0x01800000u;
  frame.track_current_scale.x = 2.0f;
  frame.track_current_scale.y = 3.0f;
  frame.track_width_or_radius = 10.0f;
  frame.track_hcylin = 0.5f;
  frame.track_current_transform.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  assert(fzgx_track_frame_sample_world_pos(0.0f, &frame, &world_pos) == FZGX_STATUS_OK);
  assert(fabsf(world_pos.x - 0.0f) < 0.0001f);
  assert(fabsf(world_pos.y - (-26.25f)) < 0.0001f);
  assert(fabsf(world_pos.z - 0.0f) < 0.0001f);

  frame.track_flags = 0x00400000u;
  frame.track_scl_x = 2.0f;
  frame.track_scl_y = 4.0f;
  assert(fzgx_track_frame_sample_world_pos(0.0f, &frame, &world_pos) == FZGX_STATUS_OK);
  assert(fabsf(world_pos.x - 0.0f) < 0.0001f);
  assert(fabsf(world_pos.y - (-10.5f)) < 0.0001f);
  assert(fabsf(world_pos.z - 0.0f) < 0.0001f);
}

static void test_load_builtin_static_collider_course01_exact(void) {
  char path[256];
  fzgx_owned_static_collider_course course = {0};
  const uint16_t *indices = 0;
  uint32_t count = 0u;

  build_builtin_coli_course_path(path, sizeof(path), 1u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.surface_count == 14u);
  assert(course.tri_count == 148u);
  assert(course.quad_count == 65u);
  assert(course.tri_index_count == 565u);
  assert(course.quad_index_count == 244u);
  assert(fabsf(course.mesh_grid.left - (-1055.0531005859375f)) < 0.0001f);
  assert(fabsf(course.mesh_grid.top - (-331.4913635253906f)) < 0.0001f);
  assert(fabsf(course.mesh_grid.subdivision_width - 120.06336975097656f) < 0.0001f);
  assert(fabsf(course.mesh_grid.subdivision_length - 140.23280334472656f) < 0.0001f);
  assert(course.mesh_grid.num_subdivisions_x == 16);
  assert(course.mesh_grid.num_subdivisions_z == 16);
  assert(fabsf(course.bounding_sphere.radius - 1515.5f) < 0.0001f);

  assert(
      fzgx_static_collider_course_get_surface_tri_cell(
          &course, 10u, 40u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 82u);
  assert(indices != 0);
  assert(indices[0] == 0u);
  assert(indices[1] == 1u);
  assert(indices[19] == 23u);
  assert(indices[77] == 122u);
  assert(indices[81] == 131u);

  assert(
      fzgx_static_collider_course_get_surface_quad_cell(
          &course, 10u, 40u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 20u);
  assert(indices != 0);
  assert(indices[0] == 1u);
  assert(indices[15] == 37u);
  assert(indices[19] == 59u);

  assert(
      fzgx_static_collider_course_get_surface_tri_cell(
          &course, 3u, 15u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 1u);
  assert(indices != 0);
  assert(indices[0] == 144u);

  fzgx_content_release_static_collider_course(&course);
}

static void test_load_builtin_static_collider_course26_exact(void) {
  char path[256];
  fzgx_owned_static_collider_course course = {0};
  const uint16_t *indices = 0;
  uint32_t count = 0u;

  build_builtin_coli_course_path(path, sizeof(path), 26u);
  assert(fzgx_content_load_static_collider_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.surface_count == 14u);
  assert(course.tri_count == 495u);
  assert(course.quad_count == 2u);
  assert(course.tri_index_count == 2401u);
  assert(course.quad_index_count == 3u);
  assert(fabsf(course.bounding_sphere.radius - 2700.400634765625f) < 0.0001f);

  assert(
      fzgx_static_collider_course_get_surface_tri_cell(
          &course, 9u, 2u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 2u);
  assert(indices != 0);
  assert(indices[0] == 404u);
  assert(indices[1] == 405u);

  assert(
      fzgx_static_collider_course_get_surface_tri_cell(
          &course, 3u, 53u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 1u);
  assert(indices != 0);
  assert(indices[0] == 494u);

  assert(
      fzgx_static_collider_course_get_surface_quad_cell(
          &course, 3u, 53u, &indices, &count) == FZGX_STATUS_OK);
  assert(count == 1u);
  assert(indices != 0);
  assert(indices[0] == 1u);

  fzgx_content_release_static_collider_course(&course);
}

static void test_reject_invalid_static_collider_course_queries(void) {
  fzgx_owned_static_collider_course course = {0};
  const uint16_t *indices = 0;
  uint32_t count = 0u;

  assert(
      fzgx_content_load_static_collider_course_from_path(0, &course) == FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_load_static_collider_course_from_path("missing-file", 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_static_collider_course_get_surface_tri_cell(0, 0u, 0u, &indices, &count) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_static_collider_course_get_surface_quad_cell(&course, 0u, 0u, 0, &count) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_static_collider_course_get_surface_tri_cell(&course, 0u, 0u, &indices, 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_static_collider_course_get_surface_quad_cell(&course, 0u, 0u, &indices, &count) ==
      FZGX_STATUS_OUT_OF_RANGE);
}

static void test_load_builtin_dynamic_scene_collision_course08_exact(void) {
  char path[256];
  fzgx_owned_dynamic_scene_collision_course course = {0};

  build_builtin_coli_course_path(path, sizeof(path), 8u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.object_count == 130u);

  assert(course.objects[0].render_flags_0 == 0x00006013u);
  assert(course.objects[0].render_flags_4 == 0x80000004u);
  assert(strcmp(course.objects[0].primary_lod_name, "08YAJIRUSHI01") == 0);
  assert(course.objects[0].has_collider_mesh == 0u);

  assert(strcmp(course.objects[1].primary_lod_name, "DASH01_BOAD") == 0);
  assert(course.objects[1].render_flags_0 == 0x0008200fu);
  assert(course.objects[1].render_flags_4 == 0xffffffffu);

  assert(strcmp(course.objects[33].primary_lod_name, "08KABE01") == 0);
  assert(course.objects[33].has_collider_mesh == 1u);
  assert(course.objects[33].collider_mesh.collider_type == 0x00002000u);
  assert(course.objects[33].collider_mesh.tri_count == 4u);
  assert(course.objects[33].collider_mesh.quad_count == 0u);
  assert(course.objects[33].collider_mesh.tris != 0);
  assert(fabsf(course.objects[33].collider_mesh.bounding_sphere.radius - 17.493804931640625f) < 0.0001f);

  assert(strcmp(course.objects[57].primary_lod_name, "LIG_CORE_GEAR01_A_LOD") == 0);
  assert(course.objects[57].animation_clip_address == 0x00004a18u);
  assert(course.objects[57].has_animation_clip == 1u);
  assert(course.objects[57].animation_clip.time_start_frames == 0.0f);
  assert(course.objects[57].animation_clip.time_end_frames == 120.0f);
  assert(course.objects[57].animation_clip.bank_time_frames[0] == 0.0f);
  assert(course.objects[57].animation_clip.bank_time_frames[1] == 0.0f);
  assert(course.objects[57].animation_clip.bank_time_frames[2] == 0.0f);
  assert(course.objects[57].animation_clip.bank_time_frames[3] == 0.0f);
  assert(course.objects[57].animation_clip.layer_flags == 0x00010000u);
  assert(course.objects[57].animation_clip.curves[4].curve.keyable_count == 2u);
  assert(course.objects[57].animation_clip.curves[4].curve.keyables != 0);
  assert(course.objects[57].animation_clip.curves[4].curve.keyables[0].interpolation_mode == 1u);
  assert(course.objects[57].animation_clip.curves[4].curve.keyables[0].time == 0.0f);
  assert(course.objects[57].animation_clip.curves[4].curve.keyables[0].value == 0.0f);
  assert(course.objects[57].animation_clip.curves[4].curve.keyables[1].time == 2.0f);
  assert(course.objects[57].animation_clip.curves[4].curve.keyables[1].value == 180.0f);

  fzgx_content_release_dynamic_scene_collision_course(&course);
}

static void test_load_builtin_dynamic_scene_collision_course41_controller_clip_exact(void) {
  char path[256];
  fzgx_owned_dynamic_scene_collision_course course = {0};

  build_builtin_coli_course_path(path, sizeof(path), 41u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.object_count > 1104u);

  assert(strcmp(course.objects[1104].primary_lod_name, "DODECA") == 0);
  assert(course.objects[1104].render_flags_0 == 0x000600e3u);
  assert(course.objects[1104].has_collider_mesh == 1u);
  assert(course.objects[1104].collider_mesh.collider_type == 0x00000800u);
  assert(course.objects[1104].animation_clip_address == 0x0000b000u);
  assert(course.objects[1104].texture_scroll_address == 0x0001e7d0u);
  assert(course.objects[1104].skeletal_animator_address == 0u);
  assert(course.objects[1104].has_animation_clip == 1u);
  assert(course.objects[1104].animation_clip.time_start_frames == 0.0f);
  assert(course.objects[1104].animation_clip.time_end_frames == 60000.0f);
  assert(course.objects[1104].animation_clip.bank_time_frames[0] == 0.0f);
  assert(course.objects[1104].animation_clip.bank_time_frames[1] == 0.0f);
  assert(course.objects[1104].animation_clip.bank_time_frames[2] == 0.0f);
  assert(course.objects[1104].animation_clip.bank_time_frames[3] == 0.0f);
  assert(course.objects[1104].animation_clip.layer_flags == 0x00010000u);

  fzgx_content_release_dynamic_scene_collision_course(&course);
}

static void test_load_builtin_dynamic_scene_collision_course26_exact(void) {
  char path[256];
  fzgx_owned_dynamic_scene_collision_course course = {0};

  build_builtin_coli_course_path(path, sizeof(path), 26u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.object_count == 230u);

  assert(strcmp(course.objects[34].primary_lod_name, "MITI01") == 0);
  assert(course.objects[34].has_collider_mesh == 1u);
  assert(course.objects[34].collider_mesh.collider_type == 0x00001000u);
  assert(course.objects[34].collider_mesh.tri_count == 16u);
  assert(course.objects[34].collider_mesh.quad_count == 10u);
  assert(course.objects[34].collider_mesh.tris != 0);
  assert(course.objects[34].collider_mesh.quads != 0);
  assert(fabsf(course.objects[34].collider_mesh.bounding_sphere.radius - 396.141754150390625f) < 0.0001f);

  assert(strcmp(course.objects[46].primary_lod_name, "COBRA_SAND") == 0);
  assert(course.objects[46].has_animation_clip == 1u);
  assert(course.objects[46].animation_clip.time_start_frames == 0.0f);
  assert(course.objects[46].animation_clip.time_end_frames == 360.0f);
  assert(course.objects[46].animation_clip.layer_flags == 0x00040000u);
  assert(course.objects[46].animation_clip.curves[6].curve.keyable_count == 1u);
  assert(course.objects[46].animation_clip.curves[6].curve.keyables != 0);
  assert(fabsf(course.objects[46].animation_clip.curves[6].curve.keyables[0].value - -686.61572265625f) <
         0.0001f);
  assert(course.objects[46].animation_clip.curves[10].curve.keyable_count == 4u);
  assert(course.objects[46].animation_clip.curves[10].curve.keyables != 0);
  assert(course.objects[46].animation_clip.curves[10].curve.keyables[1].time == 3.0f);
  assert(fabsf(course.objects[46].animation_clip.curves[10].curve.keyables[1].value - 0.1f) <
         0.0001f);

  fzgx_content_release_dynamic_scene_collision_course(&course);
}

static void test_sample_dynamic_scene_object_transform_trxs_from_animation_clip_exact(void) {
  char path[256];
  fzgx_owned_dynamic_scene_collision_course course = {0};
  fzgx_transform_trxs_record sampled;

  build_builtin_coli_course_path(path, sizeof(path), 8u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(
      fzgx_dynamic_scene_object_sample_transform_trxs(&course.objects[57], 1.0f, &sampled) ==
      FZGX_STATUS_OK);
  assert(sampled.position.x == course.objects[57].transform.position.x);
  assert(sampled.position.y == course.objects[57].transform.position.y);
  assert(sampled.position.z == course.objects[57].transform.position.z);
  assert(sampled.rotation_x_angle16 == course.objects[57].transform.rotation_x_angle16);
  assert(sampled.rotation_y_angle16 == 0x4000u);
  assert(sampled.rotation_z_angle16 == course.objects[57].transform.rotation_z_angle16);
  assert(sampled.scale.x == course.objects[57].transform.scale.x);
  assert(sampled.scale.y == course.objects[57].transform.scale.y);
  assert(sampled.scale.z == course.objects[57].transform.scale.z);

  fzgx_content_release_dynamic_scene_collision_course(&course);
}

static void test_load_builtin_dynamic_scene_collision_course05_mine_transform_exact(void) {
  char path[256];
  fzgx_owned_dynamic_scene_collision_course course = {0};

  build_builtin_coli_course_path(path, sizeof(path), 5u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &course) == FZGX_STATUS_OK);
  assert(course.object_count != 0u);
  assert(strcmp(course.objects[0].primary_lod_name, "MINE01_MINE") == 0);
  assert((course.objects[0].render_flags_0 & 0x00000001u) != 0u);
  assert(course.objects[0].collider_mesh.collider_type == 0x00004000u);
  assert(course.objects[0].transform.unknown_transform_option == 0x0fu);
  assert(course.objects[0].transform.object_active_override == 0x00u);
  assert(course.objects[0].animation_clip_address == 0u);
  assert(course.objects[0].skeletal_animator_address == 0u);
  assert(course.objects[0].has_transform_matrix == 1u);
  assert(fabsf(course.objects[0].transform_matrix.origin.x - 564.27392578125f) < 0.0001f);
  assert(fabsf(course.objects[0].transform_matrix.origin.y - 246.2825927734375f) < 0.0001f);
  assert(fabsf(course.objects[0].transform_matrix.origin.z - 1271.6309814453125f) < 0.0001f);
  assert(course.objects[0].has_collider_mesh == 1u);
  assert(course.objects[0].collider_mesh.tri_count == 0u);
  assert(course.objects[0].collider_mesh.quad_count == 1u);

  fzgx_content_release_dynamic_scene_collision_course(&course);
}

static void test_load_builtin_dynamic_scene_collision_course25_unknown_and_static_exact(void) {
  char path[256];
  fzgx_owned_dynamic_scene_collision_course course = {0};

  build_builtin_coli_course_path(path, sizeof(path), 25u);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(path, &course) == FZGX_STATUS_OK);

  assert(course.unknown_collider_count == 6u);
  assert(strcmp(course.unknown_colliders[0].primary_lod_name, "CCC") == 0);
  assert(strcmp(course.unknown_colliders[1].primary_lod_name, "BBB") == 0);
  assert(fabsf(course.unknown_colliders[0].transform.position.x - -176.68589782714844f) < 0.0001f);
  assert(fabsf(course.unknown_colliders[0].transform.position.z - -481.5f) < 0.0001f);

  assert(course.static_scene_object_count == 68u);
  assert(strcmp(course.static_scene_objects[0].primary_lod_name, "FC510_NZ13Z") == 0);
  assert(strcmp(course.static_scene_objects[1].primary_lod_name, "FC600_NZ13Z") == 0);
  assert(course.static_scene_objects[0].has_collider_mesh == 0u);

  fzgx_content_release_dynamic_scene_collision_course(&course);
}

static void test_reject_invalid_dynamic_scene_collision_course_queries(void) {
  fzgx_owned_dynamic_scene_collision_course course = {0};

  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path(0, &course) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_load_dynamic_scene_collision_course_from_path("missing-file", 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
}

static void test_load_builtin_stage_gma_model_table01_exact(void) {
  char path[256];
  fzgx_owned_stage_gma_model_table table = {0};
  const fzgx_owned_stage_gma_model_record *model = 0;

  build_builtin_stage_gma_path(path, sizeof(path), 1u);
  assert(fzgx_content_load_stage_gma_model_table_from_path(path, &table) == FZGX_STATUS_OK);
  assert(table.model_count == 22u);
  assert(table.model_base_address == 0x000001e0u);
  assert(table.name_base_address == 0x000000b8u);
  assert(fzgx_stage_gma_model_table_find_model_by_name(&table, "C01_MAP", &model) == FZGX_STATUS_OK);
  assert(model != 0);
  assert(model->index == 21u);
  assert(model->gcmf_address == 0x000992c0u);
  assert(model->gcmf_size == 0x000001a0u);
  assert(model->gcmf_magic == 0x47434d46u);
  assert(model->gcmf_attributes == 0u);
  assert(fabsf(model->gcmf_bounding_sphere.radius - 7.071067810058594f) < 0.0001f);
  assert(model->gcmf_texture_count == 2u);
  assert(model->gcmf_opaque_material_count == 0u);
  assert(model->gcmf_translucid_material_count == 1u);
  assert(model->gcmf_bone_count == 0u);
  assert(model->gcmf_submesh_offset == 0x80u);
  assert(model->gcmf_zero_0x24 == 0u);
  assert(model->name_address > table.name_base_address);

  fzgx_content_release_stage_gma_model_table(&table);
}

static void test_load_builtin_stage_gma_model_table08_exact(void) {
  char path[256];
  fzgx_owned_stage_gma_model_table table = {0};
  const fzgx_owned_stage_gma_model_record *model = 0;

  build_builtin_stage_gma_path(path, sizeof(path), 8u);
  assert(fzgx_content_load_stage_gma_model_table_from_path(path, &table) == FZGX_STATUS_OK);
  assert(table.model_count == 40u);
  assert(table.model_base_address == 0x00000380u);
  assert(table.name_base_address == 0x00000148u);
  assert(fzgx_stage_gma_model_table_find_model_by_name(&table, "C08_MAP", &model) == FZGX_STATUS_OK);
  assert(model != 0);
  assert(model->index == 39u);
  assert(model->gcmf_address == 0x002b3720u);
  assert(model->gcmf_size == 0x000001a0u);
  assert(model->gcmf_magic == 0x47434d46u);
  assert(model->gcmf_attributes == 0u);
  assert(fabsf(model->gcmf_bounding_sphere.radius - 7.071067810058594f) < 0.0001f);
  assert(model->gcmf_texture_count == 2u);
  assert(model->gcmf_opaque_material_count == 0u);
  assert(model->gcmf_translucid_material_count == 1u);
  assert(model->gcmf_bone_count == 0u);
  assert(model->gcmf_submesh_offset == 0x80u);
  assert(model->gcmf_zero_0x24 == 0u);

  fzgx_content_release_stage_gma_model_table(&table);
}

static void test_load_builtin_stage_gma_model_table25_exact(void) {
  char path[256];
  fzgx_owned_stage_gma_model_table table = {0};
  const fzgx_owned_stage_gma_model_record *model = 0;

  build_builtin_stage_gma_path(path, sizeof(path), 25u);
  assert(fzgx_content_load_stage_gma_model_table_from_path(path, &table) == FZGX_STATUS_OK);
  assert(table.model_count == 114u);
  assert(table.model_base_address == 0x000007a0u);
  assert(table.name_base_address == 0x00000398u);
  assert(fzgx_stage_gma_model_table_find_model_by_name(&table, "C25_MAP", &model) == FZGX_STATUS_OK);
  assert(model != 0);
  assert(model->index == 9u);
  assert(model->gcmf_address == 0x0008ad00u);
  assert(model->gcmf_size == 0x000001a0u);
  assert(model->gcmf_magic == 0x47434d46u);
  assert(model->gcmf_attributes == 0u);
  assert(fabsf(model->gcmf_bounding_sphere.radius - 7.071067810058594f) < 0.0001f);
  assert(model->gcmf_texture_count == 2u);
  assert(model->gcmf_opaque_material_count == 0u);
  assert(model->gcmf_translucid_material_count == 1u);
  assert(model->gcmf_bone_count == 0u);
  assert(model->gcmf_submesh_offset == 0x80u);
  assert(model->gcmf_zero_0x24 == 0u);

  fzgx_content_release_stage_gma_model_table(&table);
}

static void test_reject_invalid_stage_gma_model_table_queries(void) {
  fzgx_owned_stage_gma_model_table table = {0};

  assert(
      fzgx_content_load_stage_gma_model_table_from_path(0, &table) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_content_load_stage_gma_model_table_from_path("missing-file", 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
  assert(
      fzgx_stage_gma_model_table_find_model_by_name(0, "C01_MAP", 0) ==
      FZGX_STATUS_BAD_ARGUMENT);
}

int main(void) {
  test_validate_null_bundle();
  test_validate_minimal_bundle();
  test_validate_missing_tables();
  test_builtin_iso_bundle();
  test_load_track_mesh_course_exact();
  test_select_track_frame_export_record();
  test_find_nearest_track_frame_anchor_index();
  test_build_nearest_track_frame_export_buffer();
  test_reject_invalid_track_frame_export_buffer();
  test_can_traverse_checkpoint_interval_helpers_exact();
  test_find_checkpoint_for_track_distance_exact();
  test_find_track_course_and_checkpoint_variant();
  test_can_traverse_checkpoint_interval_without_pipe_open_transition();
  test_reject_invalid_track_course_queries();
  test_compute_checkpoint_t_for_point();
  test_compute_curve_time_for_checkpoint_fraction();
  test_reject_invalid_checkpoint_t_query();
  test_evaluate_float_animation_curve();
  test_checkpoint_variant_contains_point();
  test_scan_checkpoint_neighbors_for_point();
  test_find_shared_checkpoint_for_point();
  test_find_nearest_checkpoint_for_point();
  test_find_nearest_checkpoint_for_point_handles_track7_lower_z_special_case();
  test_build_checkpoint_query_results();
  test_resolve_branch_checkpoint_from_seed_helpers();
  test_build_seeded_checkpoint_query_results();
  test_build_active_checkpoint_bank_for_point();
  test_build_cached_frames_for_checkpoint_populates_family_specific_tail();
  test_build_cached_frames_for_checkpoint_counts_branch_root_plus_selected_path();
  test_build_cached_frames_for_checkpoint_keeps_0x200000_width_unset();
  test_build_current_track_query_result_from_bank_and_frame_buffer();
  test_build_current_track_query_result_from_bank_and_nearest_frame();
  test_build_current_track_query_result_for_point();
  test_build_cached_frames_for_checkpoint_on_builtin_ruby_courses();
  test_builtin_family_specific_cached_frame_tail_shape_invariants();
  test_builtin_0x200000_cached_frame_family_shape_invariants();
  test_build_current_track_query_result_for_active_checkpoint();
  test_build_ordinary_start_grid_slot_transform();
  test_build_ordinary_start_grid_slot_query_result();
  test_build_ordinary_start_grid_slot_transform_prefers_branch_slot1();
  test_track_frame_get_width_and_scale();
  test_track_frame_sample_world_pos();
  test_load_builtin_static_collider_course01_exact();
  test_load_builtin_static_collider_course26_exact();
  test_reject_invalid_static_collider_course_queries();
  test_load_builtin_dynamic_scene_collision_course08_exact();
  test_load_builtin_dynamic_scene_collision_course41_controller_clip_exact();
  test_load_builtin_dynamic_scene_collision_course05_mine_transform_exact();
  test_load_builtin_dynamic_scene_collision_course25_unknown_and_static_exact();
  test_load_builtin_dynamic_scene_collision_course26_exact();
  test_sample_dynamic_scene_object_transform_trxs_from_animation_clip_exact();
  test_reject_invalid_dynamic_scene_collision_course_queries();
  test_load_builtin_stage_gma_model_table01_exact();
  test_load_builtin_stage_gma_model_table08_exact();
  test_load_builtin_stage_gma_model_table25_exact();
  test_reject_invalid_stage_gma_model_table_queries();
  test_track_segment_sample_trs();
  test_track_segment_apply_trs();
  return 0;
}
