#ifndef MXT_GLB_VALIDATOR_H
#define MXT_GLB_VALIDATOR_H

#include "content/content_manifest.h"

#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace mxt::content {

struct VehicleGlbSurface {
	bool has_albedo_texture = false;
	bool has_normal_texture = false;
	bool has_paint_mask_texture = false;
	uint32_t material_index = UINT32_MAX;
	godot::String name;
};

struct VehicleGlbInfo {
	std::vector<VehicleGlbSurface> surfaces;
};

bool validate_glb_file(
		const godot::String &path,
		ContentType content_type,
		std::vector<godot::String> &out_errors,
		VehicleGlbInfo *out_vehicle_info = nullptr);

} // namespace mxt::content

#endif // MXT_GLB_VALIDATOR_H
