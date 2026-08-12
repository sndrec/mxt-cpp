#include "content/content_archive.h"

#include "content/content_manifest.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace godot;

namespace mxt::content {
namespace {

static constexpr uint32_t ZIP_LOCAL_SIGNATURE = 0x04034b50u;
static constexpr uint32_t ZIP_CENTRAL_SIGNATURE = 0x02014b50u;
static constexpr uint32_t ZIP_END_SIGNATURE = 0x06054b50u;
static constexpr uint64_t ARCHIVE_MAX_BYTES = TRACK_PACKAGE_MAX_BYTES + 16u * 1024u * 1024u;
static constexpr uint64_t COMPRESSION_RATIO_THRESHOLD_BYTES = 1024u * 1024u;
static constexpr uint64_t MAX_COMPRESSION_RATIO = 200;

static void add_error(std::vector<String> &errors, const String &message)
{
	errors.push_back(message);
}

static uint16_t read_u16(const uint8_t *bytes)
{
	return static_cast<uint16_t>(bytes[0]) |
			(static_cast<uint16_t>(bytes[1]) << 8);
}

static uint32_t read_u32(const uint8_t *bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
			(static_cast<uint32_t>(bytes[1]) << 8) |
			(static_cast<uint32_t>(bytes[2]) << 16) |
			(static_cast<uint32_t>(bytes[3]) << 24);
}

static bool read_exact(const Ref<FileAccess> &file, uint64_t offset, int64_t count, PackedByteArray &out)
{
	file->seek(offset);
	out = file->get_buffer(count);
	return out.size() == count;
}

static bool is_allowed_archive_path(const String &path)
{
	return path == "manifest.json" || path == "preview.png" ||
			path == "vehicle/" || path == "track/" ||
			path == "vehicle/model.glb" || path == "vehicle/properties.mxt_car_props" ||
			path == "track/track.mxt_track" || path == "track/visual.glb" ||
			path == "track/metadata.json";
}

static uint64_t file_limit(const String &path)
{
	if (path == "manifest.json") return MANIFEST_MAX_BYTES;
	if (path == "preview.png") return PREVIEW_MAX_BYTES;
	if (path == "vehicle/model.glb") return 48u * 1024u * 1024u;
	if (path == "vehicle/properties.mxt_car_props") return 4u * 1024u * 1024u;
	if (path == "track/track.mxt_track") return 256u * 1024u * 1024u;
	if (path == "track/visual.glb") return 256u * 1024u * 1024u;
	if (path == "track/metadata.json") return 1u * 1024u * 1024u;
	return 0;
}

static std::string ascii_path(const String &path)
{
	const CharString encoded = path.ascii();
	return std::string(encoded.get_data(), static_cast<size_t>(encoded.length()));
}

} // namespace

bool inspect_mxtpkg_archive(
		const String &archive_path,
		std::vector<ArchiveEntry> &out_entries,
		std::vector<String> &out_errors)
{
	out_entries.clear();
	Ref<FileAccess> file = FileAccess::open(archive_path, FileAccess::READ);
	if (file.is_null()) {
		add_error(out_errors, "could not open .mxtpkg archive");
		return false;
	}
	const uint64_t archive_size = file->get_length();
	if (archive_size < 22 || archive_size > ARCHIVE_MAX_BYTES) {
		add_error(out_errors, ".mxtpkg archive size is outside the supported range");
		return false;
	}
	PackedByteArray end;
	if (!read_exact(file, archive_size - 22, 22, end) || read_u32(end.ptr()) != ZIP_END_SIGNATURE) {
		add_error(out_errors, ".mxtpkg must end with a canonical ZIP end record and no comment");
		return false;
	}
	const uint16_t disk_number = read_u16(end.ptr() + 4);
	const uint16_t central_disk = read_u16(end.ptr() + 6);
	const uint16_t entries_on_disk = read_u16(end.ptr() + 8);
	const uint16_t entry_count = read_u16(end.ptr() + 10);
	const uint32_t central_size = read_u32(end.ptr() + 12);
	const uint32_t central_offset = read_u32(end.ptr() + 16);
	const uint16_t comment_length = read_u16(end.ptr() + 20);
	if (disk_number != 0 || central_disk != 0 || entries_on_disk != entry_count ||
			entry_count == 0 || entry_count > PACKAGE_MAX_FILE_COUNT || comment_length != 0 ||
			central_offset == 0xffffffffu || central_size == 0xffffffffu ||
			static_cast<uint64_t>(central_offset) + central_size != archive_size - 22) {
		add_error(out_errors, "multi-disk, ZIP64, commented, empty, or malformed .mxtpkg archives are not supported");
		return false;
	}

	uint64_t cursor = central_offset;
	uint64_t total_uncompressed = 0;
	std::vector<std::string> folded_paths;
	for (uint16_t index = 0; index < entry_count; ++index) {
		PackedByteArray fixed;
		if (!read_exact(file, cursor, 46, fixed) || read_u32(fixed.ptr()) != ZIP_CENTRAL_SIGNATURE) {
			add_error(out_errors, ".mxtpkg central directory is truncated or malformed");
			return false;
		}
		const uint16_t version_made = read_u16(fixed.ptr() + 4);
		const uint16_t version_needed = read_u16(fixed.ptr() + 6);
		const uint16_t flags = read_u16(fixed.ptr() + 8);
		const uint16_t method = read_u16(fixed.ptr() + 10);
		const uint32_t crc32 = read_u32(fixed.ptr() + 16);
		const uint32_t compressed_size = read_u32(fixed.ptr() + 20);
		const uint32_t uncompressed_size = read_u32(fixed.ptr() + 24);
		const uint16_t name_length = read_u16(fixed.ptr() + 28);
		const uint16_t extra_length = read_u16(fixed.ptr() + 30);
		const uint16_t entry_comment_length = read_u16(fixed.ptr() + 32);
		const uint16_t entry_disk = read_u16(fixed.ptr() + 34);
		const uint32_t external_attributes = read_u32(fixed.ptr() + 38);
		const uint32_t local_offset = read_u32(fixed.ptr() + 42);
		if (version_needed > 20 || compressed_size == 0xffffffffu || uncompressed_size == 0xffffffffu ||
				local_offset == 0xffffffffu || name_length == 0 || name_length > 128 ||
				extra_length != 0 || entry_comment_length != 0 || entry_disk != 0 ||
				(method != 0 && method != 8) || (flags & ~0x0806u) != 0 || (flags & 0x0009u) != 0 ||
				(method == 0 && (flags & 0x0006u) != 0)) {
			add_error(out_errors, ".mxtpkg entry uses unsupported ZIP features");
			return false;
		}
		if ((version_made >> 8) == 3) {
			const uint32_t unix_mode = external_attributes >> 16;
			if ((unix_mode & 0170000u) == 0120000u) {
				add_error(out_errors, "symbolic-link entries are not allowed in .mxtpkg archives");
				return false;
			}
		}
		PackedByteArray name_bytes;
		if (!read_exact(file, cursor + 46, name_length, name_bytes)) {
			add_error(out_errors, ".mxtpkg entry name is truncated");
			return false;
		}
		for (int64_t i = 0; i < name_bytes.size(); ++i) {
			if (name_bytes[i] < 0x20 || name_bytes[i] > 0x7e || name_bytes[i] == '\\') {
				add_error(out_errors, ".mxtpkg revision 1 entry names must use canonical printable ASCII paths");
				return false;
			}
		}
		const String path = String::utf8(reinterpret_cast<const char *>(name_bytes.ptr()), name_bytes.size());
		if (!is_allowed_archive_path(path)) {
			add_error(out_errors, "undeclared or non-canonical .mxtpkg entry path '" + path + "'");
			return false;
		}
		if (path.ends_with("/") &&
				(uncompressed_size != 0 || crc32 != 0)) {
			add_error(out_errors, ".mxtpkg directory entries must decompress to empty data");
			return false;
		}
		const std::string folded = ascii_path(path.to_lower());
		if (std::find(folded_paths.begin(), folded_paths.end(), folded) != folded_paths.end()) {
			add_error(out_errors, ".mxtpkg contains a duplicate case-folded entry path");
			return false;
		}
		folded_paths.push_back(folded);
		const uint64_t limit = file_limit(path);
		if (uncompressed_size > limit ||
				(uncompressed_size > COMPRESSION_RATIO_THRESHOLD_BYTES &&
						(compressed_size == 0 || static_cast<uint64_t>(compressed_size) * MAX_COMPRESSION_RATIO < uncompressed_size))) {
			add_error(out_errors, ".mxtpkg entry size or compression ratio exceeds its limit: '" + path + "'");
			return false;
		}
		if (total_uncompressed > TRACK_PACKAGE_MAX_BYTES - uncompressed_size) {
			add_error(out_errors, ".mxtpkg total uncompressed size exceeds 512 MiB");
			return false;
		}
		total_uncompressed += uncompressed_size;
		out_entries.push_back({
			path, compressed_size, uncompressed_size, local_offset, 0, crc32, version_needed, method, flags
		});
		cursor += 46u + name_length;
	}
	if (cursor != static_cast<uint64_t>(central_offset) + central_size) {
		add_error(out_errors, ".mxtpkg central directory length does not match its entries");
		return false;
	}

	std::vector<size_t> local_order(out_entries.size());
	for (size_t i = 0; i < local_order.size(); ++i) local_order[i] = i;
	std::sort(local_order.begin(), local_order.end(), [&](size_t a, size_t b) {
		return out_entries[a].local_header_offset < out_entries[b].local_header_offset;
	});
	uint64_t expected_local_offset = 0;
	for (size_t ordered_index : local_order) {
		ArchiveEntry &entry = out_entries[ordered_index];
		if (entry.local_header_offset != expected_local_offset) {
			add_error(out_errors, ".mxtpkg contains overlapping entries, a preamble, or hidden local data");
			return false;
		}
		PackedByteArray local;
		if (!read_exact(file, entry.local_header_offset, 30, local) || read_u32(local.ptr()) != ZIP_LOCAL_SIGNATURE) {
			add_error(out_errors, ".mxtpkg local file header is truncated or malformed");
			return false;
		}
		const uint16_t version_needed = read_u16(local.ptr() + 4);
		const uint16_t flags = read_u16(local.ptr() + 6);
		const uint16_t method = read_u16(local.ptr() + 8);
		const uint32_t crc32 = read_u32(local.ptr() + 14);
		const uint32_t compressed_size = read_u32(local.ptr() + 18);
		const uint32_t uncompressed_size = read_u32(local.ptr() + 22);
		const uint16_t name_length = read_u16(local.ptr() + 26);
		const uint16_t extra_length = read_u16(local.ptr() + 28);
		const std::string expected_name = ascii_path(entry.path);
		if (version_needed != entry.version_needed || flags != entry.flags ||
				method != entry.compression_method || crc32 != entry.crc32 ||
				compressed_size != entry.compressed_size || uncompressed_size != entry.uncompressed_size ||
				name_length != expected_name.size() || extra_length != 0) {
			add_error(out_errors, ".mxtpkg local and central file headers disagree");
			return false;
		}
		PackedByteArray local_name;
		if (!read_exact(file, entry.local_header_offset + 30, name_length, local_name) ||
				std::memcmp(local_name.ptr(), expected_name.data(), expected_name.size()) != 0) {
			add_error(out_errors, ".mxtpkg local file name disagrees with its central entry");
			return false;
		}
		entry.data_offset = entry.local_header_offset + 30u + name_length;
		if (entry.data_offset > central_offset || entry.compressed_size > central_offset - entry.data_offset) {
			add_error(out_errors, ".mxtpkg compressed data crosses into its central directory");
			return false;
		}
		expected_local_offset = entry.data_offset + entry.compressed_size;
	}
	if (expected_local_offset != central_offset) {
		add_error(out_errors, ".mxtpkg contains hidden bytes before its central directory");
		return false;
	}
	return true;
}

} // namespace mxt::content
