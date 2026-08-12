extends SceneTree

const CAR_PATHS := [
	"res://vehicle/asset/accelerator/golden_fox.mxt_car_props",
	"res://vehicle/asset/allrounder/blue_falcon.mxt_car_props",
	"res://vehicle/asset/bruiser/wild_goose.mxt_car_props",
	"res://vehicle/asset/topspeeder/fire_stingray.mxt_car_props",
]
const STAT_COUNT := 40
const LIVE_STAT_COUNT := 38
const MODIFIER_LAYER_COUNT := 6


func _fail(message: String) -> void:
	push_error("car_properties_sampler_smoke: " + message)
	print("MXT_CAR_PROPERTIES_SAMPLER_FAIL")
	quit(1)


func _validate_sample(sample: Dictionary, label: String) -> bool:
	if sample.has("error"):
		_fail("%s: %s" % [label, sample.error])
		return false
	var base_stats: Dictionary = sample.get("base_stats", {})
	var modifier_stats: Dictionary = sample.get("modifier_stats", {})
	var s_boost_stats: Dictionary = sample.get("s_boost_stats", {})
	if base_stats.size() != STAT_COUNT:
		_fail("%s: expected %d base stats, got %d" % [label, STAT_COUNT, base_stats.size()])
		return false
	if s_boost_stats.size() != LIVE_STAT_COUNT:
		_fail("%s: expected %d S-BOOST stats, got %d" % [label, LIVE_STAT_COUNT, s_boost_stats.size()])
		return false
	if modifier_stats.size() != MODIFIER_LAYER_COUNT:
		_fail("%s: expected %d modifier layers, got %d" % [label, MODIFIER_LAYER_COUNT, modifier_stats.size()])
		return false
	for layer_name in modifier_stats:
		if not modifier_stats[layer_name] is Dictionary or modifier_stats[layer_name].size() != LIVE_STAT_COUNT:
			_fail("%s: modifier layer %s has the wrong stat count" % [label, layer_name])
			return false
	for stat_name in base_stats:
		if not is_finite(float(base_stats[stat_name])):
			_fail("%s: base stat %s is non-finite" % [label, stat_name])
			return false
	return true


func _initialize() -> void:
	var sim := GameSim.new()
	for path in CAR_PATHS:
		var bytes := FileAccess.get_file_as_bytes(path)
		if bytes.is_empty():
			_fail("could not read " + path)
			return
		var zero := sim.sample_car_properties(bytes, 0.0)
		var middle := sim.sample_car_properties(bytes, 0.5)
		var one := sim.sample_car_properties(bytes, 1.0)
		if not _validate_sample(zero, path + "@0"):
			return
		if not _validate_sample(middle, path + "@0.5"):
			return
		if not _validate_sample(one, path + "@1"):
			return
		if sim.sample_car_properties(bytes, -10.0).base_stats != zero.base_stats:
			_fail(path + ": lower machine-setting clamp differs from 0")
			return
		if sim.sample_car_properties(bytes, 10.0).base_stats != one.base_stats:
			_fail(path + ": upper machine-setting clamp differs from 1")
			return

		var bad_magic := bytes.duplicate()
		bad_magic[0] = bad_magic[0] ^ 0xff
		if not sim.sample_car_properties(bad_magic, 0.5).has("error"):
			_fail(path + ": corrupt magic was accepted")
			return
		var bad_crc := bytes.duplicate()
		bad_crc[bad_crc.size() - 1] = bad_crc[bad_crc.size() - 1] ^ 0x01
		if not sim.sample_car_properties(bad_crc, 0.5).has("error"):
			_fail(path + ": corrupt payload was accepted")
			return

	print("MXT_CAR_PROPERTIES_SAMPLER_OK cars=", CAR_PATHS.size())
	quit(0)
