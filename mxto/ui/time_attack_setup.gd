class_name TimeAttackSetup extends Control

signal start_requested(ranked: bool, context: Dictionary)
signal back_requested
signal official_vehicle_requested
signal leaderboard_requested(board_name: String)
signal watch_replay_requested(board_name: String, entry: Dictionary)

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")
const LeaderboardEligibilityClass = preload("res://steam/leaderboard_eligibility.gd")
const LeaderboardDetailsClass = preload("res://steam/leaderboard_details.gd")

@onready var track_label: Label = $Center/Panel/Margin/Content/Track
@onready var vehicle_label: Label = $Center/Panel/Margin/Content/Vehicle
@onready var ranked_label: Label = $Center/Panel/Margin/Content/RankedStatus
@onready var reason_label: Label = $Center/Panel/Margin/Content/Reason
@onready var rules_label: RichTextLabel = $Center/Panel/Margin/Content/Rules
@onready var personal_label: Label = $Center/Panel/Margin/Content/Competitive/Personal
@onready var global_label: Label = $Center/Panel/Margin/Content/Competitive/Global
@onready var friends_label: Label = $Center/Panel/Margin/Content/Competitive/Friends
@onready var service_label: Label = $Center/Panel/Margin/Content/Service
@onready var ranked_button: Button = $Center/Panel/Margin/Content/Actions/Ranked
@onready var practice_button: Button = $Center/Panel/Margin/Content/Actions/Practice
@onready var official_button: Button = $Center/Panel/Margin/Content/Actions/Official
@onready var leaderboard_button: Button = $Center/Panel/Margin/Content/Secondary/ViewLeaderboard
@onready var watch_replay_button: Button = $Center/Panel/Margin/Content/Secondary/WatchReplay

var game_manager: GameManager
var eligibility: Dictionary = {}
var board_name := ""
var personal_best_milliseconds := 0
var personal_global_rank := 0
var friend_position := 0
var friend_count := 0
var personal_entry: Dictionary = {}


func initialize(in_game_manager: GameManager) -> void:
	game_manager = in_game_manager
	if game_manager.leaderboard_client != null:
		game_manager.leaderboard_client.entries_received.connect(_on_entries_received)
		game_manager.leaderboard_client.submission_status_changed.connect(_on_submission_status)
	ranked_button.pressed.connect(_start.bind(true))
	practice_button.pressed.connect(_start.bind(false))
	official_button.pressed.connect(func(): official_vehicle_requested.emit())
	leaderboard_button.pressed.connect(func(): leaderboard_requested.emit(board_name))
	watch_replay_button.pressed.connect(func(): watch_replay_requested.emit(board_name, personal_entry.duplicate(true)))
	$Center/Panel/Margin/Content/Secondary/Back.pressed.connect(func(): back_requested.emit())
	hide()


func open_for_current_selection() -> void:
	personal_best_milliseconds = 0
	personal_global_rank = 0
	friend_position = 0
	friend_count = 0
	personal_entry.clear()
	watch_replay_button.disabled = true
	var options := TimeAttackRulesClass.build_options()
	game_manager.track_content_controller.set_track_content_evidence(
		options, [game_manager.track_selector.selected])
	var settings := game_manager.car_settings.get_player_settings()
	game_manager.vehicle_content_controller.apply_evidence(settings)
	eligibility = LeaderboardEligibilityClass.evaluate_start(game_manager, options, settings)
	var board: Dictionary = eligibility.get("board", {})
	board_name = String(board.get("steam_name", ""))
	var track_name := "Unknown Track"
	if game_manager.track_selector.selected >= 0:
		track_name = game_manager.track_selector.get_item_text(game_manager.track_selector.selected)
	var definition: CarDefinition = game_manager.vehicle_content_controller.get_definition(settings.vehicle_content_id)
	track_label.text = "Track  ·  %s" % track_name
	vehicle_label.text = "Machine  ·  %s  ·  %d%% setting" % [
		definition.name if definition != null else settings.vehicle_content_id,
		roundi(settings.accel_setting * 100.0),
	]
	var eligible := bool(eligibility.get("eligible", false))
	ranked_label.text = "RANKED — VERIFIED REPLAY REQUIRED" if eligible else "UNRANKED WITH CURRENT SELECTION"
	ranked_label.modulate = Color(0.42, 1.0, 0.58) if eligible else Color(1.0, 0.5, 0.38)
	reason_label.text = "Eligible for trusted Steam submission." if eligible else TimeAttackRulesClass.friendly_reason(String(eligibility.get("reason", "ineligible")))
	ranked_button.disabled = !eligible
	official_button.visible = String(eligibility.get("reason", "")) == "unofficial_or_mismatched_vehicle"
	leaderboard_button.disabled = board_name.is_empty()
	rules_label.text = "[b]Ranked rules[/b]\n%s" % TimeAttackRulesClass.rules_description()
	personal_label.text = "Personal Best  ·  Loading…" if eligible else "Personal Best  ·  Not available"
	global_label.text = "Global Rank  ·  Loading…" if eligible else "Global Rank  ·  Not ranked"
	friends_label.text = "Friend Rank  ·  Loading…" if eligible else "Friend Rank  ·  Not ranked"
	_on_submission_status(game_manager.leaderboard_client.status())
	game_manager.get_node("Control").visible = false
	show()
	if eligible and !board_name.is_empty():
		game_manager.leaderboard_client.request_entries(board_name, "around_user")
		game_manager.leaderboard_client.request_entries(board_name, "friends")
	if eligible:
		ranked_button.grab_focus()
	else:
		practice_button.grab_focus()


func refresh_after_vehicle_change() -> void:
	open_for_current_selection()


func _start(ranked: bool) -> void:
	if ranked and !bool(eligibility.get("eligible", false)):
		return
	var context := {
		"eligibility": eligibility.duplicate(true),
		"board_name": board_name,
		"personal_best_milliseconds": personal_best_milliseconds,
		"personal_global_rank": personal_global_rank,
	}
	hide()
	start_requested.emit(ranked, context)


func _on_entries_received(request_board: String, request_type: String, result: Dictionary) -> void:
	if !visible or request_board != board_name:
		return
	if !bool(result.get("success", false)):
		var message := String(result.get("message", "Unavailable"))
		if request_type == "around_user":
			personal_label.text = "Personal Best  ·  Unavailable"
			global_label.text = "Global Rank  ·  %s" % message
		elif request_type == "friends":
			friends_label.text = "Friend Rank  ·  %s" % message
		return
	var entries: Array = result.get("entries", [])
	var local_steam_id := game_manager.steam_service.get_steam_id()
	if request_type == "around_user":
		for entry_value in entries:
			var entry: Dictionary = entry_value
			if int(entry.get("steam_id", 0)) != local_steam_id:
				continue
			personal_best_milliseconds = int(entry.get("score", 0))
			personal_global_rank = int(entry.get("global_rank", 0))
			personal_entry = entry.duplicate(true)
			personal_entry["_trusted_details"] = LeaderboardDetailsClass.decode(entry.get("details", []))
			var ugc_handle := int(entry.get("ugc_handle", 0))
			watch_replay_button.disabled = ugc_handle == 0 or ugc_handle == -1 \
				or String((personal_entry["_trusted_details"] as Dictionary).get("replay_sha256", "")).is_empty()
			break
		personal_label.text = "Personal Best  ·  %s" % (_format_score(personal_best_milliseconds) if personal_best_milliseconds > 0 else "No verified time")
		global_label.text = "Global Rank  ·  #%d" % personal_global_rank if personal_global_rank > 0 else "Global Rank  ·  Unranked"
	elif request_type == "friends":
		friend_count = entries.size()
		friend_position = 0
		var position := 0
		for entry_value in entries:
			position += 1
			if int((entry_value as Dictionary).get("steam_id", 0)) == local_steam_id:
				friend_position = position
				break
		friends_label.text = "Friend Rank  ·  %s" % ("#%d of %d" % [friend_position, friend_count] if friend_position > 0 else "No verified time")


func _on_submission_status(status: Dictionary) -> void:
	service_label.text = "Submission service  ·  %s" % String(status.get("message", "Ready"))


func _format_score(milliseconds: int) -> String:
	return "%d:%02d.%03d" % [int(milliseconds / 60000), int(milliseconds / 1000) % 60, milliseconds % 1000]
