extends Control
@onready var color_rect := $ColorRect as ColorRect
@onready var resume_button = $ColorRect/MarginContainer/Control/VBoxContainer/ResumeButton
@onready var color_rect_2 := $ColorRect2 as ColorRect
var create_time := 0
@onready var v_box_container := $ColorRect/MarginContainer/Control/VBoxContainer as VBoxContainer


# Called when the node enters the scene tree for the first time.
func _ready():
	color_rect.size = Vector2.ZERO
	resume_button.grab_focus()
	color_rect_2.modulate.a = 0
	create_time = Time.get_ticks_msec()
	v_box_container.modulate.a = 0
	var shader_mat := color_rect.material as ShaderMaterial
	shader_mat.set_shader_parameter("noise", 1.0)
	v_box_container.scale = Vector2(0, 1)

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta):
	color_rect.size = color_rect.size.move_toward(Vector2(300, 200), delta * 1000)
	color_rect_2.modulate.a = move_toward(color_rect_2.modulate.a, 0.2, delta)
	color_rect.position = Vector2(640,360) - color_rect.size * 0.5
	var shader_mat := color_rect.material as ShaderMaterial
	shader_mat.set_shader_parameter("box_size", color_rect.size)
	if Time.get_ticks_msec() > create_time + 500:
		v_box_container.scale = v_box_container.scale.move_toward(Vector2.ONE, delta)
		shader_mat.set_shader_parameter("noise", move_toward(shader_mat.get_shader_parameter("noise"), 0.1, delta))
	if Time.get_ticks_msec() > create_time + 100:
		if Input.is_action_just_pressed("Pause"):
			queue_free()

func _on_resume_button_pressed():
	queue_free()


func _on_retry_button_pressed():
	pass # Replace with function body.


func _on_options_button_pressed():
	pass # Replace with function body.


func _on_give_up_button_pressed():
	pass # Replace with function body.


func _on_disconnect_button_pressed():
	pass # Replace with function body.
