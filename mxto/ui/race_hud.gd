class_name RaceHud extends Control

const RacePresentationControllerClass = preload("res://ui/race_presentation_controller.gd")

@onready var speedometer := %speedometer
@onready var lapcounter := %lapcounter
@onready var racetimer := %racetimer
@onready var forceendtimer := %forceendtimer
@onready var healthmeter := %healthmeter
@onready var countdowncontrol := $countdowncontrol as Control
@onready var countdown_arrow := $countdowncontrol/countdown_arrow as TextureRect
@onready var leaderboard_container := $Control/leaderboard_container
@onready var place_badge := $PlaceBadge as Control
@onready var minimap_rect := $MinimapControl/TextureRect
@onready var minimap_control := $MinimapControl as Control
@onready var sub_viewport := $MinimapControl/SubViewport as SubViewport
@onready var minimap_cam := $MinimapControl/SubViewport/Camera3D as Camera3D
@onready var minimap_mesh := $MinimapControl/SubViewport/MeshInstance3D as MeshInstance3D
#@onready var race_placement_hud := $RacePlacementHud
@onready var check_control: Control = $CheckControl
@onready var sboost_meter_bg: ColorRect = %sboost_meter_bg
@onready var sboost_meter_fill: ColorRect = %sboost_meter_fill
@onready var attack_meter: Control = %attack_meter
@onready var attack_meter_border: ColorRect = %attack_meter_border
@onready var attack_meter_icon: TextureRect = %attack_meter_icon

var car_max_energy: float = 100.0
var race_presentation_controller: RacePresentationControllerClass
var boost_energy_use_rate: float = 1.0
var _sboost_full_width: float = 0.0
var focus_player_id := 0
var minimap_mode := 0
var minimap_initialized := false
var minimap_markers: MultiMesh
var minimap_marker_layer: MultiMeshInstance2D
const MINIMAP_MARKER_TEXTURE: Texture2D = preload("res://ui/circle.png")
const MAX_LEADERBOARD_ENTRIES := 5
var leaderboard_labels: Array[Label] = []
const ATTACK_COOLDOWN_FRAMES := 240.0
var render_profile_frames := 0
var render_profile_total_us := 0
var render_profile_total_max_us := 0
var render_profile_leaderboard_us := 0
var render_profile_leaderboard_max_us := 0
var render_profile_check_us := 0
var render_profile_check_max_us := 0
var render_profile_sticker_input_us := 0
var render_profile_sticker_input_max_us := 0
var render_profile_world_stickers_us := 0
var render_profile_world_stickers_max_us := 0

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
var sticker_pool: Array[Node3D] = []
var sticker_pool_actor_ids: Array[int] = []
var sticker_candidates: Array = []
var check_icons: Array[TextureRect] = []
var sticker_menu_hide_msec := 0
var sticker_menu_open := false
var sticker_input_buffer_msec := 0
const CHECK_ICON_POOL_SIZE := 6
const STICKER_POOL_SIZE := 30
const STICKER_BASE_PIXEL_SIZE := 0.003
const STICKER_BASE_TEXTURE_PIXELS := 128.0
const STICKER_BASE_HEIGHT_OFFSET := 3.0
const STICKER_BASE_RISE_OFFSET := 0.66
const STICKER_DISTANCE_SCALE_START := 20.0
const STICKER_DISTANCE_SCALE_END := 600.0
const STICKER_FAR_SCALE := 0.1
const STICKER_DISTANCE_SCALE_EXPONENT := 0.3
const STICKER_XRAY_ALPHA := 0.35

@onready var real_input := $InputViewer/RealInput
@onready var clamped_input := $InputViewer/ClampedInput

func _player_name_for_id(nm: NetworkManager, id: int) -> String:
	if id < 0:
		return ""
	var name := str(id)
	var settings = nm.lobby_settings.player_settings.get(id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		name = str(settings["username"])
	var is_cpu := nm.lobby_settings.cpu_player_settings.has(id)
	if !is_cpu and typeof(settings) == TYPE_DICTIONARY:
		is_cpu = bool(settings.get("_race_is_cpu", false))
	if !is_cpu and nm.lobby_settings.cpu_player_settings.is_empty():
		is_cpu = nm.lobby_settings.cpu_player_ids.has(id)
	if is_cpu:
		name = "[CPU] " + name
	return name

func get_render_profile_string() -> String:
	if render_profile_frames <= 0:
		return "MXT_RACE_HUD_PROFILE frames=0"
	return "MXT_RACE_HUD_PROFILE frames=%d total_us=%d total_max_us=%d leaderboard_us=%d leaderboard_max_us=%d check_us=%d check_max_us=%d sticker_input_us=%d sticker_input_max_us=%d world_stickers_us=%d world_stickers_max_us=%d" % [
		render_profile_frames,
		int(render_profile_total_us / render_profile_frames),
		render_profile_total_max_us,
		int(render_profile_leaderboard_us / render_profile_frames),
		render_profile_leaderboard_max_us,
		int(render_profile_check_us / render_profile_frames),
		render_profile_check_max_us,
		int(render_profile_sticker_input_us / render_profile_frames),
		render_profile_sticker_input_max_us,
		int(render_profile_world_stickers_us / render_profile_frames),
		render_profile_world_stickers_max_us,
	]

func _update_leaderboard(car: VisualCar, nm: NetworkManager, focus_id: int, fallback_place: int) -> int:
	if leaderboard_container == null:
		return fallback_place
	var window: PackedInt32Array = car.game_manager.game_sim.get_race_leaderboard_window(
		focus_id,
		MAX_LEADERBOARD_ENTRIES,
		nm.race_results.player_finish_times,
		nm.race_results.player_eliminations
	)
	if window.size() <= 1:
		return fallback_place
	var focus_place := int(window[0])
	var visible_count := int((window.size() - 1) / 2)
	for i in range(leaderboard_labels.size()):
		var label := leaderboard_labels[i]
		label.visible = i < visible_count
		if i >= visible_count:
			continue
		var window_index := 1 + i * 2
		var id := int(window[window_index])
		var place := int(window[window_index + 1])
		if nm.race_results.player_finish_placements.has(id):
			place = int(nm.race_results.player_finish_placements[id])
		var text := "%d  %s" % [place, _player_name_for_id(nm, id)]
		if label.text != text:
			label.text = text
	if nm.race_results.player_finish_placements.has(focus_id):
		focus_place = int(nm.race_results.player_finish_placements[focus_id])
	return focus_place

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
	var target_count := mini(count, CHECK_ICON_POOL_SIZE)
	while check_icons.size() < target_count:
		var icon := TextureRect.new()
		icon.texture = CHECK_INCOMING_TEXTURE
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		icon.size = Vector2(64.0, 64.0)
		icon.pivot_offset = Vector2(32.0, 64.0)
		check_control.add_child(icon)
		check_icons.append(icon)

func _send_sticker(car: VisualCar, slot: int) -> void:
	var sticker_index := _sticker_slot_value(car, slot)
	if stickers != null and stickers.stickers.size() > 0:
		sticker_index = wrapi(sticker_index, 0, stickers.stickers.size())
	race_presentation_controller.send_local_sticker(sticker_index)
	sticker_menu_open = false
	sticker_menu.visible = false
	sticker_input_buffer_msec = Time.get_ticks_msec() + 50

func _update_sticker_menu_icons(car: VisualCar) -> void:
	if stickers == null or stickers.stickers.is_empty():
		return
	for i in range(sticker_menu_icons.size()):
		var sticker_index := wrapi(_sticker_slot_value(car, i), 0, stickers.stickers.size())
		sticker_menu_icons[i].texture = stickers.stickers[sticker_index]

func _update_sticker_input(car: VisualCar) -> void:
	if car.game_manager != null \
			and String(car.game_manager.network_manager.race_options.get("session_kind", "")) == "practice":
		sticker_menu_open = false
		if sticker_menu != null:
			sticker_menu.visible = false
		return
	if car.game_manager != null and car.game_manager.has_method("_window_accepts_input"):
		if !bool(car.game_manager.call("_window_accepts_input")):
			return
	var now := Time.get_ticks_msec()
	if now < sticker_input_buffer_msec:
		return
	if !sticker_menu_open:
		if Input.is_action_just_pressed("StickerSlot3"):
			_update_sticker_menu_icons(car)
			sticker_menu_open = true
			sticker_menu.visible = true
			sticker_menu_hide_msec = now + 2500
			sticker_input_buffer_msec = now + 50
		return
	if Input.is_action_just_pressed("StickerSlot1"):
		_send_sticker(car, 0)
	elif Input.is_action_just_pressed("StickerSlot2"):
		_send_sticker(car, 1)
	elif Input.is_action_just_pressed("StickerSlot3"):
		_send_sticker(car, 2)
	elif Input.is_action_just_pressed("StickerSlot4"):
		_send_sticker(car, 3)
	if sticker_menu != null and sticker_menu.visible and Time.get_ticks_msec() > sticker_menu_hide_msec:
		sticker_menu_open = false
		sticker_menu.visible = false

func _update_check_warnings(car: VisualCar) -> void:
	var camera := get_viewport().get_camera_3d()
	if camera == null:
		return
	var candidates: Array = car.game_manager.game_sim.get_check_warning_candidates(car.owning_id)
	_ensure_check_icons(CHECK_ICON_POOL_SIZE)
	var viewport_size := get_viewport_rect().size
	var focus_transform: Transform3D = car.game_manager.game_sim.get_player_render_transform(car.owning_id)
	for i in range(check_icons.size()):
		var icon := check_icons[i]
		icon.visible = i < candidates.size() and i < CHECK_ICON_POOL_SIZE
		if i >= candidates.size() or i >= CHECK_ICON_POOL_SIZE:
			continue
		var candidate: Dictionary = candidates[i]
		var alpha := float(candidate.get("alpha", 0.0))
		var size := lerpf(32.0, 96.0, alpha)
		icon.size = Vector2(size, size)
		icon.pivot_offset = Vector2(size * 0.5, size)
		var intersect := candidate.get("intersect", focus_transform.origin) as Vector3
		var lateral := (intersect - focus_transform.origin).dot(camera.global_basis.x)
		var x := viewport_size.x * 0.5 - lateral * -12.0 - size * 0.5
		if x < -size or x > viewport_size.x:
			icon.visible = false
			continue
		icon.modulate.a = alpha * alpha
		icon.position.x = x
		icon.position.y = maxf(0.0, minf(580.0, viewport_size.y - size - 12.0))

func _make_world_sticker_sprite(no_depth: bool, alpha: float) -> Sprite3D:
	var sprite := Sprite3D.new()
	sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite.centered = false
	sprite.fixed_size = true
	sprite.no_depth_test = no_depth
	sprite.modulate = Color(1.0, 1.0, 1.0, alpha)
	sprite.render_priority = -1 if no_depth else 0
	return sprite

func _make_world_sticker_node() -> Node3D:
	var root := Node3D.new()
	root.top_level = true
	root.process_mode = Node.PROCESS_MODE_ALWAYS
	root.visible = false
	var xray := _make_world_sticker_sprite(true, STICKER_XRAY_ALPHA)
	xray.name = "XRay"
	root.add_child(xray)
	var normal := _make_world_sticker_sprite(false, 1.0)
	normal.name = "Normal"
	root.add_child(normal)
	return root

func _configure_sticker_pool() -> void:
	if !sticker_pool.is_empty():
		return
	for slot in STICKER_POOL_SIZE:
		var sticker_node := _make_world_sticker_node()
		sticker_node.name = "WorldStickerPool%d" % slot
		add_child(sticker_node)
		sticker_pool.append(sticker_node)
		sticker_pool_actor_ids.append(-1)

func _distance_sticker_scale(distance: float) -> float:
	var t := clampf((distance - STICKER_DISTANCE_SCALE_START) / (STICKER_DISTANCE_SCALE_END - STICKER_DISTANCE_SCALE_START), 0.0, 1.0)
	t = pow(t, STICKER_DISTANCE_SCALE_EXPONENT)
	return lerpf(1.0, STICKER_FAR_SCALE, t)


func _sticker_pixel_size_for_texture(texture: Texture2D, display_scale: float) -> float:
	if texture == null:
		return STICKER_BASE_PIXEL_SIZE * display_scale
	var texture_size := texture.get_size()
	var max_texture_axis := maxf(texture_size.x, texture_size.y)
	if max_texture_axis <= 0.0:
		return STICKER_BASE_PIXEL_SIZE * display_scale
	return STICKER_BASE_PIXEL_SIZE * display_scale * (STICKER_BASE_TEXTURE_PIXELS / max_texture_axis)

func _sticker_offset_for_anchor(texture: Texture2D, anchor_direction: Vector2) -> Vector2:
	if texture == null:
		return Vector2.ZERO
	var texture_size := texture.get_size()
	var dir := anchor_direction
	if dir.length_squared() < 0.0001:
		dir = Vector2(0.0, -1.0)
	else:
		dir = dir.normalized()
	var half_size := texture_size * 0.5
	var center_distance := absf(dir.x) * half_size.x + absf(dir.y) * half_size.y
	var center := dir * center_distance
	return Vector2(center.x - half_size.x, -center.y - half_size.y)

func _set_world_sticker_sprite(sprite: Sprite3D, texture: Texture2D, pixel_size: float, alpha: float, anchor_direction: Vector2) -> void:
	sprite.texture = texture
	sprite.pixel_size = pixel_size
	sprite.modulate.a = alpha
	sprite.offset = _sticker_offset_for_anchor(texture, anchor_direction)

func _update_world_stickers(car: VisualCar) -> void:
	var camera := get_viewport().get_camera_3d()
	if camera == null or car.game_manager == null or car.game_manager.game_sim == null:
		return
	var active: Dictionary = race_presentation_controller.active_stickers
	var now := Time.get_ticks_msec()
	var camera_position := camera.global_position
	var focus_transform: Transform3D = car.game_manager.game_sim.get_player_render_transform(car.owning_id)
	sticker_candidates.clear()
	sticker_nodes.clear()
	for slot in sticker_pool.size():
		var sticker_node := sticker_pool[slot]
		if is_instance_valid(sticker_node):
			sticker_node.visible = false
		sticker_pool_actor_ids[slot] = -1
	for actor_id in active.keys():
		var actor_int := int(actor_id)
		var render_transform: Transform3D = car.game_manager.game_sim.get_player_render_transform(actor_int)
		var world_pos := render_transform.origin + render_transform.basis.y * STICKER_BASE_HEIGHT_OFFSET
		if !camera.is_position_in_frustum(world_pos):
			continue
		var owner_distance_scale := _distance_sticker_scale(focus_transform.origin.distance_to(render_transform.origin))
		var screen_direction := camera.unproject_position(world_pos) - camera.unproject_position(render_transform.origin)
		sticker_candidates.append([camera_position.distance_squared_to(world_pos), actor_int, render_transform, world_pos, owner_distance_scale, screen_direction])
	sticker_candidates.sort_custom(func(a, b): return float(a[0]) < float(b[0]))
	var visible_count := mini(sticker_candidates.size(), sticker_pool.size())
	for slot in visible_count:
		var candidate: Array = sticker_candidates[slot]
		var actor_id := int(candidate[1])
		var render_transform := candidate[2] as Transform3D
		var world_pos := candidate[3] as Vector3
		var owner_distance_scale := float(candidate[4])
		var screen_direction := candidate[5] as Vector2
		var sticker_node := sticker_pool[slot]
		sticker_pool_actor_ids[slot] = actor_id
		sticker_nodes[actor_id] = slot
		var data: Dictionary = active[actor_id]
		var sticker_index := int(data.get("sticker", 0))
		var texture: Texture2D = null
		if stickers != null and stickers.stickers.size() > 0:
			texture = stickers.stickers[wrapi(sticker_index, 0, stickers.stickers.size())]
		var started := int(data.get("started", now))
		var expires := int(data.get("expires", now))
		var age_msec := maxi(0, now - started)
		var remaining_msec := maxi(0, expires - now)
		var pop_t := clampf(float(age_msec) / 180.0, 0.0, 1.0)
		var pop_ease := pop_t * pop_t * (3.0 - 2.0 * pop_t)
		var fade_t := clampf(float(remaining_msec) / 420.0, 0.0, 1.0)
		var scale := lerpf(0.45, 1.0, pop_ease)
		var life_alpha := minf(pop_ease, fade_t)
		sticker_node.visible = true
		sticker_node.global_position = world_pos + camera.global_basis.y * (STICKER_BASE_RISE_OFFSET * pop_ease)
		var pixel_size := _sticker_pixel_size_for_texture(texture, scale * owner_distance_scale)
		var xray := sticker_node.get_node_or_null("XRay") as Sprite3D
		if xray != null:
			_set_world_sticker_sprite(xray, texture, pixel_size, life_alpha * STICKER_XRAY_ALPHA, screen_direction)
		var normal := sticker_node.get_node_or_null("Normal") as Sprite3D
		if normal != null:
			_set_world_sticker_sprite(normal, texture, pixel_size, life_alpha, screen_direction)


func _minimap_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	return material


func _configure_minimap_materials(mesh: ArrayMesh) -> void:
	var colors := {
		"road": Color(1.0, 1.0, 1.0),
		"recharge": Color(1.0, 0.3, 0.5),
		"dirt": Color(0.5, 0.2, 0.2),
		"ice": Color(0.35, 0.75, 1.0),
		"lava": Color(1.0, 0.25, 0.05),
	}
	for surface in mesh.get_surface_count():
		var surface_name := mesh.surface_get_name(surface)
		minimap_mesh.set_surface_override_material(
			surface,
			_minimap_material(colors.get(surface_name, Color.WHITE)))


func _create_minimap_markers() -> void:
	var marker_mesh := QuadMesh.new()
	marker_mesh.size = Vector2(6.0, 6.0)
	minimap_markers = MultiMesh.new()
	minimap_markers.transform_format = MultiMesh.TRANSFORM_2D
	minimap_markers.use_colors = true
	minimap_markers.mesh = marker_mesh
	minimap_marker_layer = MultiMeshInstance2D.new()
	minimap_marker_layer.name = "RacerMarkers"
	minimap_marker_layer.multimesh = minimap_markers
	minimap_marker_layer.texture = MINIMAP_MARKER_TEXTURE
	minimap_marker_layer.z_index = 1
	minimap_rect.add_child(minimap_marker_layer)


func _configure_whole_course_minimap() -> void:
	var bounds: AABB = minimap_mesh.get_aabb()
	var center: Vector3 = bounds.get_center()
	var diagonal := maxf(bounds.size.length(), 1.0)
	minimap_cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	minimap_cam.rotation_degrees = Vector3(-25.0, 45.0, 0.0)
	minimap_cam.position = center + minimap_cam.basis.z * diagonal * 1.5
	minimap_cam.near = 0.05
	minimap_cam.far = diagonal * 4.0
	var half_width := 0.0
	var half_height := 0.0
	for endpoint in 8:
		var relative: Vector3 = bounds.get_endpoint(endpoint) - center
		half_width = maxf(half_width, absf(relative.dot(minimap_cam.basis.x)))
		half_height = maxf(half_height, absf(relative.dot(minimap_cam.basis.y)))
	var aspect := float(sub_viewport.size.x) / maxf(float(sub_viewport.size.y), 1.0)
	minimap_cam.size = maxf(half_height * 2.0, half_width * 2.0 / aspect) * 1.08
	sub_viewport.render_target_update_mode = SubViewport.UPDATE_ONCE


func _configure_follow_minimap(car: VisualCar) -> void:
	var car_transform: Transform3D = car.game_manager.game_sim.get_player_render_transform(focus_player_id)
	var vehicle_up := car_transform.basis.y.normalized()
	var map_forward := -car_transform.basis.z.normalized()
	var active_camera := get_viewport().get_camera_3d()
	if active_camera != null:
		var camera_forward := -active_camera.global_basis.z.normalized()
		var planar_camera_forward := camera_forward - vehicle_up * camera_forward.dot(vehicle_up)
		if planar_camera_forward.length_squared() > 0.000001:
			map_forward = planar_camera_forward.normalized()
	var map_right := map_forward.cross(vehicle_up).normalized()
	minimap_cam.projection = Camera3D.PROJECTION_PERSPECTIVE
	minimap_cam.basis = Basis(map_right, map_forward, vehicle_up)
	minimap_cam.position = car_transform.origin + minimap_cam.basis.z * 192.0
	minimap_cam.fov = 45.0
	minimap_cam.near = 144.0
	minimap_cam.far = 288.0
	sub_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS


func _apply_minimap_mode(car: VisualCar) -> void:
	if minimap_mode == 0:
		_configure_whole_course_minimap()
	else:
		_configure_follow_minimap(car)


func _try_initialize_minimap(car: VisualCar) -> void:
	if minimap_initialized or !minimap_control.visible or car.game_manager == null \
			or car.game_manager.game_sim == null:
		return
	var generated_mesh := car.game_manager.game_sim.get_minimap_mesh() as ArrayMesh
	if generated_mesh == null or generated_mesh.get_surface_count() == 0:
		return
	minimap_mesh.mesh = generated_mesh
	_configure_minimap_materials(generated_mesh)
	_create_minimap_markers()
	minimap_rect.texture = sub_viewport.get_texture()
	minimap_initialized = true
	_apply_minimap_mode(car)


func _update_minimap(car: VisualCar) -> void:
	_try_initialize_minimap(car)
	if !minimap_initialized:
		return
	var accepts_input := true
	if car.game_manager.has_method("_window_accepts_input"):
		accepts_input = bool(car.game_manager.call("_window_accepts_input"))
	if accepts_input and Input.is_action_just_pressed("MinimapModeToggle"):
		minimap_mode = wrapi(minimap_mode + 1, 0, 2)
		_apply_minimap_mode(car)
	if minimap_mode == 1:
		_configure_follow_minimap(car)
	if minimap_marker_layer != null:
		minimap_marker_layer.scale = Vector2(
			minimap_rect.size.x / maxf(float(sub_viewport.size.x), 1.0),
			minimap_rect.size.y / maxf(float(sub_viewport.size.y), 1.0))
	car.game_manager.game_sim.update_minimap_markers(minimap_markers, minimap_cam, focus_player_id)

func _ready() -> void:
	if leaderboard_container:
		for child in leaderboard_container.get_children():
			if child is Label:
				leaderboard_labels.append(child as Label)
	if get_parent() is VisualCar:
		var car: VisualCar = get_parent()
		var ancestor := car.get_parent()
		while ancestor != null and !(ancestor is GameManager):
			ancestor = ancestor.get_parent()
		if ancestor != null:
			race_presentation_controller = ancestor.get_node("RacePresentationController") as RacePresentationControllerClass
		car_max_energy = car.calced_max_energy
		boost_energy_use_rate = car.boost_energy_use_rate
	if sboost_meter_bg:
		_sboost_full_width = sboost_meter_bg.size.x
	_set_place_badge(1)
	_build_sticker_menu()
	_ensure_check_icons(CHECK_ICON_POOL_SIZE)
	_configure_sticker_pool()

func _exit_tree() -> void:
	for sticker_node in sticker_pool:
		if is_instance_valid(sticker_node):
			sticker_node.queue_free()
	sticker_nodes.clear()
	sticker_pool.clear()
	sticker_pool_actor_ids.clear()

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
	var profile_enabled := car.render_profile_enabled
	var profile_total_start := Time.get_ticks_usec() if profile_enabled else 0
	if focus_player_id == 0:
		focus_player_id = car.owning_id
	_update_minimap(car)
	var profile_phase_start := Time.get_ticks_usec() if profile_enabled else 0
	_update_sticker_input(car)
	if profile_enabled:
		var profile_phase_us := Time.get_ticks_usec() - profile_phase_start
		render_profile_sticker_input_us += profile_phase_us
		render_profile_sticker_input_max_us = maxi(render_profile_sticker_input_max_us, profile_phase_us)
		profile_phase_start = Time.get_ticks_usec()
	_update_check_warnings(car)
	if profile_enabled:
		var profile_phase_us := Time.get_ticks_usec() - profile_phase_start
		render_profile_check_us += profile_phase_us
		render_profile_check_max_us = maxi(render_profile_check_max_us, profile_phase_us)
		profile_phase_start = Time.get_ticks_usec()
	_update_world_stickers(car)
	if profile_enabled:
		var profile_phase_us := Time.get_ticks_usec() - profile_phase_start
		render_profile_world_stickers_us += profile_phase_us
		render_profile_world_stickers_max_us = maxi(render_profile_world_stickers_max_us, profile_phase_us)
	speedometer.text = str(roundi(car.speed_kmh)) + " km/h"
	var nm := car.game_manager.network_manager
	var target_laps := int(nm.race_options.get("lap_count", 3))
	lapcounter.text = "LAP %s/%s" % [str(car.lap), "∞" if target_laps == 0 else str(target_laps)]
	var use_tick := nm.input_transport.get_race_tick()
	var local_id := multiplayer.get_unique_id() if multiplayer.multiplayer_peer else 0
	var place_id := focus_player_id
	if car.game_manager != null and car.game_manager.replay_controller.replay_playback_active:
		local_id = focus_player_id
		use_tick = car.game_manager._singleplayer_tick
	if nm.race_results.player_finish_times.has(focus_player_id):
		use_tick = nm.race_results.player_finish_times[focus_player_id]
	elif nm.race_results.player_finish_times.has(local_id):
		use_tick = nm.race_results.player_finish_times[local_id]
	elif nm.race_results.player_finish_times.has(car.owning_id):
		use_tick = nm.race_results.player_finish_times[car.owning_id]
	var official_start_tick := 300
	if car.game_manager != null and car.game_manager.game_sim != null:
		official_start_tick = int(car.game_manager.game_sim.get_player_level_start_time(focus_player_id))
	var time_elapsed : int = use_tick - official_start_tick
	var time_elapsed_float : float = float(time_elapsed) / 60
	var seconds : int = int(floor(time_elapsed_float)) % 60
	var milliseconds : int = int(floor(time_elapsed_float * 1000)) % 1000
	var minutes : int = floor(time_elapsed_float / 60)
	racetimer.text = "%d:%02d.%03d" % [minutes, seconds, milliseconds]
	var force_end_seconds := nm.force_end_countdown_seconds_for(focus_player_id)
	forceendtimer.visible = force_end_seconds >= 0
	if forceendtimer.visible:
		forceendtimer.text = "FORCE END %02d" % force_end_seconds
		var flash := 0.35 + 0.65 * absf(sin(float(Time.get_ticks_msec()) * 0.012))
		forceendtimer.modulate = Color(1.0, 0.05, 0.05, flash)
	car_max_energy = maxf(car.calced_max_energy, 1.0)
	boost_energy_use_rate = car.boost_energy_use_rate
	healthmeter.scale.x = car_max_energy * 0.01
	var health_meter_shader := healthmeter.material as ShaderMaterial
	health_meter_shader.set_shader_parameter("health_amount", car.energy)
	health_meter_shader.set_shader_parameter("max_health_amount", car_max_energy)
	var boost_unlocked_from_start := car.game_manager != null \
		and car.game_manager.game_sim != null \
		and car.game_manager.game_sim.has_method("get_boost_unlocked_from_start") \
		and bool(car.game_manager.game_sim.call("get_boost_unlocked_from_start"))
	health_meter_shader.set_shader_parameter(
		"can_boost",
		boost_unlocked_from_start or car.lap > 1)
	var remaining_boost_energy_cost := float(car.boost_frames_manual) * 0.1666666667 * boost_energy_use_rate
	health_meter_shader.set_shader_parameter("health_to_deplete", remaining_boost_energy_cost)
	var full_boost_energy_cost := car.manual_boost_duration_seconds * 10.0 * boost_energy_use_rate
	health_meter_shader.set_shader_parameter("boost_energy_cost", full_boost_energy_cost)
	
	var time_until_start : float = float(official_start_tick - use_tick) / 60
	countdown_arrow.rotation_degrees = 360 - minf(270, (time_until_start * 90))
	if time_until_start <= 0 and countdowncontrol.modulate.a > 0:
		countdowncontrol.scale += Vector2(1, 1) * _delta * 4
		countdowncontrol.modulate.a = max(0, countdowncontrol.modulate.a - _delta * 4)
	
	var our_place := 1

	if nm.race_results.player_finish_placements.has(place_id):
		our_place = nm.race_results.player_finish_placements[place_id]
	
	var profile_leaderboard_start := Time.get_ticks_usec() if profile_enabled else 0
	our_place = _update_leaderboard(car, nm, place_id, our_place)
	if profile_enabled:
		var profile_leaderboard_us := Time.get_ticks_usec() - profile_leaderboard_start
		render_profile_leaderboard_us += profile_leaderboard_us
		render_profile_leaderboard_max_us = maxi(render_profile_leaderboard_max_us, profile_leaderboard_us)
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

	if attack_meter and attack_meter_icon:
		var attack_cooldown := clampf(float(car.attack_cooldown_frames), 0.0, ATTACK_COOLDOWN_FRAMES)
		var attack_ready := attack_cooldown <= 0.0
		var fill_ratio := 1.0 if attack_ready else 1.0 - attack_cooldown / ATTACK_COOLDOWN_FRAMES
		fill_ratio = clampf(fill_ratio, 0.0, 1.0)
		attack_meter_border.visible = attack_ready
		var attack_meter_shader := attack_meter_icon.material as ShaderMaterial
		if attack_meter_shader:
			attack_meter_shader.set_shader_parameter("fill_amount", fill_ratio)
	if profile_enabled:
		var profile_total_us := Time.get_ticks_usec() - profile_total_start
		render_profile_total_us += profile_total_us
		render_profile_total_max_us = maxi(render_profile_total_max_us, profile_total_us)
		render_profile_frames += 1
	
