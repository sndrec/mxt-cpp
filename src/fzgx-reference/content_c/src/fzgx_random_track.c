#include "fzgx/content.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  FZGX_RANDOM_TRACK_SURFACE_COUNT = 14u,
  FZGX_RANDOM_TRACK_PROFILE_CAPACITY = 18u,
  FZGX_RANDOM_TRACK_MAX_CHAIN_SEGMENTS = 4u,
  FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_X = 0u,
  FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_Y = 1u,
  FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_Z = 2u,
  FZGX_RANDOM_TRACK_TRS_CURVE_ROTATION_X = 3u,
  FZGX_RANDOM_TRACK_TRS_CURVE_ROTATION_Y = 4u,
  FZGX_RANDOM_TRACK_TRS_CURVE_ROTATION_Z = 5u,
  FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_X = 6u,
  FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_Y = 7u,
  FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_Z = 8u
};

#define FZGX_RANDOM_TRACK_METERS_PER_CHECKPOINT 100.0f
#define FZGX_RANDOM_TRACK_MIN_EDGE_SAMPLES 6u
#define FZGX_RANDOM_TRACK_MAX_EDGE_SAMPLES 18u

typedef struct fzgx_random_track_rng {
  uint64_t state;
} fzgx_random_track_rng;

typedef struct fzgx_random_track_variant_path {
  uint32_t family;
  uint32_t surface_kind;
  uint32_t overlay_surface_kind;
  uint32_t has_gap_after;
  uint32_t has_sharp_after;
  uint32_t has_mine;
  fzgx_vec3 start_center;
  fzgx_vec3 end_center;
  fzgx_vec3 start_forward;
  fzgx_vec3 end_forward;
  fzgx_vec3 start_up;
  fzgx_vec3 end_up;
  float start_width;
  float end_width;
  float start_half_height;
  float end_half_height;
} fzgx_random_track_variant_path;

typedef struct fzgx_random_track_quad_buffer {
  fzgx_static_collider_quad_record *quads;
  uint32_t count;
  uint32_t capacity;
} fzgx_random_track_quad_buffer;

typedef struct fzgx_random_track_object_buffer {
  fzgx_owned_dynamic_scene_object_record *objects;
  uint32_t count;
  uint32_t capacity;
} fzgx_random_track_object_buffer;

typedef struct fzgx_random_track_dense_sample {
  float time;
  fzgx_vec3 position;
  fzgx_vec3 forward;
  fzgx_vec3 up;
  float width;
  float half_height;
  float openness;
} fzgx_random_track_dense_sample;

typedef struct fzgx_random_track_dense_curve {
  fzgx_random_track_dense_sample *samples;
  uint32_t count;
} fzgx_random_track_dense_curve;

static float fzgx_random_track_minf(float a, float b) {
  return (a < b) ? a : b;
}

static float fzgx_random_track_maxf(float a, float b) {
  return (a > b) ? a : b;
}

static float fzgx_random_track_clampf(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static uint32_t fzgx_random_track_family_round_kind(uint32_t family) {
  switch (family) {
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
      return 1u;
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      return 2u;
    default:
      return 0u;
  }
}

static uint32_t fzgx_random_track_family_is_round_open(uint32_t family) {
  return (uint32_t)(
      (family == FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN));
}

static uint32_t fzgx_random_track_family_is_round_closed(uint32_t family) {
  return (uint32_t)(
      (family == FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED));
}

static uint32_t fzgx_random_track_family_round_matches(
    uint32_t lhs_family,
    uint32_t rhs_family) {
  uint32_t lhs_kind = fzgx_random_track_family_round_kind(lhs_family);
  uint32_t rhs_kind = fzgx_random_track_family_round_kind(rhs_family);

  return (uint32_t)((lhs_kind != 0u) && (lhs_kind == rhs_kind));
}

static uint32_t fzgx_random_track_family_open_for_closed(uint32_t family) {
  switch (family) {
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
      return FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN;
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
      return FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN;
    default:
      return family;
  }
}

static float fzgx_random_track_default_openness(uint32_t family) {
  return (fzgx_random_track_family_is_round_open(family) != 0u) ? 0.5f : 1.0f;
}

static void fzgx_random_track_clamp_dimensions_exact(
    uint32_t family,
    float *width_inout,
    float *half_height_inout) {
  float width;
  float half_height;

  if ((width_inout == 0) || (half_height_inout == 0)) {
    return;
  }

  width = fzgx_random_track_maxf(*width_inout, 15.0f);
  half_height = *half_height_inout;

  if (family == FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD) {
    half_height = fzgx_random_track_maxf(half_height, 0.0f);
  } else if (family == FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD) {
    /* Analytic flat-road contact gets unreliable once the authored Y scale collapses. */
    half_height = fzgx_random_track_maxf(half_height, 1.0f);
  } else if (fzgx_random_track_family_is_round_open(family) != 0u) {
    half_height = fzgx_random_track_maxf(half_height, 0.0f);
  } else if (fzgx_random_track_family_is_round_closed(family) != 0u) {
    width = fzgx_random_track_maxf(width, 15.0f);
    half_height = fzgx_random_track_maxf(half_height, 15.0f);
  } else {
    half_height = fzgx_random_track_maxf(half_height, 0.0f);
  }

  *width_inout = width;
  *half_height_inout = half_height;
}

static void fzgx_random_track_clamp_variant_recipe_exact(
    fzgx_random_track_variant_recipe *variant) {
  if (variant == 0) {
    return;
  }
  fzgx_random_track_clamp_dimensions_exact(
      variant->family, &variant->width, &variant->half_height);
  if (fzgx_random_track_family_is_round_open(variant->family) != 0u) {
    variant->openness = fzgx_random_track_clampf(variant->openness, 0.5f, 1.0f);
  } else {
    variant->openness = 1.0f;
  }
}

static fzgx_vec3 fzgx_random_track_vec3_add(fzgx_vec3 lhs, fzgx_vec3 rhs) {
  return (fzgx_vec3){lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

static fzgx_vec3 fzgx_random_track_vec3_sub(fzgx_vec3 lhs, fzgx_vec3 rhs) {
  return (fzgx_vec3){lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

static fzgx_vec3 fzgx_random_track_vec3_scale(fzgx_vec3 value, float scale) {
  return (fzgx_vec3){value.x * scale, value.y * scale, value.z * scale};
}

static float fzgx_random_track_vec3_dot(fzgx_vec3 lhs, fzgx_vec3 rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

static fzgx_vec3 fzgx_random_track_vec3_cross(fzgx_vec3 lhs, fzgx_vec3 rhs) {
  return (fzgx_vec3){
      lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.z * rhs.x - lhs.x * rhs.z,
      lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

static float fzgx_random_track_vec3_length_squared(fzgx_vec3 value) {
  return fzgx_random_track_vec3_dot(value, value);
}

static float fzgx_random_track_vec3_length(fzgx_vec3 value) {
  return sqrtf(fzgx_random_track_vec3_length_squared(value));
}

static fzgx_vec3 fzgx_random_track_vec3_normalize_or(fzgx_vec3 value, fzgx_vec3 fallback) {
  float length = fzgx_random_track_vec3_length(value);

  if (length <= 1.0e-6f) {
    return fallback;
  }
  return fzgx_random_track_vec3_scale(value, 1.0f / length);
}

static fzgx_mat43 fzgx_random_track_mat43_identity(void) {
  fzgx_mat43 transform;

  memset(&transform, 0, sizeof(transform));
  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  return transform;
}

static float fzgx_random_track_wrap_angle(float radians) {
  while (radians > 3.14159265358979323846f) {
    radians -= 6.28318530717958647692f;
  }
  while (radians < -3.14159265358979323846f) {
    radians += 6.28318530717958647692f;
  }
  return radians;
}

static uint64_t fzgx_random_track_rng_next_u64(fzgx_random_track_rng *rng) {
  uint64_t z;

  rng->state += 0x9e3779b97f4a7c15ull;
  z = rng->state;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

static float fzgx_random_track_rng_next_unit(fzgx_random_track_rng *rng) {
  return (float)((double)(fzgx_random_track_rng_next_u64(rng) >> 40) / (double)(1u << 24));
}

static float fzgx_random_track_rng_next_range(
    fzgx_random_track_rng *rng,
    float min_value,
    float max_value) {
  return min_value + (max_value - min_value) * fzgx_random_track_rng_next_unit(rng);
}

static uint32_t fzgx_random_track_rng_next_index(
    fzgx_random_track_rng *rng,
    uint32_t limit) {
  if (limit == 0u) {
    return 0u;
  }
  return (uint32_t)(fzgx_random_track_rng_next_u64(rng) % (uint64_t)limit);
}

static void fzgx_random_track_release_quad_buffers(
    fzgx_random_track_quad_buffer *buffers,
    uint32_t count) {
  uint32_t index;

  if (buffers == 0) {
    return;
  }
  for (index = 0u; index < count; ++index) {
    free(buffers[index].quads);
    buffers[index].quads = 0;
    buffers[index].count = 0u;
    buffers[index].capacity = 0u;
  }
}

static fzgx_status fzgx_random_track_quad_buffer_append(
    fzgx_random_track_quad_buffer *buffer,
    const fzgx_static_collider_quad_record *quad) {
  uint32_t next_capacity;
  fzgx_static_collider_quad_record *next_quads;

  if ((buffer == 0) || (quad == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (buffer->count == buffer->capacity) {
    next_capacity = (buffer->capacity == 0u) ? 32u : buffer->capacity * 2u;
    next_quads = (fzgx_static_collider_quad_record *)realloc(
        buffer->quads, next_capacity * sizeof(*next_quads));
    if (next_quads == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    buffer->quads = next_quads;
    buffer->capacity = next_capacity;
  }
  buffer->quads[buffer->count++] = *quad;
  return FZGX_STATUS_OK;
}

static void fzgx_random_track_release_object_buffer(
    fzgx_random_track_object_buffer *buffer) {
  uint32_t index;

  if (buffer == 0) {
    return;
  }
  for (index = 0u; index < buffer->count; ++index) {
    free(buffer->objects[index].collider_mesh.tris);
    free(buffer->objects[index].collider_mesh.quads);
  }
  free(buffer->objects);
  buffer->objects = 0;
  buffer->count = 0u;
  buffer->capacity = 0u;
}

static fzgx_status fzgx_random_track_object_buffer_append_mine(
    fzgx_random_track_object_buffer *buffer,
    const fzgx_mat43 *transform) {
  uint32_t next_capacity;
  fzgx_owned_dynamic_scene_object_record *next_objects;
  fzgx_owned_dynamic_scene_object_record *object;
  fzgx_static_collider_quad_record *quad;
  static const char mine_name[] = "RANDOM_MINE";

  if ((buffer == 0) || (transform == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (buffer->count == buffer->capacity) {
    next_capacity = (buffer->capacity == 0u) ? 8u : buffer->capacity * 2u;
    next_objects = (fzgx_owned_dynamic_scene_object_record *)realloc(
        buffer->objects, next_capacity * sizeof(*next_objects));
    if (next_objects == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    buffer->objects = next_objects;
    buffer->capacity = next_capacity;
  }
  object = &buffer->objects[buffer->count];
  memset(object, 0, sizeof(*object));
  object->render_flags_0 = 0x00000001u;
  object->has_transform_matrix = 1u;
  object->transform_matrix = *transform;
  object->has_collider_mesh = 1u;
  object->collider_mesh.collider_type = 0x00004000u;
  object->collider_mesh.bounding_sphere.origin = (fzgx_vec3){0.0f, 0.0f, 0.0f};
  object->collider_mesh.bounding_sphere.radius = 5.0f;
  object->collider_mesh.quad_count = 1u;
  object->collider_mesh.quads = (fzgx_static_collider_quad_record *)calloc(1u, sizeof(*quad));
  if (object->collider_mesh.quads == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  quad = object->collider_mesh.quads;
  quad->vertex0 = (fzgx_vec3){-3.5f, 0.0f, -3.5f};
  quad->vertex1 = (fzgx_vec3){3.5f, 0.0f, -3.5f};
  quad->vertex2 = (fzgx_vec3){3.5f, 0.0f, 3.5f};
  quad->vertex3 = (fzgx_vec3){-3.5f, 0.0f, 3.5f};
  quad->normal = (fzgx_vec3){0.0f, 1.0f, 0.0f};
  quad->plane_distance = 0.0f;
  quad->edge_normal0 = (fzgx_vec3){0.0f, 0.0f, 1.0f};
  quad->edge_normal1 = (fzgx_vec3){-1.0f, 0.0f, 0.0f};
  quad->edge_normal2 = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  quad->edge_normal3 = (fzgx_vec3){1.0f, 0.0f, 0.0f};
  strncpy(object->primary_lod_name, mine_name, sizeof(object->primary_lod_name) - 1u);
  object->primary_lod_name[sizeof(object->primary_lod_name) - 1u] = '\0';
  buffer->count += 1u;
  return FZGX_STATUS_OK;
}

static void fzgx_random_track_init_quad_exact(
    fzgx_static_collider_quad_record *quad,
    fzgx_vec3 vertex0,
    fzgx_vec3 vertex1,
    fzgx_vec3 vertex2,
    fzgx_vec3 vertex3,
    fzgx_vec3 normal) {
  fzgx_vec3 center;
  fzgx_vec3 edges[4];
  fzgx_vec3 deltas[4];

  if (quad == 0) {
    return;
  }
  center.x = 0.25f * (vertex0.x + vertex1.x + vertex2.x + vertex3.x);
  center.y = 0.25f * (vertex0.y + vertex1.y + vertex2.y + vertex3.y);
  center.z = 0.25f * (vertex0.z + vertex1.z + vertex2.z + vertex3.z);
  quad->plane_distance =
      -(normal.x * vertex0.x + normal.y * vertex0.y + normal.z * vertex0.z);
  quad->normal = normal;
  quad->vertex0 = vertex0;
  quad->vertex1 = vertex1;
  quad->vertex2 = vertex2;
  quad->vertex3 = vertex3;
  edges[0] = fzgx_random_track_vec3_cross(
      fzgx_random_track_vec3_sub(vertex1, vertex0), normal);
  edges[1] = fzgx_random_track_vec3_cross(
      fzgx_random_track_vec3_sub(vertex2, vertex1), normal);
  edges[2] = fzgx_random_track_vec3_cross(
      fzgx_random_track_vec3_sub(vertex3, vertex2), normal);
  edges[3] = fzgx_random_track_vec3_cross(
      fzgx_random_track_vec3_sub(vertex0, vertex3), normal);
  deltas[0] = fzgx_random_track_vec3_sub(center, vertex0);
  if (fzgx_random_track_vec3_dot(deltas[0], edges[0]) < 0.0f) {
    edges[0] = fzgx_random_track_vec3_scale(edges[0], -1.0f);
    edges[1] = fzgx_random_track_vec3_scale(edges[1], -1.0f);
    edges[2] = fzgx_random_track_vec3_scale(edges[2], -1.0f);
    edges[3] = fzgx_random_track_vec3_scale(edges[3], -1.0f);
  }
  quad->edge_normal0 = edges[0];
  quad->edge_normal1 = edges[1];
  quad->edge_normal2 = edges[2];
  quad->edge_normal3 = edges[3];
}

static fzgx_mat43 fzgx_random_track_variant_transform(
    const fzgx_random_track_variant_recipe *variant) {
  fzgx_mat43 transform;
  fzgx_vec3 basis_z;
  fzgx_vec3 basis_x;
  fzgx_vec3 basis_y;

  memset(&transform, 0, sizeof(transform));
  basis_z = fzgx_random_track_vec3_scale(variant->forward, -1.0f);
  basis_x = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(variant->up, basis_z),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  basis_y = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(basis_z, basis_x),
      (fzgx_vec3){0.0f, 1.0f, 0.0f});
  transform.basis_x_x = basis_x.x;
  transform.basis_x_y = basis_x.y;
  transform.basis_x_z = basis_x.z;
  transform.basis_y_x = basis_y.x;
  transform.basis_y_y = basis_y.y;
  transform.basis_y_z = basis_y.z;
  transform.basis_z_x = basis_z.x;
  transform.basis_z_y = basis_z.y;
  transform.basis_z_z = basis_z.z;
  transform.origin_x = variant->center.x;
  transform.origin_y = variant->center.y;
  transform.origin_z = variant->center.z;
  return transform;
}

static void fzgx_random_track_extract_zyx_rotation_degrees(
    const fzgx_mat43 *transform,
    fzgx_vec3 *rotation_out) {
  float basis_x_x;
  float basis_x_y;
  float basis_x_z;
  float basis_y_z;
  float basis_z_z;
  float rotation_x;
  float rotation_y;
  float rotation_z;
  float cy;

  if ((transform == 0) || (rotation_out == 0)) {
    return;
  }
  basis_x_x = transform->basis_x_x;
  basis_x_y = transform->basis_x_y;
  basis_x_z = transform->basis_x_z;
  basis_y_z = transform->basis_y_z;
  basis_z_z = transform->basis_z_z;
  rotation_y = asinf(fzgx_random_track_clampf(-basis_x_z, -1.0f, 1.0f));
  cy = cosf(rotation_y);
  if (fabsf(cy) > 1.0e-4f) {
    rotation_x = atan2f(basis_y_z, basis_z_z);
    rotation_z = atan2f(basis_x_y, basis_x_x);
  } else {
    rotation_x = atan2f(-transform->basis_z_y, transform->basis_y_y);
    rotation_z = 0.0f;
  }
  rotation_out->x = rotation_x * (180.0f / 3.14159265358979323846f);
  rotation_out->y = rotation_y * (180.0f / 3.14159265358979323846f);
  rotation_out->z = rotation_z * (180.0f / 3.14159265358979323846f);
}

static void fzgx_random_track_extract_yxz_rotation_degrees(
    const fzgx_mat43 *transform,
    fzgx_vec3 *rotation_out) {
  float basis_x_x;
  float basis_x_y;
  float basis_x_z;
  float basis_y_y;
  float basis_z_x;
  float basis_z_y;
  float basis_z_z;
  float rotation_x;
  float rotation_y;
  float rotation_z;
  float cx;

  if ((transform == 0) || (rotation_out == 0)) {
    return;
  }
  basis_x_x = transform->basis_x_x;
  basis_x_y = transform->basis_x_y;
  basis_x_z = transform->basis_x_z;
  basis_y_y = transform->basis_y_y;
  basis_z_x = transform->basis_z_x;
  basis_z_y = transform->basis_z_y;
  basis_z_z = transform->basis_z_z;

  /* Path wrappers compose as root(Y, X) then child(Z), so sample against YXZ. */
  rotation_x = asinf(fzgx_random_track_clampf(-basis_z_y, -1.0f, 1.0f));
  cx = cosf(rotation_x);
  if (fabsf(cx) > 1.0e-4f) {
    rotation_y = atan2f(basis_z_x, basis_z_z);
    rotation_z = atan2f(basis_x_y, basis_y_y);
  } else {
    /* Near +/-90 deg pitch, yaw and roll collapse into one degree of freedom. */
    rotation_y = atan2f(-basis_x_z, basis_x_x);
    rotation_z = 0.0f;
  }
  rotation_out->x = rotation_x * (180.0f / 3.14159265358979323846f);
  rotation_out->y = rotation_y * (180.0f / 3.14159265358979323846f);
  rotation_out->z = rotation_z * (180.0f / 3.14159265358979323846f);
}

static void fzgx_random_track_variant_profile_point(
    uint32_t family,
    float width,
    float half_height,
    uint32_t index,
    uint32_t count,
    float *x_out,
    float *y_out) {
  float t;
  float angle;
  float radius;

  t = (count <= 1u) ? 0.0f : (float)index / (float)(count - 1u);
  radius = fzgx_random_track_maxf(width, 1.0f);
  switch (family) {
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
      *x_out = -width + 2.0f * width * t;
      *y_out = half_height * 0.6f * sinf(t * 3.14159265358979323846f * 2.0f);
      break;
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
      angle = (-1.9f + 3.8f * t);
      *x_out = sinf(angle) * radius;
      *y_out = half_height * (1.0f - cosf(angle));
      break;
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      angle = (-1.7f + 3.4f * t);
      *x_out = sinf(angle) * radius;
      *y_out = half_height * (1.0f - cosf(angle));
      break;
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
      angle = -3.14159265358979323846f + 6.28318530717958647692f * t;
      *x_out = sinf(angle) * radius;
      *y_out = half_height * (1.0f - cosf(angle));
      break;
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
      angle = -3.14159265358979323846f + 6.28318530717958647692f * t;
      *x_out = sinf(angle) * radius;
      *y_out = half_height * (1.0f - cosf(angle));
      break;
    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
      if (t < 0.25f) {
        angle = -1.57079632679489661923f + (t / 0.25f) * 1.57079632679489661923f;
        *x_out = -width + half_height * cosf(angle);
        *y_out = half_height + half_height * sinf(angle);
      } else if (t < 0.5f) {
        *x_out = -width + (t - 0.25f) * (2.0f * width / 0.25f);
        *y_out = 2.0f * half_height;
      } else if (t < 0.75f) {
        *x_out = width + (t - 0.5f) * (-2.0f * width / 0.25f);
        *y_out = 0.0f;
      } else {
        angle = 0.0f + ((t - 0.75f) / 0.25f) * 1.57079632679489661923f;
        *x_out = width + half_height * cosf(angle);
        *y_out = half_height - half_height * sinf(angle);
      }
      break;
    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    default:
      *x_out = -width + 2.0f * width * t;
      *y_out = 0.0f;
      break;
  }
}

static uint32_t fzgx_random_track_variant_profile_count(uint32_t family) {
  switch (family) {
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
      return 4u;
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      return 8u;
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
      return 12u;
    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
      return 8u;
    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    default:
      return 2u;
  }
}

static fzgx_vec3 fzgx_random_track_variant_local_to_world(
    fzgx_vec3 center,
    fzgx_vec3 right,
    fzgx_vec3 up,
    float local_x,
    float local_y) {
  return fzgx_random_track_vec3_add(
      center,
      fzgx_random_track_vec3_add(
          fzgx_random_track_vec3_scale(right, local_x),
          fzgx_random_track_vec3_scale(up, local_y)));
}

static fzgx_status fzgx_random_track_append_loft_quad(
    fzgx_random_track_quad_buffer *buffer,
    fzgx_vec3 vertex0,
    fzgx_vec3 vertex1,
    fzgx_vec3 vertex2,
    fzgx_vec3 vertex3,
    fzgx_vec3 interior_reference) {
  fzgx_static_collider_quad_record quad;
  fzgx_vec3 edge01;
  fzgx_vec3 edge03;
  fzgx_vec3 normal;
  fzgx_vec3 center;

  edge01 = fzgx_random_track_vec3_sub(vertex1, vertex0);
  edge03 = fzgx_random_track_vec3_sub(vertex3, vertex0);
  normal = fzgx_random_track_vec3_cross(edge01, edge03);
  normal = fzgx_random_track_vec3_normalize_or(normal, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  center = (fzgx_vec3){
      0.25f * (vertex0.x + vertex1.x + vertex2.x + vertex3.x),
      0.25f * (vertex0.y + vertex1.y + vertex2.y + vertex3.y),
      0.25f * (vertex0.z + vertex1.z + vertex2.z + vertex3.z),
  };
  if (fzgx_random_track_vec3_dot(
          normal, fzgx_random_track_vec3_sub(interior_reference, center)) < 0.0f) {
    normal = fzgx_random_track_vec3_scale(normal, -1.0f);
  }
  fzgx_random_track_init_quad_exact(&quad, vertex0, vertex1, vertex2, vertex3, normal);
  return fzgx_random_track_quad_buffer_append(buffer, &quad);
}

static fzgx_status fzgx_random_track_append_floor_overlay_quad(
    fzgx_random_track_quad_buffer *buffer,
    const fzgx_random_track_variant_path *path) {
  fzgx_vec3 start_right;
  fzgx_vec3 end_right;
  fzgx_vec3 start_origin;
  fzgx_vec3 end_origin;
  float strip_width;
  fzgx_vec3 vertex0;
  fzgx_vec3 vertex1;
  fzgx_vec3 vertex2;
  fzgx_vec3 vertex3;
  fzgx_vec3 interior_reference;

  start_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(path->start_up, fzgx_random_track_vec3_scale(path->start_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  end_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(path->end_up, fzgx_random_track_vec3_scale(path->end_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  start_origin = fzgx_random_track_vec3_add(
      path->start_center, fzgx_random_track_vec3_scale(path->start_up, 0.05f));
  end_origin = fzgx_random_track_vec3_add(
      path->end_center, fzgx_random_track_vec3_scale(path->end_up, 0.05f));
  strip_width = 0.45f * fzgx_random_track_minf(path->start_width, path->end_width);
  vertex0 = fzgx_random_track_vec3_add(start_origin, fzgx_random_track_vec3_scale(start_right, -strip_width));
  vertex1 = fzgx_random_track_vec3_add(end_origin, fzgx_random_track_vec3_scale(end_right, -strip_width));
  vertex2 = fzgx_random_track_vec3_add(end_origin, fzgx_random_track_vec3_scale(end_right, strip_width));
  vertex3 = fzgx_random_track_vec3_add(start_origin, fzgx_random_track_vec3_scale(start_right, strip_width));
  interior_reference = fzgx_random_track_vec3_add(start_origin, fzgx_random_track_vec3_scale(path->start_up, 1.0f));
  return fzgx_random_track_append_loft_quad(
      buffer,
      vertex0,
      vertex1,
      vertex2,
      vertex3,
      interior_reference);
}

static fzgx_status fzgx_random_track_append_flat_span(
    fzgx_random_track_quad_buffer *surface_buffers,
    const fzgx_random_track_variant_path *path) {
  fzgx_random_track_quad_buffer *drive_buffer;
  fzgx_random_track_quad_buffer *wall_buffer;
  fzgx_vec3 start_right;
  fzgx_vec3 end_right;
  fzgx_vec3 start_left_floor;
  fzgx_vec3 start_right_floor;
  fzgx_vec3 end_left_floor;
  fzgx_vec3 end_right_floor;
  fzgx_vec3 start_left_top;
  fzgx_vec3 start_right_top;
  fzgx_vec3 end_left_top;
  fzgx_vec3 end_right_top;
  fzgx_status status;

  drive_buffer = &surface_buffers[path->surface_kind];
  wall_buffer = &surface_buffers[FZGX_STATIC_COLLIDER_SURFACE_WALL];
  start_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(path->start_up, fzgx_random_track_vec3_scale(path->start_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  end_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(path->end_up, fzgx_random_track_vec3_scale(path->end_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  start_left_floor = fzgx_random_track_vec3_add(
      path->start_center, fzgx_random_track_vec3_scale(start_right, -path->start_width));
  start_right_floor = fzgx_random_track_vec3_add(
      path->start_center, fzgx_random_track_vec3_scale(start_right, path->start_width));
  end_left_floor = fzgx_random_track_vec3_add(
      path->end_center, fzgx_random_track_vec3_scale(end_right, -path->end_width));
  end_right_floor = fzgx_random_track_vec3_add(
      path->end_center, fzgx_random_track_vec3_scale(end_right, path->end_width));
  status = fzgx_random_track_append_loft_quad(
      drive_buffer,
      start_left_floor,
      end_left_floor,
      end_right_floor,
      start_right_floor,
      fzgx_random_track_vec3_add(path->start_center, fzgx_random_track_vec3_scale(path->start_up, 3.0f)));
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  start_left_top = fzgx_random_track_vec3_add(
      start_left_floor, fzgx_random_track_vec3_scale(path->start_up, path->start_half_height));
  start_right_top = fzgx_random_track_vec3_add(
      start_right_floor, fzgx_random_track_vec3_scale(path->start_up, path->start_half_height));
  end_left_top = fzgx_random_track_vec3_add(
      end_left_floor, fzgx_random_track_vec3_scale(path->end_up, path->end_half_height));
  end_right_top = fzgx_random_track_vec3_add(
      end_right_floor, fzgx_random_track_vec3_scale(path->end_up, path->end_half_height));
  status = fzgx_random_track_append_loft_quad(
      wall_buffer,
      start_left_floor,
      end_left_floor,
      end_left_top,
      start_left_top,
      path->start_center);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_random_track_append_loft_quad(
      wall_buffer,
      start_right_floor,
      end_right_floor,
      end_right_top,
      start_right_top,
      path->start_center);
}

static fzgx_status fzgx_random_track_append_profile_span(
    fzgx_random_track_quad_buffer *surface_buffers,
    const fzgx_random_track_variant_path *path) {
  fzgx_random_track_quad_buffer *drive_buffer;
  fzgx_vec3 start_right;
  fzgx_vec3 end_right;
  fzgx_vec3 start_profile[FZGX_RANDOM_TRACK_PROFILE_CAPACITY];
  fzgx_vec3 end_profile[FZGX_RANDOM_TRACK_PROFILE_CAPACITY];
  fzgx_vec3 interior_reference;
  uint32_t profile_count;
  uint32_t index;
  fzgx_status status;

  drive_buffer = &surface_buffers[path->surface_kind];
  profile_count = fzgx_random_track_variant_profile_count(path->family);
  if (profile_count > FZGX_RANDOM_TRACK_PROFILE_CAPACITY) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  start_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(path->start_up, fzgx_random_track_vec3_scale(path->start_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  end_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(path->end_up, fzgx_random_track_vec3_scale(path->end_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  for (index = 0u; index < profile_count; ++index) {
    float start_x;
    float start_y;
    float end_x;
    float end_y;

    fzgx_random_track_variant_profile_point(
        path->family,
        path->start_width,
        path->start_half_height,
        index,
        profile_count,
        &start_x,
        &start_y);
    fzgx_random_track_variant_profile_point(
        path->family,
        path->end_width,
        path->end_half_height,
        index,
        profile_count,
        &end_x,
        &end_y);
    start_profile[index] = fzgx_random_track_variant_local_to_world(
        path->start_center, start_right, path->start_up, start_x, start_y);
    end_profile[index] = fzgx_random_track_variant_local_to_world(
        path->end_center, end_right, path->end_up, end_x, end_y);
  }
  interior_reference = fzgx_random_track_vec3_add(
      path->start_center, fzgx_random_track_vec3_scale(path->start_up, path->start_half_height));
  for (index = 0u; (index + 1u) < profile_count; ++index) {
    status = fzgx_random_track_append_loft_quad(
        drive_buffer,
        start_profile[index],
        end_profile[index],
        end_profile[index + 1u],
        start_profile[index + 1u],
        interior_reference);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((path->family == FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED) ||
      (path->family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED) ||
      (path->family == FZGX_RANDOM_TRACK_FAMILY_CAPSULE)) {
    status = fzgx_random_track_append_loft_quad(
        drive_buffer,
        start_profile[profile_count - 1u],
        end_profile[profile_count - 1u],
        end_profile[0],
        start_profile[0],
        interior_reference);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  return FZGX_STATUS_OK;
}

static uint32_t fzgx_random_track_surface_family_is_profile(uint32_t family) {
  return (uint32_t)(
      (family == FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_CAPSULE));
}

static fzgx_status fzgx_random_track_build_surface_geometry(
    const fzgx_random_track_recipe *recipe,
    fzgx_owned_static_collider_course *static_course_out,
    fzgx_owned_dynamic_scene_collision_course *dynamic_course_out) {
  fzgx_random_track_quad_buffer surface_buffers[FZGX_RANDOM_TRACK_SURFACE_COUNT];
  fzgx_random_track_object_buffer object_buffer;
  fzgx_vec3 bbox_min;
  fzgx_vec3 bbox_max;
  uint32_t total_quads = 0u;
  uint32_t surface_index;
  uint32_t node_index;
  uint32_t current_quad_cursor = 0u;
  fzgx_status status = FZGX_STATUS_OK;

  if ((recipe == 0) || (static_course_out == 0) || (dynamic_course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  memset(surface_buffers, 0, sizeof(surface_buffers));
  memset(&object_buffer, 0, sizeof(object_buffer));
  bbox_min = (fzgx_vec3){1000000.0f, 1000000.0f, 1000000.0f};
  bbox_max = (fzgx_vec3){-1000000.0f, -1000000.0f, -1000000.0f};

  for (node_index = 0u; node_index < recipe->node_count; ++node_index) {
    const fzgx_random_track_node_recipe *node = &recipe->nodes[node_index];
    const fzgx_random_track_node_recipe *next_node =
        &recipe->nodes[(node_index + 1u) % recipe->node_count];

    if ((node->variant_count <= 1u) && (next_node->variant_count <= 1u)) {
      fzgx_random_track_variant_path path;

      memset(&path, 0, sizeof(path));
      path.family = node->variants[0].family;
      path.surface_kind = node->variants[0].base_surface_kind;
      path.overlay_surface_kind = node->variants[0].overlay_surface_kind;
      path.has_gap_after = (node->gap_after_mask & 1u) != 0u;
      path.has_sharp_after = (node->sharp_after_mask & 1u) != 0u;
      path.has_mine = (node->mine_mask & 1u) != 0u;
      path.start_center = node->variants[0].center;
      path.end_center = next_node->variants[0].center;
      path.start_forward = node->variants[0].forward;
      path.end_forward = next_node->variants[0].forward;
      path.start_up = node->variants[0].up;
      path.end_up = next_node->variants[0].up;
      path.start_width = node->variants[0].width;
      path.end_width = next_node->variants[0].width;
      path.start_half_height = node->variants[0].half_height;
      path.end_half_height = next_node->variants[0].half_height;
      if (!path.has_gap_after) {
        if (fzgx_random_track_surface_family_is_profile(path.family) != 0u) {
          status = fzgx_random_track_append_profile_span(surface_buffers, &path);
        } else {
          status = fzgx_random_track_append_flat_span(surface_buffers, &path);
        }
        if (status != FZGX_STATUS_OK) {
          goto cleanup;
        }
        if ((path.overlay_surface_kind != 0u) &&
            (path.overlay_surface_kind != path.surface_kind) &&
            (path.overlay_surface_kind < FZGX_RANDOM_TRACK_SURFACE_COUNT)) {
          status = fzgx_random_track_append_floor_overlay_quad(
              &surface_buffers[path.overlay_surface_kind], &path);
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }
        }
      }
      if (path.has_mine != 0u) {
        fzgx_vec3 right = fzgx_random_track_vec3_normalize_or(
            fzgx_random_track_vec3_cross(
                node->variants[0].up,
                fzgx_random_track_vec3_scale(node->variants[0].forward, -1.0f)),
            (fzgx_vec3){1.0f, 0.0f, 0.0f});
        fzgx_mat43 mine_transform;

        memset(&mine_transform, 0, sizeof(mine_transform));
        mine_transform.basis_x_x = right.x;
        mine_transform.basis_x_y = right.y;
        mine_transform.basis_x_z = right.z;
        mine_transform.basis_y_x = node->variants[0].up.x;
        mine_transform.basis_y_y = node->variants[0].up.y;
        mine_transform.basis_y_z = node->variants[0].up.z;
        mine_transform.basis_z_x = node->variants[0].forward.x;
        mine_transform.basis_z_y = node->variants[0].forward.y;
        mine_transform.basis_z_z = node->variants[0].forward.z;
        mine_transform.origin_x =
            0.5f * (path.start_center.x + path.end_center.x) + 0.2f * node->variants[0].up.x;
        mine_transform.origin_y =
            0.5f * (path.start_center.y + path.end_center.y) + 0.2f * node->variants[0].up.y;
        mine_transform.origin_z =
            0.5f * (path.start_center.z + path.end_center.z) + 0.2f * node->variants[0].up.z;
        status = fzgx_random_track_object_buffer_append_mine(&object_buffer, &mine_transform);
        if (status != FZGX_STATUS_OK) {
          goto cleanup;
        }
      }
      continue;
    }

    {
      uint32_t branch_count = (node->variant_count > 1u) ? node->variant_count : next_node->variant_count;
      uint32_t slot;

      for (slot = 1u; slot < branch_count; ++slot) {
        const fzgx_random_track_variant_recipe *start_variant =
            (node->variant_count > 1u) ? &node->variants[slot] : &node->variants[0];
        const fzgx_random_track_variant_recipe *end_variant =
            (next_node->variant_count > 1u) ? &next_node->variants[slot] : &next_node->variants[0];
        fzgx_random_track_variant_path path;

        memset(&path, 0, sizeof(path));
        path.family = start_variant->family;
        path.surface_kind = start_variant->base_surface_kind;
        path.overlay_surface_kind = start_variant->overlay_surface_kind;
        path.has_gap_after = ((node->gap_after_mask >> slot) & 1u) != 0u;
        path.has_sharp_after = ((node->sharp_after_mask >> slot) & 1u) != 0u;
        path.has_mine = ((node->mine_mask >> slot) & 1u) != 0u;
        path.start_center = start_variant->center;
        path.end_center = end_variant->center;
        path.start_forward = start_variant->forward;
        path.end_forward = end_variant->forward;
        path.start_up = start_variant->up;
        path.end_up = end_variant->up;
        path.start_width = start_variant->width;
        path.end_width = end_variant->width;
        path.start_half_height = start_variant->half_height;
        path.end_half_height = end_variant->half_height;
        if (!path.has_gap_after) {
          if (fzgx_random_track_surface_family_is_profile(path.family) != 0u) {
            status = fzgx_random_track_append_profile_span(surface_buffers, &path);
          } else {
            status = fzgx_random_track_append_flat_span(surface_buffers, &path);
          }
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }
          if ((path.overlay_surface_kind != 0u) &&
              (path.overlay_surface_kind != path.surface_kind) &&
              (path.overlay_surface_kind < FZGX_RANDOM_TRACK_SURFACE_COUNT)) {
            status = fzgx_random_track_append_floor_overlay_quad(
                &surface_buffers[path.overlay_surface_kind], &path);
            if (status != FZGX_STATUS_OK) {
              goto cleanup;
            }
          }
        }
      }
    }
  }

  for (surface_index = 0u; surface_index < FZGX_RANDOM_TRACK_SURFACE_COUNT; ++surface_index) {
    uint32_t quad_index;

    total_quads += surface_buffers[surface_index].count;
    for (quad_index = 0u; quad_index < surface_buffers[surface_index].count; ++quad_index) {
      const fzgx_static_collider_quad_record *quad = &surface_buffers[surface_index].quads[quad_index];
      const fzgx_vec3 points[4] = {
          quad->vertex0,
          quad->vertex1,
          quad->vertex2,
          quad->vertex3,
      };
      uint32_t point_index;

      for (point_index = 0u; point_index < 4u; ++point_index) {
        bbox_min.x = fzgx_random_track_minf(bbox_min.x, points[point_index].x);
        bbox_min.y = fzgx_random_track_minf(bbox_min.y, points[point_index].y);
        bbox_min.z = fzgx_random_track_minf(bbox_min.z, points[point_index].z);
        bbox_max.x = fzgx_random_track_maxf(bbox_max.x, points[point_index].x);
        bbox_max.y = fzgx_random_track_maxf(bbox_max.y, points[point_index].y);
        bbox_max.z = fzgx_random_track_maxf(bbox_max.z, points[point_index].z);
      }
    }
  }

  memset(static_course_out, 0, sizeof(*static_course_out));
  memset(dynamic_course_out, 0, sizeof(*dynamic_course_out));
  static_course_out->surface_count = FZGX_RANDOM_TRACK_SURFACE_COUNT;
  static_course_out->quad_count = total_quads;
  static_course_out->quad_index_count = total_quads;
  static_course_out->surface_grids = (fzgx_static_collider_surface_grid *)calloc(
      static_course_out->surface_count, sizeof(*static_course_out->surface_grids));
  if (static_course_out->surface_grids == 0) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  if (total_quads != 0u) {
    static_course_out->quads = (fzgx_static_collider_quad_record *)calloc(
        total_quads, sizeof(*static_course_out->quads));
    static_course_out->quad_indices = (uint16_t *)calloc(
        total_quads, sizeof(*static_course_out->quad_indices));
    if ((static_course_out->quads == 0) || (static_course_out->quad_indices == 0)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }

  for (surface_index = 0u; surface_index < FZGX_RANDOM_TRACK_SURFACE_COUNT; ++surface_index) {
    uint32_t quad_index;
    fzgx_static_collider_surface_grid *surface_grid = &static_course_out->surface_grids[surface_index];

    surface_grid->quad_cells[0].offset = current_quad_cursor;
    surface_grid->quad_cells[0].count = surface_buffers[surface_index].count;
    for (quad_index = 0u; quad_index < surface_buffers[surface_index].count; ++quad_index) {
      static_course_out->quads[current_quad_cursor] = surface_buffers[surface_index].quads[quad_index];
      static_course_out->quad_indices[current_quad_cursor] = (uint16_t)current_quad_cursor;
      current_quad_cursor += 1u;
    }
  }

  static_course_out->mesh_grid.left = bbox_min.x - 1.0f;
  static_course_out->mesh_grid.top = bbox_min.z - 1.0f;
  static_course_out->mesh_grid.subdivision_width =
      fzgx_random_track_maxf(8.0f, (bbox_max.x - bbox_min.x) + 2.0f);
  static_course_out->mesh_grid.subdivision_length =
      fzgx_random_track_maxf(8.0f, (bbox_max.z - bbox_min.z) + 2.0f);
  static_course_out->mesh_grid.num_subdivisions_x = 1;
  static_course_out->mesh_grid.num_subdivisions_z = 1;
  static_course_out->bounding_sphere.origin.x = 0.5f * (bbox_min.x + bbox_max.x);
  static_course_out->bounding_sphere.origin.y = 0.5f * (bbox_min.y + bbox_max.y);
  static_course_out->bounding_sphere.origin.z = 0.5f * (bbox_min.z + bbox_max.z);
  static_course_out->bounding_sphere.radius = 0.0f;
  {
    fzgx_vec3 extents = {
        0.5f * (bbox_max.x - bbox_min.x),
        0.5f * (bbox_max.y - bbox_min.y),
        0.5f * (bbox_max.z - bbox_min.z),
    };
    static_course_out->bounding_sphere.radius = sqrtf(
        extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
  }

  dynamic_course_out->object_count = object_buffer.count;
  dynamic_course_out->objects = object_buffer.objects;
  object_buffer.objects = 0;
  object_buffer.count = 0u;
  object_buffer.capacity = 0u;

cleanup:
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_static_collider_course(static_course_out);
    fzgx_content_release_dynamic_scene_collision_course(dynamic_course_out);
  }
  fzgx_random_track_release_quad_buffers(surface_buffers, FZGX_RANDOM_TRACK_SURFACE_COUNT);
  fzgx_random_track_release_object_buffer(&object_buffer);
  return status;
}

static uint32_t fzgx_random_track_choose_surface_kind(
    fzgx_random_track_rng *rng,
    uint32_t allow_overlay) {
  float roll = fzgx_random_track_rng_next_unit(rng);

  if (roll < 0.60f) {
    return FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
  }
  if (roll < 0.72f) {
    return FZGX_STATIC_COLLIDER_SURFACE_RECOVER;
  }
  if (roll < 0.82f) {
    return FZGX_STATIC_COLLIDER_SURFACE_ICE;
  }
  if (roll < 0.90f) {
    return FZGX_STATIC_COLLIDER_SURFACE_DIRT;
  }
  if (roll < 0.96f) {
    return FZGX_STATIC_COLLIDER_SURFACE_DAMAGE;
  }
  if (allow_overlay != 0u) {
    return (fzgx_random_track_rng_next_unit(rng) < 0.5f)
               ? FZGX_STATIC_COLLIDER_SURFACE_DASH
               : FZGX_STATIC_COLLIDER_SURFACE_JUMP;
  }
  return FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
}

static void fzgx_random_track_rotate_frame_into_start_space(
    uint32_t node_count,
    fzgx_vec3 *positions,
    fzgx_vec3 *forwards,
    fzgx_vec3 *ups) {
  fzgx_vec3 origin;
  fzgx_vec3 start_forward;
  fzgx_vec3 start_up;
  fzgx_vec3 start_right;
  uint32_t index;

  if ((node_count == 0u) || (positions == 0) || (forwards == 0) || (ups == 0)) {
    return;
  }
  origin = positions[0];
  start_forward = fzgx_random_track_vec3_normalize_or(
      forwards[0], (fzgx_vec3){0.0f, 0.0f, -1.0f});
  start_up = fzgx_random_track_vec3_normalize_or(
      ups[0], (fzgx_vec3){0.0f, 1.0f, 0.0f});
  start_right = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(start_up, fzgx_random_track_vec3_scale(start_forward, -1.0f)),
      (fzgx_vec3){1.0f, 0.0f, 0.0f});
  start_up = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(
          fzgx_random_track_vec3_scale(start_forward, -1.0f), start_right),
      (fzgx_vec3){0.0f, 1.0f, 0.0f});
  for (index = 0u; index < node_count; ++index) {
    fzgx_vec3 delta = fzgx_random_track_vec3_sub(positions[index], origin);
    fzgx_vec3 forward = forwards[index];
    fzgx_vec3 up = ups[index];

    positions[index].x = fzgx_random_track_vec3_dot(delta, start_right);
    positions[index].y = fzgx_random_track_vec3_dot(delta, start_up);
    positions[index].z = fzgx_random_track_vec3_dot(delta, fzgx_random_track_vec3_scale(start_forward, -1.0f));

    forwards[index].x = fzgx_random_track_vec3_dot(forward, start_right);
    forwards[index].y = fzgx_random_track_vec3_dot(forward, start_up);
    forwards[index].z = fzgx_random_track_vec3_dot(
        forward, fzgx_random_track_vec3_scale(start_forward, -1.0f));

    ups[index].x = fzgx_random_track_vec3_dot(up, start_right);
    ups[index].y = fzgx_random_track_vec3_dot(up, start_up);
    ups[index].z = fzgx_random_track_vec3_dot(up, fzgx_random_track_vec3_scale(start_forward, -1.0f));
  }
}

fzgx_status fzgx_random_track_generate_recipe(
    uint64_t seed,
    const fzgx_random_track_config *config,
    fzgx_random_track_recipe *recipe_out) {
  uint32_t node_count;
  uint32_t branch_window_count;
  fzgx_random_track_rng rng;
  float *turn_weights = 0;
  float *lengths = 0;
  fzgx_vec3 *positions = 0;
  fzgx_vec3 *forwards = 0;
  fzgx_vec3 *ups = 0;
  uint32_t *families = 0;
  float *widths = 0;
  float *half_heights = 0;
  float *openness = 0;
  uint32_t *base_surfaces = 0;
  uint32_t *overlay_surfaces = 0;
  uint32_t seam_node_count = 6u;
  uint32_t branch_start = 0u;
  uint32_t branch_length = 0u;
  uint32_t branch_variants = 0u;
  uint32_t node_index;
  fzgx_status status = FZGX_STATUS_OK;
  float total_turn_weight = 0.0f;
  fzgx_vec3 closure = {0};

  if (recipe_out == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(recipe_out, 0, sizeof(*recipe_out));
  node_count = 48u;
  branch_window_count = 1u;
  if (config != 0) {
    if ((config->api_version != 0u) && (config->api_version != FZGX_RANDOM_TRACK_API_VERSION)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
    if (config->node_count >= 24u) {
      node_count = config->node_count;
    }
    branch_window_count = config->branch_window_count;
  }
  branch_window_count = (branch_window_count != 0u) ? 1u : 0u;
  rng.state = seed;
  if (rng.state == 0u) {
    rng.state = 0x123456789abcdef0ull;
  }

  turn_weights = (float *)calloc(node_count, sizeof(*turn_weights));
  lengths = (float *)calloc(node_count, sizeof(*lengths));
  positions = (fzgx_vec3 *)calloc(node_count, sizeof(*positions));
  forwards = (fzgx_vec3 *)calloc(node_count, sizeof(*forwards));
  ups = (fzgx_vec3 *)calloc(node_count, sizeof(*ups));
  families = (uint32_t *)calloc(node_count, sizeof(*families));
  widths = (float *)calloc(node_count, sizeof(*widths));
  half_heights = (float *)calloc(node_count, sizeof(*half_heights));
  openness = (float *)calloc(node_count, sizeof(*openness));
  base_surfaces = (uint32_t *)calloc(node_count, sizeof(*base_surfaces));
  overlay_surfaces = (uint32_t *)calloc(node_count, sizeof(*overlay_surfaces));
  recipe_out->nodes = (fzgx_random_track_node_recipe *)calloc(node_count, sizeof(*recipe_out->nodes));
  if ((turn_weights == 0) || (lengths == 0) || (positions == 0) || (forwards == 0) ||
      (ups == 0) || (families == 0) || (widths == 0) || (half_heights == 0) ||
      (openness == 0) ||
      (base_surfaces == 0) || (overlay_surfaces == 0) || (recipe_out->nodes == 0)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  for (node_index = 0u; node_index < node_count; ++node_index) {
    turn_weights[node_index] = 0.7f + fzgx_random_track_rng_next_range(&rng, 0.0f, 1.1f);
    lengths[node_index] = fzgx_random_track_rng_next_range(&rng, 120.0f, 220.0f);
  }
  {
    uint32_t hairpin_count = 1u + (fzgx_random_track_rng_next_index(&rng, 2u));
    uint32_t hairpin_index;

    for (hairpin_index = 0u; hairpin_index < hairpin_count; ++hairpin_index) {
      uint32_t center = 6u + fzgx_random_track_rng_next_index(&rng, node_count - 12u);
      int32_t offset;

      for (offset = -1; offset <= 1; ++offset) {
        uint32_t index = (uint32_t)((int32_t)center + offset);
        float weight_scale = (offset == 0) ? 2.8f : 1.8f;

        turn_weights[index] *= weight_scale;
        lengths[index] *= (offset == 0) ? 0.55f : 0.75f;
      }
    }
  }
  for (node_index = 0u; node_index < node_count; ++node_index) {
    total_turn_weight += turn_weights[node_index];
  }
  for (node_index = 0u; node_index < node_count; ++node_index) {
    turn_weights[node_index] =
        (turn_weights[node_index] / total_turn_weight) * 6.28318530717958647692f;
  }

  {
    float heading = 0.0f;
    float height_phase_a = fzgx_random_track_rng_next_range(&rng, 0.0f, 6.28318530717958647692f);
    float height_phase_b = fzgx_random_track_rng_next_range(&rng, 0.0f, 6.28318530717958647692f);
    fzgx_vec3 cursor = {0};

    positions[0] = cursor;
    for (node_index = 0u; node_index < node_count; ++node_index) {
      float angle = 6.28318530717958647692f * (float)node_index / (float)node_count;
      float height =
          85.0f * sinf(angle * 2.0f + height_phase_a) +
          35.0f * sinf(angle * 5.0f + height_phase_b);

      positions[node_index].y = height;
      if ((node_index + 1u) < node_count) {
        heading += turn_weights[node_index];
        cursor.x += cosf(heading) * lengths[node_index];
        cursor.z += sinf(heading) * lengths[node_index];
        positions[node_index + 1u] = cursor;
      } else {
        closure.x = cursor.x + cosf(heading + turn_weights[node_index]) * lengths[node_index];
        closure.z = cursor.z + sinf(heading + turn_weights[node_index]) * lengths[node_index];
      }
    }
  }
  for (node_index = 0u; node_index < node_count; ++node_index) {
    float fraction = (float)node_index / (float)node_count;

    positions[node_index].x -= closure.x * fraction;
    positions[node_index].z -= closure.z * fraction;
  }

  for (node_index = 0u; node_index < node_count; ++node_index) {
    const fzgx_vec3 prev = positions[(node_index + node_count - 1u) % node_count];
    const fzgx_vec3 next = positions[(node_index + 1u) % node_count];
    fzgx_vec3 tangent = fzgx_random_track_vec3_sub(next, prev);
    fzgx_vec3 world_up = {0.0f, 1.0f, 0.0f};
    fzgx_vec3 right;
    float bank;

    tangent = fzgx_random_track_vec3_normalize_or(tangent, (fzgx_vec3){0.0f, 0.0f, -1.0f});
    right = fzgx_random_track_vec3_normalize_or(
        fzgx_random_track_vec3_cross(world_up, tangent),
        (fzgx_vec3){1.0f, 0.0f, 0.0f});
    world_up = fzgx_random_track_vec3_normalize_or(
        fzgx_random_track_vec3_cross(tangent, right),
        (fzgx_vec3){0.0f, 1.0f, 0.0f});
    bank = fzgx_random_track_rng_next_range(&rng, -0.45f, 0.45f);
    ups[node_index] = fzgx_random_track_vec3_normalize_or(
        fzgx_random_track_vec3_add(
            fzgx_random_track_vec3_scale(world_up, cosf(bank)),
            fzgx_random_track_vec3_scale(right, sinf(bank))),
        (fzgx_vec3){0.0f, 1.0f, 0.0f});
    forwards[node_index] = tangent;
  }
  fzgx_random_track_rotate_frame_into_start_space(node_count, positions, forwards, ups);

  {
    const float seam_height = positions[0].y;
    const fzgx_vec3 world_up = {0.0f, 1.0f, 0.0f};

    for (node_index = 0u; node_index < seam_node_count; ++node_index) {
      uint32_t tail_index = node_count - seam_node_count + node_index;

      positions[node_index].y = seam_height;
      positions[tail_index].y = seam_height;
      ups[node_index] = world_up;
      ups[tail_index] = world_up;
    }

    for (node_index = 0u; node_index < node_count; ++node_index) {
      const fzgx_vec3 prev = positions[(node_index + node_count - 1u) % node_count];
      const fzgx_vec3 next = positions[(node_index + 1u) % node_count];
      fzgx_vec3 tangent = fzgx_random_track_vec3_sub(next, prev);

      if ((node_index < seam_node_count) ||
          (node_index >= (node_count - seam_node_count))) {
        tangent.y = 0.0f;
        forwards[node_index] = fzgx_random_track_vec3_normalize_or(
            tangent, (fzgx_vec3){0.0f, 0.0f, -1.0f});
        ups[node_index] = world_up;
      } else {
        fzgx_vec3 projected_up;

        tangent = fzgx_random_track_vec3_normalize_or(
            tangent, (fzgx_vec3){0.0f, 0.0f, -1.0f});
        projected_up = fzgx_random_track_vec3_sub(
            ups[node_index],
            fzgx_random_track_vec3_scale(
                tangent, fzgx_random_track_vec3_dot(ups[node_index], tangent)));
        projected_up = fzgx_random_track_vec3_normalize_or(projected_up, world_up);
        if (fzgx_random_track_vec3_dot(projected_up, world_up) < 0.0f) {
          projected_up = fzgx_random_track_vec3_scale(projected_up, -1.0f);
        }
        forwards[node_index] = tangent;
        ups[node_index] = projected_up;
      }
    }
  }

  for (node_index = 0u; node_index < node_count; ++node_index) {
    if ((node_index < seam_node_count) ||
        (node_index >= (node_count - seam_node_count))) {
      families[node_index] = FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD;
      widths[node_index] = 24.0f;
      half_heights[node_index] = 4.0f;
      openness[node_index] = 1.0f;
      base_surfaces[node_index] = FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
      overlay_surfaces[node_index] = 0u;
      continue;
    }
  }

  if ((branch_window_count != 0u) && (node_count >= 32u)) {
    uint32_t branch_start_min = seam_node_count + 6u;
    uint32_t branch_end_limit = node_count - seam_node_count;

    branch_length = 6u + fzgx_random_track_rng_next_index(&rng, 4u);
    if ((branch_start_min + branch_length) < branch_end_limit) {
      uint32_t branch_start_max = branch_end_limit - branch_length;

      branch_start = branch_start_min + fzgx_random_track_rng_next_index(
                                           &rng, (branch_start_max - branch_start_min) + 1u);
      branch_variants = 2u + fzgx_random_track_rng_next_index(&rng, 2u);
    } else {
      branch_window_count = 0u;
      branch_length = 0u;
    }
  }

  {
    uint32_t section_start = seam_node_count;
    uint32_t section_limit = node_count - seam_node_count;
    uint32_t previous_family = FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD;
    float previous_width = 24.0f;
    float previous_half_height = 4.0f;
    float previous_openness = 1.0f;
    uint32_t pending_family = UINT32_MAX;
    uint32_t pending_surface_kind = FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
    uint32_t pending_overlay_kind = 0u;
    float pending_width = previous_width;
    float pending_half_height = previous_half_height;

    while (section_start < section_limit) {
      uint32_t section_end = section_start + 3u + fzgx_random_track_rng_next_index(&rng, 5u);
      uint32_t family = FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD;
      uint32_t desired_family = FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD;
      uint32_t surface_kind = FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
      uint32_t overlay_kind = 0u;
      float section_start_width = previous_width;
      float section_start_half_height = previous_half_height;
      float section_start_openness = previous_openness;
      float target_width = previous_width;
      float target_half_height = previous_half_height;
      float target_openness = 1.0f;
      uint32_t section_index;

      if (section_end > section_limit) {
        section_end = section_limit;
      }
      if ((branch_window_count != 0u) && (section_start < branch_start) &&
          (branch_start < section_end)) {
        section_end = branch_start;
      }
      if ((branch_window_count != 0u) && (branch_start <= section_start) &&
          (section_start < (branch_start + branch_length)) &&
          ((branch_start + branch_length) < section_end)) {
        section_end = branch_start + branch_length;
      }
      if (section_end <= section_start) {
        section_end = section_start + 1u;
      }

      if (pending_family != UINT32_MAX) {
        desired_family = pending_family;
        target_width = pending_width;
        target_half_height = pending_half_height;
        surface_kind = pending_surface_kind;
        overlay_kind = pending_overlay_kind;
        pending_family = UINT32_MAX;
      } else {
        if ((previous_family != FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD) &&
            (previous_family != FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN) &&
            (previous_family != FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN) &&
            (fzgx_random_track_rng_next_unit(&rng) < 0.25f)) {
          desired_family = previous_family;
        } else {
          float family_roll = fzgx_random_track_rng_next_unit(&rng);

          if (family_roll < 0.32f) {
            desired_family = FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD;
          } else if (family_roll < 0.48f) {
            desired_family = FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD;
          } else if (family_roll < 0.65f) {
            desired_family = FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED;
          } else if (family_roll < 0.80f) {
            desired_family = FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED;
          } else {
            desired_family = FZGX_RANDOM_TRACK_FAMILY_CAPSULE;
          }
        }

        switch (desired_family) {
          case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
            target_width = fzgx_random_track_rng_next_range(&rng, 18.0f, 24.0f);
            target_half_height = fzgx_random_track_rng_next_range(&rng, 4.0f, 9.0f);
            break;
          case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
          case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
            target_width = fzgx_random_track_rng_next_range(&rng, 15.0f, 22.0f);
            target_half_height = fzgx_random_track_rng_next_range(&rng, 15.0f, 20.0f);
            break;
          case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
            target_width = fzgx_random_track_rng_next_range(&rng, 15.0f, 20.0f);
            target_half_height = fzgx_random_track_rng_next_range(&rng, 6.0f, 10.0f);
            break;
          case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
          default:
            target_width = fzgx_random_track_rng_next_range(&rng, 20.0f, 28.0f);
            target_half_height = fzgx_random_track_rng_next_range(&rng, 3.0f, 6.0f);
            break;
        }
        fzgx_random_track_clamp_dimensions_exact(
            desired_family, &target_width, &target_half_height);

        surface_kind = fzgx_random_track_choose_surface_kind(&rng, 0u);
        if ((surface_kind == FZGX_STATIC_COLLIDER_SURFACE_DASH) ||
            (surface_kind == FZGX_STATIC_COLLIDER_SURFACE_JUMP)) {
          overlay_kind = surface_kind;
          surface_kind = FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
        } else if (fzgx_random_track_rng_next_unit(&rng) > 0.90f) {
          overlay_kind = (fzgx_random_track_rng_next_unit(&rng) < 0.5f)
                             ? FZGX_STATIC_COLLIDER_SURFACE_DASH
                             : FZGX_STATIC_COLLIDER_SURFACE_JUMP;
        }
      }

      family = desired_family;
      target_openness = fzgx_random_track_default_openness(desired_family);

      if ((fzgx_random_track_family_is_round_closed(desired_family) != 0u) &&
          (fzgx_random_track_family_round_matches(previous_family, desired_family) == 0u)) {
        pending_family = desired_family;
        pending_surface_kind = surface_kind;
        pending_overlay_kind = overlay_kind;
        pending_width = target_width;
        pending_half_height = target_half_height;
        family = fzgx_random_track_family_open_for_closed(desired_family);
        section_start_half_height = 0.0f;
        section_start_openness = 0.5f;
        surface_kind = FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
        overlay_kind = 0u;
        target_openness = 1.0f;
      } else if ((fzgx_random_track_family_is_round_closed(previous_family) != 0u) &&
                 (fzgx_random_track_family_round_matches(previous_family, desired_family) == 0u)) {
        pending_family = desired_family;
        pending_surface_kind = surface_kind;
        pending_overlay_kind = overlay_kind;
        pending_width = target_width;
        pending_half_height = target_half_height;
        family = fzgx_random_track_family_open_for_closed(previous_family);
        target_half_height = 0.0f;
        section_start_openness = 1.0f;
        target_openness = 0.5f;
        surface_kind = FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE;
        overlay_kind = 0u;
      } else if ((fzgx_random_track_family_is_round_open(previous_family) != 0u) &&
                 (fzgx_random_track_family_round_matches(previous_family, desired_family) == 0u)) {
        section_start_half_height = 0.0f;
        section_start_openness = 0.5f;
      }

      fzgx_random_track_clamp_dimensions_exact(family, &target_width, &target_half_height);
      if (fzgx_random_track_family_is_round_open(family) != 0u) {
        uint32_t open_section_end =
            section_start + 2u + fzgx_random_track_rng_next_index(&rng, 2u);

        if (open_section_end < section_end) {
          section_end = open_section_end;
        }
        if (section_end > section_limit) {
          section_end = section_limit;
        }
      }

      for (section_index = section_start; section_index < section_end; ++section_index) {
        float t = (section_end - section_start <= 1u)
                      ? 1.0f
                      : (float)(section_index - section_start) /
                            (float)((section_end - section_start) - 1u);
        float smooth_t = t * t * (3.0f - 2.0f * t);
        float width =
            section_start_width + (target_width - section_start_width) * smooth_t;
        float half_height =
            section_start_half_height +
            (target_half_height - section_start_half_height) * smooth_t;

        families[section_index] = family;
        fzgx_random_track_clamp_dimensions_exact(family, &width, &half_height);
        widths[section_index] = width;
        half_heights[section_index] = half_height;
        openness[section_index] =
            (fzgx_random_track_family_is_round_open(family) != 0u)
                ? fzgx_random_track_clampf(
                      section_start_openness +
                          (target_openness - section_start_openness) * smooth_t,
                      0.5f,
                      1.0f)
                : 1.0f;
        base_surfaces[section_index] = surface_kind;
        overlay_surfaces[section_index] = overlay_kind;
      }

      previous_family = family;
      previous_width = target_width;
      previous_half_height = target_half_height;
      previous_openness = target_openness;
      section_start = section_end;
    }
  }

  recipe_out->api_version = FZGX_RANDOM_TRACK_API_VERSION;
  recipe_out->seed_hash_low32 = (uint32_t)seed;
  recipe_out->authored_track_id = 0x80000000u | (((uint32_t)seed) & 0x7fffffffu);
  recipe_out->node_count = node_count;
  recipe_out->track_total_distance = 0.0f;
  recipe_out->track_min_height = positions[0].y;

  for (node_index = 0u; node_index < node_count; ++node_index) {
    fzgx_random_track_node_recipe *node = &recipe_out->nodes[node_index];
    uint32_t variant_count = 1u;
    uint32_t variant_index;

    if ((branch_window_count != 0u) &&
        (branch_start <= node_index) &&
        (node_index < (branch_start + branch_length))) {
      variant_count = branch_variants + 1u;
    }
    node->variant_count = variant_count;
    node->gap_after_mask = 0u;
    node->sharp_after_mask = 0u;
    node->mine_mask = 0u;

    recipe_out->track_total_distance += fzgx_random_track_vec3_length(
        fzgx_random_track_vec3_sub(
            positions[(node_index + 1u) % node_count],
            positions[node_index]));
    if (positions[node_index].y < recipe_out->track_min_height) {
      recipe_out->track_min_height = positions[node_index].y;
    }

    for (variant_index = 0u; variant_index < variant_count; ++variant_index) {
      fzgx_random_track_variant_recipe *variant = &node->variants[variant_index];
      float branch_blend = 0.0f;
      fzgx_vec3 right = fzgx_random_track_vec3_normalize_or(
          fzgx_random_track_vec3_cross(
              ups[node_index],
              fzgx_random_track_vec3_scale(forwards[node_index], -1.0f)),
          (fzgx_vec3){1.0f, 0.0f, 0.0f});
      float branch_offset = 0.0f;

      memset(variant, 0, sizeof(*variant));
      variant->center = positions[node_index];
      variant->forward = forwards[node_index];
      variant->up = ups[node_index];
      variant->width = widths[node_index];
      variant->half_height = half_heights[node_index];
      variant->openness = openness[node_index];
      variant->family = (uint8_t)families[node_index];
      variant->base_surface_kind = (uint8_t)base_surfaces[node_index];
      variant->overlay_surface_kind = (uint8_t)overlay_surfaces[node_index];
      if (variant_count > 1u) {
        float t = (float)(node_index - branch_start + 1u) / (float)(branch_length + 1u);

        branch_blend = sinf(t * 3.14159265358979323846f);
      }
      if (variant_index != 0u) {
        float slot_center = ((float)(branch_variants - 1u)) * 0.5f;
        float signed_slot = (float)(variant_index - 1u) - slot_center;

        branch_offset = signed_slot * (widths[node_index] * 2.4f) * branch_blend;
        variant->center = fzgx_random_track_vec3_add(
            variant->center, fzgx_random_track_vec3_scale(right, branch_offset));
        if (variant_index == 1u) {
          variant->family = (uint8_t)FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN;
          variant->openness = 0.75f;
        } else if (variant_index == 2u) {
          variant->family = (uint8_t)FZGX_RANDOM_TRACK_FAMILY_CAPSULE;
          variant->openness = 1.0f;
        } else {
          variant->family = (uint8_t)FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED;
          variant->openness = 1.0f;
        }
      }
      fzgx_random_track_clamp_variant_recipe_exact(variant);
    }

    if ((node_index >= seam_node_count) &&
        ((node_index + 1u) < (node_count - seam_node_count)) &&
        (fzgx_random_track_rng_next_unit(&rng) > 0.94f)) {
      uint32_t slot = (node->variant_count > 1u)
                          ? (1u + fzgx_random_track_rng_next_index(&rng, node->variant_count - 1u))
                          : 0u;
      node->gap_after_mask |= (uint8_t)(1u << slot);
      node->sharp_after_mask |= (uint8_t)(1u << slot);
    } else if ((node_index >= seam_node_count) &&
               ((node_index + 1u) < (node_count - seam_node_count)) &&
               (fzgx_random_track_rng_next_unit(&rng) > 0.90f)) {
      uint32_t slot = (node->variant_count > 1u)
                          ? (1u + fzgx_random_track_rng_next_index(&rng, node->variant_count - 1u))
                          : 0u;
      node->mine_mask |= (uint8_t)(1u << slot);
    }
  }

cleanup:
  free(turn_weights);
  free(lengths);
  free(positions);
  free(forwards);
  free(ups);
  free(families);
  free(widths);
  free(half_heights);
  free(openness);
  free(base_surfaces);
  free(overlay_surfaces);
  if (status != FZGX_STATUS_OK) {
    fzgx_random_track_release_recipe(recipe_out);
  }
  return status;
}

void fzgx_random_track_release_recipe(fzgx_random_track_recipe *recipe) {
  if (recipe == 0) {
    return;
  }
  free(recipe->nodes);
  memset(recipe, 0, sizeof(*recipe));
}

static uint8_t fzgx_random_track_pipe_flags_for_family(uint32_t family) {
  if ((family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED) ||
      (family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN)) {
    return 0x01u;
  }
  return 0u;
}

static uint32_t fzgx_random_track_variant_segment_count(
    const fzgx_random_track_variant_recipe *variant) {
  if (variant == 0) {
    return 0u;
  }
  switch (variant->family) {
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      return 2u;
    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
    default:
      return 1u;
  }
}

static fzgx_status fzgx_random_track_write_variant_segments(
    const fzgx_random_track_variant_recipe *variant,
    int32_t branch_index,
    uint32_t include_world_transform,
    uint32_t *address_cursor_inout,
    fzgx_track_segment_record *segments,
    uint32_t *segment_cursor_inout,
    uint32_t *root_address_out) {
  fzgx_track_segment_record *segment;
  fzgx_mat43 transform;
  fzgx_vec3 rotation;
  fzgx_vec3 root_position;
  fzgx_vec3 root_rotation;
  uint32_t root_address;

  if ((variant == 0) || (address_cursor_inout == 0) || (segments == 0) ||
      (segment_cursor_inout == 0) || (root_address_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  transform = fzgx_random_track_variant_transform(variant);
  fzgx_random_track_extract_zyx_rotation_degrees(&transform, &rotation);
  root_position = (include_world_transform != 0u) ? variant->center : (fzgx_vec3){0};
  root_rotation = (include_world_transform != 0u) ? rotation : (fzgx_vec3){0};
  root_address = *address_cursor_inout;

  switch (variant->family) {
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = root_address;
      segment->segment_type = 0x08u;
      segment->children_count = 1u;
      segment->children_address = root_address + 0x10u;
      segment->branch_index = branch_index;
      segment->fallback_position = root_position;
      segment->fallback_rotation = root_rotation;
      segment->fallback_scale = (fzgx_vec3){variant->width, 1.0f, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;

      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = *address_cursor_inout;
      segment->segment_type = 0x00u;
      segment->embedded_property_type = 0x20u;
      segment->perimeter_flags = 0x0cu;
      segment->rail_height_left = 3.0f;
      segment->rail_height_right = 3.0f;
      segment->branch_index = branch_index;
      segment->fallback_scale = (fzgx_vec3){1.0f, variant->half_height, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;
      *root_address_out = root_address;
      return FZGX_STATUS_OK;

    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = root_address;
      segment->segment_type = 0x08u;
      segment->children_count = 1u;
      segment->children_address = root_address + 0x10u;
      segment->branch_index = branch_index;
      segment->fallback_position = root_position;
      segment->fallback_rotation = root_rotation;
      segment->fallback_scale = (fzgx_vec3){2.0f, 2.0f, 2.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;

      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = *address_cursor_inout;
      segment->segment_type = 0x00u;
      segment->embedded_property_type = 0x40u;
      segment->branch_index = branch_index;
      segment->fallback_position.x =
          fzgx_random_track_maxf(0.5f, 0.5f * variant->width - variant->half_height);
      segment->fallback_scale =
          (fzgx_vec3){variant->half_height, variant->half_height, variant->half_height};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;
      *root_address_out = root_address;
      return FZGX_STATUS_OK;

    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = root_address;
      segment->segment_type = 0x00u;
      segment->embedded_property_type = 0x80u;
      segment->perimeter_flags = 0x0cu;
      segment->pipe_cylinder_flags =
          fzgx_random_track_pipe_flags_for_family(variant->family);
      segment->rail_height_left = 3.0f;
      segment->rail_height_right = 3.0f;
      segment->children_count = 1u;
      segment->children_address = root_address + 0x10u;
      segment->branch_index = branch_index;
      segment->fallback_position = root_position;
      segment->fallback_rotation = root_rotation;
      segment->fallback_scale = (fzgx_vec3){variant->width, variant->half_height, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;

      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = *address_cursor_inout;
      segment->segment_type = 0x00u;
      segment->pipe_cylinder_flags = 0x02u;
      segment->branch_index = branch_index;
      segment->fallback_position = (fzgx_vec3){0.0f, -0.5f, 0.0f};
      segment->fallback_scale = (fzgx_vec3){1.0f, 0.5f, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;
      *root_address_out = root_address;
      return FZGX_STATUS_OK;

    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = root_address;
      segment->segment_type = 0x01u;
      segment->pipe_cylinder_flags =
          fzgx_random_track_pipe_flags_for_family(variant->family);
      segment->branch_index = branch_index;
      segment->fallback_position = root_position;
      segment->fallback_rotation = root_rotation;
      segment->fallback_scale = (fzgx_vec3){variant->width, variant->half_height, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;
      *root_address_out = root_address;
      return FZGX_STATUS_OK;

    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    default:
      segment = &segments[*segment_cursor_inout];
      memset(segment, 0, sizeof(*segment));
      segment->address = root_address;
      segment->segment_type = 0x02u;
      segment->perimeter_flags = 0x0cu;
      segment->rail_height_left = 3.0f;
      segment->rail_height_right = 3.0f;
      segment->branch_index = branch_index;
      segment->fallback_position = root_position;
      segment->fallback_rotation = root_rotation;
      segment->fallback_scale = (fzgx_vec3){variant->width, 1.0f, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;
      *root_address_out = root_address;
      return FZGX_STATUS_OK;
  }
}

static fzgx_status fzgx_random_track_write_node_segments(
    const fzgx_random_track_node_recipe *node,
    uint32_t node_index,
    uint32_t *address_cursor_inout,
    fzgx_track_segment_record *segments,
    uint32_t *segment_cursor_inout,
    uint32_t *root_address_out) {
  fzgx_track_segment_record *segment;
  uint32_t root_address;
  uint32_t slot;

  if ((node == 0) || (address_cursor_inout == 0) || (segments == 0) ||
      (segment_cursor_inout == 0) || (root_address_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  if (node->variant_count <= 1u) {
    return fzgx_random_track_write_variant_segments(
        &node->variants[0],
        0,
        1u,
        address_cursor_inout,
        segments,
        segment_cursor_inout,
        root_address_out);
  }

  root_address = *address_cursor_inout;
  segment = &segments[*segment_cursor_inout];
  memset(segment, 0, sizeof(*segment));
  segment->address = root_address;
  segment->segment_type = 0x08u;
  segment->children_count = node->variant_count - 1u;
  segment->children_address = root_address + 0x10u;
  segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
  *segment_cursor_inout += 1u;
  *address_cursor_inout += 0x10u;

  {
    uint32_t branch_wrapper_segment_indices[FZGX_RANDOM_TRACK_MAX_VARIANTS] = {0};
    uint32_t slot_count = node->variant_count - 1u;

    for (slot = 1u; slot <= slot_count; ++slot) {
      const fzgx_random_track_variant_recipe *variant = &node->variants[slot];
      fzgx_track_segment_record *branch_segment = &segments[*segment_cursor_inout];
      fzgx_mat43 transform = fzgx_random_track_variant_transform(variant);
      fzgx_vec3 rotation;

      fzgx_random_track_extract_zyx_rotation_degrees(&transform, &rotation);
      branch_wrapper_segment_indices[slot] = *segment_cursor_inout;
      memset(branch_segment, 0, sizeof(*branch_segment));
      branch_segment->address = *address_cursor_inout;
      branch_segment->segment_type = 0x04u;
      branch_segment->children_count = 1u;
      branch_segment->branch_index = (int32_t)slot;
      branch_segment->fallback_position = variant->center;
      branch_segment->fallback_rotation = rotation;
      branch_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      *segment_cursor_inout += 1u;
      *address_cursor_inout += 0x10u;
    }

    for (slot = 1u; slot <= slot_count; ++slot) {
      const fzgx_random_track_variant_recipe *variant = &node->variants[slot];
      fzgx_track_segment_record *branch_segment =
          &segments[branch_wrapper_segment_indices[slot]];
      uint32_t branch_child_address = 0u;
      fzgx_status status = fzgx_random_track_write_variant_segments(
          variant,
          (int32_t)slot,
          0u,
          address_cursor_inout,
          segments,
          segment_cursor_inout,
          &branch_child_address);

      if (status != FZGX_STATUS_OK) {
        return status;
      }
      branch_segment->children_address = branch_child_address;
    }
  }

  *root_address_out = root_address;
  (void)node_index;
  return FZGX_STATUS_OK;
}

static void fzgx_random_track_init_keyable_linear(
    fzgx_keyable_attribute *keyable,
    float time,
    float value) {
  if (keyable == 0) {
    return;
  }
  keyable->interpolation_mode = 1u;
  keyable->time = time;
  keyable->value = value;
  keyable->tangent_in = 0.0f;
  keyable->tangent_out = 0.0f;
}

static void fzgx_random_track_init_keyable_cubic(
    fzgx_keyable_attribute *keyable,
    float time,
    float value,
    float tangent_in,
    float tangent_out) {
  if (keyable == 0) {
    return;
  }
  keyable->interpolation_mode = 2u;
  keyable->time = time;
  keyable->value = value;
  keyable->tangent_in = tangent_in;
  keyable->tangent_out = tangent_out;
}

typedef struct fzgx_random_track_curve_spec {
  uint32_t keyable_count;
  fzgx_keyable_attribute *keyables;
} fzgx_random_track_curve_spec;

typedef struct fzgx_random_track_segment_animation_spec {
  uint32_t segment_address;
  uint32_t animation_address;
  fzgx_random_track_curve_spec curves[9];
} fzgx_random_track_segment_animation_spec;

typedef struct fzgx_random_track_animation_buffer {
  fzgx_random_track_segment_animation_spec *segments;
  uint32_t count;
  uint32_t capacity;
} fzgx_random_track_animation_buffer;

typedef struct fzgx_random_track_run {
  uint32_t start_node;
  uint32_t edge_count;
  uint32_t variant_count;
  uint32_t path_key_count;
  uint32_t checkpoint_count;
  uint32_t root_segment_address;
  uint32_t rz_segment_address;
  uint32_t slot_transform_segment_count[FZGX_RANDOM_TRACK_MAX_VARIANTS];
  uint32_t
      slot_transform_segment_addresses[FZGX_RANDOM_TRACK_MAX_VARIANTS]
                                       [FZGX_RANDOM_TRACK_MAX_CHAIN_SEGMENTS];
  uint32_t slot_source_segment_address[FZGX_RANDOM_TRACK_MAX_VARIANTS];
  uint8_t slot_family[FZGX_RANDOM_TRACK_MAX_VARIANTS];
  uint8_t slot_base_surface_kind[FZGX_RANDOM_TRACK_MAX_VARIANTS];
  uint8_t slot_overlay_surface_kind[FZGX_RANDOM_TRACK_MAX_VARIANTS];
  uint32_t *edge_subdivisions;
  float *key_times;
  float *path_sample_times;
  float *checkpoint_times;
  float *checkpoint_distances;
  float checkpoint_total_length;
} fzgx_random_track_run;

typedef struct fzgx_random_track_source_piece_sample {
  const fzgx_track_segment_record *track_segment;
  const fzgx_track_segment_animation_record *animation_segment;
  uint32_t source_piece_word;
  fzgx_mat43 transform;
  fzgx_vec3 scale;
} fzgx_random_track_source_piece_sample;

static void fzgx_random_track_release_curve_spec(
    fzgx_random_track_curve_spec *curve_spec) {
  if (curve_spec == 0) {
    return;
  }
  free(curve_spec->keyables);
  curve_spec->keyables = 0;
  curve_spec->keyable_count = 0u;
}

static void fzgx_random_track_release_animation_buffer(
    fzgx_random_track_animation_buffer *buffer) {
  uint32_t segment_index;

  if (buffer == 0) {
    return;
  }
  for (segment_index = 0u; segment_index < buffer->count; ++segment_index) {
    uint32_t curve_index;

    for (curve_index = 0u; curve_index < 9u; ++curve_index) {
      fzgx_random_track_release_curve_spec(&buffer->segments[segment_index].curves[curve_index]);
    }
  }
  free(buffer->segments);
  buffer->segments = 0;
  buffer->count = 0u;
  buffer->capacity = 0u;
}

static fzgx_status fzgx_random_track_animation_buffer_append(
    fzgx_random_track_animation_buffer *buffer,
    uint32_t segment_address,
    fzgx_random_track_segment_animation_spec **spec_out) {
  uint32_t next_capacity;
  fzgx_random_track_segment_animation_spec *next_segments;
  fzgx_random_track_segment_animation_spec *spec;

  if ((buffer == 0) || (spec_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (buffer->count == buffer->capacity) {
    next_capacity = (buffer->capacity == 0u) ? 16u : buffer->capacity * 2u;
    next_segments = (fzgx_random_track_segment_animation_spec *)realloc(
        buffer->segments, next_capacity * sizeof(*next_segments));
    if (next_segments == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    buffer->segments = next_segments;
    buffer->capacity = next_capacity;
  }
  spec = &buffer->segments[buffer->count];
  memset(spec, 0, sizeof(*spec));
  spec->segment_address = segment_address;
  spec->animation_address = 0x70000000u + buffer->count * 0x100u;
  *spec_out = spec;
  buffer->count += 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_random_track_animation_spec_set_curve_linear(
    fzgx_random_track_segment_animation_spec *spec,
    uint32_t curve_slot,
    uint32_t keyable_count,
    const float *times,
    const float *values) {
  uint32_t keyable_index;

  if ((spec == 0) || (curve_slot >= 9u) || (keyable_count == 0u) || (times == 0) ||
      (values == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  spec->curves[curve_slot].keyables = (fzgx_keyable_attribute *)calloc(
      keyable_count, sizeof(*spec->curves[curve_slot].keyables));
  if (spec->curves[curve_slot].keyables == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  spec->curves[curve_slot].keyable_count = keyable_count;
  for (keyable_index = 0u; keyable_index < keyable_count; ++keyable_index) {
    fzgx_random_track_init_keyable_linear(
        &spec->curves[curve_slot].keyables[keyable_index],
        times[keyable_index],
        values[keyable_index]);
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_random_track_animation_spec_set_curve_cubic(
    fzgx_random_track_segment_animation_spec *spec,
    uint32_t curve_slot,
    uint32_t keyable_count,
    const float *times,
    const float *values,
    const float *tangents) {
  uint32_t keyable_index;

  if ((spec == 0) || (curve_slot >= 9u) || (keyable_count == 0u) || (times == 0) ||
      (values == 0) || (tangents == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  spec->curves[curve_slot].keyables = (fzgx_keyable_attribute *)calloc(
      keyable_count, sizeof(*spec->curves[curve_slot].keyables));
  if (spec->curves[curve_slot].keyables == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  spec->curves[curve_slot].keyable_count = keyable_count;
  for (keyable_index = 0u; keyable_index < keyable_count; ++keyable_index) {
    fzgx_random_track_init_keyable_cubic(
        &spec->curves[curve_slot].keyables[keyable_index],
        times[keyable_index],
        values[keyable_index],
        tangents[keyable_index],
        tangents[keyable_index]);
  }
  return FZGX_STATUS_OK;
}

static void fzgx_random_track_release_runs(
    fzgx_random_track_run *runs,
    uint32_t run_count) {
  uint32_t run_index;

  if (runs == 0) {
    return;
  }
  for (run_index = 0u; run_index < run_count; ++run_index) {
    free(runs[run_index].edge_subdivisions);
    free(runs[run_index].key_times);
    free(runs[run_index].path_sample_times);
    free(runs[run_index].checkpoint_times);
    free(runs[run_index].checkpoint_distances);
    runs[run_index].edge_subdivisions = 0;
    runs[run_index].key_times = 0;
    runs[run_index].path_sample_times = 0;
    runs[run_index].checkpoint_times = 0;
    runs[run_index].checkpoint_distances = 0;
  }
  free(runs);
}

static float fzgx_random_track_wrap_degrees(float degrees) {
  while (degrees > 180.0f) {
    degrees -= 360.0f;
  }
  while (degrees < -180.0f) {
    degrees += 360.0f;
  }
  return degrees;
}

static float fzgx_random_track_unwrap_degrees(float degrees, float reference) {
  return reference + fzgx_random_track_wrap_degrees(degrees - reference);
}

static uint32_t fzgx_random_track_variant_signature_matches(
    const fzgx_random_track_variant_recipe *lhs,
    const fzgx_random_track_variant_recipe *rhs) {
  if ((lhs == 0) || (rhs == 0)) {
    return 0u;
  }
  return (uint32_t)(
      (lhs->family == rhs->family) &&
      (lhs->base_surface_kind == rhs->base_surface_kind) &&
      (lhs->overlay_surface_kind == rhs->overlay_surface_kind));
}

static uint32_t fzgx_random_track_node_signature_matches(
    const fzgx_random_track_node_recipe *lhs,
    const fzgx_random_track_node_recipe *rhs) {
  uint32_t slot;

  if ((lhs == 0) || (rhs == 0)) {
    return 0u;
  }
  if (lhs->variant_count != rhs->variant_count) {
    return 0u;
  }
  for (slot = 0u; slot < lhs->variant_count; ++slot) {
    if (fzgx_random_track_variant_signature_matches(&lhs->variants[slot], &rhs->variants[slot]) ==
        0u) {
      return 0u;
    }
  }
  return 1u;
}

static const fzgx_random_track_variant_recipe *fzgx_random_track_node_get_slot_or_shared(
    const fzgx_random_track_node_recipe *node,
    uint32_t slot) {
  if (node == 0) {
    return 0;
  }
  if (slot < node->variant_count) {
    return &node->variants[slot];
  }
  return &node->variants[0];
}

static const fzgx_random_track_variant_recipe *fzgx_random_track_run_get_control_variant_signed(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    uint32_t slot,
    int32_t point_index) {
  int32_t wrapped_index;

  if ((recipe == 0) || (run == 0) || (recipe->nodes == 0) || (recipe->node_count == 0u)) {
    return 0;
  }
  wrapped_index =
      (int32_t)run->start_node + point_index + (int32_t)recipe->node_count * 8;
  wrapped_index %= (int32_t)recipe->node_count;
  return fzgx_random_track_node_get_slot_or_shared(&recipe->nodes[(uint32_t)wrapped_index], slot);
}

static void fzgx_random_track_dense_curve_release(
    fzgx_random_track_dense_curve *curve) {
  if (curve == 0) {
    return;
  }
  free(curve->samples);
  curve->samples = 0;
  curve->count = 0u;
}

static fzgx_vec3 fzgx_random_track_vec3_lerp(
    fzgx_vec3 lhs,
    fzgx_vec3 rhs,
    float t) {
  return (fzgx_vec3){
      lhs.x + (rhs.x - lhs.x) * t,
      lhs.y + (rhs.y - lhs.y) * t,
      lhs.z + (rhs.z - lhs.z) * t,
  };
}

static float fzgx_random_track_smoothstep(float t) {
  t = fzgx_random_track_clampf(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static fzgx_mat43 fzgx_random_track_pose_transform(
    fzgx_vec3 position,
    fzgx_vec3 forward,
    fzgx_vec3 up) {
  fzgx_mat43 transform;
  fzgx_vec3 basis_z;
  fzgx_vec3 basis_x;
  fzgx_vec3 basis_y;

  memset(&transform, 0, sizeof(transform));
  basis_z = fzgx_random_track_vec3_scale(
      fzgx_random_track_vec3_normalize_or(forward, (fzgx_vec3){0.0f, 0.0f, -1.0f}),
      -1.0f);
  basis_x = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(up, basis_z), (fzgx_vec3){1.0f, 0.0f, 0.0f});
  basis_y = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_cross(basis_z, basis_x), (fzgx_vec3){0.0f, 1.0f, 0.0f});
  transform.basis_x_x = basis_x.x;
  transform.basis_x_y = basis_x.y;
  transform.basis_x_z = basis_x.z;
  transform.basis_y_x = basis_y.x;
  transform.basis_y_y = basis_y.y;
  transform.basis_y_z = basis_y.z;
  transform.basis_z_x = basis_z.x;
  transform.basis_z_y = basis_z.y;
  transform.basis_z_z = basis_z.z;
  transform.origin_x = position.x;
  transform.origin_y = position.y;
  transform.origin_z = position.z;
  return transform;
}

static fzgx_vec3 fzgx_random_track_bezier_position(
    fzgx_vec3 p0,
    fzgx_vec3 p1,
    fzgx_vec3 p2,
    fzgx_vec3 p3,
    float t) {
  float omt = 1.0f - t;
  float omt2 = omt * omt;
  float omt3 = omt2 * omt;
  float t2 = t * t;
  float t3 = t2 * t;

  return fzgx_random_track_vec3_add(
      fzgx_random_track_vec3_add(
          fzgx_random_track_vec3_scale(p0, omt3),
          fzgx_random_track_vec3_scale(p1, 3.0f * omt2 * t)),
      fzgx_random_track_vec3_add(
          fzgx_random_track_vec3_scale(p2, 3.0f * omt * t2),
          fzgx_random_track_vec3_scale(p3, t3)));
}

static fzgx_vec3 fzgx_random_track_bezier_derivative(
    fzgx_vec3 p0,
    fzgx_vec3 p1,
    fzgx_vec3 p2,
    fzgx_vec3 p3,
    float t) {
  float omt = 1.0f - t;
  return fzgx_random_track_vec3_add(
      fzgx_random_track_vec3_add(
          fzgx_random_track_vec3_scale(
              fzgx_random_track_vec3_sub(p1, p0), 3.0f * omt * omt),
          fzgx_random_track_vec3_scale(
              fzgx_random_track_vec3_sub(p2, p1), 6.0f * omt * t)),
      fzgx_random_track_vec3_scale(
          fzgx_random_track_vec3_sub(p3, p2), 3.0f * t * t));
}

static uint32_t fzgx_random_track_edge_sample_count(
    const fzgx_random_track_variant_recipe *start_variant,
    const fzgx_random_track_variant_recipe *end_variant) {
  float edge_length;
  float turn_angle;
  float forward_dot;
  uint32_t sample_count;

  if ((start_variant == 0) || (end_variant == 0)) {
    return FZGX_RANDOM_TRACK_MIN_EDGE_SAMPLES;
  }
  edge_length = fzgx_random_track_vec3_length(
      fzgx_random_track_vec3_sub(end_variant->center, start_variant->center));
  forward_dot = fzgx_random_track_clampf(
      fzgx_random_track_vec3_dot(start_variant->forward, end_variant->forward), -1.0f, 1.0f);
  turn_angle = acosf(forward_dot);
  sample_count =
      (uint32_t)ceilf(edge_length / 32.0f) + (uint32_t)ceilf(turn_angle / (3.14159265358979323846f / 12.0f));
  if (sample_count < FZGX_RANDOM_TRACK_MIN_EDGE_SAMPLES) {
    sample_count = FZGX_RANDOM_TRACK_MIN_EDGE_SAMPLES;
  }
  if (sample_count > FZGX_RANDOM_TRACK_MAX_EDGE_SAMPLES) {
    sample_count = FZGX_RANDOM_TRACK_MAX_EDGE_SAMPLES;
  }
  return sample_count;
}

static void fzgx_random_track_sample_variant_edge(
    const fzgx_random_track_variant_recipe *start_variant,
    const fzgx_random_track_variant_recipe *end_variant,
    float t,
    fzgx_vec3 *previous_up_inout,
    fzgx_random_track_dense_sample *sample_out) {
  fzgx_vec3 edge_delta;
  float edge_length;
  float handle_length;
  fzgx_vec3 start_forward;
  fzgx_vec3 end_forward;
  fzgx_vec3 p0;
  fzgx_vec3 p1;
  fzgx_vec3 p2;
  fzgx_vec3 p3;
  fzgx_vec3 derivative;
  fzgx_vec3 forward;
  fzgx_vec3 up_hint;
  fzgx_vec3 up;
  float smooth_t;

  if ((start_variant == 0) || (end_variant == 0) || (sample_out == 0)) {
    return;
  }

  edge_delta = fzgx_random_track_vec3_sub(end_variant->center, start_variant->center);
  edge_length = fzgx_random_track_vec3_length(edge_delta);
  handle_length = edge_length * 0.35f;
  start_forward = fzgx_random_track_vec3_normalize_or(
      start_variant->forward,
      fzgx_random_track_vec3_normalize_or(edge_delta, (fzgx_vec3){0.0f, 0.0f, -1.0f}));
  end_forward = fzgx_random_track_vec3_normalize_or(
      end_variant->forward,
      fzgx_random_track_vec3_normalize_or(edge_delta, (fzgx_vec3){0.0f, 0.0f, -1.0f}));
  p0 = start_variant->center;
  p1 = fzgx_random_track_vec3_add(p0, fzgx_random_track_vec3_scale(start_forward, handle_length));
  p3 = end_variant->center;
  p2 = fzgx_random_track_vec3_sub(p3, fzgx_random_track_vec3_scale(end_forward, handle_length));
  derivative = fzgx_random_track_bezier_derivative(p0, p1, p2, p3, t);
  forward = fzgx_random_track_vec3_normalize_or(
      derivative, fzgx_random_track_vec3_normalize_or(edge_delta, (fzgx_vec3){0.0f, 0.0f, -1.0f}));
  smooth_t = fzgx_random_track_smoothstep(t);
  up_hint = fzgx_random_track_vec3_normalize_or(
      fzgx_random_track_vec3_lerp(start_variant->up, end_variant->up, smooth_t),
      (fzgx_vec3){0.0f, 1.0f, 0.0f});
  up = fzgx_random_track_vec3_sub(
      up_hint, fzgx_random_track_vec3_scale(forward, fzgx_random_track_vec3_dot(up_hint, forward)));
  if ((previous_up_inout != 0) && (fzgx_random_track_vec3_length_squared(*previous_up_inout) > 1.0e-6f)) {
    if (fzgx_random_track_vec3_dot(up, *previous_up_inout) < 0.0f) {
      up = fzgx_random_track_vec3_scale(up, -1.0f);
    }
  }
  up = fzgx_random_track_vec3_normalize_or(up, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  if (previous_up_inout != 0) {
    *previous_up_inout = up;
  }

  sample_out->position = fzgx_random_track_bezier_position(p0, p1, p2, p3, t);
  sample_out->forward = forward;
  sample_out->up = up;
  sample_out->width =
      start_variant->width + (end_variant->width - start_variant->width) * smooth_t;
  sample_out->half_height =
      start_variant->half_height +
      (end_variant->half_height - start_variant->half_height) * smooth_t;
  sample_out->openness =
      start_variant->openness + (end_variant->openness - start_variant->openness) * smooth_t;
  fzgx_random_track_clamp_dimensions_exact(
      start_variant->family, &sample_out->width, &sample_out->half_height);
  if (fzgx_random_track_family_is_round_open(start_variant->family) != 0u) {
    sample_out->openness = fzgx_random_track_clampf(sample_out->openness, 0.5f, 1.0f);
  } else {
    sample_out->openness = 1.0f;
  }
}

static void fzgx_random_track_init_dense_sample_from_variant(
    const fzgx_random_track_variant_recipe *variant,
    fzgx_random_track_dense_sample *sample_out) {
  fzgx_vec3 forward;
  fzgx_vec3 up_hint;
  fzgx_vec3 up;

  if ((variant == 0) || (sample_out == 0)) {
    return;
  }
  memset(sample_out, 0, sizeof(*sample_out));
  forward = fzgx_random_track_vec3_normalize_or(
      variant->forward, (fzgx_vec3){0.0f, 0.0f, -1.0f});
  up_hint = fzgx_random_track_vec3_normalize_or(
      variant->up, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  up = fzgx_random_track_vec3_sub(
      up_hint, fzgx_random_track_vec3_scale(forward, fzgx_random_track_vec3_dot(up_hint, forward)));
  up = fzgx_random_track_vec3_normalize_or(up, (fzgx_vec3){0.0f, 1.0f, 0.0f});
  sample_out->position = variant->center;
  sample_out->forward = forward;
  sample_out->up = up;
  sample_out->width = variant->width;
  sample_out->half_height = variant->half_height;
  sample_out->openness = variant->openness;
  fzgx_random_track_clamp_dimensions_exact(
      variant->family, &sample_out->width, &sample_out->half_height);
  if (fzgx_random_track_family_is_round_open(variant->family) != 0u) {
    sample_out->openness = fzgx_random_track_clampf(sample_out->openness, 0.5f, 1.0f);
  } else {
    sample_out->openness = 1.0f;
  }
}

static fzgx_status fzgx_random_track_build_run_sampling_plan(
    const fzgx_random_track_recipe *recipe,
    fzgx_random_track_run *run) {
  uint32_t local_index;
  uint32_t sample_index = 0u;
  float cumulative_distance = 0.0f;
  fzgx_vec3 previous_up = {0};
  fzgx_random_track_dense_sample previous_sample = {0};

  if ((recipe == 0) || (run == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  run->path_key_count = 1u;
  run->edge_subdivisions = (uint32_t *)calloc(run->edge_count, sizeof(*run->edge_subdivisions));
  if (run->edge_subdivisions == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  for (local_index = 0u; local_index < run->edge_count; ++local_index) {
    const fzgx_random_track_variant_recipe *start_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, 0u, (int32_t)local_index);
    const fzgx_random_track_variant_recipe *end_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, 0u, (int32_t)local_index + 1);

    run->edge_subdivisions[local_index] =
        fzgx_random_track_edge_sample_count(start_variant, end_variant);
    run->path_key_count += run->edge_subdivisions[local_index];
  }
  run->path_sample_times =
      (float *)calloc(run->path_key_count, sizeof(*run->path_sample_times));
  if (run->path_sample_times == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  {
    const fzgx_random_track_variant_recipe *start_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, 0u, 0);
    if (start_variant == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    fzgx_random_track_init_dense_sample_from_variant(start_variant, &previous_sample);
    previous_up = previous_sample.up;
    run->path_sample_times[0] = 0.0f;
    run->key_times[0] = 0.0f;
  }

  for (local_index = 0u; local_index < run->edge_count; ++local_index) {
    const fzgx_random_track_variant_recipe *start_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, 0u, (int32_t)local_index);
    const fzgx_random_track_variant_recipe *end_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, 0u, (int32_t)local_index + 1);
    uint32_t subdivision_count = run->edge_subdivisions[local_index];
    uint32_t subdivision_index;

    if ((start_variant == 0) || (end_variant == 0)) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    for (subdivision_index = 1u; subdivision_index <= subdivision_count; ++subdivision_index) {
      fzgx_random_track_dense_sample current_sample = {0};
      float t = (float)subdivision_index / (float)subdivision_count;

      fzgx_random_track_sample_variant_edge(
          start_variant, end_variant, t, &previous_up, &current_sample);
      cumulative_distance += fzgx_random_track_vec3_length(
          fzgx_random_track_vec3_sub(current_sample.position, previous_sample.position));
      sample_index += 1u;
      run->path_sample_times[sample_index] = cumulative_distance;
      previous_sample = current_sample;
    }
    run->key_times[local_index + 1u] = cumulative_distance;
  }

  run->checkpoint_total_length = cumulative_distance;
  run->checkpoint_count = (uint32_t)ceilf(
      fzgx_random_track_maxf(cumulative_distance, 1.0f) / FZGX_RANDOM_TRACK_METERS_PER_CHECKPOINT);
  if (run->checkpoint_count == 0u) {
    run->checkpoint_count = 1u;
  }
  run->checkpoint_times =
      (float *)calloc(run->checkpoint_count + 1u, sizeof(*run->checkpoint_times));
  run->checkpoint_distances =
      (float *)calloc(run->checkpoint_count + 1u, sizeof(*run->checkpoint_distances));
  if ((run->checkpoint_times == 0) || (run->checkpoint_distances == 0)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  for (local_index = 0u; local_index <= run->checkpoint_count; ++local_index) {
    float distance =
        cumulative_distance * ((float)local_index / (float)run->checkpoint_count);
    run->checkpoint_times[local_index] = distance;
    run->checkpoint_distances[local_index] = distance;
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_random_track_build_dense_curve(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    uint32_t slot,
    fzgx_random_track_dense_curve *curve_out) {
  uint32_t local_index;
  uint32_t sample_index = 0u;
  fzgx_vec3 previous_up = {0};

  if ((recipe == 0) || (run == 0) || (curve_out == 0) || (run->path_key_count == 0u) ||
      (run->path_sample_times == 0) || (run->edge_subdivisions == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(curve_out, 0, sizeof(*curve_out));
  curve_out->samples = (fzgx_random_track_dense_sample *)calloc(
      run->path_key_count, sizeof(*curve_out->samples));
  if (curve_out->samples == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  curve_out->count = run->path_key_count;

  {
    const fzgx_random_track_variant_recipe *start_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, slot, 0);
    if (start_variant == 0) {
      fzgx_random_track_dense_curve_release(curve_out);
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    fzgx_random_track_init_dense_sample_from_variant(start_variant, &curve_out->samples[0]);
    previous_up = curve_out->samples[0].up;
    curve_out->samples[0].time = run->path_sample_times[0];
  }

  for (local_index = 0u; local_index < run->edge_count; ++local_index) {
    const fzgx_random_track_variant_recipe *start_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, slot, (int32_t)local_index);
    const fzgx_random_track_variant_recipe *end_variant =
        fzgx_random_track_run_get_control_variant_signed(recipe, run, slot, (int32_t)local_index + 1);
    uint32_t subdivision_count = run->edge_subdivisions[local_index];
    uint32_t subdivision_index;

    if ((start_variant == 0) || (end_variant == 0)) {
      fzgx_random_track_dense_curve_release(curve_out);
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    for (subdivision_index = 1u; subdivision_index <= subdivision_count; ++subdivision_index) {
      float t = (float)subdivision_index / (float)subdivision_count;

      sample_index += 1u;
      fzgx_random_track_sample_variant_edge(
          start_variant, end_variant, t, &previous_up, &curve_out->samples[sample_index]);
      curve_out->samples[sample_index].time = run->path_sample_times[sample_index];
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_random_track_dense_curve_sample_at_time(
    const fzgx_random_track_dense_curve *curve,
    float time,
    fzgx_random_track_dense_sample *sample_out) {
  uint32_t sample_index;

  if ((curve == 0) || (sample_out == 0) || (curve->count == 0u) || (curve->samples == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (time <= curve->samples[0].time) {
    *sample_out = curve->samples[0];
    return FZGX_STATUS_OK;
  }
  if (curve->samples[curve->count - 1u].time <= time) {
    *sample_out = curve->samples[curve->count - 1u];
    return FZGX_STATUS_OK;
  }
  for (sample_index = 1u; sample_index < curve->count; ++sample_index) {
    const fzgx_random_track_dense_sample *lhs = &curve->samples[sample_index - 1u];
    const fzgx_random_track_dense_sample *rhs = &curve->samples[sample_index];
    if (time <= rhs->time) {
      float t = (time - lhs->time) / (rhs->time - lhs->time);

      sample_out->time = time;
      sample_out->position = fzgx_random_track_vec3_lerp(lhs->position, rhs->position, t);
      sample_out->forward = fzgx_random_track_vec3_normalize_or(
          fzgx_random_track_vec3_lerp(lhs->forward, rhs->forward, t), lhs->forward);
      sample_out->up = fzgx_random_track_vec3_normalize_or(
          fzgx_random_track_vec3_lerp(lhs->up, rhs->up, t), lhs->up);
      if (fzgx_random_track_vec3_dot(sample_out->up, lhs->up) < 0.0f) {
        sample_out->up = fzgx_random_track_vec3_scale(sample_out->up, -1.0f);
      }
      sample_out->width = lhs->width + (rhs->width - lhs->width) * t;
      sample_out->half_height =
          lhs->half_height + (rhs->half_height - lhs->half_height) * t;
      sample_out->openness = lhs->openness + (rhs->openness - lhs->openness) * t;
      return FZGX_STATUS_OK;
    }
  }
  *sample_out = curve->samples[curve->count - 1u];
  return FZGX_STATUS_OK;
}

static void fzgx_random_track_init_checkpoint_plane_from_sample(
    const fzgx_random_track_dense_sample *sample,
    uint32_t use_backward_normal,
    fzgx_plane *plane_out) {
  fzgx_vec3 normal;

  if ((sample == 0) || (plane_out == 0)) {
    return;
  }
  normal = sample->forward;
  if (use_backward_normal != 0u) {
    normal = fzgx_random_track_vec3_scale(normal, -1.0f);
  }
  plane_out->origin = sample->position;
  plane_out->normal = normal;
  plane_out->distance =
      -fzgx_random_track_vec3_dot(plane_out->normal, plane_out->origin);
}

static void fzgx_random_track_mirror_plane(
    const fzgx_plane *plane,
    fzgx_plane *mirrored_out) {
  if ((plane == 0) || (mirrored_out == 0)) {
    return;
  }
  mirrored_out->origin = plane->origin;
  mirrored_out->normal = fzgx_random_track_vec3_scale(plane->normal, -1.0f);
  mirrored_out->distance = -plane->distance;
}

static void fzgx_random_track_compute_sampled_tangents(
    const fzgx_random_track_dense_curve *curve,
    const float *values,
    float *tangents_out) {
  uint32_t sample_index;

  if ((curve == 0) || (values == 0) || (tangents_out == 0) || (curve->count == 0u)) {
    return;
  }
  if (curve->count == 1u) {
    tangents_out[0] = 0.0f;
    return;
  }
  for (sample_index = 0u; sample_index < curve->count; ++sample_index) {
    if (sample_index == 0u) {
      float dt = curve->samples[1u].time - curve->samples[0u].time;
      tangents_out[sample_index] = (fabsf(dt) > 1.0e-6f)
                                       ? ((values[1u] - values[0u]) / dt)
                                       : 0.0f;
    } else if ((sample_index + 1u) >= curve->count) {
      float dt = curve->samples[sample_index].time - curve->samples[sample_index - 1u].time;
      tangents_out[sample_index] = (fabsf(dt) > 1.0e-6f)
                                       ? ((values[sample_index] - values[sample_index - 1u]) / dt)
                                       : 0.0f;
    } else {
      float dt =
          curve->samples[sample_index + 1u].time - curve->samples[sample_index - 1u].time;
      tangents_out[sample_index] = (fabsf(dt) > 1.0e-6f)
                                       ? ((values[sample_index + 1u] - values[sample_index - 1u]) / dt)
                                       : 0.0f;
    }
  }
}

static uint32_t fzgx_random_track_variant_extra_segment_count(uint32_t family) {
  switch (family) {
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      return 2u;
    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
    default:
      return 1u;
  }
}

static fzgx_status fzgx_random_track_build_runs(
    const fzgx_random_track_recipe *recipe,
    fzgx_random_track_run **runs_out,
    uint32_t *run_count_out,
    uint32_t *segment_total_out) {
  fzgx_random_track_run *runs;
  uint32_t run_count = 0u;
  uint32_t segment_total = 0u;
  uint32_t run_start = 0u;

  if ((recipe == 0) || (recipe->nodes == 0) || (runs_out == 0) || (run_count_out == 0) ||
      (segment_total_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  runs = (fzgx_random_track_run *)calloc(recipe->node_count, sizeof(*runs));
  if (runs == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  while (run_start < recipe->node_count) {
    const fzgx_random_track_node_recipe *start_node = &recipe->nodes[run_start];
    fzgx_random_track_run *run = &runs[run_count];
    uint32_t edge_count = 1u;
    uint32_t slot;

    while ((run_start + edge_count) < recipe->node_count) {
      uint32_t previous_node_index = run_start + edge_count - 1u;
      uint32_t next_node_index = run_start + edge_count;
      const fzgx_random_track_node_recipe *previous_node = &recipe->nodes[previous_node_index];
      const fzgx_random_track_node_recipe *next_node = &recipe->nodes[next_node_index];

      if (((previous_node->gap_after_mask | previous_node->sharp_after_mask) != 0u) ||
          (fzgx_random_track_node_signature_matches(start_node, next_node) == 0u)) {
        break;
      }
      edge_count += 1u;
    }

    memset(run, 0, sizeof(*run));
    run->start_node = run_start;
    run->edge_count = edge_count;
    run->variant_count = start_node->variant_count;
    run->key_times = (float *)calloc(edge_count + 1u, sizeof(*run->key_times));
    if (run->key_times == 0) {
      fzgx_random_track_release_runs(runs, run_count + 1u);
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    for (slot = 0u; slot < run->variant_count; ++slot) {
      run->slot_family[slot] = start_node->variants[slot].family;
      run->slot_base_surface_kind[slot] = start_node->variants[slot].base_surface_kind;
      run->slot_overlay_surface_kind[slot] = start_node->variants[slot].overlay_surface_kind;
    }
    run->key_times[0] = 0.0f;
    for (slot = 1u; slot <= edge_count; ++slot) {
      const fzgx_random_track_variant_recipe *previous_variant =
          &recipe->nodes[(run_start + slot - 1u) % recipe->node_count].variants[0];
      const fzgx_random_track_variant_recipe *next_variant =
          &recipe->nodes[(run_start + slot) % recipe->node_count].variants[0];

      run->key_times[slot] =
          run->key_times[slot - 1u] +
          fzgx_random_track_vec3_length(
              fzgx_random_track_vec3_sub(next_variant->center, previous_variant->center));
    }
    {
      fzgx_status plan_status = fzgx_random_track_build_run_sampling_plan(recipe, run);
      if (plan_status != FZGX_STATUS_OK) {
        fzgx_random_track_release_runs(runs, run_count + 1u);
        return plan_status;
      }
    }

    segment_total += 2u;
    if (run->variant_count <= 1u) {
      segment_total += 1u + fzgx_random_track_variant_extra_segment_count(run->slot_family[0]);
    } else {
      for (slot = 1u; slot < run->variant_count; ++slot) {
        segment_total += 2u + fzgx_random_track_variant_extra_segment_count(run->slot_family[slot]);
      }
    }

    run_start += edge_count;
    run_count += 1u;
  }

  *runs_out = runs;
  *run_count_out = run_count;
  *segment_total_out = segment_total;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_random_track_build_animation_course(
    const fzgx_random_track_animation_buffer *buffer,
    uint32_t authored_track_id,
    fzgx_owned_track_course_animation_content *animation_course_out) {
  uint32_t keyable_attribute_count = 0u;
  uint32_t segment_index;
  uint32_t curve_index;
  uint32_t keyable_cursor = 0u;

  if ((buffer == 0) || (animation_course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(animation_course_out, 0, sizeof(*animation_course_out));

  for (segment_index = 0u; segment_index < buffer->count; ++segment_index) {
    for (curve_index = 0u; curve_index < 9u; ++curve_index) {
      keyable_attribute_count += buffer->segments[segment_index].curves[curve_index].keyable_count;
    }
  }

  animation_course_out->course.authored_track_id = authored_track_id;
  animation_course_out->course.animation_curve_trs_count = buffer->count;
  animation_course_out->course.animation_curve_count = buffer->count * 9u;
  animation_course_out->course.keyable_attribute_count = keyable_attribute_count;
  animation_course_out->course.track_segment_count = buffer->count;

  if (buffer->count == 0u) {
    return FZGX_STATUS_OK;
  }

  animation_course_out->track_segments = (fzgx_track_segment_animation_record *)calloc(
      buffer->count, sizeof(*animation_course_out->track_segments));
  animation_course_out->animation_curve_trs = (fzgx_animation_curve_trs *)calloc(
      buffer->count, sizeof(*animation_course_out->animation_curve_trs));
  animation_course_out->animation_curves = (fzgx_animation_curve *)calloc(
      buffer->count * 9u, sizeof(*animation_course_out->animation_curves));
  if ((animation_course_out->track_segments == 0) ||
      (animation_course_out->animation_curve_trs == 0) ||
      (animation_course_out->animation_curves == 0)) {
    fzgx_content_release_track_course_animation_content(animation_course_out);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (keyable_attribute_count != 0u) {
    animation_course_out->keyable_attributes = (fzgx_keyable_attribute *)calloc(
        keyable_attribute_count, sizeof(*animation_course_out->keyable_attributes));
    if (animation_course_out->keyable_attributes == 0) {
      fzgx_content_release_track_course_animation_content(animation_course_out);
      return FZGX_STATUS_OUT_OF_RANGE;
    }
  }

  animation_course_out->course.track_segments = animation_course_out->track_segments;
  for (segment_index = 0u; segment_index < buffer->count; ++segment_index) {
    const fzgx_random_track_segment_animation_spec *spec = &buffer->segments[segment_index];
    fzgx_track_segment_animation_record *animation_segment =
        &animation_course_out->track_segments[segment_index];
    fzgx_animation_curve_trs *curve_trs = &animation_course_out->animation_curve_trs[segment_index];

    animation_segment->address = spec->segment_address;
    animation_segment->animation_curves_trs_address = spec->animation_address;
    animation_segment->animation_curve_trs = curve_trs;
    curve_trs->curve_count = 9u;
    curve_trs->curves = &animation_course_out->animation_curves[segment_index * 9u];

    for (curve_index = 0u; curve_index < 9u; ++curve_index) {
      const fzgx_random_track_curve_spec *curve_spec = &spec->curves[curve_index];
      fzgx_animation_curve *curve =
          &animation_course_out->animation_curves[segment_index * 9u + curve_index];

      curve->keyable_count = curve_spec->keyable_count;
      if (curve_spec->keyable_count == 0u) {
        curve->keyables = 0;
        continue;
      }
      curve->keyables = &animation_course_out->keyable_attributes[keyable_cursor];
      memcpy(
          animation_course_out->keyable_attributes + keyable_cursor,
          curve_spec->keyables,
          curve_spec->keyable_count * sizeof(*curve_spec->keyables));
      keyable_cursor += curve_spec->keyable_count;
    }
  }
  return FZGX_STATUS_OK;
}

static const fzgx_random_track_variant_recipe *fzgx_random_track_run_get_control_variant(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    uint32_t slot,
    uint32_t point_index) {
  uint32_t node_index;

  if ((recipe == 0) || (run == 0) || (recipe->nodes == 0) || (recipe->node_count == 0u)) {
    return 0;
  }
  node_index = (run->start_node + point_index) % recipe->node_count;
  return fzgx_random_track_node_get_slot_or_shared(&recipe->nodes[node_index], slot);
}

static fzgx_track_segment_record *fzgx_random_track_push_segment(
    fzgx_track_segment_record *segments,
    uint32_t *segment_cursor_inout,
    uint32_t *address_cursor_inout) {
  fzgx_track_segment_record *segment = &segments[*segment_cursor_inout];

  memset(segment, 0, sizeof(*segment));
  segment->address = *address_cursor_inout;
  *segment_cursor_inout += 1u;
  *address_cursor_inout += 0x50u;
  return segment;
}

static fzgx_status fzgx_random_track_append_run_path_curves(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    fzgx_random_track_segment_animation_spec *root_spec,
    fzgx_random_track_segment_animation_spec *rz_spec,
    fzgx_vec3 *first_position_out,
    fzgx_vec3 *first_rotation_out) {
  fzgx_random_track_dense_curve curve = {0};
  float *scratch = 0;
  float *position_x = 0;
  float *position_y = 0;
  float *position_z = 0;
  float *rotation_x = 0;
  float *rotation_y = 0;
  float *rotation_z = 0;
  float *tangent_x = 0;
  float *tangent_y = 0;
  float *tangent_z = 0;
  float *tangent_rx = 0;
  float *tangent_ry = 0;
  float *tangent_rz = 0;
  uint32_t point_index;
  fzgx_status status;

  if ((recipe == 0) || (run == 0) || (root_spec == 0) || (rz_spec == 0) ||
      (first_position_out == 0) || (first_rotation_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_random_track_build_dense_curve(recipe, run, 0u, &curve);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  scratch = (float *)calloc(curve.count * 12u, sizeof(*scratch));
  if (scratch == 0) {
    fzgx_random_track_dense_curve_release(&curve);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  position_x = scratch;
  position_y = position_x + curve.count;
  position_z = position_y + curve.count;
  rotation_x = position_z + curve.count;
  rotation_y = rotation_x + curve.count;
  rotation_z = rotation_y + curve.count;
  tangent_x = rotation_z + curve.count;
  tangent_y = tangent_x + curve.count;
  tangent_z = tangent_y + curve.count;
  tangent_rx = tangent_z + curve.count;
  tangent_ry = tangent_rx + curve.count;
  tangent_rz = tangent_ry + curve.count;

  for (point_index = 0u; point_index < curve.count; ++point_index) {
    fzgx_mat43 transform = fzgx_random_track_pose_transform(
        curve.samples[point_index].position,
        curve.samples[point_index].forward,
        curve.samples[point_index].up);
    fzgx_vec3 rotation = {0};

    fzgx_random_track_extract_yxz_rotation_degrees(&transform, &rotation);
    if (point_index != 0u) {
      rotation.x = fzgx_random_track_unwrap_degrees(rotation.x, rotation_x[point_index - 1u]);
      rotation.y = fzgx_random_track_unwrap_degrees(rotation.y, rotation_y[point_index - 1u]);
      rotation.z = fzgx_random_track_unwrap_degrees(rotation.z, rotation_z[point_index - 1u]);
    }
    position_x[point_index] = curve.samples[point_index].position.x;
    position_y[point_index] = curve.samples[point_index].position.y;
    position_z[point_index] = curve.samples[point_index].position.z;
    rotation_x[point_index] = rotation.x;
    rotation_y[point_index] = rotation.y;
    rotation_z[point_index] = rotation.z;
  }
  fzgx_random_track_compute_sampled_tangents(&curve, position_x, tangent_x);
  fzgx_random_track_compute_sampled_tangents(&curve, position_y, tangent_y);
  fzgx_random_track_compute_sampled_tangents(&curve, position_z, tangent_z);
  fzgx_random_track_compute_sampled_tangents(&curve, rotation_x, tangent_rx);
  fzgx_random_track_compute_sampled_tangents(&curve, rotation_y, tangent_ry);
  fzgx_random_track_compute_sampled_tangents(&curve, rotation_z, tangent_rz);

  *first_position_out = (fzgx_vec3){position_x[0], position_y[0], position_z[0]};
  *first_rotation_out = (fzgx_vec3){rotation_x[0], rotation_y[0], rotation_z[0]};

  status = fzgx_random_track_animation_spec_set_curve_cubic(
      root_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_X,
      curve.count,
      run->path_sample_times,
      position_x,
      tangent_x);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      root_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_Y,
      curve.count,
      run->path_sample_times,
      position_y,
      tangent_y);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      root_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_Z,
      curve.count,
      run->path_sample_times,
      position_z,
      tangent_z);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      root_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_ROTATION_X,
      curve.count,
      run->path_sample_times,
      rotation_x,
      tangent_rx);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      root_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_ROTATION_Y,
      curve.count,
      run->path_sample_times,
      rotation_y,
      tangent_ry);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      rz_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_ROTATION_Z,
      curve.count,
      run->path_sample_times,
      rotation_z,
      tangent_rz);

cleanup:
  fzgx_random_track_dense_curve_release(&curve);
  free(scratch);
  return status;
}

static fzgx_status fzgx_random_track_append_slot_scale_curves(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    uint32_t slot,
    fzgx_random_track_segment_animation_spec *scale_spec,
    fzgx_vec3 *first_scale_out) {
  fzgx_random_track_dense_curve curve = {0};
  float *scratch = 0;
  float *widths = 0;
  float *half_heights = 0;
  float *width_tangents = 0;
  float *height_tangents = 0;
  uint32_t point_index;
  fzgx_status status;

  if ((recipe == 0) || (run == 0) || (scale_spec == 0) || (first_scale_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_random_track_build_dense_curve(recipe, run, slot, &curve);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  scratch = (float *)calloc(curve.count * 4u, sizeof(*scratch));
  if (scratch == 0) {
    fzgx_random_track_dense_curve_release(&curve);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  widths = scratch;
  half_heights = widths + curve.count;
  width_tangents = half_heights + curve.count;
  height_tangents = width_tangents + curve.count;

  for (point_index = 0u; point_index < curve.count; ++point_index) {
    widths[point_index] = curve.samples[point_index].width;
    half_heights[point_index] = curve.samples[point_index].half_height;
  }
  fzgx_random_track_compute_sampled_tangents(&curve, widths, width_tangents);
  fzgx_random_track_compute_sampled_tangents(&curve, half_heights, height_tangents);
  *first_scale_out = (fzgx_vec3){widths[0], half_heights[0], 1.0f};
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      scale_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_X,
      curve.count,
      run->path_sample_times,
      widths,
      width_tangents);
  if (status != FZGX_STATUS_OK) {
    fzgx_random_track_dense_curve_release(&curve);
    free(scratch);
    return status;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      scale_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_Y,
      curve.count,
      run->path_sample_times,
      half_heights,
      height_tangents);
  fzgx_random_track_dense_curve_release(&curve);
  free(scratch);
  return status;
}

static fzgx_status fzgx_random_track_append_slot_openness_curves(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    uint32_t slot,
    fzgx_random_track_segment_animation_spec *open_spec,
    fzgx_vec3 *first_scale_out) {
  fzgx_random_track_dense_curve curve = {0};
  float *scratch = 0;
  float *openness_values = 0;
  float *openness_tangents = 0;
  uint32_t point_index;
  fzgx_status status;

  if ((recipe == 0) || (run == 0) || (open_spec == 0) || (first_scale_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_random_track_build_dense_curve(recipe, run, slot, &curve);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  scratch = (float *)calloc(curve.count * 2u, sizeof(*scratch));
  if (scratch == 0) {
    fzgx_random_track_dense_curve_release(&curve);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  openness_values = scratch;
  openness_tangents = openness_values + curve.count;
  for (point_index = 0u; point_index < curve.count; ++point_index) {
    openness_values[point_index] = curve.samples[point_index].openness;
  }
  fzgx_random_track_compute_sampled_tangents(&curve, openness_values, openness_tangents);

  *first_scale_out = (fzgx_vec3){1.0f, openness_values[0], 1.0f};
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      open_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_Y,
      curve.count,
      run->path_sample_times,
      openness_values,
      openness_tangents);
  fzgx_random_track_dense_curve_release(&curve);
  free(scratch);
  return status;
}

static fzgx_status fzgx_random_track_append_branch_offset_curves(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *run,
    uint32_t slot,
    fzgx_random_track_segment_animation_spec *branch_spec,
    fzgx_vec3 *first_position_out) {
  fzgx_random_track_dense_curve shared_curve = {0};
  fzgx_random_track_dense_curve slot_curve = {0};
  float *scratch = 0;
  float *offset_x = 0;
  float *offset_y = 0;
  float *tangent_x = 0;
  float *tangent_y = 0;
  uint32_t point_index;
  fzgx_status status;

  if ((recipe == 0) || (run == 0) || (branch_spec == 0) || (first_position_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_random_track_build_dense_curve(recipe, run, 0u, &shared_curve);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_random_track_build_dense_curve(recipe, run, slot, &slot_curve);
  if (status != FZGX_STATUS_OK) {
    fzgx_random_track_dense_curve_release(&shared_curve);
    return status;
  }
  if (shared_curve.count != slot_curve.count) {
    fzgx_random_track_dense_curve_release(&shared_curve);
    fzgx_random_track_dense_curve_release(&slot_curve);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  scratch = (float *)calloc(shared_curve.count * 4u, sizeof(*scratch));
  if (scratch == 0) {
    fzgx_random_track_dense_curve_release(&shared_curve);
    fzgx_random_track_dense_curve_release(&slot_curve);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  offset_x = scratch;
  offset_y = offset_x + shared_curve.count;
  tangent_x = offset_y + shared_curve.count;
  tangent_y = tangent_x + shared_curve.count;

  for (point_index = 0u; point_index < shared_curve.count; ++point_index) {
    fzgx_vec3 right = fzgx_random_track_vec3_normalize_or(
        fzgx_random_track_vec3_cross(
            shared_curve.samples[point_index].up,
            fzgx_random_track_vec3_scale(shared_curve.samples[point_index].forward, -1.0f)),
        (fzgx_vec3){1.0f, 0.0f, 0.0f});
    fzgx_vec3 delta = fzgx_random_track_vec3_sub(
        slot_curve.samples[point_index].position, shared_curve.samples[point_index].position);

    offset_x[point_index] = fzgx_random_track_vec3_dot(delta, right);
    offset_y[point_index] = fzgx_random_track_vec3_dot(delta, shared_curve.samples[point_index].up);
  }
  fzgx_random_track_compute_sampled_tangents(&shared_curve, offset_x, tangent_x);
  fzgx_random_track_compute_sampled_tangents(&shared_curve, offset_y, tangent_y);

  *first_position_out = (fzgx_vec3){offset_x[0], offset_y[0], 0.0f};
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      branch_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_X,
      shared_curve.count,
      run->path_sample_times,
      offset_x,
      tangent_x);
  if (status != FZGX_STATUS_OK) {
    fzgx_random_track_dense_curve_release(&shared_curve);
    fzgx_random_track_dense_curve_release(&slot_curve);
    free(scratch);
    return status;
  }
  status = fzgx_random_track_animation_spec_set_curve_cubic(
      branch_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_Y,
      shared_curve.count,
      run->path_sample_times,
      offset_y,
      tangent_y);
  fzgx_random_track_dense_curve_release(&shared_curve);
  fzgx_random_track_dense_curve_release(&slot_curve);
  free(scratch);
  return status;
}

static fzgx_status fzgx_random_track_append_modulated_length_curve(
    const fzgx_random_track_run *run,
    fzgx_random_track_segment_animation_spec *mod_length_spec) {
  float times[3];
  float values[3];

  if ((run == 0) || (mod_length_spec == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  times[0] = 0.0f;
  times[1] = 0.5f * run->checkpoint_total_length;
  times[2] = run->checkpoint_total_length;
  values[0] = 1.0f;
  values[1] = 1.25f;
  values[2] = 1.0f;
  return fzgx_random_track_animation_spec_set_curve_linear(
      mod_length_spec, FZGX_RANDOM_TRACK_TRS_CURVE_SCALE_Y, 3u, times, values);
}

static fzgx_status fzgx_random_track_append_modulated_profile_curves(
    fzgx_random_track_segment_animation_spec *profile_spec) {
  static const float curve6_times[2] = {0.0f, 1000.0f};
  static const float curve6_values[2] = {-0.5f, 0.5f};
  static const float curve7_times[5] = {0.0f, 250.0f, 500.0f, 750.0f, 1000.0f};
  static const float curve7_values[5] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
  fzgx_status status;

  if (profile_spec == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_random_track_animation_spec_set_curve_linear(
      profile_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_X,
      2u,
      curve6_times,
      curve6_values);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_random_track_animation_spec_set_curve_linear(
      profile_spec,
      FZGX_RANDOM_TRACK_TRS_CURVE_POSITION_Y,
      5u,
      curve7_times,
      curve7_values);
}

static fzgx_status fzgx_random_track_write_run_slot_segments(
    const fzgx_random_track_recipe *recipe,
    fzgx_random_track_run *run,
    uint32_t slot,
    int32_t branch_index,
    fzgx_track_segment_record *segments,
    uint32_t *segment_cursor_inout,
    uint32_t *address_cursor_inout,
    fzgx_random_track_animation_buffer *animation_buffer,
    uint32_t *first_child_address_out) {
  fzgx_track_segment_record *scale_segment;
  fzgx_random_track_segment_animation_spec *scale_spec = 0;
  uint32_t chain_index;
  fzgx_status status;

  if ((recipe == 0) || (run == 0) || (segments == 0) || (segment_cursor_inout == 0) ||
      (address_cursor_inout == 0) || (animation_buffer == 0) || (first_child_address_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  chain_index = run->slot_transform_segment_count[slot];
  scale_segment =
      fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
  run->slot_transform_segment_addresses[slot][chain_index] = scale_segment->address;
  run->slot_transform_segment_count[slot] += 1u;
  *first_child_address_out = scale_segment->address;

  scale_segment->segment_type = 0x08u;
  scale_segment->children_count = 1u;
  scale_segment->branch_index = branch_index;
  status = fzgx_random_track_animation_buffer_append(animation_buffer, scale_segment->address, &scale_spec);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  scale_segment->animation_curves_trs_address = scale_spec->animation_address;
  status = fzgx_random_track_append_slot_scale_curves(recipe, run, slot, scale_spec, &scale_segment->fallback_scale);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  switch (run->slot_family[slot]) {
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD: {
      fzgx_track_segment_record *mod_length_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
      fzgx_track_segment_record *profile_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
      fzgx_random_track_segment_animation_spec *mod_length_spec = 0;
      fzgx_random_track_segment_animation_spec *profile_spec = 0;

      scale_segment->children_address = mod_length_segment->address;
      run->slot_transform_segment_addresses[slot][run->slot_transform_segment_count[slot]] =
          mod_length_segment->address;
      run->slot_transform_segment_count[slot] += 1u;

      mod_length_segment->segment_type = 0x08u;
      mod_length_segment->children_count = 1u;
      mod_length_segment->children_address = profile_segment->address;
      mod_length_segment->branch_index = branch_index;
      mod_length_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      status = fzgx_random_track_animation_buffer_append(
          animation_buffer, mod_length_segment->address, &mod_length_spec);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      mod_length_segment->animation_curves_trs_address = mod_length_spec->animation_address;
      status = fzgx_random_track_append_modulated_length_curve(run, mod_length_spec);
      if (status != FZGX_STATUS_OK) {
        return status;
      }

      profile_segment->segment_type = 0x00u;
      profile_segment->embedded_property_type = 0x20u;
      profile_segment->perimeter_flags = 0x0cu;
      profile_segment->rail_height_left = 3.0f;
      profile_segment->rail_height_right = 3.0f;
      profile_segment->branch_index = branch_index;
      profile_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      run->slot_source_segment_address[slot] = profile_segment->address;
      status = fzgx_random_track_animation_buffer_append(
          animation_buffer, profile_segment->address, &profile_spec);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      profile_segment->animation_curves_trs_address = profile_spec->animation_address;
      return fzgx_random_track_append_modulated_profile_curves(profile_spec);
    }

    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN: {
      fzgx_track_segment_record *open_root_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
      fzgx_track_segment_record *open_leaf_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
      fzgx_random_track_segment_animation_spec *open_leaf_spec = 0;

      scale_segment->children_address = open_root_segment->address;
      open_root_segment->segment_type = 0x00u;
      open_root_segment->embedded_property_type = 0x80u;
      open_root_segment->perimeter_flags = 0x0cu;
      open_root_segment->pipe_cylinder_flags =
          fzgx_random_track_pipe_flags_for_family(run->slot_family[slot]);
      open_root_segment->children_count = 1u;
      open_root_segment->children_address = open_leaf_segment->address;
      open_root_segment->branch_index = branch_index;
      open_root_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      open_root_segment->rail_height_left = 3.0f;
      open_root_segment->rail_height_right = 3.0f;

      open_leaf_segment->segment_type = 0x00u;
      open_leaf_segment->pipe_cylinder_flags = 0x02u;
      open_leaf_segment->branch_index = branch_index;
      status = fzgx_random_track_animation_buffer_append(
          animation_buffer, open_leaf_segment->address, &open_leaf_spec);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      open_leaf_segment->animation_curves_trs_address = open_leaf_spec->animation_address;
      status = fzgx_random_track_append_slot_openness_curves(
          recipe, run, slot, open_leaf_spec, &open_leaf_segment->fallback_scale);
      if (status != FZGX_STATUS_OK) {
        return status;
      }

      run->slot_source_segment_address[slot] = open_root_segment->address;
      return FZGX_STATUS_OK;
    }

    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE: {
      fzgx_track_segment_record *capsule_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);

      scale_segment->children_address = capsule_segment->address;
      capsule_segment->segment_type = 0x00u;
      capsule_segment->embedded_property_type = 0x40u;
      capsule_segment->branch_index = branch_index;
      capsule_segment->fallback_position = (fzgx_vec3){0.5f, 0.0f, 0.0f};
      capsule_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      run->slot_source_segment_address[slot] = capsule_segment->address;
      return FZGX_STATUS_OK;
    }

    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED: {
      fzgx_track_segment_record *pipe_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);

      scale_segment->children_address = pipe_segment->address;
      pipe_segment->segment_type = 0x01u;
      pipe_segment->pipe_cylinder_flags =
          fzgx_random_track_pipe_flags_for_family(run->slot_family[slot]);
      pipe_segment->branch_index = branch_index;
      pipe_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      run->slot_source_segment_address[slot] = pipe_segment->address;
      return FZGX_STATUS_OK;
    }

    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    default: {
      fzgx_track_segment_record *road_segment =
          fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);

      scale_segment->children_address = road_segment->address;
      road_segment->segment_type = 0x02u;
      road_segment->perimeter_flags = 0x0cu;
      road_segment->rail_height_left = 3.0f;
      road_segment->rail_height_right = 3.0f;
      road_segment->branch_index = branch_index;
      road_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
      run->slot_source_segment_address[slot] = road_segment->address;
      return FZGX_STATUS_OK;
    }
  }
}

static fzgx_status fzgx_random_track_write_run_segments(
    const fzgx_random_track_recipe *recipe,
    fzgx_random_track_run *run,
    fzgx_track_segment_record *segments,
    uint32_t *segment_cursor_inout,
    uint32_t *address_cursor_inout,
    fzgx_random_track_animation_buffer *animation_buffer) {
  fzgx_track_segment_record *root_segment;
  fzgx_track_segment_record *rz_segment;
  fzgx_random_track_segment_animation_spec *root_spec = 0;
  fzgx_random_track_segment_animation_spec *rz_spec = 0;
  fzgx_vec3 first_position = {0};
  fzgx_vec3 first_rotation = {0};
  uint32_t branch_segment_indices[FZGX_RANDOM_TRACK_MAX_VARIANTS] = {0};
  uint32_t slot;
  fzgx_status status;

  if ((recipe == 0) || (run == 0) || (segments == 0) || (segment_cursor_inout == 0) ||
      (address_cursor_inout == 0) || (animation_buffer == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  root_segment = fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
  rz_segment = fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);
  run->root_segment_address = root_segment->address;
  run->rz_segment_address = rz_segment->address;

  status = fzgx_random_track_animation_buffer_append(animation_buffer, root_segment->address, &root_spec);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_random_track_animation_buffer_append(animation_buffer, rz_segment->address, &rz_spec);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  root_segment->animation_curves_trs_address = root_spec->animation_address;
  rz_segment->animation_curves_trs_address = rz_spec->animation_address;
  status = fzgx_random_track_append_run_path_curves(
      recipe, run, root_spec, rz_spec, &first_position, &first_rotation);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  root_segment->segment_type = 0x08u;
  root_segment->children_count = 1u;
  root_segment->children_address = rz_segment->address;
  root_segment->fallback_position = first_position;
  root_segment->fallback_rotation = (fzgx_vec3){first_rotation.x, first_rotation.y, 0.0f};
  root_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};

  rz_segment->segment_type = 0x08u;
  rz_segment->children_count = (run->variant_count <= 1u) ? 1u : (run->variant_count - 1u);
  rz_segment->fallback_rotation = (fzgx_vec3){0.0f, 0.0f, first_rotation.z};
  rz_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};

  if (run->variant_count <= 1u) {
    status = fzgx_random_track_write_run_slot_segments(
        recipe,
        run,
        0u,
        0,
        segments,
        segment_cursor_inout,
        address_cursor_inout,
        animation_buffer,
        &rz_segment->children_address);
    return status;
  }

  rz_segment->children_address = *address_cursor_inout;
  for (slot = 1u; slot < run->variant_count; ++slot) {
    fzgx_track_segment_record *branch_segment =
        fzgx_random_track_push_segment(segments, segment_cursor_inout, address_cursor_inout);

    branch_segment_indices[slot] = (uint32_t)(branch_segment - segments);
    branch_segment->segment_type = 0x04u;
    branch_segment->children_count = 1u;
    branch_segment->branch_index = (int32_t)slot;
    branch_segment->fallback_scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};
    run->slot_transform_segment_addresses[slot][0] = branch_segment->address;
    run->slot_transform_segment_count[slot] = 1u;

    {
      fzgx_random_track_segment_animation_spec *branch_spec = 0;

      status = fzgx_random_track_animation_buffer_append(
          animation_buffer, branch_segment->address, &branch_spec);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      branch_segment->animation_curves_trs_address = branch_spec->animation_address;
      status = fzgx_random_track_append_branch_offset_curves(
          recipe, run, slot, branch_spec, &branch_segment->fallback_position);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
    }
  }

  for (slot = 1u; slot < run->variant_count; ++slot) {
    fzgx_track_segment_record *branch_segment = &segments[branch_segment_indices[slot]];

    status = fzgx_random_track_write_run_slot_segments(
        recipe,
        run,
        slot,
        (int32_t)slot,
        segments,
        segment_cursor_inout,
        address_cursor_inout,
        animation_buffer,
        &branch_segment->children_address);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  return FZGX_STATUS_OK;
}

static uint8_t fzgx_random_track_checkpoint_connect_in(
    const fzgx_random_track_recipe *recipe,
    uint32_t node_index,
    uint32_t slot) {
  uint32_t previous_node_index;
  const fzgx_random_track_node_recipe *previous_node;

  if ((recipe == 0) || (recipe->nodes == 0) || (recipe->node_count == 0u)) {
    return 0u;
  }
  previous_node_index = (node_index + recipe->node_count - 1u) % recipe->node_count;
  previous_node = &recipe->nodes[previous_node_index];
  if (slot >= previous_node->variant_count) {
    return 0u;
  }
  if ((((previous_node->gap_after_mask | previous_node->sharp_after_mask) >> slot) & 1u) != 0u) {
    return 0u;
  }
  return 1u;
}

static uint8_t fzgx_random_track_checkpoint_connect_out(
    const fzgx_random_track_recipe *recipe,
    uint32_t node_index,
    uint32_t slot) {
  const fzgx_random_track_node_recipe *node;
  const fzgx_random_track_node_recipe *next_node;

  if ((recipe == 0) || (recipe->nodes == 0) || (recipe->node_count == 0u)) {
    return 0u;
  }
  node = &recipe->nodes[node_index];
  next_node = &recipe->nodes[(node_index + 1u) % recipe->node_count];
  if (slot >= next_node->variant_count) {
    return 0u;
  }
  if ((((node->gap_after_mask | node->sharp_after_mask) >> slot) & 1u) != 0u) {
    return 0u;
  }
  return 1u;
}

static fzgx_status fzgx_random_track_apply_segment_by_address(
    const fzgx_owned_track_course_content *track_course,
    const fzgx_owned_track_course_animation_content *animation_course,
    uint32_t segment_address,
    float time,
    fzgx_mat43 *transform_inout,
    fzgx_vec3 *scale_inout) {
  const fzgx_track_segment_record *segment = 0;
  const fzgx_track_segment_animation_record *animation_segment = 0;
  fzgx_status status;

  if (segment_address == 0u) {
    return FZGX_STATUS_OK;
  }
  status = fzgx_track_course_find_track_segment_by_address(
      &track_course->course, segment_address, &segment);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        &animation_course->course, segment_address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
      status = FZGX_STATUS_OK;
    }
  }
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_segment_apply_trs(
      segment, animation_segment, time, transform_inout, scale_inout, 0);
}

static fzgx_status fzgx_random_track_run_sample_source_piece_state(
    const fzgx_owned_track_course_content *track_course,
    const fzgx_owned_track_course_animation_content *animation_course,
    const fzgx_random_track_run *run,
    uint32_t slot,
    float time,
    fzgx_random_track_source_piece_sample *sample_out) {
  uint32_t chain_index;
  fzgx_status status;

  if ((track_course == 0) || (run == 0) || (sample_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(sample_out, 0, sizeof(*sample_out));
  sample_out->transform = fzgx_random_track_mat43_identity();
  sample_out->scale = (fzgx_vec3){1.0f, 1.0f, 1.0f};

  status = fzgx_random_track_apply_segment_by_address(
      track_course, animation_course, run->root_segment_address, time, &sample_out->transform, &sample_out->scale);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_random_track_apply_segment_by_address(
      track_course, animation_course, run->rz_segment_address, time, &sample_out->transform, &sample_out->scale);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  for (chain_index = 0u; chain_index < run->slot_transform_segment_count[slot]; ++chain_index) {
    status = fzgx_random_track_apply_segment_by_address(
        track_course,
        animation_course,
        run->slot_transform_segment_addresses[slot][chain_index],
        time,
        &sample_out->transform,
        &sample_out->scale);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  status = fzgx_track_course_find_track_segment_by_address(
      &track_course->course, run->slot_source_segment_address[slot], &sample_out->track_segment);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        &animation_course->course,
        run->slot_source_segment_address[slot],
        &sample_out->animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      sample_out->animation_segment = 0;
      status = FZGX_STATUS_OK;
    }
  }
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_track_segment_build_source_piece_word(
      sample_out->track_segment, &sample_out->source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((sample_out->source_piece_word & 0x00600000u) == 0u) {
    return fzgx_track_segment_apply_trs(
        sample_out->track_segment,
        sample_out->animation_segment,
        time,
        &sample_out->transform,
        &sample_out->scale,
        0);
  }
  return FZGX_STATUS_OK;
}

static uint32_t fzgx_random_track_lateral_subdivision_count(uint32_t family) {
  switch (family) {
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_CLOSED:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_CLOSED:
      return 14u;
    case FZGX_RANDOM_TRACK_FAMILY_CAPSULE:
      return 16u;
    case FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN:
    case FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN:
      return 12u;
    case FZGX_RANDOM_TRACK_FAMILY_MODULATED_ROAD:
      return 8u;
    case FZGX_RANDOM_TRACK_FAMILY_FLAT_ROAD:
    default:
      return 6u;
  }
}

static fzgx_status fzgx_random_track_finalize_surface_geometry(
    fzgx_random_track_quad_buffer *surface_buffers,
    fzgx_random_track_object_buffer *object_buffer,
    fzgx_owned_static_collider_course *static_course_out,
    fzgx_owned_dynamic_scene_collision_course *dynamic_course_out) {
  fzgx_vec3 bbox_min = {1000000.0f, 1000000.0f, 1000000.0f};
  fzgx_vec3 bbox_max = {-1000000.0f, -1000000.0f, -1000000.0f};
  uint32_t total_quads = 0u;
  uint32_t surface_index;
  uint32_t current_quad_cursor = 0u;

  if ((surface_buffers == 0) || (object_buffer == 0) || (static_course_out == 0) ||
      (dynamic_course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  for (surface_index = 0u; surface_index < FZGX_RANDOM_TRACK_SURFACE_COUNT; ++surface_index) {
    uint32_t quad_index;

    total_quads += surface_buffers[surface_index].count;
    for (quad_index = 0u; quad_index < surface_buffers[surface_index].count; ++quad_index) {
      const fzgx_static_collider_quad_record *quad = &surface_buffers[surface_index].quads[quad_index];
      const fzgx_vec3 points[4] = {quad->vertex0, quad->vertex1, quad->vertex2, quad->vertex3};
      uint32_t point_index;

      for (point_index = 0u; point_index < 4u; ++point_index) {
        bbox_min.x = fzgx_random_track_minf(bbox_min.x, points[point_index].x);
        bbox_min.y = fzgx_random_track_minf(bbox_min.y, points[point_index].y);
        bbox_min.z = fzgx_random_track_minf(bbox_min.z, points[point_index].z);
        bbox_max.x = fzgx_random_track_maxf(bbox_max.x, points[point_index].x);
        bbox_max.y = fzgx_random_track_maxf(bbox_max.y, points[point_index].y);
        bbox_max.z = fzgx_random_track_maxf(bbox_max.z, points[point_index].z);
      }
    }
  }

  memset(static_course_out, 0, sizeof(*static_course_out));
  memset(dynamic_course_out, 0, sizeof(*dynamic_course_out));
  static_course_out->surface_count = FZGX_RANDOM_TRACK_SURFACE_COUNT;
  static_course_out->quad_count = total_quads;
  static_course_out->quad_index_count = total_quads;
  static_course_out->surface_grids = (fzgx_static_collider_surface_grid *)calloc(
      static_course_out->surface_count, sizeof(*static_course_out->surface_grids));
  if (static_course_out->surface_grids == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (total_quads != 0u) {
    static_course_out->quads = (fzgx_static_collider_quad_record *)calloc(
        total_quads, sizeof(*static_course_out->quads));
    static_course_out->quad_indices = (uint16_t *)calloc(
        total_quads, sizeof(*static_course_out->quad_indices));
    if ((static_course_out->quads == 0) || (static_course_out->quad_indices == 0)) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
  } else {
    bbox_min = (fzgx_vec3){-32.0f, -8.0f, -32.0f};
    bbox_max = (fzgx_vec3){32.0f, 8.0f, 32.0f};
  }

  for (surface_index = 0u; surface_index < FZGX_RANDOM_TRACK_SURFACE_COUNT; ++surface_index) {
    uint32_t quad_index;
    fzgx_static_collider_surface_grid *surface_grid = &static_course_out->surface_grids[surface_index];

    surface_grid->quad_cells[0].offset = current_quad_cursor;
    surface_grid->quad_cells[0].count = surface_buffers[surface_index].count;
    for (quad_index = 0u; quad_index < surface_buffers[surface_index].count; ++quad_index) {
      static_course_out->quads[current_quad_cursor] = surface_buffers[surface_index].quads[quad_index];
      static_course_out->quad_indices[current_quad_cursor] = (uint16_t)current_quad_cursor;
      current_quad_cursor += 1u;
    }
  }

  static_course_out->mesh_grid.left = bbox_min.x - 1.0f;
  static_course_out->mesh_grid.top = bbox_min.z - 1.0f;
  static_course_out->mesh_grid.subdivision_width =
      fzgx_random_track_maxf(8.0f, (bbox_max.x - bbox_min.x) + 2.0f);
  static_course_out->mesh_grid.subdivision_length =
      fzgx_random_track_maxf(8.0f, (bbox_max.z - bbox_min.z) + 2.0f);
  static_course_out->mesh_grid.num_subdivisions_x = 1;
  static_course_out->mesh_grid.num_subdivisions_z = 1;
  static_course_out->bounding_sphere.origin.x = 0.5f * (bbox_min.x + bbox_max.x);
  static_course_out->bounding_sphere.origin.y = 0.5f * (bbox_min.y + bbox_max.y);
  static_course_out->bounding_sphere.origin.z = 0.5f * (bbox_min.z + bbox_max.z);
  {
    fzgx_vec3 extents = {
        0.5f * (bbox_max.x - bbox_min.x),
        0.5f * (bbox_max.y - bbox_min.y),
        0.5f * (bbox_max.z - bbox_min.z),
    };
    static_course_out->bounding_sphere.radius = sqrtf(
        extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
  }

  dynamic_course_out->object_count = object_buffer->count;
  dynamic_course_out->objects = object_buffer->objects;
  object_buffer->objects = 0;
  object_buffer->count = 0u;
  object_buffer->capacity = 0u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_random_track_build_surface_geometry_from_authored(
    const fzgx_random_track_recipe *recipe,
    const fzgx_random_track_run *runs,
    uint32_t run_count,
    const fzgx_owned_track_course_content *track_course,
    const fzgx_owned_track_course_animation_content *animation_course,
    fzgx_owned_static_collider_course *static_course_out,
    fzgx_owned_dynamic_scene_collision_course *dynamic_course_out) {
  fzgx_random_track_quad_buffer surface_buffers[FZGX_RANDOM_TRACK_SURFACE_COUNT];
  fzgx_random_track_object_buffer object_buffer;
  uint32_t run_index;
  fzgx_status status = FZGX_STATUS_OK;

  if ((recipe == 0) || (runs == 0) || (track_course == 0) || (static_course_out == 0) ||
      (dynamic_course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  memset(surface_buffers, 0, sizeof(surface_buffers));
  memset(&object_buffer, 0, sizeof(object_buffer));

  for (run_index = 0u; run_index < run_count; ++run_index) {
    const fzgx_random_track_run *run = &runs[run_index];
    uint32_t slot_start = (run->variant_count <= 1u) ? 0u : 1u;
    uint32_t slot;

    for (slot = slot_start; slot < run->variant_count; ++slot) {
      uint32_t node_local_index;

      for (node_local_index = 0u; node_local_index < run->edge_count; ++node_local_index) {
        uint32_t node_index = (run->start_node + node_local_index) % recipe->node_count;
        const fzgx_random_track_node_recipe *node = &recipe->nodes[node_index];
        float time_start = run->key_times[node_local_index];
        float time_end = run->key_times[node_local_index + 1u];
        float edge_length = time_end - time_start;
        uint32_t longitudinal_steps;
        uint32_t longitudinal_index;

        if ((((node->gap_after_mask | node->sharp_after_mask) >> slot) & 1u) != 0u) {
          continue;
        }
        longitudinal_steps = (uint32_t)ceilf(edge_length / 36.0f);
        if (longitudinal_steps == 0u) {
          longitudinal_steps = 1u;
        }

        for (longitudinal_index = 0u; longitudinal_index < longitudinal_steps; ++longitudinal_index) {
          float alpha0 = (float)longitudinal_index / (float)longitudinal_steps;
          float alpha1 = (float)(longitudinal_index + 1u) / (float)longitudinal_steps;
          float span_time0 = time_start + (time_end - time_start) * alpha0;
          float span_time1 = time_start + (time_end - time_start) * alpha1;
          fzgx_random_track_source_piece_sample sample0 = {};
          fzgx_random_track_source_piece_sample sample1 = {};
          uint32_t lateral_steps;
          uint32_t lateral_index;

          status = fzgx_random_track_run_sample_source_piece_state(
              track_course, animation_course, run, slot, span_time0, &sample0);
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }
          status = fzgx_random_track_run_sample_source_piece_state(
              track_course, animation_course, run, slot, span_time1, &sample1);
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }

          lateral_steps = fzgx_random_track_lateral_subdivision_count(run->slot_family[slot]);
          for (lateral_index = 0u; lateral_index < lateral_steps; ++lateral_index) {
            float lateral_alpha0 = (float)lateral_index / (float)lateral_steps;
            float lateral_alpha1 = (float)(lateral_index + 1u) / (float)lateral_steps;
            float lateral0 = -0.5f + lateral_alpha0;
            float lateral1 = -0.5f + lateral_alpha1;
            fzgx_vec3 interior_reference;

            if ((run->slot_overlay_surface_kind[slot] != 0u) &&
                (run->slot_overlay_surface_kind[slot] != run->slot_base_surface_kind[slot]) &&
                (run->slot_overlay_surface_kind[slot] < FZGX_RANDOM_TRACK_SURFACE_COUNT)) {
              fzgx_vec3 overlay00 = {0};
              fzgx_vec3 overlay01 = {0};
              fzgx_vec3 overlay10 = {0};
              fzgx_vec3 overlay11 = {0};

              status = fzgx_track_segment_sample_surface_world_pos(
                  &track_course->course,
                  (animation_course == 0) ? 0 : &animation_course->course,
                  sample0.track_segment,
                  sample0.animation_segment,
                  span_time0,
                  &sample0.transform,
                  &sample0.scale,
                  sample0.source_piece_word,
                  -0.2f,
                  &overlay00);
              if (status != FZGX_STATUS_OK) {
                goto cleanup;
              }
              status = fzgx_track_segment_sample_surface_world_pos(
                  &track_course->course,
                  (animation_course == 0) ? 0 : &animation_course->course,
                  sample0.track_segment,
                  sample0.animation_segment,
                  span_time0,
                  &sample0.transform,
                  &sample0.scale,
                  sample0.source_piece_word,
                  0.2f,
                  &overlay01);
              if (status != FZGX_STATUS_OK) {
                goto cleanup;
              }
              status = fzgx_track_segment_sample_surface_world_pos(
                  &track_course->course,
                  (animation_course == 0) ? 0 : &animation_course->course,
                  sample1.track_segment,
                  sample1.animation_segment,
                  span_time1,
                  &sample1.transform,
                  &sample1.scale,
                  sample1.source_piece_word,
                  -0.2f,
                  &overlay10);
              if (status != FZGX_STATUS_OK) {
                goto cleanup;
              }
              status = fzgx_track_segment_sample_surface_world_pos(
                  &track_course->course,
                  (animation_course == 0) ? 0 : &animation_course->course,
                  sample1.track_segment,
                  sample1.animation_segment,
                  span_time1,
                  &sample1.transform,
                  &sample1.scale,
                  sample1.source_piece_word,
                  0.2f,
                  &overlay11);
              if (status != FZGX_STATUS_OK) {
                goto cleanup;
              }
              interior_reference = (fzgx_vec3){
                  0.5f * (sample0.transform.origin_x + sample1.transform.origin_x),
                  0.5f * (sample0.transform.origin_y + sample1.transform.origin_y),
                  0.5f * (sample0.transform.origin_z + sample1.transform.origin_z),
              };
              interior_reference = fzgx_random_track_vec3_add(
                  interior_reference,
                  (fzgx_vec3){
                      0.5f * (sample0.transform.basis_y_x + sample1.transform.basis_y_x),
                      0.5f * (sample0.transform.basis_y_y + sample1.transform.basis_y_y),
                      0.5f * (sample0.transform.basis_y_z + sample1.transform.basis_y_z),
                  });
              status = fzgx_random_track_append_loft_quad(
                  &surface_buffers[run->slot_overlay_surface_kind[slot]],
                  overlay00,
                  overlay10,
                  overlay11,
                  overlay01,
                  interior_reference);
              if (status != FZGX_STATUS_OK) {
                goto cleanup;
              }
            }
          }
        }

        if (((node->mine_mask >> slot) & 1u) != 0u) {
          float midpoint_time = 0.5f * (time_start + time_end);
          fzgx_random_track_source_piece_sample mine_sample = {};
          fzgx_vec3 mine_center = {0};
          fzgx_vec3 right;
          fzgx_vec3 up;
          fzgx_vec3 backward;
          fzgx_mat43 mine_transform;

          status = fzgx_random_track_run_sample_source_piece_state(
              track_course, animation_course, run, slot, midpoint_time, &mine_sample);
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }
          status = fzgx_track_segment_sample_surface_world_pos(
              &track_course->course,
              (animation_course == 0) ? 0 : &animation_course->course,
              mine_sample.track_segment,
              mine_sample.animation_segment,
              midpoint_time,
              &mine_sample.transform,
              &mine_sample.scale,
              mine_sample.source_piece_word,
              0.0f,
              &mine_center);
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }

          right = (fzgx_vec3){
              mine_sample.transform.basis_x_x,
              mine_sample.transform.basis_x_y,
              mine_sample.transform.basis_x_z,
          };
          up = (fzgx_vec3){
              mine_sample.transform.basis_y_x,
              mine_sample.transform.basis_y_y,
              mine_sample.transform.basis_y_z,
          };
          backward = (fzgx_vec3){
              mine_sample.transform.basis_z_x,
              mine_sample.transform.basis_z_y,
              mine_sample.transform.basis_z_z,
          };
          memset(&mine_transform, 0, sizeof(mine_transform));
          right = fzgx_random_track_vec3_normalize_or(right, (fzgx_vec3){1.0f, 0.0f, 0.0f});
          up = fzgx_random_track_vec3_normalize_or(up, (fzgx_vec3){0.0f, 1.0f, 0.0f});
          backward = fzgx_random_track_vec3_normalize_or(backward, (fzgx_vec3){0.0f, 0.0f, 1.0f});
          mine_transform.basis_x_x = right.x;
          mine_transform.basis_x_y = right.y;
          mine_transform.basis_x_z = right.z;
          mine_transform.basis_y_x = up.x;
          mine_transform.basis_y_y = up.y;
          mine_transform.basis_y_z = up.z;
          mine_transform.basis_z_x = -backward.x;
          mine_transform.basis_z_y = -backward.y;
          mine_transform.basis_z_z = -backward.z;
          mine_transform.origin_x = mine_center.x + 0.2f * up.x;
          mine_transform.origin_y = mine_center.y + 0.2f * up.y;
          mine_transform.origin_z = mine_center.z + 0.2f * up.z;
          status = fzgx_random_track_object_buffer_append_mine(&object_buffer, &mine_transform);
          if (status != FZGX_STATUS_OK) {
            goto cleanup;
          }
        }
      }
    }
  }

  status = fzgx_random_track_finalize_surface_geometry(
      surface_buffers, &object_buffer, static_course_out, dynamic_course_out);

cleanup:
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_static_collider_course(static_course_out);
    fzgx_content_release_dynamic_scene_collision_course(dynamic_course_out);
  }
  fzgx_random_track_release_quad_buffers(surface_buffers, FZGX_RANDOM_TRACK_SURFACE_COUNT);
  fzgx_random_track_release_object_buffer(&object_buffer);
  return status;
}

fzgx_status fzgx_random_track_compile_recipe(
    const fzgx_random_track_recipe *recipe,
    fzgx_generated_track_content *generated_out) {
  fzgx_random_track_run *runs = 0;
  uint32_t run_count = 0u;
  fzgx_random_track_animation_buffer animation_buffer = {0};
  uint32_t track_node_total = 0u;
  uint32_t checkpoint_total = 0u;
  uint32_t segment_total = 0u;
  uint32_t track_node_cursor = 0u;
  uint32_t checkpoint_cursor = 0u;
  uint32_t segment_cursor = 0u;
  uint32_t address_cursor = 0x100u;
  uint32_t node_index;
  uint32_t run_index;
  float authored_total_distance = 0.0f;
  float checkpoint_distance_offset = 0.0f;
  fzgx_status status;

  if ((recipe == 0) || (generated_out == 0) || (recipe->nodes == 0) || (recipe->node_count == 0u)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(generated_out, 0, sizeof(*generated_out));

  status = fzgx_random_track_build_runs(recipe, &runs, &run_count, &segment_total);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  for (run_index = 0u; run_index < run_count; ++run_index) {
    track_node_total += runs[run_index].checkpoint_count;
    checkpoint_total += runs[run_index].checkpoint_count * runs[run_index].variant_count;
    authored_total_distance += runs[run_index].checkpoint_total_length;
  }

  generated_out->track_course.course.authored_track_id = recipe->authored_track_id;
  generated_out->track_course.course.track_node_count = track_node_total;
  generated_out->track_course.course.checkpoint_record_count = checkpoint_total;
  generated_out->track_course.course.track_segment_count = segment_total;
  generated_out->track_course.course.track_corner_count = 0u;
  generated_out->track_course.course.track_total_distance = authored_total_distance;
  generated_out->track_course.course.track_min_height = recipe->track_min_height;
  generated_out->track_course.track_nodes = (fzgx_track_node_record *)calloc(
      track_node_total, sizeof(*generated_out->track_course.track_nodes));
  generated_out->track_course.checkpoints = (fzgx_checkpoint_record *)calloc(
      checkpoint_total, sizeof(*generated_out->track_course.checkpoints));
  generated_out->track_course.track_segments = (fzgx_track_segment_record *)calloc(
      segment_total, sizeof(*generated_out->track_course.track_segments));
  if ((generated_out->track_course.track_nodes == 0) ||
      (generated_out->track_course.checkpoints == 0) ||
      (generated_out->track_course.track_segments == 0)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  for (run_index = 0u; run_index < run_count; ++run_index) {
    status = fzgx_random_track_write_run_segments(
        recipe,
        &runs[run_index],
        generated_out->track_course.track_segments,
        &segment_cursor,
        &address_cursor,
        &animation_buffer);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  generated_out->track_course.course.track_nodes = generated_out->track_course.track_nodes;
  generated_out->track_course.course.checkpoints = generated_out->track_course.checkpoints;
  generated_out->track_course.course.track_segments = generated_out->track_course.track_segments;
  generated_out->track_course.course.track_corners = 0;

  status = fzgx_random_track_build_animation_course(
      &animation_buffer,
      recipe->authored_track_id,
      &generated_out->animation_course);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }

  for (run_index = 0u; run_index < run_count; ++run_index) {
    const fzgx_random_track_run *run = &runs[run_index];
    fzgx_random_track_dense_curve slot_curves[FZGX_RANDOM_TRACK_MAX_VARIANTS] = {0};
    uint32_t checkpoint_index;
    uint32_t variant_index;
    uint32_t run_end_node_index =
        (run->start_node + run->edge_count - 1u) % recipe->node_count;
    uint32_t slot_count_to_build = run->variant_count;

    for (variant_index = 0u; variant_index < slot_count_to_build; ++variant_index) {
      status = fzgx_random_track_build_dense_curve(
          recipe, run, variant_index, &slot_curves[variant_index]);
      if (status != FZGX_STATUS_OK) {
        uint32_t release_index;
        for (release_index = 0u; release_index < slot_count_to_build; ++release_index) {
          fzgx_random_track_dense_curve_release(&slot_curves[release_index]);
        }
        goto cleanup;
      }
    }

    for (checkpoint_index = 0u; checkpoint_index < run->checkpoint_count; ++checkpoint_index) {
      fzgx_track_node_record *track_node =
          &generated_out->track_course.track_nodes[track_node_cursor];

      memset(track_node, 0, sizeof(*track_node));
      track_node->checkpoint_count = run->variant_count;
      track_node->checkpoint_offset = checkpoint_cursor;
      track_node->checkpoint_address = 0x400u + checkpoint_cursor * 0x50u;
      track_node->root_segment_address = run->root_segment_address;

      for (variant_index = 0u; variant_index < run->variant_count; ++variant_index) {
        fzgx_random_track_dense_sample start_sample = {0};
        fzgx_random_track_dense_sample end_sample = {0};
        fzgx_checkpoint_record *checkpoint =
            &generated_out->track_course.checkpoints[checkpoint_cursor];

        status = fzgx_random_track_dense_curve_sample_at_time(
            &slot_curves[variant_index], run->checkpoint_times[checkpoint_index], &start_sample);
        if (status != FZGX_STATUS_OK) {
          uint32_t release_index;
          for (release_index = 0u; release_index < slot_count_to_build; ++release_index) {
            fzgx_random_track_dense_curve_release(&slot_curves[release_index]);
          }
          goto cleanup;
        }
        status = fzgx_random_track_dense_curve_sample_at_time(
            &slot_curves[variant_index], run->checkpoint_times[checkpoint_index + 1u], &end_sample);
        if (status != FZGX_STATUS_OK) {
          uint32_t release_index;
          for (release_index = 0u; release_index < slot_count_to_build; ++release_index) {
            fzgx_random_track_dense_curve_release(&slot_curves[release_index]);
          }
          goto cleanup;
        }

        memset(checkpoint, 0, sizeof(*checkpoint));
        checkpoint->curve_time_start = run->checkpoint_times[checkpoint_index];
        checkpoint->curve_time_end = run->checkpoint_times[checkpoint_index + 1u];
        fzgx_random_track_init_checkpoint_plane_from_sample(
            &start_sample, 0u, &checkpoint->plane_start);
        fzgx_random_track_init_checkpoint_plane_from_sample(
            &end_sample, 1u, &checkpoint->plane_end);
        checkpoint->start_distance =
            checkpoint_distance_offset + checkpoint->curve_time_start;
        checkpoint->end_distance =
            checkpoint_distance_offset + checkpoint->curve_time_end;
        checkpoint->track_width = start_sample.width;
        checkpoint->connect_to_track_in =
            (checkpoint_index != 0u)
                ? 1u
                : fzgx_random_track_checkpoint_connect_in(recipe, run->start_node, variant_index);
        checkpoint->connect_to_track_out =
            ((checkpoint_index + 1u) < run->checkpoint_count)
                ? 1u
                : fzgx_random_track_checkpoint_connect_out(
                      recipe, run_end_node_index, variant_index);
        checkpoint_cursor += 1u;
      }
      track_node_cursor += 1u;
    }

    for (variant_index = 0u; variant_index < slot_count_to_build; ++variant_index) {
      fzgx_random_track_dense_curve_release(&slot_curves[variant_index]);
    }
    checkpoint_distance_offset += run->checkpoint_total_length;
  }

  generated_out->manifest.authored_track_id = recipe->authored_track_id;
  generated_out->manifest.checkpoint_count = track_node_total;
  generated_out->manifest.checkpoint_variant_count = 1u;
  generated_out->manifest.circuit_type = FZGX_CIRCUIT_TYPE_CLOSED;
  generated_out->manifest.time_extension_trigger_count = 0u;
  generated_out->manifest.supports_branching = 0u;
  for (node_index = 0u; node_index < recipe->node_count; ++node_index) {
    if (recipe->nodes[node_index].variant_count > generated_out->manifest.checkpoint_variant_count) {
      generated_out->manifest.checkpoint_variant_count = recipe->nodes[node_index].variant_count;
    }
    if (recipe->nodes[node_index].variant_count > 1u) {
      generated_out->manifest.supports_branching = 1u;
    }
  }

  status = fzgx_random_track_build_surface_geometry_from_authored(
      recipe,
      runs,
      run_count,
      &generated_out->track_course,
      &generated_out->animation_course,
      &generated_out->static_course,
      &generated_out->dynamic_course);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  fzgx_random_track_release_animation_buffer(&animation_buffer);
  fzgx_random_track_release_runs(runs, run_count);
  return FZGX_STATUS_OK;

cleanup:
  fzgx_random_track_release_animation_buffer(&animation_buffer);
  fzgx_random_track_release_runs(runs, run_count);
  if (status != FZGX_STATUS_OK) {
    fzgx_random_track_release_generated_track_content(generated_out);
  }
  return status;
}

void fzgx_random_track_release_generated_track_content(
    fzgx_generated_track_content *generated) {
  if (generated == 0) {
    return;
  }
  fzgx_content_release_track_course_content(&generated->track_course);
  fzgx_content_release_track_course_animation_content(&generated->animation_course);
  fzgx_content_release_static_collider_course(&generated->static_course);
  fzgx_content_release_dynamic_scene_collision_course(&generated->dynamic_course);
  memset(generated, 0, sizeof(*generated));
}
