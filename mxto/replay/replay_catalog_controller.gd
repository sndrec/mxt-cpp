class_name ReplayCatalogController
extends Node

signal watch_requested(path: String)

const REPLAY_SCHEMA_VERSION := 5
const REPLAY_FILE_SUFFIX := ".mxt_replay"
const REPLAY_INTERFACE_CANVAS_LAYER := 90
const GameVersionData = preload("res://core/game_version.gd")

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var race_presentation_controller: RacePresentationController = get_node("../RacePresentationController") as RacePresentationController
@onready var replays_button: Button = get_node("../Control/ReplaysButton") as Button

var interface_layer: CanvasLayer
var root: Control
var replay_list: ItemList
var metadata_label: RichTextLabel
var name_edit: LineEdit
var watch_button: Button
var rename_button: Button
var delete_button: Button
var entries: Array = []


func initialize() -> void:
	if replays_button != null and !replays_button.pressed.is_connected(open):
		replays_button.pressed.connect(open)


func configure_command_line(args: Array, user_args: Array) -> bool:
	if !args.has("--profile-replay-catalog") and !user_args.has("--profile-replay-catalog"):
		return false
	call_deferred("_profile_and_quit")
	return true


func open() -> void:
	_build()
	refresh()
	game_manager.get_node("Control").visible = false
	game_manager.lobby_control.visible = false
	root.visible = true
	if replay_list.item_count > 0:
		replay_list.select(0)
		_on_selected(0)


func close() -> void:
	if root != null:
		root.visible = false
	if !game_manager.game_sim.sim_started:
		game_manager.get_node("Control").visible = true


func is_open() -> bool:
	return root != null and root.visible


func refresh() -> void:
	entries.clear()
	if replay_list == null:
		return
	replay_list.clear()
	var replay_dir := _replay_dir()
	if DirAccess.make_dir_recursive_absolute(replay_dir) != OK:
		return
	var directory := DirAccess.open(replay_dir)
	if directory == null:
		return
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while !file_name.is_empty():
		if !directory.current_is_dir() and file_name.ends_with(REPLAY_FILE_SUFFIX):
			var path := replay_dir.path_join(file_name)
			var data := _load_metadata(path)
			if !data.is_empty():
				data["_path"] = path
				entries.append(data)
		file_name = directory.get_next()
	directory.list_dir_end()
	entries.sort_custom(
		func(a, b): return float(a.get("created_unix", 0.0)) > float(b.get("created_unix", 0.0)))
	for entry in entries:
		replay_list.add_item(str(entry.get("name", entry.get("track_name", "Replay"))))
	_update_buttons()


func _ensure_interface_layer() -> CanvasLayer:
	if interface_layer != null and is_instance_valid(interface_layer):
		return interface_layer
	interface_layer = CanvasLayer.new()
	interface_layer.name = "ReplayCatalogLayer"
	interface_layer.layer = REPLAY_INTERFACE_CANVAS_LAYER
	add_child(interface_layer)
	return interface_layer


func _build() -> void:
	if root != null and is_instance_valid(root):
		return
	root = Control.new()
	root.name = "ReplayCatalog"
	root.visible = false
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_ensure_interface_layer().add_child(root)
	var shade := ColorRect.new()
	shade.color = Color(0.0, 0.0, 0.0, 0.72)
	shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(shade)
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "top", "right", "bottom"]:
		margin.add_theme_constant_override("margin_%s" % side, 48 if side in ["left", "right"] else 42)
	root.add_child(margin)
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 18)
	margin.add_child(columns)
	replay_list = ItemList.new()
	replay_list.custom_minimum_size = Vector2(430, 0)
	replay_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	replay_list.item_selected.connect(_on_selected)
	columns.add_child(replay_list)
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 10)
	columns.add_child(right)
	var title := Label.new()
	title.text = "Replays"
	right.add_child(title)
	metadata_label = RichTextLabel.new()
	metadata_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	metadata_label.bbcode_enabled = false
	right.add_child(metadata_label)
	name_edit = LineEdit.new()
	name_edit.placeholder_text = "Replay name"
	right.add_child(name_edit)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	right.add_child(buttons)
	watch_button = Button.new()
	watch_button.text = "Watch"
	watch_button.pressed.connect(_watch)
	buttons.add_child(watch_button)
	rename_button = Button.new()
	rename_button.text = "Rename"
	rename_button.pressed.connect(_rename)
	buttons.add_child(rename_button)
	delete_button = Button.new()
	delete_button.text = "Delete"
	delete_button.pressed.connect(_delete)
	buttons.add_child(delete_button)
	var close_button := Button.new()
	close_button.text = "Close"
	close_button.pressed.connect(close)
	buttons.add_child(close_button)


func _selected_entry() -> Dictionary:
	if replay_list == null:
		return {}
	var selected := replay_list.get_selected_items()
	if selected.is_empty():
		return {}
	var index := int(selected[0])
	return entries[index] if index >= 0 and index < entries.size() else {}


func _on_selected(_index: int) -> void:
	var entry := _selected_entry()
	if entry.is_empty():
		metadata_label.text = ""
		name_edit.text = ""
		_update_buttons()
		return
	name_edit.text = str(entry.get("name", entry.get("track_name", "Replay")))
	var player_lines: Array = []
	for player in entry.get("players", []):
		if typeof(player) != TYPE_DICTIONARY:
			continue
		var player_data: Dictionary = player
		var livery: Dictionary = (
			player_data.get("car_livery", {})
			if typeof(player_data.get("car_livery", {})) == TYPE_DICTIONARY else {})
		var stamp_count := (livery.get("stamps", []) as Array).size() \
			if typeof(livery.get("stamps", [])) == TYPE_ARRAY else 0
		player_lines.append("%s%s - %s - %d stamps" % [
			str(player_data.get("username", "Player")),
			" CPU" if bool(player_data.get("cpu", false)) else "",
			str(player_data.get("vehicle_content_id", "")),
			stamp_count,
		])
	var compatibility_status := "current"
	if !_schema_supported(entry):
		compatibility_status = "unsupported replay format"
	elif !_compatible(entry):
		compatibility_status = "older version (may desync)"
	metadata_label.text = "\n".join([
		"Track: %s" % str(entry.get("track_name", "")),
		"Mode: %s" % str(entry.get("mode", "")),
		"Game version: %s" % str(entry.get("build", "")),
		"Godot version: %s" % str(entry.get("engine_version", "")),
		"Duration: %s" % race_presentation_controller.format_race_time(
			int(entry.get("duration_ticks", 0)), 0),
		"Players:",
		"\n".join(player_lines),
		"",
		"Compatibility: %s" % compatibility_status,
		str(entry.get("_path", "")),
	])
	_update_buttons()


func _update_buttons() -> void:
	var entry := _selected_entry()
	var has_entry := !entry.is_empty()
	if watch_button != null:
		watch_button.disabled = !has_entry or !_schema_supported(entry)
	if rename_button != null:
		rename_button.disabled = !has_entry
	if delete_button != null:
		delete_button.disabled = !has_entry


func _watch() -> void:
	var entry := _selected_entry()
	if !entry.is_empty():
		watch_requested.emit(str(entry.get("_path", "")))


func _rename() -> void:
	var entry := _selected_entry()
	if entry.is_empty():
		return
	var path := str(entry.get("_path", ""))
	var stream := MxtReplayStream.new()
	if !stream.load_file(path, true):
		return
	var data := stream.get_metadata()
	data["name"] = name_edit.text.strip_edges()
	if str(data["name"]).is_empty():
		data["name"] = str(data.get("track_name", "Replay"))
	if !stream.rewrite_metadata(path, data):
		push_warning("Replay rename failed: %s" % stream.get_last_error())
		return
	refresh()


func _delete() -> void:
	var entry := _selected_entry()
	if !entry.is_empty():
		DirAccess.remove_absolute(str(entry.get("_path", "")))
		refresh()


func _profile_and_quit() -> void:
	_build()
	var start_usec := Time.get_ticks_usec()
	refresh()
	print("MXT_REPLAY_CATALOG_PROFILE entries=", entries.size(),
		" metadata_us=", Time.get_ticks_usec() - start_usec)
	get_tree().quit()


func _load_metadata(path: String) -> Dictionary:
	if path.is_empty() or !FileAccess.file_exists(path):
		return {}
	var stream := MxtReplayStream.new()
	return stream.get_metadata() if stream.load_file(path, true) else {}


func _schema_supported(data: Dictionary) -> bool:
	return int(data.get("schema_version", -1)) == REPLAY_SCHEMA_VERSION


func _compatible(data: Dictionary) -> bool:
	if !_schema_supported(data):
		return false
	var version = data.get("game_version", {})
	return (
		typeof(version) == TYPE_DICTIONARY
		and int((version as Dictionary).get("major", -1)) == GameVersionData.MAJOR
		and int((version as Dictionary).get("compatibility", -1)) == GameVersionData.COMPATIBILITY)


func _replay_dir() -> String:
	return ProjectSettings.globalize_path("user://replays")
