extends Control

@onready var placement_container := $Panel/MarginContainer/VSplitContainer/PlacementContainer
@onready var h_box_container := $Panel/MarginContainer/VSplitContainer/PlacementContainer/HBoxContainer

func _ready() -> void:
	RaceSession.race_session_updated.connect(update_placement_hud)
	#update_placement_hud()

func update_placement_hud() -> void:
	if !is_instance_valid(MXGlobal.currentStageOverseer):
		return
	if !(multiplayer and multiplayer.get_peers().size() > 0):
		return
	
	await get_tree().create_timer(1.0).timeout
	
	for child in placement_container.get_children():
		if child != h_box_container:
			child.queue_free()
	var all_won := true
	for player in MXGlobal.currentStageOverseer.players:
		if (player.controlledPawn.machine_state & MXRacer.FZ_MS.COMPLETEDRACE_2_Q) == 0:
			all_won = false
	if !all_won:
		return
	visible = true
	#var player_list := MXGlobal.currentStageOverseer.players
	var placement_ordered_player_list := MXGlobal.currentStageOverseer.players.duplicate()
	placement_ordered_player_list.sort_custom( func(a:ROPlayer, b:ROPlayer) -> bool:
		var peer_a := a.get_parent() as PeerData
		var peer_b := b.get_parent() as PeerData
		var point_index_of_a := RaceSession.peer_ids.find(peer_a.id)
		var point_index_of_b := RaceSession.peer_ids.find(peer_b.id)
		return RaceSession.point_totals[point_index_of_a] > RaceSession.point_totals[point_index_of_b]
		)
	for i in placement_ordered_player_list.size():
		var pl := placement_ordered_player_list[i] as ROPlayer
		var peer_pl := pl.get_parent() as PeerData
		var point_index_of_pl := RaceSession.peer_ids.find(peer_pl.id)
		var total := RaceSession.point_totals[point_index_of_pl]
		var username:String = pl.get_parent().player_settings.username
		var label_name := Label.new()
		label_name.text = username
		var label_points := Label.new()
		label_points.text = str(total)
		label_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		label_points.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
		label_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		label_points.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var new_hbox_container := HBoxContainer.new()
		placement_container.add_child(new_hbox_container)
		new_hbox_container.add_child(label_name)
		new_hbox_container.add_child(label_points)
