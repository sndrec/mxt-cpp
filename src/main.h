#ifndef GAME_SIM
#define GAME_SIM

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/node3D.hpp"
#include "godot_cpp/classes/multi_mesh_instance3d.hpp"
#include "godot_cpp/classes/multi_mesh.hpp"
#include "godot_cpp/classes/stream_peer_buffer.hpp"
#include "godot_cpp/variant/array.hpp"
#include "track/racetrack.h"
#include "mxt_core/heap_handler.h"
#include "mxt_core/mtxa_stack.hpp"
#include "mxt_core/player_input.h"
#include "car/car_properties.h"

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
		static const int SUPER_SPARK_CAPACITY = 256;
		static constexpr float SUPER_SPARK_COLLECT_RADIUS = 8.0f;
		struct SuperSpark {
			uint8_t active = 0;
			uint8_t grounded = 0;
			uint16_t checkpoint = 0;
			godot::Vector3 position;
			godot::Vector3 velocity;
			godot::Vector3 plane_normal;
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
		godot::Object* cpu_driver_manager = nullptr;
		int32_t* car_player_ids = nullptr;
		uint8_t* car_is_cpu = nullptr;
		void reset_super_sparks();
		void update_super_sparks();
		void update_super_spark_visuals();
		float compute_car_distance_along_track(const PhysicsCar& car) const;
		uint16_t compute_s_boost_duration_frames(float gap_distance) const;
		godot::PackedByteArray build_cpu_observation(const PhysicsCar& car) const;

	protected:
		static void _bind_methods();

	public:
		bool sim_started;
		RaceTrack* current_track;
		int num_cars;
		PhysicsCar* cars;
		PhysicsCarProperties* car_properties_array = nullptr;
		MtxStack mtxa;
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
