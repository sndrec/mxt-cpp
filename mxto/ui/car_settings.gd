extends Control

@onready var machine_setting_slider: HSlider = $Container/HBoxContainer/VBoxContainer/MachineSettingSlider
@onready var machine_setting_percent: Label = $Container/HBoxContainer/VBoxContainer/MachineSettingPercent
@onready var vehicle_selector: ItemList = $Container/ScrollContainer/VehicleSelector
@onready var close_settings: Button = $Container/CloseSettings
@onready var pilot_name_input: LineEdit = $Container/HBoxContainer/VBoxContainer/PilotNameInput
@onready var spectator_toggle: CheckBox = $Container/HBoxContainer/VBoxContainer/SpectatorToggle
@onready var car_name_label: Label = $Container/HBoxContainer/VBoxContainer/CarName
@onready var sticker_slot_1: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot1
@onready var sticker_slot_2: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot2
@onready var sticker_slot_3: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot3
@onready var sticker_slot_4: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot4

var game_manager: GameManager
var player_settings: PlayerSettings = PlayerSettings.new()
var car_defs: Array = []
var sticker_selection: StickerSelection = preload("res://ui/emote_sticker/sticker_selection.tres")
var sticker_selectors: Array[OptionButton] = []

func _ready() -> void:
	game_manager = get_parent() as GameManager
	_load_settings()
	_load_car_defs()
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

func _load_car_defs() -> void:
	if game_manager != null:
		car_defs = game_manager.car_definitions
	else:
		car_defs = []
	vehicle_selector.clear()
	for def in car_defs:
		vehicle_selector.add_item(def.name)

func _load_settings() -> void:
	var path := "user://player_settings.json"
	if FileAccess.file_exists(path):
		var data = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(data) == TYPE_DICTIONARY:
			player_settings.from_dict(data)

func _save_settings() -> void:
	var path := "user://player_settings.json"
	var file = FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(player_settings.to_dict()))
	file.close()
	if game_manager:
		game_manager.network_manager.send_player_settings(player_settings.to_dict())

func _populate_sticker_selectors() -> void:
	for selector in sticker_selectors:
		selector.clear()
		selector.expand_icon = true
		if sticker_selection == null:
			continue
		for i in range(sticker_selection.stickers.size()):
			selector.add_icon_item(sticker_selection.stickers[i], "", i)
			selector.get_popup().set_item_icon_max_width(i, 96)

func _update_controls() -> void:
	machine_setting_slider.value = player_settings.accel_setting * 100.0
	machine_setting_percent.text = str(roundi(machine_setting_slider.value)) + "%"
	pilot_name_input.text = player_settings.username
	spectator_toggle.button_pressed = player_settings.spectator
	_update_sticker_controls()
	var idx := 0
	for i in car_defs.size():
		if car_defs[i].resource_path == player_settings.car_definition_path:
			idx = i
			break
	if car_defs.size() > 0:
		vehicle_selector.select(idx)
		player_settings.car_definition_path = car_defs[idx].resource_path
		car_name_label.text = car_defs[idx].name

func _on_slider_changed(value: float) -> void:
	machine_setting_percent.text = str(roundi(value)) + "%"
	player_settings.accel_setting = value / 100.0

func _on_vehicle_selected(index: int) -> void:
	if index >= 0 and index < car_defs.size():
		player_settings.car_definition_path = car_defs[index].resource_path
		car_name_label.text = car_defs[index].name

func _on_name_changed(new_text: String) -> void:
	player_settings.username = new_text

func _on_spectator_toggled(toggled: bool) -> void:
	player_settings.spectator = toggled

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
	_save_settings()
	hide()

func open_settings() -> void:
	_load_settings()
	_load_car_defs()
	_update_controls()
	show()

func get_player_settings() -> PlayerSettings:
	return player_settings
