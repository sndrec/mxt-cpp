class_name CarDefinition extends Resource

@export var name : String
@export var content_id : String
@export var properties_path : String
@export var car_scene : PackedScene
@export var manual_boost_sfx : AudioStream

var runtime_mesh: Mesh
var runtime_material: Material
var runtime_transform := Transform3D.IDENTITY

func has_visual() -> bool:
	return car_scene != null or runtime_mesh != null
