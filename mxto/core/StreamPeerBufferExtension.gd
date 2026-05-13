class_name StreamPeerBufferExtension extends StreamPeerBuffer

func get_vector3() -> Vector3:
	return Vector3(get_float(), get_float(), get_float())

func put_vector3(inVector : Vector3) -> void:
	put_float(inVector.x)
	put_float(inVector.y)
	put_float(inVector.z)

func put_quaternion(in_quat : Quaternion) -> void:
	put_float(in_quat.x)
	put_float(in_quat.y)
	put_float(in_quat.z)
	put_float(in_quat.w)

func get_basis() -> Basis:
	return Basis(get_vector3(), get_vector3(), get_vector3())

func put_basis(inBasis : Basis) -> void:
	put_vector3(inBasis.x)
	put_vector3(inBasis.y)
	put_vector3(inBasis.z)

func get_transform() -> Transform3D:
	return Transform3D(get_basis(), get_vector3())

func put_transform(inTransform : Transform3D) -> void:
	put_basis(inTransform.basis)
	put_vector3(inTransform.origin)

func put_track_editor_curve(in_curve : Resource) -> void:
	if in_curve == null:
		put_u32(0)
		return
	var packet : PackedFloat32Array = in_curve.build_linear_x_packet() if in_curve.has_method("build_linear_x_packet") else in_curve.build_packet()
	var point_count := int(packet[0])
	put_u32(point_count)
	var cursor := 1
	for point in point_count:
		put_float(packet[cursor])
		put_float(packet[cursor + 1])
		put_float(packet[cursor + 2])
		put_float(packet[cursor + 3])
		cursor += 4

func put_baked_curve_matrix(in_matrix : PackedFloat32Array) -> void:
	var cursor := 0
	for channel in 15:
		if cursor >= in_matrix.size():
			put_u32(0)
			continue
		var point_count := int(in_matrix[cursor])
		cursor += 1
		put_u32(point_count)
		for point in point_count:
			put_float(in_matrix[cursor])
			put_float(in_matrix[cursor + 1])
			put_float(in_matrix[cursor + 2])
			put_float(in_matrix[cursor + 3])
			cursor += 4

func put_checkpoint(in_checkpoint : Checkpoint) -> void:
	put_vector3(in_checkpoint.position_start)
	put_vector3(in_checkpoint.position_end)
	put_basis(in_checkpoint.orientation_start)
	put_basis(in_checkpoint.orientation_end)
	put_float(in_checkpoint.x_radius_start)
	put_float(in_checkpoint.y_radius_start)
	put_float(in_checkpoint.x_radius_end)
	put_float(in_checkpoint.y_radius_end)
	put_float(in_checkpoint.y_start)
	put_float(in_checkpoint.y_end)
	put_float(in_checkpoint.distance)
	put_u32(in_checkpoint.road_segment)
	put_vector3(in_checkpoint.start_plane.normal)
	put_float(in_checkpoint.start_plane.d)
	put_vector3(in_checkpoint.end_plane.normal)
	put_float(in_checkpoint.end_plane.d)

func put_checkpoint_with_neighbors(in_checkpoint : Checkpoint, neighbors : Array[int]) -> void:
	put_checkpoint(in_checkpoint)
	put_u32(neighbors.size())
	for neighbor in neighbors:
		put_u32(neighbor)

func get_checkpoint() -> Checkpoint:
	var new_checkpoint := Checkpoint.new()
	new_checkpoint.position_start = get_vector3()
	new_checkpoint.position_end = get_vector3()
	new_checkpoint.orientation_start = get_basis()
	new_checkpoint.orientation_end = get_basis()
	new_checkpoint.x_radius_start = get_float()
	new_checkpoint.y_radius_start = get_float()
	new_checkpoint.x_radius_end = get_float()
	new_checkpoint.y_radius_end = get_float()
	new_checkpoint.y_start = get_float()
	new_checkpoint.y_end = get_float()
	new_checkpoint.distance = get_float()
	new_checkpoint.road_segment = get_u32()
	new_checkpoint.start_plane = Plane(get_vector3(), get_float())
	new_checkpoint.end_plane = Plane(get_vector3(), get_float())
	return new_checkpoint
	
