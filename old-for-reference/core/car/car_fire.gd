@tool

extends ScreenSpaceParticle

func _ready() -> void:
	oldPosition = pos_to_screen_space(position + Vector3(0, 0.01, 0))
	olderPosition = oldPosition
	%ParticleMesh.set_instance_shader_parameter("oldScreenPos", oldPosition)
	position += velocity * 0.002
	%ParticleMesh.set_instance_shader_parameter("currentScreenPos", pos_to_screen_space(position))
	lastUpdate = Time.get_ticks_msec()
	spawnTime = Time.get_ticks_msec()
	%ParticleMesh.get_active_material(0).set_shader_parameter("spriteTexture", particleTexture)
	%ParticleMesh.set_instance_shader_parameter("spriteSize", particleSize)
	var ratio : float = ((float(Time.get_ticks_msec()) * 0.001) - (float(lastUpdate) * 0.001)) / persistence
	var particleStart := olderPosition.lerp(oldPosition, ratio)
	var particleEnd := pos_to_screen_space(position)
	var magnitude := minf((Vector2(particleStart.x, particleStart.y) / absf(particleStart.z) - Vector2(particleEnd.x, particleEnd.y) / absf(particleEnd.z)).length() * absf(maxf(particleStart.z, particleEnd.z)), 1)
	%ParticleMesh.set_instance_shader_parameter("particleModulate", particleColor / (magnitude * 10))
	if Engine.is_editor_hint():
		editorCamera = Camera3D.new()
		editorCamera.position = Vector3(0, 0, -1)
		add_child(editorCamera)

func _particle_process(delta : float) -> bool:
	velocity += -velocity * delta * 60
	position += velocity * delta
	if Time.get_ticks_msec() - spawnTime > 48:
		queue_free()
	return true
