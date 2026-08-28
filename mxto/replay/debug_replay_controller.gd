class_name DebugReplayController
extends Node

const DEBUG_REPLAY_VERSION := 2

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var vehicle_content_controller: VehicleContentController = get_node("../VehicleContentController") as VehicleContentController
@onready var race_session_controller: RaceSessionController = get_node("../RaceSessionController") as RaceSessionController

var recording := false
var playing := false
var recorded_inputs: Array = []
var snapshot_tick := -1
var snapshot_state := PackedByteArray()
var playback_inputs: Array = []
var playback_index := 0
var loaded_path := ""


func configure_command_line(args: Array, user_args: Array) -> bool:
	var index := args.find("--debug-replay")
	var source := args
	if index == -1:
		index = user_args.find("--debug-replay")
		source = user_args
	if index == -1 or index + 1 >= source.size():
		return false
	call_deferred("load_and_start", String(source[index + 1]))
	return true


func handle_unhandled_input(event: InputEvent) -> bool:
	if !(event is InputEventKey) or !event.pressed or event.echo:
		return false
	if event.keycode == KEY_F5:
		if recording:
			_stop_and_save()
		else:
			_start_recording()
		return true
	if event.keycode == KEY_F8:
		var path := DisplayServer.clipboard_get().strip_edges()
		if !path.is_empty():
			load_and_start(path)
		return true
	return false


func consume_playback_input(input_bytes: PackedByteArray) -> PackedByteArray:
	if !playing:
		return input_bytes
	if playback_index >= playback_inputs.size():
		playing = false
		game_manager.game_sim.set_sim_started(false)
		print("MXT_DEBUG_REPLAY playback complete ", loaded_path,
			" end_tick=", game_manager._singleplayer_tick)
		if game_manager.headless_mode:
			get_tree().quit()
		return PackedByteArray()
	var replay_input := (playback_inputs[playback_index] as PackedByteArray).duplicate()
	playback_index += 1
	return replay_input


func record_input(input_bytes: PackedByteArray) -> void:
	if recording:
		recorded_inputs.append(input_bytes.duplicate())


func reset_for_transition() -> void:
	if recording:
		_stop_and_save()
	playing = false
	recorded_inputs.clear()
	playback_inputs.clear()
	snapshot_state = PackedByteArray()
	loaded_path = ""


func memory_usage_stats() -> Dictionary:
	return {
		"debug_recording_frames": recorded_inputs.size(),
		"debug_playback_frames": playback_inputs.size(),
	}


func load_and_start(path: String) -> void:
	var replay := _load_file(path)
	if replay.is_empty():
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	if recording:
		_stop_and_save()
	if game_manager.game_sim.sim_started or game_manager.singleplayer_mode:
		game_manager._return_to_menu()
	var track_index := game_manager.track_content_controller.track_index_for_compatible_evidence(
		String(replay.get("track_content_id", "")),
		String(replay.get("track_gameplay_digest", "")),
		String(replay.get("track_package_digest", "")),
		String(replay.get("track_workshop_id", "")))
	if track_index < 0 or track_index >= game_manager.track_content_controller.tracks.size():
		_load_failed("MXT_DEBUG_REPLAY load failed: track not found for %s" % replay.get("track_name", ""))
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or settings.is_empty():
		_load_failed("MXT_DEBUG_REPLAY load failed: replay has no racer settings.")
		return
	if !vehicle_content_controller.vehicle_settings_content_available(settings as Array):
		_load_failed("MXT_DEBUG_REPLAY load failed: exact vehicle gameplay content is unavailable.")
		return
	var state_tick := int(replay.get("snapshot_tick", -1))
	var state := Marshalls.base64_to_raw(String(replay.get("snapshot_state_b64", "")))
	if state_tick < 0 or state.is_empty():
		_load_failed("MXT_DEBUG_REPLAY load failed: missing native snapshot.")
		return
	playback_inputs.clear()
	var encoded_inputs = replay.get("inputs_b64", [])
	if typeof(encoded_inputs) != TYPE_ARRAY:
		_load_failed("MXT_DEBUG_REPLAY load failed: inputs_b64 is not an array.")
		return
	for encoded_input in encoded_inputs:
		playback_inputs.append(Marshalls.base64_to_raw(String(encoded_input)))
	if playback_inputs.is_empty():
		_load_failed("MXT_DEBUG_REPLAY load failed: replay has no input frames.")
		return

	game_manager.singleplayer_mode = true
	game_manager._singleplayer_tick = 0
	game_manager.network_manager.reset_race_state()
	game_manager.network_manager.set_spawn_seed(int(replay.get("spawn_seed", 0)))
	var local_id := game_manager._local_player_id()
	game_manager.network_manager.player_ids = [local_id]
	game_manager.network_manager.spectator_ids = []
	game_manager.singleplayer_cpu_count = maxi(0, settings.size() - 1)
	game_manager.network_manager.lobby_settings.set_cpu_driver_count(
		game_manager.singleplayer_cpu_count)
	game_manager.network_manager.lobby_settings.set_player_settings(local_id, settings[0])
	var cpu_ids := game_manager.network_manager.lobby_settings.get_cpu_roster()
	for index in cpu_ids.size():
		if index + 1 < settings.size():
			game_manager.network_manager.lobby_settings.set_player_settings(
				cpu_ids[index], settings[index + 1], true)

	game_manager._close_settings_menus_for_race_start()
	game_manager.race_dnf_low_speed_ticks.clear()
	var racer_ids: Array = [local_id]
	racer_ids.append_array(cpu_ids)
	var cpu_flags: Array = [false]
	for _cpu_id in cpu_ids:
		cpu_flags.append(true)
	var roster := game_manager.replay_recorder.race_roster_from_settings(
		settings, racer_ids, cpu_flags)
	if roster == null:
		game_manager._return_to_menu()
		return
	race_session_controller.start_race(
		track_index, roster, game_manager.singleplayer_mode, game_manager.headless_mode)
	if !game_manager.game_sim.load_state_data(state_tick, state):
		game_manager._return_to_menu()
		_load_failed("MXT_DEBUG_REPLAY load failed: native snapshot could not be applied.")
		return
	game_manager._singleplayer_tick = state_tick + 1
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	playback_index = 0
	playing = true
	loaded_path = path
	game_manager.get_node("Control").visible = false
	game_manager.lobby_control.visible = false
	print("MXT_DEBUG_REPLAY playback started ", path,
		" start_tick=", game_manager._singleplayer_tick,
		" frames=", playback_inputs.size())


func _start_recording() -> void:
	if !game_manager.singleplayer_mode or !game_manager.game_sim.sim_started:
		print("MXT_DEBUG_REPLAY record ignored: start a singleplayer race first.")
		return
	if game_manager._singleplayer_tick <= 0:
		print("MXT_DEBUG_REPLAY record ignored: wait one physics tick, then press F5 again.")
		return
	snapshot_tick = game_manager._singleplayer_tick - 1
	snapshot_state = game_manager.game_sim.get_state_data(snapshot_tick)
	if snapshot_state.is_empty():
		print("MXT_DEBUG_REPLAY record failed: native state snapshot was empty.")
		return
	recorded_inputs.clear()
	recording = true
	print("MXT_DEBUG_REPLAY recording from completed_tick=", snapshot_tick)


func _stop_and_save() -> void:
	if !recording:
		return
	recording = false
	var directory := _directory()
	var error := DirAccess.make_dir_recursive_absolute(directory)
	if error != OK:
		print("MXT_DEBUG_REPLAY save failed: could not create ", directory, " err=", error)
		return
	var encoded_inputs: Array = []
	for input_bytes: PackedByteArray in recorded_inputs:
		encoded_inputs.append(Marshalls.raw_to_base64(input_bytes))
	var track_id := _current_track_id()
	var track_record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(track_id)
	var replay := {
		"version": DEBUG_REPLAY_VERSION,
		"created_unix": Time.get_unix_time_from_system(),
		"track_content_id": track_id,
		"track_gameplay_digest": _current_track_gameplay_digest(),
		"track_package_digest": track_record.package_digest if track_record != null else "",
		"track_workshop_id": str(track_record.published_file_id) if track_record != null and track_record.published_file_id > 0 else "",
		"track_name": _current_track_name(),
		"settings": game_manager.replay_recorder.settings_array_with_vehicle_content_evidence(
			race_session_controller.last_race_settings),
		"singleplayer_cpu_count": game_manager.singleplayer_cpu_count,
		"spawn_seed": game_manager.network_manager.spawn_seed,
		"snapshot_tick": snapshot_tick,
		"snapshot_state_b64": Marshalls.raw_to_base64(snapshot_state),
		"inputs_b64": encoded_inputs,
	}
	var safe_track := _current_track_name().replace("/", "_").replace("\\", "_").replace(" ", "_")
	var timestamp := Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")
	var path := directory.path_join("mxt_%s_%s.json" % [safe_track, timestamp])
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		print("MXT_DEBUG_REPLAY save failed: ", FileAccess.get_open_error())
		return
	file.store_string(JSON.stringify(replay, "\t"))
	file.close()
	print("MXT_DEBUG_REPLAY saved ", path, " frames=", recorded_inputs.size())


func _load_file(path: String) -> Dictionary:
	var resolved_path := path
	if resolved_path.begins_with("user://") or resolved_path.begins_with("res://"):
		resolved_path = ProjectSettings.globalize_path(resolved_path)
	elif !resolved_path.is_absolute_path():
		var project_directory := ProjectSettings.globalize_path("res://")
		var project_candidate := project_directory.path_join(resolved_path)
		var repository_candidate := project_directory.path_join("..").simplify_path().path_join(resolved_path)
		if FileAccess.file_exists(project_candidate):
			resolved_path = project_candidate
		elif FileAccess.file_exists(repository_candidate):
			resolved_path = repository_candidate
	if !FileAccess.file_exists(resolved_path):
		print("MXT_DEBUG_REPLAY load failed: file not found: ", resolved_path)
		return {}
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(resolved_path))
	if typeof(parsed) != TYPE_DICTIONARY:
		print("MXT_DEBUG_REPLAY load failed: JSON root is not a dictionary.")
		return {}
	if int(parsed.get("version", 0)) != DEBUG_REPLAY_VERSION:
		print("MXT_DEBUG_REPLAY load failed: unsupported version ", parsed.get("version", null))
		return {}
	return parsed


func _current_track_name() -> String:
	var index := race_session_controller.last_race_track_index
	if index >= 0 and index < game_manager.track_content_controller.tracks.size():
		return String(game_manager.track_content_controller.tracks[index].get("name", "track"))
	return "track"


func _current_track_id() -> String:
	var index := race_session_controller.last_race_track_index
	return game_manager.track_content_controller.track_id_for_index(index) \
		if index >= 0 and index < game_manager.track_content_controller.tracks.size() else ""


func _current_track_gameplay_digest() -> String:
	var index := race_session_controller.last_race_track_index
	return game_manager.track_content_controller.track_gameplay_digest_for_index(index) \
		if index >= 0 and index < game_manager.track_content_controller.tracks.size() else ""


func _directory() -> String:
	return ProjectSettings.globalize_path("user://debug_replays")


func _load_failed(message: String) -> void:
	print(message)
	if game_manager.headless_mode:
		get_tree().quit(1)
