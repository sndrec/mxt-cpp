extends SceneTree

const PLAYER_COUNT := 48
const STEADY_FRAMES := 180
const STAMPS_PER_LIVERY := 16

func _fail(message: String) -> void:
	push_error("MXT_LOBBY_SCALE_SMOKE_FAIL " + message)
	quit(1)

func _synthetic_livery(vehicle_content_id: String, player_index: int) -> Dictionary:
	var stamps: Array = []
	var stamp_ids := ["circle", "star", "cross"]
	for layer in range(STAMPS_PER_LIVERY):
		var x := float((layer % 4) - 2) * 0.35
		var z := float((layer / 4) - 2) * 0.35
		stamps.append({
			"stamp_id": stamp_ids[layer % stamp_ids.size()],
			"source": "base",
			"hash": "",
			"palette_id": 0,
			"rect": [0.0, 0.0, 0.0, 0.0],
			"rect_rotated": false,
			"enabled": true,
			"layer": layer,
			"local_origin": [x, 0.0, z],
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
		"primary_colour": Color.from_hsv(float(player_index) / float(PLAYER_COUNT), 0.75, 0.9).to_html(true),
		"secondary_colour": Color.from_hsv(float(player_index + 11) / float(PLAYER_COUNT), 0.55, 1.0).to_html(true),
		"accent_colour": Color.from_hsv(float(player_index + 23) / float(PLAYER_COUNT), 0.85, 0.45).to_html(true),
		"stamps": stamps,
	}

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	await process_frame
	await process_frame
	if game_manager.vehicle_content_controller.definitions.is_empty():
		_fail("no car definitions were loaded")
		return

	var roster: Array = []
	for i in range(PLAYER_COUNT):
		var player_id := 1000 + i
		var definition: CarDefinition = game_manager.vehicle_content_controller.definitions[i % game_manager.vehicle_content_controller.definitions.size()]
		roster.append(player_id)
		var settings := {
			"username": "Load Player %02d" % i,
			"vehicle_content_id": definition.content_id,
			"car_livery": _synthetic_livery(definition.content_id, i),
			"accel_setting": 1.0,
		}
		settings.merge(game_manager.vehicle_content_controller.get_evidence(definition.content_id), true)
		game_manager.network_manager.lobby_settings.player_settings[player_id] = settings
	game_manager.network_manager.player_ids = roster
	game_manager.lobby_control.visible = true

	var initial_start := Time.get_ticks_usec()
	game_manager.lobby_chibi_controller.process_lobby(1.0 / 60.0)
	var initial_usec := Time.get_ticks_usec() - initial_start
	if game_manager.lobby_chibi_controller.cars.size() != PLAYER_COUNT:
		_fail("did not create all synthetic lobby cars")
		return

	var steady_start := Time.get_ticks_usec()
	for _frame in range(STEADY_FRAMES):
		game_manager.lobby_chibi_controller.process_lobby(1.0 / 60.0)
	var steady_usec := Time.get_ticks_usec() - steady_start
	var steady_avg_usec := float(steady_usec) / float(STEADY_FRAMES)
	var changed_player_id := int(roster[0])
	var changed_settings: Dictionary = (game_manager.network_manager.lobby_settings.player_settings[changed_player_id] as Dictionary).duplicate(true)
	var replacement_definition: CarDefinition = game_manager.vehicle_content_controller.definitions[1]
	changed_settings["vehicle_content_id"] = replacement_definition.content_id
	changed_settings["car_livery"] = _synthetic_livery(replacement_definition.content_id, 0)
	changed_settings.merge(game_manager.vehicle_content_controller.get_evidence(replacement_definition.content_id), true)
	game_manager.network_manager.lobby_settings._store_player_settings(changed_player_id, changed_settings)
	var change_schedule_start := Time.get_ticks_usec()
	game_manager.lobby_chibi_controller.process_lobby(1.0 / 60.0)
	var change_schedule_usec := Time.get_ticks_usec() - change_schedule_start
	if game_manager.lobby_chibi_controller.last_settings_apply_definition_count != 1 \
			or game_manager.lobby_chibi_controller.last_settings_apply_sample_count != 1 \
			or game_manager.lobby_chibi_controller.last_settings_apply_already_current_count != PLAYER_COUNT - 1:
		_fail("one-player settings update performed redundant definition or stat work")
		return
	game_manager.lobby_chibi_controller.render_rebuild_due_msec = 0
	var change_rebuild_start := Time.get_ticks_usec()
	game_manager.lobby_chibi_controller.process_lobby(1.0 / 60.0)
	var change_rebuild_usec := Time.get_ticks_usec() - change_rebuild_start
	var sample_state := [1000, 120.0, Vector3(1.0, 0.0, -1.0), 3.0, Vector3(4.0, 0.05, -2.0), Vector3(0.0, 1.5, 0.1)]
	var variant_state_bytes := var_to_bytes(sample_state).size()
	print(
		"MXT_LOBBY_SCALE_SMOKE_PASS players=", PLAYER_COUNT,
		" stamps_per_livery=", STAMPS_PER_LIVERY,
		" initial_ms=", snappedf(float(initial_usec) * 0.001, 0.001),
		" steady_avg_us=", snappedf(steady_avg_usec, 0.1),
		" one_player_change_schedule_ms=", snappedf(float(change_schedule_usec) * 0.001, 0.001),
		" one_player_change_rebuild_ms=", snappedf(float(change_rebuild_usec) * 0.001, 0.001),
		" sampled_players=", game_manager.lobby_chibi_controller.last_settings_apply_sample_count,
		" unchanged_players=", game_manager.lobby_chibi_controller.last_settings_apply_already_current_count,
		" archetypes=", game_manager.lobby_chibi_controller.render_manager.archetypes.size(),
		" variant_state_bytes=", variant_state_bytes,
		" draw_calls=", int(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)))
	game_manager.queue_free()
	await process_frame
	quit(0)
