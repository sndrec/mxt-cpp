class_name VehicleRuntimeAssetFactory
extends RefCounted

const COMMUNITY_VEHICLE_SHADER: Shader = preload("res://vehicle/base_vehicle_shader.gdshader")
const COMMUNITY_VEHICLE_CROSS_HATCH: Texture2D = preload("res://asset/tex/crosshatch/1.png")


static func create_from_package_record(record: MxtContentRecord) -> CarDefinition:
	return _create_from_fields(
		record.content_id, record.title, record.authoritative_path, record.visual_path,
		record.visual_metadata, record.manual_boost_sfx_path,
		record.albedo_texture_path, record.normal_texture_path, record.paint_mask_texture_path)


static func create_from_draft_record(record: Dictionary) -> CarDefinition:
	return _create_from_fields(
		String(record.get("content_id", "")), String(record.get("title", "Vehicle")),
		String(record.get("authoritative_path", "")), String(record.get("visual_path", "")),
		record.get("visual_metadata", {}) as Dictionary, String(record.get("manual_boost_sfx_path", "")),
		String(record.get("albedo_texture_path", "")), String(record.get("normal_texture_path", "")),
		String(record.get("paint_mask_texture_path", "")))


static func _create_from_fields(
		content_id: String, title: String, authoritative_path: String, visual_path: String,
		visual_metadata: Dictionary, manual_boost_sfx_path: String, albedo_texture_path: String,
		normal_texture_path: String, paint_mask_texture_path: String) -> CarDefinition:
	var gltf_document := GLTFDocument.new()
	var gltf_state := GLTFState.new()
	gltf_state.base_path = visual_path.get_base_dir()
	var error := gltf_document.append_from_file(visual_path, gltf_state)
	if error != OK:
		push_error("Could not load packaged vehicle visual %s: %s" % [visual_path, error_string(error)])
		return null
	var instance := gltf_document.generate_scene(gltf_state) as Node3D
	if instance == null:
		push_error("Could not generate packaged vehicle visual: %s" % visual_path)
		return null
	var mesh_data := _find_mesh(instance, Transform3D.IDENTITY)
	if mesh_data.is_empty():
		push_error("Packaged vehicle visual has no runtime mesh: %s" % visual_path)
		instance.free()
		return null
	var mesh_instance: MeshInstance3D = mesh_data["instance"]
	var runtime_mesh := _build_body_mesh(mesh_instance.mesh, visual_metadata.get("body_surfaces", []))
	if runtime_mesh == null:
		push_error("Packaged vehicle has no selected runtime body surfaces: %s" % visual_path)
		instance.free()
		return null
	var definition := CarDefinition.new()
	definition.name = title
	definition.content_id = content_id
	definition.properties_path = authoritative_path
	definition.runtime_mesh = runtime_mesh
	definition.runtime_material = _build_material(
		mesh_instance.mesh,
		visual_metadata.get("material_inputs", {}),
		albedo_texture_path,
		normal_texture_path,
		paint_mask_texture_path)
	definition.runtime_transform = _transform_from_metadata(
		visual_metadata.get("model_transform", {})) * mesh_data["transform"]
	definition.manual_boost_sfx = _load_packaged_boost_sfx(manual_boost_sfx_path)
	definition.manual_boost_volume_db = clampf(
		float(visual_metadata.get("manual_boost_volume_db", 0.0)), -20.0, 20.0)
	for thruster_value in visual_metadata.get("thrusters", []):
		definition.runtime_thruster_transforms.append(_thruster_transform_from_metadata(thruster_value))
	instance.free()
	return definition


static func _load_packaged_boost_sfx(path: String) -> AudioStream:
	if path.is_empty() or !FileAccess.file_exists(path):
		return null
	if path.to_lower().ends_with(".wav"):
		return AudioStreamWAV.load_from_file(path)
	if path.to_lower().ends_with(".ogg"):
		return AudioStreamOggVorbis.load_from_file(path)
	return null


static func _build_body_mesh(source: Mesh, selected_surfaces: Array) -> ArrayMesh:
	if source == null or selected_surfaces.is_empty():
		return null
	var body := ArrayMesh.new()
	for surface_value in selected_surfaces:
		var surface := int(surface_value)
		if surface < 0 or surface >= source.get_surface_count():
			return null
		body.add_surface_from_arrays(
			source.surface_get_primitive_type(surface),
			source.surface_get_arrays(surface))
	return body


static func _build_material(
		source: Mesh, inputs: Dictionary, albedo_path: String, normal_path: String,
		paint_mask_path: String) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = COMMUNITY_VEHICLE_SHADER
	material.set_shader_parameter("in_lightwarp", _community_lightwarp())
	material.set_shader_parameter("in_specwarp", _community_specwarp())
	material.set_shader_parameter("cross_hatch", COMMUNITY_VEHICLE_CROSS_HATCH)
	material.set_shader_parameter("in_overlay_colour", Color.BLACK)
	# Revision-1 Workshop packages predate standalone PNG payloads. Missing paths must
	# permanently fall back to their embedded GLB textures so published cars keep rendering.
	var albedo_override := _load_packaged_texture(albedo_path)
	var normal_override := _load_packaged_texture(normal_path)
	var paint_mask_override := _load_packaged_texture(paint_mask_path)
	material.set_shader_parameter(
		"in_albedo",
		albedo_override if albedo_override != null else _texture(
			source, int(inputs.get("albedo_surface", -1)), "albedo_texture", Color.WHITE))
	material.set_shader_parameter(
		"in_normal",
		normal_override if normal_override != null else _texture(
			source, int(inputs.get("normal_surface", -1)), "normal_texture", Color(0.5, 0.5, 1.0, 1.0)))
	material.set_shader_parameter(
		"in_paint_mask",
		paint_mask_override if paint_mask_override != null else _texture(
			source, int(inputs.get("paint_mask_surface", -1)), "ao_texture", Color.BLACK))
	material.set_shader_parameter("use_mesh_normals", bool(inputs.get("use_mesh_normals", false)))
	material.set_shader_parameter("livery_colour_strength", 1.0)
	return material


static func _load_packaged_texture(path: String) -> Texture2D:
	if path.is_empty() or !FileAccess.file_exists(path):
		return null
	var image := Image.load_from_file(path)
	if image == null or image.is_empty():
		return null
	return ImageTexture.create_from_image(image)


static func _texture(source: Mesh, surface: int, property: StringName, fallback: Color) -> Texture2D:
	if source != null and surface >= 0 and surface < source.get_surface_count():
		var source_material := source.surface_get_material(surface)
		if source_material != null:
			var candidate = source_material.get(property)
			if candidate is Texture2D:
				return candidate
	var image := Image.create(1, 1, false, Image.FORMAT_RGBA8)
	image.fill(fallback)
	return ImageTexture.create_from_image(image)


static func _community_lightwarp() -> GradientTexture1D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CONSTANT
	gradient.offsets = PackedFloat32Array([0.0, 0.318519, 0.788889, 0.979532])
	gradient.colors = PackedColorArray([
		Color(0.1, 0.1, 0.1), Color.BLACK, Color(0.521, 0.521, 0.521), Color.WHITE])
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	return texture


static func _community_specwarp() -> GradientTexture1D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CONSTANT
	gradient.offsets = PackedFloat32Array([0.151852, 0.925926])
	gradient.colors = PackedColorArray([Color.BLACK, Color.WHITE])
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	return texture


static func _transform_from_metadata(value: Dictionary) -> Transform3D:
	var degrees: Vector3 = value.get("rotation_degrees", Vector3.ZERO)
	var rotation := Vector3(deg_to_rad(degrees.x), deg_to_rad(degrees.y), deg_to_rad(degrees.z))
	var scale_value: Vector3 = value.get("scale", Vector3.ONE)
	return Transform3D(
		Basis.from_euler(rotation).scaled(scale_value),
		value.get("translation", Vector3.ZERO))


static func _thruster_transform_from_metadata(value: Dictionary) -> Transform3D:
	var degrees: Vector3 = value.get("rotation_degrees", Vector3.ZERO)
	var rotation := Vector3(deg_to_rad(degrees.x), deg_to_rad(degrees.y), deg_to_rad(degrees.z))
	return Transform3D(
		Basis.from_euler(rotation).scaled(Vector3.ONE * float(value.get("scale", 1.0))),
		value.get("position", Vector3.ZERO))


static func _find_mesh(node: Node, parent_transform: Transform3D) -> Dictionary:
	var local_transform := parent_transform
	var node_3d := node as Node3D
	if node_3d != null:
		local_transform *= node_3d.transform
	var mesh_instance := node as MeshInstance3D
	if mesh_instance != null and mesh_instance.mesh != null:
		return {"instance": mesh_instance, "transform": local_transform}
	for child in node.get_children():
		var found := _find_mesh(child, local_transform)
		if !found.is_empty():
			return found
	return {}
