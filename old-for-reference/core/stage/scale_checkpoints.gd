@tool
extends EditorScript

var only_scale_objs := false

func _run() -> void:
	if only_scale_objs:
		for selected in get_editor_interface().get_selection().get_selected_nodes():
			if selected is BoostPad:
				selected.scale *= 2.5
			if selected is JumpPlate:
				selected.scale *= 2.5
	else:
		for selected in get_editor_interface().get_selection().get_selected_nodes():
			if selected is CheckpointController:
				selected.origin_handle.global_position *= 2.5
				selected.orientation_handle.global_position *= 2.5
				selected.radius_handle.global_position *= 2.5
				selected.respawn_handle.global_position *= 2.5
			if selected is DynamicFinishLine:
				selected.global_position *= 2.5
				selected.width *= 2.5
			if selected is BoostPad:
				selected.global_position *= 2.5
				selected.scale *= 2.5
			if selected is JumpPlate:
				selected.global_position *= 2.5
				selected.scale *= 2.5
			if selected is SpawnPoint:
				selected.global_position *= 2.5
