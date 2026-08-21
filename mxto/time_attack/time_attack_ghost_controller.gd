class_name TimeAttackGhostController extends Node

const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const MAX_GHOSTS := 4
const MAX_ENCODED_INPUT_BYTES := 8
const FADE_SECONDS := 0.75
const BASE_TRANSPARENCY := 0.48
const TINT_STRENGTH := 0.32
const NAME_LABEL_HEIGHT := 4.4
const SLOT_COLORS: Array[Color] = [
	Color(0.30, 0.90, 1.0),
	Color(1.0, 0.36, 0.86),
	Color(1.0, 0.82, 0.25),
	Color(0.38, 1.0, 0.48),
]

var game_manager: GameManager
var prepared_track_index := -1
var prepared_slots: Array = []
var runtime_slots: Array = []
var last_error := ""
var render_manager: CarRenderManager
var name_labels: Array[Label] = []
var prepare_total_us := 0
var instantiate_total_us := 0
var instantiate_max_us := 0
var main_tick_count := 0
var main_tick_total_us := 0
var main_tick_max_us := 0
var ghost_tick_total_us := 0
var ghost_tick_max_us := 0
var render_frame_count := 0
var render_total_us := 0
var render_max_us := 0
var cache_stats_at_prepare: Dictionary = {}
var profile_session_active := false


func initialize(manager: GameManager) -> void:
	game_manager = manager
	set_process(false)


func _exit_tree() -> void:
	teardown_runtime()


func prepare(descriptors: Array, track_index: int) -> Dictionary:
	teardown_runtime()
	prepared_slots.clear()
	prepared_track_index = -1
	last_error = ""
	prepare_total_us = 0
	cache_stats_at_prepare = game_manager.leaderboard_replay_cache.stats() if game_manager != null and game_manager.leaderboard_replay_cache != null else {}
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
		var prepare_start_us := Time.get_ticks_usec()
		var result := _prepare_descriptor(descriptor_value as Dictionary, track_index)
		var prepare_us := Time.get_ticks_usec() - prepare_start_us
		prepare_total_us += prepare_us
		if !bool(result.get("success", false)):
			prepared_slots.clear()
			return result
		var prepared: Dictionary = result.get("prepared", {})
		prepared["prepare_us"] = prepare_us
		prepared_slots.append(prepared)
	prepared_track_index = track_index
	return {"success": true, "ghost_count": prepared_slots.size()}


func start_race(track_index: int) -> Dictionary:
	teardown_runtime()
	last_error = ""
	_reset_runtime_profile()
	if prepared_track_index != track_index:
		return _runtime_failure("ghost_track_changed", "The prepared ghosts belong to a different track.")
	profile_session_active = true
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
		var instantiate_start_us := Time.get_ticks_usec()
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
		runtime["fade_elapsed"] = 0.0
		runtime["tick_count"] = 0
		runtime["tick_total_us"] = 0
		runtime["tick_max_us"] = 0
		var instantiate_us := Time.get_ticks_usec() - instantiate_start_us
		runtime["instantiate_us"] = instantiate_us
		instantiate_total_us += instantiate_us
		instantiate_max_us = maxi(instantiate_max_us, instantiate_us)
		runtime_slots.append(runtime)
	_configure_presentation()
	set_process(true)
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
		var tick_start_us := Time.get_ticks_usec()
		var sim: GameSim = slot.get("sim", null)
		if sim == null or !sim.tick_singleplayer_indexed_input(
				int(slot.get("racer_id", -1)),
				slot.get("input_bytes", PackedByteArray()),
				slot.get("frame_offsets", PackedInt32Array()),
				frame_index):
			_fail_runtime_slot(slot, "Ghost input stream failed at tick %d." % frame_index)
			continue
		sim.update_render_snapshots()
		sim.discard_race_events()
		var tick_us := Time.get_ticks_usec() - tick_start_us
		slot["tick_count"] = int(slot.get("tick_count", 0)) + 1
		slot["tick_total_us"] = int(slot.get("tick_total_us", 0)) + tick_us
		slot["tick_max_us"] = maxi(int(slot.get("tick_max_us", 0)), tick_us)
		ghost_tick_total_us += tick_us
		ghost_tick_max_us = maxi(ghost_tick_max_us, tick_us)
		slot["frame_index"] = frame_index + 1
		if frame_index + 1 >= frame_count:
			slot["state"] = "finished"


func teardown_runtime() -> void:
	if profile_session_active:
		_emit_profile_summary("teardown")
	profile_session_active = false
	set_process(false)
	_clear_presentation()
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


func _process(delta: float) -> void:
	if render_manager == null or runtime_slots.is_empty():
		return
	var render_start_us := Time.get_ticks_usec()
	render_manager.begin_manual_submit()
	var camera := get_viewport().get_camera_3d()
	var hud_hidden := game_manager != null and game_manager.debug_runtime_controller.disable_hud
	for index in range(runtime_slots.size()):
		var slot: Dictionary = runtime_slots[index]
		var state := String(slot.get("state", ""))
		if state == "finished":
			state = "fading"
			slot["state"] = state
			slot["fade_elapsed"] = 0.0
		if state == "fading":
			slot["fade_elapsed"] = float(slot.get("fade_elapsed", 0.0)) + delta
			if float(slot["fade_elapsed"]) >= FADE_SECONDS:
				slot["state"] = "hidden"
				_hide_label(index)
				continue
		elif state != "active":
			_hide_label(index)
			continue
		if int(slot.get("frame_index", 0)) <= 0:
			_hide_label(index)
			continue
		var sim: GameSim = slot.get("sim", null)
		if sim == null:
			_hide_label(index)
			continue
		var fade_ratio := clampf(float(slot.get("fade_elapsed", 0.0)) / FADE_SECONDS, 0.0, 1.0) if state == "fading" else 0.0
		var transparency := lerpf(BASE_TRANSPARENCY, 1.0, fade_ratio)
		var slot_index := clampi(int(slot.get("slot_index", index)), 0, SLOT_COLORS.size() - 1)
		var color := SLOT_COLORS[slot_index]
		var transform := sim.get_player_render_transform(int(slot.get("racer_id", -1)))
		render_manager.set_manual_car_transparency(index, transparency)
		render_manager.submit_manual_car(
			index,
			transform,
			Color(color.r * TINT_STRENGTH, color.g * TINT_STRENGTH, color.b * TINT_STRENGTH, 1.0),
			Vector3.ZERO,
			Color.TRANSPARENT,
			0.0,
			false,
			false,
			false,
			false)
		_update_label(index, transform, 1.0 - fade_ratio, camera, hud_hidden)
	var render_us := Time.get_ticks_usec() - render_start_us
	render_frame_count += 1
	render_total_us += render_us
	render_max_us = maxi(render_max_us, render_us)


func record_main_tick(tick_us: int) -> void:
	main_tick_count += 1
	main_tick_total_us += tick_us
	main_tick_max_us = maxi(main_tick_max_us, tick_us)


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


func capture_practice_rolling_state(main_saved_tick: int) -> Dictionary:
	var slots: Array = []
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		var sim: GameSim = slot.get("sim", null)
		if sim == null:
			return {"success": false}
		var frame_index := int(slot.get("frame_index", 0))
		var native_saved_tick := frame_index - 1
		if native_saved_tick < 0 or !sim.has_saved_state(native_saved_tick):
			return {"success": false}
		if String(slot.get("state", "")) == "active" and native_saved_tick != main_saved_tick:
			return {"success": false}
		slots.append(_practice_runtime_record(slot, native_saved_tick))
	return {
		"success": true,
		"main_saved_tick": main_saved_tick,
		"slots": slots,
	}


func restore_practice_rolling_state(state: Dictionary) -> bool:
	if !can_restore_practice_rolling_state(state):
		return false
	var slots: Array = state.get("slots", [])
	for index in range(slots.size()):
		var runtime: Dictionary = runtime_slots[index]
		var record: Dictionary = slots[index]
		var sim: GameSim = runtime.get("sim", null)
		if !sim.load_state(int(record.get("native_saved_tick", -1))):
			return false
		sim.discard_race_events()
		sim.snap_render_after_state_load()
		_restore_practice_runtime_record(runtime, record)
	return true


func can_restore_practice_rolling_state(state: Dictionary) -> bool:
	var slots: Array = state.get("slots", [])
	if slots.size() != runtime_slots.size():
		return false
	for index in range(slots.size()):
		var runtime: Dictionary = runtime_slots[index]
		var record: Dictionary = slots[index]
		var sim: GameSim = runtime.get("sim", null)
		var native_saved_tick := int(record.get("native_saved_tick", -1))
		if sim == null \
				or int(runtime.get("racer_id", -1)) != int(record.get("racer_id", -2)) \
				or native_saved_tick < 0 or !sim.has_saved_state(native_saved_tick):
			return false
	return true


func capture_practice_full_state() -> Dictionary:
	var slots: Array = []
	var total_bytes := 0
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		var sim: GameSim = slot.get("sim", null)
		if sim == null:
			return {"success": false}
		var native_tick := int(slot.get("frame_index", 0))
		var state_bytes := sim.get_full_state_data(native_tick)
		if state_bytes.is_empty():
			return {"success": false}
		var record: Dictionary = _practice_runtime_record(slot, native_tick)
		record["full_state"] = state_bytes
		total_bytes += state_bytes.size()
		slots.append(record)
	return {
		"success": true,
		"slots": slots,
		"total_bytes": total_bytes,
	}


func restore_practice_full_state(state: Dictionary) -> bool:
	if !can_restore_practice_full_state(state):
		return false
	var slots: Array = state.get("slots", [])
	for index in range(slots.size()):
		var runtime: Dictionary = runtime_slots[index]
		var record: Dictionary = slots[index]
		var sim: GameSim = runtime.get("sim", null)
		if !sim.load_full_state_data(int(record.get("native_saved_tick", -1)), record.get("full_state", PackedByteArray())):
			return false
		sim.discard_race_events()
		sim.snap_render_after_state_load()
		_restore_practice_runtime_record(runtime, record)
	return true


func can_restore_practice_full_state(state: Dictionary) -> bool:
	var slots: Array = state.get("slots", [])
	if slots.size() != runtime_slots.size():
		return false
	for index in range(slots.size()):
		var runtime: Dictionary = runtime_slots[index]
		var record: Dictionary = slots[index]
		var sim: GameSim = runtime.get("sim", null)
		var state_bytes: PackedByteArray = record.get("full_state", PackedByteArray())
		if sim == null \
				or int(runtime.get("racer_id", -1)) != int(record.get("racer_id", -2)) \
				or state_bytes.is_empty():
			return false
	return true


func _practice_runtime_record(slot: Dictionary, native_saved_tick: int) -> Dictionary:
	return {
		"racer_id": int(slot.get("racer_id", -1)),
		"native_saved_tick": native_saved_tick,
		"frame_index": int(slot.get("frame_index", 0)),
		"state": String(slot.get("state", "active")),
		"fade_elapsed": float(slot.get("fade_elapsed", 0.0)),
		"tick_count": int(slot.get("tick_count", 0)),
		"tick_total_us": int(slot.get("tick_total_us", 0)),
		"tick_max_us": int(slot.get("tick_max_us", 0)),
	}


func _restore_practice_runtime_record(runtime: Dictionary, record: Dictionary) -> void:
	if int(runtime.get("racer_id", -1)) != int(record.get("racer_id", -2)):
		return
	runtime["frame_index"] = int(record.get("frame_index", 0))
	runtime["state"] = String(record.get("state", "active"))
	runtime["fade_elapsed"] = float(record.get("fade_elapsed", 0.0))
	runtime["tick_count"] = int(record.get("tick_count", 0))
	runtime["tick_total_us"] = int(record.get("tick_total_us", 0))
	runtime["tick_max_us"] = int(record.get("tick_max_us", 0))


func memory_usage_stats() -> Dictionary:
	var slots: Array = []
	var aggregate_native_bytes := 0
	var aggregate_level_bytes := 0
	var aggregate_state_bytes := 0
	var aggregate_rollback_bytes := 0
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		var sim: GameSim = slot.get("sim", null)
		var stats: Dictionary = sim.get_memory_usage_stats() if sim != null else {}
		aggregate_native_bytes += int(stats.get("tracked_native_bytes", 0))
		aggregate_level_bytes += int(stats.get("level_heap_capacity_bytes", 0))
		aggregate_state_bytes += int(stats.get("gamestate_heap_capacity_bytes", 0))
		aggregate_rollback_bytes += int(stats.get("rollback_buffer_bytes", 0))
		slots.append(stats)
	return {
		"ghost_count": runtime_slots.size(),
		"aggregate_tracked_native_bytes": aggregate_native_bytes,
		"aggregate_level_heap_bytes": aggregate_level_bytes,
		"aggregate_gamestate_heap_bytes": aggregate_state_bytes,
		"aggregate_rollback_bytes": aggregate_rollback_bytes,
		"render_archetypes": render_manager.archetypes.size() if render_manager != null else 0,
		"slots": slots,
	}


func _reset_runtime_profile() -> void:
	instantiate_total_us = 0
	instantiate_max_us = 0
	main_tick_count = 0
	main_tick_total_us = 0
	main_tick_max_us = 0
	ghost_tick_total_us = 0
	ghost_tick_max_us = 0
	render_frame_count = 0
	render_total_us = 0
	render_max_us = 0


func _emit_profile_summary(event_name: String) -> void:
	var memory := memory_usage_stats()
	var active := 0
	var fading := 0
	var failed := 0
	var slot_profiles: Array = []
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		var state := String(slot.get("state", ""))
		active += 1 if state == "active" else 0
		fading += 1 if state == "fading" or state == "finished" else 0
		failed += 1 if state == "failed" else 0
		var tick_count := int(slot.get("tick_count", 0))
		slot_profiles.append({
			"slot": int(slot.get("slot_index", 0)),
			"prepare_us": int(slot.get("prepare_us", 0)),
			"instantiate_us": int(slot.get("instantiate_us", 0)),
			"ticks": tick_count,
			"tick_avg_us": int(slot.get("tick_total_us", 0)) / maxi(tick_count, 1),
			"tick_max_us": int(slot.get("tick_max_us", 0)),
			"state": state,
		})
	var current_cache_stats := game_manager.leaderboard_replay_cache.stats() if game_manager != null and game_manager.leaderboard_replay_cache != null else {}
	print("MXT_GHOST_PROFILE ", JSON.stringify({
		"event": event_name,
		"ghost_count": runtime_slots.size(),
		"active": active,
		"fading": fading,
		"failed": failed,
		"prepare_total_us": prepare_total_us,
		"instantiate_total_us": instantiate_total_us,
		"instantiate_max_us": instantiate_max_us,
		"main_ticks": main_tick_count,
		"main_tick_avg_us": main_tick_total_us / maxi(main_tick_count, 1),
		"main_tick_max_us": main_tick_max_us,
		"ghost_tick_total_us": ghost_tick_total_us,
		"ghost_tick_avg_us": ghost_tick_total_us / maxi(_total_ghost_ticks(), 1),
		"ghost_tick_max_us": ghost_tick_max_us,
		"render_frames": render_frame_count,
		"render_avg_us": render_total_us / maxi(render_frame_count, 1),
		"render_max_us": render_max_us,
		"cache_hits_total": int(current_cache_stats.get("cache_hits", 0)),
		"downloaded_bytes_total": int(current_cache_stats.get("downloaded_bytes", 0)),
		"cache_hits_since_prepare": int(current_cache_stats.get("cache_hits", 0)) - int(cache_stats_at_prepare.get("cache_hits", 0)),
		"downloaded_bytes_since_prepare": int(current_cache_stats.get("downloaded_bytes", 0)) - int(cache_stats_at_prepare.get("downloaded_bytes", 0)),
		"memory": memory,
		"slots": slot_profiles,
	}))


func _total_ghost_ticks() -> int:
	var total := 0
	for slot_value in runtime_slots:
		if typeof(slot_value) == TYPE_DICTIONARY:
			total += int((slot_value as Dictionary).get("tick_count", 0))
	return total


func _configure_presentation() -> void:
	_clear_presentation()
	if runtime_slots.is_empty() or game_manager == null:
		return
	var definitions: Array = []
	var settings: Array = []
	for slot_value in runtime_slots:
		var slot: Dictionary = slot_value
		definitions.append(slot.get("definition", null))
		settings.append((slot.get("settings", {}) as Dictionary).duplicate(true))
	render_manager = CarRenderManagerClass.new()
	render_manager.name = "TimeAttackGhostRenderManager"
	game_manager.get_node("GameWorld").add_child(render_manager)
	render_manager.configure_manual(definitions, settings, true)
	for index in range(runtime_slots.size()):
		var slot: Dictionary = runtime_slots[index]
		var slot_index := clampi(int(slot.get("slot_index", index)), 0, SLOT_COLORS.size() - 1)
		var label := Label.new()
		label.name = "TimeAttackGhostName%d" % slot_index
		label.text = String(slot.get("persona_name", "Ghost"))
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		label.z_index = 20
		var label_settings := LabelSettings.new()
		label_settings.font_size = 18
		label_settings.font_color = SLOT_COLORS[slot_index]
		label_settings.outline_size = 5
		label_settings.outline_color = Color(0.0, 0.0, 0.0, 0.85)
		label.label_settings = label_settings
		label.size = label.get_combined_minimum_size()
		label.visible = false
		game_manager.race_presentation_controller.add_child(label)
		name_labels.append(label)


func _clear_presentation() -> void:
	for label in name_labels:
		if label != null and is_instance_valid(label):
			label.free()
	name_labels.clear()
	if render_manager != null and is_instance_valid(render_manager):
		if render_manager.get_parent() != null:
			render_manager.get_parent().remove_child(render_manager)
		render_manager.free()
	render_manager = null


func _update_label(index: int, car_transform: Transform3D, alpha: float, camera: Camera3D, hud_hidden: bool) -> void:
	if index < 0 or index >= name_labels.size():
		return
	var label := name_labels[index]
	var label_anchor := car_transform.origin + car_transform.basis.y.normalized() * NAME_LABEL_HEIGHT
	if label == null or camera == null or hud_hidden \
			or camera.is_position_behind(label_anchor) or !camera.is_position_in_frustum(label_anchor):
		label.visible = false
		return
	label.visible = true
	label.modulate = Color(1.0, 1.0, 1.0, clampf(alpha, 0.0, 1.0))
	label.position = camera.unproject_position(label_anchor) - Vector2(label.size.x * 0.5, label.size.y + 8.0)


func _hide_label(index: int) -> void:
	if index >= 0 and index < name_labels.size() and name_labels[index] != null:
		name_labels[index].visible = false


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
