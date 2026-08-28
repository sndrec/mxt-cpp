class_name TrackPresentationController
extends Node

const DEFAULT_GROUND_TEXTURE: Texture2D = preload("res://asset/tex/cityscape.png")

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var debug_runtime_controller: DebugRuntimeController = get_node("../DebugRuntimeController") as DebugRuntimeController

var current_metadata: Dictionary = {}
var current_ground_image: Image
var current_track_dir := ""
var current_visual_path := ""
var current_mxt_path := ""
var current_visual_replaces_debug_environment := false
var visual_scene_instance: Node


func prepare_race(track_info: Dictionary) -> bool:
	if track_info.is_empty():
		return false
	current_metadata = {}
	current_ground_image = null
	_clear_visual_scene()
	game_manager.debug_track_mesh.mesh = null
	game_manager.obj_container.visible = !debug_runtime_controller.hide_track_visuals

	current_mxt_path = String(track_info.get("mxt", ""))
	var json_path := String(track_info.get("json", current_mxt_path.get_basename() + ".json"))
	current_track_dir = String(track_info.get("dir", json_path.get_base_dir()))
	if FileAccess.file_exists(json_path):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(json_path))
		if typeof(parsed) == TYPE_DICTIONARY:
			current_metadata = parsed

	current_visual_path = String(track_info.get("visual", ""))
	current_visual_replaces_debug_environment = _visual_replaces_debug_environment(current_visual_path)
	_set_builtin_visuals_enabled(!current_visual_replaces_debug_environment)
	var has_track_visual := !current_visual_path.is_empty()
	game_manager.debug_track_mesh.visible = !has_track_visual and !debug_runtime_controller.hide_track_visuals
	game_manager.track_floor.visible = !current_visual_replaces_debug_environment and !debug_runtime_controller.hide_track_visuals
	game_manager.track_clouds.visible = !current_visual_replaces_debug_environment and !debug_runtime_controller.hide_track_visuals
	_apply_environment()
	return true


func load_runtime_visuals() -> void:
	if current_mxt_path.is_empty():
		return
	if !_load_visual_scene(current_visual_path):
		_load_imported_mesh(current_mxt_path.get_basename() + ".obj")


func teardown_runtime() -> void:
	_clear_visual_scene()
	game_manager.debug_track_mesh.mesh = null
	_set_builtin_visuals_enabled(true)
	current_metadata = {}
	current_ground_image = null
	current_track_dir = ""
	current_visual_path = ""
	current_mxt_path = ""
	current_visual_replaces_debug_environment = false


func _clear_visual_scene() -> void:
	if visual_scene_instance != null:
		if is_instance_valid(visual_scene_instance):
			visual_scene_instance.queue_free()
		visual_scene_instance = null


func _visual_replaces_debug_environment(visual_path: String) -> bool:
	var extension := visual_path.get_extension().to_lower()
	return extension == "tscn" or extension == "scn"


func _track_material_for_name(material_name: String) -> Material:
	if material_name == "track_surface":
		return preload("res://asset/debug_track_mat.tres")
	if material_name == "track_rail":
		return preload("res://asset/debug_rail_mat.tres")
	if material_name == "embed_border":
		return preload("res://asset/embed_border_mat.tres")
	if material_name == "embed_dirt":
		return preload("res://asset/dirt_mat.tres")
	if material_name == "embed_recharge":
		return preload("res://asset/recharge_mat.tres")
	if material_name == "embed_ice":
		return preload("res://asset/ice_mat.tres")
	return null


func _apply_visual_materials_to_mesh_instance(mesh_instance: MeshInstance3D) -> void:
	var mesh := mesh_instance.mesh
	if mesh == null:
		return
	for surface_index in mesh.get_surface_count():
		var material := mesh.surface_get_material(surface_index)
		if material == null:
			continue
		var replacement := _track_material_for_name(material.resource_name)
		if replacement != null:
			mesh_instance.set_surface_override_material(surface_index, replacement)


func _apply_visual_materials(root: Node) -> void:
	if root is MeshInstance3D:
		_apply_visual_materials_to_mesh_instance(root as MeshInstance3D)
	for child in root.get_children():
		_apply_visual_materials(child)


func _load_resource_scene(scene_path: String) -> bool:
	var packed := ResourceLoader.load(scene_path) as PackedScene
	if packed == null:
		push_warning("Track visual scene is not a PackedScene: %s" % scene_path)
		return false
	var instance := packed.instantiate()
	if instance == null:
		push_warning("Failed to instantiate track visual scene: %s" % scene_path)
		return false
	visual_scene_instance = instance
	_apply_visual_materials(instance)
	game_manager.obj_container.add_child(instance)
	return true


func _load_gltf_scene(scene_path: String) -> bool:
	var gltf_document := GLTFDocument.new()
	var gltf_state := GLTFState.new()
	gltf_state.base_path = scene_path.get_base_dir()
	var error := gltf_document.append_from_file(scene_path, gltf_state)
	if error != OK:
		push_warning("Failed to load track glTF scene %s: %s" % [scene_path, error_string(error)])
		return false
	var instance := gltf_document.generate_scene(gltf_state)
	if instance == null:
		push_warning("Failed to generate track glTF scene: %s" % scene_path)
		return false
	visual_scene_instance = instance
	_apply_visual_materials(instance)
	game_manager.obj_container.add_child(instance)
	return true


func _load_visual_scene(scene_path: String) -> bool:
	if scene_path.is_empty() or !(FileAccess.file_exists(scene_path) or ResourceLoader.exists(scene_path)):
		return false
	var extension := scene_path.get_extension().to_lower()
	if extension == "glb" or extension == "gltf":
		return _load_gltf_scene(scene_path)
	return _load_resource_scene(scene_path)


func _load_imported_mesh(mesh_path: String) -> bool:
	if mesh_path.is_empty() or !ResourceLoader.exists(mesh_path):
		return false
	var loaded_mesh := ResourceLoader.load(mesh_path) as Mesh
	if loaded_mesh == null:
		return false
	game_manager.debug_track_mesh.mesh = loaded_mesh.duplicate(true)
	game_manager.lobby_control.visible = false
	_apply_visual_materials_to_mesh_instance(game_manager.debug_track_mesh)
	return true


func _set_builtin_visuals_enabled(enabled: bool) -> void:
	if game_manager.debug_track_mesh_container != null:
		game_manager.debug_track_mesh_container.visible = enabled and !debug_runtime_controller.hide_track_visuals
	if game_manager.directional_light_3d != null:
		game_manager.directional_light_3d.visible = enabled
	if game_manager.world_environment != null:
		game_manager.world_environment.environment = game_manager.default_world_environment_resource if enabled else null


func _apply_environment() -> void:
	if current_metadata.is_empty() or game_manager.world_environment.environment == null:
		return
	RenderingServer.global_shader_parameter_set("fog_dist", current_metadata.fog_distance)
	var floor_material := game_manager.track_floor.get_active_material(0) as ShaderMaterial
	var cloud_material := game_manager.track_clouds.get_active_material(0) as ShaderMaterial
	floor_material.set_shader_parameter("albedo", current_metadata.ground_color)
	cloud_material.set_shader_parameter("albedo", current_metadata.cloud_color)
	game_manager.track_floor.position.z = -current_metadata.ground_height
	game_manager.track_clouds.position.z = -current_metadata.cloud_height
	var sky_material := game_manager.world_environment.environment.sky.sky_material as ProceduralSkyMaterial
	sky_material.sky_top_color = Color(current_metadata.sky_top_color[0], current_metadata.sky_top_color[1], current_metadata.sky_top_color[2])
	sky_material.sky_horizon_color = Color(current_metadata.sky_horizon_color[0], current_metadata.sky_horizon_color[1], current_metadata.sky_horizon_color[2])
	sky_material.ground_horizon_color = Color(current_metadata.sky_horizon_color[0], current_metadata.sky_horizon_color[1], current_metadata.sky_horizon_color[2])
	sky_material.ground_bottom_color = Color(current_metadata.sky_ground_color[0], current_metadata.sky_ground_color[1], current_metadata.sky_ground_color[2])
	game_manager.directional_light_3d.light_color = Color(current_metadata.light_color[0], current_metadata.light_color[1], current_metadata.light_color[2])
	game_manager.directional_light_3d.light_energy = current_metadata.light_intensity
	game_manager.world_environment.environment.ambient_light_color = Color(current_metadata.ambient_color[0], current_metadata.ambient_color[1], current_metadata.ambient_color[2])
	game_manager.world_environment.environment.ambient_light_energy = current_metadata.ambient_intensity
	current_ground_image = null
	floor_material.set_shader_parameter("texture_albedo", DEFAULT_GROUND_TEXTURE)
	if current_visual_replaces_debug_environment:
		return
	var ground_path := current_track_dir.path_join("ground.png")
	if !FileAccess.file_exists(ground_path):
		return
	var loaded_ground_image := Image.load_from_file(ground_path)
	if loaded_ground_image == null or loaded_ground_image.is_empty():
		return
	current_ground_image = loaded_ground_image
	var floor_texture := ImageTexture.new()
	floor_texture.set_image(current_ground_image)
	floor_material.set_shader_parameter("texture_albedo", floor_texture)
