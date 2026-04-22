#include "fzgx/camera.h"

#include "../../catalog/sine_lut_14b.inc"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct fzgx_game_camera_follow_triplet_preset {
  float local_height;
  float local_distance;
  int16_t pitch_angle16;
  uint16_t reserved0;
} fzgx_game_camera_follow_triplet_preset;

typedef struct fzgx_game_camera_follow_preset_triplet {
  fzgx_game_camera_follow_triplet_preset triplets[3];
} fzgx_game_camera_follow_preset_triplet;

typedef struct fzgx_game_camera_cycle_preset {
  uint32_t duration_frames;
  float blend_step;
  uint32_t word_0x08_raw;
  fzgx_vec3 start_pos;
  fzgx_vec3 start_interest;
  float start_perspective;
  int16_t start_angle16;
  uint16_t reserved0;
  uint32_t word_0x2c_raw;
  fzgx_vec3 end_pos;
  fzgx_vec3 end_interest;
  float end_perspective;
  int16_t end_angle16;
  int16_t transition_kind;
} fzgx_game_camera_cycle_preset;

typedef struct fzgx_game_camera_local_shot_preset {
  fzgx_vec3 local_position;
  fzgx_vec3 local_interest;
} fzgx_game_camera_local_shot_preset;

typedef struct fzgx_sincos_result {
  float sin_value;
  float cos_value;
} fzgx_sincos_result;

#include "../../catalog/game_camera_content.inc"

enum {
  FZGX_GAME_CAMERA_USER_ZOOM_MIN = FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON,
  FZGX_GAME_CAMERA_USER_ZOOM_MAX = FZGX_GAME_CAMERA_ZOOM_FAR,
  FZGX_GAME_CAMERA_PRESET_LOCAL_ZOOM_MAX = 11u,
  FZGX_GAME_CAMERA_TRACK_FLAG_SPECIAL_FOLLOW = 0x01000000u,
  FZGX_GAME_CAMERA_TRACK_FLAG_SPECIAL_FOLLOW_FAMILY = 0x01800000u,
  FZGX_GAME_CAMERA_MODE_STORY = 9u,
  FZGX_MS_B1 = 0x00000001u,
  FZGX_MS_AIRBORNE = 0x00000002u,
  FZGX_MS_BOOSTING = 0x00000020u,
  FZGX_MS_JUSTPRESSEDBOOST = 0x00000040u,
  FZGX_MS_0HP = 0x00000080u,
  FZGX_MS_B9 = 0x00000100u,
  FZGX_MS_FALLOUT = 0x00000800u,
  FZGX_MS_COMPLETEDRACE_1_Q = 0x00010000u,
  FZGX_MS_B23 = 0x00400000u,
};

static const float FLOAT_80303808 = 1.1f;
static const float FLOAT_80303810 = 1.0f;
static const float FLOAT_80303818 = 0.0f;
static const float FLOAT_80303830 = 55.0f;
static const float FLOAT_80303834 = 0.0f;
static const float FLOAT_80303838 = -10.0f;
static const double DOUBLE_8030383c = 0.5;
static const float FLOAT_8030384c = 0.0f;
static const float FLOAT_80303850 = 1.0f;
static const float FLOAT_80303854 = 0.0f;
static const float FLOAT_80303894 = 1500.0f;
static const float FLOAT_80303898 = 53.0f;
static const float FLOAT_8030389c = 3.0f;
static const float FLOAT_803038a0 = 0.01f;
static const double DOUBLE_803038a4 = 0.05;
static const float FLOAT_803038ac = 0.00004f;
static const float FLOAT_803038b0 = 80.0f;
static const float FLOAT_803038b4 = 108.0f;
static const float FLOAT_803038b8 = 1.35f;
static const float FLOAT_803038bc = 90.0f;
static const float FLOAT_803038c0 = 1.2f;
static const float FLOAT_803038c4 = 0.2f;
static const double DOUBLE_803038cc = 0.01;
static const float FLOAT_803038d4 = 0.05f;
static const float FLOAT_803038d8 = 0.1f;
static const double DOUBLE_803038dc = 0.15;
static const float FLOAT_803038e4 = 1.0f;
static const double DOUBLE_803038f4 = 0.12;
static const float FLOAT_5000 = 5000.0f;
static const double DOUBLE_80303904 = 1.2;
static const double DOUBLE_8030390c = 1.4;
static const double DOUBLE_80303914 = 1.7;
static const double DOUBLE_8030391c = 0.08;
static const double DOUBLE_80303924 = 0.2;
static const double DOUBLE_8030392c = 0.04;
static const float FLOAT_80303934 = 0.5f;
static const double DOUBLE_8030393c = 0.1;
static const float FLOAT_80303944 = 1.5f;
static const float FLOAT_80303948 = 2.3f;
static const float FLOAT_8030394c = 0.7f;
static const double DOUBLE_80303954 = 0.3;
static const double DOUBLE_8030395c = 55.0;
static const double DOUBLE_80303964 = 150.0;
static const float FLOAT_8030396c = 65.0f;
static const float FLOAT_80303970 = 0.00023668639187235385f;
static const float FLOAT_80303974 = 60.0f;
static const float FLOAT_80303978 = 0.00027777778450399637f;
static const double DOUBLE_803039e4 = 1.0;
static const double DOUBLE_803039ec = 0.75;
static const double DOUBLE_803039f4 = 0.8;
static const double DOUBLE_803039fc = 0.72;
static const float FZGX_DOL_G_CAMERA_PARAMETER = 0.3f;

static const fzgx_vec3 fzgx_game_camera_world_up = {0.0f, FLOAT_80303810, 0.0f};

static const fzgx_game_camera_local_shot_preset fzgx_game_camera_local_shot_presets[] = {
    {{0.5f, 1.5f, 5.0f}, {0.5f, 1.5f, 0.0f}},
    {{-0.5f, 1.5f, 5.0f}, {-0.5f, 1.5f, 0.0f}},
    {{4.0f, 10.0f, -13.0f}, {0.0f, 0.2f, 13.0f}},
    {{-4.0f, 10.0f, -13.0f}, {0.0f, 0.2f, 13.0f}},
    {{4.0f, 0.5f, 5.0f}, {0.0f, 0.9f, 0.0f}},
    {{-4.0f, 0.5f, 5.0f}, {0.0f, 0.9f, 0.0f}},
    {{8.0f, 3.0f, -5.0f}, {0.0f, 2.0f, 0.0f}},
    {{-8.0f, 3.0f, -5.0f}, {0.0f, 2.0f, 0.0f}},
    {{-2.5f, 2.0f, 8.0f}, {-1.0f, 1.8f, -1.0f}},
    {{2.5f, 2.0f, 8.0f}, {1.0f, 1.8f, -1.0f}},
    {{-1.5f, 2.0f, -3.8f}, {0.2f, 1.6f, 0.0f}},
    {{1.5f, 2.0f, -3.8f}, {-0.2f, 1.6f, 0.0f}},
};

static fzgx_vec3 fzgx_vec3_add(fzgx_vec3 a, fzgx_vec3 b) {
  return (fzgx_vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static fzgx_vec3 fzgx_vec3_sub(fzgx_vec3 a, fzgx_vec3 b) {
  return (fzgx_vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static fzgx_vec3 fzgx_vec3_scale(fzgx_vec3 value, float scale) {
  return (fzgx_vec3){value.x * scale, value.y * scale, value.z * scale};
}

static float fzgx_vec3_dot(fzgx_vec3 a, fzgx_vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static fzgx_vec3 fzgx_vec3_cross(fzgx_vec3 a, fzgx_vec3 b) {
  return (fzgx_vec3){
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

static float fzgx_vec3_length(fzgx_vec3 value) {
  return sqrtf(fzgx_vec3_dot(value, value));
}

static fzgx_vec3 fzgx_vec3_normalize_or_exact(fzgx_vec3 value, fzgx_vec3 fallback) {
  float length = fzgx_vec3_length(value);

  if (!(length > FLT_EPSILON)) {
    return fallback;
  }
  return fzgx_vec3_scale(value, 1.0f / length);
}

static fzgx_vec3 fzgx_vec3_lerp_exact(fzgx_vec3 a, fzgx_vec3 b, float t) {
  return (fzgx_vec3){
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t,
  };
}

static fzgx_vec3 fzgx_vec3_project_onto_plane_exact(fzgx_vec3 value, fzgx_vec3 normal) {
  return fzgx_vec3_sub(value, fzgx_vec3_scale(normal, fzgx_vec3_dot(value, normal)));
}

static float fzgx_clamp_exact(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static int16_t fzgx_clamp_s16_exact(int16_t value, int16_t min_value, int16_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static bool fzgx_vec3_is_finite_exact(fzgx_vec3 value) {
  return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool fzgx_mat43_is_finite_exact(const fzgx_mat43 *transform) {
  if (transform == 0) {
    return false;
  }
  return fzgx_vec3_is_finite_exact(fzgx_mat43_get_basis_x_exact(transform)) &&
         fzgx_vec3_is_finite_exact(fzgx_mat43_get_basis_y_exact(transform)) &&
         fzgx_vec3_is_finite_exact(fzgx_mat43_get_basis_z_exact(transform)) &&
         fzgx_vec3_is_finite_exact(fzgx_mat43_get_origin_exact(transform));
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

static float fzgx_math_rsqrt_exact(float value) {
  if (!(value > 0.0f)) {
    return INFINITY;
  }
  return 1.0f / sqrtf(value);
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

static void fzgx_mat43_rotate_x_sin_cos_right_exact(
    fzgx_mat43 *transform,
    float sin_value,
    float cos_value) {
  float basis_y_x = transform->basis_y_x;
  float basis_y_y = transform->basis_y_y;
  float basis_y_z = transform->basis_y_z;
  float basis_z_x = transform->basis_z_x;
  float basis_z_y = transform->basis_z_y;
  float basis_z_z = transform->basis_z_z;

  transform->basis_y_x = basis_y_x * cos_value + basis_z_x * sin_value;
  transform->basis_y_y = basis_y_y * cos_value + basis_z_y * sin_value;
  transform->basis_y_z = basis_y_z * cos_value + basis_z_z * sin_value;
  transform->basis_z_x = basis_y_x * -sin_value + basis_z_x * cos_value;
  transform->basis_z_y = basis_y_y * -sin_value + basis_z_y * cos_value;
  transform->basis_z_z = basis_y_z * -sin_value + basis_z_z * cos_value;
}

static fzgx_mat43 fzgx_mat43_identity_exact(void) {
  fzgx_mat43 transform;

  memset(&transform, 0, sizeof(transform));
  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  return transform;
}

static fzgx_vec3 fzgx_transform_local_vector(
    const fzgx_mat43 *transform,
    fzgx_vec3 local_vector) {
  fzgx_vec3 world;

  world.x = transform->basis_z_x * local_vector.z + transform->basis_x_x * local_vector.x +
            transform->basis_y_x * local_vector.y;
  world.y = transform->basis_z_y * local_vector.z + transform->basis_x_y * local_vector.x +
            transform->basis_y_y * local_vector.y;
  world.z = transform->basis_z_z * local_vector.z + transform->basis_x_z * local_vector.x +
            transform->basis_y_z * local_vector.y;
  return world;
}

static fzgx_vec3 fzgx_transform_local_point(
    const fzgx_mat43 *transform,
    fzgx_vec3 local_point) {
  fzgx_vec3 world;

  world.x = transform->basis_z_x * local_point.z + transform->basis_x_x * local_point.x +
            transform->origin_x * 1.0f + transform->basis_y_x * local_point.y;
  world.y = transform->basis_z_y * local_point.z + transform->basis_x_y * local_point.x +
            transform->origin_y * 1.0f + transform->basis_y_y * local_point.y;
  world.z = transform->basis_z_z * local_point.z + transform->basis_x_z * local_point.x +
            transform->origin_z * 1.0f + transform->basis_y_z * local_point.y;
  return world;
}

static fzgx_vec3 fzgx_world_vector_to_local(
    const fzgx_mat43 *transform,
    fzgx_vec3 world_vector) {
  fzgx_vec3 local;

  local.x = transform->basis_x_y * world_vector.y + transform->basis_x_x * world_vector.x;
  local.y = transform->basis_y_y * world_vector.y + transform->basis_y_x * world_vector.x;
  local.z = transform->basis_z_y * world_vector.y + transform->basis_z_x * world_vector.x;
  local.x = transform->basis_x_z * world_vector.z + local.x;
  local.y = transform->basis_y_z * world_vector.z + local.y;
  local.z = transform->basis_z_z * world_vector.z + local.z;
  return local;
}

static fzgx_vec3 fzgx_world_point_to_local(
    const fzgx_mat43 *transform,
    fzgx_vec3 world_point) {
  world_point.x -= transform->origin_x;
  world_point.y -= transform->origin_y;
  world_point.z -= transform->origin_z;
  return fzgx_world_vector_to_local(transform, world_point);
}

static void fzgx_mat43_translate_neg_vec3_right_exact(
    fzgx_mat43 *transform,
    const fzgx_vec3 *value) {
  transform->origin_x =
      transform->basis_z_x * -value->z + transform->basis_x_x * -value->x +
      transform->origin_x * 1.0f + transform->basis_y_x * -value->y;
  transform->origin_y =
      transform->basis_z_y * -value->z + transform->basis_x_y * -value->x +
      transform->origin_y * 1.0f + transform->basis_y_y * -value->y;
  transform->origin_z =
      transform->basis_z_z * -value->z + transform->basis_x_z * -value->x +
      transform->origin_z * 1.0f + transform->basis_y_z * -value->y;
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

  if (transform == 0) {
    return;
  }

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
    fVar6 = sqrtf(1.0f + (matrix[iVar4 * 5] - (matrix[iVar2 * 5] + matrix[iVar3 * 5])));
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
    fVar5 = sqrtf(1.0f + fVar6);
    fVar6 = 0.5f / fVar5;
    quat->w = 0.5f * fVar5;
    quat->x = fVar6 * (matrix[9] - matrix[6]);
    quat->y = fVar6 * (matrix[2] - matrix[8]);
    quat->z = fVar6 * (matrix[4] - matrix[1]);
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

static void fzgx_ray_scale_exact(
    float scale,
    const fzgx_vec3 *start,
    const fzgx_vec3 *end,
    fzgx_vec3 *out) {
  if ((start == 0) || (end == 0) || (out == 0)) {
    return;
  }
  *out = fzgx_vec3_lerp_exact(*start, *end, scale);
}

static uint16_t fzgx_math_atan2_angle16(float y, float x) {
  float angle = atan2f(y, x) * (65536.0f / (2.0f * 3.14159265358979323846f));
  return (uint16_t)((int)lroundf(angle) & 0xffff);
}

static void fzgx_game_camera_vec_to_euler(
    fzgx_vec3 vector,
    int16_t *pitch_out,
    int16_t *yaw_out) {
  float x = -vector.x;
  float y = vector.y;
  float z = -vector.z;
  float horizontal = sqrtf(x * x + z * z);

  if (pitch_out != 0) {
    *pitch_out = (int16_t)fzgx_math_atan2_angle16(y, horizontal);
  }
  if (yaw_out != 0) {
    *yaw_out = (int16_t)fzgx_math_atan2_angle16(x, z);
  }
}

static fzgx_vec3 fzgx_game_camera_euler_to_vector(
    float scale,
    int16_t pitch_angle16,
    int16_t yaw_angle16) {
  fzgx_sincos_result pitch = fzgx_math_sincos_14b((uint16_t)pitch_angle16);
  fzgx_sincos_result yaw = fzgx_math_sincos_14b((uint16_t)yaw_angle16);

  return (fzgx_vec3){
      -scale * yaw.sin_value * pitch.cos_value,
      scale * pitch.sin_value,
      -scale * yaw.cos_value * pitch.cos_value,
  };
}

static fzgx_vec3 fzgx_rotate_about_axis_exact(
    fzgx_vec3 vector,
    fzgx_vec3 axis,
    int16_t angle16) {
  fzgx_sincos_result sincos = fzgx_math_sincos_14b((uint16_t)angle16);
  fzgx_vec3 axis_normalized = fzgx_vec3_normalize_or_exact(axis, fzgx_game_camera_world_up);
  fzgx_vec3 term1 = fzgx_vec3_scale(vector, sincos.cos_value);
  fzgx_vec3 term2 = fzgx_vec3_scale(fzgx_vec3_cross(axis_normalized, vector), sincos.sin_value);
  fzgx_vec3 term3 = fzgx_vec3_scale(
      axis_normalized,
      fzgx_vec3_dot(axis_normalized, vector) * (1.0f - sincos.cos_value));
  return fzgx_vec3_add(fzgx_vec3_add(term1, term2), term3);
}

static fzgx_mat43 fzgx_game_camera_build_direction_basis(
    fzgx_vec3 origin,
    fzgx_vec3 up_hint,
    fzgx_vec3 direction,
    const fzgx_mat43 *fallback) {
  fzgx_mat43 transform;
  fzgx_vec3 basis_z = fzgx_vec3_scale(direction, -1.0f);
  fzgx_vec3 basis_x;
  fzgx_vec3 basis_y;
  float length;

  (void)fallback;

  memset(&transform, 0, sizeof(transform));
  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  fzgx_mat43_set_origin_exact(&transform, origin);

  length = fzgx_vec3_length(basis_z);
  if (!(length > FLT_EPSILON)) {
    return transform;
  }
  basis_z = fzgx_vec3_scale(basis_z, 1.0f / length);

  basis_x = fzgx_vec3_cross(up_hint, basis_z);
  length = fzgx_vec3_length(basis_x);
  if (!(length > FLT_EPSILON)) {
    return transform;
  }
  basis_x = fzgx_vec3_scale(basis_x, 1.0f / length);

  basis_y = fzgx_vec3_cross(basis_z, basis_x);
  length = fzgx_vec3_length(basis_y);
  if (!(length > FLT_EPSILON)) {
    return transform;
  }
  basis_y = fzgx_vec3_scale(basis_y, 1.0f / length);

  fzgx_mat43_set_basis_x_exact(&transform, basis_x);
  fzgx_mat43_set_basis_y_exact(&transform, basis_y);
  fzgx_mat43_set_basis_z_exact(&transform, basis_z);
  return transform;
}

static void fzgx_game_camera_build_view_matrix_exact(
    const fzgx_vec3 *position,
    const fzgx_vec3 *up_vector,
    const fzgx_vec3 *interest,
    fzgx_mat43 *view_matrix_out) {
  fzgx_vec3 backward;
  fzgx_vec3 right;
  fzgx_vec3 corrected_up;
  float length;

  if ((position == 0) || (up_vector == 0) || (interest == 0) || (view_matrix_out == 0)) {
    return;
  }

  backward = fzgx_vec3_sub(*position, *interest);
  length = fzgx_vec3_length(backward);
  if (!(length > FLT_EPSILON)) {
    *view_matrix_out = fzgx_mat43_identity_exact();
    fzgx_mat43_set_origin_exact(view_matrix_out, *position);
    return;
  }
  backward = fzgx_vec3_scale(backward, 1.0f / length);

  right = (fzgx_vec3){
      -(up_vector->z * backward.y - up_vector->y * backward.z),
      -(up_vector->x * backward.z - up_vector->z * backward.x),
      -(up_vector->y * backward.x - up_vector->x * backward.y),
  };
  length = fzgx_vec3_length(right);
  if (!(length > FLT_EPSILON)) {
    *view_matrix_out = fzgx_mat43_identity_exact();
    fzgx_mat43_set_origin_exact(view_matrix_out, *position);
    return;
  }
  right = fzgx_vec3_scale(right, 1.0f / length);

  corrected_up = (fzgx_vec3){
      -(backward.z * right.y - backward.y * right.z),
      -(backward.x * right.z - backward.z * right.x),
      -(backward.y * right.x - backward.x * right.y),
  };
  length = fzgx_vec3_length(corrected_up);
  if (!(length > FLT_EPSILON)) {
    *view_matrix_out = fzgx_mat43_identity_exact();
    fzgx_mat43_set_origin_exact(view_matrix_out, *position);
    return;
  }
  corrected_up = fzgx_vec3_scale(corrected_up, 1.0f / length);

  *view_matrix_out = fzgx_mat43_identity_exact();
  fzgx_mat43_set_basis_x_exact(view_matrix_out, right);
  fzgx_mat43_set_basis_y_exact(view_matrix_out, corrected_up);
  fzgx_mat43_set_basis_z_exact(view_matrix_out, backward);
  fzgx_mat43_set_origin_exact(view_matrix_out, *position);
  fzgx_mat43_rigid_invert_exact(view_matrix_out);
}

static void fzgx_game_camera_build_rolled_view_matrix_exact(
    const fzgx_vec3 *position,
    const fzgx_vec3 *interest,
    int16_t roll_angle16,
    fzgx_mat43 *view_matrix_out) {
  int16_t pitch_angle16;
  int16_t yaw_angle16;
  fzgx_vec3 look_vector;

  if ((position == 0) || (interest == 0) || (view_matrix_out == 0)) {
    return;
  }

  look_vector = fzgx_vec3_sub(*interest, *position);
  fzgx_game_camera_vec_to_euler(look_vector, &pitch_angle16, &yaw_angle16);

  *view_matrix_out = fzgx_mat43_identity_exact();
  fzgx_mat43_rotate_about_z_right(view_matrix_out, (uint16_t)(-(int32_t)roll_angle16));
  fzgx_mat43_rotate_about_x_right(view_matrix_out, (uint16_t)(-(int32_t)pitch_angle16));
  fzgx_mat43_rotate_about_y_right(view_matrix_out, (uint16_t)(-(int32_t)yaw_angle16));
  fzgx_mat43_translate_neg_vec3_right_exact(view_matrix_out, position);
}

static void fzgx_game_camera_rebuild_view_matrix_exact(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine) {
  if ((camera == 0) || (machine == 0)) {
    return;
  }

  camera->previous_view_matrix = camera->view_matrix;
  if (camera->behavior_state == FZGX_GAME_CAMERA_BEHAVIOR_CYCLE) {
    int16_t pitch_angle16;
    int16_t yaw_angle16;
    fzgx_sincos_result pitch_sincos;
    fzgx_sincos_result yaw_sincos;
    fzgx_mat43 pitch_yaw_basis;
    fzgx_vec3 track_normal;
    fzgx_vec3 local_track_normal;
    int16_t track_normal_roll_angle16;

    fzgx_game_camera_vec_to_euler(
        fzgx_vec3_sub(camera->interest, camera->position),
        &pitch_angle16,
        &yaw_angle16);
    pitch_sincos = fzgx_math_sincos_14b((uint16_t)pitch_angle16);
    yaw_sincos = fzgx_math_sincos_14b((uint16_t)yaw_angle16);
    pitch_yaw_basis = fzgx_mat43_identity_exact();
    pitch_yaw_basis.basis_x_x = yaw_sincos.cos_value;
    pitch_yaw_basis.basis_z_x = yaw_sincos.sin_value;
    pitch_yaw_basis.basis_x_z = -yaw_sincos.sin_value;
    pitch_yaw_basis.basis_z_z = yaw_sincos.cos_value;
    fzgx_mat43_rotate_x_sin_cos_right_exact(
        &pitch_yaw_basis, pitch_sincos.sin_value, pitch_sincos.cos_value);
    track_normal = fzgx_vec3_normalize_or_exact(machine->track_state.track_up, camera->up);
    local_track_normal = fzgx_world_vector_to_local(&pitch_yaw_basis, track_normal);
    track_normal_roll_angle16 =
        (int16_t)fzgx_math_atan2_angle16(local_track_normal.x, local_track_normal.y);
    fzgx_game_camera_build_rolled_view_matrix_exact(
        &camera->position,
        &camera->interest,
        (int16_t)(camera->sequence_angle16 - track_normal_roll_angle16),
        &camera->view_matrix);
    return;
  }

  fzgx_game_camera_build_view_matrix_exact(
      &camera->position, &camera->up, &camera->interest, &camera->view_matrix);
}

static const fzgx_machine_snapshot *fzgx_game_camera_get_machine(
    const fzgx_sim_world *world,
    uint32_t machine_index) {
  if ((world == 0) || (machine_index >= world->machine_count) ||
      (machine_index >= FZGX_SIM_MAX_MACHINES)) {
    return 0;
  }
  return &world->machines[machine_index];
}

static bool fzgx_game_camera_has_special_track_follow(const fzgx_machine_snapshot *machine) {
  return (machine != 0) &&
         ((machine->track_state.flags & FZGX_GAME_CAMERA_TRACK_FLAG_SPECIAL_FOLLOW) != 0u);
}

static bool fzgx_game_camera_has_any_camera_corner_flag(const fzgx_machine_snapshot *machine) {
  size_t i;

  if (machine == 0) {
    return false;
  }
  for (i = 0u; i < 4u; ++i) {
    if ((machine->suspension_state[i] & 4u) != 0u) {
      return true;
    }
  }
  return false;
}

static bool fzgx_game_camera_uses_wheel_style_basis(const fzgx_machine_snapshot *machine) {
  if (machine == 0) {
    return false;
  }
  /* Raw code queries a controller-profile table via FUN_801aad70. The snapshot
   * only exposes the resolved control kind, and 3/4 are the proven wheel-style
   * paths in fzgx_sim. */
  return (machine->control_profile_kind == 3u) || (machine->control_profile_kind == 4u);
}

static bool fzgx_game_camera_uses_wide_display_mode(
    const fzgx_game_camera_runtime *camera) {
  if (camera == 0) {
    return false;
  }
  if ((camera->display_mode_kind == 1u) || (camera->display_mode_kind == 2u)) {
    return true;
  }
  if (camera->display_mode_kind != 0xffu) {
    return false;
  }
  return camera->aspect_ratio > (float)DOUBLE_8030390c;
}

static float fzgx_game_camera_get_camera_parameter(
    const fzgx_game_camera_runtime *camera) {
  if ((camera != 0) && isfinite(camera->camera_parameter) &&
      (camera->camera_parameter > 0.0f)) {
    return camera->camera_parameter;
  }
  /* `fz::g_camera_parameter` is DOL-owned rather than REL-owned. Keep it as
   * an explicit seam here until the DOL symbol/address is extracted into the
   * workbench. */
  return FZGX_DOL_G_CAMERA_PARAMETER;
}

static uint8_t fzgx_game_camera_get_camera_manager_mode(
    const fzgx_game_camera_runtime *camera) {
  return (camera != 0) ? camera->camera_manager_mode : 0xffu;
}

static bool fzgx_game_camera_track_up_override_active(void) {
  /* FUN_8022cf0c reads machine->unk_short_0x188, which is not surfaced on the
   * current snapshot seam yet. REL-side evidence currently proves only the
   * getter and reset-to-zero path, so keep the default camera-owned
   * reorientation path until a real writer/source for that field is mapped. */
  return false;
}

static void fzgx_game_camera_load_follow_preset_triplet(
    const fzgx_game_camera_runtime *camera,
    fzgx_game_camera_follow_preset_triplet *preset_out) {
  size_t index;
  float widescreen_height_offset;

  if ((camera == 0) || (preset_out == 0)) {
    return;
  }

  index = (size_t)((camera->zoom_mode >= 0) ? camera->zoom_mode : FZGX_GAME_CAMERA_ZOOM_CLOSE);
  if (index >= (sizeof(fzgx_generated_game_camera_follow_presets) /
                sizeof(fzgx_generated_game_camera_follow_presets[0]))) {
    index = FZGX_GAME_CAMERA_ZOOM_CLOSE;
  }
  /* The raw first-person path can override the shared preset with per-machine
   * cockpit offsets from entrant content. That content seam is still separate,
   * so the native port falls back to the shared table for now. */
  *preset_out = fzgx_generated_game_camera_follow_presets[index];

  if (!fzgx_game_camera_uses_wide_display_mode(camera)) {
    return;
  }

  widescreen_height_offset = FLOAT_80303808;
  if ((camera->zoom_mode != FZGX_GAME_CAMERA_ZOOM_MEDIUM) &&
      (camera->zoom_mode != FZGX_GAME_CAMERA_ZOOM_FAR)) {
    widescreen_height_offset = fzgx_game_camera_get_camera_parameter(camera);
  }
  preset_out->triplets[0].local_height -= widescreen_height_offset;
  preset_out->triplets[1].local_height -= widescreen_height_offset;
  preset_out->triplets[2].local_height -= widescreen_height_offset;
}

static float fzgx_game_camera_sample_vertical_clearance_ratio(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_machine_snapshot *machine) {
  fzgx_sim_world *world_mutable;
  fzgx_world_spherecast_request request;
  fzgx_world_spherecast_result result;
  fzgx_mat43 machine_transform;
  fzgx_vec3 probe_start_local = {0.0f, -13.0f, -20.0f};
  fzgx_vec3 probe_end_local = {0.0f, 13.0f, -20.0f};
  fzgx_vec3 probe_world;
  fzgx_status status;
  double clamped_clearance;
  float ratio;
  size_t i;

  if ((camera == 0) || (world == 0) || (machine == 0)) {
    return 0.0f;
  }

  world_mutable = (fzgx_sim_world *)world;
  machine_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&machine_transform, machine->position);

  memset(&request, 0, sizeof(request));
  memset(&result, 0, sizeof(result));
  request.start = fzgx_transform_local_point(&machine_transform, probe_start_local);
  request.end = fzgx_transform_local_point(&machine_transform, probe_end_local);
  request.flags = 0x80001u;
  request.machine_index = camera->machine_index;
  request.checkpoint_seed_index = machine->track_state.cur_cp_pointer;
  request.checkpoint_seed_aux = machine->track_state.cur_cp_idx;
  request.checkpoint_history_count = 4u;
  for (i = 0u; i < 4u; ++i) {
    request.checkpoint_history_index[i] = machine->track_state.cp_hist_idx[i];
    request.checkpoint_history_fraction[i] = machine->track_state.cp_hist_frac[i];
  }

  status = fzgx_sim_world_debug_exact_spherecast(
      world_mutable,
      &request,
      &camera->vertical_probe_piece_scratch,
      &result);
  if ((status == FZGX_STATUS_OK) && result.has_hit) {
    probe_world = result.hit_point;
  } else {
    probe_world = request.start;
  }

  probe_world = fzgx_world_point_to_local(&machine_transform, probe_world);
  clamped_clearance = DOUBLE_80303904 + (double)probe_world.y;
  if (clamped_clearance < 0.0) {
    clamped_clearance = 0.0;
  }
  if (clamped_clearance > (double)FLOAT_8030389c) {
    clamped_clearance = (double)FLOAT_8030389c;
  }
  ratio = (float)(clamped_clearance / (double)FLOAT_8030389c);
  if ((machine->track_state.flags & FZGX_GAME_CAMERA_TRACK_FLAG_SPECIAL_FOLLOW) != 0u) {
    ratio = fzgx_game_camera_get_camera_parameter(camera);
  }

  camera->clearance_ratio +=
      (float)DOUBLE_8030391c * (ratio - camera->clearance_ratio);
  return ratio;
}

static void fzgx_game_camera_update_behavior_state_from_machine(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_machine_snapshot *machine) {
  uint32_t machine_state;

  if ((camera == 0) || (machine == 0)) {
    return;
  }

  machine_state = machine->machine_state;
  if ((world != 0) && (world->race_mode != FZGX_GAME_CAMERA_MODE_STORY) &&
      ((machine_state & FZGX_MS_COMPLETEDRACE_1_Q) != 0u)) {
    camera->behavior_state = FZGX_GAME_CAMERA_BEHAVIOR_CYCLE;
    return;
  }

  if ((machine_state & FZGX_MS_B1) == 0u) {
    if ((machine_state & FZGX_MS_FALLOUT) != 0u) {
      camera->behavior_state = FZGX_GAME_CAMERA_BEHAVIOR_FALLOUT;
      return;
    }
    if ((machine_state & FZGX_MS_0HP) != 0u) {
      return;
    }
  }

  if (machine->frames_until_restored == (uint16_t)DOUBLE_80303964) {
    camera->behavior_state = FZGX_GAME_CAMERA_BEHAVIOR_RESTORE;
  } else if (machine->frames_until_restored == 20u) {
    camera->behavior_state = FZGX_GAME_CAMERA_BEHAVIOR_NORMAL;
  }
}

static void fzgx_game_camera_update_zoom_mode_from_input(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine,
    const fzgx_game_camera_input *input) {
  int zoom_mode;
  int zoom_min;
  int zoom_max;

  if ((camera == 0) || (machine == 0)) {
    return;
  }

  if (machine->frames_until_restored > 1u) {
    camera->zoom_mode = FZGX_GAME_CAMERA_ZOOM_RESTORE;
    return;
  }
  if (machine->frames_until_restored == 1u) {
    if ((camera->saved_zoom_mode < FZGX_GAME_CAMERA_USER_ZOOM_MIN) ||
        (camera->saved_zoom_mode > FZGX_GAME_CAMERA_USER_ZOOM_MAX)) {
      camera->saved_zoom_mode = FZGX_GAME_CAMERA_ZOOM_CLOSE;
    }
    camera->zoom_mode = camera->saved_zoom_mode;
    camera->persistent_saved_zoom_mode = (uint8_t)camera->saved_zoom_mode;
    return;
  }
  if ((machine->machine_state & FZGX_MS_0HP) != 0u) {
    camera->zoom_mode = FZGX_GAME_CAMERA_ZOOM_RESTORE;
    return;
  }

  zoom_mode = camera->zoom_mode;
  if ((zoom_mode < FZGX_GAME_CAMERA_USER_ZOOM_MIN) ||
      (zoom_mode > FZGX_GAME_CAMERA_USER_ZOOM_MAX)) {
    zoom_mode = camera->saved_zoom_mode;
  }
  if ((zoom_mode < FZGX_GAME_CAMERA_USER_ZOOM_MIN) ||
      (zoom_mode > FZGX_GAME_CAMERA_USER_ZOOM_MAX)) {
    zoom_mode = FZGX_GAME_CAMERA_ZOOM_CLOSE;
  }

  zoom_min = FZGX_GAME_CAMERA_USER_ZOOM_MIN;
  zoom_max = FZGX_GAME_CAMERA_USER_ZOOM_MAX;
  if (camera->behavior_state == FZGX_GAME_CAMERA_BEHAVIOR_PRESET_LOCAL) {
    zoom_min = 0;
    zoom_max = (int)FZGX_GAME_CAMERA_PRESET_LOCAL_ZOOM_MAX;
  }

  if (input != 0) {
    if (input->view_down_pressed != 0u) {
      zoom_mode += 1;
    }
    if (input->view_up_pressed != 0u) {
      zoom_mode -= 1;
    }
  }

  if (zoom_mode < zoom_min) {
    zoom_mode = zoom_min;
  } else if (zoom_mode > zoom_max) {
    zoom_mode = zoom_max;
  }

  camera->zoom_mode = (int16_t)zoom_mode;
  if ((zoom_mode >= FZGX_GAME_CAMERA_USER_ZOOM_MIN) &&
      (zoom_mode <= FZGX_GAME_CAMERA_USER_ZOOM_MAX)) {
    camera->saved_zoom_mode = (int16_t)zoom_mode;
    camera->persistent_saved_zoom_mode = (uint8_t)zoom_mode;
  }
}

static void fzgx_game_camera_recompute_follow_point_and_interest(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine) {
  fzgx_mat43 camera_transform;

  if ((camera == 0) || (machine == 0)) {
    return;
  }

  camera_transform = camera->follow_basis;
  fzgx_mat43_set_origin_exact(&camera_transform, machine->position);
  camera->position = fzgx_transform_local_point(&camera_transform, camera->local_follow_offset);

  fzgx_mat43_set_origin_exact(&camera_transform, camera->position);
  fzgx_mat43_rotate_about_x_right(&camera_transform, (uint16_t)camera->pitch_angle16);
  camera->interest = fzgx_vec3_add(
      fzgx_mat43_get_origin_exact(&camera_transform),
      fzgx_vec3_scale(fzgx_mat43_get_basis_z_exact(&camera_transform), FLOAT_80303838));
}

static void fzgx_game_camera_update_normal_follow(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_machine_snapshot *machine,
    const fzgx_game_camera_input *input) {
  fzgx_game_camera_follow_preset_triplet preset;
  fzgx_vec3 displacement;
  fzgx_mat43 target_basis;
  fzgx_mat43 machine_transform;
  fzgx_vec3 target_up;
  float speed_kmh;
  float speed_ratio;
  float target_perspective;
  float target_local_y;
  float target_local_z;
  float target_pitch;
  float perspective_step;
  float camera_parameter;
  float reorient_scale;
  float reposition_scale;
  float offset_blend;
  int16_t clamped_pitch;
  int16_t clamped_yaw;
  int16_t target_pitch_angle16;
  uint8_t camera_manager_mode;
  bool wheel_style;
  bool any_camera_corner_flag;
  bool track_up_override;
  bool special_track_follow;
  bool special_negative_tuck;

  if ((camera == 0) || (machine == 0)) {
    return;
  }

  camera->cycle_initialized = 0u;
  fzgx_game_camera_update_zoom_mode_from_input(camera, machine, input);
  fzgx_game_camera_load_follow_preset_triplet(camera, &preset);
  camera_manager_mode = fzgx_game_camera_get_camera_manager_mode(camera);
  camera_parameter = fzgx_game_camera_get_camera_parameter(camera);

  speed_kmh = fzgx_clamp_exact(machine->speed_kmh, FLOAT_80303834, FLOAT_80303894);
  if (!isfinite(speed_kmh)) {
    speed_kmh = 0.0f;
  }
  speed_ratio = speed_kmh / FLOAT_80303894;
  target_perspective = FLOAT_80303898 * speed_ratio * speed_ratio;
  if (camera_manager_mode == 1u) {
    target_perspective /= FLOAT_8030389c;
  }
  target_perspective += FLOAT_80303830;
  target_local_y =
      preset.triplets[0].local_height +
      speed_ratio * (preset.triplets[1].local_height - preset.triplets[0].local_height);
  target_local_z =
      preset.triplets[0].local_distance +
      speed_ratio * (preset.triplets[1].local_distance - preset.triplets[0].local_distance);
  target_pitch =
      (float)preset.triplets[0].pitch_angle16 +
      speed_ratio * (float)((int32_t)preset.triplets[1].pitch_angle16 -
                            (int32_t)preset.triplets[0].pitch_angle16);
  target_pitch_angle16 = (int16_t)target_pitch;

  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FAR) {
    target_local_y = preset.triplets[0].local_height;
    target_local_z = preset.triplets[0].local_distance;
    target_perspective = FLOAT_80303830;
    target_pitch_angle16 = preset.triplets[0].pitch_angle16;
  }

  if (camera->zoom_mode < FZGX_GAME_CAMERA_ZOOM_RESTORE) {
    (void)fzgx_game_camera_sample_vertical_clearance_ratio(camera, world, machine);
    target_local_y +=
        camera->clearance_ratio * (preset.triplets[2].local_height - target_local_y);
    target_local_z +=
        camera->clearance_ratio * (preset.triplets[2].local_distance - target_local_z);
    target_pitch_angle16 =
        (int16_t)((float)target_pitch_angle16 +
                  (float)((int32_t)preset.triplets[2].pitch_angle16 -
                          (int32_t)target_pitch_angle16) *
                      camera->clearance_ratio * (float)DOUBLE_8030383c);
  }

  perspective_step = (float)(DOUBLE_803038a4 - (double)(FLOAT_803038ac * speed_kmh));
  perspective_step =
      fzgx_clamp_exact(perspective_step, FLOAT_803038a0, (float)DOUBLE_803038a4);

  if ((machine->machine_state & (FZGX_MS_JUSTPRESSEDBOOST | FZGX_MS_B23)) == 0u) {
    if (((machine->machine_state & FZGX_MS_BOOSTING) == 0u) ||
        (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FAR)) {
      if (camera->perspective_transition_counter > 0) {
        camera->perspective_transition_counter -= 1;
      }
      if (camera->perspective_transition_counter < 11) {
        camera->perspective += perspective_step * (target_perspective - camera->perspective);
      } else {
        if (speed_kmh <= camera->previous_speed_kmh) {
          camera->perspective +=
              (float)DOUBLE_803038cc * (target_perspective - camera->perspective);
        } else {
          camera->perspective +=
              FLOAT_803038c4 *
              (camera->boost_perspective_target - camera->perspective);
        }
        camera->previous_speed_kmh = speed_kmh;
      }
    } else {
      if (camera->perspective_transition_counter < 20) {
        camera->perspective_transition_counter += 1;
      }
      if (camera->perspective_transition_counter < 11) {
        if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_MEDIUM) {
          camera->perspective +=
              FLOAT_803038d4 * (FLOAT_803038b0 - camera->perspective);
        } else {
          camera->perspective +=
              FLOAT_803038d8 * (FLOAT_803038b0 - camera->perspective);
        }
        if (camera_manager_mode == 1u) {
          camera->perspective +=
              FLOAT_803038d8 * (target_perspective - camera->perspective);
        }
      } else {
        if (speed_kmh <= camera->previous_speed_kmh) {
          camera->perspective +=
              (float)DOUBLE_803038cc * (target_perspective - camera->perspective);
        } else {
          camera->perspective +=
              FLOAT_803038c4 *
              (camera->boost_perspective_target - camera->perspective);
        }
        camera->previous_speed_kmh = speed_kmh;
      }
    }
  } else {
    camera->boost_perspective_target =
        fzgx_clamp_exact(camera->perspective * FLOAT_803038b8, FLOAT_803038b0, FLOAT_803038b4);
    if (camera_manager_mode == 1u) {
      camera->boost_perspective_target = fzgx_clamp_exact(
          camera->perspective * FLOAT_803038c0,
          camera->perspective,
          FLOAT_803038bc);
    }
  }

  camera->local_follow_offset.y +=
      (float)DOUBLE_803038dc * (target_local_y - camera->local_follow_offset.y);
  camera->local_follow_offset.z +=
      (float)DOUBLE_803038dc * (target_local_z - camera->local_follow_offset.z);
  camera->pitch_angle16 = (int16_t)(
      (float)camera->pitch_angle16 +
      (float)DOUBLE_803038dc *
          (float)((int32_t)target_pitch_angle16 - (int32_t)camera->pitch_angle16));

  displacement = fzgx_vec3_sub(machine->position, machine->position_old);
  wheel_style = fzgx_game_camera_uses_wheel_style_basis(machine);
  any_camera_corner_flag = fzgx_game_camera_has_any_camera_corner_flag(machine);
  track_up_override = fzgx_game_camera_track_up_override_active();
  special_track_follow = fzgx_game_camera_has_special_track_follow(machine);

  machine_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&machine_transform, machine->position);

  if (wheel_style || !any_camera_corner_flag ||
      (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON) ||
      ((machine->machine_state & FZGX_MS_B9) != 0u)) {
    if (((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) ||
        (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON) ||
        (machine->zero_minus_height_above_track != 0.0f)) {
      target_basis = machine->basis_physical;
      fzgx_mat43_set_origin_exact(&target_basis, camera->position);
    } else {
      fzgx_vec3 local_displacement =
          fzgx_world_vector_to_local(&machine->basis_physical, displacement);
      fzgx_vec3 rotated_local_direction;
      fzgx_vec3 rotated_world_direction;

      fzgx_game_camera_vec_to_euler(local_displacement, &clamped_pitch, &clamped_yaw);
      clamped_pitch = fzgx_clamp_s16_exact(clamped_pitch, (int16_t)-0x0500, (int16_t)-0x0100);
      clamped_yaw = fzgx_clamp_s16_exact(clamped_yaw, (int16_t)-100, (int16_t)100);
      rotated_local_direction =
          fzgx_game_camera_euler_to_vector(FLOAT_803038e4, clamped_pitch, clamped_yaw);
      rotated_world_direction =
          fzgx_transform_local_vector(&machine->basis_physical, rotated_local_direction);
      target_basis = fzgx_game_camera_build_direction_basis(
          camera->position, camera->up, rotated_world_direction, &machine->basis_physical);
    }
  } else {
    target_basis = fzgx_game_camera_build_direction_basis(
        camera->position, camera->up, displacement, &machine->basis_physical);
  }

  {
    int turn_reaction_step = (int)(0.4f * machine->turn_reaction_effect);
    int16_t turn_angle16 = (int16_t)(182.04445f * (float)turn_reaction_step);
    fzgx_mat43_rotate_about_y_right(&target_basis, (uint16_t)turn_angle16);
  }

  reorient_scale = fzgx_clamp_exact(speed_kmh / FLOAT_5000, (float)DOUBLE_803038a4,
                                    (float)DOUBLE_803038f4);
  reorient_scale *= machine->camera_reorienting;
  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_MEDIUM) {
    reorient_scale *= (float)DOUBLE_80303904;
  }
  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FAR) {
    reorient_scale *= (float)DOUBLE_8030390c;
  }
  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON) {
    reorient_scale = FLOAT_803038c4;
  }
  if (special_track_follow) {
    reorient_scale = FLOAT_803038c4;
  }

  if (!track_up_override) {
    fzgx_mtx_slerp_exact(reorient_scale, &camera->follow_basis, &target_basis, &camera->follow_basis);
  }

  fzgx_game_camera_recompute_follow_point_and_interest(camera, machine);

  if (!track_up_override) {
    target_up = fzgx_transform_local_vector(
        &machine->basis_physical,
        (fzgx_vec3){
            FLOAT_8030384c,
            FLOAT_80303850,
            FLOAT_80303854,
        });
  } else {
    target_up = machine->track_state.track_up;
  }

  reposition_scale =
      (((machine->machine_state & FZGX_MS_AIRBORNE) != 0u) ? camera_parameter : FLOAT_803038d8) *
      machine->camera_repositioning;
  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_MEDIUM) {
    reposition_scale *= (float)DOUBLE_80303904;
  }
  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FAR) {
    reposition_scale *= (float)DOUBLE_80303914;
  }
  if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_FIRST_PERSON) {
    reposition_scale = FLOAT_803038c4;
  }
  fzgx_ray_scale_exact(reposition_scale, &camera->up, &target_up, &camera->up);

  if ((camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_CLOSE) ||
      (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_MEDIUM) ||
      (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_CYCLE)) {
    if ((machine->machine_state & FZGX_MS_AIRBORNE) == 0u) {
      camera->airborne_transition_counter = 0;
    } else if (camera->airborne_transition_counter < 5) {
      camera->airborne_transition_counter += 1;
    }

    if (camera->airborne_transition_counter <= 1) {
      camera->vertical_offset_state +=
          FLOAT_803038d4 * -camera->vertical_offset_state;
      camera->interest_vertical_offset_state +=
          FLOAT_803038d4 * -camera->interest_vertical_offset_state;
    } else {
      special_negative_tuck =
          special_track_follow || (world != 0 &&
                                   ((world->active_track_index == 0x21u) ||
                                    (world->active_track_index == 0x57u)));

      if (special_negative_tuck) {
        if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_CLOSE) {
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_8030393c);
          camera->vertical_offset_state +=
              offset_blend * (-FLOAT_80303944 - camera->vertical_offset_state);
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->interest_vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_8030393c);
          camera->interest_vertical_offset_state +=
              offset_blend * (FLOAT_80303944 - camera->interest_vertical_offset_state);
        } else if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_CYCLE) {
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_80303924);
          camera->vertical_offset_state +=
              offset_blend * (-FLOAT_80303934 - camera->vertical_offset_state);
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->interest_vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_80303924);
          camera->interest_vertical_offset_state +=
              offset_blend * (FLOAT_80303934 - camera->interest_vertical_offset_state);
        } else {
          camera->vertical_offset_state +=
              FLOAT_803038c4 * (-FLOAT_80303948 - camera->vertical_offset_state);
          camera->interest_vertical_offset_state +=
              FLOAT_803038c4 * (FLOAT_80303948 - camera->interest_vertical_offset_state);
        }
      } else {
        if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_CLOSE) {
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_80303924);
          camera->vertical_offset_state +=
              offset_blend * (FLOAT_80303944 - camera->vertical_offset_state);
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->interest_vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_80303924);
          camera->interest_vertical_offset_state +=
              offset_blend * (FLOAT_80303944 - camera->interest_vertical_offset_state);
        } else if (camera->zoom_mode == FZGX_GAME_CAMERA_ZOOM_CYCLE) {
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_80303924);
          camera->vertical_offset_state +=
              offset_blend * (FLOAT_80303934 - camera->vertical_offset_state);
          offset_blend = fzgx_clamp_exact(
              (float)(DOUBLE_80303924 -
                      DOUBLE_8030392c *
                          (double)(FLOAT_8030389c - camera->interest_vertical_offset_state)),
              (float)DOUBLE_8030391c,
              (float)DOUBLE_80303924);
          camera->interest_vertical_offset_state +=
              offset_blend * (FLOAT_80303934 - camera->interest_vertical_offset_state);
        } else {
          camera->vertical_offset_state +=
              FLOAT_803038c4 * (FLOAT_80303948 - camera->vertical_offset_state);
          camera->interest_vertical_offset_state +=
              FLOAT_803038c4 * (FLOAT_80303948 - camera->interest_vertical_offset_state);
        }
      }

    }

    machine_transform = camera->follow_basis;
    fzgx_mat43_set_origin_exact(&machine_transform, machine->position);
    camera->position = fzgx_transform_local_point(
        &machine_transform,
        (fzgx_vec3){
            camera->local_follow_offset.x,
            camera->local_follow_offset.y - camera->vertical_offset_state,
            camera->local_follow_offset.z,
        });

    {
      fzgx_vec3 local_interest =
          fzgx_world_point_to_local(&machine_transform, camera->interest);
      local_interest.y -= camera->interest_vertical_offset_state * FLOAT_8030394c;
      camera->interest = fzgx_transform_local_point(&machine_transform, local_interest);
    }
  }
}

static void fzgx_game_camera_load_cycle_preset(
    fzgx_game_camera_runtime *camera,
    const fzgx_game_camera_cycle_preset *preset) {
  if ((camera == 0) || (preset == 0)) {
    return;
  }

  camera->restore_anchor_position = preset->start_pos;
  camera->restore_blend = 0.0f;
  camera->restore_blend_velocity = 0.0f;
  camera->airborne_transition_counter = (int16_t)preset->duration_frames;
  camera->local_follow_offset = preset->start_pos;
  camera->local_interest_offset = preset->start_interest;
  camera->perspective = preset->start_perspective;
  camera->sequence_angle16 = preset->start_angle16;
}

static const fzgx_game_camera_cycle_preset *fzgx_game_camera_get_cycle_preset(
    const fzgx_sim_world *world,
    int16_t sequence_index,
    size_t *count_out) {
  if ((world != 0) && (world->race_mode == 0u)) {
    if (count_out != 0) {
      *count_out = sizeof(fzgx_generated_game_camera_winning_cycle_presets) /
                   sizeof(fzgx_generated_game_camera_winning_cycle_presets[0]);
    }
    if ((sequence_index < 0) ||
        ((size_t)sequence_index >= (sizeof(fzgx_generated_game_camera_winning_cycle_presets) /
                                    sizeof(fzgx_generated_game_camera_winning_cycle_presets[0])))) {
      sequence_index = 0;
    }
    return &fzgx_generated_game_camera_winning_cycle_presets[sequence_index];
  }

  if (count_out != 0) {
    *count_out = sizeof(fzgx_generated_game_camera_ordinary_cycle_presets) /
                 sizeof(fzgx_generated_game_camera_ordinary_cycle_presets[0]);
  }
  if ((sequence_index < 0) ||
      ((size_t)sequence_index >= (sizeof(fzgx_generated_game_camera_ordinary_cycle_presets) /
                                  sizeof(fzgx_generated_game_camera_ordinary_cycle_presets[0])))) {
    sequence_index = 0;
  }
  return &fzgx_generated_game_camera_ordinary_cycle_presets[sequence_index];
}

static void fzgx_game_camera_update_cycle_transition(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine,
    const fzgx_game_camera_cycle_preset *preset) {
  if ((camera == 0) || (machine == 0) || (preset == 0) ||
      (camera->airborne_transition_counter <= 0)) {
    return;
  }

  if (preset->transition_kind == 2) {
    fzgx_ray_scale_exact(
        preset->blend_step, &camera->local_follow_offset, &preset->end_pos, &camera->local_follow_offset);
    fzgx_ray_scale_exact(
        preset->blend_step,
        &camera->local_interest_offset,
        &preset->end_interest,
        &camera->local_interest_offset);
    camera->perspective += preset->blend_step * (preset->end_perspective - camera->perspective);
    camera->sequence_angle16 =
        (int16_t)((float)camera->sequence_angle16 +
                  preset->blend_step *
                      (float)((int32_t)preset->end_angle16 - (int32_t)camera->sequence_angle16));
  } else if (preset->transition_kind == 0) {
    float fraction =
        (float)(DOUBLE_803039e4 -
                (double)camera->airborne_transition_counter / (double)preset->duration_frames);

    camera->local_follow_offset =
        fzgx_vec3_lerp_exact(preset->start_pos, preset->end_pos, fraction);
    camera->local_interest_offset =
        fzgx_vec3_lerp_exact(preset->start_interest, preset->end_interest, fraction);
    camera->perspective =
        preset->start_perspective + fraction * (preset->end_perspective - preset->start_perspective);
    camera->sequence_angle16 =
        (int16_t)((float)preset->start_angle16 +
                  fraction *
                      (float)((int32_t)preset->end_angle16 - (int32_t)preset->start_angle16));
  } else if (preset->transition_kind == 3) {
    float accel = 1.0f / ((float)preset->duration_frames * (float)preset->duration_frames * 0.25f);

    if ((float)preset->duration_frames * FLOAT_80303934 <=
        (float)camera->airborne_transition_counter) {
      camera->restore_blend_velocity += accel;
    } else {
      camera->restore_blend_velocity -= accel;
    }
    camera->restore_blend = fzgx_clamp_exact(
        camera->restore_blend + camera->restore_blend_velocity,
        FLOAT_80303834,
        FLOAT_803038e4);
    camera->local_follow_offset =
        fzgx_vec3_lerp_exact(preset->start_pos, preset->end_pos, camera->restore_blend);
    camera->local_interest_offset =
        fzgx_vec3_lerp_exact(preset->start_interest, preset->end_interest, camera->restore_blend);
    camera->perspective = preset->start_perspective +
                          camera->restore_blend *
                              (preset->end_perspective - preset->start_perspective);
    camera->sequence_angle16 =
        (int16_t)((float)preset->start_angle16 +
                  camera->restore_blend *
                      (float)((int32_t)preset->end_angle16 - (int32_t)preset->start_angle16));
  }

  if (fzgx_mat43_is_finite_exact(&machine->g_pitch_mtx_0x5e0)) {
    fzgx_mtx_slerp_exact(FLOAT_803038d8, &camera->follow_basis, &machine->g_pitch_mtx_0x5e0,
                         &camera->follow_basis);
  }
  fzgx_mat43_set_origin_exact(&camera->follow_basis, machine->position);
  camera->position = fzgx_transform_local_point(&camera->follow_basis, camera->local_follow_offset);
  camera->interest = fzgx_transform_local_point(&camera->follow_basis, camera->local_interest_offset);
}

static fzgx_vec3 fzgx_game_camera_build_cycle_up(
    const fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine) {
  fzgx_vec3 forward;
  fzgx_vec3 track_up;
  fzgx_vec3 base_up;

  if ((camera == 0) || (machine == 0)) {
    return fzgx_game_camera_world_up;
  }

  forward = fzgx_vec3_normalize_or_exact(
      fzgx_vec3_sub(camera->interest, camera->position),
      fzgx_vec3_scale(fzgx_mat43_get_basis_z_exact(&camera->follow_basis), -1.0f));
  track_up = fzgx_vec3_normalize_or_exact(machine->track_state.track_up, camera->up);
  base_up = fzgx_vec3_project_onto_plane_exact(track_up, forward);
  base_up = fzgx_vec3_normalize_or_exact(base_up, camera->up);
  return fzgx_vec3_normalize_or_exact(
      fzgx_rotate_about_axis_exact(base_up, forward, camera->sequence_angle16),
      base_up);
}

static void fzgx_game_camera_update_restore_follow(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine) {
  fzgx_game_camera_follow_preset_triplet preset;
  fzgx_mat43 restore_transform;
  fzgx_mat43 current_transform;
  fzgx_vec3 local_follow = {0.0f, 0.0f, 0.0f};
  fzgx_vec3 target_pos;
  fzgx_vec3 target_up;
  fzgx_vec3 current_interest;

  if ((camera == 0) || (machine == 0)) {
    return;
  }

  camera->zoom_mode = FZGX_GAME_CAMERA_ZOOM_RESTORE;
  fzgx_game_camera_load_follow_preset_triplet(camera, &preset);

  if (machine->frames_until_restored == (uint16_t)DOUBLE_80303964) {
    camera->restore_anchor_position = camera->position;
    camera->restore_blend = 0.0f;
    camera->restore_blend_velocity = 0.0f;
  }

  local_follow.y = preset.triplets[0].local_height;
  local_follow.z = preset.triplets[0].local_distance;

  restore_transform = machine->g_restore_mtx_3;
  target_pos = fzgx_transform_local_point(&restore_transform, local_follow);
  target_up = fzgx_transform_local_vector(&restore_transform, fzgx_game_camera_world_up);

  current_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&current_transform, machine->position);
  current_interest = fzgx_transform_local_point(&current_transform, local_follow);
  fzgx_mat43_set_origin_exact(&current_transform, current_interest);
  fzgx_mat43_rotate_about_x_right(&current_transform, (uint16_t)preset.triplets[0].pitch_angle16);
  current_interest = fzgx_vec3_add(
      fzgx_mat43_get_origin_exact(&current_transform),
      fzgx_vec3_scale(fzgx_mat43_get_basis_z_exact(&current_transform), FLOAT_80303838));

  if ((float)((int32_t)machine->frames_until_restored - 20) >= FLOAT_8030396c) {
    camera->restore_blend_velocity += FLOAT_80303970;
  } else {
    camera->restore_blend_velocity -= FLOAT_80303970;
  }
  camera->restore_blend = fzgx_clamp_exact(
      camera->restore_blend + camera->restore_blend_velocity,
      FLOAT_80303834,
      FLOAT_803038e4);

  fzgx_ray_scale_exact(
      camera->restore_blend,
      &camera->restore_anchor_position,
      &target_pos,
      &camera->position);
  fzgx_ray_scale_exact(FLOAT_803038c4, &camera->interest, &current_interest, &camera->interest);
  fzgx_ray_scale_exact(FLOAT_803038c4, &camera->up, &target_up, &camera->up);
  camera->follow_basis = machine->basis_physical;
  camera->perspective +=
      (float)DOUBLE_80303954 * ((float)DOUBLE_8030395c - camera->perspective);
}

static void fzgx_game_camera_update_track_restore_follow(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine) {
  fzgx_game_camera_follow_preset_triplet preset;
  fzgx_mat43 current_transform;
  fzgx_mat43 track_basis;
  fzgx_vec3 machine_up;
  fzgx_vec3 local_follow = {0.0f, 0.0f, 0.0f};
  fzgx_vec3 target_pos;
  fzgx_vec3 target_interest;

  if ((camera == 0) || (machine == 0)) {
    return;
  }

  if (camera->restore_countdown < 0) {
    camera->restore_anchor_position = camera->position;
    camera->restore_blend = 0.0f;
    camera->restore_blend_velocity = 0.0f;
    camera->restore_countdown = 0x78;
  }
  if (camera->restore_countdown > 0) {
    camera->restore_countdown -= 1;
  }

  current_transform = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&current_transform, machine->position);
  machine_up = fzgx_mat43_get_basis_y_exact(&current_transform);

  camera->zoom_mode = FZGX_GAME_CAMERA_ZOOM_RESTORE;
  fzgx_game_camera_load_follow_preset_triplet(camera, &preset);
  local_follow.y = preset.triplets[0].local_height;
  local_follow.z = preset.triplets[0].local_distance;

  track_basis = fzgx_game_camera_build_direction_basis(
      machine->position, machine_up, machine->track_state.last_track_pos, &machine->basis_physical);
  if ((machine->track_state.flags & FZGX_GAME_CAMERA_TRACK_FLAG_SPECIAL_FOLLOW) == 0u) {
    fzgx_mat43_set_origin_exact(&track_basis, machine->position);
    fzgx_mat43_set_origin_exact(
        &track_basis,
        fzgx_transform_local_point(
            &track_basis,
            (fzgx_vec3){-machine->track_state.angle_from_track_forward, 0.0f, 0.0f}));
  }
  target_pos = fzgx_transform_local_point(&track_basis, local_follow);

  target_interest = fzgx_transform_local_point(
      &current_transform, (fzgx_vec3){0.0f, 3.0f, 0.0f});

  if ((float)camera->restore_countdown >= FLOAT_80303974) {
    camera->restore_blend_velocity += FLOAT_80303978;
  } else {
    camera->restore_blend_velocity -= FLOAT_80303978;
  }
  camera->restore_blend = fzgx_clamp_exact(
      camera->restore_blend + camera->restore_blend_velocity,
      FLOAT_80303834,
      FLOAT_803038e4);
  if (camera->restore_countdown == 0) {
    camera->restore_blend = FLOAT_803038e4;
  }

  fzgx_ray_scale_exact(
      camera->restore_blend,
      &camera->restore_anchor_position,
      &target_pos,
      &camera->position);
  fzgx_ray_scale_exact(FLOAT_803038c4, &camera->interest, &target_interest, &camera->interest);
  fzgx_ray_scale_exact(FLOAT_803038c4, &camera->up, &machine_up, &camera->up);
  camera->follow_basis = machine->basis_physical;
  camera->perspective +=
      (float)DOUBLE_80303954 * ((float)DOUBLE_8030395c - camera->perspective);
}

static void fzgx_game_camera_update_fallout_follow(
    fzgx_game_camera_runtime *camera,
    const fzgx_machine_snapshot *machine) {
  if ((camera == 0) || (machine == 0)) {
    return;
  }

  if ((machine->state_2 & 0x10u) == 0u) {
    fzgx_ray_scale_exact(FLOAT_803038c4, &camera->interest, &machine->position, &camera->interest);
  }
}

static void fzgx_game_camera_update_shake(fzgx_game_camera_runtime *camera) {
  if (camera == 0) {
    return;
  }

  if (camera->shake_active == 0u) {
    camera->shake_position_base = camera->position;
    camera->shake_interest_base = camera->interest;
    return;
  }

  camera->shake_position_velocity.x = (float)(
      ((double)camera->shake_position_velocity.x -
       DOUBLE_803039ec * (double)camera->shake_position_offset.x) *
      DOUBLE_803039f4);
  camera->shake_position_velocity.y = (float)(
      ((double)camera->shake_position_velocity.y -
       DOUBLE_803039ec * (double)camera->shake_position_offset.y) *
      DOUBLE_803039f4);
  camera->shake_position_velocity.z = (float)(
      ((double)camera->shake_position_velocity.z -
       DOUBLE_803039ec * (double)camera->shake_position_offset.z) *
      DOUBLE_803039f4);
  camera->shake_position_offset =
      fzgx_vec3_add(camera->shake_position_offset, camera->shake_position_velocity);

  camera->shake_interest_velocity.x = (float)(
      ((double)camera->shake_interest_velocity.x -
       DOUBLE_803039fc * (double)camera->shake_interest_offset.x) *
      DOUBLE_803039ec);
  camera->shake_interest_velocity.y = (float)(
      ((double)camera->shake_interest_velocity.y -
       DOUBLE_803039fc * (double)camera->shake_interest_offset.y) *
      DOUBLE_803039ec);
  camera->shake_interest_velocity.z = (float)(
      ((double)camera->shake_interest_velocity.z -
       DOUBLE_803039fc * (double)camera->shake_interest_offset.z) *
      DOUBLE_803039ec);
  camera->shake_interest_offset =
      fzgx_vec3_add(camera->shake_interest_offset, camera->shake_interest_velocity);

  camera->position = fzgx_vec3_add(camera->shake_position_base, camera->shake_position_offset);
  if (camera->behavior_state == FZGX_GAME_CAMERA_BEHAVIOR_NORMAL) {
    camera->interest = fzgx_vec3_add(camera->shake_interest_base, camera->shake_interest_offset);
  }

  if (camera->shake_frames_remaining > 0) {
    camera->shake_frames_remaining -= 1;
    if (camera->shake_frames_remaining == 0) {
      camera->shake_active = 0u;
    }
  }
}

void fzgx_game_camera_init(fzgx_game_camera_runtime *camera) {
  if (camera == 0) {
    return;
  }

  memset(camera, 0, sizeof(*camera));
  camera->persistent_saved_zoom_mode = FZGX_GAME_CAMERA_ZOOM_CLOSE;
  camera->display_mode_kind = 0xffu;
  camera->behavior_state = FZGX_GAME_CAMERA_BEHAVIOR_NORMAL;
  camera->zoom_mode = FZGX_GAME_CAMERA_ZOOM_CLOSE;
  camera->saved_zoom_mode = FZGX_GAME_CAMERA_ZOOM_CLOSE;
  camera->restore_countdown = -1;
  camera->sequence_angle16 = -1;
  camera->aspect_ratio = 4.0f / 3.0f;
  camera->camera_parameter = NAN;
  camera->perspective = FLOAT_80303830;
  camera->local_interest_offset = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  camera->up = fzgx_game_camera_world_up;
  camera->view_matrix = fzgx_mat43_identity_exact();
  camera->previous_view_matrix = camera->view_matrix;
}

fzgx_status fzgx_game_camera_reset(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    uint32_t machine_index) {
  const fzgx_machine_snapshot *machine;
  fzgx_game_camera_follow_preset_triplet preset;
  float raw_clearance_ratio;
  uint8_t persistent_zoom;
  uint8_t display_mode_kind;
  uint8_t camera_manager_mode;
  float aspect_ratio;
  float camera_parameter;
  bool had_initialized_state;

  if ((camera == 0) || (world == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  machine = fzgx_game_camera_get_machine(world, machine_index);
  if (machine == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  persistent_zoom = camera->persistent_saved_zoom_mode;
  display_mode_kind = camera->display_mode_kind;
  camera_manager_mode = camera->camera_manager_mode;
  aspect_ratio = camera->aspect_ratio;
  camera_parameter = camera->camera_parameter;
  had_initialized_state = isfinite(aspect_ratio) && (aspect_ratio > 0.0f);
  fzgx_game_camera_init(camera);
  if (had_initialized_state &&
      (persistent_zoom >= FZGX_GAME_CAMERA_USER_ZOOM_MIN) &&
      (persistent_zoom <= FZGX_GAME_CAMERA_USER_ZOOM_MAX)) {
    camera->persistent_saved_zoom_mode = persistent_zoom;
  }
  camera->display_mode_kind = display_mode_kind;
  camera->camera_manager_mode = camera_manager_mode;
  if (had_initialized_state && isfinite(aspect_ratio) && (aspect_ratio > 0.0f)) {
    camera->aspect_ratio = aspect_ratio;
  }
  camera->camera_parameter = camera_parameter;

  camera->machine_index = machine_index;
  camera->active = 1u;
  if ((camera->persistent_saved_zoom_mode >= FZGX_GAME_CAMERA_USER_ZOOM_MIN) &&
      (camera->persistent_saved_zoom_mode <= FZGX_GAME_CAMERA_USER_ZOOM_MAX)) {
    camera->zoom_mode = camera->persistent_saved_zoom_mode;
    camera->saved_zoom_mode = camera->persistent_saved_zoom_mode;
  }

  fzgx_game_camera_load_follow_preset_triplet(camera, &preset);
  camera->follow_basis = machine->basis_physical;
  fzgx_mat43_set_origin_exact(&camera->follow_basis, machine->position);
  camera->up = fzgx_mat43_get_basis_y_exact(&machine->basis_physical);
  camera->local_follow_offset = (fzgx_vec3){
      FLOAT_80303818,
      preset.triplets[0].local_height,
      preset.triplets[0].local_distance,
  };
  camera->pitch_angle16 = preset.triplets[0].pitch_angle16;
  camera->boost_perspective_target = FLOAT_80303834;
  camera->airborne_transition_counter = 0;
  camera->vertical_offset_state = FLOAT_80303834;
  camera->interest_vertical_offset_state = FLOAT_80303834;
  camera->perspective_transition_counter = 0;
  camera->previous_speed_kmh = FLOAT_80303834;

  fzgx_game_camera_recompute_follow_point_and_interest(camera, machine);
  camera->restore_anchor_position = camera->position;
  camera->restore_blend = FLOAT_80303834;
  camera->restore_blend_velocity = FLOAT_80303834;

  camera->local_interest_offset = (fzgx_vec3){0.0f, 0.0f, -1.0f};
  camera->sequence_angle16 = 0;

  raw_clearance_ratio = fzgx_game_camera_sample_vertical_clearance_ratio(camera, world, machine);
  camera->clearance_ratio = raw_clearance_ratio;

  if (camera->zoom_mode < FZGX_GAME_CAMERA_ZOOM_RESTORE) {
    camera->local_follow_offset.y +=
        raw_clearance_ratio * (preset.triplets[2].local_height - camera->local_follow_offset.y);
    camera->local_follow_offset.z +=
        raw_clearance_ratio * (preset.triplets[2].local_distance - camera->local_follow_offset.z);
    camera->pitch_angle16 = (int16_t)(
        (float)camera->pitch_angle16 +
        raw_clearance_ratio *
            (float)((int32_t)preset.triplets[2].pitch_angle16 - (int32_t)camera->pitch_angle16) *
            (float)DOUBLE_8030383c);
    fzgx_game_camera_recompute_follow_point_and_interest(camera, machine);
  }

  fzgx_game_camera_rebuild_view_matrix_exact(camera, machine);
  camera->previous_view_matrix = camera->view_matrix;
  camera->previous_position = camera->position;
  camera->shake_position_base = camera->position;
  camera->shake_interest_base = camera->interest;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_game_camera_step(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_game_camera_input *input) {
  const fzgx_machine_snapshot *machine;

  if ((camera == 0) || (world == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  machine = fzgx_game_camera_get_machine(world, camera->machine_index);
  if (machine == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  camera->active = 1u;
  camera->previous_position = camera->position;
  fzgx_game_camera_update_behavior_state_from_machine(camera, world, machine);

  switch (camera->behavior_state) {
  case FZGX_GAME_CAMERA_BEHAVIOR_NORMAL:
    fzgx_game_camera_update_normal_follow(camera, world, machine, input);
    fzgx_game_camera_update_shake(camera);
    break;

  case FZGX_GAME_CAMERA_BEHAVIOR_PRESET_LOCAL:
    fzgx_game_camera_update_zoom_mode_from_input(camera, machine, input);
    if ((camera->zoom_mode < 0) ||
        ((size_t)camera->zoom_mode >=
         (sizeof(fzgx_game_camera_local_shot_presets) /
          sizeof(fzgx_game_camera_local_shot_presets[0])))) {
      camera->zoom_mode = 0;
    }
    camera->position = fzgx_transform_local_point(
        &(fzgx_mat43){machine->basis_physical.basis_x_x,
                      machine->basis_physical.basis_y_x,
                      machine->basis_physical.basis_z_x,
                      machine->position.x,
                      machine->basis_physical.basis_x_y,
                      machine->basis_physical.basis_y_y,
                      machine->basis_physical.basis_z_y,
                      machine->position.y,
                      machine->basis_physical.basis_x_z,
                      machine->basis_physical.basis_y_z,
                      machine->basis_physical.basis_z_z,
                      machine->position.z},
        fzgx_game_camera_local_shot_presets[camera->zoom_mode].local_position);
    camera->interest = fzgx_transform_local_point(
        &(fzgx_mat43){machine->basis_physical.basis_x_x,
                      machine->basis_physical.basis_y_x,
                      machine->basis_physical.basis_z_x,
                      machine->position.x,
                      machine->basis_physical.basis_x_y,
                      machine->basis_physical.basis_y_y,
                      machine->basis_physical.basis_z_y,
                      machine->position.y,
                      machine->basis_physical.basis_x_z,
                      machine->basis_physical.basis_y_z,
                      machine->basis_physical.basis_z_z,
                      machine->position.z},
        fzgx_game_camera_local_shot_presets[camera->zoom_mode].local_interest);
    camera->perspective +=
        (float)DOUBLE_80303954 * ((float)DOUBLE_8030395c - camera->perspective);
    break;

  case FZGX_GAME_CAMERA_BEHAVIOR_CYCLE: {
    const fzgx_game_camera_cycle_preset *preset;
    size_t preset_count = 0u;

    if (camera->cycle_initialized == 0u) {
      camera->zoom_mode = 0;
      preset = fzgx_game_camera_get_cycle_preset(world, camera->zoom_mode, &preset_count);
      fzgx_game_camera_load_cycle_preset(camera, preset);
      camera->cycle_initialized = 1u;
    }

    preset = fzgx_game_camera_get_cycle_preset(world, camera->zoom_mode, &preset_count);
    if (camera->airborne_transition_counter == 0) {
      camera->zoom_mode += 1;
      if ((size_t)camera->zoom_mode >= preset_count) {
        camera->zoom_mode = 0;
      }
      preset = fzgx_game_camera_get_cycle_preset(world, camera->zoom_mode, &preset_count);
      fzgx_game_camera_load_cycle_preset(camera, preset);
    }

    fzgx_game_camera_update_cycle_transition(camera, machine, preset);
    if (camera->airborne_transition_counter > 0) {
      camera->airborne_transition_counter -= 1;
    }
    camera->up = fzgx_game_camera_build_cycle_up(camera, machine);
    break;
  }

  case FZGX_GAME_CAMERA_BEHAVIOR_RESTORE:
    fzgx_game_camera_update_restore_follow(camera, machine);
    break;

  case FZGX_GAME_CAMERA_BEHAVIOR_FALLOUT:
    fzgx_game_camera_update_fallout_follow(camera, machine);
    fzgx_game_camera_update_shake(camera);
    break;

  case FZGX_GAME_CAMERA_BEHAVIOR_TRACK_RESTORE:
    fzgx_game_camera_update_track_restore_follow(camera, machine);
    fzgx_game_camera_update_shake(camera);
    break;

  default:
    break;
  }

  camera->up = fzgx_vec3_normalize_or_exact(camera->up, fzgx_game_camera_world_up);
  if (!fzgx_vec3_is_finite_exact(camera->position) ||
      !fzgx_vec3_is_finite_exact(camera->interest) ||
      !fzgx_vec3_is_finite_exact(camera->up)) {
    camera->position = machine->position;
    camera->previous_position = camera->position;
    camera->interest = fzgx_vec3_add(
        machine->position,
        fzgx_vec3_scale(fzgx_mat43_get_basis_z_exact(&machine->basis_physical), -5.0f));
    camera->up = fzgx_mat43_get_basis_y_exact(&machine->basis_physical);
    camera->perspective = FLOAT_80303830;
    camera->aspect_ratio = (camera->aspect_ratio > 0.0f) ? camera->aspect_ratio : (4.0f / 3.0f);
  }
  fzgx_game_camera_rebuild_view_matrix_exact(camera, machine);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_game_camera_reset_follow_preview(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    uint32_t machine_index) {
  return fzgx_game_camera_reset(camera, world, machine_index);
}

fzgx_status fzgx_game_camera_step_follow_preview(
    fzgx_game_camera_runtime *camera,
    const fzgx_sim_world *world,
    const fzgx_game_camera_input *input) {
  return fzgx_game_camera_step(camera, world, input);
}

fzgx_status fzgx_game_camera_get_view(
    const fzgx_game_camera_runtime *camera,
    fzgx_game_camera_view *view_out) {
  if ((camera == 0) || (view_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  memset(view_out, 0, sizeof(*view_out));
  view_out->active = camera->active;
  view_out->zoom_mode = (uint8_t)camera->zoom_mode;
  view_out->saved_zoom_mode = (uint8_t)camera->saved_zoom_mode;
  view_out->behavior_state = (uint8_t)camera->behavior_state;
  view_out->perspective = camera->perspective;
  view_out->aspect_ratio = camera->aspect_ratio;
  view_out->position = camera->position;
  view_out->previous_position = camera->previous_position;
  view_out->interest = camera->interest;
  view_out->up = fzgx_vec3_normalize_or_exact(camera->up, fzgx_game_camera_world_up);
  view_out->view_matrix = camera->view_matrix;
  view_out->previous_view_matrix = camera->previous_view_matrix;
  return FZGX_STATUS_OK;
}
