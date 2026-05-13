class_name TrackEditorDock2 extends Control

signal dock_ready

signal update_track

const TrackTriggerScript := preload("res://core/track_trigger.gd")
const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")

var track_root : TrackRoot

var current_path : RoadPath

@onready var draw_mesh: CheckBox = $Control/VBoxContainer/DrawMesh
@onready var draw_curve: CheckBox = $Control/VBoxContainer/DrawCurve
@onready var draw_handles: CheckBox = $Control/VBoxContainer/DrawHandles

@onready var track_cross_section_slider: HSlider = $Control/TabContainer/Info/VBoxContainer/TrackCrossSectionSlider
@onready var tab_container: TabContainer = $Control/TabContainer
@onready var info_panel: VBoxContainer = $Control/TabContainer/Info
@onready var smooth_curve: Line2D = $Control/TabContainer/Info/VBoxContainer/ColorRect/SmoothCurve
@onready var poly_curve: Line2D = $Control/TabContainer/Info/VBoxContainer/ColorRect/PolyCurve
@onready var road_shape_row: HBoxContainer = $Control/TabContainer/Info/RoadShapeRow
@onready var road_shape_type: OptionButton = $Control/TabContainer/Info/RoadShapeRow/RoadShapeType
@onready var road_uv_multiplier_row: HBoxContainer = $Control/TabContainer/Info/RoadUvMultiplierRow
@onready var road_uv_multiplier: SpinBox = $Control/TabContainer/Info/RoadUvMultiplierRow/RoadUvMultiplier
@onready var ground_color_row: HBoxContainer = $Control/TabContainer/Info/GroundColorRow
@onready var ground_color: ColorPickerButton = $Control/TabContainer/Info/GroundColorRow/GroundColor
@onready var rail_color_row: HBoxContainer = $Control/TabContainer/Info/RailColorRow
@onready var rail_color: ColorPickerButton = $Control/TabContainer/Info/RailColorRow/RailColor
@onready var mesh_subdivision_length_row: HBoxContainer = $Control/TabContainer/Info/MeshSubdivisionLengthRow
@onready var mesh_subdivision_length: SpinBox = $Control/TabContainer/Info/MeshSubdivisionLengthRow/MeshSubdivisionLength
@onready var mesh_subdivision_angle_row: HBoxContainer = $Control/TabContainer/Info/MeshSubdivisionAngleRow
@onready var mesh_subdivision_angle: SpinBox = $Control/TabContainer/Info/MeshSubdivisionAngleRow/MeshSubdivisionAngle
@onready var checkpoint_count_row: HBoxContainer = $Control/TabContainer/Info/CheckpointCountRow
@onready var checkpoint_count: SpinBox = $Control/TabContainer/Info/CheckpointCountRow/CheckpointCount
@onready var cross_section_controls: VBoxContainer = $Control/TabContainer/Info/VBoxContainer

@onready var spiral_panel: ScrollContainer = $Control/TabContainer/Spiral
@onready var spiral_degrees: SpinBox = $Control/TabContainer/Spiral/VBoxContainer/SpiralDegreesRow/SpiralDegrees
@onready var spiral_axis_x: SpinBox = $Control/TabContainer/Spiral/VBoxContainer/SpiralAxisRow/SpiralAxisX
@onready var spiral_axis_y: SpinBox = $Control/TabContainer/Spiral/VBoxContainer/SpiralAxisRow/SpiralAxisY
@onready var spiral_axis_z: SpinBox = $Control/TabContainer/Spiral/VBoxContainer/SpiralAxisRow/SpiralAxisZ

@onready var rail_panel: ScrollContainer = $Control/TabContainer/Rails
@onready var left_rail_height: SpinBox = $Control/TabContainer/Rails/VBoxContainer/LeftRailHeightRow/LeftRailHeight
@onready var left_rail_start: SpinBox = $Control/TabContainer/Rails/VBoxContainer/LeftRailStartRow/LeftRailStart
@onready var left_rail_end: SpinBox = $Control/TabContainer/Rails/VBoxContainer/LeftRailEndRow/LeftRailEnd
@onready var right_rail_height: SpinBox = $Control/TabContainer/Rails/VBoxContainer/RightRailHeightRow/RightRailHeight
@onready var right_rail_start: SpinBox = $Control/TabContainer/Rails/VBoxContainer/RightRailStartRow/RightRailStart
@onready var right_rail_end: SpinBox = $Control/TabContainer/Rails/VBoxContainer/RightRailEndRow/RightRailEnd
@onready var rail_mode_color: ColorPickerButton = $Control/TabContainer/Rails/VBoxContainer/RailColorRow/RailColor

@onready var track_panel: ScrollContainer = $Control/TabContainer/Track
@onready var track_name_edit: LineEdit = $Control/TabContainer/Track/VBoxContainer/TrackName
@onready var track_description_edit: LineEdit = $Control/TabContainer/Track/VBoxContainer/TrackDescription
@onready var track_difficulty_edit: SpinBox = $Control/TabContainer/Track/VBoxContainer/TrackDifficultyRow/TrackDifficulty

@onready var object_panel: ScrollContainer = $Control/TabContainer/Object
@onready var object_type: OptionButton = $Control/TabContainer/Object/VBoxContainer/ObjectType
@onready var remove_track_object: Button = $Control/TabContainer/Object/VBoxContainer/RemoveTrackObject
@onready var object_tx: SpinBox = $Control/TabContainer/Object/VBoxContainer/ObjectSurfaceRow/ObjectTX
@onready var object_ty: SpinBox = $Control/TabContainer/Object/VBoxContainer/ObjectSurfaceRow/ObjectTY
@onready var object_yaw: SpinBox = $Control/TabContainer/Object/VBoxContainer/ObjectYaw
@onready var object_scale_x: SpinBox = $Control/TabContainer/Object/VBoxContainer/ObjectScaleRow/ObjectScaleX
@onready var object_scale_y: SpinBox = $Control/TabContainer/Object/VBoxContainer/ObjectScaleRow/ObjectScaleY
@onready var object_scale_z: SpinBox = $Control/TabContainer/Object/VBoxContainer/ObjectScaleRow/ObjectScaleZ

@onready var modulation_dropdown: OptionButton = $Control/TabContainer/Modulation/VBoxContainer/HBoxContainer/ModulationDropdown
@onready var new_modulation: Button = $Control/TabContainer/Modulation/VBoxContainer/HBoxContainer/NewModulation
@onready var remove_mod_button: Button = $Control/TabContainer/Modulation/VBoxContainer/HBoxContainer/RemoveModulation

@onready var new_embed: Button = $Control/TabContainer/Embeds/VBoxContainer/HBoxContainer/NewEmbed
@onready var remove_embed: Button = $Control/TabContainer/Embeds/VBoxContainer/HBoxContainer/RemoveEmbed
@onready var embed_dropdown: OptionButton = $Control/TabContainer/Embeds/VBoxContainer/HBoxContainer/EmbedDropdown
@onready var embed_type: OptionButton = $Control/TabContainer/Embeds/VBoxContainer/EmbedType
@onready var embed_start: HSlider = $Control/TabContainer/Embeds/VBoxContainer/EmbedStart
@onready var embed_end: HSlider = $Control/TabContainer/Embeds/VBoxContainer/EmbedEnd

@onready var modulation: ScrollContainer = $Control/TabContainer/Modulation
@onready var embeds: ScrollContainer = $Control/TabContainer/Embeds

@onready var cs_rect: CurveCrossSection = $Control/TabContainer/Info/VBoxContainer/ColorRect

@onready var copy_mesh_layout_button: Button = $Control/TabContainer/Info/VBoxContainer/HBoxContainer/CopyMeshLayout
@onready var paste_mesh_layout_button: Button = $Control/TabContainer/Info/VBoxContainer/HBoxContainer/PasteMeshLayout
@onready var mesh_layout_clipboard_row: HBoxContainer = $Control/TabContainer/Info/VBoxContainer/HBoxContainer
@onready var create_mesh_layout_button: Button = $Control/TabContainer/Info/VBoxContainer/HBoxContainer2/CreateMeshLayout
@onready var mesh_layout_count: SpinBox = $Control/TabContainer/Info/VBoxContainer/HBoxContainer2/MeshLayoutCount
@onready var mesh_layout_create_row: HBoxContainer = $Control/TabContainer/Info/VBoxContainer/HBoxContainer2

@onready var bez_pos_x: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer/HandlePosX
@onready var bez_pos_y: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer2/HandlePosY
@onready var bez_pos_z: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer3/HandlePosZ
@onready var bez_rot_x: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer4/HandleRotP
@onready var bez_rot_y: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer5/HandleRotY
@onready var bez_rot_z: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer6/HandleRotR
@onready var bez_scale_w: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer7/HandleScaleW
@onready var bez_scale_h: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer8/HandleScaleH
@onready var bez_weight_i: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer9/HandleWeightI
@onready var bez_weight_o: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer10/HandleWeightO
@onready var bez_rot_ease_type: OptionButton = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer12/RotEaseType
@onready var bez_rot_ease_strength: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer12/RotEaseStrength
@onready var bez_twist_ease_type: OptionButton = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer13/TwistEaseType
@onready var bez_twist_ease_strength: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer13/TwistEaseStrength
@onready var bez_scale_ease_type: OptionButton = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer14/ScaleEaseType
@onready var bez_scale_ease_strength: SpinBox = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer14/ScaleEaseStrength

@onready var line_pos_x: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer/HandlePosX
@onready var line_pos_y: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer2/HandlePosY
@onready var line_pos_z: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer3/HandlePosZ
@onready var line_rot_x: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer4/HandleRotP
@onready var line_rot_y: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer5/HandleRotY
@onready var line_rot_z: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer6/HandleRotR
@onready var line_scale_w: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer7/HandleScaleW
@onready var line_scale_h: SpinBox = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer8/HandleScaleH

@onready var copy_transform_button_1: Button = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer11/CopyTransformButton1
@onready var paste_transform_button_1: Button = $Control/VBoxContainer/DataEditor/BezierHandleData/HBoxContainer11/PasteTransformButton1
@onready var copy_transform_button_2: Button = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer11/CopyTransformButton2
@onready var paste_transform_button_2: Button = $Control/VBoxContainer/DataEditor/LineHandleData/HBoxContainer11/PasteTransformButton2

var transform_clipboard := Transform3D.IDENTITY
var road_poly_clipboard := PackedFloat32Array()
var cross_section_mesh_instance : MeshInstance3D
var cross_section_mesh := ImmediateMesh.new()
var cross_section_material := StandardMaterial3D.new()
var connected_tool_scene : TrackEditingScene
var updating_spiral_controls := false
var updating_rail_controls := false
var updating_track_controls := false
var updating_object_controls := false
var updating_road_shape_controls := false
var updating_road_uv_controls := false
var updating_road_color_controls := false
var updating_mesh_subdivision_controls := false
var updating_embed_controls := false
var updating_handle_controls := false
var transform_clipboard_cp_scale := Vector3.ONE

func _ensure_cross_section_visual() -> void:
	if cross_section_mesh_instance:
		return
	cross_section_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	cross_section_material.vertex_color_use_as_albedo = true
	cross_section_mesh_instance = MeshInstance3D.new()
	cross_section_mesh_instance.mesh = cross_section_mesh
	cross_section_mesh_instance.top_level = true
	if FZGlobal.editing_scene:
		FZGlobal.editing_scene.add_child(cross_section_mesh_instance)
	else:
		add_child(cross_section_mesh_instance)

func _hide_cross_section_visual() -> void:
	if cross_section_mesh_instance:
		cross_section_mesh_instance.visible = false

func _update_cross_section_visual(points : PackedVector3Array) -> void:
	_ensure_cross_section_visual()
	cross_section_mesh.clear_surfaces()
	if points.size() < 2:
		cross_section_mesh_instance.visible = false
		return
	cross_section_mesh.surface_begin(Mesh.PRIMITIVE_LINES, cross_section_material)
	cross_section_mesh.surface_set_color(Color.RED)
	for i in range(0, points.size() - 1, 2):
		cross_section_mesh.surface_add_vertex(points[i])
		cross_section_mesh.surface_add_vertex(points[i + 1])
	cross_section_mesh.surface_end()
	cross_section_mesh_instance.visible = true

func get_runtime_track_root() -> TrackRoot:
	if FZGlobal.current_track:
		return FZGlobal.current_track
	var scene_root := get_tree().current_scene
	if !scene_root:
		return null
	for child in scene_root.get_children():
		if child is TrackRoot:
			return child
	return null

func get_active_node() -> Node3D:
	return FZGlobal.active_node

func _ready():
	dock_ready.emit()
	if !FZGlobal.selection_changed.is_connected(selection_updated):
		FZGlobal.selection_changed.connect(selection_updated)
	_sync_tool_mode_signal()
	embed_dropdown.item_selected.connect(_on_embed_selected)
	modulation_dropdown.item_selected.connect(_on_modulation_selected)
	new_modulation.pressed.connect(add_new_modulation)
	remove_mod_button.pressed.connect(remove_modulation)
	new_embed.pressed.connect(add_new_embed)
	remove_embed.pressed.connect(remove_embed_func)
	embed_start.value_changed.connect(update_embed_start_value)
	embed_end.value_changed.connect(update_embed_end_value)
	road_shape_type.item_selected.connect(update_road_shape_type)
	road_uv_multiplier.value_changed.connect(update_road_uv_multiplier)
	ground_color.color_changed.connect(update_ground_color)
	rail_color.color_changed.connect(update_rail_color)
	mesh_subdivision_length.value_changed.connect(update_mesh_subdivision_length)
	mesh_subdivision_angle.value_changed.connect(update_mesh_subdivision_angle)
	track_cross_section_slider.value_changed.connect(cs_rect.update_track_cross_sections)
	checkpoint_count.value_changed.connect(update_checkpoint_count)
	spiral_degrees.value_changed.connect(update_spiral_values)
	spiral_axis_x.value_changed.connect(update_spiral_values)
	spiral_axis_y.value_changed.connect(update_spiral_values)
	spiral_axis_z.value_changed.connect(update_spiral_values)
	left_rail_height.value_changed.connect(update_rail_values)
	left_rail_start.value_changed.connect(update_rail_values)
	left_rail_end.value_changed.connect(update_rail_values)
	right_rail_height.value_changed.connect(update_rail_values)
	right_rail_start.value_changed.connect(update_rail_values)
	right_rail_end.value_changed.connect(update_rail_values)
	rail_mode_color.color_changed.connect(update_rail_color)
	track_name_edit.text_changed.connect(update_track_name)
	track_description_edit.text_changed.connect(update_track_description)
	track_difficulty_edit.value_changed.connect(update_track_difficulty)
	object_type.item_selected.connect(update_track_object_type)
	remove_track_object.pressed.connect(remove_track_object_func)
	object_tx.value_changed.connect(update_track_object_values)
	object_ty.value_changed.connect(update_track_object_values)
	object_yaw.value_changed.connect(update_track_object_values)
	object_scale_x.value_changed.connect(update_track_object_values)
	object_scale_y.value_changed.connect(update_track_object_values)
	object_scale_z.value_changed.connect(update_track_object_values)
	copy_mesh_layout_button.pressed.connect(copy_mesh_layout)
	paste_mesh_layout_button.pressed.connect(paste_mesh_layout)
	create_mesh_layout_button.pressed.connect(create_simple_mesh_layout)
	copy_transform_button_1.pressed.connect(copy_transform)
	copy_transform_button_2.pressed.connect(copy_transform)
	paste_transform_button_1.pressed.connect(paste_transform)
	paste_transform_button_2.pressed.connect(paste_transform)
	bez_pos_x.value_changed.connect(update_handle_properties)
	bez_pos_y.value_changed.connect(update_handle_properties)
	bez_pos_z.value_changed.connect(update_handle_properties)
	bez_rot_x.value_changed.connect(update_handle_properties)
	bez_rot_y.value_changed.connect(update_handle_properties)
	bez_rot_z.value_changed.connect(update_handle_properties)
	bez_scale_w.value_changed.connect(update_handle_properties)
	bez_scale_h.value_changed.connect(update_handle_properties)
	bez_weight_i.value_changed.connect(update_handle_properties)
	bez_weight_o.value_changed.connect(update_handle_properties)
	bez_rot_ease_type.item_selected.connect(update_handle_ease_type)
	bez_rot_ease_strength.value_changed.connect(update_handle_properties)
	bez_twist_ease_type.item_selected.connect(update_handle_ease_type)
	bez_twist_ease_strength.value_changed.connect(update_handle_properties)
	bez_scale_ease_type.item_selected.connect(update_handle_ease_type)
	bez_scale_ease_strength.value_changed.connect(update_handle_properties)
	line_pos_x.value_changed.connect(update_handle_properties)
	line_pos_y.value_changed.connect(update_handle_properties)
	line_pos_z.value_changed.connect(update_handle_properties)
	line_rot_x.value_changed.connect(update_handle_properties)
	line_rot_y.value_changed.connect(update_handle_properties)
	line_rot_z.value_changed.connect(update_handle_properties)
	line_scale_w.value_changed.connect(update_handle_properties)
	line_scale_h.value_changed.connect(update_handle_properties)
	embed_type.item_selected.connect(update_embed_type)
	selection_updated()

func _sync_tool_mode_signal() -> void:
	var tool_scene := FZGlobal.editing_scene as TrackEditingScene
	if tool_scene == connected_tool_scene:
		return
	if connected_tool_scene and connected_tool_scene.tool_mode_changed.is_connected(_refresh_contextual_visibility):
		connected_tool_scene.tool_mode_changed.disconnect(_refresh_contextual_visibility)
	if connected_tool_scene and connected_tool_scene.track_structure_changed.is_connected(selection_updated):
		connected_tool_scene.track_structure_changed.disconnect(selection_updated)
	connected_tool_scene = tool_scene
	if connected_tool_scene and !connected_tool_scene.tool_mode_changed.is_connected(_refresh_contextual_visibility):
		connected_tool_scene.tool_mode_changed.connect(_refresh_contextual_visibility)
	if connected_tool_scene and !connected_tool_scene.track_structure_changed.is_connected(selection_updated):
		connected_tool_scene.track_structure_changed.connect(selection_updated)

func _is_spiral_path(path : Node) -> bool:
	if !path:
		return false
	var script : Script = path.get_script() as Script
	return script and script.resource_path == "res://core/road_path_spiral.gd"

func _is_track_trigger(node : Node) -> bool:
	return node and node.get_script() == TrackTriggerScript

func _active_track_trigger() -> Node3D:
	var selected := get_active_node()
	if _is_track_trigger(selected):
		return selected
	return null

func _apply_context_tabs(show_info : bool, show_spiral : bool, show_rails : bool, show_track : bool, show_object : bool, show_modulation : bool, show_embeds : bool) -> void:
	info_panel.visible = show_info
	spiral_panel.visible = show_spiral
	rail_panel.visible = show_rails
	track_panel.visible = show_track
	object_panel.visible = show_object
	modulation.visible = show_modulation
	embeds.visible = show_embeds
	var any_visible := show_info or show_spiral or show_rails or show_track or show_object or show_modulation or show_embeds
	tab_container.visible = any_visible
	if !any_visible:
		_hide_cross_section_visual()
		return
	var visible_tabs := [show_info, show_rails, show_spiral, show_track, show_object, show_modulation, show_embeds]
	var first_visible := -1
	for i in visible_tabs.size():
		if visible_tabs[i]:
			first_visible = i
			tab_container.set_tab_hidden(i, false)
	if first_visible >= 0:
		tab_container.current_tab = first_visible
	for i in visible_tabs.size():
		tab_container.set_tab_hidden(i, !visible_tabs[i])

func _refresh_contextual_visibility(_mode : int = -1) -> void:
	if !tab_container:
		return
	_sync_tool_mode_signal()
	var scene := connected_tool_scene
	var mode := scene.tool_mode if scene else TrackEditingScene.ToolMode.EDIT_SEGMENT
	var selected := get_active_node()
	var has_path := current_path != null
	var show_info := has_path and (mode == TrackEditingScene.ToolMode.EDIT_SEGMENT or mode == TrackEditingScene.ToolMode.EDIT_SHAPE or mode == TrackEditingScene.ToolMode.EDIT_MESH_LAYOUT or mode == TrackEditingScene.ToolMode.EDIT_CHECKPOINTS)
	var show_spiral := has_path and mode == TrackEditingScene.ToolMode.EDIT_SPIRAL and _is_spiral_path(current_path)
	var show_rails := has_path and mode == TrackEditingScene.ToolMode.EDIT_RAILS
	var show_track := track_root != null and mode == TrackEditingScene.ToolMode.EDIT_TRACK
	var show_object := _active_track_trigger() != null and mode == TrackEditingScene.ToolMode.EDIT_OBJECT
	var show_modulation := has_path and mode == TrackEditingScene.ToolMode.EDIT_MODULATION
	var show_embeds := has_path and mode == TrackEditingScene.ToolMode.EDIT_EMBED
	_apply_context_tabs(show_info, show_spiral, show_rails, show_track, show_object, show_modulation, show_embeds)
	var show_checkpoint_controls := has_path and mode == TrackEditingScene.ToolMode.EDIT_CHECKPOINTS
	var show_mesh_layout_controls := has_path and mode == TrackEditingScene.ToolMode.EDIT_MESH_LAYOUT
	var show_shape_type_controls := has_path and (mode == TrackEditingScene.ToolMode.EDIT_SEGMENT or mode == TrackEditingScene.ToolMode.EDIT_SHAPE)
	var show_cross_section_controls := show_info and !show_checkpoint_controls
	var show_appearance_controls := has_path and mode == TrackEditingScene.ToolMode.EDIT_SEGMENT
	road_shape_row.visible = show_shape_type_controls
	road_uv_multiplier_row.visible = show_cross_section_controls
	ground_color_row.visible = show_appearance_controls
	rail_color_row.visible = show_appearance_controls
	mesh_subdivision_length_row.visible = show_mesh_layout_controls
	mesh_subdivision_angle_row.visible = show_mesh_layout_controls
	checkpoint_count_row.visible = show_checkpoint_controls
	cross_section_controls.visible = show_cross_section_controls
	mesh_layout_clipboard_row.visible = show_mesh_layout_controls
	mesh_layout_create_row.visible = show_mesh_layout_controls
	draw_mesh.visible = has_path
	draw_curve.visible = has_path and mode == TrackEditingScene.ToolMode.EDIT_SEGMENT
	draw_handles.visible = has_path and mode == TrackEditingScene.ToolMode.EDIT_SEGMENT
	var show_bezier_handle_data := mode == TrackEditingScene.ToolMode.EDIT_SEGMENT and selected is BezierHandle
	var show_line_handle_data := selected is LineHandle and (mode == TrackEditingScene.ToolMode.EDIT_SEGMENT or mode == TrackEditingScene.ToolMode.EDIT_SPIRAL)
	var show_handle_data := show_bezier_handle_data or show_line_handle_data
	bezier_handle_data.visible = show_handle_data and selected is BezierHandle
	line_handle_data.visible = show_handle_data and selected is LineHandle

func _selected_embed() -> RoadEmbed:
	if !current_path:
		return null
	var index := embed_dropdown.selected
	if index < 0 or index >= current_path.road_shape.embed_table.size():
		return null
	return current_path.road_shape.embed_table[index]

func _sync_embed_curve_edges(embed : RoadEmbed) -> void:
	if embed.left_boundary and embed.left_boundary.point_count > 0:
		embed.left_boundary.set_point_offset(0, embed.road_start)
		embed.left_boundary.set_point_offset(embed.left_boundary.point_count - 1, embed.road_end)
	if embed.right_boundary and embed.right_boundary.point_count > 0:
		embed.right_boundary.set_point_offset(0, embed.road_start)
		embed.right_boundary.set_point_offset(embed.right_boundary.point_count - 1, embed.road_end)

func update_embed_type(in_type : int):
	if updating_embed_controls:
		return
	var embed := _selected_embed()
	if !embed:
		return
	embed.embed_type = in_type
	update_track_visuals()

func _refresh_embed_edge_controls(embed : RoadEmbed) -> void:
	updating_embed_controls = true
	embed_type.select(int(embed.embed_type))
	embed_start.set_value_no_signal(embed.road_start)
	embed_end.set_value_no_signal(embed.road_end)
	updating_embed_controls = false

func _refresh_selected_embed_controls() -> void:
	var embed := _selected_embed()
	if !embed:
		return
	_refresh_embed_edge_controls(embed)

func update_embed_start_value(new_value : float) -> void:
	if updating_embed_controls:
		return
	var embed := _selected_embed()
	if !embed:
		return
	embed.road_start = clampf(new_value, 0.0, embed.road_end - 0.01)
	_sync_embed_curve_edges(embed)
	_refresh_embed_edge_controls(embed)
	update_track_visuals()

func update_embed_end_value(new_value : float) -> void:
	if updating_embed_controls:
		return
	var embed := _selected_embed()
	if !embed:
		return
	embed.road_end = clampf(new_value, embed.road_start + 0.01, 1.0)
	_sync_embed_curve_edges(embed)
	_refresh_embed_edge_controls(embed)
	update_track_visuals()

func copy_mesh_layout() -> void:
	if !current_path:
		return
	road_poly_clipboard = current_path.horizontal_road_mesh_segments.duplicate()

func paste_mesh_layout() -> void:
	if !current_path:
		return
	current_path.horizontal_road_mesh_segments = road_poly_clipboard.duplicate()
	update_track_visuals()

func copy_transform() -> void:
	var selected := get_active_node()
	if !selected:
		return
	transform_clipboard = selected.global_transform
	if selected is LineHandle:
		transform_clipboard_cp_scale = selected.cp_scale
	elif selected is BezierHandle:
		transform_clipboard_cp_scale = selected.cp_scale

func paste_transform() -> void:
	var selected := get_active_node()
	if !selected:
		return
	selected.global_transform = transform_clipboard
	if selected is LineHandle:
		selected.cp_scale = transform_clipboard_cp_scale
	elif selected is BezierHandle:
		selected.cp_scale = transform_clipboard_cp_scale

func create_simple_mesh_layout() -> void:
	if !current_path:
		return
	var new_array := PackedFloat32Array()
	var num := mesh_layout_count.value
	for i in mesh_layout_count.value:
		new_array.append(i / (num - 1))
	current_path.horizontal_road_mesh_segments = new_array
	update_track_visuals()

func update_checkpoint_count(new_value : float) -> void:
	if !current_path:
		return
	current_path.num_checkpoints = maxi(0, int(new_value) - 1)

func _road_shape_type_id(shape : RoadShape) -> int:
	if shape is RoadShapeCylinder:
		return 1
	if shape is RoadShapeCylinderOpen:
		return 2
	if shape is RoadShapePipe:
		return 3
	if shape is RoadShapePipeOpen:
		return 4
	if shape is RoadShapeRoundedSquareOpenScript:
		return 6
	if shape is RoadShapeRoundedSquareScript:
		return 5
	return 0

func _make_road_shape(shape_id : int) -> RoadShape:
	match shape_id:
		1:
			return RoadShapeCylinder.new()
		2:
			return RoadShapeCylinderOpen.new()
		3:
			return RoadShapePipe.new()
		4:
			return RoadShapePipeOpen.new()
		5:
			return RoadShapeRoundedSquareScript.new()
		6:
			return RoadShapeRoundedSquareOpenScript.new()
	return RoadShape.new()

func _refresh_road_shape_controls() -> void:
	if !current_path:
		return
	updating_road_shape_controls = true
	road_shape_type.select(_road_shape_type_id(current_path.road_shape))
	updating_road_shape_controls = false

func update_road_shape_type(shape_id : int) -> void:
	if updating_road_shape_controls or !current_path:
		return
	if shape_id == _road_shape_type_id(current_path.road_shape):
		return
	var old_shape := current_path.road_shape
	var new_shape := _make_road_shape(shape_id)
	new_shape.modulation_table = old_shape.modulation_table
	new_shape.embed_table = old_shape.embed_table
	current_path.road_shape = new_shape
	current_path._try_generate_mesh()
	_refresh_road_shape_controls()
	if FZGlobal.editing_scene:
		FZGlobal.editing_scene.set_active_shape_path(current_path)

func update_road_uv_multiplier(new_value : float) -> void:
	if updating_road_uv_controls or !current_path:
		return
	current_path.road_uv_multiplier = maxf(0.001, new_value)
	update_track_visuals()

func _refresh_road_color_controls() -> void:
	if !current_path:
		return
	updating_road_color_controls = true
	ground_color.color = current_path.ground_color
	rail_color.color = current_path.rail_color
	rail_mode_color.color = current_path.rail_color
	updating_road_color_controls = false

func update_ground_color(new_color : Color) -> void:
	if updating_road_color_controls or !current_path:
		return
	current_path.ground_color = new_color
	update_track_visuals()

func update_rail_color(new_color : Color) -> void:
	if updating_road_color_controls or !current_path:
		return
	current_path.rail_color = new_color
	update_track_visuals()

func _refresh_rail_controls() -> void:
	if !current_path:
		return
	updating_rail_controls = true
	left_rail_height.set_value_no_signal(current_path.left_rail_height)
	left_rail_start.set_value_no_signal(current_path.left_rail_start)
	left_rail_end.set_value_no_signal(current_path.left_rail_end)
	right_rail_height.set_value_no_signal(current_path.right_rail_height)
	right_rail_start.set_value_no_signal(current_path.right_rail_start)
	right_rail_end.set_value_no_signal(current_path.right_rail_end)
	updating_rail_controls = false

func update_rail_values(_new_value : float) -> void:
	if updating_rail_controls or !current_path:
		return
	var left_start_value := clampf(left_rail_start.value, 0.0, 1.0)
	var left_end_value := clampf(left_rail_end.value, 0.0, 1.0)
	if left_end_value < left_start_value:
		left_end_value = left_start_value
	var right_start_value := clampf(right_rail_start.value, 0.0, 1.0)
	var right_end_value := clampf(right_rail_end.value, 0.0, 1.0)
	if right_end_value < right_start_value:
		right_end_value = right_start_value
	current_path.left_rail_height = maxf(0.0, left_rail_height.value)
	current_path.left_rail_start = left_start_value
	current_path.left_rail_end = left_end_value
	current_path.right_rail_height = maxf(0.0, right_rail_height.value)
	current_path.right_rail_start = right_start_value
	current_path.right_rail_end = right_end_value
	update_track_visuals()

func _refresh_mesh_subdivision_controls() -> void:
	if !current_path:
		return
	updating_mesh_subdivision_controls = true
	mesh_subdivision_length.set_value_no_signal(current_path.mesh_subdivision_length)
	mesh_subdivision_angle.set_value_no_signal(current_path.mesh_subdivision_angle_degrees)
	updating_mesh_subdivision_controls = false

func update_mesh_subdivision_length(new_value : float) -> void:
	if updating_mesh_subdivision_controls or !current_path:
		return
	current_path.mesh_subdivision_length = maxf(0.1, new_value)
	update_track_visuals()

func update_mesh_subdivision_angle(new_value : float) -> void:
	if updating_mesh_subdivision_controls or !current_path:
		return
	current_path.mesh_subdivision_angle_degrees = clampf(new_value, 0.1, 90.0)
	update_track_visuals()

func _refresh_track_controls() -> void:
	if !track_root:
		return
	updating_track_controls = true
	track_name_edit.text = track_root.track_name
	track_description_edit.text = track_root.track_description
	track_difficulty_edit.set_value_no_signal(track_root.track_difficulty)
	updating_track_controls = false

func update_track_name(new_text : String) -> void:
	if updating_track_controls or !track_root:
		return
	track_root.track_name = new_text

func update_track_description(new_text : String) -> void:
	if updating_track_controls or !track_root:
		return
	track_root.track_description = new_text

func update_track_difficulty(new_value : float) -> void:
	if updating_track_controls or !track_root:
		return
	track_root.track_difficulty = int(new_value)

func _refresh_track_object_controls() -> void:
	var trigger := _active_track_trigger()
	if !trigger:
		return
	updating_object_controls = true
	object_type.select(int(trigger.get("trigger_type")))
	var surface_t : Vector2 = trigger.get("surface_t")
	object_tx.set_value_no_signal(surface_t.x)
	object_ty.set_value_no_signal(surface_t.y)
	object_yaw.set_value_no_signal(trigger.get("add_yaw_degrees"))
	var trigger_scale : Vector3 = trigger.get("trigger_scale")
	object_scale_x.set_value_no_signal(trigger_scale.x)
	object_scale_y.set_value_no_signal(trigger_scale.y)
	object_scale_z.set_value_no_signal(trigger_scale.z)
	updating_object_controls = false

func update_track_object_type(new_type : int) -> void:
	if updating_object_controls:
		return
	var trigger := _active_track_trigger()
	if !trigger:
		return
	trigger.set("trigger_type", new_type)
	trigger.call("refresh_from_attachment")

func remove_track_object_func() -> void:
	var trigger := _active_track_trigger()
	if !trigger:
		return
	FZGlobal.clear_selection_immediate()
	trigger.queue_free()
	if FZGlobal.editing_scene:
		FZGlobal.editing_scene.track_structure_changed.emit()

func update_track_object_values(_new_value : float) -> void:
	if updating_object_controls:
		return
	var trigger := _active_track_trigger()
	if !trigger:
		return
	trigger.set("surface_t", Vector2(object_tx.value, object_ty.value))
	trigger.set("add_yaw_degrees", object_yaw.value)
	trigger.set("trigger_scale", Vector3(object_scale_x.value, object_scale_y.value, object_scale_z.value))
	trigger.call("refresh_from_attachment")

func _refresh_spiral_controls() -> void:
	if !_is_spiral_path(current_path):
		return
	updating_spiral_controls = true
	spiral_degrees.set_value_no_signal(current_path.get("spiral_degrees"))
	var axis : Vector3 = current_path.get("spiral_axis")
	spiral_axis_x.set_value_no_signal(axis.x)
	spiral_axis_y.set_value_no_signal(axis.y)
	spiral_axis_z.set_value_no_signal(axis.z)
	updating_spiral_controls = false

func _mark_spiral_dirty() -> void:
	if !_is_spiral_path(current_path):
		return
	current_path.set("should_update", true)

func update_spiral_values(_new_value : float) -> void:
	if updating_spiral_controls or !_is_spiral_path(current_path):
		return
	current_path.set("spiral_degrees", spiral_degrees.value)
	current_path.set("spiral_axis", Vector3(spiral_axis_x.value, spiral_axis_y.value, spiral_axis_z.value))
	_mark_spiral_dirty()

func _input(event: InputEvent) -> void:
	if !cs_rect.is_visible_in_tree():
		return
	if event is InputEventMouseButton:
		var mouse_pos: Vector2 = cs_rect.get_local_mouse_position()
		if mouse_pos.x < 0.0 or mouse_pos.y < 0.0 or mouse_pos.x > cs_rect.size.x or mouse_pos.y > cs_rect.size.y:
			return
		if event.button_index == MOUSE_BUTTON_LEFT:
			get_viewport().set_input_as_handled()
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			get_viewport().set_input_as_handled()

func update_handle_properties(in_value : float) -> void:
	if updating_handle_controls:
		return
	
	var selected := get_active_node()
	if !selected:
		return
	
	if selected is BezierHandle:
		selected.global_position.x = bez_pos_x.value
		selected.global_position.y = bez_pos_y.value
		selected.global_position.z = bez_pos_z.value
		selected.cp_scale.x = bez_scale_w.value
		selected.cp_scale.y = bez_scale_h.value
		var use_rot : Basis = Basis.from_euler(Vector3(deg_to_rad(bez_rot_x.value), deg_to_rad(bez_rot_y.value), deg_to_rad(bez_rot_z.value)))
		selected.global_basis = use_rot
		selected.in_handle_length = bez_weight_i.value
		selected.out_handle_length = bez_weight_o.value
		selected.rot_ease_type = bez_rot_ease_type.selected
		selected.rot_ease_strength = bez_rot_ease_strength.value
		selected.twist_ease_type = bez_twist_ease_type.selected
		selected.twist_ease_strength = bez_twist_ease_strength.value
		selected.scale_ease_type = bez_scale_ease_type.selected
		selected.scale_ease_strength = bez_scale_ease_strength.value
	elif selected is LineHandle:
		selected.global_position.x = line_pos_x.value
		selected.global_position.y = line_pos_y.value
		selected.global_position.z = line_pos_z.value
		selected.cp_scale.x = line_scale_w.value
		selected.cp_scale.y = line_scale_h.value
		var use_rot : Basis = Basis.from_euler(Vector3(deg_to_rad(line_rot_x.value), deg_to_rad(line_rot_y.value), deg_to_rad(line_rot_z.value)))
		selected.global_basis = use_rot

func update_handle_ease_type(_index : int) -> void:
	update_handle_properties(0.0)

func _refresh_bezier_handle_controls(handle : BezierHandle) -> void:
	updating_handle_controls = true
	bez_pos_x.set_value_no_signal(handle.global_position.x)
	bez_pos_y.set_value_no_signal(handle.global_position.y)
	bez_pos_z.set_value_no_signal(handle.global_position.z)
	var use_rot : Vector3 = handle.global_basis.orthonormalized().get_euler()
	bez_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
	bez_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
	bez_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
	bez_scale_w.set_value_no_signal(handle.cp_scale.x)
	bez_scale_h.set_value_no_signal(handle.cp_scale.y)
	bez_weight_i.set_value_no_signal(handle.in_handle_length)
	bez_weight_o.set_value_no_signal(handle.out_handle_length)
	bez_rot_ease_type.select(clampi(handle.rot_ease_type, 0, 3))
	bez_rot_ease_strength.set_value_no_signal(handle.rot_ease_strength)
	bez_twist_ease_type.select(clampi(handle.twist_ease_type, 0, 3))
	bez_twist_ease_strength.set_value_no_signal(handle.twist_ease_strength)
	bez_scale_ease_type.select(clampi(handle.scale_ease_type, 0, 3))
	bez_scale_ease_strength.set_value_no_signal(handle.scale_ease_strength)
	updating_handle_controls = false


func _process(delta: float) -> void:
	_sync_tool_mode_signal()
	if modulation:
		modulation.get_v_scroll_bar().custom_minimum_size.x = 24
	if embeds:
		embeds.get_v_scroll_bar().custom_minimum_size.x = 24
	if FZGlobal.editing_scene:
		FZGlobal.editing_scene.editor_cross_section_t = track_cross_section_slider.value
		FZGlobal.editing_scene.draw_segment_curve = draw_curve.button_pressed
		FZGlobal.editing_scene.draw_segment_handles = draw_handles.button_pressed
	if !track_root:
		track_root = get_runtime_track_root()
	
	if !track_root:
		return
	if track_panel.visible:
		_refresh_track_controls()
	if object_panel.visible:
		_refresh_track_object_controls()
	if rail_panel.visible:
		_refresh_rail_controls()
	if embeds.visible:
		_refresh_selected_embed_controls()
	
	var selected := get_active_node()
	if !selected:
		current_path = null
		_refresh_contextual_visibility()
		return
	
	if selected is BezierHandle:
		_refresh_bezier_handle_controls(selected)
	elif selected is LineHandle:
		line_pos_x.set_value_no_signal(selected.global_position.x)
		line_pos_y.set_value_no_signal(selected.global_position.y)
		line_pos_z.set_value_no_signal(selected.global_position.z)
		var use_rot : Vector3 = selected.global_basis.orthonormalized().get_euler()
		line_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
		line_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
		line_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
		line_scale_w.set_value_no_signal(selected.cp_scale.x)
		line_scale_h.set_value_no_signal(selected.cp_scale.y)
	
	if current_path:
		_refresh_road_shape_controls()
		if _is_spiral_path(current_path):
			_refresh_spiral_controls()
		updating_road_uv_controls = true
		road_uv_multiplier.set_value_no_signal(current_path.road_uv_multiplier)
		updating_road_uv_controls = false
		_refresh_road_color_controls()
		_refresh_mesh_subdivision_controls()
		var mesh_was_visible : bool = current_path.get_child(0).visible
		current_path.get_child(0).visible = draw_mesh.button_pressed
		checkpoint_count.set_value_no_signal(current_path.num_checkpoints + 1)
		if info_panel.visible and cross_section_controls.visible:
			var segments := 4.0
			var debug_t_values := PackedVector2Array()
			for i in segments - 1:
				debug_t_values.append(Vector2((i / (segments - 1) * 2.0) - 1.0, track_cross_section_slider.value))
				debug_t_values.append(Vector2(((i + 1) / (segments - 1) * 2.0) - 1.0, track_cross_section_slider.value))
			var debug_points : PackedVector3Array = current_path.get_surface_positions(debug_t_values)
			_update_cross_section_visual(debug_points)
		else:
			_hide_cross_section_visual()
		if !mesh_was_visible and draw_mesh.button_pressed:
			current_path._try_generate_mesh()
	else:
		_hide_cross_section_visual()
	_refresh_contextual_visibility()

@onready var bezier_handle_data: VBoxContainer = $Control/VBoxContainer/DataEditor/BezierHandleData
@onready var line_handle_data: VBoxContainer = $Control/VBoxContainer/DataEditor/LineHandleData

func selection_updated() -> void:
	smooth_curve.clear_points()
	poly_curve.clear_points()
	var this_node := get_active_node()
	if !this_node:
		current_path = null
		_refresh_contextual_visibility()
		return
	var path : RoadPath
	if this_node is RoadPath:
		path = this_node
		bezier_handle_data.visible = false
		line_handle_data.visible = false
	if this_node.get_parent() is RoadPath:
		path = this_node.get_parent()
		if this_node is BezierHandle:
			bezier_handle_data.visible = true
			line_handle_data.visible = false
			_refresh_bezier_handle_controls(this_node)
		elif this_node is LineHandle:
			line_handle_data.visible = true
			bezier_handle_data.visible = false
			line_pos_x.set_value_no_signal(this_node.global_position.x)
			line_pos_y.set_value_no_signal(this_node.global_position.y)
			line_pos_z.set_value_no_signal(this_node.global_position.z)
			var use_rot : Vector3 = this_node.global_basis.orthonormalized().get_euler()
			line_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
			line_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
			line_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
			line_scale_w.set_value_no_signal(this_node.cp_scale.x)
			line_scale_h.set_value_no_signal(this_node.cp_scale.y)
	
	if !path:
		current_path = null
		_refresh_contextual_visibility()
		return
	current_path = path
	draw_mesh.button_pressed = current_path.get_child(0).visible
	_refresh_spiral_controls()
	refresh_modulations_and_embeds()
	_refresh_contextual_visibility()

func add_new_modulation() -> void:
	if !current_path:
		return
	var new_index := current_path.road_shape.modulation_table.size()
	var new_mod := RoadModulation.new()
	new_mod.modulation_effect = ClassDB.instantiate("TrackEditorFloatCurve")
	new_mod.modulation_height = ClassDB.instantiate("TrackEditorFloatCurve")
	new_mod.modulation_effect.add_point(Vector2(0, 0))
	new_mod.modulation_effect.add_point(Vector2(1, 0))
	new_mod.modulation_height.add_point(Vector2(0, 1))
	new_mod.modulation_height.add_point(Vector2(1, 1))
	current_path.road_shape.modulation_table.append(new_mod)
	refresh_modulations_and_embeds(new_index, embed_dropdown.selected)

func remove_modulation() -> void:
	if !current_path:
		return
	var selected := modulation_dropdown.selected
	if selected < 0 or selected >= current_path.road_shape.modulation_table.size():
		return
	current_path.road_shape.modulation_table.remove_at(selected)
	refresh_modulations_and_embeds(mini(selected, current_path.road_shape.modulation_table.size() - 1), embed_dropdown.selected)

func add_new_embed() -> void:
	var scene := FZGlobal.editing_scene as TrackEditingScene
	if !scene:
		return
	scene.desired_embed_type = embed_type.selected if embed_type.selected >= 0 else RoadEmbed.EmbedType.RECHARGE
	scene.pending_embed_add = true
	scene.set_tool_mode(TrackEditingScene.ToolMode.ADD_EMBED)
	if current_path:
		scene.active_path = current_path

func remove_embed_func() -> void:
	if !current_path:
		return
	var selected := embed_dropdown.selected
	if selected < 0 or selected >= current_path.road_shape.embed_table.size():
		return
	current_path.road_shape.embed_table.remove_at(selected)
	refresh_modulations_and_embeds(modulation_dropdown.selected, mini(selected, current_path.road_shape.embed_table.size() - 1))

func refresh_modulations_and_embeds(preferred_modulation := -2, preferred_embed := -2) -> void:
	if !current_path:
		return
	var previous_modulation := modulation_dropdown.selected
	modulation_dropdown.clear()
	for i in current_path.road_shape.modulation_table.size():
		var mod := current_path.road_shape.modulation_table[i]
		modulation_dropdown.add_item("Modulation " + str(i + 1))
	var modulation_selection := previous_modulation if preferred_modulation == -2 else preferred_modulation
	if current_path.road_shape.modulation_table.size() > 0:
		modulation_selection = clampi(modulation_selection, 0, current_path.road_shape.modulation_table.size() - 1)
		modulation_dropdown.select(modulation_selection)
	else:
		modulation_selection = -1

	var previous_embed := embed_dropdown.selected
	embed_dropdown.clear()
	for i in current_path.road_shape.embed_table.size():
		var mod := current_path.road_shape.embed_table[i]
		embed_dropdown.add_item("Embed " + str(i + 1))
	var embed_selection := previous_embed if preferred_embed == -2 else preferred_embed
	if current_path.road_shape.embed_table.size() > 0:
		embed_selection = clampi(embed_selection, 0, current_path.road_shape.embed_table.size() - 1)
		embed_dropdown.select(embed_selection)
	else:
		embed_selection = -1
	update_modulations_and_embeds(modulation_selection, embed_selection)

func _on_modulation_selected(in_selected_mod : int) -> void:
	update_modulations_and_embeds(in_selected_mod, embed_dropdown.selected)

func _on_embed_selected(in_selected_embed : int) -> void:
	update_modulations_and_embeds(modulation_dropdown.selected, in_selected_embed)

func update_modulations_and_embeds(in_selected_mod : int = modulation_dropdown.selected, in_selected_embed : int = embed_dropdown.selected) -> void:
	if in_selected_mod < 0 or in_selected_mod >= current_path.road_shape.modulation_table.size() or modulation_dropdown.item_count == 0:
		if FZGlobal.editing_scene:
			FZGlobal.editing_scene.set_active_modulation(null, -1)
	else:
		if FZGlobal.editing_scene:
			FZGlobal.editing_scene.set_active_modulation(current_path, in_selected_mod)
	if in_selected_embed < 0 or in_selected_embed >= current_path.road_shape.embed_table.size() or embed_dropdown.item_count == 0:
		if FZGlobal.editing_scene:
			FZGlobal.editing_scene.set_active_embed(null, -1)
	else:
		var this_embed := current_path.road_shape.embed_table[in_selected_embed]
		_refresh_embed_edge_controls(this_embed)
		if FZGlobal.editing_scene:
			FZGlobal.editing_scene.set_active_embed(current_path, in_selected_embed)
	update_track_visuals()



func update_track_visuals() -> void:
	if !current_path:
		return
	update_track.emit()
	current_path._try_generate_mesh()
