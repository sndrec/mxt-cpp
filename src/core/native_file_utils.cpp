#include "core/native_file_utils.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace godot::mxt_file {

bool replace_atomically(const String &temporary, const String &destination) {
#ifdef _WIN32
	const Char16String temporary_utf16 = temporary.utf16();
	const Char16String destination_utf16 = destination.utf16();
	return MoveFileExW(
			reinterpret_cast<const wchar_t *>(temporary_utf16.get_data()),
			reinterpret_cast<const wchar_t *>(destination_utf16.get_data()),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	const CharString temporary_utf8 = temporary.utf8();
	const CharString destination_utf8 = destination.utf8();
	return std::rename(temporary_utf8.get_data(), destination_utf8.get_data()) == 0;
#endif
}

} // namespace godot::mxt_file
