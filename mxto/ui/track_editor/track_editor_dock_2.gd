class_name TrackEditorDock2 extends Control

signal dock_ready

signal update_track

var track_root : TrackRoot

var current_path : RoadPath

@onready var draw_mesh: CheckBox = $Control/VBoxContainer/DrawMesh
@onready var draw_curve: CheckBox = $Control/VBoxContainer/DrawCurve
@onready var draw_handles: CheckBox = $Control/VBoxContainer/DrawHandles

@onready var track_cross_section_slider: HSlider = $Control/TabContainer/Info/VBoxContainer/TrackCrossSectionSlider
@onready var tab_container: TabContainer = $Control/TabContainer
@onready var smooth_curve: Line2D = $Control/TabContainer/Info/VBoxContainer/ColorRect/SmoothCurve
@onready var poly_curve: Line2D = $Control/TabContainer/Info/VBoxContainer/ColorRect/PolyCurve

@onready var modulation_dropdown: OptionButton = $Control/TabContainer/Modulation/VBoxContainer/HBoxContainer/ModulationDropdown
@onready var new_modulation: Button = $Control/TabContainer/Modulation/VBoxContainer/HBoxContainer/NewModulation
@onready var mod_curve_effect: CurveEditor = $Control/TabContainer/Modulation/VBoxContainer/ModCurveEffect
@onready var mod_curve_height: CurveEditor = $Control/TabContainer/Modulation/VBoxContainer/ModCurveHeight
@onready var remove_mod_button: Button = $Control/TabContainer/Modulation/VBoxContainer/HBoxContainer/RemoveModulation

@onready var new_embed: Button = $Control/TabContainer/Embeds/VBoxContainer/HBoxContainer/NewEmbed
@onready var remove_embed: Button = $Control/TabContainer/Embeds/VBoxContainer/HBoxContainer/RemoveEmbed
@onready var embed_dropdown: OptionButton = $Control/TabContainer/Embeds/VBoxContainer/HBoxContainer/EmbedDropdown
@onready var embed_type: OptionButton = $Control/TabContainer/Embeds/VBoxContainer/EmbedType
@onready var embed_start: HSlider = $Control/TabContainer/Embeds/VBoxContainer/EmbedStart
@onready var embed_end: HSlider = $Control/TabContainer/Embeds/VBoxContainer/EmbedEnd
@onready var embed_curve_left: CurveEditor = $Control/TabContainer/Embeds/VBoxContainer/EmbedCurveLeft
@onready var embed_curve_right: CurveEditor = $Control/TabContainer/Embeds/VBoxContainer/EmbedCurveRight

@onready var modulation: ScrollContainer = $Control/TabContainer/Modulation
@onready var embeds: ScrollContainer = $Control/TabContainer/Embeds

@onready var cs_rect: CurveCrossSection = $Control/TabContainer/Info/VBoxContainer/ColorRect

@onready var copy_mesh_layout_button: Button = $Control/TabContainer/Info/VBoxContainer/HBoxContainer/CopyMeshLayout
@onready var paste_mesh_layout_button: Button = $Control/TabContainer/Info/VBoxContainer/HBoxContainer/PasteMeshLayout
@onready var create_mesh_layout_button: Button = $Control/TabContainer/Info/VBoxContainer/HBoxContainer2/CreateMeshLayout
@onready var mesh_layout_count: SpinBox = $Control/TabContainer/Info/VBoxContainer/HBoxContainer2/MeshLayoutCount

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
	embed_dropdown.item_selected.connect(update_modulations_and_embeds)
	modulation_dropdown.item_selected.connect(update_modulations_and_embeds)
	new_modulation.pressed.connect(add_new_modulation)
	remove_mod_button.pressed.connect(remove_modulation)
	new_embed.pressed.connect(add_new_embed)
	remove_embed.pressed.connect(remove_embed_func)
	embed_start.value_changed.connect(update_embed_values)
	embed_end.value_changed.connect(update_embed_values)
	mod_curve_effect.curve_edited.connect(update_track_visuals)
	mod_curve_height.curve_edited.connect(update_track_visuals)
	track_cross_section_slider.value_changed.connect(cs_rect.update_track_cross_sections)
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

func update_embed_type(in_type : int):
	if !current_path:
		return
	current_path.road_shape.embed_table[embed_dropdown.selected].embed_type = in_type

func update_embed_values(new_value):
	if !current_path:
		return
	current_path.road_shape.embed_table[embed_dropdown.selected].embed_type = embed_type.selected
	current_path.road_shape.embed_table[embed_dropdown.selected].road_start = embed_start.value
	current_path.road_shape.embed_table[embed_dropdown.selected].road_end = embed_end.value

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

func paste_transform() -> void:
	var selected := get_active_node()
	if !selected:
		return
	selected.global_transform = transform_clipboard

func create_simple_mesh_layout() -> void:
	if !current_path:
		return
	var new_array := PackedFloat32Array()
	var num := mesh_layout_count.value
	for i in mesh_layout_count.value:
		new_array.append(i / (num - 1))
	current_path.horizontal_road_mesh_segments = new_array
	update_track_visuals()

var left_clicked := false
var right_clicked := false

func _input(event: InputEvent) -> void:
	if !cs_rect.is_visible_in_tree():
		return
	if event is InputEventMouseButton:
		var mouse_pos: Vector2 = cs_rect.get_local_mouse_position()
		if mouse_pos.x < 0.0 or mouse_pos.y < 0.0 or mouse_pos.x > cs_rect.size.x or mouse_pos.y > cs_rect.size.y:
			return
		if event.button_index == MOUSE_BUTTON_LEFT:
			left_clicked = true
			get_viewport().set_input_as_handled()
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			right_clicked = true
			get_viewport().set_input_as_handled()

func update_handle_properties(in_value : float) -> void:
	
	var selected := get_active_node()
	if !selected:
		return
	
	if selected is BezierHandle:
		selected.global_position.x = bez_pos_x.value
		selected.global_position.y = bez_pos_y.value
		selected.global_position.z = bez_pos_z.value
		selected.scale.x = bez_scale_w.value
		selected.scale.y = bez_scale_h.value
		var use_rot : Basis = Basis.from_euler(Vector3(deg_to_rad(bez_rot_x.value), deg_to_rad(bez_rot_y.value), deg_to_rad(bez_rot_z.value)))
		use_rot.x *= selected.scale.x
		use_rot.y *= selected.scale.y
		selected.global_basis = use_rot
		selected.in_handle_length = bez_weight_i.value
		selected.out_handle_length = bez_weight_o.value
	elif selected is Marker3D:
		selected.global_position.x = line_pos_x.value
		selected.global_position.y = line_pos_y.value
		selected.global_position.z = line_pos_z.value
		selected.scale.x = line_scale_w.value
		selected.scale.y = line_scale_h.value
		var use_rot : Basis = Basis.from_euler(Vector3(deg_to_rad(line_rot_x.value), deg_to_rad(line_rot_y.value), deg_to_rad(line_rot_z.value)))
		use_rot.x *= selected.scale.x
		use_rot.y *= selected.scale.y
		selected.global_basis = use_rot


func _process(delta: float) -> void:
	if modulation:
		modulation.get_v_scroll_bar().custom_minimum_size.x = 24
	if embeds:
		embeds.get_v_scroll_bar().custom_minimum_size.x = 24
	if !track_root:
		track_root = get_runtime_track_root()
	
	if !track_root:
		return
	
	var selected := get_active_node()
	if !selected:
		tab_container.visible = false
		return
	
	var editor_cam := FZGlobal.current_cam
	
	if selected is BezierHandle:
		bez_pos_x.set_value_no_signal(selected.global_position.x)
		bez_pos_y.set_value_no_signal(selected.global_position.y)
		bez_pos_z.set_value_no_signal(selected.global_position.z)
		var use_rot : Vector3 = selected.global_basis.orthonormalized().get_euler()
		bez_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
		bez_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
		bez_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
		bez_scale_w.set_value_no_signal(selected.scale.x)
		bez_scale_h.set_value_no_signal(selected.scale.y)
		bez_weight_i.set_value_no_signal(selected.in_handle_length)
		bez_weight_o.set_value_no_signal(selected.out_handle_length)
	elif selected is Marker3D:
		line_pos_x.set_value_no_signal(selected.global_position.x)
		line_pos_y.set_value_no_signal(selected.global_position.y)
		line_pos_z.set_value_no_signal(selected.global_position.z)
		var use_rot : Vector3 = selected.global_basis.orthonormalized().get_euler()
		line_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
		line_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
		line_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
		line_scale_w.set_value_no_signal(selected.scale.x)
		line_scale_h.set_value_no_signal(selected.scale.y)
	
	if current_path:
		tab_container.visible = true
		var base_pos : Vector3 = current_path.get_root_transform(track_cross_section_slider.value).origin
		var segments := 4.0
		var debug_thickness := 1.0
		if editor_cam:
			debug_thickness = base_pos.distance_to(editor_cam.global_position) * 0.0025
		DebugDraw3D.scoped_config().set_thickness(debug_thickness)
		var debug_t_values := PackedVector2Array()
		for i in segments - 1:
			debug_t_values.append(Vector2((i / (segments - 1) * 2.0) - 1.0, track_cross_section_slider.value))
			debug_t_values.append(Vector2(((i + 1) / (segments - 1) * 2.0) - 1.0, track_cross_section_slider.value))
		var mesh_was_visible : bool = current_path.get_child(0).visible
		current_path.get_child(0).visible = draw_mesh.button_pressed
		for i in current_path.road_shape.embed_table.size():
			var current_embed := current_path.road_shape.embed_table[i]
			var left_boundary_points : PackedVector2Array = current_embed.left_boundary.build_sampled_points(32)
			var right_boundary_points : PackedVector2Array = current_embed.right_boundary.build_sampled_points(32)
			for ny in 32:
				for nx in 8:
					var ty = remap(float(ny), 0.0, 31.0, current_embed.road_start, current_embed.road_end)
					var leftbound : float = left_boundary_points[ny].y
					var rightbound : float = right_boundary_points[ny].y
					var tx = lerpf(leftbound, rightbound, nx / 7.0)
					debug_t_values.append(Vector2(tx, ty))
		var debug_points : PackedVector3Array = current_path.get_surface_positions(debug_t_values)
		var cursor := 0
		for i in segments - 1:
			DebugDraw3D.draw_line(debug_points[cursor], debug_points[cursor + 1], Color.RED, delta)
			cursor += 2
		while cursor < debug_points.size():
			DebugDraw3D.draw_sphere(debug_points[cursor], 1, Color.RED, delta)
			cursor += 1
		if !mesh_was_visible and draw_mesh.button_pressed:
			current_path._try_generate_mesh()
	else:
		tab_container.visible = false

@onready var bezier_handle_data: VBoxContainer = $Control/VBoxContainer/DataEditor/BezierHandleData
@onready var line_handle_data: VBoxContainer = $Control/VBoxContainer/DataEditor/LineHandleData

func selection_updated() -> void:
	smooth_curve.clear_points()
	poly_curve.clear_points()
	var this_node := get_active_node()
	if !this_node:
		current_path = null
		tab_container.visible = false
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
			bez_pos_x.set_value_no_signal(this_node.global_position.x)
			bez_pos_y.set_value_no_signal(this_node.global_position.y)
			bez_pos_z.set_value_no_signal(this_node.global_position.z)
			var use_rot : Vector3 = this_node.global_basis.orthonormalized().get_euler()
			bez_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
			bez_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
			bez_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
			bez_scale_w.set_value_no_signal(this_node.scale.x)
			bez_scale_h.set_value_no_signal(this_node.scale.y)
			bez_weight_i.set_value_no_signal(this_node.in_handle_length)
			bez_weight_o.set_value_no_signal(this_node.out_handle_length)
		elif this_node is Marker3D:
			line_handle_data.visible = true
			bezier_handle_data.visible = false
			line_pos_x.set_value_no_signal(this_node.global_position.x)
			line_pos_y.set_value_no_signal(this_node.global_position.y)
			line_pos_z.set_value_no_signal(this_node.global_position.z)
			var use_rot : Vector3 = this_node.global_basis.orthonormalized().get_euler()
			line_rot_x.set_value_no_signal(rad_to_deg(use_rot.x))
			line_rot_y.set_value_no_signal(rad_to_deg(use_rot.y))
			line_rot_z.set_value_no_signal(rad_to_deg(use_rot.z))
			line_scale_w.set_value_no_signal(this_node.scale.x)
			line_scale_h.set_value_no_signal(this_node.scale.y)
	
	if !path:
		return
	current_path = path
	draw_mesh.button_pressed = current_path.get_child(0).visible
	refresh_modulations_and_embeds()

func add_new_modulation() -> void:
	if !current_path:
		return
	var new_mod := RoadModulation.new()
	new_mod.modulation_effect = ClassDB.instantiate("TrackEditorFloatCurve")
	new_mod.modulation_height = ClassDB.instantiate("TrackEditorFloatCurve")
	new_mod.modulation_effect.add_point(Vector2(0, 0))
	new_mod.modulation_effect.add_point(Vector2(1, 0))
	new_mod.modulation_height.add_point(Vector2(0, 0))
	new_mod.modulation_height.add_point(Vector2(1, 0))
	current_path.road_shape.modulation_table.append(new_mod)
	refresh_modulations_and_embeds()

func remove_modulation() -> void:
	if !current_path:
		return
	current_path.road_shape.modulation_table.remove_at(modulation_dropdown.selected)
	refresh_modulations_and_embeds()

func add_new_embed() -> void:
	if !current_path:
		return
	var new_embed := RoadEmbed.new()
	new_embed.road_start = 0.0
	new_embed.road_end = 1.0
	new_embed.left_boundary = ClassDB.instantiate("TrackEditorFloatCurve")
	new_embed.right_boundary = ClassDB.instantiate("TrackEditorFloatCurve")
	new_embed.left_boundary.add_point(Vector2(0, 0))
	new_embed.left_boundary.add_point(Vector2(1, 0))
	new_embed.right_boundary.add_point(Vector2(0, 0))
	new_embed.right_boundary.add_point(Vector2(1, 0))
	new_embed.embed_type = RoadEmbed.EmbedType.RECHARGE
	current_path.road_shape.embed_table.append(new_embed)
	refresh_modulations_and_embeds()

func remove_embed_func() -> void:
	if !current_path:
		return
	current_path.road_shape.embed_table.remove_at(embed_dropdown.selected)
	refresh_modulations_and_embeds()

func refresh_modulations_and_embeds() -> void:
	if !current_path:
		return
	var selected := modulation_dropdown.selected
	modulation_dropdown.clear()
	for i in current_path.road_shape.modulation_table.size():
		var mod := current_path.road_shape.modulation_table[i]
		modulation_dropdown.add_item("Modulation " + str(i + 1))
	if current_path.road_shape.modulation_table.size() > 0 and modulation_dropdown.selected != -1:
		modulation_dropdown.select(minf(modulation_dropdown.selected + 1, current_path.road_shape.modulation_table.size() - 1))
		
	selected = embed_dropdown.selected
	embed_dropdown.clear()
	for i in current_path.road_shape.embed_table.size():
		var mod := current_path.road_shape.embed_table[i]
		embed_dropdown.add_item("Embed " + str(i + 1))
	if current_path.road_shape.embed_table.size() > 0 and embed_dropdown.selected != -1:
		embed_dropdown.select(minf(embed_dropdown.selected + 1, current_path.road_shape.embed_table.size() - 1))
	update_modulations_and_embeds()

func update_modulations_and_embeds(in_selected_mod : int = modulation_dropdown.selected, in_selected_embed : int = embed_dropdown.selected) -> void:
	if in_selected_mod == -1 or modulation_dropdown.item_count == 0:
		mod_curve_effect.visible = false
		mod_curve_height.visible = false
	else:
		mod_curve_effect.visible = true
		mod_curve_height.visible = true
		var this_mod := current_path.road_shape.modulation_table[in_selected_mod]
		mod_curve_effect.associated_curve = this_mod.modulation_effect
		mod_curve_height.associated_curve = this_mod.modulation_height
		mod_curve_effect.queue_redraw()
		mod_curve_height.queue_redraw()
	if in_selected_embed == -1 or embed_dropdown.item_count == 0:
		embed_curve_left.visible = false
		embed_curve_right.visible = false
	else:
		embed_curve_left.visible = true
		embed_curve_right.visible = true
		var this_embed := current_path.road_shape.embed_table[in_selected_embed]
		embed_curve_left.associated_curve = this_embed.left_boundary
		embed_curve_right.associated_curve = this_embed.right_boundary
		embed_curve_left.queue_redraw()
		embed_curve_right.queue_redraw()
		embed_type.selected = this_embed.embed_type
	update_track_visuals()



func update_track_visuals() -> void:
	if !current_path:
		return
	update_track.emit()
	current_path._try_generate_mesh()
