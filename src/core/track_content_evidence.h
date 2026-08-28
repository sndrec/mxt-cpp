#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace godot {

class MxtTrackContentEvidence : public RefCounted {
	GDCLASS(MxtTrackContentEvidence, RefCounted)

private:
	struct Entry {
		String content_id;
		String gameplay_digest;
		String package_digest;
		String workshop_id;
	};

	std::vector<Entry> entries;
	String last_error;

protected:
	static void _bind_methods();

public:
	Ref<MxtTrackContentEvidence> copy() const;
	void clear();
	int32_t count() const { return static_cast<int32_t>(entries.size()); }
	bool append(const String &content_id, const String &gameplay_digest, const String &package_digest, const String &workshop_id);
	String get_content_id(int32_t index) const;
	String get_gameplay_digest(int32_t index) const;
	String get_package_digest(int32_t index) const;
	String get_workshop_id(int32_t index) const;
	int32_t find_content_id(const String &content_id) const;
	String get_last_error() const { return last_error; }

	PackedByteArray encode_wire() const;
	bool decode_wire(const PackedByteArray &bytes);
	Dictionary to_metadata_dictionary() const;
	bool load_metadata_dictionary(const Dictionary &value);
};

} // namespace godot
