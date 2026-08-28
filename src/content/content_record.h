#pragma once

#include "content/content_manifest.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>

namespace mxt::content {

enum class ContentSource : uint8_t {
	OFFICIAL = 0,
	LOCAL_LOOSE,
	LOCAL_PACKAGE,
	LOCAL_DRAFT,
	WORKSHOP,
};

struct ContentRecord {
	ContentType content_type = ContentType::INVALID;
	ContentSource source = ContentSource::LOCAL_PACKAGE;
	godot::String content_id;
	godot::String package_digest;
	godot::String gameplay_digest;
	godot::String root_path;
	godot::String preview_path;
	godot::String authoritative_path;
	godot::String visual_path;
	godot::String metadata_path;
	godot::String manual_boost_sfx_path;
	godot::String albedo_texture_path;
	godot::String normal_texture_path;
	godot::String paint_mask_texture_path;
	godot::String title;
	godot::String description;
	godot::String author_name;
	godot::Dictionary visual_metadata;
	godot::Dictionary authoring_metadata;
	uint64_t published_file_id = 0;
};

godot::String content_source_name(ContentSource source);

} // namespace mxt::content

namespace godot {

class MxtContentRecord : public RefCounted {
	GDCLASS(MxtContentRecord, RefCounted)

	mxt::content::ContentRecord record;

protected:
	static void _bind_methods();

public:
	enum ContentType {
		CONTENT_INVALID = static_cast<int32_t>(mxt::content::ContentType::INVALID),
		CONTENT_VEHICLE = static_cast<int32_t>(mxt::content::ContentType::VEHICLE),
		CONTENT_TRACK = static_cast<int32_t>(mxt::content::ContentType::TRACK),
	};
	enum ContentSource {
		SOURCE_OFFICIAL = static_cast<int32_t>(mxt::content::ContentSource::OFFICIAL),
		SOURCE_LOCAL_LOOSE = static_cast<int32_t>(mxt::content::ContentSource::LOCAL_LOOSE),
		SOURCE_LOCAL_PACKAGE = static_cast<int32_t>(mxt::content::ContentSource::LOCAL_PACKAGE),
		SOURCE_LOCAL_DRAFT = static_cast<int32_t>(mxt::content::ContentSource::LOCAL_DRAFT),
		SOURCE_WORKSHOP = static_cast<int32_t>(mxt::content::ContentSource::WORKSHOP),
	};

	void set_record(const mxt::content::ContentRecord &value) { record = value; }
	const mxt::content::ContentRecord &get_record() const { return record; }

	bool is_valid() const { return record.content_type != mxt::content::ContentType::INVALID && !record.content_id.is_empty(); }
	int32_t get_content_type() const { return static_cast<int32_t>(record.content_type); }
	String get_content_type_name() const;
	int32_t get_source() const { return static_cast<int32_t>(record.source); }
	String get_source_name() const;
	String get_content_id() const { return record.content_id; }
	String get_package_digest() const { return record.package_digest; }
	String get_gameplay_digest() const { return record.gameplay_digest; }
	String get_root_path() const { return record.root_path; }
	String get_preview_path() const { return record.preview_path; }
	String get_authoritative_path() const { return record.authoritative_path; }
	String get_visual_path() const { return record.visual_path; }
	String get_metadata_path() const { return record.metadata_path; }
	String get_manual_boost_sfx_path() const { return record.manual_boost_sfx_path; }
	String get_albedo_texture_path() const { return record.albedo_texture_path; }
	String get_normal_texture_path() const { return record.normal_texture_path; }
	String get_paint_mask_texture_path() const { return record.paint_mask_texture_path; }
	String get_title() const { return record.title; }
	String get_description() const { return record.description; }
	String get_author_name() const { return record.author_name; }
	Dictionary get_visual_metadata() const { return record.visual_metadata.duplicate(true); }
	Dictionary get_authoring_metadata() const { return record.authoring_metadata.duplicate(true); }
	int64_t get_published_file_id() const { return static_cast<int64_t>(record.published_file_id); }
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MxtContentRecord::ContentType)
VARIANT_ENUM_CAST(godot::MxtContentRecord::ContentSource)
