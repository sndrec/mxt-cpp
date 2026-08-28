extends SceneTree

const CACHE_ROOT := "res://.time_attack_ghost_cache_smoke"
const ReplayValidatorClass = preload("res://leaderboards/leaderboard_replay_validator.gd")


class SelectionCache extends LeaderboardReplayCache:
	var issued_tokens: Array[int] = []
	var canceled_tokens: Array[int] = []

	func request_replay(_board_name: String, _entry: MxtLeaderboardEntry) -> int:
		var token := next_token
		next_token += 1
		issued_tokens.append(token)
		return token

	func cancel_request(token: int) -> void:
		canceled_tokens.append(token)

	func complete(token: int, result: Dictionary) -> void:
		request_completed.emit(token, result)


var game_manager: GameManager
var cache: LeaderboardReplayCache
var completions: Dictionary = {}


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_path := _argument_value("--ghost-replay-path")
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path):
		_fail("--ghost-replay-path must identify a cached leaderboard replay")
		return
	_cleanup_cache_root()
	var original_text := FileAccess.get_file_as_string(replay_path)
	var replay_value = JSON.parse_string(original_text)
	if typeof(replay_value) != TYPE_DICTIONARY:
		_fail("cached replay did not parse")
		return
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	game_manager = packed.instantiate() as GameManager
	root.add_child(game_manager)
	var base_validation: Dictionary = ReplayValidatorClass.validate(game_manager, replay_value as Dictionary)
	if !bool(base_validation.get("valid", false)):
		_fail("fixture replay is not trusted: %s" % String(base_validation.get("reason", "unknown")))
		return
	cache = LeaderboardReplayCache.new()
	cache.cache_root = CACHE_ROOT
	root.add_child(cache)
	cache.initialize(game_manager, null)
	cache.request_completed.connect(_on_request_completed)

	var bytes_a := (original_text + "\n").to_utf8_buffer()
	var bytes_b := (original_text + "\n\n").to_utf8_buffer()
	var bytes_c := (original_text + "\n \n").to_utf8_buffer()
	var entry_a := _entry_for_bytes(bytes_a, base_validation, 701)
	var entry_b := _entry_for_bytes(bytes_b, base_validation, 702)
	var entry_c := _entry_for_bytes(bytes_c, base_validation, 703)
	var board_name := String(base_validation.get("board_name", ""))

	var missing_entry := MxtLeaderboardEntry.new()
	missing_entry.load_dictionary({"score_milliseconds": 0, "replay_sha256": entry_a.replay_sha256})
	var missing_token := cache.request_replay(board_name, missing_entry)
	await _wait_for_completion(missing_token)
	if String((completions[missing_token] as Dictionary).get("reason", "")) != "missing_replay_attachment":
		_fail("missing UGC did not produce its explicit failure")
		return

	var token_a1 := _prime_download(cache, board_name, entry_a)
	var token_a2 := _prime_download(cache, board_name, entry_a)
	var digest_a := entry_a.replay_sha256
	if cache._waiter_tokens(digest_a).size() != 2:
		_fail("same-digest requests were not deduplicated")
		return
	_activate_download(cache, digest_a, 101)
	cache.cancel_request(token_a1)
	cache._on_download_completed(cache.active_download_request_id, {"success": true, "bytes": bytes_a})
	await _wait_for_completion(token_a2)
	if completions.has(token_a1) or !bool((completions[token_a2] as Dictionary).get("success", false)):
		_fail("cancellation notified the canceled consumer or lost the remaining waiter")
		return

	var hit_token := cache.request_replay(board_name, entry_a)
	await _wait_for_completion(hit_token)
	if !bool((completions[hit_token] as Dictionary).get("cache_hit", false)):
		_fail("validated same-session request did not hit the cache")
		return

	var token_b := _prime_download(cache, board_name, entry_b)
	var token_c := _prime_download(cache, board_name, entry_c)
	var digest_b := entry_b.replay_sha256
	var digest_c := entry_c.replay_sha256
	_activate_download(cache, digest_b, 102)
	cache.queued_digests.append(digest_c)
	cache._pump_queue()
	if cache.active_digest != digest_b or cache.queued_digests != [digest_c]:
		_fail("second digest was allowed to bypass the active native transfer")
		return
	cache._on_download_completed(cache.active_download_request_id, {"success": true, "bytes": bytes_b})
	await process_frame
	await process_frame
	await _wait_for_completion(token_b)
	await _wait_for_completion(token_c)
	if String((completions[token_c] as Dictionary).get("reason", "")) != "steam_offline":
		_fail("serialized queue did not advance after the active transfer completed")
		return
	var token_c2 := _prime_download(cache, board_name, entry_c)
	_activate_download(cache, digest_c, 103)
	cache._on_download_completed(cache.active_download_request_id, {"success": true, "bytes": bytes_c})
	await _wait_for_completion(token_c2)

	var warm_bytes := (original_text + "\n\t\n").to_utf8_buffer()
	var warm_entry := _entry_for_bytes(warm_bytes, base_validation, 704)
	var warm_token := _prime_download(cache, board_name, warm_entry)
	var warm_digest := warm_entry.replay_sha256
	_activate_download(cache, warm_digest, 104)
	var warm_path := String(cache.active_download_context.get("cache_path", ""))
	cache.cancel_request(warm_token)
	cache._on_download_completed(cache.active_download_request_id, {"success": true, "bytes": warm_bytes})
	await process_frame
	if completions.has(warm_token) or !FileAccess.file_exists(warm_path):
		_fail("canceled in-flight transfer did not warm the cache silently")
		return

	var corrupt_bytes := (original_text + "\n\r\n").to_utf8_buffer()
	var corrupt_entry := _entry_for_bytes(corrupt_bytes, base_validation, 705)
	var corrupt_digest := corrupt_entry.replay_sha256
	var corrupt_path := CACHE_ROOT.path_join(corrupt_digest.trim_prefix("sha256:") + ".attachment")
	_write_bytes(corrupt_path, "not a replay".to_utf8_buffer())
	var corrupt_cache := LeaderboardReplayCache.new()
	corrupt_cache.cache_root = CACHE_ROOT
	root.add_child(corrupt_cache)
	corrupt_cache.initialize(game_manager, null)
	corrupt_cache.request_completed.connect(_on_request_completed)
	var corrupt_token := corrupt_cache.request_replay(board_name, corrupt_entry)
	await process_frame
	await process_frame
	await _wait_for_completion(corrupt_token)
	if FileAccess.file_exists(corrupt_path) or String((completions[corrupt_token] as Dictionary).get("reason", "")) != "steam_offline":
		_fail("invalid cached bytes were not deleted before a redownload attempt")
		return
	corrupt_cache.queue_free()

	var invalid_bytes := "invalid download".to_utf8_buffer()
	var invalid_token := _prime_download(cache, board_name, corrupt_entry)
	_activate_download(cache, corrupt_digest, 105)
	cache._on_download_completed(cache.active_download_request_id, {"success": true, "bytes": invalid_bytes})
	await _wait_for_completion(invalid_token)
	if String((completions[invalid_token] as Dictionary).get("reason", "")) != "replay_digest_mismatch":
		_fail("invalid downloaded digest did not produce its explicit rejection")
		return

	if !(await _exercise_selection_model(board_name, base_validation, original_text)):
		return

	var cache_stats := cache.stats()
	print("MXT_TIME_ATTACK_GHOST_CACHE_SELECTION_SMOKE_OK cache_hits=", int(cache_stats.get("cache_hits", 0)),
		" downloaded_replays=", int(cache_stats.get("downloaded_replays", 0)))
	_cleanup_cache_root()
	cache.queue_free()
	game_manager.queue_free()
	await process_frame
	quit(0)


func _exercise_selection_model(board_name: String, base_validation: Dictionary, original_text: String) -> bool:
	var selection_cache := SelectionCache.new()
	root.add_child(selection_cache)
	var selection := TimeAttackGhostSelection.new()
	selection.initialize(selection_cache)
	selection.set_board(board_name)
	var entries: Array[MxtLeaderboardEntry] = []
	for index in range(5):
		var bytes := (original_text + "\n" + " ".repeat(index + 10)).to_utf8_buffer()
		entries.append(_entry_for_bytes(bytes, base_validation, 800 + index))
		var result := selection.select(board_name, entries[index])
		if index < 4 and !bool(result.get("success", false)):
			_fail("selection %d was unexpectedly refused" % index)
			return false
		if index == 4 and String(result.get("reason", "")) != "ghost_limit_reached":
			_fail("fifth selection did not enforce the four-ghost limit")
			return false
	if selection.count() != 4 or selection.all_ready():
		_fail("preparing selection state is incorrect")
		return false
	var first_snapshot: Dictionary = selection.snapshot()[0]
	var first_digest := String(first_snapshot.get("replay_sha256", ""))
	var first_token := int(first_snapshot.get("request_token", 0))
	selection_cache.complete(first_token, {"success": false, "reason": "fixture_failure", "message": "fixture failure"})
	await process_frame
	if String((selection.selected_by_digest[first_digest] as Dictionary).get("state", "")) != "failed":
		_fail("download failure did not enter the failed selection state")
		return false
	var retry_result := selection.retry(first_digest)
	if !bool(retry_result.get("success", false)) or int((selection.selected_by_digest[first_digest] as Dictionary).get("request_token", 0)) == first_token:
		_fail("failed selection did not issue a fresh retry token")
		return false
	var retry_token := int((selection.selected_by_digest[first_digest] as Dictionary).get("request_token", 0))
	selection_cache.complete(retry_token, {
		"success": true,
		"cache_path": "res://fixture.mxt_replay",
		"replay_sha256": first_digest,
		"trusted_details": entries[0].trusted_details(),
		"validation": base_validation.duplicate(true),
	})
	await process_frame
	var original_slot := int((selection.selected_by_digest[first_digest] as Dictionary).get("slot_index", -1))
	selection.unselect(first_digest)
	var replacement_result := selection.select(board_name, entries[4])
	if !bool(replacement_result.get("success", false)) or int(replacement_result.get("slot_index", -1)) != original_slot:
		_fail("selection did not reuse the first available stable display slot")
		return false
	selection.clear()
	if selection.count() != 0 or !selection.all_ready() or !selection.digest_by_request_token.is_empty():
		_fail("selection clear did not cancel and release all transient state")
		return false
	selection_cache.queue_free()
	return true


func _prime_download(target_cache: LeaderboardReplayCache, board_name: String, entry: MxtLeaderboardEntry) -> int:
	var token := target_cache.next_token
	target_cache.next_token += 1
	var request: Dictionary = target_cache._build_request(board_name, entry)
	request["token"] = token
	target_cache.requests_by_token[token] = request
	target_cache._add_waiter(String(request.get("replay_sha256", "")), token)
	return token


func _activate_download(target_cache: LeaderboardReplayCache, digest: String, request_id: int) -> void:
	target_cache.active_digest = digest
	target_cache.active_download_request_id = request_id
	target_cache.active_download_context = target_cache._first_waiting_request(digest)


func _entry_for_bytes(bytes: PackedByteArray, validation: Dictionary, ugc_handle: int) -> MxtLeaderboardEntry:
	var details := {
		"replay_sha256": _sha256(bytes),
		"ruleset_revision": int(validation.get("ruleset_revision", -1)),
		"replay_schema_version": int(validation.get("replay_schema_version", -1)),
		"track_gameplay_digest": String(validation.get("track_gameplay_digest", "")),
		"vehicle_gameplay_digest": String(validation.get("vehicle_gameplay_digest", "")),
		"machine_setting_percent": int(validation.get("machine_setting_percent", -1)),
	}
	var entry := MxtLeaderboardEntry.new()
	entry.load_dictionary({
		"steam_id": ugc_handle,
		"persona_name": "Ghost %d" % ugc_handle,
		"rank": ugc_handle - 700,
		"score_milliseconds": 60000 + ugc_handle,
		"run_id": "run-%d" % ugc_handle,
		"replay_sha256": details["replay_sha256"],
		"ruleset_revision": details["ruleset_revision"],
		"replay_schema_version": details["replay_schema_version"],
		"track_gameplay_digest": details["track_gameplay_digest"],
		"vehicle_gameplay_digest": details["vehicle_gameplay_digest"],
		"machine_setting_percent": details["machine_setting_percent"],
		"game_version": validation.get("game_version", {}),
	})
	return entry


func _wait_for_completion(token: int) -> void:
	for _frame in range(30):
		if completions.has(token):
			return
		await process_frame
	_fail("request token %d did not complete" % token)


func _on_request_completed(token: int, result: Dictionary) -> void:
	completions[token] = result.duplicate(true)


func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(CACHE_ROOT))
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_buffer(bytes)
		file.close()


func _cleanup_cache_root() -> void:
	var absolute_root := ProjectSettings.globalize_path(CACHE_ROOT)
	if !DirAccess.dir_exists_absolute(absolute_root):
		return
	var directory := DirAccess.open(absolute_root)
	if directory == null:
		return
	for file_name in directory.get_files():
		DirAccess.remove_absolute(absolute_root.path_join(file_name))
	DirAccess.remove_absolute(absolute_root)


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return "sha256:" + context.finish().hex_encode()


func _argument_value(flag: String) -> String:
	for values in [OS.get_cmdline_args(), OS.get_cmdline_user_args()]:
		var index := (values as Array).find(flag)
		if index >= 0 and index + 1 < (values as Array).size():
			return String((values as Array)[index + 1])
	return ""


func _fail(message: String) -> void:
	_cleanup_cache_root()
	push_error("MXT_TIME_ATTACK_GHOST_CACHE_SELECTION_SMOKE_FAIL " + message)
	quit(1)
