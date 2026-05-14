class_name ModulationGizmo extends Node3D

enum CurveKind {
	EFFECT,
	HEIGHT,
}

enum HandleKind {
	POINT,
	LEFT_TANGENT,
	RIGHT_TANGENT,
}

const HANDLE_RADIUS := 4.0
const TANGENT_HANDLE_RADIUS := 2.5
const NORMAL_EPSILON := 0.002
const SEARCH_STEPS := 10
const SEARCH_PASSES := 5
const PREVIEW_STEPS := 24
const DRAG_SURFACE_STEPS := 64
const HEIGHT_PREVIEW_TY := 0.5
const MIN_POINT_GAP := 0.001
const TANGENT_HANDLE_OFFSET := 0.08
const ADD_POINT_MAX_SCREEN_DISTANCE := 60.0

var mouse_cast : RayCast3D
var target_path : RoadPath
var modulation_index := -1
var dragging := false
var drag_handle := -1
var delete_pressed := false
var drag_plane := Plane.PLANE_XZ
var drag_start_point := Vector3.ZERO
var drag_snapshot := {}
var outline_mesh_instance : MeshInstance3D
var outline_mesh := ImmediateMesh.new()
var outline_material := StandardMaterial3D.new()
var handle_materials : Array[StandardMaterial3D] = []
var handles : Array[StaticBody3D] = []
var handle_curve_kinds : Array[int] = []
var handle_kinds : Array[int] = []
var handle_indices : Array[int] = []

func _ready() -> void:
	outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	outline_material.vertex_color_use_as_albedo = true
	outline_mesh_instance = MeshInstance3D.new()
	outline_mesh_instance.mesh = outline_mesh
	outline_mesh_instance.top_level = true
	add_child(outline_mesh_instance)
	set_target_modulation(null, -1)

func set_target_modulation(in_path : RoadPath, in_modulation_index : int) -> void:
	target_path = in_path
	modulation_index = in_modulation_index
	dragging = false
	drag_handle = -1
	drag_snapshot.clear()

func _active_modulation() -> RoadModulation:
	if !is_instance_valid(target_path):
		return null
	if modulation_index < 0 or modulation_index >= target_path.road_shape.modulation_table.size():
		return null
	return target_path.road_shape.modulation_table[modulation_index]

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _ensure_effect_curve(curve : Resource) -> void:
	while curve.point_count < 2:
		curve.add_point(Vector2(float(curve.point_count), 0.0))
	curve.set_point_offset(0, 0.0)
	curve.set_point_offset(curve.point_count - 1, 1.0)

func _ensure_height_curve(curve : Resource) -> void:
	while curve.point_count < 2:
		curve.add_point(Vector2(float(curve.point_count), 1.0))
	curve.set_point_offset(0, 0.0)
	curve.set_point_offset(curve.point_count - 1, 1.0)
	if is_zero_approx(curve.get_point_position(0).y) and is_zero_approx(curve.get_point_position(curve.point_count - 1).y):
		for i in curve.point_count:
			curve.set_point_value(i, 1.0)

func _curve_for_kind(modulation : RoadModulation, kind : int) -> Resource:
	return modulation.modulation_height if kind == CurveKind.HEIGHT else modulation.modulation_effect

func _curve_color(kind : int) -> Color:
	return Color(0.28, 1.0, 0.45, 1.0) if kind == CurveKind.HEIGHT else Color(0.9, 0.3, 1.0, 1.0)

func _curve_hover_color(kind : int) -> Color:
	return Color(0.72, 1.0, 0.72, 1.0) if kind == CurveKind.HEIGHT else Color(1.0, 0.75, 1.0, 1.0)

func _curve_value(curve : Resource, point_index : int) -> float:
	if !curve or curve.point_count <= point_index:
		return 0.0
	return curve.get_point_position(point_index).y

func _curve_offset(curve : Resource, point_index : int) -> float:
	if !curve or curve.point_count <= point_index:
		return 0.0
	return curve.get_point_position(point_index).x

func _surface_point(tx : float, ty : float) -> Vector3:
	var points := target_path.get_surface_positions(PackedVector2Array([Vector2(clampf(tx, -1.0, 1.0), clampf(ty, 0.0, 1.0))]))
	if points.is_empty():
		return Vector3.ZERO
	return points[0]

func _surface_normal(tx : float, ty : float) -> Vector3:
	var clamped_tx := clampf(tx, -1.0, 1.0)
	var clamped_ty := clampf(ty, 0.0, 1.0)
	var tx_side := minf(clamped_tx + NORMAL_EPSILON, 1.0) if clamped_tx < 0.0 else maxf(clamped_tx - NORMAL_EPSILON, -1.0)
	var ty_side := minf(clamped_ty + NORMAL_EPSILON, 1.0) if clamped_ty < 0.5 else maxf(clamped_ty - NORMAL_EPSILON, 0.0)
	var samples := target_path.get_surface_positions(PackedVector2Array([
		Vector2(clamped_tx, clamped_ty),
		Vector2(tx_side, clamped_ty),
		Vector2(clamped_tx, ty_side),
	]))
	if samples.size() < 3:
		return Vector3.UP
	var tangent_x := (samples[1] - samples[0]) if clamped_tx < 0.0 else (samples[0] - samples[1])
	var tangent_y := (samples[2] - samples[0]) if clamped_ty < 0.5 else (samples[0] - samples[2])
	tangent_x = tangent_x.normalized()
	tangent_y = tangent_y.normalized()
	var normal := tangent_y.cross(tangent_x).normalized()
	if normal.is_zero_approx():
		return Vector3.UP
	return normal

func _surface_param_for_kind(kind : int, offset : float) -> Vector2:
	if kind == CurveKind.HEIGHT:
		return Vector2(_height_tx(offset), HEIGHT_PREVIEW_TY)
	return Vector2(0.0, offset)

func _height_tx(offset : float) -> float:
	return 1.0 - clampf(offset, 0.0, 1.0) * 2.0

func _effect_position(curve : Resource, point_index : int) -> Vector3:
	var ty := _curve_offset(curve, point_index)
	return _surface_point(0.0, ty) + _surface_normal(0.0, ty) * _curve_value(curve, point_index)

func _height_position(curve : Resource, point_index : int) -> Vector3:
	var offset := _curve_offset(curve, point_index)
	var tx := _height_tx(offset)
	return _surface_point(tx, HEIGHT_PREVIEW_TY) + _surface_normal(tx, HEIGHT_PREVIEW_TY) * _curve_value(curve, point_index)

func _curve_base_position(kind : int, offset : float) -> Vector3:
	var tx := _height_tx(offset) if kind == CurveKind.HEIGHT else 0.0
	var ty := HEIGHT_PREVIEW_TY if kind == CurveKind.HEIGHT else offset
	return _surface_point(tx, ty)

func _curve_normal(kind : int, offset : float) -> Vector3:
	var tx := _height_tx(offset) if kind == CurveKind.HEIGHT else 0.0
	var ty := HEIGHT_PREVIEW_TY if kind == CurveKind.HEIGHT else offset
	return _surface_normal(tx, ty)

func _curve_world_position(kind : int, offset : float, value : float) -> Vector3:
	return _curve_base_position(kind, offset) + _curve_normal(kind, offset) * value

func _build_drag_surface_snapshot(kind : int) -> Array[Dictionary]:
	var samples : Array[Dictionary] = []
	var query := PackedVector2Array()
	for i in DRAG_SURFACE_STEPS + 1:
		var offset := float(i) / float(DRAG_SURFACE_STEPS)
		query.append(_surface_param_for_kind(kind, offset))
		query.append(_surface_param_for_kind(kind, clampf(offset + NORMAL_EPSILON, 0.0, 1.0)))
		query.append(_surface_param_for_kind(kind, clampf(offset - NORMAL_EPSILON, 0.0, 1.0)))
	var positions := target_path.get_surface_positions(query)
	if positions.size() != query.size():
		return samples
	for i in DRAG_SURFACE_STEPS + 1:
		var base_index := i * 3
		var offset := float(i) / float(DRAG_SURFACE_STEPS)
		var base : Vector3 = positions[base_index]
		var normal := Vector3.UP
		if kind == CurveKind.HEIGHT:
			var tx := _height_tx(offset)
			normal = _surface_normal(tx, HEIGHT_PREVIEW_TY)
		else:
			var tangent_y : Vector3
			if offset < 0.5:
				tangent_y = positions[base_index + 1] - base
			else:
				tangent_y = base - positions[base_index + 2]
			var side_a := target_path.get_surface_positions(PackedVector2Array([Vector2(NORMAL_EPSILON, offset), Vector2(-NORMAL_EPSILON, offset)]))
			var tangent_x := side_a[0] - side_a[1] if side_a.size() == 2 else Vector3.RIGHT
			normal = tangent_y.cross(tangent_x).normalized()
			if normal.is_zero_approx():
				normal = Vector3.UP
		samples.append({"offset": offset, "base": base, "normal": normal})
	return samples

func _drag_surface_sample_at(samples : Array, offset : float) -> Dictionary:
	if samples.is_empty():
		return {"offset": 0.0, "base": Vector3.ZERO, "normal": Vector3.UP}
	if samples.size() == 1:
		return samples[0]
	var scaled_index := clampf(offset, 0.0, 1.0) * float(samples.size() - 1)
	var sample_index := mini(int(floorf(scaled_index)), samples.size() - 2)
	var local_t := scaled_index - float(sample_index)
	var a : Dictionary = samples[sample_index]
	var b : Dictionary = samples[sample_index + 1]
	var normal : Vector3 = (a["normal"] as Vector3).lerp(b["normal"], local_t)
	if normal.length_squared() <= 0.00001:
		normal = a["normal"] as Vector3
	return {
		"offset": lerpf(float(a["offset"]), float(b["offset"]), local_t),
		"base": (a["base"] as Vector3).lerp(b["base"], local_t),
		"normal": normal.normalized(),
	}

func _closest_drag_surface_offset(samples : Array, world_pos : Vector3) -> float:
	var best_offset := 0.0
	var best_dist := INF
	var min_offset := 0.0
	var max_offset := 1.0
	for pass_index in SEARCH_PASSES:
		for i in SEARCH_STEPS + 1:
			var offset := lerpf(min_offset, max_offset, float(i) / float(SEARCH_STEPS))
			var sample := _drag_surface_sample_at(samples, offset)
			var dist : float = (sample["base"] as Vector3).distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_offset = float(sample["offset"])
		var radius := (max_offset - min_offset) / float(SEARCH_STEPS)
		min_offset = maxf(0.0, best_offset - radius)
		max_offset = minf(1.0, best_offset + radius)
	return best_offset

func _point_handle_position(modulation : RoadModulation, kind : int, point_index : int) -> Vector3:
	var curve := _curve_for_kind(modulation, kind)
	return _height_position(curve, point_index) if kind == CurveKind.HEIGHT else _effect_position(curve, point_index)

func _tangent_handle_position(modulation : RoadModulation, kind : int, point_index : int, handle_kind : int) -> Vector3:
	var curve := _curve_for_kind(modulation, kind)
	var point : Vector2 = curve.get_point_position(point_index)
	var offset_delta := TANGENT_HANDLE_OFFSET
	var tangent := 0.0
	if handle_kind == HandleKind.LEFT_TANGENT:
		offset_delta = -minf(TANGENT_HANDLE_OFFSET, point.x)
		tangent = curve.get_point_left_tangent(point_index)
	else:
		offset_delta = minf(TANGENT_HANDLE_OFFSET, 1.0 - point.x)
		tangent = curve.get_point_right_tangent(point_index)
	if absf(offset_delta) <= 0.00001:
		return _point_handle_position(modulation, kind, point_index)
	return _curve_world_position(kind, point.x + offset_delta, point.y + tangent * offset_delta)

func _handle_position(modulation : RoadModulation, handle_id : int) -> Vector3:
	var kind := handle_curve_kinds[handle_id]
	var point_index := handle_indices[handle_id]
	var handle_kind := handle_kinds[handle_id]
	if handle_kind == HandleKind.POINT:
		return _point_handle_position(modulation, kind, point_index)
	return _tangent_handle_position(modulation, kind, point_index, handle_kind)

func _handle_base_color(handle_id : int) -> Color:
	var curve_kind := handle_curve_kinds[handle_id]
	var color := _curve_color(curve_kind)
	return color if handle_kinds[handle_id] == HandleKind.POINT else color.lerp(Color.WHITE, 0.35)

func _make_handle(kind : int, handle_kind : int) -> StaticBody3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = _curve_color(kind)
	handle_materials.append(material)
	var body := StaticBody3D.new()
	body.set_collision_layer_value(16, true)
	body.set_collision_mask_value(16, true)
	body.set_collision_layer_value(1, false)
	body.set_collision_mask_value(1, false)
	var collision := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = TANGENT_HANDLE_RADIUS if handle_kind != HandleKind.POINT else HANDLE_RADIUS
	collision.shape = sphere
	body.add_child(collision)
	var mesh_instance := MeshInstance3D.new()
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = sphere.radius
	sphere_mesh.height = sphere.radius * 2.0
	mesh_instance.mesh = sphere_mesh
	mesh_instance.material_override = material
	body.add_child(mesh_instance)
	add_child(body)
	return body

func _sync_handles(modulation : RoadModulation) -> void:
	handle_curve_kinds.clear()
	handle_kinds.clear()
	handle_indices.clear()
	for i in modulation.modulation_effect.point_count:
		handle_curve_kinds.append(CurveKind.EFFECT)
		handle_kinds.append(HandleKind.POINT)
		handle_indices.append(i)
		if i > 0:
			handle_curve_kinds.append(CurveKind.EFFECT)
			handle_kinds.append(HandleKind.LEFT_TANGENT)
			handle_indices.append(i)
		if i < modulation.modulation_effect.point_count - 1:
			handle_curve_kinds.append(CurveKind.EFFECT)
			handle_kinds.append(HandleKind.RIGHT_TANGENT)
			handle_indices.append(i)
	for i in modulation.modulation_height.point_count:
		handle_curve_kinds.append(CurveKind.HEIGHT)
		handle_kinds.append(HandleKind.POINT)
		handle_indices.append(i)
		if i > 0:
			handle_curve_kinds.append(CurveKind.HEIGHT)
			handle_kinds.append(HandleKind.LEFT_TANGENT)
			handle_indices.append(i)
		if i < modulation.modulation_height.point_count - 1:
			handle_curve_kinds.append(CurveKind.HEIGHT)
			handle_kinds.append(HandleKind.RIGHT_TANGENT)
			handle_indices.append(i)
	while handles.size() < handle_kinds.size():
		handles.append(_make_handle(handle_curve_kinds[handles.size()], handle_kinds[handles.size()]))
	while handles.size() > handle_kinds.size():
		var handle : StaticBody3D = handles.pop_back()
		handle_materials.pop_back()
		handle.queue_free()
	for i in handles.size():
		handle_materials[i].albedo_color = _handle_base_color(i)

func _draw_curve_preview(curve : Resource, kind : int) -> void:
	outline_mesh.surface_set_color(_curve_color(kind))
	for i in PREVIEW_STEPS:
		var t0 := float(i) / float(PREVIEW_STEPS)
		var t1 := float(i + 1) / float(PREVIEW_STEPS)
		var p0 : Vector3
		var p1 : Vector3
		if kind == CurveKind.HEIGHT:
			var tx0 := _height_tx(t0)
			var tx1 := _height_tx(t1)
			p0 = _surface_point(tx0, HEIGHT_PREVIEW_TY) + _surface_normal(tx0, HEIGHT_PREVIEW_TY) * curve.sample(t0)
			p1 = _surface_point(tx1, HEIGHT_PREVIEW_TY) + _surface_normal(tx1, HEIGHT_PREVIEW_TY) * curve.sample(t1)
		else:
			p0 = _surface_point(0.0, t0) + _surface_normal(0.0, t0) * curve.sample(t0)
			p1 = _surface_point(0.0, t1) + _surface_normal(0.0, t1) * curve.sample(t1)
		outline_mesh.surface_add_vertex(p0)
		outline_mesh.surface_add_vertex(p1)

func _draw_tangent_handles(modulation : RoadModulation) -> void:
	outline_mesh.surface_set_color(Color(0.9, 0.95, 1.0, 0.85))
	for handle_id in handles.size():
		if handle_kinds[handle_id] == HandleKind.POINT:
			continue
		var point_pos := _point_handle_position(modulation, handle_curve_kinds[handle_id], handle_indices[handle_id])
		var handle_pos := _handle_position(modulation, handle_id)
		outline_mesh.surface_add_vertex(point_pos)
		outline_mesh.surface_add_vertex(handle_pos)

func _update_visuals(modulation : RoadModulation) -> void:
	_ensure_effect_curve(modulation.modulation_effect)
	_ensure_height_curve(modulation.modulation_height)
	_sync_handles(modulation)
	for i in handles.size():
		handles[i].global_position = _handle_position(modulation, i)
	outline_mesh.clear_surfaces()
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	_draw_curve_preview(modulation.modulation_effect, CurveKind.EFFECT)
	_draw_curve_preview(modulation.modulation_height, CurveKind.HEIGHT)
	_draw_tangent_handles(modulation)
	outline_mesh.surface_end()
	outline_mesh_instance.visible = true

func _hovered_handle() -> int:
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_over_editor_ui():
		return -1
	if !mouse_cast or !mouse_cast.is_colliding():
		return -1
	var collider := mouse_cast.get_collider()
	for i in handles.size():
		if collider == handles[i]:
			return i
	return -1

func _set_handle_colours(hovered : int) -> void:
	for i in handle_materials.size():
		if dragging and i == drag_handle:
			handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered:
			handle_materials[i].albedo_color = _curve_hover_color(handle_curve_kinds[i])
		else:
			handle_materials[i].albedo_color = _handle_base_color(i)

func _closest_center_ty(world_pos : Vector3) -> float:
	var best_ty := 0.0
	var best_dist := INF
	var min_ty := 0.0
	var max_ty := 1.0
	for pass_index in SEARCH_PASSES:
		for i in SEARCH_STEPS + 1:
			var ty := lerpf(min_ty, max_ty, float(i) / float(SEARCH_STEPS))
			var dist := _surface_point(0.0, ty).distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_ty = ty
		var radius := (max_ty - min_ty) / float(SEARCH_STEPS)
		min_ty = maxf(0.0, best_ty - radius)
		max_ty = minf(1.0, best_ty + radius)
	return best_ty

func _closest_height_offset(world_pos : Vector3) -> float:
	var best_offset := 0.0
	var best_dist := INF
	var min_offset := 0.0
	var max_offset := 1.0
	for pass_index in SEARCH_PASSES:
		for i in SEARCH_STEPS + 1:
			var offset := lerpf(min_offset, max_offset, float(i) / float(SEARCH_STEPS))
			var dist := _surface_point(_height_tx(offset), HEIGHT_PREVIEW_TY).distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_offset = offset
		var radius := (max_offset - min_offset) / float(SEARCH_STEPS)
		min_offset = maxf(0.0, best_offset - radius)
		max_offset = minf(1.0, best_offset + radius)
	return best_offset

func _clamped_curve_offset(curve : Resource, point_index : int, offset : float) -> float:
	if point_index == 0:
		return 0.0
	if point_index == curve.point_count - 1:
		return 1.0
	var min_offset := _curve_offset(curve, point_index - 1) + MIN_POINT_GAP
	var max_offset := _curve_offset(curve, point_index + 1) - MIN_POINT_GAP
	return clampf(offset, min_offset, max_offset)

func _drag_samples_for_handle(handle_id : int) -> Array:
	if int(drag_snapshot.get("handle", -1)) == handle_id:
		return drag_snapshot.get("samples", [])
	return []

func _write_point_handle(modulation : RoadModulation, handle_id : int, world_pos : Vector3) -> void:
	var kind := handle_curve_kinds[handle_id]
	var point_index := handle_indices[handle_id]
	var curve := _curve_for_kind(modulation, kind)
	var samples := _drag_samples_for_handle(handle_id)
	var offset := _closest_drag_surface_offset(samples, world_pos) if !samples.is_empty() else (_closest_height_offset(world_pos) if kind == CurveKind.HEIGHT else _closest_center_ty(world_pos))
	offset = _clamped_curve_offset(curve, point_index, offset)
	var surface := _drag_surface_sample_at(samples, offset) if !samples.is_empty() else {}
	var base : Vector3 = surface["base"] if !surface.is_empty() else _curve_base_position(kind, offset)
	var normal : Vector3 = surface["normal"] if !surface.is_empty() else _curve_normal(kind, offset)
	curve.set_point_offset(point_index, offset)
	curve.set_point_value(point_index, (world_pos - base).dot(normal))

func _write_tangent_handle(modulation : RoadModulation, handle_id : int, world_pos : Vector3) -> void:
	var kind := handle_curve_kinds[handle_id]
	var point_index := handle_indices[handle_id]
	var handle_kind := handle_kinds[handle_id]
	var curve := _curve_for_kind(modulation, kind)
	var point : Vector2 = curve.get_point_position(point_index)
	var samples := _drag_samples_for_handle(handle_id)
	var handle_offset := _closest_drag_surface_offset(samples, world_pos) if !samples.is_empty() else (_closest_height_offset(world_pos) if kind == CurveKind.HEIGHT else _closest_center_ty(world_pos))
	var delta : float = handle_offset - point.x
	if handle_kind == HandleKind.LEFT_TANGENT:
		delta = minf(delta, -MIN_POINT_GAP)
	else:
		delta = maxf(delta, MIN_POINT_GAP)
	handle_offset = clampf(point.x + delta, 0.0, 1.0)
	delta = handle_offset - point.x
	if absf(delta) < MIN_POINT_GAP:
		return
	var surface := _drag_surface_sample_at(samples, handle_offset) if !samples.is_empty() else {}
	var base : Vector3 = surface["base"] if !surface.is_empty() else _curve_base_position(kind, handle_offset)
	var normal : Vector3 = surface["normal"] if !surface.is_empty() else _curve_normal(kind, handle_offset)
	var value := (world_pos - base).dot(normal)
	if handle_kind == HandleKind.LEFT_TANGENT:
		curve.set_point_left_tangent(point_index, (value - point.y) / delta)
	else:
		curve.set_point_right_tangent(point_index, (value - point.y) / delta)

func _write_dragged_handle(modulation : RoadModulation, handle_id : int, world_pos : Vector3) -> void:
	if handle_kinds[handle_id] == HandleKind.POINT:
		_write_point_handle(modulation, handle_id, world_pos)
	else:
		_write_tangent_handle(modulation, handle_id, world_pos)

func _screen_distance_to_curve(curve : Resource, kind : int, cam : Camera3D, mouse_pos : Vector2) -> Dictionary:
	var best_offset := 0.0
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	for i in PREVIEW_STEPS + 1:
		var offset := float(i) / float(PREVIEW_STEPS)
		var world : Vector3
		if kind == CurveKind.HEIGHT:
			var tx := _height_tx(offset)
			world = _surface_point(tx, HEIGHT_PREVIEW_TY) + _surface_normal(tx, HEIGHT_PREVIEW_TY) * curve.sample(offset)
		else:
			world = _surface_point(0.0, offset) + _surface_normal(0.0, offset) * curve.sample(offset)
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
	return {
		"offset": best_offset,
		"distance": best_dist,
	}

func _add_curve_point(modulation : RoadModulation, kind : int, offset : float) -> void:
	var curve := _curve_for_kind(modulation, kind)
	offset = clampf(offset, MIN_POINT_GAP, 1.0 - MIN_POINT_GAP)
	curve.add_point(Vector2(offset, curve.sample(offset)))
	_update_mesh(false)
	_update_visuals(modulation)

func _try_alt_add_point(modulation : RoadModulation, cam : Camera3D) -> bool:
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return false
	var mouse_pos := get_viewport().get_mouse_position()
	var effect_pick := _screen_distance_to_curve(modulation.modulation_effect, CurveKind.EFFECT, cam, mouse_pos)
	var height_pick := _screen_distance_to_curve(modulation.modulation_height, CurveKind.HEIGHT, cam, mouse_pos)
	var best_distance : float = minf(effect_pick["distance"], height_pick["distance"])
	if best_distance > ADD_POINT_MAX_SCREEN_DISTANCE * ADD_POINT_MAX_SCREEN_DISTANCE:
		return false
	var kind := CurveKind.HEIGHT if height_pick["distance"] < effect_pick["distance"] else CurveKind.EFFECT
	var offset : float = height_pick["offset"] if kind == CurveKind.HEIGHT else effect_pick["offset"]
	var scene := FZGlobal.editing_scene
	if scene and !scene.begin_pointer_action(self):
		return false
	_add_curve_point(modulation, kind, offset)
	if scene:
		scene.end_pointer_action(self)
	get_viewport().set_input_as_handled()
	return true

func _try_delete_hovered_point(modulation : RoadModulation, hovered : int) -> bool:
	var delete_now := Input.is_key_pressed(KEY_DELETE)
	var just_delete := delete_now and !delete_pressed
	delete_pressed = delete_now
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_over_editor_ui():
		return false
	if !just_delete or hovered < 0:
		return false
	if handle_kinds[hovered] != HandleKind.POINT:
		return false
	var kind := handle_curve_kinds[hovered]
	var point_index := handle_indices[hovered]
	var curve := _curve_for_kind(modulation, kind)
	if point_index <= 0 or point_index >= curve.point_count - 1:
		return false
	curve.remove_point(point_index)
	_update_mesh(false)
	_update_visuals(modulation)
	get_viewport().set_input_as_handled()
	return true

func _update_mesh(force_collision := false) -> void:
	var update_collision := force_collision or Time.get_ticks_msec() > target_path.last_gen_time + 100
	if update_collision:
		target_path.last_gen_time = Time.get_ticks_msec()
	target_path._try_generate_mesh(update_collision)

func _process(delta : float) -> void:
	var scene := FZGlobal.editing_scene
	var modulation := _active_modulation()
	if !scene or !scene.tool_mode_allows_modulation_gizmos() or !modulation:
		if scene:
			scene.end_pointer_action(self)
		visible = false
		outline_mesh_instance.visible = false
		_set_colliders_enabled(false)
		dragging = false
		drag_snapshot.clear()
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	visible = true
	_set_colliders_enabled(true)
	_update_visuals(modulation)
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var ray_dir := cam.project_ray_normal(get_viewport().get_mouse_position())
	var hovered := _hovered_handle()
	_set_handle_colours(hovered)
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_point(modulation, hovered):
		return
	if hovered == -1 and !scene.pointer_action_busy_for(self) and _try_alt_add_point(modulation, cam):
		return
	if Input.is_action_just_pressed("LeftMouse") and hovered != -1:
		if !scene.begin_pointer_action(self):
			return
		dragging = true
		drag_handle = hovered
		drag_start_point = handles[hovered].global_position
		drag_plane = Plane(-cam.global_basis.z.normalized(), drag_start_point)
		drag_snapshot = {
			"handle": hovered,
			"kind": handle_curve_kinds[hovered],
			"samples": _build_drag_surface_snapshot(handle_curve_kinds[hovered]),
		}
		get_viewport().set_input_as_handled()
	if Input.is_action_just_released("LeftMouse"):
		if dragging or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		dragging = false
		drag_handle = -1
		drag_snapshot.clear()
		scene.end_pointer_action(self)
	if dragging and drag_handle != -1:
		var hit = drag_plane.intersects_ray(cam.global_position, ray_dir)
		if hit is Vector3:
			_write_dragged_handle(modulation, drag_handle, hit)
			_update_mesh(false)
			_update_visuals(modulation)
