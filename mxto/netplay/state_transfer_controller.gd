class_name StateTransferController
extends Node

signal state_received(tick: int, state: PackedByteArray)
signal state_sample_generated(state: PackedByteArray, tick: int, racer_count: int)
signal wire_bytes_sent(byte_count: int)
signal wire_bytes_received(byte_count: int)

const BROADCAST_INTERVAL_TICKS := 60
const CHUNK_PAYLOAD_BYTES := 1000
const MAX_PAYLOAD_BYTES := 16 * 1024 * 1024
const FEC_DATA_CHUNKS := 8
const CHUNKS_PER_PEER_PER_SEND := 3
const RACE_PHASE_TICK_BIT := 0x80000000
const RACE_PHASE_TICK_MASK := 0x7fffffff

var server_netcode_session: NetcodeSession
var peer_offsets := {}
var peer_ids: Array = []
var outgoing_transfers := {}
var pending_chunks := {}
var latest_pending_tick := -1
var latest_state_tick := -1
var race_active := false
var race_phase := 0

var log_raw_out := 0
var log_payload_out := 0
var log_sent_count := 0
var log_max_fragments_out := 0
var log_min_success_2pct := 1.0
var log_chunk_msgs_out := 0
var log_chunk_msgs_in := 0
var log_chunk_dups_in := 0
var log_chunk_stale_drops := 0
var log_chunk_bad_meta_drops := 0
var log_chunk_completed := 0
var log_chunk_completed_count_max := 0
var log_parity_chunks_out := 0
var log_fec_recovered_chunks := 0
var log_fec_abandoned := 0
var log_payload_in := 0
var log_raw_in := 0
var log_recv_count := 0
var log_max_recv_gap_ms := 0
var log_last_recv_ms := -1
var log_sec_header := 0
var log_sec_bumper_meta := 0
var log_sec_sparks := 0
var log_sec_car_scalars := 0
var log_sec_car_vec3 := 0
var log_sec_car_basis := 0
var log_sec_car_conditionals := 0
var log_sec_car_tilt := 0
var log_sec_car_wall := 0
var log_sec_bumper_total := 0
var log_sec_triggers := 0
var log_sec_total := 0
var log_stat_car_count := 0
var log_stat_bumper_count := 0
var log_stat_active_bumpers := 0
var log_stat_active_sparks := 0
var log_stat_trigger_count := 0
var log_stat_car_collision_old := 0
var log_stat_car_restore := 0
var receive_profile_usec := 0

func initialize(in_server_netcode_session: NetcodeSession) -> void:
	server_netcode_session = in_server_netcode_session

func set_race_context(active: bool, phase: int) -> void:
	race_active = active
	race_phase = phase

func rebuild_peer_schedule(is_server: bool, player_ids: Array, spectator_ids: Array) -> void:
	peer_offsets.clear()
	peer_ids.clear()
	if !is_server:
		return
	for roster in [player_ids, spectator_ids]:
		for id_value in roster:
			var peer_id := int(id_value)
			if !peer_ids.has(peer_id):
				peer_ids.append(peer_id)
	for index in peer_ids.size():
		peer_offsets[peer_ids[index]] = int(round(float(BROADCAST_INTERVAL_TICKS) * float(index) / float(peer_ids.size())))

func remove_peer(peer_id: int) -> void:
	outgoing_transfers.erase(peer_id)

func reset() -> void:
	peer_offsets.clear()
	peer_ids.clear()
	outgoing_transfers.clear()
	pending_chunks.clear()
	latest_pending_tick = -1
	latest_state_tick = -1
	race_active = false
	reset_interval_counters()

func process_server_snapshot(tick: int, is_server: bool, network_active: bool, listen_server: bool, server_game_sim: GameSim, use_compression: bool, racer_count: int) -> void:
	if !is_server or !race_active or server_game_sim == null:
		return
	var remote_recipients: Array[int] = []
	var sync_listen_host := false
	var local_id := multiplayer.get_unique_id()
	for id_value in peer_ids:
		var peer_id := int(id_value)
		if !_can_send_to_peer(peer_id, network_active, listen_server) or !peer_offsets.has(peer_id) or int(peer_offsets[peer_id]) != tick % BROADCAST_INTERVAL_TICKS:
			continue
		if listen_server and peer_id == local_id:
			sync_listen_host = true
		else:
			remote_recipients.append(peer_id)
	if !sync_listen_host and remote_recipients.is_empty():
		return
	var state: PackedByteArray = server_game_sim.get_state_data(tick)
	state_sample_generated.emit(state, tick, racer_count)
	if sync_listen_host and !state.is_empty():
		latest_state_tick = tick
		state_received.emit(tick, state)
	if remote_recipients.is_empty():
		return
	var payload := state
	var uncompressed_size := -1
	if !state.is_empty() and use_compression:
		payload = state.compress(FileAccess.COMPRESSION_ZSTD)
		uncompressed_size = state.size()
	for peer_id in remote_recipients:
		_queue_transfer(peer_id, tick, uncompressed_size, payload)
		_log_sent(uncompressed_size if uncompressed_size > 0 else payload.size(), payload.size())
		if server_game_sim.has_method("get_network_state_size_stats"):
			_log_size_stats(server_game_sim.get_network_state_size_stats())

func send_outgoing_chunks(is_server: bool, network_active: bool, listen_server: bool) -> void:
	if !is_server or !race_active:
		return
	for id_value in peer_ids:
		var peer_id := int(id_value)
		if !_can_send_to_peer(peer_id, network_active, listen_server):
			continue
		var transfer = outgoing_transfers.get(peer_id)
		if typeof(transfer) != TYPE_DICTIONARY:
			continue
		var chunks: Array = transfer.get("chunks", [])
		if chunks.is_empty():
			continue
		var data_chunk_count := int(transfer.get("data_chunk_count", 0))
		var next_chunk := int(transfer.get("next_chunk", 0))
		var sent := 0
		while sent < CHUNKS_PER_PEER_PER_SEND and next_chunk < chunks.size():
			var chunk_index := next_chunk
			next_chunk += 1
			var chunk: PackedByteArray = chunks[chunk_index]
			log_chunk_msgs_out += 1
			var chunk_group := chunk_index / (FEC_DATA_CHUNKS + 1)
			var chunk_local_index := chunk_index % (FEC_DATA_CHUNKS + 1)
			var group_data_count := mini(FEC_DATA_CHUNKS, data_chunk_count - chunk_group * FEC_DATA_CHUNKS)
			if chunk_local_index == group_data_count:
				log_parity_chunks_out += 1
			wire_bytes_sent.emit(20 + chunk.size())
			_server_state_chunk.rpc_id(
				peer_id,
				_pack_phase_tick(int(transfer["tick"])),
				int(transfer["raw_size"]),
				int(transfer["payload_size"]),
				chunk_index,
				chunks.size(),
				chunk)
			sent += 1
		transfer["next_chunk"] = next_chunk
		outgoing_transfers[peer_id] = transfer

func pending_log_fields() -> Dictionary:
	var result := {
		"records": pending_chunks.size(),
		"best_recv_pct": 0.0,
		"best_missing": 0,
		"oldest_tick": -1,
		"newest_tick": -1,
	}
	var best_received := 0
	var best_count := 0
	for tick_key in pending_chunks.keys():
		var tick := int(tick_key)
		if int(result["oldest_tick"]) < 0 or tick < int(result["oldest_tick"]):
			result["oldest_tick"] = tick
		if tick > int(result["newest_tick"]):
			result["newest_tick"] = tick
		var record = pending_chunks[tick_key]
		if typeof(record) != TYPE_DICTIONARY:
			continue
		var count := int(record.get("chunk_count", 0))
		var received := int(record.get("received_count", 0))
		if count > 0 and (best_count <= 0 or float(received) / float(count) > float(best_received) / float(best_count)):
			best_received = received
			best_count = count
	if best_count > 0:
		result["best_recv_pct"] = float(best_received) / float(best_count)
		result["best_missing"] = maxi(0, best_count - best_received)
	return result

func reset_interval_counters() -> void:
	log_raw_out = 0
	log_payload_out = 0
	log_sent_count = 0
	log_max_fragments_out = 0
	log_min_success_2pct = 1.0
	log_chunk_msgs_out = 0
	log_chunk_msgs_in = 0
	log_chunk_dups_in = 0
	log_chunk_stale_drops = 0
	log_chunk_bad_meta_drops = 0
	log_chunk_completed = 0
	log_chunk_completed_count_max = 0
	log_parity_chunks_out = 0
	log_fec_recovered_chunks = 0
	log_fec_abandoned = 0
	log_payload_in = 0
	log_raw_in = 0
	log_recv_count = 0
	log_max_recv_gap_ms = 0
	log_sec_header = 0
	log_sec_bumper_meta = 0
	log_sec_sparks = 0
	log_sec_car_scalars = 0
	log_sec_car_vec3 = 0
	log_sec_car_basis = 0
	log_sec_car_conditionals = 0
	log_sec_car_tilt = 0
	log_sec_car_wall = 0
	log_sec_bumper_total = 0
	log_sec_triggers = 0
	log_sec_total = 0
	log_stat_car_count = 0
	log_stat_bumper_count = 0
	log_stat_active_bumpers = 0
	log_stat_active_sparks = 0
	log_stat_trigger_count = 0
	log_stat_car_collision_old = 0
	log_stat_car_restore = 0
	receive_profile_usec = 0

func mark_state_restored(tick: int) -> void:
	latest_state_tick = tick
	for pending_tick in pending_chunks.keys():
		if int(pending_tick) <= latest_state_tick:
			pending_chunks.erase(pending_tick)

func _queue_transfer(peer_id: int, state_tick: int, uncompressed_size: int, payload: PackedByteArray) -> void:
	if payload.is_empty():
		return
	var data_chunk_count := int(ceil(float(payload.size()) / float(CHUNK_PAYLOAD_BYTES)))
	var chunks: Array = server_netcode_session.build_state_fec_chunks(payload, CHUNK_PAYLOAD_BYTES, FEC_DATA_CHUNKS)
	if chunks.is_empty():
		return
	outgoing_transfers[peer_id] = {
		"tick": state_tick,
		"raw_size": uncompressed_size,
		"payload_size": payload.size(),
		"data_chunk_count": data_chunk_count,
		"chunks": chunks,
		"next_chunk": 0,
	}

@rpc("authority", "call_remote", "unreliable", 4)
func _server_state_chunk(packed_state_tick: int, state_uncompressed_size: int, state_payload_size: int, chunk_index: int, chunk_count: int, chunk: PackedByteArray) -> void:
	if !race_active or _unpack_phase(packed_state_tick) != race_phase:
		return
	var state_tick := _unpack_tick(packed_state_tick)
	if chunk.is_empty():
		return
	if state_tick <= latest_state_tick:
		log_chunk_stale_drops += 1
		return
	var data_chunk_count := int(ceil(float(state_payload_size) / float(CHUNK_PAYLOAD_BYTES)))
	var fec_group_count := int(ceil(float(data_chunk_count) / float(FEC_DATA_CHUNKS)))
	var expected_chunk_count := data_chunk_count + fec_group_count
	if state_payload_size <= 0 or state_payload_size > MAX_PAYLOAD_BYTES or chunk_count != expected_chunk_count or chunk_count > 4096 or chunk_index < 0 or chunk_index >= chunk_count or chunk.size() > CHUNK_PAYLOAD_BYTES:
		log_chunk_bad_meta_drops += 1
		return
	var chunk_group := chunk_index / (FEC_DATA_CHUNKS + 1)
	var chunk_local_index := chunk_index % (FEC_DATA_CHUNKS + 1)
	var group_first_data := chunk_group * FEC_DATA_CHUNKS
	var group_data_count := mini(FEC_DATA_CHUNKS, data_chunk_count - group_first_data)
	var is_parity_chunk := chunk_local_index == group_data_count
	var data_index := group_first_data + chunk_local_index
	var expected_chunk_size := CHUNK_PAYLOAD_BYTES if is_parity_chunk else mini(CHUNK_PAYLOAD_BYTES, state_payload_size - data_index * CHUNK_PAYLOAD_BYTES)
	if chunk_local_index > group_data_count or chunk.size() != expected_chunk_size:
		log_chunk_bad_meta_drops += 1
		return
	var profile_start := Time.get_ticks_usec()
	log_chunk_msgs_in += 1
	wire_bytes_received.emit(20 + chunk.size())
	if latest_pending_tick > state_tick:
		log_chunk_stale_drops += 1
		return
	if state_tick > latest_pending_tick:
		if !pending_chunks.is_empty():
			log_fec_abandoned += 1
		pending_chunks.clear()
		latest_pending_tick = state_tick
	var record = pending_chunks.get(state_tick)
	if typeof(record) != TYPE_DICTIONARY:
		var chunks: Array = []
		var received: Array = []
		chunks.resize(chunk_count)
		received.resize(chunk_count)
		received.fill(false)
		record = {
			"raw_size": state_uncompressed_size,
			"payload_size": state_payload_size,
			"chunk_count": chunk_count,
			"data_chunk_count": data_chunk_count,
			"chunks": chunks,
			"received": received,
			"received_count": 0,
		}
		pending_chunks[state_tick] = record
	elif int(record.get("raw_size", -2)) != state_uncompressed_size or int(record.get("payload_size", -1)) != state_payload_size or int(record.get("chunk_count", -1)) != chunk_count:
		pending_chunks.erase(state_tick)
		log_chunk_bad_meta_drops += 1
		receive_profile_usec += Time.get_ticks_usec() - profile_start
		return
	var received_chunks: Array = record["received"]
	if bool(received_chunks[chunk_index]):
		log_chunk_dups_in += 1
		receive_profile_usec += Time.get_ticks_usec() - profile_start
		return
	received_chunks[chunk_index] = true
	var chunk_data: Array = record["chunks"]
	chunk_data[chunk_index] = chunk
	record["received_count"] = int(record["received_count"]) + 1
	if int(record["received_count"]) < data_chunk_count:
		receive_profile_usec += Time.get_ticks_usec() - profile_start
		return
	var recovered_chunks := _recover_missing_chunks(chunk_data, received_chunks, data_chunk_count, fec_group_count, state_payload_size)
	if recovered_chunks < 0:
		receive_profile_usec += Time.get_ticks_usec() - profile_start
		return
	var state := _assemble_state(chunk_data, data_chunk_count, state_payload_size)
	pending_chunks.erase(state_tick)
	var restored_state := state
	if state_uncompressed_size > 0:
		restored_state = state.decompress(state_uncompressed_size, FileAccess.COMPRESSION_ZSTD)
		if restored_state.size() != state_uncompressed_size:
			log_chunk_bad_meta_drops += 1
			return
	_log_received(state_uncompressed_size if state_uncompressed_size > 0 else state_payload_size, state_payload_size)
	log_chunk_completed += 1
	log_chunk_completed_count_max = maxi(log_chunk_completed_count_max, chunk_count)
	log_fec_recovered_chunks += recovered_chunks
	latest_state_tick = state_tick
	state_received.emit(state_tick, restored_state)
	mark_state_restored(state_tick)
	receive_profile_usec += Time.get_ticks_usec() - profile_start

func _recover_missing_chunks(chunks: Array, received: Array, data_chunk_count: int, fec_group_count: int, payload_size: int) -> int:
	var recovered_count := 0
	for group_index in fec_group_count:
		var first_data_index := group_index * FEC_DATA_CHUNKS
		var group_data_count := mini(FEC_DATA_CHUNKS, data_chunk_count - first_data_index)
		var first_wire_index := group_index * (FEC_DATA_CHUNKS + 1)
		var missing_local_index := -1
		var missing_count := 0
		for local_index in group_data_count:
			if !bool(received[first_wire_index + local_index]):
				missing_local_index = local_index
				missing_count += 1
		if missing_count == 0:
			continue
		var parity_index := first_wire_index + group_data_count
		if missing_count > 1 or !bool(received[parity_index]):
			return -1
		var recovered: PackedByteArray = (chunks[parity_index] as PackedByteArray).duplicate()
		for local_index in group_data_count:
			if local_index == missing_local_index:
				continue
			var source: PackedByteArray = chunks[first_wire_index + local_index]
			for byte_index in source.size():
				recovered[byte_index] = recovered[byte_index] ^ source[byte_index]
		var missing_data_index := first_data_index + missing_local_index
		chunks[first_wire_index + missing_local_index] = recovered.slice(0, mini(CHUNK_PAYLOAD_BYTES, payload_size - missing_data_index * CHUNK_PAYLOAD_BYTES))
		received[first_wire_index + missing_local_index] = true
		recovered_count += 1
	return recovered_count

func _assemble_state(chunks: Array, data_chunk_count: int, payload_size: int) -> PackedByteArray:
	var state := PackedByteArray()
	state.resize(payload_size)
	var offset := 0
	for state_data_index in data_chunk_count:
		var group_index := state_data_index / FEC_DATA_CHUNKS
		var local_index := state_data_index % FEC_DATA_CHUNKS
		var part: PackedByteArray = chunks[group_index * (FEC_DATA_CHUNKS + 1) + local_index]
		for byte_index in part.size():
			state[offset + byte_index] = part[byte_index]
		offset += part.size()
	return state

func _log_sent(raw_size: int, payload_size: int) -> void:
	if payload_size <= 0:
		return
	var fragments := int(ceil(float(payload_size) / float(CHUNK_PAYLOAD_BYTES)))
	log_raw_out += maxi(raw_size, 0)
	log_payload_out += payload_size
	log_sent_count += 1
	log_max_fragments_out = maxi(log_max_fragments_out, fragments)
	log_min_success_2pct = minf(log_min_success_2pct, pow(0.98, float(fragments)))

func _log_received(raw_size: int, payload_size: int) -> void:
	if payload_size <= 0:
		return
	var now := Time.get_ticks_msec()
	if log_last_recv_ms >= 0:
		log_max_recv_gap_ms = maxi(log_max_recv_gap_ms, now - log_last_recv_ms)
	log_last_recv_ms = now
	log_payload_in += payload_size
	log_raw_in += maxi(raw_size, 0)
	log_recv_count += 1

func _log_size_stats(stats: Dictionary) -> void:
	if stats.is_empty():
		return
	log_sec_header += int(stats.get("header", 0))
	log_sec_bumper_meta += int(stats.get("bumper_meta", 0))
	log_sec_sparks += int(stats.get("sparks", 0))
	log_sec_car_scalars += int(stats.get("car_scalars", 0))
	log_sec_car_vec3 += int(stats.get("car_vec3", 0))
	log_sec_car_basis += int(stats.get("car_basis", 0))
	log_sec_car_conditionals += int(stats.get("car_conditionals", 0))
	log_sec_car_tilt += int(stats.get("car_tilt", 0))
	log_sec_car_wall += int(stats.get("car_wall", 0))
	log_sec_bumper_total += int(stats.get("bumper_scalars", 0)) + int(stats.get("bumper_vec3", 0)) + int(stats.get("bumper_transform", 0)) + int(stats.get("bumper_basis", 0)) + int(stats.get("bumper_conditionals", 0)) + int(stats.get("bumper_tilt", 0)) + int(stats.get("bumper_wall", 0))
	log_sec_triggers += int(stats.get("triggers", 0))
	log_sec_total += int(stats.get("total", 0))
	log_stat_car_count = maxi(log_stat_car_count, int(stats.get("car_count", 0)))
	log_stat_bumper_count = maxi(log_stat_bumper_count, int(stats.get("bumper_count", 0)))
	log_stat_active_bumpers = maxi(log_stat_active_bumpers, int(stats.get("active_bumper_count", 0)))
	log_stat_active_sparks = maxi(log_stat_active_sparks, int(stats.get("active_spark_count", 0)))
	log_stat_trigger_count = maxi(log_stat_trigger_count, int(stats.get("trigger_count", 0)))
	log_stat_car_collision_old = maxi(log_stat_car_collision_old, int(stats.get("car_collision_old_count", 0)))
	log_stat_car_restore = maxi(log_stat_car_restore, int(stats.get("car_restore_count", 0)))

func _can_send_to_peer(peer_id: int, network_active: bool, listen_server: bool) -> bool:
	if !network_active or multiplayer.multiplayer_peer == null:
		return false
	if listen_server and peer_id == multiplayer.get_unique_id():
		return true
	return multiplayer.get_peers().has(peer_id)

func _pack_phase_tick(tick: int) -> int:
	return (tick & RACE_PHASE_TICK_MASK) | ((race_phase & 1) << 31)

func _unpack_phase(packed_tick: int) -> int:
	return 1 if (packed_tick & RACE_PHASE_TICK_BIT) != 0 else 0

func _unpack_tick(packed_tick: int) -> int:
	return int(packed_tick & RACE_PHASE_TICK_MASK)
