extends SceneTree

const PlayerInputClass := preload("res://player/player_input.gd")

func _make_input(tick: int, racer_index: int, mode: String) -> PackedByteArray:
	var input := PlayerInputClass.new()
	if mode == "smooth":
		input.accelerate = 1.0
		input.brake = 1.0 if ((tick + racer_index * 29) % 173) == 0 else 0.0
		input.spinattack = ((tick + racer_index * 31) % 251) == 0
		input.sideattack = ((tick + racer_index * 37) % 307) == 0
		input.boost = ((tick + racer_index * 41) % 131) == 0
		input.strafe_left = maxf(0.0, sin(float(tick + racer_index * 5) * 0.041)) * 0.5
		input.strafe_right = maxf(0.0, -sin(float(tick + racer_index * 7) * 0.039)) * 0.5
		input.steer_horizontal = sin(float(tick + racer_index * 13) * 0.047)
		input.steer_vertical = sin(float(tick + racer_index * 17) * 0.021) * 0.35
	elif mode == "randomish":
		input.accelerate = 1.0 if ((tick * 17 + racer_index * 3) % 11) != 0 else 0.0
		input.brake = 1.0 if ((tick * 19 + racer_index * 5) % 13) == 0 else 0.0
		input.spinattack = ((tick * 23 + racer_index * 7) % 17) == 0
		input.sideattack = ((tick * 29 + racer_index * 11) % 19) == 0
		input.boost = ((tick * 31 + racer_index * 13) % 23) == 0
		input.strafe_left = float((tick * 37 + racer_index * 17) % 255) / 254.0
		input.strafe_right = float((tick * 41 + racer_index * 19) % 255) / 254.0
		input.steer_horizontal = float(((tick * 43 + racer_index * 23) % 255) - 127) / 127.0
		input.steer_vertical = float(((tick * 47 + racer_index * 29) % 255) - 127) / 127.0
	else:
		input.accelerate = 1.0 if ((tick + racer_index) % 2) == 0 else 0.0
		input.brake = 1.0 if ((tick + racer_index) % 7) == 0 else 0.0
		input.spinattack = ((tick * 3 + racer_index) % 11) == 0
		input.sideattack = ((tick + racer_index * 5) % 13) == 0
		input.boost = ((tick + racer_index) % 17) == 0
		input.strafe_left = float((tick + racer_index) % 5) / 4.0
		input.strafe_right = float((tick * 2 + racer_index) % 5) / 4.0
		input.steer_horizontal = float(((tick + racer_index * 3) % 9) - 4) / 4.0
		input.steer_vertical = float(((tick * 2 + racer_index) % 9) - 4) / 4.0
	input.apply_quantization()
	return input.serialize()

func _run_case(mode: String) -> bool:
	var racer_count := 100
	var frame_count := 6
	var player_ids: Array = []
	var cpu_flags: Array = []
	for i in range(racer_count):
		player_ids.append(1000 + i)
		cpu_flags.append(i >= 2)

	var server := NetcodeSession.new()
	var client := NetcodeSession.new()
	server.configure(player_ids, cpu_flags, 1000)
	client.configure(player_ids, cpu_flags, 1001)

	var expected := {}
	for tick in range(frame_count):
		for i in range(racer_count):
			var id := int(player_ids[i])
			var bytes := _make_input(tick, i, mode)
			server.store_authoritative_input(tick, id, bytes)
			expected[[tick, id]] = bytes

	var packet: PackedByteArray = server.build_authoritative_input_packet(frame_count - 1, frame_count, 1)
	var stats: Dictionary = client.store_authoritative_input_packet(packet, 1)
	if !bool(stats.get("valid", false)) or bool(stats.get("stale", false)):
		push_error("MXT_AUTH_INPUT_ROUNDTRIP invalid mode=%s stats=%s" % [mode, stats])
		return false
	if int(stats.get("count", -1)) != frame_count or int(stats.get("first_tick", -1)) != 0 or int(stats.get("last_tick", -1)) != frame_count - 1:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP bad range mode=%s stats=%s" % [mode, stats])
		return false

	for tick in range(frame_count):
		var decoded: Dictionary = client.get_frame_as_dictionary(tick)
		for i in range(racer_count):
			var id := int(player_ids[i])
			var got: PackedByteArray = decoded.get(id, PackedByteArray())
			var want: PackedByteArray = expected[[tick, id]]
			if got != want:
				push_error("MXT_AUTH_INPUT_ROUNDTRIP mismatch mode=%s tick=%d id=%d got=%s want=%s" % [mode, tick, id, got, want])
				return false

	print("MXT_AUTH_INPUT_ROUNDTRIP_CASE_OK mode=", mode, " racers=", racer_count, " frames=", frame_count, " packet=", packet.size())
	return true

func _init() -> void:
	for mode in ["varied", "smooth", "randomish"]:
		if !_run_case(mode):
			quit(1)
			return
	print("MXT_AUTH_INPUT_ROUNDTRIP_OK")
	quit()
