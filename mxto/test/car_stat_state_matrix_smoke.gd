extends SceneTree

const CAR_PATH := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"
const TECHNIQUE_MTS := 0
const TECHNIQUE_QUICKTURN := 1
const BOOST_NO_BOOST := 2
const BOOST_MANUAL := 3
const BOOST_DASHPLATE := 4
const BOOST_STACKED := 5


func _fail(message: String) -> void:
	push_error("car_stat_state_matrix_smoke: " + message)
	print("MXT_CAR_STAT_STATE_MATRIX_FAIL")
	quit(1)


func _close(a: float, b: float) -> bool:
	return is_equal_approx(a, b) or absf(a - b) <= 0.00001 * maxf(1.0, maxf(absf(a), absf(b)))


func _evaluate(sim: GameSim, bytes: PackedByteArray, drifting: bool, strafe: float,
		slip: float, manual: bool, dash: bool, s_boost: bool, setting := 0.5) -> Dictionary:
	return sim.evaluate_car_properties(
		bytes, setting, drifting, strafe, slip, manual, dash, s_boost)


func _expect_layers(result: Dictionary, technique: int, boost: int, label: String) -> bool:
	if result.has("error"):
		_fail(label + ": " + String(result.error))
		return false
	if int(result.technique_layer) != technique or int(result.boost_layer) != boost:
		_fail("%s: got technique=%d boost=%d, expected technique=%d boost=%d" % [
			label, int(result.technique_layer), int(result.boost_layer), technique, boost])
		return false
	return true


func _initialize() -> void:
	var bytes := FileAccess.get_file_as_bytes(CAR_PATH)
	if bytes.is_empty():
		_fail("could not read " + CAR_PATH)
		return
	var sim := GameSim.new()

	var technique_cases := [
		[false, 1.0, 1.0, -1, "gripped strafe"],
		[true, 0.0, 1.0, -1, "neutral drift"],
		[true, 1.0, 0.0, -1, "zero slip"],
		[true, 1.0, 1.0, TECHNIQUE_MTS, "positive strafe MTS"],
		[true, -1.0, -1.0, TECHNIQUE_MTS, "negative strafe MTS"],
		[true, 1.0, -1.0, TECHNIQUE_QUICKTURN, "positive strafe quickturn"],
		[true, -1.0, 1.0, TECHNIQUE_QUICKTURN, "negative strafe quickturn"],
	]
	for case in technique_cases:
		var result := _evaluate(sim, bytes, case[0], case[1], case[2], false, false, false)
		if not _expect_layers(result, case[3], BOOST_NO_BOOST, case[4]):
			return
	var partial := _evaluate(sim, bytes, true, 0.35, -1.0, false, false, false)
	if not _close(float(partial.technique_intensity), 0.35):
		_fail("analog technique intensity was not preserved")
		return
	var clamped := _evaluate(sim, bytes, true, 2.0, -1.0, false, false, false)
	if not _close(float(clamped.technique_intensity), 1.0):
		_fail("technique intensity was not clamped")
		return

	var boost_cases := [
		[false, false, false, BOOST_NO_BOOST, "no boost"],
		[true, false, false, BOOST_MANUAL, "manual"],
		[false, true, false, BOOST_DASHPLATE, "dashplate"],
		[true, true, false, BOOST_STACKED, "stacked"],
		[false, false, true, -1, "S-BOOST"],
		[true, false, true, -1, "S-BOOST suppresses manual"],
		[false, true, true, BOOST_DASHPLATE, "S-BOOST plus dashplate"],
		[true, true, true, BOOST_DASHPLATE, "S-BOOST ignores manual while dashed"],
	]
	for case in boost_cases:
		var result := _evaluate(sim, bytes, false, 0.0, 0.0, case[0], case[1], case[2])
		if not _expect_layers(result, -1, case[3], case[4]):
			return

	for setting in [0.0, 0.5, 1.0]:
		var sampled := sim.sample_car_properties(bytes, setting)
		var neutral := _evaluate(sim, bytes, false, 0.0, 0.0, false, false, false, setting)
		var combined := _evaluate(sim, bytes, true, 0.25, -1.0, true, true, true, setting)
		for immutable_stat in ["weight_kg", "max_energy"]:
			var base_value := float(sampled.base_stats[immutable_stat])
			if not _close(float(neutral.effective_stats[immutable_stat]), base_value) \
					or not _close(float(combined.effective_stats[immutable_stat]), base_value):
				_fail("%s changed under a live modifier at setting %.2f" % [immutable_stat, setting])
				return
		var s_accel := float(sampled.s_boost_stats.acceleration)
		var qt_accel := float(sampled.modifier_stats.quickturn.acceleration)
		var dash_accel := float(sampled.modifier_stats.dashplate_boost.acceleration)
		var expected_accel := s_accel * lerpf(1.0, qt_accel, 0.25) * dash_accel
		if not _close(float(combined.effective_stats.acceleration), expected_accel):
			_fail("S-BOOST/technique/dash composition mismatch at setting %.2f" % setting)
			return

	print("MXT_CAR_STAT_STATE_MATRIX_OK")
	quit(0)
