class_name VisualCar extends Node3D

enum EffectTier {
	FULL,
	THRUSTER_ONLY,
}

@onready var car_visual : Node3D
@onready var car_camera: Camera3D = $CarCamera
var car_definition : CarDefinition
@onready var recharge_particles: GPUParticles3D = $CarTransform/RechargeParticles
@onready var name_label: Label = $NameLabel

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

var owning_id : int = 0
var effect_pool_slot : int = -1
var player_settings: Resource
var game_manager : GameManager
@onready var race_hud: RaceHud = $race_hud
@onready var car_transform: Node3D = $CarTransform
var local_visual_enabled := false
var effect_tier := EffectTier.FULL

var position_current := Vector3.ZERO
var position_old := Vector3.ZERO
var track_surface_normal := Vector3.ZERO
var track_surface_pos := Vector3.ZERO
var height_above_track := 0.0
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
var calced_max_energy := 100.0
var s_boost_charge := 0
var s_boost_charge_max := 50
var s_boost_active := false
var s_boost_ready := false
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
var input_accel := 0.0

var track_normal_vis := Vector3.UP
var track_normal_old_vis := Vector3.UP
var lerped_curvature := 0.0
var gameplay_camera := FzgxGameplayCamera.new()

var tilt_fl_state := 0
var tilt_fr_state := 0
var tilt_bl_state := 0
var tilt_br_state := 0
var camera_reorienting := 1.0
var camera_repositioning := 1.0

var restore_state := 0
var restore_wait_frames := 0
var restore_move_frames := 0
var restore_start_transform : Transform3D
var restore_target_transform : Transform3D


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
var rollback_offset_error_prev := Vector3.ZERO
var old_pos := Vector3.ZERO

var rollback_rot_error := Basis.IDENTITY
var rollback_rot_error_prev := Basis.IDENTITY
var old_rot := Basis.IDENTITY

var car_old_transform := Transform3D.IDENTITY
var car_desired_transform := Transform3D.IDENTITY
var car_old_basis_physical := Transform3D.IDENTITY
var car_desired_basis_physical := Transform3D.IDENTITY
var car_old_pc := Vector3.ZERO
var car_desired_pc := Vector3.ZERO
var car_old_po := Vector3.ZERO
var car_desired_po := Vector3.ZERO
var car_old_vel := Vector3.ZERO
var car_desired_vel := Vector3.ZERO
var old_ts_normal := Vector3.ZERO
var desired_ts_normal := Vector3.ZERO

var car_overlay_colour := Color.BLACK
var car_material : ShaderMaterial
var car_outline_material : ShaderMaterial
var vehicle_main : MeshInstance3D
var vehicle_shadow : MeshInstance3D
var _needs_process_reset := false

func _ready() -> void:
	car_visual = Node3D.new()
	car_visual.name = "CarVisualProxy"
	car_transform.add_child(car_visual)
	if local_visual_enabled:
		car_camera.make_current()
	_apply_effect_tier_state()
	if !local_visual_enabled:
		return
	await get_tree().create_timer(2.0).timeout

func set_effect_tier(in_tier: int) -> void:
	if effect_tier == in_tier:
		return
	effect_tier = in_tier
	_needs_process_reset = true
	_apply_effect_tier_state()

func _reset_interpolation_state() -> void:
	transform_visual = Transform3D(basis_physical.basis, position_current)
	car_old_transform = transform_visual
	car_desired_transform = transform_visual
	car_old_basis_physical = basis_physical
	car_desired_basis_physical = basis_physical
	car_old_pc = position_current
	car_desired_pc = position_current
	car_old_po = position_old
	car_desired_po = position_old
	car_old_vel = velocity
	car_desired_vel = velocity
	old_ts_normal = track_surface_normal
	desired_ts_normal = track_surface_normal
	track_surface_smoothed = track_surface_normal
	if gameplay_camera:
		gameplay_camera.reset()
	rollback_offset_error = Vector3.ZERO
	rollback_offset_error_prev = Vector3.ZERO
	rollback_rot_error = Basis.IDENTITY
	rollback_rot_error_prev = Basis.IDENTITY
	frame_accumulation = 0.0

func _apply_low_cost_visual_state() -> void:
	var use_transform := Transform3D(basis_physical.basis, position_current)
	car_transform.global_transform = use_transform
	transform_visual = use_transform
	car_overlay_colour = car_overlay_colour.lerp(Color.BLACK, 0.2)
	if car_outline_material and is_instance_valid(car_outline_material):
		car_outline_material.set_shader_parameter("overlay_colour", Color.BLACK)
		car_outline_material.set_shader_parameter("in_velocity", Vector3.ZERO)
	if car_material and is_instance_valid(car_material):
		car_material.set_shader_parameter("in_overlay_colour", Color.BLACK)

func _apply_effect_tier_state() -> void:
	var full_effects_enabled := effect_tier == EffectTier.FULL
	var use_frame_processing := local_visual_enabled or full_effects_enabled
	set_physics_process(use_frame_processing)
	set_process(use_frame_processing)
	recharge_particles.emitting = false
	attack_particles.emitting = false
	landing_particles.emitting = false
	boost_electricity.boosting = false
	boost_electricity.visible = full_effects_enabled
	if is_instance_valid(name_label):
		name_label.visible = !local_visual_enabled
	if _needs_process_reset:
		_reset_interpolation_state()
		_needs_process_reset = false

func apply_sim_state(
	in_position_current: Vector3,
	in_position_old: Vector3,
	in_track_surface_normal: Vector3,
	in_height_above_track: float,
	in_velocity: Vector3,
	in_velocity_angular: Vector3,
	in_basis_physical: Transform3D,
	in_base_speed: float,
	in_boost_turbo: float,
	in_speed_kmh: float,
	in_energy: float,
	in_lap_progress: float,
	in_boost_frames: int,
	in_boost_frames_manual: int,
	in_lap: int,
	in_machine_state: int,
	in_terrain_state: int,
	in_frames_since_start_2: int,
	in_tilt_fl_state: int,
	in_input_strafe: float,
	in_turn_reaction_input: float,
	in_g_anim_timer: int,
	in_state_2: int,
	in_tilt_fl_offset: Vector3,
	in_tilt_bl_offset: Vector3,
	in_stat_weight: float,
	in_stat_strafe: float,
	in_input_strafe_1_6: float,
	in_weight_derived_1: float,
	in_weight_derived_2: float,
	in_weight_derived_3: float,
	in_visual_rotation: Vector3,
	in_spinattack_angle: float,
	in_spinattack_direction: int,
	in_visual_shake_mult: float,
	in_input_accel: float,
	in_restore_state: int,
	in_restore_move_frames: int,
	in_restore_start_transform: Transform3D,
	in_restore_target_transform: Transform3D,
	in_s_boost_charge: int,
	in_s_boost_charge_max: int,
	in_s_boost_active: bool,
	in_s_boost_ready: bool,
	in_tilt_fr_state: int,
	in_tilt_bl_state: int,
	in_tilt_br_state: int,
	in_camera_reorienting: float,
	in_camera_repositioning: float,
	in_track_surface_pos: Vector3,
	in_calced_max_energy: float = 100.0
) -> void:
	position_current = in_position_current
	position_old = in_position_old
	track_surface_normal = in_track_surface_normal
	height_above_track = in_height_above_track
	velocity = in_velocity
	velocity_angular = in_velocity_angular
	basis_physical = in_basis_physical
	base_speed = in_base_speed
	boost_turbo = in_boost_turbo
	speed_kmh = in_speed_kmh
	energy = in_energy
	lap_progress = in_lap_progress
	boost_frames = in_boost_frames
	boost_frames_manual = in_boost_frames_manual
	lap = in_lap
	machine_state = in_machine_state
	terrain_state = in_terrain_state
	frames_since_start_2 = in_frames_since_start_2
	tilt_fl_state = in_tilt_fl_state
	tilt_fr_state = in_tilt_fr_state
	tilt_bl_state = in_tilt_bl_state
	tilt_br_state = in_tilt_br_state
	input_strafe = in_input_strafe
	turn_reaction_input = in_turn_reaction_input
	g_anim_timer = in_g_anim_timer
	state_2 = in_state_2
	tilt_fl_offset = in_tilt_fl_offset
	tilt_bl_offset = in_tilt_bl_offset
	stat_weight = in_stat_weight
	stat_strafe = in_stat_strafe
	input_strafe_1_6 = in_input_strafe_1_6
	weight_derived_1 = in_weight_derived_1
	weight_derived_2 = in_weight_derived_2
	weight_derived_3 = in_weight_derived_3
	visual_rotation = in_visual_rotation
	spinattack_angle = in_spinattack_angle
	spinattack_direction = in_spinattack_direction
	visual_shake_mult = in_visual_shake_mult
	input_accel = in_input_accel
	restore_state = in_restore_state
	restore_move_frames = in_restore_move_frames
	restore_start_transform = in_restore_start_transform
	restore_target_transform = in_restore_target_transform
	s_boost_charge = in_s_boost_charge
	s_boost_charge_max = in_s_boost_charge_max
	s_boost_active = in_s_boost_active
	s_boost_ready = in_s_boost_ready
	camera_reorienting = in_camera_reorienting
	camera_repositioning = in_camera_repositioning
	track_surface_pos = in_track_surface_pos
	calced_max_energy = in_calced_max_energy
	if !is_processing():
		_apply_low_cost_visual_state()

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
	if owning_id == multiplayer.get_unique_id():
		return
	var position_error := position_current - old_pos
	rollback_offset_error -= position_error
	
	var rotation_error := basis_physical.basis * old_rot.inverse()
	rollback_rot_error = rollback_rot_error * rotation_error.inverse()

var frame_accumulation := 0.0

func damp(a : float, b : float, lambda : float, dt : float) -> float:
	return lerp(a, b, 1.0 - exp(-lambda * dt))
func damp_vec3(a : Vector3, b : Vector3, lambda : float, dt : float) -> Vector3:
	return a.lerp(b, 1.0 - exp(-lambda * dt))
func damp_vec3_slerp(a : Vector3, b : Vector3, lambda : float, dt : float) -> Vector3:
	return a.slerp(b, 1.0 - exp(-lambda * dt))
func damp_basis(a : Basis, b : Basis, lambda : float, dt : float) -> Basis:
	return a.slerp(b, 1.0 - exp(-lambda * dt))
func damp_t3d(a : Transform3D, b : Transform3D, lambda : float, dt : float) -> Transform3D:
	return a.interpolate_with(b, 1.0 - exp(-lambda * dt))

func _camera_aspect_ratio() -> float:
	var viewport_size := get_viewport().get_visible_rect().size
	if viewport_size.y <= 0.0:
		return 4.0 / 3.0
	return viewport_size.x / viewport_size.y

func _safe_track_normal() -> Vector3:
	if track_surface_normal.length_squared() > 0.0001:
		return track_surface_normal.normalized()
	if basis_physical.basis.y.length_squared() > 0.0001:
		return basis_physical.basis.y.normalized()
	return Vector3.UP

func _step_gameplay_camera() -> void:
	if !local_visual_enabled or !gameplay_camera:
		return
	var view_up_pressed := Input.is_action_just_pressed("CameraUp")
	var view_down_pressed := Input.is_action_just_pressed("CameraDown")
	gameplay_camera.step(
		position_current,
		position_old,
		basis_physical,
		_safe_track_normal(),
		track_surface_pos,
		height_above_track,
		speed_kmh,
		camera_reorienting,
		camera_repositioning,
		turn_reaction_effect,
		machine_state,
		state_2,
		tilt_fl_state,
		tilt_fr_state,
		tilt_bl_state,
		tilt_br_state,
		restore_state,
		restore_move_frames,
		_camera_aspect_ratio(),
		view_up_pressed,
		view_down_pressed)

func _apply_gameplay_camera(ratio: float) -> void:
	if !local_visual_enabled or !gameplay_camera:
		return
	car_camera.global_transform = gameplay_camera.get_render_transform(ratio)
	car_camera.fov = gameplay_camera.get_render_fov(ratio)
	car_camera.near = 0.25
	car_camera.far = 40000.0

@onready var attack_particles: GPUParticles3D = $CarTransform/AttackParticles
@onready var landing_particles: GPUParticles3D = $CarTransform/LandingParticles
@onready var boost_electricity: BoostElectricity = $BoostElectricity

func _physics_process(delta):
	rollback_offset_error_prev = rollback_offset_error
	rollback_rot_error_prev    = rollback_rot_error

	rollback_offset_error = damp_vec3(
		rollback_offset_error, Vector3.ZERO, 20, delta)
	rollback_rot_error    = damp_basis(
		rollback_rot_error, Basis.IDENTITY, 20, delta)
	create_machine_visual_transform()
	#DebugDraw2D.set_text("current_checkpoint", current_checkpoint)
	frame_accumulation = 0.0
	car_old_pc = car_desired_pc
	car_desired_pc = position_current
	car_old_po = car_desired_po
	car_desired_po = position_old
	car_old_vel = car_desired_vel
	car_desired_vel = velocity
	car_old_transform = car_desired_transform
	car_desired_transform = transform_visual
	car_desired_transform.basis *= rollback_rot_error
	car_desired_transform.origin += rollback_offset_error
	car_old_basis_physical = car_desired_basis_physical
	car_desired_basis_physical = basis_physical
	old_ts_normal = desired_ts_normal
	desired_ts_normal = track_surface_normal
	
	var use_vy := remap(clampf(absf(velocity.y), 0, 5000), 0, 5000, 0, 1)
	if effect_tier == EffectTier.FULL:
		recharge_particles.emitting = (terrain_state & FZ_TERRAIN.RECHARGE) != 0
		attack_particles.emitting = (machine_state & FZ_MS.SIDEATTACKING) != 0 or (machine_state & FZ_MS.SPINATTACKING) != 0
		if (machine_state & FZ_MS.JUSTLANDED) != 0:
			landing_particles.restart()
			landing_particles.emitting = true
	else:
		recharge_particles.emitting = false
		attack_particles.emitting = false
		landing_particles.emitting = false
	terrain_state_old = terrain_state

var is_predicted = true

func just_rendered() -> void:
	is_predicted = false
		
	if (machine_state & FZ_MS.JUST_PRESSED_BOOST) != 0 or (machine_state & FZ_MS.JUST_HIT_DASHPLATE) != 0:
		car_overlay_colour += Color.SKY_BLUE * 0.75
	if (machine_state & FZ_MS.SPINATTACKING) != 0 or (machine_state & FZ_MS.SIDEATTACKING) != 0:
		car_overlay_colour = car_overlay_colour.lerp(Color.YELLOW * 0.5, 0.5)
	if s_boost_active:
		car_overlay_colour = car_overlay_colour.lerp(Color(1.0, 0.9, 0.3), 0.6)
	if (terrain_state & FZ_TERRAIN.RECHARGE) != 0:
		car_overlay_colour += Color.MAGENTA * 0.018
	car_overlay_colour = car_overlay_colour.lerp(Color.BLACK, 0.03)
		#var new_boost_effect := preload("res://asset/effect/boost_effect.tscn").instantiate()
		#new_boost_effect.position = Vector3(0, 0, -5)
		#new_boost_effect.life_time = 1000
		#car_camera.add_child(new_boost_effect)

var track_surface_smoothed := Vector3.UP

func _process(delta: float) -> void:
	frame_accumulation += delta
	delta = minf(1.0, delta)
	var ratio := frame_accumulation * Engine.physics_ticks_per_second
	ratio = minf(1.0, ratio)
	#ratio = 1.0
	#print(ratio)
	#print("----")
	#print(delta)
	#print(frame_accumulation)
	var use_car_pos := car_old_pc.lerp(car_desired_pc, ratio)
	var use_car_pos_old := car_old_po.lerp(car_desired_po, ratio)
	var use_basis_physical := car_old_basis_physical.interpolate_with(car_desired_basis_physical, ratio)
	var use_car_vel := car_old_vel.lerp(car_desired_vel, ratio)
	var use_ts_normal := old_ts_normal.lerp(desired_ts_normal, ratio)
	if game_manager != null and game_manager.game_sim != null:
		var native_render_transform := game_manager.game_sim.get_player_render_transform(owning_id)
		car_transform.global_transform = native_render_transform
		use_car_pos = native_render_transform.origin
	else:
		car_transform.global_transform = car_old_transform.interpolate_with(car_desired_transform, ratio)
	track_surface_smoothed = damp_vec3(track_surface_smoothed, use_ts_normal, 30, delta)
	#DebugDraw2D.set_text("rollback offset error", interp_err)
	#car_transform.global_transform.origin += interp_err
	#car_transform.global_transform.basis = car_transform.global_transform.basis * interp_rerr
	var calced_max_energy := 100.0
	var energy_ratio : float = minf(1.0, (energy / calced_max_energy) * 4.0)
	#var manual_boost_visual := float(boost_frames_manual) / (car_definition.boost_length * Engine.physics_ticks_per_second)
	#var dashplate_visual := float(boost_frames) / (car_definition.boost_length * Engine.physics_ticks_per_second * 0.5)
	#var boost_ratio : float = dashplate_visual if (machine_state & FZ_MS.BOOSTING_DASHPLATE) else manual_boost_visual
	var energy_flash := Color(0.04, -0.01, -0.01) * (sin(0.015 * Time.get_ticks_msec()) * 0.5 + 0.5) * (1.0 - energy_ratio)
	#var boost_flash := Color(0, 0.03, 0.075) * (boost_ratio)
	#var final_overlay := energy_flash + boost_flash + Color(1, 1, 1) * damage * 0.1
	
	#var flat_use_z := car_transform.global_basis.z.slide(use_ts_normal).normalized()
	#var flat_use_y := car_transform.global_basis.y.slide(flat_use_z).normalized()
	#car_camera.global_position = car_transform.global_transform.origin
	#car_camera.global_position += car_transform.global_basis.y * 1.2
	#car_camera.global_position += car_transform.global_basis.z * -1.0
	#car_camera.global_basis = car_camera.global_basis.slerp(car_transform.global_basis, 0.25)
	#car_camera.fov = 90
	
	if effect_tier == EffectTier.FULL and (boost_frames > 0 or boost_frames_manual > 0) and (machine_state & FZ_MS.AIRBORNE) == 0:
		boost_electricity.boosting = true
		if is_instance_valid(vehicle_shadow):
			boost_electricity.ground = Plane(_safe_track_normal(), vehicle_shadow.global_position)
	else:
		boost_electricity.boosting = false
	if effect_tier == EffectTier.FULL:
		boost_electricity.tendril_lifetime = remap(speed_kmh, 0, 3000, 0.3, 0.1)
		boost_electricity.calculate_electricity(delta, car_transform.global_transform)
	is_predicted = true
	#DebugDraw2D.set_text("current_checkpoint", current_checkpoint)
