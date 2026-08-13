extends SceneTree

const MAIN_SCENE := preload("res://main.tscn")
const PlayerInputClass := preload("res://player/player_input.gd")

var main: Node
var race_seen := false
var wait_frames := 0
var race_frames := 0
var race_start_local_tick := -1
var max_wait_frames := 1200
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
	if !race_seen:
		wait_frames += 1
		var sim = main.get("game_sim")
		if bool(nm.race_active) and sim != null and bool(sim.get_sim_started()):
			print("MXT_NETPLAY_STATE_SIZE_CLIENT_RACE_SEEN players=", nm.player_ids.size())
			race_seen = true
		elif wait_frames >= max_wait_frames:
			push_error("MXT_NETPLAY_STATE_SIZE_CLIENT_TIMEOUT active=%s players=%s" % [str(nm.race_active), nm.player_ids.size()])
			quit(1)
		return false
	if !bool(nm.has_network_peer()):
		push_error("MXT_NETPLAY_STATE_SIZE_CLIENT_DISCONNECTED local_tick=%d race_ticks=%d" % [int(nm.input_transport.local_tick), race_frames])
		quit(1)
		return false
	if race_start_local_tick < 0:
		race_start_local_tick = int(nm.input_transport.local_tick)
	nm.input_transport.set_local_input(input_bytes)
	main.call("_simulate_single_tick")
	race_frames = int(nm.input_transport.local_tick) - race_start_local_tick
	if race_frames >= max_race_frames:
		print("MXT_NETPLAY_STATE_SIZE_CLIENT_DONE race_ticks=", race_frames, " local_tick=", nm.input_transport.local_tick)
		quit()
	return false
