class_name CarLivery
extends Resource

const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")

const VERSION := 1
const MAX_STAMPS := 16

@export var vehicle_content_id: String = ""
@export var primary_colour: Color = Color(0.1, 0.35, 1.0, 1.0)
@export var secondary_colour: Color = Color(1.0, 1.0, 1.0, 1.0)
@export var accent_colour: Color = Color(0.05, 0.05, 0.06, 1.0)
@export var stamps: Array[CarLiveryStamp] = []

func to_dict() -> Dictionary:
	var stamp_dicts: Array = []
	var sorted_stamps := get_sorted_stamps()
	for stamp in sorted_stamps:
		if stamp == null:
			continue
		stamp_dicts.append(stamp.to_dict())
	return {
		"version": VERSION,
		"vehicle_content_id": vehicle_content_id,
		"primary_colour": primary_colour.to_html(true),
		"secondary_colour": secondary_colour.to_html(true),
		"accent_colour": accent_colour.to_html(true),
		"stamps": stamp_dicts,
	}

func from_dict(data: Dictionary) -> void:
	if data.has("vehicle_content_id"):
		vehicle_content_id = str(data["vehicle_content_id"])
	if data.has("primary_colour"):
		primary_colour = Color.html(str(data["primary_colour"]))
	if data.has("secondary_colour"):
		secondary_colour = Color.html(str(data["secondary_colour"]))
	if data.has("accent_colour"):
		accent_colour = Color.html(str(data["accent_colour"]))
	stamps.clear()
	if data.has("stamps") and typeof(data["stamps"]) == TYPE_ARRAY:
		var stamp_data: Array = data["stamps"]
		var count := mini(stamp_data.size(), MAX_STAMPS)
		for i in range(count):
			if typeof(stamp_data[i]) != TYPE_DICTIONARY:
				continue
			var stamp := CarLiveryStamp.new()
			stamp.from_dict(stamp_data[i])
			stamps.append(stamp)
	_sort_stamps_in_place()

func add_stamp(stamp: CarLiveryStamp) -> bool:
	if stamp == null or stamps.size() >= MAX_STAMPS:
		return false
	stamps.append(stamp)
	_sort_stamps_in_place()
	return true

func remove_stamp(index: int) -> void:
	if index < 0 or index >= stamps.size():
		return
	stamps.remove_at(index)

func get_sorted_stamps() -> Array[CarLiveryStamp]:
	var out: Array[CarLiveryStamp] = []
	for stamp in stamps:
		if stamp != null:
			out.append(stamp)
	out.sort_custom(_compare_stamp_layer)
	return out

func get_livery_hash() -> String:
	var json := JSON.stringify(to_dict())
	return str(json.hash()).replace("-", "n")

func get_livery_key() -> String:
	return "%s:%s" % [vehicle_content_id, get_livery_hash()]

func get_custom_stamp_manifest() -> Array:
	var manifest: Array = []
	for stamp in get_sorted_stamps():
		if stamp == null or !stamp.is_custom():
			continue
		manifest.append({
			"source": CarLiveryStamp.SOURCE_CUSTOM,
			"hash": stamp.custom_hash,
			"id": stamp.stamp_id,
			"palette_id": stamp.palette_id,
			"rect": [stamp.custom_rect.position.x, stamp.custom_rect.position.y, stamp.custom_rect.size.x, stamp.custom_rect.size.y],
			"rect_rotated": stamp.custom_rect_rotated,
			"layer": stamp.layer,
		})
	return manifest

func save_to_path(path: String) -> Error:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(JSON.stringify(to_dict(), "\t"))
	file.close()
	return OK

static func load_from_path(path: String):
	var livery = (load("res://vehicle/customization/car_livery.gd") as Script).new()
	if !FileAccess.file_exists(path):
		return livery
	var data = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(data) == TYPE_DICTIONARY:
		livery.from_dict(data)
	return livery

func _sort_stamps_in_place() -> void:
	stamps = get_sorted_stamps()

static func _compare_stamp_layer(a: CarLiveryStamp, b: CarLiveryStamp) -> bool:
	if a.layer == b.layer:
		return a.stamp_key() < b.stamp_key()
	return a.layer < b.layer
