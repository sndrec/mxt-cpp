#include "mxt_core/netcode_session.h"

#include "main.h"
#include "mxt_core/auth_input_delta_low_entropy_alt_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_s1_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_s11_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_surface_alt_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_surface_fallback_alt_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_surface_fallback_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_low_entropy_surface_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_pairs_surface_zstd_dictionary.h"
#include "mxt_core/auth_input_delta_pairs_zstd_dictionary.h"
#include "mxt_core/auth_input_hybrid_zstd_dictionary.h"
#include "mxt_core/auth_input_hybrid_smooth_zstd_dictionary.h"
#include "mxt_core/auth_input_zstd_dictionary.h"
#include "mxt_core/auth_input_zero_bitmap_zstd_dictionary.h"
#include "godot_cpp/classes/dir_access.hpp"
#include "godot_cpp/classes/file_access.hpp"
#include "godot_cpp/core/class_db.hpp"
#define ZSTD_STATIC_LINKING_ONLY
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
constexpr int MXT_NET_AUTHORITATIVE_INPUT_PACKET_HEADER_BYTES = 1;
constexpr int MXT_NET_COMPRESSION_ZSTD = FileAccess::COMPRESSION_ZSTD;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD = 0;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD = 1;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_DICT_ZSTD = 2;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_SURFACE_DICT_ZSTD = 3;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT_ZSTD = 4;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD = 5;
constexpr uint8_t MXT_NET_AUTH_MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD = 6;
constexpr uint8_t MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD = 7;
constexpr uint8_t MXT_NET_AUTH_MODE_MASK = 0x07;
constexpr uint8_t MXT_NET_AUTH_COUNT_SHIFT = 3;
constexpr uint8_t MXT_NET_AUTH_COUNT_MASK = 0x78;
constexpr uint8_t MXT_NET_AUTH_COUNT_ESCAPE = 0x0f;
constexpr uint8_t MXT_NET_AUTH_PHASE_BIT = 0x80;
constexpr int MXT_NET_AUTH_ZSTD_LEVEL = 3;
constexpr int MXT_NET_AUTH_DELTA_PAIRS_ZSTD_LEVEL = 12;
constexpr int MXT_NET_AUTH_DELTA_PAIRS_SURFACE_ZSTD_LEVEL = 7;
constexpr int MXT_NET_AUTH_ZERO_BITMAP_ZSTD_LEVEL = 7;
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
	AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP = 5,
	AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS = 6,
	AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY = 7,
};
bool g_auth_input_sample_dump_enabled = false;
int64_t g_auth_input_sample_dump_limit = MXT_NET_DEFAULT_AUTH_SAMPLE_LIMIT;
int64_t g_auth_input_sample_dump_index = 0;
char g_auth_input_sample_dump_dir[MXT_NET_AUTH_SAMPLE_DIR_BYTES] = "user://auth_input_samples";
ZSTD_CCtx* g_auth_input_zstd_cctx = nullptr;
ZSTD_DCtx* g_auth_input_zstd_dctx = nullptr;
ZSTD_CDict* g_auth_input_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_alt_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_alt_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_s1_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_s1_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_s11_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_s11_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_surface_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_surface_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_surface_alt_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_surface_alt_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_surface_fallback_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_surface_fallback_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_pairs_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_pairs_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_delta_pairs_surface_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_delta_pairs_surface_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_hybrid_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_hybrid_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_hybrid_smooth_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_hybrid_smooth_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_zero_bitmap_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_zero_bitmap_zstd_ddict = nullptr;

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
	if (layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS ||
		layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA ||
		layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP) {
		const int input_count = frame_count * racer_count;
		const int bitpacked_size = (((input_count + 7) >> 3) * 5) + input_count * 4;
		if (layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP) {
			return ((bitpacked_size + 7) >> 3) + bitpacked_size;
		}
		return bitpacked_size;
	}
	const int bytes_per_racer = layout == AUTH_INPUT_LAYOUT_OLD_BYTE_PLANES ?
		MXT_NET_AUTHORITATIVE_INPUT_OLD_BYTES_PER_RACER :
		MXT_NET_AUTHORITATIVE_INPUT_BYTES_PER_RACER;
	return frame_count * racer_count * bytes_per_racer;
}

AuthInputLayout auth_input_layout_from_mode(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	if (mode == MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_SURFACE_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS;
	}
	if (mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY;
	}
	if (mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	}
	if (mode == MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD ||
		mode == MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD) {
		return AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
	}
	return AUTH_INPUT_LAYOUT_PACKED_BUTTONS;
}

bool auth_input_mode_uses_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_SURFACE_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD ||
		mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD;
}

bool auth_input_mode_valid(uint8_t mode)
{
	return (mode & MXT_NET_AUTH_MODE_MASK) <= MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD;
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

uint8_t pack_auth_low_entropy_mode_sublayout_phase(uint8_t mode, uint8_t sublayout, int race_phase)
{
	return static_cast<uint8_t>(
		(mode & MXT_NET_AUTH_MODE_MASK) |
		static_cast<uint8_t>((sublayout & 0x0f) << MXT_NET_AUTH_COUNT_SHIFT) |
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

ZSTD_CDict* create_auth_input_zstd_cdict(const void* dict, size_t dict_size, int level, ZSTD_strategy strategy)
{
	ZSTD_compressionParameters params = ZSTD_getCParams(level, 1024, dict_size);
	params.strategy = strategy;
	const size_t check = ZSTD_checkCParams(params);
	if (ZSTD_isError(check)) {
		return nullptr;
	}
	return ZSTD_createCDict_advanced(
		dict,
		dict_size,
		ZSTD_dlm_byCopy,
		ZSTD_dct_auto,
		params,
		ZSTD_defaultCMem
	);
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

ZSTD_CDict* auth_input_delta_pairs_zstd_cdict()
{
	if (!g_auth_input_delta_pairs_zstd_cdict) {
		g_auth_input_delta_pairs_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_PAIRS_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_PAIRS_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_ZSTD_LEVEL,
			ZSTD_btopt
		);
	}
	return g_auth_input_delta_pairs_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_pairs_surface_zstd_cdict()
{
	if (!g_auth_input_delta_pairs_surface_zstd_cdict) {
		g_auth_input_delta_pairs_surface_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_PAIRS_SURFACE_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_PAIRS_SURFACE_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_SURFACE_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_delta_pairs_surface_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_zstd_cdict) {
		g_auth_input_delta_low_entropy_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_ZSTD_LEVEL,
			ZSTD_btopt
		);
	}
	return g_auth_input_delta_low_entropy_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_alt_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_alt_zstd_cdict) {
		g_auth_input_delta_low_entropy_alt_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ALT_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ALT_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_ZSTD_LEVEL,
			ZSTD_btopt
		);
	}
	return g_auth_input_delta_low_entropy_alt_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_s1_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_s1_zstd_cdict) {
		g_auth_input_delta_low_entropy_s1_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S1_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S1_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_ZSTD_LEVEL,
			ZSTD_btopt
		);
	}
	return g_auth_input_delta_low_entropy_s1_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_s11_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_s11_zstd_cdict) {
		g_auth_input_delta_low_entropy_s11_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S11_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S11_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_ZSTD_LEVEL,
			ZSTD_btopt
		);
	}
	return g_auth_input_delta_low_entropy_s11_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_surface_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_surface_zstd_cdict) {
		g_auth_input_delta_low_entropy_surface_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_SURFACE_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_delta_low_entropy_surface_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_surface_alt_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_surface_alt_zstd_cdict) {
		g_auth_input_delta_low_entropy_surface_alt_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ALT_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ALT_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_SURFACE_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_delta_low_entropy_surface_alt_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_surface_fallback_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_surface_fallback_zstd_cdict) {
		g_auth_input_delta_low_entropy_surface_fallback_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_SURFACE_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_delta_low_entropy_surface_fallback_zstd_cdict;
}

ZSTD_CDict* auth_input_delta_low_entropy_surface_fallback_alt_zstd_cdict()
{
	if (!g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_cdict) {
		g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ALT_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ALT_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_DELTA_PAIRS_SURFACE_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_cdict;
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

ZSTD_CDict* auth_input_hybrid_smooth_zstd_cdict()
{
	if (!g_auth_input_hybrid_smooth_zstd_cdict) {
		g_auth_input_hybrid_smooth_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_HYBRID_SMOOTH_ZSTD_DICT,
			MXT_AUTH_INPUT_HYBRID_SMOOTH_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_hybrid_smooth_zstd_cdict;
}

ZSTD_CDict* auth_input_zero_bitmap_zstd_cdict()
{
	if (!g_auth_input_zero_bitmap_zstd_cdict) {
		g_auth_input_zero_bitmap_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_ZERO_BITMAP_ZSTD_DICT,
			MXT_AUTH_INPUT_ZERO_BITMAP_ZSTD_DICT_SIZE,
			MXT_NET_AUTH_ZERO_BITMAP_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_zero_bitmap_zstd_cdict;
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

ZSTD_DDict* auth_input_delta_pairs_zstd_ddict()
{
	if (!g_auth_input_delta_pairs_zstd_ddict) {
		g_auth_input_delta_pairs_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_PAIRS_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_PAIRS_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_pairs_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_pairs_surface_zstd_ddict()
{
	if (!g_auth_input_delta_pairs_surface_zstd_ddict) {
		g_auth_input_delta_pairs_surface_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_PAIRS_SURFACE_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_PAIRS_SURFACE_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_pairs_surface_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_zstd_ddict) {
		g_auth_input_delta_low_entropy_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_alt_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_alt_zstd_ddict) {
		g_auth_input_delta_low_entropy_alt_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ALT_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_ALT_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_alt_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_s1_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_s1_zstd_ddict) {
		g_auth_input_delta_low_entropy_s1_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S1_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S1_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_s1_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_s11_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_s11_zstd_ddict) {
		g_auth_input_delta_low_entropy_s11_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S11_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_S11_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_s11_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_surface_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_surface_zstd_ddict) {
		g_auth_input_delta_low_entropy_surface_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_surface_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_surface_alt_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_surface_alt_zstd_ddict) {
		g_auth_input_delta_low_entropy_surface_alt_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ALT_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_ALT_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_surface_alt_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_surface_fallback_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_surface_fallback_zstd_ddict) {
		g_auth_input_delta_low_entropy_surface_fallback_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_surface_fallback_zstd_ddict;
}

ZSTD_DDict* auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict()
{
	if (!g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict) {
		g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ALT_ZSTD_DICT,
			MXT_AUTH_INPUT_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_ALT_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict;
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

ZSTD_DDict* auth_input_hybrid_smooth_zstd_ddict()
{
	if (!g_auth_input_hybrid_smooth_zstd_ddict) {
		g_auth_input_hybrid_smooth_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_HYBRID_SMOOTH_ZSTD_DICT,
			MXT_AUTH_INPUT_HYBRID_SMOOTH_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_hybrid_smooth_zstd_ddict;
}

ZSTD_DDict* auth_input_zero_bitmap_zstd_ddict()
{
	if (!g_auth_input_zero_bitmap_zstd_ddict) {
		g_auth_input_zero_bitmap_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_ZERO_BITMAP_ZSTD_DICT,
			MXT_AUTH_INPUT_ZERO_BITMAP_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_zero_bitmap_zstd_ddict;
}

bool auth_input_mode_uses_zero_bitmap_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD;
}

bool auth_input_mode_uses_hybrid_dict(uint8_t mode)
{
	(void)mode;
	return false;
}

bool auth_input_mode_uses_hybrid_smooth_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD;
}

bool auth_input_mode_uses_delta_pairs_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD;
}

bool auth_input_mode_uses_delta_pairs_surface_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_SURFACE_DICT_ZSTD;
}

bool auth_input_mode_uses_delta_low_entropy_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_DICT_ZSTD;
}

bool auth_input_mode_uses_delta_low_entropy_surface_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT_ZSTD;
}

bool auth_input_mode_uses_delta_low_entropy_surface_fallback_dict(uint8_t mode)
{
	mode &= MXT_NET_AUTH_MODE_MASK;
	return mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD;
}

uint8_t auth_input_low_entropy_layout_sublayout(uint8_t sublayout)
{
	// Rarely selected codes are aliases that keep the old layout code available and add an alt-dictionary candidate.
	if (sublayout == 1) {
		return 13;
	}
	if (sublayout == 10) {
		return 13;
	}
	if (sublayout == 11) {
		return 9;
	}
	if (sublayout == 12) {
		return 3;
	}
	if (sublayout == 13) {
		return 9;
	}
	if (sublayout == 15) {
		return 14;
	}
	return sublayout;
}

bool auth_input_low_entropy_alt_alias_sublayout(uint8_t sublayout)
{
	return sublayout == 1 ||
		sublayout == 11 ||
		sublayout == 12 ||
		sublayout == 15;
}

bool auth_input_low_entropy_fallback_alt_sublayout(uint8_t sublayout)
{
	// Deterministic zero-bit dictionary switch; selected from 100-racer fallback corpus by sublayout.
	if (auth_input_low_entropy_alt_alias_sublayout(sublayout)) {
		return true;
	}
	return sublayout == 3 ||
		sublayout == 5 ||
		sublayout == 6 ||
		sublayout == 8 ||
		sublayout == 9 ||
		sublayout == 11 ||
		sublayout == 12 ||
		sublayout == 13 ||
		sublayout == 14;
}

bool auth_input_low_entropy_default_alt_sublayout(uint8_t sublayout)
{
	return sublayout == 1 ||
		sublayout == 11 ||
		sublayout == 12 ||
		sublayout == 15;
}

bool auth_input_low_entropy_default_s1_sublayout(uint8_t sublayout)
{
	return sublayout == 10;
}

bool auth_input_low_entropy_default_s11_sublayout(uint8_t sublayout)
{
	return sublayout == 13;
}

bool auth_input_low_entropy_surface_alt_sublayout(uint8_t sublayout)
{
	return auth_input_low_entropy_alt_alias_sublayout(sublayout);
}

int auth_input_bitpacked_raw_size(int frame_count, int racer_count)
{
	const int input_count = frame_count * racer_count;
	return (((input_count + 7) >> 3) * 5) + input_count * 4;
}

PackedByteArray encode_zero_bitmap_raw(const PackedByteArray& src)
{
	const int src_size = src.size();
	if (src_size <= 0) {
		return PackedByteArray();
	}
	const int bitmap_bytes = (src_size + 7) >> 3;
	int nonzero_count = 0;
	const uint8_t* src_bytes = src.ptr();
	for (int i = 0; i < src_size; ++i) {
		if (src_bytes[i] != 0) {
			++nonzero_count;
		}
	}
	PackedByteArray out;
	if (out.resize(bitmap_bytes + nonzero_count) != 0) {
		return PackedByteArray();
	}
	uint8_t* out_bytes = out.ptrw();
	std::memset(out_bytes, 0, static_cast<size_t>(bitmap_bytes));
	int out_pos = bitmap_bytes;
	for (int i = 0; i < src_size; ++i) {
		const uint8_t value = src_bytes[i];
		if (value != 0) {
			out_bytes[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
			out_bytes[out_pos++] = value;
		}
	}
	return out;
}

bool decode_zero_bitmap_raw(const PackedByteArray& encoded, PackedByteArray& out, int raw_size)
{
	if (raw_size <= 0 || encoded.size() <= 0) {
		return false;
	}
	const int bitmap_bytes = (raw_size + 7) >> 3;
	if (encoded.size() < bitmap_bytes || out.resize(raw_size) != 0) {
		return false;
	}
	const uint8_t* encoded_bytes = encoded.ptr();
	uint8_t* out_bytes = out.ptrw();
	std::memset(out_bytes, 0, static_cast<size_t>(raw_size));
	int encoded_pos = bitmap_bytes;
	for (int i = 0; i < raw_size; ++i) {
		if ((encoded_bytes[i >> 3] & static_cast<uint8_t>(1u << (i & 7))) != 0) {
			if (encoded_pos >= encoded.size()) {
				return false;
			}
			out_bytes[i] = encoded_bytes[encoded_pos++];
		}
	}
	return encoded_pos == encoded.size();
}

int auth_input_delta_low_entropy_raw_size_bound(int frame_count, int p_racer_count, bool has_sublayout_byte)
{
	if (frame_count != 2 || p_racer_count <= 0) {
		return 0;
	}
	const int sublayout_bytes = has_sublayout_byte ? 1 : 0;
	const int field_bytes = frame_count * p_racer_count;
	const int current_bound = sublayout_bytes + (3 * field_bytes) +
		((p_racer_count + 3) >> 2) + (p_racer_count * 2) +
		((p_racer_count + 1) >> 1) + (p_racer_count * 2);
	const int table_bound = sublayout_bytes + (3 * (((p_racer_count + 1) >> 1) + (p_racer_count * 2))) +
		((p_racer_count + 3) >> 2) + (p_racer_count * 2) +
		((p_racer_count + 1) >> 1) + (p_racer_count * 2);
	return std::max(current_bound, table_bound);
}

constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT = 0;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS = 1;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS = 2;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS = 3;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_ESCAPE_TAIL = 4;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_ESCAPE_TAIL = 5;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_ESCAPE_TAIL = 6;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_TAIL_MASK2 = 7;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK2 = 8;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK2 = 9;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1_S1_DICT = 10;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK3 = 11;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK3 = 12;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1 = 13;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK1 = 14;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1 = 15;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MAX_SUBLAYOUT = MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1;
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[10][2] = {
	{1, 0}, {127, 127}, {0, 0}, {1, 32}, {17, 31},
	{17, 0}, {0, 32}, {1, 17}, {0, 31}, {32, 31},
};
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MASK3_CODES[7] = {0, 3, 4, 5, 6, 7, 8};
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIR_FIELDS[3][15][2] = {
	{
		{1, 0}, {0, 0}, {254, 0}, {1, 1}, {2, 0},
		{3, 0}, {0, 2}, {4, 0}, {5, 0}, {6, 0},
		{7, 0}, {8, 0}, {9, 0}, {1, 2}, {10, 0},
	},
	{
		{0, 0}, {254, 0}, {1, 0}, {2, 0}, {3, 0},
		{0, 2}, {1, 1}, {4, 0}, {5, 0}, {6, 0},
		{7, 0}, {8, 0}, {9, 0}, {2, 1}, {1, 2},
	},
	{
		{0, 0}, {127, 0}, {254, 0}, {135, 0}, {128, 0},
		{126, 0}, {161, 0}, {129, 0}, {93, 0}, {136, 0},
		{137, 0}, {134, 0}, {119, 0}, {162, 0}, {161, 2},
	},
};
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIR_FIELDS[3][15][2] = {
	{
		{0, 0}, {254, 0}, {1, 1}, {0, 2}, {1, 0},
		{6, 0}, {1, 2}, {2, 0}, {2, 3}, {3, 0},
		{4, 0}, {5, 0}, {0, 4}, {8, 0}, {2, 1},
	},
	{
		{0, 0}, {254, 0}, {1, 1}, {3, 0}, {2, 0},
		{1, 0}, {0, 2}, {4, 0}, {6, 0}, {2, 1},
		{7, 0}, {5, 0}, {2, 3}, {4, 1}, {8, 0},
	},
	{
		{135, 0}, {161, 0}, {127, 0}, {93, 0}, {254, 0},
		{0, 0}, {162, 0}, {161, 2}, {162, 1}, {160, 2},
		{119, 0}, {161, 1}, {152, 0}, {93, 1}, {92, 2},
	},
};
constexpr uint8_t MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIR_FIELDS[3][15][2] = {
	{
		{0, 0}, {254, 0}, {1, 1}, {1, 0}, {2, 0},
		{3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0},
		{8, 0}, {0, 2}, {9, 0}, {10, 0}, {11, 0},
	},
	{
		{0, 0}, {254, 0}, {1, 0}, {2, 0}, {3, 0},
		{4, 0}, {5, 0}, {6, 0}, {0, 2}, {7, 0},
		{9, 0}, {8, 0}, {1, 1}, {11, 0}, {1, 2},
	},
	{
		{127, 0}, {0, 0}, {128, 0}, {126, 0}, {129, 0},
		{254, 0}, {136, 0}, {135, 0}, {137, 0}, {134, 0},
		{125, 0}, {123, 0}, {133, 0}, {131, 0}, {138, 0},
	},
};

const uint8_t (*auth_input_low_entropy_table_for_sublayout(uint8_t sublayout))[15][2]
{
	sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_ESCAPE_TAIL) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_ESCAPE_TAIL) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_ESCAPE_TAIL) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_TAIL_MASK2) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK2) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK2) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK3) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK3) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS;
	} else if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK1) {
		sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS;
	}
	if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS) {
		return MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIR_FIELDS;
	}
	if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS) {
		return MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIR_FIELDS;
	}
	if (sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS) {
		return MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIR_FIELDS;
	}
	return nullptr;
}

bool auth_input_low_entropy_sublayout_escape_tail(uint8_t sublayout)
{
	sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	return sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_ESCAPE_TAIL ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_ESCAPE_TAIL ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_ESCAPE_TAIL ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_TAIL_MASK2 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK2 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK2 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK3 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK3 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK1;
}

bool auth_input_low_entropy_sublayout_mask2(uint8_t sublayout)
{
	sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	return sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_TABLE_PAIRS_TAIL_MASK2 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK2 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK2;
}

bool auth_input_low_entropy_sublayout_mask3(uint8_t sublayout)
{
	sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	return sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK3 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK3;
}

bool auth_input_low_entropy_sublayout_mask1(uint8_t sublayout)
{
	sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	return sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK1 ||
		sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1;
}

int auth_input_low_entropy_mask3_code(uint8_t table_code)
{
	for (int i = 0; i < 7; ++i) {
		if (MXT_AUTH_INPUT_LOW_ENTROPY_MASK3_CODES[i] == table_code) {
			return i;
		}
	}
	return -1;
}

void auth_input_write_packed3(uint8_t* dst, int index, uint8_t value)
{
	const int bit = index * 3;
	const int byte = bit >> 3;
	const int shift = bit & 7;
	dst[byte] |= static_cast<uint8_t>((value & 0x07) << shift);
	if (shift > 5) {
		dst[byte + 1] |= static_cast<uint8_t>((value & 0x07) >> (8 - shift));
	}
}

uint8_t auth_input_read_packed3(const uint8_t* src, int index)
{
	const int bit = index * 3;
	const int byte = bit >> 3;
	const int shift = bit & 7;
	uint16_t bits = src[byte];
	if (shift > 5) {
		bits |= static_cast<uint16_t>(src[byte + 1]) << 8;
	}
	return static_cast<uint8_t>((bits >> shift) & 0x07);
}

PackedByteArray encode_delta_low_entropy_raw(const PackedByteArray& delta_pairs_raw, int frame_count, int p_racer_count, uint8_t sublayout, bool write_sublayout)
{
	if (frame_count != 2 || p_racer_count <= 0 || delta_pairs_raw.size() != frame_count * p_racer_count * 5) {
		return PackedByteArray();
	}
	const uint8_t (*pair_table)[15][2] = auth_input_low_entropy_table_for_sublayout(sublayout);
	const uint8_t layout_sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	const bool implicit_tail = false;
	const bool implicit_mask =
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1 ||
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK1 ||
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1;
	const bool implicit_sv = implicit_tail || pair_table != nullptr;
	if (layout_sublayout != MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT &&
		layout_sublayout != MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1 &&
		!pair_table) {
		return PackedByteArray();
	}
	const bool escape_tail = auth_input_low_entropy_sublayout_escape_tail(sublayout);
	const bool mask1 = auth_input_low_entropy_sublayout_mask1(sublayout);
	const bool mask2 = auth_input_low_entropy_sublayout_mask2(sublayout);
	const bool mask3 = auth_input_low_entropy_sublayout_mask3(sublayout);
	const int field_bytes = frame_count * p_racer_count;
	const int sv_class_bytes = (p_racer_count + 3) >> 2;
	const int mask_code_bytes = (p_racer_count + 1) >> 1;
	const int mask1_bytes = (p_racer_count + 7) >> 3;
	const int mask3_bytes = ((p_racer_count * 3) + 7) >> 3;
	const int sv_out_bytes = implicit_sv ? 0 : sv_class_bytes;
	const int mask_class_bytes = implicit_mask ? 0 : (mask1 ? mask1_bytes : (mask2 ? ((p_racer_count + 3) >> 2) : (mask3 ? mask3_bytes : mask_code_bytes)));
	const uint8_t* src = delta_pairs_raw.ptr();
	int escape_count = 0;
	if (pair_table) {
		for (int field = 0; field < 3; ++field) {
			const int base = field * field_bytes;
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t a = src[base + (i * 2)];
				const uint8_t b = src[base + (i * 2) + 1];
				bool found = false;
				for (int p = 0; p < 15; ++p) {
					if (pair_table[field][p][0] == a && pair_table[field][p][1] == b) {
						found = true;
						break;
					}
				}
				if (!found) {
					++escape_count;
				}
			}
		}
	}
	const int sv_base = 3 * field_bytes;
	for (int i = 0; i < p_racer_count; ++i) {
		const uint8_t a = src[sv_base + (i * 2)];
		const uint8_t b = src[sv_base + (i * 2) + 1];
		if (implicit_sv) {
			if (a != 127 || b != 0) {
				return PackedByteArray();
			}
			continue;
		}
		if (!((a == 127 && b == 0) || (a == 0 && b == 0) || (a == 127 && b == 127))) {
			++escape_count;
		}
	}
	const int mask_base = 4 * field_bytes;
	for (int i = 0; i < p_racer_count; ++i) {
		const uint8_t a = src[mask_base + (i * 2)];
		const uint8_t b = src[mask_base + (i * 2) + 1];
		if (implicit_mask) {
			if (a != MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][0] ||
				b != MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][1]) {
				return PackedByteArray();
			}
			continue;
		}
		bool found = false;
		int found_code = -1;
		for (int p = 0; p < 10; ++p) {
			if (MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][0] == a && MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][1] == b) {
				found = true;
				found_code = p;
				break;
			}
		}
		if (!found || (mask1 && found_code != 0) || (mask2 && found_code != 0 && found_code != 3 && found_code != 4)) {
			++escape_count;
		} else if (mask3 && auth_input_low_entropy_mask3_code(static_cast<uint8_t>(found_code)) < 0) {
			++escape_count;
		}
	}
	PackedByteArray out;
	const int table_code_bytes = (p_racer_count + 1) >> 1;
	const int first_fields_size = pair_table ?
		(3 * table_code_bytes) :
		(3 * field_bytes);
	const int out_size = (write_sublayout ? 1 : 0) + first_fields_size + sv_out_bytes + mask_class_bytes + (escape_count * 2);
	if (out.resize(out_size) != 0) {
		return PackedByteArray();
	}
	uint8_t* dst = out.ptrw();
	int pos = 0;
	if (write_sublayout) {
		dst[pos++] = sublayout;
	}
	if (pair_table && escape_tail) {
		int field_code_pos[3] = {};
		for (int field = 0; field < 3; ++field) {
			field_code_pos[field] = pos;
			std::memset(dst + pos, 0, static_cast<size_t>(table_code_bytes));
			pos += table_code_bytes;
		}
		int sv_class_pos = 0;
		if (!implicit_sv) {
			sv_class_pos = pos;
			std::memset(dst + pos, 0, static_cast<size_t>(sv_class_bytes));
			pos += sv_class_bytes;
		}
		const int mask_code_pos = pos;
		std::memset(dst + pos, 0, static_cast<size_t>(mask_class_bytes));
		pos += mask_class_bytes;
		for (int field = 0; field < 3; ++field) {
			const int base = field * field_bytes;
			const int code_pos = field_code_pos[field];
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t a = src[base + (i * 2)];
				const uint8_t b = src[base + (i * 2) + 1];
				uint8_t code = 15;
				for (int p = 0; p < 15; ++p) {
					if (pair_table[field][p][0] == a && pair_table[field][p][1] == b) {
						code = static_cast<uint8_t>(p);
						break;
					}
				}
				dst[code_pos + (i >> 1)] |= static_cast<uint8_t>(code << ((i & 1) * 4));
				if (code == 15) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			}
		}
		if (!implicit_sv) {
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t a = src[sv_base + (i * 2)];
				const uint8_t b = src[sv_base + (i * 2) + 1];
				uint8_t code = 2;
				if (a == 127 && b == 0) {
					code = 0;
				} else if (a == 0 && b == 0) {
					code = 1;
				} else if (a == 127 && b == 127) {
					code = 2;
				} else {
					code = 3;
				}
				dst[sv_class_pos + (i >> 2)] |= static_cast<uint8_t>(code << ((i & 3) * 2));
				if (code == 3) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			}
		}
		if (implicit_mask) {
			if (pos != out_size) {
				return PackedByteArray();
			}
			return out;
		}
		for (int i = 0; i < p_racer_count; ++i) {
			const uint8_t a = src[mask_base + (i * 2)];
			const uint8_t b = src[mask_base + (i * 2) + 1];
			uint8_t code = 15;
			for (int p = 0; p < 10; ++p) {
				if (MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][0] == a && MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][1] == b) {
					code = static_cast<uint8_t>(p);
					break;
				}
			}
			if (mask1) {
				if (code == 0) {
					dst[mask_code_pos + (i >> 3)] &= static_cast<uint8_t>(~(1u << (i & 7)));
				} else {
					dst[mask_code_pos + (i >> 3)] |= static_cast<uint8_t>(1u << (i & 7));
					dst[pos++] = a;
					dst[pos++] = b;
				}
			} else if (mask2) {
				uint8_t compact_code = 3;
				if (code == 0) {
					compact_code = 0;
				} else if (code == 3) {
					compact_code = 1;
				} else if (code == 4) {
					compact_code = 2;
				}
				dst[mask_code_pos + (i >> 2)] |= static_cast<uint8_t>(compact_code << ((i & 3) * 2));
				if (compact_code == 3) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			} else if (mask3) {
				const int compact_code = auth_input_low_entropy_mask3_code(code);
				if (compact_code >= 0) {
					auth_input_write_packed3(dst + mask_code_pos, i, static_cast<uint8_t>(compact_code));
				} else {
					auth_input_write_packed3(dst + mask_code_pos, i, 7);
					dst[pos++] = a;
					dst[pos++] = b;
				}
			} else {
				dst[mask_code_pos + (i >> 1)] |= static_cast<uint8_t>(code << ((i & 1) * 4));
				if (code == 15) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			}
		}
	} else if (pair_table) {
		for (int field = 0; field < 3; ++field) {
			const int base = field * field_bytes;
			std::memset(dst + pos, 0, static_cast<size_t>(table_code_bytes));
			const int code_pos = pos;
			pos += table_code_bytes;
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t a = src[base + (i * 2)];
				const uint8_t b = src[base + (i * 2) + 1];
				uint8_t code = 15;
				for (int p = 0; p < 15; ++p) {
					if (pair_table[field][p][0] == a && pair_table[field][p][1] == b) {
						code = static_cast<uint8_t>(p);
						break;
					}
				}
				dst[code_pos + (i >> 1)] |= static_cast<uint8_t>(code << ((i & 1) * 4));
				if (code == 15) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			}
		}
	} else {
		std::memcpy(dst + pos, src, static_cast<size_t>(3 * field_bytes));
		pos += 3 * field_bytes;
	}
	if (implicit_tail) {
		if (pos != out_size) {
			return PackedByteArray();
		}
		return out;
	}
	if (!escape_tail) {
		if (!implicit_sv) {
			std::memset(dst + pos, 0, static_cast<size_t>(sv_class_bytes));
			const int sv_class_pos = pos;
			pos += sv_class_bytes;
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t a = src[sv_base + (i * 2)];
				const uint8_t b = src[sv_base + (i * 2) + 1];
				uint8_t code = 2;
				if (a == 127 && b == 0) {
					code = 0;
				} else if (a == 0 && b == 0) {
					code = 1;
				} else if (a == 127 && b == 127) {
					code = 2;
				} else {
					code = 3;
				}
				dst[sv_class_pos + (i >> 2)] |= static_cast<uint8_t>(code << ((i & 3) * 2));
				if (code == 3) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			}
		}
		if (implicit_mask) {
			if (pos != out_size) {
				return PackedByteArray();
			}
			return out;
		}
		std::memset(dst + pos, 0, static_cast<size_t>(mask_class_bytes));
		const int mask_code_pos = pos;
		pos += mask_class_bytes;
		for (int i = 0; i < p_racer_count; ++i) {
			const uint8_t a = src[mask_base + (i * 2)];
			const uint8_t b = src[mask_base + (i * 2) + 1];
			uint8_t code = 15;
			for (int p = 0; p < 10; ++p) {
				if (MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][0] == a && MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][1] == b) {
					code = static_cast<uint8_t>(p);
					break;
				}
			}
			if (mask1) {
				if (code == 0) {
					dst[mask_code_pos + (i >> 3)] &= static_cast<uint8_t>(~(1u << (i & 7)));
				} else {
					dst[mask_code_pos + (i >> 3)] |= static_cast<uint8_t>(1u << (i & 7));
					dst[pos++] = a;
					dst[pos++] = b;
				}
			} else {
				dst[mask_code_pos + (i >> 1)] |= static_cast<uint8_t>(code << ((i & 1) * 4));
				if (code == 15) {
					dst[pos++] = a;
					dst[pos++] = b;
				}
			}
		}
	}
	if (pos != out_size) {
		return PackedByteArray();
	}
	return out;
}

bool decode_delta_low_entropy_raw(const PackedByteArray& encoded, PackedByteArray& out, int frame_count, int p_racer_count, int external_sublayout)
{
	if (frame_count != 2 || p_racer_count <= 0) {
		return false;
	}
	const int field_bytes = frame_count * p_racer_count;
	const int sv_class_bytes = (p_racer_count + 3) >> 2;
	const int mask_code_bytes = (p_racer_count + 1) >> 1;
	const bool read_sublayout = external_sublayout < 0;
	if (encoded.size() < (read_sublayout ? 1 : 0) || out.resize(5 * field_bytes) != 0) {
		return false;
	}
	const uint8_t* src = encoded.ptr();
	uint8_t* dst = out.ptrw();
	int pos = 0;
	const uint8_t sublayout = read_sublayout ? src[pos++] : static_cast<uint8_t>(external_sublayout);
	const uint8_t (*pair_table)[15][2] = auth_input_low_entropy_table_for_sublayout(sublayout);
	const uint8_t layout_sublayout = auth_input_low_entropy_layout_sublayout(sublayout);
	const bool implicit_tail = false;
	const bool implicit_mask =
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_SURFACE_TABLE_PAIRS_TAIL_MASK1 ||
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_MULTIPLEX_TABLE_PAIRS_TAIL_MASK1 ||
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1;
	const bool implicit_sv = implicit_tail || pair_table != nullptr;
	const bool escape_tail = auth_input_low_entropy_sublayout_escape_tail(sublayout);
	const bool mask1 = auth_input_low_entropy_sublayout_mask1(sublayout);
	const bool mask2 = auth_input_low_entropy_sublayout_mask2(sublayout);
	const bool mask3 = auth_input_low_entropy_sublayout_mask3(sublayout);
	const int mask1_bytes = (p_racer_count + 7) >> 3;
	const int mask3_bytes = ((p_racer_count * 3) + 7) >> 3;
	const int sv_min_bytes = implicit_sv ? 0 : sv_class_bytes;
	const int mask_class_bytes = implicit_mask ? 0 : (mask1 ? mask1_bytes : (mask2 ? ((p_racer_count + 3) >> 2) : (mask3 ? mask3_bytes : mask_code_bytes)));
	if (encoded.size() < pos + sv_min_bytes + mask_class_bytes) {
		return false;
	}
	int tail_field_code_pos[3] = {};
	int tail_sv_class_pos = 0;
	int tail_mask_code_pos = 0;
	if (pair_table && escape_tail) {
		const int table_code_bytes = (p_racer_count + 1) >> 1;
		for (int field = 0; field < 3; ++field) {
			tail_field_code_pos[field] = pos;
			pos += table_code_bytes;
		}
		if (!implicit_sv) {
			tail_sv_class_pos = pos;
			pos += sv_class_bytes;
		}
		tail_mask_code_pos = pos;
		pos += mask_class_bytes;
		if (pos > encoded.size()) {
			return false;
		}
		for (int field = 0; field < 3; ++field) {
			const int base = field * field_bytes;
			const int code_pos = tail_field_code_pos[field];
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t code = static_cast<uint8_t>((src[code_pos + (i >> 1)] >> ((i & 1) * 4)) & 0x0f);
				uint8_t a = 0;
				uint8_t b = 0;
				if (code < 15) {
					a = pair_table[field][code][0];
					b = pair_table[field][code][1];
				} else {
					if (pos + 2 > encoded.size()) {
						return false;
					}
					a = src[pos++];
					b = src[pos++];
				}
				dst[base + (i * 2)] = a;
				dst[base + (i * 2) + 1] = b;
			}
		}
	} else if (pair_table) {
		const int table_code_bytes = (p_racer_count + 1) >> 1;
		for (int field = 0; field < 3; ++field) {
			const int base = field * field_bytes;
			const int code_pos = pos;
			pos += table_code_bytes;
			if (pos > encoded.size()) {
				return false;
			}
			for (int i = 0; i < p_racer_count; ++i) {
				const uint8_t code = static_cast<uint8_t>((src[code_pos + (i >> 1)] >> ((i & 1) * 4)) & 0x0f);
				uint8_t a = 0;
				uint8_t b = 0;
				if (code < 15) {
					a = pair_table[field][code][0];
					b = pair_table[field][code][1];
				} else {
					if (pos + 2 > encoded.size()) {
						return false;
					}
					a = src[pos++];
					b = src[pos++];
				}
				dst[base + (i * 2)] = a;
				dst[base + (i * 2) + 1] = b;
			}
		}
	} else if (layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT ||
		layout_sublayout == MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT_TAIL_MASK1) {
		if (pos + (3 * field_bytes) > encoded.size()) {
			return false;
		}
		std::memcpy(dst, src + pos, static_cast<size_t>(3 * field_bytes));
		pos += 3 * field_bytes;
	} else {
		return false;
	}
	if (implicit_tail) {
		if (pos != encoded.size()) {
			return false;
		}
		const int sv_base = 3 * field_bytes;
		const int mask_base = 4 * field_bytes;
		for (int i = 0; i < p_racer_count; ++i) {
			dst[sv_base + (i * 2)] = 127;
			dst[sv_base + (i * 2) + 1] = 0;
			dst[mask_base + (i * 2)] = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][0];
			dst[mask_base + (i * 2) + 1] = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][1];
		}
		return true;
	}
	const int sv_base = 3 * field_bytes;
	if (implicit_sv) {
		for (int i = 0; i < p_racer_count; ++i) {
			dst[sv_base + (i * 2)] = 127;
			dst[sv_base + (i * 2) + 1] = 0;
		}
	} else {
		const int sv_class_pos = escape_tail ? tail_sv_class_pos : pos;
		if (!escape_tail) {
			pos += sv_class_bytes;
			if (pos > encoded.size()) {
				return false;
			}
		}
		for (int i = 0; i < p_racer_count; ++i) {
			const uint8_t code = static_cast<uint8_t>((src[sv_class_pos + (i >> 2)] >> ((i & 3) * 2)) & 0x03);
			uint8_t a = 127;
			uint8_t b = 0;
			if (code == 1) {
				a = 0;
				b = 0;
			} else if (code == 2) {
				a = 127;
				b = 127;
			} else if (code == 3) {
				if (pos + 2 > encoded.size()) {
					return false;
				}
				a = src[pos++];
				b = src[pos++];
			}
			dst[sv_base + (i * 2)] = a;
			dst[sv_base + (i * 2) + 1] = b;
		}
	}
	if (implicit_mask) {
		if (pos != encoded.size()) {
			return false;
		}
		const int mask_base = 4 * field_bytes;
		for (int i = 0; i < p_racer_count; ++i) {
			dst[mask_base + (i * 2)] = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][0];
			dst[mask_base + (i * 2) + 1] = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][1];
		}
		return true;
	}
	const int mask_code_pos = escape_tail ? tail_mask_code_pos : pos;
	if (!escape_tail) {
		pos += mask_class_bytes;
		if (pos > encoded.size()) {
			return false;
		}
	}
	const int mask_base = 4 * field_bytes;
	for (int i = 0; i < p_racer_count; ++i) {
		const uint8_t code = mask1 ?
			static_cast<uint8_t>((src[mask_code_pos + (i >> 3)] >> (i & 7)) & 0x01) : (mask3 ?
			auth_input_read_packed3(src + mask_code_pos, i) : (mask2 ?
			static_cast<uint8_t>((src[mask_code_pos + (i >> 2)] >> ((i & 3) * 2)) & 0x03) :
			static_cast<uint8_t>((src[mask_code_pos + (i >> 1)] >> ((i & 1) * 4)) & 0x0f)));
		uint8_t a = 0;
		uint8_t b = 0;
		if (mask1 && code == 0) {
			a = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][0];
			b = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][1];
		} else if (mask3 && code < 7) {
			const uint8_t table_code = MXT_AUTH_INPUT_LOW_ENTROPY_MASK3_CODES[code];
			a = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[table_code][0];
			b = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[table_code][1];
		} else if (mask2 && code == 0) {
			a = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][0];
			b = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[0][1];
		} else if (mask2 && code == 1) {
			a = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[3][0];
			b = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[3][1];
		} else if (mask2 && code == 2) {
			a = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[4][0];
			b = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[4][1];
		} else if (!mask1 && !mask2 && !mask3 && code < 10) {
			a = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[code][0];
			b = MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[code][1];
		} else if ((mask1 && code == 1) || (mask3 && code == 7) || (mask2 && code == 3) || (!mask2 && !mask3 && code == 15)) {
			if (pos + 2 > encoded.size()) {
				return false;
			}
			a = src[pos++];
			b = src[pos++];
		} else {
			return false;
		}
		dst[mask_base + (i * 2)] = a;
		dst[mask_base + (i * 2) + 1] = b;
	}
	return pos == encoded.size();
}

PackedByteArray compress_auth_input_with_dict(const PackedByteArray& raw, bool hybrid_dict = false, bool zero_bitmap_dict = false, bool hybrid_smooth_dict = false, bool delta_pairs_dict = false, bool delta_low_entropy_dict = false, bool delta_pairs_surface_dict = false, bool delta_low_entropy_surface_dict = false, bool delta_low_entropy_surface_fallback_dict = false, bool delta_low_entropy_surface_fallback_alt_dict = false, bool delta_low_entropy_alt_dict = false, bool delta_low_entropy_surface_alt_dict = false, bool delta_low_entropy_s1_dict = false, bool delta_low_entropy_s11_dict = false)
{
	const int raw_size = raw.size();
	if (raw_size <= 0) {
		return PackedByteArray();
	}
	ZSTD_CCtx* cctx = auth_input_zstd_cctx();
	ZSTD_CDict* cdict = nullptr;
	if (delta_low_entropy_s11_dict) {
		cdict = auth_input_delta_low_entropy_s11_zstd_cdict();
	} else if (delta_low_entropy_s1_dict) {
		cdict = auth_input_delta_low_entropy_s1_zstd_cdict();
	} else if (delta_low_entropy_surface_alt_dict) {
		cdict = auth_input_delta_low_entropy_surface_alt_zstd_cdict();
	} else if (delta_low_entropy_alt_dict) {
		cdict = auth_input_delta_low_entropy_alt_zstd_cdict();
	} else if (delta_low_entropy_surface_fallback_alt_dict) {
		cdict = auth_input_delta_low_entropy_surface_fallback_alt_zstd_cdict();
	} else if (delta_low_entropy_surface_fallback_dict) {
		cdict = auth_input_delta_low_entropy_surface_fallback_zstd_cdict();
	} else if (delta_low_entropy_surface_dict) {
		cdict = auth_input_delta_low_entropy_surface_zstd_cdict();
	} else if (delta_pairs_surface_dict) {
		cdict = auth_input_delta_pairs_surface_zstd_cdict();
	} else if (delta_low_entropy_dict) {
		cdict = auth_input_delta_low_entropy_zstd_cdict();
	} else if (delta_pairs_dict) {
		cdict = auth_input_delta_pairs_zstd_cdict();
	} else if (hybrid_smooth_dict) {
		cdict = auth_input_hybrid_smooth_zstd_cdict();
	} else if (hybrid_dict) {
		cdict = auth_input_hybrid_zstd_cdict();
	} else if (zero_bitmap_dict) {
		cdict = auth_input_zero_bitmap_zstd_cdict();
	} else {
		cdict = auth_input_zstd_cdict();
	}
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
	size_t zstd_result = ZSTD_compressBegin_usingCDict(cctx, cdict);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	const size_t compressed_size = ZSTD_compressBlock(
		cctx,
		out.ptrw(),
		bound,
		raw.ptr(),
		static_cast<size_t>(raw_size)
	);
	if (ZSTD_isError(compressed_size) || compressed_size == 0 || compressed_size > bound || compressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(compressed_size));
	return out;
}

PackedByteArray compress_auth_input_plain(const PackedByteArray& raw)
{
	const int raw_size = raw.size();
	if (raw_size <= 0) {
		return PackedByteArray();
	}
	ZSTD_CCtx* cctx = auth_input_zstd_cctx();
	if (!cctx) {
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
	ZSTD_CCtx_reset(cctx, ZSTD_reset_session_and_parameters);
	size_t zstd_result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, MXT_NET_AUTH_ZSTD_LEVEL);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	zstd_result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 0);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	zstd_result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	zstd_result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_dictIDFlag, 0);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	const size_t compressed_size = ZSTD_compress2(
		cctx,
		out.ptrw(),
		bound,
		raw.ptr(),
		static_cast<size_t>(raw_size)
	);
	if (ZSTD_isError(compressed_size) || compressed_size > bound || compressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(compressed_size));
	return out;
}

PackedByteArray decompress_auth_input_with_dict(const PackedByteArray& compressed, int raw_size, bool hybrid_dict = false, bool zero_bitmap_dict = false, bool hybrid_smooth_dict = false, bool delta_pairs_dict = false, bool delta_low_entropy_dict = false, bool delta_pairs_surface_dict = false, bool delta_low_entropy_surface_dict = false, bool delta_low_entropy_surface_fallback_dict = false, bool delta_low_entropy_surface_fallback_alt_dict = false, bool delta_low_entropy_alt_dict = false, bool delta_low_entropy_surface_alt_dict = false, bool delta_low_entropy_s1_dict = false, bool delta_low_entropy_s11_dict = false)
{
	if (raw_size <= 0 || compressed.size() <= 0) {
		return PackedByteArray();
	}
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	ZSTD_DDict* ddict = nullptr;
	if (delta_low_entropy_s11_dict) {
		ddict = auth_input_delta_low_entropy_s11_zstd_ddict();
	} else if (delta_low_entropy_s1_dict) {
		ddict = auth_input_delta_low_entropy_s1_zstd_ddict();
	} else if (delta_low_entropy_surface_alt_dict) {
		ddict = auth_input_delta_low_entropy_surface_alt_zstd_ddict();
	} else if (delta_low_entropy_alt_dict) {
		ddict = auth_input_delta_low_entropy_alt_zstd_ddict();
	} else if (delta_low_entropy_surface_fallback_alt_dict) {
		ddict = auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict();
	} else if (delta_low_entropy_surface_fallback_dict) {
		ddict = auth_input_delta_low_entropy_surface_fallback_zstd_ddict();
	} else if (delta_low_entropy_surface_dict) {
		ddict = auth_input_delta_low_entropy_surface_zstd_ddict();
	} else if (delta_pairs_surface_dict) {
		ddict = auth_input_delta_pairs_surface_zstd_ddict();
	} else if (delta_low_entropy_dict) {
		ddict = auth_input_delta_low_entropy_zstd_ddict();
	} else if (delta_pairs_dict) {
		ddict = auth_input_delta_pairs_zstd_ddict();
	} else if (hybrid_smooth_dict) {
		ddict = auth_input_hybrid_smooth_zstd_ddict();
	} else if (hybrid_dict) {
		ddict = auth_input_hybrid_zstd_ddict();
	} else if (zero_bitmap_dict) {
		ddict = auth_input_zero_bitmap_zstd_ddict();
	} else {
		ddict = auth_input_zstd_ddict();
	}
	if (!dctx || !ddict) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size) != 0) {
		return PackedByteArray();
	}
	size_t zstd_result = ZSTD_decompressBegin_usingDDict(dctx, ddict);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompressBlock(
		dctx,
		out.ptrw(),
		static_cast<size_t>(raw_size),
		compressed.ptr(),
		static_cast<size_t>(compressed.size())
	);
	if (ZSTD_isError(decompressed_size) || decompressed_size != static_cast<size_t>(raw_size)) {
		return PackedByteArray();
	}
	return out;
}

PackedByteArray decompress_auth_input_plain_bound(const PackedByteArray& compressed, int raw_size_bound)
{
	if (raw_size_bound <= 0 || compressed.size() <= 0) {
		return PackedByteArray();
	}
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	if (!dctx) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size_bound) != 0) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompressDCtx(
		dctx,
		out.ptrw(),
		static_cast<size_t>(raw_size_bound),
		compressed.ptr(),
		static_cast<size_t>(compressed.size())
	);
	if (ZSTD_isError(decompressed_size) || decompressed_size > static_cast<size_t>(raw_size_bound) || decompressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(decompressed_size));
	return out;
}

PackedByteArray decompress_auth_input_with_dict_bound(const PackedByteArray& compressed, int raw_size_bound, bool hybrid_dict = false, bool zero_bitmap_dict = false, bool hybrid_smooth_dict = false, bool delta_pairs_dict = false, bool delta_low_entropy_dict = false, bool delta_pairs_surface_dict = false, bool delta_low_entropy_surface_dict = false, bool delta_low_entropy_surface_fallback_dict = false, bool delta_low_entropy_surface_fallback_alt_dict = false, bool delta_low_entropy_alt_dict = false, bool delta_low_entropy_surface_alt_dict = false, bool delta_low_entropy_s1_dict = false, bool delta_low_entropy_s11_dict = false)
{
	if (raw_size_bound <= 0 || compressed.size() <= 0) {
		return PackedByteArray();
	}
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	ZSTD_DDict* ddict = nullptr;
	if (delta_low_entropy_s11_dict) {
		ddict = auth_input_delta_low_entropy_s11_zstd_ddict();
	} else if (delta_low_entropy_s1_dict) {
		ddict = auth_input_delta_low_entropy_s1_zstd_ddict();
	} else if (delta_low_entropy_surface_alt_dict) {
		ddict = auth_input_delta_low_entropy_surface_alt_zstd_ddict();
	} else if (delta_low_entropy_alt_dict) {
		ddict = auth_input_delta_low_entropy_alt_zstd_ddict();
	} else if (delta_low_entropy_surface_fallback_alt_dict) {
		ddict = auth_input_delta_low_entropy_surface_fallback_alt_zstd_ddict();
	} else if (delta_low_entropy_surface_fallback_dict) {
		ddict = auth_input_delta_low_entropy_surface_fallback_zstd_ddict();
	} else if (delta_low_entropy_surface_dict) {
		ddict = auth_input_delta_low_entropy_surface_zstd_ddict();
	} else if (delta_pairs_surface_dict) {
		ddict = auth_input_delta_pairs_surface_zstd_ddict();
	} else if (delta_low_entropy_dict) {
		ddict = auth_input_delta_low_entropy_zstd_ddict();
	} else if (delta_pairs_dict) {
		ddict = auth_input_delta_pairs_zstd_ddict();
	} else if (hybrid_smooth_dict) {
		ddict = auth_input_hybrid_smooth_zstd_ddict();
	} else if (hybrid_dict) {
		ddict = auth_input_hybrid_zstd_ddict();
	} else if (zero_bitmap_dict) {
		ddict = auth_input_zero_bitmap_zstd_ddict();
	} else {
		ddict = auth_input_zstd_ddict();
	}
	if (!dctx || !ddict) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size_bound) != 0) {
		return PackedByteArray();
	}
	size_t zstd_result = ZSTD_decompressBegin_usingDDict(dctx, ddict);
	if (ZSTD_isError(zstd_result)) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompressBlock(
		dctx,
		out.ptrw(),
		static_cast<size_t>(raw_size_bound),
		compressed.ptr(),
		static_cast<size_t>(compressed.size())
	);
	if (ZSTD_isError(decompressed_size) || decompressed_size > static_cast<size_t>(raw_size_bound) || decompressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(decompressed_size));
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

void dump_auth_input_sample(const PackedByteArray& raw, int first_tick, int count, int racer_count, int mode, int sublayout)
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
		"_p" + String::num_int64(racer_count) +
		"_m" + String::num_int64(mode) +
		"_s" + String::num_int64(sublayout) + ".bin";
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
	const bool delta_pairs_layout = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS;
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
	if (delta_pairs_layout) {
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < frame_count; ++f) {
				const InputFrame* frame = frames[f];
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_trigger_byte(input.strafe_left);
				const uint8_t encoded = f > 0 ? zigzag_i8(wrapped_i8_delta(value, previous)) : value;
				previous = value;
				raw_bytes[raw_pos++] = encoded;
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < frame_count; ++f) {
				const InputFrame* frame = frames[f];
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_trigger_byte(input.strafe_right);
				const uint8_t encoded = f > 0 ? zigzag_i8(wrapped_i8_delta(value, previous)) : value;
				previous = value;
				raw_bytes[raw_pos++] = encoded;
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < frame_count; ++f) {
				const InputFrame* frame = frames[f];
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_axis_byte(input.steer_horizontal);
				const uint8_t encoded = f > 0 ? zigzag_i8(wrapped_i8_delta(value, previous)) : value;
				previous = value;
				raw_bytes[raw_pos++] = encoded;
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < frame_count; ++f) {
				const InputFrame* frame = frames[f];
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_axis_byte(input.steer_vertical);
				const uint8_t encoded = f > 0 ? zigzag_i8(wrapped_i8_delta(value, previous)) : value;
				previous = value;
				raw_bytes[raw_pos++] = encoded;
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < frame_count; ++f) {
				const InputFrame* frame = frames[f];
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_button_mask(input);
				const uint8_t encoded = f > 0 ? zigzag_i8(wrapped_i8_delta(value, previous)) : value;
				previous = value;
				raw_bytes[raw_pos++] = encoded;
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
	ClassDB::bind_method(D_METHOD("store_authoritative_input_packet", "packet", "expected_race_phase", "authoritative_last_tick", "external_mode_count_phase"), &NetcodeSession::store_authoritative_input_packet, DEFVAL(0), DEFVAL(-1), DEFVAL(-1));
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
		writer.write_u8(pack_auth_mode_count_phase(MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD, 0, race_phase));
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
	const int mode_count_pos = writer.pos;
	if (!writer.write_u8(pack_auth_mode_count_phase(MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD, count, race_phase))) {
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
	const int delta_pairs_raw_size = auth_input_raw_size(count, racer_count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS);
	const InputFrame* frames[MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET] = {};
	for (int f = 0; f < count; ++f) {
		frames[f] = find_frame(authoritative_history, first_tick + f);
		if (!frames[f]) {
			return PackedByteArray();
		}
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
	PackedByteArray bitpacked_zero_raw = encode_zero_bitmap_raw(bitpacked_raw);
	if (bitpacked_zero_raw.size() <= 0) {
		return PackedByteArray();
	}
	PackedByteArray hybrid_raw;
	if (hybrid_raw.resize(hybrid_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(hybrid_raw, frames, count, AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA)) {
		return PackedByteArray();
	}
	PackedByteArray delta_pairs_raw;
	if (delta_pairs_raw.resize(delta_pairs_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(delta_pairs_raw, frames, count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS)) {
		return PackedByteArray();
	}
	PackedByteArray compressed = compress_auth_input_with_dict(delta_pairs_raw, false, false, false, true);
	uint8_t compression_mode = MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD;
	AuthInputLayout selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS;
	int selected_raw_size = delta_pairs_raw_size;
	PackedByteArray candidate = compress_auth_input_with_dict(delta_pairs_raw, false, false, false, false, false, true);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_SURFACE_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS;
		selected_raw_size = delta_pairs_raw_size;
	}
	PackedByteArray delta_low_entropy_raw;
	uint8_t selected_low_entropy_sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT;
	for (uint8_t sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT; sublayout <= MXT_AUTH_INPUT_LOW_ENTROPY_MAX_SUBLAYOUT; ++sublayout) {
		PackedByteArray low_entropy_candidate_raw = encode_delta_low_entropy_raw(delta_pairs_raw, count, racer_count, sublayout, false);
		if (low_entropy_candidate_raw.size() > 0) {
			const bool default_alt = auth_input_low_entropy_default_alt_sublayout(sublayout);
			const bool default_s1 = auth_input_low_entropy_default_s1_sublayout(sublayout);
			const bool default_s11 = auth_input_low_entropy_default_s11_sublayout(sublayout);
			candidate = compress_auth_input_with_dict(low_entropy_candidate_raw, false, false, false, false, !default_alt && !default_s1 && !default_s11, false, false, false, false, default_alt, false, default_s1, default_s11);
			if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
				compressed = candidate;
				compression_mode = MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_DICT_ZSTD;
				selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY;
				selected_raw_size = low_entropy_candidate_raw.size();
				delta_low_entropy_raw = low_entropy_candidate_raw;
				selected_low_entropy_sublayout = sublayout;
			}
			const bool surface_alt = auth_input_low_entropy_surface_alt_sublayout(sublayout);
			candidate = compress_auth_input_with_dict(low_entropy_candidate_raw, false, false, false, false, false, false, !surface_alt, false, false, false, surface_alt);
			if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
				compressed = candidate;
				compression_mode = MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT_ZSTD;
				selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY;
				selected_raw_size = low_entropy_candidate_raw.size();
				delta_low_entropy_raw = low_entropy_candidate_raw;
				selected_low_entropy_sublayout = sublayout;
			}
			const bool fallback_alt = auth_input_low_entropy_fallback_alt_sublayout(sublayout);
			candidate = compress_auth_input_with_dict(low_entropy_candidate_raw, false, false, false, false, false, false, false, !fallback_alt, fallback_alt);
			if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
				compressed = candidate;
				compression_mode = MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD;
				selected_layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY;
				selected_raw_size = low_entropy_candidate_raw.size();
				delta_low_entropy_raw = low_entropy_candidate_raw;
				selected_low_entropy_sublayout = sublayout;
			}
		}
	}
	candidate = compress_auth_input_plain(bitpacked_zero_raw);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
		selected_raw_size = bitpacked_zero_raw.size();
	}
	candidate = compress_auth_input_with_dict(bitpacked_zero_raw, false, true);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
		selected_raw_size = bitpacked_zero_raw.size();
	}
	candidate = compress_auth_input_with_dict(hybrid_raw, false, false, true);
	if (candidate.size() > 0 && (compressed.size() <= 0 || candidate.size() < compressed.size())) {
		compressed = candidate;
		compression_mode = MXT_NET_AUTH_MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD;
		selected_layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
		selected_raw_size = hybrid_raw_size;
	}
	if (compressed.size() <= 0 || !writer.write_bytes(compressed.ptr(), static_cast<int>(compressed.size()))) {
		return PackedByteArray();
	}
	if (selected_layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS) {
		dump_auth_input_sample(delta_pairs_raw, first_tick, count, racer_count, compression_mode, -1);
	} else if (selected_layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY) {
		dump_auth_input_sample(delta_low_entropy_raw, first_tick, count, racer_count, compression_mode, selected_low_entropy_sublayout);
	} else if (selected_layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS) {
		dump_auth_input_sample(bitpacked_raw, first_tick, count, racer_count, compression_mode, -1);
	} else if (selected_layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA) {
		dump_auth_input_sample(hybrid_raw, first_tick, count, racer_count, compression_mode, -1);
	} else if (selected_layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP) {
		dump_auth_input_sample(bitpacked_zero_raw, first_tick, count, racer_count, compression_mode, -1);
	} else {
		dump_auth_input_sample(packed_raw, first_tick, count, racer_count, compression_mode, -1);
	}
	writer.data[mode_count_pos] = selected_layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY ?
		pack_auth_low_entropy_mode_sublayout_phase(compression_mode, selected_low_entropy_sublayout, race_phase) :
		pack_auth_mode_count_phase(compression_mode, count, race_phase);
	stat_auth_raw_bytes += static_cast<uint64_t>(selected_raw_size);
	stat_auth_payload_bytes += static_cast<uint64_t>(writer.pos);
	return writer.to_pba();
}

godot::Dictionary NetcodeSession::store_authoritative_input_packet(godot::PackedByteArray packet, int expected_race_phase, int authoritative_last_tick, int external_mode_count_phase)
{
	Dictionary stats;
	stats["first_tick"] = -1;
	stats["last_tick"] = -1;
	stats["count"] = 0;
	stats["valid"] = false;
	stats["stale"] = false;

	PacketReader reader(packet);
	uint8_t count = 0;
	uint8_t mode_count_phase = MXT_NET_AUTH_MODE_DELTA_RACER_PAIRS_DICT_ZSTD;
	if (external_mode_count_phase >= 0) {
		mode_count_phase = static_cast<uint8_t>(external_mode_count_phase & 0xff);
	} else {
		if (!reader.read_u8(mode_count_phase)) {
			return stats;
		}
	}
	const uint8_t compression_mode = mode_count_phase & MXT_NET_AUTH_MODE_MASK;
	const bool mode_is_low_entropy =
		compression_mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_DICT_ZSTD ||
		compression_mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_DICT_ZSTD ||
		compression_mode == MXT_NET_AUTH_MODE_DELTA_LOW_ENTROPY_SURFACE_FALLBACK_DICT_ZSTD;
	const uint8_t count_code = static_cast<uint8_t>((mode_count_phase & MXT_NET_AUTH_COUNT_MASK) >> MXT_NET_AUTH_COUNT_SHIFT);
	int low_entropy_sublayout = -1;
	if (mode_is_low_entropy) {
		if (count_code > MXT_AUTH_INPUT_LOW_ENTROPY_MAX_SUBLAYOUT) {
			return stats;
		}
		count = 2;
		low_entropy_sublayout = static_cast<int>(count_code);
	} else if (count_code == MXT_NET_AUTH_COUNT_ESCAPE) {
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
	if (authoritative_last_tick < 0 || static_cast<int>(count) - 1 > authoritative_last_tick) {
		stats["valid"] = false;
		return stats;
	}
	const int first_tick = authoritative_last_tick - static_cast<int>(count) + 1;
	stats["first_tick"] = first_tick;
	stats["count"] = static_cast<int>(count);
	stats["valid"] = true;
	if (!auth_input_mode_valid(compression_mode)) {
		stats["valid"] = false;
		return stats;
	}
	const AuthInputLayout packet_layout = auth_input_layout_from_mode(compression_mode);
	const bool zero_bitmap_layout = packet_layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
	const bool low_entropy_layout = packet_layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY;
	const int raw_size = low_entropy_layout ?
		auth_input_delta_low_entropy_raw_size_bound(static_cast<int>(count), racer_count, low_entropy_sublayout < 0) :
		auth_input_raw_size(static_cast<int>(count), racer_count, packet_layout);
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
		if (zero_bitmap_layout) {
			raw = decompress_auth_input_with_dict_bound(
				compressed,
				raw_size,
				false,
				auth_input_mode_uses_zero_bitmap_dict(compression_mode)
			);
		} else if (low_entropy_layout) {
			raw = decompress_auth_input_with_dict_bound(
				compressed,
				raw_size,
				false,
				false,
				false,
				false,
				auth_input_mode_uses_delta_low_entropy_dict(compression_mode) &&
					!auth_input_low_entropy_default_alt_sublayout(static_cast<uint8_t>(low_entropy_sublayout)) &&
					!auth_input_low_entropy_default_s1_sublayout(static_cast<uint8_t>(low_entropy_sublayout)) &&
					!auth_input_low_entropy_default_s11_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				false,
				auth_input_mode_uses_delta_low_entropy_surface_dict(compression_mode) &&
					!auth_input_low_entropy_surface_alt_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				auth_input_mode_uses_delta_low_entropy_surface_fallback_dict(compression_mode) &&
					!auth_input_low_entropy_fallback_alt_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				auth_input_mode_uses_delta_low_entropy_surface_fallback_dict(compression_mode) &&
					auth_input_low_entropy_fallback_alt_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				auth_input_mode_uses_delta_low_entropy_dict(compression_mode) &&
					auth_input_low_entropy_default_alt_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				auth_input_mode_uses_delta_low_entropy_surface_dict(compression_mode) &&
					auth_input_low_entropy_surface_alt_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				auth_input_mode_uses_delta_low_entropy_dict(compression_mode) &&
					auth_input_low_entropy_default_s1_sublayout(static_cast<uint8_t>(low_entropy_sublayout)),
				auth_input_mode_uses_delta_low_entropy_dict(compression_mode) &&
					auth_input_low_entropy_default_s11_sublayout(static_cast<uint8_t>(low_entropy_sublayout))
			);
		} else {
			raw = decompress_auth_input_with_dict(
				compressed,
				raw_size,
				auth_input_mode_uses_hybrid_dict(compression_mode),
				false,
				auth_input_mode_uses_hybrid_smooth_dict(compression_mode),
				auth_input_mode_uses_delta_pairs_dict(compression_mode),
				false,
				auth_input_mode_uses_delta_pairs_surface_dict(compression_mode)
			);
		}
	} else if (zero_bitmap_layout) {
		raw = decompress_auth_input_plain_bound(compressed, raw_size);
	} else {
		raw = decompress_auth_input_plain_bound(compressed, raw_size);
	}
	if ((!zero_bitmap_layout && !low_entropy_layout && raw.size() != raw_size) ||
		((zero_bitmap_layout || low_entropy_layout) && raw.size() > raw_size)) {
		stats["valid"] = false;
		return stats;
	}
	AuthInputLayout layout = packet_layout;
	if (low_entropy_layout) {
		PackedByteArray expanded_raw;
		if (!decode_delta_low_entropy_raw(raw, expanded_raw, static_cast<int>(count), racer_count, low_entropy_sublayout)) {
			stats["valid"] = false;
			return stats;
		}
		raw = expanded_raw;
		layout = AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS;
	}
	if (zero_bitmap_layout) {
		PackedByteArray expanded_raw;
		const int bitpacked_size = auth_input_bitpacked_raw_size(static_cast<int>(count), racer_count);
		if (!decode_zero_bitmap_raw(raw, expanded_raw, bitpacked_size)) {
			stats["valid"] = false;
			return stats;
		}
		raw = expanded_raw;
		layout = AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS;
	}
	const uint8_t* raw_bytes = raw.ptr();
	int raw_pos = 0;
	const bool delta_layout = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA;
	const bool delta_pairs_layout = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS;
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
	} else if (delta_pairs_layout) {
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < static_cast<int>(count); ++f) {
				InputFrame* frame = frames[f];
				uint8_t value = raw_bytes[raw_pos++];
				if (f > 0) {
					value = static_cast<uint8_t>(static_cast<int>(previous) + unzigzag_i8(value));
				}
				previous = value;
				frame->inputs[i].strafe_left = trigger_from_byte(value);
			}
		}
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
	if (delta_pairs_layout) {
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < static_cast<int>(count); ++f) {
				InputFrame* frame = frames[f];
				uint8_t value = raw_bytes[raw_pos++];
				if (f > 0) {
					value = static_cast<uint8_t>(static_cast<int>(previous) + unzigzag_i8(value));
				}
				previous = value;
				frame->inputs[i].strafe_right = trigger_from_byte(value);
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < static_cast<int>(count); ++f) {
				InputFrame* frame = frames[f];
				uint8_t value = raw_bytes[raw_pos++];
				if (f > 0) {
					value = static_cast<uint8_t>(static_cast<int>(previous) + unzigzag_i8(value));
				}
				previous = value;
				frame->inputs[i].steer_horizontal = axis_from_byte(value);
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < static_cast<int>(count); ++f) {
				InputFrame* frame = frames[f];
				uint8_t value = raw_bytes[raw_pos++];
				if (f > 0) {
					value = static_cast<uint8_t>(static_cast<int>(previous) + unzigzag_i8(value));
				}
				previous = value;
				frame->inputs[i].steer_vertical = axis_from_byte(value);
			}
		}
		for (int i = 0; i < racer_count; ++i) {
			uint8_t previous = 0;
			for (int f = 0; f < static_cast<int>(count); ++f) {
				InputFrame* frame = frames[f];
				uint8_t mask = raw_bytes[raw_pos++];
				if (f > 0) {
					mask = static_cast<uint8_t>(static_cast<int>(previous) + unzigzag_i8(mask));
				}
				previous = mask;
				frame->inputs[i].accelerate = (mask & (1u << 0)) != 0 ? 1.0f : 0.0f;
				frame->inputs[i].brake = (mask & (1u << 1)) != 0 ? 1.0f : 0.0f;
				frame->inputs[i].spinattack = (mask & (1u << 2)) != 0;
				frame->inputs[i].sideattack = (mask & (1u << 3)) != 0;
				frame->inputs[i].boost = (mask & (1u << 4)) != 0;
			}
		}
	} else {
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
	const AuthInputLayout layouts[8] = {
		AUTH_INPUT_LAYOUT_OLD_BYTE_PLANES,
		AUTH_INPUT_LAYOUT_PACKED_BUTTONS,
		AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA,
		AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS,
		AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY,
		AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS,
		AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA,
		AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP,
	};
	const char* names[8] = {
		"old",
		"packed",
		"delta",
		"delta_pairs",
		"delta_low_entropy",
		"bitpacked",
		"hybrid",
		"bitpacked_zero",
	};
	for (int l = 0; l < 8; ++l) {
		const AuthInputLayout layout = layouts[l];
		int raw_size = auth_input_raw_size(count, racer_count, layout);
		int best_low_entropy_default_sublayout = -1;
		int best_low_entropy_surface_sublayout = -1;
		int best_low_entropy_surface_fallback_sublayout = -1;
		int best_low_entropy_default_payload = 0;
		int best_low_entropy_surface_payload = 0;
		int best_low_entropy_surface_fallback_payload = 0;
		int low_entropy_mask_pair_counts[11] = {};
		PackedByteArray raw;
		if (layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP) {
			PackedByteArray bitpacked_raw;
			const int bitpacked_raw_size = auth_input_bitpacked_raw_size(count, racer_count);
			if (bitpacked_raw.resize(bitpacked_raw_size) != 0) {
				return out;
			}
			if (!write_authoritative_input_raw(bitpacked_raw, frames, count, AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS)) {
				return out;
			}
			raw = encode_zero_bitmap_raw(bitpacked_raw);
			raw_size = raw.size();
		} else if (layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY) {
			PackedByteArray delta_pairs_raw;
			const int delta_pairs_raw_size = auth_input_raw_size(count, racer_count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS);
			if (delta_pairs_raw.resize(delta_pairs_raw_size) != 0) {
				return out;
			}
			if (!write_authoritative_input_raw(delta_pairs_raw, frames, count, AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS)) {
				return out;
			}
			const int field_bytes = count * racer_count;
			const int mask_base = 4 * field_bytes;
			const uint8_t* delta_pairs_src = delta_pairs_raw.ptr();
			for (int i = 0; i < racer_count; ++i) {
				const uint8_t a = delta_pairs_src[mask_base + (i * 2)];
				const uint8_t b = delta_pairs_src[mask_base + (i * 2) + 1];
				int code = 10;
				for (int p = 0; p < 10; ++p) {
					if (MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][0] == a && MXT_AUTH_INPUT_LOW_ENTROPY_MASK_PAIRS[p][1] == b) {
						code = p;
						break;
					}
				}
				++low_entropy_mask_pair_counts[code];
			}
			int best_compressed_size = 0;
			for (uint8_t sublayout = MXT_AUTH_INPUT_LOW_ENTROPY_CURRENT; sublayout <= MXT_AUTH_INPUT_LOW_ENTROPY_MAX_SUBLAYOUT; ++sublayout) {
				PackedByteArray candidate_raw = encode_delta_low_entropy_raw(delta_pairs_raw, count, racer_count, sublayout, false);
				const bool default_alt = auth_input_low_entropy_default_alt_sublayout(sublayout);
				const bool default_s1 = auth_input_low_entropy_default_s1_sublayout(sublayout);
				const bool default_s11 = auth_input_low_entropy_default_s11_sublayout(sublayout);
				const PackedByteArray compressed_default = compress_auth_input_with_dict(candidate_raw, false, false, false, false, !default_alt && !default_s1 && !default_s11, false, false, false, false, default_alt, false, default_s1, default_s11);
				if (compressed_default.size() > 0 && (best_low_entropy_default_payload <= 0 || compressed_default.size() < best_low_entropy_default_payload)) {
					best_low_entropy_default_payload = compressed_default.size();
					best_low_entropy_default_sublayout = static_cast<int>(sublayout);
				}
				if (compressed_default.size() > 0 && (best_compressed_size <= 0 || compressed_default.size() < best_compressed_size)) {
					raw = candidate_raw;
					best_compressed_size = compressed_default.size();
				}
				const bool surface_alt = auth_input_low_entropy_surface_alt_sublayout(sublayout);
				const PackedByteArray compressed_surface = compress_auth_input_with_dict(candidate_raw, false, false, false, false, false, false, !surface_alt, false, false, false, surface_alt);
				if (compressed_surface.size() > 0 && (best_low_entropy_surface_payload <= 0 || compressed_surface.size() < best_low_entropy_surface_payload)) {
					best_low_entropy_surface_payload = compressed_surface.size();
					best_low_entropy_surface_sublayout = static_cast<int>(sublayout);
				}
				if (compressed_surface.size() > 0 && (best_compressed_size <= 0 || compressed_surface.size() < best_compressed_size)) {
					raw = candidate_raw;
					best_compressed_size = compressed_surface.size();
				}
				const bool fallback_alt = auth_input_low_entropy_fallback_alt_sublayout(sublayout);
				const PackedByteArray compressed_surface_fallback = compress_auth_input_with_dict(candidate_raw, false, false, false, false, false, false, false, !fallback_alt, fallback_alt);
				if (compressed_surface_fallback.size() > 0 && (best_low_entropy_surface_fallback_payload <= 0 || compressed_surface_fallback.size() < best_low_entropy_surface_fallback_payload)) {
					best_low_entropy_surface_fallback_payload = compressed_surface_fallback.size();
					best_low_entropy_surface_fallback_sublayout = static_cast<int>(sublayout);
				}
				if (compressed_surface_fallback.size() > 0 && (best_compressed_size <= 0 || compressed_surface_fallback.size() < best_compressed_size)) {
					raw = candidate_raw;
					best_compressed_size = compressed_surface_fallback.size();
				}
			}
			raw_size = raw.size();
		} else {
			if (raw.resize(raw_size) != 0) {
				return out;
			}
			if (!write_authoritative_input_raw(raw, frames, count, layout)) {
				return out;
			}
		}
		const PackedByteArray plain = compress_auth_input_plain(raw);
		const PackedByteArray dict = compress_auth_input_with_dict(raw);
		const PackedByteArray hybrid_dict = layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA ?
			compress_auth_input_with_dict(raw, true) :
			PackedByteArray();
		const PackedByteArray hybrid_smooth_dict = layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA ?
			compress_auth_input_with_dict(raw, false, false, true) :
			PackedByteArray();
		const PackedByteArray zero_bitmap_dict = layout == AUTH_INPUT_LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP ?
			compress_auth_input_with_dict(raw, false, true) :
			PackedByteArray();
		const PackedByteArray delta_pairs_dict = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS ?
			compress_auth_input_with_dict(raw, false, false, false, true) :
			PackedByteArray();
		const PackedByteArray delta_pairs_surface_dict = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_RACER_PAIRS ?
			compress_auth_input_with_dict(raw, false, false, false, false, false, true) :
			PackedByteArray();
		const PackedByteArray delta_low_entropy_dict = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY ?
			compress_auth_input_with_dict(raw, false, false, false, false, true) :
			PackedByteArray();
		const PackedByteArray delta_low_entropy_surface_dict = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY ?
			compress_auth_input_with_dict(raw, false, false, false, false, false, false, true) :
			PackedByteArray();
		const PackedByteArray delta_low_entropy_surface_fallback_dict = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY ?
			compress_auth_input_with_dict(raw, false, false, false, false, false, false, false, true) :
			PackedByteArray();
		const bool low_entropy_compare_layout = layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY;
		const int delta_low_entropy_best_default_size = low_entropy_compare_layout ?
			best_low_entropy_default_payload :
			delta_low_entropy_dict.size();
		const int delta_low_entropy_best_surface_size = low_entropy_compare_layout ?
			best_low_entropy_surface_payload :
			delta_low_entropy_surface_dict.size();
		const int delta_low_entropy_best_surface_fallback_size = low_entropy_compare_layout ?
			best_low_entropy_surface_fallback_payload :
			delta_low_entropy_surface_fallback_dict.size();
		const String prefix = String(names[l]) + String("_");
		out[prefix + String("raw")] = raw_size;
		out[prefix + String("plain_payload")] = plain.size();
		out[prefix + String("plain_packet")] = plain.size() + auth_input_packet_header_size(count);
		if (layout == AUTH_INPUT_LAYOUT_PACKED_BUTTONS_FRAME_DELTA_LOW_ENTROPY) {
			out[prefix + String("best_default_sublayout")] = best_low_entropy_default_sublayout;
			out[prefix + String("best_surface_sublayout")] = best_low_entropy_surface_sublayout;
			out[prefix + String("best_surface_fallback_sublayout")] = best_low_entropy_surface_fallback_sublayout;
			out[prefix + String("best_default_payload")] = best_low_entropy_default_payload;
			out[prefix + String("best_surface_payload")] = best_low_entropy_surface_payload;
			out[prefix + String("best_surface_fallback_payload")] = best_low_entropy_surface_fallback_payload;
			for (int i = 0; i < 11; ++i) {
				out[prefix + String("mask_pair_count_") + String::num_int64(i)] = low_entropy_mask_pair_counts[i];
			}
		}
		const int dict_size = delta_low_entropy_best_default_size > 0 ? delta_low_entropy_best_default_size :
			(delta_pairs_dict.size() > 0 ? delta_pairs_dict.size() :
				(hybrid_dict.size() > 0 ? hybrid_dict.size() :
					(zero_bitmap_dict.size() > 0 ? zero_bitmap_dict.size() : dict.size())));
		out[prefix + String("dict_payload")] = dict_size;
		out[prefix + String("dict_packet")] = dict_size + auth_input_packet_header_size(count);
		if (hybrid_smooth_dict.size() > 0) {
			out[prefix + String("smooth_dict_payload")] = hybrid_smooth_dict.size();
			out[prefix + String("smooth_dict_packet")] = hybrid_smooth_dict.size() + auth_input_packet_header_size(count);
		}
		if (delta_pairs_surface_dict.size() > 0) {
			out[prefix + String("surface_dict_payload")] = delta_pairs_surface_dict.size();
			out[prefix + String("surface_dict_packet")] = delta_pairs_surface_dict.size() + auth_input_packet_header_size(count);
		}
		if (delta_low_entropy_best_surface_size > 0) {
			out[prefix + String("surface_dict_payload")] = delta_low_entropy_best_surface_size;
			out[prefix + String("surface_dict_packet")] = delta_low_entropy_best_surface_size + auth_input_packet_header_size(count);
		}
		if (delta_low_entropy_best_surface_fallback_size > 0) {
			out[prefix + String("surface_fallback_dict_payload")] = delta_low_entropy_best_surface_fallback_size;
			out[prefix + String("surface_fallback_dict_packet")] = delta_low_entropy_best_surface_fallback_size + auth_input_packet_header_size(count);
		}
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
