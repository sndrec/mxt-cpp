extends SceneTree

const MAIN_SCENE := "res://main.tscn"

func _init() -> void:
	var main: Node = load(MAIN_SCENE).instantiate()
	root.add_child(main)
	await process_frame
	await process_frame

	main.lobby_control.visible = true
	main.network_manager.is_server = true
	main.network_manager.race_active = false
	main.lobby_controller.grand_prix_track_sequence.clear()
	main.lobby_controller.refresh_race_setup()
	await process_frame
	main.call("_physics_process", 1.0 / 60.0)

	if main.network_manager.race_track_evidence.count() != 0:
		push_error("empty lobby sequence should not fall back to selected track")
		quit(1)
		return
	if main.lobby_controller.stage_preview_container == null or !(main.lobby_controller.stage_preview_container is VBoxContainer):
		push_error("lobby stage preview container should be a VBoxContainer")
		quit(1)
		return
	if main.lobby_controller.stage_preview_container.get_child_count() != 0:
		push_error("empty lobby sequence should render no preview rows")
		quit(1)
		return
	if main.lobby_controller.start_race_button == null or !main.lobby_controller.start_race_button.disabled:
		push_error("Play button should be disabled with no queued races")
		quit(1)
		return

	print("MXT_LOBBY_EMPTY_TRACK_SEQUENCE_SMOKE ok")
	quit(0)
