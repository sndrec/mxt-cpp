
class_name PlayerInput extends Resource

const RAW_BIT_PRECISION := 254
const AXIS_NEUTRAL := RAW_BIT_PRECISION / 2
const TRIGGER_NEUTRAL := 0

var strafe_left: float = 0.0
var strafe_right: float = 0.0
var steer_horizontal: float = 0.0
var steer_vertical: float = 0.0
var accelerate: float = 0.0
var brake: float = 0.0
var spinattack: bool = false
var boost: bool = false

static func _quantize_axis(v: float) -> int:
	return int(round(remap(clamp(v, -1.0, 1.0), -1.0, 1.0, 0.0, RAW_BIT_PRECISION)))

static func _quantize_trigger(v: float) -> int:
	return int(round(remap(clamp(v, 0.0, 1.0), 0.0, 1.0, 0.0, RAW_BIT_PRECISION)))

static func _dequantize_axis(v: int) -> float:
	return remap(float(v), 0.0, RAW_BIT_PRECISION, -1.0, 1.0)

static func _dequantize_trigger(v: int) -> float:
	return remap(float(v), 0.0, RAW_BIT_PRECISION, 0.0, 1.0)

func apply_quantization() -> void:
	strafe_left = _dequantize_trigger(_quantize_trigger(strafe_left))
	strafe_right = _dequantize_trigger(_quantize_trigger(strafe_right))
	steer_horizontal = _dequantize_axis(_quantize_axis(steer_horizontal))
	steer_vertical = _dequantize_axis(_quantize_axis(steer_vertical))
	accelerate = _dequantize_trigger(_quantize_trigger(accelerate))
	brake = _dequantize_trigger(_quantize_trigger(brake))

func to_dict() -> Dictionary:
	return {
		"strafe_left": strafe_left,
		"strafe_right": strafe_right,
		"steer_horizontal": steer_horizontal,
		"steer_vertical": steer_vertical,
		"accelerate": accelerate,
		"brake": brake,
		"spinattack": spinattack,
		"boost": boost,
	}

func from_dict(data: Dictionary) -> void:
	if data.has("strafe_left"):
		strafe_left = float(data["strafe_left"])
	if data.has("strafe_right"):
		strafe_right = float(data["strafe_right"])
	if data.has("steer_horizontal"):
		steer_horizontal = float(data["steer_horizontal"])
	if data.has("steer_vertical"):
		steer_vertical = float(data["steer_vertical"])
	if data.has("accelerate"):
		accelerate = float(data["accelerate"])
	if data.has("brake"):
		brake = float(data["brake"])
	if data.has("spinattack"):
		spinattack = bool(data["spinattack"])
	if data.has("boost"):
		boost = bool(data["boost"])

func serialize() -> PackedByteArray:
	var buffer := StreamPeerBuffer.new()
	buffer.put_u8(0) # placeholder for bitmask
	var bitmask := 0

	var q := _quantize_trigger(strafe_left)
	if q != TRIGGER_NEUTRAL:
		bitmask |= 1 << 0
		buffer.put_u8(q)

	q = _quantize_trigger(strafe_right)
	if q != TRIGGER_NEUTRAL:
		bitmask |= 1 << 1
		buffer.put_u8(q)

	q = _quantize_axis(steer_horizontal)
	if q != AXIS_NEUTRAL:
		bitmask |= 1 << 2
		buffer.put_u8(q)

	q = _quantize_axis(steer_vertical)
	if q != AXIS_NEUTRAL:
		bitmask |= 1 << 3
		buffer.put_u8(q)

	q = _quantize_trigger(accelerate)
	if q != TRIGGER_NEUTRAL:
		bitmask |= 1 << 4
		buffer.put_u8(q)

	q = _quantize_trigger(brake)
	if q != TRIGGER_NEUTRAL:
		bitmask |= 1 << 5
		buffer.put_u8(q)

	var buttons := 0
	if spinattack:
		buttons |= 1
	if boost:
		buttons |= 2
	if buttons != 0:
		bitmask |= 1 << 6
		buffer.put_u8(buttons)

	var arr := buffer.data_array
	arr[0] = bitmask
	return arr

func deserialize(data: PackedByteArray) -> PlayerInput:
	var buffer := StreamPeerBuffer.new()
	buffer.data_array = data
	var bitmask := buffer.get_u8()

	if bitmask & (1 << 0):
		strafe_left = _dequantize_trigger(buffer.get_u8())
	else:
		strafe_left = 0.0

	if bitmask & (1 << 1):
		strafe_right = _dequantize_trigger(buffer.get_u8())
	else:
		strafe_right = 0.0

	if bitmask & (1 << 2):
		steer_horizontal = _dequantize_axis(buffer.get_u8())
	else:
		steer_horizontal = 0.0

	if bitmask & (1 << 3):
		steer_vertical = _dequantize_axis(buffer.get_u8())
	else:
		steer_vertical = 0.0

	if bitmask & (1 << 4):
		accelerate = _dequantize_trigger(buffer.get_u8())
	else:
		accelerate = 0.0

	if bitmask & (1 << 5):
		brake = _dequantize_trigger(buffer.get_u8())
	else:
		brake = 0.0

	if bitmask & (1 << 6):
		var buttons := buffer.get_u8()
		spinattack = (buttons & 1) != 0
		boost = (buttons & 2) != 0
	else:
		spinattack = false
		boost = false

	return self

static func dict_to_bytes(d: Dictionary) -> PackedByteArray:
	var pi := PlayerInput.new()
	pi.from_dict(d)
	return pi.serialize()

static func bytes_to_dict(data: PackedByteArray) -> Dictionary:
	var pi := PlayerInput.new()
	pi.deserialize(data)
	return pi.to_dict()
