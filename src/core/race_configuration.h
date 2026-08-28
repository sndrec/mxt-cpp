#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace godot {

class MxtRaceConfiguration : public RefCounted {
	GDCLASS(MxtRaceConfiguration, RefCounted)

public:
	enum SessionKind : uint8_t {
		SESSION_STANDARD = 0,
		SESSION_TIME_ATTACK = 1,
		SESSION_PRACTICE = 2,
	};

private:
	SessionKind session_kind = SESSION_STANDARD;
	uint8_t game_mode = 0;
	uint16_t cpu_count = 0;
	uint16_t lap_count = 3;
	uint16_t time_attack_ruleset_revision = 0;
	int64_t practice_local_player_id = -1;
	bool vehicle_restore = true;
	bool bumpers = false;
	bool s_boost = true;
	bool allow_workshop_vehicles = true;
	bool boost_unlocked_from_start = false;
	bool leaderboard_eligible = false;
	bool resumed_from_replay = false;
	bool custom_content = false;
	String leaderboard_ineligible_reason;
	PackedStringArray cpu_vehicle_content_ids;
	String last_error;

protected:
	static void _bind_methods();

public:
	Ref<MxtRaceConfiguration> copy() const;
	void reset();

	int32_t get_session_kind() const { return static_cast<int32_t>(session_kind); }
	void set_session_kind(int32_t value);
	bool is_time_attack() const { return session_kind == SESSION_TIME_ATTACK; }
	bool is_practice() const { return session_kind == SESSION_PRACTICE; }
	int32_t get_game_mode() const { return game_mode; }
	void set_game_mode(int32_t value);
	int32_t get_cpu_count() const { return cpu_count; }
	void set_cpu_count(int32_t value);
	int32_t get_lap_count() const { return lap_count; }
	void set_lap_count(int32_t value);
	int32_t get_time_attack_ruleset_revision() const { return time_attack_ruleset_revision; }
	void set_time_attack_ruleset_revision(int32_t value);
	int64_t get_practice_local_player_id() const { return practice_local_player_id; }
	void set_practice_local_player_id(int64_t value) { practice_local_player_id = value; }
	bool get_vehicle_restore() const { return vehicle_restore; }
	void set_vehicle_restore(bool value) { vehicle_restore = value; }
	bool get_bumpers() const { return bumpers; }
	void set_bumpers(bool value) { bumpers = value; }
	bool get_s_boost() const { return s_boost; }
	void set_s_boost(bool value) { s_boost = value; }
	bool get_allow_workshop_vehicles() const { return allow_workshop_vehicles; }
	void set_allow_workshop_vehicles(bool value) { allow_workshop_vehicles = value; }
	bool get_boost_unlocked_from_start() const { return boost_unlocked_from_start; }
	void set_boost_unlocked_from_start(bool value) { boost_unlocked_from_start = value; }
	bool get_leaderboard_eligible() const { return leaderboard_eligible; }
	void set_leaderboard_eligible(bool value) { leaderboard_eligible = value; }
	bool get_resumed_from_replay() const { return resumed_from_replay; }
	void set_resumed_from_replay(bool value) { resumed_from_replay = value; }
	bool get_custom_content() const { return custom_content; }
	void set_custom_content(bool value) { custom_content = value; }
	String get_leaderboard_ineligible_reason() const { return leaderboard_ineligible_reason; }
	void set_leaderboard_ineligible_reason(const String &value) { leaderboard_ineligible_reason = value; }
	PackedStringArray get_cpu_vehicle_content_ids() const { return cpu_vehicle_content_ids; }
	void set_cpu_vehicle_content_ids(const PackedStringArray &value) { cpu_vehicle_content_ids = value; }
	String get_last_error() const { return last_error; }

	PackedByteArray encode_wire() const;
	bool decode_wire(const PackedByteArray &bytes);
	Dictionary to_metadata_dictionary() const;
	bool load_metadata_dictionary(const Dictionary &value);
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MxtRaceConfiguration::SessionKind)
