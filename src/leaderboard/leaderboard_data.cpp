#include "leaderboard/leaderboard_data.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

using namespace godot;

namespace {
static constexpr int32_t MAX_QUERY_ENTRIES = 256;

static int64_t integer_value(const Variant &value, int64_t fallback = 0) {
	if (value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT) return value;
	if (value.get_type() == Variant::STRING) {
		const String text = value;
		if (text.is_valid_int()) return text.to_int();
	}
	return fallback;
}
} // namespace

void MxtLeaderboardEntry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_dictionary", "value"), &MxtLeaderboardEntry::load_dictionary);
	ClassDB::bind_method(D_METHOD("trusted_details"), &MxtLeaderboardEntry::trusted_details);
	ClassDB::bind_method(D_METHOD("set_presentation", "rank", "player", "vehicle", "version", "replay_available", "compatibility_warning"), &MxtLeaderboardEntry::set_presentation);
#define BIND_READONLY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtLeaderboardEntry::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "", "get_" #name)
	BIND_READONLY(Variant::INT, rank);
	BIND_READONLY(Variant::INT, steam_id);
	BIND_READONLY(Variant::STRING, persona_name);
	BIND_READONLY(Variant::INT, score_milliseconds);
	BIND_READONLY(Variant::STRING, run_id);
	BIND_READONLY(Variant::STRING, replay_sha256);
	BIND_READONLY(Variant::STRING, track_content_id);
	BIND_READONLY(Variant::STRING, track_gameplay_digest);
	BIND_READONLY(Variant::STRING, vehicle_content_id);
	BIND_READONLY(Variant::STRING, vehicle_gameplay_digest);
	BIND_READONLY(Variant::INT, machine_setting_percent);
	BIND_READONLY(Variant::INT, ruleset_revision);
	BIND_READONLY(Variant::INT, replay_schema_version);
	BIND_READONLY(Variant::INT, game_version_major);
	BIND_READONLY(Variant::INT, game_version_compatibility);
	BIND_READONLY(Variant::INT, game_version_patch);
	BIND_READONLY(Variant::STRING, provenance);
	BIND_READONLY(Variant::STRING, source);
	BIND_READONLY(Variant::STRING, local_path);
	BIND_READONLY(Variant::DICTIONARY, local_validation);
	BIND_READONLY(Variant::STRING, display_rank);
	BIND_READONLY(Variant::STRING, display_player);
	BIND_READONLY(Variant::STRING, display_vehicle);
	BIND_READONLY(Variant::STRING, display_version);
	BIND_READONLY(Variant::BOOL, replay_available);
	BIND_READONLY(Variant::BOOL, compatibility_warning);
#undef BIND_READONLY
}

bool MxtLeaderboardEntry::load_dictionary(const Dictionary &value) {
	const Variant trusted_value = value.get("_trusted_details", Dictionary());
	const Dictionary trusted = trusted_value.get_type() == Variant::DICTIONARY ? static_cast<Dictionary>(trusted_value) : Dictionary();
	rank = static_cast<int32_t>(integer_value(value.get("rank", 0)));
	steam_id = integer_value(value.get("steam_id", 0));
	persona_name = value.get("persona_name", "");
	score_milliseconds = integer_value(value.get("score_milliseconds", 0));
	run_id = value.get("run_id", "");
	replay_sha256 = value.get("replay_sha256", trusted.get("replay_sha256", ""));
	track_content_id = value.get("track_content_id", trusted.get("track_content_id", ""));
	track_gameplay_digest = value.get("track_gameplay_digest", trusted.get("track_gameplay_digest", ""));
	vehicle_content_id = value.get("vehicle_content_id", trusted.get("vehicle_content_id", ""));
	vehicle_gameplay_digest = value.get("vehicle_gameplay_digest", trusted.get("vehicle_gameplay_digest", ""));
	machine_setting_percent = static_cast<int32_t>(integer_value(value.get("machine_setting_percent", trusted.get("machine_setting_percent", -1)), -1));
	ruleset_revision = static_cast<int32_t>(integer_value(value.get("ruleset_revision", trusted.get("ruleset_revision", -1)), -1));
	replay_schema_version = static_cast<int32_t>(integer_value(value.get("replay_schema_version", trusted.get("replay_schema_version", -1)), -1));
	const Variant version_value = value.get("game_version", trusted.get("game_version", Dictionary()));
	if (version_value.get_type() == Variant::DICTIONARY) {
		const Dictionary version = version_value;
		game_version_major = static_cast<int32_t>(integer_value(version.get("major", -1), -1));
		game_version_compatibility = static_cast<int32_t>(integer_value(version.get("compatibility", -1), -1));
		game_version_patch = static_cast<int32_t>(integer_value(version.get("patch", -1), -1));
	}
	provenance = value.get("provenance", "");
	source = value.get("_source", "leaderboard");
	local_path = value.get("_local_path", "");
	const Variant validation = value.get("_local_validation", Dictionary());
	local_validation = validation.get_type() == Variant::DICTIONARY ? static_cast<Dictionary>(validation) : Dictionary();
	display_rank = value.get("_display_rank", "");
	display_player = value.get("_display_player", "");
	display_vehicle = value.get("_display_vehicle", "");
	display_version = value.get("_display_version", "");
	replay_available = value.get("_replay_available", false);
	compatibility_warning = value.get("_compatibility_warning", false);
	return score_milliseconds >= 0;
}

Dictionary MxtLeaderboardEntry::trusted_details() const {
	Dictionary version;
	version["major"] = game_version_major;
	version["compatibility"] = game_version_compatibility;
	version["patch"] = game_version_patch;
	Dictionary out;
	out["replay_sha256"] = replay_sha256;
	out["track_content_id"] = track_content_id;
	out["track_gameplay_digest"] = track_gameplay_digest;
	out["vehicle_content_id"] = vehicle_content_id;
	out["vehicle_gameplay_digest"] = vehicle_gameplay_digest;
	out["machine_setting_percent"] = machine_setting_percent;
	out["ruleset_revision"] = ruleset_revision;
	out["replay_schema_version"] = replay_schema_version;
	out["game_version"] = version;
	return out;
}

void MxtLeaderboardEntry::set_presentation(const String &in_rank, const String &in_player,
		const String &in_vehicle, const String &in_version, bool in_replay_available,
		bool in_compatibility_warning) {
	display_rank = in_rank;
	display_player = in_player;
	display_vehicle = in_vehicle;
	display_version = in_version;
	replay_available = in_replay_available;
	compatibility_warning = in_compatibility_warning;
}

void MxtLeaderboardQueryResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_dictionary", "value", "vehicle_digest", "cursor"), &MxtLeaderboardQueryResult::load_dictionary);
	ClassDB::bind_method(D_METHOD("set_failure", "message", "vehicle_digest", "cursor"), &MxtLeaderboardQueryResult::set_failure);
	ClassDB::bind_method(D_METHOD("is_ok"), &MxtLeaderboardQueryResult::is_ok);
	ClassDB::bind_method(D_METHOD("get_entry_count"), &MxtLeaderboardQueryResult::get_entry_count);
	ClassDB::bind_method(D_METHOD("get_entry", "index"), &MxtLeaderboardQueryResult::get_entry);
#define BIND_READONLY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtLeaderboardQueryResult::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "", "get_" #name)
	BIND_READONLY(Variant::STRING, message);
	BIND_READONLY(Variant::STRING, requested_vehicle_gameplay_digest);
	BIND_READONLY(Variant::STRING, requested_cursor);
	BIND_READONLY(Variant::STRING, next_cursor);
#undef BIND_READONLY
}

bool MxtLeaderboardQueryResult::load_dictionary(const Dictionary &value, const String &vehicle_digest, const String &cursor) {
	entries.clear();
	requested_vehicle_gameplay_digest = vehicle_digest;
	requested_cursor = cursor;
	ok = value.get("ok", false);
	message = value.get("message", "");
	next_cursor = value.get("next_cursor", "");
	const Variant entries_value = value.get("entries", Array());
	if (!ok || entries_value.get_type() != Variant::ARRAY) return ok;
	const Array source_entries = entries_value;
	for (int32_t i = 0; i < source_entries.size() && i < MAX_QUERY_ENTRIES; ++i) {
		if (source_entries[i].get_type() != Variant::DICTIONARY) continue;
		Ref<MxtLeaderboardEntry> entry;
		entry.instantiate();
		if (entry->load_dictionary(source_entries[i])) entries.push_back(entry);
	}
	return true;
}

void MxtLeaderboardQueryResult::set_failure(const String &in_message, const String &vehicle_digest, const String &cursor) {
	ok = false;
	message = in_message;
	requested_vehicle_gameplay_digest = vehicle_digest;
	requested_cursor = cursor;
	next_cursor = String();
	entries.clear();
}

Ref<MxtLeaderboardEntry> MxtLeaderboardQueryResult::get_entry(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index] : Ref<MxtLeaderboardEntry>();
}
