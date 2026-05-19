extends SceneTree

const PlayerInputClass := preload("res://player/player_input.gd")

const DEFAULT_TRACK := "res://track/multiplex/track.mxt_track"
const DEFAULT_RACER_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var frames := 720
	var bumpers_enabled := !args.has("--no-bumpers")
	var idx := args.find("--frames")
	if idx >= 0 and idx + 1 < args.size():
		frames = int(args[idx + 1])

	var track_bytes := FileAccess.get_file_as_bytes(DEFAULT_TRACK)
	var racer_bytes := FileAccess.get_file_as_bytes(DEFAULT_RACER_PROPS)
	if track_bytes.is_empty() or racer_bytes.is_empty():
		push_error("bumper_netstate_smoke missing track or car data")
		quit(1)
		return

	var server_buffer := StreamPeerBuffer.new()
	server_buffer.data_array = track_bytes
	server_buffer.big_endian = false
	var client_buffer := StreamPeerBuffer.new()
	client_buffer.data_array = track_bytes
	client_buffer.big_endian = false

	var server := GameSim.new()
	var client := GameSim.new()
	root.add_child(server)
	root.add_child(client)
	for sim in [server, client]:
		sim.set_spawn_seed(1)
		sim.set_bumpers_enabled(bumpers_enabled)
	server.instantiate_gamesim(server_buffer, [racer_bytes], [1.0])
	client.instantiate_gamesim(client_buffer, [racer_bytes], [1.0])
	server.set_player_metadata([1000], [true])
	client.set_player_metadata([1000], [true])
	server.set_sim_started(true)
	client.set_sim_started(true)

	var neutral := PlayerInputClass.new().serialize()
	var log_path := "user://bumper_netstate_smoke.log"
	_log_line(log_path, "start bumpers=%s frames=%d" % [str(bumpers_enabled), frames])
	for frame in range(frames):
		_log_line(log_path, "before_server_tick frame=%d server=[%s] client=[%s]" % [frame, server.get_bumper_debug_string(), client.get_bumper_debug_string()])
		server.tick_singleplayer(-1, neutral)
		_log_line(log_path, "before_client_tick frame=%d server=[%s] client=[%s]" % [frame, server.get_bumper_debug_string(), client.get_bumper_debug_string()])
		client.tick_singleplayer(-1, neutral)
		var state := server.get_state_data(frame)
		client.set_state_data(frame, state)
		client.load_state(frame)
		_log_line(log_path, "after_load frame=%d server=[%s] client=[%s]" % [frame, server.get_bumper_debug_string(), client.get_bumper_debug_string()])

	root.remove_child(server)
	root.remove_child(client)
	server.free()
	client.free()
	quit()

func _log_line(path: String, text: String) -> void:
	var f := FileAccess.open(path, FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return
	f.seek_end()
	f.store_line(text)
	f.close()
