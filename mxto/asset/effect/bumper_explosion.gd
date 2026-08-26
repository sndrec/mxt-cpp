extends Node3D

const LIFE_TIME_MSEC := 2000

var spawn_time_msec := 0


func _ready() -> void:
	spawn_time_msec = Time.get_ticks_msec()
	for child in get_children():
		var particles := child as GPUParticles3D
		if particles != null:
			particles.restart()
			particles.emitting = true


func _process(_delta: float) -> void:
	if Time.get_ticks_msec() >= spawn_time_msec + LIFE_TIME_MSEC:
		queue_free()
