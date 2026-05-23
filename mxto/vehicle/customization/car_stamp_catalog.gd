class_name CarStampCatalog
extends Resource

const CarStampEntry = preload("res://vehicle/customization/car_stamp_entry.gd")

@export var atlas_texture: Texture2D
@export var atlas_grid_size: Vector2i = Vector2i.ONE
@export var entries: Array[CarStampEntry] = []

var _entry_by_id: Dictionary = {}

func get_entry(stamp_id: String) -> CarStampEntry:
	_ensure_index()
	return _entry_by_id.get(stamp_id, null)

func get_preview_texture(stamp_id: String) -> Texture2D:
	var entry := get_entry(stamp_id)
	if entry == null:
		return null
	return entry.preview_texture

func get_entry_atlas_rect(entry: CarStampEntry) -> Rect2:
	if entry == null:
		return Rect2()
	return entry.get_atlas_rect(atlas_grid_size)

func get_atlas_rect(stamp_id: String) -> Rect2:
	return get_entry_atlas_rect(get_entry(stamp_id))

func create_stamp_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_texture = atlas_texture
	material.vertex_color_use_as_albedo = true
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	return material

func has_stamp(stamp_id: String) -> bool:
	return get_entry(stamp_id) != null

func rebuild_index() -> void:
	_entry_by_id.clear()
	for entry in entries:
		if entry == null or !entry.is_valid_entry():
			continue
		if !_entry_fits_grid(entry):
			push_warning("Car stamp entry is outside the atlas grid: %s" % entry.stamp_id)
			continue
		if _entry_by_id.has(entry.stamp_id):
			push_warning("Duplicate car stamp id: %s" % entry.stamp_id)
			continue
		_entry_by_id[entry.stamp_id] = entry

func _ensure_index() -> void:
	if _entry_by_id.size() == entries.size():
		return
	rebuild_index()

func _entry_fits_grid(entry: CarStampEntry) -> bool:
	if atlas_grid_size.x <= 0 or atlas_grid_size.y <= 0:
		return false
	if entry.atlas_tile_position.x < 0 or entry.atlas_tile_position.y < 0:
		return false
	return entry.atlas_tile_position.x + entry.atlas_tile_size.x <= atlas_grid_size.x and entry.atlas_tile_position.y + entry.atlas_tile_size.y <= atlas_grid_size.y
