extends SceneTree

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const PlayerSettings = preload("res://player/player_settings.gd")

func _init() -> void:
	var livery := CarLivery.new()
	livery.vehicle_content_id = "mxt:vehicle:official:test"
	livery.primary_colour = Color(0.2, 0.4, 0.8, 1.0)
	livery.secondary_colour = Color(0.9, 0.8, 0.7, 1.0)
	livery.accent_colour = Color(0.1, 0.2, 0.3, 1.0)

	var accepted := 0
	for i in range(CarLivery.MAX_STAMPS + 1):
		var stamp := CarLiveryStamp.new()
		stamp.stamp_id = "stamp_%02d" % i
		stamp.layer = CarLivery.MAX_STAMPS - i
		stamp.local_origin = Vector3(float(i), float(i) * 0.5, -float(i))
		stamp.local_basis = Basis.from_euler(Vector3(0.1 * float(i), 0.2, 0.3))
		stamp.rotation = 0.25 * float(i)
		stamp.size = Vector2(0.5 + float(i) * 0.01, 0.75 + float(i) * 0.02)
		stamp.projection_depth = 0.1 + float(i) * 0.01
		stamp.colour = Color(0.05 * float(i), 0.4, 0.9, 0.8)
		stamp.opacity = 0.5
		if livery.add_stamp(stamp):
			accepted += 1

	if accepted != CarLivery.MAX_STAMPS or livery.stamps.size() != CarLivery.MAX_STAMPS:
		push_error("livery should clamp accepted stamps to MAX_STAMPS")
		quit(1)
		return

	var sorted := livery.get_sorted_stamps()
	for i in range(1, sorted.size()):
		if sorted[i - 1].layer > sorted[i].layer:
			push_error("livery stamps are not sorted by layer")
			quit(1)
			return

	var roundtrip := CarLivery.new()
	roundtrip.from_dict(livery.to_dict())
	if roundtrip.stamps.size() != CarLivery.MAX_STAMPS:
		push_error("roundtrip livery lost stamps")
		quit(1)
		return
	if roundtrip.to_dict() != livery.to_dict():
		push_error("roundtrip livery dictionary mismatch")
		quit(1)
		return

	var settings := PlayerSettings.new()
	settings.username = "LiverySmoke"
	settings.vehicle_content_id = livery.vehicle_content_id
	settings.set_car_livery(livery)
	var restored_settings := PlayerSettings.new()
	restored_settings.from_dict(settings.to_dict())
	var restored_livery := restored_settings.get_car_livery_resource()
	if restored_livery.to_dict() != livery.to_dict():
		push_error("player settings did not preserve livery data")
		quit(1)
		return

	print("MXT_CAR_LIVERY_SERIALIZATION_SMOKE stamps=", roundtrip.stamps.size())
	quit(0)
