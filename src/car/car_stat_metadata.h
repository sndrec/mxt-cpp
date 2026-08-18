#pragma once

#include "car/car_properties.h"

#include <cstdint>

enum CarStatActivity : uint8_t {
	CAR_STAT_ACTIVITY_GAMEPLAY = 0,
	CAR_STAT_ACTIVITY_PRESENTATION,
	CAR_STAT_ACTIVITY_ASSIGNED_UNUSED
};

enum CarStatDirection : uint8_t {
	CAR_STAT_DIRECTION_NONE = 0,
	CAR_STAT_DIRECTION_HIGHER_BENEFIT,
	CAR_STAT_DIRECTION_LOWER_BENEFIT,
	CAR_STAT_DIRECTION_CONTEXT_DEPENDENT
};

struct CarStatMetadata {
	const char *name;
	const char *friendly_name;
	const char *explanation;
	const char *unit;
	const char *authoring_category;
	CarStatActivity activity;
	bool raw_gradeable;
	CarStatDirection base_direction;
	CarStatDirection special_direction;
};

const CarStatMetadata &get_car_stat_metadata(CarStatId stat);
const char *car_stat_activity_name(CarStatActivity activity);
const char *car_stat_direction_name(CarStatDirection direction);
