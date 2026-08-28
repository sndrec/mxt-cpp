#include "content/content_validator.h"

#include "car/car_properties.h"
#include "car/car_authoring_session.h"
#include "content/glb_validator.h"
#include "content/track_payload_validator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace godot;

namespace mxt::content {
namespace {

static constexpr uint64_t VEHICLE_PROPERTIES_MAX_BYTES = 4u * 1024u * 1024u;
static constexpr uint64_t VEHICLE_VISUAL_METADATA_MAX_BYTES = 64u * 1024u;
static constexpr uint64_t VEHICLE_AUTHORING_METADATA_MAX_BYTES = 4u * 1024u;
static constexpr uint64_t TRACK_PAYLOAD_MAX_BYTES = 256u * 1024u * 1024u;
static constexpr uint64_t TRACK_VISUAL_MAX_BYTES = 256u * 1024u * 1024u;
static constexpr uint64_t TRACK_METADATA_MAX_BYTES = 1u * 1024u * 1024u;
static constexpr uint32_t PREVIEW_MAX_DIMENSION = 4096;
static constexpr uint64_t HASH_CHUNK_BYTES = 1024u * 1024u;

struct DiskEntry {
	String relative_path;
	String absolute_path;
	uint64_t size = 0;
};

struct CachedPayload {
	String relative_path;
	PackedByteArray bytes;
};

struct ValidationProfile {
	uint64_t total_usec = 0;
	uint64_t manifest_read_usec = 0;
	uint64_t manifest_parse_usec = 0;
	uint64_t directory_enumeration_usec = 0;
	uint64_t structural_validation_usec = 0;
	uint64_t declared_hash_usec = 0;
	uint64_t payload_validation_usec = 0;
	uint64_t glb_validation_usec = 0;
	uint64_t properties_validation_usec = 0;
	uint64_t authoring_validation_usec = 0;
	uint64_t visual_metadata_validation_usec = 0;
	uint64_t combined_hash_usec = 0;
	uint64_t package_digest_usec = 0;
	uint64_t gameplay_digest_usec = 0;
	uint64_t file_open_count = 0;
	uint64_t directory_open_count = 0;
	uint64_t bytes_read = 0;
};

static uint64_t profile_now_usec()
{
	return Time::get_singleton()->get_ticks_usec();
}

static Dictionary validation_profile_dictionary(const ValidationProfile &profile)
{
	Dictionary out;
	out["total_usec"] = static_cast<int64_t>(profile.total_usec);
	out["manifest_read_usec"] = static_cast<int64_t>(profile.manifest_read_usec);
	out["manifest_parse_usec"] = static_cast<int64_t>(profile.manifest_parse_usec);
	out["directory_enumeration_usec"] = static_cast<int64_t>(profile.directory_enumeration_usec);
	out["structural_validation_usec"] = static_cast<int64_t>(profile.structural_validation_usec);
	out["declared_hash_usec"] = static_cast<int64_t>(profile.declared_hash_usec);
	out["payload_validation_usec"] = static_cast<int64_t>(profile.payload_validation_usec);
	out["glb_validation_usec"] = static_cast<int64_t>(profile.glb_validation_usec);
	out["properties_validation_usec"] = static_cast<int64_t>(profile.properties_validation_usec);
	out["authoring_validation_usec"] = static_cast<int64_t>(profile.authoring_validation_usec);
	out["visual_metadata_validation_usec"] = static_cast<int64_t>(profile.visual_metadata_validation_usec);
	out["combined_hash_usec"] = static_cast<int64_t>(profile.combined_hash_usec);
	out["package_digest_usec"] = static_cast<int64_t>(profile.package_digest_usec);
	out["gameplay_digest_usec"] = static_cast<int64_t>(profile.gameplay_digest_usec);
	out["file_open_count"] = static_cast<int64_t>(profile.file_open_count);
	out["directory_open_count"] = static_cast<int64_t>(profile.directory_open_count);
	out["bytes_read"] = static_cast<int64_t>(profile.bytes_read);
	return out;
}

struct ValidationProfileFinalizer {
	ValidationProfile &profile;
	ValidatedPackage &package;
	uint64_t start_usec;

	~ValidationProfileFinalizer()
	{
		profile.total_usec = profile_now_usec() - start_usec;
		package.validation_profile = validation_profile_dictionary(profile);
	}
};

static void add_error(std::vector<String> &errors, const String &message)
{
	errors.push_back(message);
}

static PackedStringArray make_error_array(const std::vector<String> &errors)
{
	PackedStringArray output;
	output.resize(static_cast<int64_t>(errors.size()));
	for (size_t i = 0; i < errors.size(); ++i) {
		output.set(static_cast<int64_t>(i), errors[i]);
	}
	return output;
}

static Dictionary make_result(
		bool valid,
		const std::vector<String> &errors,
		const ContentManifest *manifest = nullptr)
{
	Dictionary result;
	result["valid"] = valid;
	result["errors"] = make_error_array(errors);
	if (manifest) {
		result["manifest"] = manifest_to_dictionary(*manifest);
	}
	return result;
}

static bool read_file_limited(
		const String &path,
		uint64_t max_bytes,
		PackedByteArray &out_bytes,
		std::vector<String> &errors,
		ValidationProfile *profile = nullptr)
{
	if (profile) ++profile->file_open_count;
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		add_error(errors, "could not open '" + path + "'");
		return false;
	}
	const uint64_t length = file->get_length();
	if (length > max_bytes) {
		add_error(errors, "file exceeds its size limit: '" + path + "'");
		return false;
	}
	out_bytes = file->get_buffer(static_cast<int64_t>(length));
	if (profile) profile->bytes_read += static_cast<uint64_t>(out_bytes.size());
	if (static_cast<uint64_t>(out_bytes.size()) != length || file->get_error() != OK) {
		add_error(errors, "could not read complete file '" + path + "'");
		return false;
	}
	return true;
}

static const PackedByteArray *find_cached_payload(
		const std::vector<CachedPayload> &cache,
		const String &relative_path)
{
	for (const CachedPayload &payload : cache) {
		if (payload.relative_path == relative_path) return &payload.bytes;
	}
	return nullptr;
}

static bool read_or_reuse_file_limited(
		const DiskEntry &entry,
		uint64_t max_bytes,
		const std::vector<CachedPayload> &cache,
		PackedByteArray &out_bytes,
		std::vector<String> &errors,
		ValidationProfile *profile = nullptr)
{
	if (entry.size > max_bytes) {
		add_error(errors, "file exceeds its size limit: '" + entry.absolute_path + "'");
		return false;
	}
	const PackedByteArray *cached = find_cached_payload(cache, entry.relative_path);
	if (cached) {
		out_bytes = *cached;
		return true;
	}
	return read_file_limited(entry.absolute_path, max_bytes, out_bytes, errors, profile);
}

static bool is_allowed_directory(const String &relative_path, ContentType type)
{
	if (type == ContentType::VEHICLE) {
		return relative_path == "vehicle";
	}
	if (type == ContentType::TRACK) {
		return relative_path == "track";
	}
	return false;
}

static bool enumerate_directory(
		const String &root_path,
		const String &relative_directory,
		ContentType type,
		std::vector<DiskEntry> &out_files,
		std::vector<String> &out_directories,
		std::vector<String> &errors,
		ValidationProfile *profile = nullptr)
{
	const String disk_path = relative_directory.is_empty()
			? root_path
			: root_path.path_join(relative_directory);
	if (profile) ++profile->directory_open_count;
	Ref<DirAccess> directory = DirAccess::open(disk_path);
	if (directory.is_null()) {
		add_error(errors, "could not open package directory '" + disk_path + "'");
		return false;
	}
	directory->set_include_hidden(true);
	directory->set_include_navigational(false);
	if (directory->list_dir_begin() != OK) {
		add_error(errors, "could not enumerate package directory '" + disk_path + "'");
		return false;
	}
	bool valid = true;
	for (;;) {
		const String name = directory->get_next();
		if (name.is_empty()) {
			break;
		}
		const String relative_path = relative_directory.is_empty()
				? name
				: relative_directory.path_join(name);
		if (name == "." || name == ".." || name.begins_with(".")) {
			add_error(errors, "hidden or navigational package entry is not allowed: '" + relative_path + "'");
			valid = false;
			continue;
		}
		if (directory->is_link(name)) {
			add_error(errors, "symbolic links are not allowed in packages: '" + relative_path + "'");
			valid = false;
			continue;
		}
		if (directory->current_is_dir()) {
			out_directories.push_back(relative_path);
			if (!is_allowed_directory(relative_path, type)) {
				add_error(errors, "undeclared package directory is not allowed: '" + relative_path + "'");
				valid = false;
				continue;
			}
			if (!enumerate_directory(root_path, relative_path, type, out_files, out_directories, errors, profile)) {
				valid = false;
			}
		} else {
			if (profile) ++profile->file_open_count;
			Ref<FileAccess> file = FileAccess::open(root_path.path_join(relative_path), FileAccess::READ);
			if (file.is_null()) {
				add_error(errors, "could not open package file '" + relative_path + "'");
				valid = false;
				continue;
			}
			out_files.push_back({relative_path, root_path.path_join(relative_path), file->get_length()});
		}
	}
	directory->list_dir_end();
	return valid;
}

static std::string utf8_bytes(const String &value)
{
	const CharString encoded = value.utf8();
	return std::string(encoded.get_data(), static_cast<size_t>(encoded.length()));
}

static bool validate_disk_entries(
		const ContentManifest &manifest,
		const std::vector<DiskEntry> &files,
		std::vector<String> &errors)
{
	if (files.size() > PACKAGE_MAX_FILE_COUNT) {
		add_error(errors, "package contains more than " + String::num_uint64(PACKAGE_MAX_FILE_COUNT) + " files");
	}
	std::vector<String> expected;
	expected.reserve(manifest.files.size() + 1);
	expected.push_back("manifest.json");
	for (const ManifestFile &file : manifest.files) {
		expected.push_back(file.path);
	}
	std::vector<std::string> folded;
	for (const DiskEntry &entry : files) {
		const std::string folded_path = utf8_bytes(entry.relative_path.to_lower());
		if (std::find(folded.begin(), folded.end(), folded_path) != folded.end()) {
			add_error(errors, "package contains duplicate case-folded path '" + entry.relative_path + "'");
		} else {
			folded.push_back(folded_path);
		}
		if (std::find(expected.begin(), expected.end(), entry.relative_path) == expected.end()) {
			add_error(errors, "undeclared package file is not allowed: '" + entry.relative_path + "'");
		}
	}
	for (const String &path : expected) {
		const auto found = std::find_if(files.begin(), files.end(), [&](const DiskEntry &entry) {
			return entry.relative_path == path;
		});
		if (found == files.end()) {
			add_error(errors, "package is missing required file '" + path + "'");
		}
	}
	return errors.empty();
}

static uint64_t file_limit_for(const ContentManifest &manifest, const String &path)
{
	if (path == "manifest.json") return MANIFEST_MAX_BYTES;
	if (path == "preview.png") return PREVIEW_MAX_BYTES;
	if (path == "vehicle/model.glb") return VEHICLE_MODEL_MAX_BYTES;
	if (path == "vehicle/properties.mxt_car_props") return VEHICLE_PROPERTIES_MAX_BYTES;
	if (path == "vehicle/visual.json") return VEHICLE_VISUAL_METADATA_MAX_BYTES;
	if (path == "vehicle/authoring.json") return VEHICLE_AUTHORING_METADATA_MAX_BYTES;
	if (path == "vehicle/manual_boost.wav" || path == "vehicle/manual_boost.ogg") return VEHICLE_BOOST_SFX_MAX_BYTES;
	if (path == "vehicle/albedo.png" || path == "vehicle/normal.png" || path == "vehicle/paint_mask.png") return VEHICLE_TEXTURE_MAX_BYTES;
	if (path == "track/track.mxt_track") return TRACK_PAYLOAD_MAX_BYTES;
	if (path == "track/visual.glb") return TRACK_VISUAL_MAX_BYTES;
	if (path == "track/metadata.json") return TRACK_METADATA_MAX_BYTES;
	return manifest.content_type == ContentType::VEHICLE
			? VEHICLE_PACKAGE_MAX_BYTES
			: TRACK_PACKAGE_MAX_BYTES;
}

static const DiskEntry *find_disk_entry(const std::vector<DiskEntry> &entries, const String &path)
{
	for (const DiskEntry &entry : entries) {
		if (entry.relative_path == path) {
			return &entry;
		}
	}
	return nullptr;
}

static uint32_t read_be_u32(const uint8_t *bytes)
{
	return (static_cast<uint32_t>(bytes[0]) << 24) |
			(static_cast<uint32_t>(bytes[1]) << 16) |
			(static_cast<uint32_t>(bytes[2]) << 8) |
			static_cast<uint32_t>(bytes[3]);
}

static bool validate_preview_png(const DiskEntry &entry, std::vector<String> &errors, ValidationProfile *profile)
{
	PackedByteArray header;
	if (profile) ++profile->file_open_count;
	Ref<FileAccess> file = FileAccess::open(entry.absolute_path, FileAccess::READ);
	if (file.is_null() || entry.size < 33) {
		add_error(errors, "preview.png is missing or too short");
		return false;
	}
	header = file->get_buffer(33);
	if (profile) profile->bytes_read += static_cast<uint64_t>(header.size());
	static const uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if (header.size() != 33 || std::memcmp(header.ptr(), PNG_SIGNATURE, 8) != 0 ||
			read_be_u32(header.ptr() + 8) != 13 || std::memcmp(header.ptr() + 12, "IHDR", 4) != 0) {
		add_error(errors, "preview.png does not have a valid PNG IHDR header");
		return false;
	}
	const uint32_t width = read_be_u32(header.ptr() + 16);
	const uint32_t height = read_be_u32(header.ptr() + 20);
	if (width == 0 || height == 0 || width > PREVIEW_MAX_DIMENSION || height > PREVIEW_MAX_DIMENSION) {
		add_error(errors, "preview.png dimensions must be between 1 and 4096 pixels");
		return false;
	}
	return true;
}

static bool validate_vehicle_boost_sfx(const ContentManifest &manifest, const DiskEntry &entry, std::vector<String> &errors, ValidationProfile *profile)
{
	PackedByteArray bytes;
	if (!read_file_limited(entry.absolute_path, VEHICLE_BOOST_SFX_MAX_BYTES, bytes, errors, profile)) return false;
	if (manifest.manual_boost_sfx_path.ends_with(".wav")) {
		if (bytes.size() < 12 || std::memcmp(bytes.ptr(), "RIFF", 4) != 0 || std::memcmp(bytes.ptr() + 8, "WAVE", 4) != 0) {
			add_error(errors, "vehicle manual-boost WAV has an invalid header");
			return false;
		}
	} else if (manifest.manual_boost_sfx_path.ends_with(".ogg")) {
		if (bytes.size() < 4 || std::memcmp(bytes.ptr(), "OggS", 4) != 0) {
			add_error(errors, "vehicle manual-boost Ogg has an invalid header");
			return false;
		}
	}
	return true;
}

static bool validate_vehicle_texture_png(const DiskEntry &entry, std::vector<String> &errors, ValidationProfile *profile)
{
	PackedByteArray bytes;
	if (!read_file_limited(entry.absolute_path, VEHICLE_TEXTURE_MAX_BYTES, bytes, errors, profile)) return false;
	static const uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if (bytes.size() < 33 || std::memcmp(bytes.ptr(), PNG_SIGNATURE, 8) != 0 ||
			read_be_u32(bytes.ptr() + 8) != 13 || std::memcmp(bytes.ptr() + 12, "IHDR", 4) != 0) {
		add_error(errors, "vehicle texture '" + entry.relative_path + "' does not have a valid PNG IHDR header");
		return false;
	}
	const uint32_t declared_width = read_be_u32(bytes.ptr() + 16);
	const uint32_t declared_height = read_be_u32(bytes.ptr() + 20);
	if (declared_width == 0 || declared_height == 0 ||
			declared_width > VEHICLE_TEXTURE_MAX_DIMENSION || declared_height > VEHICLE_TEXTURE_MAX_DIMENSION) {
		add_error(errors, "vehicle texture '" + entry.relative_path + "' dimensions must be between 1 and 2048 pixels");
		return false;
	}
	Ref<Image> image;
	image.instantiate();
	if (image->load_png_from_buffer(bytes) != OK || image->is_empty()) {
		add_error(errors, "vehicle texture '" + entry.relative_path + "' is not a decodable PNG");
		return false;
	}
	if (image->get_width() != static_cast<int32_t>(declared_width) ||
			image->get_height() != static_cast<int32_t>(declared_height)) {
		add_error(errors, "vehicle texture '" + entry.relative_path + "' decoded dimensions disagree with its PNG header");
		return false;
	}
	return true;
}

static bool json_number_in_range(const Variant &value, double minimum, double maximum)
{
	double number = 0.0;
	if (value.get_type() == Variant::FLOAT) {
		number = static_cast<double>(value);
	} else if (value.get_type() == Variant::INT) {
		number = static_cast<double>(static_cast<int64_t>(value));
	} else {
		return false;
	}
	return std::isfinite(number) && number >= minimum && number <= maximum;
}

static bool json_vec3_in_range(const Variant &value, double minimum, double maximum, double *out_length_squared = nullptr)
{
	if (value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array values = value;
	if (values.size() != 3) {
		return false;
	}
	double length_squared = 0.0;
	for (int64_t i = 0; i < 3; ++i) {
		if (!json_number_in_range(values[i], minimum, maximum)) {
			return false;
		}
		const double component = values[i].get_type() == Variant::FLOAT
				? static_cast<double>(values[i])
				: static_cast<double>(static_cast<int64_t>(values[i]));
		length_squared += component * component;
	}
	if (out_length_squared) {
		*out_length_squared = length_squared;
	}
	return true;
}

static bool validate_track_metadata(
		const DiskEntry &entry,
		const std::vector<CachedPayload> &cache,
		std::vector<String> &errors,
		ValidationProfile *profile)
{
	PackedByteArray bytes;
	if (!read_or_reuse_file_limited(entry, TRACK_METADATA_MAX_BYTES, cache, bytes, errors, profile)) {
		return false;
	}
	if (!audit_json_members(bytes, errors)) {
		add_error(errors, "track metadata contains duplicate JSON members");
		return false;
	}
	Ref<JSON> json;
	json.instantiate();
	const String text = String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
	if (json->parse(text) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		add_error(errors, "track metadata must contain one valid JSON object");
		return false;
	}
	const Dictionary metadata = json->get_data();
	static const char *REQUIRED_KEYS[] = {
		"difficulty", "fog_distance", "sky_top_color", "sky_horizon_color", "sky_ground_color",
		"ground_color", "ground_height", "cloud_color", "cloud_height", "light_color",
		"light_intensity", "ambient_intensity", "ambient_color", "light_direction"
	};
	if (metadata.size() != static_cast<int64_t>(std::size(REQUIRED_KEYS))) {
		add_error(errors, "track metadata must contain exactly the revision 1 environment fields");
		return false;
	}
	for (const char *key : REQUIRED_KEYS) {
		if (!metadata.has(key)) {
			add_error(errors, String("track metadata is missing '") + key + String("'"));
			return false;
		}
	}
	double difficulty = -1.0;
	if (metadata["difficulty"].get_type() == Variant::INT) {
		difficulty = static_cast<double>(static_cast<int64_t>(metadata["difficulty"]));
	} else if (metadata["difficulty"].get_type() == Variant::FLOAT) {
		difficulty = static_cast<double>(metadata["difficulty"]);
	}
	if (difficulty < 0.0 || difficulty > 10.0 || difficulty != std::floor(difficulty) ||
			!json_number_in_range(metadata["fog_distance"], 10.0, 1'000'000.0) ||
			!json_number_in_range(metadata["ground_height"], -100'000.0, 100'000.0) ||
			!json_number_in_range(metadata["cloud_height"], -100'000.0, 100'000.0) ||
			!json_number_in_range(metadata["light_intensity"], 0.0, 100.0) ||
			!json_number_in_range(metadata["ambient_intensity"], 0.0, 100.0)) {
		add_error(errors, "track metadata contains an out-of-range scalar field");
		return false;
	}
	for (const char *color_key : {
			 "sky_top_color", "sky_horizon_color", "sky_ground_color", "ground_color",
			 "cloud_color", "light_color", "ambient_color"}) {
		if (!json_vec3_in_range(metadata[color_key], 0.0, 1.0)) {
			add_error(errors, String("track metadata color '") + color_key + String("' must contain three values from 0 through 1"));
			return false;
		}
	}
	double direction_length_squared = 0.0;
	if (!json_vec3_in_range(metadata["light_direction"], -1.0, 1.0, &direction_length_squared) ||
			direction_length_squared <= 1.0e-8) {
		add_error(errors, "track metadata light_direction must be a non-zero three-component vector");
		return false;
	}
	return true;
}

static void append_u32_be(const Ref<HashingContext> &hash, uint32_t value)
{
	PackedByteArray bytes;
	bytes.resize(4);
	uint8_t *out = bytes.ptrw();
	out[0] = static_cast<uint8_t>(value >> 24);
	out[1] = static_cast<uint8_t>(value >> 16);
	out[2] = static_cast<uint8_t>(value >> 8);
	out[3] = static_cast<uint8_t>(value);
	hash->update(bytes);
}

static void append_u64_be(const Ref<HashingContext> &hash, uint64_t value)
{
	PackedByteArray bytes;
	bytes.resize(8);
	uint8_t *out = bytes.ptrw();
	for (uint32_t i = 0; i < 8; ++i) {
		out[i] = static_cast<uint8_t>(value >> (56u - i * 8u));
	}
	hash->update(bytes);
}

static void append_utf8(const Ref<HashingContext> &hash, const String &value)
{
	const PackedByteArray bytes = value.to_utf8_buffer();
	append_u32_be(hash, static_cast<uint32_t>(bytes.size()));
	hash->update(bytes);
}

static bool append_file(
		const Ref<HashingContext> &hash,
		const DiskEntry &entry,
		std::vector<String> &errors,
		ValidationProfile *profile = nullptr)
{
	if (profile) ++profile->file_open_count;
	Ref<FileAccess> file = FileAccess::open(entry.absolute_path, FileAccess::READ);
	if (file.is_null()) {
		add_error(errors, "could not open package file while hashing '" + entry.relative_path + "'");
		return false;
	}
	uint64_t remaining = entry.size;
	while (remaining > 0) {
		const int64_t request = static_cast<int64_t>(std::min<uint64_t>(remaining, HASH_CHUNK_BYTES));
		const PackedByteArray chunk = file->get_buffer(request);
		if (profile) profile->bytes_read += static_cast<uint64_t>(chunk.size());
		if (chunk.size() != request) {
			add_error(errors, "package file changed or failed while hashing '" + entry.relative_path + "'");
			return false;
		}
		hash->update(chunk);
		remaining -= static_cast<uint64_t>(request);
	}
	return true;
}

static bool calculate_gameplay_digest(
		ContentType content_type,
		const String &authoritative_path,
		const DiskEntry &authoritative,
		String &out_gameplay_digest,
		std::vector<String> &errors,
		ValidationProfile *profile = nullptr)
{
	Ref<HashingContext> gameplay_hash;
	gameplay_hash.instantiate();
	if (gameplay_hash->start(HashingContext::HASH_SHA256) != OK) {
		add_error(errors, "could not initialize gameplay SHA-256");
		return false;
	}
	PackedByteArray domain;
	domain.resize(13);
	std::memcpy(domain.ptrw(), "MXT_GAMEPLAY\0", 13);
	gameplay_hash->update(domain);
	append_u32_be(gameplay_hash, GAMEPLAY_DIGEST_REVISION);
	append_utf8(gameplay_hash, content_type_name(content_type));
	append_utf8(gameplay_hash, authoritative_path);
	append_u64_be(gameplay_hash, authoritative.size);
	if (!append_file(gameplay_hash, authoritative, errors, profile)) {
		return false;
	}
	out_gameplay_digest = "sha256:" + gameplay_hash->finish().hex_encode();
	return true;
}

static const ManifestFile *find_manifest_file(
		const ContentManifest &manifest,
		const String &path)
{
	for (const ManifestFile &file : manifest.files) {
		if (file.path == path) return &file;
	}
	return nullptr;
}

static bool cache_payload_bytes(const String &path)
{
	return path == "vehicle/properties.mxt_car_props" ||
			path == "vehicle/visual.json" ||
			path == "vehicle/authoring.json" ||
			path == "track/metadata.json";
}

static bool calculate_and_validate_hashes(
		const ContentManifest &manifest,
		std::vector<DiskEntry> entries,
		std::vector<CachedPayload> &out_cache,
		String &out_package_digest,
		String &out_gameplay_digest,
		std::vector<String> &errors,
		ValidationProfile *profile)
{
	std::sort(entries.begin(), entries.end(), [](const DiskEntry &a, const DiskEntry &b) {
		return utf8_bytes(a.relative_path) < utf8_bytes(b.relative_path);
	});
	Ref<HashingContext> package_hash;
	package_hash.instantiate();
	if (package_hash->start(HashingContext::HASH_SHA256) != OK) {
		add_error(errors, "could not initialize package SHA-256");
		return false;
	}
	PackedByteArray domain;
	domain.resize(12);
	std::memcpy(domain.ptrw(), "MXT_PACKAGE\0", 12);
	package_hash->update(domain);
	append_u32_be(package_hash, PACKAGE_FORMAT_REVISION);

	const DiskEntry *authoritative = find_disk_entry(entries, manifest.authoritative_path);
	if (!authoritative) {
		add_error(errors, "authoritative gameplay payload is missing");
		return false;
	}
	Ref<HashingContext> gameplay_hash;
	gameplay_hash.instantiate();
	if (gameplay_hash->start(HashingContext::HASH_SHA256) != OK) {
		add_error(errors, "could not initialize gameplay SHA-256");
		return false;
	}
	PackedByteArray gameplay_domain;
	gameplay_domain.resize(13);
	std::memcpy(gameplay_domain.ptrw(), "MXT_GAMEPLAY\0", 13);
	gameplay_hash->update(gameplay_domain);
	append_u32_be(gameplay_hash, GAMEPLAY_DIGEST_REVISION);
	append_utf8(gameplay_hash, content_type_name(manifest.content_type));
	append_utf8(gameplay_hash, manifest.authoritative_path);
	append_u64_be(gameplay_hash, authoritative->size);

	const uint64_t hash_start_usec = profile_now_usec();
	for (const DiskEntry &entry : entries) {
		append_utf8(package_hash, entry.relative_path);
		append_u64_be(package_hash, entry.size);
		const ManifestFile *declared_file = find_manifest_file(manifest, entry.relative_path);
		Ref<HashingContext> declared_hash;
		if (declared_file) {
			declared_hash.instantiate();
			if (declared_hash->start(HashingContext::HASH_SHA256) != OK) {
				add_error(errors, "could not initialize declared-file SHA-256");
				return false;
			}
		}
		const bool is_authoritative = entry.relative_path == manifest.authoritative_path;
		CachedPayload cached;
		const bool cache_bytes = cache_payload_bytes(entry.relative_path);
		if (cache_bytes) {
			cached.relative_path = entry.relative_path;
			cached.bytes.resize(static_cast<int64_t>(entry.size));
		}
		if (profile) ++profile->file_open_count;
		Ref<FileAccess> file = FileAccess::open(entry.absolute_path, FileAccess::READ);
		if (file.is_null()) {
			add_error(errors, "could not open package file while hashing '" + entry.relative_path + "'");
			return false;
		}
		uint64_t remaining = entry.size;
		uint64_t offset = 0;
		while (remaining > 0) {
			const int64_t request = static_cast<int64_t>(std::min<uint64_t>(remaining, HASH_CHUNK_BYTES));
			const PackedByteArray chunk = file->get_buffer(request);
			if (profile) profile->bytes_read += static_cast<uint64_t>(chunk.size());
			if (chunk.size() != request) {
				add_error(errors, "package file changed or failed while hashing '" + entry.relative_path + "'");
				return false;
			}
			package_hash->update(chunk);
			if (is_authoritative) gameplay_hash->update(chunk);
			if (declared_file) declared_hash->update(chunk);
			if (cache_bytes) {
				std::memcpy(
						cached.bytes.ptrw() + static_cast<int64_t>(offset),
						chunk.ptr(),
						static_cast<size_t>(chunk.size()));
			}
			offset += static_cast<uint64_t>(request);
			remaining -= static_cast<uint64_t>(request);
		}
		if (declared_file) {
			const String actual = declared_hash->finish().hex_encode();
			if (actual != declared_file->declared_sha256) {
				add_error(errors, "SHA-256 mismatch for '" + entry.relative_path + "'");
			}
		}
		if (cache_bytes) out_cache.push_back(std::move(cached));
	}
	out_package_digest = "sha256:" + package_hash->finish().hex_encode();
	out_gameplay_digest = "sha256:" + gameplay_hash->finish().hex_encode();
	if (profile) profile->combined_hash_usec += profile_now_usec() - hash_start_usec;
	return errors.empty();
}

static bool has_only_keys(
		const Dictionary &value,
		const char *const *keys,
		size_t key_count,
		const String &scope,
		std::vector<String> &errors)
{
	bool valid = true;
	const Array dictionary_keys = value.keys();
	for (int64_t key_index = 0; key_index < dictionary_keys.size(); ++key_index) {
		const Variant key_value = dictionary_keys[key_index];
		if (key_value.get_type() != Variant::STRING) {
			add_error(errors, scope + String(" contains a non-string member"));
			valid = false;
			continue;
		}
		const String key = key_value;
		bool recognized = false;
		for (size_t i = 0; i < key_count; ++i) recognized |= key == keys[i];
		if (!recognized) {
			add_error(errors, scope + String(" contains unrecognized member '") + key + String("'"));
			valid = false;
		}
	}
	return valid;
}

static bool read_bounded_vector3(
		const Dictionary &object,
		const char *key,
		double minimum,
		double maximum,
		Vector3 &out,
		std::vector<String> &errors)
{
	if (!object.has(key) || object[key].get_type() != Variant::ARRAY) {
		add_error(errors, String("vehicle visual member '") + key + "' must be a three-number array");
		return false;
	}
	const Array values = object[key];
	if (values.size() != 3) {
		add_error(errors, String("vehicle visual member '") + key + "' must have three elements");
		return false;
	}
	double components[3] = {};
	for (int64_t i = 0; i < 3; ++i) {
		if (values[i].get_type() != Variant::INT && values[i].get_type() != Variant::FLOAT) {
			add_error(errors, String("vehicle visual member '") + key + "' must contain only numbers");
			return false;
		}
		components[i] = values[i].get_type() == Variant::INT
				? static_cast<double>(static_cast<int64_t>(values[i]))
				: static_cast<double>(values[i]);
		if (!std::isfinite(components[i]) || components[i] < minimum || components[i] > maximum) {
			add_error(errors, String("vehicle visual member '") + key + "' is outside its supported range");
			return false;
		}
	}
	out = Vector3(components[0], components[1], components[2]);
	return true;
}

static bool validate_vehicle_visual_metadata(
		const DiskEntry &entry,
		const VehicleGlbInfo *model_info,
		const std::vector<CachedPayload> &cache,
		Dictionary &out_metadata,
		std::vector<String> &errors,
		ValidationProfile *profile)
{
	PackedByteArray bytes;
	if (!read_or_reuse_file_limited(entry, VEHICLE_VISUAL_METADATA_MAX_BYTES, cache, bytes, errors, profile)) return false;
	if (!audit_json_members(bytes, errors)) return false;
	const Variant parsed = JSON::parse_string(String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size()));
	if (parsed.get_type() != Variant::DICTIONARY) {
		add_error(errors, "vehicle/visual.json root must be an object");
		return false;
	}
	const Dictionary root = parsed;
	static const char *ROOT_KEYS[] = {
		"format_revision", "model_transform", "body_surfaces", "material_inputs",
		"manual_boost_volume_db", "thrusters"
	};
	has_only_keys(root, ROOT_KEYS, std::size(ROOT_KEYS), "vehicle visual metadata", errors);
	const Variant revision_value = root.get("format_revision", Variant());
	const bool valid_revision =
			(revision_value.get_type() == Variant::INT && static_cast<int64_t>(revision_value) == 1) ||
			(revision_value.get_type() == Variant::FLOAT && static_cast<double>(revision_value) == 1.0);
	if (!valid_revision) {
		add_error(errors, "vehicle visual format_revision must be integer 1");
	}
	if (!root.has("model_transform") || root["model_transform"].get_type() != Variant::DICTIONARY) {
		add_error(errors, "vehicle visual model_transform must be an object");
		return false;
	}
	const Dictionary model_transform = root["model_transform"];
	static const char *TRANSFORM_KEYS[] = {"translation", "rotation_degrees", "scale"};
	has_only_keys(model_transform, TRANSFORM_KEYS, std::size(TRANSFORM_KEYS), "vehicle visual model_transform", errors);
	Vector3 translation;
	Vector3 rotation_degrees;
	Vector3 scale;
	read_bounded_vector3(model_transform, "translation", -1000.0, 1000.0, translation, errors);
	read_bounded_vector3(model_transform, "rotation_degrees", -3600.0, 3600.0, rotation_degrees, errors);
	if (read_bounded_vector3(model_transform, "scale", 0.001, 100.0, scale, errors) &&
			(scale.x <= 0.0 || scale.y <= 0.0 || scale.z <= 0.0)) {
		add_error(errors, "vehicle visual model scale must be positive");
	}
	if (!root.has("body_surfaces") || root["body_surfaces"].get_type() != Variant::ARRAY) {
		add_error(errors, "vehicle visual body_surfaces must be an array");
		return false;
	}
	const Array body_surfaces = root["body_surfaces"];
	if (body_surfaces.is_empty() || body_surfaces.size() > 1024) {
		add_error(errors, "vehicle visual must select between one and 1024 body surfaces");
	}
	Array normalized_body_surfaces;
	std::vector<uint8_t> selected(model_info ? model_info->surfaces.size() : 0, 0);
	auto read_json_integer = [](const Variant &value, int64_t &out) {
		if (value.get_type() == Variant::INT) {
			out = static_cast<int64_t>(value);
			return true;
		}
		if (value.get_type() != Variant::FLOAT) return false;
		const double number = static_cast<double>(value);
		if (!std::isfinite(number) || number != std::floor(number) ||
				number < -1.0 || number > 9.0e15) return false;
		out = static_cast<int64_t>(number);
		return true;
	};
	for (int64_t i = 0; i < body_surfaces.size(); ++i) {
		int64_t surface = -1;
		if (!read_json_integer(body_surfaces[i], surface)) {
			add_error(errors, "vehicle visual body surface indices must be integers");
			continue;
		}
		if (!model_info || surface < 0 || surface >= static_cast<int64_t>(model_info->surfaces.size())) {
			add_error(errors, "vehicle visual body surface index is outside the model");
			continue;
		}
		if (selected[static_cast<size_t>(surface)] != 0) {
			add_error(errors, "vehicle visual body surface indices must be unique");
			continue;
		}
		selected[static_cast<size_t>(surface)] = 1;
		normalized_body_surfaces.push_back(surface);
	}
	if (!root.has("material_inputs") || root["material_inputs"].get_type() != Variant::DICTIONARY) {
		add_error(errors, "vehicle visual material_inputs must be an object");
		return false;
	}
	const Dictionary material_inputs = root["material_inputs"];
	// Revision-1 Workshop vehicles resolve the later use_mesh_normals field to false,
	// preserving their original flat/texture-normal shading.
	static const char *MATERIAL_KEYS[] = {
		"albedo_surface", "normal_surface", "paint_mask_surface", "use_mesh_normals"
	};
	has_only_keys(material_inputs, MATERIAL_KEYS, std::size(MATERIAL_KEYS), "vehicle visual material_inputs", errors);
	const Variant use_mesh_normals_value = material_inputs.get("use_mesh_normals", false);
	if (use_mesh_normals_value.get_type() != Variant::BOOL) {
		add_error(errors, "vehicle visual material input 'use_mesh_normals' must be a boolean");
	}
	const bool use_mesh_normals = use_mesh_normals_value.get_type() == Variant::BOOL &&
			static_cast<bool>(use_mesh_normals_value);
	auto read_material_surface = [&](const char *key, bool VehicleGlbSurface::*texture_member) {
		int64_t surface = -2;
		if (!material_inputs.has(key) || !read_json_integer(material_inputs[key], surface)) {
			add_error(errors, String("vehicle visual material input '") + key + "' must be an integer");
			return int64_t(-2);
		}
		if (surface == -1) return surface;
		if (!model_info || surface < 0 || surface >= static_cast<int64_t>(model_info->surfaces.size())) {
			add_error(errors, String("vehicle visual material input '") + key + "' is outside the model");
			return int64_t(-2);
		}
		if (!(model_info->surfaces[static_cast<size_t>(surface)].*texture_member)) {
			add_error(errors, String("vehicle visual material input '") + key + "' selects a surface without that texture");
			return int64_t(-2);
		}
		return surface;
	};
	const int64_t albedo_surface = read_material_surface("albedo_surface", &VehicleGlbSurface::has_albedo_texture);
	const int64_t normal_surface = read_material_surface("normal_surface", &VehicleGlbSurface::has_normal_texture);
	const int64_t paint_mask_surface = read_material_surface("paint_mask_surface", &VehicleGlbSurface::has_paint_mask_texture);
	// Revision-1 Workshop vehicles predate per-vehicle boost volume. Keeping the
	// member optional and defaulting to 0 dB preserves their original playback.
	const Variant boost_volume_value = root.get("manual_boost_volume_db", 0.0);
	double manual_boost_volume_db = 0.0;
	if (boost_volume_value.get_type() == Variant::INT) {
		manual_boost_volume_db = static_cast<double>(static_cast<int64_t>(boost_volume_value));
	} else if (boost_volume_value.get_type() == Variant::FLOAT) {
		manual_boost_volume_db = static_cast<double>(boost_volume_value);
	} else {
		add_error(errors, "vehicle visual manual_boost_volume_db must be a number");
	}
	if (!std::isfinite(manual_boost_volume_db) ||
			manual_boost_volume_db < -20.0 || manual_boost_volume_db > 20.0) {
		add_error(errors, "vehicle visual manual_boost_volume_db is outside [-20, 20]");
	}
	if (!root.has("thrusters") || root["thrusters"].get_type() != Variant::ARRAY) {
		add_error(errors, "vehicle visual thrusters must be an array");
		return false;
	}
	const Array thrusters = root["thrusters"];
	if (thrusters.size() > 8) add_error(errors, "vehicle visual supports at most eight thrusters");
	Array normalized_thrusters;
	for (int64_t i = 0; i < thrusters.size(); ++i) {
		if (thrusters[i].get_type() != Variant::DICTIONARY) {
			add_error(errors, "vehicle visual thruster must be an object");
			continue;
		}
		const Dictionary thruster = thrusters[i];
		static const char *THRUSTER_KEYS[] = {"position", "rotation_degrees", "scale"};
		has_only_keys(thruster, THRUSTER_KEYS, std::size(THRUSTER_KEYS), "vehicle visual thruster", errors);
		Vector3 position;
		Vector3 rotation;
		read_bounded_vector3(thruster, "position", -100.0, 100.0, position, errors);
		read_bounded_vector3(thruster, "rotation_degrees", -3600.0, 3600.0, rotation, errors);
		if (!thruster.has("scale") ||
				(thruster["scale"].get_type() != Variant::INT && thruster["scale"].get_type() != Variant::FLOAT)) {
			add_error(errors, "vehicle visual thruster scale must be a number");
			continue;
		}
		const double scale_value = thruster["scale"].get_type() == Variant::INT
				? static_cast<double>(static_cast<int64_t>(thruster["scale"]))
				: static_cast<double>(thruster["scale"]);
		if (!std::isfinite(scale_value) || scale_value < 0.01 || scale_value > 10.0) {
			add_error(errors, "vehicle visual thruster scale is outside [0.01, 10]");
			continue;
		}
		Dictionary normalized;
		normalized["position"] = position;
		normalized["rotation_degrees"] = rotation;
		normalized["scale"] = scale_value;
		normalized_thrusters.push_back(normalized);
	}
	if (!errors.empty()) return false;
	Dictionary normalized_transform;
	normalized_transform["translation"] = translation;
	normalized_transform["rotation_degrees"] = rotation_degrees;
	normalized_transform["scale"] = scale;
	out_metadata["format_revision"] = 1;
	out_metadata["model_transform"] = normalized_transform;
	out_metadata["body_surfaces"] = normalized_body_surfaces;
	Dictionary normalized_material_inputs;
	normalized_material_inputs["albedo_surface"] = albedo_surface;
	normalized_material_inputs["normal_surface"] = normal_surface;
	normalized_material_inputs["paint_mask_surface"] = paint_mask_surface;
	normalized_material_inputs["use_mesh_normals"] = use_mesh_normals;
	out_metadata["material_inputs"] = normalized_material_inputs;
	out_metadata["manual_boost_volume_db"] = manual_boost_volume_db;
	out_metadata["thrusters"] = normalized_thrusters;
	return true;
}

static bool validate_vehicle_authoring_metadata(
		const DiskEntry &entry,
		uint16_t source_stat_count,
		const std::vector<CachedPayload> &cache,
		Dictionary &out_metadata,
		std::vector<String> &errors,
		ValidationProfile *profile)
{
	PackedByteArray bytes;
	if (!read_or_reuse_file_limited(entry, VEHICLE_AUTHORING_METADATA_MAX_BYTES, cache, bytes, errors, profile)) return false;
	if (!audit_json_members(bytes, errors)) return false;
	const Variant parsed = JSON::parse_string(String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size()));
	if (parsed.get_type() != Variant::DICTIONARY) {
		add_error(errors, "vehicle/authoring.json root must be an object");
		return false;
	}
	const Dictionary root = parsed;
	static const char *ROOT_KEYS[] = {"format_revision", "derived_mask_hex"};
	has_only_keys(root, ROOT_KEYS, std::size(ROOT_KEYS), "vehicle authoring metadata", errors);
	const Variant revision_value = root.get("format_revision", Variant());
	const bool valid_revision =
			(revision_value.get_type() == Variant::INT && static_cast<int64_t>(revision_value) == 1) ||
			(revision_value.get_type() == Variant::FLOAT && static_cast<double>(revision_value) == 1.0);
	if (!valid_revision) add_error(errors, "vehicle authoring format_revision must be integer 1");
	if (!root.has("derived_mask_hex") || root["derived_mask_hex"].get_type() != Variant::STRING) {
		add_error(errors, "vehicle authoring derived_mask_hex must be a string");
		return false;
	}
	String normalize_error;
	if (!MxtCarAuthoringSession::normalize_authoring_intent(
			root, source_stat_count, out_metadata, normalize_error)) {
		add_error(errors, normalize_error);
		return false;
	}
	return true;
}

static bool validate_payloads(
		const ContentManifest &manifest,
		const std::vector<DiskEntry> &entries,
		const std::vector<CachedPayload> &cache,
		Dictionary &out_visual_metadata,
		Dictionary &out_authoring_metadata,
		std::vector<String> &errors,
		ValidationProfile *profile)
{
	const DiskEntry *preview = find_disk_entry(entries, "preview.png");
	if (preview) {
		validate_preview_png(*preview, errors, profile);
	}
	if (manifest.content_type == ContentType::VEHICLE) {
		if (!manifest.manual_boost_sfx_path.is_empty()) {
			const DiskEntry *boost_sfx = find_disk_entry(entries, manifest.manual_boost_sfx_path);
			if (boost_sfx) validate_vehicle_boost_sfx(manifest, *boost_sfx, errors, profile);
		}
		for (const String *texture_path : {
				&manifest.albedo_texture_path,
				&manifest.normal_texture_path,
				&manifest.paint_mask_texture_path}) {
			if (texture_path->is_empty()) continue;
			const DiskEntry *texture = find_disk_entry(entries, *texture_path);
			if (texture) validate_vehicle_texture_png(*texture, errors, profile);
		}
		const DiskEntry *model = find_disk_entry(entries, "vehicle/model.glb");
		const DiskEntry *properties = find_disk_entry(entries, "vehicle/properties.mxt_car_props");
		const DiskEntry *visual_metadata = find_disk_entry(entries, "vehicle/visual.json");
		const DiskEntry *authoring_metadata = find_disk_entry(entries, "vehicle/authoring.json");
		VehicleGlbInfo model_info;
		bool model_valid = false;
		uint16_t properties_schema_stat_count = 0;
		PackedByteArray properties_bytes;
		bool properties_bytes_valid = false;
		if (model) {
			const uint64_t start_usec = profile_now_usec();
			if (profile) {
				++profile->file_open_count;
				profile->bytes_read += model->size;
			}
			model_valid = validate_glb_file(model->absolute_path, manifest.content_type, errors, &model_info);
			if (profile) profile->glb_validation_usec += profile_now_usec() - start_usec;
		}
		if (properties) {
			const uint64_t start_usec = profile_now_usec();
			if (read_or_reuse_file_limited(
					*properties, VEHICLE_PROPERTIES_MAX_BYTES, cache,
					properties_bytes, errors, profile)) {
				properties_bytes_valid = true;
				PhysicsCarProperties sampled;
				String parse_error;
				if (!PhysicsCarProperties::deserialize_and_sample(
						properties_bytes, 0.5f, sampled, parse_error, &properties_schema_stat_count)) {
					add_error(errors, "vehicle properties rejected: " + parse_error);
				}
			}
			if (profile) profile->properties_validation_usec += profile_now_usec() - start_usec;
		}
		if (authoring_metadata) {
			const uint64_t start_usec = profile_now_usec();
			validate_vehicle_authoring_metadata(
					*authoring_metadata, properties_schema_stat_count, cache,
					out_authoring_metadata, errors, profile);
			if (profile) profile->authoring_validation_usec += profile_now_usec() - start_usec;
		}
		if (properties_bytes_valid && !out_authoring_metadata.is_empty() && errors.empty()) {
			const uint64_t start_usec = profile_now_usec();
			const PackedByteArray &original = properties_bytes;
			Ref<MxtCarAuthoringSession> session;
			session.instantiate();
			Dictionary loaded = session->load_bytes(original);
			Dictionary normalized = session->serialize();
			Dictionary applied = session->set_authoring_intent(out_authoring_metadata);
			Dictionary materialized = session->serialize();
			const PackedByteArray normalized_bytes = normalized.get("bytes", PackedByteArray());
			const PackedByteArray materialized_bytes = materialized.get("bytes", PackedByteArray());
			if (!static_cast<bool>(loaded.get("valid", false)) ||
				!static_cast<bool>(normalized.get("valid", false)) ||
				!static_cast<bool>(applied.get("valid", false)) ||
				!static_cast<bool>(materialized.get("valid", false)) ||
				materialized_bytes != normalized_bytes ||
				(properties_schema_stat_count == CAR_STAT_COUNT && normalized_bytes != original)) {
				add_error(errors, "vehicle authoring intent does not match the materialized properties");
			}
			if (profile) profile->authoring_validation_usec += profile_now_usec() - start_usec;
		}
		if (visual_metadata) {
			const uint64_t start_usec = profile_now_usec();
			validate_vehicle_visual_metadata(
					*visual_metadata, model_valid ? &model_info : nullptr, cache,
					out_visual_metadata, errors, profile);
			if (profile) profile->visual_metadata_validation_usec += profile_now_usec() - start_usec;
		}
	} else if (manifest.content_type == ContentType::TRACK) {
		const DiskEntry *track = find_disk_entry(entries, "track/track.mxt_track");
		const DiskEntry *visual = find_disk_entry(entries, "track/visual.glb");
		const DiskEntry *metadata = find_disk_entry(entries, "track/metadata.json");
		if (track) {
			PackedByteArray bytes;
			if (read_file_limited(track->absolute_path, TRACK_PAYLOAD_MAX_BYTES, bytes, errors, profile)) {
				String parse_error;
				if (!validate_track_payload(bytes, parse_error)) {
					add_error(errors, "track payload rejected: " + parse_error);
				}
			}
		}
		if (visual) {
			const uint64_t start_usec = profile_now_usec();
			if (profile) {
				++profile->file_open_count;
				profile->bytes_read += visual->size;
			}
			validate_glb_file(visual->absolute_path, manifest.content_type, errors);
			if (profile) profile->glb_validation_usec += profile_now_usec() - start_usec;
		}
		if (metadata) {
			validate_track_metadata(*metadata, cache, errors, profile);
		}
	}
	return errors.empty();
}

} // namespace

bool validate_authoritative_gameplay_file(
		ContentType content_type,
		const String &path,
		bool validate_payload,
		String &out_gameplay_digest,
		std::vector<String> &out_errors)
{
	out_gameplay_digest = String();
	if (content_type != ContentType::VEHICLE && content_type != ContentType::TRACK) {
		add_error(out_errors, "invalid authoritative gameplay content type");
		return false;
	}
	const uint64_t max_bytes = content_type == ContentType::VEHICLE
			? VEHICLE_PROPERTIES_MAX_BYTES
			: TRACK_PAYLOAD_MAX_BYTES;
	PackedByteArray bytes;
	if (!read_file_limited(path, max_bytes, bytes, out_errors)) {
		return false;
	}
	if (validate_payload && content_type == ContentType::VEHICLE) {
		PhysicsCarProperties sampled;
		String parse_error;
		if (!PhysicsCarProperties::deserialize_and_sample(bytes, 0.5f, sampled, parse_error)) {
			add_error(out_errors, "vehicle properties rejected: " + parse_error);
			return false;
		}
	} else if (validate_payload) {
		String parse_error;
		if (!validate_track_payload(bytes, parse_error)) {
			add_error(out_errors, "track payload rejected: " + parse_error);
			return false;
		}
	}

	const String authoritative_path = content_type == ContentType::VEHICLE
			? String("vehicle/properties.mxt_car_props")
			: String("track/track.mxt_track");
	DiskEntry authoritative;
	authoritative.relative_path = authoritative_path;
	authoritative.absolute_path = path;
	authoritative.size = static_cast<uint64_t>(bytes.size());
	return calculate_gameplay_digest(
			content_type,
			authoritative_path,
			authoritative,
			out_gameplay_digest,
			out_errors);
}

bool validate_package_directory_internal(
		const String &root_path,
		ValidatedPackage &out_package,
		std::vector<String> &out_errors)
{
	out_package = ValidatedPackage();
	ValidationProfile profile;
	ValidationProfileFinalizer profile_finalizer{profile, out_package, profile_now_usec()};
	++profile.directory_open_count;
	if (root_path.is_empty() || DirAccess::open(root_path).is_null()) {
		add_error(out_errors, "package root is not an existing directory");
		return false;
	}

	const String manifest_path = root_path.path_join("manifest.json");
	PackedByteArray manifest_bytes;
	uint64_t phase_start_usec = profile_now_usec();
	if (!read_file_limited(manifest_path, MANIFEST_MAX_BYTES, manifest_bytes, out_errors, &profile)) {
		return false;
	}
	profile.manifest_read_usec = profile_now_usec() - phase_start_usec;
	phase_start_usec = profile_now_usec();
	if (!parse_manifest(manifest_bytes, out_package.manifest, out_errors)) {
		return false;
	}
	profile.manifest_parse_usec = profile_now_usec() - phase_start_usec;

	std::vector<DiskEntry> entries;
	std::vector<String> directories;
	phase_start_usec = profile_now_usec();
	enumerate_directory(root_path, String(), out_package.manifest.content_type, entries, directories, out_errors, &profile);
	profile.directory_enumeration_usec = profile_now_usec() - phase_start_usec;
	phase_start_usec = profile_now_usec();
	validate_disk_entries(out_package.manifest, entries, out_errors);
	if (!out_errors.empty()) {
		return false;
	}

	const uint64_t package_limit = out_package.manifest.content_type == ContentType::VEHICLE
			? VEHICLE_PACKAGE_MAX_BYTES
			: TRACK_PACKAGE_MAX_BYTES;
	uint64_t total_bytes = 0;
	for (const DiskEntry &entry : entries) {
		const uint64_t limit = file_limit_for(out_package.manifest, entry.relative_path);
		if (entry.size > limit) {
			add_error(out_errors, "file exceeds its size limit: '" + entry.relative_path + "'");
		}
		if (entry.size > package_limit - std::min(total_bytes, package_limit)) {
			add_error(out_errors, "package exceeds its total uncompressed size limit");
			break;
		}
		total_bytes += entry.size;
	}
	out_package.total_bytes = total_bytes;
	profile.structural_validation_usec = profile_now_usec() - phase_start_usec;
	if (!out_errors.empty()) {
		return false;
	}

	std::vector<CachedPayload> payload_cache;
	if (!calculate_and_validate_hashes(
			out_package.manifest,
			entries,
			payload_cache,
			out_package.package_digest,
			out_package.gameplay_digest,
			out_errors,
			&profile)) {
		return false;
	}
	phase_start_usec = profile_now_usec();
	validate_payloads(
			out_package.manifest, entries, payload_cache, out_package.visual_metadata,
			out_package.authoring_metadata, out_errors, &profile);
	profile.payload_validation_usec = profile_now_usec() - phase_start_usec;
	if (!out_errors.empty()) {
		return false;
	}
	out_package.root_path = root_path;
	return true;
}

} // namespace mxt::content

void MxtContentValidator::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("validate_manifest_bytes", "manifest_bytes"), &MxtContentValidator::validate_manifest_bytes);
	ClassDB::bind_method(D_METHOD("validate_package_directory", "root_path"), &MxtContentValidator::validate_package_directory);
}

Dictionary MxtContentValidator::validate_manifest_bytes(const PackedByteArray &manifest_bytes) const
{
	mxt::content::ContentManifest manifest;
	std::vector<String> errors;
	const bool valid = mxt::content::parse_manifest(manifest_bytes, manifest, errors);
	return mxt::content::make_result(valid, errors, valid ? &manifest : nullptr);
}

Dictionary MxtContentValidator::validate_package_directory(const String &root_path) const
{
	mxt::content::ValidatedPackage package;
	std::vector<String> errors;
	const bool valid = mxt::content::validate_package_directory_internal(root_path, package, errors);
	Dictionary result = mxt::content::make_result(valid, errors, package.manifest.content_type == mxt::content::ContentType::INVALID ? nullptr : &package.manifest);
	result["validation_profile"] = package.validation_profile;
	if (valid) {
		result["root_path"] = package.root_path;
		result["package_digest"] = package.package_digest;
		result["gameplay_digest"] = package.gameplay_digest;
		result["total_bytes"] = static_cast<int64_t>(package.total_bytes);
	}
	return result;
}
