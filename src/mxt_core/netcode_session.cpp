#include "mxt_core/netcode_session.h"

#include "main.h"
#include "mxt_core/auth_input_hybrid_zstd_dictionary.h"
#include "mxt_core/auth_input_zstd_dictionary.h"
#include "godot_cpp/classes/dir_access.hpp"
#include "godot_cpp/classes/file_access.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "zstd.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace godot;

namespace {
constexpr int MXT_NET_MAX_INPUT_BYTES = 8;
constexpr int MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET = 255;
constexpr int MXT_NET_AUTHORITATIVE_INPUT_BYTES_PER_RACER = 5;
constexpr int MXT_NET_AUTHORITATIVE_INPUT_OLD_BYTES_PER_RACER = 9;
constexpr int MXT_NET_AUTHORITATIVE_INPUT_PACKET_HEADER_BYTES = 4;
constexpr int MXT_NET_COMPRESSION_ZSTD = FileAccess::COMPRESSION_ZSTD;
constexpr uint8_t MXT_NET_AUTH_MODE_PACKED_PLAIN_ZSTD = 0;
constexpr uint8_t MXT_NET_AUTH_MODE_PACKED_DICT_ZSTD = 1;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_PLAIN_ZSTD = 2;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_DICT_ZSTD = 3;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_PLAIN_ZSTD = 4;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_DICT_ZSTD = 5;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_DELTA_PLAIN_ZSTD = 6;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_DELTA_DICT_ZSTD = 7;
constexpr uint8_t MXT_NET_AUTH_MODE_MASK = 0x07;
constexpr uint8_t MXT_NET_AUTH_COUNT_SHIFT = 3;
constexpr uint8_t MXT_NET_AUTH_COUNT_MASK = 0x78;
constexpr uint8_t MXT_NET_AUTH_COUNT_ESCAPE = 0x0f;
constexpr uint8_t MXT_NET_AUTH_PHASE_BIT = 0x80;
constexpr int MXT_NET_AUTH_ZSTD_LEVEL = 1;
constexpr int64_t MXT_NET_DEFAULT_AUTH_SAMPLE_LIMIT = 20000;
constexpr int MXT_NET_AUTH_SAMPLE_DIR_BYTES = 1024;
constexpr const char* MXT_NET_DEFAULT_AUTH_SAMPLE_DIR = "user://auth_input_samples";
constexpr uint32_t MXT_NET_RACE_PHASE_BIT = 0x80000000u;
constexpr uint32_t MXT_NET_TICK_MASK = 0x7fffffffu;
enum AuthInputLayout : uint8_t {
	AUTH_INPUT_LAYOUT_OLD_BYTE_PLANES = 0,
	AUTH_INPUT_LAYOUT_PACKED_BUTTONS = 1,
	AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA = 2,
	AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS = 3,
	AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA = 4,
};
bool g_auth_input_sample_dump_enabled = false;
int64_t g_auth_input_sample_dump_limit = MXT_NET_DEFAULT_AUTH_SAMPLE_LIMIT;
int64_t g_auth_input_sample_dump_index = 0;
char g_auth_input_sample_dump_dir[MXT_NET_AUTH_SAMPLE_DIR_BYTES] = "user://auth_input_samples";
ZSTD_CCtx* g_auth_input_zstd_cctx = nullptr;
ZSTD_DCtx* g_auth_input_zstd_dctx = nullptr;
ZSTD_CDict* g_auth_input_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_hybrid_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_hybrid_zstd_ddict = nullptr;

struct PacketWriter {
	uint8_t data[65536] = {};
	int pos = 0;

	bool write_u8(uint8_t v)
	{
		if (pos + 1 > static_cast<int>(sizeof(data))) return false;
		data[pos++] = v;
		return true;
	}

	bool write_u16(uint16_t v)
	{
		return write_u8(static_cast<uint8_t>(v & 0xff)) &&
			write_u8(static_cast<uint8_t>((v >> 8) & 0xff));
	}

	bool write_i32(int32_t v)
	{
		uint32_t u = static_cast<uint32_t>(v);
		return write_u8(static_cast<uint8_t>(u & 0xff)) &&
			write_u8(static_cast<uint8_t>((u >> 8) & 0xff)) &&
			write_u8(static_cast<uint8_t>((u >> 16) & 0xff)) &&
			write_u8(static_cast<uint8_t>((u >> 24) & 0xff));
	}

	bool write_u24(uint32_t v)
	{
		return write_u8(static_cast<uint8_t>(v & 0xff)) &&
			write_u8(static_cast<uint8_t>((v >> 8) & 0xff)) &&
			write_u8(static_cast<uint8_t>((v >> 16) & 0xff));
	}

	bool write_bytes(const uint8_t* src, int len)
	{
		if (!src || len < 0 || pos + len > static_cast<int>(sizeof(data))) return false;
		std::memcpy(data + pos, src, static_cast<size_t>(len));
		pos += len;
		return true;
	}

	PackedByteArray to_pba() const
	{
		PackedByteArray out;
		out.resize(pos);
		for (int i = 0; i < pos; ++i) {
			out.set(i, data[i]);
		}
		return out;
	}
};

struct PacketReader {
	const uint8_t* data = nullptr;
	int size = 0;
	int pos = 0;

	explicit PacketReader(const PackedByteArray& p_packet)
	{
		data = p_packet.ptr();
		size = p_packet.size();
	}

	bool read_u8(uint8_t& out)
	{
		if (pos + 1 > size) return false;
		out = data[pos++];
		return true;
	}

	bool read_u16(uint16_t& out)
	{
		uint8_t a = 0, b = 0;
		if (!read_u8(a) || !read_u8(b)) return false;
		out = static_cast<uint16_t>(a) | (static_cast<uint16_t>(b) << 8);
		return true;
	}

	bool read_i32(int32_t& out)
	{
		uint8_t a = 0, b = 0, c = 0, d = 0;
		if (!read_u8(a) || !read_u8(b) || !read_u8(c) || !read_u8(d)) return false;
		uint32_t u = static_cast<uint32_t>(a) |
			(static_cast<uint32_t>(b) << 8) |
			(static_cast<uint32_t>(c) << 16) |
			(static_cast<uint32_t>(d) << 24);
		out = static_cast<int32_t>(u);
		return true;
	}

	bool read_u24(uint32_t& out)
	{
		uint8_t a = 0, b = 0, c = 0;
		if (!read_u8(a) || !read_u8(b) || !read_u8(c)) return false;
		out = static_cast<uint32_t>(a) |
			(static_cast<uint32_t>(b) << 8) |
			(static_cast<uint32_t>(c) << 16);
		return true;
	}

	bool read_bytes(const uint8_t*& out, int len)
	{
		if (len < 0 || pos + len > size) return false;
		out = data + pos;
		pos += len;
		return true;
	}
};

bool read_packet_input(PacketReader& reader, PlayerInput& out)
{
	const uint8_t* bytes = nullptr;
	if (!reader.read_bytes(bytes, 1)) {
		return false;
	}
	const int len = PlayerInput::encoded_raw_size_from_mask(bytes[0]);
	reader.pos -= 1;
	if (len > MXT_NET_MAX_INPUT_BYTES || !reader.read_bytes(bytes, len)) {
		return false;
	}
	out = PlayerInput::from_raw(bytes, len);
	return true;
}

uint8_t input_button_mask(const PlayerInput& input)
{
	uint8_t mask = 0;
	if (input.accelerate > 0.0f) mask |= 1u << 0;
	if (input.brake > 0.0f) mask |= 1u << 1;
	if (input.spinattack) mask |= 1u << 2;
	if (input.sideattack) mask |= 1u << 3;
	if (input.boost) mask |= 1u << 4;
	return mask;
}

uint8_t input_trigger_byte(float v)
{
	return PlayerInput::quantize_trigger(v);
}

uint8_t input_axis_byte(float v)
{
	return PlayerInput::quantize_axis(v);
}

uint8_t zigzag_i8(int v)
{
	return static_cast<uint8_t>(((v << 1) ^ (v >> 7)) & 0xff);
}

int unzigzag_i8(uint8_t v)
{
	return (static_cast<int>(v) >> 1) ^ -static_cast<int>(v & 1u);
}

int wrapped_i8_delta(uint8_t value, uint8_t previous)
{
	return ((static_cast<int>(value) - static_cast<int>(previous) + 128) & 0xff) - 128;
}

float trigger_from_byte(uint8_t v)
{
	return static_cast<float>(v) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION);
}

float axis_from_byte(uint8_t v)
{
	return (static_cast<float>(v) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION)) * 2.0f - 1.0f;
}

int auth_input_raw_size(int frame_count, int racer_count, AuthInputLayout layout)
{
	if (layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS || layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA) {
		const int input_count = frame_count * racer_count;
		return (((input_count + 7) >> 3) * 5) + input_count * 4;
	}
	const int bytes_per_racer = layout == AUTH_INPUT_LAYOUT_OLD_BYTE_PLANES ?
		MXT_NET_AUTHORITATIVE_INPUT_OLD_BYTES_PER_RACER :
		MXT_NET_AUTHORITATIVE_INPUT_BYTES_PER_RACER;
	return frame_count * racer_count * bytes_per_racer;
}

AuthInputLayout auth_input_layout_from_mode(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	if (mode == MXT_NET_AUTH_MODE_DELTA_PLAIN_ZSTD || mode == MXT_NET_AUTH_MODE_DELTA_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA;
	}
	if (mode == MXT_NET_AUTH_MODE_BITPACKED_PLAIN_ZSTD || mode == MXT_NET_AUTH_MODE_BITPACKED_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS;
	}
	if (mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_PLAIN_ZSTD || mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	}
	return AUTH_INPUT_LAYOUT_PACKED_BUTTONS;
}

bool auth_input_mode_uses_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_PACKED_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_BITPACKED_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_DICT_ZSTD;
}

bool auth_input_mode_valid(uint8_t mode)
{
	return (mode & MXT_NET_AUTH_MODE_MASK) <= MXT_NET_AUTH_MODE_BITPACKED_DELTA_DICT_ZSTD;
}

uint8_t pack_auth_mode_count_phase(uint8_t mode, int count, int race_phase)
{
	const uint8_t count_code = (count >= 1 && count <= 15) ?
		static_cast<uint8_t>(count - 1) :
		MXT_NET_AUTH_COUNT_ESCAPE;
	return static_cast<uint8_t>(
		(mode & MXT_NET_AUTH_MODE_MASK) |
		static_cast<uint8_t>(count_code << MXT_NET_AUTH_COUNT_SHIFT) |
		((race_phase & 1) ? MXT_NET_AUTH_PHASE_BIT : 0)
	);
}

bool auth_input_count_needs_escape(int count)
{
	return count <= 0 || count > 15;
}

int auth_input_packet_header_size(int count)
{
	return MXT_NET_AUTHORITATIVE_INPUT_PACKET_HEADER_BYTES + (auth_input_count_needs_escape(count) ? 1 : 0);
}

int32_t pack_tick_phase(int tick, int race_phase)
{
	const uint32_t tick_bits = static_cast<uint32_t>(std::max(tick, 0)) & MXT_NET_TICK_MASK;
	const uint32_t phase_bit = (race_phase & 1) ? MXT_NET_RACE_PHASE_BIT : 0u;
	return static_cast<int32_t>(tick_bits | phase_bit);
}

int unpack_tick(int32_t packed_tick)
{
	return static_cast<int>(static_cast<uint32_t>(packed_tick) & MXT_NET_TICK_MASK);
}

int unpack_race_phase(int32_t packed_tick)
{
	return (static_cast<uint32_t>(packed_tick) & MXT_NET_RACE_PHASE_BIT) ? 1 : 0;
}

ZSTD_CCtx* auth_input_zstd_cctx()
{
	if (!g_auth_input_zstd_cctx) {
		g_auth_input_zstd_cctx = ZSTD_createCCtx();
	}
	return g_auth_input_zstd_cctx;
}

ZSTD_DCtx* auth_input_zstd_dctx()
{
	if (!g_auth_input_zstd_dctx) {
		g_auth_input_zstd_dctx = ZSTD_createDCtx();
	}
	return g_auth_input_zstd_dctx;
}

ZSTD_CDict* auth_input_zstd_cdict()
{
	if (!g_auth_input_zstd_cdict) {
		g_auth_input_zstd_cdict = ZSTD_createCDict(
			MXT_AUTH_INPUT_ZSTD_DICT,
			MXT_AUTH_INPUT_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_ZSTD_LEVEL
		);
	}
	return g_auth_input_zstd_cdict;
}

ZSTD_CDict* auth_input_hybrid_zstd_cdict()
{
	if (!g_auth_input_hybrid_zstd_cdict) {
		g_auth_input_hybrid_zstd_cdict = ZSTD_createCDict(
			MXT_AUTH_INPUT_HYBRID_ZSTD_DICT,
			MXT_AUTH_INPUT_HYBRID_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_ZSTD_LEVEL
		);
	}
	return g_auth_input_hybrid_zstd_cdict;
}

ZSTD_DDict* auth_input_zstd_ddict()
{
	if (!g_auth_input_zstd_ddict) {
		g_auth_input_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_ZSTD_DICT,
			MXT_AUTH_INPUT_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_zstd_ddict;
}

ZSTD_DDict* auth_input_hybrid_zstd_ddict()
{
	if (!g_auth_input_hybrid_zstd_ddict) {
		g_auth_input_hybrid_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_HYBRID_ZSTD_DICT,
			MXT_AUTH_INPUT_HYBRID_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_hybrid_zstd_ddict;
}

bool auth_input_mode_uses_hybrid_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_DICT_ZSTD;
}

PackedByteArray compress_auth_input_with_dict(const PackedByteArray& raw, bool hybrid_dict = false)
{
	const int raw_size = raw.size();
	if (raw_size <= 0) {
		return PackedByteArray();
	}
	ZSTD_CCtx* cctx = auth_input_zstd_cctx();
	ZSTD_CDict* cdict = hybrid_dict ? auth_input_hybrid_zstd_cdict() : auth_input_zstd_cdict();
	if (!cctx || !cdict) {
		return PackedByteArray();
	}
	const size_t bound = ZSTD_compressBound(static_cast<size_t>(raw_size));
	if (bound > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(static_cast<int>(bound)) != 0) {
		return PackedByteArray();
	}
	const size_t compressed_size = ZSTD_compress_usingCDict(
		cctx,
		out.ptrw(),
		bound,
		raw.ptr(),
		static_cast<size_t>(raw_size),
		cdict
	);
	if (ZSTD_isError(compressed_size) || compressed_size > bound || compressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(compressed_size));
	return out;
}

PackedByteArray decompress_auth_input_with_dict(const PackedByteArray& compressed, int raw_size, bool hybrid_dict = false)
{
	if (raw_size <= 0 || compressed.size() <= 0) {
		return PackedByteArray();
	}
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	ZSTD_DDict* ddict = hybrid_dict ? auth_input_hybrid_zstd_ddict() : auth_input_zstd_ddict();
	if (!dctx || !ddict) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size) != 0) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompress_usingDDict(
		dctx,
		out.ptrw(),
		static_cast<size_t>(raw_size),
		compressed.ptr(),
		static_cast<size_t>(compressed.size()),
		ddict
	);
	if (ZSTD_isError(decompressed_size) || decompressed_size != static_cast<size_t>(raw_size)) {
		return PackedByteArray();
	}
	return out;
}

void set_auth_input_sample_dump_dir(const String& directory)
{
	const String selected_dir = directory.is_empty() ? String(MXT_NET_DEFAULT_AUTH_SAMPLE_DIR) : directory;
	const CharString selected_dir_utf8 = selected_dir.utf8();
	std::snprintf(
		g_auth_input_sample_dump_dir,
		static_cast<size_t>(MXT_NET_AUTH_SAMPLE_DIR_BYTES),
		"%s",
		selected_dir_utf8.get_data()
	);
}

void dump_auth_input_sample(const PackedByteArray& raw, int first_tick, int count, int racer_count)
{
	if (!g_auth_input_sample_dump_enabled) {
		return;
	}
	if (g_auth_input_sample_dump_limit > 0 && g_auth_input_sample_dump_index >= g_auth_input_sample_dump_limit) {
		return;
	}
	const String file_name = "auth_" + String::num_int64(g_auth_input_sample_dump_index).pad_zeros(8) +
		"_t" + String::num_int64(first_tick) +
		"_f" + String::num_int64(count) +
		"_p" + String::num_int64(racer_count) + ".bin";
	const String path = String(g_auth_input_sample_dump_dir) + String("/") + file_name;
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_buffer(raw);
		file->close();
		++g_auth_input_sample_dump_index;
	}
}
}

bool NetcodeSession::write_authoritative_input_raw(
	PackedByteArray& raw,
	const InputFrame* const* frames,
	int frame_count,
	uint8_t p_layout) const
{
	const AuthInputLayout layout = static_cast<AuthInputLayout>(p_layout);
	uint8_t* raw_bytes = raw.ptrw();
	int raw_pos = 0;
	const bool delta_layout = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA;
	const bool bitpacked_layout = layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS ||
		layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	const bool bitpacked_analog_delta = layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	if (bitpacked_layout) {
		std::memset(raw_bytes, 0, static_cast<size_t>(raw.size()));
		const int input_count = frame_count * racer_count;
		const int bitset_bytes = (input_count + 7) >> 3;
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const int input_index = f * racer_count + i;
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				const uint8_t mask = input_button_mask(input);
				for (int bit = 0; bit < 5; ++bit) {
					if ((mask & (1u << bit)) != 0) {
						raw_bytes[bit * bitset_bytes + (input_index >> 3)] |= static_cast<uint8_t>(1u << (input_index & 7));
					}
				}
			}
		}
		raw_pos = bitset_bytes * 5;
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_trigger_byte(input.strafe_left);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_trigger_byte(prev.strafe_left)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_trigger_byte(input.strafe_right);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_trigger_byte(prev.strafe_right)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_axis_byte(input.steer_horizontal);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_axis_byte(prev.steer_horizontal)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_axis_byte(input.steer_vertical);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_axis_byte(prev.steer_vertical)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		return raw_pos == raw.size();
	}
	if (layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS || delta_layout) {
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_button_mask(input);
				if (delta_layout && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_button_mask(prev)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
	} else {
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				raw_bytes[raw_pos++] = input.accelerate > 0.0f ? 1 : 0;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				raw_bytes[raw_pos++] = input.brake > 0.0f ? 1 : 0;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				raw_bytes[raw_pos++] = input.spinattack ? 1 : 0;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				raw_bytes[raw_pos++] = input.sideattack ? 1 : 0;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				raw_bytes[raw_pos++] = input.boost ? 1 : 0;
			}
		}
	}
	for (int f = 0; f < frame_count; ++f) {
		const InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
			uint8_t value = input_trigger_byte(input.strafe_left);
			if (delta_layout && f > 0) {
				const InputFrame* prev_frame = frames[f - 1];
				const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
				value = zigzag_i8(wrapped_i8_delta(value, input_trigger_byte(prev.strafe_left)));
			}
			raw_bytes[raw_pos++] = value;
		}
	}
	for (int f = 0; f < frame_count; ++f) {
		const InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
			uint8_t value = input_trigger_byte(input.strafe_right);
			if (delta_layout && f > 0) {
				const InputFrame* prev_frame = frames[f - 1];
				const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
				value = zigzag_i8(wrapped_i8_delta(value, input_trigger_byte(prev.strafe_right)));
			}
			raw_bytes[raw_pos++] = value;
		}
	}
	for (int f = 0; f < frame_count; ++f) {
		const InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
			uint8_t value = input_axis_byte(input.steer_horizontal);
			if (delta_layout && f > 0) {
				const InputFrame* prev_frame = frames[f - 1];
				const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
				value = zigzag_i8(wrapped_i8_delta(value, input_axis_byte(prev.steer_horizontal)));
			}
			raw_bytes[raw_pos++] = value;
		}
	}
	for (int f = 0; f < frame_count; ++f) {
		const InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
			uint8_t value = input_axis_byte(input.steer_vertical);
			if (delta_layout && f > 0) {
				const InputFrame* prev_frame = frames[f - 1];
				const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
				value = zigzag_i8(wrapped_i8_delta(value, input_axis_byte(prev.steer_vertical)));
			}
			raw_bytes[raw_pos++] = value;
		}
	}
	return raw_pos == raw.size();
}

void NetcodeSession::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("reset"), &NetcodeSession::reset);
	ClassDB::bind_method(D_METHOD("configure", "player_ids", "cpu_flags", "local_player_id"), &NetcodeSession::configure);
	ClassDB::bind_method(D_METHOD("set_local_input", "input_bytes"), &NetcodeSession::set_local_input);
	ClassDB::bind_method(D_METHOD("store_local_input", "tick", "input_bytes"), &NetcodeSession::store_local_input);
	ClassDB::bind_method(D_METHOD("store_authoritative_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_authoritative_input);
	ClassDB::bind_method(D_METHOD("store_pending_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_pending_input);
	ClassDB::bind_method(D_METHOD("fill_missing_pending_inputs", "tick", "player_ids", "disconnected_ids", "delayed_ids", "allow_new_delayed"), &NetcodeSession::fill_missing_pending_inputs);
	ClassDB::bind_method(D_METHOD("build_local_input_packet", "first_tick", "count", "race_phase"), &NetcodeSession::build_local_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("store_pending_input_packet", "player_id", "reject_before_tick", "packet", "ahead", "now_sec", "expected_race_phase"), &NetcodeSession::store_pending_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("build_authoritative_input_packet", "last_tick", "max_frame_count", "race_phase"), &NetcodeSession::build_authoritative_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("store_authoritative_input_packet", "packet", "expected_race_phase"), &NetcodeSession::store_authoritative_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("debug_compare_authoritative_input_packet_sizes", "last_tick", "max_frame_count", "race_phase"), &NetcodeSession::debug_compare_authoritative_input_packet_sizes, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("consume_authoritative_packet_stats"), &NetcodeSession::consume_authoritative_packet_stats);
	ClassDB::bind_method(D_METHOD("get_input_frame_debug", "tick"), &NetcodeSession::get_input_frame_debug);
	ClassDB::bind_method(D_METHOD("configure_authoritative_input_sample_dump", "enabled", "limit", "directory"), &NetcodeSession::configure_authoritative_input_sample_dump);
	ClassDB::bind_method(D_METHOD("clear_peer_state"), &NetcodeSession::clear_peer_state);
	ClassDB::bind_method(D_METHOD("remove_peer", "peer_id"), &NetcodeSession::remove_peer);
	ClassDB::bind_method(D_METHOD("set_peer_last_received", "peer_id", "tick", "now_sec"), &NetcodeSession::set_peer_last_received);
	ClassDB::bind_method(D_METHOD("get_peer_last_received", "peer_id"), &NetcodeSession::get_peer_last_received);
	ClassDB::bind_method(D_METHOD("peer_has_received", "peer_id"), &NetcodeSession::peer_has_received);
	ClassDB::bind_method(D_METHOD("set_peer_desired_ahead", "peer_id", "ahead"), &NetcodeSession::set_peer_desired_ahead);
	ClassDB::bind_method(D_METHOD("get_max_peer_desired_ahead", "peer_ids", "fallback"), &NetcodeSession::get_max_peer_desired_ahead);
	ClassDB::bind_method(D_METHOD("get_peer_last_input_time", "peer_id"), &NetcodeSession::get_peer_last_input_time);
	ClassDB::bind_method(D_METHOD("server_has_full_input_frame", "tick"), &NetcodeSession::server_has_full_input_frame);
	ClassDB::bind_method(D_METHOD("tick_server_frame", "game_sim", "tick"), &NetcodeSession::tick_server_frame);
	ClassDB::bind_method(D_METHOD("tick_client_predicted_frame", "game_sim", "tick"), &NetcodeSession::tick_client_predicted_frame);
	ClassDB::bind_method(D_METHOD("recalculate_predictions", "start_tick", "end_tick"), &NetcodeSession::recalculate_predictions);
	ClassDB::bind_method(D_METHOD("replay_history", "game_sim", "start_tick", "end_tick"), &NetcodeSession::replay_history);
	ClassDB::bind_method(D_METHOD("get_frame_as_dictionary", "tick"), &NetcodeSession::get_frame_as_dictionary);
}

NetcodeSession::NetcodeSession()
{
	reset();
}

PlayerInput NetcodeSession::decay_predicted_input(const PlayerInput& prev)
{
	PlayerInput out = prev;
	auto lerp_f = [](float a, float b, float t) -> float {
		return a + (b - a) * t;
	};
	out.strafe_left = lerp_f(out.strafe_left, 0.0f, 0.25f);
	out.strafe_right = lerp_f(out.strafe_right, 0.0f, 0.25f);
	out.steer_horizontal = lerp_f(out.steer_horizontal, 0.0f, 0.25f);
	out.steer_vertical = lerp_f(out.steer_vertical, 0.0f, 0.25f);
	out.spinattack = false;
	out.sideattack = false;
	out.boost = false;
	return out;
}

void NetcodeSession::clear_frame(InputFrame& frame, int32_t tick)
{
	frame.tick = tick;
	for (int i = 0; i < MAX_RACERS; ++i) {
		frame.inputs[i] = neutral_input;
		frame.present[i] = 0;
	}
}

NetcodeSession::InputFrame& NetcodeSession::frame_for(InputFrame* frames, int32_t tick)
{
	InputFrame& frame = frames[tick & (HISTORY_LEN - 1)];
	if (frame.tick != tick) {
		clear_frame(frame, tick);
	}
	return frame;
}

const NetcodeSession::InputFrame* NetcodeSession::find_frame(const InputFrame* frames, int32_t tick) const
{
	const InputFrame& frame = frames[tick & (HISTORY_LEN - 1)];
	return frame.tick == tick ? &frame : nullptr;
}

int NetcodeSession::find_racer_index(int32_t player_id) const
{
	for (int i = 0; i < racer_count; ++i) {
		if (player_ids[i] == player_id) {
			return i;
		}
	}
	return -1;
}

int NetcodeSession::find_peer_index(int32_t peer_id) const
{
	for (int i = 0; i < MAX_PEERS; ++i) {
		if (peer_states[i].active && peer_states[i].id == peer_id) {
			return i;
		}
	}
	return -1;
}

int NetcodeSession::ensure_peer_index(int32_t peer_id)
{
	const int existing = find_peer_index(peer_id);
	if (existing >= 0) {
		return existing;
	}
	for (int i = 0; i < MAX_PEERS; ++i) {
		if (!peer_states[i].active) {
			peer_states[i] = PeerState();
			peer_states[i].id = peer_id;
			peer_states[i].active = 1;
			return i;
		}
	}
	return -1;
}

void NetcodeSession::reset()
{
	racer_count = 0;
	local_player_id = -1;
	latest_authoritative_tick = -1;
	stat_auth_packets = 0;
	stat_auth_frames = 0;
	stat_auth_encoded_inputs = 0;
	stat_auth_unchanged_inputs = 0;
	stat_auth_raw_bytes = 0;
	stat_auth_payload_bytes = 0;
	last_local_input = neutral_input;
	for (int i = 0; i < MAX_RACERS; ++i) {
		player_ids[i] = 0;
		cpu_flags[i] = 0;
	}
	clear_peer_state();
	for (int i = 0; i < HISTORY_LEN; ++i) {
		clear_frame(local_input_history[i], -1);
		clear_frame(input_history[i], -1);
		clear_frame(authoritative_history[i], -1);
		clear_frame(pending_inputs[i], -1);
	}
}

void NetcodeSession::configure(godot::Array p_player_ids, godot::Array p_cpu_flags, int p_local_player_id)
{
	racer_count = std::min(static_cast<int>(p_player_ids.size()), MAX_RACERS);
	local_player_id = p_local_player_id;
	latest_authoritative_tick = -1;
	stat_auth_packets = 0;
	stat_auth_frames = 0;
	stat_auth_encoded_inputs = 0;
	stat_auth_unchanged_inputs = 0;
	stat_auth_raw_bytes = 0;
	stat_auth_payload_bytes = 0;
	for (int i = 0; i < racer_count; ++i) {
		player_ids[i] = static_cast<int32_t>(static_cast<int64_t>(p_player_ids[i]));
		cpu_flags[i] = (i < p_cpu_flags.size() && static_cast<bool>(p_cpu_flags[i])) ? 1 : 0;
	}
	for (int i = racer_count; i < MAX_RACERS; ++i) {
		player_ids[i] = 0;
		cpu_flags[i] = 0;
	}
	for (int i = 0; i < HISTORY_LEN; ++i) {
		clear_frame(local_input_history[i], -1);
		clear_frame(input_history[i], -1);
		clear_frame(authoritative_history[i], -1);
		clear_frame(pending_inputs[i], -1);
	}
}

void NetcodeSession::set_local_input(godot::PackedByteArray input_bytes)
{
	last_local_input = PlayerInput::from_bytes(input_bytes);
}

void NetcodeSession::store_local_input(int tick, godot::PackedByteArray input_bytes)
{
	const int index = find_racer_index(local_player_id);
	if (index < 0) {
		return;
	}
	InputFrame& frame = frame_for(local_input_history, tick);
	frame.inputs[index] = PlayerInput::from_bytes(input_bytes);
	frame.present[index] = 1;
	last_local_input = frame.inputs[index];
}

void NetcodeSession::store_authoritative_input(int tick, int player_id, godot::PackedByteArray input_bytes)
{
	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return;
	}
	InputFrame& frame = frame_for(authoritative_history, tick);
	frame.inputs[index] = PlayerInput::from_bytes(input_bytes);
	frame.present[index] = 1;
	latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
}

void NetcodeSession::store_pending_input(int tick, int player_id, godot::PackedByteArray input_bytes)
{
	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return;
	}
	InputFrame& frame = frame_for(pending_inputs, tick);
	frame.inputs[index] = PlayerInput::from_bytes(input_bytes);
	frame.present[index] = 1;
}

godot::Dictionary NetcodeSession::fill_missing_pending_inputs(int tick, godot::Array p_player_ids, godot::Array p_disconnected_ids, godot::Array p_delayed_ids, bool allow_new_delayed)
{
	Dictionary out;
	Array replaced_ids;
	int skipped_present = 0;
	int skipped_unseen = 0;
	int skipped_not_delayed = 0;
	const InputFrame* prev = find_frame(authoritative_history, static_cast<int32_t>(tick - 1));
	InputFrame& frame = frame_for(pending_inputs, static_cast<int32_t>(tick));

	for (int p = 0; p < p_player_ids.size(); ++p) {
		const int32_t player_id = static_cast<int32_t>(static_cast<int64_t>(p_player_ids[p]));
		const int index = find_racer_index(player_id);
		if (index < 0 || cpu_flags[index]) {
			continue;
		}
		if (frame.present[index]) {
			++skipped_present;
			continue;
		}

		bool disconnected = false;
		for (int i = 0; i < p_disconnected_ids.size(); ++i) {
			if (static_cast<int32_t>(static_cast<int64_t>(p_disconnected_ids[i])) == player_id) {
				disconnected = true;
				break;
			}
		}

		bool already_delayed = false;
		if (!disconnected) {
			for (int i = 0; i < p_delayed_ids.size(); ++i) {
				if (static_cast<int32_t>(static_cast<int64_t>(p_delayed_ids[i])) == player_id) {
					already_delayed = true;
					break;
				}
			}
		}

		if (!disconnected && !peer_has_received(player_id)) {
			++skipped_unseen;
			continue;
		}
		if (!disconnected && !allow_new_delayed && !already_delayed) {
			++skipped_not_delayed;
			continue;
		}

		frame.inputs[index] = (prev && prev->present[index]) ? prev->inputs[index] : neutral_input;
		frame.present[index] = 1;
		replaced_ids.append(player_id);
	}

	out["replaced_ids"] = replaced_ids;
	out["replaced_count"] = replaced_ids.size();
	out["skipped_present"] = skipped_present;
	out["skipped_unseen"] = skipped_unseen;
	out["skipped_not_delayed"] = skipped_not_delayed;
	return out;
}

godot::PackedByteArray NetcodeSession::build_local_input_packet(int first_tick, int count, int race_phase) const
{
	PacketWriter writer;
	const int index = find_racer_index(local_player_id);
	if (index < 0 || count <= 0) {
		return writer.to_pba();
	}
	count = std::min(count, 255);
	if (!writer.write_i32(pack_tick_phase(first_tick, race_phase)) ||
		!writer.write_u8(static_cast<uint8_t>(count))) {
		return PackedByteArray();
	}
	for (int i = 0; i < count; ++i) {
		const int tick = first_tick + i;
		const InputFrame* frame = find_frame(local_input_history, tick);
		const PlayerInput& input = (frame && frame->present[index]) ? frame->inputs[index] : last_local_input;
		uint8_t encoded[MXT_NET_MAX_INPUT_BYTES] = {};
		const int encoded_len = PlayerInput::encode_to_raw(input, encoded, MXT_NET_MAX_INPUT_BYTES);
		if (!writer.write_bytes(encoded, encoded_len)) {
			return PackedByteArray();
		}
	}
	return writer.to_pba();
}

godot::Dictionary NetcodeSession::store_pending_input_packet(int player_id, int reject_before_tick, godot::PackedByteArray packet, double ahead, double now_sec, int expected_race_phase)
{
	Dictionary stats;
	stats["start_tick"] = -1;
	stats["count"] = 0;
	stats["accepted"] = 0;
	stats["dropped"] = 0;
	stats["last_tick"] = -1;
	stats["valid"] = false;
	stats["stale"] = false;
	stats["seen_before"] = false;

	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return stats;
	}
	const int peer_index = ensure_peer_index(static_cast<int32_t>(player_id));
	const bool seen_before = peer_index >= 0 && peer_states[peer_index].last_received_tick >= 0;
	stats["seen_before"] = seen_before;
	if (peer_index >= 0) {
		peer_states[peer_index].desired_ahead = static_cast<float>(ahead);
	}
	PacketReader reader(packet);
	uint8_t count = 0;
	int32_t packed_start_tick = -1;
	if (!reader.read_i32(packed_start_tick) || !reader.read_u8(count)) {
		return stats;
	}
	if (unpack_race_phase(packed_start_tick) != (expected_race_phase & 1)) {
		stats["stale"] = true;
		stats["valid"] = true;
		return stats;
	}
	const int start_tick = unpack_tick(packed_start_tick);
	stats["start_tick"] = start_tick;
	stats["count"] = static_cast<int>(count);
	stats["valid"] = true;
	if (count > 0 && start_tick + static_cast<int32_t>(count) <= reject_before_tick) {
		stats["dropped"] = static_cast<int>(count);
		return stats;
	}

	int accepted = 0;
	int dropped = 0;
	int last_tick = -1;
	for (int i = 0; i < static_cast<int>(count); ++i) {
		const uint8_t* bytes = nullptr;
		if (!reader.read_bytes(bytes, 1)) {
			stats["valid"] = false;
			break;
		}
		const int len = PlayerInput::encoded_raw_size_from_mask(bytes[0]);
		reader.pos -= 1;
		if (len > MXT_NET_MAX_INPUT_BYTES || !reader.read_bytes(bytes, len)) {
			stats["valid"] = false;
			break;
		}
		const int tick = start_tick + i;
		if (tick < reject_before_tick) {
			++dropped;
			continue;
		}
		InputFrame& frame = frame_for(pending_inputs, tick);
		frame.inputs[index] = PlayerInput::from_raw(bytes, len);
		frame.present[index] = 1;
		++accepted;
		last_tick = tick;
	}
	stats["accepted"] = accepted;
	stats["dropped"] = dropped;
	stats["last_tick"] = last_tick;
	if (accepted > 0 && peer_index >= 0) {
		peer_states[peer_index].last_received_tick = last_tick;
		peer_states[peer_index].last_input_time = now_sec;
	}
	return stats;
}

godot::PackedByteArray NetcodeSession::build_authoritative_input_packet(int last_tick, int max_frame_count, int race_phase) const
{
	PacketWriter writer;
	if (max_frame_count <= 0 || last_tick < 0) {
		writer.write_u24(static_cast<uint32_t>(std::max(last_tick + 1, 0)) & 0x00ffffffu);
		writer.write_u8(pack_auth_mode_count_phase(MXT_NET_AUTH_MODE_PACKED_PLAIN_ZSTD, 0, race_phase));
		writer.write_u8(0);
		return writer.to_pba();
	}
	max_frame_count = std::min(max_frame_count, MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET);
	int first_tick = last_tick - max_frame_count + 1;
	if (first_tick < 0) {
		first_tick = 0;
	}
	while (first_tick <= last_tick && !find_frame(authoritative_history, first_tick)) {
		++first_tick;
	}
	int count = last_tick >= first_tick ? last_tick - first_tick + 1 : 0;
	while (count > 0 && !find_frame(authoritative_history, first_tick + count - 1)) {
		--count;
	}
	if (!writer.write_u24(static_cast<uint32_t>(first_tick) & 0x00ffffffu)) {
		return PackedByteArray();
	}
	const int mode_count_pos = writer.pos;
	if (!writer.write_u8(pack_auth_mode_count_phase(MXT_NET_AUTH_MODE_PACKED_PLAIN_ZSTD, count, race_phase))) {
		return PackedByteArray();
	}
	if (auth_input_count_needs_escape(count) && !writer.write_u8(static_cast<uint8_t>(count))) {
		return PackedByteArray();
	}
	++stat_auth_packets;
	stat_auth_frames += static_cast<uint64_t>(count);
	stat_auth_encoded_inputs += static_cast<uint64_t>(count) * static_cast<uint64_t>(racer_count);
	if (count == 0 || racer_count <= 0) {
		stat_auth_payload_bytes += static_cast<uint64_t>(writer.pos);
		return writer.to_pba();
	}
	const int packed_raw_size = auth_input_raw_size(count, racer_count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS);
	const int bitpacked_raw_size = auth_input_raw_size(count, racer_count, AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS);
	const int hybrid_raw_size = auth_input_raw_size(count, racer_count, AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA);
	PackedByteArray delta_raw;
	if (delta_raw.resize(packed_raw_size) != 0) {
		return PackedByteArray();
	}
	const InputFrame* frames[MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET] = {};
	for (int f = 0; f < count; ++f) {
		frames[f] = find_frame(authoritative_history, first_tick + f);
		if (!frames[f]) {
			return PackedByteArray();
		}
	}
	if (!write_authoritative_input_raw(delta_raw, frames, count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA)) {
		return PackedByteArray();
	}
	PackedByteArray packed_raw;
	if (packed_raw.resize(packed_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(packed_raw, frames, count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS)) {
		return PackedByteArray();
	}
	PackedByteArray bitpacked_raw;
	if (bitpacked_raw.resize(bitpacked_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(bitpacked_raw, frames, count, AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS)) {
		return PackedByteArray();
	}
	PackedByteArray hybrid_raw;
	if (hybrid_raw.resize(hybrid_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(hybrid_raw, frames, count, AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA)) {
		return PackedByteArray();
	}
	PackedByteArray compressed = delta_raw.compress(MXT_NET_COMPRESSION_ZSTD);
	uint8_t compression_mode = MXT_NET_AUTH_MODE_DELTA_PLAIN_ZSTD;
	AuthInputLayout selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA;
	int selected_raw_size = packed_raw_size;
	PackedByteArray candidate = compress_auth_input_with_dict(delta_raw);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_DELTA_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA;
		selected_raw_size = packed_raw_size;
	}
	candidate = packed_raw.compress(MXT_NET_COMPRESSION_ZSTD);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_PACKED_PLAIN_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS;
		selected_raw_size = packed_raw_size;
	}
	candidate = compress_auth_input_with_dict(packed_raw);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_PACKED_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS;
		selected_raw_size = packed_raw_size;
	}
	candidate = bitpacked_raw.compress(MXT_NET_COMPRESSION_ZSTD);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_PLAIN_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS;
		selected_raw_size = bitpacked_raw_size;
	}
	candidate = compress_auth_input_with_dict(bitpacked_raw);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS;
		selected_raw_size = bitpacked_raw_size;
	}
	candidate = hybrid_raw.compress(MXT_NET_COMPRESSION_ZSTD);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_DELTA_PLAIN_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
		selected_raw_size = hybrid_raw_size;
	}
	candidate = compress_auth_input_with_dict(hybrid_raw, true);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_DELTA_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
		selected_raw_size = hybrid_raw_size;
	}
	if (compressed.size() <= 0 || !writer.write_bytes(compressed.ptr(), static_cast<int>(compressed.size()))) {
		return PackedByteArray();
	}
	if (selected_layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA) {
		dump_auth_input_sample(delta_raw, first_tick, count, racer_count);
	} else if (selected_layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS) {
		dump_auth_input_sample(bitpacked_raw, first_tick, count, racer_count);
	} else if (selected_layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA) {
		dump_auth_input_sample(hybrid_raw, first_tick, count, racer_count);
	} else {
		dump_auth_input_sample(packed_raw, first_tick, count, racer_count);
	}
	writer.data[mode_count_pos] = pack_auth_mode_count_phase(compression_mode, count, race_phase);
	stat_auth_raw_bytes += static_cast<uint64_t>(selected_raw_size);
	stat_auth_payload_bytes += static_cast<uint64_t>(writer.pos);
	return writer.to_pba();
}

godot::Dictionary NetcodeSession::store_authoritative_input_packet(godot::PackedByteArray packet, int expected_race_phase)
{
	Dictionary stats;
	stats["first_tick"] = -1;
	stats["last_tick"] = -1;
	stats["count"] = 0;
	stats["valid"] = false;
	stats["stale"] = false;

	PacketReader reader(packet);
	uint8_t count = 0;
	uint8_t mode_count_phase = MXT_NET_AUTH_MODE_PACKED_PLAIN_ZSTD;
	uint32_t packed_first_tick = 0;
	if (!reader.read_u24(packed_first_tick) || !reader.read_u8(mode_count_phase)) {
		return stats;
	}
	const uint8_t count_code = static_cast<uint8_t>((mode_count_phase & MXT_NET_AUTH_COUNT_MASK) >> MXT_NET_AUTH_COUNT_SHIFT);
	if (count_code == MXT_NET_AUTH_COUNT_ESCAPE) {
		if (!reader.read_u8(count)) {
			return stats;
		}
	} else {
		count = static_cast<uint8_t>(count_code + 1);
	}
	if (((mode_count_phase & MXT_NET_AUTH_PHASE_BIT) ? 1 : 0) != (expected_race_phase & 1)) {
		stats["stale"] = true;
		stats["valid"] = true;
		return stats;
	}
	const uint8_t compression_mode = mode_count_phase & MXT_NET_AUTH_MODE_MASK;
	const int first_tick = static_cast<int>(packed_first_tick);
	stats["first_tick"] = first_tick;
	stats["count"] = static_cast<int>(count);
	stats["valid"] = true;
	if (!auth_input_mode_valid(compression_mode)) {
		stats["valid"] = false;
		return stats;
	}
	const AuthInputLayout layout = auth_input_layout_from_mode(compression_mode);
	const int raw_size = auth_input_raw_size(static_cast<int>(count), racer_count, layout);
	if (raw_size < 0) {
		stats["valid"] = false;
		return stats;
	}
	if (count == 0) {
		return stats;
	}
	PackedByteArray compressed = packet.slice(reader.pos, packet.size());
	PackedByteArray raw;
	if (auth_input_mode_uses_dict(compression_mode)) {
		raw = decompress_auth_input_with_dict(
			compressed,
			raw_size,
			auth_input_mode_uses_hybrid_dict(compression_mode)
		);
	} else {
		raw = compressed.decompress(raw_size, MXT_NET_COMPRESSION_ZSTD);
	}
	if (raw.size() != raw_size) {
		stats["valid"] = false;
		return stats;
	}
	const uint8_t* raw_bytes = raw.ptr();
	int raw_pos = 0;
	const bool delta_layout = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA;
	const bool analog_delta_layout = delta_layout || layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	InputFrame* frames[MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET] = {};
	for (int f = 0; f < static_cast<int>(count); ++f) {
		const int tick = first_tick + f;
		InputFrame& frame = frame_for(authoritative_history, tick);
		clear_frame(frame, tick);
		frames[f] = &frame;
		for (int i = 0; i < racer_count; ++i) {
			frame.inputs[i] = neutral_input;
			frame.present[i] = 1;
		}
	}
	uint8_t previous_values[MAX_RACERS] = {};
	if (layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS || layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA) {
		const int input_count = static_cast<int>(count) * racer_count;
		const int bitset_bytes = (input_count + 7) >> 3;
		for (int f = 0; f < static_cast<int>(count); ++f) {
			InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const int input_index = f * racer_count + i;
				uint8_t mask = 0;
				for (int bit = 0; bit < 5; ++bit) {
					if ((raw_bytes[bit * bitset_bytes + (input_index >> 3)] & static_cast<uint8_t>(1u << (input_index & 7))) != 0) {
						mask |= static_cast<uint8_t>(1u << bit);
					}
				}
				frame->inputs[i].accelerate = (mask & (1u << 0)) != 0 ? 1.0f : 0.0f;
				frame->inputs[i].brake = (mask & (1u << 1)) != 0 ? 1.0f : 0.0f;
				frame->inputs[i].spinattack = (mask & (1u << 2)) != 0;
				frame->inputs[i].sideattack = (mask & (1u << 3)) != 0;
				frame->inputs[i].boost = (mask & (1u << 4)) != 0;
			}
		}
		raw_pos = bitset_bytes * 5;
	} else {
		for (int f = 0; f < static_cast<int>(count); ++f) {
			InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				uint8_t mask = raw_bytes[raw_pos++];
				if (delta_layout && f > 0) {
					mask = static_cast<uint8_t>(static_cast<int>(previous_values[i]) + unzigzag_i8(mask));
				}
				previous_values[i] = mask;
				frame->inputs[i].accelerate = (mask & (1u << 0)) != 0 ? 1.0f : 0.0f;
				frame->inputs[i].brake = (mask & (1u << 1)) != 0 ? 1.0f : 0.0f;
				frame->inputs[i].spinattack = (mask & (1u << 2)) != 0;
				frame->inputs[i].sideattack = (mask & (1u << 3)) != 0;
				frame->inputs[i].boost = (mask & (1u << 4)) != 0;
			}
		}
	}
	std::memset(previous_values, 0, sizeof(previous_values));
	for (int f = 0; f < static_cast<int>(count); ++f) {
		InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			uint8_t value = raw_bytes[raw_pos++];
			if (analog_delta_layout && f > 0) {
				value = static_cast<uint8_t>(static_cast<int>(previous_values[i]) + unzigzag_i8(value));
			}
			previous_values[i] = value;
			frame->inputs[i].strafe_left = trigger_from_byte(value);
		}
	}
	std::memset(previous_values, 0, sizeof(previous_values));
	for (int f = 0; f < static_cast<int>(count); ++f) {
		InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			uint8_t value = raw_bytes[raw_pos++];
			if (analog_delta_layout && f > 0) {
				value = static_cast<uint8_t>(static_cast<int>(previous_values[i]) + unzigzag_i8(value));
			}
			previous_values[i] = value;
			frame->inputs[i].strafe_right = trigger_from_byte(value);
		}
	}
	std::memset(previous_values, 0, sizeof(previous_values));
	for (int f = 0; f < static_cast<int>(count); ++f) {
		InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			uint8_t value = raw_bytes[raw_pos++];
			if (analog_delta_layout && f > 0) {
				value = static_cast<uint8_t>(static_cast<int>(previous_values[i]) + unzigzag_i8(value));
			}
			previous_values[i] = value;
			frame->inputs[i].steer_horizontal = axis_from_byte(value);
		}
	}
	std::memset(previous_values, 0, sizeof(previous_values));
	for (int f = 0; f < static_cast<int>(count); ++f) {
		InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			uint8_t value = raw_bytes[raw_pos++];
			if (analog_delta_layout && f > 0) {
				value = static_cast<uint8_t>(static_cast<int>(previous_values[i]) + unzigzag_i8(value));
			}
			previous_values[i] = value;
			frame->inputs[i].steer_vertical = axis_from_byte(value);
		}
	}
	for (int f = 0; f < static_cast<int>(count); ++f) {
		const int tick = first_tick + f;
		latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
		stats["last_tick"] = tick;
	}
	return stats;
}

godot::Dictionary NetcodeSession::debug_compare_authoritative_input_packet_sizes(int last_tick, int max_frame_count, int race_phase) const
{
	Dictionary out;
	out["valid"] = false;
	out["first_tick"] = -1;
	out["last_tick"] = -1;
	out["count"] = 0;
	out["racer_count"] = racer_count;
	out["race_phase"] = race_phase & 1;
	if (max_frame_count <= 0 || last_tick < 0 || racer_count <= 0) {
		return out;
	}
	max_frame_count = std::min(max_frame_count, MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET);
	int first_tick = last_tick - max_frame_count + 1;
	if (first_tick < 0) {
		first_tick = 0;
	}
	while (first_tick <= last_tick && !find_frame(authoritative_history, first_tick)) {
		++first_tick;
	}
	int count = last_tick >= first_tick ? last_tick - first_tick + 1 : 0;
	while (count > 0 && !find_frame(authoritative_history, first_tick + count - 1)) {
		--count;
	}
	if (count <= 0) {
		return out;
	}
	const InputFrame* frames[MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET] = {};
	for (int f = 0; f < count; ++f) {
		frames[f] = find_frame(authoritative_history, first_tick + f);
		if (!frames[f]) {
			return out;
		}
	}
	const AuthInputLayout layouts[5] = {
		AUTH_INPUT_LAYOUT_OLD_BYTE_PLANES,
		AUTH_INPUT_LAYOUT_PACKED_BUTTONS,
		AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA,
		AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS,
		AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA,
	};
	const char* names[5] = {
		"old",
		"packed",
		"delta",
		"bitpacked",
		"hybrid",
	};
	for (int l = 0; l < 5; ++l) {
		const AuthInputLayout layout = layouts[l];
		const int raw_size = auth_input_raw_size(count, racer_count, layout);
		PackedByteArray raw;
		if (raw.resize(raw_size) != 0) {
			return out;
		}
		if (!write_authoritative_input_raw(raw, frames, count, layout)) {
			return out;
		}
		const PackedByteArray plain = raw.compress(MXT_NET_COMPRESSION_ZSTD);
		const PackedByteArray dict = compress_auth_input_with_dict(raw);
		const PackedByteArray hybrid_dict = layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA ?
			compress_auth_input_with_dict(raw, true) :
			PackedByteArray();
		const String prefix = String(names[l]) + String("_");
		out[prefix + String("raw")] = raw_size;
		out[prefix + String("plain_payload")] = plain.size();
		out[prefix + String("plain_packet")] = plain.size() + auth_input_packet_header_size(count);
		const int dict_size = hybrid_dict.size() > 0 ? hybrid_dict.size() : dict.size();
		out[prefix + String("dict_payload")] = dict_size;
		out[prefix + String("dict_packet")] = dict_size + auth_input_packet_header_size(count);
	}
	out["valid"] = true;
	out["first_tick"] = first_tick;
	out["last_tick"] = first_tick + count - 1;
	out["count"] = count;
	return out;
}

godot::Dictionary NetcodeSession::consume_authoritative_packet_stats()
{
	Dictionary stats;
	stats["auth_packets"] = static_cast<int64_t>(stat_auth_packets);
	stats["auth_frames"] = static_cast<int64_t>(stat_auth_frames);
	stats["auth_encoded_inputs"] = static_cast<int64_t>(stat_auth_encoded_inputs);
	stats["auth_unchanged_inputs"] = static_cast<int64_t>(stat_auth_unchanged_inputs);
	stats["auth_raw_bytes"] = static_cast<int64_t>(stat_auth_raw_bytes);
	stats["auth_payload_bytes"] = static_cast<int64_t>(stat_auth_payload_bytes);
	stat_auth_packets = 0;
	stat_auth_frames = 0;
	stat_auth_encoded_inputs = 0;
	stat_auth_unchanged_inputs = 0;
	stat_auth_raw_bytes = 0;
	stat_auth_payload_bytes = 0;
	return stats;
}

godot::Dictionary NetcodeSession::get_input_frame_debug(int tick) const
{
	Dictionary out;
	const InputFrame* frame = find_frame(pending_inputs, tick);
	out["tick"] = tick;
	out["frame_found"] = frame != nullptr;
	out["racer_count"] = racer_count;
	int human_count = 0;
	int cpu_count = 0;
	int present_humans = 0;
	int first_missing_slot = -1;
	int first_missing_player_id = 0;
	bool first_missing_cpu = false;
	for (int i = 0; i < racer_count; ++i) {
		if (cpu_flags[i]) {
			++cpu_count;
			continue;
		}
		++human_count;
		if (frame && frame->present[i]) {
			++present_humans;
		} else if (first_missing_slot < 0) {
			first_missing_slot = i;
			first_missing_player_id = player_ids[i];
			first_missing_cpu = cpu_flags[i] != 0;
		}
	}
	out["human_count"] = human_count;
	out["cpu_count"] = cpu_count;
	out["present_humans"] = present_humans;
	out["first_missing_slot"] = first_missing_slot;
	out["first_missing_player_id"] = first_missing_player_id;
	out["first_missing_cpu"] = first_missing_cpu;
	return out;
}

void NetcodeSession::clear_peer_state()
{
	for (int i = 0; i < MAX_PEERS; ++i) {
		peer_states[i] = PeerState();
	}
}

void NetcodeSession::remove_peer(int peer_id)
{
	const int index = find_peer_index(static_cast<int32_t>(peer_id));
	if (index >= 0) {
		peer_states[index] = PeerState();
	}
}

void NetcodeSession::set_peer_last_received(int peer_id, int tick, double now_sec)
{
	const int index = ensure_peer_index(static_cast<int32_t>(peer_id));
	if (index < 0) {
		return;
	}
	peer_states[index].last_received_tick = static_cast<int32_t>(tick);
	peer_states[index].last_input_time = now_sec;
}

int NetcodeSession::get_peer_last_received(int peer_id) const
{
	const int index = find_peer_index(static_cast<int32_t>(peer_id));
	return index >= 0 ? peer_states[index].last_received_tick : -1;
}

bool NetcodeSession::peer_has_received(int peer_id) const
{
	return get_peer_last_received(peer_id) >= 0;
}

void NetcodeSession::set_peer_desired_ahead(int peer_id, double ahead)
{
	const int index = ensure_peer_index(static_cast<int32_t>(peer_id));
	if (index < 0) {
		return;
	}
	peer_states[index].desired_ahead = static_cast<float>(ahead);
}

double NetcodeSession::get_max_peer_desired_ahead(godot::Array p_peer_ids, double fallback) const
{
	double max_ahead = fallback;
	for (int i = 0; i < p_peer_ids.size(); ++i) {
		const int peer_id = static_cast<int32_t>(static_cast<int64_t>(p_peer_ids[i]));
		const int index = find_peer_index(peer_id);
		if (index >= 0 && peer_states[index].desired_ahead > max_ahead) {
			max_ahead = peer_states[index].desired_ahead;
		}
	}
	return max_ahead;
}

double NetcodeSession::get_peer_last_input_time(int peer_id) const
{
	const int index = find_peer_index(static_cast<int32_t>(peer_id));
	return index >= 0 ? peer_states[index].last_input_time : 0.0;
}

bool NetcodeSession::server_has_full_input_frame(int tick) const
{
	const InputFrame* frame = find_frame(pending_inputs, tick);
	if (!frame) {
		return false;
	}
	for (int i = 0; i < racer_count; ++i) {
		if (!cpu_flags[i] && !frame->present[i]) {
			return false;
		}
	}
	return true;
}

bool NetcodeSession::tick_server_frame(godot::Object* game_sim_obj, int tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim || !server_has_full_input_frame(tick)) {
		return false;
	}
	InputFrame& pending = frame_for(pending_inputs, tick);
	InputFrame& authoritative = frame_for(authoritative_history, tick);
	clear_frame(authoritative, tick);
	for (int i = 0; i < racer_count; ++i) {
		if (cpu_flags[i]) {
			godot::PackedByteArray cpu_bytes = sim->generate_native_cpu_input_for_tick(player_ids[i], tick);
			authoritative.inputs[i] = PlayerInput::from_bytes(cpu_bytes);
			authoritative.present[i] = 1;
		} else if (pending.present[i]) {
			authoritative.inputs[i] = pending.inputs[i];
			authoritative.present[i] = 1;
		}
	}
	sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
		-1, nullptr, authoritative.inputs, authoritative.present, racer_count);
	latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
	return true;
}

bool NetcodeSession::tick_client_predicted_frame(godot::Object* game_sim_obj, int tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim) {
		return false;
	}
	recalculate_predictions_internal(sim, tick, tick + 1);
	const InputFrame* frame = find_frame(input_history, tick);
	if (!frame) {
		return false;
	}
	sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
		-1, nullptr, frame->inputs, frame->present, racer_count);
	return true;
}

void NetcodeSession::recalculate_predictions(int start_tick, int end_tick)
{
	recalculate_predictions_internal(nullptr, start_tick, end_tick);
}

void NetcodeSession::recalculate_predictions_internal(GameSim* sim, int start_tick, int end_tick)
{
	for (int tick = start_tick; tick < end_tick; ++tick) {
		InputFrame& frame = frame_for(input_history, tick);
		clear_frame(frame, tick);
		const InputFrame* authoritative = find_frame(authoritative_history, tick);
		const InputFrame* previous = find_frame(input_history, tick - 1);
		for (int i = 0; i < racer_count; ++i) {
			if (cpu_flags[i]) {
				if (authoritative && authoritative->present[i]) {
					frame.inputs[i] = authoritative->inputs[i];
					frame.present[i] = 1;
				} else if (sim) {
					godot::PackedByteArray cpu_bytes = sim->generate_native_cpu_input_for_tick(player_ids[i], tick);
					frame.inputs[i] = PlayerInput::from_bytes(cpu_bytes);
					frame.present[i] = 1;
				} else if (previous && previous->present[i]) {
					frame.inputs[i] = previous->inputs[i];
					frame.present[i] = 1;
				}
				continue;
			}
			if (authoritative && authoritative->present[i]) {
				frame.inputs[i] = authoritative->inputs[i];
				frame.present[i] = 1;
			} else if (player_ids[i] == local_player_id) {
				const InputFrame* local = find_frame(local_input_history, tick);
				frame.inputs[i] = (local && local->present[i]) ? local->inputs[i] : last_local_input;
				frame.present[i] = 1;
			} else if (previous && previous->present[i]) {
				frame.inputs[i] = decay_predicted_input(previous->inputs[i]);
				frame.present[i] = 1;
			}
		}
	}
}

bool NetcodeSession::replay_history(godot::Object* game_sim_obj, int start_tick, int end_tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim) {
		return false;
	}
	for (int tick = start_tick; tick < end_tick; ++tick) {
		recalculate_predictions_internal(sim, tick, tick + 1);
		const InputFrame* frame = find_frame(input_history, tick);
		if (!frame) {
			sim->finish_render_rollback_correction_capture();
			return false;
		}
		sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
			-1, nullptr, frame->inputs, frame->present, racer_count);
	}
	sim->finish_render_rollback_correction_capture();
	return true;
}

godot::Dictionary NetcodeSession::get_frame_as_dictionary(int tick) const
{
	godot::Dictionary out;
	const InputFrame* frame = find_frame(authoritative_history, tick);
	if (!frame) {
		frame = find_frame(input_history, tick);
	}
	if (!frame) {
		return out;
	}
	for (int i = 0; i < racer_count; ++i) {
		if (frame->present[i]) {
			out[player_ids[i]] = PlayerInput::to_bytes(frame->inputs[i]);
		}
	}
	return out;
}

void NetcodeSession::configure_authoritative_input_sample_dump(bool enabled, int limit, godot::String directory)
{
	g_auth_input_sample_dump_enabled = enabled;
	g_auth_input_sample_dump_limit = std::max<int64_t>(0, static_cast<int64_t>(limit));
	g_auth_input_sample_dump_index = 0;
	set_auth_input_sample_dump_dir(directory);
	if (g_auth_input_sample_dump_enabled) {
		DirAccess::make_dir_recursive_absolute(String(g_auth_input_sample_dump_dir));
	}
}
