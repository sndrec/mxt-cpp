extends SceneTree

const SAMPLE_RATE := 48000
const FRAME_SIZE := 480
const SAMPLE_COUNT := 4800
const MAKEUP_DB := 10.0
const THRESHOLD_DB := -18.0
const RATIO := 4.0
const CEILING_DB := -1.0

func _init() -> void:
	var quiet_codec := _configured_codec()
	var quiet_packets: Array = quiet_codec.encode_stereo_mix(_constant_frames(0.02), SAMPLE_RATE)
	if quiet_packets.is_empty():
		push_error("voice capture dynamics smoke encoded no quiet packets")
		quit(1)
		return
	if quiet_codec.get_last_output_peak() <= quiet_codec.get_last_input_peak() * 2.5:
		push_error("voice capture dynamics smoke did not raise quiet speech")
		quit(1)
		return
	if quiet_codec.get_last_capture_gain_db() < 9.5:
		push_error("voice capture dynamics smoke quiet makeup gain mismatch")
		quit(1)
		return

	var loud_codec := _configured_codec()
	var loud_packets: Array = loud_codec.encode_stereo_mix(_constant_frames(1.0), SAMPLE_RATE)
	if loud_packets.is_empty():
		push_error("voice capture dynamics smoke encoded no loud packets")
		quit(1)
		return
	var ceiling_linear := pow(10.0, CEILING_DB / 20.0)
	if loud_codec.get_last_output_peak() > ceiling_linear + 0.0001:
		push_error("voice capture dynamics smoke exceeded limiter ceiling")
		quit(1)
		return
	if loud_codec.get_last_capture_gain_db() >= 0.0:
		push_error("voice capture dynamics smoke did not compress loud speech")
		quit(1)
		return

	print(
		"MXT_VOICE_CAPTURE_DYNAMICS_SMOKE quiet_gain_db=%.2f quiet_out=%.4f loud_gain_db=%.2f loud_out=%.4f" % [
			quiet_codec.get_last_capture_gain_db(),
			quiet_codec.get_last_output_peak(),
			loud_codec.get_last_capture_gain_db(),
			loud_codec.get_last_output_peak(),
		])
	quit(0)

func _configured_codec() -> OpusVoiceCodec:
	var codec := OpusVoiceCodec.new()
	codec.configure(SAMPLE_RATE, FRAME_SIZE, 8000, 3)
	codec.set_capture_dynamics(true, MAKEUP_DB, THRESHOLD_DB, RATIO, CEILING_DB, 5.0, 140.0)
	return codec

func _constant_frames(amplitude: float) -> PackedVector2Array:
	var frames := PackedVector2Array()
	frames.resize(SAMPLE_COUNT)
	for i in range(SAMPLE_COUNT):
		frames[i] = Vector2(amplitude, amplitude)
	return frames
