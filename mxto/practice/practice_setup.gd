class_name PracticeSetup
extends Control

signal start_requested(options: Dictionary, context: Dictionary)
signal back_requested

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")
const GhostSelectionClass = preload("res://time_attack/time_attack_ghost_selection.gd")
const PRACTICE_SESSION_KIND := "practice"

@onready var track_label: Label = $Center/Panel/Margin/Content/Track
@onready var vehicle_label: Label = $Center/Panel/Margin/Content/Vehicle
@onready var s_boost_toggle: CheckBox = $Center/Panel/Margin/Content/Options/SBoost
@onready var restore_toggle: CheckBox = $Center/Panel/Margin/Content/Options/Restore
@onready var bumpers_toggle: CheckBox = $Center/Panel/Margin/Content/Options/Bumpers
@onready var cpu_count: SpinBox = $Center/Panel/Margin/Content/Options/CpuCount
@onready var lap_count: SpinBox = $Center/Panel/Margin/Content/Options/LapCount
@onready var infinite_laps: CheckBox = $Center/Panel/Margin/Content/Options/InfiniteLaps
@onready var choose_ghosts_button: Button = $Center/Panel/Margin/Content/Ghosts/ChooseGhosts
@onready var start_button: Button = $Center/Panel/Margin/Content/Actions/Start
@onready var ghost_picker: TimeAttackGhostPicker = $GhostPicker

var game_manager: GameManager
var ghost_selection: TimeAttackGhostSelection
var ghost_selection_scope := ""


func initialize(in_game_manager: GameManager) -> void:
	game_manager = in_game_manager
	ghost_selection = GhostSelectionClass.new()
	ghost_selection.initialize(game_manager.leaderboard_replay_cache)
	ghost_selection.changed.connect(_on_ghost_selection_changed)
	ghost_picker.initialize(game_manager, ghost_selection)
	ghost_picker.closed.connect(func(): choose_ghosts_button.grab_focus())
	infinite_laps.toggled.connect(_on_infinite_laps_toggled)
	choose_ghosts_button.pressed.connect(
		func(): ghost_picker.open_for_track(ghost_selection_scope, _leaderboard_name_for_current_track()))
	start_button.pressed.connect(_start)
	$Center/Panel/Margin/Content/Actions/Back.pressed.connect(func(): back_requested.emit())
	hide()


func open_for_current_selection(default_cpu_count: int) -> void:
	var selected_track_index := game_manager.track_selector.selected
	var selected_track_digest := game_manager.track_content_controller.track_gameplay_digest_for_index(selected_track_index) \
		if selected_track_index >= 0 else ""
	var board_name := _leaderboard_name_for_current_track()
	ghost_selection_scope = board_name if !board_name.is_empty() else "local:" + selected_track_digest
	ghost_selection.set_board(ghost_selection_scope)
	var track_name := game_manager.track_selector.get_item_text(selected_track_index) \
		if selected_track_index >= 0 else "Unknown Track"
	var settings := game_manager.car_settings.get_player_settings()
	var definition: CarDefinition = game_manager.vehicle_content_controller.get_definition(settings.vehicle_content_id)
	track_label.text = "Track  ·  %s" % track_name
	vehicle_label.text = "Machine  ·  %s  ·  %d%% setting" % [
		definition.name if definition != null else settings.vehicle_content_id,
		roundi(settings.accel_setting * 100.0),
	]
	cpu_count.value = clampi(default_cpu_count, 0, 999)
	choose_ghosts_button.disabled = selected_track_digest.is_empty()
	_update_ghost_button()
	_update_start_button()
	game_manager.get_node("Control").visible = false
	show()
	start_button.grab_focus()


func _leaderboard_name_for_current_track() -> String:
	var selected_track_index := game_manager.track_selector.selected
	if selected_track_index < 0:
		return ""
	var digest := game_manager.track_content_controller.track_gameplay_digest_for_index(selected_track_index)
	return String(TimeAttackRulesClass.board_for_track_digest(digest).get("steam_name", ""))


func _start() -> void:
	if ghost_selection != null and !ghost_selection.all_ready():
		return
	var infinite := infinite_laps.button_pressed
	var options := {
		"game_mode": 0,
		"vehicle_restore": restore_toggle.button_pressed,
		"bumpers": bumpers_toggle.button_pressed,
		"s_boost": s_boost_toggle.button_pressed,
		"cpu_count": roundi(cpu_count.value),
		"lap_count": 0 if infinite else roundi(lap_count.value),
		"infinite_laps": infinite,
		"session_kind": PRACTICE_SESSION_KIND,
		"leaderboard_eligible": false,
		"leaderboard_ineligible_reason": "practice_unranked",
		"grand_prix_current_track": 0,
		"grand_prix_points": {},
		"grand_prix_ko_energy_bonuses": {},
		"grand_prix_eliminated_ids": [],
	}
	var context := {
		"ghost_descriptors": ghost_selection.ready_descriptors() if ghost_selection != null else [],
	}
	hide()
	start_requested.emit(options, context)


func _on_infinite_laps_toggled(enabled: bool) -> void:
	lap_count.editable = !enabled


func _on_ghost_selection_changed(_snapshot: Array) -> void:
	_update_ghost_button()
	_update_start_button()


func _update_ghost_button() -> void:
	var selected_count := ghost_selection.count() if ghost_selection != null else 0
	choose_ghosts_button.text = "Choose Ghosts (%d/4)" % selected_count


func _update_start_button() -> void:
	start_button.disabled = ghost_selection != null and !ghost_selection.all_ready()
