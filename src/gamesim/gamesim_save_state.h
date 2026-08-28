#pragma once

#include "core/sim_math.h"

#include <cstdint>
#include <vector>

namespace godot {

static constexpr int MXT_GAMESIM_BUMPER_POOL_SIZE = 60;

struct GameSimSavedVoiceTransform {
	int32_t player_id = -1;
	SimVec3 origin;
};

struct GameSimBumperState {
	uint8_t active = 0;
	uint32_t spawn_lap = 0;
	uint32_t next_sequence = 0;
	float target_lane = 0.0f;
};

struct GameSimSavedState {
	char *data = nullptr;
	int size = 0;
	int bumper_state_count = 0;
	uint32_t bumper_scheduler_lap = 0;
	uint32_t bumper_next_sequence = 0;
	GameSimBumperState bumper_states[MXT_GAMESIM_BUMPER_POOL_SIZE];
	int tick = -1;
	int voice_transform_count = 0;
	std::vector<GameSimSavedVoiceTransform> voice_transforms;
	uint32_t car_local_state_size = 0;
	uint32_t bumper_local_state_size = 0;
	std::vector<uint8_t> vehicle_local_state;
};

} // namespace godot
