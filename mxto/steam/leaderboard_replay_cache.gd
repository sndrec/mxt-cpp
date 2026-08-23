class_name LeaderboardReplayCache extends Node

signal request_completed(token: int, result: Dictionary)

const ReplayValidatorClass = preload("res://steam/leaderboard_replay_validator.gd")
const LegacyLeaderboardReplayReaderClass = preload("res://steam/legacy_leaderboard_replay_reader.gd")
const CACHE_ROOT := "user://leaderboard_replays"
const MAX_REPLAY_BYTES := 64 * 1024 * 1024

var game_manager: GameManager
var steam_service: MxtSteamService
var cache_root := CACHE_ROOT

var next_token := 1
var requests_by_token: Dictionary = {}
var waiters_by_digest: Dictionary = {}
var queued_digests: Array[String] = []
var validated_session_requests: Dictionary = {}

var active_digest := ""
var active_download_request_id := 0
var active_download_context: Dictionary = {}

var cache_hit_count := 0
var downloaded_replay_count := 0
var downloaded_byte_count := 0


func initialize(manager: GameManager, service: MxtSteamService) -> void:
	game_manager = manager
	steam_service = service
	if steam_service != null:
		steam_service.leaderboard_replay_download_completed.connect(_on_download_completed)


func request_replay(board_name: String, entry: Dictionary) -> int:
	var token := next_token
	next_token += 1
	var request := _build_request(board_name, entry)
	request["token"] = token
	requests_by_token[token] = request
	call_deferred("_begin_request", token)
	return token


func cancel_request(token: int) -> void:
	var request_value = requests_by_token.get(token, {})
	if typeof(request_value) != TYPE_DICTIONARY or (request_value as Dictionary).is_empty():
		return
	var request: Dictionary = request_value
	var digest := String(request.get("replay_sha256", ""))
	requests_by_token.erase(token)
	_remove_waiter(digest, token)
	if digest != active_digest and !_has_waiters(digest):
		queued_digests.erase(digest)


func stats() -> Dictionary:
	return {
		"cache_hits": cache_hit_count,
		"downloaded_replays": downloaded_replay_count,
		"downloaded_bytes": downloaded_byte_count,
		"queued_downloads": queued_digests.size(),
		"active_download": !active_digest.is_empty(),
	}


func _build_request(board_name: String, entry: Dictionary) -> Dictionary:
	var details_value = entry.get("_trusted_details", {})
	if typeof(details_value) != TYPE_DICTIONARY:
		return {"error": "missing_trusted_replay_metadata"}
	var details: Dictionary = (details_value as Dictionary).duplicate(true)
	var replay_digest := String(details.get("replay_sha256", ""))
	var digest_hex := _digest_hex(replay_digest)
	var ugc_handle := int(entry.get("ugc_handle", 0))
	if digest_hex.is_empty() or ugc_handle == 0 or ugc_handle == -1:
		return {"error": "missing_replay_attachment"}
	if board_name.is_empty():
		return {"error": "missing_leaderboard_name"}
	return {
		"board_name": board_name,
		"ugc_handle": ugc_handle,
		"trusted_details": details,
		"replay_sha256": replay_digest,
		"digest_hex": digest_hex,
		"source_path": cache_root.path_join(digest_hex + ".attachment"),
		"cache_path": cache_root.path_join(digest_hex + ".mxt_replay"),
		"validation_key": _validation_key(board_name, details),
	}


func _begin_request(token: int) -> void:
	var request_value = requests_by_token.get(token, {})
	if typeof(request_value) != TYPE_DICTIONARY or (request_value as Dictionary).is_empty():
		return
	var request: Dictionary = request_value
	var error := String(request.get("error", ""))
	if !error.is_empty():
		_complete_request(token, _failure(error))
		return
	var validation_key := String(request.get("validation_key", ""))
	var cache_path := String(request.get("cache_path", ""))
	if validated_session_requests.has(validation_key) and FileAccess.file_exists(cache_path):
		cache_hit_count += 1
		_complete_request(token, _success(request, cache_path, true, validated_session_requests[validation_key]))
		return
	var digest := String(request.get("replay_sha256", ""))
	_add_waiter(digest, token)
	if digest != active_digest and !queued_digests.has(digest):
		queued_digests.append(digest)
	_pump_queue()


func _pump_queue() -> void:
	if !active_digest.is_empty():
		return
	while !queued_digests.is_empty():
		var digest: String = String(queued_digests.pop_front())
		if !_has_waiters(digest):
			continue
		active_digest = digest
		active_download_context = _first_waiting_request(digest)
		var source_path := String(active_download_context.get("source_path", ""))
		if FileAccess.file_exists(source_path):
			var cached_bytes := FileAccess.get_file_as_bytes(source_path)
			var retry_download := _complete_waiters_from_bytes(digest, cached_bytes, true)
			if !retry_download:
				_finish_active_digest()
				return
			DirAccess.remove_absolute(ProjectSettings.globalize_path(source_path))
			var stale_cache_path := String(active_download_context.get("cache_path", ""))
			if FileAccess.file_exists(stale_cache_path):
				DirAccess.remove_absolute(ProjectSettings.globalize_path(stale_cache_path))
		if steam_service == null or !steam_service.is_initialized():
			_fail_waiters(digest, _failure("steam_offline"))
			_finish_active_digest()
			return
		active_download_request_id = steam_service.download_leaderboard_replay(
			int(active_download_context.get("ugc_handle", 0)),
			MAX_REPLAY_BYTES)
		if active_download_request_id <= 0:
			_fail_waiters(digest, _failure("steam_download_start_failed"))
			_finish_active_digest()
		return


func _on_download_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_download_request_id or active_digest.is_empty():
		return
	active_download_request_id = 0
	var digest := active_digest
	if !bool(result.get("success", false)):
		_fail_waiters(digest, _failure(
			"steam_download_failed",
			String(result.get("message", "Steam replay download failed."))))
		_finish_active_digest()
		return
	var bytes_value = result.get("bytes", PackedByteArray())
	if typeof(bytes_value) != TYPE_PACKED_BYTE_ARRAY:
		_fail_waiters(digest, _failure("invalid_steam_payload"))
		_finish_active_digest()
		return
	var bytes: PackedByteArray = bytes_value
	downloaded_replay_count += 1
	downloaded_byte_count += bytes.size()
	_complete_downloaded_waiters(digest, bytes)
	_finish_active_digest()


func _complete_waiters_from_bytes(digest: String, bytes: PackedByteArray, cache_hit: bool) -> bool:
	var retry_download := false
	var waiter_tokens := _waiter_tokens(digest)
	for token in waiter_tokens:
		var request_value = requests_by_token.get(token, {})
		if typeof(request_value) != TYPE_DICTIONARY or (request_value as Dictionary).is_empty():
			_remove_waiter(digest, token)
			continue
		var request: Dictionary = request_value
		var validation := _validate_replay(bytes, request)
		if bool(validation.get("valid", false)):
			if !_materialize_binary_cache(request, bytes, validation):
				_complete_request(token, _failure("cache_write_failed"))
				_remove_waiter(digest, token)
				continue
			var public_validation := _public_validation(validation)
			validated_session_requests[String(request.get("validation_key", ""))] = public_validation
			if cache_hit:
				cache_hit_count += 1
			_complete_request(token, _success(
				request,
				String(request.get("cache_path", "")),
				cache_hit,
				public_validation))
			_remove_waiter(digest, token)
		elif cache_hit and _cache_failure_can_redownload(String(validation.get("reason", ""))):
			retry_download = true
		else:
			_complete_request(token, _validation_failure(validation))
			_remove_waiter(digest, token)
	return retry_download and _has_waiters(digest)


func _complete_downloaded_waiters(digest: String, bytes: PackedByteArray) -> void:
	var waiter_tokens := _waiter_tokens(digest)
	var validations: Dictionary = {}
	var has_valid_request := false
	var warm_validation: Dictionary = {}
	for token in waiter_tokens:
		var request_value = requests_by_token.get(token, {})
		if typeof(request_value) != TYPE_DICTIONARY or (request_value as Dictionary).is_empty():
			continue
		var validation := _validate_replay(bytes, request_value as Dictionary)
		validations[token] = validation
		has_valid_request = has_valid_request or bool(validation.get("valid", false))

	# An unchecked row may be the final consumer while its native download is in
	# flight. Validate against the captured request so the completed transfer can
	# still warm the digest cache without re-selecting or notifying that row.
	if waiter_tokens.is_empty() and !active_download_context.is_empty():
		warm_validation = _validate_replay(bytes, active_download_context)
		has_valid_request = bool(warm_validation.get("valid", false))

	var source_path := String(active_download_context.get("source_path", ""))
	if has_valid_request and !_write_cache_atomically(source_path, bytes):
		_fail_waiters(digest, _failure("cache_write_failed"))
		return
	if waiter_tokens.is_empty() and has_valid_request \
			and !_materialize_binary_cache(active_download_context, bytes, warm_validation):
		return
	for token in waiter_tokens:
		var request_value = requests_by_token.get(token, {})
		if typeof(request_value) != TYPE_DICTIONARY or (request_value as Dictionary).is_empty():
			_remove_waiter(digest, token)
			continue
		var request: Dictionary = request_value
		var validation_value = validations.get(token, {})
		var validation: Dictionary = validation_value if typeof(validation_value) == TYPE_DICTIONARY else {}
		if bool(validation.get("valid", false)):
			if !_materialize_binary_cache(request, bytes, validation):
				_complete_request(token, _failure("cache_write_failed"))
				_remove_waiter(digest, token)
				continue
			var public_validation := _public_validation(validation)
			validated_session_requests[String(request.get("validation_key", ""))] = public_validation
			_complete_request(token, _success(request, String(request.get("cache_path", "")), false, public_validation))
		else:
			_complete_request(token, _validation_failure(validation))
		_remove_waiter(digest, token)


func _validate_replay(bytes: PackedByteArray, request: Dictionary) -> Dictionary:
	if bytes.is_empty() or bytes.size() > MAX_REPLAY_BYTES:
		return {"valid": false, "reason": "invalid_replay_size"}
	var details_value = request.get("trusted_details", {})
	if typeof(details_value) != TYPE_DICTIONARY:
		return {"valid": false, "reason": "missing_trusted_replay_metadata"}
	var details: Dictionary = details_value
	if _sha256(bytes) != String(details.get("replay_sha256", "")):
		return {"valid": false, "reason": "replay_digest_mismatch"}
	var validation: Dictionary
	if _has_binary_magic(bytes):
		validation = _validate_binary_attachment(bytes)
	else:
		validation = LegacyLeaderboardReplayReaderClass.convert(game_manager, bytes)
	if !bool(validation.get("valid", false)):
		return validation
	if String(validation.get("board_name", "")) != String(request.get("board_name", "")) \
			or int(validation.get("ruleset_revision", -1)) != int(details.get("ruleset_revision", -2)) \
			or int(validation.get("replay_schema_version", -1)) != int(details.get("replay_schema_version", -2)) \
			or String(validation.get("track_gameplay_digest", "")) != String(details.get("track_gameplay_digest", "")) \
			or String(validation.get("vehicle_gameplay_digest", "")) != String(details.get("vehicle_gameplay_digest", "")):
		return {"valid": false, "reason": "trusted_metadata_mismatch"}
	var trusted_machine_setting := int(details.get("machine_setting_percent", -1))
	if trusted_machine_setting >= 0 \
			and int(validation.get("machine_setting_percent", -2)) != trusted_machine_setting:
		return {"valid": false, "reason": "trusted_metadata_mismatch"}
	return validation


func _has_binary_magic(bytes: PackedByteArray) -> bool:
	return bytes.size() >= 8 and bytes[0] == 77 and bytes[1] == 88 and bytes[2] == 84 \
		and bytes[3] == 82 and bytes[4] == 80 and bytes[5] == 76 and bytes[6] == 89 and bytes[7] == 0


func _validate_binary_attachment(bytes: PackedByteArray) -> Dictionary:
	if DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(cache_root)) != OK:
		return {"valid": false, "reason": "cache_write_failed"}
	var temporary_path := cache_root.path_join("validate_%d_%d.tmp" % [Time.get_ticks_usec(), randi()])
	var file := FileAccess.open(temporary_path, FileAccess.WRITE)
	if file == null:
		return {"valid": false, "reason": "cache_write_failed"}
	file.store_buffer(bytes)
	file.close()
	var stream := MxtReplayStream.new()
	var loaded := stream.load_file(temporary_path)
	var metadata := stream.get_metadata() if loaded else {}
	var validation := ReplayValidatorClass.validate(game_manager, metadata, stream) if loaded else {
		"valid": false,
		"reason": "invalid_binary_replay",
	}
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary_path))
	if bool(validation.get("valid", false)):
		validation["_legacy_attachment"] = false
	return validation


func _materialize_binary_cache(request: Dictionary, source_bytes: PackedByteArray, validation: Dictionary) -> bool:
	var cache_path := String(request.get("cache_path", ""))
	if FileAccess.file_exists(cache_path) and MxtReplayStream.path_has_binary_magic(cache_path):
		var cached_stream := MxtReplayStream.new()
		if cached_stream.load_file(cache_path):
			return true
		DirAccess.remove_absolute(ProjectSettings.globalize_path(cache_path))
	if bool(validation.get("_legacy_attachment", false)):
		var stream: MxtReplayStream = validation.get("_native_stream")
		var metadata_value = validation.get("_native_metadata", {})
		if stream == null or typeof(metadata_value) != TYPE_DICTIONARY:
			return false
		var temporary_path := cache_path + ".tmp"
		if !stream.write_file(temporary_path, metadata_value as Dictionary):
			return false
		if FileAccess.file_exists(cache_path):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(cache_path))
		return DirAccess.rename_absolute(ProjectSettings.globalize_path(temporary_path), ProjectSettings.globalize_path(cache_path)) == OK
	return _write_cache_atomically(cache_path, source_bytes)


func _public_validation(validation: Dictionary) -> Dictionary:
	var output := validation.duplicate(false)
	output.erase("_native_stream")
	output.erase("_native_metadata")
	output.erase("_legacy_attachment")
	return output


func _write_cache_atomically(path: String, bytes: PackedByteArray) -> bool:
	if path.is_empty() or DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(cache_root)) != OK:
		return false
	var temporary_path := path + ".tmp"
	var file := FileAccess.open(temporary_path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(bytes)
	file.flush()
	file.close()
	var absolute_target := ProjectSettings.globalize_path(path)
	var absolute_temporary := ProjectSettings.globalize_path(temporary_path)
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(absolute_target)
	return DirAccess.rename_absolute(absolute_temporary, absolute_target) == OK


func _finish_active_digest() -> void:
	active_digest = ""
	active_download_request_id = 0
	active_download_context.clear()
	call_deferred("_pump_queue")


func _complete_request(token: int, result: Dictionary) -> void:
	if !requests_by_token.has(token):
		return
	requests_by_token.erase(token)
	request_completed.emit(token, result)


func _fail_waiters(digest: String, result: Dictionary) -> void:
	for token in _waiter_tokens(digest):
		_complete_request(token, result.duplicate(true))
	waiters_by_digest.erase(digest)


func _add_waiter(digest: String, token: int) -> void:
	var waiters: Array = waiters_by_digest.get(digest, [])
	if !waiters.has(token):
		waiters.append(token)
	waiters_by_digest[digest] = waiters


func _remove_waiter(digest: String, token: int) -> void:
	var waiters_value = waiters_by_digest.get(digest, [])
	if typeof(waiters_value) != TYPE_ARRAY:
		return
	var waiters: Array = waiters_value
	waiters.erase(token)
	if waiters.is_empty():
		waiters_by_digest.erase(digest)
	else:
		waiters_by_digest[digest] = waiters


func _waiter_tokens(digest: String) -> Array:
	var waiters_value = waiters_by_digest.get(digest, [])
	return (waiters_value as Array).duplicate() if typeof(waiters_value) == TYPE_ARRAY else []


func _has_waiters(digest: String) -> bool:
	return !_waiter_tokens(digest).is_empty()


func _first_waiting_request(digest: String) -> Dictionary:
	for token in _waiter_tokens(digest):
		var request_value = requests_by_token.get(token, {})
		if typeof(request_value) == TYPE_DICTIONARY and !(request_value as Dictionary).is_empty():
			return (request_value as Dictionary).duplicate(true)
	return {}


func _cache_failure_can_redownload(reason: String) -> bool:
	return reason == "invalid_replay_size" \
		or reason == "replay_digest_mismatch" \
		or reason == "invalid_legacy_leaderboard_replay" \
		or reason == "invalid_binary_replay"


func _success(request: Dictionary, cache_path: String, cache_hit: bool, validation: Dictionary) -> Dictionary:
	return {
		"success": true,
		"reason": "",
		"message": "Validated cached leaderboard replay ready." if cache_hit else "Leaderboard replay downloaded and validated.",
		"cache_path": cache_path,
		"replay_sha256": String(request.get("replay_sha256", "")),
		"trusted_details": (request.get("trusted_details", {}) as Dictionary).duplicate(true),
		"validation": validation.duplicate(true),
		"cache_hit": cache_hit,
	}


func _failure(reason: String, detail := "") -> Dictionary:
	var messages := {
		"missing_trusted_replay_metadata": "This leaderboard entry has no trusted replay metadata.",
		"missing_replay_attachment": "This leaderboard entry does not have a playable replay attached.",
		"missing_leaderboard_name": "The leaderboard identity is missing.",
		"steam_offline": "Steam is offline, so the replay cannot be downloaded.",
		"steam_download_start_failed": "Steam could not start the replay download.",
		"steam_download_failed": "Steam replay download failed.",
		"invalid_steam_payload": "Steam returned an invalid replay payload.",
		"cache_write_failed": "The validated replay could not be written to the local cache.",
	}
	return {
		"success": false,
		"reason": reason,
		"message": String(detail) if !String(detail).is_empty() else String(messages.get(reason, reason.replace("_", " "))),
	}


func _validation_failure(validation: Dictionary) -> Dictionary:
	var reason := String(validation.get("reason", "invalid_replay"))
	return {
		"success": false,
		"reason": reason,
		"message": "Leaderboard replay rejected: %s" % reason.replace("_", " "),
	}


func _validation_key(board_name: String, details: Dictionary) -> String:
	return "%s|%s|%d|%d|%s|%s|%d" % [
		String(details.get("replay_sha256", "")),
		board_name,
		int(details.get("ruleset_revision", -1)),
		int(details.get("replay_schema_version", -1)),
		String(details.get("track_gameplay_digest", "")),
		String(details.get("vehicle_gameplay_digest", "")),
		int(details.get("machine_setting_percent", -1)),
	]


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK:
		return ""
	if context.update(bytes) != OK:
		return ""
	return "sha256:" + context.finish().hex_encode()


func _digest_hex(digest: String) -> String:
	if !digest.begins_with("sha256:"):
		return ""
	var value := digest.trim_prefix("sha256:").to_lower()
	if value.length() != 64 or !value.is_valid_hex_number(false):
		return ""
	return value
