class_name AddRoadGizmo extends Node3D

@onready var gizmo_mesh: MeshInstance3D = $AddGizmo
@onready var gizmo_collider: StaticBody3D = $StaticBody3D
var mouse_cast : RayCast3D

var gizmo_material := preload("res://asset/mat/add_road_gizmo_material.tres")
var target_node : RoadPath

var road_segment_texture := preload("res://asset/tex/add_road_segment.png")
var bezier_point_texture := preload("res://asset/tex/add_bezier_point.png")
var active := true
var pressed_on_gizmo := false

func _segment_kind_tint(scene : TrackEditingScene) -> Color:
	match scene.desired_segment_kind:
		TrackEditingScene.SegmentKind.LINE:
			return Color(0.45, 0.75, 1.0, 1.0)
		TrackEditingScene.SegmentKind.SPIRAL:
			return Color(1.0, 0.72, 0.28, 1.0)
		_:
			return Color(0.42, 1.0, 0.52, 1.0)

func set_target_node(in_node : Node3D) -> void:
	if is_instance_valid(in_node) and (in_node is RoadPath or in_node.get_parent() is RoadPath):
		scale = Vector3(50, 50, 50)
		target_node = in_node if in_node is RoadPath else in_node.get_parent()
		active = true
	else:
		scale = Vector3(50, 50, 50)
		target_node = null
		active = false

func _process(delta: float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_segment_add_gizmo():
		if scene:
			scene.end_pointer_action(self)
		visible = false
		gizmo_collider.set_collision_layer_value(16, false)
		pressed_on_gizmo = false
		return
	visible = true
	if !target_node or !is_instance_valid(target_node):
		scene.end_pointer_action(self)
		visible = false
		gizmo_collider.set_collision_layer_value(16, false)
		pressed_on_gizmo = false
		return
	var end_of_road := target_node.get_root_transform(1.0)
	global_transform = end_of_road
	scale = Vector3(end_of_road.basis.x.length() * 2.0, end_of_road.basis.x.length() * 2.0, end_of_road.basis.x.length() * 2.0)
	if target_node is RoadPathBezier and !(target_node is RoadPathLine) and Input.is_action_pressed("Alt"):
		gizmo_material.set_shader_parameter("in_texture", bezier_point_texture)
		gizmo_material.set_shader_parameter("tint", Color.WHITE)
	else:
		gizmo_material.set_shader_parameter("in_texture", road_segment_texture)
		gizmo_material.set_shader_parameter("tint", _segment_kind_tint(scene))
	if mousecast_wants_this_gizmo():
		gizmo_material.set_shader_parameter("selected", true)
	else:
		gizmo_material.set_shader_parameter("selected", false)
		
	if Input.is_action_pressed("LeftMouse") and mousecast_wants_this_gizmo():
		gizmo_material.set_shader_parameter("clicked", true)
	else:
		gizmo_material.set_shader_parameter("clicked", false)

	if Input.is_action_just_pressed("LeftMouse") and mousecast_wants_this_gizmo():
		pressed_on_gizmo = scene.begin_pointer_action(self)
		if pressed_on_gizmo:
			get_viewport().set_input_as_handled()
	
	if Input.is_action_just_released("LeftMouse"):
		var can_release := pressed_on_gizmo and scene.owns_pointer_action(self) and mousecast_wants_this_gizmo()
		if pressed_on_gizmo or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		pressed_on_gizmo = false
		scene.end_pointer_action(self)
		if !can_release:
			return
		if target_node is RoadPathBezier and !(target_node is RoadPathLine) and Input.is_action_pressed("Alt"):
			var last_index : int = target_node.get_control_point_count() - 1
			var handle_out : float = target_node.native_curve.get_control_point_handle_out(last_index)
			target_node.add_control_point(
				1.0,
				end_of_road.origin + end_of_road.basis.z.normalized() * handle_out * 3.0,
				target_node.native_curve.get_control_point_rotation(last_index),
				target_node.native_curve.get_control_point_scale(last_index),
				handle_out,
				handle_out)
			target_node.point_changes = true
		else:
			var new_segment := scene.add_desired_track_segment_after(target_node)
			if new_segment:
				scene.active_path = new_segment
				set_target_node(new_segment)
				FZGlobal.select_node(new_segment)
	if !active:
		gizmo_collider.set_collision_layer_value(16, false)
		return
	else:
		gizmo_collider.set_collision_layer_value(16, true)

func mousecast_wants_this_gizmo() -> bool:
	return mouse_cast.get_collider() == gizmo_collider and !FZGlobal.editing_scene.rotate_gizmo.is_moving and !FZGlobal.editing_scene.translate_gizmo.is_moving
