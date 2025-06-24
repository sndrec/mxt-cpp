extends Control

@onready var machine_setting_slider: HSlider = $SettingContainer/MachineSettingSlider
@onready var machine_setting_percent: Label = $SettingContainer/MachineSettingPercent
@onready var vehicle_selector: ItemList = $VehicleSelector
@onready var close_settings: Button = $CloseSettings
@onready var pilot_name_input: LineEdit = $PilotNameInput
