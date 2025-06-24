class_name RORGameplayOverseer extends Node

class Spawn extends RefCounted:
	var position:Vector3
	var rotation:float
	
	func _init(p_position:Vector3, p_rotation:float) -> void:
		position = p_position
		rotation = p_rotation

var level:PackedScene
var loadedLevel:RORStage
var spawns:Array[Spawn] = []
var players:Array[ROPlayer] = []

func dprint( text:String ) -> void:
	if not multiplayer.is_server(): return
	print("%d @ %d: %s"% [Net.id,Time.get_ticks_usec(),text])

# If we are post tick 9 and receive a client's input for tick 5 we need to rollback to
# tick 4 and resimulate ticks from 5 to 9 so that we end on frame 10 for the start of the next
# frame

# The state we're going to send clients to force sync despite being non deterministic
var syncState := {}
# The tick we saved syncState on
var syncTick := -1
# The current tick of the simulation (this changes when rolling back)
var localTick := 0
# The tick that we will overwrite rollbackState on
# It is the most recent tick in which all the players are correct according to the simulation
# This could be because we simulated all their inputs or the received a state from the host
var rollbackTick := -1
# The default state that rollback will load if not passed a custom one
var rollbackState := {}
# True when the server is currently in a server tick
var currentlyServer := false
var pingInterval := 100_000
var lastPing := Time.get_ticks_usec()

# Tracks how many ticks we've simulated in rollback
var totalRollbackTicks := 0
# The difference between our localTick and the server's
var serverDesync := 0
# Clients will speed up or slow down based on their desync with the server
# If this is disabled, bad client performance will cause severe slowdown as the host will
# perform excessive rollbacks!
var handleDesync := true
# How often the server should send sync requests
var syncInterval := 1_000_000
# The last time we synced in usec (used to apply sync interval)
var lastSync := 0
# How often clients should test for desync
var desyncInterval := 250_000
# The last time we asked clients to check their desync in usec (used to send desync checks)
var lastDesyncCheck := 0
# How much clients try to keep ahead of the host
# This can functionally reduce host delay by making clients run ahead of the host so the
# host receives their inputs for frames in the future
var desyncTarget := 0
# The host will periodically send new desync targets based on player ping with the goal
# to create a similar delay for all participants
var automaticDesyncAdjustment := true
# The easing factor of the lerp the drives averageDesync
# 1 is no averaging, 0 is disabled, default is 0.1
# Higher values result in longer stalls or speedups to sync with the host more aggressively
# which fixes the desync sooner but results in stuttery gameplay
var automaticDesyncStrength := 0.2
# The percentage of target desync clients will aim for between none and their ping's worth of ticks
# Lower means more delay between clients receiving each others inputs
var desyncTargetLerp := 0.5
# Used for automaticDesyncAdjustment so the host doesn't need to send desync packets which may
# be out of date
var averageDesync := 0.0
# The average difference between rollbackTick and localTick which is approximately how many frames
# offset from the host we are
var averageTickDelay := 0.0
# If false clients will assume their packets arrive which will means insignificantly less bandwidth
# usage but will result in increased instability when packets drop (exacerbated on wifi and higher
# player counts)
var confirmEveryInput := true
# How many frames ahead of our localTick we will store our inputs, functionally adding an input
# delay while decreasing the distance into the future we have to predict inputs
var inputDelay := 2
# Might want this to be true so people have a similar experience online and offline
var useInputDelayInSinglePlayer := true


var paused := false
var queueReload := false
var stageLoaded := false
var all_finished := false
var change_level := 0
var RNG := RandomNumberGenerator.new()

var stageObjs : Array = []
var tickableStageObjs : Array = []
var boosters : Array = []
var gameState : Array = []

var places : Array[ROPlayer] = []

signal stageLoadedSignal

func _ready() -> void:
	if not Net.connected:
		load_stage( false, randi() )
		return
	if multiplayer and multiplayer.is_server():
		multiplayer.peer_disconnected.connect(_peer_disconnected)
		Net.peer.change_status.rpc( PeerData.Status.Ready )
		while(true):
			var allReady := true
			for peer in Net.peers:
				if not peer.status == peer.Status.Ready:
					allReady = false
					break
			if allReady:
				break
			await get_tree().create_timer( 0.1 ).timeout
		load_stage.rpc( true, randi() )
	else:
		while true:
			if not stageLoaded:
				Net.peer.change_status.rpc_id( 1, PeerData.Status.Ready )
				await get_tree().create_timer( 1.0 ).timeout
			else:
				break

# TODO figure out why the host cares if the other clients don't destroy the cars for disconnected
# players (throws missing node errors) since in theory this rpc could fail to deliver
func _peer_disconnected( peerID:int ) -> void:
	var player : ROPlayer = get_node("Player" + str(peerID))
	if is_instance_valid(player):
		destroy_node.rpc( player.get_path() )
		places.remove_at( places.find(player) )

@rpc("any_peer", "call_local", "reliable")
func destroy_node( nodePath:NodePath ) -> void:
	var node := get_node(nodePath)
	if node == null: return
	if node in gameState:
		gameState.erase(node)
	if node is ROPlayer:
		gameState.erase(node.controlledPawn)
		node.controlledPawn.queue_free()
		players.erase(node)
	node.queue_free()

func save_state() -> Dictionary:
	#var t := Time.get_ticks_usec()
	var dictionary := {}
	dictionary["rng_seed"] = RNG.seed
	for stateHaver:Variant in gameState:
		var stateBytes : PackedByteArray = stateHaver.save_state()
		dictionary[str(stateHaver.get_path())] = stateBytes
	#Debug.record("save_state",Time.get_ticks_usec()-t)
	return dictionary

func load_state( stateData:Dictionary ) -> Dictionary:
	RNG.seed = stateData["rng_seed"]
	for stateHaver:Variant in gameState:
		var path := str(stateHaver.get_path())
		if stateData.has(path):
			stateHaver.load_state(stateData[path])
	return stateData

@rpc("any_peer", "call_local", "reliable")
func exit_ball_rolling_scene() -> void:
	#if Net.connected:
		#multiplayer.multiplayer_peer.close()
	MXGlobal.currentStageOverseer = null
	MXGlobal.currentStage = null
	MXGlobal.localPlayer = null
	MXGlobal.queuedStage = null
	MXGlobal.currentlyRollback = false
	#MXGlobal.playerData = {}
	MXGlobal.stop_music()
	get_tree().change_scene_to_file("res://core/ui/menus/MainMenu.tscn")

var wants_final_lap : bool = false
var final_lap_music_playing : bool = false
var final_lap_start_time := 0.0

func _process(_delta : float) -> void:
	if not stageLoaded: return
	if Input.is_action_just_pressed("Pause") and !Net.connected:
		return_to_menu()
	if MXGlobal.localPlayer and is_instance_valid(MXGlobal.localPlayer):
		if MXGlobal.localPlayer.controlledPawn.lap == MXGlobal.current_race_settings.laps and !wants_final_lap:
			wants_final_lap = true
			final_lap_start_time = MXGlobal.globalMusicMan.get_playback_position()
		if MXGlobal.currentStage.stage_music and wants_final_lap and !final_lap_music_playing:
			for p in MXGlobal.currentStage.stage_music.finalLapTimeStamps:
				if (p > final_lap_start_time and MXGlobal.globalMusicMan.get_playback_position() > p) or (p == 0.0 and MXGlobal.globalMusicMan.get_playback_position() < 0.1):
					MXGlobal.play_music(loadedLevel.stage_music.musicLoopFinalLap, loadedLevel.stage_music.musicIntroFinalLap)
					final_lap_music_playing = true
					break
	#if Net.connected:
		#if multiplayer and multiplayer.is_server():
			#var l := localTick
			#for pl in players:
				#if pl == MXGlobal.localPlayer: continue
				#l = mini( l, pl.commonCheckpoint )
				#Debug.print(pl.name+" ic "+str(localTick - (pl.commonCheckpoint)) \
				#+ " ping " + ("%dms" % (pl.get_parent().ping / 1000.0)) )
		#Debug.print("Rollback Tick: "+str(rollbackTick)+" total "+str(totalRollbackTicks))
		#if multiplayer and !multiplayer.is_server():
			#Debug.print("Desync: "+str(serverDesync) + " Delay: %.1f" % averageTickDelay + " Desync Correction: "+str(desyncTarget))
		#else:
			#Debug.print("Delay: %.1f" % averageTickDelay)
		#if MXGlobal.localPlayer:
			#Debug.print("ID: "+str(MXGlobal.localPlayer.name))

func create_player(peerID : int, spawnPos : Vector3, spawnDir : float) -> void:
	var owning_peer : PeerData
	if multiplayer and multiplayer.get_peers().size() > 0:
		owning_peer = Net.peer_map[peerID] as PeerData
	var newPlayer := preload("res://core/player/PlayerControllerDefault.tscn")
	var loadedPlayer := newPlayer.instantiate()
	loadedPlayer.name = "Player" + str(peerID)
	loadedPlayer.overseer = self
	loadedPlayer.set_multiplayer_authority(peerID)
	loadedPlayer.playerID = peerID
	if not Net.connected or peerID == multiplayer.get_unique_id():
		MXGlobal.localPlayer = loadedPlayer
	var newCar := preload("res://core/car/car_base.tscn")
	var loadedCar := newCar.instantiate() as MXRacer
	loadedCar.set_multiplayer_authority(peerID)
	loadedCar.position_current = spawnPos
	loadedCar.basis_physical = Basis.IDENTITY.rotated(Vector3.UP, spawnDir)
	print("setting nametag for player " + str(peerID))
	if owning_peer:
		print("username will be " + owning_peer.player_settings.username)
		loadedCar.car_definition = MXGlobal.cars[MXGlobal.car_lookup[owning_peer.player_settings.car_choice]]
		loadedCar.accel_setting = owning_peer.player_settings.accel_setting
	else:
		loadedCar.car_definition = MXGlobal.cars[MXGlobal.car_lookup[MXGlobal.local_settings.car_choice]]
		loadedCar.accel_setting = MXGlobal.local_settings.accel_setting
	loadedPlayer.add_child(loadedCar)
	if owning_peer:
		owning_peer.add_child(loadedPlayer)
		#loadedCar.set_nametag(owning_peer.player_settings.username)
	else:
		add_child(loadedPlayer)
	loadedCar.add_to_group("Gamestate")
	loadedLevel._on_ball_spawn(loadedCar, peerID)
	
	#if multiplayer and multiplayer.get_peers().size() > 0:
		#if peerID == multiplayer.get_unique_id():
			#loadedCar.get_node("car_visual").get_node("NametagBackground").visible = false
	#else:
		#loadedCar.get_node("car_visual").get_node("NametagBackground").queue_free()
	
	loadedPlayer.controlledPawn = loadedCar
	players.append(loadedPlayer)
	loadedCar.pawnID = players.size() - 1

@rpc("authority", "call_local", "reliable")
func return_to_lobby() -> void:
	for peer in Net.peers:
		for child in peer.get_children():
			child.queue_free()
	MXGlobal.stop_music()
	get_tree().change_scene_to_file("res://core/ui/menus/MultiMenu.tscn")

@rpc("authority", "call_local", "reliable")
func return_to_menu() -> void:
	for peer in Net.peers:
		for child in peer.get_children():
			child.queue_free()
	MXGlobal.stop_music()
	get_tree().change_scene_to_file("res://core/ui/menus/MainMenu.tscn")

# When all peers are ready the server will call this on all peers and itself
@rpc("authority", "call_local", "reliable")
func load_stage(_retry: bool, inSeed: int) -> void:
	stageObjs.clear()
	gameState.clear()
	rollbackState.clear()
	players.clear()
	spawns.clear()
	places.clear()
	MXGlobal.localPlayer = null
	RNG.seed = inSeed
	queueReload = false
	currentlyServer = false
	localTick = 0
	lastSync = Time.get_ticks_usec()
	lastDesyncCheck = Time.get_ticks_usec()
	MXGlobal.currentStageOverseer = self
	if MXGlobal.queuedStage:
		level = MXGlobal.queuedStage
	else:
		get_tree().change_scene_to_file("res://core/ui/menus/MainMenu.tscn")
	for obj in get_children():
		obj.free()
		
	MXGlobal.has_raced = true
	
	loadedLevel = level.instantiate()
	add_child(loadedLevel)
	MXGlobal.currentStage = loadedLevel
	
	for obj in get_tree().get_nodes_in_group("StageObjects"):
		obj.add_to_group("Gamestate")
		stageObjs.append(obj)
		if obj is TickableRORStageObject:
			tickableStageObjs.append(obj)
	
	for obj in get_tree().get_nodes_in_group("StageEntities"):
		obj.add_to_group("Gamestate")
		if obj is BoostPad:
			boosters.append(obj)
		stageObjs.append(obj)
	
	for obj in get_tree().get_nodes_in_group("SpawnPoints"):
		spawns.append(Spawn.new(obj.global_position, obj.global_rotation.y))
		obj.visible = false
		obj.queue_free()
	
	
	var peers:Array[PeerData] = Net.peers.duplicate()
	for i in range(peers.size()-1, -1, -1):
		var this_peer := peers[i]
		if this_peer.team == 1:
			var new_spectator := Spectator.new()
			new_spectator.set_multiplayer_authority(this_peer.id)
			new_spectator.name = "Spectator" + str(this_peer.id)
			this_peer.add_child(new_spectator)
			peers.remove_at(i)
	# TODO positions of players should be randomized
	if peers.is_empty():
		create_player( 1, spawns[0].position, spawns[0].rotation )
	else:
		print("creating ordered peer list")
		print("old list...")
		print(Net.peers)
				
		peers.sort_custom( func(a:PeerData, b:PeerData) -> bool:
			var point_index_of_a := RaceSession.peer_ids.find(a.id)
			var point_index_of_b := RaceSession.peer_ids.find(b.id)
			if point_index_of_a == -1 or point_index_of_b == -1:
				return false
			return RaceSession.point_totals[point_index_of_a] < RaceSession.point_totals[point_index_of_b]
			)
		if RaceSession.current_track == 0:
			print("first race - shuffling peer list")
			seed(inSeed)
			peers.shuffle()
		print("new list...")
		print(peers)
		for i in peers.size():
			var peerID := peers[i].id
			#var playerID := peers[i].playerID
			if spawns.size() == 1:
				var dir := spawns[0].rotation
				var sideways := Basis.IDENTITY.rotated(Vector3.UP, dir).x
				var offset := -sideways * 0.6 * (peers.size() - 1) + sideways * 1.2 * i
				create_player(peerID, spawns[0].position + offset, dir)
			else:
				var desired_spawn := spawns[i]
				var dir :=  desired_spawn.rotation
				create_player(peerID,  desired_spawn.position, dir)
	
	for obj in get_tree().get_nodes_in_group("Gamestate"):
		obj._post_stage_loaded()
		
	for obj in get_tree().get_nodes_in_group("Gamestate"):
		gameState.append(obj)
#	if retry:
#		for i in (4 * Engine.physics_ticks_per_second):
#			tick(1 / Engine.physics_ticks_per_second)
	
	for ID in players.size():
		places.append(players[ID])
	
	rollbackState = save_state()
	syncTick = localTick
	
	if !peers.is_empty():
		Net.peer.change_status.rpc( PeerData.Status.Loaded )
	stageLoaded = true
	await get_tree().create_timer(0.1).timeout
	stageLoadedSignal.emit()
	if !paused and loadedLevel.stage_music:
		if loadedLevel.stage_music.wait_for_race_start:
			await get_tree().create_timer(MXGlobal.countdownTime + 1.0).timeout
		MXGlobal.play_music(loadedLevel.stage_music.musicLoop, loadedLevel.stage_music.musicIntro)
	else:
		print("stopping music")
		MXGlobal.stop_music()

var skippedLastFrame := false
func _physics_process( _delta:float ) -> void:
	if Engine.is_editor_hint(): return
	if not stageLoaded: return
	for peer in Net.peers:
		if peer.status != PeerData.Status.Loaded:
			#Net.peer.change_status.rpc( PeerData.Status.Loaded )
			return
	
	if multiplayer and multiplayer.has_multiplayer_peer() and !multiplayer.get_peers().is_empty():
		if Time.get_ticks_usec() - lastPing >= pingInterval:
			lastPing = Time.get_ticks_usec()
			for pl in players:
				if pl != MXGlobal.localPlayer:
					pl.get_parent().do_ping()
		rollback()
		var ticksToSimulate := 1
		if handleDesync and multiplayer and multiplayer.has_multiplayer_peer() and not multiplayer.is_server():
			# Simulate additional ticks if we're behind the server so we eventually catch up
			# This duplicates inputs but ensures we don't fall too far behind if we're only encountering
			# small frame stutters
			# Threshold to actually fix sync issues so we don't even bother to fix small, temporary
			# desyncs
			# 0.5 is arbitrary but it seems to work well, could be worth testing
			if averageDesync > 0.5:
				# We cut averageDesync in half so that we ease towards the correct sync instead of
				# trying to handle it all in a row
				averageDesync *= 0.5
				serverDesync = 1
				ticksToSimulate = 2
			# On the other hand if the host is behind, just wait until they aren't
			elif averageDesync < -0.5:
				if not skippedLastFrame:
					ticksToSimulate = 0
					skippedLastFrame = true
					averageDesync *= 0.5
					serverDesync += 1
				else:
					skippedLastFrame = false
		if ticksToSimulate > 0:
			if MXGlobal.localPlayer:
				for i in ticksToSimulate:
					MXGlobal.localPlayer.tick()
				MXGlobal.localPlayer.try_send_input()
			for i in ticksToSimulate:
				if multiplayer and multiplayer.has_multiplayer_peer() and multiplayer.is_server():
					tick_server()
					server_try_sync_gamestate()
					server_try_send_desync()
				else:
					tick_client()
			averageTickDelay = lerpf( averageTickDelay, maxi((localTick - 1) - rollbackTick,0), 0.25 )
	else:
		if MXGlobal.localPlayer:
			MXGlobal.localPlayer.client_tick()
		tick_client()
		if queueReload:
			load_stage(true, randi())
	
	handle_general_announcer_lines()
	places.sort_custom( func(a:ROPlayer, b:ROPlayer) -> bool:
		if a.controlledPawn.level_win_time > 0 and b.controlledPawn.level_win_time > 0:
			return a.controlledPawn.level_win_time < b.controlledPawn.level_win_time
		if a.controlledPawn.level_win_time > 0 and b.controlledPawn.level_win_time == 0:
			return true
		return a.controlledPawn.lap_progress + a.controlledPawn.lap > b.controlledPawn.lap_progress + b.controlledPawn.lap )
	for pl in places.size():
		places[pl].place = pl
		#print(places[pl].controlledPawn.lap_progress + places[pl].controlledPawn.lap)
	if Net.connected and multiplayer and multiplayer.is_server() and queueReload:
		var randomNum := randi()
		load_stage.rpc(true, randomNum)
		return

# todo:
# helper function for spawning/instantiating nodes,
# which takes rollback into account!
# when we spawn nodes during ticking,
# they should be deleted when rolling back
# to a tick before they spawned.
func rollback( fromTick := rollbackTick, inState := rollbackState, toTick:int = localTick ) -> void:
	# -1 is a valid input which means roll back the beginning of the first (0) tick
	if fromTick < -1:
		push_error("invalid rollback tick")
		return
	if inState.is_empty():
		push_error("tried to rollback but there was no state!")
		return
	
	# If we haven't reached the next rollback tick yet we have no reason to rollback so don't bother
	if localTick <= rollbackTick:
		return
	
	var inputCheckpoint := localTick + inputDelay
	for pl in players:
		if pl == MXGlobal.localPlayer: continue
		inputCheckpoint = mini( inputCheckpoint, pl.inputCheckpoint )
	# -1 because inputCheckpoint contains the next tick at which we DON'T have all inputs
	# so we need to subtract 1 to get the last frame in which we DO have all the inputs
	inputCheckpoint -= 1
	
	# If inputCheckpoint is not bigger than rollbackTick then we haven't received any new inputs
	# that would change the simulation so don't bother rolling back
	if inputCheckpoint <= rollbackTick:
		return
	
	rollbackTick = inputCheckpoint
	syncTick = inputCheckpoint
	
	MXGlobal.currentlyRollback = true
	load_state( inState )
	
	# +1 because we loaded the final state of fromTick so we don't have to simulate it again
	localTick = fromTick + 1
	var ticksToResim := maxi( toTick - localTick, 0 )
	if ticksToResim > 0:
		if multiplayer and multiplayer.is_server():
			tick_server( ticksToResim )
		else:
			tick_client( ticksToResim )
	
	totalRollbackTicks += ticksToResim
	MXGlobal.currentlyRollback = false

var next_stage_queued := false

func tick_server( amount := 1 ) -> void:
	currentlyServer = true
	
	for i in amount:
		for obj:Variant in tickableStageObjs:
			obj.tick()
		
		var all_complete := true
		
		for pl in players:
			pl.controlledPawn.tick(pl)
			if (pl.controlledPawn.state & MXRacer.FZ_MS.COMPLETEDRACE_2_Q) == 0:
				all_complete = false
		
		
		#for pl in players:
			#pl.controlledPawn.test_car_collision()
		
		if localTick == rollbackTick:
			rollbackState = save_state()
			if all_complete and !all_finished:
				print("ALL FINISHED!")
				print("Race session current track was " + str(RaceSession.current_track))
				all_finished = true
				change_level = localTick + 300
				RaceSession.current_track += 1
				var racer_count := 0
				for peer in Net.peers:
					if peer.team == 0:
						racer_count += 1
				for peer in Net.peers:
					if peer.team == 1:
						continue
					var pl := peer.get_child(0)
					var placement := places.find(pl)
					var point_index := RaceSession.peer_ids.find(peer.id)
					RaceSession.point_totals[point_index] += racer_count - placement
				RaceSession.sync_race_session.rpc(RaceSession.serialize())
				print("Now it's " + str(RaceSession.current_track))
				print("Race totals...")
				for pl in players.size():
					print(players[pl].get_parent().player_settings.username + ": " + str(RaceSession.point_totals[pl]))
			if all_finished and localTick >= change_level and !next_stage_queued:
				next_stage_queued = true
				print("Queuing next track...?")
				if MXGlobal.current_race_settings.tracks.size() <= RaceSession.current_track:
					print("Nope - returning to lobby")
					return_to_lobby.rpc()
				else:
					print("Yep!!")
					MXGlobal.load_stage_by_path.rpc(MXGlobal.current_race_settings.tracks[RaceSession.current_track], MXGlobal.current_race_settings.serialize())
		
		if localTick == syncTick:
			if syncTick == rollbackTick:
				syncState = rollbackState.duplicate()
			else:
				syncState = save_state()
		
		localTick += 1
		
	currentlyServer = false

func tick_client( amount := 1 ) -> void:
	#var t:int
	for i in amount:
		#if paused:
			#for obj in get_tree().get_nodes_in_group("StageObjects"):
				#if obj.is_in_group("ViewStage"):
					#obj = obj as RORStageObject
					#obj.tick()
			#continue
		#t = Time.get_ticks_usec()
		for obj:Variant in tickableStageObjs:
			obj.tick()
		#Debug.record("tick client objects", Time.get_ticks_usec() - t)
		
		#t = Time.get_ticks_usec()
		for pl in players:
			if is_instance_valid(pl):
				pl.controlledPawn.tick(pl)
		#Debug.record("tick client pawns", Time.get_ticks_usec() - t)
		
		#t = Time.get_ticks_usec()
		#for pl in players:
			#if is_instance_valid(pl):
				#pl.controlledPawn.test_car_collision()
		#Debug.record("tick client test car collision", Time.get_ticks_usec() - t)
		
		if localTick == rollbackTick:
			rollbackState = save_state()
		
		localTick += 1

func handle_general_announcer_lines() -> void:
	if !MXGlobal.localPlayer:
		return
	if not OS.is_debug_build() or (MXGlobal.debug_multiplayer <= 0 and not MXGlobal.debug_singleplayer):
		if !is_instance_valid(MXGlobal.localPlayer.controlledPawn):
			return
		if localTick == MXGlobal.localPlayer.controlledPawn.levelStartTime - Engine.physics_ticks_per_second * 3:
			MXGlobal.play_announcer_line("countdown_3")
		if localTick == MXGlobal.localPlayer.controlledPawn.levelStartTime - Engine.physics_ticks_per_second * 2:
			MXGlobal.play_announcer_line("countdown_2")
		if localTick == MXGlobal.localPlayer.controlledPawn.levelStartTime - Engine.physics_ticks_per_second * 1:
			MXGlobal.play_announcer_line("countdown_1")
		if localTick == MXGlobal.localPlayer.controlledPawn.levelStartTime:
			MXGlobal.play_announcer_line("go")

# This is called on each client every second to determine how much we have "drifted" from the
# server's localTick. Because of the round trip, this should be ZERO if everything is going well.
# Zero does NOT mean the client and host are on the same localTick in real time, but that by the
# time this client receives the localTick from the server it has reached that localTick.
@rpc("any_peer", "call_remote", "unreliable_ordered")
func sent_server_desync( server_local_tick:int ) -> void:
	serverDesync = (server_local_tick - localTick) + desyncTarget
	if automaticDesyncAdjustment:
		averageDesync = lerpf( averageDesync, float(serverDesync), automaticDesyncStrength )
		# This line trends desyncTarget towards half of the round trip time in ticks to
		# minimize the amount of delay clients experience with each other
		desyncTarget = int(Net.peer.ping * 0.000001 * Engine.physics_ticks_per_second * desyncTargetLerp)

# BIG TODO:
# do DELTA state transmissions!
# only bother sending data that changed since the last update.
# we shouldn't need to send 200 bytes of data for every single object,
# even the ones that don't move!
# in fact, we shouldn't even have to send data for objects
# that haven't been meaningfully affected by the ball
# so, we should exclude unchanged data,
# and entirely exclude objects unaffected by the player's actions!
func server_try_sync_gamestate() -> void:
	if Time.get_ticks_usec() - lastSync > syncInterval:
		if localTick <= rollbackTick:
			syncTick = localTick
			syncState = save_state()
		sync_gamestate.rpc( syncTick, syncState )
		lastSync = Time.get_ticks_usec()

func server_try_send_desync() -> void:
	if Time.get_ticks_usec() - lastDesyncCheck > desyncInterval:
		sent_server_desync.rpc( localTick )
		lastDesyncCheck = Time.get_ticks_usec()

# The server ONLY sends game states that have been properly simulated by the server
# The server ONLY properly simulates ticks in which it has all inputs from every player
# As such, this gamestate syncs ONLY to correct floating point imprecision as every client
# simulates back to their local tick starting from the host's serialized game state
@rpc("authority", "call_remote", "reliable")
func sync_gamestate( tickFromServer : int, inState : Dictionary ) -> void:
	rollbackTick = tickFromServer
	rollbackState = inState
	rollback( tickFromServer, inState, localTick )
