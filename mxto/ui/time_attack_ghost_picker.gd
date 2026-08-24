class_name TimeAttackGhostPicker extends Control

signal closed

const LeaderboardEntryPresenterClass = preload("res://steam/leaderboard_entry_presenter.gd")
const LocalReplayCatalogClass = preload("res://replay/local_time_attack_replay_catalog.gd")
const SLOT_COLORS: Array[Color] = [
	Color(0.30, 0.90, 1.0),
	Color(1.0, 0.36, 0.86),
	Color(1.0, 0.82, 0.25),
	Color(0.38, 1.0, 0.48),
]
const NAV_PRESS_THRESHOLD := 0.60
const NAV_RELEASE_THRESHOLD := 0.35
const NAV_DAS_SECONDS := 0.33
const NAV_ARR_SECONDS := 0.09

@onready var entry_tree: Tree = $Center/Panel/Margin/Content/Entries
@onready var status_label: Label = $Center/Panel/Margin/Content/Status
@onready var retry_button: Button = $Center/Panel/Margin/Content/Actions/Retry
@onready var clear_button: Button = $Center/Panel/Margin/Content/Actions/Clear
@onready var done_button: Button = $Center/Panel/Margin/Content/Actions/Done
@onready var global_button: Button = $Center/Panel/Margin/Content/Views/Global
@onready var around_button: Button = $Center/Panel/Margin/Content/Views/AroundMe
@onready var friends_button: Button = $Center/Panel/Margin/Content/Views/Friends
@onready var local_button: Button = $Center/Panel/Margin/Content/Views/Local
@onready var refresh_button: Button = $Center/Panel/Margin/Content/Views/Refresh

var game_manager: GameManager
var selection_model: TimeAttackGhostSelection
var selection_scope := ""
var steam_board_name := ""
var active_request_type := "global"
var visible_entries: Array = []
var item_by_digest: Dictionary = {}
var message_override := ""
var nav_direction := Vector2i.ZERO
var nav_repeat_seconds := 0.0
var local_replay_catalog := LocalReplayCatalogClass.new()


func _ready() -> void:
	_setup_tree()
	global_button.pressed.connect(_request.bind("global"))
	around_button.pressed.connect(_request.bind("around_user"))
	friends_button.hide()
	local_button.pressed.connect(_request.bind("local"))
	refresh_button.pressed.connect(func(): _request(active_request_type))
	retry_button.pressed.connect(_retry_selected)
	clear_button.pressed.connect(_clear_selection)
	done_button.pressed.connect(close)
	entry_tree.item_edited.connect(_on_item_edited)
	entry_tree.item_selected.connect(_update_actions)
	set_process(false)


func initialize(manager: GameManager, model: TimeAttackGhostSelection) -> void:
	game_manager = manager
	selection_model = model
	if game_manager != null and game_manager.leaderboard_client != null \
			and !game_manager.leaderboard_client.entries_received.is_connected(_on_entries_received):
		game_manager.leaderboard_client.entries_received.connect(_on_entries_received)
	if selection_model != null and !selection_model.changed.is_connected(_on_selection_changed):
		selection_model.changed.connect(_on_selection_changed)


func open_for_track(in_selection_scope: String, in_steam_board_name: String) -> void:
	if selection_model == null or game_manager == null:
		return
	selection_scope = in_selection_scope
	steam_board_name = in_steam_board_name
	selection_model.set_board(selection_scope)
	for button in [global_button, around_button]:
		(button as Button).disabled = steam_board_name.is_empty()
	if steam_board_name.is_empty():
		active_request_type = "local"
	message_override = ""
	show()
	set_process(true)
	_request(active_request_type)
	(local_button if active_request_type == "local" else global_button).grab_focus()


func close() -> void:
	if !visible:
		return
	hide()
	set_process(false)
	nav_direction = Vector2i.ZERO
	nav_repeat_seconds = 0.0
	closed.emit()


func _setup_tree() -> void:
	entry_tree.columns = 7
	var titles := ["Race", "Rank", "Player", "Machine", "Game / Replay", "Time", "Status"]
	for column in range(entry_tree.columns):
		entry_tree.set_column_title(column, titles[column])
	entry_tree.set_column_custom_minimum_width(0, 58)
	entry_tree.set_column_custom_minimum_width(1, 66)
	entry_tree.set_column_custom_minimum_width(2, 180)
	entry_tree.set_column_custom_minimum_width(3, 180)
	entry_tree.set_column_custom_minimum_width(4, 150)
	entry_tree.set_column_custom_minimum_width(5, 105)
	entry_tree.set_column_custom_minimum_width(6, 150)


func _request(request_type: String) -> void:
	if game_manager == null or selection_scope.is_empty():
		return
	if request_type != "local" and (game_manager.leaderboard_client == null or steam_board_name.is_empty()):
		return
	active_request_type = request_type
	for pair in [[global_button, "global"], [around_button, "around_user"], [local_button, "local"]]:
		(pair[0] as Button).set_pressed_no_signal(String(pair[1]) == request_type)
	visible_entries.clear()
	item_by_digest.clear()
	entry_tree.clear()
	var root := entry_tree.create_item()
	var loading := entry_tree.create_item(root)
	loading.set_text(2, "Loading %s entries…" % _friendly_mode(request_type))
	message_override = "Loading %s…" % _friendly_mode(request_type)
	_update_status()
	if request_type == "local":
		var track_index := game_manager.track_selector.selected
		_populate_entries(local_replay_catalog.scan(game_manager, track_index), false)
		return
	game_manager.leaderboard_client.request_entries(steam_board_name, request_type)


func _on_entries_received(request_board: String, request_type: String, result: Dictionary) -> void:
	if !visible or request_board != steam_board_name or request_type != active_request_type:
		return
	entry_tree.clear()
	visible_entries.clear()
	item_by_digest.clear()
	var root := entry_tree.create_item()
	if !bool(result.get("ok", false)):
		var unavailable := entry_tree.create_item(root)
		unavailable.set_text(2, "Unavailable: %s" % String(result.get("message", "Leaderboard request failed.")))
		message_override = "Leaderboard unavailable. Use Refresh to try again."
		_update_status()
		return
	var entries_value = result.get("entries", [])
	var entries: Array = entries_value if typeof(entries_value) == TYPE_ARRAY else []
	_populate_entries(entries, true)


func _populate_entries(entries: Array, decorate_leaderboard: bool) -> void:
	entry_tree.clear()
	visible_entries.clear()
	item_by_digest.clear()
	var root := entry_tree.create_item()
	for entry_value in entries:
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := LeaderboardEntryPresenterClass.decorate(game_manager, entry_value as Dictionary) \
			if decorate_leaderboard else (entry_value as Dictionary).duplicate(true)
		visible_entries.append(entry)
		var trusted_value = entry.get("_trusted_details", {})
		var trusted: Dictionary = trusted_value if typeof(trusted_value) == TYPE_DICTIONARY else {}
		var digest := String(trusted.get("replay_sha256", ""))
		if !digest.is_empty() and selection_model.contains(digest):
			selection_model.update_entry(selection_scope, entry)
		var item := entry_tree.create_item(root)
		item.set_metadata(0, visible_entries.size() - 1)
		item.set_cell_mode(0, TreeItem.CELL_MODE_CHECK)
		item.set_editable(0, bool(entry.get("_replay_available", false)))
		item.set_text(1, String(entry.get("_display_rank", "#%d" % int(entry.get("rank", 0)))))
		item.set_text(2, String(entry.get("_display_player", entry.get("persona_name", "Steam %s" % str(entry.get("steam_id", ""))))))
		item.set_text(3, String(entry.get("_display_vehicle", "Unknown")))
		item.set_text(4, String(entry.get("_display_version", "Legacy / unknown")))
		item.set_text(5, _format_score(int(entry.get("score_milliseconds", 0))))
		if !digest.is_empty():
			item_by_digest[digest] = item
	if visible_entries.is_empty():
		var empty := entry_tree.create_item(root)
		empty.set_text(2, "No compatible replays saved for this track." if active_request_type == "local" \
			else "No %s entries yet." % _friendly_mode(active_request_type).to_lower())
	message_override = ""
	_refresh_row_states()
	var first := root.get_first_child()
	if first != null:
		first.select(0)


func _on_item_edited() -> void:
	var item := entry_tree.get_edited()
	if item == null or entry_tree.get_edited_column() != 0:
		return
	_toggle_item(item, item.is_checked(0))


func _toggle_item(item: TreeItem, checked: bool) -> void:
	var index := int(item.get_metadata(0))
	if index < 0 or index >= visible_entries.size():
		return
	var entry: Dictionary = visible_entries[index]
	var trusted_value = entry.get("_trusted_details", {})
	var trusted: Dictionary = trusted_value if typeof(trusted_value) == TYPE_DICTIONARY else {}
	var digest := String(trusted.get("replay_sha256", ""))
	message_override = ""
	if checked:
		var result: Dictionary
		if String(entry.get("_source", "leaderboard")) == "local":
			var prepare := local_replay_catalog.prepare_entry(game_manager, entry, game_manager.track_selector.selected)
			if bool(prepare.get("success", false)):
				var prepared_entry: Dictionary = prepare.get("entry", {})
				visible_entries[index] = prepared_entry
				entry = prepared_entry
				result = selection_model.select_local(selection_scope, entry)
			else:
				result = prepare
		else:
			result = selection_model.select(selection_scope, entry)
		if !bool(result.get("success", false)):
			item.set_checked(0, false)
			message_override = String(result.get("message", "That ghost could not be selected."))
	else:
		selection_model.unselect(digest)
	_refresh_row_states()


func _retry_selected() -> void:
	var digest := _selected_digest()
	if digest.is_empty():
		return
	var result := selection_model.retry(digest)
	if !bool(result.get("success", false)):
		message_override = String(result.get("message", "Replay retry failed."))
	_refresh_row_states()


func _clear_selection() -> void:
	selection_model.clear()
	message_override = ""
	_refresh_row_states()


func _on_selection_changed(_snapshot: Array) -> void:
	_refresh_row_states()


func _refresh_row_states() -> void:
	if selection_model == null or !is_node_ready():
		return
	var selections: Dictionary = {}
	for selection_value in selection_model.snapshot():
		var selection: Dictionary = selection_value
		selections[String(selection.get("replay_sha256", ""))] = selection
	for digest_value in item_by_digest:
		var digest := String(digest_value)
		var item_value = item_by_digest[digest]
		if !(item_value is TreeItem):
			continue
		var item: TreeItem = item_value
		var selected := selections.has(digest)
		item.set_checked(0, selected)
		if selected:
			var selection: Dictionary = selections[digest]
			var state := String(selection.get("state", "preparing"))
			var state_text := "Downloading…" if state == "preparing" else state.capitalize()
			if state == "ready" and bool(selection.get("compatibility_warning", false)):
				state_text = "Ready · older version"
			item.set_text(6, state_text)
			var color := SLOT_COLORS[clampi(int(selection.get("slot_index", 0)), 0, SLOT_COLORS.size() - 1)]
			for column in range(entry_tree.columns):
				item.set_custom_color(column, color)
		else:
			var entry_index := int(item.get_metadata(0))
			var available := entry_index >= 0 and entry_index < visible_entries.size() \
				and bool((visible_entries[entry_index] as Dictionary).get("_replay_available", false))
			var older_version := available and bool((visible_entries[entry_index] as Dictionary).get("_compatibility_warning", false))
			var availability_text := "No replay"
			if available:
				var local := String((visible_entries[entry_index] as Dictionary).get("_source", "")) == "local"
				availability_text = "Local · older version" if local and older_version \
					else "Local" if local \
					else "Available · older version" if older_version else "Available"
			item.set_text(6, availability_text)
			for column in range(entry_tree.columns):
				item.clear_custom_color(column)
	_update_actions()
	_update_status()


func _update_actions() -> void:
	var digest := _selected_digest()
	var state := ""
	var selections := selection_model.snapshot() if selection_model != null else []
	for selection_value in selections:
		var selection: Dictionary = selection_value
		if String(selection.get("replay_sha256", "")) == digest:
			state = String(selection.get("state", ""))
			break
	retry_button.disabled = state != "failed"
	clear_button.disabled = selection_model == null or selection_model.count() == 0


func _update_status() -> void:
	if !message_override.is_empty():
		status_label.text = message_override
		return
	if selection_model == null or selection_model.count() == 0:
		status_label.text = "No ghosts selected."
		return
	var ready_count := 0
	var failed_count := 0
	var first_failure_message := ""
	for selection_value in selection_model.snapshot():
		var selection: Dictionary = selection_value
		var state := String(selection.get("state", ""))
		ready_count += 1 if state == "ready" else 0
		failed_count += 1 if state == "failed" else 0
		if state == "failed" and first_failure_message.is_empty():
			first_failure_message = String(selection.get("message", "Replay unavailable."))
	status_label.text = "%d/4 selected · %d ready%s" % [
		selection_model.count(),
		ready_count,
		" · %d failed: %s" % [failed_count, first_failure_message] if failed_count > 0 else "",
	]


func _selected_digest() -> String:
	var item := entry_tree.get_selected()
	if item == null:
		return ""
	var index_value = item.get_metadata(0)
	if typeof(index_value) != TYPE_INT:
		return ""
	var index := int(index_value)
	if index < 0 or index >= visible_entries.size():
		return ""
	var trusted_value = (visible_entries[index] as Dictionary).get("_trusted_details", {})
	return String((trusted_value as Dictionary).get("replay_sha256", "")) if typeof(trusted_value) == TYPE_DICTIONARY else ""


func _process(delta: float) -> void:
	var next_direction := _controller_direction()
	if next_direction == Vector2i.ZERO:
		nav_direction = Vector2i.ZERO
		nav_repeat_seconds = 0.0
		return
	if next_direction != nav_direction:
		nav_direction = next_direction
		nav_repeat_seconds = NAV_DAS_SECONDS
		_navigate(next_direction)
		return
	nav_repeat_seconds -= delta
	if nav_repeat_seconds <= 0.0:
		nav_repeat_seconds += NAV_ARR_SECONDS
		_navigate(next_direction)


func _unhandled_input(event: InputEvent) -> void:
	if !visible:
		return
	if event.is_action_pressed("Brake"):
		close()
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("Accelerate"):
		_accept_focused()
		get_viewport().set_input_as_handled()
		return
	var direction := Vector2i.ZERO
	if event.is_action_pressed("DpadUp") or event.is_action_pressed("DPadUp"):
		direction = Vector2i.UP
	elif event.is_action_pressed("DpadDown"):
		direction = Vector2i.DOWN
	elif event.is_action_pressed("DpadLeft"):
		direction = Vector2i.LEFT
	elif event.is_action_pressed("DpadRight"):
		direction = Vector2i.RIGHT
	if direction != Vector2i.ZERO:
		_navigate(direction)
		get_viewport().set_input_as_handled()


func _controller_direction() -> Vector2i:
	var x := Input.get_axis("SteerLeft", "SteerRight")
	var y := Input.get_axis("SteerUp", "SteerDown")
	var release := NAV_RELEASE_THRESHOLD if nav_direction != Vector2i.ZERO else NAV_PRESS_THRESHOLD
	if absf(y) >= absf(x) and absf(y) >= release:
		return Vector2i(0, 1 if y > 0.0 else -1)
	if absf(x) >= release:
		return Vector2i(1 if x > 0.0 else -1, 0)
	return Vector2i.ZERO


func _navigate(direction: Vector2i) -> void:
	if direction.y != 0:
		_navigate_vertical(direction.y)
	elif direction.x != 0:
		_navigate_horizontal(direction.x)


func _navigate_vertical(direction: int) -> void:
	var focus := get_viewport().gui_get_focus_owner()
	if focus == entry_tree:
		var current := entry_tree.get_selected()
		var next := current.get_prev() if current != null and direction < 0 else current.get_next() if current != null else null
		if next != null:
			next.select(0)
			entry_tree.scroll_to_item(next)
		elif direction < 0:
			global_button.grab_focus()
		else:
			done_button.grab_focus()
		return
	if _view_controls().has(focus):
		if direction > 0:
			_focus_tree_edge(true)
		else:
			done_button.grab_focus()
		return
	if _action_controls().has(focus):
		if direction < 0:
			_focus_tree_edge(false)
		else:
			global_button.grab_focus()
		return
	global_button.grab_focus()


func _navigate_horizontal(direction: int) -> void:
	var focus := get_viewport().gui_get_focus_owner()
	var controls := _view_controls() if _view_controls().has(focus) else _action_controls()
	if controls.is_empty() or !controls.has(focus):
		return
	var index := controls.find(focus)
	for offset in range(1, controls.size() + 1):
		var candidate: Control = controls[posmod(index + direction * offset, controls.size())]
		if !(candidate is BaseButton) or !(candidate as BaseButton).disabled:
			candidate.grab_focus()
			return


func _focus_tree_edge(first: bool) -> void:
	var root := entry_tree.get_root()
	if root == null:
		return
	var item := root.get_first_child()
	if !first:
		while item != null and item.get_next() != null:
			item = item.get_next()
	if item == null:
		return
	item.select(0)
	entry_tree.grab_focus()
	entry_tree.scroll_to_item(item)


func _accept_focused() -> void:
	var focus := get_viewport().gui_get_focus_owner()
	if focus == entry_tree:
		var item := entry_tree.get_selected()
		if item != null and item.is_editable(0):
			var checked := !item.is_checked(0)
			item.set_checked(0, checked)
			_toggle_item(item, checked)
		return
	if focus is BaseButton and !(focus as BaseButton).disabled:
		(focus as BaseButton).pressed.emit()


func _view_controls() -> Array[Control]:
	return [global_button, around_button, local_button, refresh_button]


func _action_controls() -> Array[Control]:
	return [retry_button, clear_button, done_button]


func _friendly_mode(mode: String) -> String:
	match mode:
		"around_user": return "Around Me"
		"local": return "Local Replays"
		_: return "Global Top 100"


func _format_score(milliseconds: int) -> String:
	return "%d:%02d.%03d" % [int(milliseconds / 60000), int(milliseconds / 1000) % 60, milliseconds % 1000]
