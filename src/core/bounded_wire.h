#pragma once

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mxt_wire {

struct Writer {
	std::vector<uint8_t> bytes;

	void raw(const void *data, size_t count) {
		const uint8_t *source = static_cast<const uint8_t *>(data);
		bytes.insert(bytes.end(), source, source + count);
	}
	void u8(uint8_t value) { bytes.push_back(value); }
	void u16(uint16_t value) {
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}
	void u32(uint32_t value) {
		for (uint32_t shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
	}
	void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
	void f32(float value) {
		uint32_t bits = 0;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&bits, &value, sizeof(bits));
		u32(bits);
	}
	void i64(int64_t value) {
		const uint64_t bits = static_cast<uint64_t>(value);
		u32(static_cast<uint32_t>(bits));
		u32(static_cast<uint32_t>(bits >> 32));
	}
	void string(const godot::String &value) {
		const godot::CharString utf8 = value.utf8();
		const uint32_t size = static_cast<uint32_t>(utf8.length());
		u32(size);
		raw(utf8.get_data(), size);
	}
	godot::PackedByteArray packed() const {
		godot::PackedByteArray out;
		out.resize(static_cast<int64_t>(bytes.size()));
		if (!bytes.empty()) std::memcpy(out.ptrw(), bytes.data(), bytes.size());
		return out;
	}
};

struct Reader {
	const uint8_t *data = nullptr;
	size_t size = 0;
	size_t cursor = 0;

	explicit Reader(const godot::PackedByteArray &bytes)
		: data(bytes.ptr()), size(static_cast<size_t>(bytes.size())) {}

	bool raw(void *out, size_t count) {
		if (cursor + count > size) return false;
		std::memcpy(out, data + cursor, count);
		cursor += count;
		return true;
	}
	bool u8(uint8_t &out) { return raw(&out, sizeof(out)); }
	bool u16(uint16_t &out) {
		uint8_t bytes[2];
		if (!raw(bytes, sizeof(bytes))) return false;
		out = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
		return true;
	}
	bool u32(uint32_t &out) {
		uint8_t bytes[4];
		if (!raw(bytes, sizeof(bytes))) return false;
		out = static_cast<uint32_t>(bytes[0]) |
			(static_cast<uint32_t>(bytes[1]) << 8) |
			(static_cast<uint32_t>(bytes[2]) << 16) |
			(static_cast<uint32_t>(bytes[3]) << 24);
		return true;
	}
	bool i32(int32_t &out) {
		uint32_t bits = 0;
		if (!u32(bits)) return false;
		out = static_cast<int32_t>(bits);
		return true;
	}
	bool f32(float &out) {
		uint32_t bits = 0;
		if (!u32(bits)) return false;
		std::memcpy(&out, &bits, sizeof(out));
		return true;
	}
	bool i64(int64_t &out) {
		uint32_t low = 0;
		uint32_t high = 0;
		if (!u32(low) || !u32(high)) return false;
		out = static_cast<int64_t>(static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32));
		return true;
	}
	bool string(godot::String &out, uint32_t maximum_bytes) {
		uint32_t count = 0;
		if (!u32(count) || count > maximum_bytes || cursor + count > size) return false;
		out = godot::String::utf8(reinterpret_cast<const char *>(data + cursor), count);
		cursor += count;
		return true;
	}
	bool finished() const { return cursor == size; }
};

} // namespace mxt_wire
