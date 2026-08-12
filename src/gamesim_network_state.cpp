#include "gamesim_internal.h"

#include "godot_cpp/variant/utility_functions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

using namespace godot;
namespace {
constexpr uint32_t MXT_NET_STATE_MAGIC = 0x5354584du; // "MXTS", little-endian.
constexpr uint16_t MXT_NET_STATE_FLAG_EXACT_SIM_FLOATS = 1u << 0;

static uint16_t mxt_float_to_half_bits(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	const uint32_t sign = (bits >> 16) & 0x8000u;
	int32_t exp = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
	uint32_t mant = bits & 0x7fffffu;
	if (exp <= 0) {
		if (exp < -10) {
			return static_cast<uint16_t>(sign);
		}
		mant |= 0x800000u;
		const uint32_t shift = static_cast<uint32_t>(14 - exp);
		uint32_t half_mant = mant >> shift;
		if ((mant >> (shift - 1)) & 1u) {
			half_mant += 1u;
		}
		return static_cast<uint16_t>(sign | half_mant);
	}
	if (exp >= 31) {
		return static_cast<uint16_t>(sign | 0x7c00u);
	}
	uint32_t half = sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13);
	if (mant & 0x1000u) {
		half += 1u;
	}
	return static_cast<uint16_t>(half);
}

static float mxt_half_bits_to_float(uint16_t half) {
	const uint32_t sign = (static_cast<uint32_t>(half & 0x8000u)) << 16;
	uint32_t exp = (half >> 10) & 0x1fu;
	uint32_t mant = half & 0x03ffu;
	uint32_t bits = 0;
	if (exp == 0) {
		if (mant == 0) {
			bits = sign;
		} else {
			exp = 1;
			while ((mant & 0x0400u) == 0) {
				mant <<= 1;
				--exp;
			}
			mant &= 0x03ffu;
			bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
		}
	} else if (exp == 31) {
		bits = sign | 0x7f800000u | (mant << 13);
	} else {
		bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
	}
	float value = 0.0f;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

static bool mxt_net_checkpoint_surface_at(RaceTrack* track, int cp_idx, float fraction, SimTransform& out) {
	if (!track || cp_idx < 0 || cp_idx >= track->num_checkpoints) {
		return false;
	}
	const CollisionCheckpoint& cp = track->checkpoints[cp_idx];
	if (cp.road_segment < 0 || cp.road_segment >= track->num_segments || !track->segments[cp.road_segment].road_shape) {
		return false;
	}
	const float t_y = cp.t_start + (cp.t_end - cp.t_start) * std::clamp(fraction, 0.0f, 1.0f);
	track->segments[cp.road_segment].road_shape->get_oriented_transform_at_time(out, SimVec2(0.0f, t_y));
	out.basis.orthonormalize();
	return true;
}

static SimTransform mxt_net_checkpoint_surface_or_identity(RaceTrack* track, int cp_idx, float fraction) {
	SimTransform surface;
	if (!mxt_net_checkpoint_surface_at(track, cp_idx, fraction, surface)) {
		surface = SimTransform();
	}
	return surface;
}

struct NetStateWriter {
	std::vector<uint8_t> data;

	template <typename T>
	void write_pod(const T& value) {
		const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
		data.insert(data.end(), src, src + sizeof(T));
	}

	void write_bytes(const void* src, size_t size) {
		if (!src || size == 0) {
			return;
		}
		const uint8_t* bytes = reinterpret_cast<const uint8_t*>(src);
		data.insert(data.end(), bytes, bytes + size);
	}

	void write_vec3(const SimVec3& v) {
		write_pod(v.x);
		write_pod(v.y);
		write_pod(v.z);
	}

	void write_vec3_half(const SimVec3& v) {
		write_float16(v.x);
		write_float16(v.y);
		write_float16(v.z);
	}

	void write_vec2(const SimVec2& v) {
		write_pod(v.x);
		write_pod(v.y);
	}

	void write_quat(const SimQuat& q) {
		write_pod(q.x);
		write_pod(q.y);
		write_pod(q.z);
		write_pod(q.w);
	}

	void write_quat_i16(const SimQuat& q) {
		const float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		const float inv_len = len_sq > 0.000001f ? 1.0f / std::sqrt(len_sq) : 1.0f;
		const auto pack = [&](float v) -> int16_t {
			const float clamped = std::clamp(v * inv_len, -1.0f, 1.0f);
			return static_cast<int16_t>(std::lround(clamped * 32767.0f));
		};
		write_pod(pack(q.x));
		write_pod(pack(q.y));
		write_pod(pack(q.z));
		write_pod(pack(q.w));
	}

	void write_float16(float value) {
		write_pod(mxt_float_to_half_bits(value));
	}

	void write_basis(const SimBasis& b) {
		for (int col = 0; col < 3; ++col) {
			write_vec3(b.get_column(col));
		}
	}

	void write_transform(const SimTransform& t) {
		for (int col = 0; col < 3; ++col) {
			write_vec3(t.basis.get_column(col));
		}
		write_vec3(t.origin);
	}

	int size() const {
		return static_cast<int>(data.size());
	}

	godot::PackedByteArray to_packed_byte_array() const {
		godot::PackedByteArray out;
		out.resize(static_cast<int>(data.size()));
		if (!data.empty()) {
			std::memcpy(out.ptrw(), data.data(), data.size());
		}
		return out;
	}
};

struct NetStateReader {
	const uint8_t* data = nullptr;
	int size = 0;
	int pos = 0;

	explicit NetStateReader(const godot::PackedByteArray& bytes) {
		data = bytes.ptr();
		size = bytes.size();
	}

	template <typename T>
	bool read_pod(T& out) {
		if (pos < 0 || pos + static_cast<int>(sizeof(T)) > size) {
			return false;
		}
		std::memcpy(&out, data + pos, sizeof(T));
		pos += static_cast<int>(sizeof(T));
		return true;
	}

	bool read_bytes(void* dst, size_t byte_count) {
		if (byte_count == 0) {
			return true;
		}
		if (!dst || pos < 0 || pos + static_cast<int>(byte_count) > size) {
			return false;
		}
		std::memcpy(dst, data + pos, byte_count);
		pos += static_cast<int>(byte_count);
		return true;
	}

	bool read_vec3(SimVec3& out) {
		return read_pod(out.x) && read_pod(out.y) && read_pod(out.z);
	}

	bool read_vec3_half(SimVec3& out) {
		return read_float16(out.x) && read_float16(out.y) && read_float16(out.z);
	}

	bool read_vec2(SimVec2& out) {
		return read_pod(out.x) && read_pod(out.y);
	}

	bool read_quat(SimQuat& out) {
		return read_pod(out.x) && read_pod(out.y) && read_pod(out.z) && read_pod(out.w);
	}

	bool read_quat_i16(SimQuat& out) {
		int16_t x = 0;
		int16_t y = 0;
		int16_t z = 0;
		int16_t w = 0;
		if (!read_pod(x) || !read_pod(y) || !read_pod(z) || !read_pod(w)) {
			return false;
		}
		out.x = static_cast<float>(x) / 32767.0f;
		out.y = static_cast<float>(y) / 32767.0f;
		out.z = static_cast<float>(z) / 32767.0f;
		out.w = static_cast<float>(w) / 32767.0f;
		const float len_sq = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
		if (len_sq > 0.000001f) {
			const float inv_len = 1.0f / std::sqrt(len_sq);
			out.x *= inv_len;
			out.y *= inv_len;
			out.z *= inv_len;
			out.w *= inv_len;
		} else {
			out = SimQuat();
		}
		return true;
	}

	bool read_float16(float& out) {
		uint16_t bits = 0;
		if (!read_pod(bits)) {
			return false;
		}
		out = mxt_half_bits_to_float(bits);
		return true;
	}

	bool read_basis(SimBasis& out) {
		SimVec3 c0, c1, c2;
		if (!read_vec3(c0) || !read_vec3(c1) || !read_vec3(c2)) {
			return false;
		}
		out.set_column(0, c0);
		out.set_column(1, c1);
		out.set_column(2, c2);
		return true;
	}

	bool read_transform(SimTransform& out) {
		SimVec3 c0, c1, c2;
		if (!read_vec3(c0) || !read_vec3(c1) || !read_vec3(c2) || !read_vec3(out.origin)) {
			return false;
		}
		out.basis.set_column(0, c0);
		out.basis.set_column(1, c1);
		out.basis.set_column(2, c2);
		return true;
	}
};
}

#define MXT_NET_CAR_SCALAR_FIELDS(X) \
	X(uint32_t, machine_state) \
	X(uint16_t, boost_frames_manual) \
	X(uint16_t, boost_frames_dash) \
	X(uint16_t, boost_duration_manual_frames) \
	X(uint16_t, boost_duration_dash_frames) \
	X(uint32_t, last_hit_tick) \
	X(uint32_t, last_machine_hit_tick) \
	X(uint8_t, spinattack_direction) \
	X(uint8_t, brake_timer) \
	X(uint32_t, terrain_state) \
	X(uint32_t, frames_since_start) \
	X(uint8_t, frames_since_start_2) \
	X(uint8_t, air_time) \
	X(uint8_t, frames_since_death) \
	X(uint32_t, state_2) \
	X(uint32_t, level_start_time) \
	X(uint16_t, some_breakdown_int) \
	X(uint8_t, breakdown_frame_counter) \
	X(uint16_t, restore_wait_frames) \
	X(uint16_t, restore_move_frames) \
	X(uint16_t, current_checkpoint) \
	X(int16_t, current_collision_checkpoint) \
	X(uint16_t, last_ground_checkpoint) \
	X(uint8_t, lap) \
	X(uint8_t, broken_lap_rollback_lap) \
	X(uint8_t, rail_collision_timer) \
	X(uint8_t, grip_frames_from_accel_press) \
	X(uint8_t, side_attack_delay) \
	X(uint16_t, attack_cooldown_frames) \
	X(uint8_t, car_hit_invincibility) \
	X(int8_t, drift_sign) \
	X(uint8_t, restore_state) \
	X(uint16_t, s_boost_charge) \
	X(uint16_t, s_boost_charge_max) \
	X(uint16_t, s_boost_frames_remaining) \
	X(uint8_t, s_boost_emit_frame_accumulator) \
	X(uint8_t, s_boost_pending_spark_spawns) \
	X(uint8_t, pending_super_sparks) \
	X(bool, has_last_hit_tick) \
	X(bool, has_last_machine_hit_tick) \
	X(bool, machine_crashed) \
	X(bool, s_boost_active) \
	X(bool, broken_lap_rollback_pending) \
	X(float, base_speed) \
	X(float, boost_turbo) \
	X(float, pending_dashplate_heat) \
	X(float, pending_dashplate_heat_reward_scale) \
	X(float, race_start_charge) \
	X(float, air_tilt) \
	X(float, energy) \
	X(float, ko_energy_bonus) \
	X(float, spinattack_angle) \
	X(float, spinattack_decrement) \
	X(float, height_above_track) \
	X(float, last_ground_distance) \
	X(float, previous_lap_distance) \
	X(float, checkpoint_fraction) \
	X(float, input_accel) \
	X(float, last_machine_hit_sfx_strength) \
	X(float, drift_ramp)

#define MXT_NET_CAR_VEC3_FIELDS(X) \
	X(velocity)

#define MXT_NET_CAR_VEC3_HALF_FIELDS(X) \
	X(track_surface_normal) \
	X(knockback_velocity) \
	X(velocity_angular)

#define MXT_NET_CAR_TRANSFORM_FIELDS(X)

#define MXT_NET_TILT_SCALAR_FIELDS(X) \
	X(float, force) \
	X(uint8_t, state)

#define MXT_NET_TILT_VEC3_FIELDS(X)

#define MXT_NET_WALL_VEC3_FIELDS(X)

godot::PackedByteArray GameSim::serialize_network_state(int target_tick) const {
	NetStateWriter writer;
	NetworkStateSizeStats stats;
	stats.car_count = num_cars;
	stats.bumper_count = bumper_count;
	constexpr uint16_t net_state_flags = MXT_NET_STATE_FLAG_EXACT_SIM_FLOATS;
	int section_start = writer.size();
	writer.write_pod(MXT_NET_STATE_MAGIC);
	writer.write_pod(net_state_flags);
	writer.write_pod(static_cast<int32_t>(target_tick));
	writer.write_pod(static_cast<int32_t>(num_cars));
	writer.write_pod(static_cast<int32_t>(bumper_count));
	writer.write_pod(bumper_track_seed);
	writer.write_pod(bumper_scheduler_lap);
	writer.write_pod(bumper_next_sequence);
	stats.header += writer.size() - section_start;
	section_start = writer.size();
	for (int i = 0; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		const BumperState& state = bumper_states[i];
		if (state.active) {
			++stats.active_bumper_count;
		}
		writer.write_pod(state.active);
		writer.write_pod(state.spawn_lap);
		writer.write_pod(state.next_sequence);
		writer.write_pod(state.target_lane);
	}
	stats.bumper_meta += writer.size() - section_start;

	const int trigger_count = current_track ? current_track->num_trigger_colliders : 0;
	stats.trigger_count = trigger_count;
	section_start = writer.size();
	writer.write_pod(static_cast<int32_t>(trigger_count));
	uint16_t active_spark_count = 0;
	if (super_spark_state) {
		for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
			if (super_spark_state->sparks[i].active) {
				++active_spark_count;
			}
		}
		writer.write_pod(super_spark_state->cursor);
		writer.write_pod(super_spark_state->rng_state);
		writer.write_pod(super_spark_state->placement_timer);
		writer.write_pod(active_spark_count);
		for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
			const SuperSpark& spark = super_spark_state->sparks[i];
			if (!spark.active) {
				continue;
			}
			const uint8_t spark_flags = spark.collectable ? 1u : 0u;
			writer.write_pod(i);
			writer.write_pod(spark_flags);
			writer.write_pod(spark.checkpoint);
			writer.write_vec3(spark.final_position);
			if (!spark.collectable) {
				writer.write_pod(spark.animation_frame);
				writer.write_vec3(spark.start_position);
				writer.write_vec3(spark.plane_normal);
			}
		}
	} else {
		uint16_t cursor = 0;
		uint32_t rng_state = 0;
		uint32_t placement_timer = 0;
		writer.write_pod(cursor);
		writer.write_pod(rng_state);
		writer.write_pod(placement_timer);
		writer.write_pod(active_spark_count);
	}
	stats.active_spark_count = static_cast<int>(active_spark_count);
	stats.sparks += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_SCALAR(type, name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			writer.write_pod(soa.name[lane]); \
		} else { \
			const type wire_value = static_cast<type>(soa.name[lane]); \
			writer.write_pod(wire_value); \
		} \
	}
	MXT_NET_CAR_SCALAR_FIELDS(WRITE_NET_SCALAR)
#undef WRITE_NET_SCALAR
	stats.car_scalars += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_SCALAR(type, name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			writer.write_pod(soa.name[lane]); \
		} else { \
			const type wire_value = static_cast<type>(soa.name[lane]); \
			writer.write_pod(wire_value); \
		} \
	}
	if (bumper_cars) { \
		MXT_NET_CAR_SCALAR_FIELDS(WRITE_NET_BUMPER_SCALAR) \
	}
#undef WRITE_NET_BUMPER_SCALAR
	stats.bumper_scalars += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_VEC3(name) \
	WRITE_NET_VEC3_COMPONENT(name, x) \
	WRITE_NET_VEC3_COMPONENT(name, y) \
	WRITE_NET_VEC3_COMPONENT(name, z)
	MXT_NET_CAR_VEC3_FIELDS(WRITE_NET_VEC3)
#undef WRITE_NET_VEC3
#undef WRITE_NET_VEC3_COMPONENT
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		writer.write_vec3(LOAD_INDEXED_VEC3(soa, position_current, lane));
		writer.write_vec3(LOAD_INDEXED_VEC3(soa, position_old, lane));
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
#define WRITE_NET_VEC3_HALF(name) writer.write_vec3(LOAD_INDEXED_VEC3(soa, name, lane));
		MXT_NET_CAR_VEC3_HALF_FIELDS(WRITE_NET_VEC3_HALF)
#undef WRITE_NET_VEC3_HALF
	}
	stats.car_vec3 += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_BUMPER_VEC3(name) \
	WRITE_NET_BUMPER_VEC3_COMPONENT(name, x) \
	WRITE_NET_BUMPER_VEC3_COMPONENT(name, y) \
	WRITE_NET_BUMPER_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_CAR_VEC3_FIELDS(WRITE_NET_BUMPER_VEC3) \
	}
#undef WRITE_NET_BUMPER_VEC3
#undef WRITE_NET_BUMPER_VEC3_COMPONENT
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			writer.write_vec3(LOAD_INDEXED_VEC3(soa, position_current, lane));
			writer.write_vec3(LOAD_INDEXED_VEC3(soa, position_old, lane));
		}
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
#define WRITE_NET_BUMPER_VEC3_HALF(name) writer.write_vec3(LOAD_INDEXED_VEC3(soa, name, lane));
			MXT_NET_CAR_VEC3_HALF_FIELDS(WRITE_NET_BUMPER_VEC3_HALF)
#undef WRITE_NET_BUMPER_VEC3_HALF
		}
	}
	stats.bumper_vec3 += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_TRANSFORM(name) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c0x) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c0y) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c0z) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c1x) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c1y) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c1z) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c2x) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c2y) \
	WRITE_NET_TRANSFORM_COMPONENT(name, c2z) \
	WRITE_NET_TRANSFORM_COMPONENT(name, ox) \
	WRITE_NET_TRANSFORM_COMPONENT(name, oy) \
	WRITE_NET_TRANSFORM_COMPONENT(name, oz)
	MXT_NET_CAR_TRANSFORM_FIELDS(WRITE_NET_TRANSFORM)
#undef WRITE_NET_TRANSFORM
#undef WRITE_NET_TRANSFORM_COMPONENT
	stats.car_transform += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		writer.write_pod(soa.name##_##component[lane]); \
	}
#define WRITE_NET_BUMPER_TRANSFORM(name) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c0x) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c0y) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c0z) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c1x) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c1y) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c1z) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c2x) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c2y) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, c2z) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, ox) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, oy) \
	WRITE_NET_BUMPER_TRANSFORM_COMPONENT(name, oz)
	if (bumper_cars) { \
		MXT_NET_CAR_TRANSFORM_FIELDS(WRITE_NET_BUMPER_TRANSFORM) \
	}
#undef WRITE_NET_BUMPER_TRANSFORM
#undef WRITE_NET_BUMPER_TRANSFORM_COMPONENT
	stats.bumper_transform += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BASIS(name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		writer.write_basis(MXT_LOAD_TRANSFORM(soa, name, lane).basis); \
	}
	WRITE_NET_BASIS(basis_physical)
	WRITE_NET_BASIS(basis_physical_other)
#undef WRITE_NET_BASIS
	stats.car_basis += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_BASIS(name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		writer.write_basis(MXT_LOAD_TRANSFORM(soa, name, lane).basis); \
	}
	if (bumper_cars) { \
		WRITE_NET_BUMPER_BASIS(basis_physical) \
		WRITE_NET_BUMPER_BASIS(basis_physical_other) \
	}
#undef WRITE_NET_BUMPER_BASIS
	stats.bumper_basis += writer.size() - section_start;

	section_start = writer.size();
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if (soa.restore_state[lane] != 0) {
			++stats.car_restore_count;
			writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_start_transform, lane));
			writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_target_transform, lane));
		}
	}
	stats.car_conditionals += writer.size() - section_start;
	section_start = writer.size();
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			if (soa.restore_state[lane] != 0) {
				++stats.bumper_restore_count;
				writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_start_transform, lane));
				writer.write_transform(MXT_LOAD_TRANSFORM(soa, restore_target_transform, lane));
			}
		}
	}
	stats.bumper_conditionals += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				writer.write_pod(soa.tilt_##name[p]); \
			} else { \
				const type wire_value = static_cast<type>(soa.tilt_##name[p]); \
				writer.write_pod(wire_value); \
			} \
		} \
	}
	MXT_NET_TILT_SCALAR_FIELDS(WRITE_NET_TILT_SCALAR)
#undef WRITE_NET_TILT_SCALAR
	stats.car_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				writer.write_pod(soa.tilt_##name[p]); \
			} else { \
				const type wire_value = static_cast<type>(soa.tilt_##name[p]); \
				writer.write_pod(wire_value); \
			} \
		} \
	}
	if (bumper_cars) { \
		MXT_NET_TILT_SCALAR_FIELDS(WRITE_NET_BUMPER_TILT_SCALAR) \
	}
#undef WRITE_NET_BUMPER_TILT_SCALAR
	stats.bumper_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.tilt_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_TILT_VEC3(name) \
	WRITE_NET_TILT_VEC3_COMPONENT(name, x) \
	WRITE_NET_TILT_VEC3_COMPONENT(name, y) \
	WRITE_NET_TILT_VEC3_COMPONENT(name, z)
	MXT_NET_TILT_VEC3_FIELDS(WRITE_NET_TILT_VEC3)
#undef WRITE_NET_TILT_VEC3
#undef WRITE_NET_TILT_VEC3_COMPONENT
	stats.car_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.tilt_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_BUMPER_TILT_VEC3(name) \
	WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, x) \
	WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, y) \
	WRITE_NET_BUMPER_TILT_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_TILT_VEC3_FIELDS(WRITE_NET_BUMPER_TILT_VEC3) \
	}
#undef WRITE_NET_BUMPER_TILT_VEC3
#undef WRITE_NET_BUMPER_TILT_VEC3_COMPONENT
	stats.bumper_tilt += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.wall_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_WALL_VEC3(name) \
	WRITE_NET_WALL_VEC3_COMPONENT(name, x) \
	WRITE_NET_WALL_VEC3_COMPONENT(name, y) \
	WRITE_NET_WALL_VEC3_COMPONENT(name, z)
	MXT_NET_WALL_VEC3_FIELDS(WRITE_NET_WALL_VEC3)
#undef WRITE_NET_WALL_VEC3
#undef WRITE_NET_WALL_VEC3_COMPONENT
	stats.car_wall += writer.size() - section_start;

	section_start = writer.size();
#define WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			writer.write_pod(soa.wall_##name##_##component[p]); \
		} \
	}
#define WRITE_NET_BUMPER_WALL_VEC3(name) \
	WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, x) \
	WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, y) \
	WRITE_NET_BUMPER_WALL_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_WALL_VEC3_FIELDS(WRITE_NET_BUMPER_WALL_VEC3) \
	}
#undef WRITE_NET_BUMPER_WALL_VEC3
#undef WRITE_NET_BUMPER_WALL_VEC3_COMPONENT
	stats.bumper_wall += writer.size() - section_start;

	section_start = writer.size();
	for (int i = 0; i < trigger_count; ++i) {
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		uint8_t exploded = 0;
		float heat = 0.0f;
		uint32_t last_activation_tick = 0;
		uint8_t has_last_activation = 0;
		if (trigger && trigger->type == TRIGGER_TYPE::MINE) {
			exploded = static_cast<Mine*>(trigger)->exploded ? 1 : 0;
		} else if (trigger && trigger->type == TRIGGER_TYPE::DASHPLATE) {
			Dashplate* dash = static_cast<Dashplate*>(trigger);
			heat = dash->heat;
			last_activation_tick = dash->last_activation_tick;
			has_last_activation = dash->has_last_activation ? 1 : 0;
		}
		writer.write_pod(exploded);
		writer.write_pod(heat);
		writer.write_pod(last_activation_tick);
		writer.write_pod(has_last_activation);
	}
	stats.triggers += writer.size() - section_start;

	stats.total = writer.size();
	last_network_state_size_stats = stats;
	return writer.to_packed_byte_array();
}

bool GameSim::deserialize_network_state(int target_tick, const godot::PackedByteArray& data) {
	NetStateReader reader(data);
	uint32_t magic = 0;
	uint16_t flags = 0;
	int32_t snapshot_tick = 0;
	int32_t snapshot_cars = 0;
	int32_t snapshot_bumper_count = 0;
	int32_t trigger_count = 0;
	if (!reader.read_pod(magic) || magic != MXT_NET_STATE_MAGIC ||
		!reader.read_pod(flags) ||
		!reader.read_pod(snapshot_tick) ||
		!reader.read_pod(snapshot_cars) ||
		!reader.read_pod(snapshot_bumper_count) ||
		!reader.read_pod(bumper_track_seed) ||
		!reader.read_pod(bumper_scheduler_lap) ||
		!reader.read_pod(bumper_next_sequence)) {
		return false;
	}
	const bool exact_sim_floats = (flags & MXT_NET_STATE_FLAG_EXACT_SIM_FLOATS) != 0;
	(void)flags;
	(void)snapshot_tick;
	if (snapshot_cars != num_cars ||
			snapshot_bumper_count < 0 ||
			snapshot_bumper_count != bumper_count ||
			snapshot_bumper_count > BUMPER_POOL_SIZE) {
		return false;
	}
	for (int i = 0; i < snapshot_bumper_count; ++i) {
		BumperState& state = bumper_states[i];
		if (!reader.read_pod(state.active) ||
				!reader.read_pod(state.spawn_lap) ||
				!reader.read_pod(state.next_sequence) ||
				!reader.read_pod(state.target_lane)) {
			return false;
		}
		if (state.active > 1 || !std::isfinite(state.target_lane)) {
			return false;
		}
	}
	if (!reader.read_pod(trigger_count)) {
		return false;
	}
	if (trigger_count < 0) {
		return false;
	}
	if (!super_spark_state) {
		return false;
	}
	uint16_t spark_cursor = 0;
	uint32_t spark_rng_state = 0;
	uint32_t spark_placement_timer = 0;
	uint16_t active_spark_count = 0;
	if (!reader.read_pod(spark_cursor) ||
		!reader.read_pod(spark_rng_state) ||
		!reader.read_pod(spark_placement_timer) ||
		!reader.read_pod(active_spark_count)) {
		return false;
	}
	super_spark_state->cursor = spark_cursor;
	super_spark_state->rng_state = spark_rng_state;
	super_spark_state->placement_timer = spark_placement_timer;
	for (uint16_t i = 0; i < SUPER_SPARK_CAPACITY; ++i) {
		super_spark_state->sparks[i] = SuperSpark();
	}
	for (uint16_t n = 0; n < active_spark_count; ++n) {
		uint16_t spark_index = 0;
		if (!reader.read_pod(spark_index) || spark_index >= SUPER_SPARK_CAPACITY) {
			return false;
		}
		uint8_t spark_flags = 0;
		SuperSpark& spark = super_spark_state->sparks[spark_index];
		if (!reader.read_pod(spark_flags) ||
			!reader.read_pod(spark.checkpoint) ||
			!(exact_sim_floats ? reader.read_vec3(spark.final_position) : reader.read_vec3_half(spark.final_position))) {
			return false;
		}
		spark.active = 1;
		spark.collectable = (spark_flags & 1u) != 0u ? 1u : 0u;
		if (spark.collectable) {
			spark.animation_frame = MXT_SUPER_SPARK_ANIMATION_FRAMES;
			spark.start_position = spark.final_position;
			spark.plane_normal = SimVec3(0.0f, 1.0f, 0.0f);
			spark.position = spark.final_position;
			spark.prev_position = spark.final_position;
		} else {
			if (!reader.read_pod(spark.animation_frame) ||
				!(exact_sim_floats ? reader.read_vec3(spark.start_position) : reader.read_vec3_half(spark.start_position)) ||
				!(exact_sim_floats ? reader.read_vec3(spark.plane_normal) : reader.read_vec3_half(spark.plane_normal))) {
				return false;
			}
			spark.position = mxt_super_spark_position_at_frame(
				spark.start_position, spark.final_position, spark.plane_normal, spark.animation_frame, spark.collectable);
			if (spark.animation_frame > 0) {
				spark.prev_position = mxt_super_spark_position_at_frame(
					spark.start_position, spark.final_position, spark.plane_normal, spark.animation_frame - 1, spark.collectable);
			} else {
				spark.prev_position = spark.position;
			}
		}
	}
	super_sparks = super_spark_state->sparks;

#define READ_NET_SCALAR(type, name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			float wire_value = 0.0f; \
			if (!(exact_sim_floats ? reader.read_pod(wire_value) : reader.read_float16(wire_value))) return false; \
			soa.name[lane] = wire_value; \
		} else { \
			type wire_value; \
			if (!reader.read_pod(wire_value)) return false; \
			soa.name[lane] = wire_value; \
		} \
	}
	MXT_NET_CAR_SCALAR_FIELDS(READ_NET_SCALAR)
#undef READ_NET_SCALAR

#define READ_NET_BUMPER_SCALAR(type, name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if constexpr (std::is_same_v<type, float>) { \
			float wire_value = 0.0f; \
			if (!(exact_sim_floats ? reader.read_pod(wire_value) : reader.read_float16(wire_value))) return false; \
			soa.name[lane] = wire_value; \
		} else { \
			type wire_value; \
			if (!reader.read_pod(wire_value)) return false; \
			soa.name[lane] = wire_value; \
		} \
	}
	if (bumper_count > 0 && !bumper_cars) { \
		return false; \
	}
	if (bumper_cars) { \
		MXT_NET_CAR_SCALAR_FIELDS(READ_NET_BUMPER_SCALAR) \
	}
#undef READ_NET_BUMPER_SCALAR

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		soa.simulation_tick[cars[i].soa_index] = static_cast<uint32_t>(target_tick);
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			soa.simulation_tick[bumper_cars[i].soa_index] = static_cast<uint32_t>(target_tick);
		}
	}

	if (current_track) {
		for (int i = 0; i < num_cars; ++i) {
			PhysicsCarSoA& soa = *cars[i].soa;
			const int lane = cars[i].soa_index;
			if (soa.current_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.current_collision_checkpoint[lane] < -1 ||
					soa.current_collision_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.last_ground_checkpoint[lane] >= current_track->num_checkpoints) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT network state rejected invalid checkpoint car="), static_cast<int64_t>(i),
					godot::String(" cp="), static_cast<int64_t>(soa.current_checkpoint[lane]),
					godot::String(" coll_cp="), static_cast<int64_t>(soa.current_collision_checkpoint[lane]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa.last_ground_checkpoint[lane]),
					godot::String(" checkpoint_count="), static_cast<int64_t>(current_track->num_checkpoints));
				return false;
			}
		}
		for (int i = 0; bumper_cars && i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			if (bumper_states[i].active &&
					(soa.current_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.current_collision_checkpoint[lane] < -1 ||
					soa.current_collision_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.last_ground_checkpoint[lane] >= current_track->num_checkpoints)) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT network state rejected invalid bumper checkpoint car="), static_cast<int64_t>(i),
					godot::String(" cp="), static_cast<int64_t>(soa.current_checkpoint[lane]),
					godot::String(" coll_cp="), static_cast<int64_t>(soa.current_collision_checkpoint[lane]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa.last_ground_checkpoint[lane]),
					godot::String(" checkpoint_count="), static_cast<int64_t>(current_track->num_checkpoints));
				return false;
			}
		}
	}

#define READ_NET_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_VEC3(name) \
	READ_NET_VEC3_COMPONENT(name, x) \
	READ_NET_VEC3_COMPONENT(name, y) \
	READ_NET_VEC3_COMPONENT(name, z)
	MXT_NET_CAR_VEC3_FIELDS(READ_NET_VEC3)
#undef READ_NET_VEC3
#undef READ_NET_VEC3_COMPONENT
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		const SimTransform surface = mxt_net_checkpoint_surface_or_identity(current_track, soa.current_checkpoint[lane], soa.checkpoint_fraction[lane]);
		SimVec3 local_current;
		SimVec3 local_old;
		if (!(exact_sim_floats ? reader.read_vec3(local_current) : reader.read_vec3_half(local_current)) ||
				!(exact_sim_floats ? reader.read_vec3(local_old) : reader.read_vec3_half(local_old))) {
			return false;
		}
		STORE_INDEXED_VEC3(soa, position_current, lane, exact_sim_floats ? local_current : surface.xform(local_current));
		STORE_INDEXED_VEC3(soa, position_old, lane, exact_sim_floats ? local_old : surface.xform(local_old));
	}
	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
#define READ_NET_VEC3_HALF(name) do { SimVec3 v; if (!(exact_sim_floats ? reader.read_vec3(v) : reader.read_vec3_half(v))) return false; STORE_INDEXED_VEC3(soa, name, lane, v); } while (0);
		MXT_NET_CAR_VEC3_HALF_FIELDS(READ_NET_VEC3_HALF)
#undef READ_NET_VEC3_HALF
	}

#define READ_NET_BUMPER_VEC3_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_BUMPER_VEC3(name) \
	READ_NET_BUMPER_VEC3_COMPONENT(name, x) \
	READ_NET_BUMPER_VEC3_COMPONENT(name, y) \
	READ_NET_BUMPER_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_CAR_VEC3_FIELDS(READ_NET_BUMPER_VEC3) \
	}
#undef READ_NET_BUMPER_VEC3
#undef READ_NET_BUMPER_VEC3_COMPONENT
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			const SimTransform surface = mxt_net_checkpoint_surface_or_identity(current_track, soa.current_checkpoint[lane], soa.checkpoint_fraction[lane]);
			SimVec3 local_current;
			SimVec3 local_old;
			if (!(exact_sim_floats ? reader.read_vec3(local_current) : reader.read_vec3_half(local_current)) ||
					!(exact_sim_floats ? reader.read_vec3(local_old) : reader.read_vec3_half(local_old))) {
				return false;
			}
			STORE_INDEXED_VEC3(soa, position_current, lane, exact_sim_floats ? local_current : surface.xform(local_current));
			STORE_INDEXED_VEC3(soa, position_old, lane, exact_sim_floats ? local_old : surface.xform(local_old));
		}
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
#define READ_NET_BUMPER_VEC3_HALF(name) do { SimVec3 v; if (!(exact_sim_floats ? reader.read_vec3(v) : reader.read_vec3_half(v))) return false; STORE_INDEXED_VEC3(soa, name, lane, v); } while (0);
			MXT_NET_CAR_VEC3_HALF_FIELDS(READ_NET_BUMPER_VEC3_HALF)
#undef READ_NET_BUMPER_VEC3_HALF
		}
	}

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		STORE_INDEXED_VEC3(soa, position_old_dupe, lane, LOAD_INDEXED_VEC3(soa, position_old, lane));
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			STORE_INDEXED_VEC3(soa, position_old_dupe, lane, LOAD_INDEXED_VEC3(soa, position_old, lane));
		}
	}

#define READ_NET_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_TRANSFORM(name) \
	READ_NET_TRANSFORM_COMPONENT(name, c0x) \
	READ_NET_TRANSFORM_COMPONENT(name, c0y) \
	READ_NET_TRANSFORM_COMPONENT(name, c0z) \
	READ_NET_TRANSFORM_COMPONENT(name, c1x) \
	READ_NET_TRANSFORM_COMPONENT(name, c1y) \
	READ_NET_TRANSFORM_COMPONENT(name, c1z) \
	READ_NET_TRANSFORM_COMPONENT(name, c2x) \
	READ_NET_TRANSFORM_COMPONENT(name, c2y) \
	READ_NET_TRANSFORM_COMPONENT(name, c2z) \
	READ_NET_TRANSFORM_COMPONENT(name, ox) \
	READ_NET_TRANSFORM_COMPONENT(name, oy) \
	READ_NET_TRANSFORM_COMPONENT(name, oz)
	MXT_NET_CAR_TRANSFORM_FIELDS(READ_NET_TRANSFORM)
#undef READ_NET_TRANSFORM
#undef READ_NET_TRANSFORM_COMPONENT

#define READ_NET_BUMPER_TRANSFORM_COMPONENT(name, component) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		if (!reader.read_pod(soa.name##_##component[lane])) return false; \
	}
#define READ_NET_BUMPER_TRANSFORM(name) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c0x) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c0y) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c0z) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c1x) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c1y) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c1z) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c2x) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c2y) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, c2z) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, ox) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, oy) \
	READ_NET_BUMPER_TRANSFORM_COMPONENT(name, oz)
	if (bumper_cars) { \
		MXT_NET_CAR_TRANSFORM_FIELDS(READ_NET_BUMPER_TRANSFORM) \
	}
#undef READ_NET_BUMPER_TRANSFORM
#undef READ_NET_BUMPER_TRANSFORM_COMPONENT

#define READ_NET_BASIS(name) \
	for (int i = 0; i < num_cars; ++i) { \
		PhysicsCarSoA& soa = *cars[i].soa; \
		const int lane = cars[i].soa_index; \
		SimTransform t = MXT_LOAD_TRANSFORM(soa, name, lane); \
		if (exact_sim_floats) { \
			if (!reader.read_basis(t.basis)) return false; \
		} else { \
			SimQuat q; \
			if (!reader.read_quat_i16(q)) return false; \
			t.basis = SimBasis(q); \
		} \
		MXT_STORE_TRANSFORM(soa, name, lane, t); \
		soa.name##_ox[lane] = 0.0f; \
		soa.name##_oy[lane] = 0.0f; \
		soa.name##_oz[lane] = 0.0f; \
	}
	READ_NET_BASIS(basis_physical)
	READ_NET_BASIS(basis_physical_other)
#undef READ_NET_BASIS

#define READ_NET_BUMPER_BASIS(name) \
	for (int i = 0; i < bumper_count; ++i) { \
		PhysicsCarSoA& soa = *bumper_cars[i].soa; \
		const int lane = bumper_cars[i].soa_index; \
		SimTransform t = MXT_LOAD_TRANSFORM(soa, name, lane); \
		if (exact_sim_floats) { \
			if (!reader.read_basis(t.basis)) return false; \
		} else { \
			SimQuat q; \
			if (!reader.read_quat_i16(q)) return false; \
			t.basis = SimBasis(q); \
		} \
		MXT_STORE_TRANSFORM(soa, name, lane, t); \
		soa.name##_ox[lane] = 0.0f; \
		soa.name##_oy[lane] = 0.0f; \
		soa.name##_oz[lane] = 0.0f; \
	}
	if (bumper_cars) { \
		READ_NET_BUMPER_BASIS(basis_physical) \
		READ_NET_BUMPER_BASIS(basis_physical_other) \
	}
#undef READ_NET_BUMPER_BASIS

	for (int i = 0; i < num_cars; ++i) {
		PhysicsCarSoA& soa = *cars[i].soa;
		const int lane = cars[i].soa_index;
		if (soa.restore_state[lane] != 0) {
			SimTransform restore_start;
			SimTransform restore_target;
			if (!reader.read_transform(restore_start) ||
				!reader.read_transform(restore_target)) {
				return false;
			}
			MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, restore_start);
			MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, restore_target);
		} else {
			const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
			MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, basis);
			MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, basis);
		}
	}
	if (bumper_cars) {
		for (int i = 0; i < bumper_count; ++i) {
			PhysicsCarSoA& soa = *bumper_cars[i].soa;
			const int lane = bumper_cars[i].soa_index;
			if (soa.restore_state[lane] != 0) {
				SimTransform restore_start;
				SimTransform restore_target;
				if (!reader.read_transform(restore_start) ||
					!reader.read_transform(restore_target)) {
					return false;
				}
				MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, restore_start);
				MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, restore_target);
			} else {
				const SimTransform basis = MXT_LOAD_TRANSFORM(soa, basis_physical, lane);
				MXT_STORE_TRANSFORM(soa, restore_start_transform, lane, basis);
				MXT_STORE_TRANSFORM(soa, restore_target_transform, lane, basis);
			}
		}
	}

#define READ_NET_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				float wire_value = 0.0f; \
				if (!(exact_sim_floats ? reader.read_pod(wire_value) : reader.read_float16(wire_value))) return false; \
				soa.tilt_##name[p] = wire_value; \
			} else { \
				type wire_value; \
				if (!reader.read_pod(wire_value)) return false; \
				soa.tilt_##name[p] = wire_value; \
			} \
		} \
	}
	MXT_NET_TILT_SCALAR_FIELDS(READ_NET_TILT_SCALAR)
#undef READ_NET_TILT_SCALAR

#define READ_NET_BUMPER_TILT_SCALAR(type, name) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if constexpr (std::is_same_v<type, float>) { \
				float wire_value = 0.0f; \
				if (!(exact_sim_floats ? reader.read_pod(wire_value) : reader.read_float16(wire_value))) return false; \
				soa.tilt_##name[p] = wire_value; \
			} else { \
				type wire_value; \
				if (!reader.read_pod(wire_value)) return false; \
				soa.tilt_##name[p] = wire_value; \
			} \
		} \
	}
	if (bumper_cars) { \
		MXT_NET_TILT_SCALAR_FIELDS(READ_NET_BUMPER_TILT_SCALAR) \
	}
#undef READ_NET_BUMPER_TILT_SCALAR

#define READ_NET_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.tilt_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_TILT_VEC3(name) \
	READ_NET_TILT_VEC3_COMPONENT(name, x) \
	READ_NET_TILT_VEC3_COMPONENT(name, y) \
	READ_NET_TILT_VEC3_COMPONENT(name, z)
	MXT_NET_TILT_VEC3_FIELDS(READ_NET_TILT_VEC3)
#undef READ_NET_TILT_VEC3
#undef READ_NET_TILT_VEC3_COMPONENT

#define READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.tilt_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_BUMPER_TILT_VEC3(name) \
	READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, x) \
	READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, y) \
	READ_NET_BUMPER_TILT_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_TILT_VEC3_FIELDS(READ_NET_BUMPER_TILT_VEC3) \
	}
#undef READ_NET_BUMPER_TILT_VEC3
#undef READ_NET_BUMPER_TILT_VEC3_COMPONENT

#define READ_NET_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < num_cars; ++i) { \
			PhysicsCarSoA& soa = *cars[i].soa; \
			const int p = cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.wall_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_WALL_VEC3(name) \
	READ_NET_WALL_VEC3_COMPONENT(name, x) \
	READ_NET_WALL_VEC3_COMPONENT(name, y) \
	READ_NET_WALL_VEC3_COMPONENT(name, z)
	MXT_NET_WALL_VEC3_FIELDS(READ_NET_WALL_VEC3)
#undef READ_NET_WALL_VEC3
#undef READ_NET_WALL_VEC3_COMPONENT

#define READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, component) \
	for (int point = 0; point < 4; ++point) { \
		for (int i = 0; i < bumper_count; ++i) { \
			PhysicsCarSoA& soa = *bumper_cars[i].soa; \
			const int p = bumper_cars[i].soa_index * 4 + point; \
			if (!reader.read_pod(soa.wall_##name##_##component[p])) return false; \
		} \
	}
#define READ_NET_BUMPER_WALL_VEC3(name) \
	READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, x) \
	READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, y) \
	READ_NET_BUMPER_WALL_VEC3_COMPONENT(name, z)
	if (bumper_cars) { \
		MXT_NET_WALL_VEC3_FIELDS(READ_NET_BUMPER_WALL_VEC3) \
	}
#undef READ_NET_BUMPER_WALL_VEC3
#undef READ_NET_BUMPER_WALL_VEC3_COMPONENT

	const int local_trigger_count = current_track ? current_track->num_trigger_colliders : 0;
	if (trigger_count != local_trigger_count) {
		return false;
	}
	for (int i = 0; i < trigger_count; ++i) {
		uint8_t exploded = 0;
		float heat = 0.0f;
		uint32_t last_activation_tick = 0;
		uint8_t has_last_activation = 0;
		if (!reader.read_pod(exploded) ||
			!reader.read_pod(heat) ||
			!reader.read_pod(last_activation_tick) ||
			!reader.read_pod(has_last_activation)) {
			return false;
		}
		TriggerCollider* trigger = current_track->trigger_colliders[i];
		if (!trigger) {
			continue;
		}
		if (trigger->type == TRIGGER_TYPE::MINE) {
			static_cast<Mine*>(trigger)->exploded = exploded != 0;
		} else if (trigger->type == TRIGGER_TYPE::DASHPLATE) {
			Dashplate* dash = static_cast<Dashplate*>(trigger);
			dash->heat = heat;
			dash->last_activation_tick = last_activation_tick;
			dash->has_last_activation = has_last_activation != 0;
		}
	}

	rebuild_static_state_after_network_load();
	for (int i = 0; i < bumper_count && i < BUMPER_POOL_SIZE; ++i) {
		if (!bumper_cars) {
			continue;
		}
		PhysicsCarSoA& soa = *bumper_cars[i].soa;
		const int lane = bumper_cars[i].soa_index;
		if (bumper_states[i].active) {
			soa.current_track[lane] = current_track;
			soa.restore_state[lane] = 0;
			soa.restore_wait_frames[lane] = 0;
			soa.restore_move_frames[lane] = 0;
		} else {
			deactivate_bumper_car(i);
		}
	}
	if (current_track) {
		for (int i = 0; i < num_cars; ++i) {
			PhysicsCarSoA& soa = *cars[i].soa;
			const int lane = cars[i].soa_index;
			if (soa.current_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.current_collision_checkpoint[lane] < -1 ||
					soa.current_collision_checkpoint[lane] >= current_track->num_checkpoints ||
					soa.last_ground_checkpoint[lane] >= current_track->num_checkpoints) {
				godot::UtilityFunctions::printerr(
					godot::String("MXT network state rejected invalid rebuilt checkpoint car="), static_cast<int64_t>(i),
					godot::String(" cp="), static_cast<int64_t>(soa.current_checkpoint[lane]),
					godot::String(" coll_cp="), static_cast<int64_t>(soa.current_collision_checkpoint[lane]),
					godot::String(" last_ground_cp="), static_cast<int64_t>(soa.last_ground_checkpoint[lane]),
					godot::String(" checkpoint_count="), static_cast<int64_t>(current_track->num_checkpoints));
				return false;
			}
		}
	}
	const int index = target_tick % STATE_BUFFER_LEN;
	const int size = gamestate_data.get_size();
	if (state_buffer[index].data && size > 0) {
		std::memcpy(state_buffer[index].data, gamestate_data.heap_start, size);
		state_buffer[index].size = size;
		state_buffer[index].tick = target_tick;
		save_bumper_states_to_saved_state(state_buffer[index]);
		update_saved_voice_transforms(state_buffer[index]);
	}
	return true;
}
