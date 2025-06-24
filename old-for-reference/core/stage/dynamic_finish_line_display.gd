@tool

class_name DynamicFinishLine extends Node3D

@onready var corner_1: MeshInstance3D = $Corner1
@onready var corner_2: MeshInstance3D = $Corner2
@onready var finish_line_plane: MeshInstance3D = $FinishLinePlane

@export var width := 90.0
@export var height := 25.0

func update_finishline_mesh() -> void:
	corner_1.position.x = width * 0.5
	corner_2.position.x = width * -0.5
	
	corner_1.mesh.height = height
	corner_2.mesh.height = height
	
	var shape := finish_line_plane.mesh as PlaneMesh
	shape.size = Vector2(width, height)
	var shader := finish_line_plane.get_active_material(0) as ShaderMaterial
	shader.set_shader_parameter("finishline_size", Vector2(width, height))

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	update_finishline_mesh()


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float) -> void:
	if Engine.is_editor_hint():
		update_finishline_mesh()
