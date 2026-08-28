#include "core/race_configuration.h"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace godot;

namespace {

static constexpr uint8_t WIRE_MAGIC[8] = {'M', 'X', 'T', 'R', 'C', 'F', 'G', 0};
static constexpr uint8_t WIRE_VERSION = 1;
static constexpr uint32_t MAX_REASON_BYTES = 1024;
static constexpr uint32_t MAX_CPU_VEHICLE_IDS = 1024;
static constexpr uint32_t MAX_CONTENT_ID_BYTES = 1024;

enum WireFlags : uint16_t {
	FLAG_VEHICLE_RESTORE = 1u << 0,
	FLAG_BUMPERS = 1u << 1,
	FLAG_S_BOOST = 1u << 2,
	FLAG_ALLOW_WORKSHOP = 1u << 3,
	FLAG_BOOST_UNLOCKED = 1u << 4,
	FLAG_LEADERBOARD_ELIGIBLE = 1u << 5,
	FLAG_RESUMED_FROM_REPLAY = 1u << 6,
	FLAG_CUSTOM_CONTENT = 1u << 7,
};

struct Writer {
	std::vector<uint8_t> bytes;

	void u8(uint8_t value) { bytes.push_back(value); }
	void u16(uint16_t value) {
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}
	void u32(uint32_t value) {
		for (uint32_t shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
	}
	void i64(int64_t value) {
		const uint64_t bits = static_cast<uint64_t>(value);
		u32(static_cast<uint32_t>(bits));
		u32(static_cast<uint32_t>(bits >> 32));
	}
	void string(const String &value) {
		const CharString utf8 = value.utf8();
		const uint32_t size = static_cast<uint32_t>(utf8.length());
		u32(size);
		bytes.insert(bytes.end(), reinterpret_cast<const uint8_t *>(utf8.get_data()), reinterpret_cast<const uint8_t *>(utf8.get_data()) + size);
	}
};

struct Reader {
	const uint8_t *data = nullptr;
	size_t size = 0;
	size_t cursor = 0;

	bool raw(void *out, size_t count) {
		if (cursor + count > size) return false;
		std::memcpy(out, data + cursor, count);
		cursor += count;
		return true;
	}
	bool u8(uint8_t &out) { return raw(&out, sizeof(out)); }
	bool u16(uint16_t &out) {
		uint8_t bytes[2];
		if (!raw(bytes, sizeof(bytes))) return false;
		out = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
		return true;
	}
	bool u32(uint32_t &out) {
		uint8_t bytes[4];
		if (!raw(bytes, sizeof(bytes))) return false;
		out = static_cast<uint32_t>(bytes[0]) |
			(static_cast<uint32_t>(bytes[1]) << 8) |
			(static_cast<uint32_t>(bytes[2]) << 16) |
			(static_cast<uint32_t>(bytes[3]) << 24);
		return true;
	}
	bool i64(int64_t &out) {
		uint32_t low = 0;
		uint32_t high = 0;
		if (!u32(low) || !u32(high)) return false;
		out = static_cast<int64_t>(static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32));
		return true;
	}
	bool string(String &out, uint32_t maximum) {
		uint32_t count = 0;
		if (!u32(count) || count > maximum || cursor + count > size) return false;
		out = String::utf8(reinterpret_cast<const char *>(data + cursor), count);
		cursor += count;
		return true;
	}
};

static int32_t clamped_u16(int32_t value) {
	return std::clamp(value, 0, 65535);
}

} // namespace

void MxtRaceConfiguration::_bind_methods() {
	ClassDB::bind_method(D_METHOD("copy"), &MxtRaceConfiguration::copy);
	ClassDB::bind_method(D_METHOD("reset"), &MxtRaceConfiguration::reset);
	ClassDB::bind_method(D_METHOD("is_time_attack"), &MxtRaceConfiguration::is_time_attack);
	ClassDB::bind_method(D_METHOD("is_practice"), &MxtRaceConfiguration::is_practice);
	ClassDB::bind_method(D_METHOD("encode_wire"), &MxtRaceConfiguration::encode_wire);
	ClassDB::bind_method(D_METHOD("decode_wire", "bytes"), &MxtRaceConfiguration::decode_wire);
	ClassDB::bind_method(D_METHOD("to_metadata_dictionary"), &MxtRaceConfiguration::to_metadata_dictionary);
	ClassDB::bind_method(D_METHOD("load_metadata_dictionary", "value"), &MxtRaceConfiguration::load_metadata_dictionary);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtRaceConfiguration::get_last_error);

#define BIND_PROPERTY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtRaceConfiguration::get_##name); \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &MxtRaceConfiguration::set_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "set_" #name, "get_" #name)
	BIND_PROPERTY(Variant::INT, session_kind);
	BIND_PROPERTY(Variant::INT, game_mode);
	BIND_PROPERTY(Variant::INT, cpu_count);
	BIND_PROPERTY(Variant::INT, lap_count);
	BIND_PROPERTY(Variant::INT, time_attack_ruleset_revision);
	BIND_PROPERTY(Variant::INT, practice_local_player_id);
	BIND_PROPERTY(Variant::BOOL, vehicle_restore);
	BIND_PROPERTY(Variant::BOOL, bumpers);
	BIND_PROPERTY(Variant::BOOL, s_boost);
	BIND_PROPERTY(Variant::BOOL, allow_workshop_vehicles);
	BIND_PROPERTY(Variant::BOOL, boost_unlocked_from_start);
	BIND_PROPERTY(Variant::BOOL, leaderboard_eligible);
	BIND_PROPERTY(Variant::BOOL, resumed_from_replay);
	BIND_PROPERTY(Variant::BOOL, custom_content);
	BIND_PROPERTY(Variant::STRING, leaderboard_ineligible_reason);
	BIND_PROPERTY(Variant::PACKED_STRING_ARRAY, cpu_vehicle_content_ids);
#undef BIND_PROPERTY

	BIND_ENUM_CONSTANT(SESSION_STANDARD);
	BIND_ENUM_CONSTANT(SESSION_TIME_ATTACK);
	BIND_ENUM_CONSTANT(SESSION_PRACTICE);
}

Ref<MxtRaceConfiguration> MxtRaceConfiguration::copy() const {
	Ref<MxtRaceConfiguration> out;
	out.instantiate();
	out->session_kind = session_kind;
	out->game_mode = game_mode;
	out->cpu_count = cpu_count;
	out->lap_count = lap_count;
	out->time_attack_ruleset_revision = time_attack_ruleset_revision;
	out->practice_local_player_id = practice_local_player_id;
	out->vehicle_restore = vehicle_restore;
	out->bumpers = bumpers;
	out->s_boost = s_boost;
	out->allow_workshop_vehicles = allow_workshop_vehicles;
	out->boost_unlocked_from_start = boost_unlocked_from_start;
	out->leaderboard_eligible = leaderboard_eligible;
	out->resumed_from_replay = resumed_from_replay;
	out->custom_content = custom_content;
	out->leaderboard_ineligible_reason = leaderboard_ineligible_reason;
	out->cpu_vehicle_content_ids = cpu_vehicle_content_ids;
	return out;
}

void MxtRaceConfiguration::reset() {
	session_kind = SESSION_STANDARD;
	game_mode = 0;
	cpu_count = 0;
	lap_count = 3;
	time_attack_ruleset_revision = 0;
	practice_local_player_id = -1;
	vehicle_restore = true;
	bumpers = false;
	s_boost = true;
	allow_workshop_vehicles = true;
	boost_unlocked_from_start = false;
	leaderboard_eligible = false;
	resumed_from_replay = false;
	custom_content = false;
	leaderboard_ineligible_reason = String();
	cpu_vehicle_content_ids.clear();
	last_error = String();
}

void MxtRaceConfiguration::set_session_kind(int32_t value) {
	session_kind = static_cast<SessionKind>(std::clamp(value, 0, static_cast<int32_t>(SESSION_PRACTICE)));
}

void MxtRaceConfiguration::set_game_mode(int32_t value) {
	game_mode = static_cast<uint8_t>(std::clamp(value, 0, 255));
}

void MxtRaceConfiguration::set_cpu_count(int32_t value) {
	cpu_count = static_cast<uint16_t>(clamped_u16(value));
}

void MxtRaceConfiguration::set_lap_count(int32_t value) {
	lap_count = static_cast<uint16_t>(clamped_u16(value));
}

void MxtRaceConfiguration::set_time_attack_ruleset_revision(int32_t value) {
	time_attack_ruleset_revision = static_cast<uint16_t>(clamped_u16(value));
}

PackedByteArray MxtRaceConfiguration::encode_wire() const {
	Writer writer;
	writer.bytes.insert(writer.bytes.end(), WIRE_MAGIC, WIRE_MAGIC + sizeof(WIRE_MAGIC));
	writer.u8(WIRE_VERSION);
	writer.u8(static_cast<uint8_t>(session_kind));
	writer.u8(game_mode);
	writer.u16(cpu_count);
	writer.u16(lap_count);
	writer.u16(time_attack_ruleset_revision);
	writer.i64(practice_local_player_id);
	uint16_t flags = 0;
	flags |= vehicle_restore ? FLAG_VEHICLE_RESTORE : 0;
	flags |= bumpers ? FLAG_BUMPERS : 0;
	flags |= s_boost ? FLAG_S_BOOST : 0;
	flags |= allow_workshop_vehicles ? FLAG_ALLOW_WORKSHOP : 0;
	flags |= boost_unlocked_from_start ? FLAG_BOOST_UNLOCKED : 0;
	flags |= leaderboard_eligible ? FLAG_LEADERBOARD_ELIGIBLE : 0;
	flags |= resumed_from_replay ? FLAG_RESUMED_FROM_REPLAY : 0;
	flags |= custom_content ? FLAG_CUSTOM_CONTENT : 0;
	writer.u16(flags);
	writer.string(leaderboard_ineligible_reason);
	writer.u16(static_cast<uint16_t>(std::min<int64_t>(cpu_vehicle_content_ids.size(), MAX_CPU_VEHICLE_IDS)));
	for (int64_t i = 0; i < cpu_vehicle_content_ids.size() && i < MAX_CPU_VEHICLE_IDS; ++i) writer.string(cpu_vehicle_content_ids[i]);
	PackedByteArray out;
	out.resize(static_cast<int64_t>(writer.bytes.size()));
	if (!writer.bytes.empty()) std::memcpy(out.ptrw(), writer.bytes.data(), writer.bytes.size());
	return out;
}

bool MxtRaceConfiguration::decode_wire(const PackedByteArray &bytes) {
	last_error = String();
	Reader reader{bytes.ptr(), static_cast<size_t>(bytes.size()), 0};
	uint8_t magic[sizeof(WIRE_MAGIC)];
	uint8_t version = 0;
	uint8_t kind = 0;
	uint8_t mode = 0;
	uint16_t incoming_cpu_count = 0;
	uint16_t incoming_lap_count = 0;
	uint16_t incoming_ruleset = 0;
	int64_t incoming_local_player_id = -1;
	uint16_t flags = 0;
	String reason;
	uint16_t content_id_count = 0;
	if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, WIRE_MAGIC, sizeof(magic)) != 0 ||
			!reader.u8(version) || version != WIRE_VERSION || !reader.u8(kind) || kind > SESSION_PRACTICE ||
			!reader.u8(mode) || !reader.u16(incoming_cpu_count) || !reader.u16(incoming_lap_count) ||
			!reader.u16(incoming_ruleset) || !reader.i64(incoming_local_player_id) || !reader.u16(flags) ||
			!reader.string(reason, MAX_REASON_BYTES) || !reader.u16(content_id_count) || content_id_count > MAX_CPU_VEHICLE_IDS) {
		last_error = "Malformed race configuration packet.";
		return false;
	}
	PackedStringArray content_ids;
	content_ids.resize(content_id_count);
	for (uint16_t i = 0; i < content_id_count; ++i) {
		String id;
		if (!reader.string(id, MAX_CONTENT_ID_BYTES)) {
			last_error = "Malformed CPU vehicle content ID in race configuration packet.";
			return false;
		}
		content_ids.set(i, id);
	}
	if (reader.cursor != reader.size) {
		last_error = "Race configuration packet has trailing data.";
		return false;
	}
	session_kind = static_cast<SessionKind>(kind);
	game_mode = mode;
	cpu_count = incoming_cpu_count;
	lap_count = incoming_lap_count;
	time_attack_ruleset_revision = incoming_ruleset;
	practice_local_player_id = incoming_local_player_id;
	vehicle_restore = (flags & FLAG_VEHICLE_RESTORE) != 0;
	bumpers = (flags & FLAG_BUMPERS) != 0;
	s_boost = (flags & FLAG_S_BOOST) != 0;
	allow_workshop_vehicles = (flags & FLAG_ALLOW_WORKSHOP) != 0;
	boost_unlocked_from_start = (flags & FLAG_BOOST_UNLOCKED) != 0;
	leaderboard_eligible = (flags & FLAG_LEADERBOARD_ELIGIBLE) != 0;
	resumed_from_replay = (flags & FLAG_RESUMED_FROM_REPLAY) != 0;
	custom_content = (flags & FLAG_CUSTOM_CONTENT) != 0;
	leaderboard_ineligible_reason = reason;
	cpu_vehicle_content_ids = content_ids;
	return true;
}

Dictionary MxtRaceConfiguration::to_metadata_dictionary() const {
	Dictionary out;
	out["game_mode"] = game_mode;
	out["vehicle_restore"] = vehicle_restore;
	out["bumpers"] = bumpers;
	out["s_boost"] = s_boost;
	out["allow_workshop_vehicles"] = allow_workshop_vehicles;
	out["boost_unlocked_from_start"] = boost_unlocked_from_start;
	out["cpu_count"] = cpu_count;
	out["cpu_vehicle_content_ids"] = cpu_vehicle_content_ids;
	out["lap_count"] = lap_count;
	out["session_kind"] = session_kind == SESSION_TIME_ATTACK ? "time_attack" : (session_kind == SESSION_PRACTICE ? "practice" : "");
	out["time_attack_ruleset_revision"] = time_attack_ruleset_revision;
	out["leaderboard_eligible"] = leaderboard_eligible;
	out["leaderboard_ineligible_reason"] = leaderboard_ineligible_reason;
	out["practice_local_player_id"] = practice_local_player_id;
	out["resumed_from_replay"] = resumed_from_replay;
	out["custom_content"] = custom_content;
	return out;
}

bool MxtRaceConfiguration::load_metadata_dictionary(const Dictionary &value) {
	reset();
	const String kind = value.get("session_kind", "");
	session_kind = kind == "time_attack" ? SESSION_TIME_ATTACK : (kind == "practice" ? SESSION_PRACTICE : SESSION_STANDARD);
	set_game_mode(value.get("game_mode", 0));
	set_cpu_count(value.get("cpu_count", 0));
	set_lap_count(value.get("lap_count", 3));
	set_time_attack_ruleset_revision(value.get("time_attack_ruleset_revision", 0));
	practice_local_player_id = value.get("practice_local_player_id", -1);
	vehicle_restore = value.get("vehicle_restore", true);
	bumpers = value.get("bumpers", false);
	s_boost = value.get("s_boost", true);
	allow_workshop_vehicles = value.get("allow_workshop_vehicles", true);
	boost_unlocked_from_start = value.get("boost_unlocked_from_start", false);
	leaderboard_eligible = value.get("leaderboard_eligible", false);
	resumed_from_replay = value.get("resumed_from_replay", false);
	custom_content = value.get("custom_content", false);
	leaderboard_ineligible_reason = value.get("leaderboard_ineligible_reason", "");
	const Variant ids_value = value.get("cpu_vehicle_content_ids", PackedStringArray());
	if (ids_value.get_type() == Variant::PACKED_STRING_ARRAY) {
		cpu_vehicle_content_ids = ids_value;
	} else if (ids_value.get_type() == Variant::ARRAY) {
		const Array ids = ids_value;
		for (int64_t i = 0; i < ids.size() && i < MAX_CPU_VEHICLE_IDS; ++i) cpu_vehicle_content_ids.append(String(ids[i]));
	}
	return true;
}
