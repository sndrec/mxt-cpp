class_name NetworkManager
extends Node

signal race_started(track_index)

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

func host(port: int = 3456, max_players: int = 4) -> int:
		var peer := ENetMultiplayerPeer.new()
		var err := peer.create_server(port, max_players)
		if err != OK:
				push_error("Failed to host: %s" % err)
				return err
		multiplayer.multiplayer_peer = peer
		is_server = true
		server_tick = 0
		last_received_tick.clear()
		player_ids = [multiplayer.get_unique_id()]
		multiplayer.peer_connected.connect(_on_peer_connected)
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)
		return OK

func join(ip: String, port: int = 3456) -> int:
		var peer := ENetMultiplayerPeer.new()
		var err := peer.create_client(ip, port)
		if err != OK:
				push_error("Failed to join server: %s" % err)
				return err
		multiplayer.multiplayer_peer = peer
		is_server = false
		local_tick = 0
		last_ack_tick = -1
		input_history.clear()
		sent_inputs.clear()
		player_ids = [multiplayer.get_unique_id()]
		return OK

func _on_peer_connected(id: int) -> void:
		if is_server:
				player_ids.append(id)
				rpc("_update_player_ids", player_ids)

func _on_peer_disconnected(id: int) -> void:
		if is_server:
				player_ids.erase(id)
				rpc("_update_player_ids", player_ids)

@rpc("any_peer")
func _update_player_ids(ids: Array) -> void:
               player_ids = ids

@rpc("any_peer")
func start_race(track_index: int) -> void:
               emit_signal("race_started", track_index)

func send_start_race(track_index: int) -> void:
               if is_server:
                               start_race.rpc(track_index)
                               start_race(track_index)
               else:
                               rpc_id(1, "start_race", track_index)

func set_local_input(input: Dictionary) -> void:
		last_local_input = input

func collect_inputs() -> Array:
		if is_server:
				if not pending_inputs.has(server_tick):
						pending_inputs[server_tick] = {}
				pending_inputs[server_tick][multiplayer.get_unique_id()] = last_local_input
				var frame_inputs: Array = []
				var dict = pending_inputs[server_tick]
				for id in player_ids:
						if dict.has(id):
								frame_inputs.append(dict[id])
						else:
								frame_inputs.append(NEUTRAL_INPUT)
				pending_inputs.erase(server_tick)
				last_broadcast_inputs = frame_inputs
				return frame_inputs
		else:
				sent_inputs[local_tick] = last_local_input
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
				return frame_inputs

@rpc("any_peer", "unreliable", "call_local")
func _client_send_input(tick: int, input: Dictionary) -> void:
		if is_server:
				if not pending_inputs.has(tick):
						pending_inputs[tick] = {}
				pending_inputs[tick][multiplayer.get_remote_sender_id()] = input
				last_received_tick[multiplayer.get_remote_sender_id()] = tick

@rpc("any_peer", "unreliable", "call_local")
func _server_broadcast(tick: int, inputs: Array, ids: Array, acks: Dictionary, state: PackedByteArray) -> void:
		if not is_server:
				server_tick = max(server_tick, tick + 1)
				player_ids = ids
				authoritative_inputs[tick] = inputs
				if acks.has(multiplayer.get_unique_id()):
						last_ack_tick = max(last_ack_tick, int(acks[multiplayer.get_unique_id()]))
						for key in sent_inputs.keys():
								if key <= last_ack_tick:
										sent_inputs.erase(key)
				_handle_state(tick, state)

func post_tick() -> void:
		if is_server and game_sim != null:
				var state = game_sim.get_state_data(server_tick)
				_server_broadcast.rpc(server_tick, last_broadcast_inputs, player_ids, last_received_tick, state)
				server_tick += 1

func _handle_state(tick: int, state: PackedByteArray) -> void:
		if game_sim == null:
				return
		var local_state: PackedByteArray = game_sim.get_state_data(tick)
		if local_state != state:
				game_sim.set_state_data(tick, state)
				game_sim.load_state(tick)
				var current := tick
				while current < local_tick:
						if input_history.has(current):
								game_sim.tick_gamesim(input_history[current])
						current += 1

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
		last_received_tick.clear()
		last_ack_tick = -1
		last_broadcast_inputs.clear()
