#include "netcode/auth_input_codec.h"

#include "netcode/generated/auth_input_hybrid_smooth_zstd_dictionary.h"
#include "netcode/generated/auth_input_zero_bitmap_strafe_sparse_zstd_dictionary.h"
#include "netcode/generated/auth_input_zero_bitmap_zstd_dictionary.h"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include <cstdint>
#include <cstring>

using namespace godot;

namespace mxt::auth_input {
namespace {
constexpr int AUTH_ZSTD_LEVEL = 3;
constexpr int ZERO_BITMAP_ZSTD_LEVEL = 7;
ZSTD_CCtx* g_auth_input_zstd_cctx = nullptr;
ZSTD_DCtx* g_auth_input_zstd_dctx = nullptr;
ZSTD_CDict* g_auth_input_hybrid_smooth_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_hybrid_smooth_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_zero_bitmap_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_zero_bitmap_zstd_ddict = nullptr;
ZSTD_CDict* g_auth_input_zero_bitmap_strafe_sparse_zstd_cdict = nullptr;
ZSTD_DDict* g_auth_input_zero_bitmap_strafe_sparse_zstd_ddict = nullptr;
} // namespace
int raw_size(int frame_count, int racer_count, AuthInputLayout layout)
{
	const int input_count = frame_count * racer_count;
	const int bitpacked_size = (((input_count + 7) >> 3) * 5) + input_count * 4;
	if (layout == LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP_STRAFE_SPARSE) {
		const int sparse_prefix_size = (((input_count + 7) >> 3) * 5) + input_count * 2;
		return ((sparse_prefix_size + 7) >> 3) + sparse_prefix_size + input_count * 2;
	}
	return layout == LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP ?
		((bitpacked_size + 7) >> 3) + bitpacked_size :
		bitpacked_size;
}

AuthInputLayout layout_from_mode(uint8_t mode)
{
	mode &= MODE_MASK;
	if (mode == MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD) {
		return LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	}
	if (mode == MODE_BITPACKED_ZERO_BITMAP_STRAFE_SPARSE_DICT_ZSTD) {
		return LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP_STRAFE_SPARSE;
	}
	return LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
}

bool mode_valid(uint8_t mode)
{
	return (mode & MODE_MASK) <= MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD;
}

uint8_t pack_mode_count_phase(uint8_t mode, int count, int race_phase)
{
	const uint8_t count_code = (count >= 1 && count <= 14) ?
		static_cast<uint8_t>(count - 1) :
		COUNT_ESCAPE;
	return static_cast<uint8_t>(
		(mode & MODE_MASK) |
		static_cast<uint8_t>(count_code << COUNT_SHIFT) |
		((race_phase & 1) ? PHASE_BIT : 0)
	);
}

bool count_needs_escape(int count)
{
	return count <= 0 || count >= 15;
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

ZSTD_CDict* auth_input_hybrid_smooth_zstd_cdict()
{
	if (!g_auth_input_hybrid_smooth_zstd_cdict) {
		g_auth_input_hybrid_smooth_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_HYBRID_SMOOTH_ZSTD_DICT,
			MXT_AUTH_INPUT_HYBRID_SMOOTH_ZSTD_DICT_SIZE,
			AUTH_ZSTD_LEVEL,
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
			ZERO_BITMAP_ZSTD_LEVEL,
			ZSTD_btlazy2
		);
	}
	return g_auth_input_zero_bitmap_zstd_cdict;
}

ZSTD_CDict* auth_input_zero_bitmap_strafe_sparse_zstd_cdict()
{
	if (!g_auth_input_zero_bitmap_strafe_sparse_zstd_cdict) {
		g_auth_input_zero_bitmap_strafe_sparse_zstd_cdict = create_auth_input_zstd_cdict(
			MXT_AUTH_INPUT_ZERO_BITMAP_STRAFE_SPARSE_ZSTD_DICT,
			MXT_AUTH_INPUT_ZERO_BITMAP_STRAFE_SPARSE_ZSTD_DICT_SIZE,
			ZERO_BITMAP_ZSTD_LEVEL,
			ZSTD_btultra
		);
	}
	return g_auth_input_zero_bitmap_strafe_sparse_zstd_cdict;
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

ZSTD_DDict* auth_input_zero_bitmap_strafe_sparse_zstd_ddict()
{
	if (!g_auth_input_zero_bitmap_strafe_sparse_zstd_ddict) {
		g_auth_input_zero_bitmap_strafe_sparse_zstd_ddict = ZSTD_createDDict(
			MXT_AUTH_INPUT_ZERO_BITMAP_STRAFE_SPARSE_ZSTD_DICT,
			MXT_AUTH_INPUT_ZERO_BITMAP_STRAFE_SPARSE_ZSTD_DICT_SIZE
		);
	}
	return g_auth_input_zero_bitmap_strafe_sparse_zstd_ddict;
}

int bitpacked_raw_size(int frame_count, int racer_count)
{
	const int input_count = frame_count * racer_count;
	return (((input_count + 7) >> 3) * 5) + input_count * 4;
}

PackedByteArray encode_zero_bitmap(const PackedByteArray& src)
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

bool decode_zero_bitmap(const PackedByteArray& encoded, PackedByteArray& out, int raw_size)
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

PackedByteArray encode_strafe_sparse(const PackedByteArray& bitpacked_raw, int frame_count, int p_racer_count)
{
	const int input_count = frame_count * p_racer_count;
	if (input_count <= 0) {
		return PackedByteArray();
	}
	const int bitset_bytes = (input_count + 7) >> 3;
	const int sparse_prefix_size = (bitset_bytes * 5) + (input_count * 2);
	const int dense_suffix_size = input_count * 2;
	if (bitpacked_raw.size() != sparse_prefix_size + dense_suffix_size) {
		return PackedByteArray();
	}
	const int prefix_bitmap_bytes = (sparse_prefix_size + 7) >> 3;
	const uint8_t* src = bitpacked_raw.ptr();
	int nonzero_count = 0;
	for (int i = 0; i < sparse_prefix_size; ++i) {
		if (src[i] != 0) {
			++nonzero_count;
		}
	}
	PackedByteArray out;
	if (out.resize(prefix_bitmap_bytes + nonzero_count + dense_suffix_size) != 0) {
		return PackedByteArray();
	}
	uint8_t* dst = out.ptrw();
	std::memset(dst, 0, static_cast<size_t>(prefix_bitmap_bytes));
	int out_pos = prefix_bitmap_bytes;
	for (int i = 0; i < sparse_prefix_size; ++i) {
		const uint8_t value = src[i];
		if (value != 0) {
			dst[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
			dst[out_pos++] = value;
		}
	}
	std::memcpy(dst + out_pos, src + sparse_prefix_size, static_cast<size_t>(dense_suffix_size));
	return out;
}

bool decode_strafe_sparse(const PackedByteArray& encoded, PackedByteArray& out, int frame_count, int p_racer_count)
{
	const int input_count = frame_count * p_racer_count;
	if (input_count <= 0 || encoded.size() <= 0) {
		return false;
	}
	const int bitset_bytes = (input_count + 7) >> 3;
	const int sparse_prefix_size = (bitset_bytes * 5) + (input_count * 2);
	const int dense_suffix_size = input_count * 2;
	const int prefix_bitmap_bytes = (sparse_prefix_size + 7) >> 3;
	if (encoded.size() < prefix_bitmap_bytes + dense_suffix_size) {
		return false;
	}
	int nonzero_count = 0;
	const uint8_t* src = encoded.ptr();
	for (int i = 0; i < sparse_prefix_size; ++i) {
		if ((src[i >> 3] & static_cast<uint8_t>(1u << (i & 7))) != 0) {
			++nonzero_count;
		}
	}
	if (encoded.size() != prefix_bitmap_bytes + nonzero_count + dense_suffix_size) {
		return false;
	}
	const int bitpacked_size = sparse_prefix_size + dense_suffix_size;
	if (out.resize(bitpacked_size) != 0) {
		return false;
	}
	uint8_t* dst = out.ptrw();
	std::memset(dst, 0, static_cast<size_t>(bitpacked_size));
	int encoded_pos = prefix_bitmap_bytes;
	for (int i = 0; i < sparse_prefix_size; ++i) {
		if ((src[i >> 3] & static_cast<uint8_t>(1u << (i & 7))) != 0) {
			dst[i] = src[encoded_pos++];
		}
	}
	std::memcpy(dst + sparse_prefix_size, src + encoded_pos, static_cast<size_t>(dense_suffix_size));
	return true;
}

ZSTD_CDict* auth_input_cdict(AuthInputDictionary dictionary)
{
	switch (dictionary) {
		case DICTIONARY_ZERO_BITMAP: return auth_input_zero_bitmap_zstd_cdict();
		case DICTIONARY_STRAFE_SPARSE: return auth_input_zero_bitmap_strafe_sparse_zstd_cdict();
		case DICTIONARY_SMOOTH_ANALOG_DELTA: return auth_input_hybrid_smooth_zstd_cdict();
		default: return nullptr;
	}
}

ZSTD_DDict* auth_input_ddict(AuthInputDictionary dictionary)
{
	switch (dictionary) {
		case DICTIONARY_ZERO_BITMAP: return auth_input_zero_bitmap_zstd_ddict();
		case DICTIONARY_STRAFE_SPARSE: return auth_input_zero_bitmap_strafe_sparse_zstd_ddict();
		case DICTIONARY_SMOOTH_ANALOG_DELTA: return auth_input_hybrid_smooth_zstd_ddict();
		default: return nullptr;
	}
}

PackedByteArray compress_with_dictionary(const PackedByteArray& raw, AuthInputDictionary dictionary)
{
	const int raw_size = raw.size();
	ZSTD_CCtx* cctx = auth_input_zstd_cctx();
	ZSTD_CDict* cdict = auth_input_cdict(dictionary);
	if (raw_size <= 0 || !cctx || !cdict) {
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
	const size_t begin_result = ZSTD_compressBegin_usingCDict(cctx, cdict);
	if (ZSTD_isError(begin_result)) {
		return PackedByteArray();
	}
	const size_t compressed_size = ZSTD_compressBlock(cctx, out.ptrw(), bound, raw.ptr(), static_cast<size_t>(raw_size));
	if (ZSTD_isError(compressed_size) || compressed_size == 0 || compressed_size > bound || compressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(compressed_size));
	return out;
}

PackedByteArray compress_plain(const PackedByteArray& raw)
{
	const int raw_size = raw.size();
	ZSTD_CCtx* cctx = auth_input_zstd_cctx();
	if (raw_size <= 0 || !cctx) {
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
	if (ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, AUTH_ZSTD_LEVEL)) ||
		ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 0)) ||
		ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0)) ||
		ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_dictIDFlag, 0))) {
		return PackedByteArray();
	}
	const size_t compressed_size = ZSTD_compress2(cctx, out.ptrw(), bound, raw.ptr(), static_cast<size_t>(raw_size));
	if (ZSTD_isError(compressed_size) || compressed_size > bound || compressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(compressed_size));
	return out;
}

PackedByteArray decompress_with_dictionary(const PackedByteArray& compressed, int raw_size, AuthInputDictionary dictionary)
{
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	ZSTD_DDict* ddict = auth_input_ddict(dictionary);
	if (raw_size <= 0 || compressed.size() <= 0 || !dctx || !ddict) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size) != 0 || ZSTD_isError(ZSTD_decompressBegin_usingDDict(dctx, ddict))) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompressBlock(dctx, out.ptrw(), static_cast<size_t>(raw_size), compressed.ptr(), static_cast<size_t>(compressed.size()));
	if (ZSTD_isError(decompressed_size) || decompressed_size != static_cast<size_t>(raw_size)) {
		return PackedByteArray();
	}
	return out;
}

PackedByteArray decompress_with_dictionary_bound(const PackedByteArray& compressed, int raw_size_bound, AuthInputDictionary dictionary)
{
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	ZSTD_DDict* ddict = auth_input_ddict(dictionary);
	if (raw_size_bound <= 0 || compressed.size() <= 0 || !dctx || !ddict) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size_bound) != 0 || ZSTD_isError(ZSTD_decompressBegin_usingDDict(dctx, ddict))) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompressBlock(dctx, out.ptrw(), static_cast<size_t>(raw_size_bound), compressed.ptr(), static_cast<size_t>(compressed.size()));
	if (ZSTD_isError(decompressed_size) || decompressed_size == 0 || decompressed_size > static_cast<size_t>(raw_size_bound) || decompressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(decompressed_size));
	return out;
}

PackedByteArray decompress_plain_bound(const PackedByteArray& compressed, int raw_size_bound)
{
	ZSTD_DCtx* dctx = auth_input_zstd_dctx();
	if (raw_size_bound <= 0 || compressed.size() <= 0 || !dctx) {
		return PackedByteArray();
	}
	PackedByteArray out;
	if (out.resize(raw_size_bound) != 0) {
		return PackedByteArray();
	}
	const size_t decompressed_size = ZSTD_decompressDCtx(dctx, out.ptrw(), static_cast<size_t>(raw_size_bound), compressed.ptr(), static_cast<size_t>(compressed.size()));
	if (ZSTD_isError(decompressed_size) || decompressed_size == 0 || decompressed_size > static_cast<size_t>(raw_size_bound) || decompressed_size > static_cast<size_t>(INT32_MAX)) {
		return PackedByteArray();
	}
	out.resize(static_cast<int>(decompressed_size));
	return out;
}

EncodedSelection select_best(
	const PackedByteArray& zero_bitmap,
	const PackedByteArray& strafe_sparse,
	const PackedByteArray& smooth_analog_delta)
{
	EncodedSelection selected;
	selected.candidate_count = strafe_sparse.is_empty() ? 2 : 3;

	if (!strafe_sparse.is_empty()) {
		selected.payload = compress_with_dictionary(strafe_sparse, DICTIONARY_STRAFE_SPARSE);
		if (!selected.payload.is_empty()) {
			selected.mode = MODE_BITPACKED_ZERO_BITMAP_STRAFE_SPARSE_DICT_ZSTD;
			selected.layout = LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP_STRAFE_SPARSE;
			selected.raw_size = strafe_sparse.size();
		}
	}

	PackedByteArray candidate = compress_with_dictionary(zero_bitmap, DICTIONARY_ZERO_BITMAP);
	if (!candidate.is_empty() && (selected.payload.is_empty() || candidate.size() < selected.payload.size())) {
		selected.payload = candidate;
		selected.mode = MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD;
		selected.layout = LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
		selected.raw_size = zero_bitmap.size();
	}

	candidate = compress_with_dictionary(smooth_analog_delta, DICTIONARY_SMOOTH_ANALOG_DELTA);
	if (!candidate.is_empty() && (selected.payload.is_empty() || candidate.size() < selected.payload.size())) {
		selected.payload = candidate;
		selected.mode = MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD;
		selected.layout = LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
		selected.raw_size = smooth_analog_delta.size();
	}

	if (selected.payload.is_empty()) {
		selected.payload = compress_plain(zero_bitmap);
		selected.mode = MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD;
		selected.layout = LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
		selected.raw_size = zero_bitmap.size();
	}
	return selected;
}

} // namespace mxt::auth_input
