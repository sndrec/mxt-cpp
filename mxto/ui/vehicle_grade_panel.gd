class_name VehicleGradePanel extends VBoxContainer

@onready var status: Label = $Status
@onready var category_grid: GridContainer = $CategoryGrid
@onready var summary: Label = $Summary
@onready var benchmark_rows: VBoxContainer = $Details/Benchmarks/Rows
@onready var advanced_rows: VBoxContainer = $Details/Advanced/Rows
@onready var trait_rows: VBoxContainer = $Details/Traits/Rows

var category_value_labels: Dictionary = {}
const CATEGORY_HELP := {
	"Speed": "Settled unboosted straight-line speed.",
	"Acceleration": "Early-launch speed, fixed-speed milestones, and distance covered during the first five seconds.",
	"Cornering": "Steering strength and speed retention in normal turns and ordinary drifts.",
	"Grip": "Base resistance to entering a slide and the ability to settle and re-grip afterward.",
	"Booster": "Absolute manual-boost speed and sustained advantage when boosting from settled top speed.",
	"Body": "Base maximum energy and resistance to incoming damage.",
	"Air Control": "Speed retention and controlled heading change during a fixed jump.",
}


func _ready() -> void:
	for child in category_grid.get_children():
		child.queue_free()
	for category in ["Speed", "Acceleration", "Cornering", "Grip", "Booster", "Body", "Air Control"]:
		var name_label := Label.new()
		name_label.text = category
		name_label.tooltip_text = String(CATEGORY_HELP[category])
		name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		category_grid.add_child(name_label)
		category_grid.add_child(_make_help_button(String(CATEGORY_HELP[category])))
		var grade_label := Label.new()
		grade_label.text = "—"
		grade_label.custom_minimum_size.x = 42.0
		grade_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		grade_label.tooltip_text = String(CATEGORY_HELP[category])
		grade_label.add_theme_font_size_override("font_size", 22)
		category_grid.add_child(grade_label)
		category_value_labels[category] = grade_label
	show_analysis({})


func show_analysis(result: Dictionary) -> void:
	_clear_rows(benchmark_rows)
	_clear_rows(advanced_rows)
	_clear_rows(trait_rows)
	for label_value in category_value_labels.values():
		var label := label_value as Label
		label.text = "—"
		label.modulate = Color.WHITE
	if !bool(result.get("valid", false)):
		status.text = String(result.get("error", "Performance analysis pending"))
		status.tooltip_text = ""
		summary.text = ""
		_add_message(benchmark_rows, "Select a machine to analyze it.")
		_add_message(advanced_rows, "Raw grades will appear here.")
		_add_message(trait_rows, "Special-state traits will appear here.")
		return
	status.text = "PERFORMANCE AT %d%% · GRADES VS 50%% ALL ROUNDER" % roundi(float(result.get("machine_setting", 0.5)) * 100.0)
	status.tooltip_text = "C is the All Rounder at 50%. A and E are the best and worst official results sampled at 0%, 50%, and 100%."
	for category_value in result.get("categories", []):
		var category: Dictionary = category_value
		var name := String(category.get("name", ""))
		var label := category_value_labels.get(name) as Label
		if label != null:
			var grade := String(category.get("grade", "?"))
			label.text = grade
			label.modulate = _grade_colour(grade)
		for component_value in category.get("components", []):
			var component: Dictionary = component_value
			_add_benchmark_row(name, component)
	summary.text = "Weight %.0f kg   Terminal %.1f km/h   95%% speed %.2f s   Settlement %.0f%%" % [
		float(result.get("weight_kg", 0.0)),
		float(result.get("terminal_speed_kmh", 0.0)),
		float(result.get("time_to_95_seconds", 0.0)),
		float(result.get("settlement_confidence", 0.0)) * 100.0,
	]
	for stat_value in result.get("advanced_stats", []):
		_add_advanced_row(stat_value)
	var traits: Array = result.get("traits", [])
	if traits.is_empty():
		_add_message(trait_rows, "No special-state differences of 5% or more.")
	else:
		for trait_value in traits:
			_add_trait_row(trait_value)


func _add_benchmark_row(category: String, component: Dictionary) -> void:
	var row := HBoxContainer.new()
	var explanation := String(component.get("explanation", "No explanation is available for this benchmark."))
	row.tooltip_text = explanation
	var label := Label.new()
	label.text = "%s · %s" % [category, String(component.get("name", "Metric"))]
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(label)
	row.add_child(_make_help_button(explanation))
	var value := Label.new()
	var available := bool(component.get("available", true))
	value.text = "%s %s" % [_format_value(float(component.get("value", 0.0))), String(component.get("unit", ""))] if available else "N/A"
	value.custom_minimum_size.x = 118.0
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	row.add_child(value)
	var grade := Label.new()
	grade.text = String(component.get("grade", "?"))
	grade.custom_minimum_size.x = 38.0
	grade.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	grade.modulate = _grade_colour(grade.text)
	row.add_child(grade)
	benchmark_rows.add_child(row)


func _add_advanced_row(stat_value: Dictionary) -> void:
	var row := VBoxContainer.new()
	row.tooltip_text = String(stat_value.get("explanation", ""))
	var header := HBoxContainer.new()
	var name_label := Label.new()
	name_label.text = String(stat_value.get("friendly_name", stat_value.get("name", "Stat")))
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(name_label)
	header.add_child(_make_help_button(String(stat_value.get("explanation", "No explanation is available for this stat."))))
	var exact := Label.new()
	exact.text = "%s %s" % [_format_value(float(stat_value.get("value", 0.0))), String(stat_value.get("unit", ""))]
	exact.custom_minimum_size.x = 135.0
	exact.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	header.add_child(exact)
	var grade := Label.new()
	grade.text = String(stat_value.get("grade", "?"))
	grade.custom_minimum_size.x = 38.0
	grade.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	grade.modulate = _grade_colour(grade.text)
	header.add_child(grade)
	row.add_child(header)
	var graph := ProgressBar.new()
	graph.min_value = -1.0
	graph.max_value = 5.0
	graph.value = clampf(float(stat_value.get("score", 2.0)), -1.0, 5.0)
	graph.show_percentage = false
	graph.custom_minimum_size.y = 7.0
	graph.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(graph)
	advanced_rows.add_child(row)


func _add_trait_row(trait_value: Dictionary) -> void:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.text = String(trait_value.get("text", "Special-state difference"))
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var reference_name := "Roster baseline" if bool(trait_value.get("uses_roster_baseline", false)) else "Normal-state value"
	var explanation := String(trait_value.get("explanation", "No explanation is available for this trait."))
	var details := "%s\n\n%s %s; special %s %s" % [
		explanation,
		reference_name,
		_format_value(float(trait_value.get("base_value", 0.0))),
		_format_value(float(trait_value.get("effective_value", 0.0))),
		String(trait_value.get("unit", "")),
	]
	label.tooltip_text = details
	match String(trait_value.get("kind", "distinctive")):
		"strength": label.modulate = Color(0.45, 0.82, 1.0)
		"drawback": label.modulate = Color(1.0, 0.56, 0.46)
		_: label.modulate = Color(0.82, 0.72, 1.0)
	row.add_child(label)
	row.add_child(_make_help_button(details))
	trait_rows.add_child(row)


func _make_help_button(help_text: String) -> Button:
	var button := Button.new()
	button.text = "?"
	button.tooltip_text = help_text
	button.flat = true
	button.focus_mode = Control.FOCUS_NONE
	button.mouse_default_cursor_shape = Control.CURSOR_HELP
	button.custom_minimum_size = Vector2(24.0, 24.0)
	return button


func _add_message(parent: VBoxContainer, text: String) -> void:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	parent.add_child(label)


func _clear_rows(parent: VBoxContainer) -> void:
	for child in parent.get_children():
		parent.remove_child(child)
		child.queue_free()


func _format_value(value: float) -> String:
	var magnitude := absf(value)
	if magnitude >= 1000.0:
		return "%.0f" % value
	if magnitude >= 10.0:
		return "%.2f" % value
	return "%.4f" % value


func _grade_colour(grade: String) -> Color:
	if grade == "N/A":
		return Color(0.72, 0.72, 0.72)
	if grade.begins_with("S"):
		return Color(0.38, 0.9, 1.0)
	if grade.begins_with("A"):
		return Color(0.42, 1.0, 0.58)
	if grade.begins_with("B"):
		return Color(0.76, 0.95, 0.46)
	if grade.begins_with("C"):
		return Color(1.0, 0.9, 0.42)
	if grade.begins_with("D"):
		return Color(1.0, 0.66, 0.36)
	return Color(1.0, 0.42, 0.42)
