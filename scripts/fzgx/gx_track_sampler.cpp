#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "fzgx/content.h"
}

enum {
    ROAD_SAMPLE_MAX_TERRAIN_BANDS = 16,
};

struct PieceSample {
    const fzgx_track_segment_record *segment;
    const fzgx_track_segment_animation_record *animation_segment;
    uint32_t source_piece_word;
    int32_t branch_index;
    fzgx_mat43 transform;
    fzgx_vec3 scale;
    float time;
};

struct CurveKey {
    uint32_t interpolation_mode;
    float time;
    float value;
    float tangent_in;
    float tangent_out;
};

struct ModulationProfile {
    bool has_profile;
    float fallback_height;
    std::vector<CurveKey> keys;
};

struct RoadSample {
    double distance;
    uint32_t sample_sequence;
    int32_t checkpoint_index;
    float checkpoint_fraction;
    bool authored_gap_before;
    bool stream_break_before;
    uint32_t source_piece_word;
    uint32_t source_segment_address;
    uint32_t root_segment_address;
    int32_t branch_index;
    uint32_t frame_index;
    float track_scl_x;
    float track_scl_y;
    float track_width_or_radius;
    float track_hcylin;
    float rounded_height;
    float rail_height_left;
    float rail_height_right;
    fzgx_vec3 track_anchor;
    fzgx_vec3 track_follow_offset;
    fzgx_vec3 track_current_scale;
    fzgx_vec3 center;
    fzgx_vec3 left;
    fzgx_vec3 right;
    fzgx_vec3 basis_right;
    fzgx_vec3 basis_up;
    fzgx_vec3 basis_forward;
    const char *shape;
    uint32_t terrain_flags[ROAD_SAMPLE_MAX_TERRAIN_BANDS];
    float terrain_left_tx[ROAD_SAMPLE_MAX_TERRAIN_BANDS];
    float terrain_right_tx[ROAD_SAMPLE_MAX_TERRAIN_BANDS];
    uint32_t terrain_count;
    ModulationProfile modulation;
};

struct RoadStream {
    uint32_t source_segment_address;
    ModulationProfile modulation;
    std::vector<RoadSample> samples;
};

static fzgx_mat43 identity_mat43() {
    fzgx_mat43 m = {};
    m.basis_x_x = 1.0f;
    m.basis_y_y = 1.0f;
    m.basis_z_z = 1.0f;
    return m;
}

static std::vector<uint8_t> read_file_bytes(const char *path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }
    std::vector<uint8_t> bytes((size_t)size);
    file.read((char *)bytes.data(), size);
    return bytes;
}

static uint32_t parse_course_id_from_path(const char *path) {
    const char *last_digits = nullptr;
    for (const char *p = path; *p != '\0'; ++p) {
        if (std::isdigit((unsigned char)*p)) {
            last_digits = p;
            while (std::isdigit((unsigned char)*p)) {
                ++p;
            }
            --p;
        }
    }
    return last_digits != nullptr ? (uint32_t)std::strtoul(last_digits, nullptr, 10) : 0u;
}

static bool is_skipped_in_walk(uint32_t source_piece_word) {
    return (source_piece_word & 0x001e0002u) != 0u;
}

static bool is_renderable_surface(uint32_t source_piece_word) {
    return (source_piece_word & 0x03e00000u) != 0u;
}

static uint32_t terrain_flags_for_source(uint32_t source_piece_word) {
    return source_piece_word & 0x001e0000u;
}

static const char *shape_for_source(uint32_t source_piece_word) {
    const bool open = (source_piece_word & 0x00800000u) != 0u;
    const bool round_pipe_family = (source_piece_word & 0x01800000u) != 0u;
    const bool capsule_family = (source_piece_word & 0x00400000u) != 0u;
    const bool cylinder_not_pipe = (source_piece_word & 0x00000001u) != 0u;

    if (capsule_family) {
        return open ? "ROUNDED_SQUARE_OPEN" : "ROUNDED_SQUARE";
    }
    if (round_pipe_family) {
        if (cylinder_not_pipe) {
            return open ? "CYLINDER_OPEN" : "CYLINDER";
        }
        return open ? "PIPE_OPEN" : "PIPE";
    }
    return "FLAT";
}

static const char *terrain_type_for_flag(uint32_t flag) {
    switch (flag) {
        case 0x00100000u:
            return "RECHARGE";
        case 0x00080000u:
            return "ICE";
        case 0x00040000u:
            return "DIRT";
        case 0x00020000u:
            return "LAVA";
        default:
            return "UNKNOWN";
    }
}

static bool animation_segment_curve_window_contains(
        const fzgx_track_segment_animation_record *animation_segment,
        float curve_time);

static bool source_is_capsule(uint32_t source_piece_word) {
    return (source_piece_word & 0x00400000u) != 0u;
}

static bool authored_gap_between_samples(
        const fzgx_track_course_content *course,
        const RoadSample &previous,
        const RoadSample &current) {
    uint32_t can_traverse = 1u;
    if (course == nullptr) {
        return true;
    }
    if (previous.checkpoint_index == current.checkpoint_index) {
        return false;
    }
    const fzgx_status status = fzgx_track_course_can_traverse_checkpoint_interval_exact(
            course, previous.checkpoint_index, current.checkpoint_index, &can_traverse);
    if (status != FZGX_STATUS_OK) {
        return true;
    }
    return can_traverse == 0u;
}

static int find_road_stream_index(std::vector<RoadStream> *streams, uint32_t source_segment_address) {
    if (streams == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < streams->size(); ++i) {
        if ((*streams)[i].source_segment_address == source_segment_address) {
            return (int)i;
        }
    }
    RoadStream stream = {};
    stream.source_segment_address = source_segment_address;
    streams->push_back(stream);
    return (int)(streams->size() - 1u);
}

static float sample_cross_extent(uint32_t source_piece_word, float track_width_or_radius) {
    if ((source_piece_word & 0x01800000u) != 0u) {
        return std::max(0.5f, std::fabs(track_width_or_radius));
    }
    return std::max(0.5f, 0.5f * std::fabs(track_width_or_radius));
}

static void copy_modulation_profile(
        ModulationProfile *profile,
        const PieceSample &piece) {
    const uint32_t profile_curve_index = 7u;
    const fzgx_animation_curve *profile_curve = nullptr;

    if ((profile == nullptr) || ((piece.source_piece_word & 0x00200000u) == 0u) ||
        profile->has_profile || (piece.segment == nullptr)) {
        return;
    }
    profile->has_profile = true;
    profile->fallback_height = piece.segment->fallback_position.y;
    if ((piece.animation_segment == nullptr) ||
        (piece.animation_segment->animation_curve_trs == nullptr) ||
        (piece.animation_segment->animation_curve_trs->curves == nullptr) ||
        (piece.animation_segment->animation_curve_trs->curve_count <= profile_curve_index)) {
        return;
    }
    profile_curve = &piece.animation_segment->animation_curve_trs->curves[profile_curve_index];
    if ((profile_curve->keyables == nullptr) || (profile_curve->keyable_count == 0u)) {
        return;
    }
    profile->keys.reserve(profile_curve->keyable_count);
    for (uint32_t key_index = 0u; key_index < profile_curve->keyable_count; ++key_index) {
        const fzgx_keyable_attribute *key = &profile_curve->keyables[key_index];
        CurveKey out = {};
        out.interpolation_mode = key->interpolation_mode;
        out.time = key->time;
        out.value = key->value;
        out.tangent_in = key->tangent_in;
        out.tangent_out = key->tangent_out;
        profile->keys.push_back(out);
    }
}

static fzgx_status collect_piece_samples_recursive(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        const fzgx_track_segment_record *track_segment,
        float time,
        const fzgx_mat43 *parent_transform,
        const fzgx_vec3 *parent_scale,
        std::vector<PieceSample> *pieces_out) {
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
    if (is_skipped_in_walk(source_piece_word)) {
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

    if (is_renderable_surface(source_piece_word)) {
        PieceSample piece = {};
        piece.segment = track_segment;
        piece.animation_segment = animation_segment;
        piece.source_piece_word = source_piece_word;
        piece.branch_index = track_segment->branch_index;
        piece.transform = transform;
        piece.scale = scale;
        piece.time = time;
        pieces_out->push_back(piece);
    }

    status = fzgx_track_course_get_track_segment_children(
            course, track_segment, &children, &child_count);
    if (status != FZGX_STATUS_OK) {
        return status;
    }
    for (uint32_t child_index = 0u; child_index < child_count; ++child_index) {
        status = collect_piece_samples_recursive(
                course, animation_course, &children[child_index], time,
                &transform, &scale, pieces_out);
        if (status != FZGX_STATUS_OK) {
            return status;
        }
    }
    return FZGX_STATUS_OK;
}

static fzgx_vec3 vec3_sub(fzgx_vec3 a, fzgx_vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static fzgx_vec3 vec3_add(fzgx_vec3 a, fzgx_vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static fzgx_vec3 vec3_scale(fzgx_vec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

static float vec3_dot(fzgx_vec3 a, fzgx_vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static float vec3_length(fzgx_vec3 v) {
    return std::sqrt(vec3_dot(v, v));
}

static fzgx_vec3 vec3_lerp(fzgx_vec3 a, fzgx_vec3 b, float t) {
    return {
        a.x + ((b.x - a.x) * t),
        a.y + ((b.y - a.y) * t),
        a.z + ((b.z - a.z) * t),
    };
}

static fzgx_vec3 mat43_basis_x(const fzgx_mat43 &m) {
    return {m.basis_x_x, m.basis_x_y, m.basis_x_z};
}

static fzgx_vec3 mat43_basis_y(const fzgx_mat43 &m) {
    return {m.basis_y_x, m.basis_y_y, m.basis_y_z};
}

static fzgx_vec3 mat43_basis_z(const fzgx_mat43 &m) {
    return {m.basis_z_x, m.basis_z_y, m.basis_z_z};
}

static fzgx_vec3 mat43_origin(const fzgx_mat43 &m) {
    return {m.origin_x, m.origin_y, m.origin_z};
}

static fzgx_vec3 mat43_local_point(const fzgx_mat43 &m, fzgx_vec3 p) {
    fzgx_vec3 d = {p.x - m.origin_x, p.y - m.origin_y, p.z - m.origin_z};
    fzgx_vec3 bx = {m.basis_x_x, m.basis_x_y, m.basis_x_z};
    fzgx_vec3 by = {m.basis_y_x, m.basis_y_y, m.basis_y_z};
    fzgx_vec3 bz = {m.basis_z_x, m.basis_z_y, m.basis_z_z};
    const float bx_len2 = std::max(1.0e-9f, vec3_dot(bx, bx));
    const float by_len2 = std::max(1.0e-9f, vec3_dot(by, by));
    const float bz_len2 = std::max(1.0e-9f, vec3_dot(bz, bz));
    return {vec3_dot(d, bx) / bx_len2, vec3_dot(d, by) / by_len2, vec3_dot(d, bz) / bz_len2};
}

static bool distance_for_curve_time(
        const fzgx_track_course_content *course,
        float curve_time,
        double *distance_out) {
    if ((course == nullptr) || (distance_out == nullptr) || (course->checkpoints == nullptr)) {
        return false;
    }
    for (uint32_t i = 0u; i < course->checkpoint_record_count; ++i) {
        const fzgx_checkpoint_record *checkpoint = &course->checkpoints[i];
        const float start_time = checkpoint->curve_time_start;
        const float end_time = checkpoint->curve_time_end;
        const float min_time = std::min(start_time, end_time);
        const float max_time = std::max(start_time, end_time);
        if ((curve_time + 1.0e-5f < min_time) || (max_time + 1.0e-5f < curve_time)) {
            continue;
        }
        const float denom = end_time - start_time;
        const double fraction =
                (std::fabs(denom) <= 1.0e-9f) ? 0.0 : (double)((curve_time - start_time) / denom);
        *distance_out =
                (double)checkpoint->start_distance +
                (fraction * (double)(checkpoint->end_distance - checkpoint->start_distance));
        return true;
    }
    return false;
}

static void append_source_key_distances(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        std::vector<double> *distances_inout) {
    if ((course == nullptr) || (animation_course == nullptr) || (distances_inout == nullptr)) {
        return;
    }
    for (uint32_t segment_index = 0u; segment_index < animation_course->track_segment_count; ++segment_index) {
        const fzgx_track_segment_animation_record *segment = &animation_course->track_segments[segment_index];
        const fzgx_animation_curve_trs *trs = segment->animation_curve_trs;
        if ((trs == nullptr) || (trs->curves == nullptr)) {
            continue;
        }
        for (uint32_t curve_index = 0u; curve_index < trs->curve_count; ++curve_index) {
            const fzgx_animation_curve *curve = &trs->curves[curve_index];
            if ((curve->keyables == nullptr) || (curve->keyable_count == 0u)) {
                continue;
            }
            for (uint32_t key_index = 0u; key_index < curve->keyable_count; ++key_index) {
                double distance = 0.0;
                if (distance_for_curve_time(course, curve->keyables[key_index].time, &distance)) {
                    distance = std::max(0.0, std::min((double)course->track_total_distance, distance));
                    distances_inout->push_back(distance);
                }
            }
        }
    }
}

static bool project_terrain_strip_to_road_tx(
        const fzgx_mat43 &terrain_transform,
        const fzgx_vec3 &terrain_scale,
        const RoadSample *sample,
        float *left_tx_out,
        float *right_tx_out) {
    if ((sample == nullptr) || (left_tx_out == nullptr) || (right_tx_out == nullptr)) {
        return false;
    }

    const fzgx_vec3 road_right = sample->right;
    const fzgx_vec3 road_left = sample->left;
    const fzgx_vec3 road_axis = vec3_sub(road_left, road_right);
    const float road_axis_len2 = vec3_dot(road_axis, road_axis);
    if (road_axis_len2 <= 1.0e-9f) {
        return false;
    }

    const float road_axis_len = std::sqrt(road_axis_len2);
    const fzgx_vec3 road_axis_unit = vec3_scale(road_axis, 1.0f / road_axis_len);
    const fzgx_vec3 strip_axis = mat43_basis_x(terrain_transform);
    const fzgx_vec3 strip_axis_unit = vec3_scale(
            strip_axis, 1.0f / std::max(1.0e-6f, vec3_length(strip_axis)));
    const fzgx_vec3 strip_center = mat43_origin(terrain_transform);

    const float center_u = vec3_dot(vec3_sub(strip_center, road_right), road_axis) / road_axis_len2;
    const float center_tx = (2.0f * center_u) - 1.0f;
    const float strip_half_world =
            (2.5f + (0.5f * std::fabs(terrain_scale.x))) *
            std::fabs(vec3_dot(strip_axis_unit, road_axis_unit));
    const float half_tx = (2.0f * strip_half_world) / road_axis_len;

    if (half_tx <= 1.0e-6f) {
        return false;
    }
    *left_tx_out = center_tx - half_tx;
    *right_tx_out = center_tx + half_tx;
    return true;
}

static bool terrain_strip_height_accepts_sample(
        const fzgx_mat43 &terrain_transform,
        const RoadSample *sample) {
    if (sample == nullptr) {
        return false;
    }
    const fzgx_vec3 local = mat43_local_point(terrain_transform, sample->center);
    return (-5.0f < local.y) && (local.y < 10.0f);
}

static bool animation_segment_curve_window_contains(
        const fzgx_track_segment_animation_record *animation_segment,
        float curve_time) {
    float min_time = 1000000000.0f;
    float max_time = 0.0f;
    bool has_curve_window = false;

    if ((animation_segment == nullptr) || (animation_segment->animation_curve_trs == nullptr) ||
        (animation_segment->animation_curve_trs->curves == nullptr)) {
        return true;
    }

    for (uint32_t curve_index = 0u; curve_index < 9u; ++curve_index) {
        if (animation_segment->animation_curve_trs->curve_count <= curve_index) {
            continue;
        }
        const fzgx_animation_curve *curve = &animation_segment->animation_curve_trs->curves[curve_index];
        if ((curve->keyable_count == 0u) || (curve->keyables == nullptr)) {
            continue;
        }
        min_time = std::min(min_time, curve->keyables[0].time);
        max_time = std::max(max_time, curve->keyables[curve->keyable_count - 1u].time);
        has_curve_window = true;
    }
    return !has_curve_window || ((min_time < curve_time) && (curve_time < max_time));
}

static void road_sample_add_terrain_band(
        RoadSample *sample,
        uint32_t flag,
        float left_tx,
        float right_tx) {
    if ((sample == nullptr) || (flag == 0u)) {
        return;
    }
    if (left_tx > right_tx) {
        std::swap(left_tx, right_tx);
    }
    left_tx = std::max(-1.0f, std::min(1.0f, left_tx));
    right_tx = std::max(-1.0f, std::min(1.0f, right_tx));
    if (right_tx < left_tx) {
        return;
    }

    const float merge_epsilon = 1.0e-4f;
    for (uint32_t i = 0u; i < sample->terrain_count; ++i) {
        if ((sample->terrain_flags[i] == flag) &&
            (left_tx <= sample->terrain_right_tx[i] + merge_epsilon) &&
            (sample->terrain_left_tx[i] <= right_tx + merge_epsilon)) {
            sample->terrain_left_tx[i] = std::min(sample->terrain_left_tx[i], left_tx);
            sample->terrain_right_tx[i] = std::max(sample->terrain_right_tx[i], right_tx);
            return;
        }
    }
    if (sample->terrain_count >= ROAD_SAMPLE_MAX_TERRAIN_BANDS) {
        return;
    }
    const uint32_t index = sample->terrain_count++;
    sample->terrain_flags[index] = flag;
    sample->terrain_left_tx[index] = left_tx;
    sample->terrain_right_tx[index] = right_tx;
}

static fzgx_status collect_terrain_for_sample_recursive(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        const fzgx_track_segment_record *track_segment,
        float curve_time,
        const fzgx_mat43 *parent_transform,
        const fzgx_vec3 *parent_scale,
        RoadSample *sample_out) {
    const fzgx_track_segment_animation_record *animation_segment = nullptr;
    const fzgx_track_segment_record *children = nullptr;
    fzgx_mat43 transform;
    fzgx_vec3 scale;
    uint32_t source_piece_word = 0u;
    uint32_t child_count = 0u;
    fzgx_status status;

    if ((course == nullptr) || (track_segment == nullptr) || (parent_transform == nullptr) ||
        (parent_scale == nullptr) || (sample_out == nullptr)) {
        return FZGX_STATUS_BAD_ARGUMENT;
    }
    status = fzgx_track_segment_build_source_piece_word(track_segment, &source_piece_word);
    if (status != FZGX_STATUS_OK) {
        return status;
    }
    if ((source_piece_word & 0x00000002u) != 0u) {
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
                track_segment, animation_segment, curve_time, &transform, &scale, nullptr);
        if (status != FZGX_STATUS_OK) {
            return status;
        }
    }

    const uint32_t terrain_flags = terrain_flags_for_source(source_piece_word);
    if ((terrain_flags != 0u) &&
        animation_segment_curve_window_contains(animation_segment, curve_time)) {
        float left_tx = 0.0f;
        float right_tx = 0.0f;

        if (terrain_strip_height_accepts_sample(transform, sample_out) &&
            project_terrain_strip_to_road_tx(transform, scale, sample_out, &left_tx, &right_tx)) {
            uint32_t flag = 0x00100000u;
            while (flag >= 0x00020000u) {
                if ((terrain_flags & flag) != 0u) {
                    road_sample_add_terrain_band(sample_out, flag, left_tx, right_tx);
                }
                flag >>= 1u;
            }
        }
    }

    status = fzgx_track_course_get_track_segment_children(
            course, track_segment, &children, &child_count);
    if (status != FZGX_STATUS_OK) {
        return status;
    }
    for (uint32_t child_index = 0u; child_index < child_count; ++child_index) {
        status = collect_terrain_for_sample_recursive(
                course, animation_course, &children[child_index], curve_time,
                &transform, &scale, sample_out);
        if (status != FZGX_STATUS_OK) {
            return status;
        }
    }
    return FZGX_STATUS_OK;
}

static fzgx_status sample_renderable_pieces(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        int32_t checkpoint_index,
        float checkpoint_fraction,
        uint32_t sample_sequence,
        std::vector<RoadSample> *samples_out) {
    const fzgx_track_segment_record *root_segment = nullptr;
    fzgx_mat43 identity = identity_mat43();
    fzgx_vec3 unit_scale = {1.0f, 1.0f, 1.0f};
    std::vector<PieceSample> pieces;
    float curve_time = 0.0f;
    fzgx_status status;

    if ((course == nullptr) || (samples_out == nullptr) || (checkpoint_index < 0)) {
        return FZGX_STATUS_BAD_ARGUMENT;
    }
    status = fzgx_track_course_compute_curve_time_for_checkpoint_fraction(
            course, (uint32_t)checkpoint_index, checkpoint_fraction, &curve_time);
    if (status != FZGX_STATUS_OK) {
        return status;
    }
    status = fzgx_track_course_get_root_segment_for_track_node(
            course, (uint32_t)checkpoint_index, &root_segment);
    if (status != FZGX_STATUS_OK) {
        return status;
    }
    status = collect_piece_samples_recursive(
            course, animation_course, root_segment, curve_time, &identity, &unit_scale, &pieces);
    if (status != FZGX_STATUS_OK) {
        return status;
    }

    for (size_t piece_index = 0; piece_index < pieces.size(); ++piece_index) {
        const PieceSample &piece = pieces[piece_index];
        fzgx_track_frame_surface_tail tail = {};
        RoadSample sample = {};

        status = fzgx_track_build_frame_surface_tail(
                course, animation_course, piece.segment, piece.animation_segment, curve_time,
                &piece.transform, &piece.scale, piece.source_piece_word, &tail);
        if (status != FZGX_STATUS_OK) {
            continue;
        }

        sample.distance = 0.0;
        sample.sample_sequence = sample_sequence;
        sample.checkpoint_index = checkpoint_index;
        sample.checkpoint_fraction = checkpoint_fraction;
        sample.source_piece_word = piece.source_piece_word;
        sample.source_segment_address = piece.segment->address;
        sample.root_segment_address = root_segment->address;
        sample.branch_index = piece.branch_index;
        sample.frame_index = (uint32_t)piece_index;
        sample.track_scl_x = tail.track_scl_x;
        sample.track_scl_y = tail.track_scl_y;
        sample.track_width_or_radius = tail.track_width_or_radius;
        sample.track_hcylin = tail.track_hcylin;
        if ((piece.source_piece_word & 0x00200000u) != 0u) {
            sample.track_width_or_radius = piece.scale.x;
            sample.track_hcylin = 1.0f;
        }
        sample.track_anchor = mat43_origin(piece.transform);
        sample.track_follow_offset = tail.track_follow_offset;
        sample.track_current_scale = piece.scale;
        sample.basis_right = vec3_scale(
                mat43_basis_x(piece.transform),
                1.0f / std::max(1.0e-6f, vec3_length(mat43_basis_x(piece.transform))));
        sample.basis_up = vec3_scale(
                mat43_basis_y(piece.transform),
                1.0f / std::max(1.0e-6f, vec3_length(mat43_basis_y(piece.transform))));
        sample.basis_forward = vec3_scale(
                mat43_basis_z(piece.transform),
                -1.0f / std::max(1.0e-6f, vec3_length(mat43_basis_z(piece.transform))));
        if ((piece.source_piece_word & 0x00200000u) != 0u) {
            sample.center = sample.track_anchor;
        } else {
            sample.center = sample.track_anchor;
        }
        {
            const float cross_extent = sample_cross_extent(piece.source_piece_word, tail.track_width_or_radius);
            sample.left = vec3_add(sample.center, vec3_scale(sample.basis_right, cross_extent));
            sample.right = vec3_sub(sample.center, vec3_scale(sample.basis_right, cross_extent));
        }
        sample.rounded_height = source_is_capsule(piece.source_piece_word) ? 0.0f : -1.0f;
        sample.rail_height_left = piece.segment->rail_height_left;
        sample.rail_height_right = piece.segment->rail_height_right;
        sample.shape = shape_for_source(piece.source_piece_word);
        copy_modulation_profile(&sample.modulation, piece);

        status = collect_terrain_for_sample_recursive(
                course, animation_course, root_segment, curve_time, &identity, &unit_scale, &sample);
        if (status != FZGX_STATUS_OK) {
            return status;
        }
        samples_out->push_back(sample);
    }
    return samples_out->empty() ? FZGX_STATUS_OUT_OF_RANGE : FZGX_STATUS_OK;
}

static void print_vec3_json(FILE *out, const fzgx_vec3 &v) {
    std::fprintf(out, "[%.9g,%.9g,%.9g]", (double)v.x, (double)v.y, (double)v.z);
}

static void print_sample_json(FILE *out, const RoadSample &s, const char *indent, bool trailing_comma) {
    std::fprintf(out,
            "%s{\"distance\":%.9g,\"sample_sequence\":%u,\"checkpoint_index\":%d,"
            "\"checkpoint_fraction\":%.9g,\"authored_gap_before\":%s,"
            "\"stream_break_before\":%s,",
            indent, s.distance, s.sample_sequence, s.checkpoint_index,
            (double)s.checkpoint_fraction,
            s.authored_gap_before ? "true" : "false",
            s.stream_break_before ? "true" : "false");
    std::fprintf(out,
            "\"source_piece_word\":%u,\"source_segment_address\":%u,\"root_segment_address\":%u,"
            "\"branch_index\":%d,\"frame_index\":%u,"
            "\"track_scl_x\":%.9g,\"track_scl_y\":%.9g,"
            "\"track_width_or_radius\":%.9g,\"track_hcylin\":%.9g,"
            "\"rounded_height\":%.9g,"
            "\"rail_height_left\":%.9g,\"rail_height_right\":%.9g,"
            "\"shape\":\"%s\",",
            s.source_piece_word, s.source_segment_address, s.root_segment_address,
            s.branch_index, s.frame_index,
            (double)s.track_scl_x, (double)s.track_scl_y,
            (double)s.track_width_or_radius, (double)s.track_hcylin,
            (double)s.rounded_height,
            (double)s.rail_height_left, (double)s.rail_height_right, s.shape);
    std::fprintf(out, "\"track_anchor\":");
    print_vec3_json(out, s.track_anchor);
    std::fprintf(out, ",\"track_follow_offset\":");
    print_vec3_json(out, s.track_follow_offset);
    std::fprintf(out, ",\"track_current_scale\":");
    print_vec3_json(out, s.track_current_scale);
    std::fprintf(out, ",\"center\":");
    print_vec3_json(out, s.center);
    std::fprintf(out, ",\"left\":");
    print_vec3_json(out, s.left);
    std::fprintf(out, ",\"right\":");
    print_vec3_json(out, s.right);
    std::fprintf(out, ",\"basis_right\":");
    print_vec3_json(out, s.basis_right);
    std::fprintf(out, ",\"basis_up\":");
    print_vec3_json(out, s.basis_up);
    std::fprintf(out, ",\"basis_forward\":");
    print_vec3_json(out, s.basis_forward);
    std::fprintf(out, ",\"terrain\":[");
    for (uint32_t terrain_index = 0u; terrain_index < s.terrain_count; ++terrain_index) {
        const uint32_t flag = s.terrain_flags[terrain_index];
        std::fprintf(out,
                "{\"type\":\"%s\",\"flag\":%u,\"left_tx\":%.9g,\"right_tx\":%.9g}%s",
                terrain_type_for_flag(flag), flag, (double)s.terrain_left_tx[terrain_index],
                (double)s.terrain_right_tx[terrain_index],
                (terrain_index + 1u < s.terrain_count) ? "," : "");
    }
    std::fprintf(out, "]}%s\n", trailing_comma ? "," : "");
}

static void print_modulation_profile_json(FILE *out, const ModulationProfile &profile) {
    if (!profile.has_profile) {
        std::fprintf(out, "null");
        return;
    }
    std::fprintf(out, "{\"fallback_height\":%.9g,\"keys\":[", (double)profile.fallback_height);
    for (size_t key_index = 0; key_index < profile.keys.size(); ++key_index) {
        const CurveKey &key = profile.keys[key_index];
        std::fprintf(out,
                "{\"interpolation_mode\":%u,\"time\":%.9g,\"value\":%.9g,"
                "\"tangent_in\":%.9g,\"tangent_out\":%.9g}%s",
                key.interpolation_mode, (double)key.time, (double)key.value,
                (double)key.tangent_in, (double)key.tangent_out,
                (key_index + 1u < profile.keys.size()) ? "," : "");
    }
    std::fprintf(out, "]}");
}

static void usage(const char *exe) {
    std::fprintf(stderr,
            "usage: %s COLI_COURSExx[.lz] OUT.json [--step N] [--course-id N]\n",
            exe);
}

int main(int argc, char **argv) {
    const char *input_path = nullptr;
    const char *output_path = nullptr;
    double step = 5.0;
    uint32_t authored_track_id = 0u;
    bool explicit_course_id = false;
    bool dump_segments = false;

    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }
    input_path = argv[1];
    output_path = argv[2];
    for (int i = 3; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--step") == 0) && (i + 1 < argc)) {
            step = std::atof(argv[++i]);
        } else if ((std::strcmp(argv[i], "--course-id") == 0) && (i + 1 < argc)) {
            authored_track_id = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
            explicit_course_id = true;
        } else if (std::strcmp(argv[i], "--dump-segments") == 0) {
            dump_segments = true;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (step <= 0.0) {
        step = 5.0;
    }
    if (!explicit_course_id) {
        authored_track_id = parse_course_id_from_path(input_path);
    }

    const std::vector<uint8_t> bytes = read_file_bytes(input_path);
    if (bytes.empty()) {
        std::fprintf(stderr, "failed to read input: %s\n", input_path);
        return 1;
    }

    fzgx_track_manifest manifest = {};
    fzgx_owned_track_course_content course = {};
    fzgx_owned_track_course_animation_content animation = {};
    fzgx_track_course_animation_content *animation_ptr = nullptr;
    fzgx_status status = fzgx_content_build_track_manifest_from_bytes(
            bytes.data(), (uint32_t)bytes.size(), authored_track_id, &manifest);
    if (status != FZGX_STATUS_OK) {
        std::fprintf(stderr, "manifest load failed: %d\n", (int)status);
        return 1;
    }
    status = fzgx_content_load_track_course_content_from_bytes(
            bytes.data(), (uint32_t)bytes.size(), authored_track_id, &course);
    if (status != FZGX_STATUS_OK) {
        std::fprintf(stderr, "track course load failed: %d\n", (int)status);
        return 1;
    }
    if (dump_segments) {
        for (uint32_t segment_index = 0u; segment_index < course.course.track_segment_count; ++segment_index) {
            const fzgx_track_segment_record *segment = &course.course.track_segments[segment_index];
            uint32_t source_piece_word = 0u;
            if (fzgx_track_segment_build_source_piece_word(segment, &source_piece_word) == FZGX_STATUS_OK) {
                std::fprintf(stderr,
                        "segment|index=%u|addr=%u|source=0x%08x|terrain=0x%08x|type=0x%02x|embed=0x%02x|"
                        "perim=0x%02x|pipe=0x%02x|children=%u|branch=%d|rail_l=%.6g|rail_r=%.6g\n",
                        segment_index, segment->address, source_piece_word,
                        source_piece_word & 0x001e0000u, segment->segment_type,
                        segment->embedded_property_type, segment->perimeter_flags,
                        segment->pipe_cylinder_flags, segment->children_count,
                        segment->branch_index, (double)segment->rail_height_left,
                        (double)segment->rail_height_right);
            }
        }
    }
    status = fzgx_content_load_track_course_animation_content_from_bytes(
            bytes.data(), (uint32_t)bytes.size(), authored_track_id, &animation);
    if (status == FZGX_STATUS_OK) {
        animation_ptr = &animation.course;
    } else {
        std::memset(&animation, 0, sizeof(animation));
    }

    const double total_distance = std::max(0.0f, course.course.track_total_distance);
    std::vector<double> sample_distances;
    const uint32_t uniform_sample_count = std::max((uint32_t)std::ceil(total_distance / step) + 1u, 2u);
    sample_distances.reserve(uniform_sample_count + 256u);
    for (uint32_t i = 0; i < uniform_sample_count; ++i) {
        sample_distances.push_back((i + 1u >= uniform_sample_count) ? total_distance : step * (double)i);
    }
    append_source_key_distances(&course.course, animation_ptr, &sample_distances);
    std::sort(sample_distances.begin(), sample_distances.end());
    sample_distances.erase(
            std::unique(
                    sample_distances.begin(), sample_distances.end(),
                    [](double a, double b) { return std::fabs(a - b) < 1.0e-4; }),
            sample_distances.end());
    std::vector<RoadStream> road_streams;
    std::vector<int32_t> last_seen_sequence;

    int32_t seed_checkpoint = 0;
    for (size_t i = 0; i < sample_distances.size(); ++i) {
        double distance = sample_distances[i];
        int32_t checkpoint_index = 0;
        float checkpoint_fraction = 0.0f;
        std::vector<RoadSample> frame_samples;

        if ((manifest.circuit_type != FZGX_CIRCUIT_TYPE_OPEN) && (distance >= total_distance)) {
            distance = 0.0;
        }
        status = fzgx_track_course_find_checkpoint_for_track_distance(
                &course.course, distance, seed_checkpoint, &checkpoint_index, &checkpoint_fraction);
        if (status != FZGX_STATUS_OK) {
            continue;
        }
        seed_checkpoint = checkpoint_index;
        status = sample_renderable_pieces(
                &course.course, animation_ptr, checkpoint_index, checkpoint_fraction,
                (uint32_t)i, &frame_samples);
        if (status != FZGX_STATUS_OK) {
            continue;
        }
        for (size_t frame_sample_index = 0; frame_sample_index < frame_samples.size(); ++frame_sample_index) {
            RoadSample &sample = frame_samples[frame_sample_index];
            sample.distance = sample_distances[i];
            const int stream_index = find_road_stream_index(&road_streams, sample.source_segment_address);
            if (stream_index < 0) {
                continue;
            }
            if ((size_t)stream_index >= last_seen_sequence.size()) {
                last_seen_sequence.resize((size_t)stream_index + 1u, -1);
            }
            RoadStream &stream = road_streams[(size_t)stream_index];
            if (!stream.modulation.has_profile && sample.modulation.has_profile) {
                stream.modulation = sample.modulation;
            }
            if (!stream.samples.empty()) {
                const RoadSample &previous = stream.samples.back();
                sample.authored_gap_before =
                        authored_gap_between_samples(&course.course, previous, sample);
                sample.stream_break_before =
                        (last_seen_sequence[(size_t)stream_index] + 1) != (int32_t)i;
            }
            sample.frame_index = (uint32_t)stream_index;
            last_seen_sequence[(size_t)stream_index] = (int32_t)i;
            stream.samples.push_back(sample);
        }
    }

    std::vector<RoadSample> samples;
    if (!road_streams.empty()) {
        samples = road_streams[0].samples;
    }

    FILE *out = nullptr;
    if (std::strcmp(output_path, "-") == 0) {
        out = stdout;
    } else {
        out = std::fopen(output_path, "wb");
    }
    if (out == nullptr) {
        std::fprintf(stderr, "failed to open output: %s\n", output_path);
        fzgx_content_release_track_course_content(&course);
        fzgx_content_release_track_course_animation_content(&animation);
        return 1;
    }

    std::fprintf(out, "{\n");
    std::fprintf(out, "  \"source_path\":\"");
    for (const char *p = input_path; *p != '\0'; ++p) {
        if ((*p == '\\') || (*p == '"')) {
            std::fputc('\\', out);
        }
        std::fputc(*p, out);
    }
    std::fprintf(out, "\",\n");
    std::fprintf(out, "  \"authored_track_id\":%u,\n", authored_track_id);
    std::fprintf(out, "  \"track_total_distance\":%.9g,\n", total_distance);
    std::fprintf(out, "  \"circuit_type\":%u,\n", manifest.circuit_type);
    std::fprintf(out, "  \"checkpoint_count\":%u,\n", manifest.checkpoint_count);
    std::fprintf(out, "  \"checkpoint_variant_count\":%u,\n", manifest.checkpoint_variant_count);
    std::fprintf(out, "  \"sample_step\":%.9g,\n", step);
    std::fprintf(out, "  \"roads\":[\n");
    bool wrote_road = false;
    for (size_t road_index = 0; road_index < road_streams.size(); ++road_index) {
        const std::vector<RoadSample> &road = road_streams[road_index].samples;
        if (road.empty()) {
            continue;
        }
        if (wrote_road) {
            std::fprintf(out, ",\n");
        }
        wrote_road = true;
        std::fprintf(out, "    {\"stream_index\":%zu,\"source_segment_address\":%u,\"modulation_profile\":",
                road_index, road_streams[road_index].source_segment_address);
        print_modulation_profile_json(out, road_streams[road_index].modulation);
        std::fprintf(out, ",\"samples\":[\n");
        for (size_t i = 0; i < road.size(); ++i) {
            print_sample_json(out, road[i], "      ", i + 1u < road.size());
        }
        std::fprintf(out, "    ]}");
    }
    std::fprintf(out, "\n  ],\n");
    std::fprintf(out, "  \"samples\":[\n");
    for (size_t i = 0; i < samples.size(); ++i) {
        print_sample_json(out, samples[i], "    ", i + 1u < samples.size());
    }
    std::fprintf(out, "  ]\n");
    std::fprintf(out, "}\n");
    if (out != stdout) {
        std::fclose(out);
    }

    fzgx_content_release_track_course_content(&course);
    fzgx_content_release_track_course_animation_content(&animation);
    return samples.empty() ? 1 : 0;
}
