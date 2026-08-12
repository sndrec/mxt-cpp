#ifndef MXT_CONTENT_ARCHIVE_H
#define MXT_CONTENT_ARCHIVE_H

#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace mxt::content {

struct ArchiveEntry {
	godot::String path;
	uint64_t compressed_size = 0;
	uint64_t uncompressed_size = 0;
	uint64_t local_header_offset = 0;
	uint64_t data_offset = 0;
	uint32_t crc32 = 0;
	uint16_t version_needed = 0;
	uint16_t compression_method = 0;
	uint16_t flags = 0;
};

bool inspect_mxtpkg_archive(
		const godot::String &archive_path,
		std::vector<ArchiveEntry> &out_entries,
		std::vector<godot::String> &out_errors);

} // namespace mxt::content

#endif // MXT_CONTENT_ARCHIVE_H
