class_name TrackEditingScene extends Node3D

const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")
const EmbedGizmoScript := preload("res://core/embed_gizmo.gd")
const RailGizmoScript := preload("res://core/rail_gizmo.gd")
const ModulationGizmoScript := preload("res://core/modulation_gizmo.gd")
const ShapeCurveGizmoScript := preload("res://core/shape_curve_gizmo.gd")
const MeshLayoutGizmoScript := preload("res://core/mesh_layout_gizmo.gd")
const CheckpointGizmoScript := preload("res://core/checkpoint_gizmo.gd")
const RoadPathSpiralScript := preload("res://core/road_path_spiral.gd")
const TrackTriggerScript := preload("res://core/track_trigger.gd")

enum ToolMode {
	EDIT_SEGMENT,
	ADD_SEGMENT,
	ADD_EMBED,
	EDIT_RAILS,
	EDIT_MODULATION,
	EDIT_SHAPE,
	EDIT_CHECKPOINTS,
	ADD_OBJECT,
	EDIT_TRACK,
}

signal tool_mode_changed(mode : ToolMode)
signal track_structure_changed

@onready var edit_cam: Camera3D = $EditorCamera
@onready var ref_cam: Camera3D = $RefCamera
@onready var track_root: TrackRoot = $TrackRoot
@onready var grid_origin: Node3D = $GridOrigin
@onready var mouse_picker_cast: RayCast3D = $MousePickerCast
@onready var mouse_gizmo_cast: RayCast3D = $MouseGizmoCast

@onready var translate_gizmo: TranslateGizmo = $TranslateGizmo
@onready var rotate_gizmo: RotateGizmo = $RotateGizmo
@onready var add_road_gizmo: AddRoadGizmo = $AddRoadGizmo

var cam_dist := 300.0
var cam_dist_desired := 300.0
var cam_origin := Vector3.ZERO
var cam_origin_desired := Vector3.ZERO
var last_trackball_dir := Vector3.UP
var last_pan_plane := Plane.PLANE_XZ
var last_mouse_pos := Vector2.ZERO

var active_path : RoadPath
var tool_mode : ToolMode = ToolMode.EDIT_SEGMENT
var desired_road_type := ENUMS.ROAD_TYPE.STANDARD
var desired_embed_type := RoadEmbed.EmbedType.RECHARGE
var desired_trigger_type := 0
var pending_embed_add := false
var editor_cross_section_t := 0.5
var pointer_action_owner : Node
var embed_gizmo : Node3D
var rail_gizmo : Node3D
var modulation_gizmo : Node3D
var shape_curve_gizmo : Node3D
var mesh_layout_gizmo : Node3D
var checkpoint_gizmo : Node3D

const simple_mesh_layout : PackedFloat32Array = [0.0, 0.25, 0.5, 0.75, 1.0]
const cylinder_mesh_layout : PackedFloat32Array = [0.0, 0.032258064516129, 0.064516129032258, 0.096774193548387, 0.12903225806452, 0.16129032258065, 0.19354838709677, 0.2258064516129, 0.25806451612903, 0.29032258064516, 0.32258064516129, 0.35483870967742, 0.38709677419355, 0.41935483870968, 0.45161290322581, 0.48387096774194, 0.51612903225806, 0.54838709677419, 0.58064516129032, 0.61290322580645, 0.64516129032258, 0.67741935483871, 0.70967741935484, 0.74193548387097, 0.7741935483871, 0.80645161290323, 0.83870967741935, 0.87096774193548, 0.90322580645161, 0.93548387096774, 0.96774193548387, 1.0]
const DEFAULT_EMBED_LENGTH := 0.3
const DEFAULT_EMBED_WIDTH := 0.7

func set_tool_mode(in_mode : ToolMode) -> void:
	if tool_mode == in_mode:
		return
	if in_mode != ToolMode.ADD_EMBED:
		pending_embed_add = false
	tool_mode = in_mode
	tool_mode_changed.emit(tool_mode)

func tool_mode_allows_transform_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SEGMENT

func tool_mode_allows_segment_add_gizmo() -> bool:
	return tool_mode == ToolMode.ADD_SEGMENT

func tool_mode_allows_control_point_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SEGMENT

func tool_mode_allows_embed_gizmos() -> bool:
	return tool_mode == ToolMode.ADD_EMBED

func tool_mode_allows_rail_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_RAILS

func tool_mode_allows_modulation_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_MODULATION

func tool_mode_allows_shape_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SHAPE

func tool_mode_allows_checkpoint_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_CHECKPOINTS

func set_active_embed(in_path : RoadPath, in_embed_index : int) -> void:
	if !embed_gizmo:
		return
	embed_gizmo.set_target_embed(in_path, in_embed_index)

func set_active_rail_path(in_path : RoadPath) -> void:
	if !rail_gizmo:
		return
	rail_gizmo.set_target_path(in_path)

func set_active_modulation(in_path : RoadPath, in_modulation_index : int) -> void:
	if !modulation_gizmo:
		return
	modulation_gizmo.set_target_modulation(in_path, in_modulation_index)

func set_active_shape_path(in_path : RoadPath) -> void:
	if !shape_curve_gizmo:
		return
	shape_curve_gizmo.set_target_path(in_path)
	if mesh_layout_gizmo:
		mesh_layout_gizmo.set_target_path(in_path)

func set_active_checkpoint_path(in_path : RoadPath) -> void:
	if !checkpoint_gizmo:
		return
	checkpoint_gizmo.set_target_path(in_path)

func begin_pointer_action(owner : Node) -> bool:
	if pointer_action_owner and pointer_action_owner != owner:
		return false
	pointer_action_owner = owner
	return true

func owns_pointer_action(owner : Node) -> bool:
	return pointer_action_owner == owner

func pointer_action_busy_for(owner : Node) -> bool:
	return pointer_action_owner and pointer_action_owner != owner

func end_pointer_action(owner : Node) -> void:
	if pointer_action_owner == owner:
		pointer_action_owner = null

func find_camera_trackball_normal() -> Vector3:
	var sphere_pos := ref_cam.position + ref_cam.basis.z * -100
	var sphere_radius := 95
	var ray := ref_cam.project_ray_normal(get_viewport().get_mouse_position())
	var intersect := Geometry3D.segment_intersects_sphere(ref_cam.position, ref_cam.position + ray * 1024, sphere_pos, sphere_radius)
	if intersect.size() > 0:
		return intersect[1]
	else:
		return last_trackball_dir

func _ready() -> void:
	FZGlobal.current_cam = edit_cam
	FZGlobal.editing_scene = self
	FZGlobal.current_track = track_root
	embed_gizmo = EmbedGizmoScript.new()
	add_child(embed_gizmo)
	rail_gizmo = RailGizmoScript.new()
	add_child(rail_gizmo)
	modulation_gizmo = ModulationGizmoScript.new()
	add_child(modulation_gizmo)
	shape_curve_gizmo = ShapeCurveGizmoScript.new()
	add_child(shape_curve_gizmo)
	mesh_layout_gizmo = MeshLayoutGizmoScript.new()
	add_child(mesh_layout_gizmo)
	checkpoint_gizmo = CheckpointGizmoScript.new()
	add_child(checkpoint_gizmo)
	translate_gizmo.mouse_cast = mouse_gizmo_cast
	rotate_gizmo.mouse_cast = mouse_gizmo_cast
	add_road_gizmo.mouse_cast = mouse_gizmo_cast
	embed_gizmo.mouse_cast = mouse_gizmo_cast
	rail_gizmo.mouse_cast = mouse_gizmo_cast
	modulation_gizmo.mouse_cast = mouse_gizmo_cast
	shape_curve_gizmo.mouse_cast = mouse_gizmo_cast
	mesh_layout_gizmo.mouse_cast = mouse_gizmo_cast
	checkpoint_gizmo.mouse_cast = mouse_gizmo_cast

func save_edit_source(path : String) -> Error:
	return track_root.save_edit_source(path)

func export_mxt_track(path : String) -> Error:
	return track_root.export_mxt_track(path)

func update_mouse_casts(force_update := false) -> void:
	var ray_end := edit_cam.global_position + edit_cam.project_ray_normal(get_viewport().get_mouse_position()) * 4096
	mouse_picker_cast.global_position = edit_cam.global_position
	mouse_picker_cast.target_position = mouse_picker_cast.to_local(ray_end)
	mouse_gizmo_cast.global_position = edit_cam.global_position
	mouse_gizmo_cast.target_position = mouse_gizmo_cast.to_local(ray_end)
	if force_update:
		mouse_picker_cast.force_raycast_update()
		mouse_gizmo_cast.force_raycast_update()

func _road_path_from_collider(collider : Object) -> RoadPath:
	var node := collider as Node
	while node:
		if node is RoadPath:
			return node
		node = node.get_parent()
	return null

func _pick_road_path_under_mouse() -> Dictionary:
	mouse_picker_cast.clear_exceptions()
	mouse_picker_cast.force_raycast_update()
	while mouse_picker_cast.is_colliding():
		var collider := mouse_picker_cast.get_collider()
		var path := _road_path_from_collider(collider)
		if path:
			var point := mouse_picker_cast.get_collision_point()
			mouse_picker_cast.clear_exceptions()
			return {"path": path, "point": point}
		var collision_object := collider as CollisionObject3D
		if !collision_object:
			break
		mouse_picker_cast.add_exception(collision_object)
		mouse_picker_cast.force_raycast_update()
	mouse_picker_cast.clear_exceptions()
	return {}

func _closest_surface_param(segment : RoadPath, world_pos : Vector3) -> Vector2:
	var best_param := Vector2(0.0, 0.5)
	var best_distance := INF
	var center := best_param
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
	return best_param

func _handle_add_object_input() -> void:
	if tool_mode != ToolMode.ADD_OBJECT:
		return
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return
	if !begin_pointer_action(self):
		return
	var hit := _pick_road_path_under_mouse()
	if !hit.is_empty():
		var path := hit["path"] as RoadPath
		var surface_t := _closest_surface_param(path, hit["point"])
		var trigger := add_track_trigger(desired_trigger_type, path, surface_t)
		if trigger:
			FZGlobal.select_node(trigger)
			set_tool_mode(ToolMode.EDIT_SEGMENT)
			get_viewport().set_input_as_handled()
	end_pointer_action(self)

func _handle_add_embed_input() -> void:
	if tool_mode != ToolMode.ADD_EMBED or !pending_embed_add:
		return
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return
	if !begin_pointer_action(self):
		return
	var hit := _pick_road_path_under_mouse()
	if !hit.is_empty():
		var path := hit["path"] as RoadPath
		var surface_t := _closest_surface_param(path, hit["point"])
		var embed_index := add_road_embed(desired_embed_type, path, surface_t)
		if embed_index >= 0:
			active_path = path
			set_active_embed(path, embed_index)
			FZGlobal.select_node(path)
			pending_embed_add = false
			get_viewport().set_input_as_handled()
	end_pointer_action(self)

func _handle_add_segment_input() -> void:
	if tool_mode != ToolMode.ADD_SEGMENT:
		return
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return
	if mouse_gizmo_cast.is_colliding():
		return
	if !begin_pointer_action(self):
		return
	var hit := _pick_road_path_under_mouse()
	if !hit.is_empty():
		var path := hit["path"] as RoadPath
		var new_segment := add_bezier_track_segment_after(path, desired_road_type)
		if new_segment:
			active_path = new_segment
			add_road_gizmo.set_target_node(new_segment)
			FZGlobal.select_node(new_segment)
			get_viewport().set_input_as_handled()
	end_pointer_action(self)

func _process(delta: float) -> void:
	if pointer_action_owner and !is_instance_valid(pointer_action_owner):
		pointer_action_owner = null
	if Input.is_action_just_pressed("MiddleMouse"):
		last_trackball_dir = find_camera_trackball_normal()
		if mouse_picker_cast.is_colliding():
			last_pan_plane = Plane(-edit_cam.basis.z, mouse_picker_cast.get_collision_point())
		else:
			var sphere_pos := cam_origin.lerp(edit_cam.global_position, 0.5)
			var sphere_radius := cam_dist * 0.5
			var ray := edit_cam.project_ray_normal(get_viewport().get_mouse_position())
			var p1 := edit_cam.global_position
			var intersect := Geometry3D.segment_intersects_sphere(p1 + ray * cam_dist * 2.2, p1, sphere_pos, sphere_radius)
			if intersect.size() > 0:
				var use_normal := intersect[1].slerp(-edit_cam.basis.z, 0.8)
				last_pan_plane = Plane(use_normal, intersect[0])
	if Input.is_action_pressed("MiddleMouse") and !Input.is_action_pressed("Shift"):
		var cur_trackball_dir := find_camera_trackball_normal()
		if !cur_trackball_dir.is_equal_approx(last_trackball_dir):
			var axis := last_trackball_dir.cross(cur_trackball_dir).normalized()
			var angle := last_trackball_dir.signed_angle_to(cur_trackball_dir, axis)
			var port_size : Vector2i = get_viewport().size
			var mp := get_viewport().get_mouse_position() / Vector2(port_size)
			var dist_to_edge := mp.distance_to(Vector2(0.5, 0.5))
			dist_to_edge = minf(1.0, dist_to_edge * 2)
			var angle_from_center := axis.angle_to(Vector3.FORWARD) - PI * 0.5
			angle_from_center = clampf(remap(angle_from_center, -0.08, 0.08, 1, -1), -1, 1)
			axis = axis.slerp(Vector3.FORWARD, dist_to_edge * angle_from_center)
			axis = edit_cam.basis * axis
			axis = axis.normalized()
			edit_cam.basis = edit_cam.basis.rotated(axis, angle * -24)
			last_trackball_dir = cur_trackball_dir
	
	if Input.is_action_just_pressed("ZoomIn"):
		cam_dist_desired *= 0.8
	if Input.is_action_just_pressed("ZoomOut"):
		cam_dist_desired *= (1.0 / 0.8)
	
	if Input.is_action_pressed("MiddleMouse") and Input.is_action_pressed("Shift"):
		last_trackball_dir = find_camera_trackball_normal()
		var r1 := edit_cam.project_ray_normal(last_mouse_pos)
		var r2 := edit_cam.project_ray_normal(get_viewport().get_mouse_position())
		var p1 = last_pan_plane.intersects_ray(edit_cam.position, r1)
		var p2 = last_pan_plane.intersects_ray(edit_cam.position, r2)
		if p1 and p2:
			cam_origin_desired += -(p2 - p1)
	cam_origin = cam_origin.lerp(cam_origin_desired, delta * 30)
	cam_dist = lerpf(cam_dist, cam_dist_desired, delta * 12)
	edit_cam.position = cam_origin + edit_cam.basis.z * cam_dist
	last_mouse_pos = get_viewport().get_mouse_position()
	grid_origin.position = grid_origin.position.lerp(snapped(cam_origin, Vector3(16, 16, 16)), delta * 30)
	update_mouse_casts(true)
	_handle_add_segment_input()
	_handle_add_embed_input()
	_handle_add_object_input()

func _apply_road_type_to_path(path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> void:
	match road_type:
		ENUMS.ROAD_TYPE.STANDARD:
			path.road_shape = RoadShape.new()
			path.horizontal_road_mesh_segments = simple_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.PIPE:
			path.road_shape = RoadShapePipe.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.CYLINDER:
			path.road_shape = RoadShapeCylinder.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.PIPE_OPEN:
			path.road_shape = RoadShapePipeOpen.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.CYLINDER_OPEN:
			path.road_shape = RoadShapeCylinderOpen.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.ROUNDED_SQUARE:
			path.road_shape = RoadShapeRoundedSquareScript.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.ROUNDED_SQUARE_OPEN:
			path.road_shape = RoadShapeRoundedSquareOpenScript.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()

func add_bezier_track_segment_after(in_path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> RoadPathBezier:
	var new_track_piece := RoadPathBezier.new()
	_apply_road_type_to_path(new_track_piece, road_type)
	
	var latest_track_piece_transform := in_path.get_root_transform(1.0)
	var latest_track_piece_rotation := latest_track_piece_transform.basis.orthonormalized()
	var latest_track_piece_scale := latest_track_piece_transform.basis.get_scale()
	
	new_track_piece.add_control_point(
		0.0,
		latest_track_piece_transform.origin,
		latest_track_piece_rotation,
		latest_track_piece_scale,
		166.0,
		166.0,
		0)
	new_track_piece.add_control_point(
		1.0,
		latest_track_piece_transform.origin + latest_track_piece_rotation.z.normalized() * 500.0,
		latest_track_piece_rotation,
		latest_track_piece_scale,
		166.0,
		166.0,
		1)
	track_root.add_child(new_track_piece)
	var id_to_put_above := track_root.get_children().find(in_path)
	track_root.move_child(new_track_piece, id_to_put_above + 1)
	track_structure_changed.emit()
	return new_track_piece
	

func add_regular_track_segment_after(in_path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> RoadPathLine:
	var new_track_piece := RoadPathLine.new()
	var latest_track_piece_transform := in_path.get_root_transform(1.0)
	_apply_road_type_to_path(new_track_piece, road_type)
	new_track_piece.start_pos = latest_track_piece_transform.origin
	new_track_piece.start_rotation = latest_track_piece_transform.basis.orthonormalized()
	new_track_piece.start_scale = latest_track_piece_transform.basis.get_scale()
	new_track_piece.end_pos = latest_track_piece_transform.origin + latest_track_piece_transform.basis.orthonormalized().z * 250
	new_track_piece.end_rotation = latest_track_piece_transform.basis.orthonormalized()
	new_track_piece.end_scale = latest_track_piece_transform.basis.get_scale()
	track_root.add_child(new_track_piece)
	var id_to_put_above := track_root.get_children().find(in_path)
	track_root.move_child(new_track_piece, id_to_put_above + 1)
	track_structure_changed.emit()
	return new_track_piece

func add_spiral_track_segment_after(in_path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> RoadPath:
	var new_track_piece : RoadPath = RoadPathSpiralScript.new()
	var latest_track_piece_transform := in_path.get_root_transform(1.0)
	_apply_road_type_to_path(new_track_piece, road_type)
	new_track_piece.axis_transform = latest_track_piece_transform
	new_track_piece.axis_transform.basis = latest_track_piece_transform.basis.orthonormalized()
	new_track_piece.spiral_axis = Vector3.UP
	track_root.add_child(new_track_piece)
	var id_to_put_above := track_root.get_children().find(in_path)
	track_root.move_child(new_track_piece, id_to_put_above + 1)
	track_structure_changed.emit()
	return new_track_piece

func add_road_embed(embed_type : int, in_path : RoadPath, surface_t := Vector2(0.0, 0.5)) -> int:
	if !in_path:
		return -1
	var length := minf(DEFAULT_EMBED_LENGTH, 1.0)
	var width := minf(DEFAULT_EMBED_WIDTH, 2.0)
	var road_start := clampf(surface_t.y - length * 0.5, 0.0, 1.0 - length)
	var road_end := road_start + length
	var left := clampf(surface_t.x - width * 0.5, -1.0, 1.0 - width)
	var right := left + width
	var new_embed := RoadEmbed.new()
	new_embed.road_start = road_start
	new_embed.road_end = road_end
	new_embed.embed_type = embed_type
	new_embed.left_boundary.set_point_offset(0, road_start)
	new_embed.left_boundary.set_point_offset(1, road_end)
	new_embed.right_boundary.set_point_offset(0, road_start)
	new_embed.right_boundary.set_point_offset(1, road_end)
	new_embed.left_boundary.set_point_value(0, left)
	new_embed.left_boundary.set_point_value(1, left)
	new_embed.right_boundary.set_point_value(0, right)
	new_embed.right_boundary.set_point_value(1, right)
	in_path.road_shape.embed_table.append(new_embed)
	in_path._try_generate_mesh()
	track_structure_changed.emit()
	return in_path.road_shape.embed_table.size() - 1

func add_track_trigger(trigger_type : int, in_path : RoadPath, surface_t := Vector2(0.0, 0.5)) -> Node3D:
	if !in_path:
		return null
	var trigger : Node3D = TrackTriggerScript.new()
	trigger.set("trigger_type", trigger_type)
	track_root.add_child(trigger)
	trigger.name = String(trigger.call("trigger_label")) + " Trigger"
	trigger.call("place_on_segment", in_path, surface_t.x, surface_t.y)
	track_structure_changed.emit()
	return trigger
