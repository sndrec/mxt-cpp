#ifndef MXT_SPATIAL_AUDIO_MANAGER_H
#define MXT_SPATIAL_AUDIO_MANAGER_H

#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback_polyphonic.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/audio_stream_polyphonic.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
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

	struct LoopStream {
		StringName key;
		StringName sfx_id;
		int64_t id = AudioStreamPlaybackPolyphonic::INVALID_ID;
		float volume_db = 0.0f;
		float pitch_scale = 1.0f;
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
		std::vector<LoopStream> loop_streams;
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

	struct QueuedAnnouncer {
		StringName sfx_id;
		float volume_db = 0.0f;
		float pitch_scale = 1.0f;
	};

	struct VehicleLoopState {
		float pitstop_time = 0.0f;
		float air_volume_db[2] = { -20.0f, -20.0f };
		float air_pitch_scale[2] = { 1.0f, 1.0f };
		uint32_t previous_machine_state = 0;
		uint32_t previous_terrain_state = 0;
		uint32_t previous_manual_boost_tick = 0;
		uint32_t previous_last_hit_tick = 0;
		uint8_t gx_engine_speed_control = 0;
		uint8_t gx_drift_contact_control = 0;
		bool previous_manual_boost_initialized = false;
		bool previous_last_hit_initialized = false;
		bool previous_strafe_roll_active = false;
		bool event_state_initialized = false;
		bool gx_engine_speed_initialized = false;
	};

	std::vector<Emitter> vehicle_emitters;
	std::vector<Emitter> world_emitters;
	std::vector<SfxEntry> sfx_entries;
	std::vector<VehicleCandidate> vehicle_candidates;
	std::vector<VehicleLoopState> vehicle_loop_states;
	std::vector<StringName> vehicle_manual_boost_sfx;
	std::vector<float> final_lap_music_timestamps;
	std::vector<QueuedAnnouncer> announcer_queue;
	AudioStreamPlayer* music_player = nullptr;
	AudioStreamPlayer* announcer_player = nullptr;
	Ref<AudioStream> music_intro;
	Ref<AudioStream> music_loop;
	Ref<AudioStream> music_final_intro;
	Ref<AudioStream> music_final_loop;
	Ref<AudioStream> active_music_intro;
	Ref<AudioStream> active_music_loop;
	StringName audio_bus = StringName("Master");
	StringName music_bus = StringName("Master");
	StringName announcer_bus = StringName("Master");
	float reassignment_fade_seconds = 0.05f;
	float vehicle_max_distance = 1800.0f;
	float world_reuse_distance_sq = 9.0f;
	float music_volume_db = -5.0f;
	float announcer_volume_db = -10.0f;
	float final_lap_request_position = 0.0f;
	float music_stop_fade_remaining = 0.0f;
	float music_stop_fade_total = 0.0f;
	float music_stop_fade_start_volume_db = -5.0f;
	int vehicle_polyphony = 16;
	int world_polyphony = 8;
	int max_announcer_queue = 16;
	int local_vehicle_car_index = -1;
	bool music_playing = false;
	bool final_lap_requested = false;
	bool final_lap_active = false;
	bool music_stop_pending = false;

	static float clamped_pitch(float pitch_scale);
	static float fade_volume_db(float base_volume_db, float fade_ratio);
	static Ref<AudioStream> load_audio_stream(const String& path);
	Ref<AudioStream> find_sfx(const StringName& id) const;
	void ensure_global_players();
	void clear_emitters(std::vector<Emitter>& emitters);
	void create_emitters(std::vector<Emitter>& emitters, int count, int polyphony, const char* name_prefix);
	void refresh_playback(Emitter& emitter);
	void prune_stopped_streams(Emitter& emitter);
	void apply_emitter_attenuation(Emitter& emitter, bool local_vehicle);
	void stop_all_streams(Emitter& emitter);
	int find_loop_stream(const Emitter& emitter, const StringName& key) const;
	bool set_loop_on_emitter(Emitter& emitter, const StringName& key, const StringName& sfx_id, float volume_db, float pitch_scale);
	bool stop_loop_on_emitter(Emitter& emitter, const StringName& key);
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
	void collect_vehicle_candidates(GameSim* sim);
	void assign_vehicle_candidates();
	void update_vehicle_loop_audio(GameSim* sim, double delta, bool step_events);
	void start_music_streams(const Ref<AudioStream>& intro, const Ref<AudioStream>& loop);
	void finish_music_stop();
	void advance_music(double delta);
	void advance_announcer();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	MxtSpatialAudioManager();
	~MxtSpatialAudioManager();

	void configure(int vehicle_emitter_count, int world_emitter_count, int p_vehicle_polyphony, int p_world_polyphony);
	bool register_sfx(const StringName& id, const String& path);
	bool has_sfx(const StringName& id) const;
	void clear_sfx();
	void set_audio_bus(const StringName& bus);
	StringName get_audio_bus() const { return audio_bus; }
	void set_music_bus(const StringName& bus);
	StringName get_music_bus() const { return music_bus; }
	void set_announcer_bus(const StringName& bus);
	StringName get_announcer_bus() const { return announcer_bus; }
	void set_music_volume_db(double volume_db);
	double get_music_volume_db() const { return music_volume_db; }
	void set_announcer_volume_db(double volume_db);
	double get_announcer_volume_db() const { return announcer_volume_db; }
	void set_max_announcer_queue(int max_queue);
	int get_max_announcer_queue() const { return max_announcer_queue; }
	void set_reassignment_fade_seconds(double seconds);
	double get_reassignment_fade_seconds() const { return reassignment_fade_seconds; }
	void set_vehicle_max_distance(double distance);
	double get_vehicle_max_distance() const { return vehicle_max_distance; }
	bool play_music_paths(const String& loop_path, const String& intro_path, const String& final_loop_path, const String& final_intro_path, const PackedFloat32Array& final_lap_timestamps);
	void stop_music(double fade_seconds = 0.0);
	bool request_final_lap_music();
	bool is_final_lap_music_active() const { return final_lap_active; }
	double get_music_playback_position() const;
	void process_global_audio(double delta);
	bool queue_announcer(const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	bool play_announcer_now(const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	void clear_announcer_queue();
	int get_announcer_queue_size() const { return static_cast<int>(announcer_queue.size()); }
	void update_from_gamesim(GameSim* sim, int local_player_id, double delta, bool update_assignments);
	void set_vehicle_manual_boost_sfx(int car_index, const StringName& sfx_id);
	bool play_vehicle_oneshot(int car_index, const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	bool set_vehicle_loop(int car_index, const StringName& key, const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	bool stop_vehicle_loop(int car_index, const StringName& key);
	bool play_world_oneshot(const Vector3& position, const StringName& sfx_id, double volume_db = 0.0, double pitch_scale = 1.0);
	void clear_all();
	int get_assigned_vehicle_count() const;
};

}

#endif
