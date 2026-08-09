#include "mxt_core/opus_voice_codec.h"

#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "opus.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

void OpusVoiceCodec::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure", "sample_rate", "frame_size", "bitrate", "complexity"), &OpusVoiceCodec::configure, DEFVAL(16000), DEFVAL(320), DEFVAL(24000), DEFVAL(3));
	ClassDB::bind_method(D_METHOD("set_capture_dynamics", "enabled", "makeup_db", "threshold_db", "ratio", "ceiling_db", "attack_msec", "release_msec"), &OpusVoiceCodec::set_capture_dynamics);
	ClassDB::bind_method(D_METHOD("encode", "pcm"), &OpusVoiceCodec::encode);
	ClassDB::bind_method(D_METHOD("decode", "packet"), &OpusVoiceCodec::decode);
	ClassDB::bind_method(D_METHOD("encode_stereo_mix", "input_frames", "input_rate"), &OpusVoiceCodec::encode_stereo_mix);
	ClassDB::bind_method(D_METHOD("decode_push_stereo", "packet", "playback", "left_gain", "right_gain"), &OpusVoiceCodec::decode_push_stereo, DEFVAL(1.0f), DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &OpusVoiceCodec::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &OpusVoiceCodec::get_frame_size);
	ClassDB::bind_method(D_METHOD("get_bitrate"), &OpusVoiceCodec::get_bitrate);
	ClassDB::bind_method(D_METHOD("get_last_input_peak"), &OpusVoiceCodec::get_last_input_peak);
	ClassDB::bind_method(D_METHOD("get_last_output_peak"), &OpusVoiceCodec::get_last_output_peak);
	ClassDB::bind_method(D_METHOD("get_last_capture_gain_db"), &OpusVoiceCodec::get_last_capture_gain_db);
	ClassDB::bind_method(D_METHOD("get_last_decoded_peak"), &OpusVoiceCodec::get_last_decoded_peak);
	ClassDB::bind_method(D_METHOD("get_last_packets_encoded"), &OpusVoiceCodec::get_last_packets_encoded);
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
	encode_resample_pos = 0.0;
	encode_sample_head = 0;
	last_input_peak = 0.0f;
	last_output_peak = 0.0f;
	last_capture_gain_db = 0.0f;
	last_decoded_peak = 0.0f;
	last_packets_encoded = 0;
	capture_envelope = 0.0f;
	encode_samples.clear();

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

void OpusVoiceCodec::set_capture_dynamics(bool p_enabled, float p_makeup_db, float p_threshold_db, float p_ratio, float p_ceiling_db, float p_attack_msec, float p_release_msec)
{
	capture_dynamics_enabled = p_enabled;
	capture_makeup_gain = std::pow(10.0f, std::clamp(p_makeup_db, -24.0f, 24.0f) / 20.0f);
	capture_compressor_threshold = std::pow(10.0f, std::clamp(p_threshold_db, -60.0f, 0.0f) / 20.0f);
	capture_compressor_ratio = std::clamp(p_ratio, 1.0f, 20.0f);
	capture_limiter_ceiling = std::pow(10.0f, std::clamp(p_ceiling_db, -12.0f, 0.0f) / 20.0f);
	capture_attack_seconds = std::clamp(p_attack_msec, 0.1f, 100.0f) * 0.001f;
	capture_release_seconds = std::clamp(p_release_msec, 10.0f, 2000.0f) * 0.001f;
	capture_envelope = 0.0f;
	last_output_peak = 0.0f;
	last_capture_gain_db = 0.0f;
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
	last_decoded_peak = 0.0f;
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
	const float* samples = out.ptr();
	for (int i = 0; i < decoded; ++i) {
		last_decoded_peak = std::max(last_decoded_peak, std::abs(samples[i]));
	}
	out.resize(decoded);
	return out;
}

Array OpusVoiceCodec::encode_stereo_mix(PackedVector2Array input_frames, double input_rate)
{
	Array packets;
	last_input_peak = 0.0f;
	last_output_peak = 0.0f;
	last_capture_gain_db = 0.0f;
	last_packets_encoded = 0;
	if (!encoder && !configure(sample_rate, frame_size, bitrate, complexity)) {
		return packets;
	}
	const int input_count = input_frames.size();
	if (input_count <= 0) {
		return packets;
	}
	if (input_rate <= 0.0) {
		input_rate = 48000.0;
	}
	const double step = input_rate / static_cast<double>(sample_rate);
	const float attack_coefficient = std::exp(-1.0f / (static_cast<float>(sample_rate) * capture_attack_seconds));
	const float release_coefficient = std::exp(-1.0f / (static_cast<float>(sample_rate) * capture_release_seconds));
	const float compressor_exponent = 1.0f - 1.0f / capture_compressor_ratio;
	const Vector2* input = input_frames.ptr();
	while (encode_resample_pos < static_cast<double>(input_count)) {
		const int idx = static_cast<int>(encode_resample_pos);
		const float input_mono = std::clamp((input[idx].x + input[idx].y) * 0.5f, -1.0f, 1.0f);
		last_input_peak = std::max(last_input_peak, std::abs(input_mono));
		float output_mono = input_mono;
		if (capture_dynamics_enabled) {
			const float detector = std::abs(input_mono);
			const float envelope_coefficient = detector > capture_envelope ? attack_coefficient : release_coefficient;
			capture_envelope = detector + envelope_coefficient * (capture_envelope - detector);
			float compressor_gain = 1.0f;
			if (capture_envelope > capture_compressor_threshold) {
				compressor_gain = std::pow(capture_compressor_threshold / capture_envelope, compressor_exponent);
			}
			const float total_gain = capture_makeup_gain * compressor_gain;
			output_mono = std::clamp(input_mono * total_gain, -capture_limiter_ceiling, capture_limiter_ceiling);
			last_capture_gain_db = 20.0f * std::log10(std::max(total_gain, 0.000001f));
		}
		last_output_peak = std::max(last_output_peak, std::abs(output_mono));
		encode_samples.push_back(output_mono);
		encode_resample_pos += step;
	}
	encode_resample_pos -= static_cast<double>(input_count);

	while (static_cast<int>(encode_samples.size()) - encode_sample_head >= frame_size) {
		unsigned char packet[512];
		const int written = opus_encode_float(encoder, encode_samples.data() + encode_sample_head, frame_size, packet, static_cast<opus_int32>(sizeof(packet)));
		encode_sample_head += frame_size;
		if (written <= 0) {
			continue;
		}
		PackedByteArray out;
		out.resize(written);
		std::memcpy(out.ptrw(), packet, static_cast<size_t>(written));
		packets.append(out);
		++last_packets_encoded;
	}
	if (encode_sample_head > frame_size * 8) {
		encode_samples.erase(encode_samples.begin(), encode_samples.begin() + encode_sample_head);
		encode_sample_head = 0;
	}
	return packets;
}

bool OpusVoiceCodec::decode_push_stereo(PackedByteArray packet, Ref<AudioStreamGeneratorPlayback> playback, float left_gain, float right_gain)
{
	last_decoded_peak = 0.0f;
	if (playback.is_null()) {
		return false;
	}
	if (!decoder && !configure(sample_rate, frame_size, bitrate, complexity)) {
		return false;
	}
	std::vector<float> decoded_pcm;
	decoded_pcm.resize(static_cast<size_t>(frame_size));
	const unsigned char* data = packet.size() > 0 ? packet.ptr() : nullptr;
	const int decoded = opus_decode_float(decoder, data, packet.size(), decoded_pcm.data(), frame_size, 0);
	if (decoded <= 0) {
		return false;
	}
	PackedVector2Array frames;
	frames.resize(decoded);
	Vector2* out = frames.ptrw();
	for (int i = 0; i < decoded; ++i) {
		const float sample = decoded_pcm[static_cast<size_t>(i)];
		last_decoded_peak = std::max(last_decoded_peak, std::abs(sample));
		out[i] = Vector2(sample * left_gain, sample * right_gain);
	}
	if (playback->get_frames_available() < decoded) {
		playback->clear_buffer();
	}
	return playback->push_buffer(frames);
}
