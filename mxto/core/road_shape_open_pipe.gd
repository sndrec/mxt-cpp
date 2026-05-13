class_name RoadShapePipeOpen extends RoadShape

@export var openness : Resource

func _init() -> void:
	if !openness:
		openness = ClassDB.instantiate("TrackEditorFloatCurve")
		openness.set_point_value(0, 1.0)
		openness.set_point_value(1, 1.0)

func find_t_from_relative_pos(in_pos : Vector3) -> Vector2:
	return openness.find_open_pipe_t_from_relative_pos(in_pos)
