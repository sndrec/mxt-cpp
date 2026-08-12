class_name LeaderboardBrowser extends VBoxContainer

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")

@onready var status_label: Label = $Status
@onready var track_option: OptionButton = $Controls/Track
@onready var entry_list: ItemList = $Entries
@onready var details: RichTextLabel = $Details

var game_manager: GameManager
var client: LeaderboardClient
var boards: Array = []

func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	$Controls/Global.pressed.connect(_request.bind("global"))
	$Controls/AroundMe.pressed.connect(_request.bind("around_user"))
	$Controls/Friends.pressed.connect(_request.bind("friends"))
	$Controls/RetryPending.pressed.connect(_retry_pending)
	$ClearRejected.pressed.connect(_clear_rejected)
	call_deferred("_initialize_client")

func _initialize_client() -> void:
	if game_manager == null or game_manager.leaderboard_client == null:
		status_label.text = "Leaderboard client is unavailable."
		return
	client = game_manager.leaderboard_client
	client.submission_status_changed.connect(_show_submission_status)
	client.entries_received.connect(_show_entries)
	boards = TimeAttackRulesClass.manifest().get("boards", []).duplicate(true)
	track_option.clear()
	for value in boards:
		var board: Dictionary = value
		track_option.add_item(String(board.get("track_slug", "Track")).replace("-", " ").capitalize())
		track_option.set_item_metadata(track_option.item_count - 1, String(board.get("steam_name", "")))
	_show_submission_status(client.status())
	if !boards.is_empty():
		_request("global")

func _selected_board() -> String:
	if track_option.selected < 0:
		return ""
	return String(track_option.get_item_metadata(track_option.selected))

func _request(request_type: String) -> void:
	if client == null or _selected_board().is_empty():
		return
	entry_list.clear()
	entry_list.add_item("Loading...")
	client.request_entries(_selected_board(), request_type)

func _retry_pending() -> void:
	if client != null:
		client.retry_pending_now()

func _clear_rejected() -> void:
	if client != null:
		client.clear_rejected()

func _format_score(score_milliseconds: int) -> String:
	var minutes := int(score_milliseconds / 60000)
	var seconds := int(score_milliseconds / 1000) % 60
	var milliseconds := score_milliseconds % 1000
	return "%d:%02d.%03d" % [minutes, seconds, milliseconds]

func _show_entries(board_name: String, request_type: String, result: Dictionary) -> void:
	if board_name != _selected_board():
		return
	entry_list.clear()
	if !bool(result.get("success", false)):
		entry_list.add_item("Unavailable: %s" % String(result.get("message", "Steam leaderboard request failed.")))
		return
	var entries: Array = result.get("entries", [])
	for value in entries:
		var entry: Dictionary = value
		var name := String(entry.get("persona_name", "Steam %s" % String(entry.get("steam_id", ""))))
		entry_list.add_item("#%d   %s   %s" % [
			int(entry.get("global_rank", 0)),
			name,
			_format_score(int(entry.get("score", 0))),
		])
	if entries.is_empty():
		entry_list.add_item("No %s entries yet." % request_type.replace("_", " "))

func _show_submission_status(status: Dictionary) -> void:
	status_label.text = String(status.get("message", ""))
	var lines: Array[String] = [
		"[b]Pending submissions:[/b] %d" % int(status.get("pending_count", 0)),
		"[b]Rejected submissions:[/b] %d" % int(status.get("rejected_count", 0)),
	]
	var pending_entries: Array = status.get("pending", [])
	for value in pending_entries:
		var submission: Dictionary = value
		lines.append("Pending: %s — %s" % [
			String(submission.get("board_name", "")),
			_format_score(int(submission.get("score_milliseconds", 0))),
		])
	var rejected_entries: Array = status.get("rejected", [])
	for value in rejected_entries:
		var submission: Dictionary = value
		lines.append("[color=#ff6961]Rejected: %s — %s[/color]" % [
			String(submission.get("board_name", "")),
			String(submission.get("rejected_reason", "unknown reason")),
		])
	details.text = "\n".join(lines)
