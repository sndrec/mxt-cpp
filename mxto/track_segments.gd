class_name TrackRoot extends Node3D

const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")
const TrackTriggerScript := preload("res://core/track_trigger.gd")

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

func get_track_triggers() -> Array[Node3D]:
	var out : Array[Node3D] = []
	for child in get_children():
		if child.get_script() == TrackTriggerScript:
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
	var topology := _build_segment_topology(segments)
	var trigger_exports := _build_trigger_exports(segments, cp_ranges)
	var buf := StreamPeerBufferExtension.new()
	buf.big_endian = false
	var version := "v0.5".to_ascii_buffer()
	buf.put_u32(20)
	buf.put_data(version)
	buf.put_u32(checkpoints.size())
	buf.put_u32(segments.size())
	buf.put_u32(trigger_exports.size())
	for i in checkpoints.size():
		buf.put_checkpoint_with_neighbors(checkpoints[i], _checkpoint_neighbors(i, cp_ranges, topology))
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
	for trigger_export in trigger_exports:
		_write_trigger_export(buf, trigger_export)
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_buffer(buf.data_array)
	return OK

func _build_trigger_exports(segments : Array[RoadPath], cp_ranges : Array[Vector2i]) -> Array[Dictionary]:
	var out : Array[Dictionary] = []
	for trigger in get_track_triggers():
		var segment := trigger.get_target_segment() as RoadPath
		var segment_index := segments.find(segment)
		if segment_index < 0:
			continue
		var cp_range := cp_ranges[segment_index]
		var cp_count := maxi(1, cp_range.y)
		var surface_t : Vector2 = trigger.get("surface_t")
		var local_cp := mini(int(clampf(surface_t.y, 0.0, 1.0) * float(cp_count)), cp_count - 1)
		var checkpoint_index := cp_range.x + local_cp
		trigger.set("mxt_segment_index", segment_index)
		trigger.set("mxt_checkpoint_index", checkpoint_index)
		out.append({
			"trigger": trigger,
			"type": int(trigger.get("trigger_type")),
			"segment_index": segment_index,
			"checkpoint_index": checkpoint_index,
			"extents": trigger.call("trigger_extents"),
		})
	return out

func _write_trigger_export(buf : StreamPeerBufferExtension, trigger_export : Dictionary) -> void:
	var trigger := trigger_export["trigger"] as Node3D
	buf.put_u32(trigger_export["type"])
	buf.put_u32(trigger_export["segment_index"])
	buf.put_u32(trigger_export["checkpoint_index"])
	buf.put_transform(trigger.global_transform.affine_inverse())
	buf.put_vector3(trigger_export["extents"])

func _build_checkpoint_ranges(segments : Array[RoadPath]) -> Array[Vector2i]:
	var ranges : Array[Vector2i] = []
	var start := 0
	for segment in segments:
		var count := segment.num_checkpoints + 1
		ranges.append(Vector2i(start, count))
		start += count
	return ranges

func _segment_index_for_path(segment_path : NodePath, segments : Array[RoadPath]) -> int:
	if segment_path.is_empty():
		return -1
	var node := get_node_or_null(segment_path)
	if !(node is RoadPath):
		return -1
	return segments.find(node)

func _append_unique_segment_index(out : Array, segment_index : int) -> void:
	if segment_index < 0 or out.has(segment_index):
		return
	out.append(segment_index)

func _build_segment_topology(segments : Array[RoadPath]) -> Dictionary:
	var previous_by_segment : Array = []
	var next_by_segment : Array = []
	for _segment in segments:
		previous_by_segment.append([])
		next_by_segment.append([])
	for i in segments.size():
		var segment := segments[i]
		for previous_path in segment.previous_segment_paths:
			var previous_index := _segment_index_for_path(previous_path, segments)
			if previous_index == i:
				continue
			_append_unique_segment_index(previous_by_segment[i], previous_index)
			if previous_index >= 0:
				_append_unique_segment_index(next_by_segment[previous_index], i)
		for next_path in segment.next_segment_paths:
			var next_index := _segment_index_for_path(next_path, segments)
			if next_index == i:
				continue
			_append_unique_segment_index(next_by_segment[i], next_index)
			if next_index >= 0:
				_append_unique_segment_index(previous_by_segment[next_index], i)
	return {
		"previous": previous_by_segment,
		"next": next_by_segment,
	}

func _append_unique_checkpoint(out : Array[int], checkpoint_index : int) -> void:
	if checkpoint_index < 0 or out.has(checkpoint_index):
		return
	out.append(checkpoint_index)

func _checkpoint_neighbors(index : int, ranges : Array[Vector2i], topology : Dictionary) -> Array[int]:
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
		_append_unique_checkpoint(out, index - 1)
	else:
		var previous_segments : Array = topology["previous"][segment_index]
		if previous_segments.is_empty():
			previous_segments = [(segment_index - 1 + ranges.size()) % ranges.size()]
		for previous_segment in previous_segments:
			var prev_range := ranges[int(previous_segment)]
			_append_unique_checkpoint(out, prev_range.x + prev_range.y - 1)
	if local_index < range.y - 1:
		_append_unique_checkpoint(out, index + 1)
	else:
		var next_segments : Array = topology["next"][segment_index]
		if next_segments.is_empty():
			next_segments = [(segment_index + 1) % ranges.size()]
		for next_segment in next_segments:
			_append_unique_checkpoint(out, ranges[int(next_segment)].x)
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
