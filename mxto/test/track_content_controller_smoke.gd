extends SceneTree

func _fail(message: String) -> void:
	push_error("MXT_TRACK_CONTENT_CONTROLLER_SMOKE_FAIL " + message)
	quit(1)

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	var content := game_manager.track_content_controller as TrackContentController
	if content.tracks.is_empty():
		_fail("catalog is empty")
		return
	var first_id := content.track_id_for_index(0)
	if !first_id.begins_with("mxt:track:official:"):
		_fail("first track has invalid content identity")
		return
	var first_digest := content.track_gameplay_digest_for_index(0)
	if !first_digest.begins_with("sha256:") or first_digest.length() != 71:
		_fail("first track has invalid gameplay digest")
		return
	if content.track_index_for_id(first_id) < 0:
		_fail("content identity index did not resolve")
		return
	if content.track_name_for_id(first_id) == "Missing Track":
		_fail("content identity did not resolve a name")
		return
	if !content.prepare_race(0):
		_fail("could not prepare first track")
		return
	if content.current_track_index != 0 or content.current_metadata.is_empty():
		_fail("current track metadata was not loaded")
		return
	if content.current_track_dir.is_empty():
		_fail("current track directory was not retained")
		return
	if content.current_visual_path.is_empty():
		_fail("current track visual was not resolved")
		return
	content.load_runtime_visuals()
	if content.visual_scene_instance == null and game_manager.debug_track_mesh.mesh == null:
		_fail("current track visual was not loaded")
		return
	content.teardown_runtime()
	if content.current_track_index != -1 or !content.current_metadata.is_empty():
		_fail("runtime teardown did not clear owned state")
		return
	if game_manager.world_environment.environment != game_manager.default_world_environment_resource:
		_fail("runtime teardown did not restore the built-in environment")
		return
	print("MXT_TRACK_CONTENT_CONTROLLER_SMOKE_OK tracks=", content.tracks.size(), " first_id=", first_id)
	game_manager.queue_free()
	await process_frame
	quit(0)
