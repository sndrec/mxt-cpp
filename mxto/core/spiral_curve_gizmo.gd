class_name SpiralCurveGizmo extends Node3D

const HANDLE_RADIUS := 4.0
const SEARCH_STEPS := 10
const SEARCH_PASSES := 5
const PREVIEW_STEPS := 32
const MIN_POINT_GAP := 0.001
const ADD_POINT_MAX_SCREEN_DISTANCE := 60.0

var mouse_cast : RayCast3D
var target_path : RoadPath
var dragging := false
var drag_handle := -1
var delete_pressed := false
var drag_plane := Plane.PLANE_XZ
var outline_mesh_instance : MeshInstance3D
var outline_mesh := ImmediateMesh.new()
var outline_material := StandardMaterial3D.new()
var handle_materials : Array[StandardMaterial3D] = []
var handles : Array[StaticBody3D] = []
var handle_curve_indices : Array[int] = []
var handle_point_indices : Array[int] = []
var curve_entries : Array[Dictionary] = []

func _ready() -> void:
	outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	outline_material.vertex_color_use_as_albedo = true
	outline_mesh_instance = MeshInstance3D.new()
	outline_mesh_instance.mesh = outline_mesh
	outline_mesh_instance.top_level = true
	add_child(outline_mesh_instance)
	set_target_path(null)

func set_target_path(in_path : RoadPath) -> void:
	target_path = in_path
	dragging = false
	drag_handle = -1

func _is_spiral_path(path : Node) -> bool:
	var script : Script = path.get_script() as Script if path else null
	return script and script.resource_path == "res://core/road_path_spiral.gd"

func _curve_color(index : int) -> Color:
	var palette : Array[Color] = [
		Color(0.98, 0.50, 0.18, 1.0),
		Color(0.24, 0.78, 1.0, 1.0),
		Color(0.92, 0.86, 0.28, 1.0),
		Color(0.55, 1.0, 0.42, 1.0),
		Color(1.0, 0.38, 0.72, 1.0),
	]
	return palette[index % palette.size()]

func _curve_hover_color(index : int) -> Color:
	return _curve_color(index).lerp(Color.WHITE, 0.45)

func _entry_lane_offset(entry_index : int) -> float:
	if curve_entries.size() <= 1:
		return 0.0
	return lerpf(-48.0, 48.0, float(entry_index) / float(curve_entries.size() - 1))

func _collect_curve_entries() -> void:
	curve_entries.clear()
	if !_is_spiral_path(target_path):
		return
	target_path.call("_ensure_spiral_curves")
	curve_entries.append({"name": "radius", "curve": target_path.get("radius_curve"), "min": 0.0, "scale": 1.0})
	curve_entries.append({"name": "height", "curve": target_path.get("height_curve"), "min": -INF, "scale": 1.0})
	curve_entries.append({"name": "twist", "curve": target_path.get("twist_curve"), "min": -INF, "scale": 0.75})
	curve_entries.append({"name": "scale_x", "curve": target_path.get("scale_x_curve"), "min": 0.05, "scale": 2.0})
	curve_entries.append({"name": "scale_y", "curve": target_path.get("scale_y_curve"), "min": 0.05, "scale": 2.0})
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
		return target_path.call("_sample_spiral_transform", clampf(t, 0.0, 1.0))
	return Transform3D.IDENTITY

func _basis_at(t : float) -> Basis:
	return _sample_transform(t).basis.orthonormalized()

func _base_position(entry_index : int, t : float) -> Vector3:
	var transform := _sample_transform(t)
	return transform.origin + transform.basis.orthonormalized().x * _entry_lane_offset(entry_index)

func _point_position(entry_index : int, point_index : int) -> Vector3:
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	var t := _curve_offset(curve, point_index)
	var normal := _basis_at(t).y.normalized()
	return _base_position(entry_index, t) + normal * _curve_value(curve, point_index) * float(entry["scale"])

func _make_handle(entry_index : int) -> StaticBody3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = _curve_color(entry_index)
	handle_materials.append(material)
	var body := StaticBody3D.new()
	body.set_collision_layer_value(16, true)
	body.set_collision_mask_value(16, true)
	body.set_collision_layer_value(1, false)
	body.set_collision_mask_value(1, false)
	var collision := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = HANDLE_RADIUS
	collision.shape = sphere
	body.add_child(collision)
	var mesh_instance := MeshInstance3D.new()
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = HANDLE_RADIUS
	sphere_mesh.height = HANDLE_RADIUS * 2.0
	mesh_instance.mesh = sphere_mesh
	mesh_instance.material_override = material
	body.add_child(mesh_instance)
	add_child(body)
	return body

func _sync_handles() -> void:
	handle_curve_indices.clear()
	handle_point_indices.clear()
	for entry_index in curve_entries.size():
		var curve : Resource = curve_entries[entry_index]["curve"]
		for point_index in curve.point_count:
			handle_curve_indices.append(entry_index)
			handle_point_indices.append(point_index)
	while handles.size() < handle_curve_indices.size():
		handles.append(_make_handle(handle_curve_indices[handles.size()]))
	while handles.size() > handle_curve_indices.size():
		var handle : StaticBody3D = handles.pop_back()
		handle_materials.pop_back()
		handle.queue_free()
	for i in handles.size():
		handle_materials[i].albedo_color = _curve_color(handle_curve_indices[i])

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _draw_curve_preview(entry_index : int) -> void:
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	outline_mesh.surface_set_color(_curve_color(entry_index))
	for i in PREVIEW_STEPS:
		var t0 := float(i) / float(PREVIEW_STEPS)
		var t1 := float(i + 1) / float(PREVIEW_STEPS)
		var p0 : Vector3 = _base_position(entry_index, t0) + _basis_at(t0).y.normalized() * curve.sample(t0) * float(entry["scale"])
		var p1 : Vector3 = _base_position(entry_index, t1) + _basis_at(t1).y.normalized() * curve.sample(t1) * float(entry["scale"])
		outline_mesh.surface_add_vertex(p0)
		outline_mesh.surface_add_vertex(p1)

func _update_visuals() -> void:
	_collect_curve_entries()
	_sync_handles()
	for i in handles.size():
		handles[i].global_position = _point_position(handle_curve_indices[i], handle_point_indices[i])
	outline_mesh.clear_surfaces()
	if curve_entries.is_empty():
		outline_mesh_instance.visible = false
		return
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	for entry_index in curve_entries.size():
		_draw_curve_preview(entry_index)
	outline_mesh.surface_end()
	outline_mesh_instance.visible = true

func _hovered_handle() -> int:
	if !mouse_cast or !mouse_cast.is_colliding():
		return -1
	var collider := mouse_cast.get_collider()
	for i in handles.size():
		if collider == handles[i]:
			return i
	return -1

func _set_handle_colours(hovered : int) -> void:
	for i in handle_materials.size():
		var entry_index := handle_curve_indices[i]
		if dragging and i == drag_handle:
			handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered:
			handle_materials[i].albedo_color = _curve_hover_color(entry_index)
		else:
			handle_materials[i].albedo_color = _curve_color(entry_index)

func _closest_t(world_pos : Vector3, entry_index : int) -> float:
	var best_t := 0.0
	var best_dist := INF
	var min_t := 0.0
	var max_t := 1.0
	for pass_index in SEARCH_PASSES:
		for i in SEARCH_STEPS + 1:
			var t := lerpf(min_t, max_t, float(i) / float(SEARCH_STEPS))
			var dist := _base_position(entry_index, t).distance_squared_to(world_pos)
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

func _write_dragged_handle(handle_id : int, world_pos : Vector3) -> void:
	var entry_index := handle_curve_indices[handle_id]
	var point_index := handle_point_indices[handle_id]
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	var t := _clamped_curve_offset(curve, point_index, _closest_t(world_pos, entry_index))
	var normal := _basis_at(t).y.normalized()
	var value := (world_pos - _base_position(entry_index, t)).dot(normal) / float(entry["scale"])
	value = maxf(value, float(entry["min"]))
	curve.set_point_offset(point_index, t)
	curve.set_point_value(point_index, value)

func _screen_distance_to_curve(entry_index : int, cam : Camera3D, mouse_pos : Vector2) -> Dictionary:
	var entry := curve_entries[entry_index]
	var curve : Resource = entry["curve"]
	var best_offset := 0.0
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	for i in PREVIEW_STEPS + 1:
		var offset := float(i) / float(PREVIEW_STEPS)
		var world : Vector3 = _base_position(entry_index, offset) + _basis_at(offset).y.normalized() * curve.sample(offset) * float(entry["scale"])
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
	if !just_delete or hovered < 0:
		return false
	var curve : Resource = curve_entries[handle_curve_indices[hovered]]["curve"]
	var point_index := handle_point_indices[hovered]
	if point_index <= 0 or point_index >= curve.point_count - 1:
		return false
	curve.remove_point(point_index)
	_update_mesh(false)
	_update_visuals()
	get_viewport().set_input_as_handled()
	return true

func _process(_delta : float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_spiral_gizmos() or !_is_spiral_path(target_path):
		if scene:
			scene.end_pointer_action(self)
		visible = false
		outline_mesh_instance.visible = false
		_set_colliders_enabled(false)
		dragging = false
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	visible = true
	_update_visuals()
	if curve_entries.is_empty():
		_set_colliders_enabled(false)
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	_set_colliders_enabled(true)
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var ray_dir := cam.project_ray_normal(get_viewport().get_mouse_position())
	var hovered := _hovered_handle()
	_set_handle_colours(hovered)
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_point(hovered):
		return
	if hovered == -1 and !scene.pointer_action_busy_for(self) and _try_alt_add_point(cam):
		return
	if Input.is_action_just_pressed("LeftMouse") and hovered != -1:
		if !scene.begin_pointer_action(self):
			return
		dragging = true
		drag_handle = hovered
		drag_plane = Plane(-cam.global_basis.z.normalized(), handles[hovered].global_position)
		get_viewport().set_input_as_handled()
	if Input.is_action_just_released("LeftMouse"):
		if dragging or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		dragging = false
		drag_handle = -1
		scene.end_pointer_action(self)
	if dragging and drag_handle != -1:
		var hit = drag_plane.intersects_ray(cam.global_position, ray_dir)
		if hit is Vector3:
			_write_dragged_handle(drag_handle, hit)
			_update_mesh(false)
			_update_visuals()
