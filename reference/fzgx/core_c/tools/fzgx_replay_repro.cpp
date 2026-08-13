#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

extern "C" {
#include "fzgx/content.h"
#include "fzgx/replay.h"
#include "fzgx/sim.h"
}

namespace {

struct ReplayFrameRecord {
  fzgx_control_sample control = {};
  fzgx_game_camera_input camera_input = {};
};

struct ReplayFile {
  uint32_t version = 0u;
  uint32_t track_index = 0u;
  std::string track_label = {};
  uint32_t machine_index = 0u;
  std::string machine_label = {};
  uint32_t machine_setting_percent = 0u;
  uint32_t control_profile_kind = 2u;
  uint32_t start_world_frame_index = 0u;
  uint32_t saved_world_frame_index = 0u;
  uint32_t captured_frame_count = 0u;
  fzgx_replay_camera_render_state camera_render_state = {};
  std::string initial_pose_snapshot = {};
  std::string saved_pose_snapshot = {};
  std::vector<ReplayFrameRecord> frames = {};
};

static const char *fzgx_status_name(fzgx_status status) {
  switch (status) {
    case FZGX_STATUS_OK:
      return "OK";
    case FZGX_STATUS_BAD_ARGUMENT:
      return "BAD_ARGUMENT";
    case FZGX_STATUS_OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case FZGX_STATUS_NOT_CONFIGURED:
      return "NOT_CONFIGURED";
    case FZGX_STATUS_UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    default:
      return "UNKNOWN";
  }
}

static float fzgx_float_from_bits(uint32_t bits) {
  float value = 0.0f;

  static_assert(sizeof(value) == sizeof(bits), "float/u32 bit cast must be 32-bit");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

static std::string fzgx_read_file(const char *path) {
  std::ifstream stream(path, std::ios::binary);

  if (!stream.is_open()) {
    return std::string();
  }
  return std::string(
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>());
}

static bool fzgx_parse_hex_u32(const std::string &text, uint32_t *value_out) {
  char *end = nullptr;
  unsigned long parsed = 0ul;

  if ((value_out == nullptr) || (text.size() < 3u) || (text[0] != '0') ||
      ((text[1] != 'x') && (text[1] != 'X'))) {
    return false;
  }
  errno = 0;
  parsed = std::strtoul(text.c_str(), &end, 16);
  if ((errno != 0) || (end == nullptr) || (*end != '\0') ||
      (parsed > std::numeric_limits<uint32_t>::max())) {
    return false;
  }
  *value_out = (uint32_t)parsed;
  return true;
}

class JsonParser {
public:
  explicit JsonParser(const std::string &text)
      : begin_(text.data()), cur_(text.data()), end_(text.data() + text.size()) {}

  size_t offset() const {
    return (size_t)(cur_ - begin_);
  }

  bool parse_replay_file(ReplayFile *replay_out) {
    std::string key;
    bool saw_machine_setting_percent = false;

    if (replay_out == nullptr) {
      return false;
    }
    fzgx_replay_init_camera_render_state(&replay_out->camera_render_state);
    if (!consume('{')) {
      return false;
    }
    skip_ws();
    if (consume('}')) {
      return false;
    }
    for (;;) {
      if (!parse_string(&key)) {
        return false;
      }
      if (!consume(':')) {
        return false;
      }
      if (key == "type") {
        std::string type_name;

        if (!parse_string(&type_name) || (type_name != "fzgx_input_replay")) {
          return false;
        }
      } else if (key == "version") {
        if (!parse_u32(&replay_out->version)) {
          return false;
        }
      } else if (key == "track_index") {
        if (!parse_u32(&replay_out->track_index)) {
          return false;
        }
      } else if (key == "track_label") {
        if (!parse_string(&replay_out->track_label)) {
          return false;
        }
      } else if (key == "machine_index") {
        if (!parse_u32(&replay_out->machine_index)) {
          return false;
        }
      } else if (key == "machine_label") {
        if (!parse_string(&replay_out->machine_label)) {
          return false;
        }
      } else if (key == "machine_setting_percent") {
        if (!parse_u32(&replay_out->machine_setting_percent) ||
            (replay_out->machine_setting_percent > 100u)) {
          return false;
        }
        saw_machine_setting_percent = true;
      } else if (key == "control_profile_kind") {
        if (!parse_u32(&replay_out->control_profile_kind)) {
          return false;
        }
      } else if (key == "start_world_frame_index") {
        if (!parse_u32(&replay_out->start_world_frame_index)) {
          return false;
        }
      } else if (key == "saved_world_frame_index") {
        if (!parse_u32(&replay_out->saved_world_frame_index)) {
          return false;
        }
      } else if (key == "captured_frame_count") {
        if (!parse_u32(&replay_out->captured_frame_count)) {
          return false;
        }
      } else if (key == "camera_render_state") {
        if (!parse_camera_render_state(&replay_out->camera_render_state)) {
          return false;
        }
      } else if (key == "initial_pose_snapshot") {
        if (!parse_string(&replay_out->initial_pose_snapshot)) {
          return false;
        }
      } else if (key == "saved_pose_snapshot") {
        if (!parse_string(&replay_out->saved_pose_snapshot)) {
          return false;
        }
      } else if (key == "frames") {
        if (!parse_frames(replay_out)) {
          return false;
        }
      } else {
        if (!skip_value()) {
          return false;
        }
      }
      skip_ws();
      if (consume('}')) {
        break;
      }
      if (!consume(',')) {
        return false;
      }
    }
    skip_ws();
    if (cur_ != end_) {
      return false;
    }
    if ((replay_out->captured_frame_count != 0u) &&
        (replay_out->captured_frame_count != replay_out->frames.size())) {
      return false;
    }
    if (!saw_machine_setting_percent) {
      return false;
    }
    replay_out->captured_frame_count = (uint32_t)replay_out->frames.size();
    return true;
  }

private:
  void skip_ws() {
    while ((cur_ < end_) &&
           ((*cur_ == ' ') || (*cur_ == '\n') || (*cur_ == '\r') || (*cur_ == '\t'))) {
      ++cur_;
    }
  }

  bool consume(char ch) {
    skip_ws();
    if ((cur_ >= end_) || (*cur_ != ch)) {
      return false;
    }
    ++cur_;
    return true;
  }

  bool parse_u32(uint32_t *value_out) {
    std::string token;
    unsigned long parsed = 0ul;
    char *end = nullptr;

    if ((value_out == nullptr) || !parse_number_token(&token)) {
      return false;
    }
    errno = 0;
    parsed = std::strtoul(token.c_str(), &end, 10);
    if ((errno != 0) || (end == nullptr) || (*end != '\0') ||
        (parsed > std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    *value_out = (uint32_t)parsed;
    return true;
  }

  bool parse_i32(int32_t *value_out) {
    std::string token;
    long parsed = 0l;
    char *end = nullptr;

    if ((value_out == nullptr) || !parse_number_token(&token)) {
      return false;
    }
    errno = 0;
    parsed = std::strtol(token.c_str(), &end, 10);
    if ((errno != 0) || (end == nullptr) || (*end != '\0') ||
        (parsed < std::numeric_limits<int32_t>::min()) ||
        (parsed > std::numeric_limits<int32_t>::max())) {
      return false;
    }
    *value_out = (int32_t)parsed;
    return true;
  }

  bool parse_bool(bool *value_out) {
    skip_ws();
    if ((size_t)(end_ - cur_) >= 4u && std::strncmp(cur_, "true", 4u) == 0) {
      cur_ += 4;
      *value_out = true;
      return true;
    }
    if ((size_t)(end_ - cur_) >= 5u && std::strncmp(cur_, "false", 5u) == 0) {
      cur_ += 5;
      *value_out = false;
      return true;
    }
    return false;
  }

  bool parse_string(std::string *value_out) {
    std::string result;

    if ((value_out == nullptr) || !consume('"')) {
      return false;
    }
    while (cur_ < end_) {
      char ch = *cur_++;

      if (ch == '"') {
        *value_out = result;
        return true;
      }
      if (ch != '\\') {
        result.push_back(ch);
        continue;
      }
      if (cur_ >= end_) {
        return false;
      }
      ch = *cur_++;
      switch (ch) {
        case '"':
        case '\\':
        case '/':
          result.push_back(ch);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          unsigned int code_point = 0u;

          for (int i = 0; i < 4; ++i) {
            char hex = 0;

            if (cur_ >= end_) {
              return false;
            }
            hex = *cur_++;
            code_point <<= 4u;
            if (('0' <= hex) && (hex <= '9')) {
              code_point |= (unsigned int)(hex - '0');
            } else if (('a' <= hex) && (hex <= 'f')) {
              code_point |= (unsigned int)(hex - 'a' + 10);
            } else if (('A' <= hex) && (hex <= 'F')) {
              code_point |= (unsigned int)(hex - 'A' + 10);
            } else {
              return false;
            }
          }
          if (code_point <= 0x7fu) {
            result.push_back((char)code_point);
          } else if (code_point <= 0x7ffu) {
            result.push_back((char)(0xc0u | ((code_point >> 6u) & 0x1fu)));
            result.push_back((char)(0x80u | (code_point & 0x3fu)));
          } else {
            result.push_back((char)(0xe0u | ((code_point >> 12u) & 0x0fu)));
            result.push_back((char)(0x80u | ((code_point >> 6u) & 0x3fu)));
            result.push_back((char)(0x80u | (code_point & 0x3fu)));
          }
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool parse_number_token(std::string *value_out) {
    const char *start = nullptr;

    if (value_out == nullptr) {
      return false;
    }
    skip_ws();
    start = cur_;
    if ((cur_ < end_) && ((*cur_ == '-') || (*cur_ == '+'))) {
      ++cur_;
    }
    if ((cur_ >= end_) || (*cur_ < '0') || (*cur_ > '9')) {
      return false;
    }
    while ((cur_ < end_) && (*cur_ >= '0') && (*cur_ <= '9')) {
      ++cur_;
    }
    if ((cur_ < end_) && (*cur_ == '.')) {
      ++cur_;
      while ((cur_ < end_) && (*cur_ >= '0') && (*cur_ <= '9')) {
        ++cur_;
      }
    }
    if ((cur_ < end_) && ((*cur_ == 'e') || (*cur_ == 'E'))) {
      ++cur_;
      if ((cur_ < end_) && ((*cur_ == '-') || (*cur_ == '+'))) {
        ++cur_;
      }
      while ((cur_ < end_) && (*cur_ >= '0') && (*cur_ <= '9')) {
        ++cur_;
      }
    }
    *value_out = std::string(start, cur_);
    return true;
  }

  bool skip_value() {
    skip_ws();
    if (cur_ >= end_) {
      return false;
    }
    if (*cur_ == '{') {
      std::string key;

      ++cur_;
      skip_ws();
      if (consume('}')) {
        return true;
      }
      for (;;) {
        if (!parse_string(&key) || !consume(':') || !skip_value()) {
          return false;
        }
        skip_ws();
        if (consume('}')) {
          return true;
        }
        if (!consume(',')) {
          return false;
        }
      }
    }
    if (*cur_ == '[') {
      ++cur_;
      skip_ws();
      if (consume(']')) {
        return true;
      }
      for (;;) {
        if (!skip_value()) {
          return false;
        }
        skip_ws();
        if (consume(']')) {
          return true;
        }
        if (!consume(',')) {
          return false;
        }
      }
    }
    if (*cur_ == '"') {
      std::string ignored;

      return parse_string(&ignored);
    }
    if ((*cur_ == 't') || (*cur_ == 'f')) {
      bool ignored = false;

      return parse_bool(&ignored);
    }
    if ((size_t)(end_ - cur_) >= 4u && std::strncmp(cur_, "null", 4u) == 0) {
      cur_ += 4;
      return true;
    }
    if ((*cur_ == '-') || (*cur_ == '+') || ((*cur_ >= '0') && (*cur_ <= '9'))) {
      std::string token;

      return parse_number_token(&token);
    }
    return false;
  }

  bool parse_camera_render_state(fzgx_replay_camera_render_state *state_out) {
    std::string key;

    if ((state_out == nullptr) || !consume('{')) {
      return false;
    }
    skip_ws();
    if (consume('}')) {
      return true;
    }
    for (;;) {
      if (!parse_string(&key) || !consume(':')) {
        return false;
      }
      if (key == "aspect_ratio_hex") {
        std::string hex_text;
        uint32_t bits = 0u;

        if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
          return false;
        }
        state_out->aspect_ratio = fzgx_float_from_bits(bits);
      } else if (key == "display_mode_kind") {
        int32_t value = 0;

        if (!parse_i32(&value)) {
          return false;
        }
        state_out->display_mode_kind = value;
      } else if (key == "camera_parameter_hex") {
        std::string hex_text;
        uint32_t bits = 0u;

        if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
          return false;
        }
        state_out->camera_parameter = fzgx_float_from_bits(bits);
      } else if (key == "camera_manager_mode") {
        int32_t value = 0;

        if (!parse_i32(&value)) {
          return false;
        }
        state_out->camera_manager_mode = value;
      } else {
        if (!skip_value()) {
          return false;
        }
      }
      skip_ws();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
    }
  }

  bool parse_frame_record(ReplayFile *replay_out) {
    ReplayFrameRecord frame;
    std::string hex_text;
    uint32_t bits = 0u;
    bool pressed = false;

    if ((replay_out == nullptr) || !consume('[')) {
      return false;
    }
    if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
      return false;
    }
    frame.control.steer_yaw = fzgx_float_from_bits(bits);
    if (!consume(',')) {
      return false;
    }

    if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
      return false;
    }
    frame.control.steer_pitch = fzgx_float_from_bits(bits);
    if (!consume(',')) {
      return false;
    }

    if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
      return false;
    }
    frame.control.accel = fzgx_float_from_bits(bits);
    if (!consume(',')) {
      return false;
    }

    if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
      return false;
    }
    frame.control.brake = fzgx_float_from_bits(bits);
    if (!consume(',')) {
      return false;
    }

    if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
      return false;
    }
    frame.control.strafe = fzgx_float_from_bits(bits);
    if (!consume(',')) {
      return false;
    }

    if (!parse_string(&hex_text) || !fzgx_parse_hex_u32(hex_text, &bits)) {
      return false;
    }
    frame.control.buttons = bits;
    frame.control.control_profile_kind = (uint8_t)replay_out->control_profile_kind;
    if (!consume(',')) {
      return false;
    }

    if (!parse_bool(&pressed)) {
      return false;
    }
    frame.camera_input.view_up_pressed = pressed ? 1u : 0u;
    if (!consume(',')) {
      return false;
    }

    if (!parse_bool(&pressed) || !consume(']')) {
      return false;
    }
    frame.camera_input.view_down_pressed = pressed ? 1u : 0u;
    replay_out->frames.push_back(frame);
    return true;
  }

  bool parse_frames(ReplayFile *replay_out) {
    if ((replay_out == nullptr) || !consume('[')) {
      return false;
    }
    skip_ws();
    if (consume(']')) {
      return true;
    }
    for (;;) {
      if (!parse_frame_record(replay_out)) {
        return false;
      }
      skip_ws();
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
    }
  }

  const char *begin_;
  const char *cur_;
  const char *end_;
};

static std::string fzgx_build_pose_snapshot_text(
    const ReplayFile &replay,
    const fzgx_machine_snapshot &machine,
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
      replay.track_index,
      replay.track_label.c_str(),
      replay.machine_index,
      replay.machine_label.c_str(),
      replay.machine_setting_percent,
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

static void fzgx_dump_machine_track_query_debug(
    const fzgx_content_bundle *bundle,
    const fzgx_sim_world &world,
    const fzgx_machine_snapshot &machine,
    size_t frame_index) {
  const fzgx_track_course_content *course = nullptr;
  const fzgx_track_course_animation_content *animation_course = nullptr;
  const fzgx_track_manifest *track_manifest = nullptr;
  fzgx_active_checkpoint_bank_result bank = {};
  fzgx_current_track_query_result query = {};
  fzgx_track_frame_record cached_frames[FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY] = {};
  uint32_t cached_frame_count = 0u;
  uint32_t cached_frame_slot = 0u;
  fzgx_status status = FZGX_STATUS_OK;

  if ((bundle == nullptr) || (world.active_track_index >= bundle->track_count)) {
    return;
  }

  track_manifest = &bundle->tracks[world.active_track_index];
  status = fzgx_content_bundle_get_track_course_for_track_index(
      bundle, world.active_track_index, &course);
  if (status != FZGX_STATUS_OK) {
    std::fprintf(stderr, "qdbg|f=%zu|stage=get_course|status=%s\n", frame_index, fzgx_status_name(status));
    return;
  }
  status = fzgx_content_bundle_get_track_course_animation_for_track_index(
      bundle, world.active_track_index, &animation_course);
  if (status == FZGX_STATUS_OUT_OF_RANGE) {
    animation_course = nullptr;
    status = FZGX_STATUS_OK;
  }
  if (status != FZGX_STATUS_OK) {
    std::fprintf(
        stderr,
        "qdbg|f=%zu|stage=get_animation|status=%s\n",
        frame_index,
        fzgx_status_name(status));
    return;
  }

  status = fzgx_track_course_build_active_checkpoint_bank_for_point(
      course,
      track_manifest->authored_track_id,
      track_manifest->circuit_type,
      &machine.position,
      machine.current_checkpoint,
      machine.checkpoint_fraction,
      &bank);
  if (status != FZGX_STATUS_OK) {
    std::fprintf(stderr, "qdbg|f=%zu|stage=build_bank|status=%s\n", frame_index, fzgx_status_name(status));
    return;
  }

  cached_frame_slot = bank.preferred_variant_slot;
  if (cached_frame_slot >= bank.checkpoint_variant_count) {
    cached_frame_slot = 0u;
  }

  status = fzgx_track_course_build_cached_frames_for_checkpoint(
      course,
      animation_course,
      (uint32_t)bank.checkpoint_index[cached_frame_slot],
      bank.checkpoint_fraction[cached_frame_slot],
      cached_frames,
      FZGX_TRACK_EXPORTED_CACHED_FRAME_CAPACITY,
      &cached_frame_count);
  if (status != FZGX_STATUS_OK) {
    std::fprintf(
        stderr,
        "qdbg|f=%zu|stage=build_cached_frames|status=%s|slot=%u|cp=%d|cpf=%.6f\n",
        frame_index,
        fzgx_status_name(status),
        cached_frame_slot,
        bank.checkpoint_index[cached_frame_slot],
        bank.checkpoint_fraction[cached_frame_slot]);
    return;
  }

  status = fzgx_sim_world_build_machine_current_track_query_result(&world, 0u, &query);
  if (status != FZGX_STATUS_OK) {
    std::fprintf(
        stderr,
        "qdbg|f=%zu|stage=build_query|status=%s\n",
        frame_index,
        fzgx_status_name(status));
    return;
  }

  std::fprintf(
      stderr,
      "qdbg|f=%zu|authored=%u|variants=%u|preferred=%u|bank_cp=(%d,%d,%d,%d)|"
      "bank_cpf=(%.6f,%.6f,%.6f,%.6f)|contain=(%d,%d,%d,%d)|query_sel=%d|"
      "query_bank=(%d,%d,%d,%d)|query_cp=(%d,%.6f)|qflags=0x%08x|qanchor=(%.3f,%.3f,%.3f)\n",
      frame_index,
      track_manifest->authored_track_id,
      bank.checkpoint_variant_count,
      bank.preferred_variant_slot,
      bank.checkpoint_index[0],
      bank.checkpoint_index[1],
      bank.checkpoint_index[2],
      bank.checkpoint_index[3],
      bank.checkpoint_fraction[0],
      bank.checkpoint_fraction[1],
      bank.checkpoint_fraction[2],
      bank.checkpoint_fraction[3],
      bank.containment_checkpoint_index[0],
      bank.containment_checkpoint_index[1],
      bank.containment_checkpoint_index[2],
      bank.containment_checkpoint_index[3],
      query.selected_cached_frame_index,
      query.active_bank_cp_idx[0],
      query.active_bank_cp_idx[1],
      query.active_bank_cp_idx[2],
      query.active_bank_cp_idx[3],
      query.checkpoint_index,
      query.checkpoint_fraction,
      query.frame.track_flags,
      query.frame.track_anchor.x,
      query.frame.track_anchor.y,
      query.frame.track_anchor.z);

  for (uint32_t i = 0u; i < cached_frame_count; ++i) {
    std::fprintf(
        stderr,
        "qdbg_frame|f=%zu|i=%u|flags=0x%08x|anchor=(%.3f,%.3f,%.3f)|origin=(%.3f,%.3f,%.3f)|"
        "width=%.6f|hcyl=%.6f|follow=(%.3f,%.3f,%.3f)\n",
        frame_index,
        i,
        cached_frames[i].track_flags,
        cached_frames[i].track_anchor.x,
        cached_frames[i].track_anchor.y,
        cached_frames[i].track_anchor.z,
        cached_frames[i].track_current_transform.origin_x,
        cached_frames[i].track_current_transform.origin_y,
        cached_frames[i].track_current_transform.origin_z,
        cached_frames[i].track_width_or_radius,
        cached_frames[i].track_hcylin,
        cached_frames[i].track_follow_offset.x,
        cached_frames[i].track_follow_offset.y,
        cached_frames[i].track_follow_offset.z);
  }
}

}  // namespace

int main(int argc, char **argv) {
  const fzgx_content_bundle *bundle = nullptr;
  ReplayFile replay = {};
  std::string json_text;
  JsonParser *parser = nullptr;
  fzgx_sim_world world = {};
  fzgx_game_camera_runtime camera = {};
  fzgx_race_step_options options = {};
  fzgx_replay_loaded_courses courses = {};
  fzgx_status status = FZGX_STATUS_OK;
  std::string final_pose_snapshot;
  bool final_pose_matches = false;
  bool have_prev_pos = false;
  fzgx_vec3 prev_pos = {};
  bool found_large_y_jump = false;
  bool found_air_start = false;
  size_t trace_end_frame = 0u;
  size_t tail_trace_start = 0u;
  uint32_t prev_air_time = 0u;
  int32_t prev_selected_cached_frame_index = -1;
  uint32_t prev_branch_slot = 0xffffffffu;
  std::vector<std::string> recent_trace_lines = {};

  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <replay.json>\n", argv[0]);
    return 2;
  }

  json_text = fzgx_read_file(argv[1]);
  if (json_text.empty()) {
    std::fprintf(stderr, "failed to read replay: %s\n", argv[1]);
    return 1;
  }

  parser = new JsonParser(json_text);
  if (!parser->parse_replay_file(&replay)) {
    std::fprintf(stderr, "failed to parse replay json at offset %zu\n", parser->offset());
    delete parser;
    return 1;
  }
  delete parser;

  tail_trace_start = (replay.frames.size() > 160u) ? (replay.frames.size() - 160u) : 0u;

  bundle = fzgx_content_get_builtin_iso_bundle();
  if (bundle == nullptr) {
    std::fprintf(stderr, "builtin content bundle missing\n");
    return 1;
  }
  if ((replay.track_index >= bundle->track_count) ||
      (replay.machine_index >= bundle->machine_count)) {
    std::fprintf(stderr, "replay content indices out of range\n");
    return 1;
  }
  options.advance_lap_timers = true;
  fzgx_replay_init_loaded_courses(&courses);
  status = fzgx_replay_start_session(
      FZGX_REPO_ROOT,
      bundle,
      replay.track_index,
      replay.machine_index,
      replay.machine_setting_percent,
      &world,
      &camera,
      &replay.camera_render_state,
      &courses);
  if (status != FZGX_STATUS_OK) {
    std::fprintf(stderr, "start_session failed: %s\n", fzgx_status_name(status));
    fzgx_replay_release_loaded_courses(&courses);
    return 1;
  }

  for (size_t frame_index = 0u; frame_index < replay.frames.size(); ++frame_index) {
    status = fzgx_replay_step_frame(
        &world,
        &camera,
        &replay.frames[frame_index].control,
        &replay.frames[frame_index].camera_input,
        &options);
    if (status != FZGX_STATUS_OK) {
      std::fprintf(
          stderr,
          "step_frame failed at replay frame %zu: %s\n",
          frame_index,
          fzgx_status_name(status));
      break;
    }
    if (world.machine_count != 0u) {
      const fzgx_machine_snapshot &machine = world.machines[0];
      float delta_x = 0.0f;
      float delta_y = 0.0f;
      float delta_z = 0.0f;
      float delta_len = 0.0f;
      char trace_line[1024];

      if (have_prev_pos) {
        delta_x = machine.position.x - prev_pos.x;
        delta_y = machine.position.y - prev_pos.y;
        delta_z = machine.position.z - prev_pos.z;
        delta_len = std::sqrt(
            delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
        if (!found_large_y_jump && (std::fabs(delta_y) > 50.0f)) {
          size_t recent_index;

          found_large_y_jump = true;
          trace_end_frame = frame_index + 8u;
          std::fprintf(
              stderr,
              "jump|f=%zu|dy=%.3f|prev_y=%.3f|y=%.3f\n",
              frame_index,
              delta_y,
              prev_pos.y,
              machine.position.y);
          for (recent_index = 0u; recent_index < recent_trace_lines.size(); ++recent_index) {
            std::fprintf(stderr, "%s", recent_trace_lines[recent_index].c_str());
          }
        }
        if (!found_large_y_jump && (delta_len > 50.0f)) {
          size_t recent_index;

          found_large_y_jump = true;
          trace_end_frame = frame_index + 8u;
          std::fprintf(
              stderr,
              "jump3|f=%zu|d=%.3f|dx=%.3f|dy=%.3f|dz=%.3f|prev=(%.3f,%.3f,%.3f)|"
              "pos=(%.3f,%.3f,%.3f)\n",
              frame_index,
              delta_len,
              delta_x,
              delta_y,
              delta_z,
              prev_pos.x,
              prev_pos.y,
              prev_pos.z,
              machine.position.x,
              machine.position.y,
              machine.position.z);
          for (recent_index = 0u; recent_index < recent_trace_lines.size(); ++recent_index) {
            std::fprintf(stderr, "%s", recent_trace_lines[recent_index].c_str());
          }
        }
      }
      have_prev_pos = true;
      prev_pos = machine.position;

      {
        const float basis_x_len =
            std::sqrt(
                machine.basis_physical.basis_x_x * machine.basis_physical.basis_x_x +
                machine.basis_physical.basis_x_y * machine.basis_physical.basis_x_y +
                machine.basis_physical.basis_x_z * machine.basis_physical.basis_x_z);
        const float basis_y_len =
            std::sqrt(
                machine.basis_physical.basis_y_x * machine.basis_physical.basis_y_x +
                machine.basis_physical.basis_y_y * machine.basis_physical.basis_y_y +
                machine.basis_physical.basis_y_z * machine.basis_physical.basis_y_z);
        const float basis_z_len =
            std::sqrt(
                machine.basis_physical.basis_z_x * machine.basis_physical.basis_z_x +
                machine.basis_physical.basis_z_y * machine.basis_physical.basis_z_y +
                machine.basis_physical.basis_z_z * machine.basis_physical.basis_z_z);
        const float basis_xy_dot =
            machine.basis_physical.basis_x_x * machine.basis_physical.basis_y_x +
            machine.basis_physical.basis_x_y * machine.basis_physical.basis_y_y +
            machine.basis_physical.basis_x_z * machine.basis_physical.basis_y_z;
        const float basis_xz_dot =
            machine.basis_physical.basis_x_x * machine.basis_physical.basis_z_x +
            machine.basis_physical.basis_x_y * machine.basis_physical.basis_z_y +
            machine.basis_physical.basis_x_z * machine.basis_physical.basis_z_z;
        const float basis_yz_dot =
            machine.basis_physical.basis_y_x * machine.basis_physical.basis_z_x +
            machine.basis_physical.basis_y_y * machine.basis_physical.basis_z_y +
            machine.basis_physical.basis_y_z * machine.basis_physical.basis_z_z;

      std::snprintf(
          trace_line,
          sizeof(trace_line),
          "trace|f=%zu|wf=%u|dx=%.3f|dy=%.3f|dz=%.3f|d=%.3f|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|kmh=%.3f|"
          "height=%.3f|air=%u|floor=0x%08x|branch_slot=%u|branch_indicator=0x%08x|"
          "branch_flags=0x%08x|track_flags=0x%08x|cp=%d|cpf=%.6f|cur_ptr=%d|cur_cp=%d|"
          "cur_cpf=%.6f|next_cp=%d|next_cpf=%.6f|sel=%d|cached=%u|cp_count=%d|"
          "basis_z=(%.3f,%.3f,%.3f)|basis_len=(%.6f,%.6f,%.6f)|basis_dot=(%.6f,%.6f,%.6f)|"
          "surface_normal=(%.3f,%.3f,%.3f)|"
          "angvel=(%.3f,%.3f,%.3f)|"
          "last_cp=(%d,%.6f,%.3f,%.3f,%.3f)|last_fit=(%.3f,%.3f,%.3f)|"
          "respawn=(%.3f,%.3f,%.3f)\n",
          frame_index,
          world.frame_index,
          delta_x,
          delta_y,
          delta_z,
          delta_len,
          machine.position.x,
          machine.position.y,
          machine.position.z,
          machine.velocity.x,
          machine.velocity.y,
          machine.velocity.z,
          machine.speed_kmh,
          machine.zero_minus_height_above_track,
          machine.air_time,
          machine.floor_surface_flags,
          machine.branch_slot,
          machine.branch_indicator,
          machine.branch_flags,
          machine.track_state.flags,
          machine.current_checkpoint,
          machine.checkpoint_fraction,
          machine.track_state.cur_cp_pointer,
          machine.track_state.cur_cp_idx,
          machine.track_state.cur_cp_frac,
          machine.track_state.next_cp_idx,
          machine.track_state.next_cp_frac,
          machine.track_state.selected_cached_frame_index,
          machine.track_state.cached_frame_count,
          machine.track_state.checkpoint_variant_count,
          machine.basis_physical.basis_z_x,
          machine.basis_physical.basis_z_y,
          machine.basis_physical.basis_z_z,
          basis_x_len,
          basis_y_len,
          basis_z_len,
          basis_xy_dot,
          basis_xz_dot,
          basis_yz_dot,
          machine.surface_normal.x,
          machine.surface_normal.y,
          machine.surface_normal.z,
          machine.angular_velocity.x,
          machine.angular_velocity.y,
          machine.angular_velocity.z,
          machine.track_state.last_cp_idx,
          machine.track_state.last_cp_frac,
          machine.track_state.last_cp_pos.x,
          machine.track_state.last_cp_pos.y,
          machine.track_state.last_cp_pos.z,
          machine.track_state.last_fit_pos.x,
          machine.track_state.last_fit_pos.y,
          machine.track_state.last_fit_pos.z,
          machine.track_state.respawn_pos.x,
          machine.track_state.respawn_pos.y,
          machine.track_state.respawn_pos.z);
      }
      if (recent_trace_lines.size() == 8u) {
        recent_trace_lines.erase(recent_trace_lines.begin());
      }
      recent_trace_lines.push_back(std::string(trace_line));

      if ((frame_index < 40u) || ((trace_end_frame != 0u) && (frame_index <= trace_end_frame))) {
        for (uint32_t corner_index = 0u; corner_index < 4u; ++corner_index) {
          const auto &corner = machine.suspension_corners[corner_index];

          std::fprintf(
              stderr,
              "susp|f=%zu|i=%u|state=0x%02x|force=%.6f|len=%.6f|"
              "force_world=(%.3f,%.3f,%.3f)|up=(%.3f,%.3f,%.3f)|"
              "pos=(%.3f,%.3f,%.3f)|old=(%.3f,%.3f,%.3f)\n",
              frame_index,
              corner_index,
              corner.state,
              corner.force,
              corner.force_spatial_len,
              corner.force_spatial.x,
              corner.force_spatial.y,
              corner.force_spatial.z,
              corner.up_vector.x,
              corner.up_vector.y,
              corner.up_vector.z,
              corner.pos.x,
              corner.pos.y,
              corner.pos.z,
              corner.pos_old.x,
              corner.pos_old.y,
              corner.pos_old.z);
        }
      }

      if (!found_air_start && (prev_air_time == 0u) && (machine.air_time != 0u)) {
        size_t recent_index;

        found_air_start = true;
        trace_end_frame = frame_index + 24u;
        std::fprintf(
            stderr,
            "air_start|f=%zu|air=%u|height=%.3f|pos=(%.3f,%.3f,%.3f)|vel=(%.3f,%.3f,%.3f)|"
            "branch_slot=%u|cp=%d|cpf=%.6f|cur_cp=%d|cur_cpf=%.6f|sel=%d|cached=%u|cp_count=%d\n",
            frame_index,
            machine.air_time,
            machine.zero_minus_height_above_track,
            machine.position.x,
            machine.position.y,
            machine.position.z,
            machine.velocity.x,
            machine.velocity.y,
            machine.velocity.z,
            machine.branch_slot,
            machine.current_checkpoint,
            machine.checkpoint_fraction,
            machine.track_state.cur_cp_idx,
            machine.track_state.cur_cp_frac,
            machine.track_state.selected_cached_frame_index,
            machine.track_state.cached_frame_count,
            machine.track_state.checkpoint_variant_count);
        for (recent_index = 0u; recent_index < recent_trace_lines.size(); ++recent_index) {
          std::fprintf(stderr, "%s", recent_trace_lines[recent_index].c_str());
        }
      }

      if (found_large_y_jump && (frame_index <= trace_end_frame)) {
        std::fprintf(stderr, "%s", trace_line);
        fzgx_dump_machine_track_query_debug(bundle, world, machine, frame_index);
      }
      if (found_air_start && (frame_index <= trace_end_frame)) {
        std::fprintf(stderr, "%s", trace_line);
        fzgx_dump_machine_track_query_debug(bundle, world, machine, frame_index);
      }
      if (frame_index >= tail_trace_start) {
        std::fprintf(stderr, "%s", trace_line);
        if (((prev_air_time == 0u) != (machine.air_time == 0u)) ||
            (prev_selected_cached_frame_index != machine.track_state.selected_cached_frame_index) ||
            (prev_branch_slot != machine.branch_slot) ||
            (frame_index + 1u == replay.frames.size())) {
          fzgx_dump_machine_track_query_debug(bundle, world, machine, frame_index);
        }
      }
      prev_air_time = machine.air_time;
      prev_selected_cached_frame_index = machine.track_state.selected_cached_frame_index;
      prev_branch_slot = machine.branch_slot;
    }
  }

  if (world.machine_count != 0u) {
    final_pose_snapshot = fzgx_build_pose_snapshot_text(replay, world.machines[0], world.frame_index);
    if (!replay.saved_pose_snapshot.empty()) {
      final_pose_matches = (final_pose_snapshot == replay.saved_pose_snapshot);
    }
  }

  std::printf(
      "summary|status=%s|captured_frames=%zu|world_frame=%u|expected_saved_frame=%u|final_pose_match=%u|setting=%u|speed=%.6f|energy=%.6f|branch_slot=%u|checkpoint=%d|checkpoint_fraction=%.6f\n",
      fzgx_status_name(status),
      replay.frames.size(),
      world.frame_index,
      replay.saved_world_frame_index,
      final_pose_matches ? 1u : 0u,
      replay.machine_setting_percent,
      (world.machine_count != 0u) ? world.machines[0].speed_kmh : 0.0f,
      (world.machine_count != 0u) ? world.machines[0].energy : 0.0f,
      (world.machine_count != 0u) ? world.machines[0].branch_slot : 0u,
      (world.machine_count != 0u) ? world.machines[0].current_checkpoint : -1,
      (world.machine_count != 0u) ? world.machines[0].checkpoint_fraction : 0.0f);
  if (!final_pose_snapshot.empty()) {
    std::printf("%s\n", final_pose_snapshot.c_str());
  }

  fzgx_replay_release_loaded_courses(&courses);
  return (status == FZGX_STATUS_OK) ? 0 : 1;
}
