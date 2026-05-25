class_name ProximityVoiceChat
extends Node

const VOICE_ACTION := "VoiceChat"
const VOICE_TOGGLE_ACTION := "ToggleVoice"
const VOICE_SETTINGS_PATH := "user://voice_chat_settings.json"
const VOICE_MODE_PUSH_TO_TALK := "push_to_talk"
const VOICE_MODE_TOGGLE := "toggle"
const VOICE_MODE_ALWAYS_ON := "always_on"
const VOICE_MODE_OFF := "off"
const CAPTURE_BUS_NAME := "VoiceCapture"
const VOICE_SAMPLE_RATE := 16000
const VOICE_FRAME_SAMPLES := 320
const VOICE_RANGE := 180.0
const VOICE_MAX_RECIPIENTS := 6
const PLAYBACK_BUFFER_SECONDS := 0.08
const VOICE_PEER_TIMEOUT_MSEC := 2000
const VOICE_SETTINGS_RELOAD_MSEC := 500

var network_manager: NetworkManager
var capture_player: AudioStreamPlayer
var capture_effect: AudioEffectCapture
var capture_codec: OpusVoiceCodec
var capture_resample_pos := 0.0
var capture_samples: Array[float] = []
var capture_sample_head := 0
var voice_sequence := 0
var remote_peers := {}
var voice_input_mode := VOICE_MODE_PUSH_TO_TALK
var voice_listen_enabled := true
var voice_talk_toggled := false
var last_voice_settings_load_msec := -VOICE_SETTINGS_RELOAD_MSEC

func _ready() -> void:
	network_manager = get_parent() as NetworkManager
	_ensure_voice_input_action()
	_load_voice_settings()
	set_physics_process(true)

func _ensure_voice_input_action() -> void:
	_ensure_key_action(VOICE_ACTION, KEY_V)
	_ensure_key_action(VOICE_TOGGLE_ACTION, KEY_T)

func _ensure_key_action(action: String, keycode: Key) -> void:
	if !InputMap.has_action(action):
		InputMap.add_action(action)
	if !InputMap.action_get_events(action).is_empty():
		return
	var key := InputEventKey.new()
	key.keycode = keycode
	InputMap.action_add_event(action, key)

func reset() -> void:
	_clear_remote_peers()
	capture_samples.clear()
	capture_sample_head = 0
	capture_resample_pos = 0.0
	voice_talk_toggled = false
	if capture_effect != null:
		capture_effect.clear_buffer()

func _clear_remote_peers() -> void:
	for peer_id in remote_peers.keys():
		var peer: Dictionary = remote_peers[peer_id]
		var player := peer.get("player") as AudioStreamPlayer3D
		if player != null:
			player.queue_free()
	remote_peers.clear()

func _physics_process(_delta: float) -> void:
	_poll_voice_settings()
	if network_manager == null or !network_manager.race_active or !network_manager.has_network_peer():
		reset()
		return
	_update_toggle_voice_state()
	if voice_listen_enabled:
		_update_remote_source_positions()
	elif !remote_peers.is_empty():
		_clear_remote_peers()
	_capture_and_send()

func _poll_voice_settings() -> void:
	var now := Time.get_ticks_msec()
	if now - last_voice_settings_load_msec < VOICE_SETTINGS_RELOAD_MSEC:
		return
	last_voice_settings_load_msec = now
	_load_voice_settings()

func _load_voice_settings() -> void:
	voice_input_mode = VOICE_MODE_PUSH_TO_TALK
	voice_listen_enabled = true
	if !FileAccess.file_exists(VOICE_SETTINGS_PATH):
		return
	var txt := FileAccess.get_file_as_string(VOICE_SETTINGS_PATH)
	var data = JSON.parse_string(txt)
	if typeof(data) != TYPE_DICTIONARY:
		return
	var mode := str(data.get("input_mode", VOICE_MODE_PUSH_TO_TALK))
	if mode == VOICE_MODE_PUSH_TO_TALK or mode == VOICE_MODE_TOGGLE or mode == VOICE_MODE_ALWAYS_ON or mode == VOICE_MODE_OFF:
		voice_input_mode = mode
	voice_listen_enabled = bool(data.get("listen_enabled", true))

func _update_toggle_voice_state() -> void:
	if voice_input_mode != VOICE_MODE_TOGGLE:
		voice_talk_toggled = false
		return
	if InputMap.has_action(VOICE_TOGGLE_ACTION) and Input.is_action_just_pressed(VOICE_TOGGLE_ACTION):
		voice_talk_toggled = !voice_talk_toggled

func _capture_and_send() -> void:
	if network_manager == null or network_manager.game_manager == null or network_manager.game_manager.headless_mode:
		return
	if !_local_player_should_send_voice():
		if capture_effect != null:
			capture_effect.clear_buffer()
		capture_samples.clear()
		capture_sample_head = 0
		return
	_ensure_capture()
	if capture_effect == null:
		return
	var available := capture_effect.get_frames_available()
	if available <= 0:
		return
	var input_frames := capture_effect.get_buffer(available)
	if input_frames.is_empty():
		return
	var input_rate := float(AudioServer.get_mix_rate())
	if input_rate <= 0.0:
		input_rate = 48000.0
	var step := input_rate / float(VOICE_SAMPLE_RATE)
	while capture_resample_pos < float(input_frames.size()):
		var idx := int(capture_resample_pos)
		var stereo := input_frames[idx]
		capture_samples.append(clampf((stereo.x + stereo.y) * 0.5, -1.0, 1.0))
		capture_resample_pos += step
	capture_resample_pos -= float(input_frames.size())
	while capture_samples.size() - capture_sample_head >= VOICE_FRAME_SAMPLES:
		if capture_codec == null:
			return
		var frame := PackedFloat32Array()
		frame.resize(VOICE_FRAME_SAMPLES)
		for i in range(VOICE_FRAME_SAMPLES):
			frame[i] = capture_samples[capture_sample_head + i]
		capture_sample_head += VOICE_FRAME_SAMPLES
		var payload := capture_codec.encode(frame)
		if !payload.is_empty():
			_send_voice_payload(payload)
	if capture_sample_head > VOICE_FRAME_SAMPLES * 8:
		capture_samples = capture_samples.slice(capture_sample_head)
		capture_sample_head = 0

func _local_player_should_send_voice() -> bool:
	if !_local_player_can_use_voice():
		return false
	if voice_input_mode == VOICE_MODE_OFF:
		return false
	if voice_input_mode == VOICE_MODE_ALWAYS_ON:
		return true
	if voice_input_mode == VOICE_MODE_TOGGLE:
		return voice_talk_toggled
	return InputMap.has_action(VOICE_ACTION) and Input.is_action_pressed(VOICE_ACTION)

func _local_player_can_use_voice() -> bool:
	var local_id := multiplayer.get_unique_id()
	var settings = network_manager.player_settings.get(local_id, {})
	if typeof(settings) == TYPE_DICTIONARY and bool(settings.get("spectator", false)):
		return false
	return network_manager.get_simulation_roster().has(local_id)

func _ensure_capture() -> void:
	if capture_effect != null:
		if capture_player != null and !capture_player.playing:
			capture_player.play()
		return
	var bus_index := AudioServer.get_bus_index(CAPTURE_BUS_NAME)
	if bus_index < 0:
		AudioServer.add_bus(AudioServer.get_bus_count())
		bus_index = AudioServer.get_bus_count() - 1
		AudioServer.set_bus_name(bus_index, CAPTURE_BUS_NAME)
		AudioServer.set_bus_send(bus_index, "Master")
		AudioServer.set_bus_volume_db(bus_index, -80.0)
	capture_effect = AudioEffectCapture.new()
	capture_effect.buffer_length = 0.08
	AudioServer.add_bus_effect(bus_index, capture_effect, 0)
	capture_codec = OpusVoiceCodec.new()
	capture_codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, 24000, 3)
	capture_player = AudioStreamPlayer.new()
	capture_player.name = "VoiceCapturePlayer"
	capture_player.stream = AudioStreamMicrophone.new()
	capture_player.bus = CAPTURE_BUS_NAME
	add_child(capture_player)
	capture_player.play()

func _send_voice_payload(payload: PackedByteArray) -> void:
	if payload.is_empty() or network_manager == null:
		return
	voice_sequence = (voice_sequence + 1) & 0x7fffffff
	var source_tick := _voice_spatial_tick()
	if network_manager.is_server:
		_relay_voice_from(multiplayer.get_unique_id(), voice_sequence, source_tick, payload)
	else:
		_client_voice_packet.rpc_id(1, voice_sequence, source_tick, payload)

@rpc("any_peer", "unreliable_ordered", "call_remote", 6)
func _client_voice_packet(sequence: int, source_tick: int, payload: PackedByteArray) -> void:
	if network_manager == null or !network_manager.is_server or !network_manager.race_active:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if sender_id <= 0 or !network_manager.get_simulation_roster().has(sender_id):
		return
	_relay_voice_from(sender_id, sequence, source_tick, payload)

func _relay_voice_from(sender_id: int, sequence: int, _source_tick: int, payload: PackedByteArray) -> void:
	var recipients := _voice_recipients_for(sender_id)
	var local_id := multiplayer.get_unique_id()
	for recipient in recipients:
		if int(recipient) == sender_id:
			continue
		if int(recipient) == local_id:
			_receive_voice_packet(sender_id, sequence, payload)
		else:
			_receive_voice_packet.rpc_id(int(recipient), sender_id, sequence, payload)

@rpc("authority", "unreliable_ordered", "call_remote", 6)
func _receive_voice_packet(sender_id: int, sequence: int, payload: PackedByteArray) -> void:
	if network_manager == null or !network_manager.race_active:
		return
	if !voice_listen_enabled:
		return
	if sender_id == multiplayer.get_unique_id():
		return
	var peer := _ensure_remote_peer(sender_id)
	peer["last_sequence"] = sequence
	peer["last_packet_msec"] = Time.get_ticks_msec()
	var playback := peer.get("playback") as AudioStreamGeneratorPlayback
	var codec := peer.get("codec") as OpusVoiceCodec
	if playback == null or codec == null:
		return
	var decoded := codec.decode(payload)
	if decoded.is_empty():
		return
	var frames := PackedVector2Array()
	frames.resize(decoded.size())
	for i in range(decoded.size()):
		var sample := decoded[i]
		frames[i] = Vector2(sample, sample)
	if playback.get_frames_available() >= frames.size():
		playback.push_buffer(frames)
	else:
		playback.clear_buffer()
		playback.push_buffer(frames)

func _ensure_remote_peer(sender_id: int) -> Dictionary:
	if remote_peers.has(sender_id):
		return remote_peers[sender_id]
	var generator := AudioStreamGenerator.new()
	generator.mix_rate = VOICE_SAMPLE_RATE
	generator.buffer_length = PLAYBACK_BUFFER_SECONDS
	var codec := OpusVoiceCodec.new()
	codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, 24000, 3)
	var player := AudioStreamPlayer3D.new()
	player.name = "VoicePeer%d" % sender_id
	player.stream = generator
	player.max_distance = VOICE_RANGE
	player.unit_size = maxf(VOICE_RANGE * 0.08, 1.0)
	var root: Node = self
	if network_manager != null and network_manager.game_manager != null:
		var game_world := network_manager.game_manager.get_node_or_null("GameWorld")
		if game_world != null:
			root = game_world
	root.add_child(player)
	player.play()
	var peer := {
		"player": player,
		"playback": player.get_stream_playback(),
		"codec": codec,
		"last_sequence": -1,
		"last_packet_msec": Time.get_ticks_msec(),
	}
	remote_peers[sender_id] = peer
	return peer

func _voice_recipients_for(sender_id: int) -> Array:
	if network_manager == null or network_manager.server_game_sim == null:
		return []
	var sim := network_manager.server_game_sim
	if !sim.has_method("get_saved_player_voice_transforms"):
		return []
	var snapshot: Array = sim.get_saved_player_voice_transforms(maxi(network_manager.server_tick - 1, 0))
	if snapshot.is_empty():
		return []
	var source_pos := Vector3.ZERO
	var found_source := false
	for item in snapshot:
		if typeof(item) != TYPE_DICTIONARY:
			continue
		if int(item.get("player_id", -1)) == sender_id:
			var t: Transform3D = item.get("transform", Transform3D.IDENTITY)
			source_pos = t.origin
			found_source = true
			break
	if !found_source:
		return []
	var candidates := []
	var max_dist_sq := VOICE_RANGE * VOICE_RANGE
	for item in snapshot:
		if typeof(item) != TYPE_DICTIONARY:
			continue
		var player_id := int(item.get("player_id", -1))
		if player_id == sender_id or player_id < 0:
			continue
		var t: Transform3D = item.get("transform", Transform3D.IDENTITY)
		var dist_sq := source_pos.distance_squared_to(t.origin)
		if dist_sq <= max_dist_sq:
			candidates.append({"id": player_id, "dist_sq": dist_sq})
	candidates.sort_custom(func(a, b): return float(a["dist_sq"]) < float(b["dist_sq"]))
	var recipients := []
	var count = mini(candidates.size(), VOICE_MAX_RECIPIENTS)
	for i in range(count):
		recipients.append(int(candidates[i]["id"]))
	return recipients

func _update_remote_source_positions() -> void:
	if remote_peers.is_empty() or network_manager == null:
		return
	var sim: GameSim = network_manager.game_sim
	if sim == null and network_manager.listen_server:
		sim = network_manager.server_game_sim
	if sim == null or !sim.has_method("get_saved_player_voice_transforms"):
		return
	var target_tick := _voice_spatial_tick()
	var snapshot: Array = sim.get_saved_player_voice_transforms(target_tick)
	var transforms := {}
	for item in snapshot:
		if typeof(item) == TYPE_DICTIONARY:
			transforms[int(item.get("player_id", -1))] = item.get("transform", Transform3D.IDENTITY)
	var now := Time.get_ticks_msec()
	for peer_id in remote_peers.keys():
		var peer: Dictionary = remote_peers[peer_id]
		var player := peer.get("player") as AudioStreamPlayer3D
		if player == null:
			continue
		if now - int(peer.get("last_packet_msec", now)) > VOICE_PEER_TIMEOUT_MSEC:
			player.queue_free()
			remote_peers.erase(peer_id)
			continue
		if transforms.has(int(peer_id)):
			player.global_transform = transforms[int(peer_id)]

func _voice_spatial_tick() -> int:
	if network_manager == null:
		return 0
	if network_manager.is_server and !network_manager.listen_server:
		return maxi(network_manager.server_tick - 1, 0)
	var runahead := int(round(network_manager.desired_ahead_ticks))
	return maxi(network_manager.local_tick - runahead, 0)
