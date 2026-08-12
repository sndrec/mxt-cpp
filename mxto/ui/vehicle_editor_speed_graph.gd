class_name VehicleEditorSpeedGraph extends Control

var speeds := PackedFloat32Array()
var terminal_speed := 0.0
var peak_speed := 0.0


func _ready() -> void:
	custom_minimum_size = Vector2(360.0, 180.0)


func show_result(result: Dictionary) -> void:
	speeds = result.get("speeds_kmh", PackedFloat32Array())
	terminal_speed = float(result.get("terminal_speed_kmh", 0.0))
	peak_speed = float(result.get("peak_speed_kmh", 0.0))
	queue_redraw()


func _draw() -> void:
	var rect := Rect2(Vector2(42.0, 12.0), size - Vector2(54.0, 36.0))
	draw_rect(rect, Color(0.025, 0.035, 0.05, 0.92), true)
	for i in range(6):
		var x := rect.position.x + rect.size.x * float(i) / 5.0
		draw_line(Vector2(x, rect.position.y), Vector2(x, rect.end.y), Color(0.2, 0.3, 0.42, 0.35))
	for i in range(5):
		var y := rect.position.y + rect.size.y * float(i) / 4.0
		draw_line(Vector2(rect.position.x, y), Vector2(rect.end.x, y), Color(0.2, 0.3, 0.42, 0.35))
	if speeds.size() < 2:
		return
	var maximum := maxf(peak_speed * 1.08, 10.0)
	var points := PackedVector2Array()
	for i in range(speeds.size()):
		points.push_back(Vector2(
			rect.position.x + rect.size.x * float(i) / float(speeds.size() - 1),
			rect.end.y - rect.size.y * clampf(speeds[i] / maximum, 0.0, 1.0)))
	draw_polyline(points, Color(0.25, 0.68, 1.0), 2.0, true)
	var terminal_y := rect.end.y - rect.size.y * clampf(terminal_speed / maximum, 0.0, 1.0)
	draw_dashed_line(Vector2(rect.position.x, terminal_y), Vector2(rect.end.x, terminal_y), Color(1.0, 0.46, 0.16), 1.0, 6.0)
	draw_string(get_theme_default_font(), Vector2(2.0, rect.position.y + 10.0), "%.0f" % maximum, HORIZONTAL_ALIGNMENT_LEFT, 36.0, 12)
	draw_string(get_theme_default_font(), Vector2(rect.position.x, size.y - 6.0), "0 s", HORIZONTAL_ALIGNMENT_LEFT, 40.0, 12)
	draw_string(get_theme_default_font(), Vector2(rect.end.x - 34.0, size.y - 6.0), "30 s", HORIZONTAL_ALIGNMENT_RIGHT, 34.0, 12)
