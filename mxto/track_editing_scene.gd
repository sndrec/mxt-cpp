class_name TrackEditingScene extends Node3D

const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")
const EmbedGizmoScript := preload("res://core/embed_gizmo.gd")
const RailGizmoScript := preload("res://core/rail_gizmo.gd")
const ModulationGizmoScript := preload("res://core/modulation_gizmo.gd")
const ShapeCurveGizmoScript := preload("res://core/shape_curve_gizmo.gd")
const MeshLayoutGizmoScript := preload("res://core/mesh_layout_gizmo.gd")
const CheckpointGizmoScript := preload("res://core/checkpoint_gizmo.gd")
const RoadPathSpiralScript := preload("res://core/road_path_spiral.gd")
const SpiralCurveGizmoScript := preload("res://core/spiral_curve_gizmo.gd")
const TrackTriggerScript := preload("res://core/track_trigger.gd")
const TrackTriggerGizmoScript := preload("res://core/track_trigger_gizmo.gd")

enum ToolMode {
	EDIT_SEGMENT,
	ADD_SEGMENT,
	ADD_EMBED,
	EDIT_EMBED,
	EDIT_RAILS,
	EDIT_MODULATION,
	EDIT_SHAPE,
	EDIT_MESH_LAYOUT,
	EDIT_SPIRAL,
	EDIT_CHECKPOINTS,
	ADD_OBJECT,
	EDIT_OBJECT,
	EDIT_TRACK,
}

enum SegmentKind {
	LINE,
	BEZIER,
	SPIRAL,
}

signal tool_mode_changed(mode : ToolMode)
signal track_structure_changed

@onready var edit_cam: Camera3D = $EditorCamera
@onready var ref_cam: Camera3D = $RefCamera
@onready var track_root: TrackRoot = $TrackRoot
@onready var grid_origin: Node3D = $GridOrigin
@onready var mouse_picker_cast: RayCast3D = $MousePickerCast
@onready var mouse_gizmo_cast: RayCast3D = $MouseGizmoCast

@onready var translate_gizmo: TranslateGizmo = $TranslateGizmo
@onready var rotate_gizmo: RotateGizmo = $RotateGizmo
@onready var add_road_gizmo: AddRoadGizmo = $AddRoadGizmo

var cam_dist := 300.0
var cam_dist_desired := 300.0
var cam_origin := Vector3.ZERO
var cam_origin_desired := Vector3.ZERO
var last_trackball_dir := Vector3.UP
var last_pan_plane := Plane.PLANE_XZ
var last_mouse_pos := Vector2.ZERO

var active_path : RoadPath
var tool_mode : ToolMode = ToolMode.EDIT_SEGMENT
var desired_road_type := ENUMS.ROAD_TYPE.STANDARD
var desired_segment_kind := SegmentKind.BEZIER
var desired_embed_type := RoadEmbed.EmbedType.RECHARGE
var desired_trigger_type := 0
var pending_embed_add := false
var editor_cross_section_t := 0.5
var draw_segment_curve := true
var draw_segment_handles := true
var delete_selected_pressed := false
var pointer_action_owner : Node
var embed_gizmo : Node3D
var rail_gizmo : Node3D
var modulation_gizmo : Node3D
var shape_curve_gizmo : Node3D
var mesh_layout_gizmo : Node3D
var checkpoint_gizmo : Node3D
var spiral_curve_gizmo : Node3D
var track_trigger_gizmo : Node3D
var embed_add_preview_mesh_instance : MeshInstance3D
var embed_add_preview_mesh := ImmediateMesh.new()
var embed_add_preview_material := StandardMaterial3D.new()
var object_add_preview_mesh_instance : MeshInstance3D
var object_add_preview_mesh : BoxMesh
var object_add_preview_material := StandardMaterial3D.new()

const simple_mesh_layout : PackedFloat32Array = [0.0, 0.25, 0.5, 0.75, 1.0]
const cylinder_mesh_layout : PackedFloat32Array = [0.0, 0.032258064516129, 0.064516129032258, 0.096774193548387, 0.12903225806452, 0.16129032258065, 0.19354838709677, 0.2258064516129, 0.25806451612903, 0.29032258064516, 0.32258064516129, 0.35483870967742, 0.38709677419355, 0.41935483870968, 0.45161290322581, 0.48387096774194, 0.51612903225806, 0.54838709677419, 0.58064516129032, 0.61290322580645, 0.64516129032258, 0.67741935483871, 0.70967741935484, 0.74193548387097, 0.7741935483871, 0.80645161290323, 0.83870967741935, 0.87096774193548, 0.90322580645161, 0.93548387096774, 0.96774193548387, 1.0]
const DEFAULT_EMBED_LENGTH := 0.3
const DEFAULT_EMBED_WIDTH := 0.7

func set_tool_mode(in_mode : ToolMode) -> void:
	if tool_mode == in_mode:
		return
	pointer_action_owner = null
	if in_mode != ToolMode.ADD_EMBED:
		pending_embed_add = false
	tool_mode = in_mode
	tool_mode_changed.emit(tool_mode)

func tool_mode_allows_transform_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SEGMENT or tool_mode == ToolMode.EDIT_SPIRAL

func tool_mode_allows_segment_add_gizmo() -> bool:
	return tool_mode == ToolMode.EDIT_SEGMENT and Input.is_action_pressed("Alt")

func tool_mode_allows_control_point_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SEGMENT and draw_segment_handles

func tool_mode_allows_control_point_curve() -> bool:
	return tool_mode == ToolMode.EDIT_SEGMENT and draw_segment_curve

func tool_mode_allows_embed_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_EMBED

func tool_mode_allows_rail_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_RAILS

func tool_mode_allows_modulation_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_MODULATION

func tool_mode_allows_shape_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SHAPE

func tool_mode_allows_mesh_layout_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_MESH_LAYOUT

func tool_mode_allows_spiral_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_SPIRAL

func tool_mode_allows_checkpoint_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_CHECKPOINTS

func tool_mode_allows_track_trigger_gizmos() -> bool:
	return tool_mode == ToolMode.EDIT_OBJECT

func set_active_embed(in_path : RoadPath, in_embed_index : int) -> void:
	if !embed_gizmo:
		return
	embed_gizmo.set_target_embed(in_path, in_embed_index)

func set_active_rail_path(in_path : RoadPath) -> void:
	if !rail_gizmo:
		return
	rail_gizmo.set_target_path(in_path)

func set_active_modulation(in_path : RoadPath, in_modulation_index : int) -> void:
	if !modulation_gizmo:
		return
	modulation_gizmo.set_target_modulation(in_path, in_modulation_index)

func set_active_shape_path(in_path : RoadPath) -> void:
	if !shape_curve_gizmo:
		return
	shape_curve_gizmo.set_target_path(in_path)

func set_active_mesh_layout_path(in_path : RoadPath) -> void:
	if mesh_layout_gizmo:
		mesh_layout_gizmo.set_target_path(in_path)

func set_active_spiral_path(in_path : RoadPath) -> void:
	if !spiral_curve_gizmo:
		return
	spiral_curve_gizmo.set_target_path(in_path)

func set_active_checkpoint_path(in_path : RoadPath) -> void:
	if !checkpoint_gizmo:
		return
	checkpoint_gizmo.set_target_path(in_path)

func set_active_track_trigger(in_trigger : Node3D) -> void:
	if !track_trigger_gizmo:
		return
	track_trigger_gizmo.set_target_trigger(in_trigger)

func begin_pointer_action(owner : Node) -> bool:
	if mouse_over_editor_ui():
		return false
	if pointer_action_owner and pointer_action_owner != owner:
		return false
	pointer_action_owner = owner
	return true

func owns_pointer_action(owner : Node) -> bool:
	return pointer_action_owner == owner

func pointer_action_busy_for(owner : Node) -> bool:
	return pointer_action_owner and pointer_action_owner != owner

func end_pointer_action(owner : Node) -> void:
	if pointer_action_owner == owner:
		pointer_action_owner = null

func find_camera_trackball_normal() -> Vector3:
	var sphere_pos := ref_cam.position + ref_cam.basis.z * -100
	var sphere_radius := 95
	var ray := ref_cam.project_ray_normal(get_viewport().get_mouse_position())
	var intersect := Geometry3D.segment_intersects_sphere(ref_cam.position, ref_cam.position + ray * 1024, sphere_pos, sphere_radius)
	if intersect.size() > 0:
		return intersect[1]
	else:
		return last_trackball_dir

func _ready() -> void:
	FZGlobal.current_cam = edit_cam
	FZGlobal.editing_scene = self
	FZGlobal.current_track = track_root
	embed_gizmo = EmbedGizmoScript.new()
	add_child(embed_gizmo)
	rail_gizmo = RailGizmoScript.new()
	add_child(rail_gizmo)
	modulation_gizmo = ModulationGizmoScript.new()
	add_child(modulation_gizmo)
	shape_curve_gizmo = ShapeCurveGizmoScript.new()
	add_child(shape_curve_gizmo)
	mesh_layout_gizmo = MeshLayoutGizmoScript.new()
	add_child(mesh_layout_gizmo)
	checkpoint_gizmo = CheckpointGizmoScript.new()
	add_child(checkpoint_gizmo)
	spiral_curve_gizmo = SpiralCurveGizmoScript.new()
	add_child(spiral_curve_gizmo)
	track_trigger_gizmo = TrackTriggerGizmoScript.new()
	add_child(track_trigger_gizmo)
	translate_gizmo.mouse_cast = mouse_gizmo_cast
	rotate_gizmo.mouse_cast = mouse_gizmo_cast
	add_road_gizmo.mouse_cast = mouse_gizmo_cast
	embed_gizmo.mouse_cast = mouse_gizmo_cast
	rail_gizmo.mouse_cast = mouse_gizmo_cast
	modulation_gizmo.mouse_cast = mouse_gizmo_cast
	shape_curve_gizmo.mouse_cast = mouse_gizmo_cast
	mesh_layout_gizmo.mouse_cast = mouse_gizmo_cast
	checkpoint_gizmo.mouse_cast = mouse_gizmo_cast
	spiral_curve_gizmo.mouse_cast = mouse_gizmo_cast
	track_trigger_gizmo.mouse_cast = mouse_gizmo_cast

func save_edit_source(path : String) -> Error:
	return track_root.save_edit_source(path)

func export_mxt_track(path : String) -> Error:
	return track_root.export_mxt_track(path)

func update_mouse_casts(force_update := false) -> void:
	var ray_end := edit_cam.global_position + edit_cam.project_ray_normal(get_viewport().get_mouse_position()) * 4096
	mouse_picker_cast.global_position = edit_cam.global_position
	mouse_picker_cast.target_position = mouse_picker_cast.to_local(ray_end)
	mouse_gizmo_cast.global_position = edit_cam.global_position
	mouse_gizmo_cast.target_position = mouse_gizmo_cast.to_local(ray_end)
	if force_update:
		mouse_picker_cast.force_raycast_update()
		mouse_gizmo_cast.force_raycast_update()

func _road_path_from_collider(collider : Object) -> RoadPath:
	var node := collider as Node
	while node:
		if node is RoadPath:
			return node
		node = node.get_parent()
	return null

func _pick_road_path_under_mouse() -> Dictionary:
	mouse_picker_cast.clear_exceptions()
	mouse_picker_cast.force_raycast_update()
	while mouse_picker_cast.is_colliding():
		var collider := mouse_picker_cast.get_collider()
		var path := _road_path_from_collider(collider)
		if path:
			var point := mouse_picker_cast.get_collision_point()
			mouse_picker_cast.clear_exceptions()
			return {"path": path, "point": point}
		var collision_object := collider as CollisionObject3D
		if !collision_object:
			break
		mouse_picker_cast.add_exception(collision_object)
		mouse_picker_cast.force_raycast_update()
	mouse_picker_cast.clear_exceptions()
	return {}

func _mouse_over_gizmo() -> bool:
	if !mouse_gizmo_cast:
		return false
	mouse_gizmo_cast.force_raycast_update()
	return mouse_gizmo_cast.is_colliding()

func mouse_over_editor_ui() -> bool:
	return get_viewport().gui_get_hovered_control() != null

func _closest_surface_param(segment : RoadPath, world_pos : Vector3) -> Vector2:
	var best_param := Vector2(0.0, 0.5)
	var best_distance := INF
	var center := best_param
	var span := Vector2(2.0, 1.0)
	for refine_pass in 4:
		var query := PackedVector2Array()
		var params : Array[Vector2] = []
		var x_steps := 9
		var y_steps := 17
		for y in y_steps:
			for x in x_steps:
				var tx := clampf(center.x - span.x * 0.5 + span.x * (float(x) / float(x_steps - 1)), -1.0, 1.0)
				var ty := clampf(center.y - span.y * 0.5 + span.y * (float(y) / float(y_steps - 1)), 0.0, 1.0)
				var param := Vector2(tx, ty)
				params.append(param)
				query.append(param)
		var positions := segment.get_surface_positions(query)
		for i in positions.size():
			var distance := positions[i].distance_squared_to(world_pos)
			if distance < best_distance:
				best_distance = distance
				best_param = params[i]
		center = best_param
		span *= 0.25
	return best_param

func _surface_basis(segment : RoadPath, param : Vector2) -> Basis:
	var tx := clampf(param.x, -1.0, 1.0)
	var ty := clampf(param.y, 0.0, 1.0)
	var eps := 0.002
	var tx2 := clampf(tx + eps, -1.0, 1.0)
	if is_equal_approx(tx2, tx):
		tx2 = clampf(tx - eps, -1.0, 1.0)
	var ty2 := clampf(ty + eps, 0.0, 1.0)
	if is_equal_approx(ty2, ty):
		ty2 = clampf(ty - eps, 0.0, 1.0)
	var points := segment.get_surface_positions(PackedVector2Array([
		Vector2(tx, ty),
		Vector2(tx2, ty),
		Vector2(tx, ty2),
	]))
	var base := points[0]
	var right := (points[1] - base).normalized()
	var forward := (points[2] - base).normalized()
	if right.is_zero_approx() or forward.is_zero_approx():
		var root := segment.get_root_transform(ty)
		right = root.basis.x.normalized()
		forward = root.basis.z.normalized()
	var normal := right.cross(forward).normalized()
	if normal.is_zero_approx():
		normal = Vector3.UP
	right = forward.cross(normal).normalized()
	return Basis(right, -normal, forward).orthonormalized()

func _ensure_add_previews() -> void:
	if !embed_add_preview_mesh_instance:
		embed_add_preview_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		embed_add_preview_material.vertex_color_use_as_albedo = true
		embed_add_preview_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		embed_add_preview_mesh_instance = MeshInstance3D.new()
		embed_add_preview_mesh_instance.name = "EmbedAddPreview"
		embed_add_preview_mesh_instance.mesh = embed_add_preview_mesh
		embed_add_preview_mesh_instance.top_level = true
		add_child(embed_add_preview_mesh_instance)
	if !object_add_preview_mesh_instance:
		object_add_preview_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		object_add_preview_material.albedo_color = Color(0.3, 0.8, 1.0, 0.35)
		object_add_preview_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		object_add_preview_mesh = BoxMesh.new()
		object_add_preview_mesh_instance = MeshInstance3D.new()
		object_add_preview_mesh_instance.name = "ObjectAddPreview"
		object_add_preview_mesh_instance.mesh = object_add_preview_mesh
		object_add_preview_mesh_instance.material_override = object_add_preview_material
		object_add_preview_mesh_instance.top_level = true
		add_child(object_add_preview_mesh_instance)

func _hide_add_previews() -> void:
	if embed_add_preview_mesh_instance:
		embed_add_preview_mesh_instance.visible = false
	if object_add_preview_mesh_instance:
		object_add_preview_mesh_instance.visible = false

func _show_embed_add_preview(path : RoadPath, surface_t : Vector2) -> void:
	_ensure_add_previews()
	var length := minf(DEFAULT_EMBED_LENGTH, 1.0)
	var width := minf(DEFAULT_EMBED_WIDTH, 2.0)
	var road_start := clampf(surface_t.y - length * 0.5, 0.0, 1.0 - length)
	var road_end := road_start + length
	var left := clampf(surface_t.x - width * 0.5, -1.0, 1.0 - width)
	var right := left + width
	var corners := path.get_surface_positions(PackedVector2Array([
		Vector2(left, road_start),
		Vector2(right, road_start),
		Vector2(right, road_end),
		Vector2(left, road_end),
	]))
	embed_add_preview_mesh.clear_surfaces()
	embed_add_preview_mesh.surface_begin(Mesh.PRIMITIVE_LINES, embed_add_preview_material)
	embed_add_preview_mesh.surface_set_color(Color(0.35, 1.0, 0.65, 0.85))
	for i in 4:
		embed_add_preview_mesh.surface_add_vertex(corners[i])
		embed_add_preview_mesh.surface_add_vertex(corners[(i + 1) % 4])
	embed_add_preview_mesh.surface_end()
	embed_add_preview_mesh_instance.visible = true

func _show_object_add_preview(path : RoadPath, surface_t : Vector2) -> void:
	_ensure_add_previews()
	var extents : Vector3 = TrackTriggerScript.TRIGGER_EXTENTS[clampi(desired_trigger_type, 0, TrackTriggerScript.TRIGGER_EXTENTS.size() - 1)]
	object_add_preview_mesh.size = extents * 2.0
	object_add_preview_mesh_instance.global_transform = Transform3D(_surface_basis(path, surface_t), path.get_surface_position(surface_t))
	object_add_preview_mesh_instance.visible = true

func _update_alt_add_previews() -> void:
	_hide_add_previews()
	if !Input.is_action_pressed("Alt") or mouse_over_editor_ui() or _mouse_over_gizmo():
		return
	if tool_mode != ToolMode.EDIT_EMBED and tool_mode != ToolMode.EDIT_OBJECT:
		return
	var hit := _pick_road_path_under_mouse()
	if hit.is_empty():
		return
	var path := hit["path"] as RoadPath
	var surface_t := _closest_surface_param(path, hit["point"])
	if tool_mode == ToolMode.EDIT_EMBED:
		_show_embed_add_preview(path, surface_t)
	elif tool_mode == ToolMode.EDIT_OBJECT:
		_show_object_add_preview(path, surface_t)

func _handle_add_object_input() -> void:
	if tool_mode != ToolMode.EDIT_OBJECT:
		return
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return
	if mouse_over_editor_ui():
		return
	if _mouse_over_gizmo():
		return
	if !begin_pointer_action(self):
		return
	var hit := _pick_road_path_under_mouse()
	if !hit.is_empty():
		var path := hit["path"] as RoadPath
		var surface_t := _closest_surface_param(path, hit["point"])
		var trigger := add_track_trigger(desired_trigger_type, path, surface_t)
		if trigger:
			FZGlobal.select_node(trigger)
			get_viewport().set_input_as_handled()
	end_pointer_action(self)

func _handle_add_embed_input() -> void:
	if tool_mode != ToolMode.EDIT_EMBED:
		return
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return
	if mouse_over_editor_ui():
		return
	if _mouse_over_gizmo():
		return
	if !begin_pointer_action(self):
		return
	var hit := _pick_road_path_under_mouse()
	if !hit.is_empty():
		var path := hit["path"] as RoadPath
		var surface_t := _closest_surface_param(path, hit["point"])
		var embed_index := add_road_embed(desired_embed_type, path, surface_t)
		if embed_index >= 0:
			active_path = path
			set_active_embed(path, embed_index)
			FZGlobal.select_node(path)
			get_viewport().set_input_as_handled()
	end_pointer_action(self)

func _handle_add_segment_input() -> void:
	if tool_mode != ToolMode.ADD_SEGMENT:
		return
	if !Input.is_action_pressed("Alt") or !Input.is_action_just_pressed("LeftMouse"):
		return
	if mouse_over_editor_ui():
		return
	if _mouse_over_gizmo():
		return
	if !begin_pointer_action(self):
		return
	var hit := _pick_road_path_under_mouse()
	if !hit.is_empty():
		var path := hit["path"] as RoadPath
		var new_segment := add_desired_track_segment_after(path)
		if new_segment:
			active_path = new_segment
			add_road_gizmo.set_target_node(new_segment)
			FZGlobal.select_node(new_segment)
			get_viewport().set_input_as_handled()
	end_pointer_action(self)

func _process(delta: float) -> void:
	if pointer_action_owner and !is_instance_valid(pointer_action_owner):
		pointer_action_owner = null
	_handle_delete_selected_input()
	if Input.is_action_just_pressed("MiddleMouse"):
		last_trackball_dir = find_camera_trackball_normal()
		if mouse_picker_cast.is_colliding():
			last_pan_plane = Plane(-edit_cam.basis.z, mouse_picker_cast.get_collision_point())
		else:
			var sphere_pos := cam_origin.lerp(edit_cam.global_position, 0.5)
			var sphere_radius := cam_dist * 0.5
			var ray := edit_cam.project_ray_normal(get_viewport().get_mouse_position())
			var p1 := edit_cam.global_position
			var intersect := Geometry3D.segment_intersects_sphere(p1 + ray * cam_dist * 2.2, p1, sphere_pos, sphere_radius)
			if intersect.size() > 0:
				var use_normal := intersect[1].slerp(-edit_cam.basis.z, 0.8)
				last_pan_plane = Plane(use_normal, intersect[0])
	if Input.is_action_pressed("MiddleMouse") and !Input.is_action_pressed("Shift"):
		var cur_trackball_dir := find_camera_trackball_normal()
		if !cur_trackball_dir.is_equal_approx(last_trackball_dir):
			var axis := last_trackball_dir.cross(cur_trackball_dir).normalized()
			var angle := last_trackball_dir.signed_angle_to(cur_trackball_dir, axis)
			var port_size : Vector2i = get_viewport().size
			var mp := get_viewport().get_mouse_position() / Vector2(port_size)
			var dist_to_edge := mp.distance_to(Vector2(0.5, 0.5))
			dist_to_edge = minf(1.0, dist_to_edge * 2)
			var angle_from_center := axis.angle_to(Vector3.FORWARD) - PI * 0.5
			angle_from_center = clampf(remap(angle_from_center, -0.08, 0.08, 1, -1), -1, 1)
			axis = axis.slerp(Vector3.FORWARD, dist_to_edge * angle_from_center)
			axis = edit_cam.basis * axis
			axis = axis.normalized()
			edit_cam.basis = edit_cam.basis.rotated(axis, angle * -24)
			last_trackball_dir = cur_trackball_dir
	
	if Input.is_action_just_pressed("ZoomIn"):
		cam_dist_desired *= 0.8
	if Input.is_action_just_pressed("ZoomOut"):
		cam_dist_desired *= (1.0 / 0.8)
	
	if Input.is_action_pressed("MiddleMouse") and Input.is_action_pressed("Shift"):
		last_trackball_dir = find_camera_trackball_normal()
		var r1 := edit_cam.project_ray_normal(last_mouse_pos)
		var r2 := edit_cam.project_ray_normal(get_viewport().get_mouse_position())
		var p1 = last_pan_plane.intersects_ray(edit_cam.position, r1)
		var p2 = last_pan_plane.intersects_ray(edit_cam.position, r2)
		if p1 and p2:
			cam_origin_desired += -(p2 - p1)
	cam_origin = cam_origin.lerp(cam_origin_desired, delta * 30)
	cam_dist = lerpf(cam_dist, cam_dist_desired, delta * 12)
	edit_cam.position = cam_origin + edit_cam.basis.z * cam_dist
	last_mouse_pos = get_viewport().get_mouse_position()
	grid_origin.position = grid_origin.position.lerp(snapped(cam_origin, Vector3(16, 16, 16)), delta * 30)
	update_mouse_casts(true)
	_update_alt_add_previews()
	_handle_add_embed_input()
	_handle_add_object_input()

func _apply_road_type_to_path(path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> void:
	match road_type:
		ENUMS.ROAD_TYPE.STANDARD:
			path.road_shape = RoadShape.new()
			path.horizontal_road_mesh_segments = simple_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.PIPE:
			path.road_shape = RoadShapePipe.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.CYLINDER:
			path.road_shape = RoadShapeCylinder.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.PIPE_OPEN:
			path.road_shape = RoadShapePipeOpen.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.CYLINDER_OPEN:
			path.road_shape = RoadShapeCylinderOpen.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.ROUNDED_SQUARE:
			path.road_shape = RoadShapeRoundedSquareScript.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()
		ENUMS.ROAD_TYPE.ROUNDED_SQUARE_OPEN:
			path.road_shape = RoadShapeRoundedSquareOpenScript.new()
			path.horizontal_road_mesh_segments = cylinder_mesh_layout.duplicate()

func _road_type_from_path(path : RoadPath) -> int:
	if path.road_shape is RoadShapePipe:
		return ENUMS.ROAD_TYPE.PIPE
	if path.road_shape is RoadShapeCylinder:
		return ENUMS.ROAD_TYPE.CYLINDER
	if path.road_shape is RoadShapePipeOpen:
		return ENUMS.ROAD_TYPE.PIPE_OPEN
	if path.road_shape is RoadShapeCylinderOpen:
		return ENUMS.ROAD_TYPE.CYLINDER_OPEN
	if path.road_shape is RoadShapeRoundedSquareOpenScript:
		return ENUMS.ROAD_TYPE.ROUNDED_SQUARE_OPEN
	if path.road_shape is RoadShapeRoundedSquareScript:
		return ENUMS.ROAD_TYPE.ROUNDED_SQUARE
	return ENUMS.ROAD_TYPE.STANDARD

func _segment_kind_from_path(path : RoadPath) -> int:
	var script : Script = path.get_script() as Script
	if script and script.resource_path == "res://core/road_path_spiral.gd":
		return SegmentKind.SPIRAL
	if path is RoadPathLine:
		return SegmentKind.LINE
	return SegmentKind.BEZIER

func _track_root_path_to(segment : RoadPath) -> NodePath:
	if !track_root or !segment:
		return NodePath()
	return track_root.get_path_to(segment)

func _append_unique_segment_path(paths : Array[NodePath], segment_path : NodePath) -> void:
	if segment_path.is_empty() or paths.has(segment_path):
		return
	paths.append(segment_path)

func _segment_paths_without(paths : Array[NodePath], segment_path : NodePath) -> Array[NodePath]:
	var out : Array[NodePath] = []
	for path in paths:
		if path != segment_path:
			out.append(path)
	return out

func _delete_track_trigger(trigger : Node3D) -> void:
	if !trigger or !is_instance_valid(trigger):
		return
	if FZGlobal.active_node == trigger:
		FZGlobal.clear_selection_immediate()
	trigger.queue_free()
	track_structure_changed.emit()

func _delete_road_path(segment : RoadPath) -> void:
	if !segment or !is_instance_valid(segment) or segment.get_parent() != track_root:
		return
	var segment_path := _track_root_path_to(segment)
	var previous_paths : Array[NodePath] = segment.previous_segment_paths.duplicate()
	var next_paths : Array[NodePath] = segment.next_segment_paths.duplicate()
	for previous_path in previous_paths:
		var previous_segment := track_root.get_node_or_null(previous_path) as RoadPath
		if !previous_segment:
			continue
		previous_segment.next_segment_paths = _segment_paths_without(previous_segment.next_segment_paths, segment_path)
		for next_path in next_paths:
			_append_unique_segment_path(previous_segment.next_segment_paths, next_path)
	for next_path in next_paths:
		var next_segment := track_root.get_node_or_null(next_path) as RoadPath
		if !next_segment:
			continue
		next_segment.previous_segment_paths = _segment_paths_without(next_segment.previous_segment_paths, segment_path)
		for previous_path in previous_paths:
			_append_unique_segment_path(next_segment.previous_segment_paths, previous_path)
	for child in track_root.get_children():
		if child and child.get_script() == TrackTriggerScript and child.get("segment_path") == segment_path:
			child.queue_free()
	if track_root.first_segment_path == segment_path:
		var next_first : RoadPath = null
		if !next_paths.is_empty():
			next_first = track_root.get_node_or_null(next_paths[0]) as RoadPath
		track_root.set_first_segment(next_first)
	FZGlobal.clear_selection_immediate()
	segment.queue_free()
	track_structure_changed.emit()

func _delete_bezier_handle(handle : BezierHandle) -> void:
	var path := handle.get_parent() as RoadPathBezier
	if !path:
		return
	var index := handle.associated_index
	if index <= 0 or index >= path.get_control_point_count() - 1:
		return
	FZGlobal.clear_selection_immediate()
	path.remove_bezier_point_at_index(index)
	track_structure_changed.emit()

func _handle_delete_selected_input() -> void:
	var delete_now := Input.is_key_pressed(KEY_DELETE)
	var just_delete := delete_now and !delete_selected_pressed
	delete_selected_pressed = delete_now
	if !just_delete or pointer_action_owner or mouse_over_editor_ui():
		return
	var selected := FZGlobal.active_node
	if !selected or !is_instance_valid(selected):
		return
	if selected is BezierHandle:
		_delete_bezier_handle(selected)
	elif selected is RoadPath:
		_delete_road_path(selected)
	elif selected.get_script() == TrackTriggerScript:
		_delete_track_trigger(selected)

func _insert_segment_after(in_path : RoadPath, new_track_piece : RoadPath) -> void:
	track_root.add_child(new_track_piece)
	var id_to_put_above := track_root.get_children().find(in_path)
	track_root.move_child(new_track_piece, id_to_put_above + 1)
	var previous_path := _track_root_path_to(in_path)
	var new_path := _track_root_path_to(new_track_piece)
	var previous_paths : Array[NodePath] = []
	_append_unique_segment_path(previous_paths, previous_path)
	var next_paths : Array[NodePath] = in_path.next_segment_paths.duplicate()
	var inserted_next_paths : Array[NodePath] = []
	_append_unique_segment_path(inserted_next_paths, new_path)
	for next_path in in_path.next_segment_paths:
		var next_segment := track_root.get_node_or_null(next_path) as RoadPath
		if !next_segment:
			continue
		next_segment.previous_segment_paths = _segment_paths_without(next_segment.previous_segment_paths, previous_path)
		_append_unique_segment_path(next_segment.previous_segment_paths, new_path)
	new_track_piece.previous_segment_paths = previous_paths
	new_track_piece.next_segment_paths = next_paths
	in_path.next_segment_paths = inserted_next_paths

func add_bezier_track_segment_after(in_path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> RoadPathBezier:
	var new_track_piece := RoadPathBezier.new()
	_apply_road_type_to_path(new_track_piece, road_type)
	
	var latest_track_piece_transform := in_path.get_root_transform(1.0)
	var latest_track_piece_rotation := latest_track_piece_transform.basis.orthonormalized()
	var latest_track_piece_scale := latest_track_piece_transform.basis.get_scale()
	
	new_track_piece.add_control_point(
		0.0,
		latest_track_piece_transform.origin,
		latest_track_piece_rotation,
		latest_track_piece_scale,
		166.0,
		166.0,
		0)
	new_track_piece.add_control_point(
		1.0,
		latest_track_piece_transform.origin + latest_track_piece_rotation.z.normalized() * 500.0,
		latest_track_piece_rotation,
		latest_track_piece_scale,
		166.0,
		166.0,
		1)
	_insert_segment_after(in_path, new_track_piece)
	track_structure_changed.emit()
	return new_track_piece
	

func add_regular_track_segment_after(in_path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> RoadPathLine:
	var new_track_piece := RoadPathLine.new()
	var latest_track_piece_transform := in_path.get_root_transform(1.0)
	_apply_road_type_to_path(new_track_piece, road_type)
	new_track_piece.start_pos = latest_track_piece_transform.origin
	new_track_piece.start_rotation = latest_track_piece_transform.basis.orthonormalized()
	new_track_piece.start_scale = latest_track_piece_transform.basis.get_scale()
	new_track_piece.end_pos = latest_track_piece_transform.origin + latest_track_piece_transform.basis.orthonormalized().z * 250
	new_track_piece.end_rotation = latest_track_piece_transform.basis.orthonormalized()
	new_track_piece.end_scale = latest_track_piece_transform.basis.get_scale()
	_insert_segment_after(in_path, new_track_piece)
	track_structure_changed.emit()
	return new_track_piece

func add_spiral_track_segment_after(in_path : RoadPath, road_type : ENUMS.ROAD_TYPE) -> RoadPath:
	var new_track_piece : RoadPath = RoadPathSpiralScript.new()
	var latest_track_piece_transform := in_path.get_root_transform(1.0)
	_apply_road_type_to_path(new_track_piece, road_type)
	new_track_piece.axis_transform = latest_track_piece_transform
	new_track_piece.axis_transform.basis = latest_track_piece_transform.basis.orthonormalized()
	new_track_piece.spiral_axis = Vector3.UP
	_insert_segment_after(in_path, new_track_piece)
	track_structure_changed.emit()
	return new_track_piece

func add_desired_track_segment_after(in_path : RoadPath) -> RoadPath:
	var road_type := _road_type_from_path(in_path)
	match _segment_kind_from_path(in_path):
		SegmentKind.LINE:
			return add_regular_track_segment_after(in_path, road_type)
		SegmentKind.SPIRAL:
			return add_spiral_track_segment_after(in_path, road_type)
		_:
			return add_bezier_track_segment_after(in_path, road_type)

func add_road_embed(embed_type : int, in_path : RoadPath, surface_t := Vector2(0.0, 0.5)) -> int:
	if !in_path:
		return -1
	var length := minf(DEFAULT_EMBED_LENGTH, 1.0)
	var width := minf(DEFAULT_EMBED_WIDTH, 2.0)
	var road_start := clampf(surface_t.y - length * 0.5, 0.0, 1.0 - length)
	var road_end := road_start + length
	var left := clampf(surface_t.x - width * 0.5, -1.0, 1.0 - width)
	var right := left + width
	var new_embed := RoadEmbed.new()
	new_embed.road_start = road_start
	new_embed.road_end = road_end
	new_embed.embed_type = embed_type
	new_embed.left_boundary.set_point_offset(0, road_start)
	new_embed.left_boundary.set_point_offset(1, road_end)
	new_embed.right_boundary.set_point_offset(0, road_start)
	new_embed.right_boundary.set_point_offset(1, road_end)
	new_embed.left_boundary.set_point_value(0, left)
	new_embed.left_boundary.set_point_value(1, left)
	new_embed.right_boundary.set_point_value(0, right)
	new_embed.right_boundary.set_point_value(1, right)
	in_path.road_shape.embed_table.append(new_embed)
	in_path._try_generate_mesh()
	track_structure_changed.emit()
	return in_path.road_shape.embed_table.size() - 1

func add_track_trigger(trigger_type : int, in_path : RoadPath, surface_t := Vector2(0.0, 0.5)) -> Node3D:
	if !in_path:
		return null
	var trigger : Node3D = TrackTriggerScript.new()
	trigger.set("trigger_type", trigger_type)
	track_root.add_child(trigger)
	trigger.name = String(trigger.call("trigger_label")) + " Trigger"
	trigger.call("place_on_segment", in_path, surface_t.x, surface_t.y)
	track_structure_changed.emit()
	return trigger
