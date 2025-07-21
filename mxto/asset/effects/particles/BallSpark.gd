extends ScreenSpaceParticle

var lifeTime := Time.get_ticks_msec() + randi_range(250, 500)

func _init() -> void:
	particleColor = Vector3(randf_range(1, 3), randf_range(0.0, 2), randf_range(0.0, 0.5)) * 3

func _particle_process( delta:float ) -> bool:
	velocity += Vector3(0, -32 * delta, 0)
	velocity += -velocity * delta
	%SparkCast.target_position = to_local(position + velocity * delta)
	%SparkCast.force_raycast_update()
	if %SparkCast.is_colliding():
		position = %SparkCast.get_collision_point() + %SparkCast.get_collision_normal() * 0.01
		velocity = velocity + %SparkCast.get_collision_normal() * velocity.dot(%SparkCast.get_collision_normal()) * -1.5
	else:
		position = position + velocity * delta
	particleSize = lerp(0.025, 0.0, float(Time.get_ticks_msec() - spawnTime) / (lifeTime - spawnTime))
	if particleSize <= 0:
		queue_free()
	return false
