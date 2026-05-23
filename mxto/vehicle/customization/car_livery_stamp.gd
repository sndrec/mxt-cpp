class_name CarLiveryStamp
extends Resource

const MIN_SIZE := 0.001
const SOURCE_BASE := "base"
const SOURCE_CUSTOM := "custom"

@export var stamp_id: String = ""
@export var source: String = SOURCE_BASE
@export var custom_hash: String = ""
@export var palette_id: int = 0
@export var custom_rect: Rect2 = Rect2()
@export var custom_rect_rotated: bool = false
@export var enabled: bool = true
@export var layer: int = 0
@export var local_origin: Vector3 = Vector3.ZERO
@export var local_basis: Basis = Basis.IDENTITY
@export var rotation: float = 0.0
@export var size: Vector2 = Vector2.ONE
@export var projection_depth: float = 0.25
@export var colour: Color = Color.WHITE
@export_range(0.0, 1.0, 0.01) var opacity: float = 1.0

func to_dict() -> Dictionary:
	return {
		"stamp_id": stamp_id,
		"source": source,
		"hash": custom_hash,
		"palette_id": palette_id,
		"rect": _rect2_to_array(custom_rect),
		"rect_rotated": custom_rect_rotated,
		"enabled": enabled,
		"layer": layer,
		"local_origin": _vector3_to_array(local_origin),
		"local_basis": _basis_to_array(local_basis),
		"rotation": rotation,
		"size": _vector2_to_array(size),
		"projection_depth": projection_depth,
		"colour": colour.to_html(true),
		"opacity": opacity,
	}

func from_dict(data: Dictionary) -> void:
	if data.has("stamp_id"):
		stamp_id = str(data["stamp_id"])
	if data.has("source"):
		source = str(data["source"])
	if data.has("hash"):
		custom_hash = str(data["hash"])
	if data.has("palette_id"):
		palette_id = int(data["palette_id"])
	if data.has("rect"):
		custom_rect = _array_to_rect2(data["rect"], custom_rect)
	if data.has("rect_rotated"):
		custom_rect_rotated = bool(data["rect_rotated"])
	if data.has("enabled"):
		enabled = bool(data["enabled"])
	if data.has("layer"):
		layer = int(data["layer"])
	if data.has("local_origin"):
		local_origin = _array_to_vector3(data["local_origin"], local_origin)
	if data.has("local_basis"):
		local_basis = _array_to_basis(data["local_basis"], local_basis)
	if data.has("rotation"):
		rotation = float(data["rotation"])
	if data.has("size"):
		size = _array_to_vector2(data["size"], size)
	if data.has("projection_depth"):
		projection_depth = maxf(MIN_SIZE, float(data["projection_depth"]))
	if data.has("colour"):
		colour = Color.html(str(data["colour"]))
	if data.has("opacity"):
		opacity = clampf(float(data["opacity"]), 0.0, 1.0)
	_sanitize()

func is_custom() -> bool:
	return source == SOURCE_CUSTOM

func stamp_key() -> String:
	if is_custom():
		return custom_hash if custom_hash != "" else stamp_id
	return stamp_id

func duplicate_stamp():
	var stamp = get_script().new()
	stamp.from_dict(to_dict())
	return stamp

func _sanitize() -> void:
	if source != SOURCE_CUSTOM:
		source = SOURCE_BASE
	if source == SOURCE_CUSTOM and custom_hash == "" and stamp_id != "":
		custom_hash = stamp_id
	if source == SOURCE_BASE:
		custom_hash = ""
		palette_id = 0
		custom_rect = Rect2()
		custom_rect_rotated = false
	palette_id = maxi(0, palette_id)
	custom_rect.size.x = maxf(0.0, custom_rect.size.x)
	custom_rect.size.y = maxf(0.0, custom_rect.size.y)
	size.x = maxf(MIN_SIZE, size.x)
	size.y = maxf(MIN_SIZE, size.y)
	projection_depth = maxf(MIN_SIZE, projection_depth)
	if local_basis.determinant() == 0.0:
		local_basis = Basis.IDENTITY

static func _vector2_to_array(value: Vector2) -> Array:
	return [value.x, value.y]

static func _vector3_to_array(value: Vector3) -> Array:
	return [value.x, value.y, value.z]

static func _basis_to_array(value: Basis) -> Array:
	return [
		[value.x.x, value.x.y, value.x.z],
		[value.y.x, value.y.y, value.y.z],
		[value.z.x, value.z.y, value.z.z],
	]

static func _rect2_to_array(value: Rect2) -> Array:
	return [value.position.x, value.position.y, value.size.x, value.size.y]

static func _array_to_vector2(value, fallback: Vector2) -> Vector2:
	if typeof(value) != TYPE_ARRAY or value.size() < 2:
		return fallback
	return Vector2(float(value[0]), float(value[1]))

static func _array_to_vector3(value, fallback: Vector3) -> Vector3:
	if typeof(value) != TYPE_ARRAY or value.size() < 3:
		return fallback
	return Vector3(float(value[0]), float(value[1]), float(value[2]))

static func _array_to_rect2(value, fallback: Rect2) -> Rect2:
	if typeof(value) != TYPE_ARRAY or value.size() < 4:
		return fallback
	return Rect2(float(value[0]), float(value[1]), float(value[2]), float(value[3]))

static func _array_to_basis(value, fallback: Basis) -> Basis:
	if typeof(value) != TYPE_ARRAY or value.size() < 3:
		return fallback
	var x = _array_to_vector3(value[0], fallback.x)
	var y = _array_to_vector3(value[1], fallback.y)
	var z = _array_to_vector3(value[2], fallback.z)
	return Basis(x, y, z)
