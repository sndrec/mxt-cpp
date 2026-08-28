class_name LegacyLeaderboardReplayReader extends RefCounted

# This is the sole compatibility boundary for leaderboard replay JSON. It keeps
# pending pre-0.3.2 submissions and recovered Steam attachments verifiable while
# new recordings and every ordinary local replay path remain binary.

const ReplayValidatorClass = preload("res://leaderboards/leaderboard_replay_validator.gd")
const CURRENT_BINARY_SCHEMA := 5


static func convert(game_manager: GameManager, bytes: PackedByteArray) -> Dictionary:
	var parsed = JSON.parse_string(bytes.get_string_from_utf8())
	if typeof(parsed) != TYPE_DICTIONARY:
		return {"valid": false, "reason": "invalid_legacy_leaderboard_replay"}
	var replay: Dictionary = parsed
	var validation := ReplayValidatorClass.validate(game_manager, replay)
	if !bool(validation.get("valid", false)):
		return validation
	var racer_ids_value = replay.get("racer_ids", [])
	var cpu_flags_value = replay.get("cpu_flags", [])
	var frames_value = replay.get("frames", [])
	if typeof(racer_ids_value) != TYPE_ARRAY or typeof(cpu_flags_value) != TYPE_ARRAY \
			or typeof(frames_value) != TYPE_ARRAY:
		return {"valid": false, "reason": "invalid_legacy_leaderboard_replay"}
	var racer_ids: Array = racer_ids_value
	var stream := MxtReplayStream.new()
	stream.begin_recording(racer_ids, cpu_flags_value as Array)
	if !stream.get_last_error().is_empty():
		return {"valid": false, "reason": "invalid_legacy_leaderboard_replay"}
	for tick in range((frames_value as Array).size()):
		var frame: Dictionary = (frames_value as Array)[tick]
		var encoded_inputs: Dictionary = frame.get("inputs", {})
		var decoded_inputs := {}
		for racer_id_value in racer_ids:
			var racer_id := int(racer_id_value)
			var key := str(racer_id)
			if !encoded_inputs.has(key) or typeof(encoded_inputs[key]) != TYPE_STRING:
				return {"valid": false, "reason": "noncanonical_frames"}
			decoded_inputs[racer_id] = Marshalls.base64_to_raw(String(encoded_inputs[key]))
		if !stream.append_frame_inputs(tick, decoded_inputs):
			return {"valid": false, "reason": "noncanonical_frames"}
	var binary_metadata := replay.duplicate(true)
	binary_metadata.erase("frames")
	binary_metadata["legacy_leaderboard_schema_version"] = int(replay.get("schema_version", -1))
	binary_metadata["schema_version"] = CURRENT_BINARY_SCHEMA
	validation["_native_stream"] = stream
	validation["_native_metadata"] = binary_metadata
	validation["_legacy_attachment"] = true
	return validation
