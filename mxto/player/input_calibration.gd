class_name InputCalibration
extends Resource

const CAL_PATH := "user://controller_calibration.json"
const CAL_POINT_COUNT := 64

var radii: Array = []

static func default_radii() -> Array:
	var a := []
	for i in range(CAL_POINT_COUNT):
		a.append(1.0)
	return a

static func load_from_disk() -> InputCalibration:
	var ic := InputCalibration.new()
	ic.radii = default_radii()
	if FileAccess.file_exists(CAL_PATH):
		var txt := FileAccess.get_file_as_string(CAL_PATH)
		var data = JSON.parse_string(txt)
		if typeof(data) == TYPE_DICTIONARY and data.has("radii"):
			var arr: Array = data["radii"]
			if arr.size() == CAL_POINT_COUNT:
				ic.radii = arr.duplicate(true)
			elif arr.size() == 8:
				ic.radii = _legacy_radii_to_circle(arr)
	return ic

static func _legacy_radii_to_circle(legacy: Array) -> Array:
	var expanded := default_radii()
	if legacy.size() != 8:
		return expanded
	var legacy_step := TAU / 8.0
	for i in range(CAL_POINT_COUNT):
		var phi := TAU * float(i) / float(CAL_POINT_COUNT)
		var idx := int(floor(phi / legacy_step)) % 8
		var idx2 := (idx + 1) % 8
		var t := (phi - float(idx) * legacy_step) / legacy_step
		var r1 := float(legacy[idx])
		var r2 := float(legacy[idx2])
		expanded[i] = lerp(r1, r2, t)
	return expanded

static func apply_vec(v: Vector2, radii: Array) -> Vector2:
	if v == Vector2.ZERO:
		return v
	if radii.is_empty():
		return v
	var any_positive := false
	for rtest in radii:
		if float(rtest) > 0.0:
			any_positive = true
			break
	if !any_positive:
		return v
	var phi := fposmod(atan2(v.y, v.x), TAU)
	var step := TAU / float(CAL_POINT_COUNT)
	var i := int(floor(phi / step)) % CAL_POINT_COUNT
	var i2 := (i + 1) % CAL_POINT_COUNT
	var t := (phi - float(i) * step) / step
	var r1 := float(radii[i])
	var r2 := float(radii[i2])
	var r_meas = lerp(r1, r2, t)
	var denom = max(r_meas, 0.0001)
	var c = abs(cos(phi))
	var s = abs(sin(phi))
	var r_square = 1.0 / max(c, s)
	return v * (r_square / denom)

func apply(v: Vector2) -> Vector2:
	return apply_vec(v, radii)
