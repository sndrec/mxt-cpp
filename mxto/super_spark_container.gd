class_name SuperSparkContainer
extends Node3D

const SPARK_COUNT := 256
var _sparks: Array[Node3D] = []
var spark_scene := preload("res://asset/obj_superspark.tscn")

func _ready() -> void:
	_sparks.clear()
	if spark_scene == null:
		return

	# Ensure we have a predictable number of children in a stable order.
	for child in get_children():
		if child is Node3D:
			child.visible = false
			_sparks.append(child)

	while _sparks.size() < SPARK_COUNT:
		var spark := spark_scene.instantiate()
		if spark is Node3D:
			spark.visible = false
			add_child(spark)
			_sparks.append(spark)
		else:
			spark.queue_free()
			break

func get_spark_nodes() -> Array[Node3D]:
	return _sparks.duplicate()
