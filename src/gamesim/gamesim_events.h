#pragma once

#include <cstdint>
#include <type_traits>

namespace godot {

struct GameSimRaceEvent {
	uint8_t type = 0;
	int32_t actor_id = -1;
	int32_t target_id = -1;
	int32_t tick = 0;
	int32_t value = 0;
};

static_assert(std::is_standard_layout_v<GameSimRaceEvent>);

} // namespace godot
