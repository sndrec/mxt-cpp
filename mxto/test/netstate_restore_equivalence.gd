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

func _arg_bool(args: Array, name: String, fallback: bool) -> bool:
	var value := _arg_value(args, name, "true" if fallback else "false").to_lower()
	return value == "1" or value == "true" or value == "yes" or value == "on"

func _bytes_equal(a: PackedByteArray, b: PackedByteArray) -> bool:
	if a.size() != b.size():
		return false
	for i in range(a.size()):
		if a[i] != b[i]:
			return false
	return true

func _first_byte_diff(a: PackedByteArray, b: PackedByteArray) -> int:
	var limit := mini(a.size(), b.size())
	for i in range(limit):
		if a[i] != b[i]:
			return i
	return limit if a.size() != b.size() else -1

func _basis_max_delta(a: Basis, b: Basis) -> float:
	var max_delta := 0.0
	max_delta = maxf(max_delta, a.x.distance_to(b.x))
	max_delta = maxf(max_delta, a.y.distance_to(b.y))
	max_delta = maxf(max_delta, a.z.distance_to(b.z))
	return max_delta

func _compare_visible_state(a: GameSim, b: GameSim, cars: int, player_ids: Array, tick: int, pos_epsilon: float, basis_epsilon: float) -> bool:
	var max_pos_delta := 0.0
	var max_basis_delta := 0.0
	var max_lap_delta := 0.0
	var max_pos_car := -1
	var max_basis_car := -1
	var max_lap_player := -1
	for i in range(cars):
		var ta: Transform3D = a.get_car_render_transform(i)
		var tb: Transform3D = b.get_car_render_transform(i)
		var pos_delta := ta.origin.distance_to(tb.origin)
		var basis_delta := _basis_max_delta(ta.basis, tb.basis)
		if pos_delta > max_pos_delta:
			max_pos_delta = pos_delta
			max_pos_car = i
		if basis_delta > max_basis_delta:
			max_basis_delta = basis_delta
			max_basis_car = i
		if pos_delta > pos_epsilon or basis_delta > basis_epsilon:
			push_error("netstate_restore_equivalence visible transform mismatch tick=%d car=%d pos_delta=%.9f basis_delta=%.9f" % [
				tick, i, pos_delta, basis_delta])
			return false
	for id in player_ids:
		if a.get_player_lap(int(id)) != b.get_player_lap(int(id)):
			push_error("netstate_restore_equivalence lap mismatch tick=%d player=%d a=%d b=%d" % [
				tick, int(id), a.get_player_lap(int(id)), b.get_player_lap(int(id))])
			return false
		var dist_delta := absf(float(a.get_player_lap_distance(int(id))) - float(b.get_player_lap_distance(int(id))))
		if dist_delta > max_lap_delta:
			max_lap_delta = dist_delta
			max_lap_player = int(id)
		if dist_delta > pos_epsilon:
			push_error("netstate_restore_equivalence lap distance mismatch tick=%d player=%d delta=%.9f" % [tick, int(id), dist_delta])
			return false
	print("MXT_NETSTATE_RESTORE_EQUIV_DRIFT tick=", tick,
		" max_pos_delta=", max_pos_delta,
		" max_pos_car=", max_pos_car,
		" max_basis_delta=", max_basis_delta,
		" max_basis_car=", max_basis_car,
		" max_lap_delta=", max_lap_delta,
		" max_lap_player=", max_lap_player)
	return true

func _compare_debug_state(a: GameSim, b: GameSim, player_ids: Array, tick: int) -> bool:
	for id in player_ids:
		var ida := int(id)
		var da := a.get_player_debug_string(ida)
		var db := b.get_player_debug_string(ida)
		if da != db:
			push_error("netstate_restore_equivalence debug mismatch tick=%d player=%d a=[%s] b=[%s]" % [tick, ida, da, db])
			return false
	return true

func _parse_debug_scalar(s: String, key: String) -> float:
	var marker := key + "="
	var start := s.find(marker)
	if start < 0:
		return 0.0
	start += marker.length()
	var end := s.find(" ", start)
	if end < 0:
		end = s.length()
	return float(s.substr(start, end - start))

func _parse_debug_vec(s: String, key: String) -> Vector3:
	var marker := key + "=("
	var start := s.find(marker)
	if start < 0:
		return Vector3.ZERO
	start += marker.length()
	var end := s.find(")", start)
	if end < 0:
		return Vector3.ZERO
	var parts := s.substr(start, end - start).split(",")
	if parts.size() < 3:
		return Vector3.ZERO
	return Vector3(float(parts[0]), float(parts[1]), float(parts[2]))

func _print_debug_pair(prefix: String, a: GameSim, b: GameSim, player_id: int) -> void:
	if player_id < 0:
		return
	print(prefix, " player=", player_id, " auth=[", a.get_player_debug_string(player_id), "] restored=[", b.get_player_debug_string(player_id), "]")

func _measure_debug_numeric(a: GameSim, b: GameSim, player_ids: Array, pos_epsilon: float, basis_epsilon: float) -> Dictionary:
	var max_pos_delta := 0.0
	var max_vel_delta := 0.0
	var max_up_delta := 0.0
	var max_dist_delta := 0.0
	var max_pos_player := -1
	var max_vel_player := -1
	var max_up_player := -1
	var max_dist_player := -1
	var failed := false
	var fail_player := -1
	var fail_pos_delta := 0.0
	var fail_vel_delta := 0.0
	var fail_up_delta := 0.0
	var fail_dist_delta := 0.0
	for id in player_ids:
		var ida := int(id)
		var da := a.get_player_debug_string(ida)
		var db := b.get_player_debug_string(ida)
		var pos_delta := _parse_debug_vec(da, "pos").distance_to(_parse_debug_vec(db, "pos"))
		var vel_delta := _parse_debug_vec(da, "vel").distance_to(_parse_debug_vec(db, "vel"))
		var up_delta := _parse_debug_vec(da, "up").distance_to(_parse_debug_vec(db, "up"))
		var dist_delta := absf(_parse_debug_scalar(da, "dist") - _parse_debug_scalar(db, "dist"))
		if pos_delta > max_pos_delta:
			max_pos_delta = pos_delta
			max_pos_player = ida
		if vel_delta > max_vel_delta:
			max_vel_delta = vel_delta
			max_vel_player = ida
		if up_delta > max_up_delta:
			max_up_delta = up_delta
			max_up_player = ida
		if dist_delta > max_dist_delta:
			max_dist_delta = dist_delta
			max_dist_player = ida
		if !failed and (pos_delta > pos_epsilon or up_delta > basis_epsilon):
			failed = true
			fail_player = ida
			fail_pos_delta = pos_delta
			fail_vel_delta = vel_delta
			fail_up_delta = up_delta
			fail_dist_delta = dist_delta
	return {
		"max_pos_delta": max_pos_delta,
		"max_vel_delta": max_vel_delta,
		"max_up_delta": max_up_delta,
		"max_dist_delta": max_dist_delta,
		"max_pos_player": max_pos_player,
		"max_vel_player": max_vel_player,
		"max_up_player": max_up_player,
		"max_dist_player": max_dist_player,
		"failed": failed,
		"fail_player": fail_player,
		"fail_pos_delta": fail_pos_delta,
		"fail_vel_delta": fail_vel_delta,
		"fail_up_delta": fail_up_delta,
		"fail_dist_delta": fail_dist_delta,
	}

func _print_numeric(prefix: String, tick: int, m: Dictionary) -> void:
	print(prefix, " tick=", tick,
		" max_pos_delta=", float(m.get("max_pos_delta", 0.0)),
		" max_pos_player=", int(m.get("max_pos_player", -1)),
		" max_vel_delta=", float(m.get("max_vel_delta", 0.0)),
		" max_vel_player=", int(m.get("max_vel_player", -1)),
		" max_up_delta=", float(m.get("max_up_delta", 0.0)),
		" max_up_player=", int(m.get("max_up_player", -1)),
		" max_dist_delta=", float(m.get("max_dist_delta", 0.0)),
		" max_dist_player=", int(m.get("max_dist_player", -1)))

func _maybe_print_details(prefix: String, a: GameSim, b: GameSim, tick: int, m: Dictionary, detail_threshold: float) -> void:
	if detail_threshold < 0.0 or float(m.get("max_pos_delta", 0.0)) < detail_threshold:
		return
	var max_pos_player := int(m.get("max_pos_player", -1))
	var max_vel_player := int(m.get("max_vel_player", -1))
	_print_debug_pair("%s_POS tick=%d delta=%.9f" % [prefix, tick, float(m.get("max_pos_delta", 0.0))], a, b, max_pos_player)
	if max_vel_player != max_pos_player:
		_print_debug_pair("%s_VEL tick=%d delta=%.9f" % [prefix, tick, float(m.get("max_vel_delta", 0.0))], a, b, max_vel_player)

func _compare_debug_numeric(a: GameSim, b: GameSim, player_ids: Array, tick: int, pos_epsilon: float, basis_epsilon: float, detail_threshold: float) -> bool:
	var m := _measure_debug_numeric(a, b, player_ids, pos_epsilon, basis_epsilon)
	if bool(m.get("failed", false)):
		push_error("netstate_restore_equivalence numeric mismatch tick=%d player=%d pos_delta=%.9f vel_delta=%.9f up_delta=%.12f dist_delta=%.9f" % [
			tick,
			int(m.get("fail_player", -1)),
			float(m.get("fail_pos_delta", 0.0)),
			float(m.get("fail_vel_delta", 0.0)),
			float(m.get("fail_up_delta", 0.0)),
			float(m.get("fail_dist_delta", 0.0))])
		return false
	_print_numeric("MXT_NETSTATE_RESTORE_EQUIV_NUMERIC", tick, m)
	_maybe_print_details("MXT_NETSTATE_RESTORE_EQUIV_DETAIL", a, b, tick, m, detail_threshold)
	return true

func _make_sim(track_bytes: PackedByteArray, car_bytes: PackedByteArray, cars: int, humans: int, player_ids: Array, cpu_flags: Array) -> GameSim:
	var track_buffer := StreamPeerBuffer.new()
	track_buffer.data_array = track_bytes
	track_buffer.big_endian = false
	var car_buffers: Array = []
	var accel_settings: Array = []
	for i in range(cars):
		car_buffers.append(car_bytes)
		accel_settings.append(1.0)
	var sim := GameSim.new()
	root.add_child(sim)
	sim.set_spawn_seed(1)
	sim.set_bumpers_enabled(false)
	sim.instantiate_gamesim(track_buffer, car_buffers, accel_settings)
	sim.set_player_metadata(player_ids, cpu_flags)
	sim.set_sim_started(true)
	return sim

func _make_session(player_ids: Array, cpu_flags: Array) -> NetcodeSession:
	var session := NetcodeSession.new()
	session.configure(player_ids, cpu_flags, int(player_ids[0]))
	return session

func _tick_sim(sim: GameSim, session: NetcodeSession, tick: int, humans: int, player_ids: Array) -> bool:
	for i in range(humans):
		var id := int(player_ids[i])
		session.store_pending_input(tick, id, sim.get_native_cpu_input_for_tick(id, tick))
	return session.tick_server_frame(sim, tick)

func _tick_sim_with_inputs(sim: GameSim, session: NetcodeSession, tick: int, inputs_by_id: Dictionary) -> bool:
	_store_inputs(session, tick, inputs_by_id)
	return session.tick_server_frame(sim, tick)

func _store_inputs(session: NetcodeSession, tick: int, inputs_by_id: Dictionary) -> void:
	for id in inputs_by_id.keys():
		session.store_pending_input(tick, int(id), inputs_by_id[id])

func _make_inputs(sim: GameSim, tick: int, humans: int, player_ids: Array) -> Dictionary:
	var shared_inputs := {}
	for i in range(humans):
		var id := int(player_ids[i])
		shared_inputs[id] = sim.get_native_cpu_input_for_tick(id, tick)
	return shared_inputs

func _run_resync_series(authoritative: GameSim, restored: GameSim, authoritative_session: NetcodeSession, restored_session: NetcodeSession, player_ids: Array, humans: int, end_tick: int, interval: int, pos_epsilon: float, basis_epsilon: float, detail_threshold: float) -> bool:
	var max_raw := 0
	var max_zstd := 0
	var max_tick := -1
	var max_pre_pos := 0.0
	var max_pre_tick := -1
	var max_pre_player := -1
	var max_post_pos := 0.0
	var max_post_tick := -1
	var max_post_player := -1
	var samples := 0
	for tick in range(end_tick + 1):
		var shared_inputs := _make_inputs(authoritative, tick, humans, player_ids)
		if !_tick_sim_with_inputs(authoritative, authoritative_session, tick, shared_inputs):
			push_error("netstate_restore_equivalence failed authoritative resync tick %d" % tick)
			return false
		if !_tick_sim_with_inputs(restored, restored_session, tick, shared_inputs):
			push_error("netstate_restore_equivalence failed restored resync tick %d" % tick)
			return false
		if interval > 0 and tick > 0 and tick % interval == 0:
			var pre := _measure_debug_numeric(authoritative, restored, player_ids, pos_epsilon, basis_epsilon)
			if bool(pre.get("failed", false)):
				push_error("netstate_restore_equivalence pre-resync mismatch tick=%d player=%d pos_delta=%.9f vel_delta=%.9f up_delta=%.12f dist_delta=%.9f" % [
					tick,
					int(pre.get("fail_player", -1)),
					float(pre.get("fail_pos_delta", 0.0)),
					float(pre.get("fail_vel_delta", 0.0)),
					float(pre.get("fail_up_delta", 0.0)),
					float(pre.get("fail_dist_delta", 0.0))])
				return false
			if float(pre.get("max_pos_delta", 0.0)) > max_pre_pos:
				max_pre_pos = float(pre.get("max_pos_delta", 0.0))
				max_pre_tick = tick
				max_pre_player = int(pre.get("max_pos_player", -1))
			_maybe_print_details("MXT_NETSTATE_RESTORE_RESYNC_PRE_DETAIL", authoritative, restored, tick, pre, detail_threshold)
			var snapshot := authoritative.get_state_data(tick)
			var compressed := snapshot.compress(FileAccess.COMPRESSION_ZSTD)
			var payload_size := compressed.size() if !compressed.is_empty() else snapshot.size()
			if snapshot.size() > max_raw:
				max_raw = snapshot.size()
			if payload_size > max_zstd:
				max_zstd = payload_size
				max_tick = tick
			restored.set_state_data(tick, snapshot)
			restored.load_state(tick)
			var post := _measure_debug_numeric(authoritative, restored, player_ids, pos_epsilon, basis_epsilon)
			if bool(post.get("failed", false)):
				push_error("netstate_restore_equivalence post-resync mismatch tick=%d player=%d pos_delta=%.9f vel_delta=%.9f up_delta=%.12f dist_delta=%.9f" % [
					tick,
					int(post.get("fail_player", -1)),
					float(post.get("fail_pos_delta", 0.0)),
					float(post.get("fail_vel_delta", 0.0)),
					float(post.get("fail_up_delta", 0.0)),
					float(post.get("fail_dist_delta", 0.0))])
				return false
			if float(post.get("max_pos_delta", 0.0)) > max_post_pos:
				max_post_pos = float(post.get("max_pos_delta", 0.0))
				max_post_tick = tick
				max_post_player = int(post.get("max_pos_player", -1))
			_maybe_print_details("MXT_NETSTATE_RESTORE_RESYNC_POST_DETAIL", authoritative, restored, tick, post, detail_threshold)
			samples += 1
	print("MXT_NETSTATE_RESTORE_RESYNC_OK end_tick=", end_tick,
		" interval=", interval,
		" samples=", samples,
		" max_raw=", max_raw,
		" max_zstd=", max_zstd,
		" max_zstd_tick=", max_tick,
		" max_pre_pos_delta=", max_pre_pos,
		" max_pre_tick=", max_pre_tick,
		" max_pre_player=", max_pre_player,
		" max_post_pos_delta=", max_post_pos,
		" max_post_tick=", max_post_tick,
		" max_post_player=", max_post_player)
	return true

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var track_path := _arg_value(args, "--track", DEFAULT_TRACK)
	var car_props_path := _arg_value(args, "--car-props", DEFAULT_CAR_PROPS)
	var cars := _arg_int(args, "--cars", 100)
	var humans := clampi(_arg_int(args, "--humans", 100), 0, cars)
	var snapshot_tick := _arg_int(args, "--snapshot-tick", 3600)
	var verify_frames := _arg_int(args, "--verify-frames", 240)
	var compare_every := maxi(1, _arg_int(args, "--compare-every", 60))
	var pos_epsilon := float(_arg_value(args, "--pos-epsilon", "0.02"))
	var basis_epsilon := float(_arg_value(args, "--basis-epsilon", "0.0002"))
	var detail_threshold := float(_arg_value(args, "--detail-threshold", "-1.0"))
	var raw_load_baseline := _arg_bool(args, "--raw-load-baseline", false)
	var resync_interval := _arg_int(args, "--resync-interval", 0)

	var track_bytes := FileAccess.get_file_as_bytes(track_path)
	var car_bytes := FileAccess.get_file_as_bytes(car_props_path)
	if track_bytes.is_empty() or car_bytes.is_empty() or cars <= 0:
		push_error("netstate_restore_equivalence missing track/car data or invalid car count")
		quit(1)
		return

	var player_ids: Array = []
	var cpu_flags: Array = []
	for i in range(cars):
		player_ids.append(1000 + i)
		cpu_flags.append(i >= humans)

	var authoritative := _make_sim(track_bytes, car_bytes, cars, humans, player_ids, cpu_flags)
	var restored := _make_sim(track_bytes, car_bytes, cars, humans, player_ids, cpu_flags)
	var authoritative_session := _make_session(player_ids, cpu_flags)
	var restored_session := _make_session(player_ids, cpu_flags)
	var shared_inputs_by_tick := {}

	if resync_interval > 0:
		if !_run_resync_series(authoritative, restored, authoritative_session, restored_session, player_ids, humans, snapshot_tick, resync_interval, pos_epsilon, basis_epsilon, detail_threshold):
			quit(1)
			return
		root.remove_child(authoritative)
		root.remove_child(restored)
		authoritative.free()
		restored.free()
		quit()
		return

	for tick in range(snapshot_tick + 1):
		var shared_inputs := _make_inputs(authoritative, tick, humans, player_ids)
		shared_inputs_by_tick[tick] = shared_inputs
		if !_tick_sim_with_inputs(authoritative, authoritative_session, tick, shared_inputs):
			push_error("netstate_restore_equivalence failed authoritative tick %d" % tick)
			quit(1)
			return
		if raw_load_baseline:
			if !_tick_sim_with_inputs(restored, restored_session, tick, shared_inputs):
				push_error("netstate_restore_equivalence failed raw baseline tick %d" % tick)
				quit(1)
				return

	var snapshot := PackedByteArray()
	if raw_load_baseline:
		restored.load_state(snapshot_tick)
	else:
		snapshot = authoritative.get_state_data(snapshot_tick)
		restored.set_state_data(snapshot_tick, snapshot)
		restored.load_state(snapshot_tick)

	var restored_snapshot := restored.get_state_data(snapshot_tick)
	if !raw_load_baseline and !_bytes_equal(snapshot, restored_snapshot):
		var diff := _first_byte_diff(snapshot, restored_snapshot)
		print("MXT_NETSTATE_RESTORE_EQUIV_NOTE immediate_reserialize_diff raw_a=", snapshot.size(),
			" raw_b=", restored_snapshot.size(),
			" first_diff=", diff,
			" a=", int(snapshot[diff]) if diff >= 0 and diff < snapshot.size() else -1,
			" b=", int(restored_snapshot[diff]) if diff >= 0 and diff < restored_snapshot.size() else -1)
		if !_compare_visible_state(authoritative, restored, cars, player_ids, snapshot_tick, pos_epsilon, basis_epsilon):
			push_error("netstate_restore_equivalence immediate visible mismatch raw_a=%d raw_b=%d first_diff=%d a=%d b=%d" % [
			snapshot.size(), restored_snapshot.size(), diff,
			int(snapshot[diff]) if diff >= 0 and diff < snapshot.size() else -1,
			int(restored_snapshot[diff]) if diff >= 0 and diff < restored_snapshot.size() else -1])
			quit(1)
			return
		if !_compare_debug_numeric(authoritative, restored, player_ids, snapshot_tick, pos_epsilon, basis_epsilon, detail_threshold):
			quit(1)
			return

	if raw_load_baseline:
		if !_compare_debug_numeric(authoritative, restored, player_ids, snapshot_tick, pos_epsilon, basis_epsilon, detail_threshold):
			quit(1)
			return
	for tick in range(snapshot_tick + 1, snapshot_tick + verify_frames + 1):
		var shared_inputs := _make_inputs(authoritative, tick, humans, player_ids)
		shared_inputs_by_tick[tick] = shared_inputs
		_store_inputs(authoritative_session, tick, shared_inputs)
		_store_inputs(restored_session, tick, shared_inputs)
		if !authoritative_session.tick_server_frame(authoritative, tick):
			push_error("netstate_restore_equivalence failed authoritative continuation tick %d" % tick)
			quit(1)
			return
		if !restored_session.tick_server_frame(restored, tick):
			push_error("netstate_restore_equivalence failed restored continuation tick %d" % tick)
			quit(1)
			return
		if (tick - snapshot_tick) % compare_every == 0:
			if !_compare_debug_numeric(authoritative, restored, player_ids, tick, pos_epsilon, basis_epsilon, detail_threshold):
				quit(1)
				return

	var compressed := snapshot.compress(FileAccess.COMPRESSION_ZSTD)
	var payload_size := compressed.size() if !compressed.is_empty() else snapshot.size()
	print("MXT_NETSTATE_RESTORE_EQUIV_OK tick=", snapshot_tick,
		" verify_frames=", verify_frames,
		" cars=", cars,
		" humans_with_cpu_inputs=", humans,
		" raw=", snapshot.size(),
		" zstd=", payload_size)
	root.remove_child(authoritative)
	root.remove_child(restored)
	authoritative.free()
	restored.free()
	quit()
