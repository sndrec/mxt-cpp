@tool
extends EditorScenePostImport

# Regex for identifying stage objects by their B2RO tag
var stage_object_tag_regex = RegEx.new()
const stage_object_tag_regex_pattern = "\\[(\\w*)\\]"

# Goal types
enum GoalType {GOAL_B, GOAL_G, GOAL_R}



const stage_object_resource: Resource = preload("res://core/stage/StageObject.tscn")
var stage_object_goal_resource: Resource = load("res://core/stage/Goal.tscn")
const stage_object_start_resource: Resource = preload("res://core/stage/SpawnPoint.tscn")

# Called by the editor when a scene has this script set as the import script in the import tab.
func _post_import(scene: Node) -> Object:
	# Root node needs to be RORStage - can be configured on import
	assert(scene is RORStage)
	
	stage_object_tag_regex.compile(stage_object_tag_regex_pattern)
	
	# Check for Rolled Out config
	
	# Paths to the currently imported file
	var imported_file = get_source_file()
	var imported_path = imported_file.get_base_dir()
	var ro_config = null;
	var ro_config_file = imported_path + "/config.json";
	
	print("Checking for config at: " + ro_config_file)
	if (FileAccess.file_exists(ro_config_file)):
		print("Found RO config, parsing...")
		var file = FileAccess.open(ro_config_file, FileAccess.READ)
		ro_config = JSON.parse_string(file.get_as_text())
		
		var ro_metadata = ro_config["metadata"]
		# TODO: Locale support
		scene.stageName = ro_metadata["name"]["fallback"]
		scene.stageDesc = ro_metadata["description"]["fallback"]
		scene.difficulty = int(ro_metadata["difficulty"])
		scene.timerLength = int(ro_config["timer_seconds"])
		
		print("Parsed RO config for stage " + scene.stageName)
		
	else:
		print("Warning: No RO config found!")
	
	# Parse nodes
	print("Parsing nodes...")
	stage_parse_node(scene, ro_config, scene)
	stage_parse_animation(scene)
	print("Done importing!")
	
	return scene # Return the modified root node when you're done.
		
func stage_parse_node(node: Node, config, parent):
	if node != null:
		print("\tParsing node " + node.name + "...")
		
		# Don't parse the root node itself
		if !(node is RORStage):
			var tag_result = stage_object_tag_regex.search(node.name)
			
			# Tagged stage object (goal, collectable, etc)
			if tag_result:
				# We assume tagged object is an empty
				print("\t\tFound tag: " + tag_result.get_string(1))
				match tag_result.get_string(1):
					"GOAL_B":
						# TODO: implement
						create_goal_object(node, config, parent)
					"START":
						# TODO: Multiple spawn support
						create_start_object(node, config["spawns"][0], parent)
					"IG":
						if (node is MeshInstance3D):
							var obj = create_stage_object_from_inst(node, stage_object_resource, parent)
							set_material_on_mesh(obj.meshRef)
							obj.convertMeshToCollision = true
					_:
						print("\t\tUnsupported type " + tag_result.get_string(1) + "!")
						
			# Not tagged - generic stage object
			else:
				print("\t\tFound untagged")
				if (node is MeshInstance3D):
					var obj = create_stage_object_from_inst(node, stage_object_resource, parent)
					set_material_on_mesh(obj.meshRef)
					obj.convertMeshToCollision = true
			
		# Recursively parse child nodes
		print("\t\tRecursing...")
		for child in node.get_children():
			stage_parse_node(child, config, node)
			if child is MeshInstance3D or child is Node3D:
				node.remove_child(child)
		
		# Queue original node to be freed
		
	
	return
	
# Parse animations for stage objects	
func stage_parse_animation(stage : RORStage):
	print("\t\tParsing animation...")
	var animPlayer : AnimationPlayer = stage.get_node("AnimationPlayer")
	var animations : PackedStringArray = animPlayer.get_animation_list()
	var anim : Animation = animPlayer.get_animation(animations[0])
	for track in anim.get_track_count():
		var path = anim.track_get_path(track)
		if stage.has_node(path):
			var stageObj : RORStageObject = stage.get_node(path)
			var stageAnim : AnimationPlayer = stageObj.get_node("ObjAnim")
			var newLibrary : AnimationLibrary
			if stageAnim.has_animation_library(""):
				newLibrary = stageAnim.get_animation_library("")
			else:
				newLibrary = AnimationLibrary.new()
			var newAnim : Animation
			if newLibrary.has_animation("motion"):
				newAnim = newLibrary.get_animation("motion")
			else:
				newAnim = Animation.new()
			newAnim.loop_mode = Animation.LOOP_LINEAR
			newAnim.length = anim.length
			anim.copy_track(track, newAnim)
			newAnim.track_set_path(newAnim.get_track_count() - 1, NodePath("."))
			if !newLibrary.has_animation("motion"):
				newLibrary.add_animation("motion", newAnim)
			if !stageAnim.has_animation_library(""):
				stageAnim.add_animation_library("", newLibrary)
	return
				
# Create RORStageObject from MeshInstance3D
func create_stage_object_from_inst(instance: MeshInstance3D, resource: Resource, parent: Node3D) -> RORStageObject:
	print("\t\tCreating stage object...")
	var new_object : RORStageObject = stage_object_resource.instantiate()
	
	parent.add_child(new_object)
	new_object.set_owner(parent)
	parent.set_editable_instance(new_object, true)
	new_object.transform = parent.transform * instance.transform
	new_object.meshRef = instance.mesh
	instance.name += "_old"
	new_object.name = instance.name.left(instance.name.length()-4)
	new_object.add_to_group("StageObjects")
		
	return new_object
	
# Create instance of a goal, which is a Node3D
func create_goal_object(instance: Node3D, config: Dictionary, parent: Node3D):
	print("\t\tCreating goal...")
	var new_object: Node3D = stage_object_goal_resource.instantiate()
	
	parent.add_child(new_object)
	new_object.set_owner(parent)
	parent.set_editable_instance(new_object, true)

	# TODO: implement
	if false:
		new_object.position = get_json_vec3_position(config["position"])
		new_object.quaternion = get_json_quat(config["rotation"])
		
	else:
		new_object.transform = parent.transform * instance.transform
		new_object.rotation_degrees += Vector3(0, 180, 0)
		
	return
	
# Create instance of a starting position, which is a SpawnPoint
func create_start_object(instance: Node3D, config: Dictionary, parent: Node3D):
	print("\t\tCreating spawnpoint...")
	var new_object: SpawnPoint = stage_object_start_resource.instantiate()
	
	parent.add_child(new_object)
	new_object.set_owner(parent)
	parent.set_editable_instance(new_object, true)
	
	if config != null:
		var transform = config["transform_noscale"]
		new_object.position = get_json_vec3_position(transform["position"])
		new_object.quaternion = get_json_quat(transform["rotation"])
		
	else:
		new_object.transform = parent.transform * instance.transform
		new_object.rotation_degrees += Vector3(0, 180, 0)
		
	instance.name += "_old"
	new_object.name = instance.name.left(instance.name.length()-4)
	
	return
	
func set_material_on_mesh(inMesh: Mesh) -> void:
	print("\t\tSetting material...")
	for surface in inMesh.get_surface_count():
		var mat = inMesh.surface_get_material(surface)
		if mat is BaseMaterial3D:
			var newMat = ShaderMaterial.new()
			newMat.shader = preload("res://content/base/shader/BasicStageMat.gdshader")
			var tex = mat.albedo_texture
			var mtex = mat.metallic_texture
			var rtex = mat.roughness_texture
			newMat.set_shader_parameter("texture_albedo", tex)
			newMat.set_shader_parameter("albedo", mat.albedo_color)
			newMat.set_shader_parameter("texture_metallic", mtex)
			newMat.set_shader_parameter("metallic", mat.metallic)
			newMat.set_shader_parameter("texture_roughness", rtex)
			newMat.set_shader_parameter("roughness", mat.roughness)
			newMat.set_shader_parameter("specular", 0)
			newMat.set_shader_parameter("uv1_scale", Vector3(1, 1, 1))
			newMat.set_shader_parameter("uv2_scale", Vector3(1, 1, 1))
			inMesh.surface_set_material(surface, newMat)
		if mat is ShaderMaterial:
			mat.set_shader_parameter("uv1_scale", Vector3(1, 1, 1))
			mat.set_shader_parameter("uv2_scale", Vector3(1, 1, 1))
			inMesh.surface_set_material(surface, mat)
	
func get_json_vec3_position(json: Dictionary) -> Vector3:
	return Vector3(-float(json["x"])/100, float(json["z"])/100, -float(json["y"])/100)
	
func get_json_quat(json: Dictionary) -> Quaternion:
	return Quaternion(float(json["x"]), float(json["y"]), float(json["z"]), float(json["w"]))
