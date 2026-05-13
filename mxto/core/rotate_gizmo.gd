class_name RotateGizmo extends Node3D

@onready var giz_y_col : StaticBody3D = $GIZ_Y_COL
@onready var giz_z_col : StaticBody3D = $GIZ_Z_COL
@onready var giz_x_col : StaticBody3D = $GIZ_X_COL
var mouse_cast : RayCast3D

var is_moving := false
var active := true
var use_axis := "x"
var use_vector := Vector3.UP
var original_transform := Transform3D.IDENTITY
var origin_point = Vector3.ZERO
var test_plane := Plane(Vector3.UP)
var gizmo_material := preload("res://asset/mat/rotate_gizmo_material.tres")
var target_node : Node3D

func set_colliders_active(enabled : bool) -> void:
	giz_x_col.set_collision_layer_value(16, enabled)
	giz_y_col.set_collision_layer_value(16, enabled)
	giz_z_col.set_collision_layer_value(16, enabled)

func set_target_node(in_node : Node3D) -> void:
	if is_instance_valid(in_node):
		global_position = in_node.global_position
		global_basis = in_node.global_basis.orthonormalized()
		original_transform = in_node.global_transform
		scale = Vector3(50, 50, 50)
		target_node = in_node
		is_moving = false
	else:
		scale = Vector3(50, 50, 50)
		target_node = null
		is_moving = false
		active = false
		visible = false
		set_colliders_active(false)

func mousecast_wants_this_gizmo() -> bool:
	return (mouse_cast.get_collider() == giz_x_col or mouse_cast.get_collider() == giz_y_col or mouse_cast.get_collider() == giz_z_col)

func _process(delta: float) -> void:
	var scene := FZGlobal.editing_scene
	if !target_node or !is_instance_valid(target_node) or !scene or !scene.tool_mode_allows_transform_gizmos():
		if scene:
			scene.end_pointer_action(self)
		is_moving = false
		active = false
		visible = false
		set_colliders_active(false)
		return
	else:
		active = true
	var cam := FZGlobal.current_cam
	var dir := cam.project_ray_normal(get_viewport().get_mouse_position())
	global_transform = target_node.global_transform
	scale = Vector3.ONE * global_position.distance_to(cam.global_position) * 0.25
	visible = active
	if !active:
		set_colliders_active(false)
		return
	else:
		set_colliders_active(true)
	if Input.is_action_just_released("LeftMouse") and is_moving and active:
		get_viewport().set_input_as_handled()
		FZGlobal.transform_object(target_node, global_transform, original_transform)
		scene.end_pointer_action(self)
	if !Input.is_action_pressed("LeftMouse"):
		is_moving = false
		target_node.global_basis = global_basis.orthonormalized()
		scene.end_pointer_action(self)
	if !is_moving:
		mouse_cast.global_position = cam.global_position
		mouse_cast.target_position = mouse_cast.to_local(cam.global_position + dir * 4096)
		mouse_cast.force_raycast_update()
		if Input.is_action_just_pressed("LeftMouse") and mousecast_wants_this_gizmo():
			if !scene.begin_pointer_action(self):
				return
			get_viewport().set_input_as_handled()
			gizmo_material.set_shader_parameter("x_moused", false)
			gizmo_material.set_shader_parameter("y_moused", false)
			gizmo_material.set_shader_parameter("z_moused", false)
			if mouse_cast.is_colliding():
				if mouse_cast.get_collider() == giz_x_col:
					use_axis = "x"
					use_vector = global_basis.x
				elif mouse_cast.get_collider() == giz_y_col:
					use_axis = "y"
					use_vector = global_basis.y
				elif mouse_cast.get_collider() == giz_z_col:
					use_axis = "z"
					use_vector = global_basis.z
				test_plane = Plane(use_vector, global_position)
				origin_point = test_plane.intersects_ray(cam.global_position, dir)
				if origin_point is Vector3:
					gizmo_material.set_shader_parameter("clicking", true)
					is_moving = true
					original_transform = global_transform
					if mouse_cast.get_collider() == giz_x_col:
						gizmo_material.set_shader_parameter("x_moused", true)
						use_axis = "x"
						use_vector = global_basis.x.normalized()
					elif mouse_cast.get_collider() == giz_y_col:
						gizmo_material.set_shader_parameter("y_moused", true)
						use_axis = "y"
						use_vector = global_basis.y.normalized()
					elif mouse_cast.get_collider() == giz_z_col:
						gizmo_material.set_shader_parameter("z_moused", true)
						use_axis = "z"
						use_vector = global_basis.z.normalized()
		else:
			gizmo_material.set_shader_parameter("clicking", false)
			gizmo_material.set_shader_parameter("x_moused", false)
			gizmo_material.set_shader_parameter("y_moused", false)
			gizmo_material.set_shader_parameter("z_moused", false)
			if mouse_cast.is_colliding():
				if mouse_cast.get_collider() == giz_x_col:
					gizmo_material.set_shader_parameter("x_moused", true)
				elif mouse_cast.get_collider() == giz_y_col:
					gizmo_material.set_shader_parameter("y_moused", true)
				elif mouse_cast.get_collider() == giz_z_col:
					gizmo_material.set_shader_parameter("z_moused", true)
	else:
		gizmo_material.set_shader_parameter("clicking", true)
		gizmo_material.set_shader_parameter(use_axis + "_moused", true)
		var intersect_pos = test_plane.intersects_ray(cam.global_position, dir)
		if intersect_pos is Vector3 and origin_point is Vector3:
			var rot_plane := Plane(use_vector, global_position)
			var d1 := (rot_plane.project(origin_point) - global_position).normalized() * 50
			var d2 := (rot_plane.project(intersect_pos) - global_position).normalized() * 50
			var angle := d1.signed_angle_to(d2, use_vector)
			var desire_rotation := original_transform.basis.rotated(use_vector, angle)
			if Input.is_action_pressed("Ctrl"):
				desire_rotation = original_transform.basis.rotated(use_vector, snappedf(angle, PI * 0.125))
			global_transform.basis = desire_rotation
			target_node.global_transform.basis = desire_rotation.orthonormalized()
