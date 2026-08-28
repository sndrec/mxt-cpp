class_name VehicleEditorCurveController
extends Node

signal diagnostics_requested(result: Dictionary)
signal history_changed

const PERFORMANCE_DEBOUNCE_MSEC := 120

var session: MxtCarAuthoringSession
var stat_schema: Array = []
var schema_by_name: Dictionary = {}
var current_layer := "base"
var current_stat := "weight_kg"
var curve_clipboard: Array = []
var gesture_active := false
var performance_analyzer := MxtCarPerformanceAnalyzer.new()
var performance_due_msec := 0
var updating_controls := false

var search_input: LineEdit
var category_option: OptionButton
var advanced_mode: CheckBox
var layer_option: OptionButton
var stat_option: OptionButton
var curve_graph: VehicleEditorCurveGraph
var authoring_mode_indicator: Label
var make_custom_button: Button
var revert_derived_button: Button
var stat_help: RichTextLabel
var key_time: SpinBox
var key_value: SpinBox
var key_tangent_in: SpinBox
var key_tangent_out: SpinBox
var machine_setting: HSlider
var machine_value: Label
var technique_option: OptionButton
var technique_intensity: HSlider
var boost_option: OptionButton
var start_speed: SpinBox
var frame_perfect: CheckBox
var speed_summary: Label
var speed_graph: VehicleEditorSpeedGraph
var vehicle_grade_panel: VehicleGradePanel

func initialize(owner_ui: Control, authoring_session: MxtCarAuthoringSession) -> void:
	session = authoring_session
	stat_schema = session.get_stat_schema()
	for entry_value in stat_schema:
		var entry: Dictionary = entry_value
		schema_by_name[String(entry["name"])] = entry
	search_input = owner_ui.get_node("Workspace/StatsColumn/StatFilters/Search")
	category_option = owner_ui.get_node("Workspace/StatsColumn/StatFilters/Category")
	advanced_mode = owner_ui.get_node("Workspace/StatsColumn/StatFilters/AdvancedMode")
	layer_option = owner_ui.get_node("Workspace/StatsColumn/StatFilters/Layer")
	stat_option = owner_ui.get_node("Workspace/StatsColumn/StatFilters/Stat")
	curve_graph = owner_ui.get_node("Workspace/StatsColumn/CurveGraph")
	authoring_mode_indicator = owner_ui.get_node("Workspace/StatsColumn/AuthoringMode/Indicator")
	make_custom_button = owner_ui.get_node("Workspace/StatsColumn/AuthoringMode/MakeCustom")
	revert_derived_button = owner_ui.get_node("Workspace/StatsColumn/AuthoringMode/RevertDerived")
	stat_help = owner_ui.get_node("Workspace/StatsColumn/StatHelp")
	key_time = owner_ui.get_node("Workspace/StatsColumn/KeyEditor/Time")
	key_value = owner_ui.get_node("Workspace/StatsColumn/KeyEditor/Value")
	key_tangent_in = owner_ui.get_node("Workspace/StatsColumn/KeyEditor/TangentIn")
	key_tangent_out = owner_ui.get_node("Workspace/StatsColumn/KeyEditor/TangentOut")
	machine_setting = owner_ui.get_node("Workspace/StatsColumn/SampleControls/MachineSetting")
	machine_value = owner_ui.get_node("Workspace/StatsColumn/SampleControls/MachineValue")
	technique_option = owner_ui.get_node("Workspace/StatsColumn/SampleControls/Technique")
	technique_intensity = owner_ui.get_node("Workspace/StatsColumn/SampleControls/TechniqueIntensity")
	boost_option = owner_ui.get_node("Workspace/StatsColumn/SampleControls/BoostState")
	start_speed = owner_ui.get_node("Workspace/StatsColumn/SpeedControls/StartSpeed")
	frame_perfect = owner_ui.get_node("Workspace/StatsColumn/SpeedControls/FramePerfect")
	speed_summary = owner_ui.get_node("Workspace/StatsColumn/SpeedControls/SpeedSummary")
	speed_graph = owner_ui.get_node("Workspace/StatsColumn/SpeedGraph")
	vehicle_grade_panel = owner_ui.get_node("Workspace/VisualColumn/PhysicalTabs/Performance")
	_setup_options()
	_connect_controls(owner_ui)

func refresh_all() -> void:
	refresh_stat_options()
	refresh_samples()

func cancel_active_edit() -> void:
	if gesture_active:
		curve_graph.cancel_active_edit()

func reset_performance_analysis() -> void:
	performance_analyzer = MxtCarPerformanceAnalyzer.new()
	refresh_samples()

func focus_stat_selector() -> void:
	stat_option.grab_focus()

func refresh_stat_options() -> void:
	var previous := current_stat
	stat_option.clear()
	var category := category_option.get_item_text(category_option.selected) if category_option.selected >= 0 else "All"
	var search := search_input.text.to_lower()
	for entry_value in stat_schema:
		var entry: Dictionary = entry_value
		var name := String(entry["name"])
		var friendly_name := String(entry.get("friendly_name", name.replace("_", " ").capitalize()))
		if current_layer != "base" and !bool(entry["supports_live_modifiers"]):
			continue
		if category != "All" and String(entry["category"]) != category:
			continue
		if !search.is_empty() and !name.to_lower().contains(search) \
				and !friendly_name.to_lower().contains(search) \
				and !String(entry.get("explanation", "")).to_lower().contains(search):
			continue
		stat_option.add_item(friendly_name)
		stat_option.set_item_metadata(stat_option.item_count - 1, name)
		if name == previous:
			stat_option.select(stat_option.item_count - 1)
	if stat_option.item_count == 0:
		return
	if stat_option.selected < 0:
		stat_option.select(0)
	current_stat = String(stat_option.get_item_metadata(stat_option.selected))
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()

func refresh_samples() -> void:
	machine_value.text = "%.3f" % machine_setting.value
	curve_graph.set_sample_setting(machine_setting.value)
	performance_due_msec = Time.get_ticks_msec() + PERFORMANCE_DEBOUNCE_MSEC
	_refresh_speed_preview()

func _process(_delta: float) -> void:
	if performance_due_msec != 0 and Time.get_ticks_msec() >= performance_due_msec:
		performance_due_msec = 0
		vehicle_grade_panel.show_analysis(performance_analyzer.analyze_session(session, machine_setting.value))

func _setup_options() -> void:
	category_option.add_item("All")
	var categories := {}
	for entry_value in stat_schema:
		categories[String(entry_value["category"])] = true
	for category in categories.keys():
		category_option.add_item(String(category))
	for layer in session.get_layer_names():
		layer_option.add_item(String(layer).replace("_", " ").capitalize())
		layer_option.set_item_metadata(layer_option.item_count - 1, layer)
	layer_option.add_item("S-BOOST")
	layer_option.set_item_metadata(layer_option.item_count - 1, "s_boost")
	for name in ["None", "MTS", "Quickturn"]:
		technique_option.add_item(name)
	for name in ["None", "Manual", "Dashplate", "Stacked", "S-BOOST", "S-BOOST + Dashplate"]:
		boost_option.add_item(name)
	for i in range(6):
		boost_option.set_item_metadata(i, ["none", "manual", "dashplate", "stacked", "s_boost", "s_boost_dashplate"][i])

func _connect_controls(owner_ui: Control) -> void:
	search_input.text_changed.connect(func(_value): refresh_stat_options())
	category_option.item_selected.connect(func(_index): refresh_stat_options())
	advanced_mode.toggled.connect(_on_advanced_mode_toggled)
	layer_option.item_selected.connect(_on_layer_selected)
	stat_option.item_selected.connect(_on_stat_selected)
	curve_graph.edit_started.connect(_begin_curve_gesture)
	curve_graph.curve_preview_changed.connect(_preview_curve_gesture)
	curve_graph.edit_cancelled.connect(_cancel_curve_gesture)
	curve_graph.curve_committed.connect(_commit_curve)
	curve_graph.key_selected.connect(_show_selected_key)
	owner_ui.get_node("Workspace/StatsColumn/CurveActions/Apply").pressed.connect(_apply_selected_key)
	owner_ui.get_node("Workspace/StatsColumn/CurveActions/Add").pressed.connect(_add_key)
	owner_ui.get_node("Workspace/StatsColumn/CurveActions/Remove").pressed.connect(_remove_key)
	owner_ui.get_node("Workspace/StatsColumn/CurveActions/Copy").pressed.connect(func(): curve_clipboard = curve_graph.get_keys())
	owner_ui.get_node("Workspace/StatsColumn/CurveActions/Paste").pressed.connect(_paste_curve)
	owner_ui.get_node("Workspace/StatsColumn/CurveActions/Reset").pressed.connect(_reset_curve)
	make_custom_button.pressed.connect(_make_selected_special_custom)
	revert_derived_button.pressed.connect(_revert_selected_special_derived)
	machine_setting.value_changed.connect(func(_value): refresh_samples())
	technique_option.item_selected.connect(func(_index): refresh_samples())
	technique_intensity.value_changed.connect(func(_value): refresh_samples())
	boost_option.item_selected.connect(func(_index): refresh_samples())
	start_speed.value_changed.connect(func(_value): _refresh_speed_preview())
	frame_perfect.toggled.connect(func(_value): _refresh_speed_preview())

func _on_advanced_mode_toggled(enabled: bool) -> void:
	layer_option.visible = enabled
	if !enabled:
		current_layer = "base"
		layer_option.select(0)
	refresh_stat_options()

func _on_layer_selected(index: int) -> void:
	current_layer = String(layer_option.get_item_metadata(index))
	refresh_stat_options()

func _on_stat_selected(index: int) -> void:
	current_stat = String(stat_option.get_item_metadata(index))
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	refresh_samples()

func _selected_special_is_derived() -> bool:
	return current_layer != "base" and session.is_special_derived(current_layer, current_stat)

func _refresh_selected_stat_ui() -> void:
	var schema: Dictionary = schema_by_name.get(current_stat, {})
	var special := current_layer != "base"
	var derived := special and _selected_special_is_derived()
	authoring_mode_indicator.text = "Derived — follows base machine stats" if derived else ("Custom special-state value" if special else "Base machine-setting curve")
	make_custom_button.visible = derived
	revert_derived_button.visible = special and !derived
	curve_graph.set_display_context(derived or current_layer == "s_boost", derived, String(schema.get("unit", "scalar")))
	var editable := !derived
	for node_name in ["Apply", "Add", "Remove", "Paste", "Reset"]:
		var button := curve_graph.get_node("../CurveActions/%s" % node_name) as Button
		button.disabled = !editable or (node_name in ["Add", "Remove", "Paste"] and current_layer == "s_boost")
	key_value.editable = editable
	key_time.editable = editable and current_layer != "s_boost"
	key_tangent_in.editable = editable and current_layer != "s_boost"
	key_tangent_out.editable = editable and current_layer != "s_boost"
	var unit := String(schema.get("unit", "scalar"))
	var activity := String(schema.get("activity", "always")).replace("_", " ").capitalize()
	var reference := float(schema.get("default_value", 0.0))
	var context := "Base values are sampled from the 0–1 machine-setting curve."
	if special:
		context = _special_derivation_help()
	stat_help.text = "[b]%s[/b]  [color=#91a8c7]%s · %s · reference %s[/color]\n%s\n[color=#b9c9dc]%s[/color]" % [String(schema.get("friendly_name", current_stat)), unit, activity, str(reference), String(schema.get("explanation", "")), context]

func _special_derivation_help() -> String:
	if current_layer == "s_boost": return "Derived S-BOOST values are absolute snapshots of the base curve at 50% machine setting."
	if current_layer in ["mts", "quickturn", "no_boost"]: return "The derived value is an identity multiplier (1.0); make it custom only for a deliberate state-specific trait."
	if current_stat == "drive_target_speed_multiplier": return "Derived from acceleration and manual turbo gain across machine setting using the original boost target-speed formula."
	if current_stat == "acceleration_response_multiplier": return "Derived from weight: 0.3 at 1000 kg or lighter, otherwise 0.5."
	if current_stat == "forward_thrust_multiplier": return "Derived from weight: 1.2 at 1000 kg or lighter, otherwise 1.6."
	if current_stat == "turbo_flat_loss_per_second": return "Derived boosted-state multiplier: 0.5 of the base flat turbo loss."
	if current_stat == "turbo_percent_loss_per_second": return "Derived boosted-state multiplier: 0.6 of the base percentage turbo loss."
	return "The derived value is an identity multiplier (1.0)."

func _make_selected_special_custom() -> void:
	var result: Dictionary = session.make_special_custom(current_layer, current_stat)
	diagnostics_requested.emit(result)
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	history_changed.emit()

func _revert_selected_special_derived() -> void:
	var result: Dictionary = session.revert_special_derived(current_layer, current_stat)
	diagnostics_requested.emit(result)
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	history_changed.emit()
	refresh_samples()

func _commit_curve(keys: Array) -> void:
	if current_layer == "s_boost" or _selected_special_is_derived(): return
	var result: Dictionary = session.set_curve(current_layer, current_stat, keys)
	if bool(result.get("valid", false)): session.end_edit_transaction()
	else: session.cancel_edit_transaction()
	gesture_active = false
	diagnostics_requested.emit(result)
	curve_graph.sync_keys(session.get_curve(current_layer, current_stat))
	_refresh_selected_stat_ui()
	history_changed.emit()
	refresh_samples()

func _begin_curve_gesture() -> void:
	gesture_active = true
	session.begin_edit_transaction()

func _preview_curve_gesture(keys: Array) -> void:
	if current_layer != "s_boost" and !_selected_special_is_derived():
		var result: Dictionary = session.set_curve(current_layer, current_stat, keys)
		if bool(result.get("valid", false)): refresh_samples()

func _cancel_curve_gesture() -> void:
	session.cancel_edit_transaction()
	gesture_active = false
	curve_graph.sync_keys(session.get_curve(current_layer, current_stat))
	_refresh_selected_stat_ui()
	history_changed.emit()
	refresh_samples()

func _show_selected_key(index: int) -> void:
	var keys := curve_graph.get_keys()
	if index < 0 or index >= keys.size(): return
	updating_controls = true
	var key: Dictionary = keys[index]
	key_time.value = float(key["time"])
	key_value.value = float(key["value"])
	key_tangent_in.value = float(key["tangent_in"])
	key_tangent_out.value = float(key["tangent_out"])
	key_time.editable = current_layer != "s_boost" and !_selected_special_is_derived()
	key_tangent_in.editable = key_time.editable
	key_tangent_out.editable = key_time.editable
	key_value.editable = !_selected_special_is_derived()
	updating_controls = false

func _apply_selected_key() -> void:
	if updating_controls or _selected_special_is_derived(): return
	if current_layer == "s_boost":
		session.set_s_boost_value(current_stat, key_value.value)
		curve_graph.show_curve(session, current_layer, current_stat)
		history_changed.emit()
		refresh_samples()
		return
	var keys := curve_graph.get_keys()
	var index := curve_graph.selected_key
	if index < 0 or index >= keys.size(): return
	keys[index] = {"time": key_time.value, "value": key_value.value, "tangent_in": key_tangent_in.value, "tangent_out": key_tangent_out.value}
	_commit_curve(keys)

func _add_key() -> void:
	if current_layer == "s_boost": return
	var keys := curve_graph.get_keys()
	var time := machine_setting.value
	for key in keys:
		if absf(float(key["time"]) - time) < 0.001: return
	keys.append({"time": time, "value": session.sample_curve(current_layer, current_stat, time), "tangent_in": 0.0, "tangent_out": 0.0})
	keys.sort_custom(func(a, b): return float(a["time"]) < float(b["time"]))
	_commit_curve(keys)

func _remove_key() -> void:
	if current_layer == "s_boost": return
	var keys := curve_graph.get_keys()
	if keys.size() <= 1 or curve_graph.selected_key < 0: return
	keys.remove_at(curve_graph.selected_key)
	_commit_curve(keys)

func _paste_curve() -> void:
	if current_layer != "s_boost" and !curve_clipboard.is_empty(): _commit_curve(curve_clipboard)

func _reset_curve() -> void:
	var schema: Dictionary = schema_by_name.get(current_stat, {})
	var value := float(schema.get("default_value", 0.0)) if current_layer in ["base", "s_boost"] else 1.0
	if current_layer == "s_boost":
		session.set_s_boost_value(current_stat, value)
		curve_graph.show_curve(session, current_layer, current_stat)
		history_changed.emit()
	else: _commit_curve([{"time": 0.0, "value": value, "tangent_in": 0.0, "tangent_out": 0.0}])

func _refresh_speed_preview() -> void:
	var result: Dictionary = session.simulate_speed_preview(machine_setting.value, start_speed.value, frame_perfect.button_pressed, ["none", "mts", "quickturn"][technique_option.selected], technique_intensity.value, String(boost_option.get_item_metadata(boost_option.selected)))
	if result.has("error"):
		speed_summary.text = String(result["error"])
		speed_graph.show_result({})
		return
	speed_summary.text = "Terminal %.2f km/h   Peak %.2f km/h   Settle %.2f s" % [float(result["terminal_speed_kmh"]), float(result["peak_speed_kmh"]), float(result["settle_time_seconds"])]
	speed_graph.show_result(result)
