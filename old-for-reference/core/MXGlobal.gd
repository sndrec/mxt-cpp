@tool

extends Node

# The stage to load for the debug options below
var debug_stage_path := "res://content/base/stage/debug/beginner/debug_track_beginner.tscn"
# Immediately load into single player on the map above
var debug_singleplayer := false
# If non-zero then host and wait for n clients
# If can't host, join as client
var debug_multiplayer := 0
var debug_host_spec := false
var debug_player_spec := 0

const ups_to_kmh := 10.0
const kmh_to_ups := 1.0 / 10.0
var physics_process_this_frame : bool = false
var has_raced : bool = false
var countdownTime := int(Engine.physics_ticks_per_second * 8.0)
var currentStageOverseer : RORGameplayOverseer
var currentStage : RORStage
var current_race_settings := RaceSettings.new()
var current_multi_lobby : MultiLobby
var localPlayer : ROPlayer
var local_settings : PlayerSettings
var queuedStage : PackedScene
var currentlyRollback : bool = false
var tick_delta : float = 1.0 / Engine.physics_ticks_per_second
var curFrameAccumulation : float = 0
var stageList : Dictionary = {}
var cars : Array[CarDefinition] = [
	preload("res://content/base/character/a/car_a_def.tres"),
	preload("res://content/base/character/b/car_b_def.tres"),
	preload("res://content/base/character/c/car_c_def.tres"),
	#preload("res://content/base/character/d/car_d_def.tres"),
	#preload("res://content/base/character/beetle/beetle.tres"),
	preload("res://content/base/character/ryu/car_ryu_def.tres"),
	preload("res://content/base/character/qsg4/car_quickstar_g4.tres"),
	preload("res://content/base/character/md/maximumdragon.tres"),
	preload("res://content/base/character/bfang/car_bigfang.tres"),
	preload("res://content/base/character/bbull/car_blackbull.tres")
]

var car_lookup : Dictionary = {}
var feed : Array[Control] = []

var currentAnnouncer : AnnouncerPack = preload("res://content/base/announcer/mxt_announcer.tres")
var globalAnnouncerMan : AudioStreamPlayer = AudioStreamPlayer.new()

var globalSoundMan : AudioStreamPlayer = AudioStreamPlayer.new()

var globalMusicMan : AudioStreamPlayer = AudioStreamPlayer.new()
var musicManIntro : AudioStream
var musicManLoop : AudioStream

var music_playing : bool = false

var mouse_offset : Vector2 = Vector2.ZERO
var mouse_driving_mode : int = 0

signal race_settings_updated



@rpc("authority", "call_remote", "reliable")
func sync_race_settings(in_data : PackedByteArray) -> void:
	print("SYNCING RACE SETTINGS FOR CLIENT " + str(multiplayer.get_unique_id()))
	MXGlobal.current_race_settings = RaceSettings.deserialize(in_data)
	race_settings_updated.emit()

func stop_music() -> void:
	globalMusicMan.stop()
	music_playing = false

func play_music(inLoop : AudioStream, inIntro : AudioStream = null) -> void:
	musicManIntro = inIntro
	musicManLoop = inLoop
	if (globalMusicMan.stream != musicManIntro && globalMusicMan.stream != musicManLoop) or !music_playing:
		if musicManIntro:
			globalMusicMan.stream = musicManIntro
		else:
			globalMusicMan.stream = musicManLoop
	globalMusicMan.volume_db = -5
	globalMusicMan.play(0.0)
	music_playing = true

func play_announcer_line(inLine : String) -> void:
	if currentlyRollback: return
	globalAnnouncerMan.stream = currentAnnouncer.get(inLine).pick_random()
	globalAnnouncerMan.volume_db = -10
	globalAnnouncerMan.play()

func play_sound(inSound : AudioStream, volume := 0.0, pitch := 1.0, from := 0.0) -> void:
	if currentlyRollback: return
	globalSoundMan.stream = inSound
	globalSoundMan.volume_db = volume
	globalSoundMan.pitch_scale = pitch
	globalSoundMan.play(from)

func play_sound_for_peer(inSound : AudioStream, desired_peer : int = 0, volume := 0.0, pitch := 1.0, from : float = 0.0) -> void:
	if currentlyRollback: return
	if MXGlobal.localPlayer.playerID != desired_peer: return
	globalSoundMan.stream = inSound
	globalSoundMan.volume_db = volume
	globalSoundMan.pitch_scale = pitch
	globalSoundMan.play(from)

func play_sound_from_location(inSound : AudioStream, in_pos : Vector3) -> void:
	var new_sound : AudioStreamPlayer3D = AudioStreamPlayer3D.new()
	new_sound.bus = "SFX"
	add_child(new_sound)
	new_sound.stream = inSound
	new_sound.position = in_pos
	new_sound.connect("finished", new_sound.queue_free)
	new_sound.play(0.0)

func play_sound_from_node(inSound : AudioStream, in_node : Node3D) -> void:
	var new_sound : AudioStreamPlayer3D = AudioStreamPlayer3D.new()
	new_sound.bus = "SFX"
	in_node.add_child(new_sound)
	new_sound.stream = inSound
	new_sound.connect("finished", new_sound.queue_free)
	new_sound.play(0.0)

func _unhandled_input( event:InputEvent ) -> void:
	if event is InputEventMouseMotion and mouse_driving_mode != 0:
		var mult := 0.005
		if mouse_driving_mode == 2:
			mult = 0.05
		mouse_offset += event.relative * -mult
		mouse_offset.x = clampf(mouse_offset.x, -1, 1)

func _process( delta:float ) -> void:
	if Engine.is_editor_hint():
		return
	if Input.is_action_just_pressed("ResetDebug"):
		Debug.reset()
	if Input.is_action_just_pressed("ToggleVSync"):
		if DisplayServer.window_get_vsync_mode( get_window().get_window_id() ) == DisplayServer.VSYNC_ENABLED:
			DisplayServer.window_set_vsync_mode( DisplayServer.VSYNC_DISABLED, get_window().get_window_id() )
		else:
			DisplayServer.window_set_vsync_mode( DisplayServer.VSYNC_ENABLED, get_window().get_window_id() )
			print(DisplayServer.window_get_vsync_mode( get_window().get_window_id() ))
	if !globalMusicMan or globalMusicMan == null:
		return
	if !currentStage or currentStage == null:
		return
	if !is_instance_valid(get_viewport()):
		return
	if !is_instance_valid(get_viewport().get_camera_3d()):
		return
	RenderingServer.global_shader_parameter_set("cam_pos", get_viewport().get_camera_3d().global_position)
	if !physics_process_this_frame:
		curFrameAccumulation = curFrameAccumulation + delta
	
	physics_process_this_frame = false
	if Input.is_action_just_pressed("ResetSteering") or mouse_driving_mode == 0:
		mouse_offset = Vector2.ZERO
	if Input.is_action_just_pressed("ToggleMouseDriving"):
		mouse_driving_mode += 1
		if mouse_driving_mode > 2:
			mouse_driving_mode = 0
		if mouse_driving_mode > 0:
			Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
		else:
			Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	if music_playing == false:
		return
	
	if !globalMusicMan.playing:
		globalMusicMan.stream = musicManLoop
		globalMusicMan.play(0.0)
	
	if globalMusicMan.stream != musicManIntro && globalMusicMan.stream != musicManLoop:
		if musicManIntro:
			globalMusicMan.stream = musicManIntro
		else:
			globalMusicMan.stream = musicManLoop
		globalMusicMan.volume_db = -5
		globalMusicMan.play(0.0)
	
	for i in range(feed.size()-1, -1, -1):
		if !is_instance_valid(feed[i]):
			feed.remove_at(i)
		
	var height := 48.0
	
	for control:Control in feed:
		control.position.y = height + 32
		height += control.size.y + 4

func _physics_process( _delta:float ) -> void:
	curFrameAccumulation = 0
	physics_process_this_frame = true

func add_control_to_feed(in_control : Control) -> void:
	feed.append(in_control)
	add_child(in_control)

@rpc("authority", "call_local", "reliable")
func load_stage_by_path( inPath:String, _race_settings:PackedByteArray ) -> void:
	queuedStage = stageList[inPath]
	ScreenOverlayHandler.set_color_overlay(Color(0, 0, 0), 1000, 500, 0)
	await get_tree().create_timer(1.0).timeout
	if currentStageOverseer and is_instance_valid(currentStageOverseer):
		for player in currentStageOverseer.players:
			player.queue_free()
	for peer in Net.peers:
		for ch in peer.get_children():
			ch.queue_free()
	ScreenOverlayHandler.set_color_overlay(Color(0, 0, 0), 0, 2000, 1000)
	get_tree().change_scene_to_file("res://core/BallRollingScene.tscn")

func load_stage(inStage : PackedScene) -> void:
	queuedStage = inStage
	get_tree().change_scene_to_file("res://core/BallRollingScene.tscn")

func update_tick_rate(inTickRate : int) -> void:
	Engine.physics_ticks_per_second = inTickRate
	tick_delta = 1.0 / Engine.physics_ticks_per_second

func time_to_ticks(inTime : float) -> int:
	return roundi(Engine.physics_ticks_per_second * inTime)

func dir_contents(path : String, fileArray : Array[String]) -> Array[String]:
	var dir := DirAccess.open(path)
	if dir:
		dir.list_dir_begin()
		var file_name := dir.get_next()
		while file_name != "":
			if dir.current_is_dir():
				dir_contents(path + "/" + file_name, fileArray)
			else:
				if file_name.ends_with(".tscn") or file_name.ends_with(".scn"):
					fileArray.append(path + "/" + file_name)
			file_name = dir.get_next()
	else:
		print("An error occurred when trying to access the path.")
	return fileArray

func index_stages() -> void:
	var stageSceneArray := dir_contents("res://content/base/stage", [])
	for sceneStr:String in stageSceneArray:
		var sceneRef : PackedScene = load(sceneStr)
		var sceneState : SceneState = sceneRef.get_state()
		#var sceneName : String = "Stage"
		if sceneState.get_node_name(0) == "ROStage":
			stageList[sceneStr] = sceneRef
	for scene:String in stageList:
		var loadedScene:Node = stageList[scene].instantiate()
		add_child(loadedScene)
		loadedScene.queue_free()

func _ready() -> void:
	if Engine.is_editor_hint():
		return
	process_priority = 1000
	index_stages()
	for i in range(cars.size()):
		car_lookup[cars[i].ref_name] = i
	globalAnnouncerMan.bus = "Voice"
	globalSoundMan.bus = "SFX"
	globalMusicMan.bus = "Music"
	local_settings = PlayerSettings._load_settings()
	add_child(globalAnnouncerMan)
	add_child(globalSoundMan)
	add_child(globalMusicMan)
