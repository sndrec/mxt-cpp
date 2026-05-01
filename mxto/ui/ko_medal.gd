@tool
extends Control

@onready var killer := $Panel/HBoxContainer/killer
@onready var victim := $Panel/HBoxContainer/victim

func set_names(in_killer: String, in_victim: String) -> void:
	killer.text = in_killer
	victim.text = in_victim

func _process(_delta: float) -> void:
	size.x = killer.size.x + victim.size.x + 64
	pivot_offset = size * 0.5
	position.x = 640 - size.x * 0.5
