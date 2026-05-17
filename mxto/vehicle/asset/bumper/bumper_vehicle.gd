extends Node3D

func _init() -> void:
	var body_mesh := SphereMesh.new()
	body_mesh.radius = 1.0
	body_mesh.height = 1.0
	body_mesh.radial_segments = 24
	body_mesh.rings = 8
	body_mesh.is_hemisphere = true
	var body_material := StandardMaterial3D.new()
	body_material.albedo_color = Color(0.95, 0.16, 0.08, 1.0)
	body_material.roughness = 0.35
	var shadow_material := StandardMaterial3D.new()
	shadow_material.albedo_color = Color(0.0, 0.0, 0.0, 0.45)
	shadow_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	var outline_material := StandardMaterial3D.new()
	outline_material.albedo_color = Color(0.08, 0.0, 0.0, 1.0)

	_add_mesh("VEHICLE_MAIN", body_mesh, body_material, Vector3(3.0, 1.6, 3.0), 2, 0.0)
	_add_mesh("VEHICLE_SHADOW", body_mesh, shadow_material, Vector3(3.2, 0.08, 3.2), 2, -0.05)
	_add_mesh("VEHICLE_OUTLINE", body_mesh, outline_material, Vector3(3.22, 1.72, 3.22), 4, 0.0)
	_add_mesh("VEHICLE_OUTLINE_MAIN", body_mesh, outline_material, Vector3(3.12, 1.66, 3.12), 2, 0.0)
	var thrusters := Node3D.new()
	thrusters.name = "THRUSTERS"
	add_child(thrusters)

func _add_mesh(node_name: String, mesh: Mesh, material: Material, scale: Vector3, layers: int, y_offset: float) -> void:
	var mesh_instance := MeshInstance3D.new()
	mesh_instance.name = node_name
	mesh_instance.mesh = mesh
	mesh_instance.material_override = material
	mesh_instance.layers = layers
	mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mesh_instance.transform = Transform3D(Basis().scaled(scale), Vector3(0.0, y_offset, 0.0))
	add_child(mesh_instance)
