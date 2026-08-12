class_name ReplayInputDisplay extends Control

const RAW_BIT_PRECISION := 254.0
const STICK_SIZE := 112.0
const STRAFE_BAR_WIDTH := 18.0
const STRAFE_BAR_GAP := 8.0
const DOT_RADIUS := 2.0

var steer := Vector2.ZERO
var strafe_left := 0.0
var strafe_right := 0.0

func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	custom_minimum_size = Vector2(
		STICK_SIZE + (STRAFE_BAR_WIDTH + STRAFE_BAR_GAP) * 2.0,
		STICK_SIZE
	)
	queue_redraw()

func clear_input() -> void:
	_set_values(0.0, 0.0, 0.0, 0.0)

func set_input_bytes(data: PackedByteArray) -> void:
	if data.is_empty():
		clear_input()
		return
	var bitmask := int(data[0])
	var byte_index := 1
	var new_strafe_left := 0.0
	var new_strafe_right := 0.0
	var new_steer_x := 0.0
	var new_steer_y := 0.0
	if (bitmask & (1 << 0)) != 0 and byte_index < data.size():
		new_strafe_left = float(data[byte_index]) / RAW_BIT_PRECISION
		byte_index += 1
	if (bitmask & (1 << 1)) != 0 and byte_index < data.size():
		new_strafe_right = float(data[byte_index]) / RAW_BIT_PRECISION
		byte_index += 1
	if (bitmask & (1 << 2)) != 0 and byte_index < data.size():
		new_steer_x = (float(data[byte_index]) / RAW_BIT_PRECISION) * 2.0 - 1.0
		byte_index += 1
	if (bitmask & (1 << 3)) != 0 and byte_index < data.size():
		new_steer_y = (float(data[byte_index]) / RAW_BIT_PRECISION) * 2.0 - 1.0
	_set_values(new_steer_x, new_steer_y, new_strafe_left, new_strafe_right)

func _set_values(new_steer_x: float, new_steer_y: float, new_strafe_left: float, new_strafe_right: float) -> void:
	var next_steer := Vector2(
		clampf(new_steer_x, -1.0, 1.0),
		clampf(new_steer_y, -1.0, 1.0)
	)
	var next_strafe_left := clampf(new_strafe_left, 0.0, 1.0)
	var next_strafe_right := clampf(new_strafe_right, 0.0, 1.0)
	if next_steer == steer and next_strafe_left == strafe_left and next_strafe_right == strafe_right:
		return
	steer = next_steer
	strafe_left = next_strafe_left
	strafe_right = next_strafe_right
	queue_redraw()

func _draw_strafe_bar(rect: Rect2, value: float) -> void:
	draw_rect(rect, Color(0.04, 0.05, 0.06, 0.94), true)
	var fill_height := rect.size.y * value
	if fill_height > 0.0:
		draw_rect(
			Rect2(Vector2(rect.position.x, rect.end.y - fill_height), Vector2(rect.size.x, fill_height)),
			Color(0.92, 0.95, 1.0, 0.96),
			true
		)
	draw_rect(rect, Color(0.55, 0.66, 0.72, 0.9), false, 1.0)

func _draw() -> void:
	var content_width := STICK_SIZE + (STRAFE_BAR_WIDTH + STRAFE_BAR_GAP) * 2.0
	var origin := Vector2(
		maxf(0.0, (size.x - content_width) * 0.5),
		maxf(0.0, (size.y - STICK_SIZE) * 0.5)
	)
	var left_bar := Rect2(origin, Vector2(STRAFE_BAR_WIDTH, STICK_SIZE))
	var stick_rect := Rect2(
		origin + Vector2(STRAFE_BAR_WIDTH + STRAFE_BAR_GAP, 0.0),
		Vector2(STICK_SIZE, STICK_SIZE)
	)
	var right_bar := Rect2(
		Vector2(stick_rect.end.x + STRAFE_BAR_GAP, origin.y),
		Vector2(STRAFE_BAR_WIDTH, STICK_SIZE)
	)
	_draw_strafe_bar(left_bar, strafe_left)
	draw_rect(stick_rect, Color(0.04, 0.05, 0.06, 0.94), true)
	var stick_center := stick_rect.get_center()
	draw_line(Vector2(stick_center.x, stick_rect.position.y), Vector2(stick_center.x, stick_rect.end.y), Color(0.3, 0.34, 0.37, 0.75), 1.0)
	draw_line(Vector2(stick_rect.position.x, stick_center.y), Vector2(stick_rect.end.x, stick_center.y), Color(0.3, 0.34, 0.37, 0.75), 1.0)
	draw_rect(stick_rect, Color(0.55, 0.66, 0.72, 0.9), false, 1.0)
	var stick_range := STICK_SIZE * 0.5 - DOT_RADIUS - 2.0
	draw_circle(stick_center + steer * stick_range, DOT_RADIUS, Color.WHITE)
	_draw_strafe_bar(right_bar, strafe_right)
