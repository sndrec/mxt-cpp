class_name RoadShapePipe extends RoadShape

func find_t_from_relative_pos(in_pos : Vector3) -> Vector2:
	var tx : float = Vector2(in_pos.x, in_pos.y).normalized().angle() / PI
	return Vector2(tx, in_pos.z)
