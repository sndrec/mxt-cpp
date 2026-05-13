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
@onready var bezier_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/BezierButtons
@onready var edit_rails_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditRails
@onready var edit_modulation_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditModulation
@onready var edit_shape_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditShape
@onready var edit_spiral_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditSpiral
@onready var edit_checkpoints_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons/EditCheckpoints
@onready var recharge_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Recharge
@onready var dirt_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Dirt
@onready var slip_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Slip
@onready var lava_button: Button = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons/Lava
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
	edit_rails_button.pressed.connect(_on_edit_rails_pressed)
	edit_modulation_button.pressed.connect(_on_edit_modulation_pressed)
	edit_shape_button.pressed.connect(_on_edit_shape_pressed)
	edit_spiral_button.pressed.connect(_on_edit_spiral_pressed)
	edit_checkpoints_button.pressed.connect(_on_edit_checkpoints_pressed)
	recharge_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.RECHARGE))
	dirt_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.DIRT))
	slip_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.ICE))
	lava_button.pressed.connect(_on_embed_type_pressed.bind(RoadEmbed.EmbedType.LAVA))
	dashplate_button.pressed.connect(_on_track_object_type_pressed.bind(0))
	jumpplate_button.pressed.connect(_on_track_object_type_pressed.bind(1))
	mine_button.pressed.connect(_on_track_object_type_pressed.bind(2))
	track_scene.track_structure_changed.connect(update_outliner)
	update_outliner()
	dock_ready.emit()

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
	if in_node is RoadPath:
		track_scene.active_path = in_node
		if track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_RAILS:
			track_scene.set_active_rail_path(in_node)
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_MODULATION:
			track_scene.set_active_modulation(in_node, _ensure_active_modulation(in_node))
		elif track_scene.tool_mode == TrackEditingScene.ToolMode.EDIT_SHAPE:
			track_scene.set_active_shape_path(in_node)
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
	bezier_buttons.visible = FZGlobal.active_node is BezierHandle


func _on_edit_track_props_pressed():
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_TRACK)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false
	bezier_buttons.visible = false

func _on_edit_rails_pressed() -> void:
	var selected := get_active_path()
	if !selected:
		return
	track_scene.set_active_rail_path(selected)
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_RAILS)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false
	bezier_buttons.visible = false

func _ensure_active_modulation(path : RoadPath) -> int:
	if path.road_shape.modulation_table.is_empty():
		var new_mod := RoadModulation.new()
		new_mod.modulation_height.set_point_value(0, 1.0)
		new_mod.modulation_height.set_point_value(1, 1.0)
		path.road_shape.modulation_table.append(new_mod)
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
	bezier_buttons.visible = false

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
	bezier_buttons.visible = false

func _on_edit_spiral_pressed() -> void:
	var selected := get_active_path()
	if !_is_spiral_path(selected):
		return
	track_scene.set_tool_mode(TrackEditingScene.ToolMode.EDIT_SPIRAL)
	select_node(selected)
	main_buttons.visible = true
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = false
	new_embed_buttons.visible = false
	new_track_object_buttons.visible = false
	bezier_buttons.visible = false

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
	bezier_buttons.visible = false

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
	bezier_buttons.visible = false


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

func retrieve_point_int_from_string(in_name : String) -> int:
	var cut := in_name.erase(0, 6)
	var space_pos := cut.find(" ")
	var truncate := cut
	if space_pos != -1:
		truncate = cut.erase(space_pos, 100)
	var num = int(truncate)
	return num

func _selected_bezier_handle() -> BezierHandle:
	if FZGlobal.active_node is BezierHandle:
		return FZGlobal.active_node as BezierHandle
	return null

func _insert_bezier_point_at_handle(insert_after : bool) -> void:
	var handle := _selected_bezier_handle()
	if !handle:
		return
	var bez_seg := handle.get_parent() as RoadPathBezier
	if !bez_seg or bez_seg is RoadPathLine:
		return
	var point_count := bez_seg.get_control_point_count()
	if point_count < 2:
		return
	var source_index := clampi(handle.associated_index, 0, point_count - 1)
	var left_index := source_index
	var right_index := source_index + 1
	if !insert_after:
		left_index = source_index - 1
		right_index = source_index
	var insert_index := right_index
	var in_time := 0.0
	var in_transform := Transform3D.IDENTITY
	var in_scale := Vector3.ONE
	var handle_in := 166.0
	var handle_out := 166.0
	if left_index >= 0 and right_index < point_count:
		var time_1 : float = bez_seg.native_curve.get_control_point_time(left_index)
		var time_2 : float = bez_seg.native_curve.get_control_point_time(right_index)
		var handle_out_1 : float = bez_seg.native_curve.get_control_point_handle_out(left_index)
		var handle_in_2 : float = bez_seg.native_curve.get_control_point_handle_in(right_index)
		in_time = lerpf(time_1, time_2, 0.5)
		in_transform = bez_seg.get_root_transform(in_time)
		in_scale = bez_seg.native_curve.get_control_point_scale(left_index).lerp(bez_seg.native_curve.get_control_point_scale(right_index), 0.5)
		handle_in = handle_out_1 * 0.5
		handle_out = handle_in_2 * 0.5
		bez_seg.native_curve.set_control_point_handles(left_index, bez_seg.native_curve.get_control_point_handle_in(left_index), handle_out_1 * 0.5)
		bez_seg.native_curve.set_control_point_handles(right_index, handle_in_2 * 0.5, bez_seg.native_curve.get_control_point_handle_out(right_index))
	else:
		var edge_index := source_index
		in_time = 1.0 if insert_after else 0.0
		in_transform = bez_seg.get_root_transform(in_time)
		in_scale = bez_seg.native_curve.get_control_point_scale(edge_index)
		var edge_handle := maxf(32.0, bez_seg.native_curve.get_control_point_handle_out(edge_index) if insert_after else bez_seg.native_curve.get_control_point_handle_in(edge_index))
		in_transform.origin += in_transform.basis.z.normalized() * edge_handle * (3.0 if insert_after else -3.0)
		insert_index = point_count if insert_after else 0
		handle_in = edge_handle
		handle_out = edge_handle
	bez_seg.add_control_point(
		in_time,
		in_transform.origin,
		in_transform.basis,
		in_scale,
		handle_in,
		handle_out,
		insert_index)
	if insert_index < bez_seg.bezier_handle_nodes.size():
		select_node(bez_seg.bezier_handle_nodes[insert_index])

func _on_add_point_before_button_down():
	_insert_bezier_point_at_handle(false)

func _on_add_point_after_pressed():
	_insert_bezier_point_at_handle(true)


func _on_delete_point_button_down():
	var sel = FZGlobal.active_node
	if sel is BezierHandle:
		var pt_handle : BezierHandle = sel as BezierHandle
		var num := pt_handle.associated_index + 1
		if num <= 0:
			num = retrieve_point_int_from_string(pt_handle.name)
		var bez_seg := sel.get_parent() as RoadPathBezier
		if bez_seg is RoadPathLine:
			return
		bez_seg.remove_bezier_point_at_index(num - 1)
		select_node(bez_seg)


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
