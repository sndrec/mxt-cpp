#pragma once

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/classes/audio_stream_generator_playback.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"

#include <vector>

struct OpusDecoder;
struct OpusEncoder;

namespace godot {

class OpusVoiceCodec : public RefCounted {
	GDCLASS(OpusVoiceCodec, RefCounted)

	OpusEncoder* encoder = nullptr;
	OpusDecoder* decoder = nullptr;
	int sample_rate = 16000;
	int frame_size = 320;
	int bitrate = 24000;
	int complexity = 3;
	double encode_resample_pos = 0.0;
	int encode_sample_head = 0;
	float last_input_peak = 0.0f;
	int last_packets_encoded = 0;
	std::vector<float> encode_samples;

	void destroy_codec();

protected:
	static void _bind_methods();

public:
	OpusVoiceCodec() = default;
	~OpusVoiceCodec();

	bool configure(int p_sample_rate = 16000, int p_frame_size = 320, int p_bitrate = 24000, int p_complexity = 3);
	godot::PackedByteArray encode(godot::PackedFloat32Array pcm);
	godot::PackedFloat32Array decode(godot::PackedByteArray packet);
	godot::Array encode_stereo_mix(godot::PackedVector2Array input_frames, double input_rate);
	bool decode_push_stereo(godot::PackedByteArray packet, godot::Ref<godot::AudioStreamGeneratorPlayback> playback, float left_gain = 1.0f, float right_gain = 1.0f);
	int get_sample_rate() const { return sample_rate; }
	int get_frame_size() const { return frame_size; }
	int get_bitrate() const { return bitrate; }
	float get_last_input_peak() const { return last_input_peak; }
	int get_last_packets_encoded() const { return last_packets_encoded; }
};

}
