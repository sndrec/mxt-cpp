#include "gamesim/gamesim_internal.h"

#include "track/minimap_mesh_builder.h"

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/viewport.hpp"

#include <algorithm>

using namespace godot;

Ref<ArrayMesh> GameSim::get_minimap_mesh()
{
	if (minimap_mesh.is_valid() || current_track == nullptr) {
		return minimap_mesh;
	}
	minimap_mesh = build_track_minimap_mesh(*current_track);
	return minimap_mesh;
}

void GameSim::update_minimap_markers(
	const Ref<MultiMesh>& markers,
	Camera3D* camera,
	int focus_player_id) const
{
	if (markers.is_null() || camera == nullptr || !cars || !car_player_ids || num_cars <= 0) {
		if (markers.is_valid()) {
			markers->set_visible_instance_count(0);
		}
		return;
	}
	if (markers->get_instance_count() != num_cars) {
		markers->set_instance_count(num_cars);
	}

	Viewport* viewport = camera->get_viewport();
	const Vector2 viewport_size = viewport ? viewport->get_visible_rect().size : Vector2();
	Engine* engine = Engine::get_singleton();
	const float alpha = engine ? static_cast<float>(engine->get_physics_interpolation_fraction()) : 1.0f;
	const Color other_color(1.0f, 1.0f, 1.0f, 1.0f);
	const Color focus_color(1.0f, 0.8f, 0.1f, 1.0f);
	const Transform2D hidden_transform(0.0f, Vector2(-10000.0f, -10000.0f));

	for (int index = 0; index < num_cars; ++index) {
		Vector3 position;
		if (index < static_cast<int>(render_final_prev_transforms.size()) &&
				index < static_cast<int>(render_final_current_transforms.size())) {
			position = gd_vec3(interpolate_sim_transform(
				render_final_prev_transforms[index],
				render_final_current_transforms[index],
				alpha).origin);
		} else {
			const PhysicsCarSoA& soa = *cars[index].soa;
			const int lane = cars[index].soa_index;
			position = gd_vec3(LOAD_INDEXED_VEC3(soa, position_old, lane).lerp(
				LOAD_INDEXED_VEC3(soa, position_current, lane), alpha));
		}

		if (!camera->is_position_in_frustum(position)) {
			markers->set_instance_transform_2d(index, hidden_transform);
			markers->set_instance_color(index, Color(1.0f, 1.0f, 1.0f, 0.0f));
			continue;
		}
		const Vector2 screen_position = camera->unproject_position(position);
		if (screen_position.x < -8.0f || screen_position.y < -8.0f ||
				screen_position.x > viewport_size.x + 8.0f ||
				screen_position.y > viewport_size.y + 8.0f) {
			markers->set_instance_transform_2d(index, hidden_transform);
			markers->set_instance_color(index, Color(1.0f, 1.0f, 1.0f, 0.0f));
			continue;
		}
		markers->set_instance_transform_2d(index, Transform2D(0.0f, screen_position));
		markers->set_instance_color(index, car_player_ids[index] == focus_player_id ? focus_color : other_color);
	}
	markers->set_visible_instance_count(num_cars);
}
