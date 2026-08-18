class_name RacePresentationController
extends Node

const FinishMedalScene: PackedScene = preload("res://ui/finish_medal.tscn")
const KoMedalScene: PackedScene = preload("res://ui/ko_medal.tscn")
const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")
const TrackContentControllerClass = preload("res://track/track_content_controller.gd")
const CarSettingsClass = preload("res://ui/car_settings.gd")
const FinishMedalClass = preload("res://ui/finish_medal.gd")
const KoMedalClass = preload("res://ui/ko_medal.gd")
const TOP_PLACE_BADGE_TEXTURES: Array[Texture2D] = [
	preload("res://ui/placements/mxt-1.png"),
	preload("res://ui/placements/mxt-2.png"),
	preload("res://ui/placements/mxt-3.png"),
]
const RESULTS_SCREEN_MSEC := 15000
const NAMETAG_VISIBLE_BUDGET := 30
const NAMETAG_MAX_DISTANCE_SQ := 12000.0

@onready var notification_label: Label = $"../RaceFinishLabel"
@onready var results_overlay: RaceResultsOverlay = $RaceResultsOverlay

var network_manager: NetworkManager
var game_sim: GameSim
var server_game_sim: GameSim
var replay_controller: ReplayController
var track_content_controller: TrackContentControllerClass
var car_node_container: CarNodeContainer
var car_settings: CarSettingsClass
var local_player_id := -1
var local_player_index := -1
var singleplayer_mode := false
var notification_hide_msec := 0
var active_stickers := {}
var medals: Array[Control] = []
var next_accel_setting := -1.0
var results_hid_race_hud := false
var results_saved_race_hud_visible := false
var nametag_pool: Array[Label] = []
var nametag_pool_car_indices: Array[int] = []
var nametag_pool_pending_indices: Array[int] = []
var nametag_names: Array[String] = []
var nametag_best_distances: Array[float] = []
var nametag_best_indices: Array[int] = []
var placement_badge_pool: Array[TextureRect] = []

func initialize(
	in_network_manager: NetworkManager,
	in_game_sim: GameSim,
	in_server_game_sim: GameSim,
	in_replay_controller: ReplayController,
	in_track_content_controller: TrackContentControllerClass,
	in_car_node_container: CarNodeContainer,
	in_car_settings: CarSettingsClass
) -> void:
	network_manager = in_network_manager
	game_sim = in_game_sim
	server_game_sim = in_server_game_sim
	replay_controller = in_replay_controller
	track_content_controller = in_track_content_controller
	car_node_container = in_car_node_container
	car_settings = in_car_settings
	results_overlay.machine_setting_changed.connect(_on_machine_setting_changed)
	hide_results()

func configure_race(in_local_player_id: int, in_local_player_index: int, in_singleplayer_mode: bool, racer_names: Array[String]) -> void:
	local_player_id = in_local_player_id
	local_player_index = in_local_player_index
	singleplayer_mode = in_singleplayer_mode
	next_accel_setting = -1.0
	active_stickers.clear()
	nametag_names = racer_names.duplicate()
	hide_results()
	_configure_nametag_pool()

func reset() -> void:
	hide_results()
	active_stickers.clear()
	notification_hide_msec = 0
	notification_label.visible = false
	_reset_nametag_pool()
	for medal in medals:
		if is_instance_valid(medal):
			medal.queue_free()
	medals.clear()
	next_accel_setting = -1.0
	local_player_id = -1
	local_player_index = -1

func update() -> void:
	var now_msec := Time.get_ticks_msec()
	for player_id in active_stickers.keys():
		var data: Dictionary = active_stickers[player_id]
		if now_msec > int(data.get("expires", 0)):
			active_stickers.erase(player_id)
	if notification_label.visible and notification_hide_msec > 0 and now_msec > notification_hide_msec and network_manager.race_results.net_race_finish_time == -1:
		notification_label.visible = false
		notification_hide_msec = 0
	if game_sim.sim_started and network_manager.race_results.net_race_finish_time != -1 and !replay_controller.replay_playback_active:
		show_results()

func show_notification(text: String, duration_msec := 2200) -> void:
	notification_label.text = text
	notification_label.visible = true
	notification_hide_msec = Time.get_ticks_msec() + duration_msec

func show_results() -> void:
	if replay_controller.replay_playback_active:
		hide_results()
		return
	notification_label.visible = false
	_set_results_hud_hidden(true)
	results_overlay.set_results(format_race_results_text(), format_grand_prix_results_text())
	results_overlay.set_countdown_seconds(_results_countdown_seconds())
	var next_track_id := _next_grand_prix_track_id()
	results_overlay.set_next_race(
		track_content_controller.track_name_for_id(next_track_id),
		_local_player_accel_setting(),
		!next_track_id.is_empty())
	results_overlay.visible = true
	notification_hide_msec = 0

func hide_results() -> void:
	if results_overlay != null:
		results_overlay.visible = false
		results_overlay.set_countdown_seconds(-1)
		results_overlay.set_next_race("", 1.0, false)
		results_overlay.clear_time_attack_result()
	_set_results_hud_hidden(false)


func show_time_attack_result(result: Dictionary, previous_best_milliseconds: int, status_message: String) -> void:
	results_overlay.set_time_attack_result(result, previous_best_milliseconds)
	results_overlay.set_time_attack_submission_status(status_message)


func update_time_attack_submission_status(status_message: String) -> void:
	if results_overlay.visible and results_overlay.time_attack_panel.visible:
		results_overlay.set_time_attack_submission_status(status_message)

func race_results_start_tick() -> int:
	var sim := _results_sim()
	if sim != null and sim.has_method("get_player_level_start_time"):
		if local_player_id != 0:
			var local_start := int(sim.get_player_level_start_time(local_player_id))
			if local_start > 0:
				return local_start
		for id_value in network_manager.get_simulation_roster():
			var start_tick := int(sim.get_player_level_start_time(int(id_value)))
			if start_tick > 0:
				return start_tick
	return 300

func format_race_time(tick_value: int, official_start_tick := -1) -> String:
	if official_start_tick < 0:
		official_start_tick = race_results_start_tick()
	var total_msec := TimeAttackRulesClass.finish_ticks_to_milliseconds(tick_value, official_start_tick)
	return "%d:%02d.%03d" % [int(total_msec / 60000), int(total_msec / 1000) % 60, total_msec % 1000]

func format_race_results_text() -> String:
	var lines := ["Race Results"]
	var finish_rows := []
	for id_value in network_manager.race_results.player_finish_placements.keys():
		finish_rows.append([int(network_manager.race_results.player_finish_placements[id_value]), int(id_value)])
	finish_rows.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) < int(b[0])
		return int(a[1]) < int(b[1]))
	for row in finish_rows:
		var place := int(row[0])
		var player_id := int(row[1])
		var time_text := ""
		var tick_value := int(_lookup_id_value(network_manager.race_results.player_finish_times, player_id, -1))
		if tick_value >= 0:
			time_text = "  " + format_race_time(tick_value)
		lines.append("%s  %s%s" % [_format_ordinal(place), player_display_name(player_id), time_text])
	if !network_manager.race_results.player_eliminations.is_empty():
		lines.append("")
		lines.append("Eliminated")
		for id_value in network_manager.race_results.player_eliminations.keys():
			lines.append(player_display_name(int(id_value)))
	if !network_manager.race_results.player_dnfs.is_empty():
		lines.append("")
		lines.append("DNF")
		for id_value in network_manager.race_results.player_dnfs.keys():
			lines.append(player_display_name(int(id_value)))
	return "\n".join(lines)

func format_grand_prix_results_text() -> String:
	if !network_manager.is_grand_prix_enabled():
		return ""
	var lines := ["Grand Prix Standings"]
	var points: Dictionary = network_manager.race_options.get("grand_prix_points", {})
	var standings := []
	for id_value in points.keys():
		standings.append([int(_lookup_id_value(points, int(id_value), 0)), int(id_value)])
	standings.sort_custom(func(a, b):
		if int(a[0]) != int(b[0]):
			return int(a[0]) > int(b[0])
		return int(a[1]) < int(b[1]))
	for i in range(standings.size()):
		lines.append("%s  %s  %d" % [_format_ordinal(i + 1), player_display_name(int(standings[i][1])), int(standings[i][0])])
	return "\n".join(lines)

func player_display_name(player_id: int) -> String:
	if player_id < 0:
		return "Bumper"
	var player_name := str(player_id)
	var settings = network_manager.lobby_settings.player_settings.get(player_id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		player_name = str(settings["username"])
	if network_manager.lobby_settings.get_cpu_roster().has(player_id):
		player_name = "[CPU] " + player_name
	return player_name

func show_finish_medal(actor_id: int, tick_value: int) -> void:
	var medal := FinishMedalScene.instantiate() as FinishMedalClass
	_add_medal(medal)
	medal.set_finisher_name(player_display_name(actor_id), format_race_time(tick_value))

func show_ko_medal(actor_id: int, target_id: int) -> void:
	var medal := KoMedalScene.instantiate() as KoMedalClass
	_add_medal(medal)
	medal.set_names(player_display_name(actor_id), "Obstacle" if target_id < 0 else player_display_name(target_id))

func show_sticker(actor_id: int, sticker_index: int) -> void:
	var now := Time.get_ticks_msec()
	active_stickers[actor_id] = {"sticker": sticker_index, "started": now, "expires": now + 2200}

func clear_stickers() -> void:
	active_stickers.clear()

func send_local_sticker(sticker_index: int) -> void:
	if singleplayer_mode or !network_manager.has_network_peer():
		show_sticker(local_player_id, sticker_index)
	else:
		network_manager.race_results.send_sticker(sticker_index)

func update_nametags(active_camera: Camera3D, delta: float, hud_disabled: bool) -> void:
	if hud_disabled or active_camera == null or nametag_pool.is_empty():
		return
	var camera_position := active_camera.global_position
	var camera_right := active_camera.global_basis.x
	var camera_up := active_camera.global_basis.y
	var car_count := nametag_names.size()
	for slot in NAMETAG_VISIBLE_BUDGET:
		nametag_best_distances[slot] = INF
		nametag_best_indices[slot] = -1
	for car_index in car_count:
		if car_index == local_player_index:
			continue
		var world_pos: Vector3 = game_sim.get_car_render_transform(car_index).origin
		var distance_sq := camera_position.distance_squared_to(world_pos)
		if distance_sq > NAMETAG_MAX_DISTANCE_SQ or !active_camera.is_position_in_frustum(world_pos):
			continue
		if distance_sq >= nametag_best_distances[NAMETAG_VISIBLE_BUDGET - 1]:
			continue
		var insert_at := NAMETAG_VISIBLE_BUDGET - 1
		while insert_at > 0 and distance_sq < nametag_best_distances[insert_at - 1]:
			nametag_best_distances[insert_at] = nametag_best_distances[insert_at - 1]
			nametag_best_indices[insert_at] = nametag_best_indices[insert_at - 1]
			insert_at -= 1
		nametag_best_distances[insert_at] = distance_sq
		nametag_best_indices[insert_at] = car_index
	for slot in NAMETAG_VISIBLE_BUDGET:
		var label := nametag_pool[slot]
		var car_index := nametag_pool_car_indices[slot]
		if car_index < 0:
			label.visible = false
			label.modulate.a = 0.0
			continue
		if car_index >= car_count:
			_release_nametag_slot(label, slot)
			continue
		var world_pos: Vector3 = game_sim.get_car_render_transform(car_index).origin
		if !active_camera.is_position_in_frustum(world_pos) or camera_position.distance_squared_to(world_pos) > NAMETAG_MAX_DISTANCE_SQ:
			_release_nametag_slot(label, slot)
			continue
		if !_nametag_best_contains(car_index):
			label.modulate.a = maxf(0.0, label.modulate.a - delta * 12.0)
			if label.modulate.a <= 0.0:
				label.visible = false
				nametag_pool_car_indices[slot] = -1
			continue
		label.visible = true
		label.modulate.a = minf(1.0, label.modulate.a + delta * 20.0)
		label.position = active_camera.unproject_position(world_pos + camera_right * 1.5 + camera_up * 1.5) + Vector2(72, -90)
	_update_top_place_badges(active_camera, camera_position, camera_right, camera_up)
	for desired_slot in NAMETAG_VISIBLE_BUDGET:
		var desired_car_index := nametag_best_indices[desired_slot]
		if desired_car_index < 0 or _nametag_pool_has_car(desired_car_index):
			continue
		var target_pool_slot := -1
		for slot in NAMETAG_VISIBLE_BUDGET:
			if nametag_pool_car_indices[slot] == -1:
				target_pool_slot = slot
				break
		if target_pool_slot == -1:
			for slot in NAMETAG_VISIBLE_BUDGET:
				if nametag_pool_pending_indices[slot] == -1 and !_nametag_best_contains(nametag_pool_car_indices[slot]):
					nametag_pool_pending_indices[slot] = desired_car_index
					target_pool_slot = slot
					break
		if target_pool_slot >= 0:
			var label := nametag_pool[target_pool_slot]
			if nametag_pool_car_indices[target_pool_slot] == -1 or label.modulate.a <= 0.0:
				_nametag_assign(label, target_pool_slot, desired_car_index)
	for slot in NAMETAG_VISIBLE_BUDGET:
		var pending_index := nametag_pool_pending_indices[slot]
		if pending_index < 0:
			continue
		var label := nametag_pool[slot]
		label.modulate.a = maxf(0.0, label.modulate.a - delta * 12.0)
		if label.modulate.a <= 0.0:
			_nametag_assign(label, slot, pending_index)
	for slot in NAMETAG_VISIBLE_BUDGET:
		var car_index := nametag_pool_car_indices[slot]
		if car_index < 0:
			continue
		var world_pos: Vector3 = game_sim.get_car_render_transform(car_index).origin
		var label := nametag_pool[slot]
		label.visible = true
		label.position = active_camera.unproject_position(world_pos + camera_right * 1.5 + camera_up * 1.5) + Vector2(72, -90)

func _results_sim() -> GameSim:
	return server_game_sim if network_manager.is_server and server_game_sim != null else game_sim

func _format_ordinal(value: int) -> String:
	var mod_100 := value % 100
	if mod_100 >= 11 and mod_100 <= 13:
		return "%dth" % value
	match value % 10:
		1: return "%dst" % value
		2: return "%dnd" % value
		3: return "%drd" % value
		_: return "%dth" % value

func _next_grand_prix_track_id() -> String:
	if !network_manager.is_grand_prix_enabled():
		return ""
	var track_ids: Array = network_manager.race_options.get("track_ids", [])
	var next_index := int(network_manager.race_options.get("grand_prix_current_track", 0)) + 1
	return String(track_ids[next_index]) if next_index >= 0 and next_index < track_ids.size() else ""

func _results_countdown_seconds() -> int:
	if network_manager.race_results.net_race_finish_time < 0:
		return -1
	return ceili(float(maxi(0, RESULTS_SCREEN_MSEC - (Time.get_ticks_msec() - network_manager.race_results.net_race_finish_time))) / 1000.0)

func _local_player_accel_setting() -> float:
	if next_accel_setting >= 0.0:
		return next_accel_setting
	var settings = network_manager.lobby_settings.player_settings.get(local_player_id, {})
	if typeof(settings) == TYPE_DICTIONARY and (settings as Dictionary).has("accel_setting"):
		return clampf(float((settings as Dictionary)["accel_setting"]), 0.0, 1.0)
	return clampf(car_settings.player_settings.accel_setting, 0.0, 1.0) if car_settings != null else 1.0

func _on_machine_setting_changed(accel_setting: float) -> void:
	if _next_grand_prix_track_id().is_empty():
		return
	next_accel_setting = clampf(accel_setting, 0.0, 1.0)
	var settings = network_manager.lobby_settings.player_settings.get(local_player_id, {})
	settings = (settings as Dictionary).duplicate(true) if typeof(settings) == TYPE_DICTIONARY else {}
	if settings.is_empty() and car_settings != null:
		settings = car_settings.player_settings.to_dict()
	settings["accel_setting"] = next_accel_setting
	network_manager.lobby_settings.player_settings[local_player_id] = settings
	if car_settings != null:
		car_settings.player_settings.accel_setting = next_accel_setting
	network_manager.lobby_settings.send_next_race_accel_setting(next_accel_setting)

func _set_results_hud_hidden(hidden: bool) -> void:
	var race_hud := local_race_hud()
	if hidden:
		if results_hid_race_hud or race_hud == null:
			return
		results_saved_race_hud_visible = race_hud.visible
		results_hid_race_hud = true
		race_hud.visible = false
	elif results_hid_race_hud:
		results_hid_race_hud = false
		if race_hud != null:
			race_hud.visible = results_saved_race_hud_visible

func local_race_hud() -> Control:
	return car_node_container.local_visual_car.race_hud as Control if car_node_container != null and car_node_container.local_visual_car != null else null

func _add_medal(medal: Control) -> void:
	add_child(medal)
	medal.tree_exited.connect(_refresh_medal_feed)
	medals.insert(0, medal)
	while medals.size() > 3:
		var oldest := medals.pop_back() as Control
		if oldest is FinishMedalClass:
			(oldest as FinishMedalClass).dismiss()
		elif oldest is KoMedalClass:
			(oldest as KoMedalClass).dismiss()
	_refresh_medal_feed()

func _refresh_medal_feed() -> void:
	medals = medals.filter(func(existing): return is_instance_valid(existing) and existing.is_inside_tree())
	for i in range(medals.size()):
		if medals[i] is FinishMedalClass:
			(medals[i] as FinishMedalClass).set_feed_index(i)
		elif medals[i] is KoMedalClass:
			(medals[i] as KoMedalClass).set_feed_index(i)

func _configure_nametag_pool() -> void:
	_reset_nametag_pool()
	var template_label: Label
	for car: VisualCar in car_node_container.get_children():
		if is_instance_valid(car.name_label):
			template_label = car.name_label
			break
	if template_label == null:
		return
	for slot in NAMETAG_VISIBLE_BUDGET:
		var label := template_label.duplicate() as Label
		label.name = "NametagPool%d" % slot
		label.visible = false
		label.modulate.a = 1.0
		add_child(label)
		nametag_pool.append(label)
		nametag_pool_car_indices.append(-1)
		nametag_pool_pending_indices.append(-1)
		nametag_best_distances.append(INF)
		nametag_best_indices.append(-1)
	for car: VisualCar in car_node_container.get_children():
		if is_instance_valid(car.name_label):
			car.name_label.queue_free()
	for place in TOP_PLACE_BADGE_TEXTURES.size():
		var badge := TextureRect.new()
		badge.name = "TopPlaceBadge%d" % (place + 1)
		badge.texture = TOP_PLACE_BADGE_TEXTURES[place]
		badge.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		badge.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		badge.custom_minimum_size = Vector2(56.0, 56.0)
		badge.size = Vector2(56.0, 56.0)
		badge.pivot_offset = badge.size * 0.5
		badge.visible = false
		add_child(badge)
		placement_badge_pool.append(badge)

func _reset_nametag_pool() -> void:
	for label in nametag_pool:
		if is_instance_valid(label): label.queue_free()
	for badge in placement_badge_pool:
		if is_instance_valid(badge): badge.queue_free()
	nametag_pool.clear()
	nametag_pool_car_indices.clear()
	nametag_pool_pending_indices.clear()
	nametag_best_distances.clear()
	nametag_best_indices.clear()
	placement_badge_pool.clear()

func _nametag_best_contains(car_index: int) -> bool:
	return nametag_best_indices.has(car_index)

func _nametag_pool_has_car(car_index: int) -> bool:
	return nametag_pool_car_indices.has(car_index) or nametag_pool_pending_indices.has(car_index)

func _nametag_assign(label: Label, slot: int, car_index: int) -> void:
	label.text = nametag_names[car_index] if car_index < nametag_names.size() else ""
	label.size = label.get_combined_minimum_size()
	label.modulate.a = 0.0
	label.visible = true
	nametag_pool_car_indices[slot] = car_index
	nametag_pool_pending_indices[slot] = -1

func _release_nametag_slot(label: Label, slot: int) -> void:
	label.visible = false
	label.modulate.a = 0.0
	nametag_pool_car_indices[slot] = -1
	nametag_pool_pending_indices[slot] = -1

func _update_top_place_badges(active_camera: Camera3D, camera_position: Vector3, camera_right: Vector3, camera_up: Vector3) -> void:
	for badge in placement_badge_pool:
		badge.visible = false
	if singleplayer_mode or placement_badge_pool.is_empty():
		return
	var top_players := []
	for car in car_node_container.get_children():
		var visual_car := car as VisualCar
		if visual_car == null or visual_car.owning_id == local_player_id:
			continue
		var place := int(game_sim.get_player_race_place(visual_car.owning_id))
		if place >= 1 and place <= TOP_PLACE_BADGE_TEXTURES.size():
			top_players.append([place, visual_car.owning_id])
	top_players.sort_custom(func(a, b): return int(a[0]) < int(b[0]))
	var badge_slot := 0
	for entry in top_players:
		var place := int(entry[0])
		var render_transform: Transform3D = game_sim.get_player_render_transform(int(entry[1]))
		var world_pos := render_transform.origin
		if camera_position.distance_squared_to(world_pos) > NAMETAG_MAX_DISTANCE_SQ or !active_camera.is_position_in_frustum(world_pos):
			continue
		var badge := placement_badge_pool[badge_slot]
		badge.texture = TOP_PLACE_BADGE_TEXTURES[place - 1]
		badge.visible = true
		badge.position = active_camera.unproject_position(world_pos + camera_right * 1.5 + camera_up * 2.35) + Vector2(42.0, -132.0)
		badge_slot += 1
		if badge_slot >= placement_badge_pool.size():
			return

func _lookup_id_value(source: Dictionary, player_id: int, fallback):
	if source.has(player_id): return source[player_id]
	var string_id := str(player_id)
	return source[string_id] if source.has(string_id) else fallback
