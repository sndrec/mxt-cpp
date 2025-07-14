class_name VisualCar extends Node3D

@onready var car_visual : Node3D
@onready var car_camera: Camera3D = $CarCamera
var car_definition : CarDefinition

enum FZ_TERRAIN {
	NORMAL = 0x1,
	DASH = 0x2,
	RECHARGE = 0x4,
	DIRT = 0x8,
	JUMP = 0x10,
	LAVA = 0x20,
	ICE = 0x40,
	BACKSIDE = 0x80,
	RAIL = 0x100,
	HOLE = 0x200
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

enum FZ_TC{
	B1 = 0x1,
	AIRBORNE = 0x2,
	DRIFT = 0x4,
	DISCONNECTED_FROM_TRACK = 0x8,
	STRAFING = 0x10,
	B6 = 0x20,
	B7 = 0x40,
	B8 = 0x80
}

@onready var terrain_sound: AudioStreamPlayer3D = $CarTransform/AudioStreamPlayer3D
@onready var thrust_sound: AudioStreamPlayer3D = $CarTransform/AudioStreamPlayer3D2
@onready var engine_sound: AudioStreamPlayer3D = $CarTransform/AudioStreamPlayer3D3
@onready var boost_sound: AudioStreamPlayer3D = $CarTransform/AudioStreamPlayer3D4
@onready var air_sound: AudioStreamPlayer3D = $CarTransform/AudioStreamPlayer3D5
@onready var strafe_sound: AudioStreamPlayer3D = $CarTransform/AudioStreamPlayer3D6

var owning_id : int = 0
var player_settings: Resource
var game_manager : GameManager
@onready var race_hud: RaceHud = $race_hud
@onready var car_transform: Node3D = $CarTransform

var position_current := Vector3.ZERO
var velocity := Vector3.ZERO
var velocity_angular := Vector3.ZERO
var velocity_local := Vector3.ZERO
var basis_physical := Transform3D.IDENTITY
var transform_visual := Transform3D.IDENTITY
var base_speed := 0.0
var boost_turbo := 0.0
var race_start_charge := 0.0
var speed_kmh := 0.0
var air_tilt := 0.0
var energy := 0.0
var lap_progress := 0.0
var checkpoint_fraction := 0.0
var input_strafe := 0.0
var boost_frames := 0
var boost_frames_manual := 0
var current_checkpoint := 0
var lap := 1
var air_time := 0
var machine_state := 0
var terrain_state := 0
var terrain_state_old := 0
var frames_since_start_2 := 0


var track_normal_vis := Vector3.UP
var track_normal_old_vis := Vector3.UP
var lerped_curvature := 0.0
var slerped_up_y := Vector3.UP
var slerped_forward_z := Vector3.ZERO
var camera_basis : Basis = Basis.IDENTITY
var camera_basis_smoothed : Basis = Basis.IDENTITY

var tilt_fl_state := 0
var tilt_fr_state := 0
var tilt_bl_state := 0
var tilt_br_state := 0



var unk_stat_0x5d4 := 0.0
var g_pitch_mtx_0x5e0 := Transform3D.IDENTITY
var turn_reaction_effect := 0.0
var strafe_visual_roll : float = 0.0 
var unk_quat_0x5c4 : Quaternion = Quaternion.IDENTITY
var height_adjust_from_boost := 0.0


var turn_reaction_input := 0.0
var g_anim_timer : int = 0
var state_2 : int = 0
var tilt_fl_offset : Vector3 = Vector3.ZERO
var tilt_bl_offset : Vector3 = Vector3.ZERO
var stat_weight : float = 0.0
var stat_strafe : float = 0.0
var input_strafe_1_6 : float = 0.0
var weight_derived_1 := 0.0
var weight_derived_2 := 0.0
var weight_derived_3 := 0.0
var visual_rotation := Vector3.ZERO
var spinattack_angle := 0.0
var spinattack_direction := 0
var visual_shake_mult := 0.0

var rollback_offset_error := Vector3.ZERO
var old_pos := Vector3.ZERO

var rollback_rot_error := Basis.IDENTITY
var old_rot := Basis.IDENTITY

var car_old_transform := Transform3D.IDENTITY
var car_desired_transform := Transform3D.IDENTITY
var car_old_pc := Vector3.ZERO
var car_desired_pc := Vector3.ZERO

func _ready() -> void:
	car_visual = car_definition.car_scene.instantiate()
	car_transform.add_child(car_visual)
	air_sound.stream = preload("res://sfx/vehicle/air_1.wav")
	air_sound.play()
	thrust_sound.stream = preload("res://sfx/vehicle/thrust_on.wav")
	thrust_sound.stop()
	engine_sound.stream = preload("res://sfx/vehicle/engine.wav")
	engine_sound.play()
	boost_sound.stream = preload("res://sfx/vehicle/boost/PACK1-110.wav")
	boost_sound.stop()
	terrain_sound.stream = preload("res://sfx/vehicle/restore.wav")
	terrain_sound.play()
	strafe_sound.stream = preload("res://sfx/vehicle/strafe.wav")
	strafe_sound.play()

func create_machine_visual_transform():
	var fVar12_initial_factor := 0.0
	var mtxa := Transform3D(basis_physical.basis, position_current)
	if (base_speed <= 2.0):
		fVar12_initial_factor = (2.0 - base_speed) * 0.5
	if (frames_since_start_2 < 90):
		fVar12_initial_factor *= float(frames_since_start_2) / 90.0
	unk_stat_0x5d4 += 0.05 * (fVar12_initial_factor - unk_stat_0x5d4)
	var dVar11_current_unk_stat := unk_stat_0x5d4
	var sin_val2_scaled_angle := float(g_anim_timer * 0x1a3) * (TAU / 65536.0);
	var sin_val2 := sin(sin_val2_scaled_angle);
	var y_offset_base := 0.006 * (dVar11_current_unk_stat * sin_val2);
	var visual_y_offset_world := mtxa.basis * (Vector3(0.0, y_offset_base - (0.2 * dVar11_current_unk_stat), 0.0))
	var target_visual_world_position := position_current + visual_y_offset_world
	mtxa.origin = Vector3.ZERO
	var car_rot := basis_physical
	var fr_offset_z := tilt_fl_offset.z
	var br_offset_z := tilt_bl_offset.z
	var stagger_factor := 0.0
	if (absf(fr_offset_z) > 0.0001):
		stagger_factor = (br_offset_z / -fr_offset_z) - 1.0
	var clamped_stagger := clampf(stagger_factor, -0.2, 0.2)
	var pitch_angle_deg := 30.0 * clamped_stagger
	car_rot = car_rot.rotated_local(Vector3.RIGHT, deg_to_rad(pitch_angle_deg))
	g_pitch_mtx_0x5e0 = car_rot
	var accum_transform : Transform3D = Transform3D.IDENTITY
	if ((state_2 & 0x20) == 0):
		if (machine_state & FZ_MS.ACTIVE) != 0:
			turn_reaction_effect += 0.05 * (turn_reaction_input - turn_reaction_effect)
			var yaw_reaction_rad := deg_to_rad(turn_reaction_effect);
			accum_transform = accum_transform.rotated(Vector3.UP, yaw_reaction_rad)
			#mtxa->rotate_y(yaw_reaction_rad);
		var world_vel_mag := velocity.length();
		var speed_factor_for_roll_pitch := 0.0;
		if (absf(stat_weight) > 0.0001):
			speed_factor_for_roll_pitch = (world_vel_mag / stat_weight) / 4.629629629;
		strafe_visual_roll = int(182.04445 * (stat_strafe / 15.0) * -5.0 * input_strafe_1_6 * speed_factor_for_roll_pitch);
		var banking_roll_angle_val_rad := 0.0;
		if (absf(weight_derived_2) > 0.0001):
			banking_roll_angle_val_rad = speed_factor_for_roll_pitch * 4.5 * (velocity_angular.y / weight_derived_2);
		var banking_roll_angle_fz_units := int(10430.378 * banking_roll_angle_val_rad);
		var total_roll_fz_units := banking_roll_angle_fz_units + strafe_visual_roll;
		var abs_total_roll_float := absf(float(total_roll_fz_units));
		var roll_damping_factor := 1.0 - abs_total_roll_float / 3640.0;
		roll_damping_factor = maxf(roll_damping_factor, 0.0);
		var current_visual_pitch_rad := 0.0;
		if (absf(weight_derived_1) > 0.0001):
			current_visual_pitch_rad = visual_rotation.x / weight_derived_1;
		var pitch_visual_factor := roll_damping_factor * 0.7 * current_visual_pitch_rad;
		pitch_visual_factor = clampf(pitch_visual_factor, -0.3, 0.3);
		var current_visual_roll_rad := 0.0;
		if (absf(weight_derived_3) > 0.0001):
			current_visual_roll_rad = visual_rotation.z / weight_derived_3;
		var roll_visual_factor := 2.5 * current_visual_roll_rad;
		roll_visual_factor = clampf(roll_visual_factor, -0.5, 0.5);
		accum_transform = accum_transform.rotated_local(Vector3.RIGHT, pitch_visual_factor)
		var iVar1_from_block2_approx_deg := 0.5 * (dVar11_current_unk_stat * sin(float(g_anim_timer * 0x109) * (TAU / 65536.0)));
		var additional_roll_from_sin_fz_units := int(182.04445 * iVar1_from_block2_approx_deg);
		total_roll_fz_units += int(10430.378 * -roll_visual_factor);
		total_roll_fz_units = clampi(total_roll_fz_units, -0x238e, 0x238e);
		var final_roll_fz_units_for_z_rot := total_roll_fz_units + additional_roll_from_sin_fz_units;
		var final_roll_rad_for_z_rot = float(final_roll_fz_units_for_z_rot) * (TAU / 65536.0);
		accum_transform = accum_transform.rotated_local(-Vector3.FORWARD, final_roll_rad_for_z_rot)
		var visual_delta_q := Quaternion(accum_transform.basis);
		unk_quat_0x5c4 = unk_quat_0x5c4.slerp(visual_delta_q, 0.2);
		accum_transform.basis = Basis(unk_quat_0x5c4)
		var slerped_visual_rotation_transform := accum_transform
		mtxa = mtxa * slerped_visual_rotation_transform
		if (spinattack_angle != 0.0):
			if (spinattack_direction == 0):
				mtxa = mtxa.rotated_local(Vector3.UP, spinattack_angle);
			else:
				mtxa = mtxa.rotated_local(Vector3.UP, -spinattack_angle);
	else:
		mtxa = transform_visual
	mtxa.origin = target_visual_world_position;
	var uVar8_shake_seed := randi()
	var shake_rand_norm1 := float((uVar8_shake_seed ^ int(velocity_angular.x * 4000000.0)) & 0xffff) / 65535.0;
	var shake_rand_norm2 := float((uVar8_shake_seed ^ int(velocity_angular.y * 4000000.0)) & 0xffff) / 65535.0;
	var shake_magnitude := 0.00006 * visual_shake_mult;
	var x_shake_rad := shake_magnitude * shake_rand_norm1;
	var z_shake_rad := shake_magnitude * shake_rand_norm2;
	mtxa = mtxa.rotated_local(-Vector3.FORWARD, z_shake_rad);
	mtxa = mtxa.rotated_local(Vector3.RIGHT, x_shake_rad);
	if ((machine_state & FZ_MS.BOOSTING) == 0):
		height_adjust_from_boost -= 0.05 * height_adjust_from_boost;
	else:
		var effective_pitch_for_boost_lift = maxf(0.0, visual_rotation.x);
		var target_height_adj = 0.0;
		if (absf(weight_derived_1) > 0.0001):
			target_height_adj = 4.5 * (effective_pitch_for_boost_lift / weight_derived_1);
		height_adjust_from_boost += 0.2 * (target_height_adj - height_adjust_from_boost);
		height_adjust_from_boost = minf(height_adjust_from_boost, 0.3);
	mtxa.origin += mtxa.basis.y * height_adjust_from_boost;
	if (terrain_state & FZ_TERRAIN.DIRT) != 0:
		var jitter_scale_factor := 0.1 + speed_kmh / 900.0;
		jitter_scale_factor = minf(jitter_scale_factor, 1.0);
		var rand_x_norm := float((uVar8_shake_seed ^ int(velocity_angular.y * 4000000.0)) & 0xffff) / 65535.0 - 0.5;
		var rand_z_norm := float((uVar8_shake_seed ^ int(velocity_angular.z * 4000000.0)) & 0xffff) / 65535.0 - 0.5;
		var local_jitter_offset := Vector3(rand_x_norm, 0.0, rand_z_norm);
		var world_jitter_offset := local_jitter_offset * mtxa.basis;

		var scaled_world_jitter := world_jitter_offset * (0.15 * jitter_scale_factor);
		mtxa.origin += scaled_world_jitter;

	transform_visual = mtxa


func store_old_pos() -> void:
	old_pos = position_current
	old_rot = basis_physical.basis

func calculate_error() -> void:
	var position_error := position_current - old_pos
	rollback_offset_error -= position_error
	
	var rotation_error := basis_physical.basis * old_rot.inverse()
	rollback_rot_error = rollback_rot_error * rotation_error.inverse()

var frame_accumulation := 0.0

func _physics_process(delta: float) -> void:
	create_machine_visual_transform()
	DebugDraw2D.set_text("turbo", boost_turbo)
	frame_accumulation = 0.0
	car_old_pc = car_desired_pc
	car_desired_pc = position_current
	car_old_transform = car_desired_transform
	car_desired_transform = transform_visual
	
	var use_vy := remap(clampf(absf(velocity.y), 0, 5000), 0, 5000, 0, 1)
	
	if (machine_state & FZ_MS.AIRBORNE) != 0:
		var target_db := remap(use_vy, 0, 1, 0, 20)
		air_sound.volume_db = lerpf(air_sound.volume_db, target_db, delta * 8)
		var target_pitch := remap(use_vy, 0, 1, 0.5, 1.5)
		air_sound.pitch_scale = lerpf(air_sound.pitch_scale, target_pitch, delta * 8)
	else:
		air_sound.volume_db = lerpf(air_sound.volume_db, -20, delta * 8)
	
	#DebugDraw2D.set_text("vy", velocity.y)
	
	if (terrain_state_old & FZ_TERRAIN.RECHARGE) == 0 and (terrain_state & FZ_TERRAIN.RECHARGE) != 0:
		terrain_sound.stream = preload("res://sfx/vehicle/restore.wav")
		terrain_sound.play(0.0)
	elif (terrain_state_old & FZ_TERRAIN.DIRT) == 0 and (terrain_state & FZ_TERRAIN.DIRT) != 0:
		terrain_sound.stream = preload("res://sfx/vehicle/terrain_dirt.wav")
		terrain_sound.play(0.0)
	elif (terrain_state_old & FZ_TERRAIN.LAVA) == 0 and (terrain_state & FZ_TERRAIN.LAVA) != 0:
		terrain_sound.stream = preload("res://sfx/vehicle/terrain_lava.wav")
		terrain_sound.play(0.0)
	elif terrain_state == 0:
		terrain_sound.stop()
	
	#DebugDraw2D.set_text("input_strafe", input_strafe)
	
	if (absf(input_strafe) > 0.05):
		strafe_sound.volume_db = lerpf(strafe_sound.volume_db, 20, delta * 12)
		if !strafe_sound.playing:
			strafe_sound.play(0.0)
	else:
		strafe_sound.volume_db = lerpf(strafe_sound.volume_db, -20, delta * 12)
		if strafe_sound.volume_db <= -10:
			strafe_sound.stop()
	
	if (Input.is_action_just_pressed("Accelerate")):
		thrust_sound.stop()
		thrust_sound.play(0.0)
	if (machine_state & FZ_MS.JUST_PRESSED_BOOST):
		boost_sound.stop()
		boost_sound.play(0.0)
	if (machine_state & FZ_MS.JUST_HIT_DASHPLATE):
		boost_sound.stop()
		boost_sound.play(0.0)
	terrain_state_old = terrain_state

func _process(delta: float) -> void:
	frame_accumulation += delta
	delta = minf(1.0, delta)
	var ratio := frame_accumulation * 60
	car_transform.global_transform = car_old_transform.interpolate_with(car_desired_transform, ratio)
	var use_car_pos := car_old_pc.lerp(car_desired_pc, ratio) + rollback_offset_error
	rollback_offset_error = rollback_offset_error.lerp(Vector3.ZERO, 20 * delta)
	rollback_rot_error = rollback_rot_error.slerp(Basis.IDENTITY, 20 * delta)
	#DebugDraw2D.set_text("rollback offset error", rollback_offset_error)
	car_transform.global_transform.origin += rollback_offset_error
	car_transform.global_transform.basis = car_transform.global_transform.basis * rollback_rot_error
	var calced_max_energy := 100.0
	var energy_ratio : float = minf(1.0, (energy / calced_max_energy) * 4.0)
	#var manual_boost_visual := float(boost_frames_manual) / (car_definition.boost_length * Engine.physics_ticks_per_second)
	#var dashplate_visual := float(boost_frames) / (car_definition.boost_length * Engine.physics_ticks_per_second * 0.5)
	#var boost_ratio : float = dashplate_visual if (machine_state & FZ_MS.BOOSTING_DASHPLATE) else manual_boost_visual
	var energy_flash := Color(0.04, -0.01, -0.01) * (sin(0.015 * Time.get_ticks_msec()) * 0.5 + 0.5) * (1.0 - energy_ratio)
	#var boost_flash := Color(0, 0.03, 0.075) * (boost_ratio)
	#var final_overlay := energy_flash + boost_flash + Color(1, 1, 1) * damage * 0.1
	var target_fov := remap(speed_kmh, 0, 1800, 50, 90)
	#target_fov += remap(boost_ratio, 0, 1, 0, 50)
	target_fov = minf(target_fov, 100)
	car_camera.fov = lerpf(car_camera.fov, target_fov, delta * 2)
	var use_forward_z : Vector3 = basis_physical.basis.z
	use_forward_z = use_forward_z.normalized()
	if (tilt_fl_state & FZ_TC.DRIFT) != 0:
		use_forward_z = -velocity.slide(basis_physical.basis.y.normalized()).normalized()
	
	var target_y := basis_physical.basis.y
	
	var starting_frames_past := frames_since_start_2 > 90
	
	if !slerped_up_y.is_equal_approx(target_y):
		slerped_up_y = slerped_up_y.slerp(target_y, 20 * delta).normalized()
	slerped_forward_z = slerped_forward_z.slerp(use_forward_z, 20 * delta).normalized()
	
	var use_slerpto = Basis(Quaternion(basis_physical.basis.z, slerped_forward_z)) * basis_physical.basis
	use_slerpto = Basis(Quaternion(basis_physical.basis.y, slerped_up_y)) * use_slerpto
	camera_basis = camera_basis.slerp(use_slerpto, 30 * delta).orthonormalized()
	camera_basis_smoothed = camera_basis_smoothed.slerp(camera_basis, 30 * delta).orthonormalized()
	var use_basis := camera_basis_smoothed
	
	
	var final_y := slerped_up_y
	if starting_frames_past:
		#print("ah")
		var flat_up_y := slerped_up_y.slide(use_basis.x).normalized()
		var flat_basis_y := basis_physical.basis.y.slide(use_basis.x).normalized()
		var rot_angle_1 := flat_up_y.signed_angle_to(flat_basis_y, use_basis.x)
		var rot_angle_2 := use_basis.y.signed_angle_to(flat_basis_y, use_basis.x)
		#slerped_up_y = slerped_up_y.rotated(use_basis.x, rot_angle_1 * 80 * delta)
		#use_basis = use_basis.rotated(use_basis.x, rot_angle_2 * 60 * delta)
		final_y = slerped_up_y
		if (machine_state & FZ_MS.AIRBORNE) == 0:
			if speed_kmh > 1:
				var sideways := velocity.normalized().cross(track_normal_vis).normalized()
				var flattened := track_normal_vis.slide(sideways).normalized()
				var flattened_prev := track_normal_old_vis.slide(sideways).normalized()
				var road_angle_change := flattened.signed_angle_to(flattened_prev, sideways)
				var arc_length := speed_kmh * 100
				lerped_curvature = lerpf(lerped_curvature, road_angle_change / arc_length if !is_zero_approx(arc_length) else 0.0, 7.5 * delta)
				DebugDraw2D.set_text("curvature", lerped_curvature)
				final_y = final_y.rotated(sideways, lerped_curvature * -8000)
				use_basis = use_basis.rotated(sideways, lerped_curvature * -8000)
	car_camera.position = (use_car_pos + rollback_offset_error) + final_y * remap(car_camera.fov, 50, 100, 6.0, 5.5) + use_basis.z * remap(car_camera.fov, 50, 100, 12.0, 6.0)
	car_camera.basis = use_basis.rotated(use_basis.x, deg_to_rad(-15))
