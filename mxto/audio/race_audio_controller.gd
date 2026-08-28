class_name RaceAudioController extends Node

@onready var game_manager: GameManager = get_parent() as GameManager

const SPATIAL_AUDIO_SFX := {
	&"active_start": "res://sfx/vehicle/active_start.wav",
	&"air_0": "res://sfx/vehicle/air_0.wav",
	&"air_1": "res://sfx/vehicle/air_1.wav",
	&"brake": "res://sfx/vehicle/brake.wav",
	&"collision_light": "res://sfx/vehicle/collision_light.wav",
	&"collision_light_secondary": "res://sfx/vehicle/collision/PACK1-152.wav",
	&"collision_medium": "res://sfx/vehicle/collision_medium.wav",
	&"collision_hard": "res://sfx/vehicle/collision_hard.wav",
	&"collision_heavy": "res://sfx/vehicle/collision_heavy.wav",
	&"dash_plate": "res://sfx/vehicle/dash_plate.wav",
	&"dash_plate_secondary": "res://sfx/vehicle/boost/PACK1-245.wav",
	&"energy_restore": "res://sfx/vehicle/energy_restore.wav",
	&"gx_engine_185": "res://sfx/vehicle/thrust/PACK1-185.wav",
	&"gx_engine_186": "res://sfx/vehicle/thrust/PACK1-186.wav",
	&"gx_engine_187": "res://sfx/vehicle/thrust/PACK1-187.wav",
	&"gx_engine_188": "res://sfx/vehicle/thrust/PACK1-188.wav",
	&"gx_engine_189": "res://sfx/vehicle/thrust/PACK1-189.wav",
	&"gx_engine_190": "res://sfx/vehicle/thrust/PACK1-190.wav",
	&"gx_engine_236": "res://sfx/vehicle/thrust/PACK1-236.wav",
	&"gx_engine_237": "res://sfx/vehicle/thrust/PACK1-237.wav",
	&"gx_engine_240": "res://sfx/vehicle/thrust/PACK1-240.wav",
	&"gx_engine_250": "res://sfx/vehicle/thrust/PACK1-250.wav",
	&"gx_engine_251": "res://sfx/vehicle/thrust/PACK1-251.wav",
	&"jump_plate": "res://sfx/vehicle/boost/PACK1-242.wav",
	&"landing": "res://sfx/vehicle/landing.wav",
	&"landing_b10": "res://sfx/vehicle/landing_b10.wav",
	&"spinattack": "res://sfx/vehicle/spinattack.wav",
	&"mine": "res://sfx/vehicle/mine.wav",
	&"sideattack": "res://sfx/vehicle/sideattack.wav",
	&"strafe": "res://sfx/vehicle/strafe.wav",
	&"suspension_contact": "res://sfx/vehicle/suspension_contact.wav",
	&"drift_loop": "res://sfx/vehicle/thrust/PACK1-191.wav",
	&"terrain_dirt": "res://sfx/vehicle/terrain_dirt.wav",
	&"terrain_lava": "res://sfx/vehicle/thrust/PACK1-197.wav",
	&"terrain_lava_secondary": "res://sfx/vehicle/thrust/PACK1-196.wav",
	&"thrust_on": "res://sfx/vehicle/thrust_on.wav",
	&"zero_hp": "res://sfx/vehicle/zero_hp.wav",
	&"announcer_boost_power": "res://sfx/announcer/boost_power.wav",
	&"announcer_final_lap": "res://sfx/announcer/final_lap.wav",
	&"announcer_go": "res://sfx/announcer/go.wav",
	&"announcer_one": "res://sfx/announcer/one.wav",
	&"announcer_three": "res://sfx/announcer/three.wav",
	&"announcer_two": "res://sfx/announcer/two.wav",
}
const RACE_BOOST_POWER_LAP_INDEX := 2
const RACE_MUSIC_START_LEAD_TICKS := 360
const RACE_FINISH_MUSIC_FADE_SECONDS := 0.5
const RACE_RESULTS_MUSIC_DELAY_SECONDS := 2.0
const RACE_RESULTS_MUSIC_INTRO := "res://content/base/music/raceresults_intro.ogg"
const RACE_RESULTS_MUSIC_LOOP := "res://content/base/music/raceresults_loop.ogg"
const RACE_FINISH_WHOOSH_STREAM := preload("res://sfx/whoosh.wav")
const DEFAULT_VEHICLE_BOOST_STREAM := preload("res://sfx/vehicle/boost/PACK1-110.wav")
const RACE_FINISH_SFX_DUCK_BUS := &"SFX"
const RACE_FINISH_SFX_DUCK_DB := -8.0
const RACE_FINISH_SFX_DUCK_DELAY_SECONDS := 1.0
const RACE_FINISH_SFX_DUCK_FADE_SECONDS := 2.0

var spatial_audio: Node
var ui_sfx_player: AudioStreamPlayer
var race_audio_last_tick: int = -1
var race_audio_last_local_lap: int = -1
var race_audio_boost_power_announced: bool = false
var race_audio_final_lap_requested: bool = false
var race_audio_waiting_music_start: bool = false
var race_audio_pending_music_wait_for_race_start: bool = false
var race_audio_pending_music: Dictionary = {}
var race_finish_audio_started: bool = false
var race_finish_audio_generation: int = 0
var race_finish_sfx_duck_active: bool = false
var race_finish_sfx_duck_releasing: bool = false
var race_finish_sfx_duck_current_db: float = 0.0
var race_finish_sfx_duck_target_db: float = 0.0
var race_finish_sfx_duck_base_volume_db: float = 0.0

func initialize() -> void:
	if DisplayServer.get_name() == "headless":
		return
	if spatial_audio == null and ClassDB.class_exists("MxtSpatialAudioManager"):
		var audio_node := ClassDB.instantiate("MxtSpatialAudioManager") as Node
		if audio_node != null:
			audio_node.name = "SpatialAudioManager"
			game_manager.get_node("GameWorld").add_child(audio_node)
			spatial_audio = audio_node
			spatial_audio.call("configure", 30, 16, 16, 8)
			spatial_audio.call("set_audio_bus", &"SFX")
			spatial_audio.call("set_remote_vehicle_audio_bus", &"RemoteVehicleSFX")
			spatial_audio.call("set_music_bus", &"Music")
			spatial_audio.call("set_announcer_bus", &"Announcer")
			spatial_audio.call("set_reassignment_fade_seconds", 0.05)
			spatial_audio.call("set_max_announcer_queue", 16)
			for sfx_id in SPATIAL_AUDIO_SFX.keys():
				spatial_audio.call("register_sfx", sfx_id, SPATIAL_AUDIO_SFX[sfx_id])
			if game_manager.game_sim != null and game_manager.game_sim.has_method("set_spatial_audio_manager"):
				game_manager.game_sim.call("set_spatial_audio_manager", spatial_audio)
	_ensure_ui_sfx_player()

func _ensure_ui_sfx_player() -> void:
	if DisplayServer.get_name() == "headless" or ui_sfx_player != null:
		return
	ui_sfx_player = AudioStreamPlayer.new()
	ui_sfx_player.name = "UiSfxPlayer"
	ui_sfx_player.bus = &"SFX"
	add_child(ui_sfx_player)

func configure_vehicle_properties(definitions: Array) -> void:
	if spatial_audio == null or !spatial_audio.has_method("set_vehicle_manual_boost_stream"):
		return
	for i in definitions.size():
		var definition := definitions[i] as CarDefinition
		var boost_sfx: AudioStream = null
		if definition != null:
			boost_sfx = definition.manual_boost_sfx
		if boost_sfx == null:
			boost_sfx = DEFAULT_VEHICLE_BOOST_STREAM
		var boost_volume_db := definition.manual_boost_volume_db if definition != null else 0.0
		spatial_audio.call("set_vehicle_manual_boost_stream", i, boost_sfx, boost_volume_db)

func _play_ui_sfx(stream: AudioStream, volume_db: float = 0.0, pitch_scale: float = 1.0) -> void:
	if stream == null or DisplayServer.get_name() == "headless":
		return
	if ui_sfx_player == null:
		_ensure_ui_sfx_player()
	if ui_sfx_player == null:
		return
	ui_sfx_player.stream = stream
	ui_sfx_player.volume_db = volume_db
	ui_sfx_player.pitch_scale = pitch_scale
	ui_sfx_player.play()

func _apply_race_finish_sfx_duck() -> void:
	var bus_index := AudioServer.get_bus_index(RACE_FINISH_SFX_DUCK_BUS)
	if bus_index < 0:
		return
	AudioServer.set_bus_volume_db(bus_index, race_finish_sfx_duck_base_volume_db + race_finish_sfx_duck_current_db)

func _begin_race_finish_sfx_duck() -> void:
	var bus_index := AudioServer.get_bus_index(RACE_FINISH_SFX_DUCK_BUS)
	if bus_index < 0:
		return
	if !race_finish_sfx_duck_active:
		race_finish_sfx_duck_current_db = 0.0
		race_finish_sfx_duck_base_volume_db = AudioServer.get_bus_volume_db(bus_index)
	else:
		race_finish_sfx_duck_base_volume_db = AudioServer.get_bus_volume_db(bus_index) - race_finish_sfx_duck_current_db
	race_finish_sfx_duck_target_db = RACE_FINISH_SFX_DUCK_DB
	race_finish_sfx_duck_active = true
	race_finish_sfx_duck_releasing = false
	_apply_race_finish_sfx_duck()

func _release_race_finish_sfx_duck() -> void:
	if !race_finish_sfx_duck_active:
		return
	race_finish_sfx_duck_target_db = 0.0
	race_finish_sfx_duck_releasing = true

func update(delta: float) -> void:
	if !race_finish_sfx_duck_active:
		return
	var step := 0.0
	if RACE_FINISH_SFX_DUCK_FADE_SECONDS > 0.0:
		step = absf(RACE_FINISH_SFX_DUCK_DB) * maxf(delta, 0.0) / RACE_FINISH_SFX_DUCK_FADE_SECONDS
	else:
		step = absf(race_finish_sfx_duck_target_db - race_finish_sfx_duck_current_db)
	race_finish_sfx_duck_current_db = move_toward(race_finish_sfx_duck_current_db, race_finish_sfx_duck_target_db, step)
	_apply_race_finish_sfx_duck()
	if race_finish_sfx_duck_releasing and is_equal_approx(race_finish_sfx_duck_current_db, 0.0):
		var bus_index := AudioServer.get_bus_index(RACE_FINISH_SFX_DUCK_BUS)
		if bus_index >= 0:
			AudioServer.set_bus_volume_db(bus_index, race_finish_sfx_duck_base_volume_db)
		race_finish_sfx_duck_active = false
		race_finish_sfx_duck_releasing = false
		race_finish_sfx_duck_current_db = 0.0
		race_finish_sfx_duck_target_db = 0.0

func _cancel_race_finish_audio(stop_ui_sfx: bool = false) -> void:
	race_finish_audio_generation += 1
	race_finish_audio_started = false
	_release_race_finish_sfx_duck()
	if stop_ui_sfx and ui_sfx_player != null:
		ui_sfx_player.stop()

func leave_race(music_fade_seconds: float = -1.0) -> void:
	_cancel_race_finish_audio(true)
	if music_fade_seconds >= 0.0 and spatial_audio != null and spatial_audio.has_method("stop_music"):
		spatial_audio.call("stop_music", music_fade_seconds)

func begin_local_finish() -> void:
	if race_finish_audio_started or game_manager.replay_controller.replay_playback_active:
		return
	race_finish_audio_started = true
	var generation := race_finish_audio_generation
	_play_ui_sfx(RACE_FINISH_WHOOSH_STREAM, 10.0)
	_begin_race_finish_sfx_duck_after_delay(generation)
	if spatial_audio != null and spatial_audio.has_method("stop_music"):
		spatial_audio.call("stop_music", RACE_FINISH_MUSIC_FADE_SECONDS)
	_play_race_results_music_after_delay(generation)

func _begin_race_finish_sfx_duck_after_delay(generation: int) -> void:
	if RACE_FINISH_SFX_DUCK_DELAY_SECONDS > 0.0:
		await get_tree().create_timer(RACE_FINISH_SFX_DUCK_DELAY_SECONDS).timeout
	if generation != race_finish_audio_generation or !race_finish_audio_started:
		return
	if game_manager.replay_controller.replay_playback_active or game_manager.game_sim == null or !game_manager.game_sim.sim_started:
		return
	_begin_race_finish_sfx_duck()

func _play_race_results_music_after_delay(generation: int) -> void:
	await get_tree().create_timer(RACE_RESULTS_MUSIC_DELAY_SECONDS).timeout
	if generation != race_finish_audio_generation or !race_finish_audio_started:
		return
	if game_manager.replay_controller.replay_playback_active or game_manager.game_sim == null or !game_manager.game_sim.sim_started:
		return
	_play_music_from_definition({
		"loop": RACE_RESULTS_MUSIC_LOOP,
		"intro": RACE_RESULTS_MUSIC_INTRO,
		"final_loop": "",
		"final_intro": "",
		"final_lap_timestamps": [],
	})

func reset_for_race() -> void:
	_cancel_race_finish_audio(true)
	race_audio_last_tick = -1
	race_audio_last_local_lap = -1
	race_audio_boost_power_announced = _boost_unlocked_from_start()
	race_audio_final_lap_requested = false
	race_audio_waiting_music_start = false
	race_audio_pending_music_wait_for_race_start = false
	race_audio_pending_music.clear()
	if spatial_audio != null:
		if spatial_audio.has_method("clear_announcer_queue"):
			spatial_audio.call("clear_announcer_queue")
		if spatial_audio.has_method("stop_music"):
			spatial_audio.call("stop_music")


func reconcile_practice_state_restore(next_tick: int, lap: int, finished: bool) -> void:
	race_audio_last_tick = next_tick - 1
	race_audio_last_local_lap = lap
	race_audio_boost_power_announced = _boost_unlocked_from_start() or lap >= RACE_BOOST_POWER_LAP_INDEX
	var final_lap := _configured_final_lap()
	race_audio_final_lap_requested = final_lap <= 0 or lap >= final_lap
	if !finished:
		_cancel_race_finish_audio(true)
	if spatial_audio != null and spatial_audio.has_method("clear_announcer_queue"):
		spatial_audio.call("clear_announcer_queue")


func _boost_unlocked_from_start() -> bool:
	return game_manager != null \
		and game_manager.network_manager != null \
		and game_manager.network_manager.race_configuration.is_practice() \
		and game_manager.network_manager.race_configuration.boost_unlocked_from_start


func _configured_final_lap() -> int:
	if game_manager == null or game_manager.network_manager == null:
		return 3
	return game_manager.network_manager.race_configuration.lap_count


func _resolve_track_audio_path(track_dir: String, path_value) -> String:
	var path := str(path_value)
	if path.is_empty():
		return ""
	if path.begins_with("res://") or path.begins_with("user://"):
		return path
	return track_dir.path_join(path)

func _play_music_from_definition(definition: Dictionary) -> bool:
	if spatial_audio == null or !spatial_audio.has_method("play_music_paths"):
		return false
	var timestamps := PackedFloat32Array()
	var raw_timestamps = definition.get("final_lap_timestamps", [])
	if typeof(raw_timestamps) == TYPE_PACKED_FLOAT32_ARRAY:
		timestamps = raw_timestamps
	elif typeof(raw_timestamps) == TYPE_ARRAY:
		timestamps = PackedFloat32Array(raw_timestamps)
	return bool(spatial_audio.call(
		"play_music_paths",
		str(definition.get("loop", "")),
		str(definition.get("intro", "")),
		str(definition.get("final_loop", "")),
		str(definition.get("final_intro", "")),
		timestamps))

func _audio_stream_resource_path(stream_value) -> String:
	if stream_value is Resource:
		return str(stream_value.resource_path)
	return ""

func _music_definition_from_resource(resource_path: String) -> Dictionary:
	if resource_path.is_empty() or !ResourceLoader.exists(resource_path):
		return {}
	var music_res := load(resource_path)
	if music_res == null:
		return {}
	return {
		"loop": _audio_stream_resource_path(music_res.get("musicLoop")),
		"intro": _audio_stream_resource_path(music_res.get("musicIntro")),
		"final_loop": _audio_stream_resource_path(music_res.get("musicLoopFinalLap")),
		"final_intro": _audio_stream_resource_path(music_res.get("musicIntroFinalLap")),
		"final_lap_timestamps": music_res.get("finalLapTimeStamps"),
		"wait_for_race_start": bool(music_res.get("wait_for_race_start")),
	}

func configure_track_music(track_dir: String, track_metadata: Dictionary) -> void:
	race_audio_waiting_music_start = false
	race_audio_pending_music.clear()
	if spatial_audio == null:
		return
	var music_def = track_metadata.get("song", track_metadata.get("music", {}))
	if typeof(music_def) == TYPE_STRING:
		music_def = _resolve_track_audio_path(track_dir, music_def)
	if typeof(music_def) == TYPE_STRING_NAME:
		music_def = _resolve_track_audio_path(track_dir, str(music_def))
	if typeof(music_def) == TYPE_STRING and !str(music_def).is_empty():
		music_def = _music_definition_from_resource(str(music_def))
	elif track_metadata.has("music_resource"):
		music_def = _music_definition_from_resource(_resolve_track_audio_path(track_dir, track_metadata.get("music_resource", "")))
	if typeof(music_def) != TYPE_DICTIONARY:
		if spatial_audio.has_method("stop_music"):
			spatial_audio.call("stop_music")
		return
	var music: Dictionary = music_def
	if music.has("resource"):
		var resource_def := _music_definition_from_resource(_resolve_track_audio_path(track_dir, music.get("resource", "")))
		if resource_def.is_empty():
			if spatial_audio.has_method("stop_music"):
				spatial_audio.call("stop_music")
			return
		music = resource_def
	var loop_path := _resolve_track_audio_path(track_dir, music.get("loop", ""))
	if loop_path.is_empty():
		if spatial_audio.has_method("stop_music"):
			spatial_audio.call("stop_music")
		return
	var native_def := {
		"loop": loop_path,
		"intro": _resolve_track_audio_path(track_dir, music.get("intro", "")),
		"final_loop": _resolve_track_audio_path(track_dir, music.get("final_loop", "")),
		"final_intro": _resolve_track_audio_path(track_dir, music.get("final_intro", "")),
		"final_lap_timestamps": music.get("final_lap_timestamps", []),
	}
	race_audio_pending_music = native_def
	race_audio_pending_music_wait_for_race_start = bool(music.get("wait_for_race_start", false))
	race_audio_waiting_music_start = true

func _race_audio_focus_player_id() -> int:
	var local_id := game_manager._local_player_id()
	if local_id != 0 and game_manager.network_manager.get_simulation_roster().has(local_id):
		return local_id
	var roster := game_manager.network_manager.get_simulation_roster()
	if !roster.is_empty():
		return int(roster[0])
	return local_id

func after_simulation_tick() -> void:
	if spatial_audio == null or game_manager.replay_controller.replay_playback_active or game_manager.game_sim == null or !game_manager.game_sim.sim_started:
		return
	var player_id := _race_audio_focus_player_id()
	var current_tick := game_manager.network_manager.input_transport.get_race_tick()
	if current_tick < 0:
		return
	var previous_tick := race_audio_last_tick
	race_audio_last_tick = current_tick
	if game_manager.game_sim.has_method("get_player_level_start_time"):
		var start_tick := int(game_manager.game_sim.call("get_player_level_start_time", player_id))
		if race_audio_waiting_music_start:
			var music_start_tick := start_tick
			if !race_audio_pending_music_wait_for_race_start:
				music_start_tick = maxi(0, start_tick - RACE_MUSIC_START_LEAD_TICKS)
			if previous_tick < music_start_tick and current_tick >= music_start_tick:
				race_audio_waiting_music_start = false
				race_audio_pending_music_wait_for_race_start = false
				_play_music_from_definition(race_audio_pending_music)
				race_audio_pending_music.clear()
		var countdown_marks := [
			[start_tick - 180, &"announcer_three"],
			[start_tick - 120, &"announcer_two"],
			[start_tick - 60, &"announcer_one"],
			[start_tick, &"announcer_go"],
		]
		for mark in countdown_marks:
			var mark_tick := int(mark[0])
			if previous_tick < mark_tick and current_tick >= mark_tick and spatial_audio.has_method("queue_announcer"):
				spatial_audio.call("queue_announcer", mark[1], 0.0, 1.0)
	if game_manager.game_sim.has_method("get_player_lap"):
		var lap := int(game_manager.game_sim.call("get_player_lap", player_id))
		var final_lap := _configured_final_lap()
		if race_audio_last_local_lap >= 0:
			if !race_audio_boost_power_announced and race_audio_last_local_lap < RACE_BOOST_POWER_LAP_INDEX and lap >= RACE_BOOST_POWER_LAP_INDEX:
				race_audio_boost_power_announced = true
				if spatial_audio.has_method("queue_announcer"):
					spatial_audio.call("queue_announcer", &"announcer_boost_power", 0.0, 1.0)
		if final_lap > 0 and !race_audio_final_lap_requested \
				and race_audio_last_local_lap < final_lap and lap >= final_lap:
			race_audio_final_lap_requested = true
			if spatial_audio.has_method("queue_announcer"):
				spatial_audio.call("queue_announcer", &"announcer_final_lap", 0.0, 1.0)
			if spatial_audio.has_method("request_final_lap_music"):
				spatial_audio.call("request_final_lap_music")
		race_audio_last_local_lap = lap
