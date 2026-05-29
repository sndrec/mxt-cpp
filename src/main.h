#ifndef GAME_SIM
#define GAME_SIM

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/classes/multi_mesh_instance3d.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/camera3d.hpp"
#include "godot_cpp/classes/gpu_particles3d.hpp"
#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/rendering_server.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/rid.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"
#include "fzgx_gameplay_camera.h"
#include "track/racetrack.h"
#include "mxt_core/heap_handler.h"
#include "mxt_core/player_input.h"
#include "car/car_properties.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace godot {

	class NetcodeSession;

	class GameSim : public Node {
		GDCLASS(GameSim, Node)
		friend class NetcodeSession;

	private:
		int tick;
		float tick_delta;
		HeapHandler level_data;
		HeapHandler gamestate_data;
		static const int STATE_BUFFER_LEN = 45;
		struct SavedVoiceTransform {
			int32_t player_id = -1;
			SimVec3 origin;
		};
		struct BumperState {
			uint8_t active = 0;
			uint8_t spawn_lap = 0;
			uint32_t next_sequence = 0;
			float target_lane = 0.0f;
		};
		static constexpr int BUMPER_POOL_SIZE = 60;
		struct SavedState {
			char* data;
			int size;
			int bumper_state_count;
			uint8_t bumper_scheduler_lap;
			uint32_t bumper_next_sequence;
			BumperState bumper_states[BUMPER_POOL_SIZE];
			int tick = -1;
			int voice_transform_count = 0;
			std::vector<SavedVoiceTransform> voice_transforms;
		};
		SavedState state_buffer[STATE_BUFFER_LEN];
		std::vector<char> network_state_live_backup;
		struct NetworkStateSizeStats {
			int total = 0;
			int header = 0;
			int bumper_meta = 0;
			int sparks = 0;
			int car_scalars = 0;
			int bumper_scalars = 0;
			int car_vec3 = 0;
			int bumper_vec3 = 0;
			int car_transform = 0;
			int bumper_transform = 0;
			int car_basis = 0;
			int bumper_basis = 0;
			int car_conditionals = 0;
			int bumper_conditionals = 0;
			int car_tilt = 0;
			int bumper_tilt = 0;
			int car_wall = 0;
			int bumper_wall = 0;
			int triggers = 0;
			int car_restore_count = 0;
			int bumper_restore_count = 0;
			int active_bumper_count = 0;
			int active_spark_count = 0;
			int trigger_count = 0;
			int car_count = 0;
			int bumper_count = 0;
		};
		mutable NetworkStateSizeStats last_network_state_size_stats;
		static const int INPUT_BUFFER_LEN = STATE_BUFFER_LEN;
		PlayerInput* input_buffer = nullptr;
		static constexpr int PLAYER_INDEX_LOOKUP_SIZE = 2048;
		static constexpr int PLAYER_INDEX_LOOKUP_MASK = PLAYER_INDEX_LOOKUP_SIZE - 1;
		static constexpr int32_t PLAYER_INDEX_LOOKUP_EMPTY = -2147483647 - 1;
		struct VehicleTickSoA {
			int capacity = 0;
			PlayerInput* inputs = nullptr;
			float* pre_distances = nullptr;
			float* placement_distances = nullptr;
			int* placement_indices = nullptr;
			bool placement_order_valid = false;
			uint8_t* pending_s_boost_sparks = nullptr;
			int* collision_indices = nullptr;
			int collision_order_count = 0;
			float* collision_min_x = nullptr;
			float* collision_max_x = nullptr;
			float* collision_min_y = nullptr;
			float* collision_max_y = nullptr;
			float* collision_min_z = nullptr;
			float* collision_max_z = nullptr;
			float* position_current_x = nullptr;
			float* position_current_y = nullptr;
			float* position_current_z = nullptr;
			float* position_old_x = nullptr;
			float* position_old_y = nullptr;
			float* position_old_z = nullptr;
			float* speed_kmh = nullptr;
			float* collectable_super_spark = nullptr;
		};
		VehicleTickSoA vehicle_tick_soa;
		static const int SUPER_SPARK_CAPACITY = 256;
		static constexpr float SUPER_SPARK_COLLECT_RADIUS = 8.0f;
		struct SuperSpark {
			uint8_t active = 0;
			uint8_t collectable = 0;
			uint16_t animation_frame = 0;
			uint16_t checkpoint = 0;
			SimVec3 position;
			SimVec3 prev_position;
			SimVec3 start_position;
			SimVec3 final_position;
			SimVec3 plane_normal;
		};
		struct SuperSparkState {
			SuperSpark sparks[SUPER_SPARK_CAPACITY];
			uint16_t cursor;
			uint32_t rng_state;
			uint32_t placement_timer;
		};
		SuperSparkState* super_spark_state = nullptr;
		SuperSpark* super_sparks = nullptr;
		std::vector<uint64_t> super_spark_candidate_mask_lo;
		std::vector<uint64_t> super_spark_candidate_mask_hi;
		godot::MultiMeshInstance3D* spark_multimesh_instance = nullptr;
		int32_t* car_player_ids = nullptr;
		uint8_t* car_is_cpu = nullptr;
		int32_t player_index_lookup_ids[PLAYER_INDEX_LOOKUP_SIZE] = {};
		int16_t player_index_lookup_indices[PLAYER_INDEX_LOOKUP_SIZE] = {};
		struct NativeCpuDriverState {
			int32_t player_id = -1;
			uint8_t active = 0;
			int32_t last_generated_tick = -1;
			godot::PackedByteArray pending_input;
		};
		struct RaceEvent {
			uint8_t type = 0;
			int32_t actor_id = -1;
			int32_t target_id = -1;
			int32_t tick = 0;
			int32_t value = 0;
		};
		std::vector<RaceEvent> race_events;
		std::vector<NativeCpuDriverState> native_cpu_drivers;
		BumperState bumper_states[BUMPER_POOL_SIZE];
		void reset_super_sparks();
		void update_super_sparks();
		void update_super_spark_visuals();
		float compute_car_distance_along_track(const PhysicsCar& car) const;
		float compute_vehicle_distance_along_track(uint16_t current_checkpoint, float checkpoint_fraction, uint8_t lap) const;
		uint16_t compute_s_boost_duration_frames(float gap_distance) const;
		void configure_native_cpu_drivers();
		void clear_player_index_lookup();
		void insert_player_index_lookup(int32_t player_id, int car_index);
		int find_car_index_for_player(int32_t player_id) const;
		void update_native_cpu_drivers();
		void update_native_cpu_driver(int car_index);
		void fill_native_cpu_player_inputs_for_frame(PlayerInput* out_inputs,
			uint8_t* out_present,
			const int32_t* expected_player_ids,
			const uint8_t* expected_cpu_flags,
			int input_count,
			int expected_tick);
		void fill_contiguous_native_cpu_player_inputs_for_frame(PlayerInput* out_inputs,
			uint8_t* out_present,
			const int32_t* expected_player_ids,
			int input_count,
			int expected_tick);
		bool has_contiguous_native_cpu_player_order(const int32_t* expected_player_ids, int input_count) const;
		PlayerInput generate_native_cpu_player_input_for_car_index(int car_index, int player_id, int expected_tick);
		PlayerInput generate_native_cpu_player_input_for_tick(int player_id, int expected_tick);
		godot::PackedByteArray generate_native_cpu_input_for_tick(int player_id, int expected_tick);
		NativeCpuDriverState* find_native_cpu_driver(int32_t player_id);
		void configure_bumper_car(int bumper_slot);
		void deactivate_bumper_car(int bumper_slot);
		void set_bumper_track_state(int bumper_slot, float absolute_distance, float lane_offset, bool reset_history);
		void update_bumpers(float lead_distance, int leader_lap);
		void update_bumper_vehicles();
		void collide_racers_with_bumpers();
		void save_bumper_states_to_saved_state(SavedState& state) const;
		void restore_bumper_states_from_saved_state(const SavedState& state);
		void update_saved_voice_transforms(SavedState& state) const;
		bool sample_track_transform_at_distance(float absolute_distance, float lane, SimTransform& out_transform, uint16_t& out_checkpoint, float& out_fraction) const;
		PlayerInput generate_bumper_input_for_slot(int bumper_slot) const;
		void process_pending_ko_events();
		void update_render_visual_snapshots(int visual_count);
		void apply_render_multimeshes(float alpha);
		void update_native_gameplay_camera(bool step_camera);
		uint64_t render_profile_now_us() const;
		enum class InputFrameMode : uint8_t {
			SingleLocal,
			DecodedCarArray,
			DecodedQuantizedCarArray,
		};
		void tick_gamesim_internal(InputFrameMode mode,
			int local_player_id,
			const PlayerInput* local_input,
			const PlayerInput* decoded_car_inputs,
			const uint8_t* decoded_car_input_present,
			int decoded_car_input_count,
			PlayerInput* out_authoritative_inputs = nullptr,
			uint8_t* out_authoritative_present = nullptr,
			bool store_input_history = true);
		void ensure_vehicle_tick_soa_capacity(int capacity);
		void free_vehicle_tick_soa();
		godot::PackedByteArray serialize_network_state(int target_tick) const;
		bool deserialize_network_state(int target_tick, const godot::PackedByteArray& data);
		void rebuild_static_state_after_network_load();
		void rebuild_road_samples_after_state_load();
		static constexpr int VEHICLE_WORKER_COUNT = MXT_VEHICLE_SHARD_COUNT;
		struct VehicleLaneGroup {
			int count = 1;
			std::atomic<int> waiting{0};
			std::atomic<uint32_t> generation{0};
			void reset(int p_count);
			void sync();
		};
		using VehicleLaneFn = void (*)(void*, int, VehicleLaneGroup&);
		void run_vehicle_lanes(int lane_count, bool parallel, void* context, VehicleLaneFn fn);
		void ensure_vehicle_lane_workers();
		void stop_vehicle_lane_workers();
		std::thread vehicle_lane_workers[VEHICLE_WORKER_COUNT - 1];
		std::mutex vehicle_lane_mutex;
		std::condition_variable vehicle_lane_cv;
		std::condition_variable vehicle_lane_done_cv;
		VehicleLaneGroup vehicle_lane_group;
		TrackQueryScratch vehicle_lane_track_scratch[VEHICLE_WORKER_COUNT];
		VehicleLaneFn vehicle_lane_fn = nullptr;
		void* vehicle_lane_context = nullptr;
		uint32_t vehicle_lane_generation = 0;
		int vehicle_lane_active_count = 0;
		int vehicle_lane_pending = 0;
		bool vehicle_lane_workers_started = false;
		bool vehicle_lane_stop = false;

	protected:
		static void _bind_methods();

	public:
		bool sim_started;
		RaceTrack* current_track;
		int num_cars;
		PhysicsCar* cars;
		PhysicsCarProperties* car_properties_array = nullptr;
		PhysicsCar* bumper_cars = nullptr;
		PhysicsCarProperties* bumper_properties_array = nullptr;
		godot::Node3D* car_node_container = nullptr;
		godot::Node3D* spark_node_container = nullptr;
		godot::Object* car_render_manager = nullptr;
		std::vector<godot::Node3D*> render_car_transform_nodes;
		std::vector<godot::Ref<godot::MultiMesh>> render_car_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_outline_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_outline_main_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_shadow_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_stamp_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_thruster_multimeshes;
		std::vector<SimTransform> render_car_local_transforms;
		std::vector<SimTransform> render_outline_local_transforms;
		std::vector<SimTransform> render_outline_main_local_transforms;
		std::vector<SimTransform> render_shadow_local_transforms;
		std::vector<SimTransform> render_stamp_local_transforms;
		std::vector<std::vector<SimTransform>> render_thruster_local_transforms;
		std::vector<float> render_thruster_current_thrust;
		std::vector<int> render_car_archetype_indices;
		std::vector<int> render_car_slots;
		std::vector<int> render_visible_car_slots;
		std::vector<int> render_visible_thruster_slots;
		std::vector<int> render_visible_counts;
		std::vector<int> render_visible_thruster_counts;
		int render_last_body_instances = 0;
		int render_last_thruster_instances = 0;
			std::vector<SimTransform> render_visual_prev_transforms;
			std::vector<SimTransform> render_visual_current_transforms;
			std::vector<SimTransform> render_final_prev_transforms;
			std::vector<SimTransform> render_final_current_transforms;
			std::vector<float> render_visual_prev_ground_distances;
			std::vector<float> render_visual_current_ground_distances;
			std::vector<uint8_t> render_visual_initialized;
			std::vector<SimTransform> render_rollback_corrections;
			std::vector<uint8_t> render_rollback_correction_active;
			std::vector<SimTransform> render_rollback_capture_transforms;
			bool render_rollback_capture_pending = false;
			struct RenderVehicleVisualState {
				float startup_wobble = 0.0f;
				float turn_reaction_effect = 0.0f;
				float height_adjust_from_boost = 0.0f;
				int strafe_visual_roll = 0;
				SimQuat visual_quat;
			};
			std::vector<RenderVehicleVisualState> render_vehicle_visual_state;
			struct RenderVehicleEffectRefs {
				uint32_t terrain_state_old = 0;
				uint32_t machine_state_old = 0;
				uint8_t full_effect_active = 0;
				godot::Color overlay = godot::Color(0, 0, 0, 1);
				godot::Color energy_overlay = godot::Color(0, 0, 0, 1);
			};
			std::vector<RenderVehicleEffectRefs> render_vehicle_effect_refs;
			std::vector<uint8_t> render_effect_full_flags;
			struct RenderEffectPoolSlot {
				godot::Node* node = nullptr;
				godot::Node3D* car_transform = nullptr;
				godot::GPUParticles3D* recharge_particles = nullptr;
				godot::GPUParticles3D* attack_particles = nullptr;
				godot::GPUParticles3D* landing_particles = nullptr;
				godot::GPUParticles3D* damage_electricity = nullptr;
				godot::GPUParticles3D* damage_smoke = nullptr;
				godot::Ref<godot::Material> damage_electricity_material;
				godot::Object* boost_electricity = nullptr;
				int car_index = -1;
				uint8_t fixed_local = 0;
			};
			std::vector<RenderEffectPoolSlot> render_effect_pool_slots;
			struct RenderThrusterLightRID {
				godot::RID light;
				godot::RID instance;
			};
			std::vector<RenderThrusterLightRID> render_thruster_lights;
			godot::RID render_thruster_light_scenario;
			int render_thruster_light_visible_count = 0;
			bool render_node_effects_enabled = true;
			bool render_thruster_lights_enabled = true;
			void cache_native_visual_effect_nodes();
			void update_native_visual_effects(int visual_count, float alpha, bool step_effects, float effect_delta, bool step_electricity);
			void clear_render_thruster_lights();
			void ensure_render_thruster_light_capacity(int capacity);
			void hide_unused_render_thruster_lights(int used_count);
			godot::Camera3D* gameplay_camera_node = nullptr;
			godot::Camera3D* render_camera_node = nullptr;
			bool render_profile_enabled = false;
			bool phase_profile_enabled = false;
			uint64_t phase_profile_frames = 0;
			uint64_t phase_profile_total_us = 0;
			uint64_t phase_profile_pre_us = 0;
			uint64_t phase_profile_input_us = 0;
			uint64_t phase_profile_vehicle_us = 0;
			uint64_t phase_profile_vehicle_begin_us = 0;
			uint64_t phase_profile_vehicle_apply_input_us = 0;
			uint64_t phase_profile_vehicle_floor_us = 0;
			uint64_t phase_profile_vehicle_prepare_frame_us = 0;
			uint64_t phase_profile_vehicle_floor_corner_analytic_surface_us = 0;
			uint64_t phase_profile_vehicle_floor_mesh_candidate_collect_us = 0;
			uint64_t phase_profile_vehicle_floor_mesh_cast4_us = 0;
			uint64_t phase_profile_vehicle_floor_mesh_sample_us = 0;
			uint64_t phase_profile_vehicle_find_floor_us = 0;
			uint64_t phase_profile_vehicle_find_floor_cast_us = 0;
			uint64_t phase_profile_vehicle_find_floor_mesh_us = 0;
			uint64_t phase_profile_vehicle_find_floor_analytic_us = 0;
			uint64_t phase_profile_vehicle_terrain_us = 0;
			uint64_t phase_profile_vehicle_trigger_us = 0;
			uint64_t phase_profile_vehicle_motion_us = 0;
			uint64_t phase_profile_vehicle_finish_tick_us = 0;
			uint64_t phase_profile_vehicle_collision_us = 0;
			uint64_t phase_profile_vehicle_post_tick_us = 0;
			uint64_t phase_profile_vehicle_corner_update_us = 0;
			uint64_t phase_profile_vehicle_corner_old_analytic_us = 0;
			uint64_t phase_profile_vehicle_corner_new_checkpoint_us = 0;
			uint64_t phase_profile_vehicle_corner_new_analytic_us = 0;
			uint64_t phase_profile_vehicle_corner_mesh_us = 0;
			uint64_t phase_profile_vehicle_tail_us = 0;
			uint64_t phase_profile_vehicle_checkpoint_us = 0;
			uint64_t phase_profile_vehicle_spark_collect_us = 0;
			uint64_t phase_profile_post_vehicle_us = 0;
			uint64_t phase_profile_placement_us = 0;
			uint64_t phase_profile_post_us = 0;
			uint64_t phase_profile_save_us = 0;
			uint64_t phase_profile_save_bumper_us = 0;
			uint64_t phase_profile_save_voice_us = 0;
			uint64_t phase_profile_save_memcpy_us = 0;
			uint64_t render_profile_frames = 0;
			uint64_t render_profile_total_us = 0;
			uint64_t render_profile_get_children_us = 0;
			uint64_t render_profile_cache_us = 0;
			uint64_t render_profile_snapshots_us = 0;
			uint64_t render_profile_effects_us = 0;
			uint64_t render_profile_multimesh_us = 0;
			uint64_t render_profile_body_instances = 0;
			uint64_t render_profile_thruster_instances = 0;
			uint64_t render_profile_camera_us = 0;
			uint64_t render_profile_local_visual_us = 0;
			uint64_t render_profile_cpu_driver_us = 0;
			uint64_t render_profile_spark_us = 0;
			uint64_t render_profile_visuals_only_frames = 0;
			uint64_t render_profile_visuals_only_total_us = 0;
			uint64_t render_profile_visuals_only_effects_us = 0;
			uint64_t render_profile_visuals_only_multimesh_us = 0;
			uint64_t render_profile_visuals_only_body_instances = 0;
			uint64_t render_profile_visuals_only_thruster_instances = 0;
			uint64_t render_profile_visuals_only_camera_us = 0;
		godot::Ref<godot::FzgxGameplayCamera> gameplay_camera;
		int gameplay_camera_player_id = -1;
		int spawn_seed = 0;
		std::vector<int> start_grid_slots;
		bool vehicle_restore_enabled = true;
		bool multiplayer_intro_camera_enabled = false;
		bool bumpers_enabled = false;
		bool s_boost_enabled = true;
		int bumper_count = 0;
		uint32_t bumper_track_seed = 0;
		uint8_t bumper_scheduler_lap = 0;
		uint32_t bumper_next_sequence = 0;
		uint32_t start_countdown_extra_frames = 0;

		GameSim();
		~GameSim();

		void set_sim_started(const bool p_sim_started);
		bool get_sim_started();
		void set_spawn_seed(int p_seed) { spawn_seed = p_seed; }
		void set_start_grid_slots(godot::PackedInt32Array p_slots);
		void set_vehicle_restore_enabled(bool enabled) { vehicle_restore_enabled = enabled; }
		bool get_vehicle_restore_enabled() const { return vehicle_restore_enabled; }
		void set_multiplayer_intro_camera_enabled(bool enabled);
		bool get_multiplayer_intro_camera_enabled() const { return multiplayer_intro_camera_enabled; }
		void set_bumpers_enabled(bool enabled);
		bool get_bumpers_enabled() const { return bumpers_enabled; }
		void set_s_boost_enabled(bool enabled);
		bool get_s_boost_enabled() const { return s_boost_enabled; }
		void set_car_node_container(godot::Node3D* p_car_node_container) { car_node_container = p_car_node_container; }
		godot::Node3D* get_car_node_container() const { return car_node_container; }
		void set_spark_node_container(godot::Node3D* p_spark_node_container) { spark_node_container = p_spark_node_container; }
		godot::Node3D* get_spark_node_container() const { return spark_node_container; }
		void set_car_render_manager(godot::Object* p_car_render_manager);
		void set_gameplay_camera(godot::Camera3D* p_camera, int player_id);
		void set_render_camera(godot::Camera3D* p_camera);
		void tick_singleplayer(int local_player_id, godot::PackedByteArray local_input);
		godot::String get_phase_profile_string() const;
		void set_phase_profile_enabled(bool enabled);
		godot::String get_render_profile_string() const;
		void set_render_profile_enabled(bool enabled);
		void set_render_node_effects_enabled(bool enabled);
		void set_render_thruster_lights_enabled(bool enabled);
		int get_player_race_place(int player_id) const;
		godot::PackedInt32Array get_race_leaderboard_window(int player_id, int max_entries) const;
		bool is_player_race_finished(int player_id) const;
		bool is_player_race_eliminated(int player_id) const;
		double get_player_ko_energy_bonus(int player_id) const;
		void set_player_ko_energy_bonus(int player_id, double bonus);
		double get_player_lap_distance(int player_id) const;
		int get_player_lap(int player_id) const;
		int get_player_level_start_time(int player_id) const;
		godot::String get_player_debug_string(int player_id) const;
		godot::String get_bumper_debug_string() const;
		godot::Array get_race_order();
		godot::Transform3D get_player_render_transform(int player_id) const;
		godot::Transform3D get_player_physical_render_transform(int player_id) const;
		godot::Vector3 get_player_physical_render_up(int player_id) const;
		godot::Transform3D get_car_render_transform(int car_index) const;
		godot::Transform3D get_saved_player_voice_transform(int player_id, int target_tick) const;
		godot::Array get_saved_player_voice_transforms(int target_tick) const;
		godot::Array get_check_warning_candidates(int player_id) const;
		godot::Array consume_race_events();
		void instantiate_gamesim(StreamPeerBuffer* in_buffer, godot::Array car_prop_buffers, godot::Array accel_settings);
		void destroy_gamesim();
		void render_gamesim();
		void render_gamesim_visuals_only(double process_delta);
		godot::PackedByteArray get_native_cpu_input_for_tick(int player_id, int expected_tick);
		godot::Dictionary get_input_frame_as_dictionary(int target_tick) const;
		void set_player_metadata(godot::Array player_ids, godot::Array cpu_flags);
		void save_state();
		void load_state(int target_tick);
		bool load_state_data(int target_tick, godot::PackedByteArray data);
		void finish_render_rollback_correction_capture();
		godot::PackedByteArray get_state_data(int target_tick) const;
		godot::PackedByteArray get_full_state_data(int target_tick);
		bool load_full_state_data(int target_tick, godot::PackedByteArray data);
		godot::Dictionary get_network_state_size_stats() const;
		void set_state_data(int target_tick, godot::PackedByteArray data);
		void fix_pointers();
		godot::Array get_dip_switches() const;
		bool is_dip_switch_enabled(int flag) const;
		void set_dip_switch_enabled(int flag, bool enabled);
		double get_first_lap_distance() const;
		double get_track_lap_length() const;
		void emit_super_sparks_from_car(const PhysicsCar& car, int count);
	};

}

#endif
