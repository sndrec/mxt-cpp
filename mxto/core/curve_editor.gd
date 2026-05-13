class_name CurveEditor extends Control

var current_action_target := 0
var dragging := false
var angling := false
var panning := false
var zooming := false
var zoom_anchor := Vector2.ZERO
var zoom := Vector2(0.8, 0.8)
var pan := Vector2(0.1, -0.1)

@onready var check_button: CheckButton = $HBoxContainer2/HBoxContainer/CheckButton
@onready var spin_box: SpinBox = $HBoxContainer2/HBoxContainer/SpinBox
@onready var spin_box_2: SpinBox = $HBoxContainer2/HBoxContainer/SpinBox2
@onready var point_position_label: Label = $HBoxContainer2/HBoxContainer/PointPositionLabel
@onready var copy_curve_button: Button = $HBoxContainer2/VBoxContainer/CopyCurveButton
@onready var paste_curve_button: Button = $HBoxContainer2/VBoxContainer/PasteCurveButton

signal curve_edited

var prev_mouse_input := Vector2.ZERO

@export var associated_curve : Resource:
	set(new_curve):
		associated_curve = new_curve
		update_handles()

var wd := false
var wu := false

func _ready() -> void:
	if !associated_curve:
		associated_curve = ClassDB.instantiate("TrackEditorFloatCurve")
	copy_curve_button.pressed.connect(copy_curve)
	paste_curve_button.pressed.connect(paste_curve)

func copy_curve() -> void:
	FZGlobal.curve_clipboard = associated_curve.duplicate(true)

func paste_curve() -> void:
	if !FZGlobal.curve_clipboard:
		return
	associated_curve.clear_points()
	var in_c = FZGlobal.curve_clipboard
	for point in FZGlobal.curve_clipboard.point_count:
		associated_curve.add_point(in_c.get_point_position(point), in_c.get_point_left_tangent(point), in_c.get_point_right_tangent(point))
	curve_edited.emit()

func _input(event: InputEvent) -> void:
	if !is_visible_in_tree():
		return
	if event is InputEventMouseButton:
		var mouse_pos := get_local_mouse_position()
		if mouse_pos.x < 0.0 or mouse_pos.y < 0.0 or mouse_pos.x > size.x or mouse_pos.y > size.y:
			return
		if event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			wd = true
			get_viewport().set_input_as_handled()
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
			wu = true
			get_viewport().set_input_as_handled()

func update_handles() -> void:
	pass

var right_clicked := false
var left_clicking := false

func _process(delta: float) -> void:
	
	if !is_visible_in_tree():
		return
	
	
	var lmb := Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT)
	var mmb := Input.is_mouse_button_pressed(MOUSE_BUTTON_MIDDLE)
	var rmb := Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT)
	var shift := Input.is_key_pressed(KEY_SHIFT)
	var alt := Input.is_key_pressed(KEY_ALT)
	
	var mouse_pos := get_local_mouse_position()
	var should_draw = true
	var oob := mouse_pos.x < 0.0 or mouse_pos.y < 0.0 or mouse_pos.x > size.x or mouse_pos.y > size.y
	if oob:
		lmb = false
		rmb = false
		alt = false
	
	var just_left_clicked := lmb and !left_clicking
	var just_left_released := !Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) and left_clicking
	left_clicking = Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT)
	
	if oob and !dragging:
		return
	
	if wd or wu:
		var closest_point := 0
		var closest_point_dist := 10000000000.0
		for p in associated_curve.point_count:
			var point := transform_point(associated_curve.get_point_position(p))
			var dist := point.distance_squared_to(mouse_pos)
			if dist < closest_point_dist:
				closest_point_dist = dist
				closest_point = p
		var tan : float = associated_curve.get_point_left_tangent(closest_point)
		var amt = spin_box_2.value
		if wu:
			amt = -spin_box_2.value
		associated_curve.set_point_left_tangent(closest_point, tan + amt)
		associated_curve.set_point_right_tangent(closest_point, tan + amt)
		curve_edited.emit()
		wd = false
		wu = false
	
	var snapping := check_button.pressed
	var snap := spin_box.value
	var mouse_motion := mouse_pos - prev_mouse_input
	mouse_motion.x *= size.y / size.x
	var was_zooming := zooming
	if mmb:
		panning = !shift
		zooming = shift
	else:
		panning = false
		zooming = false
	
	if zooming and !was_zooming:
		zoom_anchor = mouse_pos
	
	var right_was_clicked := right_clicked
	
	if rmb:
		right_clicked = true
	else:
		right_clicked = false
	
	if right_clicked and !right_was_clicked:
		var closest_point := 0
		var closest_point_dist := 10000000000.0
		for p in associated_curve.point_count:
			var point := transform_point(associated_curve.get_point_position(p))
			var dist := point.distance_squared_to(mouse_pos)
			if dist < closest_point_dist:
				closest_point_dist = dist
				closest_point = p
		associated_curve.remove_point(closest_point)
	
	if alt:
		if just_left_clicked:
			var point_pos := inverse_transform_point(mouse_pos)
			if snapping:
				point_pos = snapped(point_pos, Vector2(snap, snap))
			associated_curve.add_point(point_pos, 0, 0, 0, 0)
	else:
		var was_dragging := dragging
		if just_left_clicked:
			dragging = true
		if just_left_released:
			dragging = false
		
		if dragging and !was_dragging:
			var closest_point := 0
			var closest_point_dist := 10000000000.0
			for p in associated_curve.point_count:
				var point := transform_point(associated_curve.get_point_position(p))
				var dist := point.distance_squared_to(mouse_pos)
				if dist < closest_point_dist:
					closest_point_dist = dist
					closest_point = p
			current_action_target = closest_point
		
		if !dragging and was_dragging:
			var snapped_point : Vector2 = snapped(associated_curve.get_point_position(current_action_target), Vector2(spin_box.value, spin_box.value))
			associated_curve.set_point_offset(current_action_target, snapped_point.x)
			associated_curve.set_point_value(current_action_target, snapped_point.y)
			curve_edited.emit()
		
		if dragging:
			var p : Vector2 = associated_curve.get_point_position(current_action_target)
			var change := mouse_motion / zoom / size
			change.x *= size.x / size.y
			associated_curve.set_point_offset(current_action_target, p.x + change.x)
			associated_curve.set_point_value(current_action_target, p.y - change.y)
			curve_edited.emit()
	
	
	
	if panning:
		pan += mouse_motion * 1.0 / zoom / size
	if zooming:
		zoom += mouse_motion * zoom * 0.003 * Vector2(1, -1)
		pan += (mouse_motion * -0.003 / zoom) * (zoom_anchor / size) * Vector2(1, -1)
	
	prev_mouse_input = mouse_pos
	
	if should_draw:
		queue_redraw()

func transform_point(in_point : Vector2) -> Vector2:
	return ((in_point * Vector2(1.0, -1.0) + Vector2(0.0, 1.0)) + pan) * zoom * size

func inverse_transform_point(in_point : Vector2) -> Vector2:
	return (((in_point / zoom / size) - pan) - Vector2(0.0, 1.0)) * Vector2(1.0, -1.0)

func _draw() -> void:
	var snapping := check_button.pressed
	var snap := spin_box.value
	var target := minf(current_action_target, associated_curve.point_count - 1)
	var closest_point := target
	var closest_point_dist := 10000000000.0
	if !dragging:
		for p in associated_curve.point_count:
			var point := transform_point(associated_curve.get_point_position(p))
			var dist := point.distance_squared_to(get_local_mouse_position())
			if dist < closest_point_dist:
				closest_point_dist = dist
				closest_point = p
	
	for point in associated_curve.point_count:
		var p_pos : Vector2 = associated_curve.get_point_position(point)
		var p_l_tan : float = associated_curve.get_point_left_tangent(point)
		var p_r_tan : float = associated_curve.get_point_right_tangent(point)
		var tr_point := transform_point(p_pos)
		var tr_l_tan := transform_point(p_pos + Vector2(-0.05, -p_l_tan * 0.05))
		var tr_r_tan := transform_point(p_pos + Vector2(0.05, p_r_tan * 0.05))
		var tr_point_snapped := transform_point(snapped(p_pos, Vector2(spin_box.value, spin_box.value)))
		if Rect2(Vector2.ZERO, size).grow(0.001).has_point(tr_point):
			var use_color := Color.WHITE
			var use_size := 3.0
			if point == closest_point:
				use_color = Color.DEEP_SKY_BLUE
				use_size = 6.0
				if check_button.pressed:
					var snapped_p : Vector2 = snapped(associated_curve.get_point_position(point), Vector2(spin_box.value, spin_box.value))
					point_position_label.text = "Point Position: " + str(snapped_p)
				else:
					var p : Vector2 = associated_curve.get_point_position(point)
					point_position_label.text = "Point Position: " + str(p)
			draw_circle(tr_point, use_size, use_color, true, -1.0, true)
			draw_circle(tr_l_tan, use_size * 0.6, use_color, true, -1.0, true)
			draw_circle(tr_r_tan, use_size * 0.6, use_color, true, -1.0, true)
			draw_line(tr_l_tan, tr_r_tan, use_color, 0.8, true)
			if dragging and point == target:
				draw_circle(tr_point_snapped, use_size, Color.MAROON, true, -1.0, true)
	if Input.is_key_pressed(KEY_ALT):
		var point_pos := inverse_transform_point(get_local_mouse_position())
		if snapping:
			point_pos = snapped(point_pos, Vector2(snap, snap))
		draw_circle(transform_point(point_pos), 8.0, Color.CRIMSON, true, -1.0, true)
	
	var sampled_points : PackedVector2Array = associated_curve.build_sampled_points(257)
	for i in sampled_points.size() - 1:
		var tr_point_1 := transform_point(sampled_points[i])
		var tr_point_2 := transform_point(sampled_points[i + 1])
		if Rect2(Vector2.ZERO, size).has_point(tr_point_1) or Rect2(Vector2.ZERO, size).has_point(tr_point_2):
			draw_line(tr_point_1, tr_point_2, Color.WHITE, 1.0, true)
	
	var corner_1 := transform_point(Vector2(0, 0))
	var corner_2 := transform_point(Vector2(1, 1))
	var mid_point := transform_point(Vector2(0.5, 0.0))
	
	if mid_point.y >= 0.0 and mid_point.y <= size.y:
		draw_line(Vector2(0, mid_point.y), Vector2(size.x, mid_point.y), Color.WHITE * 0.5, 1.0, true)
	
	if corner_1.x >= 0.0:
		draw_line(Vector2(corner_1.x, 0), Vector2(corner_1.x, size.y), Color.WHITE, 1.0, true)
	if corner_2.x <= size.x:
		draw_line(Vector2(corner_2.x, 0), Vector2(corner_2.x, size.y), Color.WHITE, 1.0, true)
	
