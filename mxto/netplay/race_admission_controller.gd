class_name RaceAdmissionController
extends Node

signal disconnect_peer_requested(peer_id: int)
signal local_rtt_sample_received(sample_rtt_s: float)
signal peer_timing_sample_received(peer_id: int, rtt_s: float, ahead_ticks: float)
signal start_schedule_received(initial_max_ahead: float)
signal client_simulation_start_requested(initial_target_tick: int)
signal authoritative_simulation_start_requested

const SAMPLE_COUNT := 4
const PING_INTERVAL_MSEC := 50
const START_DELAY_MSEC := 750
const START_STALL_SEC := 5.0
const LOAD_STALL_SEC := 30.0
const TIMING_STALL_SEC := 5.0
const DROP_STATUS_INTERVAL_MSEC := 250
const MAX_AHEAD_TICKS := 30

const START_SENT := 0
const LOADING := 1
const READY := 2
const TIMING := 3
const SCHEDULED := 4
const FAILED := 5

const RACE_PHASE_TICK_BIT := 0x80000000
const RACE_PHASE_TICK_MASK := 0x7fffffff

var lobby_settings: Node
var game_sim: GameSim
var server_game_sim: GameSim

var is_server := false
var listen_server := false
var network_active := false
var race_active := false
var race_phase := 0
var player_ids: Array = []
var ready_roster: Array = []
var local_rtt_s := 0.0
var local_desired_ahead := 2.0

var active := false
var scheduled := false
var server_start_msec := 0
var local_start_msec := 0
var authoritative_started := false
var client_started := false
var last_ping_msec := 0
var sample_sequence := 0
var client_sample_count := 0
var server_offset_msec := 0.0
var peer_sample_counts := {}
var peer_ahead := {}
var initial_max_ahead := 2.0
var actual_client_start_msec := -1
var actual_server_start_msec := -1
var first_authoritative_input_msec := -1
var first_authoritative_first_tick := -1
var first_authoritative_last_tick := -1
var first_authoritative_count := 0
var debug_prints := 0

var admission_states := {}
var local_stage := START_SENT
var local_detail := ""
var local_progress_msec := 0
var remote_stalled_ids: Array = []
var remote_stalled_stages := PackedStringArray()
var remote_stalled_details := PackedStringArray()
var remote_drop_remaining_sec := 0.0
var last_drop_status_msec := 0

func initialize(in_lobby_settings: Node) -> void:
	lobby_settings = in_lobby_settings

func set_context(server: bool, listen: bool, active_network: bool, active_race: bool, phase: int, humans: Array, ready_humans: Array, rtt_s: float, desired_ahead: float, client_sim: GameSim, authoritative_sim: GameSim) -> void:
	is_server = server
	listen_server = listen
	network_active = active_network
	race_active = active_race
	race_phase = phase & 1
	player_ids = humans.duplicate()
	ready_roster = ready_humans.duplicate()
	local_rtt_s = rtt_s
	local_desired_ahead = desired_ahead
	game_sim = client_sim
	server_game_sim = authoritative_sim

func set_local_timing(rtt_s: float, desired_ahead: float) -> void:
	local_rtt_s = rtt_s
	local_desired_ahead = desired_ahead

func reset() -> void:
	active = false
	scheduled = false
	server_start_msec = 0
	local_start_msec = 0
	authoritative_started = false
	client_started = false
	last_ping_msec = 0
	sample_sequence = 0
	client_sample_count = 0
	server_offset_msec = 0.0
	peer_sample_counts.clear()
	peer_ahead.clear()
	initial_max_ahead = 2.0
	actual_client_start_msec = -1
	actual_server_start_msec = -1
	first_authoritative_input_msec = -1
	first_authoritative_first_tick = -1
	first_authoritative_last_tick = -1
	first_authoritative_count = 0
	debug_prints = 0
	admission_states.clear()
	local_stage = START_SENT
	local_detail = ""
	local_progress_msec = Time.get_ticks_msec()
	remote_stalled_ids.clear()
	remote_stalled_stages.clear()
	remote_stalled_details.clear()
	remote_drop_remaining_sec = 0.0
	last_drop_status_msec = 0

func remove_peer(peer_id: int) -> void:
	peer_sample_counts.erase(peer_id)
	peer_ahead.erase(peer_id)
	admission_states.erase(peer_id)
	evaluate()

func initialize_states() -> void:
	admission_states.clear()
	var now_msec := Time.get_ticks_msec()
	for id_value in ready_roster:
		admission_states[int(id_value)] = {
			"stage": START_SENT,
			"progress_msec": now_msec,
			"detail": "",
		}

func set_stage(peer_id: int, stage: int, detail: String = "") -> bool:
	if !is_server or !ready_roster.has(peer_id) or stage < START_SENT or stage > FAILED:
		return false
	var current = admission_states.get(peer_id, {})
	if typeof(current) == TYPE_DICTIONARY:
		var current_stage := int((current as Dictionary).get("stage", START_SENT))
		if current_stage == FAILED and stage != FAILED:
			return false
		if stage != FAILED and stage < current_stage:
			return false
	admission_states[peer_id] = {
		"stage": stage,
		"progress_msec": Time.get_ticks_msec(),
		"detail": detail,
	}
	return true

func stage_for(peer_id: int) -> int:
	var state = admission_states.get(peer_id, {})
	return int((state as Dictionary).get("stage", START_SENT)) if typeof(state) == TYPE_DICTIONARY else START_SENT

func all_ready() -> bool:
	if ready_roster.is_empty():
		return false
	for id_value in ready_roster:
		var stage := stage_for(int(id_value))
		if stage < READY or stage == FAILED:
			return false
	return true

func log_fields() -> Dictionary:
	var ready_count := 0
	var blocked_count := 0
	var snapshot_parts := PackedStringArray()
	var now_msec := Time.get_ticks_msec()
	if is_server:
		for id_value in ready_roster:
			var peer_id := int(id_value)
			var state = admission_states.get(peer_id, {})
			var stage := stage_for(peer_id)
			var progress_msec := now_msec
			var detail := ""
			if typeof(state) == TYPE_DICTIONARY:
				progress_msec = int((state as Dictionary).get("progress_msec", now_msec))
				detail = str((state as Dictionary).get("detail", ""))
			if stage >= READY and stage != FAILED:
				ready_count += 1
			if stage != SCHEDULED:
				blocked_count += 1
			snapshot_parts.append("%d:%d:%d:%d:%s" % [peer_id, stage, maxi(0, now_msec - progress_msec), int(peer_sample_counts.get(peer_id, 0)), _sanitize_detail(detail)])
	else:
		snapshot_parts.append("%d:%d:%d:%d:%s" % [multiplayer.get_unique_id(), local_stage, maxi(0, now_msec - local_progress_msec), client_sample_count, _sanitize_detail(local_detail)])
		if local_stage >= READY and local_stage != FAILED:
			ready_count = 1
		if local_stage != SCHEDULED:
			blocked_count = 1
	return {"ready": ready_count, "roster": ready_roster.size(), "blocked": blocked_count, "snapshot": "|".join(snapshot_parts)}

func evaluate() -> void:
	if !is_server or !race_active or scheduled or !all_ready():
		return
	if !active:
		_begin_start_sync()
	else:
		_try_schedule()

func report(stage: int, detail: String = "") -> void:
	if !race_active:
		return
	local_stage = stage
	local_detail = detail
	local_progress_msec = Time.get_ticks_msec()
	if is_server:
		if set_stage(multiplayer.get_unique_id(), stage, detail):
			evaluate()
	elif _has_network_peer():
		_submit_admission.rpc_id(1, race_phase, stage, detail)

func process() -> void:
	if !race_active:
		return
	var now := Time.get_ticks_msec()
	if is_server and !scheduled:
		_broadcast_drop_status()
	if !is_server and !listen_server and game_sim != null and !game_sim.sim_started and now >= last_ping_msec + PING_INTERVAL_MSEC and !scheduled:
		last_ping_msec = now
		sample_sequence += 1
		_start_sync_ping.rpc_id(1, now, _pack_tick(sample_sequence))
	if scheduled:
		if !client_started and game_sim != null and !game_sim.sim_started and now >= local_start_msec:
			client_started = true
			actual_client_start_msec = now
			client_simulation_start_requested.emit(int(ceil(clampf(_local_ahead(), 0.0, float(MAX_AHEAD_TICKS)))))
		if is_server and !authoritative_started and server_game_sim != null and !server_game_sim.sim_started and now >= server_start_msec:
			authoritative_started = true
			actual_server_start_msec = now
			authoritative_simulation_start_requested.emit()

func drop_info() -> Dictionary:
	var drop_active := race_active and !scheduled
	if is_server:
		drop_active = drop_active and (server_game_sim == null or !server_game_sim.sim_started)
		var stalled_ids := []
		var stalled_names := PackedStringArray()
		var stalled_stages := PackedStringArray()
		var stalled_details := PackedStringArray()
		var max_elapsed_sec := 0.0
		if drop_active:
			var now_msec := Time.get_ticks_msec()
			for id_value in ready_roster:
				var peer_id := int(id_value)
				if listen_server and peer_id == multiplayer.get_unique_id():
					continue
				var state = admission_states.get(peer_id, {})
				var stage := stage_for(peer_id)
				var progress_msec := int((state as Dictionary).get("progress_msec", now_msec)) if typeof(state) == TYPE_DICTIONARY else now_msec
				var detail := str((state as Dictionary).get("detail", "")) if typeof(state) == TYPE_DICTIONARY else ""
				var elapsed_sec := maxf(0.0, 0.001 * float(now_msec - progress_msec))
				max_elapsed_sec = maxf(max_elapsed_sec, elapsed_sec)
				if stage == FAILED or elapsed_sec >= _stall_timeout(stage):
					stalled_ids.append(peer_id)
					stalled_names.append(lobby_settings.username_for_player(peer_id))
					stalled_stages.append(_stage_name(stage))
					stalled_details.append(detail)
		return {"active": drop_active, "visible": drop_active and !stalled_ids.is_empty(), "stalled_peer_ids": stalled_ids, "stalled_names": stalled_names, "stalled_stages": stalled_stages, "stalled_details": stalled_details, "elapsed_sec": max_elapsed_sec, "can_drop": drop_active and !stalled_ids.is_empty(), "drop_remaining_sec": 0.0}
	drop_active = drop_active and !authoritative_started and !client_started
	var remote_names := PackedStringArray()
	for id_value in remote_stalled_ids:
		remote_names.append(lobby_settings.username_for_player(int(id_value)))
	return {"active": drop_active, "visible": drop_active and !remote_stalled_ids.is_empty(), "stalled_peer_ids": remote_stalled_ids.duplicate(true), "stalled_names": remote_names, "stalled_stages": remote_stalled_stages.duplicate(), "stalled_details": remote_stalled_details.duplicate(), "elapsed_sec": START_STALL_SEC, "can_drop": drop_active and !remote_stalled_ids.is_empty() and remote_drop_remaining_sec <= 0.0, "drop_remaining_sec": maxf(0.0, remote_drop_remaining_sec)}

func request_drop_stalled_players() -> bool:
	var info := drop_info()
	if !bool(info.get("can_drop", false)):
		return false
	if is_server:
		_drop_stalled_players(_id_array(info.get("stalled_peer_ids", [])))
		return true
	if race_active and _has_network_peer():
		_request_drop_stalled_players.rpc_id(1, race_phase)
		return true
	return false

@rpc("any_peer", "call_remote", "reliable", 7)
func _submit_admission(phase: int, stage: int, detail: String) -> void:
	if !is_server or !race_active or !_accept_phase(phase):
		return
	if set_stage(multiplayer.get_remote_sender_id(), stage, detail):
		evaluate()

@rpc("any_peer", "call_remote", "reliable", 7)
func _request_drop_stalled_players(phase: int) -> void:
	if !is_server or !race_active or !_accept_phase(phase):
		return
	var info := drop_info()
	if bool(info.get("can_drop", false)):
		_drop_stalled_players(_id_array(info.get("stalled_peer_ids", [])))

@rpc("authority", "call_remote", "unreliable", 7)
func _sync_drop_status(phase: int, stalled_ids: Array, stalled_stages: PackedStringArray, stalled_details: PackedStringArray, drop_remaining_sec: float) -> void:
	if !_accept_phase(phase):
		return
	remote_stalled_ids = _id_array(stalled_ids)
	remote_stalled_stages = stalled_stages.duplicate()
	remote_stalled_details = stalled_details.duplicate()
	remote_drop_remaining_sec = drop_remaining_sec

@rpc("any_peer", "call_remote", "unreliable", 5)
func _start_sync_ping(client_send_msec: int, packed_sample_sequence: int) -> void:
	if !race_active or !is_server or !active or scheduled or !_accept_phase(_unpack_phase(packed_sample_sequence)):
		return
	var sender := multiplayer.get_remote_sender_id()
	if player_ids.has(sender):
		_start_sync_pong.rpc_id(sender, client_send_msec, Time.get_ticks_msec(), packed_sample_sequence)

@rpc("authority", "call_remote", "unreliable", 5)
func _start_sync_pong(client_send_msec: int, server_recv_msec: int, packed_sample_sequence: int) -> void:
	if !race_active or is_server or !_accept_phase(_unpack_phase(packed_sample_sequence)):
		return
	var now := Time.get_ticks_msec()
	var sample_rtt_s := maxf(0.0, 0.001 * float(now - client_send_msec))
	local_rtt_sample_received.emit(sample_rtt_s)
	var sample_offset := float(server_recv_msec) - 0.5 * float(client_send_msec + now)
	server_offset_msec = sample_offset if client_sample_count == 0 else lerpf(server_offset_msec, sample_offset, 0.35)
	client_sample_count += 1
	if local_stage >= READY and local_stage < SCHEDULED:
		local_stage = TIMING
		local_detail = "timing sample %d/%d" % [mini(client_sample_count, SAMPLE_COUNT), SAMPLE_COUNT]
		local_progress_msec = now
	_client_start_sync_sample.rpc_id(1, packed_sample_sequence, local_rtt_s, local_desired_ahead)

@rpc("any_peer", "call_remote", "unreliable", 5)
func _client_start_sync_sample(packed_sample_sequence: int, client_rtt_s: float, client_ahead: float) -> void:
	if !race_active or !is_server or !active or scheduled or !_accept_phase(_unpack_phase(packed_sample_sequence)):
		return
	var sender := multiplayer.get_remote_sender_id()
	if !player_ids.has(sender):
		return
	peer_sample_counts[sender] = int(peer_sample_counts.get(sender, 0)) + 1
	peer_ahead[sender] = clampf(client_ahead, 0.0, float(MAX_AHEAD_TICKS))
	set_stage(sender, TIMING, "timing sample %d/%d" % [mini(int(peer_sample_counts[sender]), SAMPLE_COUNT), SAMPLE_COUNT])
	peer_timing_sample_received.emit(sender, client_rtt_s, peer_ahead[sender])
	_try_schedule()

@rpc("authority", "call_remote", "reliable", 7)
func begin_simulation() -> void:
	if !race_active:
		return
	if game_sim != null and !game_sim.sim_started:
		client_started = true
		actual_client_start_msec = Time.get_ticks_msec()
		client_simulation_start_requested.emit(0)
	if is_server and server_game_sim != null and !server_game_sim.sim_started:
		authoritative_started = true
		actual_server_start_msec = Time.get_ticks_msec()
		authoritative_simulation_start_requested.emit()

@rpc("authority", "call_remote", "reliable", 7)
func begin_simulation_at(phase: int, in_server_start_msec: int, in_initial_max_ahead: float) -> void:
	if !race_active or !_accept_phase(phase):
		return
	active = true
	scheduled = true
	local_stage = SCHEDULED
	local_detail = "start scheduled"
	local_progress_msec = Time.get_ticks_msec()
	server_start_msec = in_server_start_msec
	initial_max_ahead = in_initial_max_ahead
	start_schedule_received.emit(initial_max_ahead)
	var client_lead_msec := int(ceil(clampf(_local_ahead(), 0.0, float(MAX_AHEAD_TICKS)) * 1000.0 / 60.0))
	local_start_msec = server_start_msec - client_lead_msec if is_server else int(round(float(server_start_msec) - server_offset_msec))

func _begin_start_sync() -> void:
	if !is_server or active or scheduled:
		return
	active = true
	peer_sample_counts.clear()
	peer_ahead.clear()
	for id_value in ready_roster:
		set_stage(int(id_value), TIMING, "waiting for timing samples")
	if listen_server:
		var local_id := multiplayer.get_unique_id()
		peer_sample_counts[local_id] = SAMPLE_COUNT
		peer_ahead[local_id] = _local_ahead()
	_try_schedule()

func _try_schedule() -> void:
	if !is_server or !active or scheduled or !all_ready() or !_all_samples_ready():
		return
	var max_ahead := _local_ahead() if listen_server else 0.0
	for peer_id in ready_roster:
		max_ahead = maxf(max_ahead, float(peer_ahead.get(peer_id, 0.0)))
	initial_max_ahead = clampf(max_ahead, 0.0, float(MAX_AHEAD_TICKS))
	var lead_msec := int(ceil(initial_max_ahead * 1000.0 / 60.0))
	server_start_msec = Time.get_ticks_msec() + maxi(START_DELAY_MSEC, lead_msec + 250)
	scheduled = true
	for id_value in ready_roster:
		set_stage(int(id_value), SCHEDULED, "start scheduled")
	begin_simulation_at.rpc(race_phase, server_start_msec, initial_max_ahead)
	begin_simulation_at(race_phase, server_start_msec, initial_max_ahead)

func _all_samples_ready() -> bool:
	for peer_id in ready_roster:
		if is_server and listen_server and peer_id == multiplayer.get_unique_id():
			continue
		if int(peer_sample_counts.get(peer_id, 0)) < SAMPLE_COUNT:
			return false
	return true

func _broadcast_drop_status() -> void:
	if !is_server or !race_active or !_has_network_peer():
		return
	var now_msec := Time.get_ticks_msec()
	if now_msec < last_drop_status_msec + DROP_STATUS_INTERVAL_MSEC:
		return
	last_drop_status_msec = now_msec
	var info := drop_info()
	_sync_drop_status.rpc(race_phase, _id_array(info.get("stalled_peer_ids", [])), info.get("stalled_stages", PackedStringArray()), info.get("stalled_details", PackedStringArray()), float(info.get("drop_remaining_sec", 0.0)))

func _drop_stalled_players(peer_ids: Array) -> void:
	if !is_server or !race_active:
		return
	var connected_peers: PackedInt32Array = multiplayer.get_peers() if multiplayer.multiplayer_peer != null else PackedInt32Array()
	for id_value in peer_ids:
		var peer_id := int(id_value)
		if peer_id > 0 and !(listen_server and peer_id == multiplayer.get_unique_id()) and connected_peers.has(peer_id):
			disconnect_peer_requested.emit(peer_id)
	evaluate()

func _local_ahead() -> float:
	return 0.0 if is_server and listen_server else local_desired_ahead

func _stage_name(stage: int) -> String:
	match stage:
		START_SENT: return "waiting for start acknowledgement"
		LOADING: return "loading the race"
		READY: return "ready"
		TIMING: return "synchronizing clocks"
		SCHEDULED: return "scheduled"
		FAILED: return "load failed"
	return "unknown"

func _stall_timeout(stage: int) -> float:
	if stage == LOADING:
		return LOAD_STALL_SEC
	if stage == TIMING or stage == READY:
		return TIMING_STALL_SEC
	return START_STALL_SEC

func _sanitize_detail(detail: String) -> String:
	return detail.replace(",", ";").replace(":", ";").replace("|", "/")

func _id_array(values) -> Array:
	var result: Array = []
	if typeof(values) != TYPE_ARRAY:
		return result
	for value in values:
		var peer_id := int(value)
		if !result.has(peer_id):
			result.append(peer_id)
	return result

func _has_network_peer() -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if is_server:
		return true
	return multiplayer.multiplayer_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED

func _accept_phase(phase: int) -> bool:
	return (phase & 1) == race_phase

func _pack_tick(tick: int) -> int:
	return (tick & RACE_PHASE_TICK_MASK) | (race_phase << 31)

func _unpack_phase(packed_tick: int) -> int:
	return 1 if (packed_tick & RACE_PHASE_TICK_BIT) != 0 else 0
