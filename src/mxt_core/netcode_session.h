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
	static constexpr int MAX_PEERS = 256;

	struct InputFrame {
		int32_t tick = -1;
		PlayerInput inputs[MAX_RACERS];
		uint8_t present[MAX_RACERS] = {};
	};

	struct PeerState {
		int32_t id = 0;
		int32_t last_received_tick = -1;
		int32_t authoritative_ack = -1;
		double last_input_time = 0.0;
		float desired_ahead = 0.0f;
		uint8_t active = 0;
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
	PeerState peer_states[MAX_PEERS];
	int32_t latest_authoritative_tick = -1;
	mutable uint64_t stat_auth_packets = 0;
	mutable uint64_t stat_auth_frames = 0;
	mutable uint64_t stat_auth_baseline_inputs = 0;
	mutable uint64_t stat_auth_delta_frames = 0;
	mutable uint64_t stat_auth_delta_changed_inputs = 0;
	mutable uint64_t stat_auth_delta_unchanged_inputs = 0;

	static PlayerInput decay_predicted_input(const PlayerInput& prev);
	void recalculate_predictions_internal(GameSim* sim, int start_tick, int end_tick);
	InputFrame& frame_for(InputFrame* frames, int32_t tick);
	const InputFrame* find_frame(const InputFrame* frames, int32_t tick) const;
	int find_racer_index(int32_t player_id) const;
	int find_peer_index(int32_t peer_id) const;
	int ensure_peer_index(int32_t peer_id);
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
	godot::Dictionary store_pending_input_packet(int player_id, int reject_before_tick, godot::PackedByteArray packet, int ack_tick, double ahead, double now_sec);
	godot::PackedByteArray build_authoritative_input_packet(int ack_tick) const;
	godot::Dictionary store_authoritative_input_packet(godot::PackedByteArray packet);
	godot::Dictionary consume_authoritative_packet_stats();
	godot::Dictionary get_input_frame_debug(int tick) const;
	void clear_peer_state();
	void remove_peer(int peer_id);
	void set_peer_last_received(int peer_id, int tick, double now_sec);
	int get_peer_last_received(int peer_id) const;
	bool peer_has_received(int peer_id) const;
	void set_peer_authoritative_ack(int peer_id, int ack_tick);
	int get_peer_authoritative_ack(int peer_id) const;
	int get_min_peer_authoritative_ack(godot::Array peer_ids) const;
	void set_peer_desired_ahead(int peer_id, double ahead);
	double get_max_peer_desired_ahead(godot::Array peer_ids, double fallback) const;
	double get_peer_last_input_time(int peer_id) const;
	bool server_has_full_input_frame(int tick) const;
	bool tick_server_frame(godot::Object* game_sim_obj, int tick);
	bool tick_client_predicted_frame(godot::Object* game_sim_obj, int tick);
	void recalculate_predictions(int start_tick, int end_tick);
	bool replay_history(godot::Object* game_sim_obj, int start_tick, int end_tick);
	godot::Dictionary get_frame_as_dictionary(int tick) const;
};

}
