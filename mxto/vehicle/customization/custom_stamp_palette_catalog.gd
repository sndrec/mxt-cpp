class_name CustomStampPaletteCatalog
extends RefCounted

const PALETTE_MIN_ID := 1
const PALETTE_MAX_ID := 255
const PALETTE_RGB332 := 1

static var _palette_cache := {}

static func get_palette(palette_id: int) -> PackedColorArray:
	palette_id = clampi(palette_id, PALETTE_MIN_ID, PALETTE_MAX_ID)
	if _palette_cache.has(palette_id):
		return _palette_cache[palette_id]
	var palette := _build_palette(palette_id)
	_palette_cache[palette_id] = palette
	return palette

static func palette_name(palette_id: int) -> String:
	if palette_id == PALETTE_RGB332:
		return "RGB332"
	return "Palette %03d" % palette_id

static func normalize_custom_palette(source: PackedColorArray) -> PackedColorArray:
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

static func _build_palette(palette_id: int) -> PackedColorArray:
	if palette_id == PALETTE_RGB332:
		return _rgb332_palette()
	var out := PackedColorArray()
	out.resize(256)
	out[0] = Color(1.0, 1.0, 1.0, 0.0)
	var hue_offset := float((palette_id * 37) % 255) / 255.0
	var saturation_bias := 0.45 + float((palette_id * 17) % 40) / 100.0
	var value_bias := 0.38 + float((palette_id * 23) % 45) / 100.0
	for index in range(1, 256):
		var band := int((index - 1) / 16)
		var step := int((index - 1) % 16)
		var hue := fmod(hue_offset + float(band) / 16.0 + float(step % 4) * 0.018, 1.0)
		var saturation := clampf(saturation_bias + float(step - 8) * 0.025, 0.15, 1.0)
		var value := clampf(value_bias + float(step) / 20.0, 0.08, 1.0)
		out[index] = Color.from_hsv(hue, saturation, value, 1.0)
	out[1] = Color(0.0, 0.0, 0.0, 1.0)
	out[2] = Color(1.0, 1.0, 1.0, 1.0)
	return out

static func _rgb332_palette() -> PackedColorArray:
	var palette := PackedColorArray()
	palette.resize(256)
	palette[0] = Color(1.0, 1.0, 1.0, 0.0)
	for index in range(1, 256):
		var r := float((index >> 5) & 0x07) / 7.0
		var g := float((index >> 2) & 0x07) / 7.0
		var b := float(index & 0x03) / 3.0
		palette[index] = Color(r, g, b, 1.0)
	palette[1] = Color(0.0, 0.0, 0.0, 1.0)
	return palette
