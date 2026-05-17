class_name NetworkManager
extends Node

signal race_started(track_index, player_settings)
signal race_finished
signal race_event(event_type, actor_id, target_id, tick, value)
signal race_options_changed(options)

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
var network_active: bool = false
var player_ids: Array = []
var spectator_ids: Array = []
var waiting_peers: Array = []
var race_player_ids: Array = []
var _disconnected_during_race := {}
var pending_inputs := {}
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

func has_network_peer() -> bool:
	return network_active and multiplayer.multiplayer_peer != null
var last_ack_tick: int = -1
var target_tick: int = 0
const MAX_AHEAD_TICKS := 30
const MAX_HISTORY_TICKS := 60
const INPUT_RETRANSMIT_RECENT_TICKS := 5
const INPUT_RETRANSMIT_STARTUP_TICKS := 64
const STARTUP_LIGHT_NET_TICKS := 120
const SERVER_INPUT_REPLACEMENT_BACKLOG_TICKS := 5
const AUTH_INPUT_REDUNDANCY_FRAMES := 2
const AUTH_INPUT_ROLLBACK_WINDOW_TICKS := 15
var sent_input_times := {}
var rtt_s: float = 0.0
var desired_ahead_ticks: float = 2.0
var base_wait_time: float = 1.0 / 60.0
const JITTER_BUFFER := 0.016
const RTT_SMOOTHING := 0.1
const SPEED_ADJUST_STEP := 0.0003
const SHARED_AHEAD_EXTRA_TICKS := 3.0
const SHARED_AHEAD_CAP_TICKS := 10.0
const START_SYNC_SAMPLE_COUNT := 4
const START_SYNC_PING_INTERVAL_MS := 50
const START_SYNC_START_DELAY_MS := 750
var player_settings := {}
var ready_players : Array[int] = []
const PLAYER_SETTINGS_RESYNC_INTERVAL_SEC := 1.0
var _settings_resync_timer: Timer
const STATE_BROADCAST_INTERVAL_TICKS := 60
const STATE_CHUNK_PAYLOAD_BYTES := 1000
const STATE_CHUNK_SEND_COPIES := 2
var state_send_offsets := {}
var pending_state_chunks := {}
var net_race_finish_time := -1
var player_finish_times := {}
var player_finish_placements := {}
var finish_order : Array = []
var player_eliminations := {}
var race_options := {
	"game_mode": 0,
	"track_indices": [],
	"vehicle_restore": true,
	"bumpers": false,
	"grand_prix_current_track": 0,
	"grand_prix_points": {},
	"grand_prix_ko_energy_bonuses": {},
}
var pending_next_race_track_index := -1
var pending_next_race_settings: Array = []
var pending_next_race_options: Dictionary = {}
var sticker_cooldown_msec := {}
var max_ahead_from_server: float = 0.0
var peer_desired_ahead := {}
var delayed_peer_ids := {}

const CPU_ID_MIN := 1
const CPU_ID_MAX := 5000
var cpu_player_ids: Array = []
var race_cpu_player_ids: Array = []
var cpu_player_settings := {}
var cpu_driver_manager: CpuDriverManager

var clients_server_tick := 0
var clients_target_tick := 0
var last_target_tick_update := 0
var clients_max_ahead_from_server := 2.0
var authoritative_history := {}
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
var dump_auth_input_samples := false
var auth_input_sample_limit := 20000
var auth_input_sample_dir := "user://auth_input_samples"
var dump_state_samples := false
var state_sample_limit := 5000
var state_sample_dir := "user://state_samples"
var state_sample_index := 0

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
var log_auth_packets_sent := 0
var log_server_late_drops := 0
var log_server_replacements := 0
var log_server_behind_ticks_sum := 0
var log_server_behind_ticks_samples := 0
var log_server_behind_ticks_max := 0
var log_peer_inputs_accepted := {}
var log_peer_inputs_dropped := {}
var log_peer_replacements := {}
var peer_client_rtt_s := {}
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

func _clear_state_chunk_buffers() -> void:
	pending_state_chunks.clear()

func _prune_state_chunk_buffers() -> void:
	for tick in pending_state_chunks.keys():
		if int(tick) <= latest_state_tick:
			pending_state_chunks.erase(tick)

var version_string: String = ""
var _unverified_peers: Array = []
var _version_request_time := {}

func _log_add_int(dict: Dictionary, key, amount: int) -> void:
	if amount <= 0:
		return
	dict[key] = int(dict.get(key, 0)) + amount

func _format_log_float(value: float, decimals: int = 2) -> String:
	var scale := pow(10.0, float(decimals))
	return str(round(value * scale) / scale)

func _client_unacked_stats() -> Dictionary:
	var out := {
		"count": sent_inputs_bytes.size(),
		"oldest": -1,
		"newest": -1,
	}
	for key in sent_inputs_bytes.keys():
		var tick := int(key)
		if int(out["oldest"]) < 0 or tick < int(out["oldest"]):
			out["oldest"] = tick
		if tick > int(out["newest"]):
			out["newest"] = tick
	return out

func _client_target_ahead_ticks() -> float:
	var local_desired_ahead := _local_desired_ahead_for_shared()
	var shared_ahead_limit = max(local_desired_ahead, SHARED_AHEAD_CAP_TICKS)
	var shared_ahead_target = min(clients_max_ahead_from_server, local_desired_ahead + SHARED_AHEAD_EXTRA_TICKS, shared_ahead_limit)
	return max(local_desired_ahead, shared_ahead_target)

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

func _build_server_peer_log_fields() -> Dictionary:
	var out := {
		"server_peer_lag_max": 0,
		"server_peer_lag_avg": 0.0,
		"target_peer_lag_max": 0,
		"target_peer_lag_avg": 0.0,
		"peer_ahead_min": 0.0,
		"peer_ahead_max": 0.0,
		"peer_ahead_avg": 0.0,
		"peer_rtt_max_ms": 0.0,
		"peer_rtt_avg_ms": 0.0,
		"peer_inputs_accepted": 0,
		"peer_inputs_dropped": 0,
		"peer_replacements": 0,
		"peer_input_server_lead_min": 0.0,
		"peer_input_server_lead_max": 0.0,
		"peer_input_server_lead_avg": 0.0,
		"peer_input_target_lead_min": 0.0,
		"peer_input_target_lead_max": 0.0,
		"peer_input_target_lead_avg": 0.0,
		"peer_snapshot": "-",
	}
	if !is_server:
		return out
	var ids := _get_active_human_roster()
	if ids.is_empty():
		return out
	var lag_sum := 0.0
	var target_lag_sum := 0.0
	var ahead_sum := 0.0
	var ahead_min := INF
	var ahead_max := 0.0
	var rtt_sum_ms := 0.0
	var rtt_count := 0
	var rtt_max_ms := 0.0
	var snapshot_parts := []
	var accepted_total := 0
	var dropped_total := 0
	var replacement_total := 0
	for pid_variant in ids:
		var pid := int(pid_variant)
		var last_recv := int(last_received_tick.get(pid, server_netcode_session.get_peer_last_received(pid)))
		var server_lag := 0
		var target_lag := 0
		if last_recv >= 0:
			server_lag = maxi(server_tick - last_recv, 0)
			target_lag = maxi(target_tick - last_recv, 0)
		else:
			server_lag = server_tick + 1
			target_lag = target_tick + 1
		var ahead := float(peer_desired_ahead.get(pid, 0.0))
		var rtt_ms := -1.0
		if peer_client_rtt_s.has(pid):
			rtt_ms = 1000.0 * float(peer_client_rtt_s[pid])
			rtt_sum_ms += rtt_ms
			rtt_count += 1
			rtt_max_ms = max(rtt_max_ms, rtt_ms)
		var accepted := int(log_peer_inputs_accepted.get(pid, 0))
		var dropped := int(log_peer_inputs_dropped.get(pid, 0))
		var replacements := int(log_peer_replacements.get(pid, 0))
		accepted_total += accepted
		dropped_total += dropped
		replacement_total += replacements
		lag_sum += float(server_lag)
		target_lag_sum += float(target_lag)
		ahead_sum += ahead
		ahead_min = min(ahead_min, ahead)
		ahead_max = max(ahead_max, ahead)
		out["server_peer_lag_max"] = maxi(int(out["server_peer_lag_max"]), server_lag)
		out["target_peer_lag_max"] = maxi(int(out["target_peer_lag_max"]), target_lag)
		var delayed := 1 if delayed_peer_ids.has(pid) else 0
		var since_ms := -1
		if last_input_time.has(pid):
			since_ms = int(round((0.001 * float(Time.get_ticks_msec()) - float(last_input_time[pid])) * 1000.0))
		var pkt_start := int(log_peer_last_packet_start.get(pid, -1))
		var pkt_count := int(log_peer_last_packet_count.get(pid, 0))
		var pkt_accept := int(log_peer_last_packet_accept.get(pid, 0))
		var pkt_drop := int(log_peer_last_packet_drop.get(pid, 0))
		var pkt_reject_before := int(log_peer_last_packet_reject_before.get(pid, -1))
		var pkt_last_tick := int(log_peer_last_packet_last_tick.get(pid, -1))
		var pkt_server_lead := int(log_peer_last_packet_server_lead.get(pid, -9999))
		var pkt_target_lead := int(log_peer_last_packet_target_lead.get(pid, -9999))
		snapshot_parts.append("%d:%d:%d:%d:%s:%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d" % [
			pid,
			last_recv,
			server_lag,
			target_lag,
			_format_log_float(ahead, 2),
			_format_log_float(rtt_ms, 1),
			delayed,
			accepted,
			dropped,
			replacements,
			since_ms,
			pkt_start,
			pkt_count,
			pkt_accept,
			pkt_drop,
			pkt_reject_before,
			pkt_last_tick,
			pkt_server_lead,
			pkt_target_lead,
		])
	var count := float(ids.size())
	out["server_peer_lag_avg"] = lag_sum / count
	out["target_peer_lag_avg"] = target_lag_sum / count
	out["peer_ahead_min"] = 0.0 if ahead_min == INF else ahead_min
	out["peer_ahead_max"] = ahead_max
	out["peer_ahead_avg"] = ahead_sum / count
	out["peer_rtt_max_ms"] = rtt_max_ms
	out["peer_rtt_avg_ms"] = rtt_sum_ms / float(maxi(rtt_count, 1))
	out["peer_inputs_accepted"] = accepted_total
	out["peer_inputs_dropped"] = dropped_total
	out["peer_replacements"] = replacement_total
	if log_peer_packet_server_lead_samples > 0:
		out["peer_input_server_lead_min"] = log_peer_packet_server_lead_min
		out["peer_input_server_lead_max"] = log_peer_packet_server_lead_max
		out["peer_input_server_lead_avg"] = log_peer_packet_server_lead_sum / float(log_peer_packet_server_lead_samples)
	if log_peer_packet_target_lead_samples > 0:
		out["peer_input_target_lead_min"] = log_peer_packet_target_lead_min
		out["peer_input_target_lead_max"] = log_peer_packet_target_lead_max
		out["peer_input_target_lead_avg"] = log_peer_packet_target_lead_sum / float(log_peer_packet_target_lead_samples)
	var snapshot := ""
	for i in range(snapshot_parts.size()):
		if i > 0:
			snapshot += "|"
		snapshot += str(snapshot_parts[i])
	out["peer_snapshot"] = snapshot
	return out

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
		log_file.store_line("time,role,uid,is_server,listen,players,server_tick,target_tick,server_behind_ticks,server_behind_avg,server_behind_max,delayed_peers,local_tick,clients_server_tick,clients_target_tick,rtt,desired_ahead,server_max_ahead,physics_tps,start_server_ms,start_local_ms,actual_client_start_ms,actual_server_start_ms,first_auth_ms,first_auth_first_tick,first_auth_last_tick,first_auth_count,up_kbps,down_kbps,up_total_kb,down_total_kb,inputs_sent,inputs_acked,retrans,flat_client_out,flat_client_in,flat_server_out,flat_server_in,late_drops,replacements,state_raw_out,state_payload_out,state_sent,state_max_frags_out,state_min_success_2pct,state_payload_in,state_raw_in,state_recv,state_max_recv_gap_ms,auth_packets,auth_packet_builds,auth_frames,auth_encoded_inputs,auth_unchanged_inputs,auth_payload_per_packet,auth_raw_per_packet,auth_compression_ratio,auth_redundancy_frames,auth_rollback_window,net_cpu_ms,sim_cpu_ms,rollback_avg_ms,rollback_max_ms,collect_inputs_ms,idle_broadcast_ms,check_client_stalls_ms,client_send_input_ms,server_broadcast_recv_ms,handle_state_ms,handle_input_update_ms,recalc_pred_ms,adjust_time_scale_ms,car_store_old_pos_ms,car_post_render_ms,client_current_ahead,client_target_ahead,client_ahead_error,client_server_gap,client_sent_buffer,client_unacked_oldest,client_unacked_newest,client_last_ack_tick,client_ack_lag,client_throttle_frames,use_physics_ticks,client_sim_ticks,client_target_tick_advances,client_target_tick_remote_advances,client_server_tick_advances,client_ahead_samples,client_current_ahead_min,client_current_ahead_max,client_current_ahead_avg,client_target_ahead_avg,client_ahead_error_min,client_ahead_error_max,client_ahead_error_avg,client_pre_auth_adjust_samples,server_peer_lag_max,server_peer_lag_avg,target_peer_lag_max,target_peer_lag_avg,peer_ahead_min,peer_ahead_max,peer_ahead_avg,peer_rtt_max_ms,peer_rtt_avg_ms,peer_inputs_accepted,peer_inputs_dropped,peer_replacements,peer_input_server_lead_min,peer_input_server_lead_max,peer_input_server_lead_avg,peer_input_target_lead_min,peer_input_target_lead_max,peer_input_target_lead_avg,peer_snapshot")
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
	if is_server or !has_network_peer():
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

func _send_cpu_roster_to_peer(id: int) -> void:
	if !is_server:
		return
	sync_cpu_roster.rpc_id(id, cpu_player_ids, _collect_cpu_settings_array())

func _sync_cpu_manager() -> void:
	if cpu_driver_manager != null and (is_server or !has_network_peer()):
		var roster := _get_cpu_roster()
		cpu_driver_manager.configure_drivers(roster)

func get_cpu_roster() -> Array:
	return _get_cpu_roster()

func _get_cpu_roster() -> Array:
	return race_cpu_player_ids.duplicate(true) if race_cpu_player_ids.size() > 0 else cpu_player_ids.duplicate(true)

func _get_human_roster() -> Array:
	return race_player_ids.duplicate(true) if race_player_ids.size() > 0 else player_ids.duplicate(true)

func _get_active_human_roster() -> Array:
	var roster := _get_human_roster()
	if _disconnected_during_race.is_empty():
		return roster
	var out := []
	for id in roster:
		if !_disconnected_during_race.has(id):
			out.append(id)
	return out

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
	var auth_packet_builds := int(auth_stats.get("auth_packets", 0))
	var auth_packets := log_auth_packets_sent
	var auth_frames := int(auth_stats.get("auth_frames", 0))
	var auth_encoded_inputs := int(auth_stats.get("auth_encoded_inputs", 0))
	var auth_unchanged_inputs := int(auth_stats.get("auth_unchanged_inputs", 0))
	var auth_raw_bytes := int(auth_stats.get("auth_raw_bytes", 0))
	var auth_payload_bytes := int(auth_stats.get("auth_payload_bytes", 0))
	var auth_payload_per_packet := 0.0
	var auth_raw_per_packet := 0.0
	var auth_compression_ratio := 1.0
	if auth_packets > 0:
		auth_payload_per_packet = float(log_flat_server_payload_out) / float(auth_packets)
	if auth_packet_builds > 0:
		auth_raw_per_packet = float(auth_raw_bytes) / float(auth_packet_builds)
	if auth_raw_bytes > 0:
		auth_compression_ratio = float(auth_payload_bytes) / float(auth_raw_bytes)
	var state_success := log_state_min_success_2pct if log_state_sent_count > 0 else 1.0
	var server_behind_ticks = maxi(target_tick - server_tick, 0) if is_server else 0
	var server_behind_avg := 0.0
	if log_server_behind_ticks_samples > 0:
		server_behind_avg = float(log_server_behind_ticks_sum) / float(log_server_behind_ticks_samples)
	var delayed_peers := delayed_peer_ids.size() if is_server else 0

	var logged_max_ahead: float = max_ahead_from_server if is_server else clients_max_ahead_from_server
	var client_unacked := _client_unacked_stats()
	var client_current_ahead : int = local_tick - clients_target_tick
	var client_target_ahead := _client_target_ahead_ticks()
	var client_ahead_error := client_target_ahead - float(client_current_ahead)
	var client_server_gap : int = local_tick - clients_server_tick
	var client_ack_lag := local_tick - last_ack_tick if last_ack_tick >= 0 else -1
	var client_current_ahead_avg := 0.0
	var client_current_ahead_min := 0.0
	var client_current_ahead_max := 0.0
	var client_target_ahead_avg := 0.0
	var client_ahead_error_avg := 0.0
	var client_ahead_error_min := 0.0
	var client_ahead_error_max := 0.0
	if log_client_ahead_samples > 0:
		client_current_ahead_avg = log_client_current_ahead_sum / float(log_client_ahead_samples)
		client_current_ahead_min = log_client_current_ahead_min
		client_current_ahead_max = log_client_current_ahead_max
		client_target_ahead_avg = log_client_target_ahead_sum / float(log_client_ahead_samples)
		client_ahead_error_avg = log_client_ahead_error_sum / float(log_client_ahead_samples)
		client_ahead_error_min = log_client_ahead_error_min
		client_ahead_error_max = log_client_ahead_error_max
	var peer_fields := _build_server_peer_log_fields()
	var line := str(Time.get_ticks_msec()) + "," + role + "," + str(multiplayer.get_unique_id()) + "," + str(is_server) + "," + str(listen_server) + "," + str(player_ids.size()) + "," + str(server_tick) + "," + str(target_tick) + "," + str(server_behind_ticks) + "," + str(server_behind_avg) + "," + str(log_server_behind_ticks_max) + "," + str(delayed_peers) + "," + str(local_tick) + "," + str(clients_server_tick) + "," + str(clients_target_tick) + "," + str(rtt_s) + "," + str(desired_ahead_ticks) + "," + str(logged_max_ahead) + "," + str(physics_tps) + "," + str(start_sync_server_start_msec) + "," + str(start_sync_local_start_msec) + "," + str(start_sync_actual_client_start_msec) + "," + str(start_sync_actual_server_start_msec) + "," + str(start_sync_first_authoritative_input_msec) + "," + str(start_sync_first_authoritative_first_tick) + "," + str(start_sync_first_authoritative_last_tick) + "," + str(start_sync_first_authoritative_count) + "," + str(up_kbps) + "," + str(down_kbps) + "," + str(log_bytes_out_total / 1000.0) + "," + str(log_bytes_in_total / 1000.0) + "," + str(log_inputs_sent) + "," + str(log_inputs_acked) + "," + str(log_inputs_retransmitted) + "," + str(log_flat_client_payload_out) + "," + str(log_flat_client_payload_in) + "," + str(log_flat_server_payload_out) + "," + str(log_flat_server_payload_in) + "," + str(log_server_late_drops) + "," + str(log_server_replacements) + "," + str(log_state_raw_out) + "," + str(log_state_payload_out) + "," + str(log_state_sent_count) + "," + str(log_state_max_fragments_out) + "," + str(state_success) + "," + str(log_state_payload_in) + "," + str(log_state_raw_in) + "," + str(log_state_recv_count) + "," + str(log_state_max_recv_gap_ms) + "," + str(auth_packets) + "," + str(auth_packet_builds) + "," + str(auth_frames) + "," + str(auth_encoded_inputs) + "," + str(auth_unchanged_inputs) + "," + str(auth_payload_per_packet) + "," + str(auth_raw_per_packet) + "," + str(auth_compression_ratio) + "," + str(AUTH_INPUT_REDUNDANCY_FRAMES) + "," + str(AUTH_INPUT_ROLLBACK_WINDOW_TICKS) + "," + str(net_cpu_ms) + "," + str(sim_cpu_ms) + "," + str(rollback_avg_ms) + "," + str(rollback_max_ms) + "," + str(collect_inputs_ms) + "," + str(idle_broadcast_ms) + "," + str(check_client_stalls_ms) + "," + str(client_send_input_ms) + "," + str(server_broadcast_recv_ms) + "," + str(handle_state_ms) + "," + str(handle_input_update_ms) + "," + str(recalc_pred_ms) + "," + str(adjust_time_scale_ms) + "," + str(car_store_old_pos_ms) + "," + str(car_post_render_ms)
	line += "," + str(client_current_ahead) + "," + str(client_target_ahead) + "," + str(client_ahead_error) + "," + str(client_server_gap) + "," + str(client_unacked["count"]) + "," + str(client_unacked["oldest"]) + "," + str(client_unacked["newest"]) + "," + str(last_ack_tick) + "," + str(client_ack_lag) + "," + str(log_client_ahead_throttle_frames) + "," + str(use_physics_ticks)
	line += "," + str(log_client_sim_ticks) + "," + str(log_client_target_tick_advances) + "," + str(log_client_target_tick_remote_advances) + "," + str(log_client_server_tick_advances) + "," + str(log_client_ahead_samples) + "," + str(client_current_ahead_min) + "," + str(client_current_ahead_max) + "," + str(client_current_ahead_avg) + "," + str(client_target_ahead_avg) + "," + str(client_ahead_error_min) + "," + str(client_ahead_error_max) + "," + str(client_ahead_error_avg) + "," + str(log_client_pre_auth_adjust_samples)
	line += "," + str(peer_fields["server_peer_lag_max"]) + "," + str(peer_fields["server_peer_lag_avg"]) + "," + str(peer_fields["target_peer_lag_max"]) + "," + str(peer_fields["target_peer_lag_avg"]) + "," + str(peer_fields["peer_ahead_min"]) + "," + str(peer_fields["peer_ahead_max"]) + "," + str(peer_fields["peer_ahead_avg"]) + "," + str(peer_fields["peer_rtt_max_ms"]) + "," + str(peer_fields["peer_rtt_avg_ms"]) + "," + str(peer_fields["peer_inputs_accepted"]) + "," + str(peer_fields["peer_inputs_dropped"]) + "," + str(peer_fields["peer_replacements"]) + "," + str(peer_fields["peer_input_server_lead_min"]) + "," + str(peer_fields["peer_input_server_lead_max"]) + "," + str(peer_fields["peer_input_server_lead_avg"]) + "," + str(peer_fields["peer_input_target_lead_min"]) + "," + str(peer_fields["peer_input_target_lead_max"]) + "," + str(peer_fields["peer_input_target_lead_avg"]) + "," + str(peer_fields["peer_snapshot"])
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
	log_auth_packets_sent = 0
	log_server_behind_ticks_sum = 0
	log_server_behind_ticks_samples = 0
	log_server_behind_ticks_max = 0
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

func _acc_log_out(bytes: int) -> void:
	log_bytes_out_interval += bytes
	log_bytes_out_total += bytes

func _acc_log_in(bytes: int) -> void:
	log_bytes_in_interval += bytes
	log_bytes_in_total += bytes

func reset_race_state(preserve_player_settings: bool = false) -> void:
	var preserved_player_settings := {}
	if preserve_player_settings:
		var preserve_ids := []
		preserve_ids.append_array(player_ids)
		preserve_ids.append_array(spectator_ids)
		preserve_ids.append_array(waiting_peers)
		for id in preserve_ids:
			if player_settings.has(id):
				preserved_player_settings[id] = player_settings[id]
	race_active = false
	race_player_ids.clear()
	race_cpu_player_ids.clear()
	_disconnected_during_race.clear()
	pending_inputs.clear()
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
	player_eliminations.clear()
	pending_next_race_track_index = -1
	pending_next_race_settings.clear()
	pending_next_race_options.clear()
	sticker_cooldown_msec.clear()
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
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	last_server_input_tick = -1
	latest_state_tick = -1
	_clear_state_chunk_buffers()
	use_physics_ticks = 1.0
	last_target_tick_update = Time.get_ticks_msec()
	desired_ahead_ticks = 0.0 if is_server else 2.0
	net_input_debug_prints = 0
	_reset_start_sync_state()
	player_settings.clear()
	for id in preserved_player_settings.keys():
		player_settings[id] = preserved_player_settings[id]
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
	return float(server_netcode_session.get_max_peer_desired_ahead(_get_active_human_roster(), _local_desired_ahead_for_shared()))

func _local_desired_ahead_for_shared() -> float:
	return 0.0 if is_server and listen_server else desired_ahead_ticks

func _parse_auth_input_sample_dump_args() -> void:
	var args := OS.get_cmdline_args()
	args.append_array(OS.get_cmdline_user_args())
	for arg in args:
		if arg == "--mxt-dump-auth-input-samples":
			dump_auth_input_samples = true
		elif arg == "--mxt-no-dump-auth-input-samples":
			dump_auth_input_samples = false
		elif arg.begins_with("--mxt-auth-input-sample-limit="):
			auth_input_sample_limit = maxi(0, int(arg.get_slice("=", 1)))
		elif arg.begins_with("--mxt-auth-input-sample-dir="):
			auth_input_sample_dir = arg.get_slice("=", 1)
		elif arg == "--mxt-dump-state-samples" or arg == "--mxt-dump-gamestate-samples":
			dump_state_samples = true
		elif arg == "--mxt-no-dump-state-samples" or arg == "--mxt-no-dump-gamestate-samples":
			dump_state_samples = false
		elif arg.begins_with("--mxt-state-sample-limit="):
			state_sample_limit = maxi(0, int(arg.get_slice("=", 1)))
		elif arg.begins_with("--mxt-gamestate-sample-limit="):
			state_sample_limit = maxi(0, int(arg.get_slice("=", 1)))
		elif arg.begins_with("--mxt-state-sample-dir="):
			state_sample_dir = arg.get_slice("=", 1)
		elif arg.begins_with("--mxt-gamestate-sample-dir="):
			state_sample_dir = arg.get_slice("=", 1)

func _apply_auth_input_sample_dump_settings() -> void:
	netcode_session.configure_authoritative_input_sample_dump(
		dump_auth_input_samples,
		auth_input_sample_limit,
		auth_input_sample_dir
	)
	server_netcode_session.configure_authoritative_input_sample_dump(
		dump_auth_input_samples,
		auth_input_sample_limit,
		auth_input_sample_dir
	)
	state_sample_index = 0
	if dump_state_samples:
		var resolved_dir := ProjectSettings.globalize_path(state_sample_dir)
		DirAccess.make_dir_recursive_absolute(resolved_dir)

func dump_state_sample(state: PackedByteArray, tick: int, racer_count: int) -> void:
	if !dump_state_samples:
		return
	if state.size() <= 0:
		return
	if state_sample_limit > 0 and state_sample_index >= state_sample_limit:
		return
	var resolved_dir := ProjectSettings.globalize_path(state_sample_dir)
	var file_name := "state_%08d_t%d_p%d.bin" % [state_sample_index, tick, racer_count]
	var file := FileAccess.open(resolved_dir.path_join(file_name), FileAccess.WRITE)
	if file == null:
		return
	file.store_buffer(state)
	file.close()
	state_sample_index += 1

func _ready() -> void:
	_parse_auth_input_sample_dump_args()
	_apply_auth_input_sample_dump_settings()
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
	_settings_resync_timer = Timer.new()
	_settings_resync_timer.ignore_time_scale = true
	_settings_resync_timer.wait_time = PLAYER_SETTINGS_RESYNC_INTERVAL_SEC
	_settings_resync_timer.one_shot = false
	_settings_resync_timer.timeout.connect(_resync_player_settings)
	add_child(_settings_resync_timer)
	_settings_resync_timer.start()
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
		var server_behind := maxi(target_tick - server_tick, 0)
		log_server_behind_ticks_sum += server_behind
		log_server_behind_ticks_samples += 1
		log_server_behind_ticks_max = maxi(log_server_behind_ticks_max, server_behind)
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
			var _stall_t0 := Time.get_ticks_usec()
			_check_client_stalls()
			var _stall_t1 := Time.get_ticks_usec()
			prof_check_client_stalls_us_interval += _stall_t1 - _stall_t0
	if !is_server and !listen_server and Time.get_ticks_msec() > last_target_tick_update + 17 and game_sim != null and game_sim.sim_started:
		var old_clients_target_tick : int = clients_target_tick
		clients_target_tick += 1
		if clients_target_tick > clients_server_tick + MAX_AHEAD_TICKS:
			clients_target_tick = clients_server_tick + MAX_AHEAD_TICKS
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_advances += clients_target_tick - old_clients_target_tick

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
			var initial_target := int(ceil(clamp(_local_desired_ahead_for_shared(), 0.0, float(MAX_AHEAD_TICKS))))
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
	peer_client_rtt_s[sender] = client_rtt_s
	server_netcode_session.set_peer_desired_ahead(sender, start_sync_peer_ahead[sender])
	_try_schedule_synced_start()

func host(port: int = 27016, max_players: int = 64, dedicated: bool = false) -> int:
	disconnect_from_server()
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_players)
	if err != OK:
		push_error("Failed to host: %s" % err)
		return err
	push_error("Host!")
	multiplayer.multiplayer_peer = peer
	is_server = true
	network_active = true
	listen_server = !dedicated
	server_tick = 0
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	peer_client_rtt_s.clear()
	delayed_peer_ids.clear()
	server_netcode_session.clear_peer_state()
	desired_ahead_ticks = 0.0
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
	server_netcode_session.clear_peer_state()
	last_server_input_tick = -1
	latest_state_tick = -1
	_clear_state_chunk_buffers()
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
	disconnect_from_server()
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client(ip, port)
	if err != OK:
		push_error("Failed to join server: %s" % err)
		return err
	push_error("Client!")
	multiplayer.multiplayer_peer = peer
	is_server = false
	network_active = true
	listen_server = false
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	peer_client_rtt_s.clear()
	delayed_peer_ids.clear()
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
	last_server_input_tick = -1
	latest_state_tick = -1
	_clear_state_chunk_buffers()
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
		if peer_client_rtt_s.has(id):
			peer_client_rtt_s.erase(id)
		if delayed_peer_ids.has(id):
			delayed_peer_ids.erase(id)
		if player_settings.has(id):
			player_settings.erase(id)
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
		_send_cpu_roster_to_peer(id)
		sync_race_options.rpc_id(id, race_options)
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
		_send_cpu_roster_to_peer(id)
		sync_race_options.rpc_id(id, race_options)
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
		var settings = player_settings.get(id, {})
		var spec = typeof(settings) == TYPE_DICTIONARY and settings.get("spectator", false)
		if spec:
			if not spectator_ids.has(id):
				spectator_ids.append(id)
				new_ids.append(id)
		elif not player_ids.has(id):
			player_ids.append(id)
			new_ids.append(id)
		last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
		peer_desired_ahead[id] = 0.0
		server_netcode_session.set_peer_last_received(id, -1, last_input_time[id])
		server_netcode_session.set_peer_desired_ahead(id, 0.0)
		if !race_active:
			for pid in player_settings.keys():
				update_player_settings.rpc_id(id, player_settings[pid], pid)
	waiting_peers.clear()
	_update_player_ids.rpc(player_ids)
	_calc_state_offsets()
	for id in new_ids:
		_send_cpu_roster_to_peer(id)
		if !race_active and player_settings.has(id):
			update_player_settings.rpc(player_settings[id], id)

@rpc("any_peer", "reliable")
func _update_player_ids(ids: Array) -> void:
	player_ids = ids
	if is_server:
		_calc_state_offsets()

@rpc("any_peer", "reliable")
func start_race(track_index: int, settings: Array, options: Dictionary = {}) -> void:
	prepare_race_roster("start_race")
	_reset_start_sync_state()
	if !options.is_empty():
		race_options = options.duplicate(true)
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
			start_sync_peer_ahead[local_id] = _local_desired_ahead_for_shared()

func send_start_race(track_index: int, settings: Array, options: Dictionary = {}) -> void:
	if is_server:
		ready_players.clear()
		if options.is_empty():
			options = race_options.duplicate(true)
		else:
			race_options = options.duplicate(true)
		# Generate and distribute a shared spawn seed before starting the race.
		# This lets all peers randomize starting grid slots deterministically.
		var seed := randi()
		set_spawn_seed.rpc(seed)
		set_spawn_seed(seed)
		start_race.rpc(track_index, settings, options)
		start_race(track_index, settings, options)
		if player_ids.size() > 1:
			if game_sim != null:
					game_sim.set_sim_started(false)
			if server_game_sim != null:
					server_game_sim.set_sim_started(false)
		else:
			begin_simulation()
	else:
		start_race.rpc_id(1, track_index, settings, options)

@rpc("any_peer", "reliable")
func end_race(next_track_index: int = -1, next_settings: Array = [], next_options: Dictionary = {}) -> void:
	pending_next_race_track_index = next_track_index
	pending_next_race_settings = next_settings.duplicate(true)
	pending_next_race_options = next_options.duplicate(true)
	if !pending_next_race_options.is_empty():
		race_options = pending_next_race_options.duplicate(true)
	race_active = false
	emit_signal("race_finished")

func send_end_race(next_track_index: int = -1, next_settings: Array = [], next_options: Dictionary = {}) -> void:
	if is_server:
		end_race.rpc(next_track_index, next_settings, next_options)
		end_race(next_track_index, next_settings, next_options)

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
		start_sync_peer_ahead[local_id] = _local_desired_ahead_for_shared()
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
	var max_ahead := _local_desired_ahead_for_shared() if listen_server else 0.0
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
	var client_lead_msec := int(ceil(clamp(_local_desired_ahead_for_shared(), 0.0, float(MAX_AHEAD_TICKS)) * 1000.0 / 60.0))
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
	if race_active:
		return
	var my_id := multiplayer.get_unique_id()
	if is_server:
		update_player_settings(settings, my_id)
		update_player_settings.rpc(settings, my_id)
	else:
		update_player_settings.rpc_id(1, settings)
		player_settings[my_id] = settings

@rpc("any_peer", "reliable")
func update_player_settings(settings: Dictionary, id: int = -1) -> void:
	if race_active:
		return
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
			desired_ahead_ticks = 2.0 if !is_server else 0.0
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

func _get_local_player_settings_snapshot() -> Dictionary:
	if game_manager != null and game_manager.car_settings != null:
		var ps = game_manager.car_settings.get_player_settings()
		if ps != null:
			return ps.to_dict()
	var my_id := multiplayer.get_unique_id()
	var settings = player_settings.get(my_id, {})
	if typeof(settings) == TYPE_DICTIONARY:
		return settings
	return {}

func _settings_resync_recipients() -> Array:
	var recipients := []
	for source in [player_ids, spectator_ids, waiting_peers]:
		for id in source:
			if cpu_player_ids.has(id):
				continue
			if recipients.has(id):
				continue
			recipients.append(id)
	return recipients

func _rebroadcast_player_settings_to_peer(peer_id: int) -> void:
	for pid in player_settings.keys():
		update_player_settings.rpc_id(peer_id, player_settings[pid], pid)

func _resync_player_settings() -> void:
	if game_manager != null and game_manager.singleplayer_mode:
		return
	if !has_network_peer():
		return
	if race_active:
		return
	var local_settings := _get_local_player_settings_snapshot()
	if !local_settings.is_empty():
		var my_id := multiplayer.get_unique_id()
		player_settings[my_id] = local_settings
		if is_server:
			update_player_settings(local_settings, my_id)
		else:
			update_player_settings.rpc_id(1, local_settings)
	if !is_server:
		return
	for peer_id in _settings_resync_recipients():
		if int(peer_id) == multiplayer.get_unique_id():
			continue
		_rebroadcast_player_settings_to_peer(int(peer_id))

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
	var has_disconnected_peers := !_disconnected_during_race.is_empty()
	if !can_mark_new_delayed and delayed_peer_ids.is_empty() and !has_disconnected_peers:
		return
	if not pending_inputs.has(tick):
		pending_inputs[tick] = {}
	var waiting: Dictionary = pending_inputs[tick]
	var roster_chk := _get_human_roster()
	var replacement_stats: Dictionary = server_netcode_session.fill_missing_pending_inputs(
		tick,
		roster_chk,
		_disconnected_during_race.keys(),
		delayed_peer_ids.keys(),
		can_mark_new_delayed
	)
	var replaced_ids: Array = replacement_stats.get("replaced_ids", [])
	for pid in replaced_ids:
		waiting[pid] = NEUTRAL_INPUT_BYTES
		delayed_peer_ids[pid] = true
		log_server_replacements += 1
		_log_add_int(log_peer_replacements, pid, 1)
	pending_inputs[tick] = waiting

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
	if server_game_sim != null and server_game_sim.has_method("get_native_cpu_input_for_tick"):
		for id in _get_human_roster():
			if player_finish_times.has(id):
				var cpu_input: PackedByteArray = server_game_sim.get_native_cpu_input_for_tick(int(id), server_tick)
				pending_inputs[server_tick][id] = cpu_input
				server_netcode_session.store_pending_input(server_tick, int(id), cpu_input)
	if server_tick > target_tick:
		return {}
	_fill_delayed_missing_inputs_for_tick(server_tick)
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
		if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
			log_client_ahead_throttle_frames += 1
			return {}
		netcode_session.tick_client_predicted_frame(game_sim, local_tick)
		var spectator_frame := netcode_session.get_frame_as_dictionary(local_tick)
		input_history[local_tick] = spectator_frame
		if input_history.has(local_tick - INPUT_HISTORY_SIZE):
			input_history.erase(local_tick - INPUT_HISTORY_SIZE)
		local_tick += 1
		log_client_sim_ticks += 1
		_adjust_time_scale()
		return spectator_frame
	if _startup_light_net_active(local_tick):
		_store_neutral_authoritative_frame_for_all_racers(local_tick)
		netcode_session.store_local_input(local_tick, NEUTRAL_INPUT_BYTES)
		if !is_server:
			_client_startup_sync.rpc_id(1, desired_ahead_ticks, local_tick, rtt_s)
			_acc_log_out(12)
		netcode_session.tick_client_predicted_frame(game_sim, local_tick)
		var startup_frame_inputs := netcode_session.get_frame_as_dictionary(local_tick)
		input_history[local_tick] = startup_frame_inputs
		if input_history.has(local_tick - INPUT_HISTORY_SIZE):
			input_history.erase(local_tick - INPUT_HISTORY_SIZE)
		local_tick += 1
		log_client_sim_ticks += 1
		_adjust_time_scale()
		return startup_frame_inputs
	if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
		log_client_ahead_throttle_frames += 1
		if !is_server:
			var old_keys := sent_inputs_bytes.keys()
			if old_keys.size() > 0:
				var recent_keys := _recent_unacked_input_keys(old_keys)
				var start := int(recent_keys[0])
				var packet: PackedByteArray = netcode_session.build_local_input_packet(start, recent_keys.size())
				log_flat_client_payload_out += packet.size()
				_acc_log_out(12 + packet.size())
				_client_send_input_flat.rpc_id(1, packet, desired_ahead_ticks, rtt_s)
				last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		return {}
	var local_input_for_tick: PackedByteArray = last_local_input_bytes
	var local_player_id := multiplayer.get_unique_id()
	if player_finish_times.has(local_player_id) and game_sim != null and game_sim.has_method("get_native_cpu_input_for_tick"):
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
		_client_send_input_flat.rpc_id(1, input_packet, desired_ahead_ticks, rtt_s)
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
	netcode_session.tick_client_predicted_frame(game_sim, local_tick)
	var frame_inputs := netcode_session.get_frame_as_dictionary(local_tick)
	input_history[local_tick] = frame_inputs
	if input_history.has(local_tick - INPUT_HISTORY_SIZE):
		input_history.erase(local_tick - INPUT_HISTORY_SIZE)
	local_tick += 1
	log_client_sim_ticks += 1
	_adjust_time_scale()
	return frame_inputs

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_startup_sync(ahead: float, client_tick: int, client_rtt_s: float) -> void:
	if !race_active:
		return
	if !is_server:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if !player_ids.has(sender_id):
		return
	var now_sec := 0.001 * float(Time.get_ticks_msec())
	peer_desired_ahead[sender_id] = ahead
	peer_client_rtt_s[sender_id] = client_rtt_s
	server_netcode_session.set_peer_desired_ahead(sender_id, ahead)
	last_input_time[sender_id] = now_sec
	last_received_tick[sender_id] = max(int(last_received_tick.get(sender_id, -1)), min(client_tick, STARTUP_LIGHT_NET_TICKS - 1))
	server_netcode_session.set_peer_last_received(sender_id, int(last_received_tick[sender_id]), now_sec)
	_acc_log_in(12)
	_prune_authoritative_history()

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_send_input_flat(packet: PackedByteArray, ahead: float, client_rtt_s: float) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if is_server:
		var sender_id := multiplayer.get_remote_sender_id()
		var now_sec := 0.001 * float(Time.get_ticks_msec())
		var sender_seen_before: bool = server_netcode_session.peer_has_received(sender_id)
		var reject_before := maxi(target_tick - SERVER_INPUT_REPLACEMENT_BACKLOG_TICKS, server_tick)
		if !sender_seen_before:
			reject_before = server_tick
		var stats: Dictionary = server_netcode_session.store_pending_input_packet(sender_id, reject_before, packet, ahead, now_sec)
		if !bool(stats.get("valid", false)):
			return
		var start_tick := int(stats.get("start_tick", -1))
		var count := int(stats.get("count", 0))
		var accepted := int(stats.get("accepted", 0))
		var dropped := int(stats.get("dropped", 0))
		var last_tick := int(stats.get("last_tick", -1))
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
			if last_tick >= server_tick and delayed_peer_ids.has(sender_id):
				delayed_peer_ids.erase(sender_id)
		peer_desired_ahead[sender_id] = ahead
		peer_client_rtt_s[sender_id] = client_rtt_s
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
		if is_server and listen_server:
			rtt_s = 0.0
			desired_ahead_ticks = 0.0
		else:
			var sample : float = 0.001 * float(Time.get_ticks_msec()) - sent_input_times[ack_tick]
			if rtt_s == 0.0:
				rtt_s = sample
			else:
				rtt_s = lerp(rtt_s, sample, RTT_SMOOTHING)
			_update_desired_ahead()
		sent_input_times.erase(ack_tick)
	for key in sent_inputs_bytes.keys():
		if key <= last_ack_tick:
			sent_inputs_bytes.erase(key)
	for key in sent_input_times.keys():
		if key <= last_ack_tick:
			sent_input_times.erase(key)

@rpc("any_peer", "unreliable", "call_local", 3)
func _server_startup_sync(server_tick_value: int, this_ack: int, tgt: int, max_ahead: float) -> void:
	if !race_active:
		return
	if not is_server or listen_server:
		var old_clients_server_tick : int = clients_server_tick
		var old_clients_target_tick : int = clients_target_tick
		clients_server_tick = max(clients_server_tick, server_tick_value + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		if clients_server_tick > old_clients_server_tick:
			log_client_server_tick_advances += clients_server_tick - old_clients_server_tick
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_remote_advances += clients_target_tick - old_clients_target_tick
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
		last_server_input_tick = max(last_server_input_tick, server_tick_value)
	_apply_server_input_ack(this_ack)

@rpc("any_peer", "unreliable_ordered", "call_local", 2)
func _server_broadcast_flat(authoritative_last_tick: int, input_packet: PackedByteArray, this_ack: int, tgt: int, max_ahead: float) -> void:
	if !race_active:
		return
	var __prof_t0 := Time.get_ticks_usec()
	if not is_server or listen_server:
		var old_clients_server_tick : int = clients_server_tick
		var old_clients_target_tick : int = clients_target_tick
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
				var rollback_start := maxi(first_tick, local_tick - AUTH_INPUT_ROLLBACK_WINDOW_TICKS)
				if last_tick >= rollback_start:
					_handle_input_update_flat(rollback_start)
				last_server_input_tick = max(last_server_input_tick, last_tick)
			else:
				clients_server_tick = max(clients_server_tick, authoritative_last_tick + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		if clients_server_tick > old_clients_server_tick:
			log_client_server_tick_advances += clients_server_tick - old_clients_server_tick
		if clients_target_tick > old_clients_target_tick:
			log_client_target_tick_remote_advances += clients_target_tick - old_clients_target_tick
		last_target_tick_update = Time.get_ticks_msec()
		clients_max_ahead_from_server = max_ahead
		log_flat_server_payload_in += input_packet.size()
	_apply_server_input_ack(this_ack)
	_acc_log_in(20 + input_packet.size())
	var __prof_t1 := Time.get_ticks_usec()
	prof_server_broadcast_recv_us_interval += __prof_t1 - __prof_t0

@rpc("any_peer", "unreliable", "call_local", 4)
func _server_state_chunk(state_tick: int, state_uncompressed_size: int, state_payload_size: int, chunk_index: int, chunk_count: int, chunk: PackedByteArray) -> void:
	if !race_active:
		return
	if chunk.size() <= 0:
		return
	if state_tick <= latest_state_tick:
		return
	if state_payload_size <= 0 or chunk_count <= 0 or chunk_index < 0 or chunk_index >= chunk_count:
		return
	var __prof_t0 := Time.get_ticks_usec()
	_acc_log_in(20 + chunk.size())
	var record = pending_state_chunks.get(state_tick)
	if typeof(record) != TYPE_DICTIONARY:
		var chunks := []
		var received := []
		chunks.resize(chunk_count)
		received.resize(chunk_count)
		for i in range(chunk_count):
			received[i] = false
		record = {
			"raw_size": state_uncompressed_size,
			"payload_size": state_payload_size,
			"chunk_count": chunk_count,
			"chunks": chunks,
			"received": received,
			"received_count": 0,
		}
		pending_state_chunks[state_tick] = record
	else:
		if int(record.get("payload_size", -1)) != state_payload_size or int(record.get("chunk_count", -1)) != chunk_count:
			pending_state_chunks.erase(state_tick)
			var __prof_t_bad := Time.get_ticks_usec()
			prof_server_broadcast_recv_us_interval += __prof_t_bad - __prof_t0
			return
	var received_arr: Array = record["received"]
	if bool(received_arr[chunk_index]):
		var __prof_t_dup := Time.get_ticks_usec()
		prof_server_broadcast_recv_us_interval += __prof_t_dup - __prof_t0
		return
	received_arr[chunk_index] = true
	var chunks_arr: Array = record["chunks"]
	chunks_arr[chunk_index] = chunk
	record["received_count"] = int(record["received_count"]) + 1
	if int(record["received_count"]) < chunk_count:
		var __prof_t_wait := Time.get_ticks_usec()
		prof_server_broadcast_recv_us_interval += __prof_t_wait - __prof_t0
		return
	var state := PackedByteArray()
	state.resize(state_payload_size)
	var offset := 0
	for i in range(chunk_count):
		var part: PackedByteArray = chunks_arr[i]
		for j in range(part.size()):
			state[offset + j] = part[j]
		offset += part.size()
	pending_state_chunks.erase(state_tick)
	var raw_state_size := state_uncompressed_size if state_uncompressed_size > 0 else state_payload_size
	_log_state_received(raw_state_size, state_payload_size)
	var state_to_use := state
	if state_uncompressed_size > 0:
		state_to_use = state.decompress(state_uncompressed_size, FileAccess.COMPRESSION_ZSTD)
	_handle_state(state_tick, state_to_use)
	_prune_state_chunk_buffers()
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
		var input_packet: PackedByteArray = PackedByteArray()
		var input_packet_ready := false
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
					dump_state_sample(state, server_tick, get_simulation_roster().size())
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
			if not input_packet_ready:
				input_packet = server_netcode_session.build_authoritative_input_packet(server_tick, AUTH_INPUT_REDUNDANCY_FRAMES)
				input_packet_ready = true
			log_flat_server_payload_out += input_packet.size()
			log_auth_packets_sent += 1
			_acc_log_out(20 + input_packet.size())
			_server_broadcast_flat.rpc_id(id, server_tick, input_packet, server_netcode_session.get_peer_last_received(id), target_tick, max_ahead)
			if send_state.size() > 0:
				var chunk_count := int(ceil(float(send_state.size()) / float(STATE_CHUNK_PAYLOAD_BYTES)))
				var state_chunks := []
				state_chunks.resize(chunk_count)
				for chunk_index in range(chunk_count):
					var chunk_start := chunk_index * STATE_CHUNK_PAYLOAD_BYTES
					var chunk_end = mini(chunk_start + STATE_CHUNK_PAYLOAD_BYTES, send_state.size())
					state_chunks[chunk_index] = send_state.slice(chunk_start, chunk_end)
				for copy_index in range(STATE_CHUNK_SEND_COPIES):
					for chunk_index in range(chunk_count):
						var chunk: PackedByteArray = state_chunks[chunk_index]
						_acc_log_out(20 + chunk.size())
						_server_state_chunk.rpc_id(id, server_tick, send_state_uncomp_size, send_state.size(), chunk_index, chunk_count, chunk)
		server_tick += 1
		if listen_server:
			_prune_authoritative_history()
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
	var roster_chk : Array = _get_active_human_roster()
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
	network_active = false
	listen_server = false
	game_sim = null
	server_game_sim = null
	player_ids.clear()
	spectator_ids.clear()
	waiting_peers.clear()
	race_player_ids.clear()
	race_cpu_player_ids.clear()
	cpu_player_ids.clear()
	cpu_player_settings.clear()
	player_eliminations.clear()
	pending_next_race_track_index = -1
	pending_next_race_settings.clear()
	pending_next_race_options.clear()
	_disconnected_during_race.clear()
	pending_inputs.clear()
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
	player_settings.clear()
	ready_players.clear()
	_unverified_peers.clear()
	_version_request_time.clear()
	state_send_offsets.clear()
	net_race_finish_time = -1
	player_finish_times.clear()
	player_finish_placements.clear()
	finish_order.clear()
	sticker_cooldown_msec.clear()
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
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	last_server_input_tick = -1
	latest_state_tick = -1
	_clear_state_chunk_buffers()
	use_physics_ticks = 1.0
	Engine.physics_ticks_per_second = 60
	server_netcode_session.clear_peer_state()
	netcode_session.reset()
	server_netcode_session.reset()
	_reset_start_sync_state()
	_sync_cpu_manager()

func _prune_authoritative_history() -> void:
	var window_cutoff := server_tick - MAX_HISTORY_TICKS
	if window_cutoff < 0:
		return
	for key in authoritative_history.keys():
		if key <= window_cutoff:
			authoritative_history.erase(key)

func _update_desired_ahead() -> void:
	if is_server and listen_server:
		desired_ahead_ticks = 0.0
		return
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

@rpc("authority", "call_local", "reliable")
func set_player_finished(id: int, tick: int, place: int) -> void:
	var is_new := !player_finish_times.has(id)
	player_finish_times[id] = tick
	player_finish_placements[id] = place
	if finish_order.size() < place:
		finish_order.resize(place)
	finish_order[place - 1] = id
	if is_new:
		race_event.emit("finish", id, -1, tick, place)

func send_player_finished(id: int, tick: int) -> void:
	if player_finish_times.has(id):
		return
	var place := finish_order.size() + 1
	if is_server:
		set_player_finished.rpc(id, tick, place)
		set_player_finished(id, tick, place)

func record_player_finished(id: int, tick: int) -> void:
	if player_finish_times.has(id):
		return
	var place := finish_order.size() + 1
	set_player_finished(id, tick, place)

@rpc("authority", "call_local", "reliable")
func set_player_eliminated(id: int, tick: int) -> void:
	var is_new := !player_eliminations.has(id)
	player_eliminations[id] = tick
	if is_new:
		race_event.emit("eliminated", id, -1, tick, 0)

func send_player_eliminated(id: int, tick: int) -> void:
	if player_eliminations.has(id):
		return
	if is_server:
		set_player_eliminated.rpc(id, tick)
		set_player_eliminated(id, tick)

func is_vehicle_restore_enabled() -> bool:
	return bool(race_options.get("vehicle_restore", true))

func is_grand_prix_enabled() -> bool:
	return int(race_options.get("game_mode", 0)) == 1

@rpc("authority", "call_local", "reliable")
func sync_race_options(options: Dictionary) -> void:
	race_options = options.duplicate(true)
	race_options_changed.emit(race_options.duplicate(true))

func send_race_options(options: Dictionary) -> void:
	if is_server:
		sync_race_options.rpc(options)
		sync_race_options(options)

@rpc("authority", "call_local", "reliable")
func receive_race_event(event_type: String, actor_id: int, target_id: int, tick: int, value: int) -> void:
	race_event.emit(event_type, actor_id, target_id, tick, value)

func send_race_event(event_type: String, actor_id: int, target_id: int, tick: int, value: int) -> void:
	if is_server:
		receive_race_event.rpc(event_type, actor_id, target_id, tick, value)

@rpc("any_peer", "reliable")
func request_sticker(sticker_index: int) -> void:
	if !race_active:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = multiplayer.get_unique_id()
	var now := Time.get_ticks_msec()
	var last := int(sticker_cooldown_msec.get(sender, 0))
	if now < last + 500:
		return
	sticker_cooldown_msec[sender] = now
	if is_server:
		send_race_event("sticker", sender, -1, get_race_tick(), sticker_index)

func send_sticker(sticker_index: int) -> void:
	if is_server or !has_network_peer():
		request_sticker(sticker_index)
	else:
		request_sticker.rpc_id(1, sticker_index)
