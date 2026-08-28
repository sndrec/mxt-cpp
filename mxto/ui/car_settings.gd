extends Control

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const VehicleGradePanelClass = preload("res://ui/vehicle_grade_panel.gd")

@onready var machine_setting_slider: HSlider = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/MachineSettingSlider
@onready var machine_setting_percent: Label = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/MachineSettingPercent
@onready var vehicle_selector: ItemList = $Container/SettingsTabs/Driver/VehicleScroll/VehicleSelector
@onready var vehicle_grade_panel: VehicleGradePanelClass = $Container/SettingsTabs/Driver/Performance
@onready var close_settings: Button = $Container/CloseSettings
@onready var pilot_name_input: LineEdit = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/PilotNameInput
@onready var spectator_toggle: CheckBox = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/SpectatorToggle
@onready var car_name_label: Label = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/CarName
@onready var sticker_slot_1: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot1
@onready var sticker_slot_2: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot2
@onready var sticker_slot_3: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot3
@onready var sticker_slot_4: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot4
@onready var settings_tab_container: TabContainer = $Container/SettingsTabs
@onready var driver_tab: VBoxContainer = $Container/SettingsTabs/Driver
@onready var track_package_editor_tab: Control = get_node("Container/SettingsTabs/Track Packages")
@onready var vehicle_editor_tab: ScrollContainer = get_node("Container/SettingsTabs/Car Creator")
@onready var vehicle_editor: VehicleEditor = get_node("Container/SettingsTabs/Car Creator/Editor")
@onready var leaderboard_browser: LeaderboardBrowser = $Container/SettingsTabs/Leaderboards
@onready var livery_editor: LiveryEditor = $LiveryEditor

var game_manager: GameManager
var vehicle_content_controller: VehicleContentControllerClass
var player_settings: PlayerSettings = PlayerSettings.new()
var car_defs: Array = []
var sticker_selection: StickerSelection = preload("res://ui/emote_sticker/sticker_selection.tres")
var sticker_selectors: Array[OptionButton] = []
var legacy_selected_car_definition_path := ""
var previous_settings_tab := 0
var restoring_settings_tab := false
var performance_analyzer := MxtCarPerformanceAnalyzer.new()

func _ready() -> void:
	game_manager = get_parent() as GameManager
	vehicle_content_controller = get_node("../VehicleContentController") as VehicleContentControllerClass
	vehicle_content_controller.garage_catalog_changed.connect(_on_garage_catalog_changed)
	settings_tab_container.set_tab_hidden(track_package_editor_tab.get_index(), true)
	livery_editor.initialize(game_manager, player_settings)
	livery_editor.livery_reference_changed.connect(_on_livery_reference_changed)
	_load_settings()
	_load_car_defs()
	livery_editor.refresh_custom_stamp_library()
	sticker_selectors = [sticker_slot_1, sticker_slot_2, sticker_slot_3, sticker_slot_4]
	_populate_sticker_selectors()
	_update_controls()
	machine_setting_slider.value_changed.connect(_on_slider_changed)
	vehicle_selector.item_selected.connect(_on_vehicle_selected)
	pilot_name_input.text_changed.connect(_on_name_changed)
	close_settings.pressed.connect(_on_close_pressed)
	spectator_toggle.toggled.connect(_on_spectator_toggled)
	for i in range(sticker_selectors.size()):
		sticker_selectors[i].item_selected.connect(_on_sticker_selected.bind(i))
	vehicle_editor.content_changed.connect(_on_vehicle_editor_content_changed)
	vehicle_editor.test_drive_requested.connect(_on_vehicle_editor_test_drive_requested)
	previous_settings_tab = settings_tab_container.current_tab
	settings_tab_container.tab_changed.connect(_on_settings_tab_changed)

func _on_livery_reference_changed(livery: CarLivery, enabled: bool) -> void:
	if !enabled or livery == null:
		player_settings.car_livery = {}
		return
	player_settings.set_car_livery(livery)


func _load_car_defs() -> void:
	if vehicle_content_controller != null:
		car_defs = vehicle_content_controller.get_garage_vehicle_definitions()
	else:
		car_defs = []
	vehicle_selector.clear()
	for def in car_defs:
		vehicle_selector.add_item(def.name)
	_update_vehicle_selector_availability()
	_migrate_legacy_vehicle_settings()

func _on_garage_catalog_changed() -> void:
	var previous_vehicle_evidence := _selected_vehicle_evidence_signature()
	_load_car_defs()
	_update_controls()
	if _selected_vehicle_evidence_signature() != previous_vehicle_evidence:
		_save_settings()

func _load_settings() -> void:
	legacy_selected_car_definition_path = ""
	var path := "user://player_settings.json"
	if FileAccess.file_exists(path):
		var data = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(data) == TYPE_DICTIONARY:
			player_settings.from_dict(data)
			legacy_selected_car_definition_path = str(data.get("car_definition_path", ""))

func _migrate_legacy_vehicle_settings() -> void:
	if car_defs.is_empty():
		return
	var selected_vehicle_migrated := false
	for def in car_defs:
		if !(def is CarDefinition) or def.content_id == "" or def.resource_path == "":
			continue
		var err := CarLiveryStore.migrate_legacy_for_car(def.content_id, def.resource_path)
		if err != OK:
			push_warning("Failed to migrate legacy livery for %s: %s" % [def.content_id, error_string(err)])
		if player_settings.vehicle_content_id == "" and def.resource_path == legacy_selected_car_definition_path:
			player_settings.vehicle_content_id = def.content_id
			selected_vehicle_migrated = true
	if selected_vehicle_migrated:
		var file := FileAccess.open("user://player_settings.json", FileAccess.WRITE)
		if file == null:
			push_warning("Failed to save migrated player settings: %s" % error_string(FileAccess.get_open_error()))
			return
		file.store_string(JSON.stringify(player_settings.to_dict()))
		file.close()

func _save_settings() -> void:
	livery_editor.flush_livery(false)
	_sync_vehicle_content_evidence()
	var path := "user://player_settings.json"
	var file = FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(player_settings.to_dict()))
	file.close()
	if game_manager:
		var settings_dict := player_settings.to_dict()
		if _network_settings_should_exclude_livery():
			settings_dict.erase("car_livery")
		game_manager.network_manager.lobby_settings.send_player_settings(settings_dict)

func _populate_sticker_selectors() -> void:
	for selector in sticker_selectors:
		selector.clear()
		selector.expand_icon = true
		if sticker_selection == null:
			continue
		for i in range(sticker_selection.stickers.size()):
			selector.add_icon_item(sticker_selection.stickers[i], "", i)
			selector.get_popup().set_item_icon_max_width(i, 96)

func _update_controls(rebuild_preview := true) -> void:
	machine_setting_slider.value = player_settings.accel_setting * 100.0
	machine_setting_percent.text = str(roundi(machine_setting_slider.value)) + "%"
	pilot_name_input.text = player_settings.username
	spectator_toggle.button_pressed = player_settings.spectator
	_update_sticker_controls()
	var idx := 0
	for i in car_defs.size():
		if car_defs[i].content_id == player_settings.vehicle_content_id:
			idx = i
			break
	if car_defs.size() > 0:
		vehicle_selector.select(idx)
		player_settings.vehicle_content_id = car_defs[idx].content_id
		_sync_vehicle_content_evidence()
		car_name_label.text = car_defs[idx].name
		livery_editor.load_vehicle(car_defs[idx], rebuild_preview)
	livery_editor.update_lock_state()
	_update_vehicle_selector_availability()
	_refresh_vehicle_grades()

func refresh_after_game_manager_loaded() -> void:
	_load_settings()
	var previous_vehicle_evidence := _selected_vehicle_evidence_signature()
	_load_car_defs()
	livery_editor.refresh_custom_stamp_library()
	_update_controls(false)
	livery_editor.publish_livery_reference()
	if _selected_vehicle_evidence_signature() != previous_vehicle_evidence:
		_save_settings()

func _selected_vehicle_evidence_signature() -> String:
	return "%s|%s|%s|%s" % [
		player_settings.vehicle_content_id,
		player_settings.vehicle_workshop_id,
		player_settings.vehicle_gameplay_digest,
		player_settings.vehicle_package_digest,
	]

func _on_vehicle_editor_content_changed() -> void:
	if game_manager == null:
		return
	performance_analyzer = MxtCarPerformanceAnalyzer.new()
	vehicle_content_controller.refresh_installed_content()
	_load_car_defs()
	_update_controls()

func _on_vehicle_editor_test_drive_requested(snapshot: MxtContentLoadResult) -> void:
	if game_manager != null:
		game_manager.begin_vehicle_test_drive(snapshot)

func _on_slider_changed(value: float) -> void:
	machine_setting_percent.text = str(roundi(value)) + "%"
	player_settings.accel_setting = value / 100.0
	_refresh_vehicle_grades()

func _on_vehicle_selected(index: int) -> void:
	if index >= 0 and index < car_defs.size():
		if !_vehicle_definition_selectable(car_defs[index]):
			_restore_vehicle_selector_selection()
			return
		player_settings.vehicle_content_id = car_defs[index].content_id
		_sync_vehicle_content_evidence()
		car_name_label.text = car_defs[index].name
		livery_editor.load_vehicle(car_defs[index])
		_send_online_vehicle_selection_update()
		_refresh_vehicle_grades()


func _refresh_vehicle_grades() -> void:
	if vehicle_grade_panel == null:
		return
	var definition := _selected_car_definition()
	if definition == null or definition.properties_path.is_empty():
		vehicle_grade_panel.show_analysis({"valid": false, "error": "Selected machine has no performance data."})
		return
	vehicle_grade_panel.show_analysis(performance_analyzer.analyze_file(
		definition.properties_path,
		player_settings.accel_setting))

func _on_name_changed(new_text: String) -> void:
	player_settings.username = new_text

func _sync_vehicle_content_evidence() -> void:
	if game_manager != null:
		vehicle_content_controller.apply_evidence(player_settings)

func _on_spectator_toggled(toggled: bool) -> void:
	player_settings.spectator = toggled

func set_spectator_enabled(enabled: bool) -> void:
	if player_settings.spectator == enabled:
		spectator_toggle.set_pressed_no_signal(enabled)
		return
	player_settings.spectator = enabled
	spectator_toggle.set_pressed_no_signal(enabled)
	_save_settings()

func _sticker_slot_value(slot: int) -> int:
	match slot:
		0:
			return player_settings.sticker_1
		1:
			return player_settings.sticker_2
		2:
			return player_settings.sticker_3
		3:
			return player_settings.sticker_4
	return 0

func _set_sticker_slot_value(slot: int, value: int) -> void:
	match slot:
		0:
			player_settings.sticker_1 = value
		1:
			player_settings.sticker_2 = value
		2:
			player_settings.sticker_3 = value
		3:
			player_settings.sticker_4 = value

func _update_sticker_controls() -> void:
	var count := 0
	if sticker_selection != null:
		count = sticker_selection.stickers.size()
	for i in range(sticker_selectors.size()):
		if count > 0:
			var sticker_index := wrapi(_sticker_slot_value(i), 0, count)
			sticker_selectors[i].select(sticker_index)
			sticker_selectors[i].icon = sticker_selection.stickers[sticker_index]

func _on_sticker_selected(index: int, slot: int) -> void:
	_set_sticker_slot_value(slot, index)

func _on_close_pressed() -> void:
	if settings_tab_container.current_tab == vehicle_editor_tab.get_index() and !vehicle_editor.flush_pending_changes():
		return
	_save_settings()
	livery_editor.set_active(false)
	hide()

func open_settings() -> void:
	_load_settings()
	_load_car_defs()
	livery_editor.refresh_custom_stamp_library()
	_update_controls()
	livery_editor.update_lock_state()
	_update_vehicle_selector_availability()
	show()
	livery_editor.set_active(true)


func _on_settings_tab_changed(tab: int) -> void:
	if restoring_settings_tab:
		restoring_settings_tab = false
		previous_settings_tab = tab
		return
	if previous_settings_tab == vehicle_editor_tab.get_index() and tab != previous_settings_tab \
			and !vehicle_editor.flush_pending_changes():
		restoring_settings_tab = true
		settings_tab_container.current_tab = previous_settings_tab
		return
	previous_settings_tab = tab

func get_player_settings() -> PlayerSettings:
	livery_editor.publish_livery_reference()
	return player_settings


func select_ranked_default_vehicle() -> bool:
	const RANKED_DEFAULT_ID := "mxt:vehicle:official:allrounder"
	for definition_value in car_defs:
		var definition: CarDefinition = definition_value
		if definition != null and definition.content_id == RANKED_DEFAULT_ID:
			player_settings.vehicle_content_id = definition.content_id
			_update_controls()
			_send_online_vehicle_selection_update()
			return true
	return false


func apply_authoritative_vehicle_selection(settings: Dictionary) -> void:
	if player_settings == null:
		return
	var content_id := String(settings.get("vehicle_content_id", ""))
	if content_id.is_empty() or content_id == player_settings.vehicle_content_id:
		return
	player_settings.vehicle_content_id = content_id
	player_settings.vehicle_gameplay_digest = String(settings.get("vehicle_gameplay_digest", ""))
	player_settings.vehicle_package_digest = String(settings.get("vehicle_package_digest", ""))
	player_settings.vehicle_workshop_id = String(settings.get("vehicle_workshop_id", ""))
	player_settings.car_livery = {}
	_update_controls()


func open_leaderboards(board_name := "") -> void:
	open_settings()
	settings_tab_container.current_tab = leaderboard_browser.get_index()
	leaderboard_browser.select_board(board_name)

func set_test_drive_vehicle(content_id: String) -> void:
	player_settings.vehicle_content_id = content_id
	player_settings.car_livery = {}
	_sync_vehicle_content_evidence()
	livery_editor.set_active(false)
	hide()

func restore_after_test_drive(saved_settings: Dictionary) -> void:
	player_settings.from_dict(saved_settings)
	_load_car_defs()
	_update_controls()
	settings_tab_container.current_tab = vehicle_editor_tab.get_index()
	show()
	livery_editor.set_active(true)


func _network_settings_should_exclude_livery() -> bool:
	return game_manager != null and !game_manager.singleplayer_mode and game_manager.network_manager != null and game_manager.network_manager.has_network_peer()

func _send_online_vehicle_selection_update() -> void:
	if !_network_settings_should_exclude_livery():
		return
	var settings_dict := player_settings.to_dict()
	game_manager.network_manager.lobby_settings.send_player_settings(settings_dict)
	game_manager.network_manager.custom_stamp_network.send_active_custom_stamp_manifest()

func _update_vehicle_selector_availability() -> void:
	if vehicle_selector == null:
		return
	for index in range(mini(vehicle_selector.get_item_count(), car_defs.size())):
		var selectable := _vehicle_definition_selectable(car_defs[index])
		vehicle_selector.set_item_disabled(index, !selectable)
		vehicle_selector.set_item_tooltip(
			index,
			"" if selectable else "Local and draft vehicles cannot be selected in multiplayer. Publish and use the Workshop version instead.")

func _vehicle_definition_selectable(definition: CarDefinition) -> bool:
	if definition == null:
		return false
	if game_manager == null or game_manager.network_manager == null or !game_manager.network_manager.has_network_peer():
		return true
	return vehicle_content_controller.is_multiplayer_vehicle_content(
		definition.content_id,
		game_manager.network_manager.race_configuration.allow_workshop_vehicles)

func _restore_vehicle_selector_selection() -> void:
	for index in range(car_defs.size()):
		if car_defs[index].content_id == player_settings.vehicle_content_id:
			vehicle_selector.select(index)
			return

func _selected_car_definition() -> CarDefinition:
	for definition_value in car_defs:
		var definition: CarDefinition = definition_value
		if definition != null and definition.content_id == player_settings.vehicle_content_id:
			return definition
	return car_defs[0] if !car_defs.is_empty() else null

func _process(_delta: float) -> void:
	if visible:
		_update_vehicle_selector_availability()
