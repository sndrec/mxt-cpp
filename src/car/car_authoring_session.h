#pragma once

#include "car/car_properties.h"
#include "car/car_special_state_derivation.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace godot {

class MxtCarAuthoringSession : public RefCounted {
	GDCLASS(MxtCarAuthoringSession, RefCounted)

private:
	struct CurveKey {
		float time = 0.0f;
		float value = 0.0f;
		float tangent_in = 0.0f;
		float tangent_out = 0.0f;
	};

	using Curve = std::vector<CurveKey>;
	static constexpr uint32_t MAX_CURVE_KEYS = 4096;
	static constexpr uint32_t MAX_HISTORY = 64;

	std::array<Curve, CAR_CURVE_LAYER_COUNT * CAR_STAT_COUNT> curves;
	std::array<float, CAR_STAT_COUNT> s_boost_values{};
	static constexpr uint32_t AUTHORING_PAIR_COUNT = CAR_AUTHORING_SPECIAL_LAYER_COUNT * CAR_STAT_COUNT;
	static constexpr uint32_t AUTHORING_MASK_WORD_COUNT = (AUTHORING_PAIR_COUNT + 63u) / 64u;
	std::array<uint64_t, AUTHORING_MASK_WORD_COUNT> derived_pair_mask{};
	std::array<SimVec3, 4> tilt_corners{};
	std::array<SimVec3, 4> wall_corners{};
	uint32_t state_flags = 0;
	bool dirty = false;
	String model_path;
	Vector3 model_translation = Vector3();
	Vector3 model_rotation_degrees = Vector3();
	Vector3 model_scale = Vector3(1.0, 1.0, 1.0);
	std::vector<int32_t> body_surfaces;
	int32_t albedo_surface = -1;
	int32_t normal_surface = -1;
	int32_t paint_mask_surface = -1;
	struct Thruster {
		Vector3 position;
		Vector3 rotation_degrees;
		float scale = 1.0f;
	};
	std::vector<Thruster> thrusters;
	struct HistorySnapshot {
		PackedByteArray properties;
		std::array<uint64_t, AUTHORING_MASK_WORD_COUNT> derived_pair_mask{};
		bool dirty = false;
		String model_path;
		Vector3 model_translation;
		Vector3 model_rotation_degrees;
		Vector3 model_scale;
		std::vector<int32_t> body_surfaces;
		int32_t albedo_surface = -1;
		int32_t normal_surface = -1;
		int32_t paint_mask_surface = -1;
		std::vector<Thruster> thrusters;
	};
	std::vector<HistorySnapshot> undo_history;
	std::vector<HistorySnapshot> redo_history;
	HistorySnapshot transaction_snapshot;
	uint32_t edit_transaction_depth = 0;
	bool transaction_snapshot_valid = false;

	static int32_t stat_index(const String &stat_name);
	static int32_t layer_index(const String &layer_name);
	static int32_t special_layer_index(const String &layer_name);
	static bool read_document(
			const PackedByteArray &bytes,
			MxtCarAuthoringSession &target,
			String &out_error);
	bool validate_document(PackedStringArray &out_errors, PackedStringArray &out_warnings) const;
	bool serialize_document(PackedByteArray &out_bytes, String &out_error) const;
	bool capture_history_snapshot(HistorySnapshot &out_snapshot) const;
	void append_undo_snapshot(HistorySnapshot &&snapshot);
	void push_undo_snapshot();
	void clear_redo_after_mutation();
	bool restore_history_snapshot(const HistorySnapshot &snapshot, bool mark_dirty = true);
	static bool is_safe_draft_root(const String &path, String &out_global_path);
	Curve &curve_at(uint8_t layer, uint16_t stat);
	const Curve &curve_at(uint8_t layer, uint16_t stat) const;
	double sample_curve_at(uint8_t layer, uint16_t stat, float machine_setting) const;
	bool is_pair_derived(uint8_t special_layer, uint16_t stat) const;
	void set_pair_derived(uint8_t special_layer, uint16_t stat, bool derived);
	void sample_base_stats(float machine_setting, float out_stats[CAR_STAT_COUNT]) const;
	float derived_special_value(uint8_t special_layer, uint16_t stat, float machine_setting) const;
	void regenerate_derived_pair(uint8_t special_layer, uint16_t stat);
	void regenerate_all_derived_pairs();
	void infer_authoring_intent();
	bool parse_authoring_intent(const Dictionary &value, std::array<uint64_t, AUTHORING_MASK_WORD_COUNT> &out_mask) const;

protected:
	static void _bind_methods();

public:
	MxtCarAuthoringSession();

	void reset_to_defaults();
	Dictionary load_bytes(const PackedByteArray &bytes);
	Dictionary load_file(const String &path);
	Dictionary serialize() const;
	Dictionary save_file(const String &path);
	Dictionary validate() const;

	Array get_stat_schema() const;
	PackedStringArray get_layer_names() const;
	Array get_curve(const String &layer_name, const String &stat_name) const;
	Dictionary set_curve(const String &layer_name, const String &stat_name, const Array &keys);
	double sample_curve(const String &layer_name, const String &stat_name, double machine_setting) const;
	double get_s_boost_value(const String &stat_name) const;
	bool set_s_boost_value(const String &stat_name, double value);
	bool is_special_derived(const String &layer_name, const String &stat_name) const;
	Dictionary make_special_custom(const String &layer_name, const String &stat_name);
	Dictionary revert_special_derived(const String &layer_name, const String &stat_name);
	Dictionary get_authoring_intent() const;
	Dictionary set_authoring_intent(const Dictionary &value);
	PackedVector3Array get_tilt_corners() const;
	PackedVector3Array get_wall_corners() const;
	Dictionary get_collision_measurements() const;
	Dictionary set_collision_measurements(const Dictionary &value);
	int64_t get_state_flags() const;
	void set_state_flags(int64_t value);
	bool is_dirty() const;
	void clear_dirty();
	bool can_undo() const;
	bool can_redo() const;
	void begin_edit_transaction();
	void end_edit_transaction();
	void cancel_edit_transaction();
	bool undo();
	bool redo();
	Dictionary import_model(const String &source_path, const String &draft_root);
	Dictionary load_vehicle_package(const String &package_root);
	Dictionary build_vehicle_package(
			const String &package_root,
			const String &preview_png_path,
			const String &title,
			const String &description,
			const String &author_name);
	String get_model_path() const;
	bool load_draft_visual_state(
			const String &draft_model_path,
			const Dictionary &model_transform,
			const Dictionary &material_setup,
			const Array &draft_thrusters);
	Dictionary get_model_transform() const;
	bool set_model_transform(const Dictionary &value);
	Array get_model_surfaces() const;
	Dictionary get_model_resource_usage() const;
	Dictionary get_material_setup() const;
	bool set_material_setup(const Dictionary &value);
	Array get_thrusters() const;
	bool set_thrusters(const Array &value);
	Dictionary sample_effective_stats(
			double machine_setting,
			const String &technique,
			double technique_intensity,
			const String &boost_state) const;
	Dictionary simulate_speed_preview(
			double machine_setting,
			double starting_speed_kmh,
			bool frame_perfect_boosting,
			const String &technique,
			double technique_intensity,
			const String &boost_state) const;
};

} // namespace godot
