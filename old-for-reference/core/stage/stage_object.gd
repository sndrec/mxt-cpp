@tool
class_name RORStageObject extends StaticBody3D

var stageMesh : MeshInstance3D
var stageCol : CollisionShape3D
var mesh_data: MeshDataTool

@export var stageMeshTextures : Dictionary = {}
@export var stageHeightTextures : Dictionary = {}

@export_tool_button("Regenerate Mesh Texture Data", "Callable")
var regen_action = regenerate_mesh_texture_data

func regenerate_mesh_texture_data() -> void:
	stageMeshTextures = await inject_triangle_data()
	for i in stageMesh.mesh.get_surface_count():
		var mat := ShaderMaterial.new()
		mat.shader = preload("res://content/base/material/road_shader_smooth.gdshader")
		mat.set_shader_parameter("height_tex", stageMeshTextures.height[i])
		mat.set_shader_parameter("tangent_tex", stageMeshTextures.tangent[i])
		mat.set_shader_parameter("normal_tex", stageMeshTextures.normal[i])
		mat.set_shader_parameter("albedo_map", preload("res://content/base/texture/stagetex/checkerboard.png"))
		#mat.set_shader_parameter("tri_tex_size", stageMeshTextures[i].get_size())
		stageMesh.mesh.surface_set_material(i, mat)

enum object_types {
	NONE,
	PITSTOP,
	ICE,
	DIRT,
	RAIL,
	LAVA,
	DASH,
	JUMP
}

@export var object_type : object_types = object_types.NONE
@export var enableCollision : bool = true
@export var enableTrigger : bool = false
@export var enableBackfaceCollisions : bool = false:
	set(new_bool):
		enableBackfaceCollisions = new_bool
		if has_node("ObjCol") and stageCol:
			stageCol.shape.backface_collision = new_bool

var vertex_normals : Array[PackedVector3Array] = []
var vertex_positions : Array[PackedVector3Array] = []
var road_info : Array[PackedVector3Array] = []

#func inject_triangle_data() -> Dictionary:
	#var orig_mesh = stageMesh.mesh
	#if not orig_mesh or not orig_mesh is ArrayMesh:
		#if orig_mesh is PrimitiveMesh:
			#var arrays = orig_mesh.get_mesh_arrays()
			#orig_mesh = ArrayMesh.new()
			#orig_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		#else:
			#push_error("inject_triangle_data: MeshInstance3D.mesh must be an ArrayMesh.")
			#return {}
#
	#var new_mesh = ArrayMesh.new()
	#var height_map := {}
	#var surface_count = orig_mesh.get_surface_count()
	## process each surface
	#for surface_idx in surface_count:
		#var arrays = orig_mesh.surface_get_arrays(surface_idx)
		#var verts = arrays[Mesh.ARRAY_VERTEX]
		#var normals = arrays[Mesh.ARRAY_NORMAL]
		#var uvs = arrays[Mesh.ARRAY_TEX_UV]
		#var indices = arrays[Mesh.ARRAY_INDEX]
		#var colors = arrays[Mesh.ARRAY_COLOR]
		#if !colors:
			#colors = PackedColorArray()
			#colors.resize(verts.size())
#
		## prepare new arrays
		#var new_verts = PackedVector3Array()
		#var new_normals = PackedVector3Array()
		#var new_uvs = PackedVector2Array()
		#var new_uv2 = PackedVector2Array()
		#var new_indices = PackedInt32Array()
		#var new_color = PackedColorArray()
#
		## create DataTexture for triangle data
		#var tri_count = 0
		#var width = int(indices.size() / 3)
		#var height = 9
		#var img = Image.create_empty(width, height, false, Image.FORMAT_RGBAF)
#
		## fill mesh & image
		#for i in range(0, indices.size(), 3):
			## triangle vertex indices
			#var i0 = indices[i]
			#var i1 = indices[i+1]
			#var i2 = indices[i+2]
#
			## positions & normals
			#var p0 = verts[i0]
			#var p1 = verts[i1]
			#var p2 = verts[i2]
			#
			#var n0 = normals[i0]
			#var n1 = normals[i1]
			#var n2 = normals[i2]
			#
			#var c0 = colors[i0]
			#var c1 = colors[i1]
			#var c2 = colors[i2]
			#
			#var uv0 = uvs[i0]
			#var uv1 = uvs[i1]
			#var uv2 = uvs[i2]
#
			## write 5 texels: p0,p1,p2,n0,n1
			#img.set_pixel(tri_count, 0, Color(p0.x, p0.y, p0.z, 1))
			#img.set_pixel(tri_count, 1, Color(p1.x, p1.y, p1.z, 1))
			#img.set_pixel(tri_count, 2, Color(p2.x, p2.y, p2.z, 1))
			#img.set_pixel(tri_count, 3, Color(n0.x, n0.y, n0.z, 1))
			#img.set_pixel(tri_count, 4, Color(n1.x, n1.y, n1.z, 1))
			#img.set_pixel(tri_count, 5, Color(n2.x, n2.y, n2.z, 1))
			#img.set_pixel(tri_count, 6, Color(uv0.x, uv0.y, 0.0, 1.0) * 4.0)
			#img.set_pixel(tri_count, 7, Color(uv1.x, uv1.y, 0.0, 1.0) * 4.0)
			#img.set_pixel(tri_count, 8, Color(uv2.x, uv2.y, 0.0, 1.0) * 4.0)
#
			## mesh data: vertices
			#new_verts.append(p0)
			#new_verts.append(p1)
			#new_verts.append(p2)
			## normals
			#new_normals.append(n0)
			#new_normals.append(n1)
			#new_normals.append(n2)
			## uvs (fallback)
			#new_uvs.append(uvs[i0] if i0 < uvs.size() else Vector2.ZERO)
			#new_uvs.append(uvs[i1] if i1 < uvs.size() else Vector2.ZERO)
			#new_uvs.append(uvs[i2] if i2 < uvs.size() else Vector2.ZERO)
			## barycentrics in UV2: (1,0),(0,1),(0,0)
			#new_uv2.append(Vector2(1,0))
			#new_uv2.append(Vector2(0,1))
			#new_uv2.append(Vector2(0,0))
			## triangle index in CUSTOM0
			#new_color.append(c0)
			#new_color.append(c1)
			#new_color.append(c2)
			#
			#new_indices.append(i)
			#new_indices.append(i + 1)
			#new_indices.append(i + 2)
			#
			#tri_count += 1
			#
			#if tri_count % 64 == 0:
				#print("processed " + str(tri_count) + " triangles!")
				#print(img)
				#print(img.get_size())
				#await get_tree().process_frame
#
		#var tex = ImageTexture.create_from_image(img)
		#img.save_exr("res://debug/img")
		#height_map[surface_idx] = tex
		## build arrays and add to new mesh
		#var new_arrays = []
		#new_arrays.resize(Mesh.ARRAY_MAX)
		#new_arrays[Mesh.ARRAY_VERTEX] = new_verts
		#new_arrays[Mesh.ARRAY_NORMAL] = new_normals
		#new_arrays[Mesh.ARRAY_TEX_UV] = new_uvs
		#new_arrays[Mesh.ARRAY_TEX_UV2] = new_uv2
		#new_arrays[Mesh.ARRAY_INDEX] = new_indices
		#new_arrays[Mesh.ARRAY_COLOR] = new_color
		#new_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, new_arrays)
#
	## assign mesh
	#stageMesh.mesh = new_mesh
	#return height_map

static func edge_cp(pi:Vector3, pj:Vector3, ni:Vector3) -> Vector3:
	var wi:float = (pj - pi).dot(ni)
	return (pi * 2.0 + pj - wi * ni) / 3.0

static func pn_surface_point(
	u:float, v:float, w:float,
	p0:Vector3, p1:Vector3, p2:Vector3,
	n0:Vector3, n1:Vector3, n2:Vector3
) -> Vector3:
	# early-out / renormalise
	if is_zero_approx(u + v + w):
		return (p0 + p1 + p2) * 0.3333333333
	var s:float = 1.0 / (u + v + w)
	u *= s
	v *= s
	w *= s

	# six edge control points
	var b210:Vector3 = edge_cp(p0, p1, n0)	# near p0 on edge p0-p1
	var b120:Vector3 = edge_cp(p1, p0, n1)	# near p1 on edge p0-p1
	var b021:Vector3 = edge_cp(p1, p2, n1)	# near p1 on edge p1-p2
	var b012:Vector3 = edge_cp(p2, p1, n2)	# near p2 on edge p1-p2
	var b102:Vector3 = edge_cp(p2, p0, n2)	# near p2 on edge p2-p0
	var b201:Vector3 = edge_cp(p0, p2, n0)	# near p0 on edge p2-p0

	# interior control point
	var e:Vector3 = (b210 + b120 + b021 + b012 + b102 + b201) / 6.0
	var vtx_centroid:Vector3 = (p0 + p1 + p2) / 3.0
	var b111:Vector3 = e + (e - vtx_centroid) * 0.5	# = e * 1.5 − vtx_centroid * 0.5

	# Bernstein basis weights
	var uu:float = u * u
	var vv:float = v * v
	var ww:float = w * w

	# cubic Bézier evaluation
	return \
		p0 * uu * u + \
		p1 * vv * v + \
		p2 * ww * w + \
		b210 * (3.0 * uu * v) + \
		b120 * (3.0 * u * vv) + \
		b201 * (3.0 * uu * w) + \
		b102 * (3.0 * u * ww) + \
		b021 * (3.0 * vv * w) + \
		b012 * (3.0 * v * ww) + \
		b111 * (6.0 * u * v * w)

static func phong_surface_point(
	u: float, v: float, w: float,
	p0: Vector3, p1: Vector3, p2: Vector3,
	n0: Vector3, n1: Vector3, n2: Vector3
) -> Vector3:
	if is_zero_approx(u + v + w):
		return (p0 + p1 + p2) * 0.3333333333
	var flat_pos: Vector3 = u * p0 + v * p1 + w * p2
	var plane0 := Plane(n0, p0)
	var plane1 := Plane(n1, p1)
	var plane2 := Plane(n2, p2)
	var proj0: Vector3 = plane0.project(flat_pos)
	var proj1: Vector3 = plane1.project(flat_pos)
	var proj2: Vector3 = plane2.project(flat_pos)
	return (u * proj0 + v * proj1 + w * proj2)



# Helper function to compute barycentric coordinates in UV2 space
func barycentric_coords(p: Vector2, a: Vector2, b: Vector2, c: Vector2) -> Vector3:
	var v0 = b - a
	var v1 = c - a
	var v2 = p - a
	var d00 = v0.dot(v0)
	var d01 = v0.dot(v1)
	var d11 = v1.dot(v1)
	var d20 = v2.dot(v0)
	var d21 = v2.dot(v1)
	var denom = d00 * d11 - d01 * d01
	if denom == 0.0:
		return Vector3(-1, -1, -1)  # Degenerate triangle
	var v = (d11 * d20 - d01 * d21) / denom
	var w = (d00 * d21 - d01 * d20) / denom
	var u = 1.0 - v - w
	return Vector3(u, v, w)

func inject_triangle_data() -> Dictionary:
	var orig_mesh = stageMesh.mesh
	if not orig_mesh or not orig_mesh is ArrayMesh:
		if orig_mesh is PrimitiveMesh:
			var arrays = orig_mesh.get_mesh_arrays()
			orig_mesh = ArrayMesh.new()
			orig_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		else:
			push_error("inject_triangle_data: MeshInstance3D.mesh must be an ArrayMesh.")
			return {}

	var new_mesh = ArrayMesh.new()
	var height_map := {}
	var tangent_map := {}
	var normal_map := {}
	var surface_count = orig_mesh.get_surface_count()
	# process each surface
	for surface_idx in surface_count:
		var arrays = orig_mesh.surface_get_arrays(surface_idx)
		var verts = arrays[Mesh.ARRAY_VERTEX]
		var vert_tan = arrays[Mesh.ARRAY_TANGENT]
		var normals = arrays[Mesh.ARRAY_NORMAL]
		var uvs = arrays[Mesh.ARRAY_TEX_UV]
		var uv2s = arrays[Mesh.ARRAY_TEX_UV2]
		var indices = arrays[Mesh.ARRAY_INDEX]
		var colors = arrays[Mesh.ARRAY_COLOR]
		if !colors:
			colors = PackedColorArray()
			colors.resize(verts.size())

		# prepare new arrays
		var new_verts = PackedVector3Array()
		var new_normals = PackedVector3Array()
		var new_uvs = PackedVector2Array()
		var new_uv2 = PackedVector2Array()
		var new_indices = PackedInt32Array()
		var new_color = PackedColorArray()

		# create DataTexture for triangle data
		var tri_count = 0
		var width = int(indices.size() / 3)
		var height = 9
		var img = Image.create_empty(256, 256, false, Image.FORMAT_RF)
		var nrm_img = Image.create_empty(256, 256, false, Image.FORMAT_RGBF)
		var tan_img = Image.create_empty(256, 256, false, Image.FORMAT_RGBF)

		for i in range(0, indices.size(), 3):
			
			var i0 = indices[i]
			var i1 = indices[i+1]
			var i2 = indices[i+2]

			var p0 = verts[i0]
			var p1 = verts[i1]
			var p2 = verts[i2]
			
			var n0 = normals[i0]
			var n1 = normals[i1]
			var n2 = normals[i2]
			
			var c0 = colors[i0]
			var c1 = colors[i1]
			var c2 = colors[i2]
			
			var uv0 = uvs[i0]
			var uv1 = uvs[i1]
			var uv2 = uvs[i2]
			
			var uv20 = uv2s[i0]
			var uv21 = uv2s[i1]
			var uv22 = uv2s[i2]
			
			#img.set_pixel(0, 0, Color(p0.x, p0.y, p0.z, 1))
			if true:
				DebugDraw3D.draw_arrow_line(p0, p0 + n0, Color.WHITE, 0.25, true, 20)
				DebugDraw3D.draw_arrow_line(p1, p1 + n1, Color.WHITE, 0.25, true, 20)
				DebugDraw3D.draw_arrow_line(p2, p2 + n2, Color.WHITE, 0.25, true, 20)
				var debug_points := []
				var test_subdiv := 50.0
				for n in test_subdiv:
					var ratio := n / (test_subdiv - 1.0)
					debug_points.append(p1.lerp(p0, ratio))
				for n in test_subdiv:
					var ratio := n / (test_subdiv - 1.0)
					debug_points.append(p1.lerp(p2, ratio))
				for n in test_subdiv:
					for p in n:
						var ratio_2 := float(p) / n
						debug_points.append(debug_points[n].lerp(debug_points[n + test_subdiv], ratio_2))
				#DebugDraw3D.draw_sphere(current_position, 0.1, Color.CYAN, 0.016666)
				for dp in debug_points:
					var use_point : Vector3 = dp
					var debug_bary_coords: Vector3 = Geometry3D.get_triangle_barycentric_coords(use_point, p0, p1, p2)
					var debug_point := pn_surface_point(debug_bary_coords.x, debug_bary_coords.y, debug_bary_coords.z,	p0, p1, p2, -n0, -n1, -n2)
					var up_normal : Vector3 = (n0 * debug_bary_coords.x + n1 * debug_bary_coords.y + n2 * debug_bary_coords.z).normalized()
					#DebugDraw3D.draw_sphere(use_point + up_normal * 0.01, 0.02, Color.BLUE, 20)
					DebugDraw3D.draw_sphere(debug_point + up_normal * 0.01, 0.02, Color.RED, 20)
					#DebugDraw3D.draw_line(use_point, debug_point, Color.GREEN, 20)
			
			new_verts.append(p0)
			new_verts.append(p1)
			new_verts.append(p2)
			
			new_normals.append(n0)
			new_normals.append(n1)
			new_normals.append(n2)
			
			new_uvs.append(uvs[i0] if i0 < uvs.size() else Vector2.ZERO)
			new_uvs.append(uvs[i1] if i1 < uvs.size() else Vector2.ZERO)
			new_uvs.append(uvs[i2] if i2 < uvs.size() else Vector2.ZERO)
			
			new_uv2.append(uv2s[i0] if i0 < uv2s.size() else Vector2.ZERO)
			new_uv2.append(uv2s[i1] if i1 < uv2s.size() else Vector2.ZERO)
			new_uv2.append(uv2s[i2] if i2 < uv2s.size() else Vector2.ZERO)
			
			new_color.append(c0)
			new_color.append(c1)
			new_color.append(c2)
			
			new_indices.append(i)
			new_indices.append(i + 1)
			new_indices.append(i + 2)
			
			tri_count += 1
			
			if tri_count % 128 == 0:
				print("processed " + str(tri_count) + " triangles!")
				print(img)
				print(img.get_size())
				await get_tree().process_frame
		# Bake height difference to img using UV2 mapping
		#var tex_width = img.get_width()
		#var tex_height = img.get_height()
		#
		#for y in tex_height:
			#for x in tex_width:
				#var uv2 = Vector2(float(x) / tex_width, float(y) / tex_height)
				#var found = false
				#
				#for i in range(0, indices.size(), 3):
					#var i0 = indices[i]
					#var i1 = indices[i + 1]
					#var i2 = indices[i + 2]
					#
					#var uv20 = uv2s[i0]
					#var uv21 = uv2s[i1]
					#var uv22 = uv2s[i2]
					#
					#var bary = barycentric_coords(uv2, uv20, uv21, uv22)
					#var u = bary.x
					#var v = bary.y
					#var w = bary.z
					#
					#if u >= 0.0 and v >= 0.0 and w >= 0.0:
						#var p0 = verts[i0]
						#var p1 = verts[i1]
						#var p2 = verts[i2]
						#
						#var n0 = normals[i0]
						#var n1 = normals[i1]
						#var n2 = normals[i2]
						#
						#var flat_point = u * p0 + v * p1 + w * p2
						#var phong_point = phong_surface_point(u, v, w, p0, p1, p2, n0, n1, n2)
						#
						#var diff = phong_point - flat_point
						#var avg_normal = (n0 + n1 + n2).normalized()
						#var signed_dist = diff.dot(avg_normal)
						#
						#img.set_pixel(x, y, Color(-signed_dist, 0, 0, 1))
						#var interp_normal = (n0 * u + n1 * v + n2 * w).normalized()
						#
						#var t0 = _get_vertex_tangent(i0, vert_tan)
						#var t1 = _get_vertex_tangent(i1, vert_tan)
						#var t2 = _get_vertex_tangent(i2, vert_tan)
						#
						#var interp_tan = (t0 * u + t1 * v + t2 * w)
						## Gram–Schmidt: force tangent orthogonal to interpolated normal
						##interp_tan = (interp_tan - interp_normal * interp_tan.dot(interp_normal)).normalized()
						#
						## write smoothed normal & tangent into the images
						#nrm_img.set_pixel(x, y, Color(interp_normal.x,
																					#interp_normal.y,
																					#interp_normal.z, 1.0))
						#tan_img.set_pixel(x, y, Color(interp_tan.x,
																					#interp_tan.y,
																					#interp_tan.z, 1.0))
						#found = true
						#break
						#
				#if not found:
					#img.set_pixel(x, y, Color(0, 0, 0, 1))  # fallback background
		#var height_tex = ImageTexture.create_from_image(img)
		#var tangent_tex = ImageTexture.create_from_image(tan_img)
		#var normal_tex = ImageTexture.create_from_image(nrm_img)
		#height_map[surface_idx] = height_tex
		#tangent_map[surface_idx] = tangent_tex
		#normal_map[surface_idx] = normal_tex
		# build arrays and add to new mesh
		var new_arrays = []
		new_arrays.resize(Mesh.ARRAY_MAX)
		new_arrays[Mesh.ARRAY_VERTEX] = new_verts
		new_arrays[Mesh.ARRAY_NORMAL] = new_normals
		new_arrays[Mesh.ARRAY_TEX_UV] = new_uvs
		new_arrays[Mesh.ARRAY_TEX_UV2] = new_uv2
		new_arrays[Mesh.ARRAY_INDEX] = new_indices
		new_arrays[Mesh.ARRAY_COLOR] = new_color
		new_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, new_arrays)
	
	# assign mesh
	stageMesh.mesh = new_mesh
	var final_dictionary : Dictionary = {}
	final_dictionary.height = height_map
	final_dictionary.tangent = tangent_map
	final_dictionary.normal = normal_map
	return final_dictionary

func _get_vertex_tangent(idx:int, tang_store:PackedFloat32Array) -> Vector3:
	var base := idx * 4
	var t   := Vector3(tang_store[base], tang_store[base + 1], tang_store[base + 2])
	var sign := tang_store[base + 3]          # +1 or -1
	return t * sign                           # fold sign into the tangent dir

func _ready() -> void:
	if get_child(0) is MeshInstance3D:
		stageMesh = get_child(0)
	if Engine.is_editor_hint():
		get_parent().set_editable_instance(self, true)
		return
	var mesh : Mesh = get_child(0).mesh
	mesh_data = MeshDataTool.new()
	mesh_data.create_from_surface(mesh, 0)
	var collider : CollisionShape3D = CollisionShape3D.new()
	var new_shape : ConcavePolygonShape3D = ConcavePolygonShape3D.new()
	new_shape.set_faces(mesh.get_faces())
	collider.shape = new_shape
	add_child(collider)
	obj_ready()
	add_to_group("StageObjects")
	for i in mesh_data.get_face_count():
		vertex_normals.append(get_vertex_normals_at_face_index(i))
		vertex_positions.append(get_vertex_positions_at_face_index(i))
		road_info.append(get_road_collision_info(i))
	match object_type:
		object_types.NONE:
			set_collision_mask_value(2, false)
			set_collision_layer_value(2, false)
			set_collision_mask_value(1, true)
			set_collision_layer_value(1, true)
		object_types.RAIL:
			set_collision_mask_value(1, false)
			set_collision_layer_value(1, false)
			set_collision_mask_value(2, true)
			set_collision_layer_value(2, true)
		object_types.JUMP:
			set_collision_mask_value(2, false)
			set_collision_layer_value(2, false)
			set_collision_mask_value(1, true)
			set_collision_layer_value(1, true)
			set_collision_mask_value(3, true)
			set_collision_layer_value(3, true)
		object_types.DASH:
			set_collision_mask_value(2, false)
			set_collision_layer_value(2, false)
			set_collision_mask_value(1, false)
			set_collision_layer_value(1, false)
			set_collision_mask_value(3, true)
			set_collision_layer_value(3, true)
		_:
			set_collision_mask_value(5, true)
			set_collision_layer_value(5, true)

func _post_stage_loaded() -> void:
	pass

func obj_ready() -> void:
	pass

func _process(delta : float) -> void:
	obj_process(delta)
	if Engine.is_editor_hint():
		return
	if !has_node("ObjMesh"):
		return

func obj_process(_delta : float) -> void:
	pass

func evaluate(command:String, variable_names := [], variable_values := []) -> void:
	var expression := Expression.new()
	var error := expression.parse(command, variable_names)
	if error != OK:
		push_error(expression.get_error_text())
		return

	var result:Variant = expression.execute(variable_values, self)

	if not expression.has_execute_failed():
		print(str(result))

func on_trigger( _inBall: DefaultBall, _inFrac : float, _backface : bool) -> void:
	pass

func get_vertex_normals_at_face_index(index: int) -> PackedVector3Array:
	return PackedVector3Array([
		global_transform.basis * mesh_data.get_vertex_normal( mesh_data.get_face_vertex( index, 0 ) ),
		global_transform.basis * mesh_data.get_vertex_normal( mesh_data.get_face_vertex( index, 1 ) ),
		global_transform.basis * mesh_data.get_vertex_normal( mesh_data.get_face_vertex( index, 2 ) ),
	])

func get_vertex_positions_at_face_index(index: int) -> PackedVector3Array:
	return PackedVector3Array([
		global_transform * mesh_data.get_vertex( mesh_data.get_face_vertex( index, 0 ) ),
		global_transform * mesh_data.get_vertex( mesh_data.get_face_vertex( index, 1 ) ),
		global_transform * mesh_data.get_vertex( mesh_data.get_face_vertex( index, 2 ) ),
	])

func get_road_collision_info(index: int) -> PackedVector3Array:
	var fv0 := mesh_data.get_face_vertex( index, 0 )
	var fv1 := mesh_data.get_face_vertex( index, 1 )
	var fv2 := mesh_data.get_face_vertex( index, 2 )
	return PackedVector3Array([
		global_transform * mesh_data.get_vertex( fv0 ),
		global_transform * mesh_data.get_vertex( fv1 ),
		global_transform * mesh_data.get_vertex( fv2 ),
		global_transform.basis * (mesh_data.get_vertex_normal( fv0 )),
		global_transform.basis * (mesh_data.get_vertex_normal( fv1 )),
		global_transform.basis * (mesh_data.get_vertex_normal( fv2 )),
	])

var saveBuffer := StreamPeerBufferExtension.new()
func save_state() -> PackedByteArray:
	saveBuffer.data_array = []
	var byteData := 0
	if enableCollision:
		byteData |= 1
	if enableTrigger:
		byteData |= 2
	saveBuffer.put_u8( byteData )
	return saveBuffer.data_array

func load_state(inData : PackedByteArray) -> void:
	saveBuffer.data_array = inData
	var byteData := saveBuffer.get_u8()
	enableCollision = byteData & 1
	enableTrigger = byteData & 2
