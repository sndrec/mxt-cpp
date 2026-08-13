extends SceneTree

const CommunicationControllerClass = preload("res://ui/communication_controller.gd")

func _fail(message: String) -> void:
	push_error("MXT_TEXT_CHAT_HISTORY_SMOKE_FAIL " + message)
	quit(1)

func _chat_label_ids(overlay: RaceCommunicationOverlay) -> Array[int]:
	var ids: Array[int] = []
	for child in overlay.history_messages.get_children():
		ids.append(child.get_instance_id())
	ids.sort()
	return ids

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	var controller: CommunicationControllerClass = game_manager.communication_controller
	if game_manager.has_method("_append_text_chat_message") or game_manager.has_method("_send_text_chat_message_to_server"):
		_fail("GameManager should not retain text chat implementation")
		return

	game_manager.lobby_control.visible = false
	for i in range(512):
		controller.append_message(1, "hidden message %d" % i)
	if controller.lobby_history.size() != controller.MAX_LOBBY_HISTORY:
		_fail("hidden lobby history was not bounded")
		return
	game_manager.lobby_control.visible = true
	var lobby_text := controller.lobby_box.get_parsed_text()
	if !lobby_text.contains("hidden message 511") or lobby_text.contains("hidden message 0\n"):
		_fail("hidden lobby history did not rebuild to the retained window")
		return

	for i in range(512, 1024):
		controller.append_message(1, "visible message %d" % i)
	if controller.lobby_history.size() != controller.MAX_LOBBY_HISTORY:
		_fail("visible lobby history was not bounded")
		return
	if controller.lobby_rendered_history_size != controller.MAX_LOBBY_HISTORY:
		_fail("visible lobby document fell out of sync")
		return
	lobby_text = controller.lobby_box.get_parsed_text()
	if !lobby_text.contains("visible message 1023") or lobby_text.contains("visible message 512\n"):
		_fail("visible lobby document retained evicted messages")
		return

	var overlay := controller.race_overlay
	overlay.clear_messages()
	for i in range(overlay.MAX_CHAT_HISTORY):
		overlay.append_message(1, "Smoke", "race message %d" % i)
	var pooled_label_ids := _chat_label_ids(overlay)
	for i in range(overlay.MAX_CHAT_HISTORY, 2048):
		overlay.append_message(1, "Smoke", "race message %d" % i)
	if overlay._messages.size() != overlay.MAX_CHAT_HISTORY:
		_fail("race history was not bounded")
		return
	if overlay.history_messages.get_child_count() != overlay.MAX_CHAT_HISTORY:
		_fail("race label pool grew beyond the history cap")
		return
	if _chat_label_ids(overlay) != pooled_label_ids:
		_fail("race labels were allocated after the pool reached capacity")
		return
	for i in range(16):
		overlay.open_chat()
		overlay.close_chat()
	if _chat_label_ids(overlay) != pooled_label_ids:
		_fail("opening and closing chat churned label nodes")
		return
	if overlay.history_scroll.get_mouse_filter_with_override() != Control.MOUSE_FILTER_IGNORE:
		_fail("closed chat history still receives mouse input")
		return
	overlay.open_chat()
	if overlay.chat_input.get_mouse_filter_with_override() == Control.MOUSE_FILTER_IGNORE:
		_fail("open chat input did not restore mouse input")
		return
	overlay.close_chat()
	if overlay.chat_input.get_mouse_filter_with_override() != Control.MOUSE_FILTER_IGNORE:
		_fail("closed chat input still receives mouse input")
		return
	var visible_closed_labels := 0
	for child in overlay.history_messages.get_children():
		if child.visible:
			visible_closed_labels += 1
	if visible_closed_labels > overlay.CLOSED_CHAT_MESSAGE_LIMIT:
		_fail("closed chat rendered more than the recent-message limit")
		return
	if overlay.is_processing():
		_fail("race chat re-enabled per-frame processing")
		return

	var sanitized := controller.sanitize_message("  first\nsecond\tthird  ")
	if sanitized != "first second third":
		_fail("chat sanitizer did not collapse line controls")
		return
	controller.rate_state.clear()
	controller.global_tokens = controller.GLOBAL_BURST_MESSAGES
	controller.global_refill_msec = 0
	for i in range(controller.RATE_MAX_MESSAGES):
		if !controller.rate_limit_allows(1):
			_fail("rate limiter rejected an allowed burst")
			return
	if controller.rate_limit_allows(1):
		_fail("rate limiter accepted a message beyond the burst cap")
		return
	controller.rate_state.clear()
	controller.global_tokens = controller.GLOBAL_BURST_MESSAGES
	controller.global_refill_msec = Time.get_ticks_msec() + 1_000_000
	for sender_id in range(100, 100 + int(controller.GLOBAL_BURST_MESSAGES)):
		if !controller.rate_limit_allows(sender_id):
			_fail("global rate limiter rejected its allowed burst")
			return
	if controller.rate_limit_allows(999):
		_fail("global rate limiter accepted a message beyond its token budget")
		return

	var rpc_config := controller.get_rpc_config()
	for method_name in ["_send_to_server", "_broadcast_message"]:
		var config: Dictionary = rpc_config.get(method_name, {})
		if int(config.get("channel", -1)) != 8 or int(config.get("transfer_mode", -1)) != MultiplayerPeer.TRANSFER_MODE_RELIABLE:
			_fail("chat RPC channel or reliability changed for %s" % method_name)
			return

	print("MXT_TEXT_CHAT_HISTORY_SMOKE_PASS lobby_history=", controller.lobby_history.size(),
		" race_history=", overlay._messages.size(), " pooled_labels=", pooled_label_ids.size())
	game_manager.queue_free()
	await process_frame
	quit(0)
