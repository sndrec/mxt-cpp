class_name ROPlayer extends Node

var inputHistory : Array[PlayerInput] = []
# Used to keep track of what index to insert new inputs into since we have pre-sized array
var nextInputIndex := 0
# For the host player this is always equal to the last input we created (generally equal to 
# local tick )
# For every other player (host or client) this is equal to the index of the next input the server
# needs for that player
# For example, if we Player 2 has sent 3 inputs to the server and has received confirmation that
# those inputs have been received, the server will broadcast to every player that Player 2's
# input checkpoint is 3 (the server has inputs 0, 1, and 2)
var inputCheckpoint := 0
# Used by the host to keep track of what inputs clients need from other clients
var commonCheckpoint := 0
var playerID : int = 1
var place : int = 1
var focused : bool = true
var overseer : RORGameplayOverseer
var controlledPawn : MXRacer

func _ready() -> void:
	_initialize_input()
	add_to_group("Players")

func _initialize_input() -> void:
	inputHistory.clear()
	inputHistory.resize(108000)
	for i in overseer.inputDelay:
		inputHistory[i] = PlayerInput.Neutral
	inputCheckpoint = overseer.inputDelay
	nextInputIndex = overseer.inputDelay

func _notification( what:int ) -> void:
	if what == MainLoop.NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
	elif what == MainLoop.NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
	elif what == NOTIFICATION_PREDELETE:
		MXGlobal.localPlayer = null
		if is_instance_valid(MXGlobal.currentStageOverseer):
			MXGlobal.currentStageOverseer.players.erase(self)
			MXGlobal.currentStageOverseer.places.erase(self)

func create_input() -> PlayerInput:
	#if not multiplayer.is_server():
	#	return PlayerInput.random()
	if !focused:
		return PlayerInput.Neutral
	return PlayerInput.from_input()

func client_tick() -> void:
	inputHistory[nextInputIndex] = create_input()
	nextInputIndex += 1

func tick() -> void:
	# make sure our client owns this player
	if multiplayer and multiplayer.has_multiplayer_peer() and get_multiplayer_authority() != multiplayer.get_unique_id():
		return
	inputHistory[nextInputIndex] = create_input()
	nextInputIndex += 1

func _physics_process(_delta: float) -> void:
	_broadcasted_this_frame = false

# Host only, called by host player every frame, by other players when their inputs are received
# TODO this can be optimized by not serializing the inputs for each separate player
# instead just serialize the array once based on the player that is farthest behind
# and trim that array to send to each other player
var _broadcasted_this_frame := false
func broadcast_inputs_direct() -> void:
	if _broadcasted_this_frame: return
	if !is_instance_valid(overseer):
		return
	_broadcasted_this_frame = true
	# Iterate through other clients and update their version of this player's inputs
	for otherpl in overseer.players:
		# Don't send a client's inputs to themselves
		if otherpl == self: continue
		# Don't send a client's inputs to the host
		if otherpl.get_multiplayer_authority() == 1: continue
		#var inputsToSend := {}
		# Start from the other client's common input (lowest inputCheckpoint)
		var broadcastCheckpoint := maxi( otherpl.commonCheckpoint, 0 )
		var inputArray:Array[PackedByteArray] = []
		# Serialize and append this player's inputs to send in a block of data to the other player
		# Only send up to 100 inputs
		for k in range( broadcastCheckpoint, mini(nextInputIndex,broadcastCheckpoint + 100) ):
			if inputHistory[k] == null:
				assert(false,"this should never happen")
				break
			inputArray.append( inputHistory[k].serialize() )
		if not inputArray.is_empty():
			send_inputs_in_batches( otherpl, inputArray, broadcastCheckpoint )
	# Send inputs to spectators
	for peer:PeerData in Net.peers:
		# Don't send to non-spectators
		if peer.team == 0: continue
		
		var spec:Spectator = peer.get_child(0)
		#var inputsToSend := {}
		# Start from the other client's common input (lowest inputCheckpoint)
		var broadcastCheckpoint := maxi( spec.commonCheckpoint, 0 )
		var inputArray:Array[PackedByteArray] = []
		# Serialize and append this player's inputs to send in a block of data to the other player
		# Only send up to 100 inputs
		for k in range( broadcastCheckpoint, mini(nextInputIndex,broadcastCheckpoint + 100) ):
			if inputHistory[k] == null:
				assert(false,"this should never happen")
				break
			inputArray.append( inputHistory[k].serialize() )
		if not inputArray.is_empty():
			send_inputs_to_spectator( spec, inputArray, broadcastCheckpoint )
		

func send_inputs_to_spectator( in_spec:Spectator, inputsToSend:Array[PackedByteArray], inputIndex:int ) -> void:
	var batchSize := 20
	for j in ceili(inputsToSend.size() / float(batchSize)):
		var batchIndex := j * batchSize
		var batchData := inputsToSend.slice( batchIndex, batchIndex + batchSize )
		# Send the client we're sending inputs to their current inputCheckpoint as well so they
		# have an up to date acknowledgement with each input
		sent_inputs_from_server.rpc_id( in_spec.get_multiplayer_authority(), batchData, inputIndex + batchIndex, in_spec.inputCheckpoint )

func send_inputs_in_batches( otherPlayer:ROPlayer, inputsToSend:Array[PackedByteArray], inputIndex:int ) -> void:
	var batchSize := 20
	for j in ceili(inputsToSend.size() / float(batchSize)):
		var batchIndex := j * batchSize
		var batchData := inputsToSend.slice( batchIndex, batchIndex + batchSize )
		# Send the client we're sending inputs to their current inputCheckpoint as well so they
		# have an up to date acknowledgement with each input
		sent_inputs_from_server.rpc_id( otherPlayer.get_multiplayer_authority(), batchData, inputIndex + batchIndex, otherPlayer.inputCheckpoint )

# Clients send all of the inputs the server has not confirmed every frame
# This can be optimized but additional logic needs to be in place to ensure dropped
# inputs are recovered. The logic present in broadcast_inputs_direct might be enough?
func try_send_input() -> void:
	if (not multiplayer.is_server()) and multiplayer and multiplayer.has_multiplayer_peer():
		var batchSize := 20.0
		# If we have input delay we need to send inputs "in the future" since we are technically
		# supplying inputs for a future frame
		var to_send := mini( nextInputIndex - inputCheckpoint, 100 )
		var batchCheckpoint := inputCheckpoint
		commonCheckpoint = get_common_checkpoint()
		for j in ceili(to_send / batchSize):
			var batchIndex := batchCheckpoint + j * int(batchSize)
			var inputToSend := []
			# Don't batch more than we have if we don't have a full batch
			for i in range( batchIndex, mini( nextInputIndex, batchIndex + int( batchSize ) ) ):
				inputToSend.append( inputHistory[i].serialize() )
			# If we're not confirming every input, assume these inputs will be delivered
			# TODO NOTE NYI (unreliable)
			if not overseer.confirmEveryInput:
				inputCheckpoint += inputToSend.size()
			send_unacknowledged_input_to_server.rpc_id( 1, inputToSend, batchIndex, commonCheckpoint )
	else:
		inputCheckpoint = nextInputIndex
		broadcast_inputs_direct()

func get_common_checkpoint() -> int:
	var checkpoint := nextInputIndex
	for pl in overseer.players:
		if pl == self: continue
		checkpoint = mini( checkpoint, pl.inputCheckpoint )
	return maxi( checkpoint, 0 )

# Host only, clients call this to inform the host of their new most common input
@rpc("any_peer", "call_local", "unreliable")
func receive_common_input( newCommonInput:int ) -> void:
	commonCheckpoint = newCommonInput

# Host only, receives inputs from clients and rebroadcasts them to other clients
@rpc("any_peer", "call_local", "unreliable")
func send_unacknowledged_input_to_server( data:Array, inputIndex:int, p_commonCheckpoint:int ) -> void:
	# Type safe way to assign data
	var inputData:Array[PackedByteArray] = []
	inputData.assign( data )
	decompose_and_add_slice( inputData, inputIndex )
	inputCheckpoint = nextInputIndex
	commonCheckpoint = maxi( commonCheckpoint, p_commonCheckpoint )
	broadcast_inputs_direct()

# This is called on clients when they receive input data from the server about any other player
# We also send the client their input checkpoint with each input batch we send
@rpc("any_peer", "call_local", "unreliable")
func sent_inputs_from_server( data:Array, inputIndex:int, p_inputCheckpoint:int ) -> void:
	var input_data:Array[PackedByteArray] = []
	input_data.assign( data )
	decompose_and_add_slice( input_data, inputIndex )
	inputCheckpoint = nextInputIndex
	if Net.peer.team == 1:
		Net.peer.get_child(0).inputCheckpoint = get_common_checkpoint()
	# Clients are never sent their own inputs so this does not need a barrier
	if MXGlobal.localPlayer:
		MXGlobal.localPlayer.inputCheckpoint = maxi( MXGlobal.localPlayer.inputCheckpoint, p_inputCheckpoint )

# This handles out of order packets by iterating through the added history until we
# encounter a null
func decompose_and_add_slice( data:Array[PackedByteArray], inputIndex : int) -> void:
	for i in data.size():
		# They might be empty if the server was lacking an input for this batch
		if data[i].is_empty():
			assert(false,"this should never happen")
			continue
		var new_input := PlayerInput.new()
		new_input.deserialize( data[i] )
		inputHistory[inputIndex + i] = new_input
	while(true):
		if inputHistory[nextInputIndex] == null:
			break
		nextInputIndex += 1

func get_latest_input() -> PlayerInput:
	if nextInputIndex <= 0:
		return PlayerInput.Neutral
	return inputHistory[nextInputIndex - 1]

func get_suitable_input( send_neutral_on_missing := false ) -> PlayerInput:
	# If we have no input history send a blank input
	if nextInputIndex <= 0:
		return PlayerInput.Neutral
	# If we have an available input for the use tick, return it
	elif inputHistory[overseer.localTick] != null:
		return inputHistory[overseer.localTick]
	if send_neutral_on_missing:
		return PlayerInput.Neutral
	return inputHistory[nextInputIndex - 1]
