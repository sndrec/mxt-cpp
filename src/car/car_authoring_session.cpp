#include "car/car_authoring_session.h"
#include "car/car_property_derivation.h"
#include "car/car_stat_metadata.h"

#include "content/content_manifest.h"
#include "content/content_validator.h"
#include "content/glb_validator.h"
#include "core/native_file_utils.h"

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
static constexpr const char *SPECIAL_LAYER_NAMES[CAR_AUTHORING_SPECIAL_LAYER_COUNT] = {
	"mts", "quickturn", "no_boost", "manual_boost", "dashplate_boost", "stacked_boost", "s_boost"
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

} // namespace

PackedStringArray MxtCarAuthoringSession::single_error(const String &message)
{
	PackedStringArray errors;
	errors.push_back(message);
	return errors;
}

Dictionary MxtCarAuthoringSession::result_dictionary(bool valid, const PackedStringArray &errors, const PackedStringArray &warnings)
{
	Dictionary result;
	result["valid"] = valid;
	result["errors"] = errors;
	result["warnings"] = warnings;
	return result;
}

String MxtCarAuthoringSession::global_path(const String &path)
{
	if (path.begins_with("res://") || path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(path).replace("\\", "/").simplify_path();
	}
	return path.replace("\\", "/").simplify_path();
}

bool MxtCarAuthoringSession::write_text_file(const String &path, const String &text)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) return false;
	file->store_string(text);
	return file->get_error() == OK;
}

bool MxtCarAuthoringSession::is_valid_text_field(
		const String &value,
		int64_t maximum_length,
		bool allow_empty,
		bool allow_line_breaks)
{
	if ((!allow_empty && value.is_empty()) || value.length() > maximum_length) return false;
	for (int64_t i = 0; i < value.length(); ++i) {
		const char32_t codepoint = value[i];
		if (allow_line_breaks && codepoint == '\n') continue;
		if (codepoint == 0 || codepoint < 0x20 || codepoint == 0x7f) return false;
	}
	return true;
}

Array MxtCarAuthoringSession::vector3_array(const Vector3 &value)
{
	Array output;
	output.push_back(value.x);
	output.push_back(value.y);
	output.push_back(value.z);
	return output;
}

bool MxtCarAuthoringSession::dictionary_vector3(
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

bool MxtCarAuthoringSession::preflight_gltf_source(const String &path, String &out_error)
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
	ClassDB::bind_method(D_METHOD("is_special_derived", "layer_name", "stat_name"), &MxtCarAuthoringSession::is_special_derived);
	ClassDB::bind_method(D_METHOD("make_special_custom", "layer_name", "stat_name"), &MxtCarAuthoringSession::make_special_custom);
	ClassDB::bind_method(D_METHOD("revert_special_derived", "layer_name", "stat_name"), &MxtCarAuthoringSession::revert_special_derived);
	ClassDB::bind_method(D_METHOD("get_authoring_intent"), &MxtCarAuthoringSession::get_authoring_intent);
	ClassDB::bind_method(D_METHOD("set_authoring_intent", "value"), &MxtCarAuthoringSession::set_authoring_intent);
	ClassDB::bind_method(D_METHOD("get_tilt_corners"), &MxtCarAuthoringSession::get_tilt_corners);
	ClassDB::bind_method(D_METHOD("get_wall_corners"), &MxtCarAuthoringSession::get_wall_corners);
	ClassDB::bind_method(D_METHOD("get_collision_measurements"), &MxtCarAuthoringSession::get_collision_measurements);
	ClassDB::bind_method(D_METHOD("set_collision_measurements", "value"), &MxtCarAuthoringSession::set_collision_measurements);
	ClassDB::bind_method(D_METHOD("get_state_flags"), &MxtCarAuthoringSession::get_state_flags);
	ClassDB::bind_method(D_METHOD("set_state_flags", "value"), &MxtCarAuthoringSession::set_state_flags);
	ClassDB::bind_method(D_METHOD("is_dirty"), &MxtCarAuthoringSession::is_dirty);
	ClassDB::bind_method(D_METHOD("clear_dirty"), &MxtCarAuthoringSession::clear_dirty);
	ClassDB::bind_method(D_METHOD("can_undo"), &MxtCarAuthoringSession::can_undo);
	ClassDB::bind_method(D_METHOD("can_redo"), &MxtCarAuthoringSession::can_redo);
	ClassDB::bind_method(D_METHOD("begin_edit_transaction"), &MxtCarAuthoringSession::begin_edit_transaction);
	ClassDB::bind_method(D_METHOD("end_edit_transaction"), &MxtCarAuthoringSession::end_edit_transaction);
	ClassDB::bind_method(D_METHOD("cancel_edit_transaction"), &MxtCarAuthoringSession::cancel_edit_transaction);
	ClassDB::bind_method(D_METHOD("undo"), &MxtCarAuthoringSession::undo);
	ClassDB::bind_method(D_METHOD("redo"), &MxtCarAuthoringSession::redo);
	ClassDB::bind_method(D_METHOD("import_model", "source_path", "draft_root"), &MxtCarAuthoringSession::import_model);
	ClassDB::bind_method(D_METHOD("load_vehicle_package", "package_root"), &MxtCarAuthoringSession::load_vehicle_package);
	ClassDB::bind_method(
			D_METHOD("build_vehicle_package", "package_root", "preview_png_path", "title", "description", "author_name", "validate_package"),
			&MxtCarAuthoringSession::build_vehicle_package,
			DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_model_path"), &MxtCarAuthoringSession::get_model_path);
	ClassDB::bind_method(D_METHOD("get_model_transform"), &MxtCarAuthoringSession::get_model_transform);
	ClassDB::bind_method(D_METHOD("set_model_transform", "value"), &MxtCarAuthoringSession::set_model_transform);
	ClassDB::bind_method(D_METHOD("get_model_surfaces"), &MxtCarAuthoringSession::get_model_surfaces);
	ClassDB::bind_method(D_METHOD("get_model_resource_usage"), &MxtCarAuthoringSession::get_model_resource_usage);
	ClassDB::bind_method(D_METHOD("get_material_setup"), &MxtCarAuthoringSession::get_material_setup);
	ClassDB::bind_method(D_METHOD("set_material_setup", "value"), &MxtCarAuthoringSession::set_material_setup);
	ClassDB::bind_method(D_METHOD("get_manual_boost_volume_db"), &MxtCarAuthoringSession::get_manual_boost_volume_db);
	ClassDB::bind_method(D_METHOD("set_manual_boost_volume_db", "value"), &MxtCarAuthoringSession::set_manual_boost_volume_db);
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

int32_t MxtCarAuthoringSession::special_layer_index(const String &layer_name)
{
	for (uint8_t layer = 0; layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT; ++layer) {
		if (layer_name == SPECIAL_LAYER_NAMES[layer]) return layer;
	}
	return -1;
}

double MxtCarAuthoringSession::sample_curve_at(uint8_t layer, uint16_t stat, float machine_setting) const
{
	const Curve &curve = curve_at(layer, stat);
	if (curve.empty()) return 0.0;
	if (curve.size() == 1) return curve[0].value;
	const float setting = std::clamp(machine_setting, 0.0f, 1.0f);
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

bool MxtCarAuthoringSession::is_pair_derived(uint8_t special_layer, uint16_t stat) const
{
	if (special_layer >= CAR_AUTHORING_SPECIAL_LAYER_COUNT || stat >= CAR_STAT_COUNT) return false;
	const uint32_t bit = static_cast<uint32_t>(special_layer) * CAR_STAT_COUNT + stat;
	return (derived_pair_mask[bit / 64u] & (UINT64_C(1) << (bit % 64u))) != 0;
}

void MxtCarAuthoringSession::set_pair_derived(uint8_t special_layer, uint16_t stat, bool derived)
{
	if (special_layer >= CAR_AUTHORING_SPECIAL_LAYER_COUNT || stat >= CAR_STAT_COUNT) return;
	const uint32_t bit = static_cast<uint32_t>(special_layer) * CAR_STAT_COUNT + stat;
	const uint64_t mask = UINT64_C(1) << (bit % 64u);
	if (derived) derived_pair_mask[bit / 64u] |= mask;
	else derived_pair_mask[bit / 64u] &= ~mask;
}

void MxtCarAuthoringSession::sample_base_stats(float machine_setting, float out_stats[CAR_STAT_COUNT]) const
{
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		out_stats[stat] = static_cast<float>(sample_curve_at(CAR_CURVE_BASE, stat, machine_setting));
	}
}

float MxtCarAuthoringSession::derived_special_value(uint8_t special_layer, uint16_t stat, float machine_setting) const
{
	float base_stats[CAR_STAT_COUNT];
	const float setting = special_layer == CAR_AUTHORING_S_BOOST ? 0.5f : machine_setting;
	sample_base_stats(setting, base_stats);
	return derive_car_special_value(
			static_cast<CarAuthoringSpecialLayer>(special_layer), static_cast<CarStatId>(stat), base_stats);
}

void MxtCarAuthoringSession::regenerate_derived_pair(uint8_t special_layer, uint16_t stat)
{
	if (!car_special_pair_is_supported(
			static_cast<CarAuthoringSpecialLayer>(special_layer), static_cast<CarStatId>(stat))) return;
	if (special_layer == CAR_AUTHORING_S_BOOST) {
		s_boost_values[stat] = derived_special_value(special_layer, stat, 0.5f);
		return;
	}
	Curve &curve = curve_at(static_cast<uint8_t>(special_layer + 1u), stat);
	const bool sampled_formula = special_layer >= CAR_AUTHORING_MANUAL_BOOST &&
		(stat == CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER ||
		 stat == CAR_STAT_ACCELERATION_RESPONSE_MULTIPLIER ||
		 stat == CAR_STAT_FORWARD_THRUST_MULTIPLIER);
	curve.clear();
	if (!sampled_formula) {
		curve.push_back({0.0f, derived_special_value(special_layer, stat, 0.5f), 0.0f, 0.0f});
		return;
	}
	curve.reserve(101);
	for (uint32_t sample = 0; sample <= 100; ++sample) {
		const float setting = static_cast<float>(sample) * 0.01f;
		curve.push_back({setting, derived_special_value(special_layer, stat, setting), 0.0f, 0.0f});
	}
}

void MxtCarAuthoringSession::regenerate_all_derived_pairs()
{
	for (uint8_t layer = 0; layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT; ++layer) {
		for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
			if (is_pair_derived(layer, stat)) regenerate_derived_pair(layer, stat);
		}
	}
}

void MxtCarAuthoringSession::infer_authoring_intent()
{
	derived_pair_mask.fill(0);
	for (uint8_t layer = 0; layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT; ++layer) {
		for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
			if (!car_special_pair_is_supported(
					static_cast<CarAuthoringSpecialLayer>(layer), static_cast<CarStatId>(stat))) continue;
			bool matches = true;
			if (layer == CAR_AUTHORING_S_BOOST) {
				const float expected = derived_special_value(layer, stat, 0.5f);
				const float tolerance = 1.0e-5f * std::max(1.0f, std::abs(expected));
				matches = std::abs(s_boost_values[stat] - expected) <= tolerance;
			} else {
				for (uint32_t sample = 0; sample <= 100; ++sample) {
					const float setting = static_cast<float>(sample) * 0.01f;
					const float expected = derived_special_value(layer, stat, setting);
					const float actual = static_cast<float>(sample_curve_at(layer + 1u, stat, setting));
					const float tolerance = 1.0e-4f * std::max(1.0f, std::abs(expected));
					if (std::abs(actual - expected) > tolerance) {
						matches = false;
						break;
					}
				}
			}
			set_pair_derived(layer, stat, matches);
		}
	}
}

void MxtCarAuthoringSession::reset_to_defaults()
{
	if (!curves[0].empty()) push_undo_snapshot();
	PhysicsCarProperties defaults;
	for (Curve &curve : curves) curve.clear();
	derived_pair_mask.fill(0);
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		curve_at(CAR_CURVE_BASE, stat).push_back({0.0f, defaults.base_stats[stat], 0.0f, 0.0f});
		s_boost_values[stat] = defaults.s_boost_stats[stat];
		if (!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) continue;
		for (uint8_t layer = CAR_CURVE_MTS; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
			curve_at(layer, stat).push_back({0.0f, defaults.modifier_stats[layer - 1][stat], 0.0f, 0.0f});
		}
		for (uint8_t layer = 0; layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT; ++layer) {
			set_pair_derived(layer, stat, true);
		}
	}
	regenerate_all_derived_pairs();
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
		String &out_error,
		uint16_t *out_schema_stat_count)
{
	PhysicsCarProperties runtime_check;
	uint16_t schema_stat_count = 0;
	if (!PhysicsCarProperties::deserialize_and_sample(
			bytes, 0.5f, runtime_check, out_error, &schema_stat_count)) return false;
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
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		target.s_boost_values[stat] = runtime_check.s_boost_stats[stat];
	}
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
	// Older production Workshop documents do not contain curves for stats appended later.
	// Materialize those defaults in the authoring session so an old vehicle can be edited and
	// saved directly in the current format without a separate migration layer.
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		for (uint8_t layer = CAR_CURVE_BASE; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
			if (layer != CAR_CURVE_BASE &&
				!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) {
				continue;
			}
			Curve &curve = target.curve_at(layer, stat);
			if (!curve.empty()) continue;
			const float value = layer == CAR_CURVE_BASE
				? runtime_check.base_stats[stat]
				: runtime_check.modifier_stats[layer - 1][stat];
			curve.push_back({0.0f, value, 0.0f, 0.0f});
		}
	}
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (!PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat))) {
			target.s_boost_values[stat] = runtime_check.base_stats[stat];
		}
	}
	if (out_schema_stat_count) *out_schema_stat_count = schema_stat_count;
	return true;
}

Dictionary MxtCarAuthoringSession::load_bytes(const PackedByteArray &bytes)
{
	Ref<MxtCarAuthoringSession> parsed;
	parsed.instantiate();
	parsed->undo_history.clear();
	parsed->redo_history.clear();
	String error;
	uint16_t schema_stat_count = 0;
	if (!read_document(bytes, *parsed.ptr(), error, &schema_stat_count)) {
		return result_dictionary(false, single_error(error.is_empty() ? String("car properties document could not be decoded") : error), {});
	}
	curves = std::move(parsed->curves);
	s_boost_values = parsed->s_boost_values;
	tilt_corners = parsed->tilt_corners;
	wall_corners = parsed->wall_corners;
	state_flags = parsed->state_flags;
	infer_authoring_intent();
	undo_history.clear();
	redo_history.clear();
	dirty = false;
	Dictionary result = validate();
	result["source_schema_stat_count"] = schema_stat_count;
	return result;
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
	if (out_errors.is_empty()) {
		const Dictionary measurements = get_collision_measurements();
		if (!static_cast<bool>(measurements.get("valid", false))) {
			out_errors.push_back(measurements.get("error", "collision geometry is unsupported"));
		}
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
		CAR_STAT_MAX_TURN_RATE,
		CAR_STAT_SHIFT_BOOST_COOLDOWN_SECONDS,
		CAR_STAT_DRIFT_ACCEL_BUILDUP_SECONDS,
		CAR_STAT_ATTACK_COOLDOWN_SECONDS,
		CAR_STAT_LANDING_STABILITY,
		CAR_STAT_SHIFT_BOOST_ALIGNMENT_TOLERANCE,
		CAR_STAT_ACCEL_PRESS_GRIP_STRENGTH,
		CAR_STAT_AIR_PITCH_AUTHORITY_MULTIPLIER,
		CAR_STAT_AIR_ANGULAR_DAMPING_MULTIPLIER,
		CAR_STAT_AIR_AUTO_ALIGNMENT_MULTIPLIER,
		CAR_STAT_DRIFT_INITIATION_STEER_THRESHOLD,
		CAR_STAT_HIGH_SPEED_DRAG_MULTIPLIER,
		CAR_STAT_AIR_ORIENTATION_DRAG_MULTIPLIER,
		CAR_STAT_DIRT_DRAG_MULTIPLIER,
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
		const CarStatId stat_id = static_cast<CarStatId>(stat);
		const CarStatMetadata &metadata = get_car_stat_metadata(stat_id);
		Dictionary entry;
		entry["id"] = stat;
		entry["name"] = metadata.name;
		entry["friendly_name"] = metadata.friendly_name;
		entry["explanation"] = metadata.explanation;
		entry["category"] = metadata.authoring_category;
		entry["unit"] = metadata.unit;
		entry["default_value"] = defaults.base_stats[stat];
		entry["supports_live_modifiers"] = PhysicsCarProperties::stat_supports_live_modifiers(stat_id);
		entry["activity"] = car_stat_activity_name(metadata.activity);
		entry["raw_gradeable"] = metadata.raw_gradeable;
		entry["base_direction"] = car_stat_direction_name(metadata.base_direction);
		entry["special_direction"] = car_stat_direction_name(metadata.special_direction);
		entry["historically_derived"] = historical_machine_setting_derives_stat(stat_id);
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
	if (layer != CAR_CURVE_BASE && is_pair_derived(static_cast<uint8_t>(layer - 1), static_cast<uint16_t>(stat))) {
		return result_dictionary(false, single_error("derived special-state values must be made custom before editing"), {});
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
	if (layer == CAR_CURVE_BASE) regenerate_all_derived_pairs();
	clear_redo_after_mutation();
	dirty = true;
	return validate();
}

double MxtCarAuthoringSession::sample_curve(const String &layer_name, const String &stat_name, double machine_setting) const
{
	const int32_t layer = layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	if (layer < 0 || stat < 0) return 0.0;
	return sample_curve_at(
			static_cast<uint8_t>(layer), static_cast<uint16_t>(stat),
			static_cast<float>(std::clamp(machine_setting, 0.0, 1.0)));
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
	if (is_pair_derived(CAR_AUTHORING_S_BOOST, static_cast<uint16_t>(stat))) return false;
	push_undo_snapshot();
	s_boost_values[stat] = static_cast<float>(value);
	clear_redo_after_mutation();
	dirty = true;
	return true;
}

bool MxtCarAuthoringSession::is_special_derived(const String &layer_name, const String &stat_name) const
{
	const int32_t layer = special_layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	return layer >= 0 && stat >= 0 && is_pair_derived(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat));
}

Dictionary MxtCarAuthoringSession::make_special_custom(const String &layer_name, const String &stat_name)
{
	const int32_t layer = special_layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	if (layer < 0 || stat < 0 || !car_special_pair_is_supported(
			static_cast<CarAuthoringSpecialLayer>(layer), static_cast<CarStatId>(stat))) {
		return result_dictionary(false, single_error("unknown or unsupported special-state/stat pair"), {});
	}
	if (!is_pair_derived(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat))) return validate();
	push_undo_snapshot();
	set_pair_derived(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat), false);
	clear_redo_after_mutation();
	dirty = true;
	return validate();
}

Dictionary MxtCarAuthoringSession::revert_special_derived(const String &layer_name, const String &stat_name)
{
	const int32_t layer = special_layer_index(layer_name);
	const int32_t stat = stat_index(stat_name);
	if (layer < 0 || stat < 0 || !car_special_pair_is_supported(
			static_cast<CarAuthoringSpecialLayer>(layer), static_cast<CarStatId>(stat))) {
		return result_dictionary(false, single_error("unknown or unsupported special-state/stat pair"), {});
	}
	push_undo_snapshot();
	set_pair_derived(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat), true);
	regenerate_derived_pair(static_cast<uint8_t>(layer), static_cast<uint16_t>(stat));
	clear_redo_after_mutation();
	dirty = true;
	return validate();
}

Dictionary MxtCarAuthoringSession::get_authoring_intent() const
{
	static constexpr char HEX[] = "0123456789abcdef";
	String encoded;
	for (uint64_t word : derived_pair_mask) {
		char digits[17];
		for (int32_t digit = 15; digit >= 0; --digit) {
			digits[digit] = HEX[word & 0xfu];
			word >>= 4;
		}
		digits[16] = '\0';
		encoded += String(digits);
	}
	Dictionary result;
	result["format_revision"] = 1;
	result["derived_mask_hex"] = encoded;
	return result;
}

bool MxtCarAuthoringSession::normalize_authoring_intent(
		const Dictionary &value,
		uint16_t source_stat_count,
		Dictionary &out_value,
		String &out_error)
{
	out_value.clear();
	out_error = String();
	const Variant revision = value.get("format_revision", Variant());
	const bool valid_revision =
			(revision.get_type() == Variant::INT && static_cast<int64_t>(revision) == 1) ||
			(revision.get_type() == Variant::FLOAT && static_cast<double>(revision) == 1.0);
	if (value.size() != 2 || !value.has("format_revision") ||
			!value.has("derived_mask_hex") ||
			value["derived_mask_hex"].get_type() != Variant::STRING ||
			!valid_revision) {
		out_error = "vehicle authoring intent is malformed";
		return false;
	}
	if (source_stat_count == 0 || source_stat_count > CAR_STAT_COUNT) {
		out_error = "vehicle authoring metadata has no supported properties schema";
		return false;
	}
	const String encoded = value["derived_mask_hex"];
	const uint32_t source_pair_count = CAR_AUTHORING_SPECIAL_LAYER_COUNT * source_stat_count;
	const uint32_t source_mask_words = (source_pair_count + 63u) / 64u;
	if (encoded.length() != static_cast<int64_t>(source_mask_words * 16u)) {
		out_error = "vehicle authoring derived_mask_hex has the wrong length";
		return false;
	}
	std::vector<uint64_t> source_words(source_mask_words, 0);
	for (uint32_t word = 0; word < source_mask_words; ++word) {
		for (uint32_t digit = 0; digit < 16; ++digit) {
			const char32_t c = encoded[static_cast<int64_t>(word * 16u + digit)];
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
				out_error = "vehicle authoring derived_mask_hex must use lowercase hexadecimal";
				return false;
			}
			source_words[word] = (source_words[word] << 4) |
				static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
		}
	}
	for (uint32_t bit = source_pair_count; bit < source_mask_words * 64u; ++bit) {
		if ((source_words[bit / 64u] & (UINT64_C(1) << (bit % 64u))) != 0) {
			out_error = "vehicle authoring derived mask has nonzero padding bits";
			return false;
		}
	}
	std::array<uint64_t, AUTHORING_MASK_WORD_COUNT> current_words{};
	for (uint8_t layer = 0; layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT; ++layer) {
		for (uint16_t stat = 0; stat < source_stat_count; ++stat) {
			const uint32_t source_bit = static_cast<uint32_t>(layer) * source_stat_count + stat;
			if ((source_words[source_bit / 64u] & (UINT64_C(1) << (source_bit % 64u))) == 0) continue;
			if (!car_special_pair_is_supported(
					static_cast<CarAuthoringSpecialLayer>(layer), static_cast<CarStatId>(stat))) {
				out_error = "vehicle authoring derived mask enables an unsupported pair";
				return false;
			}
			const uint32_t current_bit = static_cast<uint32_t>(layer) * CAR_STAT_COUNT + stat;
			current_words[current_bit / 64u] |= UINT64_C(1) << (current_bit % 64u);
		}
		// Appended stats were not authorable by the source schema. They enter the current
		// document as automatic defaults and can be made custom normally after import.
		for (uint16_t stat = source_stat_count; stat < CAR_STAT_COUNT; ++stat) {
			if (!car_special_pair_is_supported(
					static_cast<CarAuthoringSpecialLayer>(layer), static_cast<CarStatId>(stat))) continue;
			const uint32_t current_bit = static_cast<uint32_t>(layer) * CAR_STAT_COUNT + stat;
			current_words[current_bit / 64u] |= UINT64_C(1) << (current_bit % 64u);
		}
	}
	static constexpr char HEX[] = "0123456789abcdef";
	String normalized;
	for (uint64_t word : current_words) {
		char digits[17];
		for (int32_t digit = 15; digit >= 0; --digit) {
			digits[digit] = HEX[word & 0xfu];
			word >>= 4;
		}
		digits[16] = '\0';
		normalized += String(digits);
	}
	out_value["format_revision"] = 1;
	out_value["derived_mask_hex"] = normalized;
	return true;
}

bool MxtCarAuthoringSession::parse_authoring_intent(
		const Dictionary &value,
		std::array<uint64_t, AUTHORING_MASK_WORD_COUNT> &out_mask) const
{
	if (value.size() != 2 || static_cast<int64_t>(value.get("format_revision", 0)) != 1 ||
		!value.has("derived_mask_hex") || value["derived_mask_hex"].get_type() != Variant::STRING) return false;
	const String encoded = value["derived_mask_hex"];
	if (encoded.length() != static_cast<int64_t>(AUTHORING_MASK_WORD_COUNT * 16u)) return false;
	out_mask.fill(0);
	for (uint32_t word = 0; word < AUTHORING_MASK_WORD_COUNT; ++word) {
		uint64_t decoded = 0;
		for (uint32_t digit = 0; digit < 16; ++digit) {
			const char32_t c = encoded[static_cast<int64_t>(word * 16u + digit)];
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
			decoded = (decoded << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
		}
		out_mask[word] = decoded;
	}
	for (uint8_t layer = 0; layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT; ++layer) {
		for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
			const uint32_t bit = static_cast<uint32_t>(layer) * CAR_STAT_COUNT + stat;
			if ((out_mask[bit / 64u] & (UINT64_C(1) << (bit % 64u))) != 0 &&
				!car_special_pair_is_supported(
					static_cast<CarAuthoringSpecialLayer>(layer), static_cast<CarStatId>(stat))) return false;
		}
	}
	for (uint32_t bit = AUTHORING_PAIR_COUNT; bit < AUTHORING_MASK_WORD_COUNT * 64u; ++bit) {
		if ((out_mask[bit / 64u] & (UINT64_C(1) << (bit % 64u))) != 0) return false;
	}
	return true;
}

Dictionary MxtCarAuthoringSession::set_authoring_intent(const Dictionary &value)
{
	std::array<uint64_t, AUTHORING_MASK_WORD_COUNT> parsed;
	if (!parse_authoring_intent(value, parsed)) {
		return result_dictionary(false, single_error("vehicle authoring intent is malformed"), {});
	}
	push_undo_snapshot();
	derived_pair_mask = parsed;
	regenerate_all_derived_pairs();
	clear_redo_after_mutation();
	dirty = true;
	return validate();
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

Dictionary MxtCarAuthoringSession::get_collision_measurements() const
{
	static constexpr float EPSILON = 0.0001f;
	Dictionary result;
	auto invalid = [&](const String &message) {
		result["valid"] = false;
		result["error"] = message;
		return result;
	};
	for (uint8_t i = 0; i < 4; ++i) {
		const SimVec3 &tilt = tilt_corners[i];
		const SimVec3 &wall = wall_corners[i];
		if (!std::isfinite(tilt.x) || !std::isfinite(tilt.y) || !std::isfinite(tilt.z) ||
				!std::isfinite(wall.x) || !std::isfinite(wall.y) || !std::isfinite(wall.z)) {
			return invalid("collision corners contain non-finite values");
		}
		const float x_sign = tilt.x < 0.0f ? -1.0f : 1.0f;
		const float z_sign = tilt.z < 0.0f ? -1.0f : 1.0f;
		if (std::abs(tilt.y) > EPSILON ||
				std::abs(wall.x - (tilt.x + x_sign * 0.2f)) > EPSILON ||
				std::abs(wall.y + 0.1f) > EPSILON ||
				std::abs(wall.z - (tilt.z + z_sign * 0.2f)) > EPSILON) {
			return invalid("wall corners do not use the fixed tilt-corner offset");
		}
	}
	if (tilt_corners[0].z >= 0.0f || tilt_corners[1].z >= 0.0f ||
			tilt_corners[2].z <= 0.0f || tilt_corners[3].z <= 0.0f ||
			std::abs(tilt_corners[0].z - tilt_corners[1].z) > EPSILON ||
			std::abs(tilt_corners[2].z - tilt_corners[3].z) > EPSILON ||
			std::abs(std::abs(tilt_corners[0].x) - std::abs(tilt_corners[1].x)) > EPSILON ||
			std::abs(std::abs(tilt_corners[2].x) - std::abs(tilt_corners[3].x)) > EPSILON ||
			tilt_corners[0].x * tilt_corners[1].x >= 0.0f ||
			tilt_corners[2].x * tilt_corners[3].x >= 0.0f) {
		return invalid("tilt corners are not symmetric front and rear pairs");
	}
	result["valid"] = true;
	result["front_width"] = std::abs(tilt_corners[0].x) * 2.0f;
	result["rear_width"] = std::abs(tilt_corners[2].x) * 2.0f;
	result["front_forward_extent"] = -tilt_corners[0].z;
	result["rear_backward_extent"] = tilt_corners[2].z;
	return result;
}

Dictionary MxtCarAuthoringSession::set_collision_measurements(const Dictionary &value)
{
	const double front_width = value.get("front_width", 0.0);
	const double rear_width = value.get("rear_width", 0.0);
	const double front_extent = value.get("front_forward_extent", 0.0);
	const double rear_extent = value.get("rear_backward_extent", 0.0);
	if (!std::isfinite(front_width) || !std::isfinite(rear_width) ||
			!std::isfinite(front_extent) || !std::isfinite(rear_extent) ||
			front_width <= 0.0 || rear_width <= 0.0 || front_extent <= 0.0 || rear_extent <= 0.0) {
		return result_dictionary(false, single_error("collision measurements must be finite positive values"), {});
	}
	const double max_component = static_cast<double>(std::numeric_limits<float>::max()) - 0.2;
	if (front_width * 0.5 > max_component || rear_width * 0.5 > max_component ||
			front_extent > max_component || rear_extent > max_component) {
		return result_dictionary(false, single_error("collision measurements exceed the runtime numeric format"), {});
	}
	push_undo_snapshot();
	const float front_half_width = static_cast<float>(front_width * 0.5);
	const float rear_half_width = static_cast<float>(rear_width * 0.5);
	const float front_z = static_cast<float>(-front_extent);
	const float rear_z = static_cast<float>(rear_extent);
	tilt_corners[0] = SimVec3(front_half_width, 0.0f, front_z);
	tilt_corners[1] = SimVec3(-front_half_width, 0.0f, front_z);
	tilt_corners[2] = SimVec3(rear_half_width, 0.0f, rear_z);
	tilt_corners[3] = SimVec3(-rear_half_width, 0.0f, rear_z);
	for (uint8_t i = 0; i < 4; ++i) {
		const float x_sign = tilt_corners[i].x < 0.0f ? -1.0f : 1.0f;
		const float z_sign = tilt_corners[i].z < 0.0f ? -1.0f : 1.0f;
		wall_corners[i] = SimVec3(
				tilt_corners[i].x + x_sign * 0.2f,
				-0.1f,
				tilt_corners[i].z + z_sign * 0.2f);
	}
	clear_redo_after_mutation();
	dirty = true;
	return result_dictionary(true, {}, {});
}

int64_t MxtCarAuthoringSession::get_state_flags() const { return state_flags; }

void MxtCarAuthoringSession::set_state_flags(int64_t value)
{
	push_undo_snapshot();
	state_flags = static_cast<uint32_t>(value);
	clear_redo_after_mutation();
	dirty = true;
}

bool MxtCarAuthoringSession::is_dirty() const { return dirty; }
void MxtCarAuthoringSession::clear_dirty() { dirty = false; }
bool MxtCarAuthoringSession::can_undo() const { return !undo_history.empty(); }
bool MxtCarAuthoringSession::can_redo() const { return !redo_history.empty(); }

bool MxtCarAuthoringSession::capture_history_snapshot(HistorySnapshot &out_snapshot) const
{
	String error;
	if (!serialize_document(out_snapshot.properties, error)) return false;
	out_snapshot.derived_pair_mask = derived_pair_mask;
	out_snapshot.dirty = dirty;
	out_snapshot.model_path = model_path;
	out_snapshot.model_translation = model_translation;
	out_snapshot.model_rotation_degrees = model_rotation_degrees;
	out_snapshot.model_scale = model_scale;
	out_snapshot.body_surfaces = body_surfaces;
	out_snapshot.albedo_surface = albedo_surface;
	out_snapshot.normal_surface = normal_surface;
	out_snapshot.paint_mask_surface = paint_mask_surface;
	out_snapshot.use_mesh_normals = use_mesh_normals;
	out_snapshot.manual_boost_volume_db = manual_boost_volume_db;
	out_snapshot.thrusters = thrusters;
	return true;
}

void MxtCarAuthoringSession::append_undo_snapshot(HistorySnapshot &&snapshot)
{
	if (undo_history.size() == MAX_HISTORY) undo_history.erase(undo_history.begin());
	undo_history.push_back(std::move(snapshot));
}

void MxtCarAuthoringSession::push_undo_snapshot()
{
	if (edit_transaction_depth > 0) {
		if (!transaction_snapshot_valid) {
			transaction_snapshot_valid = capture_history_snapshot(transaction_snapshot);
		}
		return;
	}
	HistorySnapshot snapshot;
	if (capture_history_snapshot(snapshot)) append_undo_snapshot(std::move(snapshot));
}

void MxtCarAuthoringSession::clear_redo_after_mutation()
{
	if (edit_transaction_depth == 0) redo_history.clear();
}

bool MxtCarAuthoringSession::restore_history_snapshot(const HistorySnapshot &snapshot, bool mark_dirty)
{
	Ref<MxtCarAuthoringSession> parsed;
	parsed.instantiate();
	parsed->undo_history.clear();
	parsed->redo_history.clear();
	String error;
	if (!read_document(snapshot.properties, *parsed.ptr(), error)) return false;
	curves = std::move(parsed->curves);
	s_boost_values = parsed->s_boost_values;
	tilt_corners = parsed->tilt_corners;
	wall_corners = parsed->wall_corners;
	state_flags = parsed->state_flags;
	derived_pair_mask = snapshot.derived_pair_mask;
	model_path = snapshot.model_path;
	model_translation = snapshot.model_translation;
	model_rotation_degrees = snapshot.model_rotation_degrees;
	model_scale = snapshot.model_scale;
	body_surfaces = snapshot.body_surfaces;
	albedo_surface = snapshot.albedo_surface;
	normal_surface = snapshot.normal_surface;
	paint_mask_surface = snapshot.paint_mask_surface;
	use_mesh_normals = snapshot.use_mesh_normals;
	manual_boost_volume_db = snapshot.manual_boost_volume_db;
	thrusters = snapshot.thrusters;
	dirty = mark_dirty ? true : snapshot.dirty;
	return true;
}

void MxtCarAuthoringSession::begin_edit_transaction()
{
	if (edit_transaction_depth++ == 0) transaction_snapshot_valid = false;
}

void MxtCarAuthoringSession::end_edit_transaction()
{
	if (edit_transaction_depth == 0) return;
	if (--edit_transaction_depth == 0 && transaction_snapshot_valid) {
		append_undo_snapshot(std::move(transaction_snapshot));
		redo_history.clear();
		transaction_snapshot = HistorySnapshot();
		transaction_snapshot_valid = false;
	}
}

void MxtCarAuthoringSession::cancel_edit_transaction()
{
	if (edit_transaction_depth == 0) return;
	if (transaction_snapshot_valid) restore_history_snapshot(transaction_snapshot, false);
	transaction_snapshot = HistorySnapshot();
	transaction_snapshot_valid = false;
	edit_transaction_depth = 0;
}

bool MxtCarAuthoringSession::undo()
{
	if (edit_transaction_depth > 0) return false;
	if (undo_history.empty()) return false;
	HistorySnapshot current;
	if (!capture_history_snapshot(current)) return false;
	if (redo_history.size() == MAX_HISTORY) redo_history.erase(redo_history.begin());
	redo_history.push_back(std::move(current));
	const HistorySnapshot snapshot = std::move(undo_history.back());
	undo_history.pop_back();
	return restore_history_snapshot(snapshot);
}

bool MxtCarAuthoringSession::redo()
{
	if (edit_transaction_depth > 0) return false;
	if (redo_history.empty()) return false;
	HistorySnapshot current;
	if (!capture_history_snapshot(current)) return false;
	append_undo_snapshot(std::move(current));
	const HistorySnapshot snapshot = std::move(redo_history.back());
	redo_history.pop_back();
	return restore_history_snapshot(snapshot);
}
