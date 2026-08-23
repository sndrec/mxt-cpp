class_name VehicleContentController
extends Node

signal workshop_content_changed(items: Array)
signal catalog_changed
signal catalog_delta(delta: Dictionary)

const OFFICIAL_VEHICLE_PREFIX := "mxt:vehicle:official:"
const WORKSHOP_VEHICLE_PREFIX := "mxt:vehicle:workshop:"
const LOCAL_CONTENT_LIBRARY_PATH := "user://content/packages"
const TEST_DRIVE_SNAPSHOT_LIBRARY_PATH := "user://content/test_drive_snapshots"
const LOBBY_WORKSHOP_DOWNLOAD_RETRY_BASE_MSEC := 3000
const LOBBY_WORKSHOP_DOWNLOAD_RETRY_MAX_MSEC := 30000
const WORKSHOP_DIAGNOSTIC_LOG_DIRECTORY := "user://logs"
const COMMUNITY_VEHICLE_SHADER: Shader = preload("res://vehicle/base_vehicle_shader.gdshader")
const COMMUNITY_VEHICLE_CROSS_HATCH: Texture2D = preload("res://asset/tex/crosshatch/1.png")
const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CustomStampAtlasBuilder = preload("res://vehicle/customization/custom_stamp_atlas_builder.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")
const LoadTransitionProfilerClass = preload("res://core/load_transition_profiler.gd")

var content_catalog: MxtContentCatalog = MxtContentCatalog.new()
var definitions: Array = []
var definitions_by_content_id: Dictionary = {}
var workshop_content_items: Array = []
var steam_service: MxtSteamService
var custom_stamp_network: CustomStampNetworkController
var runtime_content_loaded := false
var lobby_workshop_download_requests := {}
var lobby_workshop_download_failures := {}
var lobby_known_workshop_items := {}
var lobby_ready_workshop_packages := {}
var lobby_workshop_first_mismatch_msec := {}
var lobby_workshop_mismatch_signatures := {}
var workshop_diagnostic_log: FileAccess
var workshop_diagnostic_path := ""
var workshop_refresh_sequence := 0
var workshop_catalog_signature := ""

func initialize(in_steam_service: MxtSteamService, in_custom_stamp_network: CustomStampNetworkController) -> void:
	var load_profile := LoadTransitionProfilerClass.begin_transition("content", "vehicle_content_initialize")
	steam_service = in_steam_service
	custom_stamp_network = in_custom_stamp_network
	_open_workshop_diagnostic_log()
	steam_service.workshop_items_changed.connect(_on_workshop_items_changed)
	steam_service.workshop_request_completed.connect(_on_workshop_request_completed_diagnostic)
	steam_service.workshop_diagnostic_event.connect(_on_native_workshop_diagnostic)
	record_workshop_diagnostic_event("session_start", {
		"steam_initialized": steam_service.is_initialized(),
		"steam_app_id": steam_service.get_app_id(),
		"initial_workshop_items": steam_service.get_workshop_items(),
		"command_line": OS.get_cmdline_args(),
		"os": OS.get_name(),
		"os_version": OS.get_version(),
	})
	_scan_local_content_library()
	LoadTransitionProfilerClass.checkpoint(load_profile, "local_packages_scanned")
	var initial_workshop_items := steam_service.get_workshop_items()
	if !initial_workshop_items.is_empty():
		_on_workshop_items_changed(initial_workshop_items)
	LoadTransitionProfilerClass.checkpoint(load_profile, "initial_workshop_catalog", {
		"workshop_item_count": initial_workshop_items.size(),
	})
	_scan_trusted_verifier_workshop_packages()
	reload_definitions()
	runtime_content_loaded = true
	LoadTransitionProfilerClass.end_transition(load_profile, {
		"definition_count": definitions.size(),
		"content_record_count": content_catalog.get_records("vehicle").size(),
	})

func get_vehicle_content_ids() -> Array:
	var content_ids: Array = []
	for definition in definitions:
		if definition is CarDefinition and definition.content_id != "":
			content_ids.append(definition.content_id)
	return content_ids

func get_multiplayer_vehicle_content_ids(allow_workshop: bool) -> Array:
	var content_ids: Array = []
	for definition in definitions:
		if definition is CarDefinition and is_multiplayer_vehicle_content(definition.content_id, allow_workshop):
			content_ids.append(definition.content_id)
	return content_ids

func is_multiplayer_vehicle_content(content_id: String, allow_workshop: bool) -> bool:
	var record: Dictionary = content_catalog.resolve_content(content_id)
	var source := String(record.get("source", ""))
	if source == "official":
		return true
	var workshop_id_text := String(record.get("published_file_id", ""))
	return allow_workshop and source == "workshop" and workshop_id_text.is_valid_int() and workshop_id_text.to_int() > 0

func get_definition(vehicle_content_id: String) -> CarDefinition:
	var definition := definitions_by_content_id.get(vehicle_content_id) as CarDefinition
	if definition != null:
		return definition
	var record: Dictionary = content_catalog.resolve_content(vehicle_content_id)
	if String(record.get("source", "")) != "local_draft" or String(record.get("content_type", "")) != "vehicle":
		return null
	definition = _definition_from_package_record(record)
	if definition != null:
		definitions_by_content_id[vehicle_content_id] = definition
	return definition

func create_runtime_definition(record: Dictionary) -> CarDefinition:
	return _definition_from_package_record(record)

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
			var payload := _build_local_custom_stamp_payload(render_settings[i])
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

func _build_local_custom_stamp_payload(settings) -> Dictionary:
	var player_settings := player_settings_for_stamp_render(settings)
	if player_settings == null or player_settings.car_livery.is_empty():
		return {"ok": true, "manifest": [], "blobs": []}
	var livery := CarLivery.new()
	livery.from_dict(player_settings.car_livery)
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
	var record: Dictionary = content_catalog.resolve_content(vehicle_content_id)
	return {
		"vehicle_gameplay_digest": String(record.get("gameplay_digest", "")),
		"vehicle_package_digest": String(record.get("package_digest", "")),
		"vehicle_workshop_id": String(record.get("published_file_id", "")),
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
	var record: Dictionary = content_catalog.resolve_content(settings.vehicle_content_id)
	return (
		!record.is_empty()
		and String(record.get("gameplay_digest", "")) == settings.vehicle_gameplay_digest
		and String(record.get("package_digest", "")) == settings.vehicle_package_digest
		and String(record.get("published_file_id", "")) == settings.vehicle_workshop_id)

func reload_definitions() -> void:
	var load_profile := LoadTransitionProfilerClass.begin_transition("content", "vehicle_definition_reload")
	definitions.clear()
	definitions_by_content_id.clear()
	var directory := DirAccess.open("res://vehicle/asset")
	if directory == null:
		LoadTransitionProfilerClass.end_transition(load_profile, {"error": "official_vehicle_directory_unavailable"})
		return
	var official_profiles: Array = []
	directory.list_dir_begin()
	var folder := directory.get_next()
	while folder != "":
		if directory.current_is_dir() and !folder.begins_with(".") and folder != "bumper":
			var definition_start_usec := Time.get_ticks_usec()
			_register_official_definition("res://vehicle/asset/%s/definition.tres" % folder)
			official_profiles.append({
				"folder": folder,
				"duration_usec": Time.get_ticks_usec() - definition_start_usec,
			})
		folder = directory.get_next()
	directory.list_dir_end()
	LoadTransitionProfilerClass.checkpoint(load_profile, "official_definitions", {
		"count": definitions.size(),
	})
	var packaged_profiles := _load_packaged_definitions()
	LoadTransitionProfilerClass.checkpoint(load_profile, "packaged_definitions", {
		"count": packaged_profiles.size(),
	})
	definitions.sort_custom(func(a: CarDefinition, b: CarDefinition): return a.content_id < b.content_id)
	LoadTransitionProfilerClass.end_transition(load_profile, {
		"definition_count": definitions.size(),
		"official_profiles": official_profiles,
		"packaged_profiles": packaged_profiles,
	})

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
		var catalog_result: Dictionary = content_catalog.add_official_vehicle(
			definition.content_id.trim_prefix(OFFICIAL_VEHICLE_PREFIX),
			definition.name,
			definition.properties_path,
			definition.resource_path)
		if !bool(catalog_result.get("valid", false)):
			push_error("Official vehicle catalog registration failed for %s: %s" % [definition.content_id, str(catalog_result.get("errors", []))])
		else:
			definitions.append(definition)
			definitions_by_content_id[definition.content_id] = definition

func refresh_installed_content() -> void:
	var load_profile := LoadTransitionProfilerClass.begin_transition("content", "installed_vehicle_refresh")
	_scan_local_content_library()
	LoadTransitionProfilerClass.checkpoint(load_profile, "local_packages_scanned")
	reload_definitions()
	LoadTransitionProfilerClass.checkpoint(load_profile, "definitions_reloaded", {
		"definition_count": definitions.size(),
	})
	catalog_changed.emit()
	LoadTransitionProfilerClass.end_transition(load_profile)

func refresh_workshop_content() -> bool:
	return steam_service != null and steam_service.refresh_workshop_items()

func get_workshop_content_items() -> Array:
	return workshop_content_items.duplicate(true)

func get_workshop_diagnostic_path() -> String:
	return workshop_diagnostic_path

func record_workshop_diagnostic_event(event: String, fields := {}) -> void:
	var payload: Dictionary = fields.duplicate(true) if fields is Dictionary else {"value": fields}
	payload["event"] = event
	payload["ticks_msec"] = Time.get_ticks_msec()
	payload["unix_time"] = Time.get_unix_time_from_system()
	payload["utc"] = Time.get_datetime_string_from_system(true, true)
	var line := JSON.stringify(payload)
	print("MXT_WORKSHOP %s" % line)
	if workshop_diagnostic_log != null:
		workshop_diagnostic_log.store_line(line)
		workshop_diagnostic_log.flush()

func request_lobby_vehicle_content(settings: Dictionary) -> bool:
	var workshop_id_text := String(settings.get("vehicle_workshop_id", ""))
	if !workshop_id_text.is_valid_int():
		return false
	var workshop_id := workshop_id_text.to_int()
	var content_id := String(settings.get("vehicle_content_id", ""))
	var package_digest := String(settings.get("vehicle_package_digest", ""))
	if workshop_id <= 0 or content_id != WORKSHOP_VEHICLE_PREFIX + str(workshop_id):
		return false
	if String(lobby_ready_workshop_packages.get(workshop_id, "")) == package_digest:
		return true
	var record: Dictionary = content_catalog.resolve_content(content_id)
	var expected_gameplay_digest := String(settings.get("vehicle_gameplay_digest", ""))
	var local_published_file_id := String(record.get("published_file_id", ""))
	var local_gameplay_digest := String(record.get("gameplay_digest", ""))
	var local_package_digest := String(record.get("package_digest", ""))
	if (
		local_published_file_id == workshop_id_text
		and local_gameplay_digest == expected_gameplay_digest
		and local_package_digest == package_digest):
		var first_mismatch_msec := int(lobby_workshop_first_mismatch_msec.get(workshop_id, 0))
		if first_mismatch_msec > 0:
			record_workshop_diagnostic_event("lobby_content_ready", {
				"published_file_id": workshop_id,
				"content_id": content_id,
				"wait_duration_msec": Time.get_ticks_msec() - first_mismatch_msec,
				"gameplay_digest": local_gameplay_digest,
				"package_digest": local_package_digest,
				"package_path": String(record.get("root_path", "")),
			})
		lobby_workshop_download_requests.erase(workshop_id)
		lobby_workshop_download_failures.erase(workshop_id)
		lobby_workshop_first_mismatch_msec.erase(workshop_id)
		lobby_workshop_mismatch_signatures.erase(workshop_id)
		lobby_ready_workshop_packages[workshop_id] = package_digest
		return true
	var now_msec := Time.get_ticks_msec()
	if !lobby_workshop_first_mismatch_msec.has(workshop_id):
		lobby_workshop_first_mismatch_msec[workshop_id] = now_msec
	var mismatch_signature := "%s|%s|%s|%s|%s|%s" % [
		expected_gameplay_digest,
		package_digest,
		local_published_file_id,
		local_gameplay_digest,
		local_package_digest,
		String(record.get("root_path", "")),
	]
	if String(lobby_workshop_mismatch_signatures.get(workshop_id, "")) != mismatch_signature:
		lobby_workshop_mismatch_signatures[workshop_id] = mismatch_signature
		record_workshop_diagnostic_event("lobby_content_mismatch", {
			"published_file_id": workshop_id,
			"content_id": content_id,
			"expected_gameplay_digest": expected_gameplay_digest,
			"expected_package_digest": package_digest,
			"record_present": !record.is_empty(),
			"local_published_file_id": local_published_file_id,
			"local_gameplay_digest": local_gameplay_digest,
			"local_package_digest": local_package_digest,
			"local_source": String(record.get("source", "")),
			"local_package_path": String(record.get("root_path", "")),
			"steam_initialized": steam_service != null and steam_service.is_initialized(),
		})
	if steam_service == null or !steam_service.is_initialized():
		return false
	if !lobby_known_workshop_items.has(workshop_id):
		var tracked := steam_service.track_workshop_item(workshop_id)
		if tracked:
			steam_service.refresh_workshop_items()
		record_workshop_diagnostic_event("lobby_workshop_item_tracked", {
			"published_file_id": workshop_id,
			"content_id": content_id,
			"tracked": tracked,
		})
		return false
	var failure_count := int(lobby_workshop_download_failures.get(workshop_id, 0))
	var retry_delay_msec := mini(
		LOBBY_WORKSHOP_DOWNLOAD_RETRY_BASE_MSEC * (1 << mini(failure_count, 4)),
		LOBBY_WORKSHOP_DOWNLOAD_RETRY_MAX_MSEC)
	var last_request_msec := int(lobby_workshop_download_requests.get(workshop_id, -retry_delay_msec))
	if now_msec < last_request_msec + retry_delay_msec:
		return false
	lobby_workshop_download_requests[workshop_id] = now_msec
	# Steam's download-complete and item-installed callbacks refresh the catalog.
	# Refreshing synchronously here would rebuild every installed Workshop vehicle
	# once per missing lobby item, causing a severe frame stall for larger rosters.
	var accepted := steam_service.download_workshop_item(workshop_id, true)
	if !accepted:
		lobby_workshop_download_failures[workshop_id] = failure_count + 1
	record_workshop_diagnostic_event("lobby_download_request", {
		"published_file_id": workshop_id,
		"content_id": content_id,
		"accepted": accepted,
		"previous_failures": failure_count,
		"retry_delay_msec": retry_delay_msec,
		"attempt_age_msec": now_msec - int(lobby_workshop_first_mismatch_msec.get(workshop_id, now_msec)),
		"expected_gameplay_digest": expected_gameplay_digest,
		"expected_package_digest": package_digest,
		"local_gameplay_digest": local_gameplay_digest,
		"local_package_digest": local_package_digest,
	})
	return false

func _scan_local_content_library() -> void:
	var load_profile := LoadTransitionProfilerClass.begin_transition("content", "local_package_scan")
	var library_path := ProjectSettings.globalize_path(LOCAL_CONTENT_LIBRARY_PATH)
	var directory_error := DirAccess.make_dir_recursive_absolute(library_path)
	if directory_error != OK:
		push_error("Could not create the local content library: %s" % error_string(directory_error))
		LoadTransitionProfilerClass.end_transition(load_profile, {"error": error_string(directory_error)})
		return
	var result: Dictionary = content_catalog.scan_local_library(library_path)
	for diagnostic_value in result.get("diagnostics", []):
		var diagnostic: Dictionary = diagnostic_value
		push_warning("Skipped local content package %s: %s" % [String(diagnostic.get("path", "")), str(diagnostic.get("errors", []))])
	LoadTransitionProfilerClass.end_transition(load_profile, {
		"library_path": library_path,
		"diagnostic_count": (result.get("diagnostics", []) as Array).size(),
		"vehicle_record_count": content_catalog.get_records("vehicle").size(),
	})
func create_test_drive_snapshot(package_root: String) -> Dictionary:
	return content_catalog.snapshot_draft_package(
		package_root,
		ProjectSettings.globalize_path(TEST_DRIVE_SNAPSHOT_LIBRARY_PATH))

func _scan_trusted_verifier_workshop_packages() -> void:
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	if !(args.has("--leaderboard-replay-verify") or user_args.has("--leaderboard-replay-verify")):
		return
	for source_args in [args, user_args]:
		var index := 0
		while index < source_args.size():
			if String(source_args[index]) != "--trusted-workshop-package":
				index += 1
				continue
			if index + 2 >= source_args.size():
				push_error("--trusted-workshop-package requires a Workshop ID and package path")
				return
			var published_file_id := int(source_args[index + 1])
			var package_path := String(source_args[index + 2])
			var result: Dictionary = content_catalog.add_workshop_package(package_path, published_file_id)
			if !bool(result.get("valid", false)):
				push_error("Trusted verifier package %d failed validation: %s" % [published_file_id, str(result.get("errors", []))])
			index += 3

func _on_workshop_items_changed(items: Array) -> void:
	workshop_refresh_sequence += 1
	var sequence := workshop_refresh_sequence
	var refresh_start_usec := Time.get_ticks_usec()
	var load_profile := LoadTransitionProfilerClass.begin_transition("content", "workshop_catalog_refresh", {
		"sequence": sequence,
		"item_count": items.size(),
	})
	record_workshop_diagnostic_event("catalog_refresh_begin", {
		"sequence": sequence,
		"item_count": items.size(),
	})
	var processed_items := []
	for value in items:
		var item: Dictionary = value.duplicate(true)
		var published_file_id := int(item.get("published_file_id", 0))
		if published_file_id > 0:
			lobby_known_workshop_items[published_file_id] = true
		record_workshop_diagnostic_event("catalog_item_state", {
			"sequence": sequence,
			"item": item,
		})
	var synced: Dictionary = content_catalog.sync_workshop_packages(items)
	var native_results_by_id := {}
	var candidate_count := 0
	for result_value in synced.get("items", []):
		var result: Dictionary = result_value
		var published_file_id := int(result.get("published_file_id", 0))
		native_results_by_id[published_file_id] = result
		if bool(result.get("valid", false)):
			candidate_count += 1
		if bool(result.get("eligible", false)) and !bool(result.get("cache_hit", false)):
			record_workshop_diagnostic_event("catalog_package_validation", {
				"published_file_id": published_file_id,
				"install_path": String(result.get("install_path", "")),
				"valid": bool(result.get("valid", false)),
				"package_digest": String(result.get("package_digest", "")),
				"gameplay_digest": String(result.get("gameplay_digest", "")),
				"errors": result.get("errors", []),
				"validation_profile": result.get("validation_profile", {}),
			})
			record_workshop_diagnostic_event("catalog_package_registration", {
				"published_file_id": published_file_id,
				"duration_usec": int(result.get("registration_usec", 0)),
				"valid": bool(result.get("valid", false)),
				"errors": result.get("errors", []),
				"record": result.get("record", {}),
			})
	for value in items:
		var item: Dictionary = value.duplicate(true)
		var published_file_id := int(item.get("published_file_id", 0))
		var validation: Dictionary = native_results_by_id.get(published_file_id, {})
		if bool(validation.get("valid", false)):
			item["status"] = "ready_update_pending" if (
				bool(item.get("needs_update", false))
				or bool(item.get("downloading", false))
				or bool(item.get("download_pending", false))) else "ready"
			item["record"] = validation.get("record", {})
		else:
			var errors = validation.get("errors", [])
			if bool(item.get("installed", false)) and !errors.is_empty():
				item["status"] = "outdated_format" if str(errors).contains("format revision") else "invalid"
				item["errors"] = errors
		processed_items.append(item)
	var validation_cache_hit_count := int(synced.get("validation_cache_hit_count", 0))
	var validation_cache_miss_count := int(synced.get("validation_cache_miss_count", 0))
	LoadTransitionProfilerClass.checkpoint(load_profile, "validate_items", {
		"candidate_count": candidate_count,
		"validation_cache_hit_count": validation_cache_hit_count,
		"validation_cache_miss_count": validation_cache_miss_count,
	})
	var catalog_materially_changed := bool(synced.get("catalog_changed", false))
	var delta: Dictionary = synced.get("delta", {})
	workshop_catalog_signature = String(synced.get("catalog_signature", ""))
	LoadTransitionProfilerClass.checkpoint(load_profile, "catalog_signature", {
		"materially_changed": catalog_materially_changed,
	})
	for published_file_id in delta.get("changed_item_ids", []):
		lobby_ready_workshop_packages.erase(int(published_file_id))
	for published_file_id in delta.get("removed_item_ids", []):
		lobby_ready_workshop_packages.erase(int(published_file_id))
	LoadTransitionProfilerClass.checkpoint(load_profile, "register_packages", {
		"registered_count": validation_cache_miss_count,
	})
	workshop_content_items = processed_items
	LoadTransitionProfilerClass.checkpoint(load_profile, "resolve_catalog_records")
	var definition_reload_duration_usec := 0
	if runtime_content_loaded and catalog_materially_changed:
		var definition_reload_start_usec := Time.get_ticks_usec()
		var definition_profiles := _apply_workshop_content_delta(delta)
		definition_reload_duration_usec = Time.get_ticks_usec() - definition_reload_start_usec
		delta["definition_profiles"] = definition_profiles
		catalog_delta.emit(delta)
	workshop_content_changed.emit(get_workshop_content_items())
	record_workshop_diagnostic_event("catalog_refresh_end", {
		"sequence": sequence,
		"duration_usec": Time.get_ticks_usec() - refresh_start_usec,
		"definition_reload_duration_usec": definition_reload_duration_usec,
		"definition_count": definitions.size(),
		"workshop_item_count": workshop_content_items.size(),
		"catalog_materially_changed": catalog_materially_changed,
		"catalog_signature": workshop_catalog_signature,
		"validation_cache_hit_count": validation_cache_hit_count,
		"validation_cache_miss_count": validation_cache_miss_count,
		"added_content_ids": delta.get("added_content_ids", []),
		"changed_content_ids": delta.get("changed_content_ids", []),
		"removed_content_ids": delta.get("removed_content_ids", []),
	})
	LoadTransitionProfilerClass.end_transition(load_profile, {
		"catalog_materially_changed": catalog_materially_changed,
		"definition_reload_duration_usec": definition_reload_duration_usec,
		"definition_count": definitions.size(),
	})


func _open_workshop_diagnostic_log() -> void:
	var absolute_directory := ProjectSettings.globalize_path(WORKSHOP_DIAGNOSTIC_LOG_DIRECTORY)
	var directory_error := DirAccess.make_dir_recursive_absolute(absolute_directory)
	if directory_error != OK:
		push_warning("Could not create Workshop diagnostic log directory: %s" % error_string(directory_error))
		return
	workshop_diagnostic_path = "%s/workshop-diagnostics-%d.jsonl" % [WORKSHOP_DIAGNOSTIC_LOG_DIRECTORY, int(Time.get_unix_time_from_system())]
	workshop_diagnostic_log = FileAccess.open(workshop_diagnostic_path, FileAccess.WRITE)
	if workshop_diagnostic_log == null:
		push_warning("Could not open Workshop diagnostic log: %s" % FileAccess.get_open_error())
		workshop_diagnostic_path = ""
		return
	print("MXT_WORKSHOP diagnostic log: %s" % ProjectSettings.globalize_path(workshop_diagnostic_path))

func _on_workshop_request_completed_diagnostic(request_id: int, operation: String, result: Dictionary) -> void:
	var published_file_id := int(result.get("published_file_id", 0))
	if published_file_id > 0 and operation == "download":
		if bool(result.get("success", false)):
			lobby_workshop_download_failures.erase(published_file_id)
		else:
			lobby_workshop_download_failures[published_file_id] = int(lobby_workshop_download_failures.get(published_file_id, 0)) + 1
	elif published_file_id > 0 and operation == "item_installed":
		lobby_workshop_download_failures.erase(published_file_id)
	record_workshop_diagnostic_event("steam_callback", {
		"request_id": request_id,
		"operation": operation,
		"result": result,
	})

func _on_native_workshop_diagnostic(event: String, fields: Dictionary) -> void:
	record_workshop_diagnostic_event("steam_native_%s" % event, fields)


func _load_packaged_definitions() -> Array:
	var profiles: Array = []
	for record_value in content_catalog.get_records("vehicle"):
		var record: Dictionary = record_value
		var source := String(record.get("source", ""))
		if source == "official" or source == "local_draft":
			continue
		var definition_start_usec := Time.get_ticks_usec()
		var definition_profile := {
			"content_id": String(record.get("content_id", "")),
			"source": source,
			"visual_path": String(record.get("visual_path", "")),
		}
		var definition := _definition_from_package_record(record, definition_profile)
		definition_profile["duration_usec"] = Time.get_ticks_usec() - definition_start_usec
		definition_profile["valid"] = definition != null
		profiles.append(definition_profile)
		if definition == null:
			continue
		if definitions_by_content_id.has(definition.content_id):
			push_error("Duplicate packaged vehicle content ID: %s" % definition.content_id)
			continue
		definitions.append(definition)
		definitions_by_content_id[definition.content_id] = definition
	profiles.sort_custom(func(a: Dictionary, b: Dictionary): return int(a.get("duration_usec", 0)) > int(b.get("duration_usec", 0)))
	return profiles


func _apply_workshop_content_delta(delta: Dictionary) -> Array:
	var remove_ids: Array = []
	remove_ids.append_array(delta.get("removed_content_ids", []))
	remove_ids.append_array(delta.get("changed_content_ids", []))
	for content_id_value in remove_ids:
		var content_id := String(content_id_value)
		definitions_by_content_id.erase(content_id)
	definitions = definitions.filter(
		func(definition): return definition is CarDefinition and !remove_ids.has(definition.content_id))
	var profiles: Array = []
	var load_ids: Array = []
	load_ids.append_array(delta.get("added_content_ids", []))
	load_ids.append_array(delta.get("changed_content_ids", []))
	for content_id_value in load_ids:
		var content_id := String(content_id_value)
		var record: Dictionary = content_catalog.resolve_content(content_id)
		if String(record.get("content_type", "")) != "vehicle":
			continue
		var profile := {
			"content_id": content_id,
			"source": String(record.get("source", "")),
			"visual_path": String(record.get("visual_path", "")),
		}
		var start_usec := Time.get_ticks_usec()
		var definition := _definition_from_package_record(record, profile)
		profile["duration_usec"] = Time.get_ticks_usec() - start_usec
		profile["valid"] = definition != null
		profiles.append(profile)
		if definition != null:
			definitions.append(definition)
			definitions_by_content_id[content_id] = definition
	definitions.sort_custom(func(a: CarDefinition, b: CarDefinition): return a.content_id < b.content_id)
	return profiles


func _definition_from_package_record(record: Dictionary, profile: Dictionary = {}) -> CarDefinition:
	var visual_path := String(record.get("visual_path", ""))
	var phase_start_usec := Time.get_ticks_usec()
	var gltf_document := GLTFDocument.new()
	var gltf_state := GLTFState.new()
	gltf_state.base_path = visual_path.get_base_dir()
	var error := gltf_document.append_from_file(visual_path, gltf_state)
	profile["gltf_parse_usec"] = Time.get_ticks_usec() - phase_start_usec
	if error != OK:
		push_error("Could not load packaged vehicle visual %s: %s" % [visual_path, error_string(error)])
		return null
	phase_start_usec = Time.get_ticks_usec()
	var instance := gltf_document.generate_scene(gltf_state) as Node3D
	profile["scene_generation_usec"] = Time.get_ticks_usec() - phase_start_usec
	if instance == null:
		push_error("Could not generate packaged vehicle visual: %s" % visual_path)
		return null
	phase_start_usec = Time.get_ticks_usec()
	var mesh_data := _find_mesh(instance, Transform3D.IDENTITY)
	profile["mesh_traversal_usec"] = Time.get_ticks_usec() - phase_start_usec
	if mesh_data.is_empty():
		push_error("Packaged vehicle visual has no runtime mesh: %s" % visual_path)
		instance.free()
		return null
	var mesh_instance: MeshInstance3D = mesh_data["instance"]
	profile["source_surface_count"] = mesh_instance.mesh.get_surface_count() if mesh_instance.mesh != null else 0
	var visual_metadata: Dictionary = record.get("visual_metadata", {})
	phase_start_usec = Time.get_ticks_usec()
	var runtime_mesh := _build_body_mesh(mesh_instance.mesh, visual_metadata.get("body_surfaces", []))
	profile["body_mesh_build_usec"] = Time.get_ticks_usec() - phase_start_usec
	if runtime_mesh == null:
		push_error("Packaged vehicle has no selected runtime body surfaces: %s" % visual_path)
		instance.free()
		return null
	var definition := CarDefinition.new()
	definition.name = String(record.get("title", "Vehicle"))
	definition.content_id = String(record.get("content_id", ""))
	definition.properties_path = String(record.get("authoritative_path", ""))
	definition.runtime_mesh = runtime_mesh
	phase_start_usec = Time.get_ticks_usec()
	definition.runtime_material = _build_material(mesh_instance.mesh, visual_metadata.get("material_inputs", {}), record)
	profile["material_texture_build_usec"] = Time.get_ticks_usec() - phase_start_usec
	definition.runtime_transform = _transform_from_metadata(visual_metadata.get("model_transform", {})) * mesh_data["transform"]
	phase_start_usec = Time.get_ticks_usec()
	definition.manual_boost_sfx = _load_packaged_boost_sfx(String(record.get("manual_boost_sfx_path", "")))
	profile["boost_audio_load_usec"] = Time.get_ticks_usec() - phase_start_usec
	definition.manual_boost_volume_db = clampf(
		float(visual_metadata.get("manual_boost_volume_db", 0.0)), -20.0, 20.0)
	for thruster_value in visual_metadata.get("thrusters", []):
		definition.runtime_thruster_transforms.append(_thruster_transform_from_metadata(thruster_value))
	profile["runtime_surface_count"] = runtime_mesh.get_surface_count()
	profile["runtime_vertex_count"] = _mesh_vertex_count(runtime_mesh)
	profile["runtime_index_count"] = _mesh_index_count(runtime_mesh)
	instance.free()
	return definition

func _mesh_vertex_count(mesh: Mesh) -> int:
	if mesh == null:
		return 0
	var total := 0
	for surface in range(mesh.get_surface_count()):
		total += mesh.surface_get_array_len(surface)
	return total

func _mesh_index_count(mesh: Mesh) -> int:
	if mesh == null:
		return 0
	var total := 0
	for surface in range(mesh.get_surface_count()):
		total += mesh.surface_get_array_index_len(surface)
	return total

func _load_packaged_boost_sfx(path: String) -> AudioStream:
	if path.is_empty() or !FileAccess.file_exists(path):
		return null
	if path.to_lower().ends_with(".wav"):
		return AudioStreamWAV.load_from_file(path)
	if path.to_lower().ends_with(".ogg"):
		return AudioStreamOggVorbis.load_from_file(path)
	return null

func _build_body_mesh(source: Mesh, selected_surfaces: Array) -> ArrayMesh:
	if source == null or selected_surfaces.is_empty():
		return null
	var body := ArrayMesh.new()
	for surface_value in selected_surfaces:
		var surface := int(surface_value)
		if surface < 0 or surface >= source.get_surface_count():
			return null
		body.add_surface_from_arrays(source.surface_get_primitive_type(surface), source.surface_get_arrays(surface))
	return body

func _build_material(source: Mesh, inputs: Dictionary, record: Dictionary) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = COMMUNITY_VEHICLE_SHADER
	material.set_shader_parameter("in_lightwarp", _community_lightwarp())
	material.set_shader_parameter("in_specwarp", _community_specwarp())
	material.set_shader_parameter("cross_hatch", COMMUNITY_VEHICLE_CROSS_HATCH)
	material.set_shader_parameter("in_overlay_colour", Color.BLACK)
	# Revision-1 Workshop packages predate standalone PNG payloads. Missing paths must
	# permanently fall back to their embedded GLB textures so published cars keep rendering.
	var albedo_override := _load_packaged_texture(String(record.get("albedo_texture_path", "")))
	var normal_override := _load_packaged_texture(String(record.get("normal_texture_path", "")))
	var paint_mask_override := _load_packaged_texture(String(record.get("paint_mask_texture_path", "")))
	material.set_shader_parameter("in_albedo", albedo_override if albedo_override != null else _texture(source, int(inputs.get("albedo_surface", -1)), "albedo_texture", Color.WHITE))
	material.set_shader_parameter("in_normal", normal_override if normal_override != null else _texture(source, int(inputs.get("normal_surface", -1)), "normal_texture", Color(0.5, 0.5, 1.0, 1.0)))
	material.set_shader_parameter("in_paint_mask", paint_mask_override if paint_mask_override != null else _texture(source, int(inputs.get("paint_mask_surface", -1)), "ao_texture", Color.BLACK))
	material.set_shader_parameter("use_mesh_normals", bool(inputs.get("use_mesh_normals", false)))
	material.set_shader_parameter("livery_colour_strength", 1.0)
	return material

func _load_packaged_texture(path: String) -> Texture2D:
	if path.is_empty() or !FileAccess.file_exists(path):
		return null
	var image := Image.load_from_file(path)
	if image == null or image.is_empty():
		return null
	return ImageTexture.create_from_image(image)

func _texture(source: Mesh, surface: int, property: StringName, fallback: Color) -> Texture2D:
	if source != null and surface >= 0 and surface < source.get_surface_count():
		var source_material := source.surface_get_material(surface)
		if source_material != null:
			var candidate = source_material.get(property)
			if candidate is Texture2D:
				return candidate
	var image := Image.create(1, 1, false, Image.FORMAT_RGBA8)
	image.fill(fallback)
	return ImageTexture.create_from_image(image)

func _community_lightwarp() -> GradientTexture1D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CONSTANT
	gradient.offsets = PackedFloat32Array([0.0, 0.318519, 0.788889, 0.979532])
	gradient.colors = PackedColorArray([Color(0.1, 0.1, 0.1), Color.BLACK, Color(0.521, 0.521, 0.521), Color.WHITE])
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	return texture

func _community_specwarp() -> GradientTexture1D:
	var gradient := Gradient.new()
	gradient.interpolation_mode = Gradient.GRADIENT_INTERPOLATE_CONSTANT
	gradient.offsets = PackedFloat32Array([0.151852, 0.925926])
	gradient.colors = PackedColorArray([Color.BLACK, Color.WHITE])
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	return texture

func _transform_from_metadata(value: Dictionary) -> Transform3D:
	var degrees: Vector3 = value.get("rotation_degrees", Vector3.ZERO)
	var rotation := Vector3(deg_to_rad(degrees.x), deg_to_rad(degrees.y), deg_to_rad(degrees.z))
	var scale_value: Vector3 = value.get("scale", Vector3.ONE)
	return Transform3D(Basis.from_euler(rotation).scaled(scale_value), value.get("translation", Vector3.ZERO))

func _thruster_transform_from_metadata(value: Dictionary) -> Transform3D:
	var degrees: Vector3 = value.get("rotation_degrees", Vector3.ZERO)
	var rotation := Vector3(deg_to_rad(degrees.x), deg_to_rad(degrees.y), deg_to_rad(degrees.z))
	return Transform3D(Basis.from_euler(rotation).scaled(Vector3.ONE * float(value.get("scale", 1.0))), value.get("position", Vector3.ZERO))

func _find_mesh(node: Node, parent_transform: Transform3D) -> Dictionary:
	var local_transform := parent_transform
	var node_3d := node as Node3D
	if node_3d != null:
		local_transform *= node_3d.transform
	var mesh_instance := node as MeshInstance3D
	if mesh_instance != null and mesh_instance.mesh != null:
		return {"instance": mesh_instance, "transform": local_transform}
	for child in node.get_children():
		var found := _find_mesh(child, local_transform)
		if !found.is_empty():
			return found
	return {}
