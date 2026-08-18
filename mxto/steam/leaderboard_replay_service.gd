extends Node

signal attachment_status_changed(status: Dictionary)
signal playback_status_changed(message: String)

const ReplayValidatorClass = preload("res://steam/leaderboard_replay_validator.gd")
const QUEUE_PATH := "user://steam_leaderboard_replay_attachments.json"
const CACHE_ROOT := "user://leaderboard_replays"
const MAX_REPLAY_BYTES := 64 * 1024 * 1024
const MAX_HISTORY := 32
const RETRY_DELAY_MIN_SECONDS := 30.0
const RETRY_DELAY_MAX_SECONDS := 600.0

var game_manager: GameManager
var steam_service: MxtSteamService
var replay_controller: ReplayController
var retry_timer: Timer
var pending: Array = []
var completed: Array = []
var failed: Array = []
var active_upload: Dictionary = {}
var active_upload_request_id := 0
var active_download: Dictionary = {}
var active_download_request_id := 0
var last_message := "No replay attachment work is pending."


func _ready() -> void:
	retry_timer = Timer.new()
	retry_timer.name = "ReplayAttachmentRetryTimer"
	retry_timer.one_shot = true
	retry_timer.timeout.connect(_pump_upload_queue)
	add_child(retry_timer)
	_load_queue()


func initialize(manager: GameManager, service: MxtSteamService, controller: ReplayController) -> void:
	game_manager = manager
	steam_service = service
	replay_controller = controller
	if steam_service != null:
		steam_service.leaderboard_replay_upload_completed.connect(_on_upload_completed)
		steam_service.leaderboard_replay_download_completed.connect(_on_download_completed)
		steam_service.status_changed.connect(func(_status): _pump_upload_queue())
	call_deferred("_pump_upload_queue")


func status() -> Dictionary:
	return {
		"message": last_message,
		"pending_count": pending.size(),
		"completed_count": completed.size(),
		"failed_count": failed.size(),
		"active": !active_upload.is_empty(),
		"pending": pending.duplicate(true),
		"completed": completed.duplicate(true),
		"failed": failed.duplicate(true),
	}


func enqueue_verified_submission(submission: Dictionary) -> bool:
	if !bool(submission.get("retained", false)):
		return false
	var digest := String(submission.get("replay_sha256", ""))
	var replay_path := String(submission.get("replay_path", ""))
	var digest_hex := _digest_hex(digest)
	if digest_hex.is_empty() or !_allowed_local_replay_path(replay_path):
		return false
	for value in pending:
		if String((value as Dictionary).get("replay_sha256", "")) == digest:
			return true
	for value in completed:
		if String((value as Dictionary).get("replay_sha256", "")) == digest:
			return true
	var record := {
		"board_name": String(submission.get("board_name", "")),
		"replay_path": replay_path,
		"replay_sha256": digest,
		"remote_filename": "mxt_leaderboard_%s.replay.json" % digest_hex,
		"created_unix": int(Time.get_unix_time_from_system()),
		"attempts": 0,
		"last_error": "",
	}
	if String(record.board_name).is_empty():
		return false
	pending.append(record)
	last_message = "Verified leaderboard replay queued for Steam attachment."
	_save_queue()
	_emit_status()
	_pump_upload_queue()
	return true


func request_watch_replay(board_name: String, entry: Dictionary) -> void:
	if !active_download.is_empty():
		playback_status_changed.emit("Another leaderboard replay is already downloading.")
		return
	var details_value = entry.get("_trusted_details", {})
	if typeof(details_value) != TYPE_DICTIONARY:
		playback_status_changed.emit("This leaderboard entry has no trusted replay metadata.")
		return
	var details: Dictionary = details_value
	var replay_digest := String(details.get("replay_sha256", ""))
	var digest_hex := _digest_hex(replay_digest)
	var ugc_handle := int(entry.get("ugc_handle", 0))
	if digest_hex.is_empty() or ugc_handle == 0:
		playback_status_changed.emit("This leaderboard entry does not have a playable replay attached.")
		return
	active_download = {
		"board_name": board_name,
		"ugc_handle": ugc_handle,
		"trusted_details": details.duplicate(true),
		"cache_path": CACHE_ROOT.path_join(digest_hex + ".replay.json"),
	}
	var cache_path := String(active_download.cache_path)
	if FileAccess.file_exists(cache_path):
		var cached_bytes := FileAccess.get_file_as_bytes(cache_path)
		var validation := _validate_download(cached_bytes, active_download)
		if bool(validation.get("valid", false)):
			_finish_download_playback(cache_path, "Playing validated cached leaderboard replay.")
			return
		DirAccess.remove_absolute(ProjectSettings.globalize_path(cache_path))
	if steam_service == null or !steam_service.is_initialized():
		active_download.clear()
		playback_status_changed.emit("Steam is offline, so the replay cannot be downloaded.")
		return
	active_download_request_id = steam_service.download_leaderboard_replay(ugc_handle, MAX_REPLAY_BYTES)
	playback_status_changed.emit("Downloading leaderboard replay from Steam…")


func _load_queue() -> void:
	pending.clear()
	completed.clear()
	failed.clear()
	if FileAccess.file_exists(QUEUE_PATH):
		var value = JSON.parse_string(FileAccess.get_file_as_string(QUEUE_PATH))
		if typeof(value) == TYPE_DICTIONARY:
			for record_value in value.get("pending", []):
				if typeof(record_value) == TYPE_DICTIONARY:
					var record: Dictionary = record_value
					if !_digest_hex(String(record.get("replay_sha256", ""))).is_empty() and _allowed_local_replay_path(String(record.get("replay_path", ""))):
						pending.append(record.duplicate(true))
			for record_value in value.get("completed", []):
				if typeof(record_value) == TYPE_DICTIONARY:
					completed.append((record_value as Dictionary).duplicate(true))
			for record_value in value.get("failed", []):
				if typeof(record_value) == TYPE_DICTIONARY:
					failed.append((record_value as Dictionary).duplicate(true))
	_trim_history()
	_emit_status()


func _save_queue() -> void:
	var file := FileAccess.open(QUEUE_PATH, FileAccess.WRITE)
	if file == null:
		last_message = "Could not persist leaderboard replay attachment work."
		_emit_status()
		return
	file.store_string(JSON.stringify({
		"format_revision": 1,
		"pending": pending,
		"completed": completed,
		"failed": failed,
	}, "  "))
	file.close()


func _emit_status() -> void:
	attachment_status_changed.emit(status())


func _pump_upload_queue() -> void:
	if !is_node_ready() or pending.is_empty() or !active_upload.is_empty():
		return
	if steam_service == null or !steam_service.is_initialized():
		last_message = "Steam is offline; replay attachment remains pending."
		_emit_status()
		_schedule_retry()
		return
	active_upload = (pending[0] as Dictionary).duplicate(true)
	active_upload["attempts"] = int(active_upload.get("attempts", 0)) + 1
	pending[0] = active_upload.duplicate(true)
	var replay_path := String(active_upload.get("replay_path", ""))
	var bytes := FileAccess.get_file_as_bytes(replay_path) if _allowed_local_replay_path(replay_path) else PackedByteArray()
	if bytes.is_empty():
		_fail_active_upload("The verified replay file is missing or empty.")
		return
	var actual_digest := _sha256(bytes)
	if actual_digest != String(active_upload.get("replay_sha256", "")):
		_fail_active_upload("The local replay changed after trusted verification.")
		return
	active_upload_request_id = steam_service.upload_leaderboard_replay(
		String(active_upload.get("board_name", "")),
		String(active_upload.get("remote_filename", "")),
		String(active_upload.get("replay_sha256", "")),
		bytes)
	last_message = "Attaching the verified replay to the retained Steam score…"
	_save_queue()
	_emit_status()


func _on_upload_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_upload_request_id:
		return
	active_upload_request_id = 0
	if bool(result.get("success", false)):
		var record := active_upload.duplicate(true)
		record["attached_unix"] = int(Time.get_unix_time_from_system())
		record["ugc_handle"] = int(result.get("ugc_handle", 0))
		completed.append(record)
		if !pending.is_empty() and String((pending[0] as Dictionary).get("replay_sha256", "")) == String(active_upload.get("replay_sha256", "")):
			pending.pop_front()
		last_message = "Verified replay attached to the retained Steam score."
		active_upload.clear()
		_trim_history()
		_save_queue()
		_emit_status()
		_pump_upload_queue()
		return
	var message := String(result.get("message", "Steam replay attachment failed."))
	if !bool(result.get("retryable", true)):
		_fail_active_upload(message)
		return
	if !pending.is_empty():
		(pending[0] as Dictionary)["last_error"] = message
	last_message = "%s Replay attachment remains pending." % message
	active_upload.clear()
	_save_queue()
	_emit_status()
	_schedule_retry()


func _fail_active_upload(message: String) -> void:
	var record := active_upload.duplicate(true)
	record["failed_unix"] = int(Time.get_unix_time_from_system())
	record["last_error"] = message
	failed.append(record)
	if !pending.is_empty():
		pending.pop_front()
	last_message = message
	active_upload.clear()
	active_upload_request_id = 0
	_trim_history()
	_save_queue()
	_emit_status()
	_pump_upload_queue()


func _schedule_retry() -> void:
	if retry_timer == null or pending.is_empty():
		return
	var attempts := int((pending[0] as Dictionary).get("attempts", 0))
	var delay := minf(RETRY_DELAY_MAX_SECONDS, RETRY_DELAY_MIN_SECONDS * pow(2.0, minf(float(attempts), 5.0)))
	retry_timer.start(delay)


func _on_download_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_download_request_id:
		return
	active_download_request_id = 0
	if !bool(result.get("success", false)):
		active_download.clear()
		playback_status_changed.emit(String(result.get("message", "Steam replay download failed.")))
		return
	var bytes_value = result.get("bytes", PackedByteArray())
	if typeof(bytes_value) != TYPE_PACKED_BYTE_ARRAY:
		active_download.clear()
		playback_status_changed.emit("Steam returned an invalid replay payload.")
		return
	var bytes: PackedByteArray = bytes_value
	var validation := _validate_download(bytes, active_download)
	if !bool(validation.get("valid", false)):
		active_download.clear()
		playback_status_changed.emit("Leaderboard replay rejected: %s" % String(validation.get("reason", "invalid replay")).replace("_", " "))
		return
	var cache_path := String(active_download.get("cache_path", ""))
	if !_write_cache_atomically(cache_path, bytes):
		active_download.clear()
		playback_status_changed.emit("The validated replay could not be written to the local cache.")
		return
	_finish_download_playback(cache_path, "Leaderboard replay validated. Starting playback…")


func _validate_download(bytes: PackedByteArray, request: Dictionary) -> Dictionary:
	if bytes.is_empty() or bytes.size() > MAX_REPLAY_BYTES:
		return {"valid": false, "reason": "invalid_replay_size"}
	var details: Dictionary = request.get("trusted_details", {})
	if _sha256(bytes) != String(details.get("replay_sha256", "")):
		return {"valid": false, "reason": "replay_digest_mismatch"}
	var replay_value = JSON.parse_string(bytes.get_string_from_utf8())
	if typeof(replay_value) != TYPE_DICTIONARY:
		return {"valid": false, "reason": "invalid_replay_json"}
	var validation: Dictionary = ReplayValidatorClass.validate(game_manager, replay_value as Dictionary)
	if !bool(validation.get("valid", false)):
		return validation
	if String(validation.get("board_name", "")) != String(request.get("board_name", "")) \
			or int(validation.get("ruleset_revision", -1)) != int(details.get("ruleset_revision", -2)) \
			or int(validation.get("replay_schema_version", -1)) != int(details.get("replay_schema_version", -2)) \
			or String(validation.get("track_gameplay_digest", "")) != String(details.get("track_gameplay_digest", "")) \
			or String(validation.get("vehicle_gameplay_digest", "")) != String(details.get("vehicle_gameplay_digest", "")):
		return {"valid": false, "reason": "trusted_metadata_mismatch"}
	return {"valid": true, "reason": ""}


func _write_cache_atomically(path: String, bytes: PackedByteArray) -> bool:
	if DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(CACHE_ROOT)) != OK:
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


func _finish_download_playback(path: String, message: String) -> void:
	active_download.clear()
	playback_status_changed.emit(message)
	if replay_controller != null:
		replay_controller.call_deferred("play_replay_file", path)


func _allowed_local_replay_path(path: String) -> bool:
	if path.is_empty() or !FileAccess.file_exists(path):
		return false
	var replay_root := ProjectSettings.globalize_path("user://replays").simplify_path().to_lower()
	var absolute_path := ProjectSettings.globalize_path(path).simplify_path().to_lower()
	return absolute_path.begins_with(replay_root + "/") or absolute_path.begins_with(replay_root + "\\")


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


func _trim_history() -> void:
	if completed.size() > MAX_HISTORY:
		completed = completed.slice(completed.size() - MAX_HISTORY)
	if failed.size() > MAX_HISTORY:
		failed = failed.slice(failed.size() - MAX_HISTORY)
