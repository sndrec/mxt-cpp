class_name TrackEditorDock extends Control

signal dock_ready

@onready var track_root: TrackRoot = $"../TrackRoot"
@onready var track_scene: TrackEditingScene = $".."

@onready var main_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/MainButtons
@onready var new_track_segment_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackSegmentButtons
@onready var new_track_segment_type_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackSegmentTypeButtons
@onready var new_embed_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewEmbedButtons
@onready var new_track_object_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/NewTrackObjectButtons
@onready var bezier_buttons: VBoxContainer = $MainGUI/VBoxContainer/MainGUIMargin/MainGUIHBox/BezierButtons

@onready var outliner: VBoxContainer = $MainGUI/VBoxContainer/ScrollContainer/Outliner


var desired_road_type := ENUMS.ROAD_TYPE.STANDARD

# Called when the node enters the scene tree for the first time.
func _ready():
	get_track_root()
	main_buttons.visible = true
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
			new_button.text = "Track Segment " + str(i + 1)
			new_button.pressed.connect(select_node.bind(child))
			outliner.add_child(new_button)

func select_node(in_node : Node) -> void:
	if in_node is Node3D:
		FZGlobal.select_node(in_node)
	if in_node is RoadPath:
		track_scene.active_path = in_node

func get_active_path() -> RoadPath:
	var selected := FZGlobal.get_selected_road_path()
	if selected:
		return selected
	return track_scene.active_path

func _on_new_track_segment_pressed():
	main_buttons.visible = false
	new_track_segment_buttons.visible = true

func _on_add_embed_pressed():
	main_buttons.visible = false
	new_embed_buttons.visible = true

func _on_new_track_object_pressed():
	main_buttons.visible = false
	new_track_object_buttons.visible = true

func _on_edit_segment_props_pressed():
	pass # Replace with function body.


func _on_edit_track_props_pressed():
	pass # Replace with function body.


func _on_road_standard_pressed():
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.STANDARD


func _on_road_pipe_pressed():
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.PIPE



func _on_road_cylinder_pressed():
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.CYLINDER

func _on_road_cylinder_open_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.CYLINDER_OPEN

func _on_road_pipe_open_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.PIPE_OPEN

func _on_road_rounded_square_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.ROUNDED_SQUARE

func _on_road_rounded_square_open_pressed() -> void:
	new_track_segment_buttons.visible = false
	new_track_segment_type_buttons.visible = true
	desired_road_type = ENUMS.ROAD_TYPE.ROUNDED_SQUARE_OPEN

func retrieve_point_int_from_string(in_name : String) -> int:
	var cut := in_name.erase(0, 6)
	var space_pos := cut.find(" ")
	var truncate := cut
	if space_pos != -1:
		truncate = cut.erase(space_pos, 100)
	var num = int(truncate)
	return num


func _on_add_point_before_button_down():
	pass # Replace with function body.


func _on_delete_point_button_down():
	var sel = FZGlobal.active_node
	if sel is Marker3D:
		var pt_handle : Marker3D = sel as Marker3D
		var num = retrieve_point_int_from_string(pt_handle.name)
		var bez_seg := sel.get_parent() as RoadPathBezier
		if bez_seg is RoadPathLine:
			return
		bez_seg.remove_bezier_point_at_index(num - 1)


func _on_road_line_pressed() -> void:
	var selected := get_active_path()
	var new_track_piece : RoadPath
	if selected:
		new_track_piece = track_scene.add_regular_track_segment_after(selected, desired_road_type)
	elif track_root.get_child_count() > 0:
		new_track_piece = track_scene.add_regular_track_segment_after(track_root.get_child(track_root.get_child_count() - 1), desired_road_type)
	if new_track_piece:
		select_node(new_track_piece)
	update_outliner()
	new_track_segment_type_buttons.visible = false
	main_buttons.visible = true



func _on_road_bezier_pressed() -> void:
	var selected := get_active_path()
	var new_track_piece : RoadPath
	if selected:
		new_track_piece = track_scene.add_bezier_track_segment_after(selected, desired_road_type)
	elif track_root.get_child_count() > 0:
		new_track_piece = track_scene.add_bezier_track_segment_after(track_root.get_child(track_root.get_child_count() - 1), desired_road_type)
	if new_track_piece:
		select_node(new_track_piece)
	update_outliner()
	new_track_segment_type_buttons.visible = false
	main_buttons.visible = true


func _on_road_curve_pressed() -> void:
	new_track_segment_type_buttons.visible = false
	main_buttons.visible = true
