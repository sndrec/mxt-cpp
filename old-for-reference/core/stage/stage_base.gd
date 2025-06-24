@tool

class_name RORStage extends Node3D

@export var stageName : String = "Your colourful, awesome stage!"

@export_multiline var stageDesc : String = "This is a brand new stage, created by you."

var cpc_container:Node

@export var autoCalculateStageBounds : bool = false:
	set(new_bool):
		autoCalculateStageBounds = false
		if new_bool == true:
			var newBounds := AABB(Vector3.ZERO, Vector3.ZERO)
			for node in get_children():
				if node is RORStageObject and node.get_child(0) is MeshInstance3D:
					var nodeMesh : Mesh = node.get_child(0).mesh
					for vertex in nodeMesh.get_faces():
						var vertexPos:Vector3 = node.global_transform * vertex
						newBounds = newBounds.expand(vertexPos)
			stageVisualBounds = newBounds
			stageFalloutBounds = newBounds.grow(60)

var checkpoint_respawns : Array[Checkpoint] = []

@export var stageVisualBounds : AABB

@export var stageFalloutBounds : AABB

@export_range(1, 100) var difficulty : int = 10

@export var stage_music : MXMusic

func _process(_delta: float) -> void:
	if !Engine.is_editor_hint():
		return
	#DebugDraw3D.draw_aabb(stageVisualBounds, Color(1, 1, 1), delta)
	#DebugDraw3D.draw_aabb(stageFalloutBounds, Color(1, 0, 0), delta)

func _init(p_stage_music : MXMusic = null, p_difficulty := 10, p_stageName := "Your colourful, awesome stage!", p_stageDesc := "This is a brand new stage, created by you.", p_stageVisualBounds := AABB(Vector3(-20, -20, -20), Vector3(40, 40, 40)), p_stageFalloutBounds := AABB(Vector3(-40, -40, -40), Vector3(80, 80, 80)), p_checkpoint_respawns : Array[Checkpoint] = []) -> void:
	stageName = p_stageName
	stageDesc = p_stageDesc
	stageVisualBounds = p_stageVisualBounds
	stageFalloutBounds = p_stageFalloutBounds
	difficulty = p_difficulty
	stage_music = p_stage_music
	checkpoint_respawns = p_checkpoint_respawns

func _get_mesh_points(in_mesh : MeshInstance3D) -> PackedVector3Array:
	var points := in_mesh.mesh as Mesh
	var new_points : PackedVector3Array = points.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	var samples_to_delete : Array[int] = []
	for i in range(new_points.size()):
		if i != new_points.size() - 1:
			if new_points[i].is_equal_approx(new_points[i + 1]):
				samples_to_delete.append(i)
	samples_to_delete.reverse()
	for i in range(samples_to_delete.size()):
		new_points.remove_at(samples_to_delete[i])
	new_points.resize(new_points.size() - 1)
	return new_points

func _ready() -> void:
	name = "ROStage"
	if Engine.is_editor_hint(): return
	
	cpc_container = find_child("CPCContainer")
	if cpc_container:
		for child:CheckpointController in cpc_container.get_children():
			checkpoint_respawns.append(child.our_checkpoint)
		cpc_container.queue_free()

func _on_ball_spawn( _inCar : MXRacer, _inID : int ) -> void:
	pass
