extends Node2D

var lastBGStage := 0.0
var clicked := false
@onready var multi_warn := $multi_warn

func _ready() -> void:
	Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	MXGlobal.currentStageOverseer = null
	MXGlobal.currentStage = null
	MXGlobal.current_race_settings = RaceSettings.new()
	MXGlobal.current_multi_lobby = null
	MXGlobal.localPlayer = null
	#MXGlobal.local_settings = null
	MXGlobal.queuedStage = null
	Net.peer = null
	Net.peers.clear()
	Net.peer_map = {}
	Net.connected = false
	Net.players.clear()
	Net.currentlyHosting = false
	Net.id = -1
	Net.nextPlayerID = 0
	multiplayer.multiplayer_peer = null
	get_tree().multiplayer_poll = false
	multiplayer.server_relay = false
	#if MXGlobal.has_raced:
		#multi_warn.visible = true
	for button:Button in [%CarButton,%SettingsButton,%MultiplayerButton,%PlayButton]:
		var originalText := button.text
		button.mouse_entered.connect( func() -> void:
			button.modulate = Color( 1.5, 1.5, 1.5 )
			%OptionText.text = originalText )
		button.mouse_exited.connect( func() -> void:
			button.modulate = Color.WHITE
			%OptionText.text = "" )
		button.text = ""
	if OS.is_debug_build() and MXGlobal.debug_singleplayer:
		MXGlobal.load_stage_by_path( MXGlobal.debug_stage_path, MXGlobal.current_race_settings.serialize() )
	elif OS.is_debug_build() and MXGlobal.debug_multiplayer > 0:
		_on_multiplayer_button_pressed()

func _process(_delta : float) -> void:
	if Input.is_anything_pressed() and !clicked:
		clicked = true
	if MXGlobal.has_raced:
		multi_warn.visible = true

func _physics_process( _delta:float ) -> void:
	for obj in get_tree().get_nodes_in_group("TickableStageObjects"):
		obj.tick()

func _on_play_button_pressed() -> void:
	var stageList := preload("res://core/ui/menus/StageList.tscn").instantiate()
	%MainMenuControl.add_child(stageList)

func _on_multiplayer_button_pressed() -> void:
	get_tree().change_scene_to_packed(preload("res://core/ui/menus/MultiMenu.tscn"))

func _on_settings_button_pressed() -> void:
	pass

func _on_car_button_pressed() -> void:
	var car_menu := preload("res://core/ui/menus/CarMenu.tscn").instantiate()
	%MainMenuControl.add_child(car_menu)
