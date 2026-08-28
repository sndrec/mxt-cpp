class_name CustomStampStore
extends RefCounted

const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampPacker = preload("res://vehicle/customization/custom_stamp_packer.gd")
const CustomStampPaletteCatalog = preload("res://vehicle/customization/custom_stamp_palette_catalog.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")

const CACHE_DIR := "user://custom_stamps"
const LIBRARY_PATH := "user://custom_stamps/library.json"

static var _blob_cache := {}
static var _library_hash_cache: Array = []
static var _library_hash_cache_valid := false

static func build_livery_payload(livery) -> Dictionary:
	if livery == null:
		return {"ok": true, "manifest": [], "blobs": [], "placements": {}}
	var blobs: Array = []
	var blob_by_hash := {}
	var seen := {}
	var sorted_stamps: Array = livery.get_sorted_stamps()
	for stamp in sorted_stamps:
		if stamp == null or !stamp.is_custom():
			continue
		var stamp_hash: String = stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		if stamp_hash == "" or seen.has(stamp_hash):
			continue
		var blob := load_blob(stamp_hash)
		if blob == null:
			return {"ok": false, "error": "missing custom stamp blob", "hash": stamp_hash}
		seen[stamp_hash] = true
		blobs.append(blob)
		blob_by_hash[stamp_hash] = blob
	var total_compressed := 0
	for blob in blobs:
		var stamp_blob := blob as CustomStampBlob
		if stamp_blob == null:
			continue
		total_compressed += stamp_blob.compressed_indices.size()
		if total_compressed > CustomStampBlob.COMPRESSED_BYTE_CAP:
			return {"ok": false, "error": "custom stamps exceed the per-player compressed byte cap", "compressed_size": total_compressed}
	var pack := CustomStampPacker.pack_for_player_region(blobs)
	if !bool(pack.get("ok", false)):
		return pack
	var placements: Dictionary = pack.get("placements", {})
	var manifest: Array = []
	for stamp in sorted_stamps:
		if stamp == null or !stamp.is_custom():
			continue
		var stamp_hash: String = stamp.custom_hash if stamp.custom_hash != "" else stamp.stamp_id
		if !placements.has(stamp_hash):
			return {"ok": false, "error": "custom stamp has no packed placement", "hash": stamp_hash}
		var blob: CustomStampBlob = blob_by_hash.get(stamp_hash, null)
		if blob == null:
			return {"ok": false, "error": "missing custom stamp blob", "hash": stamp_hash}
		var placement: Dictionary = placements[stamp_hash]
		var rect: Rect2i = placement["rect"]
		var region_size: Vector2i = placement["region_size"]
		var entry := blob.to_manifest(true)
		entry["source"] = "custom"
		entry["id"] = stamp.stamp_id
		entry["layer"] = stamp.layer
		entry["rect_pixels"] = [rect.position.x, rect.position.y, rect.size.x, rect.size.y]
		entry["region_size"] = [region_size.x, region_size.y]
		entry["rect_rotated"] = bool(placement.get("rotated", false))
		manifest.append(entry)
	return {"ok": true, "manifest": manifest, "blobs": blobs, "placements": placements}

static func has_blob(stamp_hash: String) -> bool:
	return stamp_hash != "" and FileAccess.file_exists(_cache_path(stamp_hash))

static func load_blob(stamp_hash: String) -> CustomStampBlob:
	if _blob_cache.has(stamp_hash):
		return _blob_cache[stamp_hash]
	if !has_blob(stamp_hash):
		return null
	var data = JSON.parse_string(FileAccess.get_file_as_string(_cache_path(stamp_hash)))
	if typeof(data) != TYPE_DICTIONARY:
		return null
	var blob := CustomStampBlob.new()
	blob.from_cache_dict(data)
	if blob.validate_blob() != "":
		return null
	_blob_cache[stamp_hash] = blob
	return blob

static func save_blob(blob: CustomStampBlob, add_to_library := true) -> Error:
	if blob == null:
		return ERR_INVALID_PARAMETER
	var validation_error := blob.validate_blob()
	if validation_error != "":
		push_warning("Refusing to cache invalid custom stamp: %s" % validation_error)
		return ERR_INVALID_DATA
	var dir := DirAccess.open("user://")
	if dir == null:
		return FileAccess.get_open_error()
	if !dir.dir_exists("custom_stamps"):
		var err := dir.make_dir("custom_stamps")
		if err != OK:
			return err
	var file := FileAccess.open(_cache_path(blob.stamp_hash), FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(JSON.stringify(blob.to_cache_dict()))
	file.close()
	_blob_cache[blob.stamp_hash] = blob
	if add_to_library:
		_add_blob_to_library(blob)
	return OK

static func delete_blob(stamp_hash: String) -> Error:
	if stamp_hash == "":
		return ERR_INVALID_PARAMETER
	var hashes := _read_library_hashes()
	if hashes.has(stamp_hash):
		hashes.erase(stamp_hash)
		_write_library_hashes(hashes)
	var path := _cache_path(stamp_hash)
	if FileAccess.file_exists(path):
		var remove_err := DirAccess.remove_absolute(path)
		if remove_err != OK:
			return remove_err
	_blob_cache.erase(stamp_hash)
	return CarLiveryStore.remove_custom_stamp_references(stamp_hash)

static func list_local_blobs() -> Array:
	var hashes := _read_library_hashes()
	var out: Array = []
	for stamp_hash in hashes:
		var blob := load_blob(stamp_hash)
		if blob != null:
			out.append(blob)
	return out

static func import_png(path: String, palette_id := 0) -> Dictionary:
	if path == "":
		return {"ok": false, "error": "missing file path"}
	var image := Image.load_from_file(path)
	if image == null:
		return {"ok": false, "error": "failed to load png"}
	var blob: CustomStampBlob = null
	if palette_id > 0:
		blob = CustomStampBlob.from_image_with_authored_palette(image, palette_id, CustomStampPaletteCatalog.get_palette(palette_id))
	else:
		blob = CustomStampBlob.from_image(image)
	if blob == null:
		return {"ok": false, "error": CustomStampBlob.validate_dimensions(image.get_width(), image.get_height())}
	var validation_error: String = blob.validate_blob()
	if validation_error != "":
		return {"ok": false, "error": validation_error}
	var save_error := save_blob(blob)
	if save_error != OK:
		return {"ok": false, "error": "failed to save custom stamp"}
	return {"ok": true, "blob": blob}

static func create_preview_texture(blob: CustomStampBlob) -> Texture2D:
	var image := create_preview_image(blob)
	if image == null:
		return null
	return ImageTexture.create_from_image(image)

static func create_preview_image(blob: CustomStampBlob) -> Image:
	if blob == null or blob.validate_blob() != "":
		return null
	var palette := blob.custom_palette if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE else CustomStampPaletteCatalog.get_palette(blob.palette_id)
	if blob.bits_per_pixel == CustomStampBlob.BPP_CUSTOM_PALETTE:
		palette = CustomStampPaletteCatalog.normalize_custom_palette(palette)
	var raw := blob.decompress_indices()
	var image_builder := NativeCustomStampImageBuilder.new()
	return image_builder.build_indexed_image(raw, blob.width, blob.height, blob.bits_per_pixel, palette, false)

static func missing_hashes(manifest: Array) -> PackedStringArray:
	var missing := PackedStringArray()
	for entry in manifest:
		if typeof(entry) != TYPE_DICTIONARY:
			continue
		var stamp_hash := str(entry.get("hash", ""))
		if stamp_hash != "" and !has_blob(stamp_hash):
			missing.append(stamp_hash)
	return missing

static func _cache_path(stamp_hash: String) -> String:
	return "%s/%s.json" % [CACHE_DIR, _safe_hash(stamp_hash)]

static func _add_blob_to_library(blob: CustomStampBlob) -> void:
	var hashes := _read_library_hashes()
	if !hashes.has(blob.stamp_hash):
		hashes.append(blob.stamp_hash)
	_write_library_hashes(hashes)

static func _read_library_hashes() -> Array:
	if _library_hash_cache_valid:
		return _library_hash_cache.duplicate()
	var hashes: Array = []
	var data = JSON.parse_string(FileAccess.get_file_as_string(LIBRARY_PATH)) if FileAccess.file_exists(LIBRARY_PATH) else []
	if typeof(data) == TYPE_ARRAY:
		for value in data:
			var stamp_hash := str(value)
			if stamp_hash != "" and !hashes.has(stamp_hash):
				hashes.append(stamp_hash)
	_library_hash_cache = hashes.duplicate()
	_library_hash_cache_valid = true
	return hashes

static func _write_library_hashes(hashes: Array) -> void:
	var file := FileAccess.open(LIBRARY_PATH, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(JSON.stringify(hashes))
	file.close()
	_library_hash_cache = hashes.duplicate()
	_library_hash_cache_valid = true

static func _safe_hash(stamp_hash: String) -> String:
	var out := ""
	for i in range(stamp_hash.length()):
		var c := stamp_hash.substr(i, 1)
		if c.is_valid_hex_number():
			out += c.to_lower()
	return out
