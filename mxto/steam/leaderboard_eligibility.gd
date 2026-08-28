class_name LeaderboardEligibility extends RefCounted

const TimeAttackRulesClass = preload("res://steam/time_attack_rules.gd")

static func reject(reason: String) -> Dictionary:
	return {"eligible": false, "reason": reason}

static func evaluate_start(game_manager: GameManager, configuration: MxtRaceConfiguration, track_evidence: MxtTrackContentEvidence, settings: PlayerSettings) -> Dictionary:
	if !TimeAttackRulesClass.configuration_matches(configuration):
		return reject("modified_time_attack_rules")
	if game_manager.debug_runtime_controller.auto_accelerate or game_manager.auto_bumpers_mode or game_manager.debug_runtime_controller.bumper_smoke_enabled or game_manager.debug_runtime_controller.rail_trace_enabled:
		return reject("debug_or_automation_enabled")
	if track_evidence == null or track_evidence.count() != 1:
		return reject("invalid_track_identity")
	var track_content_id := track_evidence.get_content_id(0)
	var track_gameplay_digest := track_evidence.get_gameplay_digest(0)
	var track_record: Dictionary = game_manager.vehicle_content_controller.content_catalog.resolve_content(track_content_id)
	var board := TimeAttackRulesClass.board_for_track_digest(track_gameplay_digest)
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
		"track_gameplay_digest": track_gameplay_digest,
		"vehicle_gameplay_digest": settings.vehicle_gameplay_digest,
		"vehicle_content_id": settings.vehicle_content_id,
		"track_title": TimeAttackRulesClass.board_title(board),
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
