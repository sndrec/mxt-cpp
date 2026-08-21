#include "car/car_performance_analyzer.h"

#include "car/car_stat_metadata.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace godot {

namespace {

static constexpr const char *OFFICIAL_PATHS[4] = {
	"res://vehicle/asset/accelerator/golden_fox.mxt_car_props",
	"res://vehicle/asset/allrounder/blue_falcon.mxt_car_props",
	"res://vehicle/asset/bruiser/wild_goose.mxt_car_props",
	"res://vehicle/asset/topspeeder/fire_stingray.mxt_car_props"};
static constexpr const char *OFFICIAL_NAMES[4] = {
	"Golden Fox", "Blue Falcon", "Wild Goose", "Fire Stingray"};
static constexpr uint8_t ALL_ROUNDER_INDEX = 1;

static uint32_t float_bits(float value) {
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static uint64_t hash_bytes(const PackedByteArray &bytes) {
	uint64_t hash = UINT64_C(1469598103934665603);
	for (int64_t i = 0; i < bytes.size(); ++i) {
		hash ^= bytes[i];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static uint64_t hash_string(const String &value) {
	const CharString utf8 = value.utf8();
	uint64_t hash = UINT64_C(1469598103934665603);
	for (int64_t i = 0; i < utf8.length(); ++i) {
		hash ^= static_cast<uint8_t>(utf8[i]);
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static float score_against_anchors(
		float value,
		float center,
		float grade_a,
		float grade_e,
		float grade_s,
		float grade_f) {
	const float scale = std::max({std::abs(center), std::abs(grade_a), std::abs(grade_e),
		std::abs(grade_s), std::abs(grade_f), 1.0f});
	const float epsilon = scale * 0.000001f;
	if (std::abs(value - center) <= epsilon) return 2.0f;
	if (value > center) {
		const float c_to_a = grade_a - center;
		if (value <= grade_a && c_to_a > epsilon) {
			return 2.0f + 2.0f * (value - center) / c_to_a;
		}
		const float a_to_s = grade_s - grade_a;
		if (a_to_s > epsilon) return 4.0f + (value - grade_a) / a_to_s;
		if (c_to_a > epsilon) return 4.0f + 2.0f * (value - grade_a) / c_to_a;
		return 4.0f + (value - center) / scale;
	}
	const float c_to_e = center - grade_e;
	if (value >= grade_e && c_to_e > epsilon) {
		return 2.0f - 2.0f * (center - value) / c_to_e;
	}
	const float e_to_f = grade_e - grade_f;
	if (e_to_f > epsilon) return -(grade_e - value) / e_to_f;
	if (c_to_e > epsilon) return -2.0f * (grade_e - value) / c_to_e;
	return -(center - value) / scale;
}

static String nonnegative_grade_stem(float anchor) {
	static constexpr const char *BASE_GRADES[5] = {"E", "D", "C", "B", "A"};
	if (anchor <= 4.0f) {
		return BASE_GRADES[std::clamp(static_cast<int>(anchor), 0, 4)];
	}
	const float s_count = anchor - 4.0f;
	if (s_count > 8.0f) {
		return String::utf8("S×") + String::num(s_count, 0);
	}
	String label;
	for (int i = 0; i < static_cast<int>(s_count); ++i) label += "S";
	return label;
}

static String negative_grade_stem(float distance) {
	if (distance < 1.0f) return "E";
	if (distance > 8.0f) {
		return String::utf8("F×") + String::num(distance, 0);
	}
	String label;
	for (int i = 0; i < static_cast<int>(distance); ++i) label += "F";
	return label;
}

static String grade_label(float score) {
	if (!std::isfinite(score)) return "?";
	if (score < -0.00001f) {
		const float distance = -score;
		const float lower = std::floor(distance);
		const float fraction = distance - lower;
		const String lower_label = negative_grade_stem(lower);
		if (fraction < 1.0f / 3.0f) return lower_label;
		if (fraction < 0.5f) return lower_label + String("-");
		const String upper_label = negative_grade_stem(lower + 1.0f);
		if (fraction < 2.0f / 3.0f) return upper_label + String("+");
		return upper_label;
	}
	const float lower = std::floor(std::max(score, 0.0f));
	const float fraction = score - lower;
	const String lower_label = nonnegative_grade_stem(lower);
	if (fraction < 1.0f / 3.0f) return lower_label;
	if (fraction < 0.5f) return lower_label + String("+");
	const String upper_label = nonnegative_grade_stem(lower + 1.0f);
	if (fraction < 2.0f / 3.0f) return upper_label + String("-");
	return upper_label;
}

static float display_component_value(
		CarPerformanceCategory category, uint8_t component, float value) {
	if ((category == CAR_PERFORMANCE_ACCELERATION && (component == 3 || component == 4)) ||
		(category == CAR_PERFORMANCE_GRIP && component == 1)) {
		return -value;
	}
	return value;
}

static String trait_context(uint8_t layer) {
	static constexpr const char *NAMES[7] = {
		"While Turbo Sliding", "While Quick Turning", "Without Boost",
		"While Manual Boosting", "While Dashplate Boosting",
		"While Stacking Boosts", "During S-Boost"};
	return layer < 7 ? NAMES[layer] : "In a Special State";
}

static bool trait_context_can_use_stat(uint8_t layer, CarStatId stat) {
	// S-BOOST overrides are intentionally omitted: their individual values are
	// too implementation-facing to make useful player-visible traits.
	if (layer >= CAR_MODIFIER_LAYER_COUNT || stat == CAR_STAT_DRIVE_TARGET_SPEED_MULTIPLIER) {
		return false;
	}

	switch (stat) {
	case CAR_STAT_BOOST_ENERGY_USE_RATE:
		return layer == CAR_MODIFIER_MTS || layer == CAR_MODIFIER_QUICKTURN ||
			layer == CAR_MODIFIER_MANUAL_BOOST || layer == CAR_MODIFIER_STACKED_BOOST;
	case CAR_STAT_MANUAL_TURBO_GAIN:
	case CAR_STAT_MANUAL_BOOST_DURATION_SECONDS:
		return layer == CAR_MODIFIER_MTS || layer == CAR_MODIFIER_QUICKTURN ||
			layer == CAR_MODIFIER_MANUAL_BOOST;
	case CAR_STAT_DASHPLATE_TURBO_GAIN:
	case CAR_STAT_DASHPLATE_TURBO_HEAT_MULTIPLIER:
	case CAR_STAT_DASHPLATE_BOOST_DURATION_SECONDS:
		return layer == CAR_MODIFIER_MTS || layer == CAR_MODIFIER_QUICKTURN ||
			layer == CAR_MODIFIER_DASHPLATE_BOOST || layer == CAR_MODIFIER_STACKED_BOOST;
	case CAR_STAT_S_BOOST_BASE_SPEED_ADD_PER_SECOND:
		return layer == CAR_MODIFIER_MTS || layer == CAR_MODIFIER_QUICKTURN ||
			layer == CAR_MODIFIER_DASHPLATE_BOOST;
	default:
		return true;
	}
}

static CarStatDirection trait_direction(
		uint8_t layer, CarStatId stat, const CarStatMetadata &metadata) {
	if (layer == CAR_MODIFIER_MTS) {
		switch (stat) {
		case CAR_STAT_ACCELERATION:
		case CAR_STAT_GRIP_1:
		case CAR_STAT_GRIP_3:
		case CAR_STAT_TURN_TENSION:
		case CAR_STAT_ACCEL_PRESS_GRIP_FRAMES:
			return CAR_STAT_DIRECTION_LOWER_BENEFIT;
		case CAR_STAT_DRIFT_ACCEL:
			return CAR_STAT_DIRECTION_HIGHER_BENEFIT;
		default:
			break;
		}
	}
	return metadata.special_direction;
}

static String trait_interpretation(
		uint8_t layer, CarStatId stat, CarStatDirection direction) {
	if (layer == CAR_MODIFIER_MTS && stat == CAR_STAT_ACCELERATION) {
		return "Lower acceleration is advantageous during a Turbo Slide because it reduces the pull back down toward the normal drive-speed target while the machine is above it.";
	}
	if (layer == CAR_MODIFIER_MTS &&
		(stat == CAR_STAT_GRIP_1 || stat == CAR_STAT_GRIP_3 ||
		 stat == CAR_STAT_TURN_TENSION || stat == CAR_STAT_ACCEL_PRESS_GRIP_FRAMES)) {
		return "Lower restoring grip is advantageous during a Turbo Slide because it lets the machine remain loose and carry a stronger sideways slide.";
	}
	if (layer == CAR_MODIFIER_MTS && stat == CAR_STAT_DRIFT_ACCEL) {
		return "Higher drift acceleration directly adds more forward drive during a Turbo Slide.";
	}
	if (direction == CAR_STAT_DIRECTION_HIGHER_BENEFIT) {
		return "A higher value is treated as advantageous in this state.";
	}
	if (direction == CAR_STAT_DIRECTION_LOWER_BENEFIT) {
		return "A lower value is treated as advantageous in this state.";
	}
	return "Its advantage depends on the maneuver, so this is shown as a distinctive difference rather than a buff or drawback.";
}

static String signed_percent(float value) {
	return String(value >= 0.0f ? "+" : "") + String::num(value, 1) + "%";
}

static bool trait_values_match(float a, float b) {
	const float scale = std::max({std::abs(a), std::abs(b), 1.0f});
	return std::abs(a - b) <= scale * 0.00001f;
}

static float trait_adjustment(
		const PhysicsCarProperties &properties, uint8_t special, CarStatId stat) {
	if (special < 6) return properties.modifier_stats[special][stat];
	const float ordinary = properties.base_stats[stat];
	return std::abs(ordinary) > 0.00001f
		? properties.s_boost_stats[stat] / ordinary
		: properties.s_boost_stats[stat];
}

static float trait_effective_value(float ordinary, uint8_t special, float adjustment) {
	return special < 6 || std::abs(ordinary) > 0.00001f
		? ordinary * adjustment
		: adjustment;
}

static bool find_trait_majority_baseline(const float values[4], float &baseline) {
	for (uint8_t candidate = 0; candidate < 4; ++candidate) {
		uint8_t matches = 0;
		for (uint8_t other = 0; other < 4; ++other) {
			matches += trait_values_match(values[candidate], values[other]) ? 1 : 0;
		}
		if (matches > 2) {
			baseline = values[candidate];
			return true;
		}
	}
	return false;
}

} // namespace

void MxtCarPerformanceAnalyzer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("analyze_file", "properties_path", "machine_setting", "gameplay_digest"),
		&MxtCarPerformanceAnalyzer::analyze_file, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("analyze_session", "session", "machine_setting"),
		&MxtCarPerformanceAnalyzer::analyze_session);
	ClassDB::bind_method(D_METHOD("get_official_calibration_table"),
		&MxtCarPerformanceAnalyzer::get_official_calibration_table);
	ClassDB::bind_method(D_METHOD("clear_result_cache"),
		&MxtCarPerformanceAnalyzer::clear_result_cache);
}

bool MxtCarPerformanceAnalyzer::ensure_official_documents() {
	if (official_documents_loaded) return official_error.is_empty();
	official_documents_loaded = true;
	for (uint8_t index = 0; index < OFFICIAL_COUNT; ++index) {
		if (!FileAccess::file_exists(OFFICIAL_PATHS[index])) {
			official_error = String("official benchmark properties are missing: ") + OFFICIAL_PATHS[index];
			return false;
		}
		official_documents[index] = FileAccess::get_file_as_bytes(OFFICIAL_PATHS[index]);
		if (official_documents[index].is_empty()) {
			official_error = String("official benchmark properties could not be read: ") + OFFICIAL_PATHS[index];
			return false;
		}
	}
	return true;
}

MxtCarPerformanceAnalyzer::AnchorEntry *MxtCarPerformanceAnalyzer::get_anchor(float setting) {
	if (!ensure_official_documents()) return nullptr;
	const uint32_t bits = float_bits(setting);
	for (AnchorEntry &entry : anchor_cache) {
		if (entry.valid && entry.setting_bits == bits) return &entry;
	}
	AnchorEntry &entry = anchor_cache[next_anchor_slot++ % ANCHOR_CACHE_SIZE];
	entry = AnchorEntry{};
	entry.setting_bits = bits;
	String error;
	for (uint8_t index = 0; index < OFFICIAL_COUNT; ++index) {
		if (!PhysicsCarProperties::deserialize_and_sample(
				official_documents[index], setting, entry.properties[index], error)) {
			official_error = String("official benchmark properties are invalid: ") + error;
			return nullptr;
		}
	}
	for (uint8_t index = 0; index < OFFICIAL_COUNT; ++index) {
		if (!analyze_car_performance(
				official_documents[index], setting, entry.properties[index], entry.raw[index])) {
			official_error = "official benchmark analysis produced a non-finite result";
			return nullptr;
		}
	}
	entry.valid = true;
	return &entry;
}

bool MxtCarPerformanceAnalyzer::ensure_grade_calibration() {
	if (grade_calibration.valid) return true;
	static constexpr float SETTINGS[3] = {0.0f, 0.5f, 1.0f};
	AnchorEntry *samples[3];
	for (uint8_t setting_index = 0; setting_index < 3; ++setting_index) {
		samples[setting_index] = get_anchor(SETTINGS[setting_index]);
		if (samples[setting_index] == nullptr) return false;
	}

	GradeCalibration calibration;
	calibration.center_properties = samples[1]->properties[ALL_ROUNDER_INDEX];
	calibration.center_raw = samples[1]->raw[ALL_ROUNDER_INDEX];
	for (uint8_t category = 0; category < CAR_PERFORMANCE_CATEGORY_COUNT; ++category) {
		for (uint8_t component = 0; component < car_performance_component_count(
				static_cast<CarPerformanceCategory>(category)); ++component) {
			const float center = calibration.center_raw.components[category][component].value;
			float grade_a = center;
			float grade_e = center;
			float grade_s = center;
			float grade_f = center;
			for (uint8_t setting_index = 0; setting_index < 3; ++setting_index) {
				const float all_rounder_value = samples[setting_index]->raw[ALL_ROUNDER_INDEX]
					.components[category][component].value;
				grade_a = std::max(grade_a, all_rounder_value);
				grade_e = std::min(grade_e, all_rounder_value);
				for (uint8_t official = 0; official < OFFICIAL_COUNT; ++official) {
					const float value = samples[setting_index]->raw[official]
						.components[category][component].value;
					grade_s = std::max(grade_s, value);
					grade_f = std::min(grade_f, value);
				}
			}
			const float scale = std::max({std::abs(center), std::abs(grade_a),
				std::abs(grade_e), 1.0f});
			const float epsilon = scale * 0.000001f;
			if (grade_a - center <= epsilon) grade_a = grade_s;
			if (center - grade_e <= epsilon) grade_e = grade_f;
			calibration.component_best[category][component] = grade_a;
			calibration.component_worst[category][component] = grade_e;
			calibration.component_official_best[category][component] = grade_s;
			calibration.component_official_worst[category][component] = grade_f;
		}
	}
	for (uint16_t stat = 0; stat < CAR_STAT_COUNT; ++stat) {
		const float center = calibration.center_properties.base_stats[stat];
		float grade_e_minimum = center;
		float grade_a_maximum = center;
		float official_minimum = center;
		float official_maximum = center;
		for (uint8_t setting_index = 0; setting_index < 3; ++setting_index) {
			const float all_rounder_value =
				samples[setting_index]->properties[ALL_ROUNDER_INDEX].base_stats[stat];
			grade_e_minimum = std::min(grade_e_minimum, all_rounder_value);
			grade_a_maximum = std::max(grade_a_maximum, all_rounder_value);
			for (uint8_t official = 0; official < OFFICIAL_COUNT; ++official) {
				const float value = samples[setting_index]->properties[official].base_stats[stat];
				official_minimum = std::min(official_minimum, value);
				official_maximum = std::max(official_maximum, value);
			}
		}
		const float scale = std::max({std::abs(center), std::abs(grade_e_minimum),
			std::abs(grade_a_maximum), 1.0f});
		const float epsilon = scale * 0.000001f;
		if (center - grade_e_minimum <= epsilon) grade_e_minimum = official_minimum;
		if (grade_a_maximum - center <= epsilon) grade_a_maximum = official_maximum;
		calibration.stat_min[stat] = grade_e_minimum;
		calibration.stat_max[stat] = grade_a_maximum;
		calibration.stat_official_min[stat] = official_minimum;
		calibration.stat_official_max[stat] = official_maximum;
	}
	calibration.valid = true;
	grade_calibration = calibration;
	return true;
}

Dictionary MxtCarPerformanceAnalyzer::analyze_document(
		const PackedByteArray &bytes,
		float setting,
		uint64_t source_hash,
		bool allow_result_cache) {
	Dictionary failed;
	failed["valid"] = false;
	if (bytes.is_empty()) {
		failed["error"] = "car properties document is empty";
		return failed;
	}
	AnchorEntry *trait_anchors = get_anchor(setting);
	if (trait_anchors == nullptr || !ensure_grade_calibration()) {
		failed["error"] = official_error;
		return failed;
	}
	const uint32_t bits = float_bits(setting);
	if (allow_result_cache) {
		for (const ResultEntry &entry : result_cache) {
			if (entry.valid && entry.source_hash == source_hash && entry.setting_bits == bits) {
				return build_result(entry.properties, entry.raw, *trait_anchors, setting);
			}
		}
	}
	PhysicsCarProperties properties;
	String error;
	if (!PhysicsCarProperties::deserialize_and_sample(bytes, setting, properties, error)) {
		failed["error"] = error;
		return failed;
	}
	CarPerformanceRaw raw;
	if (!analyze_car_performance(bytes, setting, properties, raw)) {
		failed["error"] = "performance analysis produced a non-finite result";
		return failed;
	}
	if (allow_result_cache) {
		ResultEntry &entry = result_cache[next_result_slot++ % RESULT_CACHE_SIZE];
		entry.valid = true;
		entry.source_hash = source_hash;
		entry.setting_bits = bits;
		entry.properties = properties;
		entry.raw = raw;
	}
	return build_result(properties, raw, *trait_anchors, setting);
}

Dictionary MxtCarPerformanceAnalyzer::build_result(
		const PhysicsCarProperties &properties,
		const CarPerformanceRaw &raw,
		const AnchorEntry &trait_anchors,
		float setting) const {
	Dictionary result;
	result["valid"] = true;
	result["benchmark_version"] = 11;
	result["machine_setting"] = setting;
	result["benchmark_machine_setting"] = 0.5f;
	result["benchmark_reference"] = "All Rounder at 50%; All Rounder range sampled at 0%, 50%, and 100% defines A and E; official extrema define S and F, with fallback for setting-invariant metrics";
	result["weight_kg"] = properties.base_stats[CAR_STAT_WEIGHT_KG];
	result["terminal_speed_kmh"] = raw.terminal_speed_kmh;
	result["peak_boost_speed_kmh"] = raw.peak_boost_speed_kmh;
	result["time_to_95_seconds"] = raw.time_to_95_seconds;
	result["settlement_confidence"] = raw.settlement_confidence;

	Array categories;
	for (uint8_t category_raw = 0; category_raw < CAR_PERFORMANCE_CATEGORY_COUNT; ++category_raw) {
		const CarPerformanceCategory category = static_cast<CarPerformanceCategory>(category_raw);
		Array components;
		float score_sum = 0.0f;
		const uint8_t count = car_performance_component_count(category);
		for (uint8_t component = 0; component < count; ++component) {
			const float grade_a = grade_calibration.component_best[category][component];
			const float grade_e = grade_calibration.component_worst[category][component];
			const float grade_s = grade_calibration.component_official_best[category][component];
			const float grade_f = grade_calibration.component_official_worst[category][component];
			const float center = grade_calibration.center_raw.components[category][component].value;
			const float value = raw.components[category][component].value;
			const float component_score = score_against_anchors(
				value, center, grade_a, grade_e, grade_s, grade_f);
			score_sum += component_score;
			Dictionary component_result;
			component_result["name"] = car_performance_component_name(category, component);
			component_result["explanation"] = car_performance_component_explanation(category, component);
			component_result["unit"] = car_performance_component_unit(category, component);
			component_result["value"] = display_component_value(category, component, value);
			component_result["score"] = component_score;
			component_result["grade"] = grade_label(component_score);
			components.push_back(component_result);
		}
		const float category_score = count > 0 ? score_sum / static_cast<float>(count) : 2.0f;
		Dictionary category_result;
		category_result["name"] = car_performance_category_name(category);
		category_result["score"] = category_score;
		category_result["grade"] = grade_label(category_score);
		category_result["components"] = components;
		categories.push_back(category_result);
	}
	result["categories"] = categories;

	Array advanced;
	for (uint16_t stat_raw = 0; stat_raw < CAR_STAT_COUNT; ++stat_raw) {
		const CarStatId stat = static_cast<CarStatId>(stat_raw);
		const CarStatMetadata &metadata = get_car_stat_metadata(stat);
		if (metadata.activity != CAR_STAT_ACTIVITY_GAMEPLAY || !metadata.raw_gradeable ||
			(metadata.base_direction != CAR_STAT_DIRECTION_HIGHER_BENEFIT &&
			 metadata.base_direction != CAR_STAT_DIRECTION_LOWER_BENEFIT)) {
			continue;
		}
		const float direction = metadata.base_direction == CAR_STAT_DIRECTION_LOWER_BENEFIT ? -1.0f : 1.0f;
		const float grade_a = direction > 0.0f
			? grade_calibration.stat_max[stat] : -grade_calibration.stat_min[stat];
		const float grade_e = direction > 0.0f
			? grade_calibration.stat_min[stat] : -grade_calibration.stat_max[stat];
		const float grade_s = direction > 0.0f
			? grade_calibration.stat_official_max[stat] : -grade_calibration.stat_official_min[stat];
		const float grade_f = direction > 0.0f
			? grade_calibration.stat_official_min[stat] : -grade_calibration.stat_official_max[stat];
		const float center = direction * grade_calibration.center_properties.base_stats[stat];
		const float score = score_against_anchors(
			direction * properties.base_stats[stat], center,
			grade_a, grade_e, grade_s, grade_f);
		Dictionary stat_result;
		stat_result["name"] = metadata.name;
		stat_result["friendly_name"] = metadata.friendly_name;
		stat_result["explanation"] = metadata.explanation;
		stat_result["unit"] = metadata.unit;
		stat_result["value"] = properties.base_stats[stat];
		stat_result["score"] = score;
		stat_result["grade"] = grade_label(score);
		advanced.push_back(stat_result);
	}
	result["advanced_stats"] = advanced;

	Array traits;
	for (uint8_t special = 0; special < 7; ++special) {
		for (uint16_t stat_raw = 0; stat_raw < CAR_STAT_COUNT; ++stat_raw) {
			const CarStatId stat = static_cast<CarStatId>(stat_raw);
			if (!trait_context_can_use_stat(special, stat)) continue;
			if (!PhysicsCarProperties::stat_supports_live_modifiers(stat)) continue;
			const CarStatMetadata &metadata = get_car_stat_metadata(stat);
			if (metadata.activity != CAR_STAT_ACTIVITY_GAMEPLAY) continue;
			const float ordinary = properties.base_stats[stat];
			const float adjustment = trait_adjustment(properties, special, stat);
			float official_adjustments[OFFICIAL_COUNT];
			for (uint8_t official = 0; official < OFFICIAL_COUNT; ++official) {
				official_adjustments[official] = trait_adjustment(
					trait_anchors.properties[official], special, stat);
			}
			float baseline_adjustment = 1.0f;
			const bool uses_roster_baseline = find_trait_majority_baseline(
				official_adjustments, baseline_adjustment);
			const float baseline = trait_effective_value(
				ordinary, special, baseline_adjustment);
			const float effective = trait_effective_value(ordinary, special, adjustment);
			const float delta = effective - baseline;
			const float denominator = std::max(std::abs(baseline_adjustment), 0.00001f);
			const float percent = 100.0f * (adjustment - baseline_adjustment) / denominator;
			if (std::abs(percent) < 5.0f) continue;
			const CarStatDirection direction = trait_direction(special, stat, metadata);
			String kind = "distinctive";
			if (direction == CAR_STAT_DIRECTION_HIGHER_BENEFIT) {
				kind = delta > 0.0f ? "strength" : "drawback";
			} else if (direction == CAR_STAT_DIRECTION_LOWER_BENEFIT) {
				kind = delta < 0.0f ? "strength" : "drawback";
			}
			const String arrow = kind == "strength" ? String::utf8("↑")
				: (kind == "drawback" ? String::utf8("↓") : String::utf8("◆"));
			Dictionary trait;
			trait["context"] = trait_context(special);
			trait["stat_name"] = metadata.name;
			trait["friendly_name"] = metadata.friendly_name;
			trait["explanation"] = String(metadata.explanation) + String(" ") +
				trait_interpretation(special, stat, direction);
			trait["kind"] = kind;
			trait["percent"] = percent;
			trait["base_value"] = baseline;
			trait["ordinary_value"] = ordinary;
			trait["baseline_adjustment"] = baseline_adjustment;
			trait["uses_roster_baseline"] = uses_roster_baseline;
			trait["effective_value"] = effective;
			trait["unit"] = metadata.unit;
			trait["text"] = arrow + String(" ") + trait_context(special) + String(": ") +
				signed_percent(percent) + String(" ") + String(metadata.friendly_name);
			traits.push_back(trait);
		}
	}
	result["traits"] = traits;
	return result;
}

Dictionary MxtCarPerformanceAnalyzer::analyze_file(
		const String &properties_path,
		double machine_setting,
		const String &gameplay_digest) {
	Dictionary failed;
	failed["valid"] = false;
	if (!FileAccess::file_exists(properties_path)) {
		failed["error"] = "car properties file does not exist";
		return failed;
	}
	const PackedByteArray bytes = FileAccess::get_file_as_bytes(properties_path);
	const uint64_t source_hash = gameplay_digest.is_empty()
		? hash_bytes(bytes) : hash_string(gameplay_digest);
	return analyze_document(bytes, std::clamp(static_cast<float>(machine_setting), 0.0f, 1.0f),
		source_hash, true);
}

Dictionary MxtCarPerformanceAnalyzer::analyze_session(
		const Ref<MxtCarAuthoringSession> &session,
		double machine_setting) {
	Dictionary failed;
	failed["valid"] = false;
	if (session.is_null()) {
		failed["error"] = "car authoring session is missing";
		return failed;
	}
	const Dictionary serialized = session->serialize();
	if (!static_cast<bool>(serialized.get("valid", false))) {
		failed["error"] = "car authoring session is not valid";
		return failed;
	}
	const PackedByteArray bytes = serialized.get("bytes", PackedByteArray());
	return analyze_document(bytes, std::clamp(static_cast<float>(machine_setting), 0.0f, 1.0f),
		hash_bytes(bytes), true);
}

Array MxtCarPerformanceAnalyzer::get_official_calibration_table() {
	Array table;
	if (!ensure_grade_calibration()) return table;
	for (int setting_index = 0; setting_index <= 100; ++setting_index) {
		const float setting = static_cast<float>(setting_index) / 100.0f;
		AnchorEntry *sampled = get_anchor(setting);
		if (sampled == nullptr) return Array();
		for (uint8_t official = 0; official < OFFICIAL_COUNT; ++official) {
			Dictionary row = build_result(
				sampled->properties[official], sampled->raw[official], *sampled, setting);
			row["machine"] = OFFICIAL_NAMES[official];
			table.push_back(row);
		}
	}
	return table;
}

void MxtCarPerformanceAnalyzer::clear_result_cache() {
	for (ResultEntry &entry : result_cache) entry = ResultEntry{};
	next_result_slot = 0;
}

} // namespace godot
