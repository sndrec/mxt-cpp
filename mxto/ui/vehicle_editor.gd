class_name VehicleEditor extends VBoxContainer

signal content_changed
signal test_drive_requested(snapshot: Dictionary)

const PREVIEW_WORLD_SCENE: PackedScene = preload("res://ui/garage_preview_world.tscn")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const DRAFTS_ROOT := "user://vehicle_drafts"
const LOCAL_LIBRARY_ROOT := "user://content/packages"
const TEST_DRIVE_LIBRARY_ROOT := "user://content/test_drive_snapshots"

@onready var draft_option: OptionButton = $Toolbar/DraftOption
@onready var title_input: LineEdit = $Metadata/Title
@onready var author_input: LineEdit = $Metadata/Author
@onready var description_input: LineEdit = $Metadata/Description
@onready var preview_container: SubViewportContainer = $Workspace/VisualColumn/Preview
@onready var preview_viewport: SubViewport = $Workspace/VisualColumn/Preview/Viewport
@onready var visual_status: Label = $Workspace/VisualColumn/VisualStatus
@onready var transform_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Transform/Rows
@onready var corner_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Corners/Rows
@onready var thruster_selector: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Selector
@onready var thruster_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Thrusters/Rows
@onready var search_input: LineEdit = $Workspace/StatsColumn/StatFilters/Search
@onready var category_option: OptionButton = $Workspace/StatsColumn/StatFilters/Category
@onready var layer_option: OptionButton = $Workspace/StatsColumn/StatFilters/Layer
@onready var stat_option: OptionButton = $Workspace/StatsColumn/StatFilters/Stat
@onready var curve_graph: VehicleEditorCurveGraph = $Workspace/StatsColumn/CurveGraph
@onready var key_time: SpinBox = $Workspace/StatsColumn/KeyEditor/Time
@onready var key_value: SpinBox = $Workspace/StatsColumn/KeyEditor/Value
@onready var key_tangent_in: SpinBox = $Workspace/StatsColumn/KeyEditor/TangentIn
@onready var key_tangent_out: SpinBox = $Workspace/StatsColumn/KeyEditor/TangentOut
@onready var machine_setting: HSlider = $Workspace/StatsColumn/SampleControls/MachineSetting
@onready var machine_value: Label = $Workspace/StatsColumn/SampleControls/MachineValue
@onready var technique_option: OptionButton = $Workspace/StatsColumn/SampleControls/Technique
@onready var technique_intensity: HSlider = $Workspace/StatsColumn/SampleControls/TechniqueIntensity
@onready var boost_option: OptionButton = $Workspace/StatsColumn/SampleControls/BoostState
@onready var start_speed: SpinBox = $Workspace/StatsColumn/SpeedControls/StartSpeed
@onready var frame_perfect: CheckBox = $Workspace/StatsColumn/SpeedControls/FramePerfect
@onready var speed_summary: Label = $Workspace/StatsColumn/SpeedControls/SpeedSummary
@onready var speed_graph: VehicleEditorSpeedGraph = $Workspace/StatsColumn/SpeedGraph
@onready var diagnostics: RichTextLabel = $Workspace/StatsColumn/Diagnostics
@onready var import_model_dialog: FileDialog = $ImportModelDialog
@onready var export_package_dialog: FileDialog = $ExportPackageDialog

var game_manager: GameManager
var session := MxtCarAuthoringSession.new()
var draft_id := ""
var stat_schema: Array = []
var schema_by_name: Dictionary = {}
var current_layer := "base"
var current_stat := "weight_kg"
var curve_clipboard: Array = []
var preview_root: Node3D
var preview_camera: Camera3D
var preview_render_manager: CarRenderManager
var preview_definition: CarDefinition
var vector_controls: Dictionary = {}
var updating_controls := false


func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	stat_schema = session.get_stat_schema()
	for entry_value in stat_schema:
		var entry: Dictionary = entry_value
		schema_by_name[String(entry["name"])] = entry
	_setup_options()
	_setup_vector_controls()
	_setup_preview()
	_connect_controls()
	_refresh_draft_options()
	_new_draft()


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
	boost_option.set_item_metadata(0, "none")
	boost_option.set_item_metadata(1, "manual")
	boost_option.set_item_metadata(2, "dashplate")
	boost_option.set_item_metadata(3, "stacked")
	boost_option.set_item_metadata(4, "s_boost")
	boost_option.set_item_metadata(5, "s_boost_dashplate")


func _setup_vector_controls() -> void:
	_add_vector_row(transform_rows, "translation", "Translation", Vector3.ZERO, -1000.0, 1000.0)
	_add_vector_row(transform_rows, "rotation", "Rotation deg", Vector3.ZERO, -3600.0, 3600.0)
	_add_vector_row(transform_rows, "scale", "Scale", Vector3.ONE, 0.001, 100.0)
	for i in range(4):
		_add_vector_row(corner_rows, "tilt_%d" % i, "Tilt %d" % i, Vector3.ZERO, -1000.0, 1000.0)
	for i in range(4):
		_add_vector_row(corner_rows, "wall_%d" % i, "Wall %d" % i, Vector3.ZERO, -1000.0, 1000.0)
	_add_vector_row(thruster_rows, "thruster_position", "Position", Vector3.ZERO, -100.0, 100.0)
	_add_vector_row(thruster_rows, "thruster_rotation", "Rotation deg", Vector3.ZERO, -3600.0, 3600.0)
	var scale_row := HBoxContainer.new()
	var scale_label := Label.new()
	scale_label.text = "Scale"
	scale_label.custom_minimum_size.x = 110.0
	scale_row.add_child(scale_label)
	var scale_control := SpinBox.new()
	scale_control.min_value = 0.01
	scale_control.max_value = 10.0
	scale_control.step = 0.01
	scale_control.value = 1.0
	scale_control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scale_row.add_child(scale_control)
	thruster_rows.add_child(scale_row)
	vector_controls["thruster_scale"] = scale_control


func _add_vector_row(parent: VBoxContainer, key: String, label_text: String, value: Vector3, minimum: float, maximum: float) -> void:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size.x = 110.0
	row.add_child(label)
	var controls: Array[SpinBox] = []
	for axis in range(3):
		var spin := SpinBox.new()
		spin.min_value = minimum
		spin.max_value = maximum
		spin.step = 0.01
		spin.allow_greater = true
		spin.allow_lesser = true
		spin.value = value[axis]
		spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(spin)
		controls.append(spin)
	parent.add_child(row)
	vector_controls[key] = controls


func _setup_preview() -> void:
	var world := PREVIEW_WORLD_SCENE.instantiate()
	preview_viewport.add_child(world)
	preview_root = Node3D.new()
	preview_root.name = "VehicleEditorPreviewRoot"
	world.add_child(preview_root)
	preview_render_manager = CarRenderManagerClass.new()
	preview_root.add_child(preview_render_manager)
	preview_camera = Camera3D.new()
	preview_camera.position = Vector3(7.0, 3.7, 9.0)
	preview_root.add_child(preview_camera)
	preview_camera.look_at(Vector3.ZERO, Vector3.UP)
	preview_camera.current = true


func _connect_controls() -> void:
	$Toolbar/NewDraft.pressed.connect(_new_draft)
	$Toolbar/OpenDraft.pressed.connect(_open_selected_draft)
	$Toolbar/ImportModel.pressed.connect(func(): import_model_dialog.popup_centered())
	$Toolbar/SaveDraft.pressed.connect(_save_draft)
	$Toolbar/InstallVehicle.pressed.connect(_install_vehicle)
	$Toolbar/ExportPackage.pressed.connect(func(): export_package_dialog.popup_centered())
	$Toolbar/TestDrive.pressed.connect(_test_drive)
	$Toolbar/Undo.pressed.connect(func(): session.undo(); _refresh_all())
	$Toolbar/Redo.pressed.connect(func(): session.redo(); _refresh_all())
	import_model_dialog.file_selected.connect(_import_model)
	export_package_dialog.file_selected.connect(_export_package)
	search_input.text_changed.connect(func(_value): _refresh_stat_options())
	category_option.item_selected.connect(func(_index): _refresh_stat_options())
	layer_option.item_selected.connect(_on_layer_selected)
	stat_option.item_selected.connect(_on_stat_selected)
	curve_graph.curve_committed.connect(_commit_curve)
	curve_graph.key_selected.connect(_show_selected_key)
	$Workspace/StatsColumn/CurveActions/Apply.pressed.connect(_apply_selected_key)
	$Workspace/StatsColumn/CurveActions/Add.pressed.connect(_add_key)
	$Workspace/StatsColumn/CurveActions/Remove.pressed.connect(_remove_key)
	$Workspace/StatsColumn/CurveActions/Copy.pressed.connect(func(): curve_clipboard = curve_graph.get_keys())
	$Workspace/StatsColumn/CurveActions/Paste.pressed.connect(_paste_curve)
	$Workspace/StatsColumn/CurveActions/Reset.pressed.connect(_reset_curve)
	machine_setting.value_changed.connect(func(_value): _refresh_samples())
	technique_option.item_selected.connect(func(_index): _refresh_samples())
	technique_intensity.value_changed.connect(func(_value): _refresh_samples())
	boost_option.item_selected.connect(func(_index): _refresh_samples())
	start_speed.value_changed.connect(func(_value): _refresh_speed_preview())
	frame_perfect.toggled.connect(func(_value): _refresh_speed_preview())
	$Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Add.pressed.connect(_add_thruster)
	$Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Remove.pressed.connect(_remove_thruster)
	thruster_selector.item_selected.connect(func(_index): _refresh_thruster_controls())
	for key in vector_controls:
		var controls_value = vector_controls[key]
		if controls_value is Array:
			for control in controls_value:
				control.value_changed.connect(func(_value): _apply_visual_controls())
		else:
			controls_value.value_changed.connect(func(_value): _apply_visual_controls())
	title_input.text_changed.connect(func(_value): _mark_dirty())
	author_input.text_changed.connect(func(_value): _mark_dirty())
	description_input.text_changed.connect(func(_value): _mark_dirty())


func _new_draft() -> void:
	draft_id = "draft_%d_%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_msec() % 1000000]
	session = MxtCarAuthoringSession.new()
	title_input.text = "New Machine"
	var steam_name := ""
	if game_manager != null and game_manager.steam_service != null:
		steam_name = game_manager.steam_service.get_persona_name()
	author_input.text = steam_name if !steam_name.is_empty() else "Creator"
	description_input.text = ""
	_refresh_all()
	visual_status.text = "New draft. Import a static GLB or glTF vehicle model."


func _draft_root() -> String:
	return "%s/%s" % [DRAFTS_ROOT, draft_id]


func _refresh_draft_options() -> void:
	draft_option.clear()
	var root := DirAccess.open(DRAFTS_ROOT)
	if root == null:
		return
	root.list_dir_begin()
	var entry := root.get_next()
	while !entry.is_empty():
		if root.current_is_dir() and !entry.begins_with(".") and FileAccess.file_exists("%s/%s/package/manifest.json" % [DRAFTS_ROOT, entry]):
			draft_option.add_item(entry)
			draft_option.set_item_metadata(draft_option.item_count - 1, entry)
		entry = root.get_next()
	root.list_dir_end()


func _open_selected_draft() -> void:
	if draft_option.selected < 0:
		return
	var selected_id := String(draft_option.get_item_metadata(draft_option.selected))
	var result: Dictionary = session.load_vehicle_package("%s/%s/package" % [DRAFTS_ROOT, selected_id])
	if !bool(result.get("valid", false)):
		_show_diagnostics(result)
		return
	draft_id = selected_id
	title_input.text = String(result.get("title", selected_id))
	description_input.text = String(result.get("description", ""))
	author_input.text = String(result.get("author_name", "Creator"))
	visual_status.text = session.get_model_path()
	_refresh_all()


func _import_model(path: String) -> void:
	var result: Dictionary = session.import_model(path, _draft_root())
	_show_diagnostics(result)
	if bool(result.get("valid", false)):
		visual_status.text = "Imported %s" % path.get_file()
		_refresh_preview()


func _save_draft() -> Dictionary:
	_apply_visual_controls()
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(_draft_root()))
	var preview_path := _draft_root() + "/preview.png"
	var preview_image := preview_viewport.get_texture().get_image()
	if preview_image == null or preview_image.save_png(preview_path) != OK:
		var failed := {"valid": false, "errors": PackedStringArray(["Could not capture the vehicle preview image"]), "warnings": PackedStringArray()}
		_show_diagnostics(failed)
		return failed
	var result: Dictionary = session.build_vehicle_package(
		_draft_root() + "/package",
		preview_path,
		title_input.text,
		description_input.text,
		author_input.text)
	_show_diagnostics(result)
	if bool(result.get("valid", false)):
		_refresh_draft_options()
	return result


func _install_vehicle() -> String:
	var built := _save_draft()
	if !bool(built.get("valid", false)):
		return ""
	var package_io := MxtContentPackageIO.new()
	var archive_path := _draft_root() + "/%s.mxtpkg" % draft_id
	var exported: Dictionary = package_io.export_mxtpkg(_draft_root() + "/package", archive_path)
	if !bool(exported.get("valid", false)):
		_show_diagnostics(exported)
		return ""
	var imported: Dictionary = package_io.import_mxtpkg(archive_path, ProjectSettings.globalize_path(LOCAL_LIBRARY_ROOT))
	_show_diagnostics(imported)
	if !bool(imported.get("valid", false)):
		return ""
	var content_id := "mxt:vehicle:package:" + String(imported["package_digest"]).trim_prefix("sha256:")
	content_changed.emit()
	return content_id


func _export_package(path: String) -> void:
	var built := _save_draft()
	if !bool(built.get("valid", false)):
		return
	var destination := path if path.to_lower().ends_with(".mxtpkg") else path + ".mxtpkg"
	_show_diagnostics(MxtContentPackageIO.new().export_mxtpkg(_draft_root() + "/package", destination))


func _test_drive() -> void:
	var built := _save_draft()
	if !bool(built.get("valid", false)):
		return
	var package_io := MxtContentPackageIO.new()
	var archive_path := _draft_root() + "/test-drive.mxtpkg"
	var exported: Dictionary = package_io.export_mxtpkg(_draft_root() + "/package", archive_path)
	if !bool(exported.get("valid", false)):
		_show_diagnostics(exported)
		return
	var imported: Dictionary = package_io.import_mxtpkg(
		archive_path,
		ProjectSettings.globalize_path(TEST_DRIVE_LIBRARY_ROOT))
	_show_diagnostics(imported)
	if !bool(imported.get("valid", false)):
		return
	test_drive_requested.emit({
		"draft_id": draft_id,
		"package_path": String(imported.get("package_path", "")),
		"package_digest": String(imported.get("package_digest", "")),
	})


func _refresh_all() -> void:
	_refresh_stat_options()
	_refresh_visual_controls()
	_refresh_preview()
	_refresh_samples()
	_show_diagnostics(session.validate())


func _refresh_stat_options() -> void:
	var previous := current_stat
	stat_option.clear()
	var category := category_option.get_item_text(category_option.selected) if category_option.selected >= 0 else "All"
	var search := search_input.text.to_lower()
	for entry_value in stat_schema:
		var entry: Dictionary = entry_value
		var name := String(entry["name"])
		if current_layer != "base" and !bool(entry["supports_live_modifiers"]):
			continue
		if category != "All" and String(entry["category"]) != category:
			continue
		if !search.is_empty() and !name.to_lower().contains(search):
			continue
		stat_option.add_item(name.replace("_", " ").capitalize())
		stat_option.set_item_metadata(stat_option.item_count - 1, name)
		if name == previous:
			stat_option.select(stat_option.item_count - 1)
	if stat_option.item_count == 0:
		return
	if stat_option.selected < 0:
		stat_option.select(0)
	current_stat = String(stat_option.get_item_metadata(stat_option.selected))
	curve_graph.show_curve(session, current_layer, current_stat)


func _on_layer_selected(index: int) -> void:
	current_layer = String(layer_option.get_item_metadata(index))
	_refresh_stat_options()


func _on_stat_selected(index: int) -> void:
	current_stat = String(stat_option.get_item_metadata(index))
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_samples()


func _commit_curve(keys: Array) -> void:
	if current_layer == "s_boost":
		return
	var result: Dictionary = session.set_curve(current_layer, current_stat, keys)
	_show_diagnostics(result)
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_samples()


func _show_selected_key(index: int) -> void:
	var keys := curve_graph.get_keys()
	if index < 0 or index >= keys.size():
		return
	updating_controls = true
	var key: Dictionary = keys[index]
	key_time.value = float(key["time"])
	key_value.value = float(key["value"])
	key_tangent_in.value = float(key["tangent_in"])
	key_tangent_out.value = float(key["tangent_out"])
	key_time.editable = current_layer != "s_boost"
	key_tangent_in.editable = current_layer != "s_boost"
	key_tangent_out.editable = current_layer != "s_boost"
	updating_controls = false


func _apply_selected_key() -> void:
	if updating_controls:
		return
	if current_layer == "s_boost":
		session.set_s_boost_value(current_stat, key_value.value)
		curve_graph.show_curve(session, current_layer, current_stat)
		_refresh_samples()
		return
	var keys := curve_graph.get_keys()
	var index := curve_graph.selected_key
	if index < 0 or index >= keys.size():
		return
	keys[index] = {
		"time": key_time.value,
		"value": key_value.value,
		"tangent_in": key_tangent_in.value,
		"tangent_out": key_tangent_out.value,
	}
	_commit_curve(keys)


func _add_key() -> void:
	if current_layer == "s_boost":
		return
	var keys := curve_graph.get_keys()
	var time := machine_setting.value
	for key in keys:
		if absf(float(key["time"]) - time) < 0.001:
			return
	keys.append({"time": time, "value": session.sample_curve(current_layer, current_stat, time), "tangent_in": 0.0, "tangent_out": 0.0})
	keys.sort_custom(func(a, b): return float(a["time"]) < float(b["time"]))
	_commit_curve(keys)


func _remove_key() -> void:
	if current_layer == "s_boost":
		return
	var keys := curve_graph.get_keys()
	if keys.size() <= 1 or curve_graph.selected_key < 0:
		return
	keys.remove_at(curve_graph.selected_key)
	_commit_curve(keys)


func _paste_curve() -> void:
	if current_layer != "s_boost" and !curve_clipboard.is_empty():
		_commit_curve(curve_clipboard)


func _reset_curve() -> void:
	var schema: Dictionary = schema_by_name.get(current_stat, {})
	var value := float(schema.get("default_value", 0.0)) if current_layer == "base" or current_layer == "s_boost" else 1.0
	if current_layer == "s_boost":
		session.set_s_boost_value(current_stat, value)
		curve_graph.show_curve(session, current_layer, current_stat)
	else:
		_commit_curve([{"time": 0.0, "value": value, "tangent_in": 0.0, "tangent_out": 0.0}])


func _refresh_samples() -> void:
	machine_value.text = "%.3f" % machine_setting.value
	curve_graph.set_sample_setting(machine_setting.value)
	_refresh_speed_preview()


func _technique_name() -> String:
	return ["none", "mts", "quickturn"][technique_option.selected]


func _boost_name() -> String:
	return String(boost_option.get_item_metadata(boost_option.selected))


func _refresh_speed_preview() -> void:
	var result: Dictionary = session.simulate_speed_preview(
		machine_setting.value,
		start_speed.value,
		frame_perfect.button_pressed,
		_technique_name(),
		technique_intensity.value,
		_boost_name())
	if result.has("error"):
		speed_summary.text = String(result["error"])
		speed_graph.show_result({})
		return
	speed_summary.text = "Terminal %.2f km/h   Peak %.2f km/h   Settle %.2f s" % [
		float(result["terminal_speed_kmh"]),
		float(result["peak_speed_kmh"]),
		float(result["settle_time_seconds"]),
	]
	speed_graph.show_result(result)


func _vector_value(key: String) -> Vector3:
	var controls: Array = vector_controls[key]
	return Vector3(controls[0].value, controls[1].value, controls[2].value)


func _set_vector_value(key: String, value: Vector3) -> void:
	var controls: Array = vector_controls[key]
	for axis in range(3):
		controls[axis].set_value_no_signal(value[axis])


func _refresh_visual_controls() -> void:
	updating_controls = true
	var model_transform: Dictionary = session.get_model_transform()
	_set_vector_value("translation", model_transform.get("translation", Vector3.ZERO))
	_set_vector_value("rotation", model_transform.get("rotation_degrees", Vector3.ZERO))
	_set_vector_value("scale", model_transform.get("scale", Vector3.ONE))
	var tilt := session.get_tilt_corners()
	var wall := session.get_wall_corners()
	for i in range(4):
		_set_vector_value("tilt_%d" % i, tilt[i])
		_set_vector_value("wall_%d" % i, wall[i])
	_refresh_thruster_options()
	updating_controls = false


func _apply_visual_controls() -> void:
	if updating_controls:
		return
	session.set_model_transform({
		"translation": _vector_value("translation"),
		"rotation_degrees": _vector_value("rotation"),
		"scale": _vector_value("scale"),
	})
	var tilt := PackedVector3Array()
	var wall := PackedVector3Array()
	for i in range(4):
		tilt.push_back(_vector_value("tilt_%d" % i))
		wall.push_back(_vector_value("wall_%d" % i))
	session.set_tilt_corners(tilt)
	session.set_wall_corners(wall)
	var thrusters: Array = session.get_thrusters()
	if thruster_selector.selected >= 0 and thruster_selector.selected < thrusters.size():
		thrusters[thruster_selector.selected] = {
			"position": _vector_value("thruster_position"),
			"rotation_degrees": _vector_value("thruster_rotation"),
			"scale": vector_controls["thruster_scale"].value,
		}
		session.set_thrusters(thrusters)
	_refresh_preview()


func _refresh_thruster_options() -> void:
	var selected := thruster_selector.selected
	thruster_selector.clear()
	var thrusters: Array = session.get_thrusters()
	for i in range(thrusters.size()):
		thruster_selector.add_item("Thruster %d" % (i + 1))
	if !thrusters.is_empty():
		thruster_selector.select(clampi(selected, 0, thrusters.size() - 1))
	_refresh_thruster_controls()


func _refresh_thruster_controls() -> void:
	var thrusters: Array = session.get_thrusters()
	var enabled := thruster_selector.selected >= 0 and thruster_selector.selected < thrusters.size()
	thruster_rows.visible = enabled
	if !enabled:
		return
	updating_controls = true
	var value: Dictionary = thrusters[thruster_selector.selected]
	_set_vector_value("thruster_position", value.get("position", Vector3.ZERO))
	_set_vector_value("thruster_rotation", value.get("rotation_degrees", Vector3.ZERO))
	vector_controls["thruster_scale"].set_value_no_signal(float(value.get("scale", 1.0)))
	updating_controls = false


func _add_thruster() -> void:
	var thrusters: Array = session.get_thrusters()
	if thrusters.size() >= 8:
		return
	thrusters.append({"position": Vector3(0.0, 0.0, -1.5), "rotation_degrees": Vector3.ZERO, "scale": 0.5})
	session.set_thrusters(thrusters)
	_refresh_thruster_options()
	thruster_selector.select(thrusters.size() - 1)
	_refresh_thruster_controls()
	_refresh_preview()


func _remove_thruster() -> void:
	var thrusters: Array = session.get_thrusters()
	if thruster_selector.selected < 0 or thruster_selector.selected >= thrusters.size():
		return
	thrusters.remove_at(thruster_selector.selected)
	session.set_thrusters(thrusters)
	_refresh_thruster_options()
	_refresh_preview()


func _refresh_preview() -> void:
	preview_render_manager.clear_renderer()
	preview_definition = null
	var model_path := session.get_model_path()
	if model_path.is_empty() or !FileAccess.file_exists(model_path):
		return
	var record := {
		"content_id": "mxt:vehicle:draft:%s" % draft_id,
		"title": title_input.text,
		"authoritative_path": _draft_root() + "/package/vehicle/properties.mxt_car_props",
		"visual_path": model_path,
		"visual_metadata": {
			"model_transform": session.get_model_transform(),
			"thrusters": session.get_thrusters(),
		},
	}
	preview_definition = game_manager._car_definition_from_package_record(record)
	if preview_definition != null:
		preview_render_manager.configure_manual([preview_definition])
		preview_render_manager.begin_manual_submit()
		preview_render_manager.submit_manual_car(0, Transform3D.IDENTITY, Color.WHITE, Vector3.ZERO, Color.WHITE, 1.0, false)


func _show_diagnostics(result: Dictionary) -> void:
	var lines: Array[String] = []
	for error in result.get("errors", []):
		lines.append("[color=#ff6961]ERROR: %s[/color]" % error)
	for warning in result.get("warnings", []):
		lines.append("[color=#ffd166]WARNING: %s[/color]" % warning)
	if lines.is_empty() and bool(result.get("valid", false)):
		lines.append("[color=#73e2a7]Ready[/color]")
	diagnostics.text = "\n".join(lines)


func _mark_dirty() -> void:
	if !updating_controls:
		diagnostics.text = "[color=#ffd166]Unsaved draft changes[/color]"
