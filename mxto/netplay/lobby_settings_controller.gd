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
var player_settings := {}
var player_settings_revisions := {}
var revision := 0
var cpu_player_ids: Array = []
var race_cpu_player_ids: Array = []
var cpu_player_settings := {}
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
	player_settings.clear()
	player_settings_revisions.clear()
	revision += 1
	cpu_player_ids.clear()
	race_cpu_player_ids.clear()
	cpu_player_settings.clear()
	reset_latency()
	reset_interval_counters()

func reset_settings(preserved_settings: Dictionary = {}) -> void:
	player_settings.clear()
	player_settings_revisions.clear()
	revision += 1
	for id_value in preserved_settings.keys():
		var player_id := int(id_value)
		player_settings[player_id] = preserved_settings[id_value]
		player_settings_revisions[player_id] = 1
	for player_id in cpu_player_ids:
		player_settings[player_id] = cpu_player_settings.get(player_id, {})

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
		cpu_player_settings[player_id] = settings
		player_settings[player_id] = settings
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
	var settings = player_settings.get(player_id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
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
		_lobby_latency_snapshot.rpc(latency_rtt_s)
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
		_send_player_settings_update(player_settings.get(local_id, settings), local_id)
	else:
		_send_player_settings_update(settings, -1, 1)
		_store_player_settings(local_id, settings)

func send_player_settings_to_all(settings: Dictionary, player_id: int) -> void:
	_send_player_settings_update(settings, player_id)

func send_player_settings_snapshot_to_peer(peer_id: int) -> void:
	if !is_server or race_active or player_settings.is_empty() or !_can_send_to_peer(peer_id):
		return
	var raw := var_to_bytes(player_settings)
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
	return int(player_settings_revisions.get(player_id, 0))

func get_local_player_settings_snapshot() -> Dictionary:
	if game_manager != null and game_manager.car_settings != null:
		var settings = game_manager.car_settings.get_player_settings()
		if settings != null:
			return settings.to_dict()
	var local_id := multiplayer.get_unique_id()
	var settings = player_settings.get(local_id, {})
	return settings if typeof(settings) == TYPE_DICTIONARY else {}

func remove_player(player_id: int) -> void:
	if player_settings.has(player_id):
		player_settings.erase(player_id)
		player_settings_revisions.erase(player_id)
		revision += 1
	latency_rtt_s.erase(player_id)
	latency_pending_msec.erase(player_id)

func broadcast_cpu_roster() -> void:
	if !is_server:
		return
	var settings_array := _collect_cpu_settings_array()
	_apply_cpu_roster(cpu_player_ids, settings_array)
	sync_cpu_roster.rpc(cpu_player_ids, settings_array)

func send_cpu_roster_to_peer(peer_id: int) -> void:
	if is_server:
		sync_cpu_roster.rpc_id(peer_id, cpu_player_ids, _collect_cpu_settings_array())

@rpc("authority", "call_remote", "reliable")
func sync_cpu_roster(ids: Array, settings_array: Array) -> void:
	_apply_cpu_roster(ids, settings_array)

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
func _lobby_latency_snapshot(latencies: Dictionary) -> void:
	if race_active:
		return
	latency_rtt_s = latencies.duplicate(true)
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
	var decoded = bytes_to_var(raw)
	if typeof(decoded) != TYPE_DICTIONARY:
		return
	for raw_id in (decoded as Dictionary).keys():
		var player_id := int(raw_id)
		var settings = (decoded as Dictionary)[raw_id]
		if player_id > 0 and typeof(settings) == TYPE_DICTIONARY:
			_store_player_settings(player_id, settings)

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
	var decoded = bytes_to_var(raw)
	if typeof(decoded) == TYPE_DICTIONARY:
		_apply_player_settings_update(decoded, player_id, sender_id)

func _send_player_settings_update(settings: Dictionary, player_id: int, peer_id: int = 0) -> void:
	var raw := var_to_bytes(settings)
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
	settings = player_settings[player_id]
	if is_server and sender_id != 0:
		_send_player_settings_update(settings, player_id)
	if player_id == multiplayer.get_unique_id() and game_manager != null and game_manager.car_settings != null:
		game_manager.car_settings.apply_authoritative_vehicle_selection(settings)
	player_role_changed.emit(player_id, bool(settings.get("spectator", false)))

func enforce_official_vehicles() -> void:
	if !is_server or race_active or _workshop_vehicles_allowed():
		return
	for player_id_value in player_settings.keys():
		var player_id := int(player_id_value)
		var existing = player_settings[player_id]
		if typeof(existing) != TYPE_DICTIONARY:
			continue
		var normalized := _normalize_vehicle_for_lobby(existing)
		if normalized == existing:
			continue
		_store_player_settings(player_id, normalized)
		if cpu_player_ids.has(player_id):
			cpu_player_settings[player_id] = normalized.duplicate(true)
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
		var record: Dictionary = game_manager.vehicle_content_controller.content_catalog.resolve_content(content_id)
		return (
			String(record.get("source", "")) == "official"
			and String(record.get("gameplay_digest", "")) == String(settings.get("vehicle_gameplay_digest", ""))
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

func _store_player_settings(player_id: int, settings: Dictionary) -> bool:
	if player_id <= 0:
		return false
	var existing = player_settings.get(player_id, null)
	if typeof(existing) == TYPE_DICTIONARY and existing == settings:
		log_deduped += 1
		return false
	player_settings[player_id] = settings.duplicate(true)
	if game_manager != null and game_manager.vehicle_content_controller != null:
		game_manager.vehicle_content_controller.request_lobby_vehicle_content(player_settings[player_id])
	player_settings_revisions[player_id] = int(player_settings_revisions.get(player_id, 0)) + 1
	revision += 1
	log_accepted += 1
	return true

func _merge_existing_livery_settings(settings: Dictionary, player_id: int) -> Dictionary:
	if settings.has("car_livery") or !player_settings.has(player_id):
		return settings
	var existing = player_settings[player_id]
	if typeof(existing) != TYPE_DICTIONARY or !(existing as Dictionary).has("car_livery"):
		return settings
	var merged := settings.duplicate(true)
	merged["car_livery"] = (existing as Dictionary)["car_livery"]
	return merged

func _set_next_race_accel_setting(player_id: int, accel_setting: float) -> void:
	accel_setting = clampf(accel_setting, 0.0, 1.0)
	var settings = player_settings.get(player_id, {})
	settings = (settings as Dictionary).duplicate(true) if typeof(settings) == TYPE_DICTIONARY else {}
	settings["accel_setting"] = accel_setting
	_store_player_settings(player_id, settings)

func _allocate_cpu_id() -> int:
	for player_id in range(CPU_ID_MIN, CPU_ID_MAX + 1):
		if !cpu_player_ids.has(player_id) and !player_ids.has(player_id) and !spectator_ids.has(player_id):
			return player_id
	return -1

func _remap_cpu_id(old_id: int) -> int:
	if !cpu_player_ids.has(old_id):
		return old_id
	var settings = cpu_player_settings.get(old_id, player_settings.get(old_id, {}))
	cpu_player_ids.erase(old_id)
	cpu_player_settings.erase(old_id)
	player_settings.erase(old_id)
	var new_id := _allocate_cpu_id()
	if new_id < 0:
		return -1
	cpu_player_ids.append(new_id)
	cpu_player_settings[new_id] = settings
	player_settings[new_id] = settings
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
	cpu_player_settings[new_id] = settings
	player_settings[new_id] = settings

func _remove_cpu_driver() -> void:
	if cpu_player_ids.is_empty():
		return
	var removed_id := int(cpu_player_ids.pop_back())
	cpu_player_settings.erase(removed_id)
	player_settings.erase(removed_id)
	race_cpu_player_ids.erase(removed_id)
	cpu_removed.emit(removed_id)

func _collect_cpu_settings_array() -> Array:
	var settings_array: Array = []
	for player_id in cpu_player_ids:
		settings_array.append(cpu_player_settings.get(player_id, {}))
	return settings_array

func _apply_cpu_roster(ids: Array, settings_array: Array) -> void:
	var previous := cpu_player_ids.duplicate(true)
	cpu_player_ids = ids.duplicate(true)
	cpu_player_settings.clear()
	for old_id in previous:
		if !cpu_player_ids.has(old_id):
			player_settings.erase(old_id)
	for index in cpu_player_ids.size():
		var player_id := int(cpu_player_ids[index])
		var settings = settings_array[index] if index < settings_array.size() else {}
		cpu_player_settings[player_id] = settings
		player_settings[player_id] = settings

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
