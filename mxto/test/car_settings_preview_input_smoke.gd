extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	var car_settings: Control = main.car_settings
	if car_settings == null:
		push_error("car settings smoke could not find car settings")
		quit(1)
		return
	car_settings.call("open_settings")
	await process_frame
	var vehicle_selector: ItemList = car_settings.vehicle_selector
	if vehicle_selector.item_count <= 0:
		push_error("car settings smoke has no selectable cars")
		quit(1)
		return
	if vehicle_selector.item_count > 1:
		vehicle_selector.select(0)
		car_settings.call("_on_vehicle_selected", 0)
		var first_path: String = car_settings.player_settings.car_definition_path
		var item_rect := vehicle_selector.get_item_rect(1)
		var click_pos := vehicle_selector.global_position + item_rect.position + item_rect.size * 0.5
		car_settings.call("_try_select_vehicle_at_global_position", click_pos)
		await process_frame
		if car_settings.player_settings.car_definition_path == first_path:
			push_error("car settings vehicle selector did not receive click input point=%s selector_global=%s item_rect=%s mouse=%s" % [
				click_pos,
				vehicle_selector.get_global_rect(),
				item_rect,
				vehicle_selector.get_local_mouse_position(),
			])
			quit(1)
			return

	var start_yaw: float = car_settings.preview_yaw
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = Vector2(100.0, 100.0)
	car_settings.call("_handle_preview_mouse_button", press)
	var motion := InputEventMouseMotion.new()
	motion.position = Vector2(150.0, 110.0)
	car_settings.call("_handle_preview_mouse_motion", motion)
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = Vector2(150.0, 110.0)
	car_settings.call("_handle_preview_mouse_button", release)
	if is_equal_approx(start_yaw, car_settings.preview_yaw):
		push_error("car settings preview drag did not rotate the camera orbit")
		quit(1)
		return
	car_settings.preview_yaw = deg_to_rad(90.0)
	car_settings.preview_pitch = deg_to_rad(20.0)
	car_settings.call("_apply_preview_camera")
	var transform: Transform3D = car_settings.call("_preview_vehicle_transform")
	if transform.basis.x.distance_to(Vector3.RIGHT) > 0.001 or transform.basis.y.distance_to(Vector3.UP) > 0.001 or transform.basis.z.distance_to(Vector3.BACK) > 0.001:
		push_error("car settings preview orbit is rotating the car instead of the camera")
		quit(1)
		return
	var yaw_basis := Basis(Vector3.UP, car_settings.preview_yaw)
	var pitch_basis := Basis(yaw_basis.x.normalized(), car_settings.preview_pitch)
	var camera_offset: Vector3 = pitch_basis * (yaw_basis * Vector3(0.0, 3.5, car_settings.preview_distance))
	var expected_camera_pos: Vector3 = _pan_target(car_settings.preview_pan, camera_offset) + camera_offset
	if car_settings.preview_camera.position.distance_to(expected_camera_pos) > 0.001:
		push_error("car settings preview pitch is not moving the camera orbit")
		quit(1)
		return

	var start_pan: Vector3 = car_settings.preview_pan
	press = InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_RIGHT
	press.pressed = true
	press.position = Vector2(100.0, 100.0)
	car_settings.call("_handle_preview_mouse_button", press)
	motion = InputEventMouseMotion.new()
	motion.position = Vector2(130.0, 140.0)
	car_settings.call("_handle_preview_mouse_motion", motion)
	release = InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_RIGHT
	release.pressed = false
	release.position = Vector2(130.0, 140.0)
	car_settings.call("_handle_preview_mouse_button", release)
	if start_pan.is_equal_approx(car_settings.preview_pan):
		push_error("car settings preview drag did not pan the car")
		quit(1)
		return
	var panned_transform: Transform3D = car_settings.call("_preview_vehicle_transform")
	if panned_transform.origin.length() > 0.001:
		push_error("car settings preview pan moved the car instead of only the camera target")
		quit(1)
		return
	var yaw_after_pan: float = car_settings.preview_yaw
	var pitch_after_pan: float = car_settings.preview_pitch
	var yaw_basis_after_pan := Basis(Vector3.UP, yaw_after_pan)
	var pitch_basis_after_pan := Basis(yaw_basis_after_pan.x.normalized(), pitch_after_pan)
	var panned_camera_offset: Vector3 = pitch_basis_after_pan * (yaw_basis_after_pan * Vector3(0.0, 3.5, car_settings.preview_distance))
	var expected_panned_camera: Vector3 = _pan_target(car_settings.preview_pan, panned_camera_offset) + panned_camera_offset
	if car_settings.preview_camera.position.distance_to(expected_panned_camera) > 0.001:
		push_error("car settings preview pan did not move the camera orbit target")
		quit(1)
		return
	var panned_target: Vector3 = car_settings.preview_camera.position - panned_camera_offset
	var view_back := panned_camera_offset.normalized()
	if absf(panned_target.dot(view_back)) > 0.001:
		push_error("car settings preview pan target is not on the camera-facing plane through the car origin")
		quit(1)
		return
	car_settings.preview_pan = Vector3(99.0, -99.0, 12.0)
	car_settings.call("_apply_preview_camera")
	if car_settings.preview_pan.x > 4.001 or car_settings.preview_pan.y < -4.001 or absf(car_settings.preview_pan.z) > 0.001:
		push_error("car settings preview pan was not clamped to the camera plane limits")
		quit(1)
		return
	car_settings.preview_pan = Vector3(2.0, -1.5, 0.0)
	car_settings.preview_yaw += 1.2
	car_settings.preview_pitch = deg_to_rad(-18.0)
	car_settings.call("_apply_preview_camera")
	car_settings.preview_pan = Vector3.ZERO
	car_settings.call("_apply_preview_camera")
	var camera_back: Vector3 = car_settings.preview_camera.global_transform.basis.z.normalized()
	var expected_back: Vector3 = car_settings.preview_camera.global_position.normalized()
	if camera_back.distance_to(expected_back) > 0.001:
		push_error("zeroing preview pan did not recenter the camera on the world origin")
		quit(1)
		return
	car_settings.preview_pan = Vector3.ZERO
	car_settings.preview_yaw = deg_to_rad(25.0)
	car_settings.preview_pitch = 0.0
	car_settings.call("_apply_preview_camera")
	var render_manager: CarRenderManager = car_settings.preview_render_manager
	if render_manager == null or render_manager.archetypes.is_empty():
		push_error("car settings preview did not build render manager")
		quit(1)
		return
	var main_pass: Dictionary = render_manager.archetypes[0][CarRenderManager.PASS_MAIN]
	var multimesh: MultiMesh = main_pass["multimesh"]
	if multimesh.visible_instance_count != 1:
		push_error("car settings preview did not submit one visible body instance")
		quit(1)
		return
	if multimesh.get_instance_color(0) != Color.BLACK:
		push_error("car settings preview body instance colour should match in-game default overlay")
		quit(1)
		return
	if car_settings.settings_tab_container == null or car_settings.garage_panel == null:
		push_error("car settings did not build separate driver/garage tabs")
		quit(1)
		return
	if car_settings.stamp_layer_buttons.size() != 16:
		push_error("car settings did not build the 16 stamp layer buttons")
		quit(1)
		return
	var empty_layer := -1
	for layer in range(16):
		if car_settings.call("_stamp_for_layer", layer) == null:
			empty_layer = layer
			break
	if empty_layer >= 0:
		car_settings.call("_on_stamp_layer_pressed", empty_layer)
		await process_frame
		if car_settings.stamp_ui_mode != 1:
			push_error("empty stamp layer did not open the stamp chooser")
			quit(1)
			return
		car_settings.call("_on_stamp_choice_pressed", "circle")
		await process_frame
		await process_frame
		var edit_stamp: CarLiveryStamp = car_settings.call("_stamp_for_layer", empty_layer)
		if car_settings.stamp_ui_mode != 2 or edit_stamp == null:
			push_error("choosing a stamp did not enter edit mode")
			quit(1)
			return
		if edit_stamp.size.x <= 0.0 or edit_stamp.size.y <= 0.0:
			push_error("edit mode did not project a non-zero stamp size")
			quit(1)
			return
		car_settings.stamp_edit_rect_size = Vector2(2.0, 3.0)
		car_settings.call("_layout_stamp_edit_overlay")
		if car_settings.stamp_edit_square.size.x > 2.01 or car_settings.stamp_edit_square.size.y > 3.01:
			push_error("stamp edit box has an unwanted minimum size")
			quit(1)
			return
		car_settings.stamp_edit_roll = 0.35
		car_settings.call("_layout_stamp_edit_overlay")
		car_settings.call("_apply_edit_stamp_from_camera")
		if absf(edit_stamp.rotation + 0.35) > 0.001:
			push_error("stamp projection rotation does not match the edit box direction")
			quit(1)
			return
		car_settings.preview_has_camera_override = true
		car_settings.call("_focus_preview_on_stamp", edit_stamp)
		if car_settings.preview_has_camera_override:
			push_error("editing an existing stamp left the preview camera in temporary override mode")
			quit(1)
			return
		var edit_yaw: float = car_settings.preview_yaw
		press = InputEventMouseButton.new()
		press.button_index = MOUSE_BUTTON_LEFT
		press.pressed = true
		press.position = Vector2(10.0, 10.0)
		car_settings.call("_on_stamp_edit_overlay_gui_input", press)
		motion = InputEventMouseMotion.new()
		motion.position = Vector2(50.0, 16.0)
		car_settings.call("_on_stamp_edit_overlay_gui_input", motion)
		release = InputEventMouseButton.new()
		release.button_index = MOUSE_BUTTON_LEFT
		release.pressed = false
		release.position = car_settings.stamp_edit_square.position + car_settings.stamp_edit_square.size * 0.5
		car_settings.call("_on_stamp_edit_overlay_gui_input", release)
		if car_settings.stamp_ui_mode != 2 or is_equal_approx(edit_yaw, car_settings.preview_yaw):
			push_error("stamp edit overlay did not allow camera orbit outside the edit square")
			quit(1)
			return
		var released_yaw: float = car_settings.preview_yaw
		motion = InputEventMouseMotion.new()
		motion.position = Vector2(80.0, 22.0)
		car_settings.call("_on_stamp_edit_overlay_gui_input", motion)
		if !is_equal_approx(released_yaw, car_settings.preview_yaw):
			push_error("stamp edit overlay did not stop camera orbit when released over the edit square")
			quit(1)
			return
		car_settings.call("_on_stamp_edit_cancel_pressed")
		await process_frame
		if car_settings.call("_stamp_for_layer", empty_layer) != null:
			push_error("cancelling a newly added stamp did not clear the layer")
			quit(1)
			return

	print("MXT_CAR_SETTINGS_PREVIEW_INPUT_SMOKE cars=", vehicle_selector.item_count)
	quit(0)

func _push_mouse_click(position: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = position
	root.get_viewport().push_input(motion)
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = position
	root.get_viewport().push_input(press)
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = position
	root.get_viewport().push_input(release)

func _pan_target(pan: Vector3, camera_offset: Vector3) -> Vector3:
	var view_back := camera_offset.normalized()
	var right := Vector3.UP.cross(view_back)
	if right.length_squared() <= 0.0001:
		right = Vector3.RIGHT
	else:
		right = right.normalized()
	var up := view_back.cross(right).normalized()
	return right * pan.x + up * pan.y
