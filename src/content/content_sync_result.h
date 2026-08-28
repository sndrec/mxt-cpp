#pragma once

#include "content/content_record.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <vector>

namespace godot {

class MxtContentCatalogDelta : public RefCounted {
	GDCLASS(MxtContentCatalogDelta, RefCounted)

	PackedInt64Array added_item_ids;
	PackedInt64Array changed_item_ids;
	PackedInt64Array removed_item_ids;
	PackedStringArray added_content_ids;
	PackedStringArray changed_content_ids;
	PackedStringArray removed_content_ids;

protected:
	static void _bind_methods();

public:
	void add_added_item_id(int64_t value);
	void add_changed_item_id(int64_t value);
	void add_removed_item_id(int64_t value);
	void add_added_content_id(const String &value);
	void add_changed_content_id(const String &value);
	void add_removed_content_id(const String &value);
	void remove_removed_content_id(const String &value);

	PackedInt64Array get_added_item_ids() const { return added_item_ids; }
	PackedInt64Array get_changed_item_ids() const { return changed_item_ids; }
	PackedInt64Array get_removed_item_ids() const { return removed_item_ids; }
	PackedStringArray get_added_content_ids() const { return added_content_ids; }
	PackedStringArray get_changed_content_ids() const { return changed_content_ids; }
	PackedStringArray get_removed_content_ids() const { return removed_content_ids; }
};

class MxtWorkshopSyncItem : public RefCounted {
	GDCLASS(MxtWorkshopSyncItem, RefCounted)

	int64_t published_file_id = 0;
	String install_path;
	bool eligible = false;
	bool cache_hit = false;
	bool valid = false;
	int64_t registration_usec = 0;
	PackedStringArray errors;
	Dictionary validation_profile;
	Dictionary manifest;
	String package_digest;
	String gameplay_digest;
	Ref<MxtContentRecord> record;

protected:
	static void _bind_methods();

public:
	void set_values(int64_t in_published_file_id, const String &in_install_path, bool in_eligible,
			bool in_cache_hit, bool in_valid, int64_t in_registration_usec,
			const PackedStringArray &in_errors, const Dictionary &in_validation_profile,
			const Dictionary &in_manifest, const String &in_package_digest,
			const String &in_gameplay_digest, const Ref<MxtContentRecord> &in_record);

	int64_t get_published_file_id() const { return published_file_id; }
	String get_install_path() const { return install_path; }
	bool get_eligible() const { return eligible; }
	bool get_cache_hit() const { return cache_hit; }
	bool is_valid() const { return valid; }
	int64_t get_registration_usec() const { return registration_usec; }
	PackedStringArray get_errors() const { return errors; }
	Dictionary get_validation_profile() const { return validation_profile.duplicate(true); }
	Dictionary get_manifest() const { return manifest.duplicate(true); }
	String get_package_digest() const { return package_digest; }
	String get_gameplay_digest() const { return gameplay_digest; }
	Ref<MxtContentRecord> get_record() const { return record; }
};

class MxtWorkshopSyncResult : public RefCounted {
	GDCLASS(MxtWorkshopSyncResult, RefCounted)

	std::vector<Ref<MxtWorkshopSyncItem>> items;
	Ref<MxtContentCatalogDelta> delta;
	bool catalog_changed = false;
	int64_t validation_cache_hit_count = 0;
	int64_t validation_cache_miss_count = 0;
	String catalog_signature;

protected:
	static void _bind_methods();

public:
	MxtWorkshopSyncResult();
	void initialize();
	void add_item(const Ref<MxtWorkshopSyncItem> &value);
	void set_catalog_changed(bool value) { catalog_changed = value; }
	void set_validation_cache_hit_count(int64_t value) { validation_cache_hit_count = value; }
	void set_validation_cache_miss_count(int64_t value) { validation_cache_miss_count = value; }
	void set_catalog_signature(const String &value) { catalog_signature = value; }

	int32_t get_item_count() const { return static_cast<int32_t>(items.size()); }
	Ref<MxtWorkshopSyncItem> get_item(int32_t index) const;
	Ref<MxtContentCatalogDelta> get_delta() const { return delta; }
	bool get_catalog_changed() const { return catalog_changed; }
	int64_t get_validation_cache_hit_count() const { return validation_cache_hit_count; }
	int64_t get_validation_cache_miss_count() const { return validation_cache_miss_count; }
	String get_catalog_signature() const { return catalog_signature; }
};

} // namespace godot
