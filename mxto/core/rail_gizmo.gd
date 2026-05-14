class_name RailGizmo extends Node3D

enum HandleId {
	LEFT_START,
	LEFT_END,
	RIGHT_START,
	RIGHT_END,
}

const HANDLE_RADIUS := 4.0
const RAIL_SEARCH_STEPS := 10
const RAIL_SEARCH_PASSES := 5
const NORMAL_EPSILON := 0.002

var mouse_cast : RayCast3D
var target_path : RoadPath
var dragging := false
var drag_handle := -1
var drag_plane := Plane.PLANE_XZ
var outline_mesh_instance : MeshInstance3D
var outline_mesh := ImmediateMesh.new()
var outline_material := StandardMaterial3D.new()
var handle_materials : Array[StandardMaterial3D] = []
var handles : Array[StaticBody3D] = []

func _ready() -> void:
	outline_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	outline_material.vertex_color_use_as_albedo = true
	outline_mesh_instance = MeshInstance3D.new()
	outline_mesh_instance.mesh = outline_mesh
	outline_mesh_instance.top_level = true
	add_child(outline_mesh_instance)
	for i in HandleId.RIGHT_END + 1:
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = Color(0.92, 0.78, 0.18, 1.0)
		handle_materials.append(material)
		var body := StaticBody3D.new()
		body.set_collision_layer_value(16, true)
		body.set_collision_mask_value(16, true)
		body.set_collision_layer_value(1, false)
		body.set_collision_mask_value(1, false)
		var collision := CollisionShape3D.new()
		var sphere := SphereShape3D.new()
		sphere.radius = HANDLE_RADIUS
		collision.shape = sphere
		body.add_child(collision)
		var mesh_instance := MeshInstance3D.new()
		var sphere_mesh := SphereMesh.new()
		sphere_mesh.radius = HANDLE_RADIUS
		sphere_mesh.height = HANDLE_RADIUS * 2.0
		mesh_instance.mesh = sphere_mesh
		mesh_instance.material_override = material
		body.add_child(mesh_instance)
		add_child(body)
		handles.append(body)
	set_target_path(null)

func set_target_path(in_path : RoadPath) -> void:
	target_path = in_path
	dragging = false
	drag_handle = -1

func _set_colliders_enabled(enabled : bool) -> void:
	for handle in handles:
		handle.set_collision_layer_value(16, enabled)

func _side_tx(handle_id : int) -> float:
	return -1.0 if handle_id == HandleId.RIGHT_START or handle_id == HandleId.RIGHT_END else 1.0

func _handle_ty(handle_id : int) -> float:
	match handle_id:
		HandleId.LEFT_START:
			return target_path.left_rail_start
		HandleId.LEFT_END:
			return target_path.left_rail_end
		HandleId.RIGHT_START:
			return target_path.right_rail_start
		HandleId.RIGHT_END:
			return target_path.right_rail_end
	return 0.0

func _side_height(handle_id : int) -> float:
	return target_path.right_rail_height if handle_id == HandleId.RIGHT_START or handle_id == HandleId.RIGHT_END else target_path.left_rail_height

func _surface_point(tx : float, ty : float) -> Vector3:
	var points := target_path.get_surface_positions(PackedVector2Array([Vector2(tx, clampf(ty, 0.0, 1.0))]))
	if points.is_empty():
		return Vector3.ZERO
	return points[0]

func _surface_normal(tx : float, ty : float) -> Vector3:
	var tx_side := tx + NORMAL_EPSILON if tx < 0.0 else tx - NORMAL_EPSILON
	var ty_side := minf(ty + NORMAL_EPSILON, 1.0) if ty < 0.5 else maxf(ty - NORMAL_EPSILON, 0.0)
	var samples := target_path.get_surface_positions(PackedVector2Array([
		Vector2(tx, ty),
		Vector2(tx_side, ty),
		Vector2(tx, ty_side),
	]))
	if samples.size() < 3:
		return Vector3.UP
	var tangent_x := (samples[1] - samples[0]) if tx < 0.0 else (samples[0] - samples[1])
	var tangent_y := (samples[2] - samples[0]) if ty < 0.5 else (samples[0] - samples[2])
	tangent_x = tangent_x.normalized()
	tangent_y = tangent_y.normalized()
	var normal := tangent_y.cross(tangent_x).normalized()
	if normal.is_zero_approx():
		return Vector3.UP
	return normal

func _rail_top_position(handle_id : int) -> Vector3:
	var tx := _side_tx(handle_id)
	var ty := clampf(_handle_ty(handle_id), 0.0, 1.0)
	return _surface_point(tx, ty) + _surface_normal(tx, ty) * _side_height(handle_id)

func _update_visuals() -> void:
	var p0 := _rail_top_position(HandleId.LEFT_START)
	var p1 := _rail_top_position(HandleId.LEFT_END)
	var p2 := _rail_top_position(HandleId.RIGHT_START)
	var p3 := _rail_top_position(HandleId.RIGHT_END)
	handles[HandleId.LEFT_START].global_position = p0
	handles[HandleId.LEFT_END].global_position = p1
	handles[HandleId.RIGHT_START].global_position = p2
	handles[HandleId.RIGHT_END].global_position = p3
	outline_mesh.clear_surfaces()
	outline_mesh.surface_begin(Mesh.PRIMITIVE_LINES, outline_material)
	outline_mesh.surface_set_color(Color(0.92, 0.78, 0.18, 1.0))
	if target_path.left_rail_height > 0.0:
		outline_mesh.surface_add_vertex(p0)
		outline_mesh.surface_add_vertex(p1)
	if target_path.right_rail_height > 0.0:
		outline_mesh.surface_add_vertex(p2)
		outline_mesh.surface_add_vertex(p3)
	outline_mesh.surface_end()

func _hovered_handle() -> int:
	var scene := FZGlobal.editing_scene
	if scene and scene.mouse_over_editor_ui():
		return -1
	if !mouse_cast or !mouse_cast.is_colliding():
		return -1
	var collider := mouse_cast.get_collider()
	for i in handles.size():
		if collider == handles[i]:
			return i
	return -1

func _set_handle_colours(hovered : int) -> void:
	for i in handle_materials.size():
		if dragging and i == drag_handle:
			handle_materials[i].albedo_color = Color.WHITE
		elif i == hovered:
			handle_materials[i].albedo_color = Color(1.0, 0.95, 0.4, 1.0)
		else:
			handle_materials[i].albedo_color = Color(0.92, 0.78, 0.18, 1.0)

func _closest_side_ty(world_pos : Vector3, tx : float) -> float:
	var best_ty := 0.0
	var best_dist := INF
	var min_ty := 0.0
	var max_ty := 1.0
	for pass_index in RAIL_SEARCH_PASSES:
		for i in RAIL_SEARCH_STEPS + 1:
			var ty := lerpf(min_ty, max_ty, float(i) / float(RAIL_SEARCH_STEPS))
			var dist := _surface_point(tx, ty).distance_squared_to(world_pos)
			if dist < best_dist:
				best_dist = dist
				best_ty = ty
		var radius := (max_ty - min_ty) / float(RAIL_SEARCH_STEPS)
		min_ty = maxf(0.0, best_ty - radius)
		max_ty = minf(1.0, best_ty + radius)
	return best_ty

func _write_handle(handle_id : int, world_pos : Vector3) -> void:
	var tx := _side_tx(handle_id)
	var ty := _closest_side_ty(world_pos, tx)
	var base := _surface_point(tx, ty)
	var normal := _surface_normal(tx, ty)
	var height := maxf(0.0, (world_pos - base).dot(normal))
	var tunnel := target_path.road_shape is RoadShapeTunnel
	match handle_id:
		HandleId.LEFT_START:
			target_path.left_rail_start = clampf(ty, 0.0, target_path.left_rail_end)
			target_path.left_rail_height = height
		HandleId.LEFT_END:
			target_path.left_rail_end = clampf(ty, target_path.left_rail_start, 1.0)
			target_path.left_rail_height = height
		HandleId.RIGHT_START:
			target_path.right_rail_start = clampf(ty, 0.0, target_path.right_rail_end)
			target_path.right_rail_height = height
		HandleId.RIGHT_END:
			target_path.right_rail_end = clampf(ty, target_path.right_rail_start, 1.0)
			target_path.right_rail_height = height
	if tunnel:
		if handle_id == HandleId.LEFT_START or handle_id == HandleId.RIGHT_START:
			target_path.left_rail_start = clampf(ty, 0.0, target_path.left_rail_end)
			target_path.right_rail_start = clampf(ty, 0.0, target_path.right_rail_end)
		else:
			target_path.left_rail_end = clampf(ty, target_path.left_rail_start, 1.0)
			target_path.right_rail_end = clampf(ty, target_path.right_rail_start, 1.0)
		target_path.left_rail_height = height
		target_path.right_rail_height = height

func _process(delta : float) -> void:
	var scene := FZGlobal.editing_scene
	if !scene or !scene.tool_mode_allows_rail_gizmos() or !is_instance_valid(target_path):
		if scene:
			scene.end_pointer_action(self)
		visible = false
		outline_mesh_instance.visible = false
		_set_colliders_enabled(false)
		dragging = false
		return
	visible = true
	outline_mesh_instance.visible = true
	_set_colliders_enabled(true)
	_update_visuals()
	var cam := FZGlobal.current_cam
	if !cam:
		return
	var ray_dir := cam.project_ray_normal(get_viewport().get_mouse_position())
	var hovered := _hovered_handle()
	_set_handle_colours(hovered)
	if Input.is_action_just_pressed("LeftMouse") and hovered != -1:
		if !scene.begin_pointer_action(self):
			return
		dragging = true
		drag_handle = hovered
		drag_plane = Plane(-cam.global_basis.z.normalized(), handles[hovered].global_position)
		get_viewport().set_input_as_handled()
	if Input.is_action_just_released("LeftMouse"):
		if dragging or scene.owns_pointer_action(self):
			get_viewport().set_input_as_handled()
		dragging = false
		drag_handle = -1
		scene.end_pointer_action(self)
	if dragging and drag_handle != -1:
		var hit = drag_plane.intersects_ray(cam.global_position, ray_dir)
		if hit is Vector3:
			_write_handle(drag_handle, hit)
			var update_collision := Time.get_ticks_msec() > target_path.last_gen_time + 100
			if update_collision:
				target_path.last_gen_time = Time.get_ticks_msec()
			target_path._try_generate_mesh(update_collision)
			_update_visuals()
