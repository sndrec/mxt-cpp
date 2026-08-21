class_name PracticeReplayTimeline
extends RefCounted

const CHUNK_FRAME_CAPACITY := 64

var chunks: Dictionary = {}
var retained_heads: Dictionary = {}
var next_chunk_id := 1
var next_head_id := 1
var active_chunk_id := -1
var active_frame_count := 0
var active_input_bytes := 0


func begin() -> void:
	chunks.clear()
	retained_heads.clear()
	next_chunk_id = 1
	next_head_id = 1
	active_chunk_id = -1
	active_frame_count = 0
	active_input_bytes = 0


func append_frame(tick: int, frame_inputs: Dictionary) -> bool:
	if frame_inputs.is_empty():
		return false
	_ensure_writable_chunk()
	var chunk: Dictionary = chunks[active_chunk_id]
	var copied_inputs := {}
	var frame_bytes := 0
	for id_value in frame_inputs.keys():
		if typeof(frame_inputs[id_value]) != TYPE_PACKED_BYTE_ARRAY:
			continue
		var input_bytes: PackedByteArray = frame_inputs[id_value]
		copied_inputs[int(id_value)] = input_bytes.duplicate()
		frame_bytes += input_bytes.size()
	if copied_inputs.is_empty():
		return false
	var frames: Array = chunk["frames"]
	frames.append({"tick": tick, "inputs": copied_inputs})
	chunk["own_input_bytes"] = int(chunk.get("own_input_bytes", 0)) + frame_bytes
	chunk["total_input_bytes"] = int(chunk.get("start_input_bytes", 0)) + int(chunk["own_input_bytes"])
	chunks[active_chunk_id] = chunk
	active_frame_count += 1
	active_input_bytes += frame_bytes
	return true


func frame_count() -> int:
	return active_frame_count


func input_byte_count() -> int:
	return active_input_bytes


func cursor() -> int:
	return active_frame_count


func truncate_to(frame_count_value: int) -> bool:
	var target := clampi(frame_count_value, 0, active_frame_count)
	if target == active_frame_count:
		return true
	if target == 0:
		active_chunk_id = -1
		active_frame_count = 0
		active_input_bytes = 0
		_collect_unreferenced_chunks()
		return true
	var chunk_id := active_chunk_id
	while chunk_id >= 0:
		var chunk: Dictionary = chunks.get(chunk_id, {})
		if chunk.is_empty():
			return false
		var start_frame := int(chunk.get("start_frame", 0))
		var frames: Array = chunk.get("frames", [])
		var end_frame := start_frame + frames.size()
		if target == end_frame:
			active_chunk_id = chunk_id
			active_frame_count = target
			active_input_bytes = int(chunk.get("total_input_bytes", 0))
			_collect_unreferenced_chunks()
			return true
		if target == start_frame:
			active_chunk_id = int(chunk.get("parent", -1))
			active_frame_count = target
			active_input_bytes = int(chunk.get("start_input_bytes", 0))
			_collect_unreferenced_chunks()
			return true
		if target > start_frame and target < end_frame:
			var kept_count := target - start_frame
			var kept_frames := frames.slice(0, kept_count)
			var own_bytes := _frame_array_input_bytes(kept_frames)
			active_chunk_id = _create_chunk(
				int(chunk.get("parent", -1)),
				start_frame,
				int(chunk.get("start_input_bytes", 0)),
				kept_frames,
				own_bytes)
			active_frame_count = target
			active_input_bytes = int(chunk.get("start_input_bytes", 0)) + own_bytes
			_collect_unreferenced_chunks()
			return true
		chunk_id = int(chunk.get("parent", -1))
	return false


func retain_head() -> int:
	if active_chunk_id >= 0:
		var chunk: Dictionary = chunks[active_chunk_id]
		chunk["sealed"] = true
		chunks[active_chunk_id] = chunk
	var head_id := next_head_id
	next_head_id += 1
	retained_heads[head_id] = {
		"chunk_id": active_chunk_id,
		"frame_count": active_frame_count,
		"input_bytes": active_input_bytes,
	}
	return head_id


func release_head(head_id: int) -> void:
	retained_heads.erase(head_id)
	_collect_unreferenced_chunks()


func has_head(head_id: int) -> bool:
	return retained_heads.has(head_id)


func restore_head(head_id: int) -> bool:
	if !retained_heads.has(head_id):
		return false
	var head: Dictionary = retained_heads[head_id]
	var chunk_id := int(head.get("chunk_id", -1))
	if chunk_id >= 0 and !chunks.has(chunk_id):
		return false
	active_chunk_id = chunk_id
	active_frame_count = int(head.get("frame_count", 0))
	active_input_bytes = int(head.get("input_bytes", 0))
	_collect_unreferenced_chunks()
	return true


func flatten_frames() -> Array:
	var chunk_order: Array[int] = []
	var chunk_id := active_chunk_id
	while chunk_id >= 0:
		if !chunks.has(chunk_id):
			return []
		chunk_order.push_front(chunk_id)
		chunk_id = int((chunks[chunk_id] as Dictionary).get("parent", -1))
	var output: Array = []
	output.resize(active_frame_count)
	var output_index := 0
	for id in chunk_order:
		var frames: Array = (chunks[id] as Dictionary).get("frames", [])
		for frame in frames:
			if output_index >= output.size():
				break
			output[output_index] = frame
			output_index += 1
	return output


func diagnostic_snapshot() -> Dictionary:
	return {
		"frames": active_frame_count,
		"input_bytes": active_input_bytes,
		"chunks": chunks.size(),
		"retained_heads": retained_heads.size(),
	}


func _ensure_writable_chunk() -> void:
	if active_chunk_id >= 0:
		var active: Dictionary = chunks[active_chunk_id]
		var frames: Array = active.get("frames", [])
		if !bool(active.get("sealed", false)) and frames.size() < CHUNK_FRAME_CAPACITY:
			return
	active_chunk_id = _create_chunk(active_chunk_id, active_frame_count, active_input_bytes, [], 0)


func _create_chunk(
	parent: int,
	start_frame: int,
	start_input_bytes: int,
	frames: Array,
	own_input_bytes: int
) -> int:
	var chunk_id := next_chunk_id
	next_chunk_id += 1
	chunks[chunk_id] = {
		"parent": parent,
		"start_frame": start_frame,
		"start_input_bytes": start_input_bytes,
		"frames": frames,
		"own_input_bytes": own_input_bytes,
		"total_input_bytes": start_input_bytes + own_input_bytes,
		"sealed": false,
	}
	return chunk_id


func _frame_array_input_bytes(frames: Array) -> int:
	var total := 0
	for frame_value in frames:
		if typeof(frame_value) != TYPE_DICTIONARY:
			continue
		var inputs = (frame_value as Dictionary).get("inputs", {})
		if typeof(inputs) != TYPE_DICTIONARY:
			continue
		for input_value in (inputs as Dictionary).values():
			if typeof(input_value) == TYPE_PACKED_BYTE_ARRAY:
				var input_bytes: PackedByteArray = input_value
				total += input_bytes.size()
	return total


func _collect_unreferenced_chunks() -> void:
	var reachable := {}
	_mark_chunk_chain(active_chunk_id, reachable)
	for head_value in retained_heads.values():
		if typeof(head_value) == TYPE_DICTIONARY:
			_mark_chunk_chain(int((head_value as Dictionary).get("chunk_id", -1)), reachable)
	for chunk_id_value in chunks.keys():
		var chunk_id := int(chunk_id_value)
		if !reachable.has(chunk_id):
			chunks.erase(chunk_id)


func _mark_chunk_chain(chunk_id: int, reachable: Dictionary) -> void:
	var current := chunk_id
	while current >= 0 and chunks.has(current) and !reachable.has(current):
		reachable[current] = true
		current = int((chunks[current] as Dictionary).get("parent", -1))
