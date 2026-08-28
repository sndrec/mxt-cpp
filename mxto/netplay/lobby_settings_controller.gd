class_name LobbySettingsController
extends Node

signal player_role_changed(player_id: int, spectator: bool)
signal cpu_removed(player_id: int)
signal latency_sample_received(peer_id: int, sample_rtt_s: float)

const SETTINGS_SNAPSHOT_MAX_BYTES := 16 * 1024 * 1024
const CPU_ID_MIN := 1
const CPU_ID_MAX := 5000
const LATENCY_SAMPLE_INTERVAL_MSEC := 1000
const OFFICIAL_VEHICLE_PREFIX := "mxt:vehicle:official:"
const WORKSHOP_VEHICLE_PREFIX := "mxt:vehicle:workshop:"
const ALL_ROUNDER_VEHICLE_ID := "mxt:vehicle:official:allrounder"

var game_manager: GameManager
var race_roster := MxtRaceRoster.new()
var revision := 0
var cpu_player_ids: Array = []
var race_cpu_player_ids: Array = []
var latency_rtt_s := {}
var latency_pending_msec := {}
var latency_last_sample_msec := 0
var local_rtt_s := 0.0

var is_server := false
var race_active := false
var network_active := false
var player_ids: Array = []
var spectator_ids: Array = []

var log_messages_in := 0
var log_messages_out := 0
var log_bytes_in := 0
var log_bytes_out := 0
var log_accepted := 0
var log_deduped := 0

func initialize(in_game_manager: GameManager) -> void:
	game_manager = in_game_manager

func set_context(server: bool, active_race: bool, active_network: bool, humans: Array, spectators: Array) -> void:
	is_server = server
	race_active = active_race
	network_active = active_network
	player_ids = humans.duplicate()
	spectator_ids = spectators.duplicate()

func set_race_cpu_roster(ids: Array) -> void:
	race_cpu_player_ids = _id_array(ids)

func clear_race_cpu_roster() -> void:
	race_cpu_player_ids.clear()

func set_local_latency(rtt_s: float) -> void:
	local_rtt_s = rtt_s
	latency_rtt_s[multiplayer.get_unique_id()] = rtt_s

func reset_latency() -> void:
	latency_rtt_s.clear()
	latency_pending_msec.clear()
	latency_last_sample_msec = 0
	local_rtt_s = 0.0

func reset_all() -> void:
	race_roster.clear()
	revision += 1
	cpu_player_ids.clear()
	race_cpu_player_ids.clear()
	reset_latency()
	reset_interval_counters()

func reset_settings(preserved_settings: Dictionary = {}) -> void:
	var preserved_cpu_settings: Array = []
	for player_id in cpu_player_ids:
		preserved_cpu_settings.append(get_player_settings(player_id))
	race_roster.clear()
	revision += 1
	for id_value in preserved_settings.keys():
		var player_id := int(id_value)
		_store_player_settings(player_id, preserved_settings[id_value])
	for index in cpu_player_ids.size():
		var player_id := int(cpu_player_ids[index])
		var settings: Dictionary = preserved_cpu_settings[index] if index < preserved_cpu_settings.size() else game_manager.build_cpu_player_settings(index)
		_store_player_settings(player_id, settings, true)

func reset_interval_counters() -> void:
	log_messages_in = 0
	log_messages_out = 0
	log_bytes_in = 0
	log_bytes_out = 0
	log_accepted = 0
	log_deduped = 0

func set_cpu_driver_count(count: int) -> void:
	count = clampi(count, 0, CPU_ID_MAX - CPU_ID_MIN + 1)
	while cpu_player_ids.size() < count:
		_add_cpu_driver()
	while cpu_player_ids.size() > count:
		_remove_cpu_driver()
	broadcast_cpu_roster()


func set_cpu_driver_vehicle_pool(content_ids: Array) -> void:
	for index in range(cpu_player_ids.size()):
		var player_id := int(cpu_player_ids[index])
		var settings := game_manager.build_cpu_player_settings(index, content_ids)
		_store_player_settings(player_id, settings, true)
	broadcast_cpu_roster()

func add_cpu_driver() -> void:
	set_cpu_driver_count(cpu_player_ids.size() + 1)

func remove_cpu_driver() -> void:
	if cpu_player_ids.is_empty():
		return
	set_cpu_driver_count(cpu_player_ids.size() - 1)

func get_cpu_roster() -> Array:
	return race_cpu_player_ids.duplicate(true) if !race_cpu_player_ids.is_empty() else cpu_player_ids.duplicate(true)

func ensure_cpu_ids_do_not_overlap_humans() -> bool:
	var changed := false
	for player_id in player_ids + spectator_ids:
		if cpu_player_ids.has(player_id):
			_remap_cpu_id(int(player_id))
			changed = true
	return changed

func cpu_human_overlaps() -> Array:
	var overlaps: Array = []
	for player_id in player_ids + spectator_ids:
		if cpu_player_ids.has(player_id):
			overlaps.append(player_id)
	return overlaps

func username_for_player(player_id: int) -> String:
	var settings := get_player_settings(player_id)
	if settings.has("username"):
		return str(settings["username"])
	return str(player_id)

func process_latency(waiting_peer_ids: Array) -> void:
	if race_active or !_has_network_peer():
		return
	var now := Time.get_ticks_msec()
	if now < latency_last_sample_msec + LATENCY_SAMPLE_INTERVAL_MSEC:
		return
	latency_last_sample_msec = now
	if is_server:
		latency_rtt_s[multiplayer.get_unique_id()] = 0.0
		for player_id in player_ids + spectator_ids + waiting_peer_ids:
			if int(player_id) == multiplayer.get_unique_id() or cpu_player_ids.has(player_id):
				continue
			latency_pending_msec[player_id] = now
			_lobby_latency_ping.rpc_id(player_id, now)
		var latency_player_ids := PackedInt64Array()
		var latency_values := PackedFloat32Array()
		for id_value in latency_rtt_s:
			latency_player_ids.append(int(id_value))
			latency_values.append(float(latency_rtt_s[id_value]))
		_lobby_latency_snapshot.rpc(latency_player_ids, latency_values)
	else:
		latency_pending_msec[1] = now
		_lobby_latency_ping.rpc_id(1, now)

func send_player_settings(settings: Dictionary) -> void:
	if race_active:
		return
	var local_id := multiplayer.get_unique_id()
	settings = _merge_existing_livery_settings(settings, local_id)
	settings = _normalize_vehicle_for_lobby(settings)
	if is_server:
		_apply_player_settings_update(settings, local_id, 0)
		_send_player_settings_update(get_player_settings(local_id), local_id)
	else:
		_send_player_settings_update(settings, -1, 1)
		_store_player_settings(local_id, settings)

func send_player_settings_to_all(settings: Dictionary, player_id: int) -> void:
	_send_player_settings_update(settings, player_id)

func send_player_settings_snapshot_to_peer(peer_id: int) -> void:
	if !is_server or race_active or race_roster.count() == 0 or !_can_send_to_peer(peer_id):
		return
	var roster := _build_settings_roster(get_player_settings_ids())
	if roster == null:
		return
	var raw := roster.encode_wire()
	if raw.is_empty() or raw.size() > SETTINGS_SNAPSHOT_MAX_BYTES:
		push_warning("Lobby settings snapshot rejected before send: %d bytes" % raw.size())
		return
	var compressed := raw.compress(FileAccess.COMPRESSION_ZSTD)
	if compressed.is_empty():
		compressed = raw
	_receive_player_settings_snapshot.rpc_id(peer_id, raw.size(), compressed)
	log_messages_out += 1
	log_bytes_out += compressed.size()

func send_next_race_accel_setting(accel_setting: float) -> void:
	var local_id := multiplayer.get_unique_id()
	_set_next_race_accel_setting(local_id, accel_setting)
	if !_has_network_peer():
		return
	if is_server:
		update_next_race_accel_setting.rpc(accel_setting, local_id)
	else:
		update_next_race_accel_setting.rpc_id(1, accel_setting)

func get_player_settings_revision(player_id: int) -> int:
	return race_roster.get_revision(player_id)

func get_local_player_settings_snapshot() -> Dictionary:
	if game_manager != null and game_manager.car_settings != null:
		var settings = game_manager.car_settings.get_player_settings()
		if settings != null:
			return settings.to_dict()
	var local_id := multiplayer.get_unique_id()
	return get_player_settings(local_id)

func has_player_settings(player_id: int) -> bool:
	return race_roster.has_player(player_id)

func get_player_settings(player_id: int) -> Dictionary:
	return race_roster.get_player_settings_dictionary(player_id)

func get_player_settings_ids() -> Array:
	var ids: Array = []
	for index in race_roster.count():
		ids.append(race_roster.get_player_id(index))
	return ids

func get_player_settings_count() -> int:
	return race_roster.count()

func set_player_settings(player_id: int, settings: Dictionary, cpu := false) -> bool:
	return _store_player_settings(player_id, settings, cpu)

func clear_player_settings() -> void:
	race_roster.clear()
	revision += 1

func build_race_roster(player_id_order: Array) -> MxtRaceRoster:
	return _build_settings_roster(player_id_order)

func apply_race_roster(roster: MxtRaceRoster) -> void:
	if roster == null:
		return
	for index in roster.count():
		_store_player_settings(roster.get_player_id(index), roster.get_settings_dictionary(index), roster.is_cpu(index))

func remove_player(player_id: int) -> void:
	if race_roster.remove_player(player_id):
		revision += 1
	latency_rtt_s.erase(player_id)
	latency_pending_msec.erase(player_id)

func broadcast_cpu_roster() -> void:
	if !is_server:
		return
	var roster := _build_settings_roster(cpu_player_ids)
	if roster == null:
		return
	_apply_cpu_roster(roster)
	sync_cpu_roster.rpc(roster.encode_wire())

func send_cpu_roster_to_peer(peer_id: int) -> void:
	if is_server:
		var roster := _build_settings_roster(cpu_player_ids)
		if roster != null:
			sync_cpu_roster.rpc_id(peer_id, roster.encode_wire())

@rpc("authority", "call_remote", "reliable")
func sync_cpu_roster(payload: PackedByteArray) -> void:
	var roster := MxtRaceRoster.new()
	if !roster.decode_wire(payload):
		push_warning("Rejected malformed CPU roster: %s" % roster.get_last_error())
		return
	_apply_cpu_roster(roster)

@rpc("any_peer", "unreliable")
func _lobby_latency_ping(sent_msec: int) -> void:
	if race_active:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = multiplayer.get_unique_id()
	_lobby_latency_pong.rpc_id(sender, sent_msec)

@rpc("any_peer", "unreliable")
func _lobby_latency_pong(sent_msec: int) -> void:
	if race_active:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = multiplayer.get_unique_id()
	var sample := maxf(0.0, 0.001 * float(Time.get_ticks_msec() - sent_msec))
	if is_server:
		latency_rtt_s[sender] = sample
	latency_sample_received.emit(sender, sample)
	if !is_server:
		latency_rtt_s[multiplayer.get_unique_id()] = local_rtt_s
	latency_pending_msec.erase(sender)

@rpc("authority", "unreliable")
func _lobby_latency_snapshot(player_ids_snapshot: PackedInt64Array, latencies: PackedFloat32Array) -> void:
	if race_active:
		return
	if player_ids_snapshot.size() != latencies.size() or player_ids_snapshot.size() > 1024:
		return
	latency_rtt_s.clear()
	for index in player_ids_snapshot.size():
		if player_ids_snapshot[index] >= 0 and is_finite(latencies[index]) and latencies[index] >= 0.0:
			latency_rtt_s[int(player_ids_snapshot[index])] = float(latencies[index])
	if !is_server:
		latency_rtt_s[multiplayer.get_unique_id()] = local_rtt_s

@rpc("authority", "call_remote", "reliable", 10)
func _receive_player_settings_snapshot(raw_size: int, payload: PackedByteArray) -> void:
	if is_server or race_active or raw_size <= 0 or raw_size > SETTINGS_SNAPSHOT_MAX_BYTES:
		return
	if payload.is_empty() or payload.size() > SETTINGS_SNAPSHOT_MAX_BYTES:
		return
	log_messages_in += 1
	log_bytes_in += payload.size()
	var raw := payload.decompress(raw_size, FileAccess.COMPRESSION_ZSTD)
	if raw.is_empty() and payload.size() == raw_size:
		raw = payload
	if raw.size() != raw_size:
		push_warning("Rejected malformed lobby settings snapshot")
		return
	var roster := MxtRaceRoster.new()
	if !roster.decode_wire(raw):
		push_warning("Rejected malformed lobby settings snapshot: %s" % roster.get_last_error())
		return
	for index in roster.count():
		_store_player_settings(roster.get_player_id(index), roster.get_settings_dictionary(index))

@rpc("any_peer", "call_remote", "reliable", 10)
func update_next_race_accel_setting(accel_setting: float, player_id: int = -1) -> void:
	var sender_id := multiplayer.get_remote_sender_id()
	if is_server and sender_id != 0:
		player_id = sender_id
	elif player_id == -1:
		player_id = sender_id if sender_id != 0 else multiplayer.get_unique_id()
	elif !is_server and sender_id != 1:
		return
	_set_next_race_accel_setting(player_id, accel_setting)
	if is_server:
		update_next_race_accel_setting.rpc(accel_setting, player_id)

@rpc("any_peer", "call_remote", "reliable", 10)
func _receive_player_settings_update(raw_size: int, payload: PackedByteArray, player_id: int = -1) -> void:
	if race_active:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if sender_id != 0:
		log_messages_in += 1
		log_bytes_in += payload.size()
	if raw_size <= 0 or raw_size > SETTINGS_SNAPSHOT_MAX_BYTES or payload.is_empty() or payload.size() > SETTINGS_SNAPSHOT_MAX_BYTES:
		return
	if !is_server and sender_id != 1:
		return
	var raw := payload.decompress(raw_size, FileAccess.COMPRESSION_ZSTD)
	if raw.is_empty() and payload.size() == raw_size:
		raw = payload
	if raw.size() != raw_size:
		return
	var roster := MxtRaceRoster.new()
	if !roster.decode_wire(raw) or roster.count() != 1:
		return
	_apply_player_settings_update(roster.get_settings_dictionary(0), player_id, sender_id)

func _send_player_settings_update(settings: Dictionary, player_id: int, peer_id: int = 0) -> void:
	var wire_player_id := player_id if player_id > 0 else multiplayer.get_unique_id()
	var roster := MxtRaceRoster.new()
	if !roster.append_settings(wire_player_id, wire_player_id, cpu_player_ids.has(wire_player_id), false, false, settings):
		push_warning("Lobby settings update rejected before send: %s" % roster.get_last_error())
		return
	var raw := roster.encode_wire()
	if raw.is_empty() or raw.size() > SETTINGS_SNAPSHOT_MAX_BYTES:
		return
	var payload := raw.compress(FileAccess.COMPRESSION_ZSTD)
	if payload.is_empty():
		payload = raw
	if peer_id > 0:
		_receive_player_settings_update.rpc_id(peer_id, raw.size(), payload, player_id)
		log_messages_out += 1
		log_bytes_out += payload.size()
	else:
		_receive_player_settings_update.rpc(raw.size(), payload, player_id)
		var recipient_count := multiplayer.get_peers().size()
		log_messages_out += recipient_count
		log_bytes_out += payload.size() * recipient_count

func _apply_player_settings_update(settings: Dictionary, player_id: int, sender_id: int) -> void:
	if is_server and sender_id != 0:
		player_id = sender_id
	elif player_id == -1:
		player_id = sender_id if sender_id != 0 else multiplayer.get_unique_id()
	settings = _merge_existing_livery_settings(settings, player_id)
	settings = _normalize_vehicle_for_lobby(settings)
	if !_store_player_settings(player_id, settings):
		return
	settings = get_player_settings(player_id)
	if is_server and sender_id != 0:
		_send_player_settings_update(settings, player_id)
	if player_id == multiplayer.get_unique_id() and game_manager != null and game_manager.car_settings != null:
		game_manager.car_settings.apply_authoritative_vehicle_selection(settings)
	player_role_changed.emit(player_id, bool(settings.get("spectator", false)))

func enforce_official_vehicles() -> void:
	if !is_server or race_active or _workshop_vehicles_allowed():
		return
	for player_id_value in get_player_settings_ids():
		var player_id := int(player_id_value)
		var existing := get_player_settings(player_id)
		var normalized := _normalize_vehicle_for_lobby(existing)
		if normalized == existing:
			continue
		_store_player_settings(player_id, normalized, cpu_player_ids.has(player_id))
		_send_player_settings_update(normalized, player_id)
		if player_id == multiplayer.get_unique_id() and game_manager != null and game_manager.car_settings != null:
			game_manager.car_settings.apply_authoritative_vehicle_selection(normalized)

func _workshop_vehicles_allowed() -> bool:
	if game_manager == null or game_manager.network_manager == null:
		return true
	return game_manager.network_manager.race_configuration.allow_workshop_vehicles

func _normalize_vehicle_for_lobby(settings: Dictionary) -> Dictionary:
	if _vehicle_selection_allowed(settings):
		return settings
	var normalized := settings.duplicate(true)
	normalized["vehicle_content_id"] = ALL_ROUNDER_VEHICLE_ID
	normalized["car_livery"] = {}
	var evidence := game_manager.vehicle_content_controller.get_evidence(ALL_ROUNDER_VEHICLE_ID)
	normalized["vehicle_gameplay_digest"] = String(evidence.get("vehicle_gameplay_digest", ""))
	normalized["vehicle_package_digest"] = String(evidence.get("vehicle_package_digest", ""))
	normalized["vehicle_workshop_id"] = String(evidence.get("vehicle_workshop_id", ""))
	return normalized

func _vehicle_selection_allowed(settings: Dictionary) -> bool:
	var content_id := String(settings.get("vehicle_content_id", ""))
	if content_id.begins_with(OFFICIAL_VEHICLE_PREFIX):
		if game_manager == null or game_manager.vehicle_content_controller == null:
			return false
		var record: MxtContentRecord = game_manager.vehicle_content_controller.content_catalog.resolve_content(content_id)
		return (
			record != null
			and record.source == MxtContentRecord.SOURCE_OFFICIAL
			and record.gameplay_digest == String(settings.get("vehicle_gameplay_digest", ""))
			and String(settings.get("vehicle_package_digest", "")).is_empty()
			and String(settings.get("vehicle_workshop_id", "")).is_empty())
	if !_workshop_vehicles_allowed():
		return false
	var workshop_id_text := String(settings.get("vehicle_workshop_id", ""))
	if !workshop_id_text.is_valid_int():
		return false
	var workshop_id := workshop_id_text.to_int()
	return (
		workshop_id > 0
		and content_id == WORKSHOP_VEHICLE_PREFIX + str(workshop_id)
		and _is_sha256_digest(String(settings.get("vehicle_gameplay_digest", "")))
		and _is_sha256_digest(String(settings.get("vehicle_package_digest", ""))))

func _is_sha256_digest(value: String) -> bool:
	if value.length() != 71 or !value.begins_with("sha256:"):
		return false
	for index in range(7, value.length()):
		var code := value.unicode_at(index)
		if !((code >= 48 and code <= 57) or (code >= 97 and code <= 102)):
			return false
	return true

func _store_player_settings(player_id: int, settings: Dictionary, cpu := false) -> bool:
	if player_id < 0:
		return false
	if !race_roster.upsert_settings(player_id, player_id, cpu or cpu_player_ids.has(player_id), false, false, settings):
		if !race_roster.get_last_error().is_empty():
			push_warning("Rejected player settings: %s" % race_roster.get_last_error())
			return false
		log_deduped += 1
		return false
	if game_manager != null and game_manager.vehicle_content_controller != null:
		game_manager.lobby_vehicle_content_tracker.request_vehicle_content(get_player_settings(player_id))
	revision += 1
	log_accepted += 1
	return true

func _merge_existing_livery_settings(settings: Dictionary, player_id: int) -> Dictionary:
	if settings.has("car_livery") or !race_roster.has_player(player_id):
		return settings
	var existing := get_player_settings(player_id)
	if !existing.has("car_livery"):
		return settings
	var merged := settings.duplicate(true)
	merged["car_livery"] = existing["car_livery"]
	return merged

func _set_next_race_accel_setting(player_id: int, accel_setting: float) -> void:
	accel_setting = clampf(accel_setting, 0.0, 1.0)
	if race_roster.set_accel_setting(player_id, accel_setting):
		revision += 1

func _allocate_cpu_id() -> int:
	for player_id in range(CPU_ID_MIN, CPU_ID_MAX + 1):
		if !cpu_player_ids.has(player_id) and !player_ids.has(player_id) and !spectator_ids.has(player_id):
			return player_id
	return -1

func _remap_cpu_id(old_id: int) -> int:
	if !cpu_player_ids.has(old_id):
		return old_id
	var settings := get_player_settings(old_id)
	cpu_player_ids.erase(old_id)
	var new_id := _allocate_cpu_id()
	if new_id < 0:
		return -1
	cpu_player_ids.append(new_id)
	if !race_roster.replace_player_id(old_id, new_id, new_id):
		_store_player_settings(new_id, settings, true)
	if race_cpu_player_ids.has(old_id):
		race_cpu_player_ids.erase(old_id)
		race_cpu_player_ids.append(new_id)
	return new_id

func _add_cpu_driver() -> void:
	var new_id := _allocate_cpu_id()
	if new_id < 0:
		return
	cpu_player_ids.append(new_id)
	var settings := game_manager.build_cpu_player_settings(cpu_player_ids.size() - 1)
	_store_player_settings(new_id, settings, true)

func _remove_cpu_driver() -> void:
	if cpu_player_ids.is_empty():
		return
	var removed_id := int(cpu_player_ids.pop_back())
	race_roster.remove_player(removed_id)
	race_cpu_player_ids.erase(removed_id)
	cpu_removed.emit(removed_id)

func _apply_cpu_roster(roster: MxtRaceRoster) -> void:
	var previous := cpu_player_ids.duplicate(true)
	cpu_player_ids.clear()
	for index in roster.count():
		var player_id := roster.get_player_id(index)
		if !cpu_player_ids.has(player_id):
			cpu_player_ids.append(player_id)
	for old_id in previous:
		if !cpu_player_ids.has(old_id):
			race_roster.remove_player(old_id)
	for index in roster.count():
		var player_id := roster.get_player_id(index)
		var settings := roster.get_settings_dictionary(index)
		_store_player_settings(player_id, settings, true)

func _build_settings_roster(ids: Array) -> MxtRaceRoster:
	var roster := MxtRaceRoster.new()
	for id_value in ids:
		var player_id := int(id_value)
		var settings := get_player_settings(player_id)
		if settings.is_empty():
			continue
		if !roster.append_settings(player_id, player_id, cpu_player_ids.has(player_id), false, false, settings):
			push_warning("Lobby roster rejected before send: %s" % roster.get_last_error())
			return null
	return roster

func _id_array(values: Array) -> Array:
	var result: Array = []
	for value in values:
		var player_id := int(value)
		if !result.has(player_id):
			result.append(player_id)
	return result

func _can_send_to_peer(peer_id: int) -> bool:
	return _has_network_peer() and multiplayer.get_peers().has(peer_id)

func _has_network_peer() -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if is_server:
		return true
	return multiplayer.multiplayer_peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED
