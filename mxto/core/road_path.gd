class_name RoadPath extends Node3D

@export var road_shape : RoadShape = RoadShape.new()
@export var road_style : Mesh
@export var road_uv_multiplier := 1.0
@export var horizontal_road_mesh_segments : PackedFloat32Array
@export var num_checkpoints := 8
var segment_length := 0.0
var road_mesh_instance : MeshInstance3D
var road_collision : StaticBody3D
var road_collision_shape : CollisionShape3D
var road_collision_shape_mesh : ConcavePolygonShape3D
@export var mxt_segment_index : int = -1
@export var mxt_road_type : int = 0
@export var left_rail_height : float = 5.0
@export var right_rail_height : float = 5.0
@export var left_rail_start : float = 0.0
@export var left_rail_end : float = 1.0
@export var right_rail_start : float = 0.0
@export var right_rail_end : float = 1.0

var last_gen_time := 0

func get_selection_priority() -> int:
	if FZGlobal.active_node == self:
		return 5
	for node in get_children():
		if FZGlobal.active_node == node:
			return 4
	return 3

func create_road_mesh_instance() -> void:
	if !is_instance_valid(road_mesh_instance):
		road_mesh_instance = get_node_or_null("RoadMeshInstance") as MeshInstance3D
	if !is_instance_valid(road_mesh_instance):
		road_mesh_instance = MeshInstance3D.new()
		road_mesh_instance.name = "RoadMeshInstance"
		add_child(road_mesh_instance)

	if !is_instance_valid(road_collision):
		road_collision = get_node_or_null("RoadCollision") as StaticBody3D
	if !is_instance_valid(road_collision):
		road_collision = StaticBody3D.new()
		road_collision.name = "RoadCollision"
		add_child(road_collision)

	if !is_instance_valid(road_collision_shape):
		road_collision_shape = road_collision.get_node_or_null("RoadCollisionShape") as CollisionShape3D
	if !is_instance_valid(road_collision_shape):
		road_collision_shape = CollisionShape3D.new()
		road_collision_shape.name = "RoadCollisionShape"
		road_collision.add_child(road_collision_shape)

	road_collision_shape_mesh = road_collision_shape.shape as ConcavePolygonShape3D
	if !road_collision_shape_mesh:
		road_collision_shape_mesh = ConcavePolygonShape3D.new()
		road_collision_shape.shape = road_collision_shape_mesh
	road_collision_shape_mesh.backface_collision = true
	road_collision.set_collision_mask_value(15, true)
	road_collision.set_collision_layer_value(15, true)
	road_collision.set_collision_mask_value(1, false)
	road_collision.set_collision_layer_value(1, false)

func _ready() -> void:
	_try_generate_mesh()

func is_selectable() -> bool:
	return true

func get_root_transform(in_t : float) -> Transform3D:
	return Transform3D.IDENTITY

func get_surface_position(in_t : Vector2) -> Vector3:
	return Vector3.ZERO

func get_surface_positions(in_points : PackedVector2Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(in_points.size())
	return out

func get_surface_local_positions(in_points : PackedVector2Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(in_points.size())
	return out

func _get_surface(in_t : Vector2) -> Basis:
	return Basis(get_surface_position(in_t), Vector3.UP, Vector3.ZERO)

func _try_generate_mesh() -> void:
	pass

func _process(delta : float) -> void:
	if road_mesh_instance:
		road_mesh_instance.global_transform = Transform3D.IDENTITY
	if road_collision:
		road_collision.global_transform = Transform3D.IDENTITY
