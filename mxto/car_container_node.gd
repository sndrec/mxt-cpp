class_name CarNodeContainer extends Node3D

var num_cars := 100

func instantiate_cars(definitions: Array, peer_ids: Array, local_player_id: int = 0):
	for child in get_children():
		remove_child(child)
		child.queue_free()
	num_cars = definitions.size()
	for i in num_cars:
		var new_car := preload("res://vehicle/visual_car.tscn").instantiate()
		new_car.car_definition = definitions[i]
		if i < peer_ids.size():
			new_car.owning_id = peer_ids[i]
		new_car.local_visual_enabled = new_car.owning_id == local_player_id
		if !new_car.local_visual_enabled:
			new_car.effect_tier = VisualCar.EffectTier.THRUSTER_ONLY
		add_child(new_car)
		if new_car.local_visual_enabled:
			new_car.race_hud.focus_player_id = local_player_id
			new_car.car_camera.make_current()
			new_car.name_label.queue_free()
		else:
			new_car.race_hud.queue_free()
