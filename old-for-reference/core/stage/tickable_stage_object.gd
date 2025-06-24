@tool
class_name TickableRORStageObject extends RORStageObject

func _ready() -> void:
	super._ready()
	add_to_group("TickableStageObjects")

func tick() -> void:
	on_tick()
	force_update_transform()
	global_transform = global_transform.orthonormalized()

func on_tick() -> void:
	pass
