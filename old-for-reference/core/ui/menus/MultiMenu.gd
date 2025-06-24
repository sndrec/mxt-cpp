class_name MultiLobby extends Node

# TODO handle invalid port

@onready var connect_host_box := $ConnectHostBox
@onready var lobby_container := $LobbyContainer
@onready var sub_viewport := $SubViewport
@onready var camera_3d := $SubViewport/Camera3D

@onready var stage_list_container := $LobbyContainer/Container/HBoxContainer/VBoxContainer/ScrollContainer/StageListContainer
@onready var stage_preview_container := $LobbyContainer/Container/HBoxContainer/VBoxContainer/ScrollContainer2/StagePreviewContainer

@onready var gamemode_choice := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/ScrollContainer/LobbyOptionsContainer/GamemodeChoice as OptionButton
@onready var input_delay := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/ScrollContainer/LobbyOptionsContainer/InputDelay as SpinBox
@onready var lap_count := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/ScrollContainer/LobbyOptionsContainer/LapCount as SpinBox
@onready var vehicle_restore := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/ScrollContainer/LobbyOptionsContainer/VehicleRestore as CheckBox
@onready var recharge_lane := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/ScrollContainer/LobbyOptionsContainer/RechargeLane as CheckBox
@onready var bumpers := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/ScrollContainer/LobbyOptionsContainer/Bumpers as CheckBox
@onready var play_button := $LobbyContainer/Container/HBoxContainer/VBoxContainer2/PlayButton as Button


var chibi_cars : Array[ChibiCar] = []
var peers_with_cars : Dictionary = {}
var lobby_shown : bool = false
#func _ready() -> void:

func _on_host_pressed() -> void:
	Net.host_server(int(%Port.text))
	return

func _on_connect_pressed() -> void:
	var txt : String = %Address.text
	if txt == "":
		OS.alert("Need a remote to connect to.")
		print("Need a remote to connect to.")
		return
	Net.join_server(txt, int(%Port.text))


func _on_address_text_submitted( new_text:String ) -> void:
	var txt := new_text
	if txt == "":
		OS.alert("Need a remote to connect to.")
		print("Need a remote to connect to.")
		return
	Net.join_server(txt, int(%Port.text))

func _ready() -> void:
	var new_settings := RaceSettings.new()
	MXGlobal.current_race_settings = new_settings
	MXGlobal.current_multi_lobby = self
	lobby_container.visible = false
	connect_host_box.visible = true
	multiplayer.server_disconnected.connect(return_to_menu)
	multiplayer.peer_disconnected.connect(handle_peer_disconnect)
	MXGlobal.race_settings_updated.connect(update_buttons)
	MXGlobal.race_settings_updated.connect(refresh_stage_list_preview)
	
	gamemode_choice.disabled = true
	input_delay.editable = false
	lap_count.editable = false
	vehicle_restore.disabled = true
	recharge_lane.disabled = true
	bumpers.disabled = true
	play_button.disabled = true
	
	for stagePath:String in MXGlobal.stageList:
		var sceneState : SceneState = MXGlobal.stageList[stagePath].get_state()
		var new_button : MPStageButton = MPStageButton.new()
		new_button.associated_stage = stagePath
		for i in sceneState.get_node_property_count(0):
			if sceneState.get_node_property_name(0, i) == "stageName":
				new_button.text = sceneState.get_node_property_value(0, i)
		stage_list_container.add_child(new_button)

func update_buttons() -> void:
	var updated_race_settings := MXGlobal.current_race_settings as RaceSettings
	gamemode_choice.selected = updated_race_settings.gamemode_type
	input_delay.value = updated_race_settings.input_delay
	lap_count.value = updated_race_settings.laps
	vehicle_restore.button_pressed = updated_race_settings.restore
	recharge_lane.button_pressed = updated_race_settings.recharge_on
	bumpers.button_pressed = updated_race_settings.bumpers

func refresh_stage_list_preview() -> void:
	for child in stage_preview_container.get_children():
		child.queue_free()
	for st in MXGlobal.current_race_settings.tracks.size():
		var sceneState : SceneState = MXGlobal.stageList[MXGlobal.current_race_settings.tracks[st]].get_state()
		var new_button : MPStageListPreviewEntry = MPStageListPreviewEntry.new()
		for i in sceneState.get_node_property_count(0):
			if sceneState.get_node_property_name(0, i) == "stageName":
				new_button.text = sceneState.get_node_property_value(0, i)
		stage_preview_container.add_child(new_button)

func return_to_menu() -> void:
	Net.close()
	Net._disconnected_from_server()
	Net.currentlyHosting = false
	get_tree().change_scene_to_file("res://core/ui/menus/MainMenu.tscn")

var last_playerlist_update : int = 0
@onready var player_list := $LobbyContainer/Container/HBoxContainer/ScrollContainer/PlayerList

func handle_peer_disconnect(in_peer : int) -> void:
	for child in sub_viewport.get_children():
		if child is ChibiCar:
			if !is_instance_valid(child.controlling_peer):
				child._delete_car.rpc()
			elif child.controlling_peer.id == in_peer:
				child._delete_car.rpc()
	for child:MultiPlayerListButton in player_list.get_children():
		if !is_instance_valid(child.associated_peer):
			child._delete_button.rpc()
		elif child.associated_peer.id == in_peer:
			child._delete_button.rpc()

func create_peer_lobby_nodes(in_peer : PeerData) -> void:
	var new_chibi_car : ChibiCar = preload("res://core/car/chibi_car.tscn").instantiate()
	new_chibi_car.set_multiplayer_authority(in_peer.id)
	new_chibi_car.name = "ChibiCar" + str(in_peer.id)
	new_chibi_car.lobby_camera = camera_3d
	new_chibi_car.controlling_peer = in_peer
	sub_viewport.add_child(new_chibi_car)
	var new_player_button := MultiPlayerListButton.new()
	new_player_button.associated_peer = in_peer
	new_player_button.name = "PlayerButton" + str(in_peer.id)
	player_list.add_child(new_player_button)
	peers_with_cars[in_peer.id] = true

func _process( _delta:float ) -> void:
	if Input.is_action_just_pressed("Pause"):
		return_to_menu()
		return
	
	if Time.get_ticks_msec() < last_playerlist_update + 100:
		return
	last_playerlist_update = Time.get_ticks_msec()
	for peer in Net.peers:
		if !peers_with_cars.has(peer.id):
			create_peer_lobby_nodes(peer)
	if (Net.connected or Net.currentlyHosting) and !lobby_shown:
		lobby_shown = true
		ScreenOverlayHandler.set_color_overlay(Color(0, 0, 0), 400, 500, 400)
		await get_tree().create_timer(0.5).timeout
		lobby_container.visible = true
		connect_host_box.visible = false
	if Net.currentlyHosting:
		gamemode_choice.disabled = false
		input_delay.editable = true
		lap_count.editable = true
		vehicle_restore.disabled = false
		recharge_lane.disabled = false
		bumpers.disabled = false
		if MXGlobal.current_race_settings.tracks.size() > 0:
			play_button.disabled = false
		else:
			play_button.disabled = true
	#print(multiplayer.get_peers().size())
	

#var gamemode_type : gamemodes = gamemodes.SINGLE
#var tracks : Array[String] = []
#var input_delay : int = 2
#var laps : int = 3
#var restore : bool = false
#var bumpers : bool = false
#var recharge_on : bool = true


func _on_gamemode_choice_item_selected( index:int ) -> void:
	if Net.currentlyHosting:
		MXGlobal.current_race_settings.gamemode_type = index as RaceSettings.GameMode
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())

func _on_input_delay_value_changed( value:int ) -> void:
	if Net.currentlyHosting:
		MXGlobal.current_race_settings.input_delay = value
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())

func _on_lap_count_value_changed( value:int ) -> void:
	if Net.currentlyHosting:
		MXGlobal.current_race_settings.laps = value
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())


func _on_vehicle_restore_toggled( toggled_on:bool ) -> void:
	if Net.currentlyHosting:
		MXGlobal.current_race_settings.restore = toggled_on
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())


func _on_recharge_lane_toggled( toggled_on:bool ) -> void:
	if Net.currentlyHosting:
		MXGlobal.current_race_settings.recharge_on = toggled_on
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())


func _on_bumpers_toggled( toggled_on:bool ) -> void:
	if Net.currentlyHosting:
		MXGlobal.current_race_settings.bumpers = toggled_on
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())

var race_starting : bool = false

func _on_play_button_pressed() -> void:
	race_starting = true
	RaceSession.point_totals.clear()
	for p in Net.peers:
		if p.team == 0:
			RaceSession.point_totals.append(0)
	RaceSession.current_track = 0
	RaceSession.sync_race_session.rpc(RaceSession.serialize())
	MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())
	await get_tree().create_timer(1.5).timeout
	MXGlobal.load_stage_by_path.rpc(MXGlobal.current_race_settings.tracks[0], MXGlobal.current_race_settings.serialize())

@onready var say_text := $LobbyContainer/HBoxContainer/Panel/MarginContainer/VBoxContainer/VSeparator/SayText

func _on_say_text_send_pressed() -> void:
	_send_chat_message_to_server.rpc_id(1, say_text.text)
	say_text.clear()


func _on_say_text_text_submitted( new_text:String ) -> void:
	_send_chat_message_to_server.rpc_id(1, new_text)
	say_text.clear()

@onready var chat_box := $LobbyContainer/HBoxContainer/Panel/MarginContainer/VBoxContainer/Control/ChatBox
var chat_lines := 0

@rpc("any_peer", "call_local", "reliable")
func _send_chat_message_to_server(in_text : String) -> void:
	_send_chat_message_to_clients.rpc(in_text, multiplayer.get_remote_sender_id())

@rpc("any_peer", "call_local", "reliable")
func _send_chat_message_to_clients(in_text : String, sender_id : int) -> void:
	var sending_peer_data : PeerData
	for peer in Net.peers:
		if peer.id == sender_id:
			sending_peer_data = peer
	if !sending_peer_data: return
	var username : String = sending_peer_data.player_settings.username
	var use_color : String = "AAAAAA"
	if sender_id == multiplayer.get_unique_id():
		use_color = "FFFF00"
	chat_box.append_text("\n[color=" + use_color + "]" + username + "[/color]: " + in_text)
	chat_lines += 1

@onready var texture_rect := $LobbyContainer/HBoxContainer/TextureRect

func _on_texture_rect_gui_input(event:InputEvent) -> void:
	if event is InputEventMouseButton:
		say_text.release_focus()


func _on_container_gui_input(event:InputEvent) -> void:
	if event is InputEventMouseButton:
		say_text.release_focus()
