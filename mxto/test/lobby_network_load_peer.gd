extends SceneTree

const DEFAULT_PORT := 27916
const DEFAULT_DURATION_SEC := 45
const DEFAULT_TARGET_PLAYERS := 41
const STAMPS_PER_LIVERY := 16

var game_manager: GameManager
var role := "client"
var player_index := 0
var port := DEFAULT_PORT
var duration_sec := DEFAULT_DURATION_SEC
var target_players := DEFAULT_TARGET_PLAYERS
var lightweight_client := false

func _init() -> void:
	call_deferred("_run")

func _arg_value(name: String, fallback: String) -> String:
	var args := OS.get_cmdline_args()
	args.append_array(OS.get_cmdline_user_args())
	var prefix := name + "="
	for i in range(args.size()):
		var value := str(args[i])
		if value.begins_with(prefix):
			return value.trim_prefix(prefix)
		if value == name and i + 1 < args.size():
			return str(args[i + 1])
	return fallback

func _synthetic_livery(vehicle_content_id: String) -> Dictionary:
	var stamps: Array = []
	var stamp_ids := ["circle", "star", "cross"]
	for layer in range(STAMPS_PER_LIVERY):
		stamps.append({
			"stamp_id": stamp_ids[layer % stamp_ids.size()],
			"source": "base",
			"hash": "",
			"palette_id": 0,
			"rect": [0.0, 0.0, 0.0, 0.0],
			"rect_rotated": false,
			"enabled": true,
			"layer": layer,
			"local_origin": [float((layer % 4) - 2) * 0.35, 0.0, float((layer / 4) - 2) * 0.35],
			"local_basis": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
			"rotation": float(layer) * 0.17,
			"size": [0.3 + float(layer % 3) * 0.05, 0.3],
			"flip_horizontal": false,
			"flip_vertical": false,
			"mirror_local_x": false,
			"projection_depth": 0.25,
			"colour": Color.from_hsv(float(player_index * 13 + layer * 7) / 360.0, 0.8, 1.0).to_html(true),
			"opacity": 1.0,
		})
	return {
		"version": 1,
		"vehicle_content_id": vehicle_content_id,
		"primary_colour": Color.from_hsv(float(player_index % target_players) / float(target_players), 0.75, 0.9).to_html(true),
		"secondary_colour": Color.from_hsv(float((player_index + 11) % target_players) / float(target_players), 0.55, 1.0).to_html(true),
		"accent_colour": Color.from_hsv(float((player_index + 23) % target_players) / float(target_players), 0.85, 0.45).to_html(true),
		"stamps": stamps,
	}

func _settings() -> Dictionary:
	var vehicle_content_id := ""
	if !game_manager.vehicle_content_controller.definitions.is_empty():
		vehicle_content_id = game_manager.vehicle_content_controller.definitions[player_index % game_manager.vehicle_content_controller.definitions.size()].content_id
	var settings := {
		"username": "Load Player %02d" % player_index,
		"vehicle_content_id": vehicle_content_id,
		"car_livery": _synthetic_livery(vehicle_content_id),
		"accel_setting": 1.0,
		"spectator": false,
	}
	settings.merge(game_manager.vehicle_content_controller.get_evidence(vehicle_content_id), true)
	return settings

func _wait_for_client_connection(timeout_msec: int) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_msec
	while Time.get_ticks_msec() < deadline:
		var peer := game_manager.network_manager.multiplayer.multiplayer_peer
		if peer != null and peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
			return true
		await create_timer(0.05).timeout
	return false

func _on_lightweight_race_started(_track_id: String, _settings: Array) -> void:
	var manager := game_manager.network_manager
	manager.game_sim = game_manager.game_sim
	manager.race_admission.report(manager.race_admission.LOADING, "load-test client loading")
	await process_frame
	manager.race_admission.report(manager.race_admission.READY, "load-test client ready")

func _run() -> void:
	role = _arg_value("--lobby-load-role", "client")
	player_index = int(_arg_value("--lobby-load-index", "0"))
	port = int(_arg_value("--lobby-load-port", str(DEFAULT_PORT)))
	duration_sec = int(_arg_value("--lobby-load-duration", str(DEFAULT_DURATION_SEC)))
	target_players = int(_arg_value("--lobby-load-target", str(DEFAULT_TARGET_PLAYERS)))
	lightweight_client = _arg_value("--lobby-load-lightweight-client", "false").to_lower() == "true"

	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		push_error("MXT_LOBBY_NETWORK_LOAD_FAIL could not load main scene")
		quit(1)
		return
	game_manager = packed.instantiate() as GameManager
	root.add_child(game_manager)
	await process_frame
	await process_frame
	game_manager.lobby_control.visible = true
	game_manager.get_node("Control").visible = false

	var manager := game_manager.network_manager
	if role != "host" and lightweight_client:
		if manager.race_started.is_connected(game_manager._on_network_race_started):
			manager.race_started.disconnect(game_manager._on_network_race_started)
		manager.race_started.connect(_on_lightweight_race_started)
	var err := OK
	if role == "host":
		err = manager.host(port, maxi(target_players + 4, 64), false)
	else:
		err = manager.join("127.0.0.1", port)
	if err != OK:
		push_error("MXT_LOBBY_NETWORK_LOAD_FAIL role=%s network_error=%d" % [role, err])
		quit(1)
		return
	if role != "host" and !await _wait_for_client_connection(10000):
		push_error("MXT_LOBBY_NETWORK_LOAD_FAIL role=client index=%d connect_timeout" % player_index)
		quit(1)
		return
	manager.lobby_settings.send_player_settings(_settings())
	manager.custom_stamp_network.send_active_custom_stamp_manifest()

	var deadline := Time.get_ticks_msec() + duration_sec * 1000
	var full_roster_msec := 0
	var race_requested := false
	var max_players := 0
	var max_settings := 0
	while Time.get_ticks_msec() < deadline:
		max_players = maxi(max_players, manager.player_ids.size())
		max_settings = maxi(max_settings, manager.lobby_settings.player_settings.size())
		if role == "host" and !race_requested:
			if manager.player_ids.size() >= target_players and manager.lobby_settings.player_settings.size() >= target_players:
				if full_roster_msec == 0:
					full_roster_msec = Time.get_ticks_msec()
				elif Time.get_ticks_msec() - full_roster_msec >= 2000:
					race_requested = true
					print("MXT_LOBBY_NETWORK_LOAD_START tracks=%d sequence=%s is_server=%s" % [
						game_manager.track_content_controller.tracks.size(),
						str(game_manager.lobby_controller.grand_prix_track_sequence),
						str(manager.is_server),
					])
					game_manager.lobby_controller.request_start_race()
		if role == "host" and race_requested and manager.race_admission.scheduled:
			break
		await create_timer(0.1).timeout

	if role == "host":
		var admission := manager.race_admission.log_fields()
		var ready := int(admission.get("ready", 0))
		var roster := int(admission.get("roster", 0))
		var passed := max_players >= target_players and max_settings >= target_players and race_requested and ready >= target_players and roster >= target_players and int(admission.get("blocked", 0)) == 0
		var marker := "PASS" if passed else "FAIL"
		print("MXT_LOBBY_NETWORK_LOAD_%s players=%d settings=%d race_requested=%s admission_ready=%d admission_roster=%d admission_blocked=%d" % [
			marker,
			max_players,
			max_settings,
			str(race_requested),
			ready,
			roster,
			int(admission.get("blocked", 0)),
		])
		manager.disconnect_from_server()
		quit(0 if passed else 1)
	else:
		manager.disconnect_from_server()
		quit(0)
