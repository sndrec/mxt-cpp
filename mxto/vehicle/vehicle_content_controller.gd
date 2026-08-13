class_name VehicleContentController
extends Node

signal workshop_content_changed(items: Array)
signal catalog_changed

const OFFICIAL_VEHICLE_PREFIX := "mxt:vehicle:official:"
const LOCAL_CONTENT_LIBRARY_PATH := "user://content/packages"
const TEST_DRIVE_SNAPSHOT_LIBRARY_PATH := "user://content/test_drive_snapshots"
const COMMUNITY_VEHICLE_SHADER: Shader = preload("res://vehicle/base_vehicle_shader.gdshader")
const COMMUNITY_VEHICLE_CROSS_HATCH: Texture2D = preload("res://asset/tex/crosshatch/1.png")

var content_catalog: MxtContentCatalog = MxtContentCatalog.new()
var definitions: Array = []
var definitions_by_content_id: Dictionary = {}
var workshop_content_items: Array = []
var steam_service: MxtSteamService
var runtime_content_loaded := false

func initialize(in_steam_service: MxtSteamService) -> void:
	steam_service = in_steam_service
	steam_service.workshop_items_changed.connect(_on_workshop_items_changed)
	_scan_local_content_library()
	_scan_test_drive_snapshot_library()
	_scan_trusted_verifier_workshop_packages()
	reload_definitions()
	runtime_content_loaded = true

func get_vehicle_content_ids() -> Array:
	var content_ids: Array = []
	for definition in definitions:
		if definition is CarDefinition and definition.content_id != "":
			content_ids.append(definition.content_id)
	return content_ids

func get_definition(vehicle_content_id: String) -> CarDefinition:
	var definition := definitions_by_content_id.get(vehicle_content_id) as CarDefinition
	if definition != null:
		return definition
	var record: Dictionary = content_catalog.resolve_content(vehicle_content_id)
	if String(record.get("source", "")) != "local_draft" or String(record.get("content_type", "")) != "vehicle":
		return null
	definition = _definition_from_package_record(record)
	if definition != null:
		definitions_by_content_id[vehicle_content_id] = definition
	return definition

func create_runtime_definition(record: Dictionary) -> CarDefinition:
	return _definition_from_package_record(record)

func get_evidence(vehicle_content_id: String) -> Dictionary:
	var record: Dictionary = content_catalog.resolve_content(vehicle_content_id)
	return {
		"vehicle_gameplay_digest": String(record.get("gameplay_digest", "")),
		"vehicle_package_digest": String(record.get("package_digest", "")),
		"vehicle_workshop_id": String(record.get("published_file_id", "")),
	}

func apply_evidence(settings: PlayerSettings) -> bool:
	if settings == null:
		return false
	var evidence := get_evidence(settings.vehicle_content_id)
	settings.vehicle_gameplay_digest = String(evidence.get("vehicle_gameplay_digest", ""))
	settings.vehicle_package_digest = String(evidence.get("vehicle_package_digest", ""))
	settings.vehicle_workshop_id = String(evidence.get("vehicle_workshop_id", ""))
	return !settings.vehicle_gameplay_digest.is_empty()

func evidence_matches(settings: PlayerSettings) -> bool:
	if settings == null:
		return false
	var record: Dictionary = content_catalog.resolve_content(settings.vehicle_content_id)
	return (
		!record.is_empty()
		and String(record.get("gameplay_digest", "")) == settings.vehicle_gameplay_digest
		and String(record.get("package_digest", "")) == settings.vehicle_package_digest
		and String(record.get("published_file_id", "")) == settings.vehicle_workshop_id)

func reload_definitions() -> void:
	definitions.clear()
	definitions_by_content_id.clear()
	var directory := DirAccess.open("res://vehicle/asset")
	if directory == null:
		return
	directory.list_dir_begin()
	var folder := directory.get_next()
	while folder != "":
		if directory.current_is_dir() and !folder.begins_with(".") and folder != "bumper":
			_register_official_definition("res://vehicle/asset/%s/definition.tres" % folder)
		folder = directory.get_next()
	directory.list_dir_end()
	_load_packaged_definitions()
	definitions.sort_custom(func(a: CarDefinition, b: CarDefinition): return a.content_id < b.content_id)

func _register_official_definition(definition_path: String) -> void:
	if !ResourceLoader.exists(definition_path):
		return
	var definition := load(definition_path) as CarDefinition
	if definition == null:
		push_error("Invalid vehicle definition resource: %s" % definition_path)
	elif definition.content_id == "":
		push_error("Vehicle definition has no content ID: %s" % definition_path)
	elif !definition.content_id.begins_with(OFFICIAL_VEHICLE_PREFIX):
		push_error("Selectable built-in vehicle has invalid official content ID: %s" % definition.content_id)
	elif definitions_by_content_id.has(definition.content_id):
		push_error("Duplicate vehicle content ID: %s" % definition.content_id)
	elif !FileAccess.file_exists(definition.properties_path):
		push_error("Vehicle properties file is missing: %s" % definition.properties_path)
	else:
		var catalog_result: Dictionary = content_catalog.add_official_vehicle(
			definition.content_id.trim_prefix(OFFICIAL_VEHICLE_PREFIX),
			definition.name,
			definition.properties_path,
			definition.resource_path)
		if !bool(catalog_result.get("valid", false)):
			push_error("Official vehicle catalog registration failed for %s: %s" % [definition.content_id, str(catalog_result.get("errors", []))])
		else:
			definitions.append(definition)
			definitions_by_content_id[definition.content_id] = definition

func refresh_installed_content() -> void:
	_scan_local_content_library()
	_scan_test_drive_snapshot_library()
	reload_definitions()
	catalog_changed.emit()

func refresh_workshop_content() -> bool:
	return steam_service != null and steam_service.refresh_subscribed_workshop_items()

func get_workshop_content_items() -> Array:
	return workshop_content_items.duplicate(true)

func _scan_local_content_library() -> void:
	var library_path := ProjectSettings.globalize_path(LOCAL_CONTENT_LIBRARY_PATH)
	var directory_error := DirAccess.make_dir_recursive_absolute(library_path)
	if directory_error != OK:
		push_error("Could not create the local content library: %s" % error_string(directory_error))
		return
	var result: Dictionary = content_catalog.scan_local_library(library_path)
	for diagnostic_value in result.get("diagnostics", []):
		var diagnostic: Dictionary = diagnostic_value
		push_warning("Skipped local content package %s: %s" % [String(diagnostic.get("path", "")), str(diagnostic.get("errors", []))])

func _scan_test_drive_snapshot_library() -> void:
	var library_path := ProjectSettings.globalize_path(TEST_DRIVE_SNAPSHOT_LIBRARY_PATH)
	if DirAccess.make_dir_recursive_absolute(library_path) != OK:
		push_error("Could not create the test-drive snapshot library")
		return
	var directory := DirAccess.open(library_path)
	if directory == null:
		return
	directory.list_dir_begin()
	var folder := directory.get_next()
	while !folder.is_empty():
		if directory.current_is_dir() and !directory.is_link(folder) and !folder.begins_with("."):
			var result: Dictionary = content_catalog.add_draft_package(library_path.path_join(folder))
			if !bool(result.get("valid", false)):
				push_warning("Skipped test-drive snapshot %s: %s" % [folder, str(result.get("errors", []))])
		folder = directory.get_next()
	directory.list_dir_end()

func _scan_trusted_verifier_workshop_packages() -> void:
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	if !(args.has("--leaderboard-replay-verify") or user_args.has("--leaderboard-replay-verify")):
		return
	for source_args in [args, user_args]:
		var index := 0
		while index < source_args.size():
			if String(source_args[index]) != "--trusted-workshop-package":
				index += 1
				continue
			if index + 2 >= source_args.size():
				push_error("--trusted-workshop-package requires a Workshop ID and package path")
				return
			var published_file_id := int(source_args[index + 1])
			var package_path := String(source_args[index + 2])
			var result: Dictionary = content_catalog.add_workshop_package(package_path, published_file_id)
			if !bool(result.get("valid", false)):
				push_error("Trusted verifier package %d failed validation: %s" % [published_file_id, str(result.get("errors", []))])
			index += 3

func _on_workshop_items_changed(items: Array) -> void:
	content_catalog.clear_workshop_packages()
	workshop_content_items.clear()
	for value in items:
		var item: Dictionary = value.duplicate(true)
		if String(item.get("status", "")) == "installed" and !bool(item.get("needs_update", false)):
			var registered: Dictionary = content_catalog.add_workshop_package(String(item.get("install_path", "")), int(item.get("published_file_id", 0)))
			if bool(registered.get("valid", false)):
				item["status"] = "ready"
				item["record"] = registered.get("record", {})
			else:
				var errors = registered.get("errors", [])
				item["status"] = "outdated_format" if str(errors).contains("format revision") else "invalid"
				item["errors"] = errors
		workshop_content_items.append(item)
	if runtime_content_loaded:
		reload_definitions()
		catalog_changed.emit()
	workshop_content_changed.emit(get_workshop_content_items())

func _load_packaged_definitions() -> void:
	for record_value in content_catalog.get_records("vehicle"):
		var record: Dictionary = record_value
		var source := String(record.get("source", ""))
		if source == "official" or source == "local_draft":
			continue
		var definition := _definition_from_package_record(record)
		if definition == null:
			continue
		if definitions_by_content_id.has(definition.content_id):
			push_error("Duplicate packaged vehicle content ID: %s" % definition.content_id)
			continue
		definitions.append(definition)
		definitions_by_content_id[definition.content_id] = definition

func _definition_from_package_record(record: Dictionary) -> CarDefinition:
	var visual_path := String(record.get("visual_path", ""))
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
	var visual_metadata: Dictionary = record.get("visual_metadata", {})
	var runtime_mesh := _build_body_mesh(mesh_instance.mesh, visual_metadata.get("body_surfaces", []))
	if runtime_mesh == null:
		push_error("Packaged vehicle has no selected runtime body surfaces: %s" % visual_path)
		instance.free()
		return null
	var definition := CarDefinition.new()
	definition.name = String(record.get("title", "Vehicle"))
	definition.content_id = String(record.get("content_id", ""))
	definition.properties_path = String(record.get("authoritative_path", ""))
	definition.runtime_mesh = runtime_mesh
	definition.runtime_material = _build_material(mesh_instance.mesh, visual_metadata.get("material_inputs", {}))
	definition.runtime_transform = _transform_from_metadata(visual_metadata.get("model_transform", {})) * mesh_data["transform"]
	for thruster_value in visual_metadata.get("thrusters", []):
		definition.runtime_thruster_transforms.append(_thruster_transform_from_metadata(thruster_value))
	instance.free()
	return definition

func _build_body_mesh(source: Mesh, selected_surfaces: Array) -> ArrayMesh:
	if source == null or selected_surfaces.is_empty():
		return null
	var body := ArrayMesh.new()
	for surface_value in selected_surfaces:
		var surface := int(surface_value)
		if surface < 0 or surface >= source.get_surface_count():
			return null
		body.add_surface_from_arrays(source.surface_get_primitive_type(surface), source.surface_get_arrays(surface))
	return body

func _build_material(source: Mesh, inputs: Dictionary) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = COMMUNITY_VEHICLE_SHADER
	material.set_shader_parameter("in_lightwarp", _community_lightwarp())
	material.set_shader_parameter("in_specwarp", _community_specwarp())
	material.set_shader_parameter("cross_hatch", COMMUNITY_VEHICLE_CROSS_HATCH)
	material.set_shader_parameter("in_overlay_colour", Color.BLACK)
	material.set_shader_parameter("in_albedo", _texture(source, int(inputs.get("albedo_surface", -1)), "albedo_texture", Color.WHITE))
	material.set_shader_parameter("in_normal", _texture(source, int(inputs.get("normal_surface", -1)), "normal_texture", Color(0.5, 0.5, 1.0, 1.0)))
	material.set_shader_parameter("in_paint_mask", _texture(source, int(inputs.get("paint_mask_surface", -1)), "ao_texture", Color.BLACK))
	material.set_shader_parameter("livery_colour_strength", 1.0)
	return material

func _texture(source: Mesh, surface: int, property: StringName, fallback: Color) -> Texture2D:
	if source != null and surface >= 0 and surface < source.get_surface_count():
		var source_material := source.surface_get_material(surface)
		if source_material != null:
			var candidate = source_material.get(property)
			if candidate is Texture2D:
				return candidate
	var image := Image.create(1, 1, false, Image.FORMAT_RGBA8)
	image.fill(fallback)
	return ImageTexture.create_from_image(image)

func _community_lightwarp() -> GradientTexture1D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CONSTANT
	gradient.offsets = PackedFloat32Array([0.0, 0.318519, 0.788889, 0.979532])
	gradient.colors = PackedColorArray([Color(0.1, 0.1, 0.1), Color.BLACK, Color(0.521, 0.521, 0.521), Color.WHITE])
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	return texture

func _community_specwarp() -> GradientTexture1D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CONSTANT
	gradient.offsets = PackedFloat32Array([0.151852, 0.925926])
	gradient.colors = PackedColorArray([Color.BLACK, Color.WHITE])
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	return texture

func _transform_from_metadata(value: Dictionary) -> Transform3D:
	var degrees: Vector3 = value.get("rotation_degrees", Vector3.ZERO)
	var rotation := Vector3(deg_to_rad(degrees.x), deg_to_rad(degrees.y), deg_to_rad(degrees.z))
	var scale_value: Vector3 = value.get("scale", Vector3.ONE)
	return Transform3D(Basis.from_euler(rotation).scaled(scale_value), value.get("translation", Vector3.ZERO))

func _thruster_transform_from_metadata(value: Dictionary) -> Transform3D:
	var degrees: Vector3 = value.get("rotation_degrees", Vector3.ZERO)
	var rotation := Vector3(deg_to_rad(degrees.x), deg_to_rad(degrees.y), deg_to_rad(degrees.z))
	return Transform3D(Basis.from_euler(rotation).scaled(Vector3.ONE * float(value.get("scale", 1.0))), value.get("position", Vector3.ZERO))

func _find_mesh(node: Node, parent_transform: Transform3D) -> Dictionary:
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
