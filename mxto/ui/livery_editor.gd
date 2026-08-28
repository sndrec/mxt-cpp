class_name LiveryEditor
extends Control

signal livery_reference_changed(livery: CarLivery, enabled: bool)

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const CarStampCatalog = preload("res://vehicle/customization/car_stamp_catalog.gd")
const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CarRenderManager = preload("res://vehicle/car_render_manager.gd")
const GaragePreviewCameraControllerClass = preload("res://ui/garage_preview_camera_controller.gd")
const GARAGE_PREVIEW_WORLD_SCENE = preload("res://ui/garage_preview_world.tscn")

const STAMP_EDIT_MIN_SCREEN_SIZE := 1.0
const PREVIEW_TARGET_HEIGHT := 0.5

@onready var owner_ui: Control = get_parent() as Control
@onready var car_preview_space: ColorRect = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace")
@onready var camera_realign_x: Button = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CameraRealignControls/X")
@onready var camera_realign_y: Button = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CameraRealignControls/Y")
@onready var camera_realign_z: Button = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CameraRealignControls/Z")
@onready var primary_colour_picker: ColorPickerButton = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/PaintGrid/PrimaryColourPicker")
@onready var secondary_colour_picker: ColorPickerButton = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/PaintGrid/SecondaryColourPicker")
@onready var accent_colour_picker: ColorPickerButton = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/PaintGrid/AccentColourPicker")
@onready var outline_colour_picker: ColorPickerButton = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/PaintGrid/OutlineColourPicker")
@onready var trail_colour_picker: ColorPickerButton = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/PaintGrid/TrailColourPicker")
@onready var garage_tab: HBoxContainer = owner_ui.get_node("Container/SettingsTabs/Garage")
@onready var garage_panel: VBoxContainer = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel")
@onready var garage_car_name_label: Label = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/GarageCarName")
@onready var stamp_layer_list: VBoxContainer = owner_ui.get_node("Container/SettingsTabs/Garage/GaragePanel/StampLayerScroll/StampLayerList")
@onready var stamp_action_menu: PopupMenu = owner_ui.get_node("StampActionMenu")
@onready var stamp_properties_popup: PopupPanel = owner_ui.get_node("StampPropertiesPopup")
@onready var stamp_properties_rotation: SpinBox = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/RotationRow/RotationSpin")
@onready var stamp_properties_scale_x: SpinBox = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/ScaleXRow/ScaleXSpin")
@onready var stamp_properties_scale_y: SpinBox = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/ScaleYRow/ScaleYSpin")
@onready var stamp_properties_flip_horizontal: CheckBox = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/FlipHorizontal")
@onready var stamp_properties_flip_vertical: CheckBox = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/FlipVertical")
@onready var stamp_properties_mirror_local_x: CheckBox = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/MirrorLocalX")
@onready var stamp_properties_close_button: Button = owner_ui.get_node("StampPropertiesPopup/PropertiesRoot/Close")
@onready var stamp_chooser_popup: PopupPanel = owner_ui.get_node("StampChooser")
@onready var stamp_chooser_title: Label = owner_ui.get_node("StampChooser/StampChooserList/Title")
@onready var stamp_chooser_tabs: TabContainer = owner_ui.get_node("StampChooser/StampChooserList/StampChooserTabs")
@onready var stamp_chooser_base_list: GridContainer = owner_ui.get_node("StampChooser/StampChooserList/StampChooserTabs/Base/BaseList")
@onready var stamp_chooser_custom_list: GridContainer = owner_ui.get_node("StampChooser/StampChooserList/StampChooserTabs/Custom/CustomList")
@onready var stamp_chooser_cancel_button: Button = owner_ui.get_node("StampChooser/StampChooserList/Cancel")
@onready var stamp_library: CustomStampLibraryController = $CustomStampLibraryController
@onready var stamp_painter: CustomStampPainterController = $CustomStampPainterController
@onready var atlas_controller: LiveryAtlasController = $LiveryAtlasController
@onready var stamp_edit_overlay: Control = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay")
@onready var stamp_edit_square: Panel = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay/StampEditSquare")
@onready var stamp_edit_confirm_button: Button = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay/Confirm")
@onready var stamp_edit_cancel_button: Button = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/StampEditOverlay/Cancel")

var game_manager: GameManager
var player_settings: PlayerSettings
var selected_definition: CarDefinition
var current_livery: CarLivery = CarLivery.new()
var current_livery_enabled := false
var selected_stamp_index := -1
var stamp_catalog: CarStampCatalog = preload("res://vehicle/customization/stamp_catalog.tres")
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
var preview_container: SubViewportContainer
var preview_viewport: SubViewport
var preview_root: Node3D
var preview_camera: Camera3D
var preview_vehicle: Node3D
var preview_vehicle_base_transform := Transform3D.IDENTITY
var preview_render_manager: CarRenderManager
var preview_edit_render_manager: CarRenderManager
var preview_above_render_manager: CarRenderManager
var preview_camera_controller := GaragePreviewCameraControllerClass.new()
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

func initialize(in_game_manager: GameManager, in_player_settings: PlayerSettings) -> void:
	game_manager = in_game_manager
	player_settings = in_player_settings
	_build_stamp_layer_buttons()
	_setup_garage_preview()
	_setup_stamp_menus()
	_setup_stamp_properties_popup()
	stamp_library.initialize(owner_ui)
	stamp_library.paint_requested.connect(_on_custom_stamp_paint_pressed)
	stamp_library.edit_requested.connect(_edit_custom_stamp_from_catalog)
	stamp_library.delete_requested.connect(_delete_custom_stamp_from_catalog)
	stamp_painter.initialize(owner_ui)
	stamp_painter.stamp_saved.connect(_on_custom_stamp_painter_saved)
	atlas_controller.initialize(owner_ui)
	atlas_controller.atlas_texture_changed.connect(_on_preview_atlas_texture_changed)
	primary_colour_picker.color_changed.connect(_on_primary_colour_changed)
	secondary_colour_picker.color_changed.connect(_on_secondary_colour_changed)
	accent_colour_picker.color_changed.connect(_on_accent_colour_changed)
	outline_colour_picker.color_changed.connect(_on_outline_colour_changed)
	trail_colour_picker.color_changed.connect(_on_trail_colour_changed)

func load_vehicle(definition: CarDefinition, rebuild_preview := true) -> void:
	selected_definition = definition
	if garage_car_name_label != null:
		garage_car_name_label.text = definition.name if definition != null else ""
	_load_livery_for_selected_car()
	_update_livery_controls()
	_refresh_stamp_controls()
	if rebuild_preview:
		_rebuild_preview_vehicle()
	_update_livery_lock_state()

func flush_livery(rebuild_preview := false) -> void:
	if livery_dirty and !_livery_editing_locked():
		_save_livery_for_selected_car(rebuild_preview)
	publish_livery_reference()

func refresh_custom_stamp_library() -> void:
	stamp_library.refresh()

func update_lock_state() -> void:
	_update_livery_lock_state()

func set_active(active: bool) -> void:
	_set_garage_preview_active(active)


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

func _input(event: InputEvent) -> void:
	if !owner_ui.visible or stamp_drag_source_layer < 0:
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


func _load_livery_for_selected_car() -> void:
	if selected_definition == null or selected_definition.content_id.is_empty():
		current_livery = CarLivery.new()
		current_livery_enabled = false
		publish_livery_reference()
		return
	current_livery_enabled = CarLiveryStore.has_for_car(selected_definition.content_id)
	var livery: CarLivery = CarLiveryStore.load_for_car(selected_definition.content_id)
	livery.vehicle_content_id = selected_definition.content_id
	current_livery = livery
	publish_livery_reference()

func _save_livery_for_selected_car(rebuild_preview := true) -> void:
	if _livery_editing_locked():
		return
	if selected_definition == null or selected_definition.content_id.is_empty():
		publish_livery_reference()
		return
	current_livery_enabled = true
	current_livery.vehicle_content_id = selected_definition.content_id
	var err := CarLiveryStore.save_for_car(current_livery)
	if err != OK:
		push_warning("Failed to save car livery: %s" % err)
	publish_livery_reference()
	livery_dirty = false
	if rebuild_preview:
		_rebuild_preview_vehicle()

func _mark_livery_dirty() -> void:
	if _livery_editing_locked():
		return
	livery_dirty = true
	current_livery_enabled = true
	publish_livery_reference()

func _livery_editing_locked() -> bool:
	return game_manager != null and !game_manager.singleplayer_mode and game_manager.network_manager != null and game_manager.network_manager.has_network_peer()


func _update_livery_lock_state() -> void:
	var locked := _livery_editing_locked()
	primary_colour_picker.disabled = locked
	secondary_colour_picker.disabled = locked
	accent_colour_picker.disabled = locked
	outline_colour_picker.disabled = locked
	trail_colour_picker.disabled = locked
	if garage_panel != null:
		garage_panel.modulate = Color(0.55, 0.55, 0.55, 1.0) if locked else Color.WHITE
	stamp_library.set_locked(locked)
	stamp_painter.set_locked(locked)
	for button in stamp_layer_buttons:
		button.disabled = locked
	for layer in range(stamp_layer_colour_pickers.size()):
		var colour_picker := stamp_layer_colour_pickers[layer]
		if colour_picker != null:
			colour_picker.disabled = locked or _stamp_for_layer(layer) == null


func _process(_delta: float) -> void:
	if owner_ui != null and owner_ui.visible:
		atlas_controller.poll()
		_update_livery_lock_state()

func _exit_tree() -> void:
	atlas_controller.finish()

func publish_livery_reference() -> void:
	if selected_definition == null or !current_livery_enabled:
		livery_reference_changed.emit(null, false)
		return
	current_livery.vehicle_content_id = selected_definition.content_id
	livery_reference_changed.emit(current_livery, true)


func _update_livery_controls() -> void:
	updating_colour_controls = true
	primary_colour_picker.color = current_livery.primary_colour
	secondary_colour_picker.color = current_livery.secondary_colour
	accent_colour_picker.color = current_livery.accent_colour
	outline_colour_picker.color = current_livery.outline_colour if current_livery.outline_colour_customized else _authored_outline_colour(false)
	trail_colour_picker.color = current_livery.trail_colour if current_livery.trail_colour_customized else _authored_outline_colour(true)
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

func _on_outline_colour_changed(colour: Color) -> void:
	if updating_colour_controls or _livery_editing_locked():
		return
	current_livery.outline_colour = colour
	current_livery.outline_colour_customized = true
	_apply_preview_livery_colours()
	_save_livery_for_selected_car(false)

func _on_trail_colour_changed(colour: Color) -> void:
	if updating_colour_controls or _livery_editing_locked():
		return
	current_livery.trail_colour = colour
	current_livery.trail_colour_customized = true
	_apply_preview_livery_colours()
	_save_livery_for_selected_car(false)

func _authored_outline_colour(trail: bool) -> Color:
	var fallback := Color(0.25, 0.55, 1.0, 1.0)
	var definition := _selected_car_definition()
	if definition == null or definition.car_scene == null:
		return fallback
	var template := definition.car_scene.instantiate() as Node3D
	if template == null:
		return fallback
	var node_name := "VEHICLE_OUTLINE" if trail else "VEHICLE_OUTLINE_MAIN"
	var mesh := template.get_node_or_null(node_name) as MeshInstance3D
	var material := mesh.material_override as ShaderMaterial if mesh != null else null
	var parameter = material.get_shader_parameter("trail_colour" if trail else "outline_color") if material != null else null
	template.free()
	if parameter is Color:
		return parameter
	if parameter is Vector3:
		return Color(parameter.x, parameter.y, parameter.z, 1.0)
	return fallback

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
	preview_container.tooltip_text = "Left drag: orbit. Hold Ctrl to snap rotation to world 5° increments. Right, middle, or Shift+left drag: pan. Wheel: zoom. Double-click: reset view."
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
	preview_camera_controller.configure_frame(Vector3(0.0, PREVIEW_TARGET_HEIGHT, 0.0), 22.0, true)
	camera_realign_x.pressed.connect(_realign_preview_camera.bind(Vector3.AXIS_X))
	camera_realign_y.pressed.connect(_realign_preview_camera.bind(Vector3.AXIS_Y))
	camera_realign_z.pressed.connect(_realign_preview_camera.bind(Vector3.AXIS_Z))
	_setup_stamp_edit_overlay()
	_apply_preview_camera()
	_set_garage_preview_active(owner_ui.visible)

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
	var definition := _selected_car_definition()
	if preview_vehicle != null and is_instance_valid(preview_vehicle):
		preview_vehicle.queue_free()
	preview_vehicle = null
	if preview_render_manager != null:
		preview_render_manager.clear_renderer()
	if preview_edit_render_manager != null:
		preview_edit_render_manager.clear_renderer()
	if preview_above_render_manager != null:
		preview_above_render_manager.clear_renderer()
	if definition == null or !definition.has_visual():
		return
	if definition.car_scene != null:
		preview_vehicle = definition.car_scene.instantiate()
	else:
		preview_vehicle = Node3D.new()
		var mesh_instance := MeshInstance3D.new()
		mesh_instance.mesh = definition.runtime_mesh
		mesh_instance.material_override = definition.runtime_material
		mesh_instance.transform = definition.runtime_transform
		preview_vehicle.add_child(mesh_instance)
	preview_vehicle_base_transform = preview_vehicle.transform
	preview_root.add_child(preview_vehicle)
	_hide_preview_raycast_scene(preview_vehicle)
	atlas_controller.refresh(current_livery)
	var render_settings: Array = [_preview_render_settings(_preview_confirmed_livery_below())]
	preview_render_manager.stamp_visibility_masks_enabled = true
	preview_render_manager.stamp_visibility_mask_skip_layer = -1
	preview_render_manager.stamp_only_mode = false
	preview_render_manager.stamp_render_priority = 2
	preview_render_manager.set_custom_stamp_atlas(atlas_controller.atlas_texture)
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
	if definition == null or !definition.has_visual() or stamp_ui_mode != StampUiMode.EDITING:
		if apply_camera:
			_apply_preview_camera()
		return
	preview_edit_render_manager.stamp_visibility_masks_enabled = false
	preview_edit_render_manager.stamp_visibility_mask_skip_layer = -1
	preview_edit_render_manager.stamp_only_mode = true
	preview_edit_render_manager.stamp_render_priority = 3
	preview_edit_render_manager.set_custom_stamp_atlas(atlas_controller.atlas_texture)
	preview_edit_render_manager.configure_manual([definition], [_preview_render_settings(_preview_edit_livery())])
	if apply_camera:
		_apply_preview_camera()

func _rebuild_above_stamp_preview(apply_camera := true) -> void:
	if preview_above_render_manager == null:
		return
	preview_above_render_manager.clear_renderer()
	var definition := _selected_car_definition()
	if definition == null or !definition.has_visual() or stamp_ui_mode != StampUiMode.EDITING:
		if apply_camera:
			_apply_preview_camera()
		return
	preview_above_render_manager.stamp_visibility_masks_enabled = true
	preview_above_render_manager.stamp_visibility_mask_skip_layer = -1
	preview_above_render_manager.stamp_only_mode = true
	preview_above_render_manager.stamp_render_priority = 4
	preview_above_render_manager.set_custom_stamp_atlas(atlas_controller.atlas_texture)
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
		livery.vehicle_content_id = selected_definition.content_id if selected_definition != null else ""
		settings["car_livery"] = livery.to_dict()
	return settings

func _on_preview_atlas_texture_changed(texture: Texture2D) -> void:
	for manager in [preview_render_manager, preview_edit_render_manager, preview_above_render_manager]:
		if manager != null:
			manager.set_custom_stamp_atlas(texture)

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
	out.vehicle_content_id = selected_definition.content_id if selected_definition != null else ""
	out.primary_colour = current_livery.primary_colour
	out.secondary_colour = current_livery.secondary_colour
	out.accent_colour = current_livery.accent_colour
	return out

func _selected_car_definition() -> CarDefinition:
	return selected_definition

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
		var blob := stamp_library.blob_for_hash(stamp.custom_hash)
		return stamp_library.preview_texture(blob) if blob != null else null
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
	if atlas_controller.refresh(current_livery):
		return true
	_set_stamp_for_layer(layer, old_copy)
	atlas_controller.refresh(current_livery)
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
	stamp_library.refresh()
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
	for blob in stamp_library.blobs:
		var custom_blob := blob as CustomStampBlob
		if custom_blob == null:
			continue
		var button := _make_stamp_choice_image_button(stamp_library.preview_texture(custom_blob))
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
			atlas_controller.refresh(current_livery)
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
	var blob := stamp_library.blob_for_hash(stamp_hash)
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

func _on_custom_stamp_paint_pressed() -> void:
	stamp_painter.open_new()

func _on_stamp_choice_cancel_pressed() -> void:
	stamp_chooser_popup.hide()
	stamp_ui_mode = StampUiMode.IDLE
	pending_stamp_layer = -1
	pending_stamp_choice_action = ""

func _edit_custom_stamp_from_catalog(stamp_hash: String, blob: CustomStampBlob) -> void:
	if blob == null or blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE:
		push_warning("8bpp palette stamps cannot be edited in the painter yet.")
		return
	stamp_painter.open_blob(stamp_hash, blob)

func _delete_custom_stamp_from_catalog(stamp_hash: String) -> void:
	var removed_from_current_livery := false
	for i in range(current_livery.stamps.size() - 1, -1, -1):
		var stamp := current_livery.stamps[i]
		if stamp != null and stamp.is_custom() and stamp.custom_hash == stamp_hash:
			current_livery.stamps.remove_at(i)
			removed_from_current_livery = true
	if !stamp_library.delete_blob(stamp_hash):
		return
	stamp_painter.cancel_if_editing(stamp_hash)
	if removed_from_current_livery:
		atlas_controller.refresh(current_livery)
		_save_livery_for_selected_car()
		_refresh_stamp_controls()

func _on_custom_stamp_painter_saved(previous_hash: String, blob: CustomStampBlob) -> void:
	if !previous_hash.is_empty() and previous_hash != blob.stamp_hash:
		_replace_custom_stamp_references(previous_hash, blob)
		stamp_library.erase_preview(previous_hash)
	stamp_library.refresh()

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
	atlas_controller.refresh(current_livery)
	_save_livery_for_selected_car()
	_refresh_stamp_controls()

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
	preview_camera_controller.yaw = atan2(view_direction.x, view_direction.z)
	var stamp_elevation := asin(clampf(view_direction.y, -1.0, 1.0))
	preview_camera_controller.pitch = clampf(-stamp_elevation, deg_to_rad(-90.0), deg_to_rad(55.0))
	var plane_basis := preview_camera_controller.view_plane_basis(preview_camera_controller.camera_offset())
	var relative_origin := projector.origin - Vector3(0.0, PREVIEW_TARGET_HEIGHT, 0.0)
	preview_camera_controller.pan = Vector3(
		clampf(relative_origin.dot(plane_basis.x), -GaragePreviewCameraControllerClass.PAN_LIMIT, GaragePreviewCameraControllerClass.PAN_LIMIT),
		clampf(relative_origin.dot(plane_basis.y), -GaragePreviewCameraControllerClass.PAN_LIMIT, GaragePreviewCameraControllerClass.PAN_LIMIT),
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
		var is_camera_release := !mouse_button.pressed and preview_camera_controller.drag_button == mouse_button.button_index
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
		if preview_camera_controller.drag_button == 0 and !_stamp_edit_allows_camera_input(motion.position):
			return
		preview_has_camera_override = false
		var before_yaw := preview_camera_controller.yaw
		var before_pitch := preview_camera_controller.pitch
		var before_pan := preview_camera_controller.pan
		_handle_preview_mouse_motion(motion, true)
		if !is_equal_approx(before_yaw, preview_camera_controller.yaw) or !is_equal_approx(before_pitch, preview_camera_controller.pitch) or !before_pan.is_equal_approx(preview_camera_controller.pan):
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
	if preview_camera_controller.handle_mouse_button(event):
		_apply_preview_camera()
		preview_container.accept_event()

func _handle_preview_mouse_motion(event: InputEventMouseMotion, allow_edit_camera := false) -> void:
	if stamp_ui_mode == StampUiMode.EDITING and !allow_edit_camera:
		return
	if preview_camera_controller.handle_mouse_motion(event):
		_apply_preview_camera()
		preview_container.accept_event()


func _realign_preview_camera(axis: int) -> void:
	preview_has_camera_override = false
	preview_camera_controller.realign_pivot_axis(axis)
	_apply_preview_camera()
	if stamp_ui_mode == StampUiMode.EDITING:
		_apply_edit_stamp_from_camera()

func _apply_preview_camera() -> void:
	if preview_vehicle != null:
		preview_vehicle.transform = _preview_vehicle_scene_transform()
	if preview_camera == null:
		return
	if preview_has_camera_override:
		preview_camera.global_transform = preview_camera_override
	else:
		preview_camera_controller.apply(preview_camera)
	_submit_preview_render()

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
