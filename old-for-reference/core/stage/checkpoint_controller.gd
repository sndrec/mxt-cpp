@tool

class_name CheckpointController extends Node3D

@onready var origin_handle := %OriginHandle as Marker3D
@onready var orientation_handle := %OrientationHandle as Marker3D
@onready var radius_handle := %RadiusHandle as Marker3D
@onready var respawn_handle := %RespawnHandle as Marker3D
@export var required : bool = false
@export var reset_gravity : bool = true

var editor_interface:Object

var our_checkpoint : Checkpoint = Checkpoint.new()

#static var plugin
#
#class CheckpointControllerInspector extends EditorInspectorPlugin:
	#func _can_handle(object): return object is CheckpointController
	#
	#func _parse_begin(object):
		#var cpc = object as CheckpointController
		#
		#var button = Button.new()
		#button.text = "Orient Checkpoint"
		#button.pressed.connect(cpc.orient_checkpoint_controller)
		#
		#var button_2 = Button.new()
		#button_2.text = "Orient Checkpoint Respawn"
		#button_2.pressed.connect(cpc.orient_respawn)
		#
		#var box_container = VBoxContainer.new()
		#box_container.add_child(button)
		#box_container.add_child(button_2)
		#
		#var container = MarginContainer.new()
		#container.add_theme_constant_override("margin_bottom", 10)
		#container.add_child(box_container)
		#
		#add_custom_control(container)
#
#func _enter_tree():
	#if Engine.is_editor_hint():
		#if plugin == null:
			#plugin = EditorPlugin.new()
			#plugin.add_inspector_plugin(CheckpointControllerInspector.new())

enum orient_states {
	NOT_ORIENTING,
	SETTING_ORIGIN,
	SETTING_ROTATION,
	SETTING_RADIUS
}

var cp_orient_state : orient_states = orient_states.NOT_ORIENTING
var respawn_orient_state : orient_states = orient_states.NOT_ORIENTING

func orient_checkpoint_controller() -> void:
	cp_orient_state = orient_states.SETTING_ORIGIN

func orient_respawn() -> void:
	respawn_orient_state = orient_states.SETTING_ORIGIN

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	if Engine.is_editor_hint():
		editor_interface = Engine.get_singleton("EditorInterface")
	update_checkpoint()

func update_checkpoint() -> void:
	our_checkpoint.position = origin_handle.global_position
	our_checkpoint.rotation = Basis.looking_at(orientation_handle.global_position - origin_handle.global_position, orientation_handle.global_basis.y, true).get_euler()
	our_checkpoint.radius = (radius_handle.global_position - origin_handle.global_position).length()
	our_checkpoint.respawn_transform = respawn_handle.global_transform
	our_checkpoint.required_checkpoint = required
	our_checkpoint.reset_gravity = reset_gravity
# Called every frame. 'delta' is the elapsed time since the previous frame.

var is_clicking := false
var click_timeout := 0
var saved_normal : Vector3 = Vector3.ZERO

func get_mouse_position_raycast() -> Dictionary:
	var space_state := PhysicsServer3D.space_get_direct_state(get_world_3d().space)
	var query := PhysicsRayQueryParameters3D.new()
	var editor_cam:Camera3D = editor_interface.get_editor_viewport_3d(0).get_camera_3d()
	query.from = editor_cam.global_position
	query.to = editor_cam.project_position(editor_interface.get_editor_viewport_3d(0).get_mouse_position(), 32768)
	query.collision_mask |= (1 | 2 | 4 | 8 | 16 | 32)
	var desired_pos := space_state.intersect_ray(query)
	return desired_pos

func _process(delta: float) -> void:
	if Engine.is_editor_hint():
		
		
		if scale.x != 1.0:
			scale.x = 1.0
		if scale.y != 1.0:
			scale.y = 1.0
		if scale.z != 1.0:
			scale.z = 1.0
		
		if respawn_handle.scale.x != 1.0:
			respawn_handle.scale.x = 1.0
		if respawn_handle.scale.y != 1.0:
			respawn_handle.scale.y = 1.0
		if respawn_handle.scale.z != 1.0:
			respawn_handle.scale.z = 1.0
		
		var just_clicked := false
		if Input.is_mouse_button_pressed(MOUSE_BUTTON_MIDDLE):
			if !is_clicking and Time.get_ticks_msec() > click_timeout + 0.5:
				just_clicked = true
				click_timeout = Time.get_ticks_msec()
			is_clicking = true
		else:
			is_clicking = false
		
		if cp_orient_state != orient_states.NOT_ORIENTING:
			#DebugDraw2D.set_text(str(name) + " orient state", cp_orient_state)
			if cp_orient_state == orient_states.SETTING_ORIGIN:
				var ray := get_mouse_position_raycast()
				if ray.size() > 0:
					DebugDraw3D.draw_sphere(ray.position, 0.25, Color.RED, delta)
					global_position = ray.position
					origin_handle.global_position = ray.position
					saved_normal = ray.normal
					if just_clicked:
						cp_orient_state = orient_states.SETTING_ROTATION
						just_clicked = false
			elif cp_orient_state == orient_states.SETTING_ROTATION:
				var ray := get_mouse_position_raycast()
				if ray.size() > 0:
					DebugDraw3D.draw_sphere(ray.position, 0.25, Color.BLUE, delta)
					var by := saved_normal
					var bz : Vector3 = (ray.position - global_position).normalized()
					var bx := bz.cross(by).normalized()
					bz = by.cross(bx)
					global_basis = Basis(-bx, by, bz)
					orientation_handle.global_position = ray.position
					if just_clicked:
						cp_orient_state = orient_states.SETTING_RADIUS
						#respawn_orient_state = orient_states.SETTING_ORIGIN
						just_clicked = false
			elif cp_orient_state == orient_states.SETTING_RADIUS:
				var editor_cam:Camera3D = editor_interface.get_editor_viewport_3d(0).get_camera_3d()
				var from:Vector3 = editor_cam.global_position
				var to := editor_cam.project_position(editor_interface.get_editor_viewport_3d(0).get_mouse_position(), 32768)
				var plane_intersect:Variant = our_checkpoint.checkpoint_plane.intersects_segment(from, to)
				if plane_intersect:
					radius_handle.global_position = plane_intersect
				if just_clicked:
					cp_orient_state = orient_states.NOT_ORIENTING
					respawn_orient_state = orient_states.SETTING_ORIGIN
					just_clicked = false
		
		
		if respawn_orient_state != orient_states.NOT_ORIENTING:
			#DebugDraw2D.set_text(str(name) + " respawn orient state", respawn_orient_state)
			var ray := get_mouse_position_raycast()
			if ray.size() > 0:
				if respawn_orient_state == orient_states.SETTING_ORIGIN:
					DebugDraw3D.draw_sphere(ray.position, 0.25, Color.RED, delta)
					respawn_handle.global_position = ray.position
					saved_normal = ray.normal
					if just_clicked:
						respawn_orient_state = orient_states.SETTING_ROTATION
						just_clicked = false
				elif respawn_orient_state == orient_states.SETTING_ROTATION:
					DebugDraw3D.draw_sphere(ray.position, 0.25, Color.BLUE, delta)
					var by := saved_normal
					var bz:Vector3 = (ray.position - respawn_handle.global_position).normalized()
					var bx := bz.cross(by).normalized()
					bz = by.cross(bx)
					respawn_handle.global_basis = Basis(-bx, by, bz)
					if just_clicked:
						respawn_orient_state = orient_states.NOT_ORIENTING
						just_clicked = false
		var should_draw := false
		if editor_interface:
			if editor_interface.get_selection().get_selected_nodes().has(self) or editor_interface.get_selection().get_selected_nodes().has(get_parent()):
				should_draw = true
		
		if get_parent().get_child(0) == self:
			for i in get_parent().get_child_count():
				if i < get_parent().get_child_count() - 1:
					var it_cp : CheckpointController = get_parent().get_child(i)
					var it_cp_2 : CheckpointController = get_parent().get_child(i + 1)
					DebugDraw3D.draw_arrow_line(it_cp.our_checkpoint.position, it_cp_2.our_checkpoint.position, Color.RED, 1.0, true, delta)
				else:
					var it_cp : CheckpointController = get_parent().get_child(i)
					var it_cp_2 : CheckpointController = get_parent().get_child(0)
					DebugDraw3D.draw_arrow_line(it_cp.our_checkpoint.position, it_cp_2.our_checkpoint.position, Color.RED, 1.0, true, delta)
		
		if !should_draw:
			for child in get_children():
				if editor_interface and editor_interface.get_selection().get_selected_nodes().has(child):
					should_draw = true
					break
		if should_draw:
			update_checkpoint()
			
			our_checkpoint.debug_draw_checkpoint(delta)
		
