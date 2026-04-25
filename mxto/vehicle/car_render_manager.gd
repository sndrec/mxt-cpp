class_name CarRenderManager extends Node3D

const PASS_MAIN := "main"
const PASS_OUTLINE := "outline"
const PASS_OUTLINE_MAIN := "outline_main"

var cars: Array = []
var archetypes: Array = []
var car_archetype_indices: PackedInt32Array = PackedInt32Array()
var car_slots: PackedInt32Array = PackedInt32Array()

func _ready() -> void:
	process_priority = 2
	set_process(false)

func _exit_tree() -> void:
	cars.clear()
	archetypes.clear()
	car_archetype_indices = PackedInt32Array()
	car_slots = PackedInt32Array()

func clear_renderer() -> void:
	cars.clear()
	archetypes.clear()
	car_archetype_indices = PackedInt32Array()
	car_slots = PackedInt32Array()
	for child in get_children():
		child.queue_free()

func configure(definitions: Array, car_nodes: Array) -> void:
	clear_renderer()
	set_process(false)
	cars = car_nodes.duplicate()
	car_archetype_indices.resize(cars.size())
	car_slots.resize(cars.size())
	var archetype_map := {}
	for i in range(definitions.size()):
		var def: CarDefinition = definitions[i]
		var key := _definition_key(def)
		var archetype_index := -1
		if archetype_map.has(key):
			archetype_index = archetype_map[key]
		else:
			archetype_index = archetypes.size()
			archetype_map[key] = archetype_index
			archetypes.append(_build_archetype(def))
		var archetype: Dictionary = archetypes[archetype_index]
		var count: int = archetype["count"]
		car_archetype_indices[i] = archetype_index
		car_slots[i] = count
		archetype["indices"].append(i)
		archetype["count"] = count + 1
		_resize_passes(archetype, archetype["count"])
		archetypes[archetype_index] = archetype

func get_native_render_bindings() -> Dictionary:
	var multimeshes: Array = []
	var local_transforms: Array = []
	for archetype in archetypes:
		var pass_data: Dictionary = archetype[PASS_MAIN]
		multimeshes.append(pass_data["multimesh"])
		local_transforms.append(pass_data["local_transform"])
	return {
		"multimeshes": multimeshes,
		"local_transforms": local_transforms,
		"archetype_indices": car_archetype_indices,
		"slots": car_slots,
	}

func _process(_delta: float) -> void:
	if archetypes.is_empty():
		return
	for archetype_index in range(archetypes.size()):
		var archetype: Dictionary = archetypes[archetype_index]
		var indices: Array = archetype["indices"]
		for slot in range(indices.size()):
			var car_index: int = indices[slot]
			if car_index < 0 or car_index >= cars.size():
				continue
			var car_ref = cars[car_index]
			if car_ref == null or !is_instance_valid(car_ref):
				continue
			var car: VisualCar = car_ref as VisualCar
			if car == null:
				continue
			var body_transform: Transform3D = car.car_transform.global_transform
			var outline_velocity := _get_outline_velocity(car)
			var zero_custom := Vector3.ZERO
			var body_overlay := Color(car.car_overlay_colour.r, car.car_overlay_colour.g, car.car_overlay_colour.b, 1.0)
			var outline_overlay := Color(0.5, 0.7, 1.0, 1.0) * float(car.boost_frames) * 0.005
			_set_pass_instance(archetype[PASS_MAIN], slot, body_transform * archetype[PASS_MAIN]["local_transform"], zero_custom, body_overlay)
			#_set_pass_instance(archetype[PASS_OUTLINE], slot, body_transform * archetype[PASS_OUTLINE]["local_transform"], outline_velocity, outline_overlay)
			#_set_pass_instance(archetype[PASS_OUTLINE_MAIN], slot, body_transform * archetype[PASS_OUTLINE_MAIN]["local_transform"], outline_velocity, Color.BLACK)

func _definition_key(definition: CarDefinition) -> String:
	if definition == null:
		return ""
	if definition.resource_path != "":
		return definition.resource_path
	return definition.name

func _build_archetype(definition: CarDefinition) -> Dictionary:
	var template: Node3D = definition.car_scene.instantiate()
	var root_transform := template.transform
	var main_mesh: MeshInstance3D = template.get_node("VEHICLE_MAIN")
	var outline_mesh: MeshInstance3D = template.get_node("VEHICLE_OUTLINE")
	var outline_main_mesh: MeshInstance3D = template.get_node("VEHICLE_OUTLINE_MAIN")
	var archetype := {
		"indices": [],
		"count": 0,
		PASS_MAIN: _create_pass("Main_%s" % _safe_name(definition.name), main_mesh.mesh, main_mesh.material_override, root_transform * main_mesh.transform, 1, 0),
	}
	template.free()
	return archetype

func _create_pass(pass_name: String, mesh: Mesh, material: Material, local_transform: Transform3D, layers: int, render_priority: int) -> Dictionary:
	var node := MultiMeshInstance3D.new()
	node.name = pass_name
	node.layers = layers
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.use_custom_data = true
	multimesh.mesh = mesh
	multimesh.instance_count = 0
	node.multimesh = multimesh
	if material != null:
		node.material_override = material.duplicate()
		node.material_override.render_priority = render_priority
	add_child(node)
	return {
		"local_transform": local_transform,
		"node": node,
		"multimesh": multimesh,
	}

func _resize_passes(archetype: Dictionary, count: int) -> void:
	for pass_name in [PASS_MAIN]:
		var pass_data: Dictionary = archetype[pass_name]
		var multimesh: MultiMesh = pass_data["multimesh"]
		if multimesh.instance_count != count:
			multimesh.instance_count = count
		multimesh.visible_instance_count = count

func _set_pass_instance(pass_data: Dictionary, slot: int, transform: Transform3D, custom_vec: Vector3, color: Color) -> void:
	var multimesh: MultiMesh = pass_data["multimesh"]
	multimesh.set_instance_transform(slot, transform)
	multimesh.set_instance_custom_data(slot, Color(custom_vec.x, custom_vec.y, custom_vec.z, 1.0))
	multimesh.set_instance_color(slot, color)

func _get_outline_velocity(car: VisualCar) -> Vector3:
	var use_vel := car.position_old - car.position_current
	var use_vel_mag := use_vel.length()
	if use_vel_mag <= 0.0001:
		return car.basis_physical.basis.z * 0.01
	var final_vel := use_vel.normalized() * move_toward(use_vel_mag, 0.0, 4.0) * 0.5
	return final_vel + car.basis_physical.basis.z * 0.01

func _safe_name(name_in: String) -> String:
	return name_in.replace(" ", "_").replace("/", "_")
