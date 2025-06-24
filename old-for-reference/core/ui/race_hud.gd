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

@onready var real_input := $InputViewer/RealInput
@onready var clamped_input := $InputViewer/ClampedInput

func _ready() -> void:
	await MXGlobal.currentStageOverseer.stageLoadedSignal
	
	var has_pit := MXGlobal.currentStage.has_node("pit")
	var has_rough := MXGlobal.currentStage.has_node("rough")
	#var has_slip := MXGlobal.currentStage.has_node("slip")
	
	var road := MXGlobal.currentStage.get_node("track").get_child(0) as MeshInstance3D
	
	minimap_mesh.mesh = road.mesh.duplicate(true)
	
	var road_material := StandardMaterial3D.new()
	road_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	road_material.albedo_color = Color(1, 1, 1, 1.0)
	road_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	minimap_mesh.set_surface_override_material(0, road_material)
	
	if has_pit:
		var pit := MXGlobal.currentStage.get_node("pit").get_child(0) as MeshInstance3D
		var pit_mesh := MeshInstance3D.new()
		pit_mesh.mesh = pit.mesh.duplicate(true)
		var pit_material := StandardMaterial3D.new()
		pit_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		pit_material.albedo_color = Color(1, 0.3, 0.5, 1.0)
		pit_material.cull_mode = BaseMaterial3D.CULL_DISABLED
		pit_mesh.set_surface_override_material(0, pit_material)
		sub_viewport.add_child(pit_mesh)
	
	if has_rough:
		var rough := MXGlobal.currentStage.get_node("rough").get_child(0) as MeshInstance3D
		var rough_mesh := MeshInstance3D.new()
		rough_mesh.mesh = rough.mesh.duplicate(true)
		var rough_material := StandardMaterial3D.new()
		rough_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		rough_material.albedo_color = Color(0.5, 0.2, 0.2, 1.0)
		rough_material.cull_mode = BaseMaterial3D.CULL_DISABLED
		rough_mesh.set_surface_override_material(0, rough_material)
		sub_viewport.add_child(rough_mesh)
	
	emote_1.texture = stickers.stickers[MXGlobal.local_settings.sticker_1]
	emote_2.texture = stickers.stickers[MXGlobal.local_settings.sticker_2]
	emote_3.texture = stickers.stickers[MXGlobal.local_settings.sticker_3]
	emote_4.texture = stickers.stickers[MXGlobal.local_settings.sticker_4]
	
	await RenderingServer.frame_post_draw
	minimap_rect.texture = sub_viewport.get_texture()
	
	await get_tree().create_timer(2.0).timeout
	
	for pl:ROPlayer in MXGlobal.currentStageOverseer.players:
		var player_dot := TextureRect.new()
		player_dot.texture = preload("res://content/base/common/circle.png")
		player_dot.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		player_dot.size = Vector2(6, 6)
		player_dot.pivot_offset = Vector2(3, 3)
		if pl == MXGlobal.localPlayer:
			player_dot.modulate = Color(1.0, 1.0, 0.2)
		minimap_rect.add_child(player_dot)
		
		if pl != MXGlobal.localPlayer:
			var check_icon := TextureRect.new()
			check_icon.texture = preload("res://content/base/texture/ui/check_incoming_vehicle.png")
			check_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			check_control.add_child(check_icon)
	
@onready var visual_steering_box := $VisualSteeringBox
@onready var visual_steering_box_guide := $VisualSteeringBox/VisualSteeringBox2

var minimap_mode := 0

@onready var emote_menu: Control = $EmoteMenu
@onready var emote_1: TextureRect = $EmoteMenu/Emote1
@onready var emote_2: TextureRect = $EmoteMenu/Emote2
@onready var emote_3: TextureRect = $EmoteMenu/Emote3
@onready var emote_4: TextureRect = $EmoteMenu/Emote4
var stickers := preload("res://content/base/texture/emote_sticker/sticker_selection.tres")
var sticker_menu_open := false



func _process( _delta:float ) -> void:
	var road_bounds : AABB = minimap_mesh.get_aabb()
	if !is_instance_valid(MXGlobal.currentStageOverseer):
		return
	var car : MXRacer
	var pl : ROPlayer
	if get_parent() is MXRacer:
		car = get_parent()
		pl = car.get_parent()
	else:
		var spec := get_parent() as Spectator
		if spec.spec_mode != 0:
			speedometer.visible = false
			lapcounter.visible = false
			healthmeter.visible = false
			place_badge.visible = false
		else:
			speedometer.visible = true
			lapcounter.visible = true
			healthmeter.visible = true
			place_badge.visible = true
		car = MXGlobal.currentStageOverseer.players[spec.specced_player].controlledPawn as MXRacer
		pl = MXGlobal.currentStageOverseer.players[spec.specced_player]
	visual_steering_box.visible = MXGlobal.mouse_driving_mode > 0
	visual_steering_box_guide.position = Vector2(MXGlobal.mouse_offset.x * -250 + 249, 0)
	for i in minimap_rect.get_children().size():
		var player := MXGlobal.currentStageOverseer.players[i]
		if !is_instance_valid(player):
			return
		var dot := minimap_rect.get_child(i) as TextureRect
		#var car_pos := player.controlledPawn.current_transform.origin - road_bounds.get_center()
		#dot.position = minimap_cam.unproject_position(player.controlledPawn.current_transform.origin) - Vector2(3, 3)
		if player == MXGlobal.localPlayer:
			dot.modulate = Color(1.0, 0.8, 0.1)
	for i in check_control.get_children().size():
		var player := MXGlobal.currentStageOverseer.players[i]
		if !is_instance_valid(player):
			continue
		if player == MXGlobal.localPlayer:
			continue
		#var check_icon := check_control.get_child(i) as TextureRect
		#var plane := Plane(car.current_transform.basis.z, car.current_position)
		#var intersect : Variant = plane.intersects_ray(player.controlledPawn.current_position, player.controlledPawn.current_transform.basis.z)
		#var dist := plane.distance_to(player.controlledPawn.current_position)
		#if intersect and dist < 0:
			#var px : float = (intersect - car.current_position).dot(car.current_transform.basis.x)
			#var dist_affector := clampf(remap(dist, -50, -1, 0, 1), 0, 1)
			#check_icon.modulate.a = dist_affector
			#check_icon.position.x = px * -128 + 640
			#check_icon.visible = true
			#check_icon.size = Vector2(128, 128)
			#check_icon.pivot_offset = Vector2(64, 128)
			#check_icon.position.y = 580
		#else:
			#check_icon.visible = false
		
	if minimap_mode == 0:
		minimap_cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		minimap_cam.size = maxf( road_bounds.get_longest_axis_size() * 1.0, 0.0001 )
		minimap_cam.rotation_degrees = Vector3(-25, 45, 0)
		minimap_cam.position = (minimap_cam.basis.z * minimap_cam.size * 1.5) + road_bounds.get_center()
		minimap_cam.near = 64
		minimap_cam.far = 32768
	elif minimap_mode == 1:
		minimap_cam.projection = Camera3D.PROJECTION_PERSPECTIVE
		#minimap_cam.basis = car.current_transform.basis.rotated(car.current_transform.basis.x, PI * -0.5)
		minimap_cam.basis = minimap_cam.basis.rotated(minimap_cam.basis.z, PI)
		#minimap_cam.position = car.current_position + minimap_cam.basis.z * 128
		minimap_cam.fov = 45
		minimap_cam.near = 96
		minimap_cam.far = 192
	#var top_speed : String = str(roundi(car.calculate_top_speed()))
	speedometer.text = str(roundi(car.speed_kmh)) + " km/h"
	lapcounter.text = "LAP " + str(car.lap) + "/" + str(MXGlobal.current_race_settings.laps)
	var time_elapsed : int = MXGlobal.currentStageOverseer.localTick - car.levelStartTime
	if (car.machine_state & MXRacer.FZ_MS.COMPLETEDRACE_2_Q) != 0:
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
	
	var time_until_start : float = float(car.levelStartTime - MXGlobal.currentStageOverseer.localTick) / Engine.physics_ticks_per_second
	countdown_arrow.rotation_degrees = 360 - minf(270, (time_until_start * 90))
	if time_until_start <= 0 and countdowncontrol.modulate.a > 0:
		countdowncontrol.scale += Vector2(1, 1) * _delta * 4
		countdowncontrol.modulate.a = max(0, countdowncontrol.modulate.a - _delta * 4)
	
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
	
	
	if MXGlobal.localPlayer and !MXGlobal.localPlayer.focused:
		return
	if Time.get_ticks_msec() > emote_input_buffer_time:
		if Input.is_action_just_pressed("DPadLeft") and sticker_menu_open:
			#car.request_broadcast_sticker.rpc_id(1, MXGlobal.local_settings.sticker_1)
			sticker_menu_open = false
			emote_menu.visible = false
			emote_input_buffer_time = Time.get_ticks_msec() + 50
		if Input.is_action_just_pressed("DPadDown") and sticker_menu_open:
			#car.request_broadcast_sticker.rpc_id(1, MXGlobal.local_settings.sticker_2)
			sticker_menu_open = false
			emote_menu.visible = false
			emote_input_buffer_time = Time.get_ticks_msec() + 50
		if Input.is_action_just_pressed("DPadUp") and sticker_menu_open:
			#car.request_broadcast_sticker.rpc_id(1, MXGlobal.local_settings.sticker_3)
			sticker_menu_open = false
			emote_menu.visible = false
			emote_input_buffer_time = Time.get_ticks_msec() + 50
		if Input.is_action_just_pressed("DPadRight") and sticker_menu_open:
			#car.request_broadcast_sticker.rpc_id(1, MXGlobal.local_settings.sticker_4)
			sticker_menu_open = false
			emote_menu.visible = false
			emote_input_buffer_time = Time.get_ticks_msec() + 50
			
	#if Time.get_ticks_msec() > emote_input_buffer_time:
		#if Input.is_action_just_pressed("StickerMenu") and sticker_menu_open == false:
			#sticker_menu_open = true
			#emote_menu.visible = true
			#emote_input_buffer_time = Time.get_ticks_msec() + 50
	
	if Input.is_action_just_pressed("MinimapModeToggle"):
		minimap_mode = wrapi(minimap_mode + 1, 0, 2)
	#else:
		#leaderboard_container.visible = false

var emote_input_buffer_time := 0.0
