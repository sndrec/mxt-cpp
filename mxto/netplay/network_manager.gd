class_name NetworkManager
extends Node

signal race_started(track_index, player_settings)
signal race_finished

@rpc("any_peer", "reliable")
func set_race_finish_time(time: int) -> void:
	if !race_active:
		return
	net_race_finish_time = time

func send_race_finish_time(time: int) -> void:
	if is_server:
		set_race_finish_time.rpc(time)
		set_race_finish_time(time)

const PlayerInputClass = preload("res://player/player_input.gd")
var NEUTRAL_INPUT_BYTES : PackedByteArray = PlayerInputClass.new().serialize()

@onready var game_manager: GameManager = $".."

var is_server: bool = false
var listen_server: bool = false
var player_ids: Array = []
var spectator_ids: Array = []
var waiting_peers: Array = []
var race_player_ids: Array = []
var _disconnected_during_race := {}
var pending_inputs := {}
var authoritative_inputs := {}
var input_history := {}
var last_input_time := {}
var last_local_input_bytes : PackedByteArray = NEUTRAL_INPUT_BYTES.duplicate()
var sent_inputs_bytes := {}
var server_tick: int = 0
var local_tick: int = 0
var spawn_seed: int = 0
const INPUT_HISTORY_SIZE := 30
var game_sim: GameSim
var server_game_sim: GameSim
var netcode_session := NetcodeSession.new()
var server_netcode_session := NetcodeSession.new()
var last_received_tick := {}
var last_ack_tick: int = -1
var target_tick: int = 0
const MAX_AHEAD_TICKS := 30
const MAX_HISTORY_TICKS := 60
const INPUT_RETRANSMIT_RECENT_TICKS := 5
const INPUT_RETRANSMIT_STARTUP_TICKS := 64
const STARTUP_LIGHT_NET_TICKS := 120
var sent_input_times := {}
var rtt_s: float = 0.0
var desired_ahead_ticks: float = 2.0
var base_wait_time: float = 1.0 / 60.0
const JITTER_BUFFER := 0.016
const RTT_SMOOTHING := 0.1
const SPEED_ADJUST_STEP := 0.0003
const START_SYNC_SAMPLE_COUNT := 4
const START_SYNC_PING_INTERVAL_MS := 50
const START_SYNC_START_DELAY_MS := 750
var player_settings := {}
var ready_players : Array[int] = []
const STATE_BROADCAST_INTERVAL_TICKS := 60
var state_send_offsets := {}
var net_race_finish_time := -1
var player_finish_times := {}
var player_finish_placements := {}
var finish_order : Array = []
var max_ahead_from_server: float = 0.0
var peer_desired_ahead := {}

const CPU_ID_MIN := 1
const CPU_ID_MAX := 999
var cpu_player_ids: Array = []
var race_cpu_player_ids: Array = []
var cpu_player_settings := {}
var cpu_driver_manager: CpuDriverManager

var clients_server_tick = 0
var clients_target_tick = 0
var last_target_tick_update = 0
var clients_max_ahead_from_server = 2.0
var authoritative_history := {}
var authoritative_acks := {}
var last_server_input_tick := -1
var latest_state_tick := -1
var race_active: bool = false
var start_sync_active := false
var start_sync_scheduled := false
var start_sync_server_start_msec := 0
var start_sync_local_start_msec := 0
var start_sync_authoritative_started := false
var start_sync_client_started := false
var start_sync_last_ping_msec := 0
var start_sync_seq := 0
var start_sync_client_sample_count := 0
var start_sync_server_offset_msec := 0.0
var start_sync_sample_counts := {}
var start_sync_peer_ahead := {}
var start_sync_initial_max_ahead := 2.0
var start_sync_actual_client_start_msec := -1
var start_sync_actual_server_start_msec := -1
var start_sync_first_authoritative_input_msec := -1
var start_sync_first_authoritative_first_tick := -1
var start_sync_first_authoritative_last_tick := -1
var start_sync_first_authoritative_count := 0
var start_sync_debug_prints := 0

var use_state_compression := true

var log_enabled := true
var log_file: FileAccess
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
var log_server_late_drops := 0
var log_server_replacements := 0
const STATE_FRAGMENT_PAYLOAD_BYTES := 1200
var log_state_raw_out := 0
var log_state_payload_out := 0
var log_state_sent_count := 0
var log_state_max_fragments_out := 0
var log_state_min_success_2pct := 1.0
var log_state_payload_in := 0
var log_state_raw_in := 0
var log_state_recv_count := 0
var log_state_max_recv_gap_ms := 0
var log_state_last_recv_ms := -1
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
var _log_sent_counts := {}
var _log_timer: Timer
var rollback_frametime_us := 0
var net_input_debug_prints := 0

func _startup_light_net_active(tick: int) -> bool:
	return tick >= 0 and tick < STARTUP_LIGHT_NET_TICKS

func _store_neutral_authoritative_frame_for_all_racers(tick: int) -> void:
	for id in get_simulation_roster():
		netcode_session.store_authoritative_input(tick, int(id), NEUTRAL_INPUT_BYTES)

func _estimate_state_fragments(byte_count: int) -> int:
	if byte_count <= 0:
		return 0
	return int(ceil(float(byte_count) / float(STATE_FRAGMENT_PAYLOAD_BYTES)))

func _state_success_probability_at_2pct_loss(fragment_count: int) -> float:
	if fragment_count <= 0:
		return 1.0
	return pow(0.98, float(fragment_count))

func _log_state_sent(raw_size: int, payload_size: int) -> void:
	if payload_size <= 0:
		return
	var fragments := _estimate_state_fragments(payload_size)
	log_state_raw_out += max(raw_size, 0)
	log_state_payload_out += payload_size
	log_state_sent_count += 1
	log_state_max_fragments_out = max(log_state_max_fragments_out, fragments)
	log_state_min_success_2pct = min(log_state_min_success_2pct, _state_success_probability_at_2pct_loss(fragments))

func _log_state_received(raw_size: int, payload_size: int) -> void:
	if payload_size <= 0:
		return
	var now := Time.get_ticks_msec()
	if log_state_last_recv_ms >= 0:
		log_state_max_recv_gap_ms = max(log_state_max_recv_gap_ms, now - log_state_last_recv_ms)
	log_state_last_recv_ms = now
	log_state_payload_in += payload_size
	log_state_raw_in += max(raw_size, 0)
	log_state_recv_count += 1

var version_string: String = ""
var _unverified_peers: Array = []
var _version_request_time := {}

func _init_logger() -> void:
	if !log_enabled:
		return
	var dir := DirAccess.open("user://")
	if dir and !dir.dir_exists("logs"):
		dir.make_dir("logs")
	var role := "server" if is_server and !listen_server else ("listen" if is_server else "client")
	var fname := "logs/" + role + "-" + str(Time.get_unix_time_from_system()) + "-" + str(multiplayer.get_unique_id()) + ".log"
	log_file = FileAccess.open("user://" + fname, FileAccess.WRITE)
	if log_file:
		log_file.store_line("time,role,uid,is_server,listen,players,server_tick,target_tick,local_tick,clients_server_tick,clients_target_tick,rtt,desired_ahead,server_max_ahead,physics_tps,start_server_ms,start_local_ms,actual_client_start_ms,actual_server_start_ms,first_auth_ms,first_auth_first_tick,first_auth_last_tick,first_auth_count,up_kbps,down_kbps,up_total_kb,down_total_kb,inputs_sent,inputs_acked,retrans,flat_client_out,flat_client_in,flat_server_out,flat_server_in,late_drops,replacements,state_raw_out,state_payload_out,state_sent,state_max_frags_out,state_min_success_2pct,state_payload_in,state_raw_in,state_recv,state_max_recv_gap_ms,auth_packets,auth_frames,auth_baseline_inputs,auth_delta_frames,auth_delta_changed_inputs,auth_delta_unchanged_inputs,net_cpu_ms,sim_cpu_ms,rollback_avg_ms,rollback_max_ms,collect_inputs_ms,idle_broadcast_ms,check_client_stalls_ms,client_send_input_ms,server_broadcast_recv_ms,handle_state_ms,handle_input_update_ms,recalc_pred_ms,adjust_time_scale_ms,car_store_old_pos_ms,car_post_render_ms")
	_log_timer = Timer.new()
	_log_timer.wait_time = 1.0
	_log_timer.one_shot = false
	_log_timer.timeout.connect(_flush_log)
	add_child(_log_timer)
	_log_timer.start()

func set_cpu_driver_manager(manager: CpuDriverManager) -> void:
	cpu_driver_manager = manager
	if cpu_driver_manager != null:
		cpu_driver_manager.configure_drivers(cpu_player_ids)

func _allocate_cpu_id() -> int:
	for id in range(CPU_ID_MIN, CPU_ID_MAX + 1):
		if cpu_player_ids.has(id):
			continue
		if player_ids.has(id):
			continue
		if spectator_ids.has(id):
			continue
		return id
	return -1

func _remap_cpu_id(old_id: int, reason: String) -> int:
	if !cpu_player_ids.has(old_id):
		return old_id
	var settings = cpu_player_settings.get(old_id, player_settings.get(old_id, {}))
	cpu_player_ids.erase(old_id)
	cpu_player_settings.erase(old_id)
	player_settings.erase(old_id)
	var new_id := _allocate_cpu_id()
	if new_id == -1:
		return -1
	cpu_player_ids.append(new_id)
	cpu_player_settings[new_id] = settings
	player_settings[new_id] = settings
	if race_cpu_player_ids.has(old_id):
		race_cpu_player_ids.erase(old_id)
		race_cpu_player_ids.append(new_id)
	return new_id

func _ensure_cpu_ids_do_not_overlap_humans(reason: String) -> bool:
	var changed := false
	var reserved := player_ids.duplicate(true)
	reserved.append_array(spectator_ids)
	for id in reserved:
		if cpu_player_ids.has(id):
			_remap_cpu_id(id, reason)
			changed = true
	if changed:
		_sync_cpu_manager()
	return changed

func _cpu_human_overlaps() -> Array:
	var overlaps: Array = []
	var reserved := player_ids.duplicate(true)
	reserved.append_array(spectator_ids)
	for id in reserved:
		if cpu_player_ids.has(id):
			overlaps.append(id)
	return overlaps

func prepare_race_roster(reason: String) -> void:
	var changed := false
	if is_server or !multiplayer.has_multiplayer_peer():
		changed = _ensure_cpu_ids_do_not_overlap_humans(reason)
	if changed and is_server and !race_active:
		_broadcast_cpu_roster()

func _reset_start_sync_state() -> void:
	start_sync_active = false
	start_sync_scheduled = false
	start_sync_server_start_msec = 0
	start_sync_local_start_msec = 0
	start_sync_authoritative_started = false
	start_sync_client_started = false
	start_sync_last_ping_msec = 0
	start_sync_seq = 0
	start_sync_client_sample_count = 0
	start_sync_server_offset_msec = 0.0
	start_sync_sample_counts.clear()
	start_sync_peer_ahead.clear()
	start_sync_initial_max_ahead = 2.0
	start_sync_actual_client_start_msec = -1
	start_sync_actual_server_start_msec = -1
	start_sync_first_authoritative_input_msec = -1
	start_sync_first_authoritative_first_tick = -1
	start_sync_first_authoritative_last_tick = -1
	start_sync_first_authoritative_count = 0
	start_sync_debug_prints = 0

func set_cpu_driver_count(count: int) -> void:
	count = clamp(count, 0, CPU_ID_MAX - CPU_ID_MIN + 1)
	while cpu_player_ids.size() < count:
		_add_cpu_driver_internal()
	while cpu_player_ids.size() > count:
		_remove_cpu_driver_internal()
	_sync_cpu_manager()
	_broadcast_cpu_roster()

func set_singleplayer_cpu_count(count: int) -> void:
	set_cpu_driver_count(count)

func add_cpu_driver() -> void:
	set_cpu_driver_count(cpu_player_ids.size() + 1)

func remove_cpu_driver() -> void:
	if cpu_player_ids.size() == 0:
		return
	set_cpu_driver_count(cpu_player_ids.size() - 1)

func _add_cpu_driver_internal() -> void:
	var new_id := _allocate_cpu_id()
	if new_id == -1:
		return
	cpu_player_ids.append(new_id)
	var index := cpu_player_ids.size() - 1
	var settings := game_manager.build_cpu_player_settings(index)
	cpu_player_settings[new_id] = settings
	player_settings[new_id] = settings

func _remove_cpu_driver_internal() -> void:
	if cpu_player_ids.is_empty():
		return
	var removed_id = cpu_player_ids.pop_back()
	cpu_player_settings.erase(removed_id)
	player_settings.erase(removed_id)
	if race_cpu_player_ids.has(removed_id):
		race_cpu_player_ids.erase(removed_id)
	if pending_inputs.has(server_tick):
		pending_inputs[server_tick].erase(removed_id)
	for key in pending_inputs.keys():
		pending_inputs[key].erase(removed_id)

func _collect_cpu_settings_array() -> Array:
	var arr: Array = []
	for id in cpu_player_ids:
		arr.append(cpu_player_settings.get(id, {}))
	return arr

@rpc("any_peer", "reliable")
func sync_cpu_roster(ids: Array, settings_array: Array) -> void:
	_apply_cpu_roster(ids, settings_array)

func _apply_cpu_roster(ids: Array, settings_array: Array) -> void:
	var previous := cpu_player_ids.duplicate(true)
	cpu_player_ids = ids.duplicate(true)
	cpu_player_settings.clear()
	for old_id in previous:
		if !cpu_player_ids.has(old_id):
			player_settings.erase(old_id)
	for i in range(ids.size()):
		var id = ids[i]
		var settings = {}
		if i < settings_array.size():
			settings = settings_array[i]
		cpu_player_settings[id] = settings
		player_settings[id] = settings
	_sync_cpu_manager()

func _broadcast_cpu_roster() -> void:
	if !is_server:
		return
	var settings_array := _collect_cpu_settings_array()
	_apply_cpu_roster(cpu_player_ids, settings_array)
	if cpu_player_ids.size() == 0:
		sync_cpu_roster.rpc([], [])
	else:
		sync_cpu_roster.rpc(cpu_player_ids, settings_array)

func _sync_cpu_manager() -> void:
	if cpu_driver_manager != null and (is_server or !multiplayer.has_multiplayer_peer()):
		var roster := _get_cpu_roster()
		cpu_driver_manager.configure_drivers(roster)

func get_cpu_roster() -> Array:
	return _get_cpu_roster()

func _get_cpu_roster() -> Array:
	return race_cpu_player_ids.duplicate(true) if race_cpu_player_ids.size() > 0 else cpu_player_ids.duplicate(true)

func _get_human_roster() -> Array:
	return race_player_ids.duplicate(true) if race_player_ids.size() > 0 else player_ids.duplicate(true)

func get_simulation_roster() -> Array:
	var roster := _get_human_roster()
	roster.append_array(_get_cpu_roster())
	return roster

func _flush_log() -> void:
	if !log_enabled or log_file == null:
		return
	var up_kbps := (log_bytes_out_interval * 8.0) / 1000.0
	var down_kbps := (log_bytes_in_interval * 8.0) / 1000.0
	var physics_tps := Engine.physics_ticks_per_second
	var role := "server" if is_server and !listen_server else ("listen" if is_server else "client")
	var rollback_avg_ms = (float(log_rollback_us_sum) / max(log_rollback_us_count, 1)) / 1000.0
	var rollback_max_ms := float(log_rollback_us_max) / 1000.0
	var net_cpu_ms := float(log_net_cpu_us_interval) / 1000.0
	var sim_cpu_ms := float(log_sim_cpu_us_interval) / 1000.0
	var collect_inputs_ms := float(prof_collect_server_inputs_us_interval) / 1000.0
	var idle_broadcast_ms := float(prof_idle_broadcast_us_interval) / 1000.0
	var check_client_stalls_ms := float(prof_check_client_stalls_us_interval) / 1000.0
	var client_send_input_ms := float(prof_client_send_input_us_interval) / 1000.0
	var server_broadcast_recv_ms := float(prof_server_broadcast_recv_us_interval) / 1000.0
	var handle_state_ms := float(prof_handle_state_us_interval) / 1000.0
	var handle_input_update_ms := float(prof_handle_input_update_us_interval) / 1000.0
	var recalc_pred_ms := float(prof_recalc_pred_us_interval) / 1000.0
	var adjust_time_scale_ms := float(prof_adjust_time_scale_us_interval) / 1000.0
	var car_store_old_pos_ms := float(prof_car_store_old_pos_us_interval) / 1000.0
	var car_post_render_ms := float(prof_car_post_render_us_interval) / 1000.0
	var auth_stats: Dictionary = server_netcode_session.consume_authoritative_packet_stats()
	var auth_packets := int(auth_stats.get("auth_packets", 0))
	var auth_frames := int(auth_stats.get("auth_frames", 0))
	var auth_baseline_inputs := int(auth_stats.get("auth_baseline_inputs", 0))
	var auth_delta_frames := int(auth_stats.get("auth_delta_frames", 0))
	var auth_delta_changed_inputs := int(auth_stats.get("auth_delta_changed_inputs", 0))
	var auth_delta_unchanged_inputs := int(auth_stats.get("auth_delta_unchanged_inputs", 0))
	var state_success := log_state_min_success_2pct if log_state_sent_count > 0 else 1.0

	var logged_max_ahead: float = max_ahead_from_server if is_server else clients_max_ahead_from_server
	var line := str(Time.get_ticks_msec()) + "," + role + "," + str(multiplayer.get_unique_id()) + "," + str(is_server) + "," + str(listen_server) + "," + str(player_ids.size()) + "," + str(server_tick) + "," + str(target_tick) + "," + str(local_tick) + "," + str(clients_server_tick) + "," + str(clients_target_tick) + "," + str(rtt_s) + "," + str(desired_ahead_ticks) + "," + str(logged_max_ahead) + "," + str(physics_tps) + "," + str(start_sync_server_start_msec) + "," + str(start_sync_local_start_msec) + "," + str(start_sync_actual_client_start_msec) + "," + str(start_sync_actual_server_start_msec) + "," + str(start_sync_first_authoritative_input_msec) + "," + str(start_sync_first_authoritative_first_tick) + "," + str(start_sync_first_authoritative_last_tick) + "," + str(start_sync_first_authoritative_count) + "," + str(up_kbps) + "," + str(down_kbps) + "," + str(log_bytes_out_total / 1000.0) + "," + str(log_bytes_in_total / 1000.0) + "," + str(log_inputs_sent) + "," + str(log_inputs_acked) + "," + str(log_inputs_retransmitted) + "," + str(log_flat_client_payload_out) + "," + str(log_flat_client_payload_in) + "," + str(log_flat_server_payload_out) + "," + str(log_flat_server_payload_in) + "," + str(log_server_late_drops) + "," + str(log_server_replacements) + "," + str(log_state_raw_out) + "," + str(log_state_payload_out) + "," + str(log_state_sent_count) + "," + str(log_state_max_fragments_out) + "," + str(state_success) + "," + str(log_state_payload_in) + "," + str(log_state_raw_in) + "," + str(log_state_recv_count) + "," + str(log_state_max_recv_gap_ms) + "," + str(auth_packets) + "," + str(auth_frames) + "," + str(auth_baseline_inputs) + "," + str(auth_delta_frames) + "," + str(auth_delta_changed_inputs) + "," + str(auth_delta_unchanged_inputs) + "," + str(net_cpu_ms) + "," + str(sim_cpu_ms) + "," + str(rollback_avg_ms) + "," + str(rollback_max_ms) + "," + str(collect_inputs_ms) + "," + str(idle_broadcast_ms) + "," + str(check_client_stalls_ms) + "," + str(client_send_input_ms) + "," + str(server_broadcast_recv_ms) + "," + str(handle_state_ms) + "," + str(handle_input_update_ms) + "," + str(recalc_pred_ms) + "," + str(adjust_time_scale_ms) + "," + str(car_store_old_pos_ms) + "," + str(car_post_render_ms)
	log_file.store_line(line)
	log_file.flush()
	log_bytes_out_interval = 0
	log_bytes_in_interval = 0
	log_net_cpu_us_interval = 0
	log_rollback_us_sum = 0
	log_rollback_us_count = 0
	log_rollback_us_max = 0
	log_inputs_retransmitted = 0
	log_flat_client_payload_out = 0
	log_flat_client_payload_in = 0
	log_flat_server_payload_out = 0
	log_flat_server_payload_in = 0
	log_state_raw_out = 0
	log_state_payload_out = 0
	log_state_sent_count = 0
	log_state_max_fragments_out = 0
	log_state_min_success_2pct = 1.0
	log_state_payload_in = 0
	log_state_raw_in = 0
	log_state_recv_count = 0
	log_state_max_recv_gap_ms = 0
	log_sim_cpu_us_interval = 0
	prof_collect_server_inputs_us_interval = 0
	prof_idle_broadcast_us_interval = 0
	prof_check_client_stalls_us_interval = 0
	prof_client_send_input_us_interval = 0
	prof_server_broadcast_recv_us_interval = 0
	prof_handle_state_us_interval = 0
	prof_handle_input_update_us_interval = 0
	prof_recalc_pred_us_interval = 0
	prof_adjust_time_scale_us_interval = 0
	prof_car_store_old_pos_us_interval = 0
	prof_car_post_render_us_interval = 0

func _acc_log_out(bytes: int) -> void:
	log_bytes_out_interval += bytes
	log_bytes_out_total += bytes

func _acc_log_in(bytes: int) -> void:
	log_bytes_in_interval += bytes
	log_bytes_in_total += bytes

static func _estimate_pba_array_size(arr: Array) -> int:
	var total := 0
	for e in arr:
		if typeof(e) == TYPE_PACKED_BYTE_ARRAY:
			total += (e as PackedByteArray).size()
	return total

static func _estimate_nested_inputs_size(inputs: Array) -> int:
	var total := 0
	for frame in inputs:
		if typeof(frame) == TYPE_ARRAY:
			total += _estimate_pba_array_size(frame)
		elif typeof(frame) == TYPE_DICTIONARY:
			for v in (frame as Dictionary).values():
				if typeof(v) == TYPE_PACKED_BYTE_ARRAY:
					total += (v as PackedByteArray).size()
	return total

func reset_race_state() -> void:
	race_active = false
	race_player_ids.clear()
	race_cpu_player_ids.clear()
	_disconnected_during_race.clear()
	pending_inputs.clear()
	authoritative_inputs.clear()
	input_history.clear()
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
	net_race_finish_time = -1
	player_finish_times.clear()
	player_finish_placements.clear()
	finish_order.clear()
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	authoritative_acks.clear()
	last_server_input_tick = -1
	latest_state_tick = -1
	use_physics_ticks = 1.0
	last_target_tick_update = Time.get_ticks_msec()
	desired_ahead_ticks = 0.0 if is_server and !listen_server else 2.0
	net_input_debug_prints = 0
	_reset_start_sync_state()
	player_settings.clear()
	for id in cpu_player_ids:
		var settings = cpu_player_settings.get(id, {})
		player_settings[id] = settings
	netcode_session.reset()
	server_netcode_session.reset()
	server_netcode_session.clear_peer_state()
	_sync_cpu_manager()

func _calc_state_offsets() -> void:
	if not is_server:
		return
	state_send_offsets.clear()
	var count := player_ids.size()
	if count == 0:
		return
	for i in range(count):
		var id = player_ids[i]
		state_send_offsets[id] = int(round(float(STATE_BROADCAST_INTERVAL_TICKS) * float(i) / float(count)))

func _calc_max_ahead() -> float:
	return float(server_netcode_session.get_max_peer_desired_ahead(player_ids, desired_ahead_ticks))

func _ready() -> void:
	var lbl: Label = get_node_or_null("../VersionLabel")
	if lbl != null:
		version_string = str(lbl.text)
	else:
		version_string = ""
	var server_process_timer = Timer.new()
	server_process_timer.ignore_time_scale = true
	add_child(server_process_timer)
	server_process_timer.timeout.connect(server_process)
	server_process_timer.start(1.0 / 60.0)
	multiplayer.server_disconnected.connect(on_disconnect)

func on_disconnect() -> void:
	DebugDraw2D.set_text("DISCONNECTED!", null, 10, Color.RED, 10)
	disconnect_from_server()

func server_process() -> void:
	if !race_active:
		return
	_process_start_sync()
	if is_server and server_game_sim != null and server_game_sim.sim_started:
		target_tick += 1
		if target_tick > server_tick + MAX_AHEAD_TICKS:
			target_tick = server_tick + MAX_AHEAD_TICKS
		var loops := 0
		const MAX_SERVER_TICKS_PER_PROCESS := 8
		while server_tick < target_tick and loops < MAX_SERVER_TICKS_PER_PROCESS:
			var _collect_t0 := Time.get_ticks_usec()
			var server_inputs := collect_server_inputs()
			var _collect_t1 := Time.get_ticks_usec()
			prof_collect_server_inputs_us_interval += _collect_t1 - _collect_t0
			if server_inputs.is_empty():
				break
			var _net_t0 := Time.get_ticks_usec()
			post_tick()
			var _net_t1 := Time.get_ticks_usec()
			log_net_cpu_us_interval += _net_t1 - _net_t0
			loops += 1
		if server_tick < target_tick:
			var _idle_t0 := Time.get_ticks_usec()
			_idle_broadcast()
			var _idle_t1 := Time.get_ticks_usec()
			log_net_cpu_us_interval += _idle_t1 - _idle_t0
			prof_idle_broadcast_us_interval += _idle_t1 - _idle_t0
			var _stall_t0 := Time.get_ticks_usec()
			_check_client_stalls()
			var _stall_t1 := Time.get_ticks_usec()
			prof_check_client_stalls_us_interval += _stall_t1 - _stall_t0
	if !is_server and !listen_server and Time.get_ticks_msec() > last_target_tick_update + 17 and game_sim != null and game_sim.sim_started:
		clients_target_tick += 1
		if clients_target_tick > clients_server_tick + MAX_AHEAD_TICKS:
			clients_target_tick = clients_server_tick + MAX_AHEAD_TICKS

func _process_start_sync() -> void:
	if !race_active:
		return
	var now := Time.get_ticks_msec()
	if !is_server and !listen_server and game_sim != null and !game_sim.sim_started:
		if now >= start_sync_last_ping_msec + START_SYNC_PING_INTERVAL_MS and !start_sync_scheduled:
			start_sync_last_ping_msec = now
			start_sync_seq += 1
			_start_sync_ping.rpc_id(1, now, start_sync_seq)
	if start_sync_scheduled:
		if !start_sync_client_started and game_sim != null and !game_sim.sim_started and now >= start_sync_local_start_msec:
			var initial_target := int(ceil(clamp(desired_ahead_ticks, 0.0, float(MAX_AHEAD_TICKS))))
			_begin_client_simulation_now(initial_target)
		if is_server and !start_sync_authoritative_started and server_game_sim != null and !server_game_sim.sim_started and now >= start_sync_server_start_msec:
			_begin_authoritative_simulation_now()

@rpc("any_peer", "unreliable")
func _start_sync_ping(client_send_msec: int, seq: int) -> void:
	if !race_active or !is_server or start_sync_scheduled:
		return
	var sender := multiplayer.get_remote_sender_id()
	if !player_ids.has(sender):
		return
	_start_sync_pong.rpc_id(sender, client_send_msec, Time.get_ticks_msec(), seq)

@rpc("any_peer", "unreliable")
func _start_sync_pong(client_send_msec: int, server_recv_msec: int, seq: int) -> void:
	if !race_active or is_server:
		return
	var now := Time.get_ticks_msec()
	var sample_rtt_s: float = max(0.0, 0.001 * float(now - client_send_msec))
	if rtt_s == 0.0:
		rtt_s = sample_rtt_s
	else:
		rtt_s = lerp(rtt_s, sample_rtt_s, RTT_SMOOTHING)
	var midpoint := 0.5 * float(client_send_msec + now)
	var sample_offset := float(server_recv_msec) - midpoint
	if start_sync_client_sample_count == 0:
		start_sync_server_offset_msec = sample_offset
	else:
		start_sync_server_offset_msec = lerp(start_sync_server_offset_msec, sample_offset, 0.35)
	start_sync_client_sample_count += 1
	_update_desired_ahead()
	_client_start_sync_sample.rpc_id(1, seq, rtt_s, desired_ahead_ticks)

@rpc("any_peer", "unreliable")
func _client_start_sync_sample(seq: int, client_rtt_s: float, client_ahead: float) -> void:
	if !race_active or !is_server or start_sync_scheduled:
		return
	var sender := multiplayer.get_remote_sender_id()
	if !player_ids.has(sender):
		return
	start_sync_sample_counts[sender] = int(start_sync_sample_counts.get(sender, 0)) + 1
	start_sync_peer_ahead[sender] = clamp(client_ahead, 0.0, float(MAX_AHEAD_TICKS))
	peer_desired_ahead[sender] = start_sync_peer_ahead[sender]
	server_netcode_session.set_peer_desired_ahead(sender, start_sync_peer_ahead[sender])
	_try_schedule_synced_start()

func host(port: int = 27016, max_players: int = 64, dedicated: bool = false) -> int:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_players)
	if err != OK:
		push_error("Failed to host: %s" % err)
		return err
	push_error("Host!")
	multiplayer.multiplayer_peer = peer
	is_server = true
	listen_server = !dedicated
	server_tick = 0
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	server_netcode_session.clear_peer_state()
	desired_ahead_ticks = 2.0 if listen_server else 0.0
	sent_input_times.clear()
	last_input_time.clear()
	last_received_tick.clear()
	server_netcode_session.clear_peer_state()
	input_history.clear()
	sent_inputs_bytes.clear()
	last_local_input_bytes = NEUTRAL_INPUT_BYTES.duplicate()
	player_ids = [multiplayer.get_unique_id()]
	_ensure_cpu_ids_do_not_overlap_humans("host")
	player_settings.clear()
	for id in cpu_player_ids:
		var settings = cpu_player_settings.get(id, {})
		player_settings[id] = settings
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	authoritative_acks.clear()
	server_netcode_session.clear_peer_state()
	last_server_input_tick = -1
	latest_state_tick = -1
	net_input_debug_prints = 0
	_reset_start_sync_state()
	race_cpu_player_ids.clear()
	get_window().title = "Host"
	if !multiplayer.peer_connected.is_connected(_on_peer_connected):
		multiplayer.peer_connected.connect(_on_peer_connected)
	if !multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	_calc_state_offsets()
	_sync_cpu_manager()
	_broadcast_cpu_roster()
	if log_file == null:
		_init_logger()
	return OK

func join(ip: String, port: int = 27016) -> int:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client(ip, port)
	if err != OK:
		push_error("Failed to join server: %s" % err)
		return err
	push_error("Client!")
	multiplayer.multiplayer_peer = peer
	is_server = false
	listen_server = false
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	desired_ahead_ticks = 2.0
	sent_input_times.clear()
	last_input_time.clear()
	input_history.clear()
	sent_inputs_bytes.clear()
	last_local_input_bytes = NEUTRAL_INPUT_BYTES.duplicate()
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	authoritative_acks.clear()
	last_server_input_tick = -1
	latest_state_tick = -1
	net_input_debug_prints = 0
	_reset_start_sync_state()
	player_ids = [multiplayer.get_unique_id()]
	player_settings.clear()
	for id in cpu_player_ids:
		var settings = cpu_player_settings.get(id, {})
		player_settings[id] = settings
	cpu_player_ids.clear()
	cpu_player_settings.clear()
	race_cpu_player_ids.clear()
	get_window().title = "Client " + str(multiplayer.get_unique_id())
	if log_file == null:
		_init_logger()
	return OK

func _on_peer_connected(id: int) -> void:
	if is_server:
		if !_unverified_peers.has(id):
			_unverified_peers.append(id)
		_version_request_time[id] = 0.001 * float(Time.get_ticks_msec())
		_request_client_version.rpc_id(id, version_string)
func _on_peer_disconnected(id: int) -> void:
	if is_server:
		if _unverified_peers.has(id):
			_unverified_peers.erase(id)
		if _version_request_time.has(id):
			_version_request_time.erase(id)
		if waiting_peers.has(id):
			waiting_peers.erase(id)
			return
		if player_ids.has(id):
			player_ids.erase(id)
			if race_active:
				_disconnected_during_race[id] = true
		if spectator_ids.has(id):
			spectator_ids.erase(id)
		if last_input_time.has(id):
			last_input_time.erase(id)
		if peer_desired_ahead.has(id):
			peer_desired_ahead.erase(id)
		if player_settings.has(id):
			player_settings.erase(id)
		if authoritative_acks.has(id):
			authoritative_acks.erase(id)
		if last_received_tick.has(id):
			last_received_tick.erase(id)
		server_netcode_session.remove_peer(id)
		for key in pending_inputs:
			if pending_inputs[key].has(id):
				pending_inputs[key].erase(id)
		if !race_active:
			_update_player_ids.rpc(player_ids)
		_calc_state_offsets()


func _accept_peer(id: int) -> void:
	if _unverified_peers.has(id):
		_unverified_peers.erase(id)
	if _version_request_time.has(id):
		_version_request_time.erase(id)
	if server_game_sim != null and server_game_sim.sim_started:
		waiting_peers.append(id)
		_update_player_ids.rpc_id(id, player_ids)
		for pid in player_settings.keys():
			update_player_settings.rpc_id(id, player_settings[pid], pid)
		return
	if !player_ids.has(id):
		player_ids.append(id)
	var cpu_ids_changed := _ensure_cpu_ids_do_not_overlap_humans("peer_accept")
	last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
	peer_desired_ahead[id] = 0.0
	server_netcode_session.set_peer_last_received(id, -1, last_input_time[id])
	server_netcode_session.set_peer_desired_ahead(id, 0.0)
	if !race_active:
		_update_player_ids.rpc(player_ids)
		if cpu_ids_changed:
			_broadcast_cpu_roster()
	for pid in player_settings.keys():
		update_player_settings.rpc_id(id, player_settings[pid], pid)
	_calc_state_offsets()

@rpc("any_peer", "reliable")
func _request_client_version(server_version: String) -> void:
	_report_client_version.rpc_id(1, version_string)

@rpc("any_peer", "reliable")
func _report_client_version(client_version: String) -> void:
	if !is_server:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if client_version != version_string:
		push_error("Rejecting client %s due to version mismatch. Server=%s Client=%s" % [str(sender_id), version_string, client_version])
		_version_rejected.rpc_id(sender_id, version_string)
		multiplayer.disconnect_peer(sender_id)
		_on_peer_disconnected(sender_id)
		return
	_accept_peer(sender_id)

@rpc("any_peer", "reliable")
func _version_rejected(server_version: String) -> void:
	DebugDraw2D.set_text("Version mismatch. Server: " + server_version, null, 10, Color.RED, 10)
func flush_waiting_peers() -> void:
	if not is_server:
		return
	var new_ids: Array = []
	for id in waiting_peers:
		if not player_ids.has(id):
			player_ids.append(id)
			last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
			peer_desired_ahead[id] = 0.0
			server_netcode_session.set_peer_last_received(id, -1, last_input_time[id])
			server_netcode_session.set_peer_desired_ahead(id, 0.0)
			new_ids.append(id)
			for pid in player_settings.keys():
				update_player_settings.rpc_id(id, player_settings[pid], pid)
	waiting_peers.clear()
	_update_player_ids.rpc(player_ids)
	_calc_state_offsets()
	for id in new_ids:
		if player_settings.has(id):
			update_player_settings.rpc(player_settings[id], id)

@rpc("any_peer", "reliable")
func _update_player_ids(ids: Array) -> void:
	player_ids = ids
	if is_server:
		_calc_state_offsets()

@rpc("any_peer", "reliable")
func start_race(track_index: int, settings: Array) -> void:
	prepare_race_roster("start_race")
	_reset_start_sync_state()
	race_active = true
	race_player_ids = player_ids.duplicate(true)
	race_cpu_player_ids = cpu_player_ids.duplicate(true)
	_sync_cpu_manager()
	emit_signal("race_started", track_index, settings)
	if is_server:
		var now := 0.001 * float(Time.get_ticks_msec())
		for id in player_ids + spectator_ids:
			last_input_time[id] = now
			server_netcode_session.set_peer_last_received(id, -1, now)
		if listen_server:
			var local_id := multiplayer.get_unique_id()
			start_sync_sample_counts[local_id] = START_SYNC_SAMPLE_COUNT
			start_sync_peer_ahead[local_id] = desired_ahead_ticks

func send_start_race(track_index: int, settings: Array) -> void:
	if is_server:
		ready_players.clear()
		# Generate and distribute a shared spawn seed before starting the race.
		# This lets all peers randomize starting grid slots deterministically.
		var seed := randi()
		set_spawn_seed.rpc(seed)
		set_spawn_seed(seed)
		start_race.rpc(track_index, settings)
		start_race(track_index, settings)
		if player_ids.size() > 1:
			if game_sim != null:
					game_sim.set_sim_started(false)
			if server_game_sim != null:
					server_game_sim.set_sim_started(false)
		else:
			begin_simulation()
	else:
		start_race.rpc_id(1, track_index, settings)

@rpc("any_peer", "reliable")
func end_race() -> void:
	race_active = false
	emit_signal("race_finished")

func send_end_race() -> void:
	if is_server:
		end_race.rpc()
		end_race()

@rpc("any_peer", "reliable")
func set_spawn_seed(seed: int) -> void:
	spawn_seed = seed
	if game_sim != null:
		game_sim.set_spawn_seed(seed)
	if is_server and server_game_sim != null:
		server_game_sim.set_spawn_seed(seed)

@rpc("any_peer", "reliable")
func client_ready() -> void:
	if !race_active:
		return
	var id := multiplayer.get_remote_sender_id()
	if id == 0:
		id = multiplayer.get_unique_id()
	if is_server:
		if !ready_players.has(id):
			ready_players.append(id)
			if ready_players.size() == player_ids.size():
				_begin_start_sync()

@rpc("any_peer", "reliable")
func begin_simulation() -> void:
	if !race_active:
		return
	_begin_client_simulation_now(0)
	_begin_authoritative_simulation_now()

func _begin_start_sync() -> void:
	if !is_server or start_sync_active or start_sync_scheduled:
		return
	start_sync_active = true
	start_sync_sample_counts.clear()
	start_sync_peer_ahead.clear()
	if listen_server:
		var local_id := multiplayer.get_unique_id()
		start_sync_sample_counts[local_id] = START_SYNC_SAMPLE_COUNT
		start_sync_peer_ahead[local_id] = desired_ahead_ticks
	_try_schedule_synced_start()

func _all_start_sync_samples_ready() -> bool:
	for id in player_ids:
		if is_server and listen_server and id == multiplayer.get_unique_id():
			continue
		if int(start_sync_sample_counts.get(id, 0)) < START_SYNC_SAMPLE_COUNT:
			return false
	return true

func _try_schedule_synced_start() -> void:
	if !is_server or !start_sync_active or start_sync_scheduled:
		return
	if ready_players.size() < player_ids.size():
		return
	if !_all_start_sync_samples_ready():
		return
	var max_ahead := desired_ahead_ticks if listen_server else 0.0
	for id in player_ids:
		max_ahead = max(max_ahead, float(start_sync_peer_ahead.get(id, 0.0)))
	start_sync_initial_max_ahead = clamp(max_ahead, 0.0, float(MAX_AHEAD_TICKS))
	var lead_msec := int(ceil(start_sync_initial_max_ahead * 1000.0 / 60.0))
	var start_delay_msec: int = max(START_SYNC_START_DELAY_MS, lead_msec + 250)
	start_sync_server_start_msec = Time.get_ticks_msec() + start_delay_msec
	start_sync_scheduled = true
	begin_simulation_at.rpc(start_sync_server_start_msec, start_sync_initial_max_ahead)
	begin_simulation_at(start_sync_server_start_msec, start_sync_initial_max_ahead)

@rpc("any_peer", "reliable")
func begin_simulation_at(server_start_msec: int, initial_max_ahead: float) -> void:
	if !race_active:
		return
	start_sync_active = true
	start_sync_scheduled = true
	start_sync_server_start_msec = server_start_msec
	start_sync_initial_max_ahead = initial_max_ahead
	clients_max_ahead_from_server = initial_max_ahead
	clients_server_tick = 0
	last_target_tick_update = Time.get_ticks_msec()
	var client_lead_msec := int(ceil(clamp(desired_ahead_ticks, 0.0, float(MAX_AHEAD_TICKS)) * 1000.0 / 60.0))
	if is_server:
		start_sync_local_start_msec = server_start_msec - client_lead_msec
	else:
		start_sync_local_start_msec = int(round(float(server_start_msec) - start_sync_server_offset_msec))

func _begin_client_simulation_now(initial_target_tick: int) -> void:
	if game_sim == null or game_sim.sim_started:
		return
	start_sync_actual_client_start_msec = Time.get_ticks_msec()
	game_sim.set_sim_started(true)
	local_tick = 0
	clients_server_tick = 0
	clients_target_tick = clamp(initial_target_tick, 0, MAX_AHEAD_TICKS)
	last_target_tick_update = Time.get_ticks_msec()
	start_sync_client_started = true

func _begin_authoritative_simulation_now() -> void:
	if !is_server or server_game_sim == null or server_game_sim.sim_started:
		return
	start_sync_actual_server_start_msec = Time.get_ticks_msec()
	server_tick = 0
	target_tick = 0
	server_game_sim.set_sim_started(true)
	start_sync_authoritative_started = true

func send_player_settings(settings: Dictionary) -> void:
	var my_id := multiplayer.get_unique_id()
	if is_server:
		update_player_settings(settings, my_id)
		update_player_settings.rpc(settings, my_id)
	else:
		update_player_settings.rpc_id(1, settings)
		player_settings[my_id] = settings

@rpc("any_peer", "reliable")
func update_player_settings(settings: Dictionary, id: int = -1) -> void:
	var sender_id := multiplayer.get_remote_sender_id()
	if id == -1:
		id = sender_id
		if id == 0:
			id = multiplayer.get_unique_id()
		player_settings[id] = settings
		if is_server and sender_id != 0:
			update_player_settings.rpc(settings, id)
	else:
		player_settings[id] = settings
	if id == multiplayer.get_unique_id():
		if settings.get("spectator", false):
			desired_ahead_ticks = 1.0
		else:
			desired_ahead_ticks = 2.0 if !is_server else (2.0 if listen_server else 0.0)
	if is_server:
		var spec = settings.get("spectator", false)
		if spec:
			if player_ids.has(id):
				player_ids.erase(id)
				spectator_ids.append(id)
				var cpu_ids_changed := _ensure_cpu_ids_do_not_overlap_humans("settings_spectator")
				if !race_active:
					_update_player_ids.rpc(player_ids)
					if cpu_ids_changed:
						_broadcast_cpu_roster()
				_calc_state_offsets()
		else:
			if spectator_ids.has(id):
				spectator_ids.erase(id)
			if !player_ids.has(id):
				player_ids.append(id)
				var cpu_ids_changed := _ensure_cpu_ids_do_not_overlap_humans("settings_player")
				if !race_active:
					_update_player_ids.rpc(player_ids)
					if cpu_ids_changed:
						_broadcast_cpu_roster()
				_calc_state_offsets()

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

func _recent_unacked_input_keys(keys: Array) -> Array:
	var out: Array = []
	if keys.is_empty():
		return out
	keys.sort()
	var first_unacked := last_ack_tick + 1
	var max_count := INPUT_RETRANSMIT_RECENT_TICKS
	if last_ack_tick < 0:
		first_unacked = int(keys[0])
		max_count = INPUT_RETRANSMIT_STARTUP_TICKS
	var start_index := keys.size()
	for i in range(keys.size()):
		if int(keys[i]) >= first_unacked:
			start_index = i
			break
	if start_index >= keys.size():
		start_index = max(keys.size() - INPUT_RETRANSMIT_RECENT_TICKS, 0)
	var end_index = keys.size()
	if end_index - start_index > max_count:
		start_index = end_index - max_count
	for i in range(start_index, end_index):
		out.append(keys[i])
	return out

func collect_server_inputs() -> Dictionary:
	if not is_server:
		return {}
	if server_game_sim == null or !server_game_sim.sim_started:
		return {}
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
	if server_tick > target_tick:
		return {}
	if !server_netcode_session.server_has_full_input_frame(server_tick):
		return {}
	if !server_netcode_session.tick_server_frame(server_game_sim, server_tick):
		return {}
	var frame_inputs: Dictionary = server_netcode_session.get_frame_as_dictionary(server_tick)
	authoritative_history[server_tick] = frame_inputs
	pending_inputs.erase(server_tick)
	return frame_inputs

func collect_client_inputs() -> Dictionary:
	if game_sim != null and !game_sim.sim_started:
		return {}
	var my_settings = player_settings.get(multiplayer.get_unique_id(), {})
	var is_spec = typeof(my_settings) == TYPE_DICTIONARY and my_settings.get("spectator", false)
	if is_spec:
		if authoritative_inputs.has(local_tick):
			var frame: Dictionary = authoritative_inputs[local_tick]
			authoritative_inputs.erase(local_tick)
			for pid in frame.keys():
				netcode_session.store_authoritative_input(local_tick, int(pid), frame[pid])
			netcode_session.tick_client_predicted_frame(game_sim, local_tick)
			input_history[local_tick] = frame
			if input_history.has(local_tick - INPUT_HISTORY_SIZE):
				input_history.erase(local_tick - INPUT_HISTORY_SIZE)
			local_tick += 1
			_adjust_time_scale()
			return frame
		return {}
	if _startup_light_net_active(local_tick):
		_store_neutral_authoritative_frame_for_all_racers(local_tick)
		netcode_session.store_local_input(local_tick, NEUTRAL_INPUT_BYTES)
		if !is_server:
			_client_startup_sync.rpc_id(1, desired_ahead_ticks, last_server_input_tick, local_tick)
			_acc_log_out(12)
		var frame_inputs: Dictionary
		if authoritative_inputs.has(local_tick):
			frame_inputs = authoritative_inputs[local_tick]
			authoritative_inputs.erase(local_tick)
			for pid in frame_inputs.keys():
				netcode_session.store_authoritative_input(local_tick, int(pid), frame_inputs[pid])
		else:
			frame_inputs = {}
		netcode_session.tick_client_predicted_frame(game_sim, local_tick)
		frame_inputs = netcode_session.get_frame_as_dictionary(local_tick)
		input_history[local_tick] = frame_inputs
		if input_history.has(local_tick - INPUT_HISTORY_SIZE):
			input_history.erase(local_tick - INPUT_HISTORY_SIZE)
		local_tick += 1
		_adjust_time_scale()
		return frame_inputs
	if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
		if !is_server:
			var old_keys := sent_inputs_bytes.keys()
			if old_keys.size() > 0:
				var recent_keys := _recent_unacked_input_keys(old_keys)
				var start := int(recent_keys[0])
				var packet: PackedByteArray = netcode_session.build_local_input_packet(start, recent_keys.size())
				log_flat_client_payload_out += packet.size()
				_acc_log_out(12 + packet.size())
				_client_send_input_flat.rpc_id(1, packet, desired_ahead_ticks, last_server_input_tick)
				last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		return {}
	sent_inputs_bytes[local_tick] = last_local_input_bytes
	sent_input_times[local_tick] = 0.001 * float(Time.get_ticks_msec())
	netcode_session.store_local_input(local_tick, last_local_input_bytes)
	if is_server:
		if not pending_inputs.has(local_tick):
			pending_inputs[local_tick] = {}
		pending_inputs[local_tick][multiplayer.get_unique_id()] = last_local_input_bytes
		server_netcode_session.store_pending_input(local_tick, multiplayer.get_unique_id(), last_local_input_bytes)
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		last_received_tick[multiplayer.get_unique_id()] = local_tick
		server_netcode_session.set_peer_last_received(multiplayer.get_unique_id(), local_tick, last_input_time[multiplayer.get_unique_id()])
	var all_keys := sent_inputs_bytes.keys()
	if !is_server and all_keys.size() > 0:
		var recent_keys := _recent_unacked_input_keys(all_keys)
		var first_tick := int(recent_keys[0])
		var input_packet: PackedByteArray = netcode_session.build_local_input_packet(first_tick, recent_keys.size())
		log_flat_client_payload_out += input_packet.size()
		_acc_log_out(12 + input_packet.size())
		for k in recent_keys:
			var prev := int(_log_sent_counts.get(k, 0))
			_log_sent_counts[k] = prev + 1
			if prev > 0:
				log_inputs_retransmitted += 1
			log_inputs_sent += 1
		_client_send_input_flat.rpc_id(1, input_packet, desired_ahead_ticks, last_server_input_tick)
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
	var frame_inputs: Dictionary
	if authoritative_inputs.has(local_tick):
		frame_inputs = authoritative_inputs[local_tick]
		authoritative_inputs.erase(local_tick)
		for pid in frame_inputs.keys():
			netcode_session.store_authoritative_input(local_tick, int(pid), frame_inputs[pid])
	else:
		frame_inputs = {}
	netcode_session.tick_client_predicted_frame(game_sim, local_tick)
	frame_inputs = netcode_session.get_frame_as_dictionary(local_tick)
	input_history[local_tick] = frame_inputs
	if input_history.has(local_tick - INPUT_HISTORY_SIZE):
		input_history.erase(local_tick - INPUT_HISTORY_SIZE)
	local_tick += 1
	_adjust_time_scale()
	return frame_inputs

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_send_input(start_tick: int, inputs: Array, ahead: float, ack: int) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if is_server:
		var sender_id := multiplayer.get_remote_sender_id()
		var sender_seen_before: bool = server_netcode_session.peer_has_received(sender_id)
		var reject_before := target_tick - 5
		if !sender_seen_before:
			reject_before = server_tick
		var accepted := 0
		var dropped := 0
		for i in range(inputs.size()):
			var tick := start_tick + i
			if tick < reject_before:
				log_server_late_drops += 1
				dropped += 1
				continue
			var input = inputs[i]
			if !pending_inputs.has(tick):
				pending_inputs[tick] = {}
			pending_inputs[tick][sender_id] = input
			server_netcode_session.store_pending_input(tick, sender_id, input)
			last_input_time[sender_id] = 0.001 * float(Time.get_ticks_msec())
			last_received_tick[sender_id] = tick
			server_netcode_session.set_peer_last_received(sender_id, tick, last_input_time[sender_id])
			accepted += 1
		peer_desired_ahead[sender_id] = ahead
		server_netcode_session.set_peer_desired_ahead(sender_id, ahead)
		authoritative_acks[sender_id] = max(
			ack,
			authoritative_acks.get(sender_id, -1)
		)
		server_netcode_session.set_peer_authoritative_ack(sender_id, ack)
		var _est := 12
		for e in inputs:
			if typeof(e) == TYPE_PACKED_BYTE_ARRAY:
				_est += (e as PackedByteArray).size()
		_acc_log_in(_est)
		_prune_authoritative_history()
	var __prof_t1 := Time.get_ticks_usec()
	prof_client_send_input_us_interval += __prof_t1 - __prof_t0

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_startup_sync(ahead: float, ack: int, client_tick: int) -> void:
	if !race_active:
		return
	if !is_server:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if !player_ids.has(sender_id):
		return
	var now_sec := 0.001 * float(Time.get_ticks_msec())
	peer_desired_ahead[sender_id] = ahead
	server_netcode_session.set_peer_desired_ahead(sender_id, ahead)
	authoritative_acks[sender_id] = max(ack, authoritative_acks.get(sender_id, -1))
	server_netcode_session.set_peer_authoritative_ack(sender_id, ack)
	last_input_time[sender_id] = now_sec
	last_received_tick[sender_id] = max(int(last_received_tick.get(sender_id, -1)), min(client_tick, STARTUP_LIGHT_NET_TICKS - 1))
	server_netcode_session.set_peer_last_received(sender_id, int(last_received_tick[sender_id]), now_sec)
	_acc_log_in(12)
	_prune_authoritative_history()

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_send_input_flat(packet: PackedByteArray, ahead: float, ack: int) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if is_server:
		var sender_id := multiplayer.get_remote_sender_id()
		var now_sec := 0.001 * float(Time.get_ticks_msec())
		var sender_seen_before: bool = server_netcode_session.peer_has_received(sender_id)
		var reject_before := target_tick - 5
		if !sender_seen_before:
			reject_before = server_tick
		var stats: Dictionary = server_netcode_session.store_pending_input_packet(sender_id, reject_before, packet, ack, ahead, now_sec)
		if !bool(stats.get("valid", false)):
			return
		var accepted := int(stats.get("accepted", 0))
		var dropped := int(stats.get("dropped", 0))
		var last_tick := int(stats.get("last_tick", -1))
		if dropped > 0:
			log_server_late_drops += dropped
		if accepted > 0:
			last_input_time[sender_id] = now_sec
			last_received_tick[sender_id] = last_tick
		peer_desired_ahead[sender_id] = ahead
		authoritative_acks[sender_id] = server_netcode_session.get_peer_authoritative_ack(sender_id)
		log_flat_client_payload_in += packet.size()
		_acc_log_in(12 + packet.size())
		_prune_authoritative_history()
	var __prof_t1 := Time.get_ticks_usec()
	prof_client_send_input_us_interval += __prof_t1 - __prof_t0

func _apply_server_input_ack(ack_tick: int) -> void:
	if ack_tick == -1:
		return
	var old_ack := last_ack_tick
	last_ack_tick = max(last_ack_tick, ack_tick)
	var newly = max(0, last_ack_tick - max(old_ack, -1))
	log_inputs_acked += newly
	for key in _log_sent_counts.keys():
		if int(key) <= last_ack_tick:
			_log_sent_counts.erase(key)
	if sent_input_times.has(ack_tick):
		var sample : float = 0.001 * float(Time.get_ticks_msec()) - sent_input_times[ack_tick]
		if rtt_s == 0.0:
			rtt_s = sample
		else:
			rtt_s = lerp(rtt_s, sample, RTT_SMOOTHING)
		sent_input_times.erase(ack_tick)
		_update_desired_ahead()
	for key in sent_inputs_bytes.keys():
		if key <= last_ack_tick:
			sent_inputs_bytes.erase(key)
	for key in sent_input_times.keys():
		if key <= last_ack_tick:
			sent_input_times.erase(key)

@rpc("any_peer", "unreliable", "call_local", 3)
func _server_timing_update(this_ack: int, tgt: int, max_ahead: float) -> void:
	if !race_active:
		return
	if not is_server or listen_server:
		clients_target_tick = max(clients_target_tick, tgt)
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
	_apply_server_input_ack(this_ack)

@rpc("any_peer", "unreliable", "call_local", 3)
func _server_startup_sync(server_tick_value: int, this_ack: int, tgt: int, max_ahead: float) -> void:
	if !race_active:
		return
	if not is_server or listen_server:
		clients_server_tick = max(clients_server_tick, server_tick_value + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
		last_server_input_tick = max(last_server_input_tick, server_tick_value)
	_apply_server_input_ack(this_ack)

@rpc("any_peer", "unreliable_ordered", "call_local", 2)
func _server_broadcast(last_tick: int, inputs: Array, this_ack: int, state: PackedByteArray, state_uncompressed_size: int, tgt: int, max_ahead: float) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if not is_server or listen_server:
		clients_server_tick = max(clients_server_tick, last_tick + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
	if inputs.size() > 0:
		var start_tick := last_tick - inputs.size() + 1
		for i in range(inputs.size()):
			var tick := start_tick + i
			var frame = inputs[i]
			authoritative_inputs[tick] = frame
			input_history[tick] = frame	# apply immediately to history as well
			if typeof(frame) == TYPE_DICTIONARY:
				for pid in (frame as Dictionary).keys():
					netcode_session.store_authoritative_input(tick, int(pid), frame[pid])

		# rollback once from the first updated tick
		_handle_input_update(start_tick, authoritative_inputs[start_tick])
		last_server_input_tick = max(last_server_input_tick, last_tick)
	_apply_server_input_ack(this_ack)
	if state.size() > 0:
		var raw_state_size := state_uncompressed_size if state_uncompressed_size > 0 else state.size()
		_log_state_received(raw_state_size, state.size())
		var _state_to_use := state
		if state_uncompressed_size > 0:
			# Compressed payload; decompress before handling.
			_state_to_use = state.decompress(state_uncompressed_size, FileAccess.COMPRESSION_ZSTD)
		_handle_state(last_tick, _state_to_use)
	var _est := 4 + _estimate_nested_inputs_size(inputs) + state.size() + 4 + 4 + 4 + 4
	_acc_log_in(_est)
	var __prof_t1 := Time.get_ticks_usec()
	prof_server_broadcast_recv_us_interval += __prof_t1 - __prof_t0

@rpc("any_peer", "unreliable_ordered", "call_local", 2)
func _server_broadcast_flat(server_state_tick: int, input_packet: PackedByteArray, this_ack: int, state: PackedByteArray, state_uncompressed_size: int, tgt: int, max_ahead: float) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if not is_server or listen_server:
		var stats: Dictionary = netcode_session.store_authoritative_input_packet(input_packet)
		if bool(stats.get("valid", false)):
			var count := int(stats.get("count", 0))
			if count > 0:
				var first_tick := int(stats.get("first_tick", -1))
				var last_tick := int(stats.get("last_tick", -1))
				if start_sync_first_authoritative_input_msec < 0:
					start_sync_first_authoritative_input_msec = Time.get_ticks_msec()
					start_sync_first_authoritative_first_tick = first_tick
					start_sync_first_authoritative_last_tick = last_tick
					start_sync_first_authoritative_count = count
				clients_server_tick = max(clients_server_tick, last_tick + 1)
				_handle_input_update_flat(first_tick)
				last_server_input_tick = max(last_server_input_tick, last_tick)
		clients_target_tick = max(clients_target_tick, tgt)
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
		log_flat_server_payload_in += input_packet.size()
	_apply_server_input_ack(this_ack)
	_acc_log_in(20 + input_packet.size())
	var __prof_t1 := Time.get_ticks_usec()
	prof_server_broadcast_recv_us_interval += __prof_t1 - __prof_t0

@rpc("any_peer", "unreliable", "call_local", 4)
func _server_state_sync(state_tick: int, state: PackedByteArray, state_uncompressed_size: int) -> void:
	if !race_active:
		return
	if state.size() <= 0:
		return
	if state_tick <= latest_state_tick:
		return
	var __prof_t0 := Time.get_ticks_usec()
	var raw_state_size := state_uncompressed_size if state_uncompressed_size > 0 else state.size()
	_log_state_received(raw_state_size, state.size())
	var _state_to_use := state
	if state_uncompressed_size > 0:
		_state_to_use = state.decompress(state_uncompressed_size, FileAccess.COMPRESSION_ZSTD)
	_handle_state(state_tick, _state_to_use)
	_acc_log_in(8 + state.size())
	var __prof_t1 := Time.get_ticks_usec()
	prof_server_broadcast_recv_us_interval += __prof_t1 - __prof_t0

func post_tick() -> void:
	if !race_active:
		return
	if is_server and server_game_sim != null:
		var _t0 := Time.get_ticks_usec()
		var state = server_game_sim.get_state_data(server_tick)
		var max_ahead := _calc_max_ahead()
		max_ahead_from_server = max_ahead
		# Prepare compressed snapshot lazily and reuse for all recipients this tick
		var compressed_ready := false
		var compressed_state : PackedByteArray = PackedByteArray()
		var uncompressed_size := 0
		for id in player_ids + spectator_ids:
			if _startup_light_net_active(server_tick):
				var startup_ack: int = server_netcode_session.get_peer_last_received(id)
				_acc_log_out(12)
				_server_startup_sync.rpc_id(id, server_tick, startup_ack, target_tick, max_ahead)
				continue
			var send_state : PackedByteArray = PackedByteArray()
			var send_state_uncomp_size := 0
			if state_send_offsets.has(id) and int(state_send_offsets[id]) == server_tick % STATE_BROADCAST_INTERVAL_TICKS:
				if not compressed_ready:
					if state.size() > 0 and use_state_compression:
						compressed_state = state.compress(FileAccess.COMPRESSION_ZSTD)
						uncompressed_size = state.size()
					else:
						compressed_state = state
						uncompressed_size = -1
					compressed_ready = true
				send_state = compressed_state
				send_state_uncomp_size = uncompressed_size
				_log_state_sent(uncompressed_size if uncompressed_size > 0 else send_state.size(), send_state.size())
			var ack = authoritative_acks.get(id, -1)
			ack = server_netcode_session.get_peer_authoritative_ack(id)
			if server_tick >= STARTUP_LIGHT_NET_TICKS and ack < STARTUP_LIGHT_NET_TICKS - 1:
				ack = STARTUP_LIGHT_NET_TICKS - 1
				authoritative_acks[id] = ack
				server_netcode_session.set_peer_authoritative_ack(id, ack)
			var input_packet: PackedByteArray = server_netcode_session.build_authoritative_input_packet(ack)
			log_flat_server_payload_out += input_packet.size()
			_acc_log_out(20 + input_packet.size())
			_server_broadcast_flat.rpc_id(id, server_tick, input_packet, server_netcode_session.get_peer_last_received(id), PackedByteArray(), 0, target_tick, max_ahead)
			if send_state.size() > 0:
				_acc_log_out(8 + send_state.size())
				_server_state_sync.rpc_id(id, server_tick, send_state, send_state_uncomp_size)
		server_tick += 1
		if listen_server:
			authoritative_acks[multiplayer.get_unique_id()] = server_tick - 1
			server_netcode_session.set_peer_authoritative_ack(multiplayer.get_unique_id(), server_tick - 1)
			_prune_authoritative_history()
		var _t1 := Time.get_ticks_usec()
		log_net_cpu_us_interval += _t1 - _t0

func _idle_broadcast() -> void:
	if !race_active:
		return
	if server_game_sim == null:
		return
	# timing start
	var _t0 := Time.get_ticks_usec()
	var max_ahead := _calc_max_ahead()
	max_ahead_from_server = max_ahead
	for id in player_ids + spectator_ids:
		var ack = authoritative_acks.get(id, -1)
		ack = server_netcode_session.get_peer_authoritative_ack(id)
		if server_tick >= STARTUP_LIGHT_NET_TICKS and ack < STARTUP_LIGHT_NET_TICKS - 1:
			ack = STARTUP_LIGHT_NET_TICKS - 1
			authoritative_acks[id] = ack
			server_netcode_session.set_peer_authoritative_ack(id, ack)
		var input_packet: PackedByteArray = server_netcode_session.build_authoritative_input_packet(ack)
		var _bytes := 20 + input_packet.size()
		log_flat_server_payload_out += input_packet.size()
		_acc_log_out(_bytes)
		_server_broadcast_flat.rpc_id(
			id,
			max(server_tick - 1, 0),
			input_packet,
			server_netcode_session.get_peer_last_received(id),
			PackedByteArray(),
			0,
			target_tick,
			max_ahead
		)
	var _t1 := Time.get_ticks_usec()
	log_net_cpu_us_interval += _t1 - _t0

func _check_client_stalls() -> void:
	if !race_active:
		return
	if not is_server or server_game_sim == null or not server_game_sim.sim_started:
		return
	# don't test while still waiting for the very first full frame
	if server_tick >= target_tick:
		return
	var waiting = pending_inputs.get(server_tick, {})
	var now := 0.001 * float(Time.get_ticks_msec())
	var missing := []
	var roster_chk : Array = _get_human_roster()
	for id in roster_chk:
		if not waiting.has(id):
			missing.append(id)
			var native_last_input: float = server_netcode_session.get_peer_last_input_time(id)
			if native_last_input > 0.0 and now - float(native_last_input) > 10.0:
				if server_tick != 0:
					push_error("Client %s stalled, disconnecting" % str(id))
					multiplayer.disconnect_peer(id)
					_on_peer_disconnected(id)
					if !race_active:
						_update_player_ids.rpc(player_ids)
	if target_tick - server_tick > 5 and missing.size() > 0:
		var prev = authoritative_history.get(server_tick - 1, {})
		for i in range(roster_chk.size()):
			var pid = roster_chk[i]
			if missing.has(pid):
				if !server_netcode_session.peer_has_received(pid):
					continue
				waiting[pid] = _input_frame_value(prev, int(pid), NEUTRAL_INPUT_BYTES)
				server_netcode_session.store_pending_input(server_tick, int(pid), waiting[pid])
				log_server_replacements += 1
	pending_inputs[server_tick] = waiting

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
	latest_state_tick = tick
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

func _handle_input_update(tick: int, inputs: Dictionary) -> void:
	if !race_active:
		return
	if game_sim == null:
		return
	if not input_history.has(tick):
		return
	var __prof_t0 := Time.get_ticks_usec()
	#var predicted = input_history[tick]
	# we should honestly just always be rolling back for now
	# we can figure out matching later
	#if predicted == inputs:
	#	return
	if tick == 0 or latest_state_tick == -1:
		return
	input_history[tick] = inputs
	for pid in inputs.keys():
		netcode_session.store_authoritative_input(tick, int(pid), inputs[pid])
	if authoritative_inputs.has(tick):
		authoritative_inputs.erase(tick)
	_recalculate_future_predictions(tick)
	game_sim.load_state(maxi(latest_state_tick, tick - 1))
	var old_time := Time.get_ticks_usec()
	netcode_session.replay_history(game_sim, maxi(latest_state_tick + 1, tick), local_tick)
	var new_time := Time.get_ticks_usec()
	#DebugDraw2D.set_text("rollback frametime microseconds", new_time - old_time)
	rollback_frametime_us = new_time - old_time
	var __prof_t1 := Time.get_ticks_usec()
	prof_handle_input_update_us_interval += __prof_t1 - __prof_t0

func _handle_input_update_flat(tick: int) -> void:
	if !race_active:
		return
	if game_sim == null:
		return
	if tick < 0:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if tick == 0 or latest_state_tick == -1:
		return
	if tick >= local_tick:
		return
	_recalculate_future_predictions(tick)
	game_sim.load_state(maxi(latest_state_tick, tick - 1))
	var old_time := Time.get_ticks_usec()
	netcode_session.replay_history(game_sim, maxi(latest_state_tick + 1, tick), local_tick)
	var new_time := Time.get_ticks_usec()
	rollback_frametime_us = new_time - old_time
	var __prof_t1 := Time.get_ticks_usec()
	prof_handle_input_update_us_interval += __prof_t1 - __prof_t0

func _recalculate_future_predictions(start_tick: int) -> void:
	if !race_active:
		return
	netcode_session.recalculate_predictions(start_tick, local_tick)

func disconnect_from_server() -> void:
	race_active = false
	if multiplayer.multiplayer_peer != null:
		multiplayer.multiplayer_peer.close()
		multiplayer.multiplayer_peer = null
	is_server = false
	listen_server = false
	player_ids.clear()
	pending_inputs.clear()
	authoritative_inputs.clear()
	input_history.clear()
	sent_inputs_bytes.clear()
	last_input_time.clear()
	last_local_input_bytes = NEUTRAL_INPUT_BYTES.duplicate()
	server_tick = 0
	local_tick = 0
	target_tick = 0
	last_received_tick.clear()
	last_ack_tick = -1
	player_settings.clear()
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	authoritative_history.clear()
	authoritative_acks.clear()
	last_server_input_tick = -1
	latest_state_tick = -1
	server_netcode_session.clear_peer_state()
	_reset_start_sync_state()

func _prune_authoritative_history() -> void:
	var min_ack: int = server_netcode_session.get_min_peer_authoritative_ack(player_ids)
	# Always enforce a sliding window to bound memory, even if some acks stall.
	var cutoff: int = min_ack
	var window_cutoff := server_tick - MAX_HISTORY_TICKS
	if cutoff == -1 or window_cutoff > cutoff:
		cutoff = window_cutoff
	if cutoff == -1:
		return
	for key in authoritative_history.keys():
		if key <= cutoff:
			authoritative_history.erase(key)

func _update_desired_ahead() -> void:
	desired_ahead_ticks = ((rtt_s) + JITTER_BUFFER) / base_wait_time

var use_physics_ticks := 1.0

func _adjust_time_scale() -> void:
	var __prof_t0 := Time.get_ticks_usec()
	#DebugDraw2D.set_text("playercount", player_ids.size())
	#DebugDraw2D.set_text("rtt", rtt_s)
	#if is_server:
		#DebugDraw2D.set_text("server_tick", server_tick)
		#DebugDraw2D.set_text("target_tick", target_tick)
	if is_server and !listen_server:
		return
	if !is_server and start_sync_first_authoritative_input_msec < 0:
		use_physics_ticks = 1.0
		Engine.physics_ticks_per_second = 60
		var __prof_t1_no_auth := Time.get_ticks_usec()
		prof_adjust_time_scale_us_interval += __prof_t1_no_auth - __prof_t0
		return
	var current_ahead_ticks = local_tick - clients_target_tick
	var target_ahead_ticks = lerpf(desired_ahead_ticks, clients_max_ahead_from_server, 0.75)
	var diff = target_ahead_ticks - current_ahead_ticks
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

@rpc("any_peer", "reliable")
func set_player_finished(id: int, tick: int, place: int) -> void:
	player_finish_times[id] = tick
	player_finish_placements[id] = place
	if finish_order.size() < place:
		finish_order.resize(place)
	finish_order[place - 1] = id

func send_player_finished(id: int, tick: int) -> void:
	if player_finish_times.has(id):
		return
	var place := finish_order.size() + 1
	if is_server:
		set_player_finished.rpc(id, tick, place)
		set_player_finished(id, tick, place)
