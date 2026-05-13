class_name CurveCrossSection extends ColorRect

@onready var smooth_curve: Line2D = $SmoothCurve
@onready var poly_curve: Line2D = $PolyCurve

var track_editor_dock_2 : TrackEditorDock2
var cached_poly_flat_points := PackedVector2Array()
var cached_poly_closest := 0
var cached_poly_valid := false

func get_track_editor_dock_2() -> TrackEditorDock2:
	if track_editor_dock_2:
		return track_editor_dock_2
	var node := get_parent()
	while node:
		if node is TrackEditorDock2:
			track_editor_dock_2 = node
			return track_editor_dock_2
		node = node.get_parent()
	return track_editor_dock_2

func get_current_path() -> RoadPath:
	var dock: TrackEditorDock2 = get_track_editor_dock_2()
	if !dock:
		return null
	return dock.current_path

func get_cross_section_value() -> float:
	var dock: TrackEditorDock2 = get_track_editor_dock_2()
	if !dock or !dock.track_cross_section_slider:
		return 0.0
	return dock.track_cross_section_slider.value

func _local_cross_section_points(cp : RoadPath, t_values : PackedVector2Array) -> PackedVector3Array:
	return cp.get_surface_local_positions(t_values)

func _flat_cross_section_point(local_point : Vector3, half_width : float) -> Vector2:
	var flat_point := Vector2(local_point.x, local_point.y * -1) * half_width
	flat_point *= Vector2(-1, 1)
	flat_point *= poly_curve.scale
	flat_point += Vector2(half_width, half_width)
	return flat_point

func _poly_flat_points(cp : RoadPath) -> PackedVector2Array:
	var width := 358.0
	var half_width := width * 0.5
	var t_values := PackedVector2Array()
	for i in cp.horizontal_road_mesh_segments.size():
		t_values.append(Vector2(cp.horizontal_road_mesh_segments[i] * 2.0 - 1.0, get_cross_section_value()))
	var local_points : PackedVector3Array = _local_cross_section_points(cp, t_values)
	var out := PackedVector2Array()
	for point in local_points:
		out.append(_flat_cross_section_point(point, half_width))
	return out

func _closest_poly_point_from_points(flat_points : PackedVector2Array, in_pos : Vector2) -> int:
	var closest_dist := 100000.0
	var closest_point := 0
	for i in flat_points.size():
		var dist := flat_points[i].distance_to(in_pos)
		if dist < closest_dist:
			closest_dist = dist
			closest_point = i
	return closest_point

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	update_track_cross_sections()
	var dock: TrackEditorDock2 = get_track_editor_dock_2()
	if dock and !dock.update_track.is_connected(update_track_cross_sections):
		dock.update_track.connect(update_track_cross_sections)

func find_closest_poly_point(in_pos : Vector2) -> int:
	var cp := get_current_path()
	if !cp:
		return 0
	cached_poly_flat_points = _poly_flat_points(cp)
	cached_poly_closest = _closest_poly_point_from_points(cached_poly_flat_points, in_pos)
	cached_poly_valid = true
	return cached_poly_closest

func _draw():
	var cp := get_current_path()
	if !cp:
		return
	if !is_visible_in_tree():
		return
	var mouse_pos := get_local_mouse_position()
	if !cached_poly_valid:
		cached_poly_flat_points = _poly_flat_points(cp)
		cached_poly_closest = _closest_poly_point_from_points(cached_poly_flat_points, mouse_pos)
		cached_poly_valid = true
	for i in cached_poly_flat_points.size():
		var flat_point := cached_poly_flat_points[i]
		var use_color := Color.BURLYWOOD
		if i == cached_poly_closest:
			use_color = Color.RED
		draw_circle(flat_point, 4.0, use_color, true, -1.0, true)
	

func _process(_delta: float) -> void:
	if !is_visible_in_tree():
		return
	var cp := get_current_path()
	if !cp:
		return
	var mouse_pos := get_local_mouse_position()
	var previous_closest := cached_poly_closest
	var closest := find_closest_poly_point(mouse_pos)
	var oob := mouse_pos.x < 0.0 or mouse_pos.y < 0.0 or mouse_pos.x > size.x or mouse_pos.y > size.y
	if !oob and closest != previous_closest:
		queue_redraw()

func update_track_cross_sections() -> void:
	cached_poly_valid = false
	var use_val = get_cross_section_value()
	smooth_curve.clear_points()
	poly_curve.clear_points()
	var width := 358.0
	var half_width := width * 0.5
	var largest := 0.0
	smooth_curve.position = Vector2(half_width, half_width)
	poly_curve.position = Vector2(half_width, half_width)
	var cp := get_current_path()
	if cp is RoadPath:
		var t_values := PackedVector2Array()
		for i in width:
			t_values.append(Vector2((i / (width - 1) * 2.0) - 1.0, use_val))
		for i in cp.horizontal_road_mesh_segments.size():
			t_values.append(Vector2(cp.horizontal_road_mesh_segments[i] * 2.0 - 1.0, use_val))
		var local_points : PackedVector3Array = _local_cross_section_points(cp, t_values)
		for i in width:
			var point := local_points[i]
			if maxf(point.x, point.y) > largest:
				largest = maxf(point.x, point.y)
			var flat_point := Vector2(point.x, point.y * -1) * half_width
			flat_point *= Vector2(-1, 1)
			#print(flat_point)
			smooth_curve.add_point(flat_point)
		for i in cp.horizontal_road_mesh_segments.size():
			var point := local_points[int(width) + i]
			var flat_point := Vector2(point.x, point.y * -1) * half_width
			flat_point *= Vector2(-1, 1)
			#print(flat_point)
			poly_curve.add_point(flat_point)
		smooth_curve.scale = Vector2.ONE / largest
		poly_curve.scale = Vector2.ONE / largest
		smooth_curve.width = 2.0 * largest
		poly_curve.width = 2.0 * largest
	queue_redraw()
