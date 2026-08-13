#ifndef MXT_NATIVE_STAMP_MESH_BUILDER_H
#define MXT_NATIVE_STAMP_MESH_BUILDER_H

#include "godot_cpp/classes/mesh_instance3d.hpp"
#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"

namespace godot {

class Object;

class NativeStampMeshBuilder : public RefCounted {
	GDCLASS(NativeStampMeshBuilder, RefCounted)

protected:
	static void _bind_methods();

public:
	Dictionary build_for_body_mesh_with_masks(
			MeshInstance3D *p_body_mesh,
			const Transform3D &p_body_to_car,
			Object *p_livery,
			Object *p_catalog,
			bool p_build_visibility_masks = true,
			int p_visibility_mask_skip_layer = -1);
};

} // namespace godot

#endif
