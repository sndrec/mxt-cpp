#pragma once

#include "core/player_input.h"
#include "core/sim_math.h"

#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <type_traits>

namespace godot {

struct GameSimNetworkStateSizeStats {
	int total = 0;
	int header = 0;
	int bumper_meta = 0;
	int sparks = 0;
	int car_scalars = 0;
	int bumper_scalars = 0;
	int car_vec3 = 0;
	int bumper_vec3 = 0;
	int car_transform = 0;
	int bumper_transform = 0;
	int car_basis = 0;
	int bumper_basis = 0;
	int car_conditionals = 0;
	int bumper_conditionals = 0;
	int car_tilt = 0;
	int bumper_tilt = 0;
	int car_wall = 0;
	int bumper_wall = 0;
	int triggers = 0;
	int car_restore_count = 0;
	int bumper_restore_count = 0;
	int active_bumper_count = 0;
	int active_spark_count = 0;
	int trigger_count = 0;
	int car_count = 0;
	int bumper_count = 0;
};

struct GameSimVehicleTickSoA {
	int capacity = 0;
	PlayerInput *inputs = nullptr;
	float *pre_distances = nullptr;
	float *placement_distances = nullptr;
	int *placement_indices = nullptr;
	bool placement_order_valid = false;
	uint8_t *pending_s_boost_sparks = nullptr;
	int *collision_indices = nullptr;
	int collision_order_count = 0;
	float *collision_min_x = nullptr;
	float *collision_max_x = nullptr;
	float *collision_min_y = nullptr;
	float *collision_max_y = nullptr;
	float *collision_min_z = nullptr;
	float *collision_max_z = nullptr;
	float *position_current_x = nullptr;
	float *position_current_y = nullptr;
	float *position_current_z = nullptr;
	float *position_old_x = nullptr;
	float *position_old_y = nullptr;
	float *position_old_z = nullptr;
	float *speed_kmh = nullptr;
	float *collectable_super_spark = nullptr;
};

static constexpr int MXT_GAMESIM_SUPER_SPARK_CAPACITY = 256;

struct GameSimSuperSpark {
	uint8_t active = 0;
	uint8_t collectable = 0;
	uint16_t animation_frame = 0;
	uint16_t checkpoint = 0;
	SimVec3 position;
	SimVec3 prev_position;
	SimVec3 start_position;
	SimVec3 final_position;
	SimVec3 plane_normal;
};

struct GameSimSuperSparkState {
	GameSimSuperSpark sparks[MXT_GAMESIM_SUPER_SPARK_CAPACITY];
	uint16_t cursor = 0;
	uint32_t rng_state = 0;
	uint32_t placement_timer = 0;
};

struct GameSimNativeCpuDriverState {
	int32_t player_id = -1;
	uint8_t active = 0;
	int32_t last_generated_tick = -1;
	PackedByteArray pending_input;
};

static_assert(std::is_standard_layout_v<GameSimVehicleTickSoA>);
static_assert(std::is_standard_layout_v<GameSimSuperSpark>);

} // namespace godot
