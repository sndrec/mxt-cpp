class_name TrackEditorDock extends Control

signal dock_ready

const TrackTriggerScript := preload("res://core/track_trigger.gd")

@onready var track_root: TrackRoot = $"../TrackRoot"
@onready var track_scene: TrackEditingScene = $".."

@onready var main_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons
@onready var new_track_segment_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackSegmentButtons
@onready var new_track_segment_type_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackSegmentTypeButtons
@onready var new_embed_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons
@onready var new_track_object_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackObjectButtons
@onready var new_track_segment_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/NewTrackSegment
@onready var edit_segment_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditSegmentProps
@onready var edit_rails_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditRails
@onready var edit_modulation_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditModulation
@onready var edit_shape_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditShape
@onready var edit_mesh_layout_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditMeshLayout
@onready var edit_spiral_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditSpiral
@onready var edit_embeds_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditEmbeds
@onready var edit_checkpoints_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditCheckpoints
@onready var add_embed_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/AddEmbed
@onready var new_track_object_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/NewTrackObject
@onready var edit_track_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditTrackProps
@onready var recharge_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Recharge
@onready var dirt_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Dirt
@onready var slip_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Slip
@onready var lava_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Lava
@onready var gap_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Gap
@onready var dashplate_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackObjectButtons/DashPlate
@onready var jumpplate_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackObjectButtons/Jump
@onready var mine_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackObjectButtons/Mine

@onready var outliner: VBoxContainer = $MainGUI/VBoxContainer/ScrollContainer/Outliner

func _is_script_path(node : Node, script_path : String) -> bool:
	var script : Script = node.get_script() as Script
	return script and script.resource_path == script_path

func _segment_outliner_label(segment : RoadPath, index : int) -> String:
	var segment_type := "Bezier"
	if segment is RoadPathLine:
		segment_type = "Line"
	elif _is_script_path(segment, "res://core/road_path_spiral.gd"):
		segment_type = "Spiral"
	return segment_type + " Track Segment " + str(index + 1)

func _is_track_trigger(node : Node) -> bool:
	return node.get_script() == TrackTriggerScript

func _is_spiral_path(node : Node) -> bool:
	return _is_script_path(node, "res://core/road_path_spiral.gd")

func _track_object_outliner_label(track_object : Node, index : int) -> String:
	return String(track_object.call("trigger_label")) + " " + str(index + 1)

# Called when the node enters the scene tree for the first time.
func _ready():
	get_track_root()
	main_buttons.visible = true
	_enable_tool_button_toggles()
	edit_rails_button.pressed.connect(_on_edit_rails_pressed)
	edit_modulation_button.pressed.connect(_on_edit_modulation_pressed)
	edit_shape_button.pressed.connect(_on_edit_shape_pressed)
	edit_mesh_layout_button.pressed.connect(_on_edit_mesh_layout_pressed)
	edit_spiral_button.pressed.connect(_on_edit_spiral_pressed)
	edit_embeds_button.pressed.connect(_on_edit_embeds_pressed)
	edit_checkpoints_button.pressed.connect(_on_edit_checkpoints_pressed)
	recharge_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.RECHARGE))
	dirt_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.DIRT))
	slip_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.ICE))
	lava_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.LAVA))
	gap_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.HOLE))
	dashplate_button.pressed.connect(_on_track_object_type_pressed.bind(0))
	jumpplate_button.pressed.connect(_on_track_object_type_pressed.bind(1))
	mine_button.pressed.connect(_on_track_object_type_pressed.bind(2))
	track_scene.track_structure_changed.connect(update_outliner)
	FZGlobal.selection_changed.connect(_sync_active_tool_target)
	track_scene.tool_mode_changed.connect(_sync_active_tool_target.unbind(1))
	update_outliner()
	_refresh_tool_button_states()
	dock_ready.emit()

func _enable_tool_button_toggles() -> void:
	for button in [
		new_track_segment_button,
		edit_segment_button,
		edit_shape_button,
		edit_mesh_layout_button,
		edit_spiral_button,
		add_embed_button,
		edit_embeds_button,
		edit_rails_button,
		edit_modulation_button,
		edit_checkpoints_button,
		new_track_object_button,
		edit_track_button,
	]:
		button.toggle_mode = true

func _set_tool_button_pressed(button : Button, pressed : bool) -> void:
	button.set_pressed_no_signal(pressed)

func _refresh_tool_button_states() -> void:
	var mode := track_scene.tool_mode
	_set_tool_button_pressed(new_track_segment_button, mode == TrackEditingScene.ToolMode.ADD_SEGMENT)
	_set_tool_button_pressed(edit_segment_button, mode == TrackEditingScene.ToolMode.EDIT_SEGMENT)
	_set_tool_button_pressed(edit_shape_button, mode == TrackEditingScene.ToolMode.EDIT_SHAPE)
	_set_tool_button_pressed(edit_mesh_layout_button, mode == TrackEditingScene.ToolMode.EDIT_MESH_LAYOUT)
	_set_tool_button_pressed(edit_spiral_button, mode == TrackEditingScene.ToolMode.EDIT_SPIRAL)
	_set_tool_button_pressed(add_embed_button, mode == TrackEditingScene.ToolMode.ADD_EMBED)
	_set_tool_button_pressed(edit_embeds_button, mode == TrackEditingScene.ToolMode.EDIT_EMBED)
	_set_tool_button_pressed(edit_rails_button, mode == TrackEditingScene.ToolMode.EDIT_RAILS)
	_set_tool_button_pressed(edit_modulation_button, mode == TrackEditingScene.ToolMode.EDIT_MODULATION)
	_set_tool_button_pressed(edit_checkpoints_button, mode == TrackEditingScene.ToolMode.EDIT_CHECKPOINTS)
	_set_tool_button_pressed(new_track_object_button, mode == TrackEditingScene.ToolMode.ADD_OBJECT)
	_set_tool_button_pressed(edit_track_button, mode == TrackEditingScene.ToolMode.EDIT_TRACK)

func get_track_root() -> void:
	if !track_root:
		var scene_root := get_tree().current_scene
		if scene_root:
			for child in scene_root.get_children():
				if child is TrackRoot:
					track_root = child
					break

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta):
	get_track_root()
	_refresh_tool_button_states()

func update_outliner() -> void:
	if !track_root:
		return
	for child in outliner.get_children():
		child.queue_free()
	for i in track_root.get_child_count():
		var child = track_root.get_child(i)
		if child is RoadPath:
			var new_button := Button.new()
			new_button.text = _segment_outliner_label(child, i)
			new_button.pressed.connect(select_node.bind(child))
			outliner.add_child(new_button)
		elif _is_track_trigger(child):
			var new_button := Button.new()
			new_button.text = _track_object_outliner_label(child, i)
			new_button.pressed.connect(select_node.bind(child))
			outliner.add_child(new_button)

func select_node(in_node : Node) -> void:
	if in_node is Node3D:
		FZGlobal.select_node(in_node)
	_sync_active_tool_target()

func _sync_active_tool_target() -> void:
	var in_node := get_active_path()
	if !(in_node is RoadPath):
		track_scene.active_path = null
		track_scene.set_active_rail_path(null)
		track_scene.set_active_modulation(null, -1)
		track_scene.set_active_shape_path(null)
		track_scene.set_active_mesh_layout_path(null)
		track_scene.set_active_spiral_path(null)
		track_scene.set_active_embed(null, -1)
		track_scene.set_active_checkpoint_path(null)
		return
	if in_node is RoadPath:
		track_scene.active_path = in_node
		if track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_RAILS:
			track_scene.set_active_rail_path(in_node)
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_MODULATION:
			track_scene.set_active_modulation(in_node, _ensure_active_modulation(in_node))
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_SHAPE:
			track_scene.set_active_shape_path(in_node)
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_MESH_LAYOUT:
			track_scene.set_active_mesh_layout_path(in_node)
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_SPIRAL:
			track_scene.set_active_spiral_path(in_node)
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_EMBED:
			var embed_index := _active_embed_index(in_node)
			if embed_index >= 0:
				track_scene.set_active_embed(in_node, embed_index)
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_CHECKPOINTS:
			track_scene.set_active_checkpoint_path(in_node)

func get_active_path() -> RoadPath:
	var selected := FZGlobal.get_selected_road_path()
	if selected:
		return selected
	return track_scene.active_path

func _on_new_track_segment_pressed():
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.ADD_SEGMENT)
	main_buttons.visible = false
	new_track_segment_buttons.visible = true

func _on_add_embed_pressed():
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.ADD_EMBED)
	track_scene.pending_embed_add = false
	main_buttons.visible = false
	new_embed_buttons.visible = true

func _on_new_track_object_pressed():
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.ADD_OBJECT)
	main_buttons.visible = false
	new_track_object_buttons.visible = true

func _on_edit_segment_props_pressed():
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SEGMENT)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false


func _on_edit_track_props_pressed():
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_TRACK)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_edit_rails_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	track_scene.set_active_rail_path(selected)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_RAILS)
	select_node(selected)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _ensure_active_modulation(path : RoadPath) -> int:
	if path.road_shape.modulation_table.is_empty():
		var new_mod := RoadModulation.new()
		new_mod.modulation_height.set_point_value(0, 1.0)
		new_mod.modulation_height.set_point_value(1, 1.0)
		path.road_shape.modulation_table.append(new_mod)
	return 0

func _active_embed_index(path : RoadPath) -> int:
	if !path or path.road_shape.embed_table.is_empty():
		return -1
	return 0

func _on_edit_modulation_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	var modulation_index := _ensure_active_modulation(selected)
	track_scene.set_active_modulation(selected, modulation_index)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_MODULATION)
	select_node(selected)
	selected._try_generate_mesh()
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_edit_shape_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	track_scene.set_active_shape_path(selected)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SHAPE)
	select_node(selected)
	selected._try_generate_mesh()
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_edit_mesh_layout_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	track_scene.set_active_mesh_layout_path(selected)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_MESH_LAYOUT)
	select_node(selected)
	selected._try_generate_mesh()
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_edit_spiral_pressed() -> void:
	var selected := get_active_path()
	if !_is_spiral_path(selected):
		return
	track_scene.set_active_spiral_path(selected)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SPIRAL)
	select_node(selected)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_edit_embeds_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	var embed_index := _active_embed_index(selected)
	if embed_index < 0:
		_on_add_embed_pressed()
		return
	track_scene.set_active_embed(selected, embed_index)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_EMBED)
	select_node(selected)
	selected._try_generate_mesh()
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_edit_checkpoints_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	track_scene.set_active_checkpoint_path(selected)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_CHECKPOINTS)
	select_node(selected)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false

func _on_embed_type_pressed(embed_type : int) -> void:
	track_scene.desired_embed_type = embed_type
	track_scene.pending_embed_add = true
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.ADD_EMBED)
	new_embed_buttons.visible = false
	main_buttons.visible = true

func _on_track_object_type_pressed(trigger_type : int) -> void:
	track_scene.desired_trigger_type = trigger_type
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.ADD_OBJECT)
	new_track_object_buttons.visible = false
	main_buttons.visible = true


func _on_road_standard_pressed():
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.STANDARD


func _on_road_pipe_pressed():
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.PIPE



func _on_road_cylinder_pressed():
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.CYLINDER

func _on_road_cylinder_open_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.CYLINDER_OPEN

func _on_road_pipe_open_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.PIPE_OPEN

func _on_road_rounded_square_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.ROUNDED_SQUARE

func _on_road_rounded_square_open_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	track_scene.desired_road_type = ENUMS.ROAD_TYPE.ROUNDED_SQUARE_OPEN

func _on_road_line_pressed() -> void:
	var selected := get_active_path()
	var new_track_piece : RoadPath
	if selected:
		new_track_piece = track_scene.add_regular_track_segment_after(selected, track_scene.desired_road_type)
	elif track_root.get_child_count() > 0:
		new_track_piece = track_scene.add_regular_track_segment_after(track_root.get_child(track_root.get_child_count() - 1), track_scene.desired_road_type)
	if new_track_piece:
		select_node(new_track_piece)
	update_outliner()
	new_track_segment_type_buttons.visible = false
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SEGMENT)
	main_buttons.visible = true



func _on_road_bezier_pressed() -> void:
	var selected := get_active_path()
	var new_track_piece : RoadPath
	if selected:
		new_track_piece = track_scene.add_bezier_track_segment_after(selected, track_scene.desired_road_type)
	elif track_root.get_child_count() > 0:
		new_track_piece = track_scene.add_bezier_track_segment_after(track_root.get_child(track_root.get_child_count() - 1), track_scene.desired_road_type)
	if new_track_piece:
		select_node(new_track_piece)
	update_outliner()
	new_track_segment_type_buttons.visible = false
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SEGMENT)
	main_buttons.visible = true


func _on_road_curve_pressed() -> void:
	var selected := get_active_path()
	var new_track_piece : RoadPath
	if selected:
		new_track_piece = track_scene.add_spiral_track_segment_after(selected, track_scene.desired_road_type)
	elif track_root.get_child_count() > 0:
		new_track_piece = track_scene.add_spiral_track_segment_after(track_root.get_child(track_root.get_child_count() - 1), track_scene.desired_road_type)
	if new_track_piece:
		select_node(new_track_piece)
	update_outliner()
	new_track_segment_type_buttons.visible = false
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SEGMENT)
	main_buttons.visible = true
