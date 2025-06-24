class_name MPStageListPreviewEntry extends Button

var associated_index : int

func _pressed() -> void:
	MXGlobal.current_race_settings.tracks.remove_at(associated_index)
	MXGlobal.current_multi_lobby.refresh_stage_list_preview()
