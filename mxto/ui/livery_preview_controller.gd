class_name LiveryPreviewController
extends Node

signal camera_changed

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStamp = preload("res://vehicle/customization/car_livery_stamp.gd")
const CarRenderManager = preload("res://vehicle/car_render_manager.gd")
const GaragePreviewCameraControllerClass = preload("res://ui/garage_preview_camera_controller.gd")
const GARAGE_PREVIEW_WORLD_SCENE = preload("res://ui/garage_preview_world.tscn")

const PREVIEW_TARGET_HEIGHT := 0.5
const STAMP_EDIT_MIN_SCREEN_SIZE := 1.0

var preview_space: ColorRect
var preview_container: SubViewportContainer
var preview_viewport: SubViewport
var preview_root: Node3D
var preview_camera: Camera3D
var preview_vehicle: Node3D
var preview_vehicle_base_transform := Transform3D.IDENTITY
var render_manager: CarRenderManager
var edit_render_manager: CarRenderManager
var above_render_manager: CarRenderManager
var camera_controller := GaragePreviewCameraControllerClass.new()
var editing_mode := false

func initialize(owner_ui: Control) -> void:
	preview_space = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace")
	preview_space.mouse_filter = Control.MOUSE_FILTER_STOP
	preview_container = SubViewportContainer.new()
	preview_container.name = "GaragePreviewViewport"
	preview_container.stretch = true
	preview_container.mouse_filter = Control.MOUSE_FILTER_STOP
	preview_container.tooltip_text = "Left drag: orbit. Hold Ctrl to snap rotation to world 5° increments. Right, middle, or Shift+left drag: pan. Wheel: zoom. Double-click: reset view."
	preview_container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	preview_space.add_child(preview_container)
	preview_space.move_child(preview_container, 0)
	preview_container.gui_input.connect(_on_preview_gui_input)
	preview_space.resized.connect(_on_preview_resized)

	preview_viewport = SubViewport.new()
	preview_viewport.own_world_3d = true
	preview_viewport.transparent_bg = true
	preview_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	preview_viewport.size = Vector2i(maxi(1, int(preview_space.size.x)), maxi(1, int(preview_space.size.y)))
	preview_container.add_child(preview_viewport)

	preview_root = GARAGE_PREVIEW_WORLD_SCENE.instantiate()
	preview_root.name = "GaragePreviewWorld"
	preview_viewport.add_child(preview_root)

	render_manager = CarRenderManager.new()
	render_manager.name = "GaragePreviewRenderManager"
	render_manager.stamp_render_priority = 2
	preview_root.add_child(render_manager)

	edit_render_manager = CarRenderManager.new()
	edit_render_manager.name = "GaragePreviewEditRenderManager"
	edit_render_manager.stamp_only_mode = true
	edit_render_manager.stamp_render_priority = 3
	preview_root.add_child(edit_render_manager)

	above_render_manager = CarRenderManager.new()
	above_render_manager.name = "GaragePreviewAboveRenderManager"
	above_render_manager.stamp_only_mode = true
	above_render_manager.stamp_render_priority = 4
	preview_root.add_child(above_render_manager)

	preview_camera = Camera3D.new()
	preview_camera.current = true
	preview_camera.fov = 38.0
	preview_viewport.add_child(preview_camera)
	camera_controller.configure_frame(Vector3(0.0, PREVIEW_TARGET_HEIGHT, 0.0), 22.0, true)
	(owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CameraRealignControls/X") as Button).pressed.connect(_realign_camera.bind(Vector3.AXIS_X))
	(owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CameraRealignControls/Y") as Button).pressed.connect(_realign_camera.bind(Vector3.AXIS_Y))
	(owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CameraRealignControls/Z") as Button).pressed.connect(_realign_camera.bind(Vector3.AXIS_Z))
	_apply_camera()

func set_active(active: bool) -> void:
	if preview_viewport != null:
		preview_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS if active else SubViewport.UPDATE_DISABLED

func set_editing(active: bool) -> void:
	editing_mode = active

func rebuild_vehicle(definition: CarDefinition, base_settings: Dictionary, atlas_texture: Texture2D) -> void:
	if preview_root == null:
		return
	if preview_vehicle != null and is_instance_valid(preview_vehicle):
		preview_vehicle.queue_free()
	preview_vehicle = null
	render_manager.clear_renderer()
	edit_render_manager.clear_renderer()
	above_render_manager.clear_renderer()
	if definition == null or !definition.has_visual():
		return
	if definition.car_scene != null:
		preview_vehicle = definition.car_scene.instantiate()
	else:
		preview_vehicle = Node3D.new()
		var mesh_instance := MeshInstance3D.new()
		mesh_instance.mesh = definition.runtime_mesh
		mesh_instance.material_override = definition.runtime_material
		mesh_instance.transform = definition.runtime_transform
		preview_vehicle.add_child(mesh_instance)
	preview_vehicle_base_transform = preview_vehicle.transform
	preview_root.add_child(preview_vehicle)
	_hide_raycast_scene(preview_vehicle)
	render_manager.stamp_visibility_masks_enabled = true
	render_manager.stamp_visibility_mask_skip_layer = -1
	render_manager.stamp_only_mode = false
	render_manager.stamp_render_priority = 2
	render_manager.set_custom_stamp_atlas(atlas_texture)
	render_manager.configure_manual([definition], [base_settings])
	_apply_camera()

func rebuild_edit_layer(definition: CarDefinition, settings: Dictionary, atlas_texture: Texture2D) -> void:
	edit_render_manager.clear_renderer()
	if definition == null or !definition.has_visual() or !editing_mode:
		_apply_camera()
		return
	edit_render_manager.stamp_visibility_masks_enabled = false
	edit_render_manager.stamp_visibility_mask_skip_layer = -1
	edit_render_manager.stamp_only_mode = true
	edit_render_manager.stamp_render_priority = 3
	edit_render_manager.set_custom_stamp_atlas(atlas_texture)
	edit_render_manager.configure_manual([definition], [settings])
	_apply_camera()

func rebuild_above_layers(definition: CarDefinition, settings: Dictionary, atlas_texture: Texture2D) -> void:
	above_render_manager.clear_renderer()
	if definition == null or !definition.has_visual() or !editing_mode:
		_apply_camera()
		return
	above_render_manager.stamp_visibility_masks_enabled = true
	above_render_manager.stamp_visibility_mask_skip_layer = -1
	above_render_manager.stamp_only_mode = true
	above_render_manager.stamp_render_priority = 4
	above_render_manager.set_custom_stamp_atlas(atlas_texture)
	above_render_manager.configure_manual([definition], [settings])
	_apply_camera()

func set_custom_stamp_atlas(texture: Texture2D) -> void:
	for manager in [render_manager, edit_render_manager, above_render_manager]:
		if manager != null:
			manager.set_custom_stamp_atlas(texture)

func apply_livery_colours(livery: CarLivery) -> void:
	for manager in [render_manager, edit_render_manager, above_render_manager]:
		if manager != null and manager.has_method("update_livery_colours"):
			manager.update_livery_colours(livery)

func update_stamp_layer_colour(layer: int, colour: Color) -> void:
	for manager in [render_manager, edit_render_manager, above_render_manager]:
		if manager != null:
			manager.update_stamp_layer_colour(layer, colour)

func focus_stamp(stamp: CarLiveryStamp) -> void:
	if preview_camera == null or preview_vehicle == null or absf(stamp.local_basis.determinant()) <= 0.00001:
		return
	preview_vehicle.transform = preview_vehicle_base_transform
	var projector := preview_vehicle.global_transform * Transform3D(stamp.local_basis, stamp.local_origin)
	var view_direction := projector.basis.z.normalized()
	camera_controller.yaw = atan2(view_direction.x, view_direction.z)
	var stamp_elevation := asin(clampf(view_direction.y, -1.0, 1.0))
	camera_controller.pitch = clampf(-stamp_elevation, deg_to_rad(-90.0), deg_to_rad(55.0))
	var plane_basis := camera_controller.view_plane_basis(camera_controller.camera_offset())
	var relative_origin := projector.origin - Vector3(0.0, PREVIEW_TARGET_HEIGHT, 0.0)
	camera_controller.pan = Vector3(
		clampf(relative_origin.dot(plane_basis.x), -GaragePreviewCameraControllerClass.PAN_LIMIT, GaragePreviewCameraControllerClass.PAN_LIMIT),
		clampf(relative_origin.dot(plane_basis.y), -GaragePreviewCameraControllerClass.PAN_LIMIT, GaragePreviewCameraControllerClass.PAN_LIMIT),
		0.0
	)
	_apply_camera()

func handle_overlay_mouse_button(event: InputEventMouseButton) -> bool:
	if !camera_controller.handle_mouse_button(event):
		return false
	_apply_camera()
	return true

func handle_overlay_mouse_motion(event: InputEventMouseMotion) -> bool:
	if !camera_controller.handle_mouse_motion(event):
		return false
	_apply_camera()
	return true

func overlay_drag_button() -> int:
	return camera_controller.drag_button

func apply_stamp_projection(stamp: CarLiveryStamp, rect_size: Vector2, roll: float) -> bool:
	if stamp == null or preview_camera == null or preview_vehicle == null:
		return false
	var center := preview_space.size * 0.5
	var hit := _raycast_body(center)
	if hit.is_empty():
		return false
	var ray_dir := preview_camera.project_ray_normal(center).normalized()
	var car_inv := preview_vehicle.global_transform.affine_inverse()
	stamp.local_origin = car_inv * hit["position"]
	var z_axis := (car_inv.basis * -ray_dir).normalized()
	var x_axis := (car_inv.basis * preview_camera.global_transform.basis.x).normalized()
	x_axis = (x_axis - z_axis * x_axis.dot(z_axis)).normalized()
	var y_axis := z_axis.cross(x_axis).normalized()
	var projection_roll := -roll
	var roll_basis := Basis(z_axis, projection_roll)
	x_axis = roll_basis * x_axis
	y_axis = roll_basis * y_axis
	stamp.local_basis = Basis(x_axis, y_axis, z_axis)
	stamp.rotation = projection_roll
	stamp.size = _stamp_world_size_from_rect(hit["position"], rect_size)
	stamp.projection_depth = maxf(0.75, maxf(stamp.size.x, stamp.size.y) * 1.35)
	return true

func edit_rect_size(stamp: CarLiveryStamp) -> Vector2:
	if preview_camera == null or preview_vehicle == null:
		return Vector2(160.0, 160.0)
	var viewport_size := preview_space.size
	var world_pos := preview_vehicle.global_transform * stamp.local_origin
	var distance := preview_camera.global_position.distance_to(world_pos)
	if distance <= 0.01 or viewport_size.x <= 0.0 or viewport_size.y <= 0.0:
		return Vector2(160.0, 160.0)
	var world_height := 2.0 * distance * tan(deg_to_rad(preview_camera.fov) * 0.5)
	var world_width := world_height * viewport_size.x / viewport_size.y
	return Vector2(maxf(STAMP_EDIT_MIN_SCREEN_SIZE, stamp.size.x / world_width * viewport_size.x), maxf(STAMP_EDIT_MIN_SCREEN_SIZE, stamp.size.y / world_height * viewport_size.y))

func _on_preview_resized() -> void:
	if preview_viewport == null or (preview_container != null and preview_container.stretch):
		return
	preview_viewport.size = Vector2i(maxi(1, int(preview_space.size.x)), maxi(1, int(preview_space.size.y)))

func _on_preview_gui_input(event: InputEvent) -> void:
	if editing_mode:
		return
	var changed := false
	var mouse_event := event as InputEventMouseButton
	if mouse_event != null:
		changed = camera_controller.handle_mouse_button(mouse_event)
	else:
		var motion_event := event as InputEventMouseMotion
		if motion_event != null:
			changed = camera_controller.handle_mouse_motion(motion_event)
	if changed:
		_apply_camera()
		preview_container.accept_event()

func _realign_camera(axis: int) -> void:
	camera_controller.realign_pivot_axis(axis)
	_apply_camera()
	camera_changed.emit()

func _apply_camera() -> void:
	if preview_vehicle != null:
		preview_vehicle.transform = preview_vehicle_base_transform
	if preview_camera == null:
		return
	camera_controller.apply(preview_camera)
	_submit_render()

func _submit_render() -> void:
	for manager in [render_manager, edit_render_manager, above_render_manager]:
		if manager == null or manager.archetypes.is_empty():
			continue
		manager.begin_manual_submit()
		manager.submit_manual_car(0, Transform3D.IDENTITY, Color.BLACK, Vector3.ZERO, Color.BLACK, 0.0, false)

func _hide_raycast_scene(root: Node) -> void:
	for child in root.get_children():
		_hide_raycast_scene(child)
	var mesh := root as MeshInstance3D
	if mesh != null:
		mesh.visible = false

func _stamp_world_size_from_rect(hit_position: Vector3, rect_size: Vector2) -> Vector2:
	var viewport_size := preview_space.size
	if viewport_size.x <= 0.0 or viewport_size.y <= 0.0 or preview_camera == null:
		return Vector2.ONE
	var distance := preview_camera.global_position.distance_to(hit_position)
	var world_height := 2.0 * distance * tan(deg_to_rad(preview_camera.fov) * 0.5)
	var world_width := world_height * viewport_size.x / viewport_size.y
	return Vector2(world_width * rect_size.x / viewport_size.x, world_height * rect_size.y / viewport_size.y)

func _raycast_body(viewport_pos: Vector2) -> Dictionary:
	if preview_camera == null or preview_vehicle == null:
		return {}
	var body := preview_vehicle.get_node_or_null("VEHICLE_MAIN") as MeshInstance3D
	if body == null or body.mesh == null:
		return {}
	var ray_origin := preview_camera.project_ray_origin(viewport_pos)
	var ray_dir := preview_camera.project_ray_normal(viewport_pos).normalized()
	var best_t := INF
	var best_hit := {}
	for surface_index in range(body.mesh.get_surface_count()):
		var arrays := body.mesh.surface_get_arrays(surface_index)
		var vertices := _surface_vertices(arrays)
		if vertices.is_empty():
			continue
		var indices := _surface_indices(arrays)
		if indices.is_empty():
			for tri in range(int(vertices.size() / 3)):
				var hit := _raycast_triangle(ray_origin, ray_dir, body.global_transform * vertices[tri * 3], body.global_transform * vertices[tri * 3 + 1], body.global_transform * vertices[tri * 3 + 2])
				if !hit.is_empty() and hit["t"] < best_t:
					best_t = hit["t"]
					best_hit = hit
		else:
			for tri in range(int(indices.size() / 3)):
				var i0 := indices[tri * 3]
				var i1 := indices[tri * 3 + 1]
				var i2 := indices[tri * 3 + 2]
				if i0 < 0 or i1 < 0 or i2 < 0 or i0 >= vertices.size() or i1 >= vertices.size() or i2 >= vertices.size():
					continue
				var hit := _raycast_triangle(ray_origin, ray_dir, body.global_transform * vertices[i0], body.global_transform * vertices[i1], body.global_transform * vertices[i2])
				if !hit.is_empty() and hit["t"] < best_t:
					best_t = hit["t"]
					best_hit = hit
	return best_hit

func _raycast_triangle(origin: Vector3, direction: Vector3, a: Vector3, b: Vector3, c: Vector3) -> Dictionary:
	var edge_1 := b - a
	var edge_2 := c - a
	var h := direction.cross(edge_2)
	var det := edge_1.dot(h)
	if absf(det) < 0.00001:
		return {}
	var inv_det := 1.0 / det
	var s := origin - a
	var u := inv_det * s.dot(h)
	if u < 0.0 or u > 1.0:
		return {}
	var q := s.cross(edge_1)
	var v := inv_det * direction.dot(q)
	if v < 0.0 or u + v > 1.0:
		return {}
	var t := inv_det * edge_2.dot(q)
	if t <= 0.0001:
		return {}
	var normal := edge_1.cross(edge_2).normalized()
	if normal.dot(direction) > 0.0:
		normal = -normal
	return {"t": t, "position": origin + direction * t, "normal": normal}

func _surface_vertices(arrays: Array) -> PackedVector3Array:
	if arrays.size() <= Mesh.ARRAY_VERTEX or typeof(arrays[Mesh.ARRAY_VERTEX]) != TYPE_PACKED_VECTOR3_ARRAY:
		return PackedVector3Array()
	return arrays[Mesh.ARRAY_VERTEX]

func _surface_indices(arrays: Array) -> PackedInt32Array:
	if arrays.size() <= Mesh.ARRAY_INDEX or typeof(arrays[Mesh.ARRAY_INDEX]) != TYPE_PACKED_INT32_ARRAY:
		return PackedInt32Array()
	return arrays[Mesh.ARRAY_INDEX]
