#include "content/content_catalog.h"

#include "content/content_validator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
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
		case ContentSource::LOCAL_LOOSE: return String("local_loose");
		case ContentSource::LOCAL_PACKAGE: return String("local_package");
		case ContentSource::LOCAL_DRAFT: return String("local_draft");
		case ContentSource::WORKSHOP: return String("workshop");
	}
	return String();
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

static void add_error(std::vector<String> &errors, const String &message)
{
	errors.push_back(message);
}

static bool read_file(const String &path, PackedByteArray &out_bytes, std::vector<String> &errors)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		add_error(errors, "could not open package file '" + path + "'");
		return false;
	}
	const uint64_t length = file->get_length();
	out_bytes = file->get_buffer(static_cast<int64_t>(length));
	if (static_cast<uint64_t>(out_bytes.size()) != length) {
		add_error(errors, "could not read complete package file '" + path + "'");
		return false;
	}
	return true;
}

static std::vector<String> manifest_paths(const mxt::content::ContentManifest &manifest)
{
	std::vector<String> paths;
	paths.reserve(manifest.files.size() + 1);
	paths.push_back("manifest.json");
	for (const mxt::content::ManifestFile &file : manifest.files) {
		paths.push_back(file.path);
	}
	return paths;
}

static void remove_snapshot_staging(const String &root_path, const std::vector<String> &paths)
{
	for (const String &path : paths) {
		DirAccess::remove_absolute(root_path.path_join(path));
	}
	DirAccess::remove_absolute(root_path.path_join("vehicle"));
	DirAccess::remove_absolute(root_path.path_join("track"));
	DirAccess::remove_absolute(root_path);
}

static bool materialize_snapshot(
		const String &package_root,
		const String &library_root,
		mxt::content::ValidatedPackage &out_package,
		std::vector<String> &out_paths,
		std::vector<String> &errors)
{
	const String source_root = global_path(package_root);
	const String use_library_root = global_path(library_root);
	PackedByteArray manifest_bytes;
	mxt::content::ContentManifest manifest;
	if (!read_file(source_root.path_join("manifest.json"), manifest_bytes, errors) ||
			!mxt::content::parse_manifest(manifest_bytes, manifest, errors)) {
		return false;
	}
	out_paths = manifest_paths(manifest);
	if (DirAccess::make_dir_recursive_absolute(use_library_root) != OK) {
		add_error(errors, "could not create test-drive snapshot library");
		return false;
	}
	const String staging_path = use_library_root.path_join(
			String(".mxt-snapshot-") + String::num_uint64(Time::get_singleton()->get_ticks_usec()));
	if (DirAccess::make_dir_recursive_absolute(
			staging_path.path_join(mxt::content::content_type_name(manifest.content_type))) != OK) {
		add_error(errors, "could not create test-drive snapshot staging directory");
		return false;
	}
	for (const String &path : out_paths) {
		if (DirAccess::copy_absolute(source_root.path_join(path), staging_path.path_join(path)) != OK) {
			add_error(errors, "could not copy test-drive package file '" + path + "'");
			remove_snapshot_staging(staging_path, out_paths);
			return false;
		}
	}
	if (!mxt::content::validate_package_directory_internal(staging_path, out_package, errors)) {
		remove_snapshot_staging(staging_path, out_paths);
		return false;
	}
	return true;
}

static bool install_snapshot(
		mxt::content::ValidatedPackage &package,
		const String &library_root,
		const std::vector<String> &paths,
		std::vector<String> &errors)
{
	const String use_library_root = global_path(library_root);
	const String staging_path = package.root_path;
	const String final_path = use_library_root.path_join(package.package_digest.substr(7));
	String displaced_path;
	if (DirAccess::open(final_path).is_valid()) {
		displaced_path = use_library_root.path_join(
				String(".") + package.package_digest.substr(7) + String(".obsolete-") +
				String::num_uint64(Time::get_singleton()->get_ticks_usec()));
		if (DirAccess::rename_absolute(final_path, displaced_path) != OK) {
			add_error(errors, "could not replace the existing test-drive snapshot");
			remove_snapshot_staging(staging_path, paths);
			return false;
		}
	}
	if (DirAccess::rename_absolute(staging_path, final_path) != OK) {
		add_error(errors, "could not install the validated test-drive snapshot");
		if (!displaced_path.is_empty()) {
			DirAccess::rename_absolute(displaced_path, final_path);
		}
		remove_snapshot_staging(staging_path, paths);
		return false;
	}
	package.root_path = final_path;
	return true;
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
		if (!package.manifest.manual_boost_sfx_path.is_empty()) {
			record.manual_boost_sfx_path = package.root_path.path_join(package.manifest.manual_boost_sfx_path);
		}
		if (!package.manifest.albedo_texture_path.is_empty()) {
			record.albedo_texture_path = package.root_path.path_join(package.manifest.albedo_texture_path);
		}
		if (!package.manifest.normal_texture_path.is_empty()) {
			record.normal_texture_path = package.root_path.path_join(package.manifest.normal_texture_path);
		}
		if (!package.manifest.paint_mask_texture_path.is_empty()) {
			record.paint_mask_texture_path = package.root_path.path_join(package.manifest.paint_mask_texture_path);
		}
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

static bool records_match(
		const mxt::content::ContentRecord &a,
		const mxt::content::ContentRecord &b)
{
	return a.content_type == b.content_type &&
			a.source == b.source &&
			a.content_id == b.content_id &&
			a.package_digest == b.package_digest &&
			a.gameplay_digest == b.gameplay_digest &&
			a.root_path == b.root_path &&
			a.published_file_id == b.published_file_id;
}

static void append_unique(Array &values, const Variant &value)
{
	if (!values.has(value)) values.push_back(value);
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

static Ref<MxtContentRecord> make_record_ref(const mxt::content::ContentRecord &record)
{
	Ref<MxtContentRecord> output;
	output.instantiate();
	output->set_record(record);
	return output;
}

static Ref<MxtContentLoadResult> make_load_result(
		MxtContentLoadResult::ResultCode code,
		const std::vector<String> &errors = {})
{
	Ref<MxtContentLoadResult> output;
	output.instantiate();
	output->set_code(code);
	output->set_errors(errors);
	return output;
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
	ClassDB::bind_method(
			D_METHOD("add_loose_track", "title", "track_path", "visual_path", "metadata_path"),
			&MxtContentCatalog::add_loose_track);
	ClassDB::bind_method(D_METHOD("clear_loose_tracks"), &MxtContentCatalog::clear_loose_tracks);
	ClassDB::bind_method(D_METHOD("add_local_package", "package_root"), &MxtContentCatalog::add_local_package);
	ClassDB::bind_method(D_METHOD("add_draft_package", "package_root"), &MxtContentCatalog::add_draft_package);
	ClassDB::bind_method(
			D_METHOD("snapshot_draft_package", "package_root", "library_root"),
			&MxtContentCatalog::snapshot_draft_package);
	ClassDB::bind_method(D_METHOD("add_workshop_package", "package_root", "published_file_id"), &MxtContentCatalog::add_workshop_package);
	ClassDB::bind_method(D_METHOD("sync_workshop_packages", "items"), &MxtContentCatalog::sync_workshop_packages);
	ClassDB::bind_method(D_METHOD("scan_local_library", "library_root"), &MxtContentCatalog::scan_local_library);
	ClassDB::bind_method(D_METHOD("remove_content", "content_id"), &MxtContentCatalog::remove_content);
	ClassDB::bind_method(D_METHOD("clear"), &MxtContentCatalog::clear);
	ClassDB::bind_method(D_METHOD("has_content", "content_id"), &MxtContentCatalog::has_content);
	ClassDB::bind_method(D_METHOD("resolve_content", "content_id"), &MxtContentCatalog::resolve_content);
	ClassDB::bind_method(D_METHOD("get_records", "content_type"), &MxtContentCatalog::get_records, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("find_gameplay", "content_type", "gameplay_digest"), &MxtContentCatalog::find_gameplay);
	ClassDB::bind_method(D_METHOD("get_generation"), &MxtContentCatalog::get_generation);
	ADD_SIGNAL(MethodInfo("catalog_changed", PropertyInfo(Variant::INT, "generation")));
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_official_vehicle(
		const String &slug,
		const String &title,
		const String &properties_path,
		const String &definition_path)
{
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
		return make_load_result(MxtContentLoadResult::RESULT_VALIDATION_FAILED, errors);
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
	Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
	result->set_record(record);
	return result;
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_official_track(
		const String &slug,
		const String &title,
		const String &track_path,
		const String &visual_path,
		const String &metadata_path,
		const String &expected_gameplay_digest)
{
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
		return make_load_result(MxtContentLoadResult::RESULT_VALIDATION_FAILED, errors);
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
	Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
	result->set_record(record);
	return result;
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_loose_track(
		const String &title,
		const String &track_path,
		const String &visual_path,
		const String &metadata_path)
{
	std::vector<String> errors;
	if (title.is_empty() || title.length() > 128) {
		errors.push_back("loose track title must contain 1 to 128 characters");
	}
	if (track_path.is_empty() || metadata_path.is_empty()) {
		errors.push_back("loose track and metadata paths must not be empty");
	}
	if (!metadata_path.is_empty() && !FileAccess::file_exists(global_path(metadata_path))) {
		errors.push_back("loose track metadata file does not exist");
	}
	if (!visual_path.is_empty() && !FileAccess::file_exists(global_path(visual_path))) {
		errors.push_back("loose track visual file does not exist");
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
	if (!errors.empty()) {
		return make_load_result(MxtContentLoadResult::RESULT_VALIDATION_FAILED, errors);
	}
	for (const mxt::content::ContentRecord &existing : records) {
		if (existing.content_type == mxt::content::ContentType::TRACK &&
			existing.source == mxt::content::ContentSource::OFFICIAL &&
			existing.gameplay_digest == gameplay_digest) {
			Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
			result->set_record(existing);
			return result;
		}
	}

	mxt::content::ContentRecord record;
	record.content_type = mxt::content::ContentType::TRACK;
	record.source = mxt::content::ContentSource::LOCAL_LOOSE;
	record.content_id = "mxt:track:local:" + gameplay_digest.substr(7);
	record.gameplay_digest = gameplay_digest;
	record.root_path = track_path.get_base_dir();
	record.authoritative_path = track_path;
	record.visual_path = visual_path;
	record.metadata_path = metadata_path;
	record.title = title;
	for (const mxt::content::ContentRecord &existing : records) {
		if (existing.content_id == record.content_id) {
			Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
			result->set_record(existing);
			return result;
		}
	}
	replace_record(record);
	Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
	result->set_record(record);
	return result;
}

void MxtContentCatalog::clear_loose_tracks()
{
	const size_t previous_size = records.size();
	records.erase(std::remove_if(records.begin(), records.end(), [](const auto &record) {
		return record.source == mxt::content::ContentSource::LOCAL_LOOSE;
	}), records.end());
	if (records.size() != previous_size) publish_change();
}

void MxtContentCatalog::publish_change()
{
	++generation;
	emit_signal("catalog_changed", static_cast<int64_t>(generation));
}

void MxtContentCatalog::replace_record(const mxt::content::ContentRecord &record, bool publish)
{
	for (mxt::content::ContentRecord &existing : records) {
		if (existing.content_id == record.content_id) {
			existing = record;
			std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
				return a.content_id < b.content_id;
			});
			if (publish) publish_change();
			return;
		}
	}
	records.push_back(record);
	std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
		return a.content_id < b.content_id;
	});
	if (publish) publish_change();
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_package_internal(
		const String &package_root,
		mxt::content::ContentSource source,
		uint64_t published_file_id)
{
	std::vector<String> errors;
	mxt::content::ValidatedPackage package;
	const String root = global_path(package_root);
	if (!mxt::content::validate_package_directory_internal(root, package, errors)) {
		Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_VALIDATION_FAILED, errors);
		result->set_validation_profile(package.validation_profile);
		return result;
	}
	const uint64_t catalog_start_usec = Time::get_singleton()->get_ticks_usec();
	const mxt::content::ContentRecord record = make_record(package, source, published_file_id);
	replace_record(record);
	Dictionary validation_profile = package.validation_profile;
	validation_profile["catalog_record_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - catalog_start_usec);
	Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
	result->set_record(record);
	result->set_validation_profile(validation_profile);
	return result;
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_local_package(const String &package_root)
{
	return add_package_internal(package_root, mxt::content::ContentSource::LOCAL_PACKAGE, 0);
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_draft_package(const String &package_root)
{
	return add_package_internal(package_root, mxt::content::ContentSource::LOCAL_DRAFT, 0);
}

Ref<MxtContentLoadResult> MxtContentCatalog::snapshot_draft_package(
		const String &package_root,
		const String &library_root)
{
	std::vector<String> errors;
	std::vector<String> snapshot_paths;
	mxt::content::ValidatedPackage package;
	if (!materialize_snapshot(package_root, library_root, package, snapshot_paths, errors)) {
		Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_IO_ERROR, errors);
		result->set_validation_profile(package.validation_profile);
		return result;
	}
	const String final_path = global_path(library_root).path_join(package.package_digest.substr(7));
	for (const mxt::content::ContentRecord &existing : records) {
		if (existing.source == mxt::content::ContentSource::LOCAL_DRAFT &&
				existing.package_digest == package.package_digest &&
				existing.root_path == final_path &&
				DirAccess::open(final_path).is_valid()) {
			remove_snapshot_staging(package.root_path, snapshot_paths);
			Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
			result->set_record(existing);
			result->set_package_path(existing.root_path);
			result->set_package_digest(existing.package_digest);
			result->set_gameplay_digest(existing.gameplay_digest);
			result->set_validation_profile(package.validation_profile);
			result->set_reused_existing(true);
			return result;
		}
	}
	if (!install_snapshot(package, library_root, snapshot_paths, errors)) {
		Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_IO_ERROR, errors);
		result->set_validation_profile(package.validation_profile);
		return result;
	}
	const uint64_t catalog_start_usec = Time::get_singleton()->get_ticks_usec();
	const mxt::content::ContentRecord record = make_record(
			package, mxt::content::ContentSource::LOCAL_DRAFT, 0);
	replace_record(record);
	Dictionary validation_profile = package.validation_profile;
	validation_profile["catalog_record_usec"] = static_cast<int64_t>(
			Time::get_singleton()->get_ticks_usec() - catalog_start_usec);
	Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
	result->set_record(record);
	result->set_package_path(package.root_path);
	result->set_package_digest(package.package_digest);
	result->set_gameplay_digest(package.gameplay_digest);
	result->set_validation_profile(validation_profile);
	result->set_reused_existing(false);
	return result;
}

Ref<MxtContentLoadResult> MxtContentCatalog::add_workshop_package(const String &package_root, int64_t published_file_id)
{
	uint64_t item_id = 0;
	if (!decimal_u64(published_file_id, item_id)) {
		return make_load_result(
				MxtContentLoadResult::RESULT_INVALID_INPUT,
				{"published_file_id must be a positive integer"});
	}
	return add_package_internal(package_root, mxt::content::ContentSource::WORKSHOP, item_id);
}

Dictionary MxtContentCatalog::sync_workshop_packages(const Array &items)
{
	struct IncomingItem {
		uint64_t published_file_id = 0;
		String install_path;
		String install_identity;
		bool eligible = false;
	};
	std::vector<IncomingItem> incoming;
	incoming.reserve(static_cast<size_t>(items.size()));
	for (int64_t i = 0; i < items.size(); ++i) {
		if (items[i].get_type() != Variant::DICTIONARY) continue;
		const Dictionary item = items[i];
		uint64_t published_file_id = 0;
		if (!decimal_u64(static_cast<int64_t>(item.get("published_file_id", 0)), published_file_id)) continue;
		if (std::any_of(incoming.begin(), incoming.end(), [&](const IncomingItem &value) {
			return value.published_file_id == published_file_id;
		})) continue;
		IncomingItem value;
		value.published_file_id = published_file_id;
		value.install_path = static_cast<String>(item.get("install_path", String()));
		value.eligible = static_cast<bool>(item.get("installed", false)) &&
				!static_cast<bool>(item.get("locally_disabled", false)) &&
				!value.install_path.is_empty();
		const String manifest_path = value.install_path.path_join("manifest.json");
		const uint64_t manifest_modified = FileAccess::file_exists(manifest_path)
				? FileAccess::get_modified_time(manifest_path)
				: 0;
		value.install_identity = value.install_path + String("|") +
				String::num_int64(static_cast<int64_t>(item.get("install_timestamp", 0))) + String("|") +
				String::num_int64(static_cast<int64_t>(item.get("size_on_disk", 0))) + String("|") +
				String::num_uint64(manifest_modified);
		incoming.push_back(std::move(value));
	}
	std::sort(incoming.begin(), incoming.end(), [](const IncomingItem &a, const IncomingItem &b) {
		return a.published_file_id < b.published_file_id;
	});

	Array result_items;
	Array added_item_ids;
	Array changed_item_ids;
	Array removed_item_ids;
	Array added_content_ids;
	Array changed_content_ids;
	Array removed_content_ids;
	int64_t cache_hits = 0;
	int64_t cache_misses = 0;
	bool catalog_changed = false;

	auto erase_record = [&](const String &content_id) {
		const size_t previous_size = records.size();
		records.erase(std::remove_if(records.begin(), records.end(), [&](const auto &record) {
			return record.content_id == content_id;
		}), records.end());
		return records.size() != previous_size;
	};
	auto state_result = [&](const WorkshopPackageState &state, bool cache_hit, bool eligible) {
		Dictionary item_result;
		item_result["published_file_id"] = static_cast<int64_t>(state.published_file_id);
		item_result["install_path"] = state.install_path;
		item_result["eligible"] = eligible;
		item_result["cache_hit"] = cache_hit;
		item_result["valid"] = state.valid;
		item_result["registration_usec"] = static_cast<int64_t>(state.registration_usec);
		item_result["errors"] = error_array(state.errors);
		item_result["validation_profile"] = state.package.validation_profile;
		if (state.package.manifest.content_type != mxt::content::ContentType::INVALID) {
			item_result["manifest"] = mxt::content::manifest_to_dictionary(state.package.manifest);
		}
		if (state.valid) {
			item_result["package_digest"] = state.package.package_digest;
			item_result["gameplay_digest"] = state.package.gameplay_digest;
			item_result["record"] = make_record_ref(state.record);
		}
		return item_result;
	};

	for (const IncomingItem &item : incoming) {
		auto state_it = std::find_if(workshop_packages.begin(), workshop_packages.end(), [&](const auto &state) {
			return state.published_file_id == item.published_file_id;
		});
		if (!item.eligible) {
			WorkshopPackageState empty_state;
			empty_state.published_file_id = item.published_file_id;
			empty_state.install_path = item.install_path;
			if (state_it != workshop_packages.end()) {
				if (state_it->valid && erase_record(state_it->record.content_id)) {
					append_unique(removed_content_ids, state_it->record.content_id);
					catalog_changed = true;
				}
				append_unique(removed_item_ids, static_cast<int64_t>(item.published_file_id));
				workshop_packages.erase(state_it);
			}
			result_items.push_back(state_result(empty_state, false, false));
			continue;
		}
		if (state_it != workshop_packages.end() && state_it->install_identity == item.install_identity) {
			++cache_hits;
			result_items.push_back(state_result(*state_it, true, true));
			continue;
		}

		++cache_misses;
		WorkshopPackageState next;
		next.published_file_id = item.published_file_id;
		next.install_path = item.install_path;
		next.install_identity = item.install_identity;
		next.valid = mxt::content::validate_package_directory_internal(
				item.install_path, next.package, next.errors);
		if (next.valid) {
			const uint64_t registration_start_usec = Time::get_singleton()->get_ticks_usec();
			next.record = make_record(next.package, mxt::content::ContentSource::WORKSHOP, item.published_file_id);
			next.registration_usec = Time::get_singleton()->get_ticks_usec() - registration_start_usec;
		}

		const bool had_state = state_it != workshop_packages.end();
		const bool had_valid_record = had_state && state_it->valid;
		const String previous_content_id = had_valid_record ? state_it->record.content_id : String();
		const bool same_record = had_valid_record && next.valid && records_match(state_it->record, next.record);
		if (!same_record && had_valid_record && erase_record(previous_content_id)) {
			append_unique(removed_content_ids, previous_content_id);
			catalog_changed = true;
		}
		if (!same_record && next.valid) {
			replace_record(next.record, false);
			catalog_changed = true;
			if (had_valid_record && previous_content_id == next.record.content_id) {
				removed_content_ids.erase(previous_content_id);
				append_unique(changed_content_ids, next.record.content_id);
			} else {
				append_unique(added_content_ids, next.record.content_id);
			}
		}
		if (had_state) {
			append_unique(changed_item_ids, static_cast<int64_t>(item.published_file_id));
			*state_it = std::move(next);
			result_items.push_back(state_result(*state_it, false, true));
		} else {
			append_unique(added_item_ids, static_cast<int64_t>(item.published_file_id));
			workshop_packages.push_back(std::move(next));
			result_items.push_back(state_result(workshop_packages.back(), false, true));
		}
	}

	for (size_t i = workshop_packages.size(); i-- > 0;) {
		const bool present = std::any_of(incoming.begin(), incoming.end(), [&](const IncomingItem &item) {
			return item.published_file_id == workshop_packages[i].published_file_id;
		});
		if (present) continue;
		append_unique(removed_item_ids, static_cast<int64_t>(workshop_packages[i].published_file_id));
		if (workshop_packages[i].valid && erase_record(workshop_packages[i].record.content_id)) {
			append_unique(removed_content_ids, workshop_packages[i].record.content_id);
			catalog_changed = true;
		}
		workshop_packages.erase(workshop_packages.begin() + static_cast<int64_t>(i));
	}
	std::sort(workshop_packages.begin(), workshop_packages.end(), [](const auto &a, const auto &b) {
		return a.published_file_id < b.published_file_id;
	});
	std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
		return a.content_id < b.content_id;
	});
	if (catalog_changed) publish_change();

	PackedStringArray signature_parts;
	for (const WorkshopPackageState &state : workshop_packages) {
		if (!state.valid) continue;
		signature_parts.push_back(
				String::num_uint64(state.published_file_id) + String("|") +
				mxt::content::content_type_name(state.package.manifest.content_type) + String("|") +
				state.package.package_digest + String("|") + state.package.gameplay_digest + String("|") +
				state.package.root_path);
	}
	Dictionary delta;
	delta["added_item_ids"] = added_item_ids;
	delta["changed_item_ids"] = changed_item_ids;
	delta["removed_item_ids"] = removed_item_ids;
	delta["added_content_ids"] = added_content_ids;
	delta["changed_content_ids"] = changed_content_ids;
	delta["removed_content_ids"] = removed_content_ids;
	Dictionary result;
	result["valid"] = true;
	result["items"] = result_items;
	result["delta"] = delta;
	result["catalog_changed"] = catalog_changed;
	result["validation_cache_hit_count"] = cache_hits;
	result["validation_cache_miss_count"] = cache_misses;
	result["catalog_signature"] = String("\n").join(signature_parts);
	return result;
}

Ref<MxtContentLoadResult> MxtContentCatalog::scan_local_library(const String &library_root)
{
	const String root = global_path(library_root);
	Ref<DirAccess> directory = DirAccess::open(root);
	if (directory.is_null()) {
		return make_load_result(
				MxtContentLoadResult::RESULT_IO_ERROR,
				{"local content library directory does not exist"});
	}
	directory->set_include_hidden(true);
	directory->set_include_navigational(false);
	if (directory->list_dir_begin() != OK) {
		return make_load_result(
				MxtContentLoadResult::RESULT_IO_ERROR,
				{"could not enumerate local content library directory"});
	}
	std::vector<mxt::content::ContentRecord> candidates;
	Ref<MxtContentLoadResult> result = make_load_result(MxtContentLoadResult::RESULT_OK);
	for (;;) {
		const String name = directory->get_next();
		if (name.is_empty()) break;
		if (name.begins_with(".mxt-import-")) continue;
		const String diagnostic_path = root.path_join(name);
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
			result->add_diagnostic(diagnostic_path, errors);
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
	result->set_code(result->get_diagnostic_count() == 0
			? MxtContentLoadResult::RESULT_OK
			: MxtContentLoadResult::RESULT_VALIDATION_FAILED);
	result->set_registered_count(static_cast<int32_t>(candidates.size()));
	return result;
}

bool MxtContentCatalog::remove_content(const String &content_id)
{
	const auto found = std::find_if(records.begin(), records.end(), [&](const auto &record) {
		return record.content_id == content_id;
	});
	if (found == records.end()) return false;
	if (found->source == mxt::content::ContentSource::WORKSHOP) {
		const uint64_t published_file_id = found->published_file_id;
		workshop_packages.erase(std::remove_if(workshop_packages.begin(), workshop_packages.end(), [&](const auto &state) {
			return state.published_file_id == published_file_id;
		}), workshop_packages.end());
	}
	records.erase(found);
	publish_change();
	return true;
}

void MxtContentCatalog::clear()
{
	if (records.empty() && workshop_packages.empty()) return;
	records.clear();
	workshop_packages.clear();
	publish_change();
}

bool MxtContentCatalog::has_content(const String &content_id) const
{
	return std::any_of(records.begin(), records.end(), [&](const auto &record) {
		return record.content_id == content_id;
	});
}

Ref<MxtContentRecord> MxtContentCatalog::resolve_content(const String &content_id) const
{
	for (const mxt::content::ContentRecord &record : records) {
		if (record.content_id == content_id) {
			return make_record_ref(record);
		}
	}
	return Ref<MxtContentRecord>();
}

Array MxtContentCatalog::get_records(const String &content_type) const
{
	Array output;
	for (const mxt::content::ContentRecord &record : records) {
		if (content_type.is_empty() || mxt::content::content_type_name(record.content_type) == content_type) {
			output.push_back(make_record_ref(record));
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
			output.push_back(make_record_ref(record));
		}
	}
	return output;
}
