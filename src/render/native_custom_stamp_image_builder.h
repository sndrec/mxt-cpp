#ifndef MXT_NATIVE_CUSTOM_STAMP_IMAGE_BUILDER_H
#define MXT_NATIVE_CUSTOM_STAMP_IMAGE_BUILDER_H

#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/packed_color_array.hpp"

namespace godot {

class NativeCustomStampImageBuilder : public RefCounted {
	GDCLASS(NativeCustomStampImageBuilder, RefCounted)

protected:
	static void _bind_methods();

public:
	Ref<Image> build_indexed_image(
			const PackedByteArray &p_indices,
			int p_width,
			int p_height,
			int p_bits_per_pixel,
			const PackedColorArray &p_palette,
			bool p_rotated) const;
};

} // namespace godot

#endif
