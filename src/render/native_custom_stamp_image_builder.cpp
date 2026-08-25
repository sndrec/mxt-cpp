#include "render/native_custom_stamp_image_builder.h"

#include "godot_cpp/core/class_db.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace godot;

namespace {

constexpr int BPP_CUSTOM_PALETTE = 4;
constexpr int BPP_AUTHORED_PALETTE = 8;

static uint8_t to_unorm8(float p_value)
{
	return static_cast<uint8_t>(std::lround(std::clamp(p_value, 0.0f, 1.0f) * 255.0f));
}

static int palette_index_at(const uint8_t *p_indices, int p_pixel_index, int p_bits_per_pixel)
{
	if (p_bits_per_pixel == BPP_CUSTOM_PALETTE) {
		const uint8_t packed = p_indices[p_pixel_index >> 1];
		return (p_pixel_index & 1) == 0 ? packed & 0x0f : packed >> 4;
	}
	return p_indices[p_pixel_index];
}

static void bleed_transparent_edges(const std::vector<uint8_t> &p_source, int p_width, int p_height, std::vector<uint8_t> &r_output)
{
	r_output = p_source;
	for (int y = 0; y < p_height; ++y) {
		for (int x = 0; x < p_width; ++x) {
			const size_t pixel_offset = static_cast<size_t>((y * p_width + x) * 4);
			if (p_source[pixel_offset + 3] != 0) {
				continue;
			}

			uint32_t red = 0;
			uint32_t green = 0;
			uint32_t blue = 0;
			uint32_t weight = 0;
			const int min_y = std::max(0, y - 1);
			const int max_y = std::min(p_height - 1, y + 1);
			const int min_x = std::max(0, x - 1);
			const int max_x = std::min(p_width - 1, x + 1);
			for (int neighbor_y = min_y; neighbor_y <= max_y; ++neighbor_y) {
				for (int neighbor_x = min_x; neighbor_x <= max_x; ++neighbor_x) {
					const size_t neighbor_offset = static_cast<size_t>((neighbor_y * p_width + neighbor_x) * 4);
					const uint32_t alpha = p_source[neighbor_offset + 3];
					if (alpha == 0) {
						continue;
					}
					red += static_cast<uint32_t>(p_source[neighbor_offset]) * alpha;
					green += static_cast<uint32_t>(p_source[neighbor_offset + 1]) * alpha;
					blue += static_cast<uint32_t>(p_source[neighbor_offset + 2]) * alpha;
					weight += alpha;
				}
			}
			if (weight == 0) {
				continue;
			}
			r_output[pixel_offset] = static_cast<uint8_t>((red + weight / 2) / weight);
			r_output[pixel_offset + 1] = static_cast<uint8_t>((green + weight / 2) / weight);
			r_output[pixel_offset + 2] = static_cast<uint8_t>((blue + weight / 2) / weight);
		}
	}
}

} // namespace

void NativeCustomStampImageBuilder::_bind_methods()
{
	ClassDB::bind_method(
			D_METHOD("build_indexed_image", "indices", "width", "height", "bits_per_pixel", "palette", "rotated"),
			&NativeCustomStampImageBuilder::build_indexed_image);
}

Ref<Image> NativeCustomStampImageBuilder::build_indexed_image(
		const PackedByteArray &p_indices,
		int p_width,
		int p_height,
		int p_bits_per_pixel,
		const PackedColorArray &p_palette,
		bool p_rotated) const
{
	if (p_width <= 0 || p_height <= 0 || p_palette.is_empty()) {
		return Ref<Image>();
	}
	if (p_bits_per_pixel != BPP_CUSTOM_PALETTE && p_bits_per_pixel != BPP_AUTHORED_PALETTE) {
		return Ref<Image>();
	}
	const int64_t pixel_count = static_cast<int64_t>(p_width) * p_height;
	const int64_t expected_bytes = p_bits_per_pixel == BPP_CUSTOM_PALETTE ? (pixel_count + 1) / 2 : pixel_count;
	if (pixel_count <= 0 || p_indices.size() != expected_bytes) {
		return Ref<Image>();
	}

	std::vector<uint8_t> decoded(static_cast<size_t>(pixel_count) * 4);
	const uint8_t *indices = p_indices.ptr();
	const Color *palette = p_palette.ptr();
	for (int64_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
		const int palette_index = palette_index_at(indices, static_cast<int>(pixel_index), p_bits_per_pixel);
		Color colour(1.0f, 1.0f, 1.0f, 0.0f);
		if (palette_index > 0 && palette_index < p_palette.size()) {
			colour = palette[palette_index];
		}
		const size_t output_offset = static_cast<size_t>(pixel_index) * 4;
		decoded[output_offset] = to_unorm8(colour.r);
		decoded[output_offset + 1] = to_unorm8(colour.g);
		decoded[output_offset + 2] = to_unorm8(colour.b);
		decoded[output_offset + 3] = to_unorm8(colour.a);
	}

	std::vector<uint8_t> bled;
	bleed_transparent_edges(decoded, p_width, p_height, bled);
	const int output_width = p_rotated ? p_height : p_width;
	const int output_height = p_rotated ? p_width : p_height;
	PackedByteArray output;
	output.resize(pixel_count * 4);
	uint8_t *output_bytes = output.ptrw();
	if (!p_rotated) {
		std::memcpy(output_bytes, bled.data(), bled.size());
	} else {
		for (int y = 0; y < p_height; ++y) {
			for (int x = 0; x < p_width; ++x) {
				const int source_pixel = y * p_width + x;
				const int output_x = y;
				const int output_y = p_width - 1 - x;
				const int output_pixel = output_y * output_width + output_x;
				std::memcpy(output_bytes + output_pixel * 4, bled.data() + source_pixel * 4, 4);
			}
		}
	}
	return Image::create_from_data(output_width, output_height, false, Image::FORMAT_RGBA8, output);
}
