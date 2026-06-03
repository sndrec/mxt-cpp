class_name ProximityVoiceChat
extends Node

const VOICE_ACTION := "VoiceChat"
const VOICE_TOGGLE_ACTION := "ToggleVoice"
const VOICE_SETTINGS_PATH := "user://voice_chat_settings.json"
const VOICE_MODE_PUSH_TO_TALK := "push_to_talk"
const VOICE_MODE_TOGGLE := "toggle"
const VOICE_MODE_ALWAYS_ON := "always_on"
const VOICE_MODE_OFF := "off"
const VOICECHAT_BUS_NAME := "Voicechat"
const CAPTURE_BUS_NAME := "VoiceCapture"
const CAPTURE_BUS_OUTPUT_MUTE_DB := -80.0
const VOICE_SAMPLE_RATE := 48000
const VOICE_FRAME_SAMPLES := 240
const VOICE_SETTINGS_RELOAD_MSEC := 500
const VOICE_DISTANCE_DELTA_TO_KMH := 216.0
const VOICE_DEBUG_LOG_INTERVAL_MSEC := 1000

@export var voice_bitrate := 8000
@export var voice_codec_complexity := 3
@export var voice_range := 180.0
@export var max_recipients := 6
@export var playback_buffer_seconds := 0.08
@export var remote_peer_timeout_msec := 2000
@export var voice_position_lerp_speed := 18.0
@export var voice_pan_strength := 1.0
@export var voice_attenuation_exponent := 1.0
@export var voice_doppler_enabled := true
@export var voice_doppler_strength := 0.4
@export var voice_doppler_speed_of_sound_kmh := 1235.0
@export var voice_doppler_min_pitch := 0.9
@export var voice_doppler_lerp_speed := 10.0

var network_manager: NetworkManager
var capture_player: AudioStreamPlayer
var capture_effect: AudioEffectCapture
var capture_codec: OpusVoiceCodec
var voice_sequence := 0
var remote_peers := {}
var voice_input_mode := VOICE_MODE_PUSH_TO_TALK
var voice_listen_enabled := true
var voice_test_monitor_enabled := false
var voice_talk_toggled := false
var last_voice_settings_load_msec := -VOICE_SETTINGS_RELOAD_MSEC
var test_monitor_player: AudioStreamPlayer
var test_monitor_playback: AudioStreamGeneratorPlayback
var test_monitor_codec: OpusVoiceCodec
var debug_capture_level := 0.0
var debug_capture_frames_available := 0
var debug_packets_encoded := 0
var debug_packets_received := 0
var debug_last_capture_msec := 0
var debug_last_receive_msec := 0
var debug_capture_status := "idle"
var debug_voice_spatial_tick := -1
var debug_voice_snapshot_count := 0
var debug_voice_position_matches := 0
var debug_voice_listener_matches := 0
var debug_voice_min_doppler_pitch := 1.0
var debug_voice_relay_packets := 0
var debug_voice_relay_local_deliveries := 0
var debug_voice_relay_remote_deliveries := 0
var debug_voice_last_sender_id := -1
var debug_voice_last_recipient_count := 0
var debug_voice_last_recipients := []
var debug_voice_last_recipient_snapshot_count := 0
var debug_voice_last_source_found := false
var debug_voice_last_local_candidate := false
var debug_voice_last_local_distance := -1.0
var debug_voice_receive_attempts := 0
var debug_voice_decode_pushes := 0
var debug_voice_last_receive_sender_id := -1
var debug_voice_last_receive_sequence := -1
var debug_voice_last_drop_reason := ""
var debug_voice_transform_sim_kind := ""
var debug_voice_log_file: FileAccess
var debug_voice_log_path := ""
var debug_voice_log_failed := false
var debug_voice_last_summary_msec := 0
var applied_playback_buffer_seconds := -1.0
var applied_voice_bitrate := -1
var applied_voice_codec_complexity := -1

func _ready() -> void:
	network_manager = get_parent() as NetworkManager
	_ensure_voice_input_action()
	_load_voice_settings()
	applied_playback_buffer_seconds = playback_buffer_seconds
	applied_voice_bitrate = voice_bitrate
	applied_voice_codec_complexity = voice_codec_complexity
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
	voice_talk_toggled = false
	if capture_effect != null:
		capture_effect.clear_buffer()
	_clear_test_monitor()
	_reset_voice_debug_counters()
	_close_voice_debug_log()

func _reset_voice_debug_counters() -> void:
	debug_capture_level = 0.0
	debug_capture_frames_available = 0
	debug_packets_encoded = 0
	debug_packets_received = 0
	debug_last_capture_msec = 0
	debug_last_receive_msec = 0
	debug_voice_spatial_tick = -1
	debug_voice_snapshot_count = 0
	debug_voice_position_matches = 0
	debug_voice_listener_matches = 0
	debug_voice_min_doppler_pitch = 1.0
	debug_voice_relay_packets = 0
	debug_voice_relay_local_deliveries = 0
	debug_voice_relay_remote_deliveries = 0
	debug_voice_last_sender_id = -1
	debug_voice_last_recipient_count = 0
	debug_voice_last_recipients.clear()
	debug_voice_last_recipient_snapshot_count = 0
	debug_voice_last_source_found = false
	debug_voice_last_local_candidate = false
	debug_voice_last_local_distance = -1.0
	debug_voice_receive_attempts = 0
	debug_voice_decode_pushes = 0
	debug_voice_last_receive_sender_id = -1
	debug_voice_last_receive_sequence = -1
	debug_voice_last_drop_reason = ""
	debug_voice_transform_sim_kind = ""
	debug_voice_last_summary_msec = 0

func _ensure_voice_debug_log() -> void:
	if debug_voice_log_file != null or debug_voice_log_failed:
		return
	var dir := DirAccess.open("user://")
	if dir == null or dir.make_dir_recursive("logs") != OK:
		debug_voice_log_failed = true
		print("MXT_VOICE_LOG failed to create user://logs")
		return
	var role := "client"
	if network_manager != null and network_manager.is_server:
		role = "listen" if network_manager.listen_server else "server"
	var uid := multiplayer.get_unique_id()
	var file_name := "logs/voice-" + role + "-" + str(Time.get_unix_time_from_system()) + "-" + str(uid) + ".log"
	debug_voice_log_path = "user://" + file_name
	debug_voice_log_file = FileAccess.open(debug_voice_log_path, FileAccess.WRITE)
	if debug_voice_log_file == null:
		debug_voice_log_failed = true
		print("MXT_VOICE_LOG failed to open ", debug_voice_log_path, " err=", FileAccess.get_open_error())
		return
	debug_voice_log_file.store_line("# MXT voice debug log")
	debug_voice_log_file.store_line("# path=" + ProjectSettings.globalize_path(debug_voice_log_path))
	debug_voice_log_file.store_line("# JSON lines")
	debug_voice_log_file.flush()
	print("MXT_VOICE_LOG writing ", ProjectSettings.globalize_path(debug_voice_log_path))

func _close_voice_debug_log() -> void:
	if debug_voice_log_file != null:
		debug_voice_log_file.flush()
	debug_voice_log_file = null
	debug_voice_log_path = ""
	debug_voice_log_failed = false

func _write_voice_debug_summary() -> void:
	var now := Time.get_ticks_msec()
	if now - debug_voice_last_summary_msec < VOICE_DEBUG_LOG_INTERVAL_MSEC:
		return
	debug_voice_last_summary_msec = now
	_write_voice_debug_line("summary", debug_voice_last_receive_sender_id, debug_voice_last_receive_sequence, true)

func _write_voice_debug_event(event: String, sender_id: int, sequence: int) -> void:
	if event == "relay" and debug_voice_relay_packets % 30 != 1:
		return
	if event == "decode" and debug_voice_decode_pushes % 30 != 1:
		return
	_write_voice_debug_line(event, sender_id, sequence, event != "decode")

func _write_voice_debug_line(event: String, sender_id: int, sequence: int, flush_line: bool) -> void:
	_ensure_voice_debug_log()
	if debug_voice_log_file == null:
		return
	var data := _voice_debug_snapshot(event, sender_id, sequence)
	debug_voice_log_file.store_line(JSON.stringify(data))
	if flush_line:
		debug_voice_log_file.flush()

func _voice_debug_snapshot(event: String, sender_id: int, sequence: int) -> Dictionary:
	var local_id := multiplayer.get_unique_id()
	var is_server := false
	var listen_server := false
	var race_active := false
	var server_tick := -1
	var local_tick := -1
	var desired_ahead := 0.0
	var roster_size := 0
	if network_manager != null:
		is_server = network_manager.is_server
		listen_server = network_manager.listen_server
		race_active = network_manager.race_active
		server_tick = network_manager.server_tick
		local_tick = network_manager.local_tick
		desired_ahead = network_manager.desired_ahead_ticks
		roster_size = network_manager.get_simulation_roster().size()
	return {
		"time_msec": Time.get_ticks_msec(),
		"event": event,
		"uid": local_id,
		"is_server": is_server,
		"listen_server": listen_server,
		"race_active": race_active,
		"server_tick": server_tick,
		"local_tick": local_tick,
		"desired_ahead": desired_ahead,
		"roster_size": roster_size,
		"sender_id": sender_id,
		"sequence": sequence,
		"capture_status": debug_capture_status,
		"capture_level": debug_capture_level,
		"packets_encoded": debug_packets_encoded,
		"packets_received": debug_packets_received,
		"receive_attempts": debug_voice_receive_attempts,
		"decode_pushes": debug_voice_decode_pushes,
		"relay_packets": debug_voice_relay_packets,
		"relay_local_deliveries": debug_voice_relay_local_deliveries,
		"relay_remote_deliveries": debug_voice_relay_remote_deliveries,
		"last_sender_id": debug_voice_last_sender_id,
		"last_recipient_count": debug_voice_last_recipient_count,
		"last_recipients": debug_voice_last_recipients,
		"recipient_snapshot_count": debug_voice_last_recipient_snapshot_count,
		"source_found": debug_voice_last_source_found,
		"local_candidate": debug_voice_last_local_candidate,
		"local_distance": debug_voice_last_local_distance,
		"voice_spatial_tick": debug_voice_spatial_tick,
		"voice_snapshot_count": debug_voice_snapshot_count,
		"voice_position_matches": debug_voice_position_matches,
		"voice_listener_matches": debug_voice_listener_matches,
		"voice_transform_sim": debug_voice_transform_sim_kind,
		"last_receive_sender_id": debug_voice_last_receive_sender_id,
		"last_receive_sequence": debug_voice_last_receive_sequence,
		"drop_reason": debug_voice_last_drop_reason,
		"remote_peer_count": remote_peers.size(),
	}

func _clear_remote_peers() -> void:
	for peer_id in remote_peers.keys():
		var peer: Dictionary = remote_peers[peer_id]
		var player := peer.get("player") as AudioStreamPlayer
		if player != null:
			player.queue_free()
	remote_peers.clear()

func _physics_process(_delta: float) -> void:
	_poll_voice_settings()
	_apply_runtime_tuning()
	if network_manager == null or !network_manager.race_active or !network_manager.has_network_peer():
		if voice_test_monitor_enabled:
			_update_test_monitor_capture()
		else:
			reset()
			debug_capture_status = "idle"
		return
	_update_toggle_voice_state()
	if !voice_test_monitor_enabled and test_monitor_player != null:
		_clear_test_monitor()
	if voice_listen_enabled:
		_update_remote_source_positions()
	elif !remote_peers.is_empty():
		_clear_remote_peers()
	_capture_and_send()
	_write_voice_debug_summary()

func _apply_runtime_tuning() -> void:
	if !is_equal_approx(applied_playback_buffer_seconds, playback_buffer_seconds):
		applied_playback_buffer_seconds = playback_buffer_seconds
		_recreate_voice_playbacks()
	if applied_voice_bitrate != voice_bitrate or applied_voice_codec_complexity != voice_codec_complexity:
		applied_voice_bitrate = voice_bitrate
		applied_voice_codec_complexity = voice_codec_complexity
		_reconfigure_voice_codecs()

func _reconfigure_voice_codecs() -> void:
	if capture_codec != null:
		capture_codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, voice_bitrate, voice_codec_complexity)
	if test_monitor_codec != null:
		test_monitor_codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, voice_bitrate, voice_codec_complexity)
	for peer_id in remote_peers.keys():
		var peer: Dictionary = remote_peers[peer_id]
		var codec := peer.get("codec") as OpusVoiceCodec
		if codec != null:
			codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, voice_bitrate, voice_codec_complexity)

func _recreate_voice_playbacks() -> void:
	if test_monitor_player != null:
		_clear_test_monitor()
	for peer_id in remote_peers.keys():
		var peer: Dictionary = remote_peers[peer_id]
		var old_player := peer.get("player") as AudioStreamPlayer
		if old_player != null:
			old_player.queue_free()
		var generator := AudioStreamGenerator.new()
		generator.mix_rate = VOICE_SAMPLE_RATE
		generator.buffer_length = playback_buffer_seconds
		var player := AudioStreamPlayer.new()
		player.name = "VoicePeer%d" % int(peer_id)
		player.stream = generator
		player.bus = VOICECHAT_BUS_NAME
		add_child(player)
		player.play()
		peer["player"] = player
		peer["playback"] = player.get_stream_playback()
		remote_peers[peer_id] = peer

func _update_test_monitor_capture() -> void:
	if network_manager == null or network_manager.game_manager == null or network_manager.game_manager.headless_mode:
		debug_capture_status = "blocked: headless"
		return
	_capture_voice_frames(false, true)

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
	voice_test_monitor_enabled = bool(data.get("test_monitor_enabled", false))

func _update_toggle_voice_state() -> void:
	if voice_input_mode != VOICE_MODE_TOGGLE:
		voice_talk_toggled = false
		return
	if InputMap.has_action(VOICE_TOGGLE_ACTION) and Input.is_action_just_pressed(VOICE_TOGGLE_ACTION):
		voice_talk_toggled = !voice_talk_toggled

func _capture_and_send() -> void:
	if network_manager == null or network_manager.game_manager == null or network_manager.game_manager.headless_mode:
		debug_capture_status = "blocked: headless"
		return
	var should_send := _local_player_should_send_voice()
	var should_monitor := voice_test_monitor_enabled
	if !should_send and !should_monitor:
		if capture_effect != null:
			capture_effect.clear_buffer()
		debug_capture_status = "idle"
		return
	_capture_voice_frames(should_send, should_monitor)

func _capture_voice_frames(should_send: bool, should_monitor: bool) -> void:
	_ensure_capture()
	if capture_effect == null:
		debug_capture_status = "blocked: no capture effect"
		return
	var available := capture_effect.get_frames_available()
	debug_capture_frames_available = available
	if available <= 0:
		debug_capture_status = "waiting for mic frames"
		return
	var input_frames := capture_effect.get_buffer(available)
	if input_frames.is_empty():
		debug_capture_status = "waiting for mic frames"
		return
	debug_capture_status = "capturing"
	var input_rate := float(AudioServer.get_mix_rate())
	if input_rate <= 0.0:
		input_rate = 48000.0
	var packets: Array = capture_codec.encode_stereo_mix(input_frames, input_rate)
	debug_capture_level = capture_codec.get_last_input_peak()
	debug_last_capture_msec = Time.get_ticks_msec()
	for packet in packets:
		var payload := packet as PackedByteArray
		if payload.is_empty():
			continue
		debug_packets_encoded += 1
		if should_monitor:
			_play_voice_test_payload(payload)
		if should_send:
			_send_voice_payload(payload)

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
	AudioServer.set_bus_volume_db(bus_index, 0.0)
	while AudioServer.get_bus_effect_count(bus_index) > 0:
		AudioServer.remove_bus_effect(bus_index, 0)
	capture_effect = AudioEffectCapture.new()
	capture_effect.buffer_length = 0.08
	AudioServer.add_bus_effect(bus_index, capture_effect, 0)
	var capture_output_mute := AudioEffectAmplify.new()
	capture_output_mute.volume_db = CAPTURE_BUS_OUTPUT_MUTE_DB
	AudioServer.add_bus_effect(bus_index, capture_output_mute, 1)
	capture_codec = OpusVoiceCodec.new()
	capture_codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, voice_bitrate, voice_codec_complexity)
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
	debug_voice_relay_packets += 1
	debug_voice_last_sender_id = sender_id
	debug_voice_last_recipient_count = recipients.size()
	debug_voice_last_recipients = recipients.duplicate()
	_write_voice_debug_event("relay", sender_id, sequence)
	for recipient in recipients:
		if int(recipient) == sender_id:
			continue
		if int(recipient) == local_id:
			debug_voice_relay_local_deliveries += 1
			_receive_voice_packet(sender_id, sequence, payload)
		else:
			if !_can_send_voice_rpc_to(int(recipient)):
				continue
			debug_voice_relay_remote_deliveries += 1
			_receive_voice_packet.rpc_id(int(recipient), sender_id, sequence, payload)

@rpc("authority", "unreliable_ordered", "call_remote", 6)
func _receive_voice_packet(sender_id: int, sequence: int, payload: PackedByteArray) -> void:
	debug_voice_receive_attempts += 1
	debug_voice_last_receive_sender_id = sender_id
	debug_voice_last_receive_sequence = sequence
	if network_manager == null or !network_manager.race_active:
		debug_voice_last_drop_reason = "not racing"
		_write_voice_debug_event("receive_drop", sender_id, sequence)
		return
	if !voice_listen_enabled:
		debug_voice_last_drop_reason = "listen disabled"
		_write_voice_debug_event("receive_drop", sender_id, sequence)
		return
	if sender_id == multiplayer.get_unique_id():
		debug_voice_last_drop_reason = "self packet"
		_write_voice_debug_event("receive_drop", sender_id, sequence)
		return
	debug_packets_received += 1
	debug_last_receive_msec = Time.get_ticks_msec()
	var peer := _ensure_remote_peer(sender_id)
	peer["last_sequence"] = sequence
	peer["last_packet_msec"] = Time.get_ticks_msec()
	if !bool(peer.get("has_voice_transform", false)):
		_update_remote_source_positions()
		if !bool(peer.get("has_voice_transform", false)):
			debug_voice_last_drop_reason = "missing source transform"
			_write_voice_debug_event("receive_drop", sender_id, sequence)
			return
	if !bool(peer.get("has_listener_transform", false)):
		debug_voice_last_drop_reason = "missing listener transform"
		_write_voice_debug_event("receive_drop", sender_id, sequence)
		return
	var playback := peer.get("playback") as AudioStreamGeneratorPlayback
	var codec := peer.get("codec") as OpusVoiceCodec
	if playback == null or codec == null:
		debug_voice_last_drop_reason = "missing playback"
		_write_voice_debug_event("receive_drop", sender_id, sequence)
		return
	var pan_gains := _voice_stereo_gains(
		peer.get("current_origin", Vector3.ZERO),
		peer.get("listener_origin", Vector3.ZERO)
	)
	if codec.decode_push_stereo(payload, playback, pan_gains.x, pan_gains.y):
		debug_voice_decode_pushes += 1
		debug_voice_last_drop_reason = ""
		_write_voice_debug_event("decode", sender_id, sequence)
	else:
		debug_voice_last_drop_reason = "decode failed"
		_write_voice_debug_event("receive_drop", sender_id, sequence)

func _play_voice_test_payload(payload: PackedByteArray) -> void:
	_ensure_test_monitor()
	if test_monitor_playback == null or test_monitor_codec == null:
		return
	test_monitor_codec.decode_push_stereo(payload, test_monitor_playback, 1.0, 1.0)

func _ensure_test_monitor() -> void:
	if test_monitor_player != null:
		return
	var generator := AudioStreamGenerator.new()
	generator.mix_rate = VOICE_SAMPLE_RATE
	generator.buffer_length = playback_buffer_seconds
	test_monitor_codec = OpusVoiceCodec.new()
	test_monitor_codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, voice_bitrate, voice_codec_complexity)
	test_monitor_player = AudioStreamPlayer.new()
	test_monitor_player.name = "VoiceTestMonitor"
	test_monitor_player.stream = generator
	test_monitor_player.bus = VOICECHAT_BUS_NAME
	add_child(test_monitor_player)
	test_monitor_player.play()
	test_monitor_playback = test_monitor_player.get_stream_playback() as AudioStreamGeneratorPlayback

func _clear_test_monitor() -> void:
	if test_monitor_player != null:
		test_monitor_player.queue_free()
	test_monitor_player = null
	test_monitor_playback = null
	test_monitor_codec = null

func _ensure_remote_peer(sender_id: int) -> Dictionary:
	if remote_peers.has(sender_id):
		return remote_peers[sender_id]
	var generator := AudioStreamGenerator.new()
	generator.mix_rate = VOICE_SAMPLE_RATE
	generator.buffer_length = playback_buffer_seconds
	var codec := OpusVoiceCodec.new()
	codec.configure(VOICE_SAMPLE_RATE, VOICE_FRAME_SAMPLES, voice_bitrate, voice_codec_complexity)
	var player := AudioStreamPlayer.new()
	player.name = "VoicePeer%d" % sender_id
	player.stream = generator
	player.bus = VOICECHAT_BUS_NAME
	add_child(player)
	player.play()
	var peer := {
		"player": player,
		"playback": player.get_stream_playback(),
		"codec": codec,
		"last_sequence": -1,
		"last_packet_msec": Time.get_ticks_msec(),
		"has_voice_transform": false,
		"has_listener_transform": false,
		"current_origin": Vector3.ZERO,
		"target_origin": Vector3.ZERO,
		"listener_origin": Vector3.ZERO,
		"has_previous_distance": false,
		"previous_distance": 0.0,
		"doppler_pitch": 1.0,
	}
	remote_peers[sender_id] = peer
	return peer


func _voice_stereo_gains(source_origin: Vector3, listener_origin: Vector3) -> Vector2:
	var camera := get_viewport().get_camera_3d()
	var offset := source_origin - listener_origin
	var distance := offset.length()
	var attenuation := 1.0
	if voice_range > 0.0:
		attenuation = pow(clampf(1.0 - (distance / voice_range), 0.0, 1.0), maxf(voice_attenuation_exponent, 0.01))
	if camera == null or distance <= 0.0001:
		return Vector2(attenuation, attenuation)
	var pan := offset.normalized().dot(camera.global_basis.x)
	pan = clampf(pan * voice_pan_strength, -1.0, 1.0)
	var angle := (pan + 1.0) * PI * 0.25
	return Vector2(cos(angle) * attenuation, sin(angle) * attenuation)

func _voice_recipients_for(sender_id: int) -> Array:
	debug_voice_last_recipient_snapshot_count = 0
	debug_voice_last_source_found = false
	debug_voice_last_local_candidate = false
	debug_voice_last_local_distance = -1.0
	if network_manager == null or network_manager.server_game_sim == null:
		return []
	var sim := network_manager.server_game_sim
	if !sim.has_method("get_saved_player_voice_transforms"):
		return []
	var snapshot: Array = sim.get_saved_player_voice_transforms(maxi(network_manager.server_tick - 1, 0))
	debug_voice_last_recipient_snapshot_count = snapshot.size()
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
	debug_voice_last_source_found = true
	var candidates := []
	var max_dist_sq := voice_range * voice_range
	var local_id := multiplayer.get_unique_id()
	for item in snapshot:
		if typeof(item) != TYPE_DICTIONARY:
			continue
		var player_id := int(item.get("player_id", -1))
		if player_id == sender_id or player_id < 0:
			continue
		if !_can_send_voice_rpc_to(player_id):
			continue
		var t: Transform3D = item.get("transform", Transform3D.IDENTITY)
		var dist_sq := source_pos.distance_squared_to(t.origin)
		if dist_sq <= max_dist_sq:
			candidates.append({"id": player_id, "dist_sq": dist_sq})
			if player_id == local_id:
				debug_voice_last_local_candidate = true
				debug_voice_last_local_distance = sqrt(dist_sq)
	candidates.sort_custom(func(a, b): return float(a["dist_sq"]) < float(b["dist_sq"]))
	var recipients := []
	var count = mini(candidates.size(), max_recipients)
	for i in range(count):
		recipients.append(int(candidates[i]["id"]))
	return recipients

func _can_send_voice_rpc_to(peer_id: int) -> bool:
	if network_manager == null:
		return false
	if network_manager.get_cpu_roster().has(peer_id):
		return false
	if peer_id == multiplayer.get_unique_id():
		return true
	if multiplayer.multiplayer_peer == null:
		return false
	return multiplayer.get_peers().has(peer_id)

func _update_remote_source_positions() -> void:
	if remote_peers.is_empty() or network_manager == null:
		return
	var sim: GameSim = network_manager.game_sim
	var target_tick := _voice_spatial_tick()
	debug_voice_transform_sim_kind = "client"
	if network_manager.listen_server:
		sim = network_manager.server_game_sim
		target_tick = maxi(network_manager.server_tick - 1, 0)
		debug_voice_transform_sim_kind = "server"
	if sim == null or !sim.has_method("get_saved_player_voice_transforms"):
		return
	debug_voice_spatial_tick = target_tick
	var snapshot: Array = sim.get_saved_player_voice_transforms(target_tick)
	debug_voice_snapshot_count = snapshot.size()
	var transforms := {}
	for item in snapshot:
		if typeof(item) == TYPE_DICTIONARY:
			transforms[int(item.get("player_id", -1))] = item.get("transform", Transform3D.IDENTITY)
	var local_id := multiplayer.get_unique_id()
	var listener_has_transform := transforms.has(local_id)
	var listener_origin := Vector3.ZERO
	if listener_has_transform:
		var listener_transform: Transform3D = transforms[local_id]
		listener_origin = listener_transform.origin
	debug_voice_position_matches = 0
	debug_voice_listener_matches = 1 if listener_has_transform else 0
	debug_voice_min_doppler_pitch = 1.0
	var now := Time.get_ticks_msec()
	for peer_id in remote_peers.keys():
		var peer: Dictionary = remote_peers[peer_id]
		var player := peer.get("player") as AudioStreamPlayer
		if player == null:
			continue
		if now - int(peer.get("last_packet_msec", now)) > remote_peer_timeout_msec:
			player.queue_free()
			remote_peers.erase(peer_id)
			continue
		if listener_has_transform and transforms.has(int(peer_id)):
			debug_voice_position_matches += 1
			var t: Transform3D = transforms[int(peer_id)]
			peer["target_origin"] = t.origin
			peer["listener_origin"] = listener_origin
			peer["has_listener_transform"] = true
			if !bool(peer.get("has_voice_transform", false)):
				peer["current_origin"] = t.origin
				peer["has_voice_transform"] = true
			else:
				var alpha := clampf(voice_position_lerp_speed / maxf(Engine.physics_ticks_per_second, 1.0), 0.0, 1.0)
				var current_origin: Vector3 = peer.get("current_origin", t.origin)
				peer["current_origin"] = current_origin.lerp(t.origin, alpha)
			var exact_distance := t.origin.distance_to(listener_origin)
			_update_peer_doppler(peer, player, exact_distance)
			debug_voice_min_doppler_pitch = minf(debug_voice_min_doppler_pitch, float(peer.get("doppler_pitch", 1.0)))
		else:
			peer["has_voice_transform"] = false
			peer["has_listener_transform"] = false
			peer["has_previous_distance"] = false
			peer["doppler_pitch"] = 1.0
			player.pitch_scale = 1.0

func _update_peer_doppler(peer: Dictionary, player: AudioStreamPlayer, distance: float) -> void:
	var target_pitch := 1.0
	if voice_doppler_enabled:
		var min_pitch := clampf(voice_doppler_min_pitch, 0.25, 1.0)
		if bool(peer.get("has_previous_distance", false)):
			var previous_distance := float(peer.get("previous_distance", distance))
			var receding_speed_kmh := maxf((distance - previous_distance) * VOICE_DISTANCE_DELTA_TO_KMH, 0.0)
			if receding_speed_kmh > 0.0:
				var speed_of_sound := maxf(voice_doppler_speed_of_sound_kmh, 1.0)
				var strength := maxf(voice_doppler_strength, 0.0)
				target_pitch = clampf(speed_of_sound / (speed_of_sound + receding_speed_kmh * strength), min_pitch, 1.0)
		peer["has_previous_distance"] = true
		peer["previous_distance"] = distance
	else:
		peer["has_previous_distance"] = false
	var current_pitch := float(peer.get("doppler_pitch", player.pitch_scale))
	var physics_tps := maxf(Engine.physics_ticks_per_second, 1.0)
	var alpha := clampf(voice_doppler_lerp_speed / physics_tps, 0.0, 1.0)
	var pitch := lerpf(current_pitch, target_pitch, alpha)
	peer["doppler_pitch"] = pitch
	player.pitch_scale = pitch

func _voice_spatial_tick() -> int:
	if network_manager == null:
		return 0
	if network_manager.is_server and !network_manager.listen_server:
		return maxi(network_manager.server_tick - 1, 0)
	var runahead := int(round(network_manager.desired_ahead_ticks))
	return maxi(network_manager.local_tick - runahead, 0)

func get_voice_debug_status() -> Dictionary:
	return {
		"mode": voice_input_mode,
		"listen_enabled": voice_listen_enabled,
		"test_monitor_enabled": voice_test_monitor_enabled,
		"capture_active": capture_effect != null and capture_player != null and capture_player.playing,
		"capture_status": debug_capture_status,
		"capture_frames_available": debug_capture_frames_available,
		"capture_level": debug_capture_level,
		"packets_encoded": debug_packets_encoded,
		"packets_received": debug_packets_received,
		"last_capture_msec": debug_last_capture_msec,
		"last_receive_msec": debug_last_receive_msec,
		"voice_spatial_tick": debug_voice_spatial_tick,
		"voice_snapshot_count": debug_voice_snapshot_count,
		"voice_position_matches": debug_voice_position_matches,
		"voice_listener_matches": debug_voice_listener_matches,
		"voice_min_doppler_pitch": debug_voice_min_doppler_pitch,
		"voice_relay_packets": debug_voice_relay_packets,
		"voice_relay_local_deliveries": debug_voice_relay_local_deliveries,
		"voice_relay_remote_deliveries": debug_voice_relay_remote_deliveries,
		"voice_last_sender_id": debug_voice_last_sender_id,
		"voice_last_recipient_count": debug_voice_last_recipient_count,
		"voice_last_recipients": debug_voice_last_recipients,
		"voice_last_recipient_snapshot_count": debug_voice_last_recipient_snapshot_count,
		"voice_last_source_found": debug_voice_last_source_found,
		"voice_last_local_candidate": debug_voice_last_local_candidate,
		"voice_last_local_distance": debug_voice_last_local_distance,
		"voice_receive_attempts": debug_voice_receive_attempts,
		"voice_decode_pushes": debug_voice_decode_pushes,
		"voice_last_receive_sender_id": debug_voice_last_receive_sender_id,
		"voice_last_receive_sequence": debug_voice_last_receive_sequence,
		"voice_last_drop_reason": debug_voice_last_drop_reason,
		"voice_debug_log_path": debug_voice_log_path,
	}
