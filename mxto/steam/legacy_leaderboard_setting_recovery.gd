class_name LegacyLeaderboardSettingRecovery extends Node

const LeaderboardDetailsClass = preload("res://steam/leaderboard_details.gd")
const ReplayValidatorClass = preload("res://steam/leaderboard_replay_validator.gd")
const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")

const FLAG := "--recover-legacy-leaderboard-settings"
const AUDIT_FLAG := "--audit-legacy-leaderboard-settings"
const AUDIT_REPLAY_SHA_FLAG := "--legacy-leaderboard-audit-replay-sha"
const RESTORE_UGC_FLAG := "--restore-legacy-leaderboard-ugc"
const RESTORE_REPLAY_SHA_FLAG := "--legacy-leaderboard-restore-replay-sha"
const OUTPUT_FLAG := "--legacy-leaderboard-settings-output"
const REPORT_FLAG := "--legacy-leaderboard-settings-report"
const DEFAULT_OUTPUT_PATH := "user://legacy_leaderboard_machine_settings.generated.json"
const DEFAULT_REPORT_PATH := "user://legacy_leaderboard_machine_settings.report.json"
const PAGE_SIZE := 100
const MAX_GLOBAL_RANK := 10000
const MAX_REPLAY_BYTES := 64 * 1024 * 1024

var game_manager: GameManager
var steam_service: MxtSteamService
var output_path := ""
var report_path := ""
var audit_mode := false
var audit_replay_sha256 := ""
var restore_ugc_mode := false
var restore_replay_sha256 := ""
var restore_request_id := 0
var boards: Array = []
var board_index := 0
var page_start := 1
var active_read_request_id := 0
var active_download_request_id := 0
var download_queue: Array = []
var download_index := 0
var active_entry: Dictionary = {}
var recovered_entries: Dictionary = {}
var failures: Array = []
var audit_results: Dictionary = {}
var counts := {
	"boards": 0,
	"entries_scanned": 0,
	"current_entries": 0,
	"already_recovered": 0,
	"legacy_candidates": 0,
	"recovered": 0,
	"failed": 0,
}


static func requested(args: Array, user_args: Array) -> bool:
	return args.has(FLAG) or user_args.has(FLAG) or args.has(AUDIT_FLAG) or user_args.has(AUDIT_FLAG) \
		or args.has(RESTORE_UGC_FLAG) or user_args.has(RESTORE_UGC_FLAG)


func initialize(manager: GameManager, service: MxtSteamService, args: Array, user_args: Array) -> void:
	game_manager = manager
	steam_service = service
	audit_mode = args.has(AUDIT_FLAG) or user_args.has(AUDIT_FLAG)
	audit_replay_sha256 = _read_arg_value(args, user_args, AUDIT_REPLAY_SHA_FLAG, "")
	restore_ugc_mode = args.has(RESTORE_UGC_FLAG) or user_args.has(RESTORE_UGC_FLAG)
	restore_replay_sha256 = _read_arg_value(args, user_args, RESTORE_REPLAY_SHA_FLAG, "")
	output_path = _absolute_path(_read_arg_value(args, user_args, OUTPUT_FLAG, DEFAULT_OUTPUT_PATH))
	report_path = _absolute_path(_read_arg_value(args, user_args, REPORT_FLAG, DEFAULT_REPORT_PATH))
	_load_existing_manifest()
	boards = TimeAttackRulesClass.manifest().get("boards", []).duplicate(true)
	counts["boards"] = boards.size()
	if steam_service == null or !steam_service.is_initialized():
		_finish(false, "Steam is not initialized. Start Steam and run the recovery again.")
		return
	if restore_ugc_mode:
		_restore_existing_ugc()
		return
	steam_service.leaderboard_request_completed.connect(_on_leaderboard_request_completed)
	steam_service.leaderboard_replay_download_completed.connect(_on_replay_download_completed)
	print("MXT_LEGACY_SETTINGS ", "audit" if audit_mode else "recovery",
		" starting boards=", boards.size(), " output=", output_path)
	_request_current_page()


func _restore_existing_ugc() -> void:
	var record_value = recovered_entries.get(restore_replay_sha256, {})
	if typeof(record_value) != TYPE_DICTIONARY:
		push_error("MXT_LEGACY_SETTINGS restore record was not found: " + restore_replay_sha256)
		get_tree().quit(1)
		return
	var record: Dictionary = record_value
	if String(record.get("steam_id", "")) != str(steam_service.get_steam_id()):
		push_error("MXT_LEGACY_SETTINGS can only restore the logged-in user's replay attachment.")
		get_tree().quit(1)
		return
	if !steam_service.leaderboard_replay_upload_completed.is_connected(_on_restore_ugc_completed):
		steam_service.leaderboard_replay_upload_completed.connect(_on_restore_ugc_completed)
	restore_request_id = steam_service.attach_existing_leaderboard_replay(
		String(record.get("board_name", "")), int(String(record.get("ugc_handle", "0"))))
	print("MXT_LEGACY_SETTINGS restoring canary UGC board=", record.get("board_name", ""),
		" handle=", record.get("ugc_handle", ""))


func _on_restore_ugc_completed(request_id: int, result: Dictionary) -> void:
	if request_id != restore_request_id:
		return
	var success := bool(result.get("success", false))
	print("MXT_LEGACY_SETTINGS restore complete success=", success,
		" message=", result.get("message", ""), " ugc_handle=", result.get("ugc_handle", 0))
	get_tree().quit(0 if success else 1)


func _request_current_page() -> void:
	if board_index >= boards.size():
		_finish(true, "Legacy leaderboard machine-setting audit completed." if audit_mode
			else "Legacy leaderboard machine-setting recovery completed.")
		return
	if typeof(boards[board_index]) != TYPE_DICTIONARY:
		_advance_board()
		return
	var board: Dictionary = boards[board_index]
	var board_name := String(board.get("steam_name", ""))
	if board_name.is_empty():
		_record_failure({}, "manifest_board_name_missing")
		_advance_board()
		return
	var page_end := mini(page_start + PAGE_SIZE - 1, MAX_GLOBAL_RANK)
	print("MXT_LEGACY_SETTINGS scan board=", board_name, " ranks=", page_start, "-", page_end)
	active_read_request_id = steam_service.request_leaderboard_entries(board_name, "global", page_start, page_end)


func _on_leaderboard_request_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_read_request_id:
		return
	active_read_request_id = 0
	var board: Dictionary = boards[board_index]
	var board_name := String(board.get("steam_name", ""))
	if !bool(result.get("success", false)):
		_record_failure({"board_name": board_name}, "leaderboard_read_failed", String(result.get("message", "")))
		_begin_board_downloads()
		return
	var entries: Array = result.get("entries", [])
	for entry_value in entries:
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		_collect_entry(board_name, entry_value as Dictionary)
	if entries.size() == PAGE_SIZE and page_start + PAGE_SIZE <= MAX_GLOBAL_RANK:
		page_start += PAGE_SIZE
		_request_current_page()
		return
	_begin_board_downloads()


func _collect_entry(board_name: String, entry: Dictionary) -> void:
	counts["entries_scanned"] = int(counts["entries_scanned"]) + 1
	var expected_audit := _expected_audit_record(board_name, str(int(entry.get("steam_id", 0))))
	if audit_mode and expected_audit.is_empty():
		return
	var decoded := LeaderboardDetailsClass.decode(entry.get("details", []))
	if audit_mode:
		_audit_entry(entry, decoded, expected_audit)
		return
	if decoded.is_empty():
		_record_failure(entry.merged({"board_name": board_name}, true), "unsupported_leaderboard_details")
		return
	if int(decoded.get("format_revision", -1)) != LeaderboardDetailsClass.PREVIOUS_FORMAT_REVISION:
		counts["current_entries"] = int(counts["current_entries"]) + 1
		return
	var replay_sha256 := String(decoded.get("replay_sha256", ""))
	if recovered_entries.has(replay_sha256):
		var existing: Dictionary = recovered_entries[replay_sha256]
		var contextual_entry := entry.merged({"board_name": board_name}, true)
		recovered_entries[replay_sha256] = _recovery_record(
			contextual_entry, decoded, int(existing.get("machine_setting_percent", -1)))
		counts["already_recovered"] = int(counts["already_recovered"]) + 1
		return
	var ugc_handle := int(entry.get("ugc_handle", 0))
	if replay_sha256.is_empty() or ugc_handle == 0 or ugc_handle == -1:
		_record_failure(entry.merged({"board_name": board_name}, true), "legacy_replay_unavailable")
		return
	var candidate := entry.duplicate(true)
	candidate["board_name"] = board_name
	candidate["trusted_details"] = decoded
	download_queue.append(candidate)
	counts["legacy_candidates"] = int(counts["legacy_candidates"]) + 1


func _begin_board_downloads() -> void:
	if audit_mode:
		_advance_board()
		return
	download_index = 0
	_download_next()


func _download_next() -> void:
	if download_index >= download_queue.size():
		_save_outputs(false, "")
		_advance_board()
		return
	active_entry = download_queue[download_index]
	download_index += 1
	active_download_request_id = steam_service.download_leaderboard_replay(
		int(active_entry.get("ugc_handle", 0)), MAX_REPLAY_BYTES)
	print("MXT_LEGACY_SETTINGS download board=", active_entry.get("board_name", ""),
		" rank=", active_entry.get("global_rank", 0), " steam_id=", active_entry.get("steam_id", 0))


func _on_replay_download_completed(request_id: int, result: Dictionary) -> void:
	if request_id != active_download_request_id:
		return
	active_download_request_id = 0
	if !bool(result.get("success", false)):
		_record_failure(active_entry, "replay_download_failed", String(result.get("message", "")))
		active_entry.clear()
		_download_next()
		return
	var expected_owner := int(active_entry.get("steam_id", 0))
	var actual_owner := int(result.get("owner_steam_id", 0))
	if expected_owner <= 0 or actual_owner != expected_owner:
		_record_failure(active_entry, "replay_owner_mismatch")
		active_entry.clear()
		_download_next()
		return
	var bytes_value = result.get("bytes", PackedByteArray())
	if typeof(bytes_value) != TYPE_PACKED_BYTE_ARRAY:
		_record_failure(active_entry, "invalid_replay_payload")
		active_entry.clear()
		_download_next()
		return
	var bytes: PackedByteArray = bytes_value
	var trusted: Dictionary = active_entry.get("trusted_details", {})
	var replay_sha256 := _sha256(bytes)
	if replay_sha256 != String(trusted.get("replay_sha256", "")):
		_record_failure(active_entry, "replay_digest_mismatch")
		active_entry.clear()
		_download_next()
		return
	var replay_value = JSON.parse_string(bytes.get_string_from_utf8())
	if typeof(replay_value) != TYPE_DICTIONARY:
		_record_failure(active_entry, "invalid_replay_json")
		active_entry.clear()
		_download_next()
		return
	var validation := ReplayValidatorClass.validate(game_manager, replay_value as Dictionary)
	if !bool(validation.get("valid", false)):
		_record_failure(active_entry, "replay_validation_failed", String(validation.get("reason", "")))
		active_entry.clear()
		_download_next()
		return
	if !_trusted_metadata_matches(validation, trusted, String(active_entry.get("board_name", ""))):
		_record_failure(active_entry, "trusted_metadata_mismatch")
		active_entry.clear()
		_download_next()
		return
	var machine_setting_percent := int(validation.get("machine_setting_percent", -1))
	if machine_setting_percent < 0 or machine_setting_percent > 100:
		_record_failure(active_entry, "invalid_machine_setting")
		active_entry.clear()
		_download_next()
		return
	recovered_entries[replay_sha256] = _recovery_record(active_entry, trusted, machine_setting_percent)
	counts["recovered"] = int(counts["recovered"]) + 1
	print("MXT_LEGACY_SETTINGS recovered board=", active_entry.get("board_name", ""),
		" rank=", active_entry.get("global_rank", 0), " setting=", machine_setting_percent, "%")
	active_entry.clear()
	_save_outputs(false, "")
	_download_next()


func _trusted_metadata_matches(validation: Dictionary, trusted: Dictionary, board_name: String) -> bool:
	return String(validation.get("board_name", "")) == board_name \
		and int(validation.get("ruleset_revision", -1)) == int(trusted.get("ruleset_revision", -2)) \
		and int(validation.get("replay_schema_version", -1)) == int(trusted.get("replay_schema_version", -2)) \
		and String(validation.get("track_gameplay_digest", "")) == String(trusted.get("track_gameplay_digest", "")) \
		and String(validation.get("vehicle_gameplay_digest", "")) == String(trusted.get("vehicle_gameplay_digest", ""))


func _recovery_record(entry: Dictionary, details: Dictionary, machine_setting_percent: int) -> Dictionary:
	return {
		"board_name": String(entry.get("board_name", "")),
		"steam_id": str(int(entry.get("steam_id", 0))),
		"score_milliseconds": int(entry.get("score", 0)),
		"ugc_handle": str(int(entry.get("ugc_handle", 0))),
		"game_version": (details.get("game_version", {}) as Dictionary).duplicate(true),
		"ruleset_revision": int(details.get("ruleset_revision", -1)),
		"replay_schema_version": int(details.get("replay_schema_version", -1)),
		"replay_sha256": String(details.get("replay_sha256", "")),
		"track_gameplay_digest": String(details.get("track_gameplay_digest", "")),
		"vehicle_gameplay_digest": String(details.get("vehicle_gameplay_digest", "")),
		"machine_setting_percent": machine_setting_percent,
	}


func _expected_audit_record(board_name: String, steam_id: String) -> Dictionary:
	if !audit_mode:
		return {}
	for replay_sha_value in recovered_entries:
		var replay_sha := String(replay_sha_value)
		if !audit_replay_sha256.is_empty() and replay_sha != audit_replay_sha256:
			continue
		var record_value = recovered_entries[replay_sha]
		if typeof(record_value) != TYPE_DICTIONARY:
			continue
		var record: Dictionary = record_value
		if String(record.get("board_name", "")) == board_name and String(record.get("steam_id", "")) == steam_id:
			var expected := record.duplicate(true)
			expected["replay_sha256"] = replay_sha
			return expected
	return {}


func _audit_entry(entry: Dictionary, decoded: Dictionary, expected: Dictionary) -> void:
	var replay_sha := String(expected.get("replay_sha256", ""))
	var reasons := PackedStringArray()
	if decoded.is_empty() or int(decoded.get("format_revision", -1)) != LeaderboardDetailsClass.FORMAT_REVISION:
		reasons.append("details_not_revision_3")
	else:
		if String(decoded.get("replay_sha256", "")) != replay_sha:
			reasons.append("replay_digest_changed")
		if int(decoded.get("machine_setting_percent", -1)) != int(expected.get("machine_setting_percent", -2)):
			reasons.append("machine_setting_mismatch")
	if int(entry.get("score", -1)) != int(expected.get("score_milliseconds", -2)):
		reasons.append("score_changed")
	if str(int(entry.get("ugc_handle", 0))) != String(expected.get("ugc_handle", "")):
		reasons.append("ugc_handle_changed")
	audit_results[replay_sha] = {
		"passed": reasons.is_empty(),
		"reasons": reasons,
		"board_name": String(expected.get("board_name", "")),
		"steam_id": String(expected.get("steam_id", "")),
		"score_milliseconds": int(entry.get("score", 0)),
		"ugc_handle": str(int(entry.get("ugc_handle", 0))),
	}


func _advance_board() -> void:
	download_queue.clear()
	download_index = 0
	board_index += 1
	page_start = 1
	_request_current_page()


func _record_failure(entry: Dictionary, reason: String, message := "") -> void:
	var record := {
		"board_name": String(entry.get("board_name", "")),
		"global_rank": int(entry.get("global_rank", 0)),
		"steam_id": str(int(entry.get("steam_id", 0))),
		"score_milliseconds": int(entry.get("score", 0)),
		"reason": reason,
	}
	if !message.is_empty():
		record["message"] = message
	failures.append(record)
	counts["failed"] = int(counts["failed"]) + 1
	push_warning("MXT_LEGACY_SETTINGS failed board=%s rank=%d reason=%s %s" % [
		String(record.board_name), int(record.global_rank), reason, message])


func _load_existing_manifest() -> void:
	if !FileAccess.file_exists(output_path):
		return
	var value = JSON.parse_string(FileAccess.get_file_as_string(output_path))
	if typeof(value) != TYPE_DICTIONARY or int((value as Dictionary).get("format_revision", -1)) not in [1, 2]:
		return
	var entries_value = (value as Dictionary).get("entries", {})
	if typeof(entries_value) == TYPE_DICTIONARY:
		recovered_entries = (entries_value as Dictionary).duplicate(true)


func _finish(success: bool, message: String) -> void:
	if audit_mode:
		_finish_audit(success, message)
		return
	var outputs_saved := _save_outputs(success, message)
	var final_success := success and outputs_saved
	print("MXT_LEGACY_SETTINGS complete success=", final_success,
		" scanned=", counts["entries_scanned"], " recovered=", counts["recovered"],
		" failed=", counts["failed"], " output=", output_path, " report=", report_path)
	get_tree().quit(0 if final_success else 1)


func _save_outputs(complete: bool, message: String) -> bool:
	var sorted_entries := {}
	var replay_digests := recovered_entries.keys()
	replay_digests.sort()
	for replay_digest in replay_digests:
		sorted_entries[String(replay_digest)] = recovered_entries[replay_digest]
	var manifest_saved := _write_json_atomically(output_path, {
		"format_revision": 2,
		"recovery_steam_id": str(steam_service.get_steam_id()) if steam_service != null else "",
		"entries": sorted_entries,
	})
	var report_saved := _write_json_atomically(report_path, {
		"format_revision": 1,
		"complete": complete and manifest_saved,
		"message": message,
		"generated_unix": int(Time.get_unix_time_from_system()),
		"output_path": output_path,
		"counts": counts,
		"failures": failures,
	})
	return manifest_saved and report_saved


func _finish_audit(success: bool, message: String) -> void:
	var expected_replays := PackedStringArray()
	for replay_sha_value in recovered_entries:
		var replay_sha := String(replay_sha_value)
		if audit_replay_sha256.is_empty() or replay_sha == audit_replay_sha256:
			expected_replays.append(replay_sha)
	var missing := PackedStringArray()
	var passed_count := 0
	for replay_sha in expected_replays:
		if !audit_results.has(replay_sha):
			missing.append(replay_sha)
		elif bool((audit_results[replay_sha] as Dictionary).get("passed", false)):
			passed_count += 1
	var audit_success := success and !expected_replays.is_empty() \
		and missing.is_empty() and passed_count == expected_replays.size()
	var saved := _write_json_atomically(report_path, {
		"format_revision": 1,
		"complete": audit_success,
		"message": message,
		"generated_unix": int(Time.get_unix_time_from_system()),
		"expected_count": expected_replays.size(),
		"passed_count": passed_count,
		"missing": missing,
		"results": audit_results,
	})
	print("MXT_LEGACY_SETTINGS audit complete success=", audit_success,
		" expected=", expected_replays.size(), " passed=", passed_count, " report=", report_path)
	get_tree().quit(0 if audit_success and saved else 1)


func _write_json_atomically(path: String, value: Dictionary) -> bool:
	var base_dir := path.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(base_dir) != OK:
		push_error("MXT_LEGACY_SETTINGS could not create output directory: " + base_dir)
		return false
	var temporary_path := path + ".tmp"
	var file := FileAccess.open(temporary_path, FileAccess.WRITE)
	if file == null:
		push_error("MXT_LEGACY_SETTINGS could not open output: " + temporary_path)
		return false
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.flush()
	file.close()
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)
	if DirAccess.rename_absolute(temporary_path, path) != OK:
		push_error("MXT_LEGACY_SETTINGS could not replace output: " + path)
		return false
	return true


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK or context.update(bytes) != OK:
		return ""
	return "sha256:" + context.finish().hex_encode()


func _absolute_path(path: String) -> String:
	return ProjectSettings.globalize_path(path).simplify_path()


func _read_arg_value(args: Array, user_args: Array, flag: String, default_value: String) -> String:
	for source_args_value in [args, user_args]:
		var source_args: Array = source_args_value
		var index: int = source_args.find(flag)
		if index >= 0 and index + 1 < source_args.size():
			return String(source_args[index + 1])
	return default_value
