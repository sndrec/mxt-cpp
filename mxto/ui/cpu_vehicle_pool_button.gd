class_name CpuVehiclePoolButton extends MenuButton

signal selection_changed(content_ids: Array)

const ALL_VEHICLES_ID := 1
const VEHICLE_ID_BASE := 100

var vehicle_content_controller: VehicleContentController
var available_content_ids: Array[String] = []
var selected_content_ids_by_id := {}
var initialized := false


func initialize(controller: VehicleContentController) -> void:
	vehicle_content_controller = controller
	var popup := get_popup()
	popup.hide_on_checkable_item_selection = false
	if !popup.id_pressed.is_connected(_on_item_pressed):
		popup.id_pressed.connect(_on_item_pressed)
	if vehicle_content_controller != null \
			and !vehicle_content_controller.garage_catalog_changed.is_connected(_refresh):
		vehicle_content_controller.garage_catalog_changed.connect(_refresh)
	if vehicle_content_controller != null \
			and !vehicle_content_controller.catalog_changed.is_connected(_refresh):
		vehicle_content_controller.catalog_changed.connect(_refresh)
	if vehicle_content_controller != null \
			and !vehicle_content_controller.catalog_delta.is_connected(_on_catalog_delta):
		vehicle_content_controller.catalog_delta.connect(_on_catalog_delta)
	_refresh()


func selected_content_ids() -> Array:
	var selected: Array = []
	for content_id in available_content_ids:
		if selected_content_ids_by_id.has(content_id):
			selected.append(content_id)
	return selected


func _on_catalog_delta(_delta: MxtContentCatalogDelta) -> void:
	_refresh()


func _refresh() -> void:
	var previously_all_selected := initialized \
		and !available_content_ids.is_empty() \
		and selected_content_ids_by_id.size() == available_content_ids.size()
	var next_ids: Array[String] = []
	var names_by_id := {}
	if vehicle_content_controller != null:
		for definition_value in vehicle_content_controller.get_garage_vehicle_definitions():
			var definition := definition_value as CarDefinition
			if definition == null or definition.content_id.is_empty():
				continue
			next_ids.append(definition.content_id)
			names_by_id[definition.content_id] = definition.name
	available_content_ids = next_ids
	if !initialized or previously_all_selected:
		selected_content_ids_by_id.clear()
		for content_id in available_content_ids:
			selected_content_ids_by_id[content_id] = true
	else:
		for content_id_value in selected_content_ids_by_id.keys():
			if !available_content_ids.has(String(content_id_value)):
				selected_content_ids_by_id.erase(content_id_value)
		if selected_content_ids_by_id.is_empty() and !available_content_ids.is_empty():
			selected_content_ids_by_id[available_content_ids[0]] = true
	initialized = true
	_rebuild_popup(names_by_id)


func _rebuild_popup(names_by_id: Dictionary) -> void:
	var popup := get_popup()
	popup.clear()
	popup.add_check_item("All vehicles", ALL_VEHICLES_ID)
	popup.set_item_checked(popup.get_item_index(ALL_VEHICLES_ID), _all_selected())
	popup.add_separator()
	for index in range(available_content_ids.size()):
		var content_id := available_content_ids[index]
		var item_id := VEHICLE_ID_BASE + index
		popup.add_check_item(String(names_by_id.get(content_id, content_id)), item_id)
		popup.set_item_checked(popup.get_item_index(item_id), selected_content_ids_by_id.has(content_id))
	disabled = available_content_ids.is_empty()
	_update_text()


func _on_item_pressed(item_id: int) -> void:
	if item_id == ALL_VEHICLES_ID:
		selected_content_ids_by_id.clear()
		for content_id in available_content_ids:
			selected_content_ids_by_id[content_id] = true
	elif item_id >= VEHICLE_ID_BASE:
		var index := item_id - VEHICLE_ID_BASE
		if index < 0 or index >= available_content_ids.size():
			return
		var content_id := available_content_ids[index]
		if selected_content_ids_by_id.has(content_id):
			if selected_content_ids_by_id.size() > 1:
				selected_content_ids_by_id.erase(content_id)
		else:
			selected_content_ids_by_id[content_id] = true
	_update_popup_checks()
	_update_text()
	selection_changed.emit(selected_content_ids())


func _update_popup_checks() -> void:
	var popup := get_popup()
	popup.set_item_checked(popup.get_item_index(ALL_VEHICLES_ID), _all_selected())
	for index in range(available_content_ids.size()):
		var item_id := VEHICLE_ID_BASE + index
		popup.set_item_checked(
			popup.get_item_index(item_id),
			selected_content_ids_by_id.has(available_content_ids[index]))


func _all_selected() -> bool:
	return !available_content_ids.is_empty() \
		and selected_content_ids_by_id.size() == available_content_ids.size()


func _update_text() -> void:
	var selected_count := selected_content_ids_by_id.size()
	if _all_selected():
		text = "All vehicles (%d)" % available_content_ids.size()
	else:
		text = "%d of %d vehicles" % [selected_count, available_content_ids.size()]
	tooltip_text = "Choose which machines CPU racers may use. At least one remains selected."
