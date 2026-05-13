class_name Checkpoint extends Resource

@export var position_start : Vector3
@export var position_end : Vector3
@export var orientation_start : Basis
@export var orientation_end : Basis
@export var x_radius_start : float
@export var x_radius_end : float
@export var y_radius_start : float
@export var y_radius_end : float
@export var y_start : float
@export var y_end : float
@export var distance : float
@export var road_segment : int
@export var start_plane : Plane
@export var end_plane : Plane

func _init(
	p_position_start : Vector3 = Vector3.ZERO,
	p_position_end : Vector3 = Vector3.ZERO,
	p_orientation_start : Basis = Basis.IDENTITY,
	p_orientation_end : Basis = Basis.IDENTITY,
	p_x_radius_start : float = 0,
	p_x_radius_end : float = 0,
	p_y_radius_start : float = 0,
	p_y_radius_end : float = 0,
	p_y_start : float = 0,
	p_y_end : float = 0,
	p_distance : float = 0,
	p_road_segment : int = 0):
		
	position_start = p_position_start
	orientation_start = p_orientation_start
	x_radius_start = p_x_radius_start
	x_radius_end = p_x_radius_end
	position_end = p_position_end
	orientation_end = p_orientation_end
	y_radius_start = p_y_radius_start
	y_radius_end = p_y_radius_end
	y_start = p_y_start
	y_end = p_y_end
	distance = p_distance
	road_segment = p_road_segment
	start_plane = Plane(orientation_start.z, position_start)
	end_plane = Plane(orientation_end.z, position_end)
