extends SceneTree

const DEFAULT_TRACK := "res://track/surface_slide/track.mxt_track"
const DEFAULT_CAR_PROPS := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"
const AUTH_INPUT_COUNT_MASK := 0x78
const AUTH_INPUT_COUNT_SHIFT := 3
const AUTH_INPUT_COUNT_ESCAPE := 0x0f

func _authoritative_input_wire_size(packet: PackedByteArray) -> int:
	if packet.size() <= 0:
		return 0
	var meta := int(packet[0])
	var count_code := (meta & AUTH_INPUT_COUNT_MASK) >> AUTH_INPUT_COUNT_SHIFT
	return packet.size() - 1 if count_code != AUTH_INPUT_COUNT_ESCAPE else packet.size()

func _sparse_plain_wire_size(session: NetcodeSession, last_tick: int, redundancy: int, player_ids: Array) -> int:
	var first_tick := maxi(0, last_tick - redundancy + 1)
	var raw := PackedByteArray()
	for tick in range(first_tick, last_tick + 1):
		var frame: Dictionary = session.get_frame_as_dictionary(tick)
		if frame.is_empty():
			return 0
		for id in player_ids:
			var bytes: PackedByteArray = frame.get(id, PackedByteArray())
			if bytes.is_empty():
				return 0
			raw.append_array(bytes)
	var compressed := raw.compress(FileAccess.COMPRESSION_ZSTD)
	if compressed.is_empty():
		return 0
	return compressed.size() + 2

func _arg_value(args: Array, name: String, fallback: String) -> String:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return String(args[idx + 1])

func _arg_int(args: Array, name: String, fallback: int) -> int:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return int(args[idx + 1])

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var track_path := _arg_value(args, "--track", DEFAULT_TRACK)
	var car_props_path := _arg_value(args, "--car-props", DEFAULT_CAR_PROPS)
	var cars := _arg_int(args, "--cars", 100)
	var humans := clampi(_arg_int(args, "--humans", 2), 0, cars)
	var frames := _arg_int(args, "--frames", 1800)
	var sample_start := _arg_int(args, "--sample-start", 900)
	var sample_end := _arg_int(args, "--sample-end", frames)
	var redundancy := _arg_int(args, "--redundancy", 2)
	var dump_dir := _arg_value(args, "--dump-dir", "")

	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var car_bytes := FileAccess.get_file_as_bytes(car_props_path)
	if track_bytes.is_empty() or car_bytes.is_empty() or cars <= 0 or frames <= 0:
		push_error("auth_input_size_sample missing track/car data or invalid args")
		quit(1)
		return

	var track_buffer := StreamPeerBuffer.new()
	track_buffer.data_array = track_bytes
	track_buffer.big_endian = false

	var car_buffers: Array = []
	var accel_settings: Array[float] = []
	var player_ids: Array = []
	var cpu_flags: Array = []
	for i in range(cars):
		car_buffers.append(car_bytes)
		accel_settings.append(1.0)
		player_ids.append(1000 + i)
		cpu_flags.append(i >= humans)

	var sim := GameSim.new()
	root.add_child(sim)
	sim.set_spawn_seed(1)
	sim.set_bumpers_enabled(false)
	sim.instantiate_gamesim(track_buffer, car_buffers, accel_settings)
	sim.set_player_metadata(player_ids, cpu_flags)
	sim.set_sim_started(true)
	var session := NetcodeSession.new()
	session.configure(player_ids, cpu_flags, int(player_ids[0]))
	if !dump_dir.is_empty():
		session.configure_authoritative_input_sample_dump(true, frames, dump_dir)

	var sample_count := 0
	var packet_bytes := 0
	var wire_packet_bytes := 0
	var sparse_plain_wire_bytes := 0
	var packet_min := 1 << 30
	var packet_max := 0
	var wire_packet_min := 1 << 30
	var wire_packet_max := 0
	var mode_counts := {}
	var old_plain_packet_bytes := 0
	var packed_plain_packet_bytes := 0
	var delta_plain_packet_bytes := 0
	var delta_pairs_dict_packet_bytes := 0
	var bitpacked_plain_packet_bytes := 0
	var hybrid_plain_packet_bytes := 0
	var old_dict_packet_bytes := 0
	var packed_dict_packet_bytes := 0
	var delta_dict_packet_bytes := 0
	var bitpacked_dict_packet_bytes := 0
	var bitpacked_zero_plain_packet_bytes := 0
	var bitpacked_zero_dict_packet_bytes := 0
	var hybrid_dict_packet_bytes := 0
	var hybrid_smooth_dict_packet_bytes := 0
	var old_raw_bytes := 0
	var packed_raw_bytes := 0
	var delta_raw_bytes := 0
	var bitpacked_raw_bytes := 0
	for tick in range(frames):
		for i in range(humans):
			var id := int(player_ids[i])
			session.store_pending_input(tick, int(id), sim.get_native_cpu_input_for_tick(int(id), tick))
		if !session.tick_server_frame(sim, tick):
			push_error("auth_input_size_sample failed to tick server frame %d" % tick)
			quit(1)
			return
		var packet: PackedByteArray = session.build_authoritative_input_packet(tick, redundancy, 0)
		if tick >= sample_start and tick < sample_end:
			var size := packet.size()
			var wire_size := _authoritative_input_wire_size(packet)
			var sparse_wire_size := _sparse_plain_wire_size(session, tick, redundancy, player_ids)
			var packet_mode := int(packet[0]) & 0x07 if packet.size() > 0 else -1
			mode_counts[packet_mode] = int(mode_counts.get(packet_mode, 0)) + 1
			packet_bytes += size
			wire_packet_bytes += wire_size
			sparse_plain_wire_bytes += sparse_wire_size
			packet_min = mini(packet_min, size)
			packet_max = maxi(packet_max, size)
			wire_packet_min = mini(wire_packet_min, wire_size)
			wire_packet_max = maxi(wire_packet_max, wire_size)
			var cmp: Dictionary = session.debug_compare_authoritative_input_packet_sizes(tick, redundancy, 0)
			if bool(cmp.get("valid", false)):
				old_plain_packet_bytes += int(cmp.get("old_plain_packet", 0))
				packed_plain_packet_bytes += int(cmp.get("packed_plain_packet", 0))
				delta_plain_packet_bytes += int(cmp.get("delta_plain_packet", 0))
				delta_pairs_dict_packet_bytes += int(cmp.get("delta_pairs_dict_packet", 0))
				bitpacked_plain_packet_bytes += int(cmp.get("bitpacked_plain_packet", 0))
				hybrid_plain_packet_bytes += int(cmp.get("hybrid_plain_packet", 0))
				old_dict_packet_bytes += int(cmp.get("old_dict_packet", 0))
				packed_dict_packet_bytes += int(cmp.get("packed_dict_packet", 0))
				delta_dict_packet_bytes += int(cmp.get("delta_dict_packet", 0))
				bitpacked_dict_packet_bytes += int(cmp.get("bitpacked_dict_packet", 0))
				bitpacked_zero_plain_packet_bytes += int(cmp.get("bitpacked_zero_plain_packet", 0))
				bitpacked_zero_dict_packet_bytes += int(cmp.get("bitpacked_zero_dict_packet", 0))
				hybrid_dict_packet_bytes += int(cmp.get("hybrid_dict_packet", 0))
				hybrid_smooth_dict_packet_bytes += int(cmp.get("hybrid_smooth_dict_packet", 0))
				old_raw_bytes += int(cmp.get("old_raw", 0))
				packed_raw_bytes += int(cmp.get("packed_raw", 0))
				delta_raw_bytes += int(cmp.get("delta_raw", 0))
				bitpacked_raw_bytes += int(cmp.get("bitpacked_raw", 0))
			sample_count += 1

	var stats := session.consume_authoritative_packet_stats()
	var avg_packet := float(packet_bytes) / float(maxi(sample_count, 1))
	var avg_wire_packet := float(wire_packet_bytes) / float(maxi(sample_count, 1))
	print("MXT_AUTH_INPUT_SIZE_DONE track=", track_path,
		" cars=", cars,
		" humans=", humans,
		" frames=", frames,
		" sample_start=", sample_start,
		" sample_end=", sample_end,
		" redundancy=", redundancy,
		" dump_dir=", dump_dir,
		" sample_packets=", sample_count,
		" packet_avg=", avg_packet,
		" packet_min=", packet_min,
		" packet_max=", packet_max,
		" wire_packet_avg=", avg_wire_packet,
		" wire_packet_min=", wire_packet_min,
		" wire_packet_max=", wire_packet_max,
		" sparse_plain_wire_avg=", float(sparse_plain_wire_bytes) / float(maxi(sample_count, 1)),
		" mode_counts=", str(mode_counts),
		" old_raw_avg=", float(old_raw_bytes) / float(maxi(sample_count, 1)),
		" packed_raw_avg=", float(packed_raw_bytes) / float(maxi(sample_count, 1)),
		" delta_raw_avg=", float(delta_raw_bytes) / float(maxi(sample_count, 1)),
		" bitpacked_raw_avg=", float(bitpacked_raw_bytes) / float(maxi(sample_count, 1)),
		" old_plain_packet_avg=", float(old_plain_packet_bytes) / float(maxi(sample_count, 1)),
		" packed_plain_packet_avg=", float(packed_plain_packet_bytes) / float(maxi(sample_count, 1)),
		" delta_plain_packet_avg=", float(delta_plain_packet_bytes) / float(maxi(sample_count, 1)),
		" delta_pairs_dict_packet_avg=", float(delta_pairs_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" bitpacked_plain_packet_avg=", float(bitpacked_plain_packet_bytes) / float(maxi(sample_count, 1)),
		" hybrid_plain_packet_avg=", float(hybrid_plain_packet_bytes) / float(maxi(sample_count, 1)),
		" old_dict_packet_avg=", float(old_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" packed_dict_packet_avg=", float(packed_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" delta_dict_packet_avg=", float(delta_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" bitpacked_dict_packet_avg=", float(bitpacked_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" bitpacked_zero_plain_packet_avg=", float(bitpacked_zero_plain_packet_bytes) / float(maxi(sample_count, 1)),
		" bitpacked_zero_dict_packet_avg=", float(bitpacked_zero_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" hybrid_dict_packet_avg=", float(hybrid_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" hybrid_smooth_dict_packet_avg=", float(hybrid_smooth_dict_packet_bytes) / float(maxi(sample_count, 1)),
		" auth_packets=", int(stats.get("auth_packets", 0)),
		" auth_frames=", int(stats.get("auth_frames", 0)),
		" auth_encoded_inputs=", int(stats.get("auth_encoded_inputs", 0)),
		" auth_raw_bytes=", int(stats.get("auth_raw_bytes", 0)),
		" auth_payload_bytes=", int(stats.get("auth_payload_bytes", 0)))
	quit()
