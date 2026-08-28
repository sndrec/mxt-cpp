class_name VehicleEditorPreviewController
extends Node

signal authoring_edit_committed
signal preview_livery_changed
signal thruster_position_changed(index: int, position: Vector3)

const PREVIEW_WORLD_SCENE: PackedScene = preload("res://ui/garage_preview_world.tscn")
const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarRenderManagerClass = preload("res://vehicle/car_render_manager.gd")
const VehicleRuntimeAssetFactoryClass = preload("res://vehicle/vehicle_runtime_asset_factory.gd")
const GaragePreviewCameraControllerClass = preload("res://ui/garage_preview_camera_controller.gd")

var session: MxtCarAuthoringSession
var title_input: LineEdit
var preview_container: SubViewportContainer
var preview_viewport: SubViewport
var preview_primary: ColorPickerButton
var preview_secondary: ColorPickerButton
var preview_accent: ColorPickerButton
var preview_preset: OptionButton
var preview_diagnostic: OptionButton
var physical_tabs: TabContainer
var thruster_selector: OptionButton

var draft_id := ""
var properties_path := ""
var draft_root := ""
var official_definition: CarDefinition
var preview_root: Node3D
var preview_camera: Camera3D
var camera_controller := GaragePreviewCameraControllerClass.new()
var framed_model_path := ""
var livery: CarLivery = CarLivery.new()
var render_manager: CarRenderManager
var definition: CarDefinition
var gizmo_root: Node3D
var gizmo_line: MeshInstance3D
var gizmo_markers: Array[MeshInstance3D] = []
var gizmo_entries: Array[Dictionary] = []
var gizmo_sphere: SphereMesh
var gizmo_materials: Array[StandardMaterial3D] = []
var dragged_gizmo := -1
var dragged_gizmo_plane := Plane()
var updating_paint_controls := false

func initialize(owner_ui: Control, authoring_session: MxtCarAuthoringSession) -> void:
	session = authoring_session
	title_input = owner_ui.get_node("Metadata/Title")
	preview_container = owner_ui.get_node("Workspace/VisualColumn/Preview")
	preview_viewport = owner_ui.get_node("Workspace/VisualColumn/Preview/Viewport")
	preview_primary = owner_ui.get_node("Workspace/VisualColumn/PaintPreview/Primary")
	preview_secondary = owner_ui.get_node("Workspace/VisualColumn/PaintPreview/Secondary")
	preview_accent = owner_ui.get_node("Workspace/VisualColumn/PaintPreview/Accent")
	preview_preset = owner_ui.get_node("Workspace/VisualColumn/PaintPreview/Preset")
	preview_diagnostic = owner_ui.get_node("Workspace/VisualColumn/PaintPreview/Diagnostic")
	physical_tabs = owner_ui.get_node("Workspace/VisualColumn/PhysicalTabs")
	thruster_selector = owner_ui.get_node("Workspace/VisualColumn/PhysicalTabs/Thrusters/Actions/Selector")
	for preset in ["Saved / Custom", "Default Blue", "Neutral White", "High Contrast", "Warm"]:
		preview_preset.add_item(preset)
	for diagnostic in ["Rendered", "Albedo", "Normal Map", "Paint Mask", "Selected Surfaces"]:
		preview_diagnostic.add_item(diagnostic)
	_setup_scene()
	preview_container.gui_input.connect(_on_preview_gui_input)
	physical_tabs.tab_changed.connect(func(_index): refresh_gizmos())
	preview_primary.color_changed.connect(func(_colour): _on_preview_colours_changed())
	preview_secondary.color_changed.connect(func(_colour): _on_preview_colours_changed())
	preview_accent.color_changed.connect(func(_colour): _on_preview_colours_changed())
	preview_preset.item_selected.connect(_apply_preview_preset)
	preview_diagnostic.item_selected.connect(_apply_preview_diagnostic)

func set_session(authoring_session: MxtCarAuthoringSession) -> void:
	session = authoring_session

func set_document_context(in_draft_id: String, in_properties_path: String, in_draft_root: String, in_official_definition: CarDefinition) -> void:
	draft_id = in_draft_id
	properties_path = in_properties_path
	draft_root = in_draft_root
	official_definition = in_official_definition

func set_editing_official(editing_official: bool) -> void:
	preview_primary.disabled = editing_official
	preview_secondary.disabled = editing_official
	preview_accent.disabled = editing_official
	preview_preset.disabled = editing_official

func reset_livery() -> void:
	livery = CarLivery.new()
	refresh_paint_controls()

func load_livery(data: Dictionary) -> void:
	livery = CarLivery.new()
	livery.from_dict(data)
	refresh_paint_controls()

func livery_dict() -> Dictionary:
	return livery.to_dict()

func refresh_paint_controls() -> void:
	updating_paint_controls = true
	preview_primary.color = livery.primary_colour
	preview_secondary.color = livery.secondary_colour
	preview_accent.color = livery.accent_colour
	updating_paint_controls = false

func refresh() -> void:
	render_manager.clear_renderer()
	definition = null
	if official_definition != null:
		definition = official_definition
		_configure_renderer()
		var preview_key := official_definition.content_id
		_frame_camera(preview_key != framed_model_path)
		framed_model_path = preview_key
		refresh_gizmos()
		return
	var model_path := session.get_model_path()
	if model_path.is_empty() or !FileAccess.file_exists(model_path):
		framed_model_path = ""
		refresh_gizmos()
		return
	var record := {
		"content_id": "mxt:vehicle:draft:%s" % draft_id,
		"title": title_input.text,
		"authoritative_path": properties_path,
		"visual_path": model_path,
		"albedo_texture_path": _material_texture_path("albedo"),
		"normal_texture_path": _material_texture_path("normal"),
		"paint_mask_texture_path": _material_texture_path("paint_mask"),
		"visual_metadata": {
			"model_transform": session.get_model_transform(),
			"body_surfaces": session.get_material_setup().get("body_surfaces", []),
			"material_inputs": session.get_material_setup(),
			"thrusters": session.get_thrusters(),
		},
	}
	definition = VehicleRuntimeAssetFactoryClass.create_from_draft_record(record)
	if definition != null:
		_configure_renderer()
		_frame_camera(model_path != framed_model_path)
		framed_model_path = model_path
	refresh_gizmos()

func refresh_gizmos() -> void:
	if gizmo_root == null:
		return
	gizmo_entries.clear()
	var tab_name := physical_tabs.get_tab_title(physical_tabs.current_tab)
	if tab_name == "Corners":
		var tilt := session.get_tilt_corners()
		var wall := session.get_wall_corners()
		for i in range(4):
			gizmo_entries.append({"kind": "tilt", "index": i, "position": tilt[i], "material": 0})
		for i in range(4):
			gizmo_entries.append({"kind": "wall", "index": i, "position": wall[i], "material": 1})
	elif tab_name == "Thrusters":
		var thrusters: Array = session.get_thrusters()
		for i in range(thrusters.size()):
			gizmo_entries.append({
				"kind": "thruster",
				"index": i,
				"position": Vector3(thrusters[i].get("position", Vector3.ZERO)),
				"material": 2,
			})
	while gizmo_markers.size() < gizmo_entries.size():
		var marker := MeshInstance3D.new()
		marker.mesh = gizmo_sphere
		gizmo_root.add_child(marker)
		gizmo_markers.append(marker)
	for i in range(gizmo_markers.size()):
		var marker := gizmo_markers[i]
		marker.visible = i < gizmo_entries.size()
		if marker.visible:
			var entry: Dictionary = gizmo_entries[i]
			marker.position = entry["position"]
			marker.material_override = gizmo_materials[int(entry["material"])]
	_refresh_gizmo_lines(tab_name)

func _setup_scene() -> void:
	var world := PREVIEW_WORLD_SCENE.instantiate()
	preview_viewport.add_child(world)
	preview_root = Node3D.new()
	preview_root.name = "VehicleEditorPreviewRoot"
	world.add_child(preview_root)
	render_manager = CarRenderManagerClass.new()
	preview_root.add_child(render_manager)
	gizmo_root = Node3D.new()
	gizmo_root.name = "AuthoringGizmos"
	preview_root.add_child(gizmo_root)
	gizmo_line = MeshInstance3D.new()
	gizmo_root.add_child(gizmo_line)
	gizmo_sphere = SphereMesh.new()
	gizmo_sphere.radius = 0.11
	gizmo_sphere.height = 0.22
	gizmo_sphere.radial_segments = 12
	gizmo_sphere.rings = 6
	for colour in [Color(0.1, 0.9, 1.0), Color(1.0, 0.2, 0.75), Color(1.0, 0.75, 0.08)]:
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = colour
		material.no_depth_test = true
		gizmo_materials.append(material)
	preview_camera = Camera3D.new()
	preview_camera.near = 0.05
	preview_camera.fov = 38.0
	preview_camera.position = Vector3(7.0, 3.7, 9.0)
	preview_root.add_child(preview_camera)
	preview_camera.look_at(Vector3.ZERO, Vector3.UP)
	preview_camera.current = true
	camera_controller.configure_frame(Vector3(0.0, 0.5, 0.0), 12.0, true)

func _configure_renderer() -> void:
	render_manager.configure_manual([definition], [{"car_livery": livery.to_dict()}])
	render_manager.begin_manual_submit()
	render_manager.submit_manual_car(0, Transform3D.IDENTITY, Color.BLACK, Vector3.ZERO, Color.BLACK, 0.0, false)
	render_manager.set_material_diagnostic(preview_diagnostic.selected)

func _material_texture_path(layer: String) -> String:
	if draft_root.is_empty():
		return ""
	var path := draft_root.path_join("model").path_join("%s.png" % layer)
	return path if FileAccess.file_exists(path) else ""

func _on_preview_colours_changed() -> void:
	if updating_paint_controls:
		return
	livery.primary_colour = preview_primary.color
	livery.secondary_colour = preview_secondary.color
	livery.accent_colour = preview_accent.color
	preview_preset.select(0)
	render_manager.update_livery_colours(livery)
	preview_livery_changed.emit()

func _apply_preview_preset(index: int) -> void:
	match index:
		0:
			return
		1:
			livery.primary_colour = Color(0.1, 0.35, 1.0, 1.0)
			livery.secondary_colour = Color.WHITE
			livery.accent_colour = Color(0.05, 0.05, 0.06, 1.0)
		2:
			livery.primary_colour = Color.WHITE
			livery.secondary_colour = Color(0.7, 0.7, 0.7, 1.0)
			livery.accent_colour = Color(0.15, 0.15, 0.15, 1.0)
		3:
			livery.primary_colour = Color(1.0, 0.08, 0.05, 1.0)
			livery.secondary_colour = Color(0.05, 0.9, 0.15, 1.0)
			livery.accent_colour = Color(0.05, 0.2, 1.0, 1.0)
		4:
			livery.primary_colour = Color(1.0, 0.35, 0.04, 1.0)
			livery.secondary_colour = Color(1.0, 0.85, 0.3, 1.0)
			livery.accent_colour = Color(0.25, 0.03, 0.02, 1.0)
	refresh_paint_controls()
	render_manager.update_livery_colours(livery)
	preview_livery_changed.emit()

func _apply_preview_diagnostic(index: int) -> void:
	render_manager.set_material_diagnostic(index)

func _frame_camera(reset_view := false) -> void:
	if preview_camera == null or definition == null:
		return
	var bounds := AABB()
	var model_transform := Transform3D.IDENTITY
	if definition.runtime_mesh != null:
		bounds = definition.runtime_mesh.get_aabb()
		model_transform = definition.runtime_transform
	elif definition.car_scene != null:
		var source := definition.car_scene.instantiate() as Node3D
		var body: MeshInstance3D
		if source != null:
			body = source.get_node_or_null("VEHICLE_MAIN") as MeshInstance3D
		if body != null and body.mesh != null:
			bounds = body.mesh.get_aabb()
			model_transform = source.transform * body.transform
		if source != null:
			source.free()
	if bounds.size.is_zero_approx():
		return
	var center := model_transform * bounds.get_center()
	var radius := 0.0
	for x in range(2):
		for y in range(2):
			for z in range(2):
				var corner := bounds.position + Vector3(bounds.size.x * x, bounds.size.y * y, bounds.size.z * z)
				radius = maxf(radius, center.distance_to(model_transform * corner))
	var viewport_aspect := float(preview_viewport.size.x) / maxf(1.0, float(preview_viewport.size.y))
	var half_vertical_fov := deg_to_rad(preview_camera.fov * 0.5)
	var half_horizontal_fov := atan(tan(half_vertical_fov) * viewport_aspect)
	var limiting_fov := minf(half_vertical_fov, half_horizontal_fov)
	var distance := maxf(4.0, radius * 1.15 / maxf(0.1, sin(limiting_fov)))
	camera_controller.configure_frame(center, distance, reset_view)
	preview_camera.far = maxf(1000.0, distance + radius * 4.0)
	camera_controller.apply(preview_camera)

func _refresh_gizmo_lines(tab_name: String) -> void:
	var lines := ImmediateMesh.new()
	if tab_name == "Corners":
		var tilt := session.get_tilt_corners()
		var wall := session.get_wall_corners()
		lines.surface_begin(Mesh.PRIMITIVE_LINES, gizmo_materials[0])
		for i in range(4):
			lines.surface_add_vertex(tilt[i])
			lines.surface_add_vertex(tilt[(i + 1) % 4])
		lines.surface_end()
		lines.surface_begin(Mesh.PRIMITIVE_LINES, gizmo_materials[1])
		for i in range(4):
			lines.surface_add_vertex(wall[i])
			lines.surface_add_vertex(wall[(i + 1) % 4])
		lines.surface_end()
	elif tab_name == "Thrusters":
		lines.surface_begin(Mesh.PRIMITIVE_LINES, gizmo_materials[2])
		for value in session.get_thrusters():
			var thruster: Dictionary = value
			var position := Vector3(thruster.get("position", Vector3.ZERO))
			var rotation := Vector3(thruster.get("rotation_degrees", Vector3.ZERO))
			var direction := Basis.from_euler(rotation * PI / 180.0) * Vector3(0.0, 0.0, -1.0)
			lines.surface_add_vertex(position)
			lines.surface_add_vertex(position + direction * maxf(0.25, float(thruster.get("scale", 1.0))))
		lines.surface_end()
	gizmo_line.mesh = lines

func _on_preview_gui_input(event: InputEvent) -> void:
	if preview_camera == null:
		return
	var button := event as InputEventMouseButton
	var motion := event as InputEventMouseMotion
	if button == null and motion == null:
		return
	var pointer := (button.position if button != null else motion.position) * Vector2(preview_viewport.size) / preview_container.size
	if button != null:
		if button.button_index == MOUSE_BUTTON_LEFT and button.pressed and !button.double_click:
			dragged_gizmo = _pick_gizmo(pointer)
			if dragged_gizmo >= 0:
				session.begin_edit_transaction()
				var point := Vector3(gizmo_entries[dragged_gizmo]["position"])
				dragged_gizmo_plane = Plane(preview_camera.global_basis.z.normalized(), point)
				preview_container.accept_event()
				return
		if button.button_index == MOUSE_BUTTON_LEFT and !button.pressed and dragged_gizmo >= 0:
			session.end_edit_transaction()
			authoring_edit_committed.emit()
			refresh()
			preview_container.accept_event()
			dragged_gizmo = -1
			return
		if camera_controller.handle_mouse_button(button):
			camera_controller.apply(preview_camera)
			preview_container.accept_event()
		return
	if dragged_gizmo >= 0:
		var origin := preview_camera.project_ray_origin(pointer)
		var direction := preview_camera.project_ray_normal(pointer)
		var intersection = dragged_gizmo_plane.intersects_ray(origin, direction)
		if intersection != null:
			_set_dragged_gizmo_position(intersection)
			preview_container.accept_event()
		return
	if camera_controller.handle_mouse_motion(motion):
		camera_controller.apply(preview_camera)
		preview_container.accept_event()

func _pick_gizmo(pointer: Vector2) -> int:
	var best := -1
	var best_distance := 18.0
	for i in range(gizmo_entries.size()):
		if String(gizmo_entries[i]["kind"]) != "thruster":
			continue
		var position := Vector3(gizmo_entries[i]["position"])
		if preview_camera.is_position_behind(position):
			continue
		var distance := pointer.distance_to(preview_camera.unproject_position(position))
		if distance < best_distance:
			best_distance = distance
			best = i
	return best

func _set_dragged_gizmo_position(position: Vector3) -> void:
	var entry: Dictionary = gizmo_entries[dragged_gizmo]
	var index := int(entry["index"])
	if String(entry["kind"]) == "thruster":
		var thrusters: Array = session.get_thrusters()
		var thruster: Dictionary = thrusters[index]
		thruster["position"] = position
		thrusters[index] = thruster
		session.set_thrusters(thrusters)
		if index == thruster_selector.selected:
			thruster_position_changed.emit(index, position)
	gizmo_entries[dragged_gizmo]["position"] = position
	refresh_gizmos()
