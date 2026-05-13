class_name LineHandle extends Node3D

@export var cp_scale := Vector3.ONE
var handle_collision : StaticBody3D
var handle_mesh_instance : MeshInstance3D
var handle_material := StandardMaterial3D.new()

# -1 = can't select
# 0 is highest priority
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

func _ready() -> void:
	handle_collision = StaticBody3D.new()
	add_child(handle_collision)
	var handle_collision_shape := CollisionShape3D.new()
	handle_collision.add_child(handle_collision_shape)
	var handle_collision_shape_sphere := SphereShape3D.new()
	handle_collision_shape_sphere.radius = 3
	handle_collision_shape.shape = handle_collision_shape_sphere
	handle_collision.set_collision_mask_value(15, true)
	handle_collision.set_collision_layer_value(15, true)
	handle_collision.set_collision_mask_value(1, false)
	handle_collision.set_collision_layer_value(1, false)
	handle_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	handle_material.albedo_color = Color.NAVY_BLUE
	handle_mesh_instance = MeshInstance3D.new()
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = 1.0
	sphere_mesh.height = 2.0
	handle_mesh_instance.mesh = sphere_mesh
	handle_mesh_instance.material_override = handle_material
	add_child(handle_mesh_instance)

func _process(delta: float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_control_point_gizmos():
		handle_collision.set_collision_layer_value(15, false)
		handle_mesh_instance.visible = false
		return
	handle_collision.set_collision_layer_value(15, true)
	if get_selection_priority() == -1:
		handle_mesh_instance.visible = false
		return
	scale = Vector3.ONE
	var node_colour := Color.NAVY_BLUE
	var node_size := 2
	var mpc := FZGlobal.editing_scene.mouse_picker_cast
	if mpc.is_colliding() and mpc.get_collider() == handle_collision:
		node_colour = Color.DEEP_SKY_BLUE
	if FZGlobal.active_node == self:
		node_colour = Color.WHITE
	handle_material.albedo_color = node_colour
	handle_mesh_instance.visible = true
	handle_mesh_instance.scale = Vector3.ONE * node_size
	
