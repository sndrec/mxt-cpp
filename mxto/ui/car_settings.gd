extends Control

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CustomStampAtlasBuilder = preload("res://vehicle/customization/custom_stamp_atlas_builder.gd")
const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampPacker = preload("res://vehicle/customization/custom_stamp_packer.gd")
const CustomStampPaletteCatalog = preload("res://vehicle/customization/custom_stamp_palette_catalog.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")
const CarRenderManager = preload("res://vehicle/car_render_manager.gd")
const GARAGE_PREVIEW_WORLD_SCENE = preload("res://ui/garage_preview_world.tscn")

const STAMP_EDIT_MIN_SCREEN_SIZE := 1.0
const PREVIEW_PAN_LIMIT := 4.0

@onready var machine_setting_slider: HSlider = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/MachineSettingSlider
@onready var machine_setting_percent: Label = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/MachineSettingPercent
@onready var vehicle_selector: ItemList = $Container/SettingsTabs/Driver/VehicleScroll/VehicleSelector
@onready var close_settings: Button = $Container/CloseSettings
@onready var car_preview_space: ColorRect = $Container/SettingsTabs/Garage/CarPreviewSpace
@onready var custom_stamp_budget_label: Label = $Container/SettingsTabs/Garage/CarPreviewSpace/CustomStampBudget
@onready var custom_stamp_atlas_preview: TextureRect = $Container/SettingsTabs/Garage/CarPreviewSpace/CustomStampAtlasPreview
@onready var pilot_name_input: LineEdit = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/PilotNameInput
@onready var spectator_toggle: CheckBox = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/SpectatorToggle
@onready var car_name_label: Label = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/CarName
@onready var sticker_slot_1: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot1
@onready var sticker_slot_2: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot2
@onready var sticker_slot_3: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot3
@onready var sticker_slot_4: OptionButton = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/StickerGrid/StickerSlot4
@onready var primary_colour_picker: ColorPickerButton = $Container/SettingsTabs/Garage/GaragePanel/PaintGrid/PrimaryColourPicker
@onready var secondary_colour_picker: ColorPickerButton = $Container/SettingsTabs/Garage/GaragePanel/PaintGrid/SecondaryColourPicker
@onready var accent_colour_picker: ColorPickerButton = $Container/SettingsTabs/Garage/GaragePanel/PaintGrid/AccentColourPicker
@onready var settings_tab_container: TabContainer = $Container/SettingsTabs
@onready var driver_tab: VBoxContainer = $Container/SettingsTabs/Driver
@onready var garage_tab: HBoxContainer = $Container/SettingsTabs/Garage
@onready var garage_panel: VBoxContainer = $Container/SettingsTabs/Garage/GaragePanel
@onready var garage_car_name_label: Label = $Container/SettingsTabs/Garage/GaragePanel/GarageCarName
@onready var stamp_layer_list: VBoxContainer = $Container/SettingsTabs/Garage/GaragePanel/StampLayerScroll/StampLayerList
@onready var stamp_action_menu: PopupMenu = $StampActionMenu
@onready var custom_stamp_catalog_menu: PopupMenu = $CustomStampCatalogMenu
@onready var stamp_properties_popup: PopupPanel = $StampPropertiesPopup
@onready var stamp_properties_rotation: SpinBox = $StampPropertiesPopup/PropertiesRoot/RotationRow/RotationSpin
@onready var stamp_properties_scale_x: SpinBox = $StampPropertiesPopup/PropertiesRoot/ScaleXRow/ScaleXSpin
@onready var stamp_properties_scale_y: SpinBox = $StampPropertiesPopup/PropertiesRoot/ScaleYRow/ScaleYSpin
@onready var stamp_properties_flip_horizontal: CheckBox = $StampPropertiesPopup/PropertiesRoot/FlipHorizontal
@onready var stamp_properties_flip_vertical: CheckBox = $StampPropertiesPopup/PropertiesRoot/FlipVertical
@onready var stamp_properties_mirror_local_x: CheckBox = $StampPropertiesPopup/PropertiesRoot/MirrorLocalX
@onready var stamp_properties_close_button: Button = $StampPropertiesPopup/PropertiesRoot/Close
@onready var stamp_chooser_popup: PopupPanel = $StampChooser
@onready var stamp_chooser_title: Label = $StampChooser/StampChooserList/Title
@onready var stamp_chooser_tabs: TabContainer = $StampChooser/StampChooserList/StampChooserTabs
@onready var stamp_chooser_base_list: GridContainer = $StampChooser/StampChooserList/StampChooserTabs/Base/BaseList
@onready var stamp_chooser_custom_list: GridContainer = $StampChooser/StampChooserList/StampChooserTabs/Custom/CustomList
@onready var stamp_chooser_cancel_button: Button = $StampChooser/StampChooserList/Cancel
@onready var custom_stamp_catalog_tab: VBoxContainer = get_node("Container/SettingsTabs/Stamp Catalog")
@onready var custom_stamp_catalog_import_button: Button = get_node("Container/SettingsTabs/Stamp Catalog/CatalogActions/ImportCustomStamp")
@onready var custom_stamp_catalog_palette_option: OptionButton = get_node("Container/SettingsTabs/Stamp Catalog/CatalogActions/ImportPaletteOption")
@onready var custom_stamp_catalog_paint_button: Button = get_node("Container/SettingsTabs/Stamp Catalog/CatalogActions/PaintCustomStamp")
@onready var custom_stamp_library_grid: GridContainer = get_node("Container/SettingsTabs/Stamp Catalog/LibraryScroll/LibraryGrid")
@onready var custom_stamp_import_dialog: FileDialog = $CustomStampImportDialog
@onready var custom_stamp_painter_popup: PopupPanel = $CustomStampPainter
@onready var custom_stamp_painter_size_option: OptionButton = $CustomStampPainter/PainterRoot/SizeRow/SizeOption
@onready var custom_stamp_painter_canvas: TextureRect = $CustomStampPainter/PainterRoot/Canvas
@onready var custom_stamp_painter_palette_grid: GridContainer = $CustomStampPainter/PainterRoot/PaletteGrid
@onready var custom_stamp_painter_colour_picker: ColorPickerButton = $CustomStampPainter/PainterRoot/ColourPicker
@onready var custom_stamp_painter_clear_button: Button = $CustomStampPainter/PainterRoot/ButtonRow/Clear
@onready var custom_stamp_painter_save_button: Button = $CustomStampPainter/PainterRoot/ButtonRow/Save
@onready var custom_stamp_painter_cancel_button: Button = $CustomStampPainter/PainterRoot/ButtonRow/Cancel
@onready var stamp_edit_overlay: Control = $Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay
@onready var stamp_edit_square: Panel = $Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay/StampEditSquare
@onready var stamp_edit_confirm_button: Button = $Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay/Confirm
@onready var stamp_edit_cancel_button: Button = $Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay/Cancel

var game_manager: GameManager
var player_settings: PlayerSettings = PlayerSettings.new()
var current_livery: CarLivery = CarLivery.new()
var current_livery_enabled := false
var selected_stamp_index := -1
var car_defs: Array = []
var sticker_selection: StickerSelection = preload("res://ui/emote_sticker/sticker_selection.tres")
var stamp_catalog: CarStampCatalog = preload("res://vehicle/customization/stamp_catalog.tres")
var sticker_selectors: Array[OptionButton] = []
var updating_colour_controls := false
var updating_stamp_controls := false
var livery_dirty := false
var stamp_drag_source_layer := -1
var stamp_drag_target_layer := -1
var stamp_drag_start_position := Vector2.ZERO
var stamp_drag_active := false
var suppress_next_stamp_press := false
var stamp_properties_layer := -1
var updating_stamp_properties := false
var stamp_layer_rows: Array[Control] = []
var stamp_layer_buttons: Array[Button] = []
var stamp_layer_colour_pickers: Array[ColorPickerButton] = []
var custom_stamp_blobs: Array = []
var custom_stamp_preview_textures := {}
var preview_custom_stamp_atlas: Texture2D = null
var preview_custom_stamp_region_texture: Texture2D = null
var preview_custom_stamp_atlas_thread: Thread = null
var preview_custom_stamp_atlas_active_revision := 0
var preview_custom_stamp_atlas_latest_revision := 0
var preview_custom_stamp_atlas_queued_records: Array = []
var preview_custom_stamp_atlas_has_queued := false
var custom_painter_size := Vector2i(32, 32)
var custom_painter_indices := PackedByteArray()
var custom_painter_palette := PackedColorArray()
var custom_painter_texture: ImageTexture = null
var custom_painter_colour_index := 1
var custom_painter_drawing := false
var selected_custom_import_palette_id := 0
var selected_catalog_stamp_hash := ""
var editing_catalog_stamp_hash := ""
var preview_container: SubViewportContainer
var preview_viewport: SubViewport
var preview_root: Node3D
var preview_camera: Camera3D
var preview_vehicle: Node3D
var preview_vehicle_base_transform := Transform3D.IDENTITY
var preview_render_manager: CarRenderManager
var preview_edit_render_manager: CarRenderManager
var preview_above_render_manager: CarRenderManager
var preview_yaw := deg_to_rad(25.0)
var preview_pitch := deg_to_rad(-9.0)
var preview_pan := Vector3.ZERO
var preview_distance := 22.0
var preview_drag_button := 0
var preview_drag_start := Vector2.ZERO
var preview_drag_last := Vector2.ZERO
var preview_drag_moved := false
var preview_has_transform_override := false
var preview_transform_override := Transform3D.IDENTITY
var preview_has_camera_override := false
var preview_camera_override := Transform3D.IDENTITY
enum StampUiMode { IDLE, CHOOSING, EDITING }
var stamp_ui_mode := StampUiMode.IDLE
var pending_stamp_layer := -1
var pending_stamp_choice_action := ""
var editing_stamp_layer := -1
var editing_stamp: CarLiveryStamp
var editing_original_stamp: CarLiveryStamp
var editing_is_new := false
var editing_previous_livery_enabled := false
var stamp_edit_rect_size := Vector2(160.0, 160.0)
var stamp_edit_roll := 0.0
var stamp_edit_drag_kind := ""
var stamp_edit_drag_start_mouse := Vector2.ZERO
var stamp_edit_drag_start_center := Vector2.ZERO
var stamp_edit_drag_start_size := Vector2.ONE
var stamp_edit_drag_start_roll := 0.0

func _ready() -> void:
	game_manager = get_parent() as GameManager
	_build_stamp_layer_buttons()
	_load_settings()
	_load_car_defs()
	_refresh_custom_stamp_library()
	sticker_selectors = [sticker_slot_1, sticker_slot_2, sticker_slot_3, sticker_slot_4]
	_populate_sticker_selectors()
	_setup_garage_preview()
	_setup_stamp_menus()
	_setup_stamp_properties_popup()
	_setup_custom_stamp_catalog()
	_setup_custom_stamp_painter()
	_update_controls()
	machine_setting_slider.value_changed.connect(_on_slider_changed)
	vehicle_selector.item_selected.connect(_on_vehicle_selected)
	pilot_name_input.text_changed.connect(_on_name_changed)
	close_settings.pressed.connect(_on_close_pressed)
	spectator_toggle.toggled.connect(_on_spectator_toggled)
	primary_colour_picker.color_changed.connect(_on_primary_colour_changed)
	secondary_colour_picker.color_changed.connect(_on_secondary_colour_changed)
	accent_colour_picker.color_changed.connect(_on_accent_colour_changed)
	for i in range(sticker_selectors.size()):
		sticker_selectors[i].item_selected.connect(_on_sticker_selected.bind(i))

func _build_stamp_layer_buttons() -> void:
	if stamp_layer_list == null:
		return
	stamp_layer_rows.clear()
	stamp_layer_buttons.clear()
	stamp_layer_colour_pickers.clear()
	var count := mini(stamp_layer_list.get_child_count(), CarLivery.MAX_STAMPS)
	for layer in range(count):
		var row := stamp_layer_list.get_child(layer) as Control
		if row == null:
			continue
		var button := row as Button
		if button == null:
			button = row.get_node_or_null("StampButton") as Button
		if button == null:
			continue
		var colour_picker := row.get_node_or_null("ColourPicker") as ColorPickerButton
		if colour_picker != null:
			colour_picker.color_changed.connect(_on_stamp_colour_changed.bind(layer))
			colour_picker.popup_closed.connect(_on_stamp_colour_popup_closed.bind(layer))
		button.pressed.connect(_on_stamp_layer_pressed.bind(layer))
		button.gui_input.connect(_on_stamp_layer_gui_input.bind(layer))
		stamp_layer_rows.append(row)
		stamp_layer_buttons.append(button)
		stamp_layer_colour_pickers.append(colour_picker)

func _setup_stamp_menus() -> void:
	stamp_action_menu.clear()
	stamp_action_menu.add_item("Change", 0)
	stamp_action_menu.add_item("Edit", 1)
	stamp_action_menu.add_item("Delete", 2)
	stamp_action_menu.add_item("Properties", 3)
	stamp_action_menu.id_pressed.connect(_on_stamp_action_selected)
	stamp_chooser_cancel_button.pressed.connect(_on_stamp_choice_cancel_pressed)

func _setup_stamp_properties_popup() -> void:
	stamp_properties_rotation.value_changed.connect(_on_stamp_property_number_changed)
	stamp_properties_scale_x.value_changed.connect(_on_stamp_property_number_changed)
	stamp_properties_scale_y.value_changed.connect(_on_stamp_property_number_changed)
	stamp_properties_flip_horizontal.toggled.connect(_on_stamp_property_toggled)
	stamp_properties_flip_vertical.toggled.connect(_on_stamp_property_toggled)
	stamp_properties_mirror_local_x.toggled.connect(_on_stamp_property_toggled)
	stamp_properties_close_button.pressed.connect(func(): stamp_properties_popup.hide())
	stamp_properties_popup.popup_hide.connect(_on_stamp_properties_popup_hidden)

func _setup_custom_stamp_catalog() -> void:
	custom_stamp_catalog_palette_option.clear()
	custom_stamp_catalog_palette_option.add_item("Auto / Custom 15", 0)
	for palette_id in range(CustomStampPaletteCatalog.PALETTE_MIN_ID, CustomStampPaletteCatalog.PALETTE_MAX_ID + 1):
		custom_stamp_catalog_palette_option.add_item(CustomStampPaletteCatalog.palette_name(palette_id), palette_id)
	custom_stamp_catalog_palette_option.item_selected.connect(_on_custom_import_palette_selected.bind(custom_stamp_catalog_palette_option))
	custom_stamp_catalog_import_button.pressed.connect(_on_custom_stamp_import_pressed)
	custom_stamp_catalog_paint_button.pressed.connect(_on_custom_stamp_paint_pressed)
	custom_stamp_catalog_menu.clear()
	custom_stamp_catalog_menu.add_item("Edit", 0)
	custom_stamp_catalog_menu.add_item("Delete", 1)
	custom_stamp_catalog_menu.id_pressed.connect(_on_custom_stamp_catalog_action_selected)
	_refresh_custom_stamp_catalog_grid()

func _setup_custom_stamp_painter() -> void:
	custom_stamp_painter_size_option.clear()
	for size in [8, 16, 32, 64, 128]:
		custom_stamp_painter_size_option.add_item("%dx%d" % [size, size], size)
	custom_stamp_painter_size_option.add_item("256x128", 256128)
	custom_stamp_painter_size_option.add_item("128x256", 128256)
	custom_stamp_painter_size_option.select(2)
	custom_stamp_painter_size_option.item_selected.connect(_on_custom_painter_size_selected)
	custom_stamp_painter_canvas.gui_input.connect(_on_custom_painter_canvas_input)
	custom_stamp_painter_colour_picker.color_changed.connect(_on_custom_painter_colour_changed)
	custom_stamp_painter_clear_button.pressed.connect(_on_custom_painter_clear_pressed)
	custom_stamp_painter_save_button.pressed.connect(_on_custom_painter_save_pressed)
	custom_stamp_painter_cancel_button.pressed.connect(_on_custom_painter_cancel_pressed)
	_custom_painter_reset(Vector2i(32, 32))

func _input(event: InputEvent) -> void:
	if !visible or stamp_drag_source_layer < 0:
		return
	var motion := event as InputEventMouseMotion
	if motion != null:
		_update_stamp_drag_target()
		if stamp_drag_active:
			get_viewport().set_input_as_handled()
		return
	var mouse_button := event as InputEventMouseButton
	if mouse_button == null or mouse_button.button_index != MOUSE_BUTTON_LEFT or mouse_button.pressed:
		return
	var was_drag_active := stamp_drag_active
	if stamp_drag_active and stamp_drag_target_layer >= 0:
		_swap_or_move_stamp_layer(stamp_drag_source_layer, stamp_drag_target_layer)
		suppress_next_stamp_press = true
	_clear_stamp_drag()
	_refresh_stamp_controls()
	if was_drag_active:
		call_deferred("_clear_stamp_press_suppression")
	get_viewport().set_input_as_handled()

func _load_car_defs() -> void:
	if game_manager != null:
		car_defs = game_manager.car_definitions
	else:
		car_defs = []
	vehicle_selector.clear()
	for def in car_defs:
		vehicle_selector.add_item(def.name)

func _load_settings() -> void:
	var path := "user://player_settings.json"
	if FileAccess.file_exists(path):
		var data = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(data) == TYPE_DICTIONARY:
			player_settings.from_dict(data)

func _save_settings() -> void:
	if livery_dirty and !_livery_editing_locked():
		_save_livery_for_selected_car(false)
	_sync_livery_to_player_settings()
	var path := "user://player_settings.json"
	var file = FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(player_settings.to_dict()))
	file.close()
	if game_manager:
		var settings_dict := player_settings.to_dict()
		if _network_settings_should_exclude_livery():
			settings_dict.erase("car_livery")
		game_manager.network_manager.send_player_settings(settings_dict)

func _populate_sticker_selectors() -> void:
	for selector in sticker_selectors:
		selector.clear()
		selector.expand_icon = true
		if sticker_selection == null:
			continue
		for i in range(sticker_selection.stickers.size()):
			selector.add_icon_item(sticker_selection.stickers[i], "", i)
			selector.get_popup().set_item_icon_max_width(i, 96)

func _update_controls(rebuild_preview := true) -> void:
	machine_setting_slider.value = player_settings.accel_setting * 100.0
	machine_setting_percent.text = str(roundi(machine_setting_slider.value)) + "%"
	pilot_name_input.text = player_settings.username
	spectator_toggle.button_pressed = player_settings.spectator
	_update_sticker_controls()
	var idx := 0
	for i in car_defs.size():
		if car_defs[i].resource_path == player_settings.car_definition_path:
			idx = i
			break
	if car_defs.size() > 0:
		vehicle_selector.select(idx)
		player_settings.car_definition_path = car_defs[idx].resource_path
		car_name_label.text = car_defs[idx].name
		if garage_car_name_label != null:
			garage_car_name_label.text = car_defs[idx].name
		_load_livery_for_selected_car()
		_update_livery_controls()
		_refresh_stamp_controls()
		if rebuild_preview:
			_rebuild_preview_vehicle()
	_update_livery_lock_state()

func refresh_after_game_manager_loaded() -> void:
	_load_settings()
	_load_car_defs()
	_refresh_custom_stamp_library()
	_update_controls(false)
	_sync_livery_to_player_settings()

func _on_slider_changed(value: float) -> void:
	machine_setting_percent.text = str(roundi(value)) + "%"
	player_settings.accel_setting = value / 100.0

func _on_vehicle_selected(index: int) -> void:
	if index >= 0 and index < car_defs.size():
		player_settings.car_definition_path = car_defs[index].resource_path
		car_name_label.text = car_defs[index].name
		if garage_car_name_label != null:
			garage_car_name_label.text = car_defs[index].name
		_load_livery_for_selected_car()
		_update_livery_controls()
		_refresh_stamp_controls()
		_rebuild_preview_vehicle()
		_send_online_vehicle_selection_update()

func _on_name_changed(new_text: String) -> void:
	player_settings.username = new_text

func _on_spectator_toggled(toggled: bool) -> void:
	player_settings.spectator = toggled

func _sticker_slot_value(slot: int) -> int:
	match slot:
		0:
			return player_settings.sticker_1
		1:
			return player_settings.sticker_2
		2:
			return player_settings.sticker_3
		3:
			return player_settings.sticker_4
	return 0

func _set_sticker_slot_value(slot: int, value: int) -> void:
	match slot:
		0:
			player_settings.sticker_1 = value
		1:
			player_settings.sticker_2 = value
		2:
			player_settings.sticker_3 = value
		3:
			player_settings.sticker_4 = value

func _update_sticker_controls() -> void:
	var count := 0
	if sticker_selection != null:
		count = sticker_selection.stickers.size()
	for i in range(sticker_selectors.size()):
		if count > 0:
			var sticker_index := wrapi(_sticker_slot_value(i), 0, count)
			sticker_selectors[i].select(sticker_index)
			sticker_selectors[i].icon = sticker_selection.stickers[sticker_index]

func _on_sticker_selected(index: int, slot: int) -> void:
	_set_sticker_slot_value(slot, index)

func _on_close_pressed() -> void:
	_save_settings()
	_set_garage_preview_active(false)
	hide()

func open_settings() -> void:
	_load_settings()
	_load_car_defs()
	_refresh_custom_stamp_library()
	_update_controls()
	_update_livery_lock_state()
	show()
	_set_garage_preview_active(true)

func get_player_settings() -> PlayerSettings:
	_sync_livery_to_player_settings()
	return player_settings

func _load_livery_for_selected_car() -> void:
	if player_settings.car_definition_path == "":
		current_livery = CarLivery.new()
		current_livery_enabled = false
		player_settings.car_livery = {}
		return
	current_livery_enabled = CarLiveryStore.has_for_car(player_settings.car_definition_path)
	var livery: CarLivery = CarLiveryStore.load_for_car(player_settings.car_definition_path)
	livery.car_definition_path = player_settings.car_definition_path
	current_livery = livery
	_sync_livery_to_player_settings()

func _save_livery_for_selected_car(rebuild_preview := true) -> void:
	if _livery_editing_locked():
		return
	if player_settings.car_definition_path == "":
		player_settings.car_livery = {}
		return
	current_livery_enabled = true
	current_livery.car_definition_path = player_settings.car_definition_path
	var err := CarLiveryStore.save_for_car(current_livery)
	if err != OK:
		push_warning("Failed to save car livery: %s" % err)
	_sync_livery_to_player_settings()
	livery_dirty = false
	if rebuild_preview:
		_rebuild_preview_vehicle()

func _mark_livery_dirty() -> void:
	if _livery_editing_locked():
		return
	livery_dirty = true
	current_livery_enabled = true
	_sync_livery_to_player_settings()

func _livery_editing_locked() -> bool:
	return game_manager != null and !game_manager.singleplayer_mode and game_manager.network_manager != null and game_manager.network_manager.has_network_peer()

func _network_settings_should_exclude_livery() -> bool:
	return game_manager != null and !game_manager.singleplayer_mode and game_manager.network_manager != null and game_manager.network_manager.has_network_peer()

func _send_online_vehicle_selection_update() -> void:
	if !_network_settings_should_exclude_livery():
		return
	var settings_dict := player_settings.to_dict()
	game_manager.network_manager.send_player_settings(settings_dict)
	game_manager.network_manager.custom_stamp_network.send_active_custom_stamp_manifest()

func _update_livery_lock_state() -> void:
	var locked := _livery_editing_locked()
	primary_colour_picker.disabled = locked
	secondary_colour_picker.disabled = locked
	accent_colour_picker.disabled = locked
	if garage_panel != null:
		garage_panel.modulate = Color(0.55, 0.55, 0.55, 1.0) if locked else Color.WHITE
	if custom_stamp_catalog_tab != null:
		custom_stamp_catalog_tab.modulate = Color(0.55, 0.55, 0.55, 1.0) if locked else Color.WHITE
	if custom_stamp_catalog_import_button != null:
		custom_stamp_catalog_import_button.disabled = locked
	if custom_stamp_catalog_palette_option != null:
		custom_stamp_catalog_palette_option.disabled = locked
	if custom_stamp_catalog_paint_button != null:
		custom_stamp_catalog_paint_button.disabled = locked
	if custom_stamp_painter_save_button != null:
		custom_stamp_painter_save_button.disabled = locked
	for button in stamp_layer_buttons:
		button.disabled = locked
	for layer in range(stamp_layer_colour_pickers.size()):
		var colour_picker := stamp_layer_colour_pickers[layer]
		if colour_picker != null:
			colour_picker.disabled = locked or _stamp_for_layer(layer) == null

func _process(_delta: float) -> void:
	if visible:
		_poll_preview_custom_stamp_atlas_thread()
		_update_livery_lock_state()

func _notification(what: int) -> void:
	if what == NOTIFICATION_VISIBILITY_CHANGED:
		_set_garage_preview_active(visible)

func _exit_tree() -> void:
	_finish_preview_custom_stamp_atlas_thread()

func _sync_livery_to_player_settings() -> void:
	if player_settings.car_definition_path == "" or !current_livery_enabled:
		player_settings.car_livery = {}
		return
	current_livery.car_definition_path = player_settings.car_definition_path
	player_settings.set_car_livery(current_livery)

func _update_livery_controls() -> void:
	updating_colour_controls = true
	primary_colour_picker.color = current_livery.primary_colour
	secondary_colour_picker.color = current_livery.secondary_colour
	accent_colour_picker.color = current_livery.accent_colour
	updating_colour_controls = false

func _on_primary_colour_changed(colour: Color) -> void:
	if updating_colour_controls or _livery_editing_locked():
		return
	current_livery.primary_colour = colour
	_apply_preview_livery_colours()
	_save_livery_for_selected_car(false)

func _on_secondary_colour_changed(colour: Color) -> void:
	if updating_colour_controls or _livery_editing_locked():
		return
	current_livery.secondary_colour = colour
	_apply_preview_livery_colours()
	_save_livery_for_selected_car(false)

func _on_accent_colour_changed(colour: Color) -> void:
	if updating_colour_controls or _livery_editing_locked():
		return
	current_livery.accent_colour = colour
	_apply_preview_livery_colours()
	_save_livery_for_selected_car(false)

func _apply_preview_livery_colours() -> void:
	for manager in [preview_render_manager, preview_edit_render_manager, preview_above_render_manager]:
		if manager != null and manager.has_method("update_livery_colours"):
			manager.update_livery_colours(current_livery)

func _setup_garage_preview() -> void:
	car_preview_space.mouse_filter = Control.MOUSE_FILTER_STOP
	preview_container = SubViewportContainer.new()
	preview_container.name = "GaragePreviewViewport"
	preview_container.stretch = true
	preview_container.mouse_filter = Control.MOUSE_FILTER_STOP
	preview_container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	car_preview_space.add_child(preview_container)
	car_preview_space.move_child(preview_container, 0)
	preview_container.gui_input.connect(_on_preview_gui_input)
	car_preview_space.resized.connect(_on_preview_resized)

	preview_viewport = SubViewport.new()
	preview_viewport.own_world_3d = true
	preview_viewport.transparent_bg = true
	preview_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	preview_viewport.size = Vector2i(maxi(1, int(car_preview_space.size.x)), maxi(1, int(car_preview_space.size.y)))
	preview_container.add_child(preview_viewport)

	preview_root = GARAGE_PREVIEW_WORLD_SCENE.instantiate()
	preview_root.name = "GaragePreviewWorld"
	preview_viewport.add_child(preview_root)

	preview_render_manager = CarRenderManager.new()
	preview_render_manager.name = "GaragePreviewRenderManager"
	preview_render_manager.stamp_render_priority = 2
	preview_root.add_child(preview_render_manager)

	preview_edit_render_manager = CarRenderManager.new()
	preview_edit_render_manager.name = "GaragePreviewEditRenderManager"
	preview_edit_render_manager.stamp_only_mode = true
	preview_edit_render_manager.stamp_render_priority = 3
	preview_root.add_child(preview_edit_render_manager)

	preview_above_render_manager = CarRenderManager.new()
	preview_above_render_manager.name = "GaragePreviewAboveRenderManager"
	preview_above_render_manager.stamp_only_mode = true
	preview_above_render_manager.stamp_render_priority = 4
	preview_root.add_child(preview_above_render_manager)

	preview_camera = Camera3D.new()
	preview_camera.current = true
	preview_viewport.add_child(preview_camera)
	preview_camera.fov = 38.0
	_setup_stamp_edit_overlay()
	_apply_preview_camera()
	_set_garage_preview_active(visible)

func _set_garage_preview_active(active: bool) -> void:
	if preview_viewport == null:
		return
	preview_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS if active else SubViewport.UPDATE_DISABLED

func _setup_stamp_edit_overlay() -> void:
	stamp_edit_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	stamp_edit_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	stamp_edit_overlay.visible = false
	stamp_edit_square.custom_minimum_size = Vector2.ZERO
	stamp_edit_square.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for kind in ["edge_left", "edge_right", "edge_top", "edge_bottom", "corner_tl", "corner_tr", "corner_bl", "corner_br"]:
		var handle := stamp_edit_square.get_node_or_null(kind) as Control
		if handle == null:
			continue
		handle.mouse_filter = Control.MOUSE_FILTER_STOP
		handle.gui_input.connect(_on_stamp_edit_handle_input.bind(kind))
	stamp_edit_confirm_button.pressed.connect(_on_stamp_edit_confirm_pressed)
	stamp_edit_cancel_button.pressed.connect(_on_stamp_edit_cancel_pressed)
	custom_stamp_import_dialog.file_selected.connect(_on_custom_stamp_import_file_selected)
	stamp_edit_overlay.gui_input.connect(_on_stamp_edit_overlay_gui_input)
	stamp_edit_overlay.resized.connect(_layout_stamp_edit_overlay)

func _on_preview_resized() -> void:
	if preview_viewport == null:
		return
	if preview_container != null and preview_container.stretch:
		return
	preview_viewport.size = Vector2i(maxi(1, int(car_preview_space.size.x)), maxi(1, int(car_preview_space.size.y)))

func _rebuild_preview_vehicle() -> void:
	if preview_root == null:
		return
	if preview_vehicle != null and is_instance_valid(preview_vehicle):
		preview_vehicle.queue_free()
	preview_vehicle = null
	if preview_render_manager != null:
		preview_render_manager.clear_renderer()
	if preview_edit_render_manager != null:
		preview_edit_render_manager.clear_renderer()
	if preview_above_render_manager != null:
		preview_above_render_manager.clear_renderer()
	var definition := _selected_car_definition()
	if definition == null or definition.car_scene == null:
		return
	preview_vehicle = definition.car_scene.instantiate()
	preview_vehicle_base_transform = preview_vehicle.transform
	preview_root.add_child(preview_vehicle)
	_hide_preview_raycast_scene(preview_vehicle)
	_refresh_preview_custom_stamp_atlas()
	var render_settings: Array = [_preview_render_settings(_preview_confirmed_livery_below())]
	preview_render_manager.stamp_visibility_masks_enabled = true
	preview_render_manager.stamp_visibility_mask_skip_layer = -1
	preview_render_manager.stamp_only_mode = false
	preview_render_manager.stamp_render_priority = 2
	preview_render_manager.set_custom_stamp_atlas(preview_custom_stamp_atlas)
	preview_render_manager.configure_manual([definition], render_settings)
	if stamp_ui_mode == StampUiMode.EDITING:
		_rebuild_edit_stamp_preview(false)
		_rebuild_above_stamp_preview(false)
	_apply_preview_camera()

func _rebuild_edit_stamp_preview(apply_camera := true) -> void:
	if preview_edit_render_manager == null:
		return
	preview_edit_render_manager.clear_renderer()
	var definition := _selected_car_definition()
	if definition == null or definition.car_scene == null or stamp_ui_mode != StampUiMode.EDITING:
		if apply_camera:
			_apply_preview_camera()
		return
	preview_edit_render_manager.stamp_visibility_masks_enabled = false
	preview_edit_render_manager.stamp_visibility_mask_skip_layer = -1
	preview_edit_render_manager.stamp_only_mode = true
	preview_edit_render_manager.stamp_render_priority = 3
	preview_edit_render_manager.set_custom_stamp_atlas(preview_custom_stamp_atlas)
	preview_edit_render_manager.configure_manual([definition], [_preview_render_settings(_preview_edit_livery())])
	if apply_camera:
		_apply_preview_camera()

func _rebuild_above_stamp_preview(apply_camera := true) -> void:
	if preview_above_render_manager == null:
		return
	preview_above_render_manager.clear_renderer()
	var definition := _selected_car_definition()
	if definition == null or definition.car_scene == null or stamp_ui_mode != StampUiMode.EDITING:
		if apply_camera:
			_apply_preview_camera()
		return
	preview_above_render_manager.stamp_visibility_masks_enabled = true
	preview_above_render_manager.stamp_visibility_mask_skip_layer = -1
	preview_above_render_manager.stamp_only_mode = true
	preview_above_render_manager.stamp_render_priority = 4
	preview_above_render_manager.set_custom_stamp_atlas(preview_custom_stamp_atlas)
	preview_above_render_manager.configure_manual([definition], [_preview_render_settings(_preview_confirmed_livery_above())])
	if apply_camera:
		_apply_preview_camera()

func _hide_preview_raycast_scene(root: Node) -> void:
	for child in root.get_children():
		_hide_preview_raycast_scene(child)
	var mesh := root as MeshInstance3D
	if mesh == null:
		return
	mesh.visible = false

func _preview_vehicle_transform() -> Transform3D:
	if preview_has_transform_override:
		return preview_transform_override
	return Transform3D.IDENTITY

func _preview_vehicle_scene_transform() -> Transform3D:
	return _preview_vehicle_transform() * preview_vehicle_base_transform

func _submit_preview_render() -> void:
	for manager in [preview_render_manager, preview_edit_render_manager, preview_above_render_manager]:
		if manager == null or manager.archetypes.is_empty():
			continue
		manager.begin_manual_submit()
		manager.submit_manual_car(0, _preview_vehicle_transform(), Color.BLACK, Vector3.ZERO, Color.BLACK, 0.0, false)

func _preview_render_settings(livery: CarLivery) -> Dictionary:
	var settings := player_settings.to_dict()
	if livery == null:
		settings.erase("car_livery")
	else:
		livery.car_definition_path = player_settings.car_definition_path
		settings["car_livery"] = livery.to_dict()
	return settings

func _refresh_preview_custom_stamp_atlas() -> bool:
	preview_custom_stamp_atlas = null
	preview_custom_stamp_region_texture = null
	preview_custom_stamp_atlas_latest_revision += 1
	var atlas_revision := preview_custom_stamp_atlas_latest_revision
	_apply_preview_custom_stamp_atlas_texture()
	if current_livery == null:
		_update_custom_stamp_budget_overlay()
		return true
	var payload := CustomStampStore.build_livery_payload(current_livery)
	if !bool(payload.get("ok", false)):
		push_warning("Custom stamps do not fit the local vehicle atlas: %s" % str(payload.get("error", "unknown error")))
		_update_custom_stamp_budget_overlay(payload, false)
		return false
	var placements: Dictionary = payload.get("placements", {})
	_update_custom_stamp_budget_overlay(payload, true)
	if placements.is_empty():
		return true
	CustomStampPacker.apply_placements_to_livery(current_livery, placements, Vector2i.ZERO, CustomStampAtlasBuilder.ATLAS_SIZE)
	_request_preview_custom_stamp_atlas_build([{
		"player_id": 0,
		"region_origin": Vector2i.ZERO,
		"placements": placements,
		"blobs": payload.get("blobs", []),
	}], atlas_revision)
	return true

func _request_preview_custom_stamp_atlas_build(player_records: Array, revision: int) -> void:
	if preview_custom_stamp_atlas_thread != null and preview_custom_stamp_atlas_thread.is_alive():
		preview_custom_stamp_atlas_queued_records = player_records.duplicate(true)
		preview_custom_stamp_atlas_has_queued = true
		return
	_start_preview_custom_stamp_atlas_thread(player_records, revision)

func _start_preview_custom_stamp_atlas_thread(player_records: Array, revision: int) -> void:
	if preview_custom_stamp_atlas_thread != null:
		_poll_preview_custom_stamp_atlas_thread(true)
	preview_custom_stamp_atlas_active_revision = revision
	preview_custom_stamp_atlas_thread = Thread.new()
	var err := preview_custom_stamp_atlas_thread.start(_build_preview_custom_stamp_atlas_image_thread.bind(player_records.duplicate(true), revision))
	if err != OK:
		preview_custom_stamp_atlas_thread = null
		push_warning("Failed to start custom stamp atlas thread: %s" % err)
		var atlas_build := CustomStampAtlasBuilder.build_atlas_image(player_records)
		_apply_preview_custom_stamp_atlas_build_result(atlas_build, revision)

func _build_preview_custom_stamp_atlas_image_thread(player_records: Array, revision: int) -> Dictionary:
	var result := CustomStampAtlasBuilder.build_atlas_image(player_records)
	result["revision"] = revision
	return result

func _poll_preview_custom_stamp_atlas_thread(force_wait := false) -> void:
	if preview_custom_stamp_atlas_thread == null:
		return
	if !force_wait and preview_custom_stamp_atlas_thread.is_alive():
		return
	var result = preview_custom_stamp_atlas_thread.wait_to_finish()
	preview_custom_stamp_atlas_thread = null
	if typeof(result) == TYPE_DICTIONARY:
		_apply_preview_custom_stamp_atlas_build_result(result, int(result.get("revision", preview_custom_stamp_atlas_active_revision)))
	if preview_custom_stamp_atlas_has_queued:
		var queued := preview_custom_stamp_atlas_queued_records
		preview_custom_stamp_atlas_queued_records = []
		preview_custom_stamp_atlas_has_queued = false
		_start_preview_custom_stamp_atlas_thread(queued, preview_custom_stamp_atlas_latest_revision)

func _finish_preview_custom_stamp_atlas_thread() -> void:
	preview_custom_stamp_atlas_has_queued = false
	preview_custom_stamp_atlas_queued_records = []
	_poll_preview_custom_stamp_atlas_thread(true)

func _apply_preview_custom_stamp_atlas_build_result(atlas_build: Dictionary, revision: int) -> void:
	if revision != preview_custom_stamp_atlas_latest_revision:
		return
	if !bool(atlas_build.get("ok", false)):
		push_warning("Failed to build garage custom stamp atlas: %s" % str(atlas_build.get("error", "unknown error")))
		return
	var image := atlas_build.get("image", null) as Image
	preview_custom_stamp_atlas = CustomStampAtlasBuilder.texture_from_image(image)
	_apply_preview_custom_stamp_atlas_texture()

func _apply_preview_custom_stamp_atlas_texture() -> void:
	for manager in [preview_render_manager, preview_edit_render_manager, preview_above_render_manager]:
		if manager != null:
			manager.set_custom_stamp_atlas(preview_custom_stamp_atlas)

func _update_custom_stamp_budget_overlay(payload: Dictionary = {}, ok := true) -> void:
	if custom_stamp_budget_label == null or custom_stamp_atlas_preview == null:
		return
	var blobs: Array = payload.get("blobs", [])
	var pixel_count := 0
	var uncompressed_size := 0
	var compressed_size := 0
	for blob in blobs:
		var stamp_blob := blob as CustomStampBlob
		if stamp_blob == null:
			continue
		pixel_count += stamp_blob.width * stamp_blob.height
		uncompressed_size += stamp_blob.uncompressed_size
		compressed_size += stamp_blob.compressed_indices.size()
	var pixel_kib := float(pixel_count) / 1024.0
	var raw_kib := float(uncompressed_size) / 1024.0
	var compressed_kib := float(compressed_size) / 1024.0
	var text := "Custom stamps"
	if !ok:
		text += " OVER BUDGET"
	text += "\nUncompressed: %.1f / %.1f KiB" % [raw_kib, float(CustomStampBlob.PLAYER_INDEXED_PIXEL_BUDGET) / 1024.0]
	text += "\nPixels: %.1f / %.1f KiB" % [pixel_kib, float(CustomStampBlob.PLAYER_INDEXED_PIXEL_BUDGET) / 1024.0]
	text += "\nCompressed: %.1f / %.1f KiB" % [compressed_kib, float(CustomStampBlob.COMPRESSED_BYTE_CAP) / 1024.0]
	custom_stamp_budget_label.text = text
	custom_stamp_budget_label.modulate = Color(1.0, 0.35, 0.25, 1.0) if !ok else Color.WHITE
	preview_custom_stamp_region_texture = _build_custom_stamp_region_preview_texture(payload)
	custom_stamp_atlas_preview.texture = preview_custom_stamp_region_texture
	custom_stamp_atlas_preview.visible = preview_custom_stamp_region_texture != null
	if preview_custom_stamp_region_texture != null:
		var texture_size := preview_custom_stamp_region_texture.get_size()
		custom_stamp_atlas_preview.custom_minimum_size = texture_size
		custom_stamp_atlas_preview.size = texture_size
		custom_stamp_atlas_preview.offset_left = 10.0
		custom_stamp_atlas_preview.offset_top = -texture_size.y - 10.0
		custom_stamp_atlas_preview.offset_right = texture_size.x + 10.0
		custom_stamp_atlas_preview.offset_bottom = -10.0

func _build_custom_stamp_region_preview_texture(payload: Dictionary) -> Texture2D:
	var placements: Dictionary = payload.get("placements", {})
	if placements.is_empty():
		return null
	var region_size := Vector2i.ZERO
	for placement_value in placements.values():
		if typeof(placement_value) == TYPE_DICTIONARY:
			var placement: Dictionary = placement_value
			region_size = placement.get("region_size", Vector2i.ZERO)
			break
	if region_size == Vector2i.ZERO:
		return null
	var image := Image.create(region_size.x, region_size.y, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.02, 0.02, 0.025, 0.72))
	for blob in payload.get("blobs", []):
		var stamp_blob := blob as CustomStampBlob
		if stamp_blob == null or !placements.has(stamp_blob.stamp_hash):
			continue
		var stamp_image := CustomStampStore.create_preview_image(stamp_blob)
		if stamp_image == null:
			continue
		var placement: Dictionary = placements[stamp_blob.stamp_hash]
		var rect: Rect2i = placement["rect"]
		var rotated := bool(placement.get("rotated", false))
		for y in range(stamp_blob.height):
			for x in range(stamp_blob.width):
				var colour := stamp_image.get_pixel(x, y)
				if colour.a <= 0.0:
					continue
				var dest := Vector2i(rect.position.x + x, rect.position.y + y)
				if rotated:
					dest = Vector2i(rect.position.x + y, rect.position.y + stamp_blob.width - 1 - x)
				if dest.x >= 0 and dest.y >= 0 and dest.x < region_size.x and dest.y < region_size.y:
					image.set_pixel(dest.x, dest.y, colour)
	return ImageTexture.create_from_image(image)

func _preview_confirmed_livery_below() -> CarLivery:
	if !current_livery_enabled:
		return null
	if stamp_ui_mode == StampUiMode.EDITING:
		return _livery_copy_layer_range(-INF, float(editing_stamp_layer))
	return current_livery

func _preview_confirmed_livery_above() -> CarLivery:
	if !current_livery_enabled or stamp_ui_mode != StampUiMode.EDITING:
		return null
	return _livery_copy_layer_range(float(editing_stamp_layer + 1), INF)

func _preview_edit_livery() -> CarLivery:
	if stamp_ui_mode != StampUiMode.EDITING:
		return null
	return _livery_copy_only_layer(editing_stamp_layer)

func _livery_copy_layer_range(min_layer: float, max_layer: float) -> CarLivery:
	var out := _livery_copy_base()
	for stamp in current_livery.get_sorted_stamps():
		if stamp == null or float(stamp.layer) < min_layer or float(stamp.layer) >= max_layer:
			continue
		out.stamps.append(stamp.duplicate_stamp())
	return out

func _livery_copy_only_layer(layer: int) -> CarLivery:
	var out := _livery_copy_base()
	var stamp := _stamp_for_layer(layer)
	if stamp != null:
		out.stamps.append(stamp.duplicate_stamp())
	return out

func _livery_copy_base() -> CarLivery:
	var out := CarLivery.new()
	out.car_definition_path = player_settings.car_definition_path
	out.primary_colour = current_livery.primary_colour
	out.secondary_colour = current_livery.secondary_colour
	out.accent_colour = current_livery.accent_colour
	return out

func _selected_car_definition() -> CarDefinition:
	for def in car_defs:
		if def.resource_path == player_settings.car_definition_path:
			return def
	return car_defs[0] if !car_defs.is_empty() else null

func _refresh_stamp_controls() -> void:
	updating_stamp_controls = true
	var locked := _livery_editing_locked()
	for layer in range(stamp_layer_buttons.size()):
		var row: Control = stamp_layer_rows[layer] if layer < stamp_layer_rows.size() else null
		var button := stamp_layer_buttons[layer]
		var colour_picker: ColorPickerButton = stamp_layer_colour_pickers[layer] if layer < stamp_layer_colour_pickers.size() else null
		var stamp := _stamp_for_layer(layer)
		if row != null:
			row.modulate = Color(1.0, 0.92, 0.25, 1.0) if stamp_drag_active and layer == stamp_drag_target_layer else Color.WHITE
		button.modulate = Color.WHITE
		button.disabled = locked
		button.expand_icon = true
		if stamp == null:
			button.text = "EMPTY"
			button.icon = null
			if colour_picker != null:
				colour_picker.color = Color(0.45, 0.45, 0.45, 1.0)
				colour_picker.disabled = true
		else:
			var label := _stamp_display_name(stamp)
			button.text = label
			button.icon = _stamp_preview_texture(stamp)
			if colour_picker != null:
				colour_picker.disabled = locked
				colour_picker.color = stamp.colour
	updating_stamp_controls = false

func _stamp_preview_texture(stamp: CarLiveryStamp) -> Texture2D:
	if stamp == null:
		return null
	if stamp.is_custom():
		var blob := _custom_stamp_blob_for_hash(stamp.custom_hash)
		return _custom_stamp_preview_texture(blob) if blob != null else null
	if stamp_catalog == null:
		return null
	return stamp_catalog.get_preview_texture(stamp.stamp_id)

func _stamp_display_name(stamp: CarLiveryStamp) -> String:
	if stamp == null:
		return ""
	if stamp.is_custom():
		var key := stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		return "Custom %s" % key.substr(0, 8)
	if stamp_catalog != null:
		var entry := stamp_catalog.get_entry(stamp.stamp_id)
		if entry != null and entry.display_name != "":
			return entry.display_name
	return stamp.stamp_id

func _on_stamp_colour_changed(colour: Color, layer: int) -> void:
	if updating_stamp_controls or _livery_editing_locked():
		return
	var stamp := _stamp_for_layer(layer)
	if stamp == null:
		return
	stamp.colour = colour
	_mark_livery_dirty()
	if preview_render_manager != null:
		preview_render_manager.update_stamp_layer_colour(layer, Color(colour.r, colour.g, colour.b, colour.a * stamp.opacity))
	if preview_edit_render_manager != null:
		preview_edit_render_manager.update_stamp_layer_colour(layer, Color(colour.r, colour.g, colour.b, colour.a * stamp.opacity))
	if preview_above_render_manager != null:
		preview_above_render_manager.update_stamp_layer_colour(layer, Color(colour.r, colour.g, colour.b, colour.a * stamp.opacity))

func _on_stamp_colour_popup_closed(layer: int) -> void:
	if updating_stamp_controls or _livery_editing_locked():
		return
	var stamp := _stamp_for_layer(layer)
	if stamp == null:
		return
	_save_livery_for_selected_car(false)
	_refresh_stamp_controls()

func _selected_stamp() -> CarLiveryStamp:
	return _stamp_for_layer(selected_stamp_index)

func _stamp_for_layer(layer: int) -> CarLiveryStamp:
	for stamp in current_livery.stamps:
		if stamp != null and stamp.layer == layer:
			return stamp
	return null

func _on_stamp_layer_gui_input(event: InputEvent, layer: int) -> void:
	if updating_stamp_controls or _livery_editing_locked() or stamp_ui_mode != StampUiMode.IDLE:
		return
	var mouse_button := event as InputEventMouseButton
	if mouse_button != null and mouse_button.button_index == MOUSE_BUTTON_RIGHT and mouse_button.pressed:
		selected_stamp_index = layer
		if _stamp_for_layer(layer) == null:
			_show_stamp_chooser(layer, "add")
		else:
			_show_stamp_action_menu_at(layer, get_global_mouse_position())
		accept_event()
		return
	if mouse_button != null and mouse_button.button_index == MOUSE_BUTTON_LEFT:
		if mouse_button.pressed:
			if _stamp_for_layer(layer) == null:
				return
			stamp_drag_source_layer = layer
			stamp_drag_target_layer = layer
			stamp_drag_start_position = get_global_mouse_position()
			stamp_drag_active = false
		elif stamp_drag_source_layer >= 0:
			var was_drag_active := stamp_drag_active
			if stamp_drag_active and stamp_drag_target_layer >= 0:
				_swap_or_move_stamp_layer(stamp_drag_source_layer, stamp_drag_target_layer)
				suppress_next_stamp_press = true
			_clear_stamp_drag()
			_refresh_stamp_controls()
			if was_drag_active:
				call_deferred("_clear_stamp_press_suppression")
			accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion == null or stamp_drag_source_layer < 0:
		return
	_update_stamp_drag_target()
	accept_event()

func _update_stamp_drag_target() -> void:
	if stamp_drag_source_layer < 0:
		return
	if !stamp_drag_active and get_global_mouse_position().distance_to(stamp_drag_start_position) < 6.0:
		return
	if !stamp_drag_active:
		stamp_drag_active = true
		suppress_next_stamp_press = true
	var target := _stamp_layer_at_global_position(get_global_mouse_position())
	if target != stamp_drag_target_layer:
		stamp_drag_target_layer = target
		_refresh_stamp_controls()

func _stamp_layer_at_global_position(global_position: Vector2) -> int:
	for layer in range(stamp_layer_rows.size()):
		var row := stamp_layer_rows[layer]
		if row != null and row.get_global_rect().has_point(global_position):
			return layer
	return -1

func _swap_or_move_stamp_layer(source_layer: int, target_layer: int) -> void:
	if source_layer == target_layer:
		return
	var source_stamp := _stamp_for_layer(source_layer)
	if source_stamp == null:
		return
	var target_stamp := _stamp_for_layer(target_layer)
	source_stamp.layer = target_layer
	if target_stamp != null:
		target_stamp.layer = source_layer
	current_livery._sort_stamps_in_place()
	selected_stamp_index = target_layer
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _clear_stamp_drag() -> void:
	stamp_drag_source_layer = -1
	stamp_drag_target_layer = -1
	stamp_drag_active = false

func _clear_stamp_press_suppression() -> void:
	suppress_next_stamp_press = false

func _remove_stamp_layer(layer: int) -> void:
	for i in range(current_livery.stamps.size() - 1, -1, -1):
		var stamp := current_livery.stamps[i]
		if stamp != null and stamp.layer == layer:
			current_livery.stamps.remove_at(i)

func _set_stamp_for_layer(layer: int, stamp: CarLiveryStamp) -> void:
	_remove_stamp_layer(layer)
	if stamp == null:
		return
	stamp.layer = layer
	current_livery.add_stamp(stamp)

func _try_set_stamp_for_layer_with_custom_pack(layer: int, stamp: CarLiveryStamp) -> bool:
	var old_stamp := _stamp_for_layer(layer)
	var old_copy: CarLiveryStamp = old_stamp.duplicate_stamp() if old_stamp != null else null
	_set_stamp_for_layer(layer, stamp)
	if _refresh_preview_custom_stamp_atlas():
		return true
	_set_stamp_for_layer(layer, old_copy)
	_refresh_preview_custom_stamp_atlas()
	return false

func _new_stamp(layer: int, stamp_id: String) -> CarLiveryStamp:
	var stamp := CarLiveryStamp.new()
	stamp.stamp_id = stamp_id
	stamp.layer = layer
	stamp.size = Vector2.ONE
	stamp.projection_depth = 1.5
	stamp.colour = Color.WHITE
	stamp.local_origin = Vector3.ZERO
	stamp.local_basis = Basis.IDENTITY
	stamp.rotation = 0.0
	return stamp

func _new_custom_stamp(layer: int, blob: CustomStampBlob) -> CarLiveryStamp:
	var stamp := _new_stamp(layer, "custom_%s" % blob.stamp_hash.substr(0, 8))
	_set_stamp_asset_to_custom(stamp, blob)
	return stamp

func _set_stamp_asset_to_base(stamp: CarLiveryStamp, stamp_id: String) -> void:
	stamp.stamp_id = stamp_id
	stamp.source = CarLiveryStamp.SOURCE_BASE
	stamp.custom_hash = ""
	stamp.palette_id = 0
	stamp.custom_rect = Rect2()
	stamp.custom_rect_rotated = false

func _set_stamp_asset_to_custom(stamp: CarLiveryStamp, blob: CustomStampBlob) -> void:
	stamp.source = CarLiveryStamp.SOURCE_CUSTOM
	stamp.custom_hash = blob.stamp_hash
	stamp.stamp_id = "custom_%s" % blob.stamp_hash.substr(0, 8)
	stamp.palette_id = blob.palette_id

func _first_stamp_id() -> String:
	if stamp_catalog != null:
		for entry in stamp_catalog.entries:
			if entry != null and entry.is_valid_entry():
				return entry.stamp_id
	return "circle"

func _on_stamp_layer_pressed(layer: int) -> void:
	if suppress_next_stamp_press:
		suppress_next_stamp_press = false
		return
	if updating_stamp_controls or _livery_editing_locked():
		return
	selected_stamp_index = layer
	var stamp := _stamp_for_layer(layer)
	if stamp == null:
		_show_stamp_chooser(layer, "add")
		return
	_show_stamp_action_menu(layer)

func _show_stamp_action_menu(layer: int) -> void:
	if stamp_action_menu == null:
		return
	pending_stamp_layer = layer
	var button := stamp_layer_buttons[layer]
	var rect := button.get_global_rect()
	_show_stamp_action_menu_at(layer, rect.position + Vector2(24.0, rect.size.y))

func _show_stamp_action_menu_at(layer: int, popup_position: Vector2) -> void:
	if stamp_action_menu == null:
		return
	pending_stamp_layer = layer
	stamp_action_menu.position = Vector2i(popup_position)
	stamp_action_menu.popup()

func _on_stamp_action_selected(id: int) -> void:
	if pending_stamp_layer < 0 or _livery_editing_locked():
		return
	match id:
		0:
			_show_stamp_chooser(pending_stamp_layer, "change")
		1:
			var stamp := _stamp_for_layer(pending_stamp_layer)
			if stamp != null:
				_begin_stamp_edit(pending_stamp_layer, stamp, false)
		2:
			_remove_stamp_layer(pending_stamp_layer)
			_save_livery_for_selected_car()
			_refresh_stamp_controls()
		3:
			_show_stamp_properties(pending_stamp_layer)

func _show_stamp_properties(layer: int) -> void:
	var stamp := _stamp_for_layer(layer)
	if stamp == null or stamp_properties_popup == null:
		return
	stamp_properties_layer = layer
	updating_stamp_properties = true
	stamp_properties_rotation.value = rad_to_deg(stamp.rotation)
	stamp_properties_scale_x.value = stamp.size.x
	stamp_properties_scale_y.value = stamp.size.y
	stamp_properties_flip_horizontal.button_pressed = stamp.flip_horizontal
	stamp_properties_flip_vertical.button_pressed = stamp.flip_vertical
	stamp_properties_mirror_local_x.button_pressed = stamp.mirror_local_x
	updating_stamp_properties = false
	stamp_properties_popup.popup_centered(Vector2i(340, 292))

func _on_stamp_property_number_changed(_value: float) -> void:
	_apply_stamp_properties_from_popup()

func _on_stamp_property_toggled(_enabled: bool) -> void:
	_apply_stamp_properties_from_popup()

func _apply_stamp_properties_from_popup() -> void:
	if updating_stamp_properties or _livery_editing_locked():
		return
	var stamp := _stamp_for_layer(stamp_properties_layer)
	if stamp == null:
		return
	var new_rotation := deg_to_rad(float(stamp_properties_rotation.value))
	var rotation_delta := new_rotation - stamp.rotation
	if absf(rotation_delta) > 0.00001:
		var z_axis := stamp.local_basis.z.normalized()
		if z_axis.length_squared() > 0.00001:
			stamp.local_basis = Basis(z_axis, rotation_delta) * stamp.local_basis
		stamp.rotation = new_rotation
	stamp.size = Vector2(
		maxf(CarLiveryStamp.MIN_SIZE, float(stamp_properties_scale_x.value)),
		maxf(CarLiveryStamp.MIN_SIZE, float(stamp_properties_scale_y.value))
	)
	stamp.flip_horizontal = stamp_properties_flip_horizontal.button_pressed
	stamp.flip_vertical = stamp_properties_flip_vertical.button_pressed
	stamp.mirror_local_x = stamp_properties_mirror_local_x.button_pressed
	stamp.projection_depth = maxf(0.75, maxf(stamp.size.x, stamp.size.y) * 1.35)
	_mark_livery_dirty()
	_rebuild_preview_vehicle()
	_refresh_stamp_controls()

func _on_stamp_properties_popup_hidden() -> void:
	if stamp_properties_layer < 0:
		return
	if !_livery_editing_locked() and livery_dirty:
		_save_livery_for_selected_car()
	stamp_properties_layer = -1

func _show_stamp_chooser(layer: int, action: String) -> void:
	if stamp_chooser_popup == null or stamp_chooser_base_list == null or stamp_chooser_custom_list == null or _livery_editing_locked():
		return
	_refresh_custom_stamp_library()
	stamp_ui_mode = StampUiMode.CHOOSING
	pending_stamp_layer = layer
	pending_stamp_choice_action = action
	stamp_chooser_title.text = "Layer %02d" % [layer + 1]
	for child in stamp_chooser_base_list.get_children():
		child.queue_free()
	for child in stamp_chooser_custom_list.get_children():
		child.queue_free()
	if stamp_catalog != null:
		for entry in stamp_catalog.entries:
			if entry == null or !entry.is_valid_entry():
				continue
			var button := _make_stamp_choice_image_button(stamp_catalog.get_preview_texture(entry.stamp_id))
			button.pressed.connect(_on_stamp_choice_pressed.bind(entry.stamp_id))
			stamp_chooser_base_list.add_child(button)
	for blob in custom_stamp_blobs:
		var custom_blob := blob as CustomStampBlob
		if custom_blob == null:
			continue
		var button := _make_stamp_choice_image_button(_custom_stamp_preview_texture(custom_blob))
		button.pressed.connect(_on_custom_stamp_choice_pressed.bind(custom_blob.stamp_hash))
		stamp_chooser_custom_list.add_child(button)
	if stamp_chooser_custom_list.get_child_count() == 0:
		var empty_label := Label.new()
		empty_label.text = "No custom stamps in library."
		empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		stamp_chooser_custom_list.add_child(empty_label)
	stamp_chooser_tabs.current_tab = 0
	stamp_chooser_popup.popup_centered(Vector2i(380, 420))

func _make_stamp_choice_image_button(texture: Texture2D) -> Button:
	var button := Button.new()
	button.custom_minimum_size = Vector2(76.0, 76.0)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.text = ""
	button.icon = texture
	button.expand_icon = true
	return button

func _on_stamp_choice_pressed(stamp_id: String) -> void:
	stamp_chooser_popup.hide()
	if pending_stamp_layer < 0 or _livery_editing_locked():
		stamp_ui_mode = StampUiMode.IDLE
		return
	if pending_stamp_choice_action == "change":
		var existing := _stamp_for_layer(pending_stamp_layer)
		if existing != null:
			_set_stamp_asset_to_base(existing, stamp_id)
			_refresh_preview_custom_stamp_atlas()
			_save_livery_for_selected_car()
			_refresh_stamp_controls()
		stamp_ui_mode = StampUiMode.IDLE
		return
	var stamp := _new_stamp(pending_stamp_layer, stamp_id)
	_set_stamp_for_layer(pending_stamp_layer, stamp)
	_begin_stamp_edit(pending_stamp_layer, stamp, true)

func _on_custom_stamp_choice_pressed(stamp_hash: String) -> void:
	stamp_chooser_popup.hide()
	if pending_stamp_layer < 0 or _livery_editing_locked():
		stamp_ui_mode = StampUiMode.IDLE
		return
	var blob := _custom_stamp_blob_for_hash(stamp_hash)
	if blob == null:
		push_warning("Custom stamp blob is missing: %s" % stamp_hash)
		stamp_ui_mode = StampUiMode.IDLE
		return
	if pending_stamp_choice_action == "change":
		var existing := _stamp_for_layer(pending_stamp_layer)
		if existing != null:
			var changed: CarLiveryStamp = existing.duplicate_stamp()
			_set_stamp_asset_to_custom(changed, blob)
			if !_try_set_stamp_for_layer_with_custom_pack(pending_stamp_layer, changed):
				push_warning("Custom stamp does not fit this vehicle's custom atlas budget.")
				stamp_ui_mode = StampUiMode.IDLE
				return
			_save_livery_for_selected_car()
			_refresh_stamp_controls()
		stamp_ui_mode = StampUiMode.IDLE
		return
	var stamp := _new_custom_stamp(pending_stamp_layer, blob)
	if !_try_set_stamp_for_layer_with_custom_pack(pending_stamp_layer, stamp):
		push_warning("Custom stamp does not fit this vehicle's custom atlas budget.")
		stamp_ui_mode = StampUiMode.IDLE
		return
	_begin_stamp_edit(pending_stamp_layer, stamp, true)

func _on_custom_stamp_import_pressed() -> void:
	if _livery_editing_locked():
		return
	custom_stamp_import_dialog.popup_centered(Vector2i(720, 520))

func _on_custom_import_palette_selected(index: int, option: OptionButton) -> void:
	selected_custom_import_palette_id = option.get_item_id(index)

func _on_custom_stamp_paint_pressed() -> void:
	if _livery_editing_locked():
		return
	editing_catalog_stamp_hash = ""
	_custom_painter_reset(custom_painter_size)
	custom_stamp_painter_popup.popup_centered(Vector2i(580, 660))

func _on_custom_stamp_import_file_selected(path: String) -> void:
	if _livery_editing_locked():
		return
	var result := CustomStampStore.import_png(path, selected_custom_import_palette_id)
	if !bool(result.get("ok", false)):
		push_warning("Failed to import custom stamp: %s" % str(result.get("error", "unknown error")))
		return
	var blob := result.get("blob", null) as CustomStampBlob
	if blob == null:
		return
	_refresh_custom_stamp_library()

func _on_stamp_choice_cancel_pressed() -> void:
	stamp_chooser_popup.hide()
	stamp_ui_mode = StampUiMode.IDLE
	pending_stamp_layer = -1
	pending_stamp_choice_action = ""

func _refresh_custom_stamp_library() -> void:
	custom_stamp_blobs = CustomStampStore.list_local_blobs()
	_refresh_custom_stamp_catalog_grid()

func _refresh_custom_stamp_catalog_grid() -> void:
	if custom_stamp_library_grid == null:
		return
	for child in custom_stamp_library_grid.get_children():
		child.queue_free()
	if custom_stamp_blobs.is_empty():
		var empty_label := Label.new()
		empty_label.text = "No custom stamps imported."
		empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		empty_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		custom_stamp_library_grid.add_child(empty_label)
		return
	for blob in custom_stamp_blobs:
		var custom_blob := blob as CustomStampBlob
		if custom_blob == null:
			continue
		var tile := VBoxContainer.new()
		tile.custom_minimum_size = Vector2(168.0, 188.0)
		tile.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		tile.mouse_filter = Control.MOUSE_FILTER_STOP
		tile.gui_input.connect(_on_custom_stamp_catalog_tile_input.bind(custom_blob.stamp_hash))
		var preview := TextureRect.new()
		preview.custom_minimum_size = Vector2(160.0, 144.0)
		preview.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		preview.mouse_filter = Control.MOUSE_FILTER_IGNORE
		preview.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
		preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		preview.texture = _custom_stamp_preview_texture(custom_blob)
		tile.add_child(preview)
		var label := Label.new()
		label.text = _custom_stamp_button_text(custom_blob)
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		tile.add_child(label)
		custom_stamp_library_grid.add_child(tile)

func _on_custom_stamp_catalog_tile_input(event: InputEvent, stamp_hash: String) -> void:
	if _livery_editing_locked():
		return
	var mouse_button := event as InputEventMouseButton
	if mouse_button == null or !mouse_button.pressed or mouse_button.button_index != MOUSE_BUTTON_RIGHT:
		return
	selected_catalog_stamp_hash = stamp_hash
	var blob := _custom_stamp_blob_for_hash(stamp_hash)
	custom_stamp_catalog_menu.set_item_disabled(0, blob == null or blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE)
	custom_stamp_catalog_menu.position = Vector2i(get_global_mouse_position())
	custom_stamp_catalog_menu.popup()
	accept_event()

func _on_custom_stamp_catalog_action_selected(id: int) -> void:
	if selected_catalog_stamp_hash == "" or _livery_editing_locked():
		return
	match id:
		0:
			_edit_custom_stamp_from_catalog(selected_catalog_stamp_hash)
		1:
			_delete_custom_stamp_from_catalog(selected_catalog_stamp_hash)

func _edit_custom_stamp_from_catalog(stamp_hash: String) -> void:
	var blob := _custom_stamp_blob_for_hash(stamp_hash)
	if blob == null:
		return
	if blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE:
		push_warning("8bpp palette stamps cannot be edited in the painter yet.")
		return
	editing_catalog_stamp_hash = stamp_hash
	_custom_painter_load_blob(blob)
	custom_stamp_painter_popup.popup_centered(Vector2i(580, 660))

func _delete_custom_stamp_from_catalog(stamp_hash: String) -> void:
	var removed_from_current_livery := false
	for i in range(current_livery.stamps.size() - 1, -1, -1):
		var stamp := current_livery.stamps[i]
		if stamp != null and stamp.is_custom() and stamp.custom_hash == stamp_hash:
			current_livery.stamps.remove_at(i)
			removed_from_current_livery = true
	var err := CustomStampStore.delete_blob(stamp_hash)
	if err != OK:
		push_warning("Failed to delete custom stamp: %s" % err)
		return
	custom_stamp_preview_textures.erase(stamp_hash)
	selected_catalog_stamp_hash = ""
	if editing_catalog_stamp_hash == stamp_hash:
		editing_catalog_stamp_hash = ""
	if removed_from_current_livery:
		_refresh_preview_custom_stamp_atlas()
		_save_livery_for_selected_car()
		_refresh_stamp_controls()
	_refresh_custom_stamp_library()

func _custom_stamp_blob_for_hash(stamp_hash: String) -> CustomStampBlob:
	for blob in custom_stamp_blobs:
		var custom_blob := blob as CustomStampBlob
		if custom_blob != null and custom_blob.stamp_hash == stamp_hash:
			return custom_blob
	var loaded := CustomStampStore.load_blob(stamp_hash)
	if loaded != null:
		custom_stamp_blobs.append(loaded)
	return loaded

func _custom_stamp_button_text(blob: CustomStampBlob) -> String:
	var mode := "4bpp" if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE else "8bpp"
	var palette_text := "Custom" if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE else CustomStampPaletteCatalog.palette_name(blob.palette_id)
	return "%dx%d %s %s %s" % [blob.width, blob.height, mode, palette_text, blob.stamp_hash.substr(0, 8)]

func _custom_stamp_preview_texture(blob: CustomStampBlob) -> Texture2D:
	if custom_stamp_preview_textures.has(blob.stamp_hash):
		return custom_stamp_preview_textures[blob.stamp_hash]
	var texture := CustomStampStore.create_preview_texture(blob)
	if texture != null:
		custom_stamp_preview_textures[blob.stamp_hash] = texture
	return texture

func _on_custom_painter_size_selected(index: int) -> void:
	var id := custom_stamp_painter_size_option.get_item_id(index)
	match id:
		256128:
			_custom_painter_reset(Vector2i(256, 128))
		128256:
			_custom_painter_reset(Vector2i(128, 256))
		_:
			_custom_painter_reset(Vector2i(id, id))

func _custom_painter_reset(size: Vector2i) -> void:
	custom_painter_size = size
	_select_custom_painter_size_option(size)
	custom_painter_indices.resize(size.x * size.y)
	custom_painter_indices.fill(0)
	custom_painter_palette = _default_custom_painter_palette()
	custom_painter_colour_index = 1
	custom_stamp_painter_colour_picker.color = custom_painter_palette[custom_painter_colour_index]
	_rebuild_custom_painter_palette_buttons()
	_refresh_custom_painter_texture()

func _custom_painter_load_blob(blob: CustomStampBlob) -> void:
	if blob == null or blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE:
		return
	custom_painter_size = Vector2i(blob.width, blob.height)
	_select_custom_painter_size_option(custom_painter_size)
	custom_painter_indices = _custom_painter_unpack_indices(blob.decompress_indices(), blob.width * blob.height)
	custom_painter_palette = _normalized_custom_painter_palette(blob.custom_palette)
	custom_painter_colour_index = 1
	custom_stamp_painter_colour_picker.color = custom_painter_palette[custom_painter_colour_index]
	_rebuild_custom_painter_palette_buttons()
	_refresh_custom_painter_texture()

func _select_custom_painter_size_option(size: Vector2i) -> void:
	var target_id := size.x if size.x == size.y else int("%d%d" % [size.x, size.y])
	for i in range(custom_stamp_painter_size_option.item_count):
		if custom_stamp_painter_size_option.get_item_id(i) == target_id:
			custom_stamp_painter_size_option.select(i)
			return

func _custom_painter_unpack_indices(raw: PackedByteArray, pixel_count: int) -> PackedByteArray:
	var indices := PackedByteArray()
	indices.resize(pixel_count)
	for pixel_index in range(pixel_count):
		var packed := int(raw[int(pixel_index / 2)])
		if (pixel_index & 1) == 0:
			indices[pixel_index] = packed & 0x0f
		else:
			indices[pixel_index] = (packed >> 4) & 0x0f
	return indices

func _normalized_custom_painter_palette(source: PackedColorArray) -> PackedColorArray:
	var palette := PackedColorArray()
	palette.append(Color(1.0, 1.0, 1.0, 0.0))
	var start := 1 if source.size() > 0 and source[0].a <= 0.0 else 0
	for i in range(start, mini(source.size(), start + 15)):
		palette.append(source[i])
	while palette.size() < 16:
		palette.append(Color.WHITE)
	return palette

func _default_custom_painter_palette() -> PackedColorArray:
	var palette := PackedColorArray()
	palette.append(Color(1.0, 1.0, 1.0, 0.0))
	for colour in [
		Color.WHITE,
		Color.BLACK,
		Color(0.88, 0.08, 0.12, 1.0),
		Color(1.0, 0.78, 0.12, 1.0),
		Color(0.1, 0.75, 0.25, 1.0),
		Color(0.1, 0.55, 1.0, 1.0),
		Color(0.55, 0.25, 1.0, 1.0),
		Color(1.0, 0.25, 0.7, 1.0),
		Color(0.0, 0.85, 0.85, 1.0),
		Color(0.95, 0.45, 0.12, 1.0),
		Color(0.45, 0.25, 0.12, 1.0),
		Color(0.45, 0.45, 0.45, 1.0),
		Color(0.72, 0.72, 0.72, 1.0),
		Color(0.25, 0.35, 0.45, 1.0),
		Color(0.02, 0.02, 0.04, 1.0),
	]:
		palette.append(colour)
	return palette

func _rebuild_custom_painter_palette_buttons() -> void:
	for child in custom_stamp_painter_palette_grid.get_children():
		child.queue_free()
	for index in range(16):
		var button := Button.new()
		button.custom_minimum_size = Vector2(52.0, 28.0)
		button.text = "T" if index == 0 else str(index)
		button.modulate = Color(1.0, 1.0, 1.0, 1.0) if index == 0 else custom_painter_palette[index]
		button.disabled = false
		button.pressed.connect(_on_custom_painter_palette_pressed.bind(index))
		custom_stamp_painter_palette_grid.add_child(button)

func _on_custom_painter_palette_pressed(index: int) -> void:
	custom_painter_colour_index = clampi(index, 0, 15)
	if custom_painter_colour_index > 0:
		custom_stamp_painter_colour_picker.color = custom_painter_palette[custom_painter_colour_index]

func _on_custom_painter_colour_changed(colour: Color) -> void:
	if custom_painter_colour_index <= 0:
		return
	custom_painter_palette[custom_painter_colour_index] = Color(colour.r, colour.g, colour.b, 1.0)
	_rebuild_custom_painter_palette_buttons()
	_refresh_custom_painter_texture()

func _on_custom_painter_canvas_input(event: InputEvent) -> void:
	var mouse_button := event as InputEventMouseButton
	if mouse_button != null and mouse_button.button_index == MOUSE_BUTTON_LEFT:
		custom_painter_drawing = mouse_button.pressed
		if mouse_button.pressed:
			_custom_painter_draw_at(mouse_button.position)
			custom_stamp_painter_canvas.accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion != null and custom_painter_drawing:
		_custom_painter_draw_at(motion.position)
		custom_stamp_painter_canvas.accept_event()

func _custom_painter_draw_at(position: Vector2) -> void:
	if custom_stamp_painter_canvas.size.x <= 0.0 or custom_stamp_painter_canvas.size.y <= 0.0:
		return
	var x := clampi(int(floorf(position.x / custom_stamp_painter_canvas.size.x * float(custom_painter_size.x))), 0, custom_painter_size.x - 1)
	var y := clampi(int(floorf(position.y / custom_stamp_painter_canvas.size.y * float(custom_painter_size.y))), 0, custom_painter_size.y - 1)
	custom_painter_indices[y * custom_painter_size.x + x] = custom_painter_colour_index
	_refresh_custom_painter_texture()

func _refresh_custom_painter_texture() -> void:
	var image := Image.create(custom_painter_size.x, custom_painter_size.y, false, Image.FORMAT_RGBA8)
	for y in range(custom_painter_size.y):
		for x in range(custom_painter_size.x):
			var index := int(custom_painter_indices[y * custom_painter_size.x + x])
			image.set_pixel(x, y, custom_painter_palette[index] if index > 0 else Color(1.0, 1.0, 1.0, 0.0))
	if custom_painter_texture == null:
		custom_painter_texture = ImageTexture.create_from_image(image)
	else:
		custom_painter_texture.set_image(image)
	custom_stamp_painter_canvas.texture = custom_painter_texture

func _on_custom_painter_clear_pressed() -> void:
	custom_painter_indices.fill(0)
	_refresh_custom_painter_texture()

func _on_custom_painter_save_pressed() -> void:
	var raw := _custom_painter_pack_indices()
	var blob: CustomStampBlob = CustomStampBlob.from_index_bytes(custom_painter_size.x, custom_painter_size.y, CustomStampBlob.BPP_CUSTOM_PALETTE, 0, raw, custom_painter_palette)
	var validation_error := blob.validate_blob()
	if validation_error != "":
		push_warning("Painted custom stamp is invalid: %s" % validation_error)
		return
	var err := CustomStampStore.save_blob(blob)
	if err != OK:
		push_warning("Failed to save painted custom stamp: %s" % err)
		return
	var old_hash := editing_catalog_stamp_hash
	if old_hash != "" and old_hash != blob.stamp_hash:
		_replace_custom_stamp_references(old_hash, blob)
		CustomStampStore.delete_blob(old_hash)
		custom_stamp_preview_textures.erase(old_hash)
	editing_catalog_stamp_hash = ""
	custom_stamp_painter_popup.hide()
	_refresh_custom_stamp_library()

func _replace_custom_stamp_references(old_hash: String, new_blob: CustomStampBlob) -> void:
	if old_hash == "" or new_blob == null:
		return
	var changed := false
	for stamp in current_livery.stamps:
		if stamp == null or !stamp.is_custom() or stamp.custom_hash != old_hash:
			continue
		_set_stamp_asset_to_custom(stamp, new_blob)
		changed = true
	if !changed:
		return
	_refresh_preview_custom_stamp_atlas()
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _custom_painter_pack_indices() -> PackedByteArray:
	var raw := PackedByteArray()
	raw.resize(CustomStampBlob.index_byte_size(custom_painter_size.x, custom_painter_size.y, CustomStampBlob.BPP_CUSTOM_PALETTE))
	for pixel_index in range(custom_painter_indices.size()):
		var index := int(custom_painter_indices[pixel_index]) & 0x0f
		var byte_index := int(pixel_index / 2)
		if (pixel_index & 1) == 0:
			raw[byte_index] = (raw[byte_index] & 0xf0) | index
		else:
			raw[byte_index] = (raw[byte_index] & 0x0f) | (index << 4)
	return raw

func _on_custom_painter_cancel_pressed() -> void:
	custom_stamp_painter_popup.hide()
	editing_catalog_stamp_hash = ""

func _begin_stamp_edit(layer: int, stamp: CarLiveryStamp, is_new: bool) -> void:
	if _livery_editing_locked():
		return
	editing_stamp_layer = layer
	editing_stamp = stamp
	editing_is_new = is_new
	editing_previous_livery_enabled = current_livery_enabled
	editing_original_stamp = null if is_new else stamp.duplicate_stamp()
	current_livery_enabled = true
	stamp_ui_mode = StampUiMode.EDITING
	stamp_edit_roll = -stamp.rotation
	preview_has_transform_override = false
	preview_has_camera_override = false
	if is_new:
		stamp_edit_rect_size = Vector2(160.0, 160.0)
	else:
		_focus_preview_on_stamp(stamp)
		stamp_edit_rect_size = _edit_rect_size_from_stamp(stamp)
	stamp_edit_overlay.show()
	_layout_stamp_edit_overlay()
	_rebuild_preview_vehicle()
	_apply_edit_stamp_from_camera()

func _focus_preview_on_stamp(stamp: CarLiveryStamp) -> void:
	if preview_camera == null or preview_vehicle == null:
		return
	if absf(stamp.local_basis.determinant()) <= 0.00001:
		return
	preview_vehicle.transform = _preview_vehicle_scene_transform()
	var projector := preview_vehicle.global_transform * Transform3D(stamp.local_basis, stamp.local_origin)
	var view_direction := projector.basis.z.normalized()
	preview_pan = projector.origin - Vector3(0.0, 0.5, 0.0)
	preview_yaw = atan2(view_direction.x, view_direction.z)
	var stamp_elevation := asin(clampf(view_direction.y, -1.0, 1.0))
	preview_pitch = clampf(-stamp_elevation, deg_to_rad(-90.0), deg_to_rad(55.0))
	var plane_basis := _preview_view_plane_basis(_preview_camera_offset())
	preview_pan = Vector3(
		clampf(projector.origin.dot(plane_basis.x), -PREVIEW_PAN_LIMIT, PREVIEW_PAN_LIMIT),
		clampf(projector.origin.dot(plane_basis.y), -PREVIEW_PAN_LIMIT, PREVIEW_PAN_LIMIT),
		0.0
	)
	preview_has_camera_override = false
	_apply_preview_camera()

func _layout_stamp_edit_overlay() -> void:
	if stamp_edit_overlay == null or stamp_edit_square == null:
		return
	var overlay_size := stamp_edit_overlay.size
	var square_size := Vector2(maxf(STAMP_EDIT_MIN_SCREEN_SIZE, stamp_edit_rect_size.x), maxf(STAMP_EDIT_MIN_SCREEN_SIZE, stamp_edit_rect_size.y))
	stamp_edit_square.size = square_size
	stamp_edit_square.pivot_offset = square_size * 0.5
	stamp_edit_square.position = overlay_size * 0.5 - square_size * 0.5
	stamp_edit_square.rotation = stamp_edit_roll
	var handle_size := 14.0
	var edge_len := 36.0
	_place_edit_handle("edge_left", Vector2(-handle_size * 0.5, square_size.y * 0.5 - edge_len * 0.5), Vector2(handle_size, edge_len))
	_place_edit_handle("edge_right", Vector2(square_size.x - handle_size * 0.5, square_size.y * 0.5 - edge_len * 0.5), Vector2(handle_size, edge_len))
	_place_edit_handle("edge_top", Vector2(square_size.x * 0.5 - edge_len * 0.5, -handle_size * 0.5), Vector2(edge_len, handle_size))
	_place_edit_handle("edge_bottom", Vector2(square_size.x * 0.5 - edge_len * 0.5, square_size.y - handle_size * 0.5), Vector2(edge_len, handle_size))
	_place_edit_handle("corner_tl", Vector2(-handle_size * 0.5, -handle_size * 0.5), Vector2(handle_size, handle_size))
	_place_edit_handle("corner_tr", Vector2(square_size.x - handle_size * 0.5, -handle_size * 0.5), Vector2(handle_size, handle_size))
	_place_edit_handle("corner_bl", Vector2(-handle_size * 0.5, square_size.y - handle_size * 0.5), Vector2(handle_size, handle_size))
	_place_edit_handle("corner_br", Vector2(square_size.x - handle_size * 0.5, square_size.y - handle_size * 0.5), Vector2(handle_size, handle_size))
	stamp_edit_confirm_button.position = Vector2(overlay_size.x - stamp_edit_confirm_button.custom_minimum_size.x - 12.0, 12.0)
	stamp_edit_cancel_button.position = Vector2(overlay_size.x - stamp_edit_cancel_button.custom_minimum_size.x - 12.0, overlay_size.y - stamp_edit_cancel_button.custom_minimum_size.y - 12.0)

func _place_edit_handle(name: String, position: Vector2, size: Vector2) -> void:
	var handle := stamp_edit_square.get_node_or_null(name) as Control
	if handle == null:
		return
	handle.position = position
	handle.size = size

func _on_stamp_edit_handle_input(event: InputEvent, kind: String) -> void:
	var button := event as InputEventMouseButton
	if button != null and button.button_index == MOUSE_BUTTON_LEFT:
		if button.pressed:
			stamp_edit_drag_kind = kind
			stamp_edit_drag_start_mouse = get_global_mouse_position()
			stamp_edit_drag_start_center = stamp_edit_square.get_global_transform_with_canvas() * (stamp_edit_square.size * 0.5)
			stamp_edit_drag_start_size = stamp_edit_rect_size
			stamp_edit_drag_start_roll = stamp_edit_roll
		elif stamp_edit_drag_kind == kind:
			stamp_edit_drag_kind = ""
		stamp_edit_overlay.accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion == null or stamp_edit_drag_kind != kind:
		return
	var mouse_pos := get_global_mouse_position()
	var delta := (mouse_pos - stamp_edit_drag_start_mouse).rotated(-stamp_edit_drag_start_roll)
	if kind.begins_with("edge_"):
		var new_size := stamp_edit_drag_start_size
		match kind:
			"edge_left":
				new_size.x -= delta.x * 2.0
			"edge_right":
				new_size.x += delta.x * 2.0
			"edge_top":
				new_size.y -= delta.y * 2.0
			"edge_bottom":
				new_size.y += delta.y * 2.0
		stamp_edit_rect_size = Vector2(maxf(STAMP_EDIT_MIN_SCREEN_SIZE, new_size.x), maxf(STAMP_EDIT_MIN_SCREEN_SIZE, new_size.y))
	else:
		var start_angle := (stamp_edit_drag_start_mouse - stamp_edit_drag_start_center).angle()
		var current_angle := (mouse_pos - stamp_edit_drag_start_center).angle()
		stamp_edit_roll = stamp_edit_drag_start_roll + current_angle - start_angle
	_layout_stamp_edit_overlay()
	_apply_edit_stamp_from_camera()
	stamp_edit_overlay.accept_event()

func _on_stamp_edit_overlay_gui_input(event: InputEvent) -> void:
	if stamp_ui_mode != StampUiMode.EDITING:
		return
	var mouse_button := event as InputEventMouseButton
	if mouse_button != null:
		var is_camera_release := !mouse_button.pressed and preview_drag_button == mouse_button.button_index
		if !is_camera_release and !_stamp_edit_allows_camera_input(mouse_button.position):
			return
		preview_has_camera_override = false
		_handle_preview_mouse_button(mouse_button, true)
		if mouse_button.pressed and (mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP or mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN):
			_apply_edit_stamp_from_camera()
		stamp_edit_overlay.accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion != null:
		if preview_drag_button == 0 and !_stamp_edit_allows_camera_input(motion.position):
			return
		preview_has_camera_override = false
		var before_yaw := preview_yaw
		var before_pitch := preview_pitch
		var before_pan := preview_pan
		_handle_preview_mouse_motion(motion, true)
		if !is_equal_approx(before_yaw, preview_yaw) or !is_equal_approx(before_pitch, preview_pitch) or !before_pan.is_equal_approx(preview_pan):
			_apply_edit_stamp_from_camera()
		stamp_edit_overlay.accept_event()

func _stamp_edit_allows_camera_input(position: Vector2) -> bool:
	if stamp_edit_square != null and stamp_edit_square.get_rect().has_point(position):
		return false
	if stamp_edit_confirm_button != null and stamp_edit_confirm_button.get_rect().has_point(position):
		return false
	if stamp_edit_cancel_button != null and stamp_edit_cancel_button.get_rect().has_point(position):
		return false
	return true

func _apply_edit_stamp_from_camera() -> void:
	if editing_stamp == null or preview_camera == null or preview_vehicle == null:
		return
	var center := car_preview_space.size * 0.5
	var hit := _raycast_preview_body(center)
	if hit.is_empty():
		return
	var ray_dir := preview_camera.project_ray_normal(center).normalized()
	var car_inv := preview_vehicle.global_transform.affine_inverse()
	editing_stamp.local_origin = car_inv * hit["position"]
	var z_axis := (car_inv.basis * -ray_dir).normalized()
	var x_axis := (car_inv.basis * preview_camera.global_transform.basis.x).normalized()
	x_axis = (x_axis - z_axis * x_axis.dot(z_axis)).normalized()
	var y_axis := z_axis.cross(x_axis).normalized()
	var projection_roll := -stamp_edit_roll
	var roll_basis := Basis(z_axis, projection_roll)
	x_axis = roll_basis * x_axis
	y_axis = roll_basis * y_axis
	editing_stamp.local_basis = Basis(x_axis, y_axis, z_axis)
	editing_stamp.rotation = projection_roll
	editing_stamp.size = _stamp_world_size_from_edit_rect(hit["position"])
	editing_stamp.projection_depth = maxf(0.75, maxf(editing_stamp.size.x, editing_stamp.size.y) * 1.35)
	_rebuild_edit_stamp_preview()

func _stamp_world_size_from_edit_rect(hit_position: Vector3) -> Vector2:
	var viewport_size := car_preview_space.size
	if viewport_size.x <= 0.0 or viewport_size.y <= 0.0 or preview_camera == null:
		return Vector2.ONE
	var distance := preview_camera.global_position.distance_to(hit_position)
	var world_height := 2.0 * distance * tan(deg_to_rad(preview_camera.fov) * 0.5)
	var world_width := world_height * viewport_size.x / viewport_size.y
	return Vector2(world_width * stamp_edit_rect_size.x / viewport_size.x, world_height * stamp_edit_rect_size.y / viewport_size.y)

func _edit_rect_size_from_stamp(stamp: CarLiveryStamp) -> Vector2:
	if preview_camera == null or preview_vehicle == null:
		return Vector2(160.0, 160.0)
	var viewport_size := car_preview_space.size
	var world_pos := preview_vehicle.global_transform * stamp.local_origin
	var distance := preview_camera.global_position.distance_to(world_pos)
	if distance <= 0.01 or viewport_size.x <= 0.0 or viewport_size.y <= 0.0:
		return Vector2(160.0, 160.0)
	var world_height := 2.0 * distance * tan(deg_to_rad(preview_camera.fov) * 0.5)
	var world_width := world_height * viewport_size.x / viewport_size.y
	return Vector2(maxf(STAMP_EDIT_MIN_SCREEN_SIZE, stamp.size.x / world_width * viewport_size.x), maxf(STAMP_EDIT_MIN_SCREEN_SIZE, stamp.size.y / world_height * viewport_size.y))

func _on_stamp_edit_confirm_pressed() -> void:
	_end_stamp_edit(true)

func _on_stamp_edit_cancel_pressed() -> void:
	_end_stamp_edit(false)

func _end_stamp_edit(confirm: bool) -> void:
	if !confirm:
		if editing_is_new:
			_remove_stamp_layer(editing_stamp_layer)
		elif editing_original_stamp != null:
			_set_stamp_for_layer(editing_stamp_layer, editing_original_stamp)
		current_livery_enabled = editing_previous_livery_enabled
	stamp_edit_overlay.hide()
	stamp_ui_mode = StampUiMode.IDLE
	preview_has_transform_override = false
	preview_has_camera_override = false
	editing_stamp_layer = -1
	editing_stamp = null
	editing_original_stamp = null
	editing_is_new = false
	_refresh_stamp_controls()
	if confirm:
		_save_livery_for_selected_car()
	else:
		_rebuild_preview_vehicle()

func _on_preview_gui_input(event: InputEvent) -> void:
	var mouse_event := event as InputEventMouseButton
	if mouse_event != null:
		_handle_preview_mouse_button(mouse_event)
		return
	var motion_event := event as InputEventMouseMotion
	if motion_event != null:
		_handle_preview_mouse_motion(motion_event)

func _handle_preview_mouse_button(event: InputEventMouseButton, allow_edit_camera := false) -> void:
	if stamp_ui_mode == StampUiMode.EDITING and !allow_edit_camera:
		return
	if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
		preview_distance = maxf(2.5, preview_distance * 0.9)
		_apply_preview_camera()
		preview_container.accept_event()
		return
	if event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
		preview_distance = minf(44.0, preview_distance * 1.1)
		_apply_preview_camera()
		preview_container.accept_event()
		return
	if event.button_index != MOUSE_BUTTON_LEFT and event.button_index != MOUSE_BUTTON_RIGHT and event.button_index != MOUSE_BUTTON_MIDDLE:
		return
	if event.pressed:
		preview_drag_button = event.button_index
		preview_drag_start = event.position
		preview_drag_last = event.position
		preview_drag_moved = false
		preview_container.accept_event()
		return
	if preview_drag_button == event.button_index:
		preview_drag_button = 0
		preview_container.accept_event()

func _handle_preview_mouse_motion(event: InputEventMouseMotion, allow_edit_camera := false) -> void:
	if stamp_ui_mode == StampUiMode.EDITING and !allow_edit_camera:
		return
	if preview_drag_button == 0:
		return
	var delta := event.position - preview_drag_last
	preview_drag_last = event.position
	if event.position.distance_to(preview_drag_start) > 4.0:
		preview_drag_moved = true
	if preview_drag_button == MOUSE_BUTTON_LEFT and !event.shift_pressed:
		preview_yaw += delta.x * -0.004
		preview_pitch = clampf(preview_pitch + delta.y * -0.004, deg_to_rad(-89.0), deg_to_rad(55.0))
	else:
		var pan_scale := preview_distance * 0.0008
		preview_pan.x -= delta.x * pan_scale
		preview_pan.y += delta.y * pan_scale
		_clamp_preview_pan()
	_apply_preview_camera()
	preview_container.accept_event()

func _apply_preview_camera() -> void:
	if preview_vehicle != null:
		preview_vehicle.transform = _preview_vehicle_scene_transform()
	if preview_camera == null:
		return
	if preview_has_camera_override:
		preview_camera.global_transform = preview_camera_override
	else:
		_clamp_preview_pan()
		var camera_offset := _preview_camera_offset()
		var target := _preview_pan_target(camera_offset)
		preview_camera.position = target + camera_offset
		preview_camera.basis = _preview_camera_basis(camera_offset)
	_submit_preview_render()

func _preview_camera_offset() -> Vector3:
	var yaw_basis := Basis(Vector3.UP, preview_yaw)
	var pitch_basis := Basis(yaw_basis.x.normalized(), preview_pitch)
	return pitch_basis * (yaw_basis * Vector3(0.0, 0.0, preview_distance))

func _preview_camera_basis(camera_offset: Vector3) -> Basis:
	var view_back := camera_offset.normalized()
	var right := Vector3.UP.cross(view_back)
	if right.length_squared() <= 0.0001:
		right = Vector3.RIGHT
	else:
		right = right.normalized()
	var up := view_back.cross(right).normalized()
	return Basis(right, up, view_back)

func _preview_pan_target(camera_offset: Vector3) -> Vector3:
	var plane_basis := _preview_view_plane_basis(camera_offset)
	return plane_basis.x * preview_pan.x + plane_basis.y * preview_pan.y

func _preview_view_plane_basis(camera_offset: Vector3) -> Basis:
	var view_back := camera_offset.normalized()
	var right := Vector3.UP.cross(view_back)
	if right.length_squared() <= 0.0001:
		right = Vector3.RIGHT
	else:
		right = right.normalized()
	var up := view_back.cross(right).normalized()
	return Basis(right, up, view_back)

func _clamp_preview_pan() -> void:
	preview_pan.x = clampf(preview_pan.x, -PREVIEW_PAN_LIMIT, PREVIEW_PAN_LIMIT)
	preview_pan.y = clampf(preview_pan.y, -PREVIEW_PAN_LIMIT, PREVIEW_PAN_LIMIT)
	preview_pan.z = 0.0

func _place_stamp_at_preview_pos(viewport_pos: Vector2) -> void:
	return

func _raycast_preview_body(viewport_pos: Vector2) -> Dictionary:
	if preview_camera == null or preview_vehicle == null:
		return {}
	var body := preview_vehicle.get_node_or_null("VEHICLE_MAIN") as MeshInstance3D
	if body == null or body.mesh == null:
		return {}
	var ray_origin := preview_camera.project_ray_origin(viewport_pos)
	var ray_dir := preview_camera.project_ray_normal(viewport_pos).normalized()
	var best_t := INF
	var best_hit := {}
	for surface_index in range(body.mesh.get_surface_count()):
		var arrays := body.mesh.surface_get_arrays(surface_index)
		var vertices := _surface_vertices(arrays)
		if vertices.is_empty():
			continue
		var indices := _surface_indices(arrays)
		if indices.is_empty():
			var tri_count := int(vertices.size() / 3)
			for tri in range(tri_count):
				var hit := _raycast_triangle(ray_origin, ray_dir, body.global_transform * vertices[tri * 3], body.global_transform * vertices[tri * 3 + 1], body.global_transform * vertices[tri * 3 + 2])
				if !hit.is_empty() and hit["t"] < best_t:
					best_t = hit["t"]
					best_hit = hit
		else:
			var tri_count := int(indices.size() / 3)
			for tri in range(tri_count):
				var i0 := indices[tri * 3]
				var i1 := indices[tri * 3 + 1]
				var i2 := indices[tri * 3 + 2]
				if i0 < 0 or i1 < 0 or i2 < 0 or i0 >= vertices.size() or i1 >= vertices.size() or i2 >= vertices.size():
					continue
				var hit := _raycast_triangle(ray_origin, ray_dir, body.global_transform * vertices[i0], body.global_transform * vertices[i1], body.global_transform * vertices[i2])
				if !hit.is_empty() and hit["t"] < best_t:
					best_t = hit["t"]
					best_hit = hit
	return best_hit

func _raycast_triangle(origin: Vector3, direction: Vector3, a: Vector3, b: Vector3, c: Vector3) -> Dictionary:
	var edge_1 := b - a
	var edge_2 := c - a
	var h := direction.cross(edge_2)
	var det := edge_1.dot(h)
	if absf(det) < 0.00001:
		return {}
	var inv_det := 1.0 / det
	var s := origin - a
	var u := inv_det * s.dot(h)
	if u < 0.0 or u > 1.0:
		return {}
	var q := s.cross(edge_1)
	var v := inv_det * direction.dot(q)
	if v < 0.0 or u + v > 1.0:
		return {}
	var t := inv_det * edge_2.dot(q)
	if t <= 0.0001:
		return {}
	var normal := edge_1.cross(edge_2).normalized()
	if normal.dot(direction) > 0.0:
		normal = -normal
	return {"t": t, "position": origin + direction * t, "normal": normal}

func _set_stamp_basis_from_normal(stamp: CarLiveryStamp, normal: Vector3) -> void:
	if normal.length_squared() <= 0.0001:
		normal = Vector3.UP
	normal = normal.normalized()
	var reference := Vector3.UP
	if absf(normal.dot(reference)) > 0.9:
		reference = Vector3.RIGHT
	var x_axis := reference.cross(normal).normalized()
	var y_axis := normal.cross(x_axis).normalized()
	var rotation_basis := Basis(normal, stamp.rotation)
	x_axis = rotation_basis * x_axis
	y_axis = rotation_basis * y_axis
	stamp.local_basis = Basis(x_axis, y_axis, normal)

func _surface_vertices(arrays: Array) -> PackedVector3Array:
	if arrays.size() <= Mesh.ARRAY_VERTEX or typeof(arrays[Mesh.ARRAY_VERTEX]) != TYPE_PACKED_VECTOR3_ARRAY:
		return PackedVector3Array()
	return arrays[Mesh.ARRAY_VERTEX]

func _surface_indices(arrays: Array) -> PackedInt32Array:
	if arrays.size() <= Mesh.ARRAY_INDEX or typeof(arrays[Mesh.ARRAY_INDEX]) != TYPE_PACKED_INT32_ARRAY:
		return PackedInt32Array()
	return arrays[Mesh.ARRAY_INDEX]
