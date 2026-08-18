#include "content/content_catalog.h"

#include "content/content_validator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace godot;

namespace mxt::content {

String content_source_name(ContentSource source)
{
	switch (source) {
		case ContentSource::OFFICIAL: return String("official");
		case ContentSource::LOCAL_PACKAGE: return String("local_package");
		case ContentSource::LOCAL_DRAFT: return String("local_draft");
		case ContentSource::WORKSHOP: return String("workshop");
	}
	return String();
}

Dictionary content_record_to_dictionary(const ContentRecord &record)
{
	Dictionary output;
	output["content_type"] = content_type_name(record.content_type);
	output["source"] = content_source_name(record.source);
	output["content_id"] = record.content_id;
	output["package_digest"] = record.package_digest;
	output["gameplay_digest"] = record.gameplay_digest;
	output["root_path"] = record.root_path;
	output["preview_path"] = record.preview_path;
	output["authoritative_path"] = record.authoritative_path;
	output["visual_path"] = record.visual_path;
	output["metadata_path"] = record.metadata_path;
	output["title"] = record.title;
	output["description"] = record.description;
	output["author_name"] = record.author_name;
	output["visual_metadata"] = record.visual_metadata;
	output["authoring_metadata"] = record.authoring_metadata;
	output["published_file_id"] = record.published_file_id == 0
			? String()
			: String::num_uint64(record.published_file_id);
	output["availability"] = "ready";
	output["validation"] = "valid";
	return output;
}

} // namespace mxt::content

namespace {

static String global_path(const String &path)
{
	if (path.begins_with("res://") || path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(path);
	}
	return path;
}

static PackedStringArray error_array(const std::vector<String> &errors)
{
	PackedStringArray output;
	for (const String &error : errors) output.push_back(error);
	return output;
}

static bool is_digest_directory_name(const String &name)
{
	if (name.length() != 64) return false;
	for (int64_t i = 0; i < name.length(); ++i) {
		const char32_t c = name[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	}
	return true;
}

static mxt::content::ContentRecord make_record(
		const mxt::content::ValidatedPackage &package,
		mxt::content::ContentSource source,
		uint64_t published_file_id)
{
	mxt::content::ContentRecord record;
	record.content_type = package.manifest.content_type;
	record.source = source;
	record.package_digest = package.package_digest;
	record.gameplay_digest = package.gameplay_digest;
	record.root_path = package.root_path;
	record.preview_path = package.root_path.path_join("preview.png");
	record.authoritative_path = package.root_path.path_join(package.manifest.authoritative_path);
	record.title = package.manifest.title;
	record.description = package.manifest.description;
	record.author_name = package.manifest.author_name;
	record.visual_metadata = package.visual_metadata;
	record.authoring_metadata = package.authoring_metadata;
	record.published_file_id = published_file_id;
	const String type_name = mxt::content::content_type_name(record.content_type);
	if (source == mxt::content::ContentSource::WORKSHOP) {
		record.content_id = String("mxt:") + type_name + String(":workshop:") + String::num_uint64(published_file_id);
	} else if (source == mxt::content::ContentSource::LOCAL_DRAFT) {
		record.content_id = String("mxt:") + type_name + String(":draft:") + package.package_digest.substr(7);
	} else {
		record.content_id = String("mxt:") + type_name + String(":package:") + package.package_digest.substr(7);
	}
	if (record.content_type == mxt::content::ContentType::VEHICLE) {
		record.visual_path = package.root_path.path_join("vehicle/model.glb");
		record.metadata_path = package.root_path.path_join("vehicle/visual.json");
	} else {
		record.visual_path = package.root_path.path_join("track/visual.glb");
		record.metadata_path = package.root_path.path_join("track/metadata.json");
	}
	return record;
}

static bool decimal_u64(int64_t input, uint64_t &output)
{
	if (input <= 0) return false;
	output = static_cast<uint64_t>(input);
	return true;
}

static bool valid_official_slug(const String &slug)
{
	if (slug.is_empty() || slug.length() > 64) return false;
	for (int64_t i = 0; i < slug.length(); ++i) {
		const char32_t c = slug[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
	}
	return true;
}

} // namespace

void MxtContentCatalog::_bind_methods()
{
	ClassDB::bind_method(
			D_METHOD("add_official_vehicle", "slug", "title", "properties_path", "definition_path"),
			&MxtContentCatalog::add_official_vehicle);
	ClassDB::bind_method(
			D_METHOD("add_official_track", "slug", "title", "track_path", "visual_path", "metadata_path", "expected_gameplay_digest"),
			&MxtContentCatalog::add_official_track);
	ClassDB::bind_method(D_METHOD("add_local_package", "package_root"), &MxtContentCatalog::add_local_package);
	ClassDB::bind_method(D_METHOD("add_draft_package", "package_root"), &MxtContentCatalog::add_draft_package);
	ClassDB::bind_method(D_METHOD("add_workshop_package", "package_root", "published_file_id"), &MxtContentCatalog::add_workshop_package);
	ClassDB::bind_method(D_METHOD("scan_local_library", "library_root"), &MxtContentCatalog::scan_local_library);
	ClassDB::bind_method(D_METHOD("clear_workshop_packages"), &MxtContentCatalog::clear_workshop_packages);
	ClassDB::bind_method(D_METHOD("remove_content", "content_id"), &MxtContentCatalog::remove_content);
	ClassDB::bind_method(D_METHOD("clear"), &MxtContentCatalog::clear);
	ClassDB::bind_method(D_METHOD("has_content", "content_id"), &MxtContentCatalog::has_content);
	ClassDB::bind_method(D_METHOD("resolve_content", "content_id"), &MxtContentCatalog::resolve_content);
	ClassDB::bind_method(D_METHOD("get_records", "content_type"), &MxtContentCatalog::get_records, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("find_gameplay", "content_type", "gameplay_digest"), &MxtContentCatalog::find_gameplay);
	ClassDB::bind_method(D_METHOD("get_generation"), &MxtContentCatalog::get_generation);
	ADD_SIGNAL(MethodInfo("catalog_changed", PropertyInfo(Variant::INT, "generation")));
}

Dictionary MxtContentCatalog::add_official_vehicle(
		const String &slug,
		const String &title,
		const String &properties_path,
		const String &definition_path)
{
	Dictionary result;
	std::vector<String> errors;
	if (!valid_official_slug(slug)) {
		errors.push_back("official content slug must contain only lowercase letters, digits, and hyphens");
	}
	if (title.is_empty() || title.length() > 128) {
		errors.push_back("official vehicle title must contain 1 to 128 characters");
	}
	if (properties_path.is_empty() || definition_path.is_empty()) {
		errors.push_back("official vehicle paths must not be empty");
	}
	String gameplay_digest;
	if (errors.empty()) {
		mxt::content::validate_authoritative_gameplay_file(
				mxt::content::ContentType::VEHICLE,
				global_path(properties_path),
				true,
				gameplay_digest,
				errors);
	}
	if (!errors.empty()) {
		result["valid"] = false;
		result["errors"] = error_array(errors);
		return result;
	}

	mxt::content::ContentRecord record;
	record.content_type = mxt::content::ContentType::VEHICLE;
	record.source = mxt::content::ContentSource::OFFICIAL;
	record.content_id = "mxt:vehicle:official:" + slug;
	record.gameplay_digest = gameplay_digest;
	record.authoritative_path = properties_path;
	record.visual_path = definition_path;
	record.title = title;
	record.author_name = "MaxX Throttle";
	replace_record(record);
	result["valid"] = true;
	result["errors"] = PackedStringArray();
	result["record"] = mxt::content::content_record_to_dictionary(record);
	return result;
}

Dictionary MxtContentCatalog::add_official_track(
		const String &slug,
		const String &title,
		const String &track_path,
		const String &visual_path,
		const String &metadata_path,
		const String &expected_gameplay_digest)
{
	Dictionary result;
	std::vector<String> errors;
	if (!valid_official_slug(slug)) {
		errors.push_back("official content slug must contain only lowercase letters, digits, and hyphens");
	}
	if (title.is_empty() || title.length() > 128) {
		errors.push_back("official track title must contain 1 to 128 characters");
	}
	if (track_path.is_empty() || visual_path.is_empty() || metadata_path.is_empty()) {
		errors.push_back("official track paths must not be empty");
	}
	if (!FileAccess::file_exists(global_path(visual_path))) {
		errors.push_back("official track visual file does not exist");
	}
	if (!FileAccess::file_exists(global_path(metadata_path))) {
		errors.push_back("official track metadata file does not exist");
	}
	String gameplay_digest;
	if (errors.empty()) {
		mxt::content::validate_authoritative_gameplay_file(
				mxt::content::ContentType::TRACK,
				global_path(track_path),
				false,
				gameplay_digest,
				errors);
	}
	if (errors.empty() && gameplay_digest != expected_gameplay_digest) {
		errors.push_back("official track gameplay digest does not match the checked-in catalog");
	}
	if (!errors.empty()) {
		result["valid"] = false;
		result["errors"] = error_array(errors);
		return result;
	}

	mxt::content::ContentRecord record;
	record.content_type = mxt::content::ContentType::TRACK;
	record.source = mxt::content::ContentSource::OFFICIAL;
	record.content_id = "mxt:track:official:" + slug;
	record.gameplay_digest = gameplay_digest;
	record.authoritative_path = track_path;
	record.visual_path = visual_path;
	record.metadata_path = metadata_path;
	record.root_path = track_path.get_base_dir();
	record.title = title;
	record.author_name = "MaxX Throttle";
	replace_record(record);
	result["valid"] = true;
	result["errors"] = PackedStringArray();
	result["record"] = mxt::content::content_record_to_dictionary(record);
	return result;
}

void MxtContentCatalog::publish_change()
{
	++generation;
	emit_signal("catalog_changed", static_cast<int64_t>(generation));
}

void MxtContentCatalog::replace_record(const mxt::content::ContentRecord &record)
{
	for (mxt::content::ContentRecord &existing : records) {
		if (existing.content_id == record.content_id) {
			existing = record;
			std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
				return a.content_id < b.content_id;
			});
			publish_change();
			return;
		}
	}
	records.push_back(record);
	std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
		return a.content_id < b.content_id;
	});
	publish_change();
}

Dictionary MxtContentCatalog::add_package_internal(
		const String &package_root,
		mxt::content::ContentSource source,
		uint64_t published_file_id)
{
	std::vector<String> errors;
	mxt::content::ValidatedPackage package;
	const String root = global_path(package_root);
	if (!mxt::content::validate_package_directory_internal(root, package, errors)) {
		Dictionary result;
		result["valid"] = false;
		result["errors"] = error_array(errors);
		return result;
	}
	const mxt::content::ContentRecord record = make_record(package, source, published_file_id);
	replace_record(record);
	Dictionary result;
	result["valid"] = true;
	result["errors"] = PackedStringArray();
	result["record"] = mxt::content::content_record_to_dictionary(record);
	return result;
}

Dictionary MxtContentCatalog::add_local_package(const String &package_root)
{
	return add_package_internal(package_root, mxt::content::ContentSource::LOCAL_PACKAGE, 0);
}

Dictionary MxtContentCatalog::add_draft_package(const String &package_root)
{
	return add_package_internal(package_root, mxt::content::ContentSource::LOCAL_DRAFT, 0);
}

Dictionary MxtContentCatalog::add_workshop_package(const String &package_root, int64_t published_file_id)
{
	uint64_t item_id = 0;
	if (!decimal_u64(published_file_id, item_id)) {
		Dictionary result;
		result["valid"] = false;
		result["errors"] = PackedStringArray({"published_file_id must be a positive integer"});
		return result;
	}
	return add_package_internal(package_root, mxt::content::ContentSource::WORKSHOP, item_id);
}

Dictionary MxtContentCatalog::scan_local_library(const String &library_root)
{
	const String root = global_path(library_root);
	Ref<DirAccess> directory = DirAccess::open(root);
	Dictionary result;
	if (directory.is_null()) {
		result["valid"] = false;
		result["errors"] = PackedStringArray({"local content library directory does not exist"});
		result["registered_count"] = 0;
		return result;
	}
	directory->set_include_hidden(true);
	directory->set_include_navigational(false);
	if (directory->list_dir_begin() != OK) {
		result["valid"] = false;
		result["errors"] = PackedStringArray({"could not enumerate local content library directory"});
		result["registered_count"] = 0;
		return result;
	}
	std::vector<mxt::content::ContentRecord> candidates;
	Array diagnostics;
	for (;;) {
		const String name = directory->get_next();
		if (name.is_empty()) break;
		if (name.begins_with(".mxt-import-")) continue;
		Dictionary diagnostic;
		diagnostic["path"] = root.path_join(name);
		std::vector<String> errors;
		if (!directory->current_is_dir() || directory->is_link(name) || !is_digest_directory_name(name)) {
			errors.push_back("unexpected entry in content-addressed local library");
		} else {
			mxt::content::ValidatedPackage package;
			if (mxt::content::validate_package_directory_internal(root.path_join(name), package, errors)) {
				if (package.package_digest.substr(7) != name) {
					errors.push_back("package directory name does not match its package digest");
				} else {
					candidates.push_back(make_record(package, mxt::content::ContentSource::LOCAL_PACKAGE, 0));
				}
			}
		}
		if (!errors.empty()) {
			diagnostic["errors"] = error_array(errors);
			diagnostics.push_back(diagnostic);
		}
	}
	directory->list_dir_end();

	records.erase(std::remove_if(records.begin(), records.end(), [](const auto &record) {
		return record.source == mxt::content::ContentSource::LOCAL_PACKAGE;
	}), records.end());
	for (const mxt::content::ContentRecord &candidate : candidates) {
		records.push_back(candidate);
	}
	std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
		return a.content_id < b.content_id;
	});
	publish_change();
	result["valid"] = diagnostics.is_empty();
	result["errors"] = PackedStringArray();
	result["diagnostics"] = diagnostics;
	result["registered_count"] = static_cast<int64_t>(candidates.size());
	return result;
}

void MxtContentCatalog::clear_workshop_packages()
{
	const size_t previous_size = records.size();
	records.erase(std::remove_if(records.begin(), records.end(), [](const auto &record) {
		return record.source == mxt::content::ContentSource::WORKSHOP;
	}), records.end());
	if (records.size() != previous_size) publish_change();
}

bool MxtContentCatalog::remove_content(const String &content_id)
{
	const auto found = std::find_if(records.begin(), records.end(), [&](const auto &record) {
		return record.content_id == content_id;
	});
	if (found == records.end()) return false;
	records.erase(found);
	publish_change();
	return true;
}

void MxtContentCatalog::clear()
{
	if (records.empty()) return;
	records.clear();
	publish_change();
}

bool MxtContentCatalog::has_content(const String &content_id) const
{
	return std::any_of(records.begin(), records.end(), [&](const auto &record) {
		return record.content_id == content_id;
	});
}

Dictionary MxtContentCatalog::resolve_content(const String &content_id) const
{
	for (const mxt::content::ContentRecord &record : records) {
		if (record.content_id == content_id) {
			return mxt::content::content_record_to_dictionary(record);
		}
	}
	return Dictionary();
}

Array MxtContentCatalog::get_records(const String &content_type) const
{
	Array output;
	for (const mxt::content::ContentRecord &record : records) {
		if (content_type.is_empty() || mxt::content::content_type_name(record.content_type) == content_type) {
			output.push_back(mxt::content::content_record_to_dictionary(record));
		}
	}
	return output;
}

Array MxtContentCatalog::find_gameplay(const String &content_type, const String &gameplay_digest) const
{
	Array output;
	for (const mxt::content::ContentRecord &record : records) {
		if (mxt::content::content_type_name(record.content_type) == content_type &&
				record.gameplay_digest == gameplay_digest) {
			output.push_back(mxt::content::content_record_to_dictionary(record));
		}
	}
	return output;
}
