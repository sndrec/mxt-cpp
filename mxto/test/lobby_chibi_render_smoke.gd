extends SceneTree

const MAIN_SCENE := "res://main.tscn"
const LobbyChibiControllerClass = preload("res://ui/lobby_chibi_controller.gd")

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	if main.vehicle_content_controller.definitions.is_empty():
		push_error("lobby_chibi_render_smoke has no car definitions")
		quit(1)
		return
	var def: CarDefinition = main.vehicle_content_controller.definitions[0]
	main.lobby_control.visible = true
	main.network_manager.player_ids = [42, 43]
	main.network_manager.player_settings[42] = {
		"username": "Smoke",
		"vehicle_content_id": def.content_id,
		"accel_setting": 1.0,
	}
	main.network_manager.player_settings[43] = {
		"username": "Smoke Two",
		"vehicle_content_id": def.content_id,
		"accel_setting": 1.0,
	}
	main.network_manager.lobby_latency_rtt_s[42] = 0.042
	var controller: LobbyChibiControllerClass = main.lobby_chibi_controller
	if main.has_method("_update_lobby_chibi_cars") or main.has_method("_submit_lobby_chibi_state"):
		push_error("GameManager should not retain lobby chibi implementation")
		quit(1)
		return
	controller.process_lobby(1.0 / 60.0)
	controller.set_hover(-1)
	var chibi = controller.cars.get(42, null)
	if chibi != null:
		chibi.call("_update_nameplate")
	if chibi == null or chibi.nameplate.visible:
		push_error("lobby chibi nameplate should be hidden without hover")
		quit(1)
		return
	controller.set_hover(42)
	if !chibi.nameplate.visible:
		push_error("lobby chibi nameplate should be visible while hovered")
		quit(1)
		return
	if chibi.ping_label == null or chibi.ping_label.text != "42ms":
		push_error("lobby chibi latency label mismatch")
		quit(1)
		return
	if !main.car_settings_button_lobby.visible or !main.controller_settings_button_lobby.visible:
		push_error("lobby settings buttons should be visible")
		quit(1)
		return
	var render_manager: CarRenderManager = controller.render_manager
	if render_manager == null or render_manager.archetypes.is_empty():
		push_error("lobby_chibi_render_smoke did not build render archetypes")
		quit(1)
		return
	var archetype: Dictionary = render_manager.archetypes[0]
	var main_pass: Dictionary = archetype[CarRenderManager.PASS_MAIN]
	var multimesh: MultiMesh = main_pass["multimesh"]
	if multimesh.visible_instance_count <= 0:
		push_error("lobby_chibi_render_smoke submitted no visible car bodies")
		quit(1)
		return
	controller.set_hover(43)
	var second_chibi = controller.cars.get(43, null)
	if chibi.nameplate.visible or second_chibi == null or !second_chibi.nameplate.visible:
		push_error("lobby chibi hover should expose exactly the selected nameplate")
		quit(1)
		return
	if !controller.magnifier.visible or controller.magnifier_viewport.render_target_update_mode == SubViewport.UPDATE_DISABLED:
		push_error("lobby chibi magnifier should render while a car is hovered")
		quit(1)
		return
	var car_viewport_anchor: Vector2 = second_chibi.get_hover_anchor()
	var car_stack_anchor := Vector2(
		car_viewport_anchor.x * controller.viewport_stack.size.x / float(controller.viewport.size.x),
		car_viewport_anchor.y * controller.viewport_stack.size.y / float(controller.viewport.size.y))
	var car_canvas: Vector2 = controller.viewport_stack.get_global_transform_with_canvas() * car_stack_anchor
	var magnifier_center: Vector2 = controller.magnifier.get_global_transform_with_canvas() * controller.magnifier.pivot_offset
	if !magnifier_center.is_equal_approx(car_canvas):
		push_error("lobby chibi zoom circle should be centered on the selected vehicle")
		quit(1)
		return
	if controller.magnifier.get_parent() == controller.viewport_stack:
		push_error("lobby chibi zoom circle should not be clipped by the chibi viewport stack")
		quit(1)
		return
	if controller.nameplates.z_index <= controller.magnifier.z_index:
		push_error("lobby chibi nameplates should draw above the zoom circle")
		quit(1)
		return
	var circle_bottom: Vector2 = controller.magnifier.get_global_transform_with_canvas() * Vector2(
		controller.magnifier.pivot_offset.x,
		controller.magnifier.size.y)
	var nameplate_bottom_center: Vector2 = second_chibi.nameplate_panel.get_global_transform_with_canvas() * Vector2(
		second_chibi.nameplate_panel.size.x * 0.5,
		second_chibi.nameplate_panel.size.y)
	if !nameplate_bottom_center.is_equal_approx(circle_bottom):
		push_error("lobby chibi nameplate should be centered on the bottom of the zoom circle")
		quit(1)
		return
	for nameplate_child in second_chibi.nameplate.get_children():
		if nameplate_child is Line2D:
			push_error("lobby chibi nameplate should not have a car pointer line")
			quit(1)
			return
	var long_name_settings: Dictionary = second_chibi.player_settings.duplicate(true)
	long_name_settings["username"] = "A Very Very Very Long Smoke Test Name"
	second_chibi.update_settings(long_name_settings, second_chibi.get_render_definition(), {})
	second_chibi.call("_update_nameplate")
	var long_nameplate_width: float = second_chibi.nameplate_panel.size.x
	var short_name_settings: Dictionary = long_name_settings.duplicate(true)
	short_name_settings["username"] = "I"
	second_chibi.update_settings(short_name_settings, second_chibi.get_render_definition(), {})
	second_chibi.call("_update_nameplate")
	if second_chibi.nameplate_panel.size.x >= long_nameplate_width:
		push_error("lobby chibi nameplate should shrink after the username gets shorter")
		quit(1)
		return
	var magnifier_floor_position: Vector3 = controller.magnifier_floor.position
	var magnifier_position_before_move: Vector2 = controller.magnifier.position
	second_chibi.position.x += 3.0
	controller.set_hover(43)
	if controller.magnifier_floor.position != magnifier_floor_position:
		push_error("lobby chibi magnifier floor should remain fixed as the vehicle moves")
		quit(1)
		return
	if controller.magnifier.position.is_equal_approx(magnifier_position_before_move):
		push_error("lobby chibi zoom circle should follow the selected vehicle")
		quit(1)
		return
	second_chibi.position.x = 10000.0
	controller.set_hover(43)
	var magnifier_parent := controller.magnifier.get_parent() as Control
	var magnifier_max_position: Vector2 = magnifier_parent.size - controller.magnifier.size
	if (
		controller.magnifier.position.x < 0.0
		or controller.magnifier.position.y < 0.0
		or controller.magnifier.position.x > maxf(0.0, magnifier_max_position.x)
		or controller.magnifier.position.y > maxf(0.0, magnifier_max_position.y)
	):
		push_error("lobby chibi zoom circle should stay inside the game window")
		quit(1)
		return
	var magnifier_manager: CarRenderManager = controller.magnifier_render_manager
	var magnifier_visible_bodies := 0
	for magnifier_archetype in magnifier_manager.archetypes:
		var magnifier_main_pass: Dictionary = magnifier_archetype[CarRenderManager.PASS_MAIN]
		var magnifier_multimesh: MultiMesh = magnifier_main_pass["multimesh"]
		magnifier_visible_bodies += magnifier_multimesh.visible_instance_count
	if magnifier_visible_bodies != 1:
		push_error("lobby chibi magnifier should submit exactly one car body")
		quit(1)
		return
	controller.set_hover(-1)
	if controller.magnifier.visible or controller.magnifier_viewport.render_target_update_mode != SubViewport.UPDATE_DISABLED:
		push_error("lobby chibi magnifier should stop rendering after hover")
		quit(1)
		return
	main.lobby_control.visible = false
	controller.process_lobby(1.0 / 60.0)
	if !controller.cars.is_empty() or !render_manager.archetypes.is_empty() or !magnifier_manager.archetypes.is_empty():
		push_error("lobby chibi renderers should clear when leaving the lobby")
		quit(1)
		return
	if controller.viewport.render_target_update_mode != SubViewport.UPDATE_DISABLED:
		push_error("lobby chibi viewport should stop rendering when leaving the lobby")
		quit(1)
		return
	print("MXT_LOBBY_CHIBI_RENDER_SMOKE visible=", multimesh.visible_instance_count)
	quit(0)
