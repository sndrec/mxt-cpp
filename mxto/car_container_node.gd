class_name CarNodeContainer extends Node3D

var num_cars := 100

func instantiate_cars(definitions: Array, peer_ids: Array, local_index: int = 0):
	for child in get_children():
		child.queue_free()
	num_cars = definitions.size()
	for i in num_cars:
		var new_car := preload("res://vehicle/visual_car.tscn").instantiate()
		new_car.car_definition = definitions[i]
		if i < peer_ids.size():
			new_car.owning_id = peer_ids[i]
		add_child(new_car)
		if i == local_index:
			var cam := new_car.get_node_or_null("CarCamera")
			if cam != null and cam.has_method("make_current"):
				cam.make_current()
			var name_label := new_car.get_node_or_null("CarTransform/NameLabel")
			if name_label:
				name_label.queue_free()
		var hud := new_car.get_node_or_null("race_hud")
		if hud:
			hud.queue_free()
