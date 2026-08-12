#ifndef MXT_CONTENT_PACKAGE_IO_H
#define MXT_CONTENT_PACKAGE_IO_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class MxtContentPackageIO : public RefCounted {
	GDCLASS(MxtContentPackageIO, RefCounted)

protected:
	static void _bind_methods();

public:
	Dictionary inspect_mxtpkg(const String &archive_path) const;
	Dictionary import_mxtpkg(const String &archive_path, const String &library_root) const;
	Dictionary export_mxtpkg(const String &package_root, const String &archive_path) const;
};

} // namespace godot

#endif // MXT_CONTENT_PACKAGE_IO_H
