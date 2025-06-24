class_name Spectator extends Node

var spec_mode : int = 0
var specced_player : int = 0
var spec_velocity : Vector3 = Vector3.ZERO
var spec_position : Vector3 = Vector3.ZERO
var spec_rotation : Basis = Basis.IDENTITY
var spec_rotation_desired : Basis = Basis.IDENTITY
var spec_offset : Vector3 = Vector3.ZERO
var spec_camera : Camera3D
var focused : bool = false

var inputCheckpoint := 0
var commonCheckpoint := 0
var race_hud : RaceHud

func _ready() -> void:
	if get_multiplayer_authority() == multiplayer.get_unique_id():
		spec_camera = Camera3D.new()
		add_child(spec_camera)
		spec_camera.make_current()
		race_hud = preload("res://core/ui/race_hud.tscn").instantiate()
		add_child(race_hud)

func _notification( what:int ) -> void:
	if what == MainLoop.NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
	elif what == MainLoop.NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false

func get_common_checkpoint() -> int:
	if !is_instance_valid(MXGlobal.currentStageOverseer):
		return 0
	var checkpoint := MXGlobal.currentStageOverseer.localTick
	for pl in MXGlobal.currentStageOverseer.players:
		if pl == self: continue
		checkpoint = mini( checkpoint, pl.inputCheckpoint )
	return maxi( checkpoint, 0 )

func _physics_process( _delta:float ) -> void:
	if get_multiplayer_authority() == multiplayer.get_unique_id() and multiplayer.get_unique_id() != 1:
		commonCheckpoint = get_common_checkpoint()
		send_common_checkpoint_to_server.rpc_id(1, commonCheckpoint)

@rpc("any_peer", "call_remote", "unreliable_ordered")
func send_common_checkpoint_to_server( in_point:int ) -> void:
	commonCheckpoint = in_point

func _process( delta:float ) -> void:
	if !(get_multiplayer_authority() == multiplayer.get_unique_id()):
		return
	#print(commonCheckpoint)
	#var ratio := minf((MXGlobal.curFrameAccumulation / MXGlobal.tick_delta), 1)
	if !is_instance_valid(MXGlobal.currentStageOverseer):
		return
	if specced_player < MXGlobal.currentStageOverseer.players.size():
		var specced_car := MXGlobal.currentStageOverseer.players[specced_player].controlledPawn as MXRacer
		if !is_instance_valid(specced_car) or !is_instance_valid(self) or !is_instance_valid(specced_car.car_camera) or !is_instance_valid(spec_camera):
			return
		#specced_car.car_visual.global_transform.origin = specced_car.previous_transform.origin.lerp(specced_car.current_transform.origin, ratio)
		#specced_car.car_visual.global_transform.basis = specced_car.previous_transform.basis.slerp(specced_car.current_transform.basis, ratio)
		
		if focused:
			if Input.is_action_just_pressed("SpinAttack"):
				spec_mode += 1
				if spec_mode == 3:
					spec_mode = 0
				spec_offset = Vector3.ZERO
				spec_velocity = Vector3.ZERO
			if spec_mode == 0 or spec_mode == 1:
				if Input.is_action_just_pressed("Accelerate"):
					specced_player -= 1
				if Input.is_action_just_pressed("Boost"):
					specced_player += 1
			if spec_mode == 1 or spec_mode == 2:
				spec_rotation_desired = spec_rotation_desired.rotated(spec_rotation_desired.x, Input.get_axis("CamForward", "CamBack") * delta * -6)
				spec_rotation_desired = spec_rotation_desired.rotated(spec_rotation_desired.y, Input.get_axis("CamLeft", "CamRight") * delta * -6)
				spec_rotation_desired = spec_rotation_desired.rotated(spec_rotation_desired.z, Input.get_axis("StrafeLeft", "StrafeRight") * delta * -4)
				spec_rotation_desired = spec_rotation_desired.orthonormalized()
				spec_rotation = spec_rotation.slerp(spec_rotation_desired, delta * 6)
				spec_velocity += spec_camera.basis * Vector3(Input.get_axis("MoveLeft", "MoveRight"), 0, Input.get_axis("MoveForward", "MoveBack")) * delta * 600
				spec_velocity += -spec_velocity * delta * 4
		
		if spec_mode == 0: # player view
			var spec_vel : Vector3 = specced_car.velocity
			spec_vel = (specced_car.velocity - specced_car.gravity_basis.y * specced_car.velocity.dot(specced_car.gravity_basis.y))
			spec_vel = specced_car.car_visual.global_transform.basis.z.lerp(spec_vel, min(spec_vel.length(), 5) * 0.2).normalized()
			spec_camera.basis = specced_car.car_camera.basis
			#car_camera.rotation_degrees += Vector3(-15, 0, 0)
			spec_camera.global_position = specced_car.car_visual.global_transform.origin + spec_camera.global_basis.z * 3.0 + spec_camera.global_basis.y * 1.0
			spec_camera.fov = specced_car.car_camera.fov
		#elif spec_mode == 1: # orbit
			#var camera_rotation := (specced_car.gravity_basis * spec_rotation).orthonormalized()
			#spec_camera.basis = camera_rotation
			#spec_offset += (spec_velocity * specced_car.gravity_basis) * delta
			#spec_camera.position = specced_car.car_visual.global_transform.origin + specced_car.gravity_basis * spec_offset + specced_car.gravity_basis.y
		elif spec_mode == 2: # free fly
			spec_camera.basis = spec_rotation
			spec_camera.position += spec_velocity * delta
	if specced_player < 0:
		specced_player = MXGlobal.currentStageOverseer.players.size() - 1
	if specced_player >= MXGlobal.currentStageOverseer.players.size():
		specced_player = 0
		
