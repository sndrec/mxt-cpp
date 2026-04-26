#include "mxt_core/netcode_session.h"

#include "main.h"
#include "godot_cpp/core/class_db.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

namespace {
constexpr uint8_t MXT_NET_PACKET_VERSION = 1;
constexpr uint8_t MXT_NET_PACKET_CLIENT_INPUTS = 1;
constexpr uint8_t MXT_NET_PACKET_AUTHORITATIVE_INPUTS = 2;
constexpr int MXT_NET_MAX_INPUT_BYTES = 8;
constexpr int MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET = 255;

struct PacketWriter {
	uint8_t data[65536] = {};
	int pos = 0;

	bool write_u8(uint8_t v)
	{
		if (pos + 1 > static_cast<int>(sizeof(data))) return false;
		data[pos++] = v;
		return true;
	}

	bool write_u16(uint16_t v)
	{
		return write_u8(static_cast<uint8_t>(v & 0xff)) &&
			write_u8(static_cast<uint8_t>((v >> 8) & 0xff));
	}

	bool write_i32(int32_t v)
	{
		uint32_t u = static_cast<uint32_t>(v);
		return write_u8(static_cast<uint8_t>(u & 0xff)) &&
			write_u8(static_cast<uint8_t>((u >> 8) & 0xff)) &&
			write_u8(static_cast<uint8_t>((u >> 16) & 0xff)) &&
			write_u8(static_cast<uint8_t>((u >> 24) & 0xff));
	}

	bool write_bytes(const uint8_t* src, int len)
	{
		if (!src || len < 0 || pos + len > static_cast<int>(sizeof(data))) return false;
		std::memcpy(data + pos, src, static_cast<size_t>(len));
		pos += len;
		return true;
	}

	PackedByteArray to_pba() const
	{
		PackedByteArray out;
		out.resize(pos);
		for (int i = 0; i < pos; ++i) {
			out.set(i, data[i]);
		}
		return out;
	}
};

struct PacketReader {
	const uint8_t* data = nullptr;
	int size = 0;
	int pos = 0;

	explicit PacketReader(const PackedByteArray& p_packet)
	{
		data = p_packet.ptr();
		size = p_packet.size();
	}

	bool read_u8(uint8_t& out)
	{
		if (pos + 1 > size) return false;
		out = data[pos++];
		return true;
	}

	bool read_u16(uint16_t& out)
	{
		uint8_t a = 0, b = 0;
		if (!read_u8(a) || !read_u8(b)) return false;
		out = static_cast<uint16_t>(a) | (static_cast<uint16_t>(b) << 8);
		return true;
	}

	bool read_i32(int32_t& out)
	{
		uint8_t a = 0, b = 0, c = 0, d = 0;
		if (!read_u8(a) || !read_u8(b) || !read_u8(c) || !read_u8(d)) return false;
		uint32_t u = static_cast<uint32_t>(a) |
			(static_cast<uint32_t>(b) << 8) |
			(static_cast<uint32_t>(c) << 16) |
			(static_cast<uint32_t>(d) << 24);
		out = static_cast<int32_t>(u);
		return true;
	}

	bool read_bytes(const uint8_t*& out, int len)
	{
		if (len < 0 || pos + len > size) return false;
		out = data + pos;
		pos += len;
		return true;
	}
};
}

void NetcodeSession::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("reset"), &NetcodeSession::reset);
	ClassDB::bind_method(D_METHOD("configure", "player_ids", "cpu_flags", "local_player_id"), &NetcodeSession::configure);
	ClassDB::bind_method(D_METHOD("set_local_input", "input_bytes"), &NetcodeSession::set_local_input);
	ClassDB::bind_method(D_METHOD("store_local_input", "tick", "input_bytes"), &NetcodeSession::store_local_input);
	ClassDB::bind_method(D_METHOD("store_authoritative_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_authoritative_input);
	ClassDB::bind_method(D_METHOD("store_pending_input", "tick", "player_id", "input_bytes"), &NetcodeSession::store_pending_input);
	ClassDB::bind_method(D_METHOD("build_local_input_packet", "first_tick", "count"), &NetcodeSession::build_local_input_packet);
	ClassDB::bind_method(D_METHOD("store_pending_input_packet", "player_id", "reject_before_tick", "packet"), &NetcodeSession::store_pending_input_packet);
	ClassDB::bind_method(D_METHOD("build_authoritative_input_packet", "ack_tick"), &NetcodeSession::build_authoritative_input_packet);
	ClassDB::bind_method(D_METHOD("store_authoritative_input_packet", "packet"), &NetcodeSession::store_authoritative_input_packet);
	ClassDB::bind_method(D_METHOD("get_input_frame_debug", "tick"), &NetcodeSession::get_input_frame_debug);
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
	latest_authoritative_tick = -1;
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
	latest_authoritative_tick = -1;
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
	latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
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

godot::PackedByteArray NetcodeSession::build_local_input_packet(int first_tick, int count) const
{
	PacketWriter writer;
	const int index = find_racer_index(local_player_id);
	if (index < 0 || count <= 0) {
		return writer.to_pba();
	}
	count = std::min(count, 255);
	if (!writer.write_u8(MXT_NET_PACKET_CLIENT_INPUTS) ||
		!writer.write_u8(MXT_NET_PACKET_VERSION) ||
		!writer.write_i32(first_tick) ||
		!writer.write_u8(static_cast<uint8_t>(count))) {
		return PackedByteArray();
	}
	for (int i = 0; i < count; ++i) {
		const int tick = first_tick + i;
		const InputFrame* frame = find_frame(local_input_history, tick);
		const PlayerInput& input = (frame && frame->present[index]) ? frame->inputs[index] : last_local_input;
		uint8_t encoded[MXT_NET_MAX_INPUT_BYTES] = {};
		const int encoded_len = PlayerInput::encode_to_raw(input, encoded, MXT_NET_MAX_INPUT_BYTES);
		if (!writer.write_u8(static_cast<uint8_t>(encoded_len)) || !writer.write_bytes(encoded, encoded_len)) {
			return PackedByteArray();
		}
	}
	return writer.to_pba();
}

godot::Dictionary NetcodeSession::store_pending_input_packet(int player_id, int reject_before_tick, godot::PackedByteArray packet)
{
	Dictionary stats;
	stats["start_tick"] = -1;
	stats["count"] = 0;
	stats["accepted"] = 0;
	stats["dropped"] = 0;
	stats["last_tick"] = -1;
	stats["valid"] = false;

	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return stats;
	}
	PacketReader reader(packet);
	uint8_t type = 0, version = 0, count = 0;
	int32_t start_tick = -1;
	if (!reader.read_u8(type) || !reader.read_u8(version) ||
		type != MXT_NET_PACKET_CLIENT_INPUTS || version != MXT_NET_PACKET_VERSION ||
		!reader.read_i32(start_tick) || !reader.read_u8(count)) {
		return stats;
	}
	stats["start_tick"] = start_tick;
	stats["count"] = static_cast<int>(count);
	stats["valid"] = true;

	int accepted = 0;
	int dropped = 0;
	int last_tick = -1;
	for (int i = 0; i < static_cast<int>(count); ++i) {
		uint8_t len = 0;
		const uint8_t* bytes = nullptr;
		if (!reader.read_u8(len) || len > MXT_NET_MAX_INPUT_BYTES || !reader.read_bytes(bytes, len)) {
			stats["valid"] = false;
			break;
		}
		const int tick = start_tick + i;
		if (tick < reject_before_tick) {
			++dropped;
			continue;
		}
		InputFrame& frame = frame_for(pending_inputs, tick);
		frame.inputs[index] = PlayerInput::from_raw(bytes, len);
		frame.present[index] = 1;
		++accepted;
		last_tick = tick;
	}
	stats["accepted"] = accepted;
	stats["dropped"] = dropped;
	stats["last_tick"] = last_tick;
	return stats;
}

godot::PackedByteArray NetcodeSession::build_authoritative_input_packet(int ack_tick) const
{
	PacketWriter writer;
	const int first_tick = ack_tick + 1;
	int count = 0;
	while (count < MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET &&
		find_frame(authoritative_history, first_tick + count)) {
		++count;
	}
	if (!writer.write_u8(MXT_NET_PACKET_AUTHORITATIVE_INPUTS) ||
		!writer.write_u8(MXT_NET_PACKET_VERSION) ||
		!writer.write_i32(first_tick) ||
		!writer.write_u8(static_cast<uint8_t>(count)) ||
		!writer.write_u16(static_cast<uint16_t>(racer_count))) {
		return PackedByteArray();
	}
	const int bitset_bytes = (racer_count + 7) >> 3;
	for (int f = 0; f < count; ++f) {
		const InputFrame* frame = find_frame(authoritative_history, first_tick + f);
		if (!frame) {
			return PackedByteArray();
		}
		uint8_t present_bits[128] = {};
		for (int i = 0; i < racer_count; ++i) {
			if (frame->present[i]) {
				present_bits[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
			}
		}
		if (!writer.write_bytes(present_bits, bitset_bytes)) {
			return PackedByteArray();
		}
		for (int i = 0; i < racer_count; ++i) {
			if (!frame->present[i]) {
				continue;
			}
			uint8_t encoded[MXT_NET_MAX_INPUT_BYTES] = {};
			const int encoded_len = PlayerInput::encode_to_raw(frame->inputs[i], encoded, MXT_NET_MAX_INPUT_BYTES);
			if (!writer.write_u8(static_cast<uint8_t>(encoded_len)) || !writer.write_bytes(encoded, encoded_len)) {
				return PackedByteArray();
			}
		}
	}
	return writer.to_pba();
}

godot::Dictionary NetcodeSession::store_authoritative_input_packet(godot::PackedByteArray packet)
{
	Dictionary stats;
	stats["first_tick"] = -1;
	stats["last_tick"] = -1;
	stats["count"] = 0;
	stats["valid"] = false;

	PacketReader reader(packet);
	uint8_t type = 0, version = 0, count = 0;
	uint16_t packet_racer_count = 0;
	int32_t first_tick = -1;
	if (!reader.read_u8(type) || !reader.read_u8(version) ||
		type != MXT_NET_PACKET_AUTHORITATIVE_INPUTS || version != MXT_NET_PACKET_VERSION ||
		!reader.read_i32(first_tick) || !reader.read_u8(count) || !reader.read_u16(packet_racer_count)) {
		return stats;
	}
	if (packet_racer_count != static_cast<uint16_t>(racer_count)) {
		return stats;
	}
	stats["first_tick"] = first_tick;
	stats["count"] = static_cast<int>(count);
	stats["valid"] = true;
	bool valid = true;

	const int bitset_bytes = (racer_count + 7) >> 3;
	uint8_t present_bits[128] = {};
	for (int f = 0; f < static_cast<int>(count); ++f) {
		std::memset(present_bits, 0, sizeof(present_bits));
		const uint8_t* bit_bytes = nullptr;
		if (!reader.read_bytes(bit_bytes, bitset_bytes)) {
			valid = false;
			stats["valid"] = false;
			break;
		}
		std::memcpy(present_bits, bit_bytes, static_cast<size_t>(bitset_bytes));
		const int tick = first_tick + f;
		InputFrame& frame = frame_for(authoritative_history, tick);
		clear_frame(frame, tick);
		for (int i = 0; i < racer_count; ++i) {
			if ((present_bits[i >> 3] & static_cast<uint8_t>(1u << (i & 7))) == 0) {
				continue;
			}
			uint8_t len = 0;
			const uint8_t* bytes = nullptr;
			if (!reader.read_u8(len) || len > MXT_NET_MAX_INPUT_BYTES || !reader.read_bytes(bytes, len)) {
				valid = false;
				stats["valid"] = false;
				break;
			}
			frame.inputs[i] = PlayerInput::from_raw(bytes, len);
			frame.present[i] = 1;
		}
		if (!valid) {
			break;
		}
		latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
		stats["last_tick"] = tick;
	}
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
	InputFrame& pending = frame_for(pending_inputs, tick);
	InputFrame& authoritative = frame_for(authoritative_history, tick);
	clear_frame(authoritative, tick);
	for (int i = 0; i < racer_count; ++i) {
		if (cpu_flags[i]) {
			godot::PackedByteArray cpu_bytes = sim->generate_native_cpu_input_for_tick(player_ids[i], tick);
			authoritative.inputs[i] = PlayerInput::from_bytes(cpu_bytes);
			authoritative.present[i] = 1;
		} else if (pending.present[i]) {
			authoritative.inputs[i] = pending.inputs[i];
			authoritative.present[i] = 1;
		}
	}
	sim->tick_gamesim_internal(GameSim::InputFrameMode::DecodedCarArray,
		-1, nullptr, authoritative.inputs, authoritative.present, racer_count);
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
					godot::PackedByteArray cpu_bytes = sim->generate_native_cpu_input_for_tick(player_ids[i], tick);
					frame.inputs[i] = PlayerInput::from_bytes(cpu_bytes);
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
