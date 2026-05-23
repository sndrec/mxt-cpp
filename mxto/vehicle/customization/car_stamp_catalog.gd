class_name CarStampCatalog
extends Resource

const CarStampEntry = preload("res://vehicle/customization/car_stamp_entry.gd")

@export var atlas_texture: Texture2D
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
		if _entry_by_id.has(entry.stamp_id):
			push_warning("Duplicate car stamp id: %s" % entry.stamp_id)
			continue
		_entry_by_id[entry.stamp_id] = entry

func _ensure_index() -> void:
	if _entry_by_id.size() == entries.size():
		return
	rebuild_index()
