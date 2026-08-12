class_name VehicleEditorCurveGraph extends Control

signal curve_committed(keys: Array)
signal key_selected(index: int)

var session: MxtCarAuthoringSession
var layer_name := "base"
var stat_name := "weight_kg"
var keys: Array = []
var sample_setting := 0.5
var selected_key := -1
var dragging_key := -1
var y_min := 0.0
var y_max := 1.0


func _ready() -> void:
	mouse_default_cursor_shape = Control.CURSOR_CROSS
	custom_minimum_size = Vector2(520.0, 260.0)


func show_curve(authoring_session: MxtCarAuthoringSession, layer: String, stat: String) -> void:
	session = authoring_session
	layer_name = layer
	stat_name = stat
	keys = session.get_curve(layer_name, stat_name) if layer_name != "s_boost" else [{
		"time": 0.0,
		"value": session.get_s_boost_value(stat_name),
		"tangent_in": 0.0,
		"tangent_out": 0.0,
	}]
	selected_key = 0 if !keys.is_empty() else -1
	_recalculate_range()
	queue_redraw()
	key_selected.emit(selected_key)


func set_sample_setting(value: float) -> void:
	sample_setting = clampf(value, 0.0, 1.0)
	queue_redraw()


func get_keys() -> Array:
	return keys.duplicate(true)


func set_keys(value: Array, commit := false) -> void:
	keys = value.duplicate(true)
	selected_key = clampi(selected_key, 0, keys.size() - 1) if !keys.is_empty() else -1
	_recalculate_range()
	queue_redraw()
	if commit:
		curve_committed.emit(get_keys())


func _draw() -> void:
	var rect := Rect2(Vector2(44.0, 14.0), size - Vector2(58.0, 42.0))
	draw_rect(rect, Color(0.025, 0.035, 0.05, 0.92), true)
	for i in range(11):
		var x := rect.position.x + rect.size.x * float(i) / 10.0
		draw_line(Vector2(x, rect.position.y), Vector2(x, rect.end.y), Color(0.2, 0.3, 0.42, 0.35), 1.0)
	for i in range(5):
		var y := rect.position.y + rect.size.y * float(i) / 4.0
		draw_line(Vector2(rect.position.x, y), Vector2(rect.end.x, y), Color(0.2, 0.3, 0.42, 0.35), 1.0)
	var sample_x := rect.position.x + rect.size.x * sample_setting
	draw_line(Vector2(sample_x, rect.position.y), Vector2(sample_x, rect.end.y), Color(1.0, 0.62, 0.18, 0.8), 2.0)
	if keys.is_empty():
		return
	var points := PackedVector2Array()
	for i in range(257):
		var t := float(i) / 256.0
		points.push_back(_graph_point(rect, t, _sample_local(t)))
	if points.size() >= 2:
		draw_polyline(points, Color(0.25, 0.68, 1.0), 2.5, true)
	for i in range(keys.size()):
		var key: Dictionary = keys[i]
		var point := _graph_point(rect, float(key.get("time", 0.0)), float(key.get("value", 0.0)))
		draw_circle(point, 7.0 if i == selected_key else 5.0, Color(1.0, 0.72, 0.18) if i == selected_key else Color(0.95, 0.3, 0.25))
	draw_string(get_theme_default_font(), Vector2(4.0, rect.position.y + 10.0), str(snappedf(y_max, 0.0001)), HORIZONTAL_ALIGNMENT_LEFT, 38.0, 12)
	draw_string(get_theme_default_font(), Vector2(4.0, rect.end.y), str(snappedf(y_min, 0.0001)), HORIZONTAL_ALIGNMENT_LEFT, 38.0, 12)
	draw_string(get_theme_default_font(), Vector2(rect.position.x, size.y - 8.0), "0.0", HORIZONTAL_ALIGNMENT_LEFT, 40.0, 12)
	draw_string(get_theme_default_font(), Vector2(rect.end.x - 36.0, size.y - 8.0), "1.0", HORIZONTAL_ALIGNMENT_RIGHT, 36.0, 12)


func _gui_input(event: InputEvent) -> void:
	if layer_name == "s_boost":
		return
	var button := event as InputEventMouseButton
	if button != null and button.button_index == MOUSE_BUTTON_LEFT:
		if button.pressed:
			dragging_key = _key_at_position(button.position)
			if dragging_key >= 0:
				selected_key = dragging_key
				key_selected.emit(selected_key)
				accept_event()
		else:
			if dragging_key >= 0:
				dragging_key = -1
				curve_committed.emit(get_keys())
				accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion == null or dragging_key < 0:
		return
	var rect := Rect2(Vector2(44.0, 14.0), size - Vector2(58.0, 42.0))
	var time := clampf((motion.position.x - rect.position.x) / rect.size.x, 0.0, 1.0)
	var value := y_max - clampf((motion.position.y - rect.position.y) / rect.size.y, 0.0, 1.0) * (y_max - y_min)
	if dragging_key > 0:
		time = maxf(time, float(keys[dragging_key - 1]["time"]) + 0.001)
	if dragging_key + 1 < keys.size():
		time = minf(time, float(keys[dragging_key + 1]["time"]) - 0.001)
	keys[dragging_key]["time"] = time
	keys[dragging_key]["value"] = value
	_recalculate_range()
	queue_redraw()
	key_selected.emit(selected_key)
	accept_event()


func _key_at_position(position: Vector2) -> int:
	var rect := Rect2(Vector2(44.0, 14.0), size - Vector2(58.0, 42.0))
	for i in range(keys.size() - 1, -1, -1):
		var key: Dictionary = keys[i]
		if position.distance_to(_graph_point(rect, float(key["time"]), float(key["value"]))) <= 11.0:
			return i
	return -1


func _graph_point(rect: Rect2, time: float, value: float) -> Vector2:
	var normalized_y := (value - y_min) / maxf(y_max - y_min, 0.000001)
	return Vector2(rect.position.x + time * rect.size.x, rect.end.y - normalized_y * rect.size.y)


func _sample_local(time: float) -> float:
	if keys.size() == 1:
		return float(keys[0]["value"])
	if time <= float(keys[0]["time"]):
		return float(keys[0]["value"])
	for i in range(keys.size() - 1):
		var left: Dictionary = keys[i]
		var right: Dictionary = keys[i + 1]
		if time > float(right["time"]):
			continue
		var duration := float(right["time"]) - float(left["time"])
		var u := (time - float(left["time"])) / duration
		var outgoing := float(left["value"]) + duration * float(left["tangent_out"]) / 3.0
		var incoming := float(right["value"]) - duration * float(right["tangent_in"]) / 3.0
		var inverse := 1.0 - u
		return float(left["value"]) * inverse * inverse * inverse + 3.0 * outgoing * inverse * inverse * u + 3.0 * incoming * inverse * u * u + float(right["value"]) * u * u * u
	return float(keys.back()["value"])


func _recalculate_range() -> void:
	if keys.is_empty():
		y_min = 0.0
		y_max = 1.0
		return
	y_min = INF
	y_max = -INF
	for i in range(257):
		var value := _sample_local(float(i) / 256.0)
		y_min = minf(y_min, value)
		y_max = maxf(y_max, value)
	var padding := maxf((y_max - y_min) * 0.12, maxf(absf(y_max), 1.0) * 0.05)
	y_min -= padding
	y_max += padding
