class_name RoadPathBezier extends RoadPath

const RoadShapeRoundedSquareScript := preload("res://core/road_shape_rounded_square.gd")
const RoadShapeRoundedSquareOpenScript := preload("res://core/road_shape_open_rounded_square.gd")

@export var native_curve : Resource

const ROAD_SHAPE_FLAT := 0
const ROAD_SHAPE_CYLINDER := 1
const ROAD_SHAPE_CYLINDER_OPEN := 2
const ROAD_SHAPE_PIPE := 3
const ROAD_SHAPE_PIPE_OPEN := 4
const ROAD_SHAPE_ROUNDED_SQUARE := 5
const ROAD_SHAPE_ROUNDED_SQUARE_OPEN := 6
const CONTROL_STRIDE := 24
const ADD_CONTROL_POINT_PREVIEW_STEPS := 96
const ADD_CONTROL_POINT_MAX_SCREEN_DISTANCE := 60.0

var _preview_material_cache := {}
var bezier_handle_nodes : Array[BezierHandle] = []
var point_changes := false
var smoothed_bezier_path : PackedVector3Array = []
var centerline_mesh_instance : MeshInstance3D
var centerline_mesh := ImmediateMesh.new()
var centerline_material := StandardMaterial3D.new()
var add_point_preview_mesh_instance : MeshInstance3D
var add_point_preview_material := StandardMaterial3D.new()

func _preview_material(material_name : String) -> Material:
	if _preview_material_cache.has(material_name):
		return _preview_material_cache[material_name]
	var material := StandardMaterial3D.new()
	material.resource_name = material_name
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.vertex_color_use_as_albedo = true
	match material_name:
		"track_rail":
			material.albedo_color = Color(0.72, 0.74, 0.76, 1.0)
		"embed_border":
			material.albedo_color = Color(0.96, 0.92, 0.78, 1.0)
		"embed_ice":
			material.albedo_color = Color(0.36, 0.78, 1.0, 1.0)
		"embed_recharge":
			material.albedo_color = Color(0.30, 1.0, 0.62, 1.0)
		"embed_dirt":
			material.albedo_color = Color(0.56, 0.40, 0.24, 1.0)
		"embed_lava":
			material.albedo_color = Color(1.0, 0.25, 0.08, 1.0)
		"embed_hole":
			material.albedo_color = Color(0.02, 0.02, 0.025, 1.0)
		_:
			material.albedo_color = Color(0.50, 0.52, 0.54, 1.0)
	_preview_material_cache[material_name] = material
	return material

func _ensure_editor_visuals() -> void:
	if !centerline_mesh_instance:
		centerline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		centerline_material.vertex_color_use_as_albedo = true
		centerline_mesh_instance = MeshInstance3D.new()
		centerline_mesh_instance.mesh = centerline_mesh
		centerline_mesh_instance.top_level = true
		add_child(centerline_mesh_instance)
	if !add_point_preview_mesh_instance:
		add_point_preview_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		add_point_preview_material.albedo_color = Color.RED
		var sphere_mesh := SphereMesh.new()
		sphere_mesh.radius = 1.0
		sphere_mesh.height = 2.0
		add_point_preview_mesh_instance = MeshInstance3D.new()
		add_point_preview_mesh_instance.mesh = sphere_mesh
		add_point_preview_mesh_instance.material_override = add_point_preview_material
		add_point_preview_mesh_instance.top_level = true
		add_child(add_point_preview_mesh_instance)

func _hide_editor_visuals() -> void:
	if centerline_mesh_instance:
		centerline_mesh_instance.visible = false
	if add_point_preview_mesh_instance:
		add_point_preview_mesh_instance.visible = false

func _update_centerline_visual() -> void:
	_ensure_editor_visuals()
	centerline_mesh.clear_surfaces()
	if smoothed_bezier_path.size() < 2:
		centerline_mesh_instance.visible = false
		return
	centerline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, centerline_material)
	centerline_mesh.surface_set_color(Color.ROYAL_BLUE)
	for i in smoothed_bezier_path.size() - 1:
		centerline_mesh.surface_add_vertex(smoothed_bezier_path[i])
		centerline_mesh.surface_add_vertex(smoothed_bezier_path[i + 1])
	centerline_mesh.surface_end()
	centerline_mesh_instance.visible = smoothed_bezier_path.size() > 1

func _update_add_point_preview(pos : Vector3, visible_now : bool) -> void:
	_ensure_editor_visuals()
	add_point_preview_mesh_instance.visible = visible_now
	if visible_now:
		add_point_preview_mesh_instance.global_position = pos
		var cam := FZGlobal.current_cam
		var size := 1.0
		if cam:
			size = maxf(1.0, pos.distance_to(cam.global_position) * 0.01)
		add_point_preview_mesh_instance.scale = Vector3.ONE * size

func _ensure_native_curve() -> void:
	if !native_curve:
		native_curve = ClassDB.instantiate("TrackEditorCurve")

func get_control_point_count() -> int:
	_ensure_native_curve()
	return native_curve.get_control_point_count()

func add_control_point(
	in_time : float,
	in_position : Vector3,
	in_rotation : Basis,
	in_scale : Vector3,
	in_handle_in : float,
	in_handle_out : float,
	in_index : int = -1,
	in_rot_ease_type : int = 0,
	in_rot_ease_strength : float = 1.0,
	in_twist_ease_type : int = 0,
	in_twist_ease_strength : float = 1.0,
	in_scale_ease_type : int = 0,
	in_scale_ease_strength : float = 1.0) -> void:
	_ensure_native_curve()
	var insert_index := in_index
	if insert_index < 0:
		insert_index = native_curve.get_control_point_count()
	native_curve.insert_control_point(
		insert_index,
		in_time,
		in_position,
		in_rotation,
		in_scale,
		in_handle_in,
		in_handle_out,
		in_rot_ease_type,
		in_rot_ease_strength,
		in_twist_ease_type,
		in_twist_ease_strength,
		in_scale_ease_type,
		in_scale_ease_strength)
	if native_curve.get_control_point_count() >= 2:
		calculate_bezier_times()
		sort_beziers()
		refresh_handle_nodes()
		calculate_curves_from_bezier()
	_try_generate_mesh()

func get_root_transform(in_t : float) -> Transform3D:
	_ensure_native_curve()
	return native_curve.sample_bezier(in_t)

func _native_shape_type() -> int:
	if road_shape is RoadShapeCylinder:
		return ROAD_SHAPE_CYLINDER
	if road_shape is RoadShapeCylinderOpen:
		return ROAD_SHAPE_CYLINDER_OPEN
	if road_shape is RoadShapePipe:
		return ROAD_SHAPE_PIPE
	if road_shape is RoadShapePipeOpen:
		return ROAD_SHAPE_PIPE_OPEN
	if road_shape is RoadShapeRoundedSquareOpenScript:
		return ROAD_SHAPE_ROUNDED_SQUARE_OPEN
	if road_shape is RoadShapeRoundedSquareScript:
		return ROAD_SHAPE_ROUNDED_SQUARE
	return ROAD_SHAPE_FLAT

func _native_openness_value() -> float:
	return 1.0

func _native_openness_packet() -> PackedFloat32Array:
	if road_shape is RoadShapeCylinderOpen:
		return road_shape.openness.build_packet()
	if road_shape is RoadShapePipeOpen:
		return road_shape.openness.build_packet()
	if road_shape is RoadShapeRoundedSquareOpenScript:
		return road_shape.openness.build_packet()
	return PackedFloat32Array()

func _native_rounded_width_packet() -> PackedFloat32Array:
	if road_shape is RoadShapeRoundedSquareScript:
		return road_shape.width.build_packet()
	return PackedFloat32Array()

func _native_rounded_height_packet() -> PackedFloat32Array:
	if road_shape is RoadShapeRoundedSquareScript:
		return road_shape.height.build_packet()
	return PackedFloat32Array()

func _native_rounded_radius_packet() -> PackedFloat32Array:
	if road_shape is RoadShapeRoundedSquareScript:
		return road_shape.radius.build_packet()
	return PackedFloat32Array()

func _native_rounded_open_rotation_packet() -> PackedFloat32Array:
	if road_shape is RoadShapeRoundedSquareOpenScript:
		return road_shape.open_rotation.build_packet()
	return PackedFloat32Array()

func _native_modulation_packet() -> PackedFloat32Array:
	var packet := PackedFloat32Array()
	packet.append(road_shape.modulation_table.size())
	for mod in road_shape.modulation_table:
		packet.append_array(mod.modulation_effect.build_packet())
		packet.append_array(mod.modulation_height.build_packet())
	return packet

func _native_embed_packet() -> PackedFloat32Array:
	var packet := PackedFloat32Array()
	packet.append(road_shape.embed_table.size())
	for embed in road_shape.embed_table:
		packet.append(embed.road_start)
		packet.append(embed.road_end)
		packet.append(embed.embed_type)
		packet.append_array(embed.left_boundary.build_packet())
		packet.append_array(embed.right_boundary.build_packet())
	return packet

func _native_surface_config(in_t : Vector2) -> Dictionary:
	return {
		"t": in_t,
		"shape_type": _native_shape_type(),
		"openness": _native_openness_value(),
		"rounded_width": 1.0,
		"rounded_height": 1.0,
		"rounded_radius": 0.0,
		"rounded_open_rotation": 0.0,
		"openness_curve": _native_openness_packet(),
		"rounded_width_curve": _native_rounded_width_packet(),
		"rounded_height_curve": _native_rounded_height_packet(),
		"rounded_radius_curve": _native_rounded_radius_packet(),
		"rounded_open_rotation_curve": _native_rounded_open_rotation_packet(),
		"modulation_curves": _native_modulation_packet(),
		"embed_curves": _native_embed_packet(),
	}

func _control_position(points : PackedFloat32Array, index : int) -> Vector3:
	var base := index * CONTROL_STRIDE
	return Vector3(points[base + 1], points[base + 2], points[base + 3])

func _control_basis(points : PackedFloat32Array, index : int) -> Basis:
	var base := index * CONTROL_STRIDE
	return Basis(
		Vector3(points[base + 4], points[base + 5], points[base + 6]),
		Vector3(points[base + 7], points[base + 8], points[base + 9]),
		Vector3(points[base + 10], points[base + 11], points[base + 12]))

func _control_scale(points : PackedFloat32Array, index : int) -> Vector3:
	var base := index * CONTROL_STRIDE
	return Vector3(points[base + 13], points[base + 14], points[base + 15])

func _control_point_insert_pick(cam : Camera3D) -> Dictionary:
	if !cam:
		return {}
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_gizmo_cast and scene.mouse_gizmo_cast.is_colliding():
		return {}
	var mouse_pos := get_viewport().get_mouse_position()
	var best_time := 0.0
	var best_dist := INF
	var previous_screen := Vector2.ZERO
	var previous_time := 0.0
	var previous_valid := false
	for i in ADD_CONTROL_POINT_PREVIEW_STEPS + 1:
		var sample_time := float(i) / float(ADD_CONTROL_POINT_PREVIEW_STEPS)
		var world_pos := get_root_transform(sample_time).origin
		if cam.is_position_behind(world_pos):
			previous_valid = false
			continue
		var screen_pos := cam.unproject_position(world_pos)
		if previous_valid:
			var closest := Geometry2D.get_closest_point_to_segment(mouse_pos, previous_screen, screen_pos)
			var dist := mouse_pos.distance_squared_to(closest)
			if dist < best_dist:
				var screen_len := previous_screen.distance_to(screen_pos)
				var local_t := 0.0 if screen_len <= 0.001 else previous_screen.distance_to(closest) / screen_len
				best_time = lerpf(previous_time, sample_time, local_t)
				best_dist = dist
		previous_screen = screen_pos
		previous_time = sample_time
		previous_valid = true
	if best_dist > ADD_CONTROL_POINT_MAX_SCREEN_DISTANCE * ADD_CONTROL_POINT_MAX_SCREEN_DISTANCE:
		return {}
	return {
		"time": clampf(best_time, 0.0, 1.0),
		"distance": best_dist,
	}

func _control_interval_for_time(points : PackedFloat32Array, point_count : int, time : float) -> int:
	for i in point_count - 1:
		var next_base := (i + 1) * CONTROL_STRIDE
		if time <= points[next_base]:
			return i
	return point_count - 2

func _try_alt_add_control_point(scene : TrackEditingScene, points : PackedFloat32Array, point_count : int) -> bool:
	if !Input.is_action_pressed("Alt") or scene.pointer_action_busy_for(self):
		_update_add_point_preview(Vector3.ZERO, false)
		return false
	var pick := _control_point_insert_pick(FZGlobal.current_cam)
	if pick.is_empty():
		_update_add_point_preview(Vector3.ZERO, false)
		return false
	var insert_time : float = pick["time"]
	var insert_transform := get_root_transform(insert_time)
	_update_add_point_preview(insert_transform.origin, true)
	if !Input.is_action_just_pressed("LeftMouse"):
		return false
	if !scene.begin_pointer_action(self):
		return false
	get_viewport().set_input_as_handled()
	var left_index := _control_interval_for_time(points, point_count, insert_time)
	var right_index := left_index + 1
	var base_1 := left_index * CONTROL_STRIDE
	var base_2 := right_index * CONTROL_STRIDE
	var time_1 : float = points[base_1]
	var time_2 : float = points[base_2]
	var span := maxf(time_2 - time_1, 0.0001)
	var span_t := clampf((insert_time - time_1) / span, 0.0, 1.0)
	var scale_1 : Vector3 = _control_scale(points, left_index)
	var scale_2 : Vector3 = _control_scale(points, right_index)
	var handle_out_1 : float = points[base_1 + 17]
	var handle_in_2 : float = points[base_2 + 16]
	points[base_1 + 17] = handle_out_1 * span_t
	points[base_2 + 16] = handle_in_2 * (1.0 - span_t)
	native_curve.set_control_points(points)
	add_control_point(
		insert_time,
		insert_transform.origin,
		insert_transform.basis,
		scale_1.lerp(scale_2, span_t),
		handle_out_1 * span_t,
		handle_in_2 * (1.0 - span_t),
		right_index)
	if right_index < bezier_handle_nodes.size():
		FZGlobal.select_node(bezier_handle_nodes[right_index])
	scene.end_pointer_action(self)
	return true

func _write_control_transform(points : PackedFloat32Array, index : int, position : Vector3, basis : Basis, scale : Vector3, handle_in : float, handle_out : float) -> void:
	var base := index * CONTROL_STRIDE
	var clean_basis := basis.orthonormalized()
	points[base + 1] = position.x
	points[base + 2] = position.y
	points[base + 3] = position.z
	points[base + 4] = clean_basis.x.x
	points[base + 5] = clean_basis.x.y
	points[base + 6] = clean_basis.x.z
	points[base + 7] = clean_basis.y.x
	points[base + 8] = clean_basis.y.y
	points[base + 9] = clean_basis.y.z
	points[base + 10] = clean_basis.z.x
	points[base + 11] = clean_basis.z.y
	points[base + 12] = clean_basis.z.z
	points[base + 13] = scale.x
	points[base + 14] = scale.y
	points[base + 15] = scale.z
	points[base + 16] = handle_in
	points[base + 17] = handle_out

func get_surface_position(in_t : Vector2) -> Vector3:
	_ensure_native_curve()
	return native_curve.sample_surface_position(_native_surface_config(in_t))

func get_surface_positions(in_points : PackedVector2Array) -> PackedVector3Array:
	_ensure_native_curve()
	var config := _native_surface_config(Vector2.ZERO)
	config.erase("t")
	config["points"] = in_points
	return native_curve.sample_surface_positions(config)

func get_surface_local_positions(in_points : PackedVector2Array) -> PackedVector3Array:
	_ensure_native_curve()
	var config := _native_surface_config(Vector2.ZERO)
	config.erase("t")
	config["points"] = in_points
	return native_curve.sample_surface_local_positions(config)

func _get_surface(in_t : Vector2) -> Basis:
	var pos := get_surface_position(in_t)
	return Basis(pos, Vector3.UP, Vector3.ZERO)

func _try_generate_mesh(update_collision := true) -> void:
	if !road_mesh_instance:
		create_road_mesh_instance()
	_ensure_native_curve()
	if native_curve.get_control_point_count() < 2:
		return
	var mesh_data : Dictionary = native_curve.build_preview_mesh_with_curves({
		"shape_type": _native_shape_type(),
		"horizontal_segments": horizontal_road_mesh_segments,
		"uv_multiplier": road_uv_multiplier,
		"mesh_subdivision_length": 30.0,
		"mesh_subdivision_angle_radians": deg_to_rad(3.0),
		"openness": _native_openness_value(),
		"rounded_width": 1.0,
		"rounded_height": 1.0,
		"rounded_radius": 0.0,
		"rounded_open_rotation": 0.0,
		"left_rail_height": left_rail_height,
		"right_rail_height": right_rail_height,
		"left_rail_start": left_rail_start,
		"left_rail_end": left_rail_end,
		"right_rail_start": right_rail_start,
		"right_rail_end": right_rail_end,
		"openness_curve": _native_openness_packet(),
		"rounded_width_curve": _native_rounded_width_packet(),
		"rounded_height_curve": _native_rounded_height_packet(),
		"rounded_radius_curve": _native_rounded_radius_packet(),
		"rounded_open_rotation_curve": _native_rounded_open_rotation_packet(),
		"modulation_curves": _native_modulation_packet(),
		"embed_curves": _native_embed_packet(),
	})
	segment_length = mesh_data.get("segment_length", 0.0)
	var arr_mesh := ArrayMesh.new()
	var native_surfaces : Array = mesh_data.get("surfaces", [])
	if !native_surfaces.is_empty():
		for surface in native_surfaces:
			var arrays := []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = surface["vertices"]
			arrays[Mesh.ARRAY_NORMAL] = surface["normals"]
			arrays[Mesh.ARRAY_TEX_UV] = surface["uvs"]
			arrays[Mesh.ARRAY_TEX_UV2] = surface["uv2"]
			arrays[Mesh.ARRAY_COLOR] = surface["colors"]
			arr_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			arr_mesh.surface_set_material(arr_mesh.get_surface_count() - 1, _preview_material(surface.get("material_name", "track_surface")))
	else:
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = mesh_data["vertices"]
		arrays[Mesh.ARRAY_NORMAL] = mesh_data["normals"]
		arrays[Mesh.ARRAY_TEX_UV] = mesh_data["uvs"]
		arrays[Mesh.ARRAY_TEX_UV2] = mesh_data["uv2"]
		arrays[Mesh.ARRAY_COLOR] = mesh_data["colors"]
		arr_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		arr_mesh.surface_set_material(0, _preview_material("track_surface"))
	road_mesh_instance.mesh = arr_mesh
	if update_collision and road_mesh_instance.mesh and road_collision_shape_mesh:
		var col_triangles := road_mesh_instance.mesh.create_trimesh_shape()
		road_collision_shape_mesh.set_faces(col_triangles.get_faces())
	smoothed_bezier_path = native_curve.build_centerline_points(maxi(2, floori(segment_length * 0.1)))

func calculate_curves_from_bezier(subdivision : int = 64) -> void:
	_ensure_native_curve()

func build_export_curve_matrix(subdivision : int = 64) -> PackedFloat32Array:
	_ensure_native_curve()
	return native_curve.build_baked_curve_matrix(subdivision)

func sort_beziers() -> void:
	_ensure_native_curve()
	native_curve.sort_control_points_by_time()

func calculate_bezier_times() -> void:
	_ensure_native_curve()
	if native_curve.get_control_point_count() < 2:
		return
	native_curve.respace_control_point_times(8)

func remove_bezier_point_at_index(in_index : int) -> void:
	_ensure_native_curve()
	native_curve.remove_control_point(in_index)
	if native_curve.get_control_point_count() >= 2:
		calculate_bezier_times()
		sort_beziers()
		refresh_handle_nodes()
		calculate_curves_from_bezier()
	_try_generate_mesh()

func _ready():
	_ensure_native_curve()
	create_road_mesh_instance()
	if native_curve.get_control_point_count() >= 2:
		sort_beziers()
		refresh_handle_nodes()
		calculate_curves_from_bezier()
	_try_generate_mesh()

func refresh_handle_nodes() -> void:
	_ensure_native_curve()
	for node in get_children():
		if node is BezierHandle:
			node.free()
	bezier_handle_nodes.clear()
	for i in native_curve.get_control_point_count():
		var point_handle := BezierHandle.new()
		point_handle.position = native_curve.get_control_point_position(i)
		point_handle.basis = native_curve.get_control_point_rotation(i)
		point_handle.cp_scale = native_curve.get_control_point_scale(i)
		point_handle.in_handle_length = native_curve.get_control_point_handle_in(i)
		point_handle.out_handle_length = native_curve.get_control_point_handle_out(i)
		point_handle.time = native_curve.get_control_point_time(i)
		point_handle.associated_index = i
		add_child(point_handle)
		bezier_handle_nodes.append(point_handle)
		point_handle.name = "Point " + str(i + 1)

func _process(delta):
	var scene := FZGlobal.editing_scene
	if !(road_shape and native_curve):
		_hide_editor_visuals()
		return
	var point_count : int = native_curve.get_control_point_count()
	if point_count < 2:
		_hide_editor_visuals()
		return
	if !(road_mesh_instance and road_collision):
		_hide_editor_visuals()
		return
	road_mesh_instance.global_transform = Transform3D.IDENTITY
	road_collision.global_transform = Transform3D.IDENTITY
	if bezier_handle_nodes.size() != point_count:
		refresh_handle_nodes()
		point_count = native_curve.get_control_point_count()

	var control_points : PackedFloat32Array = native_curve.get_control_points()
	if scene and scene.tool_mode_allows_control_point_gizmos() and (FZGlobal.active_node == self or get_children().has(FZGlobal.active_node)):
		_update_centerline_visual()
		if _try_alt_add_control_point(scene, control_points, point_count):
			return
	else:
		_hide_editor_visuals()

	var next_control_points : PackedFloat32Array = control_points.duplicate()
	for i in point_count:
		var handle := bezier_handle_nodes[i]
		var handle_basis := handle.global_basis.orthonormalized()
		if _control_position(control_points, i) != handle.global_position:
			point_changes = true
		if _control_basis(control_points, i) != handle_basis:
			point_changes = true
		if _control_scale(control_points, i) != handle.cp_scale:
			point_changes = true
		var base := i * CONTROL_STRIDE
		if control_points[base + 16] != handle.in_handle_length:
			point_changes = true
		if control_points[base + 17] != handle.out_handle_length:
			point_changes = true
		_write_control_transform(next_control_points, i, handle.global_position, handle_basis, handle.cp_scale, handle.in_handle_length, handle.out_handle_length)

	if point_changes:
		native_curve.set_control_points(next_control_points)

	if point_changes:
		var update_collision := Time.get_ticks_msec() > last_gen_time + 100
		if update_collision:
			last_gen_time = Time.get_ticks_msec()
		point_changes = false
		calculate_bezier_times()
		calculate_curves_from_bezier()
		_try_generate_mesh(update_collision)
