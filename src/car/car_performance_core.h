#pragma once

#include "car/car_properties.h"

#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>

enum CarPerformanceCategory : uint8_t {
	CAR_PERFORMANCE_SPEED = 0,
	CAR_PERFORMANCE_ACCELERATION,
	CAR_PERFORMANCE_CORNERING,
	CAR_PERFORMANCE_GRIP,
	CAR_PERFORMANCE_BOOSTER,
	CAR_PERFORMANCE_BODY,
	CAR_PERFORMANCE_AIR_CONTROL,
	CAR_PERFORMANCE_CATEGORY_COUNT
};

static constexpr uint8_t CAR_PERFORMANCE_MAX_COMPONENTS = 6;

struct CarPerformanceComponent {
	float value = 0.0f;
};

struct CarPerformanceRaw {
	CarPerformanceComponent components[CAR_PERFORMANCE_CATEGORY_COUNT][CAR_PERFORMANCE_MAX_COMPONENTS];
	float terminal_speed_kmh = 0.0f;
	float peak_boost_speed_kmh = 0.0f;
	float time_to_95_seconds = 0.0f;
	float settlement_confidence = 0.0f;
	float base_stats[CAR_STAT_COUNT]{};
};

const char *car_performance_category_name(CarPerformanceCategory category);
uint8_t car_performance_component_count(CarPerformanceCategory category);
const char *car_performance_component_name(CarPerformanceCategory category, uint8_t component);
const char *car_performance_component_unit(CarPerformanceCategory category, uint8_t component);
const char *car_performance_component_explanation(CarPerformanceCategory category, uint8_t component);

bool analyze_car_performance(
		const godot::PackedByteArray &property_document,
		float machine_setting,
		const PhysicsCarProperties &properties,
		CarPerformanceRaw &out_analysis);
