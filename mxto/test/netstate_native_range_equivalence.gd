extends SceneTree

const DEFAULT_TRACK_RELATIVE := "../export-bin/track/multiplex/track.mxt_track"
const DEFAULT_CAR_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"

func _default_track_path() -> String:
	return ProjectSettings.globalize_path("res://").path_join(DEFAULT_TRACK_RELATIVE).simplify_path()

func _arg_value(args: Array, name: String, fallback: String) -> String:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return String(args[idx + 1])

func _arg_int(args: Array, name: String, fallback: int) -> int:
	return int(_arg_value(args, name, str(fallback)))

func _make_sim(track_path: String, car_props_path: String, cars: int, humans: int) -> Dictionary:
	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var car_bytes := FileAccess.get_file_as_bytes(car_props_path)
	if track_bytes.is_empty() or car_bytes.is_empty() or cars <= 0:
		return {}

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
	return {
		"sim": sim,
		"session": session,
		"player_ids": player_ids,
	}

func _free_sim(sim: GameSim) -> void:
	if sim != null:
		root.remove_child(sim)
		sim.free()

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var track_path := _arg_value(args, "--track", _default_track_path())
	var car_props_path := _arg_value(args, "--car-props", DEFAULT_CAR_PROPS)
	var cars := _arg_int(args, "--cars", 100)
	var humans := clampi(_arg_int(args, "--humans", 20), 0, cars)
	var end_tick := _arg_int(args, "--end-tick", 600)

	var old_path := _make_sim(track_path, car_props_path, cars, humans)
	var range_path := _make_sim(track_path, car_props_path, cars, humans)
	if old_path.is_empty() or range_path.is_empty():
		push_error("netstate_native_range_equivalence missing track/car data or invalid args")
		quit(1)
		return

	var old_sim: GameSim = old_path["sim"]
	var old_session: NetcodeSession = old_path["session"]
	var range_sim: GameSim = range_path["sim"]
	var range_session: NetcodeSession = range_path["session"]
	var player_ids: Array = old_path["player_ids"]

	for tick in range(end_tick + 1):
		for i in range(humans):
			var id := int(player_ids[i])
			old_session.store_pending_input(tick, id, old_sim.get_native_cpu_input_for_tick(id, tick))
		if !old_session.tick_server_frame(old_sim, tick):
			push_error("old server path failed at tick %d" % tick)
			_free_sim(old_sim)
			_free_sim(range_sim)
			quit(1)
			return

	if !range_session.has_method("tick_server_frames_with_native_inputs"):
		push_error("range native input method missing")
		_free_sim(old_sim)
		_free_sim(range_sim)
		quit(1)
		return
	var ticked := int(range_session.tick_server_frames_with_native_inputs(range_sim, 0, end_tick))
	if ticked != end_tick + 1:
		push_error("range server path ticked %d frames, expected %d" % [ticked, end_tick + 1])
		_free_sim(old_sim)
		_free_sim(range_sim)
		quit(1)
		return

	var old_state := old_sim.get_state_data(end_tick)
	var range_state := range_sim.get_state_data(end_tick)
	if old_state != range_state:
		push_error("native range state diverged at tick %d old=%d range=%d" % [end_tick, old_state.size(), range_state.size()])
		_free_sim(old_sim)
		_free_sim(range_sim)
		quit(1)
		return
	var old_packet := old_session.build_authoritative_input_packet(end_tick, 1)
	var range_packet := range_session.build_authoritative_input_packet(end_tick, 1)
	if old_packet != range_packet:
		push_error("native range authoritative input diverged at tick %d old=%d range=%d" % [end_tick, old_packet.size(), range_packet.size()])
		_free_sim(old_sim)
		_free_sim(range_sim)
		quit(1)
		return

	print("MXT_NETSTATE_NATIVE_RANGE_EQUIV_OK track=", track_path,
		" cars=", cars,
		" humans_with_cpu_inputs=", humans,
		" end_tick=", end_tick,
		" bytes=", old_state.size())
	_free_sim(old_sim)
	_free_sim(range_sim)
	quit()
