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
var stickers: StickerSelection = preload("res://ui/emote_sticker/sticker_selection.tres")
const CHECK_INCOMING_TEXTURE: Texture2D = preload("res://ui/check_incoming_vehicle.png")
const DPAD_TEXTURE: Texture2D = preload("res://ui/dpad.png")
var sticker_menu: Control
var sticker_menu_icons: Array[TextureRect] = []
var sticker_nodes := {}
var check_icons: Array[TextureRect] = []
var sticker_menu_hide_msec := 0

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

func _action_just_pressed_any(names: Array[String]) -> bool:
	for action in names:
		if InputMap.has_action(action) and Input.is_action_just_pressed(action):
			return true
	return false

func _sticker_slot_value(car: VisualCar, slot: int) -> int:
	var settings = car.player_settings
	if settings == null:
		return slot
	match slot:
		0:
			return int(settings.sticker_1)
		1:
			return int(settings.sticker_2)
		2:
			return int(settings.sticker_3)
		3:
			return int(settings.sticker_4)
	return slot

func _build_sticker_menu() -> void:
	sticker_menu = Control.new()
	sticker_menu.name = "EmoteMenu"
	sticker_menu.visible = false
	sticker_menu.mouse_filter = Control.MOUSE_FILTER_IGNORE
	sticker_menu.process_mode = Node.PROCESS_MODE_ALWAYS
	sticker_menu.anchor_left = 0.029
	sticker_menu.anchor_top = 0.446
	sticker_menu.anchor_right = 0.305
	sticker_menu.anchor_bottom = 0.904
	sticker_menu.offset_left = 53.88
	sticker_menu.offset_top = 116.88
	sticker_menu.offset_right = 54.6
	sticker_menu.offset_bottom = 117.12
	sticker_menu.grow_horizontal = Control.GROW_DIRECTION_BOTH
	sticker_menu.grow_vertical = Control.GROW_DIRECTION_BOTH
	sticker_menu.scale = Vector2(0.65, 0.65)
	add_child(sticker_menu)

	var dpad := _make_old_emote_rect("DPad", Vector2(-64.0, -64.0), Vector2(64.0, 64.0))
	dpad.texture = DPAD_TEXTURE
	sticker_menu.add_child(dpad)

	var offsets := [
		[Vector2(-196.0, -64.0), Vector2(-68.0, 64.0)],
		[Vector2(-64.0, 71.0), Vector2(64.0, 199.0)],
		[Vector2(-64.6923, -198.846), Vector2(63.3077, -70.8461)],
		[Vector2(71.0, -64.0), Vector2(199.0, 64.0)],
	]
	for i in range(4):
		var rect := _make_old_emote_rect("Emote%d" % (i + 1), offsets[i][0], offsets[i][1])
		sticker_menu.add_child(rect)
		sticker_menu_icons.append(rect)

func _make_old_emote_rect(node_name: String, offset_start: Vector2, offset_end: Vector2) -> TextureRect:
	var rect := TextureRect.new()
	rect.name = node_name
	rect.custom_minimum_size = Vector2(128.0, 128.0)
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	rect.anchor_left = 0.5
	rect.anchor_top = 0.5
	rect.anchor_right = 0.5
	rect.anchor_bottom = 0.5
	rect.offset_left = offset_start.x
	rect.offset_top = offset_start.y
	rect.offset_right = offset_end.x
	rect.offset_bottom = offset_end.y
	rect.grow_horizontal = Control.GROW_DIRECTION_BOTH
	rect.grow_vertical = Control.GROW_DIRECTION_BOTH
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	return rect

func _ensure_check_icons(count: int) -> void:
	check_control.visible = true
	while check_icons.size() < count:
		var icon := TextureRect.new()
		icon.texture = CHECK_INCOMING_TEXTURE
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		icon.size = Vector2(128.0, 128.0)
		icon.pivot_offset = Vector2(64.0, 128.0)
		check_control.add_child(icon)
		check_icons.append(icon)

func _send_sticker(car: VisualCar, slot: int) -> void:
	var sticker_index := _sticker_slot_value(car, slot)
	if stickers != null and stickers.stickers.size() > 0:
		sticker_index = wrapi(sticker_index, 0, stickers.stickers.size())
	car.game_manager.send_local_sticker(sticker_index)
	_update_sticker_menu_icons(car)
	sticker_menu.visible = true
	sticker_menu_hide_msec = Time.get_ticks_msec() + 650

func _update_sticker_menu_icons(car: VisualCar) -> void:
	if stickers == null or stickers.stickers.is_empty():
		return
	for i in range(sticker_menu_icons.size()):
		var sticker_index := wrapi(_sticker_slot_value(car, i), 0, stickers.stickers.size())
		sticker_menu_icons[i].texture = stickers.stickers[sticker_index]

func _update_sticker_input(car: VisualCar) -> void:
	if _action_just_pressed_any(["DpadLeft", "DPadLeft"]):
		_send_sticker(car, 0)
	elif _action_just_pressed_any(["DpadDown", "DPadDown"]):
		_send_sticker(car, 1)
	elif _action_just_pressed_any(["DpadUp", "DPadUp"]):
		_send_sticker(car, 2)
	elif _action_just_pressed_any(["DpadRight", "DPadRight"]):
		_send_sticker(car, 3)
	if sticker_menu != null and sticker_menu.visible and Time.get_ticks_msec() > sticker_menu_hide_msec:
		sticker_menu.visible = false

func _update_check_warnings(car: VisualCar) -> void:
	var candidates: Array = car.game_manager.game_sim.get_check_warning_candidates(car.owning_id)
	_ensure_check_icons(candidates.size())
	var viewport_size := get_viewport_rect().size
	for i in range(check_icons.size()):
		var icon := check_icons[i]
		icon.visible = i < candidates.size()
		if i >= candidates.size():
			continue
		var candidate: Dictionary = candidates[i]
		var lateral := float(candidate.get("lateral", 0.0))
		var alpha := float(candidate.get("alpha", 0.0))
		icon.modulate.a = alpha
		icon.position.x = clampf(viewport_size.x * 0.5 - lateral * 128.0, 0.0, viewport_size.x - icon.size.x)
		icon.position.y = minf(580.0, viewport_size.y - icon.size.y)

func _update_world_stickers(car: VisualCar) -> void:
	var camera := get_viewport().get_camera_3d()
	if camera == null or car.game_manager == null:
		return
	var active: Dictionary = car.game_manager.active_stickers
	for actor_id in active.keys():
		if !sticker_nodes.has(actor_id):
			var rect := TextureRect.new()
			rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
			rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			rect.size = Vector2(96.0, 96.0)
			add_child(rect)
			sticker_nodes[actor_id] = rect
	for actor_id in sticker_nodes.keys():
		var rect := sticker_nodes[actor_id] as TextureRect
		if !active.has(actor_id):
			rect.visible = false
			continue
		var data: Dictionary = active[actor_id]
		var sticker_index := int(data.get("sticker", 0))
		if stickers != null and stickers.stickers.size() > 0:
			rect.texture = stickers.stickers[wrapi(sticker_index, 0, stickers.stickers.size())]
		var render_transform: Transform3D = car.game_manager.game_sim.get_player_render_transform(int(actor_id))
		var world_pos := render_transform.origin + render_transform.basis.y * 3.0
		var to_sticker := world_pos - camera.global_position
		if camera.global_basis.z.dot(to_sticker) > 0.0:
			rect.visible = false
			continue
		rect.visible = true
		rect.size = Vector2(128.0, 128.0)
		rect.position = camera.unproject_position(world_pos) - rect.size * 0.5

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
	_build_sticker_menu()

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
	if car == null:
		return
	if focus_player_id == 0:
		focus_player_id = car.owning_id
	_update_sticker_input(car)
	_update_check_warnings(car)
	_update_world_stickers(car)
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
	car_max_energy = maxf(car.calced_max_energy, 1.0)
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
	
