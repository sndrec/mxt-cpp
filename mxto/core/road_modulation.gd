class_name RoadModulation extends Resource

@export var modulation_effect : Resource
@export var modulation_height : Resource

func _init() -> void:
	if !modulation_effect:
		modulation_effect = ClassDB.instantiate("TrackEditorFloatCurve")
	if !modulation_height:
		modulation_height = ClassDB.instantiate("TrackEditorFloatCurve")
	if modulation_effect.point_count == 0:
		modulation_effect.add_point(Vector2(0.0, 0.0))
		modulation_effect.add_point(Vector2(1.0, 0.0))
	if modulation_height.point_count == 0:
		modulation_height.add_point(Vector2(0.0, 0.0))
		modulation_height.add_point(Vector2(1.0, 0.0))
