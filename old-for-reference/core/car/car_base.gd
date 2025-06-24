@tool

class_name MXRacer extends Node3D

@onready var recharge_particles := $car_visual/RechargeParticles as GPUParticles3D
@onready var car_camera := %car_camera as Camera3D 
@onready var boost_electricity: BoostElectricity = $car_visual/BoostElectricity

@onready var world_sphere_cast: ShapeCast3D = %world_sphere_cast
@onready var world_ray_cast: RayCast3D = %world_ray_cast
@onready var attack_particles: GPUParticles3D = $car_visual/AttackParticles

@onready var car_visual := %car_visual as Node3D
@onready var race_hud := %race_hud as RaceHud
@onready var car_audio:= %CarAudio as AudioStreamPlayer3D

var landing_particles : Array[GPUParticles3D] = []

@onready var debug_car_mesh := %debug_car_mesh as MeshInstance3D
var game_car_mesh : Node3D

@export var car_definition : CarDefinition:
	set(new_definition):
		car_definition = new_definition
@onready var car_nametag := %Nametag as MeshInstance3D

var accel_setting := 0.5

var car_buffer : StreamPeerBuffer = StreamPeerBuffer.new()

var pawnID : int = 0

var matrix_stack : Array[Transform3D] = []
var matrix_pointer := 0









var mtxa : Transform3D:
	get():
		return matrix_stack[matrix_pointer]
	set(in_matrix):
		matrix_stack[matrix_pointer] = in_matrix
		
func mtxa_push() -> void:
	matrix_pointer += 1
	matrix_stack[matrix_pointer] = matrix_stack[matrix_pointer - 1]

func mtxa_pop() -> void:
	matrix_pointer -= 1

	
	
	
	
	
func copy_mtx_to_mtxa(in_matrix : Transform3D) -> void:
	matrix_stack[matrix_pointer] = in_matrix

func clear_mtxa_translation() -> void:
	mtxa.origin = Vector3.ZERO

func mtxa_transform_point(in_p : Vector3) -> Vector3:
	return mtxa * in_p

func mtxa_inverse_transform_point(in_p : Vector3) -> Vector3:
	return in_p * mtxa
	
func mtxa_rotate_point(in_p : Vector3) -> Vector3:
	return mtxa.basis * in_p
	
func mtxa_inverse_rotate_point(in_p : Vector3, invert := false) -> Vector3:
	var fVar1 : float
	var fVar2 : float
	var fVar3 : float
	var fVar4 : float
	var fVar5 : float
	var fVar6 : float
	var fVar7 : float
	var fVar8 : float
	var fVar9 : float
	var fVar10 : float
	
	fVar1 = in_p.x;
	fVar2 = in_p.y;
	fVar3 = in_p.z;
	fVar4 = mtxa.basis.x.x;
	fVar5 = mtxa.basis.y.x;
	fVar6 = mtxa.basis.z.x;
	fVar7 = mtxa.basis.x.y;
	fVar8 = mtxa.basis.y.y;
	fVar9 = mtxa.basis.z.y;
	fVar10 = fVar7 * fVar2 + fVar4 * fVar1;
	fVar7 = fVar8 * fVar2 + fVar5 * fVar1;
	fVar4 = mtxa.basis.x.z;
	fVar5 = mtxa.basis.y.z;
	fVar1 = fVar9 * fVar2 + fVar6 * fVar1;
	fVar2 = mtxa.basis.z.z;
	if invert:
		fVar10 = -(fVar4 * fVar3 + fVar10);
		fVar7 = -(fVar5 * fVar3 + fVar7);
		fVar1 = -(fVar2 * fVar3 + fVar1);
	else:
		fVar10 = fVar4 * fVar3 + fVar10;
		fVar7 = fVar5 * fVar3 + fVar7;
		fVar1 = fVar2 * fVar3 + fVar1;
	var out : Vector3
	out.x = fVar10;
	out.y = fVar7;
	out.z = fVar1;
	return out

func make_axis_angle_quat(in_axis : Vector3, in_angle : float) -> Quaternion:
	return Quaternion(in_axis, in_angle)

func mtxa_from_quat(in_quat : Quaternion) -> void:
	mtxa.basis = Basis(in_quat)
	mtxa.origin = Vector3.ZERO

func mtxa_from_identity() -> void:
	mtxa = Transform3D.IDENTITY

func mtxa_premultiply_mtx(in_matrix : Transform3D) -> void:
	mtxa = in_matrix * mtxa

func mtxa_multiply_mtx(in_matrix : Transform3D) -> void:
	mtxa = mtxa * in_matrix






func mtxa_rotate_about_x(in_angle_rad : float) -> void:
	mtxa = mtxa.rotated_local(Vector3.RIGHT, -in_angle_rad)
func mtxa_rotate_about_y(in_angle_rad : float) -> void:
	mtxa = mtxa.rotated_local(Vector3.UP, in_angle_rad)
func mtxa_rotate_about_z(in_angle_rad : float) -> void:
	mtxa = mtxa.rotated_local(Vector3.FORWARD, -in_angle_rad)

func set_vec3_length(in_vec : Vector3, in_len : float) -> Vector3:
	return in_vec.normalized() * in_len

enum FZ_TERRAIN {
	NORMAL = 0x1,
	DASHPLATE = 0x2,
	RECHARGE = 0x4,
	DIRT = 0x8,
	ICE = 0x10,
	JUMP = 0x20,
	LAVA = 0x40
}

enum FZ_MS {
	B1 = 0x1,
	AIRBORNE = 0x2,
	AIRBORNEMORE0_2S_Q = 0x4,
	SPINATTACKING = 0x8,
	JUSTLANDED = 0x10,
	BOOSTING = 0x20,
	JUST_PRESSED_BOOST = 0x40,
	ZEROHP = 0x80,
	B9 = 0x100,
	B10 = 0x200,
	ACTIVE = 0x400,
	FALLOUT = 0x800,
	MANUAL_DRIFT = 0x1000,
	B14 = 0x2000,
	STRAFING = 0x4000,
	STARTINGCOUNTDOWN = 0x8000,
	COMPLETEDRACE_1_Q = 0x10000,
	SIDEATTACKING = 0x20000,
	CROSSEDLAPLINE_Q = 0x40000,
	JUSTTAPPEDACCEL = 0x80000,
	RACEJUSTBEGAN_Q = 0x100000,
	BOOSTING_DASHPLATE = 0x200000,
	JUST_HIT_DASHPLATE = 0x400000,
	TOOKDAMAGE = 0x800000,
	LOWGRIP = 0x1000000,
	JUSTHITVEHICLE_Q = 0x2000000,
	COMPLETEDRACE_2_Q = 0x4000000,
	RETIRED = 0x8000000,
	B29 = 0x10000000,
	B30 = 0x20000000,
	DIEDTHISFRAMEOOB_Q = 0x40000000,
	VEHICLEACTIVE_Q = 0x80000000
}

var calced_max_energy := 100.0
var machine_state := 0
var stat_weight := 0.0
var stat_grip_1 := 0.0
var stat_grip_2 := 0.0
var stat_grip_3 := 0.0
var stat_turn_tension := 0.0
var stat_turn_movement := 0.0
var stat_strafe_turn := 0.0
var stat_strafe := 0.0
var stat_turn_reaction := 0.0
var stat_drift_accel := 0.0
var stat_body := 0.0
var stat_acceleration := 0.0
var stat_max_speed := 0.0
var stat_boost_strength := 0.0
var stat_boost_length := 0.0
var stat_turn_decel := 0.0
var stat_drag := 0.0
var stat_accel_press_grip_frames := 0
var camera_reorienting := 0.0
var camera_repositioning:= 0.0
var machine_name := "Blue Falcon"
var position_current := Vector3.ZERO
var position_old := Vector3.ZERO
var position_old_2 := Vector3.ZERO
var position_old_dupe := Vector3.ZERO
var position_bottom := Vector3.ZERO
var position_behind := Vector3.ZERO
var velocity := Vector3.ZERO
var velocity_angular := Vector3.ZERO
var velocity_local := Vector3.ZERO
var velocity_local_flattened_and_rotated := Vector3.ZERO
var base_speed := 0.0
var boost_turbo := 0.0
var weight_derived_1 := 0.0
var weight_derived_2 := 0.0
var weight_derived_3 := 0.0
var visual_rotation := Vector3.ZERO
var race_start_charge := 0.0
var basis_physical := Transform3D.IDENTITY
var basis_physical_other := Transform3D.IDENTITY
var transform_visual := Transform3D.IDENTITY
var speed_kmh := 0.0
var air_tilt := 0.0
var energy := 0.0
var boost_frames := 0
var boost_frames_manual := 0
var height_adjust_from_boost := 0.0
var spinattack_direction := 0
var spinattack_angle := 0.0
var spinattack_decrement := 0.0
var brake_timer := 0
var collision_push_track := Vector3.ZERO
var collision_push_rail := Vector3.ZERO
var collision_push_total := Vector3.ZERO
var collision_response := Vector3.ZERO
var track_surface_normal := Vector3.ZERO
var track_surface_normal_prev := Vector3.ZERO
var track_surface_pos := Vector3.ZERO
var height_above_track := 0.0
var current_checkpoint := 0
var checkpoint_fraction := 0.0
var lap := 0
var input_strafe_32 := 0.0
var input_strafe_1_6 := 0.0
var input_steer_pitch := 0.0
var input_strafe := 0.0
var input_steer_yaw := 0.0
var input_accel := 0.0
var input_brake := 0.0
var input_yaw_dupe := 0.0
var rail_collision_timer := 0.0
var terrain_state := 0
var tilt_corners : Array[MachineTiltCorner] = [MachineTiltCorner.new(), MachineTiltCorner.new(), MachineTiltCorner.new(), MachineTiltCorner.new()]
var wall_corners : Array[MachineWallCorner] = [MachineWallCorner.new(), MachineWallCorner.new(), MachineWallCorner.new(), MachineWallCorner.new()]
var grip_frames_from_accel_press := 0
var visual_shake_mult := 0.0
var frames_since_start := 0
var frames_since_start_2 := 0
var side_attack_delay := 0
var air_time := 0
var damage_from_last_hit := 0.0
var strafe_effect := 0
var machine_crashed := false
var machine_collision_frame_counter := 0
var car_hit_invincibility := 0
var boost_delay_frame_counter := 0
var turn_reaction_input := 0.0
var turn_reaction_effect := 0.0
var boost_energy_use_mult := 0.0
var frames_since_death := 0
var terrain_state_2 := 0
var suspension_reset_flag := 0
var turning_related := 0.0 
var stat_obstacle_collision := 0.0
var stat_track_collision := 0.0
var state_2 := 0
var side_attack_indicator := 0.0
var lap_progress := 0.0
var unk_stat_0x5d4 := 0.0
var g_anim_timer := 0
var g_pitch_mtx_0x5e0 := Transform3D.IDENTITY
var strafe_visual_roll := 0
var unk_quat_0x5c4 := Quaternion.IDENTITY


var damage := 0.0
var levelStartTime := MXGlobal.countdownTime
var level_win_time := 0
var camera_basis : Basis = Basis.IDENTITY
var camera_basis_smoothed : Basis = Basis.IDENTITY

@onready var nametag_background := %NametagBackground
@onready var placement_sprite := $car_visual/PlacementSprite
@onready var car_shadow_camera := $CarShadowViewport/CarShadowCamera as Camera3D
@onready var car_shadow_mesh := $CarShadowMesh as MeshInstance3D
@onready var car_shadow_viewport := $CarShadowViewport as SubViewport

var just_ticked := false

var placement_textures : Array[Texture] = [
	preload("res://content/base/texture/ui/placements/mx-1.png"),
	preload("res://content/base/texture/ui/placements/mx-2.png"),
	preload("res://content/base/texture/ui/placements/mx-3.png"),
	preload("res://content/base/texture/ui/placements/mx-4.png"),
	preload("res://content/base/texture/ui/placements/mx-5.png"),
	preload("res://content/base/texture/ui/placements/mx-6.png"),
	preload("res://content/base/texture/ui/placements/mx-7.png"),
	preload("res://content/base/texture/ui/placements/mx-8.png"),
	preload("res://content/base/texture/ui/placements/mx-9.png"),
	preload("res://content/base/texture/ui/placements/mx-10.png"),
	preload("res://content/base/texture/ui/placements/mx-11.png"),
	preload("res://content/base/texture/ui/placements/mx-12.png"),
	preload("res://content/base/texture/ui/placements/mx-13.png"),
	preload("res://content/base/texture/ui/placements/mx-14.png"),
	preload("res://content/base/texture/ui/placements/mx-15.png"),
	preload("res://content/base/texture/ui/placements/mx-16.png"),
	preload("res://content/base/texture/ui/placements/mx-17.png"),
	preload("res://content/base/texture/ui/placements/mx-18.png"),
	preload("res://content/base/texture/ui/placements/mx-19.png"),
	preload("res://content/base/texture/ui/placements/mx-20.png"),
	preload("res://content/base/texture/ui/placements/mx-21.png"),
	preload("res://content/base/texture/ui/placements/mx-22.png"),
	preload("res://content/base/texture/ui/placements/mx-23.png"),
	preload("res://content/base/texture/ui/placements/mx-24.png"),
	preload("res://content/base/texture/ui/placements/mx-25.png"),
	preload("res://content/base/texture/ui/placements/mx-26.png"),
	preload("res://content/base/texture/ui/placements/mx-27.png"),
	preload("res://content/base/texture/ui/placements/mx-28.png"),
	preload("res://content/base/texture/ui/placements/mx-29.png"),
	preload("res://content/base/texture/ui/placements/mx-30.png")]

func _ready() -> void:
	if Engine.is_editor_hint():
		return
	
	matrix_stack.resize(16)
	debug_car_mesh.queue_free()
	initialize_machine()
	
	for corner in tilt_corners:
		var new_landing_particles := preload("res://content/base/effects/particles/car_landing_particles.tscn").instantiate()
		car_visual.add_child(new_landing_particles)
		new_landing_particles.emitting = false
		new_landing_particles.position = corner.offset
		landing_particles.append(new_landing_particles)
	
	if get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
		car_camera.make_current()
		car_camera.far = 30000
		car_camera.near = 0.1
		process_priority = 100
	levelStartTime = MXGlobal.countdownTime
	if multiplayer and multiplayer.has_multiplayer_peer() and !get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
		if is_instance_valid(race_hud):
			race_hud.queue_free()
		
	else:
		nametag_background.queue_free()
		placement_sprite.queue_free()
	
	
	energy = calced_max_energy
	
	var current_stage := MXGlobal.currentStage
	current_checkpoint = current_stage.checkpoint_respawns.size() - 1
	lap = 0
	
	
	
	game_car_mesh = car_definition.model.instantiate()
	car_visual.add_child(game_car_mesh)
	var real_mesh := game_car_mesh.get_child(0) as MeshInstance3D
	real_mesh.set_layer_mask_value(19, true)
	if multiplayer and multiplayer.has_multiplayer_peer() and !get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
		real_mesh.set_layer_mask_value(20, false)
		real_mesh.set_layer_mask_value(1, true)
		car_shadow_camera.queue_free()
		car_shadow_viewport.queue_free()
		car_shadow_mesh.queue_free()
	
	var mesh_instance:MeshInstance3D = game_car_mesh.get_child(0)
	if multiplayer and not multiplayer.get_peers().is_empty():
		mesh_instance.set_instance_shader_parameter("base_color", Net.peer_map[get_multiplayer_authority()].player_settings.base_color)
		mesh_instance.set_instance_shader_parameter("secondary_color", Net.peer_map[get_multiplayer_authority()].player_settings.secondary_color)
		mesh_instance.set_instance_shader_parameter("tertiary_color", Net.peer_map[get_multiplayer_authority()].player_settings.tertiary_color)
	else:
		mesh_instance.set_instance_shader_parameter("base_color", MXGlobal.local_settings.base_color)
		mesh_instance.set_instance_shader_parameter("secondary_color", MXGlobal.local_settings.secondary_color)
		mesh_instance.set_instance_shader_parameter("tertiary_color", MXGlobal.local_settings.tertiary_color)

func _post_stage_loaded() -> void:
	pass

func set_nametag(inName : String) -> void:
	await get_tree().create_timer(0.5).timeout
	if car_nametag and is_instance_valid(car_nametag):
		var shader_mat_1 : ShaderMaterial = nametag_background.get_active_material(0).duplicate(true)
		var shader_mat_2 : ShaderMaterial = car_nametag.get_active_material(0).duplicate(true)
		nametag_background.material_override = shader_mat_1
		car_nametag.material_override = shader_mat_2
		nametag_background.mesh = nametag_background.mesh.duplicate(true)
		car_nametag.mesh = car_nametag.mesh.duplicate(true)
		car_nametag.mesh.text = inName
		var bgMesh : PlaneMesh = %NametagBackground.mesh
		bgMesh.size.x = inName.length() * 0.08

func handle_car_audio(delta : float) -> void:
	var car_audio_playback := car_audio.get_stream_playback() as AudioStreamPlaybackPolyphonic
	var vel_len : float = velocity.length()
	var vel_rescaled : float = clampf(pow(remap(MXGlobal.ups_to_kmh * vel_len, 0, 1600, 0, 1), 2), 0, 1)
	var thrust_volume : float = remap(vel_rescaled, 0, 1, -32, -24)
	var tone_volume : float = remap(vel_rescaled, 0, 1, -30, -5)
	if Net.connected and get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
		thrust_volume -= 10
		tone_volume -= 10
	var tone_pitch : float = remap(vel_rescaled, 0, 1, 0.4, 1.2)
	var p:ROPlayer = get_parent()
	var player_input:PlayerInput = p.get_latest_input()

func _process( delta:float ) -> void:
	delta = 0.0166666
	
	if Engine.is_editor_hint():
		car_shadow_mesh.global_position = position_bottom + Vector3(0, -0.5, 0.0)
		for capsule in car_definition.car_colliders:
			var substeps : int = 17
			var forward : Vector3 = (capsule.p2 - capsule.p1).normalized()
			var right : Vector3 = forward.cross(Vector3.UP).normalized()
			for i in substeps:
				var t : float = (float(i) / (substeps - 1)) * PI * 2
				var line_p1 : Vector3 = (Vector3.UP * sin(t) + right * cos(t)) * capsule.radius + capsule.p1
				var line_p2 : Vector3 = (Vector3.UP * sin(t) + right * cos(t)) * capsule.radius + capsule.p2
			var tp1 : Transform3D = Transform3D(Basis(right * capsule.radius * 2, Vector3.UP * capsule.radius * 2, forward * capsule.radius * 2), capsule.p1)
			var tp2 : Transform3D = Transform3D(Basis(right * capsule.radius * 2, Vector3.UP * capsule.radius * 2, forward * capsule.radius * 2), capsule.p2)
		return
	if !just_ticked:
		return
	just_ticked = false
	if !is_instance_valid(MXGlobal.currentStageOverseer):
		return
	var is_own_car := true
	if multiplayer and multiplayer.has_multiplayer_peer():
		is_own_car = get_multiplayer_authority() == multiplayer.get_unique_id()
	if get_parent().place < 3 and !is_own_car:
		placement_sprite.visible = true
		placement_sprite.texture = placement_textures[get_parent().place]
	elif is_instance_valid(placement_sprite):
		placement_sprite.visible = false
	
	var p:ROPlayer = get_parent()
	var player_input:PlayerInput = p.get_latest_input()
	car_visual.transform = transform_visual
	
	damage += -damage * delta * 16
	var mesh_instance := game_car_mesh.get_child(0) as MeshInstance3D
	var energy_ratio : float = minf(1.0, (energy / calced_max_energy) * 4.0)
	var manual_boost_visual := float(boost_frames_manual) / (car_definition.boost_length * Engine.physics_ticks_per_second)
	var dashplate_visual := float(boost_frames) / (car_definition.boost_length * Engine.physics_ticks_per_second * 0.5)
	var boost_ratio : float = dashplate_visual if (machine_state & FZ_MS.BOOSTING_DASHPLATE) else manual_boost_visual
	var energy_flash := Color(0.04, -0.01, -0.01) * (sin(0.015 * Time.get_ticks_msec()) * 0.5 + 0.5) * (1.0 - energy_ratio)
	var boost_flash := Color(0, 0.03, 0.075) * (boost_ratio)
	var final_overlay := energy_flash + boost_flash + Color(1, 1, 1) * damage * 0.1
	final_overlay.a = 1.0
	mesh_instance.set_instance_shader_parameter("overlay_color", final_overlay * 20.0)
	
	if (machine_state & FZ_MS.JUSTLANDED) != 0:
		for particle in landing_particles:
			particle.rotation_degrees = Vector3(0, 180, 0)
			particle.scale = Vector3(3, 3, 3)
			particle.restart()
			particle.emitting = true
	
	var should_thrust := just_tapped_accel_visual or (input_accel > 0 and (MXGlobal.currentStageOverseer.localTick == levelStartTime - 180 or MXGlobal.currentStageOverseer.localTick == levelStartTime))
	var should_boost := (machine_state & FZ_MS.BOOSTING) != 0 or (machine_state & FZ_MS.BOOSTING_DASHPLATE) != 0
	var thrust_power := pow(base_speed * 0.25, 0.25) if base_speed * 0.25 < 1.0 else base_speed * 0.25
	var attacking := (machine_state & (FZ_MS.SPINATTACKING | FZ_MS.SIDEATTACKING)) != 0
	attack_particles.emitting = attacking
	
	if should_boost and (machine_state & FZ_MS.AIRBORNE) == 0:
		boost_electricity.bounds = AABB(Vector3.ZERO, Vector3.ZERO)
		for i in tilt_corners.size():
			boost_electricity.bounds = boost_electricity.bounds.expand(tilt_corners[i].offset * 0.5)
		
		boost_electricity.global_transform = transform_visual
		boost_electricity.global_position += transform_visual.basis.y * 2.0
		boost_electricity.boosting = true
		boost_electricity.ground = Plane(track_surface_normal, track_surface_pos)
		boost_electricity.tendril_lifetime = clampf(remap(speed_kmh, 1000, 2000, 0.25, 0.05), 0.05, 0.25)
	else:
		boost_electricity.boosting = false
	
	boost_electricity.calculate_electricity(delta, transform_visual.translated_local(Vector3.UP * 0.5))
	
	for child in game_car_mesh.get_children():
		if child is ThrusterFire:
			if should_thrust:
				child.thrust_enabled = true
			elif input_accel == 0:
				child.thrust_enabled = false
			child.boosting = should_boost
			child.desired_thrust_power = thrust_power
	
		
	
	if get_viewport() and get_viewport().get_camera_3d():
		
		
		
		
		
		var inv_weight := 1.0 / stat_weight
		var inv_vel := velocity * inv_weight
		
		var target_fov := remap(speed_kmh, 0, 1800, 50, 90)
		target_fov += remap(boost_ratio, 0, 1, 0, 50)
		target_fov = minf(target_fov, 100)
		
		car_camera.fov = lerpf(car_camera.fov, target_fov, MXGlobal.tick_delta * 2)
		var use_forward_z : Vector3 = basis_physical.basis.z
		use_forward_z = use_forward_z.normalized()
		if (tilt_corners[0].state & MachineTiltCorner.FZ_TC.DRIFT) != 0:
			use_forward_z = -velocity.slide(basis_physical.basis.y.normalized()).normalized()
		
		var target_y := basis_physical.basis.y
		
		var starting_frames_past := frames_since_start_2 > 90
		
		if !slerped_up_y.is_equal_approx(target_y):
			slerped_up_y = slerped_up_y.slerp(target_y, car_definition.camera_reorienting * 0.4).normalized()
		slerped_forward_z = slerped_forward_z.slerp(use_forward_z, 0.2 * car_definition.camera_repositioning).normalized()
		
		var use_slerpto = Basis(Quaternion(basis_physical.basis.z, slerped_forward_z)) * basis_physical.basis
		use_slerpto = Basis(Quaternion(basis_physical.basis.y, slerped_up_y)) * use_slerpto
		camera_basis = camera_basis.slerp(use_slerpto, 0.5).orthonormalized()
		camera_basis_smoothed = camera_basis_smoothed.slerp(camera_basis, 0.5).orthonormalized()
		var use_basis := camera_basis_smoothed
		
		
		var final_y := slerped_up_y
		if starting_frames_past:
			var flat_up_y := slerped_up_y.slide(use_basis.x).normalized()
			var flat_basis_y := basis_physical.basis.y.slide(use_basis.x).normalized()
			var rot_angle_1 := flat_up_y.signed_angle_to(flat_basis_y, use_basis.x)
			var rot_angle_2 := use_basis.y.signed_angle_to(flat_basis_y, use_basis.x)
			slerped_up_y = slerped_up_y.rotated(use_basis.x, rot_angle_1 * 0.75)
			use_basis = use_basis.rotated(use_basis.x, rot_angle_2 * 0.75)
			final_y = slerped_up_y
			if (machine_state & FZ_MS.AIRBORNE) == 0:
				if inv_vel.length() > 1:
					var sideways := velocity.normalized().cross(track_normal_vis).normalized()
					var flattened := track_normal_vis.slide(sideways).normalized()
					var flattened_prev := track_normal_old_vis.slide(sideways).normalized()
					var road_angle_change := flattened.signed_angle_to(flattened_prev, sideways)
					var arc_length := inv_vel.length() * 100
					lerped_curvature = lerpf(lerped_curvature, road_angle_change / arc_length if !is_zero_approx(arc_length) else 0.0, 0.125)
					final_y = final_y.rotated(sideways, lerped_curvature * -2000)
					use_basis = use_basis.rotated(sideways, lerped_curvature * -2000)
		car_camera.position = position_current + final_y * remap(car_camera.fov, 50, 100, 6.0, 5.5) + use_basis.z * remap(car_camera.fov, 50, 100, 12.0, 6.0)
		car_camera.basis = use_basis.rotated(use_basis.x, deg_to_rad(-15))
		
		var track_floor := raycast_world(position_current + track_surface_normal, position_current - track_surface_normal * 200, 1)
		if track_floor.size() > 0:
			track_normal_old_vis = track_normal_vis
			track_normal_vis = track_floor[0].surface_normal
			var shbz := track_surface_normal
			var shbx := track_surface_normal.cross(car_visual.basis.z).normalized()
			var shby := shbz.cross(shbx).normalized()
			car_shadow_camera.global_position = game_car_mesh.global_position + track_surface_normal * 4.0
			car_shadow_camera.global_basis = Basis(shbx, shby, shbz);
			var floor_plane := Plane(track_surface_normal, track_floor[0].surface_position)
			car_shadow_mesh.global_position = floor_plane.project(position_current)
			car_shadow_mesh.global_basis = Basis(shbx, shby, shbz);
			var shadow_shader_mat := car_shadow_mesh.get_active_material(0) as ShaderMaterial
			shadow_shader_mat.set_shader_parameter("car_shadow", car_shadow_viewport.get_texture())

var track_normal_vis := Vector3.UP
var track_normal_old_vis := Vector3.UP
var lerped_curvature := 0.0
var slerped_up_y := Vector3.UP
var slerped_forward_z := Vector3.ZERO

static func edge_cp(pi:Vector3, pj:Vector3, ni:Vector3) -> Vector3:
	var wi:float = (pj - pi).dot(ni)
	return (pi * 2.0 + pj - wi * ni) / 3.0

static func edge_n(ni:Vector3, nj:Vector3, pi:Vector3, pj:Vector3) -> Vector3:
	var vij:Vector3 = pj - pi
	var coeff:float = 2.0 * vij.dot(ni + nj) / vij.length_squared()
	return (ni + nj - coeff * vij).normalized()
	
static func pn_surface_point(
	u:float, v:float, w:float,
	p0:Vector3, p1:Vector3, p2:Vector3,
	n0:Vector3, n1:Vector3, n2:Vector3
) -> Vector3:
	
	if is_zero_approx(u + v + w):
		return (p0 + p1 + p2) * 0.3333333333
	var s:float = 1.0 / (u + v + w)
	u *= s
	v *= s
	w *= s

	
	var b210:Vector3 = edge_cp(p0, p1, n0)	
	var b120:Vector3 = edge_cp(p1, p0, n1)	
	var b021:Vector3 = edge_cp(p1, p2, n1)	
	var b012:Vector3 = edge_cp(p2, p1, n2)	
	var b102:Vector3 = edge_cp(p2, p0, n2)	
	var b201:Vector3 = edge_cp(p0, p2, n0)	

	
	var e:Vector3 = (b210 + b120 + b021 + b012 + b102 + b201) / 6.0
	var vtx_centroid:Vector3 = (p0 + p1 + p2) / 3.0
	var b111:Vector3 = e + (e - vtx_centroid) * 0.5	

	
	var uu:float = u * u
	var vv:float = v * v
	var ww:float = w * w

	
	return \
		p0 * uu * u + \
		p1 * vv * v + \
		p2 * ww * w + \
		b210 * (3.0 * uu * v) + \
		b120 * (3.0 * u * vv) + \
		b201 * (3.0 * uu * w) + \
		b102 * (3.0 * u * ww) + \
		b021 * (3.0 * vv * w) + \
		b012 * (3.0 * v * ww) + \
		b111 * (6.0 * u * v * w)
		
static func pn_surface_normal(
	u:float, v:float, w:float,
	p0:Vector3, p1:Vector3, p2:Vector3,
	n0:Vector3, n1:Vector3, n2:Vector3
) -> Vector3:
	if is_zero_approx(u + v + w):
		return (n0 + n1 + n2).normalized()
	var s:float = 1.0 / (u + v + w)
	u *= s
	v *= s
	w *= s

	var n110:Vector3 = edge_n(n0, n1, p0, p1)
	var n011:Vector3 = edge_n(n1, n2, p1, p2)
	var n101:Vector3 = edge_n(n2, n0, p2, p0)

	var uu:float = u * u
	var vv:float = v * v
	var ww:float = w * w

	return (n0 * uu + n1 * vv + n2 * ww + n110 * u * v + n011 * v * w + n101 * w * u).normalized()

static func phong_surface_point(
	u: float, v: float, w: float,
	p0: Vector3, p1: Vector3, p2: Vector3,
	n0: Vector3, n1: Vector3, n2: Vector3
) -> Vector3:
	
	n0 = n0.normalized()
	n1 = n1.normalized()
	n2 = n2.normalized()

	
	var flat_pos: Vector3 = u * p0 + v * p1 + w * p2
	

	
	var proj0: Vector3 = flat_pos - (flat_pos - p0).dot(n0) * n0
	var proj1: Vector3 = flat_pos - (flat_pos - p1).dot(n1) * n1
	var proj2: Vector3 = flat_pos - (flat_pos - p2).dot(n2) * n2

	
	return (u * proj0 + v * proj1 + w * proj2).lerp(flat_pos, 0.5)

func sphere_sweep_world(p0 : Vector3, p1 : Vector3, in_mask : int, in_radius : float = 0.001) -> Array[Dictionary]:
	
	
	var done_colliding := false
	
	
	
	world_sphere_cast.collision_mask = in_mask
	world_sphere_cast.position = p0
	world_sphere_cast.shape.radius = in_radius
	var out_hits : Array[Dictionary] = []
	while !done_colliding:
		
		world_sphere_cast.target_position = Vector3.ZERO
		world_sphere_cast.force_update_transform()
		world_sphere_cast.force_shapecast_update()
		if world_sphere_cast.is_colliding():
			var hit : Dictionary = {}
			var surface_position : Vector3
			var surface_normal : Vector3
			var surface_type : RORStageObject.object_types
			for i in world_sphere_cast.get_collision_count():
				var other := world_sphere_cast.get_collider(i)
				world_sphere_cast.add_exception(other)
				if other is RORStageObject:
					hit.surface_type = other.object_type
				else:
					if other.get_parent() is BoostPad:
						hit.surface_type = RORStageObject.object_types.DASH
					if other.get_parent() is JumpPlate:
						hit.surface_type = RORStageObject.object_types.JUMP
				hit.surface_normal = world_sphere_cast.get_collision_normal(i)
				hit.surface_position = world_sphere_cast.get_collision_point(i)
				hit.surface_position += hit.surface_normal * 0.1
				hit.stop_point = world_sphere_cast.position
				hit.hit_time = 0.0
				out_hits.append(hit)
		else:
			done_colliding = true
			
	done_colliding = false
	world_sphere_cast.clear_exceptions()
	while !done_colliding:
		world_sphere_cast.target_position = world_sphere_cast.to_local( p1 )
		world_sphere_cast.force_update_transform()
		world_sphere_cast.force_shapecast_update()
		if world_sphere_cast.is_colliding():
			var hit : Dictionary = {}
			var surface_position : Vector3
			var surface_normal : Vector3
			var surface_type : RORStageObject.object_types
			for i in world_sphere_cast.get_collision_count():
				var other := world_sphere_cast.get_collider(i)
				world_sphere_cast.add_exception(other)
				if other is RORStageObject:
					hit.surface_type = other.object_type
				else:
					if other.get_parent() is BoostPad:
						hit.surface_type = RORStageObject.object_types.DASH
					if other.get_parent() is JumpPlate:
						hit.surface_type = RORStageObject.object_types.JUMP
				hit.surface_normal = world_sphere_cast.get_collision_normal(i)
				hit.surface_position = world_sphere_cast.get_collision_point(i)
				hit.surface_position += hit.surface_normal * 0.1
				hit.stop_point = hit.surface_position + hit.surface_normal * stat_track_collision
				var seg := p1 - p0
				var seg_len := seg.length()
				var hit_seg : Vector3 = hit.stop_point - p0
				var hit_len := hit_seg.length()
				hit.hit_time = minf(1.0, hit_len / seg_len)
				out_hits.append(hit)
		else:
			done_colliding = true
	world_sphere_cast.clear_exceptions()
	return out_hits

func raycast_world(p0 : Vector3, p1 : Vector3, in_mask : int) -> Array[Dictionary]:
	world_ray_cast.collision_mask = in_mask
	world_ray_cast.position = p0
	var out_hits : Array[Dictionary] = []
	world_ray_cast.target_position = world_ray_cast.to_local( p1 )
	world_ray_cast.force_update_transform()
	world_ray_cast.force_raycast_update()
	if world_ray_cast.is_colliding():
		var hit : Dictionary = {}
		var surface_position : Vector3
		var surface_normal : Vector3
		var surface_type : RORStageObject.object_types
		var other := world_ray_cast.get_collider() as RORStageObject
		if (in_mask & 1) != 0 and other.object_type != RORStageObject.object_types.RAIL:
			
			var collision_point := world_ray_cast.get_collision_point()
			var collision_normal := world_ray_cast.get_collision_normal()
			var data := other.road_info[world_ray_cast.get_collision_face_index()]
			var projected_pos := (position - data[0]).slide(world_ray_cast.get_collision_normal()) + data[0]
			var bary_coords: Vector3 = Geometry3D.get_triangle_barycentric_coords(collision_point, data[0], data[1], data[2])
			hit.surface_normal = (data[3] * bary_coords.x) + (data[4] * bary_coords.y) + (data[5] * bary_coords.z)
			var target_surface := pn_surface_point(bary_coords.x, bary_coords.y, bary_coords.z,	data[0], data[1], data[2], data[3], data[4], data[5])
			
			
			
			hit.surface_position = target_surface
		else:
			hit.surface_normal = world_ray_cast.get_collision_normal()
			hit.surface_position = world_ray_cast.get_collision_point()
		
		hit.stop_point = hit.surface_position + hit.surface_normal * stat_track_collision
		hit.surface_type = other.object_type
		var seg := p1 - p0
		var seg_len := seg.length()
		var hit_seg : Vector3 = hit.stop_point - p0
		var hit_len := hit_seg.length()
		hit.hit_time = minf(1.0, hit_len / seg_len)
		out_hits.append(hit)
	return out_hits

func prepare_machine_frame():
	
	if (machine_state & FZ_MS.STARTINGCOUNTDOWN) != 0:
		input_steer_yaw = 0.0
		input_steer_pitch = 0.0
		input_brake = 0.0
		input_strafe = 0.0
		machine_state &= ~(FZ_MS.SIDEATTACKING | FZ_MS.JUST_PRESSED_BOOST | FZ_MS.SPINATTACKING)

	var old_terrain_state = terrain_state

	
	machine_state &= ~(FZ_MS.DIEDTHISFRAMEOOB_Q | FZ_MS.JUST_HIT_DASHPLATE | FZ_MS.RACEJUSTBEGAN_Q | FZ_MS.JUSTTAPPEDACCEL |
		FZ_MS.CROSSEDLAPLINE_Q | FZ_MS.JUSTLANDED | FZ_MS.AIRBORNEMORE0_2S_Q | FZ_MS.AIRBORNE)
	state_2 &= 0xfffffcff
	terrain_state = 0

	if (machine_state & FZ_MS.B29) == 0:
		set_terrain_state_from_track()

	if (old_terrain_state & FZ_TERRAIN.DASHPLATE) != 0:
		machine_state &= ~FZ_MS.JUST_HIT_DASHPLATE
	DebugDraw2D.set_text("JUST_HIT_DASHPLATE", machine_state & FZ_MS.JUST_HIT_DASHPLATE)

	copy_mtx_to_mtxa(basis_physical_other)
	mtxa.origin = position_old

	basis_physical_other = basis_physical
	position_old_dupe = position_current
	position_old = position_old_dupe

	for i in range(tilt_corners.size()):
		var new_pos = tilt_corners[i].offset
		new_pos.y = new_pos.y + tilt_corners[i].force
		new_pos.y = new_pos.y - tilt_corners[i].rest_length
		tilt_corners[i].pos_old = mtxa_transform_point(new_pos)
		if (tilt_corners[i].state & MachineTiltCorner.FZ_TC.DRIFT) != 0:
			for c in tilt_corners:
				c.state |= MachineTiltCorner.FZ_TC.DRIFT

	copy_mtx_to_mtxa(basis_physical)
	mtxa.origin = position_current

	var ground_normal = Vector3.UP
	if (machine_state & FZ_MS.ACTIVE) != 0:
		ground_normal = get_avg_track_normal_from_tilt_corners()

	var all_airborne = true
	for i in range(tilt_corners.size()):
		if (tilt_corners[i].state & MachineTiltCorner.FZ_TC.AIRBORNE) == 0:
			all_airborne = false
		wall_corners[i].pos_a = position_current

	if all_airborne:
		machine_state |= FZ_MS.AIRBORNE
		air_time += 1
		if air_time > 10:
			machine_state |= FZ_MS.AIRBORNEMORE0_2S_Q
	else:
		if air_time != 0:
			machine_state |= FZ_MS.JUSTLANDED
		air_time = 0
		machine_state &= ~FZ_MS.AIRBORNEMORE0_2S_Q
		state_2 &= ~2
	
	turning_related = 0.0
	visual_rotation.z *= 0.8
	visual_rotation.x *= 0.9
	
	if (machine_state & FZ_MS.ACTIVE) != 0:
		if frames_since_start_2 != 0:
			frames_since_start_2 = mini(255, frames_since_start_2 + 1)
	
	if ((machine_state & FZ_MS.COMPLETEDRACE_1_Q) != 0) or (terrain_state & FZ_TERRAIN.RECHARGE) != 0:
		energy += 1.111111
		recharge_particles.emitting = true
		if energy > car_definition.max_energy:
			energy = car_definition.max_energy
	else:
		recharge_particles.emitting = false
	
	var vel_mag = velocity.length()
	speed_kmh = 216.0 * (vel_mag / max(stat_weight, 0.001))
	
	if ((machine_state & FZ_MS.RETIRED) != 0) and ((machine_state & FZ_MS.AIRBORNE) == 0):
		if speed_kmh >= 10.0:
			velocity *= 0.9
		else:
			velocity = Vector3.ZERO
	
	handle_attack_states()
	
	if car_hit_invincibility == 0:
		if (machine_state & FZ_MS.JUSTHITVEHICLE_Q) != 0:
			car_hit_invincibility = 6
	else:
		car_hit_invincibility -= 1
	
	velocity_local = mtxa_inverse_rotate_point(velocity)
	mtxa_push()
	var steer = -(input_steer_yaw * stat_turn_reaction + input_strafe * stat_strafe)
	steer = clamp(steer, -45.0, 45.0)
	mtxa_rotate_about_y(deg_to_rad(steer))
	velocity_local_flattened_and_rotated = mtxa_inverse_rotate_point(velocity)
	velocity_local_flattened_and_rotated.y = 0.0
	mtxa_pop()
	
	position_old_2 = position_current
	
	frames_since_start += 1
	
	return ground_normal

func get_current_stage_min_y() -> float:
	return -1000000000000.0 



func handle_machine_damage_and_visuals() -> void: 
	if (state_2 & 0x8) != 0: 
		copy_mtx_to_mtxa(basis_physical) 
		mtxa.origin = position_current
		if (terrain_state & FZ_TERRAIN.LAVA) != 0: 
			pass 
			if (state_2 & 0x200) != 0 and (machine_state & FZ_MS.ZEROHP) != 0: 
				pass 
				return 
		copy_mtx_to_mtxa(basis_physical)
		mtxa.origin = position_current
		for i in range(tilt_corners.size()): 
			var current_tilt_corner = tilt_corners[i]
			var current_wall_corner = wall_corners[i]
			current_tilt_corner.pos_old = current_tilt_corner.pos
			var local_y_offset_suspended = current_tilt_corner.offset.y + \
											current_tilt_corner.force - \
											current_tilt_corner.rest_length
			var local_pos_for_transform = Vector3(
				current_tilt_corner.offset.x,
				local_y_offset_suspended,
				current_tilt_corner.offset.z
			)
			current_tilt_corner.pos = mtxa_transform_point(local_pos_for_transform)
			current_wall_corner.pos_a = current_wall_corner.pos_b
			current_wall_corner.pos_b = mtxa_transform_point(current_wall_corner.offset)
		if not (state_2 & 0x10): 
			var y_pos = position_current.y
			var track_min_y = -1000000
			if y_pos < -5000.0 or y_pos < (track_min_y - 900.0):
				pass 
				return
		if position_current.y < -10000.0:
			position_current.y = -10000.0
			velocity = Vector3.ZERO
		create_machine_visual_transform()
		if (machine_state & FZ_MS.STARTINGCOUNTDOWN) == 0:
			var world_speed = velocity.length()
			if absf(stat_weight) > 0.0001:
				speed_kmh = 216.0 * (world_speed / stat_weight)
			else:
				speed_kmh = 0.0
			var current_speed_for_max_check = speed_kmh
			var no_bad_state_flags = (machine_state & \
				(FZ_MS.JUSTHITVEHICLE_Q | FZ_MS.LOWGRIP | FZ_MS.TOOKDAMAGE)) == 0

func find_floor_beneath_machine() -> bool:
	var p0_sweep_start_ws = mtxa_transform_point(Vector3(0.0, 1.0, 0.0))
	var p1_sweep_end_ws = mtxa_transform_point(Vector3(0.0, -200.0, 0.0))
	position_bottom = p1_sweep_end_ws
	var hit_results : Array[Dictionary] = raycast_world(
		p0_sweep_start_ws,
		position_bottom,
		1
	)
	var sweep_hit_occurred : bool = not hit_results.is_empty()
	var contact_dist_metric : float = 0.0
	var first_hit_data
	var current_stage := MXGlobal.currentStage
	var checkpoint_respawns := current_stage.checkpoint_respawns
	var cur_cp := checkpoint_respawns[current_checkpoint]
	if not sweep_hit_occurred:
		contact_dist_metric = 0.0
	else:
		first_hit_data = hit_results[0]
		track_surface_pos = first_hit_data.surface_position
		var dist_p0_to_surface = mtxa.origin.distance_to(first_hit_data.surface_position)
		contact_dist_metric = 20.0 - dist_p0_to_surface
	if (contact_dist_metric > 0.0 or cur_cp.reset_gravity == false) and sweep_hit_occurred:
		track_surface_normal = first_hit_data.surface_normal
		height_above_track = contact_dist_metric
		return true
	else:
		track_surface_normal = Vector3.UP
		position_bottom = p1_sweep_end_ws
		height_above_track = 0.0
		return false

func handle_steering() -> void:
	if (machine_state & FZ_MS.ACTIVE) == 0:
		return
	var strafe_turn_mod := 1.0
	for corner in tilt_corners:
		if (corner.state & MachineTiltCorner.FZ_TC.DRIFT) != 0:
			strafe_turn_mod -= 0.25
	var steer_strength := (stat_turn_movement + strafe_turn_mod * stat_strafe_turn * input_strafe * input_steer_yaw) * -input_steer_yaw
	if (machine_state & FZ_MS.SIDEATTACKING) != 0:
		steer_strength *= 0.3
	velocity_angular.y += (1.5 * steer_strength)
	if absf(velocity_angular.y) < 1.0:
		velocity_angular.y = 0
	input_yaw_dupe = input_steer_yaw

func set_flag_on_all_tilt_corners(in_flag : MachineTiltCorner.FZ_TC) -> void:
	for corner in tilt_corners:
		corner.state = corner.state | in_flag

func remove_flag_on_all_tilt_corners(in_flag : MachineTiltCorner.FZ_TC) -> void:
	for corner in tilt_corners:
		corner.state = corner.state & ~in_flag

func handle_suspension_states() -> void:
	if grip_frames_from_accel_press != 0:
		grip_frames_from_accel_press -= 1
	if (machine_state & FZ_MS.AIRBORNE) == 0:
		if 0.1 < base_speed:
			if (machine_state & FZ_MS.B14) == 0:
				var should_drift := false
				if (machine_state & FZ_MS.MANUAL_DRIFT) != 0:
					if 0.1 < absf(input_steer_yaw):
						should_drift = true
				if (machine_state & FZ_MS.SPINATTACKING) != 0:
					should_drift = true
				if should_drift:
					set_flag_on_all_tilt_corners(MachineTiltCorner.FZ_TC.DRIFT)
			else:
				remove_flag_on_all_tilt_corners(MachineTiltCorner.FZ_TC.DRIFT)
				grip_frames_from_accel_press = stat_accel_press_grip_frames
	else:
		remove_flag_on_all_tilt_corners(MachineTiltCorner.FZ_TC.DRIFT)
	if (machine_state & FZ_MS.STRAFING) != 0 and absf(input_steer_yaw) < 0.1:
		machine_state = machine_state & ~FZ_MS.STRAFING
	if 0.3 < absf(input_strafe):
		machine_state = machine_state | FZ_MS.STRAFING
	if (machine_state & FZ_MS.STRAFING) == 0:
		return
	set_flag_on_all_tilt_corners(MachineTiltCorner.FZ_TC.STRAFING)

func handle_machine_turn_and_strafe(tilt_corner: MachineTiltCorner, in_angle_vel: float) -> void:
	# ───────────────────── Corner movement & steering matrix ─────────────────────
	var corner_delta: Vector3 = tilt_corner.pos_old - tilt_corner.pos
	
	var is_drifting := (tilt_corner.state & MachineTiltCorner.FZ_TC.DRIFT) != 0
	var is_strafing := (tilt_corner.state & MachineTiltCorner.FZ_TC.STRAFING) != 0
	
	mtxa_push()
	
	var steer_deg := clampf(-(input_steer_yaw * stat_turn_reaction + input_strafe * stat_strafe), -45.0, 45.0)
	mtxa_rotate_about_y(deg_to_rad(steer_deg * 0.5))
	
	corner_delta = mtxa_inverse_rotate_point(corner_delta)
	var corner_dist := corner_delta.length()
	var speed_factor := (216.0 * corner_dist) / 1000.0	# 0 → 1 range
	
	# ───────────────────── Grip / drift threshold ─────────────────────
	var grip_threshold: float
	if (not is_drifting and is_strafing) or grip_frames_from_accel_press != 0:
		grip_threshold = 20.0
	else:
		var base_grip := stat_grip_1
		grip_threshold = base_grip
		if (state_2 & 4) == 0:
			if is_drifting and brake_timer == 0:
				grip_threshold = stat_grip_3
		else:
			if is_drifting and brake_timer < 30:
				grip_threshold = stat_grip_3 if base_grip >= stat_grip_3 else base_grip
	
	if absf(corner_delta.x) < stat_grip_3:
		tilt_corner.state &= ~MachineTiltCorner.FZ_TC.DRIFT
	
	var drift_allowed := true
	if not is_drifting and absf(input_steer_yaw) <= 0.7:
		drift_allowed = false
	
	var lateral_delta := corner_delta.x
	var drift_delta := lateral_delta
	
	if absf(lateral_delta) <= grip_threshold or not drift_allowed:
		if absf(lateral_delta) < 1.1920929e-7:
			drift_delta = 0.0
		tilt_corner.state &= ~MachineTiltCorner.FZ_TC.DRIFT
	else:
		tilt_corner.state |= MachineTiltCorner.FZ_TC.DRIFT
		drift_delta = -grip_threshold if lateral_delta < 0.0 else grip_threshold
	
	# ───────────────────── Global state modifiers ─────────────────────
	if machine_state & (FZ_MS.JUSTHITVEHICLE_Q | FZ_MS.LOWGRIP | FZ_MS.TOOKDAMAGE | FZ_MS.SIDEATTACKING):
		drift_delta = 0.0
	
	if machine_state & FZ_MS.RETIRED:
		drift_delta *= 0.2
	elif machine_state & FZ_MS.ZEROHP:
		var fade := clampf(0.01 * (float(frames_since_death) - 4.0), 0.0, 0.05)
		drift_delta *= fade
	
	# ───────────────────── Force computation ─────────────────────
	if drift_delta != 0.0:
		var turn_tension := stat_turn_tension
		var weighted_delta := drift_delta * stat_weight
		var applied_force: float
		
		if turn_tension >= 0.1 or grip_frames_from_accel_press != 0:
			applied_force = weighted_delta * turn_tension
		elif ((tilt_corner.state & 2) == 0) and ((machine_state & FZ_MS.JUST_PRESSED_BOOST) == 0):
			var rail_timer := float(rail_collision_timer)
			var speed_lerp := clampf(speed_factor, 0.2, 0.8)
			var steer_scale := 0.0
			if (tilt_corner.state & 4) == 0:
				steer_scale = ((speed_lerp - 0.2) / 0.6) * (turn_tension - 0.1) * (0.3 + 0.7 * absf(input_steer_yaw))
			applied_force = weighted_delta * (0.1 + steer_scale * (1.0 - rail_timer / 20.0))
		else:
			applied_force = weighted_delta * 0.1
		
		if terrain_state & FZ_TERRAIN.ICE:
			applied_force *= 0.003
		elif terrain_state & FZ_TERRAIN.DIRT:
			applied_force *= 2.0
		
		var local_force := Vector3(applied_force, 0.0, 0.0)	# forward‑axis only
		var world_force := mtxa_rotate_point(local_force)
		tilt_corner.force_spatial += world_force
		
		if (tilt_corner.state & 4) != 0:
			applied_force *= 0.6
		turning_related += applied_force
	
	# ───────────────────── Apply forces & torque ─────────────────────
	mtxa_pop()
	
	velocity += tilt_corner.force_spatial
	
	if rail_collision_timer < 6:
		apply_torque_from_force(tilt_corner.offset, tilt_corner.force_spatial)
	
	if is_drifting and (machine_state & FZ_MS.JUSTHITVEHICLE_Q) == 0:
		in_angle_vel *= stat_grip_2
	
	velocity_angular.y -= 0.125 * in_angle_vel

func handle_linear_velocity() -> void:
	var vel_flat_rot_x: float = velocity_local_flattened_and_rotated.x
	var vel_flat_rot_y: float = velocity_local_flattened_and_rotated.y
	var vel_flat_rot_z: float = velocity_local_flattened_and_rotated.z
	
	var neg_local_fwd_speed: float = -velocity_local.z
	var abs_local_lat_speed: float = absf(velocity_local.x)
	
	var mag_vel_flat_rot: float = velocity_local_flattened_and_rotated.length()

	var drift_accel_component: float = 0.0
	if (machine_state & (FZ_MS.JUSTHITVEHICLE_Q | FZ_MS.LOWGRIP | FZ_MS.TOOKDAMAGE)) == 0 and mag_vel_flat_rot > (10.0 * stat_weight) / 216.0:
		
		var norm_z_vel_flat_rot: float = 0.0
		if mag_vel_flat_rot > 0.0001:
			norm_z_vel_flat_rot = vel_flat_rot_z / mag_vel_flat_rot
		
		var drift_factor = 1.0 - (norm_z_vel_flat_rot * norm_z_vel_flat_rot)
		drift_accel_component = drift_factor * stat_drift_accel
		drift_accel_component = min(drift_accel_component, 1.0)

	var net_fwd_accel: float = handle_machine_accel_and_boost(
		neg_local_fwd_speed, 
		abs_local_lat_speed, 
		drift_accel_component
	)
	
	var broken_factor: float = 1.0 
	var overall_damping: float = 0.6 + 0.55
	overall_damping = min(overall_damping, 1.0)

	net_fwd_accel *= overall_damping
	velocity *= overall_damping 

	if (machine_state & FZ_MS.BOOSTING) == 0:
		visual_rotation.x += 0.25 * net_fwd_accel
	else:
		visual_rotation.x += 0.05 * net_fwd_accel

	var airborne_factor: float = 1.0
	if (machine_state & FZ_MS.AIRBORNE) != 0:
		var machine_up_vector_ws = mtxa.basis.z
		var dot_prod_up_with_track_normal = machine_up_vector_ws.dot(track_surface_normal)
		
		var alignment_factor = 3.4 * (0.3 + dot_prod_up_with_track_normal)
		alignment_factor = clampf(alignment_factor, 0.0, 1.0)
		airborne_factor = alignment_factor * alignment_factor
	
	mtxa_push()

	var effective_steer_degrees: float = -(input_steer_yaw * stat_turn_reaction + input_strafe * stat_strafe)
	if (machine_state & FZ_MS.SIDEATTACKING) != 0:
		effective_steer_degrees = 0.0
	effective_steer_degrees = clampf(effective_steer_degrees, -45.0, 45.0)
	
	turn_reaction_input = 0.75 * -(input_steer_yaw * stat_turn_reaction)
	mtxa_rotate_about_y(deg_to_rad(effective_steer_degrees))

	var local_thrust_vector = Vector3(0.0, 0.0, -(net_fwd_accel * airborne_factor))
	
	var world_thrust_vector = mtxa_rotate_point(local_thrust_vector)
	velocity += world_thrust_vector
	mtxa_pop()
	var current_world_speed: float = velocity.length()
	
	if current_world_speed / stat_weight > (1.0 / 1.08):
		if side_attack_delay == 6:
			var speed_cap_for_dash = (50.0 / 9.0) * stat_weight
			var clamped_speed_for_dash = min(current_world_speed, speed_cap_for_dash)
			
			var local_dash_vector = Vector3(side_attack_indicator * clamped_speed_for_dash, 0.0, 0.0)
			var world_dash_vector = mtxa_rotate_point(local_dash_vector)
			velocity += world_dash_vector
		if ((terrain_state & FZ_TERRAIN.JUMP) != 0 and (machine_state & FZ_MS.AIRBORNE) == 0):
			var local_jump_boost = Vector3(0.0, 1.13 * current_world_speed, 0.0)
			var world_jump_boost = mtxa_rotate_point(local_jump_boost)
			
			velocity += world_jump_boost
			state_2 |= 2
			velocity_angular.x = 0.0
			velocity_angular.z = 0.0
			
	input_strafe_1_6 = input_strafe_32 / 20.0
	input_strafe_32 += (8.0 * input_strafe - 5.0 * input_strafe_1_6)

func handle_machine_accel_and_boost(
		neg_local_fwd_speed: float, 
		abs_local_lateral_speed: float, 
		drift_accel_factor: float
	) -> float:

	var effective_accel_input: float = 0.0 
	var final_thrust_output: float 
	
	if not ((machine_state & FZ_MS.ZEROHP) != 0 and frames_since_death <= 0x77): 
		effective_accel_input = input_accel 
		if (state_2 & 4) == 0: 
			if effective_accel_input < 0.0 or input_brake > 0.0:
				effective_accel_input = 0.0
		elif effective_accel_input < 0.0 or brake_timer > 0x1d: 
			effective_accel_input = 0.0
		if (machine_state & FZ_MS.ACTIVE) == 0 and effective_accel_input < 0.3:
			effective_accel_input = 0.0
	if effective_accel_input <= 0.0001: 
		if race_start_charge <= 0.0: 
			if (machine_state & FZ_MS.STARTINGCOUNTDOWN) != 0:
				base_speed = 0.0 
		else:
			race_start_charge -= 2.0
			if race_start_charge < 0.0:
				race_start_charge = 0.0
			if (machine_state & FZ_MS.STARTINGCOUNTDOWN) == 0:
				base_speed = 0.0
	else: 
		if (machine_state & FZ_MS.ACTIVE) == 0:
			machine_state |= FZ_MS.ACTIVE
			frames_since_start_2 = 1 
		
		if (machine_state & FZ_MS.STARTINGCOUNTDOWN) == 0:
			if race_start_charge > 0.0:
				base_speed = 1.0
				machine_state |= (FZ_MS.RACEJUSTBEGAN_Q | FZ_MS.JUSTTAPPEDACCEL)
				race_start_charge = 0.0
		else: 
			race_start_charge += effective_accel_input
	if (machine_state & FZ_MS.STARTINGCOUNTDOWN) == 0:
		var current_machine_state = machine_state 
		var normalized_fwd_speed = neg_local_fwd_speed / stat_weight
		
		if boost_delay_frame_counter != 0:
			machine_state &= ~FZ_MS.JUST_PRESSED_BOOST
			boost_delay_frame_counter -= 1
		
		if (current_machine_state & FZ_MS.JUST_PRESSED_BOOST) != 0:
			if boost_delay_frame_counter == 0:
				boost_delay_frame_counter = 6
			else:
				boost_delay_frame_counter += 1
		current_machine_state = machine_state
		if (current_machine_state & FZ_MS.JUST_HIT_DASHPLATE) == 0:
			if boost_frames == 0: 
				var can_manual_boost = (current_machine_state & FZ_MS.JUST_PRESSED_BOOST) != 0 and \
															 energy > 1.0 and \
															 effective_accel_input > 0.0
				if not can_manual_boost:
					machine_state &= ~(FZ_MS.BOOSTING_DASHPLATE | FZ_MS.JUST_PRESSED_BOOST | FZ_MS.BOOSTING)
					boost_turbo -= (4.0 + 0.5 * boost_turbo) / 60.0
				else:
					var boost_strength_factor = 1.0 - boost_turbo / (9.0 * stat_boost_strength)

					var min_boost_strength_factor = 0.2
					var boost_duration_frames = int(60.0 * stat_boost_length)
					boost_frames = boost_duration_frames
					boost_frames_manual = boost_duration_frames
					machine_state |= FZ_MS.BOOSTING
					machine_state &= ~FZ_MS.BOOSTING_DASHPLATE

					boost_strength_factor = max(boost_strength_factor, min_boost_strength_factor)
					boost_turbo += stat_boost_strength * boost_strength_factor
			else:
				machine_state &= ~FZ_MS.JUST_PRESSED_BOOST
				machine_state |= FZ_MS.BOOSTING
		else:
			var boost_strength_factor = 1.0 - boost_turbo / (9.0 * stat_boost_strength)
			var target_dash_boost_frames = int(0.5 * 60.0 * stat_boost_length)

			if boost_frames < target_dash_boost_frames:
				boost_frames = target_dash_boost_frames
			
			var min_boost_strength_factor = 0.2
			machine_state &= ~FZ_MS.JUST_PRESSED_BOOST
			machine_state |= FZ_MS.BOOSTING

			boost_strength_factor = max(boost_strength_factor, min_boost_strength_factor)
			boost_turbo += (2.0 * stat_boost_strength) * boost_strength_factor
		
		if (machine_state & FZ_MS.SPINATTACKING) == 0:
			boost_turbo -= (2.0 + 0.5 * boost_turbo) / 60.0
		else:
			effective_accel_input *= 0.8
			boost_turbo -= (3.0 + 0.5 * boost_turbo) / 60.0
		boost_turbo = max(boost_turbo, 0.0)

		if (machine_state & FZ_MS.BOOSTING) != 0:
			if boost_frames_manual > 0:
				energy -= 0.1666666667 * boost_energy_use_mult
				boost_frames_manual -= 1
			
			boost_frames -= 1
			if boost_frames == 0 and speed_kmh > 1200.0:
				var cooldown_duration = (speed_kmh - 1200.0) / 60.0
				cooldown_duration = min(cooldown_duration, 10.0)
				if float(boost_delay_frame_counter) < cooldown_duration:
					boost_delay_frame_counter = int(cooldown_duration)
			
			if energy < 0.01:
				energy = 0.01
				boost_frames_manual = 0
				if (machine_state & FZ_MS.BOOSTING_DASHPLATE) == 0:
					boost_frames = 0
				else:
					var half_dash_boost_frames = int(0.5 * 60.0 * stat_boost_length)
					if half_dash_boost_frames < boost_frames:
						boost_frames = half_dash_boost_frames
			if boost_frames <= 0:
				boost_frames = 0
				machine_state &= ~FZ_MS.BOOSTING

		var accel_stat_scaled = 40.0 * stat_acceleration
		var target_speed_component = (effective_accel_input * accel_stat_scaled) / 348.0 + base_speed
		var speed_difference = target_speed_component - normalized_fwd_speed

		var speed_factor_denom = 36.0 + 40.0 * stat_max_speed + boost_turbo * 2.0
		var speed_factor = 0.0
		if absf(speed_factor_denom) > 0.0001:
			speed_factor = target_speed_component / speed_factor_denom
		speed_factor = max(speed_factor, 0.0)

		var current_accel_magnitude = speed_factor * 4.0 * (stat_acceleration * (0.6 + stat_acceleration))

		if not (machine_state & (FZ_MS.JUST_HIT_DASHPLATE | FZ_MS.JUST_PRESSED_BOOST)):
			if (machine_state & FZ_MS.BOOSTING) != 0:
				current_accel_magnitude *= 0.3 if stat_weight <= 1000.0 else 0.5
		else:
			current_accel_magnitude = 0.0

		if speed_difference > 0.0 and \
			 (normalized_fwd_speed < 0.0 or (terrain_state & FZ_TERRAIN.DIRT) != 0):
			current_accel_magnitude *= 5.0

		var final_accel_term = (1.0 - drift_accel_factor) * (
			(speed_difference * current_accel_magnitude) +
			((abs_local_lateral_speed * stat_acceleration) / stat_weight) * stat_turn_decel
		)

		if input_accel < 1.0:
			final_accel_term *= (0.05 + 0.95 * input_accel)
		
		base_speed = target_speed_component - final_accel_term

		if input_brake <= 0.0001:
			brake_timer = 0
		elif brake_timer < 0x1e:
			brake_timer += 1
		
		var brake_effect = 0.0
		if (state_2 & 4) == 0:
			brake_effect = input_brake * (0.5 * current_accel_magnitude)
		elif brake_timer > 0xe:
			brake_effect = input_brake * (0.5 * current_accel_magnitude)
		
		brake_effect = min(brake_effect, 0.12)
		base_speed = max(base_speed - brake_effect, 0.0)

		base_speed = max(base_speed - stat_drag, 0.0)
		
		var final_output_thrust_factor = speed_difference
		if brake_effect <= 0.0:
			var modifier = 0.3
			if (machine_state & FZ_MS.B14) != 0:
				modifier = 1.0
			
			if normalized_fwd_speed < 0.0 or final_output_thrust_factor < 0.0 :
				final_output_thrust_factor *= (0.5 * modifier)
		
		if (machine_state & FZ_MS.ZEROHP) != 0:
			var speed_ratio_for_0hp = min(speed_kmh / 100.0, 1.0)
			final_output_thrust_factor *= (0.2 - 0.15 * speed_ratio_for_0hp)

		if (machine_state & (FZ_MS.BOOSTING_DASHPLATE | FZ_MS.BOOSTING)) == 0:
			final_thrust_output = 1000.0 * final_output_thrust_factor
		elif stat_weight <= 1000.0:
			final_thrust_output = 1200.0 * final_output_thrust_factor
		else:
			final_thrust_output = 1600.0 * final_output_thrust_factor
			
	else:
		final_thrust_output = -neg_local_fwd_speed
		base_speed = 0.014 * race_start_charge

	if (machine_state & FZ_MS.ZEROHP) != 0 and frames_since_death <= 0x77:
		if brake_timer < 0x3d:
			brake_timer += 1
		else:
			input_accel = 0.0
			input_brake = 0.0001
		final_thrust_output = 0.0

	return final_thrust_output

func handle_angle_velocity() -> void:
	var weight_val : float = 0.99
	if (machine_state & FZ_MS.AIRBORNE) == 0:
		if (machine_state & FZ_MS.JUSTLANDED) == 0:
			weight_val = 0.05 * weight_derived_2
		else:
			weight_val = 0.2 * weight_derived_2
	else:
		velocity_angular.x *= 0.9
		velocity_angular.z *= weight_val
		weight_val = weight_derived_2
	velocity_angular.y = clampf(velocity_angular.y, -weight_val, weight_val)

func handle_airborne_controls() -> void:
	var min_air_tilt: float = -50.0
	var max_air_tilt: float = 60.0
	var airborne_controls_active: bool = false
	
	if frames_since_start_2 > 60 and (machine_state & FZ_MS.AIRBORNE) != 0:
		airborne_controls_active = true
	
	if airborne_controls_active:
		var tilt_effect_base: float = 2.0 * absf(input_steer_yaw)
		
		if (state_2 & 0x2) != 0:
			tilt_effect_base = 0.0
		
		var current_tilt_increment: float
		if tilt_effect_base >= 0.1:
			current_tilt_increment = tilt_effect_base + 2.0 * input_steer_pitch * absf(2.0 - tilt_effect_base)
			if (machine_state & FZ_MS.BOOSTING) != 0 and not (machine_state & FZ_MS.BOOSTING_DASHPLATE):
				current_tilt_increment *= 2.0
		else:
			current_tilt_increment = tilt_effect_base + 4.0 * input_steer_pitch
		
		if air_time > 60:
			var air_time_factor = float(air_time - 60) / 120.0
			air_time_factor = minf(air_time_factor, 1.0)
			
			current_tilt_increment = current_tilt_increment * (1.0 + 0.3 * air_time_factor) + (0.3 * air_time_factor)
		
		air_tilt += current_tilt_increment
		air_tilt = clampf(air_tilt, min_air_tilt, max_air_tilt)
	else:
		air_tilt = 0.0

func orient_vehicle_from_gravity_or_road() -> void:
	var dVar5_factor = 1.5 + stat_weight / 4000.0
	if dVar5_factor >= 1.8:
		dVar5_factor = min(dVar5_factor, 2.0)
	else:
		dVar5_factor = 3.6 - dVar5_factor

	var current_stage := MXGlobal.currentStage
	var checkpoint_respawns := current_stage.checkpoint_respawns
	var cur_cp := checkpoint_respawns[current_checkpoint]
	
	var local_a0_base_factor: float
	if (machine_state & FZ_MS.AIRBORNE) == 0:
		local_a0_base_factor = dVar5_factor * 1.3
	elif height_above_track <= 0.0:
		local_a0_base_factor = dVar5_factor * 0.6
	else:
		if (machine_state & FZ_MS.B10) == 0:
			local_a0_base_factor = dVar5_factor * 1.3
		else:
			local_a0_base_factor = dVar5_factor * 1.8
	
	var local_a0_force_magnitude = 10.0 * -(0.009 * stat_weight) * local_a0_base_factor

	var gravity_align_force = track_surface_normal * local_a0_force_magnitude
	velocity += gravity_align_force
	
	basis_physical = basis_physical.orthonormalized()
	copy_mtx_to_mtxa(basis_physical)

	if (machine_state & FZ_MS.AIRBORNE) == 0:
		var machine_world_up = mtxa.basis.y

		var dot_up_vs_track_normal = 0.0
		var safe_track_normal_gnd = _normalized_safe(track_surface_normal)
		if machine_world_up.length_squared() > 0.0001:
			dot_up_vs_track_normal = machine_world_up.dot(safe_track_normal_gnd)

		if dot_up_vs_track_normal < 0.7:
			var alignment_factor = 0.0
			if dot_up_vs_track_normal >= 0.0:
				alignment_factor = dot_up_vs_track_normal / 0.7
			
			var rotation_angle_degrees = 40.0 * (1.0 - alignment_factor)
			var rotation_angle_rad = deg_to_rad(rotation_angle_degrees)
			
			var rotation_axis = machine_world_up.cross(safe_track_normal_gnd)
			if rotation_axis.length_squared() > 0.0001: 
				var quat_rotation_ground = Quaternion(rotation_axis.normalized(), rotation_angle_rad)
				
				var T_old_basis_ground = mtxa.basis
				mtxa_from_quat(quat_rotation_ground)
				mtxa_multiply_mtx(Transform3D(T_old_basis_ground, Vector3.ZERO))
		
		basis_physical = mtxa.basis

	else:
		var air_tilt_rad = deg_to_rad(air_tilt) 
		var cos_air_tilt = cos(air_tilt_rad)
		var sin_air_tilt = sin(air_tilt_rad)

		var local_tilted_up_vec = Vector3(0.0, cos_air_tilt, sin_air_tilt)
		var world_tilted_up = mtxa_rotate_point(local_tilted_up_vec)

		var dot_tilted_up_vs_track_normal = 0.0
		var safe_world_tilted_up = _normalized_safe(world_tilted_up, Vector3.UP)
		var safe_track_normal_air = _normalized_safe(track_surface_normal, Vector3.UP)
		dot_tilted_up_vs_track_normal = safe_world_tilted_up.dot(safe_track_normal_air)

		if dot_tilted_up_vs_track_normal < 0.992:
			var adjusted_dot = dot_tilted_up_vs_track_normal + 0.008
			var base_rotation_strength_deg = 15.0

			var rotation_axis_airborne = safe_world_tilted_up.cross(safe_track_normal_air)
			
			var axis_len_sq_threshold = 0.1 * 0.1
			if rotation_axis_airborne.length_squared() < axis_len_sq_threshold or adjusted_dot < 0.008:
				var current_machine_world_up = _normalized_safe(mtxa.basis.y, Vector3.UP)
				var dot_world_up_vs_track_normal = current_machine_world_up.dot(safe_track_normal_air)

				if dot_world_up_vs_track_normal <= 0.0:
					var machine_world_x_axis = _normalized_safe(mtxa.basis.x, Vector3.RIGHT)
					rotation_axis_airborne = mtxa.basis.z
					
					var dot_track_normal_vs_machine_x = safe_track_normal_air.dot(machine_world_x_axis)
					if dot_track_normal_vs_machine_x > 0.0:
						rotation_axis_airborne = -rotation_axis_airborne

			if rotation_axis_airborne.length_squared() > 0.0001:
				var axis_norm = rotation_axis_airborne.normalized()
				var squared_dot_factor = max(0.0, adjusted_dot * adjusted_dot)
				var rotation_angle_degrees_air = base_rotation_strength_deg * (1.0 - squared_dot_factor)
				var angle_rad_airborne = deg_to_rad(rotation_angle_degrees_air)
				
				var quat_rotation_air = Quaternion(axis_norm, angle_rad_airborne)
				
				var T_old_basis_air = mtxa.basis
				mtxa_from_quat(quat_rotation_air)
				mtxa_multiply_mtx(Transform3D(T_old_basis_air, Vector3.ZERO)) 
		
		basis_physical = mtxa.basis

func _normalized_safe(vec: Vector3, default_vec := Vector3.ZERO) -> Vector3:
	if vec.length_squared() > 0.000001: 
		return vec.normalized()
	return default_vec


	
	
	
	
	
		
			
			
			
			
			
			
			
			
			
				
					
				
					
					
					
						
					
			
			
			
			
			
				
				
			
			
				
					
				
					
			
				
			
				
			
			
			
			
		
			
	
		
		

func handle_drag_and_glide_forces() -> void:
	var speed := velocity.length()
	var speed_weight_ratio := speed / stat_weight
	var scaled_speed := 216.0 * speed_weight_ratio

	if scaled_speed < 2.0:
		velocity = Vector3.ZERO
		visual_shake_mult = 0.0
		return

	if scaled_speed > 9990.0:
		velocity = set_vec3_length(velocity, 46.25)
		return

	# ───────────────────── Basic directional figures ─────────────────────
	var alignment_with_normal := track_surface_normal.dot(velocity.normalized())
	var forward_world := mtxa_rotate_point(Vector3(0, 0, -1)).normalized()
	var forward_normal_alignment := track_surface_normal.dot(forward_world)

	# ───────────────────── Normal component & base drag magnitude ─────────────────────
	var normal_force := track_surface_normal * (stat_weight * alignment_with_normal * speed_weight_ratio)
	var base_drag_mag := speed_weight_ratio * speed_weight_ratio * 8.0
	var drag_vector := velocity - normal_force

	# ───────────────────── Extra handling while airborne ─────────────────────
	if machine_state & FZ_MS.AIRBORNE:
		if forward_normal_alignment < 0.0:
			base_drag_mag *= max(0.0, 1.0 + forward_normal_alignment)
		forward_normal_alignment += 1.0	# shift into 0 → 2 range

	# Match drag_vector length to the computed magnitude
	drag_vector = set_vec3_length(drag_vector, base_drag_mag)
	visual_shake_mult = base_drag_mag

	# ───────────────────── Weight scaling ─────────────────────
	if stat_weight < 1100.0:
		var weight_scale := stat_weight / 1100.0
		alignment_with_normal *= weight_scale * weight_scale

	# ───────────────────── Drag coefficient (boost & airborne modifiers) ─────────────────────
	var boosting := (machine_state & FZ_MS.BOOSTING) != 0
	var airborne := (machine_state & FZ_MS.AIRBORNE) != 0
	var drag_coeff: float

	if boosting:
		drag_coeff = alignment_with_normal * 0.5
	elif airborne:
		if alignment_with_normal >= 0.0 or forward_normal_alignment <= 0.8:
			drag_coeff = alignment_with_normal * 0.6
		else:
			drag_coeff = alignment_with_normal * (0.6 + 4.0 * (forward_normal_alignment - 0.8))
	else:
		drag_coeff = alignment_with_normal * 0.6

	# Combine lateral drag with drag along the surface normal
	drag_vector += track_surface_normal * (base_drag_mag * drag_coeff)

	# ───────────────────── Post‑death damping ─────────────────────
	if frames_since_death != 0:
		var death_fade := clampf(0.01 * float(frames_since_death - 4), 0.0, 1.0)
		drag_vector *= maxf(1.0, death_fade)

	# ───────────────────── Apply final drag ─────────────────────
	velocity -= drag_vector

#known working
#func handle_drag_and_glide_forces() -> void: 
	#var dVar1 : float
	#var fVar2 : float
	#var uVar3 : int
	#var dVar4 : float
	#var dVar5 : float
	#var dVar6 : float
	#var dVar7 : float
	#var fVar8 : float
	#var fVar9 : float
	#var vStack_98 : Vector3
	#var fStack_8c : Vector3
	#var avStack_80 : Vector3
	#fVar9 = velocity.x
	#fVar8 = velocity.y
	#fVar2 = velocity.z
	#fVar8 = velocity.length()
	#fVar9 = 0.0
	#dVar7 = fVar8 / stat_weight
	#if 2 <= 216 * dVar7:
		#if 216 * dVar7 <= 9990:
			#fVar9 = track_surface_normal.dot(velocity.normalized())
			#dVar5 = fVar9
			#vStack_98 = mtxa_rotate_point(Vector3(0, 0, -1))
			#fVar9 = track_surface_normal.dot(vStack_98.normalized())
			#dVar6 = fVar9
			#fVar9 = stat_weight * dVar5 * dVar7
			#fStack_8c = track_surface_normal * fVar9
			#dVar7 = dVar7 * dVar7 * 8
			#avStack_80 = velocity - fStack_8c
			#if (machine_state & FZ_MS.AIRBORNE) != 0:
				#dVar4 = 0
				#if dVar4 <= dVar6:
					#dVar6 = dVar6 + 1.0
				#else:
					#dVar1 = 1.0 + dVar6
					#dVar6 = dVar1
					#if dVar1 < dVar4:
						#dVar6 = dVar4
					#dVar7 = dVar7 * dVar6
			#avStack_80 = set_vec3_length(avStack_80, dVar7)
			#visual_shake_mult = dVar7
			#if stat_weight < 1100:
				#fVar9 = stat_weight / 1100
				#dVar5 = dVar5 * fVar9 * fVar9
			#uVar3 = machine_state & FZ_MS.BOOSTING
			#if uVar3 == 0 and (machine_state & FZ_MS.AIRBORNE) != 0:
				#if 0 <= dVar5 or dVar6 <= 0.8:
					#dVar6 = dVar5 * 0.6
				#else:
					#dVar6 = dVar5 * (0.6 + 4 * (dVar6 - 0.8))
			#elif uVar3 == 0:
				#dVar6 = dVar5 * 0.6
			#else:
				#dVar6 = dVar5 * 0.5
			#fVar9 = dVar7 * dVar6
			#avStack_80 = track_surface_normal * fVar9 + avStack_80
			#if frames_since_death != 0:
				#fVar9 = clampf(0.01 * float(frames_since_death - 4), 0.0, 1.0)
				#fVar8 = maxf(1.0, fVar9)
				#avStack_80 *= fVar8
			#velocity -= avStack_80
		#else:
			#velocity = set_vec3_length(velocity, 46.25)
	#else:
		#velocity = Vector3.ZERO
		#visual_shake_mult = 0

func rotate_machine_from_angle_velocity() -> void:
	var processed_ang_vel = Vector3.ZERO
	var deadzone_threshold: float = 3.0
	var val_x = velocity_angular.x
	if absf(val_x) <= deadzone_threshold:
		processed_ang_vel.x = 0.0
	else:
		processed_ang_vel.x = val_x - sign(val_x) * deadzone_threshold
	var val_z = velocity_angular.z
	if absf(val_z) <= deadzone_threshold:
		processed_ang_vel.z = 0.0
	else:
		processed_ang_vel.z = val_z - sign(val_z) * deadzone_threshold
	processed_ang_vel.y = velocity_angular.y
	if absf(weight_derived_1) > 0.0001:
		processed_ang_vel.x /= weight_derived_1
	else:
		processed_ang_vel.x = 0.0
	if absf(weight_derived_2) > 0.0001:
		processed_ang_vel.y /= weight_derived_2
	else:
		processed_ang_vel.y = 0.0
	if absf(weight_derived_3) > 0.0001:
		processed_ang_vel.z /= weight_derived_3
	else:
		processed_ang_vel.z = 0.0
	var rotation_angle_rad: float = processed_ang_vel.length()
	if rotation_angle_rad > 0.0001:
		var rotation_axis: Vector3 = processed_ang_vel.normalized()
		var delta_rotation_quaternion = Quaternion(rotation_axis, rotation_angle_rad)
		mtxa_push() 
		mtxa_from_quat(delta_rotation_quaternion)
		var delta_rotation_transform = mtxa
		mtxa_pop()
		mtxa_multiply_mtx(delta_rotation_transform) 

func handle_startup_wobble() -> void:
	var f_val3_for_cross_prod_y: float = 0.0

	var seed_uVar4: int = int(position_current.z) ^ \
												 int(position_current.x) ^ \
												 int(position_current.y) ^ \
												 int(base_speed)

	var intermediate_uint_f1: int = (seed_uVar4 ^ int(velocity_angular.x * 4000000)) & 0xffff
	var normalized_f1: float = float(intermediate_uint_f1) / 65535.0 
	var fVar1_wobble_x: float = 2.0 * normalized_f1 - 1.0 

	var intermediate_uint_f2: int = (seed_uVar4 ^ int(velocity_angular.y * 4000000)) & 0xffff
	var normalized_f2: float = float(intermediate_uint_f2) / 65535.0 
	var fVar2_wobble_y_comp: float = 0.5 + 1.5 * normalized_f2 

	if fVar1_wobble_x <= 0.0:
		fVar1_wobble_x -= 0.5
	else:
		fVar1_wobble_x += 0.5
		
	var local_vec_y_scaled: Vector3 = Vector3.ZERO
	
	local_vec_y_scaled.y = 0.0162037037037 * stat_weight

	var local_48_rotated_vec: Vector3 = mtxa_inverse_rotate_point(local_vec_y_scaled)

	var wobble_pseudo_force_local = Vector3(fVar1_wobble_x, f_val3_for_cross_prod_y, fVar2_wobble_y_comp)
	var torque_to_add = local_48_rotated_vec.cross(wobble_pseudo_force_local)
	velocity_angular += torque_to_add

func initialize_machine() -> void:
	machine_state = 0
	machine_name = car_definition.name
	
	update_machine_stats()
	
	weight_derived_1 = 52 * stat_weight * 0.0625
	weight_derived_2 = 45 * stat_weight * 0.0625
	weight_derived_3 = 52 * stat_weight * 0.0625
	
	boost_turbo = 0
	
	if car_definition and car_definition.tilt_corners.size() == 4:
		for i in range(tilt_corners.size()):
			var corner = tilt_corners[i]
			var def_offset = car_definition.tilt_corners[i]
			
			corner.force = 0
			corner.offset = def_offset
			corner.pos_old = Vector3.ZERO
			corner.state = 0
			corner.rest_length = 1.7
	else:
		printerr("CarDefinition missing tilt_corners or wrong size for initialize_machine")
		
	stat_obstacle_collision = 0
	stat_track_collision = 1
	
	if car_definition and car_definition.wall_corners.size() == 4:
		for i in range(wall_corners.size()):
			var wall_corner = wall_corners[i]
			var def_wall_offset = car_definition.wall_corners[i]

			wall_corner.offset = def_wall_offset
			wall_corner.collision = Vector3.ZERO

			var offset_len = wall_corner.offset.length()
			if stat_obstacle_collision < offset_len:
				stat_obstacle_collision = offset_len
			
			var abs_offset_x = absf(wall_corner.offset.x)
			if stat_track_collision < abs_offset_x:
				stat_track_collision = abs_offset_x
	else:
		printerr("CarDefinition missing wall_corners or wrong size for initialize_machine")
	
	stat_obstacle_collision += 0.1
	calced_max_energy = 100.0
	
	mtxa_push()
	mtxa_from_identity()
	reset_machine(1) 
	mtxa_pop()

func update_machine_stats() -> void:
	var def_stats = car_definition.derive_machine_base_stat_values(accel_setting)
	stat_weight = def_stats.weight_kg
	stat_grip_1 = def_stats.grip_1
	stat_grip_3 = def_stats.grip_3
	stat_turn_movement = def_stats.turn_movement
	stat_strafe = def_stats.strafe
	stat_turn_reaction = def_stats.turn_reaction
	stat_grip_2 = def_stats.grip_2
	stat_body = def_stats.body
	stat_turn_tension = def_stats.turn_tension
	stat_drift_accel = def_stats.drift_accel
	stat_accel_press_grip_frames = def_stats.unk_byte_0x48
	camera_reorienting = def_stats.camera_reorienting
	camera_repositioning = def_stats.camera_repositioning
	stat_strafe_turn = def_stats.strafe_turn
	stat_acceleration = def_stats.acceleration
	stat_max_speed = def_stats.max_speed
	stat_boost_strength = 0.57 * def_stats.boost_strength 
	stat_boost_length = def_stats.boost_length
	stat_turn_decel = def_stats.turn_decel
	stat_drag = def_stats.drag

func reset_machine(param_2_reset_type: int) -> void:
	velocity = Vector3.ZERO
	velocity_local_flattened_and_rotated = Vector3.ZERO
	velocity_local = Vector3.ZERO
	velocity_angular = Vector3.ZERO
	collision_push_total = Vector3.ZERO
	collision_push_rail = Vector3.ZERO
	collision_push_track = Vector3.ZERO

	track_surface_normal = mtxa_rotate_point(Vector3.UP)

	var spawn_1 = MXGlobal.currentStageOverseer.spawns[0]
	position_current = spawn_1.position
	position_old = spawn_1.position
	position_old_2 = spawn_1.position
	position_old_dupe = spawn_1.position
	
	position_bottom = mtxa_transform_point(Vector3(0, -0.1, 0))

	position_old_2 = Vector3(0, 5, 0) 
	input_steer_yaw = 0.0
	input_yaw_dupe = 0.0
	visual_shake_mult = 0.0
	input_accel = 0.0
	input_brake = 0.0
	input_strafe = 0.0
	input_steer_pitch = 0.0
	height_above_track = 0.0
	current_checkpoint = 0
	checkpoint_fraction = 0.0
	lap = 1
	visual_rotation = Vector3.ZERO

	energy = float(calced_max_energy)
	boost_frames_manual = 0
	air_tilt = 0.0
	boost_frames = 0
	input_strafe_32 = 0.0
	input_strafe_1_6 = 0.0
	frames_since_start_2 = 0
	speed_kmh = 0.0
	race_start_charge = 0.0
	
	height_adjust_from_boost = 0.0
	grip_frames_from_accel_press = 0
	air_time = 0
	spinattack_angle = 0 
	spinattack_decrement = 0 
	spinattack_direction = 0
	damage_from_last_hit = 0.0
	frames_since_start = 0
	side_attack_delay = 0
	brake_timer = 0
	rail_collision_timer = 0
	terrain_state = 0
	machine_collision_frame_counter = 0
	frames_since_death = 0
	turning_related = 0.0
	machine_crashed = false 
	boost_delay_frame_counter = 0
	car_hit_invincibility = 0
	turn_reaction_input = 0.0
	turn_reaction_effect = 0.0
	boost_energy_use_mult = 1.0

	mtxa_push()
	var current_mtxa_origin = mtxa.origin
	mtxa.origin = spawn_1.position
	basis_physical.basis = Basis.IDENTITY.rotated(Vector3.UP, spawn_1.rotation + PI)
	basis_physical_other.basis = Basis.IDENTITY.rotated(Vector3.UP, spawn_1.rotation + PI)
	
	rotate_mtxa_from_diff_btwn_machine_front_and_back() 
	
	
	
	
	
	mtxa_pop()
	
	mtxa_push()
	var temp_visual_mtx = mtxa
	
	transform_visual = temp_visual_mtx 
	
	mtxa_pop()
	
	base_speed = 0.0
	boost_turbo = 0.0
	
	position_behind = Vector3.ZERO
	
	var state_mask_common = FZ_MS.B30 | FZ_MS.COMPLETEDRACE_2_Q | FZ_MS.COMPLETEDRACE_1_Q | \
													FZ_MS.B10 | FZ_MS.B9
	if param_2_reset_type == 0:
		machine_state &= state_mask_common
		state_2 &= 1
	else:
		machine_state &= state_mask_common
	
	state_2 &= 0xfffffc4f
	levelStartTime = MXGlobal.currentStageOverseer.localTick + MXGlobal.countdownTime

	var initial_placement_transform = Transform3D(basis_physical.basis, position_current)

	for i in range(tilt_corners.size()):
		var tc = tilt_corners[i]
		var wc = wall_corners[i]
		
		tc.state = 0
		tc.force = 0.0
		tc.force_spatial_len = 0.0
		
		tc.pos_old = mtxa_transform_point(tc.offset)
		tc.pos = tc.pos_old 
		
		tc.force_spatial = Vector3.ZERO
		tc.up_vector_2 = mtxa_rotate_point(Vector3.UP)
		tc.up_vector = mtxa_rotate_point(Vector3.UP)
		
		wc.pos_a = mtxa_transform_point(Vector3(0.0, 0.1, 0.0))
		wc.pos_b = mtxa_transform_point(wc.offset)
		wc.collision = Vector3.ZERO


func rotate_mtxa_from_diff_btwn_machine_front_and_back() -> void:
	mtxa_push()

	var fr_offset_z: float = tilt_corners[0].offset.z
	var br_offset_z: float = tilt_corners[2].offset.z

	var rotation_factor_f1: float = 0.0
	if absf(fr_offset_z) > 0.0001: 
		rotation_factor_f1 = (br_offset_z / -fr_offset_z) - 1.0

	var rotation_factor_f2_clamped: float = clampf(rotation_factor_f1, -0.2, 0.2)

	var angle_to_rotate_degrees: float = 30.0 * rotation_factor_f2_clamped
	var angle_to_rotate_rad: float = deg_to_rad(angle_to_rotate_degrees)

	mtxa_rotate_about_x(angle_to_rotate_rad)

	g_pitch_mtx_0x5e0 = mtxa 

	mtxa_pop()
	



	
	
		
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
		
	
		
		
		
		
			
		
			
			
			
		
		
		
		
			
		
			
			
		
		
		
			
			
			
			
			
			
			
				
				
			
			
			
			
			
			
			
			
				
			
				
				
			
		
			
		
			
				
			
			
			
			
			
				
				
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
			
		
			
			
			
			
				
			
			
		
			
			

func update_suspension_forces(in_corner : MachineTiltCorner) -> void:
	var time_based_factor = 0.1 + float(frames_since_start_2) / 90.0
	if time_based_factor > 0.5:
		time_based_factor = 0.5
	
	var dynamic_rest_offset = time_based_factor * 2.0 * in_corner.rest_length
	
	var inv_weight := 1.0 / stat_weight
	var inv_vel := velocity * inv_weight
	var offset_add := maxf(0.0, -(inv_vel.dot(track_surface_normal)))
	
	var p0_ray_start_ws = mtxa_transform_point(in_corner.offset + Vector3(0, 2 + offset_add, 0)) 
	var p0 = mtxa_transform_point(in_corner.offset) 
	
	var local_target_for_ray_end = Vector3(in_corner.offset.x, in_corner.offset.y - 200.0, in_corner.offset.z)
	var p1_ray_end_ws = mtxa_transform_point(local_target_for_ray_end)
	
	var compression_metric = 0.0
	var hit_found = false
	var actual_surface_normal_ws = Vector3.UP 
	
	if (in_corner.state & MachineTiltCorner.FZ_TC.B6) != 0 or (height_above_track <= 0.0 and (in_corner.state & MachineTiltCorner.FZ_TC.AIRBORNE) != 0):
		in_corner.state |= MachineTiltCorner.FZ_TC.DISCONNECTED_FROM_TRACK
	else:
		var hits : Array[Dictionary] = raycast_world(p0_ray_start_ws, p1_ray_end_ws, 1) 
		if hits.is_empty():
			hit_found = false
		else:
			var first_hit_data = hits[0]
			hit_found = true
			
			var actual_hit_surface_point_ws = first_hit_data.surface_position
			actual_surface_normal_ws = first_hit_data.surface_normal
			
			in_corner.pos = actual_hit_surface_point_ws 
			in_corner.up_vector_2 = actual_surface_normal_ws
			
			var total_sweep_length = p0.distance_to(p1_ray_end_ws)
			var hit_time_fraction_to_surface = 0.0
			if total_sweep_length > 0.0001:
				hit_time_fraction_to_surface = p0.distance_to(actual_hit_surface_point_ws) / total_sweep_length
				hit_time_fraction_to_surface = minf(hit_time_fraction_to_surface, 1.0)
			
			var actual_suspension_length = hit_time_fraction_to_surface * total_sweep_length
			
			var displacement_from_attachment_plane = -actual_suspension_length
			
			compression_metric = displacement_from_attachment_plane + dynamic_rest_offset
		
		if hit_found:
			in_corner.state &= ~MachineTiltCorner.FZ_TC.DISCONNECTED_FROM_TRACK 
		else:
			in_corner.state |= MachineTiltCorner.FZ_TC.DISCONNECTED_FROM_TRACK
			compression_metric = 0.0
		
	var calculated_force_magnitude = 0.0
		
	if compression_metric > 0.0:
		in_corner.state &= ~MachineTiltCorner.FZ_TC.AIRBORNE 
		
		var current_compression_for_spring_calc = compression_metric
		var damping1_force_component = 0.0
		
		if dynamic_rest_offset < compression_metric:
			damping1_force_component = 0.5 * (compression_metric - in_corner.force) * stat_weight
			current_compression_for_spring_calc = dynamic_rest_offset
		
		var prev_frame_compression_metric = in_corner.force 
		in_corner.force = current_compression_for_spring_calc 
		
		var mass_fraction = stat_weight / 1200.0
		var stiffness_k1 = 9000.0 
		var damping_coeff_shared = 0.009
		var stiffness_k2_for_damping = 10000.0
		
		in_corner.up_vector = in_corner.up_vector_2 
		
		var spring_force_comp = damping_coeff_shared * (stiffness_k1 * current_compression_for_spring_calc) * mass_fraction
		
		var delta_compression = prev_frame_compression_metric - current_compression_for_spring_calc
		var damping2_force_comp = mass_fraction * stiffness_k2_for_damping * damping_coeff_shared * delta_compression
		
		calculated_force_magnitude = damping1_force_component + spring_force_comp - damping2_force_comp
		
	else:
		in_corner.state |= MachineTiltCorner.FZ_TC.AIRBORNE 
		in_corner.force = 0.0
		in_corner.up_vector = Vector3.UP
		
		if (in_corner.state & MachineTiltCorner.FZ_TC.DISCONNECTED_FROM_TRACK) != 0:
			in_corner.up_vector_2 = Vector3.UP 
		
		calculated_force_magnitude = 0.0
	
	in_corner.force_spatial_len = calculated_force_magnitude
	in_corner.force_spatial = in_corner.up_vector * calculated_force_magnitude

func get_avg_track_normal_from_tilt_corners():
	var calculated_normal = null
	
	var valid_indices := []
	for i in range(tilt_corners.size()):
		var current_corner := tilt_corners[i]
		
		update_suspension_forces(current_corner)
		
		var corner_is_considered_valid = false
		if (current_corner.state & MachineTiltCorner.FZ_TC.AIRBORNE) == 0:
			corner_is_considered_valid = true
		
		if corner_is_considered_valid:
			valid_indices.append(i)
	
	if valid_indices.size() > 0:
		calculated_normal = Vector3.ZERO
		for i in valid_indices:
			calculated_normal += tilt_corners[i].up_vector
		calculated_normal = calculated_normal.normalized()
	
	return calculated_normal

func set_terrain_state_from_track() -> void:
	var terrain_bits := 0
	if (machine_state & FZ_MS.AIRBORNE) == 0:
		var hits := sphere_sweep_world(position_old, position_current, (4 | 16), 2)
		for hit in hits:
			if hit.surface_type == RORStageObject.object_types.PITSTOP:
				terrain_bits |= FZ_TERRAIN.RECHARGE
			if hit.surface_type == RORStageObject.object_types.DIRT:
				terrain_bits |= FZ_TERRAIN.DIRT
			if hit.surface_type == RORStageObject.object_types.ICE:
				terrain_bits |= FZ_TERRAIN.ICE
			if hit.surface_type == RORStageObject.object_types.LAVA:
				terrain_bits |= FZ_TERRAIN.LAVA
			if hit.surface_type == RORStageObject.object_types.DASH:
				terrain_bits |= FZ_TERRAIN.DASHPLATE
			if hit.surface_type == RORStageObject.object_types.JUMP:
				terrain_bits |= FZ_TERRAIN.JUMP
	else:
		terrain_bits = 0
	
	
	
	
	if (terrain_bits & FZ_TERRAIN.DASHPLATE) != 0:
		machine_state |= FZ_MS.JUST_HIT_DASHPLATE | FZ_MS.BOOSTING_DASHPLATE
		terrain_state |= FZ_TERRAIN.DASHPLATE
	
	if (( (terrain_bits & FZ_TERRAIN.RECHARGE) != 0) and (machine_state & FZ_MS.ZEROHP) == 0):
		state_2 |= 1
		terrain_state |= FZ_TERRAIN.RECHARGE
	
	if (machine_state & FZ_MS.BOOSTING) == 0:
		if ((terrain_bits & FZ_TERRAIN.DIRT) != 0):
			terrain_state |= FZ_TERRAIN.DIRT
	
	if ((terrain_bits & FZ_TERRAIN.ICE) != 0):
		terrain_state |= FZ_TERRAIN.ICE
	
	if (terrain_bits & FZ_TERRAIN.JUMP) != 0:
		terrain_state |= FZ_TERRAIN.JUMP
	
	if ((terrain_bits & FZ_TERRAIN.LAVA) != 0):
		terrain_state |= FZ_TERRAIN.LAVA

func handle_attack_states() -> void:
	if speed_kmh < 300.0:
		if spinattack_angle == 0.0:
			machine_state &= ~FZ_MS.SPINATTACKING
		machine_state &= ~FZ_MS.SIDEATTACKING

	if side_attack_delay != 0:
		machine_state &= ~FZ_MS.SPINATTACKING

	if (machine_state & FZ_MS.SPINATTACKING) == 0:
		spinattack_angle = 0.0
	else:
		var current_spin_angle_units = spinattack_angle

		if current_spin_angle_units == 0.0:
			spinattack_angle = PI * 8
			spinattack_decrement = PI * 0.125
			
			if input_steer_yaw <= 0.0:
				spinattack_direction = 1
			else:
				spinattack_direction = 0
		
		elif spinattack_decrement < current_spin_angle_units:
			spinattack_angle = current_spin_angle_units - spinattack_decrement
			if spinattack_angle < PI * 4: 
				spinattack_decrement -= PI * 130 / 65536 
				if spinattack_decrement < PI * 160 / 65536: 
					spinattack_decrement = PI * 160 / 65536
		else:
			spinattack_angle = 0.0
			spinattack_decrement = 0.0
			machine_state &= ~FZ_MS.SPINATTACKING
		
		machine_state &= ~FZ_MS.SIDEATTACKING

	if (machine_state & FZ_MS.SIDEATTACKING) == 0:
		side_attack_delay = 0
	else:
		var current_delay = side_attack_delay
		if current_delay == 0:
			side_attack_delay = 6
			side_attack_indicator = 0.4 * input_steer_yaw 
		elif current_delay == 1: 
			machine_state &= ~FZ_MS.SIDEATTACKING
		else:
			side_attack_delay = current_delay - 1
		
		
		if ((machine_state & (FZ_MS.JUSTHITVEHICLE_Q | FZ_MS.TOOKDAMAGE)) != 0) or (input_accel < 0.5):
			machine_state &= ~FZ_MS.SIDEATTACKING
			side_attack_delay = 1 
			
	
	if machine_collision_frame_counter > 0:
		machine_collision_frame_counter -= 1
	
	
	
	
	
	

var just_tapped_accel_visual := false

func apply_torque_from_force(p: Vector3, wf: Vector3) -> void:
	var lf: Vector3 = mtxa_inverse_rotate_point(wf)
	velocity_angular.x += -(p.z * lf.y - p.y * lf.z)
	velocity_angular.y += -(p.x * lf.z - p.z * lf.x)
	velocity_angular.z += -(p.y * lf.x - p.x * lf.y)

func simulate_machine_motion( inputs:PlayerInput = PlayerInput.Neutral ) -> void:
	
	
	input_steer_yaw = -inputs.SideMoveAxis * absf(inputs.SideMoveAxis)
	input_steer_pitch = inputs.ForwardMoveAxis
	var in_strafe_left := minf(1.0, inputs.StrafeLeft * 1.25)
	var in_strafe_right := minf(1.0, inputs.StrafeRight * 1.25)
	input_strafe = (-in_strafe_left + in_strafe_right)
	input_accel = float(inputs.Accelerate == PlayerInput.PressedState.Pressed)
	var accel_just_pressed := inputs.Accelerate == PlayerInput.PressedState.JustPressed
	input_brake = float(inputs.Brake == PlayerInput.PressedState.Pressed)
	var brake_just_pressed := inputs.Brake == PlayerInput.PressedState.JustPressed
	var in_spinattack := float(inputs.SpinAttack == PlayerInput.PressedState.Pressed)
	var in_sideattack := float(inputs.SideAttack == PlayerInput.PressedState.Pressed)
	
	if in_strafe_left > 0.05 and in_strafe_right > 0.05:
		machine_state |= FZ_MS.MANUAL_DRIFT
	if accel_just_pressed:
		machine_state = machine_state | FZ_MS.JUSTTAPPEDACCEL
		machine_state = machine_state | FZ_MS.B14
		just_tapped_accel_visual = true
	else:
		just_tapped_accel_visual = false
	
	state_2 = state_2 | 8
	
	if Input.is_action_just_pressed("DPadUp"):
		reset_machine(1)
		levelStartTime -= 270
	
	
	var ground_normal = prepare_machine_frame()
	var has_floor = find_floor_beneath_machine()
	if has_floor and ground_normal != null:
		track_surface_normal = ground_normal
	if !has_floor:
		for tc in tilt_corners:
			tc.force = 0
			tc.force_spatial = Vector3.ZERO
			tc.force_spatial_len = 0
			tc.state |= MachineTiltCorner.FZ_TC.DISCONNECTED_FROM_TRACK
			tc.state |= MachineTiltCorner.FZ_TC.AIRBORNE
	handle_steering()
	handle_suspension_states()
	var initial_angle_vel_y = velocity_angular.y
	if frames_since_start_2 > 0:
		for i in tilt_corners.size():
			handle_machine_turn_and_strafe(tilt_corners[i], initial_angle_vel_y)
	
	if (machine_state & FZ_MS.AIRBORNEMORE0_2S_Q) != 0:
		turning_related = turning_related * 0.02
	if absf(input_strafe) > 0.01:
		turning_related = turning_related * 0.04
	
	handle_linear_velocity()
	handle_angle_velocity()
	copy_mtx_to_mtxa(basis_physical)
	handle_airborne_controls()
	orient_vehicle_from_gravity_or_road()
	handle_drag_and_glide_forces()
	var inv_weight := 1.0 / stat_weight
	position_current += velocity * inv_weight
	
	mtxa.origin = position_current
	rotate_machine_from_angle_velocity()
	mtxa.origin = Vector3.ZERO
	basis_physical = mtxa
	if (machine_state & FZ_MS.STARTINGCOUNTDOWN) != 0:
		machine_state = machine_state & ~(FZ_MS.RACEJUSTBEGAN_Q|FZ_MS.JUSTTAPPEDACCEL)
	
	if (machine_state & FZ_MS.ACTIVE) != 0:
		var cd := frames_since_start_2
		if cd < 30:
			if cd % 6 == 0:
				handle_startup_wobble()
		elif cd < 90:
			velocity_angular = Vector3.ZERO
	
	if rail_collision_timer > 0:
		rail_collision_timer -= 1
	
	machine_state = machine_state & ~(FZ_MS.JUSTHITVEHICLE_Q|FZ_MS.LOWGRIP|FZ_MS.TOOKDAMAGE|FZ_MS.B14|FZ_MS.MANUAL_DRIFT)
	
	basis_physical = basis_physical.orthonormalized()
	if (machine_state & FZ_MS.STARTINGCOUNTDOWN) == 0:
		position_bottom += position_current - position_old


func respawn_car_at_last_valid_position() -> void:
	if (machine_state & FZ_MS.COMPLETEDRACE_1_Q) != 0:
		return
	var current_stage := MXGlobal.currentStage
	var checkpoint_respawns := current_stage.checkpoint_respawns
	var ccp := checkpoint_respawns[current_checkpoint]
	car_visual.visible = true
	var use_basis := ccp.respawn_transform.basis.orthonormalized()
	velocity = Vector3.ZERO
	base_speed = 0
	air_tilt = 0
	energy = max(calced_max_energy * 0.5, energy - 10)

func update_machine_corners() -> int:
	var overall_hit_detected_flag: int = 0
	var individual_corner_mask = 3
	var inv_weight := 1.0 / stat_weight
	var inv_vel := velocity * inv_weight
	
	var  any_corner_hit: bool = false
	collision_push_track = Vector3.ZERO 
	collision_push_rail = Vector3.ZERO
	collision_push_total = Vector3.ZERO
	var depenetration := Vector3.ZERO
	
	mtxa_push()
	mtxa = basis_physical
	mtxa.origin = position_current
	var total_depenetration := Vector3.ZERO
	var s0 := position_old + mtxa.basis.y * 0.5
	var s1 := position_current
	var sanity_hit := raycast_world(s0, s1, 3)
	
	
	
	if sanity_hit.size() > 0:
		
		
		
		var normal = sanity_hit[0].surface_normal
		var hit_pos = sanity_hit[0].surface_position
		for wc_2 in wall_corners:
			var p0 = mtxa_transform_point(wc_2.offset) + depenetration
			var depth = (p0 - hit_pos).dot(normal)
			
			if depth > 0:
				continue
			var depenetration_add : Vector3 = normal * maxf(0, -depth)
			collision_push_total += depenetration_add
			overall_hit_detected_flag |= 1
			any_corner_hit = true
			depenetration += depenetration_add
			if sanity_hit[0].surface_type == RORStageObject.object_types.RAIL:
				overall_hit_detected_flag |= 2
				collision_push_rail += depenetration_add
			else:
				collision_push_track += depenetration_add
		position_current += depenetration
		mtxa.origin = position_current
		total_depenetration += depenetration
		depenetration = Vector3.ZERO
	
	s0 = position_old + mtxa.basis.y * 0.5
	s1 = position_current + mtxa.basis.y * 0.5
	sanity_hit = raycast_world(s0, s1, 3)
	
	
	
	if sanity_hit.size() > 0:
		
		
		
		var normal = sanity_hit[0].surface_normal
		var hit_pos = sanity_hit[0].surface_position
		for wc_2 in wall_corners:
			var p0 = mtxa_transform_point(wc_2.offset) + depenetration
			var depth = (p0 - hit_pos).dot(normal)
			
			if depth > 0:
				continue
			var depenetration_add : Vector3 = normal * maxf(0, -depth)
			collision_push_total += depenetration_add
			overall_hit_detected_flag |= 1
			any_corner_hit = true
			depenetration += depenetration_add
			if sanity_hit[0].surface_type == RORStageObject.object_types.RAIL:
				overall_hit_detected_flag |= 2
				collision_push_rail += depenetration_add
			else:
				collision_push_track += depenetration_add
		position_current += depenetration
		mtxa.origin = position_current
		total_depenetration += depenetration
		depenetration = Vector3.ZERO
	
	var check_dist := 0.5
	if (machine_state & FZ_MS.AIRBORNE) == 0:
		check_dist = 2.0
	
	var p0_ud_tip = mtxa_transform_point(Vector3(0, check_dist + maxf(0.0, -(inv_vel + total_depenetration).dot(track_surface_normal)), 0))
	var p1_ud_target = mtxa_transform_point(Vector3(0, -10, 0))
	var hit := raycast_world(p0_ud_tip, p1_ud_target, 3)
	
	
	
	
	if hit.size() > 0:
		var normal = hit[0].surface_normal
		var hit_pos = hit[0].surface_position
		
		for wc_2 in wall_corners:
			var p0 = mtxa_transform_point(wc_2.offset) + depenetration
			var depth = (p0 - hit_pos).dot(normal)
			
			if depth > 0:
				continue
			var depenetration_add : Vector3 = normal * maxf(0, -depth)
			collision_push_total += depenetration_add
			overall_hit_detected_flag |= 1
			any_corner_hit = true
			depenetration += depenetration_add
			if hit[0].surface_type == RORStageObject.object_types.RAIL:
				collision_push_rail += depenetration_add
			else:
				collision_push_track += depenetration_add
		position_current += depenetration
		mtxa.origin = position_current
		total_depenetration += depenetration
		depenetration = Vector3.ZERO
	
	if (machine_state & FZ_MS.AIRBORNE) != 0:
		var p0_air_check = position_current - inv_vel
		var p1_air_check = position_current + inv_vel
		hit = raycast_world(p0_air_check, p1_air_check, 3)
		
		if hit.size() > 0:
			var normal = hit[0].surface_normal
			var hit_pos = hit[0].surface_position
			
			for wc_2 in wall_corners:
				var p0 = mtxa_transform_point(wc_2.offset) + depenetration
				var depth = (p0 - hit_pos).dot(normal)
				
				if depth > 0:
					continue
				var depenetration_add : Vector3 = normal * maxf(0, -depth)
				collision_push_total += depenetration_add
				overall_hit_detected_flag |= 1
				any_corner_hit = true
				depenetration += depenetration_add
				if hit[0].surface_type == RORStageObject.object_types.RAIL:
					collision_push_rail += depenetration_add
				else:
					collision_push_track += depenetration_add
			position_current += depenetration
			mtxa.origin = position_current
			total_depenetration += depenetration
			depenetration = Vector3.ZERO
	
	for i in range(4):
		var wc = wall_corners[i]
		
		var p1_feeler_target_ws = position_old + track_surface_normal * 0.5
		var p0_feeler_tip_ws = mtxa_transform_point(wc.offset)
		hit = raycast_world(p1_feeler_target_ws, p0_feeler_tip_ws, 2)
		
		if hit.size() > 0:
			var normal = hit[0].surface_normal
			var hit_pos = hit[0].surface_position
			
			for wc_2 in wall_corners:
				var p0 = mtxa_transform_point(wc_2.offset) + depenetration
				var depth = (p0 - hit_pos).dot(normal)
				
				if depth > 0:
					continue
				var depenetration_add : Vector3 = normal * maxf(0, -depth)
				if hit[0].surface_type == RORStageObject.object_types.RAIL:
					overall_hit_detected_flag |= 2
					collision_push_rail += depenetration_add
				else:
					collision_push_track += depenetration_add
				collision_push_total += depenetration_add
				overall_hit_detected_flag |= 1
				any_corner_hit = true
				depenetration += depenetration_add
			position_current += depenetration
			mtxa.origin = position_current
			total_depenetration += depenetration
			depenetration = Vector3.ZERO
		
		var p1_rail_feeler_target_ws = mtxa_transform_point(wc.offset.normalized() * 4)
		hit = raycast_world(p1_feeler_target_ws, p1_rail_feeler_target_ws, 2)
		
		if hit.size() > 0:
			var normal = hit[0].surface_normal
			var hit_pos = hit[0].surface_position
			
			for wc_2 in wall_corners:
				var p0 = mtxa_transform_point(wc_2.offset) + depenetration
				var depth = (p0 - hit_pos).dot(normal)
				
				if depth > 0:
					continue
				var depenetration_add : Vector3 = normal * maxf(0, -depth)
				if hit[0].surface_type == RORStageObject.object_types.RAIL:
					overall_hit_detected_flag |= 2
					collision_push_rail += depenetration_add
				else:
					collision_push_track += depenetration_add
				collision_push_total += depenetration_add
				overall_hit_detected_flag |= 1
				any_corner_hit = true
				depenetration += depenetration_add
			position_current += depenetration
			mtxa.origin = position_current
			total_depenetration += depenetration
			depenetration = Vector3.ZERO
			
			
			
		
		
		
		
		
		
		
		
	
	p0_ud_tip = mtxa_transform_point(Vector3(0, check_dist + maxf(0.0, -(inv_vel + total_depenetration).dot(track_surface_normal)), 0))
	p1_ud_target = mtxa_transform_point(Vector3(0, -10, 0))
	hit = raycast_world(p0_ud_tip, p1_ud_target, 3)
	
	if hit.size() > 0:
		var normal = hit[0].surface_normal
		var hit_pos = hit[0].surface_position
		
		for wc_2 in wall_corners:
			var p0 = mtxa_transform_point(wc_2.offset) + depenetration
			var depth = (p0 - hit_pos).dot(normal)
			
			if depth > 0:
				continue
			var depenetration_add : Vector3 = normal * maxf(0, -depth)
			collision_push_total += depenetration_add
			overall_hit_detected_flag |= 1
			any_corner_hit = true
			depenetration += depenetration_add
			if hit[0].surface_type == RORStageObject.object_types.RAIL:
				collision_push_rail += depenetration_add
			else:
				collision_push_track += depenetration_add
		position_current += depenetration
		mtxa.origin = position_current
		total_depenetration += depenetration
		depenetration = Vector3.ZERO
	
	if (machine_state & FZ_MS.AIRBORNE) != 0:
		var p0_air_check = position_current - inv_vel
		var p1_air_check = position_current + inv_vel
		hit = raycast_world(p0_air_check, p1_air_check, 3)
		
		if hit.size() > 0:
			var normal = hit[0].surface_normal
			var hit_pos = hit[0].surface_position
			
			for wc_2 in wall_corners:
				var p0 = mtxa_transform_point(wc_2.offset) + depenetration
				var depth = (p0 - hit_pos).dot(normal)
				
				if depth > 0:
					continue
				var depenetration_add : Vector3 = normal * maxf(0, -depth)
				collision_push_total += depenetration_add
				overall_hit_detected_flag |= 1
				any_corner_hit = true
				depenetration += depenetration_add
				if hit[0].surface_type == RORStageObject.object_types.RAIL:
					collision_push_rail += depenetration_add
				else:
					collision_push_track += depenetration_add
			position_current += depenetration
			mtxa.origin = position_current
			total_depenetration += depenetration
			depenetration = Vector3.ZERO
	
	mtxa_pop()
	return overall_hit_detected_flag

func handle_damage_and_visuals() -> void:
	pass

func create_machine_visual_transform() -> void:
	var fVar12_initial_factor: float = 0.0
	if base_speed <= 2.0:
		fVar12_initial_factor = (2.0 - base_speed) * 0.5
	
	if frames_since_start_2 < 90:
		fVar12_initial_factor *= float(frames_since_start_2) / 90.0
	
	unk_stat_0x5d4 += 0.05 * (fVar12_initial_factor - unk_stat_0x5d4)

	var dVar11_current_unk_stat = unk_stat_0x5d4
	
	var sin_val2_scaled_angle = float(g_anim_timer * 0x1a3)
	var sin_val2 = sin(sin_val2_scaled_angle)
	
	var y_offset_base = 0.006 * (dVar11_current_unk_stat * sin_val2)

	var visual_y_offset_world = mtxa_rotate_point(Vector3(0.0, y_offset_base - (0.2 * dVar11_current_unk_stat), 0.0))
	var target_visual_world_position = position_current + visual_y_offset_world
	
	mtxa.basis = mtxa.basis.orthonormalized()
	mtxa.origin = Vector3.ZERO
	basis_physical = basis_physical.orthonormalized()
	copy_mtx_to_mtxa(basis_physical)
	mtxa_push()
	var fr_offset_z = tilt_corners[0].offset.z
	var br_offset_z = tilt_corners[2].offset.z
	var stagger_factor = 0.0
	if absf(fr_offset_z) > 0.0001:
		stagger_factor = (br_offset_z / -fr_offset_z) - 1.0
	var clamped_stagger = clampf(stagger_factor, -0.2, 0.2)
	var pitch_angle_deg = 30.0 * clamped_stagger
	mtxa_rotate_about_x(deg_to_rad(pitch_angle_deg))
	g_pitch_mtx_0x5e0 = mtxa
	mtxa_pop()

	
	
		
	
		
	
		
	

	if (state_2 & 0x20) == 0:
		mtxa_push()
		mtxa_from_identity()
		if (machine_state & FZ_MS.ACTIVE) != 0:
			turn_reaction_effect += 0.05 * (turn_reaction_input - turn_reaction_effect)
			var yaw_reaction_rad = deg_to_rad(turn_reaction_effect)
			mtxa_rotate_about_y(yaw_reaction_rad)

		var world_vel_mag = velocity.length()
		var speed_factor_for_roll_pitch = 0.0
		if absf(stat_weight) > 0.0001:
			speed_factor_for_roll_pitch = (world_vel_mag / stat_weight) / 4.629629629
		
		strafe_visual_roll = int(182.04445 * (stat_strafe / 15.0) * -5.0 * input_strafe_1_6 * speed_factor_for_roll_pitch)
		
		var banking_roll_angle_val_rad = 0.0
		if absf(weight_derived_2) > 0.0001:
			banking_roll_angle_val_rad = speed_factor_for_roll_pitch * 4.5 * (velocity_angular.y / weight_derived_2)
		var banking_roll_angle_fz_units = int(10430.378 * banking_roll_angle_val_rad)

		var total_roll_fz_units: int = banking_roll_angle_fz_units + strafe_visual_roll
		
		var abs_total_roll_float = absf(float(total_roll_fz_units))
		
		var roll_damping_factor = 1.0 - abs_total_roll_float / 3640.0
		roll_damping_factor = max(roll_damping_factor, 0.0)

		var current_visual_pitch_rad = 0.0
		if absf(weight_derived_1) > 0.0001:
			current_visual_pitch_rad = visual_rotation.x / weight_derived_1
		var pitch_visual_factor = roll_damping_factor * 0.7 * current_visual_pitch_rad
		pitch_visual_factor = clampf(pitch_visual_factor, -0.3, 0.3)
		
		var current_visual_roll_rad = 0.0
		if absf(weight_derived_3) > 0.0001:
			current_visual_roll_rad = visual_rotation.z / weight_derived_3
		var roll_visual_factor = 2.5 * current_visual_roll_rad
		roll_visual_factor = clampf(roll_visual_factor, -0.5, 0.5)

		mtxa_rotate_about_x(pitch_visual_factor) 

		var iVar1_from_block2_approx_deg = 0.5 * (dVar11_current_unk_stat * sin(float(g_anim_timer * 0x109) * (TAU / 65536.0)))
		var additional_roll_from_sin_fz_units = int(182.04445 * iVar1_from_block2_approx_deg)


		total_roll_fz_units += int(10430.378 * -roll_visual_factor)
		total_roll_fz_units = clamp(total_roll_fz_units, -0x238e, 0x238e)

		var final_roll_fz_units_for_z_rot = total_roll_fz_units + additional_roll_from_sin_fz_units
		var final_roll_rad_for_z_rot = float(final_roll_fz_units_for_z_rot) * (TAU / 65536.0)
		mtxa_rotate_about_z(final_roll_rad_for_z_rot)

		var visual_delta_q = mtxa.basis.get_rotation_quaternion()
		
		unk_quat_0x5c4 = unk_quat_0x5c4.slerp(visual_delta_q, 0.2)
		mtxa_from_quat(unk_quat_0x5c4)

		var slerped_visual_rotation_transform = mtxa
		mtxa_pop()

		mtxa = mtxa * slerped_visual_rotation_transform

		if spinattack_angle != 0.0:

			if spinattack_direction == 0:
				mtxa_rotate_about_y(spinattack_angle)
			else:
				mtxa_rotate_about_y(-spinattack_angle)
	else:
		copy_mtx_to_mtxa(transform_visual)

	mtxa.origin = target_visual_world_position

	var uVar8_shake_seed = int(velocity.z * 4000000) ^ int(velocity.x * 4000000) ^ int(velocity.y * 4000000)
	
	var shake_rand_norm1 = float((uVar8_shake_seed ^ int(velocity_angular.x * 4000000)) & 0xffff) / 65535.0
	var shake_rand_norm2 = float((uVar8_shake_seed ^ int(velocity_angular.y * 4000000)) & 0xffff) / 65535.0

	var shake_magnitude = 0.00006 * visual_shake_mult
	var x_shake_rad = shake_magnitude * shake_rand_norm1
	var z_shake_rad = shake_magnitude * shake_rand_norm2 
	mtxa_rotate_about_z(z_shake_rad)
	mtxa_rotate_about_x(x_shake_rad)

	if not (machine_state & FZ_MS.BOOSTING):
		height_adjust_from_boost -= 0.05 * height_adjust_from_boost
	else:
		var effective_pitch_for_boost_lift = max(0.0, visual_rotation.x)
		var target_height_adj = 0.0
		if absf(weight_derived_1) > 0.0001:
			target_height_adj = 4.5 * (effective_pitch_for_boost_lift / weight_derived_1)
		
		height_adjust_from_boost += 0.2 * (target_height_adj - height_adjust_from_boost)
		height_adjust_from_boost = min(height_adjust_from_boost, 0.3)
	
	mtxa.origin += mtxa.basis.y * height_adjust_from_boost

	if (terrain_state & FZ_TERRAIN.DIRT) != 0:
		var jitter_scale_factor = 0.1 + speed_kmh / 900.0
		jitter_scale_factor = min(jitter_scale_factor, 1.0)

		var rand_x_norm = (float((uVar8_shake_seed ^ int(velocity_angular.y * 4000000)) & 0xffff) / 65535.0) - 0.5
		var rand_z_norm = (float((uVar8_shake_seed ^ int(velocity_angular.z * 4000000)) & 0xffff) / 65535.0) - 0.5
		
		var local_jitter_offset = Vector3(rand_x_norm, 0.0, rand_z_norm)
		var world_jitter_offset = mtxa_rotate_point(local_jitter_offset)
		
		var scaled_world_jitter = world_jitter_offset * (0.15 * jitter_scale_factor)
		mtxa.origin += scaled_world_jitter
		
	transform_visual = mtxa

func handle_machine_collision_response() -> void:
	var corner_collision_type_flag: int = update_machine_corners()
	
	var push_magnitude_rail: float = collision_push_rail.length()
	var push_magnitude_track: float = collision_push_track.length()
	var current_world_speed: float = velocity.length()
	var speed_over_weight: float = 0.0
	if absf(stat_weight) > 0.0001:
		speed_over_weight = current_world_speed / stat_weight

	if push_magnitude_track > 0.0023148148:
		if (corner_collision_type_flag & 1) != 0:
			machine_state |= FZ_MS.LOWGRIP
	if push_magnitude_rail > 0.0023148148:
		if (corner_collision_type_flag & 2) != 0 and (machine_state & FZ_MS.LOWGRIP) == 0:
			machine_state |= FZ_MS.TOOKDAMAGE
	var is_significant_collision_event: bool = (push_magnitude_rail > 0.0046296296) and (speed_over_weight > 0.0046296296)
	var apply_full_response: bool = false
	if frames_since_start_2 > 0x3c and is_significant_collision_event and (machine_state & FZ_MS.TOOKDAMAGE) != 0:
		apply_full_response = true

	if apply_full_response:
		collision_response = collision_push_total 

		var dot_push_vel_norm: float = 0.0
		if push_magnitude_rail > 0.0001 and current_world_speed > 0.0001:
			dot_push_vel_norm = collision_push_total.normalized().dot(velocity.normalized())
		
		var clamped_opposing_dot_prod: float = min(dot_push_vel_norm, 0.0)
		if speed_over_weight > 0.02314814814:
			var dot_push_track_normal: float = 0.0
			if push_magnitude_rail > 0.0001 and track_surface_normal.length_squared() > 0.0001:
				dot_push_track_normal = collision_push_total.normalized().dot(track_surface_normal.normalized())

			var response_intensity_factor: float = 0.0
			if absf(dot_push_track_normal) < 0.7:
				response_intensity_factor = (0.15 + (clamped_opposing_dot_prod * clamped_opposing_dot_prod)) / 1.5
				
				if not (machine_state & FZ_MS.B10):
					response_intensity_factor = (response_intensity_factor * current_world_speed) / 500.0
					if rail_collision_timer != 0:
						response_intensity_factor *= 0.15
				else:
					response_intensity_factor = (response_intensity_factor * current_world_speed) / 2000.0
			
			if clamped_opposing_dot_prod < -0.5:
				machine_state &= ~(FZ_MS.JUST_HIT_DASHPLATE | FZ_MS.BOOSTING_DASHPLATE | FZ_MS.JUST_PRESSED_BOOST | FZ_MS.BOOSTING)
				machine_state &= ~(FZ_MS.SIDEATTACKING | FZ_MS.SPINATTACKING)
				boost_frames = 0
				boost_frames_manual = 0
			
			if (machine_state & FZ_MS.TOOKDAMAGE) != 0:
				
				var damage_base = response_intensity_factor * stat_body
				if not (machine_state & FZ_MS.B10) and damage_base > 20.0:
					damage_base = 20.0
				
				var max_damage_this_hit = 1.01 * float(calced_max_energy)
				var actual_damage_taken = min(damage_base, max_damage_this_hit)
				
				damage_from_last_hit = actual_damage_taken
				energy -= actual_damage_taken
				
				if energy < 0.0:
					energy = 0.0
					machine_state |= FZ_MS.ZEROHP
					base_speed = 0.0
		
		var response_impulse_base = Vector3.ZERO
		if push_magnitude_rail > 0.0001:
			response_impulse_base = collision_push_total.normalized() * (clamped_opposing_dot_prod * current_world_speed)
		
		if clamped_opposing_dot_prod < 0.0:
			var ratio_clamped_dot = clamped_opposing_dot_prod / 0.7
			var val_inside_sqrt = max(0.0, 1.0 - (ratio_clamped_dot * ratio_clamped_dot))
			var sqrt_factor = sqrt(val_inside_sqrt)
			
			var base_speed_mult: float
			var boost_turbo_additional_mult: float
			
			if rail_collision_timer == 0:
				base_speed_mult = 0.2 + 0.6 * sqrt_factor
				boost_turbo_additional_mult = 0.4 * base_speed_mult
			else:
				base_speed_mult = 0.64 + 0.35 * sqrt_factor
				boost_turbo_additional_mult = 0.6 * base_speed_mult
			base_speed *= base_speed_mult
			boost_turbo *= (0.3 + boost_turbo_additional_mult)
		if speed_over_weight <= 1.851851851:
			velocity += response_impulse_base * -1.0
		else:
			var final_impulse_scale_factor: float
			if (machine_state & FZ_MS.ZEROHP) != 0:
				final_impulse_scale_factor = 3.4 - 1.7 * absf(clamped_opposing_dot_prod)
			elif rail_collision_timer == 0:
				final_impulse_scale_factor = 3.0 - 1.5 * absf(clamped_opposing_dot_prod)
			else:
				final_impulse_scale_factor = 2.0 - absf(clamped_opposing_dot_prod)
			
			velocity += response_impulse_base * (-final_impulse_scale_factor)
			
			if rail_collision_timer == 0:
				for corner in tilt_corners:
					corner.state |= MachineTiltCorner.FZ_TC.DRIFT
			rail_collision_timer = 20
			
		if response_impulse_base.length_squared() > 0.000001:
			var impulse_local_for_visuals = mtxa_inverse_rotate_point(response_impulse_base)
			visual_rotation.z += impulse_local_for_visuals.x
			visual_rotation.x += impulse_local_for_visuals.z

		if (machine_state & FZ_MS.ACTIVE) != 0:
			for i in range(wall_corners.size()):
				var wc = wall_corners[i]
				apply_torque_from_force(track_surface_normal, response_impulse_base * -0.002)
		
		if frames_since_start_2 > 60:
			align_machine_y_with_track_normal_immediate()
		
	elif (machine_state & FZ_MS.JUSTLANDED) != 0 and speed_over_weight >= 0.0462962962962:
		var vStack_a8 := mtxa_rotate_point(Vector3.UP)
		var fVar10 := vStack_a8.normalized().dot(track_surface_normal.normalized())
		var dVar8 := fVar10
		fVar10 = velocity.normalized().dot(track_surface_normal.normalized())
		var dVar7 := fVar10
		fVar10 = velocity.x
		var fVar11 := velocity.y
		var fVar1 := velocity.z
		fVar11 = velocity.length()
		fVar10 = 0.9
		var dVar9 := 2.0
		if dVar8 < 0.0:
			dVar8 = 0.0
		var dVar6 := 0.5
		fVar11 = fVar11 * dVar7
		base_speed = base_speed * dVar8
		var fStack_9c := track_surface_normal * fVar11
		fVar11 = dVar9 * absf(dVar6 + dVar7)
		var vStack_90 := velocity - fStack_9c
		if fVar11 < fVar10:
			vStack_90 = set_vec3_length(vStack_90, fVar10 * (1.0 - 1.11 * fVar11) * dVar8)
		velocity -= fStack_9c * dVar8
		velocity += vStack_90
	if frames_since_start_2 <= 90:
		velocity += track_surface_normal * -(velocity.dot(track_surface_normal))


func align_machine_y_with_track_normal_immediate() -> void:
	if track_surface_normal.length_squared() < 0.0001:
		return

	var safe_track_normal = track_surface_normal.normalized()

	var machine_current_world_up: Vector3 = mtxa_rotate_point(Vector3.UP)

	if machine_current_world_up.length_squared() < 0.0001:
		return

	var safe_machine_world_up = machine_current_world_up.normalized()

	mtxa_push()

	var delta_rotation_q = Quaternion(safe_machine_world_up, safe_track_normal)

	mtxa_from_quat(delta_rotation_q)

	var old_physical_basis_as_transform = Transform3D(basis_physical.basis, Vector3.ZERO)
	mtxa = mtxa * old_physical_basis_as_transform

	basis_physical = mtxa.basis 
	mtxa_pop()


func post_tick():
	if (state_2 & 0x8) != 0:
		copy_mtx_to_mtxa(basis_physical)
		mtxa.origin = position_current
		handle_machine_collision_response()
	handle_machine_damage_and_visuals()
	
		
	
		
		

func handle_checkpoints():
	var current_stage := MXGlobal.currentStage
	var checkpoint_respawns := current_stage.checkpoint_respawns
	var prev_lap := lap
	
	var cur_cp := checkpoint_respawns[current_checkpoint]
	var next_cp_index := wrapi(current_checkpoint + 1, 0, checkpoint_respawns.size())
	var next_cp := checkpoint_respawns[next_cp_index]
	
	
	
	
	var in_front := next_cp.checkpoint_plane.is_point_over(position_current)
	if in_front:
		if next_cp_index == 0:
			current_checkpoint = next_cp_index
			lap += 1
		else:
			var intersect:Variant = next_cp.checkpoint_plane.intersects_segment(position_old_dupe, position_current)
			if next_cp.required_checkpoint:
				if intersect and intersect.distance_to(next_cp.position) < next_cp.radius:
					current_checkpoint = next_cp_index
				elif (machine_state & FZ_MS.AIRBORNE) == 0:
					pass
					
					
			else:
				current_checkpoint = next_cp_index
	in_front = cur_cp.checkpoint_plane.is_point_over(position_current)
	var went_back := false
	if !in_front:
		current_checkpoint = wrapi(current_checkpoint - 1, 0, checkpoint_respawns.size())
		went_back = true
		if current_checkpoint == checkpoint_respawns.size() - 1:
			lap -= 1
	if !went_back:
		cur_cp = checkpoint_respawns[current_checkpoint]
		next_cp_index = wrapi(current_checkpoint + 1, 0, checkpoint_respawns.size())
		next_cp = checkpoint_respawns[next_cp_index]
		in_front = next_cp.checkpoint_plane.is_point_over(position_current)
		var intersect:Variant = next_cp.checkpoint_plane.intersects_segment(position_old, position_current)
		if in_front and intersect:
			if intersect.distance_to(next_cp.position) < next_cp.radius:
				current_checkpoint = next_cp_index
				if next_cp_index == 0:
					lap += 1
	cur_cp = checkpoint_respawns[current_checkpoint]
	next_cp_index = wrapi(current_checkpoint + 1, 0, checkpoint_respawns.size())
	next_cp = checkpoint_respawns[next_cp_index]
	var p1 := cur_cp.checkpoint_plane.project(position_current)
	var p2 := next_cp.checkpoint_plane.project(position_current)
	var p_len := (p2 - p1).length()
	var closest_point_to_center := Geometry3D.get_closest_point_to_segment(position_current, cur_cp.position, next_cp.position)
	var closest_point := Geometry3D.get_closest_point_to_segment(position_current, p1, p2)
	var fixed_origin := closest_point - p1
	var t := (fixed_origin / p_len).length()
	var lerped_radius := lerpf(cur_cp.radius, next_cp.radius, t)
	if position_current.distance_to(closest_point_to_center) > lerped_radius * 1.5:
		if (machine_state & FZ_MS.AIRBORNE) == 0:
			pass
			
			
	lap_progress = (current_checkpoint + t) / checkpoint_respawns.size()
	if lap != prev_lap:
		if !MXGlobal.currentlyRollback and get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
			if lap == 2 and prev_lap == 1 and MXGlobal.current_race_settings.laps >= 2:
				MXGlobal.play_announcer_line("boost_power")
			if lap == MXGlobal.current_race_settings.laps and prev_lap == MXGlobal.current_race_settings.laps - 1:
				MXGlobal.play_announcer_line("final_lap")
		if lap >= MXGlobal.current_race_settings.laps + 1:
			machine_state = machine_state | FZ_MS.COMPLETEDRACE_1_Q
			machine_state = machine_state | FZ_MS.COMPLETEDRACE_2_Q
			level_win_time = MXGlobal.currentStageOverseer.localTick
			if !MXGlobal.currentlyRollback and MXGlobal.currentStageOverseer.localTick == level_win_time + 90 and get_parent().get_multiplayer_authority() == multiplayer.get_unique_id():
				MXGlobal.play_announcer_line("finish")

func tick( player:ROPlayer ) -> void:
	if Engine.is_editor_hint(): return
	
	var debug_time := Time.get_ticks_usec()
	
	just_ticked = true
	calced_max_energy = 100.0
	
	side_attack_indicator = 0.0
	var input := player.get_suitable_input()
	
	if MXGlobal.currentStageOverseer.localTick < levelStartTime - 180:
		machine_state |= FZ_MS.STARTINGCOUNTDOWN
		machine_state &= ~FZ_MS.ACTIVE
	elif MXGlobal.currentStageOverseer.localTick < levelStartTime:
		machine_state |= FZ_MS.STARTINGCOUNTDOWN
		if input_accel > 0.01:
			machine_state |= FZ_MS.ACTIVE
	else:
		machine_state &= ~FZ_MS.STARTINGCOUNTDOWN
		
	
	if input.SideAttack == PlayerInput.PressedState.JustPressed:
		machine_state |= FZ_MS.SIDEATTACKING
	if input.SpinAttack == PlayerInput.PressedState.JustPressed:
		machine_state |= FZ_MS.SPINATTACKING
	if input.Boost == PlayerInput.PressedState.JustPressed and lap > 1:
		machine_state |= FZ_MS.JUST_PRESSED_BOOST
	
	g_anim_timer += 1
	update_machine_stats()
	track_surface_normal_prev = track_surface_normal
	simulate_machine_motion(input)
	mtxa = basis_physical
	mtxa.origin = position_current
	position_behind = mtxa * Vector3(0.0, 0.5, 0.5)
	
	post_tick()
	if frames_since_start_2 == 0:
		velocity = Vector3.ZERO
	handle_checkpoints()
	
	
	
	
	
	
	
	Debug.record("tick", Time.get_ticks_usec() - debug_time)
	
	
	

func save_state() -> PackedByteArray:
	car_buffer.put_u32(terrain_state_2)
	car_buffer.put_u32(machine_state)
	car_buffer.put_u32(current_checkpoint)
	car_buffer.put_u32(frames_since_death)
	car_buffer.put_u32(state_2)
	car_buffer.put_u8(suspension_reset_flag)
	car_buffer.put_u8(boost_delay_frame_counter)
	car_buffer.put_u8(machine_collision_frame_counter)
	car_buffer.put_16(strafe_effect)
	car_buffer.put_16(frames_since_start)
	car_buffer.put_16(frames_since_start_2)
	car_buffer.put_8(side_attack_delay)
	car_buffer.put_16(air_time)
	car_buffer.put_8(grip_frames_from_accel_press)
	car_buffer.put_u32(terrain_state)
	car_buffer.put_8(car_hit_invincibility)
	car_buffer.put_8(stat_accel_press_grip_frames)
	car_buffer.put_u8(boost_frames)
	car_buffer.put_u8(boost_frames_manual)
	car_buffer.put_8(spinattack_direction)
	car_buffer.put_u8(brake_timer)
	car_buffer.put_u8(lap)
	car_buffer.put_data( PackedFloat32Array([
		position_current.x,
		position_current.y,
		position_current.z,
		position_old.x,
		position_old.y,
		position_old.z,
		position_old_2.x,
		position_old_2.y,
		position_old_2.z,
		position_old_dupe.x,
		position_old_dupe.y,
		position_old_dupe.z,
		position_bottom.x,
		position_bottom.y,
		position_bottom.z,
		position_behind.x,
		position_behind.y,
		position_behind.z,
		velocity.x,
		velocity.y,
		velocity.z,
		velocity_angular.x,
		velocity_angular.y,
		velocity_angular.z,
		velocity_local.x,
		velocity_local.y,
		velocity_local.z,
		velocity_local_flattened_and_rotated.x,
		velocity_local_flattened_and_rotated.y,
		velocity_local_flattened_and_rotated.z,
		visual_rotation.x,
		visual_rotation.y,
		visual_rotation.z,
		collision_push_track.x,
		collision_push_track.y,
		collision_push_track.z,
		collision_push_rail.x,
		collision_push_rail.y,
		collision_push_rail.z,
		collision_push_total.x,
		collision_push_total.y,
		collision_push_total.z,
		collision_response.x,
		collision_response.y,
		collision_response.z,
		track_surface_normal.x,
		track_surface_normal.y,
		track_surface_normal.z,
		basis_physical.basis.x.x,
		basis_physical.basis.x.y,
		basis_physical.basis.x.z,
		basis_physical.basis.y.x,
		basis_physical.basis.y.y,
		basis_physical.basis.y.z,
		basis_physical.basis.z.x,
		basis_physical.basis.z.y,
		basis_physical.basis.z.z,
		basis_physical.origin.x,
		basis_physical.origin.y,
		basis_physical.origin.z,
		basis_physical_other.basis.x.x,
		basis_physical_other.basis.x.y,
		basis_physical_other.basis.x.z,
		basis_physical_other.basis.y.x,
		basis_physical_other.basis.y.y,
		basis_physical_other.basis.y.z,
		basis_physical_other.basis.z.x,
		basis_physical_other.basis.z.y,
		basis_physical_other.basis.z.z,
		basis_physical_other.origin.x,
		basis_physical_other.origin.y,
		basis_physical_other.origin.z,
		transform_visual.basis.x.x,
		transform_visual.basis.x.y,
		transform_visual.basis.x.z,
		transform_visual.basis.y.x,
		transform_visual.basis.y.y,
		transform_visual.basis.y.z,
		transform_visual.basis.z.x,
		transform_visual.basis.z.y,
		transform_visual.basis.z.z,
		transform_visual.origin.x,
		transform_visual.origin.y,
		transform_visual.origin.z,
		calced_max_energy,
		stat_weight,
		stat_grip_1,
		stat_grip_2,
		stat_grip_3,
		stat_turn_tension,
		stat_turn_movement,
		stat_strafe_turn,
		stat_strafe,
		stat_turn_reaction,
		stat_drift_accel,
		stat_body,
		stat_acceleration,
		stat_max_speed,
		stat_boost_strength,
		stat_boost_length,
		stat_turn_decel,
		stat_drag,
		camera_reorienting,
		camera_repositioning,
		base_speed,
		boost_turbo,
		weight_derived_1,
		weight_derived_2,
		weight_derived_3,
		race_start_charge,
		speed_kmh,
		air_tilt,
		energy,
		height_adjust_from_boost,
		height_above_track,
		checkpoint_fraction,
		input_strafe_32,
		input_strafe_1_6,
		input_steer_pitch,
		input_strafe,
		input_steer_yaw,
		input_accel,
		input_brake,
		input_yaw_dupe,
		rail_collision_timer,
		visual_shake_mult,
		damage_from_last_hit,
		turn_reaction_input,
		turn_reaction_effect,
		boost_energy_use_mult,
		turning_related,
		stat_obstacle_collision,
		stat_track_collision,
		side_attack_indicator,
		lap_progress,
		spinattack_angle,
		spinattack_decrement,
		unk_stat_0x5d4,
		g_pitch_mtx_0x5e0.basis.x.x,
		g_pitch_mtx_0x5e0.basis.x.y,
		g_pitch_mtx_0x5e0.basis.x.z,
		g_pitch_mtx_0x5e0.basis.y.x,
		g_pitch_mtx_0x5e0.basis.y.y,
		g_pitch_mtx_0x5e0.basis.y.z,
		g_pitch_mtx_0x5e0.basis.z.x,
		g_pitch_mtx_0x5e0.basis.z.y,
		g_pitch_mtx_0x5e0.basis.z.z,
		g_pitch_mtx_0x5e0.origin.x,
		g_pitch_mtx_0x5e0.origin.y,
		g_pitch_mtx_0x5e0.origin.z,
		unk_quat_0x5c4.x,
		unk_quat_0x5c4.y,
		unk_quat_0x5c4.z,
		unk_quat_0x5c4.w,
		track_surface_normal_prev.x,
		track_surface_normal_prev.y,
		track_surface_normal_prev.z,
		track_surface_pos.x,
		track_surface_pos.y,
		track_surface_pos.z
	]).to_byte_array())
	
	car_buffer.resize(car_buffer.get_position())
	
	return car_buffer.data_array

func load_state(inData : PackedByteArray) -> void:
	car_buffer.data_array = inData
	terrain_state_2 = car_buffer.get_u32()
	machine_state = car_buffer.get_u32()
	current_checkpoint = car_buffer.get_u32()
	frames_since_death = car_buffer.get_u32()
	state_2 = car_buffer.get_u32()
	suspension_reset_flag = car_buffer.get_u8()
	boost_delay_frame_counter = car_buffer.get_u8()
	machine_collision_frame_counter = car_buffer.get_u8()
	strafe_effect = car_buffer.get_16()
	frames_since_start = car_buffer.get_16()
	frames_since_start_2 = car_buffer.get_16()
	side_attack_delay = car_buffer.get_8()
	air_time = car_buffer.get_16()
	grip_frames_from_accel_press = car_buffer.get_8()
	terrain_state = car_buffer.get_u32()
	car_hit_invincibility = car_buffer.get_8()
	stat_accel_press_grip_frames = car_buffer.get_8()
	boost_frames = car_buffer.get_u8()
	boost_frames_manual = car_buffer.get_u8()
	spinattack_direction = car_buffer.get_8()
	brake_timer = car_buffer.get_u8()
	lap = car_buffer.get_u8()
	position_current.x = car_buffer.get_float()
	position_current.y = car_buffer.get_float()
	position_current.z = car_buffer.get_float()
	position_old.x = car_buffer.get_float()
	position_old.y = car_buffer.get_float()
	position_old.z = car_buffer.get_float()
	position_old_2.x = car_buffer.get_float()
	position_old_2.y = car_buffer.get_float()
	position_old_2.z = car_buffer.get_float()
	position_old_dupe.x = car_buffer.get_float()
	position_old_dupe.y = car_buffer.get_float()
	position_old_dupe.z = car_buffer.get_float()
	position_bottom.x = car_buffer.get_float()
	position_bottom.y = car_buffer.get_float()
	position_bottom.z = car_buffer.get_float()
	position_behind.x = car_buffer.get_float()
	position_behind.y = car_buffer.get_float()
	position_behind.z = car_buffer.get_float()
	velocity.x = car_buffer.get_float()
	velocity.y = car_buffer.get_float()
	velocity.z = car_buffer.get_float()
	velocity_angular.x = car_buffer.get_float()
	velocity_angular.y = car_buffer.get_float()
	velocity_angular.z = car_buffer.get_float()
	velocity_local.x = car_buffer.get_float()
	velocity_local.y = car_buffer.get_float()
	velocity_local.z = car_buffer.get_float()
	velocity_local_flattened_and_rotated.x = car_buffer.get_float()
	velocity_local_flattened_and_rotated.y = car_buffer.get_float()
	velocity_local_flattened_and_rotated.z = car_buffer.get_float()
	visual_rotation.x = car_buffer.get_float()
	visual_rotation.y = car_buffer.get_float()
	visual_rotation.z = car_buffer.get_float()
	collision_push_track.x = car_buffer.get_float()
	collision_push_track.y = car_buffer.get_float()
	collision_push_track.z = car_buffer.get_float()
	collision_push_rail.x = car_buffer.get_float()
	collision_push_rail.y = car_buffer.get_float()
	collision_push_rail.z = car_buffer.get_float()
	collision_push_total.x = car_buffer.get_float()
	collision_push_total.y = car_buffer.get_float()
	collision_push_total.z = car_buffer.get_float()
	collision_response.x = car_buffer.get_float()
	collision_response.y = car_buffer.get_float()
	collision_response.z = car_buffer.get_float()
	track_surface_normal.x = car_buffer.get_float()
	track_surface_normal.y = car_buffer.get_float()
	track_surface_normal.z = car_buffer.get_float()
	basis_physical.basis.x.x = car_buffer.get_float()
	basis_physical.basis.x.y = car_buffer.get_float()
	basis_physical.basis.x.z = car_buffer.get_float()
	basis_physical.basis.y.x = car_buffer.get_float()
	basis_physical.basis.y.y = car_buffer.get_float()
	basis_physical.basis.y.z = car_buffer.get_float()
	basis_physical.basis.z.x = car_buffer.get_float()
	basis_physical.basis.z.y = car_buffer.get_float()
	basis_physical.basis.z.z = car_buffer.get_float()
	basis_physical.origin.x = car_buffer.get_float()
	basis_physical.origin.y = car_buffer.get_float()
	basis_physical.origin.z = car_buffer.get_float()
	basis_physical_other.basis.x.x = car_buffer.get_float()
	basis_physical_other.basis.x.y = car_buffer.get_float()
	basis_physical_other.basis.x.z = car_buffer.get_float()
	basis_physical_other.basis.y.x = car_buffer.get_float()
	basis_physical_other.basis.y.y = car_buffer.get_float()
	basis_physical_other.basis.y.z = car_buffer.get_float()
	basis_physical_other.basis.z.x = car_buffer.get_float()
	basis_physical_other.basis.z.y = car_buffer.get_float()
	basis_physical_other.basis.z.z = car_buffer.get_float()
	basis_physical_other.origin.x = car_buffer.get_float()
	basis_physical_other.origin.y = car_buffer.get_float()
	basis_physical_other.origin.z = car_buffer.get_float()
	transform_visual.basis.x.x = car_buffer.get_float()
	transform_visual.basis.x.y = car_buffer.get_float()
	transform_visual.basis.x.z = car_buffer.get_float()
	transform_visual.basis.y.x = car_buffer.get_float()
	transform_visual.basis.y.y = car_buffer.get_float()
	transform_visual.basis.y.z = car_buffer.get_float()
	transform_visual.basis.z.x = car_buffer.get_float()
	transform_visual.basis.z.y = car_buffer.get_float()
	transform_visual.basis.z.z = car_buffer.get_float()
	transform_visual.origin.x = car_buffer.get_float()
	transform_visual.origin.y = car_buffer.get_float()
	transform_visual.origin.z = car_buffer.get_float()
	calced_max_energy = car_buffer.get_float()
	stat_weight = car_buffer.get_float()
	stat_grip_1 = car_buffer.get_float()
	stat_grip_2 = car_buffer.get_float()
	stat_grip_3 = car_buffer.get_float()
	stat_turn_tension = car_buffer.get_float()
	stat_turn_movement = car_buffer.get_float()
	stat_strafe_turn = car_buffer.get_float()
	stat_strafe = car_buffer.get_float()
	stat_turn_reaction = car_buffer.get_float()
	stat_drift_accel = car_buffer.get_float()
	stat_body = car_buffer.get_float()
	stat_acceleration = car_buffer.get_float()
	stat_max_speed = car_buffer.get_float()
	stat_boost_strength = car_buffer.get_float()
	stat_boost_length = car_buffer.get_float()
	stat_turn_decel = car_buffer.get_float()
	stat_drag = car_buffer.get_float()
	camera_reorienting = car_buffer.get_float()
	camera_repositioning = car_buffer.get_float()
	base_speed = car_buffer.get_float()
	boost_turbo = car_buffer.get_float()
	weight_derived_1 = car_buffer.get_float()
	weight_derived_2 = car_buffer.get_float()
	weight_derived_3 = car_buffer.get_float()
	race_start_charge = car_buffer.get_float()
	speed_kmh = car_buffer.get_float()
	air_tilt = car_buffer.get_float()
	energy = car_buffer.get_float()
	height_adjust_from_boost = car_buffer.get_float()
	height_above_track = car_buffer.get_float()
	checkpoint_fraction = car_buffer.get_float()
	input_strafe_32 = car_buffer.get_float()
	input_strafe_1_6 = car_buffer.get_float()
	input_steer_pitch = car_buffer.get_float()
	input_strafe = car_buffer.get_float()
	input_steer_yaw = car_buffer.get_float()
	input_accel = car_buffer.get_float()
	input_brake = car_buffer.get_float()
	input_yaw_dupe = car_buffer.get_float()
	rail_collision_timer = car_buffer.get_float()
	visual_shake_mult = car_buffer.get_float()
	damage_from_last_hit = car_buffer.get_float()
	turn_reaction_input = car_buffer.get_float()
	turn_reaction_effect = car_buffer.get_float()
	boost_energy_use_mult = car_buffer.get_float()
	turning_related = car_buffer.get_float()
	stat_obstacle_collision = car_buffer.get_float()
	stat_track_collision = car_buffer.get_float()
	side_attack_indicator = car_buffer.get_float()
	lap_progress = car_buffer.get_float()
	spinattack_angle = car_buffer.get_float()
	spinattack_decrement = car_buffer.get_float()
	unk_stat_0x5d4 = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.x.x = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.x.y = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.x.z = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.y.x = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.y.y = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.y.z = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.z.x = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.z.y = car_buffer.get_float()
	g_pitch_mtx_0x5e0.basis.z.z = car_buffer.get_float()
	g_pitch_mtx_0x5e0.origin.x = car_buffer.get_float()
	g_pitch_mtx_0x5e0.origin.y = car_buffer.get_float()
	g_pitch_mtx_0x5e0.origin.z = car_buffer.get_float()
	unk_quat_0x5c4.x = car_buffer.get_float()
	unk_quat_0x5c4.y = car_buffer.get_float()
	unk_quat_0x5c4.z = car_buffer.get_float()
	unk_quat_0x5c4.w = car_buffer.get_float()
	track_surface_normal_prev.x = car_buffer.get_float()
	track_surface_normal_prev.y = car_buffer.get_float()
	track_surface_normal_prev.z = car_buffer.get_float()
	track_surface_pos.x = car_buffer.get_float()
	track_surface_pos.y = car_buffer.get_float()
	track_surface_pos.z = car_buffer.get_float()
