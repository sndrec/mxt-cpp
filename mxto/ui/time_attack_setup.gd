class_name TimeAttackSetup extends Control

signal ranked_start_requested(context: Dictionary)
signal practice_requested
signal back_requested
signal official_vehicle_requested
signal leaderboard_requested(board_name: String)
signal watch_replay_requested(board_name: String, entry: MxtLeaderboardEntry)

const TimeAttackRulesClass = preload("res://leaderboards/time_attack_rules.gd")
const LeaderboardEligibilityClass = preload("res://leaderboards/leaderboard_eligibility.gd")
const LeaderboardEntryPresenterClass = preload("res://leaderboards/leaderboard_entry_presenter.gd")
const GhostSelectionClass = preload("res://time_attack/time_attack_ghost_selection.gd")

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
@onready var choose_ghosts_button: Button = $Center/Panel/Margin/Content/Secondary/ChooseGhosts
@onready var ghost_picker: TimeAttackGhostPicker = $GhostPicker

var game_manager: GameManager
var eligibility: Dictionary = {}
var board_name := ""
var personal_best_milliseconds := 0
var personal_global_rank := 0
var personal_entry: MxtLeaderboardEntry
var ghost_selection: TimeAttackGhostSelection
var ghost_selection_scope := ""


func initialize(in_game_manager: GameManager) -> void:
	game_manager = in_game_manager
	ghost_selection = GhostSelectionClass.new()
	ghost_selection.initialize(game_manager.leaderboard_replay_cache)
	ghost_selection.changed.connect(_on_ghost_selection_changed)
	ghost_picker.initialize(game_manager, ghost_selection)
	ghost_picker.closed.connect(func(): choose_ghosts_button.grab_focus())
	if game_manager.leaderboard_client != null:
		game_manager.leaderboard_client.entries_received.connect(_on_entries_received)
		game_manager.leaderboard_client.submission_status_changed.connect(_on_submission_status)
	ranked_button.pressed.connect(_start_ranked)
	practice_button.pressed.connect(func(): practice_requested.emit())
	official_button.pressed.connect(func(): official_vehicle_requested.emit())
	leaderboard_button.pressed.connect(func(): leaderboard_requested.emit(board_name))
	watch_replay_button.pressed.connect(func(): watch_replay_requested.emit(board_name, personal_entry))
	choose_ghosts_button.pressed.connect(func(): ghost_picker.open_for_track(ghost_selection_scope, board_name))
	$Center/Panel/Margin/Content/Secondary/Back.pressed.connect(func(): back_requested.emit())
	hide()


func open_for_current_selection() -> void:
	personal_best_milliseconds = 0
	personal_global_rank = 0
	personal_entry = null
	watch_replay_button.disabled = true
	var configuration := TimeAttackRulesClass.build_configuration()
	var track_evidence := game_manager.track_content_controller.build_track_content_evidence(
		[game_manager.track_selector.selected])
	var settings := game_manager.car_settings.get_player_settings()
	game_manager.vehicle_content_controller.apply_evidence(settings)
	eligibility = LeaderboardEligibilityClass.evaluate_start(game_manager, configuration, track_evidence, settings)
	var selected_track_index := game_manager.track_selector.selected
	var selected_track_digest := game_manager.track_content_controller.track_gameplay_digest_for_index(selected_track_index) \
		if selected_track_index >= 0 else ""
	# Submission eligibility follows the selected machine. Ghost selection follows
	# the selected ranked track, including during an unranked run.
	var board := TimeAttackRulesClass.board_for_track_digest(selected_track_digest)
	board_name = String(board.get("steam_name", ""))
	ghost_selection_scope = board_name if !board_name.is_empty() else "local:" + selected_track_digest
	ghost_selection.set_board(ghost_selection_scope)
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
	reason_label.text = "Eligible for verified leaderboard archival." if eligible else TimeAttackRulesClass.friendly_reason(String(eligibility.get("reason", "ineligible")))
	_update_start_buttons()
	official_button.visible = String(eligibility.get("reason", "")) == "unofficial_or_mismatched_vehicle"
	leaderboard_button.disabled = board_name.is_empty()
	choose_ghosts_button.disabled = selected_track_digest.is_empty()
	_update_ghost_button()
	rules_label.text = "[b]Ranked rules[/b]\n%s" % TimeAttackRulesClass.rules_description()
	personal_label.text = "Personal Best  ·  Loading…" if eligible else "Personal Best  ·  Not available"
	global_label.text = "Global Rank  ·  Loading…" if eligible else "Global Rank  ·  Not ranked"
	friends_label.hide()
	_on_submission_status(game_manager.leaderboard_client.status())
	game_manager.get_node("Control").visible = false
	show()
	if eligible and !board_name.is_empty():
		game_manager.leaderboard_client.request_entries(board_name, "around_user")
	if eligible:
		ranked_button.grab_focus()
	else:
		practice_button.grab_focus()


func refresh_after_vehicle_change() -> void:
	open_for_current_selection()


func _start_ranked() -> void:
	if !bool(eligibility.get("eligible", false)):
		return
	if ghost_selection != null and !ghost_selection.all_ready():
		return
	var context := {
		"eligibility": eligibility.duplicate(true),
		"board_name": board_name,
		"personal_best_milliseconds": personal_best_milliseconds,
		"personal_global_rank": personal_global_rank,
		"ghost_descriptors": ghost_selection.ready_descriptors() if ghost_selection != null else [],
	}
	hide()
	ranked_start_requested.emit(context)


func _on_entries_received(request_board: String, request_type: String, result: MxtLeaderboardQueryResult) -> void:
	if !visible or request_board != board_name:
		return
	if !result.is_ok():
		var message := result.message if !result.message.is_empty() else "Unavailable"
		if request_type == "around_user":
			personal_label.text = "Personal Best  ·  Unavailable"
			global_label.text = "Global Rank  ·  %s" % message
		return
	var local_steam_id := game_manager.steam_service.get_steam_id()
	if request_type == "around_user":
		for index in result.get_entry_count():
			var entry := result.get_entry(index)
			if entry == null or entry.steam_id != local_steam_id:
				continue
			personal_best_milliseconds = entry.score_milliseconds
			personal_global_rank = entry.rank
			personal_entry = LeaderboardEntryPresenterClass.decorate(game_manager, entry)
			watch_replay_button.disabled = !personal_entry.replay_available
			break
		personal_label.text = "Personal Best  ·  %s" % (_format_score(personal_best_milliseconds) if personal_best_milliseconds > 0 else "No verified time")
		global_label.text = "Global Rank  ·  #%d" % personal_global_rank if personal_global_rank > 0 else "Global Rank  ·  Unranked"


func _on_submission_status(status: Dictionary) -> void:
	service_label.text = "Submission service  ·  %s" % String(status.get("message", "Ready"))


func _on_ghost_selection_changed(_snapshot: Array) -> void:
	_update_ghost_button()
	_update_start_buttons()


func _update_ghost_button() -> void:
	var selected_count := ghost_selection.count() if ghost_selection != null else 0
	choose_ghosts_button.text = "Choose Ghosts (%d/4)" % selected_count


func _update_start_buttons() -> void:
	var blocked := ghost_selection != null and !ghost_selection.all_ready()
	ranked_button.disabled = !bool(eligibility.get("eligible", false)) or blocked
	practice_button.disabled = blocked


func _format_score(milliseconds: int) -> String:
	return "%d:%02d.%03d" % [int(milliseconds / 60000), int(milliseconds / 1000) % 60, milliseconds % 1000]
