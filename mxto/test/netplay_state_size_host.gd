extends SceneTree

const MAIN_SCENE := preload("res://main.tscn")
const PlayerInputClass := preload("res://player/player_input.gd")

var main: Node
var started := false
var forced_begin := false
var begin_wait_frames := 0
var wait_frames := 0
var race_frames := 0
var race_start_server_tick := -1
var max_wait_frames := 900
var max_race_frames := 900
var input_bytes := PackedByteArray()

func _arg_int(args: Array, name: String, fallback: int) -> int:
	var idx := args.find(name)
	if idx == -1 or idx + 1 >= args.size():
		return fallback
	return int(args[idx + 1])

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	max_wait_frames = _arg_int(args, "--wait-frames", max_wait_frames)
	max_race_frames = _arg_int(args, "--race-frames", max_race_frames)
	var input := PlayerInputClass.new()
	input.accelerate = 1.0
	input_bytes = input.serialize()
	main = MAIN_SCENE.instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	if main == null:
		return false
	var nm = main.get("network_manager")
	if nm == null:
		return false
	if !started:
		wait_frames += 1
		if bool(nm.is_server) and nm.player_ids.size() >= 2 and nm.player_settings.size() >= nm.player_ids.size():
			print("MXT_NETPLAY_STATE_SIZE_HOST_START players=", nm.player_ids.size(), " cpus=", nm.get_cpu_roster().size())
			main.lobby_controller.request_start_race()
			started = true
		elif wait_frames >= max_wait_frames:
			push_error("MXT_NETPLAY_STATE_SIZE_HOST_TIMEOUT players=%s settings=%s" % [nm.player_ids.size(), nm.player_settings.size()])
			quit(1)
		return false
	var sim = main.get("game_sim")
	if sim == null:
		return false
	if !bool(sim.get_sim_started()):
		begin_wait_frames += 1
		if !forced_begin and begin_wait_frames >= 30:
			forced_begin = true
			print("MXT_NETPLAY_STATE_SIZE_HOST_FORCE_BEGIN")
			nm.begin_simulation.rpc()
			nm.begin_simulation()
		return false
	if race_start_server_tick < 0:
		race_start_server_tick = int(nm.server_tick)
	main.call("_simulate_host_frame", input_bytes)
	race_frames = int(nm.server_tick) - race_start_server_tick
	if race_frames >= max_race_frames:
		print("MXT_NETPLAY_STATE_SIZE_HOST_DONE race_ticks=", race_frames, " server_tick=", nm.server_tick)
		quit()
	return false
