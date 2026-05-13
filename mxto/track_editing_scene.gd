class_name TrackEditingScene extends Node3D

const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")

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

const simple_mesh_layout : PackedFloat32Array = [0.0, 0.25, 0.5, 0.75, 1.0]
const cylinder_mesh_layout : PackedFloat32Array = [0.0, 0.032258064516129, 0.064516129032258, 0.096774193548387, 0.12903225806452, 0.16129032258065, 0.19354838709677, 0.2258064516129, 0.25806451612903, 0.29032258064516, 0.32258064516129, 0.35483870967742, 0.38709677419355, 0.41935483870968, 0.45161290322581, 0.48387096774194, 0.51612903225806, 0.54838709677419, 0.58064516129032, 0.61290322580645, 0.64516129032258, 0.67741935483871, 0.70967741935484, 0.74193548387097, 0.7741935483871, 0.80645161290323, 0.83870967741935, 0.87096774193548, 0.90322580645161, 0.93548387096774, 0.96774193548387, 1.0]

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
	translate_gizmo.mouse_cast = mouse_gizmo_cast
	rotate_gizmo.mouse_cast = mouse_gizmo_cast
	add_road_gizmo.mouse_cast = mouse_gizmo_cast

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

func _process(delta: float) -> void:
	if Input.is_action_just_pressed("MiddleMouse"):
		last_trackball_dir = find_camera_trackball_normal()
		if mouse_picker_cast.is_colliding():
			last_pan_plane = Plane(-edit_cam.basis.z, mouse_picker_cast.get_collision_point())
		else:
			var sphere_pos := cam_origin.lerp(edit_cam.global_position, 0.5)
			#print("yay")
			var sphere_radius := cam_dist * 0.5
			#DebugDraw3D.draw_sphere(sphere_pos, sphere_radius, Color.WHITE, 0.5)
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
			#print("---")
			#print(dist_to_edge)
			dist_to_edge = minf(1.0, dist_to_edge * 2)
			#print(dist_to_edge)
			var angle_from_center := axis.angle_to(Vector3.FORWARD) - PI * 0.5
			angle_from_center = clampf(remap(angle_from_center, -0.08, 0.08, 1, -1), -1, 1)
			#print(angle_from_center)
			axis = axis.slerp(Vector3.FORWARD, dist_to_edge * angle_from_center)
			axis = edit_cam.basis * axis
			axis = axis.normalized()
			#print(axis)
			#print(angle)
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
	#DebugDraw3D.draw_gizmo(Transform3D.IDENTITY.translated(cam_origin), Color.RED, true, delta)
	grid_origin.position = grid_origin.position.lerp(snapped(cam_origin, Vector3(16, 16, 16)), delta * 30)
	update_mouse_casts(true)

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
	return new_track_piece
