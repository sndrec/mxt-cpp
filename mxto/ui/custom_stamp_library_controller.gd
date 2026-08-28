class_name CustomStampLibraryController
extends Node

signal paint_requested
signal edit_requested(stamp_hash: String, blob: CustomStampBlob)
signal delete_requested(stamp_hash: String)

const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampPaletteCatalog = preload("res://vehicle/customization/custom_stamp_palette_catalog.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")

var owner_ui: Control
var catalog_menu: PopupMenu
var catalog_tab: VBoxContainer
var import_button: Button
var palette_option: OptionButton
var paint_button: Button
var library_grid: GridContainer
var import_dialog: FileDialog
var blobs: Array[CustomStampBlob] = []
var preview_textures := {}
var selected_import_palette_id := 0
var selected_stamp_hash := ""
var locked := false

func initialize(in_owner_ui: Control) -> void:
	owner_ui = in_owner_ui
	catalog_menu = owner_ui.get_node("CustomStampCatalogMenu")
	catalog_tab = owner_ui.get_node("Container/SettingsTabs/Stamp Catalog")
	import_button = owner_ui.get_node("Container/SettingsTabs/Stamp Catalog/CatalogActions/ImportCustomStamp")
	palette_option = owner_ui.get_node("Container/SettingsTabs/Stamp Catalog/CatalogActions/ImportPaletteOption")
	paint_button = owner_ui.get_node("Container/SettingsTabs/Stamp Catalog/CatalogActions/PaintCustomStamp")
	library_grid = owner_ui.get_node("Container/SettingsTabs/Stamp Catalog/LibraryScroll/LibraryGrid")
	import_dialog = owner_ui.get_node("CustomStampImportDialog")
	palette_option.clear()
	palette_option.add_item("Auto / Custom 15", 0)
	for palette_id in range(CustomStampPaletteCatalog.PALETTE_MIN_ID, CustomStampPaletteCatalog.PALETTE_MAX_ID + 1):
		palette_option.add_item(CustomStampPaletteCatalog.palette_name(palette_id), palette_id)
	palette_option.item_selected.connect(_on_custom_import_palette_selected.bind(palette_option))
	import_button.pressed.connect(_on_custom_stamp_import_pressed)
	paint_button.pressed.connect(_on_paint_pressed)
	import_dialog.file_selected.connect(_on_custom_stamp_import_file_selected)
	catalog_menu.clear()
	catalog_menu.add_item("Edit", 0)
	catalog_menu.add_item("Delete", 1)
	catalog_menu.id_pressed.connect(_on_catalog_action_selected)
	refresh()

func set_locked(value: bool) -> void:
	locked = value
	if catalog_tab != null:
		catalog_tab.modulate = Color(0.55, 0.55, 0.55, 1.0) if locked else Color.WHITE
	if import_button != null:
		import_button.disabled = locked
	if palette_option != null:
		palette_option.disabled = locked
	if paint_button != null:
		paint_button.disabled = locked

func refresh() -> void:
	blobs.clear()
	for value in CustomStampStore.list_local_blobs():
		var blob := value as CustomStampBlob
		if blob != null:
			blobs.append(blob)
	_refresh_custom_stamp_catalog_grid()

func blob_for_hash(stamp_hash: String) -> CustomStampBlob:
	for blob in blobs:
		if blob.stamp_hash == stamp_hash:
			return blob
	var loaded := CustomStampStore.load_blob(stamp_hash)
	if loaded != null:
		blobs.append(loaded)
	return loaded

func preview_texture(blob: CustomStampBlob) -> Texture2D:
	if blob == null:
		return null
	if preview_textures.has(blob.stamp_hash):
		return preview_textures[blob.stamp_hash]
	var texture := CustomStampStore.create_preview_texture(blob)
	if texture != null:
		preview_textures[blob.stamp_hash] = texture
	return texture

func erase_preview(stamp_hash: String) -> void:
	preview_textures.erase(stamp_hash)

func delete_blob(stamp_hash: String) -> bool:
	var err := CustomStampStore.delete_blob(stamp_hash)
	if err != OK:
		push_warning("Failed to delete custom stamp: %s" % err)
		return false
	preview_textures.erase(stamp_hash)
	selected_stamp_hash = ""
	refresh()
	return true

func _on_paint_pressed() -> void:
	if !locked:
		paint_requested.emit()

func _on_custom_stamp_import_pressed() -> void:
	if locked:
		return
	import_dialog.popup_centered(Vector2i(720, 520))

func _on_custom_import_palette_selected(index: int, option: OptionButton) -> void:
	selected_import_palette_id = option.get_item_id(index)

func _on_custom_stamp_import_file_selected(path: String) -> void:
	if locked:
		return
	var result := CustomStampStore.import_png(path, selected_import_palette_id)
	if !bool(result.get("ok", false)):
		push_warning("Failed to import custom stamp: %s" % str(result.get("error", "unknown error")))
		return
	var blob := result.get("blob", null) as CustomStampBlob
	if blob == null:
		return
	refresh()

func _refresh_custom_stamp_catalog_grid() -> void:
	if library_grid == null:
		return
	for child in library_grid.get_children():
		child.queue_free()
	if blobs.is_empty():
		var empty_label := Label.new()
		empty_label.text = "No custom stamps imported."
		empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		empty_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		library_grid.add_child(empty_label)
		return
	for blob in blobs:
		var custom_blob := blob as CustomStampBlob
		if custom_blob == null:
			continue
		var tile := VBoxContainer.new()
		tile.custom_minimum_size = Vector2(168.0, 188.0)
		tile.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		tile.mouse_filter = Control.MOUSE_FILTER_STOP
		tile.gui_input.connect(_on_catalog_tile_input.bind(custom_blob.stamp_hash))
		var preview := TextureRect.new()
		preview.custom_minimum_size = Vector2(160.0, 144.0)
		preview.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		preview.mouse_filter = Control.MOUSE_FILTER_IGNORE
		preview.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
		preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		preview.texture = preview_texture(custom_blob)
		tile.add_child(preview)
		var label := Label.new()
		label.text = _custom_stamp_button_text(custom_blob)
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		tile.add_child(label)
		library_grid.add_child(tile)

func _on_catalog_tile_input(event: InputEvent, stamp_hash: String) -> void:
	if locked:
		return
	var mouse_button := event as InputEventMouseButton
	if mouse_button == null or !mouse_button.pressed or mouse_button.button_index != MOUSE_BUTTON_RIGHT:
		return
	selected_stamp_hash = stamp_hash
	var blob := blob_for_hash(stamp_hash)
	catalog_menu.set_item_disabled(0, blob == null or blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE)
	catalog_menu.position = Vector2i(owner_ui.get_global_mouse_position())
	catalog_menu.popup()
	owner_ui.accept_event()

func _on_catalog_action_selected(id: int) -> void:
	if selected_stamp_hash.is_empty() or locked:
		return
	var blob := blob_for_hash(selected_stamp_hash)
	match id:
		0:
			if blob != null:
				edit_requested.emit(selected_stamp_hash, blob)
		1:
			delete_requested.emit(selected_stamp_hash)

func _custom_stamp_button_text(blob: CustomStampBlob) -> String:
	var mode := "4bpp" if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE else "8bpp"
	var palette_text := "Custom" if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE else CustomStampPaletteCatalog.palette_name(blob.palette_id)
	return "%dx%d %s %s %s" % [blob.width, blob.height, mode, palette_text, blob.stamp_hash.substr(0, 8)]

