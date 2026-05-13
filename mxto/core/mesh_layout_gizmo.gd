class_name MeshLayoutGizmo extends Node3D

const HANDLE_RADIUS := 3.5
const SEARCH_STEPS := 10
const SEARCH_PASSES := 5
const PREVIEW_STEPS := 64
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

func _cross_section_t() -> float:
	var scene := FZGlobal.editing_scene
	if !scene:
		return 0.5
	return clampf(scene.editor_cross_section_t, 0.0, 1.0)

func _handle_color() -> Color:
	return Color(0.98, 0.82, 0.38, 1.0)

func _hover_color() -> Color:
	return Color(1.0, 0.96, 0.62, 1.0)

func _surface_point(tx : float) -> Vector3:
	var points := target_path.get_surface_positions(PackedVector2Array([Vector2(clampf(tx, -1.0, 1.0), _cross_section_t())]))
	if points.is_empty():
		return Vector3.ZERO
	return points[0]

func _sync_handles() -> void:
	while handles.size() < target_path.horizontal_road_mesh_segments.size():
		handles.append(_make_handle())
	while handles.size() > target_path.horizontal_road_mesh_segments.size():
		var handle : StaticBody3D = handles.pop_back()
		handle_materials.pop_back()
		handle.queue_free()
	for i in handles.size():
		handle_materials[i].albedo_color = _handle_color()

func _make_handle() -> StaticBody3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = _handle_color()
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

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _update_visuals() -> void:
	_sync_handles()
	for i in handles.size():
		var tx := target_path.horizontal_road_mesh_segments[i] * 2.0 - 1.0
		handles[i].global_position = _surface_point(tx)
	outline_mesh.clear_surfaces()
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	outline_mesh.surface_set_color(Color(0.98, 0.82, 0.38, 1.0))
	for i in PREVIEW_STEPS:
		var tx0 := lerpf(-1.0, 1.0, float(i) / float(PREVIEW_STEPS))
		var tx1 := lerpf(-1.0, 1.0, float(i + 1) / float(PREVIEW_STEPS))
		outline_mesh.surface_add_vertex(_surface_point(tx0))
		outline_mesh.surface_add_vertex(_surface_point(tx1))
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
			handle_materials[i].albedo_color = _hover_color()
		else:
			handle_materials[i].albedo_color = _handle_color()

func _closest_segment_value(world_pos : Vector3) -> float:
	var best_value := 0.0
	var best_dist := INF
	var min_value := 0.0
	var max_value := 1.0
	for pass_index in SEARCH_PASSES:
		for i in SEARCH_STEPS + 1:
			var value := lerpf(min_value, max_value, float(i) / float(SEARCH_STEPS))
			var tx := value * 2.0 - 1.0
			var dist := _surface_point(tx).distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_value = value
		var radius := (max_value - min_value) / float(SEARCH_STEPS)
		min_value = maxf(0.0, best_value - radius)
		max_value = minf(1.0, best_value + radius)
	return best_value

func _write_dragged_handle(handle_id : int, world_pos : Vector3) -> void:
	if handle_id < 0 or handle_id >= target_path.horizontal_road_mesh_segments.size():
		return
	target_path.horizontal_road_mesh_segments[handle_id] = clampf(_closest_segment_value(world_pos), 0.0, 1.0)
	target_path.horizontal_road_mesh_segments.sort()

func _screen_distance_to_cross_section(cam : Camera3D, mouse_pos : Vector2) -> Dictionary:
	var best_value := 0.0
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	for i in PREVIEW_STEPS + 1:
		var value := float(i) / float(PREVIEW_STEPS)
		var world := _surface_point(value * 2.0 - 1.0)
		var screen := cam.unproject_position(world)
		if i > 0:
			var closest := Geometry2D.get_closest_point_to_segment(mouse_pos, previous_screen, screen)
			var dist := mouse_pos.distance_squared_to(closest)
			if dist < best_dist:
				var span_value := 1.0 / float(PREVIEW_STEPS)
				var span_len := previous_screen.distance_to(screen)
				var local_t := 0.0 if span_len <= 0.001 else previous_screen.distance_to(closest) / span_len
				best_value = clampf(float(i - 1) / float(PREVIEW_STEPS) + span_value * local_t, 0.0, 1.0)
				best_dist = dist
		previous_screen = screen
	return {
		"value": best_value,
		"distance": best_dist,
	}

func _try_alt_add_point(cam : Camera3D) -> bool:
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return false
	var pick := _screen_distance_to_cross_section(cam, get_viewport().get_mouse_position())
	if float(pick["distance"]) > ADD_POINT_MAX_SCREEN_DISTANCE * ADD_POINT_MAX_SCREEN_DISTANCE:
		return false
	var scene := FZGlobal.editing_scene
	if scene and !scene.begin_pointer_action(self):
		return false
	target_path.horizontal_road_mesh_segments.append(clampf(float(pick["value"]), 0.0, 1.0))
	target_path.horizontal_road_mesh_segments.sort()
	_update_mesh(false)
	_update_visuals()
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
	if hovered <= 0 or hovered >= target_path.horizontal_road_mesh_segments.size() - 1:
		return false
	target_path.horizontal_road_mesh_segments.remove_at(hovered)
	_update_mesh(false)
	_update_visuals()
	get_viewport().set_input_as_handled()
	return true

func _update_mesh(force_collision := false) -> void:
	var update_collision := force_collision or Time.get_ticks_msec() > target_path.last_gen_time + 100
	if update_collision:
		target_path.last_gen_time = Time.get_ticks_msec()
	target_path._try_generate_mesh(update_collision)

func _process(delta : float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_mesh_layout_gizmos() or !is_instance_valid(target_path):
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
