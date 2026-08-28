#include "car/car_draft_store.h"
#include "core/native_file_utils.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <climits>
#include <cmath>
#include <vector>

using namespace godot;

namespace {

static constexpr int64_t DRAFT_FORMAT_REVISION = 1;

static Dictionary result_dictionary(bool valid, const String &error = String()) {
	Dictionary result;
	result["valid"] = valid;
	PackedStringArray errors;
	if (!error.is_empty())
		errors.push_back(error);
	result["errors"] = errors;
	result["warnings"] = PackedStringArray();
	return result;
}

static String global_path(const String &path) {
	return ProjectSettings::get_singleton()
		->globalize_path(path)
		.replace("\\", "/")
		.simplify_path();
}

static String drafts_root() { return global_path("user://vehicle_drafts"); }

static bool valid_draft_id(const String &draft_id) {
	if (draft_id.is_empty() || draft_id.length() > 128)
		return false;
	for (int64_t i = 0; i < draft_id.length(); ++i) {
		const char32_t c = draft_id[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			  c == '_' || c == '-')) {
			return false;
		}
	}
	return true;
}

static String draft_root(const String &draft_id) { return drafts_root().path_join(draft_id); }

static bool remove_directory_tree(const String &path) {
	{
		Ref<DirAccess> directory = DirAccess::open(path);
		if (directory.is_null() || directory->list_dir_begin() != OK)
			return false;
		for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
			if (entry == "." || entry == "..")
				continue;
			const String child = path.path_join(entry);
			const bool child_is_directory = directory->current_is_dir();
			const bool child_is_link = directory->is_link(entry);
			if (child_is_directory && !child_is_link) {
				if (!remove_directory_tree(child)) {
					directory->list_dir_end();
					return false;
				}
			} else if (DirAccess::remove_absolute(child) != OK) {
				directory->list_dir_end();
				return false;
			}
		}
		directory->list_dir_end();
	}
	return DirAccess::remove_absolute(path) == OK;
}

static bool valid_text(
		const String &text,
		int64_t maximum,
		bool allow_empty,
		bool allow_line_breaks = false) {
	if ((!allow_empty && text.is_empty()) || text.length() > maximum)
		return false;
	for (int64_t i = 0; i < text.length(); ++i) {
		const char32_t codepoint = text[i];
		if (allow_line_breaks && codepoint == '\n')
			continue;
		if (codepoint == 0 || codepoint < 0x20 || codepoint == 0x7f)
			return false;
	}
	return true;
}

static bool write_bytes(const String &path, const PackedByteArray &bytes) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null())
		return false;
	file->store_buffer(bytes);
	const bool succeeded = file->get_error() == OK;
	file->close();
	return succeeded;
}

static bool write_text(const String &path, const String &text) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null())
		return false;
	file->store_string(text);
	const bool succeeded = file->get_error() == OK;
	file->close();
	return succeeded;
}

static bool read_dictionary(const String &path, Dictionary &out) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null() || file->get_length() > 1024u * 1024u)
		return false;
	const PackedByteArray bytes = file->get_buffer(file->get_length());
	if (file->get_error() != OK)
		return false;
	const Variant parsed =
		JSON::parse_string(String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size()));
	if (parsed.get_type() != Variant::DICTIONARY)
		return false;
	out = parsed;
	return true;
}

static Array vector3_array(const Vector3 &value) {
	Array output;
	output.push_back(value.x);
	output.push_back(value.y);
	output.push_back(value.z);
	return output;
}

static bool parse_vector3(const Variant &value, double minimum, double maximum, Vector3 &out) {
	if (value.get_type() != Variant::ARRAY)
		return false;
	const Array array = value;
	if (array.size() != 3)
		return false;
	double components[3];
	for (int64_t i = 0; i < 3; ++i) {
		if (array[i].get_type() == Variant::INT) {
			components[i] = static_cast<double>(static_cast<int64_t>(array[i]));
		} else if (array[i].get_type() == Variant::FLOAT) {
			components[i] = static_cast<double>(array[i]);
		} else {
			return false;
		}
		if (!std::isfinite(components[i]) || components[i] < minimum || components[i] > maximum) {
			return false;
		}
	}
	out = Vector3(components[0], components[1], components[2]);
	return true;
}

static bool parse_integer(const Variant &value, int64_t minimum, int64_t maximum, int64_t &out) {
	double number = 0.0;
	if (value.get_type() == Variant::INT) {
		number = static_cast<double>(static_cast<int64_t>(value));
	} else if (value.get_type() == Variant::FLOAT) {
		number = static_cast<double>(value);
	} else {
		return false;
	}
	if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum) {
		return false;
	}
	out = static_cast<int64_t>(number);
	return true;
}

static Dictionary visual_dictionary(const MxtCarAuthoringSession &session) {
	const Dictionary source_transform = session.get_model_transform();
	Dictionary transform;
	transform["translation"] = vector3_array(source_transform.get("translation", Vector3()));
	transform["rotation_degrees"] =
		vector3_array(source_transform.get("rotation_degrees", Vector3()));
	transform["scale"] = vector3_array(source_transform.get("scale", Vector3(1.0, 1.0, 1.0)));

	const Dictionary source_material = session.get_material_setup();
	Dictionary material;
	material["body_surfaces"] = source_material.get("body_surfaces", Array());
	material["albedo_surface"] = source_material.get("albedo_surface", -1);
	material["normal_surface"] = source_material.get("normal_surface", -1);
	material["paint_mask_surface"] = source_material.get("paint_mask_surface", -1);
	material["use_mesh_normals"] = source_material.get("use_mesh_normals", false);

	Array thrusters;
	const Array source_thrusters = session.get_thrusters();
	for (int64_t i = 0; i < source_thrusters.size(); ++i) {
		const Dictionary thruster = source_thrusters[i];
		Dictionary value;
		value["position"] = vector3_array(thruster.get("position", Vector3()));
		value["rotation_degrees"] = vector3_array(thruster.get("rotation_degrees", Vector3()));
		value["scale"] = thruster.get("scale", 1.0);
		thrusters.push_back(value);
	}

	Dictionary visual;
	visual["model_transform"] = transform;
	visual["material_setup"] = material;
	visual["manual_boost_volume_db"] = session.get_manual_boost_volume_db();
	visual["thrusters"] = thrusters;
	return visual;
}

static bool parse_visual_dictionary(const Dictionary &visual, MxtCarAuthoringSession &session,
									const String &model_path) {
	if (!visual.has("model_transform") ||
		visual["model_transform"].get_type() != Variant::DICTIONARY ||
		!visual.has("material_setup") ||
		visual["material_setup"].get_type() != Variant::DICTIONARY || !visual.has("thrusters") ||
		visual["thrusters"].get_type() != Variant::ARRAY) {
		return false;
	}
	const Dictionary transform = visual["model_transform"];
	Vector3 translation;
	Vector3 rotation_degrees;
	Vector3 scale;
	if (!transform.has("translation") || !transform.has("rotation_degrees") ||
		!transform.has("scale") ||
		!parse_vector3(transform["translation"], -1000.0, 1000.0, translation) ||
		!parse_vector3(transform["rotation_degrees"], -3600.0, 3600.0, rotation_degrees) ||
		!parse_vector3(transform["scale"], 0.001, 100.0, scale)) {
		return false;
	}
	Dictionary decoded_transform;
	decoded_transform["translation"] = translation;
	decoded_transform["rotation_degrees"] = rotation_degrees;
	decoded_transform["scale"] = scale;

	const Dictionary material = visual["material_setup"];
	if (!material.has("body_surfaces") || material["body_surfaces"].get_type() != Variant::ARRAY ||
		!material.has("albedo_surface") || !material.has("normal_surface") ||
		!material.has("paint_mask_surface")) {
		return false;
	}
	const Array surfaces = material["body_surfaces"];
	Array decoded_surfaces;
	for (int64_t i = 0; i < surfaces.size(); ++i) {
		int64_t surface = -1;
		if (!parse_integer(surfaces[i], 0, INT32_MAX, surface))
			return false;
		decoded_surfaces.push_back(surface);
	}
	int64_t albedo_surface = -1;
	int64_t normal_surface = -1;
	int64_t paint_mask_surface = -1;
	if (!parse_integer(material["albedo_surface"], -1, INT32_MAX, albedo_surface) ||
			!parse_integer(material["normal_surface"], -1, INT32_MAX, normal_surface) ||
			!parse_integer(material["paint_mask_surface"], -1, INT32_MAX, paint_mask_surface)) {
		return false;
	}
	Dictionary decoded_material;
	decoded_material["body_surfaces"] = decoded_surfaces;
	decoded_material["albedo_surface"] = albedo_surface;
	decoded_material["normal_surface"] = normal_surface;
	decoded_material["paint_mask_surface"] = paint_mask_surface;
	const Variant use_mesh_normals = material.get("use_mesh_normals", false);
	if (use_mesh_normals.get_type() != Variant::BOOL) return false;
	decoded_material["use_mesh_normals"] = use_mesh_normals;

	const Array thrusters = visual["thrusters"];
	if (thrusters.size() > 8)
		return false;
	Array decoded_thrusters;
	for (int64_t i = 0; i < thrusters.size(); ++i) {
		if (thrusters[i].get_type() != Variant::DICTIONARY)
			return false;
		const Dictionary value = thrusters[i];
		if (!value.has("position") || !value.has("rotation_degrees") || !value.has("scale"))
			return false;
		Vector3 position;
		Vector3 thruster_rotation;
		if (!parse_vector3(value["position"], -100.0, 100.0, position) ||
			!parse_vector3(value["rotation_degrees"], -3600.0, 3600.0, thruster_rotation)) {
			return false;
		}
		if (value["scale"].get_type() != Variant::INT &&
			value["scale"].get_type() != Variant::FLOAT) {
			return false;
		}
		const double scale = value["scale"].get_type() == Variant::INT
								 ? static_cast<double>(static_cast<int64_t>(value["scale"]))
								 : static_cast<double>(value["scale"]);
		if (!std::isfinite(scale) || scale < 0.01 || scale > 10.0)
			return false;
		Dictionary decoded;
		decoded["position"] = position;
		decoded["rotation_degrees"] = thruster_rotation;
		decoded["scale"] = scale;
		decoded_thrusters.push_back(decoded);
	}
	// Drafts created before boost-volume authoring shipped have no member here;
	// 0 dB preserves their prior playback exactly.
	const Variant boost_volume_value = visual.get("manual_boost_volume_db", 0.0);
	if (boost_volume_value.get_type() != Variant::INT &&
			boost_volume_value.get_type() != Variant::FLOAT) return false;
	const double boost_volume_db = boost_volume_value.get_type() == Variant::INT
			? static_cast<double>(static_cast<int64_t>(boost_volume_value))
			: static_cast<double>(boost_volume_value);
	if (!std::isfinite(boost_volume_db) || boost_volume_db < -20.0 || boost_volume_db > 20.0) return false;
	return session.load_draft_visual_state(FileAccess::file_exists(model_path) ? model_path
																   : String(),
										   decoded_transform, decoded_material, decoded_thrusters,
										   boost_volume_db);
}

static Dictionary read_manifest(const String &draft_id) {
	Dictionary manifest;
	if (!valid_draft_id(draft_id) ||
		!read_dictionary(draft_root(draft_id).path_join("draft.json"), manifest)) {
		return Dictionary();
	}
	if (static_cast<int64_t>(manifest.get("format_revision", 0)) != DRAFT_FORMAT_REVISION ||
		String(manifest.get("draft_id", "")) != draft_id) {
		return Dictionary();
	}
	return manifest;
}

static Dictionary public_metadata(const String &draft_id, const Dictionary &manifest) {
	Dictionary output;
	output["draft_id"] = draft_id;
	output["title"] = manifest.get("title", "Untitled Machine");
	output["author_name"] = manifest.get("author_name", "Creator");
	output["description"] = manifest.get("description", "");
	output["modified_unix"] = manifest.get("modified_unix", 0);
	output["status"] = manifest.get("status", "invalid");
	output["workshop_published_file_id"] = manifest.get("workshop_published_file_id", 0);
	output["preview_livery"] = manifest.get("preview_livery", Dictionary());
	const String thumbnail = draft_root(draft_id).path_join("thumbnail.png");
	output["thumbnail_path"] = FileAccess::file_exists(thumbnail) ? thumbnail : String();
	output["has_model"] =
		FileAccess::file_exists(draft_root(draft_id).path_join("source/model.glb"));
	return output;
}

} // namespace

void MxtCarDraftStore::_bind_methods() {
	ClassDB::bind_method(D_METHOD("list_drafts"), &MxtCarDraftStore::list_drafts);
	ClassDB::bind_method(D_METHOD("save_draft", "draft_id", "session", "metadata"),
						 &MxtCarDraftStore::save_draft);
	ClassDB::bind_method(D_METHOD("load_draft", "draft_id", "session"),
						 &MxtCarDraftStore::load_draft);
	ClassDB::bind_method(
		D_METHOD("duplicate_draft", "source_draft_id", "new_draft_id", "new_title"),
		&MxtCarDraftStore::duplicate_draft);
	ClassDB::bind_method(D_METHOD("archive_draft", "draft_id"), &MxtCarDraftStore::archive_draft);
	ClassDB::bind_method(D_METHOD("delete_draft", "draft_id"), &MxtCarDraftStore::delete_draft);
}

Array MxtCarDraftStore::list_drafts() const {
	Array result;
	Ref<DirAccess> directory = DirAccess::open(drafts_root());
	if (directory.is_null())
		return result;
	PackedStringArray entries = directory->get_directories();
	entries.sort();
	for (int64_t i = 0; i < entries.size(); ++i) {
		const String draft_id = entries[i];
		const Dictionary manifest = read_manifest(draft_id);
		if (!manifest.is_empty())
			result.push_back(public_metadata(draft_id, manifest));
	}
	return result;
}

Dictionary MxtCarDraftStore::save_draft(const String &draft_id,
										const Ref<MxtCarAuthoringSession> &session,
										const Dictionary &metadata) const {
	if (!valid_draft_id(draft_id) || session.is_null()) {
		return result_dictionary(false, "draft identity or authoring session is invalid");
	}
	const String title = metadata.get("title", "");
	const String author = metadata.get("author_name", "");
	const String description = metadata.get("description", "");
	if (!valid_text(title, 128, false) || !valid_text(author, 64, false) ||
		!valid_text(description, 8000, true, true)) {
		return result_dictionary(false, "draft title, author, or description is invalid");
	}
	Dictionary serialized = session->serialize();
	if (!static_cast<bool>(serialized.get("valid", false)))
		return serialized;

	const String root = draft_root(draft_id);
	if (DirAccess::make_dir_recursive_absolute(root.path_join("source")) != OK) {
		return result_dictionary(false, "could not create the vehicle draft directory");
	}
	const String canonical_model = root.path_join("source/model.glb");
	const String current_model_path = session->get_model_path();
	if (!current_model_path.is_empty()) {
		if (!FileAccess::file_exists(current_model_path)) {
			return result_dictionary(false, "the draft's imported model is missing");
		}
		if (current_model_path.replace("\\", "/").simplify_path() != canonical_model) {
			const String temporary_model = root.path_join("source/model.glb.tmp");
			DirAccess::remove_absolute(temporary_model);
			if (DirAccess::copy_absolute(current_model_path, temporary_model) != OK ||
				!mxt_file::replace_atomically(temporary_model, canonical_model)) {
				DirAccess::remove_absolute(temporary_model);
				return result_dictionary(false, "could not atomically store the draft model");
			}
		}
		Dictionary transform = session->get_model_transform();
		Dictionary material = session->get_material_setup();
		Array thrusters = session->get_thrusters();
		if (!session->load_draft_visual_state(
				canonical_model, transform, material, thrusters,
				session->get_manual_boost_volume_db())) {
			return result_dictionary(false, "could not adopt the stored draft model");
		}
	}

	uint64_t generation = static_cast<uint64_t>(
		Time::get_singleton()->get_unix_time_from_system() * 1000000.0);
	String properties_name;
	do {
		properties_name = "properties-" + String::num_uint64(generation++) + ".mxt_car_props";
	} while (FileAccess::file_exists(root.path_join(properties_name)));
	const String properties_path = root.path_join(properties_name);
	if (!write_bytes(properties_path, serialized["bytes"])) {
		DirAccess::remove_absolute(properties_path);
		return result_dictionary(false, "could not write the draft property snapshot");
	}

	const Dictionary previous = read_manifest(draft_id);
	Dictionary manifest;
	manifest["format_revision"] = DRAFT_FORMAT_REVISION;
	manifest["draft_id"] = draft_id;
	manifest["title"] = title;
	manifest["author_name"] = author;
	manifest["description"] = description;
	manifest["modified_unix"] =
		static_cast<int64_t>(Time::get_singleton()->get_unix_time_from_system());
	manifest["status"] = session->get_model_path().is_empty() ? "needs_model" : "ready";
	manifest["properties_file"] = properties_name;
	manifest["model_file"] =
		session->get_model_path().is_empty() ? String() : String("source/model.glb");
	manifest["visual"] = visual_dictionary(*session.ptr());
	manifest["workshop_published_file_id"] = metadata.get("workshop_published_file_id", 0);
	manifest["authoring_intent"] = session->get_authoring_intent();
	manifest["preview_livery"] = metadata.get("preview_livery", Dictionary());

	const String temporary_manifest = root.path_join("draft.json.tmp");
	const String manifest_path = root.path_join("draft.json");
	DirAccess::remove_absolute(temporary_manifest);
	if (!write_text(temporary_manifest, JSON::stringify(manifest, "  ", true, true)) ||
		!mxt_file::replace_atomically(temporary_manifest, manifest_path)) {
		DirAccess::remove_absolute(temporary_manifest);
		DirAccess::remove_absolute(properties_path);
		return result_dictionary(false, "could not atomically commit the draft snapshot");
	}
	if (!previous.is_empty()) {
		const String previous_name = previous.get("properties_file", "");
		if (!previous_name.is_empty() && previous_name != properties_name &&
			previous_name.get_file() == previous_name &&
			previous_name.ends_with(".mxt_car_props")) {
			DirAccess::remove_absolute(root.path_join(previous_name));
		}
	}
	session->clear_dirty();
	Dictionary result = result_dictionary(true);
	result.merge(public_metadata(draft_id, manifest), true);
	result["properties_path"] = properties_path;
	return result;
}

Dictionary MxtCarDraftStore::load_draft(const String &draft_id,
										const Ref<MxtCarAuthoringSession> &session) const {
	if (!valid_draft_id(draft_id) || session.is_null()) {
		return result_dictionary(false, "draft identity or authoring session is invalid");
	}
	const Dictionary manifest = read_manifest(draft_id);
	if (manifest.is_empty())
		return result_dictionary(false, "draft metadata is missing or invalid");
	const String properties_name = manifest.get("properties_file", "");
	if (properties_name.is_empty() || properties_name.get_file() != properties_name ||
		!properties_name.ends_with(".mxt_car_props")) {
		return result_dictionary(false, "draft property snapshot path is invalid");
	}
	Ref<FileAccess> file =
		FileAccess::open(draft_root(draft_id).path_join(properties_name), FileAccess::READ);
	if (file.is_null() || file->get_length() > 16u * 1024u * 1024u) {
		return result_dictionary(false, "draft property snapshot is missing or too large");
	}
	const PackedByteArray bytes = file->get_buffer(file->get_length());
	Ref<MxtCarAuthoringSession> parsed;
	parsed.instantiate();
	Dictionary parsed_properties = parsed->load_bytes(bytes);
	if (!static_cast<bool>(parsed_properties.get("valid", false)))
		return parsed_properties;
	if (!manifest.has("visual") || manifest["visual"].get_type() != Variant::DICTIONARY ||
		!parse_visual_dictionary(manifest["visual"], *parsed.ptr(),
								 draft_root(draft_id).path_join("source/model.glb"))) {
		return result_dictionary(false, "draft visual metadata is invalid");
	}
	Dictionary loaded_properties = session->load_bytes(bytes);
	if (!static_cast<bool>(loaded_properties.get("valid", false))) {
		return result_dictionary(false, "draft state could not be applied to the authoring session");
	}
	Dictionary intent = session->get_authoring_intent();
	if (manifest.has("authoring_intent")) {
		String intent_error;
		const uint16_t source_stat_count = static_cast<uint16_t>(
				static_cast<int64_t>(loaded_properties.get("source_schema_stat_count", 0)));
		if (!MxtCarAuthoringSession::normalize_authoring_intent(
				manifest["authoring_intent"], source_stat_count, intent, intent_error)) {
			return result_dictionary(false, intent_error);
		}
	}
	Dictionary intent_result = session->set_authoring_intent(intent);
	if (!static_cast<bool>(intent_result.get("valid", false))) {
		return result_dictionary(false, "draft authoring intent is invalid");
	}
	if (!parse_visual_dictionary(manifest["visual"], *session.ptr(),
								 draft_root(draft_id).path_join("source/model.glb"))) {
		return result_dictionary(false,
								 "draft state could not be applied to the authoring session");
	}
	session->clear_dirty();

	Dictionary result = result_dictionary(true);
	result.merge(public_metadata(draft_id, manifest), true);
	result["properties_path"] = draft_root(draft_id).path_join(properties_name);
	result["authoring_intent"] = session->get_authoring_intent();
	return result;
}

Dictionary MxtCarDraftStore::duplicate_draft(const String &source_draft_id,
											 const String &new_draft_id,
											 const String &new_title) const {
	if (!valid_draft_id(source_draft_id) || !valid_draft_id(new_draft_id) ||
		DirAccess::dir_exists_absolute(draft_root(new_draft_id))) {
		return result_dictionary(false, "source or destination draft identity is invalid");
	}
	Ref<MxtCarAuthoringSession> session;
	session.instantiate();
	Dictionary loaded = load_draft(source_draft_id, session);
	if (!static_cast<bool>(loaded.get("valid", false)))
		return loaded;
	Dictionary metadata;
	metadata["title"] = new_title;
	metadata["author_name"] = loaded.get("author_name", "Creator");
	metadata["description"] = loaded.get("description", "");
	metadata["workshop_published_file_id"] = 0;
	metadata["authoring_intent"] = loaded.get("authoring_intent", Dictionary());
	metadata["preview_livery"] = loaded.get("preview_livery", Dictionary());
	Dictionary saved = save_draft(new_draft_id, session, metadata);
	if (!static_cast<bool>(saved.get("valid", false)))
		return saved;
	const String source_thumbnail = draft_root(source_draft_id).path_join("thumbnail.png");
	const String destination_thumbnail = draft_root(new_draft_id).path_join("thumbnail.png");
	if (FileAccess::file_exists(source_thumbnail)) {
		DirAccess::copy_absolute(source_thumbnail, destination_thumbnail);
	}
	// Auxiliary authoring files live beside draft.json rather than in the property snapshot.
	// Copy them explicitly so duplicating a draft never silently drops its textures or sound.
	for (const String &name : {
			String("albedo.png"), String("normal.png"), String("paint_mask.png"),
			String("manual_boost_sfx.wav"), String("manual_boost_sfx.ogg")}) {
		const String source = draft_root(source_draft_id).path_join(name);
		if (FileAccess::file_exists(source)) {
			DirAccess::copy_absolute(source, draft_root(new_draft_id).path_join(name));
		}
	}
	return saved;
}

Dictionary MxtCarDraftStore::archive_draft(const String &draft_id) const {
	if (!valid_draft_id(draft_id) || read_manifest(draft_id).is_empty()) {
		return result_dictionary(false, "draft identity is invalid or the draft does not exist");
	}
	const String archive_root = global_path("user://vehicle_drafts_archive");
	if (DirAccess::make_dir_recursive_absolute(archive_root) != OK) {
		return result_dictionary(false, "could not create the vehicle draft archive");
	}
	const String destination =
		archive_root.path_join(draft_id + String("-") +
							   String::num_int64(static_cast<int64_t>(
								   Time::get_singleton()->get_unix_time_from_system())));
	if (DirAccess::rename_absolute(draft_root(draft_id), destination) != OK) {
		return result_dictionary(false, "could not move the draft into the local archive");
	}
	Dictionary result = result_dictionary(true);
	result["archive_path"] = destination;
	return result;
}

Dictionary MxtCarDraftStore::delete_draft(const String &draft_id) const {
	if (!valid_draft_id(draft_id) || read_manifest(draft_id).is_empty()) {
		return result_dictionary(false, "draft identity is invalid or the draft does not exist");
	}
	if (!remove_directory_tree(draft_root(draft_id))) {
		return result_dictionary(false, "could not permanently delete the vehicle draft");
	}
	return result_dictionary(true);
}
