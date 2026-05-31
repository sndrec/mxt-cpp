#ifndef MXT_SPATIAL_AUDIO_MANAGER_H
#define MXT_SPATIAL_AUDIO_MANAGER_H

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback_polyphonic.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/audio_stream_polyphonic.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class GameSim;

class MxtSpatialAudioManager : public Node3D {
	GDCLASS(MxtSpatialAudioManager, Node3D)

private:
	struct ActiveStream {
		int64_t id = -1;
		float volume_db = 0.0f;
	};

	struct PendingSound {
		bool active = false;
		StringName sfx_id;
		float volume_db = 0.0f;
		float pitch_scale = 1.0f;
	};

	struct Emitter {
		AudioStreamPlayer3D* player = nullptr;
		Ref<AudioStreamPolyphonic> stream;
		Ref<AudioStreamPlaybackPolyphonic> playback;
		std::vector<ActiveStream> active_streams;
		int car_index = -1;
		int pending_car_index = -1;
		Transform3D pending_transform;
		Vector3 pending_world_position;
		PendingSound pending_sound;
		float fade_remaining = 0.0f;
		float fade_total = 0.0f;
	};

	struct SfxEntry {
		StringName id;
		Ref<AudioStream> stream;
	};

	struct VehicleCandidate {
		int car_index = -1;
		float distance_sq = 0.0f;
		Transform3D transform;
	};

	std::vector<Emitter> vehicle_emitters;
	std::vector<Emitter> world_emitters;
	std::vector<SfxEntry> sfx_entries;
	std::vector<VehicleCandidate> vehicle_candidates;
	std::vector<int> vehicle_candidate_slots;
	StringName audio_bus = StringName("Master");
	float reassignment_fade_seconds = 0.05f;
	float vehicle_max_distance = 1800.0f;
	float world_reuse_distance_sq = 9.0f;
	int vehicle_polyphony = 16;
	int world_polyphony = 8;

	static float clamped_pitch(float pitch_scale);
	static float fade_volume_db(float base_volume_db, float fade_ratio);
	Ref<AudioStream> find_sfx(const StringName& id) const;
	void clear_emitters(std::vector<Emitter>& emitters);
	void create_emitters(std::vector<Emitter>& emitters, int count, int polyphony, const char* name_prefix);
	void refresh_playback(Emitter& emitter);
	void prune_stopped_streams(Emitter& emitter);
	void stop_all_streams(Emitter& emitter);
	void set_emitter_position(Emitter& emitter, const Vector3& position);
	void set_emitter_transform(Emitter& emitter, const Transform3D& transform);
	void begin_vehicle_reassignment(Emitter& emitter, int car_index, const Transform3D& transform);
	void begin_world_reassignment(Emitter& emitter, const Vector3& position, const StringName& sfx_id, float volume_db, float pitch_scale);
	void advance_fade(Emitter& emitter, double delta, bool vehicle_emitter);
	bool play_on_emitter(Emitter& emitter, const StringName& sfx_id, float volume_db, float pitch_scale);
	int find_vehicle_emitter_for_car(int car_index) const;
	int find_idle_vehicle_emitter() const;
	int find_idle_world_emitter() const;
	int find_reusable_world_emitter(const Vector3& position) const;
	int find_world_steal_emitter() const;
	void assign_vehicle_emitter(Emitter& emitter, int car_index, const Transform3D& transform);
	void assign_world_emitter(Emitter& emitter, const Vector3& position);
	void collect_vehicle_candidates(GameSim* sim, int local_player_id);
	void assign_vehicle_candidates();

protected:
	static void _bind_methods();

public:
	MxtSpatialAudioManager();
	~MxtSpatialAudioManager();

	void configure(int vehicle_emitter_count, int world_emitter_count, int p_vehicle_polyphony, int p_world_polyphony);
	bool register_sfx(const StringName& id, const String& path);
	bool has_sfx(const StringName& id) const;
	void clear_sfx();
	void set_audio_bus(const StringName& bus);
	StringName get_audio_bus() const { return audio_bus; }
	void set_reassignment_fade_seconds(double seconds);
	double get_reassignment_fade_seconds() const { return reassignment_fade_seconds; }
	void set_vehicle_max_distance(double distance);
	double get_vehicle_max_distance() const { return vehicle_max_distance; }
	void update_from_gamesim(GameSim* sim, int local_player_id, double delta);
	bool play_vehicle_oneshot(int car_index, const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	bool play_world_oneshot(const Vector3& position, const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	void clear_all();
	int get_assigned_vehicle_count() const;
};

}

#endif
