extends Control

@onready var close_button: Button = $HBoxContainer/Control/Button
@onready var input_square: ColorRect = $HBoxContainer/Control/SteerAndPitchInputSquare
@onready var input_dot: TextureRect = $HBoxContainer/Control/SteerAndPitchInputSquare/ControllerInputDot
@onready var input_dot_calibrated: TextureRect = $HBoxContainer/Control/SteerAndPitchInputSquare/ControllerInputDotCalibrated
@onready var input_area: TextureRect = get_node_or_null("HBoxContainer/Control/SteerAndPitchInputSquare/ControllerInputArea") as TextureRect
@onready var octagon_line: Line2D = $HBoxContainer/Control/SteerAndPitchInputSquare/Line2D
@onready var calibrate_button: Button = $HBoxContainer/Control/CalibrateButton
@onready var deadzone_label: Label = $HBoxContainer/Control/Label
@onready var deadzone_slider: HSlider = $HBoxContainer/Control/HBoxContainer/HSlider
@onready var deadzone_field: LineEdit = $HBoxContainer/Control/HBoxContainer/LineEdit

@onready var btn_accelerate: Button = $HBoxContainer/VBoxContainer/HBoxContainer/Button
@onready var btn_boost: Button = $HBoxContainer/VBoxContainer/HBoxContainer9/Button
@onready var btn_brake: Button = $HBoxContainer/VBoxContainer/HBoxContainer2/Button
@onready var btn_spin: Button = $HBoxContainer/VBoxContainer/HBoxContainer3/Button
@onready var btn_side: Button = $HBoxContainer/VBoxContainer/HBoxContainer4/Button
@onready var btn_steer: Button = $HBoxContainer/VBoxContainer/HBoxContainer5/Button
@onready var btn_pitch: Button = $HBoxContainer/VBoxContainer/HBoxContainer6/Button
@onready var btn_strafe_left: Button = $HBoxContainer/VBoxContainer/HBoxContainer7/Button
@onready var btn_strafe_right: Button = $HBoxContainer/VBoxContainer/HBoxContainer8/Button

var waiting_index: int = -1
var waiting_old_text: String = ""
var axis_baseline := {}

# Calibration state
var calibrating: bool = false
var calib_radii: Array = [] # Sampled radii along the calibration circle
const CAL_POINT_COUNT := 64
const CAL_VERSION := 2
var CAL_DIRS: Array = _build_cal_dirs()

static func _build_cal_dirs() -> Array:
		var dirs: Array = []
		dirs.resize(CAL_POINT_COUNT)
		for i in range(CAL_POINT_COUNT):
				var angle := TAU * float(i) / float(CAL_POINT_COUNT)
				dirs[i] = Vector2(cos(angle), sin(angle))
		return dirs

var bindings := [
		{"button": Callable(self, "_get_btn_accel"), "type": "any", "actions": ["Accelerate"]},
		{"button": Callable(self, "_get_btn_boost"), "type": "any", "actions": ["Boost"]},
		{"button": Callable(self, "_get_btn_brake"), "type": "any", "actions": ["Brake"]},
		{"button": Callable(self, "_get_btn_spin"), "type": "any", "actions": ["SpinAttack"]},
		{"button": Callable(self, "_get_btn_side"), "type": "any", "actions": ["SideAttack"]},
		{"button": Callable(self, "_get_btn_steer"), "type": "axis_pair", "actions": ["SteerLeft", "SteerRight"]},
		{"button": Callable(self, "_get_btn_pitch"), "type": "axis_pair", "actions": ["SteerUp", "SteerDown"]},
		{"button": Callable(self, "_get_btn_strafe_left"), "type": "axis", "actions": ["StrafeLeft"]},
		{"button": Callable(self, "_get_btn_strafe_right"), "type": "axis", "actions": ["StrafeRight"]},
]

func _get_btn_accel(): return btn_accelerate
func _get_btn_boost(): return btn_boost
func _get_btn_brake(): return btn_brake
func _get_btn_spin(): return btn_spin
func _get_btn_side(): return btn_side
func _get_btn_steer(): return btn_steer
func _get_btn_pitch(): return btn_pitch
func _get_btn_strafe_left(): return btn_strafe_left
func _get_btn_strafe_right(): return btn_strafe_right

func _ready() -> void:
		close_button.pressed.connect(_on_close_pressed)
		for i in bindings.size():
				var b: Button = bindings[i]["button"].call()
				b.pressed.connect(_on_binding_pressed.bind(i))
		deadzone_slider.value_changed.connect(_on_deadzone_changed)
		deadzone_field.text_submitted.connect(_on_deadzone_text)
		calibrate_button.pressed.connect(_on_calibrate_pressed)
		input_square.resized.connect(_update_octagon_visual)
		_load_saved_bindings()
		_update_binding_labels()
		_update_deadzone_ui()
		_load_calibration()
		_update_octagon_visual()
		call_deferred("_update_octagon_visual")
		set_process(true)

func open_settings() -> void:
		_update_binding_labels()
		_update_deadzone_ui()
		show()

func _on_close_pressed() -> void:
		_save_bindings()
		hide()

func _on_deadzone_changed(v: float) -> void:
		deadzone_field.text = str(roundi(v * 100.0)) + "%"
		var actions = ["SteerLeft", "SteerRight", "SteerUp", "SteerDown"]
		for a in actions:
				if InputMap.has_action(a):
						InputMap.action_set_deadzone(a, v)
		_save_bindings()

func _on_deadzone_text(text: String) -> void:
		var t := text.strip_edges().trim_suffix("%")
		var val = clamp(float(t) / 100.0, 0.0, 1.0)
		deadzone_slider.value = val

func _update_deadzone_ui() -> void:
		var dz := 0.0
		if InputMap.has_action("SteerLeft"):
				dz = InputMap.action_get_deadzone("SteerLeft")
		deadzone_slider.value = dz
		deadzone_field.text = str(roundi(dz * 100.0)) + "%"

func _update_binding_labels() -> void:
		for i in bindings.size():
				var b: Button = bindings[i]["button"].call()
				var desc := _describe_binding(bindings[i])
				b.text = desc if desc != "" else "Unassigned"

func _describe_binding(info: Dictionary) -> String:
		var acts: Array = info["actions"]
		if acts.size() == 1:
				var a = acts[0]
				if !InputMap.has_action(a):
						return ""
				var evs := InputMap.action_get_events(a)
				if evs.is_empty():
						return ""
				var e = evs[0]
				if e is InputEventJoypadButton:
						return "Button %d" % e.button_index
				elif e is InputEventJoypadMotion:
						var sign := "+" if e.axis_value >= 0 else "-"
						return "Axis %d%s" % [e.axis, sign]
				else:
						return e.as_text()
		else:
				var a0 = acts[0]
				if !InputMap.has_action(a0):
						return ""
				var evs0 := InputMap.action_get_events(a0)
				if evs0.is_empty():
						return ""
				var e0 = evs0[0]
				if e0 is InputEventJoypadMotion:
						return "Axis %d" % e0.axis
				elif e0 is InputEventJoypadButton:
						return "Button %d" % e0.button_index
				return e0.as_text()

func _on_binding_pressed(index: int) -> void:
		if waiting_index != -1:
				return
		waiting_index = index
		var b: Button = bindings[index]["button"].call()
		waiting_old_text = b.text
		b.text = "Setting..."
		_set_all_buttons_disabled(true, index)
		_capture_axis_baseline()

func _set_all_buttons_disabled(disabled: bool, except_index: int = -1) -> void:
		for i in bindings.size():
				if i == except_index:
						continue
				var bt: Button = bindings[i]["button"].call()
				bt.disabled = disabled
		close_button.disabled = disabled

func _capture_axis_baseline() -> void:
		axis_baseline.clear()
		var devices := Input.get_connected_joypads()
		for d in devices:
				axis_baseline[d] = {}
				for axis in range(0, 10):
						axis_baseline[d][axis] = Input.get_joy_axis(d, axis)

func _clear_action_events(actions: Array) -> void:
		for a in actions:
				if !InputMap.has_action(a):
						continue
				var evs := InputMap.action_get_events(a)
				for e in evs:
						InputMap.action_erase_event(a, e)

func _set_button_binding(actions: Array, button_index: int) -> void:
		var ev := InputEventJoypadButton.new()
		ev.button_index = button_index
		ev.device = -1
		_clear_action_events(actions)
		for a in actions:
				InputMap.action_add_event(a, ev)
		_finish_wait()
		_save_bindings()

func _set_axis_binding_single(action: String, axis: int, value: float) -> void:
		var ev := InputEventJoypadMotion.new()
		ev.axis = axis
		ev.axis_value = 1.0 if value >= 0.0 else -1.0
		ev.device = -1
		_clear_action_events([action])
		InputMap.action_add_event(action, ev)
		_finish_wait()
		_save_bindings()

func _set_axis_binding_pair(actions: Array, axis: int) -> void:
		var ev_neg := InputEventJoypadMotion.new()
		ev_neg.axis = axis
		ev_neg.axis_value = -1.0
		ev_neg.device = -1
		var ev_pos := InputEventJoypadMotion.new()
		ev_pos.axis = axis
		ev_pos.axis_value = 1.0
		ev_pos.device = -1
		_clear_action_events(actions)
		InputMap.action_add_event(actions[0], ev_neg)
		InputMap.action_add_event(actions[1], ev_pos)
		_finish_wait()
		_save_bindings()

func _finish_wait(cancel: bool=false) -> void:
		if waiting_index == -1:
				return
		var b: Button = bindings[waiting_index]["button"].call()
		if cancel:
				b.text = waiting_old_text
		else:
				_update_binding_labels()
		_set_all_buttons_disabled(false)
		waiting_index = -1
		waiting_old_text = ""
		axis_baseline.clear()


func _input(event: InputEvent) -> void:
	if calibrating and event is InputEventJoypadButton and event.pressed:
		var bi := (event as InputEventJoypadButton).button_index
		if bi >= 0 and bi <= 3:
			calibrating = false
			calibrate_button.text = "Calibrate"
			_save_calibration()
			_update_octagon_visual()
			accept_event()
			return
	if waiting_index == -1:
		return
	if event is InputEventKey and event.pressed:
		if event.is_action_pressed("ui_cancel"):
			_finish_wait(true)
			accept_event()
			return
		if event.keycode == KEY_BACKSPACE:
			_clear_action_events(bindings[waiting_index]["actions"])
			_finish_wait()
			accept_event()
			_save_bindings()
			return
	if event is InputEventJoypadButton and event.pressed:
		var info = bindings[waiting_index]
		if info["type"] == "axis_pair":
			return
		_set_button_binding(info["actions"], event.button_index)
		accept_event()
		return
	if event is InputEventJoypadMotion:
		var dev := event.device
		var axis = event.axis
		var val = event.axis_value
		var base := 0.0
		if axis_baseline.has(dev) and axis_baseline[dev].has(axis):
			base = axis_baseline[dev][axis]
		var delta = abs(val - base)
		if delta >= 0.5:
			var info2 = bindings[waiting_index]
			if info2["type"] == "axis_pair":
				_set_axis_binding_pair(info2["actions"], axis)
			else:
				_set_axis_binding_single(info2["actions"][0], axis, val)
			accept_event()

func _poll_axes_for_binding() -> void:
		if waiting_index == -1:
				return
		var info = bindings[waiting_index]
		for dev in axis_baseline.keys():
				for axis in axis_baseline[dev].keys():
						var val := Input.get_joy_axis(dev, int(axis))
						var base := float(axis_baseline[dev][axis])
						if abs(val - base) >= 0.5:
								if info["type"] == "axis_pair":
										_set_axis_binding_pair(info["actions"], int(axis))
								else:
										_set_axis_binding_single(info["actions"][0], int(axis), val)
								return

func _process(delta: float) -> void:
		if !visible:
				if waiting_index != -1:
						_poll_axes_for_binding()
				return
		if waiting_index != -1:
				_poll_axes_for_binding()
		var steer_x := Input.get_axis("SteerLeft", "SteerRight")
		var steer_y := Input.get_axis("SteerUp", "SteerDown")
		var raw := Vector2(steer_x, steer_y)
		if calibrating:
				_update_calibration_with_sample(raw)
				_update_octagon_visual()
		var rect := input_square.get_rect()
		var center := rect.size * 0.5
		var range = min(rect.size.x, rect.size.y) * 0.5 - 4.0
		var pos_raw = center + raw * range
		input_dot.position = pos_raw - input_dot.pivot_offset
		var cal := _apply_calibration(raw)
		var pos_cal = center + cal * range
		input_dot_calibrated.position = pos_cal - input_dot_calibrated.pivot_offset

const SAVE_PATH := "user://controller_settings.json"
const CAL_PATH := "user://controller_calibration.json"

func _collect_bindings() -> Dictionary:
		var actions := [
				"Accelerate", "Boost", "Brake", "SpinAttack", "SideAttack",
				"SteerLeft", "SteerRight", "SteerUp", "SteerDown",
				"StrafeLeft", "StrafeRight"
		]
		var out := {}
		for a in actions:
				if !InputMap.has_action(a):
						continue
				var evs := InputMap.action_get_events(a)
				var arr := []
				for e in evs:
						if e is InputEventJoypadButton:
								arr.append({"type": "joy_button", "button": e.button_index})
						elif e is InputEventJoypadMotion:
								arr.append({"type": "joy_axis", "axis": e.axis, "value": e.axis_value})
				out[a] = arr
		var dz := 0.0
		if InputMap.has_action("SteerLeft"):
				dz = InputMap.action_get_deadzone("SteerLeft")
		return {"version": 1, "bindings": out, "steer_deadzone": dz}

func _apply_binding_dict(data: Dictionary) -> void:
		if !data.has("bindings"):
				return
		var b = data["bindings"]
		for a in b.keys():
				if !InputMap.has_action(a):
						continue
				var evs := InputMap.action_get_events(a)
				for e in evs:
						InputMap.action_erase_event(a, e)
				for entry in b[a]:
						if typeof(entry) != TYPE_DICTIONARY:
								continue
						var t := str(entry.get("type", ""))
						if t == "joy_button":
							var iev := InputEventJoypadButton.new()
							iev.button_index = int(entry.get("button", 0))
							iev.device = -1
							InputMap.action_add_event(a, iev)
						elif t == "joy_axis":
							var iev2 := InputEventJoypadMotion.new()
							iev2.axis = int(entry.get("axis", 0))
							var v := float(entry.get("value", 1.0))
							iev2.axis_value = 1.0 if v >= 0.0 else -1.0
							iev2.device = -1
							InputMap.action_add_event(a, iev2)
		if data.has("steer_deadzone"):
				var dz2 := float(data["steer_deadzone"])
				for a2 in ["SteerLeft", "SteerRight", "SteerUp", "SteerDown"]:
						if InputMap.has_action(a2):
								InputMap.action_set_deadzone(a2, dz2)
				_update_deadzone_ui()
		_update_binding_labels()

func _save_bindings() -> void:
		var file := FileAccess.open(SAVE_PATH, FileAccess.WRITE)
		if file:
				file.store_string(JSON.stringify(_collect_bindings()))
				file.close()

func _load_saved_bindings() -> void:
		if FileAccess.file_exists(SAVE_PATH):
				var txt := FileAccess.get_file_as_string(SAVE_PATH)
				var data = JSON.parse_string(txt)
				if typeof(data) == TYPE_DICTIONARY:
						_apply_binding_dict(data)

# Calibration: persistence and logic
# Calibration: persistence and logic
func _has_valid_calibration() -> bool:
		if calib_radii.is_empty():
				return false
		for v in calib_radii:
				if float(v) > 0.0:
						return true
		return false

func _default_calib_radii() -> Array:
		var a := []
		for i in range(CAL_POINT_COUNT):
				a.append(0.0)
		return a

func _legacy_radii_to_circle(legacy: Array) -> Array:
		var expanded := _default_calib_radii()
		if legacy.size() != 8:
				return expanded
		var legacy_step := TAU / 8.0
		for i in range(CAL_POINT_COUNT):
				var phi := TAU * float(i) / float(CAL_POINT_COUNT)
				var idx := int(floor(phi / legacy_step)) % 8
				var idx2 := (idx + 1) % 8
				var t := (phi - float(idx) * legacy_step) / legacy_step
				var r1 := float(legacy[idx])
				var r2 := float(legacy[idx2])
				expanded[i] = lerp(r1, r2, t)
		return expanded

func _save_calibration() -> void:
		var data := {
				"version": CAL_VERSION,
				"radii": calib_radii,
		}
		var f := FileAccess.open(CAL_PATH, FileAccess.WRITE)
		if f:
				f.store_string(JSON.stringify(data))
				f.close()

func _load_calibration() -> void:
		calib_radii = _default_calib_radii()
		if FileAccess.file_exists(CAL_PATH):
				var txt := FileAccess.get_file_as_string(CAL_PATH)
				var data = JSON.parse_string(txt)
				if typeof(data) == TYPE_DICTIONARY and data.has("radii"):
						var arr: Array = data["radii"]
						if arr.size() == CAL_POINT_COUNT:
								calib_radii = arr.duplicate(true)
						elif arr.size() == 8:
								calib_radii = _legacy_radii_to_circle(arr)

func _on_calibrate_pressed() -> void:
		if !calibrating:
				calibrating = true
				calib_radii = _default_calib_radii()
				calibrate_button.text = "Calibrating... Press A/B/X/Y to finish"
		else:
				calibrating = false
				calibrate_button.text = "Calibrate"
				_save_calibration()
				_update_octagon_visual()

func _update_calibration_with_sample(v: Vector2) -> void:
		var mag := v.length()
		if mag <= 0.0001:
				return
		var phi := fposmod(atan2(v.y, v.x), TAU)
		var step := TAU / float(CAL_POINT_COUNT)
		var i := int(floor(phi / step)) % CAL_POINT_COUNT
		var i2 := (i + 1) % CAL_POINT_COUNT
		var base_angle := float(i) * step
		var delta := phi - base_angle
		var contrib1 = max(0.0, mag * cos(delta))
		var contrib2 = max(0.0, mag * cos(step - delta))
		if contrib1 > calib_radii[i]:
				calib_radii[i] = contrib1
		if contrib2 > calib_radii[i2]:
				calib_radii[i2] = contrib2

func _apply_calibration(v: Vector2) -> Vector2:
	if v == Vector2.ZERO:
			return v
	if !_has_valid_calibration():
			return v
	var phi := fposmod(atan2(v.y, v.x), TAU)
	var step := TAU / float(CAL_POINT_COUNT)
	var i := int(floor(phi / step)) % CAL_POINT_COUNT
	var i2 := (i + 1) % CAL_POINT_COUNT
	var t := (phi - float(i) * step) / step
	var r1 := float(calib_radii[i])
	var r2 := float(calib_radii[i2])
	var r_meas = lerp(r1, r2, t)
	var denom = max(r_meas, 0.0001)
	var c = abs(cos(phi))
	var s = abs(sin(phi))
	var r_square = 1.0 / max(c, s)
	var final = v * (r_square / denom) * 1.1
	final.x = clampf(final.x, -1.0, 1.0)
	final.y = clampf(final.y, -1.0, 1.0)
	return final

func _update_octagon_visual() -> void:
		if calib_radii.is_empty():
				octagon_line.points = PackedVector2Array()
				return
		var radii_to_use := calib_radii
		if !_has_valid_calibration():
				var fallback := []
				fallback.resize(CAL_POINT_COUNT)
				for i in range(CAL_POINT_COUNT):
						fallback[i] = 1.0
				radii_to_use = fallback
		var size := input_square.size
		if size.x <= 0.0 or size.y <= 0.0:
				octagon_line.points = PackedVector2Array()
				return
		var center := size * 0.5
		var range = min(size.x, size.y) * 0.5 - 4.0
		if range <= 0.0:
				range = 0.0
		var pts := PackedVector2Array()
		for i in range(CAL_POINT_COUNT):
				var p = center + CAL_DIRS[i] * float(radii_to_use[i]) * range
				pts.append(p)
		octagon_line.points = pts
