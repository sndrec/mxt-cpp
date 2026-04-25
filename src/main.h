#ifndef GAME_SIM
#define GAME_SIM

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/classes/multi_mesh_instance3d.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/array.hpp"
#include "track/racetrack.h"
#include "mxt_core/heap_handler.h"
#include "mxt_core/player_input.h"
#include "car/car_properties.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace godot {

	class GameSim : public Node {
		GDCLASS(GameSim, Node)

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
			uint8_t grounded = 0;
			uint16_t checkpoint = 0;
			SimVec3 position;
			SimVec3 velocity;
			SimVec3 plane_normal;
			float plane_d = 0.0f;
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
		void reset_super_sparks();
		void update_super_sparks();
		void update_super_spark_visuals();
		float compute_car_distance_along_track(const PhysicsCar& car) const;
		float compute_vehicle_distance_along_track(uint16_t current_checkpoint, float checkpoint_fraction, uint8_t lap) const;
		uint16_t compute_s_boost_duration_frames(float gap_distance) const;
		godot::PackedByteArray build_cpu_observation(const PhysicsCar& car) const;
		void ensure_vehicle_tick_soa_capacity(int capacity);
		void free_vehicle_tick_soa();
		void record_phase_profile_sample();
		void record_render_profile_sample(const uint32_t sample[RENDER_PROFILE_FIELD_COUNT]);
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
		void tick_gamesim(godot::Array player_inputs);
		godot::String get_phase_profile_string() const;
		godot::String get_render_profile_string() const;
		void instantiate_gamesim(StreamPeerBuffer* in_buffer, godot::Array car_prop_buffers, godot::Array accel_settings);
		void destroy_gamesim();
		void render_gamesim();
		void set_cpu_driver_manager(godot::Object* manager);
		godot::Object* get_cpu_driver_manager() const { return cpu_driver_manager; }
		void set_player_metadata(godot::Array player_ids, godot::Array cpu_flags);
		void save_state();
		void load_state(int target_tick);
		godot::PackedByteArray get_state_data(int target_tick) const;
		void set_state_data(int target_tick, godot::PackedByteArray data);
		void fix_pointers();
		godot::Array get_dip_switches() const;
		bool is_dip_switch_enabled(int flag) const;
		void set_dip_switch_enabled(int flag, bool enabled);
		double get_first_lap_distance() const;
		void emit_super_sparks_from_car(const PhysicsCar& car, int count);
	};

}

#endif
