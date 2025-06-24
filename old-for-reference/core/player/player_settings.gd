class_name PlayerSettings extends Resource

var version := "gx0.1"
var camera_height := 1.0
var camera_distance := 3.2
var camera_pitch_offset := -15.0
var volume := 1.0
var volume_music := 1.0
var volume_sfx: = 1.0
var volume_voice := 1.0
var accel_setting := 1.0
var username := "Baller"
var car_choice := "CAR_ALLROUNDER"

var sticker_1 := 0
var sticker_2 := 1
var sticker_3 := 2
var sticker_4 := 3

var base_color := Color.WHITE
var secondary_color := Color.WHITE
var tertiary_color := Color.WHITE

func serialize() -> PackedByteArray:
	var save := StreamPeerBuffer.new()
	save.put_u8(version.length())
	save.put_data(version.to_utf8_buffer())
	save.put_float(camera_height)
	save.put_float(camera_distance)
	save.put_float(camera_pitch_offset)
	save.put_u8(username.length())
	save.put_data(username.to_utf8_buffer())
	save.put_u8(car_choice.length())
	save.put_data(car_choice.to_utf8_buffer())
	save.put_float(volume)
	save.put_float(volume_music)
	save.put_float(volume_sfx)
	save.put_float(volume_voice)
	save.put_u8(sticker_1)
	save.put_u8(sticker_2)
	save.put_u8(sticker_3)
	save.put_u8(sticker_4)
	save.put_float(base_color.r)
	save.put_float(base_color.g)
	save.put_float(base_color.b)
	save.put_float(secondary_color.r)
	save.put_float(secondary_color.g)
	save.put_float(secondary_color.b)
	save.put_float(tertiary_color.r)
	save.put_float(tertiary_color.g)
	save.put_float(tertiary_color.b)
	save.put_float(accel_setting)
	return save.data_array

static func deserialize(in_data : PackedByteArray) -> PlayerSettings:
	var new_player_settings := PlayerSettings.new()
	var save := StreamPeerBuffer.new()
	save.data_array = in_data
	var version_string_length := save.get_u8()
	var version_string := save.get_utf8_string(version_string_length)
	match version_string:
		"0.1":
			new_player_settings.camera_height = save.get_float()
			new_player_settings.camera_distance = save.get_float()
			new_player_settings.camera_pitch_offset = save.get_float()
			new_player_settings.username = save.get_utf8_string(save.get_u8())
			new_player_settings.car_choice = save.get_utf8_string(save.get_u8())
			new_player_settings.volume = save.get_float()
			new_player_settings.volume_music = save.get_float()
			new_player_settings.volume_sfx = save.get_float()
			new_player_settings.volume_voice = save.get_float()
			new_player_settings.sticker_1 = 0
			new_player_settings.sticker_2 = 1
			new_player_settings.sticker_3 = 2
			new_player_settings.sticker_4 = 3
			new_player_settings.base_color = Color.WHITE
			new_player_settings.secondary_color = Color.WHITE
			new_player_settings.tertiary_color = Color.WHITE
			new_player_settings.accel_setting = 0.5
		"0.2":
			new_player_settings.camera_height = save.get_float()
			new_player_settings.camera_distance = save.get_float()
			new_player_settings.camera_pitch_offset = save.get_float()
			new_player_settings.username = save.get_utf8_string(save.get_u8())
			new_player_settings.car_choice = save.get_utf8_string(save.get_u8())
			new_player_settings.volume = save.get_float()
			new_player_settings.volume_music = save.get_float()
			new_player_settings.volume_sfx = save.get_float()
			new_player_settings.volume_voice = save.get_float()
			new_player_settings.sticker_1 = save.get_u8()
			new_player_settings.sticker_2 = save.get_u8()
			new_player_settings.sticker_3 = save.get_u8()
			new_player_settings.sticker_4 = save.get_u8()
			new_player_settings.base_color = Color.WHITE
			new_player_settings.secondary_color = Color.WHITE
			new_player_settings.tertiary_color = Color.WHITE
			new_player_settings.accel_setting = 0.5
		"0.3":
			new_player_settings.camera_height = save.get_float()
			new_player_settings.camera_distance = save.get_float()
			new_player_settings.camera_pitch_offset = save.get_float()
			new_player_settings.username = save.get_utf8_string(save.get_u8())
			new_player_settings.car_choice = save.get_utf8_string(save.get_u8())
			new_player_settings.volume = save.get_float()
			new_player_settings.volume_music = save.get_float()
			new_player_settings.volume_sfx = save.get_float()
			new_player_settings.volume_voice = save.get_float()
			new_player_settings.sticker_1 = save.get_u8()
			new_player_settings.sticker_2 = save.get_u8()
			new_player_settings.sticker_3 = save.get_u8()
			new_player_settings.sticker_4 = save.get_u8()
			new_player_settings.base_color.r = save.get_float()
			new_player_settings.base_color.g = save.get_float()
			new_player_settings.base_color.b = save.get_float()
			new_player_settings.secondary_color.r = save.get_float()
			new_player_settings.secondary_color.g = save.get_float()
			new_player_settings.secondary_color.b = save.get_float()
			new_player_settings.tertiary_color.r = save.get_float()
			new_player_settings.tertiary_color.g = save.get_float()
			new_player_settings.tertiary_color.b = save.get_float()
			new_player_settings.accel_setting = 0.5
		"gx0.1":
			new_player_settings.camera_height = save.get_float()
			new_player_settings.camera_distance = save.get_float()
			new_player_settings.camera_pitch_offset = save.get_float()
			new_player_settings.username = save.get_utf8_string(save.get_u8())
			new_player_settings.car_choice = save.get_utf8_string(save.get_u8())
			new_player_settings.volume = save.get_float()
			new_player_settings.volume_music = save.get_float()
			new_player_settings.volume_sfx = save.get_float()
			new_player_settings.volume_voice = save.get_float()
			new_player_settings.sticker_1 = save.get_u8()
			new_player_settings.sticker_2 = save.get_u8()
			new_player_settings.sticker_3 = save.get_u8()
			new_player_settings.sticker_4 = save.get_u8()
			new_player_settings.base_color.r = save.get_float()
			new_player_settings.base_color.g = save.get_float()
			new_player_settings.base_color.b = save.get_float()
			new_player_settings.secondary_color.r = save.get_float()
			new_player_settings.secondary_color.g = save.get_float()
			new_player_settings.secondary_color.b = save.get_float()
			new_player_settings.tertiary_color.r = save.get_float()
			new_player_settings.tertiary_color.g = save.get_float()
			new_player_settings.tertiary_color.b = save.get_float()
			new_player_settings.accel_setting = save.get_float()
	return new_player_settings

static func _load_settings() -> PlayerSettings:
	var settingsExist : bool = FileAccess.file_exists("user://mxt_settings.dat")
	if !settingsExist:
		return PlayerSettings.new()
	var file : PackedByteArray = FileAccess.get_file_as_bytes("user://mxt_settings.dat")
	return PlayerSettings.deserialize(file)

func _save_settings() -> void:
	var fileData : PackedByteArray = serialize()
	var updatedFile : FileAccess = FileAccess.open("user://mxt_settings.dat", FileAccess.WRITE)
	updatedFile.store_buffer(fileData)
	updatedFile.flush()
