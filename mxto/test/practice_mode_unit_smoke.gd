extends SceneTree

const TimelineClass = preload("res://practice/practice_replay_timeline.gd")
const PlayerInputClass = preload("res://player/player_input.gd")
const TRACK_RELATIVE := "../export-bin/track/surface_slide/track.mxt_track"
const CAR_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"
const TELEMETRY_MANUAL_BOOST_ACTIVE := 13


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	if !_exercise_timeline_branches():
		return
	if !(await _exercise_exact_input_editor()):
		return
	if !_exercise_native_practice_state():
		return
	print("MXT_PRACTICE_MODE_UNIT_SMOKE_OK")
	quit(0)


func _exercise_timeline_branches() -> bool:
	var timeline = TimelineClass.new()
	timeline.begin()
	for tick in range(70):
		if !timeline.append_frame(tick, {4: PackedByteArray([tick & 0xff])}):
			return _fail("canonical timeline rejected prefix tick %d" % tick)
	var branch_a := timeline.retain_head()
	for tick in range(70, 150):
		timeline.append_frame(tick, {4: PackedByteArray([tick & 0xff])})
	var branch_b := timeline.retain_head()
	if !timeline.restore_head(branch_a):
		return _fail("canonical timeline could not restore the first retained head")
	for tick in range(70, 91):
		timeline.append_frame(tick, {4: PackedByteArray([(255 - tick) & 0xff])})
	var authored := timeline.flatten_frames()
	if authored.size() != 91 or int(((authored[90] as Dictionary)["inputs"] as Dictionary)[4][0]) != 165:
		return _fail("canonical branch extension did not preserve its independent suffix")
	if !timeline.restore_head(branch_b):
		return _fail("canonical timeline lost the second retained branch")
	var original := timeline.flatten_frames()
	if original.size() != 150 or int(((original[90] as Dictionary)["inputs"] as Dictionary)[4][0]) != 90:
		return _fail("restoring one branch mutated another retained branch")
	var seeded := TimelineClass.new()
	if !seeded.seed_frames(original) or seeded.frame_count() != 150 or seeded.input_byte_count() != 150:
		return _fail("decoded replay prefix did not seed the canonical timeline exactly")
	var invalid := original.duplicate(true)
	(invalid[12] as Dictionary)["tick"] = 99
	if seeded.seed_frames(invalid) or seeded.frame_count() != 0:
		return _fail("noncanonical replay prefix was accepted")
	return true


func _exercise_exact_input_editor() -> bool:
	var packed := load("res://practice/practice_input_editor.tscn") as PackedScene
	if packed == null:
		return _fail("Practice input editor scene could not be loaded")
	var editor_root = packed.instantiate()
	root.add_child(editor_root)
	await process_frame
	var editor = editor_root.get_node("Panel")
	var values := [0, 1, 63, 126, 127, 128, 191, 253, 254]
	for field in ["steer_horizontal", "steer_vertical", "strafe_left", "strafe_right"]:
		for raw_value in values:
			editor.raw_values[field] = raw_value
			var decoded: Dictionary = editor._decode_raw(editor.manual_bytes())
			if int(decoded.get(field, -1)) != raw_value or !editor.last_round_trip_valid:
				editor_root.queue_free()
				return _fail("raw input round trip failed field=%s value=%d" % [field, raw_value])
	for mask in range(32):
		editor.accelerate.set_pressed_no_signal((mask & 1) != 0)
		editor.brake.set_pressed_no_signal((mask & 2) != 0)
		editor.boost.set_pressed_no_signal((mask & 4) != 0)
		editor.spin_attack.set_pressed_no_signal((mask & 8) != 0)
		editor.side_attack.set_pressed_no_signal((mask & 16) != 0)
		var decoded: Dictionary = editor._decode_raw(editor.manual_bytes())
		if bool(decoded.get("accelerate", false)) != ((mask & 1) != 0) \
				or bool(decoded.get("brake", false)) != ((mask & 2) != 0) \
				or bool(decoded.get("boost", false)) != ((mask & 4) != 0) \
				or bool(decoded.get("spin_attack", false)) != ((mask & 8) != 0) \
				or bool(decoded.get("side_attack", false)) != ((mask & 16) != 0):
			editor_root.queue_free()
			return _fail("button input round trip failed mask=%d" % mask)
	editor_root.queue_free()
	return true


func _exercise_native_practice_state() -> bool:
	var track_path := ProjectSettings.globalize_path("res://").path_join(TRACK_RELATIVE).simplify_path()
	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var car_bytes := FileAccess.get_file_as_bytes(CAR_PROPS)
	if track_bytes.is_empty() or car_bytes.is_empty():
		return _fail("native Practice fixture content is unavailable")
	var buffer := StreamPeerBuffer.new()
	buffer.data_array = track_bytes
	var sim := GameSim.new()
	root.add_child(sim)
	sim.set_target_lap_count(0)
	sim.set_boost_unlocked_from_start(true)
	sim.instantiate_gamesim(buffer, [car_bytes], [0.5])
	sim.set_player_metadata([42], [false])
	sim.set_sim_started(true)
	if sim.get_target_lap_count() != 0:
		return _fail("native infinite-lap target was not retained")
	if !sim.get_boost_unlocked_from_start():
		return _fail("native boost-from-start policy was not retained")
	var telemetry: PackedFloat32Array = sim.get_player_telemetry_sample(42)
	if telemetry.size() != 39 or int(telemetry[2]) != 0:
		return _fail("native telemetry did not expose the infinite-lap target")
	var state := sim.get_full_state_data(0)
	sim.set_target_lap_count(99)
	if sim.get_target_lap_count() != 99 or !sim.load_full_state_data(0, state) or sim.get_target_lap_count() != 0:
		return _fail("full-state restore did not preserve the recorded lap target")
	var accelerate := PlayerInputClass.new()
	accelerate.accelerate = 1.0
	for _tick in range(301):
		sim.tick_singleplayer(42, accelerate.serialize())
	if sim.get_player_lap(42) > 1:
		return _fail("boost-from-start fixture advanced beyond the opening lap")
	accelerate.boost = true
	sim.tick_singleplayer(42, accelerate.serialize())
	telemetry = sim.get_player_telemetry_sample(42)
	if telemetry[TELEMETRY_MANUAL_BOOST_ACTIVE] < 0.5:
		return _fail("boost-from-start did not permit a manual boost on the opening lap")
	sim.destroy_gamesim()
	sim.queue_free()
	return true


func _fail(message: String) -> bool:
	push_error("MXT_PRACTICE_MODE_UNIT_SMOKE_FAIL " + message)
	quit(1)
	return false
