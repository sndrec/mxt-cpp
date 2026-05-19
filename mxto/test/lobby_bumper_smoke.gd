extends SceneTree

const MAIN_SCENE := "res://main.tscn"
const TRACK_NAME := "Aeropolis - Multiplex"
const PlayerInputClass := preload("res://player/player_input.gd")

var _log: FileAccess

func _init() -> void:
	var frame_limit := _get_frame_limit()
	_log = FileAccess.open("user://lobby_bumper_smoke.log", FileAccess.WRITE_READ)
	if _log != null:
		_log.seek_end()
		_log.store_line("")
		_log.store_line("start lobby bumper smoke frames=%s" % frame_limit)
		_log.flush()
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	_log_line("MXT_LOBBY_BUMPER_SETUP loaded tracks=%s" % main.tracks.size())
	if main.port_field != null:
		main.port_field.text = "27161"

	main.call("_on_start_button_pressed")
	await process_frame
	await process_frame
	for child in main.get_children():
		if child is Timer and is_equal_approx(child.wait_time, 3.0):
			child.stop()
	_log_line("MXT_LOBBY_BUMPER_SETUP host is_server=%s ids=%s" % [main.network_manager.is_server, main.network_manager.player_ids])

	for i in range(main.tracks.size()):
		if String(main.tracks[i].get("name", "")) == TRACK_NAME:
			main.lobby_track_selector.select(i)
			main.lobby_grand_prix_track_sequence.clear()
			main.lobby_grand_prix_track_sequence.append(i)
			break
	if main.lobby_game_mode_choice != null:
		main.lobby_game_mode_choice.select(0)
	if main.lobby_bumpers_toggle != null:
		main.lobby_bumpers_toggle.button_pressed = true
	main.call("_refresh_lobby_race_options")
	await process_frame
	_log_line("MXT_LOBBY_BUMPER_SETUP options=%s" % main.network_manager.race_options)

	main.call("_on_start_race_button_pressed")
	_log_line("MXT_LOBBY_BUMPER_SETUP start pressed race_active=%s" % main.network_manager.race_active)
	main.set_physics_process(false)
	var accel_input := PlayerInputClass.new()
	accel_input.accelerate = 1.0
	var accel_bytes: PackedByteArray = accel_input.serialize()
	if main.game_sim != null:
		main.game_sim.set_render_profile_enabled(true)
	if main.server_game_sim != null:
		main.server_game_sim.set_render_profile_enabled(true)
	_print_state(main, "start")
	var frames := 0
	var saw_early_bumper := false
	var saw_lap2_bumper := false
	while frames < frame_limit:
		await physics_frame
		frames += 1
		main.call("_simulate_host_frame", accel_bytes)
		if main.game_sim != null:
			main.game_sim.render_gamesim()
		if main.server_game_sim != null and main.server_game_sim.has_method("get_bumper_debug_string"):
			var fields := _parse_debug_fields(main.server_game_sim.get_bumper_debug_string())
			var active := int(fields.get("active", "0"))
			var leader_lap := int(fields.get("leader_lap", "0"))
			if active > 0 and leader_lap < 2:
				saw_early_bumper = true
			if active > 0 and leader_lap >= 2:
				saw_lap2_bumper = true
		if frames % 120 == 0:
			_print_state(main, str(frames))
	_log_line("MXT_LOBBY_BUMPER_DONE frames=%s" % frames)
	if saw_early_bumper:
		push_error("lobby_bumper_smoke saw an active bumper before visible lap 2")
		quit(1)
		return
	if frame_limit >= 5000 and !saw_lap2_bumper:
		push_error("lobby_bumper_smoke did not spawn any bumpers after visible lap 2")
		quit(1)
		return
	quit()

func _get_frame_limit() -> int:
	var args := OS.get_cmdline_user_args()
	args.append_array(OS.get_cmdline_args())
	for i in range(args.size()):
		if args[i] == "--frames" and i + 1 < args.size():
			return max(1, int(args[i + 1]))
		if args[i].begins_with("--frames="):
			return max(1, int(args[i].substr(9)))
	return 1200

func _log_line(message: String) -> void:
	print(message)
	if _log != null:
		_log.store_line(message)
		_log.flush()

func _print_state(main: Node, label: String) -> void:
	var local_debug := "no-local"
	var server_debug := "no-server"
	var local_bumper0 := "no-local"
	var server_bumper0 := "no-server"
	var local_profile := "no-local"
	var server_profile := "no-server"
	if main.game_sim != null and main.game_sim.has_method("get_bumper_debug_string"):
		local_debug = main.game_sim.get_bumper_debug_string()
		local_bumper0 = str(main.game_sim.get_car_render_transform(1).origin)
		local_profile = main.game_sim.get_render_profile_string()
	if main.server_game_sim != null and main.server_game_sim.has_method("get_bumper_debug_string"):
		server_debug = main.server_game_sim.get_bumper_debug_string()
		server_bumper0 = str(main.server_game_sim.get_car_render_transform(1).origin)
		server_profile = main.server_game_sim.get_render_profile_string()
	_log_line("MXT_LOBBY_BUMPER_SMOKE frame=%s local=[%s] server=[%s] local_bumper0=%s server_bumper0=%s local_profile=[%s] server_profile=[%s] sim_started=%s server_started=%s race_active=%s" % [
		label,
		local_debug,
		server_debug,
		local_bumper0,
		server_bumper0,
		local_profile,
		server_profile,
		main.game_sim.sim_started if main.game_sim != null else false,
		main.server_game_sim.sim_started if main.server_game_sim != null else false,
		main.network_manager.race_active,
	])

func _parse_debug_fields(debug: String) -> Dictionary:
	var out := {}
	for part in debug.split(" "):
		var idx := part.find("=")
		if idx <= 0:
			continue
		out[part.substr(0, idx)] = part.substr(idx + 1)
	return out
