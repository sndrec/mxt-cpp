#pragma once

#include "godot_cpp/variant/packed_byte_array.hpp"

#include <cstdint>

namespace mxt::auth_input {

constexpr uint8_t MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD = 0;
constexpr uint8_t MODE_BITPACKED_ZERO_BITMAP_STRAFE_SPARSE_DICT_ZSTD = 1;
constexpr uint8_t MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD = 2;
constexpr uint8_t MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD = 3;
constexpr uint8_t MODE_MASK = 0x07;
constexpr uint8_t COUNT_SHIFT = 3;
constexpr uint8_t COUNT_MASK = 0x78;
constexpr uint8_t COUNT_ESCAPE = 0x0f;
constexpr uint8_t PHASE_BIT = 0x80;

enum AuthInputLayout : uint8_t {
	LAYOUT_BITPACKED_BUTTONS = 0,
	LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA = 1,
	LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP = 2,
	LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP_STRAFE_SPARSE = 3,
};

enum AuthInputDictionary : uint8_t {
	DICTIONARY_ZERO_BITMAP = 0,
	DICTIONARY_STRAFE_SPARSE = 1,
	DICTIONARY_SMOOTH_ANALOG_DELTA = 2,
};

struct EncodedSelection {
	godot::PackedByteArray payload;
	AuthInputLayout layout = LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP;
	uint8_t mode = MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD;
	int raw_size = 0;
	int candidate_count = 0;
};

int raw_size(int frame_count, int racer_count, AuthInputLayout layout);
int bitpacked_raw_size(int frame_count, int racer_count);
AuthInputLayout layout_from_mode(uint8_t mode);
bool mode_valid(uint8_t mode);
uint8_t pack_mode_count_phase(uint8_t mode, int count, int race_phase);
bool count_needs_escape(int count);

godot::PackedByteArray encode_zero_bitmap(const godot::PackedByteArray& source);
bool decode_zero_bitmap(const godot::PackedByteArray& encoded, godot::PackedByteArray& output, int output_size);
godot::PackedByteArray encode_strafe_sparse(const godot::PackedByteArray& bitpacked, int frame_count, int racer_count);
bool decode_strafe_sparse(const godot::PackedByteArray& encoded, godot::PackedByteArray& output, int frame_count, int racer_count);

godot::PackedByteArray compress_with_dictionary(const godot::PackedByteArray& raw, AuthInputDictionary dictionary);
godot::PackedByteArray compress_plain(const godot::PackedByteArray& raw);
godot::PackedByteArray decompress_with_dictionary(const godot::PackedByteArray& compressed, int raw_size, AuthInputDictionary dictionary);
godot::PackedByteArray decompress_with_dictionary_bound(const godot::PackedByteArray& compressed, int raw_size_bound, AuthInputDictionary dictionary);
godot::PackedByteArray decompress_plain_bound(const godot::PackedByteArray& compressed, int raw_size_bound);
EncodedSelection select_best(
	const godot::PackedByteArray& zero_bitmap,
	const godot::PackedByteArray& strafe_sparse,
	const godot::PackedByteArray& smooth_analog_delta);

} // namespace mxt::auth_input
