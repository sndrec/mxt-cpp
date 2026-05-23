extends Control

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const CarLiveryStampMeshBuilder = preload("res://vehicle/customization/car_livery_stamp_mesh_builder.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CarRenderManager = preload("res://vehicle/car_render_manager.gd")

@onready var machine_setting_slider: HSlider = $Container/HBoxContainer/VBoxContainer/MachineSettingSlider
@onready var machine_setting_percent: Label = $Container/HBoxContainer/VBoxContainer/MachineSettingPercent
@onready var vehicle_selector: ItemList = $Container/ScrollContainer/VehicleSelector
@onready var close_settings: Button = $Container/CloseSettings
@onready var car_preview_space: ColorRect = $Container/HBoxContainer/CarPreviewSpace
@onready var pilot_name_input: LineEdit = $Container/HBoxContainer/VBoxContainer/PilotNameInput
@onready var spectator_toggle: CheckBox = $Container/HBoxContainer/VBoxContainer/SpectatorToggle
@onready var car_name_label: Label = $Container/HBoxContainer/VBoxContainer/CarName
@onready var sticker_slot_1: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot1
@onready var sticker_slot_2: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot2
@onready var sticker_slot_3: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot3
@onready var sticker_slot_4: OptionButton = $Container/HBoxContainer/VBoxContainer/StickerGrid/StickerSlot4
@onready var primary_colour_picker: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/PaintGrid/PrimaryColourPicker
@onready var secondary_colour_picker: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/PaintGrid/SecondaryColourPicker
@onready var accent_colour_picker: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/PaintGrid/AccentColourPicker
@onready var stamp_slot_selector: OptionButton = $Container/HBoxContainer/VBoxContainer/StampGrid/StampSlotSelector
@onready var stamp_add_button: Button = $Container/HBoxContainer/VBoxContainer/StampGrid/StampAddButton
@onready var stamp_delete_button: Button = $Container/HBoxContainer/VBoxContainer/StampGrid/StampDeleteButton
@onready var stamp_shape_selector: OptionButton = $Container/HBoxContainer/VBoxContainer/StampGrid/StampShapeSelector
@onready var stamp_colour_picker: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/StampGrid/StampColourPicker
@onready var stamp_layer_spin: SpinBox = $Container/HBoxContainer/VBoxContainer/StampGrid/StampLayerSpin
@onready var stamp_rotation_slider: HSlider = $Container/HBoxContainer/VBoxContainer/StampGrid/StampRotationSlider
@onready var stamp_scale_slider: HSlider = $Container/HBoxContainer/VBoxContainer/StampGrid/StampScaleSlider
@onready var stamp_depth_slider: HSlider = $Container/HBoxContainer/VBoxContainer/StampGrid/StampDepthSlider

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

func _ready() -> void:
	game_manager = get_parent() as GameManager
	_wrap_settings_column()
	_prioritize_vehicle_selector_input()
	_load_settings()
	_load_car_defs()
	sticker_selectors = [sticker_slot_1, sticker_slot_2, sticker_slot_3, sticker_slot_4]
	_populate_sticker_selectors()
	_populate_stamp_shapes()
	_setup_garage_preview()
	_update_controls()
	machine_setting_slider.value_changed.connect(_on_slider_changed)
	vehicle_selector.item_selected.connect(_on_vehicle_selected)
	pilot_name_input.text_changed.connect(_on_name_changed)
	close_settings.pressed.connect(_on_close_pressed)
	spectator_toggle.toggled.connect(_on_spectator_toggled)
	primary_colour_picker.color_changed.connect(_on_primary_colour_changed)
	secondary_colour_picker.color_changed.connect(_on_secondary_colour_changed)
	accent_colour_picker.color_changed.connect(_on_accent_colour_changed)
	stamp_slot_selector.item_selected.connect(_on_stamp_slot_selected)
	stamp_add_button.pressed.connect(_on_stamp_add_pressed)
	stamp_delete_button.pressed.connect(_on_stamp_delete_pressed)
	stamp_shape_selector.item_selected.connect(_on_stamp_shape_selected)
	stamp_colour_picker.color_changed.connect(_on_stamp_colour_changed)
	stamp_layer_spin.value_changed.connect(_on_stamp_layer_changed)
	stamp_rotation_slider.value_changed.connect(_on_stamp_rotation_changed)
	stamp_scale_slider.value_changed.connect(_on_stamp_scale_changed)
	stamp_depth_slider.value_changed.connect(_on_stamp_depth_changed)
	for i in range(sticker_selectors.size()):
		sticker_selectors[i].item_selected.connect(_on_sticker_selected.bind(i))

func _wrap_settings_column() -> void:
	var hbox := $Container/HBoxContainer as HBoxContainer
	var column := $Container/HBoxContainer/VBoxContainer as VBoxContainer
	if hbox == null or column == null or column.get_parent() is ScrollContainer:
		return
	var column_index := column.get_index()
	hbox.remove_child(column)
	var settings_scroll := ScrollContainer.new()
	settings_scroll.name = "SettingsScroll"
	settings_scroll.clip_contents = true
	settings_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	settings_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	settings_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	settings_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hbox.add_child(settings_scroll)
	hbox.move_child(settings_scroll, column_index)
	settings_scroll.add_child(column)
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.size_flags_vertical = Control.SIZE_SHRINK_BEGIN

func _prioritize_vehicle_selector_input() -> void:
	var container := $Container as Control
	var hbox := $Container/HBoxContainer as Control
	var selector_scroll := $Container/ScrollContainer as ScrollContainer
	if hbox != null:
		hbox.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if selector_scroll == null or container == null:
		return
	selector_scroll.mouse_filter = Control.MOUSE_FILTER_STOP
	selector_scroll.z_index = 32
	container.move_child(selector_scroll, container.get_child_count() - 1)

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

func _populate_stamp_shapes() -> void:
	stamp_shape_selector.clear()
	if stamp_catalog == null:
		return
	for i in range(stamp_catalog.entries.size()):
		var entry := stamp_catalog.entries[i]
		if entry == null or !entry.is_valid_entry():
			continue
		var label := entry.display_name if entry.display_name != "" else entry.stamp_id
		stamp_shape_selector.add_item(label)
		stamp_shape_selector.set_item_metadata(stamp_shape_selector.get_item_count() - 1, entry.stamp_id)

func _setup_garage_preview() -> void:
	car_preview_space.mouse_filter = Control.MOUSE_FILTER_STOP
	preview_container = SubViewportContainer.new()
	preview_container.name = "GaragePreviewViewport"
	preview_container.stretch = true
	preview_container.mouse_filter = Control.MOUSE_FILTER_STOP
	preview_container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	car_preview_space.add_child(preview_container)
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
	light.rotation_degrees = Vector3(-45.0, 35.0, 0.0)
	light.light_energy = 2.0
	preview_root.add_child(light)

	preview_camera = Camera3D.new()
	preview_camera.current = true
	preview_viewport.add_child(preview_camera)
	preview_camera.fov = 38.0
	_apply_preview_camera()

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
	var yaw_basis := Basis(Vector3.UP, preview_yaw)
	var pitch_basis := Basis(Vector3.RIGHT, preview_pitch)
	return Transform3D(yaw_basis * pitch_basis, preview_pan)

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
	stamp_slot_selector.clear()
	for i in range(current_livery.stamps.size()):
		var stamp := current_livery.stamps[i]
		var label := "%02d %s" % [i + 1, stamp.stamp_id]
		stamp_slot_selector.add_item(label)
	if current_livery.stamps.is_empty():
		selected_stamp_index = -1
	else:
		selected_stamp_index = clampi(selected_stamp_index, 0, current_livery.stamps.size() - 1)
		stamp_slot_selector.select(selected_stamp_index)
	_update_selected_stamp_controls()
	updating_stamp_controls = false

func _update_selected_stamp_controls() -> void:
	var stamp := _selected_stamp()
	var has_stamp := stamp != null
	stamp_delete_button.disabled = !has_stamp
	stamp_shape_selector.disabled = !has_stamp
	stamp_colour_picker.disabled = !has_stamp
	stamp_layer_spin.editable = has_stamp
	stamp_rotation_slider.editable = has_stamp
	stamp_scale_slider.editable = has_stamp
	stamp_depth_slider.editable = has_stamp
	if !has_stamp:
		return
	_select_stamp_shape(stamp.stamp_id)
	stamp_colour_picker.color = stamp.colour
	stamp_layer_spin.value = stamp.layer
	stamp_rotation_slider.value = rad_to_deg(stamp.rotation)
	stamp_scale_slider.value = maxf(stamp.size.x, stamp.size.y)
	stamp_depth_slider.value = stamp.projection_depth

func _selected_stamp() -> CarLiveryStamp:
	if selected_stamp_index < 0 or selected_stamp_index >= current_livery.stamps.size():
		return null
	return current_livery.stamps[selected_stamp_index]

func _select_stamp_shape(stamp_id: String) -> void:
	for i in range(stamp_shape_selector.get_item_count()):
		if str(stamp_shape_selector.get_item_metadata(i)) == stamp_id:
			stamp_shape_selector.select(i)
			return

func _first_stamp_id() -> String:
	if stamp_catalog != null:
		for entry in stamp_catalog.entries:
			if entry != null and entry.is_valid_entry():
				return entry.stamp_id
	return "circle"

func _on_stamp_slot_selected(index: int) -> void:
	if updating_stamp_controls:
		return
	selected_stamp_index = index
	updating_stamp_controls = true
	_update_selected_stamp_controls()
	updating_stamp_controls = false

func _on_stamp_add_pressed() -> void:
	if current_livery.stamps.size() >= CarLivery.MAX_STAMPS:
		return
	var stamp := CarLiveryStamp.new()
	stamp.stamp_id = _first_stamp_id()
	stamp.layer = current_livery.stamps.size()
	stamp.size = Vector2.ONE
	stamp.projection_depth = 0.5
	stamp.colour = Color.WHITE
	stamp.local_origin = Vector3.ZERO
	stamp.local_basis = Basis.IDENTITY
	stamp.rotation = 0.0
	current_livery.add_stamp(stamp)
	selected_stamp_index = current_livery.stamps.size() - 1
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _on_stamp_delete_pressed() -> void:
	if _selected_stamp() == null:
		return
	current_livery.remove_stamp(selected_stamp_index)
	selected_stamp_index = mini(selected_stamp_index, current_livery.stamps.size() - 1)
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _on_stamp_shape_selected(index: int) -> void:
	if updating_stamp_controls:
		return
	var stamp := _selected_stamp()
	if stamp == null:
		return
	stamp.stamp_id = str(stamp_shape_selector.get_item_metadata(index))
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _on_stamp_colour_changed(colour: Color) -> void:
	if updating_stamp_controls:
		return
	var stamp := _selected_stamp()
	if stamp == null:
		return
	stamp.colour = colour
	_save_livery_for_selected_car()

func _on_stamp_layer_changed(value: float) -> void:
	if updating_stamp_controls:
		return
	var stamp := _selected_stamp()
	if stamp == null:
		return
	stamp.layer = int(value)
	current_livery.stamps = current_livery.get_sorted_stamps()
	selected_stamp_index = current_livery.stamps.find(stamp)
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

func _on_stamp_rotation_changed(value: float) -> void:
	if updating_stamp_controls:
		return
	var stamp := _selected_stamp()
	if stamp == null:
		return
	stamp.rotation = deg_to_rad(value)
	_set_stamp_basis_from_normal(stamp, stamp.local_basis.z.normalized())
	_save_livery_for_selected_car()

func _on_stamp_scale_changed(value: float) -> void:
	if updating_stamp_controls:
		return
	var stamp := _selected_stamp()
	if stamp == null:
		return
	stamp.size = Vector2(value, value)
	_save_livery_for_selected_car()

func _on_stamp_depth_changed(value: float) -> void:
	if updating_stamp_controls:
		return
	var stamp := _selected_stamp()
	if stamp == null:
		return
	stamp.projection_depth = value
	_save_livery_for_selected_car()

func _on_preview_gui_input(event: InputEvent) -> void:
	var mouse_event := event as InputEventMouseButton
	if mouse_event != null:
		_handle_preview_mouse_button(mouse_event)
		return
	var motion_event := event as InputEventMouseMotion
	if motion_event != null:
		_handle_preview_mouse_motion(motion_event)

func _handle_preview_mouse_button(event: InputEventMouseButton) -> void:
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
		if event.button_index == MOUSE_BUTTON_LEFT and !preview_drag_moved:
			_place_stamp_at_preview_pos(event.position)
		preview_drag_button = 0
		preview_container.accept_event()

func _handle_preview_mouse_motion(event: InputEventMouseMotion) -> void:
	if preview_drag_button == 0:
		return
	var delta := event.position - preview_drag_last
	preview_drag_last = event.position
	if event.position.distance_to(preview_drag_start) > 4.0:
		preview_drag_moved = true
	if preview_drag_button == MOUSE_BUTTON_LEFT and !event.shift_pressed:
		preview_yaw += delta.x * 0.01
		preview_pitch = clampf(preview_pitch + delta.y * 0.008, deg_to_rad(-55.0), deg_to_rad(55.0))
	else:
		var pan_scale := preview_distance * 0.0015
		preview_pan.x += delta.x * pan_scale
		preview_pan.y -= delta.y * pan_scale
	_apply_preview_camera()
	preview_container.accept_event()

func _apply_preview_camera() -> void:
	if preview_vehicle != null:
		preview_vehicle.transform = _preview_vehicle_transform()
	if preview_camera == null:
		return
	preview_camera.position = Vector3(0.0, 4.0, preview_distance)
	preview_camera.look_at(Vector3(0.0, 0.5, 0.0), Vector3.UP)
	_submit_preview_render()

func _place_stamp_at_preview_pos(viewport_pos: Vector2) -> void:
	var hit := _raycast_preview_body(viewport_pos)
	if hit.is_empty():
		return
	var stamp := _selected_stamp()
	if stamp == null:
		_on_stamp_add_pressed()
		stamp = _selected_stamp()
	if stamp == null:
		return
	var car_inv := preview_vehicle.global_transform.affine_inverse()
	stamp.local_origin = car_inv * hit["position"]
	var hit_normal: Vector3 = hit["normal"]
	var local_normal := (car_inv.basis * hit_normal).normalized()
	_set_stamp_basis_from_normal(stamp, local_normal)
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

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
