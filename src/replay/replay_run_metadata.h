#pragma once

#include "core/race_configuration.h"
#include "core/race_roster.h"
#include "core/track_content_evidence.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class MxtReplayRunMetadata : public RefCounted {
	GDCLASS(MxtReplayRunMetadata, RefCounted)

	struct Result {
		int64_t player_id = 0;
		int64_t finish_tick = -1;
		int32_t placement = -1;
		int64_t elimination_tick = -1;
	};

	int32_t schema_version = 0;
	int32_t game_version_major = 0;
	int32_t game_version_compatibility = 0;
	int32_t game_version_patch = 0;
	String build;
	String engine_version;
	int64_t created_unix = 0;
	String name;
	String mode;
	String source;
	String track_content_id;
	String track_gameplay_digest;
	String track_package_digest;
	String track_workshop_id;
	String track_name;
	Ref<MxtRaceRoster> roster;
	PackedInt32Array start_grid_slots;
	int64_t spawn_seed = 0;
	Ref<MxtRaceConfiguration> race_configuration;
	Ref<MxtTrackContentEvidence> track_evidence;
	struct GrandPrixEntry {
		int64_t player_id = 0;
		int32_t points = 0;
		float ko_energy_bonus = 0.0f;
	};
	int32_t grand_prix_current_track = 0;
	int32_t grand_prix_recorded_track = -1;
	std::vector<GrandPrixEntry> grand_prix_entries;
	std::vector<int64_t> grand_prix_eliminated_ids;
	bool runtime_auto_accelerate = false;
	bool runtime_auto_bumpers = false;
	bool runtime_debug_bumper_smoke = false;
	bool runtime_debug_rail_trace = false;
	String saved_reason;
	int64_t duration_ticks = 0;
	std::vector<Result> results;
	String benchmark_suite;
	int32_t benchmark_racer_count = 0;
	int32_t benchmark_sample = 0;
	int64_t benchmark_spawn_seed = 0;
	int64_t benchmark_generation_ticks = 0;
	int64_t benchmark_generation_usec = 0;
	String last_error;

	Result *result_for_player(int64_t player_id);
	const Result *result_for_player(int64_t player_id) const;

protected:
	static void _bind_methods();

public:
	MxtReplayRunMetadata();
	Ref<MxtReplayRunMetadata> copy() const;
	void reset();

	int32_t get_schema_version() const { return schema_version; }
	void set_schema_version(int32_t value) { schema_version = value; }
	int32_t get_game_version_major() const { return game_version_major; }
	void set_game_version_major(int32_t value) { game_version_major = value; }
	int32_t get_game_version_compatibility() const { return game_version_compatibility; }
	void set_game_version_compatibility(int32_t value) { game_version_compatibility = value; }
	int32_t get_game_version_patch() const { return game_version_patch; }
	void set_game_version_patch(int32_t value) { game_version_patch = value; }
	String get_build() const { return build; }
	void set_build(const String &value) { build = value; }
	String get_engine_version() const { return engine_version; }
	void set_engine_version(const String &value) { engine_version = value; }
	int64_t get_created_unix() const { return created_unix; }
	void set_created_unix(int64_t value) { created_unix = value; }
	String get_name() const { return name; }
	void set_name(const String &value) { name = value; }
	String get_mode() const { return mode; }
	void set_mode(const String &value) { mode = value; }
	String get_source() const { return source; }
	void set_source(const String &value) { source = value; }
	String get_track_content_id() const { return track_content_id; }
	void set_track_content_id(const String &value) { track_content_id = value; }
	String get_track_gameplay_digest() const { return track_gameplay_digest; }
	void set_track_gameplay_digest(const String &value) { track_gameplay_digest = value; }
	String get_track_package_digest() const { return track_package_digest; }
	void set_track_package_digest(const String &value) { track_package_digest = value; }
	String get_track_workshop_id() const { return track_workshop_id; }
	void set_track_workshop_id(const String &value) { track_workshop_id = value; }
	String get_track_name() const { return track_name; }
	void set_track_name(const String &value) { track_name = value; }
	Ref<MxtRaceRoster> get_roster() const { return roster; }
	void set_roster(const Ref<MxtRaceRoster> &value);
	PackedInt32Array get_start_grid_slots() const { return start_grid_slots; }
	void set_start_grid_slots(const PackedInt32Array &value) { start_grid_slots = value; }
	int64_t get_spawn_seed() const { return spawn_seed; }
	void set_spawn_seed(int64_t value) { spawn_seed = value; }
	Ref<MxtRaceConfiguration> get_race_configuration() const { return race_configuration; }
	void set_race_configuration(const Ref<MxtRaceConfiguration> &value);
	Ref<MxtTrackContentEvidence> get_track_evidence() const { return track_evidence; }
	void set_track_evidence(const Ref<MxtTrackContentEvidence> &value);
	bool get_runtime_auto_accelerate() const { return runtime_auto_accelerate; }
	void set_runtime_auto_accelerate(bool value) { runtime_auto_accelerate = value; }
	bool get_runtime_auto_bumpers() const { return runtime_auto_bumpers; }
	void set_runtime_auto_bumpers(bool value) { runtime_auto_bumpers = value; }
	bool get_runtime_debug_bumper_smoke() const { return runtime_debug_bumper_smoke; }
	void set_runtime_debug_bumper_smoke(bool value) { runtime_debug_bumper_smoke = value; }
	bool get_runtime_debug_rail_trace() const { return runtime_debug_rail_trace; }
	void set_runtime_debug_rail_trace(bool value) { runtime_debug_rail_trace = value; }
	String get_saved_reason() const { return saved_reason; }
	void set_saved_reason(const String &value) { saved_reason = value; }
	int64_t get_duration_ticks() const { return duration_ticks; }
	void set_duration_ticks(int64_t value) { duration_ticks = value; }
	String get_benchmark_suite() const { return benchmark_suite; }
	void set_benchmark_suite(const String &value) { benchmark_suite = value; }
	int32_t get_benchmark_racer_count() const { return benchmark_racer_count; }
	void set_benchmark_racer_count(int32_t value) { benchmark_racer_count = value; }
	int32_t get_benchmark_sample() const { return benchmark_sample; }
	void set_benchmark_sample(int32_t value) { benchmark_sample = value; }
	int64_t get_benchmark_spawn_seed() const { return benchmark_spawn_seed; }
	void set_benchmark_spawn_seed(int64_t value) { benchmark_spawn_seed = value; }
	int64_t get_benchmark_generation_ticks() const { return benchmark_generation_ticks; }
	void set_benchmark_generation_ticks(int64_t value) { benchmark_generation_ticks = value; }
	int64_t get_benchmark_generation_usec() const { return benchmark_generation_usec; }
	void set_benchmark_generation_usec(int64_t value) { benchmark_generation_usec = value; }
	String get_last_error() const { return last_error; }

	void set_results(const Dictionary &finish_times, const Dictionary &finish_placements, const Dictionary &eliminations);
	void set_race_metadata(const Dictionary &value);
	Dictionary to_dictionary() const;
	bool load_dictionary(const Dictionary &value);
};

} // namespace godot
