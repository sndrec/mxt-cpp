extends Node

@onready var game_sim: GameSim = $GameSim
@onready var button: Button = $Control/Button
@onready var car_node_container: MultiMeshInstance3D = $GameWorld/MultiMeshInstance3D
@onready var cam: Camera3D = $GameWorld/Camera3D2

func _on_button_pressed() -> void:
	var level_buffer := StreamPeerBuffer.new()
	var test_level := FileAccess.get_file_as_bytes("res://test/test_track.mxt_track")
	level_buffer.data_array = test_level
	game_sim.car_node_container = car_node_container
	game_sim.instantiate_gamesim(level_buffer)

func _physics_process(delta: float) -> void:
	DebugDraw3D.scoped_config().set_no_depth_test(true)
	if game_sim.sim_started:
		game_sim.tick_gamesim()
		game_sim.render_gamesim()
		var ct1 = car_node_container.multimesh.get_instance_transform(0)
		cam.basis = ct1.basis
		cam.position = ct1.origin + cam.basis.y * 4 + cam.basis.z * 10

func _process(delta: float) -> void:
	pass
