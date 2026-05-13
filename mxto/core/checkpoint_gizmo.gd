class_name CheckpointGizmo extends Node3D

const HANDLE_RADIUS := 4.0
const PREVIEW_NORMAL_LENGTH := 20.0
const PREVIEW_STEPS_MAX := 128
const ADD_POINT_MAX_SCREEN_DISTANCE := 60.0
const LINK_TARGET_MAX_SCREEN_DISTANCE := 80.0

enum LinkHandleId {
	PREVIOUS,
	NEXT,
}

var mouse_cast : RayCast3D
var target_path : RoadPath
var dragging := false
var dragging_link := false
var drag_link_handle := -1
var drag_preview_point := Vector3.ZERO
var drag_candidate_path : RoadPath
var delete_pressed := false
var outline_mesh_instance : MeshInstance3D
var outline_mesh := ImmediateMesh.new()
var outline_material := StandardMaterial3D.new()
var handle_materials : Array[StandardMaterial3D] = []
var handles : Array[StaticBody3D] = []
var link_handle_materials : Array[StandardMaterial3D] = []
var link_handles : Array[StaticBody3D] = []

func _ready() -> void:
	outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	outline_material.vertex_color_use_as_albedo = true
	outline_mesh_instance = MeshInstance3D.new()
	outline_mesh_instance.mesh = outline_mesh
	outline_mesh_instance.top_level = true
	add_child(outline_mesh_instance)
	for i in LinkHandleId.NEXT + 1:
		link_handles.append(_make_link_handle(i))
	set_target_path(null)

func set_target_path(in_path : RoadPath) -> void:
	target_path = in_path
	dragging = false
	dragging_link = false
	drag_link_handle = -1
	drag_candidate_path = null

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

func _link_handle_color(handle_id : int) -> Color:
	if handle_id == LinkHandleId.PREVIOUS:
		return Color(0.92, 0.46, 1.0, 1.0)
	return Color(0.42, 1.0, 0.48, 1.0)

func _make_link_handle(handle_id : int) -> StaticBody3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = _link_handle_color(handle_id)
	link_handle_materials.append(material)
	var body := StaticBody3D.new()
	body.set_collision_layer_value(16, true)
	body.set_collision_mask_value(16, true)
	body.set_collision_layer_value(1, false)
	body.set_collision_mask_value(1, false)
	var collision := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = HANDLE_RADIUS * 1.25
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
	for handle in link_handles:
		handle.set_collision_layer_value(16, enabled)

func _handle_ty(handle_id : int) -> float:
	return float(handle_id + 1) / float(_checkpoint_span_count())

func _link_handle_ty(handle_id : int) -> float:
	return 0.0 if handle_id == LinkHandleId.PREVIOUS else 1.0

func _candidate_link_ty(handle_id : int) -> float:
	return 1.0 if handle_id == LinkHandleId.PREVIOUS else 0.0

func _endpoint_position(path : RoadPath, ty : float) -> Vector3:
	var points := path.get_surface_positions(PackedVector2Array([Vector2(0.0, clampf(ty, 0.0, 1.0))]))
	if points.is_empty():
		return path.get_root_transform(clampf(ty, 0.0, 1.0)).origin
	return points[0]

func _track_root() -> TrackRoot:
	return FZGlobal.current_track as TrackRoot

func _segment_path(segment : RoadPath) -> NodePath:
	var root := _track_root()
	if !root or !segment:
		return NodePath()
	return root.get_path_to(segment)

func _segment_for_path(segment_path : NodePath) -> RoadPath:
	var root := _track_root()
	if !root or segment_path.is_empty():
		return null
	return root.get_node_or_null(segment_path) as RoadPath

func _append_unique_path(paths : Array[NodePath], segment_path : NodePath) -> void:
	if segment_path.is_empty() or paths.has(segment_path):
		return
	paths.append(segment_path)

func _paths_without(paths : Array[NodePath], segment_path : NodePath) -> Array[NodePath]:
	var out : Array[NodePath] = []
	for path in paths:
		if path != segment_path:
			out.append(path)
	return out

func _clear_link_side(handle_id : int) -> void:
	if !is_instance_valid(target_path):
		return
	var self_path := _segment_path(target_path)
	var old_paths := target_path.previous_segment_paths.duplicate() if handle_id == LinkHandleId.PREVIOUS else target_path.next_segment_paths.duplicate()
	if handle_id == LinkHandleId.PREVIOUS:
		target_path.previous_segment_paths.clear()
	else:
		target_path.next_segment_paths.clear()
	for old_path in old_paths:
		var old_segment := _segment_for_path(old_path)
		if !old_segment:
			continue
		if handle_id == LinkHandleId.PREVIOUS:
			old_segment.next_segment_paths = _paths_without(old_segment.next_segment_paths, self_path)
		else:
			old_segment.previous_segment_paths = _paths_without(old_segment.previous_segment_paths, self_path)

func _connect_link_side(handle_id : int, segment : RoadPath, append_link : bool) -> void:
	if !is_instance_valid(target_path) or !is_instance_valid(segment) or segment == target_path:
		return
	if !append_link:
		_clear_link_side(handle_id)
	var self_path := _segment_path(target_path)
	var segment_path := _segment_path(segment)
	if handle_id == LinkHandleId.PREVIOUS:
		_append_unique_path(target_path.previous_segment_paths, segment_path)
		_append_unique_path(segment.next_segment_paths, self_path)
	else:
		_append_unique_path(target_path.next_segment_paths, segment_path)
		_append_unique_path(segment.previous_segment_paths, self_path)

func _closest_link_candidate(cam : Camera3D, handle_id : int) -> Dictionary:
	var root := _track_root()
	if !root:
		return {}
	var mouse_pos := get_viewport().get_mouse_position()
	var best_path : RoadPath
	var best_pos := Vector3.ZERO
	var best_dist := INF
	for segment in root.get_road_segments():
		if segment == target_path:
			continue
		var pos := _endpoint_position(segment, _candidate_link_ty(handle_id))
		if cam.is_position_behind(pos):
			continue
		var screen := cam.unproject_position(pos)
		var dist := mouse_pos.distance_squared_to(screen)
		if dist < best_dist:
			best_dist = dist
			best_path = segment
			best_pos = pos
	if best_dist > LINK_TARGET_MAX_SCREEN_DISTANCE * LINK_TARGET_MAX_SCREEN_DISTANCE:
		return {}
	return {
		"path": best_path,
		"position": best_pos,
		"distance": best_dist,
	}

func _update_visuals() -> void:
	_sync_handles()
	outline_mesh.clear_surfaces()
	if !is_instance_valid(target_path):
		outline_mesh_instance.visible = false
		return
	for i in handles.size():
		handles[i].global_position = _surface_point(0.0, _handle_ty(i))
	for i in link_handles.size():
		link_handles[i].global_position = _surface_point(0.0, _link_handle_ty(i))
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
	var previous_origin := _surface_point(0.0, 0.0)
	var next_origin := _surface_point(0.0, 1.0)
	outline_mesh.surface_set_color(_link_handle_color(LinkHandleId.PREVIOUS))
	for previous_path in target_path.previous_segment_paths:
		var previous_segment := _segment_for_path(previous_path)
		if previous_segment:
			outline_mesh.surface_add_vertex(previous_origin)
			outline_mesh.surface_add_vertex(_endpoint_position(previous_segment, 1.0))
	outline_mesh.surface_set_color(_link_handle_color(LinkHandleId.NEXT))
	for next_path in target_path.next_segment_paths:
		var next_segment := _segment_for_path(next_path)
		if next_segment:
			outline_mesh.surface_add_vertex(next_origin)
			outline_mesh.surface_add_vertex(_endpoint_position(next_segment, 0.0))
	if dragging_link and drag_link_handle != -1:
		outline_mesh.surface_set_color(Color.WHITE)
		outline_mesh.surface_add_vertex(_surface_point(0.0, _link_handle_ty(drag_link_handle)))
		outline_mesh.surface_add_vertex(drag_preview_point)
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

func _hovered_link_handle() -> int:
	if !mouse_cast or !mouse_cast.is_colliding():
		return -1
	var collider := mouse_cast.get_collider()
	for i in link_handles.size():
		if collider == link_handles[i]:
			return i
	return -1

func _set_handle_colours(hovered : int, hovered_link : int) -> void:
	for i in handle_materials.size():
		if i == hovered:
			handle_materials[i].albedo_color = _hover_color()
		else:
			handle_materials[i].albedo_color = _handle_color()
	for i in link_handle_materials.size():
		if dragging_link and i == drag_link_handle:
			link_handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered_link:
			link_handle_materials[i].albedo_color = _hover_color()
		else:
			link_handle_materials[i].albedo_color = _link_handle_color(i)

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

func _try_delete_hovered_link(hovered_link : int) -> bool:
	if hovered_link < 0:
		return false
	var delete_now := Input.is_key_pressed(KEY_DELETE)
	var just_delete := delete_now and !delete_pressed
	delete_pressed = delete_now
	if !just_delete:
		return false
	_clear_link_side(hovered_link)
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
		dragging_link = false
		drag_link_handle = -1
		drag_candidate_path = null
		delete_pressed = Input.is_key_pressed(KEY_DELETE)
		return
	visible = true
	_update_visuals()
	_set_colliders_enabled(true)
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var hovered_link := _hovered_link_handle()
	var hovered := -1 if hovered_link != -1 else _hovered_handle()
	_set_handle_colours(hovered, hovered_link)
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_link(hovered_link):
		return
	if !scene.pointer_action_busy_for(self) and _try_delete_hovered_checkpoint(hovered):
		return
	if hovered_link == -1 and hovered == -1 and !scene.pointer_action_busy_for(self) and _try_alt_add_checkpoint(cam):
		return
	if Input.is_action_just_pressed("LeftMouse") and hovered_link != -1:
		if !scene.begin_pointer_action(self):
			return
		dragging_link = true
		drag_link_handle = hovered_link
		drag_candidate_path = null
		drag_preview_point = link_handles[hovered_link].global_position
		get_viewport().set_input_as_handled()
	if dragging_link and drag_link_handle != -1:
		var candidate := _closest_link_candidate(cam, drag_link_handle)
		if candidate.is_empty():
			drag_candidate_path = null
			var drag_plane := Plane(-cam.global_basis.z.normalized(), _surface_point(0.0, _link_handle_ty(drag_link_handle)))
			var hit = drag_plane.intersects_ray(cam.global_position, cam.project_ray_normal(get_viewport().get_mouse_position()))
			drag_preview_point = hit if hit is Vector3 else link_handles[drag_link_handle].global_position
		else:
			drag_candidate_path = candidate["path"]
			drag_preview_point = candidate["position"]
		_update_visuals()
	if Input.is_action_just_released("LeftMouse"):
		if dragging_link or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		if dragging_link and drag_link_handle != -1 and is_instance_valid(drag_candidate_path):
			_connect_link_side(drag_link_handle, drag_candidate_path, Input.is_action_pressed("Shift"))
			_update_visuals()
		dragging_link = false
		drag_link_handle = -1
		drag_candidate_path = null
		scene.end_pointer_action(self)
