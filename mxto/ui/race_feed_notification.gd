@tool
extends Control

enum Kind {
	FINISH,
	KNOCKOUT,
}

@onready var leading_label: Label = $Panel/HBoxContainer/Leading
@onready var knockout_icon: TextureRect = $Panel/HBoxContainer/KnockoutIcon
@onready var trailing_label: Label = $Panel/HBoxContainer/Trailing

var kind := Kind.FINISH
var feed_index := 0
var feed_y := 72.0
var feed_tween: Tween
var dismissing := false

func configure_finish(player_name: String, race_time: String) -> void:
	kind = Kind.FINISH
	leading_label.text = player_name + " finished the race at " + race_time
	knockout_icon.visible = false
	trailing_label.visible = false

func configure_knockout(attacker_name: String, victim_name: String) -> void:
	kind = Kind.KNOCKOUT
	leading_label.text = attacker_name
	knockout_icon.visible = true
	trailing_label.visible = true
	trailing_label.text = victim_name

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

func dismiss() -> void:
	if dismissing:
		return
	dismissing = true
	if feed_tween != null:
		feed_tween.kill()
	var tween := create_tween()
	tween.set_parallel(true)
	tween.set_trans(Tween.TRANS_SINE)
	tween.set_ease(Tween.EASE_IN_OUT)
	tween.tween_property(self, "modulate:a", 0.0, 0.18)
	tween.tween_property(self, "scale", Vector2(1.4, 1.4), 0.18)
	tween.set_parallel(false)
	tween.tween_callback(queue_free)

func _process(_delta: float) -> void:
	if kind == Kind.FINISH:
		size.x = leading_label.size.x + 32.0
	else:
		size.x = leading_label.size.x + trailing_label.size.x + 64.0
	pivot_offset = size * 0.5
	var viewport_size := get_viewport_rect().size
	position.x = viewport_size.x * 0.5 - size.x * 0.5
	position.y = feed_y
