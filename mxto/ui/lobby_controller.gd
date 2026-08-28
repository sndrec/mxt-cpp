class_name LobbyController
extends Node

signal start_race_requested(configuration: MxtRaceConfiguration, track_evidence: MxtTrackContentEvidence, race_state: MxtRaceSessionState)
signal car_settings_requested
signal controller_settings_requested
signal spectator_toggled(enabled: bool)

const TrackContentControllerClass = preload("res://track/track_content_controller.gd")
const LobbyChibiControllerClass = preload("res://ui/lobby_chibi_controller.gd")

@onready var lobby_control: Control = $"../Lobby"
@onready var start_race_button: Button = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/StartRaceButton"
@onready var car_settings_button: Button = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CarSettingsButton"
@onready var controller_settings_button: Button = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/ControllerSettingsButton"
@onready var cpu_count_label: Label = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CpuControlBox/CpuCountLabel"
@onready var add_cpu_button: Button = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CpuControlBox/AddCpuButton"
@onready var remove_cpu_button: Button = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/CpuControlBox/RemoveCpuButton"
@onready var game_mode_choice: OptionButton = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/GameModeChoice"
@onready var vehicle_restore_toggle: CheckBox = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/VehicleRestoreToggle"
@onready var bumpers_toggle: CheckBox = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/BumpersToggle"
@onready var s_boost_toggle: CheckBox = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/SBoostToggle"
@onready var workshop_vehicles_toggle: CheckBox = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/WorkshopVehiclesToggle"
@onready var spectator_toggle: CheckBox = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/OptionsColumn/OptionsScroll/OptionsBox/SpectatorToggle"
@onready var stage_button_container: VBoxContainer = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/StageBox/StageScroll/StageButtonContainer"
@onready var stage_preview_container: VBoxContainer = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/StageBox/PreviewScroll/StagePreviewContainer"
@onready var player_list_container: VBoxContainer = $"../Lobby/LobbyStatic/LobbyContainer/Container/TopBox/PlayerScroll/PlayerListContainer"

var network_manager: NetworkManager
var track_content_controller: TrackContentControllerClass
var lobby_chibi_controller: LobbyChibiControllerClass
var grand_prix_track_sequence: Array[int] = []
var selected_track_index := -1
var applying_race_setup := false
var player_list_signature := ""

func initialize(
	in_network_manager: NetworkManager,
	in_track_content_controller: TrackContentControllerClass,
	in_lobby_chibi_controller: LobbyChibiControllerClass
) -> void:
	network_manager = in_network_manager
	track_content_controller = in_track_content_controller
	lobby_chibi_controller = in_lobby_chibi_controller
	game_mode_choice.item_selected.connect(refresh_race_setup.unbind(1))
	vehicle_restore_toggle.toggled.connect(refresh_race_setup.unbind(1))
	bumpers_toggle.toggled.connect(refresh_race_setup.unbind(1))
	s_boost_toggle.toggled.connect(refresh_race_setup.unbind(1))
	workshop_vehicles_toggle.toggled.connect(_on_workshop_vehicles_toggled)
	spectator_toggle.toggled.connect(func(enabled: bool): spectator_toggled.emit(enabled))
	add_cpu_button.pressed.connect(_on_add_cpu_pressed)
	remove_cpu_button.pressed.connect(_on_remove_cpu_pressed)
	start_race_button.pressed.connect(request_start_race)
	car_settings_button.pressed.connect(func(): car_settings_requested.emit())
	controller_settings_button.pressed.connect(func(): controller_settings_requested.emit())
	network_manager.race_setup_changed.connect(apply_race_setup)
	refresh_controls()

func reload_tracks(command_line_track_index := -1) -> void:
	grand_prix_track_sequence.clear()
	selected_track_index = -1
	if !track_content_controller.tracks.is_empty():
		selected_track_index = command_line_track_index if command_line_track_index >= 0 else 0
		grand_prix_track_sequence.append(selected_track_index)
	_populate_stage_buttons()
	refresh_race_setup()

func build_race_configuration() -> MxtRaceConfiguration:
	var configuration := MxtRaceConfiguration.new()
	configuration.game_mode = game_mode_choice.selected
	configuration.vehicle_restore = vehicle_restore_toggle.button_pressed
	configuration.bumpers = bumpers_toggle.button_pressed
	configuration.s_boost = s_boost_toggle.button_pressed
	configuration.allow_workshop_vehicles = workshop_vehicles_toggle.button_pressed
	return configuration

func build_race_state() -> MxtRaceSessionState:
	return MxtRaceSessionState.new()

func build_track_evidence() -> MxtTrackContentEvidence:
	return track_content_controller.build_track_content_evidence(grand_prix_track_sequence)

func process_lobby(delta: float) -> void:
	var lobby_frame_start_usec := Time.get_ticks_usec()
	network_manager.lobby_settings.process_latency(network_manager.waiting_peers)
	var player_list_start_usec := Time.get_ticks_usec()
	_update_player_list()
	var player_list_usec := Time.get_ticks_usec() - player_list_start_usec
	var chibi_start_usec := Time.get_ticks_usec()
	lobby_chibi_controller.process_lobby(delta)
	var chibi_usec := Time.get_ticks_usec() - chibi_start_usec
	refresh_controls()
	network_manager.telemetry.record_lobby_frame(
		Time.get_ticks_usec() - lobby_frame_start_usec,
		player_list_usec,
		chibi_usec)

func clear() -> void:
	player_list_signature = ""
	lobby_chibi_controller.clear()

func refresh_controls() -> void:
	if network_manager == null:
		return
	var can_edit := network_manager.is_server and !network_manager.race_active
	cpu_count_label.text = "CPU Drivers: %d" % network_manager.lobby_settings.get_cpu_roster().size()
	add_cpu_button.disabled = !can_edit
	remove_cpu_button.disabled = !can_edit or network_manager.lobby_settings.get_cpu_roster().is_empty()
	start_race_button.disabled = !can_edit or track_content_controller.tracks.is_empty() or grand_prix_track_sequence.is_empty()
	game_mode_choice.disabled = !can_edit
	vehicle_restore_toggle.disabled = !can_edit
	bumpers_toggle.disabled = !can_edit
	s_boost_toggle.disabled = !can_edit
	workshop_vehicles_toggle.disabled = !can_edit
	var local_settings: Dictionary = network_manager.lobby_settings.get_player_settings(multiplayer.get_unique_id())
	var is_spectator := typeof(local_settings) == TYPE_DICTIONARY and bool(local_settings.get("spectator", false))
	spectator_toggle.set_pressed_no_signal(is_spectator)
	spectator_toggle.disabled = network_manager.race_active
	for child in stage_button_container.get_children():
		var button := child as Button
		if button != null:
			button.disabled = !can_edit
	for child in stage_preview_container.get_children():
		var button := child as Button
		if button != null:
			button.disabled = !can_edit

func apply_race_setup(configuration: MxtRaceConfiguration, track_evidence: MxtTrackContentEvidence, _state: MxtRaceSessionState) -> void:
	applying_race_setup = true
	var mode := configuration.game_mode
	if mode >= 0 and mode < game_mode_choice.item_count:
		game_mode_choice.select(mode)
	vehicle_restore_toggle.set_pressed_no_signal(configuration.vehicle_restore)
	bumpers_toggle.set_pressed_no_signal(configuration.bumpers)
	s_boost_toggle.set_pressed_no_signal(configuration.s_boost)
	workshop_vehicles_toggle.set_pressed_no_signal(configuration.allow_workshop_vehicles)
	grand_prix_track_sequence.clear()
	for track_evidence_index in range(track_evidence.count()):
		var index := track_content_controller.track_index_for_id(track_evidence.get_content_id(track_evidence_index))
		if index >= 0 and index < track_content_controller.tracks.size():
			grand_prix_track_sequence.append(index)
	if !grand_prix_track_sequence.is_empty():
		selected_track_index = grand_prix_track_sequence[0]
	applying_race_setup = false
	_refresh_stage_preview()
	refresh_controls()

func refresh_race_setup() -> void:
	if applying_race_setup:
		_refresh_stage_preview()
		refresh_controls()
		return
	var configuration := build_race_configuration()
	var track_evidence := build_track_evidence()
	var state := build_race_state()
	if network_manager.is_server:
		network_manager.send_race_state(configuration, track_evidence, state)
	else:
		network_manager.race_configuration = configuration
		network_manager.race_track_evidence = track_evidence
		network_manager.race_state = state
	_refresh_stage_preview()
	refresh_controls()

func _populate_stage_buttons() -> void:
	for child in stage_button_container.get_children():
		child.queue_free()
	for i in range(track_content_controller.tracks.size()):
		var button := Button.new()
		button.text = str(track_content_controller.tracks[i].get("name", "Track"))
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(_on_stage_button_pressed.bind(i))
		stage_button_container.add_child(button)

func _on_stage_button_pressed(track_index: int) -> void:
	if !network_manager.is_server or track_index < 0 or track_index >= track_content_controller.tracks.size():
		return
	selected_track_index = track_index
	grand_prix_track_sequence.append(track_index)
	refresh_race_setup()

func _on_stage_preview_pressed(sequence_index: int) -> void:
	if !network_manager.is_server or sequence_index < 0 or sequence_index >= grand_prix_track_sequence.size():
		return
	grand_prix_track_sequence.remove_at(sequence_index)
	refresh_race_setup()

func _refresh_stage_preview() -> void:
	for child in stage_preview_container.get_children():
		child.queue_free()
	for i in range(network_manager.race_track_evidence.count()):
		var track_index := track_content_controller.track_index_for_id(
			network_manager.race_track_evidence.get_content_id(i))
		var button := Button.new()
		button.disabled = !network_manager.is_server
		button.text = "%d. %s" % [
			i + 1,
			str(track_content_controller.tracks[track_index].get("name", "Track")) if track_index >= 0 and track_index < track_content_controller.tracks.size() else "Missing Track",
		]
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(_on_stage_preview_pressed.bind(i))
		stage_preview_container.add_child(button)

func _on_add_cpu_pressed() -> void:
	if network_manager.is_server:
		network_manager.lobby_settings.add_cpu_driver()

func _on_remove_cpu_pressed() -> void:
	if network_manager.is_server:
		network_manager.lobby_settings.remove_cpu_driver()

func _on_workshop_vehicles_toggled(enabled: bool) -> void:
	refresh_race_setup()
	if network_manager.is_server and !enabled:
		network_manager.lobby_settings.enforce_official_vehicles()

func request_start_race() -> void:
	if network_manager.is_server and !grand_prix_track_sequence.is_empty():
		if !workshop_vehicles_toggle.button_pressed:
			network_manager.lobby_settings.enforce_official_vehicles()
		start_race_requested.emit(build_race_configuration(), build_track_evidence(), build_race_state())

func _update_player_list() -> void:
	var roster := network_manager.player_ids.duplicate(true)
	for spectator_id in network_manager.spectator_ids:
		if !roster.has(spectator_id):
			roster.append(spectator_id)
	var cpu_ids := network_manager.lobby_settings.get_cpu_roster()
	for cpu_id in cpu_ids:
		if !roster.has(cpu_id):
			roster.append(cpu_id)
	var signature_parts := []
	for id in roster:
		signature_parts.append("%d:%s:%s:%s:%s" % [int(id), _player_display_name(int(id)), str(cpu_ids.has(id)), str(network_manager.spectator_ids.has(id)), str(network_manager.is_server)])
	var signature := "|".join(signature_parts)
	if signature == player_list_signature:
		return
	player_list_signature = signature
	for child in player_list_container.get_children():
		child.queue_free()
	for id in roster:
		var player_id := int(id)
		var row := HBoxContainer.new()
		row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_theme_constant_override("separation", 6)
		player_list_container.add_child(row)
		var name_label := Label.new()
		name_label.text = _player_display_name(player_id)
		name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(name_label)
		if network_manager.is_server and !cpu_ids.has(id) and player_id != multiplayer.get_unique_id():
			var kick_button := Button.new()
			kick_button.text = "Kick"
			kick_button.pressed.connect(_on_kick_player_pressed.bind(player_id))
			row.add_child(kick_button)

func _on_kick_player_pressed(player_id: int) -> void:
	network_manager.kick_human_player(player_id)
	player_list_signature = ""
	_update_player_list()

func _player_display_name(player_id: int) -> String:
	var player_name := str(player_id)
	var settings: Dictionary = network_manager.lobby_settings.get_player_settings(player_id)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		player_name = str(settings["username"])
	if network_manager.lobby_settings.get_cpu_roster().has(player_id):
		player_name = "[CPU] " + player_name
	elif network_manager.spectator_ids.has(player_id):
		player_name = "[Spectator] " + player_name
	return player_name
