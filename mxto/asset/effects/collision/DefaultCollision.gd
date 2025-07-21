extends Node

var spawnTime : int = 0
var sparks : Array = []
var grawlixes : Array = []
var collisionLight : MeshInstance3D

func instantiate_effect(inVel : Vector3, inNormal : Vector3, inPoint : Vector3, inStrength : float) -> void:
	spawnTime = Time.get_ticks_msec()
	var sparkCount = ceil(inStrength * 0.5)
	var grawlixCount = ceil((inStrength * 0.5) - 4)
	sparks.resize(sparkCount)
	grawlixes.resize(grawlixCount)
	for i in sparkCount:
		var spark = preload("res://core/ScreenSpaceParticle.tscn").instantiate()
		spark.particleColor = Vector3(5, 5, 5)
		spark.particleSize = 0.10
		spark.persistence = 0.04
		spark.position = inPoint + inNormal * 0.01
		spark.velocity = inVel * 0.5 + inNormal * 10
		spark.velocity += Vector3(randf_range(-inStrength * 0.5, inStrength * 0.5), randf_range(-inStrength * 0.5, inStrength * 0.5), randf_range(-inStrength * 0.5, inStrength * 0.5))
		spark.velocity *= 1.0
		sparks[i] = spark
		add_child(spark)
		
	for i in grawlixCount:
		var grawlix = preload("res://content/base/common/grawlix.tscn").instantiate()
		grawlix.position = inPoint + inNormal * 0.4
		grawlix.rotation_degrees = Vector3(randf_range(-180, 180), randf_range(-180, 180), randf_range(-180, 180))
		grawlix.velocity = inVel * 0.8
		grawlix.velocity += Vector3(randf_range(-inStrength * 0.5, inStrength * 0.5), randf_range(-inStrength * 0.5, inStrength * 0.5), randf_range(-inStrength * 0.5, inStrength * 0.5))
		grawlix.angleVelocity = Vector3(randf_range(-15, 15), randf_range(-15, 15), randf_range(-15, 15))
		grawlixes[i] = grawlix
		add_child(grawlix)
		
	collisionLight = MeshInstance3D.new()
	var collisionLightPlane : PlaneMesh = PlaneMesh.new()
	collisionLightPlane.size = Vector2(1.0, 1.0)
	collisionLightPlane.orientation = PlaneMesh.FACE_Z
	collisionLight.mesh = collisionLightPlane
	collisionLight.position = inPoint
	collisionLight.basis.z = inNormal
	collisionLight.basis.y = inNormal.cross(Vector3(inNormal.y, inNormal.z, inNormal.x)).normalized()
	collisionLight.basis.x = collisionLight.basis.z.cross(collisionLight.basis.y).normalized()
	collisionLight.scale = Vector3(0, 0, 0)
	add_child(collisionLight)
	

func _process(delta : float) -> void:
	for spark in sparks:
		if spark == null: continue
		spark.particleSize += -delta * 0.4
		spark.velocity += -spark.velocity * delta * 2
		if spark.particleSize <= 0: spark.queue_free()
	for grawlix in grawlixes:
		if grawlix == null: continue
		var newScale : float = grawlix.scale.x - delta
		grawlix.grawlixScale = grawlix.grawlixScale - delta
		if newScale <= 0: grawlix.queue_free()
	var newLightScale = collisionLight.scale.x + delta * 8
	collisionLight.scale = Vector3(newLightScale, newLightScale, newLightScale)
	if Time.get_ticks_msec() > spawnTime + 3000: queue_free()
