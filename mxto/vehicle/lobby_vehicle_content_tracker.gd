class_name LobbyVehicleContentTracker
extends Node

const WORKSHOP_VEHICLE_PREFIX := "mxt:vehicle:workshop:"
const DOWNLOAD_RETRY_BASE_MSEC := 3000
const DOWNLOAD_RETRY_MAX_MSEC := 30000

var steam_service: MxtSteamService
var content_catalog: MxtContentCatalog
var known_workshop_items := {}
var subscribed_workshop_items := {}
var session_item_ids := {}
var download_requests := {}
var download_failures := {}
var ready_packages := {}


func initialize(
		in_steam_service: MxtSteamService,
		in_content_catalog: MxtContentCatalog,
		catalog_owner: VehicleContentController,
		network_manager: NetworkManager) -> void:
	steam_service = in_steam_service
	content_catalog = in_content_catalog
	steam_service.workshop_items_changed.connect(_on_workshop_items_changed)
	steam_service.workshop_request_completed.connect(_on_workshop_request_completed)
	catalog_owner.catalog_delta.connect(_on_catalog_delta)
	network_manager.session_ended.connect(end_lobby_lifetime)
	_on_workshop_items_changed(steam_service.get_workshop_items())


func request_vehicle_content(settings: Dictionary) -> bool:
	var workshop_id_text := String(settings.get("vehicle_workshop_id", ""))
	if !workshop_id_text.is_valid_int():
		return false
	var workshop_id := workshop_id_text.to_int()
	var content_id := String(settings.get("vehicle_content_id", ""))
	var package_digest := String(settings.get("vehicle_package_digest", ""))
	if workshop_id <= 0 or content_id != WORKSHOP_VEHICLE_PREFIX + str(workshop_id):
		return false
	if String(ready_packages.get(workshop_id, "")) == package_digest:
		return true
	var record: MxtContentRecord = content_catalog.resolve_content(content_id)
	var expected_gameplay_digest := String(settings.get("vehicle_gameplay_digest", ""))
	var local_published_file_id := str(record.published_file_id) if record != null and record.published_file_id > 0 else ""
	var local_gameplay_digest := record.gameplay_digest if record != null else ""
	var local_package_digest := record.package_digest if record != null else ""
	if (
			local_published_file_id == workshop_id_text
			and local_gameplay_digest == expected_gameplay_digest
			and local_package_digest == package_digest):
		download_requests.erase(workshop_id)
		download_failures.erase(workshop_id)
		ready_packages[workshop_id] = package_digest
		return true
	if steam_service == null or !steam_service.is_initialized():
		return false
	if !subscribed_workshop_items.has(workshop_id):
		session_item_ids[workshop_id] = true
	if !known_workshop_items.has(workshop_id):
		if steam_service.track_workshop_item(workshop_id):
			steam_service.refresh_workshop_items()
		return false
	var failure_count := int(download_failures.get(workshop_id, 0))
	var retry_delay_msec := mini(
		DOWNLOAD_RETRY_BASE_MSEC * (1 << mini(failure_count, 4)),
		DOWNLOAD_RETRY_MAX_MSEC)
	var now_msec := Time.get_ticks_msec()
	var last_request_msec := int(download_requests.get(workshop_id, -retry_delay_msec))
	if now_msec < last_request_msec + retry_delay_msec:
		return false
	download_requests[workshop_id] = now_msec
	# Completion and installation callbacks perform one shared catalog refresh,
	# preventing a synchronous rebuild for every missing lobby item.
	var accepted := steam_service.download_workshop_item(workshop_id, true)
	if !accepted:
		download_failures[workshop_id] = failure_count + 1
	return false


func track_temporary_item(workshop_id: int) -> bool:
	if workshop_id <= 0 or steam_service == null or !steam_service.is_initialized():
		return false
	if !subscribed_workshop_items.has(workshop_id):
		session_item_ids[workshop_id] = true
	return steam_service.track_workshop_item(workshop_id)


func end_lobby_lifetime() -> void:
	if steam_service != null and steam_service.is_initialized():
		var changed := false
		for workshop_id_value in session_item_ids:
			var workshop_id := int(workshop_id_value)
			if subscribed_workshop_items.has(workshop_id):
				continue
			changed = steam_service.untrack_workshop_item(workshop_id) or changed
		if changed:
			steam_service.refresh_workshop_items()
	session_item_ids.clear()
	download_requests.clear()
	download_failures.clear()
	ready_packages.clear()


func _on_workshop_items_changed(items: Array) -> void:
	known_workshop_items.clear()
	subscribed_workshop_items.clear()
	for value in items:
		var item: Dictionary = value
		var workshop_id := int(item.get("published_file_id", 0))
		if workshop_id <= 0:
			continue
		known_workshop_items[workshop_id] = true
		if bool(item.get("subscribed", false)):
			subscribed_workshop_items[workshop_id] = true


func _on_catalog_delta(delta: MxtContentCatalogDelta) -> void:
	for workshop_id in delta.changed_item_ids:
		ready_packages.erase(int(workshop_id))
	for workshop_id in delta.removed_item_ids:
		ready_packages.erase(int(workshop_id))


func _on_workshop_request_completed(_request_id: int, operation: String, result: Dictionary) -> void:
	var workshop_id := int(result.get("published_file_id", 0))
	if workshop_id <= 0:
		return
	if operation == "download":
		if bool(result.get("success", false)):
			download_failures.erase(workshop_id)
		else:
			download_failures[workshop_id] = int(download_failures.get(workshop_id, 0)) + 1
	elif operation == "item_installed":
		download_failures.erase(workshop_id)
