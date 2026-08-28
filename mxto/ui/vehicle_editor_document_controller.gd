class_name VehicleEditorDocumentController
extends Node

signal diagnostics_requested(result: Dictionary)
signal content_changed
signal draft_saved

const DRAFTS_ROOT := "user://vehicle_drafts"
const AUTOSAVE_DEBOUNCE_MSEC := 900
const AUTOSAVE_RETRY_MSEC := 5000

var session: MxtCarAuthoringSession
var preview_controller: VehicleEditorPreviewController
var curve_controller: VehicleEditorCurveController
var draft_store := MxtCarDraftStore.new()
var draft_id := ""
var current_properties_path := ""
var editing_official_definition: CarDefinition
var metadata_dirty := false
var draft_initialized := false
var autosave_due_msec := 0
var autosave_error := ""
var title_input: LineEdit
var author_input: LineEdit
var description_input: TextEdit
var autosave_status: Label

func initialize(owner_ui: Control, authoring_session: MxtCarAuthoringSession, preview: VehicleEditorPreviewController, curve: VehicleEditorCurveController) -> void:
	session = authoring_session
	preview_controller = preview
	curve_controller = curve
	title_input = owner_ui.get_node("Metadata/Title")
	author_input = owner_ui.get_node("Metadata/Author")
	description_input = owner_ui.get_node("Metadata/Description")
	autosave_status = owner_ui.get_node("Toolbar/AutosaveStatus")
	title_input.text_changed.connect(func(_value): mark_dirty())
	author_input.text_changed.connect(func(_value): mark_dirty())
	description_input.text_changed.connect(func(_value): mark_dirty())

func set_session(authoring_session: MxtCarAuthoringSession) -> void:
	session = authoring_session
	preview_controller.set_session(session)
	curve_controller.set_session(session)

func draft_root() -> String:
	return "%s/%s" % [DRAFTS_ROOT, draft_id]

func has_editable_document() -> bool:
	return draft_initialized or editing_official_definition != null

func mark_dirty() -> void:
	metadata_dirty = true
	autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_DEBOUNCE_MSEC
	update_status("Unsaved changes")

func schedule_autosave() -> void:
	autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_DEBOUNCE_MSEC
	update_status("Unsaved changes")

func begin_new(in_draft_id: String) -> void:
	editing_official_definition = null
	draft_id = in_draft_id
	draft_initialized = true
	current_properties_path = ""
	metadata_dirty = true
	autosave_error = ""

func begin_loaded(in_draft_id: String, properties_path: String) -> void:
	editing_official_definition = null
	draft_id = in_draft_id
	draft_initialized = true
	current_properties_path = properties_path
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0

func begin_official(definition: CarDefinition) -> void:
	editing_official_definition = definition
	draft_initialized = false
	draft_id = ""
	current_properties_path = definition.properties_path
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0

func clear() -> void:
	draft_initialized = false
	draft_id = ""
	current_properties_path = ""
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0

func draft_metadata(workshop_published_file_id: int) -> Dictionary:
	return {
		"title": title_input.text,
		"author_name": author_input.text,
		"description": description_input.text,
		"workshop_published_file_id": workshop_published_file_id,
		"authoring_intent": session.get_authoring_intent(),
		"preview_livery": preview_controller.livery_dict(),
	}

func process_autosave(now: int, workshop_published_file_id: int) -> void:
	if !has_editable_document() or curve_controller.gesture_active or (!metadata_dirty and !session.is_dirty()):
		return
	if autosave_due_msec == 0:
		autosave_due_msec = now + AUTOSAVE_DEBOUNCE_MSEC
		update_status("Unsaved changes")
	elif now >= autosave_due_msec:
		save(workshop_published_file_id)

func save(workshop_published_file_id: int) -> bool:
	if editing_official_definition != null:
		return _save_official()
	if !draft_initialized:
		return true
	var result: Dictionary = draft_store.save_draft(draft_id, session, draft_metadata(workshop_published_file_id))
	if !bool(result.get("valid", false)):
		var errors: PackedStringArray = result.get("errors", PackedStringArray(["Unknown autosave error"]))
		autosave_error = "Unknown autosave error" if errors.is_empty() else String(errors[0])
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_RETRY_MSEC
		update_status("Autosave failed: %s" % autosave_error, true)
		diagnostics_requested.emit(result)
		return false
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0
	current_properties_path = String(result.get("properties_path", ""))
	update_status("Saved")
	draft_saved.emit()
	return true

func flush(workshop_published_file_id: int) -> bool:
	curve_controller.cancel_active_edit()
	if !has_editable_document():
		return true
	if metadata_dirty or session.is_dirty() or !autosave_error.is_empty():
		return save(workshop_published_file_id)
	return true

func update_status(message: String, failed := false) -> void:
	autosave_status.text = message
	autosave_status.modulate = Color(1.0, 0.42, 0.35) if failed else Color.WHITE

func _save_official() -> bool:
	if editing_official_definition == null:
		return false
	if current_properties_path != editing_official_definition.properties_path or !current_properties_path.begins_with("res://vehicle/asset/"):
		autosave_error = "Official vehicle save target is invalid"
		update_status(autosave_error, true)
		return false
	var result: Dictionary = session.save_file(current_properties_path)
	if !bool(result.get("valid", false)):
		var errors: PackedStringArray = result.get("errors", PackedStringArray(["Unknown official vehicle save error"]))
		autosave_error = "Unknown official vehicle save error" if errors.is_empty() else String(errors[0])
		autosave_due_msec = Time.get_ticks_msec() + AUTOSAVE_RETRY_MSEC
		update_status("Official save failed: %s" % autosave_error, true)
		diagnostics_requested.emit(result)
		return false
	metadata_dirty = false
	autosave_error = ""
	autosave_due_msec = 0
	curve_controller.reset_performance_analysis()
	content_changed.emit()
	update_status("Saved official %s" % editing_official_definition.name)
	return true
