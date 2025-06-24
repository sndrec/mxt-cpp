extends GPUParticles3D

var life_time := 0
var spawn_time := 0

func _ready() -> void:
	emitting = true
	spawn_time = Time.get_ticks_msec()
	for child in get_children():
		child.emitting = true

func _process( _delta:float ) -> void:
	if Time.get_ticks_msec() > spawn_time + life_time:
		queue_free()
