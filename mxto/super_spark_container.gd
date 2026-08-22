class_name SuperSparkContainer
extends Node3D

const SPARK_COUNT := 256
const SPARK_NODE_NAME := "SparkMultiMesh"
const COLLISION_SPARK_COUNT := 512
const COLLISION_SPARK_NODE_NAME := "CollisionSparkMultiMesh"

var spark_texture := preload("res://asset/tex/superspark.png")
var collision_spark_texture := preload("res://asset/tex/collision_spark.png")
var collision_spark_shader := preload("res://asset/effect/collision_spark.gdshader")

func _ready() -> void:
	_ensure_super_spark_multimesh()
	_ensure_collision_spark_multimesh()


func _ensure_super_spark_multimesh() -> void:
	var spark_multimesh := get_node_or_null(SPARK_NODE_NAME) as MultiMeshInstance3D
	if spark_multimesh != null:
		return
	if spark_texture == null:
		return

	var quad_mesh := QuadMesh.new()
	var tex_size := spark_texture.get_size()
	quad_mesh.size = Vector2(maxf(0.25, tex_size.x * 0.01), maxf(0.25, tex_size.y * 0.01))

	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.albedo_texture = spark_texture

	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.instance_count = SPARK_COUNT
	multimesh.visible_instance_count = 0
	multimesh.mesh = quad_mesh

	spark_multimesh = MultiMeshInstance3D.new()
	spark_multimesh.name = SPARK_NODE_NAME
	spark_multimesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	spark_multimesh.material_override = material
	spark_multimesh.multimesh = multimesh
	add_child(spark_multimesh)


func _ensure_collision_spark_multimesh() -> void:
	var spark_multimesh := get_node_or_null(COLLISION_SPARK_NODE_NAME) as MultiMeshInstance3D
	if spark_multimesh != null:
		return
	if collision_spark_texture == null or collision_spark_shader == null:
		return

	var quad_mesh := QuadMesh.new()
	quad_mesh.size = Vector2.ONE

	var material := ShaderMaterial.new()
	material.shader = collision_spark_shader
	material.set_shader_parameter("spark_texture", collision_spark_texture)

	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.use_colors = true
	multimesh.use_custom_data = true
	multimesh.instance_count = COLLISION_SPARK_COUNT
	multimesh.visible_instance_count = 0
	multimesh.mesh = quad_mesh
	# Particle transforms carry camera-space endpoints instead of ordinary world
	# transforms, so give the batch an explicit conservative visibility bound.
	multimesh.custom_aabb = AABB(Vector3(-40000.0, -40000.0, -40000.0), Vector3(80000.0, 80000.0, 80000.0))

	spark_multimesh = MultiMeshInstance3D.new()
	spark_multimesh.name = COLLISION_SPARK_NODE_NAME
	spark_multimesh.top_level = true
	spark_multimesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	spark_multimesh.material_override = material
	spark_multimesh.multimesh = multimesh
	add_child(spark_multimesh)
