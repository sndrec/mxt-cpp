class_name LobbyChibiController
extends Node

const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const LoadTransitionProfilerClass = preload("res://core/load_transition_profiler.gd")
const LOBBY_CHIBI_CAR_SCRIPT := "res://ui/lobby_chibi_car.gd"

const BROADCAST_INTERVAL_MSEC := 100
const RENDER_REBUILD_DEBOUNCE_MSEC := 250
const STATE_RECORD_BYTES := 18
const VELOCITY_SCALE := 16.0
const KNOCKBACK_SCALE := 32.0
const ANGLE_SCALE := 256.0
const POSITION_SCALE := 512.0
const YAW_SCALE := 10000.0
const HOVER_RADIUS_PIXELS := 64.0
const MAGNIFICATION := 2.5
const MAGNIFIER_FOCUS_HEIGHT := 0.4

@onready var lobby_control: Control = $"../Lobby"
@onready var viewport_stack: Control = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack"
@onready var viewport: SubViewport = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport"
@onready var camera: Camera3D = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport/LobbyChibiCamera"
@onready var car_root: Node3D = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ViewportStack/ViewportContainer/LobbyChibiViewport/LobbyChibiRoot"
@onready var nameplates: Control = $"../Lobby/LobbyStatic/LobbyChibiNameplates"
@onready var magnifier: Control = $"../Lobby/LobbyStatic/LobbyChibiMagnifier"
@onready var magnifier_viewport: SubViewport = $"../Lobby/LobbyStatic/LobbyChibiMagnifier/MagnifierViewport"
@onready var magnifier_camera: Camera3D = $"../Lobby/LobbyStatic/LobbyChibiMagnifier/MagnifierViewport/MagnifierCamera"
@onready var magnifier_root: Node3D = $"../Lobby/LobbyStatic/LobbyChibiMagnifier/MagnifierViewport/MagnifierRoot"
@onready var magnifier_floor: MeshInstance3D = $"../Lobby/LobbyStatic/LobbyChibiMagnifier/MagnifierViewport/MagnifierRoot/MagnifierFloor"
@onready var magnifier_texture: TextureRect = $"../Lobby/LobbyStatic/LobbyChibiMagnifier/LensTexture"

var game_manager
var network_manager: NetworkManager
var game_sim: GameSim
var input_blocker: LineEdit
var vehicle_content_controller: VehicleContentControllerClass

var cars := {}
var pending_states := {}
var last_broadcast_msec := 0
var render_indices := {}
var hovered_player_id := -1
var magnifier_tween: Tween
var render_manager: CarRenderManager
var magnifier_render_manager: CarRenderManager
var render_signature := ""
var roster_cache: Array = []
var applied_settings_revision := -1
var applied_local_player_id := -1
var last_settings_apply_definition_count := 0
var last_settings_apply_sample_count := 0
var last_settings_apply_already_current_count := 0
var pending_render_signature := ""
var render_rebuild_due_msec := 0
var render_rebuild_count_total := 0
var render_cars: Array = []
var render_settings_by_id := {}
var magnifier_render_signature := ""

func _ready() -> void:
	render_manager = CarRenderManagerClass.new()
	render_manager.name = "LobbyChibiRenderManager"
	car_root.add_child(render_manager)
	magnifier_render_manager = CarRenderManagerClass.new()
	magnifier_render_manager.name = "LobbyChibiMagnifierRenderManager"
	magnifier_root.add_child(magnifier_render_manager)
	magnifier_texture.texture = magnifier_viewport.get_texture()
	viewport_stack.gui_input.connect(_on_view_gui_input)

func initialize(
	in_game_manager,
	in_network_manager: NetworkManager,
	in_game_sim: GameSim,
	in_input_blocker: LineEdit,
	in_vehicle_content_controller: VehicleContentControllerClass
) -> void:
	game_manager = in_game_manager
	network_manager = in_network_manager
	game_sim = in_game_sim
	input_blocker = in_input_blocker
	vehicle_content_controller = in_vehicle_content_controller

func _on_view_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and input_blocker != null:
		input_blocker.release_focus()

func is_active() -> bool:
	return lobby_control.visible and network_manager != null and !network_manager.race_active

func accepts_input() -> bool:
	if !is_active() or game_manager == null or !game_manager._window_accepts_input():
		return false
	return input_blocker == null or !input_blocker.has_focus()

func _accepts_network_state() -> bool:
	if network_manager == null or network_manager.race_active:
		return false
	return network_manager.is_server or is_active()

func clear() -> void:
	var had_state := (
		!cars.is_empty()
		or !pending_states.is_empty()
		or render_signature != ""
		or hovered_player_id >= 0)
	if !had_state:
		if viewport.render_target_update_mode != SubViewport.UPDATE_DISABLED:
			viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		_hide_magnifier()
		return
	for id in cars.keys():
		var car = cars[id]
		if car != null and is_instance_valid(car):
			car.queue_free()
	for child in nameplates.get_children():
		child.queue_free()
	cars.clear()
	pending_states.clear()
	render_indices.clear()
	roster_cache.clear()
	applied_settings_revision = -1
	applied_local_player_id = -1
	last_settings_apply_definition_count = 0
	last_settings_apply_sample_count = 0
	last_settings_apply_already_current_count = 0
	hovered_player_id = -1
	last_broadcast_msec = 0
	render_signature = ""
	pending_render_signature = ""
	render_rebuild_due_msec = 0
	render_cars.clear()
	render_settings_by_id.clear()
	magnifier_render_signature = ""
	render_manager.clear_renderer()
	magnifier_render_manager.clear_renderer()
	viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	_hide_magnifier()

func _hide_magnifier() -> void:
	if magnifier_tween != null and magnifier_tween.is_valid():
		magnifier_tween.kill()
	magnifier_tween = null
	magnifier.visible = false
	magnifier_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED

func process_lobby(_delta: float) -> void:
	if !is_active():
		clear()
		return
	if viewport.render_target_update_mode != SubViewport.UPDATE_ALWAYS:
		viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var roster := _human_roster()
	for id in roster:
		var lobby_settings: Dictionary = network_manager.lobby_settings.player_settings.get(int(id), {})
		vehicle_content_controller.request_lobby_vehicle_content(lobby_settings)
	var local_id: int = game_manager._local_player_id()
	var roster_changed := roster != roster_cache
	if roster_changed:
		var live := {}
		for i in range(roster.size()):
			var id := int(roster[i])
			live[id] = true
			if !cars.has(id) or !is_instance_valid(cars[id]):
				var settings: Dictionary = network_manager.lobby_settings.player_settings.get(id, {})
				var new_car = load(LOBBY_CHIBI_CAR_SCRIPT).new()
				new_car.name = "ChibiCar%d" % id
				new_car.position = _spawn_position(i)
				car_root.add_child(new_car)
				_configure_car(new_car, id, settings, id == local_id, network_manager.lobby_settings.get_player_settings_revision(id), true)
				cars[id] = new_car
		for id in cars.keys():
			if !live.has(id):
				var stale_car = cars[id]
				if is_instance_valid(stale_car):
					stale_car.queue_free()
				cars.erase(id)
		roster_cache = roster.duplicate()
	var settings_revision := network_manager.lobby_settings.revision
	var local_control_changed := local_id != applied_local_player_id
	if roster_changed or settings_revision != applied_settings_revision or local_control_changed:
		var load_profile := LoadTransitionProfilerClass.begin_transition("lobby", "settings_revision_apply", {
			"roster_count": roster.size(),
			"roster_changed": roster_changed,
			"local_control_changed": local_control_changed,
			"previous_revision": applied_settings_revision,
			"next_revision": settings_revision,
		})
		var player_profiles: Array = []
		for id in roster:
			var player_id := int(id)
			var existing_car = cars.get(player_id, null)
			if existing_car == null or !is_instance_valid(existing_car):
				continue
			player_profiles.append(_configure_car(
				existing_car,
				player_id,
				network_manager.lobby_settings.player_settings.get(player_id, {}),
				player_id == local_id,
				network_manager.lobby_settings.get_player_settings_revision(player_id),
				false))
		applied_settings_revision = settings_revision
		applied_local_player_id = local_id
		last_settings_apply_definition_count = player_profiles.filter(
			func(profile: Dictionary): return bool(profile.get("definition_looked_up", false))).size()
		last_settings_apply_sample_count = player_profiles.filter(
			func(profile: Dictionary): return bool(profile.get("stats_sampled", false))).size()
		last_settings_apply_already_current_count = player_profiles.filter(
			func(profile: Dictionary): return bool(profile.get("already_current", false))).size()
		player_profiles.sort_custom(
			func(a: Dictionary, b: Dictionary): return int(a.get("duration_usec", 0)) > int(b.get("duration_usec", 0)))
		LoadTransitionProfilerClass.end_transition(load_profile, {
			"player_profiles": player_profiles,
			"definition_lookup_count": last_settings_apply_definition_count,
			"stats_sample_count": last_settings_apply_sample_count,
			"already_current_count": last_settings_apply_already_current_count,
		})
	_submit_render(roster)
	_update_hover_and_magnifier()
	if network_manager.is_server:
		_broadcast_states_if_needed()

func _configure_car(car, player_id: int, settings: Dictionary, local_control: bool, settings_revision: int, initial: bool, force_content_refresh := false) -> Dictionary:
	var start_usec := Time.get_ticks_usec()
	var previous_revision := int(car.settings_revision) if car != null else -1
	var already_current := !initial and !force_content_refresh and settings_revision >= 0 and settings_revision == previous_revision
	if already_current:
		var ownership_start_usec := Time.get_ticks_usec()
		car.set_local_control(local_control)
		return {
			"player_id": player_id,
			"content_id": String(settings.get("vehicle_content_id", "")),
			"settings_revision": settings_revision,
			"previous_revision": previous_revision,
			"already_current": true,
			"initial": false,
			"force_content_refresh": false,
			"definition_looked_up": false,
			"stats_sampled": false,
			"definition_usec": 0,
			"sample_stats_usec": 0,
			"apply_usec": Time.get_ticks_usec() - ownership_start_usec,
			"duration_usec": Time.get_ticks_usec() - start_usec,
		}
	var definition_start_usec := Time.get_ticks_usec()
	var definition: CarDefinition = vehicle_content_controller.get_definition(str(settings.get("vehicle_content_id", "")))
	var definition_usec := Time.get_ticks_usec() - definition_start_usec
	var stats_start_usec := Time.get_ticks_usec()
	var sampled_stats := _sample_stats(settings, definition)
	var stats_usec := Time.get_ticks_usec() - stats_start_usec
	var apply_start_usec := Time.get_ticks_usec()
	if initial:
		car.setup(player_id, settings, self, camera, nameplates, local_control, settings_revision, definition, sampled_stats)
	else:
		car.update_settings(settings, definition, sampled_stats, settings_revision, force_content_refresh)
		car.set_local_control(local_control)
	var apply_usec := Time.get_ticks_usec() - apply_start_usec
	return {
		"player_id": player_id,
		"content_id": String(settings.get("vehicle_content_id", "")),
		"settings_revision": settings_revision,
		"previous_revision": previous_revision,
		"already_current": false,
		"initial": initial,
		"force_content_refresh": force_content_refresh,
		"definition_looked_up": true,
		"stats_sampled": true,
		"definition_usec": definition_usec,
		"sample_stats_usec": stats_usec,
		"apply_usec": apply_usec,
		"duration_usec": Time.get_ticks_usec() - start_usec,
	}

func refresh_vehicle_content(affected_content_ids: Array = []) -> void:
	var refresh_start_usec := Time.get_ticks_usec()
	var targeted_refresh := !affected_content_ids.is_empty()
	var load_profile := LoadTransitionProfilerClass.begin_transition("lobby", "vehicle_content_refresh", {
		"car_count": cars.size(),
		"affected_content_ids": affected_content_ids,
	})
	var refreshed_players := []
	var player_profiles: Array = []
	var local_id: int = game_manager._local_player_id() if game_manager != null else -1
	for id in cars.keys():
		var player_id := int(id)
		var car = cars[player_id]
		if car == null or !is_instance_valid(car):
			continue
		var player_settings: Dictionary = network_manager.lobby_settings.player_settings.get(player_id, {})
		if targeted_refresh and !affected_content_ids.has(String(player_settings.get("vehicle_content_id", ""))):
			continue
		refreshed_players.append({
			"player_id": player_id,
			"vehicle_content_id": String(player_settings.get("vehicle_content_id", "")),
			"vehicle_workshop_id": String(player_settings.get("vehicle_workshop_id", "")),
			"vehicle_gameplay_digest": String(player_settings.get("vehicle_gameplay_digest", "")),
			"vehicle_package_digest": String(player_settings.get("vehicle_package_digest", "")),
		})
		player_profiles.append(_configure_car(
			car,
			player_id,
			player_settings,
			player_id == local_id,
			network_manager.lobby_settings.get_player_settings_revision(player_id),
			false,
			true))
	LoadTransitionProfilerClass.checkpoint(load_profile, "reconfigure_lobby_cars", {
		"refreshed_player_count": refreshed_players.size(),
	})
	applied_settings_revision = network_manager.lobby_settings.revision
	applied_local_player_id = local_id
	if !refreshed_players.is_empty():
		render_signature = ""
		pending_render_signature = ""
		render_rebuild_due_msec = 0
		magnifier_render_signature = ""
	LoadTransitionProfilerClass.checkpoint(load_profile, "invalidate_affected_renderers", {
		"renderer_invalidated": !refreshed_players.is_empty(),
	})
	vehicle_content_controller.record_workshop_diagnostic_event("lobby_vehicle_content_refresh", {
		"duration_usec": Time.get_ticks_usec() - refresh_start_usec,
		"players": refreshed_players,
		"car_count": cars.size(),
	})
	LoadTransitionProfilerClass.end_transition(load_profile, {
		"refreshed_player_count": refreshed_players.size(),
		"player_profiles": player_profiles,
	})

func _sample_stats(settings: Dictionary, definition: CarDefinition) -> Dictionary:
	if definition == null or definition.properties_path == "" or !FileAccess.file_exists(definition.properties_path):
		return {}
	var bytes := FileAccess.get_file_as_bytes(definition.properties_path)
	var machine_setting := clampf(float(settings.get("accel_setting", 0.5)), 0.0, 1.0)
	var sampled: Dictionary = game_sim.sample_car_properties(bytes, machine_setting)
	var stats = sampled.get("base_stats", {})
	return stats if typeof(stats) == TYPE_DICTIONARY else {}

func _submit_render(roster: Array) -> void:
	var signature := _render_source_signature(roster)
	var now_msec := Time.get_ticks_msec()
	if signature != pending_render_signature:
		pending_render_signature = signature
		render_rebuild_due_msec = now_msec + RENDER_REBUILD_DEBOUNCE_MSEC
	var renderer_empty := render_manager.archetypes.is_empty()
	if signature != render_signature and (renderer_empty or now_msec >= render_rebuild_due_msec):
		var rebuild_start_usec := Time.get_ticks_usec()
		var load_profile := LoadTransitionProfilerClass.begin_transition("lobby", "vehicle_render_rebuild", {
			"renderer_was_empty": renderer_empty,
			"roster_count": roster.size(),
		})
		render_indices.clear()
		var definitions := []
		var settings := []
		var player_ids := []
		var render_input_revisions := []
		var next_render_cars := []
		for id in roster:
			var player_id := int(id)
			var car = cars.get(player_id, null)
			if car == null or !is_instance_valid(car):
				continue
			var definition: CarDefinition = car.get_render_definition()
			if definition == null:
				continue
			definitions.append(definition)
			settings.append(car.player_settings)
			player_ids.append(player_id)
			render_input_revisions.append("%d:%d:%d" % [
				player_id,
				network_manager.lobby_settings.get_player_settings_revision(player_id),
				network_manager.custom_stamp_network.revision,
			])
			render_indices[player_id] = next_render_cars.size()
			next_render_cars.append(car)
		LoadTransitionProfilerClass.checkpoint(load_profile, "collect_render_roster", {
			"definition_count": definitions.size(),
		})
		var stamp_render: Dictionary = vehicle_content_controller.prepare_custom_stamp_render_payload(player_ids, settings, "lobby")
		LoadTransitionProfilerClass.checkpoint(load_profile, "prepare_custom_stamp_atlas")
		render_manager.set_custom_stamp_atlas(stamp_render.get("texture", null))
		var render_settings: Array = stamp_render.get("settings", settings)
		render_manager.reconfigure_manual(definitions, render_settings, render_input_revisions)
		LoadTransitionProfilerClass.checkpoint(load_profile, "configure_render_archetypes")
		render_cars = next_render_cars
		render_settings_by_id.clear()
		for i in range(mini(player_ids.size(), render_settings.size())):
			render_settings_by_id[int(player_ids[i])] = render_settings[i]
		render_signature = signature
		magnifier_render_signature = ""
		render_rebuild_count_total += 1
		var rebuild_duration_usec := Time.get_ticks_usec() - rebuild_start_usec
		network_manager.telemetry.record_lobby_render_rebuild(rebuild_duration_usec)
		game_manager.record_memory_sample("lobby_render_rebuild")
		var rendered_vehicles := []
		for entry_settings in settings:
			rendered_vehicles.append({
				"vehicle_content_id": String(entry_settings.get("vehicle_content_id", "")),
				"vehicle_workshop_id": String(entry_settings.get("vehicle_workshop_id", "")),
				"vehicle_gameplay_digest": String(entry_settings.get("vehicle_gameplay_digest", "")),
				"vehicle_package_digest": String(entry_settings.get("vehicle_package_digest", "")),
			})
		vehicle_content_controller.record_workshop_diagnostic_event("lobby_render_rebuild", {
			"duration_usec": rebuild_duration_usec,
			"signature": signature,
			"renderer_was_empty": renderer_empty,
			"roster": roster.duplicate(),
			"rendered_vehicles": rendered_vehicles,
		})
		LoadTransitionProfilerClass.end_transition(load_profile, {
			"definition_count": definitions.size(),
			"rebuild_duration_usec_existing_counter": rebuild_duration_usec,
			"signature": signature,
		})
	render_manager.begin_manual_submit()
	var render_root_inv := render_manager.global_transform.affine_inverse()
	for i in range(render_cars.size()):
		var car = render_cars[i]
		if car == null or !is_instance_valid(car):
			continue
		render_manager.submit_manual_car(i, render_root_inv * car.get_render_transform(), car.get_render_overlay(), car.get_render_outline_velocity(), car.get_render_outline_overlay(), car.get_render_thrust(), false)

func _render_source_signature(roster: Array) -> String:
	var roster_ids := PackedStringArray()
	for id in roster:
		roster_ids.append(str(int(id)))
	return "%s:%d:%d" % [",".join(roster_ids), network_manager.lobby_settings.revision, network_manager.custom_stamp_network.revision]

func _update_hover_and_magnifier() -> void:
	var stack_size := viewport_stack.size
	var viewport_size := Vector2(viewport.size)
	var cursor_stack := viewport_stack.get_local_mouse_position()
	if stack_size.x <= 0.0 or stack_size.y <= 0.0 or viewport_size.x <= 0.0 or viewport_size.y <= 0.0 or !Rect2(Vector2.ZERO, stack_size).has_point(cursor_stack):
		set_hover(-1)
		return
	var cursor_viewport := Vector2(cursor_stack.x * viewport_size.x / stack_size.x, cursor_stack.y * viewport_size.y / stack_size.y)
	var closest_player_id := -1
	var closest_distance_squared := HOVER_RADIUS_PIXELS * HOVER_RADIUS_PIXELS
	for id in cars.keys():
		var car = cars[id]
		if car == null or !is_instance_valid(car):
			continue
		var distance_squared := cursor_viewport.distance_squared_to(car.get_hover_anchor())
		if distance_squared <= closest_distance_squared:
			closest_distance_squared = distance_squared
			closest_player_id = int(id)
	set_hover(closest_player_id)

func set_hover(player_id: int) -> void:
	var selection_changed := player_id != hovered_player_id
	if selection_changed:
		var old_car = cars.get(hovered_player_id, null)
		if old_car != null and is_instance_valid(old_car):
			old_car.set_hovered(false)
		var new_car = cars.get(player_id, null)
		if new_car != null and is_instance_valid(new_car):
			new_car.set_hovered(true)
	hovered_player_id = player_id
	if player_id < 0:
		if selection_changed:
			magnifier_render_manager.begin_manual_submit()
		_hide_magnifier()
		return
	_show_magnifier(player_id, selection_changed)

func _show_magnifier(player_id: int, selection_changed: bool) -> void:
	if !cars.has(player_id) or !render_settings_by_id.has(player_id):
		_hide_magnifier()
		return
	var car = cars[player_id]
	if car == null or !is_instance_valid(car):
		_hide_magnifier()
		return
	var viewport_size := Vector2(viewport.size)
	var stack_size := viewport_stack.size
	if viewport_size.x <= 0.0 or viewport_size.y <= 0.0 or stack_size.x <= 0.0 or stack_size.y <= 0.0:
		_hide_magnifier()
		return
	var car_viewport_anchor: Vector2 = car.get_hover_anchor()
	var car_stack_anchor := Vector2(car_viewport_anchor.x * stack_size.x / viewport_size.x, car_viewport_anchor.y * stack_size.y / viewport_size.y)
	var car_canvas: Vector2 = viewport_stack.get_global_transform_with_canvas() * car_stack_anchor
	var magnifier_parent := magnifier.get_parent() as Control
	var car_parent: Vector2 = magnifier_parent.get_global_transform_with_canvas().affine_inverse() * car_canvas
	var desired_position := car_parent - magnifier.pivot_offset
	desired_position.x = clampf(desired_position.x, 0.0, maxf(0.0, magnifier_parent.size.x - magnifier.size.x))
	desired_position.y = clampf(desired_position.y, 0.0, maxf(0.0, magnifier_parent.size.y - magnifier.size.y))
	magnifier.position = desired_position
	var camera_transform := camera.global_transform
	camera_transform.origin += car.global_position + Vector3.UP * MAGNIFIER_FOCUS_HEIGHT
	magnifier_camera.global_transform = camera_transform
	magnifier_camera.size = camera.size / MAGNIFICATION
	var signature := "%d:%s" % [player_id, render_signature]
	if signature != magnifier_render_signature:
		var definition: CarDefinition = car.get_render_definition()
		if definition == null:
			_hide_magnifier()
			return
		magnifier_render_manager.set_custom_stamp_atlas(render_manager.custom_stamp_atlas_texture)
		magnifier_render_manager.reconfigure_manual([definition], [render_settings_by_id[player_id]])
		magnifier_render_signature = signature
	magnifier_render_manager.begin_manual_submit()
	var render_root_inv := magnifier_render_manager.global_transform.affine_inverse()
	magnifier_render_manager.submit_manual_car(0, render_root_inv * car.get_render_transform(), car.get_render_overlay(), car.get_render_outline_velocity(), car.get_render_outline_overlay(), car.get_render_thrust(), false)
	magnifier_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	if selection_changed or !magnifier.visible:
		if magnifier_tween != null and magnifier_tween.is_valid():
			magnifier_tween.kill()
		magnifier.scale = Vector2(0.25, 0.25)
		magnifier.visible = true
		magnifier_tween = create_tween()
		magnifier_tween.set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
		magnifier_tween.tween_property(magnifier, "scale", Vector2.ONE, 0.14)
	var circle_bottom_canvas: Vector2 = magnifier.get_global_transform_with_canvas() * Vector2(magnifier.pivot_offset.x, magnifier.size.y)
	var nameplate_anchor: Vector2 = nameplates.get_global_transform_with_canvas().affine_inverse() * circle_bottom_canvas
	car.set_nameplate_attachment(nameplate_anchor)

func _human_roster() -> Array:
	var out := []
	var seen := {}
	var cpu_lookup := {}
	for cpu_id in network_manager.lobby_settings.get_cpu_roster():
		cpu_lookup[int(cpu_id)] = true
	for source in [network_manager.player_ids, network_manager.spectator_ids, network_manager.waiting_peers]:
		for id in source:
			var int_id := int(id)
			if !cpu_lookup.has(int_id) and !seen.has(int_id):
				seen[int_id] = true
				out.append(int_id)
	return out

func _spawn_position(index: int) -> Vector3:
	return Vector3(-6.0 + float(index % 4) * 4.0, 0.6, -3.0 + float(index / 4) * 3.0)

func _pack_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> PackedByteArray:
	var state := PackedByteArray()
	state.resize(STATE_RECORD_BYTES)
	state.encode_s32(0, player_id)
	state.encode_s16(4, _quantize(velocity, VELOCITY_SCALE))
	state.encode_s16(6, _quantize(knockback_velocity.x, KNOCKBACK_SCALE))
	state.encode_s16(8, _quantize(knockback_velocity.z, KNOCKBACK_SCALE))
	state.encode_s16(10, _quantize(angle_velocity, ANGLE_SCALE))
	state.encode_s16(12, _quantize(position.x, POSITION_SCALE))
	state.encode_s16(14, _quantize(position.z, POSITION_SCALE))
	state.encode_s16(16, _quantize(wrapf(rotation.y, -PI, PI), YAW_SCALE))
	return state

func _quantize(value: float, scale: float) -> int:
	return clampi(roundi(value * scale), -32768, 32767)

func _apply_packed_state(state: PackedByteArray, offset: int = 0) -> void:
	if offset < 0 or offset + STATE_RECORD_BYTES > state.size():
		return
	_apply_state(
		state.decode_s32(offset),
		float(state.decode_s16(offset + 4)) / VELOCITY_SCALE,
		Vector3(float(state.decode_s16(offset + 6)) / KNOCKBACK_SCALE, 0.0, float(state.decode_s16(offset + 8)) / KNOCKBACK_SCALE),
		float(state.decode_s16(offset + 10)) / ANGLE_SCALE,
		Vector3(float(state.decode_s16(offset + 12)) / POSITION_SCALE, 0.0, float(state.decode_s16(offset + 14)) / POSITION_SCALE),
		Vector3(0.0, float(state.decode_s16(offset + 16)) / YAW_SCALE, 0.0))

func _broadcast_states_if_needed() -> void:
	if !_accepts_network_state() or !network_manager.is_server or pending_states.is_empty():
		return
	var now := Time.get_ticks_msec()
	if now < last_broadcast_msec + BROADCAST_INTERVAL_MSEC:
		return
	last_broadcast_msec = now
	var batch := PackedByteArray()
	batch.resize(pending_states.size() * STATE_RECORD_BYTES)
	var offset := 0
	for id in pending_states.keys():
		var state: PackedByteArray = pending_states[id]
		for byte_index in range(STATE_RECORD_BYTES):
			batch[offset + byte_index] = state[byte_index]
		offset += STATE_RECORD_BYTES
	pending_states.clear()
	_apply_state_batch.rpc(batch)
	var recipients := multiplayer.get_peers().size()
	network_manager.telemetry.record_lobby_chibi_network(0, 0, recipients, batch.size() * recipients)

func send_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !is_active():
		return
	if !network_manager.has_network_peer():
		_apply_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	elif network_manager.is_server:
		pending_states[player_id] = _pack_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	else:
		var state := _pack_state(player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
		_submit_state.rpc_id(1, state)
		network_manager.telemetry.record_lobby_chibi_network(0, 0, 1, state.size())

@rpc("any_peer", "call_local", "unreliable_ordered", 9)
func _submit_state(state: PackedByteArray) -> void:
	if !_accepts_network_state() or !network_manager.is_server or state.size() != STATE_RECORD_BYTES:
		return
	var sender := multiplayer.get_remote_sender_id()
	if sender != 0:
		network_manager.telemetry.record_lobby_chibi_network(1, state.size(), 0, 0)
		state.encode_s32(0, sender)
	pending_states[state.decode_s32(0)] = state
	_apply_packed_state(state)
	_broadcast_states_if_needed()

@rpc("authority", "call_local", "unreliable_ordered", 9)
func _apply_state_batch(states: PackedByteArray) -> void:
	if !is_active() or states.size() % STATE_RECORD_BYTES != 0:
		return
	if multiplayer.get_remote_sender_id() != 0:
		network_manager.telemetry.record_lobby_chibi_network(1, states.size(), 0, 0)
	for offset in range(0, states.size(), STATE_RECORD_BYTES):
		_apply_packed_state(states, offset)

@rpc("any_peer", "call_local", "unreliable_ordered")
func _apply_state(player_id: int, velocity: float, knockback_velocity: Vector3, angle_velocity: float, position: Vector3, rotation: Vector3) -> void:
	if !is_active() or !cars.has(player_id):
		return
	var car = cars[player_id]
	if car != null and is_instance_valid(car):
		car.apply_remote_state(velocity, knockback_velocity, angle_velocity, position, rotation)

func latency_text_for_player(player_id: int) -> String:
	if player_id == game_manager._local_player_id():
		return "0ms"
	var value := -1.0
	if network_manager.lobby_settings.latency_rtt_s.has(player_id):
		value = float(network_manager.lobby_settings.latency_rtt_s[player_id])
	elif network_manager.input_transport.peer_client_rtt_s.has(player_id):
		value = float(network_manager.input_transport.peer_client_rtt_s[player_id])
	elif !network_manager.is_server and player_id == 1:
		value = network_manager.input_transport.rtt_s
	return "--ms" if value < 0.0 else "%dms" % roundi(value * 1000.0)
