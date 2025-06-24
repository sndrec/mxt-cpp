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

func _physics_process(delta: float) -> void:
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
	car_transform.transform = transform_visual
	car_camera.fov = lerpf(car_camera.fov, target_fov, delta * 2)
	var use_forward_z : Vector3 = basis_physical.basis.z
	use_forward_z = use_forward_z.normalized()
	if (tilt_fl_state & FZ_TC.DRIFT) != 0:
		use_forward_z = -velocity.slide(basis_physical.basis.y.normalized()).normalized()
	
	var target_y := basis_physical.basis.y
	
	var starting_frames_past := frames_since_start_2 > 90
	
	if !slerped_up_y.is_equal_approx(target_y):
		slerped_up_y = slerped_up_y.slerp(target_y, 0.4).normalized()
	slerped_forward_z = slerped_forward_z.slerp(use_forward_z, 0.2).normalized()
	
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
			if speed_kmh > 1:
				var sideways := velocity.normalized().cross(track_normal_vis).normalized()
				var flattened := track_normal_vis.slide(sideways).normalized()
				var flattened_prev := track_normal_old_vis.slide(sideways).normalized()
				var road_angle_change := flattened.signed_angle_to(flattened_prev, sideways)
				var arc_length := speed_kmh * 100
				lerped_curvature = lerpf(lerped_curvature, road_angle_change / arc_length if !is_zero_approx(arc_length) else 0.0, 0.125)
				final_y = final_y.rotated(sideways, lerped_curvature * -2000)
				use_basis = use_basis.rotated(sideways, lerped_curvature * -2000)
	car_camera.position = position_current + final_y * remap(car_camera.fov, 50, 100, 6.0, 5.5) + use_basis.z * remap(car_camera.fov, 50, 100, 12.0, 6.0)
	car_camera.basis = use_basis.rotated(use_basis.x, deg_to_rad(-15))
	
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
