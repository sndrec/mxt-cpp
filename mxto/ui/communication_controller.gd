class_name CommunicationController
extends Node

const MAX_LOBBY_HISTORY := 128
const MAX_MESSAGE_CHARACTERS := 220
const RATE_WINDOW_MSEC := 5000
const RATE_MAX_MESSAGES := 8
const RATE_STATE_PRUNE_THRESHOLD := 128
const GLOBAL_BURST_MESSAGES := 48.0
const GLOBAL_REFILL_PER_SECOND := 8.0

@onready var lobby_control: Control = $"../Lobby"
@onready var lobby_box: RichTextLabel = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ChatPanel/ChatMargin/ChatBox/LobbyChatBox"
@onready var lobby_input: LineEdit = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ChatPanel/ChatMargin/ChatBox/ChatInputBox/LobbySayText"
@onready var lobby_send_button: Button = $"../Lobby/LobbyStatic/LobbyContainer/BottomBox/ChatPanel/ChatMargin/ChatBox/ChatInputBox/LobbySendTextButton"
@onready var voice_chat: ProximityVoiceChat = $"../NetworkManager/ProximityVoiceChat"
@onready var race_overlay: RaceCommunicationOverlay = $RaceCommunicationOverlay

var game_manager
var network_manager: NetworkManager
var game_sim: GameSim
var replay_controller: ReplayController

var lobby_history: Array[Dictionary] = []
var rate_state := {}
var global_tokens := GLOBAL_BURST_MESSAGES
var global_refill_msec := 0
var lobby_rendered_history_size := 0

func _ready() -> void:
	lobby_input.text_submitted.connect(_on_lobby_text_submitted)
	lobby_input.keep_editing_on_text_submit = true
	lobby_send_button.pressed.connect(_on_lobby_send_pressed)
	lobby_send_button.focus_mode = Control.FOCUS_NONE
	lobby_control.visibility_changed.connect(refresh_lobby)
	race_overlay.message_submitted.connect(submit_message)

func initialize(
	in_game_manager,
	in_network_manager: NetworkManager,
	in_game_sim: GameSim,
	in_replay_controller: ReplayController
) -> void:
	game_manager = in_game_manager
	network_manager = in_network_manager
	game_sim = in_game_sim
	replay_controller = in_replay_controller

func _on_lobby_send_pressed() -> void:
	submit_message(lobby_input.text)
	lobby_input.clear()
	_refocus_lobby_deferred()

func _on_lobby_text_submitted(text: String) -> void:
	submit_message(text)
	lobby_input.clear()
	_refocus_lobby_deferred()

func _refocus_lobby_deferred() -> void:
	_refocus_lobby.call_deferred()

func _refocus_lobby() -> void:
	if !lobby_control.visible:
		return
	lobby_input.grab_focus()
	lobby_input.caret_column = lobby_input.text.length()

func submit_message(text: String) -> void:
	var clean := sanitize_message(text)
	if clean == "":
		return
	if !network_manager.has_network_peer():
		append_message(game_manager._local_player_id(), clean)
	elif network_manager.is_server:
		_server_publish(game_manager._local_player_id(), clean)
	else:
		_send_to_server.rpc_id(1, clean)

@rpc("any_peer", "call_remote", "reliable", 8)
func _send_to_server(text: String) -> void:
	if !network_manager.is_server:
		return
	var clean := sanitize_message(text)
	if clean == "":
		return
	_server_publish(multiplayer.get_remote_sender_id(), clean)

@rpc("authority", "call_local", "reliable", 8)
func _broadcast_message(sender_id: int, text: String) -> void:
	var clean := sanitize_message(text)
	if clean != "":
		append_message(sender_id, clean)

func _server_publish(sender_id: int, text: String) -> void:
	if !network_manager.is_server or !_sender_is_valid(sender_id) or !rate_limit_allows(sender_id):
		return
	_broadcast_message.rpc(sender_id, text)

func _sender_is_valid(sender_id: int) -> bool:
	if sender_id <= 0:
		return false
	if sender_id == game_manager._local_player_id():
		return true
	return network_manager.player_ids.has(sender_id) or network_manager.spectator_ids.has(sender_id) or network_manager.waiting_peers.has(sender_id)

func rate_limit_allows(sender_id: int) -> bool:
	var now_msec := Time.get_ticks_msec()
	if rate_state.size() >= RATE_STATE_PRUNE_THRESHOLD:
		_prune_stale_rate_state(now_msec)
	var state: Dictionary = rate_state.get(sender_id, {})
	var window_start_msec := int(state.get("window_start_msec", now_msec))
	var message_count := int(state.get("message_count", 0))
	if now_msec - window_start_msec >= RATE_WINDOW_MSEC:
		window_start_msec = now_msec
		message_count = 0
	if message_count >= RATE_MAX_MESSAGES:
		return false
	_refill_global_tokens(now_msec)
	if global_tokens < 1.0:
		return false
	global_tokens -= 1.0
	rate_state[sender_id] = {
		"window_start_msec": window_start_msec,
		"message_count": message_count + 1,
	}
	return true

func _refill_global_tokens(now_msec: int) -> void:
	if global_refill_msec <= 0:
		global_refill_msec = now_msec
		return
	var elapsed_msec := now_msec - global_refill_msec
	if elapsed_msec <= 0:
		return
	global_tokens = minf(GLOBAL_BURST_MESSAGES, global_tokens + float(elapsed_msec) * 0.001 * GLOBAL_REFILL_PER_SECOND)
	global_refill_msec = now_msec

func _prune_stale_rate_state(now_msec: int) -> void:
	for sender_id in rate_state.keys():
		var state: Dictionary = rate_state[sender_id]
		if now_msec - int(state.get("window_start_msec", 0)) >= RATE_WINDOW_MSEC * 2:
			rate_state.erase(sender_id)

func sanitize_message(text: String) -> String:
	var clean := text.replace("\r", " ").replace("\n", " ").replace("\t", " ").strip_edges()
	return clean.substr(0, MAX_MESSAGE_CHARACTERS) if clean.length() > MAX_MESSAGE_CHARACTERS else clean

func append_message(sender_id: int, text: String) -> void:
	var name: String = _player_display_name(sender_id)
	var removed_oldest := lobby_history.size() >= MAX_LOBBY_HISTORY
	if removed_oldest:
		lobby_history.remove_at(0)
	var message := {"id": sender_id, "name": name, "text": text}
	lobby_history.append(message)
	if _race_overlay_accepts_messages():
		race_overlay.append_message(sender_id, name, text)
	if lobby_control.visible:
		_append_lobby_box_message(message, removed_oldest)

func refresh_lobby() -> void:
	if !lobby_control.visible:
		return
	lobby_box.clear()
	lobby_box.push_color(Color(0.33, 0.33, 0.33, 1.0))
	lobby_box.add_text("Never tell your password to anyone.")
	lobby_box.pop()
	for message in lobby_history:
		_append_lobby_box_line(message)
	lobby_rendered_history_size = lobby_history.size()

func _append_lobby_box_message(message: Dictionary, removed_oldest: bool) -> void:
	var expected_previous_size := lobby_history.size() if removed_oldest else lobby_history.size() - 1
	if lobby_rendered_history_size != expected_previous_size:
		refresh_lobby()
		return
	if removed_oldest:
		lobby_box.remove_paragraph(1, true)
	_append_lobby_box_line(message)
	lobby_rendered_history_size = lobby_history.size()

func _append_lobby_box_line(message: Dictionary) -> void:
	var sender_id := int(message.get("id", -1))
	var color := Color(1.0, 1.0, 0.4, 1.0) if sender_id == game_manager._local_player_id() else Color(0.78, 0.84, 1.0, 1.0)
	lobby_box.add_text("\n")
	lobby_box.push_color(color)
	lobby_box.add_text(str(message.get("name", str(sender_id))))
	lobby_box.pop()
	lobby_box.add_text(": " + str(message.get("text", "")))

func reset() -> void:
	lobby_history.clear()
	rate_state.clear()
	global_tokens = GLOBAL_BURST_MESSAGES
	global_refill_msec = 0
	lobby_rendered_history_size = 0
	if lobby_control.visible:
		refresh_lobby()
	race_overlay.clear_messages()

func _race_overlay_accepts_messages() -> bool:
	return game_sim != null and game_sim.sim_started and !lobby_control.visible

func is_race_chat_open() -> bool:
	return race_overlay != null and race_overlay.is_chat_open()

func close_race_chat() -> void:
	if race_overlay != null:
		race_overlay.close_chat()

func handle_unhandled_input(event: InputEvent) -> bool:
	if race_overlay == null or !(event is InputEventKey):
		return false
	var key := event as InputEventKey
	if !key.pressed or key.echo:
		return false
	var is_enter := key.keycode == KEY_ENTER or key.keycode == KEY_KP_ENTER
	var is_escape := key.keycode == KEY_ESCAPE
	if race_overlay.is_chat_open():
		if is_enter or is_escape:
			get_viewport().set_input_as_handled()
			return true
		return false
	if !is_enter or !game_sim.sim_started or lobby_control.visible or game_manager.race_pause_open:
		return false
	if game_manager.car_settings.visible or game_manager.options_menu.visible:
		return false
	var window := get_window()
	if window != null and !window.has_focus():
		return false
	race_overlay.open_chat()
	get_viewport().set_input_as_handled()
	return true

func update_race_overlay() -> void:
	if !game_sim.sim_started or replay_controller.replay_playback_active or !network_manager.has_network_peer():
		race_overlay.set_voice_status({"race_active": false}, {})
		return
	var status: Dictionary = voice_chat.get_voice_debug_status()
	var player_names := {}
	var local_id := int(status.get("local_id", game_manager._local_player_id()))
	player_names[local_id] = _player_display_name(local_id)
	var remote_peers: Array = status.get("remote_voice_peers", [])
	for peer_data in remote_peers:
		if typeof(peer_data) != TYPE_DICTIONARY:
			continue
		var peer_id := int(peer_data.get("id", -1))
		if peer_id >= 0:
			player_names[peer_id] = _player_display_name(peer_id)
	race_overlay.set_voice_status(status, player_names)

func _player_display_name(player_id: int) -> String:
	if player_id < 0:
		return "Bumper"
	var player_name := str(player_id)
	var settings = network_manager.player_settings.get(player_id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		player_name = str(settings["username"])
	if network_manager.get_cpu_roster().has(player_id):
		player_name = "[CPU] " + player_name
	return player_name
