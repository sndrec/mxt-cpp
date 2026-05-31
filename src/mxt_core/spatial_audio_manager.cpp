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

using namespace godot;

namespace {
static constexpr float MXT_AUDIO_MIN_FADE_SECONDS = 0.001f;
static constexpr float MXT_AUDIO_SILENCE_DB = -80.0f;
}

MxtSpatialAudioManager::MxtSpatialAudioManager() = default;

MxtSpatialAudioManager::~MxtSpatialAudioManager() = default;

void MxtSpatialAudioManager::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure", "vehicle_emitter_count", "world_emitter_count", "vehicle_polyphony", "world_polyphony"), &MxtSpatialAudioManager::configure, DEFVAL(30), DEFVAL(16), DEFVAL(16), DEFVAL(8));
	ClassDB::bind_method(D_METHOD("register_sfx", "id", "path"), &MxtSpatialAudioManager::register_sfx);
	ClassDB::bind_method(D_METHOD("has_sfx", "id"), &MxtSpatialAudioManager::has_sfx);
	ClassDB::bind_method(D_METHOD("clear_sfx"), &MxtSpatialAudioManager::clear_sfx);
	ClassDB::bind_method(D_METHOD("set_audio_bus", "bus"), &MxtSpatialAudioManager::set_audio_bus);
	ClassDB::bind_method(D_METHOD("get_audio_bus"), &MxtSpatialAudioManager::get_audio_bus);
	ClassDB::bind_method(D_METHOD("set_reassignment_fade_seconds", "seconds"), &MxtSpatialAudioManager::set_reassignment_fade_seconds);
	ClassDB::bind_method(D_METHOD("get_reassignment_fade_seconds"), &MxtSpatialAudioManager::get_reassignment_fade_seconds);
	ClassDB::bind_method(D_METHOD("set_vehicle_max_distance", "distance"), &MxtSpatialAudioManager::set_vehicle_max_distance);
	ClassDB::bind_method(D_METHOD("get_vehicle_max_distance"), &MxtSpatialAudioManager::get_vehicle_max_distance);
	ClassDB::bind_method(D_METHOD("update_from_gamesim", "game_sim", "local_player_id", "delta"), &MxtSpatialAudioManager::update_from_gamesim);
	ClassDB::bind_method(D_METHOD("play_vehicle_oneshot", "car_index", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::play_vehicle_oneshot, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("play_world_oneshot", "position", "sfx_id", "volume_db", "pitch_scale"), &MxtSpatialAudioManager::play_world_oneshot, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("clear_all"), &MxtSpatialAudioManager::clear_all);
	ClassDB::bind_method(D_METHOD("get_assigned_vehicle_count"), &MxtSpatialAudioManager::get_assigned_vehicle_count);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "audio_bus"), "set_audio_bus", "get_audio_bus");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reassignment_fade_seconds"), "set_reassignment_fade_seconds", "get_reassignment_fade_seconds");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vehicle_max_distance"), "set_vehicle_max_distance", "get_vehicle_max_distance");
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

Ref<AudioStream> MxtSpatialAudioManager::find_sfx(const StringName& id) const
{
	for (const SfxEntry& entry : sfx_entries) {
		if (entry.id == id) {
			return entry.stream;
		}
	}
	return Ref<AudioStream>();
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
	}
	emitters.clear();
}

void MxtSpatialAudioManager::create_emitters(std::vector<Emitter>& emitters, int count, int polyphony, const char* name_prefix)
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
		player->set_unit_size(24.0);
		player->set_max_distance(2200.0);
		player->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
		player->set_doppler_tracking(AudioStreamPlayer3D::DOPPLER_TRACKING_DISABLED);
		add_child(player);
		player->play();

		emitter.player = player;
		refresh_playback(emitter);
		emitter.active_streams.reserve(static_cast<size_t>(polyphony));
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

void MxtSpatialAudioManager::stop_all_streams(Emitter& emitter)
{
	refresh_playback(emitter);
	if (emitter.playback.is_valid()) {
		for (const ActiveStream& active : emitter.active_streams) {
			if (active.id != AudioStreamPlaybackPolyphonic::INVALID_ID) {
				emitter.playback->stop_stream(active.id);
			}
		}
	}
	emitter.active_streams.clear();
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
}

void MxtSpatialAudioManager::assign_world_emitter(Emitter& emitter, const Vector3& position)
{
	emitter.car_index = -1;
	emitter.pending_car_index = -1;
	emitter.fade_remaining = 0.0f;
	emitter.fade_total = 0.0f;
	set_emitter_position(emitter, position);
}

void MxtSpatialAudioManager::begin_vehicle_reassignment(Emitter& emitter, int car_index, const Transform3D& transform)
{
	prune_stopped_streams(emitter);
	if (emitter.active_streams.empty()) {
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
	if (emitter.active_streams.empty()) {
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
				emitter.playback->set_stream_volume(active.id, fade_volume_db(active.volume_db, fade_ratio));
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
	const int64_t stream_id = emitter.playback->play_stream(stream, 0.0, volume_db, clamped_pitch(pitch_scale), AudioServer::PLAYBACK_TYPE_DEFAULT, audio_bus);
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
	for (int i = 0; i < static_cast<int>(vehicle_emitters.size()); ++i) {
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
		if (emitter.fade_remaining <= 0.0f && emitter.active_streams.empty()) {
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

void MxtSpatialAudioManager::collect_vehicle_candidates(GameSim* sim, int local_player_id)
{
	vehicle_candidates.clear();
	if (!sim || !sim->sim_started || sim->num_cars <= 0 || vehicle_emitters.empty()) {
		return;
	}

	Vector3 local_origin;
	if (local_player_id >= 0) {
		local_origin = sim->get_player_render_transform(local_player_id).origin;
	} else if (sim->num_cars > 0) {
		local_origin = sim->get_car_render_transform(0).origin;
	}
	const float max_distance_sq = vehicle_max_distance > 0.0f ? vehicle_max_distance * vehicle_max_distance : 0.0f;
	const int max_candidates = static_cast<int>(vehicle_emitters.size());
	for (int car_index = 0; car_index < sim->num_cars; ++car_index) {
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
		if (static_cast<int>(vehicle_candidates.size()) < max_candidates) {
			vehicle_candidates.push_back(candidate);
		}
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
	vehicle_candidate_slots.assign(vehicle_candidates.size(), -1);
	for (int slot = 0; slot < static_cast<int>(vehicle_emitters.size()); ++slot) {
		Emitter& emitter = vehicle_emitters[static_cast<size_t>(slot)];
		if (emitter.fade_remaining > 0.0f || emitter.car_index < 0) {
			continue;
		}
		bool still_candidate = false;
		for (size_t c = 0; c < vehicle_candidates.size(); ++c) {
			if (vehicle_candidates[c].car_index == emitter.car_index) {
				vehicle_candidate_slots[c] = slot;
				set_emitter_transform(emitter, vehicle_candidates[c].transform);
				still_candidate = true;
				break;
			}
		}
		if (!still_candidate) {
			begin_vehicle_reassignment(emitter, -1, Transform3D());
		}
	}

	for (size_t c = 0; c < vehicle_candidates.size(); ++c) {
		if (vehicle_candidate_slots[c] >= 0) {
			continue;
		}
		int slot = find_idle_vehicle_emitter();
		if (slot < 0) {
			for (int i = 0; i < static_cast<int>(vehicle_emitters.size()); ++i) {
				if (vehicle_emitters[static_cast<size_t>(i)].fade_remaining <= 0.0f) {
					slot = i;
					break;
				}
			}
		}
		if (slot < 0) {
			continue;
		}
		begin_vehicle_reassignment(vehicle_emitters[static_cast<size_t>(slot)], vehicle_candidates[c].car_index, vehicle_candidates[c].transform);
	}
}

void MxtSpatialAudioManager::configure(int vehicle_emitter_count, int world_emitter_count, int p_vehicle_polyphony, int p_world_polyphony)
{
	clear_emitters(vehicle_emitters);
	clear_emitters(world_emitters);
	vehicle_polyphony = std::max(1, p_vehicle_polyphony);
	world_polyphony = std::max(1, p_world_polyphony);
	create_emitters(vehicle_emitters, std::max(0, vehicle_emitter_count), vehicle_polyphony, "VehicleAudioEmitter");
	create_emitters(world_emitters, std::max(0, world_emitter_count), world_polyphony, "WorldAudioEmitter");
	vehicle_candidates.reserve(vehicle_emitters.size());
	vehicle_candidate_slots.reserve(vehicle_emitters.size());
}

bool MxtSpatialAudioManager::register_sfx(const StringName& id, const String& path)
{
	if (id == StringName() || path.is_empty()) {
		return false;
	}
	ResourceLoader* loader = ResourceLoader::get_singleton();
	if (!loader) {
		return false;
	}
	Ref<Resource> resource = loader->load(path, "AudioStream");
	Ref<AudioStream> stream = resource;
	if (stream.is_null()) {
		UtilityFunctions::push_warning(String("MXT audio failed to load SFX: ") + path);
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
}

void MxtSpatialAudioManager::set_audio_bus(const StringName& bus)
{
	audio_bus = bus == StringName() ? StringName("Master") : bus;
	for (Emitter& emitter : vehicle_emitters) {
		if (emitter.player) {
			emitter.player->set_bus(audio_bus);
		}
	}
	for (Emitter& emitter : world_emitters) {
		if (emitter.player) {
			emitter.player->set_bus(audio_bus);
		}
	}
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

void MxtSpatialAudioManager::update_from_gamesim(GameSim* sim, int local_player_id, double delta)
{
	for (Emitter& emitter : vehicle_emitters) {
		prune_stopped_streams(emitter);
		advance_fade(emitter, delta, true);
	}
	for (Emitter& emitter : world_emitters) {
		prune_stopped_streams(emitter);
		advance_fade(emitter, delta, false);
	}
	collect_vehicle_candidates(sim, local_player_id);
	assign_vehicle_candidates();
}

bool MxtSpatialAudioManager::play_vehicle_oneshot(int car_index, const StringName& sfx_id, double volume_db, double pitch_scale)
{
	const int slot = find_vehicle_emitter_for_car(car_index);
	if (slot < 0) {
		return false;
	}
	return play_on_emitter(vehicle_emitters[static_cast<size_t>(slot)], sfx_id, static_cast<float>(volume_db), static_cast<float>(pitch_scale));
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
