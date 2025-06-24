@tool

class_name DefaultBall extends Node3D

var ballID : int = 0
var friction : float = 0.6
var maxTilt : float = 23.0
var acceleration : float = 35.28
var restitution : float = 0.5
var radius : float = 0.5
var weight : float = 1.0
var velocity : Vector3 = Vector3.ZERO
var ballPos : Vector3 = Vector3.ZERO

var camBasis : Basis = Basis.IDENTITY
var gravityBasis : Basis = Basis.IDENTITY
var camTransform : Transform3D = Transform3D.IDENTITY
var camVelocity : Vector3 = Vector3.ZERO

var lastGroundNormal : Vector3 = Vector3.UP
var lastGroundSpeed : Vector3 = Vector3.ZERO
var onGround : bool = false

var camMode : int = 0
var camYaw : float = 0
var camPitch : float = 0
var ballCamYaw : float = 0
var ballCamPitch : float = 0
var tiltPitch : float = 0
var tiltRoll : float = 0
var inputX : float = 0
var inputY : float = 0
var camInputX : float = 0
var camInputY : float = 0
var camTurnVelocity : float = 0

var jumpStart : float = 0

var ballPosPrev : Vector3 = Vector3.ZERO
var camYawPrev : float = 0
var camPitchPrev : float = 0
var ballCamYawPrev : float = 0
var ballCamPitchPrev : float = 0
var tiltPitchPrev : float = 0
var tiltRollPrev : float = 0

var tiltPitchLerp : float
var tiltRollLerp : float
var ballCamYawLerp : float
var ballCamPitchLerp : float

var visualStageTilt : Basis = Basis.IDENTITY

var levelStartTime : int = MXGlobal.countdownTime
var state := ballState.STATE_LEADIN
var lastStateChange : int = 0
var leadinCurve : Curve = preload("res://content/base/common/leadin_curve.tres")

var goalTime : float = 0

var soundInts : Array[int] = []
var soundVolumes : Array[float] = []

#var gameUI

enum ballState {
	STATE_LEADIN,
	STATE_PLAY,
	STATE_GOAL,
	STATE_FALL
}

@onready var collider:ShapeCast3D = %BallSphereCollision
@onready var triggertrace:RayCast3D = %BallTriggerChecker
@onready var camera_root := %CameraRoot
@onready var camera:Camera3D = %Camera3D
@onready var ball_mesh := %BallMesh
@onready var ball_roll_sound := %AudioStreamPlayer3D

func _ready() -> void:
	if get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
		camera.make_current()
		#add_child(gameUI)
	add_to_group("Pawns")
	#ballCamYaw = rotation.y
	rotation = Vector3(0, 0, 0)
	ballPos = position
	ballPosPrev = position
	camera.near = 0.25
	camera.far = 15000
	levelStartTime = MXGlobal.countdownTime
	ball_roll_sound.play()
	var rollInt:int = ball_roll_sound.get_stream_playback().play_stream(preload("res://content/base/common/ball_roll.wav"))
	var grindInt:int = ball_roll_sound.get_stream_playback().play_stream(preload("res://content/base/common/ball_grind.wav"))
	soundInts.append(rollInt)
	soundInts.append(grindInt)
	soundVolumes.resize(2)
	if !Engine.is_editor_hint():
		ball_mesh.visible = false
		ball_roll_sound.volume_db = 0
	else:
		ball_roll_sound.volume_db = -100

func _post_stage_loaded() -> void:
	pass

func respawn_ball(pos : Vector3, ang : float) -> void:
	levelStartTime = MXGlobal.currentStageOverseer.localTick + Engine.physics_ticks_per_second * 2
	velocity = Vector3.ZERO
	ballPos = pos
	ballPosPrev = pos
	ballCamYaw = ang
	ballCamYawPrev = ang
	ballCamPitch = 0
	ballCamPitchPrev = 0
	change_state(ballState.STATE_LEADIN)
	ball_mesh.visible = false

func manage_rolling_sound(delta : float) -> void:

	#roll
	var velLen := velocity.length()
	var rollPitch := clampf(remap(velLen, 0.0, 28.0, 0.25, 1.4), 0.25, 1.4)
	var targetRollVolume := clampf(remap(velLen, 0.0, 10.0, -30.0, 0.0), -30.0, 10.0)
	
	#grind
	var grindPitch := clampf(remap(velLen, 0.0, 20.0, 0.4, 1.0), 0.4, 1.0)
	var targetGrindVolume := clampf(remap(velLen, 0.0, 32.0, -50.0, -5.0), -50.0, -5.0)
	
	if !onGround or Engine.is_editor_hint():
		targetRollVolume = -50
		targetGrindVolume = -50
	
	soundVolumes[0] = lerpf(soundVolumes[0], targetRollVolume, min(delta * 30, 1.0))
	soundVolumes[1] = lerpf(soundVolumes[1], targetGrindVolume, min(delta * 30, 1.0))
	
	ball_roll_sound.get_stream_playback().set_stream_pitch_scale(soundInts[0], rollPitch)
	ball_roll_sound.get_stream_playback().set_stream_volume(soundInts[0], soundVolumes[0])
	ball_roll_sound.get_stream_playback().set_stream_pitch_scale(soundInts[1], grindPitch)
	ball_roll_sound.get_stream_playback().set_stream_volume(soundInts[1], soundVolumes[1])

func _process(delta: float) -> void:
	if Engine.is_editor_hint():
		return
	if Input.is_action_just_pressed("Pause"):
		get_tree().change_scene_to_file("res://core/ui/menus/MainMenu.tscn")
	
	manage_rolling_sound(delta)
	
	var ratio := minf((MXGlobal.curFrameAccumulation / MXGlobal.tickDelta), 1)
	position = ballPosPrev.lerp(ballPos, ratio)
	
	var rotAxis := lastGroundNormal.cross(lastGroundSpeed).normalized()
	if !rotAxis.is_normalized():
		rotAxis = Vector3.UP
	$BallMesh.basis = $BallMesh.basis.rotated(rotAxis, lastGroundSpeed.length() * delta * PI * 0.5)
	
	if multiplayer.get_unique_id() != get_multiplayer_authority():
		return
	
	camera.make_current()
	
	ballCamYawLerp = lerpf(ballCamYawPrev, ballCamYaw, ratio)
	ballCamPitchLerp = lerpf(ballCamPitchPrev, ballCamPitch, ratio)
	tiltRollLerp = lerpf(tiltRollPrev, tiltRoll, ratio)
	tiltPitchLerp = lerpf(tiltPitchPrev, tiltPitch, ratio)
	var camYawLerp := lerpf(camYawPrev, camYaw, ratio)
	var camPitchLerp := lerpf(camPitchPrev, camPitch, ratio)
	
	camera_root.basis = camBasis
	
	var finalPitch : float = -ballCamPitchLerp + camPitchLerp
	
	match state:
		ballState.STATE_LEADIN:
			var finalRot := Vector3(finalPitch + deg_to_rad(-15), ballCamYawLerp + deg_to_rad(180), 0)
			var finalPos := Basis(Quaternion.from_euler(finalRot)).z * 3.2 + Vector3(0, 1, 0)
			var leadinRatio : float = leadinCurve.sample((float(MXGlobal.currentStageOverseer.localTick - 1) + ratio) / levelStartTime)
			var stageCenter := to_local(MXGlobal.currentStage.stageVisualBounds.get_center())
			var startDist:float = MXGlobal.currentStage.stageVisualBounds.get_longest_axis_size()
			var startAngle := finalRot.y + deg_to_rad(360)
			var lerpAngle := Vector3(lerpf(deg_to_rad(-45), finalRot.x, leadinRatio), lerpf(startAngle, finalRot.y, leadinRatio), 0)
			var lerpPos := stageCenter.lerp(finalPos, leadinRatio) + Basis(Quaternion.from_euler(lerpAngle)).z * lerpf(startDist, 0.0, leadinRatio)
			camera.position = lerpPos
			camera.rotation = lerpAngle
		ballState.STATE_PLAY:
			var oldPos := camTransform.origin
			camera.rotation = Vector3(finalPitch + deg_to_rad(-15), ballCamYawLerp + camYawLerp + deg_to_rad(180), 0)
			camera.position = camera.basis.z * 3.2 + Vector3(0, 1, 0)
			camVelocity = (camera.global_position - oldPos) / delta
		ballState.STATE_GOAL:
			var dot : float = camVelocity.dot(camera.basis.x)
			var dot2 : float = camVelocity.dot(camera.basis.y)
			var dirToCam := camera.position.normalized()
			#camVelocity = camVelocity + camera.basis.x * delta * dot * dot2
			camVelocity = camVelocity + camera.basis.x * delta * 2 * signf(dot) * maxf(dot2 * 2 + 1, 0)
			camVelocity = camVelocity + -dirToCam * (dirToCam * 3).dot(camera.position - dirToCam * 3)*delta*2
			camVelocity = camVelocity + (-camVelocity * Vector3(1, 2, 1)) * delta * 2
			camera.global_position = camTransform.origin + camVelocity * delta
			var arc := Quaternion(camera.basis.z, camTransform.origin - ballPos).normalized()
			camera.quaternion = (camera.quaternion.normalized()).slerp(arc * camera.quaternion, delta * 12)
			camera.global_rotation = camera.global_rotation * Vector3(1, 1, 0)
		ballState.STATE_FALL:
			camVelocity = camVelocity + -camVelocity * delta * 2
			camera.global_position = camTransform.origin + camVelocity * delta
			var arc := Quaternion(camera.basis.z, camTransform.origin - ballPos).normalized()
			camera.quaternion = (camera.quaternion.normalized()).slerp(arc * camera.quaternion, delta * 6)
			camera.global_rotation.z = camera.global_rotation.z + -camera.global_rotation.z * delta * 4
	
	
	visualStageTilt = Basis.IDENTITY
	
	var rightDir := camBasis.x
	var forwardDir := camBasis.z
	rightDir = rightDir.rotated(camBasis.y, -camYawLerp)
	forwardDir = forwardDir.rotated(rightDir, ballCamPitchLerp)
	
	forwardDir = forwardDir.rotated(camBasis.y, -camYawLerp)
	
	visualStageTilt = visualStageTilt.rotated(forwardDir, tiltRollLerp * -deg_to_rad(MXGlobal.playerSettings["max_visual_tilt"]))
	visualStageTilt = visualStageTilt.rotated(rightDir, tiltPitchLerp * -deg_to_rad(MXGlobal.playerSettings["max_visual_tilt"]))
	
	RenderingServer.global_shader_parameter_set("stage_tilt_basis", visualStageTilt)
	
	RenderingServer.global_shader_parameter_set("ball_pos", camera.global_transform.inverse() * position)
	
	for obj:Variant in MXGlobal.currentStageOverseer.stageObjs:
		if !obj.enableTrigger: continue
		obj.set_collision_layer_value(16, true)
		triggertrace.global_position = ballPos
		triggertrace.target_position = camera.position * 0.75
		triggertrace.force_update_transform()
		triggertrace.force_raycast_update()
		obj.set_collision_layer_value(16, false)
	
	camTransform = camera.global_transform
	
	var lookBasis : Basis = camBasis.rotated(camBasis.y, ballCamYawLerp)
	
	var invStageTilt : Basis = Basis.IDENTITY
	
	invStageTilt = invStageTilt.rotated(lookBasis.z, tiltRollLerp * -deg_to_rad(28))
	invStageTilt = invStageTilt.rotated(lookBasis.x, tiltPitchLerp * -deg_to_rad(28))

func change_state(inState: ballState) -> void:
	if state != inState:
		lastStateChange = MXGlobal.currentStageOverseer.localTick
	state = inState

func get_desired_pos( frac:float ) -> Vector3:
	return ballPos + velocity * MXGlobal.tickDelta * frac

func collide_ball_with_plane(inNormal : Vector3, _inPoint : Vector3, velAtPoint : Vector3, inDelta : float) -> void:
	var relativeVelocity := velocity - velAtPoint
	var normalSpeed := relativeVelocity.dot(inNormal)
	if normalSpeed >= 0:
		return
	
	# make it positive
	normalSpeed = normalSpeed * -1
	
	# calculate parallel velocity and apply friction
	var parallelVelocity : Vector3 = relativeVelocity - normalSpeed * inNormal
	var parallelVelocityReduc := parallelVelocity - parallelVelocity * friction * inDelta
	velocity = velocity - (parallelVelocity - parallelVelocityReduc)
	
	# constant normal speed loss on bounce
	# equal to ball speed after falling for 5 ticks
	var constBounceLoss : float = acceleration * inDelta * 5
	
	if normalSpeed <= constBounceLoss:
		# if normal speed is below constant loss, just set normal speed to 0 and leave
		velocity = velocity + inNormal * normalSpeed
	else:
		var adjustedBallSpeed : float = normalSpeed - constBounceLoss
		
		# remove constant loss from normal speed
		velocity = velocity + inNormal * constBounceLoss
		
		# apply restitution to new speed
		velocity = velocity + inNormal * adjustedBallSpeed * (restitution + 1)

func test_collision_with_other_marbles() -> void:
	for pl in MXGlobal.currentStageOverseer.players:
		var marble : MXRacer = pl.controlledPawn
		if marble == self:
			continue
		var ourWeight : float = 1.0
		var theirWeight : float = 1.0
		if weight:
			ourWeight = weight
		if marble.weight:
			theirWeight = marble.weight
		var dirTo : Vector3 = ballPos - marble.ballPos
		var dirToLen : float = dirTo.length()
		var dirToNormal : Vector3 = dirTo.normalized()
		if dirToLen < radius + marble.radius:
			var relativeVelocity := velocity - marble.velocity
			var normalSpeed : float = relativeVelocity.dot(dirToNormal)
			# weight ratio of the smaller vs the bigger
			# i.e. if one of our weights is 2 and the others is 3
			# this should always return 0.666...
			var ratio1 : float = minf(theirWeight, ourWeight) / maxf(theirWeight, ourWeight)
			ratio1 *= 0.5
			# inverse. in the above example,
			# this should return 0.333...
			var ratio2 : float = 1 - ratio1
			# if we're heavier than the ball we're hitting
			# we should use ratio2 to modify the other ball's velocity
			# otherwise, use ratio2 on ourselves and ratio1 on the other
			if ourWeight > theirWeight:
				marble.velocity += dirToNormal * normalSpeed * ratio2
				velocity += dirToNormal * normalSpeed * -ratio1
			else:
				marble.velocity += dirToNormal * normalSpeed * ratio1
				velocity += dirToNormal * normalSpeed * -ratio2
			ballPos = marble.ballPos + dirToNormal * (marble.radius + radius)

func test_ball_collision() -> void:
	
	if multiplayer.get_peers().size() > 0:
		test_ball_collision_simple()
		#print("Using simple collision instead!")
		return
	
	if collider.shape is SphereShape3D:
		collider.shape.radius = radius - 0.002
	
	test_collision_with_other_marbles()
	
	collider.margin = 0.0005
	var maxCollisionIterations : int = 4
	var fractionRemaining : float = 1
	var startPos := ballPos
	var startVel := velocity
	var collisionData1 := PackedVector3Array()
	var collisionData2 := PackedVector3Array()
	var colObj : RORStageObject
	var collidedOnce := false
	var totalFriction := 0.0
	for i in maxCollisionIterations:
		if (i > 0 and ballPos.is_equal_approx(startPos)) or i == maxCollisionIterations - 1:
			#print("Reverting to simple collision...")
			ballPos = startPos
			velocity = startVel
			test_ball_collision_simple()
			return
		#print("iteration on tick " + str(MXGlobal.currentStageOverseer.localTick))
		force_update_transform()
		var desiredPos := get_desired_pos(fractionRemaining)
		var collidedThisIteration : bool = false
		var lowestFrac : float = 1
		var startSolid := false
		#var tempPos := ballPos
		for obj:Variant in MXGlobal.currentStageOverseer.stageObjs:
			if !obj.enableCollision or obj.enableTrigger: continue
			if obj.is_in_group("NoBallCollision"): continue
			obj.set_collision_layer_value(16, true)
			
			var objFractionalTransform:Transform3D = obj.currentTransform.interpolate_with(obj.oldTransform, fractionRemaining)
			
			# check if we're inside something already
			# todo: try moving this to the end of the function
			# so we can avoid having to worry about starting solid in the main phase
			collider.global_position = obj.currentTransform * (objFractionalTransform.inverse() * ballPos)
			collider.target_position = Vector3.ZERO
			collider.force_update_transform()
			collider.force_shapecast_update()
			
			var collision_count := collider.get_collision_count()
			if collision_count > 0:
				for n in collision_count:
					var depth := -((collider.global_position - collider.get_collision_point(n)).length() - radius)
					#depth = 0.001 * (i + 1)
					if depth < 0:
						continue
					var rotatedNormal := (Quaternion(objFractionalTransform.basis) * Quaternion(obj.currentTransformInverse.basis)) * collider.get_collision_normal(n)
					ballPos = ballPos + rotatedNormal * (depth)
				collidedThisIteration = true
				startSolid = true
				lowestFrac = 0
				collisionData1.resize(collision_count)
				collisionData2.resize(collision_count)
				colObj = collider.get_collider(0)
					
				for n in collision_count:
					collisionData1[n] = (Quaternion(objFractionalTransform.basis) * Quaternion(obj.currentTransformInverse.basis)) * collider.get_collision_normal(n)
					collisionData2[n] = collider.get_collision_point(n)
			
			collider.global_position = obj.currentTransform * (objFractionalTransform.inverse() * ballPos)
			collider.target_position = collider.to_local(desiredPos)
			collider.force_update_transform()
			collider.force_shapecast_update()
			
			collision_count = collider.get_collision_count()
			if collision_count > 0:
				collidedThisIteration = true
				#tempPos = collider.global_position
				if collider.get_closest_collision_safe_fraction() < lowestFrac:
					lowestFrac = collider.get_closest_collision_safe_fraction()
					if lowestFrac == 0:
						startSolid = true
					collisionData1.resize(collision_count)
					collisionData2.resize(collision_count)
					for n in collision_count:
						collisionData1[n] = (Quaternion(objFractionalTransform.basis) * Quaternion(obj.currentTransformInverse.basis)) * collider.get_collision_normal(n)
						collisionData2[n] = collider.get_collision_point(n)
					colObj = collider.get_collider(0) # only testing one collider at a time, all colliders should be the same
			
			obj.set_collision_layer_value(16, false)
		
		if !startSolid:
			# we were able to move at least a little bit, no need to depenetrate
			ballPos = ballPos.lerp(desiredPos, lowestFrac)
			
		var oldFrac := fractionRemaining
		fractionRemaining = fractionRemaining * (1 - lowestFrac)
		var fracDiff := oldFrac - fractionRemaining
		if collidedThisIteration:
			collidedOnce = true
			for n in collisionData1.size():
				var frictionFrag:float = (fracDiff / collisionData1.size())
				totalFriction = totalFriction + frictionFrag
				colObj.on_collide(self, collisionData1[n], collisionData2[n], colObj.get_velocity_at_point(collisionData2[n]), MXGlobal.tickDelta, frictionFrag, false)
		
		if fractionRemaining <= 0.0001:
			break
	
	if collidedOnce:
		onGround = true
		for n in collisionData1.size():
			var frictionFrag:float = ((1 - totalFriction) / collisionData1.size())
			totalFriction = totalFriction + frictionFrag
			colObj.on_collide(self, collisionData1[n], collisionData2[n], colObj.get_velocity_at_point(collisionData2[n]), MXGlobal.tickDelta, frictionFrag, true)
	else:
		onGround = false
	
	collider.global_position = ballPos
	collider.target_position = Vector3.ZERO
	collider.force_update_transform()
	collider.force_shapecast_update()
	if collider.get_collision_count() > 0:
		for n in collider.get_collision_count():
			var depth := -((collider.global_position - collider.get_collision_point(n)).length() - radius)
			depth = 0.001
			if depth < 0:
				continue
			ballPos = ballPos + collider.get_collision_normal(n) * (depth)
	
	for obj:Variant in MXGlobal.currentStageOverseer.stageObjs:
		if !obj.enableTrigger: continue
		if obj is RORStageObject:
			if obj.stageCol.shape is ConcavePolygonShape3D:
				obj.stageCol.shape.backface_collision = true
				#obj.stageCol.fo
			obj.set_collision_layer_value(16, true)
			triggertrace.hit_from_inside = true
			triggertrace.global_position = obj.currentTransform * (obj.oldTransformInverse * startPos)
			triggertrace.target_position = triggertrace.to_local(ballPos)
			triggertrace.force_update_transform()
			triggertrace.force_raycast_update()
			if triggertrace.is_colliding():
				var backface := true
				if obj.stageCol.shape is ConcavePolygonShape3D:
					obj.stageCol.shape.backface_collision = false
					triggertrace.force_raycast_update()
					if triggertrace.is_colliding():
						backface = false
				var fraction := triggertrace.to_local(triggertrace.get_collision_point()).length() / triggertrace.target_position.length()
				obj.on_trigger(self, fraction, backface)
			obj.set_collision_layer_value(16, false)

func test_ball_collision_simple() -> void:
	
	collider.shape.radius = 0.5
	collider.margin = 0.0
	var startPos : Vector3 = ballPos
	var collidedOnce : bool = false
	var firstCollide : bool = true
	
	test_collision_with_other_marbles()
	
	if get_multiplayer_authority() == multiplayer.get_unique_id() or MXGlobal.currentStageOverseer.currentlyServer:
		#var lineTraceHit := false
		for obj:Variant in MXGlobal.currentStageOverseer.stageObjs:
			if !obj.enableCollision: continue
			obj.set_collision_layer_value(16, true)
			
			triggertrace.global_position = obj.currentTransform * (obj.oldTransformInverse * ballPos)
			triggertrace.target_position = triggertrace.to_local(get_desired_pos(1))
			triggertrace.force_update_transform()
			triggertrace.force_raycast_update()
			
			if triggertrace.is_colliding():
				var cobj := triggertrace.get_collider()
				#lineTraceHit = true
				ballPos = triggertrace.get_collision_point() + triggertrace.get_collision_normal() * radius
				cobj.on_collide(self, triggertrace.get_collision_normal(), triggertrace.get_collision_point(), cobj.get_velocity_at_point(triggertrace.get_collision_point()), MXGlobal.tickDelta, 1, true)
			
			obj.set_collision_layer_value(16, false)
	
	if !triggertrace.is_colliding():
		ballPos = get_desired_pos(1)
		collider.target_position = Vector3.ZERO
		collider.set_collision_mask_value(1, true)
		for i in 2:
			collider.global_position = ballPos
			collider.force_update_transform()
			collider.force_shapecast_update()
			if collider.get_collision_count() > 0:
				collidedOnce = true
				for n in collider.get_collision_count():
					var cobj := collider.get_collider(n)
					var cpoint : Vector3 = collider.get_collision_point(n)
					var depth : float = -((ballPos - cpoint).length() - radius)
					#depth = 0.00001 * (i + 1)
					if depth < 0:
						continue
					ballPos = ballPos + collider.get_collision_normal(n) * (depth)
					cobj.on_collide(self, collider.get_collision_normal(n), cpoint, cobj.get_velocity_at_point(cpoint), MXGlobal.tickDelta, 1, firstCollide)
					firstCollide = false
			else:
				break
		collider.set_collision_mask_value(1, false)
	
	onGround = collidedOnce
	
	for obj:Variant in MXGlobal.currentStageOverseer.stageObjs:
		if !obj.enableTrigger: continue
		if obj is RORStageObject:
			if obj.stageCol.shape is ConcavePolygonShape3D:
				obj.stageCol.shape.backface_collision = true
				#obj.stageCol.fo
			obj.set_collision_layer_value(16, true)
			triggertrace.hit_from_inside = true
			triggertrace.global_position = obj.currentTransform * (obj.oldTransformInverse * startPos)
			triggertrace.target_position = triggertrace.to_local(ballPos)
			triggertrace.force_update_transform()
			triggertrace.force_raycast_update()
			if triggertrace.is_colliding():
				var backface := true
				if obj.stageCol.shape is ConcavePolygonShape3D:
					obj.stageCol.shape.backface_collision = false
					triggertrace.force_raycast_update()
					if triggertrace.is_colliding():
						backface = false
				var fraction := triggertrace.to_local(triggertrace.get_collision_point()).length() / triggertrace.target_position.length()
				obj.on_trigger(self, fraction, backface)
			obj.set_collision_layer_value(16, false)

func calculate_gameplay_camera() -> void:
	#get the basis for the current camera direction
	var lookBasis : Basis = Basis(Quaternion.from_euler(Vector3(0, ballCamYaw, 0)))
	
	var velocityMod : Vector3 = camBasis.inverse() * velocity
	
	#get just the forward vector from the basis
	var lDir := lookBasis.z
	
	#get the velocity direction
	var desiredDir : Vector3 = (velocityMod * Vector3(1, 0, 1)).normalized()
	
	#cheater cross product to get the signed angle
	var turn : float = lDir.signed_angle_to(desiredDir, Vector3.UP)
	
	var velLen : float = velocityMod.length()
	
	var velLenNoY : float = (velocityMod * Vector3(1, 0, 1)).length()
	
	#maximum angle difference to check between the current camera angle and desired camera angle
	var turnClamp := 75 / clampf((velLen * 0.35) + 1, 1, 5)
	
	turn = clampf( turn, deg_to_rad(-turnClamp), deg_to_rad(turnClamp))
	
	#scale angle difference to the range (-1, 1)
	turn = turn / deg_to_rad(turnClamp)
	
	#calculate a lerp modifier based on ball velocity (with no vertical component)
	#quickly scale up to 0.25x turn rate with just a tiny bit of motion
	#so you can turn the camera without moving very much
	#and then scale up to 1x turn rate a little more slowly
	var speedMod := clampf(remap(velLenNoY, 0, 0.025, 0, 0.2), 0, 0.2)
	var speedMod2 := clampf(remap(velLenNoY, 0.025, 3.5, 0.2, 1), 0.2, 1)
	var useMod := speedMod
	if velLenNoY >= 0.025:
		useMod = speedMod2
	
	#camera turn rate doesn't change instantly!
	#to give it a bit more weight, the turning has a velocity that changes over time
	#camTurnVelocity = lerp(camTurnVelocity, turn, MXGlobal.tickDelta * (velLenNoY * 4 + 2))
	
	#but if the camera SLOWS DOWN (or changes direction!), turn the camera right away
	#camTurnVelocity = min(abs(camTurnVelocity), abs(turn)) * sign(turn)
	
	#apply the change
	ballCamYaw = lerp(ballCamYaw, ballCamYaw + turn, MXGlobal.tickDelta * useMod * 2.6)
	
	#pitch is much simpler
	var velN := (velocityMod + lookBasis.z * 3).normalized()
	var desiredPitch := rad_to_deg(asin(velN.y))
	
	if camMode == 0:
		ballCamPitch = lerp(ballCamPitch, deg_to_rad(clamp(-desiredPitch, -40, 60)), MXGlobal.tickDelta * min(velocityMod.length() + 1, 10) * 0.5)
	else:
		ballCamPitch = lerp(ballCamPitch, 0.0, MXGlobal.tickDelta * 8)

func apply_controls_and_accelerate() -> void:
	
	var fmove : float = get_parent().get_axis("my") * 1.414
	var smove : float = get_parent().get_axis("mx") * 1.414
	
	fmove = clamp(fmove, -1, 1)
	smove = clamp(smove, -1, 1)
	
	fmove = pow(abs(fmove), 1.4) * sign(fmove)
	smove = pow(abs(smove), 1.4) * sign(smove)
	
	inputX = smove
	inputY = fmove
	
	tiltPitch = lerp(tiltPitch, fmove, MXGlobal.tickDelta * 12)
	tiltRoll = lerp(tiltRoll, smove, MXGlobal.tickDelta * 12)
	
	var gravDir : Vector3 = -gravityBasis.y
	
	var lookBasis : Basis = camBasis.rotated(camBasis.y, ballCamYaw)
	
	gravDir = gravDir.rotated(lookBasis.z, tiltRoll * deg_to_rad(maxTilt))
	gravDir = gravDir.rotated(lookBasis.x, tiltPitch * deg_to_rad(maxTilt))
	
	velocity = velocity + gravDir * MXGlobal.tickDelta * acceleration
	
	var fsecond : float = get_parent().get_axis("cy") * 1.414
	var ssecond : float = get_parent().get_axis("cx") * 1.414
	
	fsecond = clamp(fsecond, -1, 1)
	ssecond = clamp(ssecond, -1, 1)
	
	fsecond = pow(abs(fsecond), 1.4) * sign(fsecond)
	ssecond = pow(abs(ssecond), 1.4) * sign(ssecond)
	
	camInputX = ssecond
	camInputY = fsecond

func is_own_ball() -> bool:
	return self == MXGlobal.localPlayer.controlledPawn

func tick() -> void:
	ballPosPrev = ballPos
	camYawPrev = camYaw
	camPitchPrev = camPitch
	ballCamYawPrev = ballCamYaw
	ballCamPitchPrev = ballCamPitch
	tiltPitchPrev = tiltPitch
	tiltRollPrev = tiltRoll
	
	match state:
		ballState.STATE_LEADIN:
			if MXGlobal.currentStageOverseer.localTick == int(levelStartTime - Engine.physics_ticks_per_second / 4.0):
				var spawnEffect := preload("res://content/base/effects/spawn/ballspawn_default.tscn").instantiate()
				add_child(spawnEffect)
			if MXGlobal.currentStageOverseer.localTick > levelStartTime:
				calculate_gameplay_camera()
				change_state(ballState.STATE_PLAY)
		
		ballState.STATE_PLAY:
			$BallMesh.visible = true
			apply_controls_and_accelerate()
			test_ball_collision()
			calculate_gameplay_camera()
			camBasis = camBasis.slerp(gravityBasis, MXGlobal.tickDelta * 5)
			if !MXGlobal.currentStage.stageFalloutBounds.has_point(ballPos):
				change_state(ballState.STATE_FALL)
		
		ballState.STATE_GOAL:
			tiltPitch = lerp(tiltPitch, 0.0, MXGlobal.tickDelta * 12)
			tiltRoll = lerp(tiltRoll, 0.0, MXGlobal.tickDelta * 12)
			if MXGlobal.currentStageOverseer.localTick < lastStateChange + MXGlobal.time_to_ticks(2):
				velocity = velocity + -velocity * MXGlobal.tickDelta * 4
			else:
				velocity = velocity + Vector3(0, 50, 0) * MXGlobal.tickDelta
			test_ball_collision()
			camBasis = camBasis.slerp(Basis.IDENTITY, MXGlobal.tickDelta * 5)
			if is_own_ball() and MXGlobal.currentStageOverseer.localTick == lastStateChange + MXGlobal.time_to_ticks(0.75):
				MXGlobal.play_announcer_line("goal")
		
		ballState.STATE_FALL:
			tiltPitch = lerp(tiltPitch, 0.0, MXGlobal.tickDelta * 12)
			tiltRoll = lerp(tiltRoll, 0.0, MXGlobal.tickDelta * 12)
			var gravDir : Vector3 = -gravityBasis.y
			velocity = velocity + gravDir * MXGlobal.tickDelta * acceleration
			test_ball_collision()
			camBasis = camBasis.slerp(Basis.IDENTITY, MXGlobal.tickDelta * 5)
			if is_own_ball() and MXGlobal.currentStageOverseer.localTick == lastStateChange + MXGlobal.time_to_ticks(0.5):
				if velocity.length() <= 100:
					MXGlobal.play_announcer_line("oob")
				else:
					MXGlobal.play_announcer_line("oobfast")
	
	gravityBasis = gravityBasis.orthonormalized()
	camBasis = camBasis.orthonormalized()

func set_nametag(inName : String) -> void:
	%Nametag.mesh.text = inName
	var bgMesh : PlaneMesh = %NametagBackground.mesh
	bgMesh.size.x = inName.length() * 0.12
	

func save_state() -> PackedByteArray:
	
	var tempBuffer : StreamPeerBufferExtension = StreamPeerBufferExtension.new()
	tempBuffer.resize(1024)
	
	var byteData : int = 0
	
	if onGround:
		byteData |= 1
	
	tempBuffer.put_u8(byteData)
	tempBuffer.put_u8(camMode)
	
	tempBuffer.put_float(friction)
	tempBuffer.put_float(maxTilt)
	tempBuffer.put_float(acceleration)
	tempBuffer.put_float(restitution)
	tempBuffer.put_float(radius)
	tempBuffer.put_float(camYaw)
	tempBuffer.put_float(camPitch)
	tempBuffer.put_float(ballCamYaw)
	tempBuffer.put_float(ballCamPitch)
	tempBuffer.put_float(tiltPitch)
	tempBuffer.put_float(tiltRoll)
	tempBuffer.put_float(inputX)
	tempBuffer.put_float(inputY)
	tempBuffer.put_float(camInputX)
	tempBuffer.put_float(camInputY)
	tempBuffer.put_float(camTurnVelocity)
	tempBuffer.put_float(ballCamYawPrev)
	tempBuffer.put_float(ballCamPitchPrev)
	tempBuffer.put_float(tiltPitchPrev)
	tempBuffer.put_float(tiltRollPrev)
	tempBuffer.put_float(goalTime)
	tempBuffer.put_float(jumpStart)
	
	tempBuffer.put_u32(lastStateChange)
	tempBuffer.put_u32(levelStartTime)
	tempBuffer.put_u8(state)
	
	tempBuffer.put_vector3(ballPos)
	tempBuffer.put_vector3(ballPosPrev)
	tempBuffer.put_vector3(velocity)
	tempBuffer.put_vector3(lastGroundNormal)
	tempBuffer.put_vector3(lastGroundSpeed)
	tempBuffer.put_basis(camBasis)
	tempBuffer.put_basis(gravityBasis)
	
	tempBuffer = _save_state_extension(tempBuffer)
	
	tempBuffer.resize(tempBuffer.get_position())
	return tempBuffer.data_array

func load_state(inData : PackedByteArray) -> void:
	var buffer := StreamPeerBufferExtension.new()
	buffer.put_data(inData)
	buffer.seek(0)
	
	var byteData := buffer.get_u8()
	
	onGround = byteData & 1 > 0
	
	camMode = buffer.get_u8()
	
	friction = buffer.get_float()
	maxTilt = buffer.get_float()
	acceleration = buffer.get_float()
	restitution = buffer.get_float()
	radius = buffer.get_float()
	camYaw = buffer.get_float()
	camPitch = buffer.get_float()
	ballCamYaw = buffer.get_float()
	ballCamPitch = buffer.get_float()
	tiltPitch = buffer.get_float()
	tiltRoll = buffer.get_float()
	inputX = buffer.get_float()
	inputY = buffer.get_float()
	camInputX = buffer.get_float()
	camInputY = buffer.get_float()
	camTurnVelocity = buffer.get_float()
	ballCamYawPrev = buffer.get_float()
	ballCamPitchPrev = buffer.get_float()
	tiltPitchPrev = buffer.get_float()
	tiltRollPrev = buffer.get_float()
	goalTime = buffer.get_float()
	jumpStart = buffer.get_float()
	
	lastStateChange = buffer.get_u32()
	levelStartTime = buffer.get_u32()
	state = buffer.get_u8() as ballState
	
	ballPos = buffer.get_vector3()
	ballPosPrev = buffer.get_vector3()
	velocity = buffer.get_vector3()
	lastGroundNormal = buffer.get_vector3()
	lastGroundSpeed = buffer.get_vector3()
	camBasis = buffer.get_basis()
	gravityBasis = buffer.get_basis()
	
	_load_state_extension(buffer)
	
	position = ballPos
	camera.global_transform = camTransform

func _save_state_extension(inBuffer : StreamPeerBufferExtension) -> StreamPeerBufferExtension:
	return inBuffer

func _load_state_extension(_inBuffer : StreamPeerBufferExtension) -> void:
	pass
