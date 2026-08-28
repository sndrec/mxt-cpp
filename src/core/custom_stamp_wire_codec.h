#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class MxtCustomStampWireCodec : public RefCounted {
	GDCLASS(MxtCustomStampWireCodec, RefCounted)

	String last_error;

protected:
	static void _bind_methods();

public:
	PackedByteArray encode_manifest(const Array &manifest);
	Array decode_manifest(const PackedByteArray &bytes);
	PackedByteArray encode_blob(const Dictionary &blob);
	Dictionary decode_blob(const PackedByteArray &bytes);
	PackedByteArray encode_hashes(const Array &hashes);
	Array decode_hashes(const PackedByteArray &bytes);
	String get_last_error() const { return last_error; }
};

} // namespace godot
