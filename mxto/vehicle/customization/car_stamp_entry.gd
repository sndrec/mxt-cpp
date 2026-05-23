class_name CarStampEntry
extends Resource

@export var stamp_id: String = ""
@export var display_name: String = ""
@export var atlas_tile_position: Vector2i = Vector2i.ZERO
@export var atlas_tile_size: Vector2i = Vector2i.ONE

func is_valid_entry() -> bool:
	return stamp_id != "" and atlas_tile_size.x > 0 and atlas_tile_size.y > 0

func get_atlas_rect(atlas_grid_size: Vector2i) -> Rect2:
	if atlas_grid_size.x <= 0 or atlas_grid_size.y <= 0 or !is_valid_entry():
		return Rect2()
	return Rect2(
		float(atlas_tile_position.x) / float(atlas_grid_size.x),
		float(atlas_tile_position.y) / float(atlas_grid_size.y),
		float(atlas_tile_size.x) / float(atlas_grid_size.x),
		float(atlas_tile_size.y) / float(atlas_grid_size.y)
	)
