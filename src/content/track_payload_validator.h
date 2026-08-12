#ifndef MXT_TRACK_PAYLOAD_VALIDATOR_H
#define MXT_TRACK_PAYLOAD_VALIDATOR_H

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace mxt::content {

bool validate_track_payload(
		const godot::PackedByteArray &bytes,
		godot::String &out_error);

} // namespace mxt::content

#endif // MXT_TRACK_PAYLOAD_VALIDATOR_H
