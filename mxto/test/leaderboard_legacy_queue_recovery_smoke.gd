extends SceneTree

const LeaderboardClientClass = preload("res://leaderboards/leaderboard_client.gd")


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var replay_root := "user://leaderboard_submission_replays"
	var replay_path := replay_root.path_join("legacy_queue_recovery_smoke.replay.json")
	if DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(replay_root)) != OK:
		_fail("could not create replay directory")
		return
	var replay_file := FileAccess.open(replay_path, FileAccess.WRITE)
	if replay_file == null:
		_fail("could not create replay fixture")
		return
	replay_file.store_string("{}")
	replay_file.close()

	var submission := {
		"board_name": "mxt_ta_test_11111111_r2",
		"score_milliseconds": 55000,
		"track_gameplay_digest": "sha256:" + "11".repeat(32),
		"vehicle_gameplay_digest": "sha256:" + "22".repeat(32),
		"ruleset_revision": 2,
		"replay_path": replay_path,
		"attempts": 7,
		"last_error": "old result",
		"next_retry_unix": 1234,
	}
	var client: LeaderboardClient = LeaderboardClientClass.new()
	client._merge_queue_document({"pending": [], "rejected": [], "completed": [submission]}, true)
	if client.pending.size() != 1 or !client.completed.is_empty():
		_cleanup_and_fail(replay_path, "recoverable legacy completion was not promoted")
		return
	var recovered: Dictionary = client.pending[0]
	if int(recovered.get("attempts", -1)) != 0 or !String(recovered.get("last_error", "x")).is_empty() \
			or int(recovered.get("next_retry_unix", -1)) != 0 \
			or String(recovered.get("recovery_source", "")) != "legacy_completed_submission":
		_cleanup_and_fail(replay_path, "recovered submission retained stale retry state")
		return

	client.pending.clear()
	client.completed.clear()
	client._merge_queue_document({"pending": [], "rejected": [], "completed": [submission]}, false)
	if !client.pending.is_empty() or client.completed.size() != 1:
		_cleanup_and_fail(replay_path, "current completed history was incorrectly requeued")
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(replay_path))
	print("MXT_LEADERBOARD_LEGACY_QUEUE_RECOVERY_SMOKE_OK")
	quit()


func _cleanup_and_fail(replay_path: String, message: String) -> void:
	DirAccess.remove_absolute(ProjectSettings.globalize_path(replay_path))
	_fail(message)


func _fail(message: String) -> void:
	push_error("MXT_LEADERBOARD_LEGACY_QUEUE_RECOVERY_SMOKE_FAIL " + message)
	quit(1)
