#include "car/car_authoring_session.h"
#include "car/car_property_derivation.h"

#include "content/content_manifest.h"
#include "content/content_validator.h"
#include "content/glb_validator.h"
#include "core/native_file_utils.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

using namespace godot;

namespace {
static constexpr const char *LAYER_NAMES[CAR_CURVE_LAYER_COUNT] = {
    "base", "mts", "quickturn", "no_boost", "manual_boost", "dashplate_boost", "stacked_boost"
};
} // namespace
bool MxtCarAuthoringSession::is_safe_draft_root(const String &path, String &out_global_path)
{
	const String allowed_root = global_path("user://vehicle_drafts");
	out_global_path = global_path(path);
	return !out_global_path.is_empty() && out_global_path.begins_with(allowed_root + String("/"));
}

Dictionary MxtCarAuthoringSession::import_model(const String &source_path, const String &draft_root)
{
	String root;
	if (!is_safe_draft_root(draft_root, root)) {
		return result_dictionary(false, single_error("vehicle draft root must be below user://vehicle_drafts"), {});
	}
	const String source = global_path(source_path);
	const String extension = source.get_extension().to_lower();
	if (extension != "glb" && extension != "gltf") {
		return result_dictionary(false, single_error("vehicle model source must be .glb or .gltf"), {});
	}
	if (DirAccess::make_dir_recursive_absolute(root.path_join("source")) != OK) {
		return result_dictionary(false, single_error("could not create vehicle draft directory"), {});
	}
	const String destination = root.path_join("source/model.glb");
	const String temporary = root.path_join("source/model.importing.glb");
	DirAccess::remove_absolute(temporary);
	std::vector<String> validation_errors;
	mxt::content::VehicleGlbInfo model_info;
	if (extension == "glb") {
		if (!mxt::content::validate_glb_file(
				source, mxt::content::ContentType::VEHICLE, validation_errors, &model_info)) {
			PackedStringArray errors;
			for (const String &error : validation_errors) errors.push_back(error);
			return result_dictionary(false, errors, {});
		}
		if (DirAccess::copy_absolute(source, temporary) != OK) {
			return result_dictionary(false, single_error("could not copy vehicle GLB into the draft"), {});
		}
	} else {
		String source_error;
		if (!preflight_gltf_source(source, source_error)) {
			return result_dictionary(false, single_error(source_error), {});
		}
		Ref<GLTFDocument> document;
		document.instantiate();
		Ref<GLTFState> state;
		state.instantiate();
		state->set_base_path(source.get_base_dir());
		const Error load_error = document->append_from_file(source, state, 0, source.get_base_dir());
		if (load_error != OK || document->write_to_filesystem(state, temporary) != OK) {
			DirAccess::remove_absolute(temporary);
			return result_dictionary(false, single_error("could not normalize glTF source into a self-contained GLB"), {});
		}
		if (!mxt::content::validate_glb_file(
				temporary, mxt::content::ContentType::VEHICLE, validation_errors, &model_info)) {
			DirAccess::remove_absolute(temporary);
			PackedStringArray errors;
			for (const String &error : validation_errors) errors.push_back(error);
			return result_dictionary(false, errors, {});
		}
	}
	if (!mxt_file::replace_atomically(temporary, destination)) {
		DirAccess::remove_absolute(temporary);
		return result_dictionary(false, single_error("could not install normalized vehicle GLB into the draft"), {});
	}
	model_path = destination;
	body_surfaces.clear();
	body_surfaces.reserve(model_info.surfaces.size());
	albedo_surface = -1;
	normal_surface = -1;
	paint_mask_surface = -1;
	use_mesh_normals = false;
	for (size_t i = 0; i < model_info.surfaces.size(); ++i) {
		body_surfaces.push_back(static_cast<int32_t>(i));
		if (albedo_surface < 0 && model_info.surfaces[i].has_albedo_texture) albedo_surface = static_cast<int32_t>(i);
		if (normal_surface < 0 && model_info.surfaces[i].has_normal_texture) normal_surface = static_cast<int32_t>(i);
		if (paint_mask_surface < 0 && model_info.surfaces[i].has_paint_mask_texture) paint_mask_surface = static_cast<int32_t>(i);
	}
	dirty = true;
	return result_dictionary(true, {}, {});
}

String MxtCarAuthoringSession::get_model_path() const { return model_path; }

bool MxtCarAuthoringSession::load_draft_visual_state(
		const String &draft_model_path,
		const Dictionary &model_transform,
		const Dictionary &material_setup,
		const Array &draft_thrusters,
		double draft_manual_boost_volume_db)
{
	Ref<MxtCarAuthoringSession> parsed;
	parsed.instantiate();
	parsed->model_path = draft_model_path;
	if (!parsed->set_model_transform(model_transform) ||
			!parsed->set_thrusters(draft_thrusters) ||
			!parsed->set_manual_boost_volume_db(draft_manual_boost_volume_db)) return false;
	if (draft_model_path.is_empty()) {
		const Array surfaces = material_setup.get("body_surfaces", Array());
		if (!surfaces.is_empty() || static_cast<int64_t>(material_setup.get("albedo_surface", -1)) != -1 ||
				static_cast<int64_t>(material_setup.get("normal_surface", -1)) != -1 ||
				static_cast<int64_t>(material_setup.get("paint_mask_surface", -1)) != -1 ||
				static_cast<bool>(material_setup.get("use_mesh_normals", false))) {
			return false;
		}
	} else if (!parsed->set_material_setup(material_setup)) {
		return false;
	}
	model_path = parsed->model_path;
	model_translation = parsed->model_translation;
	model_rotation_degrees = parsed->model_rotation_degrees;
	model_scale = parsed->model_scale;
	body_surfaces = std::move(parsed->body_surfaces);
	albedo_surface = parsed->albedo_surface;
	normal_surface = parsed->normal_surface;
	paint_mask_surface = parsed->paint_mask_surface;
	use_mesh_normals = parsed->use_mesh_normals;
	manual_boost_volume_db = parsed->manual_boost_volume_db;
	thrusters = std::move(parsed->thrusters);
	dirty = true;
	return true;
}

Dictionary MxtCarAuthoringSession::get_model_transform() const
{
	Dictionary result;
	result["translation"] = model_translation;
	result["rotation_degrees"] = model_rotation_degrees;
	result["scale"] = model_scale;
	return result;
}

bool MxtCarAuthoringSession::set_model_transform(const Dictionary &value)
{
	Vector3 translation;
	Vector3 rotation;
	Vector3 scale;
	if (!dictionary_vector3(value, "translation", -1000.0, 1000.0, translation) ||
			!dictionary_vector3(value, "rotation_degrees", -3600.0, 3600.0, rotation) ||
			!dictionary_vector3(value, "scale", 0.001, 100.0, scale)) return false;
	push_undo_snapshot();
	model_translation = translation;
	model_rotation_degrees = rotation;
	model_scale = scale;
	clear_redo_after_mutation();
	dirty = true;
	return true;
}

Array MxtCarAuthoringSession::get_model_surfaces() const
{
	Array result;
	if (model_path.is_empty()) return result;
	mxt::content::VehicleGlbInfo model_info;
	std::vector<String> errors;
	if (!mxt::content::validate_glb_file(
			model_path, mxt::content::ContentType::VEHICLE, errors, &model_info)) return result;
	for (size_t i = 0; i < model_info.surfaces.size(); ++i) {
		const mxt::content::VehicleGlbSurface &surface = model_info.surfaces[i];
		Dictionary value;
		value["index"] = static_cast<int64_t>(i);
		value["name"] = surface.name;
		value["has_albedo_texture"] = surface.has_albedo_texture;
		value["has_normal_texture"] = surface.has_normal_texture;
		value["has_paint_mask_texture"] = surface.has_paint_mask_texture;
		result.push_back(value);
	}
	return result;
}

Dictionary MxtCarAuthoringSession::get_model_resource_usage() const
{
	Dictionary result;
	result["valid"] = false;
	result["file_bytes"] = 0;
	result["file_byte_limit"] = static_cast<int64_t>(mxt::content::VEHICLE_MODEL_MAX_BYTES);
	result["vertices"] = 0;
	result["vertex_limit"] = static_cast<int64_t>(mxt::content::VEHICLE_MODEL_MAX_VERTICES);
	result["triangles"] = 0;
	result["triangle_limit"] = static_cast<int64_t>(mxt::content::VEHICLE_MODEL_MAX_TRIANGLES);
	result["images"] = 0;
	result["image_limit"] = static_cast<int64_t>(mxt::content::VEHICLE_MODEL_MAX_IMAGES);
	result["texture_pixels"] = 0;
	result["texture_pixel_limit"] = static_cast<int64_t>(mxt::content::VEHICLE_MODEL_MAX_TEXTURE_PIXELS);
	if (model_path.is_empty()) return result;
	mxt::content::VehicleGlbInfo model_info;
	std::vector<String> errors;
	if (!mxt::content::validate_glb_file(
			model_path, mxt::content::ContentType::VEHICLE, errors, &model_info)) return result;
	result["valid"] = true;
	result["file_bytes"] = static_cast<int64_t>(model_info.file_bytes);
	result["vertices"] = static_cast<int64_t>(model_info.vertices);
	result["triangles"] = static_cast<int64_t>(model_info.triangles);
	result["images"] = static_cast<int64_t>(model_info.images);
	result["texture_pixels"] = static_cast<int64_t>(model_info.texture_pixels);
	return result;
}

Dictionary MxtCarAuthoringSession::get_material_setup() const
{
	Dictionary result;
	Array surfaces;
	for (const int32_t surface : body_surfaces) surfaces.push_back(surface);
	result["body_surfaces"] = surfaces;
	result["albedo_surface"] = albedo_surface;
	result["normal_surface"] = normal_surface;
	result["paint_mask_surface"] = paint_mask_surface;
	result["use_mesh_normals"] = use_mesh_normals;
	return result;
}

bool MxtCarAuthoringSession::set_manual_boost_volume_db(double value)
{
	if (!std::isfinite(value) || value < -20.0 || value > 20.0) return false;
	const float replacement = static_cast<float>(value);
	if (replacement == manual_boost_volume_db) return true;
	push_undo_snapshot();
	manual_boost_volume_db = replacement;
	clear_redo_after_mutation();
	dirty = true;
	return true;
}

bool MxtCarAuthoringSession::set_material_setup(const Dictionary &value)
{
	if (!value.has("body_surfaces") || value["body_surfaces"].get_type() != Variant::ARRAY ||
			!value.has("albedo_surface") || value["albedo_surface"].get_type() != Variant::INT ||
			!value.has("normal_surface") || value["normal_surface"].get_type() != Variant::INT ||
			!value.has("paint_mask_surface") || value["paint_mask_surface"].get_type() != Variant::INT) return false;
	mxt::content::VehicleGlbInfo model_info;
	std::vector<String> errors;
	if (!mxt::content::validate_glb_file(
			model_path, mxt::content::ContentType::VEHICLE, errors, &model_info)) return false;
	const Array input_surfaces = value["body_surfaces"];
	if (input_surfaces.is_empty() || input_surfaces.size() > static_cast<int64_t>(model_info.surfaces.size())) return false;
	std::vector<uint8_t> selected(model_info.surfaces.size(), 0);
	std::vector<int32_t> replacement;
	replacement.reserve(input_surfaces.size());
	for (int64_t i = 0; i < input_surfaces.size(); ++i) {
		if (input_surfaces[i].get_type() != Variant::INT) return false;
		const int64_t surface = static_cast<int64_t>(input_surfaces[i]);
		if (surface < 0 || surface >= static_cast<int64_t>(model_info.surfaces.size()) ||
				selected[static_cast<size_t>(surface)] != 0) return false;
		selected[static_cast<size_t>(surface)] = 1;
		replacement.push_back(static_cast<int32_t>(surface));
	}
	auto valid_input = [&](int64_t surface, bool mxt::content::VehicleGlbSurface::*texture_member) {
		return surface == -1 || (surface >= 0 && surface < static_cast<int64_t>(model_info.surfaces.size()) &&
				(model_info.surfaces[static_cast<size_t>(surface)].*texture_member));
	};
	const int64_t new_albedo = static_cast<int64_t>(value["albedo_surface"]);
	const int64_t new_normal = static_cast<int64_t>(value["normal_surface"]);
	const int64_t new_paint_mask = static_cast<int64_t>(value["paint_mask_surface"]);
	const Variant mesh_normals_value = value.get("use_mesh_normals", false);
	if (mesh_normals_value.get_type() != Variant::BOOL) return false;
	const bool new_use_mesh_normals = static_cast<bool>(mesh_normals_value);
	if (!valid_input(new_albedo, &mxt::content::VehicleGlbSurface::has_albedo_texture) ||
			!valid_input(new_normal, &mxt::content::VehicleGlbSurface::has_normal_texture) ||
			!valid_input(new_paint_mask, &mxt::content::VehicleGlbSurface::has_paint_mask_texture)) return false;
	push_undo_snapshot();
	body_surfaces = std::move(replacement);
	albedo_surface = static_cast<int32_t>(new_albedo);
	normal_surface = static_cast<int32_t>(new_normal);
	paint_mask_surface = static_cast<int32_t>(new_paint_mask);
	use_mesh_normals = new_use_mesh_normals;
	clear_redo_after_mutation();
	dirty = true;
	return true;
}

Array MxtCarAuthoringSession::get_thrusters() const
{
	Array result;
	for (const Thruster &thruster : thrusters) {
		Dictionary value;
		value["position"] = thruster.position;
		value["rotation_degrees"] = thruster.rotation_degrees;
		value["scale"] = thruster.scale;
		result.push_back(value);
	}
	return result;
}

bool MxtCarAuthoringSession::set_thrusters(const Array &value)
{
	if (value.size() > 8) return false;
	std::vector<Thruster> replacement;
	replacement.reserve(value.size());
	for (int64_t i = 0; i < value.size(); ++i) {
		if (value[i].get_type() != Variant::DICTIONARY) return false;
		const Dictionary input = value[i];
		Thruster thruster;
		if (!dictionary_vector3(input, "position", -100.0, 100.0, thruster.position) ||
				!dictionary_vector3(input, "rotation_degrees", -3600.0, 3600.0, thruster.rotation_degrees) ||
				!input.has("scale") ||
				(input["scale"].get_type() != Variant::INT && input["scale"].get_type() != Variant::FLOAT)) return false;
		const double scale_value = input["scale"].get_type() == Variant::INT
				? static_cast<double>(static_cast<int64_t>(input["scale"]))
				: static_cast<double>(input["scale"]);
		if (!std::isfinite(scale_value) || scale_value < 0.01 || scale_value > 10.0) return false;
		thruster.scale = static_cast<float>(scale_value);
		replacement.push_back(thruster);
	}
	push_undo_snapshot();
	thrusters = std::move(replacement);
	clear_redo_after_mutation();
	dirty = true;
	return true;
}

Dictionary MxtCarAuthoringSession::sample_effective_stats(
		double machine_setting,
		const String &technique,
		double technique_intensity,
		const String &boost_state) const
{
	const double setting = std::clamp(machine_setting, 0.0, 1.0);
	const double intensity = std::clamp(technique_intensity, 0.0, 1.0);
	const int32_t technique_layer = technique == "mts"
			? CAR_CURVE_MTS
			: (technique == "quickturn" ? CAR_CURVE_QUICKTURN : -1);
	int32_t boost_layer = -1;
	if (boost_state == "none") boost_layer = CAR_CURVE_NO_BOOST;
	else if (boost_state == "manual") boost_layer = CAR_CURVE_MANUAL_BOOST;
	else if (boost_state == "dashplate" || boost_state == "s_boost_dashplate") boost_layer = CAR_CURVE_DASHPLATE_BOOST;
	else if (boost_state == "stacked") boost_layer = CAR_CURVE_STACKED_BOOST;
	const bool s_boost = boost_state == "s_boost" || boost_state == "s_boost_dashplate";
	Dictionary output;
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		const String name = PhysicsCarProperties::stat_name(static_cast<CarStatId>(stat));
		const bool live = PhysicsCarProperties::stat_supports_live_modifiers(static_cast<CarStatId>(stat));
		double value = s_boost && live ? s_boost_values[stat] : sample_curve("base", name, setting);
		if (live && technique_layer >= 0) {
			const double authored = sample_curve(LAYER_NAMES[technique_layer], name, setting);
			value *= 1.0 + (authored - 1.0) * intensity;
		}
		if (live && boost_layer >= 0) value *= sample_curve(LAYER_NAMES[boost_layer], name, setting);
		output[name] = value;
	}
	return output;
}

Dictionary MxtCarAuthoringSession::simulate_speed_preview(
		double machine_setting,
		double starting_speed_kmh,
		bool frame_perfect_boosting,
		const String &technique,
		double technique_intensity,
		const String &boost_state) const
{
	Dictionary no_boost_stats = sample_effective_stats(
			machine_setting, technique, technique_intensity,
			frame_perfect_boosting ? String("none") : boost_state);
	Dictionary manual_boost_stats = frame_perfect_boosting
			? sample_effective_stats(machine_setting, technique, technique_intensity, "manual")
			: no_boost_stats;
	const bool s_boost_active = !frame_perfect_boosting &&
			(boost_state == "s_boost" || boost_state == "s_boost_dashplate");
	const double tick_rate = 60.0;
	const double weight = no_boost_stats["weight_kg"];
	Dictionary result;
	if (!std::isfinite(weight) || std::abs(weight) < 0.0001 || !std::isfinite(starting_speed_kmh)) {
		result["error"] = "speed preview requires finite nonzero weight and starting speed";
		return result;
	}
	double speed = starting_speed_kmh * weight / 216.0;
	double base_speed = speed / weight;
	double turbo = 0.0;
	int32_t manual_frames = 0;
	int32_t boost_count = 0;
	PackedFloat32Array times;
	PackedFloat32Array speeds;
	PackedFloat32Array turbos;
	times.resize(1800);
	speeds.resize(1800);
	turbos.resize(1800);
	double peak_speed = 0.0;
	double peak_turbo = 0.0;
	for (int32_t frame = 0; frame < 1800; ++frame) {
		bool started_manual_boost = false;
		if (frame_perfect_boosting && manual_frames == 0) {
			const double duration = std::max(static_cast<double>(manual_boost_stats["manual_boost_duration_seconds"]), 0.0);
			manual_frames = std::max(static_cast<int32_t>(duration * tick_rate + 0.5), 0);
			if (manual_frames > 0) {
				turbo += static_cast<double>(manual_boost_stats["manual_turbo_gain"]);
				++boost_count;
				started_manual_boost = true;
			}
		}
		const bool manual_active = manual_frames > 0;
		const Dictionary stats = manual_active ? manual_boost_stats : no_boost_stats;
		turbo -= (static_cast<double>(stats["turbo_flat_loss_per_second"]) +
				turbo * static_cast<double>(stats["turbo_percent_loss_per_second"])) / tick_rate;
		turbo = std::max(turbo, 0.0);
		double target_speed = 40.0 * static_cast<double>(stats["acceleration"]) / 348.0;
		target_speed *= static_cast<double>(stats["drive_target_speed_multiplier"]);
		target_speed += base_speed;
		const double normalized_speed = speed / weight;
		double speed_difference = target_speed - normalized_speed;
		const double denominator = 36.0 + 40.0 * static_cast<double>(stats["max_speed"]) +
				turbo * static_cast<double>(stats["turbo_top_speed_effect"]);
		double speed_factor = std::abs(denominator) > 0.0001 ? std::max(target_speed / denominator, 0.0) : 0.0;
		double accel_magnitude = speed_factor * 4.0 * static_cast<double>(stats["acceleration"]) *
				(0.6 + static_cast<double>(stats["acceleration"])) *
				static_cast<double>(stats["acceleration_response_multiplier"]);
		if (started_manual_boost) accel_magnitude = 0.0;
		double new_base_speed = target_speed - speed_difference * accel_magnitude;
		const double released_target_speed = base_speed;
		const double released_difference = released_target_speed - normalized_speed;
		const double released_factor = std::abs(denominator) > 0.0001 ? std::max(released_target_speed / denominator, 0.0) : 0.0;
		double released_magnitude = released_factor * 4.0 * static_cast<double>(stats["acceleration"]) *
				(0.6 + static_cast<double>(stats["acceleration"])) *
				static_cast<double>(stats["acceleration_response_multiplier"]);
		if (started_manual_boost) released_magnitude = 0.0;
		const double released_new_base_speed = released_target_speed - released_difference * released_magnitude * 0.05;
		if (released_new_base_speed > new_base_speed) {
			target_speed = released_target_speed;
			speed_difference = released_difference;
			new_base_speed = released_new_base_speed;
		}
		base_speed = std::max(new_base_speed - static_cast<double>(stats["drag"]), 0.0);
		if (s_boost_active) base_speed += static_cast<double>(stats["s_boost_base_speed_add_per_second"]) / tick_rate;
		double thrust = 1000.0 * static_cast<double>(stats["forward_thrust_multiplier"]) * speed_difference;
		if (normalized_speed < 0.0 || speed_difference < 0.0) thrust *= 0.15;
		speed += thrust;
		const double speed_weight_ratio = speed / weight;
		double speed_kmh = 216.0 * speed_weight_ratio;
		if (speed_kmh < 2.0) {
			speed = 0.0;
			speed_kmh = 0.0;
		} else {
			speed -= speed_weight_ratio * speed_weight_ratio * 8.0;
			speed_kmh = 216.0 * speed / weight;
		}
		if (!std::isfinite(speed_kmh) || !std::isfinite(turbo)) {
			result["error"] = "speed preview diverged to a non-finite value";
			return result;
		}
		times.set(frame, static_cast<float>(frame / tick_rate));
		speeds.set(frame, static_cast<float>(speed_kmh));
		turbos.set(frame, static_cast<float>(turbo));
		peak_speed = std::max(peak_speed, speed_kmh);
		peak_turbo = std::max(peak_turbo, turbo);
		if (manual_active) --manual_frames;
	}
	const double terminal_speed = speeds[speeds.size() - 1];
	const double tolerance = std::max(std::abs(terminal_speed) * 0.001, 0.01);
	int64_t settle_index = speeds.size() - 1;
	for (int64_t i = speeds.size() - 1; i >= 0; --i) {
		if (std::abs(speeds[i] - terminal_speed) > tolerance) {
			settle_index = std::min<int64_t>(i + 1, speeds.size() - 1);
			break;
		}
		settle_index = i;
	}
	result["times"] = times;
	result["speeds_kmh"] = speeds;
	result["turbos"] = turbos;
	result["terminal_speed_kmh"] = terminal_speed;
	result["peak_speed_kmh"] = peak_speed;
	result["peak_turbo"] = peak_turbo;
	result["settle_time_seconds"] = static_cast<double>(times[settle_index]);
	result["boost_count"] = boost_count;
	return result;
}
