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
    float curve_time;
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

struct SampleRequest {
    double distance;
    int32_t checkpoint_index;
    float checkpoint_fraction;
};

static fzgx_vec3 mat43_basis_x(const fzgx_mat43 &m);
static fzgx_vec3 mat43_basis_y(const fzgx_mat43 &m);
static fzgx_vec3 mat43_basis_z(const fzgx_mat43 &m);
static fzgx_vec3 mat43_origin(const fzgx_mat43 &m);

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
static bool animation_segment_curve_window_active(
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

static bool same_sample_request(const SampleRequest &a, const SampleRequest &b) {
    return (a.checkpoint_index == b.checkpoint_index) &&
           (std::fabs(a.checkpoint_fraction - b.checkpoint_fraction) < 1.0e-5f) &&
           (std::fabs(a.distance - b.distance) < 1.0e-4);
}

static bool same_stream_sample(const RoadSample &a, const RoadSample &b) {
    return (a.source_segment_address == b.source_segment_address) &&
           (a.branch_index == b.branch_index) &&
           (std::fabs(a.distance - b.distance) < 1.0e-3) &&
           (std::fabs(a.curve_time - b.curve_time) < 1.0e-5f);
}

static void append_sample_request(
        const fzgx_track_course_content *course,
        int32_t checkpoint_index,
        float checkpoint_fraction,
        double distance,
        std::vector<SampleRequest> *requests_inout) {
    if ((course == nullptr) || (requests_inout == nullptr) ||
        (checkpoint_index < 0) || ((uint32_t)checkpoint_index >= course->track_node_count)) {
        return;
    }
    SampleRequest request = {};
    request.distance = std::max(0.0, std::min((double)course->track_total_distance, distance));
    request.checkpoint_index = checkpoint_index;
    request.checkpoint_fraction = std::max(0.0f, std::min(1.0f, checkpoint_fraction));
    requests_inout->push_back(request);
}

static const RoadSample *find_previous_global_sample(
        const std::vector<RoadStream> &streams,
        uint32_t sample_sequence) {
    const RoadSample *best = nullptr;
    for (size_t stream_index = 0; stream_index < streams.size(); ++stream_index) {
        const std::vector<RoadSample> &samples = streams[stream_index].samples;
        for (size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
            const RoadSample &sample = samples[sample_index];
            if (sample.sample_sequence >= sample_sequence) {
                continue;
            }
            if ((best == nullptr) || (best->sample_sequence < sample.sample_sequence)) {
                best = &sample;
            }
        }
    }
    return best;
}

static void mark_stream_start_gaps(
        const fzgx_track_course_content *course,
        std::vector<RoadStream> *streams) {
    if ((course == nullptr) || (streams == nullptr)) {
        return;
    }
    for (size_t stream_index = 0; stream_index < streams->size(); ++stream_index) {
        RoadStream &stream = (*streams)[stream_index];
        if (stream.samples.empty()) {
            continue;
        }
        RoadSample &first = stream.samples.front();
        if (first.sample_sequence == 0u) {
            continue;
        }
        const RoadSample *previous = find_previous_global_sample(*streams, first.sample_sequence);
        if (previous == nullptr) {
            continue;
        }
        first.authored_gap_before = authored_gap_between_samples(course, *previous, first);
        first.stream_break_before = (previous->sample_sequence + 1u) != first.sample_sequence;
    }
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

static fzgx_vec3 mat43_transform_point(const fzgx_mat43 &m, const fzgx_vec3 &p) {
    return {
            m.origin_x + m.basis_x_x * p.x + m.basis_y_x * p.y + m.basis_z_x * p.z,
            m.origin_y + m.basis_x_y * p.x + m.basis_y_y * p.y + m.basis_z_y * p.z,
            m.origin_z + m.basis_x_z * p.x + m.basis_y_z * p.y + m.basis_z_z * p.z,
    };
}

static void mat43_set_origin(fzgx_mat43 *m, fzgx_vec3 p) {
    m->origin_x = p.x;
    m->origin_y = p.y;
    m->origin_z = p.z;
}

static void mat43_translate_local(fzgx_mat43 *m, fzgx_vec3 p) {
    m->origin_x += m->basis_x_x * p.x + m->basis_y_x * p.y + m->basis_z_x * p.z;
    m->origin_y += m->basis_x_y * p.x + m->basis_y_y * p.y + m->basis_z_y * p.z;
    m->origin_z += m->basis_x_z * p.x + m->basis_y_z * p.y + m->basis_z_z * p.z;
}

static double angle16_radians(uint16_t angle) {
    const double pi = 3.14159265358979323846264338327950288;
    return ((double)(int16_t)angle) * (2.0 * pi / 65536.0);
}

static void mat43_rotate_x_right(fzgx_mat43 *m, uint16_t angle) {
    const double a = angle16_radians(angle);
    const float c = (float)std::cos(a);
    const float s = (float)std::sin(a);
    const fzgx_vec3 y = mat43_basis_y(*m);
    const fzgx_vec3 z = mat43_basis_z(*m);
    fzgx_mat43_set_basis_y_exact(m, vec3_add(vec3_scale(y, c), vec3_scale(z, s)));
    fzgx_mat43_set_basis_z_exact(m, vec3_add(vec3_scale(y, -s), vec3_scale(z, c)));
}

static void mat43_rotate_y_right(fzgx_mat43 *m, uint16_t angle) {
    const double a = angle16_radians(angle);
    const float c = (float)std::cos(a);
    const float s = (float)std::sin(a);
    const fzgx_vec3 x = mat43_basis_x(*m);
    const fzgx_vec3 z = mat43_basis_z(*m);
    fzgx_mat43_set_basis_x_exact(m, vec3_add(vec3_scale(x, c), vec3_scale(z, -s)));
    fzgx_mat43_set_basis_z_exact(m, vec3_add(vec3_scale(x, s), vec3_scale(z, c)));
}

static void mat43_rotate_z_right(fzgx_mat43 *m, uint16_t angle) {
    const double a = angle16_radians(angle);
    const float c = (float)std::cos(a);
    const float s = (float)std::sin(a);
    const fzgx_vec3 x = mat43_basis_x(*m);
    const fzgx_vec3 y = mat43_basis_y(*m);
    fzgx_mat43_set_basis_x_exact(m, vec3_add(vec3_scale(x, c), vec3_scale(y, s)));
    fzgx_mat43_set_basis_y_exact(m, vec3_add(vec3_scale(x, -s), vec3_scale(y, c)));
}

static fzgx_mat43 track_mesh_chunk_rest_transform(const fzgx_owned_track_mesh_chunk &chunk) {
    fzgx_mat43 m = identity_mat43();
    mat43_set_origin(&m, chunk.unk_vec3_0x0);
    if (chunk.rotation_z_angle16 != 0u) {
        mat43_rotate_z_right(&m, chunk.rotation_z_angle16);
    }
    if (chunk.rotation_y_angle16 != 0u) {
        mat43_rotate_y_right(&m, chunk.rotation_y_angle16);
    }
    if (chunk.rotation_y_angle16 != 0u) {
        mat43_rotate_y_right(&m, (uint16_t)(-(int32_t)(int16_t)chunk.rotation_y_angle16));
    }
    if (chunk.rotation_z_angle16 != 0u) {
        mat43_rotate_z_right(&m, (uint16_t)(-(int32_t)(int16_t)chunk.rotation_z_angle16));
    }
    mat43_translate_local(&m, vec3_scale(chunk.unk_vec3_0x0, -1.0f));
    return m;
}

static fzgx_mat43 transform_trxs_matrix(const fzgx_transform_trxs_record &trxs) {
    fzgx_mat43 m = identity_mat43();
    mat43_set_origin(&m, trxs.position);
    if (trxs.rotation_z_angle16 != 0u) {
        mat43_rotate_z_right(&m, trxs.rotation_z_angle16);
    }
    if (trxs.rotation_y_angle16 != 0u) {
        mat43_rotate_y_right(&m, trxs.rotation_y_angle16);
    }
    if (trxs.rotation_x_angle16 != 0u) {
        mat43_rotate_x_right(&m, trxs.rotation_x_angle16);
    }
    fzgx_mat43_set_basis_x_exact(&m, vec3_scale(mat43_basis_x(m), trxs.scale.x));
    fzgx_mat43_set_basis_y_exact(&m, vec3_scale(mat43_basis_y(m), trxs.scale.y));
    fzgx_mat43_set_basis_z_exact(&m, vec3_scale(mat43_basis_z(m), trxs.scale.z));
    return m;
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

static bool checkpoint_distance_for_curve_time(
        const fzgx_checkpoint_record *checkpoint,
        float curve_time,
        double *distance_out,
        float *checkpoint_fraction_out) {
    if ((checkpoint == nullptr) || (distance_out == nullptr)) {
        return false;
    }
    const float start_time = checkpoint->curve_time_start;
    const float end_time = checkpoint->curve_time_end;
    const float min_time = std::min(start_time, end_time);
    const float max_time = std::max(start_time, end_time);
    if ((curve_time + 1.0e-5f < min_time) || (max_time + 1.0e-5f < curve_time)) {
        return false;
    }
    const float denom = end_time - start_time;
    const double fraction =
            (std::fabs(denom) <= 1.0e-9f) ? 0.0 : (double)((curve_time - start_time) / denom);
    *distance_out =
            (double)checkpoint->start_distance +
            (fraction * (double)(checkpoint->end_distance - checkpoint->start_distance));
    if (checkpoint_fraction_out != nullptr) {
        *checkpoint_fraction_out = (float)std::max(0.0, std::min(1.0, fraction));
    }
    return true;
}

static bool track_segment_tree_contains_address(
        const fzgx_track_course_content *course,
        const fzgx_track_segment_record *track_segment,
        uint32_t segment_address) {
    const fzgx_track_segment_record *children = nullptr;
    uint32_t child_count = 0u;
    fzgx_status status;

    if ((course == nullptr) || (track_segment == nullptr)) {
        return false;
    }
    if (track_segment->address == segment_address) {
        return true;
    }
    status = fzgx_track_course_get_track_segment_children(course, track_segment, &children, &child_count);
    if (status != FZGX_STATUS_OK) {
        return false;
    }
    for (uint32_t child_index = 0u; child_index < child_count; ++child_index) {
        if (track_segment_tree_contains_address(course, &children[child_index], segment_address)) {
            return true;
        }
    }
    return false;
}

static void append_requests_for_segment_curve_time(
        const fzgx_track_course_content *course,
        uint32_t segment_address,
        float curve_time,
        std::vector<SampleRequest> *requests_inout) {
    if ((course == nullptr) || (requests_inout == nullptr) ||
        (course->track_nodes == nullptr) || (course->checkpoints == nullptr)) {
        return;
    }
    for (uint32_t node_index = 0u; node_index < course->track_node_count; ++node_index) {
        const fzgx_track_node_record *node = &course->track_nodes[node_index];
        const fzgx_track_segment_record *root_segment = nullptr;
        double distance = 0.0;
        fzgx_status status = fzgx_track_course_find_track_segment_by_address(
                course, node->root_segment_address, &root_segment);
        if ((status != FZGX_STATUS_OK) ||
            !track_segment_tree_contains_address(course, root_segment, segment_address)) {
            continue;
        }
        if ((node->checkpoint_offset >= course->checkpoint_record_count) ||
            (node->checkpoint_offset + node->checkpoint_count > course->checkpoint_record_count)) {
            continue;
        }
        for (uint32_t checkpoint_index = 0u; checkpoint_index < node->checkpoint_count; ++checkpoint_index) {
            const fzgx_checkpoint_record *checkpoint =
                    &course->checkpoints[node->checkpoint_offset + checkpoint_index];
            float checkpoint_fraction = 0.0f;
            if (checkpoint_distance_for_curve_time(checkpoint, curve_time, &distance, &checkpoint_fraction)) {
                append_sample_request(
                        course, (int32_t)node_index, checkpoint_fraction, distance, requests_inout);
            }
        }
    }
}

static void append_checkpoint_boundary_requests(
        const fzgx_track_course_content *course,
        std::vector<SampleRequest> *requests_inout) {
    if ((course == nullptr) || (requests_inout == nullptr) ||
        (course->track_nodes == nullptr) || (course->checkpoints == nullptr)) {
        return;
    }
    for (uint32_t node_index = 0u; node_index < course->track_node_count; ++node_index) {
        const fzgx_track_node_record *node = &course->track_nodes[node_index];
        if (node->checkpoint_offset >= course->checkpoint_record_count) {
            continue;
        }
        const fzgx_checkpoint_record *checkpoint = &course->checkpoints[node->checkpoint_offset];
        append_sample_request(course, (int32_t)node_index, 0.0f, checkpoint->start_distance, requests_inout);
        append_sample_request(course, (int32_t)node_index, 1.0f, checkpoint->end_distance, requests_inout);
    }
}

static void dump_track_nodes(
        const fzgx_track_course_content *course,
        FILE *out) {
    if ((course == nullptr) || (out == nullptr) ||
        (course->track_nodes == nullptr) || (course->track_segments == nullptr)) {
        return;
    }
    for (uint32_t node_index = 0u; node_index < course->track_node_count; ++node_index) {
        const fzgx_track_node_record *node = &course->track_nodes[node_index];
        const fzgx_track_segment_record *root_segment = nullptr;
        uint32_t root_source = 0u;
        uint32_t accumulated_flags = 0u;
        fzgx_status root_status = fzgx_track_course_find_track_segment_by_address(
                course, node->root_segment_address, &root_segment);
        if (root_status == FZGX_STATUS_OK) {
            (void)fzgx_track_segment_build_source_piece_word(root_segment, &root_source);
            (void)fzgx_track_course_accumulate_track_segment_flags_recursive(
                    course, root_segment, &accumulated_flags);
        }
        std::fprintf(out,
                "node|index=%u|checkpoint_offset=%u|checkpoint_count=%u|"
                "checkpoint_addr=%u|root_addr=%u|root_source=0x%08x|"
                "flags=0x%08x|terrain=0x%08x|render=0x%08x|status=%d\n",
                node_index, node->checkpoint_offset, node->checkpoint_count,
                node->checkpoint_address, node->root_segment_address, root_source,
                accumulated_flags, accumulated_flags & 0x001e0000u,
                accumulated_flags & 0x03e00000u, (int)root_status);
    }
}

static void append_source_key_requests(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        std::vector<SampleRequest> *requests_inout) {
    if ((course == nullptr) || (animation_course == nullptr) || (requests_inout == nullptr)) {
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
                append_requests_for_segment_curve_time(
                        course, segment->address, curve->keyables[key_index].time, requests_inout);
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

static bool animation_segment_curve_window_active(
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
    return !has_curve_window || ((min_time - 1.0e-5f <= curve_time) && (curve_time <= max_time + 1.0e-5f));
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

    bool has_active_piece = false;
    for (size_t piece_index = 0; piece_index < pieces.size(); ++piece_index) {
        const PieceSample &piece = pieces[piece_index];
        if (animation_segment_curve_window_active(piece.animation_segment, curve_time)) {
            has_active_piece = true;
            break;
        }
    }

    for (size_t piece_index = 0; piece_index < pieces.size(); ++piece_index) {
        const PieceSample &piece = pieces[piece_index];
        fzgx_track_frame_surface_tail tail = {};
        RoadSample sample = {};

        if (has_active_piece && !animation_segment_curve_window_active(piece.animation_segment, curve_time)) {
            continue;
        }

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
        sample.curve_time = curve_time;
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

static void sort_unique_sample_requests(std::vector<SampleRequest> *requests) {
    if (requests == nullptr) {
        return;
    }
    std::sort(requests->begin(), requests->end(),
            [](const SampleRequest &a, const SampleRequest &b) {
                if (std::fabs(a.distance - b.distance) >= 1.0e-4) {
                    return a.distance < b.distance;
                }
                if (a.checkpoint_index != b.checkpoint_index) {
                    return a.checkpoint_index < b.checkpoint_index;
                }
                return a.checkpoint_fraction < b.checkpoint_fraction;
            });
    requests->erase(
            std::unique(
                    requests->begin(), requests->end(),
                    [](const SampleRequest &a, const SampleRequest &b) { return same_sample_request(a, b); }),
            requests->end());
}

static bool sample_request_center(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        const SampleRequest &request,
        fzgx_vec3 *center_out) {
    std::vector<RoadSample> samples;
    fzgx_status status;

    if ((course == nullptr) || (center_out == nullptr)) {
        return false;
    }
    status = sample_renderable_pieces(
            course, animation_course, request.checkpoint_index, request.checkpoint_fraction, 0u, &samples);
    if ((status != FZGX_STATUS_OK) || samples.empty()) {
        return false;
    }
    *center_out = samples[0].center;
    return true;
}

static bool request_for_track_distance(
        const fzgx_track_course_content *course,
        double distance,
        int32_t *seed_checkpoint,
        SampleRequest *request_out) {
    fzgx_status status;

    if ((course == nullptr) || (request_out == nullptr)) {
        return false;
    }
    status = fzgx_track_course_find_checkpoint_for_track_distance(
            course, distance, (seed_checkpoint != nullptr) ? *seed_checkpoint : 0,
            &request_out->checkpoint_index, &request_out->checkpoint_fraction);
    if (status != FZGX_STATUS_OK) {
        return false;
    }
    if (seed_checkpoint != nullptr) {
        *seed_checkpoint = request_out->checkpoint_index;
    }
    request_out->distance = std::max(0.0, std::min((double)course->track_total_distance, distance));
    request_out->checkpoint_fraction = std::max(0.0f, std::min(1.0f, request_out->checkpoint_fraction));
    return true;
}

static bool can_subdivide_request_interval(
        const fzgx_track_course_content *course,
        const SampleRequest &a,
        const SampleRequest &b) {
    uint32_t can_traverse = 1u;
    if ((course == nullptr) || (b.distance <= a.distance + 1.0e-4)) {
        return false;
    }
    if (a.checkpoint_index == b.checkpoint_index) {
        return true;
    }
    if (fzgx_track_course_can_traverse_checkpoint_interval_exact(
                course, a.checkpoint_index, b.checkpoint_index, &can_traverse) != FZGX_STATUS_OK) {
        return false;
    }
    return can_traverse != 0u;
}

static void append_centerline_subdivision_requests(
        const fzgx_track_course_content *course,
        const fzgx_track_course_animation_content *animation_course,
        double target_step,
        const std::vector<SampleRequest> &base_requests,
        std::vector<SampleRequest> *requests_inout) {
    const uint32_t max_probe_count = 64u;
    if ((course == nullptr) || (requests_inout == nullptr) || (target_step <= 0.0)) {
        return;
    }
    for (size_t request_index = 1u; request_index < base_requests.size(); ++request_index) {
        const SampleRequest &start = base_requests[request_index - 1u];
        const SampleRequest &end = base_requests[request_index];
        if (!can_subdivide_request_interval(course, start, end)) {
            continue;
        }

        const double distance_delta = end.distance - start.distance;
        const uint32_t probe_count = std::max(
                2u,
                std::min(max_probe_count, (uint32_t)std::ceil(distance_delta / target_step)));
        std::vector<double> probe_distances;
        std::vector<double> cumulative_lengths;
        probe_distances.reserve((size_t)probe_count + 1u);
        cumulative_lengths.reserve((size_t)probe_count + 1u);

        fzgx_vec3 previous_center = {};
        bool have_previous = false;
        bool interval_ok = true;
        double total_length = 0.0;
        int32_t seed_checkpoint = start.checkpoint_index;

        for (uint32_t probe_index = 0u; probe_index <= probe_count; ++probe_index) {
            const double factor = (double)probe_index / (double)probe_count;
            const double distance = start.distance + distance_delta * factor;
            SampleRequest probe = {};
            fzgx_vec3 center = {};
            if (probe_index == 0u) {
                probe = start;
            } else if (probe_index == probe_count) {
                probe = end;
            } else if (!request_for_track_distance(course, distance, &seed_checkpoint, &probe)) {
                interval_ok = false;
                break;
            }
            if (!sample_request_center(course, animation_course, probe, &center)) {
                interval_ok = false;
                break;
            }
            if (have_previous) {
                total_length += (double)vec3_length(vec3_sub(center, previous_center));
            }
            probe_distances.push_back(distance);
            cumulative_lengths.push_back(total_length);
            previous_center = center;
            have_previous = true;
        }
        if (!interval_ok || (total_length <= target_step)) {
            continue;
        }

        const uint32_t segment_count = (uint32_t)std::ceil(total_length / target_step);
        for (uint32_t segment_index = 1u; segment_index < segment_count; ++segment_index) {
            const double target_length =
                    total_length * ((double)segment_index / (double)segment_count);
            size_t probe_index = 1u;
            while ((probe_index < cumulative_lengths.size()) &&
                   (cumulative_lengths[probe_index] < target_length)) {
                ++probe_index;
            }
            if (probe_index >= cumulative_lengths.size()) {
                continue;
            }
            const double length0 = cumulative_lengths[probe_index - 1u];
            const double length1 = cumulative_lengths[probe_index];
            const double denom = length1 - length0;
            const double factor = (std::fabs(denom) <= 1.0e-9) ?
                    0.0 : ((target_length - length0) / denom);
            const double distance =
                    probe_distances[probe_index - 1u] +
                    (probe_distances[probe_index] - probe_distances[probe_index - 1u]) * factor;
            SampleRequest request = {};
            if (request_for_track_distance(course, distance, &seed_checkpoint, &request)) {
                requests_inout->push_back(request);
            }
        }
    }
}

static void print_vec3_json(FILE *out, const fzgx_vec3 &v) {
    std::fprintf(out, "[%.9g,%.9g,%.9g]", (double)v.x, (double)v.y, (double)v.z);
}

static void print_sample_json(FILE *out, const RoadSample &s, const char *indent, bool trailing_comma) {
    std::fprintf(out,
            "%s{\"distance\":%.9g,\"sample_sequence\":%u,\"checkpoint_index\":%d,"
            "\"checkpoint_fraction\":%.9g,\"curve_time\":%.9g,\"authored_gap_before\":%s,"
            "\"stream_break_before\":%s,",
            indent, s.distance, s.sample_sequence, s.checkpoint_index,
            (double)s.checkpoint_fraction, (double)s.curve_time,
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

static const char *mesh_surface_name(uint32_t surface_index) {
    switch (surface_index) {
        case FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE:
            return "driveable";
        case FZGX_STATIC_COLLIDER_SURFACE_RECOVER:
            return "recover";
        case FZGX_STATIC_COLLIDER_SURFACE_WALL:
            return "wall";
        case FZGX_STATIC_COLLIDER_SURFACE_DASH:
            return "dash";
        case FZGX_STATIC_COLLIDER_SURFACE_JUMP:
            return "jump";
        case FZGX_STATIC_COLLIDER_SURFACE_ICE:
            return "ice";
        case FZGX_STATIC_COLLIDER_SURFACE_DIRT:
            return "dirt";
        case FZGX_STATIC_COLLIDER_SURFACE_DAMAGE:
            return "damage";
        case FZGX_STATIC_COLLIDER_SURFACE_OUT_OF_BOUNDS:
            return "out_of_bounds";
        case FZGX_STATIC_COLLIDER_SURFACE_DEATH_GROUND:
            return "death_ground";
        case FZGX_STATIC_COLLIDER_SURFACE_DEATH_1:
            return "death_1";
        case FZGX_STATIC_COLLIDER_SURFACE_DEATH_2:
            return "death_2";
        case FZGX_STATIC_COLLIDER_SURFACE_DEATH_3:
            return "death_3";
        case FZGX_STATIC_COLLIDER_SURFACE_DEATH_4:
            return "death_4";
        default:
            return "unknown";
    }
}

static bool mesh_surface_is_interesting(uint32_t surface_index) {
    return (surface_index >= FZGX_STATIC_COLLIDER_SURFACE_DRIVEABLE) &&
           (surface_index <= FZGX_STATIC_COLLIDER_SURFACE_DEATH_4);
}

static void print_json_string(FILE *out, const char *value) {
    std::fputc('"', out);
    if (value != nullptr) {
        for (const char *p = value; *p != '\0'; ++p) {
            if ((*p == '\\') || (*p == '"')) {
                std::fputc('\\', out);
            }
            if (*p == '\n') {
                std::fputs("\\n", out);
            } else {
                std::fputc(*p, out);
            }
        }
    }
    std::fputc('"', out);
}

static void print_collision_tri(FILE *out, const fzgx_static_collider_triangle_record &tri,
        const fzgx_mat43 *transform) {
    const fzgx_vec3 v0 = transform != nullptr ? mat43_transform_point(*transform, tri.vertex0) : tri.vertex0;
    const fzgx_vec3 v1 = transform != nullptr ? mat43_transform_point(*transform, tri.vertex1) : tri.vertex1;
    const fzgx_vec3 v2 = transform != nullptr ? mat43_transform_point(*transform, tri.vertex2) : tri.vertex2;
    std::fprintf(out, "[");
    print_vec3_json(out, v0);
    std::fprintf(out, ",");
    print_vec3_json(out, v1);
    std::fprintf(out, ",");
    print_vec3_json(out, v2);
    std::fprintf(out, "]");
}

static void print_collision_quad(FILE *out, const fzgx_static_collider_quad_record &quad,
        const fzgx_mat43 *transform) {
    const fzgx_vec3 v0 = transform != nullptr ? mat43_transform_point(*transform, quad.vertex0) : quad.vertex0;
    const fzgx_vec3 v1 = transform != nullptr ? mat43_transform_point(*transform, quad.vertex1) : quad.vertex1;
    const fzgx_vec3 v2 = transform != nullptr ? mat43_transform_point(*transform, quad.vertex2) : quad.vertex2;
    const fzgx_vec3 v3 = transform != nullptr ? mat43_transform_point(*transform, quad.vertex3) : quad.vertex3;
    std::fprintf(out, "[");
    print_vec3_json(out, v0);
    std::fprintf(out, ",");
    print_vec3_json(out, v1);
    std::fprintf(out, ",");
    print_vec3_json(out, v2);
    std::fprintf(out, ",");
    print_vec3_json(out, v3);
    std::fprintf(out, "]");
}

static void print_mat43_json(FILE *out, const fzgx_mat43 &transform) {
    std::fprintf(out, "{\"basis_x\":");
    print_vec3_json(out, mat43_basis_x(transform));
    std::fprintf(out, ",\"basis_y\":");
    print_vec3_json(out, mat43_basis_y(transform));
    std::fprintf(out, ",\"basis_z\":");
    print_vec3_json(out, mat43_basis_z(transform));
    std::fprintf(out, ",\"origin\":");
    print_vec3_json(out, mat43_origin(transform));
    std::fprintf(out, "}");
}

static void print_collision_mesh_object_json(
        FILE *out,
        bool *wrote_any,
        const char *source,
        const char *name,
        uint32_t surface_index,
        uint32_t collider_type,
        const fzgx_static_collider_triangle_record *tris,
        const std::vector<uint8_t> &tri_used,
        const fzgx_static_collider_quad_record *quads,
        const std::vector<uint8_t> &quad_used,
        const fzgx_mat43 *transform,
        const fzgx_mat43 *object_transform) {
    uint32_t tri_count = 0u;
    uint32_t quad_count = 0u;
    for (uint8_t used : tri_used) {
        tri_count += used != 0u ? 1u : 0u;
    }
    for (uint8_t used : quad_used) {
        quad_count += used != 0u ? 1u : 0u;
    }
    if ((tri_count == 0u) && (quad_count == 0u)) {
        return;
    }
    if (*wrote_any) {
        std::fprintf(out, ",\n");
    }
    *wrote_any = true;
    std::fprintf(out, "    {\"source\":");
    print_json_string(out, source);
    std::fprintf(out, ",\"name\":");
    print_json_string(out, name);
    std::fprintf(out, ",\"surface_index\":%u,\"surface\":\"%s\",\"collider_type\":%u,\"tris\":[",
            surface_index, mesh_surface_name(surface_index), collider_type);
    bool wrote_poly = false;
    for (size_t i = 0; i < tri_used.size(); ++i) {
        if (tri_used[i] == 0u) {
            continue;
        }
        if (wrote_poly) {
            std::fprintf(out, ",");
        }
        wrote_poly = true;
        print_collision_tri(out, tris[i], transform);
    }
    std::fprintf(out, "],\"quads\":[");
    wrote_poly = false;
    for (size_t i = 0; i < quad_used.size(); ++i) {
        if (quad_used[i] == 0u) {
            continue;
        }
        if (wrote_poly) {
            std::fprintf(out, ",");
        }
        wrote_poly = true;
        print_collision_quad(out, quads[i], transform);
    }
    std::fprintf(out, "]");
    if (object_transform != nullptr) {
        std::fprintf(out, ",\"object_transform\":");
        print_mat43_json(out, *object_transform);
    }
    if ((transform != nullptr) && (transform != object_transform)) {
        std::fprintf(out, ",\"collider_transform\":");
        print_mat43_json(out, *transform);
    }
    std::fprintf(out, "}");
}

static void mark_static_surface_indices(
        const fzgx_owned_static_collider_course &course,
        uint32_t surface_index,
        std::vector<uint8_t> *tri_used,
        std::vector<uint8_t> *quad_used) {
    if ((tri_used == nullptr) || (quad_used == nullptr)) {
        return;
    }
    for (uint32_t cell_index = 0u; cell_index < FZGX_STATIC_COLLIDER_GRID_CELL_COUNT; ++cell_index) {
        const uint16_t *indices = nullptr;
        uint32_t count = 0u;
        if (fzgx_static_collider_course_get_surface_tri_cell(
                    &course, surface_index, cell_index, &indices, &count) == FZGX_STATUS_OK) {
            for (uint32_t i = 0u; i < count; ++i) {
                if (indices[i] < tri_used->size()) {
                    (*tri_used)[indices[i]] = 1u;
                }
            }
        }
        if (fzgx_static_collider_course_get_surface_quad_cell(
                    &course, surface_index, cell_index, &indices, &count) == FZGX_STATUS_OK) {
            for (uint32_t i = 0u; i < count; ++i) {
                if (indices[i] < quad_used->size()) {
                    (*quad_used)[indices[i]] = 1u;
                }
            }
        }
    }
}

static void mark_track_mesh_class_indices(
        const fzgx_owned_track_mesh_chunk &chunk,
        uint32_t class_index,
        std::vector<uint8_t> *tri_used,
        std::vector<uint8_t> *quad_used) {
    if ((tri_used == nullptr) || (quad_used == nullptr)) {
        return;
    }
    for (uint32_t cell_index = 0u; cell_index < chunk.cell_count; ++cell_index) {
        const uint16_t *indices = nullptr;
        uint32_t count = 0u;
        if (fzgx_track_mesh_chunk_get_tri_cell(&chunk, class_index, cell_index, &indices, &count) ==
            FZGX_STATUS_OK) {
            for (uint32_t i = 0u; i < count; ++i) {
                if (indices[i] < tri_used->size()) {
                    (*tri_used)[indices[i]] = 1u;
                }
            }
        }
        if (fzgx_track_mesh_chunk_get_quad_cell(&chunk, class_index, cell_index, &indices, &count) ==
            FZGX_STATUS_OK) {
            for (uint32_t i = 0u; i < count; ++i) {
                if (indices[i] < quad_used->size()) {
                    (*quad_used)[indices[i]] = 1u;
                }
            }
        }
    }
}

static void print_mesh_collision_json(FILE *out, const uint8_t *bytes, uint32_t size) {
    fzgx_owned_static_collider_course static_course = {};
    fzgx_owned_track_mesh_course track_mesh_course = {};
    fzgx_owned_dynamic_scene_collision_course dynamic_course = {};
    bool have_static = false;
    bool have_track_mesh = false;
    bool have_dynamic = false;
    bool wrote_any = false;

    std::fprintf(out, "  \"mesh_collision\":[\n");

    if (fzgx_content_load_static_collider_course_from_bytes(bytes, size, &static_course) == FZGX_STATUS_OK) {
        have_static = true;
        for (uint32_t surface_index = 0u; surface_index < static_course.surface_count; ++surface_index) {
            if (!mesh_surface_is_interesting(surface_index)) {
                continue;
            }
            std::vector<uint8_t> tri_used(static_course.tri_count, 0u);
            std::vector<uint8_t> quad_used(static_course.quad_count, 0u);
            char name[96];
            std::snprintf(name, sizeof(name), "Static %02u %s", surface_index, mesh_surface_name(surface_index));
            mark_static_surface_indices(static_course, surface_index, &tri_used, &quad_used);
            print_collision_mesh_object_json(
                    out, &wrote_any, "static_collider", name, surface_index, 0u,
                    static_course.tris, tri_used, static_course.quads, quad_used, nullptr, nullptr);
        }
    }

    if (fzgx_content_load_track_mesh_course_from_bytes(bytes, size, &track_mesh_course) == FZGX_STATUS_OK) {
        have_track_mesh = true;
        for (uint32_t chunk_index = 0u; chunk_index < track_mesh_course.chunk_count; ++chunk_index) {
            const fzgx_owned_track_mesh_chunk &chunk = track_mesh_course.chunks[chunk_index];
            const fzgx_mat43 transform = track_mesh_chunk_rest_transform(chunk);
            for (uint32_t class_index = 0u; class_index < chunk.class_count; ++class_index) {
                if (!mesh_surface_is_interesting(class_index)) {
                    continue;
                }
                std::vector<uint8_t> tri_used(chunk.tri_count, 0u);
                std::vector<uint8_t> quad_used(chunk.quad_count, 0u);
                char name[128];
                std::snprintf(name, sizeof(name), "TrackMesh chunk%03u class%02u %s",
                        chunk_index, class_index, mesh_surface_name(class_index));
                mark_track_mesh_class_indices(chunk, class_index, &tri_used, &quad_used);
                print_collision_mesh_object_json(
                        out, &wrote_any, "track_mesh", name, class_index, 0u,
                        chunk.tris, tri_used, chunk.quads, quad_used,
                        chunk_index == 0u ? nullptr : &transform, nullptr);
            }
        }
    }

    if (fzgx_content_load_dynamic_scene_collision_course_from_bytes(bytes, size, &dynamic_course) == FZGX_STATUS_OK) {
        have_dynamic = true;
        for (uint32_t object_index = 0u; object_index < dynamic_course.object_count; ++object_index) {
            const fzgx_owned_dynamic_scene_object_record &object = dynamic_course.objects[object_index];
            if (object.has_collider_mesh == 0u) {
                continue;
            }
            std::vector<uint8_t> tri_used(object.collider_mesh.tri_count, 1u);
            std::vector<uint8_t> quad_used(object.collider_mesh.quad_count, 1u);
            char name[160];
            std::snprintf(name, sizeof(name), "Dynamic %03u %s type0x%08x",
                    object_index, object.primary_lod_name, object.collider_mesh.collider_type);
            fzgx_mat43 object_transform = {};
            if (object.has_transform_matrix) {
                object_transform = object.transform_matrix;
            } else {
                object_transform = transform_trxs_matrix(object.transform);
            }
            const fzgx_mat43 *transform = &object_transform;
            fzgx_mat43 collider_transform = {};
            if (!object.has_transform_matrix && object.has_collision_transform) {
                collider_transform = transform_trxs_matrix(object.collision_transform);
                transform = &collider_transform;
            }
            print_collision_mesh_object_json(
                    out, &wrote_any, "dynamic_scene", name, 0xffffffffu,
                    object.collider_mesh.collider_type, object.collider_mesh.tris, tri_used,
                    object.collider_mesh.quads, quad_used, transform, &object_transform);
        }
        for (uint32_t object_index = 0u; object_index < dynamic_course.unknown_collider_count; ++object_index) {
            const fzgx_owned_unknown_collider_record &object = dynamic_course.unknown_colliders[object_index];
            if (object.has_collider_mesh == 0u) {
                continue;
            }
            std::vector<uint8_t> tri_used(object.collider_mesh.tri_count, 1u);
            std::vector<uint8_t> quad_used(object.collider_mesh.quad_count, 1u);
            char name[160];
            std::snprintf(name, sizeof(name), "UnknownCollider %03u %s type0x%08x",
                    object_index, object.primary_lod_name, object.collider_mesh.collider_type);
            const fzgx_mat43 transform = transform_trxs_matrix(object.transform);
            print_collision_mesh_object_json(
                    out, &wrote_any, "unknown_scene", name, 0xffffffffu,
                    object.collider_mesh.collider_type, object.collider_mesh.tris, tri_used,
                    object.collider_mesh.quads, quad_used, &transform, &transform);
        }
        for (uint32_t object_index = 0u; object_index < dynamic_course.static_scene_object_count; ++object_index) {
            const fzgx_owned_static_scene_object_record &object = dynamic_course.static_scene_objects[object_index];
            if (object.has_collider_mesh == 0u) {
                continue;
            }
            std::vector<uint8_t> tri_used(object.collider_mesh.tri_count, 1u);
            std::vector<uint8_t> quad_used(object.collider_mesh.quad_count, 1u);
            char name[160];
            std::snprintf(name, sizeof(name), "StaticScene %03u %s type0x%08x",
                    object_index, object.primary_lod_name, object.collider_mesh.collider_type);
            print_collision_mesh_object_json(
                    out, &wrote_any, "static_scene", name, 0xffffffffu,
                    object.collider_mesh.collider_type, object.collider_mesh.tris, tri_used,
                    object.collider_mesh.quads, quad_used, nullptr, nullptr);
        }
    }

    std::fprintf(out, "\n  ],\n");
    if (have_static) {
        fzgx_content_release_static_collider_course(&static_course);
    }
    if (have_track_mesh) {
        fzgx_content_release_track_mesh_course(&track_mesh_course);
    }
    if (have_dynamic) {
        fzgx_content_release_dynamic_scene_collision_course(&dynamic_course);
    }
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
    bool dump_nodes = false;

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
        } else if (std::strcmp(argv[i], "--dump-nodes") == 0) {
            dump_nodes = true;
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
    if (dump_nodes) {
        dump_track_nodes(&course.course, stderr);
    }
    status = fzgx_content_load_track_course_animation_content_from_bytes(
            bytes.data(), (uint32_t)bytes.size(), authored_track_id, &animation);
    if (status == FZGX_STATUS_OK) {
        animation_ptr = &animation.course;
    } else {
        std::memset(&animation, 0, sizeof(animation));
    }

    const double total_distance = std::max(0.0f, course.course.track_total_distance);
    std::vector<SampleRequest> base_requests;
    std::vector<SampleRequest> sample_requests;
    base_requests.reserve(course.course.track_node_count * 2u + 512u);
    append_checkpoint_boundary_requests(&course.course, &base_requests);
    append_source_key_requests(&course.course, animation_ptr, &base_requests);
    sort_unique_sample_requests(&base_requests);

    sample_requests = base_requests;
    append_centerline_subdivision_requests(
            &course.course, animation_ptr, step, base_requests, &sample_requests);
    sort_unique_sample_requests(&sample_requests);
    std::vector<RoadStream> road_streams;

    for (size_t i = 0; i < sample_requests.size(); ++i) {
        const SampleRequest &request = sample_requests[i];
        std::vector<RoadSample> frame_samples;

        status = sample_renderable_pieces(
                &course.course, animation_ptr, request.checkpoint_index, request.checkpoint_fraction,
                (uint32_t)i, &frame_samples);
        if (status != FZGX_STATUS_OK) {
            continue;
        }
        for (size_t frame_sample_index = 0; frame_sample_index < frame_samples.size(); ++frame_sample_index) {
            RoadSample &sample = frame_samples[frame_sample_index];
            sample.distance = request.distance;
            const int stream_index = find_road_stream_index(&road_streams, sample.source_segment_address);
            if (stream_index < 0) {
                continue;
            }
            RoadStream &stream = road_streams[(size_t)stream_index];
            if (!stream.modulation.has_profile && sample.modulation.has_profile) {
                stream.modulation = sample.modulation;
            }
            if (!stream.samples.empty() && same_stream_sample(stream.samples.back(), sample)) {
                continue;
            }
            if (!stream.samples.empty()) {
                const RoadSample &previous = stream.samples.back();
                sample.authored_gap_before =
                        authored_gap_between_samples(&course.course, previous, sample);
            }
            sample.frame_index = (uint32_t)stream_index;
            stream.samples.push_back(sample);
        }
    }
    mark_stream_start_gaps(&course.course, &road_streams);

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
    print_mesh_collision_json(out, bytes.data(), (uint32_t)bytes.size());
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
