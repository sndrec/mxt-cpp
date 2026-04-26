#pragma once

#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "mxt_core/player_input.h"
#include <cstdint>

namespace godot {

class GameSim;

class NetcodeSession : public Object {
	GDCLASS(NetcodeSession, Object)

	static constexpr int MAX_RACERS = 1024;
	static constexpr int HISTORY_LEN = 128;

	struct InputFrame {
		int32_t tick = -1;
		PlayerInput inputs[MAX_RACERS];
		uint8_t present[MAX_RACERS] = {};
	};

	int racer_count = 0;
	int32_t local_player_id = -1;
	int32_t player_ids[MAX_RACERS] = {};
	uint8_t cpu_flags[MAX_RACERS] = {};
	PlayerInput neutral_input = PlayerInput::from_neutral();
	PlayerInput last_local_input = PlayerInput::from_neutral();
	InputFrame local_input_history[HISTORY_LEN];
	InputFrame input_history[HISTORY_LEN];
	InputFrame authoritative_history[HISTORY_LEN];
	InputFrame pending_inputs[HISTORY_LEN];
	int32_t latest_authoritative_tick = -1;

	static PlayerInput decay_predicted_input(const PlayerInput& prev);
	void recalculate_predictions_internal(GameSim* sim, int start_tick, int end_tick);
	InputFrame& frame_for(InputFrame* frames, int32_t tick);
	const InputFrame* find_frame(const InputFrame* frames, int32_t tick) const;
	int find_racer_index(int32_t player_id) const;
	void clear_frame(InputFrame& frame, int32_t tick);

protected:
	static void _bind_methods();

public:
	NetcodeSession();
	~NetcodeSession() = default;

	void reset();
	void configure(godot::Array p_player_ids, godot::Array p_cpu_flags, int p_local_player_id);
	void set_local_input(godot::PackedByteArray input_bytes);
	void store_local_input(int tick, godot::PackedByteArray input_bytes);
	void store_authoritative_input(int tick, int player_id, godot::PackedByteArray input_bytes);
	void store_pending_input(int tick, int player_id, godot::PackedByteArray input_bytes);
	godot::PackedByteArray build_local_input_packet(int first_tick, int count) const;
	godot::Dictionary store_pending_input_packet(int player_id, int reject_before_tick, godot::PackedByteArray packet);
	godot::PackedByteArray build_authoritative_input_packet(int ack_tick) const;
	godot::Dictionary store_authoritative_input_packet(godot::PackedByteArray packet);
	bool server_has_full_input_frame(int tick) const;
	bool tick_server_frame(godot::Object* game_sim_obj, int tick);
	bool tick_client_predicted_frame(godot::Object* game_sim_obj, int tick);
	void recalculate_predictions(int start_tick, int end_tick);
	bool replay_history(godot::Object* game_sim_obj, int start_tick, int end_tick);
	godot::Dictionary get_frame_as_dictionary(int tick) const;
};

}
