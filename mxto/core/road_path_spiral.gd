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
	_ensure_curve("radius_curve", 50.0, 100.0)
	_ensure_curve("height_curve", 0.0, 0.0)
	_ensure_curve("twist_curve", 0.0, 0.0)
	_ensure_curve("scale_x_curve", 25.0, 25.0)
	_ensure_curve("scale_y_curve", 25.0, 25.0)

func _sample_curve(curve : Resource, t : float, fallback : float) -> float:
	if !curve:
		return fallback
	return curve.sample(clampf(t, 0.0, 1.0))

func _spiral_axis() -> Vector3:
	var axis := spiral_axis.normalized()
	if axis.is_zero_approx():
		return Vector3.UP
	return axis

func _perpendicular_to_axis(axis : Vector3) -> Vector3:
	var out := Vector3(axis.y, -axis.x, 0.0)
	if out.is_zero_approx():
		out = Vector3.RIGHT
	return out.normalized()

func _canonical_spiral_transform(t : float) -> Transform3D:
	var axis := _spiral_axis()
	var radius := _sample_curve(radius_curve, t, 50.0)
	var height := _sample_curve(height_curve, t, 0.0)
	var angle := deg_to_rad(spiral_degrees) * t
	var about := _perpendicular_to_axis(axis) * radius
	var rot := Quaternion(axis, angle)
	var pos := -(Basis(rot) * about) + axis * height
	var basis := Basis(rot)
	var twist := deg_to_rad(_sample_curve(twist_curve, t, 0.0))
	var twist_axis := basis.z.normalized()
	if !twist_axis.is_zero_approx():
		basis = Basis(Quaternion(twist_axis, twist)) * basis
	var eps := 0.001
	var t2 := clampf(t + eps, 0.0, 1.0)
	if is_equal_approx(t2, t):
		t2 = clampf(t - eps, 0.0, 1.0)
	var radius2 := _sample_curve(radius_curve, t2, 100.0)
	var height2 := _sample_curve(height_curve, t2, 0.0)
	var angle2 := deg_to_rad(spiral_degrees) * t2
	var pos2 := -(Basis(Quaternion(axis, angle2)) * (_perpendicular_to_axis(axis) * radius2)) + axis * height2
	var tangent := (pos2 - pos).normalized()
	if !tangent.is_zero_approx():
		var current_z := basis.z.normalized()
		var axis_x := basis.x.normalized()
		var z_proj := current_z - axis_x * current_z.dot(axis_x)
		var tan_proj := tangent - axis_x * tangent.dot(axis_x)
		if !z_proj.is_zero_approx() and !tan_proj.is_zero_approx():
			z_proj = z_proj.normalized()
			tan_proj = tan_proj.normalized()
			var adjust_angle := z_proj.angle_to(tan_proj)
			if z_proj.cross(tan_proj).dot(axis_x) < 0.0:
				adjust_angle = -adjust_angle
			basis = Basis(Quaternion(axis_x, adjust_angle)) * basis
	return Transform3D(basis, pos)

func _sample_spiral_transform(t : float) -> Transform3D:
	var raw_start := _canonical_spiral_transform(0.0)
	var correction := axis_transform * raw_start.affine_inverse()
	return correction * _canonical_spiral_transform(t)

func refresh_spiral_road() -> void:
	_ensure_spiral_curves()
	if !native_curve:
		native_curve = ClassDB.instantiate("TrackEditorCurve")
	native_curve.curve_mode = 1
	native_curve.clear_control_points()
	axis_transform = axis_marker.global_transform if axis_marker else axis_transform
	var prev := Vector3.ZERO
	segment_length = 0.0
	for i in BAKE_SUBDIVISIONS + 1:
		var t := float(i) / float(BAKE_SUBDIVISIONS)
		var transform := _sample_spiral_transform(t)
		var scale := Vector3(
			_sample_curve(scale_x_curve, t, 25.0),
			_sample_curve(scale_y_curve, t, 25.0),
			1.0)
		native_curve.insert_control_point(i, t, transform.origin, transform.basis.orthonormalized(), scale, 0.0, 0.0, 3, 1.0, 3, 1.0, 3, 1.0)
		if i > 0:
			segment_length += prev.distance_to(transform.origin)
		prev = transform.origin

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
