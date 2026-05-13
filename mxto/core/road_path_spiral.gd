class_name RoadPathSpiral extends RoadPathBezier

const BAKE_SUBDIVISIONS := 64

@export var axis_transform := Transform3D.IDENTITY
@export var spiral_axis := Vector3.UP
@export var spiral_degrees := 90.0
@export var radius_curve : Resource
@export var height_curve : Resource
@export var twist_curve : Resource
@export var scale_x_curve : Resource
@export var scale_y_curve : Resource

var axis_marker : LineHandle
var should_update := false

func _constrain_spiral_axis() -> void:
	spiral_axis.z = 0.0
	if spiral_axis.length_squared() <= 0.00001:
		spiral_axis = Vector3.UP

func _init() -> void:
	_ensure_spiral_curves()

func _ready() -> void:
	for child in get_children():
		if child is BezierHandle or child is LineHandle:
			child.free()
	_ensure_spiral_curves()
	axis_marker = LineHandle.new()
	add_child(axis_marker)
	axis_marker.global_transform = axis_transform
	axis_marker.cp_scale = Vector3.ONE * 25.0
	create_road_mesh_instance()
	refresh_spiral_road()
	_try_generate_mesh()

func _ensure_curve(curve_name : String, start_value : float, end_value : float) -> Resource:
	var curve : Resource = get(curve_name)
	var should_seed := false
	if !curve:
		curve = ClassDB.instantiate("TrackEditorFloatCurve")
		set(curve_name, curve)
		should_seed = true
	if curve.point_count == 0:
		should_seed = true
	while curve.point_count < 2:
		curve.add_point(Vector2(float(curve.point_count), start_value if curve.point_count == 0 else end_value))
	curve.set_point_offset(0, 0.0)
	curve.set_point_offset(curve.point_count - 1, 1.0)
	if should_seed:
		curve.set_point_value(0, start_value)
		curve.set_point_value(1, end_value)
	return curve

func _ensure_spiral_curves() -> void:
	_ensure_curve("radius_curve", 250.0, 250.0)
	_ensure_curve("height_curve", 0.0, 0.0)
	_ensure_curve("twist_curve", 0.0, 0.0)
	_ensure_curve("scale_x_curve", 25.0, 25.0)
	_ensure_curve("scale_y_curve", 25.0, 25.0)

func refresh_spiral_road() -> void:
	_ensure_spiral_curves()
	_constrain_spiral_axis()
	if !native_curve:
		native_curve = ClassDB.instantiate("TrackEditorCurve")
	axis_transform = axis_marker.global_transform if axis_marker else axis_transform
	segment_length = native_curve.rebuild_spiral_from_packets(
		axis_transform,
		spiral_axis,
		spiral_degrees,
		radius_curve.build_packet(),
		height_curve.build_packet(),
		twist_curve.build_packet(),
		scale_x_curve.build_packet(),
		scale_y_curve.build_packet(),
		BAKE_SUBDIVISIONS)

func _process(delta : float) -> void:
	if !(road_shape and native_curve):
		return
	if !(road_mesh_instance and road_collision):
		return
	road_mesh_instance.global_transform = Transform3D.IDENTITY
	road_collision.global_transform = Transform3D.IDENTITY
	if axis_marker and axis_marker.global_transform != axis_transform:
		should_update = true
	if should_update:
		refresh_spiral_road()
		var update_collision := Time.get_ticks_msec() > last_gen_time + 100
		if update_collision:
			last_gen_time = Time.get_ticks_msec()
		_try_generate_mesh(update_collision)
		should_update = false
