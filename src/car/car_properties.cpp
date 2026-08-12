#include "car/car_properties.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

static constexpr uint8_t CAR_PROPS_MAGIC[8] = {'M', 'X', 'T', 'C', 'P', 'R', 'P', 0};
static constexpr size_t CAR_PROPS_HEADER_SIZE = 24;

static constexpr float DEFAULT_BASE_STATS[CAR_STAT_COUNT] = {
	1260.0f,       // weight_kg
	0.45f,         // acceleration
	0.1f,          // max_speed
	0.47f,         // grip_1
	0.7f,          // grip_2
	0.2f,          // grip_3
	0.12f,         // turn_tension
	0.4f,          // drift_accel
	145.0f,        // turn_movement
	20.0f,         // strafe_turn
	35.0f,         // strafe
	10.0f,         // turn_reaction
	0.02f,         // turn_decel
	0.01f,         // drag
	0.85f,         // body
	1.0f,          // camera_reorienting
	1.0f,          // camera_repositioning
	1.3f,          // track_collision
	2.4f,          // obstacle_collision
	100.0f,        // max_energy
	1.0f,          // boost_energy_use_rate
	1.0f,          // energy_recharge_rate
	1.0f,          // accel_press_grip_frames
	4.5486f,       // manual_turbo_gain
	9.0972f,       // dashplate_turbo_gain
	0.0f,          // jumpplate_turbo_gain
	0.2f,          // dashplate_turbo_heat_multiplier
	6.14061f,      // turbo_flat_loss_per_second
	0.05117175f,   // turbo_percent_loss_per_second
	3.0f,          // turbo_top_speed_effect
	1.5f,          // manual_boost_duration_seconds
	0.75f,         // dashplate_boost_duration_seconds
	1.5f,          // s_boost_base_speed_add_per_second
	2.0f,          // shift_boost_base_speed_add
	1.4f,          // shift_boost_velocity_multiplier
	0.005f,        // air_pitch_up_speed_loss_factor
	0.012f,        // air_glide_steering_speed_loss_factor
	1.0f,          // drive_target_speed_multiplier
	1.0f,          // acceleration_response_multiplier
	1.0f           // forward_thrust_multiplier
};

static constexpr const char *CAR_STAT_NAMES[CAR_STAT_COUNT] = {
	"weight_kg",
	"acceleration",
	"max_speed",
	"grip_1",
	"grip_2",
	"grip_3",
	"turn_tension",
	"drift_accel",
	"turn_movement",
	"strafe_turn",
	"strafe",
	"turn_reaction",
	"turn_decel",
	"drag",
	"body",
	"camera_reorienting",
	"camera_repositioning",
	"track_collision",
	"obstacle_collision",
	"max_energy",
	"boost_energy_use_rate",
	"energy_recharge_rate",
	"accel_press_grip_frames",
	"manual_turbo_gain",
	"dashplate_turbo_gain",
	"jumpplate_turbo_gain",
	"dashplate_turbo_heat_multiplier",
	"turbo_flat_loss_per_second",
	"turbo_percent_loss_per_second",
	"turbo_top_speed_effect",
	"manual_boost_duration_seconds",
	"dashplate_boost_duration_seconds",
	"s_boost_base_speed_add_per_second",
	"shift_boost_base_speed_add",
	"shift_boost_velocity_multiplier",
	"air_pitch_up_speed_loss_factor",
	"air_glide_steering_speed_loss_factor",
	"drive_target_speed_multiplier",
	"acceleration_response_multiplier",
	"forward_thrust_multiplier"
};

struct ByteReader {
	const uint8_t *data;
	size_t size;
	size_t cursor;

	bool read_u8(uint8_t &out) {
		if (cursor + 1 > size) return false;
		out = data[cursor++];
		return true;
	}

	bool read_u16(uint16_t &out) {
		if (cursor + 2 > size) return false;
		out = static_cast<uint16_t>(data[cursor]) |
			(static_cast<uint16_t>(data[cursor + 1]) << 8);
		cursor += 2;
		return true;
	}

	bool read_u32(uint32_t &out) {
		if (cursor + 4 > size) return false;
		out = static_cast<uint32_t>(data[cursor]) |
			(static_cast<uint32_t>(data[cursor + 1]) << 8) |
			(static_cast<uint32_t>(data[cursor + 2]) << 16) |
			(static_cast<uint32_t>(data[cursor + 3]) << 24);
		cursor += 4;
		return true;
	}

	bool read_u64(uint64_t &out) {
		uint32_t lo = 0;
		uint32_t hi = 0;
		if (!read_u32(lo) || !read_u32(hi)) return false;
		out = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
		return true;
	}

	bool read_f32(float &out) {
		uint32_t bits = 0;
		if (!read_u32(bits)) return false;
		std::memcpy(&out, &bits, sizeof(out));
		return true;
	}
};

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t i = 0; i < size; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) {
			const uint32_t mask = 0u - (crc & 1u);
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
		}
	}
	return ~crc;
}

static bool read_finite_float(ByteReader &reader, float &out)
{
	return reader.read_f32(out) && std::isfinite(out);
}

static bool sample_curve_record(ByteReader &reader, uint16_t key_count, float machine_setting, float &out_value)
{
	if (key_count == 0) return false;
	if (key_count == 1) return read_finite_float(reader, out_value);

	const size_t key_bytes = static_cast<size_t>(key_count) * 16u;
	if (reader.cursor + key_bytes > reader.size) return false;
	const uint8_t *keys = reader.data + reader.cursor;
	reader.cursor += key_bytes;

	float previous_time = -1.0f;
	for (uint16_t key = 0; key < key_count; ++key) {
		ByteReader key_reader{keys + static_cast<size_t>(key) * 16u, 16u, 0u};
		float time = 0.0f;
		float value = 0.0f;
		float tangent_in = 0.0f;
		float tangent_out = 0.0f;
		if (!read_finite_float(key_reader, time) ||
			!read_finite_float(key_reader, value) ||
			!read_finite_float(key_reader, tangent_in) ||
			!read_finite_float(key_reader, tangent_out)) {
			return false;
		}
		if (time < 0.0f || time > 1.0f || (key > 0 && time <= previous_time)) return false;
		previous_time = time;
	}

	ByteReader first_reader{keys, 16u, 0u};
	float first_time = 0.0f;
	float first_value = 0.0f;
	float unused = 0.0f;
	first_reader.read_f32(first_time);
	first_reader.read_f32(first_value);
	first_reader.read_f32(unused);
	first_reader.read_f32(unused);
	if (machine_setting <= first_time) {
		out_value = first_value;
		return true;
	}

	for (uint16_t key = 0; key + 1 < key_count; ++key) {
		ByteReader left_reader{keys + static_cast<size_t>(key) * 16u, 16u, 0u};
		ByteReader right_reader{keys + static_cast<size_t>(key + 1) * 16u, 16u, 0u};
		float t0 = 0.0f;
		float p0 = 0.0f;
		float tangent_in_0 = 0.0f;
		float tangent_out_0 = 0.0f;
		float t1 = 0.0f;
		float p1 = 0.0f;
		float tangent_in_1 = 0.0f;
		float tangent_out_1 = 0.0f;
		left_reader.read_f32(t0);
		left_reader.read_f32(p0);
		left_reader.read_f32(tangent_in_0);
		left_reader.read_f32(tangent_out_0);
		right_reader.read_f32(t1);
		right_reader.read_f32(p1);
		right_reader.read_f32(tangent_in_1);
		right_reader.read_f32(tangent_out_1);
		if (machine_setting > t1) continue;

		const float dt = t1 - t0;
		const float u = (machine_setting - t0) / dt;
		const float handle_scale = dt * (1.0f / 3.0f);
		const float h0 = p0 + handle_scale * tangent_out_0;
		const float h1 = p1 - handle_scale * tangent_in_1;
		const float one_minus_u = 1.0f - u;
		const float one_minus_u_2 = one_minus_u * one_minus_u;
		const float u_2 = u * u;
		out_value = p0 * one_minus_u_2 * one_minus_u +
			3.0f * h0 * one_minus_u_2 * u +
			3.0f * h1 * one_minus_u * u_2 +
			p1 * u_2 * u;
		return std::isfinite(out_value);
	}

	ByteReader last_reader{keys + static_cast<size_t>(key_count - 1) * 16u, 16u, 0u};
	float last_time = 0.0f;
	last_reader.read_f32(last_time);
	last_reader.read_f32(out_value);
	return std::isfinite(out_value);
}

static void set_error(godot::String &out_error, const char *message)
{
	out_error = godot::String(message);
}

} // namespace

PhysicsCarProperties::PhysicsCarProperties()
	: state_flags(0)
{
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		base_stats[stat] = DEFAULT_BASE_STATS[stat];
		s_boost_stats[stat] = DEFAULT_BASE_STATS[stat];
		for (uint8_t layer = 0; layer < CAR_MODIFIER_LAYER_COUNT; ++layer) {
			modifier_stats[layer][stat] = 1.0f;
		}
	}
	modifier_stats[CAR_MODIFIER_MANUAL_BOOST][CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND] = 0.5f;
	modifier_stats[CAR_MODIFIER_DASHPLATE_BOOST][CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND] = 0.5f;
	modifier_stats[CAR_MODIFIER_STACKED_BOOST][CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND] = 0.5f;
	modifier_stats[CAR_MODIFIER_MANUAL_BOOST][CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND] = 0.6f;
	modifier_stats[CAR_MODIFIER_DASHPLATE_BOOST][CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND] = 0.6f;
	modifier_stats[CAR_MODIFIER_STACKED_BOOST][CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND] = 0.6f;

	tilt_corners[0] = SimVec3(0.8f, 0.0f, -1.5f);
	tilt_corners[1] = SimVec3(-0.8f, 0.0f, -1.5f);
	tilt_corners[2] = SimVec3(1.1f, 0.0f, 1.7f);
	tilt_corners[3] = SimVec3(-1.1f, 0.0f, 1.7f);
	wall_corners[0] = SimVec3(1.0f, -0.1f, -1.7f);
	wall_corners[1] = SimVec3(-1.0f, -0.1f, -1.7f);
	wall_corners[2] = SimVec3(1.3f, -0.1f, 1.9f);
	wall_corners[3] = SimVec3(-1.3f, -0.1f, 1.9f);
}

bool PhysicsCarProperties::stat_supports_live_modifiers(CarStatId stat)
{
	return stat != CAR_STAT_WEIGHT_KG && stat != CAR_STAT_MAX_ENERGY;
}

const char *PhysicsCarProperties::stat_name(CarStatId stat)
{
	return stat < CAR_STAT_COUNT ? CAR_STAT_NAMES[stat] : "unknown";
}

CarStatModifierLayer classify_car_technique_modifier(
	bool genuinely_drifting,
	float strafe_input,
	float signed_slip,
	float &out_intensity)
{
	out_intensity = 0.0f;
	const float strafe_amount = std::min(std::abs(strafe_input), 1.0f);
	if (!genuinely_drifting || strafe_amount <= 0.0001f || std::abs(signed_slip) <= 0.0001f) {
		return CAR_MODIFIER_LAYER_COUNT;
	}
	const float parity = strafe_input * ((signed_slip > 0.0f) - (signed_slip < 0.0f));
	if (std::abs(parity) <= 0.0001f) {
		return CAR_MODIFIER_LAYER_COUNT;
	}
	out_intensity = strafe_amount;
	// Lateral slip normally points away from the direction the nose is turning.
	// Strafing with that slip is an MTS; strafing against it is a quick turn.
	return parity > 0.0f ? CAR_MODIFIER_MTS : CAR_MODIFIER_QUICKTURN;
}

CarStatModifierLayer classify_car_boost_modifier(
	bool manual_boost_active,
	bool dashplate_boost_active,
	bool s_boost_active)
{
	if (s_boost_active) {
		return dashplate_boost_active
			? CAR_MODIFIER_DASHPLATE_BOOST
			: CAR_MODIFIER_LAYER_COUNT;
	}
	if (manual_boost_active && dashplate_boost_active) {
		return CAR_MODIFIER_STACKED_BOOST;
	}
	if (manual_boost_active) {
		return CAR_MODIFIER_MANUAL_BOOST;
	}
	if (dashplate_boost_active) {
		return CAR_MODIFIER_DASHPLATE_BOOST;
	}
	return CAR_MODIFIER_NO_BOOST;
}

float evaluate_car_stat(
	const PhysicsCarProperties &properties,
	CarStatId stat,
	CarStatModifierLayer technique_layer,
	float technique_intensity,
	CarStatModifierLayer boost_layer,
	bool s_boost_active)
{
	if (stat >= CAR_STAT_COUNT) {
		return 0.0f;
	}
	const bool supports_modifiers = PhysicsCarProperties::stat_supports_live_modifiers(stat);
	float value = (s_boost_active && supports_modifiers)
		? properties.s_boost_stats[stat]
		: properties.base_stats[stat];
	if (!supports_modifiers) {
		return value;
	}
	if (technique_layer < CAR_MODIFIER_LAYER_COUNT && technique_intensity > 0.0f) {
		const float authored = properties.modifier_stats[technique_layer][stat];
		value *= 1.0f + (authored - 1.0f) * std::min(technique_intensity, 1.0f);
	}
	if (boost_layer < CAR_MODIFIER_LAYER_COUNT) {
		value *= properties.modifier_stats[boost_layer][stat];
	}
	return value;
}

bool PhysicsCarProperties::deserialize_and_sample(
	const godot::PackedByteArray &bytes,
	float machine_setting,
	PhysicsCarProperties &out_properties,
	godot::String &out_error)
{
	out_error = godot::String();
	if (bytes.size() < static_cast<int64_t>(CAR_PROPS_HEADER_SIZE)) {
		set_error(out_error, "car properties file is shorter than its header");
		return false;
	}
	const uint8_t *data = bytes.ptr();
	if (std::memcmp(data, CAR_PROPS_MAGIC, sizeof(CAR_PROPS_MAGIC)) != 0) {
		set_error(out_error, "car properties magic does not match the current format");
		return false;
	}

	ByteReader header{data + 8, CAR_PROPS_HEADER_SIZE - 8, 0};
	uint64_t fingerprint = 0;
	uint32_t payload_size = 0;
	uint32_t expected_crc = 0;
	if (!header.read_u64(fingerprint) || !header.read_u32(payload_size) || !header.read_u32(expected_crc)) {
		set_error(out_error, "car properties header is truncated");
		return false;
	}
	if (fingerprint != MXT_CAR_PROPS_SCHEMA_FINGERPRINT) {
		set_error(out_error, "car properties schema fingerprint does not match this build");
		return false;
	}
	if (payload_size != static_cast<uint32_t>(bytes.size() - static_cast<int64_t>(CAR_PROPS_HEADER_SIZE))) {
		set_error(out_error, "car properties payload size does not match the file length");
		return false;
	}
	const uint8_t *payload = data + CAR_PROPS_HEADER_SIZE;
	if (crc32_bytes(payload, payload_size) != expected_crc) {
		set_error(out_error, "car properties payload CRC does not match");
		return false;
	}

	PhysicsCarProperties parsed;
	bool curve_seen[CAR_CURVE_LAYER_COUNT][CAR_STAT_COUNT] = {};
	bool s_boost_seen[CAR_STAT_COUNT] = {};
	ByteReader reader{payload, payload_size, 0};
	if (!reader.read_u32(parsed.state_flags)) {
		set_error(out_error, "car properties fixed payload is truncated");
		return false;
	}
	for (int corner = 0; corner < 4; ++corner) {
		if (!read_finite_float(reader, parsed.tilt_corners[corner].x) ||
			!read_finite_float(reader, parsed.tilt_corners[corner].y) ||
			!read_finite_float(reader, parsed.tilt_corners[corner].z)) {
			set_error(out_error, "car properties tilt corners are malformed");
			return false;
		}
	}
	for (int corner = 0; corner < 4; ++corner) {
		if (!read_finite_float(reader, parsed.wall_corners[corner].x) ||
			!read_finite_float(reader, parsed.wall_corners[corner].y) ||
			!read_finite_float(reader, parsed.wall_corners[corner].z)) {
			set_error(out_error, "car properties wall corners are malformed");
			return false;
		}
	}

	uint16_t override_count = 0;
	if (!reader.read_u16(override_count)) {
		set_error(out_error, "car properties S-BOOST override table is missing");
		return false;
	}
	for (uint16_t override_index = 0; override_index < override_count; ++override_index) {
		uint16_t stat_raw = 0;
		float value = 0.0f;
		if (!reader.read_u16(stat_raw) || !read_finite_float(reader, value)) {
			set_error(out_error, "car properties S-BOOST override is truncated");
			return false;
		}
		if (stat_raw >= CAR_STAT_COUNT ||
			!stat_supports_live_modifiers(static_cast<CarStatId>(stat_raw)) ||
			s_boost_seen[stat_raw]) {
			set_error(out_error, "car properties S-BOOST override has an invalid or duplicate stat");
			return false;
		}
		s_boost_seen[stat_raw] = true;
		parsed.s_boost_stats[stat_raw] = value;
	}

	uint16_t curve_count = 0;
	if (!reader.read_u16(curve_count)) {
		set_error(out_error, "car properties curve table is missing");
		return false;
	}
	for (uint16_t curve_index = 0; curve_index < curve_count; ++curve_index) {
		uint16_t stat_raw = 0;
		uint8_t layer = 0;
		uint8_t reserved = 0;
		uint16_t key_count = 0;
		if (!reader.read_u16(stat_raw) || !reader.read_u8(layer) || !reader.read_u8(reserved) ||
			!reader.read_u16(key_count)) {
			set_error(out_error, "car properties curve header is truncated");
			return false;
		}
		if (stat_raw >= CAR_STAT_COUNT || layer >= CAR_CURVE_LAYER_COUNT || reserved != 0 ||
			curve_seen[layer][stat_raw]) {
			set_error(out_error, "car properties curve has an invalid or duplicate stat/layer");
			return false;
		}
		if (layer != CAR_CURVE_BASE &&
			!stat_supports_live_modifiers(static_cast<CarStatId>(stat_raw))) {
			set_error(out_error, "car properties curve applies a live modifier to weight or maximum energy");
			return false;
		}
		float sampled_value = 0.0f;
		if (!sample_curve_record(reader, key_count, machine_setting, sampled_value)) {
			set_error(out_error, "car properties curve key data is malformed");
			return false;
		}
		curve_seen[layer][stat_raw] = true;
		if (layer == CAR_CURVE_BASE) {
			parsed.base_stats[stat_raw] = sampled_value;
		} else {
			parsed.modifier_stats[layer - 1][stat_raw] = sampled_value;
		}
	}
	if (reader.cursor != reader.size) {
		set_error(out_error, "car properties payload contains trailing bytes");
		return false;
	}

	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		if (!curve_seen[CAR_CURVE_BASE][stat]) {
			set_error(out_error, "car properties file is missing a required base curve");
			return false;
		}
		if (!stat_supports_live_modifiers(static_cast<CarStatId>(stat))) continue;
		if (!s_boost_seen[stat]) {
			set_error(out_error, "car properties file is missing a required S-BOOST override");
			return false;
		}
		for (uint8_t layer = CAR_CURVE_MTS; layer < CAR_CURVE_LAYER_COUNT; ++layer) {
			if (!curve_seen[layer][stat]) {
				set_error(out_error, "car properties file is missing a required modifier curve");
				return false;
			}
		}
	}

	parsed.s_boost_stats[CAR_STAT_WEIGHT_KG] = parsed.base_stats[CAR_STAT_WEIGHT_KG];
	parsed.s_boost_stats[CAR_STAT_MAX_ENERGY] = parsed.base_stats[CAR_STAT_MAX_ENERGY];
	out_properties = parsed;
	return true;
}
