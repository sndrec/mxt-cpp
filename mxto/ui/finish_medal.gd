@tool
extends Control

@onready var finisher := $Panel/HBoxContainer/finisher

func set_finisher_name(in_finisher: String, in_time: String) -> void:
	finisher.text = in_finisher + " finished the race at " + in_time

func _process(_delta: float) -> void:
	size.x = finisher.size.x + 32
	pivot_offset = size * 0.5
	position.x = 640 - size.x * 0.5
