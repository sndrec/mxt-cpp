class_name LobbyChibiCar
extends Node3D

const CarLivery = preload("res://vehicle/customization/car_livery.gd")

const BOUNDS_X := 25.0
const BOUNDS_Z := 10.0
const CHIBI_TICK_DELTA := 1.0 / 60.0
const SYNC_INTERVAL_MSEC := 100

var player_id := 0
var player_settings: Dictionary = {}
var game_manager: Node
var lobby_camera: Camera3D
var nameplate_parent: Control
var local_control := false

var velocity := 0.0
var knockback_velocity := Vector3.ZERO
var angle_velocity := 0.0
var chibi_weight := 1400.0
var chibi_acceleration := 20.0
var chibi_top_speed := 0.0
var chibi_friction := 0.6
var chibi_steer_power := 20.0
var chibi_strafe_power := 30.0

var visual_root: Node3D
var nameplate: Control
var nameplate_panel: PanelContainer
var username_label: Label
var ping_label: Label
var last_sync_msec := 0
var car_definition: CarDefinition
var hovered := false
var nameplate_attachment := Vector2.ZERO
var settings_revision := -1
var render_livery_hash := ""

func setup(in_player_id: int, in_settings: Dictionary, in_game_manager: Node, in_camera: Camera3D, in_nameplate_parent: Control, in_local_control: bool, in_settings_revision: int = 0) -> void:
	player_id = in_player_id
	game_manager = in_game_manager
	lobby_camera = in_camera
	nameplate_parent = in_nameplate_parent
	local_control = in_local_control
	settings_revision = in_settings_revision
	_apply_settings(in_settings)
	_ensure_nameplate()
	_rebuild_visual()

func set_local_control(enabled: bool) -> void:
	local_control = enabled
	if nameplate != null:
		nameplate.modulate = Color(1.0, 0.75, 0.25) if local_control else Color.WHITE

func update_settings(in_settings: Dictionary, in_settings_revision: int = -1) -> void:
	if in_settings_revision >= 0 and in_settings_revision == settings_revision:
		return
	var old_content_id := str(player_settings.get("vehicle_content_id", ""))
	var old_livery_hash := render_livery_hash
	if in_settings_revision >= 0:
		settings_revision = in_settings_revision
	_apply_settings(in_settings)
	_ensure_nameplate()
	if old_content_id != str(player_settings.get("vehicle_content_id", "")) or old_livery_hash != render_livery_hash:
		_rebuild_visual()

func apply_remote_state(in_velocity: float, in_knockback: Vector3, in_angle_velocity: float, in_position: Vector3, in_rotation: Vector3) -> void:
	if local_control:
		return
	velocity = in_velocity
	knockback_velocity = in_knockback
	angle_velocity = in_angle_velocity
	position = Vector3(in_position.x, position.y, in_position.z)
	rotation = Vector3(rotation.x, in_rotation.y, rotation.z)

func _exit_tree() -> void:
	if nameplate != null and is_instance_valid(nameplate):
		nameplate.queue_free()

func _apply_settings(in_settings: Dictionary) -> void:
	player_settings = in_settings if typeof(in_settings) == TYPE_DICTIONARY else {}
	render_livery_hash = _calculate_render_livery_hash()
	if username_label != null:
		var next_username := str(player_settings.get("username", str(player_id)))
		if username_label.text != next_username:
			username_label.text = next_username
			username_label.reset_size()
			if nameplate_panel != null:
				nameplate_panel.reset_size()

func _ensure_nameplate() -> void:
	if nameplate != null:
		return
	if nameplate_parent == null:
		return
	nameplate = Control.new()
	nameplate.mouse_filter = Control.MOUSE_FILTER_IGNORE
	nameplate.custom_minimum_size = Vector2(70.0, 52.0)
	nameplate.visible = false
	nameplate_parent.add_child(nameplate)

	nameplate_panel = PanelContainer.new()
	nameplate_panel.clip_contents = true
	nameplate.add_child(nameplate_panel)

	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.0, 0.0, 0.0, 0.56)
	style.border_color = Color(1.0, 1.0, 1.0, 0.12)
	style.set_border_width_all(1)
	nameplate_panel.add_theme_stylebox_override("panel", style)

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 3)
	margin.add_theme_constant_override("margin_top", 3)
	margin.add_theme_constant_override("margin_right", 3)
	margin.add_theme_constant_override("margin_bottom", 3)
	nameplate_panel.add_child(margin)

	var labels := VBoxContainer.new()
	labels.add_theme_constant_override("separation", 1)
	margin.add_child(labels)

	username_label = Label.new()
	username_label.text = str(player_settings.get("username", str(player_id)))
	username_label.add_theme_font_size_override("font_size", 20)
	labels.add_child(username_label)

	ping_label = Label.new()
	ping_label.text = "0ms"
	ping_label.add_theme_font_size_override("font_size", 14)
	ping_label.modulate = Color(0.75, 0.85, 1.0, 0.9)
	labels.add_child(ping_label)

	set_local_control(local_control)

func _update_nameplate_layout(anchor: Vector2) -> void:
	if nameplate == null or nameplate_panel == null:
		return
	var panel_min_size := nameplate_panel.get_combined_minimum_size()
	nameplate_panel.size = panel_min_size
	nameplate.position = anchor
	nameplate_panel.position = Vector2(-panel_min_size.x * 0.5, -panel_min_size.y)

func _rebuild_visual() -> void:
	if visual_root != null and is_instance_valid(visual_root):
		visual_root.queue_free()
	visual_root = null
	car_definition = null

	var vehicle_content_id := str(player_settings.get("vehicle_content_id", ""))
	if vehicle_content_id != "" and game_manager != null:
		var definition: CarDefinition = game_manager.get_car_definition(vehicle_content_id)
		if definition != null and definition.car_scene != null:
			car_definition = definition
			_load_chibi_stats(definition)
	if visual_root == null:
		if car_definition != null:
			return
		var mesh_instance := MeshInstance3D.new()
		var mesh := BoxMesh.new()
		mesh.size = Vector3(1.2, 0.35, 2.0)
		mesh_instance.mesh = mesh
		var material := StandardMaterial3D.new()
		material.albedo_color = Color(0.25, 0.65, 1.0, 1.0)
		mesh_instance.material_override = material
		visual_root = mesh_instance
		add_child(visual_root)

func get_render_definition() -> CarDefinition:
	return car_definition

func get_render_livery_hash() -> String:
	return render_livery_hash

func _calculate_render_livery_hash() -> String:
	if !player_settings.has("car_livery") or typeof(player_settings["car_livery"]) != TYPE_DICTIONARY:
		return ""
	var livery := CarLivery.new()
	livery.from_dict(player_settings["car_livery"])
	return livery.get_livery_hash()

func get_render_transform() -> Transform3D:
	return global_transform * Transform3D(
		Basis(Vector3.UP, PI).scaled(Vector3.ONE * 1.0),
		Vector3.ZERO)

func get_render_overlay() -> Color:
	return Color(0.0, 0.0, 0.0, 1.0)

func get_render_outline_overlay() -> Color:
	return Color(0.0, 0.0, 0.0, 1.0)

func get_render_outline_velocity() -> Vector3:
	return basis.z * 0.01

func get_render_thrust() -> float:
	return clampf(velocity / 220.0, 0.0, 1.0)

func set_hovered(enabled: bool) -> void:
	if hovered == enabled:
		return
	hovered = enabled
	_update_nameplate()

func set_nameplate_attachment(anchor: Vector2) -> void:
	nameplate_attachment = anchor
	if hovered:
		_update_nameplate_layout(nameplate_attachment)

func get_hover_anchor() -> Vector2:
	if lobby_camera == null or lobby_camera.is_position_behind(global_position):
		return Vector2(-100000.0, -100000.0)
	return lobby_camera.unproject_position(global_position)

func _load_chibi_stats(definition: CarDefinition) -> void:
	var prop_path := definition.properties_path
	if prop_path == "" or !FileAccess.file_exists(prop_path) or game_manager == null or game_manager.game_sim == null:
		return
	var bytes := FileAccess.get_file_as_bytes(prop_path)
	var machine_setting := clampf(float(player_settings.get("accel_setting", 0.5)), 0.0, 1.0)
	var sampled: Dictionary = game_manager.game_sim.sample_car_properties(bytes, machine_setting)
	var stats = sampled.get("base_stats", {})
	if typeof(stats) != TYPE_DICTIONARY or stats.is_empty():
		return
	chibi_weight = float(stats.get("weight_kg", chibi_weight))
	chibi_acceleration = float(stats.get("acceleration", chibi_acceleration / 40.0)) * 40.0
	chibi_top_speed = float(stats.get("max_speed", chibi_top_speed))
	chibi_friction = float(stats.get("grip_1", chibi_friction))
	chibi_steer_power = float(stats.get("turn_movement", chibi_steer_power * 7.0)) / 7.0
	chibi_strafe_power = float(stats.get("strafe", chibi_strafe_power))

func _configure_visual_meshes(root: Node) -> void:
	for child in root.get_children():
		_configure_visual_meshes(child)
	var mesh := root as MeshInstance3D
	if mesh == null:
		return
	mesh.layers = 1
	mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	if mesh.name.find("SHADOW") != -1 or mesh.name.find("OUTLINE") != -1:
		mesh.visible = false

func _physics_process(delta: float) -> void:
	if !_is_lobby_chibi_active():
		return
	if local_control and _can_accept_input():
		_update_local_drive(delta)
	_update_motion(delta)
	_update_nameplate()
	if local_control:
		_sync_state_if_needed()

func _is_lobby_chibi_active() -> bool:
	if game_manager != null and game_manager.has_method("_lobby_chibi_active"):
		return bool(game_manager.call("_lobby_chibi_active"))
	return true

func _can_accept_input() -> bool:
	if game_manager != null and game_manager.has_method("_lobby_accepts_chibi_input"):
		return bool(game_manager.call("_lobby_accepts_chibi_input"))
	return true

func _update_local_drive(delta: float) -> void:
	var steer := Input.get_axis("SteerLeft", "SteerRight")
	var accel := Input.get_action_strength("Accelerate")
	var strafe := Input.get_action_strength("StrafeRight") - Input.get_action_strength("StrafeLeft")
	angle_velocity = steer * chibi_steer_power * 0.5
	if accel > 0.01:
		velocity += _calculate_accel_with_speed(velocity * 10.0) * 100.0 * accel * delta
	else:
		velocity = maxf(0.0, velocity - _calculate_friction(velocity) * 10.0 * delta)
	position += basis.x.slide(Vector3.UP).normalized() * strafe * chibi_strafe_power * velocity * -delta * 0.0015

func _calculate_accel_with_speed(in_speed: float) -> float:
	var base_max_speed := 900.0 + chibi_weight * 0.01 + chibi_top_speed * 100.0
	var accel_ratio := remap(in_speed, 0.0, base_max_speed, 0.0, 1.0)
	accel_ratio = clampf(accel_ratio, 0.0, 1.0)
	return lerpf(chibi_acceleration * 4.0, 0.0, accel_ratio) * CHIBI_TICK_DELTA

func _calculate_friction(in_speed: float) -> float:
	var friction_reduction_proportional := in_speed * chibi_friction * 0.05
	var friction_reduction_linear := chibi_friction * 4.0
	return friction_reduction_linear + friction_reduction_proportional

func _update_motion(delta: float) -> void:
	position.y = lerpf(0.0, 0.05, sin(0.005 * float(Time.get_ticks_msec())))
	rotation_degrees += Vector3(0.0, angle_velocity * delta * -20.0, 0.0)
	position += basis.z * velocity * delta * 0.1 + knockback_velocity * delta * 0.2
	rotation_degrees.z = lerpf(rotation_degrees.z, angle_velocity * 2.0, delta * 4.0)
	if absf(position.x) > BOUNDS_X:
		velocity *= 0.5
		knockback_velocity += Vector3(-1.0, 0.0, 0.0) * signf(position.x) * velocity
		position.x = clampf(position.x, -BOUNDS_X, BOUNDS_X)
	if absf(position.z) > BOUNDS_Z:
		velocity *= 0.5
		knockback_velocity += Vector3(0.0, 0.0, -1.0) * signf(position.z) * velocity
		position.z = clampf(position.z, -BOUNDS_Z, BOUNDS_Z)
	knockback_velocity += -knockback_velocity * 8.0 * delta

func _update_nameplate() -> void:
	if nameplate == null or lobby_camera == null:
		return
	if !hovered or lobby_camera.is_position_behind(global_position):
		nameplate.visible = false
		return
	if ping_label != null and game_manager != null and game_manager.has_method("_lobby_latency_text_for_player"):
		var next_ping := str(game_manager.call("_lobby_latency_text_for_player", player_id))
		if ping_label.text != next_ping:
			ping_label.text = next_ping
	nameplate.visible = true
	_update_nameplate_layout(nameplate_attachment)

func _sync_state_if_needed() -> void:
	if game_manager == null or !game_manager.has_method("_send_lobby_chibi_state"):
		return
	var now := Time.get_ticks_msec()
	if now < last_sync_msec + SYNC_INTERVAL_MSEC:
		return
	game_manager.call("_send_lobby_chibi_state", player_id, velocity, knockback_velocity, angle_velocity, position, rotation)
	last_sync_msec = now
