class_name RoadShapeCylinder extends RoadShape

func find_t_from_relative_pos(in_pos : Vector3) -> Vector2:
	var tx : float = Vector2(in_pos.x, in_pos.y).normalized().angle() / PI
	tx = 0.5 - tx
	if tx < -1.0:
		tx += 2.0
	if tx > 1.0:
		tx -= 2.0
	return Vector2(tx, in_pos.z)
