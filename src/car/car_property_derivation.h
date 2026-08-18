#pragma once

#include "car/car_properties.h"

#include <cstdint>

struct CarPropertyDerivationResult {
	float base_stats[CAR_STAT_COUNT];
	uint64_t derived_stat_mask;
};

CarPropertyDerivationResult
derive_historical_machine_setting(const float source_base_stats[CAR_STAT_COUNT],
								  float machine_setting);

bool historical_machine_setting_derives_stat(CarStatId stat);
