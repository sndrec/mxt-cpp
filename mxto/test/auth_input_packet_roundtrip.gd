extends SceneTree

const PlayerInputClass := preload("res://player/player_input.gd")

func _make_input(tick: int, racer_index: int) -> PackedByteArray:
	var input := PlayerInputClass.new()
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

func _init() -> void:
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
			var bytes := _make_input(tick, i)
			server.store_authoritative_input(tick, id, bytes)
			expected[[tick, id]] = bytes

	var packet: PackedByteArray = server.build_authoritative_input_packet(frame_count - 1, frame_count, 1)
	var stats: Dictionary = client.store_authoritative_input_packet(packet, 1)
	if !bool(stats.get("valid", false)) or bool(stats.get("stale", false)):
		push_error("MXT_AUTH_INPUT_ROUNDTRIP invalid stats=%s" % [stats])
		quit(1)
		return
	if int(stats.get("count", -1)) != frame_count or int(stats.get("first_tick", -1)) != 0 or int(stats.get("last_tick", -1)) != frame_count - 1:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP bad range stats=%s" % [stats])
		quit(1)
		return

	for tick in range(frame_count):
		var decoded: Dictionary = client.get_frame_as_dictionary(tick)
		for i in range(racer_count):
			var id := int(player_ids[i])
			var got: PackedByteArray = decoded.get(id, PackedByteArray())
			var want: PackedByteArray = expected[[tick, id]]
			if got != want:
				push_error("MXT_AUTH_INPUT_ROUNDTRIP mismatch tick=%d id=%d got=%s want=%s" % [tick, id, got, want])
				quit(1)
				return

	print("MXT_AUTH_INPUT_ROUNDTRIP_OK racers=", racer_count, " frames=", frame_count, " packet=", packet.size())
	quit()
