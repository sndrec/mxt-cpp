#include "gamesim/gamesim_cpu_internal.h"

using namespace godot;
GameSim::NativeCpuDriverState* GameSim::find_native_cpu_driver(int32_t player_id)
{
	const int car_index = find_car_index_for_player(player_id);
	if (car_index >= 0 && car_index < static_cast<int>(native_cpu_drivers.size())) {
		NativeCpuDriverState& driver = native_cpu_drivers[car_index];
		return driver.active ? &driver : nullptr;
	}
	return nullptr;
}

void GameSim::clear_player_index_lookup()
{
	for (int i = 0; i < PLAYER_INDEX_LOOKUP_SIZE; ++i) {
		player_index_lookup_ids[i] = PLAYER_INDEX_LOOKUP_EMPTY;
		player_index_lookup_indices[i] = -1;
	}
}

void GameSim::insert_player_index_lookup(int32_t player_id, int car_index)
{
	uint32_t slot = native_cpu_hash_u32(static_cast<uint32_t>(player_id)) & PLAYER_INDEX_LOOKUP_MASK;
	for (int probe = 0; probe < PLAYER_INDEX_LOOKUP_SIZE; ++probe) {
		if (player_index_lookup_ids[slot] == player_id) {
			return;
		}
		if (player_index_lookup_ids[slot] == PLAYER_INDEX_LOOKUP_EMPTY) {
			player_index_lookup_ids[slot] = player_id;
			player_index_lookup_indices[slot] = static_cast<int16_t>(car_index);
			return;
		}
		slot = (slot + 1) & PLAYER_INDEX_LOOKUP_MASK;
	}
}

int GameSim::find_car_index_for_player(int32_t player_id) const
{
	uint32_t slot = native_cpu_hash_u32(static_cast<uint32_t>(player_id)) & PLAYER_INDEX_LOOKUP_MASK;
	for (int probe = 0; probe < PLAYER_INDEX_LOOKUP_SIZE; ++probe) {
		const int32_t id = player_index_lookup_ids[slot];
		if (id == player_id) {
			return player_index_lookup_indices[slot];
		}
		if (id == PLAYER_INDEX_LOOKUP_EMPTY) {
			return -1;
		}
		slot = (slot + 1) & PLAYER_INDEX_LOOKUP_MASK;
	}
	return -1;
}

void GameSim::configure_native_cpu_drivers()
{
	native_cpu_drivers.clear();
	native_cpu_drivers.resize(std::max(0, num_cars));
	const godot::PackedByteArray neutral = PlayerInput::to_bytes(PlayerInput::from_neutral());
	clear_player_index_lookup();
	for (int i = 0; i < num_cars; ++i) {
		const int32_t player_id = car_player_ids ? car_player_ids[i] : -1;
		if (player_id != -1) {
			insert_player_index_lookup(player_id, i);
		}
		NativeCpuDriverState& driver = native_cpu_drivers[i];
		driver.player_id = player_id;
		driver.active = (car_is_cpu && car_is_cpu[i] && driver.player_id != -1) ? 1 : 0;
		driver.last_generated_tick = -1;
		driver.pending_input = neutral;
	}
}

void GameSim::update_native_cpu_driver(int car_index)
{
	if (car_index < 0 || car_index >= num_cars || car_index >= static_cast<int>(native_cpu_drivers.size())) {
		return;
	}
	NativeCpuDriverState& driver = native_cpu_drivers[car_index];
	if (!driver.active) {
		return;
	}

	PlayerInput input = native_cpu_generate_input_for_car(cars[car_index], driver.player_id, tick, spawn_seed);
	driver.pending_input = PlayerInput::to_bytes(input);
	driver.last_generated_tick = tick;
}

void GameSim::update_native_cpu_drivers()
{
	if (!cars || native_cpu_drivers.empty()) {
		return;
	}
	for (int i = 0; i < num_cars; ++i) {
		update_native_cpu_driver(i);
	}
}

godot::PackedByteArray GameSim::get_native_cpu_input_for_tick(int player_id, int expected_tick)
{
	return generate_native_cpu_input_for_tick(player_id, expected_tick);
}

PlayerInput GameSim::generate_native_cpu_player_input_for_tick(int player_id, int expected_tick)
{
	if (!cars || !car_player_ids) {
		return PlayerInput::from_neutral();
	}
	const int car_index = find_car_index_for_player(static_cast<int32_t>(player_id));
	if (car_index >= 0 && car_index < num_cars) {
		return native_cpu_generate_quantized_input_for_car(
			cars[car_index], static_cast<int32_t>(player_id), expected_tick, spawn_seed);
	}
	return PlayerInput::from_neutral();
}

void GameSim::fill_native_cpu_player_inputs_for_frame(PlayerInput* out_inputs,
	uint8_t* out_present,
	const int32_t* expected_player_ids,
	const uint8_t* expected_cpu_flags,
	int input_count,
	int expected_tick)
{
	if (!out_inputs || !out_present || input_count <= 0) {
		return;
	}
	const int count = std::max(0, input_count);
	if (!cars || !car_player_ids) {
		for (int i = 0; i < count; ++i) {
			if (!expected_cpu_flags || expected_cpu_flags[i]) {
				out_inputs[i] = PlayerInput::from_neutral();
				out_present[i] = 1;
			}
		}
		return;
	}
	for (int i = 0; i < count; ++i) {
		if (expected_cpu_flags && !expected_cpu_flags[i]) {
			continue;
		}
		const int32_t player_id = expected_player_ids ? expected_player_ids[i] : (i < num_cars ? car_player_ids[i] : -1);
		if (i >= 0 && i < num_cars && car_player_ids[i] == player_id) {
			out_inputs[i] = native_cpu_generate_quantized_input_for_car(
				cars[i], player_id, expected_tick, spawn_seed);
		} else {
			out_inputs[i] = generate_native_cpu_player_input_for_tick(player_id, expected_tick);
		}
		out_present[i] = 1;
	}
}

void GameSim::fill_contiguous_native_cpu_player_inputs_for_frame(PlayerInput* out_inputs,
	uint8_t* out_present,
	const int32_t* expected_player_ids,
	int input_count,
	int expected_tick)
{
	if (!out_inputs || !out_present || input_count <= 0) {
		return;
	}
	if (!cars || !car_player_ids || input_count > num_cars) {
		fill_native_cpu_player_inputs_for_frame(out_inputs, out_present, expected_player_ids, nullptr, input_count, expected_tick);
		return;
	}
	for (int i = 0; i < input_count; ++i) {
		if (expected_player_ids && expected_player_ids[i] != car_player_ids[i]) {
			fill_native_cpu_player_inputs_for_frame(out_inputs, out_present, expected_player_ids, nullptr, input_count, expected_tick);
			return;
		}
	}
	std::memset(out_present, 1, static_cast<size_t>(input_count));
	for (int i = 0; i < input_count; ++i) {
		out_inputs[i] = native_cpu_generate_quantized_input_for_car(
			cars[i], car_player_ids[i], expected_tick, spawn_seed);
	}
}

bool GameSim::has_contiguous_native_cpu_player_order(const int32_t* expected_player_ids, int input_count) const
{
	if (!expected_player_ids || !car_player_ids || input_count < 0 || input_count > num_cars) {
		return false;
	}
	for (int i = 0; i < input_count; ++i) {
		if (expected_player_ids[i] != car_player_ids[i]) {
			return false;
		}
	}
	return true;
}

PlayerInput GameSim::generate_native_cpu_player_input_for_car_index(int car_index, int player_id, int expected_tick)
{
	if (!cars || !car_player_ids || car_index < 0 || car_index >= num_cars) {
		return PlayerInput::from_neutral();
	}
	if (car_player_ids[car_index] != player_id) {
		return generate_native_cpu_player_input_for_tick(player_id, expected_tick);
	}
	return native_cpu_generate_quantized_input_for_car(
		cars[car_index], static_cast<int32_t>(player_id), expected_tick, spawn_seed);
}

godot::PackedByteArray GameSim::generate_native_cpu_input_for_tick(int player_id, int expected_tick)
{
	NativeCpuDriverState* driver = find_native_cpu_driver(static_cast<int32_t>(player_id));
	const int car_index = find_car_index_for_player(static_cast<int32_t>(player_id));
	if (car_index >= 0 && car_index < num_cars) {
		PlayerInput input = native_cpu_generate_input_for_car(cars[car_index], static_cast<int32_t>(player_id), expected_tick, spawn_seed);
		godot::PackedByteArray input_bytes = PlayerInput::to_bytes(input);
		if (driver) {
			driver->pending_input = input_bytes;
			driver->last_generated_tick = expected_tick;
		}
		return input_bytes;
	}
	return PlayerInput::to_bytes(PlayerInput::from_neutral());
}

godot::Dictionary GameSim::get_input_frame_as_dictionary(int target_tick) const
{
	godot::Dictionary out;
	if (!input_buffer || !car_player_ids || num_cars <= 0 || target_tick < 0) {
		return out;
	}
	if (target_tick >= tick || tick - target_tick > INPUT_BUFFER_LEN) {
		return out;
	}
	const int buf_index = target_tick % INPUT_BUFFER_LEN;
	const PlayerInput* frame = input_buffer + buf_index * num_cars;
	for (int i = 0; i < num_cars; ++i) {
		out[car_player_ids[i]] = PlayerInput::to_bytes(frame[i]);
	}
	return out;
}
