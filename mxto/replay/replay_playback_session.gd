class_name ReplayPlaybackSession extends Node

const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")
const RacePresentationControllerClass = preload("res://ui/race_presentation_controller.gd")
const RaceSessionControllerClass = preload("res://core/race_session_controller.gd")

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var vehicle_content_controller: VehicleContentControllerClass = get_node("../VehicleContentController") as VehicleContentControllerClass
@onready var race_presentation_controller: RacePresentationControllerClass = get_node("../RacePresentationController") as RacePresentationControllerClass
@onready var race_session_controller: RaceSessionControllerClass = get_node("../RaceSessionController") as RaceSessionControllerClass
@onready var replay_camera_controller: ReplayCameraController = get_node("../ReplayCameraController") as ReplayCameraController
@onready var replay_timeline_controller: ReplayTimelineController = get_node("../ReplayTimelineController") as ReplayTimelineController

const REPLAY_SCHEMA_VERSION := 5
const REPLAY_COMPATIBILITY_WARNING := "This replay was recorded on an older version of the game, and may desync."
const GameVersionData = preload("res://core/game_version.gd")
const REPLAY_SEEK_CHECKPOINT_INTERVAL := 1800
const TimeAttackRulesClass = preload("res://leaderboards/time_attack_rules.gd")
const LeaderboardReplayValidatorClass = preload("res://leaderboards/leaderboard_replay_validator.gd")
const LegacyLeaderboardReplayReaderClass = preload("res://leaderboards/legacy_leaderboard_replay_reader.gd")

var replay_autoload_path: String = ""
var replay_start_grid_slots: PackedInt32Array = PackedInt32Array()
var replay_playback_active: bool = false
var replay_playback_stream: MxtReplayStream
var replay_playback_source_metadata: MxtReplayRunMetadata = MxtReplayRunMetadata.new()
var replay_playback_compatible := false
var replay_playback_track_index := -1
var replay_playback_index: int = 0
var replay_playback_loaded_path: String = ""
var replay_playback_focus_index: int = 0
var replay_playback_racer_ids: Array = []
var replay_playback_cpu_flags: Array = []
var replay_playback_local_player_id: int = 0
var replay_playback_use_multiplayer_startup: bool = false
var replay_strict_verify_requested: bool = false
var replay_leaderboard_verify_requested: bool = false
var replay_leaderboard_validation: Dictionary = {}
var replay_skip_seek_bake_requested: bool = false
var replay_load_profile_requested: bool = false
var replay_playback_use_singleplayer_tick: bool = false
var replay_saved_finish_times: Dictionary = {}
var replay_saved_finish_placements: Dictionary = {}
var replay_saved_eliminations: Dictionary = {}
var replay_playback_paused: bool = false
var replay_playback_rate: float = 1.0
var replay_seek_checkpoints: Array = []
var replay_seek_checkpoint_bytes: int = 0
var replay_playback_source_bytes: int = 0
var replay_seeking_active: bool = false
var replay_normal_playback_tick_active: bool = false
var replay_compatibility_dialog: ConfirmationDialog
var pending_incompatible_replay_path := ""

func configure_command_line(args: Array, user_args: Array) -> bool:
	var real_replay_idx := args.find("--replay")
	var real_replay_args := args
	if real_replay_idx == -1:
		real_replay_idx = user_args.find("--replay")
		real_replay_args = user_args
	if real_replay_idx != -1 and real_replay_idx + 1 < real_replay_args.size():
		replay_autoload_path = String(real_replay_args[real_replay_idx + 1])
	replay_leaderboard_verify_requested = args.has("--leaderboard-replay-verify") or user_args.has("--leaderboard-replay-verify")
	replay_strict_verify_requested = replay_leaderboard_verify_requested or args.has("--strict-replay-verify") or user_args.has("--strict-replay-verify")
	replay_skip_seek_bake_requested = args.has("--skip-replay-seek-bake") or user_args.has("--skip-replay-seek-bake")
	replay_load_profile_requested = args.has("--profile-replay-load") or user_args.has("--profile-replay-load")
	if replay_autoload_path != "":
		call_deferred("_request_replay_playback_from_path", replay_autoload_path)
		return true
	return false

func reset_playback_for_transition() -> void:
	replay_playback_active = false
	replay_timeline_controller.apply_hud_visibility()
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_normal_playback_tick_active = false
	replay_seek_checkpoints.clear()
	replay_seek_checkpoint_bytes = 0
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	replay_timeline_controller.reset()
	replay_start_grid_slots = PackedInt32Array()
	_clear_playback_payload()
	_apply_replay_playback_clock()


func detach_playback_for_practice() -> bool:
	if !replay_playback_active:
		return false
	replay_playback_active = false
	replay_timeline_controller.apply_hud_visibility()
	replay_playback_use_multiplayer_startup = false
	replay_playback_use_singleplayer_tick = false
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_normal_playback_tick_active = false
	replay_seeking_active = false
	replay_seek_checkpoints.clear()
	replay_seek_checkpoint_bytes = 0
	replay_saved_finish_times.clear()
	replay_saved_finish_placements.clear()
	replay_saved_eliminations.clear()
	replay_timeline_controller.reset()
	replay_start_grid_slots = PackedInt32Array()
	_clear_playback_payload()
	_apply_replay_playback_clock()
	return true


func _clear_playback_payload() -> void:
	replay_playback_stream = null
	replay_playback_source_metadata = MxtReplayRunMetadata.new()
	replay_playback_compatible = false
	replay_playback_track_index = -1
	replay_playback_index = 0
	replay_playback_loaded_path = ""
	replay_timeline_controller.refresh_save_local_button()
	replay_playback_focus_index = 0
	replay_playback_racer_ids.clear()
	replay_playback_cpu_flags.clear()
	replay_playback_local_player_id = 0
	replay_playback_source_bytes = 0

func get_memory_usage_stats() -> Dictionary:
	var stats := game_manager.replay_recorder.memory_usage_stats()
	stats.merge({
		"playback_frames": _playback_frame_count(),
		"playback_source_bytes": replay_playback_source_bytes,
		"seek_checkpoint_count": replay_seek_checkpoints.size(),
		"seek_checkpoint_bytes": replay_seek_checkpoint_bytes,
	})
	stats.merge(game_manager.debug_replay_controller.memory_usage_stats())
	return stats

func _replay_is_compatible(data: Dictionary) -> bool:
	if !_replay_schema_is_supported(data):
		return false
	var stored_version = data.get("game_version", {})
	if typeof(stored_version) != TYPE_DICTIONARY:
		return false
	var version: Dictionary = stored_version
	return int(version.get("major", -1)) == GameVersionData.MAJOR and int(version.get("compatibility", -1)) == GameVersionData.COMPATIBILITY

func _replay_schema_is_supported(data: Dictionary) -> bool:
	return int(data.get("schema_version", -1)) == REPLAY_SCHEMA_VERSION

func _load_replay_file(path: String) -> Dictionary:
	if !FileAccess.file_exists(path):
		push_warning("Replay load failed: file not found: %s" % path)
		return {}
	var stream := MxtReplayStream.new()
	if !stream.load_file(path):
		if !replay_leaderboard_verify_requested:
			push_warning("Replay load failed: %s" % stream.get_last_error())
			return {}
		var legacy_bytes := FileAccess.get_file_as_bytes(path)
		var legacy_result := LegacyLeaderboardReplayReaderClass.convert(game_manager, legacy_bytes)
		if !bool(legacy_result.get("valid", false)):
			push_warning("Legacy leaderboard replay load failed: %s" % String(legacy_result.get("reason", "invalid_legacy_leaderboard_replay")))
			return {}
		stream = legacy_result.get("_native_stream") as MxtReplayStream
		var legacy_metadata_value = legacy_result.get("_native_metadata", {})
		if stream == null or typeof(legacy_metadata_value) != TYPE_DICTIONARY:
			push_warning("Legacy leaderboard replay conversion produced no replay stream.")
			return {}
		var legacy_metadata: Dictionary = legacy_metadata_value
		legacy_metadata["_replay_stream"] = stream
		replay_playback_source_bytes = legacy_bytes.size()
		return legacy_metadata
	var parsed: Dictionary = stream.get_metadata()
	replay_playback_source_bytes = int(stream.get_stats().get("source_bytes", 0))
	if !_replay_schema_is_supported(parsed):
		push_warning("Replay load refused: unsupported schema %s; expected schema %d." % [
			str(parsed.get("schema_version", -1)),
			REPLAY_SCHEMA_VERSION,
		])
		return {}
	if replay_strict_verify_requested and !_replay_is_compatible(parsed):
		push_warning("Replay load refused: compatibility mismatch. Stored=%s expected=%d.%d schema=%s expected_schema=%d" % [
			str(parsed.get("game_version", {})),
			GameVersionData.MAJOR,
			GameVersionData.COMPATIBILITY,
			str(parsed.get("schema_version", -1)),
			REPLAY_SCHEMA_VERSION,
		])
		return {}
	parsed["_replay_stream"] = stream
	return parsed

func _request_replay_playback_from_path(path: String) -> void:
	var metadata := _load_replay_metadata_file(path)
	if metadata.is_empty() or !_replay_schema_is_supported(metadata) or _replay_is_compatible(metadata) or replay_strict_verify_requested:
		_start_replay_playback_from_path(path, true)
		return
	if game_manager.headless_mode:
		push_warning(REPLAY_COMPATIBILITY_WARNING)
		_start_replay_playback_from_path(path, true)
		return
	_show_replay_compatibility_warning(path)

func play_replay_file(path: String) -> void:
	_request_replay_playback_from_path(path)

func _show_replay_compatibility_warning(path: String) -> void:
	pending_incompatible_replay_path = path
	if replay_compatibility_dialog == null or !is_instance_valid(replay_compatibility_dialog):
		replay_compatibility_dialog = ConfirmationDialog.new()
		replay_compatibility_dialog.name = "ReplayCompatibilityWarning"
		replay_compatibility_dialog.title = "Replay Compatibility Warning"
		replay_compatibility_dialog.dialog_text = REPLAY_COMPATIBILITY_WARNING
		replay_compatibility_dialog.ok_button_text = "Play Anyway"
		replay_compatibility_dialog.cancel_button_text = "Cancel"
		replay_compatibility_dialog.exclusive = true
		replay_compatibility_dialog.confirmed.connect(_on_replay_compatibility_confirmed)
		replay_compatibility_dialog.canceled.connect(_on_replay_compatibility_canceled)
		add_child(replay_compatibility_dialog)
	replay_compatibility_dialog.popup_centered(Vector2i(620, 170))

func _on_replay_compatibility_confirmed() -> void:
	var path := pending_incompatible_replay_path
	pending_incompatible_replay_path = ""
	if !path.is_empty():
		call_deferred("_start_replay_playback_from_path", path, true)

func _on_replay_compatibility_canceled() -> void:
	pending_incompatible_replay_path = ""

func _load_replay_metadata_file(path: String, profile: Dictionary = {}) -> Dictionary:
	if path == "" or !FileAccess.file_exists(path):
		return {}
	var read_start_usec := Time.get_ticks_usec()
	var stream := MxtReplayStream.new()
	var loaded := stream.load_file(path, true)
	profile["read_usec"] = Time.get_ticks_usec() - read_start_usec
	var source_file := FileAccess.open(path, FileAccess.READ)
	profile["bytes"] = source_file.get_length() if source_file != null else 0
	profile["strip_frames_usec"] = 0
	profile["parse_usec"] = 0
	if !loaded:
		return {}
	return stream.get_metadata()


func _find_replay_track_index(data: Dictionary) -> int:
	var content_id := String(data.get("track_content_id", ""))
	var gameplay_digest := String(data.get("track_gameplay_digest", ""))
	if content_id.is_empty() or gameplay_digest.is_empty():
		return -1
	var track_index := game_manager.track_content_controller.track_index_for_id(content_id)
	if track_index < 0 or game_manager.track_content_controller.track_gameplay_digest_for_index(
			track_index) != gameplay_digest:
		return -1
	var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(content_id)
	if record == null:
		return -1
	if !record.package_digest.is_empty() and String(data.get(
			"track_package_digest", "")) != record.package_digest:
		return -1
	var workshop_id := str(record.published_file_id) if record.published_file_id > 0 else ""
	if !workshop_id.is_empty() and String(data.get("track_workshop_id", "")) != workshop_id:
		return -1
	return track_index


func _replay_vehicle_content_available(settings: Array) -> bool:
	for value in settings:
		if typeof(value) != TYPE_DICTIONARY:
			return false
		var player_settings: Dictionary = value
		var content_id := String(player_settings.get("vehicle_content_id", ""))
		var gameplay_digest := String(player_settings.get("vehicle_gameplay_digest", ""))
		if content_id.is_empty() or gameplay_digest.is_empty():
			return false
		var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(content_id)
		if record == null or record.gameplay_digest != gameplay_digest:
			return false
		if !record.package_digest.is_empty() and String(player_settings.get(
				"vehicle_package_digest", "")) != record.package_digest:
			return false
		var workshop_id := str(record.published_file_id) if record.published_file_id > 0 else ""
		if !workshop_id.is_empty() and String(player_settings.get(
				"vehicle_workshop_id", "")) != workshop_id:
			return false
	return true


func _decoded_replay_frame_at(frame_index: int) -> Dictionary:
	if replay_playback_stream == null or frame_index < 0 or frame_index >= replay_playback_stream.frame_count():
		return {}
	return replay_playback_stream.read_frame(frame_index)


func _decoded_replay_frame_range(begin_index: int, end_index: int) -> Array:
	var output: Array = []
	if replay_playback_stream == null:
		return output
	return replay_playback_stream.read_frame_range(begin_index, end_index)

func _playback_frame_count() -> int:
	return replay_playback_stream.frame_count() if replay_playback_stream != null else 0

func _replay_int_dictionary(source: Dictionary) -> Dictionary:
	var out := {}
	for key in source.keys():
		out[int(key)] = int(source[key])
	return out

func _replay_compare_int_dictionary(label: String, expected_raw: Dictionary, actual_raw: Dictionary) -> bool:
	var expected := _replay_int_dictionary(expected_raw)
	var actual := _replay_int_dictionary(actual_raw)
	var ok := true
	for key in expected.keys():
		if !actual.has(key):
			push_warning("Replay verify %s missing id=%d expected=%d" % [label, int(key), int(expected[key])])
			ok = false
		elif int(actual[key]) != int(expected[key]):
			push_warning("Replay verify %s mismatch id=%d expected=%d actual=%d" % [label, int(key), int(expected[key]), int(actual[key])])
			ok = false
	for key in actual.keys():
		if !expected.has(key):
			push_warning("Replay verify %s unexpected id=%d actual=%d" % [label, int(key), int(actual[key])])
			ok = false
	return ok

func _verify_replay_playback_results() -> bool:
	var ok := true
	ok = _replay_compare_int_dictionary("finish_times", replay_saved_finish_times, game_manager.network_manager.race_results.player_finish_times) and ok
	ok = _replay_compare_int_dictionary("finish_placements", replay_saved_finish_placements, game_manager.network_manager.race_results.player_finish_placements) and ok
	ok = _replay_compare_int_dictionary("eliminations", replay_saved_eliminations, game_manager.network_manager.race_results.player_eliminations) and ok
	if !ok and game_manager.game_sim != null and game_manager.game_sim.has_method("get_player_debug_string"):
		for id_value in replay_playback_racer_ids:
			print("MXT_REPLAY_VERIFY_STATE tick=", game_manager._singleplayer_tick, " ", game_manager.game_sim.get_player_debug_string(int(id_value)))
	return ok

func _lookup_replay_tick_for_id(source: Dictionary, player_id: int, fallback: int = -1) -> int:
	if source.has(player_id):
		return int(source[player_id])
	var key := str(player_id)
	if source.has(key):
		return int(source[key])
	return fallback

func _leaderboard_verified_result() -> Dictionary:
	if !bool(replay_leaderboard_validation.get("valid", false)) or replay_playback_racer_ids.size() != 1:
		return {"valid": false, "reason": "missing_leaderboard_validation"}
	var racer_id := int(replay_playback_racer_ids[0])
	var finish_tick := _lookup_replay_tick_for_id(game_manager.network_manager.race_results.player_finish_times, racer_id)
	var start_tick := race_presentation_controller.race_results_start_tick()
	if finish_tick <= start_tick:
		return {"valid": false, "reason": "invalid_resimulated_finish_time"}
	var result := replay_leaderboard_validation.duplicate(true)
	result["finish_tick"] = finish_tick
	result["start_tick"] = start_tick
	result["score_milliseconds"] = TimeAttackRulesClass.finish_ticks_to_milliseconds(finish_tick, start_tick)
	return result

func _replay_resume_eligibility() -> Dictionary:
	if !replay_playback_active:
		return {"eligible": false, "reason": "No replay is currently playing."}
	if !replay_playback_compatible:
		return {"eligible": false, "reason": "Older-version replays cannot resume into Practice."}
	if replay_playback_track_index < 0:
		return {"eligible": false, "reason": "The replay's exact track is unavailable."}
	if replay_playback_racer_ids.is_empty() or replay_playback_focus_index >= replay_playback_racer_ids.size():
		return {"eligible": false, "reason": "The focused replay racer is unavailable."}
	if game_manager.game_sim == null or !game_manager.game_sim.sim_started \
			or !game_manager.game_sim.has_method("get_full_state_data") \
			or !game_manager.game_sim.has_method("load_full_state_data"):
		return {"eligible": false, "reason": "The current replay position has no resumable simulation state."}
	var focus_id := replay_camera_controller.focused_player_id()
	if game_manager.game_sim.get_finished_player_ids().has(focus_id):
		return {"eligible": false, "reason": "A racer cannot resume after completing the race."}
	if replay_playback_source_metadata.roster == null \
			or replay_playback_source_metadata.roster.count() != replay_playback_racer_ids.size():
		return {"eligible": false, "reason": "The replay does not contain a complete recorded roster."}
	if replay_playback_source_metadata.race_configuration == null:
		return {"eligible": false, "reason": "The replay does not contain resumable race settings."}
	if game_manager._singleplayer_tick < 0 or game_manager._singleplayer_tick > _playback_frame_count():
		return {"eligible": false, "reason": "The current replay cursor is unavailable."}
	return {"eligible": true, "reason": "Continue from this exact frame as an unranked Practice session."}


func _capture_replay_resume_payload(keep_original: bool) -> Dictionary:
	var eligibility := _replay_resume_eligibility()
	if !bool(eligibility.get("eligible", false)):
		return {}
	var transition_start_usec := Time.get_ticks_usec()
	replay_playback_paused = true
	_apply_replay_playback_clock()
	var cursor := game_manager._singleplayer_tick
	var full_state: PackedByteArray = game_manager.game_sim.get_full_state_data(cursor)
	if full_state.is_empty():
		return {}
	return {
		"transition_start_usec": transition_start_usec,
		"cursor": cursor,
		"full_state": full_state,
		"race_results": game_manager.network_manager.race_results.capture_practice_state(),
		"race_dnf_low_speed_ticks": game_manager.race_dnf_low_speed_ticks.duplicate(true),
		"focused_player_id": replay_camera_controller.focused_player_id(),
		"track_index": replay_playback_track_index,
		"metadata": replay_playback_source_metadata.to_dictionary(),
		"replay_stream": replay_playback_stream,
		"canonical_prefix_count": cursor,
		"keep_original_as_ghost": keep_original,
	}

func _set_replay_playback_rate(rate: float) -> void:
	var rates := [0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0]
	var best := 1.0
	var best_delta := INF
	for value in rates:
		var delta := absf(float(value) - rate)
		if delta < best_delta:
			best = float(value)
			best_delta = delta
	replay_playback_rate = best
	_apply_replay_playback_clock()

func _apply_replay_playback_clock() -> void:
	if replay_playback_active and !replay_playback_paused:
		Engine.time_scale = replay_playback_rate
		Engine.physics_ticks_per_second = maxi(1, roundi(60.0 * replay_playback_rate))
	else:
		Engine.time_scale = 1.0
		Engine.physics_ticks_per_second = 60

func _format_replay_playback_rate() -> String:
	if replay_playback_rate >= 1.0:
		return "%dx" % roundi(replay_playback_rate)
	return "%.3fx" % replay_playback_rate

func _step_replay_by_ticks(delta_ticks: int) -> void:
	if !replay_playback_active:
		return
	if delta_ticks == 0:
		return
	replay_playback_paused = true
	_apply_replay_playback_clock()
	var target_tick := clampi(game_manager._singleplayer_tick + delta_ticks, 0, _playback_frame_count())
	if target_tick == game_manager._singleplayer_tick:
		replay_timeline_controller.update()
		return
	if target_tick < game_manager._singleplayer_tick:
		_seek_replay_to_tick(target_tick, false)
	else:
		while game_manager._singleplayer_tick < target_tick and replay_playback_index < _playback_frame_count():
			if !simulate_playback(false, false, false):
				break
		replay_camera_controller.apply_focus_to_local_visual()
		if game_manager.game_sim.sim_started:
			game_manager._update_native_render_camera()
			game_manager.game_sim.render_gamesim()
	replay_timeline_controller.update()

func _start_replay_playback_from_path(path: String, compatibility_warning_accepted := false) -> void:
	var profile_start_us := Time.get_ticks_usec()
	var replay := _load_replay_file(path)
	var loaded_source_bytes := replay_playback_source_bytes
	var profile_load_us := Time.get_ticks_usec() - profile_start_us
	if replay.is_empty():
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	if !_replay_is_compatible(replay) and !compatibility_warning_accepted:
		if game_manager.headless_mode:
			push_warning(REPLAY_COMPATIBILITY_WARNING)
		else:
			_show_replay_compatibility_warning(path)
			return
	if replay_leaderboard_verify_requested:
		replay_leaderboard_validation = LeaderboardReplayValidatorClass.validate(game_manager, replay, replay.get("_replay_stream"))
		if !bool(replay_leaderboard_validation.get("valid", false)):
			print("MXT_LEADERBOARD_VERIFY_FAIL ", JSON.stringify(replay_leaderboard_validation))
			if game_manager.headless_mode:
				get_tree().quit(1)
			return
		if replay.has("legacy_leaderboard_schema_version"):
			replay_leaderboard_validation["replay_schema_version"] = int(replay.get("legacy_leaderboard_schema_version", -1))
	if game_manager.game_sim.sim_started or game_manager.singleplayer_mode:
		game_manager._return_to_menu()
	var track_index := _find_replay_track_index(replay)
	if track_index < 0 or track_index >= game_manager.track_content_controller.tracks.size():
		push_warning("Replay load failed: track not found for %s" % str(replay.get("track_name", "")))
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	var loaded_stream = replay.get("_replay_stream")
	if !(loaded_stream is MxtReplayStream) or (loaded_stream as MxtReplayStream).frame_count() <= 0:
		push_warning("Replay load failed: replay has no frames.")
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	var settings = replay.get("settings", [])
	if typeof(settings) != TYPE_ARRAY or (settings as Array).is_empty():
		push_warning("Replay load failed: replay has no racer settings.")
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	if !_replay_vehicle_content_available(settings as Array):
		push_warning("Replay load failed: exact vehicle gameplay content is unavailable.")
		if game_manager.headless_mode:
			get_tree().quit(1)
		return
	var racer_ids: Array = replay.get("racer_ids", [])
	var cpu_flags: Array = replay.get("cpu_flags", [])
	if racer_ids.is_empty():
		for i in range((settings as Array).size()):
			racer_ids.append(i)
			cpu_flags.append(false)
	var metadata_boundary: Dictionary = replay.duplicate(true)
	metadata_boundary["racer_ids"] = racer_ids
	metadata_boundary["cpu_flags"] = cpu_flags
	var profile_validate_us := Time.get_ticks_usec() - profile_start_us - profile_load_us
	replay_playback_active = true
	replay_playback_stream = loaded_stream as MxtReplayStream
	replay_playback_source_metadata = MxtReplayRunMetadata.new()
	if !replay_playback_source_metadata.load_dictionary(metadata_boundary):
		push_warning("Replay load failed: %s" % replay_playback_source_metadata.get_last_error())
		replay_playback_active = false
		return
	replay_playback_compatible = _replay_is_compatible(replay)
	replay_playback_track_index = track_index
	replay_playback_source_bytes = loaded_source_bytes
	var profile_frames_duplicate_us := Time.get_ticks_usec() - profile_start_us - profile_load_us - profile_validate_us
	replay_playback_index = 0
	replay_playback_loaded_path = path
	replay_timeline_controller.refresh_save_local_button()
	replay_playback_racer_ids = racer_ids.duplicate(true)
	replay_playback_cpu_flags = cpu_flags.duplicate(true)
	replay_saved_finish_times = (replay.get("finish_times", {}) as Dictionary).duplicate(true) if typeof(replay.get("finish_times", {})) == TYPE_DICTIONARY else {}
	replay_saved_finish_placements = (replay.get("finish_placements", {}) as Dictionary).duplicate(true) if typeof(replay.get("finish_placements", {})) == TYPE_DICTIONARY else {}
	replay_saved_eliminations = (replay.get("eliminations", {}) as Dictionary).duplicate(true) if typeof(replay.get("eliminations", {})) == TYPE_DICTIONARY else {}
	replay_start_grid_slots = PackedInt32Array()
	replay_start_grid_slots = replay_playback_source_metadata.start_grid_slots
	replay_playback_focus_index = 0
	replay_timeline_controller.replay_input_display_frame_inputs = {}
	replay_playback_local_player_id = int(replay_playback_racer_ids[0])
	replay_playback_use_multiplayer_startup = replay_playback_source_metadata.source == "server" or replay_playback_source_metadata.mode == "Multiplayer"
	replay_playback_use_singleplayer_tick = replay_playback_source_metadata.source == "singleplayer"
	replay_playback_paused = false
	replay_playback_rate = 1.0
	replay_seek_checkpoints.clear()
	replay_timeline_controller.collecting_markers = false
	replay_seeking_active = false
	replay_normal_playback_tick_active = false
	replay_camera_controller.mode = ReplayCameraController.CAMERA_GAME
	game_manager.singleplayer_mode = true
	game_manager._singleplayer_tick = 0
	game_manager.network_manager.reset_race_state()
	game_manager.network_manager.set_spawn_seed(replay_playback_source_metadata.spawn_seed)
	game_manager.network_manager.load_race_metadata_dictionary(replay_playback_source_metadata.to_dictionary().get("race_options", {}))
	game_manager.network_manager.player_ids.clear()
	game_manager.network_manager.lobby_settings.cpu_player_ids.clear()
	for i in range(replay_playback_racer_ids.size()):
		var id := int(replay_playback_racer_ids[i])
		var is_cpu := i < replay_playback_cpu_flags.size() and bool(replay_playback_cpu_flags[i])
		if is_cpu:
			game_manager.network_manager.lobby_settings.cpu_player_ids.append(id)
		else:
			game_manager.network_manager.player_ids.append(id)
		if i < (settings as Array).size() and typeof(settings[i]) == TYPE_DICTIONARY:
			game_manager.network_manager.lobby_settings.set_player_settings(id, settings[i], is_cpu)
	var profile_setup_us := Time.get_ticks_usec() - profile_start_us - profile_load_us - profile_validate_us - profile_frames_duplicate_us
	var profile_race_start_us := Time.get_ticks_usec()
	game_manager._close_settings_menus_for_race_start()
	game_manager.race_dnf_low_speed_ticks.clear()
	var race_roster: MxtRaceRoster = game_manager.replay_recorder.race_roster_from_settings(
		settings as Array, replay_playback_racer_ids, replay_playback_cpu_flags)
	if race_roster == null:
		return
	race_session_controller.start_race(track_index, race_roster, game_manager.singleplayer_mode, game_manager.headless_mode)
	game_manager.game_sim.set_sim_started(true)
	profile_race_start_us = Time.get_ticks_usec() - profile_race_start_us
	game_manager.get_node("Control").visible = false
	game_manager.lobby_control.visible = false
	game_manager.replay_catalog_controller.close()
	replay_camera_controller.apply_focus_to_local_visual()
	replay_timeline_controller.refresh_input_display()
	var profile_timeline_us := Time.get_ticks_usec()
	replay_timeline_controller.initialize_markers()
	profile_timeline_us = Time.get_ticks_usec() - profile_timeline_us
	var profile_bake_us := 0
	if !replay_skip_seek_bake_requested:
		var profile_bake_start_us := Time.get_ticks_usec()
		_bake_replay_seek_checkpoints()
		profile_bake_us = Time.get_ticks_usec() - profile_bake_start_us
	else:
		_capture_replay_seek_checkpoint(0)
	_apply_replay_playback_clock()
	replay_camera_controller.apply_mode()
	if replay_load_profile_requested:
		var total_load_us := Time.get_ticks_usec() - profile_start_us
		print("MXT_REPLAY_LOAD_PROFILE path=", path,
			" total_us=", total_load_us,
			" file_parse_us=", profile_load_us,
			" validate_us=", profile_validate_us,
			" frames_duplicate_us=", profile_frames_duplicate_us,
			" setup_us=", profile_setup_us,
			" race_start_us=", profile_race_start_us,
			" timeline_us=", profile_timeline_us,
			" bake_us=", profile_bake_us,
			" frames=", _playback_frame_count(),
			" racers=", replay_playback_racer_ids.size(),
			" skip_bake=", replay_skip_seek_bake_requested)
	print("MXT_REPLAY playback started ", path, " frames=", _playback_frame_count())
	if game_manager.headless_mode:
		var replay_fast_forward_start_us := Time.get_ticks_usec()
		while replay_playback_active and replay_playback_index < _playback_frame_count():
			if !simulate_playback(false, false, false):
				get_tree().quit(1)
				return
			if replay_strict_verify_requested:
				game_manager._check_race_finished()
		var replay_fast_forward_elapsed_us := Time.get_ticks_usec() - replay_fast_forward_start_us
		var replay_frame_count := _playback_frame_count()
		print("MXT_REPLAY playback complete ", replay_playback_loaded_path,
			" frames=", replay_frame_count,
			" avg_tick_us=", int(float(replay_fast_forward_elapsed_us) / float(maxi(replay_frame_count, 1))))
		if replay_strict_verify_requested:
			var strict_replay_ok := _verify_replay_playback_results()
			if !strict_replay_ok:
				print("MXT_REPLAY_VERIFY_FAIL path=", replay_playback_loaded_path, " frames=", replay_frame_count)
				get_tree().quit(1)
				return
		print("MXT_REPLAY_VERIFY_OK path=", replay_playback_loaded_path, " frames=", replay_frame_count)
		if replay_leaderboard_verify_requested:
			var leaderboard_result := _leaderboard_verified_result()
			if !bool(leaderboard_result.get("valid", false)):
				print("MXT_LEADERBOARD_VERIFY_FAIL ", JSON.stringify(leaderboard_result))
				get_tree().quit(1)
				return
			print("MXT_LEADERBOARD_VERIFY_RESULT ", JSON.stringify(leaderboard_result))
		get_tree().quit()

func _capture_replay_seek_checkpoint(next_tick: int) -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_full_state_data"):
		return
	for checkpoint in replay_seek_checkpoints:
		if int((checkpoint as Dictionary).get("tick", -1)) == next_tick:
			return
	var state: PackedByteArray = game_manager.game_sim.get_full_state_data(next_tick)
	if state.is_empty():
		return
	replay_seek_checkpoints.append({
		"tick": next_tick,
		"index": replay_playback_index,
		"state": state,
		"finish_times": game_manager.network_manager.race_results.player_finish_times.duplicate(true),
		"finish_placements": game_manager.network_manager.race_results.player_finish_placements.duplicate(true),
		"eliminations": game_manager.network_manager.race_results.player_eliminations.duplicate(true),
	})
	replay_seek_checkpoint_bytes += state.size()

func _find_replay_seek_checkpoint(target_tick: int) -> Dictionary:
	var best: Dictionary = {}
	var best_tick := -1
	for checkpoint_value in replay_seek_checkpoints:
		if typeof(checkpoint_value) != TYPE_DICTIONARY:
			continue
		var checkpoint: Dictionary = checkpoint_value
		var checkpoint_tick := int(checkpoint.get("tick", -1))
		if checkpoint_tick <= target_tick and checkpoint_tick > best_tick:
			best = checkpoint
			best_tick = checkpoint_tick
	return best

func _restore_replay_race_event_state(checkpoint: Dictionary) -> void:
	game_manager.network_manager.race_results.player_finish_times = (checkpoint.get("finish_times", {}) as Dictionary).duplicate(true)
	game_manager.network_manager.race_results.player_finish_placements = (checkpoint.get("finish_placements", {}) as Dictionary).duplicate(true)
	game_manager.network_manager.race_results.player_eliminations = (checkpoint.get("eliminations", {}) as Dictionary).duplicate(true)
	game_manager.network_manager.race_results.rebuild_finish_order()
	game_manager.network_manager.race_results.net_race_finish_time = -1

func _reset_replay_netcode_session() -> void:
	if !replay_playback_active:
		return
	game_manager.network_manager.input_transport.netcode_session.configure(
		replay_playback_racer_ids,
		replay_playback_cpu_flags,
		game_manager._local_player_id()
	)

func _bake_replay_seek_checkpoints() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_full_state_data") or !game_manager.game_sim.has_method("load_full_state_data"):
		return
	replay_seeking_active = true
	replay_timeline_controller.collecting_markers = true
	replay_timeline_controller.initialize_markers()
	replay_timeline_controller.capture_simulation_markers()
	_capture_replay_seek_checkpoint(0)
	while replay_playback_index < _playback_frame_count():
		if !simulate_playback(false, false, false):
			break
		if (game_manager._singleplayer_tick % REPLAY_SEEK_CHECKPOINT_INTERVAL) == 0:
			_capture_replay_seek_checkpoint(game_manager._singleplayer_tick)
	_capture_replay_seek_checkpoint(game_manager._singleplayer_tick)
	replay_timeline_controller.collecting_markers = false
	_seek_replay_to_tick(0, false)
	replay_seeking_active = false

func _seek_replay_to_tick(target_tick: int, show_notice: bool = true) -> bool:
	if !replay_playback_active or game_manager.game_sim == null or !game_manager.game_sim.has_method("load_full_state_data"):
		return false
	target_tick = clampi(target_tick, 0, _playback_frame_count())
	var checkpoint := _find_replay_seek_checkpoint(target_tick)
	if checkpoint.is_empty():
		return false
	var checkpoint_tick := int(checkpoint.get("tick", 0))
	var state: PackedByteArray = checkpoint.get("state", PackedByteArray())
	if state.is_empty() or !game_manager.game_sim.load_full_state_data(checkpoint_tick, state):
		push_warning("Replay seek failed: could not load full checkpoint at tick %d" % checkpoint_tick)
		return false
	_restore_replay_race_event_state(checkpoint)
	_reset_replay_netcode_session()
	game_manager._singleplayer_tick = checkpoint_tick
	replay_playback_index = int(checkpoint.get("index", checkpoint_tick))
	replay_timeline_controller.replay_input_display_frame_inputs = {}
	replay_seeking_active = true
	while game_manager._singleplayer_tick < target_tick and replay_playback_index < _playback_frame_count():
		if !simulate_playback(false, false, false):
			break
	replay_seeking_active = false
	replay_timeline_controller.refresh_input_display()
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	replay_camera_controller.apply_focus_to_local_visual()
	if game_manager.game_sim.sim_started:
		game_manager._update_native_render_camera()
		game_manager.game_sim.render_gamesim()
	if show_notice:
		race_presentation_controller.show_notification(
			"Replay: %s" % replay_timeline_controller.format_time(
				game_manager._singleplayer_tick), 900)
	return true

func should_enqueue_replay_race_notification() -> bool:
	return !replay_playback_active or replay_normal_playback_tick_active

func simulate_playback(handle_terminal_state: bool = true, respect_pause: bool = true, enqueue_event_notifications: bool = true) -> bool:
	if respect_pause and replay_playback_paused:
		return true
	if replay_playback_index >= _playback_frame_count():
		if handle_terminal_state:
			print("MXT_REPLAY playback complete ", replay_playback_loaded_path)
			if game_manager.headless_mode:
				get_tree().quit()
			else:
				replay_playback_paused = true
				_apply_replay_playback_clock()
				replay_timeline_controller.update()
		return false
	var frame := _decoded_replay_frame_at(replay_playback_index)
	if frame.is_empty():
		if handle_terminal_state:
			if game_manager.headless_mode:
				get_tree().quit(1)
			else:
				game_manager._return_to_menu()
		return false
	var frame_tick := int(frame.get("tick", replay_playback_index))
	if frame_tick != game_manager._singleplayer_tick:
		push_warning("Replay playback refused: expected tick %d, found saved tick %d" % [game_manager._singleplayer_tick, frame_tick])
		if handle_terminal_state:
			if game_manager.headless_mode:
				get_tree().quit(1)
			else:
				game_manager._return_to_menu()
		return false
	var frame_inputs: Dictionary = frame.get("inputs", {})
	replay_timeline_controller.replay_input_display_frame_inputs = frame_inputs
	if !replay_seeking_active and !replay_timeline_controller.collecting_markers:
		replay_timeline_controller.refresh_input_display()
	replay_normal_playback_tick_active = enqueue_event_notifications
	if replay_playback_use_singleplayer_tick:
		var local_id := game_manager._local_player_id()
		var local_input: PackedByteArray = frame_inputs.get(local_id, game_manager.network_manager.input_transport.NEUTRAL_INPUT_BYTES)
		game_manager.game_sim.tick_singleplayer(local_id, local_input)
	else:
		for id_value in frame_inputs.keys():
			game_manager.network_manager.input_transport.netcode_session.store_pending_input(game_manager._singleplayer_tick, int(id_value), frame_inputs[id_value])
		if !game_manager.network_manager.input_transport.netcode_session.tick_server_frame(game_manager.game_sim, game_manager._singleplayer_tick, true):
			replay_normal_playback_tick_active = false
			push_warning("Replay playback failed at tick %d" % game_manager._singleplayer_tick)
			if handle_terminal_state:
				if game_manager.headless_mode:
					get_tree().quit(1)
				else:
					game_manager._return_to_menu()
			return false
	game_manager._consume_authoritative_race_events()
	if replay_timeline_controller.collecting_markers:
		replay_timeline_controller.capture_simulation_markers()
	replay_playback_index += 1
	game_manager._singleplayer_tick += 1
	game_manager.network_manager.input_transport.clients_server_tick = game_manager._singleplayer_tick
	game_manager._check_race_finished()
	replay_normal_playback_tick_active = false
	if !replay_seeking_active and (game_manager._singleplayer_tick % REPLAY_SEEK_CHECKPOINT_INTERVAL) == 0:
		_capture_replay_seek_checkpoint(game_manager._singleplayer_tick)
	return true
