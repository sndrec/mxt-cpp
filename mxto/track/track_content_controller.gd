class_name TrackContentController extends Node

const VehicleContentControllerClass = preload("res://vehicle/vehicle_content_controller.gd")

const EXTERNAL_TRACKS_DIR_NAMES := ["tracks", "track"]
const OFFICIAL_TRACK_MANIFEST_PATH := "res://track/official_tracks.json"
const OFFICIAL_TRACK_MANIFEST_REVISION := 1
@onready var vehicle_content_controller: VehicleContentControllerClass = get_node("../VehicleContentController") as VehicleContentControllerClass

var tracks: Array = []
var track_id_to_index: Dictionary = {}

func scan_catalog() -> void:
	tracks.clear()
	_scan_official_tracks()
	_scan_loose_tracks()
	_scan_installed_tracks()
	track_id_to_index.clear()
	for i in range(tracks.size()):
		var track_id := String(tracks[i].get("content_id", ""))
		if track_id != "" and !track_id_to_index.has(track_id):
			track_id_to_index[track_id] = i

func command_line_track_index() -> int:
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	var track_name := _read_arg_value(args, user_args, "--track-name").to_lower()
	if track_name.is_empty():
		return -1
	for i in range(tracks.size()):
		if String(tracks[i].get("name", "")).to_lower() == track_name:
			return i
	return -1

func track_id_for_index(track_index: int) -> String:
	if track_index >= 0 and track_index < tracks.size():
		return String(tracks[track_index].get("content_id", ""))
	return ""

func track_gameplay_digest_for_index(track_index: int) -> String:
	if track_index >= 0 and track_index < tracks.size():
		return String(tracks[track_index].get("gameplay_digest", ""))
	return ""

func build_track_content_evidence(track_indices: Array) -> MxtTrackContentEvidence:
	var result := MxtTrackContentEvidence.new()
	for index_value in track_indices:
		var content_id := track_id_for_index(int(index_value))
		if content_id.is_empty():
			continue
		var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(content_id)
		if record == null:
			continue
		if !result.append(
				content_id,
				record.gameplay_digest,
				record.package_digest,
				str(record.published_file_id) if record.published_file_id > 0 else ""):
			push_error(result.get_last_error())
			break
	return result

func track_content_evidence_matches(
		content_id: String,
		gameplay_digest: String,
		package_digest: String,
		workshop_id: String) -> bool:
	var record: MxtContentRecord = vehicle_content_controller.content_catalog.resolve_content(content_id)
	if record == null or record.gameplay_digest != gameplay_digest:
		return false
	if record.package_digest != package_digest:
		return false
	if (str(record.published_file_id) if record.published_file_id > 0 else "") != workshop_id:
		return false
	return true

func track_index_for_id(track_id: String) -> int:
	if track_id_to_index.has(track_id):
		return int(track_id_to_index[track_id])
	return -1

func track_name_for_id(track_id: String) -> String:
	var track_index := track_index_for_id(track_id)
	if track_index >= 0:
		return String(tracks[track_index].get("name", "Track"))
	return "Missing Track"

func _read_arg_value(args: Array, user_args: Array, flag: String) -> String:
	var index := args.find(flag)
	var value_args := args
	if index == -1:
		index = user_args.find(flag)
		value_args = user_args
	if index == -1 or index + 1 >= value_args.size():
		return ""
	return String(value_args[index + 1])

func _external_tracks_dir_candidates() -> PackedStringArray:
	var out := PackedStringArray()
	var args := OS.get_cmdline_args()
	var user_args := OS.get_cmdline_user_args()
	var arg_dir := _read_arg_value(args, user_args, "--tracks-dir")
	if !arg_dir.is_empty():
		out.append(arg_dir)
	var executable_dir := OS.get_executable_path().get_base_dir()
	if !executable_dir.is_empty():
		for dir_name in EXTERNAL_TRACKS_DIR_NAMES:
			out.append(executable_dir.path_join(dir_name))
	var project_dir := ProjectSettings.globalize_path("res://")
	if !project_dir.is_empty():
		out.append(project_dir.path_join("tracks"))
		var repo_dir := project_dir.trim_suffix("/").trim_suffix("\\").get_base_dir()
		out.append(repo_dir.path_join("export-bin/track"))
		for dir_name in EXTERNAL_TRACKS_DIR_NAMES:
			out.append(repo_dir.path_join(dir_name))
	var cwd := OS.get_environment("PWD")
	if !cwd.is_empty():
		for dir_name in EXTERNAL_TRACKS_DIR_NAMES:
			out.append(cwd.path_join(dir_name))
	cwd = OS.get_environment("CD")
	if !cwd.is_empty():
		for dir_name in EXTERNAL_TRACKS_DIR_NAMES:
			out.append(cwd.path_join(dir_name))
	return out

func _existing_track_roots() -> PackedStringArray:
	var roots := PackedStringArray()
	var seen := {}
	for raw_dir in _external_tracks_dir_candidates():
		var dir_path := String(raw_dir).replace("\\", "/")
		if dir_path.is_empty() or !DirAccess.dir_exists_absolute(dir_path):
			continue
		var key := dir_path.to_lower()
		if seen.has(key):
			continue
		seen[key] = true
		roots.append(dir_path)
	return roots

func _scan_official_tracks() -> void:
	var manifest_value = JSON.parse_string(FileAccess.get_file_as_string(OFFICIAL_TRACK_MANIFEST_PATH))
	if typeof(manifest_value) != TYPE_DICTIONARY:
		push_error("Official track manifest is not a JSON object")
		return
	var manifest: Dictionary = manifest_value
	if int(manifest.get("format_revision", -1)) != OFFICIAL_TRACK_MANIFEST_REVISION:
		push_error("Official track manifest has an unsupported format revision")
		return
	var entries_value = manifest.get("tracks", [])
	if typeof(entries_value) != TYPE_ARRAY:
		push_error("Official track manifest tracks field is not an array")
		return
	var roots := _existing_track_roots()
	var seen_content_ids := {}
	for entry_value in entries_value:
		if typeof(entry_value) != TYPE_DICTIONARY:
			push_error("Official track manifest contains a non-object entry")
			continue
		var entry: Dictionary = entry_value
		var slug := String(entry.get("slug", ""))
		var content_id := "mxt:track:official:" + slug
		if slug.is_empty() or seen_content_ids.has(content_id):
			push_error("Official track manifest contains an empty or duplicate slug: %s" % slug)
			continue
		seen_content_ids[content_id] = true
		_register_official_track(entry, roots)

func _register_official_track(entry: Dictionary, roots: PackedStringArray) -> void:
	var directory := String(entry.get("directory", ""))
	var mxt_file := String(entry.get("mxt_file", ""))
	var metadata_file := String(entry.get("metadata_file", ""))
	var title := String(entry.get("title", ""))
	var slug := String(entry.get("slug", ""))
	var expected_digest := String(entry.get("gameplay_digest", ""))
	for root in roots:
		var track_dir := String(root).path_join(directory)
		var mxt_path := track_dir.path_join(mxt_file)
		var metadata_path := track_dir.path_join(metadata_file)
		if !FileAccess.file_exists(mxt_path) or !FileAccess.file_exists(metadata_path):
			continue
		var metadata_value = JSON.parse_string(FileAccess.get_file_as_string(metadata_path))
		if typeof(metadata_value) != TYPE_DICTIONARY:
			push_error("Official track metadata is invalid JSON: %s" % metadata_path)
			return
		var metadata: Dictionary = metadata_value
		var visual_path := _resolve_visual_path(track_dir, metadata, mxt_path)
		if visual_path.is_empty():
			push_error("Official track visual is missing: %s" % track_dir)
			return
		var result: MxtContentLoadResult = vehicle_content_controller.content_catalog.add_official_track(
			slug,
			title,
			mxt_path,
			visual_path,
			metadata_path,
			expected_digest)
		if !result.is_valid():
			push_error("Official track catalog registration failed for %s: %s" % [slug, str(result.errors)])
			return
		var record := result.record
		if record == null:
			push_error("Official track catalog registration returned no content record for %s" % slug)
			return
		tracks.append({
			"content_id": record.content_id,
			"gameplay_digest": record.gameplay_digest,
			"name": title,
			"mxt": mxt_path,
			"json": metadata_path,
			"dir": track_dir,
			"visual": visual_path,
			"source": "official",
		})
		return

func _normalized_track_path(path: String) -> String:
	return path.simplify_path().replace("\\", "/").to_lower()

func _scan_loose_tracks() -> void:
	vehicle_content_controller.content_catalog.clear_loose_tracks()
	var seen_paths := {}
	var seen_content_ids := {}
	for track_value in tracks:
		var track: Dictionary = track_value
		seen_paths[_normalized_track_path(String(track.get("mxt", "")))] = true
	for root in _existing_track_roots():
		_scan_loose_track_dir(String(root), seen_paths, seen_content_ids)

func _scan_loose_track_dir(path: String, seen_paths: Dictionary, seen_content_ids: Dictionary) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var entry := directory.get_next()
	while !entry.is_empty():
		if directory.current_is_dir():
			if !directory.is_link(entry) and !entry.begins_with("."):
				_scan_loose_track_dir(path.path_join(entry), seen_paths, seen_content_ids)
		elif entry.get_extension().to_lower() == "json":
			_register_loose_track(path.path_join(entry), seen_paths, seen_content_ids)
		entry = directory.get_next()
	directory.list_dir_end()

func _register_loose_track(json_path: String, seen_paths: Dictionary, seen_content_ids: Dictionary) -> void:
	var mxt_path := json_path.get_basename() + ".mxt_track"
	if !FileAccess.file_exists(mxt_path):
		return
	var normalized_path := _normalized_track_path(mxt_path)
	if seen_paths.has(normalized_path):
		return
	seen_paths[normalized_path] = true
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(json_path))
	if typeof(parsed) != TYPE_DICTIONARY or !parsed.has("name"):
		push_warning("Skipped loose track with invalid metadata: %s" % json_path)
		return
	var metadata: Dictionary = parsed
	var title := String(metadata.get("name", "")).strip_edges()
	var visual_path := _resolve_visual_path(json_path.get_base_dir(), metadata, mxt_path)
	var result: MxtContentLoadResult = vehicle_content_controller.content_catalog.add_loose_track(
		title,
		mxt_path,
		visual_path,
		json_path)
	if !result.is_valid():
		push_warning("Skipped loose track %s: %s" % [json_path.get_base_dir(), str(result.errors)])
		return
	var record := result.record
	if record == null or record.source != MxtContentRecord.SOURCE_LOCAL_LOOSE:
		return
	var content_id := record.content_id
	if content_id.is_empty() or seen_content_ids.has(content_id):
		return
	seen_content_ids[content_id] = true
	tracks.append({
		"content_id": content_id,
		"gameplay_digest": record.gameplay_digest,
		"name": title,
		"mxt": mxt_path,
		"json": json_path,
		"dir": json_path.get_base_dir(),
		"visual": visual_path,
		"source": "local_loose",
	})

func _scan_installed_tracks() -> void:
	for record_value in vehicle_content_controller.content_catalog.get_records("track"):
		var record := record_value as MxtContentRecord
		if record == null or record.source in [MxtContentRecord.SOURCE_OFFICIAL, MxtContentRecord.SOURCE_LOCAL_LOOSE]:
			continue
		tracks.append({
			"content_id": record.content_id,
			"gameplay_digest": record.gameplay_digest,
			"name": record.title,
			"mxt": record.authoritative_path,
			"json": record.metadata_path,
			"dir": record.root_path,
			"visual": record.visual_path,
			"source": record.source_name,
		})

func _resolve_local_path(track_dir: String, path_value) -> String:
	var path := String(path_value).strip_edges()
	if path.is_empty():
		return ""
	if path.begins_with("res://") or path.begins_with("user://"):
		return path
	if path.is_absolute_path():
		return path.replace("\\", "/")
	return track_dir.path_join(path).replace("\\", "/")

func _runtime_file_exists(path: String) -> bool:
	if path.is_empty():
		return false
	return ResourceLoader.exists(path) or FileAccess.file_exists(path)

func _resolve_visual_path(track_dir: String, metadata: Dictionary, mxt_path: String) -> String:
	var explicit_path := _resolve_local_path(track_dir, metadata.get("visual_scene", ""))
	if _runtime_file_exists(explicit_path):
		return explicit_path
	var base_names := PackedStringArray(["track"])
	if !mxt_path.is_empty():
		var mxt_base_name := mxt_path.get_basename().get_file()
		if !base_names.has(mxt_base_name):
			base_names.append(mxt_base_name)
	for base_name in base_names:
		for extension in ["tscn", "scn", "glb", "gltf"]:
			var candidate := track_dir.path_join("%s.%s" % [base_name, extension])
			if _runtime_file_exists(candidate):
				return candidate
	return ""
