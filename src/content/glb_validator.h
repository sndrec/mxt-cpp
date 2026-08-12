#ifndef MXT_GLB_VALIDATOR_H
#define MXT_GLB_VALIDATOR_H

#include "content/content_manifest.h"

#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace mxt::content {

bool validate_glb_file(
		const godot::String &path,
		ContentType content_type,
		std::vector<godot::String> &out_errors);

} // namespace mxt::content

#endif // MXT_GLB_VALIDATOR_H
