extends SceneTree

const TEST_PATH := "user://replay_stream_smoke.mxt_replay"
const CORRUPT_PATH := "user://replay_stream_corrupt.mxt_replay"


func _fail(message: String) -> void:
	push_error("MXT_REPLAY_STREAM_SMOKE_FAIL " + message)
	quit(1)


func _init() -> void:
	var stream := MxtReplayStream.new()
	stream.begin_recording([11, 22], [false, true])
	for tick in range(600):
		var input_a := PackedByteArray([0])
		var input_b := PackedByteArray([1, tick & 0xff])
		if !stream.append_frame_inputs(tick, {11: input_a, 22: input_b}):
			_fail("append failed at tick %d" % tick)
			return
	var retained := stream.retain_head()
	if !stream.truncate_to(500) or stream.frame_count() != 500:
		_fail("truncate failed")
		return
	if !stream.restore_head(retained) or stream.frame_count() != 600:
		_fail("retained branch restore failed")
		return
	var metadata := {"schema_version": 5, "name": "Native stream smoke"}
	if !stream.write_file(TEST_PATH, metadata):
		_fail("write failed: %s" % stream.get_last_error())
		return
	var metadata_stream := MxtReplayStream.new()
	if !metadata_stream.load_file(TEST_PATH, true) or metadata_stream.frame_count() != 600 \
			or String(metadata_stream.get_metadata().get("name", "")) != "Native stream smoke":
		_fail("metadata-only read failed")
		return
	metadata["name"] = "Renamed stream smoke"
	if !metadata_stream.rewrite_metadata(TEST_PATH, metadata):
		_fail("metadata rewrite failed: %s" % metadata_stream.get_last_error())
		return
	var loaded := MxtReplayStream.new()
	if !loaded.load_file(TEST_PATH) or loaded.get_roster_ids() != [11, 22]:
		_fail("full read failed: %s" % loaded.get_last_error())
		return
	for tick in [0, 255, 256, 599]:
		var frame := loaded.read_frame(tick)
		var inputs: Dictionary = frame.get("inputs", {})
		if int(frame.get("tick", -1)) != tick or inputs.get(11) != PackedByteArray([0]) \
				or inputs.get(22) != PackedByteArray([1, tick & 0xff]):
			_fail("random read mismatch at tick %d" % tick)
			return
	var player_stream := loaded.get_player_input_stream(1)
	if int(player_stream.get("frame_count", 0)) != 600 \
			or (player_stream.get("input_bytes", PackedByteArray()) as PackedByteArray).size() != 1200:
		_fail("player stream extraction failed")
		return
	var copied := MxtReplayStream.new()
	if !copied.copy_prefix_from(loaded, 333) or copied.frame_count() != 333 \
			or copied.read_frame(332).get("inputs", {}).get(22) != PackedByteArray([1, 332 & 0xff]):
		_fail("native prefix copy failed")
		return
	var valid_bytes := FileAccess.get_file_as_bytes(TEST_PATH)
	var corrupt := valid_bytes.duplicate()
	corrupt[0] ^= 1
	if !_expect_rejected(corrupt, "magic"):
		return
	corrupt = valid_bytes.duplicate()
	corrupt.resize(corrupt.size() - 1)
	if !_expect_rejected(corrupt, "truncation"):
		return
	corrupt = valid_bytes.duplicate()
	var metadata_capacity := _load_u64(corrupt, 32) - _load_u64(corrupt, 16)
	_store_u64(corrupt, 24, metadata_capacity + 1)
	_update_header_crc(corrupt)
	if !_expect_rejected(corrupt, "metadata bounds"):
		return
	corrupt = valid_bytes.duplicate()
	corrupt[128] ^= 1
	if !_expect_rejected(corrupt, "metadata checksum"):
		return
	corrupt = valid_bytes.duplicate()
	corrupt[_load_u64(corrupt, 32)] ^= 1
	if !_expect_rejected(corrupt, "roster checksum"):
		return
	corrupt = valid_bytes.duplicate()
	corrupt[_load_u64(corrupt, 48)] ^= 1
	if !_expect_rejected(corrupt, "index checksum"):
		return
	corrupt = valid_bytes.duplicate()
	var frames_offset := _load_u64(corrupt, 64)
	corrupt[frames_offset] ^= 1
	if !_expect_rejected(corrupt, "frame-section checksum"):
		return
	corrupt = valid_bytes.duplicate()
	frames_offset = _load_u64(corrupt, 64)
	var frames_size := _load_u64(corrupt, 72)
	corrupt[frames_offset] ^= 1
	_store_u32(corrupt, 108, _crc32(corrupt.slice(frames_offset, frames_offset + frames_size)))
	_update_header_crc(corrupt)
	if !_expect_rejected(corrupt, "compressed block checksum", true):
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TEST_PATH))
	if FileAccess.file_exists(CORRUPT_PATH):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(CORRUPT_PATH))
	print("MXT_REPLAY_STREAM_SMOKE_OK frames=600 blocks=%d" % int(loaded.get_stats().get("block_count", 0)))
	quit()


func _expect_rejected(bytes: PackedByteArray, label: String, defer_block_validation := false) -> bool:
	var file := FileAccess.open(CORRUPT_PATH, FileAccess.WRITE)
	if file == null:
		_fail("could not create corrupt %s fixture" % label)
		return false
	file.store_buffer(bytes)
	file.close()
	var stream := MxtReplayStream.new()
	var loaded := stream.load_file(CORRUPT_PATH)
	var rejected := !loaded or (defer_block_validation and stream.read_frame(0).is_empty())
	if !rejected:
		_fail("%s corruption was accepted" % label)
		return false
	return true


func _load_u64(bytes: PackedByteArray, offset: int) -> int:
	var value := 0
	for byte_index in range(8):
		value |= int(bytes[offset + byte_index]) << (byte_index * 8)
	return value


func _store_u32(bytes: PackedByteArray, offset: int, value: int) -> void:
	for byte_index in range(4):
		bytes[offset + byte_index] = (value >> (byte_index * 8)) & 0xff


func _store_u64(bytes: PackedByteArray, offset: int, value: int) -> void:
	for byte_index in range(8):
		bytes[offset + byte_index] = (value >> (byte_index * 8)) & 0xff


func _update_header_crc(bytes: PackedByteArray) -> void:
	_store_u32(bytes, 120, 0)
	_store_u32(bytes, 120, _crc32(bytes.slice(0, 120)))


func _crc32(bytes: PackedByteArray) -> int:
	var crc := 0xffffffff
	for byte_value in bytes:
		crc ^= int(byte_value)
		for _bit in range(8):
			crc = ((crc >> 1) ^ (0xedb88320 if (crc & 1) != 0 else 0)) & 0xffffffff
	return (~crc) & 0xffffffff
