class_name VehicleEditor extends VBoxContainer

signal content_changed
signal test_drive_requested(snapshot: MxtContentLoadResult)

const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const DRAFTS_ROOT := "user://vehicle_drafts"
const LOCAL_LIBRARY_ROOT := "user://content/packages"
const WORKSHOP_STAGING_ROOT := "user://content/workshop_staging"
const WORKSHOP_PREVIEW_TARGET_MAX_BYTES := 950_000
const WORKSHOP_PREVIEW_MIN_LONGEST_EDGE := 128
const AUTOSAVE_DEBOUNCE_MSEC := 900
const AUTOSAVE_RETRY_MSEC := 5000
const MATERIAL_TEXTURE_MAX_BYTES := 20 * 1024 * 1024
const MATERIAL_TEXTURE_MAX_DIMENSION := 2048
const MATERIAL_TEXTURE_FILES := {
	"albedo": "albedo.png",
	"normal": "normal.png",
	"paint_mask": "paint_mask.png",
}

@onready var draft_option: OptionButton = $Toolbar/DraftOption
@onready var title_input: LineEdit = $Metadata/Title
@onready var author_input: LineEdit = $Metadata/Author
@onready var description_input: TextEdit = $Metadata/Description
@onready var visual_status: Label = $Workspace/VisualColumn/VisualStatus
@onready var physical_tabs: TabContainer = $Workspace/VisualColumn/PhysicalTabs
@onready var transform_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Transform/Rows
@onready var body_surface_list: ItemList = $Workspace/VisualColumn/PhysicalTabs/Materials/BodySurfaces
@onready var albedo_surface_option: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Materials/AlbedoRow/Source
@onready var normal_surface_option: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Materials/NormalRow/Source
@onready var paint_mask_surface_option: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Materials/PaintMaskRow/Source
@onready var corner_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Corners/Rows
@onready var thruster_selector: OptionButton = $Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Selector
@onready var thruster_rows: VBoxContainer = $Workspace/VisualColumn/PhysicalTabs/Thrusters/Rows
@onready var boost_volume_slider: HSlider = $Workspace/VisualColumn/PhysicalTabs/Audio/BoostVolumeRow/Slider
@onready var boost_volume_value: Label = $Workspace/VisualColumn/PhysicalTabs/Audio/BoostVolumeRow/Value
@onready var diagnostics: RichTextLabel = $Workspace/StatsColumn/Diagnostics
@onready var import_model_dialog: FileDialog = $ImportModelDialog
@onready var import_boost_sound_dialog: FileDialog = $ImportBoostSoundDialog
@onready var import_material_texture_dialog: FileDialog = $ImportMaterialTextureDialog
@onready var export_package_dialog: FileDialog = $ExportPackageDialog
@onready var template_vehicle_dialog: ConfirmationDialog = $TemplateVehicleDialog
@onready var template_vehicle_option: OptionButton = $TemplateVehicleDialog/Rows/VehicleOption
@onready var official_vehicle_dialog: ConfirmationDialog = $OfficialVehicleDialog
@onready var official_vehicle_option: OptionButton = $OfficialVehicleDialog/Rows/VehicleOption
@onready var workshop_visibility: OptionButton = $Workshop/Visibility
@onready var workshop_status: Label = $Workshop/Status
@onready var workshop_page_button: Button = $Workshop/OpenPage
@onready var workshop_publish_button: Button = $Toolbar/PublishWorkshop
@onready var autosave_status: Label = $Toolbar/AutosaveStatus
@onready var archive_draft_dialog: ConfirmationDialog = $ArchiveDraftDialog
@onready var delete_draft_dialog: ConfirmationDialog = $DeleteDraftDialog
@onready var workshop_capture_button: Button = $Workshop/CapturePreview
@onready var publish_review_dialog: ConfirmationDialog = $PublishReviewDialog
@onready var publish_review_summary: RichTextLabel = $PublishReviewDialog/Review/Summary
@onready var publish_review_preview: TextureRect = $PublishReviewDialog/Review/Preview
@onready var publish_changelog: TextEdit = $PublishReviewDialog/Review/Changelog
@onready var preview_controller: VehicleEditorPreviewController = $PreviewController
@onready var curve_controller: VehicleEditorCurveController = $CurveController

var game_manager: GameManager
var vehicle_content_controller: VehicleContentControllerClass
var session := MxtCarAuthoringSession.new()
var draft_store := MxtCarDraftStore.new()
var draft_id := ""
var current_properties_path := ""
var editing_official_definition: CarDefinition
var metadata_dirty := false
var draft_initialized := false
var autosave_due_msec := 0
var autosave_error := ""
var pending_material_texture := ""
var workshop_request_id := 0
var workshop_operation := ""
var workshop_published_file_id := 0
var workshop_pending_package: Dictionary = {}
var workshop_progress_update_msec := 0
var vector_controls: Dictionary = {}
var updating_controls := false
var workshop_preview_captured := false


func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	if game_manager != null:
		vehicle_content_controller = game_manager.get_node("VehicleContentController") as VehicleContentControllerClass
	curve_controller.initialize(self, session)
	curve_controller.diagnostics_requested.connect(_show_diagnostics)
	curve_controller.history_changed.connect(_refresh_history_buttons)
	_setup_options()
	_setup_vector_controls()
	preview_controller.initialize(self, session)
	preview_controller.authoring_edit_committed.connect(_refresh_history_buttons)
	preview_controller.preview_livery_changed.connect(_mark_dirty)
	preview_controller.thruster_position_changed.connect(_on_preview_thruster_position_changed)
	_connect_controls()
	_refresh_draft_options()
	if draft_option.item_count > 0:
		_open_selected_draft()
	else:
		_new_draft()
	call_deferred("_connect_steam_service")


func _setup_options() -> void:
	for visibility in ["Public", "Friends Only", "Private", "Unlisted"]:
		workshop_visibility.add_item(visibility)
	workshop_visibility.selected = 1


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


func _connect_controls() -> void:
	visibility_changed.connect(_on_visibility_changed)
	$Toolbar/NewDraft.pressed.connect(_new_draft)
	$Toolbar/OpenDraft.pressed.connect(_open_selected_draft)
	$Toolbar/DuplicateDraft.pressed.connect(_duplicate_current_draft)
	$Toolbar/ArchiveDraft.pressed.connect(func(): archive_draft_dialog.popup_centered())
	$Toolbar/ImportTemplate.pressed.connect(_open_template_vehicle_dialog)
	$Toolbar/EditOfficial.pressed.connect(_open_official_vehicle_dialog)
	$Toolbar/ImportModel.pressed.connect(func(): import_model_dialog.popup_centered())
	$Toolbar/ImportBoostSound.pressed.connect(func(): import_boost_sound_dialog.popup_centered())
	$Toolbar/ClearBoostSound.pressed.connect(_clear_boost_sound)
	$Toolbar/SaveDraft.pressed.connect(_manual_save_draft)
	$Toolbar/InstallVehicle.pressed.connect(_install_vehicle)
	$Toolbar/ExportPackage.pressed.connect(func(): export_package_dialog.popup_centered())
	$Toolbar/TestDrive.pressed.connect(_test_drive)
	$Toolbar/PublishWorkshop.pressed.connect(_publish_workshop)
	$Toolbar/DeleteDraft.pressed.connect(_prompt_delete_current_draft)
	workshop_page_button.pressed.connect(_open_workshop_page)
	$Toolbar/Undo.pressed.connect(_undo)
	$Toolbar/Redo.pressed.connect(_redo)
	import_model_dialog.file_selected.connect(_import_model)
	import_boost_sound_dialog.file_selected.connect(_import_boost_sound)
	import_material_texture_dialog.file_selected.connect(_import_material_texture)
	export_package_dialog.file_selected.connect(_export_package)
	template_vehicle_dialog.confirmed.connect(_import_selected_vehicle_template)
	official_vehicle_dialog.confirmed.connect(_open_selected_official_vehicle)
	archive_draft_dialog.confirmed.connect(_archive_current_draft)
	delete_draft_dialog.confirmed.connect(_delete_current_draft)
	workshop_capture_button.pressed.connect(_capture_workshop_preview)
	publish_review_dialog.confirmed.connect(_confirm_publish_workshop)
	diagnostics.meta_clicked.connect(_focus_diagnostic_category)
	$Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Add.pressed.connect(_add_thruster)
	$Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Remove.pressed.connect(_remove_thruster)
	thruster_selector.item_selected.connect(func(_index): _refresh_thruster_controls())
	boost_volume_slider.drag_started.connect(_begin_boost_volume_edit)
	boost_volume_slider.drag_ended.connect(_end_boost_volume_edit)
	boost_volume_slider.value_changed.connect(_set_boost_volume)
	body_surface_list.multi_selected.connect(func(_index, _selected): _apply_material_controls())
	albedo_surface_option.item_selected.connect(func(_index): _apply_material_controls())
	normal_surface_option.item_selected.connect(func(_index): _apply_material_controls())
	paint_mask_surface_option.item_selected.connect(func(_index): _apply_material_controls())
	$Workspace/VisualColumn/PhysicalTabs/Materials/AlbedoRow/Import.pressed.connect(func(): _open_material_texture_dialog("albedo"))
	$Workspace/VisualColumn/PhysicalTabs/Materials/AlbedoRow/Clear.pressed.connect(func(): _clear_material_texture("albedo"))
	$Workspace/VisualColumn/PhysicalTabs/Materials/NormalRow/Import.pressed.connect(func(): _open_material_texture_dialog("normal"))
	$Workspace/VisualColumn/PhysicalTabs/Materials/NormalRow/Clear.pressed.connect(func(): _clear_material_texture("normal"))
	$Workspace/VisualColumn/PhysicalTabs/Materials/PaintMaskRow/Import.pressed.connect(func(): _open_material_texture_dialog("paint_mask"))
	$Workspace/VisualColumn/PhysicalTabs/Materials/PaintMaskRow/Clear.pressed.connect(func(): _clear_material_texture("paint_mask"))
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
	if _has_editable_document() and !_flush_autosave():
		return false
	editing_official_definition = null
	draft_id = "draft_%d_%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_msec() % 1000000]
	session = MxtCarAuthoringSession.new()
	draft_initialized = true
	current_properties_path = ""
	preview_controller.reset_livery()
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
	_refresh_document_mode_controls()
	_refresh_all()
	visual_status.text = "New draft. Import a static GLB or glTF vehicle model."
	if !_autosave_draft():
		return false
	return true


func _draft_root() -> String:
	return "%s/%s" % [DRAFTS_ROOT, draft_id]


func _boost_sound_path() -> String:
	if !draft_initialized:
		return ""
	for extension in ["wav", "ogg"]:
		var path := "%s/manual_boost_sfx.%s" % [_draft_root(), extension]
		if FileAccess.file_exists(path):
			return path
	return ""


func _refresh_boost_sound_controls() -> void:
	var sound_path := _boost_sound_path()
	$Toolbar/ImportBoostSound.disabled = !draft_initialized
	$Toolbar/ImportBoostSound.text = "Replace Boost Sound" if !sound_path.is_empty() else "Add Boost Sound"
	$Toolbar/ClearBoostSound.disabled = sound_path.is_empty()


func _import_boost_sound(source_path: String) -> void:
	if !draft_initialized:
		return
	var extension := source_path.get_extension().to_lower()
	if extension != "wav" and extension != "ogg":
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["Boost sounds must be WAV or Ogg Vorbis files"]), "warnings": PackedStringArray()})
		return
	var source := FileAccess.open(source_path, FileAccess.READ)
	if source == null or source.get_length() <= 0 or source.get_length() > 8 * 1024 * 1024:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["Boost sounds must be readable and no larger than 8 MiB"]), "warnings": PackedStringArray()})
		return
	source.close()
	var stream: AudioStream = AudioStreamWAV.load_from_file(source_path) if extension == "wav" else AudioStreamOggVorbis.load_from_file(source_path)
	if stream == null:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["The selected boost sound could not be decoded"]), "warnings": PackedStringArray()})
		return
	var draft_path := ProjectSettings.globalize_path(_draft_root())
	if DirAccess.make_dir_recursive_absolute(draft_path) != OK:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["The vehicle draft directory could not be created"]), "warnings": PackedStringArray()})
		return
	var destination := "%s/manual_boost_sfx.%s" % [draft_path, extension]
	if DirAccess.copy_absolute(source_path, destination) != OK:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["The boost sound could not be copied into the draft"]), "warnings": PackedStringArray()})
		return
	var other_extension := "ogg" if extension == "wav" else "wav"
	DirAccess.remove_absolute("%s/manual_boost_sfx.%s" % [draft_path, other_extension])
	_refresh_boost_sound_controls()
	_update_autosave_status("Custom boost sound ready")


func _clear_boost_sound() -> void:
	if !draft_initialized:
		return
	var draft_path := ProjectSettings.globalize_path(_draft_root())
	DirAccess.remove_absolute(draft_path.path_join("manual_boost_sfx.wav"))
	DirAccess.remove_absolute(draft_path.path_join("manual_boost_sfx.ogg"))
	_refresh_boost_sound_controls()
	_update_autosave_status("Custom boost sound cleared")


func _material_texture_path(layer: String) -> String:
	if !draft_initialized or !MATERIAL_TEXTURE_FILES.has(layer):
		return ""
	return _draft_root().path_join(String(MATERIAL_TEXTURE_FILES[layer]))


func _material_texture_row(layer: String) -> HBoxContainer:
	match layer:
		"albedo":
			return $Workspace/VisualColumn/PhysicalTabs/Materials/AlbedoRow
		"normal":
			return $Workspace/VisualColumn/PhysicalTabs/Materials/NormalRow
		"paint_mask":
			return $Workspace/VisualColumn/PhysicalTabs/Materials/PaintMaskRow
	return null


func _refresh_material_texture_controls() -> void:
	for layer in MATERIAL_TEXTURE_FILES:
		var row := _material_texture_row(layer)
		if row == null:
			continue
		var path := _material_texture_path(layer)
		var has_override := !path.is_empty() and FileAccess.file_exists(path)
		var editable := draft_initialized and editing_official_definition == null
		var source := row.get_node("Source") as OptionButton
		var import_button := row.get_node("Import") as Button
		var clear_button := row.get_node("Clear") as Button
		source.disabled = !editable or has_override
		import_button.disabled = !editable
		import_button.text = "Replace PNG" if has_override else "Import PNG"
		clear_button.disabled = !editable or !has_override
		var description := "Standalone %s PNG overrides the embedded GLB source." % String(layer).replace("_", " ").capitalize() if has_override else "Uses the selected embedded GLB source."
		source.tooltip_text = description
		import_button.tooltip_text = description
		clear_button.tooltip_text = description


func _open_material_texture_dialog(layer: String) -> void:
	if !draft_initialized or !MATERIAL_TEXTURE_FILES.has(layer):
		return
	pending_material_texture = layer
	import_material_texture_dialog.title = "Import %s PNG" % layer.replace("_", " ").capitalize()
	import_material_texture_dialog.popup_centered()


func _import_material_texture(source_path: String) -> void:
	var layer := pending_material_texture
	pending_material_texture = ""
	if !draft_initialized or !MATERIAL_TEXTURE_FILES.has(layer):
		return
	if source_path.get_extension().to_lower() != "png":
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["Vehicle material textures must be PNG files"]), "warnings": PackedStringArray()})
		return
	var source := FileAccess.open(source_path, FileAccess.READ)
	if source == null or source.get_length() <= 0 or source.get_length() > MATERIAL_TEXTURE_MAX_BYTES:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["Vehicle material PNGs must be readable and no larger than 20 MiB"]), "warnings": PackedStringArray()})
		return
	source.close()
	var image := Image.load_from_file(source_path)
	if image == null or image.is_empty():
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["The selected vehicle texture is not a decodable PNG"]), "warnings": PackedStringArray()})
		return
	if image.get_width() > MATERIAL_TEXTURE_MAX_DIMENSION or image.get_height() > MATERIAL_TEXTURE_MAX_DIMENSION:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["Vehicle material PNG dimensions cannot exceed 2048 x 2048"]), "warnings": PackedStringArray()})
		return
	var draft_path := ProjectSettings.globalize_path(_draft_root())
	if DirAccess.make_dir_recursive_absolute(draft_path) != OK:
		_show_diagnostics({"valid": false, "errors": PackedStringArray(["The vehicle draft directory could not be created"]), "warnings": PackedStringArray()})
		return
	var destination := draft_path.path_join(String(MATERIAL_TEXTURE_FILES[layer]))
	var normalized_source := source_path.replace("\\", "/").simplify_path()
	var normalized_destination := destination.replace("\\", "/").simplify_path()
	if normalized_source != normalized_destination:
		var temporary := destination + ".importing"
		var previous := destination + ".previous"
		DirAccess.remove_absolute(temporary)
		DirAccess.remove_absolute(previous)
		if DirAccess.copy_absolute(source_path, temporary) != OK:
			_show_diagnostics({"valid": false, "errors": PackedStringArray(["The vehicle texture could not be copied into the draft"]), "warnings": PackedStringArray()})
			return
		if FileAccess.file_exists(destination) and DirAccess.rename_absolute(destination, previous) != OK:
			DirAccess.remove_absolute(temporary)
			_show_diagnostics({"valid": false, "errors": PackedStringArray(["The previous draft texture could not be replaced"]), "warnings": PackedStringArray()})
			return
		if DirAccess.rename_absolute(temporary, destination) != OK:
			if FileAccess.file_exists(previous):
				DirAccess.rename_absolute(previous, destination)
			_show_diagnostics({"valid": false, "errors": PackedStringArray(["The imported vehicle texture could not be installed"]), "warnings": PackedStringArray()})
			return
		DirAccess.remove_absolute(previous)
	if layer == "normal" and !session.get_model_path().is_empty():
		var setup: Dictionary = session.get_material_setup()
		setup["use_mesh_normals"] = false
		session.set_material_setup(setup)
	_refresh_material_texture_controls()
	preview_controller.refresh()
	_refresh_history_buttons()
	_update_autosave_status("Custom %s texture ready" % layer.replace("_", " "))


func _clear_material_texture(layer: String) -> void:
	var path := _material_texture_path(layer)
	if path.is_empty():
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	_refresh_material_texture_controls()
	preview_controller.refresh()
	_update_autosave_status("Custom %s texture cleared" % layer.replace("_", " "))


func _has_editable_document() -> bool:
	return draft_initialized or editing_official_definition != null


func _refresh_document_mode_controls() -> void:
	var editing_official := editing_official_definition != null
	title_input.editable = !editing_official
	author_input.editable = !editing_official
	description_input.editable = !editing_official
	$Toolbar/DuplicateDraft.disabled = editing_official
	$Toolbar/ArchiveDraft.disabled = editing_official
	$Toolbar/DeleteDraft.disabled = editing_official or !draft_initialized
	$Toolbar/ImportModel.disabled = editing_official
	$Toolbar/InstallVehicle.disabled = editing_official
	$Toolbar/ExportPackage.disabled = editing_official
	$Toolbar/TestDrive.disabled = editing_official
	$Toolbar/PublishWorkshop.disabled = editing_official
	$Toolbar/SaveDraft.text = "Save Official" if editing_official else "Save Now"
	$Workshop.visible = !editing_official
	_refresh_boost_sound_controls()
	preview_controller.set_editing_official(editing_official)
	for tab in range(physical_tabs.get_tab_count()):
		var title := physical_tabs.get_tab_title(tab)
		physical_tabs.set_tab_disabled(
			tab, editing_official and title in ["Transform", "Materials", "Thrusters", "Audio"])
	boost_volume_slider.editable = !editing_official
	if editing_official and physical_tabs.is_tab_disabled(physical_tabs.current_tab):
		_select_physical_tab("Corners")


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
	if _has_editable_document() and !_flush_autosave():
		return
	var candidate := MxtCarAuthoringSession.new()
	var result: Dictionary = draft_store.load_draft(selected_id, candidate)
	if !bool(result.get("valid", false)):
		_show_diagnostics(result)
		return
	session = candidate
	editing_official_definition = null
	draft_id = selected_id
	draft_initialized = true
	current_properties_path = String(result.get("properties_path", ""))
	preview_controller.load_livery(result.get("preview_livery", {}))
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
	_refresh_document_mode_controls()
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


func _prompt_delete_current_draft() -> void:
	if !draft_initialized or editing_official_definition != null:
		return
	delete_draft_dialog.dialog_text = (
		"Permanently delete “%s” and all of its local authoring files?\n\nThis cannot be undone."
		% title_input.text)
	delete_draft_dialog.popup_centered()


func _delete_current_draft() -> void:
	if !draft_initialized or editing_official_definition != null:
		return
	curve_controller.cancel_active_edit()
	var result: Dictionary = draft_store.delete_draft(draft_id)
	_show_diagnostics(result)
	if !bool(result.get("valid", false)):
		return
	draft_initialized = false
	draft_id = ""
	current_properties_path = ""
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0
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


func _open_official_vehicle_dialog() -> void:
	official_vehicle_option.clear()
	if vehicle_content_controller == null:
		return
	for definition_value in vehicle_content_controller.definitions:
		var definition := definition_value as CarDefinition
		if definition == null or !definition.content_id.begins_with("mxt:vehicle:official:") \
				or definition.properties_path.is_empty():
			continue
		official_vehicle_option.add_item(definition.name)
		official_vehicle_option.set_item_metadata(
			official_vehicle_option.item_count - 1, definition.content_id)
	if official_vehicle_option.item_count == 0:
		_show_diagnostics({
			"valid": false,
			"errors": PackedStringArray(["No shipped vehicle property files are available"]),
			"warnings": PackedStringArray(),
		})
		return
	official_vehicle_option.select(0)
	official_vehicle_dialog.popup_centered()


func _open_selected_official_vehicle() -> void:
	if vehicle_content_controller == null or official_vehicle_option.selected < 0:
		return
	var content_id := String(official_vehicle_option.get_item_metadata(official_vehicle_option.selected))
	var definition := vehicle_content_controller.definitions_by_content_id.get(content_id) as CarDefinition
	if definition == null or !definition.content_id.begins_with("mxt:vehicle:official:"):
		_show_diagnostics({
			"valid": false,
			"errors": PackedStringArray(["The selected official vehicle is no longer available"]),
			"warnings": PackedStringArray(),
		})
		return
	if _has_editable_document() and !_flush_autosave():
		return
	var candidate := MxtCarAuthoringSession.new()
	var result: Dictionary = candidate.load_file(definition.properties_path)
	if !bool(result.get("valid", false)):
		_show_diagnostics(result)
		return
	session = candidate
	editing_official_definition = definition
	draft_initialized = false
	draft_id = ""
	current_properties_path = definition.properties_path
	preview_controller.reset_livery()
	updating_controls = true
	title_input.text = definition.name
	author_input.text = "Shipped Asset"
	description_input.text = definition.properties_path
	updating_controls = false
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0
	workshop_published_file_id = 0
	workshop_preview_captured = false
	_refresh_workshop_controls()
	_refresh_document_mode_controls()
	_refresh_all()
	_update_autosave_status("Editing official %s" % definition.name)


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
		var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(definition.content_id)
		visual_result = _copy_packaged_vehicle_visual(record)
	if !bool(visual_result.get("valid", false)):
		return visual_result
	if !session.set_manual_boost_volume_db(definition.manual_boost_volume_db):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid boost-volume metadata"]), "warnings": PackedStringArray()}
	return session.validate()


func _copy_packaged_vehicle_visual(record: MxtContentRecord) -> Dictionary:
	var source_path := record.visual_path if record != null else ""
	if source_path.is_empty() or !FileAccess.file_exists(source_path):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle package has no readable model"]), "warnings": PackedStringArray()}
	var imported: Dictionary = session.import_model(source_path, _draft_root())
	if !bool(imported.get("valid", false)):
		return imported
	var visual_metadata: Dictionary = record.visual_metadata
	if !session.set_model_transform(visual_metadata.get("model_transform", {})):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid model-transform metadata"]), "warnings": PackedStringArray()}
	var material_setup: Dictionary = visual_metadata.get("material_inputs", {}).duplicate(true)
	material_setup["body_surfaces"] = visual_metadata.get("body_surfaces", [])
	if !session.set_material_setup(material_setup):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid material metadata"]), "warnings": PackedStringArray()}
	if !session.set_thrusters(visual_metadata.get("thrusters", [])):
		return {"valid": false, "errors": PackedStringArray(["The selected vehicle has invalid thruster metadata"]), "warnings": PackedStringArray()}
	var intent_result: Dictionary = session.set_authoring_intent(record.authoring_metadata)
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
	if editing_official_definition != null:
		workshop_publish_button.disabled = true
		workshop_page_button.disabled = true
		workshop_capture_button.disabled = true
		workshop_status.text = "Workshop publishing is unavailable while editing shipped assets"
		return
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
		for schema_value in curve_controller.stat_schema:
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
	if _has_editable_document() and !curve_controller.gesture_active and (metadata_dirty or session.is_dirty()):
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
	if _has_editable_document():
		_flush_autosave()


func _on_visibility_changed() -> void:
	if _has_editable_document() and !is_visible_in_tree():
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
		preview_controller.framed_model_path = ""
		_refresh_visual_controls()
		preview_controller.refresh()
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
		"preview_livery": preview_controller.livery_dict(),
	}


func _autosave_draft() -> bool:
	if editing_official_definition != null:
		return _save_official_properties()
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


func _save_official_properties() -> bool:
	if editing_official_definition == null:
		return false
	if current_properties_path != editing_official_definition.properties_path \
			or !current_properties_path.begins_with("res://vehicle/asset/"):
		autosave_error = "Official vehicle save target is invalid"
		_update_autosave_status(autosave_error, true)
		return false
	var result: Dictionary = session.save_file(current_properties_path)
	if !bool(result.get("valid", false)):
		var errors: PackedStringArray = result.get(
			"errors", PackedStringArray(["Unknown official vehicle save error"]))
		autosave_error = "Unknown official vehicle save error" if errors.is_empty() else String(errors[0])
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_RETRY_MSEC
		_update_autosave_status("Official save failed: %s" % autosave_error, true)
		_show_diagnostics(result)
		return false
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0
	curve_controller.reset_performance_analysis()
	content_changed.emit()
	_update_autosave_status("Saved official %s" % editing_official_definition.name)
	return true


func _flush_autosave() -> bool:
	curve_controller.cancel_active_edit()
	if !_has_editable_document():
		return true
	if metadata_dirty or session.is_dirty() or !autosave_error.is_empty():
		return _autosave_draft()
	return true


func _manual_save_draft() -> void:
	if editing_official_definition != null:
		_save_official_properties()
		return
	if !_flush_autosave():
		return
	var thumbnail_path := _draft_root() + "/thumbnail.png"
	if _save_preview_png(thumbnail_path):
		_refresh_draft_options()
		_update_autosave_status("Saved with thumbnail")
	else:
		_update_autosave_status("Saved; previous thumbnail kept")


func _build_package(preview_override := "", validate_package := true) -> Dictionary:
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
		author_input.text,
		validate_package)
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
	var preview_image := preview_controller.preview_viewport.get_texture().get_image()
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
	var built := _build_package("", false)
	if !bool(built.get("valid", false)):
		return
	var snapshot: MxtContentLoadResult = vehicle_content_controller.create_test_drive_snapshot(
		String(built.get("package_path", "")))
	_show_content_diagnostics(snapshot)
	if !snapshot.is_valid():
		return
	test_drive_requested.emit(snapshot)


func _refresh_all() -> void:
	_refresh_visual_controls()
	_refresh_boost_volume_controls()
	preview_controller.refresh_paint_controls()
	_refresh_resource_usage()
	_sync_preview_context()
	preview_controller.refresh()
	curve_controller.refresh_all()
	_show_diagnostics(session.validate())
	_refresh_history_buttons()


func _refresh_boost_volume_controls() -> void:
	var volume_db := session.get_manual_boost_volume_db()
	boost_volume_slider.set_value_no_signal(volume_db)
	boost_volume_value.text = "%+.1f dB" % volume_db


func _begin_boost_volume_edit() -> void:
	if updating_controls:
		return
	session.begin_edit_transaction()


func _end_boost_volume_edit(_value_changed: bool) -> void:
	if updating_controls:
		return
	session.end_edit_transaction()
	_refresh_history_buttons()


func _set_boost_volume(value: float) -> void:
	if updating_controls:
		return
	if session.set_manual_boost_volume_db(value):
		boost_volume_value.text = "%+.1f dB" % value
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
	if editing_official_definition != null:
		visual_status.text = "Editing shipped properties: %s" % current_properties_path
		return
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
	_populate_normal_sources(surfaces, int(setup.get("normal_surface", -1)), bool(setup.get("use_mesh_normals", false)))
	_populate_texture_sources(paint_mask_surface_option, surfaces, "has_paint_mask_texture", "No paint mask", int(setup.get("paint_mask_surface", -1)))
	_refresh_material_texture_controls()


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


func _populate_normal_sources(surfaces: Array, selected_surface: int, mesh_normals: bool) -> void:
	normal_surface_option.clear()
	normal_surface_option.add_item("Flat normal")
	normal_surface_option.set_item_metadata(0, -1)
	normal_surface_option.add_item("Mesh normals")
	normal_surface_option.set_item_metadata(1, -2)
	normal_surface_option.select(1 if mesh_normals else 0)
	for surface_value in surfaces:
		var surface: Dictionary = surface_value
		if !bool(surface.get("has_normal_texture", false)):
			continue
		var surface_index := int(surface.get("index", -1))
		normal_surface_option.add_item(String(surface.get("name", "Surface %d" % (surface_index + 1))))
		normal_surface_option.set_item_metadata(normal_surface_option.item_count - 1, surface_index)
		if !mesh_normals and surface_index == selected_surface:
			normal_surface_option.select(normal_surface_option.item_count - 1)


func _apply_material_controls() -> void:
	if updating_controls:
		return
	var selected: Array = []
	for item_index in body_surface_list.get_selected_items():
		selected.append(int(body_surface_list.get_item_metadata(item_index)))
	if selected.is_empty():
		_refresh_visual_controls()
		return
	var normal_selection := _selected_texture_surface(normal_surface_option)
	var applied := session.set_material_setup({
		"body_surfaces": selected,
		"albedo_surface": _selected_texture_surface(albedo_surface_option),
		"normal_surface": -1 if normal_selection == -2 else normal_selection,
		"paint_mask_surface": _selected_texture_surface(paint_mask_surface_option),
		"use_mesh_normals": normal_selection == -2,
	})
	if applied:
		_refresh_history_buttons()
		preview_controller.refresh()


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
	preview_controller.refresh()


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
	preview_controller.refresh_gizmos()


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
	preview_controller.refresh()


func _remove_thruster() -> void:
	var thrusters: Array = session.get_thrusters()
	if thruster_selector.selected < 0 or thruster_selector.selected >= thrusters.size():
		return
	thrusters.remove_at(thruster_selector.selected)
	session.set_thrusters(thrusters)
	_refresh_history_buttons()
	_refresh_thruster_options()
	preview_controller.refresh()


func _sync_preview_context() -> void:
	preview_controller.set_document_context(
		draft_id,
		current_properties_path,
		_draft_root() if !draft_id.is_empty() else "",
		editing_official_definition)

func _on_preview_thruster_position_changed(_index: int, position: Vector3) -> void:
	_set_vector_value("thruster_position", position)

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


func _show_content_diagnostics(result: MxtContentLoadResult) -> void:
	var lines: Array[String] = []
	for error in result.errors:
		lines.append("[color=#ff6961]ERROR: %s[/color]" % error)
	if lines.is_empty() and result.is_valid():
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
		_: curve_controller.focus_stat_selector()


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
		diagnostics.text = "[color=#ffd166]%s[/color]" % (
			"Unsaved official vehicle changes"
			if editing_official_definition != null
			else "Unsaved draft changes")
