class_name InputTransportController
extends Node

signal authoritative_server_frame(tick: int)
signal disconnect_peer_requested(peer_id: int)

const PlayerInputClass = preload("res://player/player_input.gd")
var NEUTRAL_INPUT_BYTES: PackedByteArray = PlayerInputClass.new().serialize()

const MAX_AHEAD_TICKS := 30
const INPUT_FORWARD_REDUNDANCY_TICKS := 12
const STARTUP_LIGHT_NET_TICKS := 120
const SERVER_INPUT_REPLACEMENT_BACKLOG_TICKS := 5
const AUTH_INPUT_REDUNDANCY_FRAMES := 2
const AUTH_INPUT_ROLLBACK_WINDOW_TICKS := 20
const MIN_JITTER_BUFFER_SEC := 0.016
const RTT_VARIANCE_MULTIPLIER := 2.5
const MAX_JITTER_BUFFER_SEC := 0.150
const RTT_SMOOTHING := 0.1
const RTT_VARIANCE_SMOOTHING := 0.2
const SPEED_ADJUST_STEP := 0.0003
const SHARED_AHEAD_EXTRA_TICKS := 3.0
const SHARED_AHEAD_CAP_TICKS := 10.0
const RACE_PHASE_TICK_BIT := 0x80000000
const RACE_PHASE_TICK_MASK := 0x7fffffff
const AUTH_INPUT_MODE_MASK := 0x07
const AUTH_INPUT_COUNT_MASK := 0x78
const AUTH_INPUT_COUNT_SHIFT := 3
const AUTH_INPUT_COUNT_ESCAPE := 0x0f
const AUTH_INPUT_META_SHIFT := 32
const AUTH_INPUT_META_BYTE_MASK := 0xff
const AUTH_INPUT_META_PRESENT_BIT := 1 << 40
const SERVER_TIMING_SYNC_INTERVAL_TICKS := 1
const CLIENT_TIMING_PING_INTERVAL_MS := 250

var lobby_settings: Node
var state_transfer: Node
var race_results: Node
var race_admission: Node
var game_sim: GameSim
var server_game_sim: GameSim

var is_server := false
var listen_server := false
var network_active := false
var race_active := false
var race_netplay_phase := 0
var player_ids: Array = []
var spectator_ids: Array = []
var race_player_ids: Array = []
var ready_roster: Array = []
var active_human_roster: Array = []
var simulation_roster: Array = []
var disconnected_during_race := {}

var pending_inputs := {}
var last_input_time := {}
var last_local_input_bytes: PackedByteArray = NEUTRAL_INPUT_BYTES.duplicate()
var sent_inputs_bytes := {}
var sent_input_times := {}
var server_tick := 0
var local_tick := 0
var last_received_tick := {}
var last_ack_tick := -1
var target_tick := 0
var netcode_session := NetcodeSession.new()
var server_netcode_session := NetcodeSession.new()
var rtt_s := 0.0
var rtt_variance_s := 0.0
var desired_ahead_ticks := 2.0
var base_wait_time := 1.0 / 60.0
var max_ahead_from_server := 0.0
var peer_desired_ahead := {}
var peer_client_rtt_s := {}
var delayed_peer_ids := {}
var clients_server_tick := 0
var clients_target_tick := 0
var last_target_tick_update := 0
var last_client_timing_ping_msec := 0
var clients_max_ahead_from_server := 2.0
var last_server_input_tick := -1
var use_state_compression := true
var use_physics_ticks := 1.0
var rollback_frametime_us := 0

var log_bytes_out_total := 0
var log_bytes_in_total := 0
var log_bytes_out_interval := 0
var log_bytes_in_interval := 0
var log_inputs_sent := 0
var log_inputs_retransmitted := 0
var log_inputs_acked := 0
var log_flat_client_payload_out := 0
var log_flat_client_payload_in := 0
var log_flat_server_payload_out := 0
var log_flat_server_payload_in := 0
var log_auth_packets_sent := 0
var log_server_late_drops := 0
var log_server_replacements := 0
var log_server_behind_ticks_sum := 0
var log_server_behind_ticks_samples := 0
var log_server_behind_ticks_max := 0
var log_peer_inputs_accepted := {}
var log_peer_inputs_dropped := {}
var log_peer_replacements := {}
var log_client_ahead_throttle_frames := 0
var log_client_sim_ticks := 0
var log_client_target_tick_advances := 0
var log_client_target_tick_remote_advances := 0
var log_client_server_tick_advances := 0
var log_client_ahead_samples := 0
var log_client_current_ahead_sum := 0.0
var log_client_current_ahead_min := INF
var log_client_current_ahead_max := -INF
var log_client_target_ahead_sum := 0.0
var log_client_ahead_error_sum := 0.0
var log_client_ahead_error_min := INF
var log_client_ahead_error_max := -INF
var log_client_pre_auth_adjust_samples := 0
var log_peer_last_packet_start := {}
var log_peer_last_packet_count := {}
var log_peer_last_packet_accept := {}
var log_peer_last_packet_drop := {}
var log_peer_last_packet_reject_before := {}
var log_peer_last_packet_last_tick := {}
var log_peer_last_packet_server_lead := {}
var log_peer_last_packet_target_lead := {}
var log_peer_packet_server_lead_sum := 0.0
var log_peer_packet_server_lead_samples := 0
var log_peer_packet_server_lead_min := INF
var log_peer_packet_server_lead_max := -INF
var log_peer_packet_target_lead_sum := 0.0
var log_peer_packet_target_lead_samples := 0
var log_peer_packet_target_lead_min := INF
var log_peer_packet_target_lead_max := -INF
var log_timing_ping_out := 0
var log_timing_ping_in := 0
var log_timing_sync_out := 0
var log_timing_sync_in := 0
var log_timing_sync_rtt_ms_sum := 0.0
var log_timing_sync_rtt_ms_max := 0.0
var log_timing_sync_rtt_samples := 0
var log_timing_server_gap_max := 0
var log_timing_target_gap_max := 0
var log_timing_ack_advance := 0
var log_net_cpu_us_interval := 0
var log_sim_cpu_us_interval := 0
var log_rollback_us_sum := 0
var log_rollback_us_count := 0
var log_rollback_us_max := 0
var prof_collect_server_inputs_us_interval := 0
var prof_idle_broadcast_us_interval := 0
var prof_check_client_stalls_us_interval := 0
var prof_client_send_input_us_interval := 0
var prof_server_broadcast_recv_us_interval := 0
var prof_handle_state_us_interval := 0
var prof_handle_input_update_us_interval := 0
var prof_recalc_pred_us_interval := 0
var prof_adjust_time_scale_us_interval := 0
var prof_car_store_old_pos_us_interval := 0
var prof_car_post_render_us_interval := 0

func initialize(in_lobby_settings: Node, in_state_transfer: Node, in_race_results: Node, in_race_admission: Node) -> void:
	lobby_settings = in_lobby_settings
	state_transfer = in_state_transfer
	race_results = in_race_results
	race_admission = in_race_admission

func set_context(server: bool, listen: bool, active_network: bool, active_race: bool, phase: int, humans: Array, spectators: Array, race_humans: Array, ready_humans: Array, active_humans: Array, racers: Array, disconnected: Dictionary, client_sim: GameSim, authoritative_sim: GameSim) -> void:
	is_server = server
	listen_server = listen
	network_active = active_network
	race_active = active_race
	race_netplay_phase = phase & 1
	player_ids = humans.duplicate()
	spectator_ids = spectators.duplicate()
	race_player_ids = race_humans.duplicate()
	ready_roster = ready_humans.duplicate()
	active_human_roster = active_humans.duplicate()
	simulation_roster = racers.duplicate()
	disconnected_during_race = disconnected.duplicate()
	game_sim = client_sim
	server_game_sim = authoritative_sim

func remove_peer(peer_id: int) -> void:
	last_input_time.erase(peer_id)
	peer_desired_ahead.erase(peer_id)
	peer_client_rtt_s.erase(peer_id)
	delayed_peer_ids.erase(peer_id)
	last_received_tick.erase(peer_id)
	server_netcode_session.remove_peer(peer_id)
	for tick in pending_inputs:
		pending_inputs[tick].erase(peer_id)

func remove_cpu(peer_id: int) -> void:
	for tick in pending_inputs:
		pending_inputs[tick].erase(peer_id)

func record_player_dnf(player_id: int) -> void:
	delayed_peer_ids.erase(player_id)

func set_peer_timing(peer_id: int, peer_rtt_s: float, ahead_ticks: float) -> void:
	peer_desired_ahead[peer_id] = ahead_ticks
	peer_client_rtt_s[peer_id] = peer_rtt_s
	server_netcode_session.set_peer_desired_ahead(peer_id, ahead_ticks)

func apply_start_schedule(initial_max_ahead: float) -> void:
	clients_max_ahead_from_server = initial_max_ahead
	clients_server_tick = 0
	last_client_timing_ping_msec = 0
	last_target_tick_update = Time.get_ticks_msec()

func start_client_simulation(initial_target_tick: int) -> void:
	if game_sim == null or game_sim.sim_started:
		return
	game_sim.set_sim_started(true)
	local_tick = 0
	clients_server_tick = 0
	clients_target_tick = clampi(initial_target_tick, 0, MAX_AHEAD_TICKS)
	last_client_timing_ping_msec = 0
	last_target_tick_update = Time.get_ticks_msec()

func start_authoritative_simulation() -> void:
	if !is_server or server_game_sim == null or server_game_sim.sim_started:
		return
	server_tick = 0
	target_tick = 0
	server_game_sim.set_sim_started(true)

func reset() -> void:
	pending_inputs.clear()
	sent_inputs_bytes.clear()
	sent_input_times.clear()
	last_input_time.clear()
	last_local_input_bytes = NEUTRAL_INPUT_BYTES.duplicate()
	server_tick = 0
	local_tick = 0
	target_tick = 0
	last_received_tick.clear()
	last_ack_tick = -1
	rtt_s = 0.0
	rtt_variance_s = 0.0
	desired_ahead_ticks = 0.0 if is_server else 2.0
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	peer_client_rtt_s.clear()
	delayed_peer_ids.clear()
	log_peer_inputs_accepted.clear()
	log_peer_inputs_dropped.clear()
	log_peer_replacements.clear()
	log_client_ahead_throttle_frames = 0
	log_client_sim_ticks = 0
	log_client_target_tick_advances = 0
	log_client_target_tick_remote_advances = 0
	log_client_server_tick_advances = 0
	log_client_ahead_samples = 0
	log_client_current_ahead_sum = 0.0
	log_client_current_ahead_min = INF
	log_client_current_ahead_max = -INF
	log_client_target_ahead_sum = 0.0
	log_client_ahead_error_sum = 0.0
	log_client_ahead_error_min = INF
	log_client_ahead_error_max = -INF
	log_client_pre_auth_adjust_samples = 0
	log_peer_last_packet_start.clear()
	log_peer_last_packet_count.clear()
	log_peer_last_packet_accept.clear()
	log_peer_last_packet_drop.clear()
	log_peer_last_packet_reject_before.clear()
	log_peer_last_packet_last_tick.clear()
	log_peer_last_packet_server_lead.clear()
	log_peer_last_packet_target_lead.clear()
	log_peer_packet_server_lead_sum = 0.0
	log_peer_packet_server_lead_samples = 0
	log_peer_packet_server_lead_min = INF
	log_peer_packet_server_lead_max = -INF
	log_peer_packet_target_lead_sum = 0.0
	log_peer_packet_target_lead_samples = 0
	log_peer_packet_target_lead_min = INF
	log_peer_packet_target_lead_max = -INF
	reset_timing_log_counters()
	clients_server_tick = 0
	clients_target_tick = 0
	last_client_timing_ping_msec = 0
	clients_max_ahead_from_server = 2.0
	last_server_input_tick = -1
	use_physics_ticks = 1.0
	rollback_frametime_us = 0
	Engine.physics_ticks_per_second = 60
	server_netcode_session.clear_peer_state()
	netcode_session.reset()
	server_netcode_session.reset()

func reset_timing_log_counters() -> void:
	log_timing_ping_out = 0
	log_timing_ping_in = 0
	log_timing_sync_out = 0
	log_timing_sync_in = 0
	log_timing_sync_rtt_ms_sum = 0.0
	log_timing_sync_rtt_ms_max = 0.0
	log_timing_sync_rtt_samples = 0
	log_timing_server_gap_max = 0
	log_timing_target_gap_max = 0
	log_timing_ack_advance = 0

func acc_log_out(byte_count: int) -> void:
	log_bytes_out_total += byte_count
	log_bytes_out_interval += byte_count

func acc_log_in(byte_count: int) -> void:
	log_bytes_in_total += byte_count
	log_bytes_in_interval += byte_count

func _get_human_roster() -> Array:
	return race_player_ids if !race_player_ids.is_empty() else player_ids

func _get_race_ready_roster() -> Array:
	return ready_roster

func _get_active_human_roster() -> Array:
	return active_human_roster

func _human_racer_uses_native_cpu_input(player_id: int) -> bool:
	return race_results.player_finish_times.has(player_id) or race_results.player_dnfs.has(player_id) or disconnected_during_race.has(player_id)

func get_simulation_roster() -> Array:
	return simulation_roster

func _startup_light_net_active(tick: int) -> bool:
	return tick >= 0 and tick < STARTUP_LIGHT_NET_TICKS

func _store_neutral_authoritative_frame_for_all_racers(tick: int) -> void:
	for player_id in simulation_roster:
		netcode_session.store_authoritative_input(tick, int(player_id), NEUTRAL_INPUT_BYTES)

func _accept_race_packet_phase(phase: int) -> bool:
	return (phase & 1) == race_netplay_phase

func _can_send_rpc_to_peer(peer_id: int) -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if listen_server and peer_id == multiplayer.get_unique_id():
		return true
	return multiplayer.get_peers().has(peer_id)
func _log_add_int(dict: Dictionary, key, amount: int) -> void:
	if amount <= 0:
		return
	dict[key] = int(dict.get(key, 0)) + amount

func _log_client_ahead_sample(current_ahead: float, target_ahead: float) -> void:
	var error := target_ahead - current_ahead
	log_client_ahead_samples += 1
	log_client_current_ahead_sum += current_ahead
	log_client_current_ahead_min = min(log_client_current_ahead_min, current_ahead)
	log_client_current_ahead_max = max(log_client_current_ahead_max, current_ahead)
	log_client_target_ahead_sum += target_ahead
	log_client_ahead_error_sum += error
	log_client_ahead_error_min = min(log_client_ahead_error_min, error)
	log_client_ahead_error_max = max(log_client_ahead_error_max, error)

func _log_peer_packet_lead(server_lead: int, target_lead: int) -> void:
	log_peer_packet_server_lead_sum += float(server_lead)
	log_peer_packet_server_lead_samples += 1
	log_peer_packet_server_lead_min = min(log_peer_packet_server_lead_min, float(server_lead))
	log_peer_packet_server_lead_max = max(log_peer_packet_server_lead_max, float(server_lead))
	log_peer_packet_target_lead_sum += float(target_lead)
	log_peer_packet_target_lead_samples += 1
	log_peer_packet_target_lead_min = min(log_peer_packet_target_lead_min, float(target_lead))
	log_peer_packet_target_lead_max = max(log_peer_packet_target_lead_max, float(target_lead))

func _pack_race_phase_tick(tick: int) -> int:
	return (tick & RACE_PHASE_TICK_MASK) | (race_netplay_phase << 31)

func _pack_authoritative_input_tick(tick: int, input_meta: int) -> int:
	var packed_tick := _pack_race_phase_tick(tick)
	if input_meta >= 0:
		packed_tick |= AUTH_INPUT_META_PRESENT_BIT | ((input_meta & AUTH_INPUT_META_BYTE_MASK) << AUTH_INPUT_META_SHIFT)
	return packed_tick

func _authoritative_input_meta_can_strip(input_meta: int) -> bool:
	var count_code := (input_meta & AUTH_INPUT_COUNT_MASK) >> AUTH_INPUT_COUNT_SHIFT
	return count_code != AUTH_INPUT_COUNT_ESCAPE

func _unpack_authoritative_input_meta(packed_tick: int) -> int:
	if (packed_tick & AUTH_INPUT_META_PRESENT_BIT) == 0:
		return -1
	return (packed_tick >> AUTH_INPUT_META_SHIFT) & AUTH_INPUT_META_BYTE_MASK

func _unpack_race_phase(packed_tick: int) -> int:
	return 1 if (packed_tick & RACE_PHASE_TICK_BIT) != 0 else 0

func _unpack_race_tick(packed_tick: int) -> int:
	return packed_tick & RACE_PHASE_TICK_MASK

func _calc_max_ahead() -> float:
	return float(server_netcode_session.get_max_peer_desired_ahead(_get_active_human_roster(), _local_desired_ahead_for_shared()))

func _local_desired_ahead_for_shared() -> float:
	return 0.0 if is_server and listen_server else desired_ahead_ticks

func process() -> void:
	race_results.set_race_tick(get_race_tick())
	if !race_active:
		return
	race_admission.process()
	if is_server and server_game_sim != null and server_game_sim.sim_started:
		target_tick += 1
		if target_tick > server_tick + MAX_AHEAD_TICKS:
			target_tick = server_tick + MAX_AHEAD_TICKS
		var server_behind := maxi(target_tick - server_tick, 0)
		log_server_behind_ticks_sum += server_behind
		log_server_behind_ticks_samples += 1
		log_server_behind_ticks_max = maxi(log_server_behind_ticks_max, server_behind)
		var loops := 0
		const MAX_SERVER_TICKS_PER_PROCESS := 8
		while server_tick < target_tick and loops < MAX_SERVER_TICKS_PER_PROCESS:
			var _collect_t0 := Time.get_ticks_usec()
			var collected := collect_server_inputs()
			var _collect_t1 := Time.get_ticks_usec()
			prof_collect_server_inputs_us_interval += _collect_t1 - _collect_t0
			if !collected:
				break
			post_tick()
			loops += 1
		if server_tick < target_tick:
			var _stall_t0 := Time.get_ticks_usec()
			_check_client_stalls()
			var _stall_t1 := Time.get_ticks_usec()
			prof_check_client_stalls_us_interval += _stall_t1 - _stall_t0
	if is_server:
		state_transfer.send_outgoing_chunks(is_server, network_active, listen_server)
	if !is_server and !listen_server and Time.get_ticks_msec() > last_target_tick_update + 17 and game_sim != null and game_sim.sim_started:
		var old_clients_target_tick : int = clients_target_tick
		clients_target_tick += 1
		if clients_target_tick > clients_server_tick + MAX_AHEAD_TICKS:
			clients_target_tick = clients_server_tick + MAX_AHEAD_TICKS
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_advances += clients_target_tick - old_clients_target_tick
	_process_client_timing_ping()

func _process_client_timing_ping() -> void:
	if is_server or listen_server or !race_active:
		return
	if game_sim == null or !game_sim.sim_started:
		return
	var now := Time.get_ticks_msec()
	if now < last_client_timing_ping_msec + CLIENT_TIMING_PING_INTERVAL_MS:
		return
	last_client_timing_ping_msec = now
	log_timing_ping_out += 1
	acc_log_out(8)
	_client_timing_ping.rpc_id(1, now, _pack_race_phase_tick(local_tick))

func set_local_input(input: PackedByteArray) -> void:
	last_local_input_bytes = input
	netcode_session.set_local_input(input)
	server_netcode_session.set_local_input(input)

func _input_frame_value(frame, player_id: int, fallback: PackedByteArray) -> PackedByteArray:
	if typeof(frame) == TYPE_DICTIONARY:
		var dict := frame as Dictionary
		if dict.has(player_id):
			return dict[player_id]
	elif typeof(frame) == TYPE_ARRAY:
		var roster := get_simulation_roster()
		var index := roster.find(player_id)
		if index >= 0 and index < (frame as Array).size():
			return (frame as Array)[index]
	return fallback

func _fill_delayed_missing_inputs_for_tick(tick: int) -> void:
	if !is_server:
		return
	var backlog := target_tick - tick
	var can_mark_new_delayed := backlog >= SERVER_INPUT_REPLACEMENT_BACKLOG_TICKS
	var has_disconnected_peers := !disconnected_during_race.is_empty()
	if !can_mark_new_delayed and delayed_peer_ids.is_empty() and !has_disconnected_peers:
		return
	if not pending_inputs.has(tick):
		pending_inputs[tick] = {}
	var waiting: Dictionary = pending_inputs[tick]
	var roster_chk := _get_human_roster()
	var replacement_count: int = server_netcode_session.fill_missing_pending_inputs(
		tick,
		roster_chk,
		disconnected_during_race.keys(),
		delayed_peer_ids.keys(),
		can_mark_new_delayed
	)
	for replacement_index in range(replacement_count):
		var pid: int = server_netcode_session.get_last_replaced_pending_player_id(replacement_index)
		waiting[pid] = NEUTRAL_INPUT_BYTES
		delayed_peer_ids[pid] = true
		log_server_replacements += 1
		_log_add_int(log_peer_replacements, pid, 1)
	pending_inputs[tick] = waiting

func collect_server_inputs() -> bool:
	if not is_server:
		return false
	if server_game_sim == null or !server_game_sim.sim_started:
		return false
	if not pending_inputs.has(server_tick):
		pending_inputs[server_tick] = {}
	if _startup_light_net_active(server_tick):
		var roster_chk : Array = _get_human_roster()
		for id in roster_chk:
			pending_inputs[server_tick][id] = NEUTRAL_INPUT_BYTES
			server_netcode_session.store_pending_input(server_tick, int(id), NEUTRAL_INPUT_BYTES)
	elif listen_server:
		var local_id := multiplayer.get_unique_id()
		var host_input: PackedByteArray = pending_inputs[server_tick].get(local_id, last_local_input_bytes)
		pending_inputs[server_tick][local_id] = host_input
		server_netcode_session.store_pending_input(server_tick, local_id, host_input)
		last_input_time[local_id] = 0.001 * float(Time.get_ticks_msec())
		last_received_tick[local_id] = server_tick
		server_netcode_session.set_peer_last_received(local_id, server_tick, last_input_time[local_id])
	if server_game_sim != null and server_game_sim.has_method("get_native_cpu_input_for_tick"):
		for id in _get_human_roster():
			if race_results.player_eliminations.has(id):
				pending_inputs[server_tick][id] = NEUTRAL_INPUT_BYTES
				server_netcode_session.store_pending_input(server_tick, int(id), NEUTRAL_INPUT_BYTES)
			elif _human_racer_uses_native_cpu_input(int(id)):
				var cpu_input: PackedByteArray = server_game_sim.get_native_cpu_input_for_tick(int(id), server_tick)
				pending_inputs[server_tick][id] = cpu_input
				server_netcode_session.store_pending_input(server_tick, int(id), cpu_input)
	if server_tick > target_tick:
		return false
	_fill_delayed_missing_inputs_for_tick(server_tick)
	var sim_t0 := Time.get_ticks_usec()
	var ticked := server_netcode_session.tick_server_frame(server_game_sim, server_tick)
	log_sim_cpu_us_interval += Time.get_ticks_usec() - sim_t0
	if !ticked:
		return false
	pending_inputs.erase(server_tick)
	return true

func _input_forward_window(last_tick: int) -> Vector2i:
	if last_tick < STARTUP_LIGHT_NET_TICKS:
		return Vector2i(-1, 0)
	var first_tick := maxi(STARTUP_LIGHT_NET_TICKS, last_tick - INPUT_FORWARD_REDUNDANCY_TICKS + 1)
	return Vector2i(first_tick, last_tick - first_tick + 1)

func collect_client_inputs() -> bool:
	if game_sim != null and !game_sim.sim_started:
		return false
	var my_settings: Dictionary = lobby_settings.get_player_settings(multiplayer.get_unique_id())
	var is_spec = typeof(my_settings) == TYPE_DICTIONARY and my_settings.get("spectator", false)
	if is_spec:
		if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
			log_client_ahead_throttle_frames += 1
			return false
		netcode_session.tick_client_predicted_frame(game_sim, local_tick)
		local_tick += 1
		log_client_sim_ticks += 1
		_adjust_time_scale()
		return true
	if _startup_light_net_active(local_tick):
		_store_neutral_authoritative_frame_for_all_racers(local_tick)
		netcode_session.store_local_input(local_tick, NEUTRAL_INPUT_BYTES)
		if !is_server:
			_client_startup_sync.rpc_id(1, desired_ahead_ticks, _pack_race_phase_tick(local_tick), rtt_s)
			acc_log_out(12)
		netcode_session.tick_client_predicted_frame(game_sim, local_tick)
		local_tick += 1
		log_client_sim_ticks += 1
		_adjust_time_scale()
		return true
	if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
		log_client_ahead_throttle_frames += 1
		if !is_server:
			var last_forward_tick := local_tick - 1
			var window := _input_forward_window(last_forward_tick)
			if window.y > 0:
				var packet: PackedByteArray = netcode_session.build_local_input_packet(window.x, window.y, race_netplay_phase)
				log_flat_client_payload_out += packet.size()
				acc_log_out(12 + packet.size())
				_client_send_input_flat.rpc_id(1, packet, desired_ahead_ticks, rtt_s)
				last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		return false
	var local_input_for_tick: PackedByteArray = last_local_input_bytes
	var local_player_id := multiplayer.get_unique_id()
	if race_results.player_eliminations.has(local_player_id):
		local_input_for_tick = NEUTRAL_INPUT_BYTES
	elif _human_racer_uses_native_cpu_input(local_player_id) and game_sim != null and game_sim.has_method("get_native_cpu_input_for_tick"):
		local_input_for_tick = game_sim.get_native_cpu_input_for_tick(local_player_id, local_tick)
	sent_inputs_bytes[local_tick] = local_input_for_tick
	sent_input_times[local_tick] = 0.001 * float(Time.get_ticks_msec())
	netcode_session.store_local_input(local_tick, local_input_for_tick)
	if is_server:
		if not pending_inputs.has(local_tick):
			pending_inputs[local_tick] = {}
		pending_inputs[local_tick][local_player_id] = local_input_for_tick
		server_netcode_session.store_pending_input(local_tick, local_player_id, local_input_for_tick)
		last_input_time[local_player_id] = 0.001 * float(Time.get_ticks_msec())
		last_received_tick[local_player_id] = local_tick
		server_netcode_session.set_peer_last_received(local_player_id, local_tick, last_input_time[local_player_id])
	if !is_server:
		var window := _input_forward_window(local_tick)
		var input_packet: PackedByteArray = netcode_session.build_local_input_packet(window.x, window.y, race_netplay_phase)
		log_flat_client_payload_out += input_packet.size()
		acc_log_out(12 + input_packet.size())
		log_inputs_sent += window.y
		log_inputs_retransmitted += maxi(window.y - 1, 0)
		_client_send_input_flat.rpc_id(1, input_packet, desired_ahead_ticks, rtt_s)
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
	netcode_session.tick_client_predicted_frame(game_sim, local_tick)
	local_tick += 1
	log_client_sim_ticks += 1
	_adjust_time_scale()
	return true

func _send_server_timing_sync(peer_id: int, sync_tick: int, max_ahead: float, echo_client_msec: int = -1) -> void:
	if !is_server or !race_active:
		return
	if listen_server and peer_id == multiplayer.get_unique_id():
		return
	if !_can_send_rpc_to_peer(peer_id):
		return
	var ack_tick := server_netcode_session.get_peer_last_received(peer_id)
	log_timing_sync_out += 1
	acc_log_out(28)
	_server_timing_sync.rpc_id(
		peer_id,
		_pack_race_phase_tick(sync_tick),
		target_tick,
		ack_tick,
		max_ahead,
		Time.get_ticks_msec(),
		echo_client_msec)

@rpc("any_peer", "unreliable", "call_remote", 5)
func _client_timing_ping(client_msec: int, packed_client_tick: int) -> void:
	if !race_active or !is_server or !_accept_race_packet_phase(_unpack_race_phase(packed_client_tick)):
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if sender_id == 0 or (!player_ids.has(sender_id) and !spectator_ids.has(sender_id)):
		return
	log_timing_ping_in += 1
	var max_ahead := _calc_max_ahead()
	max_ahead_from_server = max_ahead
	_send_server_timing_sync(sender_id, server_tick, max_ahead, client_msec)

@rpc("authority", "unreliable", "call_remote", 5)
func _server_timing_sync(packed_server_tick: int, tgt: int, this_ack: int, max_ahead: float, _server_msec: int, echo_client_msec: int) -> void:
	if !race_active or !_accept_race_packet_phase(_unpack_race_phase(packed_server_tick)):
		return
	var unpacked_server_tick := _unpack_race_tick(packed_server_tick)
	if not is_server or listen_server:
		log_timing_sync_in += 1
		var old_clients_server_tick : int = clients_server_tick
		var old_clients_target_tick : int = clients_target_tick
		log_timing_server_gap_max = maxi(log_timing_server_gap_max, maxi((unpacked_server_tick + 1) - clients_server_tick, 0))
		log_timing_target_gap_max = maxi(log_timing_target_gap_max, maxi(tgt - clients_target_tick, 0))
		clients_server_tick = max(clients_server_tick, unpacked_server_tick + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		if clients_server_tick > old_clients_server_tick:
			log_client_server_tick_advances += clients_server_tick - old_clients_server_tick
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_remote_advances += clients_target_tick - old_clients_target_tick
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
		last_server_input_tick = max(last_server_input_tick, unpacked_server_tick)
		if echo_client_msec >= 0:
			var sample_ms: float = maxf(0.0, float(Time.get_ticks_msec() - echo_client_msec))
			log_timing_sync_rtt_ms_sum += sample_ms
			log_timing_sync_rtt_ms_max = maxf(log_timing_sync_rtt_ms_max, sample_ms)
			log_timing_sync_rtt_samples += 1
			var sample: float = 0.001 * sample_ms
			_record_rtt_sample(sample)
	var old_ack := last_ack_tick
	_apply_server_input_ack(this_ack, false)
	log_timing_ack_advance += max(0, last_ack_tick - max(old_ack, -1))
	acc_log_in(28)

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_startup_sync(ahead: float, client_tick: int, client_rtt_s: float) -> void:
	if !race_active or !_accept_race_packet_phase(_unpack_race_phase(client_tick)):
		return
	if !is_server:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if !_get_race_ready_roster().has(sender_id):
		return
	var now_sec := 0.001 * float(Time.get_ticks_msec())
	var unpacked_client_tick := _unpack_race_tick(client_tick)
	peer_desired_ahead[sender_id] = ahead
	peer_client_rtt_s[sender_id] = client_rtt_s
	server_netcode_session.set_peer_desired_ahead(sender_id, ahead)
	last_input_time[sender_id] = now_sec
	last_received_tick[sender_id] = max(int(last_received_tick.get(sender_id, -1)), min(unpacked_client_tick, STARTUP_LIGHT_NET_TICKS - 1))
	server_netcode_session.set_peer_last_received(sender_id, int(last_received_tick[sender_id]), now_sec)
	acc_log_in(12)

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_send_input_flat(packet: PackedByteArray, ahead: float, client_rtt_s: float) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if is_server:
		var sender_id := multiplayer.get_remote_sender_id()
		if !_get_race_ready_roster().has(sender_id):
			return
		var now_sec := 0.001 * float(Time.get_ticks_msec())
		var sender_seen_before: bool = server_netcode_session.peer_has_received(sender_id)
		var recovery_cutoff := target_tick - SERVER_INPUT_REPLACEMENT_BACKLOG_TICKS
		var sender_delayed := delayed_peer_ids.has(sender_id)
		var reject_before := recovery_cutoff if sender_delayed else maxi(recovery_cutoff, server_tick)
		if !sender_seen_before:
			reject_before = server_tick
		var packet_status := server_netcode_session.store_pending_input_packet(
			sender_id, reject_before, packet, ahead, now_sec, race_netplay_phase)
		if packet_status == NetcodeSession.PACKET_STORE_INVALID:
			return
		if packet_status == NetcodeSession.PACKET_STORE_STALE:
			return
		var start_tick := server_netcode_session.get_last_pending_packet_start_tick()
		var count := server_netcode_session.get_last_pending_packet_count()
		var accepted := server_netcode_session.get_last_pending_packet_accepted()
		var dropped := server_netcode_session.get_last_pending_packet_dropped()
		var last_tick := server_netcode_session.get_last_pending_packet_last_tick()
		var server_lead := last_tick - server_tick if last_tick >= 0 else -9999
		var target_lead := last_tick - target_tick if last_tick >= 0 else -9999
		log_peer_last_packet_start[sender_id] = start_tick
		log_peer_last_packet_count[sender_id] = count
		log_peer_last_packet_accept[sender_id] = accepted
		log_peer_last_packet_drop[sender_id] = dropped
		log_peer_last_packet_reject_before[sender_id] = reject_before
		log_peer_last_packet_last_tick[sender_id] = last_tick
		log_peer_last_packet_server_lead[sender_id] = server_lead
		log_peer_last_packet_target_lead[sender_id] = target_lead
		if last_tick >= 0:
			_log_peer_packet_lead(server_lead, target_lead)
		if dropped > 0:
			log_server_late_drops += dropped
			_log_add_int(log_peer_inputs_dropped, sender_id, dropped)
		if accepted > 0:
			last_input_time[sender_id] = now_sec
			last_received_tick[sender_id] = last_tick
			_log_add_int(log_peer_inputs_accepted, sender_id, accepted)
			if last_tick >= recovery_cutoff and delayed_peer_ids.has(sender_id):
				delayed_peer_ids.erase(sender_id)
		peer_desired_ahead[sender_id] = ahead
		peer_client_rtt_s[sender_id] = client_rtt_s
		log_flat_client_payload_in += packet.size()
		acc_log_in(12 + packet.size())
	var __prof_t1 := Time.get_ticks_usec()
	prof_client_send_input_us_interval += __prof_t1 - __prof_t0

func _apply_server_input_ack(ack_tick: int, update_rtt_from_input: bool = true) -> void:
	if ack_tick == -1:
		return
	var old_ack := last_ack_tick
	last_ack_tick = max(last_ack_tick, ack_tick)
	var newly = max(0, last_ack_tick - max(old_ack, -1))
	log_inputs_acked += newly
	if update_rtt_from_input and sent_input_times.has(ack_tick):
		if is_server and listen_server:
			rtt_s = 0.0
			rtt_variance_s = 0.0
			desired_ahead_ticks = 0.0
		else:
			var sample : float = 0.001 * float(Time.get_ticks_msec()) - sent_input_times[ack_tick]
			_record_rtt_sample(sample)
		sent_input_times.erase(ack_tick)
	for key in sent_inputs_bytes.keys():
		if key <= last_ack_tick:
			sent_inputs_bytes.erase(key)
	for key in sent_input_times.keys():
		if key <= last_ack_tick:
			sent_input_times.erase(key)

@rpc("any_peer", "unreliable", "call_local", 3)
func _server_startup_sync(server_tick_value: int, this_ack: int, tgt: int, max_ahead: float) -> void:
	if !race_active or !_accept_race_packet_phase(_unpack_race_phase(server_tick_value)):
		return
	var unpacked_server_tick := _unpack_race_tick(server_tick_value)
	if not is_server or listen_server:
		var old_clients_server_tick : int = clients_server_tick
		var old_clients_target_tick : int = clients_target_tick
		clients_server_tick = max(clients_server_tick, unpacked_server_tick + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		if clients_server_tick > old_clients_server_tick:
			log_client_server_tick_advances += clients_server_tick - old_clients_server_tick
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_remote_advances += clients_target_tick - old_clients_target_tick
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
		last_server_input_tick = max(last_server_input_tick, unpacked_server_tick)
	_apply_server_input_ack(this_ack, false)

@rpc("any_peer", "unreliable_ordered", "call_local", 2)
func _server_broadcast_flat(authoritative_last_tick: int, input_packet: PackedByteArray, this_ack: int) -> void:
	if !race_active or !_accept_race_packet_phase(_unpack_race_phase(authoritative_last_tick)):
		return
	var input_packet_meta := _unpack_authoritative_input_meta(authoritative_last_tick)
	authoritative_last_tick = _unpack_race_tick(authoritative_last_tick)
	var __prof_t0 := Time.get_ticks_usec()
	if not is_server or listen_server:
		var old_clients_server_tick : int = clients_server_tick
		var old_clients_target_tick : int = clients_target_tick
		var packet_status := netcode_session.store_authoritative_input_packet(
			input_packet, race_netplay_phase, authoritative_last_tick, input_packet_meta)
		if packet_status == NetcodeSession.PACKET_STORE_STALE:
			return
		if packet_status == NetcodeSession.PACKET_STORE_VALID:
			var count := netcode_session.get_last_authoritative_packet_count()
			if count > 0:
				var first_tick := netcode_session.get_last_authoritative_packet_first_tick()
				var last_tick := netcode_session.get_last_authoritative_packet_last_tick()
				if race_admission.first_authoritative_input_msec < 0:
					race_admission.first_authoritative_input_msec = Time.get_ticks_msec()
					race_admission.first_authoritative_first_tick = first_tick
					race_admission.first_authoritative_last_tick = last_tick
					race_admission.first_authoritative_count = count
				clients_server_tick = max(clients_server_tick, last_tick + 1)
				var rollback_start := maxi(first_tick, local_tick - AUTH_INPUT_ROLLBACK_WINDOW_TICKS)
				if last_tick >= rollback_start:
					_handle_input_update_flat(rollback_start)
				last_server_input_tick = max(last_server_input_tick, last_tick)
			else:
				clients_server_tick = max(clients_server_tick, authoritative_last_tick + 1)
		if listen_server:
			clients_target_tick = max(clients_target_tick, target_tick)
		if clients_server_tick > old_clients_server_tick:
			log_client_server_tick_advances += clients_server_tick - old_clients_server_tick
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_remote_advances += clients_target_tick - old_clients_target_tick
		if listen_server:
			last_target_tick_update = Time.get_ticks_msec()
			clients_max_ahead_from_server = max_ahead_from_server
		log_flat_server_payload_in += input_packet.size()
	_apply_server_input_ack(this_ack, false)
	acc_log_in(12 + input_packet.size())
	var __prof_t1 := Time.get_ticks_usec()
	prof_server_broadcast_recv_us_interval += __prof_t1 - __prof_t0

func post_tick() -> void:
	if !race_active:
		return
	if is_server and server_game_sim != null:
		var _t0 := Time.get_ticks_usec()
		authoritative_server_frame.emit(server_tick)
		var max_ahead := _calc_max_ahead()
		max_ahead_from_server = max_ahead
		var recipients: Array = state_transfer.peer_ids
		if !_startup_light_net_active(server_tick):
			state_transfer.process_server_snapshot(
				server_tick,
				is_server,
				network_active,
				listen_server,
				server_game_sim,
				use_state_compression,
				get_simulation_roster().size())
		var input_packet: PackedByteArray = PackedByteArray()
		var input_packet_meta := -1
		var input_packet_ready := false
		for id in recipients:
			if !_can_send_rpc_to_peer(int(id)):
				continue
			if SERVER_TIMING_SYNC_INTERVAL_TICKS <= 1 or server_tick % SERVER_TIMING_SYNC_INTERVAL_TICKS == 0:
				_send_server_timing_sync(int(id), server_tick, max_ahead)
			if _startup_light_net_active(server_tick):
				var startup_ack: int = server_netcode_session.get_peer_last_received(id)
				acc_log_out(12)
				_server_startup_sync.rpc_id(id, _pack_race_phase_tick(server_tick), startup_ack, target_tick, max_ahead)
				continue
			if not input_packet_ready:
				input_packet = server_netcode_session.build_authoritative_input_packet(server_tick, AUTH_INPUT_REDUNDANCY_FRAMES, race_netplay_phase)
				if input_packet.size() > 0:
					input_packet_meta = int(input_packet[0])
					if _authoritative_input_meta_can_strip(input_packet_meta):
						input_packet = input_packet.slice(1)
					else:
						input_packet_meta = -1
				input_packet_ready = true
			log_flat_server_payload_out += input_packet.size()
			log_auth_packets_sent += 1
			acc_log_out(12 + input_packet.size())
			_server_broadcast_flat.rpc_id(id, _pack_authoritative_input_tick(server_tick, input_packet_meta), input_packet, server_netcode_session.get_peer_last_received(id))
		server_tick += 1
		race_results.set_race_tick(server_tick)
		var _t1 := Time.get_ticks_usec()
		log_net_cpu_us_interval += _t1 - _t0

func _check_client_stalls() -> void:
	if !race_active:
		return
	if not is_server or server_game_sim == null or not server_game_sim.sim_started:
		return
	# Stall detection begins after the first complete frame is available.
	if server_tick >= target_tick:
		return
	var waiting = pending_inputs.get(server_tick, {})
	var now := 0.001 * float(Time.get_ticks_msec())
	var missing := []
	var roster_chk : Array = _get_active_human_roster()
	for id in roster_chk:
		if not waiting.has(id):
			missing.append(id)
			var native_last_input: float = server_netcode_session.get_peer_last_input_time(id)
			if native_last_input > 0.0 and now - float(native_last_input) > 10.0:
				if server_tick != 0:
					push_error("Client %s stalled, disconnecting" % str(id))
					multiplayer.disconnect_peer(id)
					disconnect_peer_requested.emit(id)
	if missing.size() > 0:
		_fill_delayed_missing_inputs_for_tick(server_tick)

func _handle_state(tick: int, state: PackedByteArray) -> void:
	if !race_active:
		return
	if game_sim == null:
		return
	if tick == -1:
		return
	var __prof_t0 := Time.get_ticks_usec()
	game_sim.set_state_data(tick, state)
	game_sim.load_state(tick)
	state_transfer.mark_state_restored(tick)
	local_tick = max(local_tick, tick + 1)
	var old_time := Time.get_ticks_usec()
	netcode_session.replay_history(game_sim, tick + 1, local_tick)
	var new_time := Time.get_ticks_usec()
	#DebugDraw2D.set_text("rollback frametime microseconds", new_time - old_time)
	rollback_frametime_us = new_time - old_time
	log_rollback_us_sum += rollback_frametime_us
	log_rollback_us_count += 1
	if rollback_frametime_us > log_rollback_us_max:
		log_rollback_us_max = rollback_frametime_us
	var __prof_t1 := Time.get_ticks_usec()
	prof_handle_state_us_interval += __prof_t1 - __prof_t0

func _handle_input_update_flat(tick: int) -> void:
	if !race_active:
		return
	if game_sim == null:
		return
	if tick < 0:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if tick == 0:
		return
	if tick >= local_tick:
		return
	_recalculate_future_predictions(tick)
	game_sim.load_state(maxi(state_transfer.latest_state_tick, tick - 1))
	var old_time := Time.get_ticks_usec()
	netcode_session.replay_history(game_sim, maxi(state_transfer.latest_state_tick + 1, tick), local_tick)
	var new_time := Time.get_ticks_usec()
	rollback_frametime_us = new_time - old_time
	var __prof_t1 := Time.get_ticks_usec()
	prof_handle_input_update_us_interval += __prof_t1 - __prof_t0

func _recalculate_future_predictions(start_tick: int) -> void:
	if !race_active:
		return
	netcode_session.recalculate_predictions(start_tick, local_tick)

func _record_rtt_sample(sample_s: float) -> void:
	sample_s = maxf(sample_s, 0.0)
	if rtt_s <= 0.0:
		rtt_s = sample_s
		rtt_variance_s = 0.0
	else:
		var deviation := absf(sample_s - rtt_s)
		rtt_variance_s = lerp(rtt_variance_s, deviation, RTT_VARIANCE_SMOOTHING)
		rtt_s = lerp(rtt_s, sample_s, RTT_SMOOTHING)
	_update_desired_ahead()

func _update_desired_ahead() -> void:
	if is_server and listen_server:
		desired_ahead_ticks = 0.0
		return
	var jitter_buffer := clampf(rtt_variance_s * RTT_VARIANCE_MULTIPLIER, MIN_JITTER_BUFFER_SEC, MAX_JITTER_BUFFER_SEC)
	desired_ahead_ticks = clampf((rtt_s + jitter_buffer) / base_wait_time, 2.0, float(MAX_AHEAD_TICKS))

func _adjust_time_scale() -> void:
	var __prof_t0 := Time.get_ticks_usec()
	#DebugDraw2D.set_text("playercount", player_ids.size())
	#DebugDraw2D.set_text("rtt", rtt_s)
	#if is_server:
		#DebugDraw2D.set_text("server_tick", server_tick)
		#DebugDraw2D.set_text("target_tick", target_tick)
	if is_server and !listen_server:
		return
	if !is_server and race_admission.first_authoritative_input_msec < 0:
		log_client_pre_auth_adjust_samples += 1
		use_physics_ticks = 1.0
		Engine.physics_ticks_per_second = 60
		var __prof_t1_no_auth := Time.get_ticks_usec()
		prof_adjust_time_scale_us_interval += __prof_t1_no_auth - __prof_t0
		return
	var local_desired_ahead := _local_desired_ahead_for_shared()
	var current_ahead_ticks = local_tick - clients_target_tick
	var shared_ahead_limit = max(local_desired_ahead, SHARED_AHEAD_CAP_TICKS)
	var shared_ahead_target = min(clients_max_ahead_from_server, local_desired_ahead + SHARED_AHEAD_EXTRA_TICKS, shared_ahead_limit)
	var target_ahead_ticks = max(local_desired_ahead, shared_ahead_target)
	var diff = target_ahead_ticks - current_ahead_ticks
	_log_client_ahead_sample(float(current_ahead_ticks), float(target_ahead_ticks))
	#DebugDraw2D.set_text("local_tick", local_tick)
	#DebugDraw2D.set_text("clients_server_tick", clients_server_tick)
	#DebugDraw2D.set_text("clients_target_tick", clients_target_tick)
	#DebugDraw2D.set_text("desired_ahead_ticks", desired_ahead_ticks)
	#DebugDraw2D.set_text("server_max_ahead", clients_max_ahead_from_server)
	#DebugDraw2D.set_text("target_ahead_ticks", target_ahead_ticks)
	#DebugDraw2D.set_text("current_ahead_ticks", current_ahead_ticks)
	#DebugDraw2D.set_text("diff", diff)
	#DebugDraw2D.set_text("Engine.physics_ticks_per_second", Engine.physics_ticks_per_second)
	if abs(diff) <= 1:
		use_physics_ticks = lerp(use_physics_ticks, 1.0, RTT_SMOOTHING)
		Engine.physics_ticks_per_second = roundi(use_physics_ticks * 60.0);
		return
	if diff > 0:
		use_physics_ticks = clamp(use_physics_ticks + SPEED_ADJUST_STEP * absf(diff), 1.0, 2.0)
	else:
		use_physics_ticks = clamp(use_physics_ticks - SPEED_ADJUST_STEP * absf(diff), 0.5, 1.0)
	Engine.physics_ticks_per_second = roundi(use_physics_ticks * 60.0);
	var __prof_t1 := Time.get_ticks_usec()
	prof_adjust_time_scale_us_interval += __prof_t1 - __prof_t0
	# game simulation uses a fixed delta time
	# this just changes the rate at which we simulate the game locally
	# to catch up or slow down to try and match the server

func get_race_tick() -> int:
	return server_tick if is_server else clients_server_tick
