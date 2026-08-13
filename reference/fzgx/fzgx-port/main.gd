extends Node3D

const STATUS_OK := 0
const STRAFE_MOD_TRIGGER_THRESHOLD := 0.01
const CHECKPOINT_DEBUG_DRAW_COUNT := 24
const CHECKPOINT_DEBUG_TEXT_COUNT := 8
const CHECKPOINT_DEBUG_MAX_DISTANCE := 1600.0
const CHECKPOINT_DEBUG_TEXT_FONT_SIZE := 11
const CHECKPOINT_DEBUG_TEXT_SCREEN_MARGIN := 16.0
const CHECKPOINT_DEBUG_NORMAL_TICK := 12.0

var bridge
var session_active := false

var track_selector: OptionButton
var random_seed_edit: LineEdit
var machine_selector: OptionButton
var machine_setting_slider: HSlider
var machine_setting_value_label: Label
var start_button: Button
var generate_track_button: Button
var restart_button: Button
var copy_pose_button: Button
var save_replay_button: Button
var load_replay_button: Button
var replay_play_pause_button: Button
var replay_step_button: Button
var checkpoint_debug_checkbox: CheckBox
var status_label: Label
var replay_file_dialog: FileDialog
var track_mesh_instance: MeshInstance3D
var analytic_track_mesh_instance: MeshInstance3D
var extra_collision_root: Node3D
var checkpoint_debug_instance: MeshInstance3D
var checkpoint_debug_overlay: Control
var machine_mesh_instance: MeshInstance3D
var follow_camera: Camera3D
var machine_transform_previous: Transform3D = Transform3D.IDENTITY
var machine_transform_current: Transform3D = Transform3D.IDENTITY
var camera_transform_previous: Transform3D = Transform3D.IDENTITY
var camera_transform_current: Transform3D = Transform3D.IDENTITY
var machine_transform_valid := false
var camera_transform_valid := false
var checkpoint_debug_material: StandardMaterial3D
var checkpoint_debug_entries: Array = []
var checkpoint_debug_labels: Array = []
var checkpoint_debug_enabled := false
var latest_machine_state: Dictionary = {}
var extra_collision_instances: Array = []
var collision_debug_materials: Dictionary = {}
var replay_loaded := false
var replay_playing := false
var replay_path := ""
var replay_track_index := -1
var replay_machine_index := -1
var replay_machine_setting_percent := 0
var replay_next_frame_index := 0
var replay_frames: Array = []
var replay_camera_render_state: Dictionary = {}


func _ready() -> void:
	_build_world_nodes()
	_build_ui()
	bridge = ClassDB.instantiate("FzgxGameBridge")
	if bridge == null:
		start_button.disabled = true
		generate_track_button.disabled = true
		restart_button.disabled = true
		copy_pose_button.disabled = true
		save_replay_button.disabled = true
		checkpoint_debug_checkbox.disabled = true
		_update_replay_controls()
		_update_status("FzgxGameBridge is unavailable. Build the GDExtension first.")
		return
	_populate_dropdowns()
	_update_replay_controls()
	_update_status("Pick a track, machine, and setting, then press Start.")


func _physics_process(_delta: float) -> void:
	if Input.is_action_just_pressed("load_state"):
		_on_load_state_pressed()
		return

	if Input.is_action_just_pressed("save_state"):
		_on_save_state_pressed()

	if !session_active:
		return

	if replay_loaded:
		if replay_playing:
			_step_loaded_replay_frame()
		return

	var steer := Input.get_axis("steer_left", "steer_right")

	var accel := Input.get_action_strength("accelerate")

	var brake := Input.get_action_strength("brake")

	var strafe_left := Input.get_action_strength("strafe_left")
	var strafe_right := Input.get_action_strength("strafe_right")
	var strafe := strafe_right - strafe_left
	
	var pitch := Input.get_axis("pitch_down", "pitch_up")
	
	var buttons := 0
	if Input.is_action_pressed("sideattack"):
		buttons |= 1
	if Input.is_action_pressed("spinattack"):
		buttons |= 1 << 1
	if Input.is_action_pressed("boost"):
		buttons |= 1 << 2
	# Placeholder until the original paired-trigger threshold is recovered from the decomp data.
	if strafe_left > STRAFE_MOD_TRIGGER_THRESHOLD and strafe_right > STRAFE_MOD_TRIGGER_THRESHOLD:
		print("wants drift")
		buttons |= 1 << 3

	var view_up_pressed := Input.is_action_just_pressed("view_up")
	var view_down_pressed := Input.is_action_just_pressed("view_down")
	_sync_camera_render_state()
	var status := int(bridge.step_frame(
		steer,
		pitch,
		accel,
		brake,
		strafe,
		buttons,
		view_up_pressed,
		view_down_pressed
	))
	if status != STATUS_OK:
		session_active = false
		copy_pose_button.disabled = true
		save_replay_button.disabled = !bridge.has_replay_capture()
		_clear_checkpoint_debug_visuals()
		_update_status("step_frame failed: %d" % status)
		return

	var machine_state: Dictionary = bridge.read_machine_state()
	if !machine_state.get("active", false):
		return
	_apply_machine_state(machine_state, bridge.read_game_camera_state())
	_sync_extra_collision_debug_state()


func _process(_delta: float) -> void:
	if !session_active:
		_clear_checkpoint_debug_visuals()
		return

	_apply_interpolated_transforms(float(Engine.get_physics_interpolation_fraction()))
	_update_checkpoint_debug_visuals()


func _build_world_nodes() -> void:
	track_mesh_instance = MeshInstance3D.new()
	track_mesh_instance.name = "TrackMesh"
	add_child(track_mesh_instance)

	analytic_track_mesh_instance = MeshInstance3D.new()
	analytic_track_mesh_instance.name = "AnalyticTrackMesh"
	add_child(analytic_track_mesh_instance)

	extra_collision_root = Node3D.new()
	extra_collision_root.name = "ExtraCollisionDebug"
	add_child(extra_collision_root)

	checkpoint_debug_instance = MeshInstance3D.new()
	checkpoint_debug_instance.name = "CheckpointDebug"
	checkpoint_debug_instance.visible = false
	add_child(checkpoint_debug_instance)

	checkpoint_debug_material = StandardMaterial3D.new()
	checkpoint_debug_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	checkpoint_debug_material.vertex_color_use_as_albedo = true
	checkpoint_debug_material.no_depth_test = true
	checkpoint_debug_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	checkpoint_debug_material.albedo_color = Color(1.0, 1.0, 1.0, 1.0)

	machine_mesh_instance = MeshInstance3D.new()
	machine_mesh_instance.name = "MachineBox"
	var box_mesh := BoxMesh.new()
	box_mesh.size = Vector3(2.6, 1.2, 4.4)
	machine_mesh_instance.mesh = box_mesh
	var box_material := StandardMaterial3D.new()
	box_material.albedo_color = Color(0.92, 0.18, 0.14)
	box_material.roughness = 0.45
	machine_mesh_instance.set_surface_override_material(0, box_material)
	add_child(machine_mesh_instance)

	follow_camera = Camera3D.new()
	follow_camera.current = true
	follow_camera.fov = 90
	add_child(follow_camera)


func _build_ui() -> void:
	var canvas := CanvasLayer.new()
	add_child(canvas)

	var panel := PanelContainer.new()
	panel.position = Vector2(20.0, 20.0)
	panel.size = Vector2(420.0, 520.0)
	canvas.add_child(panel)

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 16)
	margin.add_theme_constant_override("margin_top", 16)
	margin.add_theme_constant_override("margin_right", 16)
	margin.add_theme_constant_override("margin_bottom", 16)
	panel.add_child(margin)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	margin.add_child(vbox)

	var title := Label.new()
	title.text = "F-ZERO GX Native Sim"
	title.add_theme_font_size_override("font_size", 24)
	vbox.add_child(title)

	vbox.add_child(_make_field_label("Track"))
	track_selector = OptionButton.new()
	vbox.add_child(track_selector)

	vbox.add_child(_make_field_label("Random Seed"))
	var random_track_row := HBoxContainer.new()
	random_track_row.add_theme_constant_override("separation", 8)
	vbox.add_child(random_track_row)

	random_seed_edit = LineEdit.new()
	random_seed_edit.placeholder_text = "Leave blank for a fresh seed"
	random_seed_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	random_track_row.add_child(random_seed_edit)

	generate_track_button = Button.new()
	generate_track_button.text = "Generate Track"
	generate_track_button.pressed.connect(_on_generate_random_track_pressed)
	random_track_row.add_child(generate_track_button)

	vbox.add_child(_make_field_label("Machine"))
	machine_selector = OptionButton.new()
	vbox.add_child(machine_selector)

	vbox.add_child(_make_field_label("Machine Setting"))
	var machine_setting_row := HBoxContainer.new()
	machine_setting_row.add_theme_constant_override("separation", 8)
	vbox.add_child(machine_setting_row)

	machine_setting_slider = HSlider.new()
	machine_setting_slider.min_value = 0.0
	machine_setting_slider.max_value = 100.0
	machine_setting_slider.step = 1.0
	machine_setting_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	machine_setting_slider.value_changed.connect(_on_machine_setting_changed)
	machine_setting_row.add_child(machine_setting_slider)

	machine_setting_value_label = Label.new()
	machine_setting_row.add_child(machine_setting_value_label)
	machine_setting_slider.value = 50.0
	_on_machine_setting_changed(machine_setting_slider.value)

	var button_row := HBoxContainer.new()
	button_row.add_theme_constant_override("separation", 8)
	vbox.add_child(button_row)

	start_button = Button.new()
	start_button.text = "Start"
	start_button.pressed.connect(_on_start_pressed)
	button_row.add_child(start_button)

	restart_button = Button.new()
	restart_button.text = "Restart"
	restart_button.pressed.connect(_on_restart_pressed)
	restart_button.disabled = true
	button_row.add_child(restart_button)

	var utility_row := HBoxContainer.new()
	utility_row.add_theme_constant_override("separation", 8)
	vbox.add_child(utility_row)

	copy_pose_button = Button.new()
	copy_pose_button.text = "Copy Pose"
	copy_pose_button.pressed.connect(_on_copy_pose_pressed)
	copy_pose_button.disabled = true
	utility_row.add_child(copy_pose_button)

	save_replay_button = Button.new()
	save_replay_button.text = "Save Replay"
	save_replay_button.pressed.connect(_on_save_replay_pressed)
	save_replay_button.disabled = true
	utility_row.add_child(save_replay_button)

	var replay_row := HBoxContainer.new()
	replay_row.add_theme_constant_override("separation", 8)
	vbox.add_child(replay_row)

	load_replay_button = Button.new()
	load_replay_button.text = "Load Replay"
	load_replay_button.pressed.connect(_on_load_replay_pressed)
	replay_row.add_child(load_replay_button)

	replay_play_pause_button = Button.new()
	replay_play_pause_button.text = "Play Replay"
	replay_play_pause_button.pressed.connect(_on_replay_play_pause_pressed)
	replay_play_pause_button.disabled = true
	replay_row.add_child(replay_play_pause_button)

	replay_step_button = Button.new()
	replay_step_button.text = "Step Replay"
	replay_step_button.pressed.connect(_on_replay_step_pressed)
	replay_step_button.disabled = true
	replay_row.add_child(replay_step_button)

	checkpoint_debug_checkbox = CheckBox.new()
	checkpoint_debug_checkbox.text = "Show Checkpoints"
	checkpoint_debug_checkbox.toggled.connect(_on_checkpoint_debug_toggled)
	vbox.add_child(checkpoint_debug_checkbox)

	status_label = Label.new()
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(status_label)

	checkpoint_debug_overlay = Control.new()
	checkpoint_debug_overlay.name = "CheckpointDebugOverlay"
	checkpoint_debug_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	checkpoint_debug_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	canvas.add_child(checkpoint_debug_overlay)

	replay_file_dialog = FileDialog.new()
	replay_file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	replay_file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	replay_file_dialog.filters = PackedStringArray(["*.json ; Replay JSON"])
	replay_file_dialog.file_selected.connect(_on_replay_file_selected)
	canvas.add_child(replay_file_dialog)


func _make_field_label(text: String) -> Label:
	var label := Label.new()
	label.text = text
	return label


func _parse_stage_authored_track_id(filename: String) -> int:
	if !filename.begins_with("COLI_COURSE") or !filename.ends_with(".lz"):
		return -1
	var digits := filename.trim_prefix("COLI_COURSE").trim_suffix(".lz")
	if digits.is_empty():
		return -1
	for i in digits.length():
		var code := digits.unicode_at(i)
		if code < 48 or code > 57:
			return -1
	return int(digits)


func _sanitize_filename_component(text: String) -> String:
	var result := ""
	var trimmed := text.strip_edges()

	for i in trimmed.length():
		var code := trimmed.unicode_at(i)
		var is_digit := code >= 48 and code <= 57
		var is_upper := code >= 65 and code <= 90
		var is_lower := code >= 97 and code <= 122
		if is_digit or is_upper or is_lower:
			result += char(code)
		elif code == 45 or code == 95:
			result += char(code)
		else:
			result += "_"
	if result.is_empty():
		return "track"
	return result


func _normalize_random_seed_text(seed_text: String) -> String:
	var trimmed := seed_text.strip_edges()
	if !trimmed.is_empty():
		return trimmed
	return "seed_%d_%d" % [
		int(Time.get_unix_time_from_system()),
		Time.get_ticks_usec(),
	]


func _get_content_directory_candidates(relative_dir: String) -> Array:
	var normalized := relative_dir.trim_prefix("/").trim_suffix("/")
	var candidates: Array = []
	var seen := {}
	var res_path := "res://%s" % normalized
	var global_res_path := ProjectSettings.globalize_path(res_path)
	var executable_path := OS.get_executable_path()
	var executable_dir := ""
	if !executable_path.is_empty():
		executable_dir = executable_path.get_base_dir().path_join(normalized)

	for candidate in [res_path, global_res_path, executable_dir]:
		if candidate.is_empty() or seen.has(candidate):
			continue
		seen[candidate] = true
		candidates.append(candidate)
	return candidates


func _collect_content_file_paths(relative_dir: String, suffix: String) -> Dictionary:
	var file_paths := {}

	for directory_path in _get_content_directory_candidates(relative_dir):
		var dir := DirAccess.open(directory_path)
		if dir == null:
			continue

		dir.list_dir_begin()
		while true:
			var filename := dir.get_next()
			if filename.is_empty():
				break
			if dir.current_is_dir():
				continue
			if !suffix.is_empty() and !filename.ends_with(suffix):
				continue
			if !file_paths.has(filename):
				file_paths[filename] = directory_path.path_join(filename)
		dir.list_dir_end()

	return file_paths


func _load_stock_stage_catalog_entries() -> Array:
	var entries: Array = []
	var stage_paths := _collect_content_file_paths("stage", ".lz")
	var filenames := stage_paths.keys()
	filenames.sort()

	for filename_variant in filenames:
		var filename := String(filename_variant)
		var authored_track_id := _parse_stage_authored_track_id(filename)
		if authored_track_id < 0:
			continue

		var path := String(stage_paths.get(filename, ""))
		if path.is_empty():
			continue
		var bytes := FileAccess.get_file_as_bytes(path)
		if bytes.is_empty():
			continue
		entries.append(
			{
				"authored_track_id": authored_track_id,
				"bytes": bytes,
				"entry_kind": "stage_bytes",
			}
		)

	entries.sort_custom(
		func(a: Dictionary, b: Dictionary) -> bool:
			return int(a.get("authored_track_id", 0)) < int(b.get("authored_track_id", 0))
	)
	for i in entries.size():
		entries[i]["label"] = "#%02d  COLI_COURSE%02d" % [i, int(entries[i]["authored_track_id"])]
	return entries


func _load_generated_stage_catalog_entries() -> Array:
	var entries: Array = []
	var dir := DirAccess.open("user://generated_tracks")
	if dir == null:
		return entries

	dir.list_dir_begin()
	while true:
		var filename := dir.get_next()
		if filename.is_empty():
			break
		if dir.current_is_dir() or !filename.ends_with(".json"):
			continue

		var path := "user://generated_tracks/%s" % filename
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(parsed) != TYPE_DICTIONARY:
			continue
		if String(parsed.get("type", "")) != "fzgx_generated_track":
			continue
		entries.append(
			{
				"entry_kind": "generated_track",
				"authored_track_id": int(parsed.get("authored_track_id", 0)),
				"label": String(parsed.get("label", filename.get_basename())),
				"generated_data": parsed,
			}
		)
	dir.list_dir_end()

	entries.sort_custom(
		func(a: Dictionary, b: Dictionary) -> bool:
			return String(a.get("label", "")) < String(b.get("label", ""))
	)
	return entries


func _load_stage_catalog_entries() -> Array:
	var entries := _load_stock_stage_catalog_entries()
	entries.append_array(_load_generated_stage_catalog_entries())
	return entries


func _load_machine_catalog_entries() -> Array:
	var entries: Array = []
	var machine_paths := _collect_content_file_paths("machines", ".json")
	var filenames := machine_paths.keys()
	filenames.sort()

	for filename_variant in filenames:
		var filename := String(filename_variant)
		var path := String(machine_paths.get(filename, ""))
		if path.is_empty():
			continue
		var bytes := FileAccess.get_file_as_bytes(path)
		if bytes.is_empty():
			continue

		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
		if typeof(parsed) != TYPE_DICTIONARY:
			continue

		var entry: Dictionary = {
			"machine_id": int(parsed.get("machine_id", 0)),
			"label": String(parsed.get("name", "Machine %02d" % entries.size())),
			"bytes": bytes,
		}
		entries.append(entry)

	entries.sort_custom(
		func(a: Dictionary, b: Dictionary) -> bool:
			var lhs_id := int(a.get("machine_id", 0))
			var rhs_id := int(b.get("machine_id", 0))
			if lhs_id != rhs_id:
				return lhs_id < rhs_id
			return String(a.get("label", "")) < String(b.get("label", ""))
	)
	return entries


func _save_generated_track_json_file(seed_text: String, label: String, json_text: String) -> String:
	var directory_path := "user://generated_tracks"
	var absolute_directory := ProjectSettings.globalize_path(directory_path)
	var file_path := "%s/%s_%s.json" % [
		directory_path,
		_sanitize_filename_component(seed_text).left(32),
		_sanitize_filename_component(label).left(32),
	]

	DirAccess.make_dir_recursive_absolute(absolute_directory)
	var file := FileAccess.open(file_path, FileAccess.WRITE)
	if file == null:
		return ""
	file.store_string(json_text)
	return file_path


func _populate_dropdowns(preferred_track_label: String = "", preferred_machine_index: int = -1) -> void:
	var track_entries := _load_stage_catalog_entries()
	var machine_entries := _load_machine_catalog_entries()
	var status := int(bridge.configure_content_catalog(track_entries, machine_entries))
	var previous_machine_index := preferred_machine_index

	if previous_machine_index < 0 and machine_selector != null and machine_selector.item_count > 0:
		previous_machine_index = machine_selector.get_selected_id()

	track_selector.clear()
	machine_selector.clear()
	if status != STATUS_OK:
		_update_status("configure_content_catalog failed: %d" % status)
		return

	for i in track_entries.size():
		track_selector.add_item(String(track_entries[i].get("label", "Track %02d" % i)), i)
	if track_selector.item_count > 0:
		var selected_track_index := 0
		if !preferred_track_label.is_empty():
			for i in track_selector.item_count:
				if track_selector.get_item_text(i) == preferred_track_label:
					selected_track_index = i
					break
		track_selector.select(selected_track_index)

	for i in machine_entries.size():
		machine_selector.add_item(String(machine_entries[i].get("label", "Machine %02d" % i)), i)
	if machine_selector.item_count > 0:
		if previous_machine_index >= 0 and previous_machine_index < machine_selector.item_count:
			machine_selector.select(previous_machine_index)
		else:
			machine_selector.select(0)


func _sync_camera_render_state() -> void:
	if bridge == null or follow_camera == null:
		return

	var viewport_size: Vector2 = get_viewport().get_visible_rect().size
	var aspect_ratio := 4.0 / 3.0
	if viewport_size.y > 0.0:
		aspect_ratio = viewport_size.x / viewport_size.y

	var display_mode_kind := 0
	if aspect_ratio > 1.4:
		display_mode_kind = 1

	bridge.set_camera_render_state(aspect_ratio, display_mode_kind)


func _on_generate_random_track_pressed() -> void:
	if bridge == null:
		_update_status("FzgxGameBridge is unavailable.")
		return
	var seed_text := _normalize_random_seed_text(random_seed_edit.text if random_seed_edit != null else "")
	var generated: Dictionary = bridge.generate_random_track_json(seed_text)
	var status := int(generated.get("status", -1))
	if status != STATUS_OK:
		_update_status("generate_random_track_json failed: %d" % status)
		return

	var label := String(generated.get("label", "Random Track"))
	var json_text := String(generated.get("json_text", ""))
	if json_text.is_empty():
		_update_status("Random track generation did not return JSON.")
		return

	var saved_path := _save_generated_track_json_file(seed_text, label, json_text)
	if saved_path.is_empty():
		_update_status("Failed to save generated track JSON.")
		return

	if random_seed_edit != null:
		var was_empty := random_seed_edit.text == ""
		if !was_empty:
			random_seed_edit.text = seed_text
	_populate_dropdowns(label)
	_on_start_pressed()
	if session_active:
		_update_status("Generated %s, saved it to %s, and started the session." % [label, saved_path])
	else:
		_update_status("Generated %s and saved it to %s." % [label, saved_path])


func _on_start_pressed() -> void:
	_clear_loaded_replay()
	_sync_camera_render_state()
	var status := int(
		bridge.start_session(
			track_selector.get_selected_id(),
			machine_selector.get_selected_id(),
			_get_selected_machine_setting_percent()
		)
	)
	if status != STATUS_OK:
		session_active = false
		restart_button.disabled = true
		copy_pose_button.disabled = true
		save_replay_button.disabled = true
		_clear_checkpoint_debug_visuals()
		_update_status("start_session failed: %d" % status)
		return

	session_active = true
	restart_button.disabled = false
	copy_pose_button.disabled = false
	save_replay_button.disabled = false
	_rebuild_track_mesh()
	_reset_interpolated_transforms()
	_apply_machine_state(bridge.read_machine_state(), bridge.read_game_camera_state())
	_sync_extra_collision_debug_state()
	_apply_interpolated_transforms(1.0)
	_update_replay_controls()
	_update_status("Session live.")


func _on_restart_pressed() -> void:
	if replay_loaded:
		if !_restart_loaded_replay_session():
			return
		_update_status("Replay restarted.")
		return
	else:
		var status := STATUS_OK
		_sync_camera_render_state()
		status = int(bridge.restart_session())
		if status != STATUS_OK:
			session_active = false
			copy_pose_button.disabled = true
			save_replay_button.disabled = true
			replay_playing = false
			_clear_checkpoint_debug_visuals()
			_update_replay_controls()
			_update_status("restart_session failed: %d" % status)
			return
		session_active = true
		copy_pose_button.disabled = false
		save_replay_button.disabled = false
		_rebuild_track_mesh()
		_reset_interpolated_transforms()
		_apply_machine_state(bridge.read_machine_state(), bridge.read_game_camera_state())
		_sync_extra_collision_debug_state()
		_apply_interpolated_transforms(1.0)
		_update_replay_controls()
		_update_status("Session restarted.")


func _on_copy_pose_pressed() -> void:
	if !session_active:
		_update_status("No active session to copy from.")
		return

	var machine_state: Dictionary = bridge.read_machine_state()
	if !machine_state.get("active", false):
		_update_status("Machine state is not active.")
		return

	DisplayServer.clipboard_set(_format_pose_snapshot(machine_state))
	_update_status(
		"Copied pose for frame %d to clipboard." %
		[int(machine_state.get("frame_index", 0))]
	)


func _on_save_replay_pressed() -> void:
	if bridge == null or !bridge.has_replay_capture():
		_update_status("No captured replay is available yet.")
		return

	var replay_path: String = bridge.save_replay_json()
	if replay_path.is_empty():
		_update_status("save_replay_json failed.")
		return

	_update_status(
		"Saved replay with %d frames to %s." %
		[
			int(bridge.get_replay_frame_count()),
			replay_path,
		]
	)


func _on_save_state_pressed() -> void:
	if bridge == null:
		_update_status("FzgxGameBridge is unavailable.")
		return

	var status := int(bridge.save_shared_state_slot())
	if status != STATUS_OK:
		_update_status("save_shared_state_slot failed: %d" % status)
		return

	_update_status("Saved state to shared slot: %s." % bridge.get_shared_state_slot_path())


func _on_load_state_pressed() -> void:
	_clear_loaded_replay()
	if bridge == null:
		_update_status("FzgxGameBridge is unavailable.")
		return

	var had_session = bridge.has_session()
	var previous_track_index := int(bridge.get_current_track_index()) if had_session else -1
	var status := int(bridge.load_shared_state_slot())
	if status != STATUS_OK:
		_update_status("load_shared_state_slot failed: %d" % status)
		return

	session_active = bridge.has_session()
	restart_button.disabled = !session_active
	copy_pose_button.disabled = !session_active
	save_replay_button.disabled = !bridge.has_replay_capture()
	_sync_session_controls_from_bridge()
	_update_replay_controls()
	var loaded_track_index := int(bridge.get_current_track_index())
	if !had_session or previous_track_index != loaded_track_index:
		_rebuild_track_mesh()
	_reset_interpolated_transforms()
	_apply_machine_state(bridge.read_machine_state(), bridge.read_game_camera_state())
	_sync_extra_collision_debug_state()
	_apply_interpolated_transforms(1.0)
	_update_status("Loaded state from shared slot: %s." % bridge.get_shared_state_slot_path())


func _on_load_replay_pressed() -> void:
	if bridge == null:
		_update_status("FzgxGameBridge is unavailable.")
		return
	if replay_file_dialog == null:
		_update_status("Replay file dialog is unavailable.")
		return

	var replay_dir := ProjectSettings.globalize_path("res://replays")
	if DirAccess.dir_exists_absolute(replay_dir):
		replay_file_dialog.current_dir = replay_dir
	replay_file_dialog.popup_centered_ratio(0.8)


func _on_replay_file_selected(path: String) -> void:
	var replay_data := _parse_replay_file(path)
	if replay_data.is_empty():
		return

	replay_loaded = true
	replay_playing = false
	replay_path = path
	replay_track_index = int(replay_data.get("track_index", -1))
	replay_machine_index = int(replay_data.get("machine_index", -1))
	replay_machine_setting_percent = int(replay_data.get("machine_setting_percent", 0))
	replay_next_frame_index = 0
	replay_frames = replay_data.get("frames", [])
	replay_camera_render_state = replay_data.get("camera_render_state", {})
	if !_restart_loaded_replay_session():
		_clear_loaded_replay()
		return
	_update_status(
		"Loaded replay %s (%d frames)." %
		[path.get_file(), replay_frames.size()]
	)


func _on_replay_play_pause_pressed() -> void:
	if !replay_loaded:
		_update_status("No replay is loaded.")
		return
	if !session_active:
		_update_status("Replay session is not active.")
		return
	if replay_next_frame_index >= replay_frames.size():
		if !_restart_loaded_replay_session():
			return
	replay_playing = !replay_playing
	_update_replay_controls()
	_update_status("Replay %s." % ("playing" if replay_playing else "paused"))


func _on_replay_step_pressed() -> void:
	if !replay_loaded:
		_update_status("No replay is loaded.")
		return
	if !session_active:
		_update_status("Replay session is not active.")
		return
	if replay_next_frame_index >= replay_frames.size():
		if !_restart_loaded_replay_session():
			return
	_step_loaded_replay_frame()


func _on_machine_setting_changed(value: float) -> void:
	if machine_setting_value_label != null:
		machine_setting_value_label.text = "%d%%" % int(round(value))


func _on_checkpoint_debug_toggled(enabled: bool) -> void:
	checkpoint_debug_enabled = enabled
	_update_checkpoint_debug_visuals()


func _get_selected_machine_setting_percent() -> int:
	if machine_setting_slider == null:
		return 50
	return int(round(machine_setting_slider.value))


func _sync_session_controls_from_bridge() -> void:
	if bridge == null:
		return

	var track_index := int(bridge.get_current_track_index())
	if track_selector != null and track_index >= 0 and track_index < track_selector.item_count:
		track_selector.select(track_index)

	var machine_index := int(bridge.get_current_machine_index())
	if machine_selector != null and machine_index >= 0 and machine_index < machine_selector.item_count:
		machine_selector.select(machine_index)

	var machine_setting_percent := int(bridge.get_current_machine_setting_percent())
	if machine_setting_slider != null and machine_setting_percent >= 0:
		machine_setting_slider.value = machine_setting_percent


func _update_replay_controls() -> void:
	var bridge_ready := bridge != null
	var can_control := bridge_ready and replay_loaded and session_active

	if load_replay_button != null:
		load_replay_button.disabled = !bridge_ready
	if replay_play_pause_button != null:
		replay_play_pause_button.disabled = !can_control
		replay_play_pause_button.text = "Pause Replay" if replay_playing else "Play Replay"
	if replay_step_button != null:
		replay_step_button.disabled = !can_control or replay_playing


func _clear_loaded_replay() -> void:
	replay_loaded = false
	replay_playing = false
	replay_path = ""
	replay_track_index = -1
	replay_machine_index = -1
	replay_machine_setting_percent = 0
	replay_next_frame_index = 0
	replay_frames.clear()
	replay_camera_render_state.clear()
	_update_replay_controls()


func _parse_replay_file(path: String) -> Dictionary:
	var text := FileAccess.get_file_as_string(path)
	var replay_json = JSON.parse_string(text)
	var result := {}
	var parsed_frames: Array = []
	var camera_state := {}

	if typeof(replay_json) != TYPE_DICTIONARY:
		_update_status("Failed to parse replay JSON: %s" % path)
		return {}
	if replay_json.get("type", "") != "fzgx_input_replay":
		_update_status("Unsupported replay type in %s." % path)
		return {}
	if typeof(replay_json.get("frames", [])) != TYPE_ARRAY:
		_update_status("Replay frames are missing in %s." % path)
		return {}

	for entry in replay_json["frames"]:
		if typeof(entry) != TYPE_ARRAY or entry.size() < 8:
			_update_status("Replay frame format is invalid in %s." % path)
			return {}
		parsed_frames.append(
			{
				"steer_yaw": _replay_hex_to_float(String(entry[0])),
				"steer_pitch": _replay_hex_to_float(String(entry[1])),
				"accel": _replay_hex_to_float(String(entry[2])),
				"brake": _replay_hex_to_float(String(entry[3])),
				"strafe": _replay_hex_to_float(String(entry[4])),
				"buttons": _replay_hex_to_u32(String(entry[5])),
				"view_up_pressed": bool(entry[6]),
				"view_down_pressed": bool(entry[7]),
			}
		)

	if typeof(replay_json.get("camera_render_state", {})) == TYPE_DICTIONARY:
		var source_camera_state: Dictionary = replay_json.get("camera_render_state", {})

		camera_state = {
			"aspect_ratio": _replay_hex_to_float(String(source_camera_state.get("aspect_ratio_hex", "00000000"))),
			"display_mode_kind": int(source_camera_state.get("display_mode_kind", -1)),
			"camera_parameter": _replay_hex_to_float(
				String(source_camera_state.get("camera_parameter_hex", "00000000"))
			),
			"camera_manager_mode": int(source_camera_state.get("camera_manager_mode", 0)),
		}

	result["track_index"] = int(replay_json.get("track_index", -1))
	result["machine_index"] = int(replay_json.get("machine_index", -1))
	result["machine_setting_percent"] = int(replay_json.get("machine_setting_percent", 0))
	result["frames"] = parsed_frames
	result["camera_render_state"] = camera_state
	return result


func _replay_hex_to_u32(hex_text: String) -> int:
	var cleaned := hex_text.strip_edges()

	if cleaned.begins_with("0x") or cleaned.begins_with("0X"):
		cleaned = cleaned.substr(2)
	if cleaned.is_empty():
		return 0
	return cleaned.hex_to_int()


func _replay_hex_to_float(hex_text: String) -> float:
	var bits := _replay_hex_to_u32(hex_text)
	var bytes := PackedByteArray()

	bytes.resize(4)
	bytes.encode_u32(0, bits)
	return bytes.decode_float(0)


func _apply_replay_camera_render_state() -> void:
	if bridge == null:
		return

	var viewport_size: Vector2 = get_viewport().get_visible_rect().size
	var aspect_ratio := float(replay_camera_render_state.get("aspect_ratio", 4.0 / 3.0))
	if viewport_size.y > 0.0:
		aspect_ratio = viewport_size.x / viewport_size.y
	bridge.set_camera_render_state(
		aspect_ratio,
		int(replay_camera_render_state.get("display_mode_kind", -1)),
		float(replay_camera_render_state.get("camera_parameter", -1.0)),
		int(replay_camera_render_state.get("camera_manager_mode", 0))
	)


func _restart_loaded_replay_session() -> bool:
	var status := STATUS_OK

	if bridge == null or !replay_loaded:
		return false
	_apply_replay_camera_render_state()
	status = int(
		bridge.start_session(
			replay_track_index,
			replay_machine_index,
			replay_machine_setting_percent
		)
	)
	if status != STATUS_OK:
		session_active = false
		restart_button.disabled = true
		copy_pose_button.disabled = true
		save_replay_button.disabled = true
		replay_playing = false
		_clear_checkpoint_debug_visuals()
		_update_replay_controls()
		_update_status("start_session failed: %d" % status)
		return false

	replay_playing = false
	replay_next_frame_index = 0
	session_active = true
	restart_button.disabled = false
	copy_pose_button.disabled = false
	save_replay_button.disabled = false
	_sync_session_controls_from_bridge()
	_rebuild_track_mesh()
	_reset_interpolated_transforms()
	_apply_machine_state(bridge.read_machine_state(), bridge.read_game_camera_state())
	_sync_extra_collision_debug_state()
	_apply_interpolated_transforms(1.0)
	_update_replay_controls()
	return true


func _step_loaded_replay_frame() -> void:
	if bridge == null or !replay_loaded or !session_active:
		return
	if replay_next_frame_index >= replay_frames.size():
		replay_playing = false
		_update_replay_controls()
		_update_status("Replay finished.")
		return

	var frame: Dictionary = replay_frames[replay_next_frame_index]

	_apply_replay_camera_render_state()
	var status := int(
		bridge.step_frame(
			float(frame.get("steer_yaw", 0.0)),
			float(frame.get("steer_pitch", 0.0)),
			float(frame.get("accel", 0.0)),
			float(frame.get("brake", 0.0)),
			float(frame.get("strafe", 0.0)),
			int(frame.get("buttons", 0)),
			bool(frame.get("view_up_pressed", false)),
			bool(frame.get("view_down_pressed", false))
		)
	)
	if status != STATUS_OK:
		session_active = false
		copy_pose_button.disabled = true
		save_replay_button.disabled = !bridge.has_replay_capture()
		replay_playing = false
		_clear_checkpoint_debug_visuals()
		_update_replay_controls()
		_update_status("step_frame failed: %d" % status)
		return

	replay_next_frame_index += 1
	if replay_next_frame_index >= replay_frames.size():
		replay_playing = false
	_update_replay_controls()

	var machine_state: Dictionary = bridge.read_machine_state()
	if !machine_state.get("active", false):
		return
	_apply_machine_state(machine_state, bridge.read_game_camera_state())
	_sync_extra_collision_debug_state()


func _rebuild_track_mesh() -> void:
	var mesh_info: Dictionary = bridge.read_loaded_track_collision_mesh()
	track_mesh_instance.mesh = _build_track_mesh_from_info(mesh_info, Color(0.31, 0.34, 0.38), 0.32)

	var analytic_mesh_info: Dictionary = bridge.read_loaded_track_analytic_debug_mesh()
	analytic_track_mesh_instance.mesh = _build_track_mesh_from_info(
		analytic_mesh_info,
		Color(0.14, 0.77, 0.88),
		0.0
	)

	var checkpoint_debug_info: Dictionary = bridge.read_loaded_track_checkpoint_debug()
	checkpoint_debug_entries = checkpoint_debug_info.get("entries", [])
	_update_checkpoint_debug_visuals()
	_rebuild_extra_collision_debug_meshes()


func _build_track_mesh_from_info(mesh_info: Dictionary, albedo: Color, alpha: float) -> Mesh:
	if !mesh_info.get("valid", false):
		return null

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = mesh_info["vertices"]
	arrays[Mesh.ARRAY_NORMAL] = mesh_info["normals"]
	if mesh_info.has("uvs"):
		arrays[Mesh.ARRAY_TEX_UV] = mesh_info["uvs"]

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	#var material := StandardMaterial3D.new()
	#material.albedo_color = Color(albedo.r, albedo.g, albedo.b, 1.0 - alpha)
	#material.roughness = 1.0
	#material.cull_mode = BaseMaterial3D.CULL_DISABLED
	#if alpha > 0.0:
		#material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mesh.surface_set_material(0, preload("res://debug_track_mat.tres"))

	return mesh


func _build_collision_debug_mesh_from_info(mesh_info: Dictionary, material: Material) -> Mesh:
	if !mesh_info.has("vertices") or !mesh_info.has("normals"):
		return null
	var vertices: PackedVector3Array = mesh_info["vertices"]
	if vertices.is_empty():
		return null

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = mesh_info["normals"]

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, material)
	return mesh


func _get_collision_debug_material(kind: String) -> Material:
	if collision_debug_materials.has(kind):
		return collision_debug_materials[kind]

	var material := StandardMaterial3D.new()
	material.roughness = 1.0
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED

	match kind:
		"track_mesh_chunk":
			material.albedo_color = Color(0.94, 0.74, 0.22, 0.55)
		"dynamic_scene_object":
			material.albedo_color = Color(0.18, 0.84, 0.43, 0.60)
		"unknown_collider":
			material.albedo_color = Color(0.92, 0.47, 0.20, 0.60)
		"static_scene_object":
			material.albedo_color = Color(0.22, 0.58, 0.94, 0.60)
		_:
			material.albedo_color = Color(0.85, 0.85, 0.85, 0.55)

	collision_debug_materials[kind] = material
	return material


func _clear_extra_collision_debug_meshes() -> void:
	for instance in extra_collision_instances:
		if instance != null:
			instance.queue_free()
	extra_collision_instances.clear()


func _rebuild_extra_collision_debug_meshes() -> void:
	_clear_extra_collision_debug_meshes()
	if bridge == null or extra_collision_root == null:
		return

	var entries: Array = bridge.read_loaded_extra_collision_debug_meshes()
	for entry in entries:
		var kind := String(entry.get("kind", ""))
		var instance := MeshInstance3D.new()
		var mesh := _build_collision_debug_mesh_from_info(
			entry,
			_get_collision_debug_material(kind)
		)
		if mesh == null:
			instance.queue_free()
			continue
		instance.name = String(entry.get("name", "CollisionDebug"))
		instance.mesh = mesh
		instance.transform = entry.get("transform", Transform3D.IDENTITY)
		instance.visible = entry.get("visible", true)
		extra_collision_root.add_child(instance)
		extra_collision_instances.append(instance)


func _sync_extra_collision_debug_state() -> void:
	if bridge == null or extra_collision_instances.is_empty():
		return

	var states: Array = bridge.read_loaded_extra_collision_debug_state()
	var count: int = int(min(states.size(), extra_collision_instances.size()))
	for index in range(count):
		var state: Dictionary = states[index]
		var instance: MeshInstance3D = extra_collision_instances[index]
		if instance == null:
			continue
		instance.transform = state.get("transform", Transform3D.IDENTITY)
		instance.visible = state.get("visible", true)


func _apply_machine_state(machine_state: Dictionary, camera_state: Dictionary) -> void:
	var machine_transform: Transform3D = machine_state.get(
		"visual_transform",
		machine_state["transform"]
	)
	if !machine_transform_valid:
		machine_transform_previous = machine_transform
		machine_transform_current = machine_transform
		machine_transform_valid = true
	else:
		machine_transform_previous = machine_transform_current
		machine_transform_current = machine_transform

	if camera_state.get("active", false):
		follow_camera.fov = float(camera_state.get("perspective", 55.0))
		var camera_transform: Transform3D = camera_state.get("transform", Transform3D.IDENTITY)
		if !camera_transform_valid:
			camera_transform_previous = camera_transform
			camera_transform_current = camera_transform
			camera_transform_valid = true
		else:
			camera_transform_previous = camera_transform_current
			camera_transform_current = camera_transform
	else:
		camera_transform_valid = false
	latest_machine_state = machine_state

	var status_text := (
		"Frame %d | Speed %.1f km/h | Energy %.1f | View %d" %
		[
			int(machine_state.get("frame_index", 0)),
			float(machine_state.get("speed_kmh", 0.0)),
			float(machine_state.get("energy", 0.0)),
			int(camera_state.get("zoom_mode", 1)),
		]
	)
	if replay_loaded:
		status_text += " | Replay %d/%d %s" % [
			replay_next_frame_index,
			replay_frames.size(),
			"(playing)" if replay_playing else "(paused)",
		]
	_update_status(status_text)


func _reset_interpolated_transforms() -> void:
	machine_transform_previous = Transform3D.IDENTITY
	machine_transform_current = Transform3D.IDENTITY
	camera_transform_previous = Transform3D.IDENTITY
	camera_transform_current = Transform3D.IDENTITY
	machine_transform_valid = false
	camera_transform_valid = false


func _apply_interpolated_transforms(weight: float) -> void:
	var clamped_weight := clampf(weight, 0.0, 1.0)

	if machine_transform_valid:
		machine_mesh_instance.transform = machine_transform_previous.interpolate_with(
			machine_transform_current,
			clamped_weight
		)

	if camera_transform_valid:
		follow_camera.transform = camera_transform_previous.interpolate_with(
			camera_transform_current,
			clamped_weight
		)


func _update_status(text: String) -> void:
	if status_label != null:
		status_label.text = text


func _clear_checkpoint_debug_visuals() -> void:
	if checkpoint_debug_instance != null:
		checkpoint_debug_instance.mesh = null
		checkpoint_debug_instance.visible = false
	_hide_checkpoint_debug_labels()


func _update_checkpoint_debug_visuals() -> void:
	if checkpoint_debug_instance == null or checkpoint_debug_overlay == null:
		return
	if !session_active or !checkpoint_debug_enabled or checkpoint_debug_entries.is_empty() or !camera_transform_valid:
		_clear_checkpoint_debug_visuals()
		return

	var camera_transform := follow_camera.global_transform
	var camera_origin := camera_transform.origin
	var camera_forward := (-camera_transform.basis.z).normalized()
	var current_checkpoint := int(latest_machine_state.get("current_checkpoint", -1))
	var nearest: Array = []

	for entry in checkpoint_debug_entries:
		var center: Vector3 = entry.get("center", Vector3.ZERO)
		var to_center := center - camera_origin
		var forward_distance := to_center.dot(camera_forward)
		if forward_distance < -20.0:
			continue
		var distance := to_center.length()
		if distance > CHECKPOINT_DEBUG_MAX_DISTANCE:
			continue
		var score := maxf(forward_distance, 0.0) + distance * 0.15
		nearest.append(
			{
				"entry": entry,
				"forward_distance": forward_distance,
				"distance": distance,
				"score": score,
			}
		)

	nearest.sort_custom(
		func(a: Dictionary, b: Dictionary) -> bool:
			return float(a.get("score", 0.0)) < float(b.get("score", 0.0))
	)

	if nearest.is_empty():
		_clear_checkpoint_debug_visuals()
		return

	var mesh := ImmediateMesh.new()
	mesh.clear_surfaces()
	mesh.surface_begin(Mesh.PRIMITIVE_LINES, checkpoint_debug_material)
	for i in range(mini(nearest.size(), CHECKPOINT_DEBUG_DRAW_COUNT)):
		var item: Dictionary = nearest[i]
		var entry: Dictionary = item.get("entry", {})
		var highlight := int(entry.get("checkpoint_index", -1)) == current_checkpoint
		var color := Color(0.20, 0.90, 1.0, 0.72)
		if highlight:
			color = Color(1.0, 0.86, 0.24, 0.98)
		_append_checkpoint_debug_wire(mesh, entry, color)
	mesh.surface_end()
	checkpoint_debug_instance.mesh = mesh
	checkpoint_debug_instance.visible = true
	_update_checkpoint_debug_labels(nearest, current_checkpoint)


func _append_checkpoint_debug_wire(mesh: ImmediateMesh, entry: Dictionary, color: Color) -> void:
	var origin: Vector3 = entry.get("start_origin", Vector3.ZERO)
	var end_origin: Vector3 = entry.get("end_origin", origin)
	var right: Vector3 = Vector3(entry.get("track_right", Vector3.RIGHT)).normalized()
	var up: Vector3 = Vector3(entry.get("track_up", Vector3.UP)).normalized()
	var normal: Vector3 = Vector3(entry.get("plane_normal", Vector3.FORWARD)).normalized()
	var raw_width := float(entry.get("track_width", 60.0))
	var frame_width := float(entry.get("frame_width_or_radius", raw_width))
	var frame_scale_y := absf(float(entry.get("frame_scale_y", 0.0)))
	var frame_surface_scale_y := absf(float(entry.get("frame_surface_scale_y", 0.0)))
	var half_width := maxf(raw_width, frame_width) * 0.5
	var plane_height := frame_surface_scale_y
	if plane_height <= 0.0:
		plane_height = frame_scale_y
	var half_height := 0.5 * plane_height
	var corner0 := origin - right * half_width - up * half_height
	var corner1 := origin + right * half_width - up * half_height
	var corner2 := origin + right * half_width + up * half_height
	var corner3 := origin - right * half_width + up * half_height

	_append_colored_line(mesh, corner0, corner1, color)
	_append_colored_line(mesh, corner1, corner2, color)
	_append_colored_line(mesh, corner2, corner3, color)
	_append_colored_line(mesh, corner3, corner0, color)
	_append_colored_line(mesh, origin, end_origin, color.darkened(0.12))
	_append_colored_line(mesh, origin, origin + normal * CHECKPOINT_DEBUG_NORMAL_TICK, color.lightened(0.2))


func _append_colored_line(mesh: ImmediateMesh, a: Vector3, b: Vector3, color: Color) -> void:
	mesh.surface_set_color(color)
	mesh.surface_add_vertex(a)
	mesh.surface_set_color(color)
	mesh.surface_add_vertex(b)


func _hide_checkpoint_debug_labels() -> void:
	for label in checkpoint_debug_labels:
		if label != null:
			label.visible = false


func _ensure_checkpoint_debug_label(index: int) -> Label:
	while checkpoint_debug_labels.size() <= index:
		var label := Label.new()
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		label.autowrap_mode = TextServer.AUTOWRAP_OFF
		label.add_theme_font_size_override("font_size", CHECKPOINT_DEBUG_TEXT_FONT_SIZE)
		label.add_theme_constant_override("outline_size", 1)
		label.add_theme_color_override("font_outline_color", Color(0.0, 0.0, 0.0, 1.0))
		label.add_theme_color_override("font_color", Color(0.90, 0.97, 1.0, 1.0))
		label.visible = false
		checkpoint_debug_overlay.add_child(label)
		checkpoint_debug_labels.append(label)
	return checkpoint_debug_labels[index]


func _update_checkpoint_debug_labels(nearest: Array, current_checkpoint: int) -> void:
	var viewport_rect := get_viewport().get_visible_rect()
	var visible_label_count := 0

	for i in range(mini(nearest.size(), CHECKPOINT_DEBUG_TEXT_COUNT)):
		var item: Dictionary = nearest[i]
		var entry: Dictionary = item.get("entry", {})
		var center: Vector3 = entry.get("center", Vector3.ZERO)
		if follow_camera.is_position_behind(center):
			continue

		var screen_pos := follow_camera.unproject_position(center)
		if screen_pos.x < -CHECKPOINT_DEBUG_TEXT_SCREEN_MARGIN:
			continue
		if screen_pos.y < -CHECKPOINT_DEBUG_TEXT_SCREEN_MARGIN:
			continue
		if screen_pos.x > viewport_rect.size.x + CHECKPOINT_DEBUG_TEXT_SCREEN_MARGIN:
			continue
		if screen_pos.y > viewport_rect.size.y + CHECKPOINT_DEBUG_TEXT_SCREEN_MARGIN:
			continue

		var label := _ensure_checkpoint_debug_label(visible_label_count)
		var checkpoint_index := int(entry.get("checkpoint_index", -1))
		var highlight := checkpoint_index == current_checkpoint
		label.text = _format_checkpoint_debug_label_text(entry)
		label.add_theme_color_override(
			"font_color",
			Color(1.0, 0.86, 0.24, 1.0) if highlight else Color(0.88, 0.96, 1.0, 1.0)
		)
		label.reset_size()
		label.position = Vector2(
			round(screen_pos.x - label.size.x * 0.5),
			round(screen_pos.y - label.size.y * 0.5)
		)
		label.visible = true
		visible_label_count += 1

	for i in range(visible_label_count, checkpoint_debug_labels.size()):
		var label: Label = checkpoint_debug_labels[i]
		label.visible = false


func _format_checkpoint_debug_label_text(entry: Dictionary) -> String:
	var checkpoint_index := int(entry.get("checkpoint_index", -1))
	var variant_index := int(entry.get("variant_index", 0))
	var variant_count := int(entry.get("variant_count", 1))
	var track_width := float(entry.get("track_width", 0.0))
	var frame_flags := int(entry.get("frame_flags", 0))
	return "cp %03d  v %d/%d\nw %.1f  in %d out %d\n0x%08x" % [
		checkpoint_index,
		variant_index,
		maxi(variant_count - 1, 0),
		track_width,
		int(entry.get("connect_to_track_in", false)),
		int(entry.get("connect_to_track_out", false)),
		frame_flags,
	]


func _format_pose_snapshot(machine_state: Dictionary) -> String:
	var machine_transform: Transform3D = machine_state["transform"]
	var track_item_index := track_selector.get_selected()
	var machine_item_index := machine_selector.get_selected()
	var track_name := ""
	var machine_name := ""

	if track_item_index >= 0:
		track_name = track_selector.get_item_text(track_item_index)
	if machine_item_index >= 0:
		machine_name = machine_selector.get_item_text(machine_item_index)

	return "\n".join(
		[
			"fzgx_pose_snapshot",
			"track_index=%d" % track_selector.get_selected_id(),
			"track_name=%s" % track_name,
			"machine_index=%d" % machine_selector.get_selected_id(),
			"machine_name=%s" % machine_name,
			"machine_setting_percent=%d" % int(machine_state.get("machine_setting_percent", 50)),
			"frame_index=%d" % int(machine_state.get("frame_index", 0)),
			"speed_kmh=%.6f" % float(machine_state.get("speed_kmh", 0.0)),
			"energy=%.6f" % float(machine_state.get("energy", 0.0)),
			"base_speed=%.6f" % float(machine_state.get("base_speed", 0.0)),
			"boost_turbo=%.6f" % float(machine_state.get("boost_turbo", 0.0)),
			"boost_frames=%d" % int(machine_state.get("boost_frames", 0)),
			"boost_frames_manual=%d" % int(machine_state.get("boost_frames_manual", 0)),
			"boost_delay_frame_counter=%d" % int(machine_state.get("boost_delay_frame_counter", 0)),
			"air_time=%d" % int(machine_state.get("air_time", 0)),
			"zero_minus_height_above_track=%.6f" %
				float(machine_state.get("zero_minus_height_above_track", 0.0)),
			"position=%s" % _format_vec3(machine_transform.origin),
			"basis_x=%s" % _format_vec3(machine_transform.basis.x),
			"basis_y=%s" % _format_vec3(machine_transform.basis.y),
			"basis_z=%s" % _format_vec3(machine_transform.basis.z),
			"velocity=%s" % _format_vec3(machine_state.get("velocity", Vector3.ZERO)),
			"angular_velocity=%s" % _format_vec3(machine_state.get("angular_velocity", Vector3.ZERO)),
			"surface_normal=%s" % _format_vec3(machine_state.get("surface_normal", Vector3.UP)),
			"position_bottom=%s" % _format_vec3(machine_state.get("position_bottom", Vector3.ZERO)),
			"machine_state_flags=0x%08x" % int(machine_state.get("machine_state_flags", 0)),
			"state_2_flags=0x%08x" % int(machine_state.get("state_2_flags", 0)),
			"terrain_flags=0x%08x" % int(machine_state.get("terrain_flags", 0)),
			"floor_surface_flags=0x%08x" % int(machine_state.get("floor_surface_flags", 0)),
			"branch_indicator=0x%08x" % int(machine_state.get("branch_indicator", 0)),
			"branch_flags=0x%08x" % int(machine_state.get("branch_flags", 0)),
			"branch_slot=%d" % int(machine_state.get("branch_slot", 4)),
			"control_profile_kind=%d" % int(machine_state.get("control_profile_kind", 2)),
			"frames_since_start_2=%d" % int(machine_state.get("frames_since_start_2", 0)),
			"current_checkpoint=%d" % int(machine_state.get("current_checkpoint", -1)),
			"checkpoint_fraction=%.6f" % float(machine_state.get("checkpoint_fraction", 0.0)),
			"track_cur_cp_pointer=%d" % int(machine_state.get("track_cur_cp_pointer", -1)),
			"track_cur_cp_idx=%d" % int(machine_state.get("track_cur_cp_idx", -1)),
			"track_cur_cp_frac=%.6f" % float(machine_state.get("track_cur_cp_frac", 0.0)),
			"track_next_cp_idx=%d" % int(machine_state.get("track_next_cp_idx", -1)),
			"track_next_cp_frac=%.6f" % float(machine_state.get("track_next_cp_frac", 0.0)),
			"track_selected_cached_frame_index=%d" %
				int(machine_state.get("track_selected_cached_frame_index", -1)),
		]
	)


func _format_vec3(value: Vector3) -> String:
	return "(%.9f, %.9f, %.9f)" % [value.x, value.y, value.z]
