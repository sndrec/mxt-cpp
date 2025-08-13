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
#include "ai/observation.h"

namespace godot {

	class GameSim : public Node {
		GDCLASS(GameSim, Node)

	private:
		int tick;
		float tick_delta;
		HeapHandler level_data;
		HeapHandler gamestate_data;
		HeapHandler ai_data; // AI-only heap (not part of save/load state)
		static const int STATE_BUFFER_LEN = 45;
		struct SavedState {
			char* data;
			int size;
		};
		SavedState state_buffer[STATE_BUFFER_LEN];
		static const int INPUT_BUFFER_LEN = STATE_BUFFER_LEN;
		PlayerInput* input_buffer = nullptr;

		// Cached observations per car, validated once per tick (pre-tick)
		AgentObservation* ai_observations = nullptr;
		bool ai_obs_valid = false;

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

		// Bot controllers per car (optional)
		void **bot_slots = nullptr; // opaque to avoid header deps

		GameSim();
		~GameSim();

		void set_sim_started(const bool p_sim_started);
		bool get_sim_started();
		void set_car_node_container(godot::Node3D* p_car_node_container) { car_node_container = p_car_node_container; }
		godot::Node3D* get_car_node_container() const { return car_node_container; }
		void tick_gamesim(godot::Array player_inputs);
		void instantiate_gamesim(StreamPeerBuffer* in_buffer, godot::Array car_prop_buffers, godot::Array accel_settings);
		void destroy_gamesim();
		void render_gamesim();
		void update_observations();
		void save_state();
		void load_state(int target_tick);
		godot::PackedByteArray get_state_data(int target_tick) const;
		void set_state_data(int target_tick, godot::PackedByteArray data);
		void fix_pointers();
		// RL helpers
		godot::PackedFloat32Array get_observation_for_car(int car_index) const;
		bool set_bot_model(int car_index, godot::PackedInt32Array layer_sizes, godot::PackedFloat32Array weights);
		void clear_bot(int car_index);
		void clear_all_bots();
		void set_car_retired(int car_index, bool retired);
		godot::Dictionary get_training_info() const;

	};

}

#endif