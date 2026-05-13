class_name SpiralCurveGizmo extends Node3D

enum CurveKind {
	RADIUS,
	HEIGHT,
	TWIST,
	SCALE_X,
	SCALE_Y,
}

enum HandleKind {
	POINT,
	LEFT_TANGENT,
	RIGHT_TANGENT,
	AXIS_POLE,
}

const HANDLE_RADIUS := 4.0
const TANGENT_HANDLE_RADIUS := 2.5
const CUBE_HANDLE_SIZE := 8.0
const SEARCH_STEPS := 10
const SEARCH_PASSES := 5
const PREVIEW_STEPS := 32
const CIRCLE_STEPS := 48
const MIN_POINT_GAP := 0.001
const TANGENT_HANDLE_OFFSET := 0.08
const ADD_POINT_MAX_SCREEN_DISTANCE := 60.0
const VISUAL_OFFSET := 8.0
const TWIST_VISUAL_OFFSET := VISUAL_OFFSET * 0.5
const TANGENT_VISUAL_SPAN := 16.0
const AXIS_POLE_LENGTH := 160.0
const ARROW_LENGTH := 18.0
const KEY_ARROW_HEAD_LENGTH := 6.0
const KEY_ARROW_HEAD_WIDTH := 4.0
const AXIS_POLE_HIT_RADIUS := 6.0

var mouse_cast : RayCast3D
var target_path : RoadPath
var dragging := false
var drag_handle := -1
var delete_pressed := false
var drag_snapshot := {}
var selected_entry_index := -1
var selected_point_index := -1
var hovered_entry_index := -1
var hovered_point_index := -1

var outline_mesh_instance : MeshInstance3D
var outline_mesh := ImmediateMesh.new()
var outline_material := StandardMaterial3D.new()
var cube_handle_mesh := BoxMesh.new()
var cube_handle_shape := BoxShape3D.new()
var point_handle_mesh := SphereMesh.new()
var point_handle_shape := SphereShape3D.new()
var axis_handle_shape := CapsuleShape3D.new()
var tangent_handle_mesh := SphereMesh.new()
var tangent_handle_shape := SphereShape3D.new()
var arrow_handle_mesh := ArrayMesh.new()

var handle_records : Array[Dictionary] = []
var handles : Array[StaticBody3D] = []
var handle_materials : Array[StandardMaterial3D] = []
var handle_mesh_instances : Array[MeshInstance3D] = []
var handle_collision_shapes : Array[CollisionShape3D] = []
var handle_shape_keys : Array[String] = []
var selection_records : Array[Dictionary] = []
var selection_bodies : Array[StaticBody3D] = []
var selection_collision_shapes : Array[CollisionShape3D] = []
var curve_entries : Array[Dictionary] = []

func _ready() -> void:
	outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	outline_material.vertex_color_use_as_albedo = true
	cube_handle_mesh.size = Vector3.ONE * CUBE_HANDLE_SIZE
	cube_handle_shape.size = Vector3.ONE * CUBE_HANDLE_SIZE
	point_handle_mesh.radius = HANDLE_RADIUS
	point_handle_mesh.height = HANDLE_RADIUS * 2.0
	point_handle_shape.radius = HANDLE_RADIUS
	axis_handle_shape.radius = AXIS_POLE_HIT_RADIUS
	axis_handle_shape.height = AXIS_POLE_LENGTH * 2.0
	tangent_handle_mesh.radius = TANGENT_HANDLE_RADIUS
	tangent_handle_mesh.height = TANGENT_HANDLE_RADIUS * 2.0
	tangent_handle_shape.radius = TANGENT_HANDLE_RADIUS
	_build_arrow_mesh()
	outline_mesh_instance = MeshInstance3D.new()
	outline_mesh_instance.mesh = outline_mesh
	outline_mesh_instance.top_level = true
	add_child(outline_mesh_instance)
	set_target_path(null)

func set_target_path(in_path : RoadPath) -> void:
	target_path = in_path
	dragging = false
	drag_handle = -1
	drag_snapshot.clear()
	selected_entry_index = -1
	selected_point_index = -1
	hovered_entry_index = -1
	hovered_point_index = -1

func _is_spiral_path(path : Node) -> bool:
	var script : Script = path.get_script() as Script if path else null
	return script and script.resource_path == "res://core/road_path_spiral.gd"

func _curve_color(index : int) -> Color:
	if index >= 0 and index < curve_entries.size():
		match int(curve_entries[index]["kind"]):
			CurveKind.RADIUS, CurveKind.SCALE_X:
				return Color(0.92, 0.24, 0.18, 1.0)
			CurveKind.HEIGHT, CurveKind.SCALE_Y:
				return Color(0.28, 0.82, 0.28, 1.0)
			CurveKind.TWIST:
				return Color(0.24, 0.48, 1.0, 1.0)
	return Color(0.72, 0.72, 0.72, 1.0)

func _curve_hover_color(index : int) -> Color:
	return _curve_color(index).lerp(Color.WHITE, 0.45)

func _grey() -> Color:
	return Color(0.55, 0.55, 0.55, 0.78)

func _collect_curve_entries() -> void:
	curve_entries.clear()
	if !_is_spiral_path(target_path):
		return
	target_path.call("_ensure_spiral_curves")
	curve_entries.append({"name": "radius", "kind": CurveKind.RADIUS, "curve": target_path.get("radius_curve"), "min": 0.0})
	curve_entries.append({"name": "height", "kind": CurveKind.HEIGHT, "curve": target_path.get("height_curve"), "min": -INF})
	curve_entries.append({"name": "twist", "kind": CurveKind.TWIST, "curve": target_path.get("twist_curve"), "min": -INF})
	curve_entries.append({"name": "scale_x", "kind": CurveKind.SCALE_X, "curve": target_path.get("scale_x_curve"), "min": 0.05})
	curve_entries.append({"name": "scale_y", "kind": CurveKind.SCALE_Y, "curve": target_path.get("scale_y_curve"), "min": 0.05})
	for entry in curve_entries:
		_ensure_curve(entry["curve"], float(entry["min"]))

func _ensure_curve(curve : Resource, min_value : float) -> void:
	while curve.point_count < 2:
		curve.add_point(Vector2(float(curve.point_count), maxf(0.0, min_value)))
	curve.set_point_offset(0, 0.0)
	curve.set_point_offset(curve.point_count - 1, 1.0)

func _curve_offset(curve : Resource, point_index : int) -> float:
	if !curve or curve.point_count <= point_index:
		return 0.0
	return curve.get_point_position(point_index).x

func _curve_value(curve : Resource, point_index : int) -> float:
	if !curve or curve.point_count <= point_index:
		return 0.0
	return curve.get_point_position(point_index).y

func _sample_transform(t : float) -> Transform3D:
	if _is_spiral_path(target_path):
		return target_path.get_root_transform(clampf(t, 0.0, 1.0))
	return Transform3D.IDENTITY

func _sample_frame(t : float) -> Dictionary:
	var transform := _sample_transform(t)
	var basis := transform.basis
	return {
		"transform": transform,
		"center": transform.origin,
		"x": basis.x.normalized(),
		"y": basis.y.normalized(),
		"z": basis.z.normalized(),
		"scale_x": basis.x.length(),
		"scale_y": basis.y.length(),
	}

func _axis_transform() -> Transform3D:
	if !_is_spiral_path(target_path):
		return Transform3D.IDENTITY
	return target_path.get("axis_transform")

func _spiral_axis_local() -> Vector3:
	if !_is_spiral_path(target_path):
		return Vector3.UP
	var axis : Vector3 = target_path.get("spiral_axis")
	axis.z = 0.0
	if axis.length_squared() <= 0.00001:
		return Vector3.UP
	return axis.normalized()

func _spiral_axis_world() -> Vector3:
	var transform := _axis_transform()
	return (transform.basis * _spiral_axis_local()).normalized()

func _axis_pole_center() -> Vector3:
	var transform := _axis_transform()
	var axis := _spiral_axis_world()
	var z := transform.basis.z.normalized()
	var center_dir := axis.cross(z)
	if center_dir.length_squared() <= 0.00001:
		center_dir = transform.basis.x.normalized()
	else:
		center_dir = center_dir.normalized()
	var radius_curve : Resource = target_path.get("radius_curve") if _is_spiral_path(target_path) else null
	var radius : float = radius_curve.sample(0.5) if radius_curve else 64.0
	return transform.origin + center_dir * radius

func _axis_handle_position() -> Vector3:
	return _axis_pole_center()

func _radius_axis(frame : Dictionary) -> Vector3:
	var pole_axis := _spiral_axis_world()
	var pole_center := _axis_pole_center()
	var center : Vector3 = frame["center"]
	var frame_x : Vector3 = frame["x"]
	var frame_z : Vector3 = frame["z"]
	var closest_pole_point := pole_center + pole_axis * (center - pole_center).dot(pole_axis)
	var axis := closest_pole_point - center
	if axis.length_squared() <= 0.00001:
		axis = frame_x - pole_axis * frame_x.dot(pole_axis)
	if axis.length_squared() <= 0.00001:
		axis = frame_z.cross(pole_axis)
	if axis.length_squared() <= 0.00001:
		return frame_x.normalized()
	return axis.normalized()

func _point_sides(kind : int) -> Array[float]:
	if kind == CurveKind.SCALE_X or kind == CurveKind.SCALE_Y or kind == CurveKind.RADIUS or kind == CurveKind.HEIGHT:
		return [-1.0, 1.0]
	return [1.0]

func _entry_axis(entry : Dictionary, frame : Dictionary) -> Vector3:
	match int(entry["kind"]):
		CurveKind.RADIUS:
			return _radius_axis(frame)
		CurveKind.HEIGHT:
			return _spiral_axis_world()
		CurveKind.SCALE_Y:
			return frame["y"]
		_:
			return frame["x"]

func _entry_plane_normal(entry : Dictionary, frame : Dictionary) -> Vector3:
	match int(entry["kind"]):
		CurveKind.RADIUS:
			var frame_z : Vector3 = frame["z"]
			var normal := _radius_axis(frame).cross(frame_z)
			if normal.length_squared() <= 0.00001:
				return _spiral_axis_world()
			return normal.normalized()
		CurveKind.HEIGHT:
			return frame["x"]
		CurveKind.SCALE_X:
			return frame["y"]
		CurveKind.SCALE_Y:
			return frame["x"]
		_:
			return frame["y"]

func _scale_value_at(kind : int, t : float) -> float:
	for entry in curve_entries:
		if int(entry["kind"]) == kind:
			return entry["curve"].sample(t)
	return 25.0

func _twist_radius(t : float) -> float:
	return maxf(_scale_value_at(CurveKind.SCALE_X, t), _scale_value_at(CurveKind.SCALE_Y, t)) + TWIST_VISUAL_OFFSET

func _tangent_delta(curve : Resource, point_index : int, handle_kind : int) -> float:
	var point_t := _curve_offset(curve, point_index)
	if handle_kind == HandleKind.LEFT_TANGENT:
		return -minf(TANGENT_HANDLE_OFFSET, point_t)
	return minf(TANGENT_HANDLE_OFFSET, 1.0 - point_t)

func _tangent_value(curve : Resource, point_index : int, handle_kind : int) -> float:
	var point : Vector2 = curve.get_point_position(point_index)
	var delta : float = _tangent_delta(curve, point_index, handle_kind)
	var tangent : float = curve.get_point_left_tangent(point_index) if handle_kind == HandleKind.LEFT_TANGENT else curve.get_point_right_tangent(point_index)
	return point.y + tangent * delta

func _point_handle_position(record : Dictionary) -> Vector3:
	var entry := curve_entries[int(record["entry"])]
	var curve : Resource = entry["curve"]
	var point_index := int(record["point"])
	var side := float(record.get("side", 1.0))
	var t := _curve_offset(curve, point_index)
	var value := _curve_value(curve, point_index)
	var frame := _sample_frame(t)
	match int(entry["kind"]):
		CurveKind.TWIST:
			return frame["center"] + frame["x"] * _twist_radius(t)
		CurveKind.SCALE_X, CurveKind.SCALE_Y:
			return frame["center"] + _entry_axis(entry, frame) * side * (value + VISUAL_OFFSET)
		_:
			return frame["center"] + _entry_axis(entry, frame) * side * ARROW_LENGTH

func _tangent_handle_position(record : Dictionary) -> Vector3:
	var entry := curve_entries[int(record["entry"])]
	var curve : Resource = entry["curve"]
	var point_index := int(record["point"])
	var side := float(record.get("side", 1.0))
	var point : Vector2 = curve.get_point_position(point_index)
	var delta := _tangent_delta(curve, point_index, int(record["kind"]))
	var tangent_t := clampf(point.x + delta, 0.0, 1.0)
	var tangent_value := _tangent_value(curve, point_index, int(record["kind"]))
	var frame := _sample_frame(tangent_t)
	var point_frame := _sample_frame(point.x)
	match int(entry["kind"]):
		CurveKind.TWIST:
			return frame["center"] + frame["x"] * _twist_radius(tangent_t)
		CurveKind.SCALE_X, CurveKind.SCALE_Y:
			return frame["center"] + _entry_axis(entry, frame) * side * (tangent_value + VISUAL_OFFSET)
		_:
			return point_frame["center"] + point_frame["z"] * signf(delta) * TANGENT_VISUAL_SPAN + _entry_axis(entry, point_frame) * side * (tangent_value - point.y)

func _handle_position(handle_id : int) -> Vector3:
	var record := handle_records[handle_id]
	if int(record["kind"]) == HandleKind.AXIS_POLE:
		return _axis_handle_position()
	if int(record["kind"]) == HandleKind.POINT:
		return _point_handle_position(record)
	return _tangent_handle_position(record)

func _handle_base_color(handle_id : int) -> Color:
	var record := handle_records[handle_id]
	if int(record["kind"]) == HandleKind.AXIS_POLE:
		return Color(0.85, 0.55, 1.0, 1.0)
	var color := _curve_color(int(record["entry"]))
	return color if int(record["kind"]) == HandleKind.POINT else color.lerp(Color.WHITE, 0.35)

func _record_wants_cube(record : Dictionary) -> bool:
	if int(record["kind"]) != HandleKind.POINT:
		return false
	var entry := curve_entries[int(record["entry"])]
	return int(entry["kind"]) == CurveKind.SCALE_X or int(entry["kind"]) == CurveKind.SCALE_Y

func _record_wants_arrow(record : Dictionary) -> bool:
	if int(record["kind"]) != HandleKind.POINT or !record.has("entry"):
		return false
	var entry := curve_entries[int(record["entry"])]
	return int(entry["kind"]) == CurveKind.RADIUS or int(entry["kind"]) == CurveKind.HEIGHT

func _entry_uses_side_drag(entry : Dictionary) -> bool:
	var kind := int(entry["kind"])
	return kind == CurveKind.SCALE_X or kind == CurveKind.SCALE_Y

func _record_is_key_point(record : Dictionary) -> bool:
	return int(record["kind"]) == HandleKind.POINT and record.has("entry") and record.has("point")

func _record_key_is_selected(record : Dictionary) -> bool:
	if !record.has("entry") or !record.has("point"):
		return true
	return int(record["entry"]) == selected_entry_index and int(record["point"]) == selected_point_index

func _selected_key_matches(entry_index : int, point_index : int) -> bool:
	return selected_entry_index == entry_index and selected_point_index == point_index

func _hovered_key_matches(entry_index : int, point_index : int) -> bool:
	return hovered_entry_index == entry_index and hovered_point_index == point_index

func _select_record_key(record : Dictionary) -> void:
	if !record.has("entry") or !record.has("point"):
		return
	selected_entry_index = int(record["entry"])
	selected_point_index = int(record["point"])

func _set_hovered_key_from_handle(handle_id : int) -> bool:
	var previous_entry := hovered_entry_index
	var previous_point := hovered_point_index
	hovered_entry_index = -1
	hovered_point_index = -1
	if handle_id >= 0:
		var record := handle_records[handle_id]
		if _record_is_key_point(record):
			hovered_entry_index = int(record["entry"])
			hovered_point_index = int(record["point"])
	return previous_entry != hovered_entry_index or previous_point != hovered_point_index

func _handle_id_for_selected_key(record : Dictionary) -> int:
	for i in handle_records.size():
		var handle_record := handle_records[i]
		if !_record_is_key_point(handle_record):
			continue
		if int(handle_record["entry"]) == int(record["entry"]) and int(handle_record["point"]) == int(record["point"]):
			return i
	return -1

func _record_mesh_visible(record : Dictionary) -> bool:
	if int(record["kind"]) == HandleKind.AXIS_POLE:
		return true
	if !_record_key_is_selected(record):
		return false
	if _record_is_key_point(record):
		var entry := curve_entries[int(record["entry"])]
		return int(entry["kind"]) != CurveKind.TWIST
	return true

func _build_arrow_mesh() -> void:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var indices := PackedInt32Array()
	var ring_count := 16
	var shaft_radius := 1.4
	var shaft_start := -8.0
	var shaft_end := 1.5
	var head_radius := 4.4
	var head_base := 1.5
	var head_tip := 9.5
	for i in ring_count:
		var angle := TAU * float(i) / float(ring_count)
		var dir := Vector3(cos(angle), 0.0, sin(angle))
		vertices.append(dir * shaft_radius + Vector3(0.0, shaft_start, 0.0))
		normals.append(dir)
		vertices.append(dir * shaft_radius + Vector3(0.0, shaft_end, 0.0))
		normals.append(dir)
		vertices.append(dir * head_radius + Vector3(0.0, head_base, 0.0))
		normals.append((dir * head_tip + Vector3.UP * head_radius).normalized())
	vertices.append(Vector3(0.0, head_tip, 0.0))
	normals.append(Vector3.UP)
	var tip_index := vertices.size() - 1
	vertices.append(Vector3(0.0, shaft_start, 0.0))
	normals.append(Vector3.DOWN)
	var bottom_cap_index := vertices.size() - 1
	for i in ring_count:
		var next := (i + 1) % ring_count
		var shaft_0 := i * 3
		var shaft_1 := next * 3
		var shaft_0_top := shaft_0 + 1
		var shaft_1_top := shaft_1 + 1
		var head_0 := shaft_0 + 2
		var head_1 := shaft_1 + 2
		indices.append_array([shaft_0, shaft_1, shaft_0_top, shaft_1, shaft_1_top, shaft_0_top])
		indices.append_array([bottom_cap_index, shaft_0, shaft_1])
		indices.append_array([shaft_0_top, shaft_1_top, head_0, shaft_1_top, head_1, head_0])
		indices.append_array([head_0, head_1, tip_index])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = indices
	arrow_handle_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

func _make_handle() -> StaticBody3D:
	var body := StaticBody3D.new()
	body.set_collision_layer_value(16, true)
	body.set_collision_mask_value(16, true)
	body.set_collision_layer_value(1, false)
	body.set_collision_mask_value(1, false)
	var collision := CollisionShape3D.new()
	body.add_child(collision)
	handle_collision_shapes.append(collision)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	handle_materials.append(material)
	var mesh_instance := MeshInstance3D.new()
	mesh_instance.material_override = material
	body.add_child(mesh_instance)
	handle_mesh_instances.append(mesh_instance)
	handle_shape_keys.append("")
	add_child(body)
	return body

func _make_selection_body() -> StaticBody3D:
	var body := StaticBody3D.new()
	body.set_collision_layer_value(15, true)
	body.set_collision_mask_value(15, true)
	body.set_collision_layer_value(1, false)
	body.set_collision_mask_value(1, false)
	var collision := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 4.0
	capsule.height = 8.0
	collision.shape = capsule
	body.add_child(collision)
	selection_collision_shapes.append(collision)
	add_child(body)
	return body

func _configure_handle(handle_id : int) -> void:
	var record := handle_records[handle_id]
	var collision := handle_collision_shapes[handle_id]
	var mesh_instance := handle_mesh_instances[handle_id]
	mesh_instance.visible = _record_mesh_visible(record)
	collision.disabled = _record_is_key_point(record) and !_record_key_is_selected(record)
	if int(record["kind"]) == HandleKind.AXIS_POLE:
		if handle_shape_keys[handle_id] == "axis_pole":
			return
		handle_shape_keys[handle_id] = "axis_pole"
		collision.shape = axis_handle_shape
		mesh_instance.mesh = null
	elif _record_wants_cube(record):
		if handle_shape_keys[handle_id] == "cube":
			return
		handle_shape_keys[handle_id] = "cube"
		collision.shape = cube_handle_shape
		mesh_instance.mesh = cube_handle_mesh
	elif _record_wants_arrow(record):
		if handle_shape_keys[handle_id] == "arrow":
			return
		handle_shape_keys[handle_id] = "arrow"
		collision.shape = point_handle_shape
		mesh_instance.mesh = arrow_handle_mesh
	else:
		var point_sized := int(record["kind"]) == HandleKind.POINT or int(record["kind"]) == HandleKind.AXIS_POLE
		var key := "sphere_point" if point_sized else "sphere_tangent"
		if handle_shape_keys[handle_id] == key:
			return
		handle_shape_keys[handle_id] = key
		collision.shape = point_handle_shape if point_sized else tangent_handle_shape
		mesh_instance.mesh = point_handle_mesh if point_sized else tangent_handle_mesh

func _sync_handles() -> void:
	handle_records.clear()
	if _is_spiral_path(target_path):
		handle_records.append({"kind": HandleKind.AXIS_POLE})
	for entry_index in curve_entries.size():
		var entry := curve_entries[entry_index]
		var curve : Resource = entry["curve"]
		if selected_entry_index == entry_index and selected_point_index >= curve.point_count:
			selected_entry_index = -1
			selected_point_index = -1
		for point_index in curve.point_count:
			for side in _point_sides(int(entry["kind"])):
				handle_records.append({"kind": HandleKind.POINT, "entry": entry_index, "point": point_index, "side": side})
			if !_selected_key_matches(entry_index, point_index):
				continue
			if point_index > 0:
				for side in _point_sides(int(entry["kind"])):
					handle_records.append({"kind": HandleKind.LEFT_TANGENT, "entry": entry_index, "point": point_index, "side": side})
			if point_index < curve.point_count - 1:
				for side in _point_sides(int(entry["kind"])):
					handle_records.append({"kind": HandleKind.RIGHT_TANGENT, "entry": entry_index, "point": point_index, "side": side})
	while handles.size() < handle_records.size():
		handles.append(_make_handle())
	while handles.size() > handle_records.size():
		var handle : StaticBody3D = handles.pop_back()
		handle_materials.pop_back()
		handle_mesh_instances.pop_back()
		handle_collision_shapes.pop_back()
		handle_shape_keys.pop_back()
		handle.queue_free()
	for i in handles.size():
		_configure_handle(i)
		handle_materials[i].albedo_color = _handle_base_color(i)

func _basis_from_y_axis(axis : Vector3) -> Basis:
	var y_axis := axis.normalized()
	if y_axis.length_squared() <= 0.00001:
		return Basis.IDENTITY
	var x_axis := y_axis.cross(Vector3.FORWARD)
	if x_axis.length_squared() <= 0.00001:
		x_axis = y_axis.cross(Vector3.RIGHT)
	x_axis = x_axis.normalized()
	var z_axis := x_axis.cross(y_axis).normalized()
	return Basis(x_axis, y_axis, z_axis).orthonormalized()

func _handle_basis(handle_id : int) -> Basis:
	var record := handle_records[handle_id]
	if int(record["kind"]) == HandleKind.AXIS_POLE:
		return _basis_from_y_axis(_spiral_axis_world())
	if !_record_wants_arrow(record):
		return Basis.IDENTITY
	var entry := curve_entries[int(record["entry"])]
	var curve : Resource = entry["curve"]
	var frame := _sample_frame(_curve_offset(curve, int(record["point"])))
	var axis := _entry_axis(entry, frame) * float(record.get("side", 1.0))
	return _basis_from_y_axis(axis)

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _set_selection_colliders_enabled(enabled : bool) -> void:
	for body in selection_bodies:
		body.set_collision_layer_value(15, enabled)

func _selection_handle_from_collider(collider : Object) -> int:
	for i in selection_bodies.size():
		if collider == selection_bodies[i]:
			return i
	return -1

func _handle_from_collider(collider : Object) -> int:
	for i in handles.size():
		if collider == handles[i]:
			return i
	return -1

func select_keyframe_from_selection_collider(collider : Object) -> bool:
	var selection_id := _selection_handle_from_collider(collider)
	if selection_id < 0 or selection_id >= selection_records.size():
		return false
	var record := selection_records[selection_id]
	selected_entry_index = int(record["entry"])
	selected_point_index = int(record["point"])
	hovered_entry_index = selected_entry_index
	hovered_point_index = selected_point_index
	_update_visuals()
	return true

func _append_selection_segment(entry_index : int, point_index : int, a : Vector3, b : Vector3) -> void:
	var length := a.distance_to(b)
	if length <= 0.001:
		return
	var selection_id := selection_records.size()
	selection_records.append({"entry": entry_index, "point": point_index})
	while selection_bodies.size() <= selection_id:
		selection_bodies.append(_make_selection_body())
	var body := selection_bodies[selection_id]
	var collision := selection_collision_shapes[selection_id]
	var shape := collision.shape as CapsuleShape3D
	shape.radius = 4.0
	shape.height = maxf(length, shape.radius * 2.0)
	collision.disabled = false
	body.global_transform = Transform3D(_basis_from_y_axis(b - a), (a + b) * 0.5)
	body.set_collision_layer_value(15, true)

func _arrow_head_wing(axis : Vector3, frame : Dictionary) -> Vector3:
	var wing : Vector3 = frame["z"]
	if absf(wing.normalized().dot(axis.normalized())) > 0.95:
		wing = frame["y"]
	if absf(wing.normalized().dot(axis.normalized())) > 0.95:
		wing = frame["x"]
	return wing.normalized()

func _append_selection_arrow(entry_index : int, point_index : int, center : Vector3, axis : Vector3, frame : Dictionary) -> void:
	var direction := axis.normalized()
	var wing := _arrow_head_wing(direction, frame)
	var negative_tip := center - direction * ARROW_LENGTH
	var positive_tip := center + direction * ARROW_LENGTH
	var negative_base := negative_tip + direction * KEY_ARROW_HEAD_LENGTH
	var positive_base := positive_tip - direction * KEY_ARROW_HEAD_LENGTH
	_append_selection_segment(entry_index, point_index, negative_tip, positive_tip)
	_append_selection_segment(entry_index, point_index, negative_tip, negative_base + wing * KEY_ARROW_HEAD_WIDTH)
	_append_selection_segment(entry_index, point_index, negative_tip, negative_base - wing * KEY_ARROW_HEAD_WIDTH)
	_append_selection_segment(entry_index, point_index, positive_tip, positive_base + wing * KEY_ARROW_HEAD_WIDTH)
	_append_selection_segment(entry_index, point_index, positive_tip, positive_base - wing * KEY_ARROW_HEAD_WIDTH)

func _append_selection_circle(entry_index : int, point_index : int, center : Vector3, x_axis : Vector3, y_axis : Vector3, radius : float) -> void:
	var previous := center + x_axis * radius
	for i in CIRCLE_STEPS:
		var angle := TAU * float(i + 1) / float(CIRCLE_STEPS)
		var next := center + (x_axis * cos(angle) + y_axis * sin(angle)) * radius
		_append_selection_segment(entry_index, point_index, previous, next)
		previous = next

func _sync_selection_collisions() -> void:
	selection_records.clear()
	for entry_index in curve_entries.size():
		var entry := curve_entries[entry_index]
		var curve : Resource = entry["curve"]
		for point_index in curve.point_count:
			var t := _curve_offset(curve, point_index)
			var value := _curve_value(curve, point_index)
			var frame := _sample_frame(t)
			match int(entry["kind"]):
				CurveKind.TWIST:
					_append_selection_circle(entry_index, point_index, frame["center"], frame["x"], frame["y"], _twist_radius(t))
				CurveKind.SCALE_X, CurveKind.SCALE_Y:
					var axis := _entry_axis(entry, frame)
					for side in [-1.0, 1.0]:
						var handle_pos : Vector3 = frame["center"] + axis * side * (value + VISUAL_OFFSET)
						_append_selection_segment(entry_index, point_index, handle_pos - frame["z"] * 8.0, handle_pos + frame["z"] * 8.0)
				_:
					var axis := _entry_axis(entry, frame)
					_append_selection_arrow(entry_index, point_index, frame["center"], axis, frame)
	for i in selection_bodies.size():
		var active := i < selection_records.size()
		selection_bodies[i].set_collision_layer_value(15, active)
		selection_collision_shapes[i].disabled = !active

func _draw_line(a : Vector3, b : Vector3, color : Color) -> void:
	outline_mesh.surface_set_color(color)
	outline_mesh.surface_add_vertex(a)
	outline_mesh.surface_add_vertex(b)

func _world_per_screen_pixel(world_position : Vector3) -> float:
	var cam := FZGlobal.current_cam
	if !cam:
		return 1.0
	var viewport_height := maxf(1.0, get_viewport().get_visible_rect().size.y)
	var distance := maxf(1.0, cam.global_position.distance_to(world_position))
	return 2.0 * distance * tan(deg_to_rad(cam.fov) * 0.5) / viewport_height

func _draw_key_line(a : Vector3, b : Vector3, color : Color, hovered : bool) -> void:
	if !hovered:
		_draw_line(a, b, color)
		return
	var cam := FZGlobal.current_cam
	if !cam:
		_draw_line(a, b, Color.WHITE)
		return
	var pixel := _world_per_screen_pixel((a + b) * 0.5)
	var offsets : Array[Vector3] = [
		Vector3.ZERO,
		cam.global_basis.x.normalized() * pixel * 1.5,
		-cam.global_basis.x.normalized() * pixel * 1.5,
		cam.global_basis.y.normalized() * pixel * 1.5,
		-cam.global_basis.y.normalized() * pixel * 1.5,
	]
	for offset in offsets:
		_draw_line(a + offset, b + offset, Color.WHITE)

func _draw_circle(center : Vector3, x_axis : Vector3, y_axis : Vector3, radius : float, color : Color, start_angle := 0.0, end_angle := TAU, steps := CIRCLE_STEPS) -> void:
	var previous := center + (x_axis * cos(start_angle) + y_axis * sin(start_angle)) * radius
	for i in steps:
		var t := float(i + 1) / float(steps)
		var angle := lerpf(start_angle, end_angle, t)
		var next := center + (x_axis * cos(angle) + y_axis * sin(angle)) * radius
		_draw_line(previous, next, color)
		previous = next

func _draw_key_circle(center : Vector3, x_axis : Vector3, y_axis : Vector3, radius : float, color : Color, hovered : bool) -> void:
	var previous := center + x_axis * radius
	for i in CIRCLE_STEPS:
		var angle := TAU * float(i + 1) / float(CIRCLE_STEPS)
		var next := center + (x_axis * cos(angle) + y_axis * sin(angle)) * radius
		_draw_key_line(previous, next, color, hovered)
		previous = next

func _draw_key_arrow(center : Vector3, axis : Vector3, frame : Dictionary, color : Color, hovered : bool) -> void:
	var direction := axis.normalized()
	var wing := _arrow_head_wing(direction, frame)
	var negative_tip := center - direction * ARROW_LENGTH
	var positive_tip := center + direction * ARROW_LENGTH
	var negative_base := negative_tip + direction * KEY_ARROW_HEAD_LENGTH
	var positive_base := positive_tip - direction * KEY_ARROW_HEAD_LENGTH
	_draw_key_line(negative_tip, positive_tip, color, hovered)
	_draw_key_line(negative_tip, negative_base + wing * KEY_ARROW_HEAD_WIDTH, color, hovered)
	_draw_key_line(negative_tip, negative_base - wing * KEY_ARROW_HEAD_WIDTH, color, hovered)
	_draw_key_line(positive_tip, positive_base + wing * KEY_ARROW_HEAD_WIDTH, color, hovered)
	_draw_key_line(positive_tip, positive_base - wing * KEY_ARROW_HEAD_WIDTH, color, hovered)

func _draw_axis_pole() -> void:
	var center := _axis_pole_center()
	var axis := _spiral_axis_world()
	_draw_line(center - axis * AXIS_POLE_LENGTH, center + axis * AXIS_POLE_LENGTH, Color(0.85, 0.55, 1.0, 0.9))
	_draw_line(_axis_transform().origin, center, _grey())

func _draw_scale_point(entry_index : int, entry : Dictionary, point_index : int) -> void:
	var curve : Resource = entry["curve"]
	var t := _curve_offset(curve, point_index)
	var value := _curve_value(curve, point_index)
	var frame := _sample_frame(t)
	var axis := _entry_axis(entry, frame)
	var hovered := _hovered_key_matches(entry_index, point_index)
	var color := _curve_color(entry_index)
	for side in [-1.0, 1.0]:
		var handle_pos : Vector3 = frame["center"] + axis * side * (value + VISUAL_OFFSET)
		_draw_key_line(handle_pos - frame["z"] * 8.0, handle_pos + frame["z"] * 8.0, color, hovered)

func _draw_arrow_point(entry_index : int, entry : Dictionary, point_index : int) -> void:
	var curve : Resource = entry["curve"]
	var t := _curve_offset(curve, point_index)
	var frame := _sample_frame(t)
	var axis := _entry_axis(entry, frame)
	_draw_key_arrow(frame["center"], axis, frame, _curve_color(entry_index), _hovered_key_matches(entry_index, point_index))

func _draw_twist_point(entry_index : int, entry : Dictionary, point_index : int) -> void:
	var curve : Resource = entry["curve"]
	var t := _curve_offset(curve, point_index)
	var frame := _sample_frame(t)
	_draw_key_circle(frame["center"], frame["x"], frame["y"], _twist_radius(t), _curve_color(entry_index), _hovered_key_matches(entry_index, point_index))

func _draw_tangent(entry_index : int, point_index : int, handle_kind : int, side : float) -> void:
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	var point : Vector2 = curve.get_point_position(point_index)
	var delta : float = _tangent_delta(curve, point_index, handle_kind)
	if absf(delta) <= 0.00001:
		return
	var tangent_t := clampf(point.x + delta, 0.0, 1.0)
	var frame := _sample_frame(tangent_t)
	var point_frame := _sample_frame(point.x)
	var color := _curve_color(entry_index).lerp(Color.WHITE, 0.3)
	match int(entry["kind"]):
		CurveKind.TWIST:
			var radius := _twist_radius(tangent_t)
			var left_center : Vector3 = frame["center"] - frame["x"] * radius
			var right_center : Vector3 = frame["center"] + frame["x"] * radius
			_draw_circle(left_center, frame["x"], frame["y"], radius, color, PI * 0.75, PI * 1.25, int(CIRCLE_STEPS / 4))
			_draw_circle(right_center, frame["x"], frame["y"], radius, color, -PI * 0.25, PI * 0.25, int(CIRCLE_STEPS / 4))
		CurveKind.SCALE_X, CurveKind.SCALE_Y:
			var tangent_pos := _tangent_handle_position({"kind": handle_kind, "entry": entry_index, "point": point_index, "side": side})
			var point_pos : Vector3 = point_frame["center"] + _entry_axis(entry, point_frame) * side * (_curve_value(curve, point_index) + VISUAL_OFFSET)
			_draw_line(point_pos, tangent_pos, color)
		_:
			var tangent_pos := _tangent_handle_position({"kind": handle_kind, "entry": entry_index, "point": point_index, "side": side})
			_draw_line(point_frame["center"], tangent_pos, color)

func _draw_curve_guides() -> void:
	for entry_index in curve_entries.size():
		var entry := curve_entries[entry_index]
		var curve : Resource = entry["curve"]
		for point_index in curve.point_count:
			match int(entry["kind"]):
				CurveKind.TWIST:
					_draw_twist_point(entry_index, entry, point_index)
				CurveKind.SCALE_X, CurveKind.SCALE_Y:
					_draw_scale_point(entry_index, entry, point_index)
				_:
					_draw_arrow_point(entry_index, entry, point_index)
			if point_index > 0:
				for side in _point_sides(int(entry["kind"])):
					if _selected_key_matches(entry_index, point_index):
						_draw_tangent(entry_index, point_index, HandleKind.LEFT_TANGENT, side)
			if point_index < curve.point_count - 1:
				for side in _point_sides(int(entry["kind"])):
					if _selected_key_matches(entry_index, point_index):
						_draw_tangent(entry_index, point_index, HandleKind.RIGHT_TANGENT, side)

func _update_visuals() -> void:
	_collect_curve_entries()
	_sync_handles()
	_sync_selection_collisions()
	for i in handles.size():
		handles[i].global_position = _handle_position(i)
		handles[i].global_basis = _handle_basis(i)
	outline_mesh.clear_surfaces()
	if curve_entries.is_empty():
		outline_mesh_instance.visible = false
		return
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	_draw_axis_pole()
	_draw_curve_guides()
	outline_mesh.surface_end()
	outline_mesh_instance.visible = true

func _hovered_handle(cam : Camera3D) -> int:
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_over_editor_ui():
		return -1
	if mouse_cast:
		mouse_cast.clear_exceptions()
		mouse_cast.force_raycast_update()
		while mouse_cast.is_colliding():
			var active_handle := _handle_from_collider(mouse_cast.get_collider())
			if active_handle != -1:
				mouse_cast.clear_exceptions()
				return active_handle
			var collision_object := mouse_cast.get_collider() as CollisionObject3D
			if !collision_object:
				break
			mouse_cast.add_exception(collision_object)
			mouse_cast.force_raycast_update()
		mouse_cast.clear_exceptions()
	if scene:
		var picker := scene.mouse_picker_cast
		picker.clear_exceptions()
		picker.force_raycast_update()
		while picker.is_colliding():
			var selection_handle := _selection_handle_from_collider(picker.get_collider())
			if selection_handle != -1:
				picker.clear_exceptions()
				return _handle_id_for_selected_key(selection_records[selection_handle])
			var collision_object := picker.get_collider() as CollisionObject3D
			if !collision_object:
				break
			picker.add_exception(collision_object)
			picker.force_raycast_update()
		picker.clear_exceptions()
	return -1

func _set_handle_colours(hovered : int) -> void:
	for i in handle_materials.size():
		if dragging and i == drag_handle:
			handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered:
			var record := handle_records[i]
			if int(record["kind"]) == HandleKind.AXIS_POLE:
				handle_materials[i].albedo_color = Color.WHITE
			else:
				handle_materials[i].albedo_color = _curve_hover_color(int(record["entry"]))
		else:
			handle_materials[i].albedo_color = _handle_base_color(i)

func _closest_t(world_pos : Vector3, entry : Dictionary) -> float:
	var best_t := 0.0
	var best_dist := INF
	var min_t := 0.0
	var max_t := 1.0
	for pass_index in SEARCH_PASSES:
		for i in SEARCH_STEPS + 1:
			var t := lerpf(min_t, max_t, float(i) / float(SEARCH_STEPS))
			var frame := _sample_frame(t)
			var dist : float = frame["center"].distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_t = t
		var radius := (max_t - min_t) / float(SEARCH_STEPS)
		min_t = maxf(0.0, best_t - radius)
		max_t = minf(1.0, best_t + radius)
	return best_t

func _clamped_curve_offset(curve : Resource, point_index : int, offset : float) -> float:
	if point_index == 0:
		return 0.0
	if point_index == curve.point_count - 1:
		return 1.0
	var min_offset := _curve_offset(curve, point_index - 1) + MIN_POINT_GAP
	var max_offset := _curve_offset(curve, point_index + 1) - MIN_POINT_GAP
	return clampf(offset, min_offset, max_offset)

func _line_drag_plane(cam : Camera3D, axis : Vector3, origin : Vector3) -> Plane:
	var offset_pos := cam.global_position - origin
	var cam_pos_projected := offset_pos.project(axis) + origin
	var plane_normal := (cam.global_position - cam_pos_projected).normalized()
	if plane_normal.length_squared() <= 0.00001:
		plane_normal = axis.cross(cam.global_basis.z).normalized()
	return Plane(plane_normal, origin)

func _begin_drag(handle_id : int, cam : Camera3D, ray_dir : Vector3) -> bool:
	var record := handle_records[handle_id]
	if int(record["kind"]) == HandleKind.AXIS_POLE:
		var axis_transform := _axis_transform()
		var plane := Plane(axis_transform.basis.z.normalized(), axis_transform.origin)
		var hit = plane.intersects_ray(cam.global_position, ray_dir)
		if !(hit is Vector3):
			return false
		var local_z := Vector3(0.0, 0.0, 1.0)
		var local_center_dir := axis_transform.basis.inverse() * (_axis_pole_center() - axis_transform.origin)
		local_center_dir.z = 0.0
		var local_grab_dir : Vector3 = axis_transform.basis.inverse() * (hit - axis_transform.origin)
		local_grab_dir.z = 0.0
		if local_center_dir.length_squared() <= 0.00001 or local_grab_dir.length_squared() <= 0.00001:
			return false
		var grab_to_center_angle := local_grab_dir.normalized().signed_angle_to(local_center_dir.normalized(), local_z)
		drag_snapshot = {
			"record": record.duplicate(),
			"mode": "axis",
			"plane": plane,
			"axis_origin": axis_transform.origin,
			"axis_basis_inverse": axis_transform.basis.inverse(),
			"grab_to_center_angle": grab_to_center_angle,
		}
		return true
	var entry := curve_entries[int(record["entry"])]
	var curve : Resource = entry["curve"]
	var point_index := int(record["point"])
	var point_t := _curve_offset(curve, point_index)
	var frame := _sample_frame(point_t)
	var side := float(record.get("side", 1.0))
	var kind := int(entry["kind"])
	if kind == CurveKind.TWIST:
		var center : Vector3 = frame["center"]
		if int(record["kind"]) != HandleKind.POINT:
			var delta : float = _tangent_delta(curve, point_index, int(record["kind"]))
			center = _sample_frame(clampf(point_t + delta, 0.0, 1.0))["center"]
		var plane := Plane(frame["z"], center)
		var hit = plane.intersects_ray(cam.global_position, ray_dir)
		if !(hit is Vector3):
			return false
		drag_snapshot = {
			"record": record.duplicate(),
			"mode": "rotation",
			"plane": plane,
			"center": center,
			"axis": frame["z"],
			"origin_dir": (hit - center).normalized(),
			"start_value": _curve_value(curve, point_index) if int(record["kind"]) == HandleKind.POINT else _tangent_value(curve, point_index, int(record["kind"])),
			"point_value": _curve_value(curve, point_index),
			"delta_t": _tangent_delta(curve, point_index, int(record["kind"])),
		}
		return true
	var value_axis := _entry_axis(entry, frame).normalized()
	var origin := _handle_position(handle_id)
	var plane := _line_drag_plane(cam, value_axis, origin) if int(record["kind"]) == HandleKind.POINT else Plane(_entry_plane_normal(entry, frame), origin)
	var hit = plane.intersects_ray(cam.global_position, ray_dir)
	if !(hit is Vector3):
		return false
	drag_snapshot = {
		"record": record.duplicate(),
		"mode": "line",
		"plane": plane,
		"axis": value_axis,
		"origin": origin,
		"start_axis_point": (hit - origin).project(value_axis) + origin,
		"start_value": _curve_value(curve, point_index) if int(record["kind"]) == HandleKind.POINT else _tangent_value(curve, point_index, int(record["kind"])),
		"point_value": _curve_value(curve, point_index),
		"delta_t": _tangent_delta(curve, point_index, int(record["kind"])),
		"side": side,
	}
	return true

func _apply_axis_drag(cam : Camera3D, ray_dir : Vector3) -> void:
	var plane : Plane = drag_snapshot["plane"]
	var hit = plane.intersects_ray(cam.global_position, ray_dir)
	if !(hit is Vector3):
		return
	var origin : Vector3 = drag_snapshot["axis_origin"]
	var basis_inverse : Basis = drag_snapshot["axis_basis_inverse"]
	var local_center_dir : Vector3 = basis_inverse * (hit - origin)
	local_center_dir.z = 0.0
	if local_center_dir.length_squared() <= 0.00001:
		return
	var local_z := Vector3(0.0, 0.0, 1.0)
	local_center_dir = Basis(local_z, float(drag_snapshot["grab_to_center_angle"])) * local_center_dir.normalized()
	var local_axis := local_z.cross(local_center_dir).normalized()
	if local_axis.length_squared() <= 0.00001:
		return
	target_path.set("spiral_axis", local_axis)
	_update_mesh(false)

func _apply_line_drag(cam : Camera3D, ray_dir : Vector3) -> void:
	var record : Dictionary = drag_snapshot["record"]
	var entry := curve_entries[int(record["entry"])]
	var curve : Resource = entry["curve"]
	var point_index := int(record["point"])
	var plane : Plane = drag_snapshot["plane"]
	var hit = plane.intersects_ray(cam.global_position, ray_dir)
	if !(hit is Vector3):
		return
	var axis : Vector3 = drag_snapshot["axis"]
	var start_axis_point : Vector3 = drag_snapshot["start_axis_point"]
	var side := float(drag_snapshot["side"]) if _entry_uses_side_drag(entry) else 1.0
	var delta : float = (hit - start_axis_point).dot(axis) * side
	var value := maxf(float(entry["min"]), float(drag_snapshot["start_value"]) + delta)
	if int(record["kind"]) == HandleKind.POINT:
		curve.set_point_value(point_index, value)
	else:
		var delta_t := float(drag_snapshot["delta_t"])
		if absf(delta_t) > 0.00001:
			var slope := (value - float(drag_snapshot["point_value"])) / delta_t
			if int(record["kind"]) == HandleKind.LEFT_TANGENT:
				curve.set_point_left_tangent(point_index, slope)
			else:
				curve.set_point_right_tangent(point_index, slope)
	_update_mesh(false)

func _apply_rotation_drag(cam : Camera3D, ray_dir : Vector3) -> void:
	var record : Dictionary = drag_snapshot["record"]
	var entry := curve_entries[int(record["entry"])]
	var curve : Resource = entry["curve"]
	var point_index := int(record["point"])
	var plane : Plane = drag_snapshot["plane"]
	var hit = plane.intersects_ray(cam.global_position, ray_dir)
	if !(hit is Vector3):
		return
	var center : Vector3 = drag_snapshot["center"]
	var current_dir : Vector3 = (hit - center).normalized()
	var origin_dir : Vector3 = drag_snapshot["origin_dir"]
	var axis : Vector3 = drag_snapshot["axis"]
	var value := float(drag_snapshot["start_value"]) + rad_to_deg(origin_dir.signed_angle_to(current_dir, axis))
	if int(record["kind"]) == HandleKind.POINT:
		curve.set_point_value(point_index, value)
	else:
		var delta_t := float(drag_snapshot["delta_t"])
		if absf(delta_t) > 0.00001:
			var slope := (value - float(drag_snapshot["point_value"])) / delta_t
			if int(record["kind"]) == HandleKind.LEFT_TANGENT:
				curve.set_point_left_tangent(point_index, slope)
			else:
				curve.set_point_right_tangent(point_index, slope)
	_update_mesh(false)

func _write_dragged_handle(cam : Camera3D, ray_dir : Vector3) -> void:
	match String(drag_snapshot.get("mode", "")):
		"axis":
			_apply_axis_drag(cam, ray_dir)
		"rotation":
			_apply_rotation_drag(cam, ray_dir)
		_:
			_apply_line_drag(cam, ray_dir)

func _curve_world_position(entry : Dictionary, t : float, value : float, side := 1.0) -> Vector3:
	var frame := _sample_frame(t)
	match int(entry["kind"]):
		CurveKind.TWIST:
			return frame["center"] + frame["x"] * _twist_radius(t)
		CurveKind.SCALE_X, CurveKind.SCALE_Y:
			return frame["center"] + _entry_axis(entry, frame) * side * (value + VISUAL_OFFSET)
		_:
			return frame["center"] + _entry_axis(entry, frame) * side * ARROW_LENGTH

func _screen_distance_to_curve(entry_index : int, cam : Camera3D, mouse_pos : Vector2) -> Dictionary:
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	var best_offset := 0.0
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	for i in PREVIEW_STEPS + 1:
		var offset := float(i) / float(PREVIEW_STEPS)
		var world := _curve_world_position(entry, offset, curve.sample(offset), 1.0)
		var screen := cam.unproject_position(world)
		if i > 0:
			var closest := Geometry2D.get_closest_point_to_segment(mouse_pos, previous_screen, screen)
			var dist := mouse_pos.distance_squared_to(closest)
			if dist < best_dist:
				var span_offset := 1.0 / float(PREVIEW_STEPS)
				var span_len := previous_screen.distance_to(screen)
				var local_t := 0.0 if span_len <= 0.001 else previous_screen.distance_to(closest) / span_len
				best_offset = clampf(float(i - 1) / float(PREVIEW_STEPS) + span_offset * local_t, 0.0, 1.0)
				best_dist = dist
		previous_screen = screen
	return {"offset": best_offset, "distance": best_dist}

func _update_mesh(force_collision := false) -> void:
	if !is_instance_valid(target_path):
		return
	target_path.call("refresh_spiral_road")
	var update_collision := force_collision or Time.get_ticks_msec() > target_path.last_gen_time + 100
	if update_collision:
		target_path.last_gen_time = Time.get_ticks_msec()
	target_path._try_generate_mesh(update_collision)
	target_path.set("should_update", false)

func _add_curve_point(entry_index : int, offset : float) -> void:
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	offset = clampf(offset, MIN_POINT_GAP, 1.0 - MIN_POINT_GAP)
	curve.add_point(Vector2(offset, maxf(curve.sample(offset), float(entry["min"]))))
	_update_mesh(false)
	_update_visuals()

func _try_alt_add_point(cam : Camera3D) -> bool:
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse") or curve_entries.is_empty():
		return false
	var mouse_pos := get_viewport().get_mouse_position()
	var best_entry := -1
	var best_offset := 0.0
	var best_distance := INF
	for entry_index in curve_entries.size():
		var pick := _screen_distance_to_curve(entry_index, cam, mouse_pos)
		if pick["distance"] < best_distance:
			best_distance = float(pick["distance"])
			best_entry = entry_index
			best_offset = float(pick["offset"])
	if best_entry < 0 or best_distance > ADD_POINT_MAX_SCREEN_DISTANCE * ADD_POINT_MAX_SCREEN_DISTANCE:
		return false
	var scene := FZGlobal.editing_scene
	if scene and !scene.begin_pointer_action(self):
		return false
	_add_curve_point(best_entry, best_offset)
	if scene:
		scene.end_pointer_action(self)
	get_viewport().set_input_as_handled()
	return true

func _try_delete_hovered_point(hovered : int) -> bool:
	var delete_now := Input.is_key_pressed(KEY_DELETE)
	var just_delete := delete_now and !delete_pressed
	delete_pressed = delete_now
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_over_editor_ui():
		return false
	if !just_delete or hovered < 0:
		return false
	var record := handle_records[hovered]
	if int(record["kind"]) != HandleKind.POINT or !record.has("entry"):
		return false
	var curve : Resource = curve_entries[int(record["entry"])]["curve"]
	var point_index := int(record["point"])
	if point_index <= 0 or point_index >= curve.point_count - 1:
		return false
	curve.remove_point(point_index)
	_update_mesh(false)
	_update_visuals()
	get_viewport().set_input_as_handled()
	return true

func _end_deferred_pointer_action() -> void:
	var scene := FZGlobal.editing_scene
	if scene:
		scene.end_pointer_action(self)

func _process(_delta : float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_spiral_gizmos() or !_is_spiral_path(target_path):
		if scene:
			scene.end_pointer_action(self)
		visible = false
		outline_mesh_instance.visible = false
		_set_colliders_enabled(false)
		_set_selection_colliders_enabled(false)
		dragging = false
		drag_snapshot.clear()
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	visible = true
	_update_visuals()
	if curve_entries.is_empty():
		_set_colliders_enabled(false)
		_set_selection_colliders_enabled(false)
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	_set_colliders_enabled(true)
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var ray_dir := cam.project_ray_normal(get_viewport().get_mouse_position())
	var hovered := _hovered_handle(cam)
	if _set_hovered_key_from_handle(hovered):
		_update_visuals()
	_set_handle_colours(hovered)
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_point(hovered):
		return
	if hovered == -1 and !scene.pointer_action_busy_for(self) and _try_alt_add_point(cam):
		return
	if Input.is_action_just_pressed("RightMouse") and hovered != -1:
		var hovered_record := handle_records[hovered]
		if _record_is_key_point(hovered_record):
			if !scene.begin_pointer_action(self):
				return
			_select_record_key(hovered_record)
			_update_visuals()
			get_viewport().set_input_as_handled()
			call_deferred("_end_deferred_pointer_action")
			return
	if Input.is_action_just_pressed("LeftMouse") and hovered != -1:
		var hovered_record := handle_records[hovered]
		if _record_is_key_point(hovered_record) and !_record_key_is_selected(hovered_record):
			return
		if !scene.begin_pointer_action(self):
			return
		if !_begin_drag(hovered, cam, ray_dir):
			scene.end_pointer_action(self)
			return
		dragging = true
		drag_handle = hovered
		get_viewport().set_input_as_handled()
	if Input.is_action_just_released("LeftMouse"):
		if dragging or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		dragging = false
		drag_handle = -1
		drag_snapshot.clear()
		scene.end_pointer_action(self)
	if dragging and drag_handle != -1:
		_write_dragged_handle(cam, ray_dir)
		_update_visuals()
