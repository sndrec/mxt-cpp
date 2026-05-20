extends SceneTree

const PlayerInputClass := preload("res://player/player_input.gd")
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

func _hash_u32(v: int) -> int:
	var x := v & 0x7fffffff
	x = (x ^ (x >> 16)) * 1103515245
	x = (x ^ (x >> 15)) * 12345
	return (x ^ (x >> 16)) & 0x7fffffff

func _rand01(tick: int, racer: int, channel: int) -> float:
	return float(_hash_u32(tick * 73856093 + racer * 19349663 + channel * 83492791)) / float(0x7fffffff)

func _make_input(tick: int, racer: int, mode: String) -> PackedByteArray:
	var input := PlayerInputClass.new()
	if mode == "random":
		input.accelerate = 1.0 if _rand01(tick, racer, 0) > 0.08 else 0.0
		input.brake = 1.0 if _rand01(tick, racer, 1) > 0.93 else 0.0
		input.spinattack = _rand01(tick, racer, 2) > 0.985
		input.sideattack = _rand01(tick, racer, 3) > 0.99
		input.boost = _rand01(tick, racer, 4) > 0.96
		input.strafe_left = _rand01(tick, racer, 5) if _rand01(tick, racer, 6) > 0.45 else 0.0
		input.strafe_right = _rand01(tick, racer, 7) if _rand01(tick, racer, 8) > 0.45 else 0.0
		input.steer_horizontal = _rand01(tick, racer, 9) * 2.0 - 1.0
		input.steer_vertical = _rand01(tick, racer, 10) * 2.0 - 1.0
	elif mode == "jitter":
		input.accelerate = 1.0
		input.brake = 1.0 if ((tick + racer * 17) % 47) == 0 else 0.0
		input.spinattack = ((tick + racer * 11) % 113) == 0
		input.sideattack = ((tick + racer * 13) % 157) == 0
		input.boost = ((tick + racer * 7) % 83) == 0
		input.strafe_left = 0.0 if ((tick + racer) % 5) < 3 else _rand01(tick, racer, 5)
		input.strafe_right = 0.0 if ((tick + racer * 3) % 5) < 3 else _rand01(tick, racer, 7)
		input.steer_horizontal = sin(float(tick + racer * 19) * 0.19) * 0.75 + (_rand01(tick, racer, 9) - 0.5) * 0.35
		input.steer_vertical = sin(float(tick + racer * 23) * 0.13) * 0.25 + (_rand01(tick, racer, 10) - 0.5) * 0.15
	else:
		input.accelerate = 1.0
		input.brake = 1.0 if ((tick + racer * 29) % 173) == 0 else 0.0
		input.spinattack = ((tick + racer * 31) % 251) == 0
		input.sideattack = ((tick + racer * 37) % 307) == 0
		input.boost = ((tick + racer * 41) % 131) == 0
		input.strafe_left = maxf(0.0, sin(float(tick + racer * 5) * 0.041)) * 0.5
		input.strafe_right = maxf(0.0, -sin(float(tick + racer * 7) * 0.039)) * 0.5
		input.steer_horizontal = sin(float(tick + racer * 13) * 0.047)
		input.steer_vertical = sin(float(tick + racer * 17) * 0.021) * 0.35
	input.apply_quantization()
	return input.serialize()

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var racers := _arg_int(args, "--cars", 100)
	var frames := _arg_int(args, "--frames", 3600)
	var sample_start := _arg_int(args, "--sample-start", 900)
	var sample_end := _arg_int(args, "--sample-end", frames)
	var redundancy := _arg_int(args, "--redundancy", 2)
	var mode := _arg_value(args, "--mode", "smooth")
	var dump_dir := _arg_value(args, "--dump-dir", "")
	var result_file := _arg_value(args, "--result-file", "")
	if racers <= 0 or frames <= 0:
		push_error("auth_input_synthetic_sample invalid args")
		quit(1)
		return

	var player_ids: Array = []
	var cpu_flags: Array = []
	for i in range(racers):
		player_ids.append(1000 + i)
		cpu_flags.append(false)

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
	var old_plain_packet_bytes := 0
	var packed_plain_packet_bytes := 0
	var delta_plain_packet_bytes := 0
	var bitpacked_plain_packet_bytes := 0
	var hybrid_plain_packet_bytes := 0
	var old_dict_packet_bytes := 0
	var packed_dict_packet_bytes := 0
	var delta_dict_packet_bytes := 0
	var bitpacked_dict_packet_bytes := 0
	var hybrid_dict_packet_bytes := 0
	for tick in range(frames):
		for i in range(racers):
			session.store_authoritative_input(tick, int(player_ids[i]), _make_input(tick, i, mode))
		var packet: PackedByteArray = session.build_authoritative_input_packet(tick, redundancy, 0)
		if tick >= sample_start and tick < sample_end:
			var size := packet.size()
			var wire_size := _authoritative_input_wire_size(packet)
			var sparse_wire_size := _sparse_plain_wire_size(session, tick, redundancy, player_ids)
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
				bitpacked_plain_packet_bytes += int(cmp.get("bitpacked_plain_packet", 0))
				hybrid_plain_packet_bytes += int(cmp.get("hybrid_plain_packet", 0))
				old_dict_packet_bytes += int(cmp.get("old_dict_packet", 0))
				packed_dict_packet_bytes += int(cmp.get("packed_dict_packet", 0))
				delta_dict_packet_bytes += int(cmp.get("delta_dict_packet", 0))
				bitpacked_dict_packet_bytes += int(cmp.get("bitpacked_dict_packet", 0))
				hybrid_dict_packet_bytes += int(cmp.get("hybrid_dict_packet", 0))
			sample_count += 1

	var result := "MXT_AUTH_INPUT_SYNTHETIC_SIZE_DONE mode=%s cars=%d frames=%d sample_start=%d sample_end=%d redundancy=%d dump_dir=%s sample_packets=%d packet_avg=%f packet_min=%d packet_max=%d wire_packet_avg=%f wire_packet_min=%d wire_packet_max=%d sparse_plain_wire_avg=%f old_plain_packet_avg=%f packed_plain_packet_avg=%f delta_plain_packet_avg=%f bitpacked_plain_packet_avg=%f hybrid_plain_packet_avg=%f old_dict_packet_avg=%f packed_dict_packet_avg=%f delta_dict_packet_avg=%f bitpacked_dict_packet_avg=%f hybrid_dict_packet_avg=%f" % [
		mode,
		racers,
		frames,
		sample_start,
		sample_end,
		redundancy,
		dump_dir,
		sample_count,
		float(packet_bytes) / float(maxi(sample_count, 1)),
		packet_min,
		packet_max,
		float(wire_packet_bytes) / float(maxi(sample_count, 1)),
		wire_packet_min,
		wire_packet_max,
		float(sparse_plain_wire_bytes) / float(maxi(sample_count, 1)),
		float(old_plain_packet_bytes) / float(maxi(sample_count, 1)),
		float(packed_plain_packet_bytes) / float(maxi(sample_count, 1)),
		float(delta_plain_packet_bytes) / float(maxi(sample_count, 1)),
		float(bitpacked_plain_packet_bytes) / float(maxi(sample_count, 1)),
		float(hybrid_plain_packet_bytes) / float(maxi(sample_count, 1)),
		float(old_dict_packet_bytes) / float(maxi(sample_count, 1)),
		float(packed_dict_packet_bytes) / float(maxi(sample_count, 1)),
		float(delta_dict_packet_bytes) / float(maxi(sample_count, 1)),
		float(bitpacked_dict_packet_bytes) / float(maxi(sample_count, 1)),
		float(hybrid_dict_packet_bytes) / float(maxi(sample_count, 1)),
	]
	if !result_file.is_empty():
		var out := FileAccess.open(result_file, FileAccess.WRITE)
		if out != null:
			out.store_string(result + "\n")
			out.close()
	printerr(result)
	quit()
