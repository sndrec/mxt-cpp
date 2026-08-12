#ifndef MXT_CONTENT_MANIFEST_H
#define MXT_CONTENT_MANIFEST_H

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace mxt::content {

static constexpr uint32_t PACKAGE_FORMAT_REVISION = 1;
static constexpr uint32_t GAMEPLAY_DIGEST_REVISION = 1;
static constexpr uint64_t MANIFEST_MAX_BYTES = 64u * 1024u;
static constexpr uint64_t PREVIEW_MAX_BYTES = 8u * 1024u * 1024u;
static constexpr uint64_t VEHICLE_PACKAGE_MAX_BYTES = 64u * 1024u * 1024u;
static constexpr uint64_t TRACK_PACKAGE_MAX_BYTES = 512u * 1024u * 1024u;
static constexpr uint64_t VEHICLE_MODEL_MAX_BYTES = 48u * 1024u * 1024u;
static constexpr uint64_t VEHICLE_MODEL_MAX_VERTICES = 1'000'000;
static constexpr uint64_t VEHICLE_MODEL_MAX_TRIANGLES = 250'000;
static constexpr uint32_t VEHICLE_MODEL_MAX_IMAGES = 64;
static constexpr uint64_t VEHICLE_MODEL_MAX_TEXTURE_PIXELS = 64u * 1024u * 1024u;
static constexpr uint32_t PACKAGE_MAX_FILE_COUNT = 8;

enum class ContentType : uint8_t {
	INVALID = 0,
	VEHICLE,
	TRACK,
};

struct ManifestFile {
	godot::String path;
	godot::String declared_sha256;
};

struct ContentManifest {
	uint32_t format_revision = 0;
	ContentType content_type = ContentType::INVALID;
	godot::String title;
	godot::String description;
	godot::String author_name;
	godot::String authoritative_path;
	std::vector<ManifestFile> files;
};

bool parse_manifest(
		const godot::PackedByteArray &bytes,
		ContentManifest &out_manifest,
		std::vector<godot::String> &out_errors);

bool audit_json_members(
		const godot::PackedByteArray &bytes,
		std::vector<godot::String> &out_errors);

godot::String content_type_name(ContentType type);
godot::Dictionary manifest_to_dictionary(const ContentManifest &manifest);

} // namespace mxt::content

#endif // MXT_CONTENT_MANIFEST_H
