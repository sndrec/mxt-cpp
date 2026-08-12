#include "content/track_payload_validator.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace godot;

namespace mxt::content {
namespace {

static constexpr uint32_t TRACK_HEADER_SIZE = 24;
static constexpr uint32_t MAX_CHECKPOINTS = 1'000'000;
static constexpr uint32_t MAX_SEGMENTS = 65'536;
static constexpr uint32_t MAX_TRIGGERS = 262'144;
static constexpr uint32_t MAX_COLLISION_TRIANGLES = 2'000'000;
static constexpr uint32_t MAX_NEIGHBORS_PER_CHECKPOINT = 64;
static constexpr uint32_t MAX_CURVE_KEYS = 16'384;
static constexpr uint32_t MAX_MODULATIONS_PER_SEGMENT = 256;
static constexpr uint32_t MAX_EMBEDS_PER_SEGMENT = 256;

class ByteCursor {
private:
	const uint8_t *data;
	uint64_t length;
	uint64_t position = 0;

public:
	ByteCursor(const PackedByteArray &bytes) : data(bytes.ptr()), length(static_cast<uint64_t>(bytes.size())) {}

	uint64_t remaining() const { return length - position; }
	uint64_t offset() const { return position; }

	bool read_u32(uint32_t &out)
	{
		if (remaining() < 4) {
			return false;
		}
		out = static_cast<uint32_t>(data[position]) |
				(static_cast<uint32_t>(data[position + 1]) << 8) |
				(static_cast<uint32_t>(data[position + 2]) << 16) |
				(static_cast<uint32_t>(data[position + 3]) << 24);
		position += 4;
		return true;
	}

	bool read_f32(float &out)
	{
		uint32_t bits = 0;
		if (!read_u32(bits)) {
			return false;
		}
		std::memcpy(&out, &bits, sizeof(out));
		return std::isfinite(out);
	}

	bool read_bytes(uint8_t *out, uint32_t count)
	{
		if (remaining() < count) {
			return false;
		}
		std::memcpy(out, data + position, count);
		position += count;
		return true;
	}
};

static bool fail(String &error, const String &message, uint64_t offset)
{
	error = message + String(" at byte ") + String::num_uint64(offset);
	return false;
}

static bool read_curve(ByteCursor &cursor, String &error, uint32_t minimum_count = 0)
{
	uint32_t count = 0;
	if (!cursor.read_u32(count)) {
		return fail(error, "track curve header is truncated", cursor.offset());
	}
	if (count < minimum_count || count > MAX_CURVE_KEYS) {
		return fail(error, "track curve key count is outside the supported range", cursor.offset() - 4);
	}
	float previous_time = -INFINITY;
	for (uint32_t i = 0; i < count; ++i) {
		float time = 0.0f;
		float value = 0.0f;
		float tangent_in = 0.0f;
		float tangent_out = 0.0f;
		if (!cursor.read_f32(time) || !cursor.read_f32(value) ||
				!cursor.read_f32(tangent_in) || !cursor.read_f32(tangent_out)) {
			return fail(error, "track curve contains truncated or non-finite key data", cursor.offset());
		}
		if (time < 0.0f || time > 1.0f || (i > 0 && time <= previous_time)) {
			return fail(error, "track curve key times must increase strictly from 0 through 1", cursor.offset() - 16);
		}
		previous_time = time;
	}
	return true;
}

static bool read_transform_channel(
		ByteCursor &cursor,
		String &error,
		uint32_t expected_count,
		float *times,
		bool capture_times)
{
	uint32_t count = 0;
	if (!cursor.read_u32(count)) {
		return fail(error, "track transform channel header is truncated", cursor.offset());
	}
	if (count != expected_count) {
		return fail(error, "track transform channels have mismatched key counts", cursor.offset() - 4);
	}
	float previous_time = -INFINITY;
	for (uint32_t i = 0; i < count; ++i) {
		float time = 0.0f;
		float value = 0.0f;
		float tangent_in = 0.0f;
		float tangent_out = 0.0f;
		if (!cursor.read_f32(time) || !cursor.read_f32(value) ||
				!cursor.read_f32(tangent_in) || !cursor.read_f32(tangent_out)) {
			return fail(error, "track transform curve contains truncated or non-finite data", cursor.offset());
		}
		if (time < 0.0f || time > 1.0f || (i > 0 && time <= previous_time)) {
			return fail(error, "track transform key times must increase strictly from 0 through 1", cursor.offset() - 16);
		}
		if (capture_times) {
			times[i] = time;
		} else if (time != times[i]) {
			return fail(error, "track transform channels use different key times", cursor.offset() - 16);
		}
		previous_time = time;
	}
	return true;
}

static float length_squared(float x, float y, float z)
{
	return x * x + y * y + z * z;
}

static float determinant3(const float *basis)
{
	return basis[0] * (basis[4] * basis[8] - basis[5] * basis[7]) -
			basis[1] * (basis[3] * basis[8] - basis[5] * basis[6]) +
			basis[2] * (basis[3] * basis[7] - basis[4] * basis[6]);
}

} // namespace

bool validate_track_payload(const PackedByteArray &bytes, String &out_error)
{
	out_error = String();
	if (bytes.size() < TRACK_HEADER_SIZE) {
		out_error = "track payload is shorter than its header";
		return false;
	}
	ByteCursor cursor(bytes);
	uint32_t header_size = 0;
	uint8_t version[4] = {};
	uint32_t checkpoint_count = 0;
	uint32_t segment_count = 0;
	uint32_t trigger_count = 0;
	uint32_t triangle_count = 0;
	if (!cursor.read_u32(header_size) || !cursor.read_bytes(version, 4) ||
			!cursor.read_u32(checkpoint_count) || !cursor.read_u32(segment_count) ||
			!cursor.read_u32(trigger_count) || !cursor.read_u32(triangle_count)) {
		out_error = "track header is truncated";
		return false;
	}
	if (header_size != TRACK_HEADER_SIZE || std::memcmp(version, "v0.9", 4) != 0) {
		out_error = "track header must use the current v0.9 format";
		return false;
	}
	if (checkpoint_count == 0 || checkpoint_count > MAX_CHECKPOINTS) {
		out_error = "track checkpoint count is outside the supported range";
		return false;
	}
	if (segment_count == 0 || segment_count > MAX_SEGMENTS) {
		out_error = "track segment count is outside the supported range";
		return false;
	}
	if (trigger_count > MAX_TRIGGERS || triangle_count > MAX_COLLISION_TRIANGLES) {
		out_error = "track trigger or collision-triangle count exceeds its limit";
		return false;
	}

	for (uint32_t checkpoint = 0; checkpoint < checkpoint_count; ++checkpoint) {
		float values[31] = {};
		for (uint32_t i = 0; i < 31; ++i) {
			if (!cursor.read_f32(values[i]) || std::fabs(values[i]) > 10'000'000.0f) {
				return fail(out_error, "track checkpoint contains truncated or non-finite data", cursor.offset());
			}
		}
		if (std::fabs(determinant3(values + 6)) <= 1.0e-6f ||
				std::fabs(determinant3(values + 15)) <= 1.0e-6f ||
				values[24] <= 0.0f || values[25] <= 0.0f || values[26] <= 0.0f || values[27] <= 0.0f ||
				values[28] < 0.0f || values[29] > 1.0f || values[28] > values[29] || values[30] <= 0.0f) {
			return fail(out_error, "track checkpoint has a degenerate basis, radius, interval, or distance", cursor.offset() - 124);
		}
		uint32_t segment = 0;
		if (!cursor.read_u32(segment) || segment >= segment_count) {
			return fail(out_error, "track checkpoint references an invalid segment", cursor.offset());
		}
		float planes[8] = {};
		for (uint32_t i = 0; i < 8; ++i) {
			if (!cursor.read_f32(planes[i]) || std::fabs(planes[i]) > 10'000'000.0f) {
				return fail(out_error, "track checkpoint plane contains truncated or non-finite data", cursor.offset());
			}
		}
		if (length_squared(planes[0], planes[1], planes[2]) <= 1.0e-8f ||
				length_squared(planes[4], planes[5], planes[6]) <= 1.0e-8f) {
			return fail(out_error, "track checkpoint plane has a zero normal", cursor.offset() - 32);
		}
		uint32_t neighbor_count = 0;
		if (!cursor.read_u32(neighbor_count) || neighbor_count > MAX_NEIGHBORS_PER_CHECKPOINT) {
			return fail(out_error, "track checkpoint neighbor count exceeds its limit", cursor.offset());
		}
		uint32_t neighbors[MAX_NEIGHBORS_PER_CHECKPOINT] = {};
		for (uint32_t i = 0; i < neighbor_count; ++i) {
			uint32_t neighbor = 0;
			if (!cursor.read_u32(neighbor) || neighbor >= checkpoint_count || neighbor == checkpoint) {
				return fail(out_error, "track checkpoint has an invalid neighbor", cursor.offset());
			}
			for (uint32_t prior = 0; prior < i; ++prior) {
				if (neighbors[prior] == neighbor) {
					return fail(out_error, "track checkpoint contains a duplicate neighbor", cursor.offset() - 4);
				}
			}
			neighbors[i] = neighbor;
		}
	}

	for (uint32_t segment = 0; segment < segment_count; ++segment) {
		uint32_t segment_index = 0;
		uint32_t road_type = 0;
		uint32_t analytic_collision = 0;
		if (!cursor.read_u32(segment_index) || !cursor.read_u32(road_type) || !cursor.read_u32(analytic_collision)) {
			return fail(out_error, "track segment header is truncated", cursor.offset());
		}
		if (segment_index != segment || road_type > 7 || analytic_collision > 1) {
			return fail(out_error, "track segment header contains an invalid value", cursor.offset() - 12);
		}
		uint32_t shape_curve_count = 0;
		if (road_type == 5 || road_type == 6) {
			shape_curve_count += 3;
		}
		if (road_type == 2 || road_type == 4 || road_type == 6) {
			shape_curve_count += 1;
		}
		if (road_type == 6) {
			shape_curve_count += 1;
		}
		for (uint32_t i = 0; i < shape_curve_count; ++i) {
			if (!read_curve(cursor, out_error, 1)) {
				return false;
			}
		}

		uint32_t modulation_count = 0;
		if (!cursor.read_u32(modulation_count) || modulation_count > MAX_MODULATIONS_PER_SEGMENT) {
			return fail(out_error, "track modulation count exceeds its limit", cursor.offset());
		}
		for (uint32_t i = 0; i < modulation_count * 2; ++i) {
			if (!read_curve(cursor, out_error, 1)) {
				return false;
			}
		}

		uint32_t embed_count = 0;
		if (!cursor.read_u32(embed_count) || embed_count > MAX_EMBEDS_PER_SEGMENT) {
			return fail(out_error, "track embed count exceeds its limit", cursor.offset());
		}
		for (uint32_t embed = 0; embed < embed_count; ++embed) {
			float start = 0.0f;
			float end = 0.0f;
			uint32_t type = 0;
			if (!cursor.read_f32(start) || !cursor.read_f32(end) || !cursor.read_u32(type)) {
				return fail(out_error, "track embed header is truncated or non-finite", cursor.offset());
			}
			if (start < 0.0f || end > 1.0f || start > end || type > 4) {
				return fail(out_error, "track embed has invalid bounds or type", cursor.offset() - 12);
			}
			if (!read_curve(cursor, out_error, 1) || !read_curve(cursor, out_error, 1)) {
				return false;
			}
		}

		uint32_t transform_key_count = 0;
		const uint64_t transform_header_offset = cursor.offset();
		if (!cursor.read_u32(transform_key_count)) {
			return fail(out_error, "track transform curve header is truncated", cursor.offset());
		}
		if (transform_key_count < 2 || transform_key_count > MAX_CURVE_KEYS) {
			return fail(out_error, "track transform curve key count is outside the supported range", transform_header_offset);
		}
		// Rewind is deliberately avoided: parse the first channel body here, then the
		// remaining channel headers/bodies through the shared channel routine.
		std::vector<float> transform_times(transform_key_count);
		float previous_time = -INFINITY;
		for (uint32_t i = 0; i < transform_key_count; ++i) {
			float time = 0.0f;
			float value = 0.0f;
			float tangent_in = 0.0f;
			float tangent_out = 0.0f;
			if (!cursor.read_f32(time) || !cursor.read_f32(value) ||
					!cursor.read_f32(tangent_in) || !cursor.read_f32(tangent_out)) {
				return fail(out_error, "track transform curve contains truncated or non-finite data", cursor.offset());
			}
			if (time < 0.0f || time > 1.0f || (i > 0 && time <= previous_time)) {
				return fail(out_error, "track transform key times must increase strictly from 0 through 1", cursor.offset() - 16);
			}
			transform_times[i] = time;
			previous_time = time;
		}
		for (uint32_t channel = 1; channel < 15; ++channel) {
			if (!read_transform_channel(cursor, out_error, transform_key_count, transform_times.data(), false)) {
				return false;
			}
		}
		float rail_values[6] = {};
		for (uint32_t i = 0; i < 6; ++i) {
			if (!cursor.read_f32(rail_values[i])) {
				return fail(out_error, "track rail data is truncated or non-finite", cursor.offset());
			}
		}
		if (rail_values[0] < 0.0f || rail_values[1] < 0.0f ||
				rail_values[2] < 0.0f || rail_values[2] > 1.0f ||
				rail_values[3] < rail_values[2] || rail_values[3] > 1.0f ||
				rail_values[4] < 0.0f || rail_values[4] > 1.0f ||
				rail_values[5] < rail_values[4] || rail_values[5] > 1.0f) {
			return fail(out_error, "track rail heights or intervals are invalid", cursor.offset() - 24);
		}
	}

	for (uint32_t trigger = 0; trigger < trigger_count; ++trigger) {
		uint32_t type = 0;
		uint32_t segment = 0;
		uint32_t checkpoint = 0;
		if (!cursor.read_u32(type) || !cursor.read_u32(segment) || !cursor.read_u32(checkpoint) ||
				type > 2 || segment >= segment_count || checkpoint >= checkpoint_count) {
			return fail(out_error, "track trigger header contains an invalid value", cursor.offset());
		}
		float values[15] = {};
		for (float &value : values) {
			if (!cursor.read_f32(value) || std::fabs(value) > 10'000'000.0f) {
				return fail(out_error, "track trigger transform is truncated or non-finite", cursor.offset());
			}
		}
		if (values[12] <= 0.0f || values[13] <= 0.0f || values[14] <= 0.0f) {
			return fail(out_error, "track trigger extents must be positive", cursor.offset() - 12);
		}
	}

	for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
		uint32_t terrain = 0;
		if (!cursor.read_u32(terrain) || (terrain & ~0x0fffu) != 0) {
			return fail(out_error, "track collision triangle has invalid terrain flags", cursor.offset());
		}
		float value[18] = {};
		for (float &component : value) {
			if (!cursor.read_f32(component) || std::fabs(component) > 10'000'000.0f) {
				return fail(out_error, "track collision triangle is truncated or non-finite", cursor.offset());
			}
		}
		const float edge0_x = value[3] - value[0];
		const float edge0_y = value[4] - value[1];
		const float edge0_z = value[5] - value[2];
		const float edge1_x = value[6] - value[0];
		const float edge1_y = value[7] - value[1];
		const float edge1_z = value[8] - value[2];
		const float cross_x = edge0_y * edge1_z - edge0_z * edge1_y;
		const float cross_y = edge0_z * edge1_x - edge0_x * edge1_z;
		const float cross_z = edge0_x * edge1_y - edge0_y * edge1_x;
		if (length_squared(cross_x, cross_y, cross_z) <= 1.0e-8f ||
				length_squared(value[9], value[10], value[11]) <= 1.0e-8f ||
				length_squared(value[12], value[13], value[14]) <= 1.0e-8f ||
				length_squared(value[15], value[16], value[17]) <= 1.0e-8f) {
			return fail(out_error, "track collision triangle is degenerate or has a zero normal", cursor.offset() - 72);
		}
	}

	if (cursor.remaining() != 0) {
		return fail(out_error, "track payload contains trailing bytes", cursor.offset());
	}
	return true;
}

} // namespace mxt::content
