class_name CarLiveryStore
extends RefCounted

const CarLivery = preload("res://vehicle/customization/car_livery.gd")

const LIVERY_DIR := "user://garage_liveries"

static func load_for_car(car_definition_path: String):
	var livery: CarLivery = CarLivery.load_from_path(get_path_for_car(car_definition_path))
	if livery.car_definition_path == "":
		livery.car_definition_path = car_definition_path
	return livery

static func save_for_car(livery: CarLivery) -> Error:
	if livery == null:
		return ERR_INVALID_PARAMETER
	var dir_err := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(LIVERY_DIR))
	if dir_err != OK:
		return dir_err
	return livery.save_to_path(get_path_for_car(livery.car_definition_path))

static func get_path_for_car(car_definition_path: String) -> String:
	return "%s/%s.json" % [LIVERY_DIR, _safe_car_id(car_definition_path)]

static func has_for_car(car_definition_path: String) -> bool:
	return FileAccess.file_exists(get_path_for_car(car_definition_path))

static func remove_custom_stamp_references(stamp_hash: String) -> Error:
	if stamp_hash == "":
		return ERR_INVALID_PARAMETER
	var dir := DirAccess.open(LIVERY_DIR)
	if dir == null:
		return OK
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if !dir.current_is_dir() and file_name.get_extension().to_lower() == "json":
			var err := _remove_custom_stamp_references_from_file("%s/%s" % [LIVERY_DIR, file_name], stamp_hash)
			if err != OK:
				dir.list_dir_end()
				return err
		file_name = dir.get_next()
	dir.list_dir_end()
	return OK

static func _remove_custom_stamp_references_from_file(path: String, stamp_hash: String) -> Error:
	var livery: CarLivery = CarLivery.load_from_path(path)
	var changed := false
	for i in range(livery.stamps.size() - 1, -1, -1):
		var stamp := livery.stamps[i]
		if stamp == null or !stamp.is_custom():
			continue
		var key := stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		if key != stamp_hash:
			continue
		livery.stamps.remove_at(i)
		changed = true
	if !changed:
		return OK
	return livery.save_to_path(path)

static func _safe_car_id(car_definition_path: String) -> String:
	var safe := car_definition_path
	safe = safe.replace("res://", "")
	safe = safe.replace("user://", "")
	safe = safe.replace("\\", "/")
	safe = safe.replace("/", "_")
	safe = safe.replace(":", "_")
	safe = safe.replace(".", "_")
	safe = safe.replace(" ", "_")
	return "%s_%s" % [safe, str(car_definition_path.hash()).replace("-", "n")]
