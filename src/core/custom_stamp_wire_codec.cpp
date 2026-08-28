#include "core/custom_stamp_wire_codec.h"

#include "core/bounded_wire.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>

#include <cstring>

using namespace godot;

namespace {

constexpr uint8_t MANIFEST_MAGIC[4] = {'C', 'S', 'M', 'F'};
constexpr uint8_t BLOB_MAGIC[4] = {'C', 'S', 'B', 'L'};
constexpr uint8_t HASH_MAGIC[4] = {'C', 'S', 'H', 'S'};
constexpr uint8_t WIRE_VERSION = 1;
constexpr uint32_t MAX_HASH_BYTES = 64;
constexpr uint32_t MAX_STAMPS = 64;
constexpr uint32_t MAX_COMPRESSED_BYTES = 24 * 1024;
constexpr uint32_t MAX_PALETTE_COLOURS = 16;

bool begin_read(mxt_wire::Reader &reader, const uint8_t (&magic)[4], uint8_t &version) {
	uint8_t actual[4];
	return reader.raw(actual, sizeof(actual)) && std::memcmp(actual, magic, sizeof(actual)) == 0 &&
		reader.u8(version) && version == WIRE_VERSION;
}

bool write_common(mxt_wire::Writer &writer, const Dictionary &entry, String &error) {
	const String hash = entry.get("hash", String());
	const int64_t width = entry.get("width", 0);
	const int64_t height = entry.get("height", 0);
	const int64_t bits_per_pixel = entry.get("bits_per_pixel", 0);
	const int64_t palette_id = entry.get("palette_id", 0);
	const int64_t uncompressed_size = entry.get("uncompressed_size", 0);
	if (hash.is_empty() || hash.utf8().length() > static_cast<int64_t>(MAX_HASH_BYTES) ||
		width <= 0 || width > 256 || height <= 0 || height > 256 ||
		(bits_per_pixel != 4 && bits_per_pixel != 8) || palette_id < 0 || palette_id > 255 ||
		uncompressed_size <= 0 || uncompressed_size > 32768) {
		error = "Invalid custom stamp wire fields.";
		return false;
	}
	const Array palette = entry.get("custom_palette", Array());
	if (palette.size() > static_cast<int64_t>(MAX_PALETTE_COLOURS)) {
		error = "Custom stamp palette exceeds wire limit.";
		return false;
	}
	writer.string(hash);
	writer.u16(static_cast<uint16_t>(width));
	writer.u16(static_cast<uint16_t>(height));
	writer.u8(static_cast<uint8_t>(bits_per_pixel));
	writer.u8(static_cast<uint8_t>(palette_id));
	writer.u32(static_cast<uint32_t>(uncompressed_size));
	writer.u8(static_cast<uint8_t>(palette.size()));
	for (int64_t i = 0; i < palette.size(); ++i) writer.u32(Color::html(String(palette[i])).to_rgba32());
	return true;
}

bool read_common(mxt_wire::Reader &reader, Dictionary &entry) {
	String hash;
	uint16_t width = 0;
	uint16_t height = 0;
	uint8_t bits_per_pixel = 0;
	uint8_t palette_id = 0;
	uint32_t uncompressed_size = 0;
	uint8_t palette_count = 0;
	if (!reader.string(hash, MAX_HASH_BYTES) || hash.is_empty() || !reader.u16(width) || !reader.u16(height) ||
		!reader.u8(bits_per_pixel) || !reader.u8(palette_id) || !reader.u32(uncompressed_size) || !reader.u8(palette_count) ||
		width == 0 || width > 256 || height == 0 || height > 256 ||
		(bits_per_pixel != 4 && bits_per_pixel != 8) || uncompressed_size == 0 || uncompressed_size > 32768 ||
		palette_count > MAX_PALETTE_COLOURS) return false;
	Array palette;
	for (uint8_t i = 0; i < palette_count; ++i) {
		uint32_t rgba = 0;
		if (!reader.u32(rgba)) return false;
		palette.append(Color::hex(rgba).to_html(true));
	}
	entry["hash"] = hash;
	entry["width"] = static_cast<int32_t>(width);
	entry["height"] = static_cast<int32_t>(height);
	entry["bits_per_pixel"] = static_cast<int32_t>(bits_per_pixel);
	entry["palette_id"] = static_cast<int32_t>(palette_id);
	entry["uncompressed_size"] = static_cast<int32_t>(uncompressed_size);
	if (!palette.is_empty()) entry["custom_palette"] = palette;
	return true;
}

} // namespace

void MxtCustomStampWireCodec::_bind_methods() {
	ClassDB::bind_method(D_METHOD("encode_manifest", "manifest"), &MxtCustomStampWireCodec::encode_manifest);
	ClassDB::bind_method(D_METHOD("decode_manifest", "bytes"), &MxtCustomStampWireCodec::decode_manifest);
	ClassDB::bind_method(D_METHOD("encode_blob", "blob"), &MxtCustomStampWireCodec::encode_blob);
	ClassDB::bind_method(D_METHOD("decode_blob", "bytes"), &MxtCustomStampWireCodec::decode_blob);
	ClassDB::bind_method(D_METHOD("encode_hashes", "hashes"), &MxtCustomStampWireCodec::encode_hashes);
	ClassDB::bind_method(D_METHOD("decode_hashes", "bytes"), &MxtCustomStampWireCodec::decode_hashes);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtCustomStampWireCodec::get_last_error);
}

PackedByteArray MxtCustomStampWireCodec::encode_manifest(const Array &manifest) {
	last_error = String();
	if (manifest.size() > static_cast<int64_t>(MAX_STAMPS)) {
		last_error = "Custom stamp manifest exceeds wire count limit.";
		return PackedByteArray();
	}
	mxt_wire::Writer writer;
	writer.raw(MANIFEST_MAGIC, sizeof(MANIFEST_MAGIC));
	writer.u8(WIRE_VERSION);
	writer.u8(static_cast<uint8_t>(manifest.size()));
	for (int64_t i = 0; i < manifest.size(); ++i) {
		if (manifest[i].get_type() != Variant::DICTIONARY) {
			last_error = "Custom stamp manifest contains a non-object entry.";
			return PackedByteArray();
		}
		const Dictionary entry = manifest[i];
		if (!write_common(writer, entry, last_error)) return PackedByteArray();
		const int64_t compressed_size = entry.get("compressed_size", 0);
		const Array rect = entry.get("rect_pixels", Array());
		const Array region = entry.get("region_size", Array());
		if (compressed_size <= 0 || compressed_size > MAX_COMPRESSED_BYTES || rect.size() != 4 || region.size() != 2) {
			last_error = "Invalid custom stamp manifest placement.";
			return PackedByteArray();
		}
		writer.u32(static_cast<uint32_t>(compressed_size));
		for (int j = 0; j < 4; ++j) writer.u16(static_cast<uint16_t>(static_cast<int64_t>(rect[j])));
		writer.u16(static_cast<uint16_t>(static_cast<int64_t>(region[0])));
		writer.u16(static_cast<uint16_t>(static_cast<int64_t>(region[1])));
		writer.u8(static_cast<bool>(entry.get("rect_rotated", false)) ? 1u : 0u);
	}
	return writer.packed();
}

Array MxtCustomStampWireCodec::decode_manifest(const PackedByteArray &bytes) {
	last_error = String();
	mxt_wire::Reader reader(bytes);
	uint8_t version = 0;
	uint8_t count = 0;
	if (!begin_read(reader, MANIFEST_MAGIC, version) || !reader.u8(count) || count > MAX_STAMPS) {
		last_error = "Malformed custom stamp manifest wire data.";
		return Array();
	}
	Array manifest;
	for (uint8_t i = 0; i < count; ++i) {
		Dictionary entry;
		uint32_t compressed_size = 0;
		uint16_t rect[4];
		uint16_t region[2];
		uint8_t rotated = 0;
		if (!read_common(reader, entry) || !reader.u32(compressed_size) || compressed_size == 0 || compressed_size > MAX_COMPRESSED_BYTES ||
			!reader.u16(rect[0]) || !reader.u16(rect[1]) || !reader.u16(rect[2]) || !reader.u16(rect[3]) ||
			!reader.u16(region[0]) || !reader.u16(region[1]) || !reader.u8(rotated) || rotated > 1) {
			last_error = "Malformed custom stamp manifest entry.";
			return Array();
		}
		entry["source"] = "custom";
		entry["compressed_size"] = static_cast<int32_t>(compressed_size);
		entry["rect_pixels"] = Array::make(rect[0], rect[1], rect[2], rect[3]);
		entry["region_size"] = Array::make(region[0], region[1]);
		entry["rect_rotated"] = rotated != 0;
		manifest.append(entry);
	}
	if (!reader.finished()) {
		last_error = "Custom stamp manifest has trailing wire data.";
		return Array();
	}
	return manifest;
}

PackedByteArray MxtCustomStampWireCodec::encode_blob(const Dictionary &blob) {
	last_error = String();
	mxt_wire::Writer writer;
	writer.raw(BLOB_MAGIC, sizeof(BLOB_MAGIC));
	writer.u8(WIRE_VERSION);
	if (!write_common(writer, blob, last_error)) return PackedByteArray();
	const PackedByteArray compressed = blob.get("compressed_indices", PackedByteArray());
	if (compressed.is_empty() || compressed.size() > MAX_COMPRESSED_BYTES) {
		last_error = "Custom stamp blob exceeds wire byte limit.";
		return PackedByteArray();
	}
	writer.u32(static_cast<uint32_t>(compressed.size()));
	writer.raw(compressed.ptr(), static_cast<size_t>(compressed.size()));
	return writer.packed();
}

Dictionary MxtCustomStampWireCodec::decode_blob(const PackedByteArray &bytes) {
	last_error = String();
	mxt_wire::Reader reader(bytes);
	uint8_t version = 0;
	Dictionary blob;
	uint32_t compressed_size = 0;
	if (!begin_read(reader, BLOB_MAGIC, version) || !read_common(reader, blob) || !reader.u32(compressed_size) ||
		compressed_size == 0 || compressed_size > MAX_COMPRESSED_BYTES || reader.cursor + compressed_size != reader.size) {
		last_error = "Malformed custom stamp blob wire data.";
		return Dictionary();
	}
	PackedByteArray compressed;
	compressed.resize(compressed_size);
	if (!reader.raw(compressed.ptrw(), compressed_size) || !reader.finished()) {
		last_error = "Malformed custom stamp blob payload.";
		return Dictionary();
	}
	blob["compressed_indices"] = compressed;
	return blob;
}

PackedByteArray MxtCustomStampWireCodec::encode_hashes(const Array &hashes) {
	last_error = String();
	if (hashes.size() > static_cast<int64_t>(MAX_STAMPS)) {
		last_error = "Custom stamp hash request exceeds wire count limit.";
		return PackedByteArray();
	}
	mxt_wire::Writer writer;
	writer.raw(HASH_MAGIC, sizeof(HASH_MAGIC));
	writer.u8(WIRE_VERSION);
	writer.u8(static_cast<uint8_t>(hashes.size()));
	for (int64_t i = 0; i < hashes.size(); ++i) {
		const String hash = hashes[i];
		if (hash.is_empty() || hash.utf8().length() > static_cast<int64_t>(MAX_HASH_BYTES)) {
			last_error = "Invalid custom stamp hash request.";
			return PackedByteArray();
		}
		writer.string(hash);
	}
	return writer.packed();
}

Array MxtCustomStampWireCodec::decode_hashes(const PackedByteArray &bytes) {
	last_error = String();
	mxt_wire::Reader reader(bytes);
	uint8_t version = 0;
	uint8_t count = 0;
	if (!begin_read(reader, HASH_MAGIC, version) || !reader.u8(count) || count > MAX_STAMPS) {
		last_error = "Malformed custom stamp hash request.";
		return Array();
	}
	Array hashes;
	for (uint8_t i = 0; i < count; ++i) {
		String hash;
		if (!reader.string(hash, MAX_HASH_BYTES) || hash.is_empty()) {
			last_error = "Malformed custom stamp hash entry.";
			return Array();
		}
		hashes.append(hash);
	}
	if (!reader.finished()) {
		last_error = "Custom stamp hash request has trailing wire data.";
		return Array();
	}
	return hashes;
}
