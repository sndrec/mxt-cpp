#include "mxt_core/netcode_session.h"

#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include <algorithm>
#include <cmath>

using namespace godot;

void NetcodeSession::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("reset"), &NetcodeSession::reset);
	ClassDB::bind_method(D_METHOD("configure", "player_ids", "cpu_flags", "local_player_id"), &NetcodeSession::configure);
	ClassDB::bind_method(D_METHOD("set_local_input", "input_bytes"), &NetcodeSession::set_local_input);
	ClassDB::bind_method(D_METHOD("store_local_input", "tick", "input_bytes"), &NetcodeSession::store_local_input);
	ClassDB::bind_method(D_METHOD("store_authoritative_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_authoritative_input);
	ClassDB::bind_method(D_METHOD("store_pending_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_pending_input);
	ClassDB::bind_method(D_METHOD("server_has_full_input_frame", "tick"), &NetcodeSession::server_has_full_input_frame);
	ClassDB::bind_method(D_METHOD("tick_server_frame", "game_sim", "tick"), &NetcodeSession::tick_server_frame);
	ClassDB::bind_method(D_METHOD("tick_client_predicted_frame", "game_sim", "tick"), &NetcodeSession::tick_client_predicted_frame);
	ClassDB::bind_method(D_METHOD("recalculate_predictions", "start_tick", "end_tick"), &NetcodeSession::recalculate_predictions);
	ClassDB::bind_method(D_METHOD("replay_history", "game_sim", "start_tick", "end_tick"), &NetcodeSession::replay_history);
	ClassDB::bind_method(D_METHOD("get_frame_as_dictionary", "tick"), &NetcodeSession::get_frame_as_dictionary);
}

NetcodeSession::NetcodeSession()
{
	reset();
}

PlayerInput NetcodeSession::decay_predicted_input(const PlayerInput& prev)
{
	PlayerInput out = prev;
	auto lerp_f = [](float a, float b, float t) -> float {
		return a + (b - a) * t;
	};
	out.strafe_left = lerp_f(out.strafe_left, 0.0f, 0.25f);
	out.strafe_right = lerp_f(out.strafe_right, 0.0f, 0.25f);
	out.steer_horizontal = lerp_f(out.steer_horizontal, 0.0f, 0.25f);
	out.steer_vertical = lerp_f(out.steer_vertical, 0.0f, 0.25f);
	out.spinattack = false;
	out.sideattack = false;
	out.boost = false;
	return out;
}

void NetcodeSession::clear_frame(InputFrame& frame, int32_t tick)
{
	frame.tick = tick;
	for (int i = 0; i < MAX_RACERS; ++i) {
		frame.inputs[i] = neutral_input;
		frame.present[i] = 0;
	}
}

NetcodeSession::InputFrame& NetcodeSession::frame_for(InputFrame* frames, int32_t tick)
{
	InputFrame& frame = frames[tick & (HISTORY_LEN - 1)];
	if (frame.tick != tick) {
		clear_frame(frame, tick);
	}
	return frame;
}

const NetcodeSession::InputFrame* NetcodeSession::find_frame(const InputFrame* frames, int32_t tick) const
{
	const InputFrame& frame = frames[tick & (HISTORY_LEN - 1)];
	return frame.tick == tick ? &frame : nullptr;
}

int NetcodeSession::find_racer_index(int32_t player_id) const
{
	for (int i = 0; i < racer_count; ++i) {
		if (player_ids[i] == player_id) {
			return i;
		}
	}
	return -1;
}

void NetcodeSession::reset()
{
	racer_count = 0;
	local_player_id = -1;
	last_local_input = neutral_input;
	for (int i = 0; i < MAX_RACERS; ++i) {
		player_ids[i] = 0;
		cpu_flags[i] = 0;
	}
	for (int i = 0; i < HISTORY_LEN; ++i) {
		clear_frame(local_input_history[i], -1);
		clear_frame(input_history[i], -1);
		clear_frame(authoritative_history[i], -1);
		clear_frame(pending_inputs[i], -1);
	}
}

void NetcodeSession::configure(godot::Array p_player_ids, godot::Array p_cpu_flags, int p_local_player_id)
{
	racer_count = std::min(static_cast<int>(p_player_ids.size()), MAX_RACERS);
	local_player_id = p_local_player_id;
	for (int i = 0; i < racer_count; ++i) {
		player_ids[i] = static_cast<int32_t>(static_cast<int64_t>(p_player_ids[i]));
		cpu_flags[i] = (i < p_cpu_flags.size() && static_cast<bool>(p_cpu_flags[i])) ? 1 : 0;
	}
	for (int i = racer_count; i < MAX_RACERS; ++i) {
		player_ids[i] = 0;
		cpu_flags[i] = 0;
	}
	for (int i = 0; i < HISTORY_LEN; ++i) {
		clear_frame(local_input_history[i], -1);
		clear_frame(input_history[i], -1);
		clear_frame(authoritative_history[i], -1);
		clear_frame(pending_inputs[i], -1);
	}
}

void NetcodeSession::set_local_input(godot::PackedByteArray input_bytes)
{
	last_local_input = PlayerInput::from_bytes(input_bytes);
}

void NetcodeSession::store_local_input(int tick, godot::PackedByteArray input_bytes)
{
	const int index = find_racer_index(local_player_id);
	if (index < 0) {
		return;
	}
	InputFrame& frame = frame_for(local_input_history, tick);
	frame.inputs[index] = PlayerInput::from_bytes(input_bytes);
	frame.present[index] = 1;
	last_local_input = frame.inputs[index];
}

void NetcodeSession::store_authoritative_input(int tick, int player_id, godot::PackedByteArray input_bytes)
{
	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return;
	}
	InputFrame& frame = frame_for(authoritative_history, tick);
	frame.inputs[index] = PlayerInput::from_bytes(input_bytes);
	frame.present[index] = 1;
}

void NetcodeSession::store_pending_input(int tick, int player_id, godot::PackedByteArray input_bytes)
{
	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return;
	}
	InputFrame& frame = frame_for(pending_inputs, tick);
	frame.inputs[index] = PlayerInput::from_bytes(input_bytes);
	frame.present[index] = 1;
}

bool NetcodeSession::server_has_full_input_frame(int tick) const
{
	const InputFrame* frame = find_frame(pending_inputs, tick);
	if (!frame) {
		return false;
	}
	for (int i = 0; i < racer_count; ++i) {
		if (!cpu_flags[i] && !frame->present[i]) {
			return false;
		}
	}
	return true;
}

bool NetcodeSession::tick_server_frame(godot::Object* game_sim_obj, int tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim || !server_has_full_input_frame(tick)) {
		return false;
	}
	sim->update_native_cpu_drivers();
	InputFrame& pending = frame_for(pending_inputs, tick);
	InputFrame& authoritative = frame_for(authoritative_history, tick);
	clear_frame(authoritative, tick);
	for (int i = 0; i < racer_count; ++i) {
		if (cpu_flags[i]) {
			const GameSim::NativeCpuDriverState* cpu_driver = sim->find_native_cpu_driver(player_ids[i]);
			if (cpu_driver && !cpu_driver->pending_input.is_empty()) {
				authoritative.inputs[i] = PlayerInput::from_bytes(cpu_driver->pending_input);
			} else {
				authoritative.inputs[i] = neutral_input;
			}
			authoritative.present[i] = 1;
		} else if (pending.present[i]) {
			authoritative.inputs[i] = pending.inputs[i];
			authoritative.present[i] = 1;
		}
	}
	sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
		-1, nullptr, authoritative.inputs, authoritative.present, racer_count);
	return true;
}

bool NetcodeSession::tick_client_predicted_frame(godot::Object* game_sim_obj, int tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim) {
		return false;
	}
	recalculate_predictions(tick, tick + 1);
	const InputFrame* frame = find_frame(input_history, tick);
	if (!frame) {
		return false;
	}
	sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
		-1, nullptr, frame->inputs, frame->present, racer_count);
	return true;
}

void NetcodeSession::recalculate_predictions(int start_tick, int end_tick)
{
	for (int tick = start_tick; tick < end_tick; ++tick) {
		InputFrame& frame = frame_for(input_history, tick);
		clear_frame(frame, tick);
		const InputFrame* authoritative = find_frame(authoritative_history, tick);
		const InputFrame* previous = find_frame(input_history, tick - 1);
		for (int i = 0; i < racer_count; ++i) {
			if (cpu_flags[i]) {
				if (authoritative && authoritative->present[i]) {
					frame.inputs[i] = authoritative->inputs[i];
					frame.present[i] = 1;
				} else if (previous && previous->present[i]) {
					frame.inputs[i] = previous->inputs[i];
					frame.present[i] = 1;
				}
				continue;
			}
			if (authoritative && authoritative->present[i]) {
				frame.inputs[i] = authoritative->inputs[i];
				frame.present[i] = 1;
			} else if (player_ids[i] == local_player_id) {
				const InputFrame* local = find_frame(local_input_history, tick);
				frame.inputs[i] = (local && local->present[i]) ? local->inputs[i] : last_local_input;
				frame.present[i] = 1;
			} else if (previous && previous->present[i]) {
				frame.inputs[i] = decay_predicted_input(previous->inputs[i]);
				frame.present[i] = 1;
			}
		}
	}
}

bool NetcodeSession::replay_history(godot::Object* game_sim_obj, int start_tick, int end_tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim) {
		return false;
	}
	for (int tick = start_tick; tick < end_tick; ++tick) {
		const InputFrame* frame = find_frame(input_history, tick);
		if (!frame) {
			return false;
		}
		sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
			-1, nullptr, frame->inputs, frame->present, racer_count);
	}
	return true;
}

godot::Dictionary NetcodeSession::get_frame_as_dictionary(int tick) const
{
	godot::Dictionary out;
	const InputFrame* frame = find_frame(authoritative_history, tick);
	if (!frame) {
		frame = find_frame(input_history, tick);
	}
	if (!frame) {
		return out;
	}
	for (int i = 0; i < racer_count; ++i) {
		if (frame->present[i]) {
			out[player_ids[i]] = PlayerInput::to_bytes(frame->inputs[i]);
		}
	}
	return out;
}
