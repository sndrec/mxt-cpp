class_name LoadTransitionProfiler
extends RefCounted

const LOG_DIRECTORY := "user://logs"
const LOG_PREFIX := "MXT_LOAD"

static var _next_token := 1
static var _active: Dictionary = {}
static var _pending_records: Array = []
static var _log: FileAccess
static var _log_path := ""
static var _session_started := false


static func begin_transition(category: String, name: String, fields: Dictionary = {}) -> int:
	var now_usec := Time.get_ticks_usec()
	var token := _next_token
	_next_token += 1
	_active[token] = {
		"category": category,
		"name": name,
		"start_usec": now_usec,
		"last_usec": now_usec,
		"fields": fields.duplicate(true),
		"phases": [],
	}
	return token


static func checkpoint(token: int, phase: String, fields: Dictionary = {}) -> void:
	if !_active.has(token):
		return
	var now_usec := Time.get_ticks_usec()
	var transition: Dictionary = _active[token]
	var phase_record := {
		"name": phase,
		"duration_usec": now_usec - int(transition["last_usec"]),
		"elapsed_usec": now_usec - int(transition["start_usec"]),
	}
	for key in fields:
		phase_record[key] = fields[key]
	(transition["phases"] as Array).append(phase_record)
	transition["last_usec"] = now_usec
	_active[token] = transition


static func end_transition(token: int, fields: Dictionary = {}) -> int:
	if !_active.has(token):
		return 0
	var end_usec := Time.get_ticks_usec()
	var transition: Dictionary = _active[token]
	_active.erase(token)
	var payload: Dictionary = transition["fields"]
	for key in fields:
		payload[key] = fields[key]
	payload["event"] = "transition"
	payload["category"] = String(transition["category"])
	payload["name"] = String(transition["name"])
	payload["token"] = token
	payload["duration_usec"] = end_usec - int(transition["start_usec"])
	payload["phases"] = transition["phases"]
	_queue_or_emit(payload)
	return int(payload["duration_usec"])


static func instant(category: String, name: String, fields: Dictionary = {}) -> void:
	var payload := fields.duplicate(true)
	payload["event"] = "instant"
	payload["category"] = category
	payload["name"] = name
	_queue_or_emit(payload)


static func get_log_path() -> String:
	_ensure_log()
	return ProjectSettings.globalize_path(_log_path) if !_log_path.is_empty() else ""


static func _queue_or_emit(payload: Dictionary) -> void:
	if !_active.is_empty():
		_pending_records.append(payload)
		return
	for pending_value in _pending_records:
		_emit(pending_value as Dictionary)
	_pending_records.clear()
	_emit(payload)


static func _ensure_log() -> void:
	if _log != null:
		return
	var absolute_directory := ProjectSettings.globalize_path(LOG_DIRECTORY)
	var directory_error := DirAccess.make_dir_recursive_absolute(absolute_directory)
	if directory_error != OK:
		push_warning("Could not create load-performance log directory: %s" % error_string(directory_error))
		return
	_log_path = "%s/load-performance-%d-%d.jsonl" % [
		LOG_DIRECTORY,
		int(Time.get_unix_time_from_system()),
		Time.get_ticks_msec(),
	]
	_log = FileAccess.open(_log_path, FileAccess.WRITE)
	if _log == null:
		push_warning("Could not open load-performance log: %s" % error_string(FileAccess.get_open_error()))
		_log_path = ""
		return
	print("%s log: %s" % [LOG_PREFIX, ProjectSettings.globalize_path(_log_path)])


static func _emit(payload: Dictionary) -> void:
	_ensure_log()
	var record := payload.duplicate(true)
	record["ticks_msec"] = Time.get_ticks_msec()
	record["unix_time"] = Time.get_unix_time_from_system()
	record["utc"] = Time.get_datetime_string_from_system(true)
	var line := JSON.stringify(record)
	print("%s %s" % [LOG_PREFIX, line])
	if _log == null:
		return
	if !_session_started:
		_session_started = true
		_log.store_line(JSON.stringify({
			"event": "session_start",
			"command_line": OS.get_cmdline_args(),
			"engine_version": Engine.get_version_info().get("string", ""),
			"os": OS.get_name(),
			"ticks_msec": Time.get_ticks_msec(),
			"unix_time": Time.get_unix_time_from_system(),
			"utc": Time.get_datetime_string_from_system(true),
		}))
	_log.store_line(line)
	_log.flush()
