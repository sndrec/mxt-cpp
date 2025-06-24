@tool

class_name JumpPlate extends MeshInstance3D

func _post_stage_loaded() -> void:
	pass

func _ready() -> void:
	if Engine.is_editor_hint():
		return

func _process( _delta:float ) -> void:
	return
