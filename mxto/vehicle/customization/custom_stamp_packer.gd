class_name CustomStampPacker
extends RefCounted

const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")

const REGION_WIDE := Vector2i(256, 128)
const REGION_TALL := Vector2i(128, 256)

static func pack_for_player_region(stamps: Array) -> Dictionary:
	var unique := _unique_stamp_rects(stamps)
	if unique.is_empty():
		return {"ok": true, "region_size": REGION_WIDE, "placements": {}, "pixel_count": 0}
	var pixel_count := 0
	for item in unique:
		pixel_count += int(item["width"]) * int(item["height"])
	if pixel_count > CustomStampBlob.PLAYER_INDEXED_PIXEL_BUDGET:
		return {"ok": false, "error": "custom stamps exceed the per-player pixel budget", "pixel_count": pixel_count}
	for region_size in [REGION_WIDE, REGION_TALL]:
		var packed := _try_pack_region(unique, region_size, pixel_count)
		if bool(packed.get("ok", false)):
			return packed
	return {"ok": false, "error": "custom stamps do not fit in the player atlas region", "pixel_count": pixel_count}

static func normalized_rect(pixel_rect: Rect2i, region_size: Vector2i, region_origin: Vector2i = Vector2i.ZERO, atlas_size: Vector2i = Vector2i(2048, 2048)) -> Rect2:
	var atlas := Vector2(maxi(1, atlas_size.x), maxi(1, atlas_size.y))
	var pos_i := region_origin + pixel_rect.position
	var pos := Vector2(float(pos_i.x), float(pos_i.y)) / atlas
	var size := Vector2(float(pixel_rect.size.x), float(pixel_rect.size.y)) / atlas
	return Rect2(pos, size)

static func apply_placements_to_livery(livery, placements: Dictionary, region_origin: Vector2i = Vector2i.ZERO, atlas_size: Vector2i = Vector2i(2048, 2048)) -> bool:
	if livery == null:
		return false
	var changed := false
	for stamp in livery.stamps:
		if stamp == null or !stamp.is_custom():
			continue
		var key: String = stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		if !placements.has(key):
			return false
		var placement: Dictionary = placements[key]
		var rect: Rect2i = placement["rect"]
		var region_size: Vector2i = placement["region_size"]
		stamp.custom_rect = normalized_rect(rect, region_size, region_origin, atlas_size)
		stamp.custom_rect_rotated = bool(placement.get("rotated", false))
		changed = true
	return changed

static func _unique_stamp_rects(stamps: Array) -> Array:
	var by_hash := {}
	for raw in stamps:
		var item := _stamp_rect_item(raw)
		if item.is_empty():
			continue
		var key := str(item["hash"])
		if by_hash.has(key):
			continue
		by_hash[key] = item
	var out := by_hash.values()
	out.sort_custom(_compare_pack_items)
	return out

static func _stamp_rect_item(raw) -> Dictionary:
	if raw is CustomStampBlob:
		var blob: CustomStampBlob = raw
		return {"hash": blob.stamp_hash, "width": blob.width, "height": blob.height}
	if typeof(raw) != TYPE_DICTIONARY:
		return {}
	var width := int(raw.get("width", raw.get("w", 0)))
	var height := int(raw.get("height", raw.get("h", 0)))
	var hash := str(raw.get("hash", raw.get("id", "")))
	if hash == "" or CustomStampBlob.validate_dimensions(width, height) != "":
		return {}
	return {"hash": hash, "width": width, "height": height}

static func _compare_pack_items(a: Dictionary, b: Dictionary) -> bool:
	var a_area := int(a["width"]) * int(a["height"])
	var b_area := int(b["width"]) * int(b["height"])
	if a_area == b_area:
		return maxi(int(a["width"]), int(a["height"])) > maxi(int(b["width"]), int(b["height"]))
	return a_area > b_area

static func _try_pack_region(items: Array, region_size: Vector2i, pixel_count: int) -> Dictionary:
	var root := {"x": 0, "y": 0, "w": region_size.x, "h": region_size.y, "used": false}
	var placements := {}
	for item in items:
		var placed := _insert_with_rotation(root, int(item["width"]), int(item["height"]))
		if placed.is_empty():
			return {"ok": false}
		placements[str(item["hash"])] = {
			"rect": Rect2i(int(placed["x"]), int(placed["y"]), int(placed["w"]), int(placed["h"])),
			"rotated": bool(placed["rotated"]),
			"region_size": region_size,
		}
	return {"ok": true, "region_size": region_size, "placements": placements, "pixel_count": pixel_count}

static func _insert_with_rotation(node: Dictionary, width: int, height: int) -> Dictionary:
	var placed := _insert(node, width, height, false)
	if !placed.is_empty():
		return placed
	if width == height:
		return {}
	return _insert(node, height, width, true)

static func _insert(node: Dictionary, width: int, height: int, rotated: bool) -> Dictionary:
	if bool(node.get("used", false)):
		var right: Dictionary = node.get("right", {})
		var down: Dictionary = node.get("down", {})
		var placed := _insert(right, width, height, rotated)
		if !placed.is_empty():
			return placed
		return _insert(down, width, height, rotated)
	var node_w := int(node["w"])
	var node_h := int(node["h"])
	if width > node_w or height > node_h:
		return {}
	node["used"] = true
	node["right"] = {"x": int(node["x"]) + width, "y": int(node["y"]), "w": node_w - width, "h": height, "used": false}
	node["down"] = {"x": int(node["x"]), "y": int(node["y"]) + height, "w": node_w, "h": node_h - height, "used": false}
	return {"x": int(node["x"]), "y": int(node["y"]), "w": width, "h": height, "rotated": rotated}
