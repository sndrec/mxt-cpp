class_name RaceHud extends Control

@onready var speedometer := %speedometer
@onready var lapcounter := %lapcounter
@onready var racetimer := %racetimer
@onready var healthmeter := %healthmeter
@onready var countdowncontrol := $countdowncontrol as Control
@onready var countdown_arrow := $countdowncontrol/countdown_arrow as TextureRect
@onready var leaderboard_container := $Control/leaderboard_container
@onready var place_badge := $PlaceBadge as TextureRect
@onready var minimap_rect := $MinimapControl/TextureRect
@onready var sub_viewport := $MinimapControl/SubViewport
@onready var minimap_cam := $MinimapControl/SubViewport/Camera3D
@onready var minimap_mesh := $MinimapControl/SubViewport/MeshInstance3D
@onready var race_placement_hud := $RacePlacementHud
@onready var check_control: Control = $CheckControl

var placement_textures : Array[Texture] = [
	preload("res://ui/placements/mx-1.png"),
	preload("res://ui/placements/mx-2.png"),
	preload("res://ui/placements/mx-3.png"),
	preload("res://ui/placements/mx-4.png"),
	preload("res://ui/placements/mx-5.png"),
	preload("res://ui/placements/mx-6.png"),
	preload("res://ui/placements/mx-7.png"),
	preload("res://ui/placements/mx-8.png"),
	preload("res://ui/placements/mx-9.png"),
	preload("res://ui/placements/mx-10.png"),
	preload("res://ui/placements/mx-11.png"),
	preload("res://ui/placements/mx-12.png"),
	preload("res://ui/placements/mx-13.png"),
	preload("res://ui/placements/mx-14.png"),
	preload("res://ui/placements/mx-15.png"),
	preload("res://ui/placements/mx-16.png"),
	preload("res://ui/placements/mx-17.png"),
	preload("res://ui/placements/mx-18.png"),
	preload("res://ui/placements/mx-19.png"),
	preload("res://ui/placements/mx-20.png"),
	preload("res://ui/placements/mx-21.png"),
	preload("res://ui/placements/mx-22.png"),
	preload("res://ui/placements/mx-23.png"),
	preload("res://ui/placements/mx-24.png"),
	preload("res://ui/placements/mx-25.png"),
	preload("res://ui/placements/mx-26.png"),
	preload("res://ui/placements/mx-27.png"),
	preload("res://ui/placements/mx-28.png"),
	preload("res://ui/placements/mx-29.png"),
	preload("res://ui/placements/mx-30.png")]

@onready var real_input := $InputViewer/RealInput
@onready var clamped_input := $InputViewer/ClampedInput

var game_manager : GameManager

func _ready() -> void:
	pass

func _process( _delta:float ) -> void:
	var car : VisualCar
	#var pl : ROPlayer
	if get_parent() is VisualCar:
		car = get_parent()
		#pl = car.get_parent()
	speedometer.text = str(roundi(car.speed_kmh)) + " km/h"
	lapcounter.text = "LAP " + str(car.lap) + "/3"
	var time_elapsed : int = game_manager.network_manager.local_tick - car.levelStartTime
	if (car.machine_state & VisualCar.FZ_MS.COMPLETEDRACE_2_Q) != 0:
		time_elapsed = car.level_win_time - car.levelStartTime
	var time_elapsed_float : float = float(time_elapsed) / Engine.physics_ticks_per_second
	var seconds : int = int(floor(time_elapsed_float)) % 60
	var milliseconds : int = int(floor(time_elapsed_float * 1000)) % 1000
	var minutes : int = floor(time_elapsed_float / 60)
	racetimer.text = str(minutes) + ":" + str(seconds) + "." + str(milliseconds)
	healthmeter.scale.x = car.calced_max_energy * 0.01
	var health_meter_shader := healthmeter.material as ShaderMaterial
	health_meter_shader.set_shader_parameter("health_amount", car.energy)
	health_meter_shader.set_shader_parameter("max_health_amount", car.calced_max_energy)
	health_meter_shader.set_shader_parameter("can_boost", car.lap > 1)
	var boost_health_total_cost : float = 0.0#car.car_definition.boost_health_cost * (1.0 / car.car_definition.boost_length) * MXGlobal.tick_delta * car.boost_time
	health_meter_shader.set_shader_parameter("health_to_deplete", boost_health_total_cost)
	
	var time_until_start : float = float(car.levelStartTime - game_manager.network_manager.local_tick) / 60
	countdown_arrow.rotation_degrees = 360 - minf(270, (time_until_start * 90))
	if time_until_start <= 0 and countdowncontrol.modulate.a > 0:
		countdowncontrol.scale += Vector2(1, 1) * _delta * 4
		countdowncontrol.modulate.a = max(0, countdowncontrol.modulate.a - _delta * 4)
	
	var placements = []
	placements.resize(game_manager.car_node_container.num_cars)
	
	## TODO: assign player id to each visual car
	## so that we can fill and sort this array
	## with peer IDs and determine our placement
	## using a combination of each visual car's
	## lap and lap progress
	
	place_badge.texture = placement_textures[pl.place]
	
	var move_vec := Vector2(Input.get_axis("MoveLeft", "MoveRight"), Input.get_axis("MoveForward", "MoveBack"))
	var clamped_move_vec := move_vec
	clamped_input.modulate = Color(1, 1, 1)
	real_input.modulate = Color(1, 1, 1)
	if clamped_move_vec.length() >= 0.999:
		clamped_move_vec = clamped_move_vec.normalized()
		clamped_input.modulate = Color(1, 0, 0)
	if move_vec.x > 0.999 or move_vec.x < -0.999 or move_vec.y > 0.999 or move_vec.y < -0.999:
		#print(move_vec)
		real_input.modulate = Color(1, 0, 0)
	real_input.position = Vector2(124, 124) + (move_vec * 72)
	clamped_input.position = Vector2(124, 124) + (clamped_move_vec * 72)
	
	var place : int = 0
	if multiplayer and multiplayer.has_multiplayer_peer() and multiplayer.get_peers().size() > 0:
		for child in leaderboard_container.get_children():
			if MXGlobal.currentStageOverseer.places.size() - 1 >= place and is_instance_valid(MXGlobal.currentStageOverseer.places[place]):
				var board_pl := MXGlobal.currentStageOverseer.places[place] as ROPlayer
				child.text = str(place + 1) + ". " + Net.peer_map[board_pl.playerID].player_settings.username
			else:
				child.text = ""
			place += 1
	
