extends SceneTree

const GameVersionData = preload("res://core/game_version.gd")


class SelectionCache extends LeaderboardReplayCache:
	var issued_tokens: Array[int] = []

	func request_replay(_board_name: String, _entry: MxtLeaderboardEntry) -> int:
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
	if game_manager.time_attack_setup.ghost_selection != game_manager.practice_setup.ghost_selection:
		_fail("Time Attack and Practice do not share one per-track ghost selection")
		return
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
	picker.selection_scope = board_name
	picker.steam_board_name = board_name
	picker.active_request_type = "global"
	picker.show()
	await process_frame
	var panel := picker.get_node("Center/Panel") as Control
	if panel.size.x > root.size.x or panel.size.y > root.size.y:
		_fail("ghost picker exceeds the supported 1280x720 viewport")
		return
	picker.active_request_type = "local"
	picker._populate_entries([], false)
	var empty_item := picker.entry_tree.get_root().get_first_child()
	empty_item.select(0)
	picker._update_actions()
	if !picker._selected_digest().is_empty():
		_fail("empty placeholder row was treated as a replay entry")
		return
	picker.active_request_type = "global"

	var own_id := int(game_manager.steam_service.get_steam_id())
	var own_data := _entry_data(own_id, "Your Time", 1, 61001, _digest("1"), true)
	var friend_data := _entry_data(2002, "Friend", 2, 62002, _digest("2"), true)
	var unavailable_data := _entry_data(3003, "No Replay", 3, 63003, _digest("3"), false)
	picker._on_entries_received(board_name, "global", _query([own_data, friend_data, unavailable_data]))
	if picker.visible_entries.size() != 3:
		_fail("Global Top 100 rows were not populated")
		return
	var tree_root := picker.entry_tree.get_root()
	var own_item := tree_root.get_first_child()
	var friend_item := own_item.get_next()
	var unavailable_item := friend_item.get_next()
	var decorated_friend: MxtLeaderboardEntry = picker.visible_entries[1]
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
	var refreshed_own_data := own_data.duplicate(true)
	refreshed_own_data["rank"] = 14
	picker._on_entries_received(board_name, "friends", _query([friend_data, refreshed_own_data]))
	if selection.count() != 1 or int((selection.snapshot()[0] as Dictionary).get("global_rank", 0)) != 14:
		_fail("Friends refresh did not preserve selection by digest and update display rank")
		return
	picker.active_request_type = "around_user"
	picker._on_entries_received(board_name, "around_user", _query([refreshed_own_data]))
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
	print("MXT_TIME_ATTACK_GHOST_PICKER_SMOKE_OK views=4 own_selectable=true viewport=1280x720")
	picker.queue_free()
	cache.queue_free()
	game_manager.queue_free()
	await process_frame
	quit(0)


func _entry_data(steam_id: int, persona_name: String, rank: int, score: int, digest: String, attached: bool) -> Dictionary:
	return {
		"steam_id": steam_id,
		"persona_name": persona_name,
		"rank": rank,
		"score_milliseconds": score,
		"run_id": "run-%d" % steam_id if attached else "",
		"replay_sha256": digest if attached else "",
		"track_gameplay_digest": _digest("a"),
		"vehicle_gameplay_digest": _digest("b"),
		"machine_setting_percent": 50,
		"ruleset_revision": 3,
		"replay_schema_version": 4,
		"game_version": GameVersionData.metadata(),
	}


func _query(entries: Array) -> MxtLeaderboardQueryResult:
	var result := MxtLeaderboardQueryResult.new()
	result.load_dictionary({"ok": true, "entries": entries}, "", "")
	return result


func _digest(character: String) -> String:
	return "sha256:" + character.repeat(64)


func _ready_result(digest: String, older_version := false) -> Dictionary:
	var version := GameVersionData.metadata()
	if older_version:
		version["compatibility"] = maxi(0, GameVersionData.COMPATIBILITY - 1)
	return {
		"success": true,
		"message": "Ready",
		"cache_path": "res://fixture.mxt_replay",
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
