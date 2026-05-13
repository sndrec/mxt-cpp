class_name BezierHandle extends Node3D

@export var in_handle_length : float = 1.0
@export var out_handle_length : float = 1.0
@export var time : float = 0.5
@export var cp_scale := Vector3.ONE
var handle_collision : StaticBody3D
var handle_in_collision : StaticBody3D
var handle_out_collision : StaticBody3D
var handle_mesh_instance : MeshInstance3D
var handle_in_mesh_instance : MeshInstance3D
var handle_out_mesh_instance : MeshInstance3D
var handle_line_mesh_instance : MeshInstance3D
var handle_line_mesh := ImmediateMesh.new()
var handle_material := StandardMaterial3D.new()
var handle_in_material := StandardMaterial3D.new()
var handle_out_material := StandardMaterial3D.new()
var handle_line_material := StandardMaterial3D.new()

var clicking := false
var test_plane : Plane = Plane.PLANE_XZ
var origin_point : Vector3 = Vector3.ZERO
var start_length := 0.0
var in_or_out := false
var selectable := true

var road : RoadPathBezier
var associated_index := 0

func _make_sphere_mesh_instance(material : Material) -> MeshInstance3D:
	var mesh_instance := MeshInstance3D.new()
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = 1.0
	sphere_mesh.height = 2.0
	mesh_instance.mesh = sphere_mesh
	mesh_instance.material_override = material
	return mesh_instance

func _configure_material(material : StandardMaterial3D, colour : Color) -> void:
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = colour

func _hide_visuals() -> void:
	if handle_mesh_instance:
		handle_mesh_instance.visible = false
	if handle_in_mesh_instance:
		handle_in_mesh_instance.visible = false
	if handle_out_mesh_instance:
		handle_out_mesh_instance.visible = false
	if handle_line_mesh_instance:
		handle_line_mesh_instance.visible = false

func _update_handle_lines(p1 : Vector3, p2 : Vector3, in_visible : bool, out_visible : bool, in_colour : Color, out_colour : Color) -> void:
	handle_line_mesh.clear_surfaces()
	handle_line_mesh.surface_begin(Mesh.PRIMITIVE_LINES, handle_line_material)
	if in_visible:
		handle_line_mesh.surface_set_color(in_colour)
		handle_line_mesh.surface_add_vertex(global_position)
		handle_line_mesh.surface_add_vertex(p2)
	if out_visible:
		handle_line_mesh.surface_set_color(out_colour)
		handle_line_mesh.surface_add_vertex(global_position)
		handle_line_mesh.surface_add_vertex(p1)
	handle_line_mesh.surface_end()
	handle_line_mesh_instance.visible = in_visible or out_visible

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
	_configure_material(handle_material, Color.WEB_PURPLE)
	_configure_material(handle_in_material, Color.WEB_PURPLE)
	_configure_material(handle_out_material, Color.WEB_PURPLE)
	_configure_material(handle_line_material, Color.WEB_PURPLE)
	handle_line_material.vertex_color_use_as_albedo = true
	handle_mesh_instance = _make_sphere_mesh_instance(handle_material)
	handle_in_mesh_instance = _make_sphere_mesh_instance(handle_in_material)
	handle_out_mesh_instance = _make_sphere_mesh_instance(handle_out_material)
	handle_line_mesh_instance = MeshInstance3D.new()
	handle_line_mesh_instance.mesh = handle_line_mesh
	handle_line_mesh_instance.top_level = true
	add_child(handle_mesh_instance)
	handle_in_collision.add_child(handle_in_mesh_instance)
	handle_out_collision.add_child(handle_out_mesh_instance)
	add_child(handle_line_mesh_instance)

func _process(delta: float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_control_point_gizmos():
		if scene:
			scene.end_pointer_action(self)
		handle_collision.set_collision_layer_value(15, false)
		handle_collision.set_collision_layer_value(16, false)
		handle_in_collision.set_collision_layer_value(16, false)
		handle_out_collision.set_collision_layer_value(16, false)
		clicking = false
		_hide_visuals()
		return
	handle_collision.set_collision_layer_value(15, true)
	if !is_selectable():
		handle_collision.set_collision_layer_value(15, false)
		handle_collision.set_collision_layer_value(16, false)
		handle_in_collision.set_collision_layer_value(16, false)
		handle_out_collision.set_collision_layer_value(16, false)
		_hide_visuals()
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
			if !scene.begin_pointer_action(self):
				return
			get_viewport().set_input_as_handled()
			clicking = true
			var handle_dir := global_basis.orthonormalized().z
			var cam_pos_projected := (cam.global_position - global_position).project(handle_dir)
			var plane_normal := ((cam.global_position - global_position) - cam_pos_projected).normalized()
			test_plane = Plane(plane_normal, global_position)
			origin_point = test_plane.intersects_ray(cam.global_position, cam.project_ray_normal(get_viewport().get_mouse_position()) * 4096)
			origin_point = (origin_point - global_position).project(handle_dir) + global_position
	if Input.is_action_just_released("LeftMouse"):
		if clicking or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		clicking = false
		scene.end_pointer_action(self)
	if clicking:
		var change_point = test_plane.intersects_ray(cam.global_position, cam.project_ray_normal(get_viewport().get_mouse_position()) * 4096)
		if change_point is Vector3:
			var handle_dir := global_basis.orthonormalized().z
			change_point = (change_point - global_position).project(handle_dir) + global_position
			var diff : Vector3 = change_point - origin_point
			if in_or_out:
				out_handle_length = start_length + (diff.dot(handle_dir))
				handle_out_colour = Color.WHITE
			else:
				in_handle_length = start_length - (diff.dot(handle_dir))
				handle_in_colour = Color.WHITE
	if FZGlobal.active_node == self:
		handle_colour = Color.WHITE
	var p1 := global_position + global_basis.orthonormalized().z * out_handle_length
	var p2 := global_position + global_basis.orthonormalized().z * -in_handle_length
	var in_visible := associated_index != 0
	var out_visible := associated_index != road.get_control_point_count() - 1
	if associated_index != 0:
		handle_in_collision.set_collision_mask_value(16, true)
		handle_in_collision.set_collision_layer_value(16, true)
	else:
		handle_in_collision.set_collision_mask_value(16, false)
		handle_in_collision.set_collision_layer_value(16, false)
	if associated_index != road.get_control_point_count() - 1:
		handle_out_collision.set_collision_mask_value(16, true)
		handle_out_collision.set_collision_layer_value(16, true)
	else:
		handle_out_collision.set_collision_mask_value(16, false)
		handle_out_collision.set_collision_layer_value(16, false)
	handle_in_collision.global_position = p2
	handle_out_collision.global_position = p1
	handle_material.albedo_color = handle_colour
	handle_in_material.albedo_color = handle_in_colour
	handle_out_material.albedo_color = handle_out_colour
	handle_mesh_instance.visible = true
	handle_mesh_instance.scale = Vector3.ONE * node_size
	handle_in_mesh_instance.visible = in_visible
	handle_in_mesh_instance.scale = Vector3.ONE * in_handle_size
	handle_out_mesh_instance.visible = out_visible
	handle_out_mesh_instance.scale = Vector3.ONE * out_handle_size
	_update_handle_lines(p1, p2, in_visible, out_visible, handle_in_colour, handle_out_colour)
	
