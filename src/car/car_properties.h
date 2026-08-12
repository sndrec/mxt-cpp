#pragma once

#include "mxt_core/sim_math.h"

#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/string.hpp"

#include <cstdint>

enum CarStatId : uint16_t {
	CAR_STAT_WEIGHT_KG = 0,
	CAR_STAT_ACCELERATION,
	CAR_STAT_MAX_SPEED,
	CAR_STAT_GRIP_1,
	CAR_STAT_GRIP_2,
	CAR_STAT_GRIP_3,
	CAR_STAT_TURN_TENSION,
	CAR_STAT_DRIFT_ACCEL,
	CAR_STAT_TURN_MOVEMENT,
	CAR_STAT_STRAFE_TURN,
	CAR_STAT_STRAFE,
	CAR_STAT_TURN_REACTION,
	CAR_STAT_TURN_DECEL,
	CAR_STAT_DRAG,
	CAR_STAT_BODY,
	CAR_STAT_CAMERA_REORIENTING,
	CAR_STAT_CAMERA_REPOSITIONING,
	CAR_STAT_TRACK_COLLISION,
	CAR_STAT_OBSTACLE_COLLISION,
	CAR_STAT_MAX_ENERGY,
	CAR_STAT_BOOST_ENERGY_USE_RATE,
	CAR_STAT_ENERGY_RECHARGE_RATE,
	CAR_STAT_ACCEL_PRESS_GRIP_FRAMES,
	CAR_STAT_MANUAL_TURBO_GAIN,
	CAR_STAT_DASHPLATE_TURBO_GAIN,
	CAR_STAT_JUMPPLATE_TURBO_GAIN,
	CAR_STAT_DASHPLATE_TURBO_HEAT_MULTIPLIER,
	CAR_STAT_TURBO_FLAT_LOSS_PER_SECOND,
	CAR_STAT_TURBO_PERCENT_LOSS_PER_SECOND,
	CAR_STAT_TURBO_TOP_SPEED_EFFECT,
	CAR_STAT_MANUAL_BOOST_DURATION_SECONDS,
	CAR_STAT_DASHPLATE_BOOST_DURATION_SECONDS,
	CAR_STAT_S_BOOST_BASE_SPEED_ADD_PER_SECOND,
	CAR_STAT_SHIFT_BOOST_BASE_SPEED_ADD,
	CAR_STAT_SHIFT_BOOST_VELOCITY_MULTIPLIER,
	CAR_STAT_AIR_PITCH_UP_SPEED_LOSS_FACTOR,
	CAR_STAT_AIR_GLIDE_STEERING_SPEED_LOSS_FACTOR,
	CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER,
	CAR_STAT_ACCELERATION_RESPONSE_MULTIPLIER,
	CAR_STAT_FORWARD_THRUST_MULTIPLIER,
	CAR_STAT_COUNT
};

enum CarStatCurveLayer : uint8_t {
	CAR_CURVE_BASE = 0,
	CAR_CURVE_MTS,
	CAR_CURVE_QUICKTURN,
	CAR_CURVE_NO_BOOST,
	CAR_CURVE_MANUAL_BOOST,
	CAR_CURVE_DASHPLATE_BOOST,
	CAR_CURVE_STACKED_BOOST,
	CAR_CURVE_LAYER_COUNT
};

enum CarStatModifierLayer : uint8_t {
	CAR_MODIFIER_MTS = 0,
	CAR_MODIFIER_QUICKTURN,
	CAR_MODIFIER_NO_BOOST,
	CAR_MODIFIER_MANUAL_BOOST,
	CAR_MODIFIER_DASHPLATE_BOOST,
	CAR_MODIFIER_STACKED_BOOST,
	CAR_MODIFIER_LAYER_COUNT
};

static constexpr uint64_t MXT_CAR_PROPS_SCHEMA_FINGERPRINT = UINT64_C(0xf201716eab2f6cee);

struct PhysicsCarProperties {
	float base_stats[CAR_STAT_COUNT];
	float modifier_stats[CAR_MODIFIER_LAYER_COUNT][CAR_STAT_COUNT];
	float s_boost_stats[CAR_STAT_COUNT];

	SimVec3 tilt_corners[4];
	SimVec3 wall_corners[4];
	uint32_t state_flags;

	PhysicsCarProperties();

	static bool deserialize_and_sample(
		const godot::PackedByteArray &bytes,
		float machine_setting,
		PhysicsCarProperties &out_properties,
		godot::String &out_error);

	static bool stat_supports_live_modifiers(CarStatId stat);
	static const char *stat_name(CarStatId stat);
};

CarStatModifierLayer classify_car_technique_modifier(
	bool genuinely_drifting, float strafe_input, float signed_slip,
	float &out_intensity);
CarStatModifierLayer classify_car_boost_modifier(
	bool manual_boost_active, bool dashplate_boost_active, bool s_boost_active);
float evaluate_car_stat(
	const PhysicsCarProperties &properties, CarStatId stat,
	CarStatModifierLayer technique_layer, float technique_intensity,
	CarStatModifierLayer boost_layer, bool s_boost_active);
