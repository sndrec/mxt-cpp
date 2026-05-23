class_name CustomStampBlob
extends Resource

const CustomStampPaletteCatalog = preload("res://vehicle/customization/custom_stamp_palette_catalog.gd")

const MIN_DIMENSION := 8
const MAX_DIMENSION := 256
const PLAYER_INDEXED_PIXEL_BUDGET := 32768
const COMPRESSED_BYTE_CAP := 24 * 1024
const BPP_CUSTOM_PALETTE := 4
const BPP_AUTHORED_PALETTE := 8
const PALETTE_RGB332 := CustomStampPaletteCatalog.PALETTE_RGB332
const COMPRESSION_MODE := FileAccess.COMPRESSION_ZSTD

@export var stamp_hash: String = ""
@export var width: int = 0
@export var height: int = 0
@export var bits_per_pixel: int = BPP_AUTHORED_PALETTE
@export var palette_id: int = 0
@export var custom_palette: PackedColorArray = PackedColorArray()
@export var compressed_indices: PackedByteArray = PackedByteArray()
@export var uncompressed_size: int = 0

var _decoded_indices := PackedByteArray()

static func from_index_bytes(in_width: int, in_height: int, in_bits_per_pixel: int, in_palette_id: int, raw_indices: PackedByteArray, in_custom_palette: PackedColorArray = PackedColorArray()):
	var blob := CustomStampBlob.new()
	blob.width = in_width
	blob.height = in_height
	blob.bits_per_pixel = in_bits_per_pixel
	blob.palette_id = in_palette_id
	blob.custom_palette = in_custom_palette
	blob.uncompressed_size = raw_indices.size()
	blob._decoded_indices = raw_indices.duplicate()
	blob.stamp_hash = hash_stamp_bytes(in_width, in_height, in_bits_per_pixel, in_palette_id, raw_indices, in_custom_palette)
	blob.compressed_indices = raw_indices.compress(COMPRESSION_MODE)
	if blob.compressed_indices.is_empty() and !raw_indices.is_empty():
		blob.compressed_indices = raw_indices
	return blob

static func from_image(image: Image):
	if image == null:
		return null
	var width := image.get_width()
	var height := image.get_height()
	if validate_dimensions(width, height) != "":
		return null
	image.convert(Image.FORMAT_RGBA8)
	var custom_palette := _palette_from_image_if_small(image)
	if !custom_palette.is_empty():
		var raw_4 := _index_image_custom_palette(image, custom_palette)
		return from_index_bytes(width, height, BPP_CUSTOM_PALETTE, 0, raw_4, custom_palette)
	var raw_8 := _index_image_rgb332(image)
	return from_index_bytes(width, height, BPP_AUTHORED_PALETTE, PALETTE_RGB332, raw_8)

static func from_image_with_authored_palette(image: Image, palette_id: int, palette: PackedColorArray):
	if image == null:
		return null
	var width := image.get_width()
	var height := image.get_height()
	if validate_dimensions(width, height) != "":
		return null
	image.convert(Image.FORMAT_RGBA8)
	var raw := _index_image_to_palette(image, palette)
	return from_index_bytes(width, height, BPP_AUTHORED_PALETTE, palette_id, raw)

func to_manifest(include_custom_palette := true) -> Dictionary:
	var manifest := {
		"hash": stamp_hash,
		"width": width,
		"height": height,
		"bits_per_pixel": bits_per_pixel,
		"palette_id": palette_id,
		"uncompressed_size": uncompressed_size,
		"compressed_size": compressed_indices.size(),
	}
	if include_custom_palette and bits_per_pixel == BPP_CUSTOM_PALETTE:
		manifest["custom_palette"] = _palette_to_html_array(custom_palette)
	return manifest

func to_cache_dict() -> Dictionary:
	var out := to_manifest(true)
	out["compressed_indices_base64"] = Marshalls.raw_to_base64(compressed_indices)
	return out

func from_cache_dict(data: Dictionary) -> void:
	if data.has("hash"):
		stamp_hash = str(data["hash"])
	if data.has("width"):
		width = int(data["width"])
	if data.has("height"):
		height = int(data["height"])
	if data.has("bits_per_pixel"):
		bits_per_pixel = int(data["bits_per_pixel"])
	if data.has("palette_id"):
		palette_id = int(data["palette_id"])
	if data.has("uncompressed_size"):
		uncompressed_size = int(data["uncompressed_size"])
	if data.has("custom_palette") and typeof(data["custom_palette"]) == TYPE_ARRAY:
		custom_palette = _html_array_to_palette(data["custom_palette"])
	if data.has("compressed_indices_base64"):
		compressed_indices = Marshalls.base64_to_raw(str(data["compressed_indices_base64"]))
	_decoded_indices = PackedByteArray()

func validate_blob() -> String:
	var dimension_error := validate_dimensions(width, height)
	if dimension_error != "":
		return dimension_error
	if bits_per_pixel != BPP_CUSTOM_PALETTE and bits_per_pixel != BPP_AUTHORED_PALETTE:
		return "unsupported bits_per_pixel"
	var expected_size := index_byte_size(width, height, bits_per_pixel)
	if uncompressed_size != expected_size:
		return "uncompressed size does not match dimensions"
	if compressed_indices.size() <= 0:
		return "missing compressed stamp data"
	if compressed_indices.size() > COMPRESSED_BYTE_CAP:
		return "compressed stamp data exceeds per-player cap"
	if bits_per_pixel == BPP_CUSTOM_PALETTE and custom_palette.size() > 16:
		return "custom palette exceeds 16 entries"
	var raw := decompress_indices()
	if raw.size() != expected_size:
		return "compressed stamp data did not decompress to expected size"
	var index_error := validate_palette_indices(raw, width, height, bits_per_pixel)
	if index_error != "":
		return index_error
	var expected_hash := hash_stamp_bytes(width, height, bits_per_pixel, palette_id, raw, custom_palette)
	if stamp_hash != "" and stamp_hash != expected_hash:
		return "stamp hash mismatch"
	if stamp_hash == "":
		stamp_hash = expected_hash
	return ""

func decompress_indices() -> PackedByteArray:
	if !_decoded_indices.is_empty() and _decoded_indices.size() == uncompressed_size:
		return _decoded_indices
	if uncompressed_size <= 0:
		return PackedByteArray()
	var raw := compressed_indices.decompress(uncompressed_size, COMPRESSION_MODE)
	if raw.is_empty() and compressed_indices.size() == uncompressed_size:
		_decoded_indices = compressed_indices.duplicate()
	else:
		_decoded_indices = raw
	return _decoded_indices

static func validate_dimensions(in_width: int, in_height: int) -> String:
	if !_is_power_of_two(in_width) or !_is_power_of_two(in_height):
		return "stamp dimensions must be powers of two"
	if in_width < MIN_DIMENSION or in_height < MIN_DIMENSION:
		return "stamp dimensions are below the minimum"
	if in_width > MAX_DIMENSION or in_height > MAX_DIMENSION:
		return "stamp dimensions exceed the maximum"
	if in_width * in_height > PLAYER_INDEXED_PIXEL_BUDGET:
		return "stamp exceeds per-player pixel budget"
	return ""

static func index_byte_size(in_width: int, in_height: int, in_bits_per_pixel: int) -> int:
	var pixels := in_width * in_height
	if in_bits_per_pixel == BPP_CUSTOM_PALETTE:
		return int((pixels + 1) / 2)
	return pixels

static func validate_palette_indices(raw: PackedByteArray, in_width: int, in_height: int, in_bits_per_pixel: int) -> String:
	var expected_size := index_byte_size(in_width, in_height, in_bits_per_pixel)
	if raw.size() != expected_size:
		return "palette index data has the wrong size"
	if in_bits_per_pixel == BPP_CUSTOM_PALETTE:
		return ""
	for value in raw:
		if int(value) < 0 or int(value) > 255:
			return "palette index outside 8-bit range"
	return ""

static func rgb332_palette() -> PackedColorArray:
	return CustomStampPaletteCatalog.get_palette(PALETTE_RGB332)

static func hash_stamp_bytes(in_width: int, in_height: int, in_bits_per_pixel: int, in_palette_id: int, raw_indices: PackedByteArray, in_custom_palette: PackedColorArray = PackedColorArray()) -> String:
	var bytes := ("%d:%d:%d:%d:" % [in_width, in_height, in_bits_per_pixel, in_palette_id]).to_utf8_buffer()
	for colour in in_custom_palette:
		bytes.append_array(colour.to_html(true).to_utf8_buffer())
		bytes.append(59)
	bytes.append_array(raw_indices)
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(bytes)
	return context.finish().hex_encode()

static func _is_power_of_two(value: int) -> bool:
	return value > 0 and (value & (value - 1)) == 0

static func _palette_to_html_array(palette: PackedColorArray) -> Array:
	var out: Array = []
	for colour in palette:
		out.append(colour.to_html(true))
	return out

static func _html_array_to_palette(values: Array) -> PackedColorArray:
	var out := PackedColorArray()
	var count := mini(values.size(), 16)
	for i in range(count):
		out.append(Color.html(str(values[i])))
	return out

static func _palette_from_image_if_small(image: Image) -> PackedColorArray:
	var colours := PackedColorArray()
	colours.append(Color(1.0, 1.0, 1.0, 0.0))
	var seen := {}
	for y in range(image.get_height()):
		for x in range(image.get_width()):
			var colour := image.get_pixel(x, y)
			if colour.a <= 0.0:
				continue
			var key := _colour_key(colour)
			if seen.has(key):
				continue
			if colours.size() >= 16:
				return PackedColorArray()
			seen[key] = colours.size()
			colours.append(_quantized_colour(colour))
	return colours

static func _index_image_custom_palette(image: Image, palette: PackedColorArray) -> PackedByteArray:
	var pixels := image.get_width() * image.get_height()
	var raw := PackedByteArray()
	raw.resize(index_byte_size(image.get_width(), image.get_height(), BPP_CUSTOM_PALETTE))
	var palette_lookup := {}
	for i in range(palette.size()):
		palette_lookup[_colour_key(palette[i])] = i
	for pixel_index in range(pixels):
		var x := pixel_index % image.get_width()
		var y := int(pixel_index / image.get_width())
		var colour := image.get_pixel(x, y)
		var index := 0
		if colour.a > 0.0:
			index = int(palette_lookup.get(_colour_key(colour), 0))
		var byte_index := int(pixel_index / 2)
		if (pixel_index & 1) == 0:
			raw[byte_index] = (raw[byte_index] & 0xf0) | (index & 0x0f)
		else:
			raw[byte_index] = (raw[byte_index] & 0x0f) | ((index & 0x0f) << 4)
	return raw

static func _index_image_rgb332(image: Image) -> PackedByteArray:
	return _index_image_to_palette(image, rgb332_palette())

static func _index_image_to_palette(image: Image, palette: PackedColorArray) -> PackedByteArray:
	var raw := PackedByteArray()
	raw.resize(image.get_width() * image.get_height())
	for y in range(image.get_height()):
		for x in range(image.get_width()):
			var colour := image.get_pixel(x, y)
			var pixel_index := y * image.get_width() + x
			if colour.a <= 0.0:
				raw[pixel_index] = 0
				continue
			raw[pixel_index] = _nearest_palette_index(colour, palette)
	return raw

static func _rgb332_index(colour: Color) -> int:
	var r := clampi(roundi(clampf(colour.r, 0.0, 1.0) * 7.0), 0, 7)
	var g := clampi(roundi(clampf(colour.g, 0.0, 1.0) * 7.0), 0, 7)
	var b := clampi(roundi(clampf(colour.b, 0.0, 1.0) * 3.0), 0, 3)
	var index := (r << 5) | (g << 2) | b
	return 1 if index == 0 else index

static func _nearest_palette_index(colour: Color, palette: PackedColorArray) -> int:
	var best_index := 1
	var best_distance := INF
	for index in range(1, palette.size()):
		var candidate := palette[index]
		var dr := colour.r - candidate.r
		var dg := colour.g - candidate.g
		var db := colour.b - candidate.b
		var distance := dr * dr + dg * dg + db * db
		if distance < best_distance:
			best_distance = distance
			best_index = index
	return best_index

static func _colour_key(colour: Color) -> String:
	return _quantized_colour(colour).to_html(true)

static func _quantized_colour(colour: Color) -> Color:
	return Color(
		float(clampi(roundi(clampf(colour.r, 0.0, 1.0) * 255.0), 0, 255)) / 255.0,
		float(clampi(roundi(clampf(colour.g, 0.0, 1.0) * 255.0), 0, 255)) / 255.0,
		float(clampi(roundi(clampf(colour.b, 0.0, 1.0) * 255.0), 0, 255)) / 255.0,
		float(clampi(roundi(clampf(colour.a, 0.0, 1.0) * 255.0), 0, 255)) / 255.0
	)
