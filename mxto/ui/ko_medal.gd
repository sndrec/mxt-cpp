@tool
extends Control

@onready var killer := $Panel/HBoxContainer/killer
@onready var victim := $Panel/HBoxContainer/victim
var feed_index := 0
var feed_y := 72.0
var feed_tween: Tween

func set_names(in_killer: String, in_victim: String) -> void:
	killer.text = in_killer
	victim.text = in_victim

func set_feed_index(index: int) -> void:
	feed_index = index
	var target_y := 72.0 + float(feed_index) * 34.0
	if !is_inside_tree():
		feed_y = target_y
		return
	if feed_tween != null:
		feed_tween.kill()
	feed_tween = create_tween()
	feed_tween.set_trans(Tween.TRANS_SINE)
	feed_tween.set_ease(Tween.EASE_IN_OUT)
	feed_tween.tween_property(self, "feed_y", target_y, 0.22)

func _process(_delta: float) -> void:
	size.x = killer.size.x + victim.size.x + 64
	pivot_offset = size * 0.5
	var viewport_size := get_viewport_rect().size
	position.x = viewport_size.x * 0.5 - size.x * 0.5
	position.y = feed_y
