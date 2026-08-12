#include "content/track_package_builder.h"

#include "content/content_manifest.h"
#include "content/content_validator.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <vector>

using namespace godot;

namespace {

static String global_path(const String &path)
{
	if (path.begins_with("res://") || path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(path);
	}
	return path;
}

static Dictionary result_dictionary(bool valid, const std::vector<String> &errors)
{
	PackedStringArray packed_errors;
	for (const String &error : errors) packed_errors.push_back(error);
	Dictionary result;
	result["valid"] = valid;
	result["errors"] = packed_errors;
	result["warnings"] = PackedStringArray();
	return result;
}

static bool safe_package_root(const String &path, String &out_root)
{
	const String allowed_root = global_path("user://track_drafts");
	out_root = global_path(path);
	return out_root.begins_with(allowed_root + String("/")) && out_root.ends_with("/package");
}

static bool copy_payload(const String &source, const String &destination, std::vector<String> &errors)
{
	if (!FileAccess::file_exists(source)) {
		errors.push_back("track package source file does not exist: " + source);
		return false;
	}
	if (DirAccess::copy_absolute(source, destination) != OK) {
		errors.push_back("could not copy track package source: " + source);
		return false;
	}
	return true;
}

static bool write_manifest(const String &root, const Dictionary &manifest, std::vector<String> &errors)
{
	Ref<FileAccess> file = FileAccess::open(root.path_join("manifest.json"), FileAccess::WRITE);
	if (file.is_null()) {
		errors.push_back("could not write track package manifest");
		return false;
	}
	file->store_string(JSON::stringify(manifest, "  ", true, true));
	if (file->get_error() != OK) {
		errors.push_back("could not finish writing track package manifest");
		return false;
	}
	return true;
}

} // namespace

void MxtTrackPackageBuilder::_bind_methods()
{
	ClassDB::bind_method(
			D_METHOD("build_package", "track_path", "visual_path", "metadata_path", "preview_path", "package_root", "title", "description", "author_name"),
			&MxtTrackPackageBuilder::build_package);
}

Dictionary MxtTrackPackageBuilder::build_package(
		const String &track_path,
		const String &visual_path,
		const String &metadata_path,
		const String &preview_path,
		const String &package_root,
		const String &title,
		const String &description,
		const String &author_name) const
{
	std::vector<String> errors;
	String root;
	if (!safe_package_root(package_root, root)) {
		errors.push_back("track package output must be below user://track_drafts and end in /package");
		return result_dictionary(false, errors);
	}
	if (title.is_empty() || title.length() > 128 || description.length() > 8000 ||
			author_name.is_empty() || author_name.length() > 64) {
		errors.push_back("track title, description, or author text is invalid");
		return result_dictionary(false, errors);
	}
	const String source_track = global_path(track_path);
	const String source_visual = global_path(visual_path);
	const String source_metadata = global_path(metadata_path);
	const String source_preview = global_path(preview_path);
	if (source_track.get_extension().to_lower() != "mxt_track" ||
			source_visual.get_extension().to_lower() != "glb" ||
			source_metadata.get_extension().to_lower() != "json" ||
			source_preview.get_extension().to_lower() != "png") {
		errors.push_back("track packages require .mxt_track, .glb, .json, and .png source files");
		return result_dictionary(false, errors);
	}
	if (DirAccess::make_dir_recursive_absolute(root.path_join("track")) != OK ||
			!copy_payload(source_track, root.path_join("track/track.mxt_track"), errors) ||
			!copy_payload(source_visual, root.path_join("track/visual.glb"), errors) ||
			!copy_payload(source_metadata, root.path_join("track/metadata.json"), errors) ||
			!copy_payload(source_preview, root.path_join("preview.png"), errors)) {
		return result_dictionary(false, errors);
	}
	Dictionary payload;
	payload["track"] = "track/track.mxt_track";
	payload["visual"] = "track/visual.glb";
	payload["metadata"] = "track/metadata.json";
	Dictionary hashes;
	for (const String &path : {
			 String("track/track.mxt_track"), String("track/visual.glb"),
			 String("track/metadata.json"), String("preview.png")}) {
		hashes[path] = FileAccess::get_sha256(root.path_join(path));
	}
	Dictionary manifest;
	manifest["format_revision"] = static_cast<int64_t>(mxt::content::PACKAGE_FORMAT_REVISION);
	manifest["content_type"] = "track";
	manifest["title"] = title;
	manifest["description"] = description;
	manifest["author_name"] = author_name;
	manifest["payload"] = payload;
	manifest["payload_sha256"] = hashes;
	if (!write_manifest(root, manifest, errors)) return result_dictionary(false, errors);
	mxt::content::ValidatedPackage package;
	if (!mxt::content::validate_package_directory_internal(root, package, errors)) {
		return result_dictionary(false, errors);
	}
	Dictionary result = result_dictionary(true, errors);
	result["package_path"] = root;
	result["package_digest"] = package.package_digest;
	result["gameplay_digest"] = package.gameplay_digest;
	return result;
}
