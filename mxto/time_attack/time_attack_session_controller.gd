class_name TimeAttackSessionController
extends Node

signal race_again_requested(
	configuration: MxtRaceConfiguration,
	race_state: Dictionary,
	track_evidence: MxtTrackContentEvidence)
signal leaderboard_requested(board_name: String)
signal main_menu_requested

const LeaderboardEligibility = preload("res://leaderboards/leaderboard_eligibility.gd")
const TimeAttackRules = preload("res://leaderboards/time_attack_rules.gd")

var game_manager: GameManager
var network_manager: NetworkManager
var replay_controller: ReplayController
var presentation_controller: RacePresentationController
var practice_controller: PracticeController
var leaderboard_client: LeaderboardClient
var steam_service: MxtSteamService

var eligibility: Dictionary = {}
var finalized := false
var previous_best_milliseconds := 0
var last_replay_path := ""
var rank_refresh_board := ""
var rank_refresh_global := ""


func initialize(
		in_game_manager: GameManager,
		in_network_manager: NetworkManager,
		in_replay_controller: ReplayController,
		in_presentation_controller: RacePresentationController,
		in_practice_controller: PracticeController,
		in_leaderboard_client: LeaderboardClient,
		in_steam_service: MxtSteamService) -> void:
	game_manager = in_game_manager
	network_manager = in_network_manager
	replay_controller = in_replay_controller
	presentation_controller = in_presentation_controller
	practice_controller = in_practice_controller
	leaderboard_client = in_leaderboard_client
	steam_service = in_steam_service
	leaderboard_client.submission_status_changed.connect(_on_submission_status_changed)
	leaderboard_client.submission_completed.connect(_on_submission_completed)
	leaderboard_client.entries_received.connect(_on_rank_entries_received)
	var overlay := presentation_controller.results_overlay
	overlay.time_attack_race_again_requested.connect(_on_race_again_requested)
	overlay.time_attack_save_replay_requested.connect(_save_replay)
	overlay.time_attack_watch_replay_requested.connect(_watch_replay)
	overlay.time_attack_leaderboard_requested.connect(
		func(board_name: String): leaderboard_requested.emit(board_name))
	overlay.time_attack_main_menu_requested.connect(func(): main_menu_requested.emit())


func set_previous_best(milliseconds: int) -> void:
	previous_best_milliseconds = maxi(milliseconds, 0)


func begin_run(
		configuration: MxtRaceConfiguration,
		track_evidence: MxtTrackContentEvidence,
		player_settings: PlayerSettings) -> void:
	last_replay_path = ""
	finalized = false
	if configuration.is_time_attack():
		eligibility = LeaderboardEligibility.evaluate_start(
			game_manager, configuration, track_evidence, player_settings)
		configuration.leaderboard_eligible = bool(eligibility.get("eligible", false))
		configuration.leaderboard_ineligible_reason = String(eligibility.get("reason", ""))
	else:
		eligibility.clear()


func reset() -> void:
	eligibility.clear()
	finalized = false
	previous_best_milliseconds = 0
	last_replay_path = ""
	rank_refresh_board = ""
	rank_refresh_global = ""


func finalize_ranked(local_player_id: int) -> void:
	if finalized or !network_manager.race_configuration.is_time_attack():
		return
	finalized = true
	var finish_tick := int(network_manager.race_results.player_finish_times.get(local_player_id, -1))
	var start_tick := presentation_controller.race_results_start_tick()
	presentation_controller.update_time_attack_submission_status("Preparing verification replay…")
	last_replay_path = replay_controller.stage_completed_time_attack_replay(true)
	eligibility = LeaderboardEligibility.finalize(
		eligibility, finish_tick, start_tick, last_replay_path)
	var board: Dictionary = eligibility.get("board", {})
	eligibility["board_name"] = String(board.get("steam_name", ""))
	eligibility["friendly_reason"] = TimeAttackRules.friendly_reason(
		String(eligibility.get("reason", "")))
	eligibility["replay_can_save"] = replay_controller.can_save_staged_replay_locally(last_replay_path)
	presentation_controller.show_time_attack_result(
		eligibility, previous_best_milliseconds, "Preparing trusted submission…")
	if bool(eligibility.get("eligible", false)):
		if leaderboard_client.enqueue_submission(eligibility):
			presentation_controller.show_notification("Time Attack queued for trusted verification", 5000)
			presentation_controller.update_time_attack_submission_status(
				"%s The queue is persisted; it is safe to close the game." % String(
					leaderboard_client.status().get("message", "Queued for verification.")))
		else:
			presentation_controller.show_notification("Time Attack replay could not be queued", 5000)
			presentation_controller.update_time_attack_submission_status(
				"The verification replay could not be added to the submission queue.")
	else:
		var reason := TimeAttackRules.friendly_reason(String(eligibility.get("reason", "ineligible")))
		presentation_controller.show_notification("Unranked: %s" % reason, 5000)
		presentation_controller.update_time_attack_submission_status("Unranked — %s" % reason)


func finalize_practice(local_player_id: int) -> void:
	if !practice_controller.session_active or practice_controller.session_completed:
		return
	practice_controller.mark_completed()
	var finish_tick := int(network_manager.race_results.player_finish_times.get(local_player_id, -1))
	var start_tick := presentation_controller.race_results_start_tick()
	last_replay_path = replay_controller.stage_completed_time_attack_replay(false)
	var practice_score := TimeAttackRules.finish_ticks_to_milliseconds(finish_tick, start_tick)
	var practice_board := {}
	if network_manager.race_track_evidence.count() == 1:
		practice_board = TimeAttackRules.board_for_track_digest(
			network_manager.race_track_evidence.get_gameplay_digest(0))
	var practice_result := {
		"eligible": false,
		"reason": "practice_unranked",
		"friendly_reason": "Practice Unranked",
		"score_milliseconds": practice_score,
		"board_name": String(practice_board.get("steam_name", "")),
		"replay_path": last_replay_path,
		"replay_can_save": replay_controller.can_save_staged_replay_locally(last_replay_path),
	}
	presentation_controller.show_time_attack_result(
		practice_result, 0,
		"Practice result only — use Save Replay to keep it locally.")


func _on_submission_status_changed(status: Dictionary) -> void:
	var message := String(status.get("message", ""))
	if int(status.get("pending_count", 0)) > 0:
		message += " The queue is persisted; it is safe to close the game."
	presentation_controller.update_time_attack_submission_status(message)


func _on_submission_completed(result: Dictionary) -> void:
	if String(result.get("replay_path", "")) != last_replay_path:
		return
	var message := "Verified replay archived as your vehicle best." if bool(result.get("is_vehicle_best", false)) else "Verified replay archived; your existing best is faster."
	var rank := int(result.get("global_rank", 0))
	if rank > 0:
		message += " Global rank #%d." % rank
	presentation_controller.update_time_attack_submission_status(message)
	var board_name := String(result.get("board_name", ""))
	if !board_name.is_empty():
		rank_refresh_board = board_name
		rank_refresh_global = ""
		leaderboard_client.request_entries(board_name, "around_user")


func _on_rank_entries_received(
		board_name: String,
		request_type: String,
		response: MxtLeaderboardQueryResult) -> void:
	if board_name != rank_refresh_board or !response.is_ok():
		return
	if request_type == "around_user":
		var local_steam_id := steam_service.get_steam_id()
		for index in response.get_entry_count():
			var entry := response.get_entry(index)
			if entry != null and entry.steam_id == local_steam_id:
				rank_refresh_global = "Global rank #%d" % entry.rank
				break
	if !rank_refresh_global.is_empty():
		presentation_controller.update_time_attack_submission_status(
			"Verified replay archived. %s." % rank_refresh_global)


func _on_race_again_requested() -> void:
	var practice := network_manager.race_configuration.is_practice()
	var configuration := (
		network_manager.race_configuration.copy()
		if practice else TimeAttackRules.build_configuration())
	var race_state := network_manager.race_state.duplicate(true) if practice else {}
	var track_evidence := network_manager.race_track_evidence.copy() if practice else null
	race_again_requested.emit(configuration, race_state, track_evidence)


func _save_replay() -> void:
	var saved_path := replay_controller.save_staged_replay_locally(last_replay_path)
	if saved_path.is_empty():
		presentation_controller.show_notification("Replay could not be saved", 3000)
		return
	presentation_controller.results_overlay.set_time_attack_replay_saved(saved_path)
	presentation_controller.show_notification("Replay Saved", 2200)


func _watch_replay() -> void:
	if !last_replay_path.is_empty():
		replay_controller.call_deferred("play_replay_file", last_replay_path)
