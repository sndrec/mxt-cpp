class_name TrackTriggerGizmo extends Node3D

const TrackTriggerScript := preload("res://core/track_trigger.gd")

enum HandleId {
	MOVE,
	YAW,
	SCALE_X,
	SCALE_Y,
	SCALE_Z,
}

const HANDLE_RADIUS := 3.5
const HANDLE_GAP := 10.0
const YAW_GAP := 18.0

var mouse_cast : RayCast3D
var target_trigger : Node3D
var dragging := false
var drag_handle := -1
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
	for i in HandleId.SCALE_Z + 1:
		handles.append(_make_handle(i))
	set_target_trigger(null)

func set_target_trigger(in_trigger : Node3D) -> void:
	target_trigger = in_trigger if in_trigger and in_trigger.get_script() == TrackTriggerScript else null
	dragging = false
	drag_handle = -1

func _handle_color(handle_id : int) -> Color:
	match handle_id:
		HandleId.MOVE:
			return Color(0.35, 0.8, 1.0, 1.0)
		HandleId.YAW:
			return Color(1.0, 0.88, 0.26, 1.0)
		HandleId.SCALE_X:
			return Color(1.0, 0.28, 0.28, 1.0)
		HandleId.SCALE_Y:
			return Color(0.32, 1.0, 0.32, 1.0)
		HandleId.SCALE_Z:
			return Color(0.4, 0.55, 1.0, 1.0)
	return Color.WHITE

func _make_handle(handle_id : int) -> StaticBody3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = _handle_color(handle_id)
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

func _trigger_extents() -> Vector3:
	return target_trigger.call("trigger_extents")

func _surface_basis() -> Basis:
	return target_trigger.call("surface_basis_at_attachment")

func _trigger_axes() -> Dictionary:
	var trigger_basis : Basis = target_trigger.global_basis.orthonormalized()
	var surface_basis : Basis = _surface_basis()
	return {
		"right": trigger_basis.x.normalized(),
		"up": (-surface_basis.y).normalized(),
		"forward": trigger_basis.z.normalized(),
	}

func _handle_position(handle_id : int) -> Vector3:
	var origin : Vector3 = target_trigger.global_position
	var axes := _trigger_axes()
	var extents := _trigger_extents()
	var trigger_scale : Vector3 = target_trigger.get("trigger_scale")
	match handle_id:
		HandleId.MOVE:
			return origin
		HandleId.YAW:
			return origin + axes["forward"] * (extents.z * trigger_scale.z + YAW_GAP)
		HandleId.SCALE_X:
			return origin + axes["right"] * (extents.x * trigger_scale.x + HANDLE_GAP)
		HandleId.SCALE_Y:
			return origin + axes["up"] * (extents.y * trigger_scale.y + HANDLE_GAP)
		HandleId.SCALE_Z:
			return origin + axes["forward"] * (extents.z * trigger_scale.z + HANDLE_GAP)
	return origin

func _update_visuals() -> void:
	for i in handles.size():
		handles[i].global_position = _handle_position(i)
	var origin : Vector3 = target_trigger.global_position
	outline_mesh.clear_surfaces()
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	for i in handles.size():
		outline_mesh.surface_set_color(_handle_color(i))
		outline_mesh.surface_add_vertex(origin)
		outline_mesh.surface_add_vertex(handles[i].global_position)
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
		if dragging and i == drag_handle:
			handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered:
			handle_materials[i].albedo_color = Color(1.0, 0.96, 0.62, 1.0)
		else:
			handle_materials[i].albedo_color = _handle_color(i)

func _scale_drag_axis(handle_id : int) -> Vector3:
	var axes := _trigger_axes()
	match handle_id:
		HandleId.SCALE_X:
			return axes["right"]
		HandleId.SCALE_Y:
			return axes["up"]
		HandleId.SCALE_Z:
			return axes["forward"]
	return Vector3.ZERO

func _write_yaw(world_pos : Vector3) -> void:
	var surface_basis : Basis = _surface_basis()
	var local : Vector3 = surface_basis.inverse() * (world_pos - target_trigger.global_position)
	if Vector2(local.x, local.z).length_squared() <= 0.0001:
		return
	target_trigger.set("add_yaw_degrees", rad_to_deg(atan2(local.x, local.z)))
	target_trigger.call("refresh_from_attachment")

func _write_scale(handle_id : int, world_pos : Vector3) -> void:
	var axis := _scale_drag_axis(handle_id)
	if axis.is_zero_approx():
		return
	var extents := _trigger_extents()
	var distance := maxf(0.001, (world_pos - target_trigger.global_position).dot(axis) - HANDLE_GAP)
	var trigger_scale : Vector3 = target_trigger.get("trigger_scale")
	match handle_id:
		HandleId.SCALE_X:
			trigger_scale.x = maxf(0.001, distance / maxf(extents.x, 0.001))
		HandleId.SCALE_Y:
			trigger_scale.y = maxf(0.001, distance / maxf(extents.y, 0.001))
		HandleId.SCALE_Z:
			trigger_scale.z = maxf(0.001, distance / maxf(extents.z, 0.001))
	target_trigger.set("trigger_scale", trigger_scale)
	target_trigger.call("refresh_from_attachment")

func _write_dragged_handle(handle_id : int, world_pos : Vector3) -> void:
	match handle_id:
		HandleId.MOVE:
			target_trigger.call("attach_to_nearest_surface", world_pos)
		HandleId.YAW:
			_write_yaw(world_pos)
		_:
			_write_scale(handle_id, world_pos)

func _drag_plane_for(handle_id : int, cam : Camera3D) -> Plane:
	if handle_id == HandleId.MOVE:
		return Plane(-cam.global_basis.z.normalized(), target_trigger.global_position)
	if handle_id == HandleId.YAW:
		var surface_basis := _surface_basis()
		return Plane((-surface_basis.y).normalized(), target_trigger.global_position)
	var axis := _scale_drag_axis(handle_id)
	var normal := axis.cross(cam.global_basis.z).cross(axis).normalized()
	if normal.is_zero_approx():
		normal = -cam.global_basis.z.normalized()
	return Plane(normal, handles[handle_id].global_position)

func _process(_delta : float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or scene.tool_mode != TrackEditingScene.ToolMode.EDIT_SEGMENT or !is_instance_valid(target_trigger):
		if scene:
			scene.end_pointer_action(self)
		visible = false
		outline_mesh_instance.visible = false
		_set_colliders_enabled(false)
		dragging = false
		drag_handle = -1
		return
	visible = true
	_set_colliders_enabled(true)
	_update_visuals()
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var hovered := _hovered_handle()
	_set_handle_colours(hovered)
	if Input.is_action_just_pressed("LeftMouse") and hovered != -1:
		if !scene.begin_pointer_action(self):
			return
		dragging = true
		drag_handle = hovered
		drag_plane = _drag_plane_for(hovered, cam)
		get_viewport().set_input_as_handled()
	if Input.is_action_just_released("LeftMouse"):
		if dragging or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		dragging = false
		drag_handle = -1
		scene.end_pointer_action(self)
	if dragging and drag_handle != -1:
		var ray_dir := cam.project_ray_normal(get_viewport().get_mouse_position())
		var hit = drag_plane.intersects_ray(cam.global_position, ray_dir)
		if hit is Vector3:
			_write_dragged_handle(drag_handle, hit)
			_update_visuals()
