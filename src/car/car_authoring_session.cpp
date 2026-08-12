#include "car/car_authoring_session.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace godot;

namespace {

static constexpr uint8_t CAR_PROPS_MAGIC[8] = {'M', 'X', 'T', 'C', 'P', 'R', 'P', 0};
static constexpr size_t CAR_PROPS_HEADER_SIZE = 24;
static constexpr const char *LAYER_NAMES[CAR_CURVE_LAYER_COUNT] = {
	"base", "mts", "quickturn", "no_boost", "manual_boost", "dashplate_boost", "stacked_boost"
};

struct ByteReader {
	const uint8_t *data = nullptr;
	size_t size = 0;
	size_t cursor = 0;

	bool read_u8(uint8_t &out) {
		if (cursor + 1 > size) return false;
		out = data[cursor++];
		return true;
	}
	bool read_u16(uint16_t &out) {
		if (cursor + 2 > size) return false;
		out = static_cast<uint16_t>(data[cursor]) |
			(static_cast<uint16_t>(data[cursor + 1]) << 8);
		cursor += 2;
		return true;
	}
	bool read_u32(uint32_t &out) {
		if (cursor + 4 > size) return false;
		out = static_cast<uint32_t>(data[cursor]) |
			(static_cast<uint32_t>(data[cursor + 1]) << 8) |
			(static_cast<uint32_t>(data[cursor + 2]) << 16) |
			(static_cast<uint32_t>(data[cursor + 3]) << 24);
		cursor += 4;
		return true;
	}
	bool read_u64(uint64_t &out) {
		uint32_t low = 0;
		uint32_t high = 0;
		if (!read_u32(low) || !read_u32(high)) return false;
		out = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
		return true;
	}
	bool read_f32(float &out) {
		uint32_t bits = 0;
		if (!read_u32(bits)) return false;
		std::memcpy(&out, &bits, sizeof(out));
		return std::isfinite(out);
	}
};

struct ByteWriter {
	std::vector<uint8_t> bytes;

	void write_u8(uint8_t value) { bytes.push_back(value); }
	void write_u16(uint16_t value) {
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}
	void write_u32(uint32_t value) {
		for (uint32_t shift = 0; shift < 32; shift += 8) {
			bytes.push_back(static_cast<uint8_t>(value >> shift));
		}
	}
	void write_u64(uint64_t value) {
		write_u32(static_cast<uint32_t>(value));
		write_u32(static_cast<uint32_t>(value >> 32));
	}
	void write_f32(float value) {
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		write_u32(bits);
	}
};

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t i = 0; i < size; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) {
			const uint32_t mask = 0u - (crc & 1u);
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
		}
	}
	return ~crc;
}

static PackedStringArray single_error(const String &message)
{
	PackedStringArray errors;
	errors.push_back(message);
	return errors;
}

static Dictionary result_dictionary(bool valid, const PackedStringArray &errors, const PackedStringArray &warnings)
{
	Dictionary result;
	result["valid"] = valid;
	result["errors"] = errors;
	result["warnings"] = warnings;
	return result;
}

static String stat_category(uint16_t stat)
{
	if (stat == CAR_STAT_WEIGHT_KG || stat == CAR_STAT_BODY || stat == CAR_STAT_MAX_ENERGY) return "Machine";
	if (stat == CAR_STAT_ACCELERATION || stat == CAR_STAT_MAX_SPEED || stat == CAR_STAT_DRAG ||
			stat >= CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER) return "Drive";
	if (stat >= CAR_STAT_GRIP_1 && stat <= CAR_STAT_TURN_DECEL) return "Handling";
	if (stat == CAR_STAT_CAMERA_REORIENTING || stat == CAR_STAT_CAMERA_REPOSITIONING) return "Camera";
	if (stat == CAR_STAT_TRACK_COLLISION || stat == CAR_STAT_OBSTACLE_COLLISION) return "Collision";
	if (stat >= CAR_STAT_BOOST_ENERGY_USE_RATE && stat <= CAR_STAT_SHIFT_BOOST_VELOCITY_MULTIPLIER) return "Boost";
	if (stat == CAR_STAT_AIR_PITCH_UP_SPEED_LOSS_FACTOR || stat == CAR_STAT_AIR_GLIDE_STEERING_SPEED_LOSS_FACTOR) return "Air";
	return "Other";
}

static String stat_unit(uint16_t stat)
{
	if (stat == CAR_STAT_WEIGHT_KG) return "kg";
	if (stat == CAR_STAT_MANUAL_BOOST_DURATION_SECONDS || stat == CAR_STAT_DASHPLATE_BOOST_DURATION_SECONDS) return "seconds";
	if (stat == CAR_STAT_MAX_ENERGY) return "energy";
	return "scalar";
}

} // namespace

MxtCarAuthoringSession::MxtCarAuthoringSession()
{
	reset_to_defaults();
	dirty = false;
}

void MxtCarAuthoringSession::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("reset_to_defaults"), &MxtCarAuthoringSession::reset_to_defaults);
	ClassDB::bind_method(D_METHOD("load_bytes", "bytes"), &MxtCarAuthoringSession::load_bytes);
	ClassDB::bind_method(D_METHOD("load_file", "path"), &MxtCarAuthoringSession::load_file);
	ClassDB::bind_method(D_METHOD("serialize"), &MxtCarAuthoringSession::serialize);
	ClassDB::bind_method(D_METHOD("save_file", "path"), &MxtCarAuthoringSession::save_file);
	ClassDB::bind_method(D_METHOD("validate"), &MxtCarAuthoringSession::validate);
	ClassDB::bind_method(D_METHOD("get_stat_schema"), &MxtCarAuthoringSession::get_stat_schema);
	ClassDB::bind_method(D_METHOD("get_layer_names"), &MxtCarAuthoringSession::get_layer_names);
	ClassDB::bind_method(D_METHOD("get_curve", "layer_name", "stat_name"), &MxtCarAuthoringSession::get_curve);
	ClassDB::bind_method(D_METHOD("set_curve", "layer_name", "stat_name", "keys"), &MxtCarAuthoringSession::set_curve);
	ClassDB::bind_method(D_METHOD("sample_curve", "layer_name", "stat_name", "machine_setting"), &MxtCarAuthoringSession::sample_curve);
	ClassDB::bind_method(D_METHOD("get_s_boost_value", "stat_name"), &MxtCarAuthoringSession::get_s_boost_value);
	ClassDB::bind_method(D_METHOD("set_s_boost_value", "stat_name", "value"), &MxtCarAuthoringSession::set_s_boost_value);
	ClassDB::bind_method(D_METHOD("get_tilt_corners"), &MxtCarAuthoringSession::get_tilt_corners);
	ClassDB::bind_method(D_METHOD("get_wall_corners"), &MxtCarAuthoringSession::get_wall_corners);
	ClassDB::bind_method(D_METHOD("set_tilt_corners", "value"), &MxtCarAuthoringSession::set_tilt_corners);
	ClassDB::bind_method(D_METHOD("set_wall_corners", "value"), &MxtCarAuthoringSession::set_wall_corners);
	ClassDB::bind_method(D_METHOD("get_state_flags"), &MxtCarAuthoringSession::get_state_flags);
	ClassDB::bind_method(D_METHOD("set_state_flags", "value"), &MxtCarAuthoringSession::set_state_flags);
	ClassDB::bind_method(D_METHOD("is_dirty"), &MxtCarAuthoringSession::is_dirty);
	ClassDB::bind_method(D_METHOD("clear_dirty"), &MxtCarAuthoringSession::clear_dirty);
	ClassDB::bind_method(D_METHOD("can_undo"), &MxtCarAuthoringSession::can_undo);
	ClassDB::bind_method(D_METHOD("can_redo"), &MxtCarAuthoringSession::can_redo);
	ClassDB::bind_method(D_METHOD("undo"), &MxtCarAuthoringSession::undo);
	ClassDB::bind_method(D_METHOD("redo"), &MxtCarAuthoringSession::redo);
}

MxtCarAuthoringSession::Curve &MxtCarAuthoringSession::curve_at(uint8_t layer, uint16_t stat)
{
	return curves[static_cast<size_t>(layer) * CAR_STAT_COUNT + stat];
}

const MxtCarAuthoringSession::Curve &MxtCarAuthoringSession::curve_at(uint8_t layer, uint16_t stat) const
{
	return curves[static_cast<size_t>(layer) * CAR_STAT_COUNT + stat];
}

int32_t MxtCarAuthoringSession::stat_index(const String &stat_name)
{
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (stat_name == PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat))) return stat;
	}
	return -1;
}

int32_t MxtCarAuthoringSession::layer_index(const String &layer_name)
{
	for (uint8_t layer = 0; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
		if (layer_name == LAYER_NAMES[layer]) return layer;
	}
	return -1;
}

void MxtCarAuthoringSession::reset_to_defaults()
{
	if (!curves[0].empty()) push_undo_snapshot();
	PhysicsCarProperties defaults;
	for (Curve &curve : curves) curve.clear();
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		curve_at(CAR_CURVE_BASE, stat).push_back({0.0f, defaults.base_stats[stat], 0.0f, 0.0f});
		s_boost_values[stat] = defaults.s_boost_stats[stat];
		if (!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) continue;
		for (uint8_t layer = CAR_CURVE_MTS; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
			curve_at(layer, stat).push_back({0.0f, defaults.modifier_stats[layer - 1][stat], 0.0f, 0.0f});
		}
	}
	for (uint8_t i = 0; i < 4; ++i) {
		tilt_corners[i] = defaults.tilt_corners[i];
		wall_corners[i] = defaults.wall_corners[i];
	}
	state_flags = defaults.state_flags;
	dirty = true;
	redo_history.clear();
}

bool MxtCarAuthoringSession::read_document(
		const PackedByteArray &bytes,
		MxtCarAuthoringSession &target,
		String &out_error)
{
	PhysicsCarProperties runtime_check;
	if (!PhysicsCarProperties::deserialize_and_sample(bytes, 0.5f, runtime_check, out_error)) return false;
	ByteReader reader{bytes.ptr(), static_cast<size_t>(bytes.size()), 0};
	for (uint8_t expected : CAR_PROPS_MAGIC) {
		uint8_t actual = 0;
		if (!reader.read_u8(actual) || actual != expected) {
			out_error = "car properties magic does not match the current format";
			return false;
		}
	}
	uint64_t fingerprint = 0;
	uint32_t payload_size = 0;
	uint32_t crc = 0;
	if (!reader.read_u64(fingerprint) || !reader.read_u32(payload_size) || !reader.read_u32(crc)) return false;
	(void)fingerprint;
	(void)payload_size;
	(void)crc;
	for (Curve &curve : target.curves) curve.clear();
	if (!reader.read_u32(target.state_flags)) return false;
	for (SimVec3 &corner : target.tilt_corners) {
		if (!reader.read_f32(corner.x) || !reader.read_f32(corner.y) || !reader.read_f32(corner.z)) return false;
	}
	for (SimVec3 &corner : target.wall_corners) {
		if (!reader.read_f32(corner.x) || !reader.read_f32(corner.y) || !reader.read_f32(corner.z)) return false;
	}
	target.s_boost_values.fill(0.0f);
	uint16_t override_count = 0;
	if (!reader.read_u16(override_count)) return false;
	for (uint16_t i = 0; i < override_count; ++i) {
		uint16_t stat = 0;
		float value = 0.0f;
		if (!reader.read_u16(stat) || !reader.read_f32(value) || stat >= CAR_STAT_COUNT) return false;
		target.s_boost_values[stat] = value;
	}
	uint16_t curve_count = 0;
	if (!reader.read_u16(curve_count)) return false;
	for (uint16_t i = 0; i < curve_count; ++i) {
		uint16_t stat = 0;
		uint8_t layer = 0;
		uint8_t reserved = 0;
		uint16_t key_count = 0;
		if (!reader.read_u16(stat) || !reader.read_u8(layer) || !reader.read_u8(reserved) ||
				!reader.read_u16(key_count) || stat >= CAR_STAT_COUNT || layer >= CAR_CURVE_LAYER_COUNT ||
				reserved != 0 || key_count == 0 || key_count > MAX_CURVE_KEYS) return false;
		Curve &curve = target.curve_at(layer, stat);
		curve.reserve(key_count);
		if (key_count == 1) {
			float value = 0.0f;
			if (!reader.read_f32(value)) return false;
			curve.push_back({0.0f, value, 0.0f, 0.0f});
			continue;
		}
		for (uint16_t key_index = 0; key_index < key_count; ++key_index) {
			CurveKey key;
			if (!reader.read_f32(key.time) || !reader.read_f32(key.value) ||
					!reader.read_f32(key.tangent_in) || !reader.read_f32(key.tangent_out)) return false;
			curve.push_back(key);
		}
	}
	if (reader.cursor != reader.size) return false;
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) {
			target.s_boost_values[stat] = runtime_check.base_stats[stat];
		}
	}
	return true;
}

Dictionary MxtCarAuthoringSession::load_bytes(const PackedByteArray &bytes)
{
	Ref<MxtCarAuthoringSession> parsed;
	parsed.instantiate();
	parsed->undo_history.clear();
	parsed->redo_history.clear();
	String error;
	if (!read_document(bytes, *parsed.ptr(), error)) {
		return result_dictionary(false, single_error(error.is_empty() ? String("car properties document could not be decoded") : error), {});
	}
	curves = std::move(parsed->curves);
	s_boost_values = parsed->s_boost_values;
	tilt_corners = parsed->tilt_corners;
	wall_corners = parsed->wall_corners;
	state_flags = parsed->state_flags;
	undo_history.clear();
	redo_history.clear();
	dirty = false;
	return validate();
}

Dictionary MxtCarAuthoringSession::load_file(const String &path)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) return result_dictionary(false, single_error("could not open car properties file"), {});
	return load_bytes(file->get_buffer(file->get_length()));
}

bool MxtCarAuthoringSession::validate_document(PackedStringArray &out_errors, PackedStringArray &out_warnings) const
{
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		for (uint8_t layer = 0; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
			const bool required = layer == CAR_CURVE_BASE ||
				PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat));
			const Curve &curve = curve_at(layer, stat);
			if (!required) {
				if (!curve.empty()) out_errors.push_back("unsupported live-modifier curve is present");
				continue;
			}
			if (curve.empty() || curve.size() > MAX_CURVE_KEYS) {
				out_errors.push_back(String(LAYER_NAMES[layer]) + "/" + PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat)) + " has an invalid key count");
				continue;
			}
			float previous_time = -1.0f;
			for (const CurveKey &key : curve) {
				if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.tangent_in) ||
						!std::isfinite(key.tangent_out) || key.time < 0.0f || key.time > 1.0f ||
						key.time <= previous_time) {
					out_errors.push_back(String(LAYER_NAMES[layer]) + "/" + PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat)) + " has malformed keys");
					break;
				}
				previous_time = key.time;
			}
		}
		if (PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat)) &&
				!std::isfinite(s_boost_values[stat])) {
			out_errors.push_back(String("S-BOOST/") + PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat)) + " is not finite");
		}
	}
	for (const SimVec3 &corner : tilt_corners) {
		if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z)) out_errors.push_back("tilt corner is not finite");
	}
	for (const SimVec3 &corner : wall_corners) {
		if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z)) out_errors.push_back("wall corner is not finite");
	}
	if (!out_errors.is_empty()) return false;

	if (sample_curve("base", "weight_kg", 0.0) <= 0.0 || sample_curve("base", "weight_kg", 1.0) <= 0.0) {
		out_warnings.push_back("weight_kg becomes nonpositive at an endpoint");
	}
	static constexpr CarStatId NONNEGATIVE_STATS[] = {
		CAR_STAT_MANUAL_BOOST_DURATION_SECONDS,
		CAR_STAT_DASHPLATE_BOOST_DURATION_SECONDS,
		CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND,
		CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND,
	};
	for (CarStatId stat : NONNEGATIVE_STATS) {
		for (uint32_t sample = 0; sample <= 100; ++sample) {
			if (sample_curve("base", PhysicsCarProperties::stat_name(stat), sample * 0.01) < 0.0) {
				out_warnings.push_back(String(PhysicsCarProperties::stat_name(stat)) + " becomes negative");
				break;
			}
		}
	}
	for (uint8_t layer = CAR_CURVE_MTS; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
		for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
			if (!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) continue;
			for (uint32_t sample = 0; sample <= 100; ++sample) {
				const double value = sample_curve(LAYER_NAMES[layer], PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat)), sample * 0.01);
				if (value < -10.0 || value > 10.0) {
					out_warnings.push_back(String(LAYER_NAMES[layer]) + "/" + PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat)) + " has an extreme multiplier");
					break;
				}
			}
		}
	}
	return true;
}

bool MxtCarAuthoringSession::serialize_document(PackedByteArray &out_bytes, String &out_error) const
{
	PackedStringArray errors;
	PackedStringArray warnings;
	if (!validate_document(errors, warnings)) {
		out_error = errors[0];
		return false;
	}
	ByteWriter payload;
	payload.write_u32(state_flags);
	for (const SimVec3 &corner : tilt_corners) {
		payload.write_f32(corner.x); payload.write_f32(corner.y); payload.write_f32(corner.z);
	}
	for (const SimVec3 &corner : wall_corners) {
		payload.write_f32(corner.x); payload.write_f32(corner.y); payload.write_f32(corner.z);
	}
	uint16_t live_stat_count = 0;
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) ++live_stat_count;
	}
	payload.write_u16(live_stat_count);
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) continue;
		payload.write_u16(stat);
		payload.write_f32(s_boost_values[stat]);
	}
	payload.write_u16(static_cast<uint16_t>(CAR_STAT_COUNT + (CAR_CURVE_LAYER_COUNT - 1) * live_stat_count));
	for (uint8_t layer = 0; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
		for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
			if (layer != CAR_CURVE_BASE && !PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) continue;
			const Curve &curve = curve_at(layer, stat);
			payload.write_u16(stat);
			payload.write_u8(layer);
			payload.write_u8(0);
			payload.write_u16(static_cast<uint16_t>(curve.size()));
			if (curve.size() == 1) {
				payload.write_f32(curve[0].value);
				continue;
			}
			for (const CurveKey &key : curve) {
				payload.write_f32(key.time);
				payload.write_f32(key.value);
				payload.write_f32(key.tangent_in);
				payload.write_f32(key.tangent_out);
			}
		}
	}
	ByteWriter file;
	for (uint8_t byte : CAR_PROPS_MAGIC) file.write_u8(byte);
	file.write_u64(MXT_CAR_PROPS_SCHEMA_FINGERPRINT);
	file.write_u32(static_cast<uint32_t>(payload.bytes.size()));
	file.write_u32(crc32_bytes(payload.bytes.data(), payload.bytes.size()));
	file.bytes.insert(file.bytes.end(), payload.bytes.begin(), payload.bytes.end());
	out_bytes.resize(static_cast<int64_t>(file.bytes.size()));
	std::memcpy(out_bytes.ptrw(), file.bytes.data(), file.bytes.size());
	PhysicsCarProperties runtime_check;
	if (!PhysicsCarProperties::deserialize_and_sample(out_bytes, 0.5f, runtime_check, out_error)) return false;
	return true;
}

Dictionary MxtCarAuthoringSession::serialize() const
{
	PackedByteArray bytes;
	String error;
	Dictionary result;
	if (!serialize_document(bytes, error)) {
		return result_dictionary(false, single_error(error), {});
	}
	result = validate();
	result["bytes"] = bytes;
	return result;
}

Dictionary MxtCarAuthoringSession::save_file(const String &path)
{
	Dictionary result = serialize();
	if (!static_cast<bool>(result.get("valid", false))) return result;
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) return result_dictionary(false, single_error("could not open car properties destination"), {});
	file->store_buffer(result["bytes"]);
	if (file->get_error() != OK) return result_dictionary(false, single_error("could not write car properties destination"), {});
	dirty = false;
	return result;
}

Dictionary MxtCarAuthoringSession::validate() const
{
	PackedStringArray errors;
	PackedStringArray warnings;
	const bool valid = validate_document(errors, warnings);
	return result_dictionary(valid, errors, warnings);
}

Array MxtCarAuthoringSession::get_stat_schema() const
{
	Array output;
	PhysicsCarProperties defaults;
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		Dictionary entry;
		entry["id"] = stat;
		entry["name"] = PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat));
		entry["category"] = stat_category(stat);
		entry["unit"] = stat_unit(stat);
		entry["default_value"] = defaults.base_stats[stat];
		entry["supports_live_modifiers"] = PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat));
		entry["minimum"] = -static_cast<double>(std::numeric_limits<float>::max());
		entry["maximum"] = static_cast<double>(std::numeric_limits<float>::max());
		output.push_back(entry);
	}
	return output;
}

PackedStringArray MxtCarAuthoringSession::get_layer_names() const
{
	PackedStringArray output;
	for (const char *name : LAYER_NAMES) output.push_back(name);
	return output;
}

Array MxtCarAuthoringSession::get_curve(const String &layer_name, const String &stat_name) const
{
	Array output;
	const int32_t layer = layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	if (layer < 0 || stat < 0) return output;
	for (const CurveKey &key : curve_at(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat))) {
		Dictionary value;
		value["time"] = key.time;
		value["value"] = key.value;
		value["tangent_in"] = key.tangent_in;
		value["tangent_out"] = key.tangent_out;
		output.push_back(value);
	}
	return output;
}

Dictionary MxtCarAuthoringSession::set_curve(const String &layer_name, const String &stat_name, const Array &keys)
{
	const int32_t layer = layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	if (layer < 0 || stat < 0) return result_dictionary(false, single_error("unknown curve layer or stat"), {});
	if (layer != CAR_CURVE_BASE && !PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) {
		return result_dictionary(false, single_error("stat does not support live modifier curves"), {});
	}
	if (keys.is_empty() || keys.size() > MAX_CURVE_KEYS) return result_dictionary(false, single_error("curve key count is outside the supported range"), {});
	Curve replacement;
	replacement.reserve(keys.size());
	float previous_time = -1.0f;
	for (int64_t i = 0; i < keys.size(); ++i) {
		if (keys[i].get_type() != Variant::DICTIONARY) return result_dictionary(false, single_error("curve key must be an object"), {});
		const Dictionary input = keys[i];
		CurveKey key;
		key.time = static_cast<float>(input.get("time", 0.0));
		key.value = static_cast<float>(input.get("value", 0.0));
		key.tangent_in = static_cast<float>(input.get("tangent_in", 0.0));
		key.tangent_out = static_cast<float>(input.get("tangent_out", 0.0));
		if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.tangent_in) ||
				!std::isfinite(key.tangent_out) || key.time < 0.0f || key.time > 1.0f || key.time <= previous_time) {
			return result_dictionary(false, single_error("curve keys must be finite and have strictly increasing times in [0, 1]"), {});
		}
		previous_time = key.time;
		replacement.push_back(key);
	}
	push_undo_snapshot();
	curve_at(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat)) = std::move(replacement);
	redo_history.clear();
	dirty = true;
	return validate();
}

double MxtCarAuthoringSession::sample_curve(const String &layer_name, const String &stat_name, double machine_setting) const
{
	const int32_t layer = layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	if (layer < 0 || stat < 0) return 0.0;
	const Curve &curve = curve_at(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat));
	if (curve.empty()) return 0.0;
	if (curve.size() == 1) return curve[0].value;
	const float setting = static_cast<float>(std::clamp(machine_setting, 0.0, 1.0));
	if (setting <= curve.front().time) return curve.front().value;
	for (size_t i = 0; i + 1 < curve.size(); ++i) {
		const CurveKey &left = curve[i];
		const CurveKey &right = curve[i + 1];
		if (setting > right.time) continue;
		const float duration = right.time - left.time;
		const float u = (setting - left.time) / duration;
		const float outgoing = left.value + duration * left.tangent_out / 3.0f;
		const float incoming = right.value - duration * right.tangent_in / 3.0f;
		const float inverse = 1.0f - u;
		return left.value * inverse * inverse * inverse + 3.0f * outgoing * inverse * inverse * u +
			3.0f * incoming * inverse * u * u + right.value * u * u * u;
	}
	return curve.back().value;
}

double MxtCarAuthoringSession::get_s_boost_value(const String &stat_name) const
{
	const int32_t stat = stat_index(stat_name);
	if (stat < 0 || !PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) return 0.0;
	return s_boost_values[stat];
}

bool MxtCarAuthoringSession::set_s_boost_value(const String &stat_name, double value)
{
	const int32_t stat = stat_index(stat_name);
	if (stat < 0 || !std::isfinite(value) || !PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) return false;
	push_undo_snapshot();
	s_boost_values[stat] = static_cast<float>(value);
	redo_history.clear();
	dirty = true;
	return true;
}

PackedVector3Array MxtCarAuthoringSession::get_tilt_corners() const
{
	PackedVector3Array output;
	for (const SimVec3 &corner : tilt_corners) output.push_back(Vector3(corner.x, corner.y, corner.z));
	return output;
}

PackedVector3Array MxtCarAuthoringSession::get_wall_corners() const
{
	PackedVector3Array output;
	for (const SimVec3 &corner : wall_corners) output.push_back(Vector3(corner.x, corner.y, corner.z));
	return output;
}

bool MxtCarAuthoringSession::set_tilt_corners(const PackedVector3Array &value)
{
	if (value.size() != 4) return false;
	for (int64_t i = 0; i < value.size(); ++i) if (!value[i].is_finite()) return false;
	push_undo_snapshot();
	for (uint8_t i = 0; i < 4; ++i) tilt_corners[i] = SimVec3(value[i].x, value[i].y, value[i].z);
	redo_history.clear();
	dirty = true;
	return true;
}

bool MxtCarAuthoringSession::set_wall_corners(const PackedVector3Array &value)
{
	if (value.size() != 4) return false;
	for (int64_t i = 0; i < value.size(); ++i) if (!value[i].is_finite()) return false;
	push_undo_snapshot();
	for (uint8_t i = 0; i < 4; ++i) wall_corners[i] = SimVec3(value[i].x, value[i].y, value[i].z);
	redo_history.clear();
	dirty = true;
	return true;
}

int64_t MxtCarAuthoringSession::get_state_flags() const { return state_flags; }

void MxtCarAuthoringSession::set_state_flags(int64_t value)
{
	push_undo_snapshot();
	state_flags = static_cast<uint32_t>(value);
	redo_history.clear();
	dirty = true;
}

bool MxtCarAuthoringSession::is_dirty() const { return dirty; }
void MxtCarAuthoringSession::clear_dirty() { dirty = false; }
bool MxtCarAuthoringSession::can_undo() const { return !undo_history.empty(); }
bool MxtCarAuthoringSession::can_redo() const { return !redo_history.empty(); }

void MxtCarAuthoringSession::push_undo_snapshot()
{
	PackedByteArray bytes;
	String error;
	if (!serialize_document(bytes, error)) return;
	if (undo_history.size() == MAX_HISTORY) undo_history.erase(undo_history.begin());
	undo_history.push_back(bytes);
}

bool MxtCarAuthoringSession::restore_history_snapshot(const PackedByteArray &bytes)
{
	Ref<MxtCarAuthoringSession> parsed;
	parsed.instantiate();
	parsed->undo_history.clear();
	parsed->redo_history.clear();
	String error;
	if (!read_document(bytes, *parsed.ptr(), error)) return false;
	curves = std::move(parsed->curves);
	s_boost_values = parsed->s_boost_values;
	tilt_corners = parsed->tilt_corners;
	wall_corners = parsed->wall_corners;
	state_flags = parsed->state_flags;
	dirty = true;
	return true;
}

bool MxtCarAuthoringSession::undo()
{
	if (undo_history.empty()) return false;
	PackedByteArray current;
	String error;
	if (!serialize_document(current, error)) return false;
	if (redo_history.size() == MAX_HISTORY) redo_history.erase(redo_history.begin());
	redo_history.push_back(current);
	const PackedByteArray snapshot = undo_history.back();
	undo_history.pop_back();
	return restore_history_snapshot(snapshot);
}

bool MxtCarAuthoringSession::redo()
{
	if (redo_history.empty()) return false;
	PackedByteArray current;
	String error;
	if (!serialize_document(current, error)) return false;
	if (undo_history.size() == MAX_HISTORY) undo_history.erase(undo_history.begin());
	undo_history.push_back(current);
	const PackedByteArray snapshot = redo_history.back();
	redo_history.pop_back();
	return restore_history_snapshot(snapshot);
}
