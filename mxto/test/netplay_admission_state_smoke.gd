extends SceneTree

class AdmissionTestNetworkManager extends NetworkManager:
	var begin_start_sync_calls := 0
	var schedule_calls := 0

	func _begin_start_sync() -> void:
		begin_start_sync_calls += 1
		start_sync_active = true

	func _try_schedule_synced_start() -> void:
		schedule_calls += 1

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _set_old_progress(nm: NetworkManager, id: int, elapsed_msec: int) -> void:
	var state: Dictionary = nm.race_admission_states[id]
	state["progress_msec"] = Time.get_ticks_msec() - elapsed_msec
	nm.race_admission_states[id] = state

func _init() -> void:
	var nm := AdmissionTestNetworkManager.new()
	var stamp_network := CustomStampNetworkController.new()
	nm.add_child(stamp_network)
	nm.custom_stamp_network = stamp_network

	nm.is_server = true
	nm.listen_server = false
	nm.race_active = true
	nm.player_ids = [1, 2, 3]
	nm.race_player_ids = [1, 2, 3]
	nm._initialize_race_admission_states()
	nm._set_race_admission_stage(1, nm.RACE_ADMISSION_READY, "ready")
	nm._set_race_admission_stage(2, nm.RACE_ADMISSION_LOADING, "building cars")
	nm._set_race_admission_stage(3, nm.RACE_ADMISSION_READY, "ready")
	if nm._all_race_admission_ready():
		_fail("loading peer must block race admission")
		return

	_set_old_progress(nm, 2, 31_000)
	var load_stall := nm.start_sync_drop_info()
	if load_stall.get("stalled_peer_ids", []) != [2]:
		_fail("loading stall must identify only peer 2: %s" % [load_stall])
		return
	var load_stages: PackedStringArray = load_stall.get("stalled_stages", PackedStringArray())
	if load_stages.size() != 1 or load_stages[0] != "loading the race":
		_fail("loading stall stage was not reported: %s" % [load_stages])
		return

	nm._set_race_admission_stage(2, nm.RACE_ADMISSION_READY, "ready")
	if !nm._all_race_admission_ready():
		_fail("all roster members should be ready by membership")
		return

	nm._reset_start_sync_state()
	nm._initialize_race_admission_states()
	nm._set_race_admission_stage(1, nm.RACE_ADMISSION_READY, "ready")
	nm._set_race_admission_stage(2, nm.RACE_ADMISSION_READY, "ready")
	nm._set_race_admission_stage(3, nm.RACE_ADMISSION_FAILED, "missing track")
	var failed_load := nm.start_sync_drop_info()
	if failed_load.get("stalled_peer_ids", []) != [3]:
		_fail("failed load must immediately identify only peer 3: %s" % [failed_load])
		return
	var failure_details: PackedStringArray = failed_load.get("stalled_details", PackedStringArray())
	if failure_details.size() != 1 or failure_details[0] != "missing track":
		_fail("failed load reason was not retained: %s" % [failure_details])
		return

	nm._reset_start_sync_state()
	nm._disconnected_during_race.clear()
	nm.player_ids = [1, 2, 3]
	nm.race_player_ids = [1, 2, 3]
	nm._initialize_race_admission_states()
	nm._set_race_admission_stage(1, nm.RACE_ADMISSION_READY, "ready")
	nm._set_race_admission_stage(2, nm.RACE_ADMISSION_READY, "ready")
	nm._set_race_admission_stage(3, nm.RACE_ADMISSION_LOADING, "loading")
	nm._on_peer_disconnected(3)
	if nm.begin_start_sync_calls != 1:
		_fail("disconnecting the only blocker must immediately begin synchronization")
		return
	if nm._get_race_ready_roster() != [1, 2]:
		_fail("disconnected peer remained in the ready roster: %s" % [nm._get_race_ready_roster()])
		return
	if nm.race_admission_states.has(3):
		_fail("disconnected peer retained stale admission state")
		return

	nm.start_sync_active = true
	nm.start_sync_scheduled = false
	nm._set_race_admission_stage(1, nm.RACE_ADMISSION_TIMING, "timing sample 4/4")
	nm._set_race_admission_stage(2, nm.RACE_ADMISSION_TIMING, "timing sample 1/4")
	_set_old_progress(nm, 2, 6_000)
	var timing_stall := nm.start_sync_drop_info()
	if timing_stall.get("stalled_peer_ids", []) != [2]:
		_fail("timing stall must identify only the peer that stopped sampling: %s" % [timing_stall])
		return

	print("MXT_NETPLAY_ADMISSION_STATE_OK")
	quit()
