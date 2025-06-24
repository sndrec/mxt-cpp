@tool

class_name Checkpoint extends Resource

var position := Vector3.ZERO:
	set(new_position):
		position = new_position
		emit_changed()

var rotation := Vector3.ZERO:
	set(new_rotation):
		rotation = new_rotation
		emit_changed()

var respawn_transform := Transform3D.IDENTITY:
	set(new_respawn_transform):
		respawn_transform = new_respawn_transform
		emit_changed()

var radius := 10.0:
	set(new_radius):
		radius = new_radius
		emit_changed()

var reset_gravity := false:
	set(new_reset_gravity):
		reset_gravity = new_reset_gravity
		emit_changed()

var required_checkpoint := false:
	set(new_required_checkpoint):
		required_checkpoint = new_required_checkpoint
		emit_changed()

var checkpoint_plane : Plane
var checkpoint_orientation : Quaternion
var checkpoint_basis : Basis

func _init( p_position := position, p_rotation := rotation, p_respawn_transform := respawn_transform, p_radius := radius, p_reset_gravity := reset_gravity, p_required_checkpoint := required_checkpoint ) -> void:
	position = p_position
	rotation = p_rotation
	respawn_transform = p_respawn_transform
	radius = p_radius
	reset_gravity = p_reset_gravity
	required_checkpoint = p_required_checkpoint
	changed.connect(cache_properties)

func return_duplicate() -> Checkpoint:
	var new_checkpoint := Checkpoint.new()
	new_checkpoint.position = position
	new_checkpoint.rotation = rotation
	new_checkpoint.respawn_transform = respawn_transform
	new_checkpoint.radius = radius
	new_checkpoint.reset_gravity = reset_gravity
	new_checkpoint.required_checkpoint = required_checkpoint
	return new_checkpoint

func cache_properties() -> void:
	checkpoint_orientation = Quaternion.from_euler(rotation)
	checkpoint_basis = Basis.from_euler(rotation)
	checkpoint_plane = Plane(checkpoint_basis.z, position)


func debug_draw_checkpoint(delta : float) -> void:
	var cp_transform : Transform3D = Transform3D(Basis(Quaternion.from_euler(rotation)), position)
	cp_transform.basis = cp_transform.basis.rotated(cp_transform.basis.x, PI * 0.5)
	var use_color := Color.GREEN
	var draw_radius := radius
	if required_checkpoint:
		use_color = Color.BLUE
	for i in 8:
		var origin_pos := cp_transform.origin + cp_transform.basis.z * radius * sin(0.125 * i * PI * 2) + cp_transform.basis.x * radius * cos(0.125 * i * PI * 2)
		DebugDraw3D.draw_arrow_line(origin_pos, origin_pos + checkpoint_plane.normal * 10, use_color, 2, true, delta)
		var origin_pos_2 := cp_transform.origin + cp_transform.basis.z * radius * sin(0.125 * (i + 4) * PI * 2) + cp_transform.basis.x * radius * cos(0.125 * (i + 4) * PI * 2)
		DebugDraw3D.draw_line(origin_pos, origin_pos_2, use_color, delta)
	cp_transform.basis.z *= radius
	cp_transform.basis.x *= radius
	cp_transform.basis.y *= 0.01
	DebugDraw3D.draw_cylinder(cp_transform, use_color, delta)
	
	var box_origin := respawn_transform.origin + respawn_transform.basis.y * 0.5
	DebugDraw3D.draw_box_xf(respawn_transform.translated_local(Vector3(0, 0.5, 0)), Color.GREEN, true, delta)
	DebugDraw3D.draw_arrow_line(box_origin, box_origin + respawn_transform.basis.x * 4, Color.RED, 1.0, true, delta)
	DebugDraw3D.draw_arrow_line(box_origin, box_origin + respawn_transform.basis.y * 4, Color.GREEN, 1.0, true, delta)
	DebugDraw3D.draw_arrow_line(box_origin, box_origin + respawn_transform.basis.z * 4, Color.BLUE, 1.0, true, delta)
