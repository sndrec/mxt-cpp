class_name CustomStampAtlasBuilder
extends RefCounted

const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampPaletteCatalog = preload("res://vehicle/customization/custom_stamp_palette_catalog.gd")

const ATLAS_SIZE := Vector2i(2048, 2048)

static func allocate_player_regions(player_ids: Array, manifests: Dictionary) -> Dictionary:
	var items: Array = []
	for player_id in player_ids:
		var manifest: Array = manifests.get(player_id, [])
		if manifest.is_empty():
			continue
		var region_size := _region_size_for_manifest(manifest)
		if region_size == Vector2i.ZERO:
			return {"ok": false, "error": "custom stamp manifest has invalid region size", "player_id": player_id}
		items.append({"player_id": int(player_id), "width": region_size.x, "height": region_size.y})
	items.sort_custom(_compare_region_items)
	var root := {"x": 0, "y": 0, "w": ATLAS_SIZE.x, "h": ATLAS_SIZE.y, "used": false}
	var regions := {}
	for item in items:
		var placed := _insert_region(root, int(item["width"]), int(item["height"]))
		if placed.is_empty():
			return {"ok": false, "error": "custom stamp player regions do not fit the race atlas"}
		regions[int(item["player_id"])] = {
			"origin": Vector2i(int(placed["x"]), int(placed["y"])),
			"size": Vector2i(int(item["width"]), int(item["height"])),
		}
	return {"ok": true, "regions": regions}

static func build_atlas(player_records: Array, authored_palettes: Dictionary = {}) -> Dictionary:
	var atlas := Image.create(ATLAS_SIZE.x, ATLAS_SIZE.y, false, Image.FORMAT_RGBA8)
	atlas.fill(Color(1.0, 1.0, 1.0, 0.0))
	var rects_by_player := {}
	for record in player_records:
		if typeof(record) != TYPE_DICTIONARY:
			continue
		var player_id := int(record.get("player_id", -1))
		var region_origin: Vector2i = record.get("region_origin", Vector2i.ZERO)
		var placements: Dictionary = record.get("placements", {})
		var player_rects := {}
		for blob in record.get("blobs", []):
			var stamp_blob := blob as CustomStampBlob
			if stamp_blob == null:
				continue
			var validation_error := stamp_blob.validate_blob()
			if validation_error != "":
				return {"ok": false, "error": validation_error, "player_id": player_id, "hash": stamp_blob.stamp_hash}
			if !placements.has(stamp_blob.stamp_hash):
				return {"ok": false, "error": "missing atlas placement", "player_id": player_id, "hash": stamp_blob.stamp_hash}
			var placement: Dictionary = placements[stamp_blob.stamp_hash]
			var palette := _palette_for_blob(stamp_blob, authored_palettes)
			if palette.is_empty():
				return {"ok": false, "error": "missing palette", "player_id": player_id, "hash": stamp_blob.stamp_hash}
			_blit_blob(atlas, stamp_blob, placement, region_origin, palette)
			player_rects[stamp_blob.stamp_hash] = {
				"rect": _normalized_rect(placement["rect"], region_origin),
				"rotated": bool(placement.get("rotated", false)),
			}
		if player_id >= 0:
			rects_by_player[player_id] = player_rects
	return {"ok": true, "image": atlas, "texture": ImageTexture.create_from_image(atlas), "rects_by_player": rects_by_player}

static func _region_size_for_manifest(manifest: Array) -> Vector2i:
	var out := Vector2i.ZERO
	for raw_entry in manifest:
		if typeof(raw_entry) != TYPE_DICTIONARY:
			continue
		var entry: Dictionary = raw_entry
		if !entry.has("region_size") or typeof(entry["region_size"]) != TYPE_ARRAY:
			continue
		var region_values: Array = entry["region_size"]
		if region_values.size() < 2:
			continue
		var size := Vector2i(int(region_values[0]), int(region_values[1]))
		if size != Vector2i(256, 128) and size != Vector2i(128, 256):
			return Vector2i.ZERO
		if out == Vector2i.ZERO:
			out = size
		elif out != size:
			return Vector2i.ZERO
	return out

static func _compare_region_items(a: Dictionary, b: Dictionary) -> bool:
	var a_max := maxi(int(a["width"]), int(a["height"]))
	var b_max := maxi(int(b["width"]), int(b["height"]))
	if a_max == b_max:
		return int(a["width"]) > int(b["width"])
	return a_max > b_max

static func _insert_region(node: Dictionary, width: int, height: int) -> Dictionary:
	if bool(node.get("used", false)):
		var right: Dictionary = node.get("right", {})
		var down: Dictionary = node.get("down", {})
		var placed := _insert_region(right, width, height)
		if !placed.is_empty():
			return placed
		return _insert_region(down, width, height)
	var node_w := int(node["w"])
	var node_h := int(node["h"])
	if width > node_w or height > node_h:
		return {}
	node["used"] = true
	node["right"] = {"x": int(node["x"]) + width, "y": int(node["y"]), "w": node_w - width, "h": height, "used": false}
	node["down"] = {"x": int(node["x"]), "y": int(node["y"]) + height, "w": node_w, "h": node_h - height, "used": false}
	return {"x": int(node["x"]), "y": int(node["y"])}

static func _blit_blob(atlas: Image, blob: CustomStampBlob, placement: Dictionary, region_origin: Vector2i, palette: PackedColorArray) -> void:
	var raw := blob.decompress_indices()
	var rect: Rect2i = placement["rect"]
	var rotated := bool(placement.get("rotated", false))
	var region_size: Vector2i = placement.get("region_size", ATLAS_SIZE)
	for y in range(blob.height):
		for x in range(blob.width):
			var index := _index_at(raw, y * blob.width + x, blob.bits_per_pixel)
			var colour := Color(1.0, 1.0, 1.0, 0.0)
			if index > 0 and index < palette.size():
				colour = palette[index]
			var dest := Vector2i(rect.position.x + x, rect.position.y + y)
			if rotated:
				dest = Vector2i(rect.position.x + y, rect.position.y + blob.width - 1 - x)
			if dest.x < 0 or dest.y < 0 or dest.x >= region_size.x or dest.y >= region_size.y:
				continue
			dest += region_origin
			if dest.x >= 0 and dest.y >= 0 and dest.x < ATLAS_SIZE.x and dest.y < ATLAS_SIZE.y:
				atlas.set_pixel(dest.x, dest.y, colour)

static func _index_at(raw: PackedByteArray, pixel_index: int, bits_per_pixel: int) -> int:
	if bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE:
		var packed := int(raw[int(pixel_index / 2)])
		if (pixel_index & 1) == 0:
			return packed & 0x0f
		return (packed >> 4) & 0x0f
	return int(raw[pixel_index])

static func _palette_for_blob(blob: CustomStampBlob, authored_palettes: Dictionary) -> PackedColorArray:
	if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE:
		return _normalized_custom_palette(blob.custom_palette)
	if blob.palette_id == CustomStampBlob.PALETTE_RGB332:
		return CustomStampBlob.rgb332_palette()
	if blob.palette_id >= CustomStampPaletteCatalog.PALETTE_MIN_ID and blob.palette_id <= CustomStampPaletteCatalog.PALETTE_MAX_ID:
		return CustomStampPaletteCatalog.get_palette(blob.palette_id)
	if !authored_palettes.has(blob.palette_id):
		return PackedColorArray()
	return _palette_from_variant(authored_palettes[blob.palette_id], 256)

static func _normalized_custom_palette(source: PackedColorArray) -> PackedColorArray:
	var out := PackedColorArray()
	out.append(Color(1.0, 1.0, 1.0, 0.0))
	if source.size() == 16 and source[0].a <= 0.0:
		out = source
		out[0] = Color(1.0, 1.0, 1.0, 0.0)
		return out
	var start := 1 if source.size() > 0 and source[0].a <= 0.0 else 0
	for i in range(start, mini(source.size(), start + 15)):
		out.append(source[i])
	while out.size() < 16:
		out.append(Color.WHITE)
	return out

static func _palette_from_variant(value, max_size: int) -> PackedColorArray:
	if value is PackedColorArray:
		var colours: PackedColorArray = value
		if colours.size() > 0:
			colours[0] = Color(1.0, 1.0, 1.0, 0.0)
		return colours
	var out := PackedColorArray()
	if typeof(value) != TYPE_ARRAY:
		return out
	var count := mini(value.size(), max_size)
	for i in range(count):
		if typeof(value[i]) == TYPE_COLOR:
			out.append(value[i])
		else:
			out.append(Color.html(str(value[i])))
	if out.size() > 0:
		out[0] = Color(1.0, 1.0, 1.0, 0.0)
	return out

static func _normalized_rect(pixel_rect: Rect2i, region_origin: Vector2i) -> Rect2:
	var pos_i := region_origin + pixel_rect.position
	return Rect2(
		float(pos_i.x) / float(ATLAS_SIZE.x),
		float(pos_i.y) / float(ATLAS_SIZE.y),
		float(pixel_rect.size.x) / float(ATLAS_SIZE.x),
		float(pixel_rect.size.y) / float(ATLAS_SIZE.y)
	)
