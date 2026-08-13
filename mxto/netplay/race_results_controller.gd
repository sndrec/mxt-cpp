class_name RaceResultsController
extends Node

signal race_event(event_type: String, actor_id: int, target_id: int, tick: int, value: int)
signal player_dnf_recorded(player_id: int)

const RACE_PHASE_TICK_BIT := 0x80000000
const RACE_PHASE_TICK_MASK := 0x7fffffff
const STICKER_COOLDOWN_MSEC := 500

var net_race_finish_time := -1
var player_finish_times := {}
var player_finish_placements := {}
var finish_order: Array = []
var player_eliminations := {}
var player_dnfs := {}
var race_force_end_deadline_tick := -1

var race_active := false
var race_phase := 0
var is_server := false
var network_active := false
var current_race_tick := 0
var sticker_cooldown_msec := {}

func set_context(active: bool, phase: int, server: bool, active_network: bool) -> void:
	race_active = active
	race_phase = phase & 1
	is_server = server
	network_active = active_network

func set_race_tick(tick: int) -> void:
	current_race_tick = tick

func reset() -> void:
	net_race_finish_time = -1
	player_finish_times.clear()
	player_finish_placements.clear()
	finish_order.clear()
	player_eliminations.clear()
	player_dnfs.clear()
	race_force_end_deadline_tick = -1
	sticker_cooldown_msec.clear()
	current_race_tick = 0

@rpc("authority", "call_remote", "reliable", 7)
func set_race_finish_time(phase: int, time: int) -> void:
	if !race_active or !_accept_phase(phase):
		return
	net_race_finish_time = time

func send_race_finish_time(time: int) -> void:
	if !is_server:
		return
	set_race_finish_time.rpc(race_phase, time)
	set_race_finish_time(race_phase, time)

@rpc("authority", "call_local", "reliable")
func set_player_finished(player_id: int, packed_tick: int) -> void:
	if race_active and !_accept_phase(_unpack_phase(packed_tick)):
		return
	var tick := _unpack_tick(packed_tick)
	if player_finish_times.has(player_id) or player_dnfs.has(player_id):
		return
	player_eliminations.erase(player_id)
	player_finish_times[player_id] = tick
	rebuild_finish_order()
	race_event.emit("finish", player_id, -1, tick, int(player_finish_placements.get(player_id, 0)))

func send_player_finished(player_id: int, tick: int) -> void:
	if player_finish_times.has(player_id) or !is_server:
		return
	var packed_tick := _pack_tick(tick)
	set_player_finished.rpc(player_id, packed_tick)
	set_player_finished(player_id, packed_tick)

func record_player_finished(player_id: int, tick: int) -> void:
	if player_finish_times.has(player_id):
		return
	set_player_finished(player_id, _pack_tick(tick))

@rpc("authority", "call_local", "reliable")
func set_player_dnf(player_id: int, packed_tick: int, reason: String = "") -> void:
	if race_active and !_accept_phase(_unpack_phase(packed_tick)):
		return
	var tick := _unpack_tick(packed_tick)
	if player_finish_times.has(player_id):
		return
	var is_new := !player_dnfs.has(player_id)
	player_dnfs[player_id] = {
		"tick": tick,
		"reason": reason,
	}
	player_eliminations.erase(player_id)
	player_dnf_recorded.emit(player_id)
	if is_new:
		race_event.emit("dnf", player_id, -1, tick, 0)

func send_player_dnf(player_id: int, tick: int, reason: String = "") -> void:
	if player_finish_times.has(player_id) or player_dnfs.has(player_id) or !is_server:
		return
	var packed_tick := _pack_tick(tick)
	set_player_dnf.rpc(player_id, packed_tick, reason)
	set_player_dnf(player_id, packed_tick, reason)

func record_player_dnf(player_id: int, tick: int, reason: String = "") -> void:
	if player_finish_times.has(player_id) or player_dnfs.has(player_id):
		return
	set_player_dnf(player_id, _pack_tick(tick), reason)

@rpc("authority", "call_local", "reliable")
func set_race_force_end_deadline(phase: int, deadline_tick: int) -> void:
	if !_accept_phase(phase):
		return
	race_force_end_deadline_tick = deadline_tick

func send_race_force_end_deadline(deadline_tick: int) -> void:
	if !is_server:
		return
	race_force_end_deadline_tick = deadline_tick
	set_race_force_end_deadline.rpc(race_phase, deadline_tick)

@rpc("authority", "call_local", "reliable")
func set_final_race_results(phase: int, placements: Dictionary, finish_ticks: Dictionary) -> void:
	if !_accept_phase(phase):
		return
	for id_value in finish_ticks.keys():
		var player_id := int(id_value)
		if player_dnfs.has(player_id):
			continue
		player_eliminations.erase(player_id)
		if !player_finish_times.has(player_id):
			player_finish_times[player_id] = int(finish_ticks[id_value])
	rebuild_finish_order()
	var next_place := finish_order.size() + 1
	var rows := []
	for id_value in placements.keys():
		var player_id := int(id_value)
		if player_finish_placements.has(player_id):
			continue
		var place := int(placements[id_value])
		if place > 0:
			rows.append([place, player_id])
	rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		return int(a[1]) < int(b[1])
	)
	for row in rows:
		var player_id := int(row[1])
		player_finish_placements[player_id] = next_place
		finish_order.append(player_id)
		next_place += 1

func send_final_race_results(placements: Dictionary, finish_ticks: Dictionary) -> void:
	if !is_server:
		return
	set_final_race_results.rpc(race_phase, placements, finish_ticks)
	set_final_race_results(race_phase, placements, finish_ticks)

func rebuild_finish_order() -> void:
	finish_order.clear()
	var rows := []
	for id_value in player_finish_times.keys():
		var player_id := int(id_value)
		rows.append([int(player_finish_times[id_value]), player_id])
	rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		return int(a[1]) < int(b[1])
	)
	var normalized_placements := {}
	for row in rows:
		var place := finish_order.size() + 1
		var player_id := int(row[1])
		normalized_placements[player_id] = place
		finish_order.append(player_id)
	player_finish_placements = normalized_placements

@rpc("authority", "call_local", "reliable")
func set_player_eliminated(player_id: int, packed_tick: int) -> void:
	if race_active and !_accept_phase(_unpack_phase(packed_tick)):
		return
	var tick := _unpack_tick(packed_tick)
	var is_new := !player_eliminations.has(player_id)
	player_eliminations[player_id] = tick
	if is_new:
		race_event.emit("eliminated", player_id, -1, tick, 0)

func send_player_eliminated(player_id: int, tick: int) -> void:
	if player_eliminations.has(player_id) or !is_server:
		return
	var packed_tick := _pack_tick(tick)
	set_player_eliminated.rpc(player_id, packed_tick)
	set_player_eliminated(player_id, packed_tick)

func record_player_eliminated(player_id: int, tick: int) -> void:
	if player_eliminations.has(player_id):
		return
	set_player_eliminated(player_id, _pack_tick(tick))

@rpc("authority", "call_local", "reliable")
func receive_race_event(event_type: String, actor_id: int, target_id: int, packed_tick: int, value: int) -> void:
	if race_active and !_accept_phase(_unpack_phase(packed_tick)):
		return
	race_event.emit(event_type, actor_id, target_id, _unpack_tick(packed_tick), value)

func send_race_event(event_type: String, actor_id: int, target_id: int, tick: int, value: int) -> void:
	if is_server:
		receive_race_event.rpc(event_type, actor_id, target_id, _pack_tick(tick), value)

@rpc("any_peer", "reliable")
func request_sticker(sticker_index: int) -> void:
	if !race_active:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = multiplayer.get_unique_id()
	var now := Time.get_ticks_msec()
	var last := int(sticker_cooldown_msec.get(sender, 0))
	if now < last + STICKER_COOLDOWN_MSEC:
		return
	sticker_cooldown_msec[sender] = now
	if is_server:
		send_race_event("sticker", sender, -1, current_race_tick, sticker_index)

func send_sticker(sticker_index: int) -> void:
	if is_server or !_has_network_peer():
		request_sticker(sticker_index)
	else:
		request_sticker.rpc_id(1, sticker_index)

func _has_network_peer() -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if is_server:
		return true
	return multiplayer.multiplayer_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED

func _accept_phase(phase: int) -> bool:
	return (phase & 1) == race_phase

func _pack_tick(tick: int) -> int:
	return (tick & RACE_PHASE_TICK_MASK) | (race_phase << 31)

func _unpack_phase(packed_tick: int) -> int:
	return 1 if (packed_tick & RACE_PHASE_TICK_BIT) != 0 else 0

func _unpack_tick(packed_tick: int) -> int:
	return int(packed_tick & RACE_PHASE_TICK_MASK)
