extends SceneTree

const PlayerInputClass := preload("res://player/player_input.gd")
const AUTH_INPUT_COUNT_MASK := 0x78
const AUTH_INPUT_COUNT_SHIFT := 3
const AUTH_INPUT_COUNT_ESCAPE := 0x0f

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
	elif mode == "low_entropy":
		input.accelerate = 1.0
		input.brake = 0.0
		input.spinattack = false
		input.sideattack = false
		input.boost = false
		input.strafe_left = float((tick * 37 + racer_index * 17) % 255) / 254.0
		input.strafe_right = float((tick * 41 + racer_index * 19) % 255) / 254.0
		input.steer_horizontal = float(((tick * 43 + racer_index * 23) % 255) - 127) / 127.0
		input.steer_vertical = 0.0
	elif mode == "zero_sparse":
		input.accelerate = 1.0
		input.brake = 1.0 if ((tick + racer_index * 17) % 47) == 0 else 0.0
		input.spinattack = ((tick + racer_index * 11) % 113) == 0
		input.sideattack = ((tick + racer_index * 13) % 157) == 0
		input.boost = ((tick + racer_index * 7) % 83) == 0
		input.strafe_left = 0.0 if ((tick + racer_index) % 5) < 3 else float((tick * 37 + racer_index * 17) % 255) / 254.0
		input.strafe_right = 0.0 if ((tick + racer_index * 3) % 5) < 3 else float((tick * 41 + racer_index * 19) % 255) / 254.0
		input.steer_horizontal = sin(float(tick + racer_index * 19) * 0.19) * 0.75
		input.steer_vertical = sin(float(tick + racer_index * 23) * 0.13) * 0.25
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

func _run_case(mode: String, frame_count := 6, expected_packet_mode := -1, input_tick_base := 0) -> bool:
	var racer_count := 100
	var player_ids: Array = []
	var cpu_flags: Array = []
	for i in range(racer_count):
		player_ids.append(1000 + i)
		cpu_flags.append(i >= 2)

	var server := NetcodeSession.new()
	var client := NetcodeSession.new()
	server.configure(player_ids, cpu_flags, 1000)
	client.configure(player_ids, cpu_flags, 1001)

	for tick in range(frame_count):
		for i in range(racer_count):
			var id := int(player_ids[i])
			var bytes := _make_input(tick + input_tick_base, i, mode)
			server.store_authoritative_input(tick, id, bytes)

	var packet: PackedByteArray = server.build_authoritative_input_packet(frame_count - 1, frame_count, 1)
	server.consume_authoritative_packet_stats()
	var candidate_count := server.get_authoritative_stat_compression_candidates()
	if candidate_count <= 0 or candidate_count > 3:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP candidate budget exceeded mode=%s candidates=%d" % [mode, candidate_count])
		return false
	if expected_packet_mode >= 0 and packet.size() > 0 and (int(packet[0]) & 0x07) != expected_packet_mode:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP wrong packet mode=%s got=%d want=%d packet=%d" % [mode, int(packet[0]) & 0x07, expected_packet_mode, packet.size()])
		return false
	var packet_status := client.store_authoritative_input_packet(packet, 1, frame_count - 1)
	if packet_status != NetcodeSession.PACKET_STORE_VALID:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP invalid mode=%s status=%d" % [mode, packet_status])
		return false
	if client.get_last_authoritative_packet_count() != frame_count \
			or client.get_last_authoritative_packet_first_tick() != 0 \
			or client.get_last_authoritative_packet_last_tick() != frame_count - 1:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP bad range mode=%s first=%d last=%d count=%d" % [
			mode,
			client.get_last_authoritative_packet_first_tick(),
			client.get_last_authoritative_packet_last_tick(),
			client.get_last_authoritative_packet_count(),
		])
		return false

	var rebuilt_packet := client.build_authoritative_input_packet(frame_count - 1, frame_count, 1)
	if rebuilt_packet != packet:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP decoded packet mismatch mode=%s got=%d want=%d" % [mode, rebuilt_packet.size(), packet.size()])
		return false

	var stripped_client := NetcodeSession.new()
	stripped_client.configure(player_ids, cpu_flags, 1001)
	var packet_meta := int(packet[0])
	var count_code := (packet_meta & AUTH_INPUT_COUNT_MASK) >> AUTH_INPUT_COUNT_SHIFT
	if count_code == AUTH_INPUT_COUNT_ESCAPE:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP unexpected escaped count mode=%s packet=%d" % [mode, packet.size()])
		return false
	var stripped_packet := packet.slice(1)
	var stripped_status := stripped_client.store_authoritative_input_packet(stripped_packet, 1, frame_count - 1, packet_meta)
	if stripped_status != NetcodeSession.PACKET_STORE_VALID:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP stripped invalid mode=%s status=%d" % [mode, stripped_status])
		return false
	if stripped_client.get_last_authoritative_packet_count() != frame_count \
			or stripped_client.get_last_authoritative_packet_first_tick() != 0 \
			or stripped_client.get_last_authoritative_packet_last_tick() != frame_count - 1:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP stripped bad range mode=%s first=%d last=%d count=%d" % [
			mode,
			stripped_client.get_last_authoritative_packet_first_tick(),
			stripped_client.get_last_authoritative_packet_last_tick(),
			stripped_client.get_last_authoritative_packet_count(),
		])
		return false
	var rebuilt_stripped_packet := stripped_client.build_authoritative_input_packet(frame_count - 1, frame_count, 1)
	if rebuilt_stripped_packet != packet:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP stripped decoded packet mismatch mode=%s got=%d want=%d" % [mode, rebuilt_stripped_packet.size(), packet.size()])
		return false

	print("MXT_AUTH_INPUT_ROUNDTRIP_CASE_OK mode=", mode, " racers=", racer_count, " frames=", frame_count, " packet=", packet.size())
	return true

func _init() -> void:
	var input_transport := InputTransportController.new()
	input_transport.race_netplay_phase = 1
	var packed_auth_tick: int = input_transport._pack_authoritative_input_tick(12345, 0x2a)
	if input_transport._unpack_race_phase(packed_auth_tick) != 1 or input_transport._unpack_race_tick(packed_auth_tick) != 12345 or input_transport._unpack_authoritative_input_meta(packed_auth_tick) != 0x2a:
		push_error("MXT_AUTH_INPUT_ROUNDTRIP auth tick metadata packing failed")
		quit(1)
		return
	for mode in ["varied", "smooth", "randomish"]:
		if !_run_case(mode):
			quit(1)
			return
	if !_run_case("low_entropy", 2):
		quit(1)
		return
	if !_run_case("zero_sparse", 2, 1, 900):
		quit(1)
		return
	print("MXT_AUTH_INPUT_ROUNDTRIP_OK")
	quit()
