class_name PeerData extends Node

enum Status {
	Disconnected,
	Idle,
	Busy,
	Loaded,
	Ready,
}

var id := -1
var playerID := -1
var team := 0
var status := Status.Disconnected
var player_settings : PlayerSettings
var ping := 0

func do_ping() -> void:
	send_ping.rpc( Time.get_ticks_usec() )

@rpc("any_peer", "call_remote", "unreliable")
func send_ping(inTime : int) -> void:
	receive_ping.rpc_id( multiplayer.get_remote_sender_id(), inTime )

@rpc("any_peer", "call_local", "unreliable")
func receive_ping(inTime : int) -> void:
	# This ping is Host -> Client -> Host time
	#print("who")
	ping = int(lerpf( ping, Time.get_ticks_usec() - inTime, 0.1 ))
	sync_ping.rpc(ping)

@rpc("any_peer", "call_remote", "unreliable")
func sync_ping(in_ping : int) -> void:
	#print("what")
	ping = in_ping

func _to_string() -> String:
	return "%d:%d" % [id, playerID]

func _init( p_id := id, p_playerID := playerID ) -> void:
	id = p_id
	playerID = p_playerID

@rpc("any_peer","call_local","reliable")
func change_status( newStatus:Status ) -> void:
	status = newStatus
	if multiplayer.is_server():
		status_changed.rpc( newStatus )

@rpc("any_peer","call_remote","reliable")
func status_changed( newStatus:Status ) -> void:
	status = newStatus

@rpc("any_peer","call_local","reliable")
func settings_changed( in_data : PackedByteArray ) -> void:
	broadcast_settings.rpc(in_data)

@rpc("any_peer", "call_local", "reliable")
func broadcast_settings( in_data : PackedByteArray ) -> void:
	player_settings = PlayerSettings.deserialize(in_data)

@rpc("any_peer","call_local","reliable")
func set_team( in_int : int ) -> void:
	if multiplayer.get_remote_sender_id() == 1 or multiplayer.get_remote_sender_id() == id:
		broadcast_team.rpc(in_int)

@rpc("any_peer", "call_local", "reliable")
func broadcast_team( in_int : int ) -> void:
	team = in_int
