class_name SteamLeaderboardSnapshot extends Node

const TimeAttackRulesClass = preload("res://leaderboards/time_attack_rules.gd")

const FLAG := "--export-steam-leaderboard-snapshot"
const OUTPUT_FLAG := "--steam-leaderboard-snapshot-output"
const DEFAULT_OUTPUT_ROOT := "user://steam_leaderboard_snapshot"
const PAGE_SIZE := 100
const MAX_GLOBAL_RANK := 10000
const MAX_REPLAY_BYTES := 16 * 1024 * 1024

var steam_service: MxtSteamService
var output_root := ""
var manifest_path := ""
var replay_root := ""
var snapshot_unix := 0
var boards: Array = []
var entries: Array = []
var failures: Array = []
var download_queue: Array[int] = []
var board_index := 0
var page_start := 1
var download_index := 0
var active_read_request_id := 0
var active_download_request_id := 0
var counts := {
	"boards": 0,
	"entries_scanned": 0,
	"replays_downloaded": 0,
	"replays_unavailable": 0,
	"replays_failed": 0,
}


static func requested(args: Array, user_args: Array) -> bool:
	return args.has(FLAG) or user_args.has(FLAG)


func initialize(service: MxtSteamService, args: Array, user_args: Array) -> void:
	steam_service = service
	output_root = ProjectSettings.globalize_path(
		_read_arg_value(args, user_args, OUTPUT_FLAG, DEFAULT_OUTPUT_ROOT)).simplify_path()
	manifest_path = output_root.path_join("snapshot.json")
	replay_root = output_root.path_join("replays")
	snapshot_unix = int(Time.get_unix_time_from_system())
	if DirAccess.make_dir_recursive_absolute(replay_root) != OK:
		_finish(false, "Could not create the Steam leaderboard snapshot directory.")
		return
	for value in TimeAttackRulesClass.manifest().get("boards", []):
		if typeof(value) == TYPE_DICTIONARY \
				and String((value as Dictionary).get("track_source", "official")) == "official":
			boards.append((value as Dictionary).duplicate(true))
	counts["boards"] = boards.size()
	if steam_service == null or !steam_service.is_initialized():
		_finish(false, "Steam is not initialized. Launch this tool through Steam and try again.")
		return
	steam_service.leaderboard_request_completed.connect(_on_leaderboard_request_completed)
	steam_service.leaderboard_replay_download_completed.connect(_on_replay_download_completed)
	print("MXT_STEAM_SNAPSHOT starting boards=", boards.size(), " output=", output_root)
	_request_page()


func _request_page() -> void:
	if board_index >= boards.size():
		_finish(true, "Steam leaderboard snapshot completed.")
		return
	var board: Dictionary = boards[board_index]
	var board_name := String(board.get("steam_name", ""))
	if board_name.is_empty():
		_record_failure({}, "manifest_board_name_missing")
		_advance_board()
		return
	var page_end := mini(page_start + PAGE_SIZE - 1, MAX_GLOBAL_RANK)
	print("MXT_STEAM_SNAPSHOT scan board=", board_name, " ranks=", page_start, "-", page_end)
	active_read_request_id = steam_service.request_leaderboard_entries(
		board_name, "global", page_start, page_end)


func _on_leaderboard_request_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_read_request_id:
		return
	active_read_request_id = 0
	var board: Dictionary = boards[board_index]
	var board_name := String(board.get("steam_name", ""))
	if !bool(result.get("success", false)):
		_record_failure({"board_name": board_name}, "leaderboard_read_failed", String(result.get("message", "")))
		_begin_downloads()
		return
	var page_entries: Array = result.get("entries", [])
	for entry_value in page_entries:
		if typeof(entry_value) == TYPE_DICTIONARY:
			_collect_entry(board, entry_value as Dictionary)
	if page_entries.size() == PAGE_SIZE and page_start + PAGE_SIZE <= MAX_GLOBAL_RANK:
		page_start += PAGE_SIZE
		_request_page()
		return
	_begin_downloads()


func _collect_entry(board: Dictionary, entry: Dictionary) -> void:
	var ugc_handle := int(entry.get("ugc_handle", 0))
	var track_content_id := String(board.get("track_content_id", ""))
	if track_content_id.is_empty():
		var track_slug := String(board.get("track_slug", ""))
		if !track_slug.is_empty():
			track_content_id = "mxt:track:official:" + track_slug
	var record := {
		"board_name": String(board.get("steam_name", "")),
		"track_content_id": track_content_id,
		"track_gameplay_digest": String(board.get("track_gameplay_digest", "")),
		"track_title": TimeAttackRulesClass.board_title(board),
		"ruleset_revision": int(board.get("ruleset_revision", TimeAttackRulesClass.RULESET_REVISION)),
		"global_rank": int(entry.get("global_rank", 0)),
		"steam_id": str(int(entry.get("steam_id", 0))),
		"persona_name": String(entry.get("persona_name", "")),
		"score_milliseconds": int(entry.get("score", 0)),
		"ugc_handle": str(ugc_handle) if ugc_handle != 0 and ugc_handle != -1 else "",
		"source_details": (entry.get("details", []) as Array).duplicate(true) \
			if typeof(entry.get("details", [])) == TYPE_ARRAY else [],
		"replay_status": "pending" if ugc_handle != 0 and ugc_handle != -1 else "unavailable",
		"replay_unavailable_reason": "" if ugc_handle != 0 and ugc_handle != -1 else "missing_replay_attachment",
		"replay_sha256": "",
		"replay_byte_length": 0,
		"replay_path": "",
	}
	entries.append(record)
	counts["entries_scanned"] = int(counts["entries_scanned"]) + 1
	if String(record.get("replay_status", "")) == "pending":
		download_queue.append(entries.size() - 1)
	else:
		counts["replays_unavailable"] = int(counts["replays_unavailable"]) + 1


func _begin_downloads() -> void:
	download_index = 0
	_download_next()


func _download_next() -> void:
	if download_index >= download_queue.size():
		_save_snapshot(false, "")
		_advance_board()
		return
	var entry_index := download_queue[download_index]
	download_index += 1
	var entry: Dictionary = entries[entry_index]
	active_download_request_id = steam_service.download_leaderboard_replay(
		int(String(entry.get("ugc_handle", "0"))), MAX_REPLAY_BYTES)
	print("MXT_STEAM_SNAPSHOT download board=", entry.get("board_name", ""),
		" rank=", entry.get("global_rank", 0), " steam_id=", entry.get("steam_id", ""))


func _on_replay_download_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_download_request_id:
		return
	active_download_request_id = 0
	var entry_index := download_queue[download_index - 1]
	var entry: Dictionary = entries[entry_index]
	if !bool(result.get("success", false)):
		_mark_replay_failed(entry_index, "replay_download_failed", String(result.get("message", "")))
		_download_next()
		return
	var expected_owner := int(String(entry.get("steam_id", "0")))
	if expected_owner <= 0 or int(result.get("owner_steam_id", 0)) != expected_owner:
		_mark_replay_failed(entry_index, "replay_owner_mismatch")
		_download_next()
		return
	var bytes_value = result.get("bytes", PackedByteArray())
	if typeof(bytes_value) != TYPE_PACKED_BYTE_ARRAY or (bytes_value as PackedByteArray).is_empty():
		_mark_replay_failed(entry_index, "invalid_replay_payload")
		_download_next()
		return
	var bytes: PackedByteArray = bytes_value
	var replay_sha256 := _sha256(bytes)
	var relative_path := "replays/%s.replay" % replay_sha256.trim_prefix("sha256:")
	var absolute_path := output_root.path_join(relative_path)
	if !_write_bytes_atomically(absolute_path, bytes):
		_mark_replay_failed(entry_index, "replay_write_failed")
		_download_next()
		return
	entry["replay_status"] = "downloaded"
	entry["replay_unavailable_reason"] = ""
	entry["replay_sha256"] = replay_sha256
	entry["replay_byte_length"] = bytes.size()
	entry["replay_path"] = relative_path
	entries[entry_index] = entry
	counts["replays_downloaded"] = int(counts["replays_downloaded"]) + 1
	_save_snapshot(false, "")
	_download_next()


func _mark_replay_failed(entry_index: int, reason: String, message := "") -> void:
	var entry: Dictionary = entries[entry_index]
	entry["replay_status"] = "failed"
	entry["replay_unavailable_reason"] = reason
	entries[entry_index] = entry
	counts["replays_failed"] = int(counts["replays_failed"]) + 1
	_record_failure(entry, reason, message)


func _advance_board() -> void:
	download_queue.clear()
	download_index = 0
	board_index += 1
	page_start = 1
	_request_page()


func _record_failure(entry: Dictionary, reason: String, message := "") -> void:
	var failure := {
		"board_name": String(entry.get("board_name", "")),
		"global_rank": int(entry.get("global_rank", 0)),
		"steam_id": String(entry.get("steam_id", "")),
		"reason": reason,
	}
	if !message.is_empty():
		failure["message"] = message
	failures.append(failure)
	push_warning("MXT_STEAM_SNAPSHOT failure board=%s rank=%d reason=%s %s" % [
		String(failure.board_name), int(failure.global_rank), reason, message])


func _finish(success: bool, message: String) -> void:
	var saved := _save_snapshot(success, message)
	print("MXT_STEAM_SNAPSHOT complete success=", success and saved,
		" entries=", counts["entries_scanned"], " downloaded=", counts["replays_downloaded"],
		" unavailable=", counts["replays_unavailable"], " failed=", counts["replays_failed"],
		" manifest=", manifest_path)
	get_tree().quit(0 if success and saved else 1)


func _save_snapshot(complete: bool, message: String) -> bool:
	return _write_json_atomically(manifest_path, {
		"format_revision": 1,
		"complete": complete,
		"message": message,
		"snapshot_unix": snapshot_unix,
		"generated_unix": int(Time.get_unix_time_from_system()),
		"steam_app_id": steam_service.get_app_id() if steam_service != null else 0,
		"counts": counts,
		"entries": entries,
		"failures": failures,
	})


func _write_json_atomically(path: String, value: Dictionary) -> bool:
	var temporary_path := path + ".tmp"
	var file := FileAccess.open(temporary_path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.flush()
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		return false
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)
	return DirAccess.rename_absolute(temporary_path, path) == OK


func _write_bytes_atomically(path: String, bytes: PackedByteArray) -> bool:
	if FileAccess.file_exists(path):
		var existing := FileAccess.get_file_as_bytes(path)
		return existing.size() == bytes.size() and _sha256(existing) == _sha256(bytes)
	var temporary_path := path + ".tmp"
	var file := FileAccess.open(temporary_path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(bytes)
	file.flush()
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		return false
	return DirAccess.rename_absolute(temporary_path, path) == OK


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK or context.update(bytes) != OK:
		return ""
	return "sha256:" + context.finish().hex_encode()


func _read_arg_value(args: Array, user_args: Array, flag: String, default_value: String) -> String:
	for source_value in [args, user_args]:
		var source: Array = source_value
		var index := source.find(flag)
		if index >= 0 and index + 1 < source.size():
			return String(source[index + 1])
	return default_value
