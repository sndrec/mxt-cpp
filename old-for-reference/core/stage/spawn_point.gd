@tool

class_name SpawnPoint extends Node3D

func _process( delta:float ) -> void:
	$start.rotation.y = $start.rotation.y + delta * 2
	rotation.x = 0
	rotation.z = 0
