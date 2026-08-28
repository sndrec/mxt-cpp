class_name ReplayCameraController
extends Node

const CAMERA_GAME := 0
const CAMERA_AUTO := 1
const CAMERA_SPECTATOR := 2
const CAMERA_RELATIVE := 3
const RELATIVE_DEFAULT_OFFSET := Vector3(0.0, 8.0, 28.0)
const RELATIVE_LOOK_TARGET := Vector3(0.0, 2.0, 0.0)
const RELATIVE_LOOK_SPEED := 0.0025
const RELATIVE_LOOK_ACTION_SPEED := 6.0
const RELATIVE_ROLL_SPEED := 4.0
const MANUAL_FAST_MULTIPLIER := 3.0
const MANUAL_FOV_MIN := 10.0
const MANUAL_FOV_MAX := 150.0
const MANUAL_SPEED_MIN := 1.0
const MANUAL_SPEED_MAX := 5000.0

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var playback: ReplayController = get_node("../ReplayController") as ReplayController
@onready var vehicle_content_controller: VehicleContentController = get_node("../VehicleContentController") as VehicleContentController
@onready var spectator_controller: SpectatorController = get_node("../SpectatorController") as SpectatorController
@onready var race_presentation_controller: RacePresentationController = get_node("../RacePresentationController") as RacePresentationController
@onready var debug_runtime_controller: DebugRuntimeController = get_node("../DebugRuntimeController") as DebugRuntimeController
@onready var timeline: ReplayTimelineController = get_node("../ReplayTimelineController") as ReplayTimelineController

var mode := CAMERA_GAME
var auto_camera: Camera3D
var relative_camera: Camera3D
var relative_gravity_basis := Basis.IDENTITY
var relative_gravity_basis_valid := false
var relative_camera_basis := Basis.IDENTITY
var relative_camera_basis_desired := Basis.IDENTITY
var relative_offset := Vector3.ZERO
var relative_velocity := Vector3.ZERO
var relative_pending_look_delta := Vector2.ZERO
var manual_fov := 72.0
var manual_speed := 300.0
var input_calibration: InputCalibration
var controls: HBoxContainer
var fov_slider: HSlider
var fov_value: SpinBox
var speed_slider: HSlider
var speed_value: SpinBox


func initialize() -> void:
	reload_input_calibration()


func reload_input_calibration() -> void:
	input_calibration = InputCalibration.load_from_disk()


func build_controls(parent: Container) -> void:
	if controls != null and is_instance_valid(controls):
		return
	controls = HBoxContainer.new()
	controls.name = "ManualCameraControls"
	controls.add_theme_constant_override("separation", 8)
	controls.visible = false
	parent.add_child(controls)
	var fov_label := Label.new()
	fov_label.text = "FoV"
	controls.add_child(fov_label)
	fov_slider = HSlider.new()
	fov_slider.min_value = MANUAL_FOV_MIN
	fov_slider.max_value = MANUAL_FOV_MAX
	fov_slider.step = 1.0
	fov_slider.value = manual_fov
	fov_slider.custom_minimum_size.x = 180.0
	fov_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	fov_slider.value_changed.connect(_on_fov_changed)
	controls.add_child(fov_slider)
	fov_value = SpinBox.new()
	fov_value.min_value = MANUAL_FOV_MIN
	fov_value.max_value = MANUAL_FOV_MAX
	fov_value.step = 1.0
	fov_value.value = manual_fov
	fov_value.suffix = " deg"
	fov_value.custom_minimum_size.x = 105.0
	fov_value.value_changed.connect(_on_fov_changed)
	controls.add_child(fov_value)
	var speed_label := Label.new()
	speed_label.text = "Speed"
	controls.add_child(speed_label)
	speed_slider = HSlider.new()
	speed_slider.min_value = MANUAL_SPEED_MIN
	speed_slider.max_value = MANUAL_SPEED_MAX
	speed_slider.step = 1.0
	speed_slider.exp_edit = true
	speed_slider.value = manual_speed
	speed_slider.custom_minimum_size.x = 180.0
	speed_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	speed_slider.tooltip_text = "Base movement speed. Hold Shift for 3x speed."
	speed_slider.value_changed.connect(_on_speed_changed)
	controls.add_child(speed_slider)
	speed_value = SpinBox.new()
	speed_value.min_value = MANUAL_SPEED_MIN
	speed_value.max_value = MANUAL_SPEED_MAX
	speed_value.step = 1.0
	speed_value.value = manual_speed
	speed_value.suffix = " u/s"
	speed_value.custom_minimum_size.x = 115.0
	speed_value.tooltip_text = "Base movement speed. Hold Shift for 3x speed."
	speed_value.value_changed.connect(_on_speed_changed)
	controls.add_child(speed_value)


func update_control_visibility() -> void:
	if controls != null:
		controls.visible = playback.replay_playback_active and (
			mode == CAMERA_RELATIVE or mode == CAMERA_SPECTATOR)


func handle_unhandled_input(event: InputEvent) -> bool:
	if !playback.replay_playback_active:
		return false
	if event is InputEventMouseButton:
		var mouse_button := event as InputEventMouseButton
		if mouse_button.button_index == MOUSE_BUTTON_RIGHT and mouse_button.pressed \
				and _mode_uses_mouse_capture():
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			return true
	if event.is_action_pressed("SpinAttack"):
		cycle_mode()
		return true
	if event.is_action_pressed("DpadLeft"):
		change_focus(-1)
		return true
	if event.is_action_pressed("DpadRight"):
		change_focus(1)
		return true
	return false


func _input(event: InputEvent) -> void:
	if playback.replay_playback_active and event is InputEventMouseButton:
		var mouse_button := event as InputEventMouseButton
		if !mouse_button.pressed and mouse_button.button_index == MOUSE_BUTTON_RIGHT:
			if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			get_viewport().set_input_as_handled()
			return
	if !playback.replay_playback_active or mode != CAMERA_RELATIVE:
		return
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		relative_pending_look_delta += (event as InputEventMouseMotion).relative
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("ui_cancel") and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		get_viewport().set_input_as_handled()


func update(delta: float) -> void:
	_update_auto_camera(delta)
	_update_relative_camera(delta)


func reset() -> void:
	mode = CAMERA_GAME
	relative_pending_look_delta = Vector2.ZERO
	relative_velocity = Vector3.ZERO
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	update_control_visibility()


func focused_player_id() -> int:
	if playback.replay_playback_racer_ids.is_empty():
		return game_manager._local_player_id()
	playback.replay_playback_focus_index = clampi(
		playback.replay_playback_focus_index, 0,
		playback.replay_playback_racer_ids.size() - 1)
	return int(playback.replay_playback_racer_ids[playback.replay_playback_focus_index])


func apply_focus_to_local_visual() -> void:
	if !playback.replay_playback_active or game_manager.car_node_container.local_visual_car == null:
		return
	var focus_id := focused_player_id()
	var car := game_manager.car_node_container.local_visual_car
	car.owning_id = focus_id
	car.race_hud.focus_player_id = focus_id
	var settings := game_manager.network_manager.lobby_settings.get_player_settings(focus_id)
	if !settings.is_empty():
		var player_settings := vehicle_content_controller.player_settings_for_stamp_render(settings)
		if player_settings != null:
			car.player_settings = player_settings
	if is_instance_valid(car.name_label):
		car.name_label.text = race_presentation_controller.player_display_name(focus_id)
	timeline.apply_hud_visibility()
	if !debug_runtime_controller.disable_hud and !debug_runtime_controller.disable_hud_process_only:
		car.race_hud.process_mode = Node.PROCESS_MODE_INHERIT


func apply_mode() -> void:
	if !playback.replay_playback_active:
		return
	apply_focus_to_local_visual()
	if mode == CAMERA_GAME and game_manager.car_node_container.local_visual_car != null:
		spectator_controller.disable_free_camera()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		game_manager.game_sim.set_gameplay_camera(
			game_manager.car_node_container.local_visual_car.car_camera, focused_player_id())
		game_manager.car_node_container.local_visual_car.car_camera.make_current()
		game_manager.car_node_container.local_visual_car.make_vehicle_audio_listener_current()
	elif mode == CAMERA_AUTO:
		spectator_controller.disable_free_camera()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_ensure_auto_camera().make_current()
	elif mode == CAMERA_RELATIVE:
		spectator_controller.disable_free_camera()
		_reset_relative_camera()
		_ensure_relative_camera().make_current()
		apply_manual_settings()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	else:
		spectator_controller.show_free_camera_at(_focused_transform())
		apply_manual_settings()
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	timeline.update()


func cycle_mode() -> void:
	mode = (mode + 1) % 4
	apply_mode()
	race_presentation_controller.show_notification(
		"Replay Camera: %s" % mode_name(), 1200)


func mode_name() -> String:
	match mode:
		CAMERA_GAME:
			return "Game"
		CAMERA_AUTO:
			return "Auto"
		CAMERA_RELATIVE:
			return "Relative Cam"
		_:
			return "Spectator"


func change_focus(delta: int) -> void:
	if !playback.replay_playback_active or playback.replay_playback_racer_ids.is_empty():
		return
	if mode != CAMERA_GAME and mode != CAMERA_AUTO and mode != CAMERA_RELATIVE:
		return
	playback.replay_playback_focus_index = posmod(
		playback.replay_playback_focus_index + delta,
		playback.replay_playback_racer_ids.size())
	apply_mode()
	timeline.refresh_input_display()
	timeline.replay_timeline_markers_dirty = true
	race_presentation_controller.show_notification(
		"Replay Focus: %s" % race_presentation_controller.player_display_name(
			focused_player_id()), 1200)


func apply_manual_settings() -> void:
	if relative_camera != null and is_instance_valid(relative_camera):
		relative_camera.fov = manual_fov
	if spectator_controller.spectator != null and is_instance_valid(spectator_controller.spectator):
		var free_camera := spectator_controller.spectator
		free_camera.move_speed = manual_speed
		free_camera.fast_move_speed = manual_speed * MANUAL_FAST_MULTIPLIER
		if free_camera.camera != null:
			free_camera.camera.fov = manual_fov


func _on_fov_changed(value: float) -> void:
	manual_fov = clampf(value, MANUAL_FOV_MIN, MANUAL_FOV_MAX)
	if fov_slider != null:
		fov_slider.set_value_no_signal(manual_fov)
	if fov_value != null:
		fov_value.set_value_no_signal(manual_fov)
	apply_manual_settings()


func _on_speed_changed(value: float) -> void:
	manual_speed = clampf(value, MANUAL_SPEED_MIN, MANUAL_SPEED_MAX)
	if speed_slider != null:
		speed_slider.set_value_no_signal(manual_speed)
	if speed_value != null:
		speed_value.set_value_no_signal(manual_speed)
	apply_manual_settings()


func _ensure_auto_camera() -> Camera3D:
	if auto_camera == null or !is_instance_valid(auto_camera):
		auto_camera = Camera3D.new()
		auto_camera.name = "ReplayAutoCamera"
		auto_camera.near = 0.25
		auto_camera.far = 40000.0
		auto_camera.fov = 70.0
		game_manager.get_node("GameWorld").add_child(auto_camera)
	return auto_camera


func _ensure_relative_camera() -> Camera3D:
	if relative_camera == null or !is_instance_valid(relative_camera):
		relative_camera = Camera3D.new()
		relative_camera.name = "ReplayRelativeCamera"
		relative_camera.near = 0.25
		relative_camera.far = 40000.0
		relative_camera.fov = manual_fov
		game_manager.get_node("GameWorld").add_child(relative_camera)
	return relative_camera


func _focused_car() -> VisualCar:
	var focus_id := focused_player_id()
	if game_manager.car_node_container.local_visual_car != null \
			and game_manager.car_node_container.local_visual_car.owning_id == focus_id:
		return game_manager.car_node_container.local_visual_car
	for car in game_manager.car_node_container.get_children():
		if car is VisualCar and car.owning_id == focus_id:
			return car
	return null


func _focused_transform() -> Transform3D:
	if game_manager.game_sim != null and game_manager.game_sim.has_method(
			"get_player_physical_render_transform"):
		return game_manager.game_sim.get_player_physical_render_transform(focused_player_id())
	if game_manager.game_sim != null and game_manager.game_sim.has_method("get_player_render_transform"):
		return game_manager.game_sim.get_player_render_transform(focused_player_id())
	var car := _focused_car()
	return Transform3D(car.basis_physical.basis, car.position_current) \
		if car != null else Transform3D.IDENTITY


func _focused_up() -> Vector3:
	if game_manager.game_sim != null and game_manager.game_sim.has_method(
			"get_player_physical_render_up"):
		var native_up: Vector3 = game_manager.game_sim.get_player_physical_render_up(focused_player_id())
		if native_up.length_squared() > 0.0001:
			return native_up.normalized()
	var car := _focused_car()
	if car != null and car.track_surface_normal.length_squared() > 0.0001:
		return car.track_surface_normal.normalized()
	var transform := _focused_transform()
	return transform.basis.y.normalized() \
		if transform.basis.y.length_squared() > 0.0001 else Vector3.UP


func _action_strength(action_name: String) -> float:
	return Input.get_action_strength(action_name) if InputMap.has_action(action_name) else 0.0


func _action_axis(negative_action: String, positive_action: String) -> float:
	return _action_strength(positive_action) - _action_strength(negative_action)


func _calibrated_strafe_axis() -> float:
	var raw_left := Input.get_action_raw_strength("StrafeLeft")
	var raw_right := Input.get_action_raw_strength("StrafeRight")
	if input_calibration == null:
		reload_input_calibration()
	return input_calibration.apply_strafe_right(raw_right) \
		- input_calibration.apply_strafe_left(raw_left)


func _gravity_basis_from_up(up: Vector3, preserve_basis: Basis, fallback_basis: Basis) -> Basis:
	up = up.normalized() if up.length_squared() > 0.0001 else Vector3.UP
	var forward := -preserve_basis.z
	forward -= up * forward.dot(up)
	if forward.length_squared() <= 0.0001:
		forward = -fallback_basis.z
		forward -= up * forward.dot(up)
	if forward.length_squared() <= 0.0001:
		forward = up.cross(fallback_basis.x)
	if forward.length_squared() <= 0.0001:
		var seed := Vector3.FORWARD if absf(up.dot(Vector3.FORWARD)) <= 0.95 else Vector3.RIGHT
		forward = seed - up * seed.dot(up)
	forward = forward.normalized()
	var right := forward.cross(up).normalized()
	forward = up.cross(right).normalized()
	return Basis(right, up, -forward).orthonormalized()


func _apply_relative_transform(car_transform: Transform3D) -> void:
	var camera := _ensure_relative_camera()
	var camera_basis := (relative_gravity_basis * relative_camera_basis).orthonormalized()
	var camera_position := car_transform.origin + relative_gravity_basis * relative_offset
	camera.global_transform = Transform3D(camera_basis, camera_position)


func _reset_relative_camera() -> void:
	relative_gravity_basis_valid = false
	relative_pending_look_delta = Vector2.ZERO
	relative_velocity = Vector3.ZERO
	relative_offset = RELATIVE_DEFAULT_OFFSET
	var camera := _ensure_relative_camera()
	var car_transform := _focused_transform()
	relative_gravity_basis = _gravity_basis_from_up(
		_focused_up(), car_transform.basis, car_transform.basis)
	relative_gravity_basis_valid = true
	var local_look := Transform3D(Basis.IDENTITY, relative_offset).looking_at(
		RELATIVE_LOOK_TARGET, Vector3.UP)
	relative_camera_basis_desired = local_look.basis.orthonormalized()
	relative_camera_basis = relative_camera_basis_desired
	_apply_relative_transform(car_transform)
	camera.current = true


func _mode_uses_mouse_capture() -> bool:
	return mode == CAMERA_RELATIVE or mode == CAMERA_SPECTATOR


func _update_auto_camera(delta: float) -> void:
	if !playback.replay_playback_active or mode != CAMERA_AUTO:
		return
	var camera := _ensure_auto_camera()
	var car_transform := _focused_transform()
	var speed_scale := 0.5
	var car := _focused_car()
	if car != null:
		speed_scale = clampf(car.speed_kmh / 1800.0, 0.0, 1.0)
	var target := car_transform.origin + car_transform.basis.y * 2.0
	var desired := target - car_transform.basis.z * lerpf(24.0, 42.0, speed_scale) \
		+ car_transform.basis.y * lerpf(9.0, 15.0, speed_scale)
	camera.global_position = camera.global_position.lerp(
		desired, clampf(delta * 4.0, 0.0, 1.0))
	camera.look_at(target, car_transform.basis.y.normalized())


func _update_relative_camera(delta: float) -> void:
	if !playback.replay_playback_active or mode != CAMERA_RELATIVE:
		return
	var car_transform := _focused_transform()
	var desired_gravity_basis := relative_gravity_basis
	if relative_gravity_basis_valid:
		desired_gravity_basis = _gravity_basis_from_up(
			_focused_up(), relative_gravity_basis, car_transform.basis)
	else:
		desired_gravity_basis = _gravity_basis_from_up(
			_focused_up(), car_transform.basis, car_transform.basis)
		relative_gravity_basis = desired_gravity_basis
		relative_gravity_basis_valid = true
	relative_gravity_basis = relative_gravity_basis.slerp(
		desired_gravity_basis, clampf(delta * 5.0, 0.0, 1.0)).orthonormalized()

	var look_delta := relative_pending_look_delta
	relative_pending_look_delta = Vector2.ZERO
	var pitch_amount := -look_delta.y * RELATIVE_LOOK_SPEED
	var yaw_amount := -look_delta.x * RELATIVE_LOOK_SPEED
	pitch_amount += _action_axis("CameraUp", "CameraDown") * delta * -RELATIVE_LOOK_ACTION_SPEED
	pitch_amount += _action_axis("CamForward", "CamBack") * delta * -RELATIVE_LOOK_ACTION_SPEED
	yaw_amount += _action_axis("CameraLeft", "CameraRight") * delta * -RELATIVE_LOOK_ACTION_SPEED
	yaw_amount += _action_axis("CamLeft", "CamRight") * delta * -RELATIVE_LOOK_ACTION_SPEED
	var roll_input := _calibrated_strafe_axis()
	if Input.is_physical_key_pressed(KEY_Q):
		roll_input -= 1.0
	if Input.is_physical_key_pressed(KEY_E):
		roll_input += 1.0
	var roll_amount := clampf(roll_input, -1.0, 1.0) * delta * -RELATIVE_ROLL_SPEED
	if pitch_amount != 0.0:
		relative_camera_basis_desired = relative_camera_basis_desired.rotated(
			relative_camera_basis_desired.x, pitch_amount)
	if yaw_amount != 0.0:
		relative_camera_basis_desired = relative_camera_basis_desired.rotated(
			relative_camera_basis_desired.y, yaw_amount)
	if roll_amount != 0.0:
		relative_camera_basis_desired = relative_camera_basis_desired.rotated(
			relative_camera_basis_desired.z, roll_amount)
	relative_camera_basis_desired = relative_camera_basis_desired.orthonormalized()
	relative_camera_basis = relative_camera_basis.slerp(
		relative_camera_basis_desired, clampf(delta * 8.0, 0.0, 1.0)).orthonormalized()

	var move_input := Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		move_input.z -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		move_input.z += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		move_input.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		move_input.x += 1.0
	if Input.is_physical_key_pressed(KEY_SPACE):
		move_input.y += 1.0
	if Input.is_physical_key_pressed(KEY_CTRL):
		move_input.y -= 1.0
	move_input.x += _action_axis("MoveLeft", "MoveRight")
	move_input.x += _action_axis("SteerLeft", "SteerRight")
	move_input.z += _action_axis("MoveForward", "MoveBack")
	move_input.z += _action_axis("SteerUp", "SteerDown")
	if move_input.length_squared() > 1.0:
		move_input = move_input.normalized()
	var current_speed := manual_speed * MANUAL_FAST_MULTIPLIER \
		if Input.is_physical_key_pressed(KEY_SHIFT) else manual_speed
	var desired_velocity := relative_camera_basis * move_input * current_speed
	var velocity_lerp := clampf(
		delta * (12.0 if move_input.length_squared() > 0.0 else 8.0), 0.0, 1.0)
	relative_velocity = relative_velocity.lerp(desired_velocity, velocity_lerp)
	relative_offset += relative_velocity * delta
	_apply_relative_transform(car_transform)
