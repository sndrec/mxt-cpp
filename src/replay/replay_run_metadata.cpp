#include "replay/replay_run_metadata.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <algorithm>

using namespace godot;

namespace {

static constexpr int32_t MAX_RACERS = 1024;

static Variant id_value(const Dictionary &dictionary, int64_t player_id, const Variant &fallback = Variant()) {
	if (dictionary.has(player_id)) return dictionary[player_id];
	const String key = String::num_int64(player_id);
	return dictionary.has(key) ? dictionary[key] : fallback;
}

static bool dictionary_field(const Dictionary &dictionary, const char *key, Dictionary &out) {
	const Variant value = dictionary.get(key, Dictionary());
	if (value.get_type() != Variant::DICTIONARY) return false;
	out = value;
	return true;
}

} // namespace

void MxtReplayRunMetadata::_bind_methods() {
	ClassDB::bind_method(D_METHOD("copy"), &MxtReplayRunMetadata::copy);
	ClassDB::bind_method(D_METHOD("reset"), &MxtReplayRunMetadata::reset);
	ClassDB::bind_method(D_METHOD("set_results", "finish_times", "finish_placements", "eliminations"), &MxtReplayRunMetadata::set_results);
	ClassDB::bind_method(D_METHOD("set_race_metadata", "value"), &MxtReplayRunMetadata::set_race_metadata);
	ClassDB::bind_method(D_METHOD("to_dictionary"), &MxtReplayRunMetadata::to_dictionary);
	ClassDB::bind_method(D_METHOD("load_dictionary", "value"), &MxtReplayRunMetadata::load_dictionary);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtReplayRunMetadata::get_last_error);

#define BIND_PROPERTY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtReplayRunMetadata::get_##name); \
	ClassDB::bind_method(D_METHOD("set_" #name, "value"), &MxtReplayRunMetadata::set_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "set_" #name, "get_" #name)
	BIND_PROPERTY(Variant::INT, schema_version);
	BIND_PROPERTY(Variant::INT, game_version_major);
	BIND_PROPERTY(Variant::INT, game_version_compatibility);
	BIND_PROPERTY(Variant::INT, game_version_patch);
	BIND_PROPERTY(Variant::STRING, build);
	BIND_PROPERTY(Variant::STRING, engine_version);
	BIND_PROPERTY(Variant::INT, created_unix);
	BIND_PROPERTY(Variant::STRING, name);
	BIND_PROPERTY(Variant::STRING, mode);
	BIND_PROPERTY(Variant::STRING, source);
	BIND_PROPERTY(Variant::STRING, track_content_id);
	BIND_PROPERTY(Variant::STRING, track_gameplay_digest);
	BIND_PROPERTY(Variant::STRING, track_package_digest);
	BIND_PROPERTY(Variant::STRING, track_workshop_id);
	BIND_PROPERTY(Variant::STRING, track_name);
	BIND_PROPERTY(Variant::OBJECT, roster);
	BIND_PROPERTY(Variant::PACKED_INT32_ARRAY, start_grid_slots);
	BIND_PROPERTY(Variant::INT, spawn_seed);
	BIND_PROPERTY(Variant::OBJECT, race_configuration);
	BIND_PROPERTY(Variant::OBJECT, track_evidence);
	BIND_PROPERTY(Variant::BOOL, runtime_auto_accelerate);
	BIND_PROPERTY(Variant::BOOL, runtime_auto_bumpers);
	BIND_PROPERTY(Variant::BOOL, runtime_debug_bumper_smoke);
	BIND_PROPERTY(Variant::BOOL, runtime_debug_rail_trace);
	BIND_PROPERTY(Variant::STRING, saved_reason);
	BIND_PROPERTY(Variant::INT, duration_ticks);
	BIND_PROPERTY(Variant::STRING, benchmark_suite);
	BIND_PROPERTY(Variant::INT, benchmark_racer_count);
	BIND_PROPERTY(Variant::INT, benchmark_sample);
	BIND_PROPERTY(Variant::INT, benchmark_spawn_seed);
	BIND_PROPERTY(Variant::INT, benchmark_generation_ticks);
	BIND_PROPERTY(Variant::INT, benchmark_generation_usec);
#undef BIND_PROPERTY
}

MxtReplayRunMetadata::MxtReplayRunMetadata() = default;

Ref<MxtReplayRunMetadata> MxtReplayRunMetadata::copy() const {
	Ref<MxtReplayRunMetadata> out;
	out.instantiate();
	out->schema_version = schema_version;
	out->game_version_major = game_version_major;
	out->game_version_compatibility = game_version_compatibility;
	out->game_version_patch = game_version_patch;
	out->build = build;
	out->engine_version = engine_version;
	out->created_unix = created_unix;
	out->name = name;
	out->mode = mode;
	out->source = source;
	out->track_content_id = track_content_id;
	out->track_gameplay_digest = track_gameplay_digest;
	out->track_package_digest = track_package_digest;
	out->track_workshop_id = track_workshop_id;
	out->track_name = track_name;
	out->roster = roster.is_valid() ? roster->copy() : Ref<MxtRaceRoster>();
	out->start_grid_slots = start_grid_slots;
	out->spawn_seed = spawn_seed;
	out->race_configuration = race_configuration.is_valid() ? race_configuration->copy() : Ref<MxtRaceConfiguration>();
	out->track_evidence = track_evidence.is_valid() ? track_evidence->copy() : Ref<MxtTrackContentEvidence>();
	out->grand_prix_current_track = grand_prix_current_track;
	out->grand_prix_recorded_track = grand_prix_recorded_track;
	out->grand_prix_entries = grand_prix_entries;
	out->grand_prix_eliminated_ids = grand_prix_eliminated_ids;
	out->runtime_auto_accelerate = runtime_auto_accelerate;
	out->runtime_auto_bumpers = runtime_auto_bumpers;
	out->runtime_debug_bumper_smoke = runtime_debug_bumper_smoke;
	out->runtime_debug_rail_trace = runtime_debug_rail_trace;
	out->saved_reason = saved_reason;
	out->duration_ticks = duration_ticks;
	out->results = results;
	out->benchmark_suite = benchmark_suite;
	out->benchmark_racer_count = benchmark_racer_count;
	out->benchmark_sample = benchmark_sample;
	out->benchmark_spawn_seed = benchmark_spawn_seed;
	out->benchmark_generation_ticks = benchmark_generation_ticks;
	out->benchmark_generation_usec = benchmark_generation_usec;
	return out;
}

void MxtReplayRunMetadata::reset() {
	schema_version = 0;
	game_version_major = game_version_compatibility = game_version_patch = 0;
	build = engine_version = name = mode = source = String();
	track_content_id = track_gameplay_digest = track_package_digest = track_workshop_id = track_name = String();
	created_unix = spawn_seed = duration_ticks = 0;
	roster.instantiate();
	race_configuration.instantiate();
	track_evidence.instantiate();
	grand_prix_current_track = 0;
	grand_prix_recorded_track = -1;
	grand_prix_entries.clear();
	grand_prix_eliminated_ids.clear();
	start_grid_slots.clear();
	runtime_auto_accelerate = runtime_auto_bumpers = runtime_debug_bumper_smoke = runtime_debug_rail_trace = false;
	saved_reason = String();
	results.clear();
	benchmark_suite = String();
	benchmark_racer_count = benchmark_sample = 0;
	benchmark_spawn_seed = 0;
	benchmark_generation_ticks = 0;
	benchmark_generation_usec = 0;
	last_error = String();
}

void MxtReplayRunMetadata::set_roster(const Ref<MxtRaceRoster> &value) {
	roster = value.is_valid() ? value->copy() : Ref<MxtRaceRoster>();
}

void MxtReplayRunMetadata::set_race_configuration(const Ref<MxtRaceConfiguration> &value) {
	race_configuration = value.is_valid() ? value->copy() : Ref<MxtRaceConfiguration>();
}

void MxtReplayRunMetadata::set_track_evidence(const Ref<MxtTrackContentEvidence> &value) {
	track_evidence = value.is_valid() ? value->copy() : Ref<MxtTrackContentEvidence>();
}

MxtReplayRunMetadata::Result *MxtReplayRunMetadata::result_for_player(int64_t player_id) {
	for (Result &result : results) if (result.player_id == player_id) return &result;
	results.push_back({player_id, -1, -1, -1});
	return &results.back();
}

const MxtReplayRunMetadata::Result *MxtReplayRunMetadata::result_for_player(int64_t player_id) const {
	for (const Result &result : results) if (result.player_id == player_id) return &result;
	return nullptr;
}

void MxtReplayRunMetadata::set_results(const Dictionary &finish_times, const Dictionary &finish_placements, const Dictionary &eliminations) {
	results.clear();
	if (roster.is_valid()) {
		for (int32_t i = 0; i < roster->count(); ++i) result_for_player(roster->get_player_id(i));
	}
	const Array finish_ids = finish_times.keys();
	for (int64_t i = 0; i < finish_ids.size(); ++i) result_for_player(static_cast<int64_t>(finish_ids[i]));
	for (Result &result : results) {
		const Variant finish = id_value(finish_times, result.player_id);
		const Variant placement = id_value(finish_placements, result.player_id);
		const Variant elimination = id_value(eliminations, result.player_id);
		if (finish.get_type() != Variant::NIL) result.finish_tick = finish;
		if (placement.get_type() != Variant::NIL) result.placement = placement;
		if (elimination.get_type() != Variant::NIL) result.elimination_tick = elimination;
	}
}

void MxtReplayRunMetadata::set_race_metadata(const Dictionary &value) {
	if (race_configuration.is_null()) race_configuration.instantiate();
	if (track_evidence.is_null()) track_evidence.instantiate();
	race_configuration->load_metadata_dictionary(value);
	track_evidence->load_metadata_dictionary(value);
	grand_prix_current_track = value.get("grand_prix_current_track", 0);
	grand_prix_recorded_track = value.get("grand_prix_recorded_track", -1);
	grand_prix_entries.clear();
	grand_prix_eliminated_ids.clear();
	const Dictionary points = value.get("grand_prix_points", Dictionary());
	const Dictionary bonuses = value.get("grand_prix_ko_energy_bonuses", Dictionary());
	auto entry_for_id = [this](int64_t player_id) -> GrandPrixEntry & {
		for (GrandPrixEntry &entry : grand_prix_entries) if (entry.player_id == player_id) return entry;
		grand_prix_entries.push_back({player_id, 0, 0.0f});
		return grand_prix_entries.back();
	};
	const Array point_ids = points.keys();
	for (int64_t i = 0; i < point_ids.size(); ++i) {
		const int64_t player_id = point_ids[i];
		entry_for_id(player_id).points = points[point_ids[i]];
	}
	const Array bonus_ids = bonuses.keys();
	for (int64_t i = 0; i < bonus_ids.size(); ++i) {
		const int64_t player_id = bonus_ids[i];
		entry_for_id(player_id).ko_energy_bonus = bonuses[bonus_ids[i]];
	}
	const Variant eliminated_value = value.get("grand_prix_eliminated_ids", Array());
	if (eliminated_value.get_type() == Variant::ARRAY) {
		const Array eliminated = eliminated_value;
		for (int64_t i = 0; i < eliminated.size() && i < MAX_RACERS; ++i) grand_prix_eliminated_ids.push_back(eliminated[i]);
	}
}

Dictionary MxtReplayRunMetadata::to_dictionary() const {
	Dictionary out;
	out["schema_version"] = schema_version;
	out["build"] = build;
	Dictionary game_version;
	game_version["major"] = game_version_major;
	game_version["compatibility"] = game_version_compatibility;
	game_version["patch"] = game_version_patch;
	out["game_version"] = game_version;
	out["engine_version"] = engine_version;
	out["created_unix"] = created_unix;
	out["name"] = name;
	out["mode"] = mode;
	out["source"] = source;
	out["track_content_id"] = track_content_id;
	out["track_gameplay_digest"] = track_gameplay_digest;
	out["track_package_digest"] = track_package_digest;
	out["track_workshop_id"] = track_workshop_id;
	out["track_name"] = track_name;
	Array settings;
	Array racer_ids;
	Array cpu_flags;
	Array players;
	if (roster.is_valid()) {
		settings.resize(roster->count());
		racer_ids.resize(roster->count());
		cpu_flags.resize(roster->count());
		players.resize(roster->count());
		for (int32_t i = 0; i < roster->count(); ++i) {
			const Dictionary player_settings = roster->get_settings_dictionary(i);
			const int64_t player_id = roster->get_player_id(i);
			settings[i] = player_settings;
			racer_ids[i] = player_id;
			cpu_flags[i] = roster->is_cpu(i);
			Dictionary player;
			player["id"] = player_id;
			player["username"] = player_settings.get("username", "Player");
			player["cpu"] = roster->is_cpu(i);
			player["vehicle_content_id"] = player_settings.get("vehicle_content_id", "");
			player["vehicle_gameplay_digest"] = player_settings.get("vehicle_gameplay_digest", "");
			player["sticker_1"] = player_settings.get("sticker_1", 0);
			player["sticker_2"] = player_settings.get("sticker_2", 1);
			player["sticker_3"] = player_settings.get("sticker_3", 2);
			player["sticker_4"] = player_settings.get("sticker_4", 3);
			player["car_livery"] = player_settings.get("car_livery", Dictionary());
			player["settings"] = player_settings;
			players[i] = player;
		}
	}
	out["settings"] = settings;
	out["racer_ids"] = racer_ids;
	out["cpu_flags"] = cpu_flags;
	Array grid;
	grid.resize(start_grid_slots.size());
	for (int64_t i = 0; i < start_grid_slots.size(); ++i) grid[i] = start_grid_slots[i];
	out["start_grid_slots"] = grid;
	out["players"] = players;
	out["spawn_seed"] = spawn_seed;
	Dictionary race_options = race_configuration.is_valid() ? race_configuration->to_metadata_dictionary() : Dictionary();
	if (track_evidence.is_valid()) race_options.merge(track_evidence->to_metadata_dictionary(), true);
	race_options["grand_prix_current_track"] = grand_prix_current_track;
	race_options["grand_prix_recorded_track"] = grand_prix_recorded_track;
	Dictionary points;
	Dictionary bonuses;
	for (const GrandPrixEntry &entry : grand_prix_entries) {
		points[entry.player_id] = entry.points;
		if (entry.ko_energy_bonus != 0.0f) bonuses[entry.player_id] = entry.ko_energy_bonus;
	}
	race_options["grand_prix_points"] = points;
	race_options["grand_prix_ko_energy_bonuses"] = bonuses;
	Array eliminated;
	eliminated.resize(grand_prix_eliminated_ids.size());
	for (size_t i = 0; i < grand_prix_eliminated_ids.size(); ++i) eliminated[i] = grand_prix_eliminated_ids[i];
	race_options["grand_prix_eliminated_ids"] = eliminated;
	out["race_options"] = race_options;
	Dictionary runtime_flags;
	runtime_flags["auto_accelerate"] = runtime_auto_accelerate;
	runtime_flags["auto_bumpers"] = runtime_auto_bumpers;
	runtime_flags["debug_bumper_smoke"] = runtime_debug_bumper_smoke;
	runtime_flags["debug_rail_trace"] = runtime_debug_rail_trace;
	out["runtime_flags"] = runtime_flags;
	if (!saved_reason.is_empty()) out["saved_reason"] = saved_reason;
	if (duration_ticks > 0) out["duration_ticks"] = duration_ticks;
	Dictionary finish_times;
	Dictionary finish_placements;
	Dictionary eliminations;
	for (const Result &result : results) {
		if (result.finish_tick >= 0) finish_times[result.player_id] = result.finish_tick;
		if (result.placement >= 0) finish_placements[result.player_id] = result.placement;
		if (result.elimination_tick >= 0) eliminations[result.player_id] = result.elimination_tick;
	}
	if (!finish_times.is_empty()) out["finish_times"] = finish_times;
	if (!finish_placements.is_empty()) out["finish_placements"] = finish_placements;
	if (!eliminations.is_empty()) out["eliminations"] = eliminations;
	if (!benchmark_suite.is_empty()) {
		out["benchmark_suite"] = benchmark_suite;
		out["benchmark_racer_count"] = benchmark_racer_count;
		out["benchmark_sample"] = benchmark_sample;
		out["benchmark_spawn_seed"] = benchmark_spawn_seed;
		out["benchmark_generation_ticks"] = benchmark_generation_ticks;
		out["benchmark_generation_usec"] = benchmark_generation_usec;
	}
	return out;
}

bool MxtReplayRunMetadata::load_dictionary(const Dictionary &value) {
	reset();
	schema_version = value.get("schema_version", 0);
	build = value.get("build", "");
	Dictionary game_version;
	if (dictionary_field(value, "game_version", game_version)) {
		game_version_major = game_version.get("major", 0);
		game_version_compatibility = game_version.get("compatibility", 0);
		game_version_patch = game_version.get("patch", 0);
	}
	engine_version = value.get("engine_version", "");
	created_unix = static_cast<int64_t>(value.get("created_unix", 0));
	name = value.get("name", "");
	mode = value.get("mode", "");
	source = value.get("source", "");
	track_content_id = value.get("track_content_id", "");
	track_gameplay_digest = value.get("track_gameplay_digest", "");
	track_package_digest = value.get("track_package_digest", "");
	track_workshop_id = value.get("track_workshop_id", "");
	track_name = value.get("track_name", "");
	const Variant settings_value = value.get("settings", Array());
	const Variant ids_value = value.get("racer_ids", Array());
	const Variant cpus_value = value.get("cpu_flags", Array());
	if (settings_value.get_type() != Variant::ARRAY || ids_value.get_type() != Variant::ARRAY || cpus_value.get_type() != Variant::ARRAY) {
		last_error = "Replay metadata roster fields are malformed.";
		return false;
	}
	const Array settings = settings_value;
	const Array ids = ids_value;
	const Array cpus = cpus_value;
	if (settings.size() == 0 || settings.size() != ids.size() || settings.size() != cpus.size() || settings.size() > MAX_RACERS) {
		last_error = "Replay metadata roster sizes do not match.";
		return false;
	}
	Ref<MxtRaceRoster> decoded_roster;
	decoded_roster.instantiate();
	for (int64_t i = 0; i < settings.size(); ++i) {
		if (settings[i].get_type() != Variant::DICTIONARY || !decoded_roster->append_settings(
				static_cast<int64_t>(ids[i]), static_cast<int64_t>(ids[i]), static_cast<bool>(cpus[i]), false, false, settings[i])) {
			last_error = decoded_roster->get_last_error().is_empty() ? "Replay metadata contains malformed player settings." : decoded_roster->get_last_error();
			return false;
		}
	}
	roster = decoded_roster;
	const Variant grid_value = value.get("start_grid_slots", Array());
	if (grid_value.get_type() == Variant::PACKED_INT32_ARRAY) start_grid_slots = grid_value;
	else if (grid_value.get_type() == Variant::ARRAY) {
		const Array grid = grid_value;
		if (grid.size() > MAX_RACERS) {
			last_error = "Replay metadata start grid is too large.";
			return false;
		}
		start_grid_slots.resize(grid.size());
		for (int64_t i = 0; i < grid.size(); ++i) start_grid_slots.set(i, grid[i]);
	}
	spawn_seed = value.get("spawn_seed", 0);
	Dictionary options;
	if (dictionary_field(value, "race_options", options)) set_race_metadata(options);
	Dictionary runtime;
	if (dictionary_field(value, "runtime_flags", runtime)) {
		runtime_auto_accelerate = runtime.get("auto_accelerate", false);
		runtime_auto_bumpers = runtime.get("auto_bumpers", false);
		runtime_debug_bumper_smoke = runtime.get("debug_bumper_smoke", false);
		runtime_debug_rail_trace = runtime.get("debug_rail_trace", false);
	}
	saved_reason = value.get("saved_reason", "");
	duration_ticks = value.get("duration_ticks", 0);
	Dictionary finish_times;
	Dictionary placements;
	Dictionary eliminations;
	dictionary_field(value, "finish_times", finish_times);
	dictionary_field(value, "finish_placements", placements);
	dictionary_field(value, "eliminations", eliminations);
	set_results(finish_times, placements, eliminations);
	benchmark_suite = value.get("benchmark_suite", "");
	benchmark_racer_count = value.get("benchmark_racer_count", 0);
	benchmark_sample = value.get("benchmark_sample", 0);
	benchmark_spawn_seed = value.get("benchmark_spawn_seed", 0);
	benchmark_generation_ticks = value.get("benchmark_generation_ticks", 0);
	benchmark_generation_usec = value.get("benchmark_generation_usec", 0);
	last_error = String();
	return true;
}
