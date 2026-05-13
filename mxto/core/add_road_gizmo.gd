class_name AddRoadGizmo extends Node3D

const TrackEditorRadialMenuScript := preload("res://ui/track_editor/radial_menu.gd")

@onready var gizmo_mesh: MeshInstance3D = $AddGizmo
@onready var gizmo_collider: StaticBody3D = $StaticBody3D
var mouse_cast : RayCast3D

var gizmo_material := preload("res://asset/mat/add_road_gizmo_material.tres")
var target_node : RoadPath

var road_segment_texture := preload("res://asset/tex/add_road_segment.png")
var active := true
var pressed_on_gizmo := false
var press_screen_position := Vector2.ZERO
var active_radial_menu : Control

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
			press_screen_position = get_viewport().get_mouse_position()
			get_viewport().set_input_as_handled()
	
	if Input.is_action_just_released("LeftMouse"):
		var can_release := pressed_on_gizmo and scene.owns_pointer_action(self) and mousecast_wants_this_gizmo()
		if pressed_on_gizmo or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		pressed_on_gizmo = false
		scene.end_pointer_action(self)
		if !can_release:
			return
		_open_add_segment_menu(press_screen_position)
	if !active:
		gizmo_collider.set_collision_layer_value(16, false)
		return
	else:
		gizmo_collider.set_collision_layer_value(16, true)

func _open_add_segment_menu(screen_position : Vector2) -> void:
	if active_radial_menu and is_instance_valid(active_radial_menu):
		return
	var scene := FZGlobal.editing_scene
	if !scene or !target_node or !is_instance_valid(target_node):
		return
	var add_target := target_node
	active_radial_menu = TrackEditorRadialMenuScript.new()
	scene.add_child(active_radial_menu)
	active_radial_menu.open_options(_shape_options(), screen_position)
	var shape_result : Dictionary = await active_radial_menu.finished
	if _menu_cancelled(shape_result):
		active_radial_menu = null
		return
	var road_type := int(shape_result.get("value", ENUMS.ROAD_TYPE.STANDARD))
	if !active_radial_menu or !is_instance_valid(active_radial_menu):
		return
	await active_radial_menu.transition_options(_segment_kind_options())
	var segment_result : Dictionary = await active_radial_menu.finished
	if _menu_cancelled(segment_result):
		active_radial_menu = null
		return
	var segment_kind := int(segment_result.get("value", TrackEditingScene.SegmentKind.BEZIER))
	if active_radial_menu and is_instance_valid(active_radial_menu):
		await active_radial_menu.close_menu()
	active_radial_menu = null
	if !scene or !is_instance_valid(scene) or !is_instance_valid(add_target):
		return
	var new_segment := scene.add_track_segment_after(add_target, segment_kind, road_type)
	if new_segment:
		scene.active_path = new_segment
		set_target_node(new_segment)
		FZGlobal.select_node(new_segment)

func _menu_cancelled(result : Dictionary) -> bool:
	return bool(result.get("cancelled", true))

func _shape_options() -> Array:
	return [
		{"label": "Std", "tooltip": "Standard Road", "value": ENUMS.ROAD_TYPE.STANDARD},
		{"label": "Pipe", "tooltip": "Pipe Road", "value": ENUMS.ROAD_TYPE.PIPE},
		{"label": "Cyl", "tooltip": "Cylinder Road", "value": ENUMS.ROAD_TYPE.CYLINDER},
		{"label": "Open\nPipe", "tooltip": "Open Pipe Road", "value": ENUMS.ROAD_TYPE.PIPE_OPEN},
		{"label": "Open\nCyl", "tooltip": "Open Cylinder Road", "value": ENUMS.ROAD_TYPE.CYLINDER_OPEN},
		{"label": "Round", "tooltip": "Rounded Square Road", "value": ENUMS.ROAD_TYPE.ROUNDED_SQUARE},
		{"label": "Open\nRound", "tooltip": "Open Rounded Square Road", "value": ENUMS.ROAD_TYPE.ROUNDED_SQUARE_OPEN},
	]

func _segment_kind_options() -> Array:
	return [
		{"label": "Line", "tooltip": "Line Segment", "value": TrackEditingScene.SegmentKind.LINE},
		{"label": "Bezier", "tooltip": "Bezier Segment", "value": TrackEditingScene.SegmentKind.BEZIER},
		{"label": "Spiral", "tooltip": "Spiral Segment", "value": TrackEditingScene.SegmentKind.SPIRAL},
	]

func mousecast_wants_this_gizmo() -> bool:
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_over_editor_ui():
		return false
	return mouse_cast.get_collider() == gizmo_collider and !FZGlobal.editing_scene.rotate_gizmo.is_moving and !FZGlobal.editing_scene.translate_gizmo.is_moving
