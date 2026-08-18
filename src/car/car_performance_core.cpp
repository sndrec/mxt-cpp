#include "car/car_performance_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

static constexpr float TICK_RATE = 60.0f;
static constexpr uint32_t DRIVE_FRAMES = 1800;
static constexpr float STANDARD_SPEED_KMH = 600.0f;

struct DriveResult {
	float speed_1s = 0.0f;
	float speed_3s = 0.0f;
	float area_5s = 0.0f;
	float terminal_speed = 0.0f;
	float peak_speed = 0.0f;
	float distance = 0.0f;
	float time_300 = 31.0f;
	float time_500 = 31.0f;
	float time_95 = 30.0f;
	float settlement_confidence = 0.0f;
};

static float finite_or(float value, float fallback = 0.0f) {
	return std::isfinite(value) ? value : fallback;
}

static float safe_positive(float value, float floor = 0.0001f) {
	return std::max(std::abs(finite_or(value)), floor);
}

static void sample_effective_stats(
		const PhysicsCarProperties &properties,
		CarStatModifierLayer technique,
		float technique_intensity,
		CarStatModifierLayer boost,
		bool s_boost,
		float out_stats[CAR_STAT_COUNT]) {
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		out_stats[stat] = finite_or(evaluate_car_stat(
			properties, static_cast<CarStatId>(stat), technique,
			technique_intensity, boost, s_boost));
	}
}

static DriveResult run_drive(
		const PhysicsCarProperties &properties,
		bool spend_full_boost_reserve) {
	float ordinary[CAR_STAT_COUNT];
	float boosted[CAR_STAT_COUNT];
	sample_effective_stats(properties, CAR_MODIFIER_LAYER_COUNT, 0.0f,
		CAR_MODIFIER_NO_BOOST, false, ordinary);
	sample_effective_stats(properties, CAR_MODIFIER_LAYER_COUNT, 0.0f,
		CAR_MODIFIER_MANUAL_BOOST, false, boosted);

	const float weight = safe_positive(ordinary[CAR_STAT_WEIGHT_KG], 1.0f);
	float world_speed = 0.0f;
	float base_speed = 0.0f;
	float turbo = 0.0f;
	float energy = std::max(ordinary[CAR_STAT_MAX_ENERGY], 0.0f);
	int32_t manual_frames = 0;
	float last_speeds[120]{};
	DriveResult result;
	for (uint32_t frame = 0; frame < DRIVE_FRAMES; ++frame) {
		bool started_manual = false;
		if (spend_full_boost_reserve && manual_frames == 0 && energy > 1.0f) {
			const int32_t duration = std::max(0, static_cast<int32_t>(
				boosted[CAR_STAT_MANUAL_BOOST_DURATION_SECONDS] * TICK_RATE + 0.5f));
			if (duration > 0) {
				manual_frames = duration;
				turbo += boosted[CAR_STAT_MANUAL_TURBO_GAIN];
				started_manual = true;
			}
		}
		const bool manual_active = manual_frames > 0;
		const float *stats = manual_active ? boosted : ordinary;
		turbo -= (stats[CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND] +
			turbo * stats[CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND]) / TICK_RATE;
		turbo = std::max(turbo, 0.0f);

		float target_speed = 40.0f * stats[CAR_STAT_ACCELERATION] / 348.0f;
		target_speed *= stats[CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER];
		target_speed += base_speed;
		const float normalized_speed = world_speed / weight;
		const float speed_difference = target_speed - normalized_speed;
		const float denominator = 36.0f + 40.0f * stats[CAR_STAT_MAX_SPEED] +
			turbo * stats[CAR_STAT_TURBO_TOP_SPEED_EFFECT];
		const float speed_factor = std::abs(denominator) > 0.0001f
			? std::max(target_speed / denominator, 0.0f) : 0.0f;
		float accel_magnitude = speed_factor * 4.0f * stats[CAR_STAT_ACCELERATION] *
			(0.6f + stats[CAR_STAT_ACCELERATION]) *
			stats[CAR_STAT_ACCELERATION_RESPONSE_MULTIPLIER];
		if (started_manual) accel_magnitude = 0.0f;
		base_speed = std::max(
			target_speed - speed_difference * accel_magnitude - stats[CAR_STAT_DRAG], 0.0f);
		float thrust = 1000.0f * stats[CAR_STAT_FORWARD_THRUST_MULTIPLIER] * speed_difference;
		if (normalized_speed < 0.0f || speed_difference < 0.0f) thrust *= 0.15f;
		world_speed += thrust;
		const float speed_over_weight = world_speed / weight;
		float speed_kmh = 216.0f * speed_over_weight;
		if (speed_kmh < 2.0f) {
			world_speed = 0.0f;
			speed_kmh = 0.0f;
		} else {
			world_speed -= speed_over_weight * speed_over_weight * 8.0f;
			speed_kmh = 216.0f * world_speed / weight;
		}
		if (!std::isfinite(speed_kmh) || std::abs(speed_kmh) > 1000000.0f) {
			speed_kmh = std::clamp(finite_or(speed_kmh), -1000000.0f, 1000000.0f);
			world_speed = speed_kmh * weight / 216.0f;
		}

		const float seconds = static_cast<float>(frame + 1) / TICK_RATE;
		if (frame == 59) result.speed_1s = speed_kmh;
		if (frame == 179) result.speed_3s = speed_kmh;
		if (frame < 300) result.area_5s += speed_kmh / TICK_RATE;
		if (result.time_300 > 30.0f && speed_kmh >= 300.0f) result.time_300 = seconds;
		if (result.time_500 > 30.0f && speed_kmh >= 500.0f) result.time_500 = seconds;
		result.distance += std::max(speed_kmh, 0.0f) / 3.6f / TICK_RATE;
		result.peak_speed = std::max(result.peak_speed, speed_kmh);
		last_speeds[frame % 120] = speed_kmh;

		if (manual_active) {
			energy = std::max(energy - 0.1666666667f *
				safe_positive(stats[CAR_STAT_BOOST_ENERGY_USE_RATE]), 0.0f);
			--manual_frames;
			if (energy < 0.01f) manual_frames = 0;
		}
	}

	float terminal_sum = 0.0f;
	float terminal_min = last_speeds[0];
	float terminal_max = last_speeds[0];
	for (float speed : last_speeds) {
		terminal_sum += speed;
		terminal_min = std::min(terminal_min, speed);
		terminal_max = std::max(terminal_max, speed);
	}
	result.terminal_speed = terminal_sum / 120.0f;
	const float allowed_span = std::max(std::abs(result.terminal_speed) * 0.01f, 0.25f);
	result.settlement_confidence = std::clamp(
		1.0f - (terminal_max - terminal_min) / allowed_span, 0.0f, 1.0f);

	// A bounded second pass finds the descriptive time to 95% of the settled result.
	const float target_95 = result.terminal_speed * 0.95f;
	if (target_95 <= 0.0f) return result;
	float speed = 0.0f;
	float base = 0.0f;
	for (uint32_t frame = 0; frame < DRIVE_FRAMES; ++frame) {
		const float target = 40.0f * ordinary[CAR_STAT_ACCELERATION] / 348.0f *
			ordinary[CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER] + base;
		const float normalized = speed / weight;
		const float difference = target - normalized;
		const float denom = 36.0f + 40.0f * ordinary[CAR_STAT_MAX_SPEED];
		const float factor = std::abs(denom) > 0.0001f ? std::max(target / denom, 0.0f) : 0.0f;
		const float response = factor * 4.0f * ordinary[CAR_STAT_ACCELERATION] *
			(0.6f + ordinary[CAR_STAT_ACCELERATION]) *
			ordinary[CAR_STAT_ACCELERATION_RESPONSE_MULTIPLIER];
		base = std::max(target - difference * response - ordinary[CAR_STAT_DRAG], 0.0f);
		speed += 1000.0f * ordinary[CAR_STAT_FORWARD_THRUST_MULTIPLIER] * difference;
		const float ratio = speed / weight;
		speed -= ratio * ratio * 8.0f;
		if (216.0f * speed / weight >= target_95) {
			result.time_95 = static_cast<float>(frame + 1) / TICK_RATE;
			break;
		}
	}
	return result;
}

static void analyze_handling(
		const PhysicsCarProperties &properties,
		CarPerformanceRaw &out) {
	float normal[CAR_STAT_COUNT];
	float drift[CAR_STAT_COUNT];
	sample_effective_stats(properties, CAR_MODIFIER_LAYER_COUNT, 0.0f,
		CAR_MODIFIER_NO_BOOST, false, normal);
	sample_effective_stats(properties, CAR_MODIFIER_MTS, 1.0f,
		CAR_MODIFIER_NO_BOOST, false, drift);
	const float weight = safe_positive(normal[CAR_STAT_WEIGHT_KG], 1.0f);
	const float angular_inertia_y = std::max(45.0f * weight * 0.0625f, 1.0f);
	const float ordinary_steer = normal[CAR_STAT_TURN_MOVEMENT] +
		0.35f * normal[CAR_STAT_STRAFE_TURN];
	const float drift_steer = drift[CAR_STAT_TURN_MOVEMENT] +
		0.35f * drift[CAR_STAT_STRAFE_TURN];
	const float ordinary_yaw = 1.5f * ordinary_steer *
		std::abs(normal[CAR_STAT_TURN_REACTION]) / angular_inertia_y;
	const float drift_yaw = 1.5f * drift_steer *
		std::abs(drift[CAR_STAT_TURN_REACTION]) *
		safe_positive(drift[CAR_STAT_GRIP_2]) / angular_inertia_y;
	const float ordinary_retention = 1.0f /
		(1.0f + std::abs(normal[CAR_STAT_TURN_DECEL]) * STANDARD_SPEED_KMH * 0.02f);
	const float drift_retention = (1.0f + std::max(drift[CAR_STAT_DRIFT_ACCEL], 0.0f)) /
		(1.0f + std::abs(drift[CAR_STAT_TURN_DECEL]) * STANDARD_SPEED_KMH * 0.02f);
	out.components[CAR_PERFORMANCE_CORNERING][0].value = ordinary_yaw;
	out.components[CAR_PERFORMANCE_CORNERING][1].value = ordinary_retention;
	out.components[CAR_PERFORMANCE_CORNERING][2].value = drift_yaw;
	out.components[CAR_PERFORMANCE_CORNERING][3].value = drift_retention;

	const float breakaway = normal[CAR_STAT_GRIP_1] *
		(1.0f + std::max(normal[CAR_STAT_ACCEL_PRESS_GRIP_FRAMES], 0.0f) / 120.0f);
	const float restoring = normal[CAR_STAT_TURN_TENSION] * weight;
	const float unwanted_slide = safe_positive(normal[CAR_STAT_TURN_REACTION]) /
		safe_positive(restoring, 0.01f);
	const float regrip = normal[CAR_STAT_GRIP_3] *
		safe_positive(normal[CAR_STAT_TURN_TENSION]);
	const float drift_release = normal[CAR_STAT_GRIP_3] /
		safe_positive(normal[CAR_STAT_GRIP_2]);
	out.components[CAR_PERFORMANCE_GRIP][0].value = breakaway;
	out.components[CAR_PERFORMANCE_GRIP][1].value = -unwanted_slide;
	out.components[CAR_PERFORMANCE_GRIP][2].value = regrip;
	out.components[CAR_PERFORMANCE_GRIP][3].value = drift_release;
}

static void analyze_body(
		const PhysicsCarProperties &properties,
		const PhysicsCarProperties &all_rounder_reference,
		CarPerformanceRaw &out) {
	const float weight = safe_positive(properties.base_stats[CAR_STAT_WEIGHT_KG], 1.0f);
	const float body = safe_positive(properties.base_stats[CAR_STAT_BODY]);
	const float energy = std::max(properties.base_stats[CAR_STAT_MAX_ENERGY], 0.0f);
	const float reference_weight = safe_positive(
		all_rounder_reference.base_stats[CAR_STAT_WEIGHT_KG], 1.0f);
	const float relative_speed = STANDARD_SPEED_KMH / 216.0f;
	const float reduced_mass = weight * reference_weight / (weight + reference_weight);
	const float impact_strength = relative_speed * reduced_mass / reference_weight * 20.0f;
	const float energy_after = std::max(energy - impact_strength * body, 0.0f);
	const float self_velocity_change = 2.0f * reference_weight /
		(weight + reference_weight) * STANDARD_SPEED_KMH;
	float lever_sum = 0.0f;
	for (const SimVec3 &corner : properties.wall_corners) {
		lever_sum += std::sqrt(corner.x * corner.x + corner.z * corner.z);
	}
	const float lever = std::max(lever_sum * 0.25f, 0.25f);
	const float angular_inertia = 45.0f * weight * 0.0625f;
	out.components[CAR_PERFORMANCE_BODY][0].value = energy / body;
	out.components[CAR_PERFORMANCE_BODY][1].value = energy_after;
	out.components[CAR_PERFORMANCE_BODY][2].value = -self_velocity_change;
	out.components[CAR_PERFORMANCE_BODY][3].value = angular_inertia / lever;
}

static void analyze_air(
		const PhysicsCarProperties &properties,
		CarPerformanceRaw &out) {
	const float weight = safe_positive(properties.base_stats[CAR_STAT_WEIGHT_KG], 1.0f);
	float world_speed = STANDARD_SPEED_KMH * weight / 216.0f;
	float base_speed = world_speed / weight;
	float heading = 0.0f;
	const float pitch_factor = 0.35f;
	const float steer = 0.75f;
	for (uint32_t frame = 0; frame < 180; ++frame) {
		const float normalized = world_speed / weight;
		world_speed = std::max(world_speed - normalized * normalized * 8.0f, 0.0f);
		const float airtime = std::min(static_cast<float>(frame + 1) / TICK_RATE, 1.0f);
		const float authored_drag = pitch_factor *
			properties.base_stats[CAR_STAT_AIR_PITCH_UP_SPEED_LOSS_FACTOR] +
			std::sqrt(steer) * airtime *
			properties.base_stats[CAR_STAT_AIR_GLIDE_STEERING_SPEED_LOSS_FACTOR];
		base_speed = std::max(base_speed - base_speed * authored_drag, 0.0f);
		world_speed = std::min(world_speed, base_speed * weight);
		const float angular_inertia_y = std::max(45.0f * weight * 0.0625f, 1.0f);
		heading += 1.5f * properties.base_stats[CAR_STAT_TURN_MOVEMENT] *
			std::abs(properties.base_stats[CAR_STAT_TURN_REACTION]) /
			angular_inertia_y / TICK_RATE;
	}
	out.components[CAR_PERFORMANCE_AIR_CONTROL][0].value =
		STANDARD_SPEED_KMH > 0.0f ? 216.0f * world_speed / weight / STANDARD_SPEED_KMH : 0.0f;
	out.components[CAR_PERFORMANCE_AIR_CONTROL][1].value = heading;
}

} // namespace

const char *car_performance_category_name(CarPerformanceCategory category) {
	static constexpr const char *NAMES[CAR_PERFORMANCE_CATEGORY_COUNT] = {
		"Speed", "Acceleration", "Cornering", "Grip", "Booster", "Body", "Air Control"};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT ? NAMES[category] : "Unknown";
}

uint8_t car_performance_component_count(CarPerformanceCategory category) {
	static constexpr uint8_t COUNTS[CAR_PERFORMANCE_CATEGORY_COUNT] = {1, 5, 4, 4, 4, 4, 2};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT ? COUNTS[category] : 0;
}

const char *car_performance_component_name(CarPerformanceCategory category, uint8_t component) {
	static constexpr const char *NAMES[CAR_PERFORMANCE_CATEGORY_COUNT][CAR_PERFORMANCE_MAX_COMPONENTS] = {
		{"Settled terminal speed", nullptr, nullptr, nullptr, nullptr, nullptr},
		{"Speed after 1 second", "Speed after 3 seconds", "Five-second speed area", "Time to 300 km/h", "Time to 500 km/h", nullptr},
		{"Ordinary turn authority", "Ordinary speed retention", "Drift turn authority", "Drift speed retention", nullptr, nullptr},
		{"Breakaway resistance", "Unwanted slide", "Re-grip tendency", "Drift release", nullptr, nullptr},
		{"Initial speed advantage", "Peak speed advantage", "One-reserve distance advantage", "Energy efficiency", nullptr, nullptr},
		{"Effective durability", "Energy after reference impact", "Impact stability", "Rotation stability", nullptr, nullptr},
		{"Airborne speed retention", "Controlled heading change", nullptr, nullptr, nullptr, nullptr}};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT && component < CAR_PERFORMANCE_MAX_COMPONENTS
		? NAMES[category][component] : nullptr;
}

const char *car_performance_component_unit(CarPerformanceCategory category, uint8_t component) {
	static constexpr const char *UNITS[CAR_PERFORMANCE_CATEGORY_COUNT][CAR_PERFORMANCE_MAX_COMPONENTS] = {
		{"km/h", nullptr, nullptr, nullptr, nullptr, nullptr},
		{"km/h", "km/h", "km/h*s", "seconds", "seconds", nullptr},
		{"yaw/tick", "ratio", "yaw/tick", "ratio", nullptr, nullptr},
		{"threshold", "inverse", "response", "response", nullptr, nullptr},
		{"km/h", "km/h", "meters", "energy/tick", nullptr, nullptr},
		{"effective energy", "energy", "stability", "inertia", nullptr, nullptr},
		{"ratio", "heading", nullptr, nullptr, nullptr, nullptr}};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT && component < CAR_PERFORMANCE_MAX_COMPONENTS
		? UNITS[category][component] : "scalar";
}

bool analyze_car_performance(
		const PhysicsCarProperties &properties,
		const PhysicsCarProperties &all_rounder_reference,
		CarPerformanceRaw &out_analysis) {
	out_analysis = CarPerformanceRaw{};
	std::memcpy(out_analysis.base_stats, properties.base_stats, sizeof(out_analysis.base_stats));
	const DriveResult ordinary = run_drive(properties, false);
	const DriveResult boosted = run_drive(properties, true);
	out_analysis.terminal_speed_kmh = ordinary.terminal_speed;
	out_analysis.peak_boost_speed_kmh = boosted.peak_speed;
	out_analysis.time_to_95_seconds = ordinary.time_95;
	out_analysis.settlement_confidence = ordinary.settlement_confidence;
	out_analysis.components[CAR_PERFORMANCE_SPEED][0].value = ordinary.terminal_speed;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][0].value = ordinary.speed_1s;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][1].value = ordinary.speed_3s;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][2].value = ordinary.area_5s;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][3].value = -ordinary.time_300;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][4].value = -ordinary.time_500;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][0].value = boosted.speed_1s - ordinary.speed_1s;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][1].value = boosted.peak_speed - ordinary.peak_speed;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][2].value = boosted.distance - ordinary.distance;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][3].value =
		properties.base_stats[CAR_STAT_MAX_ENERGY] /
		safe_positive(properties.base_stats[CAR_STAT_BOOST_ENERGY_USE_RATE]);
	analyze_handling(properties, out_analysis);
	analyze_body(properties, all_rounder_reference, out_analysis);
	analyze_air(properties, out_analysis);
	for (uint8_t category = 0; category < CAR_PERFORMANCE_CATEGORY_COUNT; ++category) {
		for (uint8_t component = 0; component < car_performance_component_count(
				static_cast<CarPerformanceCategory>(category)); ++component) {
			if (!std::isfinite(out_analysis.components[category][component].value)) return false;
		}
	}
	return true;
}
