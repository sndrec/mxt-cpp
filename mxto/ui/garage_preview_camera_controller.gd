class_name GaragePreviewCameraController extends RefCounted

const PAN_LIMIT := 4.0
const DEFAULT_YAW := deg_to_rad(25.0)
const DEFAULT_PITCH := deg_to_rad(-9.0)
const MIN_PITCH := deg_to_rad(-89.0)
const MAX_PITCH := deg_to_rad(55.0)

var yaw := DEFAULT_YAW
var pitch := DEFAULT_PITCH
var pan := Vector3.ZERO
var distance := 22.0
var target := Vector3(0.0, 0.5, 0.0)
var minimum_distance := 2.5
var maximum_distance := 44.0
var default_distance := 22.0
var default_target := Vector3(0.0, 0.5, 0.0)
var drag_button := 0
var drag_start := Vector2.ZERO
var drag_last := Vector2.ZERO
var drag_moved := false


func configure_frame(frame_target: Vector3, frame_distance: float, reset_view := false) -> void:
	default_target = frame_target
	default_distance = maxf(0.05, frame_distance)
	minimum_distance = maxf(0.05, default_distance * 0.1)
	maximum_distance = maxf(44.0, default_distance * 4.0)
	target = frame_target
	if reset_view:
		reset()


func reset() -> void:
	yaw = DEFAULT_YAW
	pitch = DEFAULT_PITCH
	pan = Vector3.ZERO
	distance = clampf(default_distance, minimum_distance, maximum_distance)
	target = default_target
	drag_button = 0
	drag_moved = false


func handle_mouse_button(event: InputEventMouseButton) -> bool:
	if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
		distance = maxf(minimum_distance, distance * 0.9)
		return true
	if event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
		distance = minf(maximum_distance, distance * 1.1)
		return true
	if event.button_index != MOUSE_BUTTON_LEFT \
			and event.button_index != MOUSE_BUTTON_RIGHT \
			and event.button_index != MOUSE_BUTTON_MIDDLE:
		return false
	if event.pressed and event.button_index == MOUSE_BUTTON_LEFT and event.double_click:
		reset()
		return true
	if event.pressed:
		drag_button = event.button_index
		drag_start = event.position
		drag_last = event.position
		drag_moved = false
		return true
	if drag_button == event.button_index:
		drag_button = 0
		return true
	return false


func handle_mouse_motion(event: InputEventMouseMotion) -> bool:
	if drag_button == 0:
		return false
	var delta := event.position - drag_last
	drag_last = event.position
	if event.position.distance_to(drag_start) > 4.0:
		drag_moved = true
	if drag_button == MOUSE_BUTTON_LEFT and !event.shift_pressed:
		yaw += delta.x * -0.004
		pitch = clampf(pitch + delta.y * -0.004, MIN_PITCH, MAX_PITCH)
	else:
		var pan_scale := distance * 0.0008
		pan.x -= delta.x * pan_scale
		pan.y += delta.y * pan_scale
		_clamp_pan()
	return true


func apply(camera: Camera3D) -> void:
	if camera == null:
		return
	_clamp_pan()
	var offset := camera_offset()
	var focus := pan_target(offset)
	camera.position = focus + offset
	camera.basis = camera_basis(offset)


func camera_offset() -> Vector3:
	var yaw_basis := Basis(Vector3.UP, yaw)
	var pitch_basis := Basis(yaw_basis.x.normalized(), pitch)
	return pitch_basis * (yaw_basis * Vector3(0.0, 0.0, distance))


func camera_basis(offset: Vector3) -> Basis:
	var view_back := offset.normalized()
	var right := Vector3.UP.cross(view_back)
	if right.length_squared() <= 0.0001:
		right = Vector3.RIGHT
	else:
		right = right.normalized()
	var up := view_back.cross(right).normalized()
	return Basis(right, up, view_back)


func pan_target(offset: Vector3) -> Vector3:
	var plane_basis := view_plane_basis(offset)
	return target + plane_basis.x * pan.x + plane_basis.y * pan.y


func view_plane_basis(offset: Vector3) -> Basis:
	return camera_basis(offset)


func _clamp_pan() -> void:
	pan.x = clampf(pan.x, -PAN_LIMIT, PAN_LIMIT)
	pan.y = clampf(pan.y, -PAN_LIMIT, PAN_LIMIT)
	pan.z = 0.0
