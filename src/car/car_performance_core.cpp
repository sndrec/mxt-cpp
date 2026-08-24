#include "car/car_performance_core.h"

#include "gamesim/gamesim.h"

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
	float terminal_base_speed = 0.0f;
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
		bool spend_full_boost_reserve,
		float initial_speed_kmh,
		float initial_base_speed,
		bool measure_launch) {
	float ordinary[CAR_STAT_COUNT];
	float boosted[CAR_STAT_COUNT];
	sample_effective_stats(properties, CAR_MODIFIER_LAYER_COUNT, 0.0f,
		CAR_MODIFIER_NO_BOOST, false, ordinary);
	sample_effective_stats(properties, CAR_MODIFIER_LAYER_COUNT, 0.0f,
		CAR_MODIFIER_MANUAL_BOOST, false, boosted);

	const float weight = safe_positive(ordinary[CAR_STAT_WEIGHT_KG], 1.0f);
	float world_speed = initial_speed_kmh * weight / 216.0f;
	float base_speed = initial_base_speed;
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
	result.terminal_base_speed = base_speed;
	const float allowed_span = std::max(std::abs(result.terminal_speed) * 0.01f, 0.25f);
	result.settlement_confidence = std::clamp(
		1.0f - (terminal_max - terminal_min) / allowed_span, 0.0f, 1.0f);

	// A bounded second pass finds the descriptive time to 95% of the settled result.
	if (!measure_launch) return result;
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

static bool analyze_handling(
		const godot::PackedByteArray &property_document,
		float machine_setting,
		const PhysicsCarProperties &properties,
		float settled_speed_kmh,
		float settled_base_speed,
		CarPerformanceRaw &out) {
	float normal[CAR_STAT_COUNT];
	sample_effective_stats(properties, CAR_MODIFIER_LAYER_COUNT, 0.0f,
		CAR_MODIFIER_NO_BOOST, false, normal);
	const float *drift = normal;
	const float weight = safe_positive(normal[CAR_STAT_WEIGHT_KG], 1.0f);
	const float benchmark_speed_kmh = std::max(finite_or(settled_speed_kmh), 0.0f);
	float ordinary_yaw = 0.0f;
	float drift_yaw = 0.0f;
	bool drift_observed = false;
	if (!godot::GameSim::measure_flat_ground_steering(
			property_document, machine_setting, benchmark_speed_kmh,
			settled_base_speed, ordinary_yaw, drift_yaw, drift_observed)) {
		return false;
	}
	const float ordinary_retention = 1.0f /
		(1.0f + std::abs(normal[CAR_STAT_TURN_DECEL]) * benchmark_speed_kmh * 0.02f);
	const float drift_retention = (1.0f + std::max(drift[CAR_STAT_DRIFT_ACCEL], 0.0f)) /
		(1.0f + std::abs(drift[CAR_STAT_TURN_DECEL]) * benchmark_speed_kmh * 0.02f);
	out.components[CAR_PERFORMANCE_CORNERING][0].value = ordinary_yaw;
	out.components[CAR_PERFORMANCE_CORNERING][1].value = ordinary_retention;
	out.components[CAR_PERFORMANCE_CORNERING][2].value = drift_yaw;
	out.components[CAR_PERFORMANCE_CORNERING][2].available = drift_observed;
	out.components[CAR_PERFORMANCE_CORNERING][3].value = drift_retention;

	const float breakaway = normal[CAR_STAT_GRIP_1];
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
	return true;
}

static void analyze_body(
		const PhysicsCarProperties &properties,
		CarPerformanceRaw &out) {
	const float body = safe_positive(properties.base_stats[CAR_STAT_BODY]);
	const float energy = std::max(properties.base_stats[CAR_STAT_MAX_ENERGY], 0.0f);
	out.components[CAR_PERFORMANCE_BODY][0].value = energy;
	out.components[CAR_PERFORMANCE_BODY][1].value = 1.0f / body;
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
	static constexpr uint8_t COUNTS[CAR_PERFORMANCE_CATEGORY_COUNT] = {1, 5, 4, 4, 4, 2, 2};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT ? COUNTS[category] : 0;
}

const char *car_performance_component_name(CarPerformanceCategory category, uint8_t component) {
	static constexpr const char *NAMES[CAR_PERFORMANCE_CATEGORY_COUNT][CAR_PERFORMANCE_MAX_COMPONENTS] = {
		{"Top Speed", nullptr, nullptr, nullptr, nullptr, nullptr},
		{"Speed After 1 Second", "Speed After 3 Seconds", "Distance in First 5 Seconds", "Time to 300 km/h", "Time to 500 km/h", nullptr},
		{"Normal Steering", "Normal Cornering Speed", "Drift Steering", "Drift Speed", nullptr, nullptr},
		{"Resistance to Sliding", "Unwanted Slide Tendency", "Re-grip Strength", "Drift Recovery", nullptr, nullptr},
		{"Boost Speed After 1 Second", "Peak Boost Speed", "Full-Energy Distance Gain", "Full-Energy Boost Time", nullptr, nullptr},
		{"Max Energy", "Damage Resistance", nullptr, nullptr, nullptr, nullptr},
		{"Airborne Speed Retained", "Air Turning", nullptr, nullptr, nullptr, nullptr}};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT && component < CAR_PERFORMANCE_MAX_COMPONENTS
		? NAMES[category][component] : nullptr;
}

const char *car_performance_component_unit(CarPerformanceCategory category, uint8_t component) {
	static constexpr const char *UNITS[CAR_PERFORMANCE_CATEGORY_COUNT][CAR_PERFORMANCE_MAX_COMPONENTS] = {
		{"km/h", nullptr, nullptr, nullptr, nullptr, nullptr},
		{"km/h", "km/h", "meters", "seconds", "seconds", nullptr},
		{"degrees/second", "ratio", "degrees/second", "ratio", nullptr, nullptr},
		{"threshold", "inverse", "response", "response", nullptr, nullptr},
		{"km/h", "km/h", "meters", "seconds", nullptr, nullptr},
		{"energy", "multiplier", nullptr, nullptr, nullptr, nullptr},
		{"ratio", "heading", nullptr, nullptr, nullptr, nullptr}};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT && component < CAR_PERFORMANCE_MAX_COMPONENTS
		? UNITS[category][component] : "scalar";
}

const char *car_performance_component_explanation(CarPerformanceCategory category, uint8_t component) {
	static constexpr const char *EXPLANATIONS[CAR_PERFORMANCE_CATEGORY_COUNT][CAR_PERFORMANCE_MAX_COMPONENTS] = {
		{"Average straight-line speed over the final two seconds of a 30-second unboosted run.", nullptr, nullptr, nullptr, nullptr, nullptr},
		{"Straight-line speed one second into an unboosted launch.",
		 "Straight-line speed three seconds into an unboosted launch.",
		 "Ground covered during the first five seconds of an unboosted launch.",
		 "Time needed to reach 300 km/h during an unboosted launch. Less time earns a higher grade.",
		 "Time needed to reach 500 km/h during an unboosted launch. Less time earns a higher grade.", nullptr},
		{"Sustained turning rate at the machine's settled unboosted speed on flat ground with full steering input.",
		 "How much speed the machine preserves while cornering normally at its settled unboosted speed.",
		 "Sustained turning rate at the machine's settled unboosted speed during a full-input drift on flat ground.",
		 "How much speed the machine preserves during a normal drift at its settled unboosted speed.", nullptr, nullptr},
		{"How strongly the machine's base grip resists entering a slide.",
		 "How readily steering can overpower the machine's restoring grip. Less unwanted sliding earns a higher grade.",
		 "How strongly the machine settles back into a planted state after sliding.",
		 "How readily the machine settles into a planted state at the end of a drift.", nullptr, nullptr},
		{"Absolute speed one second after beginning repeated manual boosts from settled top speed.",
		 "Highest absolute speed reached while spending one full energy reserve from settled top speed.",
		 "Extra distance covered versus normal driving from the same settled speed while spending one full energy reserve.",
		 "Approximate seconds of manual boost supplied by a full energy reserve.", nullptr, nullptr},
		{"The machine's base maximum energy reserve.",
		 "Reciprocal of its damage-taken multiplier. 1.00x is standard; 2.00x requires twice as much incoming damage to remove the same energy.", nullptr, nullptr, nullptr, nullptr},
		{"Fraction of 600 km/h retained through a fixed three-second pitched and steered jump.",
		 "Heading change achieved during the same fixed three-second jump.", nullptr, nullptr, nullptr, nullptr}};
	return category < CAR_PERFORMANCE_CATEGORY_COUNT && component < CAR_PERFORMANCE_MAX_COMPONENTS
		? EXPLANATIONS[category][component] : "No explanation is available for this benchmark.";
}

bool analyze_car_performance(
		const godot::PackedByteArray &property_document,
		float machine_setting,
		const PhysicsCarProperties &properties,
		CarPerformanceRaw &out_analysis) {
	out_analysis = CarPerformanceRaw{};
	std::memcpy(out_analysis.base_stats, properties.base_stats, sizeof(out_analysis.base_stats));
	const DriveResult ordinary = run_drive(properties, false, 0.0f, 0.0f, true);
	const DriveResult cruise = run_drive(properties, false,
		ordinary.terminal_speed, ordinary.terminal_base_speed, false);
	const DriveResult boosted = run_drive(properties, true,
		ordinary.terminal_speed, ordinary.terminal_base_speed, false);
	out_analysis.terminal_speed_kmh = ordinary.terminal_speed;
	out_analysis.peak_boost_speed_kmh = boosted.peak_speed;
	out_analysis.time_to_95_seconds = ordinary.time_95;
	out_analysis.settlement_confidence = ordinary.settlement_confidence;
	out_analysis.components[CAR_PERFORMANCE_SPEED][0].value = ordinary.terminal_speed;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][0].value = ordinary.speed_1s;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][1].value = ordinary.speed_3s;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][2].value = ordinary.area_5s / 3.6f;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][3].value = -ordinary.time_300;
	out_analysis.components[CAR_PERFORMANCE_ACCELERATION][4].value = -ordinary.time_500;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][0].value = boosted.speed_1s;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][1].value = boosted.peak_speed;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][2].value = boosted.distance - cruise.distance;
	out_analysis.components[CAR_PERFORMANCE_BOOSTER][3].value =
		properties.base_stats[CAR_STAT_MAX_ENERGY] /
		(10.0f * safe_positive(properties.base_stats[CAR_STAT_BOOST_ENERGY_USE_RATE]));
	if (!analyze_handling(property_document, machine_setting, properties,
			ordinary.terminal_speed, ordinary.terminal_base_speed, out_analysis)) {
		return false;
	}
	analyze_body(properties, out_analysis);
	analyze_air(properties, out_analysis);
	for (uint8_t category = 0; category < CAR_PERFORMANCE_CATEGORY_COUNT; ++category) {
		for (uint8_t component = 0; component < car_performance_component_count(
				static_cast<CarPerformanceCategory>(category)); ++component) {
			const CarPerformanceComponent &result = out_analysis.components[category][component];
			if (result.available && !std::isfinite(result.value)) return false;
		}
	}
	return true;
}
