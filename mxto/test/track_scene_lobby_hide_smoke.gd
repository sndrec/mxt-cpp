extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	if main.vehicle_content_controller.definitions.is_empty():
		push_error("track scene lobby hide smoke has no car definitions")
		quit(1)
		return
	var track_index := -1
	for i in range(main.track_content_controller.tracks.size()):
		if String(main.track_content_controller.tracks[i].get("name", "")) == "Mute City - Serial Gaps":
			track_index = i
			break
	if track_index < 0:
		push_error("track scene lobby hide smoke could not find Serial Gaps")
		quit(1)
		return
	var player_id := 1
	var def: CarDefinition = main.vehicle_content_controller.definitions[0]
	var settings := {
		"username": "Smoke",
		"vehicle_content_id": def.content_id,
		"accel_setting": 1.0,
	}
	settings.merge(main.vehicle_content_controller.get_evidence(def.content_id), true)
	main.singleplayer_mode = false
	main.network_manager.is_server = true
	main.network_manager.listen_server = true
	main.network_manager.player_ids = [player_id]
	main.network_manager.spectator_ids = []
	main.network_manager.lobby_settings.player_settings[player_id] = settings
	main.get_node("Control").visible = true
	main.lobby_control.visible = true
	if main.has_method("_start_race"):
		push_error("GameManager should not retain race-session setup")
		quit(1)
		return
	if !main.race_session_controller.start_race(track_index, [settings], false, false):
		push_error("race-session owner rejected valid smoke setup")
		quit(1)
		return
	if main.get_node("Control").visible:
		push_error("main menu should be hidden after race start")
		quit(1)
		return
	if main.lobby_control.visible:
		push_error("multiplayer lobby should be hidden after track-scene race start")
		quit(1)
		return
	main.call("_return_to_lobby")
	if !main.lobby_control.visible or !main.race_session_controller.players.is_empty():
		push_error("race-session teardown did not restore the lobby and clear players")
		quit(1)
		return
	print("MXT_TRACK_SCENE_LOBBY_HIDE_SMOKE track_index=", track_index)
	quit(0)
