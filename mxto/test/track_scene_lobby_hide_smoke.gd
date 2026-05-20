extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	if main.car_definitions.is_empty():
		push_error("track scene lobby hide smoke has no car definitions")
		quit(1)
		return
	var track_index := -1
	for i in range(main.tracks.size()):
		if String(main.tracks[i].get("name", "")) == "Mute City - Serial Gaps":
			track_index = i
			break
	if track_index < 0:
		push_error("track scene lobby hide smoke could not find Serial Gaps")
		quit(1)
		return
	var player_id := 1
	var def: CarDefinition = main.car_definitions[0]
	var settings := {
		"username": "Smoke",
		"car_definition_path": def.resource_path,
		"accel_setting": 1.0,
	}
	main.singleplayer_mode = false
	main.network_manager.is_server = true
	main.network_manager.listen_server = true
	main.network_manager.player_ids = [player_id]
	main.network_manager.spectator_ids = []
	main.network_manager.player_settings[player_id] = settings
	main.get_node("Control").visible = true
	main.lobby_control.visible = true
	main.call("_start_race", track_index, [settings])
	if main.get_node("Control").visible:
		push_error("main menu should be hidden after race start")
		quit(1)
		return
	if main.lobby_control.visible:
		push_error("multiplayer lobby should be hidden after track-scene race start")
		quit(1)
		return
	print("MXT_TRACK_SCENE_LOBBY_HIDE_SMOKE track_index=", track_index)
	quit(0)
