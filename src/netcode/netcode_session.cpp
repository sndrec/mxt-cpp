#include "netcode/netcode_session.h"

#include "gamesim/gamesim.h"
#include "netcode/auth_input_codec.h"
#include "godot_cpp/core/class_db.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>

using namespace godot;
using namespace mxt::auth_input;

namespace {
constexpr int MXT_NET_MAX_INPUT_BYTES = 8;
constexpr int MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET = 255;
constexpr uint32_t MXT_NET_RACE_PHASE_BIT = 0x80000000u;
constexpr uint32_t MXT_NET_TICK_MASK = 0x7fffffffu;

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

bool read_packet_input(PacketReader& reader, PlayerInput& out)
{
	const uint8_t* bytes = nullptr;
	if (!reader.read_bytes(bytes, 1)) {
		return false;
	}
	const int len = PlayerInput::encoded_raw_size_from_mask(bytes[0]);
	reader.pos -= 1;
	if (len > MXT_NET_MAX_INPUT_BYTES || !reader.read_bytes(bytes, len)) {
		return false;
	}
	out = PlayerInput::from_raw(bytes, len);
	return true;
}

uint8_t input_button_mask(const PlayerInput& input)
{
	uint8_t mask = 0;
	if (input.accelerate > 0.0f) mask |= 1u << 0;
	if (input.brake > 0.0f) mask |= 1u << 1;
	if (input.spinattack) mask |= 1u << 2;
	if (input.sideattack) mask |= 1u << 3;
	if (input.boost) mask |= 1u << 4;
	return mask;
}

uint8_t input_trigger_byte(float v)
{
	return PlayerInput::quantize_trigger(v);
}

uint8_t input_axis_byte(float v)
{
	return PlayerInput::quantize_axis(v);
}

uint8_t zigzag_i8(int v)
{
	return static_cast<uint8_t>(((v << 1) ^ (v >> 7)) & 0xff);
}

int unzigzag_i8(uint8_t v)
{
	return (static_cast<int>(v) >> 1) ^ -static_cast<int>(v & 1u);
}

int wrapped_i8_delta(uint8_t value, uint8_t previous)
{
	return ((static_cast<int>(value) - static_cast<int>(previous) + 128) & 0xff) - 128;
}

float trigger_from_byte(uint8_t v)
{
	return static_cast<float>(v) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION);
}

float axis_from_byte(uint8_t v)
{
	return (static_cast<float>(v) / static_cast<float>(PlayerInput::RAW_BIT_PRECISION)) * 2.0f - 1.0f;
}

int32_t pack_tick_phase(int tick, int race_phase)
{
	const uint32_t tick_bits = static_cast<uint32_t>(std::max(tick, 0)) & MXT_NET_TICK_MASK;
	const uint32_t phase_bit = (race_phase & 1) ? MXT_NET_RACE_PHASE_BIT : 0u;
	return static_cast<int32_t>(tick_bits | phase_bit);
}

int unpack_tick(int32_t packed_tick)
{
	return static_cast<int>(static_cast<uint32_t>(packed_tick) & MXT_NET_TICK_MASK);
}

int unpack_race_phase(int32_t packed_tick)
{
	return (static_cast<uint32_t>(packed_tick) & MXT_NET_RACE_PHASE_BIT) ? 1 : 0;
}

}

bool NetcodeSession::write_authoritative_input_raw(
	PackedByteArray& raw,
	const InputFrame* const* frames,
	int frame_count,
	uint8_t p_layout) const
{
	const AuthInputLayout layout = static_cast<AuthInputLayout>(p_layout);
	uint8_t* raw_bytes = raw.ptrw();
	int raw_pos = 0;
	const bool bitpacked_layout = layout == LAYOUT_BITPACKED_BUTTONS ||
		layout == LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	const bool bitpacked_analog_delta = layout == LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	if (!bitpacked_layout) {
		return false;
	}
	{
		std::memset(raw_bytes, 0, static_cast<size_t>(raw.size()));
		const int input_count = frame_count * racer_count;
		const int bitset_bytes = (input_count + 7) >> 3;
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const int input_index = f * racer_count + i;
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				const uint8_t mask = input_button_mask(input);
				for (int bit = 0; bit < 5; ++bit) {
					if ((mask & (1u << bit)) != 0) {
						raw_bytes[bit * bitset_bytes + (input_index >> 3)] |= static_cast<uint8_t>(1u << (input_index & 7));
					}
				}
			}
		}
		raw_pos = bitset_bytes * 5;
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_trigger_byte(input.strafe_left);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_trigger_byte(prev.strafe_left)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_trigger_byte(input.strafe_right);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_trigger_byte(prev.strafe_right)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_axis_byte(input.steer_horizontal);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_axis_byte(prev.steer_horizontal)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		for (int f = 0; f < frame_count; ++f) {
			const InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				const PlayerInput& input = frame->present[i] ? frame->inputs[i] : neutral_input;
				uint8_t value = input_axis_byte(input.steer_vertical);
				if (bitpacked_analog_delta && f > 0) {
					const InputFrame* prev_frame = frames[f - 1];
					const PlayerInput& prev = prev_frame->present[i] ? prev_frame->inputs[i] : neutral_input;
					value = zigzag_i8(wrapped_i8_delta(value, input_axis_byte(prev.steer_vertical)));
				}
				raw_bytes[raw_pos++] = value;
			}
		}
		return raw_pos == raw.size();
	}
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
	std::memset(frame.present, 0, static_cast<size_t>(std::max(racer_count, 0)));
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

static inline uint32_t hash_racer_player_id(int32_t player_id)
{
	uint32_t x = static_cast<uint32_t>(player_id);
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

void NetcodeSession::clear_racer_lookup()
{
	for (int i = 0; i < RACER_LOOKUP_SIZE; ++i) {
		racer_lookup_ids[i] = RACER_LOOKUP_EMPTY;
		racer_lookup_indices[i] = -1;
	}
}

void NetcodeSession::insert_racer_lookup(int32_t player_id, int index)
{
	uint32_t slot = hash_racer_player_id(player_id) & RACER_LOOKUP_MASK;
	for (int probe = 0; probe < RACER_LOOKUP_SIZE; ++probe) {
		if (racer_lookup_ids[slot] == player_id) {
			return;
		}
		if (racer_lookup_ids[slot] == RACER_LOOKUP_EMPTY) {
			racer_lookup_ids[slot] = player_id;
			racer_lookup_indices[slot] = static_cast<int16_t>(index);
			return;
		}
		slot = (slot + 1) & RACER_LOOKUP_MASK;
	}
}

int NetcodeSession::find_racer_index(int32_t player_id) const
{
	uint32_t slot = hash_racer_player_id(player_id) & RACER_LOOKUP_MASK;
	for (int probe = 0; probe < RACER_LOOKUP_SIZE; ++probe) {
		const int32_t id = racer_lookup_ids[slot];
		if (id == player_id) {
			return racer_lookup_indices[slot];
		}
		if (id == RACER_LOOKUP_EMPTY) {
			return -1;
		}
		slot = (slot + 1) & RACER_LOOKUP_MASK;
	}
	return -1;
}

int NetcodeSession::find_peer_index(int32_t peer_id) const
{
	for (int i = 0; i < MAX_PEERS; ++i) {
		if (peer_states[i].active && peer_states[i].id == peer_id) {
			return i;
		}
	}
	return -1;
}

int NetcodeSession::ensure_peer_index(int32_t peer_id)
{
	const int existing = find_peer_index(peer_id);
	if (existing >= 0) {
		return existing;
	}
	for (int i = 0; i < MAX_PEERS; ++i) {
		if (!peer_states[i].active) {
			peer_states[i] = PeerState();
			peer_states[i].id = peer_id;
			peer_states[i].active = 1;
			return i;
		}
	}
	return -1;
}

void NetcodeSession::reset()
{
	racer_count = 0;
	cpu_racer_count = 0;
	local_player_id = -1;
	latest_authoritative_tick = -1;
	last_pending_packet_result = PendingInputPacketResult();
	last_authoritative_packet_result = AuthoritativeInputPacketResult();
	last_consumed_authoritative_packet_stats = AuthoritativePacketStats();
	last_replaced_pending_player_count = 0;
	stat_auth_packets = 0;
	stat_auth_frames = 0;
	stat_auth_encoded_inputs = 0;
	stat_auth_unchanged_inputs = 0;
	stat_auth_raw_bytes = 0;
	stat_auth_payload_bytes = 0;
	stat_auth_compression_candidates = 0;
	stat_auth_build_usec = 0;
	last_local_input = neutral_input;
	for (int i = 0; i < MAX_RACERS; ++i) {
		player_ids[i] = 0;
		cpu_flags[i] = 0;
	}
	clear_racer_lookup();
	clear_peer_state();
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
	cpu_racer_count = 0;
	local_player_id = p_local_player_id;
	latest_authoritative_tick = -1;
	last_pending_packet_result = PendingInputPacketResult();
	last_authoritative_packet_result = AuthoritativeInputPacketResult();
	last_consumed_authoritative_packet_stats = AuthoritativePacketStats();
	last_replaced_pending_player_count = 0;
	stat_auth_packets = 0;
	stat_auth_frames = 0;
	stat_auth_encoded_inputs = 0;
	stat_auth_unchanged_inputs = 0;
	stat_auth_raw_bytes = 0;
	stat_auth_payload_bytes = 0;
	stat_auth_compression_candidates = 0;
	stat_auth_build_usec = 0;
	for (int i = 0; i < racer_count; ++i) {
		player_ids[i] = static_cast<int32_t>(static_cast<int64_t>(p_player_ids[i]));
		cpu_flags[i] = (i < p_cpu_flags.size() && static_cast<bool>(p_cpu_flags[i])) ? 1 : 0;
		cpu_racer_count += cpu_flags[i] != 0 ? 1 : 0;
	}
	for (int i = racer_count; i < MAX_RACERS; ++i) {
		player_ids[i] = 0;
		cpu_flags[i] = 0;
	}
	clear_racer_lookup();
	for (int i = 0; i < racer_count; ++i) {
		insert_racer_lookup(player_ids[i], i);
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

int NetcodeSession::fill_missing_pending_inputs(int tick, godot::Array p_player_ids, godot::Array p_disconnected_ids, godot::Array p_delayed_ids, bool allow_new_delayed)
{
	last_replaced_pending_player_count = 0;
	const InputFrame* prev = find_frame(authoritative_history, static_cast<int32_t>(tick - 1));
	InputFrame& frame = frame_for(pending_inputs, static_cast<int32_t>(tick));

	for (int p = 0; p < p_player_ids.size(); ++p) {
		const int32_t player_id = static_cast<int32_t>(static_cast<int64_t>(p_player_ids[p]));
		const int index = find_racer_index(player_id);
		if (index < 0 || cpu_flags[index]) {
			continue;
		}
		if (frame.present[index]) {
			continue;
		}

		bool disconnected = false;
		for (int i = 0; i < p_disconnected_ids.size(); ++i) {
			if (static_cast<int32_t>(static_cast<int64_t>(p_disconnected_ids[i])) == player_id) {
				disconnected = true;
				break;
			}
		}

		bool already_delayed = false;
		if (!disconnected) {
			for (int i = 0; i < p_delayed_ids.size(); ++i) {
				if (static_cast<int32_t>(static_cast<int64_t>(p_delayed_ids[i])) == player_id) {
					already_delayed = true;
					break;
				}
			}
		}

		if (!disconnected && !peer_has_received(player_id)) {
			continue;
		}
		if (!disconnected && !allow_new_delayed && !already_delayed) {
			continue;
		}

		frame.inputs[index] = (prev && prev->present[index]) ? prev->inputs[index] : neutral_input;
		frame.present[index] = 1;
		last_replaced_pending_player_ids[last_replaced_pending_player_count++] = player_id;
	}

	return last_replaced_pending_player_count;
}

int NetcodeSession::get_last_replaced_pending_player_id(int index) const
{
	if (index < 0 || index >= last_replaced_pending_player_count) {
		return -1;
	}
	return last_replaced_pending_player_ids[index];
}

godot::PackedByteArray NetcodeSession::build_local_input_packet(int first_tick, int count, int race_phase) const
{
	PacketWriter writer;
	const int index = find_racer_index(local_player_id);
	if (index < 0 || count <= 0) {
		return writer.to_pba();
	}
	count = std::min(count, 255);
	if (!writer.write_i32(pack_tick_phase(first_tick, race_phase)) ||
		!writer.write_u8(static_cast<uint8_t>(count))) {
		return PackedByteArray();
	}
	for (int i = 0; i < count; ++i) {
		const int tick = first_tick + i;
		const InputFrame* frame = find_frame(local_input_history, tick);
		const PlayerInput& input = (frame && frame->present[index]) ? frame->inputs[index] : last_local_input;
		uint8_t encoded[MXT_NET_MAX_INPUT_BYTES] = {};
		const int encoded_len = PlayerInput::encode_to_raw(input, encoded, MXT_NET_MAX_INPUT_BYTES);
		if (!writer.write_bytes(encoded, encoded_len)) {
			return PackedByteArray();
		}
	}
	return writer.to_pba();
}

int NetcodeSession::store_pending_input_packet(int player_id, int reject_before_tick, godot::PackedByteArray packet, double ahead, double now_sec, int expected_race_phase)
{
	last_pending_packet_result = PendingInputPacketResult();

	const int index = find_racer_index(static_cast<int32_t>(player_id));
	if (index < 0) {
		return PACKET_STORE_INVALID;
	}
	const int peer_index = ensure_peer_index(static_cast<int32_t>(player_id));
	const bool seen_before = peer_index >= 0 && peer_states[peer_index].last_received_tick >= 0;
	last_pending_packet_result.seen_before = seen_before ? 1 : 0;
	if (peer_index >= 0) {
		peer_states[peer_index].desired_ahead = static_cast<float>(ahead);
	}
	PacketReader reader(packet);
	uint8_t count = 0;
	int32_t packed_start_tick = -1;
	if (!reader.read_i32(packed_start_tick) || !reader.read_u8(count)) {
		return PACKET_STORE_INVALID;
	}
	if (unpack_race_phase(packed_start_tick) != (expected_race_phase & 1)) {
		return PACKET_STORE_STALE;
	}
	const int start_tick = unpack_tick(packed_start_tick);
	last_pending_packet_result.start_tick = start_tick;
	last_pending_packet_result.count = static_cast<int32_t>(count);
	if (count > 0 && start_tick + static_cast<int32_t>(count) <= reject_before_tick) {
		last_pending_packet_result.dropped = static_cast<int32_t>(count);
		return PACKET_STORE_VALID;
	}

	int accepted = 0;
	int dropped = 0;
	int last_tick = -1;
	for (int i = 0; i < static_cast<int>(count); ++i) {
		const uint8_t* bytes = nullptr;
		if (!reader.read_bytes(bytes, 1)) {
			return PACKET_STORE_INVALID;
		}
		const int len = PlayerInput::encoded_raw_size_from_mask(bytes[0]);
		reader.pos -= 1;
		if (len > MXT_NET_MAX_INPUT_BYTES || !reader.read_bytes(bytes, len)) {
			return PACKET_STORE_INVALID;
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
	last_pending_packet_result.accepted = accepted;
	last_pending_packet_result.dropped = dropped;
	last_pending_packet_result.last_tick = last_tick;
	if (accepted > 0 && peer_index >= 0) {
		peer_states[peer_index].last_received_tick = last_tick;
		peer_states[peer_index].last_input_time = now_sec;
	}
	return PACKET_STORE_VALID;
}

int NetcodeSession::get_last_pending_packet_start_tick() const
{
	return last_pending_packet_result.start_tick;
}

int NetcodeSession::get_last_pending_packet_count() const
{
	return last_pending_packet_result.count;
}

int NetcodeSession::get_last_pending_packet_accepted() const
{
	return last_pending_packet_result.accepted;
}

int NetcodeSession::get_last_pending_packet_dropped() const
{
	return last_pending_packet_result.dropped;
}

int NetcodeSession::get_last_pending_packet_last_tick() const
{
	return last_pending_packet_result.last_tick;
}

bool NetcodeSession::get_last_pending_packet_seen_before() const
{
	return last_pending_packet_result.seen_before != 0;
}

godot::PackedByteArray NetcodeSession::build_authoritative_input_packet(int last_tick, int max_frame_count, int race_phase) const
{
	const auto build_start = std::chrono::steady_clock::now();
	PacketWriter writer;
	if (max_frame_count <= 0 || last_tick < 0) {
		writer.write_u8(pack_mode_count_phase(MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD, 0, race_phase));
		writer.write_u8(0);
		return writer.to_pba();
	}
	max_frame_count = std::min(max_frame_count, MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET);
	int first_tick = last_tick - max_frame_count + 1;
	if (first_tick < 0) {
		first_tick = 0;
	}
	while (first_tick <= last_tick && !find_frame(authoritative_history, first_tick)) {
		++first_tick;
	}
	int count = last_tick >= first_tick ? last_tick - first_tick + 1 : 0;
	while (count > 0 && !find_frame(authoritative_history, first_tick + count - 1)) {
		--count;
	}
	const int mode_count_pos = writer.pos;
	if (!writer.write_u8(pack_mode_count_phase(MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD, count, race_phase))) {
		return PackedByteArray();
	}
	if (count_needs_escape(count) && !writer.write_u8(static_cast<uint8_t>(count))) {
		return PackedByteArray();
	}
	++stat_auth_packets;
	stat_auth_frames += static_cast<uint64_t>(count);
	stat_auth_encoded_inputs += static_cast<uint64_t>(count) * static_cast<uint64_t>(racer_count);
	if (count == 0 || racer_count <= 0) {
		stat_auth_payload_bytes += static_cast<uint64_t>(writer.pos);
		return writer.to_pba();
	}
	const int bitpacked_raw_size = raw_size(count, racer_count, LAYOUT_BITPACKED_BUTTONS);
	const int hybrid_raw_size = raw_size(count, racer_count, LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA);
	const InputFrame* frames[MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET] = {};
	for (int f = 0; f < count; ++f) {
		frames[f] = find_frame(authoritative_history, first_tick + f);
		if (!frames[f]) {
			return PackedByteArray();
		}
	}
	PackedByteArray bitpacked_raw;
	if (bitpacked_raw.resize(bitpacked_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(bitpacked_raw, frames, count, LAYOUT_BITPACKED_BUTTONS)) {
		return PackedByteArray();
	}
	PackedByteArray bitpacked_zero_raw = encode_zero_bitmap(bitpacked_raw);
	if (bitpacked_zero_raw.size() <= 0) {
		return PackedByteArray();
	}
	PackedByteArray bitpacked_zero_strafe_sparse_raw;
	if (count == 2) {
		bitpacked_zero_strafe_sparse_raw = encode_strafe_sparse(bitpacked_raw, count, racer_count);
	}
	PackedByteArray hybrid_raw;
	if (hybrid_raw.resize(hybrid_raw_size) != 0) {
		return PackedByteArray();
	}
	if (!write_authoritative_input_raw(hybrid_raw, frames, count, LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA)) {
		return PackedByteArray();
	}
	const EncodedSelection selected = select_best(bitpacked_zero_raw, bitpacked_zero_strafe_sparse_raw, hybrid_raw);
	if (selected.payload.is_empty() || !writer.write_bytes(selected.payload.ptr(), selected.payload.size())) {
		return PackedByteArray();
	}
	writer.data[mode_count_pos] = pack_mode_count_phase(selected.mode, count, race_phase);
	stat_auth_raw_bytes += static_cast<uint64_t>(selected.raw_size);
	stat_auth_payload_bytes += static_cast<uint64_t>(writer.pos);
	stat_auth_compression_candidates += static_cast<uint64_t>(selected.candidate_count);
	stat_auth_build_usec += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - build_start).count());
	return writer.to_pba();
}

int NetcodeSession::store_authoritative_input_packet(godot::PackedByteArray packet, int expected_race_phase, int authoritative_last_tick, int external_mode_count_phase)
{
	last_authoritative_packet_result = AuthoritativeInputPacketResult();

	PacketReader reader(packet);
	uint8_t count = 0;
	uint8_t mode_count_phase = MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD;
	if (external_mode_count_phase >= 0) {
		mode_count_phase = static_cast<uint8_t>(external_mode_count_phase & 0xff);
	} else {
		if (!reader.read_u8(mode_count_phase)) {
			return PACKET_STORE_INVALID;
		}
	}
	const uint8_t compression_mode = mode_count_phase & MODE_MASK;
	const uint8_t count_code = static_cast<uint8_t>((mode_count_phase & COUNT_MASK) >> COUNT_SHIFT);
	if (count_code == COUNT_ESCAPE) {
		if (!reader.read_u8(count)) {
			return PACKET_STORE_INVALID;
		}
	} else {
		count = static_cast<uint8_t>(count_code + 1);
	}
	if (((mode_count_phase & PHASE_BIT) ? 1 : 0) != (expected_race_phase & 1)) {
		return PACKET_STORE_STALE;
	}
	if (!mode_valid(compression_mode) || authoritative_last_tick < 0 || static_cast<int>(count) - 1 > authoritative_last_tick) {
		return PACKET_STORE_INVALID;
	}
	const int first_tick = authoritative_last_tick - static_cast<int>(count) + 1;
	last_authoritative_packet_result.first_tick = first_tick;
	last_authoritative_packet_result.count = static_cast<int32_t>(count);
	if (count == 0) {
		return PACKET_STORE_VALID;
	}

	const AuthInputLayout packet_layout = layout_from_mode(compression_mode);
	const bool zero_bitmap_strafe_sparse_layout = packet_layout == LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP_STRAFE_SPARSE;
	const bool zero_bitmap_layout = packet_layout == LAYOUT_BITPACKED_BUTTONS_ZERO_BITMAP || zero_bitmap_strafe_sparse_layout;
	const int raw_size_bound = raw_size(static_cast<int>(count), racer_count, packet_layout);
	if (raw_size_bound <= 0) {
		return PACKET_STORE_INVALID;
	}

	const PackedByteArray compressed = packet.slice(reader.pos, packet.size());
	PackedByteArray raw;
	switch (compression_mode) {
		case MODE_BITPACKED_ZERO_BITMAP_DICT_ZSTD:
			raw = decompress_with_dictionary_bound(compressed, raw_size_bound, DICTIONARY_ZERO_BITMAP);
			break;
		case MODE_BITPACKED_ZERO_BITMAP_STRAFE_SPARSE_DICT_ZSTD:
			raw = decompress_with_dictionary_bound(compressed, raw_size_bound, DICTIONARY_STRAFE_SPARSE);
			break;
		case MODE_BITPACKED_DELTA_SMOOTH_DICT_ZSTD:
			raw = decompress_with_dictionary(compressed, raw_size_bound, DICTIONARY_SMOOTH_ANALOG_DELTA);
			break;
		case MODE_BITPACKED_ZERO_BITMAP_PLAIN_ZSTD:
			raw = decompress_plain_bound(compressed, raw_size_bound);
			break;
		default:
			return PACKET_STORE_INVALID;
	}
	if ((!zero_bitmap_layout && raw.size() != raw_size_bound) || (zero_bitmap_layout && (raw.size() <= 0 || raw.size() > raw_size_bound))) {
		return PACKET_STORE_INVALID;
	}

	AuthInputLayout layout = packet_layout;
	if (zero_bitmap_strafe_sparse_layout) {
		PackedByteArray expanded_raw;
		if (!decode_strafe_sparse(raw, expanded_raw, static_cast<int>(count), racer_count)) {
			return PACKET_STORE_INVALID;
		}
		raw = expanded_raw;
		layout = LAYOUT_BITPACKED_BUTTONS;
	} else if (zero_bitmap_layout) {
		PackedByteArray expanded_raw;
		const int bitpacked_size = bitpacked_raw_size(static_cast<int>(count), racer_count);
		if (!decode_zero_bitmap(raw, expanded_raw, bitpacked_size)) {
			return PACKET_STORE_INVALID;
		}
		raw = expanded_raw;
		layout = LAYOUT_BITPACKED_BUTTONS;
	}
	const uint8_t* raw_bytes = raw.ptr();
	const bool analog_delta_layout = layout == LAYOUT_BITPACKED_BUTTONS_ANALOG_DELTA;
	const int input_count = static_cast<int>(count) * racer_count;
	const int bitset_bytes = (input_count + 7) >> 3;
	const int expected_bitpacked_size = bitset_bytes * 5 + input_count * 4;
	if (raw.size() != expected_bitpacked_size) {
		return PACKET_STORE_INVALID;
	}

	InputFrame* frames[MXT_NET_MAX_AUTHORITATIVE_FRAMES_PER_PACKET] = {};
	for (int f = 0; f < static_cast<int>(count); ++f) {
		const int tick = first_tick + f;
		InputFrame& frame = frame_for(authoritative_history, tick);
		clear_frame(frame, tick);
		frames[f] = &frame;
		for (int i = 0; i < racer_count; ++i) {
			frame.inputs[i] = neutral_input;
			frame.present[i] = 1;
		}
	}

	for (int f = 0; f < static_cast<int>(count); ++f) {
		InputFrame* frame = frames[f];
		for (int i = 0; i < racer_count; ++i) {
			const int input_index = f * racer_count + i;
			uint8_t mask = 0;
			for (int bit = 0; bit < 5; ++bit) {
				if ((raw_bytes[bit * bitset_bytes + (input_index >> 3)] &
						static_cast<uint8_t>(1u << (input_index & 7))) != 0) {
					mask |= static_cast<uint8_t>(1u << bit);
				}
			}
			frame->inputs[i].accelerate = (mask & (1u << 0)) != 0 ? 1.0f : 0.0f;
			frame->inputs[i].brake = (mask & (1u << 1)) != 0 ? 1.0f : 0.0f;
			frame->inputs[i].spinattack = (mask & (1u << 2)) != 0;
			frame->inputs[i].sideattack = (mask & (1u << 3)) != 0;
			frame->inputs[i].boost = (mask & (1u << 4)) != 0;
		}
	}

	int raw_pos = bitset_bytes * 5;
	uint8_t previous_values[MAX_RACERS] = {};
	for (int component = 0; component < 4; ++component) {
		std::memset(previous_values, 0, sizeof(previous_values));
		for (int f = 0; f < static_cast<int>(count); ++f) {
			InputFrame* frame = frames[f];
			for (int i = 0; i < racer_count; ++i) {
				uint8_t value = raw_bytes[raw_pos++];
				if (analog_delta_layout && f > 0) {
					value = static_cast<uint8_t>(static_cast<int>(previous_values[i]) + unzigzag_i8(value));
				}
				previous_values[i] = value;
				switch (component) {
					case 0:
						frame->inputs[i].strafe_left = trigger_from_byte(value);
						break;
					case 1:
						frame->inputs[i].strafe_right = trigger_from_byte(value);
						break;
					case 2:
						frame->inputs[i].steer_horizontal = axis_from_byte(value);
						break;
					default:
						frame->inputs[i].steer_vertical = axis_from_byte(value);
						break;
				}
			}
		}
	}
	if (raw_pos != raw.size()) {
		return PACKET_STORE_INVALID;
	}

	for (int f = 0; f < static_cast<int>(count); ++f) {
		const int tick = first_tick + f;
		latest_authoritative_tick = std::max(latest_authoritative_tick, static_cast<int32_t>(tick));
		last_authoritative_packet_result.last_tick = tick;
	}
	return PACKET_STORE_VALID;
}

int NetcodeSession::get_last_authoritative_packet_first_tick() const
{
	return last_authoritative_packet_result.first_tick;
}

int NetcodeSession::get_last_authoritative_packet_last_tick() const
{
	return last_authoritative_packet_result.last_tick;
}

int NetcodeSession::get_last_authoritative_packet_count() const
{
	return last_authoritative_packet_result.count;
}
