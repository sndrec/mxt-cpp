extends Node2D

@onready var stage_list_root_control:Control = $StageListRootControl
@onready var stage_name_label:Label = %StageNameLabel
@onready var stage_desc_label:Label = %StageDescLabel
var startSize : Vector2 = Vector2.ZERO

var dispDifficulty : int = 0
var queuedData := {
	"stageName" = "",
	"stageDesc" = "",
	"difficulty" = 0
}

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	get_tree().paused = true
	visible = false
	for stagePath:String in MXGlobal.stageList:
		var sceneState : SceneState = MXGlobal.stageList[stagePath].get_state()
		var newButtonScene := preload("res://core/ui/menus/stagebutton.tscn")
		var newButton := newButtonScene.instantiate()
		for i in sceneState.get_node_property_count(0):
			#stageName
			#stageDesc
			#difficulty
			if sceneState.get_node_property_name(0, i) == "stageName":
				newButton.stageName = sceneState.get_node_property_value(0, i)
			if sceneState.get_node_property_name(0, i) == "stageDesc":
				newButton.stageDesc = sceneState.get_node_property_value(0, i)
			if sceneState.get_node_property_name(0, i) == "difficulty":
				newButton.difficulty = sceneState.get_node_property_value(0, i)
		newButton.stageScene = MXGlobal.stageList[stagePath]
		newButton.stagePath = stagePath
		newButton.stageListRef = self
		%GridContainer.add_child(newButton)
	clear_stage_data_queue()
	%StageDetailsPanel.custom_minimum_size.y = 0
	stage_name_label.text = ""
	stage_desc_label.text = ""
	startSize = DisplayServer.window_get_size()
	%AnimationPlayer.play("created")
	await RenderingServer.frame_post_draw
	visible = true
	get_tree().paused = false

func _process( delta:float ) -> void:
	
	if queuedData["stageName"] != "":
		%StageDetailsPanel.custom_minimum_size.y = lerp(%StageDetailsPanel.custom_minimum_size.y, 0.0, minf(delta * 24, 1.0))
		if %StageDetailsPanel.custom_minimum_size.y <= 1:
			stage_name_label.text = queuedData["stageName"]
			stage_desc_label.text = queuedData["stageDesc"]
			dispDifficulty = queuedData["difficulty"]
			clear_stage_data_queue()
	else:
		%StageDetailsPanel.custom_minimum_size.y = lerp(%StageDetailsPanel.custom_minimum_size.y, %StageListContainer.size.y, minf(delta * 24, 1.0))
	
	if stage_name_label.text == "":
		%StageDetailsPanel.custom_minimum_size.y = 0
		

func clear_stage_data_queue() -> void:
	queuedData = {
		"stageName" = "",
		"stageDesc" = "",
		"difficulty" = 0
	}

func queue_stage_data(inName : String, inDesc : String, inDifficulty : int) -> void:
	queuedData = {
		"stageName" = inName,
		"stageDesc" = inDesc,
		"difficulty" = inDifficulty
	}

func _on_back_button_pressed() -> void:
	queue_free()
