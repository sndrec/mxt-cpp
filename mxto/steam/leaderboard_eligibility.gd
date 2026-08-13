class_name LeaderboardEligibility extends RefCounted

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")

static func reject(reason: String) -> Dictionary:
	return {"eligible": false, "reason": reason}

static func evaluate_start(game_manager: GameManager, options: Dictionary, settings: PlayerSettings) -> Dictionary:
	if !TimeAttackRulesClass.options_match(options):
		return reject("modified_time_attack_rules")
	if game_manager.debug_runtime_controller.auto_accelerate or game_manager.auto_bumpers_mode or game_manager.debug_runtime_controller.bumper_smoke_enabled or game_manager.debug_runtime_controller.rail_trace_enabled:
		return reject("debug_or_automation_enabled")
	var track_ids: Array = options.get("track_ids", [])
	var track_digests: Array = options.get("track_gameplay_digests", [])
	if track_ids.size() != 1 or track_digests.size() != 1:
		return reject("invalid_track_identity")
	var track_record: Dictionary = game_manager.vehicle_content_controller.content_catalog.resolve_content(String(track_ids[0]))
	var board := TimeAttackRulesClass.board_for_track_digest(String(track_digests[0]))
	if board.is_empty():
		return reject("track_has_no_ranked_board")
	if !TimeAttackRulesClass.track_record_matches_board(track_record, board):
		return reject("uncurated_or_mismatched_track")
	var vehicle_record: Dictionary = game_manager.vehicle_content_controller.content_catalog.resolve_content(settings.vehicle_content_id)
	if String(vehicle_record.get("source", "")) != "official" or String(vehicle_record.get("gameplay_digest", "")) != settings.vehicle_gameplay_digest:
		return reject("unofficial_or_mismatched_vehicle")
	return {
		"eligible": true,
		"reason": "",
		"board": board,
		"ruleset_revision": TimeAttackRulesClass.RULESET_REVISION,
		"track_gameplay_digest": String(track_digests[0]),
		"vehicle_gameplay_digest": settings.vehicle_gameplay_digest,
	}

static func finalize(start_result: Dictionary, finish_tick: int, start_tick: int, replay_path: String) -> Dictionary:
	if !bool(start_result.get("eligible", false)):
		return start_result.duplicate(true)
	if finish_tick <= start_tick:
		return reject("invalid_finish_time")
	if replay_path.is_empty() or !FileAccess.file_exists(replay_path):
		return reject("replay_recording_failed")
	var result := start_result.duplicate(true)
	result["finish_tick"] = finish_tick
	result["start_tick"] = start_tick
	result["score_milliseconds"] = TimeAttackRulesClass.finish_ticks_to_milliseconds(finish_tick, start_tick)
	result["replay_path"] = replay_path
	return result
