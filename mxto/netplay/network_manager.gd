class_name NetworkManager
extends Node

signal race_started(track_index, player_settings)

const PlayerInputClass = preload("res://player/player_input.gd")
var NEUTRAL_INPUT = PlayerInputClass.new().to_dict()

var is_server: bool = false
var player_ids: Array = []
var pending_inputs := {}
var authoritative_inputs := {}
var input_history := {}
var sent_inputs := {}
var last_local_input := NEUTRAL_INPUT.duplicate()
var server_tick: int = 0
var local_tick: int = 0
const INPUT_HISTORY_SIZE := 15
var game_sim: GameSim
var last_received_tick := {}
var last_ack_tick: int = -1
var last_broadcast_inputs: Array = []
var target_tick: int = 0
const MAX_AHEAD_TICKS := 15
var sent_input_times := {}
var rtt_s: float = 0.0
var desired_ahead_ticks: float = 2.0
var base_wait_time: float = 1.0 / 60.0
const JITTER_BUFFER := 0.016
const RTT_SMOOTHING := 0.1
const SPEED_ADJUST_STEP := 0.005
var _accum: float = 0.0	# local frame accumulator
var player_settings := {}
const STATE_BROADCAST_INTERVAL_TICKS := 60
var state_send_offsets := {}

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

func _physics_process(delta: float) -> void:
	if is_server and game_sim != null and game_sim.sim_started:
		target_tick += 1

func host(port: int = 27016, max_players: int = 64) -> int:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_players)
	if err != OK:
		push_error("Failed to host: %s" % err)
		return err
	multiplayer.multiplayer_peer = peer
	is_server = true
	server_tick = 0
	target_tick = 0
	rtt_s = 0.0
	desired_ahead_ticks = 0.0
	sent_input_times.clear()
	last_received_tick.clear()
	player_ids = [multiplayer.get_unique_id()]
	player_settings.clear()
	multiplayer.peer_connected.connect(_on_peer_connected)
	multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	_calc_state_offsets()
	return OK

func join(ip: String, port: int = 27016) -> int:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client(ip, port)
	if err != OK:
		push_error("Failed to join server: %s" % err)
		return err
	multiplayer.multiplayer_peer = peer
	is_server = false
	local_tick = 0
	target_tick = 0
	last_ack_tick = -1
	rtt_s = 0.0
	desired_ahead_ticks = 2.0
	sent_input_times.clear()
	input_history.clear()
	sent_inputs.clear()
	player_ids = [multiplayer.get_unique_id()]
	player_settings.clear()
	return OK

func _on_peer_connected(id: int) -> void:
	if is_server:
		player_ids.append(id)
		_update_player_ids.rpc(player_ids)
		_calc_state_offsets()

func _on_peer_disconnected(id: int) -> void:
	if is_server:
		player_ids.erase(id)
		_update_player_ids.rpc(player_ids)
		_calc_state_offsets()

@rpc("any_peer")
func _update_player_ids(ids: Array) -> void:
	player_ids = ids
	if is_server:
		_calc_state_offsets()

@rpc("any_peer")
func start_race(track_index: int, settings: Array) -> void:
				emit_signal("race_started", track_index, settings)

func send_start_race(track_index: int, settings: Array) -> void:
	if is_server:
		start_race.rpc(track_index, settings)
		start_race(track_index, settings)
	else:
		start_race.rpc_id(1, track_index, settings)

func send_player_settings(settings: Dictionary) -> void:
	if is_server:
		update_player_settings(settings)
		update_player_settings.rpc(settings)
	else:
		update_player_settings.rpc_id(1, settings)
		player_settings[multiplayer.get_unique_id()] = settings

@rpc("any_peer")
func update_player_settings(settings: Dictionary) -> void:
	var id := multiplayer.get_remote_sender_id()
	if id == 0:
		id = multiplayer.get_unique_id()
	player_settings[id] = settings

func set_local_input(input: Dictionary) -> void:
	last_local_input = input

func collect_inputs() -> Array:
	if is_server:
		if not pending_inputs.has(server_tick):
			pending_inputs[server_tick] = {}
		pending_inputs[server_tick][multiplayer.get_unique_id()] = last_local_input
		last_received_tick[multiplayer.get_unique_id()] = server_tick
		if server_tick > target_tick:
			return []
		var dict = pending_inputs[server_tick]
		for id in player_ids:
			if not dict.has(id):
				return []
		var frame_inputs: Array = []
		for id in player_ids:
			frame_inputs.append(dict[id])
		pending_inputs.erase(server_tick)
		last_broadcast_inputs = frame_inputs
		return frame_inputs
	else:
		if local_tick >= target_tick + MAX_AHEAD_TICKS:
			return []
		sent_inputs[local_tick] = last_local_input
		sent_input_times[local_tick] = 0.001 * float(Time.get_ticks_msec())
		for key in sent_inputs.keys():
			_client_send_input.rpc_id(1, key, sent_inputs[key])
		var frame_inputs: Array
		if authoritative_inputs.has(local_tick):
			frame_inputs = authoritative_inputs[local_tick]
			authoritative_inputs.erase(local_tick)
		else:
			frame_inputs = []
			for id in player_ids:
				if id == multiplayer.get_unique_id():
					frame_inputs.append(last_local_input)
				else:
					frame_inputs.append(NEUTRAL_INPUT)
		input_history[local_tick] = frame_inputs
		if input_history.has(local_tick - INPUT_HISTORY_SIZE):
			input_history.erase(local_tick - INPUT_HISTORY_SIZE)
		local_tick += 1
		_adjust_time_scale()
		return frame_inputs

@rpc("any_peer", "unreliable", "call_local")
func _client_send_input(tick: int, input: Dictionary) -> void:
	if is_server:
		if not pending_inputs.has(tick):
			pending_inputs[tick] = {}
		pending_inputs[tick][multiplayer.get_remote_sender_id()] = input
		last_received_tick[multiplayer.get_remote_sender_id()] = tick

@rpc("any_peer", "unreliable", "call_local")
func _server_broadcast(tick: int, inputs: Array, ids: Array, acks: Dictionary, state: PackedByteArray, tgt: int) -> void:
	if not is_server:
		server_tick = max(server_tick, tick + 1)
		target_tick = max(target_tick, tgt)
		player_ids = ids
		authoritative_inputs[tick] = inputs
		_handle_input_update(tick, inputs)
		if acks.has(multiplayer.get_unique_id()):
			var ack_tick := int(acks[multiplayer.get_unique_id()])
			last_ack_tick = max(last_ack_tick, ack_tick)
			if sent_input_times.has(ack_tick):
				var sample : float = 0.001 * float(Time.get_ticks_msec()) - sent_input_times[ack_tick]
				if rtt_s == 0.0:
					rtt_s = sample
				else:
					rtt_s = lerp(rtt_s, sample, RTT_SMOOTHING)
				sent_input_times.erase(ack_tick)
				_update_desired_ahead()
			for key in sent_inputs.keys():
				if key <= last_ack_tick:
					sent_inputs.erase(key)
			for key in sent_input_times.keys():
				if key <= last_ack_tick:
					sent_input_times.erase(key)
				if state.size() > 0:
					_handle_state(tick, state)

func post_tick() -> void:
	if is_server and game_sim != null:
		var state = game_sim.get_state_data(server_tick)
		for id in player_ids:
			var send_state : PackedByteArray = PackedByteArray()
			if state_send_offsets.has(id) and int(state_send_offsets[id]) == server_tick % STATE_BROADCAST_INTERVAL_TICKS:
				send_state = state
			_server_broadcast.rpc_id(id, server_tick, last_broadcast_inputs, player_ids, last_received_tick, send_state, target_tick)
		server_tick += 1

func _handle_state(tick: int, state: PackedByteArray) -> void:
	if game_sim == null:
		return
	var local_state: PackedByteArray = game_sim.get_state_data(tick)
	if hash(local_state) != hash(state):
		game_sim.set_state_data(tick, state)
		game_sim.load_state(tick)
		var current := tick + 1
		var old_time := Time.get_ticks_usec()
		while current < local_tick:
			if input_history.has(current):
				game_sim.tick_gamesim(input_history[current])
			current += 1
		var new_time := Time.get_ticks_usec()
		DebugDraw2D.set_text("rollback frametime microseconds", new_time - old_time)

func _handle_input_update(tick: int, inputs: Array) -> void:
	if game_sim == null:
		return
	if not input_history.has(tick):
		return
	var predicted = input_history[tick]
	if predicted == inputs:
		return
	input_history[tick] = inputs
	game_sim.load_state(max(0, tick - 1))
	var current := tick
	var old_time := Time.get_ticks_usec()
	while current < local_tick:
		if input_history.has(current):
			game_sim.tick_gamesim(input_history[current])
		current += 1
	var new_time := Time.get_ticks_usec()
	DebugDraw2D.set_text("rollback frametime microseconds", new_time - old_time)

func disconnect_from_server() -> void:
	if multiplayer.multiplayer_peer != null:
		multiplayer.multiplayer_peer.close()
		multiplayer.multiplayer_peer = null
	is_server = false
	player_ids.clear()
	pending_inputs.clear()
	authoritative_inputs.clear()
	input_history.clear()
	sent_inputs.clear()
	last_local_input = NEUTRAL_INPUT.duplicate()
	server_tick = 0
	local_tick = 0
	target_tick = 0
	last_received_tick.clear()
	last_ack_tick = -1
	last_broadcast_inputs.clear()
	player_settings.clear()

func _update_desired_ahead() -> void:
	desired_ahead_ticks = ((rtt_s * 0.5) + JITTER_BUFFER) / base_wait_time

var use_physics_ticks := 1.0

func _adjust_time_scale() -> void:
	if is_server:
		return
	var current_ahead_ticks = local_tick - target_tick
	var diff = desired_ahead_ticks - current_ahead_ticks
	DebugDraw2D.set_text("local_tick", local_tick)
	DebugDraw2D.set_text("server_tick", server_tick)
	DebugDraw2D.set_text("target_tick", target_tick)
	DebugDraw2D.set_text("desired_ahead_ticks", desired_ahead_ticks)
	DebugDraw2D.set_text("current_ahead_ticks", current_ahead_ticks)
	DebugDraw2D.set_text("diff", diff)
	DebugDraw2D.set_text("Engine.physics_ticks_per_second", Engine.physics_ticks_per_second)
	if abs(diff) <= 1:
		use_physics_ticks = lerp(use_physics_ticks, 1.0, RTT_SMOOTHING)
		Engine.physics_ticks_per_second = roundi(use_physics_ticks * 60.0);
		return
	if diff > 0:
		use_physics_ticks = clamp(use_physics_ticks + SPEED_ADJUST_STEP, 0.5, 1.5)
	else:
		use_physics_ticks = clamp(use_physics_ticks - SPEED_ADJUST_STEP, 0.5, 1.5)
	Engine.physics_ticks_per_second = roundi(use_physics_ticks * 60.0);
