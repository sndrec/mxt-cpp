class_name TrackEditorRadialMenu extends Control

signal finished(result : Dictionary)

const BUTTON_SIZE := Vector2(64.0, 64.0)
const RADIUS := 112.0
const EDGE_MARGIN := 8.0
const ANIM_TIME := 0.1

var menu_root := Control.new()
var menu_center := Vector2.ZERO
var current_options : Array = []
var is_closing := false
var active_tween : Tween

func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	set_anchors_preset(Control.PRESET_FULL_RECT)
	size = get_viewport_rect().size
	menu_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(menu_root)

func open_options(options : Array, screen_position : Vector2) -> void:
	current_options = options
	menu_center = _clamped_center(screen_position, options)
	_rebuild_buttons()
	menu_root.scale = Vector2.ZERO
	_play_scale_animation(Vector2.ONE, Tween.EASE_IN)

func transition_options(options : Array) -> void:
	if is_closing:
		return
	current_options = options
	await _animate_scale(Vector2.ZERO, Tween.EASE_OUT)
	if is_closing:
		return
	menu_center = _clamped_center(menu_center, options)
	_rebuild_buttons()
	menu_root.scale = Vector2.ZERO
	await _animate_scale(Vector2.ONE, Tween.EASE_IN)

func close_menu() -> void:
	if is_closing:
		return
	is_closing = true
	await _animate_scale(Vector2.ZERO, Tween.EASE_OUT)
	queue_free()

func cancel_menu() -> void:
	if is_closing:
		return
	finished.emit({
		"cancelled": true,
		"value": null,
	})
	await close_menu()

func _notification(what : int) -> void:
	if what == NOTIFICATION_RESIZED:
		size = get_viewport_rect().size

func _gui_input(event : InputEvent) -> void:
	if is_closing:
		return
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT or event.button_index == MOUSE_BUTTON_RIGHT:
			if !_point_hits_button(event.position):
				accept_event()
				cancel_menu()

func _unhandled_input(event : InputEvent) -> void:
	if is_closing:
		return
	if event.is_action_pressed("ui_cancel"):
		cancel_menu()

func _rebuild_buttons() -> void:
	for child in menu_root.get_children():
		child.queue_free()
	menu_root.position = menu_center
	menu_root.size = Vector2.ZERO
	menu_root.pivot_offset = Vector2.ZERO
	for i in current_options.size():
		var option : Dictionary = current_options[i]
		var button := Button.new()
		button.custom_minimum_size = BUTTON_SIZE
		button.size = BUTTON_SIZE
		button.text = String(option.get("label", ""))
		button.tooltip_text = String(option.get("tooltip", button.text))
		button.mouse_filter = Control.MOUSE_FILTER_STOP
		button.focus_mode = Control.FOCUS_NONE
		button.position = _option_offset(i, current_options.size()) - BUTTON_SIZE * 0.5
		button.pressed.connect(_on_button_pressed.bind(option.get("value", null)))
		menu_root.add_child(button)

func _on_button_pressed(value : Variant) -> void:
	_set_buttons_disabled(true)
	finished.emit({
		"cancelled": false,
		"value": value,
	})

func _set_buttons_disabled(disabled : bool) -> void:
	for child in menu_root.get_children():
		var button := child as Button
		if button:
			button.disabled = disabled

func _animate_scale(target_scale : Vector2, ease : Tween.EaseType) -> void:
	var tween := _play_scale_animation(target_scale, ease)
	await tween.finished

func _play_scale_animation(target_scale : Vector2, ease : Tween.EaseType) -> Tween:
	if active_tween:
		active_tween.kill()
	active_tween = create_tween()
	active_tween.set_trans(Tween.TRANS_QUAD)
	active_tween.set_ease(ease)
	active_tween.tween_property(menu_root, "scale", target_scale, ANIM_TIME)
	return active_tween

func _point_hits_button(point : Vector2) -> bool:
	for child in menu_root.get_children():
		var control := child as Control
		if control and control.get_global_rect().has_point(point):
			return true
	return false

func _clamped_center(requested_center : Vector2, options : Array) -> Vector2:
	var viewport_size := get_viewport_rect().size
	var bounds := _option_bounds(options.size())
	var min_center := Vector2(
		-bounds.position.x + EDGE_MARGIN,
		-bounds.position.y + EDGE_MARGIN)
	var max_center := Vector2(
		viewport_size.x - bounds.end.x - EDGE_MARGIN,
		viewport_size.y - bounds.end.y - EDGE_MARGIN)
	if max_center.x < min_center.x:
		requested_center.x = viewport_size.x * 0.5
	else:
		requested_center.x = clampf(requested_center.x, min_center.x, max_center.x)
	if max_center.y < min_center.y:
		requested_center.y = viewport_size.y * 0.5
	else:
		requested_center.y = clampf(requested_center.y, min_center.y, max_center.y)
	return requested_center

func _option_bounds(option_count : int) -> Rect2:
	if option_count <= 0:
		return Rect2(Vector2.ZERO, Vector2.ZERO)
	var min_pos := Vector2(INF, INF)
	var max_pos := Vector2(-INF, -INF)
	for i in option_count:
		var position := _option_offset(i, option_count)
		min_pos = min_pos.min(position - BUTTON_SIZE * 0.5)
		max_pos = max_pos.max(position + BUTTON_SIZE * 0.5)
	return Rect2(min_pos, max_pos - min_pos)

func _option_offset(index : int, option_count : int) -> Vector2:
	if option_count <= 1:
		return Vector2.ZERO
	var angle := -PI * 0.5 + TAU * float(index) / float(option_count)
	return Vector2(cos(angle), sin(angle)) * RADIUS
