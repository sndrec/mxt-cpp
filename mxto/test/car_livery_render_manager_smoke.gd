extends SceneTree

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarRenderManager = preload("res://vehicle/car_render_manager.gd")
const PlayerSettings = preload("res://player/player_settings.gd")

func _init() -> void:
	var manager := CarRenderManager.new()
	root.add_child(manager)

	var definition := _make_definition()
	var livery := _make_livery()
	var settings_a := PlayerSettings.new()
	settings_a.set_car_livery(livery)
	var settings_b := PlayerSettings.new()
	settings_b.set_car_livery(livery)

	manager.configure_manual([definition, definition], [settings_a, settings_b])
	if manager.archetypes.size() != 1:
		push_error("matching livery cars should share one render archetype")
		quit(1)
		return

	var archetype: Dictionary = manager.archetypes[0]
	if int(archetype["count"]) != 2:
		push_error("shared livery archetype should contain two slots")
		quit(1)
		return
	var main_pass: Dictionary = archetype[CarRenderManager.PASS_MAIN]
	var main_node: MultiMeshInstance3D = main_pass["node"]
	var main_material := main_node.material_override as ShaderMaterial
	if main_material == null:
		push_error("main livery pass should keep a shader material")
		quit(1)
		return
	if main_material.get_shader_parameter("livery_colour_strength") != 1.0:
		push_error("main livery pass should enable livery colour strength")
		quit(1)
		return
	if main_material.get_shader_parameter("in_paint_mask") == null:
		push_error("main livery pass should provide a paint mask fallback")
		quit(1)
		return

	var stamp_pass: Dictionary = archetype[CarRenderManager.PASS_STAMP]
	var stamp_node: MultiMeshInstance3D = stamp_pass["node"]
	var stamp_material := stamp_node.material_override as ShaderMaterial
	if stamp_material == null or stamp_material.get_shader_parameter("stamp_atlas") == null:
		push_error("stamp pass should use the multiplicative stamp shader material")
		quit(1)
		return
	if stamp_material.get_shader_parameter("in_albedo") == null or stamp_material.get_shader_parameter("in_paint_mask") != null:
		push_error("stamp pass should sample base albedo without receiving the paint mask")
		quit(1)
		return
	var stamp_multimesh: MultiMesh = stamp_pass["multimesh"]
	if stamp_multimesh.mesh == null or stamp_multimesh.mesh.get_surface_count() != 1:
		push_error("stamp pass should generate one combined stamp mesh surface")
		quit(1)
		return
	if stamp_multimesh.instance_count != 2:
		push_error("stamp pass should allocate one instance per car slot")
		quit(1)
		return

	var bindings := manager.get_native_render_bindings()
	var stamp_multimeshes: Array = bindings["stamp_multimeshes"]
	if stamp_multimeshes.size() != 1 or stamp_multimeshes[0] == null:
		push_error("native render bindings should expose the stamp multimesh")
		quit(1)
		return

	manager.begin_manual_submit()
	manager.submit_manual_car(0, Transform3D.IDENTITY, Color.WHITE, Vector3.ZERO, Color.BLACK, 0.0)
	manager.submit_manual_car(1, Transform3D(Basis.IDENTITY, Vector3(1.0, 2.0, 3.0)), Color.WHITE, Vector3.ZERO, Color.BLACK, 0.0)
	if stamp_multimesh.visible_instance_count != 2:
		push_error("manual submit should make both stamp instances visible")
		quit(1)
		return

	var stamp_vertices: int = stamp_multimesh.mesh.surface_get_array_len(0)
	manager.clear_renderer()
	root.remove_child(manager)
	manager.free()
	print("MXT_CAR_LIVERY_RENDER_MANAGER_SMOKE archetypes=1 stamp_vertices=", stamp_vertices)
	quit(0)

func _make_definition() -> CarDefinition:
	var root_node := Node3D.new()
	root_node.name = "SmokeCar"
	var mesh := _make_quad_mesh()
	_add_mesh_child(root_node, "VEHICLE_MAIN", mesh, _make_body_material())
	_add_mesh_child(root_node, "VEHICLE_SHADOW", mesh)
	_add_mesh_child(root_node, "VEHICLE_OUTLINE", mesh)
	_add_mesh_child(root_node, "VEHICLE_OUTLINE_MAIN", mesh)

	var packed := PackedScene.new()
	var err := packed.pack(root_node)
	if err != OK:
		push_error("failed to pack smoke car scene")
	root_node.free()

	var definition := CarDefinition.new()
	definition.name = "SmokeCar"
	definition.car_scene = packed
	return definition

func _add_mesh_child(parent: Node3D, name_in: String, mesh: Mesh, material: Material = null) -> void:
	var child := MeshInstance3D.new()
	child.name = name_in
	child.mesh = mesh
	child.material_override = material
	parent.add_child(child)
	child.owner = parent

func _make_body_material() -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = load("res://vehicle/base_vehicle_shader.gdshader")
	var albedo := GradientTexture1D.new()
	var gradient := Gradient.new()
	gradient.colors = PackedColorArray([Color(0.0, 0.0, 0.0, 1.0), Color(1.0, 1.0, 1.0, 1.0)])
	albedo.gradient = gradient
	material.set_shader_parameter("in_albedo", albedo)
	return material

func _make_livery() -> CarLivery:
	var livery := CarLivery.new()
	var stamp := CarLiveryStamp.new()
	stamp.stamp_id = "circle"
	stamp.local_origin = Vector3.ZERO
	stamp.local_basis = Basis.IDENTITY
	stamp.size = Vector2.ONE
	stamp.projection_depth = 0.5
	stamp.colour = Color(1.0, 0.2, 0.3, 0.85)
	stamp.opacity = 0.75
	if !livery.add_stamp(stamp):
		push_error("failed to add renderer smoke stamp")
	return livery

func _make_quad_mesh() -> ArrayMesh:
	var vertices := PackedVector3Array([
		Vector3(-1.0, -1.0, 0.0),
		Vector3(1.0, -1.0, 0.0),
		Vector3(1.0, 1.0, 0.0),
		Vector3(-1.0, -1.0, 0.0),
		Vector3(1.0, 1.0, 0.0),
		Vector3(-1.0, 1.0, 0.0),
	])
	var normals := PackedVector3Array()
	for i in range(vertices.size()):
		normals.append(Vector3(0.0, 0.0, 1.0))
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh
