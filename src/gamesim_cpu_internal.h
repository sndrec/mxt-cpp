#pragma once

#include "gamesim_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
struct NativeCpuTickContext {
	int expected_tick = 0;
	int lane_noise_t0 = 0;
	int lane_noise_t1 = 1;
	float lane_noise_smooth = 0.0f;
};

static inline NativeCpuTickContext native_cpu_make_tick_context(int expected_tick)
{
	constexpr int lane_period_ticks = 480;
	NativeCpuTickContext out;
	out.expected_tick = expected_tick;
	out.lane_noise_t0 = expected_tick / lane_period_ticks;
	out.lane_noise_t1 = out.lane_noise_t0 + 1;
	const float frac = static_cast<float>(expected_tick - out.lane_noise_t0 * lane_period_ticks) / static_cast<float>(lane_period_ticks);
	out.lane_noise_smooth = frac * frac * (3.0f - 2.0f * frac);
	return out;
}

static inline PlayerInput native_cpu_generate_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed);
static inline PlayerInput native_cpu_generate_quantized_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed);
static inline PlayerInput native_cpu_generate_quantized_input_for_car(const PhysicsCar& car, int32_t player_id, const NativeCpuTickContext& tick_context, int spawn_seed);

static inline uint32_t native_cpu_hash_u32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static inline float native_cpu_rand01_from_seed(uint32_t seed)
{
	return static_cast<float>(native_cpu_hash_u32(seed) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

static inline float native_cpu_smooth_noise_signed(uint32_t seed_base, int t0, int t1, float smooth)
{
	const float a = native_cpu_rand01_from_seed(seed_base ^ (static_cast<uint32_t>(t0) * 0x27D4EB2Du)) * 2.0f - 1.0f;
	const float b = native_cpu_rand01_from_seed(seed_base ^ (static_cast<uint32_t>(t1) * 0x27D4EB2Du)) * 2.0f - 1.0f;
	return a + (b - a) * smooth;
}

static inline float checkpoint_fraction_for_cpu_guidance(const CollisionCheckpoint& cp, const SimVec3& pos)
{
	return std::max(0.0f, std::min(1.0f, checkpoint_plane_fraction_unclamped(cp, pos, 1.0e-6f)));
}

struct NativeCpuGuidanceInput {
	float strafe_left = 0.0f;
	float strafe_right = 0.0f;
	float steer_horizontal = 0.0f;
	bool boost = false;
};

static inline uint8_t native_cpu_quantize_unit_fast(float v)
{
	v = std::max(0.0f, std::min(1.0f, v));
	return static_cast<uint8_t>(v * static_cast<float>(PlayerInput::RAW_BIT_PRECISION) + 0.5f);
}

static inline uint8_t native_cpu_quantize_axis_fast(float v)
{
	v = std::max(-1.0f, std::min(1.0f, v));
	return static_cast<uint8_t>(((v + 1.0f) * 0.5f) * static_cast<float>(PlayerInput::RAW_BIT_PRECISION) + 0.5f);
}

static inline NativeCpuGuidanceInput native_cpu_generate_guidance_for_car(const PhysicsCar& car, int32_t player_id, const NativeCpuTickContext& tick_context, int spawn_seed)
{
	PhysicsCarSoA& soa = *car.soa;
	const int i = car.soa_index;
	const int expected_tick = tick_context.expected_tick;
	const SimBasis physical_basis = MXT_LOAD_TRANSFORM(soa, basis_physical, i).basis;
	SimBasis surface = soa.road_sample[i].closest_surface.basis;
	float road_tx = soa.road_sample[i].road_t.x;
	if (soa.current_track[i]) {
		int sample_cp = soa.current_collision_checkpoint[i];
		if (sample_cp < 0 || sample_cp >= soa.current_track[i]->num_checkpoints) {
			sample_cp = soa.current_checkpoint[i];
		}
		if (sample_cp >= 0 && sample_cp < soa.current_track[i]->num_checkpoints) {
			const CollisionCheckpoint &cp = soa.current_track[i]->checkpoints[sample_cp];
			const SimVec3 pos = LOAD_INDEXED_VEC3(soa, position_current, i);
			float cp_t = 0.0f;
			if (sample_cp == static_cast<int>(soa.current_checkpoint[i])) {
				cp_t = std::max(0.0f, std::min(1.0f, soa.checkpoint_fraction[i]));
			} else {
				cp_t = checkpoint_fraction_for_cpu_guidance(cp, pos);
			}
			surface[0] = cp.orientation_start[0].lerp(cp.orientation_end[0], cp_t);
			surface[1] = cp.orientation_start[1].lerp(cp.orientation_end[1], cp_t);
			surface[2] = cp.orientation_start[2].lerp(cp.orientation_end[2], cp_t);
			const SimVec3 center = cp.position_start.lerp(cp.position_end, cp_t);
			const float x_radius_inv = lerp(cp.x_radius_start_inv, cp.x_radius_end_inv, cp_t);
			road_tx = (pos - center).dot(surface[0]) * x_radius_inv;
		}
	}
	const float energy = soa.energy[i];
	const uint32_t tilt_state = soa.tilt_state[i * 4 + 1];
	const uint32_t seed_base =
		static_cast<uint32_t>(player_id) * 0x9E3779B9u ^
		static_cast<uint32_t>(expected_tick) * 0x85EBCA6Bu ^
		static_cast<uint32_t>(spawn_seed) * 0xC2B2AE35u;
	const uint32_t lane_seed =
		static_cast<uint32_t>(player_id) * 0x9E3779B9u ^
		static_cast<uint32_t>(spawn_seed) * 0xC2B2AE35u ^
		0xA341316Cu;

	NativeCpuGuidanceInput input;

	float desired_steer = (physical_basis.c0 + surface.c0).dot(surface.c2);
	const float desired_lane = native_cpu_smooth_noise_signed(
		lane_seed,
		tick_context.lane_noise_t0,
		tick_context.lane_noise_t1,
		tick_context.lane_noise_smooth) * 0.8f;

	const float lane_offset = road_tx + desired_lane;
	input.strafe_left = std::max(0.0f, std::min(1.0f, std::abs(std::min(lane_offset, 0.0f)) * 4.0f));
	input.strafe_right = std::max(0.0f, std::min(1.0f, std::max(lane_offset, 0.0f) * 4.0f));

	const bool drifting = (tilt_state & 0x4u) != 0;
	const bool wants_drift = std::abs(desired_steer) >= 0.4f && !drifting;
	if (drifting) {
		desired_steer *= 5.0f;
	}
	input.steer_horizontal = std::max(-1.0f, std::min(1.0f, desired_steer * 30.0f));
	if (wants_drift) {
		input.strafe_left = 1.0f;
		input.strafe_right = 1.0f;
	}

	const uint32_t boost_phase = native_cpu_hash_u32(seed_base ^ 0xB5297A4Du) % 720u;
	const bool wants_boost = energy > 10.0f && boost_phase == 0u;
	input.boost = wants_boost;

	return input;
}

static inline PlayerInput native_cpu_generate_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed)
{
	const NativeCpuGuidanceInput guidance = native_cpu_generate_guidance_for_car(car, player_id, native_cpu_make_tick_context(expected_tick), spawn_seed);
	PlayerInput input = PlayerInput::from_neutral();
	input.accelerate = 1.0f;
	input.strafe_left = guidance.strafe_left;
	input.strafe_right = guidance.strafe_right;
	input.steer_horizontal = guidance.steer_horizontal;
	input.boost = guidance.boost;
	return input;
}

static inline PlayerInput native_cpu_generate_quantized_input_for_car(const PhysicsCar& car, int32_t player_id, int expected_tick, int spawn_seed)
{
	return native_cpu_generate_quantized_input_for_car(car, player_id, native_cpu_make_tick_context(expected_tick), spawn_seed);
}

static inline PlayerInput native_cpu_generate_quantized_input_for_car(const PhysicsCar& car, int32_t player_id, const NativeCpuTickContext& tick_context, int spawn_seed)
{
	const NativeCpuGuidanceInput input = native_cpu_generate_guidance_for_car(car, player_id, tick_context, spawn_seed);
	PlayerInput out{};
	uint8_t q = native_cpu_quantize_unit_fast(input.strafe_left);
	if (q != PlayerInput::TRIGGER_NEUTRAL) {
		out.strafe_left = static_cast<float>(q) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION);
	}
	q = native_cpu_quantize_unit_fast(input.strafe_right);
	if (q != PlayerInput::TRIGGER_NEUTRAL) {
		out.strafe_right = static_cast<float>(q) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION);
	}
	q = native_cpu_quantize_axis_fast(input.steer_horizontal);
	if (q != PlayerInput::AXIS_NEUTRAL) {
		out.steer_horizontal = (static_cast<float>(q) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION)) * 2.0f - 1.0f;
	}
	out.accelerate = 1.0f;
	out.boost = input.boost;
	return out;
}
