#include "netcode/netcode_session.h"

#include "godot_cpp/core/class_db.hpp"

using namespace godot;
void NetcodeSession::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("reset"), &NetcodeSession::reset);
	ClassDB::bind_method(D_METHOD("configure", "player_ids", "cpu_flags", "local_player_id"), &NetcodeSession::configure);
	ClassDB::bind_method(D_METHOD("set_local_input", "input_bytes"), &NetcodeSession::set_local_input);
	ClassDB::bind_method(D_METHOD("store_local_input", "tick", "input_bytes"), &NetcodeSession::store_local_input);
	ClassDB::bind_method(D_METHOD("store_authoritative_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_authoritative_input);
	ClassDB::bind_method(D_METHOD("store_pending_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_pending_input);
	ClassDB::bind_method(D_METHOD("fill_missing_pending_inputs", "tick", "player_ids", "disconnected_ids", "delayed_ids", "allow_new_delayed"), &NetcodeSession::fill_missing_pending_inputs);
	ClassDB::bind_method(D_METHOD("build_local_input_packet", "first_tick", "count", "race_phase"), &NetcodeSession::build_local_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("build_state_fec_chunks", "payload", "chunk_size", "data_chunks_per_group"), &NetcodeSession::build_state_fec_chunks);
	ClassDB::bind_method(D_METHOD("store_pending_input_packet", "player_id", "reject_before_tick", "packet", "ahead", "now_sec", "expected_race_phase"), &NetcodeSession::store_pending_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("build_authoritative_input_packet", "last_tick", "max_frame_count", "race_phase"), &NetcodeSession::build_authoritative_input_packet, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("store_authoritative_input_packet", "packet", "expected_race_phase", "authoritative_last_tick", "external_mode_count_phase"), &NetcodeSession::store_authoritative_input_packet, DEFVAL(0), DEFVAL(-1), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_compare_authoritative_input_packet_sizes", "last_tick", "max_frame_count", "race_phase"), &NetcodeSession::debug_compare_authoritative_input_packet_sizes, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("consume_authoritative_packet_stats"), &NetcodeSession::consume_authoritative_packet_stats);
	ClassDB::bind_method(D_METHOD("get_input_frame_debug", "tick"), &NetcodeSession::get_input_frame_debug);
	ClassDB::bind_method(D_METHOD("configure_authoritative_input_sample_dump", "enabled", "limit", "directory"), &NetcodeSession::configure_authoritative_input_sample_dump);
	ClassDB::bind_method(D_METHOD("clear_peer_state"), &NetcodeSession::clear_peer_state);
	ClassDB::bind_method(D_METHOD("remove_peer", "peer_id"), &NetcodeSession::remove_peer);
	ClassDB::bind_method(D_METHOD("set_peer_last_received", "peer_id", "tick", "now_sec"), &NetcodeSession::set_peer_last_received);
	ClassDB::bind_method(D_METHOD("get_peer_last_received", "peer_id"), &NetcodeSession::get_peer_last_received);
	ClassDB::bind_method(D_METHOD("peer_has_received", "peer_id"), &NetcodeSession::peer_has_received);
	ClassDB::bind_method(D_METHOD("set_peer_desired_ahead", "peer_id", "ahead"), &NetcodeSession::set_peer_desired_ahead);
	ClassDB::bind_method(D_METHOD("get_max_peer_desired_ahead", "peer_ids", "fallback"), &NetcodeSession::get_max_peer_desired_ahead);
	ClassDB::bind_method(D_METHOD("get_peer_last_input_time", "peer_id"), &NetcodeSession::get_peer_last_input_time);
	ClassDB::bind_method(D_METHOD("server_has_full_input_frame", "tick"), &NetcodeSession::server_has_full_input_frame);
	ClassDB::bind_method(D_METHOD("tick_server_frame", "game_sim", "tick", "use_pending_cpu_inputs"), &NetcodeSession::tick_server_frame, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("tick_server_frames", "game_sim", "start_tick", "end_tick", "use_pending_cpu_inputs"), &NetcodeSession::tick_server_frames, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("tick_server_frames_with_native_inputs", "game_sim", "start_tick", "end_tick"), &NetcodeSession::tick_server_frames_with_native_inputs);
	ClassDB::bind_method(D_METHOD("tick_client_predicted_frame", "game_sim", "tick"), &NetcodeSession::tick_client_predicted_frame);
	ClassDB::bind_method(D_METHOD("recalculate_predictions", "start_tick", "end_tick"), &NetcodeSession::recalculate_predictions);
	ClassDB::bind_method(D_METHOD("replay_history", "game_sim", "start_tick", "end_tick"), &NetcodeSession::replay_history);
	ClassDB::bind_method(D_METHOD("get_frame_as_dictionary", "tick"), &NetcodeSession::get_frame_as_dictionary);
}

NetcodeSession::NetcodeSession()
{
	reset();
}
