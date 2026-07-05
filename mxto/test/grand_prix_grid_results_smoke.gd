extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	if main.game_sim == null or !main.game_sim.has_method("set_start_grid_slots"):
		push_error("grand_prix_grid_results_smoke missing native set_start_grid_slots")
		quit(1)
		return
	main.network_manager.race_options = {
		"game_mode": 1,
		"grand_prix_current_track": 0,
		"grand_prix_points": {1: 10, 2: 7, 3: 2},
	}
	var first_grid: PackedInt32Array = main.call("_build_start_grid_slots", [1, 2, 3])
	if first_grid != PackedInt32Array([-1, -1, -1]):
		push_error("first Grand Prix race should use randomized grid, got %s" % [first_grid])
		quit(1)
		return
	main.network_manager.race_options["grand_prix_current_track"] = 1
	var standings_grid: PackedInt32Array = main.call("_build_start_grid_slots", [1, 2, 3])
	if standings_grid != PackedInt32Array([2, 1, 0]):
		push_error("Grand Prix standings grid mismatch, got %s" % [standings_grid])
		quit(1)
		return
	main.network_manager.finish_order = [2, 1, 3]
	main.network_manager.player_finish_times = {2: 420, 1: 480, 3: 600}
	main.call("_show_race_results_summary")
	if main.race_results_overlay == null or !main.race_results_overlay.visible:
		push_error("race results overlay did not become visible")
		quit(1)
		return
	main.network_manager.race_player_ids = [1, 2]
	main.network_manager.race_cpu_player_ids = [3]
	main.network_manager.is_server = true
	main.network_manager.player_finish_placements = {1: 2, 2: 3, 3: 1}
	main.network_manager.player_finish_times = {1: 480}
	main.network_manager.finish_order = [3, 1, 2]
	main.network_manager.server_tick = 900
	main.network_manager.race_options = {
		"game_mode": 1,
		"grand_prix_current_track": 2,
		"grand_prix_points": {1: 0, 2: 0, 3: 0},
	}
	main.call("_record_grand_prix_race_results", main.game_sim)
	var points: Dictionary = main.network_manager.race_options.get("grand_prix_points", {})
	if int(points.get(3, -1)) != 3 or int(points.get(1, -1)) != 2 or int(points.get(2, -1)) != 1:
		push_error("Grand Prix points should use all racers, got %s" % [points])
		quit(1)
		return
	if String(main.call("_format_race_time", 900, 420)) != "0:08.000":
		push_error("race result formatting should respect the race start tick")
		quit(1)
		return
	main.network_manager.player_finish_times.clear()
	main.network_manager.player_finish_placements.clear()
	main.network_manager.finish_order.clear()
	main.network_manager.send_player_finished(1, 720)
	if int(main.network_manager.player_finish_placements.get(1, -1)) != 1:
		push_error("finish placement should be assigned from finish tick order")
		quit(1)
		return
	var race_text: String = main.call("_format_race_results_text")
	if race_text.find("1st") == -1:
		push_error("race results text did not use finish tick placement: %s" % race_text)
		quit(1)
		return
	main.network_manager.set_player_finished(1, 780)
	if int(main.network_manager.player_finish_times.get(1, -1)) != 720 or int(main.network_manager.player_finish_placements.get(1, -1)) != 1:
		push_error("duplicate finish should not rewrite official time or placement, times=%s places=%s" % [
			main.network_manager.player_finish_times,
			main.network_manager.player_finish_placements,
		])
		quit(1)
		return
	main.network_manager.player_finish_times = {1: 600, 2: 500}
	main.network_manager.player_finish_placements = {1: 1, 2: 1}
	main.network_manager._rebuild_finish_order_from_placements()
	if int(main.network_manager.player_finish_placements.get(2, -1)) != 1 or int(main.network_manager.player_finish_placements.get(1, -1)) != 2:
		push_error("finish placements should be rebuilt from finish time, got %s order=%s" % [
			main.network_manager.player_finish_placements,
			main.network_manager.finish_order,
		])
		quit(1)
		return
	main.network_manager.player_finish_times = {1: 600, 2: 500}
	main.network_manager.player_finish_placements = {1: 1, 2: 1}
	var normalized_places: Dictionary = main.call("_build_final_race_place_map", main.game_sim, [1, 2, 3])
	if int(normalized_places.get(2, -1)) != 1 or int(normalized_places.get(1, -1)) != 2:
		push_error("final race place map should not preserve duplicate places, got %s" % [normalized_places])
		quit(1)
		return
	var nm: NetworkManager = main.network_manager
	var race_started_callable := Callable(main, "_on_network_race_started")
	var race_started_was_connected := nm.race_started.is_connected(race_started_callable)
	if race_started_was_connected:
		nm.race_started.disconnect(race_started_callable)
	nm.is_server = true
	nm.listen_server = false
	nm.player_ids = [1, 2, 3]
	nm.ready_players.assign([1, 2, 3])
	nm.start_sync_active = true
	nm.start_sync_scheduled = true
	nm.start_race("sha256:test", [], {})
	if !nm.ready_players.is_empty():
		push_error("Grand Prix next race start must clear stale ready players, got %s" % [nm.ready_players])
		quit(1)
		return
	if nm.start_sync_active or nm.start_sync_scheduled:
		push_error("Grand Prix next race start must reset stale start-sync state")
		quit(1)
		return
	nm.netcode_session.configure([1], [false], 1)
	nm.netcode_session.store_local_input(0, nm.NEUTRAL_INPUT_BYTES)
	var phase_zero_packet: PackedByteArray = nm.netcode_session.build_local_input_packet(0, 1, 0)
	nm.server_netcode_session.configure([1], [false], 1)
	var stale_input_stats: Dictionary = nm.server_netcode_session.store_pending_input_packet(1, 0, phase_zero_packet, 0.0, 0.0, 1)
	if !bool(stale_input_stats.get("stale", false)):
		push_error("phase-mismatched client input packet should be dropped as stale")
		quit(1)
		return
	var current_input_stats: Dictionary = nm.server_netcode_session.store_pending_input_packet(1, 0, phase_zero_packet, 0.0, 0.0, 0)
	if bool(current_input_stats.get("stale", false)) or !bool(current_input_stats.get("valid", false)):
		push_error("phase-matched client input packet should be valid, got %s" % [current_input_stats])
		quit(1)
		return
	nm.server_netcode_session.configure([1], [false], 1)
	nm.server_netcode_session.store_authoritative_input(0, 1, nm.NEUTRAL_INPUT_BYTES)
	var phase_one_auth_packet: PackedByteArray = nm.server_netcode_session.build_authoritative_input_packet(0, 1, 1)
	nm.netcode_session.configure([1], [false], 1)
	var stale_auth_stats: Dictionary = nm.netcode_session.store_authoritative_input_packet(phase_one_auth_packet, 0, 0)
	if !bool(stale_auth_stats.get("stale", false)):
		push_error("phase-mismatched authoritative packet should be dropped as stale, packet=%s stats=%s" % [phase_one_auth_packet, stale_auth_stats])
		quit(1)
		return
	var current_auth_stats: Dictionary = nm.netcode_session.store_authoritative_input_packet(phase_one_auth_packet, 1, 0)
	if bool(current_auth_stats.get("stale", false)) or !bool(current_auth_stats.get("valid", false)):
		push_error("phase-matched authoritative packet should be valid, got %s" % [current_auth_stats])
		quit(1)
		return
	var recv_nm := NetworkManager.new()
	recv_nm.race_active = true
	recv_nm.race_netplay_phase = 1
	recv_nm.is_server = false
	recv_nm.listen_server = false
	recv_nm.player_ids = [1]
	recv_nm.netcode_session.configure([1], [false], 1)
	recv_nm._server_broadcast_flat(recv_nm._pack_authoritative_input_tick(0, -1), phase_one_auth_packet, 0)
	if recv_nm.clients_server_tick != 1 or recv_nm.clients_target_tick != 0:
		push_error("authoritative input broadcast should advance input tick without timing fields, server=%d target=%d" % [recv_nm.clients_server_tick, recv_nm.clients_target_tick])
		quit(1)
		return
	var listen_nm := NetworkManager.new()
	listen_nm.race_active = true
	listen_nm.race_netplay_phase = 1
	listen_nm.is_server = true
	listen_nm.listen_server = true
	listen_nm.target_tick = 7
	listen_nm.max_ahead_from_server = 3.0
	listen_nm.player_ids = [1]
	listen_nm.netcode_session.configure([1], [false], 1)
	listen_nm._server_broadcast_flat(listen_nm._pack_authoritative_input_tick(0, -1), phase_one_auth_packet, 0)
	if listen_nm.clients_server_tick != 1 or listen_nm.clients_target_tick != 7:
		push_error("listen authoritative input broadcast should preserve local target advancement, server=%d target=%d" % [listen_nm.clients_server_tick, listen_nm.clients_target_tick])
		quit(1)
		return
	if race_started_was_connected:
		nm.race_started.connect(race_started_callable)
	print("MXT_GRAND_PRIX_GRID_RESULTS_SMOKE grid=", standings_grid)
	quit(0)
