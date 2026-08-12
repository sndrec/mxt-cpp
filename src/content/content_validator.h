#ifndef MXT_CONTENT_VALIDATOR_H
#define MXT_CONTENT_VALIDATOR_H

#include "content/content_manifest.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace mxt::content {

struct ValidatedPackage {
	ContentManifest manifest;
	godot::String root_path;
	godot::String package_digest;
	godot::String gameplay_digest;
	uint64_t total_bytes = 0;
};

bool validate_package_directory_internal(
		const godot::String &root_path,
		ValidatedPackage &out_package,
		std::vector<godot::String> &out_errors);

} // namespace mxt::content

namespace godot {

class MxtContentValidator : public RefCounted {
	GDCLASS(MxtContentValidator, RefCounted)

protected:
	static void _bind_methods();

public:
	Dictionary validate_manifest_bytes(const PackedByteArray &manifest_bytes) const;
	Dictionary validate_package_directory(const String &root_path) const;
};

} // namespace godot

#endif // MXT_CONTENT_VALIDATOR_H
