extends Node3D

var firstFrame : bool = true

func _ready() -> void:
	$AnimationPlayer.play("spawn")

func _process( _delta:float ) -> void:
	if $AnimationPlayer.current_animation_position >= $AnimationPlayer.current_animation_length:
		queue_free()
