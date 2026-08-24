extends SceneTree

const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")

func _fail(message: String) -> void:
	push_error("MXT_VEHICLE_CONTENT_CONTROLLER_SMOKE_FAIL " + message)
	quit(1)

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	await process_frame
	var controller := game_manager.get_node("VehicleContentController") as VehicleContentControllerClass
	if controller == null or controller.definitions.is_empty():
		_fail("vehicle content owner did not load definitions")
		return
	for old_method in [
		"_load_car_definitions",
		"_scan_local_content_library",
		"_scan_test_drive_snapshot_library",
		"_car_definition_from_package_record",
		"_prepare_custom_stamp_render_payload",
	]:
		if game_manager.has_method(old_method):
			_fail("GameManager still owns %s" % old_method)
			return
	var content_ids := controller.get_vehicle_content_ids()
	if content_ids.size() != controller.definitions.size():
		_fail("definition index and selectable ID list disagree")
		return
	for definition_value in controller.definitions:
		var definition := definition_value as CarDefinition
		if definition == null or definition.content_id.is_empty() or !definition.has_visual():
			_fail("definition is missing identity or render content")
			return
		if controller.get_definition(definition.content_id) != definition:
			_fail("definition index did not return its registered resource")
			return
		var evidence := controller.get_evidence(definition.content_id)
		if !String(evidence.get("vehicle_gameplay_digest", "")).begins_with("sha256:"):
			_fail("definition has no catalog gameplay evidence")
			return
	var source := {
		"username": "Vehicle Content Smoke",
		"vehicle_content_id": String(content_ids[0]),
		"accel_setting": 0.75,
	}
	var stamp_render := controller.prepare_custom_stamp_render_payload([], [source], "smoke")
	var render_settings: Array = stamp_render.get("settings", [])
	if stamp_render.get("texture", null) != null or render_settings.size() != 1:
		_fail("empty stamp manifest render payload was not normalized")
		return
	var normalized := render_settings[0] as PlayerSettings
	if normalized == null or normalized.username != source["username"] or normalized.vehicle_content_id != source["vehicle_content_id"]:
		_fail("render settings normalization changed vehicle identity")
		return
	print("MXT_VEHICLE_CONTENT_CONTROLLER_SMOKE_OK definitions=", controller.definitions.size())
	game_manager.queue_free()
	await process_frame
	quit(0)
