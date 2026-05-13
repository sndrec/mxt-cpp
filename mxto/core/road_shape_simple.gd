class_name RoadShape extends Resource

@export var modulation_table : Array[RoadModulation] = []
@export var embed_table : Array[RoadEmbed] = []

func add_modulation(in_mod : RoadModulation) -> void:
	modulation_table.append(in_mod)

func find_t_from_relative_pos(in_pos : Vector3) -> Vector2:
	return Vector2(in_pos.x, in_pos.z)
