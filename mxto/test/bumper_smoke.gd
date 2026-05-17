extends SceneTree

const PlayerInputClass := preload("res://player/player_input.gd")

const DEFAULT_TRACK := "res://track/smokestack/track.mxt_track"
const DEFAULT_RACER_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"
const DEFAULT_BUMPER_PROPS := "res://vehicle/asset/bruiser/wild_goose.mxt_car_props"
const BUMPER_POOL_SIZE := 60

func _arg_value(args: Array, name: String, fallback: String) -> String:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return String(args[idx + 1])

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var track_path := _arg_value(args, "--track", DEFAULT_TRACK)
	var frames := int(_arg_value(args, "--frames", "9000"))
	var watch_every := int(_arg_value(args, "--watch-every", "120"))
	if frames <= 0:
		push_error("bumper_smoke requires positive --frames")
		quit(1)
		return

	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var racer_bytes := FileAccess.get_file_as_bytes(DEFAULT_RACER_PROPS)
	var bumper_bytes := FileAccess.get_file_as_bytes(DEFAULT_BUMPER_PROPS)
	if track_bytes.is_empty() or racer_bytes.is_empty() or bumper_bytes.is_empty():
		push_error("bumper_smoke missing track or car data")
		quit(1)
		return

	var track_buffer := StreamPeerBuffer.new()
	track_buffer.data_array = track_bytes
	track_buffer.big_endian = false

	var car_buffers: Array = [racer_bytes]
	var accel_settings: Array = [1.0]
	for _slot in range(BUMPER_POOL_SIZE):
		car_buffers.append(bumper_bytes)
		accel_settings.append(1.0)

	var sim := GameSim.new()
	root.add_child(sim)
	sim.set_spawn_seed(1)
	sim.set_bumpers_enabled(true)
	sim.instantiate_gamesim(track_buffer, car_buffers, accel_settings)
	sim.set_player_metadata([1000], [true])
	sim.set_sim_started(true)

	var neutral := PlayerInputClass.new().serialize()
	var saw_lap1_active := false
	var saw_active := false
	var last_slot := -1
	var last_distance := -INF
	var min_forward_delta := INF
	var max_backward_delta := 0.0
	for frame in range(frames):
		sim.tick_singleplayer(-1, neutral)
		var tick := frame + 1
		var debug := sim.get_bumper_debug_string()
		if watch_every > 0 and tick % watch_every == 0:
			print("MXT_BUMPER_SMOKE tick=", tick, " ", debug)
		var fields := _parse_debug_fields(debug)
		var active := int(fields.get("active", "0"))
		var leader_lap := int(fields.get("leader_lap", "0"))
		if active > 0:
			saw_active = true
			if leader_lap <= 0:
				saw_lap1_active = true
			var slot := int(fields.get("first_slot", "-1"))
			if slot != last_slot:
				last_distance = -INF
				last_slot = slot
			var dist := float(fields.get("dist", "0"))
			if last_distance > -INF:
				var delta := dist - last_distance
				min_forward_delta = minf(min_forward_delta, delta)
				if delta < max_backward_delta:
					max_backward_delta = delta
			last_distance = dist

	print("MXT_BUMPER_RESULT saw_active=", saw_active,
		" saw_lap1_active=", saw_lap1_active,
		" min_forward_delta=", min_forward_delta,
		" max_backward_delta=", max_backward_delta)
	root.remove_child(sim)
	sim.free()
	if saw_lap1_active:
		push_error("bumper_smoke saw an active bumper before lap 2")
		quit(1)
		return
	if !saw_active:
		push_error("bumper_smoke did not spawn any bumpers")
		quit(1)
		return
	quit()

func _parse_debug_fields(debug: String) -> Dictionary:
	var out := {}
	for part in debug.split(" "):
		var idx := part.find("=")
		if idx <= 0:
			continue
		out[part.substr(0, idx)] = part.substr(idx + 1)
	return out
