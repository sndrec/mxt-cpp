extends Node

@onready var game_sim: GameSim = $GameSim
@onready var button: Button = $Control/Button
@onready var car_node_container: CarNodeContainer = $GameWorld/CarNodeContainer
@onready var cam: Camera3D = $GameWorld/Camera3D2

func _on_button_pressed() -> void:
	car_node_container.instantiate_cars()
	var level_buffer := StreamPeerBuffer.new()
	var test_level := FileAccess.get_file_as_bytes("res://test/cylinder_test/cylinder_test.mxt_track")
	level_buffer.data_array = test_level
	game_sim.car_node_container = car_node_container
	game_sim.instantiate_gamesim(level_buffer)

func _physics_process(delta: float) -> void:
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if game_sim.sim_started:
		game_sim.tick_gamesim()
		game_sim.render_gamesim()

func _process(delta: float) -> void:
	pass
