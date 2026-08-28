class_name GrandPrixSessionController
extends Node

var network_manager: NetworkManager


func initialize(in_network_manager: NetworkManager) -> void:
	network_manager = in_network_manager


func initialize_race_state(
		configuration: MxtRaceConfiguration,
		state: MxtRaceSessionState,
		roster: Array) -> MxtRaceSessionState:
	var initialized := state.copy()
	if configuration.game_mode != 1:
		return initialized
	initialized.clear_grand_prix_points()
	for id in roster:
		initialized.set_grand_prix_points(int(id), 0)
	initialized.grand_prix_current_track = 0
	initialized.grand_prix_recorded_track = -1
	initialized.grand_prix_eliminated_ids = PackedInt64Array()
	initialized.clear_grand_prix_ko_energy_bonuses()
	return initialized


func apply_eliminations(state: MxtRaceSessionState) -> void:
	var eliminated_ids: PackedInt64Array = state.grand_prix_eliminated_ids
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
	var state := network_manager.race_state.copy()
	var current_track_index: int = state.grand_prix_current_track
	if state.grand_prix_recorded_track == current_track_index:
		return
	var race_racers := network_manager.get_simulation_roster()
	var racer_count := race_racers.size()
	var place_by_id := _build_final_race_place_map(sim, race_racers)
	var finish_tick_by_id := _build_final_race_finish_tick_map(place_by_id)
	network_manager.race_results.send_final_race_results(place_by_id, finish_tick_by_id)
	for id_value in race_racers:
		var id := int(id_value)
		var total := state.get_grand_prix_points(id)
		var place := int(_lookup_id_value(place_by_id, id, 0))
		if place > 0:
			total += maxi(0, racer_count - place + 1)
		state.set_grand_prix_points(id, total)
	var eliminated_ids: PackedInt64Array = state.grand_prix_eliminated_ids
	if !network_manager.is_vehicle_restore_enabled():
		for id_value in network_manager.race_results.player_eliminations.keys():
			var id := int(id_value)
			if !eliminated_ids.has(id):
				eliminated_ids.append(id)
	state.grand_prix_eliminated_ids = eliminated_ids
	_capture_ko_energy_bonuses(sim, state)
	state.grand_prix_recorded_track = current_track_index
	network_manager.race_state = state
	network_manager.send_race_state(
		network_manager.race_configuration,
		network_manager.race_track_evidence,
		state)


func finish_or_advance(finish_sim: GameSim) -> void:
	record_race_results(finish_sim)
	if !network_manager.is_grand_prix_enabled():
		network_manager.send_end_race()
		return
	var state := network_manager.race_state.copy()
	var next_index: int = state.grand_prix_current_track + 1
	if next_index >= network_manager.race_track_evidence.count() or !_has_active_human_racer(state):
		network_manager.send_end_race()
		return
	state.grand_prix_current_track = next_index
	var next_track_id := network_manager.race_track_evidence.get_content_id(next_index)
	var next_roster := _build_next_roster(state)
	if next_roster == null:
		network_manager.send_end_race()
		return
	_assign_next_rosters(state)
	state = network_manager.reserve_next_race_netplay_state(state)
	state.spawn_seed = randi()
	network_manager.send_end_race(
		next_track_id,
		next_roster,
		network_manager.race_configuration,
		network_manager.race_track_evidence,
		state)


func _lookup_id_value(dictionary: Dictionary, id: int, fallback):
	if dictionary.has(id):
		return dictionary[id]
	var id_string := str(id)
	return dictionary[id_string] if dictionary.has(id_string) else fallback


func _capture_ko_energy_bonuses(sim: GameSim, state: MxtRaceSessionState) -> void:
	state.clear_grand_prix_ko_energy_bonuses()
	if sim == null or !sim.has_method("get_player_ko_energy_bonus"):
		return
	for id_value in network_manager.get_simulation_roster():
		var id := int(id_value)
		state.set_grand_prix_ko_energy_bonus(id, float(sim.get_player_ko_energy_bonus(id)))


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


func _build_next_roster(state: MxtRaceSessionState) -> MxtRaceRoster:
	var eliminated_ids: PackedInt64Array = state.grand_prix_eliminated_ids
	var active_ids := network_manager.player_ids.duplicate(true)
	active_ids.append_array(network_manager.lobby_settings.cpu_player_ids)
	for index in range(active_ids.size() - 1, -1, -1):
		if eliminated_ids.has(int(active_ids[index])):
			active_ids.remove_at(index)
	return network_manager.lobby_settings.build_race_roster(active_ids)


func _assign_next_rosters(state: MxtRaceSessionState) -> void:
	var eliminated_ids: PackedInt64Array = state.grand_prix_eliminated_ids
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
	state.human_ids = PackedInt64Array(human_ids)
	state.cpu_ids = PackedInt64Array(cpu_ids)
	state.spectator_ids = PackedInt64Array(spectator_ids)


func _has_active_human_racer(state: MxtRaceSessionState) -> bool:
	var eliminated_ids: PackedInt64Array = state.grand_prix_eliminated_ids
	for id_value in network_manager.player_ids:
		if !eliminated_ids.has(int(id_value)):
			return true
	return false
