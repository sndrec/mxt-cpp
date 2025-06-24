class_name PlayerSettings
extends Resource

@export var username: String = "Player"
@export var car_definition_path: String = ""
@export var accel_setting: float = 1.0

func to_dict() -> Dictionary:
    return {
        "username": username,
        "car_definition_path": car_definition_path,
        "accel_setting": accel_setting,
    }

func from_dict(data: Dictionary) -> void:
    if data.has("username"):
        username = str(data["username"])
    if data.has("car_definition_path"):
        car_definition_path = str(data["car_definition_path"])
    if data.has("accel_setting"):
        accel_setting = float(data["accel_setting"])
