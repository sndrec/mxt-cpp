class_name ReplayRecorder
extends Node

signal staged_replay_saved

const REPLAY_SCHEMA_VERSION := 5
const REPLAY_FILE_SUFFIX := ".mxt_replay"
const SUBMISSION_REPLAY_ROOT := "user://leaderboard_submission_replays"
const GameVersionData = preload("res://core/game_version.gd")

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var vehicle_content_controller: VehicleContentController = get_node("../VehicleContentController") as VehicleContentController
@onready var debug_runtime_controller: DebugRuntimeController = get_node("../DebugRuntimeController") as DebugRuntimeController
@onready var race_session_controller: RaceSessionController = get_node("../RaceSessionController") as RaceSessionController
@onready var race_presentation_controller: RacePresentationController = get_node("../RacePresentationController") as RacePresentationController
@onready var pause_save_button: Button = get_node("../RacePauseLayer/RacePauseRoot/Center/Panel/Box/SaveReplayButton") as Button

var active := false
var saved := false
var source := ""
var metadata := MxtReplayRunMetadata.new()
var racer_ids: Array = []
var cpu_flags: Array = []
var stream := MxtReplayStream.new()
var staged_path := ""


func initialize() -> void:
	if pause_save_button != null and !pause_save_button.pressed.is_connected(_on_pause_save_pressed):
		pause_save_button.pressed.connect(_on_pause_save_pressed)
	var frame_signal := game_manager.network_manager.input_transport.authoritative_server_frame
	if !frame_signal.is_connected(record_authoritative_frame):
		frame_signal.connect(record_authoritative_frame)
	refresh_pause_button()


func reset(save_multiplayer_host_replay: bool) -> void:
	stop(save_multiplayer_host_replay)
	_clear_payload()
	staged_path = ""


func start(track_index: int, race_roster: MxtRaceRoster, start_grid_slots: PackedInt32Array) -> void:
	stop()
	_clear_payload()
	staged_path = ""
	if !_should_record_current_race() or race_roster == null:
		return
	var practice_session := game_manager.network_manager.race_configuration.is_practice()
	source = "practice" if practice_session else ("singleplayer" if game_manager.singleplayer_mode else "server")
	var normalized_roster := MxtRaceRoster.new()
	for index in race_roster.count():
		var settings := settings_with_vehicle_content_evidence(
			race_roster.get_settings_dictionary(index))
		var player_id := race_roster.get_player_id(index)
		if !normalized_roster.append_settings(
				player_id, player_id, race_roster.is_cpu(index), false, false, settings):
			push_error("Replay recording roster rejected: %s" % normalized_roster.get_last_error())
			return
		racer_ids.append(player_id)
		cpu_flags.append(race_roster.is_cpu(index))
	stream = MxtReplayStream.new()
	stream.begin_recording(racer_ids, cpu_flags)
	if !stream.get_last_error().is_empty():
		push_error("Replay recording roster rejected: %s" % stream.get_last_error())
		return
	active = true
	saved = false
	if practice_session:
		game_manager.practice_controller.configure_timeline_roster(racer_ids, cpu_flags)
	var track_content_id := game_manager.track_content_controller.track_id_for_index(track_index)
	var track_record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(track_content_id)
	metadata.schema_version = REPLAY_SCHEMA_VERSION
	metadata.build = GameVersionData.display_string()
	metadata.game_version_major = GameVersionData.MAJOR
	metadata.game_version_compatibility = GameVersionData.COMPATIBILITY
	metadata.game_version_patch = GameVersionData.PATCH
	metadata.engine_version = str(Engine.get_version_info().get("string", ""))
	metadata.created_unix = int(Time.get_unix_time_from_system())
	metadata.name = "%s %s" % [_current_track_name(), _make_stamp()]
	metadata.mode = _mode_name()
	metadata.source = source
	metadata.track_content_id = track_content_id
	metadata.track_gameplay_digest = game_manager.track_content_controller.track_gameplay_digest_for_index(track_index)
	metadata.track_package_digest = track_record.package_digest if track_record != null else ""
	metadata.track_workshop_id = str(track_record.published_file_id) if track_record != null and track_record.published_file_id > 0 else ""
	metadata.track_name = _current_track_name()
	metadata.roster = normalized_roster
	metadata.start_grid_slots = start_grid_slots
	metadata.spawn_seed = game_manager.network_manager.spawn_seed
	metadata.set_race_metadata(game_manager.network_manager.race_metadata_dictionary())
	metadata.runtime_auto_accelerate = debug_runtime_controller.auto_accelerate
	metadata.runtime_auto_bumpers = game_manager.auto_bumpers_mode
	metadata.runtime_debug_bumper_smoke = debug_runtime_controller.bumper_smoke_enabled
	metadata.runtime_debug_rail_trace = debug_runtime_controller.rail_trace_enabled
	refresh_pause_button()


func stop(save_multiplayer_host_replay := false) -> void:
	if save_multiplayer_host_replay and active and !saved and source == "server":
		if !_write("auto", _replay_dir()).is_empty():
			saved = true
	active = false


func finish() -> void:
	active = false
	refresh_pause_button()


func record_singleplayer_frame(tick: int) -> void:
	if !active:
		return
	if game_manager.practice_controller.session_active:
		game_manager.practice_controller.append_canonical_game_sim_frame(game_manager.game_sim, tick)
	elif !stream.append_game_sim_frame(game_manager.game_sim, tick):
		push_error("Replay recording rejected native frame %d: %s" % [tick, stream.get_last_error()])


func record_authoritative_frame(tick: int) -> void:
	if active and !saved and !stream.append_game_sim_frame(
			game_manager.network_manager.server_game_sim, tick):
		push_error("Replay recording rejected authoritative frame %d." % tick)


func can_save_locally() -> bool:
	if !game_manager.singleplayer_mode:
		return false
	if game_manager.practice_controller.session_active:
		return game_manager.practice_controller.timeline_enabled \
			and game_manager.practice_controller.canonical_frame_count() > 0
	return !saved and stream.frame_count() > 0


func save_locally() -> String:
	if !can_save_locally():
		return ""
	if game_manager.practice_controller.session_active:
		return save_practice_snapshot()
	var path := _write("manual", _replay_dir())
	if path.is_empty():
		return ""
	saved = true
	active = false
	_clear_payload()
	refresh_pause_button()
	return path


func save_practice_snapshot() -> String:
	if (
			!game_manager.singleplayer_mode
			or !game_manager.practice_controller.session_active
			or !game_manager.practice_controller.timeline_enabled
			or game_manager.practice_controller.canonical_frame_count() <= 0):
		return ""
	return _write("practice_snapshot", _replay_dir())


func stage_completed_time_attack(for_submission: bool) -> String:
	var configuration := game_manager.network_manager.race_configuration
	if (
			(!configuration.is_time_attack() and !configuration.is_practice())
			or for_submission != configuration.is_time_attack()):
		return ""
	finish()
	staged_path = _write(
		"time_attack_submission" if for_submission else "time_attack_preview",
		ProjectSettings.globalize_path(SUBMISSION_REPLAY_ROOT))
	if !staged_path.is_empty():
		_clear_payload()
	return staged_path


func can_save_staged_locally(path: String) -> bool:
	var local_path := staged_local_path(path)
	return !local_path.is_empty() and FileAccess.file_exists(path) and !FileAccess.file_exists(local_path)


func save_staged_locally(path: String) -> String:
	var local_path := staged_local_path(path)
	if local_path.is_empty() or !FileAccess.file_exists(path):
		return ""
	if FileAccess.file_exists(local_path):
		return local_path
	if DirAccess.make_dir_recursive_absolute(_replay_dir()) != OK:
		return ""
	if DirAccess.copy_absolute(ProjectSettings.globalize_path(path), local_path) != OK:
		return ""
	if game_manager.replay_catalog_controller.is_open():
		game_manager.replay_catalog_controller.refresh()
	staged_replay_saved.emit()
	return local_path


func refresh_pause_button() -> void:
	if pause_save_button == null:
		return
	var active_practice := game_manager.practice_controller.session_active \
		and game_manager.practice_controller.timeline_enabled
	var can_save := can_save_locally() and (
		active_practice or game_manager.network_manager.race_results.net_race_finish_time != -1)
	pause_save_button.text = "Save Current Replay" if active_practice else "Save Replay"
	pause_save_button.visible = can_save
	pause_save_button.disabled = !can_save


func memory_usage_stats() -> Dictionary:
	var practice_recording := game_manager.practice_controller.session_active \
		and game_manager.practice_controller.timeline_enabled
	return {
		"recording_frames": game_manager.practice_controller.canonical_frame_count() \
			if practice_recording else stream.frame_count(),
		"recording_input_bytes": game_manager.practice_controller.canonical_input_byte_count() \
			if practice_recording else stream.input_byte_count(),
	}


func settings_with_vehicle_content_evidence(settings: Dictionary) -> Dictionary:
	var output := settings.duplicate(true)
	var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(
		String(output.get("vehicle_content_id", "")))
	output["vehicle_gameplay_digest"] = record.gameplay_digest if record != null else ""
	if record != null and !record.package_digest.is_empty():
		output["vehicle_package_digest"] = record.package_digest
	if record != null and record.published_file_id > 0:
		output["vehicle_workshop_id"] = str(record.published_file_id)
	return output


func settings_array_with_vehicle_content_evidence(settings: Array) -> Array:
	var output: Array = []
	for value in settings:
		if typeof(value) == TYPE_DICTIONARY:
			output.append(settings_with_vehicle_content_evidence(value))
	return output


func race_roster_from_settings(
		settings: Array,
		input_racer_ids: Array,
		input_cpu_flags: Array) -> MxtRaceRoster:
	if settings.size() != input_racer_ids.size():
		push_error("Replay roster settings do not match the recorded racer IDs.")
		return null
	var roster := MxtRaceRoster.new()
	for index in settings.size():
		if typeof(settings[index]) != TYPE_DICTIONARY:
			push_error("Replay roster contains malformed player settings.")
			return null
		var player_id := int(input_racer_ids[index])
		var is_cpu := index < input_cpu_flags.size() and bool(input_cpu_flags[index])
		if !roster.append_settings(player_id, player_id, is_cpu, false, false, settings[index]):
			push_error("Replay roster conversion failed: %s" % roster.get_last_error())
			return null
	return roster


func _on_pause_save_pressed() -> void:
	var saved_path := save_practice_snapshot() \
		if game_manager.practice_controller.session_active else save_locally()
	if !saved_path.is_empty():
		race_presentation_controller.show_notification("Replay Saved", 2200)
	refresh_pause_button()


func _should_record_current_race() -> bool:
	if game_manager.replay_playback_session.replay_playback_active:
		return false
	if (
			game_manager.network_manager.race_configuration.is_practice()
			and game_manager.network_manager.race_configuration.lap_count == 0):
		return false
	return game_manager.singleplayer_mode or game_manager.network_manager.is_server


func _mode_name() -> String:
	if !game_manager.singleplayer_mode:
		return "Multiplayer"
	if game_manager.network_manager.race_configuration.is_practice():
		return "Practice"
	return "Time Attack" \
		if game_manager.network_manager.lobby_settings.get_cpu_roster().is_empty() else "CPU Race"


func _current_track_name() -> String:
	var index := race_session_controller.last_race_track_index
	if index >= 0 and index < game_manager.track_content_controller.tracks.size():
		return String(game_manager.track_content_controller.tracks[index].get("name", "track"))
	return "track"


func _write(reason: String, replay_dir: String) -> String:
	var export_stream := _stream_for_export()
	if export_stream == null or export_stream.frame_count() <= 0:
		return ""
	if DirAccess.make_dir_recursive_absolute(replay_dir) != OK:
		return ""
	var run_metadata := metadata.copy()
	run_metadata.saved_reason = reason
	run_metadata.duration_ticks = export_stream.frame_count()
	run_metadata.set_results(
		game_manager.network_manager.race_results.player_finish_times,
		game_manager.network_manager.race_results.player_finish_placements,
		game_manager.network_manager.race_results.player_eliminations)
	var safe_track := run_metadata.track_name.replace("/", "_").replace("\\", "_").replace(" ", "_")
	var path := _unique_path(replay_dir, safe_track)
	var temporary_path := path + ".tmp"
	if !export_stream.write_file(temporary_path, run_metadata.to_dictionary()):
		push_warning("Replay save failed: %s" % export_stream.get_last_error())
		return ""
	if DirAccess.rename_absolute(temporary_path, path) != OK:
		DirAccess.remove_absolute(temporary_path)
		return ""
	print("MXT_REPLAY saved ", path, " frames=", export_stream.frame_count())
	return path


func _stream_for_export() -> MxtReplayStream:
	if game_manager.practice_controller.session_active and game_manager.practice_controller.timeline_enabled:
		return game_manager.practice_controller.canonical_stream()
	return stream


func staged_local_path(path: String) -> String:
	if path.is_empty():
		return ""
	var staging_root := ProjectSettings.globalize_path(SUBMISSION_REPLAY_ROOT).simplify_path().to_lower()
	var absolute_path := ProjectSettings.globalize_path(path).simplify_path()
	var lower_path := absolute_path.to_lower()
	if !lower_path.begins_with(staging_root + "/") and !lower_path.begins_with(staging_root + "\\"):
		return ""
	return _replay_dir().path_join(absolute_path.get_file())


func _unique_path(replay_dir: String, safe_track: String) -> String:
	var basename := "mxt_%s_%s" % [safe_track, _make_stamp()]
	var path := replay_dir.path_join(basename + REPLAY_FILE_SUFFIX)
	var suffix := 2
	while FileAccess.file_exists(path):
		path = replay_dir.path_join("%s_%d%s" % [basename, suffix, REPLAY_FILE_SUFFIX])
		suffix += 1
	return path


func _clear_payload() -> void:
	metadata = MxtReplayRunMetadata.new()
	racer_ids.clear()
	cpu_flags.clear()
	stream = MxtReplayStream.new()


func _replay_dir() -> String:
	return ProjectSettings.globalize_path("user://replays")


func _make_stamp() -> String:
	return Time.get_datetime_string_from_system(false, true).replace(":", "-").replace(" ", "_")
