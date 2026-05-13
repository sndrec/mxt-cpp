extends Node

signal selection_changed

var tick_delta : float = 1.0 / Engine.physics_ticks_per_second
var units_to_kmh : float = 260
var kmh_to_units : float = 1.0 / units_to_kmh
var current_track : TrackRoot
var current_cam : Camera3D
var editing_scene : TrackEditingScene

var undo_redo = UndoRedo.new()
var active_node : Node3D
var selected_nodes : Array[Node3D] = []

var can_select := true

func get_selected_road_path() -> RoadPath:
	var node := active_node
	while node:
		if node is RoadPath:
			return node
		node = node.get_parent()
	return null

func emit_selection_changed() -> void:
	if editing_scene:
		editing_scene.active_path = get_selected_road_path()
	selection_changed.emit()

func get_transform_gizmo_target(in_node : Node3D) -> Node3D:
	if !is_instance_valid(in_node):
		return null
	if in_node is RoadPath:
		return null
	return in_node

func transform_object(in_node : Node3D, in_transform : Transform3D, original_transform : Transform3D) -> void:
	undo_redo.create_action("Move Node")
	undo_redo.add_do_property(editing_scene.translate_gizmo, "global_transform", in_transform)
	undo_redo.add_do_property(editing_scene.rotate_gizmo, "global_transform", in_transform)
	undo_redo.add_do_property(editing_scene.add_road_gizmo, "global_transform", in_transform)
	undo_redo.add_do_property(in_node, "global_transform", in_transform)
	undo_redo.add_undo_property(editing_scene.translate_gizmo, "global_transform", original_transform)
	undo_redo.add_undo_property(editing_scene.rotate_gizmo, "global_transform", original_transform)
	undo_redo.add_undo_property(editing_scene.add_road_gizmo, "global_transform", original_transform)
	undo_redo.add_undo_property(in_node, "global_transform", original_transform)
	undo_redo.commit_action()

func select_node(in_node : Node3D):
	undo_redo.create_action("Select Node")
	undo_redo.add_do_property(self, "active_node", in_node)
	var select_array : Array[Node3D] = [in_node]
	undo_redo.add_do_property(self, "selected_nodes", select_array)
	if editing_scene:
		var transform_target := get_transform_gizmo_target(in_node)
		undo_redo.add_do_method(editing_scene.translate_gizmo.set_target_node.bind(transform_target))
		undo_redo.add_do_method(editing_scene.rotate_gizmo.set_target_node.bind(transform_target))
		undo_redo.add_do_method(editing_scene.add_road_gizmo.set_target_node.bind(in_node))
	undo_redo.add_undo_property(self, "active_node", active_node)
	undo_redo.add_undo_property(self, "selected_nodes", selected_nodes)
	if editing_scene:
		var previous_transform_target := get_transform_gizmo_target(active_node)
		undo_redo.add_undo_method(editing_scene.translate_gizmo.set_target_node.bind(previous_transform_target))
		undo_redo.add_undo_method(editing_scene.rotate_gizmo.set_target_node.bind(previous_transform_target))
		undo_redo.add_undo_method(editing_scene.add_road_gizmo.set_target_node.bind(active_node))
	undo_redo.add_do_method(emit_selection_changed)
	undo_redo.add_undo_method(emit_selection_changed)
	undo_redo.commit_action()

func select_additional_node(in_node : Node3D):
	undo_redo.create_action("Select Node")
	undo_redo.add_do_property(self, "active_node", in_node)
	var select_array : Array[Node3D] = selected_nodes.duplicate()
	if !select_array.has(in_node):
		select_array.append(in_node)
	undo_redo.add_do_property(self, "selected_nodes", select_array)
	if editing_scene:
		var transform_target := get_transform_gizmo_target(in_node)
		undo_redo.add_do_method(editing_scene.translate_gizmo.set_target_node.bind(transform_target))
		undo_redo.add_do_method(editing_scene.rotate_gizmo.set_target_node.bind(transform_target))
		undo_redo.add_do_method(editing_scene.add_road_gizmo.set_target_node.bind(in_node))
	undo_redo.add_undo_property(self, "active_node", active_node)
	undo_redo.add_undo_property(self, "selected_nodes", selected_nodes)
	if editing_scene:
		var previous_transform_target := get_transform_gizmo_target(active_node)
		undo_redo.add_undo_method(editing_scene.translate_gizmo.set_target_node.bind(previous_transform_target))
		undo_redo.add_undo_method(editing_scene.rotate_gizmo.set_target_node.bind(previous_transform_target))
		undo_redo.add_undo_method(editing_scene.add_road_gizmo.set_target_node.bind(active_node))
	undo_redo.add_do_method(emit_selection_changed)
	undo_redo.add_undo_method(emit_selection_changed)
	undo_redo.commit_action()

func deselect_all():
	undo_redo.create_action("Deselect All")
	undo_redo.add_do_property(self, "active_node", null)
	var select_array : Array[Node3D] = []
	undo_redo.add_do_property(self, "selected_nodes", select_array)
	if editing_scene:
		undo_redo.add_do_method(editing_scene.translate_gizmo.set_target_node.bind(null))
		undo_redo.add_do_method(editing_scene.rotate_gizmo.set_target_node.bind(null))
		undo_redo.add_do_method(editing_scene.add_road_gizmo.set_target_node.bind(null))
	undo_redo.add_undo_property(self, "active_node", active_node)
	undo_redo.add_undo_property(self, "selected_nodes", selected_nodes)
	if editing_scene:
		undo_redo.add_undo_method(editing_scene.translate_gizmo.set_target_node.bind(active_node))
		undo_redo.add_undo_method(editing_scene.rotate_gizmo.set_target_node.bind(active_node))
		undo_redo.add_undo_method(editing_scene.add_road_gizmo.set_target_node.bind(active_node))
	undo_redo.add_do_method(emit_selection_changed)
	undo_redo.add_undo_method(emit_selection_changed)
	undo_redo.commit_action()

func get_selectable_node_from_collider(in_collider : Node) -> Node3D:
	var node := in_collider
	while node:
		if node is Node3D and node.has_method("get_selection_priority"):
			if !node.has_method("is_selectable") or node.is_selectable():
				return node
		node = node.get_parent()
	return null

# Called when the node enters the scene tree for the first time.
func _ready():
	pass # Replace with function body.


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta):
	if Input.is_action_just_pressed("Undo"):
		if undo_redo.has_undo():
			undo_redo.undo()
	elif Input.is_action_just_pressed("Redo"):
		if undo_redo.has_redo():
			undo_redo.redo()
	
	if editing_scene and can_select and Input.is_action_just_pressed("RightMouse"):
		var selectable_nodes : Array[Node3D] = []
		var mpc := editing_scene.mouse_picker_cast
		editing_scene.update_mouse_casts(true)
		mpc.clear_exceptions()
		mpc.force_raycast_update()
		while mpc.is_colliding():
			var selectable_node := get_selectable_node_from_collider(mpc.get_collider())
			if selectable_node and !selectable_nodes.has(selectable_node):
				selectable_nodes.append(selectable_node)
			mpc.add_exception(mpc.get_collider())
			mpc.force_raycast_update()
		if selectable_nodes.size() == 0:
			deselect_all()
			return
		var desired_node_to_select : Node3D
		var lowest_priority := 1024
		for node in selectable_nodes:
			var priority : int = node.get_selection_priority()
			if priority == -1:
				continue
			if priority < lowest_priority:
				lowest_priority = priority
				desired_node_to_select = node
		if !desired_node_to_select:
			deselect_all()
			return
		#DebugDraw3D.draw_arrow(mpc.global_position, mpc.to_global(mpc.target_position), Color.RED, 0.5, true, 3)
		if Input.is_action_pressed("Shift"):
			select_additional_node(desired_node_to_select)
		else:
			select_node(desired_node_to_select)
