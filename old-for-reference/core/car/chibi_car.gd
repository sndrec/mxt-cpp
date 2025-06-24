class_name ChibiCar extends Node3D

@export var car_definition : CarDefinition
var car_mesh : Node3D
@onready var name_plate_control := $NamePlateControl as Control
@onready var name_plate_username := $NamePlateControl/Panel/MarginContainer/Control/NamePlateUsername as Label
@onready var name_plate_ping := $NamePlateControl/Panel/MarginContainer/Control/NamePlatePing as Label

var velocity := 0.0
var knockback_velocity := Vector3.ZERO
var angle_velocity := 0.0
var lobby_camera : Camera3D
var controlling_peer : PeerData

@rpc("any_peer", "call_local", "unreliable_ordered")
func send_state_to_server(in_vel : float, in_kb : Vector3, in_ang : float, in_pos : Vector3, in_rot : Vector3) -> void:
	broadcast_state.rpc(in_vel, in_kb, in_ang, in_pos, in_rot)

@rpc("any_peer", "call_local", "unreliable_ordered")
func broadcast_state(in_vel : float, in_kb : Vector3, in_ang : float, in_pos : Vector3, in_rot : Vector3) -> void:
	if get_multiplayer_authority() != Net.id:
		velocity = in_vel
		knockback_velocity = in_kb
		angle_velocity = in_ang
		position = in_pos
		rotation = in_rot

var last_sync : int = 0
var set_up : bool = false

@rpc("any_peer", "call_local", "reliable")
func _delete_car() -> void:
	if controlling_peer and is_instance_valid(controlling_peer):
		MXGlobal.current_multi_lobby.peers_with_cars.erase(controlling_peer.id)
	queue_free()

func _process( delta:float ) -> void:
	if !Net.peer_map.has(get_multiplayer_authority()):
		_delete_car.rpc()
		return
	controlling_peer = Net.peer_map[get_multiplayer_authority()]
	if !set_up and controlling_peer.player_settings:
		car_definition = MXGlobal.cars[MXGlobal.car_lookup[controlling_peer.player_settings.car_choice]]
		car_mesh = car_definition.model.instantiate()
		add_child(car_mesh)
		var mesh_instance := car_mesh.get_child(0) as MeshInstance3D
		print("set up dupe mesh")
		name_plate_username.text = controlling_peer.player_settings.username
		mesh_instance.set_instance_shader_parameter("base_color", controlling_peer.player_settings.base_color)
		mesh_instance.set_instance_shader_parameter("secondary_color", controlling_peer.player_settings.secondary_color)
		mesh_instance.set_instance_shader_parameter("tertiary_color", controlling_peer.player_settings.tertiary_color)
		mesh_instance.set_instance_shader_parameter("overlay_color", Color.BLACK)
		set_up = true
	if get_multiplayer_authority() == multiplayer.get_unique_id() and !Engine.is_editor_hint():
		#var inp := PlayerInput.from_input()
		angle_velocity = Input.get_axis("MoveRight", "MoveLeft") * -car_definition.steer_power * 0.5
		if Input.is_action_pressed("Accelerate"):
			velocity += car_definition.calculate_accel_with_speed(velocity * 10) * 100 * delta
		else:
			velocity = maxf(0, velocity - car_definition.calculate_friction(velocity) * 10 * delta)
		position += (basis.x.slide(Vector3.UP)) * Input.get_axis("StrafeLeft", "StrafeRight") * car_definition.strafe_power * velocity * -delta * 0.0015
		name_plate_control.modulate = Color(1.0, 0.7, 0.2)
		#name_plate_control.visible = true
	#print(controlling_peer.ping)
	name_plate_ping.text = str(0.001 * controlling_peer.ping) + "ms"
	if is_instance_valid(lobby_camera):
		name_plate_control.position = lobby_camera.unproject_position(position) + Vector2(16, -32)
	position.y = lerpf(0.6, 0.65, sin(0.005 * Time.get_ticks_msec()))
	rotation_degrees += Vector3(0, angle_velocity * delta * -20, 0)
	position += basis.z * velocity * delta * 0.1 + knockback_velocity * delta * 0.2
	rotation_degrees.z = lerpf(rotation_degrees.z, angle_velocity * 2, delta * 4)
	if absf(position.x) > 14:
		velocity *= 0.5
		knockback_velocity += Vector3(-1, 0, 0) * signf(position.x) * velocity
		position.x = clampf(position.x, -14, 14)
	if absf(position.z) > 10:
		velocity *= 0.5
		knockback_velocity += Vector3(0, 0, -1) * signf(position.z) * velocity
		position.z = clampf(position.z, -10, 10)
	knockback_velocity += -knockback_velocity * 8 * delta
	if get_multiplayer_authority() == multiplayer.get_unique_id() and Time.get_ticks_msec() > last_sync + 15:
		send_state_to_server.rpc_id(1, velocity, knockback_velocity, angle_velocity, position, rotation)
		last_sync = Time.get_ticks_msec()
	#print(position)
