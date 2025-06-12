class_name CarNodeContainer extends Node3D

var num_cars := 30
var car_pos : PackedVector3Array = []
var car_bx : PackedVector3Array = []
var car_by : PackedVector3Array = []
var car_bz : PackedVector3Array = []
@onready var multi_mesh_instance_3d: MultiMeshInstance3D = $MultiMeshInstance3D
