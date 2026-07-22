#include "track/finish_line_display.h"

#include "godot_cpp/classes/cylinder_mesh.hpp"
#include "godot_cpp/classes/mesh_instance3d.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/classes/packed_scene.hpp"
#include "godot_cpp/classes/plane_mesh.hpp"
#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/resource_loader.hpp"
#include "godot_cpp/classes/shader_material.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "track/racetrack.h"

#include <algorithm>

namespace godot {

static constexpr float FINISH_LINE_HEIGHT = 4.0f;

static Vector3 finish_line_vec3(const SimVec3& value)
{
	return Vector3(value.x, value.y, value.z);
}

static Transform3D finish_line_transform(const SimTransform& value)
{
	Basis basis;
	basis.set_column(0, finish_line_vec3(value.basis.c0));
	basis.set_column(1, finish_line_vec3(value.basis.c1));
	basis.set_column(2, finish_line_vec3(value.basis.c2));
	return Transform3D(basis, finish_line_vec3(value.origin));
}

void FinishLineDisplay::create(Node* parent)
{
	if (root || !parent) {
		return;
	}

	ResourceLoader* loader = ResourceLoader::get_singleton();
	if (!loader) {
		return;
	}
	Ref<Resource> resource = loader->load(
		"res://asset/finish_line_display.tscn",
		"PackedScene");
	Ref<PackedScene> scene = resource;
	if (scene.is_null()) {
		UtilityFunctions::push_warning("MXT finish-line scene failed to load");
		return;
	}

	Node* instance = scene->instantiate();
	root = Object::cast_to<Node3D>(instance);
	if (!root) {
		if (instance) {
			instance->queue_free();
		}
		UtilityFunctions::push_warning("MXT finish-line scene root is not Node3D");
		return;
	}
	parent->add_child(root);

	corner_1 = Object::cast_to<MeshInstance3D>(root->get_node_or_null("Corner1"));
	corner_2 = Object::cast_to<MeshInstance3D>(root->get_node_or_null("Corner2"));
	plane = Object::cast_to<MeshInstance3D>(root->get_node_or_null("FinishLinePlane"));
	if (!corner_1 || !corner_2 || !plane) {
		root->queue_free();
		root = nullptr;
		corner_1 = nullptr;
		corner_2 = nullptr;
		plane = nullptr;
		UtilityFunctions::push_warning("MXT finish-line scene is missing required mesh nodes");
		return;
	}
	root->set_visible(false);
}

void FinishLineDisplay::configure(Node* parent, const RaceTrack& track)
{
	if (configured_track == &track && root && root->is_visible()) {
		return;
	}
	if (track.num_checkpoints <= 0 || !track.checkpoints) {
		hide();
		return;
	}

	create(parent);
	if (!root || !corner_1 || !corner_2 || !plane) {
		return;
	}

	const int lap_checkpoint_index =
		track.canonical_start_index >= 0 &&
		track.canonical_start_index < track.num_checkpoints
			? track.canonical_start_index
			: 0;
	const CollisionCheckpoint& lap_checkpoint = track.checkpoints[lap_checkpoint_index];

	SimTransform surface(lap_checkpoint.orientation_start, lap_checkpoint.position_start);
	if (lap_checkpoint.road_segment >= 0 &&
			lap_checkpoint.road_segment < track.num_segments &&
			track.segments &&
			track.segments[lap_checkpoint.road_segment].road_shape) {
		track.segments[lap_checkpoint.road_segment].road_shape->get_oriented_transform_at_time(
			surface,
			SimVec2(0.0f, lap_checkpoint.t_start));
	}
	surface.basis.orthonormalize();
	surface.origin += surface.basis.get_column(1) * (FINISH_LINE_HEIGHT * 0.5f);

	const float width = std::max(0.25f, lap_checkpoint.x_radius_start * 2.0f);
	root->set_transform(finish_line_transform(surface));
	corner_1->set_position(Vector3(width * 0.5f, 0.0f, 0.0f));
	corner_2->set_position(Vector3(width * -0.5f, 0.0f, 0.0f));

	Ref<CylinderMesh> corner_1_mesh = corner_1->get_mesh();
	Ref<CylinderMesh> corner_2_mesh = corner_2->get_mesh();
	if (corner_1_mesh.is_valid()) {
		corner_1_mesh->set_height(FINISH_LINE_HEIGHT);
	}
	if (corner_2_mesh.is_valid()) {
		corner_2_mesh->set_height(FINISH_LINE_HEIGHT);
	}

	Ref<PlaneMesh> plane_mesh = plane->get_mesh();
	if (plane_mesh.is_valid()) {
		plane_mesh->set_size(Vector2(width, FINISH_LINE_HEIGHT));
	}
	Ref<ShaderMaterial> plane_material = plane->get_active_material(0);
	if (plane_material.is_valid()) {
		plane_material->set_shader_parameter(
			"finishline_size",
			Vector2(width, FINISH_LINE_HEIGHT));
	}

	configured_track = &track;
	root->set_visible(true);
}

void FinishLineDisplay::hide()
{
	configured_track = nullptr;
	if (root) {
		root->set_visible(false);
	}
}

}
