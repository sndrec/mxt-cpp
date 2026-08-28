#include "car/car_authoring_session.h"

#include "content/content_manifest.h"
#include "content/content_validator.h"
#include "core/native_file_utils.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>

#include <cstddef>
#include <iterator>
#include <vector>

using namespace godot;
Dictionary MxtCarAuthoringSession::load_vehicle_package(const String &package_root)
{
	const String root = global_path(package_root);
	mxt::content::ValidatedPackage package;
	std::vector<String> validation_errors;
	if (!mxt::content::validate_package_directory_internal(root, package, validation_errors) ||
			package.manifest.content_type != mxt::content::ContentType::VEHICLE) {
		PackedStringArray errors;
		for (const String &error : validation_errors) errors.push_back(error);
		if (errors.is_empty()) errors.push_back("package is not a vehicle package");
		return result_dictionary(false, errors, {});
	}
	Dictionary loaded = load_file(root.path_join("vehicle/properties.mxt_car_props"));
	if (!static_cast<bool>(loaded.get("valid", false))) return loaded;
	Dictionary intent_result = set_authoring_intent(package.authoring_metadata);
	if (!static_cast<bool>(intent_result.get("valid", false))) return intent_result;
	model_path = root.path_join("vehicle/model.glb");
	set_model_transform(package.visual_metadata.get("model_transform", Dictionary()));
	Dictionary material_setup = package.visual_metadata.get("material_inputs", Dictionary());
	material_setup["body_surfaces"] = package.visual_metadata.get("body_surfaces", Array());
	set_material_setup(material_setup);
	set_manual_boost_volume_db(package.visual_metadata.get("manual_boost_volume_db", 0.0));
	set_thrusters(package.visual_metadata.get("thrusters", Array()));
	undo_history.clear();
	redo_history.clear();
	dirty = false;
	loaded["title"] = package.manifest.title;
	loaded["description"] = package.manifest.description;
	loaded["author_name"] = package.manifest.author_name;
	loaded["package_digest"] = package.package_digest;
	loaded["gameplay_digest"] = package.gameplay_digest;
	return loaded;
}

Dictionary MxtCarAuthoringSession::build_vehicle_package(
		const String &package_root,
		const String &preview_png_path,
		const String &title,
		const String &description,
		const String &author_name,
		bool validate_package)
{
	String root;
	const String draft_root = package_root.trim_suffix("/package").trim_suffix("\\package");
	if (!is_safe_draft_root(draft_root, root) || global_path(package_root) != root.path_join("package")) {
		return result_dictionary(false, single_error("vehicle package output must be the package directory of a user draft"), {});
	}
	root = root.path_join("package");
	if (!is_valid_text_field(title, 128, false) || !is_valid_text_field(description, 8000, true, true) ||
			!is_valid_text_field(author_name, 64, false)) {
		return result_dictionary(false, single_error("vehicle title, description, or author text is invalid"), {});
	}
	const String use_model_path = model_path;
	if (use_model_path.is_empty() || !FileAccess::file_exists(use_model_path)) {
		return result_dictionary(false, single_error("vehicle draft has no imported model"), {});
	}
	if (DirAccess::make_dir_recursive_absolute(root.path_join("vehicle")) != OK) {
		return result_dictionary(false, single_error("could not create vehicle package directory"), {});
	}
	struct TexturePayload {
		const char *draft_name;
		const char *package_path;
		const char *manifest_member;
	};
	static constexpr TexturePayload TEXTURE_PAYLOADS[] = {
		{"albedo.png", "vehicle/albedo.png", "albedo_texture"},
		{"normal.png", "vehicle/normal.png", "normal_texture"},
		{"paint_mask.png", "vehicle/paint_mask.png", "paint_mask_texture"},
	};
	bool packaged_textures[std::size(TEXTURE_PAYLOADS)] = {};
	for (size_t i = 0; i < std::size(TEXTURE_PAYLOADS); ++i) {
		const TexturePayload &texture = TEXTURE_PAYLOADS[i];
		const String source = draft_root.path_join(texture.draft_name);
		const String destination = root.path_join(texture.package_path);
		DirAccess::remove_absolute(destination);
		if (!FileAccess::file_exists(source)) continue;
		if (DirAccess::copy_absolute(source, destination) != OK) {
			return result_dictionary(false, single_error(
					String("could not copy custom vehicle texture '") + texture.draft_name + "' into the package"), {});
		}
		packaged_textures[i] = true;
	}
	const String source_boost_wav = draft_root.path_join("manual_boost_sfx.wav");
	const String source_boost_ogg = draft_root.path_join("manual_boost_sfx.ogg");
	const String packaged_boost_wav = root.path_join("vehicle/manual_boost.wav");
	const String packaged_boost_ogg = root.path_join("vehicle/manual_boost.ogg");
	DirAccess::remove_absolute(packaged_boost_wav);
	DirAccess::remove_absolute(packaged_boost_ogg);
	String packaged_boost_relative;
	if (FileAccess::file_exists(source_boost_wav)) {
		if (DirAccess::copy_absolute(source_boost_wav, packaged_boost_wav) != OK) {
			return result_dictionary(false, single_error("could not copy the custom manual-boost WAV into the package"), {});
		}
		packaged_boost_relative = "vehicle/manual_boost.wav";
	} else if (FileAccess::file_exists(source_boost_ogg)) {
		if (DirAccess::copy_absolute(source_boost_ogg, packaged_boost_ogg) != OK) {
			return result_dictionary(false, single_error("could not copy the custom manual-boost Ogg into the package"), {});
		}
		packaged_boost_relative = "vehicle/manual_boost.ogg";
	}
	regenerate_all_derived_pairs();
	const String packaged_model_path = root.path_join("vehicle/model.glb");
	const String temporary_model_path = root.path_join("vehicle/model.packaging.glb");
	DirAccess::remove_absolute(temporary_model_path);
	if (use_model_path != packaged_model_path) {
		if (DirAccess::copy_absolute(use_model_path, temporary_model_path) != OK ||
				!mxt_file::replace_atomically(temporary_model_path, packaged_model_path)) {
			DirAccess::remove_absolute(temporary_model_path);
			return result_dictionary(false, single_error("could not copy the draft model into the package"), {});
		}
	}
	Dictionary properties_result = save_file(root.path_join("vehicle/properties.mxt_car_props"));
	if (!static_cast<bool>(properties_result.get("valid", false))) return properties_result;
	Dictionary transform;
	transform["translation"] = vector3_array(model_translation);
	transform["rotation_degrees"] = vector3_array(model_rotation_degrees);
	transform["scale"] = vector3_array(model_scale);
	Array thruster_values;
	for (const Thruster &thruster : thrusters) {
		Dictionary value;
		value["position"] = vector3_array(thruster.position);
		value["rotation_degrees"] = vector3_array(thruster.rotation_degrees);
		value["scale"] = thruster.scale;
		thruster_values.push_back(value);
	}
	Dictionary visual;
	visual["format_revision"] = 1;
	visual["model_transform"] = transform;
	Array body_surface_values;
	for (const int32_t surface : body_surfaces) body_surface_values.push_back(surface);
	visual["body_surfaces"] = body_surface_values;
	Dictionary material_inputs;
	material_inputs["albedo_surface"] = albedo_surface;
	material_inputs["normal_surface"] = normal_surface;
	material_inputs["paint_mask_surface"] = paint_mask_surface;
	material_inputs["use_mesh_normals"] = use_mesh_normals;
	visual["material_inputs"] = material_inputs;
	visual["manual_boost_volume_db"] = manual_boost_volume_db;
	visual["thrusters"] = thruster_values;
	if (!write_text_file(root.path_join("vehicle/visual.json"), JSON::stringify(visual, "  ", true, true))) {
		return result_dictionary(false, single_error("could not write vehicle visual metadata"), {});
	}
	if (!write_text_file(root.path_join("vehicle/authoring.json"), JSON::stringify(get_authoring_intent(), "  ", true, true))) {
		return result_dictionary(false, single_error("could not write vehicle authoring metadata"), {});
	}
	if (DirAccess::copy_absolute(global_path(preview_png_path), root.path_join("preview.png")) != OK) {
		return result_dictionary(false, single_error("could not copy vehicle preview PNG"), {});
	}
	Dictionary payload;
	payload["model"] = "vehicle/model.glb";
	payload["properties"] = "vehicle/properties.mxt_car_props";
	payload["visual_metadata"] = "vehicle/visual.json";
	payload["authoring"] = "vehicle/authoring.json";
	if (!packaged_boost_relative.is_empty()) payload["manual_boost_sfx"] = packaged_boost_relative;
	for (size_t i = 0; i < std::size(TEXTURE_PAYLOADS); ++i) {
		if (packaged_textures[i]) payload[TEXTURE_PAYLOADS[i].manifest_member] = TEXTURE_PAYLOADS[i].package_path;
	}
	Dictionary hashes;
	for (const String &path : {String("vehicle/model.glb"), String("vehicle/properties.mxt_car_props"), String("vehicle/visual.json"), String("vehicle/authoring.json"), String("preview.png")}) {
		hashes[path] = FileAccess::get_sha256(root.path_join(path));
	}
	if (!packaged_boost_relative.is_empty()) {
		hashes[packaged_boost_relative] = FileAccess::get_sha256(root.path_join(packaged_boost_relative));
	}
	for (size_t i = 0; i < std::size(TEXTURE_PAYLOADS); ++i) {
		if (packaged_textures[i]) {
			hashes[TEXTURE_PAYLOADS[i].package_path] = FileAccess::get_sha256(root.path_join(TEXTURE_PAYLOADS[i].package_path));
		}
	}
	Dictionary manifest;
	manifest["format_revision"] = static_cast<int64_t>(mxt::content::PACKAGE_FORMAT_REVISION);
	manifest["content_type"] = "vehicle";
	manifest["title"] = title;
	manifest["description"] = description;
	manifest["author_name"] = author_name;
	manifest["payload"] = payload;
	manifest["payload_sha256"] = hashes;
	if (!write_text_file(root.path_join("manifest.json"), JSON::stringify(manifest, "  ", true, true))) {
		return result_dictionary(false, single_error("could not write vehicle package manifest"), {});
	}
	Dictionary result = result_dictionary(true, {}, properties_result.get("warnings", PackedStringArray()));
	result["package_path"] = root;
	if (validate_package) {
		mxt::content::ValidatedPackage package;
		std::vector<String> validation_errors;
		if (!mxt::content::validate_package_directory_internal(root, package, validation_errors)) {
			PackedStringArray errors;
			for (const String &error : validation_errors) errors.push_back(error);
			return result_dictionary(false, errors, {});
		}
		result["package_digest"] = package.package_digest;
		result["gameplay_digest"] = package.gameplay_digest;
	}
	dirty = false;
	return result;
}

