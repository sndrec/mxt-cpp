extends SceneTree

const GameVersionData = preload("res://core/game_version.gd")


class SelectionCache extends LeaderboardReplayCache:
	var issued_tokens: Array[int] = []

	func request_replay(_board_name: String, _entry: Dictionary) -> int:
		var token := next_token
		next_token += 1
		issued_tokens.append(token)
		return token

	func cancel_request(_token: int) -> void:
		pass

	func complete(token: int, result: Dictionary) -> void:
		request_completed.emit(token, result)


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1280, 720)
	var main_scene := load("res://main.tscn") as PackedScene
	var picker_scene := load("res://ui/time_attack_ghost_picker.tscn") as PackedScene
	if main_scene == null or picker_scene == null:
		_fail("required UI scene did not load")
		return
	var game_manager := main_scene.instantiate() as GameManager
	root.add_child(game_manager)
	game_manager.set_physics_process(false)
	for child in game_manager.get_children():
		if child is Timer:
			(child as Timer).stop()
	var cache := SelectionCache.new()
	root.add_child(cache)
	var selection := TimeAttackGhostSelection.new()
	selection.initialize(cache)
	var picker := picker_scene.instantiate() as TimeAttackGhostPicker
	root.add_child(picker)
	picker.initialize(game_manager, selection)
	var board_name := "mxt_ta_picker_smoke"
	selection.set_board(board_name)
	picker.board_name = board_name
	picker.active_request_type = "global"
	picker.show()
	await process_frame
	var panel := picker.get_node("Center/Panel") as Control
	if panel.size.x > root.size.x or panel.size.y > root.size.y:
		_fail("ghost picker exceeds the supported 1280x720 viewport")
		return

	var own_id := int(game_manager.steam_service.get_steam_id())
	var own_entry := _entry(own_id, "Your Time", 1, 61001, 1001, _digest("1"), true)
	var friend_entry := _entry(2002, "Friend", 2, 62002, 1002, _digest("2"), true)
	var unavailable_entry := _entry(3003, "No Replay", 3, 63003, 0, _digest("3"), false)
	picker._on_entries_received(board_name, "global", {
		"success": true,
		"entries": [own_entry, friend_entry, unavailable_entry],
	})
	if picker.visible_entries.size() != 3:
		_fail("Global Top 100 rows were not populated")
		return
	var tree_root := picker.entry_tree.get_root()
	var own_item := tree_root.get_first_child()
	var friend_item := own_item.get_next()
	var unavailable_item := friend_item.get_next()
	var decorated_friend: Dictionary = (picker.visible_entries[1] as Dictionary).duplicate(true)
	if own_item == null or !own_item.is_editable(0) or unavailable_item == null or unavailable_item.is_editable(0):
		_fail("replay availability did not control checkbox editing")
		return
	own_item.select(0)
	picker.entry_tree.grab_focus()
	var accept := InputEventAction.new()
	accept.action = "Accelerate"
	accept.pressed = true
	picker._unhandled_input(accept)
	if selection.count() != 1 or !selection.contains(_digest("1")):
		_fail("controller accept did not select the local player's own time")
		return
	var own_selection: Dictionary = selection.snapshot()[0]
	var own_token := int(own_selection.get("request_token", 0))
	cache.complete(own_token, _ready_result(_digest("1")))
	await process_frame
	if !selection.all_ready() or own_item.get_text(6) != "Ready":
		_fail("ready selection state did not reach the picker row")
		return

	picker.active_request_type = "friends"
	var refreshed_friend_entry := own_entry.duplicate(true)
	refreshed_friend_entry["global_rank"] = 14
	picker._on_entries_received(board_name, "friends", {
		"success": true,
		"entries": [friend_entry, refreshed_friend_entry],
	})
	if selection.count() != 1 or int((selection.snapshot()[0] as Dictionary).get("global_rank", 0)) != 14:
		_fail("Friends refresh did not preserve selection by digest and update display rank")
		return
	picker.active_request_type = "around_user"
	picker._on_entries_received(board_name, "around_user", {
		"success": true,
		"entries": [refreshed_friend_entry],
	})
	if picker.visible_entries.size() != 1 or !selection.contains(_digest("1")):
		_fail("Around Me view did not retain the selected local entry")
		return

	var setup := game_manager.time_attack_setup
	setup.ghost_selection = selection
	if !selection.changed.is_connected(setup._on_ghost_selection_changed):
		selection.changed.connect(setup._on_ghost_selection_changed)
	var friend_select := selection.select(board_name, decorated_friend)
	if !bool(friend_select.get("success", false)) or selection.all_ready():
		_fail("second selection did not enter unresolved state: %s" % JSON.stringify(friend_select))
		return
	setup._update_start_buttons()
	if !setup.practice_button.disabled:
		_fail("Practice start remained enabled while a selected ghost was unresolved")
		return
	var pending_selection: Dictionary = selection.selected_by_digest[_digest("2")]
	cache.complete(int(pending_selection.get("request_token", 0)), _ready_result(_digest("2"), true))
	await process_frame
	setup._update_start_buttons()
	if setup.practice_button.disabled \
			or !bool((selection.selected_by_digest[_digest("2")] as Dictionary).get("compatibility_warning", false)):
		_fail("ready older-version ghost did not re-enable Practice with its warning retained")
		return

	picker.show()
	var brake := InputEventAction.new()
	brake.action = "Brake"
	brake.pressed = true
	picker._unhandled_input(brake)
	if picker.visible:
		_fail("controller Brake did not close the picker")
		return
	print("MXT_TIME_ATTACK_GHOST_PICKER_SMOKE_OK views=3 own_selectable=true viewport=1280x720")
	picker.queue_free()
	cache.queue_free()
	game_manager.queue_free()
	await process_frame
	quit(0)


func _entry(steam_id: int, persona_name: String, rank: int, score: int, ugc_handle: int, digest: String, attached: bool) -> Dictionary:
	return {
		"steam_id": steam_id,
		"persona_name": persona_name,
		"global_rank": rank,
		"score": score,
		"ugc_handle": ugc_handle if attached else 0,
		"details": _details(digest),
	}


func _details(replay_digest: String) -> Array:
	var values: Array = [
		0x3154584d,
		3,
		1,
		4,
		(GameVersionData.MAJOR << 24) | (GameVersionData.COMPATIBILITY << 16) | GameVersionData.PATCH,
	]
	_append_digest_words(values, replay_digest)
	_append_digest_words(values, _digest("a"))
	_append_digest_words(values, _digest("b"))
	values.append(50)
	return values


func _append_digest_words(values: Array, digest: String) -> void:
	var hex := digest.trim_prefix("sha256:")
	for index in range(8):
		values.append(hex.substr(index * 8, 8).hex_to_int())


func _digest(character: String) -> String:
	return "sha256:" + character.repeat(64)


func _ready_result(digest: String, older_version := false) -> Dictionary:
	var version := GameVersionData.metadata()
	if older_version:
		version["compatibility"] = maxi(0, GameVersionData.COMPATIBILITY - 1)
	return {
		"success": true,
		"message": "Ready",
		"cache_path": "res://fixture.replay.json",
		"replay_sha256": digest,
		"trusted_details": {},
		"validation": {
			"valid": true,
			"game_version": version,
		},
	}


func _fail(message: String) -> void:
	push_error("MXT_TIME_ATTACK_GHOST_PICKER_SMOKE_FAIL " + message)
	quit(1)
