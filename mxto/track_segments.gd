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
@export var first_segment_path : NodePath
@export var track_name : String = "New Track"
@export var track_description : String = ""
@export var track_difficulty : int = 1
@export var fog_distance : float = 2000.0
@export var sky_top_color : Color = Color(0.0, 0.1, 0.25, 1.0)
@export var sky_horizon_color : Color = Color(0.2, 0.25, 0.3, 1.0)
@export var sky_ground_color : Color = Color(0.02, 0.02, 0.02, 1.0)
@export var ground_color_global : Color = Color(0.08, 0.08, 0.08, 1.0)
@export var ground_height : float = 0.0
@export var cloud_color : Color = Color(1.0, 1.0, 1.0, 1.0)
@export var cloud_height : float = 800.0
@export var light_color : Color = Color(1.0, 0.95, 0.9, 1.0)
@export var light_intensity : float = 1.0
@export var ambient_intensity : float = 0.1
@export var ambient_color : Color = Color(0.15, 0.15, 0.18, 1.0)
@export var light_direction : Vector3 = Vector3(0.3, -1.0, 0.4)

func _generate_checkpoints(segments : Array[RoadPath] = []) -> void:
	if segments.is_empty():
		segments = get_road_segments()
	checkpoints.clear()
	for ch in segments.size():
		var child := segments[ch]
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

func get_first_segment() -> RoadPath:
	if first_segment_path.is_empty():
		return null
	var node := get_node_or_null(first_segment_path)
	if node is RoadPath:
		return node
	return null

func set_first_segment(segment : RoadPath) -> void:
	if !segment or segment.get_parent() != self:
		first_segment_path = NodePath()
		return
	first_segment_path = get_path_to(segment)

func _next_segments_for(segment : RoadPath) -> Array[RoadPath]:
	var out : Array[RoadPath] = []
	for next_path in segment.next_segment_paths:
		var next_node := get_node_or_null(next_path)
		if next_node is RoadPath and !out.has(next_node):
			out.append(next_node)
	return out

func get_export_road_segments() -> Array[RoadPath]:
	var all_segments := get_road_segments()
	var first := get_first_segment()
	if !first:
		return all_segments
	var reachable : Array[RoadPath] = []
	var queue : Array[RoadPath] = [first]
	while !queue.is_empty():
		var segment : RoadPath = queue.pop_front()
		if !segment or reachable.has(segment):
			continue
		reachable.append(segment)
		for next_segment in _next_segments_for(segment):
			if !reachable.has(next_segment):
				queue.append(next_segment)

	var indegrees : Array[int] = []
	for _segment in reachable:
		indegrees.append(0)
	for segment in reachable:
		for next_segment in _next_segments_for(segment):
			var next_index := reachable.find(next_segment)
			if next_index >= 0 and next_segment != first:
				indegrees[next_index] += 1

	var ordered : Array[RoadPath] = []
	var sort_queue : Array[RoadPath] = []
	for i in reachable.size():
		if indegrees[i] == 0:
			sort_queue.append(reachable[i])
	if sort_queue.has(first):
		sort_queue.erase(first)
		sort_queue.push_front(first)

	var seen : Array[RoadPath] = []
	while !sort_queue.is_empty():
		var segment : RoadPath = sort_queue.pop_front()
		if seen.has(segment):
			continue
		seen.append(segment)
		ordered.append(segment)
		for next_segment in _next_segments_for(segment):
			var next_index := reachable.find(next_segment)
			if next_index >= 0 and next_segment != first:
				indegrees[next_index] -= 1
				if indegrees[next_index] == 0:
					sort_queue.append(next_segment)
	for segment in reachable:
		if !seen.has(segment):
			ordered.append(segment)
	for segment in all_segments:
		if !ordered.has(segment):
			ordered.append(segment)
	return ordered

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
	var segments := get_export_road_segments()
	if segments.is_empty():
		return ERR_DOES_NOT_EXIST
	_generate_checkpoints(segments)
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
	var metadata_err := export_track_metadata(path.get_basename() + ".json", segments)
	if metadata_err != OK:
		return metadata_err
	return OK

func export_track_metadata(path : String, segments : Array[RoadPath] = []) -> Error:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(JSON.stringify(_track_metadata(segments), "\t"))
	return OK

func _color3(color : Color) -> Array[float]:
	return [color.r, color.g, color.b]

func _segment_metadata(segments : Array[RoadPath]) -> Array[Dictionary]:
	var out : Array[Dictionary] = []
	for i in segments.size():
		var segment := segments[i]
		out.append({
			"seg_index": i,
			"name": segment.name,
			"ground_color": _color3(segment.ground_color),
			"rail_color": _color3(segment.rail_color),
			"rail_start_left": segment.left_rail_start,
			"rail_end_left": segment.left_rail_end,
			"rail_start_right": segment.right_rail_start,
			"rail_end_right": segment.right_rail_end,
		})
	return out

func _track_metadata(segments : Array[RoadPath] = []) -> Dictionary:
	if segments.is_empty():
		segments = get_export_road_segments()
	return {
		"name": track_name,
		"description": track_description,
		"difficulty": track_difficulty,
		"fog_distance": fog_distance,
		"sky_top_color": _color3(sky_top_color),
		"sky_horizon_color": _color3(sky_horizon_color),
		"sky_ground_color": _color3(sky_ground_color),
		"ground_color": _color3(ground_color_global),
		"ground_height": ground_height,
		"cloud_color": _color3(cloud_color),
		"cloud_height": cloud_height,
		"light_color": _color3(light_color),
		"light_intensity": light_intensity,
		"ambient_intensity": ambient_intensity,
		"ambient_color": _color3(ambient_color),
		"light_direction": [light_direction.x, light_direction.y, light_direction.z],
		"segments": _segment_metadata(segments),
	}

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
