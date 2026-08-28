#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <vector>

namespace godot {

class MxtLeaderboardEntry : public RefCounted {
	GDCLASS(MxtLeaderboardEntry, RefCounted)

	int32_t rank = 0;
	int64_t steam_id = 0;
	String persona_name;
	int64_t score_milliseconds = 0;
	String run_id;
	String replay_sha256;
	String track_content_id;
	String track_gameplay_digest;
	String vehicle_content_id;
	String vehicle_gameplay_digest;
	int32_t machine_setting_percent = -1;
	int32_t ruleset_revision = -1;
	int32_t replay_schema_version = -1;
	int32_t game_version_major = -1;
	int32_t game_version_compatibility = -1;
	int32_t game_version_patch = -1;
	String provenance;
	String source = "leaderboard";
	String local_path;
	Dictionary local_validation;
	String display_rank;
	String display_player;
	String display_vehicle;
	String display_version;
	bool replay_available = false;
	bool compatibility_warning = false;

protected:
	static void _bind_methods();

public:
	bool load_dictionary(const Dictionary &value);
	Dictionary trusted_details() const;
	void set_presentation(const String &in_rank, const String &in_player,
			const String &in_vehicle, const String &in_version,
			bool in_replay_available, bool in_compatibility_warning);

	int32_t get_rank() const { return rank; }
	int64_t get_steam_id() const { return steam_id; }
	String get_persona_name() const { return persona_name; }
	int64_t get_score_milliseconds() const { return score_milliseconds; }
	String get_run_id() const { return run_id; }
	String get_replay_sha256() const { return replay_sha256; }
	String get_track_content_id() const { return track_content_id; }
	String get_track_gameplay_digest() const { return track_gameplay_digest; }
	String get_vehicle_content_id() const { return vehicle_content_id; }
	String get_vehicle_gameplay_digest() const { return vehicle_gameplay_digest; }
	int32_t get_machine_setting_percent() const { return machine_setting_percent; }
	int32_t get_ruleset_revision() const { return ruleset_revision; }
	int32_t get_replay_schema_version() const { return replay_schema_version; }
	int32_t get_game_version_major() const { return game_version_major; }
	int32_t get_game_version_compatibility() const { return game_version_compatibility; }
	int32_t get_game_version_patch() const { return game_version_patch; }
	String get_provenance() const { return provenance; }
	String get_source() const { return source; }
	String get_local_path() const { return local_path; }
	Dictionary get_local_validation() const { return local_validation.duplicate(true); }
	String get_display_rank() const { return display_rank; }
	String get_display_player() const { return display_player; }
	String get_display_vehicle() const { return display_vehicle; }
	String get_display_version() const { return display_version; }
	bool get_replay_available() const { return replay_available; }
	bool get_compatibility_warning() const { return compatibility_warning; }
};

class MxtLeaderboardQueryResult : public RefCounted {
	GDCLASS(MxtLeaderboardQueryResult, RefCounted)

	bool ok = false;
	String message;
	String requested_vehicle_gameplay_digest;
	String requested_cursor;
	String next_cursor;
	std::vector<Ref<MxtLeaderboardEntry>> entries;

protected:
	static void _bind_methods();

public:
	bool load_dictionary(const Dictionary &value, const String &vehicle_digest, const String &cursor);
	void set_failure(const String &in_message, const String &vehicle_digest, const String &cursor);
	bool is_ok() const { return ok; }
	String get_message() const { return message; }
	String get_requested_vehicle_gameplay_digest() const { return requested_vehicle_gameplay_digest; }
	String get_requested_cursor() const { return requested_cursor; }
	String get_next_cursor() const { return next_cursor; }
	int32_t get_entry_count() const { return static_cast<int32_t>(entries.size()); }
	Ref<MxtLeaderboardEntry> get_entry(int32_t index) const;
};

} // namespace godot
