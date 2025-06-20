class_name CarNodeContainer extends Node3D

var num_cars := 1

func instantiate_cars():
	for child in get_children():
		child.queue_free()
	for i in num_cars:
		var new_car := preload("res://vehicle/visual_car.tscn").instantiate()
		add_child(new_car)
		if i == 0:
			new_car.car_camera.make_current()
