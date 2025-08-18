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
var netcode_core := NetcodeCore.new()

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
var last_received_tick := {}
var last_ack_tick: int = -1
var target_tick: int = 0
const MAX_AHEAD_TICKS := 30
const MAX_HISTORY_TICKS := 60
var sent_input_times := {}
var rtt_s: float = 0.0
var desired_ahead_ticks: float = 2.0
var base_wait_time: float = 1.0 / 60.0
const JITTER_BUFFER := 0.016
const RTT_SMOOTHING := 0.1
const SPEED_ADJUST_STEP := 0.0003
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

var clients_server_tick = 0
var clients_target_tick = 0
var last_target_tick_update = 0
var clients_max_ahead_from_server = 2.0
var authoritative_history := {}
var authoritative_acks := {}
var last_server_input_tick := -1
var latest_state_tick := -1
var race_active: bool = false

var use_state_compression := true

var log_enabled := false
var log_file: FileAccess
var log_bytes_out_total := 0
var log_bytes_in_total := 0
var log_bytes_out_interval := 0
var log_bytes_in_interval := 0
var log_inputs_sent := 0
var log_inputs_retransmitted := 0
var log_inputs_acked := 0
var log_server_late_drops := 0
var log_server_replacements := 0
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
		log_file.store_line("time,role,uid,is_server,listen,players,server_tick,target_tick,local_tick,clients_server_tick,clients_target_tick,rtt,desired_ahead,server_max_ahead,physics_tps,up_kbps,down_kbps,up_total_kb,down_total_kb,inputs_sent,inputs_acked,retrans,late_drops,replacements,net_cpu_ms,sim_cpu_ms,rollback_avg_ms,rollback_max_ms,collect_inputs_ms,idle_broadcast_ms,check_client_stalls_ms,client_send_input_ms,server_broadcast_recv_ms,handle_state_ms,handle_input_update_ms,recalc_pred_ms,adjust_time_scale_ms,car_store_old_pos_ms,car_post_render_ms")
	_log_timer = Timer.new()
	_log_timer.wait_time = 1.0
	_log_timer.one_shot = false
	_log_timer.timeout.connect(_flush_log)
	add_child(_log_timer)
	_log_timer.start()

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

	var line := str(Time.get_ticks_msec()) + "," + role + "," + str(multiplayer.get_unique_id()) + "," + str(is_server) + "," + str(listen_server) + "," + str(player_ids.size()) + "," + str(server_tick) + "," + str(target_tick) + "," + str(local_tick) + "," + str(clients_server_tick) + "," + str(clients_target_tick) + "," + str(rtt_s) + "," + str(desired_ahead_ticks) + "," + str(max_ahead_from_server) + "," + str(physics_tps) + "," + str(up_kbps) + "," + str(down_kbps) + "," + str(log_bytes_out_total / 1000.0) + "," + str(log_bytes_in_total / 1000.0) + "," + str(log_inputs_sent) + "," + str(log_inputs_acked) + "," + str(log_inputs_retransmitted) + "," + str(log_server_late_drops) + "," + str(log_server_replacements) + "," + str(net_cpu_ms) + "," + str(sim_cpu_ms) + "," + str(rollback_avg_ms) + "," + str(rollback_max_ms) + "," + str(collect_inputs_ms) + "," + str(idle_broadcast_ms) + "," + str(check_client_stalls_ms) + "," + str(client_send_input_ms) + "," + str(server_broadcast_recv_ms) + "," + str(handle_state_ms) + "," + str(handle_input_update_ms) + "," + str(recalc_pred_ms) + "," + str(adjust_time_scale_ms) + "," + str(car_store_old_pos_ms) + "," + str(car_post_render_ms)
	log_file.store_line(line)
	log_file.flush()
	log_bytes_out_interval = 0
	log_bytes_in_interval = 0
	log_net_cpu_us_interval = 0
	log_rollback_us_sum = 0
	log_rollback_us_count = 0
	log_rollback_us_max = 0
	log_inputs_retransmitted = 0
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
	return total

func reset_race_state() -> void:
	race_active = false
	race_player_ids.clear()
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
	var max_ahead : float = desired_ahead_ticks
	for id in peer_desired_ahead.keys():
		var ahead := float(peer_desired_ahead[id])
		if ahead > max_ahead:
			max_ahead = ahead
	return max_ahead

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
			var _sim_t0 := Time.get_ticks_usec()
			server_game_sim.tick_gamesim(server_inputs)
			var _sim_t1 := Time.get_ticks_usec()
			log_sim_cpu_us_interval += _sim_t1 - _sim_t0
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
	desired_ahead_ticks = 2.0 if listen_server else 0.0
	sent_input_times.clear()
	last_input_time.clear()
	last_received_tick.clear()
	input_history.clear()
	sent_inputs_bytes.clear()
	last_local_input_bytes = NEUTRAL_INPUT_BYTES.duplicate()
	player_ids = [multiplayer.get_unique_id()]
	player_settings.clear()
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	authoritative_acks.clear()
	last_server_input_tick = -1
	latest_state_tick = -1
	get_window().title = "Host"
	if !multiplayer.peer_connected.is_connected(_on_peer_connected):
		multiplayer.peer_connected.connect(_on_peer_connected)
	if !multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	_calc_state_offsets()
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
	player_ids = [multiplayer.get_unique_id()]
	player_settings.clear()
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
	last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
	peer_desired_ahead[id] = 0.0
	if !race_active:
		_update_player_ids.rpc(player_ids)
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
	race_active = true
	race_player_ids = player_ids.duplicate(true)
	emit_signal("race_started", track_index, settings)
	if is_server:
		var now := 0.001 * float(Time.get_ticks_msec())
		for id in player_ids + spectator_ids:
			last_input_time[id] = now

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
				begin_simulation.rpc()
				begin_simulation()

@rpc("any_peer", "reliable")
func begin_simulation() -> void:
	if !race_active:
		return
	if game_sim != null:
		game_sim.set_sim_started(true)
		local_tick = 0
	if is_server and server_game_sim != null:
		server_game_sim.set_sim_started(true)
		target_tick = 0

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
				if !race_active:
					_update_player_ids.rpc(player_ids)
				_calc_state_offsets()
		else:
			if spectator_ids.has(id):
				spectator_ids.erase(id)
			if !player_ids.has(id):
				player_ids.append(id)
				if !race_active:
					_update_player_ids.rpc(player_ids)
				_calc_state_offsets()

func set_local_input(input: PackedByteArray) -> void:
	last_local_input_bytes = input

func collect_server_inputs() -> Array:
	if not is_server:
		return []
	if server_game_sim == null or !server_game_sim.sim_started:
		return []
	if not pending_inputs.has(server_tick):
		pending_inputs[server_tick] = {}
	if listen_server and not pending_inputs[server_tick].has(multiplayer.get_unique_id()):
		pending_inputs[server_tick][multiplayer.get_unique_id()] = last_local_input_bytes
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		last_received_tick[multiplayer.get_unique_id()] = server_tick
	if server_tick > target_tick:
		return []
	var dict = pending_inputs[server_tick]
	var roster : Array = race_player_ids if race_player_ids.size() > 0 else player_ids
	var prev = authoritative_history.get(server_tick - 1, [])
	for i in range(roster.size()):
		var pid = roster[i]
		if not dict.has(pid):
			if _disconnected_during_race.has(pid):
				var synth : PackedByteArray = NEUTRAL_INPUT_BYTES
				if prev.size() == roster.size():
					synth = prev[i]
				dict[pid] = synth
			else:
				return []
	var frame_inputs_bytes: Array = []
	for id in roster:
		frame_inputs_bytes.append(dict[id])
	authoritative_history[server_tick] = frame_inputs_bytes
	pending_inputs.erase(server_tick)
	return frame_inputs_bytes

func collect_client_inputs() -> Array:
	if game_sim != null and !game_sim.sim_started:
		return []
	var my_settings = player_settings.get(multiplayer.get_unique_id(), {})
	var is_spec = typeof(my_settings) == TYPE_DICTIONARY and my_settings.get("spectator", false)
	if is_spec:
		if authoritative_inputs.has(local_tick):
			var frame = authoritative_inputs[local_tick]
			authoritative_inputs.erase(local_tick)
			input_history[local_tick] = frame
			if input_history.has(local_tick - INPUT_HISTORY_SIZE):
				input_history.erase(local_tick - INPUT_HISTORY_SIZE)
			local_tick += 1
			_adjust_time_scale()
			return frame
		return []
	if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
		if !is_server:
			var old_keys := sent_inputs_bytes.keys()
			if old_keys.size() > 0:
				old_keys.sort()
				
				var start_index = max(old_keys.size() - 5, 0)
				var recent_keys : Array = []
				for i in range(start_index, old_keys.size()):
					recent_keys.append(old_keys[i])
				var start := int(recent_keys[0])
				var data : Array = []
				for k in recent_keys:
					data.append(sent_inputs_bytes[k])
				_client_send_input.rpc_id(1, start, data, desired_ahead_ticks, last_server_input_tick)
				last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		return []
	sent_inputs_bytes[local_tick] = last_local_input_bytes
	sent_input_times[local_tick] = 0.001 * float(Time.get_ticks_msec())
	if is_server:
		if not pending_inputs.has(local_tick):
			pending_inputs[local_tick] = {}
		pending_inputs[local_tick][multiplayer.get_unique_id()] = last_local_input_bytes
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		last_received_tick[multiplayer.get_unique_id()] = local_tick
	var all_keys := sent_inputs_bytes.keys()
	if !is_server and all_keys.size() > 0:
		all_keys.sort()
		
		var start_index = max(all_keys.size() - 5, 0)
		var recent_keys : Array = []
		for i in range(start_index, all_keys.size()):
			recent_keys.append(all_keys[i])
		var first_tick := int(recent_keys[0])
		var inputs_arr : Array = []
		for k in recent_keys:
			inputs_arr.append(sent_inputs_bytes[k])
		var _est2 := 12 + _estimate_pba_array_size(inputs_arr)
		_acc_log_out(_est2)
		for k in recent_keys:
			var prev := int(_log_sent_counts.get(k, 0))
			_log_sent_counts[k] = prev + 1
			if prev > 0:
				log_inputs_retransmitted += 1
			log_inputs_sent += 1
		_client_send_input.rpc_id(1, first_tick, inputs_arr, desired_ahead_ticks, last_server_input_tick)
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
	var frame_inputs: Array
	if authoritative_inputs.has(local_tick):
		frame_inputs = authoritative_inputs[local_tick]
		authoritative_inputs.erase(local_tick)
	else:
		var prev_frame = input_history.get(local_tick - 1, [])
		var roster : Array = race_player_ids if race_player_ids.size() > 0 else player_ids
		var existing := []
		existing.resize(roster.size())
		for i in range(roster.size()):
			if roster[i] == multiplayer.get_unique_id():
				existing[i] = last_local_input_bytes
		frame_inputs = netcode_core.build_predicted_frame(roster, multiplayer.get_unique_id(), existing, prev_frame, NEUTRAL_INPUT_BYTES)
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
		var reject_before := target_tick - 5
		for i in range(inputs.size()):
			var tick := start_tick + i
			if tick < reject_before:
				log_server_late_drops += 1
				continue
			var input = inputs[i]
			if !pending_inputs.has(tick):
				pending_inputs[tick] = {}
			pending_inputs[tick][multiplayer.get_remote_sender_id()] = input
			last_input_time[multiplayer.get_remote_sender_id()] = 0.001 * float(Time.get_ticks_msec())
			last_received_tick[multiplayer.get_remote_sender_id()] = tick
		peer_desired_ahead[multiplayer.get_remote_sender_id()] = ahead
		authoritative_acks[multiplayer.get_remote_sender_id()] = max(
			ack,
			authoritative_acks.get(multiplayer.get_remote_sender_id(), -1)
		)
		var _est := 12
		for e in inputs:
			if typeof(e) == TYPE_PACKED_BYTE_ARRAY:
				_est += (e as PackedByteArray).size()
		_acc_log_in(_est)
		_prune_authoritative_history()
	var __prof_t1 := Time.get_ticks_usec()
	prof_client_send_input_us_interval += __prof_t1 - __prof_t0

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

		# rollback once from the first updated tick
		_handle_input_update(start_tick, authoritative_inputs[start_tick])
		last_server_input_tick = max(last_server_input_tick, last_tick)
	if this_ack != -1:
		var ack_tick := this_ack
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
	if state.size() > 0:
		var _state_to_use := state
		if state_uncompressed_size > 0:
			# Compressed payload; decompress before handling.
			_state_to_use = state.decompress(state_uncompressed_size, FileAccess.COMPRESSION_ZSTD)
		_handle_state(last_tick, _state_to_use)
	var _est := 4 + _estimate_nested_inputs_size(inputs) + state.size() + 4 + 4 + 4 + 4
	_acc_log_in(_est)
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
		var keys := authoritative_history.keys()
		keys.sort()
		var ack_cache := {}
		# Prepare compressed snapshot lazily and reuse for all recipients this tick
		var compressed_ready := false
		var compressed_state : PackedByteArray = PackedByteArray()
		var uncompressed_size := 0
		for id in player_ids + spectator_ids:
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
			var ack = authoritative_acks.get(id, -1)
			var pack = ack_cache.get(ack)
			var start := -1
			var arr : Array = []
			var last_tick_local = ack
			if pack == null:
				for k in keys:
					if k > ack:
						if start == -1:
							start = k
						arr.append(authoritative_history[k])
				last_tick_local = start + arr.size() - 1 if arr.size() > 0 else ack
				ack_cache[ack] = {"arr": arr, "last": last_tick_local}
			else:
				arr = pack["arr"]
				last_tick_local = int(pack["last"])
			var _bytes := 4 + _estimate_nested_inputs_size(arr) + send_state.size() + 4 + 4 + 4 + 4
			_acc_log_out(_bytes)
			_server_broadcast.rpc_id(id, last_tick_local, arr, last_received_tick.get(id, -1), send_state, send_state_uncomp_size, target_tick, max_ahead)
		server_tick += 1
		if listen_server:
			authoritative_acks[multiplayer.get_unique_id()] = server_tick - 1
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
	var keys := authoritative_history.keys()
	keys.sort()
	var ack_cache := {}
	for id in player_ids + spectator_ids:
		var ack = authoritative_acks.get(id, -1)
		var pack = ack_cache.get(ack)
		var start := -1
		var arr : Array = []
		var last_tick = ack
		if pack == null:
			for k in keys:
				if k > ack:
					if start == -1:
						start = k
					arr.append(authoritative_history[k])
			last_tick = start + arr.size() - 1 if arr.size() > 0 else ack
			ack_cache[ack] = {"arr": arr, "last": last_tick}
		else:
			arr = pack["arr"]
			last_tick = int(pack["last"])
		var _bytes := 4 + _estimate_nested_inputs_size(arr) + 0 + 4 + 4 + 4 + 4
		_acc_log_out(_bytes)
		_server_broadcast.rpc_id(
			id,
			max(last_tick, 0),
			arr,
			last_received_tick.get(id, -1),
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
	var roster_chk : Array = race_player_ids if race_player_ids.size() > 0 else player_ids
	for id in roster_chk:
		if not waiting.has(id):
			missing.append(id)
			if last_input_time.has(id) and now - float(last_input_time[id]) > 10.0:
				if server_tick != 0:
					push_error("Client %s stalled, disconnecting" % str(id))
					multiplayer.disconnect_peer(id)
					_on_peer_disconnected(id)
					if !race_active:
						_update_player_ids.rpc(player_ids)
	if target_tick - server_tick > 5 and missing.size() > 0:
		var prev = authoritative_history.get(server_tick - 1, [])
		for i in range(roster_chk.size()):
			var pid = roster_chk[i]
			if missing.has(pid):
				var inp : PackedByteArray = NEUTRAL_INPUT_BYTES
				if prev.size() == roster_chk.size():
					inp = prev[i]
				waiting[pid] = inp
		log_server_replacements += missing.size()
	pending_inputs[server_tick] = waiting

func _handle_state(tick: int, state: PackedByteArray) -> void:
	if !race_active:
		return
	if game_sim == null:
		return
	if tick == -1:
		return
	var __prof_t0 := Time.get_ticks_usec()
	var _car1_t0 := Time.get_ticks_usec()
	for car:VisualCar in game_manager.car_node_container.get_children():
		car.store_old_pos()
	var _car1_t1 := Time.get_ticks_usec()
	prof_car_store_old_pos_us_interval += _car1_t1 - _car1_t0
	game_sim.set_state_data(tick, state)
	game_sim.load_state(tick)
	latest_state_tick = tick
	local_tick = max(local_tick, tick + 1)
	var current := tick + 1
	var old_time := Time.get_ticks_usec()
	while current < local_tick:
		if input_history.has(current):
			game_sim.tick_gamesim(input_history[current])
		current += 1
	game_sim.render_gamesim()
	var _car2_t0 := Time.get_ticks_usec()
	for car:VisualCar in game_manager.car_node_container.get_children():
		car.calculate_error()
		car.just_rendered()
	var _car2_t1 := Time.get_ticks_usec()
	prof_car_post_render_us_interval += _car2_t1 - _car2_t0
	var new_time := Time.get_ticks_usec()
	#DebugDraw2D.set_text("rollback frametime microseconds", new_time - old_time)
	rollback_frametime_us = new_time - old_time
	log_rollback_us_sum += rollback_frametime_us
	log_rollback_us_count += 1
	if rollback_frametime_us > log_rollback_us_max:
		log_rollback_us_max = rollback_frametime_us
	var __prof_t1 := Time.get_ticks_usec()
	prof_handle_state_us_interval += __prof_t1 - __prof_t0

func _handle_input_update(tick: int, inputs: Array) -> void:
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
	var _car1_t0 := Time.get_ticks_usec()
	for car:VisualCar in game_manager.car_node_container.get_children():
		car.store_old_pos()
	var _car1_t1 := Time.get_ticks_usec()
	prof_car_store_old_pos_us_interval += _car1_t1 - _car1_t0
	input_history[tick] = inputs
	if authoritative_inputs.has(tick):
		authoritative_inputs.erase(tick)
	_recalculate_future_predictions(tick + 1)
	game_sim.load_state(maxi(latest_state_tick, tick - 1))
	var current := maxi(latest_state_tick + 1, tick)
	var old_time := Time.get_ticks_usec()
	while current < local_tick:
		if input_history.has(current):
			game_sim.tick_gamesim(input_history[current])
		current += 1
	game_sim.render_gamesim()
	var _car2_t0 := Time.get_ticks_usec()
	for car:VisualCar in game_manager.car_node_container.get_children():
		car.calculate_error()
		car.just_rendered()
	var _car2_t1 := Time.get_ticks_usec()
	prof_car_post_render_us_interval += _car2_t1 - _car2_t0
	var new_time := Time.get_ticks_usec()
	#DebugDraw2D.set_text("rollback frametime microseconds", new_time - old_time)
	rollback_frametime_us = new_time - old_time
	var __prof_t1 := Time.get_ticks_usec()
	prof_handle_input_update_us_interval += __prof_t1 - __prof_t0

func _recalculate_future_predictions(start_tick: int) -> void:
	if !race_active:
		return
	var roster : Array = race_player_ids if race_player_ids.size() > 0 else player_ids
	netcode_core.recalc_future_predictions(start_tick, local_tick, roster, multiplayer.get_unique_id(), input_history, authoritative_inputs, NEUTRAL_INPUT_BYTES)

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

func _prune_authoritative_history() -> void:
	var min_ack := -1
	for id in player_ids:
		var ack = authoritative_acks.get(id, -1)
		if min_ack == -1 or ack < min_ack:
			min_ack = ack
	# Always enforce a sliding window to bound memory, even if some acks stall.
	var cutoff := min_ack
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
