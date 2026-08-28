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
	var spectator = main.spectator_controller
	spectator.configure_race(42, true)
	main.network_manager.race_configuration.vehicle_restore = false
	main.network_manager.race_results.player_eliminations[42] = 120
	ok = _expect(spectator.is_local_eliminated(), true, "restore-off elimination") and ok
	ok = _expect(spectator.should_suppress_local_race_input(), true, "elimination input suppression") and ok
	main.network_manager.race_configuration.vehicle_restore = true
	ok = _expect(spectator.is_local_eliminated(), false, "restore-on elimination") and ok
	main.network_manager.race_results.player_dnfs[42] = "low_speed"
	ok = _expect(spectator.is_local_dnf(), true, "local DNF") and ok
	ok = _expect(spectator.should_suppress_local_race_input(), true, "DNF input suppression") and ok
	spectator.reset()
	ok = _expect(spectator.should_suppress_local_race_input(), false, "reset input suppression") and ok

	if !ok:
		quit(1)
		return
	print("MXT_VEHICLE_RESTORE_ELIMINATION_SMOKE ok")
	quit(0)
