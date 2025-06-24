extends Button

var stagePath : String
var stageScene : PackedScene

@onready var previewViewport := %previewViewport
@onready var stage_button_texture := %stageButtonTexture

var stageListRef : Node2D
var viewportCamera : Camera3D = Camera3D.new()
var stageName : String = "Stage"
var stageDesc : String = "Desc"
var difficulty : int = 100
var frameCount : int = 0
var axis : float = 0
var center : Vector3 = Vector3.ZERO
var loadedStage : RORStage

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	loadedStage = stageScene.instantiate()
	previewViewport.add_child(loadedStage)
	
	axis = loadedStage.stageVisualBounds.get_longest_axis_size()
	center = loadedStage.stageVisualBounds.get_center()
	
	viewportCamera.rotation_degrees = Vector3(-30, 45, 0)
	viewportCamera.position = (viewportCamera.basis.z * axis * 1.5) + center
	viewportCamera.fov = 45
	viewportCamera.far = viewportCamera.position.length() * 4
	viewportCamera.near = 4
	viewportCamera.set_cull_mask_value(2, false)
	previewViewport.add_child(viewportCamera)
	
	#size.y = size.x
	custom_minimum_size.y = size.x
	
	await RenderingServer.frame_post_draw
	stage_button_texture.texture = previewViewport.get_texture()
	loadedStage.queue_free()
	#viewportCamera.queue_free()
	
	

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process( delta:float ) -> void:
	size.y = size.x
	custom_minimum_size.y = size.x
	if previewViewport.render_target_update_mode == previewViewport.UPDATE_ALWAYS:
		previewViewport.size = size
		viewportCamera.rotation_degrees = Vector3(-30, viewportCamera.rotation_degrees.y + delta * 15, 0)
		viewportCamera.position = (viewportCamera.basis.z * axis * 1.5) + center
		

func _on_pressed() -> void:
	if multiplayer and multiplayer.is_server():
		var peers := multiplayer.get_peers()
		peers.append(multiplayer.get_unique_id())
		print(multiplayer)
		print(peers)
		MXGlobal.load_stage_by_path.rpc(stagePath, MXGlobal.current_race_settings.serialize())
	else:
		MXGlobal.load_stage_by_path(stagePath, MXGlobal.current_race_settings.serialize())

func _on_mouse_entered() -> void:
	loadedStage = stageScene.instantiate()
	previewViewport.add_child(loadedStage)
	previewViewport.render_target_update_mode = previewViewport.UPDATE_ALWAYS
	stageListRef.queue_stage_data(stageName, stageDesc, difficulty)

func _on_mouse_exited() -> void:
	loadedStage.queue_free()
	previewViewport.render_target_update_mode = previewViewport.UPDATE_DISABLED
