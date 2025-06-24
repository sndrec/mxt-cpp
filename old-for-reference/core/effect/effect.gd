class_name Effect extends Node3D

@export var life_time := 3000
var spawn_time := 0

func _ready() -> void:
	spawn_time = Time.get_ticks_msec()
	for child in get_children():
		if child is GPUParticles3D:
			child.emitting = true

func _process( _delta:float ) -> void:
	if Time.get_ticks_msec() > spawn_time + life_time:
		queue_free()
