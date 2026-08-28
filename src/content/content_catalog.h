#ifndef MXT_CONTENT_CATALOG_H
#define MXT_CONTENT_CATALOG_H

#include "content/content_record.h"
#include "content/content_validator.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace mxt::content {

} // namespace mxt::content

namespace godot {

class MxtContentCatalog : public RefCounted {
	GDCLASS(MxtContentCatalog, RefCounted)

private:
	struct WorkshopPackageState {
		uint64_t published_file_id = 0;
		String install_path;
		String install_identity;
		bool valid = false;
		uint64_t registration_usec = 0;
		mxt::content::ValidatedPackage package;
		mxt::content::ContentRecord record;
		std::vector<String> errors;
	};

	std::vector<mxt::content::ContentRecord> records;
	std::vector<WorkshopPackageState> workshop_packages;
	uint64_t generation = 0;

	Dictionary add_package_internal(
			const String &package_root,
			mxt::content::ContentSource source,
			uint64_t published_file_id);
	void replace_record(const mxt::content::ContentRecord &record, bool publish = true);
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
	Dictionary add_loose_track(
			const String &title,
			const String &track_path,
			const String &visual_path,
			const String &metadata_path);
	void clear_loose_tracks();
	Dictionary add_local_package(const String &package_root);
	Dictionary add_draft_package(const String &package_root);
	Dictionary snapshot_draft_package(const String &package_root, const String &library_root);
	Dictionary add_workshop_package(const String &package_root, int64_t published_file_id);
	Dictionary sync_workshop_packages(const Array &items);
	Dictionary scan_local_library(const String &library_root);
	bool remove_content(const String &content_id);
	void clear();

	bool has_content(const String &content_id) const;
	Ref<MxtContentRecord> resolve_content(const String &content_id) const;
	Array get_records(const String &content_type = String()) const;
	Array find_gameplay(const String &content_type, const String &gameplay_digest) const;
	int64_t get_generation() const { return static_cast<int64_t>(generation); }
};

} // namespace godot

#endif // MXT_CONTENT_CATALOG_H
