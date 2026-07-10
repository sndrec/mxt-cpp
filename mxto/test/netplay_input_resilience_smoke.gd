extends SceneTree

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _init() -> void:
	var nm := NetworkManager.new()
	var first_window := nm._input_forward_window(nm.STARTUP_LIGHT_NET_TICKS)
	if first_window != Vector2i(nm.STARTUP_LIGHT_NET_TICKS, 1):
		_fail("first gameplay input window was incorrect: %s" % first_window)
		return
	var full_window := nm._input_forward_window(nm.STARTUP_LIGHT_NET_TICKS + 40)
	if full_window.y != nm.INPUT_FORWARD_REDUNDANCY_TICKS or full_window.x != nm.STARTUP_LIGHT_NET_TICKS + 40 - nm.INPUT_FORWARD_REDUNDANCY_TICKS + 1:
		_fail("rolling forward input window was incorrect: %s" % full_window)
		return
	nm.last_ack_tick = nm.STARTUP_LIGHT_NET_TICKS + 40
	if nm._input_forward_window(nm.STARTUP_LIGHT_NET_TICKS + 40) != full_window:
		_fail("server acknowledgement changed proactive input redundancy")
		return

	nm.is_server = false
	nm.listen_server = false
	nm._record_rtt_sample(0.050)
	var stable_ahead := nm.desired_ahead_ticks
	for sample in [0.140, 0.045, 0.135, 0.050]:
		nm._record_rtt_sample(sample)
	if nm.rtt_variance_s <= 0.0 or nm.desired_ahead_ticks <= stable_ahead:
		_fail("RTT variance did not add lead for a jittery peer: rtt=%f variance=%f ahead=%f stable=%f" % [nm.rtt_s, nm.rtt_variance_s, nm.desired_ahead_ticks, stable_ahead])
		return
	if nm.desired_ahead_ticks > nm.MAX_AHEAD_TICKS:
		_fail("jitter lead exceeded the existing maximum ahead bound")
		return

	print("MXT_NETPLAY_INPUT_RESILIENCE_OK")
	quit(0)
