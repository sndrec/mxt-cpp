class_name TrackRoot extends Node3D

const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")

@export var gen_checkpoints : bool = false:
	set(new_bool):
		gen_checkpoints = false
		if new_bool:
			_generate_checkpoints()

@export var checkpoints : Array[Checkpoint] = []
@export var track_name : String = "New Track"
@export var track_description : String = ""
@export var track_difficulty : int = 1

func _generate_checkpoints() -> void:
	checkpoints.clear()
	for ch in get_children().size():
		var child := get_child(ch) as RoadPath
		if child == null:
			continue
		var num_checks := child.num_checkpoints
		for i in num_checks + 1:
			var ty := (1.0 / (num_checks + 1)) * i
			var ty2 := ty + (1.0 / (num_checks + 1))
			var tf_1 := child.get_root_transform(ty)
			var tf_2 := child.get_root_transform(ty2)
			var cp_x_rad_1 := tf_1.basis.x.length()
			var cp_x_rad_2 := tf_2.basis.x.length()
			var cp_y_rad_1 := tf_1.basis.y.length()
			var cp_y_rad_2 := tf_2.basis.y.length()
			var cp_dist := (tf_2.origin - tf_1.origin).length()
			var new_checkpoint = Checkpoint.new(tf_1.origin, tf_2.origin, tf_1.basis.orthonormalized(), tf_2.basis.orthonormalized(), cp_x_rad_1, cp_x_rad_2, cp_y_rad_1, cp_y_rad_2, ty, ty2, cp_dist, ch)
			checkpoints.append(new_checkpoint)

func get_road_segments() -> Array[RoadPath]:
	var out : Array[RoadPath] = []
	for child in get_children():
		if child is RoadPath:
			out.append(child)
	return out

func save_edit_source(path : String) -> Error:
	var scene := PackedScene.new()
	var pack_err := scene.pack(self)
	if pack_err != OK:
		return pack_err
	return ResourceSaver.save(scene, path)

func export_mxt_track(path : String) -> Error:
	var segments := get_road_segments()
	if segments.is_empty():
		return ERR_DOES_NOT_EXIST
	_generate_checkpoints()
	var cp_ranges := _build_checkpoint_ranges(segments)
	var buf := StreamPeerBufferExtension.new()
	buf.big_endian = false
	var version := "v0.5".to_ascii_buffer()
	buf.put_u32(20)
	buf.put_data(version)
	buf.put_u32(checkpoints.size())
	buf.put_u32(segments.size())
	buf.put_u32(0)
	for i in checkpoints.size():
		buf.put_checkpoint_with_neighbors(checkpoints[i], _checkpoint_neighbors(i, cp_ranges))
	for i in segments.size():
		var segment := segments[i]
		if !(segment is RoadPathBezier):
			return ERR_INVALID_DATA
		segment.mxt_segment_index = i
		buf.put_u32(i)
		var road_type := _road_type_for_shape(segment.road_shape)
		buf.put_u32(road_type)
		_write_shape_prefix(buf, segment.road_shape, road_type)
		buf.put_u32(segment.road_shape.modulation_table.size())
		for mod in segment.road_shape.modulation_table:
			buf.put_track_editor_curve(mod.modulation_effect)
			buf.put_track_editor_curve(mod.modulation_height)
		buf.put_u32(segment.road_shape.embed_table.size())
		for embed in segment.road_shape.embed_table:
			buf.put_float(embed.road_start)
			buf.put_float(embed.road_end)
			buf.put_u32(int(embed.embed_type))
			buf.put_track_editor_curve(embed.left_boundary)
			buf.put_track_editor_curve(embed.right_boundary)
		buf.put_baked_curve_matrix(segment.build_export_curve_matrix())
		buf.put_float(segment.left_rail_height)
		buf.put_float(segment.right_rail_height)
		buf.put_float(clampf(segment.left_rail_start, 0.0, 1.0))
		buf.put_float(clampf(segment.left_rail_end, 0.0, 1.0))
		buf.put_float(clampf(segment.right_rail_start, 0.0, 1.0))
		buf.put_float(clampf(segment.right_rail_end, 0.0, 1.0))
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_buffer(buf.data_array)
	return OK

func _build_checkpoint_ranges(segments : Array[RoadPath]) -> Array[Vector2i]:
	var ranges : Array[Vector2i] = []
	var start := 0
	for segment in segments:
		var count := segment.num_checkpoints + 1
		ranges.append(Vector2i(start, count))
		start += count
	return ranges

func _checkpoint_neighbors(index : int, ranges : Array[Vector2i]) -> Array[int]:
	var segment_index := 0
	for i in ranges.size():
		var range := ranges[i]
		if index >= range.x and index < range.x + range.y:
			segment_index = i
			break
	var range := ranges[segment_index]
	var local_index := index - range.x
	var out : Array[int] = []
	if local_index > 0:
		out.append(index - 1)
	else:
		var prev_segment := (segment_index - 1 + ranges.size()) % ranges.size()
		var prev_range := ranges[prev_segment]
		out.append(prev_range.x + prev_range.y - 1)
	if local_index < range.y - 1:
		out.append(index + 1)
	else:
		var next_segment := (segment_index + 1) % ranges.size()
		out.append(ranges[next_segment].x)
	return out

func _road_type_for_shape(shape : RoadShape) -> int:
	if shape is RoadShapeCylinder:
		return 1
	if shape is RoadShapeCylinderOpen:
		return 2
	if shape is RoadShapePipe:
		return 3
	if shape is RoadShapePipeOpen:
		return 4
	if shape is RoadShapeRoundedSquareOpenScript:
		return 6
	if shape is RoadShapeRoundedSquareScript:
		return 5
	return 0

func _write_shape_prefix(buf : StreamPeerBufferExtension, shape : RoadShape, road_type : int) -> void:
	if road_type == 5 or road_type == 6:
		buf.put_track_editor_curve(shape.width)
		buf.put_track_editor_curve(shape.height)
		buf.put_track_editor_curve(shape.radius)
	if road_type == 2 or road_type == 4 or road_type == 6:
		buf.put_track_editor_curve(shape.openness)
	if road_type == 6:
		buf.put_track_editor_curve(shape.open_rotation)
