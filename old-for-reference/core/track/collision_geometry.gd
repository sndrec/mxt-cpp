extends StaticBody3D
class_name CollisionGeometry

var mesh_data: MeshDataTool

func _ready() -> void:
	var mesh : Mesh = get_parent().mesh
	mesh_data = MeshDataTool.new()
	mesh_data.create_from_surface(mesh, 0)
	var collider : CollisionShape3D = CollisionShape3D.new()
	var new_shape : ConcavePolygonShape3D = ConcavePolygonShape3D.new()
	new_shape.set_faces(mesh.get_faces())
	collider.shape = new_shape
	add_child(collider)

func get_vertex_normals_at_face_index(index: int) -> Array[Vector3]:
	var normals: Array[Vector3] = []
	for i in 3:
		normals.append(global_transform.basis * mesh_data.get_vertex_normal(mesh_data.get_face_vertex(index, i)))
	return normals

func get_vertex_positions_at_face_index(index: int) -> Array[Vector3]:
	var vertices: Array[Vector3] = []
	for i in 3:
		vertices.append(global_transform * mesh_data.get_vertex(mesh_data.get_face_vertex(index, i)))
	return vertices
