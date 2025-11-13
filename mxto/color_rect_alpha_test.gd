@tool
extends ColorRect

var frame := 0

func _process(delta: float) -> void:
	frame += 1
	if frame % 2 == 0:
		modulate.a = 0
	else:
		modulate.a = 1
