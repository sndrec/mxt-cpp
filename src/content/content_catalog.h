#ifndef MXT_CONTENT_CATALOG_H
#define MXT_CONTENT_CATALOG_H

#include "content/content_manifest.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace mxt::content {

enum class ContentSource : uint8_t {
	OFFICIAL = 0,
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
godot::Dictionary content_record_to_dictionary(const ContentRecord &record);

} // namespace mxt::content

namespace godot {

class MxtContentCatalog : public RefCounted {
	GDCLASS(MxtContentCatalog, RefCounted)

private:
	std::vector<mxt::content::ContentRecord> records;
	uint64_t generation = 0;

	Dictionary add_package_internal(
			const String &package_root,
			mxt::content::ContentSource source,
			uint64_t published_file_id);
	void replace_record(const mxt::content::ContentRecord &record);
	void publish_change();

protected:
	static void _bind_methods();

public:
	Dictionary add_official_vehicle(
			const String &slug,
			const String &title,
			const String &properties_path,
			const String &definition_path);
	Dictionary add_official_track(
			const String &slug,
			const String &title,
			const String &track_path,
			const String &visual_path,
			const String &metadata_path,
			const String &expected_gameplay_digest);
	Dictionary add_local_package(const String &package_root);
	Dictionary add_draft_package(const String &package_root);
	Dictionary add_workshop_package(const String &package_root, int64_t published_file_id);
	Dictionary scan_local_library(const String &library_root);
	void clear_workshop_packages();
	bool remove_content(const String &content_id);
	void clear();

	bool has_content(const String &content_id) const;
	Dictionary resolve_content(const String &content_id) const;
	Array get_records(const String &content_type = String()) const;
	Array find_gameplay(const String &content_type, const String &gameplay_digest) const;
	int64_t get_generation() const { return static_cast<int64_t>(generation); }
};

} // namespace godot

#endif // MXT_CONTENT_CATALOG_H
