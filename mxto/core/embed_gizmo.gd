class_name EmbedGizmo extends Node3D

enum BoundaryKind {
	LEFT,
	RIGHT,
}

enum HandleKind {
	POINT,
	LEFT_TANGENT,
	RIGHT_TANGENT,
}

const HANDLE_RADIUS := 4.0
const TANGENT_HANDLE_RADIUS := 2.5
const SURFACE_SEARCH_STEPS := 10
const SURFACE_SEARCH_PASSES := 5
const PREVIEW_STEPS := 24
const MIN_POINT_GAP := 0.001
const TANGENT_HANDLE_OFFSET := 0.08
const ADD_POINT_MAX_SCREEN_DISTANCE := 60.0

var mouse_cast : RayCast3D
var target_path : RoadPath
var embed_index := -1
var dragging := false
var drag_handle := -1
var delete_pressed := false
var drag_plane := Plane.PLANE_XZ
var outline_mesh_instance : MeshInstance3D
var outline_mesh := ImmediateMesh.new()
var outline_material := StandardMaterial3D.new()
var handle_materials : Array[StandardMaterial3D] = []
var handles : Array[StaticBody3D] = []
var handle_boundaries : Array[int] = []
var handle_indices : Array[int] = []
var handle_kinds : Array[int] = []

func _ready() -> void:
	outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	outline_material.vertex_color_use_as_albedo = true
	outline_mesh_instance = MeshInstance3D.new()
	outline_mesh_instance.mesh = outline_mesh
	outline_mesh_instance.top_level = true
	add_child(outline_mesh_instance)
	set_target_embed(null, -1)

func set_target_embed(in_path : RoadPath, in_embed_index : int) -> void:
	target_path = in_path
	embed_index = in_embed_index
	dragging = false
	drag_handle = -1

func _active_embed() -> RoadEmbed:
	if !is_instance_valid(target_path):
		return null
	if embed_index < 0 or embed_index >= target_path.road_shape.embed_table.size():
		return null
	return target_path.road_shape.embed_table[embed_index]

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _boundary_curve(embed : RoadEmbed, boundary : int) -> Resource:
	return embed.right_boundary if boundary == BoundaryKind.RIGHT else embed.left_boundary

func _opposite_curve(embed : RoadEmbed, boundary : int) -> Resource:
	return embed.left_boundary if boundary == BoundaryKind.RIGHT else embed.right_boundary

func _ensure_boundary_curve(curve : Resource, fallback_value : float, start_t : float, end_t : float) -> void:
	while curve.point_count < 2:
		var offset := start_t if curve.point_count == 0 else end_t
		curve.add_point(Vector2(offset, fallback_value))
	curve.set_point_offset(0, start_t)
	curve.set_point_offset(curve.point_count - 1, end_t)

func _ensure_embed_curves(embed : RoadEmbed) -> void:
	_ensure_boundary_curve(embed.left_boundary, -0.35, embed.road_start, embed.road_end)
	_ensure_boundary_curve(embed.right_boundary, 0.35, embed.road_start, embed.road_end)

func _curve_value(curve : Resource, point_index : int, fallback : float) -> float:
	if !curve or curve.point_count <= point_index:
		return fallback
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

func _curve_point_position(embed : RoadEmbed, boundary : int, point_index : int) -> Vector3:
	var curve := _boundary_curve(embed, boundary)
	var point : Vector2 = curve.get_point_position(point_index)
	return _surface_point(point.y, point.x)

func _tangent_handle_position(embed : RoadEmbed, boundary : int, point_index : int, kind : int) -> Vector3:
	var curve := _boundary_curve(embed, boundary)
	var point : Vector2 = curve.get_point_position(point_index)
	var offset_delta := TANGENT_HANDLE_OFFSET
	var tangent := 0.0
	if kind == HandleKind.LEFT_TANGENT:
		offset_delta = -minf(TANGENT_HANDLE_OFFSET, point.x)
		tangent = curve.get_point_left_tangent(point_index)
	else:
		offset_delta = minf(TANGENT_HANDLE_OFFSET, 1.0 - point.x)
		tangent = curve.get_point_right_tangent(point_index)
	if absf(offset_delta) <= 0.00001:
		return _curve_point_position(embed, boundary, point_index)
	return _surface_point(clampf(point.y + tangent * offset_delta, -1.0, 1.0), point.x + offset_delta)

func _handle_position(embed : RoadEmbed, handle_id : int) -> Vector3:
	var boundary := handle_boundaries[handle_id]
	var point_index := handle_indices[handle_id]
	var kind := handle_kinds[handle_id]
	if kind == HandleKind.POINT:
		return _curve_point_position(embed, boundary, point_index)
	return _tangent_handle_position(embed, boundary, point_index, kind)

func _handle_base_color(handle_id : int) -> Color:
	var boundary := handle_boundaries[handle_id]
	var kind := handle_kinds[handle_id]
	if kind == HandleKind.POINT:
		return Color(0.15, 0.9, 0.95, 1.0) if boundary == BoundaryKind.LEFT else Color(0.28, 0.55, 1.0, 1.0)
	return Color(0.55, 0.95, 0.95, 1.0) if boundary == BoundaryKind.LEFT else Color(0.62, 0.78, 1.0, 1.0)

func _boundary_color(boundary : int) -> Color:
	return Color(0.15, 0.9, 0.95, 1.0) if boundary == BoundaryKind.LEFT else Color(0.28, 0.55, 1.0, 1.0)

func _make_handle(boundary : int, kind : int) -> StaticBody3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color.WHITE
	handle_materials.append(material)
	var body := StaticBody3D.new()
	body.set_collision_layer_value(16, true)
	body.set_collision_mask_value(16, true)
	body.set_collision_layer_value(1, false)
	body.set_collision_mask_value(1, false)
	var collision := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = TANGENT_HANDLE_RADIUS if kind != HandleKind.POINT else HANDLE_RADIUS
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

func _sync_handles(embed : RoadEmbed) -> void:
	_ensure_embed_curves(embed)
	handle_boundaries.clear()
	handle_indices.clear()
	handle_kinds.clear()
	for boundary in [BoundaryKind.LEFT, BoundaryKind.RIGHT]:
		var curve := _boundary_curve(embed, boundary)
		for point_index in curve.point_count:
			handle_boundaries.append(boundary)
			handle_indices.append(point_index)
			handle_kinds.append(HandleKind.POINT)
			if point_index > 0:
				handle_boundaries.append(boundary)
				handle_indices.append(point_index)
				handle_kinds.append(HandleKind.LEFT_TANGENT)
			if point_index < curve.point_count - 1:
				handle_boundaries.append(boundary)
				handle_indices.append(point_index)
				handle_kinds.append(HandleKind.RIGHT_TANGENT)
	while handles.size() < handle_kinds.size():
		handles.append(_make_handle(handle_boundaries[handles.size()], handle_kinds[handles.size()]))
	while handles.size() > handle_kinds.size():
		var handle : StaticBody3D = handles.pop_back()
		handle_materials.pop_back()
		handle.queue_free()

func _set_handle_colours(hovered : int) -> void:
	for i in handle_materials.size():
		if dragging and i == drag_handle:
			handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered:
			handle_materials[i].albedo_color = Color(1.0, 0.76, 0.25, 1.0)
		else:
			handle_materials[i].albedo_color = _handle_base_color(i)

func _draw_boundary_curve(embed : RoadEmbed, boundary : int) -> void:
	var curve := _boundary_curve(embed, boundary)
	outline_mesh.surface_set_color(_boundary_color(boundary))
	for i in PREVIEW_STEPS:
		var u0 := lerpf(embed.road_start, embed.road_end, float(i) / float(PREVIEW_STEPS))
		var u1 := lerpf(embed.road_start, embed.road_end, float(i + 1) / float(PREVIEW_STEPS))
		outline_mesh.surface_add_vertex(_surface_point(curve.sample(u0), u0))
		outline_mesh.surface_add_vertex(_surface_point(curve.sample(u1), u1))

func _draw_cross_edge(embed : RoadEmbed, curve_offset : float) -> void:
	outline_mesh.surface_set_color(Color(0.15, 0.9, 0.95, 0.85))
	outline_mesh.surface_add_vertex(_surface_point(embed.left_boundary.sample(curve_offset), curve_offset))
	outline_mesh.surface_add_vertex(_surface_point(embed.right_boundary.sample(curve_offset), curve_offset))

func _draw_tangent_handles(embed : RoadEmbed) -> void:
	outline_mesh.surface_set_color(Color(0.85, 0.95, 1.0, 0.85))
	for handle_id in handles.size():
		if handle_kinds[handle_id] == HandleKind.POINT:
			continue
		var point_pos := _curve_point_position(embed, handle_boundaries[handle_id], handle_indices[handle_id])
		var handle_pos := _handle_position(embed, handle_id)
		outline_mesh.surface_add_vertex(point_pos)
		outline_mesh.surface_add_vertex(handle_pos)

func _update_visuals(embed : RoadEmbed) -> void:
	_sync_handles(embed)
	for i in handles.size():
		handles[i].global_position = _handle_position(embed, i)
	outline_mesh.clear_surfaces()
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	_draw_boundary_curve(embed, BoundaryKind.LEFT)
	_draw_boundary_curve(embed, BoundaryKind.RIGHT)
	_draw_cross_edge(embed, embed.road_start)
	_draw_cross_edge(embed, embed.road_end)
	_draw_tangent_handles(embed)
	outline_mesh.surface_end()

func _hovered_handle() -> int:
	if !mouse_cast or !mouse_cast.is_colliding():
		return -1
	var collider := mouse_cast.get_collider()
	for i in handles.size():
		if collider == handles[i]:
			return i
	return -1

func _closest_surface_param(world_pos : Vector3) -> Vector2:
	var best_param := Vector2.ZERO
	var best_dist := INF
	var min_tx := -1.0
	var max_tx := 1.0
	var min_ty := 0.0
	var max_ty := 1.0
	for pass_index in SURFACE_SEARCH_PASSES:
		var query := PackedVector2Array()
		for y in SURFACE_SEARCH_STEPS + 1:
			var ty := lerpf(min_ty, max_ty, float(y) / float(SURFACE_SEARCH_STEPS))
			for x in SURFACE_SEARCH_STEPS + 1:
				var tx := lerpf(min_tx, max_tx, float(x) / float(SURFACE_SEARCH_STEPS))
				query.append(Vector2(tx, ty))
		var positions := target_path.get_surface_positions(query)
		for i in positions.size():
			var dist := positions[i].distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_param = query[i]
		var tx_radius := (max_tx - min_tx) / float(SURFACE_SEARCH_STEPS)
		var ty_radius := (max_ty - min_ty) / float(SURFACE_SEARCH_STEPS)
		min_tx = maxf(-1.0, best_param.x - tx_radius)
		max_tx = minf(1.0, best_param.x + tx_radius)
		min_ty = maxf(0.0, best_param.y - ty_radius)
		max_ty = minf(1.0, best_param.y + ty_radius)
	return best_param

func _clamped_curve_offset(curve : Resource, point_index : int, offset : float) -> float:
	if point_index == 0:
		return offset
	if point_index == curve.point_count - 1:
		return offset
	var min_offset := _curve_offset(curve, point_index - 1) + MIN_POINT_GAP
	var max_offset := _curve_offset(curve, point_index + 1) - MIN_POINT_GAP
	return clampf(offset, min_offset, max_offset)

func _write_point_handle(embed : RoadEmbed, boundary : int, point_index : int, param : Vector2) -> void:
	var curve := _boundary_curve(embed, boundary)
	var opposite := _opposite_curve(embed, boundary)
	var curve_offset := _clamped_curve_offset(curve, point_index, clampf(param.y, 0.0, 1.0))
	if point_index == 0:
		embed.road_start = clampf(param.y, 0.0, embed.road_end - 0.01)
		embed.left_boundary.set_point_offset(0, embed.road_start)
		embed.right_boundary.set_point_offset(0, embed.road_start)
		curve_offset = embed.road_start
	elif point_index == curve.point_count - 1:
		embed.road_end = clampf(param.y, embed.road_start + 0.01, 1.0)
		embed.left_boundary.set_point_offset(embed.left_boundary.point_count - 1, embed.road_end)
		embed.right_boundary.set_point_offset(embed.right_boundary.point_count - 1, embed.road_end)
		curve_offset = embed.road_end
	else:
		curve.set_point_offset(point_index, curve_offset)
	var opposite_value : float = opposite.sample(curve_offset)
	var value := clampf(param.x, -1.0, opposite_value - 0.01) if boundary == BoundaryKind.LEFT else clampf(param.x, opposite_value + 0.01, 1.0)
	curve.set_point_value(point_index, value)

func _write_tangent_handle(embed : RoadEmbed, boundary : int, point_index : int, kind : int, param : Vector2) -> void:
	var curve := _boundary_curve(embed, boundary)
	var point : Vector2 = curve.get_point_position(point_index)
	var handle_offset := clampf(param.y, 0.0, 1.0)
	var delta : float = handle_offset - point.x
	if kind == HandleKind.LEFT_TANGENT:
		delta = minf(delta, -MIN_POINT_GAP)
		curve.set_point_left_tangent(point_index, (param.x - point.y) / delta)
	else:
		delta = maxf(delta, MIN_POINT_GAP)
		curve.set_point_right_tangent(point_index, (param.x - point.y) / delta)

func _write_handle(embed : RoadEmbed, handle_id : int, param : Vector2) -> void:
	var boundary := handle_boundaries[handle_id]
	var point_index := handle_indices[handle_id]
	var kind := handle_kinds[handle_id]
	if kind == HandleKind.POINT:
		_write_point_handle(embed, boundary, point_index, param)
	else:
		_write_tangent_handle(embed, boundary, point_index, kind, param)

func _screen_distance_to_boundary(embed : RoadEmbed, boundary : int, cam : Camera3D, mouse_pos : Vector2) -> Dictionary:
	var curve := _boundary_curve(embed, boundary)
	var best_offset := embed.road_start
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	for i in PREVIEW_STEPS + 1:
		var offset := lerpf(embed.road_start, embed.road_end, float(i) / float(PREVIEW_STEPS))
		var world := _surface_point(curve.sample(offset), offset)
		var screen := cam.unproject_position(world)
		if i > 0:
			var closest := Geometry2D.get_closest_point_to_segment(mouse_pos, previous_screen, screen)
			var dist := mouse_pos.distance_squared_to(closest)
			if dist < best_dist:
				var span_offset := 1.0 / float(PREVIEW_STEPS)
				var span_len := previous_screen.distance_to(screen)
				var local_t := 0.0 if span_len <= 0.001 else previous_screen.distance_to(closest) / span_len
				var embed_t := clampf(float(i - 1) / float(PREVIEW_STEPS) + span_offset * local_t, 0.0, 1.0)
				best_offset = lerpf(embed.road_start, embed.road_end, embed_t)
				best_dist = dist
		previous_screen = screen
	return {
		"offset": best_offset,
		"distance": best_dist,
	}

func _try_alt_add_point(embed : RoadEmbed, cam : Camera3D) -> bool:
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return false
	var mouse_pos := get_viewport().get_mouse_position()
	var left_pick := _screen_distance_to_boundary(embed, BoundaryKind.LEFT, cam, mouse_pos)
	var right_pick := _screen_distance_to_boundary(embed, BoundaryKind.RIGHT, cam, mouse_pos)
	var best_distance : float = minf(left_pick["distance"], right_pick["distance"])
	if best_distance > ADD_POINT_MAX_SCREEN_DISTANCE * ADD_POINT_MAX_SCREEN_DISTANCE:
		return false
	var boundary := BoundaryKind.RIGHT if right_pick["distance"] < left_pick["distance"] else BoundaryKind.LEFT
	var offset : float = right_pick["offset"] if boundary == BoundaryKind.RIGHT else left_pick["offset"]
	offset = clampf(offset, embed.road_start + MIN_POINT_GAP, embed.road_end - MIN_POINT_GAP)
	var scene := FZGlobal.editing_scene
	if scene and !scene.begin_pointer_action(self):
		return false
	var curve := _boundary_curve(embed, boundary)
	curve.add_point(Vector2(offset, curve.sample(offset)))
	_update_mesh(false)
	_update_visuals(embed)
	if scene:
		scene.end_pointer_action(self)
	get_viewport().set_input_as_handled()
	return true

func _try_delete_hovered_point(embed : RoadEmbed, hovered : int) -> bool:
	var delete_now := Input.is_key_pressed(KEY_DELETE)
	var just_delete := delete_now and !delete_pressed
	delete_pressed = delete_now
	if !just_delete or hovered < 0:
		return false
	if handle_kinds[hovered] != HandleKind.POINT:
		return false
	var curve := _boundary_curve(embed, handle_boundaries[hovered])
	var point_index := handle_indices[hovered]
	if point_index <= 0 or point_index >= curve.point_count - 1:
		return false
	curve.remove_point(point_index)
	_update_mesh(false)
	_update_visuals(embed)
	get_viewport().set_input_as_handled()
	return true

func _update_mesh(force_collision := false) -> void:
	var update_collision := force_collision or Time.get_ticks_msec() > target_path.last_gen_time + 100
	if update_collision:
		target_path.last_gen_time = Time.get_ticks_msec()
	target_path._try_generate_mesh(update_collision)

func _process(delta : float) -> void:
	var scene := FZGlobal.editing_scene
	var embed := _active_embed()
	if !scene or !scene.tool_mode_allows_embed_gizmos() or !embed:
		if scene:
			scene.end_pointer_action(self)
		visible = false
		outline_mesh_instance.visible = false
		_set_colliders_enabled(false)
		dragging = false
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	visible = true
	outline_mesh_instance.visible = true
	_set_colliders_enabled(true)
	_update_visuals(embed)
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var ray_dir := cam.project_ray_normal(get_viewport().get_mouse_position())
	var hovered := _hovered_handle()
	_set_handle_colours(hovered)
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_point(embed, hovered):
		return
	if hovered == -1 and !scene.pointer_action_busy_for(self) and _try_alt_add_point(embed, cam):
		return
	if Input.is_action_just_pressed("LeftMouse") and hovered != -1:
		if !scene.begin_pointer_action(self):
			return
		dragging = true
		drag_handle = hovered
		drag_plane = Plane(-cam.global_basis.z.normalized(), handles[hovered].global_position)
		get_viewport().set_input_as_handled()
	if Input.is_action_just_released("LeftMouse"):
		dragging = false
		drag_handle = -1
		scene.end_pointer_action(self)
	if dragging and drag_handle != -1:
		var hit = drag_plane.intersects_ray(cam.global_position, ray_dir)
		if hit is Vector3:
			_write_handle(embed, drag_handle, _closest_surface_param(hit))
			_update_mesh(false)
			_update_visuals(embed)
