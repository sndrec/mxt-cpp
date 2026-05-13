class_name CheckpointGizmo extends Node3D

const HANDLE_RADIUS := 4.0
const PREVIEW_NORMAL_LENGTH := 20.0
const PREVIEW_STEPS_MAX := 128
const ADD_POINT_MAX_SCREEN_DISTANCE := 60.0

var mouse_cast : RayCast3D
var target_path : RoadPath
var dragging := false
var delete_pressed := false
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

func _checkpoint_span_count() -> int:
	if !is_instance_valid(target_path):
		return 0
	return maxi(1, target_path.num_checkpoints + 1)

func _handle_count() -> int:
	return maxi(0, _checkpoint_span_count() - 1)

func _handle_color() -> Color:
	return Color(0.42, 1.0, 0.92, 1.0)

func _hover_color() -> Color:
	return Color(1.0, 0.86, 0.35, 1.0)

func _surface_point(tx : float, ty : float) -> Vector3:
	var points := target_path.get_surface_positions(PackedVector2Array([Vector2(clampf(tx, -1.0, 1.0), clampf(ty, 0.0, 1.0))]))
	if points.is_empty():
		return Vector3.ZERO
	return points[0]

func _surface_normal(ty : float) -> Vector3:
	var center := _surface_point(0.0, ty)
	var right := _surface_point(0.05, ty)
	var ahead := _surface_point(0.0, minf(1.0, ty + 0.002))
	if is_equal_approx(ty, 1.0):
		ahead = _surface_point(0.0, maxf(0.0, ty - 0.002))
	var tangent_x := (right - center).normalized()
	var tangent_y := (ahead - center).normalized()
	var normal := tangent_y.cross(tangent_x).normalized()
	if normal.is_zero_approx():
		return Vector3.UP
	return normal

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

func _sync_handles() -> void:
	var count := _handle_count()
	while handles.size() < count:
		handles.append(_make_handle())
	while handles.size() > count:
		var handle : StaticBody3D = handles.pop_back()
		handle_materials.pop_back()
		handle.queue_free()
	for i in handles.size():
		handle_materials[i].albedo_color = _handle_color()

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _handle_ty(handle_id : int) -> float:
	return float(handle_id + 1) / float(_checkpoint_span_count())

func _update_visuals() -> void:
	_sync_handles()
	outline_mesh.clear_surfaces()
	if !is_instance_valid(target_path):
		outline_mesh_instance.visible = false
		return
	for i in handles.size():
		handles[i].global_position = _surface_point(0.0, _handle_ty(i))
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	outline_mesh.surface_set_color(Color(0.42, 1.0, 0.92, 1.0))
	for i in _checkpoint_span_count() + 1:
		var ty := float(i) / float(_checkpoint_span_count())
		var left := _surface_point(-1.0, ty)
		var right := _surface_point(1.0, ty)
		var center := _surface_point(0.0, ty)
		var normal := _surface_normal(ty)
		outline_mesh.surface_add_vertex(left)
		outline_mesh.surface_add_vertex(right)
		outline_mesh.surface_add_vertex(center)
		outline_mesh.surface_add_vertex(center + normal * PREVIEW_NORMAL_LENGTH)
	outline_mesh.surface_set_color(Color(0.42, 1.0, 0.92, 0.55))
	var steps := mini(PREVIEW_STEPS_MAX, maxi(8, _checkpoint_span_count() * 4))
	for i in steps:
		var t0 := float(i) / float(steps)
		var t1 := float(i + 1) / float(steps)
		outline_mesh.surface_add_vertex(_surface_point(0.0, t0))
		outline_mesh.surface_add_vertex(_surface_point(0.0, t1))
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
		if i == hovered:
			handle_materials[i].albedo_color = _hover_color()
		else:
			handle_materials[i].albedo_color = _handle_color()

func _screen_distance_to_centerline(cam : Camera3D, mouse_pos : Vector2) -> float:
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	var steps := mini(PREVIEW_STEPS_MAX, maxi(8, _checkpoint_span_count() * 4))
	for i in steps + 1:
		var ty := float(i) / float(steps)
		var screen := cam.unproject_position(_surface_point(0.0, ty))
		if i > 0:
			var closest := Geometry2D.get_closest_point_to_segment(mouse_pos, previous_screen, screen)
			best_dist = minf(best_dist, mouse_pos.distance_squared_to(closest))
		previous_screen = screen
	return best_dist

func _try_alt_add_checkpoint(cam : Camera3D) -> bool:
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return false
	var dist := _screen_distance_to_centerline(cam, get_viewport().get_mouse_position())
	if dist > ADD_POINT_MAX_SCREEN_DISTANCE * ADD_POINT_MAX_SCREEN_DISTANCE:
		return false
	var scene := FZGlobal.editing_scene
	if scene and !scene.begin_pointer_action(self):
		return false
	target_path.num_checkpoints += 1
	_update_visuals()
	if scene:
		scene.end_pointer_action(self)
	get_viewport().set_input_as_handled()
	return true

func _try_delete_hovered_checkpoint(hovered : int) -> bool:
	var delete_now := Input.is_key_pressed(KEY_DELETE)
	var just_delete := delete_now and !delete_pressed
	delete_pressed = delete_now
	if !just_delete or hovered < 0:
		return false
	if target_path.num_checkpoints <= 0:
		return false
	target_path.num_checkpoints -= 1
	_update_visuals()
	get_viewport().set_input_as_handled()
	return true

func _process(delta : float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_checkpoint_gizmos() or !is_instance_valid(target_path):
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
	var hovered := _hovered_handle()
	_set_handle_colours(hovered)
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_checkpoint(hovered):
		return
	if hovered == -1 and !scene.pointer_action_busy_for(self) and _try_alt_add_checkpoint(cam):
		return
