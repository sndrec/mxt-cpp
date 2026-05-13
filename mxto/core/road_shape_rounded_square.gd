class_name RoadShapeRoundedSquare extends RoadShape

@export var width : Resource
@export var height : Resource
@export var radius : Resource

func _constant_curve(value : float) -> Resource:
	var curve : Resource = ClassDB.instantiate("TrackEditorFloatCurve")
	curve.set_point_value(0, value)
	curve.set_point_value(1, value)
	return curve

func _init() -> void:
	if !width:
		width = _constant_curve(1.0)
	if !height:
		height = _constant_curve(1.0)
	if !radius:
		radius = _constant_curve(0.0)
