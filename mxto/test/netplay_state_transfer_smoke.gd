extends SceneTree

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _init() -> void:
	var state_transfer := StateTransferController.new()
	state_transfer.initialize(NetcodeSession.new())
	state_transfer.set_race_context(true, 1)

	var payload := PackedByteArray()
	payload.resize(2501)
	for i in range(payload.size()):
		payload[i] = i & 0xff
	state_transfer._queue_transfer(7, 90, -1, payload)
	var transfer: Dictionary = state_transfer.outgoing_transfers.get(7, {})
	var queued_chunks: Array = transfer.get("chunks", [])
	if queued_chunks.size() != 4 or (queued_chunks[0] as PackedByteArray).size() != 1000 or (queued_chunks[2] as PackedByteArray).size() != 501 or (queued_chunks[3] as PackedByteArray).size() != 1000:
		_fail("state transfer did not include bounded data plus forward parity: %s" % [transfer])
		return

	var packed_tick := state_transfer._pack_phase_tick(90)
	var chunks: Array = transfer["chunks"]
	state_transfer._server_state_chunk(packed_tick, -1, payload.size(), 1, chunks.size(), chunks[1])
	state_transfer._server_state_chunk(packed_tick, -1, payload.size(), 1, chunks.size(), chunks[1])
	if state_transfer.log_chunk_dups_in != 1:
		_fail("duplicate snapshot chunk was not counted")
		return
	state_transfer._server_state_chunk(packed_tick, -1, payload.size(), 0, chunks.size(), chunks[0])
	if !state_transfer.pending_chunks.has(90):
		_fail("incomplete state transfer was discarded before parity arrived")
		return
	state_transfer._server_state_chunk(packed_tick, -1, payload.size(), 3, chunks.size(), chunks[3])
	if state_transfer.log_chunk_completed != 1 or state_transfer.log_fec_recovered_chunks != 1 or state_transfer.pending_chunks.has(90):
		_fail("forward parity did not recover one missing data chunk locally")
		return

	var oversized := PackedByteArray()
	oversized.resize(state_transfer.CHUNK_PAYLOAD_BYTES + 1)
	state_transfer._server_state_chunk(state_transfer._pack_phase_tick(91), -1, oversized.size(), 0, 1, oversized)
	if state_transfer.log_chunk_bad_meta_drops != 1:
		_fail("oversized state chunk was not rejected")
		return

	state_transfer._server_state_chunk(state_transfer._pack_phase_tick(93), -1, payload.size(), 0, chunks.size(), chunks[0])
	state_transfer._server_state_chunk(state_transfer._pack_phase_tick(93), -1, payload.size(), 3, chunks.size(), chunks[3])
	if !state_transfer.pending_chunks.has(93):
		_fail("parity incorrectly recovered two missing data chunks")
		return

	var compressed := payload.compress(FileAccess.COMPRESSION_ZSTD)
	state_transfer._queue_transfer(7, 94, payload.size(), compressed)
	var compressed_transfer: Dictionary = state_transfer.outgoing_transfers[7]
	var compressed_chunks: Array = compressed_transfer["chunks"]
	var compressed_data_count := int(compressed_transfer["data_chunk_count"])
	for data_index in range(compressed_data_count):
		var group_index := data_index / state_transfer.FEC_DATA_CHUNKS
		var local_index := data_index % state_transfer.FEC_DATA_CHUNKS
		var i := group_index * (state_transfer.FEC_DATA_CHUNKS + 1) + local_index
		state_transfer._server_state_chunk(
			state_transfer._pack_phase_tick(94),
			payload.size(),
			compressed.size(),
			i,
			compressed_chunks.size(),
			compressed_chunks[i])
	if state_transfer.log_chunk_completed != 2:
		_fail("valid compressed state transfer did not complete")
		return
	if state_transfer.log_fec_abandoned != 1:
		_fail("unrecoverable older snapshot was not abandoned when forward data arrived")
		return

	print("MXT_NETPLAY_STATE_TRANSFER_OK")
	quit(0)
