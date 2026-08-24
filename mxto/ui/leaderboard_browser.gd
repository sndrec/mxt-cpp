class_name LeaderboardBrowser extends VBoxContainer

signal watch_replay_requested(board_name: String, entry: Dictionary)

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")
const LeaderboardEntryPresenterClass = preload("res://steam/leaderboard_entry_presenter.gd")

@onready var status_label: Label = $Status
@onready var summary_label: Label = $Summary
@onready var my_bests_label: Label = $MyBests
@onready var track_option: OptionButton = $Controls/Track
@onready var vehicle_option: OptionButton = $Controls/Vehicle
@onready var entry_tree: Tree = $Entries
@onready var details: RichTextLabel = $History/DetailsScroll/Details
@onready var watch_button: Button = $EntryActions/WatchReplay

var game_manager: GameManager
var client: LeaderboardClient
var boards: Array = []
var active_board_name := ""
var active_request_type := "global"
var visible_entries: Array = []
var mode_buttons: Dictionary = {}


func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	mode_buttons = {"global": $Controls/Global, "around_user": $Controls/AroundMe}
	$Controls/Friends.hide()
	for mode in mode_buttons:
		var button: Button = mode_buttons[mode]
		button.toggle_mode = true
		button.pressed.connect(_request.bind(mode))
	$Controls/Refresh.pressed.connect(_refresh_selected_board)
	$History/Actions/RetryPending.pressed.connect(_retry_pending)
	$History/Actions/ClearRejected.pressed.connect(_clear_rejected)
	watch_button.pressed.connect(_watch_selected)
	track_option.item_selected.connect(_on_track_selected)
	vehicle_option.item_selected.connect(_on_vehicle_selected)
	entry_tree.item_selected.connect(_on_entry_selected)
	_setup_tree()
	call_deferred("_initialize_client")


func _setup_tree() -> void:
	entry_tree.columns = 5
	for column in range(5):
		entry_tree.set_column_title(column, ["Rank", "Player", "Machine", "Game / Replay", "Time"][column])
	entry_tree.set_column_custom_minimum_width(0, 72)
	entry_tree.set_column_custom_minimum_width(1, 210)
	entry_tree.set_column_custom_minimum_width(2, 180)
	entry_tree.set_column_custom_minimum_width(3, 170)
	entry_tree.set_column_custom_minimum_width(4, 120)
	entry_tree.hide_root = true


func _initialize_client() -> void:
	if game_manager == null or game_manager.leaderboard_client == null:
		status_label.text = "Leaderboard client is unavailable."
		return
	client = game_manager.leaderboard_client
	if !client.submission_status_changed.is_connected(_show_submission_status):
		client.submission_status_changed.connect(_show_submission_status)
	if !client.entries_received.is_connected(_show_entries):
		client.entries_received.connect(_show_entries)
	if !client.categories_received.is_connected(_show_categories):
		client.categories_received.connect(_show_categories)
	if !client.player_bests_received.is_connected(_show_player_bests):
		client.player_bests_received.connect(_show_player_bests)
	boards.clear()
	for value in TimeAttackRulesClass.manifest().get("boards", []):
		if typeof(value) == TYPE_DICTIONARY and String((value as Dictionary).get("track_source", "official")) == "official":
			boards.append((value as Dictionary).duplicate(true))
	track_option.clear()
	for value in boards:
		var board: Dictionary = value
		track_option.add_item(TimeAttackRulesClass.board_title(board))
		track_option.set_item_metadata(track_option.item_count - 1, String(board.get("steam_name", "")))
	_show_submission_status(client.status())
	if !boards.is_empty():
		_load_selected_board()


func select_board(board_name: String) -> void:
	if client == null:
		call_deferred("select_board", board_name)
		return
	if !board_name.is_empty():
		for index in range(track_option.item_count):
			if String(track_option.get_item_metadata(index)) == board_name:
				track_option.select(index)
				break
	_load_selected_board()


func _selected_board() -> String:
	return "" if track_option.selected < 0 else String(track_option.get_item_metadata(track_option.selected))


func _selected_vehicle_digest() -> String:
	return "" if vehicle_option.selected < 0 else String(vehicle_option.get_item_metadata(vehicle_option.selected))


func _request(request_type: String) -> void:
	if client == null or _selected_board().is_empty():
		return
	active_board_name = _selected_board()
	active_request_type = request_type
	var vehicle_digest := _selected_vehicle_digest()
	for mode in mode_buttons:
		(mode_buttons[mode] as Button).set_pressed_no_signal(mode == request_type)
	visible_entries.clear()
	entry_tree.clear()
	var root := entry_tree.create_item()
	var loading := entry_tree.create_item(root)
	loading.set_text(1, "Loading %s entries…" % _friendly_mode(request_type))
	status_label.text = "Loading %s…" % _board_title(active_board_name)
	summary_label.text = ""
	watch_button.disabled = true
	client.request_entries(active_board_name, active_request_type, vehicle_digest)


func _on_track_selected(_index: int) -> void:
	_load_selected_board()


func _load_selected_board() -> void:
	if client == null or _selected_board().is_empty():
		return
	vehicle_option.clear()
	vehicle_option.add_item("Overall")
	vehicle_option.set_item_metadata(0, "")
	vehicle_option.disabled = true
	my_bests_label.text = "Loading your vehicle bests…"
	_refresh_selected_board()


func _refresh_selected_board() -> void:
	if client == null or _selected_board().is_empty():
		return
	client.request_categories(_selected_board())
	client.request_player_bests(_selected_board())
	_request(active_request_type)


func _on_vehicle_selected(_index: int) -> void:
	_request(active_request_type)


func _retry_pending() -> void:
	if client != null:
		client.retry_pending_now()


func _clear_rejected() -> void:
	if client != null:
		client.clear_rejected()


func _format_score(score_milliseconds: int) -> String:
	return "%d:%02d.%03d" % [int(score_milliseconds / 60000), int(score_milliseconds / 1000) % 60, score_milliseconds % 1000]


func _show_entries(board_name: String, request_type: String, result: Dictionary) -> void:
	if board_name != active_board_name or request_type != active_request_type \
			or String(result.get("requested_vehicle_gameplay_digest", "")) != _selected_vehicle_digest():
		return
	entry_tree.clear()
	visible_entries.clear()
	var root := entry_tree.create_item()
	if !bool(result.get("ok", false)):
		var unavailable := entry_tree.create_item(root)
		unavailable.set_text(1, "Unavailable: %s" % String(result.get("message", "Leaderboard request failed.")))
		status_label.text = "Leaderboard unavailable"
		summary_label.text = "Check connectivity and use Refresh to try again."
		return
	var entries: Array = result.get("entries", [])
	var local_steam_id := game_manager.steam_service.get_steam_id() if game_manager.steam_service != null else 0
	var local_entry: Dictionary = {}
	for value in entries:
		var entry: Dictionary = LeaderboardEntryPresenterClass.decorate(game_manager, value as Dictionary)
		visible_entries.append(entry)
		var item := entry_tree.create_item(root)
		item.set_metadata(0, visible_entries.size() - 1)
		item.set_text(0, "#%d" % int(entry.get("rank", 0)))
		item.set_text(1, String(entry.get("_display_player", "Unknown")))
		item.set_text(2, String(entry.get("_display_vehicle", "Unknown")))
		item.set_text(3, String(entry.get("_display_version", "Legacy / unknown")))
		item.set_text(4, _format_score(int(entry.get("score_milliseconds", 0))))
		if int(entry.get("steam_id", 0)) == local_steam_id:
			local_entry = entry
			for column in range(entry_tree.columns):
				item.set_custom_color(column, Color(0.45, 0.86, 1.0))
	status_label.text = "%s · %s · %s" % [_board_title(board_name), _selected_vehicle_name(), _friendly_mode(request_type)]
	if !local_entry.is_empty():
		summary_label.text = "Your verified best: #%d · %s" % [int(local_entry.get("rank", 0)), _format_score(int(local_entry.get("score_milliseconds", 0)))]
	else:
		summary_label.text = "You do not have a verified entry in this view."
	if entries.is_empty():
		var empty := entry_tree.create_item(root)
		empty.set_text(1, "No %s entries yet." % _friendly_mode(request_type).to_lower())


func _show_categories(board_name: String, result: Dictionary) -> void:
	if board_name != _selected_board():
		return
	var previous_digest := _selected_vehicle_digest()
	vehicle_option.clear()
	vehicle_option.add_item("Overall")
	vehicle_option.set_item_metadata(0, "")
	if bool(result.get("ok", false)):
		for value in result.get("categories", []):
			if typeof(value) != TYPE_DICTIONARY:
				continue
			var category: Dictionary = value
			var digest := String(category.get("vehicle_gameplay_digest", ""))
			if digest.is_empty():
				continue
			var vehicle_name := LeaderboardEntryPresenterClass.vehicle_name(
				game_manager,
				String(category.get("vehicle_content_id", "")),
				digest)
			vehicle_option.add_item("%s (%d)" % [vehicle_name, int(category.get("entry_count", 0))])
			var index := vehicle_option.item_count - 1
			vehicle_option.set_item_metadata(index, digest)
			vehicle_option.set_item_tooltip(index, "%s record: %s by %s" % [
				vehicle_name,
				_format_score(int(category.get("record_milliseconds", 0))),
				LeaderboardEntryPresenterClass.player_name(category.merged({
					"persona_name": category.get("record_persona_name", ""),
					"steam_id": category.get("record_steam_id", ""),
				}, true)),
			])
			if digest == previous_digest:
				vehicle_option.select(index)
	vehicle_option.disabled = vehicle_option.item_count <= 1
	if previous_digest != "" and _selected_vehicle_digest() != previous_digest:
		_request(active_request_type)


func _show_player_bests(board_name: String, result: Dictionary) -> void:
	if board_name != _selected_board():
		return
	if !bool(result.get("ok", false)):
		my_bests_label.text = "Your vehicle bests are unavailable."
		return
	var best_text: Array[String] = []
	for value in result.get("entries", []):
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var entry := LeaderboardEntryPresenterClass.decorate(game_manager, value as Dictionary)
		best_text.append("%s  %s" % [
			String(entry.get("_display_vehicle", "Unknown")),
			_format_score(int(entry.get("score_milliseconds", 0))),
		])
	my_bests_label.text = "Your vehicle bests: %s" % " · ".join(best_text) \
		if !best_text.is_empty() else "You do not have a verified vehicle best on this track yet."


func _on_entry_selected() -> void:
	var selected := entry_tree.get_selected()
	if selected == null:
		watch_button.disabled = true
		return
	var index := int(selected.get_metadata(0))
	if index < 0 or index >= visible_entries.size():
		watch_button.disabled = true
		return
	var entry: Dictionary = visible_entries[index]
	watch_button.disabled = !bool(entry.get("_replay_available", false))


func _watch_selected() -> void:
	var selected := entry_tree.get_selected()
	if selected == null:
		return
	var index := int(selected.get_metadata(0))
	if index >= 0 and index < visible_entries.size():
		watch_replay_requested.emit(active_board_name, (visible_entries[index] as Dictionary).duplicate(true))


func _show_submission_status(status: Dictionary) -> void:
	var lines: Array[String] = []
	lines.append("[b]Submission queue[/b]  %d pending · %d rejected · %d completed" % [int(status.get("pending_count", 0)), int(status.get("rejected_count", 0)), int(status.get("completed_count", 0))])
	var now_unix := int(Time.get_unix_time_from_system())
	for value in status.get("pending", []):
		var submission: Dictionary = value
		var next_retry := int(submission.get("next_retry_unix", 0))
		var retry_text := "now" if next_retry <= now_unix else "in %ds" % (next_retry - now_unix)
		var error_suffix := "" if String(submission.get("last_error", "")).is_empty() else " · " + String(submission.get("last_error", ""))
		lines.append("[color=#ffd166]Pending[/color]  %s · %s · %s old · attempt %d · retry %s%s" % [String(submission.get("track_title", _board_title(String(submission.get("board_name", ""))))), _format_score(int(submission.get("score_milliseconds", 0))), _age_text(now_unix - int(submission.get("created_unix", now_unix))), int(submission.get("attempts", 0)), retry_text, error_suffix])
	for value in status.get("completed", []):
		var submission: Dictionary = value
		var outcome := "Archived vehicle best" if bool(submission.get("is_vehicle_best", false)) else "Archived ranked attempt"
		lines.append("[color=#75e6a4]%s[/color]  %s · %s · %s ago" % [outcome, String(submission.get("track_title", _board_title(String(submission.get("board_name", ""))))), _format_score(int(submission.get("score_milliseconds", 0))), _age_text(now_unix - int(submission.get("accepted_unix", now_unix)))])
	for value in status.get("rejected", []):
		var submission: Dictionary = value
		lines.append("[color=#ff6961]Rejected[/color]  %s · %s · %s" % [String(submission.get("track_title", _board_title(String(submission.get("board_name", ""))))), _format_score(int(submission.get("score_milliseconds", 0))), _friendly_error(String(submission.get("rejected_reason", "unknown_reason")))])
	details.text = "\n".join(lines)


func _friendly_mode(mode: String) -> String:
	match mode:
		"around_user": return "Around Me"
		_: return "Global Top 100"


func _board_title(board_name: String) -> String:
	return TimeAttackRulesClass.board_title(TimeAttackRulesClass.board_for_name(board_name))


func _selected_vehicle_name() -> String:
	return vehicle_option.get_item_text(vehicle_option.selected).get_slice(" (", 0) \
		if vehicle_option.selected >= 0 else "Overall"


func _friendly_error(reason: String) -> String:
	return TimeAttackRulesClass.friendly_reason(reason)


func _age_text(seconds: int) -> String:
	seconds = maxi(seconds, 0)
	if seconds < 60: return "%ds" % seconds
	if seconds < 3600: return "%dm" % int(seconds / 60)
	if seconds < 86400: return "%dh" % int(seconds / 3600)
	return "%dd" % int(seconds / 86400)
