#include "netcode/netcode_session.h"

#include "gamesim/gamesim.h"

#include <algorithm>
#include <cstring>

using namespace godot;

godot::Array NetcodeSession::build_state_fec_chunks(godot::PackedByteArray payload, int chunk_size, int data_chunks_per_group) const
{
	Array chunks;
	const int payload_size = payload.size();
	if (payload_size <= 0 || chunk_size <= 0 || data_chunks_per_group <= 0) {
		return chunks;
	}

	const int data_chunk_count = (payload_size + chunk_size - 1) / chunk_size;
	const int group_count = (data_chunk_count + data_chunks_per_group - 1) / data_chunks_per_group;
	chunks.resize(data_chunk_count + group_count);
	const uint8_t* source = payload.ptr();
	int wire_index = 0;
	for (int group_index = 0; group_index < group_count; ++group_index) {
		const int first_data_index = group_index * data_chunks_per_group;
		const int group_data_count = std::min(data_chunks_per_group, data_chunk_count - first_data_index);
		PackedByteArray parity;
		parity.resize(chunk_size);
		uint8_t* parity_bytes = parity.ptrw();
		std::memset(parity_bytes, 0, static_cast<size_t>(chunk_size));
		for (int local_index = 0; local_index < group_data_count; ++local_index) {
			const int data_index = first_data_index + local_index;
			const int source_offset = data_index * chunk_size;
			const int data_size = std::min(chunk_size, payload_size - source_offset);
			PackedByteArray data_chunk;
			data_chunk.resize(data_size);
			std::memcpy(data_chunk.ptrw(), source + source_offset, static_cast<size_t>(data_size));
			chunks[wire_index++] = data_chunk;
			for (int byte_index = 0; byte_index < data_size; ++byte_index) {
				parity_bytes[byte_index] ^= source[source_offset + byte_index];
			}
		}
		chunks[wire_index++] = parity;
	}
	return chunks;
}

godot::Dictionary NetcodeSession::consume_authoritative_packet_stats()
{
	Dictionary stats;
	stats["auth_packets"] = static_cast<int64_t>(stat_auth_packets);
	stats["auth_frames"] = static_cast<int64_t>(stat_auth_frames);
	stats["auth_encoded_inputs"] = static_cast<int64_t>(stat_auth_encoded_inputs);
	stats["auth_unchanged_inputs"] = static_cast<int64_t>(stat_auth_unchanged_inputs);
	stats["auth_raw_bytes"] = static_cast<int64_t>(stat_auth_raw_bytes);
	stats["auth_payload_bytes"] = static_cast<int64_t>(stat_auth_payload_bytes);
	stats["auth_compression_candidates"] = static_cast<int64_t>(stat_auth_compression_candidates);
	stats["auth_build_usec"] = static_cast<int64_t>(stat_auth_build_usec);
	stat_auth_packets = 0;
	stat_auth_frames = 0;
	stat_auth_encoded_inputs = 0;
	stat_auth_unchanged_inputs = 0;
	stat_auth_raw_bytes = 0;
	stat_auth_payload_bytes = 0;
	stat_auth_compression_candidates = 0;
	stat_auth_build_usec = 0;
	return stats;
}

godot::Dictionary NetcodeSession::get_input_frame_debug(int tick) const
{
	Dictionary out;
	const InputFrame* frame = find_frame(pending_inputs, tick);
	out["tick"] = tick;
	out["frame_found"] = frame != nullptr;
	out["racer_count"] = racer_count;
	int human_count = 0;
	int cpu_count = 0;
	int present_humans = 0;
	int first_missing_slot = -1;
	int first_missing_player_id = 0;
	bool first_missing_cpu = false;
	for (int i = 0; i < racer_count; ++i) {
		if (cpu_flags[i]) {
			++cpu_count;
			continue;
		}
		++human_count;
		if (frame && frame->present[i]) {
			++present_humans;
		} else if (first_missing_slot < 0) {
			first_missing_slot = i;
			first_missing_player_id = player_ids[i];
			first_missing_cpu = cpu_flags[i] != 0;
		}
	}
	out["human_count"] = human_count;
	out["cpu_count"] = cpu_count;
	out["present_humans"] = present_humans;
	out["first_missing_slot"] = first_missing_slot;
	out["first_missing_player_id"] = first_missing_player_id;
	out["first_missing_cpu"] = first_missing_cpu;
	return out;
}

void NetcodeSession::clear_peer_state()
{
	for (int i = 0; i < MAX_PEERS; ++i) {
		peer_states[i] = PeerState();
	}
}

void NetcodeSession::remove_peer(int peer_id)
{
	const int index = find_peer_index(static_cast<int32_t>(peer_id));
	if (index >= 0) {
		peer_states[index] = PeerState();
	}
}

void NetcodeSession::set_peer_last_received(int peer_id, int tick, double now_sec)
{
	const int index = ensure_peer_index(static_cast<int32_t>(peer_id));
	if (index < 0) {
		return;
	}
	peer_states[index].last_received_tick = static_cast<int32_t>(tick);
	peer_states[index].last_input_time = now_sec;
}

int NetcodeSession::get_peer_last_received(int peer_id) const
{
	const int index = find_peer_index(static_cast<int32_t>(peer_id));
	return index >= 0 ? peer_states[index].last_received_tick : -1;
}

bool NetcodeSession::peer_has_received(int peer_id) const
{
	return get_peer_last_received(peer_id) >= 0;
}

void NetcodeSession::set_peer_desired_ahead(int peer_id, double ahead)
{
	const int index = ensure_peer_index(static_cast<int32_t>(peer_id));
	if (index < 0) {
		return;
	}
	peer_states[index].desired_ahead = static_cast<float>(ahead);
}

double NetcodeSession::get_max_peer_desired_ahead(godot::Array p_peer_ids, double fallback) const
{
	double max_ahead = fallback;
	for (int i = 0; i < p_peer_ids.size(); ++i) {
		const int peer_id = static_cast<int32_t>(static_cast<int64_t>(p_peer_ids[i]));
		const int index = find_peer_index(peer_id);
		if (index >= 0 && peer_states[index].desired_ahead > max_ahead) {
			max_ahead = peer_states[index].desired_ahead;
		}
	}
	return max_ahead;
}

double NetcodeSession::get_peer_last_input_time(int peer_id) const
{
	const int index = find_peer_index(static_cast<int32_t>(peer_id));
	return index >= 0 ? peer_states[index].last_input_time : 0.0;
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

bool NetcodeSession::tick_server_frame(godot::Object* game_sim_obj, int tick, bool use_pending_cpu_inputs)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim) {
		return false;
	}
	return tick_server_frame_internal(sim, tick, use_pending_cpu_inputs);
}

int NetcodeSession::tick_server_frames(godot::Object* game_sim_obj, int start_tick, int end_tick, bool use_pending_cpu_inputs)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim || end_tick < start_tick) {
		return 0;
	}
	if (!use_pending_cpu_inputs &&
		cpu_racer_count == racer_count &&
		sim->has_contiguous_native_cpu_player_order(player_ids, racer_count)) {
		for (int i = 0; i < HISTORY_LEN; ++i) {
			std::memset(authoritative_history[i].present, 1, static_cast<size_t>(racer_count));
		}
		int ticked = 0;
		for (int tick = start_tick; tick <= end_tick; ++tick) {
			InputFrame& authoritative = authoritative_history[tick & (HISTORY_LEN - 1)];
			authoritative.tick = tick;
			sim->tick_gamesim_internal(GameSim::InputFrameMode::SingleLocal,
				-1, nullptr, nullptr, nullptr, racer_count,
				authoritative.inputs, nullptr, false);
			++ticked;
		}
		latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(end_tick));
		return ticked;
	}
	int ticked = 0;
	for (int tick = start_tick; tick <= end_tick; ++tick) {
		if (!tick_server_frame_internal(sim, tick, use_pending_cpu_inputs)) {
			break;
		}
		++ticked;
	}
	return ticked;
}

int NetcodeSession::tick_server_frames_with_native_inputs(godot::Object* game_sim_obj, int start_tick, int end_tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim || end_tick < start_tick) {
		return 0;
	}

	const bool contiguous_player_order = sim->has_contiguous_native_cpu_player_order(player_ids, racer_count);
	int ticked = 0;
	for (int tick = start_tick; tick <= end_tick; ++tick) {
		InputFrame& authoritative = authoritative_history[tick & (HISTORY_LEN - 1)];
		clear_frame(authoritative, tick);
		if (contiguous_player_order) {
			for (int i = 0; i < racer_count; ++i) {
				if (!cpu_flags[i]) {
					authoritative.inputs[i] = sim->generate_native_cpu_player_input_for_tick(player_ids[i], tick);
					authoritative.present[i] = 1;
				}
			}
		} else {
			for (int i = 0; i < racer_count; ++i) {
				authoritative.inputs[i] = sim->generate_native_cpu_player_input_for_tick(player_ids[i], tick);
				authoritative.present[i] = 1;
			}
		}
		sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedQuantizedCarArray,
			-1, nullptr, authoritative.inputs, authoritative.present, racer_count,
			authoritative.inputs, authoritative.present, false);
		++ticked;
	}
	latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(end_tick));
	return ticked;
}

bool NetcodeSession::tick_server_frame_internal(GameSim* sim, int tick, bool use_pending_cpu_inputs)
{
	if (!use_pending_cpu_inputs && cpu_racer_count == racer_count) {
		InputFrame& authoritative = authoritative_history[tick & (HISTORY_LEN - 1)];
		authoritative.tick = tick;
		if (sim->has_contiguous_native_cpu_player_order(player_ids, racer_count)) {
			sim->tick_gamesim_internal(GameSim::InputFrameMode::SingleLocal,
				-1, nullptr, nullptr, nullptr, racer_count,
				authoritative.inputs, authoritative.present, false);
		} else {
			sim->fill_contiguous_native_cpu_player_inputs_for_frame(
				authoritative.inputs,
				authoritative.present,
				player_ids,
				racer_count,
				tick);
			sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedQuantizedCarArray,
				-1, nullptr, authoritative.inputs, nullptr, racer_count, nullptr, nullptr, false);
		}
		latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
		return true;
	}

	const InputFrame* pending_frame = find_frame(pending_inputs, tick);
	if (!pending_frame) {
		if (use_pending_cpu_inputs || cpu_racer_count != racer_count) {
			return false;
		}
	}
	InputFrame& authoritative = frame_for(authoritative_history, tick);
	clear_frame(authoritative, tick);
	const bool contiguous_player_order =
		!use_pending_cpu_inputs &&
		sim->has_contiguous_native_cpu_player_order(player_ids, racer_count);
	if (contiguous_player_order) {
		for (int i = 0; i < racer_count; ++i) {
			if (cpu_flags[i]) {
				continue;
			}
			if (pending_frame && pending_frame->present[i]) {
				authoritative.inputs[i] = pending_frame->inputs[i];
				authoritative.present[i] = 1;
			} else {
				return false;
			}
		}
	} else {
		if (!use_pending_cpu_inputs) {
			sim->fill_native_cpu_player_inputs_for_frame(
				authoritative.inputs,
				authoritative.present,
				player_ids,
				cpu_flags,
				racer_count,
				tick);
		}
		for (int i = 0; i < racer_count; ++i) {
			if (cpu_flags[i] && use_pending_cpu_inputs) {
				if (!pending_frame->present[i]) {
					return false;
				}
				authoritative.inputs[i] = pending_frame->inputs[i];
				authoritative.present[i] = 1;
			} else if (cpu_flags[i]) {
				if (!authoritative.present[i]) {
					authoritative.inputs[i] = PlayerInput::from_neutral();
					authoritative.present[i] = 1;
				}
			} else if (pending_frame && pending_frame->present[i]) {
				authoritative.inputs[i] = pending_frame->inputs[i];
				authoritative.present[i] = 1;
			} else {
				return false;
			}
		}
	}
	if (contiguous_player_order) {
		sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedQuantizedCarArray,
			-1, nullptr, authoritative.inputs, authoritative.present, racer_count,
			authoritative.inputs, authoritative.present, false);
	} else {
		sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedQuantizedCarArray,
			-1, nullptr, authoritative.inputs, authoritative.present, racer_count, nullptr, nullptr, false);
	}
	latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
	return true;
}

bool NetcodeSession::tick_client_predicted_frame(godot::Object* game_sim_obj, int tick)
{
	GameSim* sim = Object::cast_to<GameSim>(game_sim_obj);
	if (!sim) {
		return false;
	}
	recalculate_predictions_internal(sim, tick, tick + 1);
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
	recalculate_predictions_internal(nullptr, start_tick, end_tick);
}

void NetcodeSession::recalculate_predictions_internal(GameSim* sim, int start_tick, int end_tick)
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
				} else if (sim) {
					frame.inputs[i] = sim->generate_native_cpu_player_input_for_car_index(i, player_ids[i], tick);
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
		recalculate_predictions_internal(sim, tick, tick + 1);
		const InputFrame* frame = find_frame(input_history, tick);
		if (!frame) {
			sim->finish_render_rollback_correction_capture();
			return false;
		}
		sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
			-1, nullptr, frame->inputs, frame->present, racer_count);
	}
	sim->finish_render_rollback_correction_capture();
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
