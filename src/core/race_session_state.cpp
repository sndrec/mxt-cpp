#include "core/race_session_state.h"

#include "core/bounded_wire.h"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

namespace {
constexpr uint8_t WIRE_MAGIC[4] = {'M', 'X', 'R', 'S'};
constexpr uint8_t WIRE_VERSION = 1;
constexpr uint16_t MAX_RACERS = 1024;

void write_ids(mxt_wire::Writer &writer, const PackedInt64Array &ids) {
	writer.u16(static_cast<uint16_t>(ids.size()));
	for (int64_t id : ids) writer.i64(id);
}

bool read_ids(mxt_wire::Reader &reader, PackedInt64Array &ids) {
	uint16_t count = 0;
	if (!reader.u16(count) || count > MAX_RACERS) return false;
	ids.resize(count);
	for (uint16_t i = 0; i < count; ++i) if (!reader.i64(ids.ptrw()[i]) || ids[i] < 0) return false;
	return true;
}

PackedInt64Array ids_from_variant(const Variant &value) {
	if (value.get_type() == Variant::PACKED_INT64_ARRAY) return value;
	PackedInt64Array out;
	if (value.get_type() != Variant::ARRAY) return out;
	const Array source = value;
	const int64_t count = std::min<int64_t>(source.size(), MAX_RACERS);
	out.resize(count);
	for (int64_t i = 0; i < count; ++i) out.ptrw()[i] = static_cast<int64_t>(source[i]);
	return out;
}
} // namespace

void MxtRaceSessionState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("copy"), &MxtRaceSessionState::copy);
	ClassDB::bind_method(D_METHOD("clear"), &MxtRaceSessionState::clear);
	ClassDB::bind_method(D_METHOD("is_empty"), &MxtRaceSessionState::is_empty);
	ClassDB::bind_method(D_METHOD("get_netplay_phase"), &MxtRaceSessionState::get_netplay_phase);
	ClassDB::bind_method(D_METHOD("set_netplay_phase", "value"), &MxtRaceSessionState::set_netplay_phase);
	ClassDB::bind_method(D_METHOD("get_spawn_seed"), &MxtRaceSessionState::get_spawn_seed);
	ClassDB::bind_method(D_METHOD("set_spawn_seed", "value"), &MxtRaceSessionState::set_spawn_seed);
	ClassDB::bind_method(D_METHOD("get_human_ids"), &MxtRaceSessionState::get_human_ids);
	ClassDB::bind_method(D_METHOD("set_human_ids", "value"), &MxtRaceSessionState::set_human_ids);
	ClassDB::bind_method(D_METHOD("get_cpu_ids"), &MxtRaceSessionState::get_cpu_ids);
	ClassDB::bind_method(D_METHOD("set_cpu_ids", "value"), &MxtRaceSessionState::set_cpu_ids);
	ClassDB::bind_method(D_METHOD("get_spectator_ids"), &MxtRaceSessionState::get_spectator_ids);
	ClassDB::bind_method(D_METHOD("set_spectator_ids", "value"), &MxtRaceSessionState::set_spectator_ids);
	ClassDB::bind_method(D_METHOD("get_grand_prix_current_track"), &MxtRaceSessionState::get_grand_prix_current_track);
	ClassDB::bind_method(D_METHOD("set_grand_prix_current_track", "value"), &MxtRaceSessionState::set_grand_prix_current_track);
	ClassDB::bind_method(D_METHOD("get_grand_prix_recorded_track"), &MxtRaceSessionState::get_grand_prix_recorded_track);
	ClassDB::bind_method(D_METHOD("set_grand_prix_recorded_track", "value"), &MxtRaceSessionState::set_grand_prix_recorded_track);
	ClassDB::bind_method(D_METHOD("get_grand_prix_eliminated_ids"), &MxtRaceSessionState::get_grand_prix_eliminated_ids);
	ClassDB::bind_method(D_METHOD("set_grand_prix_eliminated_ids", "value"), &MxtRaceSessionState::set_grand_prix_eliminated_ids);
	ClassDB::bind_method(D_METHOD("get_grand_prix_points", "player_id"), &MxtRaceSessionState::get_grand_prix_points);
	ClassDB::bind_method(D_METHOD("get_grand_prix_point_player_ids"), &MxtRaceSessionState::get_grand_prix_point_player_ids);
	ClassDB::bind_method(D_METHOD("set_grand_prix_points", "player_id", "value"), &MxtRaceSessionState::set_grand_prix_points);
	ClassDB::bind_method(D_METHOD("get_grand_prix_ko_energy_bonus", "player_id"), &MxtRaceSessionState::get_grand_prix_ko_energy_bonus);
	ClassDB::bind_method(D_METHOD("set_grand_prix_ko_energy_bonus", "player_id", "value"), &MxtRaceSessionState::set_grand_prix_ko_energy_bonus);
	ClassDB::bind_method(D_METHOD("clear_grand_prix_points"), &MxtRaceSessionState::clear_grand_prix_points);
	ClassDB::bind_method(D_METHOD("clear_grand_prix_ko_energy_bonuses"), &MxtRaceSessionState::clear_grand_prix_ko_energy_bonuses);
	ClassDB::bind_method(D_METHOD("to_metadata_dictionary"), &MxtRaceSessionState::to_metadata_dictionary);
	ClassDB::bind_method(D_METHOD("load_metadata_dictionary", "value"), &MxtRaceSessionState::load_metadata_dictionary);
	ClassDB::bind_method(D_METHOD("encode_wire"), &MxtRaceSessionState::encode_wire);
	ClassDB::bind_method(D_METHOD("decode_wire", "bytes"), &MxtRaceSessionState::decode_wire);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtRaceSessionState::get_last_error);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "netplay_phase"), "set_netplay_phase", "get_netplay_phase");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spawn_seed"), "set_spawn_seed", "get_spawn_seed");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "human_ids"), "set_human_ids", "get_human_ids");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "cpu_ids"), "set_cpu_ids", "get_cpu_ids");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "spectator_ids"), "set_spectator_ids", "get_spectator_ids");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grand_prix_current_track"), "set_grand_prix_current_track", "get_grand_prix_current_track");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grand_prix_recorded_track"), "set_grand_prix_recorded_track", "get_grand_prix_recorded_track");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "grand_prix_eliminated_ids"), "set_grand_prix_eliminated_ids", "get_grand_prix_eliminated_ids");
}

Ref<MxtRaceSessionState> MxtRaceSessionState::copy() const {
	Ref<MxtRaceSessionState> out;
	out.instantiate();
	out->netplay_phase = netplay_phase;
	out->spawn_seed = spawn_seed;
	out->human_ids = human_ids;
	out->cpu_ids = cpu_ids;
	out->spectator_ids = spectator_ids;
	out->grand_prix_current_track = grand_prix_current_track;
	out->grand_prix_recorded_track = grand_prix_recorded_track;
	out->grand_prix_eliminated_ids = grand_prix_eliminated_ids;
	out->grand_prix_points = grand_prix_points;
	out->grand_prix_ko_energy_bonuses = grand_prix_ko_energy_bonuses;
	return out;
}

void MxtRaceSessionState::clear() {
	netplay_phase = 0;
	spawn_seed = 0;
	human_ids.clear();
	cpu_ids.clear();
	spectator_ids.clear();
	grand_prix_current_track = 0;
	grand_prix_recorded_track = -1;
	grand_prix_eliminated_ids.clear();
	grand_prix_points.clear();
	grand_prix_ko_energy_bonuses.clear();
	last_error = String();
}

bool MxtRaceSessionState::is_empty() const {
	return spawn_seed == 0 && human_ids.is_empty() && cpu_ids.is_empty() && spectator_ids.is_empty() &&
		grand_prix_eliminated_ids.is_empty() && grand_prix_points.empty() && grand_prix_ko_energy_bonuses.empty() &&
		grand_prix_current_track == 0 && grand_prix_recorded_track < 0;
}

int32_t MxtRaceSessionState::find_int(const std::vector<IntValue> &values, int64_t player_id) {
	for (size_t i = 0; i < values.size(); ++i) if (values[i].player_id == player_id) return static_cast<int32_t>(i);
	return -1;
}

int32_t MxtRaceSessionState::find_float(const std::vector<FloatValue> &values, int64_t player_id) {
	for (size_t i = 0; i < values.size(); ++i) if (values[i].player_id == player_id) return static_cast<int32_t>(i);
	return -1;
}

int32_t MxtRaceSessionState::get_grand_prix_points(int64_t player_id) const {
	const int32_t index = find_int(grand_prix_points, player_id);
	return index >= 0 ? grand_prix_points[index].value : 0;
}

PackedInt64Array MxtRaceSessionState::get_grand_prix_point_player_ids() const {
	PackedInt64Array out;
	out.resize(static_cast<int64_t>(grand_prix_points.size()));
	for (size_t i = 0; i < grand_prix_points.size(); ++i) out.ptrw()[i] = grand_prix_points[i].player_id;
	return out;
}

void MxtRaceSessionState::set_grand_prix_points(int64_t player_id, int32_t value) {
	const int32_t index = find_int(grand_prix_points, player_id);
	if (index >= 0) grand_prix_points[index].value = value;
	else if (grand_prix_points.size() < MAX_RACERS) grand_prix_points.push_back({player_id, value});
}

float MxtRaceSessionState::get_grand_prix_ko_energy_bonus(int64_t player_id) const {
	const int32_t index = find_float(grand_prix_ko_energy_bonuses, player_id);
	return index >= 0 ? grand_prix_ko_energy_bonuses[index].value : 0.0f;
}

void MxtRaceSessionState::set_grand_prix_ko_energy_bonus(int64_t player_id, float value) {
	if (!std::isfinite(value)) return;
	const int32_t index = find_float(grand_prix_ko_energy_bonuses, player_id);
	if (index >= 0) grand_prix_ko_energy_bonuses[index].value = value;
	else if (grand_prix_ko_energy_bonuses.size() < MAX_RACERS) grand_prix_ko_energy_bonuses.push_back({player_id, value});
}

Dictionary MxtRaceSessionState::to_metadata_dictionary() const {
	Dictionary out;
	out["race_netplay_phase"] = netplay_phase;
	out["spawn_seed"] = spawn_seed;
	out["race_human_ids"] = human_ids;
	out["race_cpu_ids"] = cpu_ids;
	out["race_spectator_ids"] = spectator_ids;
	out["grand_prix_current_track"] = grand_prix_current_track;
	out["grand_prix_recorded_track"] = grand_prix_recorded_track;
	out["grand_prix_eliminated_ids"] = grand_prix_eliminated_ids;
	Dictionary points;
	for (const IntValue &entry : grand_prix_points) points[entry.player_id] = entry.value;
	out["grand_prix_points"] = points;
	Dictionary bonuses;
	for (const FloatValue &entry : grand_prix_ko_energy_bonuses) bonuses[entry.player_id] = entry.value;
	out["grand_prix_ko_energy_bonuses"] = bonuses;
	return out;
}

bool MxtRaceSessionState::load_metadata_dictionary(const Dictionary &value) {
	clear();
	netplay_phase = static_cast<int32_t>(value.get("race_netplay_phase", 0)) & 1;
	spawn_seed = value.get("spawn_seed", 0);
	human_ids = ids_from_variant(value.get("race_human_ids", Variant()));
	cpu_ids = ids_from_variant(value.get("race_cpu_ids", Variant()));
	spectator_ids = ids_from_variant(value.get("race_spectator_ids", Variant()));
	grand_prix_current_track = value.get("grand_prix_current_track", 0);
	grand_prix_recorded_track = value.get("grand_prix_recorded_track", -1);
	grand_prix_eliminated_ids = ids_from_variant(value.get("grand_prix_eliminated_ids", Variant()));
	const Dictionary points = value.get("grand_prix_points", Dictionary());
	const Array point_keys = points.keys();
	for (int64_t i = 0; i < point_keys.size(); ++i) set_grand_prix_points(static_cast<int64_t>(point_keys[i]), points[point_keys[i]]);
	const Dictionary bonuses = value.get("grand_prix_ko_energy_bonuses", Dictionary());
	const Array bonus_keys = bonuses.keys();
	for (int64_t i = 0; i < bonus_keys.size(); ++i) set_grand_prix_ko_energy_bonus(static_cast<int64_t>(bonus_keys[i]), bonuses[bonus_keys[i]]);
	return true;
}

PackedByteArray MxtRaceSessionState::encode_wire() const {
	mxt_wire::Writer writer;
	writer.raw(WIRE_MAGIC, sizeof(WIRE_MAGIC));
	writer.u8(WIRE_VERSION);
	writer.u8(static_cast<uint8_t>(netplay_phase));
	writer.i64(spawn_seed);
	write_ids(writer, human_ids);
	write_ids(writer, cpu_ids);
	write_ids(writer, spectator_ids);
	writer.i32(grand_prix_current_track);
	writer.i32(grand_prix_recorded_track);
	write_ids(writer, grand_prix_eliminated_ids);
	writer.u16(static_cast<uint16_t>(grand_prix_points.size()));
	for (const IntValue &entry : grand_prix_points) { writer.i64(entry.player_id); writer.i32(entry.value); }
	writer.u16(static_cast<uint16_t>(grand_prix_ko_energy_bonuses.size()));
	for (const FloatValue &entry : grand_prix_ko_energy_bonuses) { writer.i64(entry.player_id); writer.f32(entry.value); }
	return writer.packed();
}

bool MxtRaceSessionState::decode_wire(const PackedByteArray &bytes) {
	last_error = String();
	mxt_wire::Reader reader(bytes);
	uint8_t magic[sizeof(WIRE_MAGIC)];
	uint8_t version = 0;
	uint8_t phase = 0;
	Ref<MxtRaceSessionState> decoded;
	decoded.instantiate();
	if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, WIRE_MAGIC, sizeof(magic)) != 0 ||
		!reader.u8(version) || version != WIRE_VERSION || !reader.u8(phase) || phase > 1 || !reader.i64(decoded->spawn_seed) ||
		!read_ids(reader, decoded->human_ids) || !read_ids(reader, decoded->cpu_ids) || !read_ids(reader, decoded->spectator_ids) ||
		!reader.i32(decoded->grand_prix_current_track) || !reader.i32(decoded->grand_prix_recorded_track) ||
		!read_ids(reader, decoded->grand_prix_eliminated_ids)) {
		last_error = "Malformed race session state packet.";
		return false;
	}
	decoded->netplay_phase = phase;
	uint16_t count = 0;
	if (!reader.u16(count) || count > MAX_RACERS) { last_error = "Malformed race session points."; return false; }
	decoded->grand_prix_points.resize(count);
	for (IntValue &entry : decoded->grand_prix_points) if (!reader.i64(entry.player_id) || entry.player_id < 0 || !reader.i32(entry.value)) {
		last_error = "Malformed race session points entry."; return false;
	}
	if (!reader.u16(count) || count > MAX_RACERS) { last_error = "Malformed race session bonuses."; return false; }
	decoded->grand_prix_ko_energy_bonuses.resize(count);
	for (FloatValue &entry : decoded->grand_prix_ko_energy_bonuses) if (!reader.i64(entry.player_id) || entry.player_id < 0 || !reader.f32(entry.value) || !std::isfinite(entry.value)) {
		last_error = "Malformed race session bonus entry."; return false;
	}
	if (!reader.finished()) { last_error = "Race session state packet has trailing bytes."; return false; }
	netplay_phase = decoded->netplay_phase;
	spawn_seed = decoded->spawn_seed;
	human_ids = decoded->human_ids;
	cpu_ids = decoded->cpu_ids;
	spectator_ids = decoded->spectator_ids;
	grand_prix_current_track = decoded->grand_prix_current_track;
	grand_prix_recorded_track = decoded->grand_prix_recorded_track;
	grand_prix_eliminated_ids = decoded->grand_prix_eliminated_ids;
	grand_prix_points = decoded->grand_prix_points;
	grand_prix_ko_energy_bonuses = decoded->grand_prix_ko_energy_bonuses;
	return true;
}
