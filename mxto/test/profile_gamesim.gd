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

	var neutral := PackedByteArray([0])
	for frame in range(frames):
		sim.tick_singleplayer(-1, neutral)

	print("MXT_PROFILE_RUN track=", track_path, " cars=", cars, " frames=", frames)
	print(sim.get_phase_profile_string())
	root.remove_child(sim)
	sim.free()
	quit()
