class_name RaceResultsOverlay
extends Control

signal machine_setting_changed(accel_setting: float)
signal time_attack_race_again_requested
signal time_attack_save_replay_requested
signal time_attack_watch_replay_requested
signal time_attack_leaderboard_requested(board_name: String)
signal time_attack_main_menu_requested

@onready var race_results_label: RichTextLabel = $Center/Panel/Margin/Content/Columns/RaceResultsPanel/Margin/Scroll/RaceResultsText
@onready var grand_prix_label: RichTextLabel = $Center/Panel/Margin/Content/Columns/GrandPrixPanel/Margin/Scroll/GrandPrixText
@onready var grand_prix_panel: PanelContainer = $Center/Panel/Margin/Content/Columns/GrandPrixPanel
@onready var countdown_label: Label = $CountdownLabel
@onready var next_race_panel: PanelContainer = $Center/Panel/Margin/Content/NextRacePanel
@onready var next_track_label: Label = $Center/Panel/Margin/Content/NextRacePanel/Margin/Row/NextTrackLabel
@onready var machine_setting_slider: HSlider = $Center/Panel/Margin/Content/NextRacePanel/Margin/Row/MachineSettingSlider
@onready var machine_setting_percent: Label = $Center/Panel/Margin/Content/NextRacePanel/Margin/Row/MachineSettingPercent
@onready var time_attack_panel: PanelContainer = $Center/Panel/Margin/Content/TimeAttackPanel
@onready var time_attack_result: RichTextLabel = $Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/Result
@onready var time_attack_status: Label = $Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/SubmissionStatus
@onready var save_replay_button: Button = $Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/Actions/SaveReplay
@onready var watch_replay_button: Button = $Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/Actions/WatchReplay
@onready var view_leaderboard_button: Button = $Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/Actions/ViewLeaderboard

var _updating_machine_setting := false
var _time_attack_board_name := ""

func _ready() -> void:
	if machine_setting_slider != null:
		machine_setting_slider.value_changed.connect(_on_machine_setting_slider_changed)
	$Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/Actions/RaceAgain.pressed.connect(func(): time_attack_race_again_requested.emit())
	save_replay_button.pressed.connect(func(): time_attack_save_replay_requested.emit())
	watch_replay_button.pressed.connect(func(): time_attack_watch_replay_requested.emit())
	view_leaderboard_button.pressed.connect(func(): time_attack_leaderboard_requested.emit(_time_attack_board_name))
	$Center/Panel/Margin/Content/TimeAttackPanel/Margin/Content/Actions/MainMenu.pressed.connect(func(): time_attack_main_menu_requested.emit())

func set_results(race_text: String, grand_prix_text: String) -> void:
	if race_results_label != null:
		race_results_label.text = race_text
	if grand_prix_label != null:
		grand_prix_label.text = grand_prix_text
	if grand_prix_panel != null:
		grand_prix_panel.visible = grand_prix_text.strip_edges() != ""

func set_countdown_seconds(seconds: int) -> void:
	if countdown_label == null:
		return
	countdown_label.visible = seconds >= 0
	if seconds >= 0:
		countdown_label.text = "%ds" % seconds

func set_next_race(next_track_name: String, accel_setting: float, show_machine_setting: bool) -> void:
	if next_race_panel != null:
		next_race_panel.visible = show_machine_setting
	if !show_machine_setting:
		return
	if next_track_label != null:
		next_track_label.text = "Next Track: " + next_track_name
	_updating_machine_setting = true
	if machine_setting_slider != null:
		machine_setting_slider.value = clampf(accel_setting, 0.0, 1.0) * 100.0
	if machine_setting_percent != null:
		machine_setting_percent.text = "%d%%" % roundi(clampf(accel_setting, 0.0, 1.0) * 100.0)
	_updating_machine_setting = false


func clear_time_attack_result() -> void:
	time_attack_panel.visible = false
	_time_attack_board_name = ""
	time_attack_result.text = ""
	time_attack_status.text = ""
	save_replay_button.disabled = true
	watch_replay_button.disabled = true
	view_leaderboard_button.disabled = true


func set_time_attack_result(result: Dictionary, previous_best_milliseconds: int) -> void:
	var score := int(result.get("score_milliseconds", 0))
	var lines := ["[b]Time Attack[/b]", "Finish  ·  %s" % _format_milliseconds(score)]
	if previous_best_milliseconds > 0:
		var delta := score - previous_best_milliseconds
		lines.append("Previous verified best  ·  %s" % _format_milliseconds(previous_best_milliseconds))
		lines.append("Delta  ·  %s%s" % ["+" if delta >= 0 else "−", _format_milliseconds(absi(delta))])
	else:
		lines.append("Previous verified best  ·  None")
	if bool(result.get("eligible", false)):
		lines.append("This is a provisional local result until trusted verification completes.")
	else:
		lines.append("Unranked  ·  %s" % String(result.get("friendly_reason", result.get("reason", "Practice run"))))
	time_attack_result.text = "\n".join(lines)
	_time_attack_board_name = String(result.get("board_name", ""))
	save_replay_button.disabled = !bool(result.get("replay_can_save", false))
	watch_replay_button.disabled = String(result.get("replay_path", "")).is_empty()
	view_leaderboard_button.disabled = _time_attack_board_name.is_empty()
	time_attack_panel.visible = true


func set_time_attack_submission_status(message: String) -> void:
	time_attack_status.text = message


func set_time_attack_replay_saved(path: String) -> void:
	save_replay_button.disabled = true
	watch_replay_button.disabled = path.is_empty()


func _format_milliseconds(milliseconds: int) -> String:
	return "%d:%02d.%03d" % [int(milliseconds / 60000), int(milliseconds / 1000) % 60, milliseconds % 1000]

func _on_machine_setting_slider_changed(value: float) -> void:
	if _updating_machine_setting:
		return
	if machine_setting_percent != null:
		machine_setting_percent.text = "%d%%" % roundi(value)
	machine_setting_changed.emit(clampf(value / 100.0, 0.0, 1.0))
