class_name NetworkManager
extends Node

const PlayerInputClass = preload("res://player/player_input.gd")
const NEUTRAL_INPUT = PlayerInputClass.new().to_dict()

var is_server: bool = false
var player_ids: Array = []
var pending_inputs := {}
var authoritative_inputs := {}
var last_local_input := NEUTRAL_INPUT.duplicate()
var server_tick: int = 0
var local_tick: int = 0
var input_redundancy := ProjectSettings.get_setting("rollback/input_redundancy", 5)

func host(port: int = 3456, max_players: int = 4) -> int:
    var peer := ENetMultiplayerPeer.new()
    var err := peer.create_server(port, max_players)
    if err != OK:
        push_error("Failed to host: %s" % err)
        return err
    multiplayer.multiplayer_peer = peer
    is_server = true
    server_tick = 0
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

@rpc(any_peer)
func _update_player_ids(ids: Array) -> void:
    player_ids = ids

func set_local_input(input: Dictionary) -> void:
    last_local_input = input

func collect_inputs() -> Array:
    if is_server:
        if not pending_inputs.has(server_tick):
            pending_inputs[server_tick] = {}
        pending_inputs[server_tick][multiplayer.get_unique_id()] = last_local_input
        var frame_inputs: Array = []
        var dict := pending_inputs[server_tick]
        for id in player_ids:
            if dict.has(id):
                frame_inputs.append(dict[id])
            else:
                frame_inputs.append(NEUTRAL_INPUT)
        pending_inputs.erase(server_tick)
        rpc_unreliable("_server_broadcast", server_tick, frame_inputs, player_ids)
        server_tick += 1
        return frame_inputs
    else:
        for i in input_redundancy:
            rpc_unreliable_id(1, "_client_send_input", local_tick + i, last_local_input)
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
        local_tick += 1
        return frame_inputs

@rpc(any_peer, transfer_mode=MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
func _client_send_input(tick: int, input: Dictionary) -> void:
    if is_server:
        if not pending_inputs.has(tick):
            pending_inputs[tick] = {}
        pending_inputs[tick][multiplayer.get_remote_sender_id()] = input

@rpc(any_peer, transfer_mode=MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
func _server_broadcast(tick: int, inputs: Array, ids: Array) -> void:
    if not is_server:
        server_tick = max(server_tick, tick + 1)
        player_ids = ids
        authoritative_inputs[tick] = inputs
