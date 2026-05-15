extends SceneTree

const DEFAULT_TRACK := "res://track/smokestack_profile_analytic/track.mxt_track"
const DEFAULT_CAR_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"

func _arg_value(args: Array, name: String, fallback: String) -> String:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return String(args[idx + 1])

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var track_path := _arg_value(args, "--track", DEFAULT_TRACK)
	var car_props_path := _arg_value(args, "--car-props", DEFAULT_CAR_PROPS)
	var frames := int(_arg_value(args, "--frames", "3600"))
	var cars := int(_arg_value(args, "--cars", "100"))
	var require_full_lap := args.has("--require-full-lap")
	if frames <= 0 or cars <= 0:
		push_error("profile_gamesim requires positive --frames and --cars")
		quit(1)
		return

	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var car_bytes := FileAccess.get_file_as_bytes(car_props_path)
	if track_bytes.is_empty() or car_bytes.is_empty():
		push_error("profile_gamesim missing track or car data")
		quit(1)
		return

	var track_buffer := StreamPeerBuffer.new()
	track_buffer.data_array = track_bytes
	track_buffer.big_endian = false

	var car_buffers: Array = []
	var accel_settings: Array = []
	var player_ids: Array = []
	var cpu_flags: Array = []
	for i in range(cars):
		car_buffers.append(car_bytes)
		accel_settings.append(1.0)
		player_ids.append(1000 + i)
		cpu_flags.append(true)

	var sim := GameSim.new()
	root.add_child(sim)
	sim.set_spawn_seed(1)
	sim.instantiate_gamesim(track_buffer, car_buffers, accel_settings)
	sim.set_player_metadata(player_ids, cpu_flags)
	sim.set_sim_started(true)

	var lap_length := float(sim.get_track_lap_length())
	var start_distances: Array[float] = []
	for player_id in player_ids:
		start_distances.append(float(sim.get_player_lap_distance(player_id)))

	var neutral := PackedByteArray([0])
	var frames_run := 0
	var full_lap_count := 0
	var min_lap_delta := 0.0
	var max_lap_delta := 0.0
	var min_lap_player := -1
	var max_lap_player := -1
	for frame in range(frames):
		sim.tick_singleplayer(-1, neutral)
		frames_run = frame + 1
		if require_full_lap:
			full_lap_count = 0
			min_lap_delta = INF
			max_lap_delta = -INF
			min_lap_player = -1
			max_lap_player = -1
			for i in range(cars):
				var distance := float(sim.get_player_lap_distance(player_ids[i]))
				var delta := distance - start_distances[i]
				if delta < min_lap_delta:
					min_lap_delta = delta
					min_lap_player = player_ids[i]
				if delta > max_lap_delta:
					max_lap_delta = delta
					max_lap_player = player_ids[i]
				if delta >= lap_length:
					full_lap_count += 1
			if full_lap_count == cars:
				break

	if require_full_lap:
		print("MXT_PROFILE_LAP_CHECK completed=", full_lap_count, " cars=", cars, " frames_run=", frames_run, " lap_length=", lap_length, " min_delta=", min_lap_delta, " min_player=", min_lap_player, " min_lap=", sim.get_player_lap(min_lap_player), " max_delta=", max_lap_delta, " max_player=", max_lap_player, " max_lap=", sim.get_player_lap(max_lap_player))
		if full_lap_count != cars:
			print("MXT_PROFILE_MIN_PLAYER ", sim.get_player_debug_string(min_lap_player))
			print("MXT_PROFILE_MAX_PLAYER ", sim.get_player_debug_string(max_lap_player))
			push_error("profile_gamesim full-lap requirement was not met")
			root.remove_child(sim)
			sim.free()
			quit(1)
			return

	print("MXT_PROFILE_RUN track=", track_path, " cars=", cars, " frames=", frames_run)
	print(sim.get_phase_profile_string())
	root.remove_child(sim)
	sim.free()
	quit()
