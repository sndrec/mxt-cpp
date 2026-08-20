class_name TimeAttackGhostSelection extends RefCounted

signal changed(snapshot: Array)

const GameVersionData = preload("res://core/game_version.gd")
const MAX_GHOSTS := 4

var replay_cache: LeaderboardReplayCache
var active_board_name := ""
var selected_by_digest: Dictionary = {}
var selected_order: Array[String] = []
var digest_by_request_token: Dictionary = {}


func initialize(cache: LeaderboardReplayCache) -> void:
	replay_cache = cache
	if replay_cache != null and !replay_cache.request_completed.is_connected(_on_cache_request_completed):
		replay_cache.request_completed.connect(_on_cache_request_completed)


func set_board(board_name: String) -> void:
	if board_name == active_board_name:
		return
	clear()
	active_board_name = board_name


func select(board_name: String, entry: Dictionary) -> Dictionary:
	if active_board_name.is_empty():
		active_board_name = board_name
	elif board_name != active_board_name:
		return _failure("wrong_leaderboard", "Ghost selections must come from the current track.")
	var trusted_value = entry.get("_trusted_details", {})
	if typeof(trusted_value) != TYPE_DICTIONARY:
		return _failure("missing_trusted_replay_metadata", "This time has no trusted replay metadata.")
	var trusted: Dictionary = trusted_value
	var digest := String(trusted.get("replay_sha256", ""))
	var ugc_handle := int(entry.get("ugc_handle", 0))
	if digest.is_empty() or ugc_handle == 0 or ugc_handle == -1:
		return _failure("missing_replay_attachment", "This time does not have a replay attached.")
	if selected_by_digest.has(digest):
		update_entry(board_name, entry)
		return {"success": true, "replay_sha256": digest}
	if selected_order.size() >= MAX_GHOSTS:
		return _failure("ghost_limit_reached", "Up to four leaderboard ghosts may be selected.")
	if replay_cache == null:
		return _failure("replay_cache_unavailable", "The leaderboard replay cache is unavailable.")
	var slot_index := _first_available_slot()
	var selection := _selection_record(board_name, entry, trusted, digest, slot_index)
	var token := replay_cache.request_replay(board_name, entry)
	selection["request_token"] = token
	selected_by_digest[digest] = selection
	selected_order.append(digest)
	digest_by_request_token[token] = digest
	_emit_changed()
	return {"success": true, "replay_sha256": digest, "slot_index": slot_index}


func unselect(replay_sha256: String) -> void:
	var selection_value = selected_by_digest.get(replay_sha256, {})
	if typeof(selection_value) != TYPE_DICTIONARY or (selection_value as Dictionary).is_empty():
		return
	var token := int((selection_value as Dictionary).get("request_token", 0))
	if token != 0:
		digest_by_request_token.erase(token)
		if replay_cache != null:
			replay_cache.cancel_request(token)
	selected_by_digest.erase(replay_sha256)
	selected_order.erase(replay_sha256)
	_emit_changed()


func retry(replay_sha256: String) -> Dictionary:
	var selection_value = selected_by_digest.get(replay_sha256, {})
	if typeof(selection_value) != TYPE_DICTIONARY or (selection_value as Dictionary).is_empty():
		return _failure("selection_missing", "That ghost is no longer selected.")
	if replay_cache == null:
		return _failure("replay_cache_unavailable", "The leaderboard replay cache is unavailable.")
	var selection: Dictionary = selection_value
	var old_token := int(selection.get("request_token", 0))
	if old_token != 0:
		digest_by_request_token.erase(old_token)
		replay_cache.cancel_request(old_token)
	var entry_value = selection.get("entry", {})
	if typeof(entry_value) != TYPE_DICTIONARY:
		return _failure("selection_missing", "The selected leaderboard entry is unavailable.")
	var token := replay_cache.request_replay(String(selection.get("board_name", "")), entry_value as Dictionary)
	selection["request_token"] = token
	selection["state"] = "preparing"
	selection["message"] = "Preparing replay…"
	selection.erase("cache_result")
	selected_by_digest[replay_sha256] = selection
	digest_by_request_token[token] = replay_sha256
	_emit_changed()
	return {"success": true, "replay_sha256": replay_sha256}


func update_entry(board_name: String, entry: Dictionary) -> void:
	if board_name != active_board_name:
		return
	var trusted_value = entry.get("_trusted_details", {})
	if typeof(trusted_value) != TYPE_DICTIONARY:
		return
	var digest := String((trusted_value as Dictionary).get("replay_sha256", ""))
	var selection_value = selected_by_digest.get(digest, {})
	if typeof(selection_value) != TYPE_DICTIONARY or (selection_value as Dictionary).is_empty():
		return
	var selection: Dictionary = selection_value
	selection["entry"] = entry.duplicate(true)
	selection["steam_id"] = int(entry.get("steam_id", 0))
	selection["persona_name"] = String(entry.get("persona_name", "Steam %s" % String(entry.get("steam_id", ""))))
	selection["global_rank"] = int(entry.get("global_rank", 0))
	selection["score_milliseconds"] = int(entry.get("score", 0))
	selection["ugc_handle"] = int(entry.get("ugc_handle", 0))
	selected_by_digest[digest] = selection
	_emit_changed()


func clear() -> void:
	if replay_cache != null:
		for token_value in digest_by_request_token:
			replay_cache.cancel_request(int(token_value))
	digest_by_request_token.clear()
	selected_by_digest.clear()
	selected_order.clear()
	_emit_changed()


func count() -> int:
	return selected_order.size()


func all_ready() -> bool:
	for digest in selected_order:
		var selection: Dictionary = selected_by_digest.get(digest, {})
		if String(selection.get("state", "")) != "ready":
			return false
	return true


func contains(replay_sha256: String) -> bool:
	return selected_by_digest.has(replay_sha256)


func snapshot() -> Array:
	var result: Array = []
	for digest in selected_order:
		var selection_value = selected_by_digest.get(digest, {})
		if typeof(selection_value) == TYPE_DICTIONARY:
			result.append((selection_value as Dictionary).duplicate(true))
	return result


func ready_descriptors() -> Array:
	if !all_ready():
		return []
	var result: Array = []
	for selection_value in snapshot():
		var selection: Dictionary = selection_value
		var cache_result_value = selection.get("cache_result", {})
		if typeof(cache_result_value) != TYPE_DICTIONARY:
			return []
		var descriptor: Dictionary = (cache_result_value as Dictionary).duplicate(true)
		descriptor["board_name"] = String(selection.get("board_name", ""))
		descriptor["steam_id"] = int(selection.get("steam_id", 0))
		descriptor["persona_name"] = String(selection.get("persona_name", ""))
		descriptor["global_rank"] = int(selection.get("global_rank", 0))
		descriptor["score_milliseconds"] = int(selection.get("score_milliseconds", 0))
		descriptor["slot_index"] = int(selection.get("slot_index", 0))
		descriptor["compatibility_warning"] = bool(selection.get("compatibility_warning", false))
		result.append(descriptor)
	return result


func _on_cache_request_completed(token: int, result: Dictionary) -> void:
	if !digest_by_request_token.has(token):
		return
	var digest := String(digest_by_request_token[token])
	digest_by_request_token.erase(token)
	var selection_value = selected_by_digest.get(digest, {})
	if typeof(selection_value) != TYPE_DICTIONARY or (selection_value as Dictionary).is_empty():
		return
	var selection: Dictionary = selection_value
	if int(selection.get("request_token", 0)) != token:
		return
	selection["request_token"] = 0
	selection["message"] = String(result.get("message", ""))
	if bool(result.get("success", false)):
		selection["state"] = "ready"
		selection["cache_result"] = result.duplicate(true)
		selection["compatibility_warning"] = _has_compatibility_warning(result)
	else:
		selection["state"] = "failed"
		selection["failure_reason"] = String(result.get("reason", "replay_unavailable"))
	selected_by_digest[digest] = selection
	_emit_changed()


func _selection_record(board_name: String, entry: Dictionary, trusted: Dictionary, digest: String, slot_index: int) -> Dictionary:
	return {
		"board_name": board_name,
		"steam_id": int(entry.get("steam_id", 0)),
		"persona_name": String(entry.get("persona_name", "Steam %s" % String(entry.get("steam_id", "")))),
		"global_rank": int(entry.get("global_rank", 0)),
		"score_milliseconds": int(entry.get("score", 0)),
		"ugc_handle": int(entry.get("ugc_handle", 0)),
		"replay_sha256": digest,
		"trusted_details": trusted.duplicate(true),
		"slot_index": slot_index,
		"state": "preparing",
		"message": "Preparing replay…",
		"compatibility_warning": false,
		"entry": entry.duplicate(true),
	}


func _first_available_slot() -> int:
	var occupied: Array[int] = []
	for selection_value in selected_by_digest.values():
		if typeof(selection_value) == TYPE_DICTIONARY:
			occupied.append(int((selection_value as Dictionary).get("slot_index", -1)))
	for slot_index in range(MAX_GHOSTS):
		if !occupied.has(slot_index):
			return slot_index
	return -1


func _has_compatibility_warning(result: Dictionary) -> bool:
	var validation_value = result.get("validation", {})
	if typeof(validation_value) != TYPE_DICTIONARY:
		return false
	var version_value = (validation_value as Dictionary).get("game_version", {})
	if typeof(version_value) != TYPE_DICTIONARY:
		return false
	var version: Dictionary = version_value
	return int(version.get("major", -1)) != GameVersionData.MAJOR \
		or int(version.get("compatibility", -1)) != GameVersionData.COMPATIBILITY


func _emit_changed() -> void:
	changed.emit(snapshot())


func _failure(reason: String, message: String) -> Dictionary:
	return {"success": false, "reason": reason, "message": message}
