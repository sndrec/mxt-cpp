class_name GrandPrixSessionController
extends Node

var network_manager: NetworkManager


func initialize(in_network_manager: NetworkManager) -> void:
	network_manager = in_network_manager


func initialize_race_state(
		configuration: MxtRaceConfiguration,
		options: Dictionary,
		roster: Array) -> Dictionary:
	var initialized := options.duplicate(true)
	if configuration.game_mode != 1:
		return initialized
	var points := {}
	for id in roster:
		points[int(id)] = 0
	initialized["grand_prix_current_track"] = 0
	initialized["grand_prix_points"] = points
	initialized["grand_prix_ko_energy_bonuses"] = {}
	initialized["grand_prix_eliminated_ids"] = []
	return initialized


func apply_eliminations(options: Dictionary) -> void:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	for eliminated_id in eliminated_ids:
		var id := int(eliminated_id)
		if network_manager.player_ids.has(id):
			network_manager.player_ids.erase(id)
			if !network_manager.spectator_ids.has(id):
				network_manager.spectator_ids.append(id)
		if network_manager.lobby_settings.cpu_player_ids.has(id):
			network_manager.lobby_settings.cpu_player_ids.erase(id)
			network_manager.lobby_settings.remove_player(id)


func record_race_results(sim: GameSim) -> void:
	if !network_manager.is_server or !network_manager.is_grand_prix_enabled():
		return
	var options := network_manager.race_state.duplicate(true)
	var current_track_index := int(options.get("grand_prix_current_track", 0))
	if int(options.get("grand_prix_recorded_track", -1)) == current_track_index:
		return
	var points: Dictionary = options.get("grand_prix_points", {})
	var race_racers := network_manager.get_simulation_roster()
	var racer_count := race_racers.size()
	var place_by_id := _build_final_race_place_map(sim, race_racers)
	var finish_tick_by_id := _build_final_race_finish_tick_map(place_by_id)
	network_manager.race_results.send_final_race_results(place_by_id, finish_tick_by_id)
	for id_value in race_racers:
		var id := int(id_value)
		var total := int(_lookup_id_value(points, id, 0))
		var place := int(_lookup_id_value(place_by_id, id, 0))
		if place > 0:
			total += maxi(0, racer_count - place + 1)
		points[id] = total
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	if !network_manager.is_vehicle_restore_enabled():
		for id_value in network_manager.race_results.player_eliminations.keys():
			var id := int(id_value)
			if !eliminated_ids.has(id):
				eliminated_ids.append(id)
	options["grand_prix_points"] = points
	options["grand_prix_eliminated_ids"] = eliminated_ids
	options["grand_prix_ko_energy_bonuses"] = _capture_ko_energy_bonuses(sim)
	options["grand_prix_recorded_track"] = current_track_index
	network_manager.race_state = options
	network_manager.send_race_state(
		network_manager.race_configuration,
		network_manager.race_track_evidence,
		options)


func finish_or_advance(finish_sim: GameSim) -> void:
	record_race_results(finish_sim)
	if !network_manager.is_grand_prix_enabled():
		network_manager.send_end_race()
		return
	var options := network_manager.race_state.duplicate(true)
	var next_index := int(options.get("grand_prix_current_track", 0)) + 1
	if next_index >= network_manager.race_track_evidence.count() or !_has_active_human_racer(options):
		network_manager.send_end_race()
		return
	options["grand_prix_current_track"] = next_index
	var next_track_id := network_manager.race_track_evidence.get_content_id(next_index)
	var next_roster := _build_next_roster(options)
	if next_roster == null:
		network_manager.send_end_race()
		return
	var next_rosters := _build_next_rosters(options)
	options["race_human_ids"] = (next_rosters["human_ids"] as Array).duplicate(true)
	options["race_cpu_ids"] = (next_rosters["cpu_ids"] as Array).duplicate(true)
	options["race_spectator_ids"] = (next_rosters["spectator_ids"] as Array).duplicate(true)
	options = network_manager.reserve_next_race_netplay_state(options)
	options["spawn_seed"] = randi()
	network_manager.send_end_race(
		next_track_id,
		next_roster,
		network_manager.race_configuration,
		network_manager.race_track_evidence,
		options)


func _lookup_id_value(dictionary: Dictionary, id: int, fallback):
	if dictionary.has(id):
		return dictionary[id]
	var id_string := str(id)
	return dictionary[id_string] if dictionary.has(id_string) else fallback


func _capture_ko_energy_bonuses(sim: GameSim) -> Dictionary:
	var bonuses := {}
	if sim == null or !sim.has_method("get_player_ko_energy_bonus"):
		return bonuses
	for id_value in network_manager.get_simulation_roster():
		var id := int(id_value)
		bonuses[id] = float(sim.get_player_ko_energy_bonus(id))
	return bonuses


func _build_final_race_place_map(sim: GameSim, race_racers: Array) -> Dictionary:
	var place_by_id := {}
	var placement_rows := []
	for id_value in race_racers:
		var id := int(id_value)
		if network_manager.race_results.player_dnfs.has(id):
			continue
		var place := int(_lookup_id_value(
			network_manager.race_results.player_finish_placements, id, 0))
		if place > 0:
			var finish_tick := int(_lookup_id_value(
				network_manager.race_results.player_finish_times, id, 0x7fffffff))
			placement_rows.append([place, finish_tick, id])
	placement_rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		if int(a[1]) != int(b[1]):
			return int(a[1]) < int(b[1])
		return int(a[2]) < int(b[2]))
	for row in placement_rows:
		place_by_id[int(row[2])] = place_by_id.size() + 1
	var finish_rows := []
	for id_value in race_racers:
		var id := int(id_value)
		if place_by_id.has(id) or network_manager.race_results.player_dnfs.has(id):
			continue
		var finish_tick := int(_lookup_id_value(
			network_manager.race_results.player_finish_times, id, -1))
		if finish_tick >= 0:
			finish_rows.append([finish_tick, id])
	finish_rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		return int(a[1]) < int(b[1]))
	for row in finish_rows:
		place_by_id[int(row[1])] = place_by_id.size() + 1
	if sim == null or !sim.has_method("get_race_order"):
		return place_by_id
	for id_value in sim.get_race_order():
		var id := int(id_value)
		if !race_racers.has(id) or place_by_id.has(id):
			continue
		if (
				network_manager._disconnected_during_race.has(id)
				or network_manager.race_results.player_eliminations.has(id)
				or network_manager.race_results.player_dnfs.has(id)):
			continue
		place_by_id[id] = place_by_id.size() + 1
	return place_by_id


func _build_final_race_finish_tick_map(place_by_id: Dictionary) -> Dictionary:
	var finish_tick_by_id := {}
	for id_value in place_by_id:
		var id := int(id_value)
		var finish_tick := int(_lookup_id_value(
			network_manager.race_results.player_finish_times, id, -1))
		if finish_tick >= 0:
			finish_tick_by_id[id] = finish_tick
	return finish_tick_by_id


func _build_next_roster(options: Dictionary) -> MxtRaceRoster:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var active_ids := network_manager.player_ids.duplicate(true)
	active_ids.append_array(network_manager.lobby_settings.cpu_player_ids)
	for index in range(active_ids.size() - 1, -1, -1):
		if eliminated_ids.has(int(active_ids[index])):
			active_ids.remove_at(index)
	return network_manager.lobby_settings.build_race_roster(active_ids)


func _build_next_rosters(options: Dictionary) -> Dictionary:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	var human_ids := []
	for id_value in network_manager.player_ids:
		var id := int(id_value)
		if !eliminated_ids.has(id):
			human_ids.append(id)
	var cpu_ids := []
	for id_value in network_manager.lobby_settings.cpu_player_ids:
		var id := int(id_value)
		if !eliminated_ids.has(id):
			cpu_ids.append(id)
	var spectator_ids := network_manager.spectator_ids.duplicate(true)
	for id_value in network_manager.waiting_peers:
		var id := int(id_value)
		if !spectator_ids.has(id):
			spectator_ids.append(id)
	return {
		"human_ids": human_ids,
		"cpu_ids": cpu_ids,
		"spectator_ids": spectator_ids,
	}


func _has_active_human_racer(options: Dictionary) -> bool:
	var eliminated_ids: Array = options.get("grand_prix_eliminated_ids", [])
	for id_value in network_manager.player_ids:
		if !eliminated_ids.has(int(id_value)):
			return true
	return false
