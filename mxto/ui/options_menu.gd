class_name OptionsMenu
extends Control

const VOICE_SETTINGS_PATH := "user://voice_chat_settings.json"
const AUDIO_SETTINGS_PATH := "user://audio_settings.json"
const AUDIO_BUS_NAMES := [&"Master", &"Announcer", &"SFX", &"Music", &"Voicechat"]
const AUDIO_MULTIPLIER_MIN := 0.0
const AUDIO_MULTIPLIER_MAX := 1.0
const AUDIO_MULTIPLIER_DEFAULT := 1.0
const AUDIO_MULTIPLIER_MUTE_DB := -80.0

@onready var close_button: Button = $Shade/Center/Panel/Margin/Root/Header/CloseButton
@onready var voice_mode_option: OptionButton = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceModeRow/VoiceModeOption
@onready var hear_voice_toggle: CheckBox = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/HearVoiceToggle
@onready var mic_monitor_toggle: CheckBox = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/MicMonitorToggle
@onready var voice_status_label: Label = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceStatus
@onready var audio_master_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/MasterVolumeRow/MasterVolumeSlider
@onready var audio_master_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/MasterVolumeRow/MasterVolumeValue
@onready var audio_announcer_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/AnnouncerVolumeRow/AnnouncerVolumeSlider
@onready var audio_announcer_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/AnnouncerVolumeRow/AnnouncerVolumeValue
@onready var audio_sfx_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/SFXVolumeRow/SFXVolumeSlider
@onready var audio_sfx_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/SFXVolumeRow/SFXVolumeValue
@onready var audio_music_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/MusicVolumeRow/MusicVolumeSlider
@onready var audio_music_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/MusicVolumeRow/MusicVolumeValue
@onready var audio_voicechat_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/VoicechatVolumeRow/VoicechatVolumeSlider
@onready var audio_voicechat_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Audio/AudioBox/VoicechatVolumeRow/VoicechatVolumeValue
@onready var controller_settings: Control = $Shade/Center/Panel/Margin/Root/Tabs/Controls/ControllerSettings

var voice_input_mode := "push_to_talk"
var voice_listen_enabled := true
var voice_test_monitor_enabled := false
var audio_bus_base_volume_db := {}
var audio_bus_multipliers := {}
var audio_multiplier_sliders := {}
var audio_multiplier_value_labels := {}
var audio_controls_loading := false

func _ready() -> void:
	close_button.pressed.connect(_on_close_pressed)
	_capture_audio_bus_base_volumes()
	_configure_audio_controls()
	_configure_voice_options()
	voice_mode_option.item_selected.connect(_on_voice_mode_selected)
	hear_voice_toggle.toggled.connect(_on_hear_voice_toggled)
	mic_monitor_toggle.toggled.connect(_on_mic_monitor_toggled)
	if controller_settings != null and controller_settings.has_method("set_embedded_mode"):
		controller_settings.call("set_embedded_mode", true)
	_load_voice_settings()
	_load_audio_settings()
	_update_voice_controls()
	_update_audio_controls()
	_apply_audio_bus_multipliers()
	set_process(true)
	hide()

func open_settings() -> void:
	_load_voice_settings()
	_load_audio_settings()
	_update_voice_controls()
	_update_audio_controls()
	_apply_audio_bus_multipliers()
	if controller_settings != null and controller_settings.has_method("open_settings"):
		controller_settings.call("open_settings")
	show()

func close_settings() -> void:
	_save_voice_settings()
	_save_audio_settings()
	if controller_settings != null and controller_settings.has_method("save_settings"):
		controller_settings.call("save_settings")
	hide()

func _on_close_pressed() -> void:
	close_settings()

func _configure_voice_options() -> void:
	voice_mode_option.clear()
	voice_mode_option.add_item("Push to Talk", 0)
	voice_mode_option.set_item_metadata(0, "push_to_talk")
	voice_mode_option.add_item("Toggle", 1)
	voice_mode_option.set_item_metadata(1, "toggle")
	voice_mode_option.add_item("Always On", 2)
	voice_mode_option.set_item_metadata(2, "always_on")
	voice_mode_option.add_item("Muted", 3)
	voice_mode_option.set_item_metadata(3, "off")

func _update_voice_controls() -> void:
	for i in range(voice_mode_option.item_count):
		if str(voice_mode_option.get_item_metadata(i)) == voice_input_mode:
			voice_mode_option.select(i)
			break
	hear_voice_toggle.button_pressed = voice_listen_enabled
	mic_monitor_toggle.button_pressed = voice_test_monitor_enabled

func _on_voice_mode_selected(index: int) -> void:
	voice_input_mode = str(voice_mode_option.get_item_metadata(index))
	_save_voice_settings()

func _on_hear_voice_toggled(toggled: bool) -> void:
	voice_listen_enabled = toggled
	_save_voice_settings()

func _on_mic_monitor_toggled(toggled: bool) -> void:
	voice_test_monitor_enabled = toggled
	_save_voice_settings()

func _capture_audio_bus_base_volumes() -> void:
	audio_bus_base_volume_db.clear()
	for bus_name in AUDIO_BUS_NAMES:
		var bus_index := AudioServer.get_bus_index(bus_name)
		if bus_index >= 0:
			audio_bus_base_volume_db[bus_name] = AudioServer.get_bus_volume_db(bus_index)

func _configure_audio_controls() -> void:
	audio_multiplier_sliders = {
		&"Master": audio_master_slider,
		&"Announcer": audio_announcer_slider,
		&"SFX": audio_sfx_slider,
		&"Music": audio_music_slider,
		&"Voicechat": audio_voicechat_slider,
	}
	audio_multiplier_value_labels = {
		&"Master": audio_master_value,
		&"Announcer": audio_announcer_value,
		&"SFX": audio_sfx_value,
		&"Music": audio_music_value,
		&"Voicechat": audio_voicechat_value,
	}
	for bus_name in AUDIO_BUS_NAMES:
		var slider := audio_multiplier_sliders.get(bus_name) as HSlider
		if slider != null:
			slider.value_changed.connect(_on_audio_multiplier_slider_changed.bind(bus_name))

func _update_audio_controls() -> void:
	audio_controls_loading = true
	for bus_name in AUDIO_BUS_NAMES:
		var multiplier := float(audio_bus_multipliers.get(bus_name, AUDIO_MULTIPLIER_DEFAULT))
		var slider := audio_multiplier_sliders.get(bus_name) as HSlider
		if slider != null:
			slider.value = multiplier * 100.0
		_update_audio_multiplier_label(bus_name, multiplier)
	audio_controls_loading = false

func _update_audio_multiplier_label(bus_name: StringName, multiplier: float) -> void:
	var value_label := audio_multiplier_value_labels.get(bus_name) as Label
	if value_label != null:
		value_label.text = "%d%%" % roundi(multiplier * 100.0)

func _on_audio_multiplier_slider_changed(value: float, bus_name: StringName) -> void:
	if audio_controls_loading:
		return
	var multiplier := clampf(value * 0.01, AUDIO_MULTIPLIER_MIN, AUDIO_MULTIPLIER_MAX)
	audio_bus_multipliers[bus_name] = multiplier
	_update_audio_multiplier_label(bus_name, multiplier)
	_apply_audio_bus_multiplier(bus_name)
	_save_audio_settings()

func _audio_multiplier_to_db(multiplier: float) -> float:
	if multiplier <= 0.0001:
		return AUDIO_MULTIPLIER_MUTE_DB
	return 20.0 * log(multiplier) / log(10.0)

func _apply_audio_bus_multiplier(bus_name: StringName) -> void:
	var bus_index := AudioServer.get_bus_index(bus_name)
	if bus_index < 0:
		return
	var base_volume := float(audio_bus_base_volume_db.get(bus_name, AudioServer.get_bus_volume_db(bus_index)))
	var multiplier := float(audio_bus_multipliers.get(bus_name, AUDIO_MULTIPLIER_DEFAULT))
	AudioServer.set_bus_volume_db(bus_index, base_volume + _audio_multiplier_to_db(multiplier))

func _apply_audio_bus_multipliers() -> void:
	for bus_name in AUDIO_BUS_NAMES:
		_apply_audio_bus_multiplier(bus_name)

func _process(_delta: float) -> void:
	if !visible:
		return
	var voice_node := _voice_chat_node()
	if voice_node == null or !voice_node.has_method("get_voice_debug_status"):
		voice_status_label.text = "Voice Status: unavailable"
		return
	var status: Dictionary = voice_node.call("get_voice_debug_status")
	var level := float(status.get("capture_level", 0.0))
	var level_pct := roundi(clampf(level, 0.0, 1.0) * 100.0)
	var encoded := int(status.get("packets_encoded", 0))
	var received := int(status.get("packets_received", 0))
	var capture_status := str(status.get("capture_status", "idle"))
	var spatial_tick := int(status.get("voice_spatial_tick", -1))
	var snapshot_count := int(status.get("voice_snapshot_count", 0))
	var matches := int(status.get("voice_position_matches", 0))
	var relay_local := int(status.get("voice_relay_local_deliveries", 0))
	var relay_remote := int(status.get("voice_relay_remote_deliveries", 0))
	var receive_attempts := int(status.get("voice_receive_attempts", 0))
	var decode_pushes := int(status.get("voice_decode_pushes", 0))
	var last_sender := int(status.get("voice_last_sender_id", -1))
	var recipient_count := int(status.get("voice_last_recipient_count", 0))
	var recipient_snapshot_count := int(status.get("voice_last_recipient_snapshot_count", 0))
	var local_candidate := bool(status.get("voice_last_local_candidate", false))
	var local_distance := float(status.get("voice_last_local_distance", -1.0))
	var drop_reason := str(status.get("voice_last_drop_reason", ""))
	if drop_reason.is_empty():
		drop_reason = "none"
	voice_status_label.text = "Voice Status: %s | Mic %d%% | Sent %d | Heard %d/%d | Decoded %d | Relay L/R %d/%d | Last sender %d recips %d snap %d local %s %.1f | Spatial %d src %d/%d | Drop %s" % [
		capture_status,
		level_pct,
		encoded,
		received,
		receive_attempts,
		decode_pushes,
		relay_local,
		relay_remote,
		last_sender,
		recipient_count,
		recipient_snapshot_count,
		str(local_candidate),
		local_distance,
		spatial_tick,
		matches,
		snapshot_count,
		drop_reason,
	]

func _voice_chat_node() -> Node:
	var root := get_parent()
	if root == null:
		return null
	var network_manager := root.get_node_or_null("NetworkManager")
	if network_manager == null:
		return null
	return network_manager.get_node_or_null("ProximityVoiceChat")

func _save_voice_settings() -> void:
	var data := {
		"version": 1,
		"input_mode": voice_input_mode,
		"listen_enabled": voice_listen_enabled,
		"test_monitor_enabled": voice_test_monitor_enabled,
	}
	var file := FileAccess.open(VOICE_SETTINGS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data))
		file.close()

func _load_voice_settings() -> void:
	voice_input_mode = "push_to_talk"
	voice_listen_enabled = true
	voice_test_monitor_enabled = false
	if !FileAccess.file_exists(VOICE_SETTINGS_PATH):
		return
	var txt := FileAccess.get_file_as_string(VOICE_SETTINGS_PATH)
	var data = JSON.parse_string(txt)
	if typeof(data) != TYPE_DICTIONARY:
		return
	var mode := str(data.get("input_mode", "push_to_talk"))
	if mode == "push_to_talk" or mode == "toggle" or mode == "always_on" or mode == "off":
		voice_input_mode = mode
	voice_listen_enabled = bool(data.get("listen_enabled", true))
	voice_test_monitor_enabled = bool(data.get("test_monitor_enabled", false))

func _save_audio_settings() -> void:
	var multipliers := {}
	for bus_name in AUDIO_BUS_NAMES:
		multipliers[str(bus_name)] = float(audio_bus_multipliers.get(bus_name, AUDIO_MULTIPLIER_DEFAULT))
	var data := {
		"version": 1,
		"multipliers": multipliers,
	}
	var file := FileAccess.open(AUDIO_SETTINGS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data))
		file.close()

func _load_audio_settings() -> void:
	audio_bus_multipliers.clear()
	for bus_name in AUDIO_BUS_NAMES:
		audio_bus_multipliers[bus_name] = AUDIO_MULTIPLIER_DEFAULT
	if !FileAccess.file_exists(AUDIO_SETTINGS_PATH):
		return
	var txt := FileAccess.get_file_as_string(AUDIO_SETTINGS_PATH)
	var data = JSON.parse_string(txt)
	if typeof(data) != TYPE_DICTIONARY:
		return
	var multipliers = data.get("multipliers", {})
	if typeof(multipliers) != TYPE_DICTIONARY:
		return
	for bus_name in AUDIO_BUS_NAMES:
		var key := str(bus_name)
		if multipliers.has(key):
			audio_bus_multipliers[bus_name] = clampf(float(multipliers[key]), AUDIO_MULTIPLIER_MIN, AUDIO_MULTIPLIER_MAX)
