extends SceneTree

class MockSpatialAudio extends Node:
	var stopped_music: Array[float] = []
	var cleared_announcer_count := 0
	var vehicle_stream_count := 0

	func stop_music(fade_seconds: float = 0.0) -> void:
		stopped_music.append(fade_seconds)

	func clear_announcer_queue() -> void:
		cleared_announcer_count += 1

	func set_vehicle_manual_boost_stream(_car_index: int, _stream: AudioStream) -> void:
		vehicle_stream_count += 1

func _fail(message: String) -> void:
	push_error("MXT_RACE_AUDIO_CONTROLLER_SMOKE_FAIL " + message)
	quit(1)

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
			child.queue_free()
	var audio := game_manager.race_audio_controller as RaceAudioController
	var mock := MockSpatialAudio.new()
	audio.add_child(mock)
	audio.spatial_audio = mock
	audio.configure_track_music("res://content/base/music", {
		"music": {
			"loop": "raceresults_loop.ogg",
			"intro": "raceresults_intro.ogg",
			"wait_for_race_start": true,
		},
	})
	if !audio.race_audio_waiting_music_start or !audio.race_audio_pending_music_wait_for_race_start:
		_fail("track music was not queued")
		return
	if String(audio.race_audio_pending_music.get("loop", "")) != "res://content/base/music/raceresults_loop.ogg":
		_fail("track music path was not resolved")
		return
	var bus_index := AudioServer.get_bus_index(audio.RACE_FINISH_SFX_DUCK_BUS)
	if bus_index < 0:
		_fail("SFX bus is missing")
		return
	var original_bus_db := AudioServer.get_bus_volume_db(bus_index)
	audio._begin_race_finish_sfx_duck()
	audio.update(audio.RACE_FINISH_SFX_DUCK_FADE_SECONDS * 0.5)
	if !audio.race_finish_sfx_duck_active or !is_equal_approx(audio.race_finish_sfx_duck_current_db, audio.RACE_FINISH_SFX_DUCK_DB * 0.5):
		AudioServer.set_bus_volume_db(bus_index, original_bus_db)
		_fail("finish duck did not reach its midpoint")
		return
	audio.leave_race(0.5)
	audio.update(audio.RACE_FINISH_SFX_DUCK_FADE_SECONDS)
	if audio.race_finish_sfx_duck_active or !is_equal_approx(AudioServer.get_bus_volume_db(bus_index), original_bus_db):
		AudioServer.set_bus_volume_db(bus_index, original_bus_db)
		_fail("finish duck did not restore the SFX bus")
		return
	if mock.stopped_music != [0.5]:
		_fail("leave-race music fade did not reach native audio")
		return
	audio.reset_for_race()
	if mock.cleared_announcer_count != 1 or mock.stopped_music.size() != 2:
		_fail("race reset did not clear native audio state")
		return
	AudioServer.set_bus_volume_db(bus_index, original_bus_db)
	print("MXT_RACE_AUDIO_CONTROLLER_SMOKE_OK pending_loop=",
		audio.race_audio_pending_music.get("loop", ""),
		" stop_calls=", mock.stopped_music.size())
	game_manager.queue_free()
	await process_frame
	quit(0)
