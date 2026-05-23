class_name CarStampEntry
extends Resource

@export var stamp_id: String = ""
@export var display_name: String = ""
@export var preview_texture: Texture2D
@export var atlas_rect: Rect2 = Rect2(0.0, 0.0, 1.0, 1.0)

func is_valid_entry() -> bool:
	return stamp_id != "" and atlas_rect.size.x > 0.0 and atlas_rect.size.y > 0.0
