#include "content/content_package_io.h"

#include "content/content_archive.h"
#include "content/content_manifest.h"
#include "content/content_validator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/zip_packer.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace godot;

namespace {

static void add_error(std::vector<String> &errors, const String &message)
{
	errors.push_back(message);
}

static PackedStringArray error_array(const std::vector<String> &errors)
{
	PackedStringArray output;
	for (const String &error : errors) {
		output.push_back(error);
	}
	return output;
}

static Dictionary result_dictionary(bool valid, const std::vector<String> &errors)
{
	Dictionary result;
	result["valid"] = valid;
	result["errors"] = error_array(errors);
	return result;
}

static String global_path(const String &path)
{
	if (path.begins_with("res://") || path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(path);
	}
	return path;
}

static const mxt::content::ArchiveEntry *find_entry(
		const std::vector<mxt::content::ArchiveEntry> &entries,
		const String &path)
{
	for (const mxt::content::ArchiveEntry &entry : entries) {
		if (entry.path == path) return &entry;
	}
	return nullptr;
}

static std::vector<String> expected_paths(const mxt::content::ContentManifest &manifest)
{
	std::vector<String> paths;
	paths.reserve(manifest.files.size() + 1);
	paths.push_back("manifest.json");
	for (const mxt::content::ManifestFile &file : manifest.files) {
		paths.push_back(file.path);
	}
	return paths;
}

static std::string utf8_path(const String &path)
{
	const CharString encoded = path.utf8();
	return std::string(encoded.get_data(), static_cast<size_t>(encoded.length()));
}

static bool archive_matches_manifest(
		const std::vector<mxt::content::ArchiveEntry> &entries,
		const mxt::content::ContentManifest &manifest,
		std::vector<String> &errors)
{
	const std::vector<String> expected = expected_paths(manifest);
	const String directory_entry = mxt::content::content_type_name(manifest.content_type) + String("/");
	if (entries.size() != expected.size() + 1 || !find_entry(entries, directory_entry)) {
		add_error(errors, ".mxtpkg entries do not exactly match its manifest");
		return false;
	}
	for (const String &path : expected) {
		if (!find_entry(entries, path)) {
			add_error(errors, ".mxtpkg is missing manifest-declared entry '" + path + "'");
			return false;
		}
	}
	return true;
}

static bool read_archive_manifest(
		const String &archive_path,
		const std::vector<mxt::content::ArchiveEntry> &entries,
		mxt::content::ContentManifest &out_manifest,
		std::vector<String> &errors)
{
	const mxt::content::ArchiveEntry *manifest_entry = find_entry(entries, "manifest.json");
	if (!manifest_entry) {
		add_error(errors, ".mxtpkg does not contain manifest.json");
		return false;
	}
	Ref<ZIPReader> reader;
	reader.instantiate();
	if (reader->open(archive_path) != OK) {
		add_error(errors, "Godot could not open the preflighted .mxtpkg archive");
		return false;
	}
	const PackedByteArray bytes = reader->read_file("manifest.json", true);
	reader->close();
	if (static_cast<uint64_t>(bytes.size()) != manifest_entry->uncompressed_size) {
		add_error(errors, "manifest.json failed ZIP decompression or CRC validation");
		return false;
	}
	return mxt::content::parse_manifest(bytes, out_manifest, errors);
}

static void remove_staging_package(
		const String &staging_path,
		const std::vector<mxt::content::ArchiveEntry> &entries)
{
	for (const mxt::content::ArchiveEntry &entry : entries) {
		if (entry.path.ends_with("/")) {
			continue;
		}
		DirAccess::remove_absolute(staging_path.path_join(entry.path));
	}
	DirAccess::remove_absolute(staging_path.path_join("vehicle"));
	DirAccess::remove_absolute(staging_path.path_join("track"));
	DirAccess::remove_absolute(staging_path);
}

static bool extract_archive(
		const String &archive_path,
		const String &staging_path,
		const mxt::content::ContentManifest &manifest,
		const std::vector<mxt::content::ArchiveEntry> &entries,
		std::vector<String> &errors)
{
	if (DirAccess::make_dir_recursive_absolute(staging_path) != OK ||
			DirAccess::make_dir_recursive_absolute(staging_path.path_join(mxt::content::content_type_name(manifest.content_type))) != OK) {
		add_error(errors, "could not create .mxtpkg import staging directory");
		return false;
	}
	Ref<ZIPReader> reader;
	reader.instantiate();
	if (reader->open(archive_path) != OK) {
		add_error(errors, "Godot could not open the preflighted .mxtpkg archive");
		return false;
	}
	for (const mxt::content::ArchiveEntry &entry : entries) {
		if (entry.path.ends_with("/")) {
			continue;
		}
		const PackedByteArray bytes = reader->read_file(entry.path, true);
		if (static_cast<uint64_t>(bytes.size()) != entry.uncompressed_size) {
			add_error(errors, "entry failed ZIP decompression or CRC validation: '" + entry.path + "'");
			reader->close();
			return false;
		}
		Ref<FileAccess> output = FileAccess::open(staging_path.path_join(entry.path), FileAccess::WRITE);
		if (output.is_null()) {
			add_error(errors, "could not create extracted package file '" + entry.path + "'");
			reader->close();
			return false;
		}
		output->store_buffer(bytes);
		output->close();
	}
	reader->close();
	return true;
}

static bool read_file(const String &path, PackedByteArray &out, std::vector<String> &errors)
{
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		add_error(errors, "could not open package file '" + path + "'");
		return false;
	}
	const uint64_t length = file->get_length();
	out = file->get_buffer(static_cast<int64_t>(length));
	if (static_cast<uint64_t>(out.size()) != length) {
		add_error(errors, "could not read complete package file '" + path + "'");
		return false;
	}
	return true;
}

static String temporary_path_for(const String &base, const String &label)
{
	return base + String(".") + label + String("-") + String::num_uint64(Time::get_singleton()->get_ticks_usec());
}

} // namespace

void MxtContentPackageIO::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("inspect_mxtpkg", "archive_path"), &MxtContentPackageIO::inspect_mxtpkg);
	ClassDB::bind_method(D_METHOD("import_mxtpkg", "archive_path", "library_root"), &MxtContentPackageIO::import_mxtpkg);
	ClassDB::bind_method(D_METHOD("export_mxtpkg", "package_root", "archive_path"), &MxtContentPackageIO::export_mxtpkg);
}

Dictionary MxtContentPackageIO::inspect_mxtpkg(const String &archive_path) const
{
	const String use_archive_path = global_path(archive_path);
	std::vector<mxt::content::ArchiveEntry> entries;
	std::vector<String> errors;
	if (!mxt::content::inspect_mxtpkg_archive(use_archive_path, entries, errors)) {
		return result_dictionary(false, errors);
	}
	mxt::content::ContentManifest manifest;
	if (!read_archive_manifest(use_archive_path, entries, manifest, errors) ||
			!archive_matches_manifest(entries, manifest, errors)) {
		return result_dictionary(false, errors);
	}
	Dictionary result = result_dictionary(true, errors);
	result["manifest"] = mxt::content::manifest_to_dictionary(manifest);
	Array result_entries;
	for (const mxt::content::ArchiveEntry &entry : entries) {
		Dictionary item;
		item["path"] = entry.path;
		item["compressed_size"] = static_cast<int64_t>(entry.compressed_size);
		item["uncompressed_size"] = static_cast<int64_t>(entry.uncompressed_size);
		result_entries.push_back(item);
	}
	result["entries"] = result_entries;
	return result;
}

Dictionary MxtContentPackageIO::import_mxtpkg(const String &archive_path, const String &library_root) const
{
	const String use_archive_path = global_path(archive_path);
	const String use_library_root = global_path(library_root);
	std::vector<mxt::content::ArchiveEntry> entries;
	std::vector<String> errors;
	if (!mxt::content::inspect_mxtpkg_archive(use_archive_path, entries, errors)) {
		return result_dictionary(false, errors);
	}
	mxt::content::ContentManifest manifest;
	if (!read_archive_manifest(use_archive_path, entries, manifest, errors) ||
			!archive_matches_manifest(entries, manifest, errors)) {
		return result_dictionary(false, errors);
	}
	if (DirAccess::make_dir_recursive_absolute(use_library_root) != OK) {
		add_error(errors, "could not create local content library directory");
		return result_dictionary(false, errors);
	}
	const String archive_hash = FileAccess::get_sha256(use_archive_path);
	const String staging_path = use_library_root.path_join(
			String(".mxt-import-") + archive_hash.substr(0, 16) + String("-") +
			String::num_uint64(Time::get_singleton()->get_ticks_usec()));
	if (!extract_archive(use_archive_path, staging_path, manifest, entries, errors)) {
		remove_staging_package(staging_path, entries);
		return result_dictionary(false, errors);
	}
	mxt::content::ValidatedPackage package;
	if (!mxt::content::validate_package_directory_internal(staging_path, package, errors)) {
		remove_staging_package(staging_path, entries);
		return result_dictionary(false, errors);
	}
	const String final_path = use_library_root.path_join(package.package_digest.substr(7));
	bool reused_existing = false;
	if (DirAccess::open(final_path).is_valid()) {
		mxt::content::ValidatedPackage existing;
		std::vector<String> existing_errors;
		if (!mxt::content::validate_package_directory_internal(final_path, existing, existing_errors) ||
				existing.package_digest != package.package_digest) {
			add_error(errors, "content library already contains an invalid package at the digest destination");
			remove_staging_package(staging_path, entries);
			return result_dictionary(false, errors);
		}
		reused_existing = true;
		remove_staging_package(staging_path, entries);
	} else if (DirAccess::rename_absolute(staging_path, final_path) != OK) {
		add_error(errors, "could not move validated package into the local content library");
		remove_staging_package(staging_path, entries);
		return result_dictionary(false, errors);
	}
	Dictionary result = result_dictionary(true, errors);
	result["package_path"] = final_path;
	result["package_digest"] = package.package_digest;
	result["gameplay_digest"] = package.gameplay_digest;
	result["content_type"] = mxt::content::content_type_name(package.manifest.content_type);
	result["reused_existing"] = reused_existing;
	return result;
}

Dictionary MxtContentPackageIO::export_mxtpkg(const String &package_root, const String &archive_path) const
{
	const String use_package_root = global_path(package_root);
	const String use_archive_path = global_path(archive_path);
	std::vector<String> errors;
	mxt::content::ValidatedPackage package;
	if (!mxt::content::validate_package_directory_internal(use_package_root, package, errors)) {
		return result_dictionary(false, errors);
	}
	std::vector<String> paths = expected_paths(package.manifest);
	std::sort(paths.begin(), paths.end(), [](const String &a, const String &b) {
		return utf8_path(a) < utf8_path(b);
	});
	const String output_parent = use_archive_path.get_base_dir();
	if (!output_parent.is_empty() && DirAccess::make_dir_recursive_absolute(output_parent) != OK) {
		add_error(errors, "could not create .mxtpkg output directory");
		return result_dictionary(false, errors);
	}
	const String temporary_path = temporary_path_for(use_archive_path, "mxt-export");
	Ref<ZIPPacker> packer;
	packer.instantiate();
	if (packer->open(temporary_path, ZIPPacker::APPEND_CREATE) != OK) {
		add_error(errors, "could not create .mxtpkg archive");
		return result_dictionary(false, errors);
	}
	bool packed = true;
	for (const String &path : paths) {
		PackedByteArray bytes;
		if (!read_file(use_package_root.path_join(path), bytes, errors) ||
				packer->start_file(path) != OK || packer->write_file(bytes) != OK || packer->close_file() != OK) {
			add_error(errors, "could not write .mxtpkg entry '" + path + "'");
			packed = false;
			break;
		}
	}
	if (packer->close() != OK) {
		add_error(errors, "could not finalize .mxtpkg archive");
		packed = false;
	}
	std::vector<mxt::content::ArchiveEntry> archived_entries;
	if (packed) {
		if (!mxt::content::inspect_mxtpkg_archive(temporary_path, archived_entries, errors) ||
				!archive_matches_manifest(archived_entries, package.manifest, errors)) {
			packed = false;
		}
	}
	if (!packed) {
		DirAccess::remove_absolute(temporary_path);
		return result_dictionary(false, errors);
	}
	if (FileAccess::file_exists(use_archive_path) && DirAccess::remove_absolute(use_archive_path) != OK) {
		add_error(errors, "could not replace existing .mxtpkg archive");
		DirAccess::remove_absolute(temporary_path);
		return result_dictionary(false, errors);
	}
	if (DirAccess::rename_absolute(temporary_path, use_archive_path) != OK) {
		add_error(errors, "could not move completed .mxtpkg archive into place");
		DirAccess::remove_absolute(temporary_path);
		return result_dictionary(false, errors);
	}
	Dictionary result = result_dictionary(true, errors);
	result["archive_path"] = use_archive_path;
	result["package_digest"] = package.package_digest;
	result["gameplay_digest"] = package.gameplay_digest;
	return result;
}
