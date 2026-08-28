extends SceneTree

func _arg_value(name: String) -> String:
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	var index := args.find(name)
	var source := args
	if index == -1:
		index = user_args.find(name)
		source = user_args
	if index == -1 or index + 1 >= source.size():
		return ""
	return String(source[index + 1])

func _fail(message: String) -> void:
	push_error("MXT_REPLAY_CONTROLLER_SMOKE_FAIL " + message)
	quit(1)

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var replay_path := _arg_value("--replay-smoke")
	if replay_path == "" or !FileAccess.file_exists(replay_path):
		_fail("missing --replay-smoke path")
		return
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
	var replay := game_manager.replay_controller as ReplayController
	replay._start_replay_playback_from_path(replay_path)
	replay.replay_playback_paused = true
	if !replay.replay_playback_active or replay._playback_frame_count() <= 0:
		_fail("playback did not start")
		return
	var total_ticks := replay._playback_frame_count()
	var seek_tick := maxi(1, total_ticks / 2)
	if !replay._seek_replay_to_tick(seek_tick, false):
		_fail("seek failed")
		return
	if game_manager._singleplayer_tick != seek_tick or replay.replay_playback_index != seek_tick:
		_fail("seek position mismatch tick=%d index=%d expected=%d" % [
			game_manager._singleplayer_tick,
			replay.replay_playback_index,
			seek_tick,
		])
		return
	for camera_mode in [
		replay.REPLAY_CAMERA_GAME,
		replay.REPLAY_CAMERA_AUTO,
		replay.REPLAY_CAMERA_RELATIVE,
		replay.REPLAY_CAMERA_SPECTATOR,
	]:
		replay.replay_camera_mode = camera_mode
		replay._apply_replay_camera_mode()
		if game_manager.get_viewport().get_camera_3d() == null:
			_fail("camera mode %d did not select a camera" % camera_mode)
			return
	var catalog := game_manager.replay_catalog_controller as ReplayCatalogController
	catalog._build()
	catalog.refresh()
	if catalog.root == null or catalog.replay_list == null:
		_fail("catalog UI was not constructed")
		return
	if catalog.entries.is_empty():
		_fail("catalog did not discover replay metadata")
		return
	var recorder := game_manager.replay_recorder as ReplayRecorder
	recorder.stream = MxtReplayStream.new()
	recorder.stream.begin_recording([77], [false])
	recorder.active = true
	recorder.saved = false
	var frame_input := PackedByteArray([0])
	if !recorder.stream.append_frame_inputs(0, {77: frame_input}):
		_fail("authoritative frame could not be encoded")
		return
	if recorder.stream.frame_count() != 1:
		_fail("authoritative frame was not recorded")
		return
	recorder.active = false
	var debug_replay := game_manager.debug_replay_controller as DebugReplayController
	debug_replay.recorded_inputs.clear()
	debug_replay.recording = true
	debug_replay.record_input(frame_input)
	if debug_replay.recorded_inputs.size() != 1:
		_fail("debug frame was not recorded")
		return
	debug_replay.recording = false
	print("MXT_REPLAY_CONTROLLER_SMOKE_OK frames=", total_ticks,
		" seek_tick=", seek_tick,
		" catalog_entries=", catalog.entries.size())
	game_manager.queue_free()
	await process_frame
	quit(0)
