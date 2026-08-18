#include "car/car_special_state_derivation.h"

bool car_special_pair_is_supported(CarAuthoringSpecialLayer layer, CarStatId stat)
{
	return layer < CAR_AUTHORING_SPECIAL_LAYER_COUNT && stat < CAR_STAT_COUNT &&
		PhysicsCarProperties::stat_supports_live_modifiers(stat);
}

float derive_car_special_value(
		CarAuthoringSpecialLayer layer,
		CarStatId stat,
		const float base_stats[CAR_STAT_COUNT])
{
	if (!car_special_pair_is_supported(layer, stat)) return 0.0f;
	if (layer == CAR_AUTHORING_S_BOOST) return base_stats[stat];

	const bool boosted = layer == CAR_AUTHORING_MANUAL_BOOST ||
		layer == CAR_AUTHORING_DASHPLATE_BOOST || layer == CAR_AUTHORING_STACKED_BOOST;
	if (!boosted) return 1.0f;

	switch (stat) {
		case CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND:
			return 0.5f;
		case CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND:
			return 0.6f;
		case CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER:
			return 1.0f + base_stats[CAR_STAT_MANUAL_TURBO_GAIN] *
				base_stats[CAR_STAT_ACCELERATION] * 0.038f;
		case CAR_STAT_ACCELERATION_RESPONSE_MULTIPLIER:
			return base_stats[CAR_STAT_WEIGHT_KG] <= 1000.0f ? 0.3f : 0.5f;
		case CAR_STAT_FORWARD_THRUST_MULTIPLIER:
			return base_stats[CAR_STAT_WEIGHT_KG] <= 1000.0f ? 1.2f : 1.6f;
		default:
			return 1.0f;
	}
}

