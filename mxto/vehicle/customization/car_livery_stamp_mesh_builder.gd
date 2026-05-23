class_name CarLiveryStampMeshBuilder
extends RefCounted

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CarStampEntry = preload("res://vehicle/customization/car_stamp_entry.gd")

const BODY_NODE_PATH := NodePath("VEHICLE_MAIN")
const SURFACE_OFFSET := 0.006
const EPSILON := 0.00001
const OCCLUSION_MAP_SIZE := 128
const OCCLUSION_DEPTH_EPSILON := 0.025
const OCCLUSION_DEPTH_EMPTY := -100000000.0
const OCCLUSION_ATLAS_COLUMNS := 4
const OCCLUSION_ATLAS_ROWS := 4

static func build_for_vehicle_scene(vehicle_root: Node3D, livery: CarLivery, catalog: CarStampCatalog) -> ArrayMesh:
	return build_for_vehicle_scene_with_masks(vehicle_root, livery, catalog)["mesh"]

static func build_for_vehicle_scene_with_masks(vehicle_root: Node3D, livery: CarLivery, catalog: CarStampCatalog, build_visibility_masks := true, visibility_mask_skip_layer := -1) -> Dictionary:
	if vehicle_root == null:
		return _empty_build_result()
	var body_mesh := vehicle_root.get_node_or_null(BODY_NODE_PATH) as MeshInstance3D
	return build_for_body_mesh_with_masks(body_mesh, livery, catalog, vehicle_root, build_visibility_masks, visibility_mask_skip_layer)

static func build_for_body_mesh(body_mesh: MeshInstance3D, livery: CarLivery, catalog: CarStampCatalog, car_root: Node3D = null) -> ArrayMesh:
	return build_for_body_mesh_with_masks(body_mesh, livery, catalog, car_root)["mesh"]

static func build_for_body_mesh_with_masks(body_mesh: MeshInstance3D, livery: CarLivery, catalog: CarStampCatalog, car_root: Node3D = null, build_visibility_masks := true, visibility_mask_skip_layer := -1) -> Dictionary:
	var out_mesh := ArrayMesh.new()
	if body_mesh == null or body_mesh.mesh == null or livery == null or catalog == null:
		return {"mesh": out_mesh, "visibility_mask": null, "stamp_vertex_ranges": {}}

	var out_vertices := PackedVector3Array()
	var out_normals := PackedVector3Array()
	var out_body_uvs := PackedVector2Array()
	var out_stamp_uvs := PackedVector2Array()
	var out_colours := PackedColorArray()
	var out_mask_data := PackedFloat32Array()
	var out_source_data := PackedFloat32Array()
	var body_to_car := _body_to_car_transform(body_mesh, car_root)
	var car_to_body := body_to_car.affine_inverse()
	var stamp_vertex_ranges := {}
	var mask_image: Image = null
	if build_visibility_masks:
		mask_image = Image.create(OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE, OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE, false, Image.FORMAT_RGF)
		mask_image.fill(Color(0.0, 0.0, 0.0, 1.0))
	var mask_slot := 0

	for stamp in livery.get_sorted_stamps():
		if stamp == null or !stamp.enabled or stamp.opacity <= 0.0:
			continue
		var atlas_rect := catalog.get_stamp_atlas_rect(stamp)
		if atlas_rect.size.x <= 0.0 or atlas_rect.size.y <= 0.0:
			continue
		if !stamp.is_custom() and catalog.get_entry(stamp.stamp_id) == null:
			continue
		var vertex_start := out_vertices.size()
		var build_stamp_visibility_mask := build_visibility_masks and stamp.layer != visibility_mask_skip_layer
		_append_stamp(body_mesh.mesh, body_to_car, car_to_body, stamp, atlas_rect, catalog.get_stamp_source_flag(stamp), stamp.custom_rect_rotated, mask_slot, mask_image, build_stamp_visibility_mask, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)
		var vertex_count := out_vertices.size() - vertex_start
		if vertex_count > 0:
			stamp_vertex_ranges[stamp.layer] = {"start": vertex_start, "count": vertex_count}
		mask_slot += 1
		if mask_slot >= OCCLUSION_ATLAS_COLUMNS * OCCLUSION_ATLAS_ROWS:
			break

	if out_vertices.is_empty():
		return {"mesh": out_mesh, "visibility_mask": null, "stamp_vertex_ranges": {}}

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = out_vertices
	arrays[Mesh.ARRAY_NORMAL] = out_normals
	arrays[Mesh.ARRAY_TEX_UV] = out_body_uvs
	arrays[Mesh.ARRAY_TEX_UV2] = out_stamp_uvs
	arrays[Mesh.ARRAY_COLOR] = out_colours
	arrays[Mesh.ARRAY_CUSTOM0] = out_mask_data
	arrays[Mesh.ARRAY_CUSTOM1] = out_source_data
	var format_flags := Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT
	format_flags |= Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT
	out_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, format_flags)
	var visibility_mask: Texture2D = ImageTexture.create_from_image(mask_image) if mask_image != null else null
	return {"mesh": out_mesh, "visibility_mask": visibility_mask, "stamp_vertex_ranges": stamp_vertex_ranges}

static func _empty_build_result() -> Dictionary:
	return {"mesh": ArrayMesh.new(), "visibility_mask": null, "stamp_vertex_ranges": {}}

static func _append_stamp(
	mesh: Mesh,
	body_to_car: Transform3D,
	car_to_body: Transform3D,
	stamp: CarLiveryStamp,
	atlas_rect: Rect2,
	source_flag: float,
	atlas_rotated: bool,
	mask_slot: int,
	mask_image: Image,
	build_visibility_masks: bool,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_body_uvs: PackedVector2Array,
	out_stamp_uvs: PackedVector2Array,
	out_colours: PackedColorArray,
	out_mask_data: PackedFloat32Array,
	out_source_data: PackedFloat32Array
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
	var depth_epsilon := maxf(OCCLUSION_DEPTH_EPSILON, half_depth * 0.02)
	var depth_range := maxf(clip_max.z - clip_min.z, EPSILON)
	var depth_epsilon_normalized := clampf(depth_epsilon / depth_range, 0.0, 1.0)
	var mask_rect := _mask_rect_for_slot(mask_slot) if mask_image != null else Rect2(0.0, 0.0, 1.0, 1.0)
	if build_visibility_masks and mask_image != null:
		var depth_map := _build_stamp_depth_map(mesh, body_to_car, car_to_projector, clip_min, clip_max)
		_write_depth_map_to_mask_image(depth_map, clip_min, clip_max, mask_slot, mask_image)

	for surface_index in range(mesh.get_surface_count()):
		var arrays := mesh.surface_get_arrays(surface_index)
		if arrays.size() <= Mesh.ARRAY_VERTEX:
			continue
		var vertices := _surface_vertices(arrays)
		if vertices.is_empty():
			continue
		var normals := _surface_normals(arrays)
		var body_uvs := _surface_uvs(arrays)
		var indices := _surface_indices(arrays)
		if indices.is_empty():
			_append_unindexed_surface(body_to_car, car_to_body, car_to_projector, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, stamp_colour, depth_epsilon_normalized, vertices, normals, body_uvs, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)
		else:
			_append_indexed_surface(body_to_car, car_to_body, car_to_projector, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, stamp_colour, depth_epsilon_normalized, vertices, normals, body_uvs, indices, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)

static func _append_indexed_surface(
	body_to_car: Transform3D,
	car_to_body: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	source_flag: float,
	atlas_rotated: bool,
	mask_rect: Rect2,
	colour: Color,
	depth_epsilon_normalized: float,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	body_uvs: PackedVector2Array,
	indices: PackedInt32Array,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_body_uvs: PackedVector2Array,
	out_stamp_uvs: PackedVector2Array,
	out_colours: PackedColorArray,
	out_mask_data: PackedFloat32Array,
	out_source_data: PackedFloat32Array
) -> void:
	var tri_count := int(indices.size() / 3)
	for tri in range(tri_count):
		var i0 := indices[tri * 3]
		var i1 := indices[tri * 3 + 1]
		var i2 := indices[tri * 3 + 2]
		_append_triangle(body_to_car, car_to_body, car_to_projector, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, colour, depth_epsilon_normalized, vertices, normals, body_uvs, i0, i1, i2, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)

static func _append_unindexed_surface(
	body_to_car: Transform3D,
	car_to_body: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	source_flag: float,
	atlas_rotated: bool,
	mask_rect: Rect2,
	colour: Color,
	depth_epsilon_normalized: float,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	body_uvs: PackedVector2Array,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_body_uvs: PackedVector2Array,
	out_stamp_uvs: PackedVector2Array,
	out_colours: PackedColorArray,
	out_mask_data: PackedFloat32Array,
	out_source_data: PackedFloat32Array
) -> void:
	var tri_count := int(vertices.size() / 3)
	for tri in range(tri_count):
		var i0 := tri * 3
		_append_triangle(body_to_car, car_to_body, car_to_projector, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, colour, depth_epsilon_normalized, vertices, normals, body_uvs, i0, i0 + 1, i0 + 2, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)

static func _append_triangle(
	body_to_car: Transform3D,
	car_to_body: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	source_flag: float,
	atlas_rotated: bool,
	mask_rect: Rect2,
	colour: Color,
	depth_epsilon_normalized: float,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	body_uvs: PackedVector2Array,
	i0: int,
	i1: int,
	i2: int,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_body_uvs: PackedVector2Array,
	out_stamp_uvs: PackedVector2Array,
	out_colours: PackedColorArray,
	out_mask_data: PackedFloat32Array,
	out_source_data: PackedFloat32Array
) -> void:
	var polygon := _clipped_triangle_polygon(body_to_car, car_to_projector, clip_min, clip_max, vertices, normals, body_uvs, i0, i1, i2)
	if polygon.size() < 3:
		return
	for i in range(1, polygon.size() - 1):
		_emit_vertex(polygon[0], car_to_body, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)
		_emit_vertex(polygon[i], car_to_body, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)
		_emit_vertex(polygon[i + 1], car_to_body, clip_min, clip_max, atlas_rect, source_flag, atlas_rotated, mask_rect, colour, depth_epsilon_normalized, out_vertices, out_normals, out_body_uvs, out_stamp_uvs, out_colours, out_mask_data, out_source_data)

static func _clipped_triangle_polygon(
	body_to_car: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	vertices: PackedVector3Array,
	normals: PackedVector3Array,
	body_uvs: PackedVector2Array,
	i0: int,
	i1: int,
	i2: int
) -> Array:
	if i0 < 0 or i1 < 0 or i2 < 0 or i0 >= vertices.size() or i1 >= vertices.size() or i2 >= vertices.size():
		return []
	var p0 := body_to_car * vertices[i0]
	var p1 := body_to_car * vertices[i1]
	var p2 := body_to_car * vertices[i2]
	var face_normal := (p1 - p0).cross(p2 - p0)
	if face_normal.length_squared() <= EPSILON:
		return []
	face_normal = face_normal.normalized()
	var body_face_normal := (vertices[i1] - vertices[i0]).cross(vertices[i2] - vertices[i0])
	if body_face_normal.length_squared() <= EPSILON:
		body_face_normal = Vector3(0.0, 1.0, 0.0)
	else:
		body_face_normal = body_face_normal.normalized()
	var n0 := _normal_at(body_to_car, normals, i0, face_normal)
	var n1 := _normal_at(body_to_car, normals, i1, face_normal)
	var n2 := _normal_at(body_to_car, normals, i2, face_normal)
	var bn0 := _body_normal_at(normals, i0, body_face_normal)
	var bn1 := _body_normal_at(normals, i1, body_face_normal)
	var bn2 := _body_normal_at(normals, i2, body_face_normal)
	var polygon := [
		{"projector_pos": car_to_projector * p0, "car_pos": p0, "body_pos": vertices[i0], "normal": n0, "body_normal": bn0, "body_uv": _uv_at(body_uvs, i0)},
		{"projector_pos": car_to_projector * p1, "car_pos": p1, "body_pos": vertices[i1], "normal": n1, "body_normal": bn1, "body_uv": _uv_at(body_uvs, i1)},
		{"projector_pos": car_to_projector * p2, "car_pos": p2, "body_pos": vertices[i2], "normal": n2, "body_normal": bn2, "body_uv": _uv_at(body_uvs, i2)},
	]
	polygon = _clip_polygon_to_box(polygon, clip_min, clip_max)
	return polygon

static func _build_stamp_depth_map(
	mesh: Mesh,
	body_to_car: Transform3D,
	car_to_projector: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3
) -> PackedFloat32Array:
	var depth_map := PackedFloat32Array()
	depth_map.resize(OCCLUSION_MAP_SIZE * OCCLUSION_MAP_SIZE)
	for i in range(depth_map.size()):
		depth_map[i] = OCCLUSION_DEPTH_EMPTY
	for surface_index in range(mesh.get_surface_count()):
		var arrays := mesh.surface_get_arrays(surface_index)
		if arrays.size() <= Mesh.ARRAY_VERTEX:
			continue
		var vertices := _surface_vertices(arrays)
		if vertices.is_empty():
			continue
		var normals := _surface_normals(arrays)
		var body_uvs := _surface_uvs(arrays)
		var indices := _surface_indices(arrays)
		if indices.is_empty():
			var tri_count := int(vertices.size() / 3)
			for tri in range(tri_count):
				var i0 := tri * 3
				var polygon := _clipped_triangle_polygon(body_to_car, car_to_projector, clip_min, clip_max, vertices, normals, body_uvs, i0, i0 + 1, i0 + 2)
				_raster_depth_polygon(polygon, clip_min, clip_max, depth_map)
		else:
			var tri_count := int(indices.size() / 3)
			for tri in range(tri_count):
				var polygon := _clipped_triangle_polygon(body_to_car, car_to_projector, clip_min, clip_max, vertices, normals, body_uvs, indices[tri * 3], indices[tri * 3 + 1], indices[tri * 3 + 2])
				_raster_depth_polygon(polygon, clip_min, clip_max, depth_map)
	return depth_map

static func _raster_depth_polygon(polygon: Array, clip_min: Vector3, clip_max: Vector3, depth_map: PackedFloat32Array) -> void:
	if polygon.size() < 3:
		return
	for i in range(1, polygon.size() - 1):
		_raster_depth_triangle(polygon[0]["projector_pos"], polygon[i]["projector_pos"], polygon[i + 1]["projector_pos"], clip_min, clip_max, depth_map)

static func _raster_depth_triangle(a: Vector3, b: Vector3, c: Vector3, clip_min: Vector3, clip_max: Vector3, depth_map: PackedFloat32Array) -> void:
	var a_pixel := _projector_pixel(a, clip_min, clip_max)
	var b_pixel := _projector_pixel(b, clip_min, clip_max)
	var c_pixel := _projector_pixel(c, clip_min, clip_max)
	var min_x := maxi(0, int(floorf(minf(a_pixel.x, minf(b_pixel.x, c_pixel.x)))))
	var max_x := mini(OCCLUSION_MAP_SIZE - 1, int(ceilf(maxf(a_pixel.x, maxf(b_pixel.x, c_pixel.x)))))
	var min_y := maxi(0, int(floorf(minf(a_pixel.y, minf(b_pixel.y, c_pixel.y)))))
	var max_y := mini(OCCLUSION_MAP_SIZE - 1, int(ceilf(maxf(a_pixel.y, maxf(b_pixel.y, c_pixel.y)))))
	if min_x > max_x or min_y > max_y:
		return
	var denom := _barycentric_denominator(a_pixel, b_pixel, c_pixel)
	if absf(denom) <= EPSILON:
		return
	for y in range(min_y, max_y + 1):
		for x in range(min_x, max_x + 1):
			var sample := Vector2(float(x) + 0.5, float(y) + 0.5)
			var bary := _barycentric(sample, a_pixel, b_pixel, c_pixel, denom)
			if bary.x < -EPSILON or bary.y < -EPSILON or bary.z < -EPSILON:
				continue
			var depth := a.z * bary.x + b.z * bary.y + c.z * bary.z
			var depth_index := y * OCCLUSION_MAP_SIZE + x
			if depth > depth_map[depth_index]:
				depth_map[depth_index] = depth

static func _write_depth_map_to_mask_image(
	depth_map: PackedFloat32Array,
	clip_min: Vector3,
	clip_max: Vector3,
	mask_slot: int,
	mask_image: Image
) -> void:
	var tile_origin := _mask_tile_origin(mask_slot)
	for y in range(OCCLUSION_MAP_SIZE):
		for x in range(OCCLUSION_MAP_SIZE):
			var depth := depth_map[y * OCCLUSION_MAP_SIZE + x]
			var encoded_depth := 0.0
			var valid := 0.0
			if depth > OCCLUSION_DEPTH_EMPTY * 0.5:
				encoded_depth = clampf(inverse_lerp(clip_min.z, clip_max.z, depth), 0.0, 1.0)
				valid = 1.0
			mask_image.set_pixel(tile_origin.x + x, tile_origin.y + y, Color(encoded_depth, valid, 0.0, 1.0))

static func _mask_tile_origin(mask_slot: int) -> Vector2i:
	var tile := Vector2i(mask_slot % OCCLUSION_ATLAS_COLUMNS, int(mask_slot / OCCLUSION_ATLAS_COLUMNS))
	return Vector2i(tile.x * OCCLUSION_MAP_SIZE, tile.y * OCCLUSION_MAP_SIZE)

static func _mask_rect_for_slot(mask_slot: int) -> Rect2:
	var atlas_size := Vector2(float(OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE), float(OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE))
	var tile_origin_i := _mask_tile_origin(mask_slot)
	var tile_origin := Vector2(float(tile_origin_i.x), float(tile_origin_i.y))
	return Rect2(tile_origin / atlas_size, Vector2(float(OCCLUSION_MAP_SIZE), float(OCCLUSION_MAP_SIZE)) / atlas_size)

static func _mask_uv_for_projector_uv(mask_rect: Rect2, projector_uv: Vector2) -> Vector2:
	var atlas_size := Vector2(float(OCCLUSION_ATLAS_COLUMNS * OCCLUSION_MAP_SIZE), float(OCCLUSION_ATLAS_ROWS * OCCLUSION_MAP_SIZE))
	var tile_pixel := projector_uv * float(OCCLUSION_MAP_SIZE - 1) + Vector2(0.5, 0.5)
	return mask_rect.position + tile_pixel / atlas_size

static func _projector_pixel(point: Vector3, clip_min: Vector3, clip_max: Vector3) -> Vector2:
	return _projector_uv(point, clip_min, clip_max) * float(OCCLUSION_MAP_SIZE - 1)

static func _projector_uv(point: Vector3, clip_min: Vector3, clip_max: Vector3) -> Vector2:
	return Vector2(
		inverse_lerp(clip_min.x, clip_max.x, point.x),
		inverse_lerp(clip_max.y, clip_min.y, point.y)
	)

static func _barycentric_denominator(a: Vector2, b: Vector2, c: Vector2) -> float:
	return (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y)

static func _barycentric(point: Vector2, a: Vector2, b: Vector2, c: Vector2, denom: float) -> Vector3:
	var w0 := ((b.y - c.y) * (point.x - c.x) + (c.x - b.x) * (point.y - c.y)) / denom
	var w1 := ((c.y - a.y) * (point.x - c.x) + (a.x - c.x) * (point.y - c.y)) / denom
	return Vector3(w0, w1, 1.0 - w0 - w1)

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
	var a_body_pos: Vector3 = a["body_pos"]
	var b_body_pos: Vector3 = b["body_pos"]
	var a_body_normal: Vector3 = a["body_normal"]
	var b_body_normal: Vector3 = b["body_normal"]
	var a_body_uv: Vector2 = a["body_uv"]
	var b_body_uv: Vector2 = b["body_uv"]
	return {
		"projector_pos": a_pos.lerp(b_pos, t),
		"car_pos": a_car_pos.lerp(b_car_pos, t),
		"body_pos": a_body_pos.lerp(b_body_pos, t),
		"normal": normal.normalized(),
		"body_normal": a_body_normal.lerp(b_body_normal, t).normalized(),
		"body_uv": a_body_uv.lerp(b_body_uv, t),
	}

static func _emit_vertex(
	vertex: Dictionary,
	car_to_body: Transform3D,
	clip_min: Vector3,
	clip_max: Vector3,
	atlas_rect: Rect2,
	source_flag: float,
	atlas_rotated: bool,
	mask_rect: Rect2,
	colour: Color,
	depth_epsilon_normalized: float,
	out_vertices: PackedVector3Array,
	out_normals: PackedVector3Array,
	out_body_uvs: PackedVector2Array,
	out_stamp_uvs: PackedVector2Array,
	out_colours: PackedColorArray,
	out_mask_data: PackedFloat32Array,
	out_source_data: PackedFloat32Array
) -> void:
	var normal: Vector3 = vertex["normal"]
	var car_pos: Vector3 = vertex["car_pos"]
	var projector_pos: Vector3 = vertex["projector_pos"]
	var projector_uv := _projector_uv(projector_pos, clip_min, clip_max)
	var atlas_uv := _atlas_uv_for_projector_uv(atlas_rect, projector_uv, atlas_rotated)
	var mask_uv := _mask_uv_for_projector_uv(mask_rect, projector_uv)
	var projector_depth := clampf(inverse_lerp(clip_min.z, clip_max.z, projector_pos.z), 0.0, 1.0)
	out_vertices.append(car_to_body * (car_pos + normal * SURFACE_OFFSET))
	out_normals.append(vertex["body_normal"])
	out_body_uvs.append(vertex["body_uv"])
	out_stamp_uvs.append(atlas_uv)
	out_colours.append(colour)
	out_mask_data.append(mask_uv.x)
	out_mask_data.append(mask_uv.y)
	out_mask_data.append(projector_depth)
	out_mask_data.append(depth_epsilon_normalized)
	out_source_data.append(source_flag)
	out_source_data.append(0.0)
	out_source_data.append(0.0)
	out_source_data.append(0.0)

static func _atlas_uv_for_projector_uv(atlas_rect: Rect2, projector_uv: Vector2, atlas_rotated: bool) -> Vector2:
	if atlas_rotated:
		return atlas_rect.position + Vector2(projector_uv.y, 1.0 - projector_uv.x) * atlas_rect.size
	return atlas_rect.position + projector_uv * atlas_rect.size

static func _normal_at(body_to_car: Transform3D, normals: PackedVector3Array, index: int, fallback: Vector3) -> Vector3:
	if index < 0 or index >= normals.size():
		return fallback
	var normal := body_to_car.basis * normals[index]
	if normal.length_squared() <= EPSILON:
		return fallback
	return normal.normalized()

static func _body_normal_at(normals: PackedVector3Array, index: int, fallback: Vector3) -> Vector3:
	if index < 0 or index >= normals.size():
		return fallback
	var normal := normals[index]
	if normal.length_squared() <= EPSILON:
		return fallback
	return normal.normalized()

static func _uv_at(uvs: PackedVector2Array, index: int) -> Vector2:
	if index < 0 or index >= uvs.size():
		return Vector2.ZERO
	return uvs[index]

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

static func _surface_uvs(arrays: Array) -> PackedVector2Array:
	if arrays.size() <= Mesh.ARRAY_TEX_UV:
		return PackedVector2Array()
	if typeof(arrays[Mesh.ARRAY_TEX_UV]) != TYPE_PACKED_VECTOR2_ARRAY:
		return PackedVector2Array()
	return arrays[Mesh.ARRAY_TEX_UV]

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
