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
	if !replay.replay_playback_active or replay.replay_playback_frames.is_empty():
		_fail("playback did not start")
		return
	var total_ticks := replay.replay_playback_frames.size()
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
	replay._build_replay_catalog()
	replay._refresh_replay_catalog()
	if replay.replay_catalog_root == null or replay.replay_catalog_list == null:
		_fail("catalog UI was not constructed")
		return
	if replay.replay_catalog_entries.is_empty():
		_fail("catalog did not discover replay metadata")
		return
	replay.replay_recording_frames.clear()
	replay.replay_recording_active = true
	replay.replay_recording_saved = false
	var frame_input := PackedByteArray([1, 2, 3, 4])
	replay.record_frame(123, {77: frame_input})
	if replay.replay_recording_frames.size() != 1:
		_fail("authoritative frame was not recorded")
		return
	replay.replay_recording_active = false
	replay.debug_replay_inputs.clear()
	replay.debug_replay_recording = true
	replay.record_debug_input(frame_input)
	if replay.debug_replay_inputs.size() != 1:
		_fail("debug frame was not recorded")
		return
	replay.debug_replay_recording = false
	print("MXT_REPLAY_CONTROLLER_SMOKE_OK frames=", total_ticks,
		" seek_tick=", seek_tick,
		" catalog_entries=", replay.replay_catalog_entries.size())
	game_manager.queue_free()
	await process_frame
	quit(0)
