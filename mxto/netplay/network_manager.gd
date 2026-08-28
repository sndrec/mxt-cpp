class_name NetworkManager
extends Node

signal race_started(track_id, player_settings)
signal race_finished
signal race_setup_changed(configuration, track_evidence, race_state)
const ProximityVoiceChatClass = preload("res://netplay/proximity_voice_chat.gd")
const GameVersionData = preload("res://core/game_version.gd")
const StateTransferControllerClass = preload("res://netplay/state_transfer_controller.gd")
const RaceResultsControllerClass = preload("res://netplay/race_results_controller.gd")
const LobbySettingsControllerClass = preload("res://netplay/lobby_settings_controller.gd")
const RaceAdmissionControllerClass = preload("res://netplay/race_admission_controller.gd")
const InputTransportControllerClass = preload("res://netplay/input_transport_controller.gd")
const NetworkTelemetryControllerClass = preload("res://netplay/network_telemetry_controller.gd")
const RACE_CONFIGURATION_METADATA_KEYS := [
	"game_mode", "vehicle_restore", "bumpers", "s_boost", "allow_workshop_vehicles",
	"boost_unlocked_from_start", "cpu_count", "cpu_vehicle_content_ids", "lap_count",
	"session_kind", "time_attack_ruleset_revision", "leaderboard_eligible",
	"leaderboard_ineligible_reason", "practice_local_player_id", "resumed_from_replay",
	"custom_content", "infinite_laps",
]
const TRACK_EVIDENCE_METADATA_KEYS := [
	"track_ids", "track_gameplay_digests", "track_package_digests", "track_workshop_ids",
]
@onready var game_manager: GameManager = $".."
@onready var custom_stamp_network: CustomStampNetworkController = $CustomStampNetwork
@onready var state_transfer: StateTransferControllerClass = $StateTransferController
@onready var race_results: RaceResultsControllerClass = $RaceResultsController
@onready var lobby_settings: LobbySettingsControllerClass = $LobbySettingsController
@onready var race_admission: RaceAdmissionControllerClass = $RaceAdmissionController
@onready var input_transport: InputTransportControllerClass = $InputTransportController
@onready var telemetry: NetworkTelemetryControllerClass = $NetworkTelemetryController

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
var race_configuration: MxtRaceConfiguration = MxtRaceConfiguration.new()
var race_track_evidence: MxtTrackContentEvidence = MxtTrackContentEvidence.new()
var race_state := {
	"race_netplay_phase": 0,
	"grand_prix_current_track": 0,
	"grand_prix_points": {},
	"grand_prix_ko_energy_bonuses": {},
}
var race_netplay_phase := 0
var pending_next_race_track_id := ""
var pending_next_race_settings: Array = []
var pending_next_race_configuration: MxtRaceConfiguration
var pending_next_race_track_evidence: MxtTrackContentEvidence
var pending_next_race_state: Dictionary = {}


var race_active: bool = false


var version_string: String = GameVersionData.display_string()
var _unverified_peers: Array = []
var _version_request_time := {}

func prepare_race_roster(reason: String) -> void:
	var changed := false
	_sync_lobby_settings_context()
	if is_server or !has_network_peer():
		changed = lobby_settings.ensure_cpu_ids_do_not_overlap_humans()
	if changed and is_server and !race_active:
		lobby_settings.broadcast_cpu_roster()

func reserve_next_race_netplay_state(state: Dictionary) -> Dictionary:
	var out := state.duplicate(true)
	if !is_server:
		return out
	out["race_netplay_phase"] = 1 - race_netplay_phase
	return out

func _race_phase_from_state(state: Dictionary) -> int:
	return int(state.get("race_netplay_phase", race_netplay_phase)) & 1

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
	pending_next_race_configuration = null
	pending_next_race_track_evidence = null
	pending_next_race_state.clear()
	lobby_settings.reset_latency()
	state_transfer.reset()
	race_admission.reset()
	lobby_settings.reset_settings(preserved_player_settings)
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	input_transport.reset()

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
	telemetry.set_context(is_server, listen_server, network_active, player_ids, _get_active_human_roster())
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
		_update_player_ids.rpc(player_ids, spectator_ids)
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
	telemetry.initialize(game_manager, custom_stamp_network, state_transfer, lobby_settings, race_admission, input_transport)
	refresh_protocol_contexts()
	state_transfer.initialize(input_transport.server_netcode_session)
	state_transfer.state_received.connect(input_transport._handle_state)
	state_transfer.state_sample_generated.connect(telemetry.dump_state_sample)
	state_transfer.wire_bytes_sent.connect(telemetry._acc_log_out)
	state_transfer.wire_bytes_received.connect(telemetry._acc_log_in)
	race_results.player_dnf_recorded.connect(_on_player_dnf_recorded)
	proximity_voice_chat = get_node_or_null("ProximityVoiceChat") as ProximityVoiceChat
	if proximity_voice_chat == null:
		proximity_voice_chat = ProximityVoiceChatClass.new()
		proximity_voice_chat.name = "ProximityVoiceChat"
		add_child(proximity_voice_chat)
	var server_process_timer = Timer.new()
	server_process_timer.ignore_time_scale = true
	add_child(server_process_timer)
	server_process_timer.timeout.connect(input_transport.process)
	server_process_timer.start(1.0 / 60.0)
	multiplayer.server_disconnected.connect(on_disconnect)

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
	return OK

func _on_peer_connected(id: int) -> void:
	telemetry.record_peer_connected()
	if is_server:
		if !_unverified_peers.has(id):
			_unverified_peers.append(id)
		_version_request_time[id] = 0.001 * float(Time.get_ticks_msec())
		_request_client_version.rpc_id(id, version_string)
func _on_peer_disconnected(id: int) -> void:
	telemetry.record_peer_disconnected()
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
			_update_player_ids.rpc(player_ids, spectator_ids)
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
	_update_player_ids.rpc(player_ids, spectator_ids)
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)


func _accept_peer(id: int) -> void:
	if _unverified_peers.has(id):
		_unverified_peers.erase(id)
	if _version_request_time.has(id):
		_version_request_time.erase(id)
	if race_active or (server_game_sim != null and server_game_sim.sim_started):
		if !waiting_peers.has(id):
			waiting_peers.append(id)
		_update_player_ids.rpc_id(id, player_ids, spectator_ids)
		lobby_settings.send_cpu_roster_to_peer(id)
		sync_race_state.rpc_id(id, race_configuration.encode_wire(), race_track_evidence.encode_wire(), race_state)
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
		_update_player_ids.rpc(player_ids, spectator_ids)
		if cpu_ids_changed:
			lobby_settings.broadcast_cpu_roster()
		lobby_settings.send_cpu_roster_to_peer(id)
		sync_race_state.rpc_id(id, race_configuration.encode_wire(), race_track_evidence.encode_wire(), race_state)
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
	_update_player_ids.rpc(player_ids, spectator_ids)
	refresh_protocol_contexts()
	state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)
	for id in new_ids:
		lobby_settings.send_cpu_roster_to_peer(id)
		if !race_active and lobby_settings.player_settings.has(id):
			lobby_settings.send_player_settings_to_all(lobby_settings.player_settings[id], id)

func broadcast_lobby_roster() -> void:
	if !is_server:
		return
	_update_player_ids.rpc(player_ids, spectator_ids)
	lobby_settings.broadcast_cpu_roster()

@rpc("authority", "call_remote", "reliable", 7)
func _update_player_ids(ids: Array, spectators: Array) -> void:
	player_ids = ids
	spectator_ids = spectators
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	if is_server:
		state_transfer.rebuild_peer_schedule(is_server, player_ids, spectator_ids)

@rpc("authority", "call_remote", "reliable", 7)
func start_race(track_id: String, settings: Array, configuration_bytes: PackedByteArray, track_evidence_bytes: PackedByteArray, state: Dictionary = {}) -> void:
	prepare_race_roster("start_race")
	var configuration := MxtRaceConfiguration.new()
	if !configuration.decode_wire(configuration_bytes):
		push_error("Rejected malformed race configuration: %s" % configuration.get_last_error())
		return
	var track_evidence := MxtTrackContentEvidence.new()
	if !track_evidence.decode_wire(track_evidence_bytes):
		push_error("Rejected malformed track content evidence: %s" % track_evidence.get_last_error())
		return
	var incoming_phase := _race_phase_from_state(state)
	if !_accept_race_start_phase(incoming_phase):
		return
	race_admission.reset()
	race_configuration = configuration
	race_track_evidence = track_evidence
	race_state = state.duplicate(true)
	if race_state.has("spawn_seed"):
		set_spawn_seed(int(race_state.get("spawn_seed", spawn_seed)))
	race_active = true
	state_transfer.set_race_context(true, race_netplay_phase)
	race_results.set_context(true, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
	if proximity_voice_chat != null:
		proximity_voice_chat.reset()
	if race_state.has("race_human_ids"):
		race_player_ids = _id_array_from_value(race_state.get("race_human_ids", []))
	else:
		race_player_ids = player_ids.duplicate(true)
	if race_state.has("race_cpu_ids"):
		lobby_settings.set_race_cpu_roster(_id_array_from_value(race_state.get("race_cpu_ids", [])))
	else:
		lobby_settings.set_race_cpu_roster(lobby_settings.cpu_player_ids)
	if race_state.has("race_spectator_ids"):
		spectator_ids = _id_array_from_value(race_state.get("race_spectator_ids", []))
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

func send_start_race(track_id: String, settings: Array, configuration: MxtRaceConfiguration, track_evidence: MxtTrackContentEvidence, state: Dictionary = {}) -> void:
	if !is_server:
		return
	if configuration == null:
		push_error("Cannot start a race without a race configuration.")
		return
	if track_evidence == null:
		push_error("Cannot start a race without track content evidence.")
		return
	if state.is_empty():
		state = reserve_next_race_netplay_state(race_state)
	else:
		state = reserve_next_race_netplay_state(state)
	race_state = state.duplicate(true)
	race_configuration = configuration.copy()
	race_track_evidence = track_evidence.copy()
	# Generate and distribute a shared spawn seed before starting the race.
	# This lets all peers randomize starting grid slots deterministically.
	state["spawn_seed"] = randi()
	race_state = state.duplicate(true)
	var configuration_bytes := race_configuration.encode_wire()
	var track_evidence_bytes := race_track_evidence.encode_wire()
	start_race.rpc(track_id, settings, configuration_bytes, track_evidence_bytes, state)
	start_race(track_id, settings, configuration_bytes, track_evidence_bytes, state)

@rpc("authority", "call_remote", "reliable", 7)
func end_race(phase: int, next_track_id: String = "", next_settings: Array = [], next_configuration_bytes: PackedByteArray = PackedByteArray(), next_track_evidence_bytes: PackedByteArray = PackedByteArray(), next_state: Dictionary = {}) -> void:
	if !_accept_race_packet_phase(phase):
		return
	pending_next_race_track_id = next_track_id
	pending_next_race_settings = next_settings.duplicate(true)
	pending_next_race_configuration = null
	pending_next_race_track_evidence = null
	if !next_configuration_bytes.is_empty():
		var decoded_configuration := MxtRaceConfiguration.new()
		if !decoded_configuration.decode_wire(next_configuration_bytes):
			push_error("Rejected malformed next-race configuration: %s" % decoded_configuration.get_last_error())
			return
		pending_next_race_configuration = decoded_configuration
	if !next_track_evidence_bytes.is_empty():
		var decoded_track_evidence := MxtTrackContentEvidence.new()
		if !decoded_track_evidence.decode_wire(next_track_evidence_bytes):
			push_error("Rejected malformed next-race track evidence: %s" % decoded_track_evidence.get_last_error())
			return
		pending_next_race_track_evidence = decoded_track_evidence
	pending_next_race_state = next_state.duplicate(true)
	if !pending_next_race_state.is_empty():
		race_state = pending_next_race_state.duplicate(true)
	if pending_next_race_configuration != null:
		race_configuration = pending_next_race_configuration.copy()
	if pending_next_race_track_evidence != null:
		race_track_evidence = pending_next_race_track_evidence.copy()
	race_active = false
	state_transfer.set_race_context(false, race_netplay_phase)
	race_results.set_context(false, race_netplay_phase, is_server, network_active)
	_sync_lobby_settings_context()
	refresh_protocol_contexts()
	if proximity_voice_chat != null:
		proximity_voice_chat.reset()
	emit_signal("race_finished")

func send_end_race(next_track_id: String = "", next_settings: Array = [], next_configuration: MxtRaceConfiguration = null, next_track_evidence: MxtTrackContentEvidence = null, next_state: Dictionary = {}) -> void:
	if is_server:
		var configuration_bytes := next_configuration.encode_wire() if next_configuration != null else PackedByteArray()
		var track_evidence_bytes := next_track_evidence.encode_wire() if next_track_evidence != null else PackedByteArray()
		end_race.rpc(race_netplay_phase, next_track_id, next_settings, configuration_bytes, track_evidence_bytes, next_state)
		end_race(race_netplay_phase, next_track_id, next_settings, configuration_bytes, track_evidence_bytes, next_state)

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
	pending_next_race_configuration = null
	pending_next_race_track_evidence = null
	pending_next_race_state.clear()
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
	return race_configuration.vehicle_restore

func is_s_boost_enabled() -> bool:
	return race_configuration.s_boost

func is_grand_prix_enabled() -> bool:
	return race_configuration.game_mode == 1

func race_metadata_dictionary() -> Dictionary:
	var value := race_configuration.to_metadata_dictionary()
	value.merge(race_track_evidence.to_metadata_dictionary(), true)
	value.merge(race_state, true)
	return value

func load_race_metadata_dictionary(value: Dictionary) -> void:
	var configuration := MxtRaceConfiguration.new()
	configuration.load_metadata_dictionary(value)
	var track_evidence := MxtTrackContentEvidence.new()
	track_evidence.load_metadata_dictionary(value)
	race_configuration = configuration
	race_track_evidence = track_evidence
	race_state = value.duplicate(true)
	for key in RACE_CONFIGURATION_METADATA_KEYS:
		race_state.erase(key)
	for key in TRACK_EVIDENCE_METADATA_KEYS:
		race_state.erase(key)

@rpc("authority", "call_local", "reliable")
func sync_race_state(configuration_bytes: PackedByteArray, track_evidence_bytes: PackedByteArray, state: Dictionary) -> void:
	if race_active and state.has("race_netplay_phase") and !_accept_race_packet_phase(int(state.get("race_netplay_phase", race_netplay_phase))):
		return
	var configuration := MxtRaceConfiguration.new()
	if !configuration.decode_wire(configuration_bytes):
		push_error("Rejected malformed lobby race configuration: %s" % configuration.get_last_error())
		return
	var track_evidence := MxtTrackContentEvidence.new()
	if !track_evidence.decode_wire(track_evidence_bytes):
		push_error("Rejected malformed lobby track evidence: %s" % track_evidence.get_last_error())
		return
	race_configuration = configuration
	race_track_evidence = track_evidence
	race_state = state.duplicate(true)
	race_setup_changed.emit(race_configuration.copy(), race_track_evidence.copy(), race_state.duplicate(true))

func send_race_state(configuration: MxtRaceConfiguration, track_evidence: MxtTrackContentEvidence, state: Dictionary) -> void:
	if is_server and configuration != null and track_evidence != null:
		var configuration_bytes := configuration.encode_wire()
		var track_evidence_bytes := track_evidence.encode_wire()
		sync_race_state.rpc(configuration_bytes, track_evidence_bytes, state)
		sync_race_state(configuration_bytes, track_evidence_bytes, state)
