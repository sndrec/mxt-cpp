class_name CarNodeContainer extends Node3D

const EFFECT_POOL_SIZE := 30

var num_cars := 100
var local_visual_car: VisualCar = null
var effect_pool: Array[VisualCar] = []

func instantiate_cars(definitions: Array, peer_ids: Array, local_player_id: int = 0):
	for child in get_children():
		remove_child(child)
		child.queue_free()
	local_visual_car = null
	effect_pool.clear()
	num_cars = definitions.size()
	var local_index := peer_ids.find(local_player_id)
	var pool_count = min(EFFECT_POOL_SIZE, max(num_cars, 0))
	var fallback_definition = definitions[0] if !definitions.is_empty() else null
	for slot in pool_count:
		var pool_car := preload("res://vehicle/visual_car.tscn").instantiate()
		pool_car.name = "EffectPoolCar%d" % slot
		pool_car.car_definition = fallback_definition
		pool_car.owning_id = -1
		pool_car.local_visual_enabled = false
		pool_car.effect_tier = VisualCar.EffectTier.THRUSTER_ONLY
		pool_car.effect_pool_slot = slot
		add_child(pool_car)
		pool_car.race_hud.queue_free()
		effect_pool.append(pool_car)
	if local_index >= 0 and local_index < num_cars:
		var new_car := preload("res://vehicle/visual_car.tscn").instantiate()
		new_car.name = "LocalVisualCar"
		new_car.car_definition = definitions[local_index]
		new_car.owning_id = peer_ids[local_index]
		new_car.local_visual_enabled = true
		new_car.effect_pool_slot = -1
		add_child(new_car)
		new_car.race_hud.focus_player_id = local_player_id
		new_car.car_camera.make_current()
		new_car.make_vehicle_audio_listener_current()
		local_visual_car = new_car
