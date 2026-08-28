class_name NetworkTelemetryController
extends Node

const TELEMETRY_HISTORY_CAPACITY := 600

var game_manager: Node
var custom_stamp_network: Node
var state_transfer: Node
var lobby_settings: Node
var race_admission: Node
var input_transport: Node

var is_server := false
var listen_server := false
var network_active := false
var player_ids: Array = []
var active_human_roster: Array = []

var log_lobby_frame_samples := 0
var log_lobby_frame_us := 0
var log_lobby_frame_max_us := 0
var log_lobby_player_list_us := 0
var log_lobby_player_list_max_us := 0
var log_lobby_chibi_us := 0
var log_lobby_chibi_max_us := 0
var log_lobby_render_rebuilds := 0
var log_lobby_render_rebuild_us := 0
var log_lobby_render_rebuild_max_us := 0
var log_lobby_chibi_in := 0
var log_lobby_chibi_out := 0
var log_lobby_chibi_bytes_in := 0
var log_lobby_chibi_bytes_out := 0
var log_lobby_peer_connects := 0
var log_lobby_peer_disconnects := 0

var dump_state_samples := false
var state_sample_limit := 5000
var state_sample_dir := "user://state_samples"
var state_sample_index := 0

var log_bytes_out_total := 0
var log_bytes_in_total := 0
var log_bytes_out_interval := 0
var log_bytes_in_interval := 0
var telemetry_history: Array[String] = []
var telemetry_history_start := 0
var telemetry_history_count := 0
var _log_timer: Timer

func initialize(in_game_manager: Node, in_custom_stamp_network: Node, in_state_transfer: Node, in_lobby_settings: Node, in_race_admission: Node, in_input_transport: Node) -> void:
	game_manager = in_game_manager
	custom_stamp_network = in_custom_stamp_network
	state_transfer = in_state_transfer
	lobby_settings = in_lobby_settings
	race_admission = in_race_admission
	input_transport = in_input_transport
	_parse_state_sample_dump_args()
	_apply_state_sample_dump_settings()
	telemetry_history.resize(TELEMETRY_HISTORY_CAPACITY)
	_log_timer = Timer.new()
	_log_timer.wait_time = 1.0
	_log_timer.one_shot = false
	_log_timer.timeout.connect(_capture_telemetry_sample)
	add_child(_log_timer)
	_log_timer.start()

func set_context(server: bool, listen: bool, active_network: bool, humans: Array, active_humans: Array) -> void:
	is_server = server
	listen_server = listen
	network_active = active_network
	player_ids = humans.duplicate()
	active_human_roster = active_humans.duplicate()

func has_network_peer() -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if is_server:
		return true
	return multiplayer.multiplayer_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED

func record_peer_connected() -> void:
	log_lobby_peer_connects += 1

func record_peer_disconnected() -> void:
	log_lobby_peer_disconnects += 1

func _format_log_float(value: float, decimals: int = 2) -> String:
	var scale := pow(10.0, float(decimals))
	return str(round(value * scale) / scale)

func _client_unacked_stats() -> Dictionary:
	var out := {
		"count": input_transport.sent_inputs_bytes.size(),
		"oldest": -1,
		"newest": -1,
	}
	for key in input_transport.sent_inputs_bytes.keys():
		var tick := int(key)
		if int(out["oldest"]) < 0 or tick < int(out["oldest"]):
			out["oldest"] = tick
		if tick > int(out["newest"]):
			out["newest"] = tick
	return out

func _client_target_ahead_ticks() -> float:
	var local_desired_ahead: float = input_transport._local_desired_ahead_for_shared()
	var shared_ahead_limit = max(local_desired_ahead, input_transport.SHARED_AHEAD_CAP_TICKS)
	var shared_ahead_target = min(input_transport.clients_max_ahead_from_server, local_desired_ahead + input_transport.SHARED_AHEAD_EXTRA_TICKS, shared_ahead_limit)
	return max(local_desired_ahead, shared_ahead_target)

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
	var ids := active_human_roster
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
		var last_recv := int(input_transport.last_received_tick.get(pid, input_transport.server_netcode_session.get_peer_last_received(pid)))
		var server_lag := 0
		var target_lag := 0
		if last_recv >= 0:
			server_lag = maxi(input_transport.server_tick - last_recv, 0)
			target_lag = maxi(input_transport.target_tick - last_recv, 0)
		else:
			server_lag = input_transport.server_tick + 1
			target_lag = input_transport.target_tick + 1
		var ahead := float(input_transport.peer_desired_ahead.get(pid, 0.0))
		var rtt_ms := -1.0
		if input_transport.peer_client_rtt_s.has(pid):
			rtt_ms = 1000.0 * float(input_transport.peer_client_rtt_s[pid])
			rtt_sum_ms += rtt_ms
			rtt_count += 1
			rtt_max_ms = max(rtt_max_ms, rtt_ms)
		var accepted := int(input_transport.log_peer_inputs_accepted.get(pid, 0))
		var dropped := int(input_transport.log_peer_inputs_dropped.get(pid, 0))
		var replacements := int(input_transport.log_peer_replacements.get(pid, 0))
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
		var delayed := 1 if input_transport.delayed_peer_ids.has(pid) else 0
		var since_ms := -1
		if input_transport.last_input_time.has(pid):
			since_ms = int(round((0.001 * float(Time.get_ticks_msec()) - float(input_transport.last_input_time[pid])) * 1000.0))
		var pkt_start := int(input_transport.log_peer_last_packet_start.get(pid, -1))
		var pkt_count := int(input_transport.log_peer_last_packet_count.get(pid, 0))
		var pkt_accept := int(input_transport.log_peer_last_packet_accept.get(pid, 0))
		var pkt_drop := int(input_transport.log_peer_last_packet_drop.get(pid, 0))
		var pkt_reject_before := int(input_transport.log_peer_last_packet_reject_before.get(pid, -1))
		var pkt_last_tick := int(input_transport.log_peer_last_packet_last_tick.get(pid, -1))
		var pkt_server_lead := int(input_transport.log_peer_last_packet_server_lead.get(pid, -9999))
		var pkt_target_lead := int(input_transport.log_peer_last_packet_target_lead.get(pid, -9999))
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
	if input_transport.log_peer_packet_server_lead_samples > 0:
		out["peer_input_server_lead_min"] = input_transport.log_peer_packet_server_lead_min
		out["peer_input_server_lead_max"] = input_transport.log_peer_packet_server_lead_max
		out["peer_input_server_lead_avg"] = input_transport.log_peer_packet_server_lead_sum / float(input_transport.log_peer_packet_server_lead_samples)
	if input_transport.log_peer_packet_target_lead_samples > 0:
		out["peer_input_target_lead_min"] = input_transport.log_peer_packet_target_lead_min
		out["peer_input_target_lead_max"] = input_transport.log_peer_packet_target_lead_max
		out["peer_input_target_lead_avg"] = input_transport.log_peer_packet_target_lead_sum / float(input_transport.log_peer_packet_target_lead_samples)
	var snapshot := ""
	for i in range(snapshot_parts.size()):
		if i > 0:
			snapshot += "|"
		snapshot += str(snapshot_parts[i])
	out["peer_snapshot"] = snapshot
	return out

func _telemetry_csv_header() -> String:
	return "time,role,uid,is_server,listen,players,server_tick,target_tick,server_behind_ticks,server_behind_avg,server_behind_max,delayed_peers,local_tick,clients_server_tick,clients_target_tick,rtt,rtt_variance,input_forward_redundancy,desired_ahead,server_max_ahead,physics_tps,start_server_ms,start_local_ms,actual_client_start_ms,actual_server_start_ms,first_auth_ms,first_auth_first_tick,first_auth_last_tick,first_auth_count,up_kbps,down_kbps,up_total_kb,down_total_kb,inputs_sent,inputs_acked,retrans,flat_client_out,flat_client_in,flat_server_out,flat_server_in,late_drops,replacements,state_raw_out,state_payload_out,state_sent,state_max_frags_out,state_min_success_2pct,state_payload_in,state_raw_in,state_recv,state_max_recv_gap_ms,auth_packets,auth_packet_builds,auth_compression_candidates,auth_build_ms,auth_frames,auth_encoded_inputs,auth_unchanged_inputs,auth_payload_per_packet,auth_raw_per_packet,auth_compression_ratio,auth_redundancy_frames,auth_rollback_window,net_cpu_ms,sim_cpu_ms,rollback_avg_ms,rollback_max_ms,collect_inputs_ms,idle_broadcast_ms,check_client_stalls_ms,client_send_input_ms,server_broadcast_recv_ms,handle_state_ms,handle_input_update_ms,recalc_pred_ms,adjust_time_scale_ms,car_store_old_pos_ms,car_post_render_ms,client_current_ahead,client_target_ahead,client_ahead_error,client_server_gap,client_sent_buffer,client_unacked_oldest,client_unacked_newest,client_last_ack_tick,client_ack_lag,client_throttle_frames,use_physics_ticks,client_sim_ticks,client_target_tick_advances,client_target_tick_remote_advances,client_server_tick_advances,client_ahead_samples,client_current_ahead_min,client_current_ahead_max,client_current_ahead_avg,client_target_ahead_avg,client_ahead_error_min,client_ahead_error_max,client_ahead_error_avg,client_pre_auth_adjust_samples,server_peer_lag_max,server_peer_lag_avg,target_peer_lag_max,target_peer_lag_avg,peer_ahead_min,peer_ahead_max,peer_ahead_avg,peer_rtt_max_ms,peer_rtt_avg_ms,peer_inputs_accepted,peer_inputs_dropped,peer_replacements,peer_input_server_lead_min,peer_input_server_lead_max,peer_input_server_lead_avg,peer_input_target_lead_min,peer_input_target_lead_max,peer_input_target_lead_avg,peer_snapshot,timing_ping_out,timing_ping_in,timing_sync_out,timing_sync_in,timing_sync_rtt_ms_avg,timing_sync_rtt_ms_max,timing_sync_server_gap_max,timing_sync_target_gap_max,timing_ack_advance,state_chunk_out,state_chunk_in,state_chunk_dup_in,state_chunk_stale_drop,state_chunk_bad_meta_drop,state_chunk_complete,state_chunk_complete_max_chunks,state_parity_chunks_out,state_fec_recovered_chunks,state_fec_abandoned,state_pending_records,state_pending_best_recv_pct,state_pending_best_missing,state_pending_oldest_tick,state_pending_newest_tick,state_sec_header,state_sec_bumper_meta,state_sec_sparks,state_sec_car_scalars,state_sec_car_vec3,state_sec_car_basis,state_sec_car_conditionals,state_sec_car_tilt,state_sec_car_wall,state_sec_bumper_total,state_sec_triggers,state_sec_total,state_car_count,state_bumper_count,state_active_bumpers,state_active_sparks,state_trigger_count,state_car_collision_old,state_car_restore,admission_ready,admission_roster,admission_blocked,admission_snapshot,lobby_frame_samples,lobby_frame_avg_ms,lobby_frame_max_ms,lobby_player_list_avg_ms,lobby_player_list_max_ms,lobby_chibi_avg_ms,lobby_chibi_max_ms,lobby_render_rebuilds,lobby_render_rebuild_avg_ms,lobby_render_rebuild_max_ms,lobby_settings_in,lobby_settings_out,lobby_settings_bytes_in,lobby_settings_bytes_out,lobby_settings_accepted,lobby_settings_deduped,lobby_chibi_in,lobby_chibi_out,lobby_chibi_bytes_in,lobby_chibi_bytes_out,lobby_peer_connects,lobby_peer_disconnects,stamp_manifest_in,stamp_manifest_out,stamp_manifest_bytes_in,stamp_manifest_bytes_out,stamp_manifest_accepted,stamp_manifest_deduped,stamp_blob_in,stamp_blob_out,stamp_blob_bytes_in,stamp_blob_bytes_out,stamp_blob_accepted,stamp_blob_deduped,stamp_blob_queue_messages,stamp_blob_queue_bytes,engine_process_ms,engine_physics_ms,draw_calls"

func _append_telemetry_sample(line: String) -> void:
	var write_index := (telemetry_history_start + telemetry_history_count) % TELEMETRY_HISTORY_CAPACITY
	if telemetry_history_count == TELEMETRY_HISTORY_CAPACITY:
		write_index = telemetry_history_start
		telemetry_history_start = (telemetry_history_start + 1) % TELEMETRY_HISTORY_CAPACITY
	else:
		telemetry_history_count += 1
	telemetry_history[write_index] = line

func export_telemetry_history() -> Dictionary:
	if telemetry_history_count == 0:
		return {"success": false, "error": "No netplay telemetry has been captured yet."}
	var logs_path := "user://logs"
	var make_dir_error := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(logs_path))
	if make_dir_error != OK and make_dir_error != ERR_ALREADY_EXISTS:
		return {"success": false, "error": "Could not create the telemetry export directory (%d)." % make_dir_error}
	var role := "server" if is_server and !listen_server else ("listen" if is_server else "client")
	var file_path := "%s/netplay-%s-%d-%d.csv" % [
		logs_path,
		role,
		int(Time.get_unix_time_from_system()),
		multiplayer.get_unique_id(),
	]
	var file := FileAccess.open(file_path, FileAccess.WRITE)
	if file == null:
		return {"success": false, "error": "Could not create telemetry export (%d)." % FileAccess.get_open_error()}
	file.store_line(_telemetry_csv_header())
	for history_offset in range(telemetry_history_count):
		var history_index := (telemetry_history_start + history_offset) % TELEMETRY_HISTORY_CAPACITY
		file.store_line(telemetry_history[history_index])
	file.flush()
	return {
		"success": true,
		"path": ProjectSettings.globalize_path(file_path),
		"sample_count": telemetry_history_count,
	}

func record_lobby_frame(frame_us: int, player_list_us: int, chibi_us: int) -> void:
	log_lobby_frame_samples += 1
	log_lobby_frame_us += frame_us
	log_lobby_frame_max_us = maxi(log_lobby_frame_max_us, frame_us)
	log_lobby_player_list_us += player_list_us
	log_lobby_player_list_max_us = maxi(log_lobby_player_list_max_us, player_list_us)
	log_lobby_chibi_us += chibi_us
	log_lobby_chibi_max_us = maxi(log_lobby_chibi_max_us, chibi_us)

func record_lobby_render_rebuild(rebuild_us: int) -> void:
	log_lobby_render_rebuilds += 1
	log_lobby_render_rebuild_us += rebuild_us
	log_lobby_render_rebuild_max_us = maxi(log_lobby_render_rebuild_max_us, rebuild_us)

func record_lobby_chibi_network(in_messages: int, in_bytes: int, out_messages: int, out_bytes: int) -> void:
	log_lobby_chibi_in += in_messages
	log_lobby_chibi_bytes_in += in_bytes
	log_lobby_chibi_out += out_messages
	log_lobby_chibi_bytes_out += out_bytes

func _capture_telemetry_sample() -> void:
	if !has_network_peer():
		return
	var bytes_out_interval: int = log_bytes_out_interval + input_transport.log_bytes_out_interval
	var bytes_in_interval: int = log_bytes_in_interval + input_transport.log_bytes_in_interval
	var bytes_out_total: int = log_bytes_out_total + input_transport.log_bytes_out_total
	var bytes_in_total: int = log_bytes_in_total + input_transport.log_bytes_in_total
	var up_kbps: float = (bytes_out_interval * 8.0) / 1000.0
	var down_kbps: float = (bytes_in_interval * 8.0) / 1000.0
	var physics_tps := Engine.physics_ticks_per_second
	var role := "server" if is_server and !listen_server else ("listen" if is_server else "client")
	var rollback_avg_ms = (float(input_transport.log_rollback_us_sum) / max(input_transport.log_rollback_us_count, 1)) / 1000.0
	var rollback_max_ms := float(input_transport.log_rollback_us_max) / 1000.0
	var net_cpu_ms := float(input_transport.log_net_cpu_us_interval) / 1000.0
	var sim_cpu_ms := float(input_transport.log_sim_cpu_us_interval) / 1000.0
	var collect_inputs_ms := float(input_transport.prof_collect_server_inputs_us_interval) / 1000.0
	var idle_broadcast_ms := float(input_transport.prof_idle_broadcast_us_interval) / 1000.0
	var check_client_stalls_ms := float(input_transport.prof_check_client_stalls_us_interval) / 1000.0
	var client_send_input_ms := float(input_transport.prof_client_send_input_us_interval) / 1000.0
	var server_broadcast_recv_ms := float(input_transport.prof_server_broadcast_recv_us_interval + state_transfer.receive_profile_usec) / 1000.0
	var handle_state_ms := float(input_transport.prof_handle_state_us_interval) / 1000.0
	var handle_input_update_ms := float(input_transport.prof_handle_input_update_us_interval) / 1000.0
	var recalc_pred_ms := float(input_transport.prof_recalc_pred_us_interval) / 1000.0
	var adjust_time_scale_ms := float(input_transport.prof_adjust_time_scale_us_interval) / 1000.0
	var car_store_old_pos_ms := float(input_transport.prof_car_store_old_pos_us_interval) / 1000.0
	var car_post_render_ms := float(input_transport.prof_car_post_render_us_interval) / 1000.0
	input_transport.server_netcode_session.consume_authoritative_packet_stats()
	var auth_packet_builds: int = input_transport.server_netcode_session.get_authoritative_stat_packets()
	var auth_compression_candidates: int = input_transport.server_netcode_session.get_authoritative_stat_compression_candidates()
	var auth_build_ms := float(input_transport.server_netcode_session.get_authoritative_stat_build_usec()) / 1000.0
	var auth_packets: int = input_transport.log_auth_packets_sent
	var auth_frames: int = input_transport.server_netcode_session.get_authoritative_stat_frames()
	var auth_encoded_inputs: int = input_transport.server_netcode_session.get_authoritative_stat_encoded_inputs()
	var auth_unchanged_inputs: int = input_transport.server_netcode_session.get_authoritative_stat_unchanged_inputs()
	var auth_raw_bytes: int = input_transport.server_netcode_session.get_authoritative_stat_raw_bytes()
	var auth_payload_bytes: int = input_transport.server_netcode_session.get_authoritative_stat_payload_bytes()
	var auth_payload_per_packet := 0.0
	var auth_raw_per_packet := 0.0
	var auth_compression_ratio := 1.0
	if auth_packets > 0:
		auth_payload_per_packet = float(input_transport.log_flat_server_payload_out) / float(auth_packets)
	if auth_packet_builds > 0:
		auth_raw_per_packet = float(auth_raw_bytes) / float(auth_packet_builds)
	if auth_raw_bytes > 0:
		auth_compression_ratio = float(auth_payload_bytes) / float(auth_raw_bytes)
	var state_success: float = state_transfer.log_min_success_2pct if state_transfer.log_sent_count > 0 else 1.0
	var server_behind_ticks = maxi(input_transport.target_tick - input_transport.server_tick, 0) if is_server else 0
	var server_behind_avg := 0.0
	if input_transport.log_server_behind_ticks_samples > 0:
		server_behind_avg = float(input_transport.log_server_behind_ticks_sum) / float(input_transport.log_server_behind_ticks_samples)
	var delayed_peers: int = input_transport.delayed_peer_ids.size() if is_server else 0

	var logged_max_ahead: float = input_transport.max_ahead_from_server if is_server else input_transport.clients_max_ahead_from_server
	var client_unacked := _client_unacked_stats()
	var client_current_ahead : int = input_transport.local_tick - input_transport.clients_target_tick
	var client_target_ahead := _client_target_ahead_ticks()
	var client_ahead_error := client_target_ahead - float(client_current_ahead)
	var client_server_gap : int = input_transport.local_tick - input_transport.clients_server_tick
	var client_ack_lag: int = input_transport.local_tick - input_transport.last_ack_tick if input_transport.last_ack_tick >= 0 else -1
	var client_current_ahead_avg := 0.0
	var client_current_ahead_min := 0.0
	var client_current_ahead_max := 0.0
	var client_target_ahead_avg := 0.0
	var client_ahead_error_avg := 0.0
	var client_ahead_error_min := 0.0
	var client_ahead_error_max := 0.0
	if input_transport.log_client_ahead_samples > 0:
		client_current_ahead_avg = input_transport.log_client_current_ahead_sum / float(input_transport.log_client_ahead_samples)
		client_current_ahead_min = input_transport.log_client_current_ahead_min
		client_current_ahead_max = input_transport.log_client_current_ahead_max
		client_target_ahead_avg = input_transport.log_client_target_ahead_sum / float(input_transport.log_client_ahead_samples)
		client_ahead_error_avg = input_transport.log_client_ahead_error_sum / float(input_transport.log_client_ahead_samples)
		client_ahead_error_min = input_transport.log_client_ahead_error_min
		client_ahead_error_max = input_transport.log_client_ahead_error_max
	var timing_sync_rtt_avg := 0.0
	if input_transport.log_timing_sync_rtt_samples > 0:
		timing_sync_rtt_avg = input_transport.log_timing_sync_rtt_ms_sum / float(input_transport.log_timing_sync_rtt_samples)
	var peer_fields := _build_server_peer_log_fields()
	var state_pending: Dictionary = state_transfer.pending_log_fields()
	var line := str(Time.get_ticks_msec()) + "," + role + "," + str(multiplayer.get_unique_id()) + "," + str(is_server) + "," + str(listen_server) + "," + str(player_ids.size()) + "," + str(input_transport.server_tick) + "," + str(input_transport.target_tick) + "," + str(server_behind_ticks) + "," + str(server_behind_avg) + "," + str(input_transport.log_server_behind_ticks_max) + "," + str(delayed_peers) + "," + str(input_transport.local_tick) + "," + str(input_transport.clients_server_tick) + "," + str(input_transport.clients_target_tick) + "," + str(input_transport.rtt_s) + "," + str(input_transport.rtt_variance_s) + "," + str(input_transport.INPUT_FORWARD_REDUNDANCY_TICKS) + "," + str(input_transport.desired_ahead_ticks) + "," + str(logged_max_ahead) + "," + str(physics_tps) + "," + str(race_admission.server_start_msec) + "," + str(race_admission.local_start_msec) + "," + str(race_admission.actual_client_start_msec) + "," + str(race_admission.actual_server_start_msec) + "," + str(race_admission.first_authoritative_input_msec) + "," + str(race_admission.first_authoritative_first_tick) + "," + str(race_admission.first_authoritative_last_tick) + "," + str(race_admission.first_authoritative_count) + "," + str(up_kbps) + "," + str(down_kbps) + "," + str(bytes_out_total / 1000.0) + "," + str(bytes_in_total / 1000.0) + "," + str(input_transport.log_inputs_sent) + "," + str(input_transport.log_inputs_acked) + "," + str(input_transport.log_inputs_retransmitted) + "," + str(input_transport.log_flat_client_payload_out) + "," + str(input_transport.log_flat_client_payload_in) + "," + str(input_transport.log_flat_server_payload_out) + "," + str(input_transport.log_flat_server_payload_in) + "," + str(input_transport.log_server_late_drops) + "," + str(input_transport.log_server_replacements) + "," + str(state_transfer.log_raw_out) + "," + str(state_transfer.log_payload_out) + "," + str(state_transfer.log_sent_count) + "," + str(state_transfer.log_max_fragments_out) + "," + str(state_success) + "," + str(state_transfer.log_payload_in) + "," + str(state_transfer.log_raw_in) + "," + str(state_transfer.log_recv_count) + "," + str(state_transfer.log_max_recv_gap_ms) + "," + str(auth_packets) + "," + str(auth_packet_builds) + "," + str(auth_compression_candidates) + "," + str(auth_build_ms) + "," + str(auth_frames) + "," + str(auth_encoded_inputs) + "," + str(auth_unchanged_inputs) + "," + str(auth_payload_per_packet) + "," + str(auth_raw_per_packet) + "," + str(auth_compression_ratio) + "," + str(input_transport.AUTH_INPUT_REDUNDANCY_FRAMES) + "," + str(input_transport.AUTH_INPUT_ROLLBACK_WINDOW_TICKS) + "," + str(net_cpu_ms) + "," + str(sim_cpu_ms) + "," + str(rollback_avg_ms) + "," + str(rollback_max_ms) + "," + str(collect_inputs_ms) + "," + str(idle_broadcast_ms) + "," + str(check_client_stalls_ms) + "," + str(client_send_input_ms) + "," + str(server_broadcast_recv_ms) + "," + str(handle_state_ms) + "," + str(handle_input_update_ms) + "," + str(recalc_pred_ms) + "," + str(adjust_time_scale_ms) + "," + str(car_store_old_pos_ms) + "," + str(car_post_render_ms)
	line += "," + str(client_current_ahead) + "," + str(client_target_ahead) + "," + str(client_ahead_error) + "," + str(client_server_gap) + "," + str(client_unacked["count"]) + "," + str(client_unacked["oldest"]) + "," + str(client_unacked["newest"]) + "," + str(input_transport.last_ack_tick) + "," + str(client_ack_lag) + "," + str(input_transport.log_client_ahead_throttle_frames) + "," + str(input_transport.use_physics_ticks)
	line += "," + str(input_transport.log_client_sim_ticks) + "," + str(input_transport.log_client_target_tick_advances) + "," + str(input_transport.log_client_target_tick_remote_advances) + "," + str(input_transport.log_client_server_tick_advances) + "," + str(input_transport.log_client_ahead_samples) + "," + str(client_current_ahead_min) + "," + str(client_current_ahead_max) + "," + str(client_current_ahead_avg) + "," + str(client_target_ahead_avg) + "," + str(client_ahead_error_min) + "," + str(client_ahead_error_max) + "," + str(client_ahead_error_avg) + "," + str(input_transport.log_client_pre_auth_adjust_samples)
	line += "," + str(peer_fields["server_peer_lag_max"]) + "," + str(peer_fields["server_peer_lag_avg"]) + "," + str(peer_fields["target_peer_lag_max"]) + "," + str(peer_fields["target_peer_lag_avg"]) + "," + str(peer_fields["peer_ahead_min"]) + "," + str(peer_fields["peer_ahead_max"]) + "," + str(peer_fields["peer_ahead_avg"]) + "," + str(peer_fields["peer_rtt_max_ms"]) + "," + str(peer_fields["peer_rtt_avg_ms"]) + "," + str(peer_fields["peer_inputs_accepted"]) + "," + str(peer_fields["peer_inputs_dropped"]) + "," + str(peer_fields["peer_replacements"]) + "," + str(peer_fields["peer_input_server_lead_min"]) + "," + str(peer_fields["peer_input_server_lead_max"]) + "," + str(peer_fields["peer_input_server_lead_avg"]) + "," + str(peer_fields["peer_input_target_lead_min"]) + "," + str(peer_fields["peer_input_target_lead_max"]) + "," + str(peer_fields["peer_input_target_lead_avg"]) + "," + str(peer_fields["peer_snapshot"])
	line += "," + str(input_transport.log_timing_ping_out) + "," + str(input_transport.log_timing_ping_in) + "," + str(input_transport.log_timing_sync_out) + "," + str(input_transport.log_timing_sync_in) + "," + str(timing_sync_rtt_avg) + "," + str(input_transport.log_timing_sync_rtt_ms_max) + "," + str(input_transport.log_timing_server_gap_max) + "," + str(input_transport.log_timing_target_gap_max) + "," + str(input_transport.log_timing_ack_advance)
	line += "," + str(state_transfer.log_chunk_msgs_out) + "," + str(state_transfer.log_chunk_msgs_in) + "," + str(state_transfer.log_chunk_dups_in) + "," + str(state_transfer.log_chunk_stale_drops) + "," + str(state_transfer.log_chunk_bad_meta_drops) + "," + str(state_transfer.log_chunk_completed) + "," + str(state_transfer.log_chunk_completed_count_max) + "," + str(state_transfer.log_parity_chunks_out) + "," + str(state_transfer.log_fec_recovered_chunks) + "," + str(state_transfer.log_fec_abandoned) + "," + str(state_pending["records"]) + "," + str(state_pending["best_recv_pct"]) + "," + str(state_pending["best_missing"]) + "," + str(state_pending["oldest_tick"]) + "," + str(state_pending["newest_tick"])
	line += "," + str(state_transfer.log_sec_header) + "," + str(state_transfer.log_sec_bumper_meta) + "," + str(state_transfer.log_sec_sparks) + "," + str(state_transfer.log_sec_car_scalars) + "," + str(state_transfer.log_sec_car_vec3) + "," + str(state_transfer.log_sec_car_basis) + "," + str(state_transfer.log_sec_car_conditionals) + "," + str(state_transfer.log_sec_car_tilt) + "," + str(state_transfer.log_sec_car_wall) + "," + str(state_transfer.log_sec_bumper_total) + "," + str(state_transfer.log_sec_triggers) + "," + str(state_transfer.log_sec_total) + "," + str(state_transfer.log_stat_car_count) + "," + str(state_transfer.log_stat_bumper_count) + "," + str(state_transfer.log_stat_active_bumpers) + "," + str(state_transfer.log_stat_active_sparks) + "," + str(state_transfer.log_stat_trigger_count) + "," + str(state_transfer.log_stat_car_collision_old) + "," + str(state_transfer.log_stat_car_restore)
	var admission_fields: Dictionary = race_admission.log_fields()
	line += "," + str(admission_fields["ready"]) + "," + str(admission_fields["roster"]) + "," + str(admission_fields["blocked"]) + "," + str(admission_fields["snapshot"])
	var lobby_samples := maxi(log_lobby_frame_samples, 1)
	var rebuild_samples := maxi(log_lobby_render_rebuilds, 1)
	var stamp_log: Dictionary = custom_stamp_network.consume_log_interval()
	var lobby_fields := [
		log_lobby_frame_samples,
		float(log_lobby_frame_us) / float(lobby_samples) / 1000.0,
		float(log_lobby_frame_max_us) / 1000.0,
		float(log_lobby_player_list_us) / float(lobby_samples) / 1000.0,
		float(log_lobby_player_list_max_us) / 1000.0,
		float(log_lobby_chibi_us) / float(lobby_samples) / 1000.0,
		float(log_lobby_chibi_max_us) / 1000.0,
		log_lobby_render_rebuilds,
		float(log_lobby_render_rebuild_us) / float(rebuild_samples) / 1000.0,
		float(log_lobby_render_rebuild_max_us) / 1000.0,
		lobby_settings.log_messages_in,
		lobby_settings.log_messages_out,
		lobby_settings.log_bytes_in,
		lobby_settings.log_bytes_out,
		lobby_settings.log_accepted,
		lobby_settings.log_deduped,
		log_lobby_chibi_in,
		log_lobby_chibi_out,
		log_lobby_chibi_bytes_in,
		log_lobby_chibi_bytes_out,
		log_lobby_peer_connects,
		log_lobby_peer_disconnects,
		stamp_log["manifest_in"],
		stamp_log["manifest_out"],
		stamp_log["manifest_bytes_in"],
		stamp_log["manifest_bytes_out"],
		stamp_log["manifest_accepted"],
		stamp_log["manifest_deduped"],
		stamp_log["blob_in"],
		stamp_log["blob_out"],
		stamp_log["blob_bytes_in"],
		stamp_log["blob_bytes_out"],
		stamp_log["blob_accepted"],
		stamp_log["blob_deduped"],
		stamp_log["blob_queue_messages"],
		stamp_log["blob_queue_bytes"],
		Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0,
		Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0,
		int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)),
	]
	for value in lobby_fields:
		line += "," + str(value)
	_append_telemetry_sample(line)
	log_bytes_out_interval = 0
	log_bytes_in_interval = 0
	input_transport.log_bytes_out_interval = 0
	input_transport.log_bytes_in_interval = 0
	log_lobby_frame_samples = 0
	log_lobby_frame_us = 0
	log_lobby_frame_max_us = 0
	log_lobby_player_list_us = 0
	log_lobby_player_list_max_us = 0
	log_lobby_chibi_us = 0
	log_lobby_chibi_max_us = 0
	log_lobby_render_rebuilds = 0
	log_lobby_render_rebuild_us = 0
	log_lobby_render_rebuild_max_us = 0
	lobby_settings.reset_interval_counters()
	log_lobby_chibi_in = 0
	log_lobby_chibi_out = 0
	log_lobby_chibi_bytes_in = 0
	log_lobby_chibi_bytes_out = 0
	log_lobby_peer_connects = 0
	log_lobby_peer_disconnects = 0
	input_transport.log_net_cpu_us_interval = 0
	input_transport.log_rollback_us_sum = 0
	input_transport.log_rollback_us_count = 0
	input_transport.log_rollback_us_max = 0
	input_transport.log_inputs_retransmitted = 0
	input_transport.log_flat_client_payload_out = 0
	input_transport.log_flat_client_payload_in = 0
	input_transport.log_flat_server_payload_out = 0
	input_transport.log_flat_server_payload_in = 0
	input_transport.log_auth_packets_sent = 0
	input_transport.log_server_behind_ticks_sum = 0
	input_transport.log_server_behind_ticks_samples = 0
	input_transport.log_server_behind_ticks_max = 0
	state_transfer.reset_interval_counters()
	input_transport.log_sim_cpu_us_interval = 0
	input_transport.prof_collect_server_inputs_us_interval = 0
	input_transport.prof_idle_broadcast_us_interval = 0
	input_transport.prof_check_client_stalls_us_interval = 0
	input_transport.prof_client_send_input_us_interval = 0
	input_transport.prof_server_broadcast_recv_us_interval = 0
	input_transport.prof_handle_state_us_interval = 0
	input_transport.prof_handle_input_update_us_interval = 0
	input_transport.prof_recalc_pred_us_interval = 0
	input_transport.prof_adjust_time_scale_us_interval = 0
	input_transport.prof_car_store_old_pos_us_interval = 0
	input_transport.prof_car_post_render_us_interval = 0
	input_transport.log_peer_inputs_accepted.clear()
	input_transport.log_peer_inputs_dropped.clear()
	input_transport.log_peer_replacements.clear()
	input_transport.log_client_ahead_throttle_frames = 0
	input_transport.log_client_sim_ticks = 0
	input_transport.log_client_target_tick_advances = 0
	input_transport.log_client_target_tick_remote_advances = 0
	input_transport.log_client_server_tick_advances = 0
	input_transport.log_client_ahead_samples = 0
	input_transport.log_client_current_ahead_sum = 0.0
	input_transport.log_client_current_ahead_min = INF
	input_transport.log_client_current_ahead_max = -INF
	input_transport.log_client_target_ahead_sum = 0.0
	input_transport.log_client_ahead_error_sum = 0.0
	input_transport.log_client_ahead_error_min = INF
	input_transport.log_client_ahead_error_max = -INF
	input_transport.log_client_pre_auth_adjust_samples = 0
	input_transport.log_peer_last_packet_start.clear()
	input_transport.log_peer_last_packet_count.clear()
	input_transport.log_peer_last_packet_accept.clear()
	input_transport.log_peer_last_packet_drop.clear()
	input_transport.log_peer_last_packet_reject_before.clear()
	input_transport.log_peer_last_packet_last_tick.clear()
	input_transport.log_peer_last_packet_server_lead.clear()
	input_transport.log_peer_last_packet_target_lead.clear()
	input_transport.log_peer_packet_server_lead_sum = 0.0
	input_transport.log_peer_packet_server_lead_samples = 0
	input_transport.log_peer_packet_server_lead_min = INF
	input_transport.log_peer_packet_server_lead_max = -INF
	input_transport.log_peer_packet_target_lead_sum = 0.0
	input_transport.log_peer_packet_target_lead_samples = 0
	input_transport.log_peer_packet_target_lead_min = INF
	input_transport.log_peer_packet_target_lead_max = -INF
	input_transport.log_timing_ping_out = 0
	input_transport.log_timing_ping_in = 0
	input_transport.log_timing_sync_out = 0
	input_transport.log_timing_sync_in = 0
	input_transport.log_timing_sync_rtt_ms_sum = 0.0
	input_transport.log_timing_sync_rtt_ms_max = 0.0
	input_transport.log_timing_sync_rtt_samples = 0
	input_transport.log_timing_server_gap_max = 0
	input_transport.log_timing_target_gap_max = 0
	input_transport.log_timing_ack_advance = 0

func _acc_log_out(bytes: int) -> void:
	log_bytes_out_interval += bytes
	log_bytes_out_total += bytes

func _acc_log_in(bytes: int) -> void:
	log_bytes_in_interval += bytes
	log_bytes_in_total += bytes

func _parse_state_sample_dump_args() -> void:
	var args := OS.get_cmdline_args()
	args.append_array(OS.get_cmdline_user_args())
	for arg in args:
		if arg == "--mxt-dump-state-samples" or arg == "--mxt-dump-gamestate-samples":
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

func _apply_state_sample_dump_settings() -> void:
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
