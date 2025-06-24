@tool
class_name UIBox extends ColorRect

@export var center_pos : Vector2 = Vector2(0.5, 0.5)
@export var desired_size : Vector2 = Vector2(200, 200)
@export var border_size : float = 32.0
@export var border_uv_scale : float = 1.0
@export var bg_alpha : float = 0.2
@export var bg_uv_scale : float = 1.0
@export var bg_scroll_speed : Vector2 = Vector2(0, 0)
@export var bg_random_scroll : bool = false
@export var border_texture : Texture2D
@export var bg_texture : Texture2D

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	if !Engine.is_editor_hint():
		size = Vector2(0, 0)
	material = ShaderMaterial.new()
	material.shader = preload("res://core/ui/menus/UIBox.gdshader")
	material.set_shader_parameter("box_size", size)
	material.set_shader_parameter("border_size", border_size)
	material.set_shader_parameter("border_uv_scale", border_uv_scale)
	material.set_shader_parameter("noise", bg_alpha)
	material.set_shader_parameter("noise_uv_scale", bg_uv_scale)
	material.set_shader_parameter("border_texture", border_texture)
	material.set_shader_parameter("bg_texture", bg_texture)
	material.set_shader_parameter("bg_scroll_speed", bg_scroll_speed)
	material.set_shader_parameter("bg_random_scroll", bg_random_scroll)


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	if Engine.is_editor_hint():
		material.set_shader_parameter("border_size", border_size)
		material.set_shader_parameter("border_uv_scale", border_uv_scale)
		material.set_shader_parameter("noise", bg_alpha)
		material.set_shader_parameter("noise_uv_scale", bg_uv_scale)
		material.set_shader_parameter("border_texture", border_texture)
		material.set_shader_parameter("bg_texture", bg_texture)
		material.set_shader_parameter("bg_scroll_speed", bg_scroll_speed)
		material.set_shader_parameter("bg_random_scroll", bg_random_scroll)
	size = size.move_toward(desired_size, 2000 * delta)
	material.set_shader_parameter("box_size", size)
	position = Vector2(1280, 720) * center_pos - size * 0.5
