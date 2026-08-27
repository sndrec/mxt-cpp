class_name CarRenderManager extends Node3D

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")

const PASS_MAIN := "main"
const PASS_OUTLINE := "outline"
const PASS_OUTLINE_MAIN := "outline_main"
const PASS_STAMP := "stamp"
const STAMP_CATALOG_PATH := "res://vehicle/customization/stamp_catalog.tres"
const OUTLINE_SHADER: Shader = preload("res://vehicle/vehicle_outline.gdshader")
const OUTLINE_MAIN_SHADER: Shader = preload("res://vehicle/vehicle_outline_main.gdshader")
const SHADOW_SHADER: Shader = preload("res://vehicle/vehicle_shadow.gdshader")
const THRUSTER_SCENE: PackedScene = preload("res://vehicle/particle/thruster.tscn")
const HIDDEN_INSTANCE_TRANSFORM := Transform3D(Basis.IDENTITY, Vector3(0.0, -100000.0, 0.0))

var archetypes: Array = []
var car_archetype_indices: PackedInt32Array = PackedInt32Array()
var car_slots: PackedInt32Array = PackedInt32Array()
var multimesh_render_enabled: bool = true
var stamp_only_mode: bool = false
var stamp_render_priority: int = 2
var stamp_visibility_masks_enabled: bool = true
var stamp_visibility_mask_skip_layer: int = -1
var stamp_catalog: CarStampCatalog = null
var custom_stamp_atlas_texture: Texture2D = null
var stamp_mesh_builder: NativeStampMeshBuilder = NativeStampMeshBuilder.new()
var force_unique_archetypes := false
var _cached_input_revisions: Array = []
var _cached_definition_instances: Array = []
var _cached_liveries: Array = []
var _cached_archetype_keys: Array = []
var _prepared_stamp_builds: Dictionary = {}
var _active_archetype_key := ""

func _exit_tree() -> void:
	archetypes.clear()
	car_archetype_indices = PackedInt32Array()
	car_slots = PackedInt32Array()
	_clear_render_input_cache()

func clear_renderer() -> void:
	archetypes.clear()
	car_archetype_indices = PackedInt32Array()
	car_slots = PackedInt32Array()
	_clear_render_input_cache()
	for child in get_children():
		child.queue_free()

func configure(definitions: Array, player_settings: Array = []) -> void:
	clear_renderer()
	force_unique_archetypes = false
	_configure_archetypes(definitions, player_settings)

func configure_manual(definitions: Array, player_settings: Array = [], unique_archetypes := false) -> void:
	clear_renderer()
	force_unique_archetypes = unique_archetypes
	_configure_archetypes(definitions, player_settings)

func reconfigure_manual(definitions: Array, player_settings: Array = [], input_revisions: Array = []) -> void:
	_reconfigure_archetypes(definitions, player_settings, input_revisions)


func create_manual_reconfigure_preparation(definitions: Array, player_settings: Array = [], input_revisions: Array = []) -> Dictionary:
	var resolved := _resolve_reconfigure_inputs(definitions, player_settings, input_revisions)
	var reusable := {}
	for archetype in archetypes:
		var existing_key := String(archetype.get("key", ""))
		if !existing_key.is_empty():
			reusable[existing_key] = true
	var snapshots := {}
	var keys: Array = resolved.get("keys", [])
	var liveries: Array = resolved.get("liveries", [])
	for i in range(mini(definitions.size(), keys.size())):
		var key := String(keys[i])
		if key.is_empty() or reusable.has(key) or snapshots.has(key):
			continue
		var livery := liveries[i] as CarLivery
		if livery == null or livery.stamps.is_empty():
			continue
		var snapshot := _snapshot_stamp_build(definitions[i] as CarDefinition, livery)
		if !snapshot.is_empty():
			snapshots[key] = snapshot
	resolved["snapshots"] = snapshots
	return resolved


func apply_manual_reconfigure_preparation(definitions: Array, player_settings: Array, input_revisions: Array, preparation: Dictionary, prepared_stamp_builds: Dictionary) -> void:
	_prepared_stamp_builds = prepared_stamp_builds
	_reconfigure_archetypes(definitions, player_settings, input_revisions, preparation)
	_prepared_stamp_builds = {}


func _clear_render_input_cache() -> void:
	_cached_input_revisions.clear()
	_cached_definition_instances.clear()
	_cached_liveries.clear()
	_cached_archetype_keys.clear()


func set_custom_stamp_atlas(texture: Texture2D) -> void:
	custom_stamp_atlas_texture = texture
	_update_stamp_material_custom_atlases()

func update_livery_colours(livery: CarLivery) -> void:
	if livery == null:
		return
	for archetype in archetypes:
		_apply_livery_to_pass(archetype, PASS_MAIN, livery)
		_apply_outline_livery_to_pass(archetype, PASS_OUTLINE_MAIN, livery, false)
		_apply_outline_livery_to_pass(archetype, PASS_OUTLINE, livery, true)

func set_material_diagnostic(mode: int) -> void:
	for archetype in archetypes:
		if !archetype.has(PASS_MAIN):
			continue
		var pass_data: Dictionary = archetype[PASS_MAIN]
		var node := pass_data.get("node", null) as MultiMeshInstance3D
		if node == null:
			continue
		var material := node.material_override as ShaderMaterial
		if material != null:
			material.set_shader_parameter("diagnostic_mode", clampi(mode, 0, 4))

func _configure_archetypes(definitions: Array, player_settings: Array = []) -> void:
	car_archetype_indices.resize(definitions.size())
	car_slots.resize(definitions.size())
	if !multimesh_render_enabled:
		for i in range(definitions.size()):
			car_archetype_indices[i] = -1
			car_slots[i] = -1
		return
	var archetype_map := {}
	for i in range(definitions.size()):
		var def: CarDefinition = definitions[i]
		var livery := _livery_for_index(i, def, player_settings)
		var key := _definition_key(def, livery)
		if force_unique_archetypes:
			key += ":manual_instance_%d" % i
		var archetype_index := -1
		if archetype_map.has(key):
			archetype_index = archetype_map[key]
		else:
			archetype_index = archetypes.size()
			archetype_map[key] = archetype_index
			_active_archetype_key = key
			archetypes.append(_build_archetype(def, livery, key))
			_active_archetype_key = ""
		var archetype: Dictionary = archetypes[archetype_index]
		var count: int = archetype["count"]
		car_archetype_indices[i] = archetype_index
		car_slots[i] = count
		archetype["indices"].append(i)
		archetype["count"] = count + 1
		archetypes[archetype_index] = archetype
	for archetype in archetypes:
		_resize_passes(archetype, int(archetype["count"]))

func _resolve_reconfigure_inputs(definitions: Array, player_settings: Array, input_revisions: Array) -> Dictionary:
	var next_input_revisions: Array = []
	var next_definition_instances: Array = []
	var next_liveries: Array = []
	var next_archetype_keys: Array = []
	next_input_revisions.resize(definitions.size())
	next_definition_instances.resize(definitions.size())
	next_liveries.resize(definitions.size())
	next_archetype_keys.resize(definitions.size())
	for i in range(definitions.size()):
		var definition: CarDefinition = definitions[i]
		var definition_instance := definition.get_instance_id() if definition != null else 0
		var input_revision = input_revisions[i] if i < input_revisions.size() else null
		var cache_matches: bool = (
			input_revision != null
			and i < _cached_input_revisions.size()
			and input_revision == _cached_input_revisions[i]
			and i < _cached_definition_instances.size()
			and definition_instance == _cached_definition_instances[i])
		var livery: CarLivery
		var key: String
		if cache_matches:
			livery = _cached_liveries[i] as CarLivery
			key = String(_cached_archetype_keys[i])
		else:
			livery = _livery_for_index(i, definition, player_settings)
			key = _definition_key(definition, livery)
		next_input_revisions[i] = input_revision
		next_definition_instances[i] = definition_instance
		next_liveries[i] = livery
		next_archetype_keys[i] = key
	return {
		"input_revisions": next_input_revisions,
		"definition_instances": next_definition_instances,
		"liveries": next_liveries,
		"keys": next_archetype_keys,
	}


func _reconfigure_archetypes(definitions: Array, player_settings: Array = [], input_revisions: Array = [], resolved: Dictionary = {}) -> void:
	car_archetype_indices.resize(definitions.size())
	car_slots.resize(definitions.size())
	if !multimesh_render_enabled:
		clear_renderer()
		car_archetype_indices.resize(definitions.size())
		car_slots.resize(definitions.size())
		for i in range(definitions.size()):
			car_archetype_indices[i] = -1
			car_slots[i] = -1
		return
	var reusable := {}
	for archetype in archetypes:
		var existing_key := str(archetype.get("key", ""))
		if existing_key != "":
			reusable[existing_key] = archetype
	var next_archetypes: Array = []
	var archetype_map := {}
	if resolved.is_empty():
		resolved = _resolve_reconfigure_inputs(definitions, player_settings, input_revisions)
	var next_input_revisions: Array = resolved.get("input_revisions", [])
	var next_definition_instances: Array = resolved.get("definition_instances", [])
	var next_liveries: Array = resolved.get("liveries", [])
	var next_archetype_keys: Array = resolved.get("keys", [])
	for i in range(definitions.size()):
		var definition: CarDefinition = definitions[i]
		var livery := next_liveries[i] as CarLivery
		var key := String(next_archetype_keys[i])
		var archetype_index := -1
		if archetype_map.has(key):
			archetype_index = int(archetype_map[key])
		else:
			archetype_index = next_archetypes.size()
			archetype_map[key] = archetype_index
			var archetype: Dictionary
			if reusable.has(key):
				archetype = reusable[key]
				reusable.erase(key)
				archetype["indices"] = []
				archetype["count"] = 0
			else:
				_active_archetype_key = key
				archetype = _build_archetype(definition, livery, key)
				_active_archetype_key = ""
			next_archetypes.append(archetype)
		var target: Dictionary = next_archetypes[archetype_index]
		var count := int(target["count"])
		car_archetype_indices[i] = archetype_index
		car_slots[i] = count
		target["indices"].append(i)
		target["count"] = count + 1
		next_archetypes[archetype_index] = target
	for obsolete in reusable.values():
		_free_archetype_nodes(obsolete)
	archetypes = next_archetypes
	_cached_input_revisions = next_input_revisions
	_cached_definition_instances = next_definition_instances
	_cached_liveries = next_liveries
	_cached_archetype_keys = next_archetype_keys
	for archetype in archetypes:
		_resize_passes(archetype, int(archetype["count"]))

func _free_archetype_nodes(archetype: Dictionary) -> void:
	for pass_name in [PASS_MAIN, PASS_OUTLINE, PASS_OUTLINE_MAIN, "shadow", PASS_STAMP, "thruster"]:
		if !archetype.has(pass_name):
			continue
		var pass_data: Dictionary = archetype[pass_name]
		var node := pass_data.get("node", null) as Node
		if node != null and is_instance_valid(node):
			node.queue_free()

func begin_manual_submit() -> void:
	for archetype in archetypes:
		for pass_name in [PASS_MAIN, PASS_OUTLINE, PASS_OUTLINE_MAIN, "shadow", PASS_STAMP]:
			var pass_data: Dictionary = archetype[pass_name]
			var multimesh: MultiMesh = pass_data["multimesh"]
			multimesh.visible_instance_count = 0
		var thruster_pass: Dictionary = archetype["thruster"]
		var thruster_multimesh: MultiMesh = thruster_pass["multimesh"]
		thruster_multimesh.visible_instance_count = 0

func submit_manual_car(index: int, body_transform: Transform3D, body_overlay: Color, outline_velocity: Vector3, outline_overlay: Color, thrust: float, submit_outlines: bool = true, isolated: bool = false, submit_shadow: bool = true, submit_thrusters: bool = true, visibility: float = 1.0) -> void:
	if index < 0 or index >= car_archetype_indices.size() or index >= car_slots.size():
		return
	var archetype_index := car_archetype_indices[index]
	var slot := 0 if isolated else car_slots[index]
	if archetype_index < 0 or archetype_index >= archetypes.size() or slot < 0:
		return
	var archetype: Dictionary = archetypes[archetype_index]
	visibility = clampf(visibility, 0.0, 1.0)
	_set_pass_instance(archetype[PASS_MAIN], slot, body_transform * archetype[PASS_MAIN]["local_transform"], Vector3.ZERO, body_overlay, visibility)
	if _pass_has_mesh(archetype[PASS_STAMP]):
		_set_pass_instance(archetype[PASS_STAMP], slot, body_transform * archetype[PASS_STAMP]["local_transform"], Vector3.ZERO, Color.WHITE, visibility)
	if submit_outlines:
		_set_pass_instance(archetype[PASS_OUTLINE], slot, body_transform * archetype[PASS_OUTLINE]["local_transform"], outline_velocity, outline_overlay, visibility)
		_set_pass_instance(archetype[PASS_OUTLINE_MAIN], slot, body_transform * archetype[PASS_OUTLINE_MAIN]["local_transform"], outline_velocity, Color.BLACK, visibility)
	if submit_shadow:
		_set_pass_instance(archetype["shadow"], slot, body_transform * archetype["shadow"]["local_transform"], Vector3.ZERO, Color.WHITE, visibility)
	for pass_name in [PASS_MAIN]:
		var pass_data: Dictionary = archetype[pass_name]
		var multimesh: MultiMesh = pass_data["multimesh"]
		if multimesh.mesh != null:
			multimesh.visible_instance_count = max(multimesh.visible_instance_count, slot + 1)
	if submit_shadow:
		var shadow_multimesh: MultiMesh = (archetype["shadow"] as Dictionary)["multimesh"]
		if shadow_multimesh.mesh != null:
			shadow_multimesh.visible_instance_count = max(shadow_multimesh.visible_instance_count, slot + 1)
	if _pass_has_mesh(archetype[PASS_STAMP]):
		var stamp_multimesh: MultiMesh = archetype[PASS_STAMP]["multimesh"]
		stamp_multimesh.visible_instance_count = max(stamp_multimesh.visible_instance_count, slot + 1)
	if submit_outlines:
		for pass_name in [PASS_OUTLINE, PASS_OUTLINE_MAIN]:
			var pass_data: Dictionary = archetype[pass_name]
			var multimesh: MultiMesh = pass_data["multimesh"]
			if multimesh.mesh != null:
				multimesh.visible_instance_count = max(multimesh.visible_instance_count, slot + 1)
	if submit_thrusters:
		var thruster_pass: Dictionary = archetype["thruster"]
		var thruster_multimesh: MultiMesh = thruster_pass["multimesh"]
		var thruster_locals: Array = thruster_pass["local_transforms"]
		var tick_phase := int(Time.get_ticks_msec() / 16)
		for i in range(thruster_locals.size()):
			var thruster_slot := slot * thruster_locals.size() + i
			thruster_multimesh.set_instance_transform(thruster_slot, body_transform * thruster_locals[i])
			thruster_multimesh.set_instance_color(thruster_slot, Color(thrust, thrust, thrust, thrust))
			thruster_multimesh.set_instance_custom_data(thruster_slot, Color(thrust * 0.2, float((tick_phase + i) & 255) * 0.0245436926, thrust, visibility))
		thruster_multimesh.visible_instance_count = max(thruster_multimesh.visible_instance_count, slot * thruster_locals.size() + thruster_locals.size())

func get_native_render_bindings() -> Dictionary:
	var multimeshes: Array = []
	var outline_multimeshes: Array = []
	var outline_main_multimeshes: Array = []
	var shadow_multimeshes: Array = []
	var stamp_multimeshes: Array = []
	var thruster_multimeshes: Array = []
	var local_transforms: Array = []
	var outline_local_transforms: Array = []
	var outline_main_local_transforms: Array = []
	var shadow_local_transforms: Array = []
	var stamp_local_transforms: Array = []
	var thruster_local_transforms: Array = []
	for archetype in archetypes:
		var pass_data: Dictionary = archetype[PASS_MAIN]
		multimeshes.append(pass_data["multimesh"])
		local_transforms.append(pass_data["local_transform"])
		var outline_pass_data: Dictionary = archetype[PASS_OUTLINE]
		outline_multimeshes.append(outline_pass_data["multimesh"])
		outline_local_transforms.append(outline_pass_data["local_transform"])
		var outline_main_pass_data: Dictionary = archetype[PASS_OUTLINE_MAIN]
		outline_main_multimeshes.append(outline_main_pass_data["multimesh"])
		outline_main_local_transforms.append(outline_main_pass_data["local_transform"])
		var shadow_pass_data: Dictionary = archetype["shadow"]
		shadow_multimeshes.append(shadow_pass_data["multimesh"])
		shadow_local_transforms.append(shadow_pass_data["local_transform"])
		var stamp_pass_data: Dictionary = archetype[PASS_STAMP]
		stamp_multimeshes.append(stamp_pass_data["multimesh"] if _pass_has_mesh(stamp_pass_data) else null)
		stamp_local_transforms.append(stamp_pass_data["local_transform"])
		var thruster_pass_data: Dictionary = archetype["thruster"]
		thruster_multimeshes.append(thruster_pass_data["multimesh"])
		thruster_local_transforms.append(thruster_pass_data["local_transforms"])
	return {
		"multimeshes": multimeshes,
		"outline_multimeshes": outline_multimeshes,
		"outline_main_multimeshes": outline_main_multimeshes,
		"shadow_multimeshes": shadow_multimeshes,
		"stamp_multimeshes": stamp_multimeshes,
		"thruster_multimeshes": thruster_multimeshes,
		"local_transforms": local_transforms,
		"outline_local_transforms": outline_local_transforms,
		"outline_main_local_transforms": outline_main_local_transforms,
		"shadow_local_transforms": shadow_local_transforms,
		"stamp_local_transforms": stamp_local_transforms,
		"thruster_local_transforms": thruster_local_transforms,
		"archetype_indices": car_archetype_indices,
		"slots": car_slots,
	}

func _definition_key(definition: CarDefinition, livery: CarLivery = null) -> String:
	if definition == null:
		return ""
	var base_key := definition.content_id
	if livery == null:
		return base_key
	return "%s:%s" % [base_key, livery.get_livery_hash()]

func _build_archetype(definition: CarDefinition, livery: CarLivery = null, key := "") -> Dictionary:
	if definition.car_scene == null:
		return _build_runtime_mesh_archetype(definition, livery, key)
	var template: Node3D = definition.car_scene.instantiate()
	var root_transform := template.transform
	var main_mesh: MeshInstance3D = template.get_node("VEHICLE_MAIN")
	var shadow_mesh: MeshInstance3D = template.get_node("VEHICLE_SHADOW")
	var outline_mesh: MeshInstance3D = template.get_node("VEHICLE_OUTLINE")
	var outline_main_mesh: MeshInstance3D = template.get_node("VEHICLE_OUTLINE_MAIN")
	var thruster_data := _collect_thruster_data(template, root_transform)
	var main_pass := _create_pass("Main_%s" % _safe_name(definition.name), null if stamp_only_mode else main_mesh.mesh, null if stamp_only_mode else main_mesh.material_override, root_transform * main_mesh.transform, 1, 0, livery)
	var outline_pass := _create_outline_pass("Outline_%s" % _safe_name(definition.name), null if stamp_only_mode else outline_mesh.mesh, null if stamp_only_mode else outline_mesh.material_override, root_transform * outline_mesh.transform, 4, -1, livery, true)
	var outline_main_pass := _create_outline_pass("OutlineMain_%s" % _safe_name(definition.name), null if stamp_only_mode else outline_main_mesh.mesh, null if stamp_only_mode else outline_main_mesh.material_override, root_transform * outline_main_mesh.transform, 2, -2, livery, false)
	var shadow_pass := _create_pass("Shadow_%s" % _safe_name(definition.name), null if stamp_only_mode else shadow_mesh.mesh, null if stamp_only_mode else shadow_mesh.material_override, root_transform * shadow_mesh.transform, 1, 96)
	var stamp_pass := _create_stamp_pass("Stamp_%s" % _safe_name(definition.name), main_mesh, template, livery, root_transform * main_mesh.transform, main_mesh.material_override)
	var thruster_pass := _create_thruster_pass("Thruster_%s" % _safe_name(definition.name), null if stamp_only_mode else thruster_data["material"], [] if stamp_only_mode else thruster_data["local_transforms"])
	var archetype := {
		"key": key if key != "" else _definition_key(definition, livery),
		"indices": [],
		"count": 0,
		PASS_MAIN: main_pass,
		PASS_OUTLINE: outline_pass,
		PASS_OUTLINE_MAIN: outline_main_pass,
		"shadow": shadow_pass,
		PASS_STAMP: stamp_pass,
		"thruster": thruster_pass,
	}
	template.free()
	return archetype

func _build_runtime_mesh_archetype(definition: CarDefinition, livery: CarLivery, key: String) -> Dictionary:
	var main_mesh := definition.runtime_mesh
	var local_transform := definition.runtime_transform
	var outline_material := ShaderMaterial.new()
	outline_material.shader = OUTLINE_SHADER
	outline_material.set_shader_parameter("base_outline_width", 1.0)
	outline_material.set_shader_parameter("trail_colour", Color(0.25, 0.55, 1.0))
	var outline_main_material := ShaderMaterial.new()
	outline_main_material.shader = OUTLINE_MAIN_SHADER
	outline_main_material.set_shader_parameter("base_outline_width", 1.0)
	outline_main_material.set_shader_parameter("outline_color", Color(0.25, 0.55, 1.0))
	var shadow_material := ShaderMaterial.new()
	shadow_material.shader = SHADOW_SHADER
	var template := Node3D.new()
	var body_mesh := MeshInstance3D.new()
	body_mesh.mesh = main_mesh
	body_mesh.material_override = definition.runtime_material
	body_mesh.transform = local_transform
	template.add_child(body_mesh)
	var main_pass := _create_pass("Main_%s" % _safe_name(definition.name), null if stamp_only_mode else main_mesh, null if stamp_only_mode else definition.runtime_material, local_transform, 1, 0, livery)
	var outline_pass := _create_outline_pass("Outline_%s" % _safe_name(definition.name), null if stamp_only_mode else main_mesh, null if stamp_only_mode else outline_material, local_transform, 4, -1, livery, true)
	var outline_main_pass := _create_outline_pass("OutlineMain_%s" % _safe_name(definition.name), null if stamp_only_mode else main_mesh, null if stamp_only_mode else outline_main_material, local_transform, 2, -2, livery, false)
	var shadow_pass := _create_pass("Shadow_%s" % _safe_name(definition.name), null if stamp_only_mode else main_mesh, null if stamp_only_mode else shadow_material, local_transform, 1, 96)
	var stamp_pass := _create_stamp_pass("Stamp_%s" % _safe_name(definition.name), body_mesh, template, livery, local_transform, definition.runtime_material)
	var runtime_thruster_material := _runtime_thruster_material()
	var thruster_pass := _create_thruster_pass("Thruster_%s" % _safe_name(definition.name), runtime_thruster_material, [] if stamp_only_mode else definition.runtime_thruster_transforms)
	var archetype := {
		"key": key if key != "" else _definition_key(definition, livery),
		"indices": [],
		"count": 0,
		PASS_MAIN: main_pass,
		PASS_OUTLINE: outline_pass,
		PASS_OUTLINE_MAIN: outline_main_pass,
		"shadow": shadow_pass,
		PASS_STAMP: stamp_pass,
		"thruster": thruster_pass,
	}
	template.free()
	return archetype

func _runtime_thruster_material() -> Material:
	var template := THRUSTER_SCENE.instantiate() as Node3D
	var sprite := template.get_node("Sprite3D") as Sprite3D
	var material := sprite.material_override
	template.free()
	return material


func _snapshot_stamp_build(definition: CarDefinition, livery: CarLivery) -> Dictionary:
	if definition == null or livery == null or livery.stamps.is_empty():
		return {}
	var catalog := _get_stamp_catalog()
	if catalog == null:
		return {}
	if definition.car_scene != null:
		var template := definition.car_scene.instantiate() as Node3D
		var body_mesh := template.get_node("VEHICLE_MAIN") as MeshInstance3D
		var snapshot := stamp_mesh_builder.snapshot_build_inputs(
			body_mesh,
			_body_to_car_transform(body_mesh, template),
			livery,
			catalog,
			stamp_visibility_masks_enabled,
			stamp_visibility_mask_skip_layer)
		template.free()
		return snapshot
	var runtime_template := Node3D.new()
	var runtime_body := MeshInstance3D.new()
	runtime_body.mesh = definition.runtime_mesh
	runtime_body.transform = definition.runtime_transform
	runtime_template.add_child(runtime_body)
	var snapshot := stamp_mesh_builder.snapshot_build_inputs(
		runtime_body,
		definition.runtime_transform,
		livery,
		catalog,
		stamp_visibility_masks_enabled,
		stamp_visibility_mask_skip_layer)
	runtime_template.free()
	return snapshot

func _create_stamp_pass(pass_name: String, body_mesh: MeshInstance3D, template: Node3D, livery: CarLivery, local_transform: Transform3D, base_material: Material) -> Dictionary:
	var mesh: Mesh = null
	var material: Material = null
	var stamp_vertex_ranges := {}
	if livery != null and !livery.stamps.is_empty():
		var catalog := _get_stamp_catalog()
		if catalog != null:
			if custom_stamp_atlas_texture != null:
				catalog = catalog.duplicate() as CarStampCatalog
				catalog.custom_atlas_texture = custom_stamp_atlas_texture
			var stamp_build: Dictionary
			if _prepared_stamp_builds.has(_active_archetype_key):
				stamp_build = stamp_mesh_builder.install_prepared(_prepared_stamp_builds[_active_archetype_key])
			else:
				stamp_build = stamp_mesh_builder.build_for_body_mesh_with_masks(body_mesh, _body_to_car_transform(body_mesh, template), livery, catalog, stamp_visibility_masks_enabled, stamp_visibility_mask_skip_layer)
			var generated_mesh: Mesh = stamp_build["mesh"]
			if generated_mesh != null and generated_mesh.get_surface_count() > 0:
				mesh = generated_mesh
				material = catalog.create_stamp_material(base_material, stamp_build["visibility_mask"])
				stamp_vertex_ranges = stamp_build.get("stamp_vertex_ranges", {})
	var pass_data := _create_pass(pass_name, mesh, material, local_transform, 2, stamp_render_priority)
	pass_data["stamp_vertex_ranges"] = stamp_vertex_ranges
	if mesh == null:
		var node: MultiMeshInstance3D = pass_data["node"]
		node.visible = false
	return pass_data

func _body_to_car_transform(body_mesh: MeshInstance3D, car_root: Node3D) -> Transform3D:
	if car_root == null:
		return body_mesh.transform
	var node: Node = body_mesh
	var out := Transform3D.IDENTITY
	while node != null and node != car_root:
		var node_3d := node as Node3D
		if node_3d == null:
			break
		out = node_3d.transform * out
		node = node.get_parent()
	if node == car_root:
		return out
	if body_mesh.is_inside_tree() and car_root.is_inside_tree():
		return car_root.global_transform.affine_inverse() * body_mesh.global_transform
	return body_mesh.transform

func _update_stamp_material_custom_atlases() -> void:
	for archetype in archetypes:
		if !archetype.has(PASS_STAMP):
			continue
		var pass_data: Dictionary = archetype[PASS_STAMP]
		var node := pass_data.get("node", null) as MultiMeshInstance3D
		if node == null:
			continue
		var material := node.material_override as ShaderMaterial
		if material == null:
			continue
		material.set_shader_parameter("custom_stamp_atlas", custom_stamp_atlas_texture)

func update_stamp_layer_colour(layer: int, colour: Color) -> void:
	for archetype in archetypes:
		var pass_data: Dictionary = archetype[PASS_STAMP]
		var multimesh: MultiMesh = pass_data["multimesh"]
		if multimesh == null:
			continue
		var mesh := multimesh.mesh as ArrayMesh
		if mesh == null or mesh.get_surface_count() <= 0:
			continue
		var ranges: Dictionary = pass_data.get("stamp_vertex_ranges", {})
		if !ranges.has(layer):
			continue
		var range_data: Dictionary = ranges[layer]
		var start := int(range_data.get("start", 0))
		var count := int(range_data.get("count", 0))
		if count <= 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		if arrays.size() <= Mesh.ARRAY_COLOR or typeof(arrays[Mesh.ARRAY_COLOR]) != TYPE_PACKED_COLOR_ARRAY:
			continue
		var colours: PackedColorArray = arrays[Mesh.ARRAY_COLOR]
		var end := mini(start + count, colours.size())
		for i in range(start, end):
			colours[i] = colour
		arrays[Mesh.ARRAY_COLOR] = colours
		mesh.clear_surfaces()
		var format_flags := Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT
		if arrays.size() > Mesh.ARRAY_CUSTOM1 and typeof(arrays[Mesh.ARRAY_CUSTOM1]) == TYPE_PACKED_FLOAT32_ARRAY:
			format_flags |= Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, format_flags)

func _collect_thruster_data(template: Node3D, root_transform: Transform3D) -> Dictionary:
	var local_transforms: Array[Transform3D] = []
	var material: Material = null
	var thruster_root := template.get_node_or_null("THRUSTERS") as Node3D
	if thruster_root == null:
		return {"local_transforms": local_transforms, "material": material}
	for child in thruster_root.get_children():
		var thruster := child as Node3D
		if thruster == null:
			continue
		local_transforms.append(root_transform * thruster_root.transform * thruster.transform)
		if material == null:
			var sprite := thruster.get_node_or_null("Sprite3D") as Sprite3D
			if sprite != null:
				material = sprite.material_override
	return {"local_transforms": local_transforms, "material": material}

func _create_pass(pass_name: String, mesh: Mesh, material: Material, local_transform: Transform3D, layers: int, render_priority: int, livery: CarLivery = null) -> Dictionary:
	var node := MultiMeshInstance3D.new()
	node.name = pass_name
	node.layers = layers
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.ignore_occlusion_culling = true
	node.extra_cull_margin = 1000000.0
	node.visibility_range_end = 0.0
	node.visibility_range_end_margin = 0.0
	node.lod_bias = 1000000.0
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.use_custom_data = true
	multimesh.mesh = mesh
	multimesh.instance_count = 0
	node.multimesh = multimesh
	if material != null:
		node.material_override = material.duplicate()
		node.material_override.render_priority = render_priority
		_apply_livery_to_material(node.material_override, livery)
	add_child(node)
	return {
		"local_transform": local_transform,
		"node": node,
		"multimesh": multimesh,
	}

func _create_outline_pass(pass_name: String, mesh: Mesh, material: Material, local_transform: Transform3D, layers: int, render_priority: int, livery: CarLivery, trail: bool) -> Dictionary:
	var pass_data := _create_pass(pass_name, mesh, material, local_transform, layers, render_priority)
	var node := pass_data.get("node", null) as MultiMeshInstance3D
	if node != null:
		_apply_outline_livery_to_material(node.material_override, livery, trail)
	return pass_data

func _create_thruster_pass(pass_name: String, material: Material, local_transforms: Array) -> Dictionary:
	var node := MultiMeshInstance3D.new()
	node.name = pass_name
	node.layers = 2
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.ignore_occlusion_culling = true
	node.extra_cull_margin = 1000000.0
	var quad := QuadMesh.new()
	quad.size = Vector2(1.0, 1.0)
	if material != null:
		quad.material = material.duplicate()
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.use_custom_data = true
	multimesh.mesh = quad
	multimesh.instance_count = 0
	node.multimesh = multimesh
	add_child(node)
	return {
		"local_transforms": local_transforms,
		"node": node,
		"multimesh": multimesh,
	}

func _resize_passes(archetype: Dictionary, count: int) -> void:
	for pass_name in [PASS_MAIN, PASS_OUTLINE, PASS_OUTLINE_MAIN, "shadow", PASS_STAMP]:
		var pass_data: Dictionary = archetype[pass_name]
		var multimesh: MultiMesh = pass_data["multimesh"]
		if multimesh.mesh == null:
			multimesh.instance_count = 0
			multimesh.visible_instance_count = 0
			continue
		if multimesh.instance_count != count:
			var old_count := multimesh.instance_count
			multimesh.instance_count = count
			_park_multimesh_instances(multimesh, old_count, count)
		multimesh.visible_instance_count = 0
	var thruster_pass: Dictionary = archetype["thruster"]
	var thruster_multimesh: MultiMesh = thruster_pass["multimesh"]
	var thruster_count: int = (thruster_pass["local_transforms"] as Array).size()
	var total_thrusters := count * thruster_count
	if thruster_multimesh.instance_count != total_thrusters:
		var old_thruster_count := thruster_multimesh.instance_count
		thruster_multimesh.instance_count = total_thrusters
		_park_multimesh_instances(thruster_multimesh, old_thruster_count, total_thrusters)
	thruster_multimesh.visible_instance_count = 0

func _park_multimesh_instances(multimesh: MultiMesh, start: int, end: int) -> void:
	if multimesh == null or multimesh.mesh == null:
		return
	for i in range(maxi(0, start), maxi(0, end)):
		multimesh.set_instance_transform(i, HIDDEN_INSTANCE_TRANSFORM)
		multimesh.set_instance_color(i, Color(0.0, 0.0, 0.0, 0.0))
		multimesh.set_instance_custom_data(i, Color(0.0, 0.0, 0.0, 0.0))

func _set_pass_instance(pass_data: Dictionary, slot: int, transform: Transform3D, custom_vec: Vector3, color: Color, visibility: float) -> void:
	var multimesh: MultiMesh = pass_data["multimesh"]
	if multimesh.mesh == null:
		return
	multimesh.set_instance_transform(slot, transform)
	multimesh.set_instance_custom_data(slot, Color(custom_vec.x, custom_vec.y, custom_vec.z, 1.0))
	color.a *= visibility
	multimesh.set_instance_color(slot, color)

func _pass_has_mesh(pass_data: Dictionary) -> bool:
	var multimesh: MultiMesh = pass_data["multimesh"]
	return multimesh != null and multimesh.mesh != null

func _apply_livery_to_material(material: Material, livery: CarLivery) -> void:
	if material == null or livery == null:
		return
	var shader_material := material as ShaderMaterial
	if shader_material != null:
		if shader_material.get_shader_parameter("in_paint_mask") == null:
			var fallback_mask = shader_material.get_shader_parameter("in_albedo")
			if fallback_mask != null:
				shader_material.set_shader_parameter("in_paint_mask", fallback_mask)
		shader_material.set_shader_parameter("livery_colour_strength", 1.0)
		shader_material.set_shader_parameter("livery_primary_colour", livery.primary_colour)
		shader_material.set_shader_parameter("livery_secondary_colour", livery.secondary_colour)
		shader_material.set_shader_parameter("livery_accent_colour", livery.accent_colour)
		return
	var standard_material := material as StandardMaterial3D
	if standard_material != null:
		standard_material.albedo_color = livery.primary_colour

func _apply_livery_to_pass(archetype: Dictionary, pass_name: String, livery: CarLivery) -> void:
	if !archetype.has(pass_name):
		return
	var pass_data: Dictionary = archetype[pass_name]
	var node := pass_data.get("node", null) as MultiMeshInstance3D
	if node != null:
		_apply_livery_to_material(node.material_override, livery)

func _apply_outline_livery_to_pass(archetype: Dictionary, pass_name: String, livery: CarLivery, trail: bool) -> void:
	if !archetype.has(pass_name):
		return
	var pass_data: Dictionary = archetype[pass_name]
	var node := pass_data.get("node", null) as MultiMeshInstance3D
	if node != null:
		_apply_outline_livery_to_material(node.material_override, livery, trail)

func _apply_outline_livery_to_material(material: Material, livery: CarLivery, trail: bool) -> void:
	if material == null or livery == null:
		return
	var shader_material := material as ShaderMaterial
	if shader_material == null:
		return
	if trail:
		if livery.trail_colour_customized:
			shader_material.set_shader_parameter("trail_colour", livery.trail_colour)
	elif livery.outline_colour_customized:
		shader_material.set_shader_parameter("outline_color", livery.outline_colour)

func _livery_for_index(index: int, definition: CarDefinition, player_settings: Array) -> CarLivery:
	if index < 0 or index >= player_settings.size():
		return null
	var raw_settings = player_settings[index]
	var livery: CarLivery = null
	if raw_settings is PlayerSettings:
		if raw_settings.car_livery.is_empty():
			return null
		livery = raw_settings.get_car_livery_resource()
	elif typeof(raw_settings) == TYPE_DICTIONARY:
		var settings_dict: Dictionary = raw_settings
		if settings_dict.has("car_livery") and typeof(settings_dict["car_livery"]) == TYPE_DICTIONARY:
			var livery_dict: Dictionary = settings_dict["car_livery"]
			if livery_dict.is_empty():
				return null
			livery = CarLivery.new()
			livery.from_dict(livery_dict)
	if livery == null:
		return null
	if livery.vehicle_content_id == "" and definition != null:
		livery.vehicle_content_id = definition.content_id
	return livery

func _get_stamp_catalog() -> CarStampCatalog:
	if stamp_catalog != null:
		return stamp_catalog
	if ResourceLoader.exists(STAMP_CATALOG_PATH):
		stamp_catalog = load(STAMP_CATALOG_PATH) as CarStampCatalog
	return stamp_catalog

func _safe_name(name_in: String) -> String:
	return name_in.replace(" ", "_").replace("/", "_")
