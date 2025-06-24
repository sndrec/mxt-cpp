class_name MultiPlayerListButton extends Button

var associated_peer : PeerData

func _process( _delta:float ) -> void:
	if associated_peer and is_instance_valid(associated_peer) and associated_peer.player_settings:
		if associated_peer.team == 0:
			text = associated_peer.player_settings.username
		elif associated_peer.team == 1:
			text = associated_peer.player_settings.username + " (Spec)"

func _pressed() -> void:
	if associated_peer.team == 0:
		associated_peer.set_team.rpc(1)
	else:
		associated_peer.set_team.rpc(0)

@rpc("any_peer", "call_local", "reliable")
func _delete_button() -> void:
	queue_free()
