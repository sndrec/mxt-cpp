#pragma once

#include "car/car_properties.h"

#include <cstdint>

enum CarAuthoringSpecialLayer : uint8_t {
	CAR_AUTHORING_MTS = 0,
	CAR_AUTHORING_QUICKTURN,
	CAR_AUTHORING_NO_BOOST,
	CAR_AUTHORING_MANUAL_BOOST,
	CAR_AUTHORING_DASHPLATE_BOOST,
	CAR_AUTHORING_STACKED_BOOST,
	CAR_AUTHORING_S_BOOST,
	CAR_AUTHORING_SPECIAL_LAYER_COUNT
};

bool car_special_pair_is_supported(CarAuthoringSpecialLayer layer, CarStatId stat);
float derive_car_special_value(
		CarAuthoringSpecialLayer layer,
		CarStatId stat,
		const float base_stats[CAR_STAT_COUNT]);

