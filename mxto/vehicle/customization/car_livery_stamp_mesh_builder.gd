class_name CarLiveryStampMeshBuilder
extends RefCounted

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CarStampEntry = preload("res://vehicle/customization/car_stamp_entry.gd")

const BODY_NODE_PATH := NodePath("VEHICLE_MAIN")
const SURFACE_OFFSET := 0.006
const EPSILON := 0.00001

static func build_for_vehicle_scene(vehicle_root: Node3D, livery: CarLivery, catalog: CarStampCatalog) -> ArrayMesh:
	if vehicle_root == null:
		return ArrayMesh.new()
	var body_mesh := vehicle_root.get_node_or_null(BODY_NODE_PATH) as MeshInstance3D
	return build_for_body_mesh(body_mesh, livery, catalog, vehicle_root)

static func build_for_body_mesh(body_mesh: MeshInstance3D, livery: CarLivery, catalog: CarStampCatalog, car_root: Node3D = null) -> ArrayMesh:
	var out_mesh := ArrayMesh.new()
	if body_mesh == null or body_mesh.mesh == null or livery == null or catalog == null:
		return out_mesh

	var out_vertices := PackedVector3Array()
	var out_normals := PackedVector3Array()
	var out_uvs := PackedVector2Array()
	var out_colours := PackedColorArray()
	var body_to_car := _body_to_car_transform(body_mesh, car_root)

	for stamp in livery.get_sorted_stamps():
		if stamp == null or !stamp.enabled or stamp.opacity <= 0.0:
			continue
		var entry := catalog.get_entry(stamp.stamp_id)
		if entry == null:
			continue
		_append_stamp(body_mesh.mesh, body_to_car, stamp, entry, out_vertices, out_normals, out_uvs, out_colours)

	if out_vertices.is_empty():
		return out_mesh

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = out_vertices
	arrays[Mesh.ARRAY_NORMAL] = out_normals
	arrays[Mesh.ARRAY_TEX_UV] = out_uvs
	arrays[Mesh.ARRAY_COLOR] = out_colours
	out_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return out_mesh

static func _append_stamp(
	mesh: Mesh,
	body_to_car: Transform3D,
	stamp: CarLiveryStamp,
	entry: CarStampEntry,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_uvs: PackedVector2Array,
	out_colours: PackedColorArray
) -> void:
	var projector := Transform3D(stamp.local_basis, stamp.local_origin)
	if absf(projector.basis.determinant()) <= EPSILON:
		return
	var car_to_projector := projector.affine_inverse()
	var half_size := Vector2(maxf(stamp.size.x, EPSILON), maxf(stamp.size.y, EPSILON)) * 0.5
	var half_depth := maxf(stamp.projection_depth, EPSILON) * 0.5
	var clip_min := Vector3(-half_size.x, -half_size.y, -half_depth)
	var clip_max := Vector3(half_size.x, half_size.y, half_depth)
	var stamp_colour := Color(stamp.colour.r, stamp.colour.g, stamp.colour.b, stamp.colour.a * stamp.opacity)

	for surface_index in range(mesh.get_surface_count()):
		var arrays := mesh.surface_get_arrays(surface_index)
		if arrays.size() <= Mesh.ARRAY_VERTEX:
			continue
		var vertices := _surface_vertices(arrays)
		if vertices.is_empty():
			continue
		var normals := _surface_normals(arrays)
		var indices := _surface_indices(arrays)
		if indices.is_empty():
			_append_unindexed_surface(body_to_car, car_to_projector, clip_min, clip_max, entry.atlas_rect, stamp_colour, vertices, normals, out_vertices, out_normals, out_uvs, out_colours)
		else:
			_append_indexed_surface(body_to_car, car_to_projector, clip_min, clip_max, entry.atlas_rect, stamp_colour, vertices, normals, indices, out_vertices, out_normals, out_uvs, out_colours)

static func _append_indexed_surface(
	body_to_car: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	colour: Color,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	indices: PackedInt32Array,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_uvs: PackedVector2Array,
	out_colours: PackedColorArray
) -> void:
	var tri_count := int(indices.size() / 3)
	for tri in range(tri_count):
		var i0 := indices[tri * 3]
		var i1 := indices[tri * 3 + 1]
		var i2 := indices[tri * 3 + 2]
		_append_triangle(body_to_car, car_to_projector, clip_min, clip_max, atlas_rect, colour, vertices, normals, i0, i1, i2, out_vertices, out_normals, out_uvs, out_colours)

static func _append_unindexed_surface(
	body_to_car: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	colour: Color,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_uvs: PackedVector2Array,
	out_colours: PackedColorArray
) -> void:
	var tri_count := int(vertices.size() / 3)
	for tri in range(tri_count):
		var i0 := tri * 3
		_append_triangle(body_to_car, car_to_projector, clip_min, clip_max, atlas_rect, colour, vertices, normals, i0, i0 + 1, i0 + 2, out_vertices, out_normals, out_uvs, out_colours)

static func _append_triangle(
	body_to_car: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	colour: Color,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	i0: int,
	i1: int,
	i2: int,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_uvs: PackedVector2Array,
	out_colours: PackedColorArray
) -> void:
	if i0 < 0 or i1 < 0 or i2 < 0 or i0 >= vertices.size() or i1 >= vertices.size() or i2 >= vertices.size():
		return
	var p0 := body_to_car * vertices[i0]
	var p1 := body_to_car * vertices[i1]
	var p2 := body_to_car * vertices[i2]
	var face_normal := (p1 - p0).cross(p2 - p0)
	if face_normal.length_squared() <= EPSILON:
		return
	face_normal = face_normal.normalized()
	var n0 := _normal_at(body_to_car, normals, i0, face_normal)
	var n1 := _normal_at(body_to_car, normals, i1, face_normal)
	var n2 := _normal_at(body_to_car, normals, i2, face_normal)
	var polygon := [
		{"projector_pos": car_to_projector * p0, "car_pos": p0, "normal": n0},
		{"projector_pos": car_to_projector * p1, "car_pos": p1, "normal": n1},
		{"projector_pos": car_to_projector * p2, "car_pos": p2, "normal": n2},
	]
	polygon = _clip_polygon_to_box(polygon, clip_min, clip_max)
	if polygon.size() < 3:
		return
	for i in range(1, polygon.size() - 1):
		_emit_vertex(polygon[0], clip_min, clip_max, atlas_rect, colour, out_vertices, out_normals, out_uvs, out_colours)
		_emit_vertex(polygon[i], clip_min, clip_max, atlas_rect, colour, out_vertices, out_normals, out_uvs, out_colours)
		_emit_vertex(polygon[i + 1], clip_min, clip_max, atlas_rect, colour, out_vertices, out_normals, out_uvs, out_colours)

static func _clip_polygon_to_box(polygon: Array, clip_min: Vector3, clip_max: Vector3) -> Array:
	polygon = _clip_polygon_axis(polygon, 0, clip_min.x, true)
	polygon = _clip_polygon_axis(polygon, 0, clip_max.x, false)
	polygon = _clip_polygon_axis(polygon, 1, clip_min.y, true)
	polygon = _clip_polygon_axis(polygon, 1, clip_max.y, false)
	polygon = _clip_polygon_axis(polygon, 2, clip_min.z, true)
	polygon = _clip_polygon_axis(polygon, 2, clip_max.z, false)
	return polygon

static func _clip_polygon_axis(polygon: Array, axis: int, plane: float, keep_greater: bool) -> Array:
	if polygon.is_empty():
		return polygon
	var out := []
	var previous = polygon[polygon.size() - 1]
	var previous_inside := _point_inside_axis(previous["projector_pos"], axis, plane, keep_greater)
	for current in polygon:
		var current_inside := _point_inside_axis(current["projector_pos"], axis, plane, keep_greater)
		if current_inside != previous_inside:
			out.append(_interpolate_clip_vertex(previous, current, axis, plane))
		if current_inside:
			out.append(current)
		previous = current
		previous_inside = current_inside
	return out

static func _point_inside_axis(point: Vector3, axis: int, plane: float, keep_greater: bool) -> bool:
	var value := _axis_value(point, axis)
	if keep_greater:
		return value >= plane - EPSILON
	return value <= plane + EPSILON

static func _interpolate_clip_vertex(a: Dictionary, b: Dictionary, axis: int, plane: float) -> Dictionary:
	var a_pos: Vector3 = a["projector_pos"]
	var b_pos: Vector3 = b["projector_pos"]
	var denom := _axis_value(b_pos, axis) - _axis_value(a_pos, axis)
	var t := 0.0
	if absf(denom) > EPSILON:
		t = (plane - _axis_value(a_pos, axis)) / denom
	t = clampf(t, 0.0, 1.0)
	var a_normal: Vector3 = a["normal"]
	var b_normal: Vector3 = b["normal"]
	var normal := a_normal.lerp(b_normal, t)
	if normal.length_squared() <= EPSILON:
		normal = a_normal
	var a_car_pos: Vector3 = a["car_pos"]
	var b_car_pos: Vector3 = b["car_pos"]
	return {
		"projector_pos": a_pos.lerp(b_pos, t),
		"car_pos": a_car_pos.lerp(b_car_pos, t),
		"normal": normal.normalized(),
	}

static func _emit_vertex(
	vertex: Dictionary,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	colour: Color,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_uvs: PackedVector2Array,
	out_colours: PackedColorArray
) -> void:
	var normal: Vector3 = vertex["normal"]
	var car_pos: Vector3 = vertex["car_pos"]
	var projector_pos: Vector3 = vertex["projector_pos"]
	var u := inverse_lerp(clip_min.x, clip_max.x, projector_pos.x)
	var v := inverse_lerp(clip_max.y, clip_min.y, projector_pos.y)
	var atlas_uv := atlas_rect.position + Vector2(u, v) * atlas_rect.size
	out_vertices.append(car_pos + normal * SURFACE_OFFSET)
	out_normals.append(normal)
	out_uvs.append(atlas_uv)
	out_colours.append(colour)

static func _normal_at(body_to_car: Transform3D, normals: PackedVector3Array, index: int, fallback: Vector3) -> Vector3:
	if index < 0 or index >= normals.size():
		return fallback
	var normal := body_to_car.basis * normals[index]
	if normal.length_squared() <= EPSILON:
		return fallback
	return normal.normalized()

static func _body_to_car_transform(body_mesh: MeshInstance3D, car_root: Node3D) -> Transform3D:
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

static func _surface_vertices(arrays: Array) -> PackedVector3Array:
	if arrays.size() <= Mesh.ARRAY_VERTEX:
		return PackedVector3Array()
	if typeof(arrays[Mesh.ARRAY_VERTEX]) != TYPE_PACKED_VECTOR3_ARRAY:
		return PackedVector3Array()
	return arrays[Mesh.ARRAY_VERTEX]

static func _surface_normals(arrays: Array) -> PackedVector3Array:
	if arrays.size() <= Mesh.ARRAY_NORMAL:
		return PackedVector3Array()
	if typeof(arrays[Mesh.ARRAY_NORMAL]) != TYPE_PACKED_VECTOR3_ARRAY:
		return PackedVector3Array()
	return arrays[Mesh.ARRAY_NORMAL]

static func _surface_indices(arrays: Array) -> PackedInt32Array:
	if arrays.size() <= Mesh.ARRAY_INDEX:
		return PackedInt32Array()
	if typeof(arrays[Mesh.ARRAY_INDEX]) != TYPE_PACKED_INT32_ARRAY:
		return PackedInt32Array()
	return arrays[Mesh.ARRAY_INDEX]

static func _axis_value(point: Vector3, axis: int) -> float:
	if axis == 0:
		return point.x
	if axis == 1:
		return point.y
	return point.z
