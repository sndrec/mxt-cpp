extends Control

var viewportCamera : Camera3D = Camera3D.new()
@onready var preview_viewport := %previewViewport
@onready var grid_container := $Container/ScrollContainer/GridContainer
@onready var car_preview_texture := %carPreviewTexture
@onready var car_name := $Container/HBoxContainer/VBoxContainer/CarName
@onready var line_edit := $LineEdit
#@onready var sticker_button_container: VBoxContainer = $Container/VBoxContainer
@onready var base_color: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/HBoxContainer/BaseColor
@onready var sec_color: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/HBoxContainer/SecColor
@onready var ter_color: ColorPickerButton = $Container/HBoxContainer/VBoxContainer/HBoxContainer/TerColor
@onready var setting_label: Label = $Container/HBoxContainer/VBoxContainer/SettingLabel
@onready var setting_slider: HSlider = $Container/HBoxContainer/VBoxContainer/SettingSlider

#var sticker_buttons : Array[MenuButton] = []
#
#var stickers := preload("res://content/base/texture/emote_sticker/sticker_selection.tres")

var loadedCar : Node3D
var axis : float = 0
var center : Vector3 = Vector3.ZERO
var current_car : String

#func populate_sticker_buttons() -> void:
	#for button in sticker_buttons.size():
		#var this_button := sticker_buttons[button]
		#for i in stickers.stickers.size():
			#this_button.get_popup().add_icon_item(stickers.stickers[i], "", i)
			#this_button.get_popup().set_item_icon_max_width(i, 124)
#
#func update_sticker_button_icons() -> void:
	#for button in sticker_buttons.size():
		#var this_button := sticker_buttons[button]
		#match button:
			#0:
				#this_button.icon = stickers.stickers[MXGlobal.local_settings.sticker_1]
			#1:
				#this_button.icon = stickers.stickers[MXGlobal.local_settings.sticker_2]
			#2:
				#this_button.icon = stickers.stickers[MXGlobal.local_settings.sticker_3]
			#3:
				#this_button.icon = stickers.stickers[MXGlobal.local_settings.sticker_4]

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	current_car = MXGlobal.local_settings.car_choice
	for i in MXGlobal.cars.size():
		var new_button := preload("res://core/ui/menus/CarButton.tscn").instantiate()
		new_button.car_definition = MXGlobal.cars[i]
		new_button.custom_minimum_size = Vector2(110, 110)
		grid_container.add_child(new_button)
	viewportCamera.rotation_degrees = Vector3(-30, 45, 0)
	viewportCamera.position = (viewportCamera.basis.z * axis * 1.5) + center
	viewportCamera.fov = 60
	viewportCamera.far = 32
	viewportCamera.near = 0.1
	#viewportCamera.set_cull_mask_value(2, false)
	preview_viewport.add_child(viewportCamera)
	var new_light := DirectionalLight3D.new()
	preview_viewport.add_child(new_light)
	new_light.global_rotation_degrees = Vector3(-45, 45, 0)
	line_edit.text = MXGlobal.local_settings.username
	#for i in sticker_button_container.get_child_count():
		#sticker_buttons.append(sticker_button_container.get_child(i))
		#match i:
			#0:
				#sticker_button_container.get_child(i).get_popup().id_pressed.connect(sticker_slot_1)
			#1:
				#sticker_button_container.get_child(i).get_popup().id_pressed.connect(sticker_slot_2)
			#2:
				#sticker_button_container.get_child(i).get_popup().id_pressed.connect(sticker_slot_3)
			#3:
				#sticker_button_container.get_child(i).get_popup().id_pressed.connect(sticker_slot_4)
	
	base_color.color_changed.connect(update_base_color)
	sec_color.color_changed.connect(update_secondary_color)
	ter_color.color_changed.connect(update_tertiary_color)
	setting_slider.value_changed.connect(set_machine_setting)
	setting_slider.value = MXGlobal.local_settings.accel_setting
	setting_label.text = "MACHINE SETTING: " + str(roundi(setting_slider.value * 100)) + "%"
	
	#populate_sticker_buttons()
	#update_sticker_button_icons()
	refresh_color_pickers()
	refresh_car_preview()

#func sticker_slot_1(in_sticker_index : int) -> void:
	#set_sticker_slot(1, in_sticker_index)
#func sticker_slot_2(in_sticker_index : int) -> void:
	#set_sticker_slot(2, in_sticker_index)
#func sticker_slot_3(in_sticker_index : int) -> void:
	#set_sticker_slot(3, in_sticker_index)
#func sticker_slot_4(in_sticker_index : int) -> void:
	#set_sticker_slot(4, in_sticker_index)

func set_machine_setting(in_setting : float) -> void:
	MXGlobal.local_settings.accel_setting = snappedf(setting_slider.value, 0.01)
	setting_label.text = "MACHINE SETTING: " + str(roundi(setting_slider.value * 100)) + "%"
	MXGlobal.local_settings._save_settings()

#func set_sticker_slot(in_slot : int, in_sticker_index : int) -> void:
	#match in_slot:
		#1:
			#MXGlobal.local_settings.sticker_1 = in_sticker_index
		#2:
			#MXGlobal.local_settings.sticker_2 = in_sticker_index
		#3:
			#MXGlobal.local_settings.sticker_3 = in_sticker_index
		#4:
			#MXGlobal.local_settings.sticker_4 = in_sticker_index
	#update_sticker_button_icons()
	#MXGlobal.local_settings._save_settings()

func refresh_color_pickers() -> void:
	base_color.color = MXGlobal.local_settings.base_color
	sec_color.color = MXGlobal.local_settings.secondary_color
	ter_color.color = MXGlobal.local_settings.tertiary_color

func update_base_color(in_color : Color) -> void:
	MXGlobal.local_settings.base_color = in_color
	MXGlobal.local_settings._save_settings()

func update_secondary_color(in_color : Color) -> void:
	MXGlobal.local_settings.secondary_color = in_color
	MXGlobal.local_settings._save_settings()
	
func update_tertiary_color(in_color : Color) -> void:
	MXGlobal.local_settings.tertiary_color = in_color
	MXGlobal.local_settings._save_settings()

func refresh_car_preview() -> void:
	if is_instance_valid(loadedCar):
		loadedCar.queue_free()
	var car_definition : CarDefinition = MXGlobal.cars[MXGlobal.car_lookup[current_car]]
	loadedCar = car_definition.model.instantiate()
	car_name.text = car_definition.name
	preview_viewport.add_child(loadedCar)
	
	axis = loadedCar.get_child(0).get_aabb().get_longest_axis_size() * loadedCar.get_child(0).scale.x
	center = loadedCar.get_child(0).position
	
	#var current_speed : float = 0
	#var iterations : int = Engine.physics_ticks_per_second * 30
	#var base_max_speed := 900.0 + (car_definition.weight * 0.01) + car_definition.top_speed * 100.0
	#for i in iterations:
		#var accel_ratio := remap(current_speed * MXGlobal.ups_to_kmh * 60, 0, base_max_speed, 0, 1)
		#var base_accel := lerpf(car_definition.acceleration * 4, 0, accel_ratio) * MXGlobal.tick_delta
		#current_speed = maxf(0.0, current_speed - MXGlobal.tick_delta * ((car_definition.drag * 2.0 + current_speed * car_definition.drag * 0.9) * 0.01))
		#var accel_add := base_accel * MXGlobal.tick_delta
		#current_speed += accel_add
		#var origin : Vector2 = Vector2(0, car_graph_offset.size.y)
		#var ratio := float(i) / float(iterations)
		#var limit := 20.0 * MXGlobal.kmh_to_ups
		#line_2d.add_point(origin + Vector2(car_graph_offset.size.x * ratio, (current_speed / limit) * -car_graph_offset.size.y))
	
	await RenderingServer.frame_post_draw
	car_preview_texture.texture = preview_viewport.get_texture()

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process( delta:float ) -> void:
	if Input.is_action_pressed("MenuBack"):
		queue_free()
	if current_car != MXGlobal.local_settings.car_choice:
		current_car = MXGlobal.local_settings.car_choice
		refresh_car_preview()
	var mesh_instance := loadedCar.get_child(0) as MeshInstance3D
	mesh_instance.set_instance_shader_parameter("base_color", MXGlobal.local_settings.base_color)
	mesh_instance.set_instance_shader_parameter("secondary_color", MXGlobal.local_settings.secondary_color)
	mesh_instance.set_instance_shader_parameter("tertiary_color", MXGlobal.local_settings.tertiary_color)
	mesh_instance.set_instance_shader_parameter("depth_offset", 0)
	viewportCamera.rotation_degrees = Vector3(-30, viewportCamera.rotation_degrees.y + delta * 15, 0)
	viewportCamera.position = (viewportCamera.basis.z * axis * 1.5) + center

func _on_line_edit_text_changed( new_text:String ) -> void:
	MXGlobal.local_settings.username = new_text
	MXGlobal.local_settings._save_settings()

func _on_line_edit_text_submitted( new_text:String ) -> void:
	MXGlobal.local_settings.username = new_text
	MXGlobal.local_settings._save_settings()
