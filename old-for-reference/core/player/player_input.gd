class_name PlayerInput extends RefCounted

enum PressedState {
	Released,
	JustReleased,
	Pressed,
	JustPressed
}

static var Neutral := PlayerInput.new()
func _to_string() -> String:
	return str(hash(serialize()))
# Must be an even number so that zero is true zero
const RAW_BIT_PRECISION := 2**8 - 2
const AXIS_NEUTRAL := roundi(remap( 0.0, -1.0, 1.0, 0, RAW_BIT_PRECISION ))
const TRIGGER_NEUTRAL := roundi(remap( 0.0, 0.0, 1.0, 0, RAW_BIT_PRECISION ))

var ForwardMoveAxis := 0.0
var ForwardMoveAxisRaw := AXIS_NEUTRAL:
	set(value):
		if value != ForwardMoveAxisRaw:
			ForwardMoveAxisRaw = value
			ForwardMoveAxis = remap( ForwardMoveAxisRaw, 0, RAW_BIT_PRECISION, -1.0, 1.0 )
var SideMoveAxis := 0.0
var SideMoveAxisRaw := AXIS_NEUTRAL:
	set(value):
		if value != SideMoveAxisRaw:
			SideMoveAxisRaw = value
			SideMoveAxis = remap( SideMoveAxisRaw, 0, RAW_BIT_PRECISION, -1.0, 1.0 )
var ForwardCamAxis := 0.0
var ForwardCamAxisRaw := AXIS_NEUTRAL:
	set(value):
		if value != ForwardCamAxisRaw:
			ForwardCamAxisRaw = value
			ForwardCamAxis = remap( ForwardCamAxisRaw, 0, RAW_BIT_PRECISION, -1.0, 1.0 )
var SideCamAxis := 0.0
var SideCamAxisRaw := AXIS_NEUTRAL:
	set(value):
		if value != SideCamAxisRaw:
			SideCamAxisRaw = value
			SideCamAxis = remap( SideCamAxisRaw, 0, RAW_BIT_PRECISION, -1.0, 1.0 )
var StrafeRight := 0.0
var StrafeRightRaw := TRIGGER_NEUTRAL:
	set(value):
		if value != StrafeRightRaw:
			StrafeRightRaw = value
			StrafeRight = remap( StrafeRightRaw, 0, RAW_BIT_PRECISION, 0.0, 1.0 )
var StrafeLeft := 0.0
var StrafeLeftRaw := TRIGGER_NEUTRAL:
	set(value):
		if value != StrafeLeftRaw:
			StrafeLeftRaw = value
			StrafeLeft = remap( StrafeLeftRaw, 0, RAW_BIT_PRECISION, 0.0, 1.0 )
var Accelerate := PressedState.Released
var SpinAttack := PressedState.Released
var MenuConfirm := PressedState.Released
var MenuBack := PressedState.Released
var SideAttack := PressedState.Released
var Boost := PressedState.Released
var Brake := PressedState.Released
var Pause := PressedState.Released

func copy_to( i:PlayerInput ) -> PlayerInput:
	i.ForwardMoveAxisRaw = ForwardMoveAxisRaw
	i.SideMoveAxisRaw = SideMoveAxisRaw
	i.ForwardCamAxisRaw = ForwardCamAxisRaw
	i.SideCamAxisRaw = SideCamAxisRaw
	i.StrafeRightRaw = StrafeRightRaw
	i.StrafeLeftRaw = StrafeLeftRaw
	i.Accelerate = Accelerate
	i.SpinAttack = SpinAttack
	i.MenuConfirm = MenuConfirm
	i.MenuBack = MenuBack
	i.SideAttack = SideAttack
	i.Boost = Boost
	i.Brake = Brake
	i.Pause = Pause
	return i

func duplicate() -> PlayerInput:
	return copy_to( PlayerInput.new() )

func lerp( i:PlayerInput, percent:float ) -> PlayerInput:
	var new := copy_to( PlayerInput.new() )
	new.ForwardMoveAxisRaw = int(lerpf( new.ForwardMoveAxisRaw, i.ForwardMoveAxisRaw, percent ))
	new.SideMoveAxisRaw = int(lerpf( new.SideMoveAxisRaw, i.SideMoveAxisRaw, percent ))
	new.ForwardCamAxisRaw = int(lerpf( new.ForwardCamAxisRaw, i.ForwardCamAxisRaw, percent ))
	new.SideCamAxisRaw = int(lerpf( new.SideCamAxisRaw, i.SideCamAxisRaw, percent ))
	new.StrafeRightRaw = int(lerpf( new.StrafeRightRaw, i.StrafeRightRaw, percent ))
	new.StrafeLeftRaw = int(lerpf( new.StrafeLeftRaw, i.StrafeLeftRaw, percent ))
	return new

static func get_pressed_state( action:String ) -> PressedState:
	if Input.is_action_just_pressed(action):
		return PressedState.JustPressed
	if Input.is_action_pressed(action):
		return PressedState.Pressed
	if Input.is_action_just_released(action):
		return PressedState.JustReleased
	return PressedState.Released

static func from_input() -> PlayerInput:
	var result := PlayerInput.new()
	var strafe_modifier := Input.is_action_pressed("StrafeModifier")
	result.ForwardMoveAxisRaw = roundi(remap( Input.get_axis("MoveBack", "MoveForward"), -1.0, 1.0, 0, RAW_BIT_PRECISION ))
	if !strafe_modifier:
		result.SideMoveAxisRaw = roundi(remap( clampf(Input.get_axis("MoveRight", "MoveLeft") + MXGlobal.mouse_offset.x, -1.0, 1.0), -1.0, 1.0, 0, RAW_BIT_PRECISION ))
	else:
		result.SideMoveAxisRaw = roundi(remap(Input.get_axis("MoveRight", "MoveLeft"), -1.0, 1.0, 0, RAW_BIT_PRECISION ))
	if MXGlobal.mouse_driving_mode == 2:
		MXGlobal.mouse_offset = Vector2.ZERO
	result.ForwardCamAxisRaw = roundi(remap( Input.get_axis("CamForward", "CamBack"), -1.0, 1.0, 0, RAW_BIT_PRECISION ))
	result.SideCamAxisRaw = roundi(remap( Input.get_axis("CamRight", "CamLeft"), -1.0, 1.0, 0, RAW_BIT_PRECISION ))
	result.StrafeRightRaw = roundi(remap( Input.get_action_strength("StrafeRight"), 0.0, 1.0, 0, RAW_BIT_PRECISION ))
	result.StrafeLeftRaw = roundi(remap( Input.get_action_strength("StrafeLeft"), 0.0, 1.0, 0, RAW_BIT_PRECISION ))
	result.Accelerate = PlayerInput.get_pressed_state("Accelerate")
	result.SpinAttack = PlayerInput.get_pressed_state("SpinAttack")
	result.MenuConfirm = PlayerInput.get_pressed_state("MenuConfirm")
	result.MenuBack = PlayerInput.get_pressed_state("MenuBack")
	result.SideAttack = PlayerInput.get_pressed_state("SideAttack")
	result.Boost = PlayerInput.get_pressed_state("Boost")
	result.Brake = PlayerInput.get_pressed_state("Brake")
	result.Pause = PlayerInput.get_pressed_state("Pause")
	#return result.deserialize(result.serialize())
	return result

static func random() -> PlayerInput:
	var result := PlayerInput.new()
	result.ForwardMoveAxisRaw = randi_range( 0, RAW_BIT_PRECISION )
	result.SideMoveAxisRaw = randi_range( 0, RAW_BIT_PRECISION )
	result.ForwardCamAxisRaw = randi_range( 0, RAW_BIT_PRECISION )
	result.SideCamAxisRaw = randi_range( 0, RAW_BIT_PRECISION )
	result.StrafeRightRaw = randi_range( 0, RAW_BIT_PRECISION )
	result.StrafeLeftRaw = randi_range( 0, RAW_BIT_PRECISION )
	result.Accelerate = (randi() % PressedState.size()) as PressedState
	return result

func serialize() -> PackedByteArray:
	var bitmask := 0
	var button_states:int = Accelerate + (SpinAttack << 3) + (MenuConfirm << 6) + (MenuBack << 9) + (SideAttack << 12) + (Boost << 15) + (Brake << 18) + (Pause << 21)
	if ForwardMoveAxisRaw != AXIS_NEUTRAL: bitmask |= 1<<0
	if SideMoveAxisRaw != AXIS_NEUTRAL: bitmask |= 1<<1
	if ForwardCamAxisRaw != AXIS_NEUTRAL: bitmask |= 1<<2
	if SideCamAxisRaw != AXIS_NEUTRAL: bitmask |= 1<<3
	if StrafeRightRaw != TRIGGER_NEUTRAL: bitmask |= 1<<4
	if StrafeLeftRaw != TRIGGER_NEUTRAL: bitmask |= 1<<5
	if button_states != 0: bitmask |= 1<<6
	var buffer := StreamPeerBuffer.new()
	buffer.put_u8( bitmask )
	if bitmask & 1<<0 != 0: buffer.put_u8( ForwardMoveAxisRaw )
	if bitmask & 1<<1 != 0: buffer.put_u8( SideMoveAxisRaw )
	if bitmask & 1<<2 != 0: buffer.put_u8( ForwardCamAxisRaw )
	if bitmask & 1<<3 != 0: buffer.put_u8( SideCamAxisRaw )
	if bitmask & 1<<4 != 0: buffer.put_u8( StrafeRightRaw )
	if bitmask & 1<<5 != 0: buffer.put_u8( StrafeLeftRaw )
	if bitmask & 1<<6 != 0: buffer.put_u32( button_states )
	return buffer.data_array

func deserialize( data:PackedByteArray ) -> PlayerInput:
	var buffer := StreamPeerBuffer.new()
	buffer.data_array = data
	var bitmask := buffer.get_u8()
	if bitmask & 1<<0 != 0: ForwardMoveAxisRaw = buffer.get_u8()
	if bitmask & 1<<1 != 0: SideMoveAxisRaw = buffer.get_u8()
	if bitmask & 1<<2 != 0: ForwardCamAxisRaw = buffer.get_u8()
	if bitmask & 1<<3 != 0: SideCamAxisRaw = buffer.get_u8()
	if bitmask & 1<<4 != 0: StrafeRightRaw = buffer.get_u8()
	if bitmask & 1<<5 != 0: StrafeLeftRaw = buffer.get_u8()
	if bitmask & 1<<6 != 0:
		# NOTE each state requires 3 bits
		var button_states := buffer.get_u32()
		Accelerate = (button_states & 7) as PressedState
		SpinAttack = ((button_states >> 3) & 7) as PressedState
		MenuConfirm = ((button_states >> 6) & 7) as PressedState
		MenuBack = ((button_states >> 9) & 7) as PressedState
		SideAttack = ((button_states >> 12) & 7) as PressedState
		Boost = ((button_states >> 15) & 7) as PressedState
		Brake = ((button_states >> 18) & 7) as PressedState
		Pause = ((button_states >> 21) & 7) as PressedState
	return self
