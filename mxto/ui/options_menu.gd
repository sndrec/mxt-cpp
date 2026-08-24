class_name OptionsMenu
extends Control

signal vehicle_view_distance_changed(multiplier: float, render_all: bool)

const VOICE_SETTINGS_PATH := "user://voice_chat_settings.json"
const AUDIO_SETTINGS_PATH := "user://audio_settings.json"
const GRAPHICS_SETTINGS_PATH := "user://graphics_settings.json"
const AUDIO_BUS_NAMES := [&"Master", &"Announcer", &"SFX", &"Music", &"Voicechat"]
const AUDIO_MULTIPLIER_MIN := 0.0
const AUDIO_MULTIPLIER_MAX := 1.0
const AUDIO_MULTIPLIER_DEFAULT := 1.0
const AUDIO_MULTIPLIER_MUTE_DB := -80.0
const VOICE_SENSITIVITY_DEFAULT := 0.08
const FPS_LIMIT_MIN := 30
const FPS_LIMIT_MAX := 1000
const FPS_LIMIT_DEFAULT := 360
const VEHICLE_VIEW_DISTANCE_MIN_MULTIPLIER := 1.0
const VEHICLE_VIEW_DISTANCE_MAX_MULTIPLIER := 3.0
const VEHICLE_VIEW_DISTANCE_STEP := 0.25
const VEHICLE_VIEW_DISTANCE_ALL_VALUE := VEHICLE_VIEW_DISTANCE_MAX_MULTIPLIER + VEHICLE_VIEW_DISTANCE_STEP

@onready var close_button: Button = $Shade/Center/Panel/Margin/Root/Header/CloseButton
@onready var voice_mode_option: OptionButton = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceModeRow/VoiceModeOption
@onready var voice_sensitivity_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceSensitivityRow/VoiceSensitivityStack/VoiceSensitivitySlider
@onready var voice_sensitivity_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceSensitivityRow/TopLine/VoiceSensitivityValue
@onready var voice_sensitivity_meter_track: ColorRect = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceSensitivityRow/VoiceSensitivityStack/VoiceSensitivityMeterTrack
@onready var voice_sensitivity_meter_fill: ColorRect = $Shade/Center/Panel/Margin/Root/Tabs/Communication/CommunicationBox/VoiceSensitivityRow/VoiceSensitivityStack/VoiceSensitivityMeterTrack/VoiceSensitivityMeterFill
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
@onready var graphics_window_mode_option: OptionButton = $Shade/Center/Panel/Margin/Root/Tabs/Graphics/GraphicsBox/WindowModeRow/WindowModeOption
@onready var graphics_vsync_mode_option: OptionButton = $Shade/Center/Panel/Margin/Root/Tabs/Graphics/GraphicsBox/VSyncModeRow/VSyncModeOption
@onready var graphics_fps_limit_spin_box: SpinBox = $Shade/Center/Panel/Margin/Root/Tabs/Graphics/GraphicsBox/FPSLimitRow/FPSLimitSpinBox
@onready var graphics_vehicle_view_distance_slider: HSlider = $Shade/Center/Panel/Margin/Root/Tabs/Graphics/GraphicsBox/VehicleViewDistanceRow/VehicleViewDistanceSlider
@onready var graphics_vehicle_view_distance_value: Label = $Shade/Center/Panel/Margin/Root/Tabs/Graphics/GraphicsBox/VehicleViewDistanceRow/VehicleViewDistanceValue
@onready var graphics_current_display_label: Label = $Shade/Center/Panel/Margin/Root/Tabs/Graphics/GraphicsBox/CurrentDisplayLabel
@onready var controller_settings: Control = $Shade/Center/Panel/Margin/Root/Tabs/Controls/ControllerSettings

var voice_input_mode := "push_to_talk"
var voice_listen_enabled := true
var voice_test_monitor_enabled := false
var voice_always_on_threshold := VOICE_SENSITIVITY_DEFAULT
var voice_controls_loading := false
var audio_bus_base_volume_db := {}
var audio_bus_multipliers := {}
var audio_multiplier_sliders := {}
var audio_multiplier_value_labels := {}
var audio_controls_loading := false
var graphics_window_mode := DisplayServer.WINDOW_MODE_WINDOWED
var graphics_vsync_mode := DisplayServer.VSYNC_ENABLED
var graphics_fps_limit := FPS_LIMIT_DEFAULT
var graphics_vehicle_view_distance_multiplier := VEHICLE_VIEW_DISTANCE_MIN_MULTIPLIER
var graphics_render_all_vehicles := false
var graphics_controls_loading := false

func _ready() -> void:
	close_button.pressed.connect(_on_close_pressed)
	_capture_audio_bus_base_volumes()
	_configure_audio_controls()
	_configure_voice_options()
	_configure_graphics_options()
	voice_mode_option.item_selected.connect(_on_voice_mode_selected)
	voice_sensitivity_slider.value_changed.connect(_on_voice_sensitivity_changed)
	hear_voice_toggle.toggled.connect(_on_hear_voice_toggled)
	mic_monitor_toggle.toggled.connect(_on_mic_monitor_toggled)
	graphics_window_mode_option.item_selected.connect(_on_graphics_window_mode_selected)
	graphics_vsync_mode_option.item_selected.connect(_on_graphics_vsync_mode_selected)
	graphics_fps_limit_spin_box.value_changed.connect(_on_graphics_fps_limit_changed)
	graphics_vehicle_view_distance_slider.value_changed.connect(_on_graphics_vehicle_view_distance_changed)
	if controller_settings != null and controller_settings.has_method("set_embedded_mode"):
		controller_settings.call("set_embedded_mode", true)
	_load_voice_settings()
	_load_audio_settings()
	_load_graphics_settings()
	_update_voice_controls()
	_update_audio_controls()
	_update_graphics_controls()
	_apply_audio_bus_multipliers()
	_apply_graphics_settings()
	set_process(true)
	hide()

func open_settings() -> void:
	_load_voice_settings()
	_load_audio_settings()
	_load_graphics_settings()
	_update_voice_controls()
	_update_audio_controls()
	_update_graphics_controls()
	_apply_audio_bus_multipliers()
	_set_voice_level_meter_enabled(true)
	if controller_settings != null and controller_settings.has_method("open_settings"):
		controller_settings.call("open_settings")
	show()
	close_button.grab_focus()

func close_settings() -> void:
	_set_voice_level_meter_enabled(false)
	_save_voice_settings()
	_save_audio_settings()
	_save_graphics_settings()
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
	voice_controls_loading = true
	for i in range(voice_mode_option.item_count):
		if str(voice_mode_option.get_item_metadata(i)) == voice_input_mode:
			voice_mode_option.select(i)
			break
	voice_sensitivity_slider.value = voice_always_on_threshold * 100.0
	_update_voice_sensitivity_label()
	hear_voice_toggle.button_pressed = voice_listen_enabled
	mic_monitor_toggle.button_pressed = voice_test_monitor_enabled
	voice_controls_loading = false

func _on_voice_mode_selected(index: int) -> void:
	voice_input_mode = str(voice_mode_option.get_item_metadata(index))
	_save_voice_settings()

func _on_voice_sensitivity_changed(value: float) -> void:
	if voice_controls_loading:
		return
	voice_always_on_threshold = clampf(value * 0.01, 0.0, 1.0)
	_update_voice_sensitivity_label()
	_apply_voice_sensitivity_to_runtime()
	_save_voice_settings()

func _update_voice_sensitivity_label() -> void:
	if voice_sensitivity_value != null:
		voice_sensitivity_value.text = "%d%%" % roundi(voice_always_on_threshold * 100.0)

func _apply_voice_sensitivity_to_runtime() -> void:
	var voice_node := _voice_chat_node()
	if voice_node != null and voice_node.has_method("set_always_on_threshold"):
		voice_node.call("set_always_on_threshold", voice_always_on_threshold)

func _set_voice_level_meter_enabled(enabled: bool) -> void:
	var voice_node := _voice_chat_node()
	if voice_node != null and voice_node.has_method("set_level_meter_enabled"):
		voice_node.call("set_level_meter_enabled", enabled)

func _on_hear_voice_toggled(toggled: bool) -> void:
	voice_listen_enabled = toggled
	_save_voice_settings()

func _on_mic_monitor_toggled(toggled: bool) -> void:
	voice_test_monitor_enabled = toggled
	_save_voice_settings()

func _configure_graphics_options() -> void:
	graphics_window_mode_option.clear()
	graphics_window_mode_option.add_item("Windowed")
	graphics_window_mode_option.set_item_metadata(0, DisplayServer.WINDOW_MODE_WINDOWED)
	graphics_window_mode_option.add_item("Borderless Fullscreen")
	graphics_window_mode_option.set_item_metadata(1, DisplayServer.WINDOW_MODE_FULLSCREEN)
	graphics_window_mode_option.add_item("Exclusive Fullscreen")
	graphics_window_mode_option.set_item_metadata(2, DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN)

	graphics_vsync_mode_option.clear()
	graphics_vsync_mode_option.add_item("Disabled")
	graphics_vsync_mode_option.set_item_metadata(0, DisplayServer.VSYNC_DISABLED)
	graphics_vsync_mode_option.add_item("Enabled")
	graphics_vsync_mode_option.set_item_metadata(1, DisplayServer.VSYNC_ENABLED)
	graphics_vsync_mode_option.add_item("Adaptive")
	graphics_vsync_mode_option.set_item_metadata(2, DisplayServer.VSYNC_ADAPTIVE)
	graphics_vsync_mode_option.add_item("Mailbox")
	graphics_vsync_mode_option.set_item_metadata(3, DisplayServer.VSYNC_MAILBOX)

func _update_graphics_controls() -> void:
	graphics_controls_loading = true
	for i in range(graphics_window_mode_option.item_count):
		if int(graphics_window_mode_option.get_item_metadata(i)) == graphics_window_mode:
			graphics_window_mode_option.select(i)
			break
	for i in range(graphics_vsync_mode_option.item_count):
		if int(graphics_vsync_mode_option.get_item_metadata(i)) == graphics_vsync_mode:
			graphics_vsync_mode_option.select(i)
			break
	graphics_fps_limit_spin_box.value = graphics_fps_limit
	graphics_vehicle_view_distance_slider.value = VEHICLE_VIEW_DISTANCE_ALL_VALUE if graphics_render_all_vehicles else graphics_vehicle_view_distance_multiplier
	_update_vehicle_view_distance_label()
	_update_current_display_label()
	graphics_controls_loading = false

func _on_graphics_window_mode_selected(index: int) -> void:
	if graphics_controls_loading:
		return
	graphics_window_mode = int(graphics_window_mode_option.get_item_metadata(index))
	_apply_graphics_window_mode()
	_update_current_display_label()
	_save_graphics_settings()

func _on_graphics_vsync_mode_selected(index: int) -> void:
	if graphics_controls_loading:
		return
	graphics_vsync_mode = int(graphics_vsync_mode_option.get_item_metadata(index))
	_apply_graphics_vsync_mode()
	_save_graphics_settings()

func _on_graphics_fps_limit_changed(value: float) -> void:
	if graphics_controls_loading:
		return
	graphics_fps_limit = clampi(roundi(value), FPS_LIMIT_MIN, FPS_LIMIT_MAX)
	Engine.max_fps = graphics_fps_limit
	_save_graphics_settings()

func _on_graphics_vehicle_view_distance_changed(value: float) -> void:
	if graphics_controls_loading:
		return
	graphics_render_all_vehicles = value >= VEHICLE_VIEW_DISTANCE_ALL_VALUE - 0.001
	if !graphics_render_all_vehicles:
		graphics_vehicle_view_distance_multiplier = clampf(
			value,
			VEHICLE_VIEW_DISTANCE_MIN_MULTIPLIER,
			VEHICLE_VIEW_DISTANCE_MAX_MULTIPLIER)
	_update_vehicle_view_distance_label()
	vehicle_view_distance_changed.emit(graphics_vehicle_view_distance_multiplier, graphics_render_all_vehicles)
	_save_graphics_settings()

func _update_vehicle_view_distance_label() -> void:
	if graphics_render_all_vehicles:
		graphics_vehicle_view_distance_value.text = "All vehicles"
	elif is_equal_approx(graphics_vehicle_view_distance_multiplier, VEHICLE_VIEW_DISTANCE_MIN_MULTIPLIER):
		graphics_vehicle_view_distance_value.text = "Current"
	else:
		graphics_vehicle_view_distance_value.text = "%.2fx" % graphics_vehicle_view_distance_multiplier

func get_vehicle_view_distance_multiplier() -> float:
	return graphics_vehicle_view_distance_multiplier

func get_render_all_vehicles() -> bool:
	return graphics_render_all_vehicles

func _apply_graphics_settings() -> void:
	Engine.max_fps = graphics_fps_limit
	if DisplayServer.get_name() == "headless":
		_update_current_display_label()
		return
	_apply_graphics_window_mode()
	_apply_graphics_vsync_mode()
	_update_current_display_label()

func _apply_graphics_window_mode() -> void:
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_mode(graphics_window_mode)

func _apply_graphics_vsync_mode() -> void:
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_vsync_mode(graphics_vsync_mode)

func _update_current_display_label() -> void:
	if DisplayServer.get_name() == "headless":
		graphics_current_display_label.text = "Current monitor mode is unavailable in headless mode."
		return
	var screen := DisplayServer.window_get_current_screen()
	var screen_size := DisplayServer.screen_get_size(screen)
	var refresh_rate := DisplayServer.screen_get_refresh_rate(screen)
	var refresh_text := "unknown refresh rate"
	if refresh_rate > 0.0:
		var rounded_refresh_rate := roundi(refresh_rate)
		if absf(refresh_rate - float(rounded_refresh_rate)) < 0.01:
			refresh_text = "%d Hz" % rounded_refresh_rate
		else:
			refresh_text = "%.2f Hz" % refresh_rate
	graphics_current_display_label.text = "Current monitor mode: %d x %d @ %s\nBorderless and exclusive fullscreen use this resolution and refresh rate." % [
		screen_size.x,
		screen_size.y,
		refresh_text,
	]

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
	var output_level := float(status.get("capture_output_level", level))
	var capture_gain_db := float(status.get("capture_gain_db", 0.0))
	var smoothed_level := float(status.get("smoothed_capture_level", level))
	_update_voice_sensitivity_meter(smoothed_level)
	var level_pct := roundi(clampf(level, 0.0, 1.0) * 100.0)
	var output_level_pct := roundi(clampf(output_level, 0.0, 1.0) * 100.0)
	var encoded := int(status.get("packets_encoded", 0))
	var received := int(status.get("packets_received", 0))
	var capture_discarded := int(status.get("capture_frames_discarded", 0))
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
	voice_status_label.text = "Voice Status: %s | Mic %d%%>%d%% %.1fdB | Sent %d | Heard %d/%d | Decoded %d | Discard %d | Relay L/R %d/%d | Last sender %d recips %d snap %d local %s %.1f | Spatial %d src %d/%d | Drop %s" % [
		capture_status,
		level_pct,
		output_level_pct,
		capture_gain_db,
		encoded,
		received,
		receive_attempts,
		decode_pushes,
		capture_discarded,
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

func _update_voice_sensitivity_meter(level: float) -> void:
	if voice_sensitivity_meter_track == null or voice_sensitivity_meter_fill == null:
		return
	var width := voice_sensitivity_meter_track.size.x
	voice_sensitivity_meter_fill.offset_right = maxf(1.0, width * clampf(level, 0.0, 1.0))

func _save_voice_settings() -> void:
	var data := {
		"version": 1,
		"input_mode": voice_input_mode,
		"listen_enabled": voice_listen_enabled,
		"test_monitor_enabled": voice_test_monitor_enabled,
		"always_on_threshold": voice_always_on_threshold,
	}
	var file := FileAccess.open(VOICE_SETTINGS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data))
		file.close()

func _load_voice_settings() -> void:
	voice_input_mode = "push_to_talk"
	voice_listen_enabled = true
	voice_test_monitor_enabled = false
	voice_always_on_threshold = VOICE_SENSITIVITY_DEFAULT
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
	voice_always_on_threshold = clampf(float(data.get("always_on_threshold", VOICE_SENSITIVITY_DEFAULT)), 0.0, 1.0)
	_apply_voice_sensitivity_to_runtime()

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

func _save_graphics_settings() -> void:
	var data := {
		"window_mode": graphics_window_mode,
		"vsync_mode": graphics_vsync_mode,
		"fps_limit": graphics_fps_limit,
		"vehicle_view_distance_multiplier": graphics_vehicle_view_distance_multiplier,
		"render_all_vehicles": graphics_render_all_vehicles,
	}
	var file := FileAccess.open(GRAPHICS_SETTINGS_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(data))
		file.close()

func _load_graphics_settings() -> void:
	graphics_window_mode = DisplayServer.WINDOW_MODE_WINDOWED
	graphics_vsync_mode = DisplayServer.VSYNC_ENABLED
	graphics_fps_limit = Engine.max_fps
	graphics_vehicle_view_distance_multiplier = VEHICLE_VIEW_DISTANCE_MIN_MULTIPLIER
	graphics_render_all_vehicles = false
	if graphics_fps_limit < FPS_LIMIT_MIN or graphics_fps_limit > FPS_LIMIT_MAX:
		graphics_fps_limit = FPS_LIMIT_DEFAULT
	if DisplayServer.get_name() != "headless":
		var current_window_mode := DisplayServer.window_get_mode()
		if current_window_mode == DisplayServer.WINDOW_MODE_FULLSCREEN or current_window_mode == DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN:
			graphics_window_mode = current_window_mode
		var current_vsync_mode := DisplayServer.window_get_vsync_mode()
		if current_vsync_mode >= DisplayServer.VSYNC_DISABLED and current_vsync_mode <= DisplayServer.VSYNC_MAILBOX:
			graphics_vsync_mode = current_vsync_mode
	if !FileAccess.file_exists(GRAPHICS_SETTINGS_PATH):
		return
	var txt := FileAccess.get_file_as_string(GRAPHICS_SETTINGS_PATH)
	var data = JSON.parse_string(txt)
	if typeof(data) != TYPE_DICTIONARY:
		return
	var loaded_window_mode := int(data.get("window_mode", graphics_window_mode))
	if loaded_window_mode == DisplayServer.WINDOW_MODE_WINDOWED or loaded_window_mode == DisplayServer.WINDOW_MODE_FULLSCREEN or loaded_window_mode == DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN:
		graphics_window_mode = loaded_window_mode
	var loaded_vsync_mode := int(data.get("vsync_mode", graphics_vsync_mode))
	if loaded_vsync_mode >= DisplayServer.VSYNC_DISABLED and loaded_vsync_mode <= DisplayServer.VSYNC_MAILBOX:
		graphics_vsync_mode = loaded_vsync_mode
	graphics_fps_limit = clampi(int(data.get("fps_limit", graphics_fps_limit)), FPS_LIMIT_MIN, FPS_LIMIT_MAX)
	graphics_vehicle_view_distance_multiplier = clampf(
		float(data.get("vehicle_view_distance_multiplier", graphics_vehicle_view_distance_multiplier)),
		VEHICLE_VIEW_DISTANCE_MIN_MULTIPLIER,
		VEHICLE_VIEW_DISTANCE_MAX_MULTIPLIER)
	graphics_render_all_vehicles = bool(data.get("render_all_vehicles", graphics_render_all_vehicles))
