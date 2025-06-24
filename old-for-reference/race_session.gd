extends Node

var point_totals : PackedInt32Array = []
var peer_ids : PackedInt32Array = []
var current_track : int = 0

signal race_session_updated

@rpc("authority", "call_local", "reliable")
func sync_race_session(in_data : PackedByteArray) -> void:
	deserialize(in_data)
	race_session_updated.emit()

func deserialize(in_data : PackedByteArray) -> void:
	var buffer := StreamPeerBuffer.new()
	buffer.data_array = in_data
	buffer.seek(0)
	current_track = buffer.get_u8()
	point_totals.resize(Net.peers.size())
	peer_ids.resize(Net.peers.size())
	for i in Net.peers.size():
		point_totals[i] = buffer.get_u16()
		peer_ids[i] = buffer.get_u32()

func serialize() -> PackedByteArray:
	var buffer := StreamPeerBuffer.new()
	buffer.put_u8(current_track)
	for i in point_totals.size():
		buffer.put_u16(point_totals[i])
		buffer.put_u32(Net.peers[i].id)
	return buffer.data_array
