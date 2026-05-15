#ifndef GAME_SIM
#define GAME_SIM

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/classes/multi_mesh_instance3d.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/camera3d.hpp"
#include "godot_cpp/classes/gpu_particles3d.hpp"
#include "godot_cpp/classes/light3d.hpp"
#include "godot_cpp/classes/sprite3d.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/array.hpp"
#include "fzgx_gameplay_camera.h"
#include "track/racetrack.h"
#include "mxt_core/heap_handler.h"
#include "mxt_core/player_input.h"
#include "car/car_properties.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

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
		struct SavedState {
			char* data;
			int size;
		};
		SavedState state_buffer[STATE_BUFFER_LEN];
		std::vector<char> network_state_live_backup;
		static const int INPUT_BUFFER_LEN = STATE_BUFFER_LEN;
		PlayerInput* input_buffer = nullptr;
		static const int PROFILE_WINDOW_TICKS = 360;
		static const int PROFILE_FIELD_COUNT = 20;
		static const int RENDER_PROFILE_FIELD_COUNT = 9;
		enum ProfileField {
			PROFILE_TOTAL,
			PROFILE_INPUT,
			PROFILE_BEGIN,
			PROFILE_PREPARE_FLOOR,
			PROFILE_PROJECT,
			PROFILE_STEER_SUSP,
			PROFILE_LINEAR,
			PROFILE_INTEGRATE,
			PROFILE_COLLISION,
			PROFILE_POST,
			PROFILE_POST_RESPONSE,
			PROFILE_POST_SAMPLE_OLD,
			PROFILE_POST_CORNERS,
			PROFILE_POST_APPLY_RESPONSE,
			PROFILE_POST_PROJECT_SPEED,
			PROFILE_POST_VISUAL_GEOM,
			PROFILE_POST_DAMAGE_TAIL,
			PROFILE_MISC,
			PROFILE_LANE_GROUP,
			PROFILE_LANES,
		};
		enum RenderProfileField {
			RENDER_PROFILE_TOTAL,
			RENDER_PROFILE_GET_CHILDREN,
			RENDER_PROFILE_VISUAL_APPLY,
			RENDER_PROFILE_CPU_TOTAL,
			RENDER_PROFILE_CPU_BUILD_OBS,
			RENDER_PROFILE_CPU_SUBMIT,
			RENDER_PROFILE_SPARKS,
			RENDER_PROFILE_DEBUG_DRAW,
			RENDER_PROFILE_VIS_CARS,
		};
		uint32_t profile_samples[PROFILE_WINDOW_TICKS][PROFILE_FIELD_COUNT] = {};
		uint64_t profile_sums[PROFILE_FIELD_COUNT] = {};
		int profile_cursor = 0;
		int profile_count = 0;
		uint32_t render_profile_samples[PROFILE_WINDOW_TICKS][RENDER_PROFILE_FIELD_COUNT] = {};
		uint64_t render_profile_sums[RENDER_PROFILE_FIELD_COUNT] = {};
		int render_profile_cursor = 0;
		int render_profile_count = 0;
		struct VehicleTickSoA {
			int capacity = 0;
			PlayerInput* inputs = nullptr;
			float* pre_distances = nullptr;
			float* placement_distances = nullptr;
			int* placement_indices = nullptr;
			uint8_t* pending_s_boost_sparks = nullptr;
			int* collision_indices = nullptr;
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
			uint32_t prof_input_us = 0;
			uint32_t prof_begin_us = 0;
			uint32_t prof_prepare_floor_us = 0;
			uint32_t prof_project_us = 0;
			uint32_t prof_steer_susp_us = 0;
			uint32_t prof_linear_us = 0;
			uint32_t prof_integrate_us = 0;
			uint32_t prof_collision_us = 0;
			uint32_t prof_post_us = 0;
			uint32_t prof_post_response_us = 0;
			uint32_t prof_post_checkpoints_us = 0;
			uint32_t prof_post_sparks_us = 0;
			uint32_t prof_post_sample_old_us = 0;
			uint32_t prof_post_corners_us = 0;
			uint32_t prof_post_apply_response_us = 0;
			uint32_t prof_post_project_speed_us = 0;
			uint32_t prof_post_visual_geom_us = 0;
			uint32_t prof_post_damage_tail_us = 0;
			uint32_t prof_misc_us = 0;
			uint32_t prof_total_us = 0;
			uint32_t prof_lane_group_us = 0;
			uint32_t prof_lanes = 1;
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
		godot::MultiMeshInstance3D* spark_multimesh_instance = nullptr;
		godot::Object* cpu_driver_manager = nullptr;
		int32_t* car_player_ids = nullptr;
		uint8_t* car_is_cpu = nullptr;
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
		void reset_super_sparks();
		void update_super_sparks();
		void update_super_spark_visuals();
		float compute_car_distance_along_track(const PhysicsCar& car) const;
		float compute_vehicle_distance_along_track(uint16_t current_checkpoint, float checkpoint_fraction, uint8_t lap) const;
		uint16_t compute_s_boost_duration_frames(float gap_distance) const;
		godot::PackedByteArray build_cpu_observation(const PhysicsCar& car) const;
		void configure_native_cpu_drivers();
		void update_native_cpu_drivers();
		void update_native_cpu_driver(int car_index);
		godot::PackedByteArray generate_native_cpu_input_for_tick(int player_id, int expected_tick);
		NativeCpuDriverState* find_native_cpu_driver(int32_t player_id);
		void process_pending_ko_events();
		void update_render_visual_snapshots(int visual_count);
		void apply_render_multimeshes(float alpha);
		void update_native_gameplay_camera(bool step_camera);
		enum class InputFrameMode : uint8_t {
			SingleLocal,
			DecodedCarArray,
		};
		void tick_gamesim_internal(InputFrameMode mode,
			int local_player_id,
			const PlayerInput* local_input,
			const PlayerInput* decoded_car_inputs,
			const uint8_t* decoded_car_input_present,
			int decoded_car_input_count);
		void ensure_vehicle_tick_soa_capacity(int capacity);
		void free_vehicle_tick_soa();
		void record_phase_profile_sample();
		void record_render_profile_sample(const uint32_t sample[RENDER_PROFILE_FIELD_COUNT]);
		godot::PackedByteArray serialize_network_state(int target_tick) const;
		bool deserialize_network_state(int target_tick, const godot::PackedByteArray& data);
		void rebuild_static_state_after_network_load();
		static constexpr int VEHICLE_WORKER_COUNT = 4;
		struct VehicleLaneGroup {
			int count = 1;
			int waiting = 0;
			uint32_t generation = 0;
			std::mutex mutex;
			std::condition_variable cv;
			void reset(int p_count);
			void sync();
		};
		void run_vehicle_lanes(int lane_count, bool parallel, const std::function<void(int, VehicleLaneGroup&)>& fn);
		void ensure_vehicle_lane_workers();
		void stop_vehicle_lane_workers();
		std::thread vehicle_lane_workers[VEHICLE_WORKER_COUNT - 1];
		std::mutex vehicle_lane_mutex;
		std::condition_variable vehicle_lane_cv;
		std::condition_variable vehicle_lane_done_cv;
		VehicleLaneGroup vehicle_lane_group;
		TrackQueryScratch vehicle_lane_track_scratch[VEHICLE_WORKER_COUNT];
		std::function<void(int, VehicleLaneGroup&)> vehicle_lane_fn;
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
		godot::Node3D* car_node_container = nullptr;
		godot::Node3D* spark_node_container = nullptr;
		godot::Object* car_render_manager = nullptr;
		std::vector<godot::Node3D*> render_car_transform_nodes;
		std::vector<godot::Ref<godot::MultiMesh>> render_car_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_outline_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_outline_main_multimeshes;
		std::vector<godot::Ref<godot::MultiMesh>> render_shadow_multimeshes;
		std::vector<SimTransform> render_car_local_transforms;
		std::vector<SimTransform> render_outline_local_transforms;
		std::vector<SimTransform> render_outline_main_local_transforms;
		std::vector<SimTransform> render_shadow_local_transforms;
		std::vector<int> render_car_archetype_indices;
		std::vector<int> render_car_slots;
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
			struct RenderThrusterVisualRefs {
				godot::Node3D* root = nullptr;
				godot::GPUParticles3D* particles = nullptr;
				godot::Sprite3D* sprite = nullptr;
				godot::Light3D* light = nullptr;
				float current_thrust = 0.0f;
			};
			struct RenderVehicleEffectRefs {
				godot::Node3D* car_transform = nullptr;
				godot::GPUParticles3D* recharge_particles = nullptr;
				godot::GPUParticles3D* attack_particles = nullptr;
				godot::GPUParticles3D* landing_particles = nullptr;
				godot::GPUParticles3D* damage_electricity = nullptr;
				godot::GPUParticles3D* damage_smoke = nullptr;
				godot::Object* damage_electricity_material = nullptr;
				godot::Object* boost_electricity = nullptr;
				std::vector<RenderThrusterVisualRefs> thrusters;
				uint32_t terrain_state_old = 0;
				uint32_t machine_state_old = 0;
				godot::Color overlay = godot::Color(0, 0, 0, 1);
				godot::Color energy_overlay = godot::Color(0, 0, 0, 1);
			};
			std::vector<RenderVehicleEffectRefs> render_vehicle_effect_refs;
			void cache_native_visual_effect_nodes();
			void update_native_visual_effects(int visual_count, float alpha, bool step_effects, float effect_delta, bool step_electricity);
			godot::Camera3D* gameplay_camera_node = nullptr;
		godot::Ref<godot::FzgxGameplayCamera> gameplay_camera;
		int gameplay_camera_player_id = -1;
		int spawn_seed = 0;

		GameSim();
		~GameSim();

		void set_sim_started(const bool p_sim_started);
		bool get_sim_started();
		void set_spawn_seed(int p_seed) { spawn_seed = p_seed; }
		void set_car_node_container(godot::Node3D* p_car_node_container) { car_node_container = p_car_node_container; }
		godot::Node3D* get_car_node_container() const { return car_node_container; }
		void set_spark_node_container(godot::Node3D* p_spark_node_container) { spark_node_container = p_spark_node_container; }
		godot::Node3D* get_spark_node_container() const { return spark_node_container; }
		void set_car_render_manager(godot::Object* p_car_render_manager);
		void set_gameplay_camera(godot::Camera3D* p_camera, int player_id);
		void tick_singleplayer(int local_player_id, godot::PackedByteArray local_input);
		godot::String get_phase_profile_string() const;
		godot::String get_render_profile_string() const;
		int get_player_race_place(int player_id) const;
		bool is_player_race_finished(int player_id) const;
		double get_player_lap_distance(int player_id) const;
		int get_player_lap(int player_id) const;
		godot::Array get_race_order();
		godot::Transform3D get_player_render_transform(int player_id) const;
		godot::Array get_check_warning_candidates(int player_id) const;
		godot::Array consume_race_events();
		void instantiate_gamesim(StreamPeerBuffer* in_buffer, godot::Array car_prop_buffers, godot::Array accel_settings);
		void destroy_gamesim();
		void render_gamesim();
		void render_gamesim_visuals_only(double process_delta);
		void set_cpu_driver_manager(godot::Object* manager);
		godot::Object* get_cpu_driver_manager() const { return cpu_driver_manager; }
		godot::PackedByteArray get_native_cpu_input_for_tick(int player_id, int expected_tick);
		void set_player_metadata(godot::Array player_ids, godot::Array cpu_flags);
		void save_state();
		void load_state(int target_tick);
		void finish_render_rollback_correction_capture();
		godot::PackedByteArray get_state_data(int target_tick) const;
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
