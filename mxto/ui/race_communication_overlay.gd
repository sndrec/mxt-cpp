class_name RaceCommunicationOverlay
extends Control

signal message_submitted(text: String)

const MESSAGE_VISIBLE_MSEC := 8000
const MESSAGE_FADE_MSEC := 2000
const CHAT_FADE_SECONDS := 0.5
const MAX_CHAT_HISTORY := 96
const DEFAULT_CHAT_FONT_SIZE := 18
const DEFAULT_CHAT_OUTLINE_SIZE := 1
const DEFAULT_CHAT_TEXT_COLOR := Color.WHITE
const DEFAULT_CHAT_OUTLINE_COLOR := Color.BLACK
const CHAT_PANEL_BG_COLOR := Color(0.0, 0.0, 0.0, 0.52)
const CHAT_PANEL_BORDER_COLOR := Color(0.0, 0.0, 0.0, 0.92)
const CHAT_INPUT_BG_COLOR := Color(0.0, 0.0, 0.0, 0.62)
const CHAT_INPUT_BORDER_COLOR := Color(1.0, 0.86, 0.12, 0.85)
const VOICE_QUIET_COLOR := Color(0.0, 0.0, 0.0, 0.72)
const VOICE_LOUD_COLOR := Color(0.58, 1.0, 0.46, 0.82)

@export_group("Chat Text")
@export var chat_label_settings: LabelSettings
@export var chat_username_color := Color(1.0, 0.86, 0.12, 1.0)

@export_group("Voice")
@export var voice_box_size := Vector2(200.0, 64.0)

@onready var chat_area: Control = $ChatArea
@onready var history_panel: PanelContainer = $ChatArea/HistoryPanel
@onready var history_messages: VBoxContainer = $ChatArea/HistoryPanel/Margin/HistoryBox/HistoryScroll/HistoryMessages
@onready var history_scroll: ScrollContainer = $ChatArea/HistoryPanel/Margin/HistoryBox/HistoryScroll
@onready var chat_input: LineEdit = $ChatArea/HistoryPanel/Margin/HistoryBox/ChatInput
@onready var voice_boxes_root: VBoxContainer = $VoiceArea/VoiceBoxes
@onready var local_voice_icon: TextureRect = $VoiceArea/LocalVoiceIcon

var _chat_open := false
var _chat_tween: Tween
var _chat_chrome_alpha := 0.0
var _messages: Array[Dictionary] = []
var _voice_boxes := {}
var _voice_box_styles := {}
var _panel_style: StyleBoxFlat
var _input_style: StyleBoxFlat
var text_chat_enabled := true

func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	chat_area.visible = text_chat_enabled
	_configure_chat_panel()
	history_panel.visible = true
	history_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	history_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	chat_input.visible = false
	local_voice_icon.visible = false
	_set_chat_chrome_alpha(0.0)
	set_process(true)

func set_text_chat_enabled(enabled: bool) -> void:
	text_chat_enabled = enabled
	if text_chat_enabled:
		set_process(true)
		if chat_area != null:
			chat_area.visible = true
		if history_panel != null:
			history_panel.visible = true
		return
	_chat_open = false
	if _chat_tween != null:
		_chat_tween.kill()
		_chat_tween = null
	_messages.clear()
	set_process(false)
	if chat_area != null:
		chat_area.visible = false
	if history_panel != null:
		history_panel.visible = false
		history_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if history_scroll != null:
		history_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	if chat_input != null:
		chat_input.visible = false
		chat_input.release_focus()
	if history_messages != null:
		for child in history_messages.get_children():
			child.queue_free()

func _configure_chat_panel() -> void:
	_panel_style = StyleBoxFlat.new()
	_panel_style.bg_color = CHAT_PANEL_BG_COLOR
	_panel_style.border_color = CHAT_PANEL_BORDER_COLOR
	_panel_style.set_border_width_all(1)
	_panel_style.set_corner_radius_all(2)
	history_panel.add_theme_stylebox_override("panel", _panel_style)
	_input_style = StyleBoxFlat.new()
	_input_style.bg_color = CHAT_INPUT_BG_COLOR
	_input_style.border_color = CHAT_INPUT_BORDER_COLOR
	_input_style.set_border_width_all(1)
	_input_style.set_corner_radius_all(2)
	chat_input.add_theme_stylebox_override("normal", _input_style)
	chat_input.add_theme_stylebox_override("focus", _input_style)
	_apply_chat_input_style()

func is_chat_open() -> bool:
	return text_chat_enabled and _chat_open

func open_chat() -> void:
	if !text_chat_enabled:
		return
	if _chat_open:
		return
	_chat_open = true
	history_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	history_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	chat_input.visible = true
	chat_input.modulate.a = _chat_chrome_alpha
	chat_input.clear()
	chat_input.grab_focus()
	_refresh_history_messages()
	_scroll_history_to_bottom_next_frame()
	_fade_chat_chrome(1.0)

func close_chat() -> void:
	if !text_chat_enabled:
		return
	if !_chat_open:
		return
	_chat_open = false
	chat_input.release_focus()
	history_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	history_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_fade_chat_chrome(0.0)
	_refresh_history_messages()

func append_message(sender_id: int, sender_name: String, text: String) -> void:
	if !text_chat_enabled:
		return
	var clean_text := text.strip_edges()
	if clean_text == "":
		return
	_messages.append({
		"id": sender_id,
		"name": sender_name,
		"text": clean_text,
		"msec": Time.get_ticks_msec(),
	})
	while _messages.size() > MAX_CHAT_HISTORY:
		_messages.remove_at(0)
	_refresh_history_messages()

func set_voice_status(status: Dictionary, player_names: Dictionary) -> void:
	var local_id := int(status.get("local_id", -1))
	var local_broadcasting := bool(status.get("local_voice_broadcasting", false))
	var race_active := bool(status.get("race_active", false))
	local_voice_icon.visible = race_active and local_broadcasting
	var active := {}
	if !race_active:
		_sync_voice_boxes(active)
		return
	var remote_peers: Array = status.get("remote_voice_peers", [])
	for peer_data in remote_peers:
		if typeof(peer_data) != TYPE_DICTIONARY:
			continue
		var peer_id := int(peer_data.get("id", -1))
		if peer_id < 0 or peer_id == local_id:
			continue
		active[peer_id] = {
			"name": str(player_names.get(peer_id, str(peer_id))),
			"level": float(peer_data.get("level", 0.0)),
		}
	_sync_voice_boxes(active)

func _input(event: InputEvent) -> void:
	if !text_chat_enabled:
		return
	if !_chat_open:
		return
	if !(event is InputEventKey):
		return
	var key := event as InputEventKey
	if !key.pressed or key.echo:
		return
	if key.keycode == KEY_ESCAPE:
		close_chat()
		get_viewport().set_input_as_handled()
	elif key.keycode == KEY_ENTER or key.keycode == KEY_KP_ENTER:
		var text := chat_input.text.strip_edges()
		if text != "":
			message_submitted.emit(text)
		close_chat()
		get_viewport().set_input_as_handled()

func _process(_delta: float) -> void:
	if !text_chat_enabled:
		return
	if !_chat_open:
		_refresh_history_messages()
		_scroll_history_to_bottom_deferred()

func _fade_chat_chrome(target_alpha: float) -> void:
	if !text_chat_enabled:
		return
	if _chat_tween != null:
		_chat_tween.kill()
	_chat_tween = create_tween()
	_chat_tween.set_trans(Tween.TRANS_SINE)
	_chat_tween.set_ease(Tween.EASE_OUT)
	_chat_tween.tween_method(_set_chat_chrome_alpha, _chat_chrome_alpha, target_alpha, CHAT_FADE_SECONDS)
	if target_alpha <= 0.0:
		_chat_tween.tween_callback(_on_chat_chrome_fade_out_complete)

func _on_chat_chrome_fade_out_complete() -> void:
	if !_chat_open:
		chat_input.visible = false

func _set_chat_chrome_alpha(alpha: float) -> void:
	_chat_chrome_alpha = clampf(alpha, 0.0, 1.0)
	if _panel_style != null:
		_panel_style.bg_color = Color(CHAT_PANEL_BG_COLOR.r, CHAT_PANEL_BG_COLOR.g, CHAT_PANEL_BG_COLOR.b, CHAT_PANEL_BG_COLOR.a * _chat_chrome_alpha)
		_panel_style.border_color = Color(CHAT_PANEL_BORDER_COLOR.r, CHAT_PANEL_BORDER_COLOR.g, CHAT_PANEL_BORDER_COLOR.b, CHAT_PANEL_BORDER_COLOR.a * _chat_chrome_alpha)
	if _input_style != null:
		_input_style.bg_color = Color(CHAT_INPUT_BG_COLOR.r, CHAT_INPUT_BG_COLOR.g, CHAT_INPUT_BG_COLOR.b, CHAT_INPUT_BG_COLOR.a * _chat_chrome_alpha)
		_input_style.border_color = Color(CHAT_INPUT_BORDER_COLOR.r, CHAT_INPUT_BORDER_COLOR.g, CHAT_INPUT_BORDER_COLOR.b, CHAT_INPUT_BORDER_COLOR.a * _chat_chrome_alpha)
	if chat_input != null:
		chat_input.modulate.a = _chat_chrome_alpha

func _refresh_history_messages() -> void:
	if !text_chat_enabled:
		return
	if history_messages == null:
		return
	_sync_chat_labels(history_messages, _messages, _chat_open)
	if !_chat_open:
		_scroll_history_to_bottom_deferred()

func _scroll_history_to_bottom_deferred() -> void:
	if history_scroll != null:
		call_deferred("_scroll_history_to_bottom")

func _scroll_history_to_bottom_next_frame() -> void:
	await get_tree().process_frame
	_scroll_history_to_bottom()

func _scroll_history_to_bottom() -> void:
	if history_scroll == null:
		return
	var scroll_bar := history_scroll.get_v_scroll_bar()
	if scroll_bar != null:
		scroll_bar.value = scroll_bar.max_value

func _sync_chat_labels(parent: VBoxContainer, source_messages: Array, force_alpha: bool) -> void:
	while parent.get_child_count() > source_messages.size():
		var child := parent.get_child(parent.get_child_count() - 1)
		parent.remove_child(child)
		child.queue_free()
	while parent.get_child_count() < source_messages.size():
		parent.add_child(_new_chat_label())
	for i in range(source_messages.size()):
		var label := parent.get_child(i) as RichTextLabel
		if label == null:
			continue
		var message: Dictionary = source_messages[i]
		_apply_chat_label_style(label)
		label.text = _format_chat_line(str(message.get("name", "")), str(message.get("text", "")))
		label.modulate.a = 1.0 if force_alpha else _message_alpha(message, Time.get_ticks_msec())

func _new_chat_label() -> RichTextLabel:
	var label := RichTextLabel.new()
	label.bbcode_enabled = true
	label.fit_content = true
	label.scroll_active = false
	label.selection_enabled = false
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_apply_chat_label_style(label)
	return label

func _apply_chat_label_style(label: RichTextLabel) -> void:
	var font := _chat_font()
	if font != null:
		label.add_theme_font_override("normal_font", font)
	else:
		label.remove_theme_font_override("normal_font")
	label.add_theme_font_size_override("normal_font_size", _chat_font_size())
	label.add_theme_color_override("default_color", _chat_text_color())
	label.add_theme_color_override("font_outline_color", _chat_outline_color())
	label.add_theme_constant_override("outline_size", _chat_outline_size())

func _apply_chat_input_style() -> void:
	var font := _chat_font()
	if font != null:
		chat_input.add_theme_font_override("font", font)
	else:
		chat_input.remove_theme_font_override("font")
	chat_input.add_theme_font_size_override("font_size", _chat_font_size())
	chat_input.add_theme_color_override("font_color", _chat_text_color())
	chat_input.add_theme_color_override("font_outline_color", _chat_outline_color())
	chat_input.add_theme_constant_override("outline_size", _chat_outline_size())

func _chat_font() -> Font:
	if chat_label_settings == null:
		return null
	return chat_label_settings.font

func _chat_font_size() -> int:
	if chat_label_settings == null:
		return DEFAULT_CHAT_FONT_SIZE
	return chat_label_settings.font_size

func _chat_outline_size() -> int:
	if chat_label_settings == null:
		return DEFAULT_CHAT_OUTLINE_SIZE
	return chat_label_settings.outline_size

func _chat_outline_color() -> Color:
	if chat_label_settings == null:
		return DEFAULT_CHAT_OUTLINE_COLOR
	return chat_label_settings.outline_color

func _chat_text_color() -> Color:
	if chat_label_settings == null:
		return DEFAULT_CHAT_TEXT_COLOR
	return chat_label_settings.font_color

func _format_chat_line(sender_name: String, text: String) -> String:
	return "[color=#%s]%s[/color][color=#%s]: %s[/color]" % [
		chat_username_color.to_html(false),
		_escape_bbcode(sender_name),
		_chat_text_color().to_html(false),
		_escape_bbcode(text),
	]

func _escape_bbcode(text: String) -> String:
	return text.replace("[", "[lb]")

func _message_alpha(message: Dictionary, now_msec: int) -> float:
	var age := now_msec - int(message.get("msec", now_msec))
	if age < MESSAGE_VISIBLE_MSEC:
		return 1.0
	if age >= MESSAGE_VISIBLE_MSEC + MESSAGE_FADE_MSEC:
		return 0.0
	return 1.0 - float(age - MESSAGE_VISIBLE_MSEC) / float(MESSAGE_FADE_MSEC)

func _sync_voice_boxes(active: Dictionary) -> void:
	for id in _voice_boxes.keys().duplicate():
		if active.has(id):
			continue
		var stale := _voice_boxes[id] as Control
		if stale != null:
			stale.queue_free()
		_voice_boxes.erase(id)
		_voice_box_styles.erase(id)
	for id in active.keys():
		var box := _voice_boxes.get(id, null) as PanelContainer
		if box == null:
			box = _new_voice_box(int(id))
			_voice_boxes[id] = box
			voice_boxes_root.add_child(box)
		var data: Dictionary = active[id]
		var name_label := box.get_node_or_null("Name") as Label
		if name_label != null:
			name_label.text = str(data.get("name", str(id)))
		var style := _voice_box_styles.get(id, null) as StyleBoxFlat
		if style != null:
			var level := clampf(float(data.get("level", 0.0)), 0.0, 1.0)
			style.bg_color = VOICE_QUIET_COLOR.lerp(VOICE_LOUD_COLOR, level)

func _new_voice_box(peer_id: int) -> PanelContainer:
	var box := PanelContainer.new()
	box.name = "VoiceBox%d" % peer_id
	box.custom_minimum_size = voice_box_size
	box.size_flags_horizontal = Control.SIZE_SHRINK_END
	var style := StyleBoxFlat.new()
	style.bg_color = VOICE_QUIET_COLOR
	style.border_color = Color(0.0, 0.0, 0.0, 0.95)
	style.set_border_width_all(1)
	style.set_corner_radius_all(3)
	box.add_theme_stylebox_override("panel", style)
	_voice_box_styles[peer_id] = style
	var label := Label.new()
	label.name = "Name"
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	label.add_theme_font_size_override("font_size", 18)
	label.add_theme_color_override("font_color", Color.WHITE)
	label.add_theme_color_override("font_outline_color", Color.BLACK)
	label.add_theme_constant_override("outline_size", 1)
	box.add_child(label)
	return box
