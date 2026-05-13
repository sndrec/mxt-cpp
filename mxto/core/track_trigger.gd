class_name TrackTrigger extends Node3D

enum TriggerType {
	DASHPLATE,
	JUMPPLATE,
	MINE,
}

const TRIGGER_SCENES := [
	preload("res://asset/obj_dashplate.tscn"),
	preload("res://asset/obj_jumpplate.tscn"),
	preload("res://asset/obj_mine.tscn"),
]

const TRIGGER_LABELS := [
	"Dashplate",
	"Jumpplate",
	"Mine",
]

const TRIGGER_EXTENTS := [
	Vector3(6.0, 4.0, 12.0),
	Vector3(12.0, 4.0, 2.0),
	Vector3(2.0, 3.0, 2.0),
]

@export var trigger_type : TriggerType = TriggerType.DASHPLATE:
	set(new_type):
		trigger_type = new_type
		_refresh_preview()

@export var segment_path : NodePath
@export var surface_t := Vector2(0.0, 0.5)
@export var add_yaw_degrees := 0.0
@export var trigger_scale := Vector3.ONE
@export var mxt_segment_index : int = -1
@export var mxt_checkpoint_index : int = -1

var preview_instance : Node3D
var trigger_collision : StaticBody3D
var trigger_collision_shape : CollisionShape3D
var last_synced_transform := Transform3D.IDENTITY
var has_synced_transform := false

func get_selection_priority() -> int:
	if FZGlobal.active_node == self:
		return 1
	return 2

func is_selectable() -> bool:
	return true

func trigger_label() -> String:
	return TRIGGER_LABELS[int(trigger_type)]

func trigger_extents() -> Vector3:
	return TRIGGER_EXTENTS[int(trigger_type)]

func set_target_segment(segment : RoadPath) -> void:
	if !segment:
		segment_path = NodePath()
		return
	segment_path = get_path_to(segment)

func get_target_segment() -> RoadPath:
	if segment_path.is_empty():
		return null
	return get_node_or_null(segment_path) as RoadPath

func _ready() -> void:
	_ensure_collision()
	_refresh_preview()

func _ensure_collision() -> void:
	if !trigger_collision:
		trigger_collision = get_node_or_null("TriggerCollision") as StaticBody3D
	if !trigger_collision:
		trigger_collision = StaticBody3D.new()
		trigger_collision.name = "TriggerCollision"
		add_child(trigger_collision)
	if !trigger_collision_shape:
		trigger_collision_shape = trigger_collision.get_node_or_null("TriggerCollisionShape") as CollisionShape3D
	if !trigger_collision_shape:
		trigger_collision_shape = CollisionShape3D.new()
		trigger_collision_shape.name = "TriggerCollisionShape"
		trigger_collision.add_child(trigger_collision_shape)
	var box_shape := trigger_collision_shape.shape as BoxShape3D
	if !box_shape:
		box_shape = BoxShape3D.new()
		trigger_collision_shape.shape = box_shape
	box_shape.size = trigger_extents() * 2.0
	trigger_collision.set_collision_mask_value(15, true)
	trigger_collision.set_collision_layer_value(15, true)
	trigger_collision.set_collision_mask_value(1, false)
	trigger_collision.set_collision_layer_value(1, false)

func _refresh_preview() -> void:
	if !is_inside_tree():
		return
	if preview_instance:
		preview_instance.queue_free()
		preview_instance = null
	_ensure_collision()
	var scene : PackedScene = TRIGGER_SCENES[int(trigger_type)]
	if scene:
		preview_instance = scene.instantiate() as Node3D
		preview_instance.name = "TriggerPreview"
		add_child(preview_instance)

func _surface_transform(segment : RoadPath) -> Transform3D:
	var basis := _surface_basis(segment, surface_t)
	basis = basis * Basis(Vector3.UP, deg_to_rad(add_yaw_degrees))
	basis.x *= trigger_scale.x
	basis.y *= trigger_scale.y
	basis.z *= trigger_scale.z
	return Transform3D(basis, segment.get_surface_position(surface_t))

func _surface_basis(segment : RoadPath, param : Vector2) -> Basis:
	var tx := clampf(surface_t.x, -1.0, 1.0)
	var ty := clampf(surface_t.y, 0.0, 1.0)
	if param != surface_t:
		tx = clampf(param.x, -1.0, 1.0)
		ty = clampf(param.y, 0.0, 1.0)
	var eps := 0.002
	var tx2 := clampf(tx + eps, -1.0, 1.0)
	if is_equal_approx(tx2, tx):
		tx2 = clampf(tx - eps, -1.0, 1.0)
	var ty2 := clampf(ty + eps, 0.0, 1.0)
	if is_equal_approx(ty2, ty):
		ty2 = clampf(ty - eps, 0.0, 1.0)
	var points := segment.get_surface_positions(PackedVector2Array([
		Vector2(tx, ty),
		Vector2(tx2, ty),
		Vector2(tx, ty2),
	]))
	var base := points[0]
	var right := (points[1] - base).normalized()
	var forward := (points[2] - base).normalized()
	if right.is_zero_approx() or forward.is_zero_approx():
		var root := segment.get_root_transform(ty)
		right = root.basis.x.normalized()
		forward = root.basis.z.normalized()
	var normal := right.cross(forward).normalized()
	if normal.is_zero_approx():
		normal = Vector3.UP
	right = forward.cross(normal).normalized()
	return Basis(right, -normal, forward).orthonormalized()

func _closest_surface_param(segment : RoadPath, world_pos : Vector3) -> Dictionary:
	var best_param := surface_t
	var best_distance := INF
	var center := Vector2(0.0, 0.5)
	var span := Vector2(2.0, 1.0)
	for refine_pass in 4:
		var query := PackedVector2Array()
		var params : Array[Vector2] = []
		var x_steps := 9
		var y_steps := 17
		for y in y_steps:
			for x in x_steps:
				var tx := clampf(center.x - span.x * 0.5 + span.x * (float(x) / float(x_steps - 1)), -1.0, 1.0)
				var ty := clampf(center.y - span.y * 0.5 + span.y * (float(y) / float(y_steps - 1)), 0.0, 1.0)
				var param := Vector2(tx, ty)
				params.append(param)
				query.append(param)
		var positions := segment.get_surface_positions(query)
		for i in positions.size():
			var distance := positions[i].distance_squared_to(world_pos)
			if distance < best_distance:
				best_distance = distance
				best_param = params[i]
		center = best_param
		span *= 0.25
	return {"param": best_param, "distance": best_distance}

func _closest_segment_surface(world_pos : Vector3) -> Dictionary:
	var best_segment : RoadPath = get_target_segment()
	var best_param := surface_t
	var best_distance := INF
	var parent := get_parent()
	if !parent:
		return {"segment": best_segment, "param": best_param}
	for child in parent.get_children():
		var segment := child as RoadPath
		if !segment:
			continue
		var result := _closest_surface_param(segment, world_pos)
		var distance : float = result["distance"]
		if distance < best_distance:
			best_distance = distance
			best_param = result["param"]
			best_segment = segment
	return {"segment": best_segment, "param": best_param}

func _sync_transform_cache() -> void:
	last_synced_transform = global_transform
	has_synced_transform = true

func _sync_attachment_from_transform() -> void:
	var result := _closest_segment_surface(global_position)
	var segment := result["segment"] as RoadPath
	if !segment:
		_sync_transform_cache()
		return
	var next_surface_t : Vector2 = result["param"]
	var surface_basis := _surface_basis(segment, next_surface_t)
	var object_basis := global_basis
	trigger_scale = Vector3(
		maxf(object_basis.x.length(), 0.001),
		maxf(object_basis.y.length(), 0.001),
		maxf(object_basis.z.length(), 0.001))
	var object_basis_normalized := object_basis.orthonormalized()
	var local_forward := surface_basis.inverse() * object_basis_normalized.z
	add_yaw_degrees = rad_to_deg(atan2(local_forward.x, local_forward.z))
	surface_t = next_surface_t
	set_target_segment(segment)
	global_transform = _surface_transform(segment)
	_sync_transform_cache()

func place_on_segment(segment : RoadPath, tx := 0.0, ty := 0.5) -> void:
	if !segment:
		return
	surface_t = Vector2(clampf(tx, -1.0, 1.0), clampf(ty, 0.0, 1.0))
	set_target_segment(segment)
	global_transform = _surface_transform(segment)
	_sync_transform_cache()

func refresh_from_attachment() -> void:
	var segment := get_target_segment()
	if !segment:
		return
	surface_t = Vector2(clampf(surface_t.x, -1.0, 1.0), clampf(surface_t.y, 0.0, 1.0))
	global_transform = _surface_transform(segment)
	_sync_transform_cache()

func attach_to_surface(segment : RoadPath, param : Vector2) -> void:
	if !segment:
		return
	surface_t = Vector2(clampf(param.x, -1.0, 1.0), clampf(param.y, 0.0, 1.0))
	set_target_segment(segment)
	refresh_from_attachment()

func attach_to_nearest_surface(world_pos : Vector3) -> void:
	var result := _closest_segment_surface(world_pos)
	attach_to_surface(result["segment"] as RoadPath, result["param"])

func surface_basis_at_attachment() -> Basis:
	var segment := get_target_segment()
	if !segment:
		return global_basis.orthonormalized()
	return _surface_basis(segment, surface_t)

func _process(_delta : float) -> void:
	if !has_synced_transform:
		_sync_transform_cache()
	var scene := FZGlobal.editing_scene
	if scene and scene.pointer_action_owner and scene.pointer_action_owner != self:
		return
	if global_transform != last_synced_transform:
		_sync_attachment_from_transform()
