class_name NetworkManager
extends Node

signal race_started(track_id, player_settings)
signal race_finished
signal race_options_changed(options)
const ProximityVoiceChatClass = preload("res://netplay/proximity_voice_chat.gd")
const GameVersionData = preload("res://core/game_version.gd")
const StateTransferControllerClass = preload("res://netplay/state_transfer_controller.gd")
const RaceResultsControllerClass = preload("res://netplay/race_results_controller.gd")
const LobbySettingsControllerClass = preload("res://netplay/lobby_settings_controller.gd")
const RaceAdmissionControllerClass = preload("res://netplay/race_admission_controller.gd")
const InputTransportControllerClass = preload("res://netplay/input_transport_controller.gd")
@onready var game_manager: GameManager = $".."
@onready var custom_stamp_network: CustomStampNetworkController = $CustomStampNetwork
@onready var state_transfer: StateTransferControllerClass = $StateTransferController
@onready var race_results: RaceResultsControllerClass = $RaceResultsController
@onready var lobby_settings: LobbySettingsControllerClass = $LobbySettingsController
@onready var race_admission: RaceAdmissionControllerClass = $RaceAdmissionController
@onready var input_transport: InputTransportControllerClass = $InputTransportController

var is_server: bool = false
var listen_server: bool = false
var network_active: bool = false
var player_ids: Array = []
var spectator_ids: Array = []
var waiting_peers: Array = []
var race_player_ids: Array = []
var _disconnected_during_race := {}
var spawn_seed: int = 0
var game_sim: GameSim
var server_game_sim: GameSim
var proximity_voice_chat: ProximityVoiceChat

func has_network_peer() -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if is_server:
		return true
	return multiplayer.multiplayer_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED

func _can_send_rpc_to_peer(peer_id: int) -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if listen_server and peer_id == multiplayer.get_unique_id():
		return true
	return multiplayer.get_peers().has(peer_id)
var race_options := {
	"game_mode": 0,
	"track_ids": [],
	"track_gameplay_digests": [],
	"track_package_digests": [],
	"track_workshop_ids": [],
	"vehicle_restore": true,
	"bumpers": false,
	"s_boost": true,
	"race_netplay_phase": 0,
	"grand_prix_current_track": 0,
	"grand_prix_points": {},
	"grand_prix_ko_energy_bonuses": {},
}
var race_netplay_phase := 0
var pending_next_race_track_id := ""
var pending_next_race_settings: Array = []
var pending_next_race_options: Dictionary = {}
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


var race_active: bool = false

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
var _log_timer: Timer
var version_string: String = GameVersionData.display_string()
var _unverified_peers: Array = []
var _version_request_time := {}

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
	var local_desired_ahead := input_transport._local_desired_ahead_for_shared()
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
		log_file.store_line("time,role,uid,is_server,listen,players,input_transport.server_tick,input_transport.target_tick,server_behind_ticks,server_behind_avg,server_behind_max,delayed_peers,input_transport.local_tick,input_transport.clients_server_tick,input_transport.clients_target_tick,rtt,rtt_variance,input_forward_redundancy,desired_ahead,server_max_ahead,physics_tps,start_server_ms,start_local_ms,actual_client_start_ms,actual_server_start_ms,first_auth_ms,first_auth_first_tick,first_auth_last_tick,first_auth_count,up_kbps,down_kbps,up_total_kb,down_total_kb,inputs_sent,inputs_acked,retrans,flat_client_out,flat_client_in,flat_server_out,flat_server_in,late_drops,replacements,state_raw_out,state_payload_out,state_sent,state_max_frags_out,state_min_success_2pct,state_payload_in,state_raw_in,state_recv,state_max_recv_gap_ms,auth_packets,auth_packet_builds,auth_compression_candidates,auth_build_ms,auth_frames,auth_encoded_inputs,auth_unchanged_inputs,auth_payload_per_packet,auth_raw_per_packet,auth_compression_ratio,auth_redundancy_frames,auth_rollback_window,net_cpu_ms,sim_cpu_ms,rollback_avg_ms,rollback_max_ms,collect_inputs_ms,idle_broadcast_ms,check_client_stalls_ms,client_send_input_ms,server_broadcast_recv_ms,handle_state_ms,handle_input_update_ms,recalc_pred_ms,adjust_time_scale_ms,car_store_old_pos_ms,car_post_render_ms,client_current_ahead,client_target_ahead,client_ahead_error,client_server_gap,client_sent_buffer,client_unacked_oldest,client_unacked_newest,client_last_ack_tick,client_ack_lag,client_throttle_frames,input_transport.use_physics_ticks,client_sim_ticks,client_target_tick_advances,client_target_tick_remote_advances,client_server_tick_advances,client_ahead_samples,client_current_ahead_min,client_current_ahead_max,client_current_ahead_avg,client_target_ahead_avg,client_ahead_error_min,client_ahead_error_max,client_ahead_error_avg,client_pre_auth_adjust_samples,server_peer_lag_max,server_peer_lag_avg,target_peer_lag_max,target_peer_lag_avg,peer_ahead_min,peer_ahead_max,peer_ahead_avg,peer_rtt_max_ms,peer_rtt_avg_ms,peer_inputs_accepted,peer_inputs_dropped,peer_replacements,peer_input_server_lead_min,peer_input_server_lead_max,peer_input_server_lead_avg,peer_input_target_lead_min,peer_input_target_lead_max,peer_input_target_lead_avg,peer_snapshot,timing_ping_out,timing_ping_in,timing_sync_out,timing_sync_in,timing_sync_rtt_ms_avg,timing_sync_rtt_ms_max,timing_sync_server_gap_max,timing_sync_target_gap_max,timing_ack_advance,state_chunk_out,state_chunk_in,state_chunk_dup_in,state_chunk_stale_drop,state_chunk_bad_meta_drop,state_chunk_complete,state_chunk_complete_max_chunks,state_parity_chunks_out,state_fec_recovered_chunks,state_fec_abandoned,state_pending_records,state_pending_best_recv_pct,state_pending_best_missing,state_pending_oldest_tick,state_pending_newest_tick,state_sec_header,state_sec_bumper_meta,state_sec_sparks,state_sec_car_scalars,state_sec_car_vec3,state_sec_car_basis,state_sec_car_conditionals,state_sec_car_tilt,state_sec_car_wall,state_sec_bumper_total,state_sec_triggers,state_sec_total,state_car_count,state_bumper_count,state_active_bumpers,state_active_sparks,state_trigger_count,state_car_collision_old,state_car_restore,admission_ready,admission_roster,admission_blocked,admission_snapshot,lobby_frame_samples,lobby_frame_avg_ms,lobby_frame_max_ms,lobby_player_list_avg_ms,lobby_player_list_max_ms,lobby_chibi_avg_ms,lobby_chibi_max_ms,lobby_render_rebuilds,lobby_render_rebuild_avg_ms,lobby_render_rebuild_max_ms,lobby_settings_in,lobby_settings_out,lobby_settings_bytes_in,lobby_settings_bytes_out,lobby_settings_accepted,lobby_settings_deduped,lobby_chibi_in,lobby_chibi_out,lobby_chibi_bytes_in,lobby_chibi_bytes_out,lobby_peer_connects,lobby_peer_disconnects,stamp_manifest_in,stamp_manifest_out,stamp_manifest_bytes_in,stamp_manifest_bytes_out,stamp_manifest_accepted,stamp_manifest_deduped,stamp_blob_in,stamp_blob_out,stamp_blob_bytes_in,stamp_blob_bytes_out,stamp_blob_accepted,stamp_blob_deduped,stamp_blob_queue_messages,stamp_blob_queue_bytes,engine_process_ms,engine_physics_ms,draw_calls")

func prepare_race_roster(reason: String) -> void:
	var changed := false
	_sync_lobby_settings_context()
	if is_server or !has_network_peer():
		changed = lobby_settings.ensure_cpu_ids_do_not_overlap_humans()
	if changed and is_server and !race_active:
		lobby_settings.broadcast_cpu_roster()

func reserve_next_race_netplay_options(options: Dictionary) -> Dictionary:
	var out := options.duplicate(true)
	if !is_server:
		return out
	out["race_netplay_phase"] = 1 - race_netplay_phase
	return out

func _race_phase_from_options(options: Dictionary) -> int:
	return int(options.get("race_netplay_phase", race_netplay_phase)) & 1

func _accept_race_start_phase(phase: int) -> bool:
	phase = phase & 1
	if race_active and phase != race_netplay_phase:
		return false
	race_netplay_phase = phase
	state_transfer.set_race_context(race_active, race_netplay_phase)
	race_results.set_context(race_active, race_netplay_phase, is_server, network_active)
	refresh_protocol_contexts()
	return true

func _accept_race_packet_phase(phase: int) -> bool:
	return (phase & 1) == race_netplay_phase

func _get_human_roster() -> Array:
	return race_player_ids.duplicate(true) if race_player_ids.size() > 0 else player_ids.duplicate(true)

func _get_race_ready_roster() -> Array:
	var roster := race_player_ids.duplicate(true) if race_player_ids.size() > 0 else player_ids.duplicate(true)
	if _disconnected_during_race.is_empty():
		return roster
	var out := []
	for id in roster:
		if !_disconnected_during_race.has(id):
			out.append(id)
	return out

func _get_active_human_roster() -> Array:
	var roster := _get_human_roster()
	if _disconnected_during_race.is_empty():
		return roster
	var out := []
	for id in roster:
		if !_disconnected_during_race.has(id):
			out.append(id)
	return out

func _human_racer_uses_native_cpu_input(id: int) -> bool:
	return race_results.player_finish_times.has(id) or race_results.player_dnfs.has(id) or _disconnected_during_race.has(id)

func get_simulation_roster() -> Array:
	var roster := _get_human_roster()
	roster.append_array(lobby_settings.get_cpu_roster())
	return roster

func _id_array_from_value(value) -> Array:
	var out := []
	if typeof(value) != TYPE_ARRAY:
		return out
	for raw_id in value:
		var id := int(raw_id)
		if !out.has(id):
			out.append(id)
	return out

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

func _flush_log() -> void:
	game_manager.record_memory_sample("interval")
	if !log_enabled or log_file == null or !has_network_peer():
		return
	var bytes_out_interval := log_bytes_out_interval + input_transport.log_bytes_out_interval
	var bytes_in_interval := log_bytes_in_interval + input_transport.log_bytes_in_interval
	var bytes_out_total := log_bytes_out_total + input_transport.log_bytes_out_total
	var bytes_in_total := log_bytes_in_total + input_transport.log_bytes_in_total
	var up_kbps := (bytes_out_interval * 8.0) / 1000.0
	var down_kbps := (bytes_in_interval * 8.0) / 1000.0
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
	var auth_stats: Dictionary = input_transport.server_netcode_session.consume_authoritative_packet_stats()
	var auth_packet_builds := int(auth_stats.get("auth_packets", 0))
	var auth_compression_candidates := int(auth_stats.get("auth_compression_candidates", 0))
	var auth_build_ms := float(auth_stats.get("auth_build_usec", 0)) / 1000.0
	var auth_packets := input_transport.log_auth_packets_sent
	var auth_frames := int(auth_stats.get("auth_frames", 0))
	var auth_encoded_inputs := int(auth_stats.get("auth_encoded_inputs", 0))
	var auth_unchanged_inputs := int(auth_stats.get("auth_unchanged_inputs", 0))
	var auth_raw_bytes := int(auth_stats.get("auth_raw_bytes", 0))
	var auth_payload_bytes := int(auth_stats.get("auth_payload_bytes", 0))
	var auth_payload_per_packet := 0.0
	var auth_raw_per_packet := 0.0
	var auth_compression_ratio := 1.0
	if auth_packets > 0:
		auth_payload_per_packet = float(input_transport.log_flat_server_payload_out) / float(auth_packets)
	if auth_packet_builds > 0:
		auth_raw_per_packet = float(auth_raw_bytes) / float(auth_packet_builds)
	if auth_raw_bytes > 0:
		auth_compression_ratio = float(auth_payload_bytes) / float(auth_raw_bytes)
	var state_success := state_transfer.log_min_success_2pct if state_transfer.log_sent_count > 0 else 1.0
	var server_behind_ticks = maxi(input_transport.target_tick - input_transport.server_tick, 0) if is_server else 0
	var server_behind_avg := 0.0
	if input_transport.log_server_behind_ticks_samples > 0:
		server_behind_avg = float(input_transport.log_server_behind_ticks_sum) / float(input_transport.log_server_behind_ticks_samples)
	var delayed_peers := input_transport.delayed_peer_ids.size() if is_server else 0

	var logged_max_ahead: float = input_transport.max_ahead_from_server if is_server else input_transport.clients_max_ahead_from_server
	var client_unacked := _client_unacked_stats()
	var client_current_ahead : int = input_transport.local_tick - input_transport.clients_target_tick
	var client_target_ahead := _client_target_ahead_ticks()
	var client_ahead_error := client_target_ahead - float(client_current_ahead)
	var client_server_gap : int = input_transport.local_tick - input_transport.clients_server_tick
	var client_ack_lag := input_transport.local_tick - input_transport.last_ack_tick if input_transport.last_ack_tick >= 0 else -1
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
	var state_pending := state_transfer.pending_log_fields()
	var line := str(Time.get_ticks_msec()) + "," + role + "," + str(multiplayer.get_unique_id()) + "," + str(is_server) + "," + str(listen_server) + "," + str(player_ids.size()) + "," + str(input_transport.server_tick) + "," + str(input_transport.target_tick) + "," + str(server_behind_ticks) + "," + str(server_behind_avg) + "," + str(input_transport.log_server_behind_ticks_max) + "," + str(delayed_peers) + "," + str(input_transport.local_tick) + "," + str(input_transport.clients_server_tick) + "," + str(input_transport.clients_target_tick) + "," + str(input_transport.rtt_s) + "," + str(input_transport.rtt_variance_s) + "," + str(input_transport.INPUT_FORWARD_REDUNDANCY_TICKS) + "," + str(input_transport.desired_ahead_ticks) + "," + str(logged_max_ahead) + "," + str(physics_tps) + "," + str(race_admission.server_start_msec) + "," + str(race_admission.local_start_msec) + "," + str(race_admission.actual_client_start_msec) + "," + str(race_admission.actual_server_start_msec) + "," + str(race_admission.first_authoritative_input_msec) + "," + str(race_admission.first_authoritative_first_tick) + "," + str(race_admission.first_authoritative_last_tick) + "," + str(race_admission.first_authoritative_count) + "," + str(up_kbps) + "," + str(down_kbps) + "," + str(bytes_out_total / 1000.0) + "," + str(bytes_in_total / 1000.0) + "," + str(input_transport.log_inputs_sent) + "," + str(input_transport.log_inputs_acked) + "," + str(input_transport.log_inputs_retransmitted) + "," + str(input_transport.log_flat_client_payload_out) + "," + str(input_transport.log_flat_client_payload_in) + "," + str(input_transport.log_flat_server_payload_out) + "," + str(input_transport.log_flat_server_payload_in) + "," + str(input_transport.log_server_late_drops) + "," + str(input_transport.log_server_replacements) + "," + str(state_transfer.log_raw_out) + "," + str(state_transfer.log_payload_out) + "," + str(state_transfer.log_sent_count) + "," + str(state_transfer.log_max_fragments_out) + "," + str(state_success) + "," + str(state_transfer.log_payload_in) + "," + str(state_transfer.log_raw_in) + "," + str(state_transfer.log_recv_count) + "," + str(state_transfer.log_max_recv_gap_ms) + "," + str(auth_packets) + "," + str(auth_packet_builds) + "," + str(auth_compression_candidates) + "," + str(auth_build_ms) + "," + str(auth_frames) + "," + str(auth_encoded_inputs) + "," + str(auth_unchanged_inputs) + "," + str(auth_payload_per_packet) + "," + str(auth_raw_per_packet) + "," + str(auth_compression_ratio) + "," + str(input_transport.AUTH_INPUT_REDUNDANCY_FRAMES) + "," + str(input_transport.AUTH_INPUT_ROLLBACK_WINDOW_TICKS) + "," + str(net_cpu_ms) + "," + str(sim_cpu_ms) + "," + str(rollback_avg_ms) + "," + str(rollback_max_ms) + "," + str(collect_inputs_ms) + "," + str(idle_broadcast_ms) + "," + str(check_client_stalls_ms) + "," + str(client_send_input_ms) + "," + str(server_broadcast_recv_ms) + "," + str(handle_state_ms) + "," + str(handle_input_update_ms) + "," + str(recalc_pred_ms) + "," + str(adjust_time_scale_ms) + "," + str(car_store_old_pos_ms) + "," + str(car_post_render_ms)
	line += "," + str(client_current_ahead) + "," + str(client_target_ahead) + "," + str(client_ahead_error) + "," + str(client_server_gap) + "," + str(client_unacked["count"]) + "," + str(client_unacked["oldest"]) + "," + str(client_unacked["newest"]) + "," + str(input_transport.last_ack_tick) + "," + str(client_ack_lag) + "," + str(input_transport.log_client_ahead_throttle_frames) + "," + str(input_transport.use_physics_ticks)
	line += "," + str(input_transport.log_client_sim_ticks) + "," + str(input_transport.log_client_target_tick_advances) + "," + str(input_transport.log_client_target_tick_remote_advances) + "," + str(input_transport.log_client_server_tick_advances) + "," + str(input_transport.log_client_ahead_samples) + "," + str(client_current_ahead_min) + "," + str(client_current_ahead_max) + "," + str(client_current_ahead_avg) + "," + str(client_target_ahead_avg) + "," + str(client_ahead_error_min) + "," + str(client_ahead_error_max) + "," + str(client_ahead_error_avg) + "," + str(input_transport.log_client_pre_auth_adjust_samples)
	line += "," + str(peer_fields["server_peer_lag_max"]) + "," + str(peer_fields["server_peer_lag_avg"]) + "," + str(peer_fields["target_peer_lag_max"]) + "," + str(peer_fields["target_peer_lag_avg"]) + "," + str(peer_fields["peer_ahead_min"]) + "," + str(peer_fields["peer_ahead_max"]) + "," + str(peer_fields["peer_ahead_avg"]) + "," + str(peer_fields["peer_rtt_max_ms"]) + "," + str(peer_fields["peer_rtt_avg_ms"]) + "," + str(peer_fields["peer_inputs_accepted"]) + "," + str(peer_fields["peer_inputs_dropped"]) + "," + str(peer_fields["peer_replacements"]) + "," + str(peer_fields["peer_input_server_lead_min"]) + "," + str(peer_fields["peer_input_server_lead_max"]) + "," + str(peer_fields["peer_input_server_lead_avg"]) + "," + str(peer_fields["peer_input_target_lead_min"]) + "," + str(peer_fields["peer_input_target_lead_max"]) + "," + str(peer_fields["peer_input_target_lead_avg"]) + "," + str(peer_fields["peer_snapshot"])
	line += "," + str(input_transport.log_timing_ping_out) + "," + str(input_transport.log_timing_ping_in) + "," + str(input_transport.log_timing_sync_out) + "," + str(input_transport.log_timing_sync_in) + "," + str(timing_sync_rtt_avg) + "," + str(input_transport.log_timing_sync_rtt_ms_max) + "," + str(input_transport.log_timing_server_gap_max) + "," + str(input_transport.log_timing_target_gap_max) + "," + str(input_transport.log_timing_ack_advance)
	line += "," + str(state_transfer.log_chunk_msgs_out) + "," + str(state_transfer.log_chunk_msgs_in) + "," + str(state_transfer.log_chunk_dups_in) + "," + str(state_transfer.log_chunk_stale_drops) + "," + str(state_transfer.log_chunk_bad_meta_drops) + "," + str(state_transfer.log_chunk_completed) + "," + str(state_transfer.log_chunk_completed_count_max) + "," + str(state_transfer.log_parity_chunks_out) + "," + str(state_transfer.log_fec_recovered_chunks) + "," + str(state_transfer.log_fec_abandoned) + "," + str(state_pending["records"]) + "," + str(state_pending["best_recv_pct"]) + "," + str(state_pending["best_missing"]) + "," + str(state_pending["oldest_tick"]) + "," + str(state_pending["newest_tick"])
	line += "," + str(state_transfer.log_sec_header) + "," + str(state_transfer.log_sec_bumper_meta) + "," + str(state_transfer.log_sec_sparks) + "," + str(state_transfer.log_sec_car_scalars) + "," + str(state_transfer.log_sec_car_vec3) + "," + str(state_transfer.log_sec_car_basis) + "," + str(state_transfer.log_sec_car_conditionals) + "," + str(state_transfer.log_sec_car_tilt) + "," + str(state_transfer.log_sec_car_wall) + "," + str(state_transfer.log_sec_bumper_total) + "," + str(state_transfer.log_sec_triggers) + "," + str(state_transfer.log_sec_total) + "," + str(state_transfer.log_stat_car_count) + "," + str(state_transfer.log_stat_bumper_count) + "," + str(state_transfer.log_stat_active_bumpers) + "," + str(state_transfer.log_stat_active_sparks) + "," + str(state_transfer.log_stat_trigger_count) + "," + str(state_transfer.log_stat_car_collision_old) + "," + str(state_transfer.log_stat_car_restore)
	var admission_fields := race_admission.log_fields()
	line += "," + str(admission_fields["ready"]) + "," + str(admission_fields["roster"]) + "," + str(admission_fields["blocked"]) + "," + str(admission_fields["snapshot"])
	var lobby_samples := maxi(log_lobby_frame_samples, 1)
	var rebuild_samples := maxi(log_lobby_render_rebuilds, 1)
	var stamp_log := custom_stamp_network.consume_log_interval()
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
	log_file.store_line(line)
	log_file.flush()
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

func reset_race_state(preserve_player_settings: bool = false) -> void:
	var preserved_player_settings := {}
	if preserve_player_settings:
		var preserve_ids := []
		preserve_ids.append_array(player_ids)
		preserve_ids.append_array(spectator_ids)
		preserve_ids.append_array(waiting_peers)
		for id in preserve_ids:
			if lobby_settings.player_settings.has(id):
				preserved_player_settings[id] = lobby_settings.player_settings[id]
	race_active = false
	state_transfer.set_race_context(false, race_netplay_phase)
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
	race_player_ids.clear()
	lobby_settings.clear_race_cpu_roster()
	_disconnected_during_race.clear()
	race_results.reset()
	pending_next_race_track_id = ""
	pending_next_race_settings.clear()
	pending_next_race_options.clear()
	lobby_settings.reset_latency()
	state_transfer.reset()
	race_admission.reset()
	lobby_settings.reset_settings(preserved_player_settings)
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	input_transport.reset()

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
	input_transport.netcode_session.configure_authoritative_input_sample_dump(
		dump_auth_input_samples,
		auth_input_sample_limit,
		auth_input_sample_dir
	)
	input_transport.server_netcode_session.configure_authoritative_input_sample_dump(
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

func _on_player_dnf_recorded(player_id: int) -> void:
	_disconnected_during_race[player_id] = true
	input_transport.record_player_dnf(player_id)
	refresh_protocol_contexts()

func _sync_lobby_settings_context() -> void:
	lobby_settings.set_context(is_server, race_active, network_active, player_ids, spectator_ids)

func refresh_protocol_contexts() -> void:
	input_transport.set_context(
		is_server,
		listen_server,
		network_active,
		race_active,
		race_netplay_phase,
		player_ids,
		spectator_ids,
		race_player_ids,
		_get_race_ready_roster(),
		_get_active_human_roster(),
		get_simulation_roster(),
		_disconnected_during_race,
		game_sim,
		server_game_sim)
	race_admission.set_context(
		is_server,
		listen_server,
		network_active,
		race_active,
		race_netplay_phase,
		player_ids,
		_get_race_ready_roster(),
		input_transport.rtt_s,
		input_transport.desired_ahead_ticks,
		game_sim,
		server_game_sim)

func _on_lobby_cpu_removed(player_id: int) -> void:
	input_transport.remove_cpu(player_id)

func _on_lobby_latency_sample_received(peer_id: int, sample_rtt_s: float) -> void:
	if is_server:
		input_transport.peer_client_rtt_s[peer_id] = sample_rtt_s
	else:
		input_transport._record_rtt_sample(sample_rtt_s)
		lobby_settings.set_local_latency(input_transport.rtt_s)

func _on_admission_local_rtt_sample_received(sample_rtt_s: float) -> void:
	input_transport._record_rtt_sample(sample_rtt_s)
	race_admission.set_local_timing(input_transport.rtt_s, input_transport.desired_ahead_ticks)

func _on_admission_peer_timing_sample_received(peer_id: int, peer_rtt_s: float, ahead_ticks: float) -> void:
	input_transport.set_peer_timing(peer_id, peer_rtt_s, ahead_ticks)

func _on_admission_disconnect_peer_requested(peer_id: int) -> void:
	if multiplayer.multiplayer_peer != null and multiplayer.get_peers().has(peer_id):
		multiplayer.disconnect_peer(peer_id)
		_on_peer_disconnected(peer_id)
		refresh_protocol_contexts()

func _on_admission_start_schedule_received(initial_max_ahead: float) -> void:
	input_transport.apply_start_schedule(initial_max_ahead)

func _on_admission_client_simulation_start_requested(initial_target_tick: int) -> void:
	input_transport.start_client_simulation(initial_target_tick)

func _on_admission_authoritative_simulation_start_requested() -> void:
	input_transport.start_authoritative_simulation()

func _on_lobby_player_role_changed(player_id: int, spectator: bool) -> void:
	if player_id == multiplayer.get_unique_id():
		input_transport.desired_ahead_ticks = 1.0 if spectator else (0.0 if is_server else 2.0)
	if !is_server:
		return
	var roster_changed := false
	if spectator:
		if player_ids.has(player_id):
			player_ids.erase(player_id)
			spectator_ids.append(player_id)
			roster_changed = true
	else:
		if spectator_ids.has(player_id):
			spectator_ids.erase(player_id)
		if !player_ids.has(player_id):
			player_ids.append(player_id)
			roster_changed = true
	if !roster_changed:
		return
	_sync_lobby_settings_context()
	var cpu_ids_changed := lobby_settings.ensure_cpu_ids_do_not_overlap_humans()
	refresh_protocol_contexts()
	if !race_active:
		_update_player_ids.rpc(player_ids)
		if cpu_ids_changed:
			lobby_settings.broadcast_cpu_roster()
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)

func _ready() -> void:
	lobby_settings.initialize(game_manager)
	lobby_settings.player_role_changed.connect(_on_lobby_player_role_changed)
	lobby_settings.cpu_removed.connect(_on_lobby_cpu_removed)
	lobby_settings.latency_sample_received.connect(_on_lobby_latency_sample_received)
	_sync_lobby_settings_context()
	race_admission.initialize(lobby_settings)
	race_admission.disconnect_peer_requested.connect(_on_admission_disconnect_peer_requested)
	race_admission.local_rtt_sample_received.connect(_on_admission_local_rtt_sample_received)
	race_admission.peer_timing_sample_received.connect(_on_admission_peer_timing_sample_received)
	race_admission.start_schedule_received.connect(_on_admission_start_schedule_received)
	race_admission.client_simulation_start_requested.connect(_on_admission_client_simulation_start_requested)
	race_admission.authoritative_simulation_start_requested.connect(_on_admission_authoritative_simulation_start_requested)
	input_transport.initialize(lobby_settings, state_transfer, race_results, race_admission)
	input_transport.disconnect_peer_requested.connect(_on_admission_disconnect_peer_requested)
	refresh_protocol_contexts()
	state_transfer.initialize(input_transport.server_netcode_session)
	state_transfer.state_received.connect(input_transport._handle_state)
	state_transfer.state_sample_generated.connect(dump_state_sample)
	state_transfer.wire_bytes_sent.connect(_acc_log_out)
	state_transfer.wire_bytes_received.connect(_acc_log_in)
	race_results.player_dnf_recorded.connect(_on_player_dnf_recorded)
	proximity_voice_chat = get_node_or_null("ProximityVoiceChat") as ProximityVoiceChat
	if proximity_voice_chat == null:
		proximity_voice_chat = ProximityVoiceChatClass.new()
		proximity_voice_chat.name = "ProximityVoiceChat"
		add_child(proximity_voice_chat)
	_parse_auth_input_sample_dump_args()
	_apply_auth_input_sample_dump_settings()
	var server_process_timer = Timer.new()
	server_process_timer.ignore_time_scale = true
	add_child(server_process_timer)
	server_process_timer.timeout.connect(input_transport.process)
	server_process_timer.start(1.0 / 60.0)
	multiplayer.server_disconnected.connect(on_disconnect)
	_log_timer = Timer.new()
	_log_timer.wait_time = 1.0
	_log_timer.one_shot = false
	_log_timer.timeout.connect(_flush_log)
	add_child(_log_timer)
	_log_timer.start()

func on_disconnect() -> void:
	DebugDraw2D.set_text("DISCONNECTED!", null, 10, Color.RED, 10)
	disconnect_from_server()

func host(port: int = 27016, max_players: int = 64, dedicated: bool = false) -> int:
	disconnect_from_server()
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_players)
	if err != OK:
		push_error("Failed to host: %s" % err)
		return err
	multiplayer.multiplayer_peer = peer
	is_server = true
	network_active = true
	listen_server = !dedicated
	player_ids = [multiplayer.get_unique_id()]
	_sync_lobby_settings_context()
	lobby_settings.ensure_cpu_ids_do_not_overlap_humans()
	lobby_settings.reset_settings()
	lobby_settings.clear_race_cpu_roster()
	custom_stamp_network.clear()
	state_transfer.reset()
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	race_admission.reset()
	refresh_protocol_contexts()
	input_transport.reset()
	get_window().title = "Host"
	if !multiplayer.peer_connected.is_connected(_on_peer_connected):
		multiplayer.peer_connected.connect(_on_peer_connected)
	if !multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
	lobby_settings.broadcast_cpu_roster()
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
	multiplayer.multiplayer_peer = peer
	is_server = false
	network_active = true
	listen_server = false
	player_ids = [multiplayer.get_unique_id()]
	_sync_lobby_settings_context()
	lobby_settings.reset_all()
	custom_stamp_network.clear()
	state_transfer.reset()
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	race_admission.reset()
	refresh_protocol_contexts()
	input_transport.reset()
	get_window().title = "Client " + str(multiplayer.get_unique_id())
	if log_file == null:
		_init_logger()
	return OK

func _on_peer_connected(id: int) -> void:
	log_lobby_peer_connects += 1
	if is_server:
		if !_unverified_peers.has(id):
			_unverified_peers.append(id)
		_version_request_time[id] = 0.001 * float(Time.get_ticks_msec())
		_request_client_version.rpc_id(id, version_string)
func _on_peer_disconnected(id: int) -> void:
	log_lobby_peer_disconnects += 1
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
		_sync_lobby_settings_context()
		refresh_protocol_contexts()
		input_transport.remove_peer(id)
		lobby_settings.remove_player(id)
		custom_stamp_network.remove_peer(id)
		race_admission.remove_peer(id)
		state_transfer.remove_peer(id)
		if !race_active:
			_update_player_ids.rpc(player_ids)
		state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
		if race_active:
			race_admission.evaluate()

func kick_human_player(id: int) -> void:
	if !is_server or race_active:
		return
	if id == multiplayer.get_unique_id() or lobby_settings.cpu_player_ids.has(id):
		return
	if !player_ids.has(id) and !spectator_ids.has(id) and !waiting_peers.has(id):
		return
	multiplayer.disconnect_peer(id)
	_on_peer_disconnected(id)
	_update_player_ids.rpc(player_ids)
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)


func _accept_peer(id: int) -> void:
	if _unverified_peers.has(id):
		_unverified_peers.erase(id)
	if _version_request_time.has(id):
		_version_request_time.erase(id)
	if race_active or (server_game_sim != null and server_game_sim.sim_started):
		if !waiting_peers.has(id):
			waiting_peers.append(id)
		_update_player_ids.rpc_id(id, player_ids)
		lobby_settings.send_cpu_roster_to_peer(id)
		sync_race_options.rpc_id(id, race_options)
		return
	if !player_ids.has(id):
		player_ids.append(id)
	_sync_lobby_settings_context()
	var cpu_ids_changed := lobby_settings.ensure_cpu_ids_do_not_overlap_humans()
	refresh_protocol_contexts()
	input_transport.last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
	input_transport.peer_desired_ahead[id] = 0.0
	input_transport.server_netcode_session.set_peer_last_received(id, -1, input_transport.last_input_time[id])
	input_transport.server_netcode_session.set_peer_desired_ahead(id, 0.0)
	if !race_active:
		_update_player_ids.rpc(player_ids)
		if cpu_ids_changed:
			lobby_settings.broadcast_cpu_roster()
		lobby_settings.send_cpu_roster_to_peer(id)
		sync_race_options.rpc_id(id, race_options)
	lobby_settings.send_player_settings_snapshot_to_peer(id)
	custom_stamp_network.send_manifests_to_peer(id)
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)

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
func flush_waiting_peers(force_spectator: bool = false) -> void:
	if not is_server:
		return
	var new_ids: Array = []
	for id in waiting_peers:
		var settings = lobby_settings.player_settings.get(id, {})
		if typeof(settings) != TYPE_DICTIONARY:
			settings = {}
		settings = (settings as Dictionary).duplicate(true)
		if force_spectator:
			settings["spectator"] = true
			if !settings.has("username"):
				settings["username"] = str(id)
			lobby_settings.player_settings[id] = settings
		var spec = force_spectator or settings.get("spectator", false)
		if spec:
			if player_ids.has(id):
				player_ids.erase(id)
			if not spectator_ids.has(id):
				spectator_ids.append(id)
				new_ids.append(id)
		elif not player_ids.has(id):
			player_ids.append(id)
			new_ids.append(id)
		input_transport.last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
		input_transport.peer_desired_ahead[id] = 0.0
		input_transport.server_netcode_session.set_peer_last_received(id, -1, input_transport.last_input_time[id])
		input_transport.server_netcode_session.set_peer_desired_ahead(id, 0.0)
		if !race_active:
			lobby_settings.send_player_settings_snapshot_to_peer(id)
			custom_stamp_network.send_manifests_to_peer(id)
	waiting_peers.clear()
	_sync_lobby_settings_context()
	_update_player_ids.rpc(player_ids)
	refresh_protocol_contexts()
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
	for id in new_ids:
		lobby_settings.send_cpu_roster_to_peer(id)
		if !race_active and lobby_settings.player_settings.has(id):
			lobby_settings.send_player_settings_to_all(lobby_settings.player_settings[id], id)

func broadcast_lobby_roster() -> void:
	if !is_server:
		return
	_update_player_ids.rpc(player_ids)
	lobby_settings.broadcast_cpu_roster()

@rpc("authority", "call_remote", "reliable", 7)
func _update_player_ids(ids: Array) -> void:
	player_ids = ids
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	if is_server:
		state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)

@rpc("authority", "call_remote", "reliable", 7)
func start_race(track_id: String, settings: Array, options: Dictionary = {}) -> void:
	prepare_race_roster("start_race")
	var incoming_phase := _race_phase_from_options(options)
	if !_accept_race_start_phase(incoming_phase):
		return
	race_admission.reset()
	if !options.is_empty():
		race_options = options.duplicate(true)
	if race_options.has("spawn_seed"):
		set_spawn_seed(int(race_options.get("spawn_seed", spawn_seed)))
	race_active = true
	state_transfer.set_race_context(true, race_netplay_phase)
	race_results.set_context(true, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
	if proximity_voice_chat != null:
		proximity_voice_chat.reset()
	if race_options.has("race_human_ids"):
		race_player_ids = _id_array_from_value(race_options.get("race_human_ids", []))
	else:
		race_player_ids = player_ids.duplicate(true)
	if race_options.has("race_cpu_ids"):
		lobby_settings.set_race_cpu_roster(_id_array_from_value(race_options.get("race_cpu_ids", [])))
	else:
		lobby_settings.set_race_cpu_roster(lobby_settings.cpu_player_ids)
	if race_options.has("race_spectator_ids"):
		spectator_ids = _id_array_from_value(race_options.get("race_spectator_ids", []))
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	if is_server:
		race_admission.initialize_states()
		state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
	emit_signal("race_started", track_id, settings)
	if is_server:
		var now := 0.001 * float(Time.get_ticks_msec())
		for id in race_player_ids + spectator_ids:
			input_transport.last_input_time[id] = now
			input_transport.server_netcode_session.set_peer_last_received(id, -1, now)

func send_start_race(track_id: String, settings: Array, options: Dictionary = {}) -> void:
	if !is_server:
		return
	if options.is_empty():
		options = reserve_next_race_netplay_options(race_options)
	else:
		options = reserve_next_race_netplay_options(options)
	race_options = options.duplicate(true)
	# Generate and distribute a shared spawn seed before starting the race.
	# This lets all peers randomize starting grid slots deterministically.
	options["spawn_seed"] = randi()
	race_options = options.duplicate(true)
	start_race.rpc(track_id, settings, options)
	start_race(track_id, settings, options)

@rpc("authority", "call_remote", "reliable", 7)
func end_race(phase: int, next_track_id: String = "", next_settings: Array = [], next_options: Dictionary = {}) -> void:
	if !_accept_race_packet_phase(phase):
		return
	pending_next_race_track_id = next_track_id
	pending_next_race_settings = next_settings.duplicate(true)
	pending_next_race_options = next_options.duplicate(true)
	if !pending_next_race_options.is_empty():
		race_options = pending_next_race_options.duplicate(true)
	race_active = false
	state_transfer.set_race_context(false, race_netplay_phase)
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	if proximity_voice_chat != null:
		proximity_voice_chat.reset()
	emit_signal("race_finished")

func send_end_race(next_track_id: String = "", next_settings: Array = [], next_options: Dictionary = {}) -> void:
	if is_server:
		end_race.rpc(race_netplay_phase, next_track_id, next_settings, next_options)
		end_race(race_netplay_phase, next_track_id, next_settings, next_options)

@rpc("authority", "call_remote", "reliable", 7)
func set_spawn_seed(seed: int) -> void:
	spawn_seed = seed
	if game_sim != null:
		game_sim.set_spawn_seed(seed)
	if is_server and server_game_sim != null:
		server_game_sim.set_spawn_seed(seed)

func disconnect_from_server() -> void:
	race_active = false
	state_transfer.set_race_context(false, race_netplay_phase)
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
	if proximity_voice_chat != null:
		proximity_voice_chat.reset()
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
	lobby_settings.reset_all()
	pending_next_race_track_id = ""
	pending_next_race_settings.clear()
	pending_next_race_options.clear()
	_disconnected_during_race.clear()
	custom_stamp_network.clear()
	_unverified_peers.clear()
	_version_request_time.clear()
	state_transfer.reset()
	race_results.reset()
	race_admission.reset()
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	input_transport.reset()

func force_end_countdown_seconds_for(player_id: int) -> int:
	if race_results.race_force_end_deadline_tick < 0:
		return -1
	if race_results.player_finish_times.has(player_id) or _disconnected_during_race.has(player_id) or race_results.player_eliminations.has(player_id):
		return -1
	var human_roster := _get_human_roster()
	var human_count := human_roster.size()
	if human_count <= 0:
		return -1
	var finished_count := 0
	for id_value in human_roster:
		if race_results.player_finish_times.has(int(id_value)):
			finished_count += 1
	if finished_count * 2 <= human_count:
		return -1
	var remaining_ticks := maxi(0, race_results.race_force_end_deadline_tick - input_transport.get_race_tick())
	return ceili(float(remaining_ticks) / 60.0)

func is_vehicle_restore_enabled() -> bool:
	return bool(race_options.get("vehicle_restore", true))

func is_s_boost_enabled() -> bool:
	return bool(race_options.get("s_boost", true))

func is_grand_prix_enabled() -> bool:
	return int(race_options.get("game_mode", 0)) == 1

@rpc("authority", "call_local", "reliable")
func sync_race_options(options: Dictionary) -> void:
	if race_active and options.has("race_netplay_phase") and !_accept_race_packet_phase(int(options.get("race_netplay_phase", race_netplay_phase))):
		return
	race_options = options.duplicate(true)
	race_options_changed.emit(race_options.duplicate(true))

func send_race_options(options: Dictionary) -> void:
	if is_server:
		sync_race_options.rpc(options)
		sync_race_options(options)
