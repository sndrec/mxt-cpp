class_name BezierHandle extends Node3D

@export var in_handle_length : float = 1.0
@export var out_handle_length : float = 1.0
@export var time : float = 0.5
@export var cp_scale := Vector3.ONE
var handle_collision : StaticBody3D
var handle_in_collision : StaticBody3D
var handle_out_collision : StaticBody3D

var clicking := false
var test_plane : Plane = Plane.PLANE_XZ
var origin_point : Vector3 = Vector3.ZERO
var start_length := 0.0
var in_or_out := false
var selectable := true

var road : RoadPathBezier
var associated_index := 0

func get_selection_priority() -> int:
	if FZGlobal.active_node == get_parent():
		return 0
	for node in get_parent().get_children():
		if FZGlobal.active_node == node:
			if FZGlobal.active_node == self:
				return 4
			else:
				return 0
	return -1

func is_selectable() -> bool:
	#if FZGlobal.active_node == self:
		#return false
	if FZGlobal.active_node == get_parent():
		return true
	for node in get_parent().get_children():
		if FZGlobal.active_node == node:
			return true
	return false

func _ready() -> void:
	road = get_parent()
	handle_collision = StaticBody3D.new()
	handle_in_collision = StaticBody3D.new()
	handle_out_collision = StaticBody3D.new()
	add_child(handle_collision)
	add_child(handle_in_collision)
	add_child(handle_out_collision)
	var handle_collision_shape := CollisionShape3D.new()
	var handle_collision_shape_2 := CollisionShape3D.new()
	var handle_collision_shape_3 := CollisionShape3D.new()
	handle_collision.add_child(handle_collision_shape)
	handle_in_collision.add_child(handle_collision_shape_2)
	handle_out_collision.add_child(handle_collision_shape_3)
	var handle_collision_shape_sphere := SphereShape3D.new()
	handle_collision_shape_sphere.radius = 3
	handle_collision_shape.shape = handle_collision_shape_sphere
	handle_collision_shape_2.shape = handle_collision_shape_sphere
	handle_collision_shape_3.shape = handle_collision_shape_sphere
	handle_collision.set_collision_mask_value(16, true)
	handle_collision.set_collision_layer_value(16, true)
	handle_collision.set_collision_mask_value(15, true)
	handle_collision.set_collision_layer_value(15, true)
	handle_collision.set_collision_mask_value(1, false)
	handle_collision.set_collision_layer_value(1, false)
	handle_in_collision.set_collision_mask_value(16, true)
	handle_in_collision.set_collision_layer_value(16, true)
	handle_in_collision.set_collision_mask_value(1, false)
	handle_in_collision.set_collision_layer_value(1, false)
	handle_out_collision.set_collision_mask_value(16, true)
	handle_out_collision.set_collision_layer_value(16, true)
	handle_out_collision.set_collision_mask_value(1, false)
	handle_out_collision.set_collision_layer_value(1, false)

func _process(delta: float) -> void:
	if !is_selectable():
		return
	var handle_in_colour := Color.WEB_PURPLE
	var handle_out_colour := Color.WEB_PURPLE
	var node_size := 2.5
	var in_handle_size := 1.5
	var out_handle_size := 1.5
	var handle_colour := Color.WEB_PURPLE
	var mpc := FZGlobal.editing_scene.mouse_picker_cast
	var mgc := FZGlobal.editing_scene.mouse_gizmo_cast
	var cam := FZGlobal.editing_scene.edit_cam
	if !(FZGlobal.active_node == get_parent() or FZGlobal.active_node == self):
		for node in get_parent().get_children():
			if FZGlobal.active_node == node:
				handle_in_colour = Color.NAVY_BLUE
				handle_out_colour = Color.NAVY_BLUE
				handle_colour = Color.NAVY_BLUE
				if mpc.is_colliding() and !clicking:
					match mpc.get_collider():
						handle_collision:
							handle_colour = Color.DEEP_SKY_BLUE
				DebugDraw3D.draw_sphere(global_position, node_size, handle_colour, delta)
				break
	scale = Vector3.ONE
	if mgc.is_colliding() and !clicking and FZGlobal.active_node == self:
		match mgc.get_collider():
			handle_collision:
				handle_colour = Color.PALE_VIOLET_RED
			handle_in_collision:
				handle_in_colour = Color.PALE_VIOLET_RED
				in_or_out = false
				start_length = in_handle_length
				in_handle_size = 3.0
			handle_out_collision:
				handle_out_colour = Color.PALE_VIOLET_RED
				in_or_out = true
				start_length = out_handle_length
				out_handle_size = 3.0
		if FZGlobal.active_node == self and Input.is_action_just_pressed("LeftMouse") and (mgc.get_collider() == handle_in_collision or mgc.get_collider() == handle_out_collision):
			clicking = true
			var handle_dir := global_basis.orthonormalized().z
			var cam_pos_projected := (cam.global_position - global_position).project(handle_dir)
			var plane_normal := ((cam.global_position - global_position) - cam_pos_projected).normalized()
			test_plane = Plane(plane_normal, global_position)
			origin_point = test_plane.intersects_ray(cam.global_position, cam.project_ray_normal(get_viewport().get_mouse_position()) * 4096)
			origin_point = (origin_point - global_position).project(handle_dir) + global_position
	if Input.is_action_just_released("LeftMouse"):
		clicking = false
	if clicking:
		var change_point = test_plane.intersects_ray(cam.global_position, cam.project_ray_normal(get_viewport().get_mouse_position()) * 4096)
		if change_point is Vector3:
			var handle_dir := global_basis.orthonormalized().z
			change_point = (change_point - global_position).project(handle_dir) + global_position
			var diff : Vector3 = change_point - origin_point
			DebugDraw3D.draw_sphere(origin_point, 0.5, Color.RED, delta)
			DebugDraw3D.draw_sphere(change_point, 0.5, Color.RED, delta)
			if in_or_out:
				out_handle_length = start_length + (diff.dot(handle_dir))
				handle_out_colour = Color.WHITE
			else:
				in_handle_length = start_length - (diff.dot(handle_dir))
				handle_in_colour = Color.WHITE
	DebugDraw3D.scoped_config().set_thickness(1)
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if FZGlobal.active_node == self:
		handle_colour = Color.WHITE
	DebugDraw3D.draw_sphere(global_position, node_size, handle_colour, delta)
	var p1 := global_position + global_basis.orthonormalized().z * out_handle_length
	var p2 := global_position + global_basis.orthonormalized().z * -in_handle_length
	if associated_index != 0:
		DebugDraw3D.draw_sphere(p2, in_handle_size, handle_in_colour, delta)
		DebugDraw3D.draw_line(global_position, p2, handle_in_colour, delta)
		handle_in_collision.set_collision_mask_value(16, true)
		handle_in_collision.set_collision_layer_value(16, true)
	else:
		handle_in_collision.set_collision_mask_value(16, false)
		handle_in_collision.set_collision_layer_value(16, false)
	if associated_index != road.get_control_point_count() - 1:
		DebugDraw3D.draw_sphere(p1, out_handle_size, handle_out_colour, delta)
		DebugDraw3D.draw_line(global_position, p1, handle_out_colour, delta)
		handle_out_collision.set_collision_mask_value(16, true)
		handle_out_collision.set_collision_layer_value(16, true)
	else:
		handle_out_collision.set_collision_mask_value(16, false)
		handle_out_collision.set_collision_layer_value(16, false)
	handle_in_collision.global_position = p2
	handle_out_collision.global_position = p1
	
