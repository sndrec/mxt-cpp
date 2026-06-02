#include "mxt_core/spatial_audio_manager.h"

#include "main.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace godot;

namespace {
static constexpr float MXT_AUDIO_MIN_FADE_SECONDS = 0.001f;
static constexpr float MXT_AUDIO_SILENCE_DB = -80.0f;
static constexpr float MXT_LOCAL_VEHICLE_VOLUME_GAIN_DB = 8.0f;

static float mxt_audio_clamp_float(float value, float min_value, float max_value)
{
	if (!std::isfinite(value)) {
		return min_value;
	}
	return std::max(min_value, std::min(max_value, value));
}

static float mxt_audio_lerp(float from, float to, float weight)
{
	return from + (to - from) * weight;
}

struct MxtMusyxCurvePoint {
	uint8_t threshold;
	uint8_t value;
	int16_t slope;
};

struct MxtMusyxPitchRatioPoint {
	uint8_t threshold;
	uint16_t pitch;
	int16_t slope;
};

struct MxtGxEngineTone {
	int sample_id;
	uint8_t volume_count;
	MxtMusyxCurvePoint volume[6];
	uint8_t pitch_count;
	MxtMusyxPitchRatioPoint pitch[6];
	float trim_db;
};

struct MxtGxEngineProgram {
	uint8_t tone_count;
	MxtGxEngineTone tones[5];
};

static int mxt_audio_eval_musyx_curve(uint8_t control, uint8_t base_control, const MxtMusyxCurvePoint* points, int count)
{
	if (!points || count <= 0) {
		return 0;
	}

	int point_index = 0;
	uint8_t segment_base = base_control;
	while (point_index + 1 < count && points[point_index].threshold < control) {
		segment_base = static_cast<uint8_t>(points[point_index].threshold + 1u);
		++point_index;
	}

	const int delta = static_cast<int>(control) - static_cast<int>(segment_base);
	return static_cast<int>(points[point_index].value) + ((static_cast<int>(points[point_index].slope) * delta) >> 8);
}

static float mxt_audio_eval_musyx_pitch_ratio(uint8_t control, uint8_t base_control, const MxtMusyxPitchRatioPoint* points, int count)
{
	if (!points || count <= 0) {
		return 1.0f;
	}

	int point_index = 0;
	uint8_t segment_base = base_control;
	while (point_index + 1 < count && points[point_index].threshold < control) {
		segment_base = static_cast<uint8_t>(points[point_index].threshold + 1u);
		++point_index;
	}

	const int delta = static_cast<int>(control) - static_cast<int>(segment_base);
	const int pitch_value = static_cast<int>(points[point_index].pitch) + ((static_cast<int>(points[point_index].slope) * delta) >> 8);
	const float cents = static_cast<float>(pitch_value - 0x0c00);
	return std::pow(2.0f, cents * (1.0f / 1200.0f));
}

static float mxt_audio_eval_musyx_pitch_lookup_scale(uint8_t control, const MxtMusyxCurvePoint* points, int count)
{
	static constexpr uint8_t pitch_lookup[128] = {
		0x01, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x08,
		0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x14,
		0x15, 0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x1e, 0x20,
		0x21, 0x23, 0x25, 0x26, 0x28, 0x29, 0x2b, 0x2c,
		0x2e, 0x2f, 0x30, 0x32, 0x33, 0x35, 0x36, 0x38,
		0x39, 0x3b, 0x3c, 0x3e, 0x40, 0x41, 0x43, 0x45,
		0x46, 0x48, 0x4a, 0x4b, 0x4d, 0x4f, 0x50, 0x52,
		0x54, 0x56, 0x57, 0x59, 0x5b, 0x5d, 0x5e, 0x60,
		0x62, 0x64, 0x67, 0x69, 0x6b, 0x6e, 0x70, 0x72,
		0x75, 0x77, 0x79, 0x7b, 0x7e, 0x80, 0x82, 0x85,
		0x87, 0x89, 0x8c, 0x8e, 0x90, 0x93, 0x95, 0x97,
		0x9a, 0x9c, 0x9e, 0xa0, 0xa3, 0xa5, 0xa7, 0xaa,
		0xac, 0xae, 0xb0, 0xb2, 0xb5, 0xb7, 0xba, 0xbc,
		0xbf, 0xc1, 0xc4, 0xc7, 0xc9, 0xcc, 0xcf, 0xd1,
		0xd4, 0xd7, 0xda, 0xdc, 0xdf, 0xe2, 0xe5, 0xe8,
		0xeb, 0xed, 0xf0, 0xf3, 0xf6, 0xf9, 0xfc, 0xff,
	};

	const int lookup_index = std::max(0, std::min(127, mxt_audio_eval_musyx_curve(control, 0u, points, count)));
	return static_cast<float>(pitch_lookup[lookup_index]) * (1.0f / 128.0f);
}

static float mxt_audio_musyx_volume_to_db(int volume, float trim_db)
{
	if (volume <= 0) {
		return MXT_AUDIO_SILENCE_DB;
	}
	const float gain = static_cast<float>(std::min(volume, 127)) * (1.0f / 127.0f);
	return std::max(MXT_AUDIO_SILENCE_DB, trim_db + 20.0f * std::log10(gain));
}

static float mxt_audio_speed_kmh_to_gx_visual_speed_norm(float speed_kmh)
{
	return speed_kmh * 0.001f;
}

static uint8_t mxt_audio_gx_engine_speed_control(float speed_kmh)
{
	return static_cast<uint8_t>(mxt_audio_clamp_float(speed_kmh * (127.0f / 1500.0f), 0.0f, 127.0f));
}

static uint8_t mxt_audio_gx_engine_base_speed_control(float base_speed)
{
	static constexpr float gx_high_speed_sfx_threshold = 16.933332f;
	return static_cast<uint8_t>(mxt_audio_clamp_float(base_speed * gx_high_speed_sfx_threshold, 1.0f, 127.0f));
}

static uint8_t mxt_audio_step_gx_control(uint8_t current, uint8_t target, uint8_t max_step)
{
	if (current < target) {
		const uint8_t delta = static_cast<uint8_t>(target - current);
		return static_cast<uint8_t>(current + std::min(delta, max_step));
	}
	const uint8_t delta = static_cast<uint8_t>(current - target);
	return static_cast<uint8_t>(current - std::min(delta, max_step));
}

static float mxt_audio_gx_engine_pitch(uint8_t speed_control, const MxtGxEngineTone& tone)
{
	return mxt_audio_eval_musyx_pitch_ratio(speed_control, 0u, tone.pitch, tone.pitch_count);
}

static float mxt_audio_gx_engine_volume(uint8_t speed_control, const MxtGxEngineTone& tone)
{
	return mxt_audio_musyx_volume_to_db(mxt_audio_eval_musyx_curve(speed_control, 0u, tone.volume, tone.volume_count), tone.trim_db);
}

static float mxt_audio_gx_terrain_pitch(uint8_t speed_control)
{
	const float control = static_cast<float>(speed_control) * (1.0f / 127.0f);
	return 0.65f + control * 0.85f;
}

static float mxt_audio_gx_drift_contact_pitch(uint8_t pitch_control)
{
	static constexpr MxtMusyxPitchRatioPoint pitch_curve[1] = { { 0x7f, 0x0b52, 0x01c4 } };
	return mxt_audio_eval_musyx_pitch_ratio(pitch_control, 0u, pitch_curve, 1);
}

static float mxt_audio_gx_drift_contact_volume_db(uint8_t volume_control)
{
	static constexpr MxtMusyxCurvePoint volume_curve[1] = { { 0x7f, 0x40, 0x0000 } };
	const int musyx_volume = mxt_audio_eval_musyx_curve(volume_control, 0u, volume_curve, 1);
	return mxt_audio_musyx_volume_to_db(musyx_volume, -7.0f);
}

static uint8_t mxt_audio_gx_drift_contact_count(const PhysicsCarSoA& soa, int lane)
{
	uint8_t contact_count = 0;
	const int base = lane * 4;
	for (int corner = 0; corner < 4; ++corner) {
		const uint32_t state = soa.tilt_state[base + corner];
		if ((state & TILTSTATE::DRIFT) != 0u) {
			++contact_count;
		}
	}
	return contact_count;
}

static uint8_t mxt_audio_gx_drift_target_control(const PhysicsCarSoA& soa, int lane)
{
	const float forward_x = -soa.basis_physical_c2x[lane];
	const float forward_y = -soa.basis_physical_c2y[lane];
	const float forward_z = -soa.basis_physical_c2z[lane];
	float normal_x = soa.track_surface_normal_x[lane];
	float normal_y = soa.track_surface_normal_y[lane];
	float normal_z = soa.track_surface_normal_z[lane];
	float normal_len_sq = normal_x * normal_x + normal_y * normal_y + normal_z * normal_z;
	if (normal_len_sq <= 0.000001f) {
		normal_x = soa.basis_physical_c1x[lane];
		normal_y = soa.basis_physical_c1y[lane];
		normal_z = soa.basis_physical_c1z[lane];
		normal_len_sq = normal_x * normal_x + normal_y * normal_y + normal_z * normal_z;
		if (normal_len_sq <= 0.000001f) {
			return 0;
		}
	}

	const float normal_inv_len = 1.0f / std::sqrt(normal_len_sq);
	normal_x *= normal_inv_len;
	normal_y *= normal_inv_len;
	normal_z *= normal_inv_len;

	float lateral_x = -(forward_y * normal_z - forward_z * normal_y);
	float lateral_y = -(forward_z * normal_x - forward_x * normal_z);
	float lateral_z = -(forward_x * normal_y - forward_y * normal_x);
	const float lateral_len_sq = lateral_x * lateral_x + lateral_y * lateral_y + lateral_z * lateral_z;
	if (lateral_len_sq <= 0.000001f) {
		return 0;
	}
	const float lateral_inv_len = 1.0f / std::sqrt(lateral_len_sq);
	lateral_x *= lateral_inv_len;
	lateral_y *= lateral_inv_len;
	lateral_z *= lateral_inv_len;

	float control_sum = 0.0f;
	const int base = lane * 4;
	for (int corner = 0; corner < 4; ++corner) {
		const int point = base + corner;
		if ((soa.tilt_state[point] & TILTSTATE::DRIFT) == 0u ||
			(soa.tilt_state[point] & TILTSTATE::AIRBORNE) != 0u) {
			continue;
		}
		const float delta_x = soa.tilt_pos_x[point] - soa.tilt_pos_old_x[point];
		const float delta_y = soa.tilt_pos_y[point] - soa.tilt_pos_old_y[point];
		const float delta_z = soa.tilt_pos_z[point] - soa.tilt_pos_old_z[point];
		const float lateral_delta = delta_x * lateral_x + delta_y * lateral_y + delta_z * lateral_z;
		const bool positive_side_corner = corner == 0 || corner == 2;
		const float same_side_delta = positive_side_corner ? lateral_delta : -lateral_delta;
		if (same_side_delta > 1.0f) {
			control_sum += std::min(same_side_delta - 1.0f, 1.0f);
		}
	}

	const float drift_intensity = 0.5f * control_sum;
	return static_cast<uint8_t>(mxt_audio_clamp_float(127.0f * drift_intensity, 0.0f, 127.0f));
}

static uint8_t mxt_audio_gx_air_tilt_control(float air_tilt)
{
	float control = 30.0f;
	if (air_tilt >= 0.0f) {
		control += 1.6166667f * air_tilt;
	} else {
		control += 0.6f * air_tilt;
	}
	return static_cast<uint8_t>(mxt_audio_clamp_float(control, 0.0f, 127.0f));
}

static uint8_t mxt_audio_gx_collision_tier(float hit_strength)
{
	static constexpr float gx_collision_strength_scale = 0.25f;
	static constexpr float gx_collision_strength_max = 1.0f;
	static constexpr float gx_collision_tier_1 = 0.125f;
	static constexpr float gx_collision_tier_2 = 0.4f;
	static constexpr float gx_collision_tier_3 = 0.75f;

	const float strength = mxt_audio_clamp_float(hit_strength * gx_collision_strength_scale, 0.0f, gx_collision_strength_max);
	if (strength < gx_collision_tier_1) {
		return 0;
	}
	if (strength < gx_collision_tier_2) {
		return 1;
	}
	if (strength < gx_collision_tier_3) {
		return 2;
	}
	return 3;
}

static bool mxt_audio_gx_machine_collision_heavy(float strength)
{
	static constexpr float gx_machine_collision_heavy_threshold = 0.75f;
	return strength > gx_machine_collision_heavy_threshold;
}

static const MxtGxEngineProgram& mxt_audio_select_gx_engine_program(float stat_weight)
{
	static constexpr MxtGxEngineProgram light_program = {
		4,
		{
			{ 189, 2, { { 0x31, 0x32, -51 }, { 0x7f, 0x28, 0x0021 } }, 6, { { 0x11, 0x0824, 0x1b72 }, { 0x2f, 0x0a12, 0x012b }, { 0x3c, 0x0a35, 0x0327 }, { 0x4c, 0x0a5e, 0x0690 }, { 0x66, 0x0b08, 0x018b }, { 0x7f, 0x0b2f, 0x0000 } }, -13.0f },
			{ 190, 3, { { 0x0a, 0x21, 0x038b }, { 0x1c, 0x48, 0x002a }, { 0x7f, 0x4b, 0x000d } }, 2, { { 0x1f, 0x09f2, 0x0cf0 }, { 0x7f, 0x0b8f, 0x03ba } }, -13.0f },
			{ 237, 3, { { 0x10, 0x54, -496 }, { 0x20, 0x33, 0x01b0 }, { 0x7f, 0x4e, 0x0020 } }, 1, { { 0x7f, 0x09f2, 0x050a } }, -13.0f },
			{ 185, 5, { { 0x14, 0x32, -377 }, { 0x4f, 0x13, 0x0008 }, { 0x55, 0x15, 0x0600 }, { 0x63, 0x39, 0x0236 }, { 0x7f, 0x58, 0x012f } }, 1, { { 0x7f, 0x0941, 0x03c8 } }, -13.0f },
		}
	};
	static constexpr MxtGxEngineProgram mid_program = {
		5,
		{
			{ 185, 2, { { 0x58, 0x32, 0x000e }, { 0x7f, 0x37, 0x0172 } }, 3, { { 0x10, 0x0927, 0x0210 }, { 0x19, 0x0949, 0x0d55 }, { 0x7f, 0x09c1, 0x037c } }, -13.0f },
			{ 186, 6, { { 0x16, 0x32, 0x0000 }, { 0x2a, 0x32, 0x0000 }, { 0x3c, 0x32, -28 }, { 0x4d, 0x30, 0x001e }, { 0x62, 0x32, 0x0079 }, { 0x7f, 0x3c, 0x002d } }, 1, { { 0x7f, 0x0952, 0x064d } }, -13.0f },
			{ 187, 1, { { 0x7f, 0x0a, 0x003c } }, 3, { { 0x16, 0x0b43, 0x0b38 }, { 0x48, 0x0c44, -630 }, { 0x7f, 0x0bc9, 0x04e4 } }, -13.0f },
			{ 236, 3, { { 0x51, 0x0a, 0x0006 }, { 0x5c, 0x0c, 0x0674 }, { 0x7f, 0x53, 0x014b } }, 1, { { 0x7f, 0x0a92, 0x03c8 } }, -13.0f },
			{ 240, 1, { { 0x7f, 0x32, 0x0078 } }, 2, { { 0x5e, 0x0b32, 0x027f }, { 0x7f, 0x0c1e, -104 } }, -13.0f },
		}
	};
	static constexpr MxtGxEngineProgram heavy_program = {
		3,
		{
			{ 250, 2, { { 0x21, 0x14, 0x00cb }, { 0x7f, 0x2f, 0x0008 } }, 1, { { 0x7f, 0x0c12, 0x050a } }, -13.0f },
			{ 251, 2, { { 0x15, 0x32, -267 }, { 0x7f, 0x1b, 0x0050 } }, 1, { { 0x7f, 0x09e0, 0x050a } }, -13.0f },
			{ 188, 2, { { 0x0e, 0x00, 0x02aa }, { 0x7f, 0x28, 0x0016 } }, 1, { { 0x7f, 0x0a92, 0x03c8 } }, -13.0f },
		}
	};
	static constexpr float gx_engine_mid_weight_threshold_kg = 1100.0f;
	static constexpr float gx_engine_heavy_weight_threshold_kg = 1900.0f;
	if (stat_weight > gx_engine_heavy_weight_threshold_kg) {
		return heavy_program;
	}
	if (stat_weight > gx_engine_mid_weight_threshold_kg) {
		return mid_program;
	}
	return light_program;
}

static const StringName& mxt_audio_gx_engine_sfx_id(int sample_id)
{
	static const StringName empty;
	static const StringName gx_engine_185("gx_engine_185");
	static const StringName gx_engine_186("gx_engine_186");
	static const StringName gx_engine_187("gx_engine_187");
	static const StringName gx_engine_188("gx_engine_188");
	static const StringName gx_engine_189("gx_engine_189");
	static const StringName gx_engine_190("gx_engine_190");
	static const StringName gx_engine_236("gx_engine_236");
	static const StringName gx_engine_237("gx_engine_237");
	static const StringName gx_engine_240("gx_engine_240");
	static const StringName gx_engine_250("gx_engine_250");
	static const StringName gx_engine_251("gx_engine_251");
	switch (sample_id) {
		case 185: return gx_engine_185;
		case 186: return gx_engine_186;
		case 187: return gx_engine_187;
		case 188: return gx_engine_188;
		case 189: return gx_engine_189;
		case 190: return gx_engine_190;
		case 236: return gx_engine_236;
		case 237: return gx_engine_237;
		case 240: return gx_engine_240;
		case 250: return gx_engine_250;
		case 251: return gx_engine_251;
		default: return empty;
	}
}
}

MxtSpatialAudioManager::MxtSpatialAudioManager()
{
	announcer_queue.reserve(static_cast<size_t>(max_announcer_queue));
	final_lap_music_timestamps.reserve(16);
	set_process(true);
}

MxtSpatialAudioManager::~MxtSpatialAudioManager() = default;

void MxtSpatialAudioManager::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure", "vehicle_emitter_count", "world_emitter_count", "vehicle_polyphony", "world_polyphony"), &MxtSpatialAudioManager::configure, DEFVAL(30), DEFVAL(16), DEFVAL(16), DEFVAL(8));
	ClassDB::bind_method(D_METHOD("register_sfx", "id", "path"), &MxtSpatialAudioManager::register_sfx);
	ClassDB::bind_method(D_METHOD("has_sfx", "id"), &MxtSpatialAudioManager::has_sfx);
	ClassDB::bind_method(D_METHOD("clear_sfx"), &MxtSpatialAudioManager::clear_sfx);
	ClassDB::bind_method(D_METHOD("set_audio_bus", "bus"), &MxtSpatialAudioManager::set_audio_bus);
	ClassDB::bind_method(D_METHOD("get_audio_bus"), &MxtSpatialAudioManager::get_audio_bus);
	ClassDB::bind_method(D_METHOD("set_remote_vehicle_audio_bus", "bus"), &MxtSpatialAudioManager::set_remote_vehicle_audio_bus);
	ClassDB::bind_method(D_METHOD("get_remote_vehicle_audio_bus"), &MxtSpatialAudioManager::get_remote_vehicle_audio_bus);
	ClassDB::bind_method(D_METHOD("set_music_bus", "bus"), &MxtSpatialAudioManager::set_music_bus);
	ClassDB::bind_method(D_METHOD("get_music_bus"), &MxtSpatialAudioManager::get_music_bus);
	ClassDB::bind_method(D_METHOD("set_announcer_bus", "bus"), &MxtSpatialAudioManager::set_announcer_bus);
	ClassDB::bind_method(D_METHOD("get_announcer_bus"), &MxtSpatialAudioManager::get_announcer_bus);
	ClassDB::bind_method(D_METHOD("set_music_volume_db", "volume_db"), &MxtSpatialAudioManager::set_music_volume_db);
	ClassDB::bind_method(D_METHOD("get_music_volume_db"), &MxtSpatialAudioManager::get_music_volume_db);
	ClassDB::bind_method(D_METHOD("set_announcer_volume_db", "volume_db"), &MxtSpatialAudioManager::set_announcer_volume_db);
	ClassDB::bind_method(D_METHOD("get_announcer_volume_db"), &MxtSpatialAudioManager::get_announcer_volume_db);
	ClassDB::bind_method(D_METHOD("set_max_announcer_queue", "max_queue"), &MxtSpatialAudioManager::set_max_announcer_queue);
	ClassDB::bind_method(D_METHOD("get_max_announcer_queue"), &MxtSpatialAudioManager::get_max_announcer_queue);
	ClassDB::bind_method(D_METHOD("set_reassignment_fade_seconds", "seconds"), &MxtSpatialAudioManager::set_reassignment_fade_seconds);
	ClassDB::bind_method(D_METHOD("get_reassignment_fade_seconds"), &MxtSpatialAudioManager::get_reassignment_fade_seconds);
	ClassDB::bind_method(D_METHOD("set_vehicle_max_distance", "distance"), &MxtSpatialAudioManager::set_vehicle_max_distance);
	ClassDB::bind_method(D_METHOD("get_vehicle_max_distance"), &MxtSpatialAudioManager::get_vehicle_max_distance);
	ClassDB::bind_method(D_METHOD("play_music_paths", "loop_path", "intro_path", "final_loop_path", "final_intro_path", "final_lap_timestamps"), &MxtSpatialAudioManager::play_music_paths, DEFVAL(String()), DEFVAL(String()), DEFVAL(String()), DEFVAL(PackedFloat32Array()));
	ClassDB::bind_method(D_METHOD("stop_music", "fade_seconds"), &MxtSpatialAudioManager::stop_music, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("request_final_lap_music"), &MxtSpatialAudioManager::request_final_lap_music);
	ClassDB::bind_method(D_METHOD("is_final_lap_music_active"), &MxtSpatialAudioManager::is_final_lap_music_active);
	ClassDB::bind_method(D_METHOD("get_music_playback_position"), &MxtSpatialAudioManager::get_music_playback_position);
	ClassDB::bind_method(D_METHOD("process_global_audio", "delta"), &MxtSpatialAudioManager::process_global_audio);
	ClassDB::bind_method(D_METHOD("queue_announcer", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::queue_announcer, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("play_announcer_now", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::play_announcer_now, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("clear_announcer_queue"), &MxtSpatialAudioManager::clear_announcer_queue);
	ClassDB::bind_method(D_METHOD("get_announcer_queue_size"), &MxtSpatialAudioManager::get_announcer_queue_size);
	ClassDB::bind_method(D_METHOD("update_from_gamesim", "game_sim", "local_player_id", "delta", "update_assignments"), &MxtSpatialAudioManager::update_from_gamesim, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("set_vehicle_manual_boost_sfx", "car_index", "sfx_id"), &MxtSpatialAudioManager::set_vehicle_manual_boost_sfx);
	ClassDB::bind_method(D_METHOD("play_vehicle_oneshot", "car_index", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::play_vehicle_oneshot, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("set_vehicle_loop", "car_index", "key", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::set_vehicle_loop, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("stop_vehicle_loop", "car_index", "key"), &MxtSpatialAudioManager::stop_vehicle_loop);
	ClassDB::bind_method(D_METHOD("play_world_oneshot", "position", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::play_world_oneshot, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("clear_all"), &MxtSpatialAudioManager::clear_all);
	ClassDB::bind_method(D_METHOD("get_assigned_vehicle_count"), &MxtSpatialAudioManager::get_assigned_vehicle_count);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "audio_bus"), "set_audio_bus", "get_audio_bus");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "remote_vehicle_audio_bus"), "set_remote_vehicle_audio_bus", "get_remote_vehicle_audio_bus");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "music_bus"), "set_music_bus", "get_music_bus");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "announcer_bus"), "set_announcer_bus", "get_announcer_bus");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "music_volume_db"), "set_music_volume_db", "get_music_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "announcer_volume_db"), "set_announcer_volume_db", "get_announcer_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_announcer_queue"), "set_max_announcer_queue", "get_max_announcer_queue");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reassignment_fade_seconds"), "set_reassignment_fade_seconds", "get_reassignment_fade_seconds");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vehicle_max_distance"), "set_vehicle_max_distance", "get_vehicle_max_distance");
}

void MxtSpatialAudioManager::_notification(int p_what)
{
	if (p_what == NOTIFICATION_READY) {
		ensure_global_players();
	} else if (p_what == NOTIFICATION_PROCESS) {
		process_global_audio(get_process_delta_time());
	}
}

float MxtSpatialAudioManager::clamped_pitch(float pitch_scale)
{
	if (!std::isfinite(pitch_scale)) {
		return 1.0f;
	}
	return std::max(0.05f, std::min(4.0f, pitch_scale));
}

float MxtSpatialAudioManager::fade_volume_db(float base_volume_db, float fade_ratio)
{
	if (fade_ratio <= 0.001f) {
		return MXT_AUDIO_SILENCE_DB;
	}
	return std::max(MXT_AUDIO_SILENCE_DB, base_volume_db + 20.0f * std::log10(fade_ratio));
}

Ref<AudioStream> MxtSpatialAudioManager::load_audio_stream(const String& path)
{
	if (path.is_empty()) {
		return Ref<AudioStream>();
	}
	ResourceLoader* loader = ResourceLoader::get_singleton();
	if (!loader) {
		return Ref<AudioStream>();
	}
	Ref<Resource> resource = loader->load(path, "AudioStream");
	Ref<AudioStream> stream = resource;
	if (stream.is_null()) {
		UtilityFunctions::push_warning(String("MXT audio failed to load stream: ") + path);
	}
	return stream;
}

Ref<AudioStream> MxtSpatialAudioManager::find_sfx(const StringName& id) const
{
	for (const SfxEntry& entry : sfx_entries) {
		if (entry.id == id) {
			return entry.stream;
		}
	}
	return Ref<AudioStream>();
}

void MxtSpatialAudioManager::ensure_global_players()
{
	if (!music_player) {
		music_player = memnew(AudioStreamPlayer);
		music_player->set_name("MusicPlayer");
		music_player->set_bus(music_bus);
		music_player->set_volume_db(music_volume_db);
		add_child(music_player);
	}
	if (!announcer_player) {
		announcer_player = memnew(AudioStreamPlayer);
		announcer_player->set_name("AnnouncerPlayer");
		announcer_player->set_bus(announcer_bus);
		announcer_player->set_volume_db(announcer_volume_db);
		add_child(announcer_player);
	}
}

void MxtSpatialAudioManager::clear_emitters(std::vector<Emitter>& emitters)
{
	for (Emitter& emitter : emitters) {
		if (emitter.player) {
			stop_all_streams(emitter);
			if (emitter.player->get_parent() == this) {
				remove_child(emitter.player);
			}
			emitter.player->queue_free();
			emitter.player = nullptr;
		}
		emitter.playback.unref();
		emitter.stream.unref();
		emitter.active_streams.clear();
		emitter.loop_streams.clear();
	}
	emitters.clear();
}

void MxtSpatialAudioManager::create_emitters(std::vector<Emitter>& emitters, int count, int polyphony, const char* name_prefix, float unit_size)
{
	if (count <= 0) {
		return;
	}
	emitters.resize(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		Emitter& emitter = emitters[static_cast<size_t>(i)];
		emitter.stream.instantiate();
		emitter.stream->set_polyphony(polyphony);

		AudioStreamPlayer3D* player = memnew(AudioStreamPlayer3D);
		player->set_name(String(name_prefix) + String::num_int64(i));
		player->set_stream(emitter.stream);
		player->set_max_polyphony(polyphony);
		player->set_bus(audio_bus);
		player->set_unit_size(unit_size);
		player->set_max_distance(2200.0);
		player->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
		player->set_doppler_tracking(AudioStreamPlayer3D::DOPPLER_TRACKING_DISABLED);
		add_child(player);
		player->play();

		emitter.player = player;
		refresh_playback(emitter);
		emitter.active_streams.reserve(static_cast<size_t>(polyphony));
		emitter.loop_streams.reserve(12);
	}
}

void MxtSpatialAudioManager::refresh_playback(Emitter& emitter)
{
	if (!emitter.player || emitter.playback.is_valid()) {
		return;
	}
	Ref<AudioStreamPlayback> playback = emitter.player->get_stream_playback();
	emitter.playback = playback;
}

void MxtSpatialAudioManager::prune_stopped_streams(Emitter& emitter)
{
	refresh_playback(emitter);
	if (emitter.playback.is_null()) {
		emitter.active_streams.clear();
		return;
	}
	size_t write = 0;
	for (size_t i = 0; i < emitter.active_streams.size(); ++i) {
		const ActiveStream& active = emitter.active_streams[i];
		if (active.id != AudioStreamPlaybackPolyphonic::INVALID_ID && emitter.playback->is_stream_playing(active.id)) {
			emitter.active_streams[write++] = active;
		}
	}
	emitter.active_streams.resize(write);

}

void MxtSpatialAudioManager::apply_emitter_attenuation(Emitter& emitter, bool local_vehicle)
{
	const AudioStreamPlayer3D::AttenuationModel attenuation_model =
		local_vehicle ? AudioStreamPlayer3D::ATTENUATION_DISABLED : AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE;
	if (emitter.player) {
		emitter.player->set_attenuation_model(attenuation_model);
	}
}

void MxtSpatialAudioManager::apply_emitter_bus(Emitter& emitter)
{
	if (emitter.player) {
		emitter.player->set_bus(bus_for_emitter(emitter));
	}
}

bool MxtSpatialAudioManager::is_local_vehicle_emitter(const Emitter& emitter) const
{
	return local_vehicle_car_index >= 0 && emitter.car_index == local_vehicle_car_index;
}

StringName MxtSpatialAudioManager::bus_for_emitter(const Emitter& emitter) const
{
	if (emitter.car_index >= 0 && !is_local_vehicle_emitter(emitter)) {
		return remote_vehicle_audio_bus == StringName() ? audio_bus : remote_vehicle_audio_bus;
	}
	return audio_bus;
}

float MxtSpatialAudioManager::effective_emitter_volume_db(const Emitter& emitter, float volume_db) const
{
	return is_local_vehicle_emitter(emitter) ? volume_db + MXT_LOCAL_VEHICLE_VOLUME_GAIN_DB : volume_db;
}

void MxtSpatialAudioManager::stop_all_streams(Emitter& emitter)
{
	refresh_playback(emitter);
	if (emitter.playback.is_valid()) {
		for (const ActiveStream& active : emitter.active_streams) {
			if (active.id != AudioStreamPlaybackPolyphonic::INVALID_ID) {
				emitter.playback->stop_stream(active.id);
			}
		}
		for (const LoopStream& loop : emitter.loop_streams) {
			if (loop.id != AudioStreamPlaybackPolyphonic::INVALID_ID) {
				emitter.playback->stop_stream(loop.id);
			}
		}
	}
	emitter.active_streams.clear();
	emitter.loop_streams.clear();
}

int MxtSpatialAudioManager::find_loop_stream(const Emitter& emitter, const StringName& key) const
{
	for (int i = 0; i < static_cast<int>(emitter.loop_streams.size()); ++i) {
		if (emitter.loop_streams[static_cast<size_t>(i)].key == key) {
			return i;
		}
	}
	return -1;
}

bool MxtSpatialAudioManager::set_loop_on_emitter(Emitter& emitter, const StringName& key, const StringName& sfx_id, float volume_db, float pitch_scale)
{
	if (key == StringName() || sfx_id == StringName()) {
		return false;
	}
	Ref<AudioStream> stream = find_sfx(sfx_id);
	if (stream.is_null()) {
		return false;
	}
	if (!emitter.player) {
		return false;
	}
	refresh_playback(emitter);
	if (emitter.playback.is_null()) {
		return false;
	}

	const float next_volume_db = mxt_audio_clamp_float(volume_db, -80.0f, 48.0f);
	const float next_pitch_scale = clamped_pitch(pitch_scale);
	const int loop_index = find_loop_stream(emitter, key);
	if (loop_index >= 0) {
		LoopStream& loop = emitter.loop_streams[static_cast<size_t>(loop_index)];
		if (loop.sfx_id == sfx_id) {
			loop.volume_db = next_volume_db;
			loop.pitch_scale = next_pitch_scale;
			if (loop.id != AudioStreamPlaybackPolyphonic::INVALID_ID && emitter.playback->is_stream_playing(loop.id)) {
				emitter.playback->set_stream_volume(loop.id, effective_emitter_volume_db(emitter, next_volume_db));
				emitter.playback->set_stream_pitch_scale(loop.id, next_pitch_scale);
			} else {
				loop.id = emitter.playback->play_stream(stream, 0.0, effective_emitter_volume_db(emitter, next_volume_db), next_pitch_scale, AudioServer::PLAYBACK_TYPE_DEFAULT, bus_for_emitter(emitter));
				if (loop.id == AudioStreamPlaybackPolyphonic::INVALID_ID) {
					return false;
				}
			}
			return true;
		}
		if (loop.id != AudioStreamPlaybackPolyphonic::INVALID_ID) {
			emitter.playback->stop_stream(loop.id);
		}
		loop.id = emitter.playback->play_stream(stream, 0.0, effective_emitter_volume_db(emitter, next_volume_db), next_pitch_scale, AudioServer::PLAYBACK_TYPE_DEFAULT, bus_for_emitter(emitter));
		if (loop.id == AudioStreamPlaybackPolyphonic::INVALID_ID) {
			return false;
		}
		loop.sfx_id = sfx_id;
		loop.volume_db = next_volume_db;
		loop.pitch_scale = next_pitch_scale;
		return true;
	}

	LoopStream loop;
	loop.key = key;
	loop.sfx_id = sfx_id;
	loop.volume_db = next_volume_db;
	loop.pitch_scale = next_pitch_scale;
	loop.id = emitter.playback->play_stream(stream, 0.0, effective_emitter_volume_db(emitter, next_volume_db), next_pitch_scale, AudioServer::PLAYBACK_TYPE_DEFAULT, bus_for_emitter(emitter));
	if (loop.id == AudioStreamPlaybackPolyphonic::INVALID_ID) {
		return false;
	}
	emitter.loop_streams.push_back(loop);
	return true;
}

bool MxtSpatialAudioManager::stop_loop_on_emitter(Emitter& emitter, const StringName& key)
{
	const int loop_index = find_loop_stream(emitter, key);
	if (loop_index < 0) {
		return false;
	}
	LoopStream& loop = emitter.loop_streams[static_cast<size_t>(loop_index)];
	refresh_playback(emitter);
	if (emitter.playback.is_valid() && loop.id != AudioStreamPlaybackPolyphonic::INVALID_ID) {
		emitter.playback->stop_stream(loop.id);
	}
	const size_t last = emitter.loop_streams.size() - 1;
	emitter.loop_streams[static_cast<size_t>(loop_index)] = emitter.loop_streams[last];
	emitter.loop_streams.pop_back();
	return true;
}

void MxtSpatialAudioManager::set_emitter_position(Emitter& emitter, const Vector3& position)
{
	if (emitter.player) {
		emitter.player->set_global_position(position);
	}
}

void MxtSpatialAudioManager::set_emitter_transform(Emitter& emitter, const Transform3D& transform)
{
	if (emitter.player) {
		emitter.player->set_global_transform(transform);
	}
}

void MxtSpatialAudioManager::assign_vehicle_emitter(Emitter& emitter, int car_index, const Transform3D& transform)
{
	emitter.car_index = car_index;
	emitter.pending_car_index = -1;
	emitter.pending_sound.active = false;
	emitter.fade_remaining = 0.0f;
	emitter.fade_total = 0.0f;
	set_emitter_transform(emitter, transform);
	apply_emitter_bus(emitter);
}

void MxtSpatialAudioManager::assign_world_emitter(Emitter& emitter, const Vector3& position)
{
	emitter.car_index = -1;
	emitter.pending_car_index = -1;
	emitter.fade_remaining = 0.0f;
	emitter.fade_total = 0.0f;
	set_emitter_position(emitter, position);
	apply_emitter_bus(emitter);
}

void MxtSpatialAudioManager::begin_vehicle_reassignment(Emitter& emitter, int car_index, const Transform3D& transform)
{
	prune_stopped_streams(emitter);
	if (emitter.active_streams.empty() && emitter.loop_streams.empty()) {
		assign_vehicle_emitter(emitter, car_index, transform);
		return;
	}
	emitter.pending_car_index = car_index;
	emitter.pending_transform = transform;
	emitter.pending_sound.active = false;
	emitter.fade_total = std::max(MXT_AUDIO_MIN_FADE_SECONDS, reassignment_fade_seconds);
	emitter.fade_remaining = emitter.fade_total;
}

void MxtSpatialAudioManager::begin_world_reassignment(Emitter& emitter, const Vector3& position, const StringName& sfx_id, float volume_db, float pitch_scale)
{
	prune_stopped_streams(emitter);
	if (emitter.active_streams.empty() && emitter.loop_streams.empty()) {
		assign_world_emitter(emitter, position);
		play_on_emitter(emitter, sfx_id, volume_db, pitch_scale);
		return;
	}
	emitter.pending_world_position = position;
	emitter.pending_sound.active = true;
	emitter.pending_sound.sfx_id = sfx_id;
	emitter.pending_sound.volume_db = volume_db;
	emitter.pending_sound.pitch_scale = pitch_scale;
	emitter.fade_total = std::max(MXT_AUDIO_MIN_FADE_SECONDS, reassignment_fade_seconds);
	emitter.fade_remaining = emitter.fade_total;
}

void MxtSpatialAudioManager::advance_fade(Emitter& emitter, double delta, bool vehicle_emitter)
{
	if (emitter.fade_remaining <= 0.0f) {
		return;
	}
	emitter.fade_remaining -= static_cast<float>(std::max(0.0, delta));
	const float fade_ratio = std::max(0.0f, emitter.fade_remaining / std::max(MXT_AUDIO_MIN_FADE_SECONDS, emitter.fade_total));
	refresh_playback(emitter);
	if (emitter.playback.is_valid()) {
		for (const ActiveStream& active : emitter.active_streams) {
			if (active.id != AudioStreamPlaybackPolyphonic::INVALID_ID && emitter.playback->is_stream_playing(active.id)) {
				emitter.playback->set_stream_volume(active.id, fade_volume_db(effective_emitter_volume_db(emitter, active.volume_db), fade_ratio));
			}
		}
		for (const LoopStream& loop : emitter.loop_streams) {
			if (loop.id != AudioStreamPlaybackPolyphonic::INVALID_ID && emitter.playback->is_stream_playing(loop.id)) {
				emitter.playback->set_stream_volume(loop.id, fade_volume_db(effective_emitter_volume_db(emitter, loop.volume_db), fade_ratio));
			}
		}
	}
	if (emitter.fade_remaining > 0.0f) {
		return;
	}

	stop_all_streams(emitter);
	emitter.fade_remaining = 0.0f;
	emitter.fade_total = 0.0f;
	if (vehicle_emitter) {
		if (emitter.pending_car_index >= 0) {
			assign_vehicle_emitter(emitter, emitter.pending_car_index, emitter.pending_transform);
		} else {
			emitter.car_index = -1;
		}
	} else {
		assign_world_emitter(emitter, emitter.pending_world_position);
		if (emitter.pending_sound.active) {
			const PendingSound pending = emitter.pending_sound;
			emitter.pending_sound.active = false;
			play_on_emitter(emitter, pending.sfx_id, pending.volume_db, pending.pitch_scale);
		}
	}
}

bool MxtSpatialAudioManager::play_on_emitter(Emitter& emitter, const StringName& sfx_id, float volume_db, float pitch_scale)
{
	Ref<AudioStream> stream = find_sfx(sfx_id);
	if (stream.is_null()) {
		return false;
	}
	refresh_playback(emitter);
	if (emitter.playback.is_null()) {
		return false;
	}
	const int64_t stream_id = emitter.playback->play_stream(stream, 0.0, effective_emitter_volume_db(emitter, volume_db), clamped_pitch(pitch_scale), AudioServer::PLAYBACK_TYPE_DEFAULT, bus_for_emitter(emitter));
	if (stream_id == AudioStreamPlaybackPolyphonic::INVALID_ID) {
		return false;
	}
	ActiveStream active;
	active.id = stream_id;
	active.volume_db = volume_db;
	emitter.active_streams.push_back(active);
	return true;
}

int MxtSpatialAudioManager::find_vehicle_emitter_for_car(int car_index) const
{
	for (int i = 0; i < static_cast<int>(vehicle_emitters.size()); ++i) {
		const Emitter& emitter = vehicle_emitters[static_cast<size_t>(i)];
		if (emitter.car_index == car_index && emitter.fade_remaining <= 0.0f) {
			return i;
		}
	}
	return -1;
}

int MxtSpatialAudioManager::find_idle_vehicle_emitter() const
{
	for (int i = 1; i < static_cast<int>(vehicle_emitters.size()); ++i) {
		const Emitter& emitter = vehicle_emitters[static_cast<size_t>(i)];
		if (emitter.car_index < 0 && emitter.fade_remaining <= 0.0f) {
			return i;
		}
	}
	return -1;
}

int MxtSpatialAudioManager::find_idle_world_emitter() const
{
	for (int i = 0; i < static_cast<int>(world_emitters.size()); ++i) {
		const Emitter& emitter = world_emitters[static_cast<size_t>(i)];
		if (emitter.fade_remaining <= 0.0f && emitter.active_streams.empty() && emitter.loop_streams.empty()) {
			return i;
		}
	}
	return -1;
}

int MxtSpatialAudioManager::find_reusable_world_emitter(const Vector3& position) const
{
	for (int i = 0; i < static_cast<int>(world_emitters.size()); ++i) {
		const Emitter& emitter = world_emitters[static_cast<size_t>(i)];
		if (!emitter.player || emitter.fade_remaining > 0.0f) {
			continue;
		}
		if (emitter.player->get_global_position().distance_squared_to(position) <= world_reuse_distance_sq) {
			return i;
		}
	}
	return -1;
}

int MxtSpatialAudioManager::find_world_steal_emitter() const
{
	int best = -1;
	size_t best_count = SIZE_MAX;
	for (int i = 0; i < static_cast<int>(world_emitters.size()); ++i) {
		const Emitter& emitter = world_emitters[static_cast<size_t>(i)];
		if (emitter.fade_remaining > 0.0f) {
			continue;
		}
		if (emitter.active_streams.size() < best_count) {
			best = i;
			best_count = emitter.active_streams.size();
		}
	}
	return best;
}

void MxtSpatialAudioManager::collect_vehicle_candidates(GameSim* sim)
{
	vehicle_candidates.clear();
	if (!sim || !sim->sim_started || sim->num_cars <= 0 || vehicle_emitters.empty()) {
		return;
	}

	Vector3 local_origin;
	if (local_vehicle_car_index >= 0 && local_vehicle_car_index < sim->num_cars) {
		local_origin = sim->get_car_render_transform(local_vehicle_car_index).origin;
	} else if (sim->num_cars > 0) {
		local_origin = sim->get_car_render_transform(0).origin;
	}
	const float max_distance_sq = vehicle_max_distance > 0.0f ? vehicle_max_distance * vehicle_max_distance : 0.0f;
	for (int car_index = 0; car_index < sim->num_cars; ++car_index) {
		if (car_index == local_vehicle_car_index) {
			continue;
		}
		const Transform3D transform = sim->get_car_render_transform(car_index);
		const float dist_sq = static_cast<float>(local_origin.distance_squared_to(transform.origin));
		if (max_distance_sq > 0.0f && dist_sq > max_distance_sq) {
			continue;
		}
		VehicleCandidate candidate;
		candidate.car_index = car_index;
		candidate.distance_sq = dist_sq;
		candidate.transform = transform;

		size_t insert_at = 0;
		while (insert_at < vehicle_candidates.size() && vehicle_candidates[insert_at].distance_sq <= dist_sq) {
			++insert_at;
		}
		vehicle_candidates.push_back(candidate);
		if (insert_at < vehicle_candidates.size()) {
			for (size_t i = vehicle_candidates.size() - 1; i > insert_at; --i) {
				vehicle_candidates[i] = vehicle_candidates[i - 1];
			}
			vehicle_candidates[insert_at] = candidate;
		}
	}
}

void MxtSpatialAudioManager::assign_vehicle_candidates()
{
	for (int slot = 1; slot < static_cast<int>(vehicle_emitters.size()); ++slot) {
		Emitter& emitter = vehicle_emitters[static_cast<size_t>(slot)];
		if (emitter.fade_remaining > 0.0f || emitter.car_index < 0) {
			continue;
		}
		int candidate_index = -1;
		for (int c = 0; c < static_cast<int>(vehicle_candidates.size()); ++c) {
			if (vehicle_candidates[static_cast<size_t>(c)].car_index == emitter.car_index) {
				candidate_index = c;
				break;
			}
		}
		if (candidate_index >= 0) {
			set_emitter_transform(emitter, vehicle_candidates[static_cast<size_t>(candidate_index)].transform);
		} else {
			begin_vehicle_reassignment(emitter, -1, Transform3D());
		}
	}

	for (size_t c = 0; c < vehicle_candidates.size(); ++c) {
		const VehicleCandidate& candidate = vehicle_candidates[c];
		if (find_vehicle_emitter_for_car(candidate.car_index) >= 0) {
			continue;
		}
		int slot = find_idle_vehicle_emitter();
		if (slot >= 0) {
			begin_vehicle_reassignment(vehicle_emitters[static_cast<size_t>(slot)], candidate.car_index, candidate.transform);
			continue;
		}

		slot = -1;
		float furthest_distance_sq = -1.0f;
		for (int remote_slot = 1; remote_slot < static_cast<int>(vehicle_emitters.size()); ++remote_slot) {
			const Emitter& emitter = vehicle_emitters[static_cast<size_t>(remote_slot)];
			if (emitter.fade_remaining > 0.0f || emitter.car_index < 0) {
				continue;
			}
			for (int assigned_candidate_index = 0; assigned_candidate_index < static_cast<int>(vehicle_candidates.size()); ++assigned_candidate_index) {
				if (vehicle_candidates[static_cast<size_t>(assigned_candidate_index)].car_index != emitter.car_index) {
					continue;
				}
				const float assigned_distance_sq = vehicle_candidates[static_cast<size_t>(assigned_candidate_index)].distance_sq;
				if (assigned_distance_sq > furthest_distance_sq) {
					furthest_distance_sq = assigned_distance_sq;
					slot = remote_slot;
				}
				break;
			}
		}
		if (slot < 0) {
			break;
		}
		if (candidate.distance_sq >= furthest_distance_sq) {
			break;
		}
		begin_vehicle_reassignment(vehicle_emitters[static_cast<size_t>(slot)], candidate.car_index, candidate.transform);
	}
}

void MxtSpatialAudioManager::update_vehicle_loop_audio(GameSim* sim, double delta, bool step_events)
{
	static const StringName energy_key("energy_restore");
	static const StringName energy_sfx("energy_restore");
	static const StringName air_keys[2] = { StringName("air_0"), StringName("air_1") };
	static const StringName air_sfx[2] = { StringName("air_0"), StringName("air_1") };
	static const StringName brake_key("brake");
	static const StringName brake_sfx("brake");
	static const StringName drift_loop_key("drift_loop");
	static const StringName drift_loop_sfx("drift_loop");
	static const StringName terrain_dirt_key("terrain_dirt");
	static const StringName terrain_lava_key("terrain_lava");
	static const StringName terrain_lava_secondary_key("terrain_lava_secondary");
	static const StringName terrain_dirt_sfx("terrain_dirt");
	static const StringName terrain_lava_sfx("terrain_lava");
	static const StringName terrain_lava_secondary_sfx("terrain_lava_secondary");
	static const StringName dash_plate_sfx("dash_plate");
	static const StringName dash_plate_secondary_sfx("dash_plate_secondary");
	static const StringName jump_plate_sfx("jump_plate");
	static const StringName landing_sfx("landing");
	static const StringName landing_b10_sfx("landing_b10");
	static const StringName mine_sfx("mine");
	static const StringName collision_light_secondary_sfx("collision_light_secondary");
	static const StringName machine_collision_medium_sfx("collision_medium");
	static const StringName machine_collision_heavy_sfx("collision_heavy");
	static const StringName collision_sfx[4] = {
		StringName("collision_light"),
		StringName("collision_medium"),
		StringName("collision_hard"),
		StringName("collision_heavy"),
	};
	static const StringName spin_sfx("spinattack");
	static const StringName strafe_sfx("strafe");
	static const StringName sideattack_sfx("sideattack");
	static const StringName active_start_sfx("active_start");
	static const StringName thrust_on_sfx("thrust_on");
	static const StringName zero_hp_sfx("zero_hp");
	static const StringName gx_engine_keys[5] = {
		StringName("gx_engine_0"),
		StringName("gx_engine_1"),
		StringName("gx_engine_2"),
		StringName("gx_engine_3"),
		StringName("gx_engine_4"),
	};
	static constexpr MxtMusyxCurvePoint air_pitch_lookup_curves[2][2] = {
		{ { 0x39, 0x5b, 0x004f }, { 0x7f, 0x6d, 0x0042 } },
		{ { 0x4e, 0x64, 0x0047 }, { 0x7f, 0x7a, 0x001a } },
	};
	static constexpr MxtMusyxPitchRatioPoint air_pitch_ratio_curves[2][1] = {
		{ { 0x7f, 0x0cb2, 0x01c4 } },
		{ { 0x7f, 0x0b32, 0x0453 } },
	};
	static constexpr float terrain_layer_volume_db = -12.0f;
	static constexpr float engine_layer_volume_gain_db = 5.0f;
	static constexpr float drift_layer_volume_gain_db = 2.0f;
	static constexpr uint32_t terrain_hit_mine = 0x40000000u;
	static constexpr float gx_strafe_min_speed_kmh = 100.0f;
	static constexpr float gx_jump_plate_min_speed_kmh = 850.0f;

	if (!sim || !sim->cars || sim->num_cars <= 0) {
		return;
	}
	if (static_cast<int>(vehicle_loop_states.size()) < sim->num_cars) {
		vehicle_loop_states.resize(static_cast<size_t>(sim->num_cars));
	}

	const float delta_f = mxt_audio_clamp_float(static_cast<float>(delta), 0.0f, 1.0f);
	const float pitstop_lerp_weight = std::min(1.0f, delta_f * 10.0f);
	const float vehicle_loop_lerp_weight = std::min(1.0f, delta_f * 12.0f);

	for (Emitter& emitter : vehicle_emitters) {
		if (emitter.fade_remaining > 0.0f || emitter.car_index < 0 || emitter.car_index >= sim->num_cars) {
			continue;
		}

		PhysicsCar& car = sim->cars[emitter.car_index];
		if (!car.soa) {
			continue;
		}
		PhysicsCarSoA& soa = *car.soa;
		const int lane = car.soa_index;
		VehicleLoopState& loop_state = vehicle_loop_states[static_cast<size_t>(emitter.car_index)];
		const uint32_t machine_state = soa.machine_state[lane];
		const uint32_t terrain_state = soa.terrain_state[lane];
		const bool car_audible =
			(machine_state & (MACHINESTATE::ZEROHP | MACHINESTATE::FALLOUT | MACHINESTATE::RETIRED)) == 0u;
		const uint8_t drift_contact_count = mxt_audio_gx_drift_contact_count(soa, lane);
		const bool drift_contact_active = car_audible && drift_contact_count != 0u;

		if (step_events) {
			if (!loop_state.event_state_initialized) {
				loop_state.previous_machine_state = machine_state;
				loop_state.previous_terrain_state = terrain_state;
				loop_state.previous_manual_boost_tick = soa.last_manual_boost_tick[lane];
				loop_state.previous_manual_boost_initialized = soa.has_last_manual_boost_tick[lane];
				loop_state.previous_last_hit_tick = soa.last_hit_tick[lane];
				loop_state.previous_last_hit_initialized = soa.has_last_hit_tick[lane];
				loop_state.previous_last_machine_hit_tick = soa.last_machine_hit_tick[lane];
				loop_state.previous_last_machine_hit_initialized = soa.has_last_machine_hit_tick[lane];
				loop_state.previous_strafe_roll_active = false;
				loop_state.event_state_initialized = true;
			} else {
				const uint32_t rising_machine_state = machine_state & ~loop_state.previous_machine_state;
				const uint32_t rising_terrain_state = terrain_state & ~loop_state.previous_terrain_state;
				const bool landing_active =
					car_audible &&
					(machine_state & MACHINESTATE::JUSTLANDED) != 0u &&
					soa.speed_kmh[lane] > 5.0f;
				if (landing_active) {
					const StringName& selected_landing_sfx =
						((machine_state & MACHINESTATE::B10) != 0u) ? landing_b10_sfx : landing_sfx;
					play_on_emitter(emitter, selected_landing_sfx, 0.0f, 1.0f);
				}
				if ((rising_terrain_state & terrain_hit_mine) != 0u) {
					play_on_emitter(emitter, mine_sfx, 0.0f, 1.0f);
				}
				if (car_audible && soa.has_last_hit_tick[lane] &&
					(!loop_state.previous_last_hit_initialized ||
						soa.last_hit_tick[lane] != loop_state.previous_last_hit_tick)) {
					const uint8_t collision_tier = mxt_audio_gx_collision_tier(soa.last_hit_sfx_strength[lane]);
					play_on_emitter(emitter, collision_sfx[collision_tier], 0.0f, 1.0f);
					if (collision_tier == 0u) {
						play_on_emitter(emitter, collision_light_secondary_sfx, 0.0f, 1.0f);
					}
				}
				if (car_audible && soa.has_last_machine_hit_tick[lane] &&
					(!loop_state.previous_last_machine_hit_initialized ||
						soa.last_machine_hit_tick[lane] != loop_state.previous_last_machine_hit_tick)) {
					const StringName& selected_machine_hit_sfx =
						mxt_audio_gx_machine_collision_heavy(soa.last_machine_hit_sfx_strength[lane]) ?
						machine_collision_heavy_sfx : machine_collision_medium_sfx;
					play_on_emitter(emitter, selected_machine_hit_sfx, 0.0f, 1.0f);
				}
				if (car_audible && soa.has_last_manual_boost_tick[lane] &&
					(!loop_state.previous_manual_boost_initialized ||
						soa.last_manual_boost_tick[lane] != loop_state.previous_manual_boost_tick)) {
					const size_t boost_sfx_index = static_cast<size_t>(emitter.car_index);
					if (boost_sfx_index < vehicle_manual_boost_sfx.size() &&
						!String(vehicle_manual_boost_sfx[boost_sfx_index]).is_empty()) {
						play_on_emitter(emitter, vehicle_manual_boost_sfx[boost_sfx_index], -3.0f, 1.0f);
					}
				}
				if (car_audible && (rising_machine_state & MACHINESTATE::JUST_HIT_DASHPLATE) != 0u) {
					play_on_emitter(emitter, dash_plate_secondary_sfx, -3.0f, 1.0f);
					play_on_emitter(emitter, dash_plate_sfx, -3.0f, 1.0f);
				}
				if (car_audible && (rising_terrain_state & TERRAIN::JUMP) != 0u && soa.speed_kmh[lane] > gx_jump_plate_min_speed_kmh) {
					play_on_emitter(emitter, jump_plate_sfx, 0.0f, 1.0f);
				}
				if ((rising_machine_state & MACHINESTATE::ZEROHP) != 0u && (machine_state & MACHINESTATE::B10) == 0u) {
					play_on_emitter(emitter, zero_hp_sfx, 0.0f, 1.0f);
				}
				if ((rising_machine_state & MACHINESTATE::ACTIVE) != 0u) {
					play_on_emitter(emitter, active_start_sfx, 0.0f, 1.0f);
				}
				if ((machine_state & MACHINESTATE::RACEJUSTBEGAN_Q) == 0u &&
					(rising_machine_state & MACHINESTATE::JUSTTAPPEDACCEL) != 0u) {
					play_on_emitter(emitter, thrust_on_sfx, -3.0f, 1.0f);
				}
				if ((rising_machine_state & MACHINESTATE::SPINATTACKING) != 0u) {
					play_on_emitter(emitter, spin_sfx, 0.0f, 1.0f);
				}
				if ((rising_machine_state & MACHINESTATE::SIDEATTACKING) != 0u) {
					play_on_emitter(emitter, sideattack_sfx, 0.0f, 1.0f);
				}
				loop_state.previous_machine_state = machine_state;
				loop_state.previous_terrain_state = terrain_state;
				loop_state.previous_manual_boost_tick = soa.last_manual_boost_tick[lane];
				loop_state.previous_manual_boost_initialized = soa.has_last_manual_boost_tick[lane];
				loop_state.previous_last_hit_tick = soa.last_hit_tick[lane];
				loop_state.previous_last_hit_initialized = soa.has_last_hit_tick[lane];
				loop_state.previous_last_machine_hit_tick = soa.last_machine_hit_tick[lane];
				loop_state.previous_last_machine_hit_initialized = soa.has_last_machine_hit_tick[lane];
			}
		}

		const float gx_visual_speed_norm = mxt_audio_speed_kmh_to_gx_visual_speed_norm(soa.speed_kmh[lane]);
		const int16_t gx_strafe_visual_roll = static_cast<int16_t>(static_cast<int>(
			182.04445f * (soa.stat_strafe[lane] * (1.0f / 15.0f)) *
			-5.0f * soa.input_strafe_1_6[lane] * gx_visual_speed_norm));
		const bool strafe_roll_active =
			car_audible &&
			std::abs(static_cast<int>(gx_strafe_visual_roll)) >= 100 &&
			soa.speed_kmh[lane] > gx_strafe_min_speed_kmh;
		if (strafe_roll_active) {
			set_loop_on_emitter(emitter, strafe_sfx, strafe_sfx, -10.0f, 1.0f);
		} else if (loop_state.previous_strafe_roll_active || find_loop_stream(emitter, strafe_sfx) >= 0) {
			stop_loop_on_emitter(emitter, strafe_sfx);
		}
		loop_state.previous_strafe_roll_active = strafe_roll_active;

		const bool brake_scrub_active =
			car_audible &&
			(machine_state & MACHINESTATE::B10) == 0u &&
			soa.speed_kmh[lane] > 5.0f &&
			soa.input_brake[lane] > 0.1f;
		if (brake_scrub_active) {
			set_loop_on_emitter(emitter, brake_key, brake_sfx, -4.0f, 1.0f);
		} else if (find_loop_stream(emitter, brake_key) >= 0) {
			stop_loop_on_emitter(emitter, brake_key);
		}

		const uint8_t drift_target_control = mxt_audio_gx_drift_target_control(soa, lane);
		if (drift_contact_active && (drift_target_control != 0u || loop_state.gx_drift_contact_control != 0u)) {
			loop_state.gx_drift_contact_control =
				mxt_audio_step_gx_control(loop_state.gx_drift_contact_control, drift_target_control, 1u);
			const uint8_t contact_control = loop_state.gx_drift_contact_control;
			const uint8_t volume_control = 0x7f;
			const float contact_volume_db = mxt_audio_gx_drift_contact_volume_db(volume_control) + drift_layer_volume_gain_db;
			const float contact_pitch = clamped_pitch(mxt_audio_gx_drift_contact_pitch(contact_control));
			set_loop_on_emitter(emitter, drift_loop_key, drift_loop_sfx, contact_volume_db, contact_pitch);
		} else {
			loop_state.gx_drift_contact_control = 0;
			if (find_loop_stream(emitter, drift_loop_key) >= 0) {
				stop_loop_on_emitter(emitter, drift_loop_key);
			}
		}

		const bool engine_active =
			car_audible &&
			(machine_state & MACHINESTATE::STARTINGCOUNTDOWN) == 0u;
		const uint8_t engine_target_speed_control = mxt_audio_gx_engine_base_speed_control(soa.base_speed[lane]);
		if (!loop_state.gx_engine_speed_initialized) {
			loop_state.gx_engine_speed_control = engine_target_speed_control;
			loop_state.gx_engine_speed_initialized = true;
		} else {
			loop_state.gx_engine_speed_control =
				mxt_audio_step_gx_control(loop_state.gx_engine_speed_control, engine_target_speed_control, 4u);
		}
		const uint8_t engine_speed_control = loop_state.gx_engine_speed_control;
		if (engine_active) {
			const MxtGxEngineProgram& engine_program = mxt_audio_select_gx_engine_program(soa.stat_weight[lane]);
			for (uint8_t tone_index = 0; tone_index < 5; ++tone_index) {
				if (tone_index < engine_program.tone_count) {
					const MxtGxEngineTone& tone = engine_program.tones[tone_index];
					const StringName& tone_sfx = mxt_audio_gx_engine_sfx_id(tone.sample_id);
					if (tone_sfx != StringName()) {
						const float tone_volume_db = mxt_audio_gx_engine_volume(engine_speed_control, tone) + engine_layer_volume_gain_db;
						const float tone_pitch = clamped_pitch(mxt_audio_gx_engine_pitch(engine_speed_control, tone));
						set_loop_on_emitter(emitter, gx_engine_keys[tone_index], tone_sfx, tone_volume_db, tone_pitch);
						continue;
					}
				}
				if (find_loop_stream(emitter, gx_engine_keys[tone_index]) >= 0) {
					stop_loop_on_emitter(emitter, gx_engine_keys[tone_index]);
				}
			}
		} else {
			for (uint8_t tone_index = 0; tone_index < 5; ++tone_index) {
				if (find_loop_stream(emitter, gx_engine_keys[tone_index]) >= 0) {
					stop_loop_on_emitter(emitter, gx_engine_keys[tone_index]);
				}
			}
		}

		const float terrain_pitch = clamped_pitch(mxt_audio_gx_terrain_pitch(engine_speed_control));
		if (car_audible && (terrain_state & TERRAIN::DIRT) != 0u) {
			set_loop_on_emitter(emitter, terrain_dirt_key, terrain_dirt_sfx,
				terrain_layer_volume_db, terrain_pitch);
		} else if (find_loop_stream(emitter, terrain_dirt_key) >= 0) {
			stop_loop_on_emitter(emitter, terrain_dirt_key);
		}
		if (car_audible && (terrain_state & TERRAIN::LAVA) != 0u) {
			const float terrain_volume_db = terrain_layer_volume_db;
			set_loop_on_emitter(emitter, terrain_lava_key, terrain_lava_sfx, terrain_volume_db, terrain_pitch);
			set_loop_on_emitter(emitter, terrain_lava_secondary_key, terrain_lava_secondary_sfx, terrain_volume_db, terrain_pitch);
		} else if (find_loop_stream(emitter, terrain_lava_key) >= 0) {
			stop_loop_on_emitter(emitter, terrain_lava_key);
			if (find_loop_stream(emitter, terrain_lava_secondary_key) >= 0) {
				stop_loop_on_emitter(emitter, terrain_lava_secondary_key);
			}
		} else if (find_loop_stream(emitter, terrain_lava_secondary_key) >= 0) {
			stop_loop_on_emitter(emitter, terrain_lava_secondary_key);
		}

		const bool recharging = (terrain_state & TERRAIN::RECHARGE) != 0u;
		float energy_pitch = mxt_audio_clamp_float((loop_state.pitstop_time + 2.0f) * 0.25f, 0.5f, 2.0f);
		if (recharging) {
			loop_state.pitstop_time += delta_f;
		} else {
			loop_state.pitstop_time = mxt_audio_lerp(loop_state.pitstop_time, 0.0f, pitstop_lerp_weight);
		}
		const float energy_volume_base = mxt_audio_clamp_float((loop_state.pitstop_time / 0.25f) * 60.0f - 60.0f, -60.0f, 0.0f);
		const float energy_volume = energy_volume_base + 8.0f;
		if (recharging || energy_volume_base > -59.5f || find_loop_stream(emitter, energy_key) >= 0) {
			set_loop_on_emitter(emitter, energy_key, energy_sfx, energy_volume, energy_pitch);
			if (!recharging && energy_volume_base <= -59.5f) {
				stop_loop_on_emitter(emitter, energy_key);
			}
		}

		const bool gx_air_enabled =
			(machine_state & MACHINESTATE::AIRBORNE) != 0u &&
			(machine_state & (MACHINESTATE::ZEROHP | MACHINESTATE::FALLOUT)) == 0u &&
			soa.speed_kmh[lane] > 5.0f;
		if (gx_air_enabled) {
			const uint8_t gx_air_tilt_control = mxt_audio_gx_air_tilt_control(soa.air_tilt[lane]);
			const uint8_t gx_air_speed_control = mxt_audio_gx_engine_speed_control(soa.speed_kmh[lane]);
			for (int air_index = 0; air_index < 2; ++air_index) {
				const float pitch_lookup_scale = mxt_audio_eval_musyx_pitch_lookup_scale(
					gx_air_tilt_control, air_pitch_lookup_curves[air_index], 2);
				const float pitch_ratio_scale =
					mxt_audio_eval_musyx_pitch_ratio(gx_air_speed_control, 0u, air_pitch_ratio_curves[air_index], 1);
				const float target_pitch = clamped_pitch(
					pitch_lookup_scale * pitch_ratio_scale);
				loop_state.air_volume_db[air_index] = 0.0f;
				loop_state.air_pitch_scale[air_index] = target_pitch;
				set_loop_on_emitter(emitter, air_keys[air_index], air_sfx[air_index], loop_state.air_volume_db[air_index], loop_state.air_pitch_scale[air_index]);
			}
		} else {
			for (int air_index = 0; air_index < 2; ++air_index) {
				if (find_loop_stream(emitter, air_keys[air_index]) >= 0) {
					stop_loop_on_emitter(emitter, air_keys[air_index]);
				}
				loop_state.air_volume_db[air_index] = -20.0f;
			}
		}

	}
}

void MxtSpatialAudioManager::start_music_streams(const Ref<AudioStream>& intro, const Ref<AudioStream>& loop)
{
	ensure_global_players();
	music_stop_pending = false;
	music_stop_fade_remaining = 0.0f;
	music_stop_fade_total = 0.0f;
	active_music_intro = intro;
	active_music_loop = loop;
	if (!music_player || loop.is_null()) {
		music_playing = false;
		return;
	}
	music_player->set_bus(music_bus);
	music_player->set_volume_db(music_volume_db);
	if (intro.is_valid()) {
		music_player->set_stream(intro);
	} else {
		music_player->set_stream(loop);
	}
	music_player->play(0.0);
	music_playing = true;
}

void MxtSpatialAudioManager::finish_music_stop()
{
	if (music_player) {
		music_player->stop();
		music_player->set_stream(Ref<AudioStream>());
		music_player->set_volume_db(music_volume_db);
	}
	music_playing = false;
	music_stop_pending = false;
	music_stop_fade_remaining = 0.0f;
	music_stop_fade_total = 0.0f;
	final_lap_requested = false;
	final_lap_active = false;
	final_lap_request_position = 0.0f;
	active_music_intro.unref();
	active_music_loop.unref();
}

void MxtSpatialAudioManager::advance_music(double delta)
{
	if (music_stop_pending) {
		if (!music_player || !music_player->is_playing()) {
			finish_music_stop();
			return;
		}
		music_stop_fade_remaining -= static_cast<float>(std::max(0.0, delta));
		const float fade_ratio = std::max(0.0f, music_stop_fade_remaining / std::max(MXT_AUDIO_MIN_FADE_SECONDS, music_stop_fade_total));
		music_player->set_volume_db(fade_volume_db(music_stop_fade_start_volume_db, fade_ratio));
		if (music_stop_fade_remaining <= 0.0f) {
			finish_music_stop();
		}
		return;
	}
	if (!music_playing || active_music_loop.is_null()) {
		return;
	}
	ensure_global_players();
	if (!music_player) {
		return;
	}
	if (!music_player->is_playing()) {
		music_player->set_stream(active_music_loop);
		music_player->set_bus(music_bus);
		music_player->set_volume_db(music_volume_db);
		music_player->play(0.0);
	}

	const Ref<AudioStream> current_stream = music_player->get_stream();
	if (current_stream != active_music_intro && current_stream != active_music_loop) {
		if (active_music_intro.is_valid()) {
			music_player->set_stream(active_music_intro);
		} else {
			music_player->set_stream(active_music_loop);
		}
		music_player->set_bus(music_bus);
		music_player->set_volume_db(music_volume_db);
		music_player->play(0.0);
	}

	if (!final_lap_requested || final_lap_active || music_final_loop.is_null()) {
		return;
	}
	bool transition_now = final_lap_music_timestamps.empty();
	const float playback_position = static_cast<float>(music_player->get_playback_position());
	for (float marker : final_lap_music_timestamps) {
		if ((marker > final_lap_request_position && playback_position >= marker) ||
			(marker == 0.0f && playback_position < 0.1f)) {
			transition_now = true;
			break;
		}
	}
	if (!transition_now) {
		return;
	}
	final_lap_requested = false;
	final_lap_active = true;
	start_music_streams(music_final_intro, music_final_loop);
}

void MxtSpatialAudioManager::advance_announcer()
{
	ensure_global_players();
	if (!announcer_player || announcer_player->is_playing() || announcer_queue.empty()) {
		return;
	}
	QueuedAnnouncer next = announcer_queue.front();
	for (size_t i = 1; i < announcer_queue.size(); ++i) {
		announcer_queue[i - 1] = announcer_queue[i];
	}
	announcer_queue.pop_back();
	play_announcer_now(next.sfx_id, next.volume_db, next.pitch_scale);
}

void MxtSpatialAudioManager::configure(int vehicle_emitter_count, int world_emitter_count, int p_vehicle_polyphony, int p_world_polyphony)
{
	clear_emitters(vehicle_emitters);
	clear_emitters(world_emitters);
	vehicle_polyphony = std::max(1, p_vehicle_polyphony);
	world_polyphony = std::max(1, p_world_polyphony);
	create_emitters(vehicle_emitters, std::max(0, vehicle_emitter_count), vehicle_polyphony, "VehicleAudioEmitter", 120.0f);
	create_emitters(world_emitters, std::max(0, world_emitter_count), world_polyphony, "WorldAudioEmitter", 120.0f);
	vehicle_candidates.reserve(vehicle_emitters.size());
}

bool MxtSpatialAudioManager::register_sfx(const StringName& id, const String& path)
{
	if (id == StringName() || path.is_empty()) {
		return false;
	}
	Ref<AudioStream> stream = load_audio_stream(path);
	if (stream.is_null()) {
		return false;
	}
	for (SfxEntry& entry : sfx_entries) {
		if (entry.id == id) {
			entry.stream = stream;
			return true;
		}
	}
	SfxEntry entry;
	entry.id = id;
	entry.stream = stream;
	sfx_entries.push_back(entry);
	return true;
}

bool MxtSpatialAudioManager::has_sfx(const StringName& id) const
{
	return find_sfx(id).is_valid();
}

void MxtSpatialAudioManager::clear_sfx()
{
	sfx_entries.clear();
	announcer_queue.clear();
}

void MxtSpatialAudioManager::set_audio_bus(const StringName& bus)
{
	audio_bus = bus == StringName() ? StringName("Master") : bus;
	for (Emitter& emitter : vehicle_emitters) {
		apply_emitter_bus(emitter);
	}
	for (Emitter& emitter : world_emitters) {
		apply_emitter_bus(emitter);
	}
}

void MxtSpatialAudioManager::set_remote_vehicle_audio_bus(const StringName& bus)
{
	remote_vehicle_audio_bus = bus;
	for (Emitter& emitter : vehicle_emitters) {
		apply_emitter_bus(emitter);
	}
}

void MxtSpatialAudioManager::set_music_bus(const StringName& bus)
{
	music_bus = bus == StringName() ? StringName("Master") : bus;
	if (music_player) {
		music_player->set_bus(music_bus);
	}
}

void MxtSpatialAudioManager::set_announcer_bus(const StringName& bus)
{
	announcer_bus = bus == StringName() ? StringName("Master") : bus;
	if (announcer_player) {
		announcer_player->set_bus(announcer_bus);
	}
}

void MxtSpatialAudioManager::set_music_volume_db(double volume_db)
{
	if (!std::isfinite(volume_db)) {
		return;
	}
	music_volume_db = static_cast<float>(std::max(-80.0, std::min(24.0, volume_db)));
	if (music_player && !music_stop_pending) {
		music_player->set_volume_db(music_volume_db);
	}
}

void MxtSpatialAudioManager::set_announcer_volume_db(double volume_db)
{
	if (!std::isfinite(volume_db)) {
		return;
	}
	announcer_volume_db = static_cast<float>(std::max(-80.0, std::min(24.0, volume_db)));
	if (announcer_player) {
		announcer_player->set_volume_db(announcer_volume_db);
	}
}

void MxtSpatialAudioManager::set_max_announcer_queue(int max_queue)
{
	max_announcer_queue = std::max(1, std::min(64, max_queue));
	if (announcer_queue.size() > static_cast<size_t>(max_announcer_queue)) {
		announcer_queue.resize(static_cast<size_t>(max_announcer_queue));
	}
	announcer_queue.reserve(static_cast<size_t>(max_announcer_queue));
}

void MxtSpatialAudioManager::set_reassignment_fade_seconds(double seconds)
{
	if (!std::isfinite(seconds)) {
		return;
	}
	reassignment_fade_seconds = std::max(0.0f, std::min(1.0f, static_cast<float>(seconds)));
}

void MxtSpatialAudioManager::set_vehicle_max_distance(double distance)
{
	if (!std::isfinite(distance)) {
		return;
	}
	vehicle_max_distance = std::max(0.0f, static_cast<float>(distance));
}

bool MxtSpatialAudioManager::play_music_paths(const String& loop_path, const String& intro_path, const String& final_loop_path, const String& final_intro_path, const PackedFloat32Array& final_lap_timestamps)
{
	music_loop = load_audio_stream(loop_path);
	if (music_loop.is_null()) {
		stop_music(0.0);
		return false;
	}
	music_intro = load_audio_stream(intro_path);
	music_final_loop = load_audio_stream(final_loop_path);
	music_final_intro = load_audio_stream(final_intro_path);
	final_lap_music_timestamps.clear();
	final_lap_music_timestamps.reserve(static_cast<size_t>(final_lap_timestamps.size()));
	for (int i = 0; i < final_lap_timestamps.size(); ++i) {
		const float marker = final_lap_timestamps[i];
		if (std::isfinite(marker) && marker >= 0.0f) {
			final_lap_music_timestamps.push_back(marker);
		}
	}
	std::sort(final_lap_music_timestamps.begin(), final_lap_music_timestamps.end());
	final_lap_requested = false;
	final_lap_active = false;
	final_lap_request_position = 0.0f;
	start_music_streams(music_intro, music_loop);
	return true;
}

void MxtSpatialAudioManager::stop_music(double fade_seconds)
{
	if (!std::isfinite(fade_seconds) || fade_seconds <= 0.0 || !music_player || !music_player->is_playing()) {
		finish_music_stop();
		return;
	}
	music_stop_pending = true;
	music_stop_fade_total = std::max(MXT_AUDIO_MIN_FADE_SECONDS, static_cast<float>(std::min(10.0, fade_seconds)));
	music_stop_fade_remaining = music_stop_fade_total;
	music_stop_fade_start_volume_db = music_volume_db;
	final_lap_requested = false;
}

bool MxtSpatialAudioManager::request_final_lap_music()
{
	if (music_final_loop.is_null()) {
		return false;
	}
	if (final_lap_active) {
		return true;
	}
	ensure_global_players();
	final_lap_requested = true;
	final_lap_request_position = music_player ? static_cast<float>(music_player->get_playback_position()) : 0.0f;
	return true;
}

double MxtSpatialAudioManager::get_music_playback_position() const
{
	return music_player ? music_player->get_playback_position() : 0.0;
}

void MxtSpatialAudioManager::process_global_audio(double delta)
{
	advance_music(delta);
	advance_announcer();
}

bool MxtSpatialAudioManager::queue_announcer(const StringName& sfx_id, double volume_db, double pitch_scale)
{
	if (sfx_id == StringName() || find_sfx(sfx_id).is_null()) {
		return false;
	}
	if (static_cast<int>(announcer_queue.size()) >= max_announcer_queue) {
		return false;
	}
	QueuedAnnouncer queued;
	queued.sfx_id = sfx_id;
	queued.volume_db = static_cast<float>(std::isfinite(volume_db) ? volume_db : 0.0);
	queued.pitch_scale = clamped_pitch(static_cast<float>(pitch_scale));
	announcer_queue.push_back(queued);
	advance_announcer();
	return true;
}

bool MxtSpatialAudioManager::play_announcer_now(const StringName& sfx_id, double volume_db, double pitch_scale)
{
	Ref<AudioStream> stream = find_sfx(sfx_id);
	if (stream.is_null()) {
		return false;
	}
	ensure_global_players();
	if (!announcer_player) {
		return false;
	}
	announcer_player->set_stream(stream);
	announcer_player->set_bus(announcer_bus);
	announcer_player->set_volume_db(announcer_volume_db + static_cast<float>(std::isfinite(volume_db) ? volume_db : 0.0));
	announcer_player->set_pitch_scale(clamped_pitch(static_cast<float>(pitch_scale)));
	announcer_player->play(0.0);
	return true;
}

void MxtSpatialAudioManager::clear_announcer_queue()
{
	announcer_queue.clear();
}

void MxtSpatialAudioManager::update_from_gamesim(GameSim* sim, int local_player_id, double delta, bool update_assignments)
{
	local_vehicle_car_index = sim ? sim->get_car_index_for_player(static_cast<int32_t>(local_player_id)) : -1;
	if (!vehicle_emitters.empty()) {
		Emitter& local_emitter = vehicle_emitters[0];
		if (sim && local_vehicle_car_index >= 0 && local_vehicle_car_index < sim->num_cars) {
			if (local_emitter.car_index != local_vehicle_car_index) {
				stop_all_streams(local_emitter);
				assign_vehicle_emitter(local_emitter, local_vehicle_car_index, sim->get_car_render_transform(local_vehicle_car_index));
			} else {
				set_emitter_transform(local_emitter, sim->get_car_render_transform(local_vehicle_car_index));
			}
			apply_emitter_attenuation(local_emitter, true);
			apply_emitter_bus(local_emitter);
		} else if (local_emitter.car_index >= 0) {
			stop_all_streams(local_emitter);
			local_emitter.car_index = -1;
			local_emitter.pending_car_index = -1;
			local_emitter.fade_remaining = 0.0f;
			local_emitter.fade_total = 0.0f;
			apply_emitter_attenuation(local_emitter, false);
			apply_emitter_bus(local_emitter);
		}
	}

	for (Emitter& emitter : vehicle_emitters) {
		prune_stopped_streams(emitter);
		if (sim && emitter.car_index >= 0 && emitter.car_index < sim->num_cars) {
			set_emitter_transform(emitter, sim->get_car_render_transform(emitter.car_index));
		}
		apply_emitter_attenuation(emitter, local_vehicle_car_index >= 0 && emitter.car_index == local_vehicle_car_index);
		apply_emitter_bus(emitter);
		advance_fade(emitter, delta, true);
	}
	for (Emitter& emitter : world_emitters) {
		prune_stopped_streams(emitter);
		advance_fade(emitter, delta, false);
	}
	if (update_assignments) {
		collect_vehicle_candidates(sim);
		assign_vehicle_candidates();
	}
	update_vehicle_loop_audio(sim, delta, update_assignments);
	for (Emitter& emitter : vehicle_emitters) {
		apply_emitter_attenuation(emitter, local_vehicle_car_index >= 0 && emitter.car_index == local_vehicle_car_index);
		apply_emitter_bus(emitter);
	}
}

void MxtSpatialAudioManager::set_vehicle_manual_boost_sfx(int car_index, const StringName& sfx_id)
{
	if (car_index < 0) {
		return;
	}
	const size_t index = static_cast<size_t>(car_index);
	if (index >= vehicle_manual_boost_sfx.size()) {
		vehicle_manual_boost_sfx.resize(index + 1u);
	}
	vehicle_manual_boost_sfx[index] = sfx_id;
}

bool MxtSpatialAudioManager::play_vehicle_oneshot(int car_index, const StringName& sfx_id, double volume_db, double pitch_scale)
{
	const int slot = find_vehicle_emitter_for_car(car_index);
	if (slot < 0) {
		return false;
	}
	return play_on_emitter(vehicle_emitters[static_cast<size_t>(slot)], sfx_id, static_cast<float>(volume_db), static_cast<float>(pitch_scale));
}

bool MxtSpatialAudioManager::set_vehicle_loop(int car_index, const StringName& key, const StringName& sfx_id, double volume_db, double pitch_scale)
{
	const int slot = find_vehicle_emitter_for_car(car_index);
	if (slot < 0) {
		return false;
	}
	return set_loop_on_emitter(vehicle_emitters[static_cast<size_t>(slot)], key, sfx_id, static_cast<float>(volume_db), static_cast<float>(pitch_scale));
}

bool MxtSpatialAudioManager::stop_vehicle_loop(int car_index, const StringName& key)
{
	const int slot = find_vehicle_emitter_for_car(car_index);
	if (slot < 0) {
		return false;
	}
	return stop_loop_on_emitter(vehicle_emitters[static_cast<size_t>(slot)], key);
}

bool MxtSpatialAudioManager::play_world_oneshot(const Vector3& position, const StringName& sfx_id, double volume_db, double pitch_scale)
{
	if (world_emitters.empty() || find_sfx(sfx_id).is_null()) {
		return false;
	}
	for (Emitter& emitter : world_emitters) {
		prune_stopped_streams(emitter);
	}
	int slot = find_reusable_world_emitter(position);
	if (slot >= 0) {
		return play_on_emitter(world_emitters[static_cast<size_t>(slot)], sfx_id, static_cast<float>(volume_db), static_cast<float>(pitch_scale));
	}
	slot = find_idle_world_emitter();
	if (slot >= 0) {
		Emitter& emitter = world_emitters[static_cast<size_t>(slot)];
		assign_world_emitter(emitter, position);
		return play_on_emitter(emitter, sfx_id, static_cast<float>(volume_db), static_cast<float>(pitch_scale));
	}
	slot = find_world_steal_emitter();
	if (slot < 0) {
		return false;
	}
	begin_world_reassignment(world_emitters[static_cast<size_t>(slot)], position, sfx_id, static_cast<float>(volume_db), static_cast<float>(pitch_scale));
	return true;
}

void MxtSpatialAudioManager::clear_all()
{
	for (Emitter& emitter : vehicle_emitters) {
		stop_all_streams(emitter);
		emitter.car_index = -1;
		emitter.pending_car_index = -1;
		emitter.pending_sound.active = false;
		emitter.fade_remaining = 0.0f;
		emitter.fade_total = 0.0f;
	}
	for (Emitter& emitter : world_emitters) {
		stop_all_streams(emitter);
		emitter.pending_sound.active = false;
		emitter.fade_remaining = 0.0f;
		emitter.fade_total = 0.0f;
	}
	vehicle_loop_states.clear();
	vehicle_manual_boost_sfx.clear();
	clear_announcer_queue();
}

int MxtSpatialAudioManager::get_assigned_vehicle_count() const
{
	int count = 0;
	for (const Emitter& emitter : vehicle_emitters) {
		if (emitter.car_index >= 0 && emitter.fade_remaining <= 0.0f) {
			++count;
		}
	}
	return count;
}
