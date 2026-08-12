#ifndef MXT_TRACK_PACKAGE_BUILDER_H
#define MXT_TRACK_PACKAGE_BUILDER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class MxtTrackPackageBuilder : public RefCounted {
	GDCLASS(MxtTrackPackageBuilder, RefCounted)

protected:
	static void _bind_methods();

public:
	Dictionary build_package(
			const String &track_path,
			const String &visual_path,
			const String &metadata_path,
			const String &preview_path,
			const String &package_root,
			const String &title,
			const String &description,
			const String &author_name) const;
};

} // namespace godot

#endif // MXT_TRACK_PACKAGE_BUILDER_H
