#include "car/car_authoring_session.h"

#include "content/content_manifest.h"
#include "content/content_validator.h"
#include "content/glb_validator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

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

static String global_path(const String &path)
{
	if (path.begins_with("res://") || path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(path).replace("\\", "/").simplify_path();
	}
	return path.replace("\\", "/").simplify_path();
}

static bool write_text_file(const String &path, const String &text)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) return false;
	file->store_string(text);
	return file->get_error() == OK;
}

static bool is_valid_text_field(const String &value, int64_t maximum_length, bool allow_empty)
{
	if ((!allow_empty && value.is_empty()) || value.length() > maximum_length) return false;
	for (int64_t i = 0; i < value.length(); ++i) {
		if (value[i] == 0 || value[i] < 0x20 || value[i] == 0x7f) return false;
	}
	return true;
}

static Array vector3_array(const Vector3 &value)
{
	Array output;
	output.push_back(value.x);
	output.push_back(value.y);
	output.push_back(value.z);
	return output;
}

static bool dictionary_vector3(
		const Dictionary &value,
		const char *key,
		double minimum,
		double maximum,
		Vector3 &out)
{
	if (!value.has(key)) return false;
	if (value[key].get_type() == Variant::VECTOR3) {
		out = value[key];
	} else if (value[key].get_type() == Variant::ARRAY) {
		const Array array = value[key];
		if (array.size() != 3) return false;
		double components[3];
		for (int64_t i = 0; i < 3; ++i) {
			if (array[i].get_type() != Variant::INT && array[i].get_type() != Variant::FLOAT) return false;
			components[i] = array[i].get_type() == Variant::INT
					? static_cast<double>(static_cast<int64_t>(array[i]))
					: static_cast<double>(array[i]);
		}
		out = Vector3(components[0], components[1], components[2]);
	} else {
		return false;
	}
	return out.is_finite() && out.x >= minimum && out.x <= maximum &&
			out.y >= minimum && out.y <= maximum && out.z >= minimum && out.z <= maximum;
}

static bool preflight_gltf_source(const String &path, String &out_error)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null() || file->get_length() > 48u * 1024u * 1024u) {
		out_error = "glTF source is missing or exceeds 48 MiB";
		return false;
	}
	const PackedByteArray bytes = file->get_buffer(file->get_length());
	std::vector<String> audit_errors;
	if (!mxt::content::audit_json_members(bytes, audit_errors)) {
		out_error = audit_errors.empty() ? String("glTF JSON is invalid") : audit_errors.front();
		return false;
	}
	const Variant parsed = JSON::parse_string(String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size()));
	if (parsed.get_type() != Variant::DICTIONARY) {
		out_error = "glTF root must be an object";
		return false;
	}
	const Dictionary root = parsed;
	if (root.has("extensionsUsed") && root["extensionsUsed"].get_type() == Variant::ARRAY &&
			!static_cast<Array>(root["extensionsUsed"]).is_empty()) {
		out_error = "glTF source extensions are unsupported";
		return false;
	}
	const String base = path.get_base_dir().replace("\\", "/").simplify_path();
	uint64_t total_dependency_bytes = static_cast<uint64_t>(bytes.size());
	for (const char *collection_name : {"buffers", "images"}) {
		if (!root.has(collection_name)) continue;
		if (root[collection_name].get_type() != Variant::ARRAY) {
			out_error = String("glTF ") + collection_name + " must be an array";
			return false;
		}
		const Array collection = root[collection_name];
		for (int64_t i = 0; i < collection.size(); ++i) {
			if (collection[i].get_type() != Variant::DICTIONARY) continue;
			const Dictionary entry = collection[i];
			if (!entry.has("uri")) continue;
			if (entry["uri"].get_type() != Variant::STRING) {
				out_error = "glTF dependency URI must be a string";
				return false;
			}
			const String uri = entry["uri"];
			if (uri.begins_with("data:")) continue;
			if (uri.is_empty() || uri.is_absolute_path() || uri.contains("\\") || uri.contains(":") ||
					uri.contains("?") || uri.contains("#")) {
				out_error = "glTF dependency URI is not a local canonical relative path";
				return false;
			}
			const String dependency = base.path_join(uri).simplify_path();
			if (!dependency.begins_with(base + String("/")) || !FileAccess::file_exists(dependency)) {
				out_error = "glTF dependency escapes the import root or is missing";
				return false;
			}
			Ref<FileAccess> dependency_file = FileAccess::open(dependency, FileAccess::READ);
			if (dependency_file.is_null() || dependency_file->get_length() > 48u * 1024u * 1024u - std::min<uint64_t>(total_dependency_bytes, 48u * 1024u * 1024u)) {
				out_error = "glTF source dependencies exceed 48 MiB";
				return false;
			}
			total_dependency_bytes += dependency_file->get_length();
		}
	}
	return true;
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
	ClassDB::bind_method(D_METHOD("import_model", "source_path", "draft_root"), &MxtCarAuthoringSession::import_model);
	ClassDB::bind_method(D_METHOD("load_vehicle_package", "package_root"), &MxtCarAuthoringSession::load_vehicle_package);
	ClassDB::bind_method(D_METHOD("build_vehicle_package", "package_root", "preview_png_path", "title", "description", "author_name"), &MxtCarAuthoringSession::build_vehicle_package);
	ClassDB::bind_method(D_METHOD("get_model_path"), &MxtCarAuthoringSession::get_model_path);
	ClassDB::bind_method(D_METHOD("get_model_transform"), &MxtCarAuthoringSession::get_model_transform);
	ClassDB::bind_method(D_METHOD("set_model_transform", "value"), &MxtCarAuthoringSession::set_model_transform);
	ClassDB::bind_method(D_METHOD("get_model_surfaces"), &MxtCarAuthoringSession::get_model_surfaces);
	ClassDB::bind_method(D_METHOD("get_material_setup"), &MxtCarAuthoringSession::get_material_setup);
	ClassDB::bind_method(D_METHOD("set_material_setup", "value"), &MxtCarAuthoringSession::set_material_setup);
	ClassDB::bind_method(D_METHOD("get_thrusters"), &MxtCarAuthoringSession::get_thrusters);
	ClassDB::bind_method(D_METHOD("set_thrusters", "value"), &MxtCarAuthoringSession::set_thrusters);
	ClassDB::bind_method(D_METHOD("sample_effective_stats", "machine_setting", "technique", "technique_intensity", "boost_state"), &MxtCarAuthoringSession::sample_effective_stats);
	ClassDB::bind_method(D_METHOD("simulate_speed_preview", "machine_setting", "starting_speed_kmh", "frame_perfect_boosting", "technique", "technique_intensity", "boost_state"), &MxtCarAuthoringSession::simulate_speed_preview);
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

bool MxtCarAuthoringSession::is_safe_draft_root(const String &path, String &out_global_path)
{
	const String allowed_root = global_path("user://vehicle_drafts");
	out_global_path = global_path(path);
	return !out_global_path.is_empty() && out_global_path.begins_with(allowed_root + String("/"));
}

Dictionary MxtCarAuthoringSession::import_model(const String &source_path, const String &draft_root)
{
	String root;
	if (!is_safe_draft_root(draft_root, root)) {
		return result_dictionary(false, single_error("vehicle draft root must be below user://vehicle_drafts"), {});
	}
	const String source = global_path(source_path);
	const String extension = source.get_extension().to_lower();
	if (extension != "glb" && extension != "gltf") {
		return result_dictionary(false, single_error("vehicle model source must be .glb or .gltf"), {});
	}
	if (DirAccess::make_dir_recursive_absolute(root.path_join("package/vehicle")) != OK) {
		return result_dictionary(false, single_error("could not create vehicle draft directory"), {});
	}
	const String destination = root.path_join("package/vehicle/model.glb");
	const String temporary = root.path_join("package/vehicle/model.importing.glb");
	DirAccess::remove_absolute(temporary);
	std::vector<String> validation_errors;
	mxt::content::VehicleGlbInfo model_info;
	if (extension == "glb") {
		if (!mxt::content::validate_glb_file(
				source, mxt::content::ContentType::VEHICLE, validation_errors, &model_info)) {
			PackedStringArray errors;
			for (const String &error : validation_errors) errors.push_back(error);
			return result_dictionary(false, errors, {});
		}
		if (DirAccess::copy_absolute(source, temporary) != OK) {
			return result_dictionary(false, single_error("could not copy vehicle GLB into the draft"), {});
		}
	} else {
		String source_error;
		if (!preflight_gltf_source(source, source_error)) {
			return result_dictionary(false, single_error(source_error), {});
		}
		Ref<GLTFDocument> document;
		document.instantiate();
		Ref<GLTFState> state;
		state.instantiate();
		state->set_base_path(source.get_base_dir());
		const Error load_error = document->append_from_file(source, state, 0, source.get_base_dir());
		if (load_error != OK || document->write_to_filesystem(state, temporary) != OK) {
			DirAccess::remove_absolute(temporary);
			return result_dictionary(false, single_error("could not normalize glTF source into a self-contained GLB"), {});
		}
		if (!mxt::content::validate_glb_file(
				temporary, mxt::content::ContentType::VEHICLE, validation_errors, &model_info)) {
			DirAccess::remove_absolute(temporary);
			PackedStringArray errors;
			for (const String &error : validation_errors) errors.push_back(error);
			return result_dictionary(false, errors, {});
		}
	}
	DirAccess::remove_absolute(destination);
	if (DirAccess::rename_absolute(temporary, destination) != OK) {
		DirAccess::remove_absolute(temporary);
		return result_dictionary(false, single_error("could not install normalized vehicle GLB into the draft"), {});
	}
	model_path = destination;
	body_surfaces.clear();
	body_surfaces.reserve(model_info.surfaces.size());
	albedo_surface = -1;
	normal_surface = -1;
	paint_mask_surface = -1;
	for (size_t i = 0; i < model_info.surfaces.size(); ++i) {
		body_surfaces.push_back(static_cast<int32_t>(i));
		if (albedo_surface < 0 && model_info.surfaces[i].has_albedo_texture) albedo_surface = static_cast<int32_t>(i);
		if (normal_surface < 0 && model_info.surfaces[i].has_normal_texture) normal_surface = static_cast<int32_t>(i);
		if (paint_mask_surface < 0 && model_info.surfaces[i].has_paint_mask_texture) paint_mask_surface = static_cast<int32_t>(i);
	}
	dirty = true;
	return result_dictionary(true, {}, {});
}

Dictionary MxtCarAuthoringSession::load_vehicle_package(const String &package_root)
{
	const String root = global_path(package_root);
	mxt::content::ValidatedPackage package;
	std::vector<String> validation_errors;
	if (!mxt::content::validate_package_directory_internal(root, package, validation_errors) ||
			package.manifest.content_type != mxt::content::ContentType::VEHICLE) {
		PackedStringArray errors;
		for (const String &error : validation_errors) errors.push_back(error);
		if (errors.is_empty()) errors.push_back("package is not a vehicle package");
		return result_dictionary(false, errors, {});
	}
	Dictionary loaded = load_file(root.path_join("vehicle/properties.mxt_car_props"));
	if (!static_cast<bool>(loaded.get("valid", false))) return loaded;
	model_path = root.path_join("vehicle/model.glb");
	set_model_transform(package.visual_metadata.get("model_transform", Dictionary()));
	Dictionary material_setup = package.visual_metadata.get("material_inputs", Dictionary());
	material_setup["body_surfaces"] = package.visual_metadata.get("body_surfaces", Array());
	set_material_setup(material_setup);
	set_thrusters(package.visual_metadata.get("thrusters", Array()));
	undo_history.clear();
	redo_history.clear();
	dirty = false;
	loaded["title"] = package.manifest.title;
	loaded["description"] = package.manifest.description;
	loaded["author_name"] = package.manifest.author_name;
	loaded["package_digest"] = package.package_digest;
	loaded["gameplay_digest"] = package.gameplay_digest;
	return loaded;
}

Dictionary MxtCarAuthoringSession::build_vehicle_package(
		const String &package_root,
		const String &preview_png_path,
		const String &title,
		const String &description,
		const String &author_name)
{
	String root;
	const String draft_root = package_root.trim_suffix("/package").trim_suffix("\\package");
	if (!is_safe_draft_root(draft_root, root) || global_path(package_root) != root.path_join("package")) {
		return result_dictionary(false, single_error("vehicle package output must be the package directory of a user draft"), {});
	}
	root = root.path_join("package");
	if (!is_valid_text_field(title, 128, false) || !is_valid_text_field(description, 8000, true) ||
			!is_valid_text_field(author_name, 64, false)) {
		return result_dictionary(false, single_error("vehicle title, description, or author text is invalid"), {});
	}
	const String use_model_path = root.path_join("vehicle/model.glb");
	if (!FileAccess::file_exists(use_model_path)) {
		return result_dictionary(false, single_error("vehicle draft has no imported model"), {});
	}
	if (DirAccess::make_dir_recursive_absolute(root.path_join("vehicle")) != OK) {
		return result_dictionary(false, single_error("could not create vehicle package directory"), {});
	}
	Dictionary properties_result = save_file(root.path_join("vehicle/properties.mxt_car_props"));
	if (!static_cast<bool>(properties_result.get("valid", false))) return properties_result;
	Dictionary transform;
	transform["translation"] = vector3_array(model_translation);
	transform["rotation_degrees"] = vector3_array(model_rotation_degrees);
	transform["scale"] = vector3_array(model_scale);
	Array thruster_values;
	for (const Thruster &thruster : thrusters) {
		Dictionary value;
		value["position"] = vector3_array(thruster.position);
		value["rotation_degrees"] = vector3_array(thruster.rotation_degrees);
		value["scale"] = thruster.scale;
		thruster_values.push_back(value);
	}
	Dictionary visual;
	visual["format_revision"] = 1;
	visual["model_transform"] = transform;
	Array body_surface_values;
	for (const int32_t surface : body_surfaces) body_surface_values.push_back(surface);
	visual["body_surfaces"] = body_surface_values;
	Dictionary material_inputs;
	material_inputs["albedo_surface"] = albedo_surface;
	material_inputs["normal_surface"] = normal_surface;
	material_inputs["paint_mask_surface"] = paint_mask_surface;
	visual["material_inputs"] = material_inputs;
	visual["thrusters"] = thruster_values;
	if (!write_text_file(root.path_join("vehicle/visual.json"), JSON::stringify(visual, "  ", true, true))) {
		return result_dictionary(false, single_error("could not write vehicle visual metadata"), {});
	}
	if (DirAccess::copy_absolute(global_path(preview_png_path), root.path_join("preview.png")) != OK) {
		return result_dictionary(false, single_error("could not copy vehicle preview PNG"), {});
	}
	Dictionary payload;
	payload["model"] = "vehicle/model.glb";
	payload["properties"] = "vehicle/properties.mxt_car_props";
	payload["visual_metadata"] = "vehicle/visual.json";
	Dictionary hashes;
	for (const String &path : {String("vehicle/model.glb"), String("vehicle/properties.mxt_car_props"), String("vehicle/visual.json"), String("preview.png")}) {
		hashes[path] = FileAccess::get_sha256(root.path_join(path));
	}
	Dictionary manifest;
	manifest["format_revision"] = static_cast<int64_t>(mxt::content::PACKAGE_FORMAT_REVISION);
	manifest["content_type"] = "vehicle";
	manifest["title"] = title;
	manifest["description"] = description;
	manifest["author_name"] = author_name;
	manifest["payload"] = payload;
	manifest["payload_sha256"] = hashes;
	if (!write_text_file(root.path_join("manifest.json"), JSON::stringify(manifest, "  ", true, true))) {
		return result_dictionary(false, single_error("could not write vehicle package manifest"), {});
	}
	mxt::content::ValidatedPackage package;
	std::vector<String> validation_errors;
	if (!mxt::content::validate_package_directory_internal(root, package, validation_errors)) {
		PackedStringArray errors;
		for (const String &error : validation_errors) errors.push_back(error);
		return result_dictionary(false, errors, {});
	}
	Dictionary result = result_dictionary(true, {}, properties_result.get("warnings", PackedStringArray()));
	result["package_path"] = root;
	result["package_digest"] = package.package_digest;
	result["gameplay_digest"] = package.gameplay_digest;
	dirty = false;
	return result;
}

String MxtCarAuthoringSession::get_model_path() const { return model_path; }

Dictionary MxtCarAuthoringSession::get_model_transform() const
{
	Dictionary result;
	result["translation"] = model_translation;
	result["rotation_degrees"] = model_rotation_degrees;
	result["scale"] = model_scale;
	return result;
}

bool MxtCarAuthoringSession::set_model_transform(const Dictionary &value)
{
	Vector3 translation;
	Vector3 rotation;
	Vector3 scale;
	if (!dictionary_vector3(value, "translation", -1000.0, 1000.0, translation) ||
			!dictionary_vector3(value, "rotation_degrees", -3600.0, 3600.0, rotation) ||
			!dictionary_vector3(value, "scale", 0.001, 100.0, scale)) return false;
	model_translation = translation;
	model_rotation_degrees = rotation;
	model_scale = scale;
	dirty = true;
	return true;
}

Array MxtCarAuthoringSession::get_model_surfaces() const
{
	Array result;
	if (model_path.is_empty()) return result;
	mxt::content::VehicleGlbInfo model_info;
	std::vector<String> errors;
	if (!mxt::content::validate_glb_file(
			model_path, mxt::content::ContentType::VEHICLE, errors, &model_info)) return result;
	for (size_t i = 0; i < model_info.surfaces.size(); ++i) {
		const mxt::content::VehicleGlbSurface &surface = model_info.surfaces[i];
		Dictionary value;
		value["index"] = static_cast<int64_t>(i);
		value["name"] = surface.name;
		value["has_albedo_texture"] = surface.has_albedo_texture;
		value["has_normal_texture"] = surface.has_normal_texture;
		value["has_paint_mask_texture"] = surface.has_paint_mask_texture;
		result.push_back(value);
	}
	return result;
}

Dictionary MxtCarAuthoringSession::get_material_setup() const
{
	Dictionary result;
	Array surfaces;
	for (const int32_t surface : body_surfaces) surfaces.push_back(surface);
	result["body_surfaces"] = surfaces;
	result["albedo_surface"] = albedo_surface;
	result["normal_surface"] = normal_surface;
	result["paint_mask_surface"] = paint_mask_surface;
	return result;
}

bool MxtCarAuthoringSession::set_material_setup(const Dictionary &value)
{
	if (!value.has("body_surfaces") || value["body_surfaces"].get_type() != Variant::ARRAY ||
			!value.has("albedo_surface") || value["albedo_surface"].get_type() != Variant::INT ||
			!value.has("normal_surface") || value["normal_surface"].get_type() != Variant::INT ||
			!value.has("paint_mask_surface") || value["paint_mask_surface"].get_type() != Variant::INT) return false;
	mxt::content::VehicleGlbInfo model_info;
	std::vector<String> errors;
	if (!mxt::content::validate_glb_file(
			model_path, mxt::content::ContentType::VEHICLE, errors, &model_info)) return false;
	const Array input_surfaces = value["body_surfaces"];
	if (input_surfaces.is_empty() || input_surfaces.size() > static_cast<int64_t>(model_info.surfaces.size())) return false;
	std::vector<uint8_t> selected(model_info.surfaces.size(), 0);
	std::vector<int32_t> replacement;
	replacement.reserve(input_surfaces.size());
	for (int64_t i = 0; i < input_surfaces.size(); ++i) {
		if (input_surfaces[i].get_type() != Variant::INT) return false;
		const int64_t surface = static_cast<int64_t>(input_surfaces[i]);
		if (surface < 0 || surface >= static_cast<int64_t>(model_info.surfaces.size()) ||
				selected[static_cast<size_t>(surface)] != 0) return false;
		selected[static_cast<size_t>(surface)] = 1;
		replacement.push_back(static_cast<int32_t>(surface));
	}
	auto valid_input = [&](int64_t surface, bool mxt::content::VehicleGlbSurface::*texture_member) {
		return surface == -1 || (surface >= 0 && surface < static_cast<int64_t>(model_info.surfaces.size()) &&
				(model_info.surfaces[static_cast<size_t>(surface)].*texture_member));
	};
	const int64_t new_albedo = static_cast<int64_t>(value["albedo_surface"]);
	const int64_t new_normal = static_cast<int64_t>(value["normal_surface"]);
	const int64_t new_paint_mask = static_cast<int64_t>(value["paint_mask_surface"]);
	if (!valid_input(new_albedo, &mxt::content::VehicleGlbSurface::has_albedo_texture) ||
			!valid_input(new_normal, &mxt::content::VehicleGlbSurface::has_normal_texture) ||
			!valid_input(new_paint_mask, &mxt::content::VehicleGlbSurface::has_paint_mask_texture)) return false;
	body_surfaces = std::move(replacement);
	albedo_surface = static_cast<int32_t>(new_albedo);
	normal_surface = static_cast<int32_t>(new_normal);
	paint_mask_surface = static_cast<int32_t>(new_paint_mask);
	dirty = true;
	return true;
}

Array MxtCarAuthoringSession::get_thrusters() const
{
	Array result;
	for (const Thruster &thruster : thrusters) {
		Dictionary value;
		value["position"] = thruster.position;
		value["rotation_degrees"] = thruster.rotation_degrees;
		value["scale"] = thruster.scale;
		result.push_back(value);
	}
	return result;
}

bool MxtCarAuthoringSession::set_thrusters(const Array &value)
{
	if (value.size() > 8) return false;
	std::vector<Thruster> replacement;
	replacement.reserve(value.size());
	for (int64_t i = 0; i < value.size(); ++i) {
		if (value[i].get_type() != Variant::DICTIONARY) return false;
		const Dictionary input = value[i];
		Thruster thruster;
		if (!dictionary_vector3(input, "position", -100.0, 100.0, thruster.position) ||
				!dictionary_vector3(input, "rotation_degrees", -3600.0, 3600.0, thruster.rotation_degrees) ||
				!input.has("scale") ||
				(input["scale"].get_type() != Variant::INT && input["scale"].get_type() != Variant::FLOAT)) return false;
		const double scale_value = input["scale"].get_type() == Variant::INT
				? static_cast<double>(static_cast<int64_t>(input["scale"]))
				: static_cast<double>(input["scale"]);
		if (!std::isfinite(scale_value) || scale_value < 0.01 || scale_value > 10.0) return false;
		thruster.scale = static_cast<float>(scale_value);
		replacement.push_back(thruster);
	}
	thrusters = std::move(replacement);
	dirty = true;
	return true;
}

Dictionary MxtCarAuthoringSession::sample_effective_stats(
		double machine_setting,
		const String &technique,
		double technique_intensity,
		const String &boost_state) const
{
	const double setting = std::clamp(machine_setting, 0.0, 1.0);
	const double intensity = std::clamp(technique_intensity, 0.0, 1.0);
	const int32_t technique_layer = technique == "mts"
			? CAR_CURVE_MTS
			: (technique == "quickturn" ? CAR_CURVE_QUICKTURN : -1);
	int32_t boost_layer = -1;
	if (boost_state == "none") boost_layer = CAR_CURVE_NO_BOOST;
	else if (boost_state == "manual") boost_layer = CAR_CURVE_MANUAL_BOOST;
	else if (boost_state == "dashplate" || boost_state == "s_boost_dashplate") boost_layer = CAR_CURVE_DASHPLATE_BOOST;
	else if (boost_state == "stacked") boost_layer = CAR_CURVE_STACKED_BOOST;
	const bool s_boost = boost_state == "s_boost" || boost_state == "s_boost_dashplate";
	Dictionary output;
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		const String name = PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat));
		const bool live = PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat));
		double value = s_boost && live ? s_boost_values[stat] : sample_curve("base", name, setting);
		if (live && technique_layer >= 0) {
			const double authored = sample_curve(LAYER_NAMES[technique_layer], name, setting);
			value *= 1.0 + (authored - 1.0) * intensity;
		}
		if (live && boost_layer >= 0) value *= sample_curve(LAYER_NAMES[boost_layer], name, setting);
		output[name] = value;
	}
	return output;
}

Dictionary MxtCarAuthoringSession::simulate_speed_preview(
		double machine_setting,
		double starting_speed_kmh,
		bool frame_perfect_boosting,
		const String &technique,
		double technique_intensity,
		const String &boost_state) const
{
	Dictionary no_boost_stats = sample_effective_stats(
			machine_setting, technique, technique_intensity,
			frame_perfect_boosting ? String("none") : boost_state);
	Dictionary manual_boost_stats = frame_perfect_boosting
			? sample_effective_stats(machine_setting, technique, technique_intensity, "manual")
			: no_boost_stats;
	const bool s_boost_active = !frame_perfect_boosting &&
			(boost_state == "s_boost" || boost_state == "s_boost_dashplate");
	const double tick_rate = 60.0;
	const double weight = no_boost_stats["weight_kg"];
	Dictionary result;
	if (!std::isfinite(weight) || std::abs(weight) < 0.0001 || !std::isfinite(starting_speed_kmh)) {
		result["error"] = "speed preview requires finite nonzero weight and starting speed";
		return result;
	}
	double speed = starting_speed_kmh * weight / 216.0;
	double base_speed = speed / weight;
	double turbo = 0.0;
	int32_t manual_frames = 0;
	int32_t boost_count = 0;
	PackedFloat32Array times;
	PackedFloat32Array speeds;
	PackedFloat32Array turbos;
	times.resize(1800);
	speeds.resize(1800);
	turbos.resize(1800);
	double peak_speed = 0.0;
	double peak_turbo = 0.0;
	for (int32_t frame = 0; frame < 1800; ++frame) {
		bool started_manual_boost = false;
		if (frame_perfect_boosting && manual_frames == 0) {
			const double duration = std::max(static_cast<double>(manual_boost_stats["manual_boost_duration_seconds"]), 0.0);
			manual_frames = std::max(static_cast<int32_t>(duration * tick_rate + 0.5), 0);
			if (manual_frames > 0) {
				turbo += static_cast<double>(manual_boost_stats["manual_turbo_gain"]);
				++boost_count;
				started_manual_boost = true;
			}
		}
		const bool manual_active = manual_frames > 0;
		const Dictionary stats = manual_active ? manual_boost_stats : no_boost_stats;
		turbo -= (static_cast<double>(stats["turbo_flat_loss_per_second"]) +
				turbo * static_cast<double>(stats["turbo_percent_loss_per_second"])) / tick_rate;
		turbo = std::max(turbo, 0.0);
		double target_speed = 40.0 * static_cast<double>(stats["acceleration"]) / 348.0;
		target_speed *= static_cast<double>(stats["drive_target_speed_multiplier"]);
		target_speed += base_speed;
		const double normalized_speed = speed / weight;
		double speed_difference = target_speed - normalized_speed;
		const double denominator = 36.0 + 40.0 * static_cast<double>(stats["max_speed"]) +
				turbo * static_cast<double>(stats["turbo_top_speed_effect"]);
		double speed_factor = std::abs(denominator) > 0.0001 ? std::max(target_speed / denominator, 0.0) : 0.0;
		double accel_magnitude = speed_factor * 4.0 * static_cast<double>(stats["acceleration"]) *
				(0.6 + static_cast<double>(stats["acceleration"])) *
				static_cast<double>(stats["acceleration_response_multiplier"]);
		if (started_manual_boost) accel_magnitude = 0.0;
		double new_base_speed = target_speed - speed_difference * accel_magnitude;
		const double released_target_speed = base_speed;
		const double released_difference = released_target_speed - normalized_speed;
		const double released_factor = std::abs(denominator) > 0.0001 ? std::max(released_target_speed / denominator, 0.0) : 0.0;
		double released_magnitude = released_factor * 4.0 * static_cast<double>(stats["acceleration"]) *
				(0.6 + static_cast<double>(stats["acceleration"])) *
				static_cast<double>(stats["acceleration_response_multiplier"]);
		if (started_manual_boost) released_magnitude = 0.0;
		const double released_new_base_speed = released_target_speed - released_difference * released_magnitude * 0.05;
		if (released_new_base_speed > new_base_speed) {
			target_speed = released_target_speed;
			speed_difference = released_difference;
			new_base_speed = released_new_base_speed;
		}
		base_speed = std::max(new_base_speed - static_cast<double>(stats["drag"]), 0.0);
		if (s_boost_active) base_speed += static_cast<double>(stats["s_boost_base_speed_add_per_second"]) / tick_rate;
		double thrust = 1000.0 * static_cast<double>(stats["forward_thrust_multiplier"]) * speed_difference;
		if (normalized_speed < 0.0 || speed_difference < 0.0) thrust *= 0.15;
		speed += thrust;
		const double speed_weight_ratio = speed / weight;
		double speed_kmh = 216.0 * speed_weight_ratio;
		if (speed_kmh < 2.0) {
			speed = 0.0;
			speed_kmh = 0.0;
		} else {
			speed -= speed_weight_ratio * speed_weight_ratio * 8.0;
			speed_kmh = 216.0 * speed / weight;
		}
		if (!std::isfinite(speed_kmh) || !std::isfinite(turbo)) {
			result["error"] = "speed preview diverged to a non-finite value";
			return result;
		}
		times.set(frame, static_cast<float>(frame / tick_rate));
		speeds.set(frame, static_cast<float>(speed_kmh));
		turbos.set(frame, static_cast<float>(turbo));
		peak_speed = std::max(peak_speed, speed_kmh);
		peak_turbo = std::max(peak_turbo, turbo);
		if (manual_active) --manual_frames;
	}
	const double terminal_speed = speeds[speeds.size() - 1];
	const double tolerance = std::max(std::abs(terminal_speed) * 0.001, 0.01);
	int64_t settle_index = speeds.size() - 1;
	for (int64_t i = speeds.size() - 1; i >= 0; --i) {
		if (std::abs(speeds[i] - terminal_speed) > tolerance) {
			settle_index = std::min<int64_t>(i + 1, speeds.size() - 1);
			break;
		}
		settle_index = i;
	}
	result["times"] = times;
	result["speeds_kmh"] = speeds;
	result["turbos"] = turbos;
	result["terminal_speed_kmh"] = terminal_speed;
	result["peak_speed_kmh"] = peak_speed;
	result["peak_turbo"] = peak_turbo;
	result["settle_time_seconds"] = static_cast<double>(times[settle_index]);
	result["boost_count"] = boost_count;
	return result;
}
