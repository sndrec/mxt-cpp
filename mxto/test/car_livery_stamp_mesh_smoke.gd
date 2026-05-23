extends SceneTree

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarLiveryStampMeshBuilder = preload("res://vehicle/customization/car_livery_stamp_mesh_builder.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CarStampEntry = preload("res://vehicle/customization/car_stamp_entry.gd")

func _init() -> void:
	var root := Node3D.new()
	var body := MeshInstance3D.new()
	body.name = "VEHICLE_MAIN"
	body.mesh = _make_quad_mesh()
	root.add_child(body)

	var livery := CarLivery.new()
	var stamp := CarLiveryStamp.new()
	stamp.stamp_id = "smoke"
	stamp.local_origin = Vector3.ZERO
	stamp.local_basis = Basis.IDENTITY
	stamp.size = Vector2.ONE
	stamp.projection_depth = 0.5
	stamp.colour = Color(0.5, 0.75, 1.0, 1.0)
	if !livery.add_stamp(stamp):
		push_error("failed to add livery smoke stamp")
		quit(1)
		return

	var entry := CarStampEntry.new()
	entry.stamp_id = "smoke"
	entry.atlas_tile_position = Vector2i(1, 1)
	entry.atlas_tile_size = Vector2i(2, 2)
	var catalog := CarStampCatalog.new()
	catalog.atlas_grid_size = Vector2i(4, 4)
	catalog.entries.append(entry)

	var decal_mesh := CarLiveryStampMeshBuilder.build_for_vehicle_scene(root, livery, catalog)
	if decal_mesh.get_surface_count() != 1:
		push_error("expected one generated stamp surface")
		quit(1)
		return

	var arrays := decal_mesh.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
	var colours: PackedColorArray = arrays[Mesh.ARRAY_COLOR]
	if vertices.size() < 3 or vertices.size() != uvs.size() or vertices.size() != colours.size():
		push_error("generated stamp mesh arrays are inconsistent")
		quit(1)
		return
	for i in range(vertices.size()):
		var p := vertices[i]
		var uv := uvs[i]
		if absf(p.x) > 0.506 or absf(p.y) > 0.506 or p.z < 0.005:
			push_error("generated stamp vertex outside expected clipped projector bounds")
			quit(1)
			return
		if uv.x < 0.249 or uv.x > 0.751 or uv.y < 0.249 or uv.y > 0.751:
			push_error("generated stamp uv outside atlas rect")
			quit(1)
			return
	var vertex_count := vertices.size()
	root.free()
	print("MXT_CAR_LIVERY_STAMP_MESH_SMOKE vertices=", vertex_count)
	quit(0)

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
