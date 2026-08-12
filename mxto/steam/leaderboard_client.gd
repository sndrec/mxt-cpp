class_name LeaderboardClient extends Node

signal submission_status_changed(status: Dictionary)
signal entries_received(board_name: String, request_type: String, result: Dictionary)

const CONFIG_PATH := "res://steam/leaderboard_service.json"
const QUEUE_PATH := "user://steam_leaderboard_submissions.json"
const MAX_PENDING_SUBMISSIONS := 32
const MAX_REJECTED_SUBMISSIONS := 32
const RETRY_DELAY_MIN_SECONDS := 30.0
const RETRY_DELAY_MAX_SECONDS := 600.0

var steam_service: MxtSteamService
var http_request: HTTPRequest
var retry_timer: Timer
var config: Dictionary = {}
var pending: Array = []
var rejected: Array = []
var read_requests: Dictionary = {}
var active_ticket_request_id := 0
var active_ticket_handle := 0
var active_submission: Dictionary = {}
var last_message := "Leaderboard service is not configured."

func initialize(service: MxtSteamService) -> void:
	steam_service = service
	if steam_service != null:
		steam_service.web_api_ticket_request_completed.connect(_on_ticket_completed)
		steam_service.leaderboard_request_completed.connect(_on_leaderboard_request_completed)
		steam_service.status_changed.connect(func(_status): _pump_submission_queue())
	call_deferred("_pump_submission_queue")

func _ready() -> void:
	http_request = HTTPRequest.new()
	http_request.name = "SubmissionRequest"
	http_request.timeout = 180.0
	http_request.request_completed.connect(_on_http_request_completed)
	add_child(http_request)
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
	if typeof(value) == TYPE_DICTIONARY and int(value.get("format_revision", -1)) == 1:
		config = value as Dictionary
	else:
		config = {}

func _allowed_replay_path(path: String) -> bool:
	if path.is_empty() or !FileAccess.file_exists(path):
		return false
	var replay_root := ProjectSettings.globalize_path("user://replays").simplify_path().to_lower()
	var absolute_path := ProjectSettings.globalize_path(path).simplify_path().to_lower()
	return absolute_path.begins_with(replay_root + "/") or absolute_path.begins_with(replay_root + "\\")

func _load_queue() -> void:
	pending.clear()
	rejected.clear()
	if !FileAccess.file_exists(QUEUE_PATH):
		_emit_status()
		return
	var value = JSON.parse_string(FileAccess.get_file_as_string(QUEUE_PATH))
	if typeof(value) != TYPE_DICTIONARY:
		_emit_status()
		return
	for submission_value in value.get("pending", []):
		if typeof(submission_value) == TYPE_DICTIONARY:
			var submission: Dictionary = submission_value
			if _submission_record_valid(submission) and _allowed_replay_path(String(submission.get("replay_path", ""))):
				pending.append(submission.duplicate(true))
	for submission_value in value.get("rejected", []):
		if typeof(submission_value) == TYPE_DICTIONARY:
			rejected.append((submission_value as Dictionary).duplicate(true))
	if rejected.size() > MAX_REJECTED_SUBMISSIONS:
		rejected = rejected.slice(rejected.size() - MAX_REJECTED_SUBMISSIONS)
	_emit_status()

func _save_queue() -> void:
	var file := FileAccess.open(QUEUE_PATH, FileAccess.WRITE)
	if file == null:
		last_message = "Could not persist pending leaderboard submissions."
		_emit_status()
		return
	file.store_string(JSON.stringify({
		"format_revision": 1,
		"pending": pending,
		"rejected": rejected,
	}, "  "))
	file.close()

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
		"created_unix": int(Time.get_unix_time_from_system()),
		"attempts": 0,
		"last_error": "",
	}
	if !_submission_record_valid(submission) or !_allowed_replay_path(replay_path):
		return false
	for existing_value in pending:
		var existing: Dictionary = existing_value
		if String(existing.get("replay_path", "")) == replay_path:
			return true
	if pending.size() >= MAX_PENDING_SUBMISSIONS:
		_reject_submission(pending.pop_front(), "pending_queue_full")
	pending.append(submission)
	last_message = "Time Attack replay queued for verification."
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

func request_entries(board_name: String, request_type: String) -> int:
	if steam_service == null:
		entries_received.emit(board_name, request_type, {"success": false, "message": "Steam is unavailable."})
		return 0
	var range_start := 1
	var range_end := 100
	if request_type == "around_user":
		range_start = -5
		range_end = 5
	elif request_type == "friends":
		range_start = 0
		range_end = 0
	var request_id := steam_service.request_leaderboard_entries(board_name, request_type, range_start, range_end)
	read_requests[request_id] = {"board_name": board_name, "request_type": request_type}
	return request_id

func status() -> Dictionary:
	return {
		"message": last_message,
		"pending_count": pending.size(),
		"rejected_count": rejected.size(),
		"active": !active_submission.is_empty(),
		"service_configured": !_service_url().is_empty(),
		"steam_available": steam_service != null and steam_service.is_initialized(),
		"pending": pending.duplicate(true),
		"rejected": rejected.duplicate(true),
	}

func _emit_status() -> void:
	submission_status_changed.emit(status())

func _service_url() -> String:
	var base_url := String(config.get("base_url", "")).strip_edges().trim_suffix("/")
	if base_url.begins_with("https://"):
		return base_url + "/v1/time-attack/submit"
	if base_url.begins_with("http://127.0.0.1") or base_url.begins_with("http://localhost"):
		return base_url + "/v1/time-attack/submit"
	return ""

func _schedule_retry() -> void:
	if retry_timer == null or pending.is_empty():
		return
	var attempts := int((pending[0] as Dictionary).get("attempts", 0))
	var delay := minf(RETRY_DELAY_MAX_SECONDS, RETRY_DELAY_MIN_SECONDS * pow(2.0, minf(float(attempts), 5.0)))
	retry_timer.start(delay)

func _pump_submission_queue() -> void:
	if !is_node_ready() or pending.is_empty() or active_ticket_request_id != 0 or !active_submission.is_empty():
		return
	if _service_url().is_empty():
		last_message = "Leaderboard submission endpoint is not configured; replay remains pending."
		_emit_status()
		return
	if steam_service == null or !steam_service.is_initialized():
		last_message = "Steam is offline; leaderboard replay remains pending."
		_emit_status()
		_schedule_retry()
		return
	active_submission = (pending[0] as Dictionary).duplicate(true)
	active_submission["attempts"] = int(active_submission.get("attempts", 0)) + 1
	pending[0] = active_submission.duplicate(true)
	_save_queue()
	last_message = "Requesting Steam authentication ticket..."
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
		"Content-Type: application/vnd.mxt.replay+json",
		"Authorization: SteamTicket %s" % String(result.get("ticket_hex", "")),
		"X-MXT-Ticket-Identity: %s" % String(result.get("identity", "")),
		"X-MXT-Steam-App-ID: %d" % steam_service.get_app_id(),
		"X-MXT-Board: %s" % String(active_submission.get("board_name", "")),
		"X-MXT-Claimed-Score-Milliseconds: %d" % int(active_submission.get("score_milliseconds", 0)),
	])
	var error := http_request.request_raw(_service_url(), headers, HTTPClient.METHOD_POST, replay_bytes)
	if error != OK:
		_cancel_active_ticket()
		_retry_active("Could not start leaderboard submission: %s" % error_string(error))
		return
	last_message = "Uploading replay for trusted verification..."
	_emit_status()

func _on_http_request_completed(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_cancel_active_ticket()
	var response_value = JSON.parse_string(body.get_string_from_utf8())
	var response: Dictionary = response_value if typeof(response_value) == TYPE_DICTIONARY else {}
	if result == HTTPRequest.RESULT_SUCCESS and response_code >= 200 and response_code < 300 and bool(response.get("ok", false)):
		if !pending.is_empty() and String((pending[0] as Dictionary).get("replay_path", "")) == String(active_submission.get("replay_path", "")):
			pending.pop_front()
		last_message = "Verified Time Attack score uploaded to Steam."
		active_submission.clear()
		_save_queue()
		_emit_status()
		_pump_submission_queue()
		return
	var message := String(response.get("message", response.get("error", "Leaderboard service request failed.")))
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

func _on_leaderboard_request_completed(request_id: int, result: Dictionary) -> void:
	if !read_requests.has(request_id):
		return
	var request: Dictionary = read_requests[request_id]
	read_requests.erase(request_id)
	entries_received.emit(String(request.get("board_name", "")), String(request.get("request_type", "")), result)
