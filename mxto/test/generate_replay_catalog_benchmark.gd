extends SceneTree

const SUITE_ID := "replay_catalog_twist_road_v1"
const TRACK_CONTENT_ID := "mxt:track:official:twist-road"
const DEFAULT_RACER_COUNTS := [6, 15, 30, 100]
const DEFAULT_SAMPLE_COUNT := 10
const DEFAULT_MAX_TICKS := 18_000
const FINISH_CHECK_INTERVAL := 30
const PROGRESS_INTERVAL := 600

var game_manager: GameManager


func _init() -> void:
	call_deferred("_run")


func _fail(message: String) -> void:
	push_error("MXT_REPLAY_BENCHMARK_FAIL " + message)
	quit(1)


func _arg_value(args: Array, name: String, fallback: String) -> String:
	var index := args.find(name)
	if index < 0 or index + 1 >= args.size():
		return fallback
	return String(args[index + 1])


func _racer_counts(args: Array) -> Array:
	var text := _arg_value(args, "--racer-counts", "")
	if text.is_empty():
		return DEFAULT_RACER_COUNTS.duplicate()
	var result: Array = []
	for part in text.split(",", false):
		var value := int(part.strip_edges())
		if value > 0 and !result.has(value):
			result.append(value)
	return result


func _existing_suite_samples(replay_catalog_controller: ReplayCatalogController, replay_playback_session: ReplayPlaybackSession) -> Dictionary:
	var existing := {}
	var replay_dir: String = replay_catalog_controller._replay_dir()
	if DirAccess.make_dir_recursive_absolute(replay_dir) != OK:
		return existing
	var directory := DirAccess.open(replay_dir)
	if directory == null:
		return existing
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while !file_name.is_empty():
		if !directory.current_is_dir() and file_name.ends_with(".mxt_replay"):
			var path := replay_dir.path_join(file_name)
			var metadata: Dictionary = replay_playback_session._load_replay_metadata_file(path)
			if String(metadata.get("benchmark_suite", "")) == SUITE_ID:
				var key := "%d:%d" % [
					int(metadata.get("benchmark_racer_count", 0)),
					int(metadata.get("benchmark_sample", 0)),
				]
				existing[key] = path
		file_name = directory.get_next()
	directory.list_dir_end()
	return existing


func _race_configuration(racer_count: int) -> MxtRaceConfiguration:
	var configuration := game_manager._build_default_singleplayer_race_configuration()
	configuration.leaderboard_ineligible_reason = "benchmark"
	configuration.cpu_count = racer_count
	configuration.lap_count = 3
	configuration.vehicle_restore = true
	configuration.bumpers = false
	configuration.s_boost = true
	return configuration


func _start_cpu_race(track_index: int, racer_count: int, spawn_seed: int) -> bool:
	game_manager.singleplayer_mode = true
	game_manager._singleplayer_tick = 0
	game_manager.network_manager.reset_race_state()
	game_manager.network_manager.player_ids = []
	game_manager.network_manager.spectator_ids = []
	game_manager.network_manager.lobby_settings.set_cpu_driver_count(racer_count)
	var cpu_ids: Array = game_manager.network_manager.lobby_settings.cpu_player_ids.duplicate(true)
	game_manager.network_manager.lobby_settings.set_race_cpu_roster(cpu_ids)
	var configuration := _race_configuration(racer_count)
	var options := game_manager._build_default_singleplayer_race_state()
	var track_evidence := game_manager.track_content_controller.build_track_content_evidence([track_index])
	options["race_human_ids"] = []
	options["race_cpu_ids"] = cpu_ids.duplicate(true)
	options["race_spectator_ids"] = []
	game_manager.network_manager.race_configuration = configuration
	game_manager.network_manager.race_track_evidence = track_evidence
	game_manager.network_manager.race_state = options
	game_manager.network_manager.set_spawn_seed(spawn_seed)
	var roster: MxtRaceRoster = game_manager.network_manager.lobby_settings.build_race_roster(cpu_ids)
	return roster != null and game_manager.race_session_controller.start_race(track_index, roster, true, true)


func _all_racers_finished(racer_count: int) -> bool:
	return game_manager.game_sim.get_finished_player_ids().size() >= racer_count


func _run_race(track_index: int, racer_count: int, sample_index: int, max_ticks: int) -> Dictionary:
	var spawn_seed := 0x4D585400 + racer_count * 100 + sample_index
	var race_start_usec := Time.get_ticks_usec()
	if !_start_cpu_race(track_index, racer_count, spawn_seed):
		return {"success": false, "error": "race startup failed"}
	var neutral_input := PackedByteArray([0])
	var ticks_run := 0
	while ticks_run < max_ticks and !_all_racers_finished(racer_count):
		game_manager._simulate_singleplayer_tick(neutral_input)
		ticks_run += 1
		if ticks_run % FINISH_CHECK_INTERVAL == 0:
			game_manager._check_race_finished()
		if ticks_run % PROGRESS_INTERVAL == 0:
			print(
				"MXT_REPLAY_BENCHMARK_PROGRESS racers=", racer_count,
				" sample=", sample_index,
				" tick=", ticks_run,
				" finished=", game_manager.game_sim.get_finished_player_ids().size())
	game_manager._check_race_finished()
	var finished_count := game_manager.game_sim.get_finished_player_ids().size()
	if finished_count < racer_count:
		game_manager.replay_recorder.finish()
		return {
			"success": false,
			"error": "race hit the tick cap",
			"ticks": ticks_run,
			"finished": finished_count,
		}
	var metadata := game_manager.replay_recorder.metadata
	metadata.name = "Replay Catalog Benchmark - %03d racers - %02d" % [racer_count, sample_index]
	metadata.benchmark_suite = SUITE_ID
	metadata.benchmark_racer_count = racer_count
	metadata.benchmark_sample = sample_index
	metadata.benchmark_spawn_seed = spawn_seed
	metadata.benchmark_generation_ticks = ticks_run
	metadata.benchmark_generation_usec = Time.get_ticks_usec() - race_start_usec
	var save_start_usec := Time.get_ticks_usec()
	var replay_path: String = game_manager.replay_recorder.save_locally()
	var save_usec := Time.get_ticks_usec() - save_start_usec
	if replay_path.is_empty():
		return {"success": false, "error": "replay save failed", "ticks": ticks_run}
	return {
		"success": true,
		"path": replay_path,
		"ticks": ticks_run,
		"simulation_usec": save_start_usec - race_start_usec,
		"save_usec": save_usec,
		"bytes": FileAccess.get_file_as_bytes(replay_path).size(),
	}


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var racer_counts := _racer_counts(args)
	var sample_count := int(_arg_value(args, "--samples", str(DEFAULT_SAMPLE_COUNT)))
	var max_ticks := int(_arg_value(args, "--max-ticks", str(DEFAULT_MAX_TICKS)))
	if racer_counts.is_empty() or sample_count <= 0 or max_ticks <= 0:
		_fail("racer counts, sample count, and max ticks must be positive")
		return
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	game_manager = packed.instantiate() as GameManager
	root.add_child(game_manager)
	await process_frame
	await process_frame
	game_manager.set_process(false)
	game_manager.set_physics_process(false)
	var track_index := game_manager.track_content_controller.track_index_for_id(TRACK_CONTENT_ID)
	if track_index < 0:
		_fail("Twist Road is absent from the track catalog")
		return
	game_manager.track_selector.select(track_index)
	var existing := _existing_suite_samples(game_manager.replay_catalog_controller, game_manager.replay_playback_session)
	var requested_count := racer_counts.size() * sample_count
	var generated_count := 0
	var skipped_count := 0
	var total_bytes := 0
	var suite_start_usec := Time.get_ticks_usec()
	print(
		"MXT_REPLAY_BENCHMARK_BEGIN suite=", SUITE_ID,
		" counts=", racer_counts,
		" samples=", sample_count,
		" replay_dir=", game_manager.replay_catalog_controller._replay_dir())
	for racer_count_value in racer_counts:
		var racer_count := int(racer_count_value)
		for sample_index in range(1, sample_count + 1):
			var key := "%d:%d" % [racer_count, sample_index]
			if existing.has(key):
				skipped_count += 1
				print("MXT_REPLAY_BENCHMARK_SKIP racers=", racer_count, " sample=", sample_index, " path=", existing[key])
				continue
			var result := _run_race(track_index, racer_count, sample_index, max_ticks)
			if !bool(result.get("success", false)):
				_fail("racers=%d sample=%d: %s (%s)" % [racer_count, sample_index, String(result.get("error", "unknown error")), str(result)])
				return
			generated_count += 1
			total_bytes += int(result.get("bytes", 0))
			print(
				"MXT_REPLAY_BENCHMARK_SAVED racers=", racer_count,
				" sample=", sample_index,
				" ticks=", result.get("ticks", 0),
				" simulation_ms=", snappedf(float(result.get("simulation_usec", 0)) * 0.001, 0.001),
				" save_ms=", snappedf(float(result.get("save_usec", 0)) * 0.001, 0.001),
				" bytes=", result.get("bytes", 0),
				" path=", result.get("path", ""))
			game_manager._return_to_menu()
			await process_frame
	game_manager.replay_catalog_controller._build()
	var catalog_start_usec := Time.get_ticks_usec()
	game_manager.replay_catalog_controller.refresh()
	var catalog_usec := Time.get_ticks_usec() - catalog_start_usec
	print(
		"MXT_REPLAY_BENCHMARK_COMPLETE requested=", requested_count,
		" generated=", generated_count,
		" skipped=", skipped_count,
		" generated_bytes=", total_bytes,
		" suite_ms=", snappedf(float(Time.get_ticks_usec() - suite_start_usec) * 0.001, 0.001),
		" catalog_entries=", game_manager.replay_catalog_controller.entries.size(),
		" catalog_refresh_ms=", snappedf(float(catalog_usec) * 0.001, 0.001))
	# The active race has already been explicitly destroyed after each sample. Let
	# SceneTree own final root teardown; freeing Main here races GDExtension cleanup.
	quit(0)
