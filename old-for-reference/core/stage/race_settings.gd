class_name RaceSettings extends RefCounted

enum GameMode {
	SINGLE,
	GRANDPRIX
}

var gamemode_type := GameMode.SINGLE
var tracks : Array[String] = []
var input_delay := 2
var laps := 3
var restore := true
var bumpers := false
var recharge_on := true

func serialize() -> PackedByteArray:
	var buffer := StreamPeerBuffer.new()
	buffer.put_u8(gamemode_type)
	buffer.put_u8(tracks.size())
	for i in tracks.size():
		buffer.put_u8(tracks[i].length())
		buffer.put_data(tracks[i].to_utf8_buffer())
	buffer.put_u8(input_delay)
	buffer.put_u8(laps)
	var byteData := 0
	if restore:
		byteData |= 1
	if bumpers:
		byteData |= 2
	if recharge_on:
		byteData |= 4
	buffer.put_u8(byteData)
	buffer.resize(buffer.get_position())
	return buffer.data_array

static func deserialize(in_data : PackedByteArray) -> RaceSettings:
	var data_buffer := StreamPeerBuffer.new()
	data_buffer.data_array = in_data
	data_buffer.seek(0)
	var new_settings := RaceSettings.new()
	new_settings.gamemode_type = data_buffer.get_u8() as GameMode
	var num_tracks := data_buffer.get_u8()
	for i in num_tracks:
		var track_string_length := data_buffer.get_u8()
		new_settings.tracks.append(data_buffer.get_utf8_string(track_string_length))
	new_settings.input_delay = data_buffer.get_u8()
	new_settings.laps = data_buffer.get_u8()
	var byte_data := data_buffer.get_u8()
	new_settings.restore = byte_data & 1
	new_settings.bumpers = byte_data & 2
	new_settings.recharge_on = byte_data & 4
	return new_settings
