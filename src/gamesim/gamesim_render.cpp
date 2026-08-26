#include "gamesim/gamesim_cpu_internal.h"
#include "gamesim/gamesim_internal.h"
#include "gamesim/gamesim_render_internal.h"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/input.hpp"
#include "godot_cpp/classes/viewport.hpp"
#include "godot_cpp/classes/world3d.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "core/debug.hpp"
#include "audio/spatial_audio_manager.h"
#include "track/finish_line_display.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace godot;

static constexpr float VEHICLE_OVERLAY_RESPONSE_MULTIPLIER = 4.0f;
static constexpr float VEHICLE_RECHARGE_OVERLAY_GAIN = 0.018f * VEHICLE_OVERLAY_RESPONSE_MULTIPLIER;
static constexpr float VEHICLE_OVERLAY_FADE_WEIGHT = 0.03f * VEHICLE_OVERLAY_RESPONSE_MULTIPLIER;
static constexpr float DASHPLATE_VISUAL_HEAT_TO_INTENSITY = 0.4f;

static godot::Transform3D build_camera_transform(const godot::Vector3& position, const godot::Vector3& interest, const godot::Vector3& up)
	{
		godot::Vector3 backward = position - interest;
		if (backward.length_squared() <= 0.0000001f) {
			return godot::Transform3D(godot::Basis(), position);
		}
		backward.normalize();
		godot::Vector3 right = up.cross(backward);
		if (right.length_squared() <= 0.0000001f) {
			right = godot::Vector3(1.0f, 0.0f, 0.0f);
		} else {
			right.normalize();
		}
		godot::Vector3 corrected_up = backward.cross(right);
		if (corrected_up.length_squared() <= 0.0000001f) {
			corrected_up = godot::Vector3(0.0f, 1.0f, 0.0f);
		} else {
			corrected_up.normalize();
		}
		godot::Basis basis;
		basis.set_column(0, right);
		basis.set_column(1, corrected_up);
		basis.set_column(2, backward);
		return godot::Transform3D(basis, position);
	}

	static float smoothstep01(float alpha)
	{
		alpha = std::max(0.0f, std::min(1.0f, alpha));
		return alpha * alpha * (3.0f - 2.0f * alpha);
	}

	static void populate_visual_car_args(godot::Array& visual_args, const PhysicsCar& car)
	{
		visual_args[0] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, position_current, car.soa_index));
		visual_args[1] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, position_old, car.soa_index));
		visual_args[2] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, track_surface_normal, car.soa_index));
		visual_args[3] = car.soa->height_above_track[car.soa_index];
		visual_args[4] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity, car.soa_index));
		visual_args[5] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, velocity_angular, car.soa_index));
		visual_args[6] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, basis_physical, car.soa_index));
		visual_args[7] = car.soa->base_speed[car.soa_index];
		visual_args[8] = car.soa->boost_turbo[car.soa_index];
		visual_args[9] = car.soa->speed_kmh[car.soa_index];
		visual_args[10] = car.soa->energy[car.soa_index];
		visual_args[11] = car.soa->lap_progress[car.soa_index];
		visual_args[12] = std::max(car.soa->boost_frames_manual[car.soa_index],
			car.soa->boost_frames_dash[car.soa_index]);
		visual_args[13] = car.soa->boost_frames_manual[car.soa_index];
		visual_args[14] = car.soa->lap[car.soa_index];
		visual_args[15] = car.soa->machine_state[car.soa_index];
		visual_args[16] = car.soa->terrain_state[car.soa_index];
		visual_args[17] = car.soa->frames_since_start_2[car.soa_index];
		visual_args[18] = car.soa->tilt_state[car.soa_index * 4];
		visual_args[19] = car.soa->input_strafe[car.soa_index];
		visual_args[20] = car.soa->turn_reaction_input[car.soa_index];
		visual_args[21] = car.soa->g_anim_timer[car.soa_index];
		visual_args[22] = car.soa->state_2[car.soa_index];
		visual_args[23] = gd_vec3(SimVec3(car.soa->tilt_offset_x[car.soa_index * 4], car.soa->tilt_offset_y[car.soa_index * 4], car.soa->tilt_offset_z[car.soa_index * 4]));
		visual_args[24] = gd_vec3(SimVec3(car.soa->tilt_offset_x[car.soa_index * 4 + 2], car.soa->tilt_offset_y[car.soa_index * 4 + 2], car.soa->tilt_offset_z[car.soa_index * 4 + 2]));
		visual_args[25] = car.soa->stat_weight[car.soa_index];
		visual_args[26] = car.soa->stat_strafe[car.soa_index];
		visual_args[27] = car.soa->input_strafe_1_6[car.soa_index];
		visual_args[28] = car.soa->weight_derived_1[car.soa_index];
		visual_args[29] = car.soa->weight_derived_2[car.soa_index];
		visual_args[30] = car.soa->weight_derived_3[car.soa_index];
		visual_args[31] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, visual_rotation, car.soa_index));
		visual_args[32] = car.soa->spinattack_angle[car.soa_index];
		visual_args[33] = car.soa->spinattack_direction[car.soa_index];
		visual_args[34] = car.soa->visual_shake_mult[car.soa_index];
		visual_args[35] = car.soa->input_accel[car.soa_index];
		visual_args[36] = car.soa->restore_state[car.soa_index];
		visual_args[37] = car.soa->restore_move_frames[car.soa_index];
		visual_args[38] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, restore_start_transform, car.soa_index));
		visual_args[39] = gd_transform(MXT_LOAD_TRANSFORM(*car.soa, restore_target_transform, car.soa_index));
		visual_args[40] = static_cast<int>(car.get_s_boost_charge());
		visual_args[41] = static_cast<int>(car.get_s_boost_max_charge());
		visual_args[42] = car.is_s_boost_active();
		visual_args[43] = car.is_s_boost_ready();
		visual_args[44] = car.soa->tilt_state[car.soa_index * 4 + 1];
		visual_args[45] = car.soa->tilt_state[car.soa_index * 4 + 2];
		visual_args[46] = car.soa->tilt_state[car.soa_index * 4 + 3];
		visual_args[47] = car.soa->camera_reorienting[car.soa_index];
		visual_args[48] = car.soa->camera_repositioning[car.soa_index];
		visual_args[49] = gd_vec3(LOAD_INDEXED_VEC3(*car.soa, track_surface_pos, car.soa_index));
		visual_args[50] = car.soa->calced_max_energy[car.soa_index];
		visual_args[51] = static_cast<int>(car.soa->attack_cooldown_frames[car.soa_index]);
		visual_args[52] = car.soa->boost_energy_use_mult[car.soa_index];
		visual_args[53] = car.soa->stat_manual_boost_duration_seconds[car.soa_index];
	}

static inline godot::AABB gd_aabb(const SimAABB& b)
	{
		return godot::AABB(gd_vec3(b.position), gd_vec3(b.size));
	}

static inline void write_multimesh_buffer_instance(
		float* buffer,
		int slot,
		const SimTransform& transform,
		const godot::Color& color,
		const godot::Color& custom_data)
{
	float* const instance = buffer + slot * 20;
	instance[0] = transform.basis.c0.x;
	instance[1] = transform.basis.c1.x;
	instance[2] = transform.basis.c2.x;
	instance[3] = transform.origin.x;
	instance[4] = transform.basis.c0.y;
	instance[5] = transform.basis.c1.y;
	instance[6] = transform.basis.c2.y;
	instance[7] = transform.origin.y;
	instance[8] = transform.basis.c0.z;
	instance[9] = transform.basis.c1.z;
	instance[10] = transform.basis.c2.z;
	instance[11] = transform.origin.z;
	instance[12] = color.r;
	instance[13] = color.g;
	instance[14] = color.b;
	instance[15] = color.a;
	instance[16] = custom_data.r;
	instance[17] = custom_data.g;
	instance[18] = custom_data.b;
	instance[19] = custom_data.a;
}

void GameSim::clear_trigger_visuals()
{
	render_dashplate_visuals.clear();
}

void GameSim::set_trigger_visuals(godot::Array visual_nodes)
{
	clear_trigger_visuals();
	if (!current_track || !current_track->trigger_colliders) {
		return;
	}

	const int visual_count = std::min(
		static_cast<int>(visual_nodes.size()),
		current_track->num_trigger_colliders);
	render_dashplate_visuals.reserve(visual_count);
	for (int i = 0; i < visual_count; ++i) {
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		if (!trigger || trigger->type != TRIGGER_TYPE::DASHPLATE) {
			continue;
		}

		Node* root = Object::cast_to<Node>(visual_nodes[i]);
		if (!root) {
			continue;
		}
		MeshInstance3D* mesh = Object::cast_to<MeshInstance3D>(
			root->get_node_or_null(NodePath("Visual/DashplateMesh")));
		if (!mesh) {
			continue;
		}
		Ref<ShaderMaterial> projection_material = mesh->get_active_material(0);
		if (projection_material.is_null()) {
			continue;
		}

		RenderDashplateVisual visual;
		visual.trigger = static_cast<Dashplate*>(trigger);
		visual.projection_material = projection_material;
		visual.projection_material->set_shader_parameter("booster_intensity", 1.0f);
		visual.projection_material->set_shader_parameter("boost_time", 0.0f);
		render_dashplate_visuals.push_back(visual);
	}
}

void GameSim::update_dashplate_visuals(float process_delta)
{
	if (render_dashplate_visuals.empty()) {
		return;
	}

	const float delta = std::clamp(process_delta, 0.0f, 0.1f);
	const uint32_t current_tick = tick > 0 ? static_cast<uint32_t>(tick) : 0u;
	for (RenderDashplateVisual& visual : render_dashplate_visuals) {
		if (!visual.trigger || visual.projection_material.is_null()) {
			continue;
		}
		const float heat = visual.trigger->effective_heat_at_tick(current_tick);
		const float intensity = 1.0f + heat * DASHPLATE_VISUAL_HEAT_TO_INTENSITY;
		visual.projection_material->set_shader_parameter("booster_intensity", intensity);
		visual.boost_time += delta * (((intensity - 1.0f) * 3.0f) + 1.0f);
		visual.projection_material->set_shader_parameter("boost_time", visual.boost_time);
	}
}

void GameSim::set_car_render_manager(godot::Object* p_car_render_manager)
{
	car_render_manager = p_car_render_manager;
	render_car_multimeshes.clear();
	render_outline_multimeshes.clear();
	render_outline_main_multimeshes.clear();
	render_shadow_multimeshes.clear();
	render_stamp_multimeshes.clear();
	render_thruster_multimeshes.clear();
	render_body_buffers.clear();
	render_thruster_buffers.clear();
	render_thruster_buffer_write_ptrs.clear();
	render_thruster_current_thrust.clear();
	render_car_local_transforms.clear();
	render_outline_local_transforms.clear();
	render_outline_main_local_transforms.clear();
	render_shadow_local_transforms.clear();
	render_stamp_local_transforms.clear();
	render_thruster_local_transforms.clear();
	render_car_archetype_indices.clear();
	render_car_slots.clear();
	render_visible_car_slots.clear();
	render_visible_thruster_slots.clear();
	render_visible_counts.clear();
	render_visible_thruster_counts.clear();
	render_last_body_instances = 0;
	render_last_thruster_instances = 0;
	render_visual_prev_transforms.clear();
	render_visual_current_transforms.clear();
	render_final_prev_transforms.clear();
	render_final_current_transforms.clear();
	render_visual_prev_ground_distances.clear();
	render_visual_current_ground_distances.clear();
	render_visual_initialized.clear();
	render_rollback_corrections.clear();
	render_rollback_correction_active.clear();
	render_rollback_capture_transforms.clear();
	render_rollback_capture_pending = false;
	render_vehicle_visual_state.clear();
	render_vehicle_effect_refs.clear();
	render_effect_full_flags.clear();
	render_effect_pool_slots.clear();
	clear_render_thruster_lights();
	if (!car_render_manager) {
		return;
	}

	godot::Variant bindings_var = car_render_manager->call("get_native_render_bindings");
	if (bindings_var.get_type() != godot::Variant::DICTIONARY) {
		return;
	}
	godot::Dictionary bindings = bindings_var;
	godot::Array multimeshes = bindings.get("multimeshes", godot::Array());
	godot::Array outline_multimeshes = bindings.get("outline_multimeshes", godot::Array());
	godot::Array outline_main_multimeshes = bindings.get("outline_main_multimeshes", godot::Array());
	godot::Array shadow_multimeshes = bindings.get("shadow_multimeshes", godot::Array());
	godot::Array stamp_multimeshes = bindings.get("stamp_multimeshes", godot::Array());
	godot::Array thruster_multimeshes = bindings.get("thruster_multimeshes", godot::Array());
	godot::Array local_transforms = bindings.get("local_transforms", godot::Array());
	godot::Array outline_local_transforms = bindings.get("outline_local_transforms", godot::Array());
	godot::Array outline_main_local_transforms = bindings.get("outline_main_local_transforms", godot::Array());
	godot::Array shadow_local_transforms = bindings.get("shadow_local_transforms", godot::Array());
	godot::Array stamp_local_transforms = bindings.get("stamp_local_transforms", godot::Array());
	godot::Array thruster_local_transforms = bindings.get("thruster_local_transforms", godot::Array());
	godot::PackedInt32Array archetype_indices = bindings.get("archetype_indices", godot::PackedInt32Array());
	godot::PackedInt32Array slots = bindings.get("slots", godot::PackedInt32Array());

	render_car_multimeshes.reserve(multimeshes.size());
	render_outline_multimeshes.reserve(multimeshes.size());
	render_outline_main_multimeshes.reserve(multimeshes.size());
	render_shadow_multimeshes.reserve(shadow_multimeshes.size());
	render_stamp_multimeshes.reserve(stamp_multimeshes.size());
	render_thruster_multimeshes.reserve(thruster_multimeshes.size());
	render_body_buffers.reserve(multimeshes.size());
	render_thruster_buffers.reserve(thruster_multimeshes.size());
	render_thruster_buffer_write_ptrs.reserve(thruster_multimeshes.size());
	render_car_local_transforms.reserve(local_transforms.size());
	render_outline_local_transforms.reserve(local_transforms.size());
	render_outline_main_local_transforms.reserve(local_transforms.size());
	render_shadow_local_transforms.reserve(shadow_local_transforms.size());
	render_stamp_local_transforms.reserve(stamp_local_transforms.size());
	render_thruster_local_transforms.reserve(thruster_local_transforms.size());
	for (int i = 0; i < multimeshes.size(); ++i) {
		godot::Ref<godot::MultiMesh> multimesh = multimeshes[i];
		render_car_multimeshes.push_back(multimesh);
		godot::Ref<godot::MultiMesh> outline_multimesh;
		if (i < outline_multimeshes.size()) {
			outline_multimesh = outline_multimeshes[i];
		}
		render_outline_multimeshes.push_back(outline_multimesh);
		godot::Ref<godot::MultiMesh> outline_main_multimesh;
		if (i < outline_main_multimeshes.size()) {
			outline_main_multimesh = outline_main_multimeshes[i];
		}
		render_outline_main_multimeshes.push_back(outline_main_multimesh);
		godot::Ref<godot::MultiMesh> shadow_multimesh;
		if (i < shadow_multimeshes.size()) {
			shadow_multimesh = shadow_multimeshes[i];
		}
		render_shadow_multimeshes.push_back(shadow_multimesh);
		godot::Ref<godot::MultiMesh> stamp_multimesh;
		if (i < stamp_multimeshes.size()) {
			stamp_multimesh = stamp_multimeshes[i];
		}
		render_stamp_multimeshes.push_back(stamp_multimesh);
		godot::Ref<godot::MultiMesh> thruster_multimesh;
		if (i < thruster_multimeshes.size()) {
			thruster_multimesh = thruster_multimeshes[i];
		}
		render_thruster_multimeshes.push_back(thruster_multimesh);
		RenderBodyMultimeshBuffers body_buffers;
		if (multimesh.is_valid()) {
			body_buffers.main.resize(multimesh->get_instance_count() * 20);
		}
		if (outline_multimesh.is_valid()) {
			body_buffers.outline.resize(outline_multimesh->get_instance_count() * 20);
		}
		if (outline_main_multimesh.is_valid()) {
			body_buffers.outline_main.resize(outline_main_multimesh->get_instance_count() * 20);
		}
		if (shadow_multimesh.is_valid()) {
			body_buffers.shadow.resize(shadow_multimesh->get_instance_count() * 20);
		}
		if (stamp_multimesh.is_valid()) {
			body_buffers.stamp.resize(stamp_multimesh->get_instance_count() * 20);
		}
		render_body_buffers.push_back(std::move(body_buffers));
		godot::PackedFloat32Array thruster_buffer;
		if (thruster_multimesh.is_valid()) {
			thruster_buffer.resize(thruster_multimesh->get_instance_count() * 20);
		}
		render_thruster_buffers.push_back(std::move(thruster_buffer));
		render_thruster_buffer_write_ptrs.push_back(nullptr);
		if (i < local_transforms.size() && local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_car_local_transforms.push_back(sim_transform(local_transforms[i]));
		} else {
			render_car_local_transforms.push_back(SimTransform());
		}
		if (i < outline_local_transforms.size() && outline_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_outline_local_transforms.push_back(sim_transform(outline_local_transforms[i]));
		} else {
			render_outline_local_transforms.push_back(SimTransform());
		}
		if (i < outline_main_local_transforms.size() && outline_main_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_outline_main_local_transforms.push_back(sim_transform(outline_main_local_transforms[i]));
		} else {
			render_outline_main_local_transforms.push_back(SimTransform());
		}
		if (i < shadow_local_transforms.size() && shadow_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_shadow_local_transforms.push_back(sim_transform(shadow_local_transforms[i]));
		} else {
			render_shadow_local_transforms.push_back(SimTransform());
		}
		if (i < stamp_local_transforms.size() && stamp_local_transforms[i].get_type() == godot::Variant::TRANSFORM3D) {
			render_stamp_local_transforms.push_back(sim_transform(stamp_local_transforms[i]));
		} else {
			render_stamp_local_transforms.push_back(SimTransform());
		}
		std::vector<SimTransform> local_thrusters;
		if (i < thruster_local_transforms.size() && thruster_local_transforms[i].get_type() == godot::Variant::ARRAY) {
			godot::Array transforms = thruster_local_transforms[i];
			local_thrusters.reserve(transforms.size());
			for (int t = 0; t < transforms.size(); ++t) {
				if (transforms[t].get_type() == godot::Variant::TRANSFORM3D) {
					local_thrusters.push_back(sim_transform(transforms[t]));
				}
			}
		}
		render_thruster_local_transforms.push_back(std::move(local_thrusters));
	}

	render_car_archetype_indices.resize(archetype_indices.size());
	for (int i = 0; i < archetype_indices.size(); ++i) {
		render_car_archetype_indices[i] = archetype_indices[i];
	}
	render_car_slots.resize(slots.size());
	for (int i = 0; i < slots.size(); ++i) {
		render_car_slots[i] = slots[i];
	}
	render_visible_car_slots.assign(render_car_slots.size(), -1);
	render_visible_thruster_slots.assign(render_car_slots.size(), -1);
	render_visible_counts.assign(render_car_multimeshes.size(), 0);
	render_visible_thruster_counts.assign(render_thruster_multimeshes.size(), 0);
	cache_native_visual_effect_nodes();
}

void GameSim::clear_render_thruster_lights()
{
	RenderingServer* rs = RenderingServer::get_singleton();
	if (rs) {
		for (RenderThrusterLightRID& light : render_thruster_lights) {
			if (light.instance.is_valid()) {
				rs->free_rid(light.instance);
			}
			if (light.light.is_valid()) {
				rs->free_rid(light.light);
			}
		}
	}
	render_thruster_lights.clear();
	render_thruster_light_scenario = RID();
	render_thruster_light_visible_count = 0;
}

void GameSim::ensure_render_thruster_light_capacity(int capacity)
{
	if (capacity <= static_cast<int>(render_thruster_lights.size())) {
		return;
	}
	RenderingServer* rs = RenderingServer::get_singleton();
	if (!rs || !car_node_container) {
		return;
	}
	Ref<World3D> world = car_node_container->get_world_3d();
	if (world.is_null()) {
		return;
	}
	const RID scenario = world->get_scenario();
	if (!scenario.is_valid()) {
		return;
	}
	if (render_thruster_light_scenario.is_valid() && render_thruster_light_scenario != scenario) {
		clear_render_thruster_lights();
	}
	render_thruster_light_scenario = scenario;
	render_thruster_lights.reserve(capacity);
	while (static_cast<int>(render_thruster_lights.size()) < capacity) {
		RenderThrusterLightRID item;
		item.light = rs->omni_light_create();
		rs->light_set_color(item.light, Color(0.3f, 0.7f, 1.0f, 1.0f));
		rs->light_set_param(item.light, RenderingServer::LIGHT_PARAM_RANGE, 64.0);
		rs->light_set_param(item.light, RenderingServer::LIGHT_PARAM_ENERGY, 0.0);
		rs->light_set_param(item.light, RenderingServer::LIGHT_PARAM_ATTENUATION, 1.0);
		rs->light_set_shadow(item.light, false);
		rs->light_set_cull_mask(item.light, 2);
		item.instance = rs->instance_create2(item.light, render_thruster_light_scenario);
		rs->instance_set_layer_mask(item.instance, 2);
		rs->instance_set_visible(item.instance, false);
		render_thruster_lights.push_back(item);
	}
}

void GameSim::hide_unused_render_thruster_lights(int used_count)
{
	RenderingServer* rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	if (used_count < 0) {
		used_count = 0;
	}
	if (used_count > static_cast<int>(render_thruster_lights.size())) {
		used_count = static_cast<int>(render_thruster_lights.size());
	}
	for (int i = used_count; i < render_thruster_light_visible_count && i < static_cast<int>(render_thruster_lights.size()); ++i) {
		rs->instance_set_visible(render_thruster_lights[i].instance, false);
	}
	render_thruster_light_visible_count = used_count;
}

void GameSim::cache_native_visual_effect_nodes()
{
	render_vehicle_effect_refs.clear();
	render_effect_full_flags.clear();
	render_effect_pool_slots.clear();
	const int car_count = std::max(0, num_cars);
	render_vehicle_effect_refs.resize(car_count);
	render_effect_full_flags.resize(car_count);
	if (!car_node_container) {
		return;
	}
	TypedArray<godot::Node> visual_nodes = car_node_container->get_children();
	for (int i = 0; i < visual_nodes.size(); ++i) {
		Node* car_node = Object::cast_to<Node>(visual_nodes[i]);
		if (!car_node) {
			continue;
		}
		car_node->set_process(false);
		car_node->set_physics_process(false);
		const Variant slot_var = car_node->get(StringName("effect_pool_slot"));
		if (slot_var.get_type() != Variant::INT) {
			continue;
		}
		const int slot_index = static_cast<int>(static_cast<int64_t>(slot_var));
		const bool local_visual = static_cast<bool>(car_node->get(StringName("local_visual_enabled")));
		int output_slot = slot_index;
		if (slot_index < 0 && local_visual) {
			output_slot = static_cast<int>(render_effect_pool_slots.size());
		}
		if (output_slot < 0) {
			continue;
		}
		if (static_cast<int>(render_effect_pool_slots.size()) <= output_slot) {
			render_effect_pool_slots.resize(output_slot + 1);
		}
		RenderEffectPoolSlot& slot = render_effect_pool_slots[output_slot];
		slot.car_index = -1;
		slot.fixed_local = local_visual ? 1 : 0;
		slot.node = car_node;
		slot.car_transform = Object::cast_to<Node3D>(car_node->get_node_or_null(NodePath("CarTransform")));
		if (slot.car_transform) {
			slot.recharge_particles = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("RechargeParticles")));
			slot.attack_particles = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("AttackParticles")));
			slot.landing_particles = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("LandingParticles")));
			slot.damage_electricity = Object::cast_to<GPUParticles3D>(slot.car_transform->get_node_or_null(NodePath("DamageElectricity")));
			if (slot.damage_electricity) {
				slot.damage_smoke = Object::cast_to<GPUParticles3D>(slot.damage_electricity->get_node_or_null(NodePath("DamageSmoke")));
				slot.damage_electricity_material = slot.damage_electricity->get_process_material();
			}
		}
		slot.boost_electricity = Object::cast_to<Object>(car_node->get_node_or_null(NodePath("BoostElectricity")));
		if (slot.recharge_particles) {
			slot.recharge_particles->set_emitting(false);
		}
		if (slot.attack_particles) {
			slot.attack_particles->set_emitting(false);
		}
		if (slot.landing_particles) {
			slot.landing_particles->set_emitting(false);
		}
		if (slot.damage_electricity) {
			slot.damage_electricity->set_emitting(false);
			slot.damage_electricity->set_amount_ratio(0.0);
			slot.damage_electricity->set_visible(false);
		}
		if (slot.damage_smoke) {
			slot.damage_smoke->set_emitting(false);
			slot.damage_smoke->set_amount_ratio(0.0);
		}
		if (slot.boost_electricity) {
			slot.boost_electricity->set("boosting", false);
			slot.boost_electricity->set("visible", false);
		}
	}
}

void GameSim::set_gameplay_camera(godot::Camera3D* p_camera, int player_id)
{
	gameplay_camera_node = p_camera;
	gameplay_camera_player_id = player_id;
	if (gameplay_camera.is_valid()) {
		gameplay_camera_zoom_mode = gameplay_camera->get_zoom_mode();
	}
	if (gameplay_camera.is_null()) {
		gameplay_camera.instantiate();
	}
	if (gameplay_camera.is_valid()) {
		gameplay_camera->reset();
		gameplay_camera->set_zoom_mode(gameplay_camera_zoom_mode);
	}
	if (gameplay_camera_node) {
		gameplay_camera_node->make_current();
		gameplay_camera_node->set_near(0.25f);
		gameplay_camera_node->set_far(40000.0f);
	}
	update_finish_line_visual();
}

void GameSim::set_gameplay_camera_zoom_mode(int zoom_mode)
{
	gameplay_camera_zoom_mode = std::clamp(zoom_mode, 0, 3);
	if (gameplay_camera.is_valid()) {
		gameplay_camera->set_zoom_mode(gameplay_camera_zoom_mode);
	}
}

void GameSim::set_render_camera(godot::Camera3D* p_camera)
{
	render_camera_node = p_camera;
	update_finish_line_visual();
}

void GameSim::update_finish_line_visual()
{
	if (!current_track || (!render_camera_node && !gameplay_camera_node)) {
		return;
	}
	if (!finish_line_display) {
		finish_line_display = new FinishLineDisplay;
	}
	finish_line_display->configure(this, *current_track);
}

void GameSim::hide_finish_line_visual()
{
	if (finish_line_display) {
		finish_line_display->hide();
	}
}

void GameSim::update_render_visual_snapshots(int visual_count)
{
	if (visual_count <= 0 || !cars) {
		return;
	}
	if (static_cast<int>(render_visual_prev_transforms.size()) != visual_count) {
		render_visual_prev_transforms.resize(visual_count);
		render_visual_current_transforms.resize(visual_count);
		render_final_prev_transforms.resize(visual_count);
		render_final_current_transforms.resize(visual_count);
		render_visual_prev_ground_distances.resize(visual_count);
		render_visual_current_ground_distances.resize(visual_count);
		render_visual_initialized.assign(visual_count, 0);
		render_rollback_corrections.assign(visual_count, SimTransform());
		render_rollback_correction_active.assign(visual_count, 0);
		render_vehicle_visual_state.assign(visual_count, RenderVehicleVisualState());
		render_rollback_capture_transforms.clear();
		render_rollback_capture_pending = false;
	}
	for (int i = 0; i < visual_count; ++i) {
		PhysicsCar* visual_car = nullptr;
		if (i < num_cars) {
			visual_car = &cars[i];
		} else if (bumper_cars && i < num_cars + bumper_count) {
			const int bumper_slot = i - num_cars;
			if (bumper_states[bumper_slot].active) {
				visual_car = &bumper_cars[bumper_slot];
			}
		}
		if (!visual_car) {
			render_visual_initialized[i] = 0;
			render_visual_prev_transforms[i] = SimTransform();
			render_visual_current_transforms[i] = SimTransform();
			render_final_prev_transforms[i] = SimTransform();
			render_final_current_transforms[i] = SimTransform();
			render_visual_prev_ground_distances[i] = 20.0f;
			render_visual_current_ground_distances[i] = 20.0f;
			continue;
		}
		update_machine_visual_transform_for_render(*visual_car->soa, visual_car->soa_index, render_vehicle_visual_state[i]);
		PhysicsCarSoA& soa = *visual_car->soa;
		const int lane = visual_car->soa_index;
		const SimTransform current = MXT_LOAD_TRANSFORM(soa, transform_visual, lane);
		float current_ground_distance = 20.0f - soa.height_above_track[lane];
		if (current_ground_distance < 0.0f) {
			current_ground_distance = 0.0f;
		}
		if (current_ground_distance > 20.0f) {
			current_ground_distance = 20.0f;
		}
		const bool was_initialized = render_visual_initialized[i] != 0;
		if (was_initialized) {
			render_visual_prev_transforms[i] = render_visual_current_transforms[i];
			if (i < static_cast<int>(render_final_prev_transforms.size()) &&
					i < static_cast<int>(render_final_current_transforms.size())) {
				render_final_prev_transforms[i] = render_final_current_transforms[i];
			}
			render_visual_prev_ground_distances[i] = render_visual_current_ground_distances[i];
		} else {
			render_visual_prev_transforms[i] = current;
			if (i < static_cast<int>(render_final_prev_transforms.size())) {
				render_final_prev_transforms[i] = current;
			}
			render_visual_prev_ground_distances[i] = current_ground_distance;
			render_visual_initialized[i] = 1;
		}
		render_visual_current_transforms[i] = current;
		render_visual_current_ground_distances[i] = current_ground_distance;
		if (i < static_cast<int>(render_rollback_correction_active.size()) && render_rollback_correction_active[i]) {
			render_rollback_corrections[i] = interpolate_sim_transform(render_rollback_corrections[i], SimTransform(), 0.3f);
			if (render_correction_is_small(render_rollback_corrections[i])) {
				render_rollback_corrections[i] = SimTransform();
				render_rollback_correction_active[i] = 0;
			}
		}
		SimTransform final_transform = current;
		if (i < static_cast<int>(render_rollback_correction_active.size()) &&
				render_rollback_correction_active[i] &&
				i < static_cast<int>(render_rollback_corrections.size())) {
			final_transform = apply_render_correction(current, render_rollback_corrections[i]);
		}
		if (i < static_cast<int>(render_final_current_transforms.size())) {
			render_final_current_transforms[i] = final_transform;
			if (!was_initialized && i < static_cast<int>(render_final_prev_transforms.size())) {
				render_final_prev_transforms[i] = final_transform;
			}
		}
	}
}

void GameSim::apply_render_multimeshes(float alpha)
{
	const int render_binding_count = static_cast<int>(render_car_archetype_indices.size());
	const int visual_count = std::min(render_binding_count, static_cast<int>(render_final_current_transforms.size()));
	if (render_visible_car_slots.size() < static_cast<size_t>(visual_count)) {
		render_visible_car_slots.resize(visual_count, -1);
	}
	if (render_visible_thruster_slots.size() < static_cast<size_t>(visual_count)) {
		render_visible_thruster_slots.resize(visual_count, -1);
	}
	if (render_visible_counts.size() < render_car_multimeshes.size()) {
		render_visible_counts.resize(render_car_multimeshes.size(), 0);
	}
	if (render_visible_thruster_counts.size() < render_thruster_multimeshes.size()) {
		render_visible_thruster_counts.resize(render_thruster_multimeshes.size(), 0);
	}
	render_last_body_instances = 0;
	render_last_thruster_instances = 0;
	std::fill(render_visible_counts.begin(), render_visible_counts.end(), 0);
	std::fill(render_visible_thruster_counts.begin(), render_visible_thruster_counts.end(), 0);
	for (int archetype = 0; archetype < static_cast<int>(render_body_buffers.size()); ++archetype) {
		RenderBodyMultimeshBuffers& buffers = render_body_buffers[archetype];
		buffers.main_write = nullptr;
		buffers.outline_write = nullptr;
		buffers.outline_main_write = nullptr;
		buffers.shadow_write = nullptr;
		buffers.stamp_write = nullptr;
		if (archetype < static_cast<int>(render_car_multimeshes.size()) && render_car_multimeshes[archetype].is_valid()) {
			const int required = render_car_multimeshes[archetype]->get_instance_count() * 20;
			if (buffers.main.size() != required) {
				buffers.main.resize(required);
			}
			if (required > 0) {
				buffers.main_write = buffers.main.ptrw();
			}
		}
		if (archetype < static_cast<int>(render_outline_multimeshes.size()) && render_outline_multimeshes[archetype].is_valid()) {
			const int required = render_outline_multimeshes[archetype]->get_instance_count() * 20;
			if (buffers.outline.size() != required) {
				buffers.outline.resize(required);
			}
			if (required > 0) {
				buffers.outline_write = buffers.outline.ptrw();
			}
		}
		if (archetype < static_cast<int>(render_outline_main_multimeshes.size()) && render_outline_main_multimeshes[archetype].is_valid()) {
			const int required = render_outline_main_multimeshes[archetype]->get_instance_count() * 20;
			if (buffers.outline_main.size() != required) {
				buffers.outline_main.resize(required);
			}
			if (required > 0) {
				buffers.outline_main_write = buffers.outline_main.ptrw();
			}
		}
		if (archetype < static_cast<int>(render_shadow_multimeshes.size()) && render_shadow_multimeshes[archetype].is_valid()) {
			const int required = render_shadow_multimeshes[archetype]->get_instance_count() * 20;
			if (buffers.shadow.size() != required) {
				buffers.shadow.resize(required);
			}
			if (required > 0) {
				buffers.shadow_write = buffers.shadow.ptrw();
			}
		}
		if (archetype < static_cast<int>(render_stamp_multimeshes.size()) && render_stamp_multimeshes[archetype].is_valid()) {
			const int required = render_stamp_multimeshes[archetype]->get_instance_count() * 20;
			if (buffers.stamp.size() != required) {
				buffers.stamp.resize(required);
			}
			if (required > 0) {
				buffers.stamp_write = buffers.stamp.ptrw();
			}
		}
	}
	for (int archetype = 0; archetype < static_cast<int>(render_thruster_multimeshes.size()); ++archetype) {
		render_thruster_buffer_write_ptrs[archetype] = nullptr;
		if (!render_thruster_multimeshes[archetype].is_valid()) {
			continue;
		}
		godot::PackedFloat32Array& buffer = render_thruster_buffers[archetype];
		const int required_float_count = render_thruster_multimeshes[archetype]->get_instance_count() * 20;
		if (buffer.size() != required_float_count) {
			buffer.resize(required_float_count);
		}
		if (required_float_count > 0) {
			render_thruster_buffer_write_ptrs[archetype] = buffer.ptrw();
		}
	}
	Camera3D* camera = render_camera_node ? render_camera_node : gameplay_camera_node;
	SimVec3 camera_origin;
	SimVec3 camera_right;
	SimVec3 camera_up;
	SimVec3 camera_forward;
	float camera_tan_half_fov = 1.0f;
	float camera_aspect = 4.0f / 3.0f;
	float camera_far = 40000.0f;
	if (camera) {
		const Transform3D camera_transform = camera->get_global_transform();
		camera_origin = sim_vec3(camera_transform.origin);
		camera_right = sim_vec3(camera_transform.basis.get_column(0)).normalized();
		camera_up = sim_vec3(camera_transform.basis.get_column(1)).normalized();
		camera_forward = sim_vec3(camera_transform.basis.get_column(2)) * -1.0f;
		camera_forward = camera_forward.normalized();
		camera_tan_half_fov = std::tan(static_cast<float>(camera->get_fov()) * (TAU / 360.0f) * 0.5f);
		camera_far = static_cast<float>(camera->get_far());
		if (Viewport* viewport = camera->get_viewport()) {
			const Vector2 size = viewport->get_visible_rect().size;
			if (size.y > 0.0f) {
				camera_aspect = static_cast<float>(size.x / size.y);
			}
		}
	}
	constexpr float CAR_VISIBILITY_RADIUS = 32.0f;
	const float body_lod_max_distance_sq = render_car_body_view_distance * render_car_body_view_distance;
	for (int i = 0; i < visual_count; ++i) {
		PhysicsCar* visual_car = nullptr;
		if (i < num_cars) {
			visual_car = &cars[i];
		} else if (bumper_cars && i < num_cars + bumper_count) {
			const int bumper_slot = i - num_cars;
			if (bumper_states[bumper_slot].active) {
				visual_car = &bumper_cars[bumper_slot];
			}
		}
		if (!visual_car) {
			render_visible_car_slots[i] = -1;
			render_visible_thruster_slots[i] = -1;
			continue;
		}
		if (i >= static_cast<int>(render_car_archetype_indices.size()) || i >= static_cast<int>(render_car_slots.size())) {
			render_visible_car_slots[i] = -1;
			render_visible_thruster_slots[i] = -1;
			continue;
		}
		const int archetype = render_car_archetype_indices[i];
		if (archetype < 0) {
			render_visible_car_slots[i] = -1;
			render_visible_thruster_slots[i] = -1;
			continue;
		}
		SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		render_visible_thruster_slots[i] = -1;
		if (archetype < static_cast<int>(render_thruster_multimeshes.size()) &&
				archetype < static_cast<int>(render_thruster_local_transforms.size()) &&
				archetype < static_cast<int>(render_visible_thruster_counts.size()) &&
				render_thruster_multimeshes[archetype].is_valid()) {
			const std::vector<SimTransform>& thruster_locals = render_thruster_local_transforms[archetype];
			const int thruster_count = static_cast<int>(thruster_locals.size());
			const int thruster_visible_slot = render_visible_thruster_counts[archetype]++;
			render_visible_thruster_slots[i] = thruster_visible_slot;
			const float thrust = i < static_cast<int>(render_thruster_current_thrust.size()) ? render_thruster_current_thrust[i] : 0.0f;
			float* const thruster_buffer = render_thruster_buffer_write_ptrs[archetype];
			for (int t = 0; t < thruster_count; ++t) {
				const int thruster_slot = thruster_visible_slot * thruster_count + t;
				const SimTransform thruster_transform = visual_transform * thruster_locals[t];
				if (!thruster_buffer) {
					continue;
				}
				float* const instance = thruster_buffer + thruster_slot * 20;
				instance[0] = thruster_transform.basis.c0.x;
				instance[1] = thruster_transform.basis.c1.x;
				instance[2] = thruster_transform.basis.c2.x;
				instance[3] = thruster_transform.origin.x;
				instance[4] = thruster_transform.basis.c0.y;
				instance[5] = thruster_transform.basis.c1.y;
				instance[6] = thruster_transform.basis.c2.y;
				instance[7] = thruster_transform.origin.y;
				instance[8] = thruster_transform.basis.c0.z;
				instance[9] = thruster_transform.basis.c1.z;
				instance[10] = thruster_transform.basis.c2.z;
				instance[11] = thruster_transform.origin.z;
				instance[12] = thrust;
				instance[13] = thrust;
				instance[14] = thrust;
				instance[15] = thrust;
				instance[16] = thrust * 0.2f;
				instance[17] = static_cast<float>((tick + t) & 255) * 0.0245436926f;
				instance[18] = thrust;
				instance[19] = 1.0f;
			}
		}
		const int slot = render_car_slots[i];
		if (archetype >= static_cast<int>(render_car_multimeshes.size()) ||
				archetype >= static_cast<int>(render_car_local_transforms.size()) ||
				render_car_multimeshes[archetype].is_null() || slot < 0) {
			render_visible_car_slots[i] = -1;
			continue;
		}
		bool visible = true;
		if (camera && !render_all_car_bodies) {
			const SimVec3 camera_to_car = visual_transform.origin - camera_origin;
			const float camera_distance_sq = camera_to_car.length_squared();
			const float forward_distance = camera_to_car.dot(camera_forward);
			const float right_distance = camera_to_car.dot(camera_right);
			const float up_distance = camera_to_car.dot(camera_up);
			const float depth_for_width = std::max(forward_distance, 0.0f);
			visible =
				forward_distance >= -CAR_VISIBILITY_RADIUS &&
				forward_distance <= camera_far + CAR_VISIBILITY_RADIUS &&
				std::abs(right_distance) <= depth_for_width * camera_tan_half_fov * camera_aspect + CAR_VISIBILITY_RADIUS &&
				std::abs(up_distance) <= depth_for_width * camera_tan_half_fov + CAR_VISIBILITY_RADIUS;
			visible = visible && camera_distance_sq <= body_lod_max_distance_sq;
		}
		if (!visible) {
			render_visible_car_slots[i] = -1;
			continue;
		}
		const int visible_slot = render_visible_counts[archetype]++;
		render_visible_car_slots[i] = visible_slot;
		PhysicsCarSoA& soa = *visual_car->soa;
		const int lane = visual_car->soa_index;
		godot::Color body_overlay(0, 0, 0, 1);
		if (i < static_cast<int>(render_vehicle_effect_refs.size())) {
			body_overlay = render_vehicle_effect_refs[i].overlay;
			body_overlay.r += render_vehicle_effect_refs[i].energy_overlay.r;
			body_overlay.g += render_vehicle_effect_refs[i].energy_overlay.g;
			body_overlay.b += render_vehicle_effect_refs[i].energy_overlay.b;
			body_overlay.r += render_vehicle_effect_refs[i].impact_flash;
			body_overlay.g += render_vehicle_effect_refs[i].impact_flash;
			body_overlay.b += render_vehicle_effect_refs[i].impact_flash;
			body_overlay.a = 1.0f;
		}
		SimVec3 outline_velocity = LOAD_INDEXED_VEC3(soa, position_old, lane) - LOAD_INDEXED_VEC3(soa, position_current, lane);
		const float outline_speed = outline_velocity.length();
		if (outline_speed <= 0.0001f) {
			outline_velocity = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(2) * 0.01f;
		} else {
			outline_velocity = outline_velocity * ((std::max(outline_speed - 4.0f, 0.0f) * 0.5f) / outline_speed) +
				MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(2) * 0.01f;
		}
		RenderBodyMultimeshBuffers& body_buffers = render_body_buffers[archetype];
		if (render_car_multimeshes[archetype].is_valid() && body_buffers.main_write) {
			const SimTransform instance_transform = visual_transform * render_car_local_transforms[archetype];
			write_multimesh_buffer_instance(body_buffers.main_write, visible_slot, instance_transform, body_overlay, godot::Color(0, 0, 0, 1));
		}
		if (archetype < static_cast<int>(render_stamp_multimeshes.size()) &&
				archetype < static_cast<int>(render_stamp_local_transforms.size()) &&
				render_stamp_multimeshes[archetype].is_valid() && body_buffers.stamp_write) {
			const SimTransform stamp_transform = visual_transform * render_stamp_local_transforms[archetype];
			write_multimesh_buffer_instance(body_buffers.stamp_write, visible_slot, stamp_transform, godot::Color(1, 1, 1, 1), godot::Color(0, 0, 0, 1));
		}
		if (archetype < static_cast<int>(render_outline_multimeshes.size()) &&
				archetype < static_cast<int>(render_outline_local_transforms.size()) &&
				render_outline_multimeshes[archetype].is_valid() && body_buffers.outline_write) {
			const SimTransform outline_transform = visual_transform * render_outline_local_transforms[archetype];
			const float boost_outline = std::max(0.0f, std::min(1.0f,
				static_cast<float>(std::max(soa.boost_frames_manual[lane], soa.boost_frames_dash[lane])) * 0.005f));
			write_multimesh_buffer_instance(body_buffers.outline_write, visible_slot, outline_transform,
				godot::Color(0.5f * boost_outline, 0.7f * boost_outline, 1.0f * boost_outline, 1.0f),
				godot::Color(outline_velocity.x, outline_velocity.y, outline_velocity.z, 1.0f));
		}
		if (archetype < static_cast<int>(render_outline_main_multimeshes.size()) &&
				archetype < static_cast<int>(render_outline_main_local_transforms.size()) &&
				render_outline_main_multimeshes[archetype].is_valid() && body_buffers.outline_main_write) {
			const SimTransform outline_transform = visual_transform * render_outline_main_local_transforms[archetype];
			write_multimesh_buffer_instance(body_buffers.outline_main_write, visible_slot, outline_transform,
				godot::Color(0, 0, 0, 1), godot::Color(outline_velocity.x, outline_velocity.y, outline_velocity.z, 1.0f));
		}
		if (archetype < static_cast<int>(render_shadow_multimeshes.size()) &&
				archetype < static_cast<int>(render_shadow_local_transforms.size()) &&
				render_shadow_multimeshes[archetype].is_valid() && body_buffers.shadow_write) {
			const float prev_ground_distance = i < static_cast<int>(render_visual_prev_ground_distances.size()) ? render_visual_prev_ground_distances[i] : 20.0f;
			const float current_ground_distance = i < static_cast<int>(render_visual_current_ground_distances.size()) ? render_visual_current_ground_distances[i] : prev_ground_distance;
			const float ground_distance = prev_ground_distance + (current_ground_distance - prev_ground_distance) * alpha;
			SimVec3 shadow_normal = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (shadow_normal.length_squared() <= 0.0001f) {
				shadow_normal = visual_transform.basis.get_column(1);
			}
			shadow_normal = shadow_normal.normalized();
			SimTransform shadow_transform = visual_transform * render_shadow_local_transforms[archetype];
			if (ground_distance >= 20.0f) {
				shadow_transform.basis.c0 = SimVec3();
				shadow_transform.basis.c1 = SimVec3();
				shadow_transform.basis.c2 = SimVec3();
			} else {
				shadow_transform.origin += -shadow_normal * ground_distance;
				shadow_transform.basis.c0 = shadow_transform.basis.c0.slide(shadow_normal);
				shadow_transform.basis.c1 = shadow_transform.basis.c1.slide(shadow_normal);
				shadow_transform.basis.c2 = shadow_transform.basis.c2.slide(shadow_normal);
			}
			write_multimesh_buffer_instance(body_buffers.shadow_write, visible_slot, shadow_transform,
				godot::Color(1, 1, 1, 1), godot::Color(0, 0, 0, 0));
		}
	}
	for (int archetype = 0; archetype < static_cast<int>(render_car_multimeshes.size()); ++archetype) {
		const int visible_count = archetype < static_cast<int>(render_visible_counts.size()) ? render_visible_counts[archetype] : 0;
		render_last_body_instances += visible_count;
		if (render_car_multimeshes[archetype].is_valid()) {
			if (visible_count > 0 && archetype < static_cast<int>(render_body_buffers.size())) {
				render_car_multimeshes[archetype]->set_buffer(render_body_buffers[archetype].main);
			}
			render_car_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_outline_multimeshes.size()) && render_outline_multimeshes[archetype].is_valid()) {
			if (visible_count > 0) {
				render_outline_multimeshes[archetype]->set_buffer(render_body_buffers[archetype].outline);
			}
			render_outline_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_outline_main_multimeshes.size()) && render_outline_main_multimeshes[archetype].is_valid()) {
			if (visible_count > 0) {
				render_outline_main_multimeshes[archetype]->set_buffer(render_body_buffers[archetype].outline_main);
			}
			render_outline_main_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_shadow_multimeshes.size()) && render_shadow_multimeshes[archetype].is_valid()) {
			if (visible_count > 0) {
				render_shadow_multimeshes[archetype]->set_buffer(render_body_buffers[archetype].shadow);
			}
			render_shadow_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_stamp_multimeshes.size()) && render_stamp_multimeshes[archetype].is_valid()) {
			if (visible_count > 0) {
				render_stamp_multimeshes[archetype]->set_buffer(render_body_buffers[archetype].stamp);
			}
			render_stamp_multimeshes[archetype]->set_visible_instance_count(visible_count);
		}
		if (archetype < static_cast<int>(render_thruster_multimeshes.size()) && render_thruster_multimeshes[archetype].is_valid()) {
			int thruster_count = 0;
			if (archetype < static_cast<int>(render_thruster_local_transforms.size())) {
				thruster_count = static_cast<int>(render_thruster_local_transforms[archetype].size());
			}
			const int visible_thruster_count = archetype < static_cast<int>(render_visible_thruster_counts.size()) ? render_visible_thruster_counts[archetype] : 0;
			const int thruster_instances = visible_thruster_count * thruster_count;
			render_last_thruster_instances += thruster_instances;
			if (thruster_instances > 0 && archetype < static_cast<int>(render_thruster_buffers.size())) {
				render_thruster_multimeshes[archetype]->set_buffer(render_thruster_buffers[archetype]);
			}
			render_thruster_multimeshes[archetype]->set_visible_instance_count(thruster_instances);
		}
	}
}

void GameSim::update_native_visual_effects(int visual_count, float alpha, bool step_effects, float effect_delta, bool step_electricity)
{
	if (!cars || render_vehicle_effect_refs.empty()) {
		return;
	}
	const int count = std::min(visual_count, static_cast<int>(render_vehicle_effect_refs.size()));
	if (static_cast<int>(render_thruster_current_thrust.size()) < count) {
		render_thruster_current_thrust.resize(count, 0.0f);
	}
	int local_car_index = -1;
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids && car_player_ids[i] == gameplay_camera_player_id) {
			local_car_index = i;
			break;
		}
	}
	if (static_cast<int>(render_effect_full_flags.size()) < count) {
		render_effect_full_flags.resize(count);
	}
	std::fill(render_effect_full_flags.begin(), render_effect_full_flags.begin() + count, 0);
	constexpr int FULL_EFFECT_BUDGET = 30;
	float nearest_distances[FULL_EFFECT_BUDGET];
	int nearest_indices[FULL_EFFECT_BUDGET];
	for (int i = 0; i < FULL_EFFECT_BUDGET; ++i) {
		nearest_distances[i] = FLT_MAX;
		nearest_indices[i] = -1;
	}
	SimVec3 camera_origin;
	bool has_camera = false;
	Camera3D* render_camera = render_camera_node ? render_camera_node : gameplay_camera_node;
	if (render_camera) {
		camera_origin = sim_vec3(render_camera->get_global_transform().origin);
		has_camera = true;
	}
	for (int i = 0; i < count; ++i) {
		const SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		if (has_camera && !render_camera->is_position_in_frustum(gd_vec3(visual_transform.origin))) {
			continue;
		}
		const float dist_sq = has_camera ? (visual_transform.origin - camera_origin).length_squared() : 0.0f;
		if (dist_sq < nearest_distances[FULL_EFFECT_BUDGET - 1]) {
			int insert_at = FULL_EFFECT_BUDGET - 1;
			while (insert_at > 0 && dist_sq < nearest_distances[insert_at - 1]) {
				nearest_distances[insert_at] = nearest_distances[insert_at - 1];
				nearest_indices[insert_at] = nearest_indices[insert_at - 1];
				--insert_at;
			}
			nearest_distances[insert_at] = dist_sq;
			nearest_indices[insert_at] = i;
		}
	}
	const int full_budget = std::min(count, FULL_EFFECT_BUDGET);
	if (local_car_index >= 0 && local_car_index < count) {
		bool local_selected = false;
		for (int n = 0; n < full_budget; ++n) {
			if (nearest_indices[n] == local_car_index) {
				local_selected = true;
				break;
			}
		}
		if (!local_selected && full_budget > 0) {
			nearest_indices[full_budget - 1] = local_car_index;
			nearest_distances[full_budget - 1] = -1.0f;
		}
	}
	for (int n = 0; n < full_budget; ++n) {
		const int idx = nearest_indices[n];
		if (idx >= 0 && idx < count) {
			render_effect_full_flags[idx] = 1;
		}
	}

	int max_thrusters_per_car = 0;
	if (render_thruster_lights_enabled) {
		for (const std::vector<SimTransform>& thrusters : render_thruster_local_transforms) {
			max_thrusters_per_car = std::max(max_thrusters_per_car, static_cast<int>(thrusters.size()));
		}
	}
	ensure_render_thruster_light_capacity((FULL_EFFECT_BUDGET + 1) * max_thrusters_per_car);
	RenderingServer* rs = RenderingServer::get_singleton();
	const float light_phase = std::sin(static_cast<float>(tick) * 2.0f) * 0.5f + 0.5f;
	int thruster_light_slot = 0;
	int node_effect_slot = 0;
	RenderEffectPoolSlot* local_effect_slot = nullptr;
	for (RenderEffectPoolSlot& slot : render_effect_pool_slots) {
		if (slot.fixed_local) {
			local_effect_slot = &slot;
			if (render_node_effects_enabled) {
				continue;
			}
		}
		if (!render_node_effects_enabled ||
				(slot.car_index >= 0 && (slot.car_index >= count || render_effect_full_flags[slot.car_index] == 0))) {
			if (slot.recharge_particles) {
				slot.recharge_particles->set_emitting(false);
			}
			if (slot.attack_particles) {
				slot.attack_particles->set_emitting(false);
			}
			if (slot.landing_particles) {
				slot.landing_particles->set_emitting(false);
			}
			if (slot.damage_electricity) {
				slot.damage_electricity->set_visible(false);
				slot.damage_electricity->set_emitting(false);
				slot.damage_electricity->set_amount_ratio(0.0);
			}
			if (slot.damage_smoke) {
				slot.damage_smoke->set_visible(false);
				slot.damage_smoke->set_emitting(false);
				slot.damage_smoke->set_amount_ratio(0.0);
			}
			if (slot.boost_electricity) {
				slot.boost_electricity->set("boosting", false);
				slot.boost_electricity->set("visible", false);
			}
			slot.car_index = -1;
		}
	}

	for (int i = 0; i < count; ++i) {
		RenderVehicleEffectRefs& refs = render_vehicle_effect_refs[i];
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const uint32_t machine_state = soa.machine_state[lane];
		const uint32_t terrain_state = soa.terrain_state[lane];
		SimTransform visual_transform = interpolate_sim_transform(
			render_final_prev_transforms[i],
			render_final_current_transforms[i],
			alpha);
		const bool full = render_effect_full_flags[i] != 0;

		if (step_effects && (machine_state & (MACHINESTATE::JUST_PRESSED_BOOST | MACHINESTATE::JUST_HIT_DASHPLATE)) != 0u) {
			refs.overlay.r += 0.293f * 0.75f;
			refs.overlay.g += 0.560f * 0.75f;
			refs.overlay.b += 0.886f * 0.75f;
		}
		if (step_effects && (machine_state & (MACHINESTATE::SPINATTACKING | MACHINESTATE::SIDEATTACKING)) != 0u) {
			refs.overlay.r += (0.5f - refs.overlay.r) * 0.5f;
			refs.overlay.g += (0.5f - refs.overlay.g) * 0.5f;
			refs.overlay.b += (0.0f - refs.overlay.b) * 0.5f;
		}
		if (step_effects && soa.s_boost_active[lane]) {
			refs.overlay.r += (1.0f - refs.overlay.r) * 0.6f;
			refs.overlay.g += (0.9f - refs.overlay.g) * 0.6f;
			refs.overlay.b += (0.3f - refs.overlay.b) * 0.6f;
		}
		if (step_effects && (terrain_state & TERRAIN::RECHARGE) != 0u) {
			refs.overlay.r += VEHICLE_RECHARGE_OVERLAY_GAIN;
			refs.overlay.b += VEHICLE_RECHARGE_OVERLAY_GAIN;
		}
		if (step_effects) {
			refs.overlay.r += (0.0f - refs.overlay.r) * VEHICLE_OVERLAY_FADE_WEIGHT;
			refs.overlay.g += (0.0f - refs.overlay.g) * VEHICLE_OVERLAY_FADE_WEIGHT;
			refs.overlay.b += (0.0f - refs.overlay.b) * VEHICLE_OVERLAY_FADE_WEIGHT;
		}
		const float max_energy = std::max(soa.calced_max_energy[lane], 0.001f);
		const float energy_ratio = std::clamp(soa.energy[lane] / max_energy, 0.0f, 1.0f);
		const float health_effect_ratio = std::min(1.0f, energy_ratio * 4.0f);
		const float low_energy_ratio = std::max(0.0f, 1.0f - health_effect_ratio);
		const float manual_boost_ratio = static_cast<float>(soa.boost_frames_manual[lane]) /
			static_cast<float>(std::max(1u, soa.boost_duration_manual_frames[lane]));
		const float dash_boost_ratio = static_cast<float>(soa.boost_frames_dash[lane]) /
			static_cast<float>(std::max(1u, soa.boost_duration_dash_frames[lane]));
		const float boost_ratio = std::max(manual_boost_ratio, dash_boost_ratio);
		const float low_energy_flash = (std::sin(static_cast<float>(tick) * 0.25f) * 0.5f + 0.5f) * low_energy_ratio;
		refs.energy_overlay = godot::Color(0.8f * low_energy_flash, -0.2f * low_energy_flash, -0.2f * low_energy_flash, 1.0f);
		const float thrust = std::max(0.0f, (soa.input_accel[lane] + std::sqrt(std::max(0.0f, soa.boost_turbo[lane])) * 0.1f) * soa.input_accel[lane]);
		render_thruster_current_thrust[i] += (thrust - render_thruster_current_thrust[i]) * 0.4f;
		if (!full) {
			if (refs.full_effect_active) {
				refs.full_effect_active = 0;
			}
			if (step_effects) {
				refs.terrain_state_old = terrain_state;
				refs.machine_state_old = machine_state;
			}
			continue;
		}
		refs.full_effect_active = 1;
		RenderEffectPoolSlot* pool_slot = nullptr;
		if (!render_node_effects_enabled) {
			pool_slot = nullptr;
		} else if (i == local_car_index && local_effect_slot) {
			pool_slot = local_effect_slot;
		} else {
			while (node_effect_slot < static_cast<int>(render_effect_pool_slots.size()) &&
					render_effect_pool_slots[node_effect_slot].fixed_local) {
				++node_effect_slot;
			}
			if (node_effect_slot < static_cast<int>(render_effect_pool_slots.size())) {
				pool_slot = &render_effect_pool_slots[node_effect_slot];
				++node_effect_slot;
			}
		}
		if (pool_slot) {
			if (pool_slot->car_index != i) {
				if (pool_slot->recharge_particles) {
					pool_slot->recharge_particles->set_emitting(false);
				}
				if (pool_slot->attack_particles) {
					pool_slot->attack_particles->set_emitting(false);
				}
				if (pool_slot->landing_particles) {
					pool_slot->landing_particles->set_emitting(false);
				}
				if (pool_slot->damage_electricity) {
					pool_slot->damage_electricity->set_visible(false);
					pool_slot->damage_electricity->set_emitting(false);
					pool_slot->damage_electricity->set_amount_ratio(0.0);
					pool_slot->damage_electricity->restart();
				}
				if (pool_slot->damage_smoke) {
					pool_slot->damage_smoke->set_emitting(false);
					pool_slot->damage_smoke->set_amount_ratio(0.0);
					pool_slot->damage_smoke->restart();
				}
				if (pool_slot->boost_electricity) {
					pool_slot->boost_electricity->set("boosting", false);
					pool_slot->boost_electricity->set("visible", false);
					pool_slot->boost_electricity->set("old_transform", gd_transform(visual_transform));
					pool_slot->boost_electricity->set("queued_tendrils", 0.0);
				}
				if (!pool_slot->fixed_local && pool_slot->node) {
					pool_slot->node->set("owning_id", car_player_ids ? car_player_ids[i] : -1);
				}
			}
			pool_slot->car_index = i;
		}
		if (pool_slot) {
			if (pool_slot->car_transform) {
				pool_slot->car_transform->set_global_transform(gd_transform(visual_transform));
			}
			if (pool_slot->recharge_particles) {
				pool_slot->recharge_particles->set_emitting((terrain_state & TERRAIN::RECHARGE) != 0u);
			}
			if (pool_slot->attack_particles) {
				pool_slot->attack_particles->set_emitting((machine_state & (MACHINESTATE::SIDEATTACKING | MACHINESTATE::SPINATTACKING)) != 0u);
			}
			if (step_effects && pool_slot->landing_particles &&
					(machine_state & MACHINESTATE::JUSTLANDED) != 0u &&
					(refs.machine_state_old & MACHINESTATE::JUSTLANDED) == 0u) {
				pool_slot->landing_particles->restart();
				pool_slot->landing_particles->set_emitting(true);
			}
			if (pool_slot->damage_electricity) {
				const bool active = low_energy_ratio > 0.001f || boost_ratio > 0.5f;
				const SimVec3 damage_effect_origin = visual_transform.origin + visual_transform.basis.get_column(1) * -0.125f;
				pool_slot->damage_electricity->set_global_position(gd_vec3(damage_effect_origin));
				pool_slot->damage_electricity->set_visible(active);
				pool_slot->damage_electricity->set_emitting(active);
				pool_slot->damage_electricity->set_amount_ratio(active ? low_energy_ratio + std::max(boost_ratio - 0.5f, 0.0f) : 0.0f);
				if (pool_slot->damage_electricity_material.is_valid()) {
					pool_slot->damage_electricity_material->set(
						StringName("color"),
						godot::Color(
							1.0f + (0.5f - 1.0f) * health_effect_ratio,
							0.75f,
							0.5f + (1.0f - 0.5f) * health_effect_ratio,
							1.0f));
				}
			}
			if (pool_slot->damage_smoke) {
				const bool smoke_active = low_energy_ratio > 0.001f;
				pool_slot->damage_smoke->set_visible(smoke_active);
				pool_slot->damage_smoke->set_emitting(smoke_active);
				pool_slot->damage_smoke->set_amount_ratio(smoke_active ? low_energy_ratio : 0.0f);
			}
		}
		const int archetype = i < static_cast<int>(render_car_archetype_indices.size()) ? render_car_archetype_indices[i] : -1;
		if (render_thruster_lights_enabled && rs && archetype >= 0 && archetype < static_cast<int>(render_thruster_local_transforms.size())) {
			const std::vector<SimTransform>& local_thrusters = render_thruster_local_transforms[archetype];
			const float current_thrust = render_thruster_current_thrust[i];
			for (int t = 0; t < static_cast<int>(local_thrusters.size()) && thruster_light_slot < static_cast<int>(render_thruster_lights.size()); ++t) {
				RenderThrusterLightRID& light = render_thruster_lights[thruster_light_slot];
				if (current_thrust > 0.01f) {
					const SimTransform thruster_transform = visual_transform * local_thrusters[t];
					rs->instance_set_transform(light.instance, gd_transform(thruster_transform));
					rs->light_set_param(light.light, RenderingServer::LIGHT_PARAM_ENERGY, (4.0f + 2.0f * light_phase) * current_thrust);
					rs->light_set_param(light.light, RenderingServer::LIGHT_PARAM_ATTENUATION, (1.0f + 2.0f * light_phase) * current_thrust);
					rs->instance_set_visible(light.instance, true);
				} else {
					rs->instance_set_visible(light.instance, false);
				}
				++thruster_light_slot;
			}
		}

		if (pool_slot && pool_slot->boost_electricity) {
			SimVec3 track_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (track_up.length_squared() <= 0.0001f) {
				track_up = visual_transform.basis.get_column(1);
			}
			track_up = track_up.normalized();
			float ground_distance = 20.0f - soa.height_above_track[lane];
			if (ground_distance < 0.0f) {
				ground_distance = 0.0f;
			}
			if (ground_distance > 20.0f) {
				ground_distance = 20.0f;
			}
			const SimVec3 track_surface_pos = LOAD_INDEXED_VEC3(soa, position_current, lane) - track_up * ground_distance;
			const bool boosting =
				full &&
				((soa.boost_frames_dash[lane] > 0u || soa.boost_frames_manual[lane] > 0u) &&
					(machine_state & MACHINESTATE::AIRBORNE) == 0u);
			pool_slot->boost_electricity->set("boosting", boosting);
			pool_slot->boost_electricity->set("visible", true);
			if (full && step_electricity) {
				pool_slot->boost_electricity->set("ground", godot::Plane(gd_vec3(track_up), gd_vec3(track_surface_pos)));
				pool_slot->boost_electricity->set("tendril_lifetime", std::max(0.1f, std::min(0.3f, 0.3f - soa.speed_kmh[lane] * (0.2f / 3000.0f))));
				pool_slot->boost_electricity->call("calculate_electricity", static_cast<double>(effect_delta), gd_transform(visual_transform));
			}
		}

		if (step_effects) {
			refs.terrain_state_old = terrain_state;
			refs.machine_state_old = machine_state;
		}
	}
	for (int i = node_effect_slot; i < static_cast<int>(render_effect_pool_slots.size()); ++i) {
		RenderEffectPoolSlot& slot = render_effect_pool_slots[i];
		if (slot.fixed_local) {
			continue;
		}
		if (slot.recharge_particles) {
			slot.recharge_particles->set_emitting(false);
		}
		if (slot.attack_particles) {
			slot.attack_particles->set_emitting(false);
		}
		if (slot.landing_particles) {
			slot.landing_particles->set_emitting(false);
		}
		if (slot.damage_electricity) {
			slot.damage_electricity->set_visible(false);
			slot.damage_electricity->set_emitting(false);
			slot.damage_electricity->set_amount_ratio(0.0);
		}
		if (slot.damage_smoke) {
			slot.damage_smoke->set_emitting(false);
			slot.damage_smoke->set_amount_ratio(0.0);
		}
		if (slot.boost_electricity) {
			slot.boost_electricity->set("boosting", false);
			slot.boost_electricity->set("visible", false);
		}
	}
	hide_unused_render_thruster_lights(thruster_light_slot);
}

void GameSim::update_native_gameplay_camera(bool step_camera)
{
	if (!gameplay_camera_node || gameplay_camera.is_null() || !cars || !car_player_ids) {
		return;
	}
	int car_index = -1;
	for (int i = 0; i < num_cars; ++i) {
		if (car_player_ids[i] == gameplay_camera_player_id) {
			car_index = i;
			break;
		}
	}
	if (car_index < 0) {
		return;
	}
	PhysicsCarSoA& soa = *cars[car_index].soa;
	const int lane = cars[car_index].soa_index;
	godot::Input* input = godot::Input::get_singleton();
	SimVec3 camera_position_correction;
	const bool has_camera_render_correction =
		car_index < static_cast<int>(render_rollback_correction_active.size()) &&
		render_rollback_correction_active[car_index] &&
		car_index < static_cast<int>(render_rollback_corrections.size());
	if (has_camera_render_correction) {
		camera_position_correction = render_rollback_corrections[car_index].origin;
	}
	if (step_camera) {
		float aspect_ratio = 4.0f / 3.0f;
		if (godot::Viewport* viewport = gameplay_camera_node->get_viewport()) {
			const godot::Vector2 size = viewport->get_visible_rect().size;
			if (size.y > 0.0f) {
				aspect_ratio = static_cast<float>(size.x / size.y);
			}
		}
		SimVec3 track_up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		if (track_up.length_squared() <= 0.0001f) {
			track_up = MXT_LOAD_TRANSFORM(soa, basis_physical, lane).basis.get_column(1);
		}
		track_up = track_up.normalized();
		float vehicle_pitch_delta_radians = 0.0f;
		const float raw_pitch = soa.velocity_angular_x[lane];
		if (std::abs(raw_pitch) > 3.0f && std::abs(soa.weight_derived_1[lane]) > 0.0001f) {
			vehicle_pitch_delta_radians =
				(raw_pitch - std::copysign(3.0f, raw_pitch)) / soa.weight_derived_1[lane];
		}
		const bool view_up_pressed = input && input->is_action_just_pressed(godot::StringName("CameraUp"));
		const bool view_down_pressed = input && input->is_action_just_pressed(godot::StringName("CameraDown"));
			gameplay_camera->step(
			gd_vec3(LOAD_INDEXED_VEC3(soa, position_current, lane) + camera_position_correction),
			gd_vec3(LOAD_INDEXED_VEC3(soa, position_old, lane) + camera_position_correction),
			gd_transform(MXT_LOAD_TRANSFORM(soa, basis_physical, lane)),
			gd_vec3(track_up),
			gd_vec3(LOAD_INDEXED_VEC3(soa, track_surface_pos, lane)),
			soa.height_above_track[lane],
			soa.speed_kmh[lane],
			vehicle_pitch_delta_radians,
			soa.camera_reorienting[lane],
			soa.camera_repositioning[lane],
				car_index < static_cast<int>(render_vehicle_visual_state.size()) ? render_vehicle_visual_state[car_index].turn_reaction_effect : 0.0f,
			static_cast<int>(soa.machine_state[lane]),
			static_cast<int>(soa.state_2[lane]),
			static_cast<int>(soa.tilt_state[lane * 4 + 0]),
			static_cast<int>(soa.tilt_state[lane * 4 + 1]),
			static_cast<int>(soa.tilt_state[lane * 4 + 2]),
			static_cast<int>(soa.tilt_state[lane * 4 + 3]),
			static_cast<int>(soa.restore_state[lane]),
			static_cast<int>(soa.restore_move_frames[lane]),
			aspect_ratio,
			view_up_pressed,
			view_down_pressed);
		gameplay_camera_zoom_mode = gameplay_camera->get_zoom_mode();
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	auto get_interpolated_car_transform = [&](int index) -> SimTransform {
		if (index >= 0 &&
				index < static_cast<int>(render_final_prev_transforms.size()) &&
				index < static_cast<int>(render_final_current_transforms.size())) {
			return interpolate_sim_transform(
				render_final_prev_transforms[index],
				render_final_current_transforms[index],
				alpha);
		}
		PhysicsCarSoA& fallback_soa = *cars[index].soa;
		const int fallback_lane = cars[index].soa_index;
		SimTransform fallback = interpolate_sim_transform(
			MXT_LOAD_TRANSFORM(fallback_soa, basis_physical_other, fallback_lane),
			MXT_LOAD_TRANSFORM(fallback_soa, basis_physical, fallback_lane),
			alpha);
		fallback.origin = LOAD_INDEXED_VEC3(fallback_soa, position_old, fallback_lane).lerp(
			LOAD_INDEXED_VEC3(fallback_soa, position_current, fallback_lane),
			alpha);
		return fallback;
	};
	godot::Transform3D render_transform = gameplay_camera->get_render_transform(alpha);
	float render_fov = gameplay_camera->get_render_fov(alpha);
	bool intro_camera_active = false;
	if (multiplayer_intro_camera_enabled && start_countdown_extra_frames > 0u) {
		const float intro_frame = static_cast<float>(tick) + alpha;
		if (intro_frame < static_cast<float>(start_countdown_extra_frames)) {
			intro_camera_active = true;
			constexpr float kFlybyFrames = 420.0f;
			constexpr float kReturnFrames = 180.0f;
			SimTransform focus_basis_transform = get_interpolated_car_transform(car_index);
			SimVec3 up = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
			if (up.length_squared() <= 0.0001f) {
				up = focus_basis_transform.basis.get_column(1);
			}
			up = up.normalized();
			SimVec3 forward = focus_basis_transform.basis.get_column(2) * -1.0f;
			if (forward.length_squared() <= 0.0001f) {
				forward = SimVec3(0.0f, 0.0f, -1.0f);
			}
			forward = forward.normalized();
			SimVec3 right = focus_basis_transform.basis.get_column(0);
			if (right.length_squared() <= 0.0001f) {
				right = forward.cross(up);
			}
			right = right.normalized();
			const SimVec3 origin = focus_basis_transform.origin + camera_position_correction;
			float min_forward = FLT_MAX;
			float max_forward = -FLT_MAX;
			float lateral_sum = 0.0f;
			int grid_count = 0;
			for (int i = 0; i < num_cars; ++i) {
				if (car_player_ids && car_player_ids[i] < 0) {
					continue;
				}
				const SimVec3 pos = get_interpolated_car_transform(i).origin;
				const SimVec3 delta = pos - origin;
				const float forward_proj = delta.dot(forward);
				min_forward = std::min(min_forward, forward_proj);
				max_forward = std::max(max_forward, forward_proj);
				lateral_sum += delta.dot(right);
				grid_count += 1;
			}
			if (grid_count > 0) {
				const float flyby_alpha = smoothstep01(std::min(intro_frame / kFlybyFrames, 1.0f));
				const float sweep_start = min_forward - 24.0f;
				const float sweep_end = max_forward + 24.0f;
				const float sweep = sweep_start + (sweep_end - sweep_start) * flyby_alpha;
				const float lateral = lateral_sum / static_cast<float>(grid_count);
				const SimVec3 interest_sim = origin + forward * sweep + right * lateral + up * 3.5f;
				const SimVec3 position_sim = interest_sim - forward * 58.0f + right * 42.0f + up * 30.0f;
				const godot::Vector3 preview_interest = gd_vec3(interest_sim);
				const godot::Vector3 preview_position = gd_vec3(position_sim);
				const godot::Vector3 preview_up = gd_vec3(up);
				if (intro_frame < kFlybyFrames) {
					render_transform = build_camera_transform(preview_position, preview_interest, preview_up);
					render_fov = 72.0f;
				} else {
					const float return_alpha = smoothstep01((intro_frame - kFlybyFrames) / kReturnFrames);
					const godot::Vector3 normal_position = render_transform.origin;
					const godot::Vector3 normal_up = render_transform.basis.get_column(1).normalized();
					const godot::Vector3 normal_interest = normal_position - render_transform.basis.get_column(2).normalized() * 80.0f;
					const godot::Vector3 blended_position = preview_position.lerp(normal_position, return_alpha);
					const godot::Vector3 blended_interest = preview_interest.lerp(normal_interest, return_alpha);
					const godot::Vector3 blended_up = preview_up.lerp(normal_up, return_alpha).normalized();
					render_transform = build_camera_transform(blended_position, blended_interest, blended_up);
					render_fov = 72.0f + (render_fov - 72.0f) * return_alpha;
				}
			}
		}
	}
	if (!intro_camera_active && (soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) != 0u) {
		SimTransform car_basis = get_interpolated_car_transform(car_index);
		SimVec3 up_sim = LOAD_INDEXED_VEC3(soa, track_surface_normal, lane);
		if (up_sim.length_squared() <= 0.0001f) {
			up_sim = car_basis.basis.get_column(1);
		}
		up_sim = up_sim.normalized();
		SimVec3 forward_sim = car_basis.basis.get_column(2) * -1.0f;
		if (forward_sim.length_squared() <= 0.0001f) {
			forward_sim = SimVec3(0.0f, 0.0f, -1.0f);
		}
		forward_sim = forward_sim.normalized();
		SimVec3 right_sim = car_basis.basis.get_column(0);
		if (right_sim.length_squared() <= 0.0001f) {
			right_sim = forward_sim.cross(up_sim);
		}
		right_sim = right_sim.normalized();
		const SimVec3 car_pos_sim = car_basis.origin;
		const godot::Vector3 car_pos = gd_vec3(car_pos_sim);
		const godot::Vector3 up = gd_vec3(up_sim);
		const godot::Vector3 forward = gd_vec3(forward_sim);
		const godot::Vector3 right = gd_vec3(right_sim);
		const float mode_time = static_cast<float>(tick) + alpha;
		const float mode_phase = std::fmod(mode_time, 240.0f) / 240.0f;
		const int camera_mode = static_cast<int>(std::floor(mode_time / 240.0f)) & 3;
		godot::Vector3 interest = car_pos + up * 2.4f;
		godot::Vector3 position;
		if (camera_mode == 0) {
			position = interest - forward * 24.0f + up * 8.0f + right * 9.0f;
			render_fov = 62.0f;
		} else if (camera_mode == 1) {
			const float side = mode_phase < 0.5f ? -1.0f : 1.0f;
			position = interest + forward * 34.0f + right * (24.0f * side) + up * 7.5f;
			render_fov = 54.0f;
		} else if (camera_mode == 2) {
			const float angle = mode_phase * 6.28318530718f;
			position = interest + right * (std::cos(angle) * 28.0f) - forward * (std::sin(angle) * 28.0f) + up * 10.0f;
			render_fov = 66.0f;
		} else {
			position = interest - forward * 10.0f + up * 38.0f + right * 7.0f;
			render_fov = 72.0f;
		}
		render_transform = build_camera_transform(position, interest, up);
	}
	const bool race_camera_active =
		!intro_camera_active && (soa.machine_state[lane] & MACHINESTATE::COMPLETEDRACE_1_Q) == 0u;
	if (race_camera_active && input && input->is_action_pressed(godot::StringName("LookBack"))) {
		const SimTransform car_transform = get_interpolated_car_transform(car_index);
		const godot::Vector3 pivot = gd_vec3(car_transform.origin + camera_position_correction);
		godot::Vector3 up = gd_vec3(car_transform.basis.get_column(1));
		if (up.length_squared() <= 0.0001f) {
			up = render_transform.basis.get_column(1);
		}
		up.normalize();
		auto rotate_half_turn = [&](const godot::Vector3& vector) {
			return up * (2.0f * up.dot(vector)) - vector;
		};
		render_transform.origin = pivot + rotate_half_turn(render_transform.origin - pivot);
		for (int column = 0; column < 3; ++column) {
			render_transform.basis.set_column(
				column,
				rotate_half_turn(render_transform.basis.get_column(column)));
		}
	}
	gameplay_camera_node->set_global_transform(render_transform);
	gameplay_camera_node->set_fov(render_fov);
	gameplay_camera_node->set_near(0.25);
	gameplay_camera_node->set_far(40000.0);
}

void GameSim::render_gamesim_visuals_only(double process_delta)
{
	if (!sim_started || !cars) {
		return;
	}
	const uint64_t profile_start = render_profile_enabled ? render_profile_now_us() : 0;
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	const float effect_delta = std::max(0.0f, std::min(0.1f, static_cast<float>(process_delta)));
	update_dashplate_visuals(effect_delta);
	uint64_t profile_step = render_profile_enabled ? render_profile_now_us() : 0;
	update_native_visual_effects(std::min(num_cars, static_cast<int>(render_final_current_transforms.size())), alpha, false, effect_delta, true);
	if (render_profile_enabled) {
		const uint64_t now = render_profile_now_us();
		const uint64_t elapsed = now - profile_step;
		render_profile_visuals_only_effects_us += elapsed;
		render_profile_visuals_only_effects_max_us = std::max(render_profile_visuals_only_effects_max_us, elapsed);
		profile_step = now;
	}
	apply_render_multimeshes(alpha);
	if (render_profile_enabled) {
		const uint64_t now = render_profile_now_us();
		const uint64_t elapsed = now - profile_step;
		render_profile_visuals_only_multimesh_us += elapsed;
		render_profile_visuals_only_multimesh_max_us = std::max(render_profile_visuals_only_multimesh_max_us, elapsed);
		render_profile_visuals_only_body_instances += static_cast<uint64_t>(std::max(render_last_body_instances, 0));
		render_profile_visuals_only_thruster_instances += static_cast<uint64_t>(std::max(render_last_thruster_instances, 0));
		profile_step = now;
	}
	update_native_gameplay_camera(false);
	render_collision_spark_effects(alpha);
	render_drift_plasma_effects(alpha);
	update_spatial_audio(effect_delta);
	if (render_profile_enabled) {
		const uint64_t now = render_profile_now_us();
		render_profile_visuals_only_camera_us += now - profile_step;
		const uint64_t elapsed = now - profile_start;
		render_profile_visuals_only_total_us += elapsed;
		render_profile_visuals_only_total_max_us = std::max(render_profile_visuals_only_total_max_us, elapsed);
		render_profile_visuals_only_frames += 1;
	}
}

void GameSim::update_spatial_audio(double delta)
{
	if (!spatial_audio_manager || !sim_started || !cars) {
		return;
	}
	const bool update_assignments = spatial_audio_last_assignment_tick != tick;
	if (update_assignments) {
		spatial_audio_last_assignment_tick = tick;
	}

	Engine* engine = Engine::get_singleton();
	const uint64_t process_frame = engine ? engine->get_process_frames() : spatial_audio_last_update_frame + 1;
	const bool update_time = spatial_audio_last_update_frame != process_frame;
	if (update_time) {
		spatial_audio_last_update_frame = process_frame;
	}
	spatial_audio_manager->update_from_gamesim(this, gameplay_camera_player_id, update_time ? delta : 0.0, update_assignments);
}

bool GameSim::play_car_oneshot_sfx(int car_index, const godot::StringName& sfx_id, double volume_db, double pitch_scale)
{
	if (!spatial_audio_manager || car_index < 0) {
		return false;
	}
	return spatial_audio_manager->play_vehicle_oneshot(car_index, sfx_id, volume_db, pitch_scale);
}

bool GameSim::play_player_oneshot_sfx(int player_id, const godot::StringName& sfx_id, double volume_db, double pitch_scale)
{
	const int car_index = find_car_index_for_player(player_id);
	if (car_index < 0) {
		return false;
	}
	return play_car_oneshot_sfx(car_index, sfx_id, volume_db, pitch_scale);
}

bool GameSim::play_world_oneshot_sfx(const godot::Vector3& position, const godot::StringName& sfx_id, double volume_db, double pitch_scale)
{
	if (!spatial_audio_manager) {
		return false;
	}
	return spatial_audio_manager->play_world_oneshot(position, sfx_id, volume_db, pitch_scale);
}

void GameSim::update_super_spark_visuals()
{
	if (!spark_node_container || !super_sparks)
		return;
	if (!spark_multimesh_instance) {
		Node *spark_node = spark_node_container->get_node_or_null(NodePath("SparkMultiMesh"));
		spark_multimesh_instance = Object::cast_to<godot::MultiMeshInstance3D>(spark_node);
		if (!spark_multimesh_instance) {
			return;
		}
	}
	Ref<godot::MultiMesh> spark_multimesh = spark_multimesh_instance->get_multimesh();
	if (spark_multimesh.is_null()) {
		return;
	}
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	int active_count = 0;
	for (int i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		if (super_sparks[i].active == 0) {
			continue;
		}
		godot::Transform3D spark_transform;
		const SimVec3 render_position = super_sparks[i].prev_position.lerp(super_sparks[i].position, alpha);
		spark_transform.origin = gd_vec3(render_position);
		spark_multimesh->set_instance_transform(active_count, spark_transform);
		active_count += 1;
	}
	spark_multimesh->set_visible_instance_count(active_count);
}

	void GameSim::update_render_snapshots() {
		if (!sim_started || !cars) {
			return;
		}
		update_render_visual_snapshots(std::max(0, num_cars));
	}

	void GameSim::snap_render_after_state_load() {
		if (!sim_started || !cars) {
			return;
		}
		const int visual_count = std::max(0, num_cars + (bumpers_enabled ? bumper_count : 0));
		render_rollback_corrections.assign(visual_count, SimTransform());
		render_rollback_correction_active.assign(visual_count, 0);
		render_rollback_capture_transforms.clear();
		render_rollback_capture_pending = false;
		update_render_visual_snapshots(visual_count);
	}

	void GameSim::render_gamesim() {
		if (!sim_started || !car_node_container || !cars) {
			return;
		}

		if (car_node_container == nullptr) {
			return;
		}

		const uint64_t profile_start = render_profile_enabled ? render_profile_now_us() : 0;
		uint64_t profile_step = profile_start;
		TypedArray<godot::Node> vis_cars = car_node_container->get_children();
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_get_children_us += now - profile_step;
			profile_step = now;
		}
		const int vis_car_count = std::max(0, num_cars);
		const int native_visual_count = std::max(vis_car_count + (bumpers_enabled ? bumper_count : 0),
				static_cast<int>(render_car_archetype_indices.size()));
		if (static_cast<int>(render_vehicle_effect_refs.size()) != vis_car_count ||
				render_effect_pool_slots.empty()) {
			cache_native_visual_effect_nodes();
		}
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_cache_us += now - profile_step;
			profile_step = now;
		}
		update_render_visual_snapshots(native_visual_count);
		step_collision_spark_effects();
		step_drift_plasma_effects();
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			const uint64_t elapsed = now - profile_step;
			render_profile_snapshots_us += elapsed;
			render_profile_snapshots_max_us = std::max(render_profile_snapshots_max_us, elapsed);
			profile_step = now;
		}
		Engine* engine = Engine::get_singleton();
		const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
		update_native_visual_effects(vis_car_count, alpha, true, 1.0f / 60.0f, false);
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			const uint64_t elapsed = now - profile_step;
			render_profile_effects_us += elapsed;
			render_profile_effects_max_us = std::max(render_profile_effects_max_us, elapsed);
			profile_step = now;
		}
		update_native_gameplay_camera(true);
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_camera_us += now - profile_step;
			profile_step = now;
		}
		godot::Array local_visual_args;
		local_visual_args.resize(54);
		for (int i = 0; i < vis_cars.size(); i++) {
			godot::Object *vis_car = Object::cast_to<godot::Object>(vis_cars[i]);
			if (vis_car && static_cast<bool>(vis_car->get("local_visual_enabled"))) {
				const int32_t owner_id = static_cast<int32_t>(static_cast<int64_t>(vis_car->get("owning_id")));
				int car_index = -1;
				for (int n = 0; n < num_cars; ++n) {
					if (car_player_ids && car_player_ids[n] == owner_id) {
						car_index = n;
						break;
					}
				}
				if (car_index < 0 || car_index >= num_cars) {
					continue;
				}
				populate_visual_car_args(local_visual_args, cars[car_index]);
				vis_car->callv("apply_sim_state", local_visual_args);
			}
		}
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_local_visual_us += now - profile_step;
			profile_step = now;
		}
		if (car_player_ids && car_is_cpu) {
			update_native_cpu_drivers();
		}
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			const uint64_t elapsed = now - profile_step;
			render_profile_cpu_driver_us += elapsed;
			render_profile_cpu_driver_max_us = std::max(render_profile_cpu_driver_max_us, elapsed);
			profile_step = now;
		}
		update_super_spark_visuals();
		if (render_profile_enabled) {
			const uint64_t now = render_profile_now_us();
			render_profile_spark_us += now - profile_step;
			const uint64_t elapsed = now - profile_start;
			render_profile_total_us += elapsed;
			render_profile_total_max_us = std::max(render_profile_total_max_us, elapsed);
			render_profile_frames += 1;
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_CHECKPOINTS))
		{
			for (int i = 0; i < current_track->num_checkpoints; i++)
			{
				current_track->checkpoints[i].debug_draw();
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_BRANCH_CENTERLINE))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			if (dd3d && num_cars > 0)
			{
				int cp_idx = cars[0].soa->current_checkpoint[cars[0].soa_index];
				if (cp_idx >= 0 && cp_idx < current_track->num_checkpoints)
				{
					std::vector<int> branch_indices;
					current_track->collect_branch_sequence(cp_idx, branch_indices);
					if (!branch_indices.empty())
					{
						for (size_t b = 0; b < branch_indices.size(); ++b)
						{
							int idx = branch_indices[b];
							if (idx < 0 || idx >= current_track->num_checkpoints)
							{
								continue;
							}
							const CollisionCheckpoint &cp = current_track->checkpoints[idx];
							dd3d->call("draw_line", gd_vec3(cp.position_start), gd_vec3(cp.position_end), godot::Color(1.0f, 0.9f, 0.1f), _TICK_DELTA);
							if (b + 1 < branch_indices.size())
							{
								int next_idx = branch_indices[b + 1];
								if (next_idx >= 0 && next_idx < current_track->num_checkpoints)
								{
									const CollisionCheckpoint &next_cp = current_track->checkpoints[next_idx];
									dd3d->call("draw_line", gd_vec3(cp.position_end), gd_vec3(next_cp.position_start), godot::Color(0.6f, 0.8f, 0.2f), _TICK_DELTA);
								}
							}
						}
					}
				}
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEG_BOUNDS))
		{
			godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");
			for (int i = 0; i < current_track->num_segments; i++)
			{
				dd3d->call("draw_aabb", gd_aabb(current_track->segments[i].bounds), godot::Color(1.0f, 0.0f, 1.0f, 0.1f), _TICK_DELTA);
			}
		}
		if (DEBUG::dip_enabled(DIP_SWITCH::DIP_DRAW_SEGMENT_SURF))
		{
		//DEBUG::disp_text("current checkpoint", cars[0].soa->current_checkpoint[cars[0].soa_index]);
			int use_seg_ind = current_track->checkpoints[cars[0].soa->current_checkpoint[cars[0].soa_index]].road_segment;
			for (int i = 0; i < current_track->num_segments; i++)
			{
				if (i > use_seg_ind + 1 || i < use_seg_ind - 1){
					continue;
				}
				godot::Object* dd3d = godot::Engine::get_singleton()->get_singleton("DebugDraw3D");

			const int x_subdiv = 16; // Adjust as needed
			const int y_subdiv = 32;  // Adjust as needed

			for (int yi = 0; yi <= y_subdiv; yi++)
			{
				float y_frac = static_cast<float>(yi) / y_subdiv;
				float y_val = y_frac; // Y: 0.0 to 1.0

				for (int xi = 0; xi <= x_subdiv; xi++)
				{
					float x_frac = static_cast<float>(xi) / x_subdiv;
					float x_val = -1.0f + 2.0f * x_frac; // X: -1.0 to +1.0

					// Interpolated color: red to blue across X, green from 0 to 1 across Y
					float r = 1.0f - x_frac;
					float g = y_frac;
					float b = x_frac;

					SimVec2 shape_pos(x_val, y_val);
					SimTransform road_transform;
					current_track->segments[i].road_shape->get_oriented_transform_at_time(road_transform, shape_pos);

					SimVec3 start = road_transform.origin;
					SimVec3 end = start + road_transform.basis[1] * 2.0f; // arrow in local Y/up

					dd3d->call("draw_arrow", gd_vec3(start), gd_vec3(end), godot::Color(r, g, b), 0.5, true, _TICK_DELTA);
				}
			}
		}
	}
}
