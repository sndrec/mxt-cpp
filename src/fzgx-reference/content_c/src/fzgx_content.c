#include "fzgx/content.h"

#include "../../catalog/sine_lut_14b.inc"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fzgx_sincos_result {
  float sin_value;
  float cos_value;
} fzgx_sincos_result;

typedef struct fzgx_owned_byte_buffer {
  uint8_t *data;
  uint32_t size;
} fzgx_owned_byte_buffer;

static fzgx_status fzgx_track_segment_sample_curve_or_fallback(
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    uint32_t curve_index,
    float fallback_value,
    float time,
    float *value_out);

fzgx_status fzgx_track_course_get_track_segment_children(
    const fzgx_track_course_content *course,
    const fzgx_track_segment_record *parent_segment,
    const fzgx_track_segment_record **children_out,
    uint32_t *children_count_out);

static fzgx_status fzgx_parse_static_collider_triangle_array_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t tri_ptr,
    uint32_t tri_count,
    fzgx_static_collider_triangle_record **tris_out);
static fzgx_status fzgx_parse_static_collider_quad_array_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t quad_ptr,
    uint32_t quad_count,
    fzgx_static_collider_quad_record **quads_out);
static fzgx_status fzgx_parse_track_mesh_index_grid_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t grid_ptr,
    uint32_t cell_count,
    fzgx_static_collider_index_span *spans_out,
    uint16_t **indices_inout,
    uint32_t *index_count_inout,
    uint32_t *index_capacity_inout,
    uint32_t *largest_index_inout);
static fzgx_status fzgx_parse_animation_curve_keyables_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t keyable_ptr,
    uint32_t keyable_count,
    fzgx_keyable_attribute **keyables_out);
static fzgx_status fzgx_parse_track_mesh_animation_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t animation_ptr,
    fzgx_owned_track_mesh_animation_record *animation_out,
    uint8_t *has_animation_out);
static fzgx_status fzgx_parse_dynamic_scene_animation_clip_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t clip_ptr,
    fzgx_owned_animation_clip_record *clip_out,
    uint8_t *has_clip_out);

enum {
  FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_X = 0,
  FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y = 1,
  FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Z = 2,
  FZGX_TRACK_SEGMENT_TRS_CURVE_ROTATION_X = 3,
  FZGX_TRACK_SEGMENT_TRS_CURVE_ROTATION_Y = 4,
  FZGX_TRACK_SEGMENT_TRS_CURVE_ROTATION_Z = 5,
  FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X = 6,
  FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y = 7,
  FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Z = 8
};

enum {
  FZGX_COLI_STATIC_COLLIDER_SURFACE_COUNT_AX = 11u,
  FZGX_COLI_STATIC_COLLIDER_SURFACE_COUNT_GX = 14u,
  FZGX_COLI_INDEX_GRID_CELL_COUNT = 256u,
  FZGX_COLI_INDEX_GRID_POINTER_TABLE_SIZE =
      FZGX_COLI_INDEX_GRID_CELL_COUNT * (uint32_t)sizeof(uint32_t),
  FZGX_COLI_TRACK_MESH_RECORD_SIZE = 0x12cu,
  FZGX_COLI_TRACK_MESH_LOGICAL_CHUNK_STRIDE = 0x4b0u,
  FZGX_COLI_TRIANGLE_RECORD_SIZE = 0x58u,
  FZGX_COLI_QUAD_RECORD_SIZE = 0x70u
};

static uint16_t fzgx_read_u16be_exact(const uint8_t *data, uint32_t offset) {
  return (uint16_t)(((uint16_t)data[offset] << 8) | (uint16_t)data[offset + 1u]);
}

static uint32_t fzgx_read_u32be_exact(const uint8_t *data, uint32_t offset) {
  return ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1u] << 16) |
         ((uint32_t)data[offset + 2u] << 8) | (uint32_t)data[offset + 3u];
}

static uint32_t fzgx_read_u32le_exact(const uint8_t *data, uint32_t offset) {
  return ((uint32_t)data[offset]) | ((uint32_t)data[offset + 1u] << 8) |
         ((uint32_t)data[offset + 2u] << 16) | ((uint32_t)data[offset + 3u] << 24);
}

static float fzgx_read_f32be_exact(const uint8_t *data, uint32_t offset) {
  uint32_t bits = fzgx_read_u32be_exact(data, offset);
  float value;

  memcpy(&value, &bits, sizeof(value));
  return value;
}

static int fzgx_range_is_valid_exact(uint32_t size, uint32_t offset, uint32_t length) {
  return offset <= size && length <= (size - offset);
}

static void fzgx_read_vec3be_exact(const uint8_t *data, uint32_t offset, fzgx_vec3 *value_out) {
  value_out->x = fzgx_read_f32be_exact(data, offset);
  value_out->y = fzgx_read_f32be_exact(data, offset + 4u);
  value_out->z = fzgx_read_f32be_exact(data, offset + 8u);
}

static void fzgx_owned_byte_buffer_release_exact(fzgx_owned_byte_buffer *buffer) {
  if (buffer == 0) {
    return;
  }
  free(buffer->data);
  buffer->data = 0;
  buffer->size = 0u;
}

static fzgx_status fzgx_read_file_exact(const char *path, fzgx_owned_byte_buffer *buffer_out) {
  FILE *file;
  long file_size_long;
  uint8_t *data;
  size_t read_size;

  if ((path == 0) || (buffer_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(buffer_out, 0, sizeof(*buffer_out));

  file = fopen(path, "rb");
  if (file == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  file_size_long = ftell(file);
  if ((file_size_long < 0L) || (file_size_long > 0x7fffffffL)) {
    fclose(file);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (fseek(file, 0L, SEEK_SET) != 0) {
    fclose(file);
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  data = (uint8_t *)malloc((size_t)file_size_long);
  if ((data == 0) && (file_size_long != 0L)) {
    fclose(file);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  read_size = fread(data, 1u, (size_t)file_size_long, file);
  fclose(file);
  if (read_size != (size_t)file_size_long) {
    free(data);
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  buffer_out->data = data;
  buffer_out->size = (uint32_t)file_size_long;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_decode_av_lz_exact(
    const uint8_t *compressed,
    uint32_t compressed_size,
    fzgx_owned_byte_buffer *buffer_out) {
  enum {
    FZGX_LZ_N = 4096,
    FZGX_LZ_F = 18,
    FZGX_LZ_THRESHOLD = 3
  };
  uint32_t header_size_field;
  uint32_t uncompressed_size;
  uint32_t payload_size;
  const uint8_t *payload;
  uint8_t *output;
  uint8_t ring[FZGX_LZ_N];
  uint32_t input_pos = 0u;
  uint32_t output_pos = 0u;
  uint32_t ring_pos = FZGX_LZ_N - FZGX_LZ_F;
  uint32_t flags = 0u;

  if ((compressed == 0) || (buffer_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(buffer_out, 0, sizeof(*buffer_out));
  if (compressed_size < 8u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  header_size_field = fzgx_read_u32le_exact(compressed, 0u);
  uncompressed_size = fzgx_read_u32le_exact(compressed, 4u);
  if (header_size_field == compressed_size) {
    payload_size = header_size_field - 8u;
  } else if ((header_size_field + 8u) == compressed_size) {
    payload_size = header_size_field;
  } else {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (!fzgx_range_is_valid_exact(compressed_size, 8u, payload_size)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  payload = compressed + 8u;
  output = (uint8_t *)malloc(uncompressed_size);
  if ((output == 0) && (uncompressed_size != 0u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  memset(ring, 0, sizeof(ring));

  while (input_pos < payload_size) {
    if ((flags & 0xff00u) == 0u) {
      flags = (uint32_t)payload[input_pos] | 0x8000u;
      ++input_pos;
    }

    if ((flags & 1u) != 0u) {
      if ((input_pos >= payload_size) || (output_pos >= uncompressed_size)) {
        free(output);
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      output[output_pos] = payload[input_pos];
      ring[ring_pos % FZGX_LZ_N] = payload[input_pos];
      ++input_pos;
      ++output_pos;
      ++ring_pos;
    } else {
      uint32_t lo;
      uint32_t hi;
      uint32_t index;
      uint32_t count;
      uint32_t i;

      if (!fzgx_range_is_valid_exact(payload_size, input_pos, 2u)) {
        free(output);
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      lo = payload[input_pos];
      hi = payload[input_pos + 1u];
      input_pos += 2u;
      index = ((hi & 0xf0u) << 4) | lo;
      count = (hi & 0x0fu) + FZGX_LZ_THRESHOLD;
      for (i = 0u; i < count; ++i) {
        uint8_t value;

        if (output_pos >= uncompressed_size) {
          free(output);
          return FZGX_STATUS_OUT_OF_RANGE;
        }
        value = ring[(index + i) % FZGX_LZ_N];
        output[output_pos] = value;
        ring[ring_pos % FZGX_LZ_N] = value;
        ++output_pos;
        ++ring_pos;
      }
    }
    flags >>= 1;
  }

  if (output_pos != uncompressed_size) {
    free(output);
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  buffer_out->data = output;
  buffer_out->size = uncompressed_size;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_prepare_scene_bytes_exact(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_byte_buffer *decoded_out,
    const uint8_t **scene_bytes_out,
    uint32_t *scene_size_out) {
  fzgx_status status;

  if ((data == 0) || (decoded_out == 0) || (scene_bytes_out == 0) || (scene_size_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(decoded_out, 0, sizeof(*decoded_out));
  *scene_bytes_out = 0;
  *scene_size_out = 0u;

  if ((size >= 8u) &&
      (((fzgx_read_u32le_exact(data, 0u) == size) ||
        ((fzgx_read_u32le_exact(data, 0u) + 8u) == size)))) {
    status = fzgx_decode_av_lz_exact(data, size, decoded_out);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    *scene_bytes_out = decoded_out->data;
    *scene_size_out = decoded_out->size;
    return FZGX_STATUS_OK;
  }

  *scene_bytes_out = data;
  *scene_size_out = size;
  return FZGX_STATUS_OK;
}

static void fzgx_content_release_static_collider_course_internal(
    fzgx_owned_static_collider_course *course) {
  if (course == 0) {
    return;
  }
  free(course->tris);
  free(course->quads);
  free(course->tri_indices);
  free(course->quad_indices);
  free(course->surface_grids);
  memset(course, 0, sizeof(*course));
}

static void fzgx_content_release_dynamic_scene_collision_course_internal(
    fzgx_owned_dynamic_scene_collision_course *course) {
  uint32_t object_index;
  uint32_t curve_index;

  if (course == 0) {
    return;
  }
  if (course->objects != 0) {
    for (object_index = 0u; object_index < course->object_count; ++object_index) {
      for (curve_index = 0u; curve_index < 11u; ++curve_index) {
        free((void *)course->objects[object_index].animation_clip.curves[curve_index].curve.keyables);
      }
      free(course->objects[object_index].collider_mesh.tris);
      free(course->objects[object_index].collider_mesh.quads);
    }
  }
  if (course->unknown_colliders != 0) {
    for (object_index = 0u; object_index < course->unknown_collider_count; ++object_index) {
      free(course->unknown_colliders[object_index].collider_mesh.tris);
      free(course->unknown_colliders[object_index].collider_mesh.quads);
    }
  }
  if (course->static_scene_objects != 0) {
    for (object_index = 0u; object_index < course->static_scene_object_count; ++object_index) {
      free(course->static_scene_objects[object_index].collider_mesh.tris);
      free(course->static_scene_objects[object_index].collider_mesh.quads);
    }
  }
  free(course->objects);
  free(course->unknown_colliders);
  free(course->static_scene_objects);
  memset(course, 0, sizeof(*course));
}

static void fzgx_content_release_track_mesh_course_internal(
    fzgx_owned_track_mesh_course *course) {
  uint32_t chunk_index;
  uint32_t channel_index;

  if (course == 0) {
    return;
  }
  if (course->chunks != 0) {
    for (chunk_index = 0u; chunk_index < course->chunk_count; ++chunk_index) {
      fzgx_owned_track_mesh_chunk *chunk = &course->chunks[chunk_index];
      for (channel_index = 0u; channel_index < FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT;
           ++channel_index) {
        free(chunk->animation_record.channels[channel_index].keyables);
      }
      free(chunk->tris);
      free(chunk->quads);
      free(chunk->tri_indices);
      free(chunk->quad_indices);
      free(chunk->tri_cells);
      free(chunk->quad_cells);
    }
  }
  free(course->chunks);
  memset(course, 0, sizeof(*course));
}

static void fzgx_content_release_stage_gma_model_table_internal(
    fzgx_owned_stage_gma_model_table *table) {
  if (table == 0) {
    return;
  }
  free(table->models);
  memset(table, 0, sizeof(*table));
}

static void fzgx_content_release_track_course_content_internal(
    fzgx_owned_track_course_content *course) {
  if (course == 0) {
    return;
  }
  free(course->time_extension_triggers);
  free(course->track_nodes);
  free(course->checkpoints);
  free(course->track_segments);
  free(course->track_corners);
  memset(course, 0, sizeof(*course));
}

static void fzgx_content_release_track_course_animation_content_internal(
    fzgx_owned_track_course_animation_content *course) {
  if (course == 0) {
    return;
  }
  free(course->track_segments);
  free(course->animation_curve_trs);
  free(course->animation_curves);
  free(course->keyable_attributes);
  memset(course, 0, sizeof(*course));
}

typedef struct fzgx_scene_track_header_exact {
  uint32_t circuit_type;
  float track_total_distance;
  float track_min_height;
  uint32_t track_node_count;
  uint32_t track_node_ptr;
  uint32_t time_extension_trigger_count;
  uint32_t time_extension_trigger_ptr;
} fzgx_scene_track_header_exact;

static int fzgx_compare_u32_exact(const void *lhs, const void *rhs) {
  uint32_t a = *(const uint32_t *)lhs;
  uint32_t b = *(const uint32_t *)rhs;

  if (a < b) {
    return -1;
  }
  if (a > b) {
    return 1;
  }
  return 0;
}

static int fzgx_compare_track_segment_record_address_exact(const void *lhs, const void *rhs) {
  const fzgx_track_segment_record *a = (const fzgx_track_segment_record *)lhs;
  const fzgx_track_segment_record *b = (const fzgx_track_segment_record *)rhs;

  if (a->address < b->address) {
    return -1;
  }
  if (a->address > b->address) {
    return 1;
  }
  return 0;
}

static int fzgx_u32_array_contains_exact(
    const uint32_t *values,
    uint32_t count,
    uint32_t value) {
  uint32_t index;

  for (index = 0u; index < count; ++index) {
    if (values[index] == value) {
      return 1;
    }
  }
  return 0;
}

static fzgx_status fzgx_u32_array_append_unique_exact(
    uint32_t **values_inout,
    uint32_t *count_inout,
    uint32_t *capacity_inout,
    uint32_t value) {
  uint32_t *values;
  uint32_t count;
  uint32_t capacity;

  if ((values_inout == 0) || (count_inout == 0) || (capacity_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  values = *values_inout;
  count = *count_inout;
  capacity = *capacity_inout;
  if (fzgx_u32_array_contains_exact(values, count, value)) {
    return FZGX_STATUS_OK;
  }
  if (count == capacity) {
    uint32_t new_capacity = (capacity == 0u) ? 16u : (capacity * 2u);
    uint32_t *new_values;

    if (new_capacity < count) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    new_values = (uint32_t *)realloc(values, new_capacity * sizeof(*new_values));
    if (new_values == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    values = new_values;
    capacity = new_capacity;
  }
  values[count] = value;
  *values_inout = values;
  *count_inout = count + 1u;
  *capacity_inout = capacity;
  return FZGX_STATUS_OK;
}

static int fzgx_track_segment_record_array_contains_exact(
    const fzgx_track_segment_record *segments,
    uint32_t count,
    uint32_t address) {
  uint32_t index;

  for (index = 0u; index < count; ++index) {
    if (segments[index].address == address) {
      return 1;
    }
  }
  return 0;
}

static fzgx_status fzgx_track_segment_record_array_append_exact(
    fzgx_track_segment_record **segments_inout,
    uint32_t *count_inout,
    uint32_t *capacity_inout,
    const fzgx_track_segment_record *segment) {
  fzgx_track_segment_record *segments;
  uint32_t count;
  uint32_t capacity;

  if ((segments_inout == 0) || (count_inout == 0) || (capacity_inout == 0) || (segment == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  segments = *segments_inout;
  count = *count_inout;
  capacity = *capacity_inout;
  if (count == capacity) {
    uint32_t new_capacity = (capacity == 0u) ? 32u : (capacity * 2u);
    fzgx_track_segment_record *new_segments;

    if (new_capacity < count) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    new_segments = (fzgx_track_segment_record *)realloc(
        segments, new_capacity * sizeof(*new_segments));
    if (new_segments == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    segments = new_segments;
    capacity = new_capacity;
  }
  segments[count] = *segment;
  *segments_inout = segments;
  *count_inout = count + 1u;
  *capacity_inout = capacity;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_scene_track_header_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    fzgx_scene_track_header_exact *header_out) {
  uint32_t scene_zeroes0x20_ptr;
  uint32_t scene_track_min_height_ptr;
  uint32_t track_total_distance_ptr;

  if ((scene_bytes == 0) || (header_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(header_out, 0, sizeof(*header_out));
  if (!fzgx_range_is_valid_exact(scene_size, 0xb4u, 8u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  scene_zeroes0x20_ptr = fzgx_read_u32be_exact(scene_bytes, 0x20u);
  scene_track_min_height_ptr = fzgx_read_u32be_exact(scene_bytes, 0x24u);
  if (!(((scene_zeroes0x20_ptr == 0xe8u) && (scene_track_min_height_ptr == 0xfcu)) ||
        ((scene_zeroes0x20_ptr == 0xe4u) && (scene_track_min_height_ptr == 0xf8u)))) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  header_out->track_node_count = fzgx_read_u32be_exact(scene_bytes, 0x08u);
  header_out->track_node_ptr = fzgx_read_u32be_exact(scene_bytes, 0x0cu);
  if ((header_out->track_node_count != 0u) &&
      !fzgx_range_is_valid_exact(scene_size, header_out->track_node_ptr,
                                 header_out->track_node_count * 0x0cu)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  track_total_distance_ptr = fzgx_read_u32be_exact(scene_bytes, 0x90u);
  if (!fzgx_range_is_valid_exact(scene_size, track_total_distance_ptr, 4u) ||
      !fzgx_range_is_valid_exact(scene_size, scene_track_min_height_ptr, 4u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  header_out->circuit_type = fzgx_read_u32be_exact(scene_bytes, 0x7cu);
  header_out->track_total_distance = fzgx_read_f32be_exact(scene_bytes, track_total_distance_ptr);
  header_out->track_min_height = fzgx_read_f32be_exact(scene_bytes, scene_track_min_height_ptr);
  header_out->time_extension_trigger_count = fzgx_read_u32be_exact(scene_bytes, 0xacu);
  header_out->time_extension_trigger_ptr = fzgx_read_u32be_exact(scene_bytes, 0xb0u);
  if ((header_out->time_extension_trigger_count != 0u) &&
      !fzgx_range_is_valid_exact(scene_size, header_out->time_extension_trigger_ptr,
                                 header_out->time_extension_trigger_count * 0x24u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  return FZGX_STATUS_OK;
}

static void fzgx_parse_plane_exact(
    const uint8_t *scene_bytes,
    uint32_t offset,
    fzgx_plane *plane_out) {
  plane_out->distance = fzgx_read_f32be_exact(scene_bytes, offset + 0x00u);
  fzgx_read_vec3be_exact(scene_bytes, offset + 0x04u, &plane_out->normal);
  fzgx_read_vec3be_exact(scene_bytes, offset + 0x10u, &plane_out->origin);
}

static fzgx_status fzgx_parse_track_checkpoint_record_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    uint32_t checkpoint_address,
    fzgx_checkpoint_record *checkpoint_out) {
  if ((scene_bytes == 0) || (checkpoint_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (!fzgx_range_is_valid_exact(scene_size, checkpoint_address, 0x50u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  checkpoint_out->curve_time_start = fzgx_read_f32be_exact(scene_bytes, checkpoint_address + 0x00u);
  checkpoint_out->curve_time_end = fzgx_read_f32be_exact(scene_bytes, checkpoint_address + 0x04u);
  fzgx_parse_plane_exact(scene_bytes, checkpoint_address + 0x08u, &checkpoint_out->plane_start);
  fzgx_parse_plane_exact(scene_bytes, checkpoint_address + 0x24u, &checkpoint_out->plane_end);
  checkpoint_out->start_distance = fzgx_read_f32be_exact(scene_bytes, checkpoint_address + 0x40u);
  checkpoint_out->end_distance = fzgx_read_f32be_exact(scene_bytes, checkpoint_address + 0x44u);
  checkpoint_out->track_width = fzgx_read_f32be_exact(scene_bytes, checkpoint_address + 0x48u);
  checkpoint_out->connect_to_track_in = scene_bytes[checkpoint_address + 0x4cu];
  checkpoint_out->connect_to_track_out = scene_bytes[checkpoint_address + 0x4du];
  checkpoint_out->reserved0x4e = fzgx_read_u16be_exact(scene_bytes, checkpoint_address + 0x4eu);
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_track_corner_record_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    uint32_t corner_address,
    fzgx_track_corner_record *corner_out) {
  if ((scene_bytes == 0) || (corner_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (!fzgx_range_is_valid_exact(scene_size, corner_address, 0x38u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  corner_out->address = corner_address;
  corner_out->transform.basis_x_x = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x00u);
  corner_out->transform.basis_y_x = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x04u);
  corner_out->transform.basis_z_x = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x08u);
  corner_out->transform.origin_x = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x0cu);
  corner_out->transform.basis_x_y = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x10u);
  corner_out->transform.basis_y_y = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x14u);
  corner_out->transform.basis_z_y = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x18u);
  corner_out->transform.origin_y = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x1cu);
  corner_out->transform.basis_x_z = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x20u);
  corner_out->transform.basis_y_z = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x24u);
  corner_out->transform.basis_z_z = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x28u);
  corner_out->transform.origin_z = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x2cu);
  corner_out->width = fzgx_read_f32be_exact(scene_bytes, corner_address + 0x30u);
  corner_out->const_0x34 = scene_bytes[corner_address + 0x34u];
  corner_out->zero_0x35 = scene_bytes[corner_address + 0x35u];
  corner_out->perimeter_flags = scene_bytes[corner_address + 0x36u];
  corner_out->zero_0x37 = scene_bytes[corner_address + 0x37u];
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_track_segment_record_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    uint32_t segment_address,
    fzgx_track_segment_record *segment_out) {
  if ((scene_bytes == 0) || (segment_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (!fzgx_range_is_valid_exact(scene_size, segment_address, 0x50u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  memset(segment_out, 0, sizeof(*segment_out));
  segment_out->address = segment_address;
  segment_out->segment_type = scene_bytes[segment_address + 0x00u];
  segment_out->embedded_property_type = scene_bytes[segment_address + 0x01u];
  segment_out->perimeter_flags = scene_bytes[segment_address + 0x02u];
  segment_out->pipe_cylinder_flags = scene_bytes[segment_address + 0x03u];
  segment_out->animation_curves_trs_address =
      fzgx_read_u32be_exact(scene_bytes, segment_address + 0x04u);
  segment_out->track_corner_address = fzgx_read_u32be_exact(scene_bytes, segment_address + 0x08u);
  segment_out->children_count = fzgx_read_u32be_exact(scene_bytes, segment_address + 0x0cu);
  segment_out->children_address = fzgx_read_u32be_exact(scene_bytes, segment_address + 0x10u);
  fzgx_read_vec3be_exact(scene_bytes, segment_address + 0x14u, &segment_out->fallback_scale);
  fzgx_read_vec3be_exact(scene_bytes, segment_address + 0x20u, &segment_out->fallback_rotation);
  fzgx_read_vec3be_exact(scene_bytes, segment_address + 0x2cu, &segment_out->fallback_position);
  segment_out->root_unk_0x38 = fzgx_read_u16be_exact(scene_bytes, segment_address + 0x38u);
  segment_out->root_unk_0x3a = fzgx_read_u16be_exact(scene_bytes, segment_address + 0x3au);
  segment_out->rail_height_right = fzgx_read_f32be_exact(scene_bytes, segment_address + 0x3cu);
  segment_out->rail_height_left = fzgx_read_f32be_exact(scene_bytes, segment_address + 0x40u);
  segment_out->zero_0x44 = fzgx_read_u32be_exact(scene_bytes, segment_address + 0x44u);
  segment_out->zero_0x48 = fzgx_read_u32be_exact(scene_bytes, segment_address + 0x48u);
  segment_out->branch_index = (int32_t)fzgx_read_u32be_exact(scene_bytes, segment_address + 0x4cu);
  if ((segment_out->children_count != 0u) &&
      !fzgx_range_is_valid_exact(scene_size, segment_out->children_address,
                                 segment_out->children_count * 0x50u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_collect_track_segment_records_recursive_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    uint32_t segment_address,
    fzgx_track_segment_record **segments_inout,
    uint32_t *segment_count_inout,
    uint32_t *segment_capacity_inout,
    uint32_t **corner_addresses_inout,
    uint32_t *corner_count_inout,
    uint32_t *corner_capacity_inout) {
  fzgx_track_segment_record segment;
  uint32_t child_index;
  fzgx_status status;

  if ((segments_inout == 0) || (segment_count_inout == 0) || (segment_capacity_inout == 0) ||
      (corner_addresses_inout == 0) || (corner_count_inout == 0) || (corner_capacity_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (segment_address == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (fzgx_track_segment_record_array_contains_exact(
          *segments_inout, *segment_count_inout, segment_address)) {
    return FZGX_STATUS_OK;
  }

  status = fzgx_parse_track_segment_record_exact(scene_bytes, scene_size, segment_address, &segment);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_record_array_append_exact(
      segments_inout, segment_count_inout, segment_capacity_inout, &segment);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (segment.track_corner_address != 0u) {
    status = fzgx_u32_array_append_unique_exact(
        corner_addresses_inout,
        corner_count_inout,
        corner_capacity_inout,
        segment.track_corner_address);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  for (child_index = 0u; child_index < segment.children_count; ++child_index) {
    status = fzgx_collect_track_segment_records_recursive_exact(
        scene_bytes,
        scene_size,
        segment.children_address + child_index * 0x50u,
        segments_inout,
        segment_count_inout,
        segment_capacity_inout,
        corner_addresses_inout,
        corner_count_inout,
        corner_capacity_inout);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_copy_c_string_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t string_ptr,
    char *string_out,
    uint32_t string_capacity) {
  uint32_t length = 0u;

  if ((scene_data == 0) || (string_out == 0) || (string_capacity == 0u)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  string_out[0] = '\0';
  if (string_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  while (1) {
    if (!fzgx_range_is_valid_exact(scene_size, string_ptr + length, 1u)) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    if (scene_data[string_ptr + length] == 0u) {
      break;
    }
    if ((length + 1u) >= string_capacity) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    string_out[length] = (char)scene_data[string_ptr + length];
    ++length;
  }
  string_out[length] = '\0';
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_transform_trxs_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t offset,
    fzgx_transform_trxs_record *transform_out) {
  if ((scene_data == 0) || (transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (!fzgx_range_is_valid_exact(scene_size, offset, 0x20u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  fzgx_read_vec3be_exact(scene_data, offset + 0x00u, &transform_out->position);
  transform_out->rotation_x_angle16 = fzgx_read_u16be_exact(scene_data, offset + 0x0cu);
  transform_out->rotation_y_angle16 = fzgx_read_u16be_exact(scene_data, offset + 0x0eu);
  transform_out->rotation_z_angle16 = fzgx_read_u16be_exact(scene_data, offset + 0x10u);
  transform_out->unknown_transform_option = scene_data[offset + 0x12u];
  transform_out->object_active_override = scene_data[offset + 0x13u];
  fzgx_read_vec3be_exact(scene_data, offset + 0x14u, &transform_out->scale);
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_scene_object_lod_name_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t scene_object_ptr,
    char *name_out,
    uint32_t name_capacity) {
  uint32_t lod_count;
  uint32_t lod_ptr;
  uint32_t name_ptr;

  if ((scene_data == 0) || (name_out == 0) || (name_capacity == 0u)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  name_out[0] = '\0';
  if (scene_object_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, scene_object_ptr, 0x10u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  lod_count = fzgx_read_u32be_exact(scene_data, scene_object_ptr + 0x04u);
  lod_ptr = fzgx_read_u32be_exact(scene_data, scene_object_ptr + 0x08u);
  if ((lod_count == 0u) || (lod_ptr == 0u)) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, lod_ptr, 0x10u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  name_ptr = fzgx_read_u32be_exact(scene_data, lod_ptr + 0x04u);
  return fzgx_copy_c_string_exact(scene_data, scene_size, name_ptr, name_out, name_capacity);
}

static fzgx_status fzgx_parse_scene_object_collision_transform_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t scene_object_ptr,
    uint32_t *transform_address_out,
    fzgx_transform_trxs_record *transform_out,
    uint8_t *has_transform_out) {
  if ((scene_data == 0) || (transform_address_out == 0) || (transform_out == 0) ||
      (has_transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *transform_address_out = 0u;
  memset(transform_out, 0, sizeof(*transform_out));
  *has_transform_out = 0u;
  (void)scene_size;
  (void)scene_object_ptr;

  /*
   * The serialized SceneObject template only carries LOD and optional collider-mesh
   * references. Dynamic-object transforms live in the 0x40-byte SceneObjectDynamic
   * record itself, with an optional matrix pointer there as well.
   *
   * Some shipped courses contain non-pointer payload at scene_object + 0x34, so
   * treating that word as a collision-transform pointer rejects valid content.
   */
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_transform_matrix_3x4_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t matrix_ptr,
    fzgx_mat43 *matrix_out,
    uint8_t *has_matrix_out) {
  if ((scene_data == 0) || (matrix_out == 0) || (has_matrix_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(matrix_out, 0, sizeof(*matrix_out));
  *has_matrix_out = 0u;
  if (matrix_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, matrix_ptr, 0x30u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  matrix_out->basis_x_x = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x00u);
  matrix_out->basis_y_x = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x04u);
  matrix_out->basis_z_x = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x08u);
  matrix_out->origin_x = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x0cu);
  matrix_out->basis_x_y = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x10u);
  matrix_out->basis_y_y = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x14u);
  matrix_out->basis_z_y = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x18u);
  matrix_out->origin_y = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x1cu);
  matrix_out->basis_x_z = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x20u);
  matrix_out->basis_y_z = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x24u);
  matrix_out->basis_z_z = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x28u);
  matrix_out->origin_z = fzgx_read_f32be_exact(scene_data, matrix_ptr + 0x2cu);
  *has_matrix_out = 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_animation_curve_keyables_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t keyable_ptr,
    uint32_t keyable_count,
    fzgx_keyable_attribute **keyables_out) {
  fzgx_keyable_attribute *keyables;
  uint32_t keyable_index;

  if ((scene_data == 0) || (keyables_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *keyables_out = 0;
  if (keyable_count == 0u) {
    return FZGX_STATUS_OK;
  }
  if ((keyable_ptr == 0u) ||
      !fzgx_range_is_valid_exact(scene_size, keyable_ptr, keyable_count * 0x14u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  keyables = (fzgx_keyable_attribute *)calloc(keyable_count, sizeof(*keyables));
  if (keyables == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  for (keyable_index = 0u; keyable_index < keyable_count; ++keyable_index) {
    uint32_t offset = keyable_ptr + keyable_index * 0x14u;

    keyables[keyable_index].interpolation_mode = fzgx_read_u32be_exact(scene_data, offset + 0x00u);
    keyables[keyable_index].time = fzgx_read_f32be_exact(scene_data, offset + 0x04u);
    keyables[keyable_index].value = fzgx_read_f32be_exact(scene_data, offset + 0x08u);
    keyables[keyable_index].tangent_in = fzgx_read_f32be_exact(scene_data, offset + 0x0cu);
    keyables[keyable_index].tangent_out = fzgx_read_f32be_exact(scene_data, offset + 0x10u);
  }
  *keyables_out = keyables;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_track_mesh_animation_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t animation_ptr,
    fzgx_owned_track_mesh_animation_record *animation_out,
    uint8_t *has_animation_out) {
  uint32_t channel_index;

  if ((scene_data == 0) || (animation_out == 0) || (has_animation_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(animation_out, 0, sizeof(*animation_out));
  *has_animation_out = 0u;
  if (animation_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(
          scene_size,
          animation_ptr,
          FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT * 8u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  animation_out->address = animation_ptr;
  for (channel_index = 0u; channel_index < FZGX_TRACK_MESH_ANIMATION_CHANNEL_COUNT;
       ++channel_index) {
    uint32_t channel_offset = animation_ptr + channel_index * 8u;
    uint32_t keyable_count = fzgx_read_u32be_exact(scene_data, channel_offset + 0x00u);
    uint32_t keyable_ptr = fzgx_read_u32be_exact(scene_data, channel_offset + 0x04u);
    fzgx_status status;

    animation_out->channels[channel_index].keyable_count = keyable_count;
    status = fzgx_parse_animation_curve_keyables_exact(
        scene_data,
        scene_size,
        keyable_ptr,
        keyable_count,
        &animation_out->channels[channel_index].keyables);
    if (status != FZGX_STATUS_OK) {
      uint32_t cleanup_index;
      for (cleanup_index = 0u; cleanup_index < channel_index; ++cleanup_index) {
        free(animation_out->channels[cleanup_index].keyables);
        animation_out->channels[cleanup_index].keyables = 0;
        animation_out->channels[cleanup_index].keyable_count = 0u;
      }
      memset(animation_out, 0, sizeof(*animation_out));
      return status;
    }
  }

  *has_animation_out = 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_dynamic_scene_animation_clip_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t clip_ptr,
    fzgx_owned_animation_clip_record *clip_out,
    uint8_t *has_clip_out) {
  uint32_t curve_index;

  if ((scene_data == 0) || (clip_out == 0) || (has_clip_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(clip_out, 0, sizeof(*clip_out));
  *has_clip_out = 0u;
  if (clip_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, clip_ptr, 0x124u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  clip_out->time_start_frames = fzgx_read_f32be_exact(scene_data, clip_ptr + 0x00u);
  clip_out->time_end_frames = fzgx_read_f32be_exact(scene_data, clip_ptr + 0x04u);
  clip_out->bank_time_frames[0] = fzgx_read_f32be_exact(scene_data, clip_ptr + 0x08u);
  clip_out->bank_time_frames[1] = fzgx_read_f32be_exact(scene_data, clip_ptr + 0x0cu);
  clip_out->bank_time_frames[2] = fzgx_read_f32be_exact(scene_data, clip_ptr + 0x10u);
  clip_out->bank_time_frames[3] = fzgx_read_f32be_exact(scene_data, clip_ptr + 0x14u);
  clip_out->layer_flags = fzgx_read_u32be_exact(scene_data, clip_ptr + 0x18u);
  for (curve_index = 0u; curve_index < 11u; ++curve_index) {
    uint32_t curve_offset = clip_ptr + 0x1cu + curve_index * 0x18u;
    uint32_t keyable_count = fzgx_read_u32be_exact(scene_data, curve_offset + 0x10u);
    uint32_t keyable_ptr = fzgx_read_u32be_exact(scene_data, curve_offset + 0x14u);
    fzgx_status status;

    clip_out->curves[curve_index].unknown_0x00 =
        fzgx_read_u32be_exact(scene_data, curve_offset + 0x00u);
    clip_out->curves[curve_index].unknown_0x04 =
        fzgx_read_u32be_exact(scene_data, curve_offset + 0x04u);
    clip_out->curves[curve_index].unknown_0x08 =
        fzgx_read_u32be_exact(scene_data, curve_offset + 0x08u);
    clip_out->curves[curve_index].unknown_0x0c =
        fzgx_read_u32be_exact(scene_data, curve_offset + 0x0cu);
    clip_out->curves[curve_index].curve.keyable_count = keyable_count;
    if (keyable_count == 0u) {
      continue;
    }
    status = fzgx_parse_animation_curve_keyables_exact(
        scene_data,
        scene_size,
        keyable_ptr,
        keyable_count,
        (fzgx_keyable_attribute **)&clip_out->curves[curve_index].curve.keyables);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  *has_clip_out = 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_scene_object_collider_mesh_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t scene_object_ptr,
    fzgx_owned_scene_object_collider_mesh *mesh_out,
    uint8_t *has_mesh_out) {
  uint32_t mesh_ptr;
  fzgx_status status;

  if ((scene_data == 0) || (mesh_out == 0) || (has_mesh_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(mesh_out, 0, sizeof(*mesh_out));
  *has_mesh_out = 0u;
  if (scene_object_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, scene_object_ptr, 0x10u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  mesh_ptr = fzgx_read_u32be_exact(scene_data, scene_object_ptr + 0x0cu);
  if (mesh_ptr == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, mesh_ptr, 0x24u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  mesh_out->collider_type = fzgx_read_u32be_exact(scene_data, mesh_ptr + 0x00u);
  fzgx_read_vec3be_exact(scene_data, mesh_ptr + 0x04u, &mesh_out->bounding_sphere.origin);
  mesh_out->bounding_sphere.radius = fzgx_read_f32be_exact(scene_data, mesh_ptr + 0x10u);
  mesh_out->tri_count = fzgx_read_u32be_exact(scene_data, mesh_ptr + 0x14u);
  mesh_out->quad_count = fzgx_read_u32be_exact(scene_data, mesh_ptr + 0x18u);

  status = fzgx_parse_static_collider_triangle_array_exact(
      scene_data,
      scene_size,
      fzgx_read_u32be_exact(scene_data, mesh_ptr + 0x1cu),
      mesh_out->tri_count,
      &mesh_out->tris);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_parse_static_collider_quad_array_exact(
      scene_data,
      scene_size,
      fzgx_read_u32be_exact(scene_data, mesh_ptr + 0x20u),
      mesh_out->quad_count,
      &mesh_out->quads);
  if (status != FZGX_STATUS_OK) {
    free(mesh_out->tris);
    mesh_out->tris = 0;
    mesh_out->tri_count = 0u;
    return status;
  }

  *has_mesh_out = 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_static_collider_indices_append_exact(
    uint16_t **indices_inout,
    uint32_t *count_inout,
    uint32_t *capacity_inout,
    uint16_t value) {
  uint16_t *resized;
  uint32_t new_capacity;

  if ((indices_inout == 0) || (count_inout == 0) || (capacity_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (*count_inout >= *capacity_inout) {
    new_capacity = (*capacity_inout == 0u) ? 64u : (*capacity_inout * 2u);
    resized = (uint16_t *)realloc(*indices_inout, (size_t)new_capacity * sizeof(**indices_inout));
    if (resized == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    *indices_inout = resized;
    *capacity_inout = new_capacity;
  }
  (*indices_inout)[*count_inout] = value;
  *count_inout += 1u;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_static_collider_index_grid_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t grid_ptr,
    fzgx_static_collider_index_span *spans_out,
    uint16_t **indices_inout,
    uint32_t *index_count_inout,
    uint32_t *index_capacity_inout,
    uint32_t *largest_index_inout) {
  uint32_t cell_index;

  if ((scene_data == 0) || (spans_out == 0) || (indices_inout == 0) || (index_count_inout == 0) ||
      (index_capacity_inout == 0) || (largest_index_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (!fzgx_range_is_valid_exact(scene_size, grid_ptr, FZGX_COLI_INDEX_GRID_POINTER_TABLE_SIZE)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  for (cell_index = 0u; cell_index < FZGX_COLI_INDEX_GRID_CELL_COUNT; ++cell_index) {
    uint32_t list_ptr = fzgx_read_u32be_exact(scene_data, grid_ptr + cell_index * 4u);

    spans_out[cell_index].offset = *index_count_inout;
    spans_out[cell_index].count = 0u;
    if (list_ptr == 0u) {
      continue;
    }
    while (1) {
      uint16_t value;
      fzgx_status status;

      if (!fzgx_range_is_valid_exact(scene_size, list_ptr, 2u)) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      value = fzgx_read_u16be_exact(scene_data, list_ptr);
      list_ptr += 2u;
      if (value == 0xffffu) {
        break;
      }
      status = fzgx_static_collider_indices_append_exact(
          indices_inout, index_count_inout, index_capacity_inout, value);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      spans_out[cell_index].count += 1u;
      if (value > *largest_index_inout) {
        *largest_index_inout = value;
      }
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_static_collider_triangle_array_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t tri_ptr,
    uint32_t tri_count,
    fzgx_static_collider_triangle_record **tris_out) {
  fzgx_static_collider_triangle_record *tris;
  uint32_t tri_index;

  if ((scene_data == 0) || (tris_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *tris_out = 0;
  if (tri_count == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(
          scene_size, tri_ptr, tri_count * FZGX_COLI_TRIANGLE_RECORD_SIZE)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  tris = (fzgx_static_collider_triangle_record *)calloc(tri_count, sizeof(*tris));
  if (tris == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  for (tri_index = 0u; tri_index < tri_count; ++tri_index) {
    uint32_t offset = tri_ptr + tri_index * FZGX_COLI_TRIANGLE_RECORD_SIZE;

    tris[tri_index].plane_distance = fzgx_read_f32be_exact(scene_data, offset + 0x00u);
    fzgx_read_vec3be_exact(scene_data, offset + 0x04u, &tris[tri_index].normal);
    fzgx_read_vec3be_exact(scene_data, offset + 0x10u, &tris[tri_index].vertex0);
    fzgx_read_vec3be_exact(scene_data, offset + 0x1cu, &tris[tri_index].vertex1);
    fzgx_read_vec3be_exact(scene_data, offset + 0x28u, &tris[tri_index].vertex2);
    fzgx_read_vec3be_exact(scene_data, offset + 0x34u, &tris[tri_index].edge_normal0);
    fzgx_read_vec3be_exact(scene_data, offset + 0x40u, &tris[tri_index].edge_normal1);
    fzgx_read_vec3be_exact(scene_data, offset + 0x4cu, &tris[tri_index].edge_normal2);
  }
  *tris_out = tris;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_static_collider_quad_array_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t quad_ptr,
    uint32_t quad_count,
    fzgx_static_collider_quad_record **quads_out) {
  fzgx_static_collider_quad_record *quads;
  uint32_t quad_index;

  if ((scene_data == 0) || (quads_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *quads_out = 0;
  if (quad_count == 0u) {
    return FZGX_STATUS_OK;
  }
  if (!fzgx_range_is_valid_exact(scene_size, quad_ptr, quad_count * FZGX_COLI_QUAD_RECORD_SIZE)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  quads = (fzgx_static_collider_quad_record *)calloc(quad_count, sizeof(*quads));
  if (quads == 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  for (quad_index = 0u; quad_index < quad_count; ++quad_index) {
    uint32_t offset = quad_ptr + quad_index * FZGX_COLI_QUAD_RECORD_SIZE;

    quads[quad_index].plane_distance = fzgx_read_f32be_exact(scene_data, offset + 0x00u);
    fzgx_read_vec3be_exact(scene_data, offset + 0x04u, &quads[quad_index].normal);
    fzgx_read_vec3be_exact(scene_data, offset + 0x10u, &quads[quad_index].vertex0);
    fzgx_read_vec3be_exact(scene_data, offset + 0x1cu, &quads[quad_index].vertex1);
    fzgx_read_vec3be_exact(scene_data, offset + 0x28u, &quads[quad_index].vertex2);
    fzgx_read_vec3be_exact(scene_data, offset + 0x34u, &quads[quad_index].vertex3);
    fzgx_read_vec3be_exact(scene_data, offset + 0x40u, &quads[quad_index].edge_normal0);
    fzgx_read_vec3be_exact(scene_data, offset + 0x4cu, &quads[quad_index].edge_normal1);
    fzgx_read_vec3be_exact(scene_data, offset + 0x58u, &quads[quad_index].edge_normal2);
    fzgx_read_vec3be_exact(scene_data, offset + 0x64u, &quads[quad_index].edge_normal3);
  }
  *quads_out = quads;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_parse_track_mesh_index_grid_exact(
    const uint8_t *scene_data,
    uint32_t scene_size,
    uint32_t grid_ptr,
    uint32_t cell_count,
    fzgx_static_collider_index_span *spans_out,
    uint16_t **indices_inout,
    uint32_t *index_count_inout,
    uint32_t *index_capacity_inout,
    uint32_t *largest_index_inout) {
  uint32_t cell_index;

  if ((scene_data == 0) || (spans_out == 0) || (indices_inout == 0) || (index_count_inout == 0) ||
      (index_capacity_inout == 0) || (largest_index_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (!fzgx_range_is_valid_exact(scene_size, grid_ptr, cell_count * 4u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  for (cell_index = 0u; cell_index < cell_count; ++cell_index) {
    uint32_t list_ptr = fzgx_read_u32be_exact(scene_data, grid_ptr + cell_index * 4u);

    spans_out[cell_index].offset = *index_count_inout;
    spans_out[cell_index].count = 0u;
    if (list_ptr != 0u) {
      uint32_t offset = list_ptr;

      while (1) {
        uint16_t index_be;
        uint32_t index;

        if (!fzgx_range_is_valid_exact(scene_size, offset, 2u)) {
          return FZGX_STATUS_OUT_OF_RANGE;
        }
        index_be = fzgx_read_u16be_exact(scene_data, offset);
        offset += 2u;
        if (index_be == 0xffffu) {
          break;
        }
        index = (uint32_t)index_be;
        if (0xffffu <= index) {
          return FZGX_STATUS_OUT_OF_RANGE;
        }
        if (*largest_index_inout < index) {
          *largest_index_inout = index;
        }
        {
          fzgx_status status = fzgx_static_collider_indices_append_exact(
              indices_inout,
              index_count_inout,
              index_capacity_inout,
              (uint16_t)index);
          if (status != FZGX_STATUS_OK) {
            return status;
          }
        }
        ++spans_out[cell_index].count;
      }
    }
  }

  return FZGX_STATUS_OK;
}

static float fzgx_plane_eval_point(const fzgx_plane *plane, const fzgx_vec3 *point) {
  return plane->distance +
         point->x * plane->normal.x +
         point->y * plane->normal.y +
         point->z * plane->normal.z;
}

static float fzgx_checkpoint_query_epsilon(uint32_t authored_track_id) {
  if (authored_track_id == 0x19u) {
    return 1.7f;
  }
  return 0.001f;
}

static uint32_t fzgx_count_leading_zeros_u32_exact(uint32_t value) {
  if (value == 0u) {
    return 32u;
  }
  return (uint32_t)__builtin_clz(value);
}

static float fzgx_course7_checkpoint_lower_z_margin(void) {
  return 36.0f;
}

static float fzgx_vec3_distance_squared(const fzgx_vec3 *a, const fzgx_vec3 *b) {
  float dx = a->x - b->x;
  float dy = a->y - b->y;
  float dz = a->z - b->z;
  return dx * dx + dy * dy + dz * dz;
}

static float fzgx_vec3_length_squared(const fzgx_vec3 *value) {
  return value->x * value->x + value->y * value->y + value->z * value->z;
}

static fzgx_vec3 fzgx_vec3_cross(const fzgx_vec3 *lhs, const fzgx_vec3 *rhs) {
  fzgx_vec3 out;
  out.x = lhs->y * rhs->z - lhs->z * rhs->y;
  out.y = lhs->z * rhs->x - lhs->x * rhs->z;
  out.z = lhs->x * rhs->y - lhs->y * rhs->x;
  return out;
}

static int fzgx_vec3_normalize_in_place(fzgx_vec3 *value) {
  float length_squared;
  float length;

  if (value == 0) {
    return 0;
  }
  length_squared = fzgx_vec3_length_squared(value);
  if (length_squared <= 0.0f) {
    return 0;
  }
  length = sqrtf(length_squared);
  if (length <= 0.0f) {
    return 0;
  }
  value->x /= length;
  value->y /= length;
  value->z /= length;
  return 1;
}

static fzgx_mat43 fzgx_mat43_identity(void) {
  fzgx_mat43 transform = {0};
  transform.basis_x_x = 1.0f;
  transform.basis_y_y = 1.0f;
  transform.basis_z_z = 1.0f;
  return transform;
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

static void fzgx_mat43_translate_right(fzgx_mat43 *transform, float x, float y, float z) {
  transform->origin_x =
      transform->basis_z_x * z + transform->basis_x_x * x + transform->origin_x +
      transform->basis_y_x * y;
  transform->origin_y =
      transform->basis_z_y * z + transform->basis_x_y * x + transform->origin_y +
      transform->basis_y_y * y;
  transform->origin_z =
      transform->basis_z_z * z + transform->basis_x_z * x + transform->origin_z +
      transform->basis_y_z * y;
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

static void fzgx_mat43_scale_basis(
    fzgx_mat43 *transform,
    float scale_x,
    float scale_y,
    float scale_z) {
  if (transform == 0) {
    return;
  }
  transform->basis_x_x *= scale_x;
  transform->basis_x_y *= scale_x;
  transform->basis_x_z *= scale_x;
  transform->basis_y_x *= scale_y;
  transform->basis_y_y *= scale_y;
  transform->basis_y_z *= scale_y;
  transform->basis_z_x *= scale_z;
  transform->basis_z_y *= scale_z;
  transform->basis_z_z *= scale_z;
}

static fzgx_status fzgx_track_frame_evaluate_offset_exact(
    float lateral_offset,
    float width,
    float scale,
    uint32_t use_follow_offset_origin,
    const fzgx_track_frame_record *frame,
    fzgx_vec3 *world_pos_out) {
  static const float fzgx_track_cross_section_half_exact = 0.5f;
  static const float fzgx_track_cross_section_double_exact = 2.0f;
  static const float fzgx_track_capsule_rotation_scale_exact = 32768.0f;
  fzgx_mat43 transform;
  uint32_t flags;
  float signed_scale = scale;

  if ((frame == 0) || (world_pos_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  transform = frame->track_current_transform;
  if ((use_follow_offset_origin != 0u) && (scale == 1.0f)) {
    transform.origin_x = frame->track_follow_offset.x;
    transform.origin_y = frame->track_follow_offset.y;
    transform.origin_z = frame->track_follow_offset.z;
  }
  flags = frame->track_flags;
  if ((flags & 1u) != 0u) {
    signed_scale = -signed_scale;
  }

  if (((flags & 0x02200000u) == 0u) && (signed_scale != 0.0f)) {
    if ((flags & 0x01800000u) == 0u) {
      if ((flags & 0x00400000u) == 0u) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      {
        float shoulder_fraction =
            fzgx_track_cross_section_half_exact * (frame->track_scl_x / width);
        float side_sign = (lateral_offset > 0.0f) ? 1.0f : -1.0f;

        if (signed_scale != 1.0f) {
          fzgx_mat43_scale_basis(&transform, 1.0f, signed_scale, 1.0f);
        }
        if (shoulder_fraction <= fabsf(lateral_offset)) {
          if (fabsf(lateral_offset) <=
              (fzgx_track_cross_section_half_exact - shoulder_fraction)) {
            float translate_x =
                fzgx_track_cross_section_half_exact * (side_sign * frame->track_scl_x);
            float rotation_ratio =
                (lateral_offset - side_sign * shoulder_fraction) /
                (fzgx_track_cross_section_half_exact -
                 fzgx_track_cross_section_double_exact * shoulder_fraction);

            fzgx_mat43_translate_right(&transform, translate_x, 0.0f, 0.0f);
            fzgx_mat43_rotate_about_z_right(
                &transform,
                (uint16_t)(int32_t)(fzgx_track_capsule_rotation_scale_exact * rotation_ratio));
            fzgx_mat43_translate_right(
                &transform, 0.0f, -fzgx_track_cross_section_half_exact * frame->track_scl_y, 0.0f);
          } else {
            fzgx_mat43_translate_right(
                &transform,
                width * (fzgx_track_cross_section_half_exact * side_sign - lateral_offset),
                fzgx_track_cross_section_half_exact * frame->track_scl_y,
                0.0f);
          }
        } else {
          fzgx_mat43_translate_right(
              &transform,
              lateral_offset * width,
              -fzgx_track_cross_section_half_exact * frame->track_scl_y,
              0.0f);
        }
      }
    } else {
      fzgx_mat43_scale_basis(&transform, 1.0f, signed_scale, 1.0f);
      fzgx_mat43_rotate_about_z_right(
          &transform,
          (uint16_t)(int32_t)(
              fzgx_track_capsule_rotation_scale_exact * fzgx_track_cross_section_double_exact *
              lateral_offset * frame->track_hcylin));
      fzgx_mat43_translate_right(
          &transform,
          0.0f,
          -fzgx_track_cross_section_half_exact * frame->track_width_or_radius,
          0.0f);
    }
  } else {
    fzgx_mat43_translate_right(&transform, lateral_offset * width, 0.0f, 0.0f);
  }

  world_pos_out->x = transform.origin_x;
  world_pos_out->y = transform.origin_y;
  world_pos_out->z = transform.origin_z;
  return FZGX_STATUS_OK;
}

static uint32_t fzgx_track_segment_is_skipped_in_cached_frame_walk(uint32_t source_piece_word) {
  return (uint32_t)((source_piece_word & 0x001e0002u) != 0u);
}

static uint32_t fzgx_track_segment_is_ordinary_driveable_track(
    const fzgx_track_segment_record *track_segment) {
  return (uint32_t)((track_segment->segment_type & 0x02u) != 0u);
}

static uint32_t fzgx_track_segment_matches_branch_slot(
    const fzgx_track_segment_record *track_segment,
    int32_t branch_slot) {
  if (track_segment->branch_index == 0) {
    return 1u;
  }
  return (uint32_t)(track_segment->branch_index == branch_slot);
}

static void fzgx_track_frame_build_inverse_vectors_exact(
    const fzgx_mat43 *transform,
    fzgx_vec3 *track_forward_out,
    fzgx_vec3 *track_up_out) {
  fzgx_mat43 inverse;

  if ((transform == 0) || (track_forward_out == 0) || (track_up_out == 0)) {
    return;
  }

  inverse = *transform;
  fzgx_mat43_rigid_invert_exact(&inverse);
  *track_forward_out = (fzgx_vec3){
      -inverse.basis_x_z,
      -inverse.basis_y_z,
      -inverse.basis_z_z,
  };
  *track_up_out = (fzgx_vec3){
      inverse.basis_x_y,
      inverse.basis_y_y,
      inverse.basis_z_y,
  };
}

fzgx_status fzgx_track_build_frame_surface_tail(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    const fzgx_mat43 *transform,
    const fzgx_vec3 *scale,
    uint32_t source_piece_word,
    fzgx_track_frame_surface_tail *tail_out) {
  fzgx_vec3 track_forward;
  fzgx_vec3 track_up;
  fzgx_track_frame_surface_tail tail;
  fzgx_status status;

  if ((course == 0) || (track_segment == 0) || (transform == 0) || (scale == 0) || (tail_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  memset(&tail, 0, sizeof(tail));
  tail.track_follow_offset = fzgx_mat43_get_origin_exact(transform);
  fzgx_track_frame_build_inverse_vectors_exact(
      transform, &track_forward, &track_up);

  if ((source_piece_word & 0x00200000u) == 0u) {
    tail.track_width_or_radius = scale->x;
    tail.track_hcylin = 1.0f;
  }

  if ((source_piece_word & 0x00400000u) != 0u) {
    float curve_position_x;
    float curve_scale_y;

    status = fzgx_track_segment_sample_curve_or_fallback(
        track_segment,
        animation_segment,
        FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X,
        track_segment->fallback_position.x,
        time,
        &curve_position_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_segment_sample_curve_or_fallback(
        track_segment,
        animation_segment,
        FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y,
        track_segment->fallback_scale.y,
        time,
        &curve_scale_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    tail.track_scl_x = fabsf((2.0f * curve_position_x) * scale->x);
    tail.track_scl_y = fabsf(curve_scale_y * scale->y);
    tail.track_width_or_radius = scale->x * ((2.0f * curve_position_x) + curve_scale_y);
    tail.track_follow_offset.x =
        transform->origin_x + 0.5f * tail.track_scl_y * track_up.x;
    tail.track_follow_offset.y =
        transform->origin_y + 0.5f * tail.track_scl_y * track_up.y;
    tail.track_follow_offset.z =
        transform->origin_z + 0.5f * tail.track_scl_y * track_up.z;
    *tail_out = tail;
    return FZGX_STATUS_OK;
  }

  if (((source_piece_word & 0x00800000u) != 0u) && (track_segment->children_count == 1u)) {
    const fzgx_track_segment_record *children = 0;
    uint32_t child_count = 0u;

    status = fzgx_track_course_get_track_segment_children(
        course, track_segment, &children, &child_count);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if (child_count == 1u) {
      const fzgx_track_segment_record *child_segment = &children[0];
      const fzgx_track_segment_animation_record *child_animation_segment = 0;
      float child_scale_y;

      if (animation_course != 0) {
        status = fzgx_track_course_animation_find_track_segment_by_address(
            animation_course, child_segment->address, &child_animation_segment);
        if (status == FZGX_STATUS_OUT_OF_RANGE) {
          child_animation_segment = 0;
        } else if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
      status = fzgx_track_segment_sample_curve_or_fallback(
          child_segment,
          child_animation_segment,
          FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y,
          child_segment->fallback_scale.y,
          time,
          &child_scale_y);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      tail.track_hcylin = child_scale_y;
    }
  }

  if ((source_piece_word & 0x01800000u) != 0u) {
    tail.track_follow_offset.x =
        transform->origin_x + 0.5f * scale->y * track_up.x;
    tail.track_follow_offset.y =
        transform->origin_y + 0.5f * scale->y * track_up.y;
    tail.track_follow_offset.z =
        transform->origin_z + 0.5f * scale->y * track_up.z;
  }

  *tail_out = tail;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_track_frame_record_write_exact(
    fzgx_track_frame_record *frame_out,
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    const fzgx_mat43 *transform,
    const fzgx_vec3 *scale,
    uint32_t source_piece_word) {
  fzgx_track_frame_surface_tail surface_tail;
  fzgx_status status;

  if ((frame_out == 0) || (course == 0) || (track_segment == 0) || (transform == 0) ||
      (scale == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  memset(frame_out, 0, sizeof(*frame_out));
  frame_out->track_current_transform = *transform;
  frame_out->track_current_scale = *scale;
  frame_out->track_anchor = fzgx_mat43_get_origin_exact(transform);
  fzgx_track_frame_build_inverse_vectors_exact(
      transform, &frame_out->track_forward, &frame_out->track_up);
  frame_out->track_flags = source_piece_word & 0x07e00c01u;
  status = fzgx_track_build_frame_surface_tail(
      course,
      animation_course,
      track_segment,
      animation_segment,
      time,
      transform,
      scale,
      source_piece_word,
      &surface_tail);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  frame_out->track_scl_x = surface_tail.track_scl_x;
  frame_out->track_scl_y = surface_tail.track_scl_y;
  frame_out->track_width_or_radius = surface_tail.track_width_or_radius;
  frame_out->track_hcylin = surface_tail.track_hcylin;
  frame_out->track_follow_offset = surface_tail.track_follow_offset;
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_populate_track_cached_frame_node_exact(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    float time,
    fzgx_mat43 *transform_inout,
    fzgx_vec3 *scale_inout,
    fzgx_track_frame_record *cached_frames_out,
    uint32_t cached_frame_capacity,
    uint32_t *cached_frame_cursor_inout) {
  const fzgx_track_segment_animation_record *animation_segment = 0;
  const fzgx_track_segment_record *children = 0;
  const fzgx_track_segment_record *child_segment = 0;
  const fzgx_track_segment_animation_record *child_animation_segment = 0;
  fzgx_track_frame_record *cached_frame;
  fzgx_status status;
  uint32_t source_piece_word;
  uint32_t slot_index;
  uint32_t child_count = 0u;
  float curve_position_x = 0.0f;
  float curve_scale_y = 0.0f;
  float curve_width = 0.0f;
  float child_scale_y = 0.0f;

  if ((course == 0) || (track_segment == 0) || (transform_inout == 0) || (scale_inout == 0) ||
      (cached_frames_out == 0) || (cached_frame_cursor_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  cached_frame = &cached_frames_out[*cached_frame_cursor_inout];
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (fzgx_track_segment_is_skipped_in_cached_frame_walk(source_piece_word) != 0u) {
    return FZGX_STATUS_OK;
  }

  if (animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((source_piece_word & 0x00600000u) == 0u) {
    status = fzgx_track_segment_apply_trs(
        track_segment, animation_segment, time, transform_inout, scale_inout, 0);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if (((source_piece_word & 0x04000000u) != 0u) &&
      (((*cached_frame_cursor_inout == 0u) || (*cached_frame_cursor_inout == 1u)) &&
       ((*cached_frame_cursor_inout + 1u) < cached_frame_capacity))) {
    *cached_frame_cursor_inout += 1u;
    cached_frame = &cached_frames_out[*cached_frame_cursor_inout];
    cached_frame->track_flags = 0u;
  }
  slot_index = *cached_frame_cursor_inout;
  if ((cached_frame_capacity != 0u) && (cached_frame_capacity <= slot_index)) {
    slot_index = cached_frame_capacity - 1u;
  }
  cached_frame = &cached_frames_out[slot_index];

  if ((source_piece_word & 0x00400000u) != 0u) {
    curve_position_x = track_segment->fallback_position.x;
    if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
        (animation_segment->animation_curve_trs->curves != 0) &&
        (animation_segment->animation_curve_trs->curve_count >
         FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X)) {
      const fzgx_animation_curve *curve =
          &animation_segment->animation_curve_trs
               ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X];

      if ((curve->keyable_count != 0u) && (curve->keyables != 0)) {
        status = fzgx_evaluate_float_animation_curve(curve, time, &curve_position_x);
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
         FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y)) {
      const fzgx_animation_curve *curve =
          &animation_segment->animation_curve_trs
               ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y];

      if ((curve->keyable_count != 0u) && (curve->keyables != 0)) {
        status = fzgx_evaluate_float_animation_curve(curve, time, &curve_scale_y);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
      }
    }
    cached_frame->track_scl_x = fabsf(curve_position_x * scale_inout->x);
    cached_frame->track_scl_y = fabsf(curve_scale_y * scale_inout->y);
    curve_width = scale_inout->x * (curve_position_x + curve_scale_y);
  }

  cached_frame->track_current_transform = *transform_inout;
  cached_frame->track_current_scale = *scale_inout;
  cached_frame->track_anchor = fzgx_mat43_get_origin_exact(transform_inout);
  {
    fzgx_mat43 inverse_transform = *transform_inout;

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
  }
  cached_frame->track_flags |= source_piece_word & 0x07e00c01u;

  if (((source_piece_word & 0x00800000u) != 0u) && (track_segment->children_count == 1u)) {
    cached_frame->track_width_or_radius = scale_inout->x;
    status = fzgx_track_course_get_track_segment_children(
        course, track_segment, &children, &child_count);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if (child_count == 1u) {
      child_segment = &children[0];
      child_scale_y = child_segment->fallback_scale.y;
      if (animation_course != 0) {
        status = fzgx_track_course_animation_find_track_segment_by_address(
            animation_course, child_segment->address, &child_animation_segment);
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
           FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y)) {
        const fzgx_animation_curve *curve =
            &child_animation_segment->animation_curve_trs
                 ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y];

        if ((curve->keyable_count != 0u) && (curve->keyables != 0)) {
          status = fzgx_evaluate_float_animation_curve(curve, time, &child_scale_y);
          if (status != FZGX_STATUS_OK) {
            return status;
          }
        }
      }
      cached_frame->track_hcylin = child_scale_y;
    }
  } else if ((source_piece_word & 0x00200000u) == 0u) {
    cached_frame->track_width_or_radius =
        ((source_piece_word & 0x00400000u) == 0u) ? scale_inout->x : curve_width;
    cached_frame->track_hcylin = 1.0f;
  }

  if ((source_piece_word & 0x00400000u) == 0u) {
    if ((source_piece_word & 0x01800000u) == 0u) {
      cached_frame->track_follow_offset = cached_frame->track_anchor;
    } else {
      cached_frame->track_follow_offset.x =
          transform_inout->origin_x + 0.5f * scale_inout->y * cached_frame->track_up.x;
      cached_frame->track_follow_offset.y =
          transform_inout->origin_y + 0.5f * scale_inout->y * cached_frame->track_up.y;
      cached_frame->track_follow_offset.z =
          transform_inout->origin_z + 0.5f * scale_inout->y * cached_frame->track_up.z;
    }
  } else {
    cached_frame->track_follow_offset.x =
        transform_inout->origin_x + 0.5f * cached_frame->track_scl_y * cached_frame->track_up.x;
    cached_frame->track_follow_offset.y =
        transform_inout->origin_y + 0.5f * cached_frame->track_scl_y * cached_frame->track_up.y;
    cached_frame->track_follow_offset.z =
        transform_inout->origin_z + 0.5f * cached_frame->track_scl_y * cached_frame->track_up.z;
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_populate_track_cached_frames_from_piece_children_exact(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    float time,
    fzgx_mat43 *transform_inout,
    fzgx_vec3 *scale_inout,
    fzgx_track_segment_trs_curve_cache_exact *curve_cache_inout,
    fzgx_track_frame_record *cached_frames_out,
    uint32_t cached_frame_capacity,
    uint32_t *cached_frame_cursor_inout) {
  const fzgx_track_segment_record *children = 0;
  uint32_t child_count = 0u;
  uint32_t child_index;
  fzgx_status status;

  if ((course == 0) || (track_segment == 0) || (transform_inout == 0) || (scale_inout == 0) ||
      (cached_frames_out == 0) || (cached_frame_cursor_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_course_get_track_segment_children(
      course, track_segment, &children, &child_count);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (child_count != 0u) {
    fzgx_mat43 saved_transform = *transform_inout;
    fzgx_vec3 saved_scale = *scale_inout;

    for (child_index = 0u; child_index < child_count; ++child_index) {
      uint32_t source_piece_word = 0u;

      status = fzgx_track_segment_build_source_piece_word(&children[child_index], &source_piece_word);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((source_piece_word & 0x001e0002u) == 0u) {
        *transform_inout = saved_transform;
        *scale_inout = saved_scale;
        status = fzgx_populate_track_cached_frames_from_piece_tree_exact(
            course,
            animation_course,
            &children[child_index],
            time,
            transform_inout,
            scale_inout,
            curve_cache_inout,
            cached_frames_out,
            cached_frame_capacity,
            cached_frame_cursor_inout);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        *transform_inout = saved_transform;
        *scale_inout = saved_scale;
      }
    }
    *transform_inout = saved_transform;
    *scale_inout = saved_scale;
  }
  return FZGX_STATUS_OK;
}

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
    uint32_t *cached_frame_cursor_inout) {
  const fzgx_track_segment_record *children20 = 0;
  const fzgx_track_segment_record *children18 = 0;
  const fzgx_track_segment_record *children16 = 0;
  const fzgx_track_segment_record *children14 = 0;
  uint32_t child_count20 = 0u;
  uint32_t child_count18 = 0u;
  uint32_t child_count16 = 0u;
  uint32_t child_count14 = 0u;
  uint32_t child_index20;
  uint32_t child_index18;
  uint32_t child_index16;
  uint32_t child_index14;
  fzgx_status status;

  if ((course == 0) || (track_segment == 0) || (transform_inout == 0) || (scale_inout == 0) ||
      (cached_frames_out == 0) || (cached_frame_cursor_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_populate_track_cached_frame_node_exact(
      course,
      animation_course,
      track_segment,
      time,
      transform_inout,
      scale_inout,
      cached_frames_out,
      cached_frame_capacity,
      cached_frame_cursor_inout);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_track_course_get_track_segment_children(
      course, track_segment, &children20, &child_count20);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (child_count20 != 0u) {
    fzgx_mat43 saved_transform20 = *transform_inout;
    fzgx_vec3 saved_scale20 = *scale_inout;

    for (child_index20 = 0u; child_index20 < child_count20; ++child_index20) {
      uint32_t source_piece_word20 = 0u;

      status = fzgx_track_segment_build_source_piece_word(
          &children20[child_index20], &source_piece_word20);
      if (status != FZGX_STATUS_OK) {
        return status;
      }
      if ((source_piece_word20 & 0x001e0002u) == 0u) {
        *transform_inout = saved_transform20;
        *scale_inout = saved_scale20;
        status = fzgx_populate_track_cached_frame_node_exact(
            course,
            animation_course,
            &children20[child_index20],
            time,
            transform_inout,
            scale_inout,
            cached_frames_out,
            cached_frame_capacity,
            cached_frame_cursor_inout);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        status = fzgx_track_course_get_track_segment_children(
            course, &children20[child_index20], &children18, &child_count18);
        if (status != FZGX_STATUS_OK) {
          return status;
        }
        if (child_count18 != 0u) {
          fzgx_mat43 saved_transform18 = *transform_inout;
          fzgx_vec3 saved_scale18 = *scale_inout;

          for (child_index18 = 0u; child_index18 < child_count18; ++child_index18) {
            uint32_t source_piece_word18 = 0u;

            status = fzgx_track_segment_build_source_piece_word(
                &children18[child_index18], &source_piece_word18);
            if (status != FZGX_STATUS_OK) {
              return status;
            }
            if ((source_piece_word18 & 0x001e0002u) == 0u) {
              *transform_inout = saved_transform18;
              *scale_inout = saved_scale18;
              status = fzgx_populate_track_cached_frame_node_exact(
                  course,
                  animation_course,
                  &children18[child_index18],
                  time,
                  transform_inout,
                  scale_inout,
                  cached_frames_out,
                  cached_frame_capacity,
                  cached_frame_cursor_inout);
              if (status != FZGX_STATUS_OK) {
                return status;
              }
              status = fzgx_track_course_get_track_segment_children(
                  course, &children18[child_index18], &children16, &child_count16);
              if (status != FZGX_STATUS_OK) {
                return status;
              }
              if (child_count16 != 0u) {
                fzgx_mat43 saved_transform16 = *transform_inout;
                fzgx_vec3 saved_scale16 = *scale_inout;

                for (child_index16 = 0u; child_index16 < child_count16; ++child_index16) {
                  uint32_t source_piece_word16 = 0u;

                  status = fzgx_track_segment_build_source_piece_word(
                      &children16[child_index16], &source_piece_word16);
                  if (status != FZGX_STATUS_OK) {
                    return status;
                  }
                  if ((source_piece_word16 & 0x001e0002u) == 0u) {
                    *transform_inout = saved_transform16;
                    *scale_inout = saved_scale16;
                    status = fzgx_populate_track_cached_frame_node_exact(
                        course,
                        animation_course,
                        &children16[child_index16],
                        time,
                        transform_inout,
                        scale_inout,
                        cached_frames_out,
                        cached_frame_capacity,
                        cached_frame_cursor_inout);
                    if (status != FZGX_STATUS_OK) {
                      return status;
                    }
                    status = fzgx_track_course_get_track_segment_children(
                        course, &children16[child_index16], &children14, &child_count14);
                    if (status != FZGX_STATUS_OK) {
                      return status;
                    }
                    if (child_count14 != 0u) {
                      fzgx_mat43 saved_transform14 = *transform_inout;
                      fzgx_vec3 saved_scale14 = *scale_inout;

                      for (child_index14 = 0u; child_index14 < child_count14; ++child_index14) {
                        uint32_t source_piece_word14 = 0u;

                        status = fzgx_track_segment_build_source_piece_word(
                            &children14[child_index14], &source_piece_word14);
                        if (status != FZGX_STATUS_OK) {
                          return status;
                        }
                        if ((source_piece_word14 & 0x001e0002u) == 0u) {
                          *transform_inout = saved_transform14;
                          *scale_inout = saved_scale14;
                          status = fzgx_populate_track_cached_frame_node_exact(
                              course,
                              animation_course,
                              &children14[child_index14],
                              time,
                              transform_inout,
                              scale_inout,
                              cached_frames_out,
                              cached_frame_capacity,
                              cached_frame_cursor_inout);
                          if (status != FZGX_STATUS_OK) {
                            return status;
                          }
                          status = fzgx_populate_track_cached_frames_from_piece_children_exact(
                              course,
                              animation_course,
                              &children14[child_index14],
                              time,
                              transform_inout,
                              scale_inout,
                              curve_cache_inout,
                              cached_frames_out,
                              cached_frame_capacity,
                              cached_frame_cursor_inout);
                          if (status != FZGX_STATUS_OK) {
                            return status;
                          }
                          *transform_inout = saved_transform14;
                          *scale_inout = saved_scale14;
                        }
                      }
                      *transform_inout = saved_transform14;
                      *scale_inout = saved_scale14;
                    }
                    *transform_inout = saved_transform16;
                    *scale_inout = saved_scale16;
                  }
                }
                *transform_inout = saved_transform16;
                *scale_inout = saved_scale16;
              }
              *transform_inout = saved_transform18;
              *scale_inout = saved_scale18;
            }
          }
          *transform_inout = saved_transform18;
          *scale_inout = saved_scale18;
        }
        *transform_inout = saved_transform20;
        *scale_inout = saved_scale20;
      }
    }
    *transform_inout = saved_transform20;
    *scale_inout = saved_scale20;
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_track_course_find_first_ordinary_start_grid_frame_recursive(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    const fzgx_track_segment_record *track_segment,
    float time,
    int32_t branch_slot,
    const fzgx_mat43 *parent_transform,
    const fzgx_vec3 *parent_scale,
    fzgx_track_frame_record *frame_out,
    uint32_t *found_out) {
  const fzgx_track_segment_animation_record *animation_segment = 0;
  const fzgx_track_segment_record *children = 0;
  fzgx_mat43 transform;
  fzgx_vec3 scale;
  uint32_t source_piece_word;
  uint32_t child_count = 0u;
  uint32_t child_index;
  fzgx_status status;

  if ((course == 0) || (track_segment == 0) || (parent_transform == 0) || (parent_scale == 0) ||
      (frame_out == 0) || (found_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (fzgx_track_segment_matches_branch_slot(track_segment, branch_slot) == 0u) {
    return FZGX_STATUS_OK;
  }
  status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (fzgx_track_segment_is_skipped_in_cached_frame_walk(source_piece_word) != 0u) {
    return FZGX_STATUS_OK;
  }

  transform = *parent_transform;
  scale = *parent_scale;
  if (animation_course != 0) {
    status = fzgx_track_course_animation_find_track_segment_by_address(
        animation_course, track_segment->address, &animation_segment);
    if (status == FZGX_STATUS_OUT_OF_RANGE) {
      animation_segment = 0;
    } else if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((source_piece_word & 0x00600000u) == 0u) {
    status = fzgx_track_segment_apply_trs(
        track_segment, animation_segment, time, &transform, &scale, 0);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  if (fzgx_track_segment_is_ordinary_driveable_track(track_segment) != 0u) {
    status = fzgx_track_frame_record_write_exact(
        frame_out,
        course,
        animation_course,
        track_segment,
        animation_segment,
        time,
        &transform,
        &scale,
        source_piece_word);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    *found_out = 1u;
    return FZGX_STATUS_OK;
  }

  status = fzgx_track_course_get_track_segment_children(
      course, track_segment, &children, &child_count);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  for (child_index = 0u; child_index < child_count; ++child_index) {
    status = fzgx_track_course_find_first_ordinary_start_grid_frame_recursive(
        course,
        animation_course,
        &children[child_index],
        time,
        branch_slot,
        &transform,
        &scale,
        frame_out,
        found_out);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if (*found_out != 0u) {
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OK;
}

static fzgx_status fzgx_track_course_build_ordinary_start_grid_slot_frame(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    uint32_t slot_index,
    int32_t *checkpoint_index_out,
    float *checkpoint_fraction_out,
    fzgx_track_frame_record *frame_out,
    fzgx_mat43 *placement_transform_out) {
  fzgx_mat43 transform;
  fzgx_vec3 query_point;
  fzgx_current_track_query_result query_result;
  int32_t checkpoint_index = 0;
  float checkpoint_fraction = 0.0f;
  float lane_fraction;
  fzgx_status status;

  if ((course == 0) || (checkpoint_index_out == 0) || (checkpoint_fraction_out == 0) ||
      (frame_out == 0) || (placement_transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  query_point.x = 0.0f;
  query_point.y = 0.0f;
  query_point.z = 19.0f + (float)slot_index * 12.9f;
  status = fzgx_track_course_find_shared_checkpoint_for_point(
      course,
      authored_track_id,
      circuit_type,
      &query_point,
      &checkpoint_index,
      &checkpoint_fraction);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  memset(&query_result, 0, sizeof(query_result));
  status = fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
      course,
      animation_course,
      authored_track_id,
      circuit_type,
      &query_point,
      checkpoint_index,
      checkpoint_fraction,
      &query_result);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  *frame_out = query_result.frame;
  transform = query_result.frame.track_current_transform;
  lane_fraction = 0.5f - (float)(slot_index % 6u) / 5.0f;
  fzgx_mat43_translate_right(
      &transform,
      query_result.frame.track_width_or_radius * 0.5f * lane_fraction,
      1.0f,
      0.0f);

  *checkpoint_index_out = checkpoint_index;
  *checkpoint_fraction_out = checkpoint_fraction;
  *placement_transform_out = transform;
  return FZGX_STATUS_OK;
}

static float fzgx_evaluate_cubic_animation_curve_segment(
    const fzgx_keyable_attribute *key0,
    const fzgx_keyable_attribute *key1,
    float time) {
  float segment_time = key1->time - key0->time;
  float time_fraction = (time - key0->time) / segment_time;
  float time_fraction_squared = time_fraction * time_fraction;
  float cubic_minus_square =
      (time_fraction * time_fraction_squared) - time_fraction_squared;
  float cubic_weight = (cubic_minus_square + cubic_minus_square) - time_fraction_squared;
  return (((key0->tangent_out *
                ((cubic_minus_square - time_fraction_squared) + time_fraction) +
            cubic_minus_square * key1->tangent_in) *
           segment_time) +
          ((key0->value * (cubic_weight + 1.0f)) - cubic_weight * key1->value));
}

static fzgx_status fzgx_track_segment_sample_curve_or_fallback(
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    uint32_t curve_index,
    float fallback_value,
    float time,
    float *value_out) {
  if ((track_segment == 0) || (value_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((animation_segment == 0) ||
      (animation_segment->animation_curve_trs == 0) ||
      (curve_index >= animation_segment->animation_curve_trs->curve_count)) {
    *value_out = fallback_value;
    return FZGX_STATUS_OK;
  }
  if (animation_segment->animation_curve_trs->curves == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (animation_segment->address != track_segment->address) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (animation_segment->animation_curve_trs->curves[curve_index].keyable_count == 0u) {
    *value_out = fallback_value;
    return FZGX_STATUS_OK;
  }
  return fzgx_evaluate_float_animation_curve(
      &animation_segment->animation_curve_trs->curves[curve_index], time, value_out);
}

static fzgx_status fzgx_track_course_eval_checkpoint_point(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    uint32_t variant_index,
    float t,
    fzgx_vec3 *point_out) {
  const fzgx_checkpoint_record *checkpoint = 0;
  if ((course == 0) || (point_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (fzgx_track_course_get_checkpoint_variant(course, track_node_index, variant_index, &checkpoint) !=
      FZGX_STATUS_OK) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  point_out->x =
      checkpoint->plane_start.origin.x +
      (checkpoint->plane_end.origin.x - checkpoint->plane_start.origin.x) * t;
  point_out->y =
      checkpoint->plane_start.origin.y +
      (checkpoint->plane_end.origin.y - checkpoint->plane_start.origin.y) * t;
  point_out->z =
      checkpoint->plane_start.origin.z +
      (checkpoint->plane_end.origin.z - checkpoint->plane_start.origin.z) * t;
  return FZGX_STATUS_OK;
}

static void fzgx_init_current_checkpoint_query_result(
    fzgx_current_checkpoint_query_result *query_out) {
  query_out->checkpoint_index = -1;
  query_out->checkpoint_fraction = 0.0f;
  query_out->variant_index = 0u;
  query_out->reserved0 = 0u;
  query_out->point_on_track = (fzgx_vec3){0};
}

static void fzgx_init_active_checkpoint_bank_result(
    fzgx_active_checkpoint_bank_result *bank_out,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction) {
  int i;
  bank_out->checkpoint_variant_count = 0u;
  bank_out->preferred_variant_slot = 0u;
  for (i = 0; i < 4; ++i) {
    bank_out->checkpoint_index[i] = active_checkpoint_index;
    bank_out->checkpoint_fraction[i] = active_checkpoint_fraction;
    bank_out->containment_checkpoint_index[i] = -1;
  }
}

static fzgx_status fzgx_checkpoint_variant_contains_point_internal(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t track_node_index,
    uint32_t variant_index,
    int32_t *checkpoint_index_out) {
  const fzgx_track_node_record *track_node;
  const fzgx_checkpoint_record *checkpoint;
  float epsilon;
  float start_plane_distance;
  float end_plane_distance;
  uint32_t start_ok;
  uint32_t end_ok;
  if ((course == 0) || (point == 0) || (checkpoint_index_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *checkpoint_index_out = -1;
  if ((course->track_nodes == 0) || (course->checkpoints == 0) || (course->track_node_count == 0u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((track_node_index < 0) || ((uint32_t)track_node_index >= course->track_node_count)) {
    track_node_index = 0;
  }
  track_node = &course->track_nodes[(uint32_t)track_node_index];
  if (variant_index >= track_node->checkpoint_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((track_node->checkpoint_offset + variant_index) >= course->checkpoint_record_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  checkpoint = &course->checkpoints[track_node->checkpoint_offset + variant_index];
  epsilon = fzgx_checkpoint_query_epsilon(authored_track_id);
  start_plane_distance = fzgx_plane_eval_point(&checkpoint->plane_start, point);
  end_plane_distance = fzgx_plane_eval_point(&checkpoint->plane_end, point);
  if (track_node_index == 0) {
    start_ok = (uint32_t)(end_plane_distance >= -epsilon);
    end_ok = (uint32_t)(point->z < 0.0f);
    if (circuit_type == FZGX_CIRCUIT_TYPE_OPEN) {
      end_ok = 1u;
    }
  } else if ((uint32_t)track_node_index == (course->track_node_count - 1u)) {
    start_ok = (uint32_t)(point->z >= 0.0f);
    end_ok = (uint32_t)(start_plane_distance >= -epsilon);
    if ((circuit_type == FZGX_CIRCUIT_TYPE_OPEN) && (authored_track_id != 0x29u)) {
      start_ok = 1u;
    }
  } else {
    start_ok = (uint32_t)(end_plane_distance >= -epsilon);
    end_ok = (uint32_t)(start_plane_distance >= -epsilon);
  }
  if (((start_ok == 0u) || (end_ok == 0u)) &&
      (((authored_track_id != 7u) ||
        ((track_node_index < 0x20) || (0x2b < track_node_index) ||
         (point->z < (checkpoint->plane_start.origin.z - fzgx_course7_checkpoint_lower_z_margin())))) ||
       ((checkpoint->plane_end.origin.z + epsilon) < point->z))) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  *checkpoint_index_out = track_node_index;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_content_bundle_validate(const fzgx_content_bundle *bundle) {
  uint32_t index;
  if (bundle == 0) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (bundle->api_version != FZGX_CONTENT_API_VERSION) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_count != 0u) && (bundle->tracks == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->machine_count != 0u) && (bundle->machines == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_course_count != 0u) && (bundle->track_courses == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_animation_course_count != 0u) && (bundle->track_animations == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (index = 0u; index < bundle->track_course_count; ++index) {
    const fzgx_track_course_content *course = &bundle->track_courses[index];
    uint32_t segment_index;
    uint32_t corner_index;
    if ((course->track_node_count != 0u) && (course->track_nodes == 0)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
    if ((course->checkpoint_record_count != 0u) && (course->checkpoints == 0)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
    if ((course->track_segment_count != 0u) && (course->track_segments == 0)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
    if ((course->track_corner_count != 0u) && (course->track_corners == 0)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
    for (corner_index = 0u; corner_index < course->track_corner_count; ++corner_index) {
      const fzgx_track_corner_record *corner = &course->track_corners[corner_index];
      if ((corner->const_0x34 != 0x02u) ||
          (corner->zero_0x35 != 0u) ||
          (corner->zero_0x37 != 0u)) {
        return FZGX_STATUS_BAD_ARGUMENT;
      }
    }
    for (segment_index = 0u; segment_index < course->track_segment_count; ++segment_index) {
      const fzgx_track_segment_record *segment = &course->track_segments[segment_index];
      if (segment->track_corner_address != 0u) {
        const fzgx_track_corner_record *corner = 0;
        if (fzgx_track_course_find_track_corner_by_address(
                course, segment->track_corner_address, &corner) != FZGX_STATUS_OK) {
          return FZGX_STATUS_BAD_ARGUMENT;
        }
      }
    }
  }
  for (index = 0u; index < bundle->track_animation_course_count; ++index) {
    const fzgx_track_course_animation_content *course = &bundle->track_animations[index];
    uint32_t segment_index;
    if ((course->track_segment_count != 0u) && (course->track_segments == 0)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
    for (segment_index = 0u; segment_index < course->track_segment_count; ++segment_index) {
      const fzgx_track_segment_animation_record *segment = &course->track_segments[segment_index];
      uint32_t curve_index;
      if (segment->animation_curve_trs == 0) {
        continue;
      }
      if ((segment->animation_curves_trs_address == 0u) ||
          (segment->animation_curve_trs->curve_count != 9u) ||
          (segment->animation_curve_trs->curves == 0)) {
        return FZGX_STATUS_BAD_ARGUMENT;
      }
      for (curve_index = 0u; curve_index < segment->animation_curve_trs->curve_count; ++curve_index) {
        const fzgx_animation_curve *curve = &segment->animation_curve_trs->curves[curve_index];
        if ((curve->keyable_count != 0u) && (curve->keyables == 0)) {
          return FZGX_STATUS_BAD_ARGUMENT;
        }
      }
    }
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_frame_export_buffer_select(
    const fzgx_track_frame_export_buffer *buffer,
    fzgx_track_frame_record *frame_out) {
  if ((buffer == 0) || (frame_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((buffer->cached_frame_count != 0u) && (buffer->cached_frames == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((buffer->selected_frame_index < 0) ||
      ((uint32_t)buffer->selected_frame_index >= buffer->cached_frame_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  *frame_out = buffer->cached_frames[buffer->selected_frame_index];
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_frame_export_buffer_find_nearest_anchor_index(
    const fzgx_track_frame_export_buffer *buffer,
    const fzgx_vec3 *point,
    int32_t *selected_index_out) {
  uint32_t index;
  int32_t best_index;
  float best_distance_squared;
  if ((buffer == 0) || (point == 0) || (selected_index_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((buffer->cached_frame_count != 0u) && (buffer->cached_frames == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (buffer->cached_frame_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (buffer->cached_frame_count < 2u) {
    *selected_index_out = 0;
    return FZGX_STATUS_OK;
  }
  best_index = 1;
  best_distance_squared =
      fzgx_vec3_distance_squared(point, &buffer->cached_frames[1].track_anchor);
  for (index = 2u; index < buffer->cached_frame_count; ++index) {
    float distance_squared =
        fzgx_vec3_distance_squared(point, &buffer->cached_frames[index].track_anchor);
    if (distance_squared < best_distance_squared) {
      best_index = (int32_t)index;
      best_distance_squared = distance_squared;
    }
  }
  *selected_index_out = best_index;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_frame_export_buffer_build_nearest_anchor_selection(
    const fzgx_track_frame_record *cached_frames,
    uint32_t cached_frame_count,
    const fzgx_vec3 *point,
    fzgx_track_frame_export_buffer *buffer_out) {
  fzgx_track_frame_export_buffer buffer;
  fzgx_status status;

  if ((point == 0) || (buffer_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  buffer.cached_frames = cached_frames;
  buffer.cached_frame_count = cached_frame_count;
  buffer.selected_frame_index = 0;
  status = fzgx_track_frame_export_buffer_find_nearest_anchor_index(&buffer, point,
                                                                    &buffer.selected_frame_index);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  *buffer_out = buffer;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_frame_get_width_and_scale(
    const fzgx_track_frame_record *frame,
    float *width_out,
    float *scale_out) {
  static const float fzgx_track_curved_ratio_threshold_exact = 0.05f;
  static const float fzgx_track_curve_width_factor_exact = 2.1415927410125732f;
  static const float fzgx_track_cross_section_double_exact = 2.0f;
  float scale = 0.0f;
  float width;
  uint32_t flags;

  if ((frame == 0) || (width_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  flags = frame->track_flags;
  if ((flags & 0x01c00000u) == 0u) {
    if ((flags & 0x02200000u) == 0u) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    scale = 1.0f;
  } else if (frame->track_current_scale.x != 0.0f) {
    scale = frame->track_current_scale.y / frame->track_current_scale.x;
    if (scale < fzgx_track_curved_ratio_threshold_exact) {
      scale = 0.0f;
    }
  }

  if (((flags & 0x02200000u) == 0u) && (scale != 0.0f)) {
    if ((flags & 0x01800000u) != 0u) {
      width = (1.0f + fzgx_track_curve_width_factor_exact * scale) *
              frame->track_hcylin * frame->track_width_or_radius;
    } else if ((flags & 0x00400000u) != 0u) {
      width = fzgx_track_cross_section_double_exact * frame->track_scl_x +
              (1.0f + fzgx_track_curve_width_factor_exact * scale) *
                  (frame->track_width_or_radius - frame->track_scl_x);
    } else {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
  } else {
    width = frame->track_width_or_radius;
  }

  *width_out = width;
  if (scale_out != 0) {
    *scale_out = scale;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_frame_sample_world_pos(
    float lateral_offset,
    const fzgx_track_frame_record *frame,
    fzgx_vec3 *world_pos_out) {
  float width;
  float scale;
  fzgx_status status;

  if ((frame == 0) || (world_pos_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_track_frame_get_width_and_scale(frame, &width, &scale);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_frame_evaluate_offset_exact(
      lateral_offset, width, scale, 1u, frame, world_pos_out);
}

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
    fzgx_vec3 *world_pos_out) {
  if ((course == 0) || (track_segment == 0) || (transform == 0) || (scale == 0) ||
      (world_pos_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  if ((source_piece_word & 0x00200000u) != 0u) {
    const fzgx_animation_curve *profile_curve = 0;
    float profile_height;

    if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0) &&
        (animation_segment->animation_curve_trs->curves != 0) &&
        (animation_segment->animation_curve_trs->curve_count >
         FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y)) {
      profile_curve = &animation_segment->animation_curve_trs
                           ->curves[FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y];
    }

    if ((profile_curve == 0) || (profile_curve->keyables == 0) ||
        (profile_curve->keyable_count == 0u)) {
      profile_height = track_segment->fallback_position.y;
    } else {
      const fzgx_keyable_attribute *first_profile_keyable =
          &profile_curve->keyables[0];
      const fzgx_keyable_attribute *last_profile_keyable =
          &profile_curve->keyables[profile_curve->keyable_count - 1u];
      float profile_curve_time =
          first_profile_keyable->time +
          ((0.5f + lateral_offset) *
           (last_profile_keyable->time - first_profile_keyable->time));
      fzgx_status status;

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

    world_pos_out->x =
        transform->origin_x +
        transform->basis_x_x * (lateral_offset * scale->x) +
        transform->basis_y_x * (profile_height * scale->y);
    world_pos_out->y =
        transform->origin_y +
        transform->basis_x_y * (lateral_offset * scale->x) +
        transform->basis_y_y * (profile_height * scale->y);
    world_pos_out->z =
        transform->origin_z +
        transform->basis_x_z * (lateral_offset * scale->x) +
        transform->basis_y_z * (profile_height * scale->y);
    return FZGX_STATUS_OK;
  }

  {
    fzgx_track_frame_record frame;
    float width;
    float scale_ratio;
    fzgx_status status = fzgx_track_frame_record_write_exact(
        &frame,
        course,
        animation_course,
        track_segment,
        animation_segment,
        time,
        transform,
        scale,
        source_piece_word);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_frame_get_width_and_scale(&frame, &width, &scale_ratio);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    return fzgx_track_frame_evaluate_offset_exact(
        lateral_offset,
        width,
        scale_ratio,
        0u,
        &frame,
        world_pos_out);
  }
}

fzgx_status fzgx_content_bundle_find_track_course(
    const fzgx_content_bundle *bundle,
    uint32_t authored_track_id,
    const fzgx_track_course_content **course_out) {
  uint32_t index;

  if ((bundle == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_course_count != 0u) && (bundle->track_courses == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (index = 0u; index < bundle->track_course_count; ++index) {
    const fzgx_track_course_content *course = &bundle->track_courses[index];
    if (course->authored_track_id == authored_track_id) {
      *course_out = course;
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_content_bundle_find_track_course_animation(
    const fzgx_content_bundle *bundle,
    uint32_t authored_track_id,
    const fzgx_track_course_animation_content **course_out) {
  uint32_t index;

  if ((bundle == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_animation_course_count != 0u) && (bundle->track_animations == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (index = 0u; index < bundle->track_animation_course_count; ++index) {
    const fzgx_track_course_animation_content *course = &bundle->track_animations[index];
    if (course->authored_track_id == authored_track_id) {
      *course_out = course;
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_content_bundle_get_track_course_for_track_index(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    const fzgx_track_course_content **course_out) {
  if ((bundle == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_count != 0u) && (bundle->tracks == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (track_index >= bundle->track_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  return fzgx_content_bundle_find_track_course(
      bundle, bundle->tracks[track_index].authored_track_id, course_out);
}

fzgx_status fzgx_content_bundle_get_track_course_animation_for_track_index(
    const fzgx_content_bundle *bundle,
    uint32_t track_index,
    const fzgx_track_course_animation_content **course_out) {
  if ((bundle == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((bundle->track_count != 0u) && (bundle->tracks == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (track_index >= bundle->track_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  return fzgx_content_bundle_find_track_course_animation(
      bundle, bundle->tracks[track_index].authored_track_id, course_out);
}

fzgx_status fzgx_track_course_get_track_node(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    const fzgx_track_node_record **track_node_out) {
  if ((course == 0) || (track_node_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((course->track_node_count != 0u) && (course->track_nodes == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (track_node_index >= course->track_node_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  *track_node_out = &course->track_nodes[track_node_index];
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_animation_find_track_segment_by_address(
    const fzgx_track_course_animation_content *course,
    uint32_t address,
    const fzgx_track_segment_animation_record **track_segment_out) {
  uint32_t index;
  if ((course == 0) || (track_segment_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((course->track_segment_count != 0u) && (course->track_segments == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (index = 0u; index < course->track_segment_count; ++index) {
    if (course->track_segments[index].address == address) {
      *track_segment_out = &course->track_segments[index];
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_track_course_find_track_segment_by_address(
    const fzgx_track_course_content *course,
    uint32_t address,
    const fzgx_track_segment_record **track_segment_out) {
  uint32_t index;
  if ((course == 0) || (track_segment_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((course->track_segment_count != 0u) && (course->track_segments == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (index = 0u; index < course->track_segment_count; ++index) {
    if (course->track_segments[index].address == address) {
      *track_segment_out = &course->track_segments[index];
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_track_course_find_track_corner_by_address(
    const fzgx_track_course_content *course,
    uint32_t address,
    const fzgx_track_corner_record **track_corner_out) {
  uint32_t index;
  if ((course == 0) || (track_corner_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((course->track_corner_count != 0u) && (course->track_corners == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  for (index = 0u; index < course->track_corner_count; ++index) {
    if (course->track_corners[index].address == address) {
      *track_corner_out = &course->track_corners[index];
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_evaluate_float_animation_curve(
    const fzgx_animation_curve *curve,
    float time,
    float *value_out) {
  uint32_t segment_index;
  const fzgx_keyable_attribute *key0;
  const fzgx_keyable_attribute *key1;

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
    *value_out = curve->keyables[0].value;
    return FZGX_STATUS_OK;
  }
  if (curve->keyables[curve->keyable_count - 1u].time <= time) {
    *value_out = curve->keyables[curve->keyable_count - 1u].value;
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
    *value_out = key0->value;
    return FZGX_STATUS_OK;
  }
  if (key0->interpolation_mode == 1u) {
    float time_fraction = (time - key0->time) / (key1->time - key0->time);
    *value_out = (key0->value * (1.0f - time_fraction)) + (key1->value * time_fraction);
    return FZGX_STATUS_OK;
  }
  *value_out = fzgx_evaluate_cubic_animation_curve_segment(key0, key1, time);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_evaluate_float_animation_curve_cached(
    const fzgx_animation_curve *curve,
    float time,
    int32_t *last_segment_index_inout,
    float *value_out) {
  int32_t segment_index;
  int32_t max_segment_index;
  const fzgx_keyable_attribute *key0;
  const fzgx_keyable_attribute *key1;

  if ((curve == 0) || (last_segment_index_inout == 0) || (value_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((curve->keyable_count != 0u) && (curve->keyables == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (curve->keyable_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  max_segment_index = (int32_t)curve->keyable_count - 2;
  if ((max_segment_index < 0) || (time <= curve->keyables[0].time)) {
    *last_segment_index_inout = 0;
    *value_out = curve->keyables[0].value;
    return FZGX_STATUS_OK;
  }
  if (time >= curve->keyables[curve->keyable_count - 1u].time) {
    *last_segment_index_inout = max_segment_index;
    *value_out = curve->keyables[curve->keyable_count - 1u].value;
    return FZGX_STATUS_OK;
  }
  segment_index = *last_segment_index_inout;
  if (segment_index < 0) {
    segment_index = 0;
  } else if (segment_index > max_segment_index) {
    segment_index = max_segment_index;
  }
  key0 = &curve->keyables[segment_index];
  if (time < key0->time) {
    do {
      --segment_index;
      if (segment_index < 0) {
        segment_index = 0;
        break;
      }
      key0 = &curve->keyables[segment_index];
    } while (time < key0->time);
    *last_segment_index_inout = segment_index;
  } else {
    while ((segment_index + 1 < (int32_t)curve->keyable_count - 1) &&
           (curve->keyables[segment_index + 1].time <= time)) {
      ++segment_index;
    }
    *last_segment_index_inout = segment_index;
    key0 = &curve->keyables[segment_index];
  }
  key1 = &curve->keyables[segment_index + 1];
  if (key0->interpolation_mode == 0u) {
    *value_out = key0->value;
    return FZGX_STATUS_OK;
  }
  if (key0->interpolation_mode == 1u) {
    float time_fraction = (time - key0->time) / (key1->time - key0->time);
    *value_out = (key0->value * (1.0f - time_fraction)) + (key1->value * time_fraction);
    return FZGX_STATUS_OK;
  }
  *value_out = fzgx_evaluate_cubic_animation_curve_segment(key0, key1, time);
  return FZGX_STATUS_OK;
}

static float fzgx_angle16_to_degrees_exact(uint16_t angle16) {
  return ((float)(int16_t)angle16 / 32768.0f) * 180.0f;
}

static uint16_t fzgx_degrees_to_angle16_exact(float degrees) {
  return (uint16_t)(int32_t)(degrees * (32768.0f / 180.0f));
}

fzgx_status fzgx_dynamic_scene_object_sample_transform_trxs(
    const fzgx_owned_dynamic_scene_object_record *object,
    float clip_time_seconds,
    fzgx_transform_trxs_record *transform_out) {
  float rotation_x_degrees;
  float rotation_y_degrees;
  float rotation_z_degrees;
  float position_x;
  float position_y;
  float position_z;
  float scale_x;
  float scale_y;
  float scale_z;
  uint32_t curve_index;

  if ((object == 0) || (transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  *transform_out = object->transform;
  if (object->has_animation_clip == 0u) {
    return FZGX_STATUS_OK;
  }

  scale_x = object->transform.scale.x;
  scale_y = object->transform.scale.y;
  scale_z = object->transform.scale.z;
  rotation_x_degrees = fzgx_angle16_to_degrees_exact(object->transform.rotation_x_angle16);
  rotation_y_degrees = fzgx_angle16_to_degrees_exact(object->transform.rotation_y_angle16);
  rotation_z_degrees = fzgx_angle16_to_degrees_exact(object->transform.rotation_z_angle16);
  position_x = object->transform.position.x;
  position_y = object->transform.position.y;
  position_z = object->transform.position.z;

  for (curve_index = 0u; curve_index < 9u; ++curve_index) {
    const fzgx_animation_curve *curve = &object->animation_clip.curves[curve_index].curve;
    float *target = 0;
    fzgx_status status;

    if (curve->keyable_count == 0u) {
      continue;
    }
    switch (curve_index) {
      case FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_X:
        target = &scale_x;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Y:
        target = &scale_y;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_SCALE_Z:
        target = &scale_z;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_ROTATION_X:
        target = &rotation_x_degrees;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_ROTATION_Y:
        target = &rotation_y_degrees;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_ROTATION_Z:
        target = &rotation_z_degrees;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_X:
        target = &position_x;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Y:
        target = &position_y;
        break;
      case FZGX_TRACK_SEGMENT_TRS_CURVE_POSITION_Z:
        target = &position_z;
        break;
    }
    if (target == 0) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    status = fzgx_evaluate_float_animation_curve(curve, clip_time_seconds, target);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  transform_out->position.x = position_x;
  transform_out->position.y = position_y;
  transform_out->position.z = position_z;
  transform_out->rotation_x_angle16 = fzgx_degrees_to_angle16_exact(rotation_x_degrees);
  transform_out->rotation_y_angle16 = fzgx_degrees_to_angle16_exact(rotation_y_degrees);
  transform_out->rotation_z_angle16 = fzgx_degrees_to_angle16_exact(rotation_z_degrees);
  transform_out->scale.x = scale_x;
  transform_out->scale.y = scale_y;
  transform_out->scale.z = scale_z;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_segment_sample_trs(
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    fzgx_track_segment_trs_sample *sample_out) {
  fzgx_status status;
  if ((track_segment == 0) || (sample_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 0u, track_segment->fallback_scale.x, time,
      &sample_out->scale.x);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 1u, track_segment->fallback_scale.y, time,
      &sample_out->scale.y);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 2u, track_segment->fallback_scale.z, time,
      &sample_out->scale.z);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 3u, track_segment->fallback_rotation.x, time,
      &sample_out->rotation.x);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 4u, track_segment->fallback_rotation.y, time,
      &sample_out->rotation.y);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 5u, track_segment->fallback_rotation.z, time,
      &sample_out->rotation.z);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 6u, track_segment->fallback_position.x, time,
      &sample_out->position.x);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 7u, track_segment->fallback_position.y, time,
      &sample_out->position.y);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_segment_sample_curve_or_fallback(
      track_segment, animation_segment, 8u, track_segment->fallback_position.z, time,
      &sample_out->position.z);
}

fzgx_status fzgx_track_segment_apply_trs(
    const fzgx_track_segment_record *track_segment,
    const fzgx_track_segment_animation_record *animation_segment,
    float time,
    fzgx_mat43 *transform_inout,
    fzgx_vec3 *scale_inout,
    fzgx_track_segment_trs_curve_cache_exact *curve_cache_inout) {
  const fzgx_animation_curve *curve = 0;
  int32_t *last_segment_index = 0;
  uint32_t curve_slot;
  uint32_t curve_count = 0u;
  float scale_x;
  float scale_y;
  float scale_z;
  float rotation_x;
  float rotation_y;
  float rotation_z;
  float position_x;
  float position_y;
  float position_z;
  uint16_t angle16;
  fzgx_status status;

  if ((track_segment == 0) || (transform_inout == 0) || (scale_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((animation_segment != 0) && (animation_segment->animation_curve_trs != 0)) {
    curve_count = animation_segment->animation_curve_trs->curve_count;
    curve = animation_segment->animation_curve_trs->curves;
    if ((curve_count != 0u) && (curve == 0)) {
      return FZGX_STATUS_BAD_ARGUMENT;
    }
  }
  if (curve_cache_inout != 0) {
    curve_slot = curve_cache_inout->cursor;
    last_segment_index = curve_cache_inout->last_segment_index[curve_slot];
    if (curve_cache_inout->track_segment[curve_slot] != track_segment) {
      memset(last_segment_index, 0, sizeof(curve_cache_inout->last_segment_index[curve_slot]));
    }
  }

  if ((curve == 0) || (curve_count <= 6u) || (curve[6].keyables == 0) ||
      (curve[6].keyable_count == 0u)) {
    position_x = track_segment->fallback_position.x;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[6], time, &position_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[6], time, &last_segment_index[0], &position_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((curve == 0) || (curve_count <= 7u) || (curve[7].keyables == 0) ||
      (curve[7].keyable_count == 0u)) {
    position_y = track_segment->fallback_position.y;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[7], time, &position_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[7], time, &last_segment_index[1], &position_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((curve == 0) || (curve_count <= 8u) || (curve[8].keyables == 0) ||
      (curve[8].keyable_count == 0u)) {
    position_z = track_segment->fallback_position.z;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[8], time, &position_z);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[8], time, &last_segment_index[2], &position_z);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((position_x != 0.0f) || (position_y != 0.0f) || (position_z != 0.0f)) {
    fzgx_mat43_translate_right(
        transform_inout,
        scale_inout->x * position_x,
        scale_inout->y * position_y,
        scale_inout->z * position_z);
  }

  if ((curve == 0) || (curve_count <= 3u) || (curve[3].keyables == 0) ||
      (curve[3].keyable_count == 0u)) {
    rotation_x = track_segment->fallback_rotation.x;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[3], time, &rotation_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[3], time, &last_segment_index[3], &rotation_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((curve == 0) || (curve_count <= 4u) || (curve[4].keyables == 0) ||
      (curve[4].keyable_count == 0u)) {
    rotation_y = track_segment->fallback_rotation.y;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[4], time, &rotation_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[4], time, &last_segment_index[4], &rotation_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  if ((curve == 0) || (curve_count <= 5u) || (curve[5].keyables == 0) ||
      (curve[5].keyable_count == 0u)) {
    rotation_z = track_segment->fallback_rotation.z;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[5], time, &rotation_z);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[5], time, &last_segment_index[5], &rotation_z);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }

  angle16 = (uint16_t)(int32_t)((65536.0f / 360.0f) * rotation_z);
  if ((rotation_z != 0.0f) && (angle16 != 0u)) {
    fzgx_mat43_rotate_about_z_right(transform_inout, angle16);
  }
  angle16 = (uint16_t)(int32_t)((65536.0f / 360.0f) * rotation_y);
  if ((rotation_y != 0.0f) && (angle16 != 0u)) {
    fzgx_mat43_rotate_about_y_right(transform_inout, angle16);
  }
  angle16 = (uint16_t)(int32_t)((65536.0f / 360.0f) * rotation_x);
  if ((rotation_x != 0.0f) && (angle16 != 0u)) {
    fzgx_mat43_rotate_about_x_right(transform_inout, angle16);
  }

  if ((curve == 0) || (curve_count <= 0u) || (curve[0].keyables == 0) ||
      (curve[0].keyable_count == 0u)) {
    scale_x = track_segment->fallback_scale.x;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[0], time, &scale_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[0], time, &last_segment_index[6], &scale_x);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  scale_inout->x *= scale_x;
  if ((curve == 0) || (curve_count <= 1u) || (curve[1].keyables == 0) ||
      (curve[1].keyable_count == 0u)) {
    scale_y = track_segment->fallback_scale.y;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[1], time, &scale_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[1], time, &last_segment_index[7], &scale_y);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  scale_inout->y *= scale_y;
  if ((curve == 0) || (curve_count <= 2u) || (curve[2].keyables == 0) ||
      (curve[2].keyable_count == 0u)) {
    scale_z = track_segment->fallback_scale.z;
  } else if (curve_cache_inout == 0) {
    status = fzgx_evaluate_float_animation_curve(&curve[2], time, &scale_z);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  } else {
    status = fzgx_evaluate_float_animation_curve_cached(&curve[2], time, &last_segment_index[8], &scale_z);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  scale_inout->z *= scale_z;

  if (curve_cache_inout != 0) {
    curve_cache_inout->track_segment[curve_slot] = track_segment;
    if (curve_cache_inout->cursor < 9u) {
      curve_cache_inout->cursor += 1u;
    }
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_get_root_segment_for_track_node(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    const fzgx_track_segment_record **track_segment_out) {
  const fzgx_track_node_record *track_node = 0;
  fzgx_status status;
  if ((course == 0) || (track_segment_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_course_get_track_node(course, track_node_index, &track_node);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_course_find_track_segment_by_address(
      course, track_node->root_segment_address, track_segment_out);
}

fzgx_status fzgx_track_course_get_track_segment_children(
    const fzgx_track_course_content *course,
    const fzgx_track_segment_record *parent_segment,
    const fzgx_track_segment_record **children_out,
    uint32_t *children_count_out) {
  const fzgx_track_segment_record *first_child = 0;
  fzgx_status status;
  if ((course == 0) || (parent_segment == 0) || (children_out == 0) || (children_count_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (parent_segment->children_count == 0u) {
    *children_out = 0;
    *children_count_out = 0u;
    return FZGX_STATUS_OK;
  }
  status = fzgx_track_course_find_track_segment_by_address(
      course, parent_segment->children_address, &first_child);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((uint32_t)(first_child - course->track_segments) + parent_segment->children_count >
      course->track_segment_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  *children_out = first_child;
  *children_count_out = parent_segment->children_count;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_segment_build_source_piece_word(
    const fzgx_track_segment_record *track_segment,
    uint32_t *source_piece_word_out) {
  if ((track_segment == 0) || (source_piece_word_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *source_piece_word_out =
      ((uint32_t)track_segment->segment_type << 24) |
      ((uint32_t)track_segment->embedded_property_type << 16) |
      ((uint32_t)track_segment->perimeter_flags << 8) |
      (uint32_t)track_segment->pipe_cylinder_flags;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_accumulate_track_segment_flags_recursive(
    const fzgx_track_course_content *course,
    const fzgx_track_segment_record *root_segment,
    uint32_t *flags_inout) {
  const fzgx_track_segment_record *children = 0;
  uint32_t source_piece_word;
  uint32_t child_count = 0u;
  uint32_t child_index;
  fzgx_status status;
  if ((course == 0) || (root_segment == 0) || (flags_inout == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_segment_build_source_piece_word(root_segment, &source_piece_word);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  *flags_inout |= source_piece_word;
  status = fzgx_track_course_get_track_segment_children(
      course, root_segment, &children, &child_count);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  for (child_index = 0u; child_index < child_count; ++child_index) {
    status = fzgx_track_course_accumulate_track_segment_flags_recursive(
        course, &children[child_index], flags_inout);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_can_traverse_checkpoint_interval(
    const fzgx_track_course_content *course,
    double minimum_gap_distance,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out) {
  int32_t start_index;
  int32_t end_index;
  int32_t interval_length;
  int32_t traversed_count = 0;
  const fzgx_checkpoint_record *start_checkpoint = 0;
  const fzgx_checkpoint_record *end_checkpoint = 0;
  const fzgx_checkpoint_record *checkpoint = 0;
  fzgx_status status;

  if ((course == 0) || (can_traverse_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *can_traverse_out = 1u;
  if (course->track_node_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((checkpoint_a < 0) || ((uint32_t)checkpoint_a >= course->track_node_count) ||
      (checkpoint_b < 0) || ((uint32_t)checkpoint_b >= course->track_node_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (checkpoint_a == checkpoint_b) {
    return FZGX_STATUS_OK;
  }

  start_index = checkpoint_a;
  end_index = checkpoint_b;
  if (checkpoint_b < checkpoint_a) {
    start_index = checkpoint_b;
    end_index = checkpoint_a;
  }
  interval_length = end_index - start_index;
  if ((int32_t)course->track_node_count - interval_length < interval_length) {
    int32_t previous_start_index = start_index;
    start_index = end_index;
    end_index = previous_start_index;
    interval_length = (int32_t)course->track_node_count - interval_length;
  }

  if (0.0 < minimum_gap_distance) {
    double gap_distance;

    status = fzgx_track_course_get_checkpoint_variant(
        course, (uint32_t)start_index, 0u, &start_checkpoint);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_get_checkpoint_variant(
        course, (uint32_t)end_index, 0u, &end_checkpoint);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    gap_distance = (double)end_checkpoint->start_distance - (double)start_checkpoint->end_distance;
    if (gap_distance < 0.0) {
      gap_distance += (double)course->track_total_distance;
    }
    if (minimum_gap_distance < gap_distance) {
      *can_traverse_out = 0u;
      return FZGX_STATUS_OK;
    }
  }

  for (int32_t step_index = 0; step_index < interval_length; ++step_index) {
    status = fzgx_track_course_get_checkpoint_variant(
        course, (uint32_t)start_index, 0u, &checkpoint);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if (checkpoint->connect_to_track_out == 0u) {
      break;
    }
    traversed_count += 1;
    start_index += 1;
    if ((uint32_t)start_index >= course->track_node_count) {
      start_index = 0;
    }
  }

  if ((interval_length != 0) && (traversed_count < interval_length)) {
    *can_traverse_out = 0u;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_can_traverse_checkpoint_interval_exact(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out) {
  return fzgx_track_course_can_traverse_checkpoint_interval(
      course, 0.0, checkpoint_a, checkpoint_b, can_traverse_out);
}

fzgx_status fzgx_track_course_can_traverse_checkpoint_variant_count_order(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t require_nonincreasing_order,
    uint32_t *can_traverse_out) {
  int32_t start_index;
  int32_t end_index;
  int32_t interval_length;
  int32_t traversed_count = 0;
  const fzgx_track_node_record *current_node;

  if ((course == 0) || (can_traverse_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *can_traverse_out = 1u;
  if (course->track_node_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((checkpoint_a < 0) || ((uint32_t)checkpoint_a >= course->track_node_count) ||
      (checkpoint_b < 0) || ((uint32_t)checkpoint_b >= course->track_node_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (checkpoint_a == checkpoint_b) {
    return FZGX_STATUS_OK;
  }

  start_index = checkpoint_a;
  end_index = checkpoint_b;
  if (checkpoint_b < checkpoint_a) {
    start_index = checkpoint_b;
    end_index = checkpoint_a;
  }
  interval_length = end_index - start_index;
  if ((int32_t)course->track_node_count - interval_length < interval_length) {
    start_index = end_index;
    interval_length = (int32_t)course->track_node_count - interval_length;
  }

  current_node = &course->track_nodes[(uint32_t)start_index];
  for (int32_t step_index = 0; step_index < interval_length; ++step_index) {
    const fzgx_track_node_record *next_node;

    start_index += 1;
    if ((uint32_t)start_index >= course->track_node_count) {
      start_index = 0;
    }
    next_node = &course->track_nodes[(uint32_t)start_index];
    if (require_nonincreasing_order == 0u) {
      if (next_node->checkpoint_count < current_node->checkpoint_count) {
        break;
      }
    } else if (current_node->checkpoint_count < next_node->checkpoint_count) {
      break;
    }
    traversed_count += 1;
    current_node = next_node;
  }

  if ((interval_length != 0) && (traversed_count < interval_length)) {
    *can_traverse_out = 0u;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_can_traverse_nonincreasing_checkpoint_variant_count(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out) {
  return fzgx_track_course_can_traverse_checkpoint_variant_count_order(
      course, checkpoint_a, checkpoint_b, 1u, can_traverse_out);
}

fzgx_status fzgx_track_course_can_traverse_nondecreasing_checkpoint_variant_count(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out) {
  return fzgx_track_course_can_traverse_checkpoint_variant_count_order(
      course, checkpoint_a, checkpoint_b, 0u, can_traverse_out);
}

fzgx_status fzgx_track_course_find_checkpoint_for_track_distance(
    const fzgx_track_course_content *course,
    double track_distance,
    int32_t seed_track_node_index,
    int32_t *checkpoint_index_out,
    float *checkpoint_fraction_out) {
  uint32_t checkpoint_count;
  int32_t offset_pair[2];
  uint32_t remaining_count;
  uint32_t use_negative_offset;

  if ((course == 0) || (checkpoint_index_out == 0) || (checkpoint_fraction_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *checkpoint_index_out = 0;
  *checkpoint_fraction_out = 0.0f;
  if ((course->track_nodes == 0) || (course->checkpoints == 0)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  checkpoint_count = course->track_node_count;
  if (checkpoint_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((seed_track_node_index < 0) || ((uint32_t)seed_track_node_index >= checkpoint_count)) {
    seed_track_node_index = 0;
  }
  if (((uint32_t)(float)track_distance & 0x7f800000u) == 0x7f800000u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  offset_pair[0] = 0;
  offset_pair[1] = -1;
  while (track_distance < 0.0) {
    track_distance = (double)(float)(track_distance + (double)course->track_total_distance);
  }
  while ((double)course->track_total_distance <= track_distance) {
    track_distance = (double)(float)(track_distance - (double)course->track_total_distance);
  }
  use_negative_offset = 0u;
  remaining_count = checkpoint_count;
  if (0 < (int32_t)checkpoint_count) {
    do {
      int32_t candidate_index;
      const fzgx_track_node_record *track_node;
      const fzgx_checkpoint_record *checkpoint;
      double start_distance;
      double end_distance;

      candidate_index = seed_track_node_index + offset_pair[use_negative_offset] + (int32_t)checkpoint_count;
      candidate_index -= (candidate_index / (int32_t)checkpoint_count) * (int32_t)checkpoint_count;
      if (use_negative_offset != 0u) {
        offset_pair[1] = offset_pair[1] - 1;
      } else {
        offset_pair[0] = offset_pair[0] + 1;
      }
      track_node = &course->track_nodes[(uint32_t)candidate_index];
      if (track_node->checkpoint_offset >= course->checkpoint_record_count) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      checkpoint = &course->checkpoints[track_node->checkpoint_offset];
      start_distance = (double)checkpoint->start_distance;
      end_distance = (double)checkpoint->end_distance;
      if ((start_distance <= track_distance) && (track_distance <= end_distance)) {
        *checkpoint_fraction_out =
            (float)((track_distance - start_distance) / (double)(float)(end_distance - start_distance));
        *checkpoint_index_out = candidate_index;
        return FZGX_STATUS_OK;
      }
      use_negative_offset ^= 1u;
      remaining_count -= 1u;
    } while (remaining_count != 0u);
  }

  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_track_course_can_traverse_checkpoint_interval_without_pipe_open_transition(
    const fzgx_track_course_content *course,
    int32_t checkpoint_a,
    int32_t checkpoint_b,
    uint32_t *can_traverse_out) {
  int32_t start_index;
  int32_t end_index;
  int32_t interval_length;
  int32_t step_index;
  const fzgx_track_segment_record *segment_a = 0;
  const fzgx_track_segment_record *segment_b = 0;
  fzgx_status status;
  if ((course == 0) || (can_traverse_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *can_traverse_out = 1u;
  if (course->track_node_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((checkpoint_a < 0) || ((uint32_t)checkpoint_a >= course->track_node_count) ||
      (checkpoint_b < 0) || ((uint32_t)checkpoint_b >= course->track_node_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (checkpoint_a == checkpoint_b) {
    return FZGX_STATUS_OK;
  }
  start_index = checkpoint_a;
  end_index = checkpoint_b;
  if (checkpoint_b < checkpoint_a) {
    start_index = checkpoint_b;
    end_index = checkpoint_a;
  }
  interval_length = end_index - start_index;
  if ((int32_t)course->track_node_count - interval_length < interval_length) {
    start_index = end_index;
    interval_length = (int32_t)course->track_node_count - interval_length;
  }
  status = fzgx_track_course_get_root_segment_for_track_node(
      course, (uint32_t)start_index, &segment_a);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  for (step_index = 0; step_index < interval_length; ++step_index) {
    uint32_t flags_a = 0u;
    uint32_t flags_b = 0u;
    start_index += 1;
    if ((uint32_t)start_index >= course->track_node_count) {
      start_index = 0;
    }
    status = fzgx_track_course_get_root_segment_for_track_node(
        course, (uint32_t)start_index, &segment_b);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_accumulate_track_segment_flags_recursive(course, segment_a, &flags_a);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    status = fzgx_track_course_accumulate_track_segment_flags_recursive(course, segment_b, &flags_b);
    if (status != FZGX_STATUS_OK) {
      return status;
    }
    if ((((flags_a & 0x01000000u) != 0u) && ((flags_b & 0x00800000u) != 0u)) ||
        (((flags_b & 0x01000000u) != 0u) && ((flags_a & 0x00800000u) != 0u))) {
      *can_traverse_out = 0u;
      return FZGX_STATUS_OK;
    }
    segment_a = segment_b;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_get_checkpoint_variant(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    uint32_t variant_index,
    const fzgx_checkpoint_record **checkpoint_out) {
  const fzgx_track_node_record *track_node = 0;
  uint32_t checkpoint_index;
  if ((course == 0) || (checkpoint_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_status status = fzgx_track_course_get_track_node(course, track_node_index, &track_node);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((course->checkpoint_record_count != 0u) && (course->checkpoints == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (variant_index >= track_node->checkpoint_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  checkpoint_index = track_node->checkpoint_offset + variant_index;
  if (checkpoint_index >= course->checkpoint_record_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  *checkpoint_out = &course->checkpoints[checkpoint_index];
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_compute_checkpoint_t_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    const fzgx_vec3 *point,
    int32_t track_node_index,
    uint32_t variant_index,
    float *t_out) {
  float fVar1;
  float fVar2;
  float fVar3;
  float dVar5;
  const fzgx_track_node_record *track_node;
  const fzgx_checkpoint_record *checkpoint;
  if ((course == 0) || (point == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((course->track_nodes == 0) || (course->checkpoints == 0) || (course->track_node_count == 0u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  fVar1 = 0.001f;
  if ((uint16_t)authored_track_id == 0x19u) {
    fVar1 = 1.7f;
  }
  if ((track_node_index < 0) || ((uint32_t)track_node_index >= course->track_node_count)) {
    track_node_index = 0;
  }
  track_node = &course->track_nodes[(uint32_t)track_node_index];
  if (variant_index >= track_node->checkpoint_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((track_node->checkpoint_offset + variant_index) >= course->checkpoint_record_count) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  checkpoint = &course->checkpoints[track_node->checkpoint_offset + variant_index];
  if (track_node_index == 0) {
    dVar5 = point->z;
    if (0.0f <= dVar5) {
      *t_out = 0.0f;
      return FZGX_STATUS_OK;
    }
    fVar2 =
        checkpoint->plane_end.distance +
        (float)(dVar5 * (double)checkpoint->plane_end.normal.z +
                (double)(point->y * checkpoint->plane_end.normal.y +
                         point->x * checkpoint->plane_end.normal.x));
    if (fVar2 < -fVar1) {
      *t_out = 1.0f;
      return FZGX_STATUS_OK;
    }
    fVar3 =
        checkpoint->plane_start.distance +
        (float)(dVar5 * (double)checkpoint->plane_start.normal.z +
                (double)(point->y * checkpoint->plane_start.normal.y +
                         point->x * checkpoint->plane_start.normal.x));
  } else if ((uint32_t)track_node_index == (course->track_node_count - 1u)) {
    dVar5 = point->z;
    if (dVar5 < 0.0f) {
      *t_out = 1.0f;
      return FZGX_STATUS_OK;
    }
    fVar3 =
        checkpoint->plane_start.distance +
        (float)(dVar5 * (double)checkpoint->plane_start.normal.z +
                (double)(point->y * checkpoint->plane_start.normal.y +
                         point->x * checkpoint->plane_start.normal.x));
    if (fVar3 < -fVar1) {
      *t_out = 0.0f;
      return FZGX_STATUS_OK;
    }
    fVar2 =
        checkpoint->plane_end.distance +
        (float)(dVar5 * (double)checkpoint->plane_end.normal.z +
                (double)(point->y * checkpoint->plane_end.normal.y +
                         point->x * checkpoint->plane_end.normal.x));
  } else {
    fVar2 = checkpoint->plane_end.distance + point->z * checkpoint->plane_end.normal.z +
            point->y * checkpoint->plane_end.normal.y + point->x * checkpoint->plane_end.normal.x;
    if (fVar2 < -fVar1) {
      *t_out = 1.0f;
      return FZGX_STATUS_OK;
    }
    fVar3 = checkpoint->plane_start.distance + point->z * checkpoint->plane_start.normal.z +
            point->y * checkpoint->plane_start.normal.y + point->x * checkpoint->plane_start.normal.x;
    if (fVar3 < -fVar1) {
      *t_out = 0.0f;
      return FZGX_STATUS_OK;
    }
  }
  *t_out = fVar3 / (fVar3 + fVar2);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_compute_curve_time_for_checkpoint_fraction(
    const fzgx_track_course_content *course,
    uint32_t track_node_index,
    float checkpoint_fraction,
    float *curve_time_out) {
  const fzgx_checkpoint_record *checkpoint = 0;
  fzgx_status status;
  if ((course == 0) || (curve_time_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_course_get_checkpoint_variant(course, track_node_index, 0u, &checkpoint);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  *curve_time_out = checkpoint->curve_time_start +
                    checkpoint_fraction * (checkpoint->curve_time_end - checkpoint->curve_time_start);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_checkpoint_variant_contains_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t track_node_index,
    uint32_t variant_index,
    int32_t *checkpoint_index_out) {
  return fzgx_checkpoint_variant_contains_point_internal(
      course,
      authored_track_id,
      circuit_type,
      point,
      track_node_index,
      variant_index,
      checkpoint_index_out);
}

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
    fzgx_vec3 *point_on_track_out) {
  float epsilon;
  uint32_t checkpoint_count;
  uint32_t start_ok_history[2] = {1u, 1u};
  uint32_t end_ok_history[2] = {1u, 1u};
  int32_t direction_offset[2] = {0, -1};
  uint32_t blocked[2] = {0u, 0u};
  uint32_t scan_count;
  uint8_t is_open_circuit;
  uint32_t seed_track_node_checkpoint_count;
  uint32_t seed_track_node_root_segment_address;
  if ((course == 0) || (point == 0) || (checkpoint_index_out == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *checkpoint_index_out = -1;
  *t_out = 0.0f;
  if (point_on_track_out != 0) {
    *point_on_track_out = *point;
  }
  checkpoint_count = course->track_node_count;
  if ((course->track_nodes == 0) || (course->checkpoints == 0) || (checkpoint_count == 0u)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if ((seed_track_node_index < 0) || ((uint32_t)seed_track_node_index >= checkpoint_count)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  epsilon = fzgx_checkpoint_query_epsilon(authored_track_id);
  is_open_circuit = (uint8_t)(circuit_type != FZGX_CIRCUIT_TYPE_CLOSED);
  seed_track_node_checkpoint_count = course->track_nodes[(uint32_t)seed_track_node_index].checkpoint_count;
  seed_track_node_root_segment_address = course->track_nodes[(uint32_t)seed_track_node_index].root_segment_address;

  for (scan_count = 0u; (int32_t)scan_count < (int32_t)checkpoint_count; scan_count += 1u) {
    uint32_t parity;
    int32_t candidate_index;
    const fzgx_track_node_record *candidate_track_node;
    const fzgx_checkpoint_record *checkpoint;
    float start_plane_distance;
    float end_plane_distance;
    uint32_t start_ok;
    uint32_t end_ok;

    if (is_open_circuit == 0u) {
      parity = scan_count & 1u;
    } else {
      parity = 0u;
    }
    candidate_index = seed_track_node_index + direction_offset[parity] + (int32_t)checkpoint_count;
    candidate_index -= (candidate_index / (int32_t)checkpoint_count) * (int32_t)checkpoint_count;
    candidate_track_node = &course->track_nodes[(uint32_t)candidate_index];
    if (require_matching_branch_corridor != 0u) {
      if ((seed_track_node_root_segment_address != candidate_track_node->root_segment_address) ||
          (seed_track_node_checkpoint_count != candidate_track_node->checkpoint_count)) {
        blocked[parity] = 1u;
      }
    }
    if (parity == 0u) {
      direction_offset[0] += 1;
    } else {
      direction_offset[1] -= 1;
    }
    if (blocked[parity] != 0u) {
      continue;
    }
    if (variant_index >= candidate_track_node->checkpoint_count) {
      blocked[parity] = 1u;
      continue;
    }
    if ((candidate_track_node->checkpoint_offset + variant_index) >= course->checkpoint_record_count) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    checkpoint = &course->checkpoints[candidate_track_node->checkpoint_offset + variant_index];

    start_plane_distance = fzgx_plane_eval_point(&checkpoint->plane_start, point);
    end_plane_distance = fzgx_plane_eval_point(&checkpoint->plane_end, point);

    if (candidate_index == 0) {
      start_ok = (uint32_t)(end_plane_distance >= -epsilon);
      end_ok = (uint32_t)(point->z < 0.0f);
      if (circuit_type == FZGX_CIRCUIT_TYPE_OPEN) {
        end_ok = 1u;
      }
    } else if ((uint32_t)candidate_index == (checkpoint_count - 1u)) {
      start_ok = (uint32_t)(point->z >= 0.0f);
      end_ok = (uint32_t)(start_plane_distance >= -epsilon);
      if ((circuit_type == FZGX_CIRCUIT_TYPE_OPEN) && (authored_track_id != 0x29u)) {
        start_ok = 1u;
      }
    } else {
      start_ok = (uint32_t)(end_plane_distance >= -epsilon);
      end_ok = (uint32_t)(start_plane_distance >= -epsilon);
    }

    if ((start_ok != 0u) && (end_ok != 0u)) {
      float t = start_plane_distance / (start_plane_distance + end_plane_distance);

      *checkpoint_index_out = candidate_index;
      *t_out = t;
      if (point_on_track_out != 0) {
        point_on_track_out->x =
            (checkpoint->plane_end.origin.x - checkpoint->plane_start.origin.x) * t +
            checkpoint->plane_start.origin.x;
        point_on_track_out->y =
            (checkpoint->plane_end.origin.y - checkpoint->plane_start.origin.y) * t +
            checkpoint->plane_start.origin.y;
        point_on_track_out->z =
            (checkpoint->plane_end.origin.z - checkpoint->plane_start.origin.z) * t +
            checkpoint->plane_start.origin.z;
      }
      return FZGX_STATUS_OK;
    }

    if ((((parity == 0u) && (start_ok_history[0] == 0u)) && (end_ok == 0u)) ||
        (((parity != 0u) && (end_ok_history[parity] == 0u)) && (start_ok == 0u))) {
      *checkpoint_index_out = -1;
      break;
    }
    end_ok_history[parity] = end_ok;
    start_ok_history[parity] = start_ok;
  }

  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_track_course_find_shared_checkpoint_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t *checkpoint_index_out,
    float *t_out) {
  int32_t seed_track_node_index;
  if ((course == 0) || (point == 0) || (checkpoint_index_out == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *checkpoint_index_out = 0;
  *t_out = 0.0f;
  if (course->track_node_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  if (circuit_type == FZGX_CIRCUIT_TYPE_OPEN) {
    seed_track_node_index = 0;
  } else {
    seed_track_node_index = (int32_t)course->track_node_count - 1;
  }
  while (seed_track_node_index >= 0) {
    if (fzgx_track_course_scan_checkpoint_neighbors_for_point(
        course,
        authored_track_id,
        circuit_type,
        point,
        seed_track_node_index,
        0u,
        0u,
        checkpoint_index_out,
        t_out,
        0) == FZGX_STATUS_OK) {
      return FZGX_STATUS_OK;
    }
    seed_track_node_index += -1;
  }
  *checkpoint_index_out = 0;
  *t_out = 0.0f;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_find_nearest_checkpoint_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t *checkpoint_index_out,
    float *t_out,
    uint32_t *variant_index_out) {
  float epsilon;
  double best_distance;
  uint8_t is_open_circuit;
  int32_t best_index;
  uint32_t checkpoint_count;
  int32_t track_id;
  int32_t checkpoint_index;
  uint32_t skip_gate;
  uint32_t best_variant;

  if ((course == 0) || (point == 0) || (checkpoint_index_out == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *checkpoint_index_out = -1;
  *t_out = 0.0f;
  if (variant_index_out != 0) {
    *variant_index_out = 0u;
  }
  epsilon = fzgx_checkpoint_query_epsilon(authored_track_id);
  best_distance = 0.0;
  if ((course->track_nodes == 0) || (course->checkpoints == 0)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  checkpoint_count = course->track_node_count;
  if (checkpoint_count == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  is_open_circuit = (uint8_t)(circuit_type != FZGX_CIRCUIT_TYPE_CLOSED);
  best_index = -1;
  best_variant = 0u;
  track_id = (int32_t)(int16_t)authored_track_id;
  checkpoint_index = 0;
  skip_gate = fzgx_count_leading_zeros_u32_exact((uint32_t)(0x1d - track_id));
  while ((uint32_t)checkpoint_index < checkpoint_count) {
    int32_t candidate_index = checkpoint_index;

    checkpoint_index += 1;
    if ((skip_gate >> 5 == 0u) ||
        ((((candidate_index < 0x6b) || (0xde < candidate_index)) &&
          (((candidate_index < 0xf2) || (0x131 < candidate_index)) &&
           (((candidate_index < 0x13) || (0x24 < candidate_index)) &&
            (candidate_index != 0x25) && (candidate_index != 0xf1)))))) {
      const fzgx_track_node_record *track_node = &course->track_nodes[(uint32_t)candidate_index];
      int32_t variant_index;
      int32_t last_variant_index;
      uint32_t checkpoint_offset = track_node->checkpoint_offset;

      if (track_node->checkpoint_count < 2u) {
        variant_index = 0;
        last_variant_index = 0;
      } else {
        variant_index = 1;
        last_variant_index = (int32_t)track_node->checkpoint_count - 1;
      }
      for (; variant_index <= last_variant_index; variant_index += 1) {
        const fzgx_checkpoint_record *checkpoint;
        float point_z;
        double start_plane_distance;
        double end_plane_distance;
        uint32_t start_ok;
        uint32_t end_ok;

        if ((checkpoint_offset + (uint32_t)variant_index) >= course->checkpoint_record_count) {
          return FZGX_STATUS_OUT_OF_RANGE;
        }
        checkpoint = &course->checkpoints[checkpoint_offset + (uint32_t)variant_index];
        point_z = point->z;
        start_plane_distance =
            (double)(checkpoint->plane_start.distance +
                     point_z * checkpoint->plane_start.normal.z +
                     point->y * checkpoint->plane_start.normal.y +
                     point->x * checkpoint->plane_start.normal.x);
        end_plane_distance =
            (double)(checkpoint->plane_end.distance +
                     point_z * checkpoint->plane_end.normal.z +
                     point->y * checkpoint->plane_end.normal.y +
                     point->x * checkpoint->plane_end.normal.x);
        if (candidate_index == 0) {
          start_ok = (uint32_t)(-(double)epsilon <= end_plane_distance);
          end_ok = (uint32_t)(point_z < 0.0f);
          if (is_open_circuit != 0u) {
            end_ok = 1u;
          }
        } else if ((uint32_t)candidate_index == (checkpoint_count - 1u)) {
          start_ok = (uint32_t)(0.0f <= point_z);
          end_ok = (uint32_t)(-(double)epsilon <= start_plane_distance);
          if ((is_open_circuit != 0u) && (authored_track_id != 0x29u)) {
            start_ok = 1u;
          }
        } else {
          start_ok = (uint32_t)(-(double)epsilon <= end_plane_distance);
          end_ok = (uint32_t)(-(double)epsilon <= start_plane_distance);
        }
        if ((end_ok != 0u) && (start_ok != 0u)) {
          float segment_x = checkpoint->plane_end.origin.x - checkpoint->plane_start.origin.x;
          float segment_y = checkpoint->plane_end.origin.y - checkpoint->plane_start.origin.y;
          float start_x = checkpoint->plane_start.origin.x - point->x;
          float start_y = checkpoint->plane_start.origin.y - point->y;
          float segment_z = checkpoint->plane_end.origin.z - checkpoint->plane_start.origin.z;
          float start_z = checkpoint->plane_start.origin.z - point->z;
          float segment_t =
              -(segment_z * start_z + segment_y * start_y + segment_x * start_x) /
              (segment_z * segment_z + segment_y * segment_y + segment_x * segment_x);
          double distance;

          if (((uint32_t)segment_t & 0x7f800000u) == 0x7f800000u) {
            distance = (double)sqrtf(start_z * start_z + start_y * start_y + start_x * start_x);
          } else if ((segment_t < 0.0f) || (1.0f < segment_t)) {
            float end_x = checkpoint->plane_end.origin.x - point->x;
            float end_y = checkpoint->plane_end.origin.y - point->y;
            float end_z = checkpoint->plane_end.origin.z - point->z;
            float start_distance_sq = start_z * start_z + start_y * start_y + start_x * start_x;
            float end_distance_sq = end_z * end_z + end_y * end_y + end_x * end_x;

            if (end_distance_sq <= start_distance_sq) {
              distance = (double)sqrtf(end_distance_sq);
            } else {
              distance = (double)sqrtf(start_distance_sq);
            }
          } else {
            float nearest_delta_x = segment_x * segment_t;
            float nearest_delta_y = segment_y * segment_t;
            float nearest_delta_z = segment_z * segment_t;

            distance = (double)sqrtf(
                (start_z + nearest_delta_z) * (start_z + nearest_delta_z) +
                (start_y + nearest_delta_y) * (start_y + nearest_delta_y) +
                (start_x + nearest_delta_x) * (start_x + nearest_delta_x));
          }
          if (track_id == 8) {
            distance = (double)(float)(distance - (double)(0.5f * checkpoint->track_width));
          }
          if ((best_index < 0) || (distance < best_distance)) {
            *t_out = (float)(start_plane_distance / (double)(float)(start_plane_distance + end_plane_distance));
            best_index = candidate_index;
            best_variant = (uint32_t)variant_index;
            best_distance = distance;
          }
        }
      }
    }
  }
  if (-1 < best_index) {
    *checkpoint_index_out = best_index;
    if (variant_index_out != 0) {
      *variant_index_out = best_variant;
    }
    return FZGX_STATUS_OK;
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t *variant_index_inout,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out) {
  uint8_t cVar1;
  float fVar2;
  int32_t iVar3;
  int32_t iVar4;
  int bVar5;
  int32_t iVar6;
  int32_t iVar7;
  int32_t iVar8;
  uint32_t uVar9;
  int32_t iVar10;
  uint32_t uVar11;
  int32_t iVar12;
  int bVar13;
  float dVar14;
  float dVar15;
  float dVar16;
  const fzgx_track_node_record *track_node;
  const fzgx_checkpoint_record *checkpoint;

  if ((course == 0) || (point == 0) || (variant_index_inout == 0) ||
      (checkpoint_index_out == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *checkpoint_index_out = -1;
  *t_out = 0.0f;
  if ((course->track_nodes == 0) || (course->checkpoints == 0)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  uVar11 = course->track_node_count;
  if (uVar11 == 0u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  fVar2 = 0.001f;
  if ((uint16_t)authored_track_id == 0x19u) {
    fVar2 = 1.7f;
  }
  if ((seed_track_node_index < 0) || ((uint32_t)seed_track_node_index >= uVar11)) {
    seed_track_node_index = 0;
  }
  iVar10 = (int32_t)(*variant_index_inout);
  cVar1 = (uint8_t)(circuit_type != FZGX_CIRCUIT_TYPE_CLOSED);
  if (iVar10 == 0) {
    iVar10 = 1;
  }
  uVar9 = 10u;
  if ((int32_t)uVar11 < 10) {
    uVar9 = uVar11;
  }
  dVar16 = -fVar2;
  iVar12 = 0;
  iVar8 = (int32_t)course->track_nodes[(uint32_t)seed_track_node_index].checkpoint_count;
  do {
    if ((int32_t)uVar9 <= iVar12) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    iVar7 =
        (seed_track_node_index + (int32_t)uVar11) -
        (((seed_track_node_index + (int32_t)uVar11) / (int32_t)uVar11) * (int32_t)uVar11);
    track_node = &course->track_nodes[(uint32_t)iVar7];
    if ((require_matching_branch_corridor != 0u) &&
        (iVar8 != (int32_t)track_node->checkpoint_count)) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    iVar6 = iVar10;
    if ((iVar10 != 0) && ((int32_t)track_node->checkpoint_count <= iVar10)) {
      iVar6 = (int32_t)track_node->checkpoint_count - 1;
    }
    if ((iVar6 < 0) || ((uint32_t)iVar6 >= track_node->checkpoint_count)) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    if ((track_node->checkpoint_offset + (uint32_t)iVar6) >= course->checkpoint_record_count) {
      return FZGX_STATUS_OUT_OF_RANGE;
    }
    checkpoint = &course->checkpoints[track_node->checkpoint_offset + (uint32_t)iVar6];
    dVar15 =
        checkpoint->plane_start.distance + point->z * checkpoint->plane_start.normal.z +
        point->y * checkpoint->plane_start.normal.y + point->x * checkpoint->plane_start.normal.x;
    dVar14 =
        checkpoint->plane_end.distance + point->z * checkpoint->plane_end.normal.z +
        point->y * checkpoint->plane_end.normal.y + point->x * checkpoint->plane_end.normal.x;
    if (iVar7 == 0) {
      bVar5 = dVar16 <= dVar14;
      bVar13 = point->z < 0.0f;
      if (cVar1 != 0) {
        bVar13 = 1;
      }
    } else if ((uint32_t)iVar7 == (uVar11 - 1u)) {
      bVar5 = 0.0f <= point->z;
      bVar13 = dVar16 <= dVar15;
      if ((cVar1 != 0) && ((uint16_t)authored_track_id != 0x29u)) {
        bVar5 = 1;
      }
    } else {
      bVar5 = dVar16 <= dVar14;
      bVar13 = dVar16 <= dVar15;
    }
    if (bVar5) {
      if (bVar13) {
        *variant_index_inout = (uint32_t)iVar6;
        *checkpoint_index_out = iVar7;
        *t_out = dVar15 / (dVar15 + dVar14);
        return FZGX_STATUS_OK;
      }
      if ((require_matching_branch_corridor != 0u) && (checkpoint->connect_to_track_in == 0u)) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      seed_track_node_index += -1;
    } else {
      if ((require_matching_branch_corridor != 0u) && (checkpoint->connect_to_track_out == 0u)) {
        return FZGX_STATUS_OUT_OF_RANGE;
      }
      seed_track_node_index += 1;
    }
    iVar12 += 1;
  } while (1);
}

fzgx_status fzgx_track_course_resolve_branch_checkpoint_from_seed_with_fallback(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t *variant_index_inout,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out) {
  uint32_t resolved_variant_index;
  fzgx_status status;
  if ((course == 0) || (point == 0) || (variant_index_inout == 0) ||
      (checkpoint_index_out == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  (void)require_matching_branch_corridor;
  resolved_variant_index = 0u;
  status = fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
      course,
      authored_track_id,
      circuit_type,
      point,
      seed_track_node_index,
      &resolved_variant_index,
      0u,
      checkpoint_index_out,
      t_out);
  if (status == FZGX_STATUS_OK) {
    *variant_index_inout = resolved_variant_index;
    return FZGX_STATUS_OK;
  }
  resolved_variant_index = 0u;
  status = fzgx_track_course_compute_checkpoint_t_for_point(
      course,
      authored_track_id,
      point,
      seed_track_node_index,
      0u,
      t_out);
  if (status != FZGX_STATUS_OK) {
    *checkpoint_index_out = -1;
    *variant_index_inout = 0u;
    return status;
  }
  *variant_index_inout = resolved_variant_index;
  *checkpoint_index_out = seed_track_node_index;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_resolve_branch_checkpoint_from_seed_strict(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t *variant_index_inout,
    uint32_t require_matching_branch_corridor,
    int32_t *checkpoint_index_out,
    float *t_out) {
  uint32_t resolved_variant_index;
  if ((course == 0) || (point == 0) || (variant_index_inout == 0) ||
      (checkpoint_index_out == 0) || (t_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  (void)require_matching_branch_corridor;
  resolved_variant_index = 0u;
  if (fzgx_track_course_resolve_branch_checkpoint_and_t_from_seed(
      course,
      authored_track_id,
      circuit_type,
      point,
      seed_track_node_index,
      &resolved_variant_index,
      0u,
      checkpoint_index_out,
      t_out) != FZGX_STATUS_OK) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  *variant_index_inout = resolved_variant_index;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_build_shared_checkpoint_query_result(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    fzgx_current_checkpoint_query_result *query_out) {
  fzgx_status status;
  if ((course == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_init_current_checkpoint_query_result(query_out);
  status = fzgx_track_course_find_shared_checkpoint_for_point(
      course,
      authored_track_id,
      circuit_type,
      point,
      &query_out->checkpoint_index,
      &query_out->checkpoint_fraction);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)query_out->checkpoint_index,
      0u,
      query_out->checkpoint_fraction,
      &query_out->point_on_track);
}

fzgx_status fzgx_track_course_build_neighbor_checkpoint_query_result(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    uint32_t variant_index,
    uint32_t require_matching_branch_corridor,
    fzgx_current_checkpoint_query_result *query_out) {
  fzgx_status status;
  if ((course == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_init_current_checkpoint_query_result(query_out);
  query_out->variant_index = variant_index;
  status = fzgx_track_course_scan_checkpoint_neighbors_for_point(
      course,
      authored_track_id,
      circuit_type,
      point,
      seed_track_node_index,
      variant_index,
      require_matching_branch_corridor,
      &query_out->checkpoint_index,
      &query_out->checkpoint_fraction,
      &query_out->point_on_track);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_build_seeded_checkpoint_query_result_with_fallback(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    fzgx_current_checkpoint_query_result *query_out) {
  fzgx_status status;
  uint32_t variant_index = 0u;
  if ((course == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_init_current_checkpoint_query_result(query_out);
  status = fzgx_track_course_resolve_branch_checkpoint_from_seed_with_fallback(
      course,
      authored_track_id,
      circuit_type,
      point,
      seed_track_node_index,
      &variant_index,
      0u,
      &query_out->checkpoint_index,
      &query_out->checkpoint_fraction);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  query_out->variant_index = variant_index;
  return fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)query_out->checkpoint_index,
      query_out->variant_index,
      query_out->checkpoint_fraction,
      &query_out->point_on_track);
}

fzgx_status fzgx_track_course_build_seeded_checkpoint_query_result_strict(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t seed_track_node_index,
    fzgx_current_checkpoint_query_result *query_out) {
  fzgx_status status;
  uint32_t variant_index = 0u;
  if ((course == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_init_current_checkpoint_query_result(query_out);
  status = fzgx_track_course_resolve_branch_checkpoint_from_seed_strict(
      course,
      authored_track_id,
      circuit_type,
      point,
      seed_track_node_index,
      &variant_index,
      0u,
      &query_out->checkpoint_index,
      &query_out->checkpoint_fraction);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  query_out->variant_index = variant_index;
  return fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)query_out->checkpoint_index,
      query_out->variant_index,
      query_out->checkpoint_fraction,
      &query_out->point_on_track);
}

fzgx_status fzgx_track_course_build_nearest_checkpoint_query_result(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    fzgx_current_checkpoint_query_result *query_out) {
  fzgx_status status;
  if ((course == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_init_current_checkpoint_query_result(query_out);
  status = fzgx_track_course_find_nearest_checkpoint_for_point(
      course,
      authored_track_id,
      circuit_type,
      point,
      &query_out->checkpoint_index,
      &query_out->checkpoint_fraction,
      &query_out->variant_index);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)query_out->checkpoint_index,
      query_out->variant_index,
      query_out->checkpoint_fraction,
      &query_out->point_on_track);
}

fzgx_status fzgx_track_course_build_active_checkpoint_bank_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction,
    fzgx_active_checkpoint_bank_result *bank_out) {
  const fzgx_track_node_record *track_node = 0;
  fzgx_status status;
  uint32_t variant_slot;
  uint32_t best_variant_slot = 0u;
  float best_distance_squared = 0.0f;
  uint32_t have_best_variant = 0u;

  if ((course == 0) || (point == 0) || (bank_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if (active_checkpoint_index < 0) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  fzgx_init_active_checkpoint_bank_result(
      bank_out, active_checkpoint_index, active_checkpoint_fraction);
  status = fzgx_track_course_get_track_node(course, (uint32_t)active_checkpoint_index, &track_node);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if (track_node->checkpoint_count > 4u) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  bank_out->checkpoint_variant_count = track_node->checkpoint_count;
  status = fzgx_track_course_checkpoint_variant_contains_point(
      course,
      authored_track_id,
      circuit_type,
      point,
      active_checkpoint_index,
      0u,
      &bank_out->containment_checkpoint_index[0]);
  if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_OUT_OF_RANGE)) {
    return status;
  }

  for (variant_slot = 1u; variant_slot < track_node->checkpoint_count; ++variant_slot) {
    int32_t checkpoint_index = active_checkpoint_index;
    float checkpoint_fraction = active_checkpoint_fraction;
    fzgx_vec3 point_on_track = {0};
    status = fzgx_track_course_scan_checkpoint_neighbors_for_point(
        course,
        authored_track_id,
        circuit_type,
        point,
        active_checkpoint_index,
        variant_slot,
        0u,
        &checkpoint_index,
        &checkpoint_fraction,
        &point_on_track);
    if (status == FZGX_STATUS_OK) {
      float distance_squared = fzgx_vec3_distance_squared(point, &point_on_track);
      if ((have_best_variant == 0u) || (distance_squared <= best_distance_squared)) {
        have_best_variant = 1u;
        best_variant_slot = variant_slot;
        best_distance_squared = distance_squared;
      }
      bank_out->checkpoint_index[variant_slot] = checkpoint_index;
      bank_out->checkpoint_fraction[variant_slot] = checkpoint_fraction;
    } else if (status != FZGX_STATUS_OUT_OF_RANGE) {
      return status;
    }
    status = fzgx_track_course_checkpoint_variant_contains_point(
        course,
        authored_track_id,
        circuit_type,
        point,
        bank_out->checkpoint_index[variant_slot],
        variant_slot,
        &bank_out->containment_checkpoint_index[variant_slot]);
    if ((status != FZGX_STATUS_OK) && (status != FZGX_STATUS_OUT_OF_RANGE)) {
      return status;
    }
  }
  bank_out->preferred_variant_slot = best_variant_slot;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_build_current_track_query_result_from_bank_and_frame_buffer(
    const fzgx_current_checkpoint_query_result *checkpoint_result,
    const fzgx_active_checkpoint_bank_result *bank_result,
    const fzgx_track_frame_export_buffer *frame_buffer,
    fzgx_current_track_query_result *query_out) {
  fzgx_status status;
  if ((checkpoint_result == 0) || (bank_result == 0) || (frame_buffer == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_frame_export_buffer_select(frame_buffer, &query_out->frame);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  query_out->checkpoint_index = checkpoint_result->checkpoint_index;
  query_out->checkpoint_fraction = checkpoint_result->checkpoint_fraction;
  query_out->segment_index = bank_result->containment_checkpoint_index[0];
  query_out->checkpoint_variant_count = bank_result->checkpoint_variant_count;
  query_out->cached_frame_count = frame_buffer->cached_frame_count;
  query_out->selected_cached_frame_index = frame_buffer->selected_frame_index;
  for (int i = 0; i < 4; ++i) {
    query_out->active_bank_cp_idx[i] = bank_result->checkpoint_index[i];
    query_out->active_bank_cp_frac[i] = bank_result->checkpoint_fraction[i];
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_build_current_track_query_result_for_point(
    const fzgx_track_course_content *course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction,
    const fzgx_track_frame_record *cached_frames,
    uint32_t cached_frame_count,
    fzgx_current_track_query_result *query_out) {
  fzgx_current_checkpoint_query_result checkpoint_result;
  fzgx_active_checkpoint_bank_result bank_result;
  fzgx_status status;

  if ((course == 0) || (point == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  fzgx_init_current_checkpoint_query_result(&checkpoint_result);
  checkpoint_result.checkpoint_index = active_checkpoint_index;
  checkpoint_result.checkpoint_fraction = active_checkpoint_fraction;
  status = fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)active_checkpoint_index,
      0u,
      active_checkpoint_fraction,
      &checkpoint_result.point_on_track);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_course_build_active_checkpoint_bank_for_point(
      course,
      authored_track_id,
      circuit_type,
      point,
      active_checkpoint_index,
      active_checkpoint_fraction,
      &bank_result);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_build_current_track_query_result_from_bank_and_nearest_frame(
      &checkpoint_result, &bank_result, cached_frames, cached_frame_count, point, query_out);
}

fzgx_status fzgx_build_current_track_query_result_from_bank_and_nearest_frame(
    const fzgx_current_checkpoint_query_result *checkpoint_result,
    const fzgx_active_checkpoint_bank_result *bank_result,
    const fzgx_track_frame_record *cached_frames,
    uint32_t cached_frame_count,
    const fzgx_vec3 *selection_point,
    fzgx_current_track_query_result *query_out) {
  fzgx_track_frame_export_buffer buffer = {0};
  fzgx_status status;

  if ((checkpoint_result == 0) || (bank_result == 0) || (selection_point == 0) ||
      (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_frame_export_buffer_build_nearest_anchor_selection(
      cached_frames, cached_frame_count, selection_point, &buffer);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  return fzgx_build_current_track_query_result_from_bank_and_frame_buffer(
      checkpoint_result, bank_result, &buffer, query_out);
}

fzgx_status fzgx_track_course_build_cached_frames_for_checkpoint(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t track_node_index,
    float checkpoint_fraction,
    fzgx_track_frame_record *cached_frames_out,
    uint32_t cached_frame_capacity,
    uint32_t *cached_frame_count_out) {
  const fzgx_track_segment_record *root_segment = 0;
  fzgx_mat43 identity;
  fzgx_vec3 scale = {1.0f, 1.0f, 1.0f};
  float curve_time;
  uint32_t cached_frame_cursor = 0u;
  fzgx_status status;

  if ((course == 0) || (cached_frames_out == 0) || (cached_frame_count_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((cached_frame_capacity == 0u) ||
      (cached_frame_capacity < FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }

  status = fzgx_track_course_compute_curve_time_for_checkpoint_fraction(
      course, track_node_index, checkpoint_fraction, &curve_time);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_course_get_root_segment_for_track_node(
      course, track_node_index, &root_segment);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  memset(cached_frames_out, 0, sizeof(*cached_frames_out) * cached_frame_capacity);
  identity = fzgx_mat43_identity();

  status = fzgx_populate_track_cached_frames_from_piece_tree_exact(
      course,
      animation_course,
      root_segment,
      curve_time,
      &identity,
      &scale,
      0,
      cached_frames_out,
      cached_frame_capacity,
      &cached_frame_cursor);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  *cached_frame_count_out = cached_frame_cursor + 1u;
  if (cached_frame_capacity < *cached_frame_count_out) {
    *cached_frame_count_out = cached_frame_capacity;
  }
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_build_current_track_query_result_for_active_checkpoint(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    const fzgx_vec3 *point,
    int32_t active_checkpoint_index,
    float active_checkpoint_fraction,
    fzgx_current_track_query_result *query_out) {
  fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY];
  fzgx_current_checkpoint_query_result checkpoint_result;
  fzgx_active_checkpoint_bank_result bank_result;
  uint32_t cached_frame_count = 0u;
  uint32_t cached_frame_slot = 0u;
  fzgx_status status;

  if ((course == 0) || (point == 0) || (query_out == 0) || (active_checkpoint_index < 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  fzgx_init_current_checkpoint_query_result(&checkpoint_result);
  checkpoint_result.checkpoint_index = active_checkpoint_index;
  checkpoint_result.checkpoint_fraction = active_checkpoint_fraction;
  status = fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)active_checkpoint_index,
      0u,
      active_checkpoint_fraction,
      &checkpoint_result.point_on_track);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_course_build_active_checkpoint_bank_for_point(
      course,
      authored_track_id,
      circuit_type,
      point,
      active_checkpoint_index,
      active_checkpoint_fraction,
      &bank_result);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  cached_frame_slot = bank_result.preferred_variant_slot;
  if (cached_frame_slot >= bank_result.checkpoint_variant_count) {
    cached_frame_slot = 0u;
  }
  status = fzgx_track_course_build_cached_frames_for_checkpoint(
      course,
      animation_course,
      (uint32_t)bank_result.checkpoint_index[cached_frame_slot],
      bank_result.checkpoint_fraction[cached_frame_slot],
      cached_frames,
      FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
      &cached_frame_count);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  status = fzgx_build_current_track_query_result_from_bank_and_nearest_frame(
      &checkpoint_result,
      &bank_result,
      cached_frames,
      cached_frame_count,
      point,
      query_out);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  query_out->last_frac_diff = course->track_total_distance;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_build_ordinary_start_grid_slot_transform(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    uint32_t slot_index,
    float *track_width_or_radius_out,
    fzgx_mat43 *transform_out) {
  fzgx_track_frame_record frame;
  fzgx_mat43 transform;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  fzgx_status status;

  if ((course == 0) || (transform_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_track_course_build_ordinary_start_grid_slot_frame(
      course,
      animation_course,
      authored_track_id,
      circuit_type,
      slot_index,
      &checkpoint_index,
      &checkpoint_fraction,
      &frame,
      &transform);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  if (track_width_or_radius_out != 0) {
    *track_width_or_radius_out = frame.track_width_or_radius;
  }
  *transform_out = transform;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_course_build_ordinary_start_grid_slot_query_result(
    const fzgx_track_course_content *course,
    const fzgx_track_course_animation_content *animation_course,
    uint32_t authored_track_id,
    uint32_t circuit_type,
    uint32_t slot_index,
    fzgx_current_track_query_result *query_out) {
  fzgx_track_frame_record resolved_frame;
  fzgx_mat43 placement_transform;
  fzgx_current_checkpoint_query_result checkpoint_result;
  fzgx_active_checkpoint_bank_result bank_result;
  fzgx_track_frame_export_buffer frame_buffer;
  int32_t checkpoint_index;
  float checkpoint_fraction;
  fzgx_status status;

  if ((course == 0) || (query_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }

  status = fzgx_track_course_build_ordinary_start_grid_slot_frame(
      course,
      animation_course,
      authored_track_id,
      circuit_type,
      slot_index,
      &checkpoint_index,
      &checkpoint_fraction,
      &resolved_frame,
      &placement_transform);
  if (status != FZGX_STATUS_OK) {
    return status;
  }

  memset(query_out, 0, sizeof(*query_out));
  fzgx_init_current_checkpoint_query_result(&checkpoint_result);
  checkpoint_result.checkpoint_index = checkpoint_index;
  checkpoint_result.checkpoint_fraction = checkpoint_fraction;
  status = fzgx_track_course_eval_checkpoint_point(
      course,
      (uint32_t)checkpoint_index,
      0u,
      checkpoint_fraction,
      &checkpoint_result.point_on_track);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_track_course_build_active_checkpoint_bank_for_point(
      course,
      authored_track_id,
      circuit_type,
      &(fzgx_vec3){placement_transform.origin_x, placement_transform.origin_y, placement_transform.origin_z},
      checkpoint_index,
      checkpoint_fraction,
      &bank_result);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  frame_buffer.cached_frames = &resolved_frame;
  frame_buffer.cached_frame_count = 1u;
  frame_buffer.selected_frame_index = 0;
  status = fzgx_build_current_track_query_result_from_bank_and_frame_buffer(
      &checkpoint_result,
      &bank_result,
      &frame_buffer,
      query_out);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  query_out->last_frac_diff = course->track_total_distance;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_content_build_track_manifest_from_bytes(
    const uint8_t *data,
    uint32_t size,
    uint32_t authored_track_id,
    fzgx_track_manifest *manifest_out) {
  fzgx_owned_byte_buffer scene_data = {0};
  const uint8_t *scene_bytes = 0;
  uint32_t scene_size = 0u;
  fzgx_scene_track_header_exact header;
  uint32_t track_node_index;
  uint32_t checkpoint_variant_count = 0u;
  uint8_t supports_branching = 0u;
  fzgx_status status;

  if ((data == 0) || (manifest_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(manifest_out, 0, sizeof(*manifest_out));
  status = fzgx_prepare_scene_bytes_exact(data, size, &scene_data, &scene_bytes, &scene_size);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_parse_scene_track_header_exact(scene_bytes, scene_size, &header);
  if (status != FZGX_STATUS_OK) {
    fzgx_owned_byte_buffer_release_exact(&scene_data);
    return status;
  }
  for (track_node_index = 0u; track_node_index < header.track_node_count; ++track_node_index) {
    uint32_t node_offset = header.track_node_ptr + track_node_index * 0x0cu;
    uint32_t checkpoint_count = fzgx_read_u32be_exact(scene_bytes, node_offset + 0x00u);

    if (checkpoint_count > checkpoint_variant_count) {
      checkpoint_variant_count = checkpoint_count;
    }
    if (checkpoint_count > 1u) {
      supports_branching = 1u;
    }
  }

  manifest_out->authored_track_id = authored_track_id;
  manifest_out->checkpoint_count = header.track_node_count;
  manifest_out->checkpoint_variant_count = checkpoint_variant_count;
  manifest_out->circuit_type = header.circuit_type;
  manifest_out->time_extension_trigger_count = header.time_extension_trigger_count;
  manifest_out->supports_branching = supports_branching;
  fzgx_owned_byte_buffer_release_exact(&scene_data);
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_content_load_track_course_content_from_bytes(
    const uint8_t *data,
    uint32_t size,
    uint32_t authored_track_id,
    fzgx_owned_track_course_content *course_out) {
  fzgx_owned_byte_buffer scene_data = {0};
  const uint8_t *scene_bytes = 0;
  uint32_t scene_size = 0u;
  fzgx_scene_track_header_exact header;
  fzgx_track_segment_record *segments = 0;
  uint32_t segment_count = 0u;
  uint32_t segment_capacity = 0u;
  uint32_t *corner_addresses = 0;
  uint32_t corner_count = 0u;
  uint32_t corner_capacity = 0u;
  uint32_t checkpoint_total = 0u;
  uint32_t track_node_index;
  fzgx_status status;

  if ((data == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(course_out, 0, sizeof(*course_out));
  status = fzgx_prepare_scene_bytes_exact(data, size, &scene_data, &scene_bytes, &scene_size);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_parse_scene_track_header_exact(scene_bytes, scene_size, &header);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }

  course_out->course.authored_track_id = authored_track_id;
  course_out->course.track_node_count = header.track_node_count;
  course_out->course.track_total_distance = header.track_total_distance;
  course_out->course.track_min_height = header.track_min_height;
  course_out->course.time_extension_trigger_count = header.time_extension_trigger_count;

  if (header.time_extension_trigger_count != 0u) {
    uint32_t trigger_index;

    course_out->time_extension_triggers = (fzgx_time_extension_trigger_record *)calloc(
        header.time_extension_trigger_count, sizeof(*course_out->time_extension_triggers));
    if (course_out->time_extension_triggers == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    for (trigger_index = 0u; trigger_index < header.time_extension_trigger_count; ++trigger_index) {
      uint32_t trigger_address = header.time_extension_trigger_ptr + trigger_index * 0x24u;
      fzgx_time_extension_trigger_record *trigger = &course_out->time_extension_triggers[trigger_index];

      fzgx_read_vec3be_exact(scene_bytes, trigger_address + 0x00u, &trigger->position);
      trigger->rotation_x_angle16 = fzgx_read_u16be_exact(scene_bytes, trigger_address + 0x0cu);
      trigger->rotation_y_angle16 = fzgx_read_u16be_exact(scene_bytes, trigger_address + 0x0eu);
      trigger->rotation_z_angle16 = fzgx_read_u16be_exact(scene_bytes, trigger_address + 0x10u);
      trigger->unknown_transform_option = scene_bytes[trigger_address + 0x12u];
      trigger->object_active_override = scene_bytes[trigger_address + 0x13u];
      fzgx_read_vec3be_exact(scene_bytes, trigger_address + 0x14u, &trigger->scale);
      trigger->option = fzgx_read_u32be_exact(scene_bytes, trigger_address + 0x20u);
    }
  }

  if (header.track_node_count != 0u) {
    course_out->track_nodes = (fzgx_track_node_record *)calloc(
        header.track_node_count, sizeof(*course_out->track_nodes));
    if (course_out->track_nodes == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }

  for (track_node_index = 0u; track_node_index < header.track_node_count; ++track_node_index) {
    uint32_t node_offset = header.track_node_ptr + track_node_index * 0x0cu;
    uint32_t checkpoint_count = fzgx_read_u32be_exact(scene_bytes, node_offset + 0x00u);
    uint32_t checkpoint_address = fzgx_read_u32be_exact(scene_bytes, node_offset + 0x04u);
    uint32_t root_segment_address = fzgx_read_u32be_exact(scene_bytes, node_offset + 0x08u);
    fzgx_track_node_record *track_node = &course_out->track_nodes[track_node_index];

    if ((checkpoint_count != 0u) &&
        !fzgx_range_is_valid_exact(scene_size, checkpoint_address, checkpoint_count * 0x50u)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    if (checkpoint_total > (0xffffffffu - checkpoint_count)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    track_node->checkpoint_count = checkpoint_count;
    track_node->checkpoint_offset = checkpoint_total;
    track_node->checkpoint_address = checkpoint_address;
    track_node->root_segment_address = root_segment_address;
    checkpoint_total += checkpoint_count;

    if (root_segment_address != 0u) {
      status = fzgx_collect_track_segment_records_recursive_exact(
          scene_bytes,
          scene_size,
          root_segment_address,
          &segments,
          &segment_count,
          &segment_capacity,
          &corner_addresses,
          &corner_count,
          &corner_capacity);
      if (status != FZGX_STATUS_OK) {
        goto cleanup;
      }
    }
  }

  course_out->course.checkpoint_record_count = checkpoint_total;
  if (checkpoint_total != 0u) {
    uint32_t checkpoint_index = 0u;

    course_out->checkpoints = (fzgx_checkpoint_record *)calloc(
        checkpoint_total, sizeof(*course_out->checkpoints));
    if (course_out->checkpoints == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    for (track_node_index = 0u; track_node_index < header.track_node_count; ++track_node_index) {
      const fzgx_track_node_record *track_node = &course_out->track_nodes[track_node_index];
      uint32_t variant_index;

      for (variant_index = 0u; variant_index < track_node->checkpoint_count; ++variant_index) {
        status = fzgx_parse_track_checkpoint_record_exact(
            scene_bytes,
            scene_size,
            track_node->checkpoint_address + variant_index * 0x50u,
            &course_out->checkpoints[checkpoint_index]);
        if (status != FZGX_STATUS_OK) {
          goto cleanup;
        }
        ++checkpoint_index;
      }
    }
  }

  if (segment_count != 0u) {
    qsort(segments, segment_count, sizeof(*segments), fzgx_compare_track_segment_record_address_exact);
  }
  if (corner_count != 0u) {
    qsort(corner_addresses, corner_count, sizeof(*corner_addresses), fzgx_compare_u32_exact);
  }

  course_out->course.track_segment_count = segment_count;
  course_out->track_segments = segments;
  segments = 0;

  course_out->course.track_corner_count = corner_count;
  if (corner_count != 0u) {
    uint32_t corner_index;

    course_out->track_corners = (fzgx_track_corner_record *)calloc(
        corner_count, sizeof(*course_out->track_corners));
    if (course_out->track_corners == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    for (corner_index = 0u; corner_index < corner_count; ++corner_index) {
      status = fzgx_parse_track_corner_record_exact(
          scene_bytes,
          scene_size,
          corner_addresses[corner_index],
          &course_out->track_corners[corner_index]);
      if (status != FZGX_STATUS_OK) {
        goto cleanup;
      }
    }
  }

  course_out->course.time_extension_triggers = course_out->time_extension_triggers;
  course_out->course.track_nodes = course_out->track_nodes;
  course_out->course.checkpoints = course_out->checkpoints;
  course_out->course.track_segments = course_out->track_segments;
  course_out->course.track_corners = course_out->track_corners;
  status = FZGX_STATUS_OK;

cleanup:
  free(corner_addresses);
  free(segments);
  fzgx_owned_byte_buffer_release_exact(&scene_data);
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_track_course_content_internal(course_out);
  }
  return status;
}

void fzgx_content_release_track_course_content(fzgx_owned_track_course_content *course) {
  fzgx_content_release_track_course_content_internal(course);
}

fzgx_status fzgx_content_load_track_course_animation_content_from_bytes(
    const uint8_t *data,
    uint32_t size,
    uint32_t authored_track_id,
    fzgx_owned_track_course_animation_content *course_out) {
  fzgx_owned_byte_buffer scene_data = {0};
  const uint8_t *scene_bytes = 0;
  uint32_t scene_size = 0u;
  fzgx_scene_track_header_exact header;
  fzgx_track_segment_record *segments = 0;
  uint32_t segment_count = 0u;
  uint32_t segment_capacity = 0u;
  uint32_t *corner_addresses = 0;
  uint32_t corner_count = 0u;
  uint32_t corner_capacity = 0u;
  uint32_t animation_curve_trs_count = 0u;
  uint32_t animation_curve_count = 0u;
  uint32_t keyable_attribute_count = 0u;
  uint32_t segment_index;
  uint32_t curve_trs_cursor = 0u;
  uint32_t curve_cursor = 0u;
  uint32_t keyable_cursor = 0u;
  fzgx_status status;

  if ((data == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(course_out, 0, sizeof(*course_out));
  status = fzgx_prepare_scene_bytes_exact(data, size, &scene_data, &scene_bytes, &scene_size);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_parse_scene_track_header_exact(scene_bytes, scene_size, &header);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }

  for (segment_index = 0u; segment_index < header.track_node_count; ++segment_index) {
    uint32_t node_offset = header.track_node_ptr + segment_index * 0x0cu;
    uint32_t root_segment_address = fzgx_read_u32be_exact(scene_bytes, node_offset + 0x08u);

    if (root_segment_address == 0u) {
      continue;
    }
    status = fzgx_collect_track_segment_records_recursive_exact(
        scene_bytes,
        scene_size,
        root_segment_address,
        &segments,
        &segment_count,
        &segment_capacity,
        &corner_addresses,
        &corner_count,
        &corner_capacity);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  if (segment_count != 0u) {
    qsort(segments, segment_count, sizeof(*segments), fzgx_compare_track_segment_record_address_exact);
  }

  for (segment_index = 0u; segment_index < segment_count; ++segment_index) {
    uint32_t animation_ptr = segments[segment_index].animation_curves_trs_address;
    uint32_t curve_index;

    if (animation_ptr == 0u) {
      continue;
    }
    if (!fzgx_range_is_valid_exact(scene_size, animation_ptr, 0x48u)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    ++animation_curve_trs_count;
    animation_curve_count += 9u;
    for (curve_index = 0u; curve_index < 9u; ++curve_index) {
      uint32_t keyable_count = fzgx_read_u32be_exact(scene_bytes, animation_ptr + curve_index * 4u);

      if (keyable_attribute_count > (0xffffffffu - keyable_count)) {
        status = FZGX_STATUS_OUT_OF_RANGE;
        goto cleanup;
      }
      keyable_attribute_count += keyable_count;
    }
  }

  course_out->course.authored_track_id = authored_track_id;
  course_out->course.animation_curve_trs_count = animation_curve_trs_count;
  course_out->course.animation_curve_count = animation_curve_count;
  course_out->course.keyable_attribute_count = keyable_attribute_count;
  course_out->course.track_segment_count = segment_count;

  if (segment_count != 0u) {
    course_out->track_segments = (fzgx_track_segment_animation_record *)calloc(
        segment_count, sizeof(*course_out->track_segments));
    if (course_out->track_segments == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }
  if (animation_curve_trs_count != 0u) {
    course_out->animation_curve_trs = (fzgx_animation_curve_trs *)calloc(
        animation_curve_trs_count, sizeof(*course_out->animation_curve_trs));
    course_out->animation_curves = (fzgx_animation_curve *)calloc(
        animation_curve_count, sizeof(*course_out->animation_curves));
    if ((course_out->animation_curve_trs == 0) || (course_out->animation_curves == 0)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }
  if (keyable_attribute_count != 0u) {
    course_out->keyable_attributes = (fzgx_keyable_attribute *)calloc(
        keyable_attribute_count, sizeof(*course_out->keyable_attributes));
    if (course_out->keyable_attributes == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }

  for (segment_index = 0u; segment_index < segment_count; ++segment_index) {
    const fzgx_track_segment_record *segment = &segments[segment_index];
    fzgx_track_segment_animation_record *animation_segment = &course_out->track_segments[segment_index];
    uint32_t animation_ptr = segment->animation_curves_trs_address;

    animation_segment->address = segment->address;
    animation_segment->animation_curves_trs_address = animation_ptr;
    if (animation_ptr != 0u) {
      fzgx_animation_curve_trs *curve_trs = &course_out->animation_curve_trs[curve_trs_cursor];
      uint32_t curve_index;

      animation_segment->animation_curve_trs = curve_trs;
      curve_trs->curve_count = 9u;
      curve_trs->curves = &course_out->animation_curves[curve_cursor];
      ++curve_trs_cursor;

      for (curve_index = 0u; curve_index < 9u; ++curve_index) {
        uint32_t keyable_count = fzgx_read_u32be_exact(scene_bytes, animation_ptr + curve_index * 4u);
        uint32_t keyable_ptr = fzgx_read_u32be_exact(scene_bytes, animation_ptr + 0x24u + curve_index * 4u);
        fzgx_animation_curve *curve = &course_out->animation_curves[curve_cursor + curve_index];
        uint32_t keyable_index;

        curve->keyable_count = keyable_count;
        if (keyable_count == 0u) {
          curve->keyables = 0;
          continue;
        }
        if (!fzgx_range_is_valid_exact(scene_size, keyable_ptr, keyable_count * 0x14u)) {
          status = FZGX_STATUS_OUT_OF_RANGE;
          goto cleanup;
        }
        curve->keyables = &course_out->keyable_attributes[keyable_cursor];
        for (keyable_index = 0u; keyable_index < keyable_count; ++keyable_index) {
          uint32_t key_offset = keyable_ptr + keyable_index * 0x14u;
          fzgx_keyable_attribute *keyable =
              &course_out->keyable_attributes[keyable_cursor + keyable_index];

          keyable->interpolation_mode = fzgx_read_u32be_exact(scene_bytes, key_offset + 0x00u);
          keyable->time = fzgx_read_f32be_exact(scene_bytes, key_offset + 0x04u);
          keyable->value = fzgx_read_f32be_exact(scene_bytes, key_offset + 0x08u);
          keyable->tangent_in = fzgx_read_f32be_exact(scene_bytes, key_offset + 0x0cu);
          keyable->tangent_out = fzgx_read_f32be_exact(scene_bytes, key_offset + 0x10u);
        }
        keyable_cursor += keyable_count;
      }
      curve_cursor += 9u;
    }
  }

  course_out->course.track_segments = course_out->track_segments;
  status = FZGX_STATUS_OK;

cleanup:
  free(corner_addresses);
  free(segments);
  fzgx_owned_byte_buffer_release_exact(&scene_data);
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_track_course_animation_content_internal(course_out);
  }
  return status;
}

void fzgx_content_release_track_course_animation_content(
    fzgx_owned_track_course_animation_content *course) {
  fzgx_content_release_track_course_animation_content_internal(course);
}

static fzgx_status fzgx_content_parse_static_collider_course_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    fzgx_owned_static_collider_course *course_out) {
  uint32_t scene_static_collider_ptr;
  uint32_t scene_zeroes0x20_ptr;
  uint32_t scene_track_min_height_ptr;
  uint32_t surface_count;
  uint32_t tri_ptr_offset;
  uint32_t tri_grid_ptrs_offset;
  uint32_t mesh_grid_offset;
  uint32_t quad_ptr_offset;
  uint32_t quad_grid_ptrs_offset;
  uint32_t bounding_sphere_ptr_offset;
  uint32_t tri_ptr;
  uint32_t quad_ptr;
  uint32_t bounding_sphere_ptr;
  uint32_t largest_tri_index = 0u;
  uint32_t largest_quad_index = 0u;
  uint32_t have_tri_index = 0u;
  uint32_t have_quad_index = 0u;
  uint32_t tri_capacity = 0u;
  uint32_t quad_capacity = 0u;
  uint32_t surface_index;
  fzgx_status status;

  if ((scene_bytes == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(course_out, 0, sizeof(*course_out));

  if (!fzgx_range_is_valid_exact(scene_size, 0x24u, 4u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  scene_static_collider_ptr = fzgx_read_u32be_exact(scene_bytes, 0x1cu);
  scene_zeroes0x20_ptr = fzgx_read_u32be_exact(scene_bytes, 0x20u);
  scene_track_min_height_ptr = fzgx_read_u32be_exact(scene_bytes, 0x24u);
  if ((scene_zeroes0x20_ptr == 0xe8u) && (scene_track_min_height_ptr == 0xfcu)) {
    surface_count = FZGX_COLI_STATIC_COLLIDER_SURFACE_COUNT_GX;
  } else if ((scene_zeroes0x20_ptr == 0xe4u) && (scene_track_min_height_ptr == 0xf8u)) {
    surface_count = FZGX_COLI_STATIC_COLLIDER_SURFACE_COUNT_AX;
  } else {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  if (scene_static_collider_ptr == 0u) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  tri_ptr_offset = scene_static_collider_ptr + 0x24u;
  tri_grid_ptrs_offset = tri_ptr_offset + 4u;
  mesh_grid_offset = tri_grid_ptrs_offset + surface_count * 4u;
  quad_ptr_offset = mesh_grid_offset + 0x18u;
  quad_grid_ptrs_offset = quad_ptr_offset + 4u;
  bounding_sphere_ptr_offset =
      quad_grid_ptrs_offset + surface_count * 4u + 0x20u + 8u + 8u + 0x10u;
  if (!fzgx_range_is_valid_exact(scene_size, tri_ptr_offset, 4u) ||
      !fzgx_range_is_valid_exact(scene_size, mesh_grid_offset, 0x18u) ||
      !fzgx_range_is_valid_exact(scene_size, quad_ptr_offset, 4u) ||
      !fzgx_range_is_valid_exact(scene_size, bounding_sphere_ptr_offset, 4u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  course_out->surface_count = surface_count;
  course_out->surface_grids = (fzgx_static_collider_surface_grid *)calloc(
      surface_count, sizeof(*course_out->surface_grids));
  if (course_out->surface_grids == 0) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  course_out->mesh_grid.left = fzgx_read_f32be_exact(scene_bytes, mesh_grid_offset + 0x00u);
  course_out->mesh_grid.top = fzgx_read_f32be_exact(scene_bytes, mesh_grid_offset + 0x04u);
  course_out->mesh_grid.subdivision_width =
      fzgx_read_f32be_exact(scene_bytes, mesh_grid_offset + 0x08u);
  course_out->mesh_grid.subdivision_length =
      fzgx_read_f32be_exact(scene_bytes, mesh_grid_offset + 0x0cu);
  course_out->mesh_grid.num_subdivisions_x =
      (int32_t)fzgx_read_u32be_exact(scene_bytes, mesh_grid_offset + 0x10u);
  course_out->mesh_grid.num_subdivisions_z =
      (int32_t)fzgx_read_u32be_exact(scene_bytes, mesh_grid_offset + 0x14u);

  for (surface_index = 0u; surface_index < surface_count; ++surface_index) {
    uint32_t tri_grid_ptr = fzgx_read_u32be_exact(scene_bytes, tri_grid_ptrs_offset + surface_index * 4u);
    uint32_t quad_grid_ptr =
        fzgx_read_u32be_exact(scene_bytes, quad_grid_ptrs_offset + surface_index * 4u);

    if (tri_grid_ptr != 0u) {
      status = fzgx_parse_static_collider_index_grid_exact(
          scene_bytes,
          scene_size,
          tri_grid_ptr,
          course_out->surface_grids[surface_index].tri_cells,
          &course_out->tri_indices,
          &course_out->tri_index_count,
          &tri_capacity,
          &largest_tri_index);
      if (status != FZGX_STATUS_OK) {
        goto cleanup;
      }
      if (course_out->tri_index_count != 0u) {
        have_tri_index = 1u;
      }
    }
    if (quad_grid_ptr != 0u) {
      status = fzgx_parse_static_collider_index_grid_exact(
          scene_bytes,
          scene_size,
          quad_grid_ptr,
          course_out->surface_grids[surface_index].quad_cells,
          &course_out->quad_indices,
          &course_out->quad_index_count,
          &quad_capacity,
          &largest_quad_index);
      if (status != FZGX_STATUS_OK) {
        goto cleanup;
      }
      if (course_out->quad_index_count != 0u) {
        have_quad_index = 1u;
      }
    }
  }

  tri_ptr = fzgx_read_u32be_exact(scene_bytes, tri_ptr_offset);
  quad_ptr = fzgx_read_u32be_exact(scene_bytes, quad_ptr_offset);
  course_out->tri_count = have_tri_index != 0u ? (largest_tri_index + 1u) : 0u;
  course_out->quad_count = have_quad_index != 0u ? (largest_quad_index + 1u) : 0u;

  status = fzgx_parse_static_collider_triangle_array_exact(
      scene_bytes, scene_size, tri_ptr, course_out->tri_count, &course_out->tris);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }
  status = fzgx_parse_static_collider_quad_array_exact(
      scene_bytes, scene_size, quad_ptr, course_out->quad_count, &course_out->quads);
  if (status != FZGX_STATUS_OK) {
    goto cleanup;
  }

  bounding_sphere_ptr = fzgx_read_u32be_exact(scene_bytes, bounding_sphere_ptr_offset);
  if ((bounding_sphere_ptr != 0u) && fzgx_range_is_valid_exact(scene_size, bounding_sphere_ptr, 0x10u)) {
    fzgx_read_vec3be_exact(scene_bytes, bounding_sphere_ptr + 0x00u, &course_out->bounding_sphere.origin);
    course_out->bounding_sphere.radius =
        fzgx_read_f32be_exact(scene_bytes, bounding_sphere_ptr + 0x0cu);
  }

  status = FZGX_STATUS_OK;

cleanup:
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_static_collider_course_internal(course_out);
  }
  return status;
}

fzgx_status fzgx_content_load_static_collider_course_from_bytes(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_static_collider_course *course_out) {
  fzgx_owned_byte_buffer scene_data = {0};
  const uint8_t *scene_bytes = 0;
  uint32_t scene_size = 0u;
  fzgx_status status;

  if ((data == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_prepare_scene_bytes_exact(data, size, &scene_data, &scene_bytes, &scene_size);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_parse_static_collider_course_exact(scene_bytes, scene_size, course_out);
  fzgx_owned_byte_buffer_release_exact(&scene_data);
  return status;
}

fzgx_status fzgx_content_load_static_collider_course_from_path(
    const char *path,
    fzgx_owned_static_collider_course *course_out) {
  fzgx_owned_byte_buffer file_data = {0};
  fzgx_status status;

  if ((path == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_read_file_exact(path, &file_data);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_load_static_collider_course_from_bytes(
      file_data.data,
      file_data.size,
      course_out);
  fzgx_owned_byte_buffer_release_exact(&file_data);
  return status;
}

void fzgx_content_release_static_collider_course(fzgx_owned_static_collider_course *course) {
  fzgx_content_release_static_collider_course_internal(course);
}

static fzgx_status fzgx_content_parse_dynamic_scene_collision_course_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    fzgx_owned_dynamic_scene_collision_course *course_out) {
  uint32_t object_count;
  uint32_t object_ptr;
  uint32_t unknown_collider_count;
  uint32_t unknown_collider_ptr;
  uint32_t static_scene_object_count;
  uint32_t static_scene_object_ptr;
  uint32_t object_index;
  fzgx_status status;

  if ((scene_bytes == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(course_out, 0, sizeof(*course_out));

  if (!fzgx_range_is_valid_exact(scene_size, 0x70u, 4u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  object_count = fzgx_read_u32be_exact(scene_bytes, 0x48u);
  object_ptr = fzgx_read_u32be_exact(scene_bytes, 0x54u);
  unknown_collider_count = fzgx_read_u32be_exact(scene_bytes, 0x5cu);
  unknown_collider_ptr = fzgx_read_u32be_exact(scene_bytes, 0x60u);
  static_scene_object_count = fzgx_read_u32be_exact(scene_bytes, 0x6cu);
  static_scene_object_ptr = fzgx_read_u32be_exact(scene_bytes, 0x70u);
  if ((object_count != 0u) &&
      !fzgx_range_is_valid_exact(scene_size, object_ptr, object_count * 0x40u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  if ((unknown_collider_count != 0u) &&
      !fzgx_range_is_valid_exact(scene_size, unknown_collider_ptr, unknown_collider_count * 0x24u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  if ((static_scene_object_count != 0u) &&
      !fzgx_range_is_valid_exact(scene_size, static_scene_object_ptr, static_scene_object_count * 4u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  course_out->object_count = object_count;
  if (object_count != 0u) {
    course_out->objects = (fzgx_owned_dynamic_scene_object_record *)calloc(
        object_count, sizeof(*course_out->objects));
    if (course_out->objects == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }
  course_out->unknown_collider_count = unknown_collider_count;
  if (unknown_collider_count != 0u) {
    course_out->unknown_colliders = (fzgx_owned_unknown_collider_record *)calloc(
        unknown_collider_count, sizeof(*course_out->unknown_colliders));
    if (course_out->unknown_colliders == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }
  course_out->static_scene_object_count = static_scene_object_count;
  if (static_scene_object_count != 0u) {
    course_out->static_scene_objects = (fzgx_owned_static_scene_object_record *)calloc(
        static_scene_object_count, sizeof(*course_out->static_scene_objects));
    if (course_out->static_scene_objects == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }

  for (object_index = 0u; object_index < object_count; ++object_index) {
    uint32_t record_offset = object_ptr + object_index * 0x40u;
    uint32_t scene_object_ptr;
    fzgx_owned_dynamic_scene_object_record *object = &course_out->objects[object_index];

    object->render_flags_0 = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x00u);
    object->render_flags_4 = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x04u);
    object->scene_object_address = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x08u);
    status = fzgx_parse_transform_trxs_exact(
        scene_bytes, scene_size, record_offset + 0x0cu, &object->transform);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    scene_object_ptr = object->scene_object_address;
    object->animation_clip_address = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x30u);
    status = fzgx_parse_dynamic_scene_animation_clip_exact(
        scene_bytes,
        scene_size,
        object->animation_clip_address,
        &object->animation_clip,
        &object->has_animation_clip);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    object->texture_scroll_address = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x34u);
    object->skeletal_animator_address = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x38u);
    object->transform_matrix_address = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x3cu);
    status = fzgx_parse_transform_matrix_3x4_exact(
        scene_bytes,
        scene_size,
        object->transform_matrix_address,
        &object->transform_matrix,
        &object->has_transform_matrix);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }

    status = fzgx_parse_scene_object_lod_name_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        object->primary_lod_name,
        (uint32_t)sizeof(object->primary_lod_name));
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    status = fzgx_parse_scene_object_collision_transform_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        &object->collision_transform_address,
        &object->collision_transform,
        &object->has_collision_transform);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    status = fzgx_parse_scene_object_collider_mesh_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        &object->collider_mesh,
        &object->has_collider_mesh);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  for (object_index = 0u; object_index < unknown_collider_count; ++object_index) {
    uint32_t record_offset = unknown_collider_ptr + object_index * 0x24u;
    uint32_t scene_object_ptr;
    fzgx_owned_unknown_collider_record *object = &course_out->unknown_colliders[object_index];

    object->scene_object_address = fzgx_read_u32be_exact(scene_bytes, record_offset + 0x00u);
    status = fzgx_parse_transform_trxs_exact(
        scene_bytes, scene_size, record_offset + 0x04u, &object->transform);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    scene_object_ptr = object->scene_object_address;
    status = fzgx_parse_scene_object_lod_name_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        object->primary_lod_name,
        (uint32_t)sizeof(object->primary_lod_name));
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    status = fzgx_parse_scene_object_collider_mesh_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        &object->collider_mesh,
        &object->has_collider_mesh);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  for (object_index = 0u; object_index < static_scene_object_count; ++object_index) {
    uint32_t record_offset = static_scene_object_ptr + object_index * 4u;
    uint32_t scene_object_ptr;
    fzgx_owned_static_scene_object_record *object = &course_out->static_scene_objects[object_index];

    object->scene_object_address = fzgx_read_u32be_exact(scene_bytes, record_offset);
    scene_object_ptr = object->scene_object_address;
    status = fzgx_parse_scene_object_lod_name_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        object->primary_lod_name,
        (uint32_t)sizeof(object->primary_lod_name));
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    status = fzgx_parse_scene_object_collider_mesh_exact(
        scene_bytes,
        scene_size,
        scene_object_ptr,
        &object->collider_mesh,
        &object->has_collider_mesh);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  status = FZGX_STATUS_OK;

cleanup:
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_dynamic_scene_collision_course_internal(course_out);
  }
  return status;
}

fzgx_status fzgx_content_load_dynamic_scene_collision_course_from_bytes(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_dynamic_scene_collision_course *course_out) {
  fzgx_owned_byte_buffer scene_data = {0};
  const uint8_t *scene_bytes = 0;
  uint32_t scene_size = 0u;
  fzgx_status status;

  if ((data == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_prepare_scene_bytes_exact(data, size, &scene_data, &scene_bytes, &scene_size);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_parse_dynamic_scene_collision_course_exact(scene_bytes, scene_size, course_out);
  fzgx_owned_byte_buffer_release_exact(&scene_data);
  return status;
}

fzgx_status fzgx_content_load_dynamic_scene_collision_course_from_path(
    const char *path,
    fzgx_owned_dynamic_scene_collision_course *course_out) {
  fzgx_owned_byte_buffer file_data = {0};
  fzgx_status status;

  if ((path == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_read_file_exact(path, &file_data);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_load_dynamic_scene_collision_course_from_bytes(
      file_data.data,
      file_data.size,
      course_out);
  fzgx_owned_byte_buffer_release_exact(&file_data);
  return status;
}

void fzgx_content_release_dynamic_scene_collision_course(
    fzgx_owned_dynamic_scene_collision_course *course) {
  fzgx_content_release_dynamic_scene_collision_course_internal(course);
}

static fzgx_status fzgx_content_parse_track_mesh_course_exact(
    const uint8_t *scene_bytes,
    uint32_t scene_size,
    fzgx_owned_track_mesh_course *course_out) {
  uint32_t scene_zeroes0x20_ptr;
  uint32_t scene_track_min_height_ptr;
  uint32_t class_count;
  uint32_t chunk_count;
  uint32_t chunk_ptr;
  uint32_t chunk_index;
  fzgx_status status;

  if ((scene_bytes == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(course_out, 0, sizeof(*course_out));

  if (!fzgx_range_is_valid_exact(scene_size, 0x24u, 4u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  scene_zeroes0x20_ptr = fzgx_read_u32be_exact(scene_bytes, 0x20u);
  scene_track_min_height_ptr = fzgx_read_u32be_exact(scene_bytes, 0x24u);
  if ((scene_zeroes0x20_ptr != 0xe8u) || (scene_track_min_height_ptr != 0xfcu)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  /*
   * g_read_and_instantiate_track_runtime() relocates 14 tri-grid slots and 14
   * quad-grid slots out of each GX mesh chunk runtime record. The decomp walks
   * those as seven 8-byte pairs, which makes the logical chunk stride 0x4b0
   * even though the typed slice only covers the leading 0x12c-byte header.
   */
  class_count = FZGX_TRACK_MESH_CLASS_COUNT_GX;
  chunk_count = fzgx_read_u32be_exact(scene_bytes, 0x18u);
  chunk_ptr = fzgx_read_u32be_exact(scene_bytes, 0x1cu);
  if ((chunk_count != 0u) &&
      !fzgx_range_is_valid_exact(
          scene_size,
          chunk_ptr,
          chunk_count * FZGX_COLI_TRACK_MESH_LOGICAL_CHUNK_STRIDE)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  course_out->class_count = class_count;
  course_out->chunk_count = chunk_count;
  if (chunk_count != 0u) {
    course_out->chunks = (fzgx_owned_track_mesh_chunk *)calloc(
        chunk_count, sizeof(*course_out->chunks));
    if (course_out->chunks == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }

  for (chunk_index = 0u; chunk_index < chunk_count; ++chunk_index) {
    fzgx_owned_track_mesh_chunk *chunk = &course_out->chunks[chunk_index];
    uint32_t chunk_address = chunk_ptr + chunk_index * FZGX_COLI_TRACK_MESH_LOGICAL_CHUNK_STRIDE;
    uint32_t tri_capacity = 0u;
    uint32_t quad_capacity = 0u;
    uint32_t largest_tri_index = 0u;
    uint32_t largest_quad_index = 0u;
    uint32_t have_tri_index = 0u;
    uint32_t have_quad_index = 0u;
    uint32_t class_index;
    uint32_t time_start_bits;
    uint32_t time_end_bits;

    chunk->address = chunk_address;
    chunk->class_count = class_count;
    chunk->unk_vec3_0x0.x = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x00u);
    chunk->unk_vec3_0x0.y = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x04u);
    chunk->unk_vec3_0x0.z = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x08u);
    chunk->rotation_x_angle16 = fzgx_read_u16be_exact(scene_bytes, chunk_address + 0x0cu);
    chunk->rotation_y_angle16 = fzgx_read_u16be_exact(scene_bytes, chunk_address + 0x0eu);
    chunk->rotation_z_angle16 = fzgx_read_u16be_exact(scene_bytes, chunk_address + 0x10u);
    chunk->flags_0x12 = fzgx_read_u16be_exact(scene_bytes, chunk_address + 0x12u);
    chunk->animation_record_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x14u);
    status = fzgx_parse_track_mesh_animation_exact(
        scene_bytes,
        scene_size,
        chunk->animation_record_address,
        &chunk->animation_record,
        &chunk->has_animation_record);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }

    chunk->unk_vec3_0x18.x = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x18u);
    chunk->unk_vec3_0x18.y = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x1cu);
    chunk->unk_vec3_0x18.z = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x20u);
    chunk->tri_vertex_base_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x24u);
    chunk->grid_origin_x = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x60u);
    chunk->grid_origin_z = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x64u);
    chunk->inv_cell_size_x = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x68u);
    chunk->inv_cell_size_z = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x6cu);
    chunk->grid_subdiv_x = (int32_t)fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x70u);
    chunk->grid_subdiv_z = (int32_t)fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x74u);
    if ((chunk->grid_subdiv_x < 0) || (chunk->grid_subdiv_z < 0)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    if ((chunk->grid_subdiv_z != 0) &&
        ((uint32_t)chunk->grid_subdiv_x >
         (0xffffffffu / (uint32_t)chunk->grid_subdiv_z))) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    chunk->cell_count = (uint32_t)chunk->grid_subdiv_x * (uint32_t)chunk->grid_subdiv_z;
    chunk->quad_vertex_base_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x78u);
    chunk->unknown_0xb8_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xb8u);
    chunk->unknown_0xc0_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xc0u);
    chunk->unknown_0xc8_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xc8u);
    chunk->unknown_0xd0_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xd0u);
    chunk->unknown_count_0xd4 = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xd4u);
    chunk->unknown_0xd8_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xd8u);
    chunk->render_instance_count_0xdc = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xdcu);
    chunk->render_instance_table_0xe0_address =
        fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xe0u);
    chunk->unknown_0xe8_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xe8u);
    chunk->attachment_record_address = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0xf4u);
    if (chunk->attachment_record_address != 0u) {
      if (!fzgx_range_is_valid_exact(scene_size, chunk->attachment_record_address, 0x10u)) {
        status = FZGX_STATUS_OUT_OF_RANGE;
        goto cleanup;
      }
      chunk->has_attachment_record = 1u;
      chunk->attachment_record.address = chunk->attachment_record_address;
      fzgx_read_vec3be_exact(
          scene_bytes,
          chunk->attachment_record_address + 0x00u,
          &chunk->attachment_record.point);
      chunk->attachment_record.unknown_0x0c =
          fzgx_read_f32be_exact(scene_bytes, chunk->attachment_record_address + 0x0cu);
    }
    chunk->flags_0x104 = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x104u);
    time_start_bits = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x108u);
    time_end_bits = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x10cu);
    if ((time_start_bits == 0u) && (time_end_bits == 0u)) {
      chunk->time_start_0x108 = fzgx_read_f32be_exact(scene_bytes, 0x00u);
      chunk->time_end_0x10c = fzgx_read_f32be_exact(scene_bytes, 0x04u);
    } else {
      chunk->time_start_0x108 = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x108u);
      chunk->time_end_0x10c = fzgx_read_f32be_exact(scene_bytes, chunk_address + 0x10cu);
    }
    chunk->curve_slot_addresses[0] = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x110u);
    chunk->curve_slot_addresses[1] = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x114u);
    chunk->curve_slot_addresses[2] = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x118u);
    chunk->curve_slot_addresses[3] = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x11cu);

    if (chunk->cell_count != 0u) {
      chunk->tri_cells = (fzgx_static_collider_index_span *)calloc(
          class_count * chunk->cell_count, sizeof(*chunk->tri_cells));
      chunk->quad_cells = (fzgx_static_collider_index_span *)calloc(
          class_count * chunk->cell_count, sizeof(*chunk->quad_cells));
      if ((chunk->tri_cells == 0) || (chunk->quad_cells == 0)) {
        status = FZGX_STATUS_OUT_OF_RANGE;
        goto cleanup;
      }
    }

    for (class_index = 0u; class_index < class_count; ++class_index) {
      uint32_t tri_grid_ptr = fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x28u + class_index * 4u);
      uint32_t quad_grid_ptr =
          fzgx_read_u32be_exact(scene_bytes, chunk_address + 0x7cu + class_index * 4u);
      chunk->tri_cell_table_addresses[class_index] = tri_grid_ptr;
      chunk->quad_cell_table_addresses[class_index] = quad_grid_ptr;

      if (tri_grid_ptr != 0u) {
        status = fzgx_parse_track_mesh_index_grid_exact(
            scene_bytes,
            scene_size,
            tri_grid_ptr,
            chunk->cell_count,
            chunk->tri_cells + class_index * chunk->cell_count,
            &chunk->tri_indices,
            &chunk->tri_index_count,
            &tri_capacity,
            &largest_tri_index);
        if (status != FZGX_STATUS_OK) {
          goto cleanup;
        }
        if (chunk->tri_index_count != 0u) {
          have_tri_index = 1u;
        }
      }
      if (quad_grid_ptr != 0u) {
        status = fzgx_parse_track_mesh_index_grid_exact(
            scene_bytes,
            scene_size,
            quad_grid_ptr,
            chunk->cell_count,
            chunk->quad_cells + class_index * chunk->cell_count,
            &chunk->quad_indices,
            &chunk->quad_index_count,
            &quad_capacity,
            &largest_quad_index);
        if (status != FZGX_STATUS_OK) {
          goto cleanup;
        }
        if (chunk->quad_index_count != 0u) {
          have_quad_index = 1u;
        }
      }
    }

    chunk->tri_count = have_tri_index != 0u ? (largest_tri_index + 1u) : 0u;
    chunk->quad_count = have_quad_index != 0u ? (largest_quad_index + 1u) : 0u;
    status = fzgx_parse_static_collider_triangle_array_exact(
        scene_bytes,
        scene_size,
        chunk->tri_vertex_base_address,
        chunk->tri_count,
        &chunk->tris);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
    status = fzgx_parse_static_collider_quad_array_exact(
        scene_bytes,
        scene_size,
        chunk->quad_vertex_base_address,
        chunk->quad_count,
        &chunk->quads);
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  status = FZGX_STATUS_OK;

cleanup:
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_track_mesh_course_internal(course_out);
  }
  return status;
}

fzgx_status fzgx_content_load_track_mesh_course_from_bytes(
    const uint8_t *data,
    uint32_t size,
    fzgx_owned_track_mesh_course *course_out) {
  fzgx_owned_byte_buffer scene_data = {0};
  const uint8_t *scene_bytes = 0;
  uint32_t scene_size = 0u;
  fzgx_status status;

  if ((data == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_prepare_scene_bytes_exact(data, size, &scene_data, &scene_bytes, &scene_size);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_parse_track_mesh_course_exact(scene_bytes, scene_size, course_out);
  fzgx_owned_byte_buffer_release_exact(&scene_data);
  return status;
}

fzgx_status fzgx_content_load_track_mesh_course_from_path(
    const char *path,
    fzgx_owned_track_mesh_course *course_out) {
  fzgx_owned_byte_buffer file_data = {0};
  fzgx_status status;

  if ((path == 0) || (course_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  status = fzgx_read_file_exact(path, &file_data);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  status = fzgx_content_load_track_mesh_course_from_bytes(
      file_data.data,
      file_data.size,
      course_out);
  fzgx_owned_byte_buffer_release_exact(&file_data);
  return status;
}

void fzgx_content_release_track_mesh_course(fzgx_owned_track_mesh_course *course) {
  fzgx_content_release_track_mesh_course_internal(course);
}

fzgx_status fzgx_content_load_stage_gma_model_table_from_path(
    const char *path,
    fzgx_owned_stage_gma_model_table *table_out) {
  fzgx_owned_byte_buffer file_data = {0};
  fzgx_owned_byte_buffer gma_data = {0};
  const uint8_t *gma_bytes = 0;
  uint32_t gma_size = 0u;
  uint32_t model_count;
  uint32_t model_base_address;
  uint32_t name_base_address;
  uint32_t model_index;
  fzgx_status status;

  if ((path == 0) || (table_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  memset(table_out, 0, sizeof(*table_out));

  status = fzgx_read_file_exact(path, &file_data);
  if (status != FZGX_STATUS_OK) {
    return status;
  }
  if ((file_data.size >= 8u) &&
      (((fzgx_read_u32le_exact(file_data.data, 0u) == file_data.size) ||
        ((fzgx_read_u32le_exact(file_data.data, 0u) + 8u) == file_data.size)))) {
    status = fzgx_decode_av_lz_exact(file_data.data, file_data.size, &gma_data);
    if (status != FZGX_STATUS_OK) {
      fzgx_owned_byte_buffer_release_exact(&file_data);
      return status;
    }
    gma_bytes = gma_data.data;
    gma_size = gma_data.size;
  } else {
    gma_bytes = file_data.data;
    gma_size = file_data.size;
  }

  if (!fzgx_range_is_valid_exact(gma_size, 0u, 8u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  model_count = fzgx_read_u32be_exact(gma_bytes, 0u);
  model_base_address = fzgx_read_u32be_exact(gma_bytes, 4u);
  if (!fzgx_range_is_valid_exact(gma_size, 8u, model_count * 8u)) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }
  name_base_address = 8u + model_count * 8u;
  if (name_base_address > gma_size) {
    status = FZGX_STATUS_OUT_OF_RANGE;
    goto cleanup;
  }

  table_out->model_count = model_count;
  table_out->model_base_address = model_base_address;
  table_out->name_base_address = name_base_address;
  if (model_count != 0u) {
    table_out->models = (fzgx_owned_stage_gma_model_record *)calloc(
        model_count, sizeof(*table_out->models));
    if (table_out->models == 0) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
  }

  for (model_index = 0u; model_index < model_count; ++model_index) {
    uint32_t entry_offset = 8u + model_index * 8u;
    uint32_t gcmf_relative_offset = fzgx_read_u32be_exact(gma_bytes, entry_offset + 0u);
    uint32_t name_relative_offset = fzgx_read_u32be_exact(gma_bytes, entry_offset + 4u);
    fzgx_owned_stage_gma_model_record *model = &table_out->models[model_index];

    model->index = model_index;
    model->gcmf_relative_offset = gcmf_relative_offset;
    model->name_relative_offset = name_relative_offset;
    model->is_null = (gcmf_relative_offset == 0xffffffffu) && (name_relative_offset == 0u);
    if (model->is_null != 0u) {
      continue;
    }

    model->gcmf_address = model_base_address + gcmf_relative_offset;
    model->name_address = name_base_address + name_relative_offset;
    if ((model->gcmf_address >= gma_size) || (model->name_address >= gma_size)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    status = fzgx_copy_c_string_exact(
        gma_bytes,
        gma_size,
        model->name_address,
        model->name,
        (uint32_t)sizeof(model->name));
    if (status != FZGX_STATUS_OK) {
      goto cleanup;
    }
  }

  for (model_index = 0u; model_index < model_count; ++model_index) {
    uint32_t next_gcmf_address = gma_size;
    uint32_t candidate_index;
    fzgx_owned_stage_gma_model_record *model = &table_out->models[model_index];

    if (model->is_null != 0u) {
      continue;
    }
    for (candidate_index = 0u; candidate_index < model_count; ++candidate_index) {
      const fzgx_owned_stage_gma_model_record *candidate = &table_out->models[candidate_index];
      if ((candidate->is_null == 0u) && (candidate->gcmf_address > model->gcmf_address) &&
          (candidate->gcmf_address < next_gcmf_address)) {
        next_gcmf_address = candidate->gcmf_address;
      }
    }
    model->gcmf_size = next_gcmf_address - model->gcmf_address;
    if (!fzgx_range_is_valid_exact(gma_size, model->gcmf_address, 0x30u)) {
      status = FZGX_STATUS_OUT_OF_RANGE;
      goto cleanup;
    }
    model->gcmf_magic = fzgx_read_u32be_exact(gma_bytes, model->gcmf_address + 0x00u);
    model->gcmf_attributes = fzgx_read_u32be_exact(gma_bytes, model->gcmf_address + 0x04u);
    fzgx_read_vec3be_exact(
        gma_bytes, model->gcmf_address + 0x08u, &model->gcmf_bounding_sphere.origin);
    model->gcmf_bounding_sphere.radius =
        fzgx_read_f32be_exact(gma_bytes, model->gcmf_address + 0x14u);
    model->gcmf_texture_count = fzgx_read_u16be_exact(gma_bytes, model->gcmf_address + 0x18u);
    model->gcmf_opaque_material_count =
        fzgx_read_u16be_exact(gma_bytes, model->gcmf_address + 0x1au);
    model->gcmf_translucid_material_count =
        fzgx_read_u16be_exact(gma_bytes, model->gcmf_address + 0x1cu);
    model->gcmf_bone_count = gma_bytes[model->gcmf_address + 0x1eu];
    model->gcmf_reserved_0x1f = gma_bytes[model->gcmf_address + 0x1fu];
    model->gcmf_submesh_offset =
        fzgx_read_u32be_exact(gma_bytes, model->gcmf_address + 0x20u);
    model->gcmf_zero_0x24 = fzgx_read_u32be_exact(gma_bytes, model->gcmf_address + 0x24u);
    memcpy(
        model->gcmf_bone_indices,
        gma_bytes + model->gcmf_address + 0x28u,
        sizeof(model->gcmf_bone_indices));
  }

  status = FZGX_STATUS_OK;

cleanup:
  fzgx_owned_byte_buffer_release_exact(&gma_data);
  fzgx_owned_byte_buffer_release_exact(&file_data);
  if (status != FZGX_STATUS_OK) {
    fzgx_content_release_stage_gma_model_table_internal(table_out);
  }
  return status;
}

void fzgx_content_release_stage_gma_model_table(fzgx_owned_stage_gma_model_table *table) {
  fzgx_content_release_stage_gma_model_table_internal(table);
}

fzgx_status fzgx_stage_gma_model_table_find_model_by_name(
    const fzgx_owned_stage_gma_model_table *table,
    const char *name,
    const fzgx_owned_stage_gma_model_record **model_out) {
  uint32_t model_index;

  if ((table == 0) || (name == 0) || (model_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  *model_out = 0;
  for (model_index = 0u; model_index < table->model_count; ++model_index) {
    const fzgx_owned_stage_gma_model_record *model = &table->models[model_index];
    if ((model->is_null == 0u) && (strcmp(model->name, name) == 0)) {
      *model_out = model;
      return FZGX_STATUS_OK;
    }
  }
  return FZGX_STATUS_OUT_OF_RANGE;
}

fzgx_status fzgx_static_collider_course_get_surface_tri_cell(
    const fzgx_owned_static_collider_course *course,
    uint32_t surface_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out) {
  const fzgx_static_collider_index_span *span;

  if ((course == 0) || (indices_out == 0) || (count_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((surface_index >= course->surface_count) ||
      (cell_index >= FZGX_STATIC_COLLIDER_GRID_CELL_COUNT)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  span = &course->surface_grids[surface_index].tri_cells[cell_index];
  *count_out = span->count;
  *indices_out = (span->count != 0u) ? (course->tri_indices + span->offset) : 0;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_static_collider_course_get_surface_quad_cell(
    const fzgx_owned_static_collider_course *course,
    uint32_t surface_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out) {
  const fzgx_static_collider_index_span *span;

  if ((course == 0) || (indices_out == 0) || (count_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((surface_index >= course->surface_count) ||
      (cell_index >= FZGX_STATIC_COLLIDER_GRID_CELL_COUNT)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  span = &course->surface_grids[surface_index].quad_cells[cell_index];
  *count_out = span->count;
  *indices_out = (span->count != 0u) ? (course->quad_indices + span->offset) : 0;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_mesh_chunk_get_tri_cell(
    const fzgx_owned_track_mesh_chunk *chunk,
    uint32_t class_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out) {
  const fzgx_static_collider_index_span *span;

  if ((chunk == 0) || (indices_out == 0) || (count_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((class_index >= chunk->class_count) || (cell_index >= chunk->cell_count) ||
      (chunk->tri_cells == 0)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  span = &chunk->tri_cells[class_index * chunk->cell_count + cell_index];
  *count_out = span->count;
  *indices_out = (span->count != 0u) ? (chunk->tri_indices + span->offset) : 0;
  return FZGX_STATUS_OK;
}

fzgx_status fzgx_track_mesh_chunk_get_quad_cell(
    const fzgx_owned_track_mesh_chunk *chunk,
    uint32_t class_index,
    uint32_t cell_index,
    const uint16_t **indices_out,
    uint32_t *count_out) {
  const fzgx_static_collider_index_span *span;

  if ((chunk == 0) || (indices_out == 0) || (count_out == 0)) {
    return FZGX_STATUS_BAD_ARGUMENT;
  }
  if ((class_index >= chunk->class_count) || (cell_index >= chunk->cell_count) ||
      (chunk->quad_cells == 0)) {
    return FZGX_STATUS_OUT_OF_RANGE;
  }
  span = &chunk->quad_cells[class_index * chunk->cell_count + cell_index];
  *count_out = span->count;
  *indices_out = (span->count != 0u) ? (chunk->quad_indices + span->offset) : 0;
  return FZGX_STATUS_OK;
}
