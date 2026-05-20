extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	if main.car_definitions.is_empty():
		push_error("lobby_chibi_render_smoke has no car definitions")
		quit(1)
		return
	var def: CarDefinition = main.car_definitions[0]
	main.lobby_control.visible = true
	main.network_manager.player_ids = [42]
	main.network_manager.player_settings[42] = {
		"username": "Smoke",
		"car_definition_path": def.resource_path,
		"accel_setting": 1.0,
	}
	main.network_manager.lobby_latency_rtt_s[42] = 0.042
	main.call("_update_lobby_chibi_cars", 1.0 / 60.0)
	var chibi = main.lobby_chibi_cars.get(42, null)
	if chibi != null:
		chibi.call("_update_nameplate")
	if chibi == null or chibi.ping_label == null or chibi.ping_label.text != "42ms":
		push_error("lobby chibi latency label mismatch")
		quit(1)
		return
	if !main.car_settings_button_lobby.visible or !main.controller_settings_button_lobby.visible:
		push_error("lobby settings buttons should be visible")
		quit(1)
		return
	var render_manager: CarRenderManager = main.lobby_chibi_render_manager
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
	print("MXT_LOBBY_CHIBI_RENDER_SMOKE visible=", multimesh.visible_instance_count)
	quit(0)
