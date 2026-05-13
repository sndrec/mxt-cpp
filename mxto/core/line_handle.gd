class_name LineHandle extends Node3D

@export var cp_scale := Vector3.ONE
var handle_collision : StaticBody3D

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

func _process(delta: float) -> void:
	if get_selection_priority() == -1:
		return
	scale = Vector3.ONE
	var node_colour := Color.NAVY_BLUE
	var node_size := 2
	var mpc := FZGlobal.editing_scene.mouse_picker_cast
	#print(mpc.is_colliding())
	if mpc.is_colliding() and mpc.get_collider() == handle_collision:
		node_colour = Color.DEEP_SKY_BLUE
	if FZGlobal.active_node == self:
		node_colour = Color.WHITE
		#print("yup")
	#print(FZGlobal.active_node == self)
	DebugDraw3D.scoped_config().set_thickness(0.25)
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	DebugDraw3D.draw_sphere(global_position, node_size, node_colour, delta)
	
