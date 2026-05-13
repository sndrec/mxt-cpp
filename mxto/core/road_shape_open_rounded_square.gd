class_name RoadShapeRoundedSquareOpen
extends "res://core/road_shape_rounded_square.gd"

@export var openness : Resource
@export var open_rotation : Resource

func _init() -> void:
	super()
	if !openness:
		openness = _constant_curve(1.0)
	if !open_rotation:
		open_rotation = _constant_curve(0.0)
