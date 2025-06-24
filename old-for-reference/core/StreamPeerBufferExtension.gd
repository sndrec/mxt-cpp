class_name StreamPeerBufferExtension extends StreamPeerBuffer

func get_vector3() -> Vector3:
	return Vector3(get_float(), get_float(), get_float())

func put_vector3(inVector : Vector3) -> void:
	put_float(inVector.x)
	put_float(inVector.y)
	put_float(inVector.z)

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
