extends Control

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const CarLiveryStampMeshBuilder = preload("res://vehicle/customization/car_livery_stamp_mesh_builder.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CarRenderManager = preload("res://vehicle/car_render_manager.gd")

const STAMP_EDIT_MIN_SCREEN_SIZE := 1.0
const PREVIEW_PAN_LIMIT := 4.0

@onready var machine_setting_slider: HSlider = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/MachineSettingSlider
@onready var machine_setting_percent: Label = $Container/SettingsTabs/Driver/DriverSettingsScroll/DriverSettings/MachineSettingPercent
@onready var vehicle_selector: ItemList = $Container/SettingsTabs/Driver/VehicleScroll/VehicleSelector
@onready var close_settings: Button = $Container/CloseSettings
@onready var car_preview_space: ColorRect = $Container/SettingsTabs/Garage/CarPreviewSpace
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
@onready var stamp_chooser_popup: PopupPanel = $StampChooser
@onready var stamp_chooser_list: VBoxContainer = $StampChooser/StampChooserList
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
var stamp_layer_buttons: Array[Button] = []
var stamp_layer_colour_pickers: Array[ColorPickerButton] = []
var preview_container: SubViewportContainer
var preview_viewport: SubViewport
var preview_root: Node3D
var preview_camera: Camera3D
var preview_vehicle: Node3D
var preview_render_manager: CarRenderManager
var preview_yaw := deg_to_rad(25.0)
var preview_pitch := 0.0
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
	sticker_selectors = [sticker_slot_1, sticker_slot_2, sticker_slot_3, sticker_slot_4]
	_populate_sticker_selectors()
	_setup_garage_preview()
	_setup_stamp_menus()
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
	stamp_layer_buttons.clear()
	stamp_layer_colour_pickers.clear()
	var count := mini(stamp_layer_list.get_child_count(), CarLivery.MAX_STAMPS)
	for layer in range(count):
		var row := stamp_layer_list.get_child(layer)
		var button := row as Button
		if button == null:
			button = row.get_node_or_null("StampButton") as Button
		if button == null:
			continue
		var colour_picker := row.get_node_or_null("ColourPicker") as ColorPickerButton
		if colour_picker != null:
			colour_picker.color_changed.connect(_on_stamp_colour_changed.bind(layer))
		button.pressed.connect(_on_stamp_layer_pressed.bind(layer))
		stamp_layer_buttons.append(button)
		stamp_layer_colour_pickers.append(colour_picker)

func _setup_stamp_menus() -> void:
	stamp_action_menu.clear()
	stamp_action_menu.add_item("Change", 0)
	stamp_action_menu.add_item("Edit", 1)
	stamp_action_menu.add_item("Delete", 2)
	stamp_action_menu.id_pressed.connect(_on_stamp_action_selected)

func _input(event: InputEvent) -> void:
	if !visible:
		return
	var mouse_event := event as InputEventMouseButton
	if mouse_event == null or !mouse_event.pressed or mouse_event.button_index != MOUSE_BUTTON_LEFT:
		return
	if _try_select_vehicle_at_global_position(mouse_event.position):
		get_viewport().set_input_as_handled()

func _try_select_vehicle_at_global_position(global_pos: Vector2) -> bool:
	if vehicle_selector == null or !vehicle_selector.visible:
		return false
	var selector_rect := vehicle_selector.get_global_rect()
	if !selector_rect.has_point(global_pos):
		return false
	var local_pos := global_pos - selector_rect.position
	for i in range(vehicle_selector.item_count):
		var item_rect := vehicle_selector.get_item_rect(i)
		if item_rect.has_point(local_pos):
			vehicle_selector.select(i)
			_on_vehicle_selected(i)
			return true
	return false

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
	_sync_livery_to_player_settings()
	var path := "user://player_settings.json"
	var file = FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(player_settings.to_dict()))
	file.close()
	if game_manager:
		game_manager.network_manager.send_player_settings(player_settings.to_dict())

func _populate_sticker_selectors() -> void:
	for selector in sticker_selectors:
		selector.clear()
		selector.expand_icon = true
		if sticker_selection == null:
			continue
		for i in range(sticker_selection.stickers.size()):
			selector.add_icon_item(sticker_selection.stickers[i], "", i)
			selector.get_popup().set_item_icon_max_width(i, 96)

func _update_controls() -> void:
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
		_rebuild_preview_vehicle()

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
	hide()

func open_settings() -> void:
	_load_settings()
	_load_car_defs()
	_update_controls()
	show()

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

func _save_livery_for_selected_car() -> void:
	if player_settings.car_definition_path == "":
		player_settings.car_livery = {}
		return
	current_livery_enabled = true
	current_livery.car_definition_path = player_settings.car_definition_path
	var err := CarLiveryStore.save_for_car(current_livery)
	if err != OK:
		push_warning("Failed to save car livery: %s" % err)
	_sync_livery_to_player_settings()
	if game_manager:
		game_manager.network_manager.send_player_settings(player_settings.to_dict())
	_rebuild_preview_vehicle()

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
	if updating_colour_controls:
		return
	current_livery.primary_colour = colour
	_save_livery_for_selected_car()

func _on_secondary_colour_changed(colour: Color) -> void:
	if updating_colour_controls:
		return
	current_livery.secondary_colour = colour
	_save_livery_for_selected_car()

func _on_accent_colour_changed(colour: Color) -> void:
	if updating_colour_controls:
		return
	current_livery.accent_colour = colour
	_save_livery_for_selected_car()

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
	preview_viewport.transparent_bg = true
	preview_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	preview_viewport.size = Vector2i(maxi(1, int(car_preview_space.size.x)), maxi(1, int(car_preview_space.size.y)))
	preview_container.add_child(preview_viewport)

	preview_root = Node3D.new()
	preview_viewport.add_child(preview_root)

	preview_render_manager = CarRenderManager.new()
	preview_render_manager.name = "GaragePreviewRenderManager"
	preview_root.add_child(preview_render_manager)

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(45.0, 35.0, 0.0)
	light.light_energy = 5.0
	preview_root.add_child(light)

	preview_camera = Camera3D.new()
	preview_camera.current = true
	preview_viewport.add_child(preview_camera)
	preview_camera.fov = 38.0
	_setup_stamp_edit_overlay()
	_apply_preview_camera()

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
	var definition := _selected_car_definition()
	if definition == null or definition.car_scene == null:
		return
	preview_vehicle = definition.car_scene.instantiate()
	preview_root.add_child(preview_vehicle)
	_hide_preview_raycast_scene(preview_vehicle)
	var render_settings: Array = [{}]
	if current_livery_enabled:
		current_livery.car_definition_path = player_settings.car_definition_path
		player_settings.set_car_livery(current_livery)
		render_settings[0] = player_settings.to_dict()
	preview_render_manager.configure_manual([definition], render_settings)
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

func _submit_preview_render() -> void:
	if preview_render_manager == null or preview_render_manager.archetypes.is_empty():
		return
	preview_render_manager.begin_manual_submit()
	preview_render_manager.submit_manual_car(0, _preview_vehicle_transform(), Color.BLACK, Vector3.ZERO, Color.BLACK, 0.0, false)

func _selected_car_definition() -> CarDefinition:
	for def in car_defs:
		if def.resource_path == player_settings.car_definition_path:
			return def
	return car_defs[0] if !car_defs.is_empty() else null

func _refresh_stamp_controls() -> void:
	updating_stamp_controls = true
	for layer in range(stamp_layer_buttons.size()):
		var button := stamp_layer_buttons[layer]
		var colour_picker: ColorPickerButton = stamp_layer_colour_pickers[layer] if layer < stamp_layer_colour_pickers.size() else null
		var stamp := _stamp_for_layer(layer)
		button.modulate = Color.WHITE
		button.expand_icon = true
		if stamp == null:
			button.text = "EMPTY"
			button.icon = null
			if colour_picker != null:
				colour_picker.color = Color(0.45, 0.45, 0.45, 1.0)
				colour_picker.disabled = true
		else:
			var label := _stamp_display_name(stamp.stamp_id)
			button.text = label
			button.icon = _stamp_preview_texture(stamp.stamp_id)
			if colour_picker != null:
				colour_picker.disabled = false
				colour_picker.color = stamp.colour
	updating_stamp_controls = false

func _stamp_preview_texture(stamp_id: String) -> Texture2D:
	if stamp_catalog == null:
		return null
	return stamp_catalog.get_preview_texture(stamp_id)

func _stamp_display_name(stamp_id: String) -> String:
	if stamp_catalog != null:
		var entry := stamp_catalog.get_entry(stamp_id)
		if entry != null and entry.display_name != "":
			return entry.display_name
	return stamp_id

func _on_stamp_colour_changed(colour: Color, layer: int) -> void:
	if updating_stamp_controls:
		return
	var stamp := _stamp_for_layer(layer)
	if stamp == null:
		return
	stamp.colour = colour
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _selected_stamp() -> CarLiveryStamp:
	return _stamp_for_layer(selected_stamp_index)

func _stamp_for_layer(layer: int) -> CarLiveryStamp:
	for stamp in current_livery.stamps:
		if stamp != null and stamp.layer == layer:
			return stamp
	return null

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

func _first_stamp_id() -> String:
	if stamp_catalog != null:
		for entry in stamp_catalog.entries:
			if entry != null and entry.is_valid_entry():
				return entry.stamp_id
	return "circle"

func _on_stamp_layer_pressed(layer: int) -> void:
	if updating_stamp_controls:
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
	stamp_action_menu.position = Vector2i(rect.position + Vector2(24.0, rect.size.y))
	stamp_action_menu.popup()

func _on_stamp_action_selected(id: int) -> void:
	if pending_stamp_layer < 0:
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

func _show_stamp_chooser(layer: int, action: String) -> void:
	if stamp_chooser_popup == null or stamp_chooser_list == null:
		return
	stamp_ui_mode = StampUiMode.CHOOSING
	pending_stamp_layer = layer
	pending_stamp_choice_action = action
	for child in stamp_chooser_list.get_children():
		child.queue_free()
	var title := Label.new()
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.text = "Layer %02d" % [layer + 1]
	stamp_chooser_list.add_child(title)
	if stamp_catalog != null:
		for entry in stamp_catalog.entries:
			if entry == null or !entry.is_valid_entry():
				continue
			var button := Button.new()
			button.text = entry.display_name if entry.display_name != "" else entry.stamp_id
			button.custom_minimum_size = Vector2(0.0, 34.0)
			button.pressed.connect(_on_stamp_choice_pressed.bind(entry.stamp_id))
			stamp_chooser_list.add_child(button)
	var cancel := Button.new()
	cancel.text = "Cancel"
	cancel.custom_minimum_size = Vector2(0.0, 34.0)
	cancel.pressed.connect(_on_stamp_choice_cancel_pressed)
	stamp_chooser_list.add_child(cancel)
	stamp_chooser_popup.popup_centered(Vector2i(260, 190))

func _on_stamp_choice_pressed(stamp_id: String) -> void:
	stamp_chooser_popup.hide()
	if pending_stamp_layer < 0:
		stamp_ui_mode = StampUiMode.IDLE
		return
	if pending_stamp_choice_action == "change":
		var existing := _stamp_for_layer(pending_stamp_layer)
		if existing != null:
			existing.stamp_id = stamp_id
			_save_livery_for_selected_car()
			_refresh_stamp_controls()
		stamp_ui_mode = StampUiMode.IDLE
		return
	var stamp := _new_stamp(pending_stamp_layer, stamp_id)
	_set_stamp_for_layer(pending_stamp_layer, stamp)
	_begin_stamp_edit(pending_stamp_layer, stamp, true)

func _on_stamp_choice_cancel_pressed() -> void:
	stamp_chooser_popup.hide()
	stamp_ui_mode = StampUiMode.IDLE
	pending_stamp_layer = -1
	pending_stamp_choice_action = ""

func _begin_stamp_edit(layer: int, stamp: CarLiveryStamp, is_new: bool) -> void:
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
	_apply_edit_stamp_from_camera()

func _focus_preview_on_stamp(stamp: CarLiveryStamp) -> void:
	if preview_camera == null or preview_vehicle == null:
		return
	if absf(stamp.local_basis.determinant()) <= 0.00001:
		return
	preview_vehicle.transform = _preview_vehicle_transform()
	var projector := preview_vehicle.global_transform * Transform3D(stamp.local_basis, stamp.local_origin)
	var view_direction := projector.basis.z.normalized()
	preview_pan = projector.origin - Vector3(0.0, 0.5, 0.0)
	preview_yaw = atan2(view_direction.x, view_direction.z)
	var base_elevation := atan2(3.5, preview_distance)
	var stamp_elevation := asin(clampf(view_direction.y, -1.0, 1.0))
	preview_pitch = clampf(stamp_elevation - base_elevation, deg_to_rad(-55.0), deg_to_rad(55.0))
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
	_rebuild_preview_vehicle()

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
		preview_distance = maxf(8.0, preview_distance - 1.25)
		_apply_preview_camera()
		preview_container.accept_event()
		return
	if event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
		preview_distance = minf(44.0, preview_distance + 1.25)
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
		preview_yaw += delta.x * -0.01
		preview_pitch = clampf(preview_pitch + delta.y * -0.008, deg_to_rad(-55.0), deg_to_rad(55.0))
	else:
		var pan_scale := preview_distance * 0.0015
		preview_pan.x -= delta.x * pan_scale
		preview_pan.y += delta.y * pan_scale
		_clamp_preview_pan()
	_apply_preview_camera()
	preview_container.accept_event()

func _apply_preview_camera() -> void:
	if preview_vehicle != null:
		preview_vehicle.transform = _preview_vehicle_transform()
	if preview_camera == null:
		return
	if preview_has_camera_override:
		preview_camera.global_transform = preview_camera_override
	else:
		_clamp_preview_pan()
		var camera_offset := _preview_camera_offset()
		var target := _preview_pan_target(camera_offset)
		preview_camera.position = target + camera_offset
		preview_camera.look_at(target, Vector3.UP)
	_submit_preview_render()

func _preview_camera_offset() -> Vector3:
	var yaw_basis := Basis(Vector3.UP, preview_yaw)
	var pitch_basis := Basis(yaw_basis.x.normalized(), preview_pitch)
	return pitch_basis * (yaw_basis * Vector3(0.0, 3.5, preview_distance))

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
