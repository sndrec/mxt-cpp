class_name CarStampCatalog
extends Resource

const CarStampEntry = preload("res://vehicle/customization/car_stamp_entry.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const STAMP_MULTIPLY_SHADER = preload("res://vehicle/customization/car_stamp_multiply.gdshader")

@export var atlas_texture: Texture2D
@export var custom_atlas_texture: Texture2D
@export var atlas_grid_size: Vector2i = Vector2i.ONE
@export var entries: Array[CarStampEntry] = []

var _entry_by_id: Dictionary = {}
var _preview_texture_by_id: Dictionary = {}
var _white_visibility_mask: Texture2D = null
var _transparent_stamp_atlas: Texture2D = null

func get_entry(stamp_id: String) -> CarStampEntry:
	_ensure_index()
	return _entry_by_id.get(stamp_id, null)

func get_preview_texture(stamp_id: String) -> Texture2D:
	var entry := get_entry(stamp_id)
	if entry == null:
		return null
	if _preview_texture_by_id.has(stamp_id):
		return _preview_texture_by_id[stamp_id]
	if atlas_texture == null:
		return null
	var atlas_rect := get_entry_atlas_rect(entry)
	if atlas_rect.size.x <= 0.0 or atlas_rect.size.y <= 0.0:
		return null
	var atlas_size := Vector2(atlas_texture.get_width(), atlas_texture.get_height())
	var preview := AtlasTexture.new()
	preview.atlas = atlas_texture
	preview.region = Rect2(atlas_rect.position * atlas_size, atlas_rect.size * atlas_size)
	_preview_texture_by_id[stamp_id] = preview
	return preview

func get_entry_atlas_rect(entry: CarStampEntry) -> Rect2:
	if entry == null:
		return Rect2()
	return entry.get_atlas_rect(atlas_grid_size)

func get_atlas_rect(stamp_id: String) -> Rect2:
	return get_entry_atlas_rect(get_entry(stamp_id))

func get_stamp_atlas_rect(stamp: CarLiveryStamp) -> Rect2:
	if stamp == null:
		return Rect2()
	if stamp.is_custom():
		return stamp.custom_rect
	return get_atlas_rect(stamp.stamp_id)

func get_stamp_source_flag(stamp: CarLiveryStamp) -> float:
	if stamp != null and stamp.is_custom():
		return 1.0
	return 0.0

func create_stamp_material(base_material: Material = null, visibility_mask: Texture2D = null) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = STAMP_MULTIPLY_SHADER
	material.set_shader_parameter("base_stamp_atlas", atlas_texture if atlas_texture != null else _get_transparent_stamp_atlas())
	material.set_shader_parameter("custom_stamp_atlas", custom_atlas_texture if custom_atlas_texture != null else _get_transparent_stamp_atlas())
	material.set_shader_parameter("stamp_visibility_mask", visibility_mask if visibility_mask != null else _get_white_visibility_mask())
	var base_shader_material := base_material as ShaderMaterial
	if base_shader_material != null:
		for parameter in ["in_albedo", "in_normal", "in_lightwarp", "in_lightwarp_2", "in_lightwarp_2_fresnel", "in_specwarp", "in_specwarp_fresnel"]:
			var value = base_shader_material.get_shader_parameter(parameter)
			if value != null:
				material.set_shader_parameter(parameter, value)
	return material

func has_stamp(stamp_id: String) -> bool:
	return get_entry(stamp_id) != null

func rebuild_index() -> void:
	_entry_by_id.clear()
	_preview_texture_by_id.clear()
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

func _get_white_visibility_mask() -> Texture2D:
	if _white_visibility_mask != null:
		return _white_visibility_mask
	var image := Image.create(1, 1, false, Image.FORMAT_RGF)
	image.set_pixel(0, 0, Color(0.0, 0.0, 0.0, 1.0))
	_white_visibility_mask = ImageTexture.create_from_image(image)
	return _white_visibility_mask

func _get_transparent_stamp_atlas() -> Texture2D:
	if _transparent_stamp_atlas != null:
		return _transparent_stamp_atlas
	var image := Image.create(1, 1, false, Image.FORMAT_RGBA8)
	image.set_pixel(0, 0, Color(1.0, 1.0, 1.0, 0.0))
	_transparent_stamp_atlas = ImageTexture.create_from_image(image)
	return _transparent_stamp_atlas

func _entry_fits_grid(entry: CarStampEntry) -> bool:
	if atlas_grid_size.x <= 0 or atlas_grid_size.y <= 0:
		return false
	if entry.atlas_tile_position.x < 0 or entry.atlas_tile_position.y < 0:
		return false
	return entry.atlas_tile_position.x + entry.atlas_tile_size.x <= atlas_grid_size.x and entry.atlas_tile_position.y + entry.atlas_tile_size.y <= atlas_grid_size.y
