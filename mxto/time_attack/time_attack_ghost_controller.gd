class_name TimeAttackGhostController extends Node

const MAX_GHOSTS := 4
const MAX_ENCODED_INPUT_BYTES := 8

var game_manager: GameManager
var prepared_track_index := -1
var prepared_slots: Array = []
var runtime_slots: Array = []
var last_error := ""


func initialize(manager: GameManager) -> void:
	game_manager = manager


func _exit_tree() -> void:
	teardown_runtime()


func prepare(descriptors: Array, track_index: int) -> Dictionary:
	teardown_runtime()
	prepared_slots.clear()
	prepared_track_index = -1
	last_error = ""
	if descriptors.size() > MAX_GHOSTS:
		return _prepare_failure("ghost_limit_exceeded", "No more than four ghosts may be prepared.")
	if descriptors.is_empty():
		prepared_track_index = track_index
		return {"success": true, "ghost_count": 0}
	if !_track_matches_descriptors(track_index, descriptors):
		return _prepare_failure("ghost_track_mismatch", "The selected ghost replays do not match this track.")
	for descriptor_value in descriptors:
		if typeof(descriptor_value) != TYPE_DICTIONARY:
			return _prepare_failure("invalid_ghost_descriptor", "A selected ghost descriptor is invalid.")
		var result := _prepare_descriptor(descriptor_value as Dictionary, track_index)
		if !bool(result.get("success", false)):
			prepared_slots.clear()
			return result
		prepared_slots.append(result.get("prepared", {}))
	prepared_track_index = track_index
	return {"success": true, "ghost_count": prepared_slots.size()}


func start_race(track_index: int) -> Dictionary:
	teardown_runtime()
	last_error = ""
	if prepared_track_index != track_index:
		return _runtime_failure("ghost_track_changed", "The prepared ghosts belong to a different track.")
	if prepared_slots.is_empty():
		return {"success": true, "ghost_count": 0}
	if game_manager == null or track_index < 0 or track_index >= game_manager.track_content_controller.tracks.size():
		return _runtime_failure("ghost_track_unavailable", "The selected track is unavailable to the ghost simulations.")
	var track: Dictionary = game_manager.track_content_controller.tracks[track_index]
	var track_bytes := FileAccess.get_file_as_bytes(String(track.get("mxt", "")))
	if track_bytes.is_empty():
		return _runtime_failure("ghost_track_unavailable", "The selected track data could not be loaded for the ghosts.")
	for prepared_value in prepared_slots:
		var prepared: Dictionary = prepared_value
		var sim := GameSim.new()
		sim.name = "TimeAttackGhostSim%d" % int(prepared.get("slot_index", runtime_slots.size()))
		add_child(sim)
		sim.set_spawn_seed(int(prepared.get("spawn_seed", 0)))
		var grid_slots := PackedInt32Array([int(prepared.get("start_grid_slot", -1))])
		sim.set_start_grid_slots(grid_slots)
		sim.set_vehicle_restore_enabled(true)
		sim.set_bumpers_enabled(false)
		sim.set_s_boost_enabled(false)
		sim.set_multiplayer_intro_camera_enabled(false)
		var level_buffer := StreamPeerBuffer.new()
		level_buffer.data_array = track_bytes
		sim.instantiate_gamesim(
			level_buffer,
			[prepared.get("car_properties", PackedByteArray())],
			[float(prepared.get("machine_setting", 0.5))])
		sim.set_player_metadata([int(prepared.get("racer_id", -1))], [false])
		sim.set_sim_started(true)
		var runtime := prepared.duplicate(false)
		runtime["sim"] = sim
		runtime["frame_index"] = 0
		runtime["state"] = "active"
		runtime_slots.append(runtime)
	return {"success": true, "ghost_count": runtime_slots.size()}


func tick(live_tick: int) -> void:
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		if String(slot.get("state", "")) != "active":
			continue
		var frame_index := int(slot.get("frame_index", 0))
		var frame_count := int(slot.get("frame_count", 0))
		if frame_index != live_tick:
			_fail_runtime_slot(slot, "Ghost clock mismatch: expected tick %d, slot is at %d." % [live_tick, frame_index])
			continue
		if frame_index >= frame_count:
			slot["state"] = "finished"
			continue
		var sim: GameSim = slot.get("sim", null)
		if sim == null or !sim.tick_singleplayer_indexed_input(
				int(slot.get("racer_id", -1)),
				slot.get("input_bytes", PackedByteArray()),
				slot.get("frame_offsets", PackedInt32Array()),
				frame_index):
			_fail_runtime_slot(slot, "Ghost input stream failed at tick %d." % frame_index)
			continue
		sim.discard_race_events()
		slot["frame_index"] = frame_index + 1
		if frame_index + 1 >= frame_count:
			slot["state"] = "finished"


func teardown_runtime() -> void:
	for slot_value in runtime_slots:
		if typeof(slot_value) != TYPE_DICTIONARY:
			continue
		var sim: GameSim = (slot_value as Dictionary).get("sim", null)
		if sim == null:
			continue
		sim.destroy_gamesim()
		if sim.get_parent() == self:
			remove_child(sim)
		sim.free()
	runtime_slots.clear()


func clear() -> void:
	teardown_runtime()
	prepared_slots.clear()
	prepared_track_index = -1
	last_error = ""


func active_count() -> int:
	var result := 0
	for slot_value in runtime_slots:
		if typeof(slot_value) == TYPE_DICTIONARY and String((slot_value as Dictionary).get("state", "")) == "active":
			result += 1
	return result


func runtime_slot_snapshot() -> Array:
	return runtime_slots.duplicate(false)


func memory_usage_stats() -> Dictionary:
	var slots: Array = []
	var aggregate_bytes := 0
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		var sim: GameSim = slot.get("sim", null)
		var stats: Dictionary = sim.get_memory_usage_stats() if sim != null else {}
		aggregate_bytes += int(stats.get("total_tracked_bytes", 0))
		slots.append(stats)
	return {
		"ghost_count": runtime_slots.size(),
		"aggregate_tracked_bytes": aggregate_bytes,
		"slots": slots,
	}


func _prepare_descriptor(descriptor: Dictionary, track_index: int) -> Dictionary:
	var cache_path := String(descriptor.get("cache_path", ""))
	if cache_path.is_empty() or !FileAccess.file_exists(cache_path):
		return _prepare_failure("ghost_replay_missing", "A selected ghost replay is no longer in the cache.")
	var replay_value = JSON.parse_string(FileAccess.get_file_as_string(cache_path))
	if typeof(replay_value) != TYPE_DICTIONARY:
		return _prepare_failure("ghost_replay_invalid", "A selected ghost replay could not be parsed.")
	var replay: Dictionary = replay_value
	var racer_ids_value = replay.get("racer_ids", [])
	var settings_value = replay.get("settings", [])
	var frames_value = replay.get("frames", [])
	if typeof(racer_ids_value) != TYPE_ARRAY or (racer_ids_value as Array).size() != 1 \
			or typeof(settings_value) != TYPE_ARRAY or (settings_value as Array).size() != 1 \
			or typeof(frames_value) != TYPE_ARRAY or (frames_value as Array).is_empty():
		return _prepare_failure("ghost_replay_invalid", "A selected ghost replay has an invalid single-racer stream.")
	var racer_id := int((racer_ids_value as Array)[0])
	var settings_raw = (settings_value as Array)[0]
	if typeof(settings_raw) != TYPE_DICTIONARY:
		return _prepare_failure("ghost_replay_invalid", "A selected ghost replay has invalid machine settings.")
	var settings: Dictionary = settings_raw
	var definition: CarDefinition = game_manager.vehicle_content_controller.get_definition(String(settings.get("vehicle_content_id", "")))
	if definition == null:
		return _prepare_failure("ghost_vehicle_unavailable", "A selected ghost's exact machine is unavailable.")
	var car_properties := FileAccess.get_file_as_bytes(definition.properties_path)
	if car_properties.is_empty():
		return _prepare_failure("ghost_vehicle_unavailable", "A selected ghost's machine properties could not be loaded.")
	var encoded := _predecode_inputs(frames_value as Array, racer_id)
	if !bool(encoded.get("success", false)):
		return encoded
	var saved_grid_slots_value = replay.get("start_grid_slots", [])
	var start_grid_slot := -1
	if typeof(saved_grid_slots_value) == TYPE_ARRAY and !(saved_grid_slots_value as Array).is_empty():
		start_grid_slot = int((saved_grid_slots_value as Array)[0])
	var validation_value = descriptor.get("validation", {})
	var validation: Dictionary = validation_value if typeof(validation_value) == TYPE_DICTIONARY else {}
	return {
		"success": true,
		"prepared": {
			"slot_index": int(descriptor.get("slot_index", prepared_slots.size())),
			"board_name": String(descriptor.get("board_name", "")),
			"replay_sha256": String(descriptor.get("replay_sha256", "")),
			"persona_name": String(descriptor.get("persona_name", "Ghost")),
			"steam_id": int(descriptor.get("steam_id", 0)),
			"global_rank": int(descriptor.get("global_rank", 0)),
			"score_milliseconds": int(descriptor.get("score_milliseconds", 0)),
			"compatibility_warning": bool(descriptor.get("compatibility_warning", false)),
			"cache_path": cache_path,
			"track_index": track_index,
			"racer_id": racer_id,
			"definition": definition,
			"settings": settings.duplicate(true),
			"machine_setting": float(settings.get("accel_setting", 0.5)),
			"spawn_seed": int(replay.get("spawn_seed", 0)),
			"start_grid_slot": start_grid_slot,
			"input_bytes": encoded.get("input_bytes", PackedByteArray()),
			"frame_offsets": encoded.get("frame_offsets", PackedInt32Array()),
			"frame_count": int(encoded.get("frame_count", 0)),
			"finish_tick": _dictionary_int(replay.get("finish_times", {}), racer_id),
			"car_properties": car_properties,
			"validation": validation.duplicate(true),
		},
	}


func _predecode_inputs(frames: Array, racer_id: int) -> Dictionary:
	var frame_count := frames.size()
	var input_bytes := PackedByteArray()
	input_bytes.resize(frame_count * MAX_ENCODED_INPUT_BYTES)
	var frame_offsets := PackedInt32Array()
	frame_offsets.resize(frame_count + 1)
	var write_offset := 0
	var racer_key := str(racer_id)
	for frame_index in range(frame_count):
		var frame_value = frames[frame_index]
		if typeof(frame_value) != TYPE_DICTIONARY or int((frame_value as Dictionary).get("tick", -1)) != frame_index:
			return _prepare_failure("ghost_noncanonical_frames", "A selected ghost replay has a discontinuous frame stream.")
		var inputs_value = (frame_value as Dictionary).get("inputs", {})
		if typeof(inputs_value) != TYPE_DICTIONARY or !(inputs_value as Dictionary).has(racer_key):
			return _prepare_failure("ghost_noncanonical_frames", "A selected ghost replay is missing racer input.")
		var encoded_value = (inputs_value as Dictionary)[racer_key]
		if typeof(encoded_value) != TYPE_STRING:
			return _prepare_failure("ghost_noncanonical_frames", "A selected ghost replay has invalid racer input.")
		var frame_bytes := Marshalls.base64_to_raw(String(encoded_value))
		if !_encoded_input_is_valid(frame_bytes):
			return _prepare_failure("ghost_noncanonical_frames", "A selected ghost replay contains malformed racer input.")
		frame_offsets[frame_index] = write_offset
		for byte_value in frame_bytes:
			input_bytes[write_offset] = byte_value
			write_offset += 1
	frame_offsets[frame_count] = write_offset
	input_bytes.resize(write_offset)
	return {
		"success": true,
		"input_bytes": input_bytes,
		"frame_offsets": frame_offsets,
		"frame_count": frame_count,
	}


func _encoded_input_is_valid(bytes: PackedByteArray) -> bool:
	if bytes.is_empty() or bytes.size() > MAX_ENCODED_INPUT_BYTES:
		return false
	var mask := int(bytes[0])
	var expected_size := 1
	for bit in [0, 1, 2, 3, 6]:
		expected_size += 1 if (mask & (1 << bit)) != 0 else 0
	return bytes.size() == expected_size


func _track_matches_descriptors(track_index: int, descriptors: Array) -> bool:
	if game_manager == null or track_index < 0:
		return false
	var current_digest := game_manager.track_content_controller.track_gameplay_digest_for_index(track_index)
	for descriptor_value in descriptors:
		if typeof(descriptor_value) != TYPE_DICTIONARY:
			return false
		var trusted_value = (descriptor_value as Dictionary).get("trusted_details", {})
		if typeof(trusted_value) != TYPE_DICTIONARY \
				or String((trusted_value as Dictionary).get("track_gameplay_digest", "")) != current_digest:
			return false
	return true


func _dictionary_int(source_value, key: int) -> int:
	if typeof(source_value) != TYPE_DICTIONARY:
		return -1
	var source: Dictionary = source_value
	if source.has(key):
		return int(source[key])
	return int(source.get(str(key), -1))


func _fail_runtime_slot(slot: Dictionary, message: String) -> void:
	slot["state"] = "failed"
	last_error = message
	push_error(message)


func _prepare_failure(reason: String, message: String) -> Dictionary:
	last_error = message
	return {"success": false, "reason": reason, "message": message}


func _runtime_failure(reason: String, message: String) -> Dictionary:
	last_error = message
	teardown_runtime()
	return {"success": false, "reason": reason, "message": message}
