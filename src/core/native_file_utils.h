#pragma once

#include <godot_cpp/variant/string.hpp>

namespace godot::mxt_file {

bool replace_atomically(const String &temporary, const String &destination);

} // namespace godot::mxt_file
