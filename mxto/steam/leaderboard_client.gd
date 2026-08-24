class_name LeaderboardClient extends Node

signal submission_status_changed(status: Dictionary)
signal entries_received(board_name: String, request_type: String, result: Dictionary)
signal categories_received(board_name: String, result: Dictionary)
signal player_bests_received(board_name: String, result: Dictionary)
signal submission_completed(result: Dictionary)

const CONFIG_PATH := "res://steam/leaderboard_service.json"
const QUEUE_PATH := "user://leaderboard_submissions.json"
const LEGACY_QUEUE_PATH := "user://steam_leaderboard_submissions.json"
const SUBMISSION_REPLAY_ROOT := "user://leaderboard_submission_replays"
const MAX_REJECTED_SUBMISSIONS := 32
const MAX_COMPLETED_SUBMISSIONS := 32
const RETRY_DELAY_MIN_SECONDS := 30.0
const RETRY_DELAY_MAX_SECONDS := 600.0

var steam_service: MxtSteamService
var submission_request: HTTPRequest
var retry_timer: Timer
var config: Dictionary = {}
var pending: Array = []
var rejected: Array = []
var completed: Array = []
var active_ticket_request_id := 0
var active_ticket_handle := 0
var active_submission: Dictionary = {}
var next_read_request_id := 1
var read_requests: Dictionary = {}
var last_message := "Leaderboard service is not configured."


func initialize(service: MxtSteamService) -> void:
	steam_service = service
	if steam_service != null:
		steam_service.web_api_ticket_request_completed.connect(_on_ticket_completed)
		steam_service.status_changed.connect(func(_status): _pump_submission_queue())
	call_deferred("_pump_submission_queue")


func _ready() -> void:
	submission_request = HTTPRequest.new()
	submission_request.name = "SubmissionRequest"
	submission_request.timeout = 180.0
	submission_request.request_completed.connect(_on_submission_request_completed)
	add_child(submission_request)
	retry_timer = Timer.new()
	retry_timer.name = "RetryTimer"
	retry_timer.one_shot = true
	retry_timer.timeout.connect(_pump_submission_queue)
	add_child(retry_timer)
	_load_config()
	_load_queue()


func _exit_tree() -> void:
	_cancel_active_ticket()


func _load_config() -> void:
	var value = JSON.parse_string(FileAccess.get_file_as_string(CONFIG_PATH))
	config = value as Dictionary if typeof(value) == TYPE_DICTIONARY and int(value.get("format_revision", -1)) == 2 else {}
	var submission_override := OS.get_environment("MXT_LEADERBOARD_SUBMISSION_URL").strip_edges()
	if !submission_override.is_empty():
		config["submission_base_url"] = submission_override
	var api_override := OS.get_environment("MXT_LEADERBOARD_API_URL").strip_edges()
	if !api_override.is_empty():
		config["api_base_url"] = api_override


func api_base_url() -> String:
	return _validated_base_url(String(config.get("api_base_url", "")))


func _submission_url() -> String:
	var base_url := _validated_base_url(String(config.get("submission_base_url", "")))
	return base_url + "/v1/time-attack/submit" if !base_url.is_empty() else ""


func _validated_base_url(value: String) -> String:
	var base_url := value.strip_edges().trim_suffix("/")
	if base_url.begins_with("https://"):
		return base_url
	if base_url.begins_with("http://127.0.0.1") or base_url.begins_with("http://localhost"):
		return base_url
	return ""


func _allowed_replay_path(path: String) -> bool:
	if path.is_empty() or !FileAccess.file_exists(path):
		return false
	var replay_root := ProjectSettings.globalize_path("user://replays").simplify_path().to_lower()
	var submission_root := ProjectSettings.globalize_path(SUBMISSION_REPLAY_ROOT).simplify_path().to_lower()
	var absolute_path := ProjectSettings.globalize_path(path).simplify_path().to_lower()
	return absolute_path.begins_with(replay_root + "/") \
		or absolute_path.begins_with(replay_root + "\\") \
		or absolute_path.begins_with(submission_root + "/") \
		or absolute_path.begins_with(submission_root + "\\")


func _load_queue() -> void:
	pending.clear()
	rejected.clear()
	completed.clear()
	var queue_paths: Array[String] = []
	if FileAccess.file_exists(QUEUE_PATH):
		queue_paths.append(QUEUE_PATH)
	if FileAccess.file_exists(LEGACY_QUEUE_PATH):
		queue_paths.append(LEGACY_QUEUE_PATH)
	if queue_paths.is_empty():
		_emit_status()
		return
	for queue_path in queue_paths:
		var value = JSON.parse_string(FileAccess.get_file_as_string(queue_path))
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var queue: Dictionary = value
		for submission_value in queue.get("pending", []):
			if typeof(submission_value) != TYPE_DICTIONARY:
				continue
			var submission: Dictionary = submission_value
			if _submission_record_valid(submission) \
					and _allowed_replay_path(String(submission.get("replay_path", ""))) \
					and !_queue_contains_replay(pending, String(submission.get("replay_path", ""))):
				pending.append(submission.duplicate(true))
		for submission_value in queue.get("rejected", []):
			if typeof(submission_value) == TYPE_DICTIONARY \
					and !_queue_contains_replay(rejected, String((submission_value as Dictionary).get("replay_path", ""))):
				rejected.append((submission_value as Dictionary).duplicate(true))
		for submission_value in queue.get("completed", []):
			if typeof(submission_value) == TYPE_DICTIONARY \
					and !_queue_contains_replay(completed, String((submission_value as Dictionary).get("replay_path", ""))):
				completed.append((submission_value as Dictionary).duplicate(true))
	if rejected.size() > MAX_REJECTED_SUBMISSIONS:
		rejected = rejected.slice(rejected.size() - MAX_REJECTED_SUBMISSIONS)
	if completed.size() > MAX_COMPLETED_SUBMISSIONS:
		completed = completed.slice(completed.size() - MAX_COMPLETED_SUBMISSIONS)
	if FileAccess.file_exists(LEGACY_QUEUE_PATH):
		_save_queue()
	_emit_status()


func _queue_contains_replay(queue: Array, replay_path: String) -> bool:
	if replay_path.is_empty():
		return false
	for submission_value in queue:
		if typeof(submission_value) == TYPE_DICTIONARY \
				and String((submission_value as Dictionary).get("replay_path", "")) == replay_path:
			return true
	return false


func _save_queue() -> void:
	var file := FileAccess.open(QUEUE_PATH, FileAccess.WRITE)
	if file == null:
		last_message = "Could not persist pending leaderboard submissions."
		_emit_status()
		return
	file.store_string(JSON.stringify({
		"format_revision": 2,
		"pending": pending,
		"rejected": rejected,
		"completed": completed,
	}, "  "))
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		last_message = "Could not persist pending leaderboard submissions."
		_emit_status()
		return
	if FileAccess.file_exists(LEGACY_QUEUE_PATH):
		var remove_error := DirAccess.remove_absolute(ProjectSettings.globalize_path(LEGACY_QUEUE_PATH))
		if remove_error != OK:
			push_warning("Could not remove migrated legacy leaderboard queue: %s" % error_string(remove_error))


func _submission_record_valid(submission: Dictionary) -> bool:
	return !String(submission.get("board_name", "")).is_empty() \
		and int(submission.get("score_milliseconds", 0)) > 0 \
		and !String(submission.get("track_gameplay_digest", "")).is_empty() \
		and !String(submission.get("vehicle_gameplay_digest", "")).is_empty() \
		and int(submission.get("ruleset_revision", 0)) > 0


func enqueue_submission(eligibility: Dictionary) -> bool:
	var board_value = eligibility.get("board", {})
	if typeof(board_value) != TYPE_DICTIONARY:
		return false
	var replay_path := String(eligibility.get("replay_path", ""))
	var submission := {
		"board_name": String(board_value.get("steam_name", "")),
		"score_milliseconds": int(eligibility.get("score_milliseconds", 0)),
		"track_gameplay_digest": String(eligibility.get("track_gameplay_digest", "")),
		"vehicle_gameplay_digest": String(eligibility.get("vehicle_gameplay_digest", "")),
		"ruleset_revision": int(eligibility.get("ruleset_revision", 0)),
		"replay_path": replay_path,
		"track_title": String(eligibility.get("track_title", "Track")),
		"vehicle_content_id": String(eligibility.get("vehicle_content_id", "")),
		"created_unix": int(Time.get_unix_time_from_system()),
		"attempts": 0,
		"last_error": "",
		"next_retry_unix": 0,
	}
	if !_submission_record_valid(submission) or !_allowed_replay_path(replay_path):
		return false
	for existing_value in pending:
		var existing: Dictionary = existing_value
		if String(existing.get("replay_path", "")) == replay_path:
			return true
	pending.append(submission)
	last_message = "Ranked replay queued for verification and archival."
	_save_queue()
	_emit_status()
	_pump_submission_queue()
	return true


func retry_pending_now() -> void:
	if retry_timer != null:
		retry_timer.stop()
	_pump_submission_queue()


func clear_rejected() -> void:
	rejected.clear()
	_save_queue()
	_emit_status()


func request_entries(board_name: String, request_type: String, vehicle_digest := "") -> int:
	var request_id := next_read_request_id
	next_read_request_id += 1
	if request_type == "friends":
		call_deferred("_emit_read_failure", request_id, board_name, request_type, "Friends filtering is not available yet.")
		return request_id
	var base_url := api_base_url()
	if base_url.is_empty():
		call_deferred("_emit_read_failure", request_id, board_name, request_type, "Leaderboard API is not configured.")
		return request_id
	var url := "%s/v1/leaderboards/%s?scope=%s" % [base_url, board_name.uri_encode(), request_type.uri_encode()]
	if !vehicle_digest.is_empty():
		url += "&vehicle_digest=%s" % vehicle_digest.uri_encode()
	if request_type == "around_user":
		if steam_service == null or !steam_service.is_initialized():
			call_deferred("_emit_read_failure", request_id, board_name, request_type, "Steam identity is unavailable.")
			return request_id
		url += "&steam_id=%s" % str(steam_service.get_steam_id()).uri_encode()
	elif request_type == "global":
		url += "&limit=100"
	else:
		call_deferred("_emit_read_failure", request_id, board_name, request_type, "Unsupported leaderboard view.")
		return request_id
	var request := HTTPRequest.new()
	request.name = "LeaderboardRead%d" % request_id
	request.timeout = 20.0
	request.request_completed.connect(_on_read_request_completed.bind(request_id))
	add_child(request)
	read_requests[request_id] = {
		"kind": "entries",
		"board_name": board_name,
		"request_type": request_type,
		"vehicle_digest": vehicle_digest,
		"node": request,
	}
	var error := request.request(url, PackedStringArray(["Accept: application/json"]), HTTPClient.METHOD_GET)
	if error != OK:
		request.queue_free()
		read_requests.erase(request_id)
		call_deferred("_emit_read_failure", request_id, board_name, request_type, "Could not start leaderboard request: %s" % error_string(error))
	return request_id


func request_categories(board_name: String) -> int:
	var base_url := api_base_url()
	if base_url.is_empty():
		call_deferred("_emit_categories_failure", board_name, "Leaderboard API is not configured.")
		return 0
	return _start_auxiliary_read(
		"%s/v1/boards/%s/categories" % [base_url, board_name.uri_encode()],
		{"kind": "categories", "board_name": board_name})


func request_player_bests(board_name: String) -> int:
	if steam_service == null or !steam_service.is_initialized():
		call_deferred("_emit_player_bests_failure", board_name, "Steam identity is unavailable.")
		return 0
	var base_url := api_base_url()
	if base_url.is_empty():
		call_deferred("_emit_player_bests_failure", board_name, "Leaderboard API is not configured.")
		return 0
	return _start_auxiliary_read(
		"%s/v1/boards/%s/players/%s/bests" % [
			base_url,
			board_name.uri_encode(),
			str(steam_service.get_steam_id()).uri_encode(),
		],
		{"kind": "player_bests", "board_name": board_name})


func _start_auxiliary_read(url: String, context: Dictionary) -> int:
	var request_id := next_read_request_id
	next_read_request_id += 1
	var request := HTTPRequest.new()
	request.name = "LeaderboardRead%d" % request_id
	request.timeout = 20.0
	request.request_completed.connect(_on_read_request_completed.bind(request_id))
	add_child(request)
	context["node"] = request
	read_requests[request_id] = context
	var error := request.request(url, PackedStringArray(["Accept: application/json"]), HTTPClient.METHOD_GET)
	if error == OK:
		return request_id
	request.queue_free()
	read_requests.erase(request_id)
	var message := "Could not start leaderboard request: %s" % error_string(error)
	if String(context.get("kind", "")) == "categories":
		call_deferred("_emit_categories_failure", String(context.get("board_name", "")), message)
	else:
		call_deferred("_emit_player_bests_failure", String(context.get("board_name", "")), message)
	return 0


func _emit_read_failure(_request_id: int, board_name: String, request_type: String, message: String) -> void:
	entries_received.emit(board_name, request_type, {"ok": false, "message": message})


func _emit_categories_failure(board_name: String, message: String) -> void:
	categories_received.emit(board_name, {"ok": false, "message": message})


func _emit_player_bests_failure(board_name: String, message: String) -> void:
	player_bests_received.emit(board_name, {"ok": false, "message": message})


func _on_read_request_completed(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray, request_id: int) -> void:
	var context_value = read_requests.get(request_id, {})
	if typeof(context_value) != TYPE_DICTIONARY:
		return
	var context: Dictionary = context_value
	read_requests.erase(request_id)
	var node := context.get("node") as HTTPRequest
	if node != null:
		node.queue_free()
	var parsed_value = JSON.parse_string(body.get_string_from_utf8())
	var parsed: Dictionary = parsed_value if typeof(parsed_value) == TYPE_DICTIONARY else {}
	if result != HTTPRequest.RESULT_SUCCESS or response_code < 200 or response_code >= 300 or !bool(parsed.get("ok", false)):
		var message := String(parsed.get("message", parsed.get("error", "Leaderboard request failed.")))
		parsed = {"ok": false, "message": message}
	match String(context.get("kind", "entries")):
		"categories":
			categories_received.emit(String(context.get("board_name", "")), parsed)
		"player_bests":
			player_bests_received.emit(String(context.get("board_name", "")), parsed)
		_:
			parsed["requested_vehicle_gameplay_digest"] = String(context.get("vehicle_digest", ""))
			entries_received.emit(String(context.get("board_name", "")), String(context.get("request_type", "")), parsed)


func status() -> Dictionary:
	return {
		"message": last_message,
		"pending_count": pending.size(),
		"rejected_count": rejected.size(),
		"completed_count": completed.size(),
		"active": !active_submission.is_empty(),
		"service_configured": !_submission_url().is_empty() and !api_base_url().is_empty(),
		"steam_available": steam_service != null and steam_service.is_initialized(),
		"pending": pending.duplicate(true),
		"rejected": rejected.duplicate(true),
		"completed": completed.duplicate(true),
		"next_retry_seconds": retry_timer.time_left if retry_timer != null and !retry_timer.is_stopped() else 0.0,
	}


func _emit_status() -> void:
	submission_status_changed.emit(status())


func _schedule_retry() -> void:
	if retry_timer == null or pending.is_empty():
		return
	var attempts := int((pending[0] as Dictionary).get("attempts", 0))
	var delay := minf(RETRY_DELAY_MAX_SECONDS, RETRY_DELAY_MIN_SECONDS * pow(2.0, minf(float(attempts), 5.0)))
	(pending[0] as Dictionary)["next_retry_unix"] = int(Time.get_unix_time_from_system() + delay)
	_save_queue()
	retry_timer.start(delay)


func _pump_submission_queue() -> void:
	if !is_node_ready() or pending.is_empty() or active_ticket_request_id != 0 or !active_submission.is_empty():
		return
	if _submission_url().is_empty():
		last_message = "Leaderboard verifier endpoint is not configured; replay remains pending."
		_emit_status()
		return
	if steam_service == null or !steam_service.is_initialized():
		last_message = "Steam is offline; ranked replay remains pending."
		_emit_status()
		_schedule_retry()
		return
	active_submission = (pending[0] as Dictionary).duplicate(true)
	active_submission["attempts"] = int(active_submission.get("attempts", 0)) + 1
	active_submission["next_retry_unix"] = 0
	pending[0] = active_submission.duplicate(true)
	_save_queue()
	last_message = "Requesting Steam authentication ticket…"
	_emit_status()
	active_ticket_request_id = steam_service.request_web_api_auth_ticket(String(config.get("ticket_identity", "mxt-leaderboard-v1")))


func _on_ticket_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_ticket_request_id:
		return
	active_ticket_request_id = 0
	if !bool(result.get("success", false)):
		_retry_active(String(result.get("message", "Steam ticket request failed.")))
		return
	active_ticket_handle = int(result.get("ticket_handle", 0))
	var replay_path := String(active_submission.get("replay_path", ""))
	if !_allowed_replay_path(replay_path):
		_reject_active("replay_file_missing")
		return
	var replay_bytes := FileAccess.get_file_as_bytes(replay_path)
	if replay_bytes.is_empty():
		_reject_active("replay_file_empty")
		return
	var headers := PackedStringArray([
		"Content-Type: application/vnd.mxt.replay",
		"Authorization: SteamTicket %s" % String(result.get("ticket_hex", "")),
		"X-MXT-Ticket-Identity: %s" % String(result.get("identity", "")),
		"X-MXT-Steam-App-ID: %d" % steam_service.get_app_id(),
		"X-MXT-Board: %s" % String(active_submission.get("board_name", "")),
		"X-MXT-Claimed-Score-Milliseconds: %d" % int(active_submission.get("score_milliseconds", 0)),
	])
	var error := submission_request.request_raw(_submission_url(), headers, HTTPClient.METHOD_POST, replay_bytes)
	if error != OK:
		_cancel_active_ticket()
		_retry_active("Could not start leaderboard submission: %s" % error_string(error))
		return
	last_message = "Uploading ranked replay for trusted verification…"
	_emit_status()


func _response_header(headers: PackedStringArray, name: String) -> String:
	var prefix := name.to_lower() + ":"
	for header in headers:
		var text := String(header)
		if text.to_lower().begins_with(prefix):
			return text.substr(prefix.length()).strip_edges()
	return ""


func _submission_failure_message(result: int, response_code: int, headers: PackedStringArray, response: Dictionary) -> String:
	var request_id := String(response.get("request_id", _response_header(headers, "X-MXT-Request-ID")))
	var message := String(response.get("message", "")).strip_edges()
	if message.is_empty():
		message = String(response.get("error", "")).replace("_", " ").strip_edges()
	if message.is_empty() and response_code > 0:
		message = "Leaderboard service returned HTTP %d." % response_code
	if message.is_empty():
		message = "Leaderboard service request failed (transport result %d)." % result
	if !request_id.is_empty() and !message.contains(request_id):
		message = "%s Reference %s." % [message.trim_suffix("."), request_id]
	return message


func _on_submission_request_completed(result: int, response_code: int, headers: PackedStringArray, body: PackedByteArray) -> void:
	_cancel_active_ticket()
	var response_value = JSON.parse_string(body.get_string_from_utf8())
	var response: Dictionary = response_value if typeof(response_value) == TYPE_DICTIONARY else {}
	if result == HTTPRequest.RESULT_SUCCESS and response_code >= 200 and response_code < 300 \
			and bool(response.get("ok", false)) and bool(response.get("archived", false)):
		var completed_submission := active_submission.duplicate(true)
		completed_submission["accepted_unix"] = int(Time.get_unix_time_from_system())
		completed_submission["archived"] = true
		completed_submission["run_id"] = String(response.get("run_id", ""))
		completed_submission["run_created"] = bool(response.get("run_created", false))
		completed_submission["vehicle_best_changed"] = bool(response.get("vehicle_best_changed", false))
		completed_submission["is_vehicle_best"] = bool(response.get("is_vehicle_best", false))
		completed_submission["outcome"] = "vehicle_best" if bool(response.get("is_vehicle_best", false)) else "archived_attempt"
		completed_submission["replay_sha256"] = String(response.get("replay_sha256", ""))
		completed_submission["global_rank"] = int(response.get("global_rank", 0))
		completed_submission["personal_best_milliseconds"] = int(response.get("personal_best_milliseconds", 0))
		completed.append(completed_submission)
		if completed.size() > MAX_COMPLETED_SUBMISSIONS:
			completed.pop_front()
		if !pending.is_empty() and String((pending[0] as Dictionary).get("replay_path", "")) == String(active_submission.get("replay_path", "")):
			pending.pop_front()
		last_message = "Ranked replay archived as your best." if bool(response.get("is_vehicle_best", false)) else "Ranked replay archived; your best is faster."
		active_submission.clear()
		_save_queue()
		_emit_status()
		submission_completed.emit(completed_submission.duplicate(true))
		_pump_submission_queue()
		return
	var message := _submission_failure_message(result, response_code, headers, response)
	push_warning("MXT_LEADERBOARD submission failed result=%d http=%d message=%s" % [result, response_code, message])
	if response_code in [400, 403, 413, 415, 422]:
		_reject_active(message)
	else:
		_retry_active(message)


func _cancel_active_ticket() -> void:
	if active_ticket_handle != 0 and steam_service != null:
		steam_service.cancel_web_api_auth_ticket(active_ticket_handle)
	active_ticket_handle = 0


func _retry_active(message: String) -> void:
	_cancel_active_ticket()
	if !pending.is_empty():
		(pending[0] as Dictionary)["last_error"] = message
		(pending[0] as Dictionary)["next_retry_unix"] = 0
	last_message = "%s Replay remains pending." % message
	active_submission.clear()
	_save_queue()
	_emit_status()
	_schedule_retry()


func _reject_submission(submission_value, reason: String) -> void:
	if typeof(submission_value) != TYPE_DICTIONARY:
		return
	var submission: Dictionary = (submission_value as Dictionary).duplicate(true)
	submission["rejected_reason"] = reason
	submission["rejected_unix"] = int(Time.get_unix_time_from_system())
	rejected.append(submission)
	if rejected.size() > MAX_REJECTED_SUBMISSIONS:
		rejected.pop_front()


func _reject_active(reason: String) -> void:
	_cancel_active_ticket()
	if !pending.is_empty():
		_reject_submission(pending.pop_front(), reason)
	last_message = "Leaderboard replay rejected: %s" % reason.replace("_", " ")
	active_submission.clear()
	_save_queue()
	_emit_status()
	_pump_submission_queue()
