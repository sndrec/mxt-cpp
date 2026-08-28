#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class MxtRaceSessionState : public RefCounted {
	GDCLASS(MxtRaceSessionState, RefCounted)

	struct IntValue { int64_t player_id = 0; int32_t value = 0; };
	struct FloatValue { int64_t player_id = 0; float value = 0.0f; };

	int32_t netplay_phase = 0;
	int64_t spawn_seed = 0;
	PackedInt64Array human_ids;
	PackedInt64Array cpu_ids;
	PackedInt64Array spectator_ids;
	int32_t grand_prix_current_track = 0;
	int32_t grand_prix_recorded_track = -1;
	PackedInt64Array grand_prix_eliminated_ids;
	std::vector<IntValue> grand_prix_points;
	std::vector<FloatValue> grand_prix_ko_energy_bonuses;
	String last_error;

	static int32_t find_int(const std::vector<IntValue> &values, int64_t player_id);
	static int32_t find_float(const std::vector<FloatValue> &values, int64_t player_id);

protected:
	static void _bind_methods();

public:
	Ref<MxtRaceSessionState> copy() const;
	void clear();
	bool is_empty() const;

	int32_t get_netplay_phase() const { return netplay_phase; }
	void set_netplay_phase(int32_t value) { netplay_phase = value & 1; }
	int64_t get_spawn_seed() const { return spawn_seed; }
	void set_spawn_seed(int64_t value) { spawn_seed = value; }
	PackedInt64Array get_human_ids() const { return human_ids; }
	void set_human_ids(const PackedInt64Array &value) { human_ids = value; }
	PackedInt64Array get_cpu_ids() const { return cpu_ids; }
	void set_cpu_ids(const PackedInt64Array &value) { cpu_ids = value; }
	PackedInt64Array get_spectator_ids() const { return spectator_ids; }
	void set_spectator_ids(const PackedInt64Array &value) { spectator_ids = value; }
	int32_t get_grand_prix_current_track() const { return grand_prix_current_track; }
	void set_grand_prix_current_track(int32_t value) { grand_prix_current_track = value; }
	int32_t get_grand_prix_recorded_track() const { return grand_prix_recorded_track; }
	void set_grand_prix_recorded_track(int32_t value) { grand_prix_recorded_track = value; }
	PackedInt64Array get_grand_prix_eliminated_ids() const { return grand_prix_eliminated_ids; }
	void set_grand_prix_eliminated_ids(const PackedInt64Array &value) { grand_prix_eliminated_ids = value; }

	int32_t get_grand_prix_points(int64_t player_id) const;
	PackedInt64Array get_grand_prix_point_player_ids() const;
	void set_grand_prix_points(int64_t player_id, int32_t value);
	float get_grand_prix_ko_energy_bonus(int64_t player_id) const;
	void set_grand_prix_ko_energy_bonus(int64_t player_id, float value);
	void clear_grand_prix_points() { grand_prix_points.clear(); }
	void clear_grand_prix_ko_energy_bonuses() { grand_prix_ko_energy_bonuses.clear(); }

	Dictionary to_metadata_dictionary() const;
	bool load_metadata_dictionary(const Dictionary &value);
	PackedByteArray encode_wire() const;
	bool decode_wire(const PackedByteArray &bytes);
	String get_last_error() const { return last_error; }
};

} // namespace godot
