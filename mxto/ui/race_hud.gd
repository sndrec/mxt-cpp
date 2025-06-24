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
	
       # Determine race placements based on each car's lap and lap progress.
       var cars : Array[VisualCar] = []
       for c in game_manager.car_node_container.get_children():
               if c is VisualCar:
                       cars.append(c)

       cars.sort_custom(func(a:VisualCar, b:VisualCar) -> bool:
               if a.lap == b.lap:
                       return a.lap_progress > b.lap_progress
               return a.lap > b.lap)

       var our_place := 1
       var local_id := multiplayer.get_unique_id() if multiplayer else 0
       for i in cars.size():
               if cars[i] == car or cars[i].owning_id == local_id:
                       our_place = i + 1
               if i < leaderboard_container.get_child_count():
                       var label := leaderboard_container.get_child(i)
                       var name := str(cars[i].owning_id)
                       if Engine.has_singleton("Net") and Net.peer_map.has(cars[i].owning_id):
                               name = Net.peer_map[cars[i].owning_id].player_settings.username
                       label.text = str(i + 1) + ". " + name
       for i in range(cars.size(), leaderboard_container.get_child_count()):
               leaderboard_container.get_child(i).text = ""

       var tex_index := clamp(our_place - 1, 0, placement_textures.size() - 1)
       place_badge.texture = placement_textures[tex_index]
	
       var move_vec := Vector2(Input.get_axis("MoveLeft", "MoveRight"), Input.get_axis("MoveForward", "MoveBack"))
       var clamped_move_vec := move_vec
       clamped_input.modulate = Color(1, 1, 1)
       real_input.modulate = Color(1, 1, 1)
       if clamped_move_vec.length() >= 0.999:
               clamped_move_vec = clamped_move_vec.normalized()
               clamped_input.modulate = Color(1, 0, 0)
       if move_vec.x > 0.999 or move_vec.x < -0.999 or move_vec.y > 0.999 or move_vec.y < -0.999:
               real_input.modulate = Color(1, 0, 0)
       real_input.position = Vector2(124, 124) + (move_vec * 72)
       clamped_input.position = Vector2(124, 124) + (clamped_move_vec * 72)
	
