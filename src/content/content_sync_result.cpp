#include "content/content_sync_result.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {
static void append_unique(PackedInt64Array &values, int64_t value) {
	if (!values.has(value)) values.push_back(value);
}

static void append_unique(PackedStringArray &values, const String &value) {
	if (!values.has(value)) values.push_back(value);
}
} // namespace

void MxtContentCatalogDelta::_bind_methods() {
#define BIND_READONLY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtContentCatalogDelta::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "", "get_" #name)
	BIND_READONLY(Variant::PACKED_INT64_ARRAY, added_item_ids);
	BIND_READONLY(Variant::PACKED_INT64_ARRAY, changed_item_ids);
	BIND_READONLY(Variant::PACKED_INT64_ARRAY, removed_item_ids);
	BIND_READONLY(Variant::PACKED_STRING_ARRAY, added_content_ids);
	BIND_READONLY(Variant::PACKED_STRING_ARRAY, changed_content_ids);
	BIND_READONLY(Variant::PACKED_STRING_ARRAY, removed_content_ids);
#undef BIND_READONLY
}

void MxtContentCatalogDelta::add_added_item_id(int64_t value) { append_unique(added_item_ids, value); }
void MxtContentCatalogDelta::add_changed_item_id(int64_t value) { append_unique(changed_item_ids, value); }
void MxtContentCatalogDelta::add_removed_item_id(int64_t value) { append_unique(removed_item_ids, value); }
void MxtContentCatalogDelta::add_added_content_id(const String &value) { append_unique(added_content_ids, value); }
void MxtContentCatalogDelta::add_changed_content_id(const String &value) { append_unique(changed_content_ids, value); }
void MxtContentCatalogDelta::add_removed_content_id(const String &value) { append_unique(removed_content_ids, value); }
void MxtContentCatalogDelta::remove_removed_content_id(const String &value) {
	const int64_t index = removed_content_ids.find(value);
	if (index >= 0) removed_content_ids.remove_at(index);
}

void MxtWorkshopSyncItem::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_valid"), &MxtWorkshopSyncItem::is_valid);
#define BIND_READONLY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtWorkshopSyncItem::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "", "get_" #name)
	BIND_READONLY(Variant::INT, published_file_id);
	BIND_READONLY(Variant::STRING, install_path);
	BIND_READONLY(Variant::BOOL, eligible);
	BIND_READONLY(Variant::BOOL, cache_hit);
	BIND_READONLY(Variant::INT, registration_usec);
	BIND_READONLY(Variant::PACKED_STRING_ARRAY, errors);
	BIND_READONLY(Variant::DICTIONARY, validation_profile);
	BIND_READONLY(Variant::DICTIONARY, manifest);
	BIND_READONLY(Variant::STRING, package_digest);
	BIND_READONLY(Variant::STRING, gameplay_digest);
	BIND_READONLY(Variant::OBJECT, record);
#undef BIND_READONLY
}

void MxtWorkshopSyncItem::set_values(int64_t in_published_file_id, const String &in_install_path,
		bool in_eligible, bool in_cache_hit, bool in_valid, int64_t in_registration_usec,
		const PackedStringArray &in_errors, const Dictionary &in_validation_profile,
		const Dictionary &in_manifest, const String &in_package_digest,
		const String &in_gameplay_digest, const Ref<MxtContentRecord> &in_record) {
	published_file_id = in_published_file_id;
	install_path = in_install_path;
	eligible = in_eligible;
	cache_hit = in_cache_hit;
	valid = in_valid;
	registration_usec = in_registration_usec;
	errors = in_errors;
	validation_profile = in_validation_profile;
	manifest = in_manifest;
	package_digest = in_package_digest;
	gameplay_digest = in_gameplay_digest;
	record = in_record;
}

MxtWorkshopSyncResult::MxtWorkshopSyncResult() = default;

void MxtWorkshopSyncResult::initialize() {
	delta.instantiate();
}

void MxtWorkshopSyncResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_item_count"), &MxtWorkshopSyncResult::get_item_count);
	ClassDB::bind_method(D_METHOD("get_item", "index"), &MxtWorkshopSyncResult::get_item);
#define BIND_READONLY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtWorkshopSyncResult::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "", "get_" #name)
	BIND_READONLY(Variant::OBJECT, delta);
	BIND_READONLY(Variant::BOOL, catalog_changed);
	BIND_READONLY(Variant::INT, validation_cache_hit_count);
	BIND_READONLY(Variant::INT, validation_cache_miss_count);
	BIND_READONLY(Variant::STRING, catalog_signature);
#undef BIND_READONLY
}

void MxtWorkshopSyncResult::add_item(const Ref<MxtWorkshopSyncItem> &value) {
	if (value.is_valid()) items.push_back(value);
}

Ref<MxtWorkshopSyncItem> MxtWorkshopSyncResult::get_item(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < items.size() ? items[index] : Ref<MxtWorkshopSyncItem>();
}
