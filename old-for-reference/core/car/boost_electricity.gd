@tool
class_name BoostElectricity extends Node3D

@export var bounds : AABB = AABB(Vector3.UP, Vector3.ONE)
@export var ground : Plane = Plane(Vector3.UP, Vector3.ZERO)
@export var boosting := false

# New exports for mesh appearance
@export var electricity_color: Color = Color.CYAN
@export var electricity_width: float = 0.05 # Adjusted for a thinner, more "electric" look

var old_transform := Transform3D.IDENTITY
var tendrils : Array[PackedVector3Array] = []
var tendril_start_times : PackedFloat32Array = []
var tendril_index = 0
var last_tendril := 0.0
var tendril_lifetime := 0.25
var queued_tendrils := 0

@onready var mesh_instance: MeshInstance3D = $MeshInstance3D
var array_mesh: ArrayMesh
var electricity_material: ShaderMaterial

func _ready() -> void:
	tendrils.resize(64)
	tendril_start_times.resize(64)
	array_mesh = mesh_instance.mesh
	electricity_material = mesh_instance.material_override


func calculate_electricity(delta: float, in_tr : Transform3D) -> void:
	var cur_time := 0.001 * Time.get_ticks_msec()

	# Update material color if changed in editor
	if is_instance_valid(electricity_material):
		pass
	
	if Engine.is_editor_hint():
		var mins := bounds.position - bounds.end
		var maxs := bounds.position + bounds.end
		DebugDraw3D.draw_aabb_ab(global_transform * mins, global_transform * maxs, Color.RED, delta)

	var difference := in_tr.origin - old_transform.origin
	
	# --- Tendril point update logic (UNCHANGED) ---
	for n in tendrils.size():
		# Check if tendril data exists and is not older than 0.25s for this specific logic part
		if tendrils[n] != null and tendrils[n].size() > 0 and cur_time - tendril_start_times[n] < tendril_lifetime: # Ensure tendril is active for this update
			var tendril_path := tendrils[n] # Use a clearer name
			for i in tendril_path.size():
				var ratio := float(i) / (tendril_path.size() - 1)
				var inv_ratio := 1.0 - ratio # Corrected inv_ratio
				var diff_interped := difference * (inv_ratio * 0.333 + 0.666)
				# var sloped_ratio := minf(ratio, inv_ratio) * 2.0 # This was in original, ensure it's used if intended for point update
				tendril_path[i] = tendril_path[i] + diff_interped
				if i > 0 and i < tendril_path.size() - 1:
					var sloped_ratio := minf(ratio, inv_ratio) * 2.0 # Apply sloped_ratio here for jitter
					tendril_path[i] += in_tr.basis * Vector3(randf_range(-0.5, 0.5), randf_range(-0.2, 1.5), randf_range(-0.5, 0.5)) * delta * 8 * sloped_ratio
	
	# --- Tendril generation logic (UNCHANGED) ---
	if boosting:
		var num_tendrils_to_spawn := maxf(1.0, remap(tendril_lifetime, 0.25, 0.05, 1.0, 3.0))
		queued_tendrils += num_tendrils_to_spawn
		for n in floori(queued_tendrils):
			queued_tendrils -= 1.0
			last_tendril = cur_time
			var new_tendril : PackedVector3Array = []
			new_tendril.resize(8)
			var random_start := Vector3(randf_range(-bounds.size.x, bounds.size.x), randf_range(-bounds.size.y, bounds.size.y), randf_range(-bounds.size.z, bounds.size.z))
			var gbx := in_tr.basis.x
			var random_end := ground.project((gbx * signf(random_start.x)).normalized() * randf_range(2.0, 4.0) + in_tr.origin + in_tr.basis.z * random_start.z * 2.0)
			for i in new_tendril.size():
				var ratio := float(i) / (new_tendril.size() - 1)
				var inv_ratio := 1.0 - ratio # Corrected inv_ratio
				var sloped_ratio := minf(ratio, inv_ratio) * 2.0 # Apply sloped_ratio here for jitter
				new_tendril[i] = (in_tr * random_start).lerp(random_end, ratio) + ground.normal * sloped_ratio * 3.0
				if i > 0 and i < new_tendril.size() - 1:
					new_tendril[i] += Vector3(randf_range(-0.333, 0.333), randf_range(-0.333, 0.333), randf_range(-0.333, 0.333))
			tendrils[tendril_index] = new_tendril
			tendril_start_times[tendril_index] = cur_time
			tendril_index = (tendril_index + 1) % 63
	
	# --- Mesh Generation ---
	var vertices = PackedVector3Array()
	var uvs = PackedVector2Array()
	var uvs2 = PackedVector2Array()
	var colors = PackedColorArray()
	var indices = PackedInt32Array()
	var current_vertex_index = 0

	var camera: Camera3D = get_viewport().get_camera_3d()# if !Engine.is_editor_hint() else EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
	var camera_global_origin = Vector3.ZERO 
	if is_instance_valid(camera): # Check if camera is valid
		camera_global_origin = camera.global_transform.origin
	else: # Fallback if no camera
		if Engine.is_editor_hint(): # In editor, try to use a simulated view
			camera_global_origin = in_tr.origin + Vector3.UP * 2.0 - in_tr.basis.z * 5.0 # Arbitrary view
		# else runtime, could use a fixed offset or a warning

	for i in tendrils.size():
		var age = cur_time - tendril_start_times[i]
		# Use 0.5s for the full lifetime of the visual mesh tendril
		if age < tendril_lifetime: 
			var life_ratio := clampf(remap(age, 0.0, tendril_lifetime, 1.0, 0.0), 0.0, 1.0)
			if life_ratio <= 0.001: # Skip if effectively invisible
				continue

			var tendril_points: PackedVector3Array = tendrils[i]
			if tendril_points == null or tendril_points.size() < 2:
				continue
			var current_tendril_color = electricity_color
			current_tendril_color.a = life_ratio # Fade alpha with life_ratio
			# Width can also diminish with age for a sparking effect
			var current_segment_width = electricity_width * life_ratio 
			for p_idx in range(tendril_points.size() - 1):
				var p_start = tendril_points[p_idx]
				var p_end = tendril_points[p_idx+1]
				var segment_vector = p_end - p_start
				if segment_vector.length_squared() < 0.00001: # Skip zero-length segments
					continue
				
				var segment_direction = segment_vector.normalized()
				var segment_midpoint = (p_start + p_end) * 0.5
				
				var to_camera_vector = camera_global_origin - segment_midpoint
				if to_camera_vector.length_squared() < 0.00001: # Camera is at midpoint or vector is zero
					if is_instance_valid(camera):
						to_camera_vector = -camera.global_transform.basis.z # Use camera's forward direction
					else: # Further fallback
						to_camera_vector = -(in_tr.origin - segment_midpoint).normalized()
				var ribbon_right_vector = segment_direction.cross(to_camera_vector).normalized()
				# Handle case where segment_direction is (anti-)parallel to to_camera_vector
				if ribbon_right_vector.length_squared() < 0.1: # Cross product result is near zero vector
					var fallback_dir = Vector3.UP
					if abs(segment_direction.dot(fallback_dir)) > 0.99:
						fallback_dir = Vector3.RIGHT
						if abs(segment_direction.dot(fallback_dir)) > 0.99:
							fallback_dir = Vector3.FORWARD
					ribbon_right_vector = segment_direction.cross(fallback_dir).normalized()
				
				# Define the four vertices of the quad for this segment
				var v_s_left = p_start - ribbon_right_vector * current_segment_width * 0.5
				var v_s_right = p_start + ribbon_right_vector * current_segment_width * 0.5
				var v_e_left = p_end - ribbon_right_vector * current_segment_width * 0.5
				var v_e_right = p_end + ribbon_right_vector * current_segment_width * 0.5
				
				vertices.append(v_s_left)    # Index + 0
				vertices.append(v_s_right)   # Index + 1
				vertices.append(v_e_left)    # Index + 2
				vertices.append(v_e_right)   # Index + 3
				
				# UVs: U goes across the ribbon, V goes along the tendril's length
				var v_coord_start = float(p_idx) / (tendril_points.size() - 1.0)
				var v_coord_end = float(p_idx + 1) / (tendril_points.size() - 1.0)
				
				
				uvs.append(Vector2(0, v_coord_start)) # For v_s_left
				uvs.append(Vector2(1, v_coord_start)) # For v_s_right
				uvs.append(Vector2(0, v_coord_end))   # For v_e_left
				uvs.append(Vector2(1, v_coord_end))   # For v_e_right
				
				uvs2.append(Vector2(age, 0.0))
				uvs2.append(Vector2(age, 0.0))
				uvs2.append(Vector2(age, 0.0))
				uvs2.append(Vector2(age, 0.0))
				
				colors.append(current_tendril_color)
				colors.append(current_tendril_color)
				colors.append(current_tendril_color)
				colors.append(current_tendril_color)
				
				# Triangle 1: (v_s_left, v_s_right, v_e_left)
				indices.append(current_vertex_index + 0)
				indices.append(current_vertex_index + 1)
				indices.append(current_vertex_index + 2)
				# Triangle 2: (v_e_left, v_s_right, v_e_right)
				indices.append(current_vertex_index + 2)
				indices.append(current_vertex_index + 1)
				indices.append(current_vertex_index + 3)
				
				current_vertex_index += 4
	
	array_mesh.clear_surfaces()

	if vertices.size() > 0 and indices.size() > 0:
		var surface_arrays = []
		surface_arrays.resize(Mesh.ARRAY_MAX)
		surface_arrays[Mesh.ARRAY_VERTEX] = vertices
		surface_arrays[Mesh.ARRAY_TEX_UV] = uvs
		surface_arrays[Mesh.ARRAY_TEX_UV2] = uvs2
		surface_arrays[Mesh.ARRAY_COLOR] = colors
		surface_arrays[Mesh.ARRAY_INDEX] = indices
		
		array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, surface_arrays)
	
	mesh_instance.global_transform = Transform3D.IDENTITY
	old_transform = in_tr
