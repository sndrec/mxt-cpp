extends Control

func set_player_name(inName : String) -> void:
	%NameLabel.text = inName

func set_player_ping(inPing : float) -> void:
	%PingLabel.text = str(inPing) + "ms"
