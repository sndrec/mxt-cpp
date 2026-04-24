#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

extern "C" {
#include "fzgx/camera.h"
#include "fzgx/content.h"
#include "fzgx/replay.h"
#include "fzgx/sim.h"
}

using namespace godot;

namespace {

static uint32_t fzgx_float_bits_exact(float value);

static Vector3 fzgx_to_godot_vec3(const fzgx_vec3 &value) {
  return Vector3(value.x, value.y, value.z);
}

static Transform3D fzgx_machine_transform_to_godot(const fzgx_machine_snapshot &machine) {
  Basis basis(
      fzgx_to_godot_vec3(fzgx_mat43_get_basis_x_exact(&machine.basis_physical)),
      fzgx_to_godot_vec3(fzgx_mat43_get_basis_y_exact(&machine.basis_physical)),
      fzgx_to_godot_vec3(fzgx_mat43_get_basis_z_exact(&machine.basis_physical)));
  return Transform3D(basis, fzgx_to_godot_vec3(machine.position));
}

static Transform3D fzgx_mat43_to_godot_transform(const fzgx_mat43 &transform) {
  Basis basis(
      fzgx_to_godot_vec3(fzgx_mat43_get_basis_x_exact(&transform)),
      fzgx_to_godot_vec3(fzgx_mat43_get_basis_y_exact(&transform)),
      fzgx_to_godot_vec3(fzgx_mat43_get_basis_z_exact(&transform)));
  return Transform3D(basis, fzgx_to_godot_vec3(fzgx_mat43_get_origin_exact(&transform)));
}

static fzgx_mat43 fzgx_mat43_rigid_inverted(const fzgx_mat43 &transform) {
  fzgx_mat43 inverse = transform;
  const float origin_x = transform.origin_x;
  const float origin_y = transform.origin_y;
  const float origin_z = transform.origin_z;
  const float basis_x_x = transform.basis_x_x;
  const float basis_y_x = transform.basis_y_x;
  const float basis_z_x = transform.basis_z_x;
  const float basis_x_y = transform.basis_x_y;
  const float basis_y_y = transform.basis_y_y;
  const float basis_z_y = transform.basis_z_y;
  const float basis_x_z = transform.basis_x_z;
  const float basis_y_z = transform.basis_y_z;
  const float basis_z_z = transform.basis_z_z;

  inverse.basis_x_y = basis_y_x;
  inverse.basis_x_z = basis_z_x;
  inverse.basis_y_x = basis_x_y;
  inverse.basis_y_z = basis_z_y;
  inverse.basis_z_x = basis_x_z;
  inverse.basis_z_y = basis_y_z;
  inverse.origin_x = -(origin_z * basis_x_z + origin_y * basis_x_y + origin_x * basis_x_x);
  inverse.origin_y = -(origin_z * basis_y_z + origin_y * basis_y_y + origin_x * basis_y_x);
  inverse.origin_z = -(origin_z * basis_z_z + origin_y * basis_z_y + origin_x * basis_z_x);
  return inverse;
}

static double fzgx_clamp_unit(double value) {
  return std::max(-1.0, std::min(1.0, value));
}

static Vector3 fzgx_normalized_or_fallback(
    const Vector3 &value,
    const Vector3 &fallback) {
  if (value.length_squared() <= 1.0e-8f) {
    return fallback;
  }
  return value.normalized();
}

static fzgx_mat43 fzgx_mat43_identity_exact() {
  fzgx_mat43 transform = {};

  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  return transform;
}

static float fzgx_angle16_to_radians_exact(uint16_t angle16) {
  return ((float)(int16_t)angle16 / 32768.0f) * 3.14159265358979323846f;
}

static uint16_t fzgx_degrees_to_angle16_exact(float degrees) {
  return (uint16_t)(int32_t)(degrees * (32768.0f / 180.0f));
}

static fzgx_vec3 fzgx_vec3_scale_exact(fzgx_vec3 value, float scale) {
  return (fzgx_vec3){value.x * scale, value.y * scale, value.z * scale};
}

static fzgx_vec3 fzgx_vec3_sub_exact(fzgx_vec3 lhs, fzgx_vec3 rhs) {
  return (fzgx_vec3){lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

static void fzgx_mat43_rotate_about_x_right_exact(fzgx_mat43 *transform, uint16_t angle) {
  const float radians = fzgx_angle16_to_radians_exact(angle);
  const float sin_value = std::sin(radians);
  const float cos_value = std::cos(radians);
  const float basis_y_x = transform->basis_y_x;
  const float basis_y_y = transform->basis_y_y;
  const float basis_y_z = transform->basis_y_z;
  const float basis_z_x = transform->basis_z_x;
  const float basis_z_y = transform->basis_z_y;
  const float basis_z_z = transform->basis_z_z;

  transform->basis_y_x = basis_y_x * cos_value + basis_z_x * sin_value;
  transform->basis_y_y = basis_y_y * cos_value + basis_z_y * sin_value;
  transform->basis_y_z = basis_y_z * cos_value + basis_z_z * sin_value;
  transform->basis_z_x = basis_y_x * -sin_value + basis_z_x * cos_value;
  transform->basis_z_y = basis_y_y * -sin_value + basis_z_y * cos_value;
  transform->basis_z_z = basis_y_z * -sin_value + basis_z_z * cos_value;
}

static void fzgx_mat43_rotate_about_y_right_exact(fzgx_mat43 *transform, uint16_t angle) {
  const float radians = fzgx_angle16_to_radians_exact(angle);
  const float sin_value = std::sin(radians);
  const float cos_value = std::cos(radians);
  const float basis_x_x = transform->basis_x_x;
  const float basis_x_y = transform->basis_x_y;
  const float basis_x_z = transform->basis_x_z;
  const float basis_z_x = transform->basis_z_x;
  const float basis_z_y = transform->basis_z_y;
  const float basis_z_z = transform->basis_z_z;

  transform->basis_z_x = basis_x_x * sin_value + basis_z_x * cos_value;
  transform->basis_z_y = basis_x_y * sin_value + basis_z_y * cos_value;
  transform->basis_z_z = basis_x_z * sin_value + basis_z_z * cos_value;
  transform->basis_x_x = basis_x_x * cos_value + basis_z_x * -sin_value;
  transform->basis_x_y = basis_x_y * cos_value + basis_z_y * -sin_value;
  transform->basis_x_z = basis_x_z * cos_value + basis_z_z * -sin_value;
}

static void fzgx_mat43_rotate_about_z_right_exact(fzgx_mat43 *transform, uint16_t angle) {
  const float radians = fzgx_angle16_to_radians_exact(angle);
  const float sin_value = std::sin(radians);
  const float cos_value = std::cos(radians);
  const float basis_x_x = transform->basis_x_x;
  const float basis_x_y = transform->basis_x_y;
  const float basis_x_z = transform->basis_x_z;
  const float basis_y_x = transform->basis_y_x;
  const float basis_y_y = transform->basis_y_y;
  const float basis_y_z = transform->basis_y_z;

  transform->basis_x_x = basis_x_x * cos_value + basis_y_x * sin_value;
  transform->basis_x_y = basis_x_y * cos_value + basis_y_y * sin_value;
  transform->basis_x_z = basis_x_z * cos_value + basis_y_z * sin_value;
  transform->basis_y_x = basis_x_x * -sin_value + basis_y_x * cos_value;
  transform->basis_y_y = basis_x_y * -sin_value + basis_y_y * cos_value;
  transform->basis_y_z = basis_x_z * -sin_value + basis_y_z * cos_value;
}

static void fzgx_mat43_translate_local_exact(fzgx_mat43 *transform, fzgx_vec3 local_offset) {
  const float basis_x_x = transform->basis_x_x;
  const float basis_y_x = transform->basis_y_x;
  const float basis_z_x = transform->basis_z_x;
  const float origin_x = transform->origin_x;
  const float basis_x_y = transform->basis_x_y;
  const float basis_y_y = transform->basis_y_y;
  const float basis_z_y = transform->basis_z_y;
  const float origin_y = transform->origin_y;
  const float basis_x_z = transform->basis_x_z;
  const float basis_y_z = transform->basis_y_z;
  const float basis_z_z = transform->basis_z_z;
  const float origin_z = transform->origin_z;

  transform->origin_x =
      basis_z_x * local_offset.z + basis_x_x * local_offset.x + origin_x + basis_y_x * local_offset.y;
  transform->origin_y =
      basis_z_y * local_offset.z + basis_x_y * local_offset.x + origin_y + basis_y_y * local_offset.y;
  transform->origin_z =
      basis_z_z * local_offset.z + basis_x_z * local_offset.x + origin_z + basis_y_z * local_offset.y;
}

static fzgx_mat43 fzgx_mat43_from_transform_trxs_exact(
    const fzgx_transform_trxs_record *transform_record) {
  fzgx_mat43 transform = fzgx_mat43_identity_exact();

  if (transform_record == nullptr) {
    return transform;
  }
  fzgx_mat43_set_origin_exact(&transform, transform_record->position);
  if (transform_record->rotation_z_angle16 != 0u) {
    fzgx_mat43_rotate_about_z_right_exact(&transform, transform_record->rotation_z_angle16);
  }
  if (transform_record->rotation_y_angle16 != 0u) {
    fzgx_mat43_rotate_about_y_right_exact(&transform, transform_record->rotation_y_angle16);
  }
  if (transform_record->rotation_x_angle16 != 0u) {
    fzgx_mat43_rotate_about_x_right_exact(&transform, transform_record->rotation_x_angle16);
  }
  fzgx_mat43_set_basis_x_exact(
      &transform,
      fzgx_vec3_scale_exact(fzgx_mat43_get_basis_x_exact(&transform), transform_record->scale.x));
  fzgx_mat43_set_basis_y_exact(
      &transform,
      fzgx_vec3_scale_exact(fzgx_mat43_get_basis_y_exact(&transform), transform_record->scale.y));
  fzgx_mat43_set_basis_z_exact(
      &transform,
      fzgx_vec3_scale_exact(fzgx_mat43_get_basis_z_exact(&transform), transform_record->scale.z));
  return transform;
}

static bool fzgx_scene_object_collider_mesh_has_geometry_exact(
    const fzgx_owned_scene_object_collider_mesh *mesh) {
  if (mesh == nullptr) {
    return false;
  }
  return ((mesh->tri_count != 0u) && (mesh->tris != nullptr)) ||
         ((mesh->quad_count != 0u) && (mesh->quads != nullptr));
}

static void fzgx_append_collision_mesh_geometry_exact(
    PackedVector3Array &vertices,
    PackedVector3Array &normals,
    const fzgx_static_collider_triangle_record *tris,
    uint32_t tri_count,
    const fzgx_static_collider_quad_record *quads,
    uint32_t quad_count) {
  for (uint32_t i = 0u; i < tri_count; ++i) {
    const fzgx_static_collider_triangle_record &tri = tris[i];
    const Vector3 normal = fzgx_to_godot_vec3(tri.normal);

    vertices.push_back(fzgx_to_godot_vec3(tri.vertex0));
    vertices.push_back(fzgx_to_godot_vec3(tri.vertex1));
    vertices.push_back(fzgx_to_godot_vec3(tri.vertex2));
    normals.push_back(normal);
    normals.push_back(normal);
    normals.push_back(normal);
  }
  for (uint32_t i = 0u; i < quad_count; ++i) {
    const fzgx_static_collider_quad_record &quad = quads[i];
    const Vector3 normal = fzgx_to_godot_vec3(quad.normal);
    const Vector3 v0 = fzgx_to_godot_vec3(quad.vertex0);
    const Vector3 v1 = fzgx_to_godot_vec3(quad.vertex1);
    const Vector3 v2 = fzgx_to_godot_vec3(quad.vertex2);
    const Vector3 v3 = fzgx_to_godot_vec3(quad.vertex3);

    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v0);
    vertices.push_back(v2);
    vertices.push_back(v3);
    for (int tri_vertex = 0; tri_vertex < 6; ++tri_vertex) {
      normals.push_back(normal);
    }
  }
}

static bool fzgx_sample_dynamic_scene_object_world_transform_exact(
    const fzgx_sim_world *world,
    uint32_t machine_index,
    uint32_t object_index,
    const fzgx_owned_dynamic_scene_object_record *object,
    fzgx_mat43 *transform_out) {
  fzgx_transform_trxs_record sampled_transform;

  if ((world == nullptr) || (object == nullptr) || (transform_out == nullptr)) {
    return false;
  }
  if (object->has_transform_matrix != 0u) {
    *transform_out = object->transform_matrix;
    return true;
  }

  sampled_transform = object->transform;
  if (object->has_animation_clip != 0u) {
    const uint16_t mode_pair =
        (uint16_t)(((uint16_t)object->transform.unknown_transform_option << 8) |
                   (uint16_t)object->transform.object_active_override);
    uint32_t source_slot;
    float source_frames;
    float clip_time_seconds;
    float clip_start_frames;
    float clip_end_frames;
    float clip_span_frames;
    float clip_offset_frames;
    const uint32_t flags = object->render_flags_0;

    if ((flags & 0x00040000u) == 0u) {
      source_slot = (uint32_t)((mode_pair >> 12) & 3u);
      source_frames = (float)world->stage_scene_frame_banks[source_slot];
    } else {
      const uint32_t context_mask = world->stage_scene_context_mask;
      int32_t active_machine_index = world->stage_scene_context_active_machine_index;

      if ((flags & 0x000000e0u) != 0u) {
        if ((flags & context_mask) == 0u) {
          return false;
        }
      }
      if (active_machine_index < 0) {
        if (machine_index >= world->machine_count) {
          return false;
        }
        active_machine_index = (int32_t)machine_index;
      }
      if ((uint32_t)active_machine_index >= world->machine_count) {
        return false;
      }
      source_slot = world->stage_scene_context_view_slot;
      if (source_slot >= 4u) {
        return false;
      }
      if (object_index >= world->dynamic_scene_runtime_flag_count) {
        return false;
      }
      source_frames = world->dynamic_scene_clip_bank_time_frames[object_index][source_slot];
      if ((fzgx_float_bits_exact(source_frames) == 0xffffffffu) || ((int32_t)source_frames < 0)) {
        return false;
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
      return false;
    }
  }

  *transform_out = fzgx_mat43_from_transform_trxs_exact(&sampled_transform);
  return true;
}

static fzgx_mat43 fzgx_resolve_dynamic_scene_object_collider_transform_exact(
    const fzgx_owned_dynamic_scene_object_record *object,
    const fzgx_mat43 *object_transform) {
  if ((object != nullptr) && (object->has_transform_matrix == 0u) &&
      (object->has_collision_transform != 0u)) {
    return fzgx_mat43_from_transform_trxs_exact(&object->collision_transform);
  }
  if (object_transform != nullptr) {
    return *object_transform;
  }
  return fzgx_mat43_identity_exact();
}

static bool fzgx_sample_track_mesh_chunk_transform_exact(
    const fzgx_sim_world *world,
    const fzgx_owned_track_mesh_chunk *chunk,
    uint32_t chunk_index,
    fzgx_mat43 *transform_out) {
  fzgx_vec3 current_position;
  uint16_t current_rotation_x;
  uint16_t current_rotation_y;
  uint16_t current_rotation_z;

  if ((world == nullptr) || (chunk == nullptr) || (transform_out == nullptr)) {
    return false;
  }
  if (chunk_index == 0u) {
    *transform_out = fzgx_mat43_identity_exact();
    return true;
  }

  current_position = chunk->unk_vec3_0x0;
  current_rotation_x = chunk->rotation_x_angle16;
  current_rotation_y = chunk->rotation_y_angle16;
  current_rotation_z = chunk->rotation_z_angle16;
  if ((chunk->has_animation_record != 0u) && (chunk->animation_record.has_animation != 0u)) {
    float current_frame = (float)world->frame_index;
    float current_seconds;

    if ((chunk->flags_0x12 & 1u) == 0u) {
      const float span_frames = chunk->time_end_0x10c - chunk->time_start_0x108;

      if (span_frames > 0.0f) {
        current_frame = (current_frame - span_frames * floorf(current_frame / span_frames)) +
                        chunk->time_start_0x108;
      }
    }
    current_seconds = current_frame / 60.0f;
    for (uint32_t channel_index = 0u;
         channel_index < FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT;
         ++channel_index) {
      const fzgx_owned_track_mesh_animation_channel *channel =
          &chunk->animation_record.channels[channel_index];
      fzgx_animation_curve curve;
      float current_value = 0.0f;

      if ((channel->keyable_count == 0u) || (channel->keyables == nullptr)) {
        continue;
      }
      curve.keyable_count = channel->keyable_count;
      curve.keyables = channel->keyables;
      if (fzgx_evaluate_float_animation_curve(&curve, current_seconds, &current_value) !=
          FZGX_STATUS_OK) {
        continue;
      }
      switch (channel_index) {
        case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_X:
          current_rotation_x = fzgx_degrees_to_angle16_exact(current_value);
          break;
        case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Y:
          current_rotation_y = fzgx_degrees_to_angle16_exact(current_value);
          break;
        case FZGX_TRACK_MESH_ANIMATION_CHANNEL_ROTATION_Z:
          current_rotation_z = fzgx_degrees_to_angle16_exact(current_value);
          break;
        case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_X:
          current_position.x = current_value;
          break;
        case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Y:
          current_position.y = current_value;
          break;
        case FZGX_TRACK_MESH_ANIMATION_CHANNEL_POSITION_Z:
          current_position.z = current_value;
          break;
      }
    }
  }

  *transform_out = fzgx_mat43_identity_exact();
  fzgx_mat43_set_origin_exact(transform_out, current_position);
  if (current_rotation_z != 0u) {
    fzgx_mat43_rotate_about_z_right_exact(transform_out, current_rotation_z);
  }
  if (current_rotation_y != 0u) {
    fzgx_mat43_rotate_about_y_right_exact(transform_out, current_rotation_y);
  }
  {
    const int32_t relative_x =
        (int32_t)(int16_t)current_rotation_x - (int32_t)(int16_t)chunk->rotation_x_angle16;
    if (relative_x != 0) {
      fzgx_mat43_rotate_about_x_right_exact(transform_out, (uint16_t)relative_x);
    }
  }
  if (chunk->rotation_y_angle16 != 0u) {
    fzgx_mat43_rotate_about_y_right_exact(
        transform_out, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_y_angle16));
  }
  if (chunk->rotation_z_angle16 != 0u) {
    fzgx_mat43_rotate_about_z_right_exact(
        transform_out, (uint16_t)(-(int32_t)(int16_t)chunk->rotation_z_angle16));
  }
  fzgx_mat43_translate_local_exact(
      transform_out, fzgx_vec3_scale_exact(chunk->unk_vec3_0x0, -1.0f));
  return true;
}

struct AnalyticTrackPieceSample {
  uint32_t segment_address = 0u;
  uint32_t source_piece_word = 0u;
  int32_t branch_index = 0;
  const fzgx_track_segment_record *track_segment = nullptr;
  const fzgx_track_segment_animation_record *animation_segment = nullptr;
  float time = 0.0f;
  fzgx_mat43 transform = {};
  fzgx_vec3 scale = {};
};

struct AnalyticTrackDebugSample {
  int32_t checkpoint_index = -1;
  float checkpoint_fraction = 0.0f;
  float uv_v = 0.0f;
  std::vector<AnalyticTrackPieceSample> pieces = {};
};

static void fzgx_append_quad(
    PackedVector3Array &vertices,
    PackedVector3Array &normals,
    PackedVector2Array &uvs,
    const Vector3 &a0,
    const Vector3 &a1,
    const Vector3 &b0,
    const Vector3 &b1,
    float u0,
    float u1,
    float v0,
    float v1);

static bool fzgx_track_segment_is_skipped_in_cached_frame_walk(uint32_t source_piece_word) {
  return (source_piece_word & 0x001e0002u) != 0u;
}

static bool fzgx_track_segment_is_renderable_surface(uint32_t source_piece_word) {
  return (source_piece_word & 0x03e00000u) != 0u;
}

struct AnalyticTrackPieceMatch {
  uint32_t piece_index_a = 0u;
  uint32_t piece_index_b = 0u;
};

static std::vector<AnalyticTrackPieceMatch> fzgx_match_track_piece_samples(
    const std::vector<AnalyticTrackPieceSample> &pieces_a,
    const std::vector<AnalyticTrackPieceSample> &pieces_b,
    bool allow_generated_fallback_matching) {
  std::vector<AnalyticTrackPieceMatch> matches;
  std::vector<bool> matched_a(pieces_a.size(), false);
  std::vector<bool> matched_b(pieces_b.size(), false);

  for (uint32_t index_a = 0u; index_a < pieces_a.size(); ++index_a) {
    for (uint32_t index_b = 0u; index_b < pieces_b.size(); ++index_b) {
      if (matched_b[index_b]) {
        continue;
      }
      if (pieces_a[index_a].segment_address == pieces_b[index_b].segment_address) {
        matches.push_back({index_a, index_b});
        matched_a[index_a] = true;
        matched_b[index_b] = true;
        break;
      }
    }
  }

  if (!allow_generated_fallback_matching) {
    return matches;
  }

  std::vector<uint32_t> unmatched_a;
  std::vector<uint32_t> unmatched_b;
  unmatched_a.reserve(pieces_a.size());
  unmatched_b.reserve(pieces_b.size());
  for (uint32_t index_a = 0u; index_a < pieces_a.size(); ++index_a) {
    if (!matched_a[index_a]) {
      unmatched_a.push_back(index_a);
    }
  }
  for (uint32_t index_b = 0u; index_b < pieces_b.size(); ++index_b) {
    if (!matched_b[index_b]) {
      unmatched_b.push_back(index_b);
    }
  }
  if (unmatched_a.empty() || unmatched_b.empty()) {
    return matches;
  }

  if ((unmatched_a.size() == 1u) && (unmatched_b.size() > 1u)) {
    for (uint32_t index_b : unmatched_b) {
      matches.push_back({unmatched_a[0], index_b});
    }
    return matches;
  }
  if ((unmatched_b.size() == 1u) && (unmatched_a.size() > 1u)) {
    for (uint32_t index_a : unmatched_a) {
      matches.push_back({index_a, unmatched_b[0]});
    }
    return matches;
  }

  for (uint32_t index_a : unmatched_a) {
    if (matched_a[index_a]) {
      continue;
    }
    for (uint32_t index_b : unmatched_b) {
      if (matched_b[index_b]) {
        continue;
      }
      if (pieces_a[index_a].branch_index == pieces_b[index_b].branch_index) {
        matches.push_back({index_a, index_b});
        matched_a[index_a] = true;
        matched_b[index_b] = true;
        break;
      }
    }
  }

  unmatched_a.clear();
  unmatched_b.clear();
  for (uint32_t index_a = 0u; index_a < pieces_a.size(); ++index_a) {
    if (!matched_a[index_a]) {
      unmatched_a.push_back(index_a);
    }
  }
  for (uint32_t index_b = 0u; index_b < pieces_b.size(); ++index_b) {
    if (!matched_b[index_b]) {
      unmatched_b.push_back(index_b);
    }
  }
  if (unmatched_a.size() != unmatched_b.size()) {
    return matches;
  }
  for (uint32_t index = 0u; index < unmatched_a.size(); ++index) {
    matches.push_back({unmatched_a[index], unmatched_b[index]});
  }
  return matches;
}

static fzgx_status fzgx_collect_direct_track_piece_samples_recursive(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    float time,
    const fzgx_mat43 *parent_transform,
    const fzgx_vec3 *parent_scale,
    std::vector<AnalyticTrackPieceSample> *pieces_out) {
  const fzgx_track_segment_animation_record *animation_segment = nullptr;
  const fzgx_track_segment_record *children = nullptr;
  fzgx_mat43 transform;
  fzgx_vec3 scale;
  uint32_t source_piece_word = 0u;
  uint32_t child_count = 0u;
  fzgx_status status;

  if ((course == nullptr) || (track_segment == nullptr) || (parent_transform == nullptr) ||
      (parent_scale == nullptr) || (pieces_out == nullptr)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (fzgx_track_segment_is_skipped_in_cached_frame_walk(source_piece_word)) {
    return FZGX_STATUS_OK;
  }

  transform = *parent_transform;
  scale = *parent_scale;
  if (animation_course != nullptr) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = nullptr;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((source_piece_word & 0x00600000u) == 0u) {
    status = fzgx_track_segment_apply_trs(
        track_segment, animation_segment, time, &transform, &scale, nullptr);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if (fzgx_track_segment_is_renderable_surface(source_piece_word)) {
    AnalyticTrackPieceSample piece = {};

    piece.segment_address = track_segment->address;
    piece.source_piece_word = source_piece_word;
    piece.branch_index = track_segment->branch_index;
    piece.track_segment = track_segment;
    piece.animation_segment = animation_segment;
    piece.time = time;
    piece.transform = transform;
    piece.scale = scale;
    pieces_out->push_back(piece);
  }

  status = fzgx_track_course_get_track_segment_children(
      course, track_segment, &children, &child_count);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  for (uint32_t child_index = 0u; child_index < child_count; ++child_index) {
    status = fzgx_collect_direct_track_piece_samples_recursive(
        course,
        animation_course,
        &children[child_index],
        time,
        &transform,
        &scale,
        pieces_out);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_build_analytic_track_debug_sample_exact(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    int32_t checkpoint_index,
    float checkpoint_fraction,
    float uv_v,
    AnalyticTrackDebugSample *sample_out) {
  const fzgx_track_segment_record *root_segment = nullptr;
  fzgx_mat43 identity;
  fzgx_vec3 unit_scale = {1.0f, 1.0f, 1.0f};
  float curve_time = 0.0f;
  fzgx_status status;

  if ((course == nullptr) || (sample_out == nullptr) || (checkpoint_index < 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_track_course_compute_curve_time_for_checkpoint_fraction(
      course, (uint32_t)checkpoint_index, checkpoint_fraction, &curve_time);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_track_course_get_root_segment_for_track_node(
      course, (uint32_t)checkpoint_index, &root_segment);
  if ((status != FZGX_STATUS_OK) || (root_segment == nullptr)) {
    return status;
  }

  identity = fzgx_mat43_identity_exact();
  sample_out->checkpoint_index = checkpoint_index;
  sample_out->checkpoint_fraction = checkpoint_fraction;
  sample_out->uv_v = uv_v;
  sample_out->pieces.clear();
  sample_out->pieces.reserve(16u);

  return fzgx_collect_direct_track_piece_samples_recursive(
      course,
      animation_course,
      root_segment,
      curve_time,
      &identity,
      &unit_scale,
      &sample_out->pieces);
}

static void fzgx_append_analytic_track_debug_span(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const AnalyticTrackDebugSample &sample_a,
    const AnalyticTrackDebugSample &sample_b,
    bool allow_generated_fallback_matching,
    PackedVector3Array &vertices,
    PackedVector3Array &normals,
    PackedVector2Array &uvs) {
  static constexpr float kRoadLateralMin = -0.5f;
  static constexpr float kRoadLateralMax = 0.5f;
  static constexpr uint32_t kRoadLateralSubdivisionCount = 32u;
  static constexpr uint32_t kCapsuleLateralSubdivisionCount = 64u;
  static constexpr uint32_t kRoundPipeLateralSubdivisionCount = 32u;

  const std::vector<AnalyticTrackPieceMatch> piece_matches = fzgx_match_track_piece_samples(
      sample_a.pieces, sample_b.pieces, allow_generated_fallback_matching);

  for (const AnalyticTrackPieceMatch &piece_match : piece_matches) {
    const AnalyticTrackPieceSample &piece_a = sample_a.pieces[piece_match.piece_index_a];
    const AnalyticTrackPieceSample &piece_b = sample_b.pieces[piece_match.piece_index_b];
    const uint32_t combined_flags = piece_a.source_piece_word | piece_b.source_piece_word;
    const bool is_round_pipe_family = (combined_flags & 0x01800000u) != 0u;
    const bool is_capsule_family = (combined_flags & 0x00400000u) != 0u;
    const uint32_t lateral_subdivision_count =
        is_round_pipe_family ? kRoundPipeLateralSubdivisionCount
                             : (is_capsule_family ? kCapsuleLateralSubdivisionCount
                                                  : kRoadLateralSubdivisionCount);

    for (uint32_t lateral_index = 0u; lateral_index < lateral_subdivision_count; ++lateral_index) {
      float alpha0 = (float)lateral_index / (float)lateral_subdivision_count;
      float alpha1 = (float)(lateral_index + 1u) / (float)lateral_subdivision_count;
      float lateral0 = kRoadLateralMin + (kRoadLateralMax - kRoadLateralMin) * alpha0;
      float lateral1 = kRoadLateralMin + (kRoadLateralMax - kRoadLateralMin) * alpha1;
      fzgx_vec3 a0_native = {};
      fzgx_vec3 a1_native = {};
      fzgx_vec3 b0_native = {};
      fzgx_vec3 b1_native = {};

      if ((fzgx_track_segment_sample_surface_world_pos(
               course,
               animation_course,
               piece_a.track_segment,
               piece_a.animation_segment,
               piece_a.time,
               &piece_a.transform,
               &piece_a.scale,
               piece_a.source_piece_word,
               lateral0,
               &a0_native) != FZGX_STATUS_OK) ||
          (fzgx_track_segment_sample_surface_world_pos(
               course,
               animation_course,
               piece_a.track_segment,
               piece_a.animation_segment,
               piece_a.time,
               &piece_a.transform,
               &piece_a.scale,
               piece_a.source_piece_word,
               lateral1,
               &a1_native) != FZGX_STATUS_OK) ||
          (fzgx_track_segment_sample_surface_world_pos(
               course,
               animation_course,
               piece_b.track_segment,
               piece_b.animation_segment,
               piece_b.time,
               &piece_b.transform,
               &piece_b.scale,
               piece_b.source_piece_word,
               lateral0,
               &b0_native) != FZGX_STATUS_OK) ||
          (fzgx_track_segment_sample_surface_world_pos(
               course,
               animation_course,
               piece_b.track_segment,
               piece_b.animation_segment,
               piece_b.time,
               &piece_b.transform,
               &piece_b.scale,
               piece_b.source_piece_word,
               lateral1,
               &b1_native) != FZGX_STATUS_OK)) {
        continue;
      }

      fzgx_append_quad(
          vertices,
          normals,
          uvs,
          fzgx_to_godot_vec3(a0_native),
          fzgx_to_godot_vec3(a1_native),
          fzgx_to_godot_vec3(b0_native),
          fzgx_to_godot_vec3(b1_native),
          alpha0,
          alpha1,
          sample_a.uv_v,
          sample_b.uv_v);
    }
  }
}

static Vector3 fzgx_safe_triangle_normal(
    const Vector3 &a,
    const Vector3 &b,
    const Vector3 &c) {
  Vector3 normal = (b - a).cross(c - a);

  if (normal.length_squared() <= 1.0e-8f) {
    return Vector3(0.0, 1.0, 0.0);
  }
  return normal.normalized();
}

static void fzgx_append_triangle(
    PackedVector3Array &vertices,
    PackedVector3Array &normals,
    PackedVector2Array &uvs,
    const Vector3 &a,
    const Vector3 &b,
    const Vector3 &c,
    const Vector2 &uv_a,
    const Vector2 &uv_b,
    const Vector2 &uv_c) {
  Vector3 normal = fzgx_safe_triangle_normal(a, b, c);

  vertices.push_back(a);
  vertices.push_back(b);
  vertices.push_back(c);
  normals.push_back(normal);
  normals.push_back(normal);
  normals.push_back(normal);
  uvs.push_back(uv_a);
  uvs.push_back(uv_b);
  uvs.push_back(uv_c);
}

static void fzgx_append_quad(
    PackedVector3Array &vertices,
    PackedVector3Array &normals,
    PackedVector2Array &uvs,
    const Vector3 &a0,
    const Vector3 &a1,
    const Vector3 &b0,
    const Vector3 &b1,
    float u0,
    float u1,
    float v0,
    float v1) {
  const Vector2 uv_a0(u0, v0);
  const Vector2 uv_a1(u1, v0);
  const Vector2 uv_b0(u0, v1);
  const Vector2 uv_b1(u1, v1);

  fzgx_append_triangle(vertices, normals, uvs, a0, a1, b1, uv_a0, uv_a1, uv_b1);
  fzgx_append_triangle(vertices, normals, uvs, a0, b1, b0, uv_a0, uv_b1, uv_b0);
}

static fzgx_status fzgx_track_course_eval_shared_checkpoint_distance_for_debug_exact(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    float checkpoint_fraction,
    double *distance_out) {
  const fzgx_checkpoint_record *checkpoint = nullptr;
  fzgx_status status;

  if ((course == nullptr) || (distance_out == nullptr)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_track_course_get_checkpoint_variant(course, track_node_index, 0u, &checkpoint);
  if ((status != FZGX_STATUS_OK) || (checkpoint == nullptr)) {
    return status;
  }

  *distance_out = (double)checkpoint->start_distance +
                  (double)checkpoint_fraction *
                      (double)(checkpoint->end_distance - checkpoint->start_distance);
  return FZGX_STATUS_OK;
}

static Dictionary fzgx_build_checkpoint_debug_entry_exact(
    const fzgx_checkpoint_record &checkpoint,
    int32_t checkpoint_index,
    uint32_t variant_index,
    uint32_t variant_count,
    const fzgx_track_frame_record *frame) {
  Dictionary entry;
  const Vector3 start_origin = fzgx_to_godot_vec3(checkpoint.plane_start.origin);
  const Vector3 end_origin = fzgx_to_godot_vec3(checkpoint.plane_end.origin);
  Vector3 forward = fzgx_normalized_or_fallback(
      fzgx_to_godot_vec3(checkpoint.plane_start.normal),
      Vector3(0.0, 0.0, -1.0));
  Vector3 up = Vector3(0.0, 1.0, 0.0);
  Vector3 right = Vector3(1.0, 0.0, 0.0);
  float frame_width_or_radius = checkpoint.track_width;
  int32_t frame_index = (int32_t)variant_index;
  int64_t frame_flags = 0;

  if (frame != nullptr) {
    forward = fzgx_normalized_or_fallback(
        fzgx_to_godot_vec3(frame->track_forward), forward);
    up = fzgx_normalized_or_fallback(
        fzgx_to_godot_vec3(frame->track_up), up);
    right = fzgx_normalized_or_fallback(
        fzgx_to_godot_vec3(fzgx_mat43_get_basis_x_exact(&frame->track_current_transform)),
        up.cross(forward));
    up = fzgx_normalized_or_fallback(forward.cross(right), up);
    frame_width_or_radius = frame->track_width_or_radius;
    frame_flags = (int64_t)frame->track_flags;
  } else {
    right = fzgx_normalized_or_fallback(
        up.cross(forward),
        Vector3(1.0, 0.0, 0.0));
    up = fzgx_normalized_or_fallback(forward.cross(right), up);
    frame_index = -1;
  }

  entry["checkpoint_index"] = checkpoint_index;
  entry["variant_index"] = (int64_t)variant_index;
  entry["variant_count"] = (int64_t)variant_count;
  entry["frame_index"] = frame_index;
  entry["connect_to_track_in"] = checkpoint.connect_to_track_in != 0u;
  entry["connect_to_track_out"] = checkpoint.connect_to_track_out != 0u;
  entry["track_width"] = checkpoint.track_width;
  entry["frame_width_or_radius"] = frame_width_or_radius;
  entry["frame_scale_y"] = frame != nullptr ? frame->track_current_scale.y : 0.0f;
  entry["frame_surface_scale_y"] = frame != nullptr ? frame->track_scl_y : 0.0f;
  entry["start_distance"] = checkpoint.start_distance;
  entry["end_distance"] = checkpoint.end_distance;
  entry["start_origin"] = start_origin;
  entry["end_origin"] = end_origin;
  entry["center"] = start_origin.lerp(end_origin, 0.5f);
  entry["plane_normal"] = fzgx_to_godot_vec3(checkpoint.plane_start.normal);
  entry["track_forward"] = forward;
  entry["track_up"] = up;
  entry["track_right"] = right;
  entry["frame_flags"] = frame_flags;
  return entry;
}

static const char *fzgx_lookup_track_name(uint32_t authored_track_id) {
  switch (authored_track_id) {
    case 0u:
      return "Screw Drive (test)";
    case 1u:
      return "Mute City - Twist Road";
    case 3u:
      return "Mute City - Serial Gaps";
    case 5u:
      return "Aeropolis - Multiplex";
    case 7u:
      return "Port Town - Aero Dive";
    case 8u:
      return "Lightning - Loop Cross";
    case 9u:
      return "Lightning - Half Pipe";
    case 10u:
      return "Green Plant - Intersection";
    case 11u:
      return "Green Plant - Mobius Ring";
    case 13u:
      return "Port Town - Long Pipe";
    case 14u:
      return "Big Blue - Drift Highway";
    case 15u:
      return "Fire Field - Cylinder Knot";
    case 16u:
      return "Casino Palace - Split Oval";
    case 17u:
      return "Fire Field - Undulation";
    case 21u:
      return "Aeropolis - Dragon Slope";
    case 24u:
      return "Cosmo Terminal - Trident";
    case 25u:
      return "Sand Ocean - Lateral Shift";
    case 26u:
      return "Sand Ocean - Surface Slide";
    case 27u:
      return "Big Blue - Ordeal";
    case 28u:
      return "Phantom Road - Slim-Line Slits";
    case 29u:
      return "Casino Palace - Double Branches";
    case 31u:
      return "Aeropolis - Screw Drive";
    case 32u:
      return "Outer Space - Meteor Stream";
    case 33u:
      return "Port Town - Cylinder Wave";
    case 34u:
      return "Lightning - Thunder Road";
    case 35u:
      return "Green Plant - Spiral";
    case 36u:
      return "Mute City - Sonic Oval";
    case 37u:
      return "Story 1 - Captain Falcon Trains";
    case 38u:
      return "Story 2 - Goroh: The Vengeful Samurai";
    case 39u:
      return "Story 3 - High Stakes in Mute City";
    case 40u:
      return "Story 4 - Challenge of the Bloody Chain";
    case 41u:
      return "Story 5 - Save Jody!";
    case 42u:
      return "Story 6 - Black Shadow's Trap";
    case 43u:
      return "Story 7 - The F-Zero Grand Prix";
    case 44u:
      return "Story 8 - Secrets of the Champion Belt";
    case 45u:
      return "Story 9 - Finale: Enter the Creators";
    case 49u:
      return "Grand Prix Podium";
    case 50u:
      return "Victory Lap";
    default:
      return nullptr;
  }
}

static String fzgx_format_track_label(const fzgx_track_manifest &track, uint32_t track_index) {
  char buffer[64];

  std::snprintf(
      buffer,
      sizeof(buffer),
      "#%02u  COLI_COURSE%02u  %s",
      track_index,
      track.authored_track_id,
      (track.circuit_type == FZGX_CIRCUIT_TYPE_OPEN) ? "OPEN" : "CLOSED");
  return String(buffer);
}

static String fzgx_format_machine_label(
    const fzgx_machine_definition &machine,
    uint32_t machine_index) {
  if (machine.name[0] != '\0') {
    return String(machine.name);
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "Machine %02u", machine_index);
  return String(buffer);
}

static std::string fzgx_string_to_utf8_std(const String &value) {
  CharString utf8 = value.utf8();

  return std::string(utf8.get_data());
}

struct FzgxStageCatalogEntry {
  enum Kind {
    KIND_STAGE_BYTES = 0,
    KIND_GENERATED_TRACK = 1,
  };

  Kind kind = KIND_STAGE_BYTES;
  PackedByteArray bytes;
  Dictionary generated_data;
  String label;
  fzgx_track_manifest manifest = {};
};

static std::filesystem::path fzgx_resolve_project_resource_dir_exact(const char *resource_path) {
  ProjectSettings *project_settings = ProjectSettings::get_singleton();

  if ((project_settings == nullptr) || (resource_path == nullptr) || (resource_path[0] == '\0')) {
    return std::filesystem::path();
  }

  const std::string absolute_path =
      fzgx_string_to_utf8_std(project_settings->globalize_path(String(resource_path)));
  if (absolute_path.empty()) {
    return std::filesystem::path();
  }
  return std::filesystem::path(absolute_path);
}

static bool fzgx_variant_to_double_exact(const Variant &value, double *value_out) {
  if (value_out == nullptr) {
    return false;
  }
  if (value.get_type() == Variant::FLOAT) {
    *value_out = (double)value;
    return true;
  }
  if (value.get_type() == Variant::INT) {
    *value_out = (double)(int64_t)value;
    return true;
  }
  return false;
}

static bool fzgx_dictionary_read_u32_exact(
    const Dictionary &dict,
    const char *key,
    uint32_t *value_out) {
  const Variant value = dict.get(String(key), Variant());
  double raw_value = 0.0;

  if ((value_out == nullptr) || !fzgx_variant_to_double_exact(value, &raw_value) ||
      !std::isfinite(raw_value) || (raw_value < 0.0) || (raw_value > 4294967295.0) ||
      (std::floor(raw_value) != raw_value)) {
    return false;
  }
  *value_out = (uint32_t)raw_value;
  return true;
}

static bool fzgx_dictionary_read_u8_exact(
    const Dictionary &dict,
    const char *key,
    uint8_t *value_out) {
  uint32_t value = 0u;

  if ((value_out == nullptr) || !fzgx_dictionary_read_u32_exact(dict, key, &value) ||
      (value > 0xffu)) {
    return false;
  }
  *value_out = (uint8_t)value;
  return true;
}

static bool fzgx_dictionary_read_float_exact(
    const Dictionary &dict,
    const char *key,
    float *value_out) {
  const Variant value = dict.get(String(key), Variant());
  double raw_value = 0.0;

  if ((value_out == nullptr) || !fzgx_variant_to_double_exact(value, &raw_value) ||
      !std::isfinite(raw_value)) {
    return false;
  }
  *value_out = (float)raw_value;
  return true;
}

static bool fzgx_dictionary_read_string_exact(
    const Dictionary &dict,
    const char *key,
    String *value_out) {
  const Variant value = dict.get(String(key), Variant());

  if ((value_out == nullptr) || (value.get_type() != Variant::STRING)) {
    return false;
  }
  *value_out = (String)value;
  return true;
}

static bool fzgx_dictionary_read_bool_exact(
    const Dictionary &dict,
    const char *key,
    bool *value_out) {
  const Variant value = dict.get(String(key), Variant());

  if (value_out == nullptr) {
    return false;
  }
  if (value.get_type() == Variant::BOOL) {
    *value_out = (bool)value;
    return true;
  }
  if (value.get_type() == Variant::INT) {
    *value_out = ((int64_t)value) != 0;
    return true;
  }
  return false;
}

static bool fzgx_dictionary_read_packed_byte_array_exact(
    const Dictionary &dict,
    const char *key,
    PackedByteArray *value_out) {
  const Variant value = dict.get(String(key), Variant());

  if ((value_out == nullptr) || (value.get_type() != Variant::PACKED_BYTE_ARRAY)) {
    return false;
  }
  *value_out = (PackedByteArray)value;
  return true;
}

static bool fzgx_parse_vec3_array_exact(const Variant &value, fzgx_vec3 *vec_out) {
  Array array;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  if ((vec_out == nullptr) || (value.get_type() != Variant::ARRAY)) {
    return false;
  }
  array = (Array)value;
  if ((int64_t)array.size() != 3) {
    return false;
  }
  if (!fzgx_variant_to_double_exact(array[0], &x) ||
      !fzgx_variant_to_double_exact(array[1], &y) ||
      !fzgx_variant_to_double_exact(array[2], &z) ||
      !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    return false;
  }
  vec_out->x = (float)x;
  vec_out->y = (float)y;
  vec_out->z = (float)z;
  return true;
}

static bool fzgx_dictionary_read_vec3_array_table_exact(
    const Dictionary &dict,
    const char *key,
    fzgx_vec3 *values_out,
    uint32_t value_count) {
  const Variant value = dict.get(String(key), Variant());
  Array array;

  if ((values_out == nullptr) || (value.get_type() != Variant::ARRAY)) {
    return false;
  }
  array = (Array)value;
  if ((uint32_t)array.size() != value_count) {
    return false;
  }
  for (uint32_t i = 0u; i < value_count; ++i) {
    if (!fzgx_parse_vec3_array_exact(array[(int32_t)i], &values_out[i])) {
      return false;
    }
  }
  return true;
}

static bool fzgx_parse_machine_definition_json_exact(
    const Dictionary &dict,
    fzgx_machine_definition *machine_out) {
  String name;
  std::string name_utf8;
  bool is_custom_machine = false;

  if (machine_out == nullptr) {
    return false;
  }
  std::memset(machine_out, 0, sizeof(*machine_out));
  if (!fzgx_dictionary_read_u32_exact(dict, "machine_id", &machine_out->machine_id) ||
      !fzgx_dictionary_read_float_exact(dict, "weight", &machine_out->weight) ||
      !fzgx_dictionary_read_float_exact(dict, "acceleration", &machine_out->acceleration) ||
      !fzgx_dictionary_read_float_exact(dict, "max_speed", &machine_out->max_speed) ||
      !fzgx_dictionary_read_float_exact(dict, "grip_1", &machine_out->grip_1) ||
      !fzgx_dictionary_read_float_exact(dict, "grip_3", &machine_out->grip_3) ||
      !fzgx_dictionary_read_float_exact(dict, "turn_tension", &machine_out->turn_tension) ||
      !fzgx_dictionary_read_float_exact(dict, "drift_accel", &machine_out->drift_accel) ||
      !fzgx_dictionary_read_float_exact(dict, "turn_movement", &machine_out->turn_movement) ||
      !fzgx_dictionary_read_float_exact(dict, "strafe_turn", &machine_out->strafe_turn) ||
      !fzgx_dictionary_read_float_exact(dict, "strafe", &machine_out->strafe) ||
      !fzgx_dictionary_read_float_exact(dict, "turn_reaction", &machine_out->turn_reaction) ||
      !fzgx_dictionary_read_float_exact(dict, "grip_2", &machine_out->grip_2) ||
      !fzgx_dictionary_read_float_exact(dict, "boost_strength", &machine_out->boost_strength) ||
      !fzgx_dictionary_read_float_exact(dict, "boost_length", &machine_out->boost_length) ||
      !fzgx_dictionary_read_float_exact(dict, "turn_decel", &machine_out->turn_decel) ||
      !fzgx_dictionary_read_float_exact(dict, "drag", &machine_out->drag) ||
      !fzgx_dictionary_read_float_exact(dict, "body", &machine_out->body) ||
      !fzgx_dictionary_read_u8_exact(
          dict, "grip_frames_from_accel_press", &machine_out->grip_frames_from_accel_press) ||
      !fzgx_dictionary_read_u8_exact(dict, "state_flags", &machine_out->state_flags) ||
      !fzgx_dictionary_read_u8_exact(
          dict, "reserved_stat_0x4a", &machine_out->reserved_stat_0x4a) ||
      !fzgx_dictionary_read_u8_exact(
          dict, "reserved_stat_0x4b", &machine_out->reserved_stat_0x4b) ||
      !fzgx_dictionary_read_float_exact(
          dict, "camera_reorienting", &machine_out->camera_reorienting) ||
      !fzgx_dictionary_read_float_exact(
          dict, "camera_repositioning", &machine_out->camera_repositioning) ||
      !fzgx_dictionary_read_vec3_array_table_exact(
          dict,
          "suspension_offsets",
          &machine_out->suspension_offsets[0],
          4u) ||
      !fzgx_dictionary_read_vec3_array_table_exact(
          dict,
          "wall_offsets",
          &machine_out->wall_offsets[0],
          4u) ||
      !fzgx_dictionary_read_bool_exact(dict, "is_custom_machine", &is_custom_machine) ||
      !fzgx_dictionary_read_string_exact(dict, "name", &name)) {
    return false;
  }

  machine_out->is_custom_machine = is_custom_machine ? 1u : 0u;
  name_utf8 = fzgx_string_to_utf8_std(name);
  std::strncpy(machine_out->name, name_utf8.c_str(), sizeof(machine_out->name) - 1u);
  machine_out->name[sizeof(machine_out->name) - 1u] = '\0';
  return true;
}

static bool fzgx_parse_machine_definition_bytes_exact(
    const PackedByteArray &bytes,
    fzgx_machine_definition *machine_out) {
  String json_text;
  Variant parsed_json;

  if ((machine_out == nullptr) || (bytes.size() <= 0)) {
    return false;
  }
  json_text = String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
  parsed_json = JSON::parse_string(json_text);
  if (parsed_json.get_type() != Variant::DICTIONARY) {
    return false;
  }
  return fzgx_parse_machine_definition_json_exact((Dictionary)parsed_json, machine_out);
}

static std::filesystem::path fzgx_resolve_project_replay_dir() {
  return fzgx_resolve_project_resource_dir_exact("res://replays");
}

static constexpr int32_t FZGX_BRIDGE_STATUS_IO_ERROR = -1;
static constexpr int32_t FZGX_BRIDGE_STATUS_INVALID_STATE_SLOT = -2;
static constexpr char FZGX_SHARED_STATE_SLOT_MAGIC[8] = {'F', 'Z', 'G', 'X', 'S', 'L', 'T', '1'};
static constexpr uint32_t FZGX_SHARED_STATE_SLOT_VERSION = 1u;

struct FzgxSharedStateSlotFileHeader {
  char magic[8];
  uint32_t version;
  uint32_t sim_api_version;
  uint32_t world_snapshot_size;
  uint32_t camera_runtime_size;
  uint32_t camera_render_state_size;
  int32_t track_index;
  int32_t machine_index;
  int32_t machine_setting_percent;
  uint32_t reserved0;
  fzgx_replay_camera_render_state camera_render_state;
};

static_assert(
    std::is_trivially_copyable_v<FzgxSharedStateSlotFileHeader>,
    "savestate header must be trivially copyable");
static_assert(
    std::is_trivially_copyable_v<fzgx_world_snapshot>,
    "world snapshot must be trivially copyable");
static_assert(
    std::is_trivially_copyable_v<fzgx_game_camera_runtime>,
    "camera runtime must be trivially copyable");

static std::filesystem::path fzgx_resolve_project_shared_state_slot_path() {
  ProjectSettings *project_settings = ProjectSettings::get_singleton();

  if (project_settings != nullptr) {
    const std::string slot_path = fzgx_string_to_utf8_std(
        project_settings->globalize_path(String("user://savestates/shared_slot.bin")));
    if (!slot_path.empty()) {
      return std::filesystem::path(slot_path);
    }
  }
  return std::filesystem::path();
}

struct ReplayCapturedFrame {
  fzgx_control_sample control = {};
  fzgx_game_camera_input camera_input = {};
};

static uint32_t fzgx_float_bits_exact(float value) {
  uint32_t bits = 0u;

  static_assert(sizeof(bits) == sizeof(value), "float/u32 bit cast must be 32-bit");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static std::string fzgx_format_hex_u32(uint32_t value) {
  char buffer[16];

  std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
  return std::string(buffer);
}

static String fzgx_format_hex_float_string_exact(float value) {
  return String(fzgx_format_hex_u32(fzgx_float_bits_exact(value)).c_str());
}

static bool fzgx_parse_hex_u32_exact(const String &text, uint32_t *value_out) {
  const std::string native = fzgx_string_to_utf8_std(text.strip_edges());
  char *end = nullptr;
  unsigned long long parsed = 0ull;

  if (value_out == nullptr) {
    return false;
  }
  if (native.empty()) {
    return false;
  }
  parsed = std::strtoull(native.c_str(), &end, 0);
  if ((end == nullptr) || (*end != '\0') || (parsed > 0xffffffffull)) {
    return false;
  }
  *value_out = (uint32_t)parsed;
  return true;
}

static bool fzgx_parse_hex_float_exact(const String &text, float *value_out) {
  uint32_t bits = 0u;

  if ((value_out == nullptr) || !fzgx_parse_hex_u32_exact(text, &bits)) {
    return false;
  }
  std::memcpy(value_out, &bits, sizeof(bits));
  return true;
}

static bool fzgx_dictionary_read_hex_float_exact(
    const Dictionary &dict,
    const char *key,
    float *value_out) {
  String text;

  if (!fzgx_dictionary_read_string_exact(dict, key, &text)) {
    return false;
  }
  return fzgx_parse_hex_float_exact(text, value_out);
}

static Array fzgx_build_vec3_hex_array_exact(const fzgx_vec3 &value) {
  Array array;

  array.resize(3);
  array[0] = fzgx_format_hex_float_string_exact(value.x);
  array[1] = fzgx_format_hex_float_string_exact(value.y);
  array[2] = fzgx_format_hex_float_string_exact(value.z);
  return array;
}

static bool fzgx_parse_vec3_hex_array_exact(const Variant &value, fzgx_vec3 *vec_out) {
  Array array;
  String x_text;
  String y_text;
  String z_text;

  if ((vec_out == nullptr) || (value.get_type() != Variant::ARRAY)) {
    return false;
  }
  array = (Array)value;
  if ((int64_t)array.size() != 3) {
    return false;
  }
  if ((array[0].get_type() != Variant::STRING) ||
      (array[1].get_type() != Variant::STRING) ||
      (array[2].get_type() != Variant::STRING)) {
    return false;
  }
  x_text = (String)array[0];
  y_text = (String)array[1];
  z_text = (String)array[2];
  return fzgx_parse_hex_float_exact(x_text, &vec_out->x) &&
         fzgx_parse_hex_float_exact(y_text, &vec_out->y) &&
         fzgx_parse_hex_float_exact(z_text, &vec_out->z);
}

static Dictionary fzgx_build_track_manifest_dictionary_exact(const fzgx_track_manifest &manifest) {
  Dictionary dict;

  dict["authored_track_id"] = (int64_t)manifest.authored_track_id;
  dict["checkpoint_count"] = (int64_t)manifest.checkpoint_count;
  dict["checkpoint_variant_count"] = (int64_t)manifest.checkpoint_variant_count;
  dict["circuit_type"] = (int64_t)manifest.circuit_type;
  dict["time_extension_trigger_count"] = (int64_t)manifest.time_extension_trigger_count;
  dict["supports_branching"] = manifest.supports_branching != 0u;
  return dict;
}

static bool fzgx_parse_track_manifest_dictionary_exact(
    const Dictionary &dict,
    fzgx_track_manifest *manifest_out) {
  bool supports_branching = false;

  if (manifest_out == nullptr) {
    return false;
  }
  std::memset(manifest_out, 0, sizeof(*manifest_out));
  if (!fzgx_dictionary_read_u32_exact(dict, "authored_track_id", &manifest_out->authored_track_id) ||
      !fzgx_dictionary_read_u32_exact(dict, "checkpoint_count", &manifest_out->checkpoint_count) ||
      !fzgx_dictionary_read_u32_exact(
          dict, "checkpoint_variant_count", &manifest_out->checkpoint_variant_count) ||
      !fzgx_dictionary_read_u32_exact(dict, "circuit_type", &manifest_out->circuit_type) ||
      !fzgx_dictionary_read_u32_exact(
          dict, "time_extension_trigger_count", &manifest_out->time_extension_trigger_count) ||
      !fzgx_dictionary_read_bool_exact(dict, "supports_branching", &supports_branching)) {
    return false;
  }
  manifest_out->supports_branching = supports_branching ? 1u : 0u;
  return true;
}

static Dictionary fzgx_build_random_track_recipe_dictionary_exact(
    const fzgx_random_track_recipe &recipe) {
  Dictionary dict;
  Array nodes;
  uint32_t node_index;

  dict["api_version"] = (int64_t)recipe.api_version;
  dict["seed_hash_low32"] = (int64_t)recipe.seed_hash_low32;
  dict["authored_track_id"] = (int64_t)recipe.authored_track_id;
  dict["node_count"] = (int64_t)recipe.node_count;
  dict["track_total_distance_hex"] = fzgx_format_hex_float_string_exact(recipe.track_total_distance);
  dict["track_min_height_hex"] = fzgx_format_hex_float_string_exact(recipe.track_min_height);
  for (node_index = 0u; node_index < recipe.node_count; ++node_index) {
    const fzgx_random_track_node_recipe &node = recipe.nodes[node_index];
    Dictionary node_dict;
    Array variants;
    uint32_t variant_index;

    node_dict["variant_count"] = (int64_t)node.variant_count;
    node_dict["gap_after_mask"] = (int64_t)node.gap_after_mask;
    node_dict["sharp_after_mask"] = (int64_t)node.sharp_after_mask;
    node_dict["mine_mask"] = (int64_t)node.mine_mask;
    for (variant_index = 0u; variant_index < node.variant_count; ++variant_index) {
      const fzgx_random_track_variant_recipe &variant = node.variants[variant_index];
      Dictionary variant_dict;

      variant_dict["center"] = fzgx_build_vec3_hex_array_exact(variant.center);
      variant_dict["forward"] = fzgx_build_vec3_hex_array_exact(variant.forward);
      variant_dict["up"] = fzgx_build_vec3_hex_array_exact(variant.up);
      variant_dict["width_hex"] = fzgx_format_hex_float_string_exact(variant.width);
      variant_dict["half_height_hex"] =
          fzgx_format_hex_float_string_exact(variant.half_height);
      variant_dict["openness_hex"] =
          fzgx_format_hex_float_string_exact(variant.openness);
      variant_dict["family"] = (int64_t)variant.family;
      variant_dict["base_surface_kind"] = (int64_t)variant.base_surface_kind;
      variant_dict["overlay_surface_kind"] = (int64_t)variant.overlay_surface_kind;
      variants.push_back(variant_dict);
    }
    node_dict["variants"] = variants;
    nodes.push_back(node_dict);
  }
  dict["nodes"] = nodes;
  return dict;
}

static bool fzgx_parse_random_track_recipe_dictionary_exact(
    const Dictionary &dict,
    fzgx_random_track_recipe *recipe_out) {
  Array nodes;
  uint32_t node_count = 0u;

  if (recipe_out == nullptr) {
    return false;
  }
  std::memset(recipe_out, 0, sizeof(*recipe_out));
  if (!fzgx_dictionary_read_u32_exact(dict, "api_version", &recipe_out->api_version) ||
      !fzgx_dictionary_read_u32_exact(dict, "seed_hash_low32", &recipe_out->seed_hash_low32) ||
      !fzgx_dictionary_read_u32_exact(dict, "authored_track_id", &recipe_out->authored_track_id) ||
      !fzgx_dictionary_read_u32_exact(dict, "node_count", &node_count) ||
      !fzgx_dictionary_read_hex_float_exact(
          dict, "track_total_distance_hex", &recipe_out->track_total_distance) ||
      !fzgx_dictionary_read_hex_float_exact(
          dict, "track_min_height_hex", &recipe_out->track_min_height)) {
    return false;
  }
  if (recipe_out->api_version != FZGX_RANDOM_TRACK_API_VERSION) {
    return false;
  }
  if ((dict.get("nodes", Variant()).get_type() != Variant::ARRAY) || (node_count == 0u)) {
    return false;
  }
  nodes = (Array)dict.get("nodes", Variant());
  if ((uint32_t)nodes.size() != node_count) {
    return false;
  }
  recipe_out->node_count = node_count;
  recipe_out->nodes = (fzgx_random_track_node_recipe *)std::calloc(
      recipe_out->node_count, sizeof(*recipe_out->nodes));
  if (recipe_out->nodes == nullptr) {
    return false;
  }
  for (uint32_t node_index = 0u; node_index < recipe_out->node_count; ++node_index) {
    const Variant &node_value = nodes[(int32_t)node_index];
    Dictionary node_dict;
    Array variants;
    uint32_t variant_count = 0u;

    if (node_value.get_type() != Variant::DICTIONARY) {
      fzgx_random_track_release_recipe(recipe_out);
      return false;
    }
    node_dict = (Dictionary)node_value;
    if (!fzgx_dictionary_read_u32_exact(node_dict, "variant_count", &variant_count) ||
        (variant_count == 0u) || (variant_count > FZGX_RANDOM_TRACK_MAX_VARIANTS) ||
        !fzgx_dictionary_read_u8_exact(
            node_dict, "gap_after_mask", &recipe_out->nodes[node_index].gap_after_mask) ||
        !fzgx_dictionary_read_u8_exact(
            node_dict, "sharp_after_mask", &recipe_out->nodes[node_index].sharp_after_mask) ||
        !fzgx_dictionary_read_u8_exact(
            node_dict, "mine_mask", &recipe_out->nodes[node_index].mine_mask) ||
        (node_dict.get("variants", Variant()).get_type() != Variant::ARRAY)) {
      fzgx_random_track_release_recipe(recipe_out);
      return false;
    }
    variants = (Array)node_dict.get("variants", Variant());
    if ((uint32_t)variants.size() != variant_count) {
      fzgx_random_track_release_recipe(recipe_out);
      return false;
    }
    recipe_out->nodes[node_index].variant_count = variant_count;
    for (uint32_t variant_index = 0u; variant_index < variant_count; ++variant_index) {
      const Variant &variant_value = variants[(int32_t)variant_index];
      Dictionary variant_dict;
      fzgx_random_track_variant_recipe &variant =
          recipe_out->nodes[node_index].variants[variant_index];
      uint32_t family = 0u;
      uint32_t base_surface_kind = 0u;
      uint32_t overlay_surface_kind = 0u;
      bool has_openness = false;

      if (variant_value.get_type() != Variant::DICTIONARY) {
        fzgx_random_track_release_recipe(recipe_out);
        return false;
      }
      variant_dict = (Dictionary)variant_value;
      if (!fzgx_parse_vec3_hex_array_exact(variant_dict.get("center", Variant()), &variant.center) ||
          !fzgx_parse_vec3_hex_array_exact(
              variant_dict.get("forward", Variant()), &variant.forward) ||
          !fzgx_parse_vec3_hex_array_exact(variant_dict.get("up", Variant()), &variant.up) ||
          !fzgx_dictionary_read_hex_float_exact(variant_dict, "width_hex", &variant.width) ||
          !fzgx_dictionary_read_hex_float_exact(
              variant_dict, "half_height_hex", &variant.half_height) ||
          !fzgx_dictionary_read_u32_exact(variant_dict, "family", &family) ||
          !fzgx_dictionary_read_u32_exact(
              variant_dict, "base_surface_kind", &base_surface_kind) ||
          !fzgx_dictionary_read_u32_exact(
              variant_dict, "overlay_surface_kind", &overlay_surface_kind)) {
        fzgx_random_track_release_recipe(recipe_out);
        return false;
      }
      variant.family = (uint8_t)family;
      variant.base_surface_kind = (uint8_t)base_surface_kind;
      variant.overlay_surface_kind = (uint8_t)overlay_surface_kind;
      has_openness =
          fzgx_dictionary_read_hex_float_exact(variant_dict, "openness_hex", &variant.openness);
      if (!has_openness) {
        variant.openness =
            ((family == FZGX_RANDOM_TRACK_FAMILY_PIPE_OPEN) ||
             (family == FZGX_RANDOM_TRACK_FAMILY_CYLINDER_OPEN))
                ? 0.5f
                : 1.0f;
      }
    }
  }
  return true;
}

static bool fzgx_parse_generated_track_dictionary_manifest_exact(
    const Dictionary &dict,
    fzgx_track_manifest *manifest_out) {
  Variant manifest_value = dict.get("manifest", Variant());

  if ((manifest_value.get_type() != Variant::DICTIONARY) || (manifest_out == nullptr)) {
    return false;
  }
  return fzgx_parse_track_manifest_dictionary_exact((Dictionary)manifest_value, manifest_out);
}

static bool fzgx_parse_generated_track_dictionary_recipe_exact(
    const Dictionary &dict,
    fzgx_random_track_recipe *recipe_out) {
  Variant recipe_value = dict.get("recipe", Variant());

  if ((recipe_value.get_type() != Variant::DICTIONARY) || (recipe_out == nullptr)) {
    return false;
  }
  return fzgx_parse_random_track_recipe_dictionary_exact((Dictionary)recipe_value, recipe_out);
}

static uint32_t fzgx_hash_seed_text_exact(const std::string &seed_text) {
  uint32_t hash = 2166136261u;

  for (char ch : seed_text) {
    hash ^= (uint8_t)ch;
    hash *= 16777619u;
  }
  return hash;
}

static String fzgx_normalize_seed_text_exact(const String &seed_text) {
  String trimmed = seed_text.strip_edges();
  char buffer[64];

  if (!trimmed.is_empty()) {
    return trimmed;
  }
  std::snprintf(buffer, sizeof(buffer), "seed_%lld", (long long)std::time(nullptr));
  return String(buffer);
}

static void fzgx_write_json_escaped_string(
    std::ofstream &stream,
    const std::string &value) {
  stream << '"';
  for (char ch : value) {
    switch (ch) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if ((unsigned char)ch < 0x20u) {
          char buffer[8];

          std::snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned int)(unsigned char)ch);
          stream << buffer;
        } else {
          stream << ch;
        }
        break;
    }
  }
  stream << '"';
}

static std::string fzgx_format_pose_snapshot_text(
    const fzgx_machine_snapshot &machine,
    uint32_t track_index,
    const String &track_label,
    uint32_t machine_index,
    const String &machine_label,
    uint32_t machine_setting_percent,
    uint32_t frame_index) {
  char buffer[2048];

  std::snprintf(
      buffer,
      sizeof(buffer),
      "fzgx_pose_snapshot\n"
      "track_index=%u\n"
      "track_name=%s\n"
      "machine_index=%u\n"
      "machine_name=%s\n"
      "machine_setting_percent=%u\n"
      "frame_index=%u\n"
      "speed_kmh=%.6f\n"
      "energy=%.6f\n"
      "base_speed=%.6f\n"
      "boost_turbo=%.6f\n"
      "boost_frames=%u\n"
      "boost_frames_manual=%u\n"
      "boost_delay_frame_counter=%u\n"
      "air_time=%u\n"
      "zero_minus_height_above_track=%.6f\n"
      "position=(%.9f, %.9f, %.9f)\n"
      "basis_x=(%.9f, %.9f, %.9f)\n"
      "basis_y=(%.9f, %.9f, %.9f)\n"
      "basis_z=(%.9f, %.9f, %.9f)\n"
      "velocity=(%.9f, %.9f, %.9f)\n"
      "angular_velocity=(%.9f, %.9f, %.9f)\n"
      "surface_normal=(%.9f, %.9f, %.9f)\n"
      "position_bottom=(%.9f, %.9f, %.9f)\n"
      "machine_state_flags=0x%08x\n"
      "state_2_flags=0x%08x\n"
      "terrain_flags=0x%08x\n"
      "floor_surface_flags=0x%08x\n"
      "branch_indicator=0x%08x\n"
      "branch_flags=0x%08x\n"
      "branch_slot=%u\n"
      "control_profile_kind=%u\n"
      "frames_since_start_2=%u\n"
      "current_checkpoint=%d\n"
      "checkpoint_fraction=%.6f\n"
      "track_cur_cp_pointer=%d\n"
      "track_cur_cp_idx=%d\n"
      "track_cur_cp_frac=%.6f\n"
      "track_next_cp_idx=%d\n"
      "track_next_cp_frac=%.6f\n"
      "track_selected_cached_frame_index=%d",
      track_index,
      fzgx_string_to_utf8_std(track_label).c_str(),
      machine_index,
      fzgx_string_to_utf8_std(machine_label).c_str(),
      machine_setting_percent,
      frame_index,
      machine.speed_kmh,
      machine.energy,
      machine.base_speed,
      machine.boost_turbo,
      machine.boost_frames,
      machine.boost_frames_manual,
      machine.boost_delay_frame_counter,
      machine.air_time,
      machine.zero_minus_height_above_track,
      machine.position.x,
      machine.position.y,
      machine.position.z,
      machine.basis_physical.basis_x_x,
      machine.basis_physical.basis_x_y,
      machine.basis_physical.basis_x_z,
      machine.basis_physical.basis_y_x,
      machine.basis_physical.basis_y_y,
      machine.basis_physical.basis_y_z,
      machine.basis_physical.basis_z_x,
      machine.basis_physical.basis_z_y,
      machine.basis_physical.basis_z_z,
      machine.velocity.x,
      machine.velocity.y,
      machine.velocity.z,
      machine.angular_velocity.x,
      machine.angular_velocity.y,
      machine.angular_velocity.z,
      machine.surface_normal.x,
      machine.surface_normal.y,
      machine.surface_normal.z,
      machine.position_bottom.x,
      machine.position_bottom.y,
      machine.position_bottom.z,
      machine.machine_state,
      machine.state_2,
      machine.terrain_flags,
      machine.floor_surface_flags,
      machine.branch_indicator,
      machine.branch_flags,
      machine.branch_slot,
      machine.control_profile_kind,
      machine.frames_since_start_2,
      machine.current_checkpoint,
      machine.checkpoint_fraction,
      machine.track_state.cur_cp_pointer,
      machine.track_state.cur_cp_idx,
      machine.track_state.cur_cp_frac,
      machine.track_state.next_cp_idx,
      machine.track_state.next_cp_frac,
      machine.track_state.selected_cached_frame_index);
  return std::string(buffer);
}

}  // namespace

class FzgxGameBridge : public RefCounted {
  GDCLASS(FzgxGameBridge, RefCounted);

private:
  fzgx_content_bundle runtime_bundle_ = {};
  const fzgx_content_bundle *bundle_ = &runtime_bundle_;
  std::vector<FzgxStageCatalogEntry> stage_catalog_ = {};
  std::vector<fzgx_track_manifest> track_manifests_ = {};
  std::vector<fzgx_machine_definition> machine_definitions_ = {};
  std::vector<String> machine_labels_ = {};
  fzgx_owned_track_course_content active_track_course_ = {};
  fzgx_owned_track_course_animation_content active_track_animation_ = {};
  bool has_active_track_course_ = false;
  bool has_active_track_animation_ = false;
  fzgx_sim_world world_ = {};
  fzgx_game_camera_runtime game_camera_ = {};
  fzgx_race_step_options options_ = {};
  fzgx_replay_loaded_courses loaded_courses_ = {};
  fzgx_replay_camera_render_state camera_render_state_ = {};
  bool session_active_ = false;
  int32_t current_track_index_ = -1;
  int32_t current_machine_index_ = -1;
  int32_t current_machine_setting_percent_ = -1;
  bool has_replay_start_machine_ = false;
  fzgx_machine_snapshot replay_start_machine_ = {};
  uint32_t replay_start_frame_index_ = 0u;
  std::vector<ReplayCapturedFrame> replay_frames_ = {};

  void apply_camera_render_state() {
    fzgx_replay_apply_camera_render_state(&game_camera_, &camera_render_state_);
  }

  void refresh_runtime_bundle_view() {
    runtime_bundle_.api_version = FZGX_CONTENT_API_VERSION;
    runtime_bundle_.track_count = (uint32_t)track_manifests_.size();
    runtime_bundle_.machine_count = (uint32_t)machine_definitions_.size();
    runtime_bundle_.track_course_count = has_active_track_course_ ? 1u : 0u;
    runtime_bundle_.track_animation_course_count = has_active_track_animation_ ? 1u : 0u;
    runtime_bundle_.tracks = track_manifests_.empty() ? nullptr : track_manifests_.data();
    runtime_bundle_.machines = machine_definitions_.empty() ? nullptr : machine_definitions_.data();
    runtime_bundle_.track_courses =
        has_active_track_course_ ? &active_track_course_.course : nullptr;
    runtime_bundle_.track_animations =
        has_active_track_animation_ ? &active_track_animation_.course : nullptr;
  }

  void release_active_track_content() {
    fzgx_content_release_track_course_content(&active_track_course_);
    fzgx_content_release_track_course_animation_content(&active_track_animation_);
    std::memset(&active_track_course_, 0, sizeof(active_track_course_));
    std::memset(&active_track_animation_, 0, sizeof(active_track_animation_));
    has_active_track_course_ = false;
    has_active_track_animation_ = false;
    refresh_runtime_bundle_view();
  }

  int32_t configure_content_catalog(
      const Array &track_entries,
      const Array &machine_entries) {
    auto reset_catalogs = [this]() {
      stage_catalog_.clear();
      track_manifests_.clear();
      machine_definitions_.clear();
      machine_labels_.clear();
      refresh_runtime_bundle_view();
    };

    fzgx_replay_release_loaded_courses(&loaded_courses_);
    clear_replay_capture();
    session_active_ = false;
    current_track_index_ = -1;
    current_machine_index_ = -1;
    current_machine_setting_percent_ = -1;
    release_active_track_content();
    reset_catalogs();

    for (int32_t i = 0; i < track_entries.size(); ++i) {
      const Variant &entry_value = track_entries[i];
      FzgxStageCatalogEntry entry;
      Dictionary entry_dict;
      String entry_kind;

      if (entry_value.get_type() != Variant::DICTIONARY) {
        reset_catalogs();
        return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
      }
      entry_dict = (Dictionary)entry_value;
      fzgx_dictionary_read_string_exact(entry_dict, "entry_kind", &entry_kind);
      if (entry_kind == "generated_track") {
        Variant generated_value = entry_dict.get("generated_data", Variant());
        String json_text;
        Variant parsed_json;

        entry.kind = FzgxStageCatalogEntry::KIND_GENERATED_TRACK;
        if (generated_value.get_type() == Variant::DICTIONARY) {
          entry.generated_data = (Dictionary)generated_value;
        } else if (fzgx_dictionary_read_string_exact(entry_dict, "json_text", &json_text)) {
          parsed_json = JSON::parse_string(json_text);
          if (parsed_json.get_type() != Variant::DICTIONARY) {
            reset_catalogs();
            return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
          }
          entry.generated_data = (Dictionary)parsed_json;
        } else {
          reset_catalogs();
          return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
        }
        if (!fzgx_parse_generated_track_dictionary_manifest_exact(
                entry.generated_data, &entry.manifest)) {
          reset_catalogs();
          return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
        }
      } else {
        uint32_t authored_track_id = 0u;

        entry.kind = FzgxStageCatalogEntry::KIND_STAGE_BYTES;
        if (!fzgx_dictionary_read_u32_exact(entry_dict, "authored_track_id", &authored_track_id) ||
            !fzgx_dictionary_read_packed_byte_array_exact(entry_dict, "bytes", &entry.bytes) ||
            (entry.bytes.size() <= 0)) {
          reset_catalogs();
          return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
        }
        if (fzgx_content_build_track_manifest_from_bytes(
                entry.bytes.ptr(),
                (uint32_t)entry.bytes.size(),
                authored_track_id,
                &entry.manifest) != FZGX_STATUS_OK) {
          reset_catalogs();
          return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
        }
      }
      fzgx_dictionary_read_string_exact(entry_dict, "label", &entry.label);
      if (entry.label.is_empty()) {
        if ((entry.kind == FzgxStageCatalogEntry::KIND_GENERATED_TRACK) &&
            (entry.generated_data.get("label", Variant()).get_type() == Variant::STRING)) {
          entry.label = (String)entry.generated_data.get("label", Variant());
        }
        if (entry.label.is_empty()) {
          entry.label = fzgx_format_track_label(entry.manifest, (uint32_t)stage_catalog_.size());
        }
      }
      stage_catalog_.push_back(entry);
      track_manifests_.push_back(entry.manifest);
    }

    for (int32_t i = 0; i < machine_entries.size(); ++i) {
      const Variant &entry_value = machine_entries[i];
      PackedByteArray machine_bytes;
      fzgx_machine_definition machine = {};
      String label;

      if (entry_value.get_type() != Variant::DICTIONARY) {
        reset_catalogs();
        return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
      }
      if (!fzgx_dictionary_read_packed_byte_array_exact((Dictionary)entry_value, "bytes", &machine_bytes) ||
          !fzgx_parse_machine_definition_bytes_exact(machine_bytes, &machine) ||
          (machine_bytes.size() <= 0)) {
        reset_catalogs();
        return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
      }
      if (!fzgx_dictionary_read_string_exact((Dictionary)entry_value, "label", &label)) {
        label = fzgx_format_machine_label(machine, (uint32_t)machine_definitions_.size());
      }
      machine_definitions_.push_back(machine);
      machine_labels_.push_back(label);
    }

    refresh_runtime_bundle_view();
    return (int32_t)FZGX_STATUS_OK;
  }

  void clear_replay_capture() {
    replay_frames_.clear();
    has_replay_start_machine_ = false;
    replay_start_frame_index_ = 0u;
    std::memset(&replay_start_machine_, 0, sizeof(replay_start_machine_));
  }

  void begin_replay_capture() {
    clear_replay_capture();
    if (world_.machine_count == 0u) {
      return;
    }
    replay_start_machine_ = world_.machines[0];
    replay_start_frame_index_ = world_.frame_index;
    has_replay_start_machine_ = true;
  }

  String build_default_replay_path() const {
    char filename[128];
    std::filesystem::path directory;
    std::filesystem::path path;
    std::time_t current_time = std::time(nullptr);

    directory = fzgx_resolve_project_replay_dir();
    if (directory.empty()) {
      return String();
    }
    std::snprintf(
        filename,
        sizeof(filename),
        "replay_t%02d_m%02d_s%03d_f%06u_%lld.json",
        current_track_index_,
        current_machine_index_,
        current_machine_setting_percent_,
        world_.frame_index,
        (long long)current_time);
    path = directory / filename;
    return String(path.string().c_str());
  }

  String build_shared_state_slot_path() const {
    const std::filesystem::path slot_path = fzgx_resolve_project_shared_state_slot_path();

    if (slot_path.empty()) {
      return String();
    }
    return String(slot_path.string().c_str());
  }

  bool write_replay_json_file(const std::string &path) const {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    String track_label;
    String machine_label;
    std::string initial_pose_snapshot;
    std::string saved_pose_snapshot;

    if (!stream.is_open()) {
      return false;
    }
    if ((bundle_ == nullptr) || (current_track_index_ < 0) || (current_machine_index_ < 0) ||
        !has_replay_start_machine_ || (world_.machine_count == 0u) ||
        ((uint32_t)current_track_index_ >= stage_catalog_.size()) ||
        ((uint32_t)current_machine_index_ >= machine_labels_.size())) {
      return false;
    }

    track_label = stage_catalog_[(uint32_t)current_track_index_].label;
    machine_label = machine_labels_[(uint32_t)current_machine_index_];
    initial_pose_snapshot = fzgx_format_pose_snapshot_text(
        replay_start_machine_,
        (uint32_t)current_track_index_,
        track_label,
        (uint32_t)current_machine_index_,
        machine_label,
        (uint32_t)current_machine_setting_percent_,
        replay_start_frame_index_);
    saved_pose_snapshot = fzgx_format_pose_snapshot_text(
        world_.machines[0],
        (uint32_t)current_track_index_,
        track_label,
        (uint32_t)current_machine_index_,
        machine_label,
        (uint32_t)current_machine_setting_percent_,
        world_.frame_index);

    stream << "{\n";
    stream << "  \"type\": \"fzgx_input_replay\",\n";
    stream << "  \"version\": 2,\n";
    stream << "  \"sim_api_version\": " << FZGX_SIM_API_VERSION << ",\n";
    stream << "  \"track_index\": " << current_track_index_ << ",\n";
    stream << "  \"track_label\": ";
    fzgx_write_json_escaped_string(stream, fzgx_string_to_utf8_std(track_label));
    stream << ",\n";
    stream << "  \"authored_track_id\": "
           << bundle_->tracks[(uint32_t)current_track_index_].authored_track_id << ",\n";
    stream << "  \"machine_index\": " << current_machine_index_ << ",\n";
    stream << "  \"machine_label\": ";
    fzgx_write_json_escaped_string(stream, fzgx_string_to_utf8_std(machine_label));
    stream << ",\n";
    stream << "  \"machine_setting_percent\": " << current_machine_setting_percent_ << ",\n";
    stream << "  \"control_profile_kind\": 2,\n";
    stream << "  \"start_world_frame_index\": " << replay_start_frame_index_ << ",\n";
    stream << "  \"saved_world_frame_index\": " << world_.frame_index << ",\n";
    stream << "  \"captured_frame_count\": " << replay_frames_.size() << ",\n";
    stream << "  \"camera_render_state\": {\n";
    stream << "    \"aspect_ratio_hex\": ";
    fzgx_write_json_escaped_string(
        stream,
        fzgx_format_hex_u32(fzgx_float_bits_exact(camera_render_state_.aspect_ratio)));
    stream << ",\n";
    stream << "    \"display_mode_kind\": " << camera_render_state_.display_mode_kind << ",\n";
    stream << "    \"camera_parameter_hex\": ";
    fzgx_write_json_escaped_string(
        stream,
        fzgx_format_hex_u32(fzgx_float_bits_exact(camera_render_state_.camera_parameter)));
    stream << ",\n";
    stream << "    \"camera_manager_mode\": " << camera_render_state_.camera_manager_mode << "\n";
    stream << "  },\n";
    stream << "  \"frame_format\": [\n";
    stream << "    \"steer_yaw_hex\",\n";
    stream << "    \"steer_pitch_hex\",\n";
    stream << "    \"accel_hex\",\n";
    stream << "    \"brake_hex\",\n";
    stream << "    \"strafe_hex\",\n";
    stream << "    \"buttons_hex\",\n";
    stream << "    \"view_up_pressed\",\n";
    stream << "    \"view_down_pressed\"\n";
    stream << "  ],\n";
    stream << "  \"initial_pose_snapshot\": ";
    fzgx_write_json_escaped_string(stream, initial_pose_snapshot);
    stream << ",\n";
    stream << "  \"saved_pose_snapshot\": ";
    fzgx_write_json_escaped_string(stream, saved_pose_snapshot);
    stream << ",\n";
    stream << "  \"frames\": [\n";
    for (size_t i = 0u; i < replay_frames_.size(); ++i) {
      const ReplayCapturedFrame &frame = replay_frames_[i];

      stream << "    [";
      fzgx_write_json_escaped_string(
          stream,
          fzgx_format_hex_u32(fzgx_float_bits_exact(frame.control.steer_yaw)));
      stream << ", ";
      fzgx_write_json_escaped_string(
          stream,
          fzgx_format_hex_u32(fzgx_float_bits_exact(frame.control.steer_pitch)));
      stream << ", ";
      fzgx_write_json_escaped_string(
          stream,
          fzgx_format_hex_u32(fzgx_float_bits_exact(frame.control.accel)));
      stream << ", ";
      fzgx_write_json_escaped_string(
          stream,
          fzgx_format_hex_u32(fzgx_float_bits_exact(frame.control.brake)));
      stream << ", ";
      fzgx_write_json_escaped_string(
          stream,
          fzgx_format_hex_u32(fzgx_float_bits_exact(frame.control.strafe)));
      stream << ", ";
      fzgx_write_json_escaped_string(
          stream,
          fzgx_format_hex_u32(frame.control.buttons));
      stream << ", "
             << ((frame.camera_input.view_up_pressed != 0u) ? "true" : "false")
             << ", "
             << ((frame.camera_input.view_down_pressed != 0u) ? "true" : "false")
             << "]";
      if ((i + 1u) < replay_frames_.size()) {
        stream << ",";
      }
      stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.good();
  }

  static void _bind_methods() {
    ClassDB::bind_method(D_METHOD("get_track_labels"), &FzgxGameBridge::get_track_labels);
    ClassDB::bind_method(D_METHOD("get_machine_labels"), &FzgxGameBridge::get_machine_labels);
    ClassDB::bind_method(
        D_METHOD("generate_random_track_json", "seed_text"),
        &FzgxGameBridge::generate_random_track_json);
    ClassDB::bind_method(
        D_METHOD("configure_content_catalog", "tracks", "machines"),
        &FzgxGameBridge::configure_content_catalog);
    ClassDB::bind_method(
        D_METHOD("start_session", "track_index", "machine_index", "machine_setting_percent"),
        &FzgxGameBridge::start_session);
    ClassDB::bind_method(D_METHOD("restart_session"), &FzgxGameBridge::restart_session);
    ClassDB::bind_method(D_METHOD("has_session"), &FzgxGameBridge::has_session);
    ClassDB::bind_method(D_METHOD("has_replay_capture"), &FzgxGameBridge::has_replay_capture);
    ClassDB::bind_method(D_METHOD("get_replay_frame_count"), &FzgxGameBridge::get_replay_frame_count);
    ClassDB::bind_method(D_METHOD("save_replay_json"), &FzgxGameBridge::save_replay_json);
    ClassDB::bind_method(
        D_METHOD("get_shared_state_slot_path"),
        &FzgxGameBridge::get_shared_state_slot_path);
    ClassDB::bind_method(
        D_METHOD("save_shared_state_slot"),
        &FzgxGameBridge::save_shared_state_slot);
    ClassDB::bind_method(
        D_METHOD("load_shared_state_slot"),
        &FzgxGameBridge::load_shared_state_slot);
    ClassDB::bind_method(
        D_METHOD("get_current_track_index"),
        &FzgxGameBridge::get_current_track_index);
    ClassDB::bind_method(
        D_METHOD("get_current_machine_index"),
        &FzgxGameBridge::get_current_machine_index);
    ClassDB::bind_method(
        D_METHOD("get_current_machine_setting_percent"),
        &FzgxGameBridge::get_current_machine_setting_percent);
    ClassDB::bind_method(
        D_METHOD(
            "set_camera_render_state",
            "aspect_ratio",
            "display_mode_kind",
            "camera_parameter",
            "camera_manager_mode"),
        &FzgxGameBridge::set_camera_render_state,
        DEFVAL(-1),
        DEFVAL(-1.0),
        DEFVAL(0));
    ClassDB::bind_method(
        D_METHOD(
            "step_frame",
            "steer_yaw",
            "steer_pitch",
            "accel",
            "brake",
            "strafe",
            "buttons",
            "view_up_pressed",
            "view_down_pressed"),
        &FzgxGameBridge::step_frame,
        DEFVAL(0),
        DEFVAL(false),
        DEFVAL(false));
    ClassDB::bind_method(D_METHOD("read_machine_state"), &FzgxGameBridge::read_machine_state);
    ClassDB::bind_method(D_METHOD("read_game_camera_state"), &FzgxGameBridge::read_game_camera_state);
    ClassDB::bind_method(D_METHOD("read_loaded_track_collision_mesh"), &FzgxGameBridge::read_loaded_track_collision_mesh);
    ClassDB::bind_method(
        D_METHOD("read_loaded_extra_collision_debug_meshes"),
        &FzgxGameBridge::read_loaded_extra_collision_debug_meshes);
    ClassDB::bind_method(
        D_METHOD("read_loaded_extra_collision_debug_state"),
        &FzgxGameBridge::read_loaded_extra_collision_debug_state);
    ClassDB::bind_method(
        D_METHOD("read_loaded_track_analytic_debug_mesh"),
        &FzgxGameBridge::read_loaded_track_analytic_debug_mesh);
    ClassDB::bind_method(
        D_METHOD("read_loaded_track_checkpoint_debug"),
        &FzgxGameBridge::read_loaded_track_checkpoint_debug);
  }

public:
  FzgxGameBridge() {
    options_.advance_lap_timers = true;
    refresh_runtime_bundle_view();
    fzgx_replay_init_loaded_courses(&loaded_courses_);
    fzgx_replay_init_camera_render_state(&camera_render_state_);
    fzgx_game_camera_init(&game_camera_);
    apply_camera_render_state();
    fzgx_sim_world_init(&world_);
  }

  ~FzgxGameBridge() override {
    release_active_track_content();
    fzgx_replay_release_loaded_courses(&loaded_courses_);
  }

  PackedStringArray get_track_labels() const {
    PackedStringArray labels;

    for (uint32_t i = 0u; i < stage_catalog_.size(); ++i) {
      labels.push_back(stage_catalog_[i].label);
    }
    return labels;
  }

  PackedStringArray get_machine_labels() const {
    PackedStringArray labels;

    for (const String &label : machine_labels_) {
      labels.push_back(label);
    }
    return labels;
  }

  Dictionary generate_random_track_json(const String &seed_text) const {
    Dictionary result;
    fzgx_random_track_config config = {};
    fzgx_random_track_recipe recipe = {};
    fzgx_generated_track_content generated = {};
    String normalized_seed = fzgx_normalize_seed_text_exact(seed_text);
    std::string seed_utf8 = fzgx_string_to_utf8_std(normalized_seed);
    uint32_t seed_hash = fzgx_hash_seed_text_exact(seed_utf8);
    char label_buffer[64];
    int32_t status;

    config.api_version = FZGX_RANDOM_TRACK_API_VERSION;
    config.node_count = 48u;
    config.branch_window_count = 1u;

    status = (int32_t)fzgx_random_track_generate_recipe(seed_hash, &config, &recipe);
    if (status != (int32_t)FZGX_STATUS_OK) {
      result["status"] = status;
      return result;
    }
    status = (int32_t)fzgx_random_track_compile_recipe(&recipe, &generated);
    if (status != (int32_t)FZGX_STATUS_OK) {
      fzgx_random_track_release_recipe(&recipe);
      result["status"] = status;
      return result;
    }

    std::snprintf(label_buffer, sizeof(label_buffer), "Random %08x", seed_hash);
    {
      Dictionary root;
      String label = String(label_buffer);

      root["type"] = "fzgx_generated_track";
      root["version"] = (int64_t)1;
      root["seed_text"] = normalized_seed;
      root["seed_hash_hex"] = String(fzgx_format_hex_u32(seed_hash).c_str());
      root["label"] = label;
      root["authored_track_id"] = (int64_t)generated.manifest.authored_track_id;
      root["manifest"] = fzgx_build_track_manifest_dictionary_exact(generated.manifest);
      root["recipe"] = fzgx_build_random_track_recipe_dictionary_exact(recipe);

      result["status"] = (int64_t)FZGX_STATUS_OK;
      result["seed_text"] = normalized_seed;
      result["label"] = label;
      result["authored_track_id"] = (int64_t)generated.manifest.authored_track_id;
      result["json_text"] = JSON::stringify(root, "  ", true, false);
    }

    fzgx_random_track_release_generated_track_content(&generated);
    fzgx_random_track_release_recipe(&recipe);
    return result;
  }

  int32_t start_session(
      int32_t track_index,
      int32_t machine_index,
      int32_t machine_setting_percent) {
    int32_t status;
    const FzgxStageCatalogEntry *stage_entry = nullptr;

    fzgx_replay_release_loaded_courses(&loaded_courses_);
    clear_replay_capture();
    session_active_ = false;
    current_track_index_ = -1;
    current_machine_index_ = -1;
    current_machine_setting_percent_ = -1;
    release_active_track_content();

    if ((track_index < 0) || (machine_index < 0) ||
        (machine_setting_percent < 0) || (machine_setting_percent > 100) ||
        ((uint32_t)track_index >= stage_catalog_.size()) ||
        ((uint32_t)machine_index >= machine_definitions_.size())) {
      return (int32_t)FZGX_STATUS_OUT_OF_RANGE;
    }
    stage_entry = &stage_catalog_[(uint32_t)track_index];
    if (stage_entry->kind == FzgxStageCatalogEntry::KIND_STAGE_BYTES) {
      PackedByteArray stage_bytes = stage_entry->bytes;

      if (stage_bytes.size() <= 0) {
        return (int32_t)FZGX_STATUS_OUT_OF_RANGE;
      }
      status = (int32_t)fzgx_content_load_track_course_content_from_bytes(
          stage_bytes.ptr(),
          (uint32_t)stage_bytes.size(),
          stage_entry->manifest.authored_track_id,
          &active_track_course_);
      if (status != (int32_t)FZGX_STATUS_OK) {
        release_active_track_content();
        return status;
      }
      has_active_track_course_ = true;

      status = (int32_t)fzgx_content_load_track_course_animation_content_from_bytes(
          stage_bytes.ptr(),
          (uint32_t)stage_bytes.size(),
          stage_entry->manifest.authored_track_id,
          &active_track_animation_);
      if (status != (int32_t)FZGX_STATUS_OK) {
        release_active_track_content();
        return status;
      }
      has_active_track_animation_ = true;
      refresh_runtime_bundle_view();

      status = (int32_t)fzgx_replay_start_session_from_stage_bytes(
          stage_bytes.ptr(),
          (uint32_t)stage_bytes.size(),
          bundle_,
          (uint32_t)track_index,
          (uint32_t)machine_index,
          (uint32_t)machine_setting_percent,
          &world_,
          &game_camera_,
          &camera_render_state_,
          &loaded_courses_);
      if (status != (int32_t)FZGX_STATUS_OK) {
        release_active_track_content();
        return status;
      }
    } else {
      fzgx_random_track_recipe recipe = {};
      fzgx_generated_track_content generated = {};

      if (!fzgx_parse_generated_track_dictionary_recipe_exact(
              stage_entry->generated_data, &recipe)) {
        return (int32_t)FZGX_STATUS_BAD_ARGUMENT;
      }
      status = (int32_t)fzgx_random_track_compile_recipe(&recipe, &generated);
      fzgx_random_track_release_recipe(&recipe);
      if (status != (int32_t)FZGX_STATUS_OK) {
        fzgx_random_track_release_generated_track_content(&generated);
        return status;
      }
      active_track_course_ = generated.track_course;
      std::memset(&generated.track_course, 0, sizeof(generated.track_course));
      has_active_track_course_ = true;
      active_track_animation_ = generated.animation_course;
      std::memset(&generated.animation_course, 0, sizeof(generated.animation_course));
      has_active_track_animation_ =
          active_track_animation_.course.track_segment_count != 0u;
      loaded_courses_.static_course = generated.static_course;
      loaded_courses_.has_static_course = 1u;
      std::memset(&generated.static_course, 0, sizeof(generated.static_course));
      loaded_courses_.dynamic_course = generated.dynamic_course;
      loaded_courses_.has_dynamic_course = 1u;
      std::memset(&generated.dynamic_course, 0, sizeof(generated.dynamic_course));
      refresh_runtime_bundle_view();

      status = (int32_t)fzgx_replay_start_session_from_loaded_courses(
          bundle_,
          (uint32_t)track_index,
          (uint32_t)machine_index,
          (uint32_t)machine_setting_percent,
          &world_,
          &game_camera_,
          &camera_render_state_,
          &loaded_courses_);
      if (status != (int32_t)FZGX_STATUS_OK) {
        release_active_track_content();
        return status;
      }
    }

    session_active_ = true;
    current_track_index_ = track_index;
    current_machine_index_ = machine_index;
    current_machine_setting_percent_ = machine_setting_percent;
    begin_replay_capture();
    return (int32_t)FZGX_STATUS_OK;
  }

  int32_t restart_session() {
    if (!session_active_) {
      return (int32_t)FZGX_STATUS_NOT_CONFIGURED;
    }
    return start_session(
        current_track_index_, current_machine_index_, current_machine_setting_percent_);
  }

  bool has_session() const {
    return session_active_;
  }

  bool has_replay_capture() const {
    return (current_track_index_ >= 0) &&
           (current_machine_index_ >= 0) &&
           has_replay_start_machine_;
  }

  int64_t get_replay_frame_count() const {
    return (int64_t)replay_frames_.size();
  }

  String save_replay_json() const {
    String path;
    std::string native_path;
    std::error_code ec;

    if (!has_replay_capture()) {
      return String();
    }
    path = build_default_replay_path();
    if (path.is_empty()) {
      return String();
    }
    native_path = fzgx_string_to_utf8_std(path);
    std::filesystem::create_directories(std::filesystem::path(native_path).parent_path(), ec);
    if (ec) {
      return String();
    }
    if (!write_replay_json_file(native_path)) {
      return String();
    }
    return path;
  }

  String get_shared_state_slot_path() const {
    return build_shared_state_slot_path();
  }

  int32_t save_shared_state_slot() const {
    const std::filesystem::path slot_path = fzgx_resolve_project_shared_state_slot_path();
    FzgxSharedStateSlotFileHeader header = {};
    fzgx_world_snapshot snapshot = {};
    std::error_code ec;
    std::ofstream stream;
    fzgx_status status;

    if (!session_active_) {
      return (int32_t)FZGX_STATUS_NOT_CONFIGURED;
    }
    if (slot_path.empty()) {
      return FZGX_BRIDGE_STATUS_IO_ERROR;
    }

    status = fzgx_sim_world_get_snapshot(&world_, &snapshot);
    if (status != FZGX_STATUS_OK) {
      return (int32_t)status;
    }

    std::memcpy(header.magic, FZGX_SHARED_STATE_SLOT_MAGIC, sizeof(header.magic));
    header.version = FZGX_SHARED_STATE_SLOT_VERSION;
    header.sim_api_version = FZGX_SIM_API_VERSION;
    header.world_snapshot_size = (uint32_t)sizeof(snapshot);
    header.camera_runtime_size = (uint32_t)sizeof(game_camera_);
    header.camera_render_state_size = (uint32_t)sizeof(camera_render_state_);
    header.track_index = current_track_index_;
    header.machine_index = current_machine_index_;
    header.machine_setting_percent = current_machine_setting_percent_;
    header.camera_render_state = camera_render_state_;

    std::filesystem::create_directories(slot_path.parent_path(), ec);
    if (ec) {
      return FZGX_BRIDGE_STATUS_IO_ERROR;
    }

    stream.open(slot_path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
      return FZGX_BRIDGE_STATUS_IO_ERROR;
    }

    stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char *>(&snapshot), sizeof(snapshot));
    stream.write(reinterpret_cast<const char *>(&game_camera_), sizeof(game_camera_));
    if (!stream.good()) {
      return FZGX_BRIDGE_STATUS_IO_ERROR;
    }
    return (int32_t)FZGX_STATUS_OK;
  }

  int32_t load_shared_state_slot() {
    const std::filesystem::path slot_path = fzgx_resolve_project_shared_state_slot_path();
    FzgxSharedStateSlotFileHeader header = {};
    fzgx_world_snapshot snapshot = {};
    fzgx_game_camera_runtime saved_camera = {};
    std::ifstream stream(slot_path, std::ios::binary);
    bool needs_reconfigure;
    int32_t status;

    if (slot_path.empty()) {
      return FZGX_BRIDGE_STATUS_IO_ERROR;
    }
    if (!stream.is_open()) {
      return FZGX_BRIDGE_STATUS_IO_ERROR;
    }

    stream.read(reinterpret_cast<char *>(&header), sizeof(header));
    stream.read(reinterpret_cast<char *>(&snapshot), sizeof(snapshot));
    stream.read(reinterpret_cast<char *>(&saved_camera), sizeof(saved_camera));
    if (!stream.good()) {
      return FZGX_BRIDGE_STATUS_INVALID_STATE_SLOT;
    }
    if ((std::memcmp(header.magic, FZGX_SHARED_STATE_SLOT_MAGIC, sizeof(header.magic)) != 0) ||
        (header.version != FZGX_SHARED_STATE_SLOT_VERSION) ||
        (header.sim_api_version != FZGX_SIM_API_VERSION) ||
        (header.world_snapshot_size != sizeof(snapshot)) ||
        (header.camera_runtime_size != sizeof(saved_camera)) ||
        (header.camera_render_state_size != sizeof(header.camera_render_state)) ||
        (bundle_ == nullptr) ||
        (header.track_index < 0) ||
        (header.machine_index < 0) ||
        (header.machine_setting_percent < 0) ||
        (header.machine_setting_percent > 100) ||
        ((uint32_t)header.track_index >= bundle_->track_count) ||
        ((uint32_t)header.machine_index >= bundle_->machine_count) ||
        (snapshot.api_version != FZGX_SIM_API_VERSION) ||
        (snapshot.machine_count == 0u) ||
        (saved_camera.machine_index >= snapshot.machine_count) ||
        (snapshot.active_track_index != (uint32_t)header.track_index)) {
      return FZGX_BRIDGE_STATUS_INVALID_STATE_SLOT;
    }

    needs_reconfigure =
        !session_active_ ||
        (current_track_index_ != header.track_index) ||
        (current_machine_index_ != header.machine_index) ||
        (current_machine_setting_percent_ != header.machine_setting_percent);
    if (needs_reconfigure) {
      status = start_session(
          header.track_index,
          header.machine_index,
          header.machine_setting_percent);
      if (status != (int32_t)FZGX_STATUS_OK) {
        return status;
      }
    }

    status = (int32_t)fzgx_sim_world_set_snapshot(&world_, &snapshot);
    if (status != (int32_t)FZGX_STATUS_OK) {
      return status;
    }

    camera_render_state_ = header.camera_render_state;
    game_camera_ = saved_camera;
    session_active_ = true;
    current_track_index_ = header.track_index;
    current_machine_index_ = header.machine_index;
    current_machine_setting_percent_ = header.machine_setting_percent;
    begin_replay_capture();
    return (int32_t)FZGX_STATUS_OK;
  }

  int32_t get_current_track_index() const {
    return current_track_index_;
  }

  int32_t get_current_machine_index() const {
    return current_machine_index_;
  }

  int32_t get_current_machine_setting_percent() const {
    return current_machine_setting_percent_;
  }

  void set_camera_render_state(
      double aspect_ratio,
      int32_t display_mode_kind = -1,
      double camera_parameter = -1.0,
      int32_t camera_manager_mode = 0) {
    camera_render_state_.aspect_ratio = (float)aspect_ratio;
    camera_render_state_.display_mode_kind = display_mode_kind;
    camera_render_state_.camera_parameter = (float)camera_parameter;
    camera_render_state_.camera_manager_mode = camera_manager_mode;
    apply_camera_render_state();
  }

  int32_t step_frame(
      double steer_yaw,
      double steer_pitch,
      double accel,
      double brake,
      double strafe,
      int32_t buttons = 0,
      bool view_up_pressed = false,
      bool view_down_pressed = false) {
    fzgx_control_sample control = {};
    fzgx_game_camera_input camera_input = {};
    fzgx_status status;

    if (!session_active_) {
      return (int32_t)FZGX_STATUS_NOT_CONFIGURED;
    }
    control.steer_yaw = (float)fzgx_clamp_unit(steer_yaw);
    control.steer_pitch = (float)fzgx_clamp_unit(steer_pitch);
    control.accel = (float)std::max(0.0, std::min(1.0, accel));
    control.brake = (float)std::max(0.0, std::min(1.0, brake));
    control.strafe = (float)fzgx_clamp_unit(strafe);
    control.buttons = (uint32_t)buttons;
    control.control_profile_kind = 2u;
    camera_input.view_up_pressed = view_up_pressed ? 1u : 0u;
    camera_input.view_down_pressed = view_down_pressed ? 1u : 0u;
    replay_frames_.push_back({control, camera_input});
    status = fzgx_replay_step_frame(
        &world_,
        &game_camera_,
        &control,
        &camera_input,
        &options_);
    return (int32_t)status;
  }

  Dictionary read_machine_state() const {
    Dictionary state;
    const fzgx_machine_snapshot &machine = world_.machines[0];
    const fzgx_machine_track_state &track = machine.track_state;

    if (!session_active_) {
      state["active"] = false;
      return state;
    }
    state["active"] = true;
    state["frame_index"] = (int64_t)world_.frame_index;
    state["machine_setting_percent"] = (int64_t)current_machine_setting_percent_;
    state["transform"] = fzgx_machine_transform_to_godot(machine);
    state["visual_transform"] = fzgx_mat43_to_godot_transform(machine.transform_visual);
    state["speed_kmh"] = machine.speed_kmh;
    state["energy"] = machine.energy;
    state["velocity"] = fzgx_to_godot_vec3(machine.velocity);
    state["angular_velocity"] = fzgx_to_godot_vec3(machine.angular_velocity);
    state["surface_normal"] = fzgx_to_godot_vec3(machine.surface_normal);
    state["position_bottom"] = fzgx_to_godot_vec3(machine.position_bottom);
    state["base_speed"] = machine.base_speed;
    state["boost_turbo"] = machine.boost_turbo;
    state["boost_frames"] = (int64_t)machine.boost_frames;
    state["boost_frames_manual"] = (int64_t)machine.boost_frames_manual;
    state["boost_delay_frame_counter"] = (int64_t)machine.boost_delay_frame_counter;
    state["air_time"] = (int64_t)machine.air_time;
    state["zero_minus_height_above_track"] = machine.zero_minus_height_above_track;
    state["machine_state_flags"] = (int64_t)machine.machine_state;
    state["state_2_flags"] = (int64_t)machine.state_2;
    state["terrain_flags"] = (int64_t)machine.terrain_flags;
    state["floor_surface_flags"] = (int64_t)machine.floor_surface_flags;
    state["branch_indicator"] = (int64_t)machine.branch_indicator;
    state["branch_flags"] = (int64_t)machine.branch_flags;
    state["branch_slot"] = (int64_t)machine.branch_slot;
    state["control_profile_kind"] = (int64_t)machine.control_profile_kind;
    state["frames_since_start_2"] = (int64_t)machine.frames_since_start_2;
    state["current_checkpoint"] = (int64_t)machine.current_checkpoint;
    state["checkpoint_fraction"] = machine.checkpoint_fraction;
    state["track_cur_cp_pointer"] = (int64_t)track.cur_cp_pointer;
    state["track_cur_cp_idx"] = (int64_t)track.cur_cp_idx;
    state["track_cur_cp_frac"] = track.cur_cp_frac;
    state["track_next_cp_idx"] = (int64_t)track.next_cp_idx;
    state["track_next_cp_frac"] = track.next_cp_frac;
    state["track_selected_cached_frame_index"] = (int64_t)track.selected_cached_frame_index;
    state["lap_time_frames"] = track.lap_time_frames;
    state["rank"] = (int64_t)track.rank_this_frame;
    return state;
  }

  Dictionary read_game_camera_state() const {
    Dictionary state;
    fzgx_game_camera_view view = {};

    if (!session_active_) {
      state["active"] = false;
      return state;
    }
    if (fzgx_game_camera_get_view(&game_camera_, &view) != FZGX_STATUS_OK) {
      state["active"] = false;
      return state;
    }
    state["active"] = view.active != 0u;
    state["position"] = fzgx_to_godot_vec3(view.position);
    state["previous_position"] = fzgx_to_godot_vec3(view.previous_position);
    state["interest"] = fzgx_to_godot_vec3(view.interest);
    state["up"] = fzgx_to_godot_vec3(view.up);
    state["transform"] =
        fzgx_mat43_to_godot_transform(fzgx_mat43_rigid_inverted(view.view_matrix));
    state["zoom_mode"] = (int64_t)view.zoom_mode;
    state["saved_zoom_mode"] = (int64_t)view.saved_zoom_mode;
    state["behavior_state"] = (int64_t)view.behavior_state;
    state["perspective"] = view.perspective;
    state["aspect_ratio"] = view.aspect_ratio;
    return state;
  }

  Dictionary read_loaded_track_collision_mesh() const {
    Dictionary mesh_info;
    PackedVector3Array vertices;
    PackedVector3Array normals;
    PackedVector2Array uvs;

    if (loaded_courses_.has_static_course == 0u) {
      mesh_info["valid"] = false;
      return mesh_info;
    }

    for (uint32_t i = 0u; i < loaded_courses_.static_course.tri_count; ++i) {
      const fzgx_static_collider_triangle_record &tri = loaded_courses_.static_course.tris[i];
      Vector3 normal = fzgx_to_godot_vec3(tri.normal);

      vertices.push_back(fzgx_to_godot_vec3(tri.vertex0));
      vertices.push_back(fzgx_to_godot_vec3(tri.vertex1));
      vertices.push_back(fzgx_to_godot_vec3(tri.vertex2));
      normals.push_back(normal);
      normals.push_back(normal);
      normals.push_back(normal);
    }
    for (uint32_t i = 0u; i < loaded_courses_.static_course.quad_count; ++i) {
      const fzgx_static_collider_quad_record &quad = loaded_courses_.static_course.quads[i];
      Vector3 normal = fzgx_to_godot_vec3(quad.normal);
      Vector3 v0 = fzgx_to_godot_vec3(quad.vertex0);
      Vector3 v1 = fzgx_to_godot_vec3(quad.vertex1);
      Vector3 v2 = fzgx_to_godot_vec3(quad.vertex2);
      Vector3 v3 = fzgx_to_godot_vec3(quad.vertex3);

      vertices.push_back(v0);
      vertices.push_back(v1);
      vertices.push_back(v2);
      vertices.push_back(v0);
      vertices.push_back(v2);
      vertices.push_back(v3);
      for (int tri_vertex = 0; tri_vertex < 6; ++tri_vertex) {
        normals.push_back(normal);
      }
    }

    mesh_info["valid"] = true;
    mesh_info["vertices"] = vertices;
    mesh_info["normals"] = normals;
    return mesh_info;
  }

  Array read_loaded_extra_collision_debug_meshes() const {
    Array entries;
    const uint32_t machine_index =
        ((world_.stage_scene_context_active_machine_index >= 0) &&
         ((uint32_t)world_.stage_scene_context_active_machine_index < world_.machine_count))
            ? (uint32_t)world_.stage_scene_context_active_machine_index
            : 0u;

    if (loaded_courses_.has_track_mesh_course != 0u) {
      for (uint32_t chunk_index = 0u; chunk_index < loaded_courses_.track_mesh_course.chunk_count;
           ++chunk_index) {
        const fzgx_owned_track_mesh_chunk &chunk = loaded_courses_.track_mesh_course.chunks[chunk_index];
        PackedVector3Array vertices;
        PackedVector3Array normals;
        fzgx_mat43 chunk_transform = fzgx_mat43_identity_exact();
        char name_buffer[96];
        Dictionary entry;

        if ((((chunk.tri_count == 0u) || (chunk.tris == nullptr)) &&
             ((chunk.quad_count == 0u) || (chunk.quads == nullptr)))) {
          continue;
        }
        fzgx_append_collision_mesh_geometry_exact(
            vertices, normals, chunk.tris, chunk.tri_count, chunk.quads, chunk.quad_count);
        if (vertices.size() == 0) {
          continue;
        }
        fzgx_sample_track_mesh_chunk_transform_exact(&world_, &chunk, chunk_index, &chunk_transform);
        std::snprintf(name_buffer, sizeof(name_buffer), "TrackMeshChunk_%u", chunk_index);
        entry["name"] = name_buffer;
        entry["kind"] = "track_mesh_chunk";
        entry["collider_type"] = (int64_t)-1;
        entry["visible"] = true;
        entry["transform"] = fzgx_mat43_to_godot_transform(chunk_transform);
        entry["vertices"] = vertices;
        entry["normals"] = normals;
        entries.push_back(entry);
      }
    }

    if (loaded_courses_.has_dynamic_course != 0u) {
      for (uint32_t object_index = 0u; object_index < loaded_courses_.dynamic_course.object_count;
           ++object_index) {
        const fzgx_owned_dynamic_scene_object_record &object =
            loaded_courses_.dynamic_course.objects[object_index];
        PackedVector3Array vertices;
        PackedVector3Array normals;
        fzgx_mat43 object_transform = fzgx_mat43_identity_exact();
        fzgx_mat43 collider_transform = fzgx_mat43_identity_exact();
        const bool visible = fzgx_sample_dynamic_scene_object_world_transform_exact(
            &world_, machine_index, object_index, &object, &object_transform);
        char name_buffer[160];
        Dictionary entry;

        if (!fzgx_scene_object_collider_mesh_has_geometry_exact(&object.collider_mesh)) {
          continue;
        }
        fzgx_append_collision_mesh_geometry_exact(
            vertices,
            normals,
            object.collider_mesh.tris,
            object.collider_mesh.tri_count,
            object.collider_mesh.quads,
            object.collider_mesh.quad_count);
        if (vertices.size() == 0) {
          continue;
        }
        if (visible) {
          collider_transform =
              fzgx_resolve_dynamic_scene_object_collider_transform_exact(&object, &object_transform);
        }
        if (object.primary_lod_name[0] != '\0') {
          std::snprintf(
              name_buffer,
              sizeof(name_buffer),
              "DynamicObject_%u_%s",
              object_index,
              object.primary_lod_name);
        } else {
          std::snprintf(name_buffer, sizeof(name_buffer), "DynamicObject_%u", object_index);
        }
        entry["name"] = name_buffer;
        entry["kind"] = "dynamic_scene_object";
        entry["collider_type"] = (int64_t)object.collider_mesh.collider_type;
        entry["visible"] = visible;
        entry["transform"] =
            visible ? Variant(fzgx_mat43_to_godot_transform(collider_transform))
                    : Variant(Transform3D(Basis(), Vector3()));
        entry["vertices"] = vertices;
        entry["normals"] = normals;
        entries.push_back(entry);
      }

      for (uint32_t object_index = 0u;
           object_index < loaded_courses_.dynamic_course.unknown_collider_count;
           ++object_index) {
        const fzgx_owned_unknown_collider_record &object =
            loaded_courses_.dynamic_course.unknown_colliders[object_index];
        PackedVector3Array vertices;
        PackedVector3Array normals;
        const fzgx_mat43 object_transform = fzgx_mat43_from_transform_trxs_exact(&object.transform);
        char name_buffer[160];
        Dictionary entry;

        if (!fzgx_scene_object_collider_mesh_has_geometry_exact(&object.collider_mesh)) {
          continue;
        }
        fzgx_append_collision_mesh_geometry_exact(
            vertices,
            normals,
            object.collider_mesh.tris,
            object.collider_mesh.tri_count,
            object.collider_mesh.quads,
            object.collider_mesh.quad_count);
        if (vertices.size() == 0) {
          continue;
        }
        if (object.primary_lod_name[0] != '\0') {
          std::snprintf(
              name_buffer,
              sizeof(name_buffer),
              "UnknownCollider_%u_%s",
              object_index,
              object.primary_lod_name);
        } else {
          std::snprintf(name_buffer, sizeof(name_buffer), "UnknownCollider_%u", object_index);
        }
        entry["name"] = name_buffer;
        entry["kind"] = "unknown_collider";
        entry["collider_type"] = (int64_t)object.collider_mesh.collider_type;
        entry["visible"] = true;
        entry["transform"] = fzgx_mat43_to_godot_transform(object_transform);
        entry["vertices"] = vertices;
        entry["normals"] = normals;
        entries.push_back(entry);
      }

      for (uint32_t object_index = 0u;
           object_index < loaded_courses_.dynamic_course.static_scene_object_count;
           ++object_index) {
        const fzgx_owned_static_scene_object_record &object =
            loaded_courses_.dynamic_course.static_scene_objects[object_index];
        PackedVector3Array vertices;
        PackedVector3Array normals;
        char name_buffer[160];
        Dictionary entry;

        if (!fzgx_scene_object_collider_mesh_has_geometry_exact(&object.collider_mesh)) {
          continue;
        }
        fzgx_append_collision_mesh_geometry_exact(
            vertices,
            normals,
            object.collider_mesh.tris,
            object.collider_mesh.tri_count,
            object.collider_mesh.quads,
            object.collider_mesh.quad_count);
        if (vertices.size() == 0) {
          continue;
        }
        if (object.primary_lod_name[0] != '\0') {
          std::snprintf(
              name_buffer,
              sizeof(name_buffer),
              "StaticSceneObject_%u_%s",
              object_index,
              object.primary_lod_name);
        } else {
          std::snprintf(name_buffer, sizeof(name_buffer), "StaticSceneObject_%u", object_index);
        }
        entry["name"] = name_buffer;
        entry["kind"] = "static_scene_object";
        entry["collider_type"] = (int64_t)object.collider_mesh.collider_type;
        entry["visible"] = true;
        entry["transform"] = Transform3D(Basis(), Vector3());
        entry["vertices"] = vertices;
        entry["normals"] = normals;
        entries.push_back(entry);
      }
    }

    return entries;
  }

  Array read_loaded_extra_collision_debug_state() const {
    Array states;
    const uint32_t machine_index =
        ((world_.stage_scene_context_active_machine_index >= 0) &&
         ((uint32_t)world_.stage_scene_context_active_machine_index < world_.machine_count))
            ? (uint32_t)world_.stage_scene_context_active_machine_index
            : 0u;

    if (loaded_courses_.has_track_mesh_course != 0u) {
      for (uint32_t chunk_index = 0u; chunk_index < loaded_courses_.track_mesh_course.chunk_count;
           ++chunk_index) {
        const fzgx_owned_track_mesh_chunk &chunk = loaded_courses_.track_mesh_course.chunks[chunk_index];
        fzgx_mat43 chunk_transform = fzgx_mat43_identity_exact();
        Dictionary state;

        if ((((chunk.tri_count == 0u) || (chunk.tris == nullptr)) &&
             ((chunk.quad_count == 0u) || (chunk.quads == nullptr)))) {
          continue;
        }
        fzgx_sample_track_mesh_chunk_transform_exact(&world_, &chunk, chunk_index, &chunk_transform);
        state["visible"] = true;
        state["transform"] = fzgx_mat43_to_godot_transform(chunk_transform);
        states.push_back(state);
      }
    }

    if (loaded_courses_.has_dynamic_course != 0u) {
      for (uint32_t object_index = 0u; object_index < loaded_courses_.dynamic_course.object_count;
           ++object_index) {
        const fzgx_owned_dynamic_scene_object_record &object =
            loaded_courses_.dynamic_course.objects[object_index];
        fzgx_mat43 object_transform = fzgx_mat43_identity_exact();
        fzgx_mat43 collider_transform = fzgx_mat43_identity_exact();
        const bool visible = fzgx_sample_dynamic_scene_object_world_transform_exact(
            &world_, machine_index, object_index, &object, &object_transform);
        Dictionary state;

        if (!fzgx_scene_object_collider_mesh_has_geometry_exact(&object.collider_mesh)) {
          continue;
        }
        if (visible) {
          collider_transform =
              fzgx_resolve_dynamic_scene_object_collider_transform_exact(&object, &object_transform);
        }
        state["visible"] = visible;
        state["transform"] =
            visible ? Variant(fzgx_mat43_to_godot_transform(collider_transform))
                    : Variant(Transform3D(Basis(), Vector3()));
        states.push_back(state);
      }

      for (uint32_t object_index = 0u;
           object_index < loaded_courses_.dynamic_course.unknown_collider_count;
           ++object_index) {
        const fzgx_owned_unknown_collider_record &object =
            loaded_courses_.dynamic_course.unknown_colliders[object_index];
        Dictionary state;

        if (!fzgx_scene_object_collider_mesh_has_geometry_exact(&object.collider_mesh)) {
          continue;
        }
        state["visible"] = true;
        state["transform"] =
            fzgx_mat43_to_godot_transform(fzgx_mat43_from_transform_trxs_exact(&object.transform));
        states.push_back(state);
      }

      for (uint32_t object_index = 0u;
           object_index < loaded_courses_.dynamic_course.static_scene_object_count;
           ++object_index) {
        const fzgx_owned_static_scene_object_record &object =
            loaded_courses_.dynamic_course.static_scene_objects[object_index];
        Dictionary state;

        if (!fzgx_scene_object_collider_mesh_has_geometry_exact(&object.collider_mesh)) {
          continue;
        }
        state["visible"] = true;
        state["transform"] = Transform3D(Basis(), Vector3());
        states.push_back(state);
      }
    }

    return states;
  }

  Dictionary read_loaded_track_analytic_debug_mesh() const {
    static constexpr uint32_t kMinLongitudinalSegments = 64u;
    static constexpr double kLongitudinalStepTarget = 5.0;
    static constexpr double kLongitudinalUvWorldUnitsPerRepeat = 5.0;

    Dictionary mesh_info;
    PackedVector3Array vertices;
    PackedVector3Array normals;
    PackedVector2Array uvs;
    const fzgx_track_course_content *course = nullptr;
    const fzgx_track_course_animation_content *animation_course = nullptr;
    const fzgx_track_manifest *track_manifest = nullptr;
    std::vector<AnalyticTrackDebugSample> samples;
    bool allow_generated_fallback_matching = false;
    int32_t checkpoint_seed_index;
    uint32_t sample_count;
    double segment_length;
    fzgx_status status;

    if ((bundle_ == nullptr) || (current_track_index_ < 0) ||
        ((uint32_t)current_track_index_ >= bundle_->track_count)) {
      mesh_info["valid"] = false;
      return mesh_info;
    }

    status = fzgx_content_bundle_get_track_course_for_track_index(
        bundle_, (uint32_t)current_track_index_, &course);
    if (status != FZGX_STATUS_OK) {
      mesh_info["valid"] = false;
      return mesh_info;
    }
    status = fzgx_content_bundle_get_track_course_animation_for_track_index(
        bundle_, (uint32_t)current_track_index_, &animation_course);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_course = nullptr;
    } else if (status != FZGX_STATUS_OK) {
      mesh_info["valid"] = false;
      return mesh_info;
    }
    if ((course == nullptr) ||
        (course->track_total_distance <= 0.0f) || (course->track_node_count == 0u)) {
      mesh_info["valid"] = false;
      return mesh_info;
    }
    track_manifest = &bundle_->tracks[(uint32_t)current_track_index_];
    allow_generated_fallback_matching =
        (track_manifest->authored_track_id & 0x80000000u) != 0u;

    sample_count = (uint32_t)std::ceil((double)course->track_total_distance / kLongitudinalStepTarget);
    sample_count = std::max(sample_count, kMinLongitudinalSegments);
    if (track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_OPEN) {
      sample_count += 1u;
    }
    segment_length = (double)course->track_total_distance /
                     (double)std::max(1u, sample_count - 1u);

    samples.resize(sample_count);
    checkpoint_seed_index = 0;
    for (uint32_t sample_index = 0u; sample_index < sample_count; ++sample_index) {
      AnalyticTrackDebugSample &sample = samples[sample_index];
      double sample_distance = segment_length * (double)sample_index;
      double sample_uv_distance = sample_distance;
      int32_t checkpoint_index = 0;
      float checkpoint_fraction = 0.0f;

      if ((track_manifest->circuit_type != FZGX_CIRCUIT_TYPE_OPEN) && (sample_index == sample_count - 1u)) {
        sample_distance = 0.0;
        sample_uv_distance = (double)course->track_total_distance;
      }
      status = fzgx_track_course_find_checkpoint_for_track_distance(
          course,
          sample_distance,
          checkpoint_seed_index,
          &checkpoint_index,
          &checkpoint_fraction);
      if (status != FZGX_STATUS_OK) {
        mesh_info["valid"] = false;
        return mesh_info;
      }
      checkpoint_seed_index = checkpoint_index;
      status = fzgx_build_analytic_track_debug_sample_exact(
          course,
          animation_course,
          checkpoint_index,
          checkpoint_fraction,
          (float)(sample_uv_distance / kLongitudinalUvWorldUnitsPerRepeat),
          &sample);
      if (status != FZGX_STATUS_OK) {
        mesh_info["valid"] = false;
        return mesh_info;
      }
    }

    for (uint32_t sample_index = 0u; (sample_index + 1u) < sample_count; ++sample_index) {
      const AnalyticTrackDebugSample &sample_a = samples[sample_index];
      const AnalyticTrackDebugSample &sample_b = samples[sample_index + 1u];
      if (sample_a.checkpoint_index != sample_b.checkpoint_index) {
        AnalyticTrackDebugSample boundary_end;
        AnalyticTrackDebugSample boundary_start;
        double boundary_end_distance = 0.0;
        double boundary_start_distance = 0.0;

        status = fzgx_track_course_eval_shared_checkpoint_distance_for_debug_exact(
            course, (uint32_t)sample_a.checkpoint_index, 1.0f, &boundary_end_distance);
        if (status != FZGX_STATUS_OK) {
          mesh_info["valid"] = false;
          return mesh_info;
        }
        status = fzgx_track_course_eval_shared_checkpoint_distance_for_debug_exact(
            course, (uint32_t)sample_b.checkpoint_index, 0.0f, &boundary_start_distance);
        if (status != FZGX_STATUS_OK) {
          mesh_info["valid"] = false;
          return mesh_info;
        }
        if ((track_manifest->circuit_type == FZGX_CIRCUIT_TYPE_CLOSED) &&
            (boundary_start_distance < boundary_end_distance)) {
          boundary_start_distance += (double)course->track_total_distance;
        }

        status = fzgx_build_analytic_track_debug_sample_exact(
            course,
            animation_course,
            sample_a.checkpoint_index,
            1.0f,
            (float)(boundary_end_distance / kLongitudinalUvWorldUnitsPerRepeat),
            &boundary_end);
        if (status != FZGX_STATUS_OK) {
          mesh_info["valid"] = false;
          return mesh_info;
        }
        status = fzgx_build_analytic_track_debug_sample_exact(
            course,
            animation_course,
            sample_b.checkpoint_index,
            0.0f,
            (float)(boundary_start_distance / kLongitudinalUvWorldUnitsPerRepeat),
            &boundary_start);
        if (status != FZGX_STATUS_OK) {
          mesh_info["valid"] = false;
          return mesh_info;
        }

        fzgx_append_analytic_track_debug_span(
            course,
            animation_course,
            sample_a,
            boundary_end,
            allow_generated_fallback_matching,
            vertices,
            normals,
            uvs);
        fzgx_append_analytic_track_debug_span(
            course,
            animation_course,
            boundary_end,
            boundary_start,
            allow_generated_fallback_matching,
            vertices,
            normals,
            uvs);
        fzgx_append_analytic_track_debug_span(
            course,
            animation_course,
            boundary_start,
            sample_b,
            allow_generated_fallback_matching,
            vertices,
            normals,
            uvs);
      } else {
        fzgx_append_analytic_track_debug_span(
            course,
            animation_course,
            sample_a,
            sample_b,
            allow_generated_fallback_matching,
            vertices,
            normals,
            uvs);
      }
    }

    mesh_info["valid"] = vertices.size() > 0;
    mesh_info["vertices"] = vertices;
    mesh_info["normals"] = normals;
    mesh_info["uvs"] = uvs;
    return mesh_info;
  }

  Dictionary read_loaded_track_checkpoint_debug() const {
    static constexpr uint32_t kCachedFrameCapacity = FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY;

    Dictionary checkpoint_info;
    Array entries;
    const fzgx_track_course_content *course = nullptr;
    const fzgx_track_course_animation_content *animation_course = nullptr;
    fzgx_status status;

    if ((bundle_ == nullptr) || (current_track_index_ < 0) ||
        ((uint32_t)current_track_index_ >= bundle_->track_count)) {
      checkpoint_info["valid"] = false;
      return checkpoint_info;
    }

    status = fzgx_content_bundle_get_track_course_for_track_index(
        bundle_, (uint32_t)current_track_index_, &course);
    if ((status != FZGX_STATUS_OK) || (course == nullptr)) {
      checkpoint_info["valid"] = false;
      return checkpoint_info;
    }
    status = fzgx_content_bundle_get_track_course_animation_for_track_index(
        bundle_, (uint32_t)current_track_index_, &animation_course);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_course = nullptr;
    } else if (status != FZGX_STATUS_OK) {
      checkpoint_info["valid"] = false;
      return checkpoint_info;
    }

    for (uint32_t checkpoint_index = 0u; checkpoint_index < course->track_node_count; ++checkpoint_index) {
      const fzgx_track_node_record *track_node = nullptr;
      fzgx_track_frame_record cached_frames[kCachedFrameCapacity] = {};
      uint32_t cached_frame_count = 0u;

      status = fzgx_track_course_get_track_node(course, checkpoint_index, &track_node);
      if ((status != FZGX_STATUS_OK) || (track_node == nullptr) || (track_node->checkpoint_count == 0u)) {
        continue;
      }
      status = fzgx_track_course_build_cached_frames_for_checkpoint(
          course,
          animation_course,
          checkpoint_index,
          0.5f,
          cached_frames,
          kCachedFrameCapacity,
          &cached_frame_count);
      if (status != FZGX_STATUS_OK) {
        cached_frame_count = 0u;
      }

      for (uint32_t variant_index = 0u; variant_index < track_node->checkpoint_count; ++variant_index) {
        const fzgx_checkpoint_record *checkpoint = nullptr;
        const fzgx_track_frame_record *frame = nullptr;
        uint32_t frame_index = variant_index;

        status = fzgx_track_course_get_checkpoint_variant(
            course, checkpoint_index, variant_index, &checkpoint);
        if ((status != FZGX_STATUS_OK) || (checkpoint == nullptr)) {
          continue;
        }
        if (cached_frame_count != 0u) {
          if (frame_index >= cached_frame_count) {
            frame_index = cached_frame_count - 1u;
          }
          frame = &cached_frames[frame_index];
        }
        entries.push_back(
            fzgx_build_checkpoint_debug_entry_exact(
                *checkpoint,
                (int32_t)checkpoint_index,
                variant_index,
                track_node->checkpoint_count,
                frame));
      }
    }

    checkpoint_info["valid"] = entries.size() > 0;
    checkpoint_info["entries"] = entries;
    return checkpoint_info;
  }
};

static void initialize_fzgx_godot_bridge(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
  ClassDB::register_class<FzgxGameBridge>();
}

static void uninitialize_fzgx_godot_bridge(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}

extern "C" GDExtensionBool GDE_EXPORT
fzgx_godot_bridge_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
  godot::GDExtensionBinding::InitObject init_obj(
      p_get_proc_address, p_library, r_initialization);

  init_obj.register_initializer(initialize_fzgx_godot_bridge);
  init_obj.register_terminator(uninitialize_fzgx_godot_bridge);
  init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
  return init_obj.init();
}
