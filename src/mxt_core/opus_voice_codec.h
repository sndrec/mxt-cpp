#pragma once

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"

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

	void destroy_codec();

protected:
	static void _bind_methods();

public:
	OpusVoiceCodec() = default;
	~OpusVoiceCodec();

	bool configure(int p_sample_rate = 16000, int p_frame_size = 320, int p_bitrate = 24000, int p_complexity = 3);
	godot::PackedByteArray encode(godot::PackedFloat32Array pcm);
	godot::PackedFloat32Array decode(godot::PackedByteArray packet);
	int get_sample_rate() const { return sample_rate; }
	int get_frame_size() const { return frame_size; }
	int get_bitrate() const { return bitrate; }
};

}
