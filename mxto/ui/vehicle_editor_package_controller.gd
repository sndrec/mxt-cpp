class_name VehicleEditorPackageController
extends Node

signal diagnostics_requested(result: Dictionary)
signal content_diagnostics_requested(result: MxtContentLoadResult)
signal content_changed
signal test_drive_requested(snapshot: MxtContentLoadResult)
signal draft_list_changed

const LOCAL_LIBRARY_ROOT := "user://content/packages"
const WORKSHOP_STAGING_ROOT := "user://content/workshop_staging"
const WORKSHOP_PREVIEW_TARGET_MAX_BYTES := 950_000
const WORKSHOP_PREVIEW_MIN_LONGEST_EDGE := 128

var game_manager: GameManager
var vehicle_content_controller: VehicleContentController
var document_controller: VehicleEditorDocumentController
var preview_controller: VehicleEditorPreviewController
var curve_controller: VehicleEditorCurveController
var title_input: LineEdit
var author_input: LineEdit
var description_input: TextEdit
var workshop_visibility: OptionButton
var workshop_status: Label
var workshop_page_button: Button
var workshop_publish_button: Button
var workshop_capture_button: Button
var publish_review_dialog: ConfirmationDialog
var publish_review_summary: RichTextLabel
var publish_review_preview: TextureRect
var publish_changelog: TextEdit
var export_package_dialog: FileDialog
var workshop_request_id := 0
var workshop_operation := ""
var workshop_published_file_id := 0
var workshop_pending_package: Dictionary = {}
var workshop_progress_update_msec := 0
var workshop_preview_captured := false

func initialize(owner_ui: Control, manager: GameManager, vehicle_controller: VehicleContentController, document: VehicleEditorDocumentController, preview: VehicleEditorPreviewController, curve: VehicleEditorCurveController) -> void:
	game_manager = manager
	vehicle_content_controller = vehicle_controller
	document_controller = document
	preview_controller = preview
	curve_controller = curve
	title_input = owner_ui.get_node("Metadata/Title")
	author_input = owner_ui.get_node("Metadata/Author")
	description_input = owner_ui.get_node("Metadata/Description")
	workshop_visibility = owner_ui.get_node("Workshop/Visibility")
	workshop_status = owner_ui.get_node("Workshop/Status")
	workshop_page_button = owner_ui.get_node("Workshop/OpenPage")
	workshop_publish_button = owner_ui.get_node("Toolbar/PublishWorkshop")
	workshop_capture_button = owner_ui.get_node("Workshop/CapturePreview")
	publish_review_dialog = owner_ui.get_node("PublishReviewDialog")
	publish_review_summary = owner_ui.get_node("PublishReviewDialog/Review/Summary")
	publish_review_preview = owner_ui.get_node("PublishReviewDialog/Review/Preview")
	publish_changelog = owner_ui.get_node("PublishReviewDialog/Review/Changelog")
	export_package_dialog = owner_ui.get_node("ExportPackageDialog")
	for visibility in ["Public", "Friends Only", "Private", "Unlisted"]:
		workshop_visibility.add_item(visibility)
	workshop_visibility.selected = 1
	workshop_page_button.pressed.connect(_open_workshop_page)
	workshop_publish_button.pressed.connect(_publish_workshop)
	workshop_capture_button.pressed.connect(_capture_workshop_preview)
	publish_review_dialog.confirmed.connect(_confirm_publish_workshop)
	owner_ui.get_node("Toolbar/InstallVehicle").pressed.connect(install_vehicle)
	owner_ui.get_node("Toolbar/ExportPackage").pressed.connect(func(): export_package_dialog.popup_centered())
	owner_ui.get_node("Toolbar/TestDrive").pressed.connect(_test_drive)
	export_package_dialog.file_selected.connect(_export_package)
	call_deferred("_connect_steam_service")

func reset_publication() -> void:
	workshop_published_file_id = 0
	workshop_preview_captured = false
	refresh_workshop_controls()

func load_publication(published_file_id: int) -> void:
	workshop_published_file_id = published_file_id
	workshop_preview_captured = FileAccess.file_exists(workshop_preview_path())
	refresh_workshop_controls()

func set_editing_official(_definition: CarDefinition) -> void:
	refresh_workshop_controls()

func focus_visibility() -> void:
	workshop_visibility.grab_focus()

func workshop_preview_path() -> String:
	return document_controller.draft_root() + "/workshop-preview.png"

func save_preview_png(preview_path: String) -> bool:
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
		preview_image.resize(maxi(1, int(floor(float(preview_image.get_width()) * scale))), maxi(1, int(floor(float(preview_image.get_height()) * scale))), Image.INTERPOLATE_LANCZOS)
	return false

func build_package(preview_override := "", validate_package := true) -> Dictionary:
	if !document_controller.flush(workshop_published_file_id):
		return {"valid": false, "errors": PackedStringArray([document_controller.autosave_error]), "warnings": PackedStringArray()}
	var preview_path := String(preview_override)
	var preview_ready := false
	if preview_path.is_empty():
		preview_path = document_controller.draft_root() + "/thumbnail.png"
		preview_ready = save_preview_png(preview_path)
	else:
		preview_ready = FileAccess.file_exists(preview_path)
	if !preview_ready:
		var failed := {"valid": false, "errors": PackedStringArray(["Could not capture the vehicle preview image"]), "warnings": PackedStringArray()}
		diagnostics_requested.emit(failed)
		return failed
	var result: Dictionary = document_controller.session.build_vehicle_package(document_controller.draft_root() + "/package", preview_path, title_input.text, description_input.text, author_input.text, validate_package)
	diagnostics_requested.emit(result)
	if bool(result.get("valid", false)):
		draft_list_changed.emit()
	return result

func install_vehicle() -> String:
	var built := build_package()
	if !bool(built.get("valid", false)):
		return ""
	var package_io := MxtContentPackageIO.new()
	var archive_path := document_controller.draft_root() + "/%s.mxtpkg" % document_controller.draft_id
	var exported: Dictionary = package_io.export_mxtpkg(document_controller.draft_root() + "/package", archive_path)
	if !bool(exported.get("valid", false)):
		diagnostics_requested.emit(exported)
		return ""
	var imported: Dictionary = package_io.import_mxtpkg(archive_path, ProjectSettings.globalize_path(LOCAL_LIBRARY_ROOT))
	diagnostics_requested.emit(imported)
	if !bool(imported.get("valid", false)):
		return ""
	content_changed.emit()
	return "mxt:vehicle:package:" + String(imported["package_digest"]).trim_prefix("sha256:")

func _export_package(path: String) -> void:
	var built := build_package()
	if !bool(built.get("valid", false)):
		return
	var destination := path if path.to_lower().ends_with(".mxtpkg") else path + ".mxtpkg"
	diagnostics_requested.emit(MxtContentPackageIO.new().export_mxtpkg(document_controller.draft_root() + "/package", destination))

func _test_drive() -> void:
	var built := build_package("", false)
	if !bool(built.get("valid", false)):
		return
	var snapshot: MxtContentLoadResult = vehicle_content_controller.create_test_drive_snapshot(String(built.get("package_path", "")))
	content_diagnostics_requested.emit(snapshot)
	if snapshot.is_valid():
		test_drive_requested.emit(snapshot)

func _capture_workshop_preview() -> void:
	if !document_controller.draft_initialized:
		return
	workshop_preview_captured = save_preview_png(workshop_preview_path())
	workshop_status.text = "Workshop preview captured from the current camera and paint." if workshop_preview_captured else "Workshop preview capture failed."
	refresh_workshop_controls()

func refresh_workshop_controls() -> void:
	if document_controller.editing_official_definition != null:
		workshop_publish_button.disabled = true
		workshop_page_button.disabled = true
		workshop_capture_button.disabled = true
		workshop_status.text = "Workshop publishing is unavailable while editing shipped assets"
		return
	var steam_available := game_manager != null and game_manager.steam_service != null and game_manager.steam_service.is_initialized()
	workshop_publish_button.disabled = workshop_request_id != 0 or !steam_available
	workshop_page_button.disabled = workshop_request_id != 0 or workshop_published_file_id <= 0
	workshop_capture_button.disabled = workshop_request_id != 0
	if workshop_request_id != 0: return
	if !steam_available:
		workshop_status.text = "Steam Workshop is unavailable"
	elif workshop_published_file_id > 0:
		workshop_status.text = "Workshop item %d · %s" % [workshop_published_file_id, "preview captured" if workshop_preview_captured else "capture preview before update"]
	else:
		workshop_status.text = "Not published · %s" % ("preview captured" if workshop_preview_captured else "capture preview before publishing")

func _connect_steam_service() -> void:
	if game_manager == null or game_manager.steam_service == null: return
	var service := game_manager.steam_service
	if !service.workshop_request_completed.is_connected(_on_workshop_request_completed): service.workshop_request_completed.connect(_on_workshop_request_completed)
	if !service.status_changed.is_connected(_on_steam_status_changed): service.status_changed.connect(_on_steam_status_changed)
	_on_steam_status_changed(service.get_status())

func _on_steam_status_changed(status: Dictionary) -> void:
	if bool(status.get("initialized", false)) and author_input.text == "Creator":
		var persona_name := String(status.get("persona_name", ""))
		if !persona_name.is_empty(): author_input.text = persona_name
	refresh_workshop_controls()

func _publish_workshop() -> void:
	if workshop_request_id != 0: return
	if game_manager == null or game_manager.steam_service == null or !game_manager.steam_service.is_initialized():
		workshop_status.text = "Steam Workshop is unavailable"
		return
	if !workshop_preview_captured or !FileAccess.file_exists(workshop_preview_path()):
		workshop_status.text = "Capture the Workshop preview after framing and painting the machine."
		return
	var built := build_package(workshop_preview_path())
	if !bool(built.get("valid", false)): return
	var package_io := MxtContentPackageIO.new()
	var archive_path := document_controller.draft_root() + "/workshop-upload.mxtpkg"
	var exported: Dictionary = package_io.export_mxtpkg(String(built["package_path"]), archive_path)
	if !bool(exported.get("valid", false)):
		diagnostics_requested.emit(exported)
		return
	var imported: Dictionary = package_io.import_mxtpkg(archive_path, ProjectSettings.globalize_path(WORKSHOP_STAGING_ROOT))
	if !bool(imported.get("valid", false)):
		diagnostics_requested.emit(imported)
		return
	workshop_pending_package = imported
	_show_publish_review(built)

func _show_publish_review(validation: Dictionary) -> void:
	var visibility: String = ["Public", "Friends Only", "Private", "Unlisted"][workshop_visibility.selected]
	var intent_counts := _authoring_intent_counts()
	var warnings_value = validation.get("warnings", [])
	var warning_type := typeof(warnings_value)
	var warning_count: int = warnings_value.size() if warning_type in [TYPE_ARRAY, TYPE_PACKED_STRING_ARRAY] else 0
	publish_review_summary.text = "%s Workshop item\n\nTitle: %s\nAuthor: %s\nVisibility: %s\nDescription:\n%s\n\nSpecial adjustments: %d Derived · %d Custom\nValidation: Ready%s" % ["Update" if workshop_published_file_id > 0 else "New", title_input.text, author_input.text, visibility, description_input.text, int(intent_counts.get("derived", 0)), int(intent_counts.get("custom", 0)), " · %d warning(s)" % warning_count if warning_count > 0 else ""]
	var image := Image.load_from_file(workshop_preview_path())
	publish_review_preview.texture = ImageTexture.create_from_image(image) if image != null and !image.is_empty() else null
	publish_review_dialog.ok_button_text = "Update Workshop Item" if workshop_published_file_id > 0 else "Create Workshop Item"
	publish_changelog.text = ""
	publish_review_dialog.popup_centered(Vector2i(720, 650))

func _authoring_intent_counts() -> Dictionary:
	var derived := 0
	var custom := 0
	var layers: Array[String] = []
	for layer in document_controller.session.get_layer_names():
		if String(layer) != "base": layers.append(String(layer))
	layers.append("s_boost")
	for layer in layers:
		for schema_value in curve_controller.stat_schema:
			var schema: Dictionary = schema_value
			if !bool(schema.get("supports_live_modifiers", false)): continue
			if document_controller.session.is_special_derived(layer, String(schema.get("name", ""))): derived += 1
			else: custom += 1
	return {"derived": derived, "custom": custom}

func _confirm_publish_workshop() -> void:
	if workshop_pending_package.is_empty() or workshop_request_id != 0: return
	if workshop_published_file_id <= 0:
		workshop_operation = "create_item"
		workshop_status.text = "Creating Workshop item..."
		workshop_request_id = game_manager.steam_service.create_workshop_item()
		refresh_workshop_controls()
	else: _submit_workshop_update()

func _submit_workshop_update() -> void:
	var metadata := JSON.stringify({"content_type": "vehicle", "format_revision": 1, "gameplay_digest": String(workshop_pending_package.get("gameplay_digest", ""))})
	var visibility: String = ["public", "friends_only", "private", "unlisted"][workshop_visibility.selected]
	workshop_operation = "submit_update"
	workshop_status.text = "Preparing Workshop upload..."
	workshop_request_id = game_manager.steam_service.submit_workshop_item_update(workshop_published_file_id, title_input.text, description_input.text, String(workshop_pending_package.get("package_path", "")), String(workshop_pending_package.get("package_path", "")).path_join("preview.png"), ["Vehicle", "Format Revision 1"], metadata, visibility, publish_changelog.text.strip_edges())
	refresh_workshop_controls()

func _on_workshop_request_completed(request_id: int, operation: String, result: Dictionary) -> void:
	if request_id != workshop_request_id or operation != workshop_operation: return
	workshop_request_id = 0
	workshop_operation = ""
	if !bool(result.get("success", false)):
		refresh_workshop_controls()
		workshop_status.text = "Workshop failed: %s" % String(result.get("message", "Unknown error"))
		return
	if operation == "create_item":
		workshop_published_file_id = int(result.get("published_file_id", 0))
		document_controller.mark_dirty()
		document_controller.flush(workshop_published_file_id)
		refresh_workshop_controls()
		_submit_workshop_update()
		return
	refresh_workshop_controls()
	var agreement := " Accept the Steam Workshop agreement." if bool(result.get("legal_agreement_required", false)) else ""
	workshop_status.text = "Workshop upload complete.%s" % agreement
	if bool(result.get("legal_agreement_required", false)): game_manager.steam_service.open_workshop_item_page(workshop_published_file_id)

func _open_workshop_page() -> void:
	if workshop_published_file_id > 0 and game_manager != null and game_manager.steam_service != null:
		game_manager.steam_service.open_workshop_item_page(workshop_published_file_id)

func _process(_delta: float) -> void:
	if workshop_request_id == 0 or workshop_operation != "submit_update" or game_manager == null or game_manager.steam_service == null: return
	var now := Time.get_ticks_msec()
	if now < workshop_progress_update_msec + 200: return
	workshop_progress_update_msec = now
	var progress: Dictionary = game_manager.steam_service.get_workshop_update_progress()
	if bool(progress.get("active", false)):
		var total := int(progress.get("total_bytes", 0))
		var processed := int(progress.get("processed_bytes", 0))
		var percent := 0.0 if total <= 0 else 100.0 * float(processed) / float(total)
		workshop_status.text = "%s  %.1f%%" % [String(progress.get("status", "uploading")).replace("_", " ").capitalize(), percent]
