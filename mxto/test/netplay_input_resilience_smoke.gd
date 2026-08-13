extends SceneTree

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _init() -> void:
	var transport := InputTransportController.new()
	var first_window := transport._input_forward_window(transport.STARTUP_LIGHT_NET_TICKS)
	if first_window != Vector2i(transport.STARTUP_LIGHT_NET_TICKS, 1):
		_fail("first gameplay input window was incorrect: %s" % first_window)
		return
	var full_window := transport._input_forward_window(transport.STARTUP_LIGHT_NET_TICKS + 40)
	if full_window.y != transport.INPUT_FORWARD_REDUNDANCY_TICKS or full_window.x != transport.STARTUP_LIGHT_NET_TICKS + 40 - transport.INPUT_FORWARD_REDUNDANCY_TICKS + 1:
		_fail("rolling forward input window was incorrect: %s" % full_window)
		return
	transport.last_ack_tick = transport.STARTUP_LIGHT_NET_TICKS + 40
	if transport._input_forward_window(transport.STARTUP_LIGHT_NET_TICKS + 40) != full_window:
		_fail("server acknowledgement changed proactive input redundancy")
		return

	transport.set_context(false, false, false, false, 0, [], [], [], [], [], [], {}, null, null)
	transport._record_rtt_sample(0.050)
	var stable_ahead := transport.desired_ahead_ticks
	for sample in [0.140, 0.045, 0.135, 0.050]:
		transport._record_rtt_sample(sample)
	if transport.rtt_variance_s <= 0.0 or transport.desired_ahead_ticks <= stable_ahead:
		_fail("RTT variance did not add lead for a jittery peer: rtt=%f variance=%f ahead=%f stable=%f" % [transport.rtt_s, transport.rtt_variance_s, transport.desired_ahead_ticks, stable_ahead])
		return
	if transport.desired_ahead_ticks > transport.MAX_AHEAD_TICKS:
		_fail("jitter lead exceeded the existing maximum ahead bound")
		return

	print("MXT_NETPLAY_INPUT_RESILIENCE_OK")
	quit(0)
