extends Node

@export var listen_port: int = 5566
@export var gamesim_path: NodePath
@export var num_bots: int = 8
@export var car_container_path: NodePath
@export var follow_car_index: int = 0
@export var debug_mesh_path: NodePath

var _server := TCPServer.new()
var _peer: StreamPeerTCP
var _buf_str := ""
var _prev_lap_progress: Array = []
var _done_mask := PackedByteArray()
var _episode_idx := 0
var _step_idx := 0
var _rew_total := []
var _rew_last := []

func _ready() -> void:
	set_process(true)
	if gamesim_path == NodePath():
		push_warning("RLServer: gamesim_path not set; please assign GameSim node")
		return
	# Parse CLI: --rl-port <port> --rl-bots <n>
	var args := OS.get_cmdline_args()
	for i in range(args.size()):
		var a := String(args[i])
		if a == "--rl-port" and i + 1 < args.size():
			listen_port = int(args[i + 1])
		elif a == "--rl-bots" and i + 1 < args.size():
			num_bots = int(args[i + 1])
	var err = _server.listen(listen_port)
	if err != OK:
		push_error("RLServer: Failed to listen on port %d" % listen_port)
	else:
		print("RLServer listening on ", listen_port)
		_new_random_race() # visible immediately in UI

func _process(_dt: float) -> void:
	if _server.is_connection_available():
		_peer = _server.take_connection()
		_buf_str = ""
		_prev_lap_progress.clear()
		print("RLServer: client connected")
		_new_random_race()
	if _peer and _peer.get_status() == StreamPeerTCP.STATUS_CONNECTED:
		var available := _peer.get_available_bytes()
		if available > 0:
			var pkt := _peer.get_partial_data(available)
			if pkt[0] == OK:
				_buf_str += pkt[1].get_string_from_utf8()
				while true:
					var nl := _buf_str.find("\n")
					if nl == -1:
						break
					var line := _buf_str.substr(0, nl).strip_edges()
					_buf_str = _buf_str.substr(nl + 1)
					if line.is_empty():
						continue
					_handle_cmd(line)
	elif _peer and _peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		_peer = null
		print("RLServer: client disconnected")

func _send_json(obj: Variant) -> void:
	if not _peer or _peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		return
	var line := JSON.stringify(obj) + "\n"
	_peer.put_data(line.to_utf8_buffer())

func _handle_cmd(json_line: String) -> void:
	var parsed = JSON.parse_string(json_line)
	if typeof(parsed) != TYPE_DICTIONARY or not parsed.has("cmd"):
		_send_json({"ok": false, "err": "invalid_cmd"})
		return
	var cmd := String(parsed["cmd"]).to_lower()
	match cmd:
		"reset":
			_new_random_race()
			_episode_idx += 1
			_step_idx = 0
			_rew_total.clear(); _rew_last.clear()
			_send_json({"ok": true, "obs": _get_obs()})
		"get_obs":
			_update_obs()
			_send_json({"ok": true, "obs": _get_obs()})
		"step":
			if not parsed.has("actions") or typeof(parsed["actions"]) != TYPE_ARRAY:
				_send_json({"ok": false, "err": "actions_required"})
				return
			var actions: Array = parsed["actions"]
			# Size-robust input build to match number of cars
			var gs: GameSim = get_node(gamesim_path)
			var expected_n := 0
			var inf := gs.get_training_info()
			if typeof(inf) == TYPE_DICTIONARY and inf.has("lap"):
				expected_n = inf["lap"].size()
			var inputs: Array = []
			for i in expected_n:
				if i < actions.size():
					var a = actions[i]
					if typeof(a) == TYPE_ARRAY and a.size() >= 7:
						var sh = _tanh(a[0])
						var sv = _tanh(a[1])
						# to avoid pressing both at 0.5 when inputs are near zero.
						var accel = clamp(max(0.0, float(a[2])), 0.0, 1.0)
						var brake = clamp(max(0.0, float(a[3])), 0.0, 1.0)
						if accel > 0.0 and brake > 0.0:
							if accel >= brake:
								brake = 0.0
							else:
								accel = 0.0
						var boost = a[4] > 0.0
						var sideattack = a[5] > 0.0
						var spinattack = a[6] > 0.0
						inputs.append({"steer_horizontal": sh, "steer_vertical": sv, "accelerate": accel, "brake": brake, "boost": boost, "sideattack": sideattack, "spinattack": spinattack})
					else:
						inputs.append({})
				else:
					inputs.append({})
			# Pre-tick obs cache
			_update_obs()
			#var gs: GameSim = get_node(gamesim_path)
			# Ensure visuals are connected
			if car_container_path != NodePath():
				var cont := get_node_or_null(car_container_path)
				if cont != null:
					for child in cont.get_children():
						if child.has_node("race_hud"):
							child.get_node("race_hud").queue_free()
					gs.set_car_node_container(cont)
			# Advance sim one tick with inputs
			gs.tick_gamesim(inputs)
			gs.render_gamesim()
			# Build next observation and training signals
			var obs := _update_obs()
			var status := gs.get_training_info()
			var cur_prog: PackedFloat32Array = status.get("lap_progress", PackedFloat32Array())
			var laps: PackedInt32Array = status.get("lap", PackedInt32Array())
			var restore: PackedInt32Array = status.get("restore_state", PackedInt32Array())
			var rew: Array = []
			var done: Array = []
			var all_done := true
			for i in range(cur_prog.size()):
				var prev := float(_prev_lap_progress[i]) if (i < _prev_lap_progress.size()) else 0.0
				var r := float(cur_prog[i]) - prev
				rew.append(r)
				if _rew_total.size() <= i:
					_rew_total.resize(i+1)
					_rew_last.resize(i+1)
					_rew_total[i] = 0.0
				_rew_total[i] = _rew_total[i] + r
				_rew_last[i] = r
				var d := false
				if restore[i] != 0:
					d = true
				elif int(laps[i]) >= 4:
					d = true
				done.append(d)
				if d and _done_mask.size() > i and _done_mask[i] == 0:
					gs.set_car_retired(i, true)
					_done_mask[i] = 1
				if not d:
					all_done = false
			_prev_lap_progress = cur_prog
						var retired_arr: PackedInt32Array = status.get("retired", PackedInt32Array())
			var all_retired := false
			if retired_arr.size() > 0:
				all_retired = true
				for i in range(retired_arr.size()):
					if retired_arr[i] == 0:
						all_retired = false
						break
			var episode_end := false
			if all_done or all_retired:
				# Do NOT auto-reset here. Signal episode end and wait for explicit reset from client.
				episode_end = true
				# Ensure done flags are set so clients relying on them can terminate properly.
				for i in range(done.size()):
					done[i] = true
			_send_json({"ok": true, "obs": obs, "rew": rew, "done": done, "episode_end": episode_end})
			_step_idx += 1
			if Engine.has_singleton("DebugDraw2D"):
				DebugDraw2D.set_text("RL ep", _episode_idx)
				DebugDraw2D.set_text("RL step", _step_idx)
				for i in range(_rew_total.size()):
					DebugDraw2D.set_text("RL car %d r" % i, _rew_last[i])
					DebugDraw2D.set_text("RL car %d R" % i, _rew_total[i])
		_:
			_send_json({"ok": false, "err": "unknown_cmd"})

func _update_obs() -> Array:
	var gs: GameSim = get_node(gamesim_path)
	gs.update_observations()
	return _get_obs()

func _get_obs() -> Array:
	var gs: GameSim = get_node(gamesim_path)
	var count := 0
	var info := gs.get_training_info()
	if typeof(info) == TYPE_DICTIONARY and info.has("lap"):
		count = info["lap"].size()
	var out: Array = []
	for i in count:
		out.append(gs.get_observation_for_car(i))
	return out

static func _tanh(x: float) -> float:
	var e2x = exp(2.0 * clamp(x, -20.0, 20.0))
	return (e2x - 1.0) / (e2x + 1.0)

static func _sigmoid(x: float) -> float:
	var v = clamp(x, -20.0, 20.0)
	return 1.0 / (1.0 + exp(-v))

func _new_random_race() -> void:
	var gs: GameSim = get_node(gamesim_path)
	# Hook up visuals so we can watch
	if car_container_path != NodePath():
		var cont := get_node_or_null(car_container_path)
		if cont != null:
			for child in cont.get_children():
				if child.has_node("race_hud"):
					child.get_node("race_hud").queue_free()
			gs.set_car_node_container(cont)
	var tracks := _scan_tracks()
	if tracks.is_empty():
		push_error("No tracks found under res://track")
		return
	var info = tracks[randi() % tracks.size()]
	# Load debug track mesh like normal game flow
	if debug_mesh_path != NodePath():
		var mesh_node := get_node_or_null(debug_mesh_path)
		if mesh_node != null:
			var obj_path := String(info["mxt"]).get_basename() + ".obj"
			if ResourceLoader.exists(obj_path):
				mesh_node.mesh = load(obj_path)
				# Swap material named "track_surface" for debug rendering if present
				if mesh_node.mesh != null:
					for i in mesh_node.mesh.get_surface_count():
						var mat = mesh_node.mesh.surface_get_material(i)
						if mat != null and mat.resource_name == "track_surface":
							mesh_node.mesh.surface_set_material(i, preload("res://asset/debug_track_mat.tres"))
	var car_defs := _scan_car_defs()
	if car_defs.is_empty():
		push_error("No car definitions found under res://vehicle/asset")
		return
	var car_props : Array = []
	var accel : Array = []
	for i in num_bots:
		var def_res = car_defs[randi() % car_defs.size()]
		var bytes := FileAccess.get_file_as_bytes(def_res.car_definition)
		car_props.append(bytes)
		accel.append(randf())
	var level_buffer := StreamPeerBuffer.new()
	level_buffer.data_array = FileAccess.get_file_as_bytes(info["mxt"])
	# Instantiate visuals if available
	if car_container_path != NodePath():
		var cont := get_node_or_null(car_container_path)
		if cont and cont.has_method("instantiate_cars"):
			var ids := []
			for i in num_bots:
				ids.append(i + 1)
			var cam_index = clamp(follow_car_index, 0, max(0, num_bots-1))
			cont.instantiate_cars(car_defs.slice(0, num_bots), ids, cam_index)
			for child in cont.get_children():
				if child.has_node("race_hud"):
					child.get_node("race_hud").queue_free()
			gs.set_car_node_container(cont)
	gs.instantiate_gamesim(level_buffer, car_props, accel)
	_prev_lap_progress = []
	_done_mask = PackedByteArray()
	_done_mask.resize(num_bots)
	for i in num_bots:
		_done_mask[i] = 0
	_update_obs()

func _scan_tracks() -> Array:
	var out := []
	var dir := DirAccess.open("res://track")
	if dir == null:
		return out
	dir.list_dir_begin()
	var file := dir.get_next()
	while file != "":
		if dir.current_is_dir() and not file.begins_with("."):
			out.append_array(_scan_tracks_dir("res://track/%s" % file))
		file = dir.get_next()
	dir.list_dir_end()
	return out

func _scan_tracks_dir(path: String) -> Array:
	var out := []
	var dir := DirAccess.open(path)
	if dir == null:
		return out
	dir.list_dir_begin()
	var file := dir.get_next()
	while file != "":
		if not dir.current_is_dir() and file.get_extension() == "json":
			var json_path := path + "/" + file
			var mxt_path := json_path.get_basename() + ".mxt_track"
			if FileAccess.file_exists(mxt_path):
				var parsed = JSON.parse_string(FileAccess.get_file_as_string(json_path))
				if typeof(parsed) == TYPE_DICTIONARY and parsed.has("name"):
					out.append({"name": parsed["name"], "mxt": mxt_path})
		file = dir.get_next()
	dir.list_dir_end()
	return out

func _scan_car_defs() -> Array:
	var out := []
	var dir := DirAccess.open("res://vehicle/asset")
	if dir == null:
		return out
	dir.list_dir_begin()
	var folder := dir.get_next()
	while folder != "":
		if dir.current_is_dir() and not folder.begins_with("."):
			var def_path := "res://vehicle/asset/%s/definition.tres" % folder
			if ResourceLoader.exists(def_path):
				var def_res := load(def_path)
				if def_res != null:
					out.append(def_res)
		folder = dir.get_next()
	dir.list_dir_end()
	return out
