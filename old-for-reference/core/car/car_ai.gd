extends MXRacer

var bumper_lane : float = 0.3

func do_car_driving( inputs:PlayerInput = PlayerInput.Neutral ) -> void:
	var in_steer_x := 0
	var in_steer_y := 0
	var in_strafe_left := 0
	var in_strafe_right := 0
	var in_accel := true
	var in_boost := false
	var in_spinattack := false
	
	# useful stuff for driving math
	var desiredTransform := current_transform
	var use_z := current_transform.basis.z.slide(gravity_basis.y).normalized()
	var use_y := gravity_basis.y
	var use_x := velocity.cross(use_y).normalized()
	if !use_x.is_normalized():
		use_x = current_transform.basis.x
	var side_dir : Vector3 = current_transform.basis.x.slide(gravity_basis.y).normalized()
	
	if floor_type == RORStageObject.object_types.DIRT:
		velocity += -velocity * MXGlobal.tick_delta * 0.5
	
	grav_point_cast.position = previous_transform.origin
	grav_point_cast.target_position = grav_point_cast.to_local(current_transform.origin)
	grav_point_cast.set_collision_mask_value(1, false)
	grav_point_cast.set_collision_mask_value(2, false)
	grav_point_cast.set_collision_mask_value(3, true)
	grav_point_cast.force_raycast_update()
	if grav_point_cast.is_colliding() and !on_booster:
		var active_booster : BoostPad = grav_point_cast.get_collider().get_parent()
		boostpad_time = Engine.physics_ticks_per_second
		current_turbo += car_definition.turbo_add * 0.8 * active_booster.current_boost_intensity
		boost_recharge += (active_booster.current_boost_intensity - 1.0) * 15
		active_booster.execute_boost()
		var car_audio_playback := car_audio.get_stream_playback() as AudioStreamPlaybackPolyphonic
		if !MXGlobal.currentlyRollback:
			var new_boost_effect = preload("res://core/car/boost_effect.tscn").instantiate()
			new_boost_effect.rotation_degrees = Vector3(0, car_definition.model_rotation + 180, 0)
			new_boost_effect.life_time = 3000
			car_mesh.add_child(new_boost_effect)
			new_boost_effect.global_position = car_visual.global_position
			new_boost_effect.global_basis = new_boost_effect.global_basis.orthonormalized()
			car_audio_playback.play_stream(preload("res://content/base/common/boost_pad.wav"), 0, -5.0, 1.0)
		if boost_state == BoostState.NONE:
			boost_state = BoostState.PAD
		else: if boost_state == BoostState.MANUAL:
			boost_state = BoostState.BOTH
		on_booster = true
	else:
		on_booster = false
	grav_point_cast.set_collision_mask_value(1, true)
	grav_point_cast.set_collision_mask_value(3, false)
			
	if boost_state == BoostState.PAD:
		current_boost_strength = 1.0 + (car_definition.boost_power / (car_definition.weight / 1000.0)) + current_turbo * 0.1
		if boostpad_time == 0:
			boost_state = BoostState.NONE
	elif boost_state == BoostState.NONE:
		current_boost_strength = 1.0 + current_turbo * 0.1
	
	boost_time = max(boost_time - 1, 0)
	boostpad_time = max(boostpad_time - 1, 0)
	current_turbo = maxf(0.0, current_turbo - car_definition.turbo_depletion * MXGlobal.tick_delta)
	
	var accel_proportion : float = in_accel
	car_mesh.rotation = Vector3(0, deg_to_rad(car_definition.model_rotation), 0)
	
	var desiredPoint = currentPoint + 1
	if desiredPoint >= MXGlobal.currentStage.sampled_track_path.size():
		desiredPoint -= MXGlobal.currentStage.sampled_track_path.size()
	var segment_ratio := fmod(lap_progress * MXGlobal.currentStage.sampled_track_path.size(), 1.0)
	#print(lap_progress * MXGlobal.currentStage.sampled_track_path.size())
	#print(segment_ratio)
	var f_p1 = lerp(MXGlobal.currentStage.sampled_track_path[currentPoint], MXGlobal.currentStage.sampled_track_path[desiredPoint], segment_ratio)
	var dir_forward = (f_p2 - f_p1).normalized()
	
	var oldVelVert := velocity.dot( gravity_basis.y )
	var velNoVert := ( velocity - gravity_basis.y * oldVelVert )
	var angle_offset := velNoVert.signed_angle_to(current_transform.basis.z, gravity_basis.y)
	#if car_definition.cant_drive_backward:
	#	angle_offset = velNoVert.signed_angle_to(dir_forward, gravity_basis.y) * 2.0
	
	var steer_power_mod : float = (1 - absf(angle_offset / (PI * 0.5)))
	
	var steer_power_mod_magic : float = ((log(car_definition.grip + 10) / log(10)) - 1.0) * lerpf(car_definition.drift_stiffness, car_definition.drift_stiffness * 0.05, 1.0 - pow(speed_factor, 2))
	if steer_state == SteerState.QUICK:
		steer_power_mod_magic *= car_definition.quickturn_stiffness
	steer_power_mod = pow(absf(steer_power_mod), steer_power_mod_magic) * signf(steer_power_mod)
	var half_steer_power_mod : float = lerpf(steer_power_mod, 1.0, 0.5)
	if absf(angle_offset) > PI * 0.5:
		steer_power_mod = 0.05
	
	var current_steer_power := car_definition.steer_power
	#if !(signf(in_steer_x) == sign(angle_offset)):
		#current_steer_power = absf(current_steer_power * lerpf(steer_power_mod, half_steer_power_mod, speed_factor))
	
	var redir_mult_proportional : float = (car_definition.grip / (4000 / car_definition.weight)) * (1 - steer_power_mod)
	
	var redir_mult_constant : float = current_steer_power * 0.2 * steer_power_mod
	
	if steer_state == SteerState.NORMAL:
		redir_mult_constant = current_steer_power * car_definition.redirect_power / maxf(0.001, absf(angle_offset))
		redir_mult_constant *= remap(speed_factor, 1.0, 0.0, 1.0, 0.5)
		
	if steer_state == SteerState.QUICK:
		cam_turn_rate = 20.0
		drift_dot = lerpf(drift_dot, 0, MXGlobal.tick_delta * 40)
		var old_redir_mult := redir_mult_constant
		redir_mult_proportional *= car_definition.quickturn_snap
		redir_mult_constant = current_steer_power * car_definition.redirect_power_quickturn / maxf(0.001, absf(angle_offset))
		redir_mult_constant = lerpf(old_redir_mult, redir_mult_constant, absf(current_strafe))
		#current_steer_power = lerpf(current_steer_power, current_steer_power * car_definition.steer_power_quickturn_mult, absf(current_strafe))
		
	if steer_state == SteerState.DRIFT:
		#redir_mult_proportional *=
		redir_mult_proportional *= car_definition.drift_snap
		redir_mult_constant = (((current_steer_power * car_definition.redirect_power_drift) / maxf(0.03, absf(angle_offset)))) * 0.5
		redir_mult_constant *= remap(speed_factor, 1.0, 0.0, 1.0, 0.5)
		#current_steer_power = lerpf(current_steer_power * car_definition.steer_power_drift_mult, current_steer_power, minf(current_strafe * strafe_toward_dir, 0.0))
	
	if floor_type == RORStageObject.object_types.ICE:
		redir_mult_constant *= 0.1
	
	var total_redir_mult : float = minf(60, redir_mult_constant + (redir_mult_proportional / maxf(knockback_velocity.length(), 1.0)))
	#var current_speed : float = velocity.dot(use_z)
	# NOTE OPTIMIZATION remove method call calculate_accel
	
	var steer_mult := 2.5
	if !grounded:
		steer_mult *= car_definition.steer_power_airborne_mult
	
	# NOTE OPTIMIZATION remove method calls calculate_top_speed
	steer_mult *= 1.0 - ((velocity.length() / (MXGlobal.kmh_to_ups * calced_top_speed)) / (car_definition.grip * 0.5))
	#visual_drift_loss = 0

	#angle_velocity += current_transform.basis.z * current_steer_power * in_steer_x * MXGlobal.tick_delta * steer_mult * -1.25
	var rot_amount := current_steer_power * current_steering * MXGlobal.tick_delta * steer_mult
	desiredTransform.basis = desiredTransform.basis.rotated(use_y, rot_amount * 0.1)
	#angle_velocity += current_transform.basis.y * maxf(current_steer_power,10.0) * in_steer_x * MXGlobal.tick_delta * steer_mult
	#angle_velocity += current_transform.basis.z * in_steer_x * MXGlobal.tick_delta * steer_mult * -1.25 * 20
	#angle_velocity += current_transform.basis.y * in_steer_x * MXGlobal.tick_delta * steer_mult * 20
	
	if !grounded:
		total_friction = 0
		angle_velocity += -angle_velocity * MXGlobal.tick_delta * 50.0
	else:
		angle_velocity += -angle_velocity * MXGlobal.tick_delta * car_definition.grip * 0.8
	velocity += -velocity.normalized() * total_friction
	
	var accel_add : float = calculate_accel_with_speed(apparent_velocity.length()) * accel_proportion * MXGlobal.tick_delta
	#print(accel_add)
	
	if grounded:
		air_tilt_velocity = 0
		air_tilt = 0
		air_roll_velocity = 0
		air_roll = 0
		if velocity.length() > 1:
			var to_facing : Quaternion = Quaternion(desiredTransform.basis.z, velocity.normalized())
			var recenter_factor : float = car_definition.recenter_factor
			recenter_factor *= speed_factor
			if car_definition.autopilot:
				recenter_factor = 2.0
			angle_velocity += to_facing.get_axis() * to_facing.get_angle() * recenter_factor * MXGlobal.tick_delta * 120
	else:
		if !gravity_velocity.is_zero_approx():
			velocity = velocity.rotated(gravity_velocity.normalized(), gravity_velocity.length() * MXGlobal.tick_delta * 0.6)
		velocity = velocity.rotated(use_x, air_tilt_velocity * MXGlobal.tick_delta * -0.15)
		velocity += -velocity.normalized() * gravity_velocity.length() * MXGlobal.tick_delta * 12
		velocity += use_z * car_definition.acceleration * MXGlobal.tick_delta * 0.1
		var fall_vel = minf(sqrt(absf(velocity.dot(gravity_basis.y))) * signf(velocity.dot(gravity_basis.y)), 0.0)
		velocity += -gravity_basis.y * (180 + fall_vel * 10) * MXGlobal.tick_delta
		var glide_factor := 1.0 - maxf(0, air_tilt)
		velocity += gravity_basis.y * glide_factor * MXGlobal.tick_delta * 40
		air_tilt_velocity += -air_tilt_velocity * MXGlobal.tick_delta * 20
		if (air_tilt > deg_to_rad(59) and signf(in_steer_y) == -1) or (air_tilt < deg_to_rad(-59) and signf(in_steer_y) == 1) or (air_tilt > deg_to_rad(-59) and air_tilt < deg_to_rad(59)):
			air_tilt_velocity += in_steer_y * MXGlobal.tick_delta * 70
		else:
			air_tilt_velocity = 0
		air_tilt = clampf(air_tilt + air_tilt_velocity * MXGlobal.tick_delta, deg_to_rad(-60), deg_to_rad(60))
		if air_tilt < 0:
			velocity += -velocity * absf(air_tilt) * MXGlobal.tick_delta * 3
		air_roll_velocity += -in_steer_x * MXGlobal.tick_delta * 70
		air_roll_velocity += -air_roll_velocity * MXGlobal.tick_delta * 20
		air_roll = clampf(air_roll + air_roll_velocity * MXGlobal.tick_delta, deg_to_rad(-15), deg_to_rad(15))
	
	knockback_velocity += -knockback_velocity * MXGlobal.tick_delta * 12
	desiredTransform.origin += velocity * MXGlobal.tick_delta
	use_y = use_y.rotated( use_x, -air_tilt )
	use_y = use_y.rotated( current_transform.basis.z, air_roll)
	var arc_basis_visual := Quaternion( desiredTransform.basis.y, use_y )
	if grounded:
		angle_velocity += arc_basis_visual.get_axis() * arc_basis_visual.get_angle() * MXGlobal.tick_delta * 180.0
	else:
		angle_velocity += arc_basis_visual.get_axis() * arc_basis_visual.get_angle() * MXGlobal.tick_delta * 480.0
	var arrow_pos = current_transform.origin + velocity * MXGlobal.tick_delta

	if !angle_velocity.is_zero_approx():
		desiredTransform.basis = desiredTransform.basis.rotated(angle_velocity.normalized(), angle_velocity.length() * MXGlobal.tick_delta).orthonormalized()
	
	if air_time > Engine.physics_ticks_per_second * 0.02 and calced_current_checkpoint.reset_gravity and !no_gravity_reset:
		var to_gravity : Quaternion = Quaternion(gravity_basis.y, Vector3.UP)
		var arc_basis_gravity := Basis( to_gravity )
		gravity_basis = gravity_basis.slerp(arc_basis_gravity * gravity_basis, MXGlobal.tick_delta * 12)
		gravity_velocity = to_gravity.get_axis() * to_gravity.get_angle() * 10
		#var stable_dir := velocity.cross(gravity_basis.y).normalized()
		#var stable_dot = to_gravity.get_axis().dot(stable_dir)
		#gravity_velocity += to_gravity.get_axis() * to_gravity.get_angle() * 50 * (absf(stable_dot) * 3.0 + 1.0)
	
	if grounded:
		oldVelVert = velocity.dot( gravity_basis.y )
		velNoVert = ( velocity - gravity_basis.y * oldVelVert )
		var old_vel : Vector3 = velNoVert
		velNoVert = velNoVert.normalized().slerp(use_z, MXGlobal.tick_delta * total_redir_mult) * velNoVert.length()
		var steering_amount : float = old_vel.length() - sqrt(absf(old_vel.dot(velNoVert)))
		var turn_accel = velNoVert.normalized() * car_definition.turn_accel * steering_amount * car_definition.acceleration * MXGlobal.tick_delta
		velNoVert += turn_accel
		if car_definition.autopilot:
			velNoVert = velNoVert.slerp(dir_forward.slide(gravity_basis.y).normalized(), MXGlobal.tick_delta * 30).normalized() * velNoVert.length()
		velocity = velNoVert + gravity_basis.y * oldVelVert
		velocity += use_z * accel_add
	else:
		oldVelVert = velocity.dot( gravity_basis.y )
		velNoVert = ( velocity - gravity_basis.y * oldVelVert )
		velNoVert = velNoVert.normalized().slerp(use_z, MXGlobal.tick_delta * 12) * velNoVert.length()
		velocity = velNoVert + gravity_basis.y * oldVelVert
	
	
	var strafe_sign := signf((side_dir * current_strafe).dot(velocity))
	var drift_sign := signf(current_transform.basis.x.dot(velocity))
	var steering_sign := signf(in_steer_x)
	var strafe_toward_dir := current_strafe * strafe_sign
	var strafe_and_steer_match := steering_sign == signf(in_strafe_left - in_strafe_right)
	
	if steer_state == SteerState.QUICK and !strafe_and_steer_match and (strafe_sign == 1 or absf(in_strafe_left - in_strafe_right) == 0.0):
		steer_state = SteerState.NORMAL
	
	if steer_state == SteerState.QUICK and strafe_and_steer_match and absf(in_steer_x) <= 0.01:
		steer_state = SteerState.NORMAL
	
	if steer_state == SteerState.DRIFT and strafe_and_steer_match and strafe_sign == -1 and car_definition.can_quickturn:
		steer_state = SteerState.QUICK
	
	var desired_drift_dot := (absf(velocity.dot( side_dir )) + maxf(velocity.dot( current_transform.basis.y ) * -0.01, 0)) * (1000.0 / car_definition.weight)
	drift_dot = minf(desired_drift_dot, lerpf(drift_dot, desired_drift_dot, MXGlobal.tick_delta * 2.0))
	
	if absf( drift_dot ) < normal_threshold and steer_state == SteerState.DRIFT:
		if !strafe_and_steer_match:
			steer_state = SteerState.NORMAL
		else:
			steer_state = SteerState.QUICK
	
	if ((drift_dot > drift_threshold and steer_state != SteerState.QUICK) or (in_strafe_left > 0.1 and in_strafe_right > 0.1 and steer_state == SteerState.NORMAL)) and car_definition.can_drift:
		drift_dot = desired_drift_dot
		steer_state = SteerState.DRIFT
	
	if grounded and inputs.Accelerate == PlayerInput.PressedState.JustPressed and steer_state != SteerState.NORMAL:
		steer_state = SteerState.NORMAL
		velocity = current_transform.basis.z * velocity.length()
	if !grounded:
		steer_state = SteerState.NORMAL
	
	if steer_state == SteerState.DRIFT or spinning:
		var drift_const : float = 0.004
		if spinning:
			drift_const = 0.0
		var drift_loss : float = MXGlobal.tick_delta * drift_dot * drift_const
		visual_drift_loss = lerpf(visual_drift_loss, drift_loss * drift_sign * 1000, MXGlobal.tick_delta * 12)
		drift_loss = lerpf(drift_loss, drift_loss / (car_definition.weight / 250), absf(current_strafe) ) * car_definition.drift_friction
		velocity += -velocity * drift_loss
	else:
		visual_drift_loss = lerpf(visual_drift_loss, 0, MXGlobal.tick_delta * 30)
	
	air_time += 1
	
	# MOVE AND COLLIDE CAR
	
	var was_grounded := grounded
	var was_conneected_to_ground := connected_to_ground
	var old_air_time := air_time
	grounded = false
	connected_to_ground = false
	previous_transform = current_transform.orthonormalized()
	current_transform = desiredTransform.orthonormalized()
	
	floor_type = RORStageObject.object_types.NONE
	
	for i in 4:
		grav_point_cast.global_position = previous_transform.origin
		grav_point_cast.target_position = grav_point_cast.to_local( current_transform.origin )
		grav_point_cast.force_update_transform()
		grav_point_cast.force_raycast_update()
		if grav_point_cast.is_colliding():
			# COLLIDE WITH ROAD
			air_time = 0
			grounded = true
			connected_to_ground = true
			var other := grav_point_cast.get_collider() as RORStageObject
			var collision_face_index := grav_point_cast.get_collision_face_index()
			var collision_point := grav_point_cast.get_collision_point()
			var data := other.road_info[collision_face_index]
			var bary_coords: Vector3 = Geometry3D.get_triangle_barycentric_coords(collision_point, data[0], data[1], data[2])
			var up_normal: Vector3 = (data[3] * bary_coords.x) + (data[4] * bary_coords.y) + (data[5] * bary_coords.z)
			var to_gravity : Quaternion = Quaternion(gravity_basis.y, up_normal)
			var arc_basis_gravity := Basis( to_gravity )
			last_ground_position = current_transform.origin
			last_gravity_direction = grav_point_cast.get_collision_normal()
			#var desired_gravity := arc_basis_gravity * gravity_basis
			var stable_dir := velocity.cross(gravity_basis.y).normalized()
			var stable_dot = to_gravity.get_axis().dot(stable_dir)
			#print(stable_dot)
			#gravity_velocity += stable_dir * gravity_velocity.dot(stable_dir) * -2.0 * MXGlobal.tick_delta
			gravity_velocity += to_gravity.get_axis() * to_gravity.get_angle() * 4 * (absf(stable_dot) * 3.0 + 1.0)
			if !was_grounded:
				var evenness : float = maxf(0.0, current_transform.basis.y.dot(up_normal))
				var horiz_velocity : Vector3 = velocity + (up_normal * velocity.dot(up_normal) * -1.0)
				var vert_velocity : Vector3 = velocity - horiz_velocity
				velocity = horiz_velocity * remap(evenness, 0.0, 1.0, 0.75, 1.0)
				velocity += velocity.normalized() * vert_velocity.length() * evenness * 0.15
			current_transform.origin = collision_point + up_normal * 0.35
			floor_type = other.object_type
			# COLLIDE WITH ROAD END
			was_grounded = grounded
			velocity = velocity.slide(last_gravity_direction).normalized() * velocity.length()
		else:
			break
	
	if !grounded:
		grav_point_cast.position = current_transform.origin + gravity_basis.y
		var extra_dist = car_definition.grip * 0.04
		if !was_grounded:
			extra_dist = 0
		grav_point_cast.target_position = -gravity_basis.y * (1.35 + extra_dist)
		grav_point_cast.force_update_transform()
		grav_point_cast.force_raycast_update()
		if grav_point_cast.is_colliding():
			# COLLIDE WITH ROAD
			air_time = 0
			grounded = true
			connected_to_ground = true
			var other := grav_point_cast.get_collider() as RORStageObject
			var collision_face_index := grav_point_cast.get_collision_face_index()
			var collision_point := grav_point_cast.get_collision_point()
			var data := other.road_info[collision_face_index]
			var bary_coords: Vector3 = Geometry3D.get_triangle_barycentric_coords(collision_point, data[0], data[1], data[2])
			var up_normal: Vector3 = (data[3] * bary_coords.x) + (data[4] * bary_coords.y) + (data[5] * bary_coords.z)
			var to_gravity : Quaternion = Quaternion(gravity_basis.y, up_normal)
			var arc_basis_gravity := Basis( to_gravity )
			last_ground_position = current_transform.origin
			last_gravity_direction = grav_point_cast.get_collision_normal()
			#var desired_gravity := arc_basis_gravity * gravity_basis
			var stable_dir := velocity.cross(gravity_basis.y).normalized()
			var stable_dot = to_gravity.get_axis().dot(stable_dir)
			#print(stable_dot)
			#gravity_velocity += stable_dir * gravity_velocity.dot(stable_dir) * -2.0 * MXGlobal.tick_delta
			gravity_velocity += to_gravity.get_axis() * to_gravity.get_angle() * 4 * (absf(stable_dot) * 3.0 + 1.0)
			if !was_grounded:
				var evenness : float = maxf(0.0, current_transform.basis.y.dot(up_normal))
				var horiz_velocity : Vector3 = velocity + (up_normal * velocity.dot(up_normal) * -1.0)
				var vert_velocity : Vector3 = velocity - horiz_velocity
				velocity = horiz_velocity * remap(evenness, 0.0, 1.0, 0.75, 1.0)
				velocity += velocity.normalized() * vert_velocity.length() * evenness * 0.15
			current_transform.origin = collision_point + up_normal * 0.35
			floor_type = other.object_type
			# COLLIDE WITH ROAD END
	
	if grounded:
		no_gravity_reset = true
		if !was_grounded and old_air_time <= Engine.physics_ticks_per_second * 0.2 and old_air_time > 2:
			print("shift boost")
			velocity += velocity.normalized() * MXGlobal.kmh_to_ups * 200
	else:
		if was_grounded:
			print("ahh")
			#var tilt_dir = (last_ground_position - current_transform.origin).cross(gravity_basis.y).normalized()
			#tilt_dir = tilt_dir.slide(current_transform.basis.z).normalized()
			#tilt_dir = tilt_dir.slide(current_transform.basis.x).normalized()
			#gravity_velocity += tilt_dir * 8
		var check_dist := 50.0
		if !was_conneected_to_ground:
			check_dist = 4.0
		grav_point_cast.position = current_transform.origin
		grav_point_cast.target_position = -gravity_basis.y * check_dist
		grav_point_cast.force_update_transform()
		grav_point_cast.force_raycast_update()
		if grav_point_cast.is_colliding():
			# ADJUST GRAVITY TO ROAD
			connected_to_ground = true
			var other := grav_point_cast.get_collider()
			var vertices: PackedVector3Array = other.vertex_positions[grav_point_cast.get_collision_face_index()]
			var vertex_normals: PackedVector3Array = other.vertex_normals[grav_point_cast.get_collision_face_index()]
			var bary_coords: Vector3 = Geometry3D.get_triangle_barycentric_coords(grav_point_cast.get_collision_point(), vertices[0], vertices[1], vertices[2])
			var up_normal: Vector3 = (vertex_normals[0] * bary_coords.x) + (vertex_normals[1] * bary_coords.y) + (vertex_normals[2] * bary_coords.z)
			var to_gravity : Quaternion = Quaternion(gravity_basis.y, up_normal)
			var arc_basis_gravity := Basis( to_gravity )
			gravity_basis = gravity_basis.slerp(arc_basis_gravity * gravity_basis, MXGlobal.tick_delta * 12)
			gravity_velocity = to_gravity.get_axis() * to_gravity.get_angle() * 10
			#gravity_velocity += to_gravity.get_axis() * to_gravity.get_angle() * 57
			air_tilt_velocity += MXGlobal.tick_delta * 15
			# ADJUST GRAVITY TO ROAD END
			no_gravity_reset = true
		else:
			no_gravity_reset = false
	
	if !gravity_velocity.is_zero_approx() and grounded:
		gravity_velocity += -gravity_velocity * MXGlobal.tick_delta * 8
		var stable_dir := velocity.cross(gravity_basis.y).normalized()
		gravity_velocity += stable_dir * gravity_velocity.dot(stable_dir) * -24.0 * MXGlobal.tick_delta
		gravity_basis = gravity_basis.rotated(gravity_velocity.normalized(), gravity_velocity.length() * MXGlobal.tick_delta).orthonormalized()
		if grounded:
			var slid_velocity := velocity.slide(last_gravity_direction)
			velocity = velocity.normalized().slerp(slid_velocity.normalized(), MXGlobal.tick_delta * 8).normalized() * velocity.length()
			
	# NOTE could use a second ray so you don't need to switch its mask each time
	grav_point_cast.set_collision_mask_value(2, true)
	grav_point_cast.position = previous_transform.origin
	grav_point_cast.force_update_transform()
	
	for point in car_definition.wall_colliders:
		var global_point : Vector3 = current_transform * point
		grav_point_cast.target_position = grav_point_cast.to_local(global_point)
		grav_point_cast.force_raycast_update()
		var test_dir := global_point - grav_point_cast.position
		if grav_point_cast.is_colliding() and test_dir.dot(grav_point_cast.get_collision_normal()) <= 0:# and apparent_velocity.dot(grav_point_cast.get_collision_normal()) <= 0:
			var depth : float = (grav_point_cast.get_collision_point() - global_point).dot(grav_point_cast.get_collision_normal())
			current_transform.origin += grav_point_cast.get_collision_normal() * (depth + 0.05)
			if grav_point_cast.get_collider().get_collision_mask_value(2):
				#var amount : float = velocity.dot(grav_point_cast.get_collision_normal())
				#current_transform.basis = current_transform.basis.rotated(velocity.cross(grav_point_cast.get_collision_normal()).normalized(), amount * -0.001)
				var altered_normal : Vector3 = grav_point_cast.get_collision_normal() - gravity_basis.y * grav_point_cast.get_collision_normal().dot(gravity_basis.y)
				altered_normal = altered_normal.normalized()
				#var altered_velocity : Vector3 = apparent_velocity.length() * velocity.normalized()
				var impulse : Vector3 = altered_normal * (velocity.dot(altered_normal)) * -1.5
				var apparent_impulse : Vector3 = altered_normal * (apparent_velocity.dot(altered_normal)) * -1.0
				if impulse.length() + apparent_impulse.length() > 5 and !MXGlobal.currentlyRollback:
					MXGlobal.play_sound_from_node(preload("res://content/base/sound/car/wall_collision_heavy.wav"), car_mesh)
				health -= impulse.length() * 0.025
				#var old_vel = velocity.length()
				#var new_vel_length = (velocity + impulse).length()
				velocity += impulse
				#velocity = velocity.normalized() * maxf((velocity.length() - 15), 0)
				knockback_velocity += apparent_impulse * 1.0
				if knockback_velocity.length() > 80:
					knockback_velocity = knockback_velocity.normalized() * 80
	grav_point_cast.set_collision_mask_value(2, false)
