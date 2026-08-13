class_name TrackPackageEditor extends VBoxContainer

const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const DRAFTS_ROOT := "user://track_drafts"
const LOCAL_LIBRARY_ROOT := "user://content/packages"
const WORKSHOP_STAGING_ROOT := "user://content/workshop_staging"

@onready var draft_option: OptionButton = $Toolbar/Draft
@onready var title_input: LineEdit = $Metadata/Title
@onready var author_input: LineEdit = $Metadata/Author
@onready var description_input: LineEdit = $Metadata/Description
@onready var track_path: LineEdit = $Sources/TrackPath
@onready var visual_path: LineEdit = $Sources/VisualPath
@onready var environment_path: LineEdit = $Sources/EnvironmentPath
@onready var preview_path: LineEdit = $Sources/PreviewPath
@onready var visibility_option: OptionButton = $Workshop/Visibility
@onready var workshop_status: Label = $Workshop/Status
@onready var diagnostics: RichTextLabel = $Diagnostics
@onready var workshop_page_button: Button = $Toolbar/OpenPage

var game_manager: GameManager
var vehicle_content_controller: VehicleContentControllerClass
var builder := MxtTrackPackageBuilder.new()
var draft_id := ""
var published_file_id := 0
var workshop_request_id := 0
var workshop_operation := ""
var workshop_package: Dictionary = {}
var progress_update_msec := 0

func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	if game_manager != null:
		vehicle_content_controller = game_manager.get_node("VehicleContentController") as VehicleContentControllerClass
	for visibility in ["Public", "Friends Only", "Private", "Unlisted"]:
		visibility_option.add_item(visibility)
	visibility_option.selected = 1
	$Toolbar/New.pressed.connect(_new_draft)
	$Toolbar/Open.pressed.connect(_open_selected_draft)
	$Toolbar/Build.pressed.connect(_build_and_install)
	$Toolbar/Export.pressed.connect(func(): $ExportDialog.popup_centered())
	$Toolbar/Publish.pressed.connect(_publish)
	workshop_page_button.pressed.connect(_open_workshop_page)
	$Sources/ChooseTrack.pressed.connect(func(): $TrackDialog.popup_centered())
	$Sources/ChooseVisual.pressed.connect(func(): $VisualDialog.popup_centered())
	$Sources/ChooseEnvironment.pressed.connect(func(): $EnvironmentDialog.popup_centered())
	$Sources/ChoosePreview.pressed.connect(func(): $PreviewDialog.popup_centered())
	$TrackDialog.file_selected.connect(func(path): track_path.text = path)
	$VisualDialog.file_selected.connect(func(path): visual_path.text = path)
	$EnvironmentDialog.file_selected.connect(func(path): environment_path.text = path)
	$PreviewDialog.file_selected.connect(func(path): preview_path.text = path)
	$ExportDialog.file_selected.connect(_export_package)
	_refresh_drafts()
	_new_draft()
	call_deferred("_connect_steam_service")

func _draft_root() -> String:
	return "%s/%s" % [DRAFTS_ROOT, draft_id]

func _new_draft() -> void:
	draft_id = "track_%d_%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_msec() % 1000000]
	title_input.text = "New Track"
	author_input.text = game_manager.steam_service.get_persona_name() if game_manager != null and game_manager.steam_service != null and !game_manager.steam_service.get_persona_name().is_empty() else "Creator"
	description_input.text = ""
	track_path.text = ""
	visual_path.text = ""
	environment_path.text = ""
	preview_path.text = ""
	published_file_id = 0
	_refresh_workshop_status()
	diagnostics.text = "Choose an exported .mxt_track, GLB, environment JSON, and preview PNG."

func _refresh_drafts() -> void:
	draft_option.clear()
	var directory := DirAccess.open(DRAFTS_ROOT)
	if directory == null:
		return
	directory.list_dir_begin()
	var folder := directory.get_next()
	while !folder.is_empty():
		if directory.current_is_dir() and FileAccess.file_exists(DRAFTS_ROOT.path_join(folder).path_join("draft.json")):
			draft_option.add_item(folder)
			draft_option.set_item_metadata(draft_option.item_count - 1, folder)
		folder = directory.get_next()
	directory.list_dir_end()

func _save_draft_sidecar() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(_draft_root()))
	var file := FileAccess.open(_draft_root() + "/draft.json", FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify({
			"title": title_input.text,
			"author": author_input.text,
			"description": description_input.text,
			"track_path": track_path.text,
			"visual_path": visual_path.text,
			"environment_path": environment_path.text,
			"preview_path": preview_path.text,
			"published_file_id": published_file_id,
		}, "  "))
	_refresh_drafts()

func _open_selected_draft() -> void:
	if draft_option.selected < 0:
		return
	var selected := String(draft_option.get_item_metadata(draft_option.selected))
	var value = JSON.parse_string(FileAccess.get_file_as_string(DRAFTS_ROOT.path_join(selected).path_join("draft.json")))
	if typeof(value) != TYPE_DICTIONARY:
		return
	draft_id = selected
	title_input.text = String(value.get("title", "Track"))
	author_input.text = String(value.get("author", "Creator"))
	description_input.text = String(value.get("description", ""))
	track_path.text = String(value.get("track_path", ""))
	visual_path.text = String(value.get("visual_path", ""))
	environment_path.text = String(value.get("environment_path", ""))
	preview_path.text = String(value.get("preview_path", ""))
	published_file_id = int(value.get("published_file_id", 0))
	_refresh_workshop_status()

func _build_package() -> Dictionary:
	var result: Dictionary = builder.build_package(
		track_path.text,
		visual_path.text,
		environment_path.text,
		preview_path.text,
		_draft_root() + "/package",
		title_input.text,
		description_input.text,
		author_input.text)
	_show_diagnostics(result)
	if bool(result.get("valid", false)):
		_save_draft_sidecar()
	return result

func _archive_and_import(built: Dictionary, library_root: String, archive_name: String) -> Dictionary:
	var io := MxtContentPackageIO.new()
	var archive_path := _draft_root() + "/" + archive_name
	var exported: Dictionary = io.export_mxtpkg(String(built.get("package_path", "")), archive_path)
	if !bool(exported.get("valid", false)):
		return exported
	return io.import_mxtpkg(archive_path, ProjectSettings.globalize_path(library_root))

func _build_and_install() -> void:
	var built := _build_package()
	if !bool(built.get("valid", false)):
		return
	var imported := _archive_and_import(built, LOCAL_LIBRARY_ROOT, "track.mxtpkg")
	_show_diagnostics(imported)
	if bool(imported.get("valid", false)) and vehicle_content_controller != null:
		vehicle_content_controller.refresh_installed_content()

func _export_package(path: String) -> void:
	var built := _build_package()
	if !bool(built.get("valid", false)):
		return
	var destination := path if path.to_lower().ends_with(".mxtpkg") else path + ".mxtpkg"
	_show_diagnostics(MxtContentPackageIO.new().export_mxtpkg(String(built["package_path"]), destination))

func _connect_steam_service() -> void:
	if game_manager == null or game_manager.steam_service == null:
		return
	if !game_manager.steam_service.workshop_request_completed.is_connected(_on_workshop_completed):
		game_manager.steam_service.workshop_request_completed.connect(_on_workshop_completed)

func _publish() -> void:
	if workshop_request_id != 0:
		return
	if game_manager == null or game_manager.steam_service == null or !game_manager.steam_service.is_initialized():
		workshop_status.text = "Steam Workshop is unavailable"
		return
	var built := _build_package()
	if !bool(built.get("valid", false)):
		return
	workshop_package = _archive_and_import(built, WORKSHOP_STAGING_ROOT, "workshop-upload.mxtpkg")
	if !bool(workshop_package.get("valid", false)):
		_show_diagnostics(workshop_package)
		return
	if published_file_id <= 0:
		workshop_operation = "create_item"
		workshop_status.text = "Creating Workshop item..."
		workshop_request_id = game_manager.steam_service.create_workshop_item()
	else:
		_submit_update()

func _submit_update() -> void:
	var metadata := JSON.stringify({
		"content_type": "track",
		"format_revision": 1,
		"gameplay_digest": String(workshop_package.get("gameplay_digest", "")),
	})
	var visibility: String = ["public", "friends_only", "private", "unlisted"][visibility_option.selected]
	workshop_operation = "submit_update"
	workshop_status.text = "Preparing Workshop upload..."
	var package_path := String(workshop_package.get("package_path", ""))
	workshop_request_id = game_manager.steam_service.submit_workshop_item_update(
		published_file_id, title_input.text, description_input.text, package_path,
		package_path.path_join("preview.png"), ["Track", "Format Revision 1"], metadata,
		visibility, "Updated from the in-game Track Package Assembler")

func _on_workshop_completed(request_id: int, operation: String, result: Dictionary) -> void:
	if request_id != workshop_request_id or operation != workshop_operation:
		return
	workshop_request_id = 0
	workshop_operation = ""
	if !bool(result.get("success", false)):
		workshop_status.text = "Workshop failed: %s" % String(result.get("message", "Unknown error"))
		return
	if operation == "create_item":
		published_file_id = int(result.get("published_file_id", 0))
		_save_draft_sidecar()
		_submit_update()
		return
	var agreement_required := bool(result.get("legal_agreement_required", false))
	workshop_status.text = "Workshop upload complete.%s" % (" Accept the Workshop agreement." if agreement_required else "")
	_refresh_workshop_status()
	if agreement_required:
		game_manager.steam_service.open_workshop_item_page(published_file_id)

func _refresh_workshop_status() -> void:
	workshop_page_button.disabled = published_file_id <= 0
	if workshop_request_id == 0:
		workshop_status.text = "Workshop item %d" % published_file_id if published_file_id > 0 else "Not published"

func _open_workshop_page() -> void:
	if published_file_id > 0:
		game_manager.steam_service.open_workshop_item_page(published_file_id)

func _process(_delta: float) -> void:
	if workshop_request_id == 0 or workshop_operation != "submit_update":
		return
	var now := Time.get_ticks_msec()
	if now < progress_update_msec + 200:
		return
	progress_update_msec = now
	var progress: Dictionary = game_manager.steam_service.get_workshop_update_progress()
	if bool(progress.get("active", false)):
		var total := int(progress.get("total_bytes", 0))
		var percent := 0.0 if total <= 0 else 100.0 * float(progress.get("processed_bytes", 0)) / float(total)
		workshop_status.text = "%s  %.1f%%" % [String(progress.get("status", "uploading")).replace("_", " ").capitalize(), percent]

func _show_diagnostics(result: Dictionary) -> void:
	var lines: Array[String] = []
	for error in result.get("errors", []):
		lines.append("[color=#ff6961]ERROR: %s[/color]" % error)
	for warning in result.get("warnings", []):
		lines.append("[color=#ffd166]WARNING: %s[/color]" % warning)
	if lines.is_empty() and bool(result.get("valid", false)):
		lines.append("[color=#73e2a7]Validated package ready[/color]")
	diagnostics.text = "\n".join(lines)
