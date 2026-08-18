class_name VehicleEditorCurveGraph extends Control

signal curve_committed(keys: Array)
signal curve_preview_changed(keys: Array)
signal edit_started
signal edit_cancelled
signal key_selected(index: int)

enum Interaction {
	IDLE,
	GRAB,
	ROTATE,
	SCALE,
	HANDLE_IN,
	HANDLE_OUT,
	PAN_Y,
}

const PREVIEW_DEBOUNCE_MSEC := 75
const TIME_SEPARATION := 0.00001
const HANDLE_RADIUS := 7.0
const KEY_HIT_RADIUS := 11.0
const MIN_VIEW_SPAN := 0.000000001
const MAX_VIEW_SPAN := 1.0e30

var session: MxtCarAuthoringSession
var layer_name := "base"
var stat_name := "weight_kg"
var keys: Array = []
var sample_setting := 0.5
var selected_key := -1
var selected_ids: Dictionary = {}
var active_id := -1
var next_id := 1
var interaction := Interaction.IDLE
var interaction_source: Array = []
var interaction_mouse_start := Vector2.ZERO
var interaction_center := Vector2.ZERO
var interaction_handle_id := -1
var interaction_changed := false
var last_mouse_position := Vector2.ZERO
var last_preview_msec := 0
var pan_source_min := 0.0
var pan_source_max := 1.0
var y_min := 0.0
var y_max := 1.0
var read_only := false
var derived_display := false
var unit_label := "scalar"


func _ready() -> void:
	focus_mode = Control.FOCUS_ALL
	mouse_default_cursor_shape = Control.CURSOR_CROSS
	custom_minimum_size = Vector2(520.0, 260.0)


func show_curve(authoring_session: MxtCarAuthoringSession, layer: String, stat: String) -> void:
	_cancel_local_interaction(false)
	session = authoring_session
	layer_name = layer
	stat_name = stat
	var source: Array = session.get_curve(layer_name, stat_name) if layer_name != "s_boost" else [{
		"time": 0.0,
		"value": session.get_s_boost_value(stat_name),
		"tangent_in": 0.0,
		"tangent_out": 0.0,
	}]
	keys = _keys_with_ids(source)
	selected_ids.clear()
	active_id = -1
	if !keys.is_empty():
		active_id = int(keys[0]["_editor_id"])
		selected_ids[active_id] = true
	_update_selected_index()
	_frame_y_view()
	queue_redraw()
	key_selected.emit(selected_key)


func set_display_context(is_read_only: bool, is_derived: bool, unit: String) -> void:
	read_only = is_read_only
	derived_display = is_derived
	unit_label = unit
	if read_only and interaction != Interaction.IDLE and interaction != Interaction.PAN_Y:
		_cancel_local_interaction(true)
	queue_redraw()


func set_sample_setting(value: float) -> void:
	sample_setting = clampf(value, 0.0, 1.0)
	queue_redraw()


func cancel_active_edit() -> void:
	if interaction in [Interaction.GRAB, Interaction.ROTATE, Interaction.SCALE, Interaction.HANDLE_IN, Interaction.HANDLE_OUT]:
		_cancel_local_interaction(true)


func get_keys() -> Array:
	var output: Array = []
	for key_value in keys:
		var key: Dictionary = key_value.duplicate(true)
		key.erase("_editor_id")
		output.append(key)
	return output


func set_keys(value: Array, commit := false) -> void:
	_cancel_local_interaction(false)
	keys = _keys_with_ids(value)
	selected_ids.clear()
	active_id = -1
	if !keys.is_empty():
		active_id = int(keys[0]["_editor_id"])
		selected_ids[active_id] = true
	_update_selected_index()
	_frame_y_view()
	queue_redraw()
	key_selected.emit(selected_key)
	if commit:
		curve_committed.emit(get_keys())


func _keys_with_ids(source: Array) -> Array:
	var output: Array = []
	for value in source:
		var key: Dictionary = Dictionary(value).duplicate(true)
		key["_editor_id"] = next_id
		next_id += 1
		output.append(key)
	_sort_keys(output)
	return output


func _sort_keys(target: Array = keys) -> void:
	target.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var a_time := float(a["time"])
		var b_time := float(b["time"])
		if a_time == b_time:
			return int(a["_editor_id"]) < int(b["_editor_id"])
		return a_time < b_time
	)


func _draw() -> void:
	var rect := _graph_rect()
	draw_rect(rect, Color(0.025, 0.035, 0.05, 0.92), true)
	for i in range(11):
		var x := rect.position.x + rect.size.x * float(i) / 10.0
		draw_line(Vector2(x, rect.position.y), Vector2(x, rect.end.y), Color(0.2, 0.3, 0.42, 0.35), 1.0)
	for i in range(5):
		var y := rect.position.y + rect.size.y * float(i) / 4.0
		draw_line(Vector2(rect.position.x, y), Vector2(rect.end.x, y), Color(0.2, 0.3, 0.42, 0.35), 1.0)
	_draw_reference_line(rect, 0.0, Color(0.65, 0.7, 0.8, 0.42), 1.5)
	_draw_reference_line(rect, 1.0, Color(0.42, 0.65, 0.48, 0.35), 1.0)
	var sample_x := rect.position.x + rect.size.x * sample_setting
	draw_line(Vector2(sample_x, rect.position.y), Vector2(sample_x, rect.end.y), Color(1.0, 0.62, 0.18, 0.8), 2.0)
	if !keys.is_empty():
		var points := PackedVector2Array()
		for i in range(257):
			var t := float(i) / 256.0
			points.push_back(_graph_point(rect, t, _sample_local(t)))
		if points.size() >= 2:
			var curve_colour := Color(0.68, 0.42, 1.0) if derived_display else Color(0.25, 0.68, 1.0)
			draw_polyline(points, curve_colour, 2.5, true)
		_draw_selected_handles(rect)
		for key_value in keys:
			var key: Dictionary = key_value
			var key_id := int(key["_editor_id"])
			var point := _graph_point(rect, float(key["time"]), float(key["value"]))
			var selected := selected_ids.has(key_id)
			var colour := Color(0.95, 0.3, 0.25)
			if selected:
				colour = Color(1.0, 0.72, 0.18) if key_id == active_id else Color(1.0, 0.48, 0.16)
			draw_circle(point, 7.0 if key_id == active_id else (6.0 if selected else 5.0), colour)
	var font := get_theme_default_font()
	draw_string(font, Vector2(4.0, rect.position.y + 10.0), _format_axis(y_max), HORIZONTAL_ALIGNMENT_LEFT, 48.0, 12)
	draw_string(font, Vector2(4.0, rect.end.y), _format_axis(y_min), HORIZONTAL_ALIGNMENT_LEFT, 48.0, 12)
	draw_string(font, Vector2(rect.position.x, size.y - 8.0), "0.0", HORIZONTAL_ALIGNMENT_LEFT, 40.0, 12)
	draw_string(font, Vector2(rect.end.x - 36.0, size.y - 8.0), "1.0", HORIZONTAL_ALIGNMENT_RIGHT, 36.0, 12)
	draw_string(font, Vector2(rect.end.x - 180.0, rect.position.y + 14.0), unit_label, HORIZONTAL_ALIGNMENT_RIGHT, 180.0, 12, Color(0.7, 0.78, 0.9))
	var state_text := _interaction_name()
	if derived_display:
		state_text = "DERIVED · " + state_text
	draw_string(font, Vector2(rect.position.x + 8.0, rect.position.y + 15.0), state_text, HORIZONTAL_ALIGNMENT_LEFT, 380.0, 12, Color(0.85, 0.72, 1.0) if derived_display else Color(0.72, 0.8, 0.9))


func _draw_reference_line(rect: Rect2, value: float, colour: Color, width: float) -> void:
	if value < y_min or value > y_max:
		return
	var y := _graph_point(rect, 0.0, value).y
	draw_line(Vector2(rect.position.x, y), Vector2(rect.end.x, y), colour, width)


func _draw_selected_handles(rect: Rect2) -> void:
	for i in range(keys.size()):
		var key: Dictionary = keys[i]
		if !selected_ids.has(int(key["_editor_id"])):
			continue
		var key_point := _graph_point(rect, float(key["time"]), float(key["value"]))
		if i > 0:
			var in_point := _handle_point(rect, i, true)
			draw_line(key_point, in_point, Color(0.35, 0.85, 0.95, 0.8), 1.5)
			draw_circle(in_point, HANDLE_RADIUS, Color(0.3, 0.95, 0.88) if interaction == Interaction.HANDLE_IN and interaction_handle_id == int(key["_editor_id"]) else Color(0.2, 0.65, 0.8))
		if i + 1 < keys.size():
			var out_point := _handle_point(rect, i, false)
			draw_line(key_point, out_point, Color(0.35, 0.85, 0.95, 0.8), 1.5)
			draw_circle(out_point, HANDLE_RADIUS, Color(0.3, 0.95, 0.88) if interaction == Interaction.HANDLE_OUT and interaction_handle_id == int(key["_editor_id"]) else Color(0.2, 0.65, 0.8))


func _gui_input(event: InputEvent) -> void:
	var mouse_motion := event as InputEventMouseMotion
	if mouse_motion != null:
		last_mouse_position = mouse_motion.position
		_handle_mouse_motion(mouse_motion)
		return
	var button := event as InputEventMouseButton
	if button != null:
		last_mouse_position = button.position
		_handle_mouse_button(button)
		return
	var key_event := event as InputEventKey
	if key_event != null and key_event.pressed and !key_event.echo:
		_handle_key(key_event)


func _handle_mouse_button(event: InputEventMouseButton) -> void:
	grab_focus()
	if interaction in [Interaction.GRAB, Interaction.ROTATE, Interaction.SCALE] and event.pressed:
		_commit_interaction()
		accept_event()
		return
	if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
		_zoom_y(event.position, 0.8)
		accept_event()
		return
	if event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
		_zoom_y(event.position, 1.25)
		accept_event()
		return
	if event.button_index == MOUSE_BUTTON_MIDDLE:
		if event.pressed:
			interaction = Interaction.PAN_Y
			interaction_mouse_start = event.position
			pan_source_min = y_min
			pan_source_max = y_max
		else:
			interaction = Interaction.IDLE
		queue_redraw()
		accept_event()
		return
	if event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
		_select_at(event.position, event.shift_pressed)
		accept_event()
		return
	if event.button_index != MOUSE_BUTTON_LEFT:
		return
	if event.pressed:
		if read_only or layer_name == "s_boost":
			return
		var handle := _handle_at_position(event.position)
		if !handle.is_empty():
			_begin_handle_drag(int(handle["id"]), bool(handle["incoming"]), event.position)
			accept_event()
	elif interaction in [Interaction.HANDLE_IN, Interaction.HANDLE_OUT]:
		_commit_interaction()
		accept_event()


func _handle_key(event: InputEventKey) -> void:
	if event.keycode == KEY_ESCAPE and interaction != Interaction.IDLE:
		_cancel_local_interaction(true)
		accept_event()
		return
	if event.keycode in [KEY_ENTER, KEY_KP_ENTER] and interaction in [Interaction.GRAB, Interaction.ROTATE, Interaction.SCALE]:
		_commit_interaction()
		accept_event()
		return
	if event.keycode == KEY_HOME:
		_frame_y_view()
		queue_redraw()
		accept_event()
		return
	if read_only or layer_name == "s_boost" or selected_ids.is_empty() or interaction != Interaction.IDLE:
		return
	if event.keycode == KEY_G:
		_begin_keyboard_transform(Interaction.GRAB)
	elif event.keycode == KEY_R:
		_begin_keyboard_transform(Interaction.ROTATE)
	elif event.keycode == KEY_S:
		_begin_keyboard_transform(Interaction.SCALE)
	else:
		return
	accept_event()


func _handle_mouse_motion(event: InputEventMouseMotion) -> void:
	if interaction == Interaction.PAN_Y:
		var span := pan_source_max - pan_source_min
		var delta := (event.position.y - interaction_mouse_start.y) * span / maxf(_graph_rect().size.y, 1.0)
		y_min = pan_source_min + delta
		y_max = pan_source_max + delta
		queue_redraw()
		accept_event()
		return
	if interaction == Interaction.GRAB:
		_apply_grab(event.position)
	elif interaction == Interaction.ROTATE:
		_apply_rotate(event.position)
	elif interaction == Interaction.SCALE:
		_apply_scale(event.position)
	elif interaction in [Interaction.HANDLE_IN, Interaction.HANDLE_OUT]:
		_apply_handle_drag(event.position)
	else:
		return
	_preview_interaction()
	accept_event()


func _select_at(position: Vector2, toggle: bool) -> void:
	var hit := _handle_at_position(position)
	var key_id := int(hit.get("id", -1))
	if key_id < 0:
		key_id = _key_id_at_position(position)
	if key_id < 0:
		if !toggle:
			selected_ids.clear()
			active_id = -1
	else:
		if toggle:
			if selected_ids.has(key_id):
				selected_ids.erase(key_id)
				if active_id == key_id:
					active_id = int(selected_ids.keys().back()) if !selected_ids.is_empty() else -1
			else:
				selected_ids[key_id] = true
				active_id = key_id
		else:
			selected_ids.clear()
			selected_ids[key_id] = true
			active_id = key_id
	_update_selected_index()
	queue_redraw()
	key_selected.emit(selected_key)


func _begin_keyboard_transform(kind: Interaction) -> void:
	interaction = kind
	interaction_source = keys.duplicate(true)
	interaction_center = _selection_center_screen(interaction_source)
	interaction_mouse_start = last_mouse_position
	if interaction_mouse_start.distance_to(interaction_center) < 8.0:
		interaction_mouse_start = interaction_center + Vector2(80.0, 0.0)
	last_preview_msec = 0
	interaction_changed = false
	edit_started.emit()
	queue_redraw()


func _begin_handle_drag(key_id: int, incoming: bool, position: Vector2) -> void:
	interaction = Interaction.HANDLE_IN if incoming else Interaction.HANDLE_OUT
	interaction_handle_id = key_id
	interaction_source = keys.duplicate(true)
	interaction_mouse_start = position
	last_preview_msec = 0
	interaction_changed = false
	edit_started.emit()
	queue_redraw()


func _apply_grab(position: Vector2) -> void:
	keys = interaction_source.duplicate(true)
	var rect := _graph_rect()
	var delta_time := (position.x - interaction_mouse_start.x) / maxf(rect.size.x, 1.0)
	var delta_value := -(position.y - interaction_mouse_start.y) * (y_max - y_min) / maxf(rect.size.y, 1.0)
	for key_value in keys:
		var key: Dictionary = key_value
		if selected_ids.has(int(key["_editor_id"])):
			key["time"] = float(key["time"]) + delta_time
			key["value"] = float(key["value"]) + delta_value
	_clamp_selected_x_as_group()
	_sort_keys()


func _apply_scale(position: Vector2) -> void:
	keys = interaction_source.duplicate(true)
	var start_vector := interaction_mouse_start - interaction_center
	var current_vector := position - interaction_center
	var factor := current_vector.length() / maxf(start_vector.length(), 0.0001)
	if start_vector.dot(current_vector) < 0.0:
		factor = -factor
	var center_data := _data_point(interaction_center, false)
	for key_value in keys:
		var key: Dictionary = key_value
		if !selected_ids.has(int(key["_editor_id"])):
			continue
		key["time"] = center_data.x + (float(key["time"]) - center_data.x) * factor
		key["value"] = center_data.y + (float(key["value"]) - center_data.y) * factor
	_clamp_selected_x_as_group()
	_sort_keys()


func _apply_rotate(position: Vector2) -> void:
	keys = interaction_source.duplicate(true)
	var start_vector := interaction_mouse_start - interaction_center
	var current_vector := position - interaction_center
	var angle := start_vector.angle_to(current_vector)
	var rect := _graph_rect()
	for key_value in keys:
		var key: Dictionary = key_value
		if !selected_ids.has(int(key["_editor_id"])):
			continue
		var source_point := _graph_point(rect, float(key["time"]), float(key["value"]))
		var rotated := interaction_center + (source_point - interaction_center).rotated(angle)
		var data := _data_point(rotated, false)
		key["time"] = data.x
		key["value"] = data.y
		key["tangent_in"] = _rotate_tangent(float(key["tangent_in"]), angle, rect)
		key["tangent_out"] = _rotate_tangent(float(key["tangent_out"]), angle, rect)
	_clamp_selected_x_as_group()
	_sort_keys()


func _rotate_tangent(tangent: float, angle: float, rect: Rect2) -> float:
	var span := maxf(y_max - y_min, MIN_VIEW_SPAN)
	var direction := Vector2(rect.size.x, -tangent * rect.size.y / span).rotated(angle)
	if absf(direction.x) < 0.000001:
		return signf(-direction.y) * 1.0e20
	return clampf(-direction.y * span * rect.size.x / (direction.x * rect.size.y), -1.0e20, 1.0e20)


func _apply_handle_drag(position: Vector2) -> void:
	keys = interaction_source.duplicate(true)
	var index := _index_for_id(interaction_handle_id)
	if index < 0:
		return
	var key: Dictionary = keys[index]
	var data := _data_point(position, false)
	if interaction == Interaction.HANDLE_IN:
		var denominator := float(key["time"]) - minf(data.x, float(key["time"]) - 0.000001)
		key["tangent_in"] = clampf((float(key["value"]) - data.y) / denominator, -1.0e20, 1.0e20)
	else:
		var denominator := maxf(data.x, float(key["time"]) + 0.000001) - float(key["time"])
		key["tangent_out"] = clampf((data.y - float(key["value"])) / denominator, -1.0e20, 1.0e20)


func _preview_interaction() -> void:
	interaction_changed = true
	_update_selected_index()
	queue_redraw()
	key_selected.emit(selected_key)
	var now := Time.get_ticks_msec()
	if last_preview_msec == 0 or now >= last_preview_msec + PREVIEW_DEBOUNCE_MSEC:
		last_preview_msec = now
		curve_preview_changed.emit(get_keys())


func _commit_interaction() -> void:
	if interaction == Interaction.IDLE or interaction == Interaction.PAN_Y:
		return
	if !interaction_changed:
		_cancel_local_interaction(true)
		return
	_separate_equal_times()
	_sort_keys()
	_update_selected_index()
	interaction = Interaction.IDLE
	interaction_source.clear()
	interaction_handle_id = -1
	interaction_changed = false
	curve_committed.emit(get_keys())
	key_selected.emit(selected_key)
	queue_redraw()


func _cancel_local_interaction(notify_session: bool) -> void:
	if interaction in [Interaction.GRAB, Interaction.ROTATE, Interaction.SCALE, Interaction.HANDLE_IN, Interaction.HANDLE_OUT]:
		keys = interaction_source.duplicate(true)
		_sort_keys()
		_update_selected_index()
		if notify_session:
			edit_cancelled.emit()
	interaction = Interaction.IDLE
	interaction_source.clear()
	interaction_handle_id = -1
	interaction_changed = false
	queue_redraw()
	key_selected.emit(selected_key)


func _separate_equal_times() -> void:
	_sort_keys()
	for _pass in range(maxi(keys.size() * 2, 1)):
		var changed := false
		for i in range(1, keys.size()):
			var left: Dictionary = keys[i - 1]
			var right: Dictionary = keys[i]
			if float(right["time"]) - float(left["time"]) >= TIME_SEPARATION:
				continue
			if selected_ids.has(int(right["_editor_id"])):
				right["time"] = minf(1.0, float(left["time"]) + TIME_SEPARATION)
				changed = true
			elif selected_ids.has(int(left["_editor_id"])):
				left["time"] = maxf(0.0, float(right["time"]) - TIME_SEPARATION)
				changed = true
		if !changed:
			break
		_sort_keys()


func _clamp_selected_x_as_group() -> void:
	var minimum := INF
	var maximum := -INF
	for key_value in keys:
		var key: Dictionary = key_value
		if selected_ids.has(int(key["_editor_id"])):
			minimum = minf(minimum, float(key["time"]))
			maximum = maxf(maximum, float(key["time"]))
	if minimum == INF:
		return
	var shift := 0.0
	if minimum < 0.0:
		shift = -minimum
	if maximum + shift > 1.0:
		shift += 1.0 - (maximum + shift)
	if shift == 0.0:
		return
	for key_value in keys:
		var key: Dictionary = key_value
		if selected_ids.has(int(key["_editor_id"])):
			key["time"] = float(key["time"]) + shift


func _zoom_y(position: Vector2, factor: float) -> void:
	var rect := _graph_rect()
	if !rect.has_point(position):
		return
	var old_span := maxf(y_max - y_min, MIN_VIEW_SPAN)
	var new_span := clampf(old_span * factor, MIN_VIEW_SPAN, MAX_VIEW_SPAN)
	var ratio := clampf((position.y - rect.position.y) / maxf(rect.size.y, 1.0), 0.0, 1.0)
	var anchor := y_max - ratio * old_span
	y_max = anchor + ratio * new_span
	y_min = y_max - new_span
	queue_redraw()


func _frame_y_view() -> void:
	if keys.is_empty():
		y_min = 0.0
		y_max = 1.0
		return
	var minimum := INF
	var maximum := -INF
	for i in range(257):
		var value := _sample_local(float(i) / 256.0)
		minimum = minf(minimum, value)
		maximum = maxf(maximum, value)
	for key_value in keys:
		minimum = minf(minimum, float(key_value["value"]))
		maximum = maxf(maximum, float(key_value["value"]))
	var padding := maxf((maximum - minimum) * 0.12, maxf(absf(maximum), 1.0) * 0.05)
	y_min = minimum - padding
	y_max = maximum + padding
	if y_max - y_min < MIN_VIEW_SPAN:
		y_min -= 0.5
		y_max += 0.5


func _selection_center_screen(source: Array) -> Vector2:
	var minimum := Vector2(INF, INF)
	var maximum := Vector2(-INF, -INF)
	var rect := _graph_rect()
	for key_value in source:
		var key: Dictionary = key_value
		if !selected_ids.has(int(key["_editor_id"])):
			continue
		var point := _graph_point(rect, float(key["time"]), float(key["value"]))
		minimum.x = minf(minimum.x, point.x)
		minimum.y = minf(minimum.y, point.y)
		maximum.x = maxf(maximum.x, point.x)
		maximum.y = maxf(maximum.y, point.y)
	return (minimum + maximum) * 0.5


func _handle_at_position(position: Vector2) -> Dictionary:
	var rect := _graph_rect()
	for i in range(keys.size() - 1, -1, -1):
		var key: Dictionary = keys[i]
		var key_id := int(key["_editor_id"])
		if !selected_ids.has(key_id):
			continue
		if i > 0 and position.distance_to(_handle_point(rect, i, true)) <= HANDLE_RADIUS + 4.0:
			return {"id": key_id, "incoming": true}
		if i + 1 < keys.size() and position.distance_to(_handle_point(rect, i, false)) <= HANDLE_RADIUS + 4.0:
			return {"id": key_id, "incoming": false}
	return {}


func _handle_point(rect: Rect2, index: int, incoming: bool) -> Vector2:
	var key: Dictionary = keys[index]
	if incoming:
		var duration := float(key["time"]) - float(keys[index - 1]["time"])
		return _graph_point(rect, float(key["time"]) - duration / 3.0, float(key["value"]) - float(key["tangent_in"]) * duration / 3.0)
	var duration := float(keys[index + 1]["time"]) - float(key["time"])
	return _graph_point(rect, float(key["time"]) + duration / 3.0, float(key["value"]) + float(key["tangent_out"]) * duration / 3.0)


func _key_id_at_position(position: Vector2) -> int:
	var rect := _graph_rect()
	for i in range(keys.size() - 1, -1, -1):
		var key: Dictionary = keys[i]
		if position.distance_to(_graph_point(rect, float(key["time"]), float(key["value"]))) <= KEY_HIT_RADIUS:
			return int(key["_editor_id"])
	return -1


func _index_for_id(key_id: int) -> int:
	for i in range(keys.size()):
		if int(keys[i]["_editor_id"]) == key_id:
			return i
	return -1


func _update_selected_index() -> void:
	selected_key = _index_for_id(active_id)


func _graph_rect() -> Rect2:
	return Rect2(Vector2(54.0, 20.0), Vector2(maxf(size.x - 70.0, 1.0), maxf(size.y - 50.0, 1.0)))


func _graph_point(rect: Rect2, time: float, value: float) -> Vector2:
	var normalized_y := (value - y_min) / maxf(y_max - y_min, MIN_VIEW_SPAN)
	return Vector2(rect.position.x + time * rect.size.x, rect.end.y - normalized_y * rect.size.y)


func _data_point(point: Vector2, clamp_time: bool) -> Vector2:
	var rect := _graph_rect()
	var time := (point.x - rect.position.x) / maxf(rect.size.x, 1.0)
	if clamp_time:
		time = clampf(time, 0.0, 1.0)
	var value := y_max - (point.y - rect.position.y) * (y_max - y_min) / maxf(rect.size.y, 1.0)
	return Vector2(time, value)


func _sample_local(time: float) -> float:
	if keys.is_empty():
		return 0.0
	if keys.size() == 1 or time <= float(keys[0]["time"]):
		return float(keys[0]["value"])
	for i in range(keys.size() - 1):
		var left: Dictionary = keys[i]
		var right: Dictionary = keys[i + 1]
		if time > float(right["time"]):
			continue
		var duration := float(right["time"]) - float(left["time"])
		if duration <= 0.0:
			return float(right["value"])
		var u := (time - float(left["time"])) / duration
		var outgoing := float(left["value"]) + duration * float(left["tangent_out"]) / 3.0
		var incoming := float(right["value"]) - duration * float(right["tangent_in"]) / 3.0
		var inverse := 1.0 - u
		return float(left["value"]) * inverse * inverse * inverse + 3.0 * outgoing * inverse * inverse * u + 3.0 * incoming * inverse * u * u + float(right["value"]) * u * u * u
	return float(keys.back()["value"])


func _format_axis(value: float) -> String:
	var magnitude := absf(value)
	if magnitude >= 100000.0 or (magnitude > 0.0 and magnitude < 0.0001):
		return "%.3e" % value
	return str(snappedf(value, 0.0001))


func _interaction_name() -> String:
	match interaction:
		Interaction.GRAB: return "G · Grab · click/Enter confirm · Esc cancel"
		Interaction.ROTATE: return "R · Rotate · click/Enter confirm · Esc cancel"
		Interaction.SCALE: return "S · Scale · click/Enter confirm · Esc cancel"
		Interaction.HANDLE_IN: return "Incoming tangent"
		Interaction.HANDLE_OUT: return "Outgoing tangent"
		Interaction.PAN_Y: return "Pan Y"
	return "RMB select · Shift+RMB multi · G/R/S · wheel zoom · MMB pan · Home frame"
