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
		push_error("car settings preview drag did not rotate the car")
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

	print("MXT_CAR_SETTINGS_PREVIEW_INPUT_SMOKE cars=", vehicle_selector.item_count)
	quit(0)
