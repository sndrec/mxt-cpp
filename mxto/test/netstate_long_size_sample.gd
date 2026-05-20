extends SceneTree

const DEFAULT_TRACK := "res://track/surface_slide/track.mxt_track"
const DEFAULT_CAR_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"

func _arg_value(args: Array, name: String, fallback: String) -> String:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return String(args[idx + 1])

func _arg_int(args: Array, name: String, fallback: int) -> int:
	return int(_arg_value(args, name, str(fallback)))

func _sample_state(sim: GameSim, tick: int, dump_dir: String) -> void:
	var state := sim.get_state_data(tick)
	if !dump_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(dump_dir)
		var file := FileAccess.open(dump_dir.path_join("state_%06d.bin" % tick), FileAccess.WRITE)
		if file != null:
			file.store_buffer(state)
			file.close()
	var compressed := state.compress(FileAccess.COMPRESSION_ZSTD)
	var stats: Dictionary = sim.get_network_state_size_stats()
	var payload_size := compressed.size() if !compressed.is_empty() else state.size()
	var fragments := int(ceil(float(payload_size) / 1000.0))
	print("MXT_NETSTATE_LONG_SAMPLE tick=", tick,
		" raw=", state.size(),
		" zstd=", payload_size,
		" fragments=", fragments,
		" cars=", int(stats.get("car_count", 0)),
		" bumpers=", int(stats.get("bumper_count", 0)),
		" active_bumpers=", int(stats.get("active_bumper_count", 0)),
		" sparks=", int(stats.get("active_spark_count", 0)),
		" triggers=", int(stats.get("trigger_count", 0)),
		" collision_old=", int(stats.get("car_collision_old_count", 0)),
		" restore=", int(stats.get("car_restore_count", 0)),
		" header=", int(stats.get("header", 0)),
		" bumper_meta=", int(stats.get("bumper_meta", 0)),
		" spark_bytes=", int(stats.get("sparks", 0)),
		" scalars=", int(stats.get("car_scalars", 0)),
		" vec3=", int(stats.get("car_vec3", 0)),
		" basis=", int(stats.get("car_basis", 0)),
		" conditionals=", int(stats.get("car_conditionals", 0)),
		" tilt=", int(stats.get("car_tilt", 0)),
		" wall=", int(stats.get("car_wall", 0)),
		" trigger_bytes=", int(stats.get("triggers", 0)))

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var track_path := _arg_value(args, "--track", DEFAULT_TRACK)
	var car_props_path := _arg_value(args, "--car-props", DEFAULT_CAR_PROPS)
	var cars := _arg_int(args, "--cars", 100)
	var humans := _arg_int(args, "--humans", 2)
	var end_tick := _arg_int(args, "--end-tick", 3600)
	var sample_start := _arg_int(args, "--sample-start", maxi(0, end_tick - 300))
	var sample_every := _arg_int(args, "--sample-every", 60)
	var dump_dir := _arg_value(args, "--dump-dir", "")

	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var car_bytes := FileAccess.get_file_as_bytes(car_props_path)
	if track_bytes.is_empty() or car_bytes.is_empty() or cars <= 0:
		push_error("netstate_long_size_sample missing track/car data or invalid car count")
		quit(1)
		return

	humans = clampi(humans, 0, cars)
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
		cpu_flags.append(i >= humans)

	var sim := GameSim.new()
	var session := NetcodeSession.new()
	root.add_child(sim)
	sim.set_spawn_seed(1)
	sim.set_bumpers_enabled(false)
	sim.instantiate_gamesim(track_buffer, car_buffers, accel_settings)
	sim.set_player_metadata(player_ids, cpu_flags)
	sim.set_sim_started(true)
	session.configure(player_ids, cpu_flags, int(player_ids[0]))

	var start_us := Time.get_ticks_usec()
	for tick in range(end_tick + 1):
		for i in range(humans):
			var id := int(player_ids[i])
			session.store_pending_input(tick, id, sim.get_native_cpu_input_for_tick(id, tick))
		if !session.tick_server_frame(sim, tick):
			push_error("netstate_long_size_sample failed to tick server frame %d" % tick)
			root.remove_child(sim)
			sim.free()
			quit(1)
			return
		if tick >= sample_start and sample_every > 0 and (tick - sample_start) % sample_every == 0:
			_sample_state(sim, tick, dump_dir)

	var total_us := Time.get_ticks_usec() - start_us
	print("MXT_NETSTATE_LONG_DONE track=", track_path,
		" cars=", cars,
		" humans_with_cpu_inputs=", humans,
		" end_tick=", end_tick,
		" avg_tick_us=", int(float(total_us) / float(maxi(end_tick + 1, 1))))
	root.remove_child(sim)
	sim.free()
	quit()
