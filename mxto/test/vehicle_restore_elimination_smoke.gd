extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _expect(value: bool, expected: bool, label: String) -> bool:
	if value == expected:
		return true
	push_error("%s expected %s got %s" % [label, str(expected), str(value)])
	return false

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame

	var ok := true
	var zero_hp := VisualCar.FZ_MS.ZEROHP
	var completed := VisualCar.FZ_MS.COMPLETEDRACE_1_Q
	var fallout := VisualCar.FZ_MS.FALLOUT
	var retired := VisualCar.FZ_MS.RETIRED
	var airborne := VisualCar.FZ_MS.AIRBORNE

	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", zero_hp, 0, 0.0, -1000.0), false, "zero hp grace") and ok
	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", zero_hp, 0x80, 0.0, -1000.0), true, "settled breakdown") and ok
	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", zero_hp | airborne, 0x80, 0.0, -1000.0), false, "airborne breakdown") and ok
	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", zero_hp | retired, 0, 0.0, -1000.0), true, "retired breakdown") and ok
	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", fallout, 0, 0.0, -1000.0), true, "fallout") and ok
	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", zero_hp, 0, -1001.0, -1000.0), true, "below minimum y") and ok
	ok = _expect(main.call("_vehicle_restore_off_state_is_eliminated", completed | fallout | zero_hp | retired, 0x80, -1001.0, -1000.0), false, "completed race wins") and ok

	if !ok:
		quit(1)
		return
	print("MXT_VEHICLE_RESTORE_ELIMINATION_SMOKE ok")
	quit(0)
