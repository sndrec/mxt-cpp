extends Control

@onready var machine_setting_slider: HSlider = $SettingContainer/MachineSettingSlider
@onready var machine_setting_percent: Label = $SettingContainer/MachineSettingPercent
@onready var vehicle_selector: ItemList = $VehicleSelector
@onready var close_settings: Button = $CloseSettings
@onready var pilot_name_input: LineEdit = $PilotNameInput

var game_manager: GameManager
var player_settings: PlayerSettings = PlayerSettings.new()
var car_defs: Array = []

func _ready() -> void:
	game_manager = get_parent() as GameManager
	_load_settings()
	_load_car_defs()
	_update_controls()
	machine_setting_slider.value_changed.connect(_on_slider_changed)
	vehicle_selector.item_selected.connect(_on_vehicle_selected)
	pilot_name_input.text_changed.connect(_on_name_changed)
	close_settings.pressed.connect(_on_close_pressed)

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

func _update_controls() -> void:
	machine_setting_slider.value = player_settings.accel_setting * 100.0
	machine_setting_percent.text = str(roundi(machine_setting_slider.value)) + "%"
	pilot_name_input.text = player_settings.username
	var idx := 0
	for i in car_defs.size():
		if car_defs[i].resource_path == player_settings.car_definition_path:
			idx = i
			break
	if car_defs.size() > 0:
		vehicle_selector.select(idx)
		player_settings.car_definition_path = car_defs[idx].resource_path

func _on_slider_changed(value: float) -> void:
	machine_setting_percent.text = str(roundi(value)) + "%"
	player_settings.accel_setting = value / 100.0

func _on_vehicle_selected(index: int) -> void:
	if index >= 0 and index < car_defs.size():
		player_settings.car_definition_path = car_defs[index].resource_path

func _on_name_changed(new_text: String) -> void:
	player_settings.username = new_text

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
