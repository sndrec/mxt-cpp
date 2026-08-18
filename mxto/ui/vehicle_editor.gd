class_name VehicleEditor extends VBoxContainer

signal content_changed
signal test_drive_requested(snapshot: Dictionary)

const PREVIEW_WORLD_SCENE: PackedScene = preload("res://ui/garage_preview_world.tscn")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const GaragePreviewCameraControllerClass = preload("res://ui/garage_preview_camera_controller.gd")
const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const VehicleGradePanelClass = preload("res://ui/vehicle_grade_panel.gd")
const DRAFTS_ROOT := "user://vehicle_drafts"
const LOCAL_LIBRARY_ROOT := "user://content/packages"
const TEST_DRIVE_LIBRARY_ROOT := "user://content/test_drive_snapshots"
const WORKSHOP_STAGING_ROOT := "user://content/workshop_staging"
const WORKSHOP_PREVIEW_TARGET_MAX_BYTES := 950_000
const WORKSHOP_PREVIEW_MIN_LONGEST_EDGE := 128
const AUTOSAVE_DEBOUNCE_MSEC := 900
const AUTOSAVE_RETRY_MSEC := 5000
const PERFORMANCE_DEBOUNCE_MSEC := 120

@onready var draft_option: OptionButton = $Toolbar/DraftOption
@onready var title_input: LineEdit = $Metadata/Title
@onready var author_input: LineEdit = $Metadata/Author
@onready var description_input: TextEdit = $Metadata/Description
@onready var preview_container: SubViewportContainer = $Workspace/VisualColumn/Preview
@onready var preview_viewport: SubViewport = $Workspace/VisualColumn/Preview/Viewport
@onready var visual_status: Label = $Workspace/VisualColumn/VisualStatus
@onready var preview_primary: ColorPickerButton = $Workspace/VisualColumn/PaintPreview/Primary
@onready var preview_secondary: ColorPickerButton = $Workspace/VisualColumn/PaintPreview/Secondary
@onready var preview_accent: ColorPickerButton = $Workspace/VisualColumn/PaintPreview/Accent
@onready var preview_preset: OptionButton = $Workspace/VisualColumn/PaintPreview/Preset
@onready var preview_diagnostic: OptionButton = $Workspace/VisualColumn/PaintPreview/Diagnostic
@onready var physical_tabs: TabContainer = $Workspace/VisualColumn/PhysicalTabs
@onready var transform_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Transform/Rows
@onready var body_surface_list: ItemList = $Workspace/VisualColumn/PhysicalTabs/Materials/BodySurfaces
@onready var albedo_surface_option: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Materials/AlbedoRow/Source
@onready var normal_surface_option: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Materials/NormalRow/Source
@onready var paint_mask_surface_option: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Materials/PaintMaskRow/Source
@onready var corner_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Corners/Rows
@onready var thruster_selector: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Selector
@onready var thruster_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Thrusters/Rows
@onready var search_input: LineEdit = $Workspace/StatsColumn/StatFilters/Search
@onready var category_option: OptionButton = $Workspace/StatsColumn/StatFilters/Category
@onready var advanced_mode: CheckBox = $Workspace/StatsColumn/StatFilters/AdvancedMode
@onready var layer_option: OptionButton = $Workspace/StatsColumn/StatFilters/Layer
@onready var stat_option: OptionButton = $Workspace/StatsColumn/StatFilters/Stat
@onready var curve_graph: VehicleEditorCurveGraph = $Workspace/StatsColumn/CurveGraph
@onready var authoring_mode_indicator: Label = $Workspace/StatsColumn/AuthoringMode/Indicator
@onready var make_custom_button: Button = $Workspace/StatsColumn/AuthoringMode/MakeCustom
@onready var revert_derived_button: Button = $Workspace/StatsColumn/AuthoringMode/RevertDerived
@onready var stat_help: RichTextLabel = $Workspace/StatsColumn/StatHelp
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
@onready var vehicle_grade_panel: VehicleGradePanelClass = $Workspace/VisualColumn/PhysicalTabs/Performance
@onready var import_model_dialog: FileDialog = $ImportModelDialog
@onready var export_package_dialog: FileDialog = $ExportPackageDialog
@onready var template_vehicle_dialog: ConfirmationDialog = $TemplateVehicleDialog
@onready var template_vehicle_option: OptionButton = $TemplateVehicleDialog/Rows/VehicleOption
@onready var workshop_visibility: OptionButton = $Workshop/Visibility
@onready var workshop_status: Label = $Workshop/Status
@onready var workshop_page_button: Button = $Workshop/OpenPage
@onready var workshop_publish_button: Button = $Toolbar/PublishWorkshop
@onready var autosave_status: Label = $Toolbar/AutosaveStatus
@onready var archive_draft_dialog: ConfirmationDialog = $ArchiveDraftDialog
@onready var workshop_capture_button: Button = $Workshop/CapturePreview
@onready var publish_review_dialog: ConfirmationDialog = $PublishReviewDialog
@onready var publish_review_summary: RichTextLabel = $PublishReviewDialog/Review/Summary
@onready var publish_review_preview: TextureRect = $PublishReviewDialog/Review/Preview
@onready var publish_changelog: TextEdit = $PublishReviewDialog/Review/Changelog

var game_manager: GameManager
var vehicle_content_controller: VehicleContentControllerClass
var session := MxtCarAuthoringSession.new()
var draft_store := MxtCarDraftStore.new()
var draft_id := ""
var current_properties_path := ""
var metadata_dirty := false
var draft_initialized := false
var autosave_due_msec := 0
var autosave_error := ""
var stat_schema: Array = []
var schema_by_name: Dictionary = {}
var current_layer := "base"
var current_stat := "weight_kg"
var curve_clipboard: Array = []
var preview_root: Node3D
var preview_camera: Camera3D
var preview_camera_controller := GaragePreviewCameraControllerClass.new()
var preview_framed_model_path := ""
var preview_livery: CarLivery = CarLivery.new()
var preview_render_manager: CarRenderManager
var preview_definition: CarDefinition
var preview_gizmo_root: Node3D
var preview_gizmo_line: MeshInstance3D
var preview_gizmo_markers: Array[MeshInstance3D] = []
var preview_gizmo_entries: Array[Dictionary] = []
var preview_gizmo_sphere: SphereMesh
var preview_gizmo_materials: Array[StandardMaterial3D] = []
var dragged_gizmo := -1
var dragged_gizmo_plane := Plane()
var workshop_request_id := 0
var workshop_operation := ""
var workshop_published_file_id := 0
var workshop_pending_package: Dictionary = {}
var workshop_progress_update_msec := 0
var vector_controls: Dictionary = {}
var updating_controls := false
var curve_gesture_active := false
var performance_analyzer := MxtCarPerformanceAnalyzer.new()
var performance_due_msec := 0
var workshop_preview_captured := false


func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	if game_manager != null:
		vehicle_content_controller = game_manager.get_node("VehicleContentController") as VehicleContentControllerClass
	stat_schema = session.get_stat_schema()
	for entry_value in stat_schema:
		var entry: Dictionary = entry_value
		schema_by_name[String(entry["name"])] = entry
	_setup_options()
	_setup_vector_controls()
	_setup_preview()
	_connect_controls()
	_refresh_draft_options()
	if draft_option.item_count > 0:
		_open_selected_draft()
	else:
		_new_draft()
	call_deferred("_connect_steam_service")


func _setup_options() -> void:
	for preset in ["Saved / Custom", "Default Blue", "Neutral White", "High Contrast", "Warm"]:
		preview_preset.add_item(preset)
	for diagnostic in ["Rendered", "Albedo", "Normal Map", "Paint Mask", "Selected Surfaces"]:
		preview_diagnostic.add_item(diagnostic)
	for visibility in ["Public", "Friends Only", "Private", "Unlisted"]:
		workshop_visibility.add_item(visibility)
	workshop_visibility.selected = 1
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
	_add_scalar_row(corner_rows, "front_width", "Front Width", 1.6, 0.01, 1000.0)
	_add_scalar_row(corner_rows, "rear_width", "Rear Width", 2.2, 0.01, 1000.0)
	_add_scalar_row(corner_rows, "front_forward_extent", "Front Forward Extent", 1.5, 0.01, 1000.0)
	_add_scalar_row(corner_rows, "rear_backward_extent", "Rear Backward Extent", 1.7, 0.01, 1000.0)
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


func _add_scalar_row(parent: VBoxContainer, key: String, label_text: String, value: float, minimum: float, maximum: float) -> void:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size.x = 150.0
	row.add_child(label)
	var control := SpinBox.new()
	control.min_value = minimum
	control.max_value = maximum
	control.step = 0.01
	control.allow_greater = true
	control.value = value
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)
	parent.add_child(row)
	vector_controls[key] = control


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
	preview_gizmo_root = Node3D.new()
	preview_gizmo_root.name = "AuthoringGizmos"
	preview_root.add_child(preview_gizmo_root)
	preview_gizmo_line = MeshInstance3D.new()
	preview_gizmo_root.add_child(preview_gizmo_line)
	preview_gizmo_sphere = SphereMesh.new()
	preview_gizmo_sphere.radius = 0.11
	preview_gizmo_sphere.height = 0.22
	preview_gizmo_sphere.radial_segments = 12
	preview_gizmo_sphere.rings = 6
	for colour in [Color(0.1, 0.9, 1.0), Color(1.0, 0.2, 0.75), Color(1.0, 0.75, 0.08)]:
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = colour
		material.no_depth_test = true
		preview_gizmo_materials.append(material)
	preview_camera = Camera3D.new()
	preview_camera.near = 0.05
	preview_camera.fov = 38.0
	preview_camera.position = Vector3(7.0, 3.7, 9.0)
	preview_root.add_child(preview_camera)
	preview_camera.look_at(Vector3.ZERO, Vector3.UP)
	preview_camera.current = true
	preview_camera_controller.configure_frame(Vector3(0.0, 0.5, 0.0), 12.0, true)
	preview_container.gui_input.connect(_on_preview_gui_input)


func _connect_controls() -> void:
	visibility_changed.connect(_on_visibility_changed)
	$Toolbar/NewDraft.pressed.connect(_new_draft)
	$Toolbar/OpenDraft.pressed.connect(_open_selected_draft)
	$Toolbar/DuplicateDraft.pressed.connect(_duplicate_current_draft)
	$Toolbar/ArchiveDraft.pressed.connect(func(): archive_draft_dialog.popup_centered())
	$Toolbar/ImportTemplate.pressed.connect(_open_template_vehicle_dialog)
	$Toolbar/ImportModel.pressed.connect(func(): import_model_dialog.popup_centered())
	$Toolbar/SaveDraft.pressed.connect(_manual_save_draft)
	$Toolbar/InstallVehicle.pressed.connect(_install_vehicle)
	$Toolbar/ExportPackage.pressed.connect(func(): export_package_dialog.popup_centered())
	$Toolbar/TestDrive.pressed.connect(_test_drive)
	$Toolbar/PublishWorkshop.pressed.connect(_publish_workshop)
	workshop_page_button.pressed.connect(_open_workshop_page)
	$Toolbar/Undo.pressed.connect(_undo)
	$Toolbar/Redo.pressed.connect(_redo)
	import_model_dialog.file_selected.connect(_import_model)
	export_package_dialog.file_selected.connect(_export_package)
	template_vehicle_dialog.confirmed.connect(_import_selected_vehicle_template)
	archive_draft_dialog.confirmed.connect(_archive_current_draft)
	workshop_capture_button.pressed.connect(_capture_workshop_preview)
	publish_review_dialog.confirmed.connect(_confirm_publish_workshop)
	diagnostics.meta_clicked.connect(_focus_diagnostic_category)
	search_input.text_changed.connect(func(_value): _refresh_stat_options())
	category_option.item_selected.connect(func(_index): _refresh_stat_options())
	advanced_mode.toggled.connect(_on_advanced_mode_toggled)
	layer_option.item_selected.connect(_on_layer_selected)
	stat_option.item_selected.connect(_on_stat_selected)
	curve_graph.edit_started.connect(_begin_curve_gesture)
	curve_graph.curve_preview_changed.connect(_preview_curve_gesture)
	curve_graph.edit_cancelled.connect(_cancel_curve_gesture)
	curve_graph.curve_committed.connect(_commit_curve)
	curve_graph.key_selected.connect(_show_selected_key)
	$Workspace/StatsColumn/CurveActions/Apply.pressed.connect(_apply_selected_key)
	$Workspace/StatsColumn/CurveActions/Add.pressed.connect(_add_key)
	$Workspace/StatsColumn/CurveActions/Remove.pressed.connect(_remove_key)
	$Workspace/StatsColumn/CurveActions/Copy.pressed.connect(func(): curve_clipboard = curve_graph.get_keys())
	$Workspace/StatsColumn/CurveActions/Paste.pressed.connect(_paste_curve)
	$Workspace/StatsColumn/CurveActions/Reset.pressed.connect(_reset_curve)
	make_custom_button.pressed.connect(_make_selected_special_custom)
	revert_derived_button.pressed.connect(_revert_selected_special_derived)
	machine_setting.value_changed.connect(func(_value): _refresh_samples())
	technique_option.item_selected.connect(func(_index): _refresh_samples())
	technique_intensity.value_changed.connect(func(_value): _refresh_samples())
	boost_option.item_selected.connect(func(_index): _refresh_samples())
	start_speed.value_changed.connect(func(_value): _refresh_speed_preview())
	frame_perfect.toggled.connect(func(_value): _refresh_speed_preview())
	$Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Add.pressed.connect(_add_thruster)
	$Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Remove.pressed.connect(_remove_thruster)
	thruster_selector.item_selected.connect(func(_index): _refresh_thruster_controls())
	body_surface_list.multi_selected.connect(func(_index, _selected): _apply_material_controls())
	albedo_surface_option.item_selected.connect(func(_index): _apply_material_controls())
	normal_surface_option.item_selected.connect(func(_index): _apply_material_controls())
	paint_mask_surface_option.item_selected.connect(func(_index): _apply_material_controls())
	physical_tabs.tab_changed.connect(func(_index): _refresh_gizmos())
	preview_primary.color_changed.connect(func(_colour): _on_preview_colours_changed())
	preview_secondary.color_changed.connect(func(_colour): _on_preview_colours_changed())
	preview_accent.color_changed.connect(func(_colour): _on_preview_colours_changed())
	preview_preset.item_selected.connect(_apply_preview_preset)
	preview_diagnostic.item_selected.connect(_apply_preview_diagnostic)
	for key in vector_controls:
		var controls_value = vector_controls[key]
		if controls_value is Array:
			for control in controls_value:
				control.value_changed.connect(func(_value): _apply_visual_controls())
		else:
			controls_value.value_changed.connect(func(_value): _apply_visual_controls())
	title_input.text_changed.connect(func(_value): _mark_dirty())
	author_input.text_changed.connect(func(_value): _mark_dirty())
	description_input.text_changed.connect(_mark_dirty)


func _new_draft() -> bool:
	if draft_initialized and !_flush_autosave():
		return false
	draft_id = "draft_%d_%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_msec() % 1000000]
	session = MxtCarAuthoringSession.new()
	draft_initialized = true
	current_properties_path = ""
	preview_livery = CarLivery.new()
	updating_controls = true
	title_input.text = "New Machine"
	var steam_name := ""
	if game_manager != null and game_manager.steam_service != null:
		steam_name = game_manager.steam_service.get_persona_name()
	author_input.text = steam_name if !steam_name.is_empty() else "Creator"
	description_input.text = ""
	updating_controls = false
	workshop_published_file_id = 0
	workshop_preview_captured = false
	metadata_dirty = true
	autosave_error = ""
	_refresh_workshop_controls()
	_refresh_all()
	visual_status.text = "New draft. Import a static GLB or glTF vehicle model."
	if !_autosave_draft():
		return false
	return true


func _draft_root() -> String:
	return "%s/%s" % [DRAFTS_ROOT, draft_id]


func _refresh_draft_options() -> void:
	var selected_id := draft_id
	draft_option.clear()
	var drafts: Array = draft_store.list_drafts()
	drafts.sort_custom(func(a, b): return int(a.get("modified_unix", 0)) > int(b.get("modified_unix", 0)))
	for draft_value in drafts:
		var draft: Dictionary = draft_value
		var modified := Time.get_datetime_string_from_unix_time(int(draft.get("modified_unix", 0)), true)
		var status := String(draft.get("status", "invalid")).replace("_", " ")
		var label := "%s — %s [%s]" % [String(draft.get("title", "Untitled Machine")), modified, status]
		var thumbnail := String(draft.get("thumbnail_path", ""))
		if !thumbnail.is_empty():
			var image := Image.load_from_file(thumbnail)
			if image != null and !image.is_empty():
				image.resize(48, 32, Image.INTERPOLATE_LANCZOS)
				draft_option.add_icon_item(ImageTexture.create_from_image(image), label)
			else:
				draft_option.add_item(label)
		else:
			draft_option.add_item(label)
		var index := draft_option.item_count - 1
		draft_option.set_item_metadata(index, String(draft["draft_id"]))
		if String(draft["draft_id"]) == selected_id:
			draft_option.select(index)


func _open_selected_draft() -> void:
	if draft_option.selected < 0:
		return
	var selected_id := String(draft_option.get_item_metadata(draft_option.selected))
	if draft_initialized and selected_id == draft_id:
		return
	if draft_initialized and !_flush_autosave():
		return
	var candidate := MxtCarAuthoringSession.new()
	var result: Dictionary = draft_store.load_draft(selected_id, candidate)
	if !bool(result.get("valid", false)):
		_show_diagnostics(result)
		return
	session = candidate
	draft_id = selected_id
	draft_initialized = true
	current_properties_path = String(result.get("properties_path", ""))
	preview_livery = CarLivery.new()
	preview_livery.from_dict(result.get("preview_livery", {}))
	updating_controls = true
	title_input.text = String(result.get("title", selected_id))
	description_input.text = String(result.get("description", ""))
	author_input.text = String(result.get("author_name", "Creator"))
	updating_controls = false
	workshop_published_file_id = int(result.get("workshop_published_file_id", 0))
	workshop_preview_captured = FileAccess.file_exists(_workshop_preview_path())
	metadata_dirty = false
	autosave_error = ""
	_refresh_workshop_controls()
	visual_status.text = session.get_model_path()
	_refresh_all()
	_update_autosave_status("Saved")


func _duplicate_current_draft() -> void:
	if !draft_initialized or !_flush_autosave():
		return
	var new_id := "draft_%d_%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_msec() % 1000000]
	var result: Dictionary = draft_store.duplicate_draft(draft_id, new_id, title_input.text + " Copy")
	_show_diagnostics(result)
	if !bool(result.get("valid", false)):
		return
	_refresh_draft_options()
	for i in range(draft_option.item_count):
		if String(draft_option.get_item_metadata(i)) == new_id:
			draft_option.select(i)
			break
	_open_selected_draft()


func _archive_current_draft() -> void:
	if !draft_initialized or !_flush_autosave():
		return
	var archived_id := draft_id
	var result: Dictionary = draft_store.archive_draft(archived_id)
	_show_diagnostics(result)
	if !bool(result.get("valid", false)):
		return
	draft_initialized = false
	draft_id = ""
	_refresh_draft_options()
	if draft_option.item_count > 0:
		draft_option.select(0)
		_open_selected_draft()
	else:
		_new_draft()


func _open_template_vehicle_dialog() -> void:
	template_vehicle_option.clear()
	if game_manager == null:
		return
	for definition_value in vehicle_content_controller.definitions:
		var definition := definition_value as CarDefinition
		if definition == null or !definition.has_visual() or definition.properties_path.is_empty():
			continue
		template_vehicle_option.add_item(definition.name)
		template_vehicle_option.set_item_metadata(template_vehicle_option.item_count - 1, definition.content_id)
	if template_vehicle_option.item_count == 0:
		var unavailable := {"valid": false, "errors": PackedStringArray(["No editable garage vehicles are available as templates"]), "warnings": PackedStringArray()}
		_show_diagnostics(unavailable)
		return
	template_vehicle_option.select(0)
	template_vehicle_dialog.popup_centered()


func _import_selected_vehicle_template() -> void:
	if game_manager == null or template_vehicle_option.selected < 0:
		return
	var content_id := String(template_vehicle_option.get_item_metadata(template_vehicle_option.selected))
	var definition := vehicle_content_controller.definitions_by_content_id.get(content_id) as CarDefinition
	if definition == null:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["The selected vehicle is no longer available"]), "warnings": PackedStringArray()})
		return
	if !_new_draft():
		return
	var result := _copy_vehicle_template(definition)
	_show_diagnostics(result)
	if !bool(result.get("valid", false)):
		return
	title_input.text = "%s Template" % definition.name
	description_input.text = "Based on %s." % definition.name
	visual_status.text = "Template copied from %s" % definition.name
	_refresh_all()


func _copy_vehicle_template(definition: CarDefinition) -> Dictionary:
	var properties_result: Dictionary = session.load_file(definition.properties_path)
	if !bool(properties_result.get("valid", false)):
		return properties_result
	var measurements: Dictionary = session.get_collision_measurements()
	if !bool(measurements.get("valid", false)):
		return {
			"valid": false,
			"errors": PackedStringArray(["The template has unsupported asymmetric collision geometry: %s" % String(measurements.get("error", "unknown geometry error"))]),
			"warnings": PackedStringArray(),
		}
	var visual_result: Dictionary
	if definition.car_scene != null:
		visual_result = _copy_official_vehicle_visual(definition)
	else:
		var record: Dictionary = vehicle_content_controller.content_catalog.resolve_content(definition.content_id)
		visual_result = _copy_packaged_vehicle_visual(record)
	if !bool(visual_result.get("valid", false)):
		return visual_result
	return session.validate()


func _copy_packaged_vehicle_visual(record: Dictionary) -> Dictionary:
	var source_path := String(record.get("visual_path", ""))
	if source_path.is_empty() or !FileAccess.file_exists(source_path):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle package has no readable model"]), "warnings": PackedStringArray()}
	var imported: Dictionary = session.import_model(source_path, _draft_root())
	if !bool(imported.get("valid", false)):
		return imported
	var visual_metadata: Dictionary = record.get("visual_metadata", {})
	if !session.set_model_transform(visual_metadata.get("model_transform", {})):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid model-transform metadata"]), "warnings": PackedStringArray()}
	var material_setup: Dictionary = visual_metadata.get("material_inputs", {}).duplicate(true)
	material_setup["body_surfaces"] = visual_metadata.get("body_surfaces", [])
	if !session.set_material_setup(material_setup):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid material metadata"]), "warnings": PackedStringArray()}
	if !session.set_thrusters(visual_metadata.get("thrusters", [])):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid thruster metadata"]), "warnings": PackedStringArray()}
	var intent_result: Dictionary = session.set_authoring_intent(record.get("authoring_metadata", {}))
	if !bool(intent_result.get("valid", false)):
		return intent_result
	return {"valid": true, "errors": PackedStringArray(), "warnings": PackedStringArray()}


func _copy_official_vehicle_visual(definition: CarDefinition) -> Dictionary:
	var source := definition.car_scene.instantiate() as Node3D
	if source == null:
		return {"valid": false, "errors": PackedStringArray(["The selected built-in vehicle visual could not be loaded"]), "warnings": PackedStringArray()}
	var body := source.get_node_or_null("VEHICLE_MAIN") as MeshInstance3D
	if body == null or body.mesh == null:
		source.free()
		return {"valid": false, "errors": PackedStringArray(["The selected built-in vehicle has no body mesh"]), "warnings": PackedStringArray()}
	var export_root := Node3D.new()
	var export_body := MeshInstance3D.new()
	export_body.name = "VehicleBody"
	export_body.mesh = body.mesh
	export_body.material_override = _gltf_material_from_official_body(body.material_override)
	export_root.add_child(export_body)
	var draft_path := ProjectSettings.globalize_path(_draft_root())
	if DirAccess.make_dir_recursive_absolute(draft_path) != OK:
		export_root.free()
		source.free()
		return {"valid": false, "errors": PackedStringArray(["The new vehicle draft directory could not be created"]), "warnings": PackedStringArray()}
	var gltf_document := GLTFDocument.new()
	var gltf_state := GLTFState.new()
	var append_error := gltf_document.append_from_scene(export_root, gltf_state)
	var source_path := _draft_root() + "/template-source.glb"
	var write_error := FAILED if append_error != OK else gltf_document.write_to_filesystem(gltf_state, source_path)
	export_root.free()
	if append_error != OK or write_error != OK:
		source.free()
		return {"valid": false, "errors": PackedStringArray(["The selected built-in vehicle could not be converted into an editable GLB"]), "warnings": PackedStringArray()}
	var imported: Dictionary = session.import_model(source_path, _draft_root())
	DirAccess.remove_absolute(ProjectSettings.globalize_path(source_path))
	if !bool(imported.get("valid", false)):
		source.free()
		return imported
	var root_transform := source.transform
	var body_transform := root_transform * body.transform
	var transform_result := {
		"translation": body_transform.origin,
		"rotation_degrees": body_transform.basis.get_euler() * (180.0 / PI),
		"scale": body_transform.basis.get_scale(),
	}
	if !session.set_model_transform(transform_result):
		source.free()
		return {"valid": false, "errors": PackedStringArray(["The built-in vehicle model transform could not be imported"]), "warnings": PackedStringArray()}
	var surfaces := session.get_model_surfaces()
	var body_surfaces: Array = []
	for surface_value in surfaces:
		body_surfaces.append(int((surface_value as Dictionary).get("index", -1)))
	var material_setup := {
		"body_surfaces": body_surfaces,
		"albedo_surface": _first_surface_with_texture(surfaces, "has_albedo_texture"),
		"normal_surface": _first_surface_with_texture(surfaces, "has_normal_texture"),
		"paint_mask_surface": _first_surface_with_texture(surfaces, "has_paint_mask_texture"),
	}
	if !session.set_material_setup(material_setup):
		source.free()
		return {"valid": false, "errors": PackedStringArray(["The built-in vehicle materials could not be imported"]), "warnings": PackedStringArray()}
	var thrusters: Array = []
	var thruster_root := source.get_node_or_null("THRUSTERS") as Node3D
	if thruster_root != null:
		for child in thruster_root.get_children():
			var thruster := child as Node3D
			if thruster == null:
				continue
			var transform := root_transform * thruster_root.transform * thruster.transform
			var scale := transform.basis.get_scale()
			thrusters.append({
				"position": transform.origin,
				"rotation_degrees": transform.basis.get_euler() * (180.0 / PI),
				"scale": (scale.x + scale.y + scale.z) / 3.0,
			})
	source.free()
	if !session.set_thrusters(thrusters):
		return {"valid": false, "errors": PackedStringArray(["The built-in vehicle thrusters could not be imported"]), "warnings": PackedStringArray()}
	return {"valid": true, "errors": PackedStringArray(), "warnings": PackedStringArray()}


func _gltf_material_from_official_body(source: Material) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	var shader_material := source as ShaderMaterial
	if shader_material == null:
		return material
	var albedo = shader_material.get_shader_parameter("in_albedo")
	if albedo is Texture2D:
		material.albedo_texture = _gltf_exportable_texture(albedo)
	var normal = shader_material.get_shader_parameter("in_normal")
	if normal is Texture2D:
		material.normal_enabled = true
		material.normal_texture = _gltf_exportable_texture(normal)
	var paint_mask = shader_material.get_shader_parameter("in_paint_mask")
	if paint_mask is Texture2D:
		material.ao_enabled = true
		material.ao_texture = _gltf_exportable_texture(paint_mask)
	return material


func _gltf_exportable_texture(source: Texture2D) -> ImageTexture:
	var image := source.get_image()
	if image == null or image.is_empty():
		image = Image.create(1, 1, false, Image.FORMAT_RGBA8)
		image.fill(Color.WHITE)
	elif image.is_compressed():
		image = image.duplicate()
		image.decompress()
	return ImageTexture.create_from_image(image)


func _first_surface_with_texture(surfaces: Array, key: String) -> int:
	for surface_value in surfaces:
		var surface: Dictionary = surface_value
		if bool(surface.get(key, false)):
			return int(surface.get("index", -1))
	return -1


func _connect_steam_service() -> void:
	if game_manager == null or game_manager.steam_service == null:
		return
	var service := game_manager.steam_service
	if !service.workshop_request_completed.is_connected(_on_workshop_request_completed):
		service.workshop_request_completed.connect(_on_workshop_request_completed)
	if !service.status_changed.is_connected(_on_steam_status_changed):
		service.status_changed.connect(_on_steam_status_changed)
	_on_steam_status_changed(service.get_status())


func _on_steam_status_changed(status: Dictionary) -> void:
	if bool(status.get("initialized", false)) and author_input.text == "Creator":
		var persona_name := String(status.get("persona_name", ""))
		if !persona_name.is_empty():
			author_input.text = persona_name
	_refresh_workshop_controls()


func _refresh_workshop_controls() -> void:
	var steam_available := game_manager != null \
		and game_manager.steam_service != null \
		and game_manager.steam_service.is_initialized()
	workshop_publish_button.disabled = workshop_request_id != 0 or !steam_available
	workshop_page_button.disabled = workshop_request_id != 0 or workshop_published_file_id <= 0
	workshop_capture_button.disabled = workshop_request_id != 0
	if workshop_request_id != 0:
		return
	if !steam_available:
		workshop_status.text = "Steam Workshop is unavailable"
		return
	if workshop_published_file_id > 0:
		workshop_status.text = "Workshop item %d · %s" % [workshop_published_file_id, "preview captured" if workshop_preview_captured else "capture preview before update"]
	else:
		workshop_status.text = "Not published · %s" % ("preview captured" if workshop_preview_captured else "capture preview before publishing")


func _publish_workshop() -> void:
	if workshop_request_id != 0:
		return
	if game_manager == null or game_manager.steam_service == null or !game_manager.steam_service.is_initialized():
		workshop_status.text = "Steam Workshop is unavailable"
		return
	if !workshop_preview_captured or !FileAccess.file_exists(_workshop_preview_path()):
		workshop_status.text = "Capture the Workshop preview after framing and painting the machine."
		return
	var built := _build_package(_workshop_preview_path())
	if !bool(built.get("valid", false)):
		return
	var package_io := MxtContentPackageIO.new()
	var archive_path := _draft_root() + "/workshop-upload.mxtpkg"
	var exported: Dictionary = package_io.export_mxtpkg(String(built["package_path"]), archive_path)
	if !bool(exported.get("valid", false)):
		_show_diagnostics(exported)
		return
	var imported: Dictionary = package_io.import_mxtpkg(
		archive_path,
		ProjectSettings.globalize_path(WORKSHOP_STAGING_ROOT))
	if !bool(imported.get("valid", false)):
		_show_diagnostics(imported)
		return
	workshop_pending_package = imported
	_show_publish_review(built)


func _confirm_publish_workshop() -> void:
	if workshop_pending_package.is_empty() or workshop_request_id != 0:
		return
	if workshop_published_file_id <= 0:
		workshop_operation = "create_item"
		workshop_status.text = "Creating Workshop item..."
		workshop_request_id = game_manager.steam_service.create_workshop_item()
		_refresh_workshop_controls()
	else:
		_submit_workshop_update()


func _show_publish_review(validation: Dictionary) -> void:
	var visibility: String = ["Public", "Friends Only", "Private", "Unlisted"][workshop_visibility.selected]
	var intent_counts := _authoring_intent_counts()
	var warnings_value = validation.get("warnings", [])
	var warning_type := typeof(warnings_value)
	var warning_count: int = warnings_value.size() if warning_type == TYPE_ARRAY or warning_type == TYPE_PACKED_STRING_ARRAY else 0
	publish_review_summary.text = "%s Workshop item\n\nTitle: %s\nAuthor: %s\nVisibility: %s\nDescription:\n%s\n\nSpecial adjustments: %d Derived · %d Custom\nValidation: Ready%s" % [
		"Update" if workshop_published_file_id > 0 else "New",
		title_input.text,
		author_input.text,
		visibility,
		description_input.text,
		int(intent_counts.get("derived", 0)),
		int(intent_counts.get("custom", 0)),
		" · %d warning(s)" % warning_count if warning_count > 0 else "",
	]
	var image := Image.load_from_file(_workshop_preview_path())
	publish_review_preview.texture = ImageTexture.create_from_image(image) if image != null and !image.is_empty() else null
	publish_review_dialog.ok_button_text = "Update Workshop Item" if workshop_published_file_id > 0 else "Create Workshop Item"
	publish_changelog.text = ""
	publish_review_dialog.popup_centered(Vector2i(720, 650))


func _authoring_intent_counts() -> Dictionary:
	var derived := 0
	var custom := 0
	var layers: Array[String] = []
	for layer in session.get_layer_names():
		if String(layer) != "base":
			layers.append(String(layer))
	layers.append("s_boost")
	for layer in layers:
		for schema_value in stat_schema:
			var schema: Dictionary = schema_value
			if !bool(schema.get("supports_live_modifiers", false)):
				continue
			if session.is_special_derived(layer, String(schema.get("name", ""))):
				derived += 1
			else:
				custom += 1
	return {"derived": derived, "custom": custom}


func _submit_workshop_update() -> void:
	var metadata := JSON.stringify({
		"content_type": "vehicle",
		"format_revision": 1,
		"gameplay_digest": String(workshop_pending_package.get("gameplay_digest", "")),
	})
	var visibility: String = ["public", "friends_only", "private", "unlisted"][workshop_visibility.selected]
	workshop_operation = "submit_update"
	workshop_status.text = "Preparing Workshop upload..."
	workshop_request_id = game_manager.steam_service.submit_workshop_item_update(
		workshop_published_file_id,
		title_input.text,
		description_input.text,
		String(workshop_pending_package.get("package_path", "")),
		String(workshop_pending_package.get("package_path", "")).path_join("preview.png"),
		["Vehicle", "Format Revision 1"],
		metadata,
		visibility,
		publish_changelog.text.strip_edges())
	_refresh_workshop_controls()


func _on_workshop_request_completed(request_id: int, operation: String, result: Dictionary) -> void:
	if request_id != workshop_request_id or operation != workshop_operation:
		return
	workshop_request_id = 0
	workshop_operation = ""
	if !bool(result.get("success", false)):
		_refresh_workshop_controls()
		workshop_status.text = "Workshop failed: %s" % String(result.get("message", "Unknown error"))
		return
	if operation == "create_item":
		workshop_published_file_id = int(result.get("published_file_id", 0))
		_mark_dirty()
		_flush_autosave()
		_refresh_workshop_controls()
		_submit_workshop_update()
		return
	_refresh_workshop_controls()
	var agreement := " Accept the Steam Workshop agreement." if bool(result.get("legal_agreement_required", false)) else ""
	workshop_status.text = "Workshop upload complete.%s" % agreement
	if bool(result.get("legal_agreement_required", false)):
		game_manager.steam_service.open_workshop_item_page(workshop_published_file_id)


func _open_workshop_page() -> void:
	if workshop_published_file_id > 0 and game_manager != null and game_manager.steam_service != null:
		game_manager.steam_service.open_workshop_item_page(workshop_published_file_id)


func _process(_delta: float) -> void:
	var now := Time.get_ticks_msec()
	if performance_due_msec != 0 and now >= performance_due_msec:
		performance_due_msec = 0
		vehicle_grade_panel.show_analysis(performance_analyzer.analyze_session(session, machine_setting.value))
	if draft_initialized and !curve_gesture_active and (metadata_dirty or session.is_dirty()):
		if autosave_due_msec == 0:
			autosave_due_msec = now + AUTOSAVE_DEBOUNCE_MSEC
			_update_autosave_status("Unsaved changes")
		elif now >= autosave_due_msec:
			_autosave_draft()
	if workshop_request_id != 0 and workshop_operation == "submit_update" \
			and game_manager != null and game_manager.steam_service != null \
			and now >= workshop_progress_update_msec + 200:
		workshop_progress_update_msec = now
		var progress: Dictionary = game_manager.steam_service.get_workshop_update_progress()
		if bool(progress.get("active", false)):
			var total := int(progress.get("total_bytes", 0))
			var processed := int(progress.get("processed_bytes", 0))
			var percent := 0.0 if total <= 0 else 100.0 * float(processed) / float(total)
			workshop_status.text = "%s  %.1f%%" % [String(progress.get("status", "uploading")).replace("_", " ").capitalize(), percent]


func _exit_tree() -> void:
	if draft_initialized:
		_flush_autosave()


func _on_visibility_changed() -> void:
	if draft_initialized and !is_visible_in_tree():
		_flush_autosave()


func flush_pending_changes() -> bool:
	return _flush_autosave()


func _import_model(path: String) -> void:
	visual_status.text = "Importing %s..." % path.get_file()
	var normalized := _normalize_imported_vehicle_model(path)
	if !bool(normalized.get("valid", false)):
		_show_model_import_failure(normalized)
		return
	var normalized_path := String(normalized.get("path", ""))
	var result: Dictionary = session.import_model(normalized_path, _draft_root())
	DirAccess.remove_absolute(ProjectSettings.globalize_path(normalized_path))
	_show_diagnostics(result)
	if bool(result.get("valid", false)):
		visual_status.text = "Imported %s" % path.get_file()
		preview_framed_model_path = ""
		_refresh_visual_controls()
		_refresh_preview()
	else:
		_show_model_import_failure(result)


func _normalize_imported_vehicle_model(path: String) -> Dictionary:
	var source := FileAccess.open(path, FileAccess.READ)
	if source == null:
		return _model_import_error("The selected model could not be opened")
	if source.get_length() > 48 * 1024 * 1024:
		return _model_import_error("The selected model exceeds the 48 MiB vehicle-model limit")
	source.close()
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	state.base_path = path.get_base_dir()
	var load_error := document.append_from_file(path, state)
	if load_error != OK:
		return _model_import_error("Godot could not read the selected model: %s" % error_string(load_error))
	var imported_scene := document.generate_scene(state) as Node3D
	if imported_scene == null:
		return _model_import_error("Godot could not create a scene from the selected model")
	var merged_mesh := ArrayMesh.new()
	var surface_count := _append_imported_mesh_surfaces(imported_scene, Transform3D.IDENTITY, merged_mesh)
	imported_scene.free()
	if surface_count == 0:
		return _model_import_error("The selected model contains no static mesh surfaces")
	var export_root := Node3D.new()
	var export_body := MeshInstance3D.new()
	export_body.name = "VehicleBody"
	export_body.mesh = merged_mesh
	export_root.add_child(export_body)
	var normalized_document := GLTFDocument.new()
	var normalized_state := GLTFState.new()
	var append_error := normalized_document.append_from_scene(export_root, normalized_state)
	var normalized_path := _draft_root() + "/normalized-model-import.glb"
	var draft_path := ProjectSettings.globalize_path(_draft_root())
	var directory_error := DirAccess.make_dir_recursive_absolute(draft_path)
	var write_error := FAILED if append_error != OK or directory_error != OK else normalized_document.write_to_filesystem(normalized_state, normalized_path)
	export_root.free()
	if append_error != OK or write_error != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(normalized_path))
		return _model_import_error("The selected model could not be converted into an editable vehicle mesh")
	return {"valid": true, "errors": PackedStringArray(), "warnings": PackedStringArray(), "path": normalized_path}


func _append_imported_mesh_surfaces(node: Node, parent_transform: Transform3D, target: ArrayMesh) -> int:
	var transform := parent_transform
	var node_3d := node as Node3D
	if node_3d != null:
		transform *= node_3d.transform
	var appended := 0
	var mesh_instance := node as MeshInstance3D
	if mesh_instance != null and mesh_instance.mesh != null:
		var mesh := mesh_instance.mesh
		for surface in range(mesh.get_surface_count()):
			var surface_tool := SurfaceTool.new()
			surface_tool.append_from(mesh, surface, transform)
			var material := mesh_instance.material_override
			if material == null:
				material = mesh_instance.get_surface_override_material(surface)
			if material == null:
				material = mesh.surface_get_material(surface)
			surface_tool.set_material(material)
			var previous_count := target.get_surface_count()
			if surface_tool.commit(target) != null and target.get_surface_count() > previous_count:
				var surface_name: String = mesh.surface_get_name(surface)
				if !surface_name.is_empty():
					target.surface_set_name(previous_count, surface_name)
				appended += 1
	for child in node.get_children():
		appended += _append_imported_mesh_surfaces(child, transform, target)
	return appended


func _model_import_error(message: String) -> Dictionary:
	return {"valid": false, "errors": PackedStringArray([message]), "warnings": PackedStringArray()}


func _show_model_import_failure(result: Dictionary) -> void:
	_show_diagnostics(result)
	var errors: Array = result.get("errors", [])
	visual_status.text = "Import failed" if errors.is_empty() else "Import failed: %s" % String(errors[0])


func _draft_metadata() -> Dictionary:
	return {
		"title": title_input.text,
		"author_name": author_input.text,
		"description": description_input.text,
		"workshop_published_file_id": workshop_published_file_id,
		"authoring_intent": session.get_authoring_intent(),
		"preview_livery": preview_livery.to_dict(),
	}


func _autosave_draft() -> bool:
	if !draft_initialized:
		return true
	var result: Dictionary = draft_store.save_draft(draft_id, session, _draft_metadata())
	if !bool(result.get("valid", false)):
		var errors: PackedStringArray = result.get("errors", PackedStringArray(["Unknown autosave error"]))
		autosave_error = "Unknown autosave error" if errors.is_empty() else String(errors[0])
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_RETRY_MSEC
		_update_autosave_status("Autosave failed: %s" % autosave_error, true)
		_show_diagnostics(result)
		return false
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0
	current_properties_path = String(result.get("properties_path", ""))
	_update_autosave_status("Saved")
	_refresh_draft_options()
	return true


func _flush_autosave() -> bool:
	if curve_gesture_active:
		curve_graph.cancel_active_edit()
	if !draft_initialized:
		return true
	if metadata_dirty or session.is_dirty() or !autosave_error.is_empty():
		return _autosave_draft()
	return true


func _manual_save_draft() -> void:
	if !_flush_autosave():
		return
	var thumbnail_path := _draft_root() + "/thumbnail.png"
	if _save_preview_png(thumbnail_path):
		_refresh_draft_options()
		_update_autosave_status("Saved with thumbnail")
	else:
		_update_autosave_status("Saved; previous thumbnail kept")


func _build_package(preview_override := "") -> Dictionary:
	if !_flush_autosave():
		return {"valid": false, "errors": PackedStringArray([autosave_error]), "warnings": PackedStringArray()}
	var preview_path := String(preview_override)
	var preview_ready := false
	if preview_path.is_empty():
		preview_path = _draft_root() + "/thumbnail.png"
		preview_ready = _save_preview_png(preview_path)
	else:
		preview_ready = FileAccess.file_exists(preview_path)
	if !preview_ready:
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


func _workshop_preview_path() -> String:
	return _draft_root() + "/workshop-preview.png"


func _capture_workshop_preview() -> void:
	if !draft_initialized:
		return
	workshop_preview_captured = _save_preview_png(_workshop_preview_path())
	workshop_status.text = "Workshop preview captured from the current camera and paint." if workshop_preview_captured else "Workshop preview capture failed."
	_refresh_workshop_controls()


func _save_preview_png(preview_path: String) -> bool:
	var preview_image := preview_viewport.get_texture().get_image()
	if preview_image == null or preview_image.is_empty():
		return false
	while true:
		if preview_image.save_png(preview_path) != OK:
			return false
		var preview_file := FileAccess.open(preview_path, FileAccess.READ)
		if preview_file == null:
			return false
		var preview_bytes := preview_file.get_length()
		preview_file.close()
		if preview_bytes < WORKSHOP_PREVIEW_TARGET_MAX_BYTES:
			return true
		var longest_edge := maxi(preview_image.get_width(), preview_image.get_height())
		if longest_edge <= WORKSHOP_PREVIEW_MIN_LONGEST_EDGE:
			return false
		var scale := maxf(0.75, float(WORKSHOP_PREVIEW_MIN_LONGEST_EDGE) / float(longest_edge))
		preview_image.resize(
			maxi(1, int(floor(float(preview_image.get_width()) * scale))),
			maxi(1, int(floor(float(preview_image.get_height()) * scale))),
			Image.INTERPOLATE_LANCZOS)
	return false


func _install_vehicle() -> String:
	var built := _build_package()
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
	var built := _build_package()
	if !bool(built.get("valid", false)):
		return
	var destination := path if path.to_lower().ends_with(".mxtpkg") else path + ".mxtpkg"
	_show_diagnostics(MxtContentPackageIO.new().export_mxtpkg(_draft_root() + "/package", destination))


func _test_drive() -> void:
	var built := _build_package()
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
	_refresh_preview_paint_controls()
	_refresh_resource_usage()
	_refresh_preview()
	_refresh_samples()
	_show_diagnostics(session.validate())
	_refresh_history_buttons()


func _refresh_history_buttons() -> void:
	$Toolbar/Undo.disabled = !session.can_undo()
	$Toolbar/Redo.disabled = !session.can_redo()


func _undo() -> void:
	if session.undo():
		_refresh_all()
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_DEBOUNCE_MSEC


func _redo() -> void:
	if session.redo():
		_refresh_all()
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_DEBOUNCE_MSEC


func _update_autosave_status(message: String, failed := false) -> void:
	autosave_status.text = message
	autosave_status.modulate = Color(1.0, 0.42, 0.35) if failed else Color.WHITE


func _refresh_resource_usage() -> void:
	var usage: Dictionary = session.get_model_resource_usage()
	if !bool(usage.get("valid", false)):
		visual_status.text = "Import a static GLB or glTF vehicle model."
		return
	visual_status.text = "Model: %.2f / %.0f MiB   %d / %d triangles   %d / %d vertices   %d / %d texture pixels (%d / %d images)" % [
		float(usage["file_bytes"]) / (1024.0 * 1024.0),
		float(usage["file_byte_limit"]) / (1024.0 * 1024.0),
		int(usage["triangles"]), int(usage["triangle_limit"]),
		int(usage["vertices"]), int(usage["vertex_limit"]),
		int(usage["texture_pixels"]), int(usage["texture_pixel_limit"]),
		int(usage["images"]), int(usage["image_limit"]),
	]


func _refresh_preview_paint_controls() -> void:
	updating_controls = true
	preview_preset.select(0)
	preview_primary.color = preview_livery.primary_colour
	preview_secondary.color = preview_livery.secondary_colour
	preview_accent.color = preview_livery.accent_colour
	updating_controls = false


func _on_preview_colours_changed() -> void:
	if updating_controls:
		return
	preview_livery.primary_colour = preview_primary.color
	preview_livery.secondary_colour = preview_secondary.color
	preview_livery.accent_colour = preview_accent.color
	preview_preset.select(0)
	preview_render_manager.update_livery_colours(preview_livery)
	_mark_dirty()


func _apply_preview_preset(index: int) -> void:
	if updating_controls:
		return
	match index:
		0:
			return
		1:
			preview_livery.primary_colour = Color(0.1, 0.35, 1.0, 1.0)
			preview_livery.secondary_colour = Color.WHITE
			preview_livery.accent_colour = Color(0.05, 0.05, 0.06, 1.0)
		2:
			preview_livery.primary_colour = Color.WHITE
			preview_livery.secondary_colour = Color(0.7, 0.7, 0.7, 1.0)
			preview_livery.accent_colour = Color(0.15, 0.15, 0.15, 1.0)
		3:
			preview_livery.primary_colour = Color(1.0, 0.08, 0.05, 1.0)
			preview_livery.secondary_colour = Color(0.05, 0.9, 0.15, 1.0)
			preview_livery.accent_colour = Color(0.05, 0.2, 1.0, 1.0)
		4:
			preview_livery.primary_colour = Color(1.0, 0.35, 0.04, 1.0)
			preview_livery.secondary_colour = Color(1.0, 0.85, 0.3, 1.0)
			preview_livery.accent_colour = Color(0.25, 0.03, 0.02, 1.0)
	_refresh_preview_paint_controls()
	preview_render_manager.update_livery_colours(preview_livery)
	_mark_dirty()


func _apply_preview_diagnostic(index: int) -> void:
	preview_render_manager.set_material_diagnostic(index)


func _refresh_stat_options() -> void:
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


func _on_advanced_mode_toggled(enabled: bool) -> void:
	layer_option.visible = enabled
	if !enabled:
		current_layer = "base"
		layer_option.select(0)
	_refresh_stat_options()


func _on_layer_selected(index: int) -> void:
	current_layer = String(layer_option.get_item_metadata(index))
	_refresh_stat_options()


func _on_stat_selected(index: int) -> void:
	current_stat = String(stat_option.get_item_metadata(index))
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	_refresh_samples()


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
	$Workspace/StatsColumn/CurveActions/Apply.disabled = !editable
	$Workspace/StatsColumn/CurveActions/Add.disabled = !editable or current_layer == "s_boost"
	$Workspace/StatsColumn/CurveActions/Remove.disabled = !editable or current_layer == "s_boost"
	$Workspace/StatsColumn/CurveActions/Paste.disabled = !editable or current_layer == "s_boost"
	$Workspace/StatsColumn/CurveActions/Reset.disabled = !editable
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
	stat_help.text = "[b]%s[/b]  [color=#91a8c7]%s · %s · reference %s[/color]\n%s\n[color=#b9c9dc]%s[/color]" % [
		String(schema.get("friendly_name", current_stat)), unit, activity, str(reference),
		String(schema.get("explanation", "")), context,
	]


func _special_derivation_help() -> String:
	if current_layer == "s_boost":
		return "Derived S-BOOST values are absolute snapshots of the base curve at 50% machine setting."
	if current_layer in ["mts", "quickturn", "no_boost"]:
		return "The derived value is an identity multiplier (1.0); make it custom only for a deliberate state-specific trait."
	if current_stat == "drive_target_speed_multiplier":
		return "Derived from acceleration and manual turbo gain across machine setting using the original boost target-speed formula."
	if current_stat == "acceleration_response_multiplier":
		return "Derived from weight: 0.3 at 1000 kg or lighter, otherwise 0.5."
	if current_stat == "forward_thrust_multiplier":
		return "Derived from weight: 1.2 at 1000 kg or lighter, otherwise 1.6."
	if current_stat == "turbo_flat_loss_per_second":
		return "Derived boosted-state multiplier: 0.5 of the base flat turbo loss."
	if current_stat == "turbo_percent_loss_per_second":
		return "Derived boosted-state multiplier: 0.6 of the base percentage turbo loss."
	return "The derived value is an identity multiplier (1.0)."


func _make_selected_special_custom() -> void:
	var result: Dictionary = session.make_special_custom(current_layer, current_stat)
	_show_diagnostics(result)
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	_refresh_history_buttons()


func _revert_selected_special_derived() -> void:
	var result: Dictionary = session.revert_special_derived(current_layer, current_stat)
	_show_diagnostics(result)
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	_refresh_history_buttons()
	_refresh_samples()


func _commit_curve(keys: Array) -> void:
	if current_layer == "s_boost" or _selected_special_is_derived():
		return
	var result: Dictionary = session.set_curve(current_layer, current_stat, keys)
	if bool(result.get("valid", false)):
		session.end_edit_transaction()
	else:
		session.cancel_edit_transaction()
	curve_gesture_active = false
	_show_diagnostics(result)
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	_refresh_history_buttons()
	_refresh_samples()


func _begin_curve_gesture() -> void:
	curve_gesture_active = true
	session.begin_edit_transaction()


func _preview_curve_gesture(keys: Array) -> void:
	if current_layer == "s_boost" or _selected_special_is_derived():
		return
	var result: Dictionary = session.set_curve(current_layer, current_stat, keys)
	if bool(result.get("valid", false)):
		_refresh_samples()


func _cancel_curve_gesture() -> void:
	session.cancel_edit_transaction()
	curve_gesture_active = false
	curve_graph.show_curve(session, current_layer, current_stat)
	_refresh_selected_stat_ui()
	_refresh_history_buttons()
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
	key_time.editable = current_layer != "s_boost" and !_selected_special_is_derived()
	key_tangent_in.editable = current_layer != "s_boost" and !_selected_special_is_derived()
	key_tangent_out.editable = current_layer != "s_boost" and !_selected_special_is_derived()
	key_value.editable = !_selected_special_is_derived()
	updating_controls = false


func _apply_selected_key() -> void:
	if updating_controls:
		return
	if _selected_special_is_derived():
		return
	if current_layer == "s_boost":
		session.set_s_boost_value(current_stat, key_value.value)
		curve_graph.show_curve(session, current_layer, current_stat)
		_refresh_history_buttons()
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
		_refresh_history_buttons()
	else:
		_commit_curve([{"time": 0.0, "value": value, "tangent_in": 0.0, "tangent_out": 0.0}])


func _refresh_samples() -> void:
	machine_value.text = "%.3f" % machine_setting.value
	curve_graph.set_sample_setting(machine_setting.value)
	performance_due_msec = Time.get_ticks_msec() + PERFORMANCE_DEBOUNCE_MSEC
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
	var measurements: Dictionary = session.get_collision_measurements()
	if bool(measurements.get("valid", false)):
		for key in ["front_width", "rear_width", "front_forward_extent", "rear_backward_extent"]:
			vector_controls[key].set_value_no_signal(float(measurements[key]))
	_refresh_material_controls()
	_refresh_thruster_options()
	updating_controls = false


func _refresh_material_controls() -> void:
	var surfaces: Array = session.get_model_surfaces()
	var setup: Dictionary = session.get_material_setup()
	var selected_surfaces: Array = setup.get("body_surfaces", [])
	body_surface_list.clear()
	for surface_value in surfaces:
		var surface: Dictionary = surface_value
		var surface_index := int(surface.get("index", -1))
		body_surface_list.add_item(String(surface.get("name", "Surface %d" % (surface_index + 1))))
		body_surface_list.set_item_metadata(body_surface_list.item_count - 1, surface_index)
		if selected_surfaces.has(surface_index):
			body_surface_list.select(body_surface_list.item_count - 1, false)
	_populate_texture_sources(albedo_surface_option, surfaces, "has_albedo_texture", "Flat white", int(setup.get("albedo_surface", -1)))
	_populate_texture_sources(normal_surface_option, surfaces, "has_normal_texture", "Flat normal", int(setup.get("normal_surface", -1)))
	_populate_texture_sources(paint_mask_surface_option, surfaces, "has_paint_mask_texture", "No paint mask", int(setup.get("paint_mask_surface", -1)))


func _populate_texture_sources(option: OptionButton, surfaces: Array, capability: String, none_label: String, selected_surface: int) -> void:
	option.clear()
	option.add_item(none_label)
	option.set_item_metadata(0, -1)
	option.select(0)
	for surface_value in surfaces:
		var surface: Dictionary = surface_value
		if !bool(surface.get(capability, false)):
			continue
		var surface_index := int(surface.get("index", -1))
		option.add_item(String(surface.get("name", "Surface %d" % (surface_index + 1))))
		option.set_item_metadata(option.item_count - 1, surface_index)
		if surface_index == selected_surface:
			option.select(option.item_count - 1)


func _apply_material_controls() -> void:
	if updating_controls:
		return
	var selected: Array = []
	for item_index in body_surface_list.get_selected_items():
		selected.append(int(body_surface_list.get_item_metadata(item_index)))
	if selected.is_empty():
		_refresh_visual_controls()
		return
	var applied := session.set_material_setup({
		"body_surfaces": selected,
		"albedo_surface": _selected_texture_surface(albedo_surface_option),
		"normal_surface": _selected_texture_surface(normal_surface_option),
		"paint_mask_surface": _selected_texture_surface(paint_mask_surface_option),
	})
	if applied:
		_refresh_history_buttons()
		_refresh_preview()


func _selected_texture_surface(option: OptionButton) -> int:
	return -1 if option.selected < 0 else int(option.get_item_metadata(option.selected))


func _apply_visual_controls() -> void:
	if updating_controls:
		return
	session.begin_edit_transaction()
	session.set_model_transform({
		"translation": _vector_value("translation"),
		"rotation_degrees": _vector_value("rotation"),
		"scale": _vector_value("scale"),
	})
	session.set_collision_measurements({
		"front_width": vector_controls["front_width"].value,
		"rear_width": vector_controls["rear_width"].value,
		"front_forward_extent": vector_controls["front_forward_extent"].value,
		"rear_backward_extent": vector_controls["rear_backward_extent"].value,
	})
	var thrusters: Array = session.get_thrusters()
	if thruster_selector.selected >= 0 and thruster_selector.selected < thrusters.size():
		thrusters[thruster_selector.selected] = {
			"position": _vector_value("thruster_position"),
			"rotation_degrees": _vector_value("thruster_rotation"),
			"scale": vector_controls["thruster_scale"].value,
		}
		session.set_thrusters(thrusters)
	session.end_edit_transaction()
	_refresh_history_buttons()
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
	_refresh_gizmos()


func _add_thruster() -> void:
	var thrusters: Array = session.get_thrusters()
	if thrusters.size() >= 8:
		return
	thrusters.append({"position": Vector3(0.0, 0.0, -1.5), "rotation_degrees": Vector3.ZERO, "scale": 0.5})
	session.set_thrusters(thrusters)
	_refresh_history_buttons()
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
	_refresh_history_buttons()
	_refresh_thruster_options()
	_refresh_preview()


func _refresh_preview() -> void:
	preview_render_manager.clear_renderer()
	preview_definition = null
	var model_path := session.get_model_path()
	if model_path.is_empty() or !FileAccess.file_exists(model_path):
		preview_framed_model_path = ""
		return
	var record := {
		"content_id": "mxt:vehicle:draft:%s" % draft_id,
		"title": title_input.text,
		"authoritative_path": current_properties_path,
		"visual_path": model_path,
		"visual_metadata": {
			"model_transform": session.get_model_transform(),
			"body_surfaces": session.get_material_setup().get("body_surfaces", []),
			"material_inputs": session.get_material_setup(),
			"thrusters": session.get_thrusters(),
		},
	}
	preview_definition = vehicle_content_controller.create_runtime_definition(record)
	if preview_definition != null:
		preview_render_manager.configure_manual([preview_definition], [{"car_livery": preview_livery.to_dict()}])
		preview_render_manager.begin_manual_submit()
		preview_render_manager.submit_manual_car(0, Transform3D.IDENTITY, Color.BLACK, Vector3.ZERO, Color.BLACK, 0.0, false)
		preview_render_manager.set_material_diagnostic(preview_diagnostic.selected)
		_frame_preview_camera(model_path != preview_framed_model_path)
		preview_framed_model_path = model_path
	_refresh_gizmos()


func _frame_preview_camera(reset_view := false) -> void:
	if preview_camera == null or preview_definition == null or preview_definition.runtime_mesh == null:
		return
	var bounds := preview_definition.runtime_mesh.get_aabb()
	if bounds.size.is_zero_approx():
		return
	var model_transform := preview_definition.runtime_transform
	var center := model_transform * bounds.get_center()
	var radius := 0.0
	for x in range(2):
		for y in range(2):
			for z in range(2):
				var corner := bounds.position + Vector3(bounds.size.x * x, bounds.size.y * y, bounds.size.z * z)
				radius = maxf(radius, center.distance_to(model_transform * corner))
	var viewport_aspect := float(preview_viewport.size.x) / maxf(1.0, float(preview_viewport.size.y))
	var half_vertical_fov := deg_to_rad(preview_camera.fov * 0.5)
	var half_horizontal_fov := atan(tan(half_vertical_fov) * viewport_aspect)
	var limiting_fov := minf(half_vertical_fov, half_horizontal_fov)
	var distance := maxf(4.0, radius * 1.15 / maxf(0.1, sin(limiting_fov)))
	preview_camera_controller.configure_frame(center, distance, reset_view)
	preview_camera.far = maxf(1000.0, distance + radius * 4.0)
	preview_camera_controller.apply(preview_camera)


func _refresh_gizmos() -> void:
	if preview_gizmo_root == null:
		return
	preview_gizmo_entries.clear()
	var tab_name := physical_tabs.get_tab_title(physical_tabs.current_tab)
	if tab_name == "Corners":
		var tilt := session.get_tilt_corners()
		var wall := session.get_wall_corners()
		for i in range(4):
			preview_gizmo_entries.append({"kind": "tilt", "index": i, "position": tilt[i], "material": 0})
		for i in range(4):
			preview_gizmo_entries.append({"kind": "wall", "index": i, "position": wall[i], "material": 1})
	elif tab_name == "Thrusters":
		var thrusters: Array = session.get_thrusters()
		for i in range(thrusters.size()):
			preview_gizmo_entries.append({
				"kind": "thruster",
				"index": i,
				"position": Vector3(thrusters[i].get("position", Vector3.ZERO)),
				"material": 2,
			})
	while preview_gizmo_markers.size() < preview_gizmo_entries.size():
		var marker := MeshInstance3D.new()
		marker.mesh = preview_gizmo_sphere
		preview_gizmo_root.add_child(marker)
		preview_gizmo_markers.append(marker)
	for i in range(preview_gizmo_markers.size()):
		var marker := preview_gizmo_markers[i]
		marker.visible = i < preview_gizmo_entries.size()
		if marker.visible:
			var entry: Dictionary = preview_gizmo_entries[i]
			marker.position = entry["position"]
			marker.material_override = preview_gizmo_materials[int(entry["material"])]
	_refresh_gizmo_lines(tab_name)


func _refresh_gizmo_lines(tab_name: String) -> void:
	var lines := ImmediateMesh.new()
	if tab_name == "Corners":
		var tilt := session.get_tilt_corners()
		var wall := session.get_wall_corners()
		lines.surface_begin(Mesh.PRIMITIVE_LINES, preview_gizmo_materials[0])
		for i in range(4):
			lines.surface_add_vertex(tilt[i])
			lines.surface_add_vertex(tilt[(i + 1) % 4])
		lines.surface_end()
		lines.surface_begin(Mesh.PRIMITIVE_LINES, preview_gizmo_materials[1])
		for i in range(4):
			lines.surface_add_vertex(wall[i])
			lines.surface_add_vertex(wall[(i + 1) % 4])
		lines.surface_end()
	elif tab_name == "Thrusters":
		lines.surface_begin(Mesh.PRIMITIVE_LINES, preview_gizmo_materials[2])
		for value in session.get_thrusters():
			var thruster: Dictionary = value
			var position := Vector3(thruster.get("position", Vector3.ZERO))
			var rotation := Vector3(thruster.get("rotation_degrees", Vector3.ZERO))
			var direction := Basis.from_euler(rotation * PI / 180.0) * Vector3(0.0, 0.0, -1.0)
			lines.surface_add_vertex(position)
			lines.surface_add_vertex(position + direction * maxf(0.25, float(thruster.get("scale", 1.0))))
		lines.surface_end()
	preview_gizmo_line.mesh = lines


func _on_preview_gui_input(event: InputEvent) -> void:
	if preview_camera == null:
		return
	var button := event as InputEventMouseButton
	var motion := event as InputEventMouseMotion
	if button == null and motion == null:
		return
	var pointer := (button.position if button != null else motion.position) * Vector2(preview_viewport.size) / preview_container.size
	if button != null:
		if button.button_index == MOUSE_BUTTON_LEFT and button.pressed and !button.double_click:
			dragged_gizmo = _pick_preview_gizmo(pointer)
			if dragged_gizmo >= 0:
				session.begin_edit_transaction()
				var point := Vector3(preview_gizmo_entries[dragged_gizmo]["position"])
				dragged_gizmo_plane = Plane(preview_camera.global_basis.z.normalized(), point)
				preview_container.accept_event()
				return
		if button.button_index == MOUSE_BUTTON_LEFT and !button.pressed and dragged_gizmo >= 0:
			session.end_edit_transaction()
			_refresh_history_buttons()
			_refresh_preview()
			preview_container.accept_event()
			dragged_gizmo = -1
			return
		if preview_camera_controller.handle_mouse_button(button):
			preview_camera_controller.apply(preview_camera)
			preview_container.accept_event()
		return
	if motion == null:
		return
	if dragged_gizmo >= 0:
		var origin := preview_camera.project_ray_origin(pointer)
		var direction := preview_camera.project_ray_normal(pointer)
		var intersection = dragged_gizmo_plane.intersects_ray(origin, direction)
		if intersection != null:
			_set_dragged_gizmo_position(intersection)
			preview_container.accept_event()
		return
	if preview_camera_controller.handle_mouse_motion(motion):
		preview_camera_controller.apply(preview_camera)
		preview_container.accept_event()


func _pick_preview_gizmo(pointer: Vector2) -> int:
	var best := -1
	var best_distance := 18.0
	for i in range(preview_gizmo_entries.size()):
		if String(preview_gizmo_entries[i]["kind"]) != "thruster":
			continue
		var position := Vector3(preview_gizmo_entries[i]["position"])
		if preview_camera.is_position_behind(position):
			continue
		var distance := pointer.distance_to(preview_camera.unproject_position(position))
		if distance < best_distance:
			best_distance = distance
			best = i
	return best


func _set_dragged_gizmo_position(position: Vector3) -> void:
	var entry: Dictionary = preview_gizmo_entries[dragged_gizmo]
	var index := int(entry["index"])
	match String(entry["kind"]):
		"thruster":
			var thrusters: Array = session.get_thrusters()
			var thruster: Dictionary = thrusters[index]
			thruster["position"] = position
			thrusters[index] = thruster
			session.set_thrusters(thrusters)
			if index == thruster_selector.selected:
				_set_vector_value("thruster_position", position)
	preview_gizmo_entries[dragged_gizmo]["position"] = position
	_refresh_gizmos()


func _show_diagnostics(result: Dictionary) -> void:
	var lines: Array[String] = []
	var grouped := {}
	for error in result.get("errors", []):
		var category := _diagnostic_category(String(error))
		if !grouped.has(category): grouped[category] = []
		(grouped[category] as Array).append("[color=#ff6961]ERROR: %s[/color]" % error)
	for warning in result.get("warnings", []):
		var category := _diagnostic_category(String(warning))
		if !grouped.has(category): grouped[category] = []
		(grouped[category] as Array).append("[color=#ffd166]WARNING: %s[/color]" % warning)
	for category in ["Metadata", "Model", "Materials", "Geometry", "Physics", "Publishing"]:
		if grouped.has(category):
			lines.append("[url=%s][b]%s[/b][/url]" % [category, category])
			for message in grouped[category]:
				lines.append(String(message))
	if lines.is_empty() and bool(result.get("valid", false)):
		lines.append("[color=#73e2a7]Ready[/color]")
	diagnostics.text = "\n".join(lines)


func _diagnostic_category(message: String) -> String:
	var lower := message.to_lower()
	if "title" in lower or "author" in lower or "description" in lower:
		return "Metadata"
	if "material" in lower or "surface" in lower or "texture" in lower or "paint" in lower:
		return "Materials"
	if "corner" in lower or "geometry" in lower or "thruster" in lower:
		return "Geometry"
	if "model" in lower or "mesh" in lower or "gltf" in lower or "glb" in lower:
		return "Model"
	if "preview" in lower or "workshop" in lower or "publish" in lower or "package" in lower:
		return "Publishing"
	return "Physics"


func _focus_diagnostic_category(category_value) -> void:
	var category := String(category_value)
	match category:
		"Metadata": title_input.grab_focus()
		"Model": import_model_dialog.popup_centered()
		"Materials": _select_physical_tab("Materials")
		"Geometry": _select_physical_tab("Corners")
		"Publishing": workshop_visibility.grab_focus()
		_: stat_option.grab_focus()


func _select_physical_tab(title: String) -> void:
	for index in range(physical_tabs.get_tab_count()):
		if physical_tabs.get_tab_title(index) == title:
			physical_tabs.current_tab = index
			return


func _mark_dirty() -> void:
	if !updating_controls:
		metadata_dirty = true
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_DEBOUNCE_MSEC
		_update_autosave_status("Unsaved changes")
		diagnostics.text = "[color=#ffd166]Unsaved draft changes[/color]"
