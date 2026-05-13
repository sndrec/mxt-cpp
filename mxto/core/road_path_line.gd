class_name RoadPathLine extends RoadPathBezier

@export var start_pos : Vector3 = Vector3.ZERO
@export var end_pos : Vector3 = Vector3.ZERO
@export var start_scale : Vector3 = Vector3.ONE * 25
@export var end_scale : Vector3 = Vector3.ONE * 25
@export var start_rotation : Basis = Basis.IDENTITY
@export var end_rotation : Basis = Basis.IDENTITY
@export var start_tangent : float = 0.5
@export var end_tangent : float = 0.5

var start_marker : LineHandle
var end_marker : LineHandle
var should_update := false

func _ready():
	for child in get_children():
		if child is LineHandle:
			child.free()
	start_marker = LineHandle.new()
	add_child(start_marker)
	start_marker.global_position = start_pos
	start_marker.global_basis = start_rotation
	start_marker.cp_scale = start_scale
	
	end_marker = LineHandle.new()
	add_child(end_marker)
	if !end_pos.is_zero_approx():
		end_marker.global_position = end_pos
	else:
		end_marker.global_position = Vector3(0, 0, 250)
	end_marker.global_basis = end_rotation
	end_marker.cp_scale = end_scale
	create_road_mesh_instance()
	refresh_line_road()
	_try_generate_mesh()

var is_clicking := false
var click_timeout := 0

func refresh_line_road() -> void:
	if !native_curve:
		native_curve = ClassDB.instantiate("TrackEditorCurve")
	native_curve.curve_mode = 1
	start_pos = start_marker.global_position
	end_pos = end_marker.global_position
	start_rotation = start_marker.global_basis.orthonormalized()
	end_rotation = end_marker.global_basis.orthonormalized()
	start_scale = start_marker.cp_scale
	end_scale = end_marker.cp_scale
	segment_length = start_pos.distance_to(end_pos)
	native_curve.clear_control_points()
	native_curve.insert_control_point(0, 0.0, start_pos, start_rotation, start_scale, 0.0, 0.0, 3, 1.0, 3, 1.0, 3, 1.0)
	native_curve.insert_control_point(1, 1.0, end_pos, end_rotation, end_scale, 0.0, 0.0, 3, 1.0, 3, 1.0, 3, 1.0)

func _process(delta):
	if !(road_shape and native_curve):
		return
	if !(road_mesh_instance and road_collision):
		return
	road_mesh_instance.global_transform = Transform3D.IDENTITY
	road_collision.global_transform = Transform3D.IDENTITY
	if start_marker.global_position != start_pos:
		should_update = true
	if end_marker.global_position != end_pos:
		should_update = true
	if start_marker.global_basis.orthonormalized() != start_rotation:
		should_update = true
	if end_marker.global_basis.orthonormalized() != end_rotation:
		should_update = true
	if start_marker.cp_scale != start_scale:
		should_update = true
	if end_marker.cp_scale != end_scale:
		should_update = true
	if should_update:
		refresh_line_road()
		var update_collision := Time.get_ticks_msec() > last_gen_time + 100
		if update_collision:
			last_gen_time = Time.get_ticks_msec()
		_try_generate_mesh(update_collision)
		should_update = false
