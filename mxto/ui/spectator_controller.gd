class_name SpectatorController
extends Node

signal notification_requested(text: String, duration_msec: int)
signal race_configured
signal race_reset

const SPECTATOR_SCENE: PackedScene = preload("res://player/spectator.tscn")
const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const LIVE_SPECTATE_STRAFE_THRESHOLD := 0.65

var network_manager: NetworkManager
var game_sim: GameSim
var car_node_container: CarNodeContainer
var vehicle_content_controller: VehicleContentControllerClass
var spectator: SpectatorPlayer
var local_player_id := -1
var local_elimination_active := false
var live_focus_id := -1
var live_strafe_direction := 0

func initialize(
	in_network_manager: NetworkManager,
	in_game_sim: GameSim,
	in_car_node_container: CarNodeContainer,
	in_vehicle_content_controller: VehicleContentControllerClass
) -> void:
	network_manager = in_network_manager
	game_sim = in_game_sim
	car_node_container = in_car_node_container
	vehicle_content_controller = in_vehicle_content_controller

func configure_race(in_local_player_id: int, local_player_is_racer: bool) -> void:
	reset()
	local_player_id = in_local_player_id
	if !local_player_is_racer:
		ensure_free_camera()
		if car_node_container.local_visual_car != null:
			live_focus_id = car_node_container.local_visual_car.owning_id
	race_configured.emit()

func reset() -> void:
	if spectator != null:
		spectator.queue_free()
		spectator = null
	local_player_id = -1
	local_elimination_active = false
	live_focus_id = -1
	live_strafe_direction = 0
	race_reset.emit()

func is_local_eliminated() -> bool:
	return local_player_id >= 0 and !network_manager.is_vehicle_restore_enabled() and network_manager.race_results.player_eliminations.has(local_player_id)

func is_local_dnf() -> bool:
	return local_player_id >= 0 and network_manager.race_results.player_dnfs.has(local_player_id)

func should_suppress_local_race_input() -> bool:
	return is_local_eliminated() or is_local_dnf()

func can_live_spectate() -> bool:
	return local_player_id >= 0 and (
		network_manager.spectator_ids.has(local_player_id)
		or network_manager.race_results.player_finish_times.has(local_player_id)
		or network_manager.race_results.player_dnfs.has(local_player_id))

func activate_local_elimination() -> void:
	if local_elimination_active or !is_local_eliminated():
		return
	local_elimination_active = true
	var current_camera := get_viewport().get_camera_3d()
	var start_transform := Transform3D.IDENTITY
	if current_camera != null:
		start_transform = current_camera.global_transform
	elif car_node_container.local_visual_car != null:
		var car := car_node_container.local_visual_car
		var car_transform := car.car_transform.global_transform
		var interest := car_transform.origin + car_transform.basis.y * 2.0
		start_transform.origin = interest - car_transform.basis.z * 26.0 + car_transform.basis.y * 10.0 + car_transform.basis.x * 10.0
		start_transform = start_transform.looking_at(interest, car_transform.basis.y.normalized())
	var free_camera := ensure_free_camera()
	free_camera.global_transform = start_transform
	free_camera.sync_look_from_current_transform()
	if car_node_container.local_visual_car != null:
		car_node_container.local_visual_car.race_hud.visible = false
	notification_requested.emit("Eliminated - Spectating", 3000)

func change_focus(delta: int) -> void:
	if !can_live_spectate():
		return
	var targets := _live_targets()
	if targets.is_empty():
		return
	var current_id := live_focus_id
	if current_id < 0 and car_node_container.local_visual_car != null:
		current_id = car_node_container.local_visual_car.owning_id
	var current_index := targets.find(current_id)
	var next_index := 0
	if current_index >= 0:
		next_index = posmod(current_index + delta, targets.size())
	elif delta < 0:
		next_index = targets.size() - 1
	_apply_live_focus(int(targets[next_index]))
	notification_requested.emit("Spectating: %s" % _player_display_name(int(targets[next_index])), 1200)

func focus_player(focus_id: int) -> bool:
	if !can_live_spectate() or !network_manager.get_simulation_roster().has(focus_id):
		return false
	if network_manager._disconnected_during_race.has(focus_id):
		return false
	_apply_live_focus(focus_id)
	notification_requested.emit("Spectating: %s" % _player_display_name(focus_id), 1200)
	return true

func toggle_camera() -> void:
	if !can_live_spectate() or spectator == null:
		return
	var current_camera := get_viewport().get_camera_3d()
	if current_camera == spectator.camera:
		var targets := _live_targets()
		if targets.is_empty():
			return
		var focus_id := live_focus_id
		if !targets.has(focus_id):
			focus_id = int(targets[0])
		_apply_live_focus(focus_id)
		notification_requested.emit("Gameplay Camera: %s" % _player_display_name(focus_id), 1200)
		return
	if current_camera != null:
		spectator.global_transform = current_camera.global_transform
	spectator.sync_look_from_current_transform()
	spectator.set_input_enabled(true)
	spectator.camera.make_current()
	notification_requested.emit("Free Camera", 1200)

func handle_unhandled_input(event: InputEvent) -> bool:
	if !can_live_spectate():
		return false
	if event.is_action_pressed("DpadLeft"):
		change_focus(-1)
		return true
	if event.is_action_pressed("DpadRight"):
		change_focus(1)
		return true
	if event.is_action_pressed("SpinAttack") or (event is InputEventKey and event.pressed and !event.echo and event.keycode == KEY_TAB):
		toggle_camera()
		return true
	return false

func update_finished_input() -> void:
	if !can_live_spectate():
		live_strafe_direction = 0
		live_focus_id = -1
		return
	if spectator != null and get_viewport().get_camera_3d() == spectator.camera:
		live_strafe_direction = 0
		return
	var left := Input.get_action_raw_strength("StrafeLeft")
	var right := Input.get_action_raw_strength("StrafeRight")
	var direction := 0
	if right >= LIVE_SPECTATE_STRAFE_THRESHOLD and right >= left:
		direction = 1
	elif left >= LIVE_SPECTATE_STRAFE_THRESHOLD:
		direction = -1
	if direction == 0:
		live_strafe_direction = 0
		return
	if live_strafe_direction == direction:
		return
	live_strafe_direction = direction
	change_focus(direction)

func ensure_free_camera() -> SpectatorPlayer:
	if spectator == null:
		spectator = SPECTATOR_SCENE.instantiate() as SpectatorPlayer
		add_child(spectator)
	return spectator

func disable_free_camera() -> void:
	if spectator != null:
		spectator.set_input_enabled(false)


func reconcile_after_practice_state_restore() -> void:
	if local_player_id < 0 or should_suppress_local_race_input() \
			or network_manager.race_results.player_finish_times.has(local_player_id):
		return
	local_elimination_active = false
	_apply_live_focus(local_player_id)
	if car_node_container.local_visual_car != null:
		car_node_container.local_visual_car.race_hud.visible = true

func show_free_camera_at(focus_transform: Transform3D) -> void:
	var free_camera := ensure_free_camera()
	free_camera.global_position = focus_transform.origin - focus_transform.basis.z * 32.0 + focus_transform.basis.y * 12.0
	free_camera.look_at(focus_transform.origin + focus_transform.basis.y * 2.0, focus_transform.basis.y.normalized())
	free_camera.sync_look_from_current_transform()
	free_camera.set_input_enabled(true)
	free_camera.camera.make_current()

func _live_targets() -> Array:
	var targets := []
	for id_value in network_manager.get_simulation_roster():
		var player_id := int(id_value)
		if network_manager.race_results.player_finish_times.has(player_id):
			continue
		if network_manager.race_results.player_dnfs.has(player_id):
			continue
		if network_manager._disconnected_during_race.has(player_id):
			continue
		if network_manager.race_results.player_eliminations.has(player_id):
			continue
		targets.append(player_id)
	return targets

func _apply_live_focus(focus_id: int) -> void:
	if car_node_container.local_visual_car == null:
		return
	live_focus_id = focus_id
	var car := car_node_container.local_visual_car
	car.owning_id = focus_id
	car.race_hud.focus_player_id = focus_id
	var settings = network_manager.lobby_settings.player_settings.get(focus_id, null)
	if settings != null:
		var player_settings := vehicle_content_controller.player_settings_for_stamp_render(settings)
		if player_settings != null:
			car.player_settings = player_settings
	if is_instance_valid(car.name_label):
		car.name_label.text = _player_display_name(focus_id)
	game_sim.set_gameplay_camera(car.car_camera, focus_id)
	disable_free_camera()
	car.car_camera.make_current()
	car.make_vehicle_audio_listener_current()

func _player_display_name(player_id: int) -> String:
	var player_name := str(player_id)
	var settings = network_manager.lobby_settings.player_settings.get(player_id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		player_name = str(settings["username"])
	if network_manager.lobby_settings.get_cpu_roster().has(player_id):
		player_name = "[CPU] " + player_name
	return player_name
