class_name VehicleContentController
extends Node

signal workshop_content_changed(items: Array)
signal catalog_changed
signal catalog_delta(delta: MxtContentCatalogDelta)
signal garage_catalog_changed

const OFFICIAL_VEHICLE_PREFIX := "mxt:vehicle:official:"
const WORKSHOP_VEHICLE_PREFIX := "mxt:vehicle:workshop:"
const LOCAL_CONTENT_LIBRARY_PATH := "user://content/packages"
const TEST_DRIVE_SNAPSHOT_LIBRARY_PATH := "user://content/test_drive_snapshots"
const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const CustomStampAtlasBuilder = preload("res://vehicle/customization/custom_stamp_atlas_builder.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")

var content_catalog: MxtContentCatalog = MxtContentCatalog.new()
var definitions: Array = []
var definitions_by_content_id: Dictionary = {}
var workshop_content_items: Array = []
var subscribed_workshop_item_ids := {}
var steam_service: MxtSteamService
var custom_stamp_network: CustomStampNetworkController
var runtime_content_loaded := false

func initialize(in_steam_service: MxtSteamService, in_custom_stamp_network: CustomStampNetworkController) -> void:
	steam_service = in_steam_service
	custom_stamp_network = in_custom_stamp_network
	steam_service.workshop_items_changed.connect(_on_workshop_items_changed)
	_scan_local_content_library()
	var initial_workshop_items := steam_service.get_workshop_items()
	if !initial_workshop_items.is_empty():
		_on_workshop_items_changed(initial_workshop_items)
	reload_definitions()
	runtime_content_loaded = true

func get_vehicle_content_ids() -> Array:
	var content_ids: Array = []
	for definition in definitions:
		if definition is CarDefinition and definition.content_id != "":
			content_ids.append(definition.content_id)
	return content_ids

func get_garage_vehicle_definitions() -> Array:
	var garage_definitions: Array = []
	for definition_value in definitions:
		var definition := definition_value as CarDefinition
		if definition == null:
			continue
		var record: MxtContentRecord = content_catalog.resolve_content(definition.content_id)
		if record == null:
			continue
		var source := record.source_name
		if source == "official":
			garage_definitions.append(definition)
			continue
		if source != "workshop":
			continue
		if record.published_file_id > 0 \
				and subscribed_workshop_item_ids.has(record.published_file_id):
			garage_definitions.append(definition)
	return garage_definitions

func get_multiplayer_vehicle_content_ids(allow_workshop: bool) -> Array:
	var content_ids: Array = []
	for definition in definitions:
		if definition is CarDefinition and is_multiplayer_vehicle_content(definition.content_id, allow_workshop):
			content_ids.append(definition.content_id)
	return content_ids

func is_multiplayer_vehicle_content(content_id: String, allow_workshop: bool) -> bool:
	var record: MxtContentRecord = content_catalog.resolve_content(content_id)
	if record == null:
		return false
	var source := record.source_name
	if source == "official":
		return true
	return allow_workshop and source == "workshop" and record.published_file_id > 0

func get_definition(vehicle_content_id: String) -> CarDefinition:
	var definition := definitions_by_content_id.get(vehicle_content_id) as CarDefinition
	if definition != null:
		return definition
	var record: MxtContentRecord = content_catalog.resolve_content(vehicle_content_id)
	if record == null or record.source != MxtContentRecord.SOURCE_LOCAL_DRAFT \
			or record.content_type != MxtContentRecord.CONTENT_VEHICLE:
		return null
	definition = VehicleRuntimeAssetFactory.create_from_package_record(record)
	if definition != null:
		definitions_by_content_id[vehicle_content_id] = definition
	return definition

func vehicle_settings_content_available(settings: Array) -> bool:
	for value in settings:
		if typeof(value) != TYPE_DICTIONARY:
			return false
		var player_settings: Dictionary = value
		var content_id := String(player_settings.get("vehicle_content_id", ""))
		var gameplay_digest := String(player_settings.get("vehicle_gameplay_digest", ""))
		if content_id.is_empty() or gameplay_digest.is_empty():
			return false
		var record: MxtContentRecord = content_catalog.resolve_content(content_id)
		if record == null or record.gameplay_digest != gameplay_digest:
			return false
		if !record.package_digest.is_empty() and String(player_settings.get(
				"vehicle_package_digest", "")) != record.package_digest:
			return false
		var workshop_id := str(record.published_file_id) if record.published_file_id > 0 else ""
		if !workshop_id.is_empty() and String(player_settings.get(
				"vehicle_workshop_id", "")) != workshop_id:
			return false
	return true

func prepare_custom_stamp_render_payload(racer_ids: Array, racer_settings: Array, warning_context := "race") -> Dictionary:
	var render_data := prepare_custom_stamp_render_data(racer_ids, racer_settings, warning_context)
	var render_settings: Array = render_data.get("settings", racer_settings)
	for i in range(render_settings.size()):
		if render_settings[i] is PlayerSettings:
			continue
		var normalized := player_settings_for_stamp_render(render_settings[i])
		if normalized != null:
			render_settings[i] = normalized
	if !bool(render_data.get("ok", false)):
		return {"texture": null, "settings": render_settings}
	var player_records: Array = render_data.get("player_records", [])
	if player_records.is_empty():
		return {"texture": null, "settings": render_settings}
	var atlas_build := CustomStampAtlasBuilder.build_atlas_image(player_records)
	if !bool(atlas_build.get("ok", false)):
		push_warning("Failed to build custom stamp %s atlas: %s" % [warning_context, str(atlas_build.get("error", "unknown error"))])
		return {"texture": null, "settings": render_settings}
	return {
		"texture": CustomStampAtlasBuilder.texture_from_image(atlas_build.get("image", null) as Image),
		"settings": render_settings,
	}


func prepare_custom_stamp_render_data(racer_ids: Array, racer_settings: Array, warning_context := "race") -> Dictionary:
	var render_settings: Array = racer_settings.duplicate()
	if racer_ids.is_empty() or racer_settings.is_empty():
		return {"ok": true, "settings": render_settings, "player_records": []}
	var manifests := {}
	var local_payloads := {}
	for i in range(mini(racer_ids.size(), render_settings.size())):
		var racer_id := int(racer_ids[i])
		var manifest := custom_stamp_network.get_custom_stamp_manifest(racer_id)
		if manifest.is_empty() and _settings_have_custom_stamp(render_settings[i]):
			var payload := build_custom_stamp_payload(render_settings[i])
			if bool(payload.get("ok", false)):
				manifest = payload.get("manifest", [])
				if !manifest.is_empty():
					local_payloads[racer_id] = payload
			else:
				push_warning("Failed to prepare local custom stamps for %s: %s" % [warning_context, str(payload.get("error", "unknown error"))])
		if !manifest.is_empty():
			manifests[racer_id] = manifest
	if manifests.is_empty():
		return {"ok": true, "settings": render_settings, "player_records": []}
	var region_build := CustomStampAtlasBuilder.allocate_player_regions(racer_ids, manifests)
	if !bool(region_build.get("ok", false)):
		push_warning("Failed to allocate custom stamp player regions for %s: %s" % [warning_context, str(region_build.get("error", "unknown error"))])
		return {"ok": false, "error": region_build.get("error", "unknown error"), "settings": render_settings, "player_records": []}
	var regions: Dictionary = region_build.get("regions", {})
	var player_records: Array = []
	for i in range(mini(racer_ids.size(), render_settings.size())):
		var player_id := int(racer_ids[i])
		if !manifests.has(player_id) or !regions.has(player_id):
			continue
		var manifest: Array = manifests[player_id]
		var region: Dictionary = regions[player_id]
		var placements := {}
		var blobs: Array = []
		for raw_entry in manifest:
			if typeof(raw_entry) != TYPE_DICTIONARY:
				continue
			var entry: Dictionary = raw_entry
			var stamp_hash: String = str(entry.get("hash", ""))
			if stamp_hash == "":
				continue
			var blob = _custom_stamp_blob_for_hash(player_id, stamp_hash, local_payloads)
			if blob == null:
				push_warning("Missing custom stamp blob for %s atlas: %s" % [warning_context, stamp_hash])
				return {"ok": false, "error": "missing custom stamp blob", "settings": render_settings, "player_records": []}
			var rect := _custom_stamp_manifest_rect(entry)
			var region_size := _custom_stamp_manifest_region_size(entry)
			if rect.size.x <= 0 or rect.size.y <= 0 or region_size == Vector2i.ZERO:
				push_warning("Invalid custom stamp packed rect for %s atlas: %s" % [warning_context, stamp_hash])
				return {"ok": false, "error": "invalid custom stamp packed rect", "settings": render_settings, "player_records": []}
			placements[stamp_hash] = {
				"rect": rect,
				"rotated": bool(entry.get("rect_rotated", false)),
				"region_size": region_size,
			}
			blobs.append(blob)
		if placements.is_empty():
			continue
		var region_origin: Vector2i = region["origin"]
		var normalized := player_settings_for_stamp_render(render_settings[i])
		if normalized == null:
			continue
		render_settings[i] = normalized
		_apply_custom_stamp_manifest_to_settings(render_settings[i], manifest, region_origin)
		player_records.append({
			"player_id": player_id,
			"region_origin": region_origin,
			"placements": placements,
			"blobs": blobs,
		})
	return {
		"ok": true,
		"settings": render_settings,
		"player_records": player_records,
	}


func _settings_have_custom_stamp(settings) -> bool:
	var livery_dict: Dictionary
	if settings is PlayerSettings:
		livery_dict = (settings as PlayerSettings).car_livery
	elif typeof(settings) == TYPE_DICTIONARY:
		var settings_dict: Dictionary = settings
		if typeof(settings_dict.get("car_livery", null)) == TYPE_DICTIONARY:
			livery_dict = settings_dict.get("car_livery", {})
	if livery_dict.is_empty():
		return false
	var stamps = livery_dict.get("stamps", [])
	if typeof(stamps) != TYPE_ARRAY:
		return false
	for raw_stamp in stamps:
		if typeof(raw_stamp) != TYPE_DICTIONARY:
			continue
		var stamp: Dictionary = raw_stamp
		if String(stamp.get("source", "base")) == "custom" or !String(stamp.get("hash", "")).is_empty():
			return true
	return false

func player_settings_for_stamp_render(settings) -> PlayerSettings:
	if settings is PlayerSettings:
		var copy := PlayerSettings.new()
		copy.from_dict((settings as PlayerSettings).to_dict())
		return copy
	elif typeof(settings) == TYPE_DICTIONARY:
		var copy := PlayerSettings.new()
		copy.from_dict(settings)
		return copy
	return null

func build_custom_stamp_payload(settings) -> Dictionary:
	var player_settings := player_settings_for_stamp_render(settings)
	if player_settings == null or player_settings.vehicle_content_id.is_empty():
		return {"ok": true, "manifest": [], "blobs": []}
	var livery: CarLivery = null
	if player_settings.car_livery.is_empty():
		livery = CarLiveryStore.load_for_car(player_settings.vehicle_content_id)
	else:
		livery = CarLivery.new()
		livery.from_dict(player_settings.car_livery)
	if livery == null:
		return {"ok": true, "manifest": [], "blobs": []}
	livery.vehicle_content_id = player_settings.vehicle_content_id
	return CustomStampStore.build_livery_payload(livery)

func _custom_stamp_blob_for_hash(player_id: int, stamp_hash: String, local_payloads: Dictionary):
	if local_payloads.has(player_id):
		var payload: Dictionary = local_payloads[player_id]
		for blob in payload.get("blobs", []):
			if blob != null and blob.stamp_hash == stamp_hash:
				return blob
	return custom_stamp_network.get_custom_stamp_blob(stamp_hash)

func _apply_custom_stamp_manifest_to_settings(settings, manifest: Array, region_origin: Vector2i) -> void:
	var player_settings := settings as PlayerSettings
	if player_settings == null or player_settings.car_livery.is_empty():
		return
	var entry_by_hash := {}
	for raw_entry in manifest:
		if typeof(raw_entry) == TYPE_DICTIONARY:
			var entry: Dictionary = raw_entry
			entry_by_hash[str(entry.get("hash", ""))] = entry
	if entry_by_hash.is_empty():
		return
	var livery := CarLivery.new()
	livery.from_dict(player_settings.car_livery)
	var changed := false
	for stamp in livery.stamps:
		if stamp == null or !stamp.is_custom():
			continue
		var stamp_hash: String = stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		if !entry_by_hash.has(stamp_hash):
			continue
		var entry: Dictionary = entry_by_hash[stamp_hash]
		var rect := _custom_stamp_manifest_rect(entry)
		stamp.custom_rect = Rect2(
			float(region_origin.x + rect.position.x) / float(CustomStampAtlasBuilder.ATLAS_SIZE.x),
			float(region_origin.y + rect.position.y) / float(CustomStampAtlasBuilder.ATLAS_SIZE.y),
			float(rect.size.x) / float(CustomStampAtlasBuilder.ATLAS_SIZE.x),
			float(rect.size.y) / float(CustomStampAtlasBuilder.ATLAS_SIZE.y))
		stamp.custom_rect_rotated = bool(entry.get("rect_rotated", false))
		stamp.palette_id = int(entry.get("palette_id", stamp.palette_id))
		changed = true
	if changed:
		player_settings.set_car_livery(livery)

func _custom_stamp_manifest_rect(entry: Dictionary) -> Rect2i:
	if !entry.has("rect_pixels") or typeof(entry["rect_pixels"]) != TYPE_ARRAY:
		return Rect2i()
	var values: Array = entry["rect_pixels"]
	if values.size() < 4:
		return Rect2i()
	return Rect2i(int(values[0]), int(values[1]), int(values[2]), int(values[3]))

func _custom_stamp_manifest_region_size(entry: Dictionary) -> Vector2i:
	if !entry.has("region_size") or typeof(entry["region_size"]) != TYPE_ARRAY:
		return Vector2i.ZERO
	var values: Array = entry["region_size"]
	if values.size() < 2:
		return Vector2i.ZERO
	return Vector2i(int(values[0]), int(values[1]))

func get_evidence(vehicle_content_id: String) -> Dictionary:
	var record: MxtContentRecord = content_catalog.resolve_content(vehicle_content_id)
	return {
		"vehicle_gameplay_digest": record.gameplay_digest if record != null else "",
		"vehicle_package_digest": record.package_digest if record != null else "",
		"vehicle_workshop_id": str(record.published_file_id) if record != null and record.published_file_id > 0 else "",
	}

func apply_evidence(settings: PlayerSettings) -> bool:
	if settings == null:
		return false
	var evidence := get_evidence(settings.vehicle_content_id)
	settings.vehicle_gameplay_digest = String(evidence.get("vehicle_gameplay_digest", ""))
	settings.vehicle_package_digest = String(evidence.get("vehicle_package_digest", ""))
	settings.vehicle_workshop_id = String(evidence.get("vehicle_workshop_id", ""))
	return !settings.vehicle_gameplay_digest.is_empty()

func evidence_matches(settings: PlayerSettings) -> bool:
	if settings == null:
		return false
	var record: MxtContentRecord = content_catalog.resolve_content(settings.vehicle_content_id)
	return (
		record != null
		and record.gameplay_digest == settings.vehicle_gameplay_digest
		and record.package_digest == settings.vehicle_package_digest
		and (str(record.published_file_id) if record.published_file_id > 0 else "") == settings.vehicle_workshop_id)

func reload_definitions() -> void:
	definitions.clear()
	definitions_by_content_id.clear()
	var directory := DirAccess.open("res://vehicle/asset")
	if directory == null:
		return
	directory.list_dir_begin()
	var folder := directory.get_next()
	while folder != "":
		if directory.current_is_dir() and !folder.begins_with(".") and folder != "bumper":
			_register_official_definition("res://vehicle/asset/%s/definition.tres" % folder)
		folder = directory.get_next()
	directory.list_dir_end()
	_load_packaged_definitions()
	definitions.sort_custom(func(a: CarDefinition, b: CarDefinition): return a.content_id < b.content_id)

func _register_official_definition(definition_path: String) -> void:
	if !ResourceLoader.exists(definition_path):
		return
	var definition := load(definition_path) as CarDefinition
	if definition == null:
		push_error("Invalid vehicle definition resource: %s" % definition_path)
	elif definition.content_id == "":
		push_error("Vehicle definition has no content ID: %s" % definition_path)
	elif !definition.content_id.begins_with(OFFICIAL_VEHICLE_PREFIX):
		push_error("Selectable built-in vehicle has invalid official content ID: %s" % definition.content_id)
	elif definitions_by_content_id.has(definition.content_id):
		push_error("Duplicate vehicle content ID: %s" % definition.content_id)
	elif !FileAccess.file_exists(definition.properties_path):
		push_error("Vehicle properties file is missing: %s" % definition.properties_path)
	else:
		var catalog_result: MxtContentLoadResult = content_catalog.add_official_vehicle(
			definition.content_id.trim_prefix(OFFICIAL_VEHICLE_PREFIX),
			definition.name,
			definition.properties_path,
			definition.resource_path)
		if !catalog_result.is_valid():
			push_error("Official vehicle catalog registration failed for %s: %s" % [definition.content_id, str(catalog_result.errors)])
		else:
			definitions.append(definition)
			definitions_by_content_id[definition.content_id] = definition

func refresh_installed_content() -> void:
	_scan_local_content_library()
	reload_definitions()
	catalog_changed.emit()

func refresh_workshop_content() -> bool:
	return steam_service != null and steam_service.refresh_workshop_items()

func get_workshop_content_items() -> Array:
	return workshop_content_items.duplicate(true)

func _scan_local_content_library() -> void:
	var library_path := ProjectSettings.globalize_path(LOCAL_CONTENT_LIBRARY_PATH)
	var directory_error := DirAccess.make_dir_recursive_absolute(library_path)
	if directory_error != OK:
		push_error("Could not create the local content library: %s" % error_string(directory_error))
		return
	var result: MxtContentLoadResult = content_catalog.scan_local_library(library_path)
	for index in result.get_diagnostic_count():
		push_warning("Skipped local content package %s: %s" % [result.get_diagnostic_path(index), str(result.get_diagnostic_errors(index))])
func create_test_drive_snapshot(package_root: String) -> MxtContentLoadResult:
	return content_catalog.snapshot_draft_package(
		package_root,
		ProjectSettings.globalize_path(TEST_DRIVE_SNAPSHOT_LIBRARY_PATH))

func _on_workshop_items_changed(items: Array) -> void:
	var next_subscribed_workshop_item_ids := {}
	for value in items:
		var item: Dictionary = value
		var published_file_id := int(item.get("published_file_id", 0))
		if published_file_id > 0 and bool(item.get("subscribed", false)):
			next_subscribed_workshop_item_ids[published_file_id] = true
	var garage_visibility_changed := next_subscribed_workshop_item_ids != subscribed_workshop_item_ids
	subscribed_workshop_item_ids = next_subscribed_workshop_item_ids
	var processed_items := []
	for value in items:
		var item: Dictionary = value.duplicate(true)
		var published_file_id := int(item.get("published_file_id", 0))
	var synced: MxtWorkshopSyncResult = content_catalog.sync_workshop_packages(items)
	var native_results_by_id := {}
	for index in synced.get_item_count():
		var result := synced.get_item(index)
		native_results_by_id[result.published_file_id] = result
	for value in items:
		var item: Dictionary = value.duplicate(true)
		var published_file_id := int(item.get("published_file_id", 0))
		var validation := native_results_by_id.get(published_file_id) as MxtWorkshopSyncItem
		if validation != null and validation.is_valid():
			item["status"] = "ready_update_pending" if (
				bool(item.get("needs_update", false))
				or bool(item.get("downloading", false))
				or bool(item.get("download_pending", false))) else "ready"
			item["record"] = validation.record
		else:
			var errors := validation.errors if validation != null else PackedStringArray()
			if bool(item.get("installed", false)) and !errors.is_empty():
				item["status"] = "outdated_format" if str(errors).contains("format revision") else "invalid"
				item["errors"] = errors
		processed_items.append(item)
	var catalog_materially_changed := synced.catalog_changed
	var delta := synced.delta
	workshop_content_items = processed_items
	if runtime_content_loaded and catalog_materially_changed:
		_apply_workshop_content_delta(delta)
		catalog_delta.emit(delta)
	workshop_content_changed.emit(get_workshop_content_items())
	if garage_visibility_changed and !catalog_materially_changed:
		garage_catalog_changed.emit()


func _load_packaged_definitions() -> void:
	for record_value in content_catalog.get_records("vehicle"):
		var record := record_value as MxtContentRecord
		if record == null:
			continue
		var source := record.source_name
		if source == "official" or source == "local_draft":
			continue
		var definition := VehicleRuntimeAssetFactory.create_from_package_record(record)
		if definition == null:
			continue
		if definitions_by_content_id.has(definition.content_id):
			push_error("Duplicate packaged vehicle content ID: %s" % definition.content_id)
			continue
		definitions.append(definition)
		definitions_by_content_id[definition.content_id] = definition
func _apply_workshop_content_delta(delta: MxtContentCatalogDelta) -> void:
	var remove_ids: Array = []
	remove_ids.append_array(Array(delta.removed_content_ids))
	remove_ids.append_array(Array(delta.changed_content_ids))
	for content_id_value in remove_ids:
		var content_id := String(content_id_value)
		definitions_by_content_id.erase(content_id)
	definitions = definitions.filter(
		func(definition): return definition is CarDefinition and !remove_ids.has(definition.content_id))
	var load_ids: Array = []
	load_ids.append_array(Array(delta.added_content_ids))
	load_ids.append_array(Array(delta.changed_content_ids))
	for content_id_value in load_ids:
		var content_id := String(content_id_value)
		var record: MxtContentRecord = content_catalog.resolve_content(content_id)
		if record == null or record.content_type != MxtContentRecord.CONTENT_VEHICLE:
			continue
		var definition := VehicleRuntimeAssetFactory.create_from_package_record(record)
		if definition != null:
			definitions.append(definition)
			definitions_by_content_id[content_id] = definition
	definitions.sort_custom(func(a: CarDefinition, b: CarDefinition): return a.content_id < b.content_id)
