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
	var presentation := game_manager.track_presentation_controller as TrackPresentationController
	if content.tracks.is_empty():
		_fail("catalog is empty")
		return
	var official_digests := {}
	var loose_track_count := 0
	for track_value in content.tracks:
		var track: Dictionary = track_value
		if String(track.get("source", "")) == "official":
			official_digests[String(track.get("gameplay_digest", ""))] = true
	for track_value in content.tracks:
		var track: Dictionary = track_value
		if String(track.get("source", "")) != "local_loose":
			continue
		loose_track_count += 1
		if official_digests.has(String(track.get("gameplay_digest", ""))):
			_fail("catalog retained an official gameplay digest as loose content")
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
	var first_track: Dictionary = content.tracks[0]
	var track_count_before_duplicate_scan := content.tracks.size()
	var loose_seen_paths := {}
	var loose_seen_content_ids := {}
	content._scan_loose_track_dir(String(first_track.get("dir", "")), loose_seen_paths, loose_seen_content_ids)
	if content.tracks.size() != track_count_before_duplicate_scan:
		_fail("an official track was registered again as loose content")
		return
	for track_value in content.tracks:
		var track: Dictionary = track_value
		if String(track.get("source", "")) == "local_loose" \
				and String(track.get("gameplay_digest", "")) == first_digest:
			_fail("an official gameplay digest was retained as loose content")
			return
	if !presentation.prepare_race(first_track):
		_fail("could not prepare first track")
		return
	if presentation.current_metadata.is_empty():
		_fail("current track metadata was not loaded")
		return
	if presentation.current_track_dir.is_empty():
		_fail("current track directory was not retained")
		return
	if presentation.current_visual_path.is_empty():
		_fail("current track visual was not resolved")
		return
	presentation.load_runtime_visuals()
	if presentation.visual_scene_instance == null and game_manager.debug_track_mesh.mesh == null:
		_fail("current track visual was not loaded")
		return
	var fallback_track_index := -1
	for i in range(content.tracks.size()):
		var track: Dictionary = content.tracks[i]
		if !FileAccess.file_exists(String(track.get("dir", "")).path_join("ground.png")):
			fallback_track_index = i
			break
	if fallback_track_index < 0 or !presentation.prepare_race(content.tracks[fallback_track_index]):
		_fail("could not prepare a track without ground.png")
		return
	var floor_material := game_manager.track_floor.get_active_material(0) as ShaderMaterial
	if floor_material == null or floor_material.get_shader_parameter("texture_albedo") != TrackPresentationController.DEFAULT_GROUND_TEXTURE:
		_fail("track without ground.png did not restore cityscape.png")
		return
	presentation.teardown_runtime()
	if !presentation.current_metadata.is_empty():
		_fail("runtime teardown did not clear owned state")
		return
	if game_manager.world_environment.environment != game_manager.default_world_environment_resource:
		_fail("runtime teardown did not restore the built-in environment")
		return
	print("MXT_TRACK_CONTENT_CONTROLLER_SMOKE_OK tracks=", content.tracks.size(),
		" loose=", loose_track_count, " first_id=", first_id)
	game_manager.queue_free()
	await process_frame
	quit(0)
