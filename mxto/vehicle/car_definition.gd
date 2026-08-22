class_name CarDefinition extends Resource

@export var name : String
@export var content_id : String
@export var properties_path : String
@export var car_scene : PackedScene
@export var manual_boost_sfx : AudioStream
@export_range(-20.0, 20.0, 0.5, "suffix:dB") var manual_boost_volume_db := 0.0

var runtime_mesh: Mesh
var runtime_material: Material
var runtime_transform := Transform3D.IDENTITY
var runtime_thruster_transforms: Array[Transform3D] = []

func has_visual() -> bool:
	return car_scene != null or runtime_mesh != null
