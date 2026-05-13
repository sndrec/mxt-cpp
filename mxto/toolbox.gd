class_name Toolbox extends Control

@onready var translate_gizmo: TranslateGizmo = $"../TranslateGizmo"
@onready var rotate_gizmo: RotateGizmo = $"../RotateGizmo"

@onready var transform_button: Button = $HBoxContainer/TransformButton
@onready var edit_button: Button = $HBoxContainer/EditButton

func _ready() -> void:
	transform_button.pressed.connect(enable_gizmos)
	edit_button.pressed.connect(disable_gizmos)

func disable_gizmos() -> void:
	rotate_gizmo.active = false
	translate_gizmo.active = false
func enable_gizmos() -> void:
	rotate_gizmo.active = true
	translate_gizmo.active = true
