class_name RoadEmbed extends Resource

enum EmbedType{
	RECHARGE = 0,
	DIRT = 1,
	ICE = 2,
	LAVA = 3,
	HOLE = 4
}
@export var road_start : float = 0.0
@export var road_end : float = 1.0
@export var left_boundary : Resource
@export var right_boundary : Resource
@export var embed_type : EmbedType = EmbedType.RECHARGE

func _init() -> void:
	if !left_boundary:
		left_boundary = ClassDB.instantiate("TrackEditorFloatCurve")
	if !right_boundary:
		right_boundary = ClassDB.instantiate("TrackEditorFloatCurve")
	if left_boundary.point_count == 0:
		left_boundary.add_point(Vector2(0.0, 0.0))
		left_boundary.add_point(Vector2(1.0, 0.0))
	if right_boundary.point_count == 0:
		right_boundary.add_point(Vector2(0.0, 0.0))
		right_boundary.add_point(Vector2(1.0, 0.0))
