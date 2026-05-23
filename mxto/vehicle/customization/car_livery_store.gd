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
