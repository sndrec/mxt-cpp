class_name TriggerCalibrationBar
extends Control

signal calibration_changed(action_name: String, low_value: float, high_value: float, committed: bool)

@export var action_name := ""

const HANDLE_THICKNESS := 8.0
const HANDLE_PICK_RADIUS := 12.0
const MIN_RANGE := 0.02

var low_value := 0.0
var high_value := 1.0
var raw_value := 0.0
var calibrated_value := 0.0

var _hover_handle := 0
var _drag_handle := 0

func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	queue_redraw()

func _get_minimum_size() -> Vector2:
	return Vector2(80.0, 260.0)

func set_range_values(new_low: float, new_high: float) -> void:
	low_value = clampf(new_low, 0.0, 1.0)
	high_value = clampf(new_high, 0.0, 1.0)
	if high_value < low_value + MIN_RANGE:
		high_value = minf(1.0, low_value + MIN_RANGE)
		low_value = minf(low_value, high_value - MIN_RANGE)
	queue_redraw()

func set_input_values(new_raw: float, new_calibrated: float) -> void:
	raw_value = clampf(new_raw, 0.0, 1.0)
	calibrated_value = clampf(new_calibrated, 0.0, 1.0)
	queue_redraw()

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if _drag_handle != 0:
			_set_handle_from_y(_drag_handle, motion.position.y, false)
			accept_event()
			return
		_hover_handle = _handle_at_y(motion.position.y)
		mouse_default_cursor_shape = Control.CURSOR_VSIZE if _hover_handle != 0 else Control.CURSOR_ARROW
		queue_redraw()
	elif event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index != MOUSE_BUTTON_LEFT:
			return
		if button.pressed:
			_hover_handle = _handle_at_y(button.position.y)
			if _hover_handle != 0:
				_drag_handle = _hover_handle
				accept_event()
		elif _drag_handle != 0:
			_set_handle_from_y(_drag_handle, button.position.y, true)
			_drag_handle = 0
			accept_event()

func _notification(what: int) -> void:
	if what == NOTIFICATION_MOUSE_EXIT and _drag_handle == 0:
		_hover_handle = 0
		mouse_default_cursor_shape = Control.CURSOR_ARROW
		queue_redraw()

func _draw() -> void:
	var bar := _bar_rect()
	if bar.size.x <= 0.0 or bar.size.y <= 0.0:
		return
	var low_y := _value_to_y(low_value)
	var high_y := _value_to_y(high_value)
	draw_rect(bar, Color(0.04, 0.05, 0.06, 0.9), true)
	draw_rect(Rect2(bar.position, Vector2(bar.size.x, maxf(0.0, high_y - bar.position.y))), Color(0.0, 0.0, 0.0, 0.5), true)
	draw_rect(Rect2(Vector2(bar.position.x, low_y), Vector2(bar.size.x, maxf(0.0, bar.end.y - low_y))), Color(0.0, 0.0, 0.0, 0.5), true)
	draw_rect(Rect2(Vector2(bar.position.x, high_y), Vector2(bar.size.x, maxf(0.0, low_y - high_y))), Color(0.12, 0.16, 0.18, 1.0), true)
	var fill_color := Color(1.0, 0.62, 0.62, 0.95) if calibrated_value >= 0.999 else Color(1.0, 1.0, 1.0, 0.88)
	var fill_top := lerpf(low_y, high_y, calibrated_value)
	draw_rect(Rect2(Vector2(bar.position.x, fill_top), Vector2(bar.size.x, maxf(0.0, low_y - fill_top))), fill_color, true)
	draw_rect(bar, Color(0.55, 0.66, 0.72, 0.85), false, 1.0)
	_draw_handle(low_y, -1)
	_draw_handle(high_y, 1)
	var raw_y := _value_to_y(raw_value)
	draw_line(Vector2(bar.position.x - 8.0, raw_y), Vector2(bar.end.x + 8.0, raw_y), Color.WHITE, 3.0)

func _draw_handle(y: float, handle: int) -> void:
	var bar := _bar_rect()
	var color := Color.WHITE if _hover_handle == handle or _drag_handle == handle else Color(0.1, 0.38, 1.0, 1.0)
	var rect := Rect2(Vector2(bar.position.x - 8.0, y - HANDLE_THICKNESS * 0.5), Vector2(bar.size.x + 16.0, HANDLE_THICKNESS))
	draw_rect(rect, color, true)

func _bar_rect() -> Rect2:
	var width := minf(46.0, maxf(24.0, size.x - 28.0))
	var height := maxf(0.0, size.y - 20.0)
	return Rect2(Vector2((size.x - width) * 0.5, 10.0), Vector2(width, height))

func _value_to_y(value: float) -> float:
	var bar := _bar_rect()
	return bar.position.y + (1.0 - clampf(value, 0.0, 1.0)) * bar.size.y

func _y_to_value(y: float) -> float:
	var bar := _bar_rect()
	if bar.size.y <= 0.0:
		return 0.0
	return clampf(1.0 - ((y - bar.position.y) / bar.size.y), 0.0, 1.0)

func _handle_at_y(y: float) -> int:
	var low_dist := absf(y - _value_to_y(low_value))
	var high_dist := absf(y - _value_to_y(high_value))
	if low_dist <= HANDLE_PICK_RADIUS and low_dist <= high_dist:
		return -1
	if high_dist <= HANDLE_PICK_RADIUS:
		return 1
	return 0

func _set_handle_from_y(handle: int, y: float, committed: bool) -> void:
	var value := _y_to_value(y)
	if handle < 0:
		low_value = clampf(value, 0.0, high_value - MIN_RANGE)
	elif handle > 0:
		high_value = clampf(value, low_value + MIN_RANGE, 1.0)
	calibration_changed.emit(action_name, low_value, high_value, committed)
	queue_redraw()
