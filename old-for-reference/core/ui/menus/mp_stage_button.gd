class_name MPStageButton extends Button

var associated_stage : String

func _pressed() -> void:
	if multiplayer.get_unique_id() == 1:
		MXGlobal.current_race_settings.tracks.append(associated_stage)
		MXGlobal.sync_race_settings.rpc(MXGlobal.current_race_settings.serialize())
		MXGlobal.current_multi_lobby.refresh_stage_list_preview()
