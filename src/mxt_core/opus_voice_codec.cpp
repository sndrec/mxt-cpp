#include "mxt_core/opus_voice_codec.h"

#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "opus.h"
#include <algorithm>

using namespace godot;

void OpusVoiceCodec::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure", "sample_rate", "frame_size", "bitrate", "complexity"), &OpusVoiceCodec::configure, DEFVAL(16000), DEFVAL(320), DEFVAL(24000), DEFVAL(3));
	ClassDB::bind_method(D_METHOD("encode", "pcm"), &OpusVoiceCodec::encode);
	ClassDB::bind_method(D_METHOD("decode", "packet"), &OpusVoiceCodec::decode);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &OpusVoiceCodec::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &OpusVoiceCodec::get_frame_size);
	ClassDB::bind_method(D_METHOD("get_bitrate"), &OpusVoiceCodec::get_bitrate);
}

OpusVoiceCodec::~OpusVoiceCodec()
{
	destroy_codec();
}

void OpusVoiceCodec::destroy_codec()
{
	if (encoder) {
		opus_encoder_destroy(encoder);
		encoder = nullptr;
	}
	if (decoder) {
		opus_decoder_destroy(decoder);
		decoder = nullptr;
	}
}

bool OpusVoiceCodec::configure(int p_sample_rate, int p_frame_size, int p_bitrate, int p_complexity)
{
	if (p_sample_rate != 8000 && p_sample_rate != 12000 && p_sample_rate != 16000 &&
			p_sample_rate != 24000 && p_sample_rate != 48000) {
		return false;
	}
	if (p_frame_size <= 0 || p_frame_size > p_sample_rate / 10) {
		return false;
	}
	destroy_codec();
	sample_rate = p_sample_rate;
	frame_size = p_frame_size;
	bitrate = std::clamp(p_bitrate, 6000, 64000);
	complexity = std::clamp(p_complexity, 0, 10);

	int err = OPUS_OK;
	encoder = opus_encoder_create(sample_rate, 1, OPUS_APPLICATION_VOIP, &err);
	if (err != OPUS_OK || !encoder) {
		destroy_codec();
		return false;
	}
	decoder = opus_decoder_create(sample_rate, 1, &err);
	if (err != OPUS_OK || !decoder) {
		destroy_codec();
		return false;
	}
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
	opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(complexity));
	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	return true;
}

PackedByteArray OpusVoiceCodec::encode(PackedFloat32Array pcm)
{
	PackedByteArray out;
	if (!encoder && !configure(sample_rate, frame_size, bitrate, complexity)) {
		return out;
	}
	if (pcm.size() < frame_size) {
		return out;
	}
	unsigned char packet[512];
	const int written = opus_encode_float(encoder, pcm.ptr(), frame_size, packet, static_cast<opus_int32>(sizeof(packet)));
	if (written <= 0) {
		return out;
	}
	out.resize(written);
	for (int i = 0; i < written; ++i) {
		out.set(i, packet[i]);
	}
	return out;
}

PackedFloat32Array OpusVoiceCodec::decode(PackedByteArray packet)
{
	PackedFloat32Array out;
	if (!decoder && !configure(sample_rate, frame_size, bitrate, complexity)) {
		return out;
	}
	out.resize(frame_size);
	const unsigned char* data = packet.size() > 0 ? packet.ptr() : nullptr;
	const int decoded = opus_decode_float(decoder, data, packet.size(), out.ptrw(), frame_size, 0);
	if (decoded <= 0) {
		out.resize(0);
		return out;
	}
	out.resize(decoded);
	return out;
}
