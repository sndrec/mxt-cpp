class_name PlayerSettings
extends Resource

const CarLivery = preload("res://vehicle/customization/car_livery.gd")

@export var username: String = "Player"
@export var vehicle_content_id: String = ""
@export var vehicle_gameplay_digest: String = ""
@export var vehicle_package_digest: String = ""
@export var vehicle_workshop_id: String = ""
@export var accel_setting: float = 1.0
@export var spectator: bool = false
@export var sticker_1: int = 0
@export var sticker_2: int = 1
@export var sticker_3: int = 2
@export var sticker_4: int = 3
@export var car_livery: Dictionary = {}

func to_dict() -> Dictionary:
		return {
				"username": username,
				"vehicle_content_id": vehicle_content_id,
				"vehicle_gameplay_digest": vehicle_gameplay_digest,
				"vehicle_package_digest": vehicle_package_digest,
				"vehicle_workshop_id": vehicle_workshop_id,
				"accel_setting": accel_setting,
				"spectator": spectator,
				"sticker_1": sticker_1,
				"sticker_2": sticker_2,
				"sticker_3": sticker_3,
				"sticker_4": sticker_4,
				"car_livery": car_livery.duplicate(true),
		}

func from_dict(data: Dictionary) -> void:
		if data.has("username"):
				username = str(data["username"])
		if data.has("vehicle_content_id"):
				vehicle_content_id = str(data["vehicle_content_id"])
		if data.has("vehicle_gameplay_digest"):
				vehicle_gameplay_digest = str(data["vehicle_gameplay_digest"])
		if data.has("vehicle_package_digest"):
				vehicle_package_digest = str(data["vehicle_package_digest"])
		if data.has("vehicle_workshop_id"):
				vehicle_workshop_id = str(data["vehicle_workshop_id"])
		if data.has("accel_setting"):
				accel_setting = float(data["accel_setting"])
		if data.has("spectator"):
				spectator = bool(data["spectator"])
		if data.has("sticker_1"):
				sticker_1 = int(data["sticker_1"])
		if data.has("sticker_2"):
				sticker_2 = int(data["sticker_2"])
		if data.has("sticker_3"):
				sticker_3 = int(data["sticker_3"])
		if data.has("sticker_4"):
				sticker_4 = int(data["sticker_4"])
		if data.has("car_livery") and typeof(data["car_livery"]) == TYPE_DICTIONARY:
				car_livery = data["car_livery"].duplicate(true)

func set_car_livery(livery: CarLivery) -> void:
		if livery == null:
				car_livery = {}
				return
		car_livery = livery.to_dict()

func get_car_livery_resource() -> CarLivery:
		var livery := CarLivery.new()
		if typeof(car_livery) == TYPE_DICTIONARY:
				livery.from_dict(car_livery)
		return livery
