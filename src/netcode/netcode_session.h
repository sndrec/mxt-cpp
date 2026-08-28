#pragma once

#include "godot_cpp/classes/object.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "core/player_input.h"
#include <cstdint>

namespace godot {

class GameSim;

class NetcodeSession : public Object {
	GDCLASS(NetcodeSession, Object)

public:
	enum PacketStoreStatus : int32_t {
		PACKET_STORE_INVALID = 0,
		PACKET_STORE_VALID = 1,
		PACKET_STORE_STALE = 2,
	};

private:

	static constexpr int MAX_RACERS = 1024;
	static constexpr int HISTORY_LEN = 128;
	static constexpr int MAX_PEERS = 256;
	static constexpr int RACER_LOOKUP_SIZE = 2048;
	static constexpr int RACER_LOOKUP_MASK = RACER_LOOKUP_SIZE - 1;
	static constexpr int32_t RACER_LOOKUP_EMPTY = -2147483647 - 1;

	struct InputFrame {
		int32_t tick = -1;
		PlayerInput inputs[MAX_RACERS];
		uint8_t present[MAX_RACERS] = {};
	};

	struct PeerState {
		int32_t id = 0;
		int32_t last_received_tick = -1;
		double last_input_time = 0.0;
		float desired_ahead = 0.0f;
		uint8_t active = 0;
	};

	struct PendingInputPacketResult {
		int32_t start_tick = -1;
		int32_t count = 0;
		int32_t accepted = 0;
		int32_t dropped = 0;
		int32_t last_tick = -1;
		uint8_t seen_before = 0;
	};

	struct AuthoritativeInputPacketResult {
		int32_t first_tick = -1;
		int32_t last_tick = -1;
		int32_t count = 0;
	};

	struct AuthoritativePacketStats {
		uint64_t packets = 0;
		uint64_t frames = 0;
		uint64_t encoded_inputs = 0;
		uint64_t unchanged_inputs = 0;
		uint64_t raw_bytes = 0;
		uint64_t payload_bytes = 0;
		uint64_t compression_candidates = 0;
		uint64_t build_usec = 0;
	};

	int racer_count = 0;
	int cpu_racer_count = 0;
	int32_t local_player_id = -1;
	int32_t player_ids[MAX_RACERS] = {};
	uint8_t cpu_flags[MAX_RACERS] = {};
	int32_t racer_lookup_ids[RACER_LOOKUP_SIZE] = {};
	int16_t racer_lookup_indices[RACER_LOOKUP_SIZE] = {};
	PlayerInput neutral_input = PlayerInput::from_neutral();
	PlayerInput last_local_input = PlayerInput::from_neutral();
	InputFrame local_input_history[HISTORY_LEN];
	InputFrame input_history[HISTORY_LEN];
	InputFrame authoritative_history[HISTORY_LEN];
	InputFrame pending_inputs[HISTORY_LEN];
	PeerState peer_states[MAX_PEERS];
	PendingInputPacketResult last_pending_packet_result;
	AuthoritativeInputPacketResult last_authoritative_packet_result;
	AuthoritativePacketStats last_consumed_authoritative_packet_stats;
	int32_t last_replaced_pending_player_ids[MAX_RACERS] = {};
	int32_t last_replaced_pending_player_count = 0;
	int32_t latest_authoritative_tick = -1;
	mutable uint64_t stat_auth_packets = 0;
	mutable uint64_t stat_auth_frames = 0;
	mutable uint64_t stat_auth_encoded_inputs = 0;
	mutable uint64_t stat_auth_unchanged_inputs = 0;
	mutable uint64_t stat_auth_raw_bytes = 0;
	mutable uint64_t stat_auth_payload_bytes = 0;
	mutable uint64_t stat_auth_compression_candidates = 0;
	mutable uint64_t stat_auth_build_usec = 0;

	static PlayerInput decay_predicted_input(const PlayerInput& prev);
	bool write_authoritative_input_raw(godot::PackedByteArray& raw, const InputFrame* const* frames, int frame_count, uint8_t layout) const;
	void recalculate_predictions_internal(GameSim* sim, int start_tick, int end_tick);
	InputFrame& frame_for(InputFrame* frames, int32_t tick);
	const InputFrame* find_frame(const InputFrame* frames, int32_t tick) const;
	int find_racer_index(int32_t player_id) const;
	void clear_racer_lookup();
	void insert_racer_lookup(int32_t player_id, int index);
	int find_peer_index(int32_t peer_id) const;
	int ensure_peer_index(int32_t peer_id);
	void clear_frame(InputFrame& frame, int32_t tick);
	bool tick_server_frame_internal(GameSim* sim, int tick, bool use_pending_cpu_inputs);

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
	int fill_missing_pending_inputs(int tick, godot::Array player_ids, godot::Array disconnected_ids, godot::Array delayed_ids, bool allow_new_delayed);
	int get_last_replaced_pending_player_id(int index) const;
	godot::PackedByteArray build_local_input_packet(int first_tick, int count, int race_phase = 0) const;
	godot::Array build_state_fec_chunks(godot::PackedByteArray payload, int chunk_size, int data_chunks_per_group) const;
	int store_pending_input_packet(int player_id, int reject_before_tick, godot::PackedByteArray packet, double ahead, double now_sec, int expected_race_phase = 0);
	int get_last_pending_packet_start_tick() const;
	int get_last_pending_packet_count() const;
	int get_last_pending_packet_accepted() const;
	int get_last_pending_packet_dropped() const;
	int get_last_pending_packet_last_tick() const;
	bool get_last_pending_packet_seen_before() const;
	godot::PackedByteArray build_authoritative_input_packet(int last_tick, int max_frame_count, int race_phase = 0) const;
	int store_authoritative_input_packet(godot::PackedByteArray packet, int expected_race_phase = 0, int authoritative_last_tick = -1, int external_mode_count_phase = -1);
	int get_last_authoritative_packet_first_tick() const;
	int get_last_authoritative_packet_last_tick() const;
	int get_last_authoritative_packet_count() const;
	godot::Dictionary debug_compare_authoritative_input_packet_sizes(int last_tick, int max_frame_count, int race_phase = 0) const;
	void consume_authoritative_packet_stats();
	int64_t get_authoritative_stat_packets() const;
	int64_t get_authoritative_stat_frames() const;
	int64_t get_authoritative_stat_encoded_inputs() const;
	int64_t get_authoritative_stat_unchanged_inputs() const;
	int64_t get_authoritative_stat_raw_bytes() const;
	int64_t get_authoritative_stat_payload_bytes() const;
	int64_t get_authoritative_stat_compression_candidates() const;
	int64_t get_authoritative_stat_build_usec() const;
	godot::Dictionary get_input_frame_debug(int tick) const;
	void configure_authoritative_input_sample_dump(bool enabled, int limit, godot::String directory);
	void clear_peer_state();
	void remove_peer(int peer_id);
	void set_peer_last_received(int peer_id, int tick, double now_sec);
	int get_peer_last_received(int peer_id) const;
	bool peer_has_received(int peer_id) const;
	void set_peer_desired_ahead(int peer_id, double ahead);
	double get_max_peer_desired_ahead(godot::Array peer_ids, double fallback) const;
	double get_peer_last_input_time(int peer_id) const;
	bool server_has_full_input_frame(int tick) const;
	bool tick_server_frame(godot::Object* game_sim_obj, int tick, bool use_pending_cpu_inputs = false);
	int tick_server_frames(godot::Object* game_sim_obj, int start_tick, int end_tick, bool use_pending_cpu_inputs = false);
	int tick_server_frames_with_native_inputs(godot::Object* game_sim_obj, int start_tick, int end_tick);
	bool tick_client_predicted_frame(godot::Object* game_sim_obj, int tick);
	void recalculate_predictions(int start_tick, int end_tick);
	bool replay_history(godot::Object* game_sim_obj, int start_tick, int end_tick);
	godot::Dictionary get_frame_as_dictionary(int tick) const;
};

}

VARIANT_ENUM_CAST(godot::NetcodeSession::PacketStoreStatus);
