#ifndef FZGX_CONTENT_H
#define FZGX_CONTENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FZGX_CONTENT_API_VERSION
#define FZGX_CONTENT_API_VERSION 1u
#endif

typedef enum fzgx_status {
  FZGX_STATUS_OK = 0,
  FZGX_STATUS_BAD_ARGUMENT = 1,
  FZGX_STATUS_OUT_OF_RANGE = 2,
  FZGX_STATUS_NOT_CONFIGURED = 3,
  FZGX_STATUS_UNIMPLEMENTED = 4
} fzgx_status;

enum {
  FZGX_CIRCUIT_TYPE_CLOSED = 0x00000000u,
  FZGX_CIRCUIT_TYPE_OPEN = 0x00010000u
};

typedef struct fzgx_vec3 {
  float x;
  float y;
  float z;
} fzgx_vec3;

typedef struct fzgx_mat43 {
  float basis_x_x;
  float basis_y_x;
  float basis_z_x;
  float origin_x;
  float basis_x_y;
  float basis_y_y;
  float basis_z_y;
  float origin_y;
  float basis_x_z;
  float basis_y_z;
  float basis_z_z;
  float origin_z;
} fzgx_mat43;

static inline fzgx_vec3 fzgx_mat43_get_basis_x_exact(const fzgx_mat43 *transform) {
  return (fzgx_vec3){transform->basis_x_x, transform->basis_x_y, transform->basis_x_z};
}

static inline fzgx_vec3 fzgx_mat43_get_basis_y_exact(const fzgx_mat43 *transform) {
  return (fzgx_vec3){transform->basis_y_x, transform->basis_y_y, transform->basis_y_z};
}

static inline fzgx_vec3 fzgx_mat43_get_basis_z_exact(const fzgx_mat43 *transform) {
  return (fzgx_vec3){transform->basis_z_x, transform->basis_z_y, transform->basis_z_z};
}

static inline fzgx_vec3 fzgx_mat43_get_origin_exact(const fzgx_mat43 *transform) {
  return (fzgx_vec3){transform->origin_x, transform->origin_y, transform->origin_z};
}

static inline void fzgx_mat43_set_basis_x_exact(fzgx_mat43 *transform, fzgx_vec3 value) {
  transform->basis_x_x = value.x;
  transform->basis_x_y = value.y;
  transform->basis_x_z = value.z;
}

static inline void fzgx_mat43_set_basis_y_exact(fzgx_mat43 *transform, fzgx_vec3 value) {
  transform->basis_y_x = value.x;
  transform->basis_y_y = value.y;
  transform->basis_y_z = value.z;
}

static inline void fzgx_mat43_set_basis_z_exact(fzgx_mat43 *transform, fzgx_vec3 value) {
  transform->basis_z_x = value.x;
  transform->basis_z_y = value.y;
  transform->basis_z_z = value.z;
}

static inline void fzgx_mat43_set_origin_exact(fzgx_mat43 *transform, fzgx_vec3 value) {
  transform->origin_x = value.x;
  transform->origin_y = value.y;
  transform->origin_z = value.z;
}

typedef struct fzgx_track_manifest {
  uint32_t authored_track_id;
  uint32_t checkpoint_count;
  uint32_t checkpoint_variant_count;
  uint32_t circuit_type;
  uint32_t time_extension_trigger_count;
  uint8_t supports_branching;
  uint8_t reserved[3];
} fzgx_track_manifest;

typedef struct fzgx_plane {
  float distance;
  fzgx_vec3 normal;
  fzgx_vec3 origin;
} fzgx_plane;

typedef struct fzgx_checkpoint_record {
  float curve_time_start;
  float curve_time_end;
  fzgx_plane plane_start;
  fzgx_plane plane_end;
  float start_distance;
  float end_distance;
  float track_width;
  uint8_t connect_to_track_in;
  uint8_t connect_to_track_out;
  uint16_t reserved0x4e;
} fzgx_checkpoint_record;

typedef struct fzgx_track_node_record {
  uint32_t checkpoint_count;
  uint32_t checkpoint_offset;
  uint32_t checkpoint_address;
  uint32_t root_segment_address;
} fzgx_track_node_record;

typedef struct fzgx_track_segment_record {
  uint32_t address;
  uint8_t segment_type;
  uint8_t embedded_property_type;
  uint8_t perimeter_flags;
  uint8_t pipe_cylinder_flags;
  uint32_t animation_curves_trs_address;
  uint32_t track_corner_address;
  uint32_t children_count;
  uint32_t children_address;
  fzgx_vec3 fallback_scale;
  fzgx_vec3 fallback_rotation;
  fzgx_vec3 fallback_position;
  uint16_t root_unk_0x38;
  uint16_t root_unk_0x3a;
  float rail_height_right;
  float rail_height_left;
  uint32_t zero_0x44;
  uint32_t zero_0x48;
  int32_t branch_index;
} fzgx_track_segment_record;

typedef struct fzgx_track_corner_record {
  uint32_t address;
  fzgx_mat43 transform;
  float width;
  uint8_t const_0x34;
  uint8_t zero_0x35;
  uint8_t perimeter_flags;
  uint8_t zero_0x37;
} fzgx_track_corner_record;

typedef struct fzgx_time_extension_trigger_record {
  fzgx_vec3 position;
  uint16_t rotation_x_angle16;
  uint16_t rotation_y_angle16;
  uint16_t rotation_z_angle16;
  uint8_t unknown_transform_option;
  uint8_t object_active_override;
  fzgx_vec3 scale;
  uint32_t option;
} fzgx_time_extension_trigger_record;

typedef struct fzgx_grid_xz_record {
  float left;
  float top;
  float subdivision_width;
  float subdivision_length;
  int32_t num_subdivisions_x;
  int32_t num_subdivisions_z;
} fzgx_grid_xz_record;

typedef struct fzgx_bounding_sphere_record {
  fzgx_vec3 origin;
  float radius;
} fzgx_bounding_sphere_record;

typedef struct fzgx_static_collider_triangle_record {
  float plane_distance;
  fzgx_vec3 normal;
  fzgx_vec3 vertex0;
  fzgx_vec3 vertex1;
  fzgx_vec3 vertex2;
  fzgx_vec3 edge_normal0;
  fzgx_vec3 edge_normal1;
  fzgx_vec3 edge_normal2;
} fzgx_static_collider_triangle_record;

typedef struct fzgx_static_collider_quad_record {
  float plane_distance;
  fzgx_vec3 normal;
  fzgx_vec3 vertex0;
  fzgx_vec3 vertex1;
  fzgx_vec3 vertex2;
  fzgx_vec3 vertex3;
  fzgx_vec3 edge_normal0;
  fzgx_vec3 edge_normal1;
  fzgx_vec3 edge_normal2;
  fzgx_vec3 edge_normal3;
} fzgx_static_collider_quad_record;

typedef struct fzgx_static_collider_index_span {
  uint32_t offset;
  uint32_t count;
} fzgx_static_collider_index_span;

enum {
  FZGX_STATIC_COLLIDER_GRID_CELL_COUNT = 256u,
  FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE = 0u,
  FZGX_STATIC_COLLIDER_SURFACE_RECOVER = 1u,
  FZGX_STATIC_COLLIDER_SURFACE_WALL = 2u,
  FZGX_STATIC_COLLIDER_SURFACE_DASH = 3u,
  FZGX_STATIC_COLLIDER_SURFACE_JUMP = 4u,
  FZGX_STATIC_COLLIDER_SURFACE_ICE = 5u,
  FZGX_STATIC_COLLIDER_SURFACE_DIRT = 6u,
  FZGX_STATIC_COLLIDER_SURFACE_DAMAGE = 7u,
  FZGX_STATIC_COLLIDER_SURFACE_OUT_OF_BOUNDS = 8u,
  FZGX_STATIC_COLLIDER_SURFACE_DEATH_GROUND = 9u,
  FZGX_STATIC_COLLIDER_SURFACE_DEATH_1 = 10u,
  FZGX_STATIC_COLLIDER_SURFACE_DEATH_2 = 11u,
  FZGX_STATIC_COLLIDER_SURFACE_DEATH_3 = 12u,
  FZGX_STATIC_COLLIDER_SURFACE_DEATH_4 = 13u
};

typedef struct fzgx_static_collider_surface_grid {
  fzgx_static_collider_index_span tri_cells[FZGX_STATIC_COLLIDER_GRID_CELL_COUNT];
  fzgx_static_collider_index_span quad_cells[FZGX_STATIC_COLLIDER_GRID_CELL_COUNT];
} fzgx_static_collider_surface_grid;

typedef struct fzgx_owned_static_collider_course {
  uint32_t surface_count;
  fzgx_grid_xz_record mesh_grid;
  fzgx_bounding_sphere_record bounding_sphere;
  uint32_t tri_count;
  uint32_t quad_count;
  uint32_t tri_index_count;
  uint32_t quad_index_count;
  fzgx_static_collider_triangle_record *tris;
  fzgx_static_collider_quad_record *quads;
  uint16_t *tri_indices;
  uint16_t *quad_indices;
  fzgx_static_collider_surface_grid *surface_grids;
} fzgx_owned_static_collider_course;

typedef struct fzgx_transform_trxs_record {
  fzgx_vec3 position;
  uint16_t rotation_x_angle16;
  uint16_t rotation_y_angle16;
  uint16_t rotation_z_angle16;
  uint8_t unknown_transform_option;
  uint8_t object_active_override;
  fzgx_vec3 scale;
} fzgx_transform_trxs_record;

typedef struct fzgx_keyable_attribute {
  uint32_t interpolation_mode;
  float time;
  float value;
  float tangent_in;
  float tangent_out;
} fzgx_keyable_attribute;

typedef struct fzgx_animation_curve {
  uint32_t keyable_count;
  const fzgx_keyable_attribute *keyables;
} fzgx_animation_curve;

typedef struct fzgx_owned_scene_object_collider_mesh {
  uint32_t collider_type;
  fzgx_bounding_sphere_record bounding_sphere;
  uint32_t tri_count;
  uint32_t quad_count;
  fzgx_static_collider_triangle_record *tris;
  fzgx_static_collider_quad_record *quads;
} fzgx_owned_scene_object_collider_mesh;

typedef struct fzgx_owned_animation_clip_curve_record {
  uint32_t unknown_0x00;
  uint32_t unknown_0x04;
  uint32_t unknown_0x08;
  uint32_t unknown_0x0c;
  fzgx_animation_curve curve;
} fzgx_owned_animation_clip_curve_record;

typedef struct fzgx_owned_animation_clip_record {
  float time_start_frames;
  float time_end_frames;
  float bank_time_frames[4];
  uint32_t layer_flags;
  fzgx_owned_animation_clip_curve_record curves[11];
} fzgx_owned_animation_clip_record;

typedef struct fzgx_owned_dynamic_scene_object_record {
  uint32_t render_flags_0;
  uint32_t render_flags_4;
  uint32_t scene_object_address;
  fzgx_transform_trxs_record transform;
  uint32_t collision_transform_address;
  uint8_t has_collision_transform;
  uint8_t reserved3[3];
  fzgx_transform_trxs_record collision_transform;
  uint32_t animation_clip_address;
  uint8_t has_animation_clip;
  uint8_t reserved2[3];
  fzgx_owned_animation_clip_record animation_clip;
  uint32_t texture_scroll_address;
  uint32_t skeletal_animator_address;
  uint32_t transform_matrix_address;
  uint8_t has_transform_matrix;
  uint8_t reserved1[3];
  fzgx_mat43 transform_matrix;
  char primary_lod_name[64];
  uint8_t has_collider_mesh;
  uint8_t reserved0[3];
  fzgx_owned_scene_object_collider_mesh collider_mesh;
} fzgx_owned_dynamic_scene_object_record;

typedef struct fzgx_owned_unknown_collider_record {
  uint32_t scene_object_address;
  fzgx_transform_trxs_record transform;
  char primary_lod_name[64];
  uint8_t has_collider_mesh;
  uint8_t reserved[3];
  fzgx_owned_scene_object_collider_mesh collider_mesh;
} fzgx_owned_unknown_collider_record;

typedef struct fzgx_owned_static_scene_object_record {
  uint32_t scene_object_address;
  char primary_lod_name[64];
  uint8_t has_collider_mesh;
  uint8_t reserved[3];
  fzgx_owned_scene_object_collider_mesh collider_mesh;
} fzgx_owned_static_scene_object_record;

typedef struct fzgx_owned_dynamic_scene_collision_course {
  uint32_t object_count;
  fzgx_owned_dynamic_scene_object_record *objects;
  uint32_t unknown_collider_count;
  fzgx_owned_unknown_collider_record *unknown_colliders;
  uint32_t static_scene_object_count;
  fzgx_owned_static_scene_object_record *static_scene_objects;
} fzgx_owned_dynamic_scene_collision_course;

enum {
  FZGX_TRACK_MESH_CLASS_COUNT_AX = 11u,
  FZGX_TRACK_MESH_CLASS_COUNT_GX = 14u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT = 6u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_X = 0u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Y = 1u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Z = 2u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_X = 3u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Y = 4u,
  FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Z = 5u
};

typedef struct fzgx_owned_track_mesh_animation_channel {
  uint32_t keyable_count;
  fzgx_keyable_attribute *keyables;
} fzgx_owned_track_mesh_animation_channel;

typedef struct fzgx_owned_track_mesh_animation_record {
  uint32_t address;
  uint8_t has_animation;
  uint8_t reserved[3];
  fzgx_owned_track_mesh_animation_channel channels[FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT];
} fzgx_owned_track_mesh_animation_record;

typedef struct fzgx_owned_track_mesh_attachment_record {
  uint32_t address;
  fzgx_vec3 point;
  float unknown_0x0c;
} fzgx_owned_track_mesh_attachment_record;

typedef struct fzgx_owned_track_mesh_chunk {
  uint32_t address;
  uint32_t class_count;
  uint32_t cell_count;
  fzgx_vec3 unk_vec3_0x0;
  uint16_t rotation_x_angle16;
  uint16_t rotation_y_angle16;
  uint16_t rotation_z_angle16;
  uint16_t flags_0x12;
  uint32_t animation_record_address;
  uint8_t has_animation_record;
  uint8_t reserved0[3];
  fzgx_owned_track_mesh_animation_record animation_record;
  fzgx_vec3 unk_vec3_0x18;
  uint32_t tri_vertex_base_address;
  uint32_t tri_cell_table_addresses[FZGX_TRACK_MESH_CLASS_COUNT_GX];
  float grid_origin_x;
  float grid_origin_z;
  float inv_cell_size_x;
  float inv_cell_size_z;
  int32_t grid_subdiv_x;
  int32_t grid_subdiv_z;
  uint32_t quad_vertex_base_address;
  uint32_t quad_cell_table_addresses[FZGX_TRACK_MESH_CLASS_COUNT_GX];
  uint32_t unknown_0xb8_address;
  uint32_t unknown_0xc0_address;
  uint32_t unknown_0xc8_address;
  uint32_t unknown_0xd0_address;
  uint32_t unknown_count_0xd4;
  uint32_t unknown_0xd8_address;
  uint32_t render_instance_count_0xdc;
  uint32_t render_instance_table_0xe0_address;
  uint32_t unknown_0xe8_address;
  uint32_t attachment_record_address;
  uint8_t has_attachment_record;
  uint8_t reserved1[3];
  fzgx_owned_track_mesh_attachment_record attachment_record;
  uint32_t flags_0x104;
  float time_start_0x108;
  float time_end_0x10c;
  uint32_t curve_slot_addresses[4];
  uint32_t tri_count;
  uint32_t quad_count;
  uint32_t tri_index_count;
  uint32_t quad_index_count;
  fzgx_static_collider_triangle_record *tris;
  fzgx_static_collider_quad_record *quads;
  uint16_t *tri_indices;
  uint16_t *quad_indices;
  fzgx_static_collider_index_span *tri_cells;
  fzgx_static_collider_index_span *quad_cells;
} fzgx_owned_track_mesh_chunk;

typedef struct fzgx_owned_track_mesh_course {
  uint32_t class_count;
  uint32_t chunk_count;
  fzgx_owned_track_mesh_chunk *chunks;
} fzgx_owned_track_mesh_course;

typedef struct fzgx_owned_stage_gma_model_record {
  uint32_t index;
  uint32_t is_null;
  uint32_t gcmf_relative_offset;
  uint32_t name_relative_offset;
  uint32_t gcmf_address;
  uint32_t gcmf_size;
  uint32_t gcmf_magic;
  uint32_t gcmf_attributes;
  fzgx_bounding_sphere_record gcmf_bounding_sphere;
  uint16_t gcmf_texture_count;
  uint16_t gcmf_opaque_material_count;
  uint16_t gcmf_translucid_material_count;
  uint8_t gcmf_bone_count;
  uint8_t gcmf_reserved_0x1f;
  uint32_t gcmf_submesh_offset;
  uint32_t gcmf_zero_0x24;
  int8_t gcmf_bone_indices[8];
  uint32_t name_address;
  char name[64];
} fzgx_owned_stage_gma_model_record;

typedef struct fzgx_owned_stage_gma_model_table {
  uint32_t model_count;
  uint32_t model_base_address;
  uint32_t name_base_address;
  fzgx_owned_stage_gma_model_record *models;
} fzgx_owned_stage_gma_model_table;

typedef struct fzgx_track_course_content {
  uint32_t authored_track_id;
  uint32_t track_node_count;
  uint32_t checkpoint_record_count;
  uint32_t track_segment_count;
  uint32_t track_corner_count;
  float track_total_distance;
  float track_min_height;
  uint32_t time_extension_trigger_count;
  const fzgx_time_extension_trigger_record *time_extension_triggers;
  const fzgx_track_node_record *track_nodes;
  const fzgx_checkpoint_record *checkpoints;
  const fzgx_track_segment_record *track_segments;
  const fzgx_track_corner_record *track_corners;
} fzgx_track_course_content;

typedef struct fzgx_animation_curve_trs {
  uint32_t curve_count;
  const fzgx_animation_curve *curves;
} fzgx_animation_curve_trs;

typedef struct fzgx_track_segment_animation_record {
  uint32_t address;
  uint32_t animation_curves_trs_address;
  const fzgx_animation_curve_trs *animation_curve_trs;
} fzgx_track_segment_animation_record;

typedef struct fzgx_track_course_animation_content {
  uint32_t authored_track_id;
  uint32_t animation_curve_trs_count;
  uint32_t animation_curve_count;
  uint32_t keyable_attribute_count;
  uint32_t track_segment_count;
  const fzgx_track_segment_animation_record *track_segments;
} fzgx_track_course_animation_content;

typedef struct fzgx_owned_track_course_content {
  fzgx_track_course_content course;
  fzgx_time_extension_trigger_record *time_extension_triggers;
  fzgx_track_node_record *track_nodes;
  fzgx_checkpoint_record *checkpoints;
  fzgx_track_segment_record *track_segments;
  fzgx_track_corner_record *track_corners;
} fzgx_owned_track_course_content;

typedef struct fzgx_owned_track_course_animation_content {
  fzgx_track_course_animation_content course;
  fzgx_track_segment_animation_record *track_segments;
  fzgx_animation_curve_trs *animation_curve_trs;
  fzgx_animation_curve *animation_curves;
  fzgx_keyable_attribute *keyable_attributes;
} fzgx_owned_track_course_animation_content;

typedef struct fzgx_machine_definition {
  uint32_t machine_id;
  float weight;
  float acceleration;
  float max_speed;
  float grip_1;
  float grip_3;
  float turn_tension;
  float drift_accel;
  float turn_movement;
  float strafe_turn;
  float strafe;
  float turn_reaction;
  float grip_2;
  float boost_strength;
  float boost_length;
  float turn_decel;
  float drag;
  float body;
  uint8_t grip_frames_from_accel_press;
  uint8_t state_flags;
  uint8_t reserved_stat_0x4a;
  uint8_t reserved_stat_0x4b;
  float camera_reorienting;
  float camera_repositioning;
  fzgx_vec3 suspension_offsets[4];
  fzgx_vec3 wall_offsets[4];
  uint8_t is_custom_machine;
  uint8_t reserved[3];
  char name[64];
} fzgx_machine_definition;

typedef struct fzgx_track_frame_record {
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
  uint32_t track_flags;
} fzgx_track_frame_record;

typedef struct fzgx_track_frame_surface_tail {
  float track_scl_x;
  float track_scl_y;
  float track_width_or_radius;
  float track_hcylin;
  fzgx_vec3 track_follow_offset;
} fzgx_track_frame_surface_tail;

typedef struct fzgx_current_track_query_result {
  int32_t checkpoint_index;
  float checkpoint_fraction;
  int32_t active_bank_cp_idx[4];
  float active_bank_cp_frac[4];
  int32_t segment_index;
  uint32_t checkpoint_variant_count;
  uint32_t cached_frame_count;
  int32_t selected_cached_frame_index;
  float lap_progress_fraction;
  float last_frac_diff;
  fzgx_track_frame_record frame;
} fzgx_current_track_query_result;

typedef struct fzgx_current_checkpoint_query_result {
  int32_t checkpoint_index;
  float checkpoint_fraction;
  uint32_t variant_index;
  uint32_t reserved0;
  fzgx_vec3 point_on_track;
} fzgx_current_checkpoint_query_result;

typedef struct fzgx_active_checkpoint_bank_result {
  uint32_t checkpoint_variant_count;
  uint32_t preferred_variant_slot;
  int32_t checkpoint_index[4];
  float checkpoint_fraction[4];
  int32_t containment_checkpoint_index[4];
} fzgx_active_checkpoint_bank_result;

typedef struct fzgx_track_frame_export_buffer {
  const fzgx_track_frame_record *cached_frames;
  uint32_t cached_frame_count;
  int32_t selected_frame_index;
} fzgx_track_frame_export_buffer;

enum {
  FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY = 4u
};

typedef struct fzgx_track_segment_trs_sample {
  fzgx_vec3 scale;
  fzgx_vec3 rotation;
  fzgx_vec3 position;
} fzgx_track_segment_trs_sample;

typedef struct fzgx_track_segment_trs_curve_cache_exact {
  uint32_t cursor;
  const fzgx_track_segment_record *track_segment[10];
  int32_t last_segment_index[10][9];
} fzgx_track_segment_trs_curve_cache_exact;

typedef struct fzgx_content_bundle {
  uint32_t api_version;
  uint32_t track_count;
  uint32_t machine_count;
  uint32_t track_course_count;
  uint32_t track_animation_course_count;
  const fzgx_track_manifest *tracks;
  const fzgx_machine_definition *machines;
  const fzgx_track_course_content *track_courses;
  const fzgx_track_course_animation_content *track_animations;
} fzgx_content_bundle;

enum {
  FZGX_RANDOM_TRACK_API_VERSION = 1u,
  FZGX_RANDOM_TRACK_MAX_VARIANTS = 4u,
  FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD = 0u,
  FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD = 1u,
  FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED = 2u,
  FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED = 3u,
  FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN = 4u,
  FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN = 5u,
  FZGX_RANDOM_TRACK_FAMILY_CAPSULE = 6u
};

typedef struct fzgx_random_track_variant_recipe {
  fzgx_vec3 center;
  fzgx_vec3 forward;
  fzgx_vec3 up;
  float width;
  float half_height;
  float openness;
  uint8_t family;
  uint8_t base_surface_kind;
  uint8_t overlay_surface_kind;
  uint8_t reserved0;
} fzgx_random_track_variant_recipe;

typedef struct fzgx_random_track_node_recipe {
  uint32_t variant_count;
  uint8_t gap_after_mask;
  uint8_t sharp_after_mask;
  uint8_t mine_mask;
  uint8_t reserved0;
  fzgx_random_track_variant_recipe variants[FZGX_RANDOM_TRACK_MAX_VARIANTS];
} fzgx_random_track_node_recipe;

typedef struct fzgx_random_track_recipe {
  uint32_t api_version;
  uint32_t seed_hash_low32;
  uint32_t authored_track_id;
  uint32_t node_count;
  float track_total_distance;
  float track_min_height;
  fzgx_random_track_node_recipe *nodes;
} fzgx_random_track_recipe;

typedef struct fzgx_random_track_config {
  uint32_t api_version;
  uint32_t node_count;
  uint32_t branch_window_count;
  uint32_t flags;
} fzgx_random_track_config;

typedef struct fzgx_generated_track_content {
  fzgx_track_manifest manifest;
  fzgx_owned_track_course_content track_course;
  fzgx_owned_track_course_animation_content animation_course;
  fzgx_owned_static_collider_course static_course;
  fzgx_owned_dynamic_scene_collision_course dynamic_course;
} fzgx_generated_track_content;

const fzgx_content_bundle *fzgx_content_get_builtin_iso_bundle(void);

fzgx_status fzgx_random_track_generate_recipe(
    uint64_t seed,
    const fzgx_random_track_config *config,
    fzgx_random_track_recipe *recipe_out);
void fzgx_random_track_release_recipe(fzgx_random_track_recipe *recipe);
fzgx_status fzgx_random_track_compile_recipe(
    const fzgx_random_track_recipe *recipe,
    fzgx_generated_track_content *generated_out);
void fzgx_random_track_release_generated_track_content(
    fzgx_generated_track_content *generated);

fzgx_status fzgx_content_bundle_validate(const fzgx_content_bundle *bundle);
fzgx_status fzgx_track_frame_export_buffer_select(
    const fzgx_track_frame_export_buffer *buffer,
    fzgx_track_frame_record *frame_out);
fzgx_status fzgx_track_frame_export_buffer_find_nearest_anchor_index(
    const fzgx_track_frame_export_buffer *buffer,
    const fzgx_vec3 *point,
    int32_t *selected_index_out);
fzgx_status fzgx_track_frame_export_buffer_build_nearest_anchor_selection(
    const fzgx_track_frame_record *cached_frames,
    uint32_t cached_frame_count,
    const fzgx_vec3 *point,
    fzgx_track_frame_export_buffer *buffer_out);
fzgx_status fzgx_track_frame_get_width_and_scale(
    const fzgx_track_frame_record *frame,
    float *width_out,
    float *scale_out);
fzgx_status fzgx_track_frame_sample_world_pos(
    float lateral_offset,
    const fzgx_track_frame_record *frame,
    fzgx_vec3 *world_pos_out);
fzgx_status fzgx_track_segment_sample_surface_world_pos(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    const fzgx_mat43 *transform,
    const fzgx_vec3 *scale,
    uint32_t source_piece_word,
    float lateral_offset,
    fzgx_vec3 *world_pos_out);
fzgx_status fzgx_content_bundle_find_track_course(
    const fzgx_content_bundle *bundle,
    uint32_t authored_track_id,
    const fzgx_track_course_content **course_out);
fzgx_status fzgx_content_bundle_get_track_course_for_track_index(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    const fzgx_track_course_content **course_out);
fzgx_status fzgx_content_bundle_find_track_course_animation(
    const fzgx_content_bundle *bundle,
    uint32_t authored_track_id,
    const fzgx_track_course_animation_content **course_out);
fzgx_status fzgx_content_bundle_get_track_course_animation_for_track_index(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    const fzgx_track_course_animation_content **course_out);
fzgx_status fzgx_track_course_get_track_node(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    const fzgx_track_node_record **track_node_out);
fzgx_status fzgx_track_course_animation_find_track_segment_by_address(
    const fzgx_track_course_animation_content *course,
    uint32_t address,
    const fzgx_track_segment_animation_record **track_segment_out);
fzgx_status fzgx_track_course_find_track_segment_by_address(
    const fzgx_track_course_content *course,
    uint32_t address,
    const fzgx_track_segment_record **track_segment_out);
fzgx_status fzgx_track_course_find_track_corner_by_address(
    const fzgx_track_course_content *course,
    uint32_t address,
    const fzgx_track_corner_record **track_corner_out);
fzgx_status fzgx_evaluate_float_animation_curve(
    const fzgx_animation_curve *curve,
    float time,
    float *value_out);
fzgx_status fzgx_evaluate_float_animation_curve_cached(
    const fzgx_animation_curve *curve,
    float time,
    int32_t *last_segment_index_inout,
    float *value_out);
fzgx_status fzgx_dynamic_scene_object_sample_transform_trxs(
    const fzgx_owned_dynamic_scene_object_record *object,
    float clip_time_seconds,
    fzgx_transform_trxs_record *transform_out);
fzgx_status fzgx_track_segment_sample_trs(
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    fzgx_track_segment_trs_sample *sample_out);
fzgx_status fzgx_track_segment_apply_trs(
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    fzgx_mat43 *transform_inout,
    fzgx_vec3 *scale_inout,
    fzgx_track_segment_trs_curve_cache_exact *curve_cache_inout);
fzgx_status fzgx_track_course_get_root_segment_for_track_node(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    const fzgx_track_segment_record **track_segment_out);
fzgx_status fzgx_track_course_get_track_segment_children(
    const fzgx_track_course_content *course,
    const fzgx_track_segment_record *parent_segment,
    const fzgx_track_segment_record **children_out,
    uint32_t *children_count_out);
fzgx_status fzgx_track_segment_build_source_piece_word(
    const fzgx_track_segment_record *track_segment,
    uint32_t *source_piece_word_out);
fzgx_status fzgx_track_course_accumulate_track_segment_flags_recursive(
    const fzgx_track_course_content *course,
    const fzgx_track_segment_record *root_segment,
    uint32_t *flags_inout);
fzgx_status fzgx_track_course_can_traverse_checkpoint_interval(
    const fzgx_track_course_content *course,
    double minimum_gap_distance,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out);
fzgx_status fzgx_track_course_can_traverse_checkpoint_interval_exact(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out);
fzgx_status fzgx_track_course_can_traverse_checkpoint_variant_count_order(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t require_nonincreasing_order,
    uint32_t *can_traverse_out);
fzgx_status fzgx_track_course_can_traverse_nonincreasing_checkpoint_variant_count(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out);
fzgx_status fzgx_track_course_can_traverse_nondecreasing_checkpoint_variant_count(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out);
fzgx_status fzgx_track_course_find_checkpoint_for_track_distance(
    const fzgx_track_course_content *course,
    double track_distance,
    int32_t seed_track_node_index,
    int32_t *checkpoint_index_out,
    float *checkpoint_fraction_out);
fzgx_status fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out);
fzgx_status fzgx_track_course_get_checkpoint_variant(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    uint32_t variant_index,
    const fzgx_checkpoint_record **checkpoint_out);
fzgx_status fzgx_track_course_compute_checkpoint_t_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    const fzgx_vec3 *point,
    int32_t track_node_index,
    uint32_t variant_index,
    float *t_out);
fzgx_status fzgx_track_course_compute_curve_time_for_checkpoint_fraction(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    float checkpoint_fraction,
    float *curve_time_out);
fzgx_status fzgx_track_course_checkpoint_variant_contains_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t track_node_index,
    uint32_t variant_index,
    int32_t *checkpoint_index_out);
fzgx_status fzgx_track_course_scan_checkpoint_neighbors_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t variant_index,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out,
    fzgx_vec3 *point_on_track_out);
fzgx_status fzgx_track_course_find_shared_checkpoint_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t *checkpoint_index_out,
    float *t_out);
fzgx_status fzgx_track_course_find_nearest_checkpoint_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t *checkpoint_index_out,
    float *t_out,
    uint32_t *variant_index_out);
fzgx_status fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t *variant_index_inout,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out);
fzgx_status fzgx_track_course_resolve_branch_checkpoint_from_seed_with_fallback(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t *variant_index_inout,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out);
fzgx_status fzgx_track_course_resolve_branch_checkpoint_from_seed_strict(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t *variant_index_inout,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out);
fzgx_status fzgx_track_course_build_shared_checkpoint_query_result(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    fzgx_current_checkpoint_query_result *query_out);
fzgx_status fzgx_track_course_build_neighbor_checkpoint_query_result(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t variant_index,
    uint32_t require_matching_branch_corridor,
    fzgx_current_checkpoint_query_result *query_out);
fzgx_status fzgx_track_course_build_seeded_checkpoint_query_result_with_fallback(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    fzgx_current_checkpoint_query_result *query_out);
fzgx_status fzgx_track_course_build_seeded_checkpoint_query_result_strict(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    fzgx_current_checkpoint_query_result *query_out);
fzgx_status fzgx_track_course_build_nearest_checkpoint_query_result(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    fzgx_current_checkpoint_query_result *query_out);
fzgx_status fzgx_track_course_build_active_checkpoint_bank_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction,
    fzgx_active_checkpoint_bank_result *bank_out);
fzgx_status fzgx_track_course_build_current_track_query_result_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction,
    const fzgx_track_frame_record *cached_frames,
    uint32_t cached_frame_count,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_build_current_track_query_result_from_bank_and_frame_buffer(
    const fzgx_current_checkpoint_query_result *checkpoint_result,
    const fzgx_active_checkpoint_bank_result *bank_result,
    const fzgx_track_frame_export_buffer *frame_buffer,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_build_current_track_query_result_from_bank_and_nearest_frame(
    const fzgx_current_checkpoint_query_result *checkpoint_result,
    const fzgx_active_checkpoint_bank_result *bank_result,
    const fzgx_track_frame_record *cached_frames,
    uint32_t cached_frame_count,
    const fzgx_vec3 *selection_point,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_track_course_build_cached_frames_for_checkpoint(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t track_node_index,
    float checkpoint_fraction,
    fzgx_track_frame_record *cached_frames_out,
    uint32_t cached_frame_capacity,
    uint32_t *cached_frame_count_out);
fzgx_status fzgx_populate_track_cached_frames_from_piece_tree_exact(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    float time,
    fzgx_mat43 *transform_inout,
    fzgx_vec3 *scale_inout,
    fzgx_track_segment_trs_curve_cache_exact *curve_cache_inout,
    fzgx_track_frame_record *cached_frames_out,
    uint32_t cached_frame_capacity,
    uint32_t *cached_frame_cursor_inout);
fzgx_status fzgx_track_build_frame_surface_tail(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    const fzgx_mat43 *transform,
    const fzgx_vec3 *scale,
    uint32_t source_piece_word,
    fzgx_track_frame_surface_tail *tail_out);
fzgx_status fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_track_course_build_ordinary_start_grid_slot_query_result(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    uint32_t slot_index,
    fzgx_current_track_query_result *query_out);
fzgx_status fzgx_track_course_build_ordinary_start_grid_slot_transform(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    uint32_t slot_index,
    float *track_width_or_radius_out,
    fzgx_mat43 *transform_out);
fzgx_status fzgx_content_build_track_manifest_from_bytes(
    const uint8_t *data,
    uint32_t size,
    uint32_t authored_track_id,
    fzgx_track_manifest *manifest_out);
fzgx_status fzgx_content_load_track_course_content_from_bytes(
    const uint8_t *data,
    uint32_t size,
    uint32_t authored_track_id,
    fzgx_owned_track_course_content *course_out);
void fzgx_content_release_track_course_content(fzgx_owned_track_course_content *course);
fzgx_status fzgx_content_load_track_course_animation_content_from_bytes(
    const uint8_t *data,
    uint32_t size,
    uint32_t authored_track_id,
    fzgx_owned_track_course_animation_content *course_out);
void fzgx_content_release_track_course_animation_content(
    fzgx_owned_track_course_animation_content *course);
fzgx_status fzgx_content_load_static_collider_course_from_path(
    const char *path,
    fzgx_owned_static_collider_course *course_out);
fzgx_status fzgx_content_load_static_collider_course_from_bytes(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_static_collider_course *course_out);
void fzgx_content_release_static_collider_course(fzgx_owned_static_collider_course *course);
fzgx_status fzgx_static_collider_course_get_surface_tri_cell(
    const fzgx_owned_static_collider_course *course,
    uint32_t surface_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out);
fzgx_status fzgx_static_collider_course_get_surface_quad_cell(
    const fzgx_owned_static_collider_course *course,
    uint32_t surface_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out);
fzgx_status fzgx_content_load_dynamic_scene_collision_course_from_path(
    const char *path,
    fzgx_owned_dynamic_scene_collision_course *course_out);
fzgx_status fzgx_content_load_dynamic_scene_collision_course_from_bytes(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_dynamic_scene_collision_course *course_out);
void fzgx_content_release_dynamic_scene_collision_course(
    fzgx_owned_dynamic_scene_collision_course *course);
fzgx_status fzgx_content_load_track_mesh_course_from_path(
    const char *path,
    fzgx_owned_track_mesh_course *course_out);
fzgx_status fzgx_content_load_track_mesh_course_from_bytes(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_track_mesh_course *course_out);
void fzgx_content_release_track_mesh_course(fzgx_owned_track_mesh_course *course);
fzgx_status fzgx_track_mesh_chunk_get_tri_cell(
    const fzgx_owned_track_mesh_chunk *chunk,
    uint32_t class_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out);
fzgx_status fzgx_track_mesh_chunk_get_quad_cell(
    const fzgx_owned_track_mesh_chunk *chunk,
    uint32_t class_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out);
fzgx_status fzgx_content_load_stage_gma_model_table_from_path(
    const char *path,
    fzgx_owned_stage_gma_model_table *table_out);
void fzgx_content_release_stage_gma_model_table(fzgx_owned_stage_gma_model_table *table);
fzgx_status fzgx_stage_gma_model_table_find_model_by_name(
    const fzgx_owned_stage_gma_model_table *table,
    const char *name,
    const fzgx_owned_stage_gma_model_record **model_out);

#ifdef __cplusplus
}
#endif

#endif
