extends SceneTree

class AdmissionTestController extends RaceAdmissionController:
	var begin_start_sync_calls := 0
	var schedule_calls := 0

	func _begin_start_sync() -> void:
		begin_start_sync_calls += 1
		active = true

	func _try_schedule() -> void:
		schedule_calls += 1

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _set_old_progress(controller: RaceAdmissionController, id: int, elapsed_msec: int) -> void:
	var state: Dictionary = controller.admission_states[id]
	state["progress_msec"] = Time.get_ticks_msec() - elapsed_msec
	controller.admission_states[id] = state

func _set_roster(controller: RaceAdmissionController, roster: Array) -> void:
	controller.set_context(true, false, false, true, 0, roster, roster, 0.0, 0.0, null, null)

func _init() -> void:
	var lobby_settings := LobbySettingsController.new()
	var controller := AdmissionTestController.new()
	controller.initialize(lobby_settings)
	_set_roster(controller, [1, 2, 3])
	controller.initialize_states()
	controller.set_stage(1, controller.READY, "ready")
	controller.set_stage(2, controller.LOADING, "building cars")
	controller.set_stage(3, controller.READY, "ready")
	if controller.all_ready():
		_fail("loading peer must block race admission")
		return

	_set_old_progress(controller, 2, 31_000)
	var load_stall := controller.drop_info()
	if load_stall.get("stalled_peer_ids", []) != [2]:
		_fail("loading stall must identify only peer 2: %s" % [load_stall])
		return
	var load_stages: PackedStringArray = load_stall.get("stalled_stages", PackedStringArray())
	if load_stages.size() != 1 or load_stages[0] != "loading the race":
		_fail("loading stall stage was not reported: %s" % [load_stages])
		return

	controller.set_stage(2, controller.READY, "ready")
	if !controller.all_ready():
		_fail("all roster members should be ready by membership")
		return

	controller.reset()
	controller.initialize_states()
	controller.set_stage(1, controller.READY, "ready")
	controller.set_stage(2, controller.READY, "ready")
	controller.set_stage(3, controller.FAILED, "missing track")
	var failed_load := controller.drop_info()
	if failed_load.get("stalled_peer_ids", []) != [3]:
		_fail("failed load must immediately identify only peer 3: %s" % [failed_load])
		return
	var failure_details: PackedStringArray = failed_load.get("stalled_details", PackedStringArray())
	if failure_details.size() != 1 or failure_details[0] != "missing track":
		_fail("failed load reason was not retained: %s" % [failure_details])
		return

	controller.reset()
	_set_roster(controller, [1, 2, 3])
	controller.initialize_states()
	controller.set_stage(1, controller.READY, "ready")
	controller.set_stage(2, controller.READY, "ready")
	controller.set_stage(3, controller.LOADING, "loading")
	_set_roster(controller, [1, 2])
	controller.remove_peer(3)
	if controller.begin_start_sync_calls != 1:
		_fail("disconnecting the only blocker must immediately begin synchronization")
		return
	if controller.ready_roster != [1, 2]:
		_fail("disconnected peer remained in the ready roster: %s" % [controller.ready_roster])
		return
	if controller.admission_states.has(3):
		_fail("disconnected peer retained stale admission state")
		return

	controller.active = true
	controller.scheduled = false
	controller.set_stage(1, controller.TIMING, "timing sample 4/4")
	controller.set_stage(2, controller.TIMING, "timing sample 1/4")
	_set_old_progress(controller, 2, 6_000)
	var timing_stall := controller.drop_info()
	if timing_stall.get("stalled_peer_ids", []) != [2]:
		_fail("timing stall must identify only the peer that stopped sampling: %s" % [timing_stall])
		return

	print("MXT_NETPLAY_ADMISSION_STATE_OK")
	quit()
