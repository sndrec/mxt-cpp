class_name RaceResultsOverlay
extends Control

signal machine_setting_changed(accel_setting: float)

@onready var race_results_label: RichTextLabel = $Center/Panel/Margin/Content/Columns/RaceResultsPanel/Margin/Scroll/RaceResultsText
@onready var grand_prix_label: RichTextLabel = $Center/Panel/Margin/Content/Columns/GrandPrixPanel/Margin/Scroll/GrandPrixText
@onready var grand_prix_panel: PanelContainer = $Center/Panel/Margin/Content/Columns/GrandPrixPanel
@onready var countdown_label: Label = $CountdownLabel
@onready var next_race_panel: PanelContainer = $Center/Panel/Margin/Content/NextRacePanel
@onready var next_track_label: Label = $Center/Panel/Margin/Content/NextRacePanel/Margin/Row/NextTrackLabel
@onready var machine_setting_slider: HSlider = $Center/Panel/Margin/Content/NextRacePanel/Margin/Row/MachineSettingSlider
@onready var machine_setting_percent: Label = $Center/Panel/Margin/Content/NextRacePanel/Margin/Row/MachineSettingPercent

var _updating_machine_setting := false

func _ready() -> void:
	if machine_setting_slider != null:
		machine_setting_slider.value_changed.connect(_on_machine_setting_slider_changed)

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

func _on_machine_setting_slider_changed(value: float) -> void:
	if _updating_machine_setting:
		return
	if machine_setting_percent != null:
		machine_setting_percent.text = "%d%%" % roundi(value)
	machine_setting_changed.emit(clampf(value / 100.0, 0.0, 1.0))
