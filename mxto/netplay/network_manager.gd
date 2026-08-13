class_name NetworkManager
extends Node

signal race_started(track_id, player_settings)
signal race_finished
signal race_options_changed(options)
signal authoritative_server_frame(tick, frame_inputs)

const PlayerInputClass = preload("res://player/player_input.gd")
const ProximityVoiceChatClass = preload("res://netplay/proximity_voice_chat.gd")
const GameVersionData = preload("res://core/game_version.gd")
const StateTransferControllerClass = preload("res://netplay/state_transfer_controller.gd")
const RaceResultsControllerClass = preload("res://netplay/race_results_controller.gd")
const LobbySettingsControllerClass = preload("res://netplay/lobby_settings_controller.gd")
const RaceAdmissionControllerClass = preload("res://netplay/race_admission_controller.gd")
var NEUTRAL_INPUT_BYTES : PackedByteArray = PlayerInputClass.new().serialize()

@onready var game_manager: GameManager = $".."
@onready var custom_stamp_network: CustomStampNetworkController = $CustomStampNetwork
@onready var state_transfer: StateTransferControllerClass = $StateTransferController
@onready var race_results: RaceResultsControllerClass = $RaceResultsController
@onready var lobby_settings: LobbySettingsControllerClass = $LobbySettingsController
@onready var race_admission: RaceAdmissionControllerClass = $RaceAdmissionController

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
var proximity_voice_chat: ProximityVoiceChat
var last_received_tick := {}

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
var last_ack_tick: int = -1
var target_tick: int = 0
const MAX_AHEAD_TICKS := 30
const MAX_HISTORY_TICKS := 60
const INPUT_FORWARD_REDUNDANCY_TICKS := 12
const STARTUP_LIGHT_NET_TICKS := 120
const SERVER_INPUT_REPLACEMENT_BACKLOG_TICKS := 5
const AUTH_INPUT_REDUNDANCY_FRAMES := 2
const AUTH_INPUT_ROLLBACK_WINDOW_TICKS := 20
var sent_input_times := {}
var rtt_s: float = 0.0
var rtt_variance_s: float = 0.0
var desired_ahead_ticks: float = 2.0
var base_wait_time: float = 1.0 / 60.0
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
const AUTH_INPUT_MODE_DELTA_LOW_ENTROPY_DICT := 2
const AUTH_INPUT_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT := 4
const AUTH_INPUT_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT := 7
const AUTH_INPUT_META_SHIFT := 32
const AUTH_INPUT_META_BYTE_MASK := 0xff
const AUTH_INPUT_META_PRESENT_BIT := 1 << 40
const SERVER_TIMING_SYNC_INTERVAL_TICKS := 1
const CLIENT_TIMING_PING_INTERVAL_MS := 250
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
var max_ahead_from_server: float = 0.0
var peer_desired_ahead := {}
var delayed_peer_ids := {}
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


var clients_server_tick := 0
var clients_target_tick := 0
var last_target_tick_update := 0
var last_client_timing_ping_msec := 0
var clients_max_ahead_from_server := 2.0
var authoritative_history := {}
var last_server_input_tick := -1
var race_active: bool = false

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
var _log_timer: Timer
var rollback_frametime_us := 0
var net_input_debug_prints := 0

func _startup_light_net_active(tick: int) -> bool:
	return tick >= 0 and tick < STARTUP_LIGHT_NET_TICKS

func _store_neutral_authoritative_frame_for_all_racers(tick: int) -> void:
	for id in get_simulation_roster():
		netcode_session.store_authoritative_input(tick, int(id), NEUTRAL_INPUT_BYTES)

var version_string: String = GameVersionData.display_string()
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
		log_file.store_line("time,role,uid,is_server,listen,players,server_tick,target_tick,server_behind_ticks,server_behind_avg,server_behind_max,delayed_peers,local_tick,clients_server_tick,clients_target_tick,rtt,rtt_variance,input_forward_redundancy,desired_ahead,server_max_ahead,physics_tps,start_server_ms,start_local_ms,actual_client_start_ms,actual_server_start_ms,first_auth_ms,first_auth_first_tick,first_auth_last_tick,first_auth_count,up_kbps,down_kbps,up_total_kb,down_total_kb,inputs_sent,inputs_acked,retrans,flat_client_out,flat_client_in,flat_server_out,flat_server_in,late_drops,replacements,state_raw_out,state_payload_out,state_sent,state_max_frags_out,state_min_success_2pct,state_payload_in,state_raw_in,state_recv,state_max_recv_gap_ms,auth_packets,auth_packet_builds,auth_compression_candidates,auth_build_ms,auth_frames,auth_encoded_inputs,auth_unchanged_inputs,auth_payload_per_packet,auth_raw_per_packet,auth_compression_ratio,auth_redundancy_frames,auth_rollback_window,net_cpu_ms,sim_cpu_ms,rollback_avg_ms,rollback_max_ms,collect_inputs_ms,idle_broadcast_ms,check_client_stalls_ms,client_send_input_ms,server_broadcast_recv_ms,handle_state_ms,handle_input_update_ms,recalc_pred_ms,adjust_time_scale_ms,car_store_old_pos_ms,car_post_render_ms,client_current_ahead,client_target_ahead,client_ahead_error,client_server_gap,client_sent_buffer,client_unacked_oldest,client_unacked_newest,client_last_ack_tick,client_ack_lag,client_throttle_frames,use_physics_ticks,client_sim_ticks,client_target_tick_advances,client_target_tick_remote_advances,client_server_tick_advances,client_ahead_samples,client_current_ahead_min,client_current_ahead_max,client_current_ahead_avg,client_target_ahead_avg,client_ahead_error_min,client_ahead_error_max,client_ahead_error_avg,client_pre_auth_adjust_samples,server_peer_lag_max,server_peer_lag_avg,target_peer_lag_max,target_peer_lag_avg,peer_ahead_min,peer_ahead_max,peer_ahead_avg,peer_rtt_max_ms,peer_rtt_avg_ms,peer_inputs_accepted,peer_inputs_dropped,peer_replacements,peer_input_server_lead_min,peer_input_server_lead_max,peer_input_server_lead_avg,peer_input_target_lead_min,peer_input_target_lead_max,peer_input_target_lead_avg,peer_snapshot,timing_ping_out,timing_ping_in,timing_sync_out,timing_sync_in,timing_sync_rtt_ms_avg,timing_sync_rtt_ms_max,timing_sync_server_gap_max,timing_sync_target_gap_max,timing_ack_advance,state_chunk_out,state_chunk_in,state_chunk_dup_in,state_chunk_stale_drop,state_chunk_bad_meta_drop,state_chunk_complete,state_chunk_complete_max_chunks,state_parity_chunks_out,state_fec_recovered_chunks,state_fec_abandoned,state_pending_records,state_pending_best_recv_pct,state_pending_best_missing,state_pending_oldest_tick,state_pending_newest_tick,state_sec_header,state_sec_bumper_meta,state_sec_sparks,state_sec_car_scalars,state_sec_car_vec3,state_sec_car_basis,state_sec_car_conditionals,state_sec_car_tilt,state_sec_car_wall,state_sec_bumper_total,state_sec_triggers,state_sec_total,state_car_count,state_bumper_count,state_active_bumpers,state_active_sparks,state_trigger_count,state_car_collision_old,state_car_restore,admission_ready,admission_roster,admission_blocked,admission_snapshot,lobby_frame_samples,lobby_frame_avg_ms,lobby_frame_max_ms,lobby_player_list_avg_ms,lobby_player_list_max_ms,lobby_chibi_avg_ms,lobby_chibi_max_ms,lobby_render_rebuilds,lobby_render_rebuild_avg_ms,lobby_render_rebuild_max_ms,lobby_settings_in,lobby_settings_out,lobby_settings_bytes_in,lobby_settings_bytes_out,lobby_settings_accepted,lobby_settings_deduped,lobby_chibi_in,lobby_chibi_out,lobby_chibi_bytes_in,lobby_chibi_bytes_out,lobby_peer_connects,lobby_peer_disconnects,stamp_manifest_in,stamp_manifest_out,stamp_manifest_bytes_in,stamp_manifest_bytes_out,stamp_manifest_accepted,stamp_manifest_deduped,stamp_blob_in,stamp_blob_out,stamp_blob_bytes_in,stamp_blob_bytes_out,stamp_blob_accepted,stamp_blob_deduped,stamp_blob_queue_messages,stamp_blob_queue_bytes,engine_process_ms,engine_physics_ms,draw_calls")

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
	refresh_race_admission_context()
	return true

func _accept_race_packet_phase(phase: int) -> bool:
	return (phase & 1) == race_netplay_phase

func _pack_race_phase_tick(tick: int) -> int:
	return (tick & RACE_PHASE_TICK_MASK) | (race_netplay_phase << 31)

func _pack_authoritative_input_tick(tick: int, input_meta: int) -> int:
	var packed_tick := _pack_race_phase_tick(tick)
	if input_meta >= 0:
		packed_tick |= AUTH_INPUT_META_PRESENT_BIT | ((input_meta & AUTH_INPUT_META_BYTE_MASK) << AUTH_INPUT_META_SHIFT)
	return packed_tick

func _authoritative_input_meta_can_strip(input_meta: int) -> bool:
	var mode := input_meta & AUTH_INPUT_MODE_MASK
	if mode == AUTH_INPUT_MODE_DELTA_LOW_ENTROPY_DICT \
			or mode == AUTH_INPUT_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT \
			or mode == AUTH_INPUT_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT:
		return true
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
	var server_broadcast_recv_ms := float(prof_server_broadcast_recv_us_interval + state_transfer.receive_profile_usec) / 1000.0
	var handle_state_ms := float(prof_handle_state_us_interval) / 1000.0
	var handle_input_update_ms := float(prof_handle_input_update_us_interval) / 1000.0
	var recalc_pred_ms := float(prof_recalc_pred_us_interval) / 1000.0
	var adjust_time_scale_ms := float(prof_adjust_time_scale_us_interval) / 1000.0
	var car_store_old_pos_ms := float(prof_car_store_old_pos_us_interval) / 1000.0
	var car_post_render_ms := float(prof_car_post_render_us_interval) / 1000.0
	var auth_stats: Dictionary = server_netcode_session.consume_authoritative_packet_stats()
	var auth_packet_builds := int(auth_stats.get("auth_packets", 0))
	var auth_compression_candidates := int(auth_stats.get("auth_compression_candidates", 0))
	var auth_build_ms := float(auth_stats.get("auth_build_usec", 0)) / 1000.0
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
	var state_success := state_transfer.log_min_success_2pct if state_transfer.log_sent_count > 0 else 1.0
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
	var timing_sync_rtt_avg := 0.0
	if log_timing_sync_rtt_samples > 0:
		timing_sync_rtt_avg = log_timing_sync_rtt_ms_sum / float(log_timing_sync_rtt_samples)
	var peer_fields := _build_server_peer_log_fields()
	var state_pending := state_transfer.pending_log_fields()
	var line := str(Time.get_ticks_msec()) + "," + role + "," + str(multiplayer.get_unique_id()) + "," + str(is_server) + "," + str(listen_server) + "," + str(player_ids.size()) + "," + str(server_tick) + "," + str(target_tick) + "," + str(server_behind_ticks) + "," + str(server_behind_avg) + "," + str(log_server_behind_ticks_max) + "," + str(delayed_peers) + "," + str(local_tick) + "," + str(clients_server_tick) + "," + str(clients_target_tick) + "," + str(rtt_s) + "," + str(rtt_variance_s) + "," + str(INPUT_FORWARD_REDUNDANCY_TICKS) + "," + str(desired_ahead_ticks) + "," + str(logged_max_ahead) + "," + str(physics_tps) + "," + str(race_admission.server_start_msec) + "," + str(race_admission.local_start_msec) + "," + str(race_admission.actual_client_start_msec) + "," + str(race_admission.actual_server_start_msec) + "," + str(race_admission.first_authoritative_input_msec) + "," + str(race_admission.first_authoritative_first_tick) + "," + str(race_admission.first_authoritative_last_tick) + "," + str(race_admission.first_authoritative_count) + "," + str(up_kbps) + "," + str(down_kbps) + "," + str(log_bytes_out_total / 1000.0) + "," + str(log_bytes_in_total / 1000.0) + "," + str(log_inputs_sent) + "," + str(log_inputs_acked) + "," + str(log_inputs_retransmitted) + "," + str(log_flat_client_payload_out) + "," + str(log_flat_client_payload_in) + "," + str(log_flat_server_payload_out) + "," + str(log_flat_server_payload_in) + "," + str(log_server_late_drops) + "," + str(log_server_replacements) + "," + str(state_transfer.log_raw_out) + "," + str(state_transfer.log_payload_out) + "," + str(state_transfer.log_sent_count) + "," + str(state_transfer.log_max_fragments_out) + "," + str(state_success) + "," + str(state_transfer.log_payload_in) + "," + str(state_transfer.log_raw_in) + "," + str(state_transfer.log_recv_count) + "," + str(state_transfer.log_max_recv_gap_ms) + "," + str(auth_packets) + "," + str(auth_packet_builds) + "," + str(auth_compression_candidates) + "," + str(auth_build_ms) + "," + str(auth_frames) + "," + str(auth_encoded_inputs) + "," + str(auth_unchanged_inputs) + "," + str(auth_payload_per_packet) + "," + str(auth_raw_per_packet) + "," + str(auth_compression_ratio) + "," + str(AUTH_INPUT_REDUNDANCY_FRAMES) + "," + str(AUTH_INPUT_ROLLBACK_WINDOW_TICKS) + "," + str(net_cpu_ms) + "," + str(sim_cpu_ms) + "," + str(rollback_avg_ms) + "," + str(rollback_max_ms) + "," + str(collect_inputs_ms) + "," + str(idle_broadcast_ms) + "," + str(check_client_stalls_ms) + "," + str(client_send_input_ms) + "," + str(server_broadcast_recv_ms) + "," + str(handle_state_ms) + "," + str(handle_input_update_ms) + "," + str(recalc_pred_ms) + "," + str(adjust_time_scale_ms) + "," + str(car_store_old_pos_ms) + "," + str(car_post_render_ms)
	line += "," + str(client_current_ahead) + "," + str(client_target_ahead) + "," + str(client_ahead_error) + "," + str(client_server_gap) + "," + str(client_unacked["count"]) + "," + str(client_unacked["oldest"]) + "," + str(client_unacked["newest"]) + "," + str(last_ack_tick) + "," + str(client_ack_lag) + "," + str(log_client_ahead_throttle_frames) + "," + str(use_physics_ticks)
	line += "," + str(log_client_sim_ticks) + "," + str(log_client_target_tick_advances) + "," + str(log_client_target_tick_remote_advances) + "," + str(log_client_server_tick_advances) + "," + str(log_client_ahead_samples) + "," + str(client_current_ahead_min) + "," + str(client_current_ahead_max) + "," + str(client_current_ahead_avg) + "," + str(client_target_ahead_avg) + "," + str(client_ahead_error_min) + "," + str(client_ahead_error_max) + "," + str(client_ahead_error_avg) + "," + str(log_client_pre_auth_adjust_samples)
	line += "," + str(peer_fields["server_peer_lag_max"]) + "," + str(peer_fields["server_peer_lag_avg"]) + "," + str(peer_fields["target_peer_lag_max"]) + "," + str(peer_fields["target_peer_lag_avg"]) + "," + str(peer_fields["peer_ahead_min"]) + "," + str(peer_fields["peer_ahead_max"]) + "," + str(peer_fields["peer_ahead_avg"]) + "," + str(peer_fields["peer_rtt_max_ms"]) + "," + str(peer_fields["peer_rtt_avg_ms"]) + "," + str(peer_fields["peer_inputs_accepted"]) + "," + str(peer_fields["peer_inputs_dropped"]) + "," + str(peer_fields["peer_replacements"]) + "," + str(peer_fields["peer_input_server_lead_min"]) + "," + str(peer_fields["peer_input_server_lead_max"]) + "," + str(peer_fields["peer_input_server_lead_avg"]) + "," + str(peer_fields["peer_input_target_lead_min"]) + "," + str(peer_fields["peer_input_target_lead_max"]) + "," + str(peer_fields["peer_input_target_lead_avg"]) + "," + str(peer_fields["peer_snapshot"])
	line += "," + str(log_timing_ping_out) + "," + str(log_timing_ping_in) + "," + str(log_timing_sync_out) + "," + str(log_timing_sync_in) + "," + str(timing_sync_rtt_avg) + "," + str(log_timing_sync_rtt_ms_max) + "," + str(log_timing_server_gap_max) + "," + str(log_timing_target_gap_max) + "," + str(log_timing_ack_advance)
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
	state_transfer.reset_interval_counters()
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

func _acc_log_out(bytes: int) -> void:
	log_bytes_out_interval += bytes
	log_bytes_out_total += bytes

func _acc_log_in(bytes: int) -> void:
	log_bytes_in_interval += bytes
	log_bytes_in_total += bytes

func _reset_timing_sync_log_counters() -> void:
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
	rtt_variance_s = 0.0
	race_results.reset()
	pending_next_race_track_id = ""
	pending_next_race_settings.clear()
	pending_next_race_options.clear()
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	peer_client_rtt_s.clear()
	lobby_settings.reset_latency()
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
	_reset_timing_sync_log_counters()
	clients_server_tick = 0
	clients_target_tick = 0
	last_client_timing_ping_msec = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	last_server_input_tick = -1
	state_transfer.reset()
	use_physics_ticks = 1.0
	last_target_tick_update = Time.get_ticks_msec()
	desired_ahead_ticks = 0.0 if is_server else 2.0
	net_input_debug_prints = 0
	race_admission.reset()
	lobby_settings.reset_settings(preserved_player_settings)
	_sync_lobby_settings_context()
	refresh_race_admission_context()
	netcode_session.reset()
	server_netcode_session.reset()
	server_netcode_session.clear_peer_state()

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

func _on_player_dnf_recorded(player_id: int) -> void:
	_disconnected_during_race[player_id] = true
	delayed_peer_ids.erase(player_id)

func _sync_lobby_settings_context() -> void:
	lobby_settings.set_context(is_server, race_active, network_active, player_ids, spectator_ids)

func refresh_race_admission_context() -> void:
	race_admission.set_context(
		is_server,
		listen_server,
		network_active,
		race_active,
		race_netplay_phase,
		player_ids,
		_get_race_ready_roster(),
		rtt_s,
		desired_ahead_ticks,
		game_sim,
		server_game_sim)

func _on_lobby_cpu_removed(player_id: int) -> void:
	for tick in pending_inputs.keys():
		pending_inputs[tick].erase(player_id)

func _on_lobby_latency_sample_received(peer_id: int, sample_rtt_s: float) -> void:
	if is_server:
		peer_client_rtt_s[peer_id] = sample_rtt_s
	else:
		_record_rtt_sample(sample_rtt_s)
		lobby_settings.set_local_latency(rtt_s)

func _on_admission_local_rtt_sample_received(sample_rtt_s: float) -> void:
	_record_rtt_sample(sample_rtt_s)
	race_admission.set_local_timing(rtt_s, desired_ahead_ticks)

func _on_admission_peer_timing_sample_received(peer_id: int, peer_rtt_s: float, ahead_ticks: float) -> void:
	peer_desired_ahead[peer_id] = ahead_ticks
	peer_client_rtt_s[peer_id] = peer_rtt_s
	server_netcode_session.set_peer_desired_ahead(peer_id, ahead_ticks)

func _on_admission_disconnect_peer_requested(peer_id: int) -> void:
	if multiplayer.multiplayer_peer != null and multiplayer.get_peers().has(peer_id):
		multiplayer.disconnect_peer(peer_id)
		_on_peer_disconnected(peer_id)
		refresh_race_admission_context()

func _on_admission_start_schedule_received(initial_max_ahead: float) -> void:
	clients_max_ahead_from_server = initial_max_ahead
	clients_server_tick = 0
	last_client_timing_ping_msec = 0
	last_target_tick_update = Time.get_ticks_msec()

func _on_admission_client_simulation_start_requested(initial_target_tick: int) -> void:
	if game_sim == null or game_sim.sim_started:
		return
	game_sim.set_sim_started(true)
	local_tick = 0
	clients_server_tick = 0
	clients_target_tick = clampi(initial_target_tick, 0, MAX_AHEAD_TICKS)
	last_client_timing_ping_msec = 0
	last_target_tick_update = Time.get_ticks_msec()

func _on_admission_authoritative_simulation_start_requested() -> void:
	if !is_server or server_game_sim == null or server_game_sim.sim_started:
		return
	server_tick = 0
	target_tick = 0
	server_game_sim.set_sim_started(true)

func _on_lobby_player_role_changed(player_id: int, spectator: bool) -> void:
	if player_id == multiplayer.get_unique_id():
		desired_ahead_ticks = 1.0 if spectator else (0.0 if is_server else 2.0)
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
	refresh_race_admission_context()
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
	refresh_race_admission_context()
	state_transfer.initialize(server_netcode_session)
	state_transfer.state_received.connect(_handle_state)
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
	server_process_timer.timeout.connect(server_process)
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

func server_process() -> void:
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
			var server_inputs := collect_server_inputs()
			var _collect_t1 := Time.get_ticks_usec()
			prof_collect_server_inputs_us_interval += _collect_t1 - _collect_t0
			if server_inputs.is_empty():
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
	_acc_log_out(8)
	_client_timing_ping.rpc_id(1, now, _pack_race_phase_tick(local_tick))

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
	server_tick = 0
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	rtt_variance_s = 0.0
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
	_sync_lobby_settings_context()
	lobby_settings.ensure_cpu_ids_do_not_overlap_humans()
	lobby_settings.reset_settings()
	custom_stamp_network.clear()
	clients_server_tick = 0
	clients_target_tick = 0
	last_client_timing_ping_msec = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	server_netcode_session.clear_peer_state()
	last_server_input_tick = -1
	state_transfer.reset()
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	net_input_debug_prints = 0
	race_admission.reset()
	lobby_settings.clear_race_cpu_roster()
	get_window().title = "Host"
	if !multiplayer.peer_connected.is_connected(_on_peer_connected):
		multiplayer.peer_connected.connect(_on_peer_connected)
	if !multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
	refresh_race_admission_context()
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
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	rtt_variance_s = 0.0
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
	last_client_timing_ping_msec = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	last_server_input_tick = -1
	state_transfer.reset()
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	net_input_debug_prints = 0
	race_admission.reset()
	player_ids = [multiplayer.get_unique_id()]
	_sync_lobby_settings_context()
	lobby_settings.reset_all()
	custom_stamp_network.clear()
	refresh_race_admission_context()
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
		refresh_race_admission_context()
		if last_input_time.has(id):
			last_input_time.erase(id)
		if peer_desired_ahead.has(id):
			peer_desired_ahead.erase(id)
		if peer_client_rtt_s.has(id):
			peer_client_rtt_s.erase(id)
		if delayed_peer_ids.has(id):
			delayed_peer_ids.erase(id)
		lobby_settings.remove_player(id)
		custom_stamp_network.remove_peer(id)
		if last_received_tick.has(id):
			last_received_tick.erase(id)
		race_admission.remove_peer(id)
		state_transfer.remove_peer(id)
		server_netcode_session.remove_peer(id)
		for key in pending_inputs:
			if pending_inputs[key].has(id):
				pending_inputs[key].erase(id)
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
	refresh_race_admission_context()
	last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
	peer_desired_ahead[id] = 0.0
	server_netcode_session.set_peer_last_received(id, -1, last_input_time[id])
	server_netcode_session.set_peer_desired_ahead(id, 0.0)
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
		last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
		peer_desired_ahead[id] = 0.0
		server_netcode_session.set_peer_last_received(id, -1, last_input_time[id])
		server_netcode_session.set_peer_desired_ahead(id, 0.0)
		if !race_active:
			lobby_settings.send_player_settings_snapshot_to_peer(id)
			custom_stamp_network.send_manifests_to_peer(id)
	waiting_peers.clear()
	_sync_lobby_settings_context()
	_update_player_ids.rpc(player_ids)
	refresh_race_admission_context()
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
	refresh_race_admission_context()
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
	refresh_race_admission_context()
	if is_server:
		race_admission.initialize_states()
		state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
	emit_signal("race_started", track_id, settings)
	if is_server:
		var now := 0.001 * float(Time.get_ticks_msec())
		for id in race_player_ids + spectator_ids:
			last_input_time[id] = now
			server_netcode_session.set_peer_last_received(id, -1, now)

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
	refresh_race_admission_context()
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
			if race_results.player_eliminations.has(id):
				pending_inputs[server_tick][id] = NEUTRAL_INPUT_BYTES
				server_netcode_session.store_pending_input(server_tick, int(id), NEUTRAL_INPUT_BYTES)
			elif _human_racer_uses_native_cpu_input(int(id)):
				var cpu_input: PackedByteArray = server_game_sim.get_native_cpu_input_for_tick(int(id), server_tick)
				pending_inputs[server_tick][id] = cpu_input
				server_netcode_session.store_pending_input(server_tick, int(id), cpu_input)
	if server_tick > target_tick:
		return {}
	_fill_delayed_missing_inputs_for_tick(server_tick)
	var sim_t0 := Time.get_ticks_usec()
	var ticked := server_netcode_session.tick_server_frame(server_game_sim, server_tick)
	log_sim_cpu_us_interval += Time.get_ticks_usec() - sim_t0
	if !ticked:
		return {}
	var frame_inputs: Dictionary = server_netcode_session.get_frame_as_dictionary(server_tick)
	authoritative_history[server_tick] = frame_inputs
	pending_inputs.erase(server_tick)
	return frame_inputs

func _input_forward_window(last_tick: int) -> Vector2i:
	if last_tick < STARTUP_LIGHT_NET_TICKS:
		return Vector2i(-1, 0)
	var first_tick := maxi(STARTUP_LIGHT_NET_TICKS, last_tick - INPUT_FORWARD_REDUNDANCY_TICKS + 1)
	return Vector2i(first_tick, last_tick - first_tick + 1)

func collect_client_inputs() -> Dictionary:
	if game_sim != null and !game_sim.sim_started:
		return {}
	var my_settings = lobby_settings.player_settings.get(multiplayer.get_unique_id(), {})
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
			_client_startup_sync.rpc_id(1, desired_ahead_ticks, _pack_race_phase_tick(local_tick), rtt_s)
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
			var last_forward_tick := local_tick - 1
			var window := _input_forward_window(last_forward_tick)
			if window.y > 0:
				var packet: PackedByteArray = netcode_session.build_local_input_packet(window.x, window.y, race_netplay_phase)
				log_flat_client_payload_out += packet.size()
				_acc_log_out(12 + packet.size())
				_client_send_input_flat.rpc_id(1, packet, desired_ahead_ticks, rtt_s)
				last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		return {}
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
		_acc_log_out(12 + input_packet.size())
		log_inputs_sent += window.y
		log_inputs_retransmitted += maxi(window.y - 1, 0)
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

func _send_server_timing_sync(peer_id: int, sync_tick: int, max_ahead: float, echo_client_msec: int = -1) -> void:
	if !is_server or !race_active:
		return
	if listen_server and peer_id == multiplayer.get_unique_id():
		return
	if !_can_send_rpc_to_peer(peer_id):
		return
	var ack_tick := server_netcode_session.get_peer_last_received(peer_id)
	log_timing_sync_out += 1
	_acc_log_out(28)
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
	_acc_log_in(28)

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
	_acc_log_in(12)
	_prune_authoritative_history()

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
		var stats: Dictionary = server_netcode_session.store_pending_input_packet(sender_id, reject_before, packet, ahead, now_sec, race_netplay_phase)
		if !bool(stats.get("valid", false)):
			return
		if bool(stats.get("stale", false)):
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
			if last_tick >= recovery_cutoff and delayed_peer_ids.has(sender_id):
				delayed_peer_ids.erase(sender_id)
		peer_desired_ahead[sender_id] = ahead
		peer_client_rtt_s[sender_id] = client_rtt_s
		log_flat_client_payload_in += packet.size()
		_acc_log_in(12 + packet.size())
		_prune_authoritative_history()
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
		var stats: Dictionary = netcode_session.store_authoritative_input_packet(input_packet, race_netplay_phase, authoritative_last_tick, input_packet_meta)
		if bool(stats.get("valid", false)):
			if bool(stats.get("stale", false)):
				return
			var count := int(stats.get("count", 0))
			if count > 0:
				var first_tick := int(stats.get("first_tick", -1))
				var last_tick := int(stats.get("last_tick", -1))
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
	_acc_log_in(12 + input_packet.size())
	var __prof_t1 := Time.get_ticks_usec()
	prof_server_broadcast_recv_us_interval += __prof_t1 - __prof_t0

func post_tick() -> void:
	if !race_active:
		return
	if is_server and server_game_sim != null:
		var _t0 := Time.get_ticks_usec()
		var authoritative_inputs: Dictionary = server_netcode_session.get_frame_as_dictionary(server_tick)
		if !authoritative_inputs.is_empty():
			authoritative_server_frame.emit(server_tick, authoritative_inputs)
		var max_ahead := _calc_max_ahead()
		max_ahead_from_server = max_ahead
		var recipients := state_transfer.peer_ids
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
				_acc_log_out(12)
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
			_acc_log_out(12 + input_packet.size())
			_server_broadcast_flat.rpc_id(id, _pack_authoritative_input_tick(server_tick, input_packet_meta), input_packet, server_netcode_session.get_peer_last_received(id))
		server_tick += 1
		race_results.set_race_tick(server_tick)
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
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
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
	rtt_variance_s = 0.0
	custom_stamp_network.clear()
	_unverified_peers.clear()
	_version_request_time.clear()
	state_transfer.reset()
	race_results.reset()
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
	_reset_timing_sync_log_counters()
	clients_server_tick = 0
	clients_target_tick = 0
	last_client_timing_ping_msec = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	last_server_input_tick = -1
	use_physics_ticks = 1.0
	Engine.physics_ticks_per_second = 60
	server_netcode_session.clear_peer_state()
	netcode_session.reset()
	server_netcode_session.reset()
	_sync_lobby_settings_context()
	refresh_race_admission_context()
	race_admission.reset()

func _prune_authoritative_history() -> void:
	var window_cutoff := server_tick - MAX_HISTORY_TICKS
	if window_cutoff < 0:
		return
	for key in authoritative_history.keys():
		if key <= window_cutoff:
			authoritative_history.erase(key)

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
	var remaining_ticks := maxi(0, race_results.race_force_end_deadline_tick - get_race_tick())
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
