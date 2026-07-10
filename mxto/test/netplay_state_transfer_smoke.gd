extends SceneTree

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _init() -> void:
	var nm := NetworkManager.new()
	nm.is_server = true
	nm.race_active = true
	nm.race_netplay_phase = 1

	var payload := PackedByteArray()
	payload.resize(2501)
	for i in range(payload.size()):
		payload[i] = i & 0xff
	nm._queue_state_transfer(7, 90, -1, payload)
	var transfer: Dictionary = nm.outgoing_state_transfers.get(7, {})
	var queued_chunks: Array = transfer.get("chunks", [])
	if queued_chunks.size() != 4 or (queued_chunks[0] as PackedByteArray).size() != 1000 or (queued_chunks[2] as PackedByteArray).size() != 501 or (queued_chunks[3] as PackedByteArray).size() != 1000:
		_fail("state transfer did not include bounded data plus forward parity: %s" % [transfer])
		return

	nm.is_server = false
	var packed_tick := nm._pack_race_phase_tick(90)
	var chunks: Array = transfer["chunks"]
	nm._server_state_chunk(packed_tick, -1, payload.size(), 1, chunks.size(), chunks[1])
	nm._server_state_chunk(packed_tick, -1, payload.size(), 1, chunks.size(), chunks[1])
	if nm.log_state_chunk_dups_in != 1:
		_fail("duplicate snapshot chunk was not counted")
		return
	nm._server_state_chunk(packed_tick, -1, payload.size(), 0, chunks.size(), chunks[0])
	if !nm.pending_state_chunks.has(90):
		_fail("incomplete state transfer was discarded before parity arrived")
		return
	nm._server_state_chunk(packed_tick, -1, payload.size(), 3, chunks.size(), chunks[3])
	if nm.log_state_chunk_completed != 1 or nm.log_state_fec_recovered_chunks != 1 or nm.pending_state_chunks.has(90):
		_fail("forward parity did not recover one missing data chunk locally")
		return

	var oversized := PackedByteArray()
	oversized.resize(nm.STATE_CHUNK_PAYLOAD_BYTES + 1)
	nm._server_state_chunk(nm._pack_race_phase_tick(91), -1, oversized.size(), 0, 1, oversized)
	if nm.log_state_chunk_bad_meta_drops != 1:
		_fail("oversized state chunk was not rejected")
		return

	nm._server_state_chunk(nm._pack_race_phase_tick(93), -1, payload.size(), 0, chunks.size(), chunks[0])
	nm._server_state_chunk(nm._pack_race_phase_tick(93), -1, payload.size(), 3, chunks.size(), chunks[3])
	if !nm.pending_state_chunks.has(93):
		_fail("parity incorrectly recovered two missing data chunks")
		return

	var compressed := payload.compress(FileAccess.COMPRESSION_ZSTD)
	nm._queue_state_transfer(7, 94, payload.size(), compressed)
	var compressed_transfer: Dictionary = nm.outgoing_state_transfers[7]
	var compressed_chunks: Array = compressed_transfer["chunks"]
	var compressed_data_count := int(compressed_transfer["data_chunk_count"])
	for data_index in range(compressed_data_count):
		var group_index := data_index / nm.STATE_FEC_DATA_CHUNKS
		var local_index := data_index % nm.STATE_FEC_DATA_CHUNKS
		var i := group_index * (nm.STATE_FEC_DATA_CHUNKS + 1) + local_index
		nm._server_state_chunk(
			nm._pack_race_phase_tick(94),
			payload.size(),
			compressed.size(),
			i,
			compressed_chunks.size(),
			compressed_chunks[i])
	if nm.log_state_chunk_completed != 2:
		_fail("valid compressed state transfer did not complete")
		return
	if nm.log_state_fec_abandoned != 1:
		_fail("unrecoverable older snapshot was not abandoned when forward data arrived")
		return

	print("MXT_NETPLAY_STATE_TRANSFER_OK")
	quit(0)
