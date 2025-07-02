class_name NetworkManager
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
var NEUTRAL_INPUT_BYTES : PackedByteArray = PlayerInputClass.new().serialize()

var is_server: bool = false
var listen_server: bool = false
var player_ids: Array = []
var waiting_peers: Array = []
var pending_inputs := {}
var authoritative_inputs := {}
var input_history := {}
var last_input_time := {}
var last_local_input_bytes : PackedByteArray = NEUTRAL_INPUT_BYTES.duplicate()
var sent_inputs_bytes := {}
var server_tick: int = 0
var local_tick: int = 0
const INPUT_HISTORY_SIZE := 300
var game_sim: GameSim
var server_game_sim: GameSim
var last_received_tick := {}
var last_ack_tick: int = -1
var target_tick: int = 0
const MAX_AHEAD_TICKS := 30
var sent_input_times := {}
var rtt_s: float = 0.0
var desired_ahead_ticks: float = 2.0
var base_wait_time: float = 1.0 / 60.0
const JITTER_BUFFER := 0.016
const RTT_SMOOTHING := 0.1
const SPEED_ADJUST_STEP := 0.005
var player_settings := {}
const STATE_BROADCAST_INTERVAL_TICKS := 60
var state_send_offsets := {}
var net_race_finish_time := -1
var max_ahead_from_server: float = 0.0
var peer_desired_ahead := {}

var clients_server_tick = 0
var clients_target_tick = 0
var clients_max_ahead_from_server = 2.0
var authoritative_history := {}
var authoritative_acks := {}
var last_server_input_tick := -1

func reset_race_state() -> void:
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
	max_ahead_from_server = 0.0
	peer_desired_ahead.clear()
	clients_server_tick = 0
	clients_target_tick = 0
	clients_max_ahead_from_server = 2.0
	authoritative_history.clear()
	authoritative_acks.clear()
	last_server_input_tick = -1
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
	var server_process_timer = Timer.new()
	server_process_timer.ignore_time_scale = true
	add_child(server_process_timer)
	server_process_timer.timeout.connect(server_process)
	server_process_timer.start(1.0 / 60.0)
	multiplayer.server_disconnected.connect(on_disconnect)

func on_disconnect() -> void:
	DebugDraw2D.set_text("DISCONNECTED!", null, 10, Color.RED, 10)

func server_process() -> void:
	if is_server and server_game_sim != null and server_game_sim.sim_started:
		target_tick += 1
		if target_tick > server_tick + MAX_AHEAD_TICKS:
			target_tick = server_tick + MAX_AHEAD_TICKS
		if server_tick < target_tick:
			_idle_broadcast()
		_check_client_stalls()


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
	if !multiplayer.peer_connected.is_connected(_on_peer_connected):
		multiplayer.peer_connected.connect(_on_peer_connected)
	if !multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	_calc_state_offsets()
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
	player_ids = [multiplayer.get_unique_id()]
	player_settings.clear()
	return OK

func _on_peer_connected(id: int) -> void:
	if is_server:
		if server_game_sim != null and server_game_sim.sim_started:
			waiting_peers.append(id)
			_update_player_ids.rpc_id(id, player_ids)
			for pid in player_settings.keys():
				update_player_settings.rpc_id(id, player_settings[pid], pid)
			return
		player_ids.append(id)
		last_input_time[id] = 0.001 * float(Time.get_ticks_msec())
		peer_desired_ahead[id] = 0.0
		_update_player_ids.rpc(player_ids)
		for pid in player_settings.keys():
			update_player_settings.rpc_id(id, player_settings[pid], pid)
		_calc_state_offsets()

func _on_peer_disconnected(id: int) -> void:
	if is_server:
		if waiting_peers.has(id):
			waiting_peers.erase(id)
			return
		player_ids.erase(id)
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
		_update_player_ids.rpc(player_ids)
		_calc_state_offsets()

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
	emit_signal("race_started", track_index, settings)
	if is_server:
		var now := 0.001 * float(Time.get_ticks_msec())
		for id in player_ids:
			last_input_time[id] = now

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

func set_local_input(input: PackedByteArray) -> void:
	last_local_input_bytes = input

func collect_server_inputs() -> Array:
	if not is_server:
		return []
	if not pending_inputs.has(server_tick):
		pending_inputs[server_tick] = {}
	if not pending_inputs[server_tick].has(multiplayer.get_unique_id()):
		pending_inputs[server_tick][multiplayer.get_unique_id()] = last_local_input_bytes
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())
		last_received_tick[multiplayer.get_unique_id()] = server_tick
	if server_tick > target_tick:
		return []
	var dict = pending_inputs[server_tick]
	for id in player_ids:
		if not dict.has(id):
			return []
	var frame_inputs_bytes: Array = []
	for id in player_ids:
		frame_inputs_bytes.append(dict[id])
	authoritative_history[server_tick] = frame_inputs_bytes
	pending_inputs.erase(server_tick)
	return frame_inputs_bytes

func collect_client_inputs() -> Array:
	if local_tick >= clients_target_tick + MAX_AHEAD_TICKS:
		if !is_server:
			var old_keys := sent_inputs_bytes.keys()
			if old_keys.size() > 0:
				old_keys.sort()
				var start := int(old_keys[0])
				var data : Array = []
				for k in old_keys:
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
		var first_tick := int(all_keys[0])
		var inputs_arr : Array = []
		for k in all_keys:
			inputs_arr.append(sent_inputs_bytes[k])
		_client_send_input.rpc_id(1, first_tick, inputs_arr, desired_ahead_ticks, last_server_input_tick)
		last_input_time[multiplayer.get_unique_id()] = 0.001 * float(Time.get_ticks_msec())

	var frame_inputs: Array
	if authoritative_inputs.has(local_tick):
		frame_inputs = authoritative_inputs[local_tick]
		authoritative_inputs.erase(local_tick)
	else:
		frame_inputs = []
		for id in player_ids:
			if id == multiplayer.get_unique_id():
				frame_inputs.append(last_local_input_bytes)
			else:
				frame_inputs.append(NEUTRAL_INPUT_BYTES)
	input_history[local_tick] = frame_inputs
	if input_history.has(local_tick - INPUT_HISTORY_SIZE):
		input_history.erase(local_tick - INPUT_HISTORY_SIZE)
	local_tick += 1
	_adjust_time_scale()
	return frame_inputs

@rpc("any_peer", "unreliable_ordered", "call_remote", 1)
func _client_send_input(start_tick: int, inputs: Array, ahead: float, ack: int) -> void:
	if is_server:
		for i in range(inputs.size()):
			var tick := start_tick + i
			var input = inputs[i]
			if not pending_inputs.has(tick):
				pending_inputs[tick] = {}
			pending_inputs[tick][multiplayer.get_remote_sender_id()] = input
			last_input_time[multiplayer.get_remote_sender_id()] = 0.001 * float(Time.get_ticks_msec())
			last_received_tick[multiplayer.get_remote_sender_id()] = tick
		peer_desired_ahead[multiplayer.get_remote_sender_id()] = ahead
		authoritative_acks[multiplayer.get_remote_sender_id()] = max(ack, authoritative_acks.get(multiplayer.get_remote_sender_id(), -1))
		_prune_authoritative_history()

@rpc("any_peer", "unreliable_ordered", "call_local", 2)
func _server_broadcast(last_tick: int, inputs: Array, ids: Array, this_ack: int, state: PackedByteArray, tgt: int, max_ahead: float) -> void:
	if not is_server or listen_server:
		clients_server_tick = max(clients_server_tick, last_tick + 1)
		clients_target_tick = max(clients_target_tick, tgt)
		clients_max_ahead_from_server = max_ahead
		player_ids = ids
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
		last_ack_tick = max(last_ack_tick, ack_tick)
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
		_handle_state(last_tick, state)

func post_tick() -> void:
	if is_server and server_game_sim != null:
		var state = server_game_sim.get_state_data(server_tick)
		var max_ahead := _calc_max_ahead()
		max_ahead_from_server = max_ahead
		for id in player_ids:
			var send_state : PackedByteArray = PackedByteArray()
			if state_send_offsets.has(id) and int(state_send_offsets[id]) == server_tick % STATE_BROADCAST_INTERVAL_TICKS:
				send_state = state
			var ack = authoritative_acks.get(id, -1)
			var keys := authoritative_history.keys()
			keys.sort()
			var start := -1
			var arr : Array = []
			for k in keys:
				if k > ack:
					if start == -1:
						start = k
					arr.append(authoritative_history[k])
			var last_tick = start + arr.size() - 1 if arr.size() > 0 else ack
			_server_broadcast.rpc_id(id, last_tick, arr, player_ids, last_received_tick.get(id, -1), send_state, target_tick, max_ahead)
		server_tick += 1

func _idle_broadcast() -> void:
	if server_game_sim == null:
		return
	var state = server_game_sim.get_state_data(server_tick)
	var max_ahead := _calc_max_ahead()
	max_ahead_from_server = max_ahead
	for id in player_ids:
		var ack = authoritative_acks.get(id, -1)
		var keys := authoritative_history.keys()
		keys.sort()
		var start := -1
		var arr : Array = []
		for k in keys:
			if k > ack:
				if start == -1:
					start = k
				arr.append(authoritative_history[k])
		var last_tick = start + arr.size() - 1 if arr.size() > 0 else ack
		_server_broadcast.rpc_id(
			id,
			last_tick,
			arr,
			player_ids,
			last_received_tick.get(id, -1),
			PackedByteArray(),
			target_tick,
			max_ahead
		)

func _check_client_stalls() -> void:
	if not is_server or server_game_sim == null or not server_game_sim.sim_started:
		return
	# don’t test while still waiting for the very first full frame
	if server_tick == 0:
		return
	if server_tick >= target_tick:
		return
	var waiting = pending_inputs.get(server_tick, {})
	var now := 0.001 * float(Time.get_ticks_msec())
	for id in player_ids:
		if not waiting.has(id):
			if not last_input_time.has(id):
				continue	# haven’t ever received a packet from this guy yet
			if now - float(last_input_time[id]) > 10.0:	# give them 10 s grace
				push_error("Client %s stalled, disconnecting" % str(id))
				multiplayer.disconnect_peer(id)

var rollback_frametime_us = 0

func _handle_state(tick: int, state: PackedByteArray) -> void:
	if game_sim == null:
		return
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
	rollback_frametime_us = new_time - old_time

func _handle_input_update(tick: int, inputs: Array) -> void:
	if game_sim == null:
		return
	if not input_history.has(tick):
		return
	#var predicted = input_history[tick]
	# we should honestly just always be rolling back for now
	# we can figure out matching later
	#if predicted == inputs:
	#	return
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
	rollback_frametime_us = new_time - old_time

func disconnect_from_server() -> void:
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

func _prune_authoritative_history() -> void:
	var min_ack := -1
	for id in player_ids:
		var ack = authoritative_acks.get(id, -1)
		if min_ack == -1 or ack < min_ack:
			min_ack = ack
	if min_ack == -1:
		return
	for key in authoritative_history.keys():
		if key <= min_ack:
			authoritative_history.erase(key)

func _update_desired_ahead() -> void:
	desired_ahead_ticks = ((rtt_s) + JITTER_BUFFER) / base_wait_time

var use_physics_ticks := 1.0

func _adjust_time_scale() -> void:
	DebugDraw2D.set_text("rtt", rtt_s)
	if is_server:
		DebugDraw2D.set_text("server_tick", server_tick)
		DebugDraw2D.set_text("target_tick", target_tick)
	if is_server and !listen_server:
		return
	var current_ahead_ticks = local_tick - clients_target_tick
	var target_ahead_ticks = lerpf(desired_ahead_ticks, clients_max_ahead_from_server, 0.75)
	var diff = target_ahead_ticks - current_ahead_ticks
	DebugDraw2D.set_text("local_tick", local_tick)
	DebugDraw2D.set_text("clients_server_tick", clients_server_tick)
	DebugDraw2D.set_text("clients_target_tick", clients_target_tick)
	DebugDraw2D.set_text("desired_ahead_ticks", desired_ahead_ticks)
	DebugDraw2D.set_text("server_max_ahead", clients_max_ahead_from_server)
	DebugDraw2D.set_text("target_ahead_ticks", target_ahead_ticks)
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
