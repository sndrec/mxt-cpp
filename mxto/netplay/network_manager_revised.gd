class_name NetworkManagerRevised
extends Node

signal race_started(track_index, player_settings)
signal race_finished

@rpc("any_peer", "reliable")
func set_race_finish_time(time: int) -> void:
	net_race_finish_time = time

func send_race_finish_time(time: int) -> void:
	if is_server:
		set_race_finish_time.rpc(time)
		set_race_finish_time(time)

const PlayerInputClass = preload("res://player/player_input.gd")
const MAX_AHEAD_TICKS := 30
const INPUT_HISTORY_SIZE := 30
const STATE_BROADCAST_INTERVAL_TICKS := 60
var NEUTRAL_INPUT_BYTES : PackedByteArray = PlayerInputClass.new().serialize()

# SHARED STATE VARS

var net_race_finish_time : int = -1
var player_ids: Array = []
var player_settings := {}
var is_server: bool = false
var listen_server: bool = false

# SERVER STATE VARS

var server_game_sim: GameSim
var target_tick : int = 0
var server_tick : int = 0
var server_acks : PackedInt64Array = []

# CLIENT STATE VARS

var game_sim: GameSim
var desired_ahead_ticks : float = 0.0
var rtt_s: float = 0.0
var local_tick : int = 0
var max_ahead_from_server : float = 0.0
var client_known_server_tick : int = 0
var client_known_target_tick : int = 0
var client_latest_ack_tick : int = 0


func on_disconnect() -> void:
	DebugDraw2D.set_text("DISCONNECTED!", null, 10, Color.RED, 10)
	disconnect_from_server()

func server_process() -> void:
	if server_game_sim != null and server_game_sim.sim_started:
		target_tick += 1
		if target_tick > server_tick + MAX_AHEAD_TICKS:
			target_tick = server_tick + MAX_AHEAD_TICKS

func client_process() -> void:
	if game_sim != null and game_sim.sim_started:
		client_known_target_tick += 1
		if client_known_target_tick > client_known_server_tick + MAX_AHEAD_TICKS:
			client_known_target_tick = client_known_server_tick + MAX_AHEAD_TICKS

func _physics_process(delta: float) -> void:
	pass

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
	if !multiplayer.peer_connected.is_connected(_on_peer_connected):
		multiplayer.peer_connected.connect(_on_peer_connected)
	if !multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	var server_process_timer = Timer.new()
	server_process_timer.ignore_time_scale = true
	add_child(server_process_timer)
	server_process_timer.timeout.connect(server_process)
	server_process_timer.start(1.0 / 60.0)
	multiplayer.server_disconnected.connect(on_disconnect)
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
	var client_process_timer = Timer.new()
	client_process_timer.ignore_time_scale = true
	add_child(client_process_timer)
	client_process_timer.timeout.connect(client_process)
	client_process_timer.start(1.0 / 60.0)
	multiplayer.server_disconnected.connect(on_disconnect)
	return OK




func _on_peer_connected(id: int) -> void:
	if is_server:
		if server_game_sim != null and server_game_sim.sim_started:
			# TODO: implement waiting peers list for when characters join mid-race
			# waiting_peers.append(id)
			_update_player_ids.rpc_id(id, player_ids)
			for pid in player_settings.keys():
				update_player_settings.rpc_id(id, player_settings[pid], pid)
			return
		player_ids.append(id)
		_update_player_ids.rpc(player_ids)
		for pid in player_settings.keys():
			update_player_settings.rpc_id(id, player_settings[pid], pid)

func _on_peer_disconnected(id: int) -> void:
	if is_server:
		#if waiting_peers.has(id):
			#waiting_peers.erase(id)
			#return
		if not player_ids.has(id):
			return
		player_ids.erase(id)
		if player_settings.has(id):
			player_settings.erase(id)
		_update_player_ids.rpc(player_ids)

@rpc("any_peer", "reliable")
func _update_player_ids(ids: Array) -> void:
	player_ids = ids

@rpc("any_peer", "reliable")
func start_race(track_index: int, settings: Array) -> void:
	emit_signal("race_started", track_index, settings)
	if is_server:
		var now := 0.001 * float(Time.get_ticks_msec())

func send_start_race(track_index: int, settings: Array) -> void:
	if is_server:
		start_race.rpc(track_index, settings)
		start_race(track_index, settings)
	else:
		start_race.rpc_id(1, track_index, settings)

@rpc("any_peer", "reliable")
func end_race() -> void:
	emit_signal("race_finished")

func send_end_race() -> void:
	if is_server:
		end_race.rpc()
		end_race()

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

var rollback_frametime_us = 0

func disconnect_from_server() -> void:
	if multiplayer.multiplayer_peer != null:
		multiplayer.multiplayer_peer.close()
		multiplayer.multiplayer_peer = null
	is_server = false
	listen_server = false

func _update_desired_ahead() -> void:
	desired_ahead_ticks = ((rtt_s) + 0.016) * 60.0

var use_physics_ticks := 1.0

	# game simulation uses a fixed delta time
	# this just changes the rate at which we simulate the game locally
	# to catch up or slow down to try and match the server
func _adjust_time_scale() -> void:
	DebugDraw2D.set_text("rtt", rtt_s)
	if is_server:
		DebugDraw2D.set_text("server_tick", server_tick)
		DebugDraw2D.set_text("target_tick", target_tick)
	if is_server and !listen_server:
		return
	var current_ahead_ticks = local_tick - client_known_target_tick
	var target_ahead_ticks = lerpf(desired_ahead_ticks, max_ahead_from_server, 0.75)
	var diff = target_ahead_ticks - current_ahead_ticks
	DebugDraw2D.set_text("local_tick", local_tick)
	DebugDraw2D.set_text("clients_server_tick", client_known_server_tick)
	DebugDraw2D.set_text("clients_target_tick", client_known_target_tick)
	DebugDraw2D.set_text("desired_ahead_ticks", desired_ahead_ticks)
	DebugDraw2D.set_text("server_max_ahead", max_ahead_from_server)
	DebugDraw2D.set_text("target_ahead_ticks", target_ahead_ticks)
	DebugDraw2D.set_text("current_ahead_ticks", current_ahead_ticks)
	DebugDraw2D.set_text("diff", diff)
	DebugDraw2D.set_text("Engine.physics_ticks_per_second", Engine.physics_ticks_per_second)
	if abs(diff) <= 1:
		use_physics_ticks = lerp(use_physics_ticks, 1.0, 0.1)
		Engine.physics_ticks_per_second = roundi(use_physics_ticks * 60.0);
		return
	if diff > 0:
		use_physics_ticks = clamp(use_physics_ticks + 0.0003 * absf(diff), 1.0, 2.0)
	else:
		use_physics_ticks = clamp(use_physics_ticks - 0.0003 * absf(diff), 0.5, 1.0)
	Engine.physics_ticks_per_second = roundi(use_physics_ticks * 60.0);
