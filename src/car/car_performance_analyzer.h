#pragma once

#include "car/car_authoring_session.h"
#include "car/car_performance_core.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <array>
#include <cstdint>

namespace godot {

class MxtCarPerformanceAnalyzer : public RefCounted {
	GDCLASS(MxtCarPerformanceAnalyzer, RefCounted)

private:
	static constexpr uint8_t OFFICIAL_COUNT = 4;
	static constexpr uint16_t ANCHOR_CACHE_SIZE = 256;
	static constexpr uint16_t RESULT_CACHE_SIZE = 128;

	struct AnchorEntry {
		bool valid = false;
		uint32_t setting_bits = 0;
		PhysicsCarProperties properties[OFFICIAL_COUNT];
		CarPerformanceRaw raw[OFFICIAL_COUNT];
	};
	struct ResultEntry {
		bool valid = false;
		uint64_t source_hash = 0;
		uint32_t setting_bits = 0;
		PhysicsCarProperties properties;
		CarPerformanceRaw raw;
	};

	PackedByteArray official_documents[OFFICIAL_COUNT];
	std::array<AnchorEntry, ANCHOR_CACHE_SIZE> anchor_cache{};
	std::array<ResultEntry, RESULT_CACHE_SIZE> result_cache{};
	uint16_t next_anchor_slot = 0;
	uint16_t next_result_slot = 0;
	bool official_documents_loaded = false;
	String official_error;

	bool ensure_official_documents();
	AnchorEntry *get_anchor(float setting);
	Dictionary analyze_document(
			const PackedByteArray &bytes,
			float setting,
			uint64_t source_hash,
			bool allow_result_cache);
	Dictionary build_result(
			const PhysicsCarProperties &properties,
			const CarPerformanceRaw &raw,
			const AnchorEntry &anchors,
			float setting) const;

protected:
	static void _bind_methods();

public:
	Dictionary analyze_file(
			const String &properties_path,
			double machine_setting,
			const String &gameplay_digest = String());
	Dictionary analyze_session(
			const Ref<MxtCarAuthoringSession> &session,
			double machine_setting);
	Array get_official_calibration_table();
	void clear_result_cache();
};

} // namespace godot

