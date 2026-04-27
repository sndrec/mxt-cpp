class_name RaceHud extends Control

@onready var speedometer := %speedometer
@onready var lapcounter := %lapcounter
@onready var racetimer := %racetimer
@onready var healthmeter := %healthmeter
@onready var countdowncontrol := $countdowncontrol as Control
@onready var countdown_arrow := $countdowncontrol/countdown_arrow as TextureRect
@onready var leaderboard_container := $Control/leaderboard_container
@onready var place_badge := $PlaceBadge as Control
@onready var minimap_rect := $MinimapControl/TextureRect
@onready var sub_viewport := $MinimapControl/SubViewport
@onready var minimap_cam := $MinimapControl/SubViewport/Camera3D
@onready var minimap_mesh := $MinimapControl/SubViewport/MeshInstance3D
#@onready var race_placement_hud := $RacePlacementHud
@onready var check_control: Control = $CheckControl
@onready var sboost_meter_bg: ColorRect = %sboost_meter_bg
@onready var sboost_meter_fill: ColorRect = %sboost_meter_fill

var car_max_energy: float = 100.0
var boost_energy_use_rate: float = 1.0
var _sboost_full_width: float = 0.0
var focus_player_id := 0
const MAX_LEADERBOARD_ENTRIES := 5

@export var placement_digit_size := Vector2(96.0, 96.0)
@export var placement_digit_kerning := -24.0

var placement_digit_textures: Array[Texture2D] = [
	preload("res://ui/placements/mxt-0.png"),
	preload("res://ui/placements/mxt-1.png"),
	preload("res://ui/placements/mxt-2.png"),
	preload("res://ui/placements/mxt-3.png"),
	preload("res://ui/placements/mxt-4.png"),
	preload("res://ui/placements/mxt-5.png"),
	preload("res://ui/placements/mxt-6.png"),
	preload("res://ui/placements/mxt-7.png"),
	preload("res://ui/placements/mxt-8.png"),
	preload("res://ui/placements/mxt-9.png")]
var placement_digit_nodes: Array[TextureRect] = []

@onready var real_input := $InputViewer/RealInput
@onready var clamped_input := $InputViewer/ClampedInput

func _player_name_for_id(nm: NetworkManager, id: int) -> String:
	var name := str(id)
	var settings = nm.player_settings.get(id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		name = str(settings["username"])
	if nm.get_cpu_roster().has(id):
		name = "[CPU] " + name
	return name

func _ordered_race_ids(car: VisualCar, nm: NetworkManager) -> Array:
	var ordered: Array = []
	for id in nm.finish_order:
		if id != null and !ordered.has(id):
			ordered.append(id)
	var native_order: Array = car.game_manager.game_sim.get_race_order()
	for id in native_order:
		if !ordered.has(id):
			ordered.append(id)
	for id in nm.get_simulation_roster():
		if !ordered.has(id):
			ordered.append(id)
	return ordered

func _update_leaderboard(car: VisualCar, nm: NetworkManager, focus_id: int, fallback_place: int) -> int:
	if leaderboard_container == null:
		return fallback_place
	var entries: Array = []
	var ordered_ids := _ordered_race_ids(car, nm)
	for i in range(ordered_ids.size()):
		var id = ordered_ids[i]
		var place := int(nm.player_finish_placements[id]) if nm.player_finish_placements.has(id) else i + 1
		entries.append({
			"id": id,
			"name": _player_name_for_id(nm, id),
			"place": place,
		})
	var focus_index := ordered_ids.find(focus_id)
	if focus_index < 0:
		focus_index = clampi(fallback_place - 1, 0, maxi(entries.size() - 1, 0))
	var start_index := 0
	if entries.size() > MAX_LEADERBOARD_ENTRIES:
		start_index = clampi(focus_index - 2, 0, entries.size() - MAX_LEADERBOARD_ENTRIES)
	var visible_count := mini(MAX_LEADERBOARD_ENTRIES, entries.size() - start_index)
	var labels := leaderboard_container.get_children()
	for i in range(labels.size()):
		var label := labels[i] as Label
		if label == null:
			continue
		label.visible = i < visible_count
		if i >= visible_count:
			continue
		var entry: Dictionary = entries[start_index + i]
		label.text = "%d  %s" % [int(entry["place"]), str(entry["name"])]
	if focus_index >= 0 and focus_index < entries.size():
		return int(entries[focus_index]["place"])
	return fallback_place

func _ready() -> void:
	if get_parent() is VisualCar:
		var car: VisualCar = get_parent()
		var path: String = car.car_definition.car_definition
		if path != "" and FileAccess.file_exists(path):
			var f := FileAccess.open(path, FileAccess.READ)
			if f:
				f.seek(84)
				car_max_energy = f.get_float()
				if f.get_length() >= 196:
					f.seek(192)
					boost_energy_use_rate = f.get_float()
				f.close()
	if sboost_meter_bg:
		_sboost_full_width = sboost_meter_bg.size.x
	_set_place_badge(1)

func _set_place_badge(place: int) -> void:
	var digits := str(maxi(place, 1))
	while placement_digit_nodes.size() < digits.length():
		var rect := TextureRect.new()
		rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
		rect.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
		rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		place_badge.add_child(rect)
		placement_digit_nodes.append(rect)

	var digit_count := digits.length()
	var step := placement_digit_size.x + placement_digit_kerning
	var total_width := placement_digit_size.x + step * float(digit_count - 1)
	var scale_to_fit := 1.0
	if place_badge.size.x > 0.0 and total_width > place_badge.size.x:
		scale_to_fit = place_badge.size.x / total_width
	if place_badge.size.y > 0.0 and placement_digit_size.y * scale_to_fit > place_badge.size.y:
		scale_to_fit = place_badge.size.y / placement_digit_size.y
	var use_digit_size := placement_digit_size * scale_to_fit
	var use_step := step * scale_to_fit
	var use_total_width := use_digit_size.x + use_step * float(digit_count - 1)
	var start := (place_badge.size - Vector2(use_total_width, use_digit_size.y)) * 0.5

	for i in placement_digit_nodes.size():
		var rect := placement_digit_nodes[i]
		rect.visible = i < digit_count
		if i >= digit_count:
			continue
		var digit := int(digits.substr(i, 1))
		rect.texture = placement_digit_textures[digit]
		rect.position = start + Vector2(use_step * float(i), 0.0)
		rect.size = use_digit_size

func _process( _delta:float ) -> void:
	var car : VisualCar
	#var pl : ROPlayer
	if get_parent() is VisualCar:
		car = get_parent()
		#pl = car.get_parent()
	speedometer.text = str(roundi(car.speed_kmh)) + " km/h"
	lapcounter.text = "LAP " + str(car.lap) + "/3"
	var nm := car.game_manager.network_manager
	var use_tick := nm.get_race_tick()
	var local_id := multiplayer.get_unique_id() if multiplayer.multiplayer_peer else 0
	var place_id := focus_player_id
	if nm.player_finish_times.has(local_id):
		use_tick = nm.player_finish_times[local_id]
	elif nm.player_finish_times.has(car.owning_id):
		use_tick = nm.player_finish_times[car.owning_id]
	var time_elapsed : int = use_tick - 300
	var time_elapsed_float : float = float(time_elapsed) / 60
	var seconds : int = int(floor(time_elapsed_float)) % 60
	var milliseconds : int = int(floor(time_elapsed_float * 1000)) % 1000
	var minutes : int = floor(time_elapsed_float / 60)
	racetimer.text = str(minutes) + ":" + str(seconds) + "." + str(milliseconds)
	healthmeter.scale.x = car_max_energy * 0.01
	var health_meter_shader := healthmeter.material as ShaderMaterial
	health_meter_shader.set_shader_parameter("health_amount", car.energy)
	health_meter_shader.set_shader_parameter("max_health_amount", car_max_energy)
	health_meter_shader.set_shader_parameter("can_boost", car.lap > 1)
	var boost_health_total_cost : float = float(car.boost_frames_manual) * 0.1666666667 * boost_energy_use_rate
	health_meter_shader.set_shader_parameter("health_to_deplete", boost_health_total_cost)
	
	var time_until_start : float = float(300 - use_tick) / 60
	countdown_arrow.rotation_degrees = 360 - minf(270, (time_until_start * 90))
	if time_until_start <= 0 and countdowncontrol.modulate.a > 0:
		countdowncontrol.scale += Vector2(1, 1) * _delta * 4
		countdowncontrol.modulate.a = max(0, countdowncontrol.modulate.a - _delta * 4)
	
	var our_place := car.game_manager.game_sim.get_player_race_place(place_id)

	if nm.player_finish_placements.has(place_id):
		our_place = nm.player_finish_placements[place_id]
	
	our_place = _update_leaderboard(car, nm, place_id, our_place)
	_set_place_badge(our_place)
	
	var move_vec := Vector2(Input.get_axis("SteerLeft", "SteerRight"), Input.get_axis("SteerUp", "SteerDown"))
	var clamped_move_vec := move_vec
	clamped_input.modulate = Color(1, 1, 1)
	real_input.modulate = Color(1, 1, 1)
	if clamped_move_vec.length() >= 0.999:
		clamped_move_vec = clamped_move_vec.normalized()
		clamped_input.modulate = Color(1, 0, 0)
	if move_vec.x > 0.999 or move_vec.x < -0.999 or move_vec.y > 0.999 or move_vec.y < -0.999:
		real_input.modulate = Color(1, 0, 0)
	real_input.position = Vector2(112, 112) + (move_vec * 56)
	clamped_input.position = Vector2(112, 112) + (clamped_move_vec * 56)

	if sboost_meter_bg and sboost_meter_fill:
		var ratio := 0.0
		if car.s_boost_charge_max > 0:
			ratio = float(car.s_boost_charge) / float(car.s_boost_charge_max)
		ratio = clampf(ratio, 0.0, 1.0)
		sboost_meter_bg.visible = car.s_boost_active or ratio > 0.0
		var width := _sboost_full_width * ratio
		sboost_meter_fill.size.x = width
		if car.s_boost_active:
			sboost_meter_fill.color = Color(1.0, 0.96, 0.45, 1.0)
		elif car.s_boost_ready:
			sboost_meter_fill.color = Color(1.0, 0.82, 0.3, 1.0)
		else:
			sboost_meter_fill.color = Color(0.75, 0.65, 0.25, 1.0)
	
