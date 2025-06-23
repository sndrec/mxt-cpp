class_name CarNodeContainer extends Node3D

var num_cars := 100

func instantiate_cars(definitions: Array, local_index: int = 0):
				for child in get_children():
								child.queue_free()
				num_cars = definitions.size()
				for i in num_cars:
								var new_car := preload("res://vehicle/visual_car.tscn").instantiate()
								new_car.car_definition = definitions[i]
								add_child(new_car)
								if i == local_index:
												new_car.car_camera.make_current()
