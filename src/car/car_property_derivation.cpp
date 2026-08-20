#include "car/car_property_derivation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

static constexpr uint64_t HISTORICAL_DERIVED_STAT_MASK =
	(UINT64_C(1) << CAR_STAT_ACCELERATION) | (UINT64_C(1) << CAR_STAT_MAX_SPEED) |
	(UINT64_C(1) << CAR_STAT_GRIP_1) | (UINT64_C(1) << CAR_STAT_GRIP_3) |
	(UINT64_C(1) << CAR_STAT_DRIFT_ACCEL) | (UINT64_C(1) << CAR_STAT_TURN_MOVEMENT) |
	(UINT64_C(1) << CAR_STAT_DRIFT_TURN_MOVEMENT) |
	(UINT64_C(1) << CAR_STAT_TURN_DECEL) | (UINT64_C(1) << CAR_STAT_MANUAL_TURBO_GAIN) |
	(UINT64_C(1) << CAR_STAT_DASHPLATE_TURBO_GAIN);

static_assert(CAR_STAT_COUNT <= 64);

} // namespace

CarPropertyDerivationResult
derive_historical_machine_setting(const float source_base_stats[CAR_STAT_COUNT],
								  float machine_setting) {
	CarPropertyDerivationResult result{};
	std::memcpy(result.base_stats, source_base_stats, sizeof(result.base_stats));
	result.derived_stat_mask = HISTORICAL_DERIVED_STAT_MASK;

	const float setting = std::clamp(machine_setting, 0.0f, 1.0f);
	const float balance_offset = setting - 0.5f;
	float &drift_accel = result.base_stats[CAR_STAT_DRIFT_ACCEL];
	if (balance_offset <= 0.0f) {
		const float source_drift_accel = drift_accel;
		if (source_drift_accel >= 1.0f) {
			if (source_drift_accel >= 1.5f) {
				drift_accel = source_drift_accel - (1.2f - (source_drift_accel - 1.5f)) *
													   (source_drift_accel * balance_offset);
			} else {
				drift_accel = source_drift_accel - 1.2f * (source_drift_accel * balance_offset);
			}
		} else {
			drift_accel =
				source_drift_accel - 2.0f * ((2.0f - source_drift_accel) * balance_offset);
		}
		drift_accel = std::min(drift_accel, 2.3f);
	} else if (drift_accel > 1.0f) {
		drift_accel -= 1.8f * (drift_accel * balance_offset);
	}

	float &acceleration = result.base_stats[CAR_STAT_ACCELERATION];
	float &max_speed = result.base_stats[CAR_STAT_MAX_SPEED];
	const bool should_modify_boost =
		!(balance_offset < 0.0f && acceleration >= 0.5f && max_speed <= 0.2f);

	float max_speed_delta = 0.0f;
	if (balance_offset <= 0.0f) {
		const float normalized_speed = std::min((max_speed - 0.12f) / 0.08f, 1.0f);
		max_speed_delta = 0.45f * (0.4f + 0.2f * normalized_speed);
	} else {
		float speed_factor = 1.0f;
		if (acceleration >= 0.4f) {
			if (acceleration >= 0.5f && max_speed >= 0.15f) {
				speed_factor = -0.25f;
			}
		} else {
			speed_factor = 3.2f;
		}
		max_speed_delta = 0.16f * speed_factor;
	}
	max_speed_delta = balance_offset * std::abs(1.0f - max_speed) * max_speed_delta;

	if (acceleration <= 0.6f || balance_offset >= 0.0f) {
		acceleration += 0.6f * -balance_offset * std::abs(acceleration);
	} else {
		acceleration += 2.0f * balance_offset * std::abs(0.7f - acceleration);
	}

	float &turn_decel = result.base_stats[CAR_STAT_TURN_DECEL];
	if (acceleration < 0.4f) {
		float decel_factor = 1.0f;
		if (acceleration < 0.31f) {
			max_speed_delta *= 1.5f;
			decel_factor = 1.5f;
		}
		if (turn_decel > 0.03f) {
			decel_factor *= 1.5f;
		}
		if (balance_offset < 0.0f) {
			decel_factor *= 2.0f;
		}
		turn_decel -= std::abs(0.7f * decel_factor * (turn_decel * balance_offset));
		turn_decel = std::max(turn_decel, 0.01f);
	}

	if (result.base_stats[CAR_STAT_WEIGHT_KG] < 700.0f && acceleration > 0.7f) {
		acceleration = 0.7f;
	}
	max_speed += max_speed_delta;

	float &turn_movement = result.base_stats[CAR_STAT_TURN_MOVEMENT];
	const float turn_movement_multiplier = balance_offset <= 0.0f
		? 1.0f - 0.2f * balance_offset
		: 1.0f - 0.6f * balance_offset;
	turn_movement *= turn_movement_multiplier;
	result.base_stats[CAR_STAT_DRIFT_TURN_MOVEMENT] *= turn_movement_multiplier;

	const float grip_scaling = 1.0f + 0.25f * balance_offset;
	result.base_stats[CAR_STAT_GRIP_1] *= grip_scaling;
	result.base_stats[CAR_STAT_GRIP_3] *= grip_scaling;
	if (should_modify_boost) {
		// The active runtime stores the historical boost_strength after its 0.57
		// conversion. Manual and dashplate turbo were both sourced from that one
		// value, so the same setting multiplier applies to both active gains.
		const float boost_multiplier = 1.0f + 0.1f * balance_offset;
		result.base_stats[CAR_STAT_MANUAL_TURBO_GAIN] *= boost_multiplier;
		result.base_stats[CAR_STAT_DASHPLATE_TURBO_GAIN] *= boost_multiplier;
	}
	return result;
}

bool historical_machine_setting_derives_stat(CarStatId stat) {
	return stat < CAR_STAT_COUNT && (HISTORICAL_DERIVED_STAT_MASK & (UINT64_C(1) << stat)) != 0;
}
