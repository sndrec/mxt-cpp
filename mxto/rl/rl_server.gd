extends Node

@export var listen_port: int = 5566
@export var gamesim_path: NodePath
@export var num_bots: int = 32
@export var car_container_path: NodePath
@export var follow_car_index: int = 0
@export var debug_mesh_path: NodePath
@export var warmup_steps: int = 300
@export var damage_penalty_scale: float = 1.0
@export var align_bonus_scale: float = 0.2
@export var step_max_hz: int = 60
@export var show_obs: bool = false
@export var obs_car_index: int = 0
@export var render_during_training: bool = false
@export var render_visible_cars: int = 4
@export var render_skip: int = 3
@export var retire_neg_streak: int = 90 # per-car early-out if consecutive non-positive rewards exceed this (0 disables)
@export var early_end_retired_frac: float = 0.95 # end episode if this fraction of cars are retired (post-warmup)

var _server := TCPServer.new()
var _peer: StreamPeerTCP
var _last_step_ms := 0
var _buf_str := ""
var _prev_lap_progress: Array = []
var _done_mask := PackedByteArray()
var _warmup_left := 0
var _episode_idx := 0
var _step_idx := 0
var _rew_total := []
var _rew_last := []
var _no_pos_rew_steps := 0
var _input_cache: Array = []
var _input_cache_size := 0
var _render_frame_i := 0
var _neg_streak := PackedInt32Array()

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
			_warmup_left = warmup_steps
			_input_cache.clear()
			_input_cache_size = 0
			_no_pos_rew_steps = 0
			_last_step_ms = Time.get_ticks_msec()
			_render_frame_i = 0
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
					if typeof(a) == TYPE_ARRAY and a.size() >= 5:
						var sh = _tanh(a[0])
						var sv = _tanh(a[1])
						var accel = clamp(max(0.0, float(a[2])), 0.0, 1.0)
						var brake = clamp(max(0.0, float(a[3])), 0.0, 1.0)
						if accel > 0.0 and brake > 0.0:
							if accel >= brake:
								brake = 0.0
							else:
								accel = 0.0
						var boost = a[4] > 0.0
						var sideattack = false
						var spinattack = false
						inputs.append({"steer_horizontal": sh, "steer_vertical": sv, "accelerate": accel, "brake": brake, "boost": boost, "sideattack": sideattack, "spinattack": spinattack})
					else:
						inputs.append({})
				else:
					inputs.append({})
			# Pre-tick obs cache
			_update_obs()
			# Avoid per-step visual/node churn; visuals are set up in _new_random_race()
			# Advance sim one tick with inputs
			var in_warmup := _warmup_left > 0
			if in_warmup:
				if _input_cache_size != inputs.size():
					_input_cache.clear()
					_input_cache.resize(inputs.size())
					for i in range(inputs.size()):
						_input_cache[i] = {"steer_horizontal": 0.0, "steer_vertical": 0.0, "accelerate": 1.0, "brake": 0.0, "boost": false, "sideattack": false, "spinattack": false}
					_input_cache_size = inputs.size()
				for i in range(inputs.size()):
					inputs[i] = _input_cache[i]
			var __min_ms := int(1000.0 / float(step_max_hz)) if step_max_hz > 0 else 0
			if __min_ms > 0:
				var __now := Time.get_ticks_msec()
				if _last_step_ms > 0:
					var __dt := __now - _last_step_ms
					if __dt < __min_ms:
						OS.delay_msec(__min_ms - __dt)
						__now = Time.get_ticks_msec()
				_last_step_ms = __now
			gs.tick_gamesim(inputs)
			if render_during_training:
				var skip = max(render_skip, 1)
				if (_render_frame_i % skip) == 0:
					gs.render_gamesim()
				_render_frame_i += 1
			# Build next observation and training signals
			var obs := _update_obs()
			# Optional: display observation floats for chosen car index
			if show_obs and Engine.has_singleton("DebugDraw2D"):
				var ci = clamp(obs_car_index, 0, max(0, obs.size()-1))
				if obs.size() > 0 and ci < obs.size():
					var ob = obs[ci]
					#if typeof(ob) == TYPE_PACKED_FLOAT32_ARRAY:
						#for i in range(ob.size()):
							#DebugDraw2D.set_text("obs[%d]" % i, ob[i])

			var status := gs.get_training_info()
			var cur_prog: PackedFloat32Array = status.get("lap_progress", PackedFloat32Array())
			var laps: PackedInt32Array = status.get("lap", PackedInt32Array())
			var restore: PackedInt32Array = status.get("restore_state", PackedInt32Array())
			var rew: Array = []
			var done: Array = []
			var all_done := true
			for i in range(cur_prog.size()):
				var r := 0.0
				# If this car was already marked done/retired, don't accrue more reward
				if _done_mask.size() > i and _done_mask[i] == 1:
					rew.append(0.0)
					done.append(true)
					continue
				if i < obs.size():
					var ob = obs[i]
					if typeof(ob) == TYPE_PACKED_FLOAT32_ARRAY:
						var vel := Vector3(float(ob[5]), float(ob[6]), float(ob[4]))
						var dmg := float(ob[21])
						var tx := float(ob[0])
						tx = move_toward(tx, 0, 0.25)
						tx = absf(tx)
						tx = remap(tx, 0, 0.75, 1.0, -0.25)
						tx = maxf(tx, 0.01)
						var speed_reward : float = vel.length() * 0.005 * tx * ob[7]
						if speed_reward > 0 and signf(vel.z) < 0:
							speed_reward *= -1
						if speed_reward < 0:
							speed_reward *= 1.25
						var damage_penalty := damage_penalty_scale * dmg
						if damage_penalty > 0:
							speed_reward *= 0.1
						var align_reward : float = ob[11] * ob[3]
						var zero_penalty = 0.0025 if vel.length() <= 0.01 else 0.0
						if i == obs_car_index:
							DebugDraw2D.set_text("align 1", ob[7])
							DebugDraw2D.set_text("align 2", ob[8])
							DebugDraw2D.set_text("align 3", ob[9])
							DebugDraw2D.set_text("align 4", ob[10])
							DebugDraw2D.set_text("side align 1", ob[11])
							DebugDraw2D.set_text("side align 2", ob[12])
							DebugDraw2D.set_text("side align 3", ob[13])
							DebugDraw2D.set_text("side align 4", ob[14])
							DebugDraw2D.set_text("angle vel y", ob[3])
							DebugDraw2D.set_text("align_reward", align_reward)
							DebugDraw2D.set_text("tx", tx)
							DebugDraw2D.set_text("speed_reward", speed_reward)
							DebugDraw2D.set_text("damage_penalty", damage_penalty)
							#DebugDraw2D.set_text("align_reward", align_reward)
						r = speed_reward + align_reward - damage_penalty - zero_penalty
						if r < 0:
							r *= 1.25
				rew.append(r)
				if _rew_total.size() <= i:
					_rew_total.resize(i+1)
					_rew_last.resize(i+1)
					_rew_total[i] = 0.0
				if _warmup_left <= 0:
					_rew_total[i] = _rew_total[i] + r
					_rew_last[i] = r
				else:
					_rew_last[i] = 0.0
				var d := false
				if restore[i] != 0:
					d = true
				elif int(laps[i]) >= 4:
					d = true
				# Per-car early-out: retire if too many consecutive non-positive rewards
				if not d and _warmup_left <= 0 and retire_neg_streak > 0:
					if r > 0.0:
						if _neg_streak.size() > i:
							_neg_streak[i] = 0
					else:
						if _neg_streak.size() <= i:
							_neg_streak.resize(i+1)
							_neg_streak[i] = 0
						_neg_streak[i] = _neg_streak[i] + 1
						if _neg_streak[i] >= retire_neg_streak:
							d = true
				done.append(d)
				if d and _done_mask.size() > i and _done_mask[i] == 0:
					gs.set_car_retired(i, true)
					_done_mask[i] = 1
				if not d:
					all_done = false
			_prev_lap_progress = cur_prog
			var in_warmup2 := _warmup_left > 0
			if in_warmup2:
				for i in range(rew.size()):
					rew[i] = 0.0
				for i in range(done.size()):
					done[i] = false
				_warmup_left -= 1
				_no_pos_rew_steps = 0
			else:
				var any_pos := false
				for i in range(rew.size()):
					if rew[i] > 0.0:
						any_pos = true
						break
				if any_pos:
					_no_pos_rew_steps = 0
				else:
					_no_pos_rew_steps += 1

			var retired_arr: PackedInt32Array = status.get("retired", PackedInt32Array())
			var all_retired := false
			var retired_frac := 0.0
			if retired_arr.size() > 0:
				var retired_count := 0
				all_retired = true
				for i in range(retired_arr.size()):
					if retired_arr[i] == 0:
						all_retired = false
					else:
						retired_count += 1
				retired_frac = float(retired_count) / float(retired_arr.size())
			var all_done_mask := true
			if _done_mask.size() > 0:
				for i in range(_done_mask.size()):
					if _done_mask[i] == 0:
						all_done_mask = false
						break
			var episode_end := false
			var hit_retired_frac := (retired_frac >= early_end_retired_frac) and (_warmup_left <= 0)
			if ((all_done or all_retired or all_done_mask) and _warmup_left <= 0) or hit_retired_frac:
				# Do NOT auto-reset here. Signal episode end and wait for explicit reset from client.
				episode_end = true
				# Ensure done flags are set so clients relying on them can terminate properly.
				for i in range(done.size()):
					done[i] = true
			elif _no_pos_rew_steps > 120 and _warmup_left <= 0:
				episode_end = true
				for i in range(done.size()):
					done[i] = true
			_send_json({"ok": true, "obs": obs, "rew": rew, "done": done, "episode_end": episode_end, "warmup": (_warmup_left > 0)})
			_step_idx += 1
			if episode_end:
				_rew_total.clear()
				_rew_last.clear()
			if Engine.has_singleton("DebugDraw2D"):
				DebugDraw2D.set_text("RL ep", _episode_idx)
				DebugDraw2D.set_text("RL step", _step_idx)
				DebugDraw2D.set_text("RL car last reward", _rew_last[obs_car_index])
				DebugDraw2D.set_text("RL car total reward", _rew_total[obs_car_index])
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
	# Fully reset GameSim if a session is already running
	if gs.get_sim_started():
		gs.destroy_gamesim()
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
	var car_defs_for_bots : Array = []
	for i in num_bots:
		var def_res = car_defs[randi() % car_defs.size()]
		var bytes := FileAccess.get_file_as_bytes(def_res.car_definition)
		car_props.append(bytes)
		accel.append(randf())
		car_defs_for_bots.append(def_res)
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
			var vis_n = clamp(render_visible_cars, 0, num_bots)
			if vis_n > 0:
				cont.instantiate_cars(car_defs_for_bots.slice(0, vis_n), ids.slice(0, vis_n), clamp(cam_index, 0, max(0, vis_n-1)))
			for child in cont.get_children():
				if child.has_node("race_hud"):
					child.get_node("race_hud").queue_free()
			gs.set_car_node_container(cont)
	gs.instantiate_gamesim(level_buffer, car_props, accel)
	_prev_lap_progress = []
	_done_mask = PackedByteArray()
	_done_mask.resize(num_bots)
	_neg_streak = PackedInt32Array()
	_neg_streak.resize(num_bots)
	for i in num_bots:
		_done_mask[i] = 0
		_neg_streak[i] = 0
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
