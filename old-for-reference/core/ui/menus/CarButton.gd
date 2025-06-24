extends Button

var car_definition : CarDefinition

@onready var previewViewport := %previewViewport
@onready var stage_button_texture := %carButtonTexture

var viewportCamera : Camera3D = Camera3D.new()
var stageName : String = "Stage"
var frameCount : int = 0
var axis : float = 0
var center : Vector3 = Vector3.ZERO
var loadedCar : Node3D

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	loadedCar = car_definition.model.instantiate()
	var mesh_instance := loadedCar.get_child(0) as MeshInstance3D
	mesh_instance.set_instance_shader_parameter("base_color", MXGlobal.local_settings.base_color)
	mesh_instance.set_instance_shader_parameter("secondary_color", MXGlobal.local_settings.secondary_color)
	mesh_instance.set_instance_shader_parameter("tertiary_color", MXGlobal.local_settings.tertiary_color)
	mesh_instance.set_instance_shader_parameter("depth_offset", 0)
	previewViewport.add_child(loadedCar)
	var new_light := DirectionalLight3D.new()
	previewViewport.add_child(new_light)
	new_light.global_rotation_degrees = Vector3(-45, 45, 0)
	
	axis = loadedCar.get_child(0).get_aabb().get_longest_axis_size() * loadedCar.get_child(0).scale.x
	center = loadedCar.get_child(0).position
	
	viewportCamera.rotation_degrees = Vector3(-30, 45, 0)
	viewportCamera.position = (viewportCamera.basis.z * axis * 1.5) + center
	viewportCamera.fov = 60
	viewportCamera.far = 32
	viewportCamera.near = 0.1
	#viewportCamera.set_cull_mask_value(2, false)
	previewViewport.add_child(viewportCamera)
	#size.x = size.y
	custom_minimum_size.x = size.y
	
	await RenderingServer.frame_post_draw
	stage_button_texture.texture = previewViewport.get_texture()
	loadedCar.queue_free()
	#viewportCamera.queue_free()
	size.x = size.y
	custom_minimum_size.x = size.y

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process( delta:float ) -> void:
	size.x = size.y
	custom_minimum_size.x = size.y
	if previewViewport.render_target_update_mode == previewViewport.UPDATE_ALWAYS:
		previewViewport.size = size
		viewportCamera.rotation_degrees = Vector3(-30, viewportCamera.rotation_degrees.y + delta * 15, 0)
		viewportCamera.position = (viewportCamera.basis.z * axis * 1.5) + center
		

func _on_pressed() -> void:
	MXGlobal.local_settings.car_choice = car_definition.ref_name
	MXGlobal.local_settings._save_settings()

func _on_mouse_entered() -> void:
	loadedCar = car_definition.model.instantiate()
	var mesh_instance := loadedCar.get_child(0) as MeshInstance3D
	mesh_instance.set_instance_shader_parameter("base_color", MXGlobal.local_settings.base_color)
	mesh_instance.set_instance_shader_parameter("secondary_color", MXGlobal.local_settings.secondary_color)
	mesh_instance.set_instance_shader_parameter("tertiary_color", MXGlobal.local_settings.tertiary_color)
	mesh_instance.set_instance_shader_parameter("depth_offset", 0)
	previewViewport.add_child(loadedCar)
	previewViewport.render_target_update_mode = previewViewport.UPDATE_ALWAYS

func _on_mouse_exited() -> void:
	loadedCar.queue_free()
	previewViewport.render_target_update_mode = previewViewport.UPDATE_DISABLED
