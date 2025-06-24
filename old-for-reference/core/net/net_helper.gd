extends Node

var nextPlayerID := 0
var id := -1:
	get:
		return multiplayer.get_unique_id()
var peer:PeerData:
	get:
		return peer_map.get(id)
var peers:Array[PeerData] = []
var peer_map := {}
var connected := false:
	get:
		if not multiplayer.has_multiplayer_peer(): return false
		return not multiplayer.get_peers().is_empty()

var players:Array[ROPlayer] = []
var lastPing : int = 0
var currentlyHosting : bool = false
var compressionMode := ENetConnection.COMPRESS_RANGE_CODER

func _ready() -> void:
	process_physics_priority = -10
	get_tree().multiplayer_poll = false
	multiplayer.server_relay = false
	multiplayer.connected_to_server.connect(_connected_to_server)
	multiplayer.server_disconnected.connect(_disconnected_from_server)
	multiplayer.peer_connected.connect(_peer_connected)
	multiplayer.peer_disconnected.connect(_peer_disconnected)
	
	if DisplayServer.get_name() == "headless":
		print("Automatically starting dedicated server.")
		host_server.call_deferred()
	
	if OS.is_debug_build() and MXGlobal.debug_multiplayer > 0:
		if not host_server( 27015 ):
			await get_tree().create_timer(0.5).timeout
			join_server( "127.0.0.1", 27015 )
			#get_window().mode = Window.MODE_MINIMIZED
		else:
			while true:
				if Net.peers.size() == MXGlobal.debug_multiplayer:
					if MXGlobal.debug_host_spec:
						Net.peer.set_team.rpc(1)
					MXGlobal.debug_multiplayer = 0
					get_window().set_flag(Window.FLAG_ALWAYS_ON_TOP,true)
					await get_tree().create_timer(1.0).timeout
					get_window().set_flag(Window.FLAG_ALWAYS_ON_TOP,false)
					for other_peer in Net.peers:
						other_peer.change_status( other_peer.Status.Busy )
						other_peer.change_status.rpc_id( other_peer.id, other_peer.Status.Busy )
					var new_settings : RaceSettings = RaceSettings.new()
					MXGlobal.current_race_settings = new_settings
					RaceSession.point_totals.clear()
					for p in Net.peers:
						if p.team == 0:
							RaceSession.point_totals.append(0)
					RaceSession.current_track = 0
					RaceSession.sync_race_session.rpc(RaceSession.serialize())
					MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())
					MXGlobal.load_stage_by_path.rpc( MXGlobal.debug_stage_path )
					break
				await get_tree().create_timer(0.1).timeout

func create_peer( p_peerID:int, playerID := nextPlayerID, team := 0 ) -> PeerData:
	var newPeer := PeerData.new( p_peerID, playerID )
	while (playerID == nextPlayerID):
		nextPlayerID += 1
	newPeer.name = "Peer" + str(p_peerID)
	newPeer.set_multiplayer_authority( p_peerID )
	newPeer.team = team
	peers.append(newPeer)
	peer_map[p_peerID] = newPeer
	add_child(newPeer)
	return newPeer

func destroy_peer( p_peerID:int ) -> void:
	if peer_map.has( p_peerID ):
		peer_map[p_peerID].queue_free()
		peers.erase(peer_map[p_peerID])
		peer_map.erase(p_peerID)

@rpc("any_peer","call_remote","reliable")
func sync_peers( peerIDs := PackedInt32Array(), playerIDs := PackedInt32Array(), teams := PackedInt32Array()) -> void:
	for i in peerIDs.size():
		var peerID := peerIDs[i]
		var playerID := playerIDs[i]
		var team := teams[i]
		if not peer_map.has( peerID ):
			create_peer( peerID, playerID, team )
		peer_map[peerID].playerID = playerID
	for peerID:int in peer_map:
		if not peerID in peerIDs:
			destroy_peer( peerID )

func _connected_to_server() -> void:
	var newPeer := create_peer(multiplayer.get_unique_id())
	newPeer.change_status.rpc(newPeer.Status.Idle)

func _disconnected_from_server() -> void:
	sync_peers()
	currentlyHosting = false
	close()

func close() -> void:
	for i in range(peers.size()-1,-1,-1):
		destroy_peer(i)
	if multiplayer.has_multiplayer_peer():
		multiplayer.multiplayer_peer.close()

func server_sync_peers() -> void:
	var peerIDs := PackedInt32Array()
	var playerIDs := PackedInt32Array()
	var teams := PackedInt32Array()
	for peerData in peers:
		peerIDs.append( peerData.id )
		playerIDs.append( peerData.playerID )
		teams.append( peerData.team )
	sync_peers.rpc( peerIDs, playerIDs, teams )
	request_player_settings.rpc()
	for this_peer in peers:
		this_peer.set_team.rpc(this_peer.team)

func _peer_connected( p_peerID:int ) -> void:
	if multiplayer.is_server():
		if peer == null:
			create_peer( 1 )
	create_peer( p_peerID )
	if multiplayer.is_server():
		server_sync_peers()

func _peer_disconnected( p_peerID:int ) -> void:
	destroy_peer(p_peerID)
	if multiplayer.is_server():
		server_sync_peers()

func _physics_process(_delta: float) -> void:
	multiplayer.poll()
	multiplayer.poll.call_deferred()

#send ping
@rpc("any_peer", "call_remote", "unreliable_ordered")
func send_ping(inTime : int) -> void:
	receive_ping.rpc_id(1, inTime)

#receive ping
@rpc("any_peer", "call_remote", "unreliable_ordered")
func receive_ping(inTime : int)  -> void:
	var p := Time.get_ticks_usec() - inTime
	var sender := multiplayer.get_remote_sender_id()
	peer_map[sender].ping = p
	sync_ping.rpc(sender, p)

@rpc("authority", "call_remote", "unreliable_ordered")
func sync_ping(sender : int, in_ping : int) -> void:
	peer_map[sender].ping = in_ping

@rpc("any_peer", "call_local", "reliable")
func request_player_settings() -> void:
	broadcast_player_settings.rpc_id(1, MXGlobal.local_settings.serialize())

@rpc("any_peer", "call_local", "reliable")
func receive_player_settings(byteData : PackedByteArray, peer_id : int) -> void:
	for desired_peer in peers:
		if desired_peer.get_multiplayer_authority() == peer_id:
			desired_peer.player_settings = PlayerSettings.deserialize(byteData)

@rpc("any_peer", "call_local", "reliable")
func broadcast_player_settings(byteData : PackedByteArray) -> void:
	receive_player_settings.rpc(byteData, multiplayer.get_remote_sender_id())
	
func _process(_delta : float) -> void:
	if connected and multiplayer.is_server() and Time.get_ticks_usec() > lastPing + 250000 and currentlyHosting:
		lastPing = Time.get_ticks_usec()
		send_ping.rpc(Time.get_ticks_usec())

func host_server( port:int ) -> bool:
	var enet_peer := ENetMultiplayerPeer.new()
	var error := enet_peer.create_server( port )
	if error != 0 or enet_peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
		print("Failed to start multiplayer server.")
		return false
	enet_peer.host.compress(compressionMode)
	multiplayer.multiplayer_peer = enet_peer
	currentlyHosting = true
	return true

func join_server( ip_address:String, port:int ) -> bool:
	if ip_address == "":
		print("Need a remote to connect to.")
		return false
	var enet_peer := ENetMultiplayerPeer.new()
	enet_peer.create_client( ip_address, port )
	if enet_peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
		print("Failed to start multiplayer client.")
		return false
	enet_peer.host.compress(compressionMode)
	multiplayer.multiplayer_peer = enet_peer
	#start_game()
	return true
